#include "map.h"

#define PSF .8
#define VMARGIN 5 /* margin at top and bottom of legend graphic */
#define HMARGIN 5 /* margin at left and right of legend graphic */

/*
** Creates a GD image of a legend for a specific map. msDrawLegend()
** respects the current scale, and classes without a name are not
** added to the legend.
*/

gdImagePtr msDrawLegend(mapObj *map)
{
  shapeObj p;
  gdImagePtr img; /* image data structure */
  int i,j; /* loop counters */
  pointObj pnt;
  int size_x, size_y;
  layerObj *lp;  
  int maxwidth=0, maxheight=0, n=0;
  int *heights;
  rectObj rect;

  map->cellsize = msAdjustExtent(&(map->extent), map->width, map->height);
  map->scale = msCalculateScale(map->extent, map->units, map->width, map->height);

  msApplyScale(map);

  /* Initialize the polygon/polyline */
  p.line = (lineObj *)malloc(sizeof(lineObj));
  p.numlines = 1;
  p.line[0].point = (pointObj *)malloc(sizeof(pointObj)*5);
  p.line[0].numpoints = 5;

  /*
  ** allocate heights array
  */
  for(i=0; i<map->numlayers; i++) {
    if((map->layers[i].status == MS_OFF) || (map->layers[i].status == MS_QUERY)) /* skip it */
      continue;
    
    for(j=0;j<map->layers[i].numclasses;j++) {
      if(map->layers[i].class[j].name == NULL)
	continue; /* skip it */
      n++;
    }
  }

  if((heights = (int *)malloc(sizeof(int)*n)) == NULL) {
    msSetError(MS_MEMERR, "Error allocating heights array.", "msDrawLegend()");
    return(NULL);
  }

  /*
  ** Calculate the optimal image size for the legend
  */
  n=0;
  for(i=0; i<map->numlayers; i++) { /* Need to find the longest legend label string */
    if((map->layers[i].status == MS_OFF) || (map->layers[i].status == MS_QUERY)) /* skip it */
      continue;
   
    for(j=0;j<map->layers[i].numclasses;j++) {
      if(map->layers[i].class[j].name == NULL)
	continue; /* skip it */
      if(msGetLabelSize(map->layers[i].class[j].name, &map->legend.label, &rect, &(map->fontset)) != 0)
	return(NULL); /* something bad happened */
      maxheight = MS_MAX(maxheight, MS_NINT(rect.maxy - rect.miny));
      maxwidth = MS_MAX(maxwidth, MS_NINT(rect.maxx - rect.minx));
      heights[n] = MS_NINT(rect.maxy - rect.miny);
      n++;
    }
  }
  
  size_x = (2*HMARGIN)+(maxwidth)+(map->legend.keyspacingx)+(map->legend.keysizex);
  size_y = (2*VMARGIN) + ((n-1)*map->legend.keyspacingy);
  for(i=0; i<n; i++) {
    heights[i] = MS_MAX(heights[i], maxheight);
    size_y += MS_MAX(heights[i], map->legend.keysizey);
  }

  /*
  ** Initialize the legend image
  */
  if((img = gdImageCreate(size_x, size_y)) == NULL) {
    msSetError(MS_GDERR, "Unable to initialize image.", "msDrawLegend()");
    free(heights);
    return(NULL);
  }

  /*
  ** Load colormap for the image
  */
  if(msLoadPalette(img, &map->palette, map->legend.imagecolor) == -1)
     return(NULL);

  pnt.y = VMARGIN;
    
  /* for(i=0; i<map->numlayers; i++) { */
  for(i=map->numlayers-1; i>=0; i--) {

    lp = &(map->layers[i]); /* assign for brevity */

    if((lp->numclasses == 0) || (lp->status == MS_OFF) || (lp->status == MS_QUERY))
      continue; /* skip this layer */

    for(j=0; j<lp->numclasses; j++) { /* always at least 1 class */

      if(!lp->class[j].name)
	continue; /* skip it */
      
      pnt.x = HMARGIN + map->legend.keysizex + map->legend.keyspacingx;

      /* 
      ** now draw the appropriate color/symbol/size combination 
      */      
      switch(lp->type) {
      case MS_POINT:            
	p.line[0].point[0].x = MS_NINT(HMARGIN + (map->legend.keysizex/2.0)) - 1;
	p.line[0].point[0].y = MS_NINT(pnt.y + (map->legend.keysizey/2.0)) - 1;
	p.line[0].numpoints = 1;
	msDrawMarkerSymbol(&map->markerset, img, &(p.line[0].point[0]), &(lp->class[j]));
	break;
      case MS_LINE:
      case MS_POLYLINE:
	p.line[0].point[0].x = HMARGIN;
	p.line[0].point[0].y = pnt.y + map->legend.keysizey - 1;
	p.line[0].point[1].x = HMARGIN + MS_NINT(map->legend.keysizex/3.0) - 1;
	p.line[0].point[1].y = pnt.y;
	p.line[0].point[2].x = HMARGIN + MS_NINT(2*map->legend.keysizex/3.0) - 1;
	p.line[0].point[2].y = pnt.y + map->legend.keysizey - 1;
	p.line[0].point[3].x = HMARGIN + map->legend.keysizex - 1;
	p.line[0].point[3].y = pnt.y;
	p.line[0].numpoints = 4;
	msDrawLineSymbol(&map->lineset, img, &p, &(lp->class[j]));       
	break;
      case MS_RASTER:
      case MS_POLYGON:
	p.line[0].point[0].x = HMARGIN;
	p.line[0].point[0].y = pnt.y;
	p.line[0].point[1].x = HMARGIN + map->legend.keysizex - 1;
	p.line[0].point[1].y = pnt.y;
	p.line[0].point[2].x = HMARGIN + map->legend.keysizex - 1;
	p.line[0].point[2].y = pnt.y + map->legend.keysizey - 1;
	p.line[0].point[3].x = HMARGIN;
	p.line[0].point[3].y = pnt.y + map->legend.keysizey - 1;
	p.line[0].point[4].x = p.line[0].point[0].x;
	p.line[0].point[4].y = p.line[0].point[0].y;
	p.line[0].numpoints = 5;
	msDrawShadeSymbol(&map->shadeset, img, &p, &(lp->class[j]));
	break;
      default:
	break;
     } /* end symbol drawing */

      /* Draw the outline if a color is specified */
      if(map->legend.outlinecolor > 0) { /* 0 is background, so who cares about drawing it. */
	p.line[0].point[0].x = HMARGIN;
        p.line[0].point[0].y = pnt.y;
	p.line[0].point[1].x = HMARGIN + map->legend.keysizex - 1;
        p.line[0].point[1].y = pnt.y;
        p.line[0].point[2].x = HMARGIN + map->legend.keysizex - 1;
        p.line[0].point[2].y = pnt.y + map->legend.keysizey - 1;
        p.line[0].point[3].x = HMARGIN;
        p.line[0].point[3].y = pnt.y + map->legend.keysizey - 1;
	p.line[0].point[4].x = p.line[0].point[0].x;
	p.line[0].point[4].y = p.line[0].point[0].y;
	msImagePolyline(img, &p, map->legend.outlinecolor);
      }

      pnt.y += MS_MAX(map->legend.keysizey, maxheight);
      msDrawLabel(img, map, pnt, lp->class[j].name, &(map->legend.label));

      pnt.y += map->legend.keyspacingy; /* bump y for next label */
	
    } /* next label */
  } /* next layer please */

  free(heights);
  free(p.line[0].point);
  free(p.line);

  return(img);  /* return image */
}

