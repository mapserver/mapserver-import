#include <time.h>

#include "map.h"
#include "maperror.h"

#ifdef USE_SDE
#include <sdetype.h> /* ESRI SDE Client Includes */
#include <sdeerno.h>

#define MS_SDE_MAXBLOBSIZE 1024*50 // 50 kbytes
#define MS_SDE_NULLSTRING "<null>"
#define MS_SDE_SHAPESTRING "<shape>"
#define MS_SDE_TIMEFMTSIZE 128 // bytes
#define MS_SDE_TIMEFMT "%T %m/%d/%Y"

typedef struct { 
  SE_CONNECTION connection;
  SE_LAYERINFO layerinfo;
  SE_COORDREF coordref;
  SE_STREAM stream;

  SE_COLUMN_DEF *itemdefs; // relative to a stream query

  char *table, *column;
} sdeLayerObj;

/*
** Start SDE/MapServer helper functions.
*/
static void sde_error(long error_code, char *routine, char *sde_routine) {
  char error_string[SE_MAX_MESSAGE_LENGTH];

  error_string[0] = '\0';
  SE_error_get_string(error_code, error_string);

  sprintf(ms_error.message, "%s: %s. (%ld)", sde_routine, error_string, error_code);
  msSetError(MS_SDEERR, ms_error.message, routine);

  return;
}

static int sdeShapeCopy(SE_SHAPE inshp, shapeObj *outshp) {
  long numparts, numsubparts, numpoints;
  long *subparts=NULL;
  SE_POINT *points=NULL;
  long type, status;

  lineObj line={0,NULL};

  int i,j,k;
  
  SE_shape_get_type(inshp, &type);

  if(type == SG_NIL_SHAPE) return(MS_SUCCESS); // skip null shapes  
  switch(type) {
  case(SG_POINT_SHAPE):
  case(SG_MULTI_POINT_SHAPE):
    outshp->type = MS_SHAPE_POINT;
    break;
  case(SG_LINE_SHAPE):
  case(SG_SIMPLE_LINE_SHAPE): 
  case(SG_MULTI_LINE_SHAPE):
  case(SG_MULTI_SIMPLE_LINE_SHAPE):
    outshp->type = MS_SHAPE_LINE;
    break;
  case(SG_AREA_SHAPE):
  case(SG_MULTI_AREA_SHAPE):
    outshp->type = MS_SHAPE_POLYGON;
    break;
  default:
    msSetError(MS_SDEERR, "Unsupported SDE shape type.", "sdeCopyShape()");
    return(MS_FAILURE);
  }

  SE_shape_get_num_points(inshp, 0, 0, &numpoints);
  SE_shape_get_num_parts(inshp, &numparts, &numsubparts);

  if(numsubparts > 0) {
    subparts = (long *)malloc(numsubparts*sizeof(long));
    if(!subparts) {
      msSetError(MS_MEMERR, "Unable to allocate parts array.", "sdeCopyShape()");
      return(MS_FAILURE);
    }
    
  }

  points = (SE_POINT *)malloc(numpoints*sizeof(SE_POINT));
  if(!points) {
    msSetError(MS_MEMERR, "Unable to allocate points array.", "sdeCopyShape()");
    return(MS_FAILURE);
  }

  status = SE_shape_get_all_points(inshp, SE_DEFAULT_ROTATION, NULL, subparts, points, NULL, NULL);
  if(status != SE_SUCCESS) {
    sde_error(status, "sdeCopyShape()", "SE_shape_get_all_points()");
    return(MS_FAILURE);
  }

  k = 0; // overall point counter
  for(i=0; i<numsubparts; i++) {
    
    if( i == numsubparts-1)
      line.numpoints = numpoints - subparts[i];
    else
      line.numpoints = subparts[i+1] - subparts[i];

    line.point = (pointObj *)malloc(sizeof(pointObj)*line.numpoints);
    if(!line.point) {
      msSetError(MS_MEMERR, "Unable to allocate temporary point cache.", "sdeShapeCopy()");
      return(MS_FAILURE);
    }
     
    for(j=0; j < line.numpoints; j++) {
      line.point[j].x = points[k].x; 
      line.point[j].y = points[k].y;     
      k++;
    }

    msAddLine(outshp, &line);
    free(line.point);
  }

  free(subparts);
  free(points);

  return(MS_SUCCESS);
}

