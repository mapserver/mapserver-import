#include "map.h"
#include "mapparser.h"

/*
** Match this with with unit enumerations is map.h
*/
static double inchesPerUnit[6]={1, 12, 63360.0, 39.3701, 39370.1, 4374754};

/*
** This function fills the items and data buffers for a given join (record based).
** Now handles one-to-many joins correctly. Cleans up previous join before allocating
** space for the new one. 
*/ 
int msJoinDBFTables(joinObj *join, char *path, char *tile) {
  int i, j, idx;
  DBFHandle hDBF;
  int nrecs, *ids=NULL;

  char old_path[MS_PATH_LENGTH];

  getcwd(old_path, MS_PATH_LENGTH); /* save old working directory */
  if(path) chdir(path);
  if(tile) chdir(tile);

  /* first open the lookup table file */
  if((hDBF = msDBFOpen(join->table, "rb")) == NULL) {
    sprintf(ms_error.message, "(%s)", join->table);
    msSetError(MS_IOERR, ms_error.message, "msJoinDBFTables()");
    chdir(old_path); /* restore old cwd */
    return(-1);
  }

  if((idx = msDBFGetItemIndex(hDBF, join->to)) == -1) { 
    sprintf(ms_error.message, "Item %s not found.", join->to);   
    msSetError(MS_DBFERR, ms_error.message, "msJoinDBFTables()");    
    msDBFClose(hDBF);
    chdir(old_path); /* restore old cwd */
    return(-1);
  }

  /*
  ** Now we need to pull the record and item names in to the join buffers
  */
  join->numitems =  msDBFGetFieldCount(hDBF);
  if(!join->items) {
    join->items = msDBFGetItems(hDBF);
    if(!join->items) {
      chdir(old_path); /* restore old cwd */
      return(-1);
    }
  }

  nrecs = msDBFGetRecordCount(hDBF);

  if(join->type == MS_SINGLE) { /* only one row */
    
    if((join->data = (char ***)malloc(sizeof(char **))) == NULL) {
      msSetError(MS_MEMERR, NULL, "msJoinDBFTables()");
      chdir(old_path); /* restore old cwd */
      return(-1);
    }
    
    for(i=0; i<nrecs; i++) { /* find a match */
      if(strcmp(join->match, msDBFReadStringAttribute(hDBF, i, idx)) == 0)
	break;
    }  
    
    if(i == nrecs) { /* just return zero length strings */
      if((join->data[0] = (char **)malloc(sizeof(char *)*join->numitems)) == NULL) {
	msSetError(MS_MEMERR, NULL, "msJoinDBFTables()");
	chdir(old_path); /* restore old cwd */
	return(-1);
      }
      for(i=0; i<join->numitems; i++)
	join->data[0][i] = strdup("\0"); /* intialize to zero length strings */
    } else {
      if((join->data[0] = msDBFGetValues(hDBF,i)) == NULL) {
	chdir(old_path); /* restore old cwd */
	return(-1);
      }
    }

  } else {

    if(join->data) { /* free old data */
      for(i=0; i<join->numrecords; i++)
	msFreeCharArray(join->data[i], join->numitems);
      free(join->data);
      join->numrecords = 0;
    }

    ids = (int *)malloc(sizeof(int)*nrecs);
    if(!ids) {
      msSetError(MS_MEMERR, NULL, "msJoinDBFTables()");
      chdir(old_path); /* restore old cwd */
      return(-1);
    }
    
    j=0;
    for(i=0; i<nrecs; i++) { /* find the matches, save ids */
      if(strcmp(join->match, msDBFReadStringAttribute(hDBF, i, idx)) == 0) {
	ids[j] = i;
	j++;
      }
    }
  
    join->numrecords = j;

    if(join->numrecords > 0) { /* save em */
      if((join->data = (char ***)malloc(sizeof(char **)*join->numrecords)) == NULL) {
	msSetError(MS_MEMERR, NULL, "msJoinDBFTables()");
	free(ids);
	chdir(old_path); /* restore old cwd */
	return(-1);
      }

      for(i=0; i<join->numrecords; i++) {
	join->data[i] = (char **)malloc(sizeof(char *)*join->numitems);
	if(!join->data[i]) {
	  msSetError(MS_MEMERR, NULL, "msJoinDBFTables()");
	  free(ids);
	  chdir(old_path); /* restore old cwd */
	  return(-1);
	}

	join->data[i] = msDBFGetValues(hDBF,ids[i]);
	if(!join->data[i]) {
	  free(ids);
	  chdir(old_path); /* restore old cwd */
	  return(-1);
	}
      }
    }

    free(ids);
  }

  chdir(old_path); /* restore old cwd */
  msDBFClose(hDBF);
  return(0);
}