int msEmbedLegend(mapObj *map, gdImagePtr img)
{
  int s,l;
  pointObj point;

  s = msGetSymbolIndex(&(map->markerset), "legend");
  if(s == -1) {
    s = map->markerset.numsymbols;
    map->markerset.numsymbols++;
  } else {
    if(map->markerset.symbol[s].img) 
      gdImageDestroy(map->markerset.symbol[s].img);
  }

  map->markerset.symbol[s].img = msDrawLegend(map);
  if(!map->markerset.symbol[s].img) return(-1); // something went wrong creating scalebar

  map->markerset.symbol[s].type = MS_SYMBOL_PIXMAP;
  map->markerset.symbol[s].name = strdup("legend");  

  if(map->legend.transparent)
    gdImageColorTransparent(map->markerset.symbol[s].img, 0);

  switch(map->legend.position) {
  case(MS_LL):
    point.x = MS_NINT(map->markerset.symbol[s].img->sx/2.0);
    point.y = map->height - MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  case(MS_LR):
    point.x = map->width - MS_NINT(map->markerset.symbol[s].img->sx/2.0);
    point.y = map->height - MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  case(MS_LC):
    point.x = MS_NINT(map->width/2.0);
    point.y = map->height - MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  case(MS_UR):
    point.x = map->width - MS_NINT(map->markerset.symbol[s].img->sx/2.0);
    point.y = MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  case(MS_UL):
    point.x = MS_NINT(map->markerset.symbol[s].img->sx/2.0);
    point.y = MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  case(MS_UC):
    point.x = MS_NINT(map->width/2.0);
    point.y = MS_NINT(map->markerset.symbol[s].img->sy/2.0);
    break;
  }

  l = msGetLayerIndex(map, "legend");
  if(l == -1) {
    l = map->numlayers;
    map->numlayers++;

    if(initLayer(&(map->layers[l])) == -1) return(-1);
    map->layers[l].name = strdup("legend");
    map->layers[l].type = MS_ANNOTATION;
    map->layers[l].status = MS_ON;

    if(initClass(&(map->layers[l].class[0])) == -1) return(-1);    
  }

  map->layers[l].class[0].symbol = s;
  map->layers[l].class[0].color = 0;
  map->layers[l].class[0].label.force = MS_TRUE;

  if(map->legend.postlabelcache) // add it directly to the image
    msDrawMarkerSymbol(&map->markerset, img, &point, &(map->layers[l].class[0]));
  else
    msAddLabel(map, l, 0, -1, -1, point, " ", -1);

  return(0);
}