/*
** Retrieves the current row as setup via the SDE stream query or row fetch routines.
*/
static int sdeGetRecord(layerObj *layer, shapeObj *shape, int skip) {
  int i;
  long status;

  double doubleval;
  long longval;
  struct tm dateval;

  SE_SHAPE shapeval=0;

  sdeLayerObj *sde;

  sde = layer->sdelayer;

  if(layer->numitems > 0) {
    shape->numvalues = layer->numitems;
    shape->values = (char **)malloc(sizeof(char *)*layer->numitems);
    if(!shape->values) {
      msSetError(MS_MEMERR, "Error allocation shape attribute array.", "sdeGetRecord()");
      return(MS_FAILURE);
    }
  }

  status = SE_shape_create(NULL, &shapeval);
  if(status != SE_SUCCESS) {
    sde_error(status, "sdeGetRecord()", "SE_shape_create()");
    return(MS_FAILURE);
  }

  if(skip == 2) { // a ...Next... request (id, shape)
    status = SE_stream_get_integer(sde->stream, 1, &shape->index);
    if(status != SE_SUCCESS) {
      sde_error(status, "sdeGetRecord()", "SE_stream_get_integer()");
      return(MS_FAILURE);
    }

    status = SE_stream_get_shape(sde->stream, 2, shapeval);
    if(status != SE_SUCCESS) { 
      sde_error(status, "sdeGetRecord()", "SE_stream_get_shape()");
      return(MS_FAILURE);
    }
  } 
  
  for(i=0; i<layer->numitems; i++) {
    switch(sde->itemdefs[i].sde_type) {
    case SE_SMALLINT_TYPE:
      status = SE_stream_get_smallint(sde->stream, i+skip+1, (short *) &longval);
      if(status == SE_SUCCESS) {
	shape->values[i] = (char *)malloc(sde->itemdefs[i].size+1);
	sprintf(shape->values[i], "%ld", longval);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {
	sde_error(status, "sdeGetRecord()", "SE_stream_get_smallint()");
	return(MS_FAILURE);
      }      
    case SE_INTEGER_TYPE:
      status = SE_stream_get_integer(sde->stream, i+skip+1, &longval);
      if(status == SE_SUCCESS) {
	shape->values[i] = (char *)malloc(sde->itemdefs[i].size+1);
	sprintf(shape->values[i], "%ld", longval);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {
	sde_error(status, "sdeGetRecord()", "SE_stream_get_integer()");
	return(MS_FAILURE);
      }      
      break;
    case SE_FLOAT_TYPE:
      status = SE_stream_get_float(sde->stream, i+skip+1, (float *) &doubleval);
      if(status == SE_SUCCESS) {
	shape->values[i] = (char *)malloc(sde->itemdefs[i].size+1);
	sprintf(shape->values[i], "%g", doubleval);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {     
	sde_error(status, "sdeGetRecord()", "SE_stream_get_float()");
	return(MS_FAILURE);
      }
      break;
    case SE_DOUBLE_TYPE:
      status = SE_stream_get_double(sde->stream, i+skip+1, &doubleval);
      if(status == SE_SUCCESS) {
	shape->values[i] = (char *)malloc(sde->itemdefs[i].size+1);
	sprintf(shape->values[i], "%g", doubleval);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {     
	sde_error(status, "sdeGetRecord()", "SE_stream_get_double()");
	return(MS_FAILURE);
      }
      break;
    case SE_STRING_TYPE:
      shape->values[i] = (char *)malloc(sde->itemdefs[i].size+1);
      status = SE_stream_get_string(sde->stream, i+skip+1, shape->values[i]);
      if(status != SE_SUCCESS) {
	sde_error(status, "sdeGetRecord()", "SE_stream_get_string()");
	return(MS_FAILURE);
      }
      break;
    case SE_BLOB_TYPE:
      shape->values[i] = strdup("<blob>");
      msSetError(MS_SDEERR, "Retrieval of BLOBs is not yet supported.", "sdeGetRecord()");
      break;
    case SE_DATE_TYPE:
      status = SE_stream_get_date(sde->stream, i+skip+1, &dateval);
      if(status == SE_SUCCESS) {
	shape->values[i] = (char *)malloc(sizeof(char)*MS_SDE_TIMEFMTSIZE);
	strftime(shape->values[i], MS_SDE_TIMEFMTSIZE, MS_SDE_TIMEFMT, &dateval);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {     
	sde_error(status, "sdeGetRecord()", "SE_stream_get_date()");
	return(MS_FAILURE);
      }
      break;
    case SE_SHAPE_TYPE:
      status = SE_stream_get_shape(sde->stream, i+skip+1, shapeval);
      if(status == SE_SUCCESS) {
	shape->values[i] = strdup(MS_SDE_SHAPESTRING);
      } else if(status == SE_NULL_VALUE) {
	shape->values[i] = strdup(MS_SDE_NULLSTRING);
      } else {     
	sde_error(status, "sdeGetRecord()", "SE_stream_get_shape()");
	return(MS_FAILURE);
      }
      break;
    default: 
      msSetError(MS_SDEERR, "Unknown SDE column type.", "sdeGetRecord()");
      return(MS_FAILURE);
      break;
    }
  }

  if(SE_shape_is_nil(shapeval)) return(MS_SUCCESS);
  
  // copy sde shape to a mapserver shape
  status = sdeShapeCopy(shapeval, shape);
  if(status != MS_SUCCESS) return(MS_FAILURE);

  // clean up
  SE_shape_free(shapeval);

  return(MS_SUCCESS);
}
#endif

/*
** Start SDE/MapServer library functions.
*/

// connects, gets basic information and opens a stream
int msSDELayerOpen(layerObj *layer) {
#ifdef USE_SDE
  long status;
  char **params;
  int numparams;

  SE_ERROR error;

  sdeLayerObj *sde;

  if(layer->sdelayer) return MS_SUCCESS; // layer already open

  params = split(layer->connection, ',', &numparams);
  if(!params) {
    msSetError(MS_MEMERR, "Error spliting SDE connection information.", "msSDELayerOpen()");
    return(MS_FAILURE);
  }

  if(numparams < 5) {
    msSetError(MS_SDEERR, "Not enough SDE connection parameters specified.", "msSDELayerOpen()");
    return(MS_FAILURE);
  }

  sde = (sdeLayerObj *) malloc(sizeof(sdeLayerObj));
  if(!sde) {
    msSetError(MS_MEMERR, "Error allocating SDE layer structure.", "msSDELayerOpen()");
    return(MS_FAILURE);
  }
  layer->sdelayer = sde;

  // initialize a few things
  sde->itemdefs = NULL;
  sde->table = sde->column = NULL;

  status = SE_connection_create(params[0], params[1], params[2], params[3], params[4], &error, &(sde->connection));
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_connection_create()");
    return(MS_FAILURE);
  }

  msFreeCharArray(params, numparams);

  params = split(layer->data, ',', &numparams);
  if(!params) {
    msSetError(MS_MEMERR, "Error spliting SDE layer information.", "msSDELayerOpen()");
    return(MS_FAILURE);
  }

  if(numparams != 2) {
    msSetError(MS_SDEERR, "Not enough SDE layer parameters specified.", "msSDELayerOpen()");
    return(MS_FAILURE);
  }

  sde->table = params[0]; // no need to free
  sde->column = params[1];

  SE_layerinfo_create(NULL, &(sde->layerinfo));
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_layerinfo_create()");
    return(MS_FAILURE);
  }

  status = SE_layer_get_info(sde->connection, params[0], params[1], sde->layerinfo);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_layer_get_info()");
    return(MS_FAILURE);
  }

  SE_coordref_create(&(sde->coordref));
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_coordref_create()");
    return(MS_FAILURE);
  }

  status = SE_layerinfo_get_coordref(sde->layerinfo, sde->coordref);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_layerinfo_get_coordref()");
    return(MS_FAILURE);
  }

  status = SE_stream_create(sde->connection, &(sde->stream));
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerOpen()", "SE_stream_create()");
    return(MS_FAILURE);
  }

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerOpen()");
  return(MS_FAILURE);