extern int msyyresult; // result of parsing, true/false
extern int msyystate;
extern char *msyystring;
extern int msyyparse();

static int addResult(resultCacheObj *cache, int classindex, int shapeindex, int tileindex)
{
  int i;

  if(cache->numresults == cache->cachesize) { // just add it to the end
    cache->results = (resultCacheMemberObj *) realloc(cache->results, sizeof(resultCacheMemberObj)*(cache->cachesize+MS_RESULTCACHEINCREMENT));
    if(!cache->results) {
      msSetError(MS_MEMERR, "Realloc() error.", "addResult()");
      return(MS_FAILURE);
    }   
    cache->cachesize += MS_RESULTCACHEINCREMENT;
  }

  i = cache->numresults;

  cache->results[i].classindex = classindex;
  cache->results[i].tileindex = tileindex;
  cache->results[i].shapeindex = shapeindex;

  cache->numresults++;
  return(MS_SUCCESS);
}

int msSaveQuery(mapObj *map, char *filename) {
  FILE *stream;
  int i, j, n=0;

  if(!filename) {
    msSetError(MS_MISCERR, "No filename provided to save query to.", "msSaveQuery()");
    return(MS_FAILURE);
  }

  stream = fopen(filename, "wb");
  if(!stream) {
    sprintf(ms_error.message, "(%s)", filename);
    msSetError(MS_IOERR, ms_error.message, "msSaveQuery()");
    return(MS_FAILURE);
  }

  // count the number of layers with results
  for(i=0; i<map->numlayers; i++)
    if(map->layers[i].resultcache) n++;
  fwrite(&n, sizeof(int), 1, stream);

  // now write the result set for each layer
  for(i=0; i<map->numlayers; i++) {
    if(map->layers[i].resultcache) {
      fwrite(&i, sizeof(int), 1, stream); // layer index
      fwrite(&(map->layers[i].resultcache->numresults), sizeof(int), 1, stream); // number of results
      fwrite(&(map->layers[i].resultcache->bounds), sizeof(rectObj), 1, stream); // bounding box
      for(j=0; j<map->layers[i].resultcache->numresults; j++)
	fwrite(&(map->layers[i].resultcache->results[j]), sizeof(resultCacheMemberObj), 1, stream); // each result
    }
  }

  fclose(stream);
  return(MS_SUCCESS); 
}

int msLoadQuery(mapObj *map, char *filename) {
  FILE *stream;
  int i, j, k, n=0;

  if(!filename) {
    msSetError(MS_MISCERR, "No filename provided to load query from.", "msLoadQuery()");
    return(MS_FAILURE);
  }

  stream = fopen(filename, "rb");
  if(!stream) {
    sprintf(ms_error.message, "(%s)", filename);
    msSetError(MS_IOERR, ms_error.message, "msLoadQuery()");    
    return(MS_FAILURE);
  }

  fread(&n, sizeof(int), 1, stream);

  // now load the result set for each layer found in the query file
  for(i=0; i<n; i++) {
    fread(&j, sizeof(int), 1, stream); // layer index

    if(j<0 || j>map->numlayers) {
      msSetError(MS_MISCERR, "Invalid layer index loaded from query file.", "msLoadQuery()");
      return(MS_FAILURE);
    }
    
    // inialize the results for this layer
    map->layers[j].resultcache = (resultCacheObj *)malloc(sizeof(resultCacheObj)); // allocate and initialize the result cache

    fread(&(map->layers[j].resultcache->numresults), sizeof(int), 1, stream); // number of results   
    map->layers[j].resultcache->cachesize = map->layers[j].resultcache->numresults;

    fread(&(map->layers[j].resultcache->bounds), sizeof(rectObj), 1, stream); // bounding box

    map->layers[j].resultcache->results = (resultCacheMemberObj *) malloc(sizeof(resultCacheMemberObj)*map->layers[j].resultcache->numresults);

    for(k=0; k<map->layers[j].resultcache->numresults; k++)
      fread(&(map->layers[j].resultcache->results[k]), sizeof(resultCacheMemberObj), 1, stream); // each result
  }

  fclose(stream);
  return(MS_SUCCESS);
}