#endif
}

void msSDELayerClose(layerObj *layer) {
#ifdef USE_SDE
  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if (sde == NULL) return;  // Silently return if layer not opened.

  SE_table_free_descriptions(sde->itemdefs);
  SE_stream_free(sde->stream);
  SE_layerinfo_free(sde->layerinfo);
  SE_coordref_free(sde->coordref);
  SE_connection_free(sde->connection);

  free(layer->sdelayer);
  layer->sdelayer = NULL;

#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerClose()");
  return;
#endif
}

// starts a stream query using spatial filter (and optionally values)
int msSDELayerWhichShapes(layerObj *layer, rectObj rect) {
#ifdef USE_SDE
  char **items=NULL;
  int i, numitems=0;
  long status;

  SE_ENVELOPE envelope;
  SE_SHAPE shape=0;
  SE_SQL_CONSTRUCT *sql;
  SE_FILTER constraint;

  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if(!sde) {
    msSetError(MS_SDEERR, "SDE layer has not been opened.", "msSDELayerGetExtent()");
    return(MS_FAILURE);
  }

  status = SE_shape_create(sde->coordref, &shape);
  // status = SE_shape_create(NULL, &shape);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_shape_create()");
    return(MS_FAILURE);
  }

  status = SE_layerinfo_get_envelope(sde->layerinfo, &envelope);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_layerinfo_get_envelope()");
    return(MS_FAILURE);
  }
  
  if(envelope.minx > rect.maxx) return(MS_DONE); // there is NO overlap, return MS_DONE (FIX: use this in ALL which shapes functions)
  if(envelope.maxx < rect.minx) return(MS_DONE);
  if(envelope.miny > rect.maxy) return(MS_DONE);
  if(envelope.maxy < rect.miny) return(MS_DONE);

  // set spatial constraint search shape
  envelope.minx = MS_MAX(rect.minx, envelope.minx); // crop against SDE layer extent *argh*
  envelope.miny = MS_MAX(rect.miny, envelope.miny);
  envelope.maxx = MS_MIN(rect.maxx, envelope.maxx);
  envelope.maxy = MS_MIN(rect.maxy, envelope.maxy);

  status = SE_shape_generate_rectangle(&envelope, shape);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_shape_generate_rectangle()");
    return(MS_FAILURE);
  }
  constraint.filter.shape = shape;

  // set spatial constraint column and table
  strcpy(constraint.table, sde->table);
  strcpy(constraint.column, sde->column);

  // set a couple of other spatial constraint properties
  constraint.method = SM_ENVP;
  constraint.filter_type = SE_SHAPE_FILTER;
  constraint.truth = TRUE;

  // set up the SQL statement, no joins allowed here
  status = SE_sql_construct_alloc(1, &sql);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_sql_construct_alloc()");
    return(-1);
  }

  strcpy(sql->tables[0], sde->table); // main table

  numitems = layer->numitems + 2; // add both the spatial column and the unique id
  items = (char **)malloc(numitems*sizeof(char *));
  if(!items) {
    msSetError(MS_MEMERR, "Error allocating items array.", "msSDELayerWhichShapes()");
    return(MS_FAILURE);
  }

  items[0] = strdup("SE_ROW_ID"); // row id
  items[1] = strdup(sde->column); // the shape    
  for(i=0; i<layer->numitems; i++)
    items[i+2] = strdup(layer->items[i]); // any other items needed for labeling or classification

  // set the "where" clause
  if(!(layer->filter.string))
    sql->where = strdup("");
  else
    sql->where = strdup(layer->filter.string);

  status = SE_stream_query(sde->stream, numitems, (const char **)items, sql);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_stream_query()");
    return(MS_FAILURE);
  }

  status = SE_stream_set_spatial_constraints(sde->stream, SE_SPATIAL_FIRST, FALSE, 1, &constraint);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_stream_set_spatial_constraints()");
    return(MS_FAILURE);
  }

  status = SE_stream_execute(sde->stream); // *should* be ready to step through shapes now
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerWhichShapes()", "SE_stream_query()");
    return(MS_FAILURE);
  }

  // now that query has been run we can gather item descriptions
  sde->itemdefs = (SE_COLUMN_DEF *) malloc(sizeof(SE_COLUMN_DEF)*layer->numitems); // don't need defs for id or shape
  if(!sde->itemdefs) {
    msSetError(MS_MEMERR, "Error allocating SDE item definition array.", "msSDELayerWhichShapes()");
    return(MS_FAILURE);
  }
  for(i=0; i<layer->numitems; i++) { // column numbers start at one
    status = SE_stream_describe_column(sde->stream, i+3, &(sde->itemdefs[i]));
    if(status != MS_SUCCESS) {
      sde_error(status, "msSDELayerWhichShapes()", "SE_stream_describe_column()");
      return(MS_FAILURE);
    }
  }

  // clean-up
  msFreeCharArray(items, numitems);
  SE_shape_free(shape);
  SE_sql_construct_free(sql);

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerWhichShapes()");
  return(MS_FAILURE);
#endif
}

int msSDELayerNextShape(layerObj *layer, shapeObj *shape) {
#ifdef USE_SDE
  long status;

  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if(!sde) {
    msSetError(MS_SDEERR, "SDE layer has not been opened.", "msSDELayerGetExtent()");
    return(MS_FAILURE);
  }

  // fetch the next record from the stream
  status = SE_stream_fetch(sde->stream);
  if(status == SE_FINISHED)
    return(MS_DONE);
  else if(status != MS_SUCCESS) {
    sde_error(status, "msSDELayerNextShape()", "SE_stream_fetch()");
    return(MS_FAILURE);
  }

  // get the shape and values (first column is the shape id, second is the shape itself)
  status = sdeGetRecord(layer, shape, 2);
  if(status != MS_SUCCESS)
    return(MS_FAILURE); // something went wrong fetching the record/shape

  if(shape->numlines == 0) // null shape, skip it
    return(msSDELayerNextShape(layer, shape));

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerNextShape()");
  return(MS_FAILURE);
#endif
}

int msSDELayerGetItems(layerObj *layer) {
#ifdef USE_SDE
  int i;
  short n;
  long status;

  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if(!sde) {
    msSetError(MS_SDEERR, "SDE layer has not been opened.", "msSDELayerGetExtent()");
    return(MS_FAILURE);
  }

  status = SE_table_describe(sde->connection, sde->table, &n, &sde->itemdefs);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerGetItems()", "SE_table_describe()");
    return(MS_FAILURE);
  }

  layer->items = (char **)malloc(n*sizeof(char *));
  if(!layer->items) {
    msSetError(MS_MEMERR, "Error allocating layer items array.", "msSDELayerGetItems()");
    return(MS_FAILURE);
  }
  
  for(i=0; i<n; i++) 
    layer->items[i] = strdup(sde->itemdefs[i].column_name);    
  layer->numitems = n;

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerGetItems()");
  return(MS_FAILURE);