int msQueryByItem(mapObj *map, char *layer, int mode, char *item, char *value)
{
  // FIX: NEED A GENERIC SOMETHING OR OTHER HERE
  return(MS_FAILURE);
}

int msQueryByRect(mapObj *map, char *layer, rectObj rect) 
{
  int i,l; /* counters */
  int start, stop=0;

  layerObj *lp;

  char status;
  shapeObj shape, search_shape;

  msInitShape(&shape);
  msInitShape(&search_shape);

  msRectToPolygon(rect, &search_shape);

  if(layer) {
    if((start = msGetLayerIndex(map, layer)) == -1) {
      sprintf(ms_error.message, "Unable to find query layer %s.", layer);
      msSetError(MS_MISCERR, ms_error.message, "msQueryByRect()");
      return(-1);
    }
    stop = start;
  } else {
    start = map->numlayers-1;
  }

  for(l=start; l>=stop; l--) {
    lp = &(map->layers[l]);

    if(lp->status == MS_OFF)
      continue;

    if(map->scale > 0) {
      if((lp->maxscale > 0) && (map->scale > lp->maxscale))
	continue;
      if((lp->minscale > 0) && (map->scale <= lp->minscale))
	continue;
    }

    // FIX: CHECK CLASSES FOR TEMPLATES TO MAKE SURE THIS LAYER CAN BE PROCESSED

    // build item list (no annotation)
    status = msLayerWhichItems(lp, MS_FALSE);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // open this layer
    status = msLayerOpen(lp, map->shapepath);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // identify target shapes
    status = msLayerWhichShapes(lp, map->shapepath, rect, &(map->projection));
    if(status != MS_SUCCESS) {
      msLayerClose(lp);
      return(MS_FAILURE);
    }

    lp->resultcache = (resultCacheObj *)malloc(sizeof(resultCacheObj)); // allocate and initialize the result cache
    lp->resultcache->results = NULL;
    lp->resultcache->numresults = lp->resultcache->cachesize = 0;
    lp->resultcache->bounds.minx = lp->resultcache->bounds.miny = lp->resultcache->bounds.maxx = lp->resultcache->bounds.maxy = -1;

    while(msLayerNextShape(lp, map->shapepath, &shape, MS_TRUE)) { /* step through the shapes */ 
      shape.classindex = msShapeGetClass(lp, &shape);

      if(shape.classindex == -1) { // not a valid shape
	msFreeShape(&shape);
	continue;
      }

      if(!(lp->class[shape.classindex].template)) { // no valid template
	msFreeShape(&shape);
	continue;
      }

      if(msRectContained(&shape.bounds, &rect) == MS_TRUE) { /* if the whole shape is in, don't intersect */	
	addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	msFreeShape(&shape);
	continue; /* next shape */
      }

      // FIX: WRITE INTERSECTION FUNCTIONS FOR JUST A RECTOBJ!

      switch(shape.type) { /* make sure shape actually intersects the rect */
      case MS_POINT:	
	if(msIntersectMultipointPolygon(&shape.line[0], &search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      case MS_LINE:
	if(msIntersectPolylinePolygon(&shape, &search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      case MS_POLYGON:
	if(msIntersectPolygons(&shape, &search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      default:
	break;
      }

      if(lp->resultcache->numresults == 1)
	lp->resultcache->bounds = shape.bounds;
      else
	msMergeRect(&(lp->resultcache->bounds), &shape.bounds);

      msFreeShape(&shape);
    } // next shape
      
    if(status != MS_DONE) return(MS_FAILURE);

    msLayerClose(lp);
  } // next layer
 
  msFreeShape(&search_shape);
 
  // was anything found?
  for(l=start; l>=stop; l--) {    
    if(map->layers[l].resultcache->numresults > 0)
      return(MS_SUCCESS);
  }
 
  msSetError(MS_NOTFOUND, "No matching record(s) found.", "msQueryByRect()"); 
  return(MS_FAILURE);
}

int msQueryByPoint(mapObj *map, char *layer, int mode, pointObj p, double buffer)
{
  int i, l;
  int start, stop=0;

  double d, t;

  layerObj *lp;

  char status;
  rectObj rect;
  shapeObj shape;

  msInitShape(&shape);

  if(layer) {
    if((start = msGetLayerIndex(map, layer)) == -1) {
      sprintf(ms_error.message, "Unable to find query layer %s.", layer);
      msSetError(MS_MISCERR, ms_error.message, "msQueryByPoint()");
      return(-1);
    }
    stop = start;
  } else {
    start = map->numlayers-1;
  }

  for(l=start; l>=stop; l--) {
    lp = &(map->layers[l]);

    if(lp->status == MS_OFF)
      continue;

    if(map->scale > 0) {
      if((lp->maxscale > 0) && (map->scale > lp->maxscale))
	continue;
      if((lp->minscale > 0) && (map->scale <= lp->minscale))
	continue;
    }

    if(buffer <= 0) { // use layer tolerance
      if(lp->toleranceunits == MS_PIXELS)
	t = lp->tolerance * msAdjustExtent(&(map->extent), map->width, map->height);
      else
	t = lp->tolerance * (inchesPerUnit[lp->toleranceunits]/inchesPerUnit[map->units]);
    } else // use buffer distance
      t = buffer;

    rect.minx = p.x - t;
    rect.maxx = p.x + t;
    rect.miny = p.y - t;
    rect.maxy = p.y + t;

    // build item list (no annotation)
    status = msLayerWhichItems(lp, MS_FALSE);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // open this layer
    status = msLayerOpen(lp, map->shapepath);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // identify target shapes
    status = msLayerWhichShapes(lp, map->shapepath, rect, &(map->projection));
    if(status != MS_SUCCESS) { 
      msLayerClose(lp);
      return(MS_FAILURE);
    }

    lp->resultcache = (resultCacheObj *)malloc(sizeof(resultCacheObj)); // allocate and initialize the result cache
    lp->resultcache->results = NULL;
    lp->resultcache->numresults = lp->resultcache->cachesize = 0;
    lp->resultcache->bounds.minx = lp->resultcache->bounds.miny = lp->resultcache->bounds.maxx = lp->resultcache->bounds.maxy = -1;

    while(msLayerNextShape(lp, map->shapepath, &shape, MS_TRUE)) { // step through the shapes
      shape.classindex = msShapeGetClass(lp, &shape);

      if(shape.classindex == -1) { // not a valid shape
	msFreeShape(&shape);
	continue;
      }

      if(!(lp->class[shape.classindex].template)) { // no valid template
	msFreeShape(&shape);
	continue;
      }

      if(shape.type == MS_POINT)
	d = msDistanceFromPointToMultipoint(&p, &shape.line[0]);
      else if(shape.type == MS_LINE)
	d = msDistanceFromPointToPolyline(&p, &shape);
      else // MS_POLYGON
	d = msDistanceFromPointToPolygon(&p, &shape);	  

      if( d <= t ) { // found one
	if(mode == MS_SINGLE) {
	  lp->resultcache->numresults = 0;
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	  t = d;
	} else {
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	}	
      }

      if(lp->resultcache->numresults == 1)
	lp->resultcache->bounds = shape.bounds;
      else
	msMergeRect(&(lp->resultcache->bounds), &shape.bounds);
      
      msFreeShape(&shape);	
    } // next shape

    if(status != MS_DONE) return(MS_FAILURE);

    msLayerClose(lp);

    if((lp->resultcache->numresults > 0) && (mode == MS_SINGLE)) // no need to search any further
      break;
  } // next layer

  // was anything found?
  for(l=start; l>=stop; l--) {    
    if(map->layers[l].resultcache->numresults > 0)
      return(MS_SUCCESS);
  }
 
  msSetError(MS_NOTFOUND, "No matching record(s) found.", "msQueryByPoint()"); 
  return(MS_FAILURE);
}

int msQueryByFeatures(mapObj *map, char *layer)
{
  return(MS_FAILURE);
}

int msQueryByShape(mapObj *map, char *layer, shapeObj *search_shape)
{
  int start, stop=0, l;
  shapeObj shape;
  layerObj *lp;
  char status;

  // FIX: do some checking on search_shape here...

  /*
  ** Do we have query layer, if not we need to search all layers
  */
  if(layer) {
    if((start = msGetLayerIndex(map, layer)) == -1) {
      sprintf(ms_error.message, "Unable to find query layer %s.", layer);
      msSetError(MS_MISCERR, ms_error.message, "msQueryByShape()");
      return(MS_FAILURE);
    }
    stop = start;
  } else {
    start = map->numlayers-1;
  }

  msComputeBounds(search_shape); // make sure an accurate extent exists
 
  for(l=start; l>=stop; l--) { /* each layer */
    lp = &(map->layers[l]);

    if(lp->status == MS_OFF)
      continue;
    
    if(map->scale > 0) {
      if((lp->maxscale > 0) && (map->scale > lp->maxscale))
	continue;
      if((lp->minscale > 0) && (map->scale <= lp->minscale))
	continue;
    }
        
    // FIX: CHECK CLASSES FOR TEMPLATES TO MAKE SURE THIS LAYER CAN BE PROCESSED

    // build item list (no annotation)
    status = msLayerWhichItems(lp, MS_FALSE);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // open this layer
    status = msLayerOpen(lp, map->shapepath);
    if(status != MS_SUCCESS) return(MS_FAILURE);

    // identify target shapes
    status = msLayerWhichShapes(lp, map->shapepath, search_shape->bounds, &(map->projection));
    if(status != MS_SUCCESS) {
      msLayerClose(lp);
      return(MS_FAILURE);
    }

    lp->resultcache = (resultCacheObj *)malloc(sizeof(resultCacheObj)); // allocate and initialize the result cache
    lp->resultcache->results = NULL;
    lp->resultcache->numresults = lp->resultcache->cachesize = 0;
    lp->resultcache->bounds.minx = lp->resultcache->bounds.miny = lp->resultcache->bounds.maxx = lp->resultcache->bounds.maxy = -1;

    while((status = msLayerNextShape(lp, map->shapepath, &shape, MS_TRUE)) == MS_SUCCESS) { // step through the shapes
      shape.classindex = msShapeGetClass(lp, &shape);

      if(shape.classindex == -1) { // not a valid shape
	msFreeShape(&shape);
	continue;
      }

      if(!(lp->class[shape.classindex].template)) { // no valid template
	msFreeShape(&shape);
	continue;
      }

      switch(shape.type) { /* make sure shape actually intersects the rect */
      case MS_POINT:
	if(msIntersectMultipointPolygon(&shape.line[0], search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      case MS_LINE:
	if(msIntersectPolylinePolygon(&shape, search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      case MS_POLYGON:
	if(msIntersectPolygons(&shape, search_shape) == MS_TRUE)
	  addResult(lp->resultcache, shape.classindex, shape.index, shape.tileindex);
	break;
      default:
	break;
      }

      if(lp->resultcache->numresults == 1)
	lp->resultcache->bounds = shape.bounds;
      else
	msMergeRect(&(lp->resultcache->bounds), &shape.bounds);

      msFreeShape(&shape);
    } // next shape

    if(status != MS_DONE) return(MS_FAILURE);

    msLayerClose(lp);
  } // next layer

  // was anything found?
  for(l=start; l>=stop; l--) {    
    if(map->layers[l].resultcache->numresults > 0)
      return(MS_SUCCESS);
  }
 
  msSetError(MS_NOTFOUND, "No matching record(s) found.", "msQueryByShape()"); 
  return(MS_FAILURE);
}