#endif
}

int msSDELayerGetExtent(layerObj *layer, rectObj *extent) {
#ifdef USE_SDE
  long status;

  SE_ENVELOPE envelope;

  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if(!sde) {
    msSetError(MS_SDEERR, "SDE layer has not been opened.", "msSDELayerGetExtent()");
    return(MS_FAILURE);
  }

  status = SE_layerinfo_get_envelope(sde->layerinfo, &envelope);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerGetExtent()", "SE_layerinfo_get_envelope()");
    return(MS_FAILURE);
  }
  
  extent->minx = envelope.minx;
  extent->miny = envelope.miny;
  extent->maxx = envelope.maxx;
  extent->maxy = envelope.maxy;

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerGetExtent()");
  return(MS_FAILURE);
#endif
}

int msSDELayerGetShape(layerObj *layer, shapeObj *shape, long record) {
#ifdef USE_SDE
  int i;
  long status;

  sdeLayerObj *sde=NULL;

  sde = layer->sdelayer;
  if(!sde) {
    msSetError(MS_SDEERR, "SDE layer has not been opened.", "msSDELayerGetExtent()");
    return(MS_FAILURE);
  }

  if(layer->numitems < 1) { // must be at least one thing to retrieve (i.e. spatial column)
    msSetError(MS_MISCERR, "No items requested, SDE requires at least one item.", "msSDELayerGetShape()");
    return(MS_FAILURE);
  }
  
  status = SE_stream_fetch_row(sde->stream, sde->table, record, layer->numitems, (const char **)layer->items);
  if(status != SE_SUCCESS) {
    sde_error(status, "msSDELayerGetShape()", "SE_table_describe()");
    return(MS_FAILURE);
  }

  if(!sde->itemdefs) {
    sde->itemdefs = (SE_COLUMN_DEF *) malloc(sizeof(SE_COLUMN_DEF)*layer->numitems);
    if(!sde->itemdefs) {
      msSetError(MS_MEMERR, "Error allocating SDE item definition array.", "msSDELayerGetShape()");
      return(MS_FAILURE);
    }
    for(i=0; i<layer->numitems; i++) { // columns start at one
      status = SE_stream_describe_column(sde->stream, i+1, &(sde->itemdefs[i]));
      if(status != MS_SUCCESS) {
	sde_error(status, "msSDELayerGetShape()", "SE_stream_describe_column()");
	return(MS_FAILURE);
      }
    }
  }

  status = sdeGetRecord(layer, shape, 0);
  if(status != MS_SUCCESS)
    return(MS_FAILURE); // something went wrong fetching the record/shape

  return(MS_SUCCESS);
#else
  msSetError(MS_MISCERR, "SDE support is not available.", "msSDELayerGetShape()");
  return(MS_FAILURE);
#endif
}
