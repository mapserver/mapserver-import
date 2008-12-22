/******************************************************************************
 * $id: maptemplate.c 7725 2008-06-21 15:56:58Z sdlime $
 *
 * Project:  MapServer
 * Purpose:  Various template processing functions.
 * Author:   Steve Lime and the MapServer team.
 *
 ******************************************************************************
 * Copyright (c) 1996-2008 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "maptemplate.h"
#include "maphash.h"
#include "mapserver.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include <ctype.h>

MS_CVSID("$Id$")

static char *processLine(mapservObj *mapserv, char *instr, FILE *stream, int mode);

/*
 * Redirect to (only use in CGI)
 * 
*/
int msRedirect(char *url)
{
  msIO_printf("Status: 302 Found\n");
  msIO_printf("Uri: %s\n", url);
  msIO_printf("Location: %s\n", url);
  msIO_printf("Content-type: text/html%c%c",10,10);

  /* the following may be an issue for fastcgi/msIO_.  */
  fflush(stdout);
  return MS_SUCCESS;
}

/*
** Sets the map extent under a variety of scenarios.
*/
int setExtent(mapservObj *mapserv)
{
  double cellx,celly,cellsize;

  switch(mapserv->CoordSource) 
  {
   case FROMUSERBOX: /* user passed in a map extent */
     break;
   case FROMIMGBOX: /* fully interactive web, most likely with java front end */
     cellx = MS_CELLSIZE(mapserv->ImgExt.minx, mapserv->ImgExt.maxx, mapserv->ImgCols);
     celly = MS_CELLSIZE(mapserv->ImgExt.miny, mapserv->ImgExt.maxy, mapserv->ImgRows);
     mapserv->map->extent.minx = MS_IMAGE2MAP_X(mapserv->ImgBox.minx, mapserv->ImgExt.minx, cellx);
     mapserv->map->extent.maxx = MS_IMAGE2MAP_X(mapserv->ImgBox.maxx, mapserv->ImgExt.minx, cellx);
     mapserv->map->extent.maxy = MS_IMAGE2MAP_Y(mapserv->ImgBox.miny, mapserv->ImgExt.maxy, celly); /* y's are flip flopped because img/map coordinate systems are */
     mapserv->map->extent.miny = MS_IMAGE2MAP_Y(mapserv->ImgBox.maxy, mapserv->ImgExt.maxy, celly);
     break;
   case FROMIMGPNT:
     cellx = MS_CELLSIZE(mapserv->ImgExt.minx, mapserv->ImgExt.maxx, mapserv->ImgCols);
     celly = MS_CELLSIZE(mapserv->ImgExt.miny, mapserv->ImgExt.maxy, mapserv->ImgRows);
     mapserv->mappnt.x = MS_IMAGE2MAP_X(mapserv->ImgPnt.x, mapserv->ImgExt.minx, cellx);
     mapserv->mappnt.y = MS_IMAGE2MAP_Y(mapserv->ImgPnt.y, mapserv->ImgExt.maxy, celly);

     mapserv->map->extent.minx = mapserv->mappnt.x - .5*((mapserv->ImgExt.maxx - mapserv->ImgExt.minx)/mapserv->fZoom); /* create an extent around that point */
     mapserv->map->extent.miny = mapserv->mappnt.y - .5*((mapserv->ImgExt.maxy - mapserv->ImgExt.miny)/mapserv->fZoom);
     mapserv->map->extent.maxx = mapserv->mappnt.x + .5*((mapserv->ImgExt.maxx - mapserv->ImgExt.minx)/mapserv->fZoom);
     mapserv->map->extent.maxy = mapserv->mappnt.y + .5*((mapserv->ImgExt.maxy - mapserv->ImgExt.miny)/mapserv->fZoom);
     break;
   case FROMREFPNT:
     cellx = MS_CELLSIZE(mapserv->map->reference.extent.minx, mapserv->map->reference.extent.maxx, mapserv->map->reference.width);
     celly = MS_CELLSIZE(mapserv->map->reference.extent.miny, mapserv->map->reference.extent.maxy, mapserv->map->reference.height);
     mapserv->mappnt.x = MS_IMAGE2MAP_X(mapserv->RefPnt.x, mapserv->map->reference.extent.minx, cellx);
     mapserv->mappnt.y = MS_IMAGE2MAP_Y(mapserv->RefPnt.y, mapserv->map->reference.extent.maxy, celly);  

     mapserv->map->extent.minx = mapserv->mappnt.x - .5*(mapserv->ImgExt.maxx - mapserv->ImgExt.minx); /* create an extent around that point */
     mapserv->map->extent.miny = mapserv->mappnt.y - .5*(mapserv->ImgExt.maxy - mapserv->ImgExt.miny);
     mapserv->map->extent.maxx = mapserv->mappnt.x + .5*(mapserv->ImgExt.maxx - mapserv->ImgExt.minx);
     mapserv->map->extent.maxy = mapserv->mappnt.y + .5*(mapserv->ImgExt.maxy - mapserv->ImgExt.miny);
     break;
   case FROMBUF:
     mapserv->map->extent.minx = mapserv->mappnt.x - mapserv->Buffer; /* create an extent around that point, using the buffer */
     mapserv->map->extent.miny = mapserv->mappnt.y - mapserv->Buffer;
     mapserv->map->extent.maxx = mapserv->mappnt.x + mapserv->Buffer;
     mapserv->map->extent.maxy = mapserv->mappnt.y + mapserv->Buffer;
     break;
   case FROMSCALE: 
     cellsize = (mapserv->ScaleDenom/mapserv->map->resolution)/msInchesPerUnit(mapserv->map->units,0); /* user supplied a point and a scale denominator */
     mapserv->map->extent.minx = mapserv->mappnt.x - cellsize*(mapserv->map->width-1)/2.0;
     mapserv->map->extent.miny = mapserv->mappnt.y - cellsize*(mapserv->map->height-1)/2.0;
     mapserv->map->extent.maxx = mapserv->mappnt.x + cellsize*(mapserv->map->width-1)/2.0;
     mapserv->map->extent.maxy = mapserv->mappnt.y + cellsize*(mapserv->map->height-1)/2.0;
     break;
   default: /* use the default in the mapfile if it exists */
     if((mapserv->map->extent.minx == mapserv->map->extent.maxx) && (mapserv->map->extent.miny == mapserv->map->extent.maxy)) {
       msSetError(MS_WEBERR, "No way to generate map extent.", "mapserv()");
       return MS_FAILURE;
     }
  }

  mapserv->RawExt = mapserv->map->extent; /* save unaltered extent */
   
  return MS_SUCCESS;
}

int checkWebExtent(mapservObj *mapserv)
{
  return MS_SUCCESS;
}

int checkWebScale(mapservObj *mapserv) 
{
  int status;

  mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height); /* we do this cause we need a scale */
  if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return status;

  if((mapserv->map->scaledenom < mapserv->map->web.minscaledenom) && (mapserv->map->web.minscaledenom > 0)) {
    if(mapserv->map->web.mintemplate) { /* use the template provided */
      if(TEMPLATE_TYPE(mapserv->map->web.mintemplate) == MS_FILE) {
        if((status = msReturnPage(mapserv, mapserv->map->web.mintemplate, BROWSE, NULL)) != MS_SUCCESS) return status;
      } else {
        if((status = msReturnURL(mapserv, mapserv->map->web.mintemplate, BROWSE)) != MS_SUCCESS) return status;
      }
    } else { /* force zoom = 1 (i.e. pan) */
      mapserv->fZoom = mapserv->Zoom = 1;
      mapserv->ZoomDirection = 0;
      mapserv->CoordSource = FROMSCALE;
      mapserv->ScaleDenom = mapserv->map->web.minscaledenom;
      mapserv->mappnt.x = (mapserv->map->extent.maxx + mapserv->map->extent.minx)/2; /* use center of bad extent */
      mapserv->mappnt.y = (mapserv->map->extent.maxy + mapserv->map->extent.miny)/2;
      setExtent(mapserv);
      mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);      
      if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return status;
    }
  } else { 
    if((mapserv->map->scaledenom > mapserv->map->web.maxscaledenom) && (mapserv->map->web.maxscaledenom > 0)) {
      if(mapserv->map->web.maxtemplate) { /* use the template provided */
        if(TEMPLATE_TYPE(mapserv->map->web.maxtemplate) == MS_FILE) {
          if((status = msReturnPage(mapserv, mapserv->map->web.maxtemplate, BROWSE, NULL)) != MS_SUCCESS) return status;
        } else {
          if((status = msReturnURL(mapserv, mapserv->map->web.maxtemplate, BROWSE)) != MS_SUCCESS) return status;
        }
      } else { /* force zoom = 1 (i.e. pan) */
        mapserv->fZoom = mapserv->Zoom = 1;
        mapserv->ZoomDirection = 0;
        mapserv->CoordSource = FROMSCALE;
        mapserv->ScaleDenom = mapserv->map->web.maxscaledenom;
        mapserv->mappnt.x = (mapserv->map->extent.maxx + mapserv->map->extent.minx)/2; /* use center of bad extent */
        mapserv->mappnt.y = (mapserv->map->extent.maxy + mapserv->map->extent.miny)/2;
        setExtent(mapserv);
        mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), mapserv->map->width, mapserv->map->height);
        if((status = msCalculateScale(mapserv->map->extent, mapserv->map->units, mapserv->map->width, mapserv->map->height, mapserv->map->resolution, &mapserv->map->scaledenom)) != MS_SUCCESS) return status;
      }
    }
  }
   
  return MS_SUCCESS;
}

int msReturnTemplateQuery(mapservObj *mapserv, char *queryFormat, char **papszBuffer)
{
  imageObj *img = NULL;
  int i, status;

  outputFormatObj *outputFormat=NULL;

  if(!queryFormat) {
    msSetError(MS_WEBERR, "Return format/mime-type not specified.", "msReturnTemplateQuery()");
    return MS_FAILURE;
  }

  /*
  ** The functions in mapoutput.c rely on mime-type too and we don't want that with queries. So
  ** we do a name search only.
  */
  for( i = 0; i < mapserv->map->numoutputformats; i++ ) {
    if( strcasecmp(queryFormat, mapserv->map->outputformatlist[i]->name) == 0 )
      outputFormat = mapserv->map->outputformatlist[i];
  }

  if(outputFormat) {
     if( !MS_RENDERER_TEMPLATE(outputFormat) ) { /* got an image format, return the query results that way */
       outputFormatObj *tempOutputFormat = mapserv->map->outputformat; /* save format */

       checkWebScale(mapserv);

       mapserv->map->outputformat = outputFormat; /* override what was given for IMAGETYPE */
       img = msDrawMap(mapserv->map, MS_TRUE);
       if(!img) return MS_FAILURE;
       mapserv->map->outputformat = tempOutputFormat; /* restore format */

       if(mapserv->sendheaders) msIO_printf("Content-type: %s%c%c", MS_IMAGE_MIME_TYPE(outputFormat), 10,10);
       status = msSaveImage(mapserv->map, img, NULL);
       msFreeImage(img);

       return status;
    }
  }

  /* 
  ** At this point we know we have a template of some sort, either the new style that references a or the old
  ** style made up of external files slammed together. Either way we may have to compute a query map and other
  ** images. We only create support images IF the querymap has status=MS_ON.
  */ 
  if(mapserv->map->querymap.status) {
    checkWebScale(mapserv);
    msGenerateImages(mapserv, MS_TRUE, MS_TRUE);
  }

  if(outputFormat) {
    const char *file = msGetOutputFormatOption( outputFormat, "FILE", NULL );
    if(!file) {
      msSetError(MS_WEBERR, "Template driver requires \"FILE\" format option be set.", "msReturnTemplateQuery()");
      return MS_FAILURE;
    }

    if(mapserv->sendheaders) msIO_printf("Content-type: %s%c%c", outputFormat->mimetype, 10, 10);
    if((status = msReturnPage(mapserv, (char *) file, BROWSE, papszBuffer)) != MS_SUCCESS)
      return status;
  } else {
    if((status = msReturnNestedTemplateQuery(mapserv, queryFormat, papszBuffer)) != MS_SUCCESS)
      return status;
  }

  return MS_SUCCESS;
}

/*
** Is a particular layer or group on, that is was it requested explicitly by the user.
*/
int isOn(mapservObj *mapserv, char *name, char *group)
{
  int i;

  for(i=0;i<mapserv->NumLayers;i++) {
    if(name && strcmp(mapserv->Layers[i], name) == 0)  return(MS_TRUE);
    if(group && strcmp(mapserv->Layers[i], group) == 0) return(MS_TRUE);
  }

  return(MS_FALSE);
}

/************************************************************************/
/*            int sortLayerByOrder(mapObj *map, char* pszOrder)         */
/*                                                                      */
/*      sorth the displaying in ascending or descending order.          */
/************************************************************************/
int sortLayerByOrder(mapObj *map, char* pszOrder)
{
    int *panCurrentOrder = NULL;
    int i = 0;

    if(!map) 
    {
        msSetError(MS_WEBERR, "Invalid pointer.", "sortLayerByOrder()");
        return MS_FAILURE;
    }
/* ==================================================================== */
/*      The flag "ascending" is in fact not useful since the            */
/*      default ordering is ascending.                                  */
/* ==================================================================== */

/* -------------------------------------------------------------------- */
/*      the map->layerorder should be set at this point in the          */
/*      sortLayerByMetadata.                                            */
/* -------------------------------------------------------------------- */
    if(map->layerorder)
    {
        panCurrentOrder = (int*)malloc(map->numlayers * sizeof(int));
         for (i=0; i<map->numlayers ;i++)
           panCurrentOrder[i] = map->layerorder[i];
         
         if(strcasecmp(pszOrder, "DESCENDING") == 0)
         {
             for (i=0; i<map->numlayers; i++)
               map->layerorder[i] = panCurrentOrder[map->numlayers-1-i];
         }

         free(panCurrentOrder);
    }

    return MS_SUCCESS;
}
             
             
/*!
 * This function set the map->layerorder
 * index order by the metadata collumn name
*/
int sortLayerByMetadata(mapObj *map, char* pszMetadata)
{
   int nLegendOrder1;
   int nLegendOrder2;
   char *pszLegendOrder1;
   char *pszLegendOrder2;
   int i, j;
   int tmp;

   if(!map) {
     msSetError(MS_WEBERR, "Invalid pointer.", "sortLayerByMetadata()");
     return MS_FAILURE;
   }
   
   /*
    * Initiate to default order (Reverse mapfile order)
    */
   if(map->layerorder)
   {
     int *pnLayerOrder;

     /* Backup the original layer order to be able to reverse it */
     pnLayerOrder = (int*)malloc(map->numlayers * sizeof(int));
     for (i=0; i<map->numlayers ;i++)
       pnLayerOrder[i] = map->layerorder[i];

     /* Get a new layerorder array */
     free(map->layerorder);
     map->layerorder = (int*)malloc(map->numlayers * sizeof(int));

     /* Reverse the layerorder array */
     for (i=0; i<map->numlayers ;i++)
       map->layerorder[i] = pnLayerOrder[map->numlayers - i - 1];

     free(pnLayerOrder);
   }
   else
   {
     map->layerorder = (int*)malloc(map->numlayers * sizeof(int));

     for (i=0; i<map->numlayers ;i++)
       map->layerorder[i] = map->numlayers - i - 1;
   }

   if(!pszMetadata)
     return MS_SUCCESS;
   
   /* 
    * Bubble sort algo (not very efficient)
    * should implement a kind of quick sort
    * alog instead
   */
   for (i=0; i<map->numlayers-1; i++) {
      for (j=0; j<map->numlayers-1-i; j++) {
         pszLegendOrder1 = msLookupHashTable(&(GET_LAYER(map, map->layerorder[j+1])->metadata), pszMetadata);
         pszLegendOrder2 = msLookupHashTable(&(GET_LAYER(map, map->layerorder[j])->metadata), pszMetadata);
     
         if(!pszLegendOrder1 || !pszLegendOrder2)
           continue;
         
         nLegendOrder1 = atoi(pszLegendOrder1);
         nLegendOrder2 = atoi(pszLegendOrder2);      
         
         if(nLegendOrder1 < nLegendOrder2) {  /* compare the two neighbors */
            tmp = map->layerorder[j];         /* swap a[j] and a[j+1]      */
            map->layerorder[j] = map->layerorder[j+1];
            map->layerorder[j+1] = tmp;
         }
      }
   }
   
   return MS_SUCCESS;
}

/*
** This function return a pointer
** at the begining of the first occurence
** of pszTag in pszInstr.
** 
** Tag can be [TAG] or [TAG something]
*/
char *findTag(char *pszInstr, char *pszTag)
{
   char *pszTag1, *pszStart=NULL;
   char *pszTemp;
   int done=MS_FALSE;
   int length;

   if(!pszInstr || !pszTag) {
     msSetError(MS_WEBERR, "Invalid pointer.", "findTag()");
     return NULL;
   }

   length = strlen(pszTag) + 1; /* adding [ character to the beginning */
   pszTag1 = (char*) malloc(length+1);

   strcpy(pszTag1, "[");   
   strcat(pszTag1, pszTag);

   pszTemp = pszInstr;
   while(!done) {
     pszStart = strstr(pszTemp, pszTag1);

     if(pszStart == NULL)
       done = MS_TRUE; /* tag not found */
     else if((*(pszStart+length) == ']' || *(pszStart+length) == ' '))
       done = MS_TRUE; /* valid tag */
     else
       pszTemp += length; /* skip ahead and start over */
   }

   free(pszTag1);
   
   return pszStart;
}

/* 
** This function return a pointer 
** to the end of the tag in pszTag
** 
** The end of a tag is the next
** non-quoted ']' character. 
** Return NULL if not found.
*/ 

char *findTagEnd(const char *pszTag)
{
   char *pszEnd = NULL, 
        *pszTmp = (char*)pszTag;

   while (pszTmp != NULL) {
      if (*pszTmp == '"')
         pszTmp = strchr(pszTmp+1,'"');
      if ((pszTmp == NULL) || (*pszTmp == ']')) { 
         pszEnd = pszTmp;
         pszTmp = NULL;
      } else
         pszTmp++;
   }
   
   return pszEnd;
}

/*
** Return a hashtableobj from instr of all
** arguments. hashtable must be freed by caller.
*/
int getTagArgs(char* pszTag, char* pszInstr, hashTableObj **ppoHashTable)
{
   char *pszStart, *pszEnd, *pszArgs;
   int nLength;
   char **papszArgs, **papszVarVal;
   int nArgs, nDummy;
   int i;
   
   if(!pszTag || !pszInstr) {
     msSetError(MS_WEBERR, "Invalid pointer.", "getTagArgs()");
     return MS_FAILURE;
   }
   
   /* set position to the begining of tag */
   pszStart = findTag(pszInstr, pszTag);

   if(pszStart) {
       /* find ending position */
       pszEnd = findTagEnd(pszStart);
   
      if(pszEnd) {
         /* skip the tag name */
         pszStart = pszStart + strlen(pszTag) + 1;

         /* get length of all args */
         nLength = pszEnd - pszStart;
   
         if(nLength > 0) { /* is there arguments ? */
            pszArgs = (char*)malloc(nLength + 1);
            strncpy(pszArgs, pszStart, nLength);
            pszArgs[nLength] = '\0';
            
            if(!(*ppoHashTable))
              *ppoHashTable = msCreateHashTable();
            
            /* put all arguments seperate by space in a hash table */
            papszArgs = msStringTokenize(pszArgs, " ", &nArgs, MS_TRUE);

            /* msReturnTemplateQuerycheck all argument if they have values */
            for (i=0; i<nArgs; i++) {
               if(strchr(papszArgs[i], '='))
               {
                  papszVarVal = msStringTokenize(papszArgs[i], "=", &nDummy, MS_FALSE);               
                  msInsertHashTable(*ppoHashTable, papszVarVal[0], 
                                    papszVarVal[1]);
                  free(papszVarVal[0]);
                  free(papszVarVal[1]);
                  free(papszVarVal);                  
               }
               else /* no value specified. set it to 1 */
                  msInsertHashTable(*ppoHashTable, papszArgs[i], "1");
               
               free(papszArgs[i]);
            }
            free(papszArgs);
            free(pszArgs);
         }
      }
   }  

   return MS_SUCCESS;
}   

/*
** Return a substring from instr between [tag] and [/tag]
** char * returned must be freed by caller.
** pszNextInstr will be a pointer at the end of the 
** first occurence found.
*/
int getInlineTag(char *pszTag, char *pszInstr, char **pszResult)
{
   char *pszStart, *pszEnd=NULL,  *pszEndTag, *pszPatIn, *pszPatOut=NULL, *pszTmp;
   int nInst=0;
   int nLength;

   *pszResult = NULL;

   if(!pszInstr || !pszTag) {
     msSetError(MS_WEBERR, "Invalid pointer.", "getInlineTag()");
     return MS_FAILURE;
   }

   pszEndTag = (char*)malloc(strlen(pszTag) + 3);
   strcpy(pszEndTag, "[/");
   strcat(pszEndTag, pszTag);

   /* find start tag */
   pszPatIn  = findTag(pszInstr, pszTag);
   pszPatOut = strstr(pszInstr, pszEndTag);      

   pszStart = pszPatIn;

   pszTmp = pszInstr;

   if(pszPatIn)
   {
      do 
      {
         if(pszPatIn && pszPatIn < pszPatOut)
         {
            nInst++;
         
            pszTmp = pszPatIn;
         }
      
         if(pszPatOut && ((pszPatIn == NULL) || pszPatOut < pszPatIn))
         {
            pszEnd = pszPatOut;
            nInst--;
         
            pszTmp = pszPatOut;
         }

         pszPatIn  = findTag(pszTmp+1, pszTag);
         pszPatOut = strstr(pszTmp+1, pszEndTag);
      
      }while (pszTmp != NULL && nInst > 0);
   }

   if(pszStart && pszEnd) {
      /* find end of start tag */
      pszStart = strchr(pszStart, ']');
   
      if(pszStart) {
         pszStart++;

         nLength = pszEnd - pszStart;
            
         if(nLength > 0) {
            *pszResult = (char*)malloc(nLength + 1);

            /* copy string beetween start and end tag */
            strncpy(*pszResult, pszStart, nLength);

            (*pszResult)[nLength] = '\0';
         }
      }
      else
      {
         msSetError(MS_WEBERR, "Malformed [%s] tag.", "getInlineTag()", pszTag);
         return MS_FAILURE;
      }
   }

   msFree(pszEndTag);

   return MS_SUCCESS;
}

/*!
 * this function process all if tag in pszInstr.
 * this function return a modified pszInstr.
 * ht mus contain all variables needed by the function
 * to interpret if expression.
 *
 * If bLastPass is true then all tests for 'null' values will be
 * considered true if the corresponding value is not set.
*/
int processIfTag(char **pszInstr, hashTableObj *ht, int bLastPass)
{
/*   char *pszNextInstr = pszInstr; */
   char *pszStart, *pszEnd=NULL;
   char *pszName, *pszValue, *pszOperator, *pszThen=NULL, *pszHTValue;
   char *pszIfTag;
   char *pszPatIn=NULL, *pszPatOut=NULL, *pszTmp;
   int nInst = 0;
   int bEmpty = 0;
   int nLength;

   hashTableObj *ifArgs=NULL;

   if(!*pszInstr) {
     msSetError(MS_WEBERR, "Invalid pointer.", "processIfTag()");
     return MS_FAILURE;
   }

   /* find the if start tag */
   
   pszStart  = findTag(*pszInstr, "if");

   while (pszStart) {
     pszPatIn  = findTag(pszStart, "if");
     pszPatOut = strstr(pszStart, "[/if]");
     pszTmp = pszPatIn;
      
     do {
       if(pszPatIn && pszPatIn < pszPatOut) {
         nInst++;         
         pszTmp = pszPatIn;
       }
      
       if(pszPatOut && ((pszPatIn == NULL) || pszPatOut < pszPatIn)) {
         pszEnd = pszPatOut;
         nInst--;         
         pszTmp = pszPatOut;         
       }

       pszPatIn  = findTag(pszTmp+1, "if");
       pszPatOut = strstr(pszTmp+1, "[/if]");
      
     } while (pszTmp != NULL && nInst > 0);

     /* get the then string (if expression is true) */
     if(getInlineTag("if", pszStart, &pszThen) != MS_SUCCESS) {
       msSetError(MS_WEBERR, "Malformed then if tag.", "processIfTag()");
       return MS_FAILURE;
     }
      
     /* retrieve if tag args */
     if(getTagArgs("if", pszStart, &ifArgs) != MS_SUCCESS) {
       msSetError(MS_WEBERR, "Malformed args if tag.", "processIfTag()");
       return MS_FAILURE;
     }
      
     pszName = msLookupHashTable(ifArgs, "name");
     pszValue = msLookupHashTable(ifArgs, "value");
     pszOperator = msLookupHashTable(ifArgs, "oper"); 
     if(pszOperator == NULL) /* Default operator if not set is "eq" */
       pszOperator = "eq";

     bEmpty = 0;
      
     if(pszName) {
       /* build the complete if tag ([if all_args]then string[/if]) */
       /* to replace if by then string if expression is true */
       /* or by a white space if not. */
       nLength = pszEnd - pszStart;
       pszIfTag = (char*)malloc(nLength + 6);
       strncpy(pszIfTag, pszStart, nLength);
       pszIfTag[nLength] = '\0';
       strcat(pszIfTag, "[/if]");
         
       pszHTValue = msLookupHashTable(ht, pszName);

       if(strcmp(pszOperator, "neq") == 0) {
         if(pszValue && pszHTValue && strcasecmp(pszValue, pszHTValue) != 0) {
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, pszThen);
         } else if(pszHTValue) {
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, "");
           bEmpty = 1;
         }
       } else if(strcmp(pszOperator, "eq") == 0) {
         if(pszValue && pszHTValue && strcasecmp(pszValue, pszHTValue) == 0) {
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, pszThen);
         } else if(pszHTValue) {
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, "");
           bEmpty = 1;
         }
       } else if(strcmp(pszOperator, "isnull") == 0) {
         if(pszHTValue != NULL) {
           /* We met a non-null value... condition is false */
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, "");
           bEmpty = 1;
         } else if(bLastPass) {
           /* On last pass, if value is still null then condition is true */
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, pszThen);
         }
       } else if(strcmp(pszOperator, "isset") == 0) {
         if(pszHTValue != NULL) {
           /* Found a non-null value... condition is true */
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, pszThen);
         } else if(bLastPass) {
           /* On last pass, if value still not set then condition is false */
           *pszInstr = msReplaceSubstring(*pszInstr, pszIfTag, "");
           bEmpty = 1;
         }
       } else {
         msSetError(MS_WEBERR, "Unsupported operator (%s) in if tag.",  "processIfTag()", pszOperator);
         return MS_FAILURE;
       }                    

       if(pszIfTag)
         free(pszIfTag);

       pszIfTag = NULL;
     }
      
     if(pszThen)
       free (pszThen);

     pszThen=NULL;
      
     msFreeHashTable(ifArgs);
     ifArgs=NULL;
      
     /* find the if start tag */
     if(bEmpty)
       pszStart = findTag(pszStart, "if");
     else
       pszStart = findTag(pszStart + 1, "if");
   }
   
   return MS_SUCCESS;
}

/* Helper function to return the text before the supplied string2 in string1. */
static char *getPreTagText(const char *string1, const char *string2)
{
  int n;
  char *result, *tmpstr;

  if((tmpstr = strstr(string1, string2)) == NULL) return strdup(""); /* return an empty string */

  n = strlen(string1) - strlen(tmpstr);
  result = (char *) malloc(n + 1);
  result[n] = '\0';
  strncpy(result, string1, n);

  return result;
}

/* Helper function to retunr the text after the supplied string2 in string1. */
static char *getPostTagText(const char *string1, const char *string2)
{
  char *tmpstr;

  if((tmpstr = strstr(string1, string2)) == NULL) return strdup(""); /* return an empty string */

  tmpstr += strlen(string2); /* skip string2 */
  return strdup(tmpstr); 
}

/*
** Function to process a [feature ...] tag. This tag can *only* be found within
** a [resultset ...][/resultset] block.
*/
static int processFeatureTag(mapservObj *mapserv, char **line, layerObj *layer) 
{
  char *preTag, *postTag; /* text before and after the tag */

  char *argValue;
  char *tag, *tagInstance, *tagStart;
  hashTableObj *tagArgs=NULL;

  int limit=-1;
  char *trimLast=NULL;

  int i, j, status;

  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processFeatureTag()");
    return(MS_FAILURE);
  }

  tagStart = findTag(*line, "feature");
  if(!tagStart) return(MS_SUCCESS); /* OK, just return; */

  /* check for any tag arguments */
  if(getTagArgs("feature", tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
  if(tagArgs) {
    argValue = msLookupHashTable(tagArgs, "limit");
    if(argValue) limit = atoi(argValue);

    argValue = msLookupHashTable(tagArgs, "trimlast");
    if(argValue) trimLast = argValue;
  }

  if(strstr(*line, "[/feature]") == NULL) { /* we know the closing tag must be here, if not throw an error */
    msSetError(MS_WEBERR, "[feature] tag found without closing [/feature].", "processFeatureTag()");
    return(MS_FAILURE);
  }

  if(getInlineTag("feature", *line, &tag) != MS_SUCCESS) {
    msSetError(MS_WEBERR, "Malformed feature tag.", "processFeatureTag()");
    return MS_FAILURE;
  }

  preTag = getPreTagText(*line, "[feature");
  postTag = getPostTagText(*line, "[/feature]");

  /* start rebuilding **line */
  free(*line); *line = preTag;

  /* we know the layer has query results or we wouldn't be in this code */

  status = msLayerOpen(layer); /* open the layer */
  if(status != MS_SUCCESS) return status;
  
  status = msLayerGetItems(layer); /* retrieve all the item names */
  if(status != MS_SUCCESS) return status;

  if(layer->numjoins > 0) { /* initialize necessary JOINs here */
    for(j=0; j<layer->numjoins; j++) {
      status = msJoinConnect(layer, &(layer->joins[j]));
      if(status != MS_SUCCESS) return status;
    }
  }

  mapserv->LRN = 1; /* layer result counter */
  mapserv->resultlayer = layer;
  msInitShape(&(mapserv->resultshape));

  if(limit == -1) /* return all */
    limit = layer->resultcache->numresults;
  else
    limit = MS_MIN(limit, layer->resultcache->numresults);

  for(i=0; i<limit; i++) {
    status = msLayerGetShape(layer, &(mapserv->resultshape), layer->resultcache->results[i].tileindex, layer->resultcache->results[i].shapeindex);
    if(status != MS_SUCCESS) return status;

    /* prepare any necessary JOINs here (one-to-one only) */
    if(layer->numjoins > 0) {
      for(j=0; j<layer->numjoins; j++) {
        if(layer->joins[j].type == MS_JOIN_ONE_TO_ONE) {
          msJoinPrepare(&(layer->joins[j]), &(mapserv->resultshape));
          msJoinNext(&(layer->joins[j])); /* fetch the first row */
        }
      }
    }

    /*
    ** if necessary trim a few characters off the end of the tag
    */
    if(trimLast && (i == limit-1)) {    
      char *ptr;
      if((ptr = strrstr(tag, trimLast)) != NULL)
        *ptr = '\0';
    }

    /* process the tag */
    tagInstance = processLine(mapserv, tag, NULL, QUERY); /* do substitutions */
    *line = msStringConcatenate(*line, tagInstance); /* grow the line */

    free(tagInstance);
    msFreeShape(&(mapserv->resultshape)); /* init too */

    mapserv->RN++; /* increment counters */
    mapserv->LRN++;
  }

  msLayerClose(layer);
  mapserv->resultlayer = NULL; /* necessary? */

  *line = msStringConcatenate(*line, postTag);

  /*
  ** clean up 
  */
  free(postTag);
  free(tag);

  return(MS_SUCCESS);
}

/*
** Function to process a [resultset ...] tag.
*/
static int processResultSetTag(mapservObj *mapserv, char **line, FILE *stream) 
{
  char lineBuffer[MS_BUFFER_LENGTH];
  int foundTagEnd;

  char *preTag, *postTag; /* text before and after the tag */

  char *tag, *tagStart;
  hashTableObj *tagArgs=NULL;

  char *layerName=NULL;

  int layerIndex=-1;
  layerObj *lp;

  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processResultSetTag()");
    return(MS_FAILURE);
  }

  tagStart = findTag(*line, "resultset");
  if(!tagStart) return(MS_SUCCESS); /* OK, just return; */

  if(!stream) {
    msSetError(MS_WEBERR, "Invalid file pointer.", "processResultSetTag()");
    return(MS_FAILURE);
  }

  /* check for any tag arguments */
  if(getTagArgs("resultset", tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
  if(tagArgs) {
    layerName = msLookupHashTable(tagArgs, "layer");
  }

  if(!layerName) {
    msSetError(MS_WEBERR, "[resultset] tag missing required 'layer' argument.", "processResultSetTag()");
    return(MS_FAILURE);
  }

  layerIndex = msGetLayerIndex(mapserv->map, layerName);
  if(layerIndex>=mapserv->map->numlayers || layerIndex<0) {
    msSetError(MS_MISCERR, "Layer named '%s' does not exist.", "processResultSetTag()", layerName);
    return MS_FAILURE;
  }
  lp = GET_LAYER(mapserv->map, layerIndex);

  if(strstr(*line, "[/resultset]") == NULL) { /* read ahead */
    foundTagEnd = MS_FALSE;
    while(!foundTagEnd) {
      if(fgets(lineBuffer, MS_BUFFER_LENGTH, stream) != NULL) {
        *line = msStringConcatenate(*line, lineBuffer);
        if(strstr(*line, "[/resultset]") != NULL)
          foundTagEnd = MS_TRUE;
      } else 
        break; /* ran out of file */
    }
    if(foundTagEnd == MS_FALSE) {
      msSetError(MS_WEBERR, "[resultset] tag found without closing [/resultset].", "processResultSetTag()");
      return(MS_FAILURE);
    }
  }

  if(getInlineTag("resultset", *line, &tag) != MS_SUCCESS) {
    msSetError(MS_WEBERR, "Malformed resultset tag.", "processResultSetTag()");
    return MS_FAILURE;
  }

  preTag = getPreTagText(*line, "[resultset"); /* TODO: need to handle tags in these */
  postTag = getPostTagText(*line, "[/resultset]");

  /* start rebuilding **line */
  free(*line); *line = preTag;

  if(lp->resultcache && lp->resultcache->numresults > 0) {    
    /* probably will need a while-loop here to handle multiple instances of [feature ...] tags */
    if(processFeatureTag(mapserv, &tag, lp) != MS_SUCCESS)
      return(MS_FAILURE); /* TODO: how to handle */ 
    *line = msStringConcatenate(*line, tag);
  }

  *line = msStringConcatenate(*line, postTag);

  /* clean up */
  msFreeHashTable(tagArgs); tagArgs=NULL;
  free(postTag);
  free(tag);

  return(MS_SUCCESS);
}

/*
** Function process a [include src="..."] tag.
**
** TODO's:
**   - allow URLs
*/
static int processIncludeTag(mapservObj *mapserv, char **line, FILE *stream, int mode)
{
  char *tag, *tagStart, *tagEnd;
  hashTableObj *tagArgs=NULL;
  int tagOffset, tagLength;

  char *content=NULL, *processedContent=NULL, *src=NULL;

  FILE *includeStream;
  char buffer[MS_BUFFER_LENGTH], path[MS_MAXPATHLEN];

  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processIncludeTag()");
    return(MS_FAILURE);
  }

  tagStart = findTag(*line, "include");

  /* It is OK to have no include tags, just return. */
  if( !tagStart ) return MS_SUCCESS;

  while( tagStart ) {
    tagOffset = tagStart - *line;
    
    /* check for any tag arguments */
    if(getTagArgs("include", tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
    if(tagArgs) {
      src = msLookupHashTable(tagArgs, "src");
    }

    if(!src) return(MS_SUCCESS); /* don't process the tag, could be something else so return MS_SUCCESS */

    if((includeStream = fopen(msBuildPath(path, mapserv->map->mappath, src), "r")) == NULL) {
      msSetError(MS_IOERR, src, "processIncludeTag()");
      return MS_FAILURE;
    } 
    
    while(fgets(buffer, MS_BUFFER_LENGTH, includeStream) != NULL)
      content = msStringConcatenate(content, buffer);

    /* done with included file handle */
    fclose(includeStream);

     /* find the end of the tag */
    tagEnd = strchr(tagStart, ']');
    tagEnd++;

    /* build the complete tag so we can do substitution */
    tagLength = tagEnd - tagStart;
    tag = (char *) malloc(tagLength + 1);
    strncpy(tag, tagStart, tagLength);
    tag[tagLength] = '\0';

    /* process any other tags in the content */
    processedContent = processLine(mapserv, content, stream, mode);

    /* do the replacement */
    *line = msReplaceSubstring(*line, tag, processedContent);

    /* clean up */
    free(tag); tag = NULL;
    msFreeHashTable(tagArgs); tagArgs=NULL;
    free(content);
    free(processedContent);
    
    if((*line)[tagOffset] != '\0')
      tagStart = findTag(*line+tagOffset+1, "include");
    else
      tagStart = NULL;
  }

  return(MS_SUCCESS);
}

/*
** Function to process an [item ...] tag: line contains the tag, shape holds the attributes.
*/
enum ITEM_ESCAPING {ESCAPE_HTML, ESCAPE_URL, ESCAPE_NONE};

static int processItemTag(layerObj *layer, char **line, shapeObj *shape)
{
  int i, j;

  char *tag, *tagStart, *tagEnd;
  hashTableObj *tagArgs=NULL;
  int tagOffset, tagLength;
  char *encodedTagValue=NULL, *tagValue=NULL;

  char *argValue;

  char *name=NULL;
  char *pattern=NULL;
  int precision=-1; /* don't change */
  char *format="$value";
  char *nullFormat="";
  int uc=MS_FALSE, lc=MS_FALSE, commify=MS_FALSE;

  /* int substr=MS_FALSE, substrStart, substrLength; */
  int escape=ESCAPE_HTML;

  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processItemTag()");
    return(MS_FAILURE);
  }

  tagStart = findTag(*line, "item");

  if(!tagStart) return(MS_SUCCESS); /* OK, just return; */

  while (tagStart) {  
    tagOffset = tagStart - *line;

    /* check for any tag arguments */
    if(getTagArgs("item", tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
    if(tagArgs) {
      argValue = msLookupHashTable(tagArgs, "name");
      if(argValue) name = argValue;

      argValue = msLookupHashTable(tagArgs, "pattern");
      if(argValue) pattern = argValue;

      argValue = msLookupHashTable(tagArgs, "precision");
      if(argValue) precision = atoi(argValue);

      argValue = msLookupHashTable(tagArgs, "format");
      if(argValue) format = argValue;

      argValue = msLookupHashTable(tagArgs, "nullformat");
      if(argValue) nullFormat = argValue;

      argValue = msLookupHashTable(tagArgs, "uc");
      if(argValue && strcasecmp(argValue, "true") == 0) uc = MS_TRUE;

      argValue = msLookupHashTable(tagArgs, "lc");
      if(argValue && strcasecmp(argValue, "true") == 0) lc = MS_TRUE;

      argValue = msLookupHashTable(tagArgs, "commify");
      if(argValue && strcasecmp(argValue, "true") == 0) commify = MS_TRUE;

      argValue = msLookupHashTable(tagArgs, "escape");
      if(argValue && strcasecmp(argValue, "url") == 0) escape = ESCAPE_URL;
      else if(argValue && strcasecmp(argValue, "none") == 0) escape = ESCAPE_NONE;

      /* TODO: deal with sub strings */
    }

    if(!name) {
      msSetError(MS_WEBERR, "Item tag contains no name attribute.", "processItemTag()");
      return(MS_FAILURE);
    }

    for(i=0; i<layer->numitems; i++)			
      if(strcasecmp(name, layer->items[i]) == 0) break;

    if(i == layer->numitems) {
      msSetError(MS_WEBERR, "Item name (%s) not found in layer item list.", "processItemTag()", name);
      return(MS_FAILURE);
    }    

    /*
    ** now we know which item so build the tagValue
    */
    if(shape->values[i] && strlen(shape->values[i]) > 0) {

      if(pattern && msEvalRegex(pattern, shape->values[i]) != MS_TRUE)
        tagValue = strdup(nullFormat);
      else {
        char *itemValue=NULL;

        if(precision != -1) {
          char numberFormat[16];
        
          itemValue = (char *) malloc(64); /* plenty big */
          snprintf(numberFormat, 16, "%%.%dlf", precision);
          snprintf(itemValue, 64, numberFormat, atof(shape->values[i]));
        } else
          itemValue = strdup(shape->values[i]);

        if(commify == MS_TRUE)
          itemValue = msCommifyString(itemValue);

        /* apply other effects */
        if(uc == MS_TRUE)
          for(j=0; j<strlen(itemValue); j++) itemValue[j] = toupper(itemValue[j]);
        if(lc == MS_TRUE)
          for(j=0; j<strlen(itemValue); j++) itemValue[j] = tolower(itemValue[j]);
      
        tagValue = strdup(format);
        tagValue = msReplaceSubstring(tagValue, "$value", itemValue);
        msFree(itemValue);

        if(!tagValue) {
          msSetError(MS_WEBERR, "Error applying item format.", "processItemTag()");
          return(MS_FAILURE); /* todo leaking... */
        }
      }
    } else {
      tagValue = strdup(nullFormat);
    }

    /* find the end of the tag */
    tagEnd = strchr(tagStart, ']');
    tagEnd++;

    /* build the complete tag so we can do substitution */
    tagLength = tagEnd - tagStart;
    tag = (char *) malloc(tagLength + 1);
    strncpy(tag, tagStart, tagLength);
    tag[tagLength] = '\0';

    /* do the replacement */
    switch(escape) {
    case ESCAPE_HTML:
      encodedTagValue = msEncodeHTMLEntities(tagValue);
      *line = msReplaceSubstring(*line, tag, encodedTagValue);
      break;
    case ESCAPE_URL:
      encodedTagValue = msEncodeUrl(tagValue);
      *line = msReplaceSubstring(*line, tag, encodedTagValue);
      break;  
    case ESCAPE_NONE:
      *line = msReplaceSubstring(*line, tag, tagValue);
      break;
    default:
      break;
    }

    /* clean up */
    free(tag); tag = NULL;
    msFreeHashTable(tagArgs); tagArgs=NULL;
    msFree(tagValue); tagValue=NULL;
    msFree(encodedTagValue); encodedTagValue=NULL;

    if((*line)[tagOffset] != '\0')
      tagStart = findTag(*line+tagOffset+1, "item");
    else
      tagStart = NULL;
  }

  return(MS_SUCCESS);
}

/*
** Function process any number of MapServer extent tags (e.g. shpext, mapext, etc...).
**
** TODO: Add projection support (see shpxy tag).
**       Allow percentage expansion (e.g. 10%).
*/
static int processExtentTag(mapservObj *mapserv, char **line, char *name, rectObj *extent)
{
  char *argValue;

  char *tag, *tagStart, *tagEnd;
  hashTableObj *tagArgs=NULL;
  int tagOffset, tagLength;

  char *encodedTagValue=NULL, *tagValue=NULL;

  rectObj tempExtent;
  double xExpand=0, yExpand=0;

  char number[64]; /* holds a single number in the extent */
  char numberFormat[16]="%f";
  char *format="$minx $miny $maxx $maxy";

  int precision=-1;
  int escape=ESCAPE_HTML;

  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processExtentTag()");
    return(MS_FAILURE);
  }

  tagStart = findTag(*line, name); /* this supports any extent */

  /* It is OK to have no include tags, just return. */
  if(!tagStart) return MS_SUCCESS;

  /* hack to handle tags like 'mapext_esc' easily */
  if(strstr(name, "_esc")) escape = ESCAPE_URL;

  while(tagStart) {
    tagOffset = tagStart - *line;

     /* check for any tag arguments */
    if(getTagArgs(name, tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
    if(tagArgs) {
      argValue = msLookupHashTable(tagArgs, "expand");
      if(argValue) {
        if(strchr(argValue, '%') != NULL) {
          float f;
          sscanf(argValue, "%f%%", &f);
          xExpand = ((f/100.0)*(extent->maxx-extent->minx))/2;
          yExpand = ((f/100.0)*(extent->maxy-extent->miny))/2;          
        } else {
          xExpand = atof(argValue);
          yExpand = xExpand;
        }
      }

      argValue = msLookupHashTable(tagArgs, "escape");
      if(argValue && strcasecmp(argValue, "url") == 0) escape = ESCAPE_URL;
      else if(argValue && strcasecmp(argValue, "none") == 0) escape = ESCAPE_NONE;

      argValue = msLookupHashTable(tagArgs, "format");
      if(argValue) format = argValue;

      argValue = msLookupHashTable(tagArgs, "precision");
      if(argValue) precision = atoi(argValue);
    }

    tempExtent.minx = extent->minx - xExpand;
    tempExtent.miny = extent->miny - yExpand;
    tempExtent.maxx = extent->maxx + xExpand;
    tempExtent.maxy = extent->maxy + yExpand;

    tagValue = strdup(format);

    if(precision != -1)
      snprintf(numberFormat, 16, "%%.%dlf", precision);

    snprintf(number, 64, numberFormat, tempExtent.minx);
    tagValue = msReplaceSubstring(tagValue, "$minx", number);
    snprintf(number, 64, numberFormat, tempExtent.miny);
    tagValue = msReplaceSubstring(tagValue, "$miny", number);
    snprintf(number, 64, numberFormat, tempExtent.maxx);
    tagValue = msReplaceSubstring(tagValue, "$maxx", number);
    snprintf(number, 64, numberFormat, tempExtent.maxy);
    tagValue = msReplaceSubstring(tagValue, "$maxy", number);

     /* find the end of the tag */
    tagEnd = strchr(tagStart, ']');
    tagEnd++;

    /* build the complete tag so we can do substitution */
    tagLength = tagEnd - tagStart;
    tag = (char *) malloc(tagLength + 1);
    strncpy(tag, tagStart, tagLength);
    tag[tagLength] = '\0';

    /* do the replacement */
    switch(escape) {
    case ESCAPE_HTML:
      encodedTagValue = msEncodeHTMLEntities(tagValue);
      *line = msReplaceSubstring(*line, tag, encodedTagValue);
      break;
    case ESCAPE_URL:
      encodedTagValue = msEncodeUrl(tagValue);
      *line = msReplaceSubstring(*line, tag, encodedTagValue);
      break;
    case ESCAPE_NONE:
      *line = msReplaceSubstring(*line, tag, tagValue);
      break;
    default:
      break;
    }

    /* clean up */
    free(tag); tag = NULL;
    msFreeHashTable(tagArgs); tagArgs=NULL;
    msFree(tagValue); tagValue=NULL;
    msFree(encodedTagValue); encodedTagValue=NULL;
    
    if((*line)[tagOffset] != '\0')
      tagStart = findTag(*line+tagOffset+1, name);
    else
      tagStart = NULL;
  }

  return(MS_SUCCESS);
}

/*
** Function to process a [shpxy ...] tag: line contains the tag, shape holds the coordinates. 
**
** TODO's: 
**   - May need to change attribute names.
**   - Need generalization routines (not here, but in mapprimative.c).
**   - Try to avoid all the realloc calls.
*/
static int processShpxyTag(layerObj *layer, char **line, shapeObj *shape) 
{
  int i,j;
  int status;
  
  char *tag, *tagStart, *tagEnd;
  hashTableObj *tagArgs=NULL;
  int tagOffset, tagLength;

  char *argValue;
  char *pointFormat1, *pointFormat2;
  int pointFormatLength;

  /* h = header, f=footer, s=seperator */
  char *xh="", *xf=",";
  char *yh="", *yf="";
  char *cs=" "; /* coordinate */

  char *ph="", *pf="", *ps=" "; /* part: works for multi-point/multi-linestring and simple polygons */
  char *irh="", *irf="", *irs=""; /* inner ring: necessary for complex polygons */
  char *orh="", *orf="", *ors=""; /* outer ring */

  char *sh="", *sf=""; /* shape */

  int centroid=MS_FALSE; /* output just the centroid */
  int precision=0;

  double buffer=0; /* no buffer */
  int bufferUnits=-1;

  shapeObj tShape;

  char *projectionString=NULL;
 
  char *coords=NULL, point[128];  
  
  if(!*line) {
    msSetError(MS_WEBERR, "Invalid line pointer.", "processShpxyTag()");
    return(MS_FAILURE);
  }
  if( msCheckParentPointer(layer->map,"map")==MS_FAILURE )
  return MS_FAILURE;
  
  tagStart = findTag(*line, "shpxy");

  /* It is OK to have no shpxy tags, just return. */
  if( !tagStart )
      return MS_SUCCESS;

  if(!shape || shape->numlines <= 0) { /* I suppose we need to make sure the part has vertices (need shape checker?) */
    msSetError(MS_WEBERR, "Null or empty shape.", "processShpxyTag()");
    return(MS_FAILURE);
  }

  while (tagStart) {  
    tagOffset = tagStart - *line;
    
    /* check for any tag arguments */
    if(getTagArgs("shpxy", tagStart, &tagArgs) != MS_SUCCESS) return(MS_FAILURE);
    if(tagArgs) {
      argValue = msLookupHashTable(tagArgs, "xh");
      if(argValue) xh = argValue;
      argValue = msLookupHashTable(tagArgs, "xf");
      if(argValue) xf = argValue;

      argValue = msLookupHashTable(tagArgs, "yh");
      if(argValue) yh = argValue;
      argValue = msLookupHashTable(tagArgs, "yf");
      if(argValue) yf = argValue;

      argValue = msLookupHashTable(tagArgs, "cs");
      if(argValue) cs = argValue;

      argValue = msLookupHashTable(tagArgs, "ph");
      if(argValue) ph = argValue;
      argValue = msLookupHashTable(tagArgs, "pf");
      if(argValue) pf = argValue;
      argValue = msLookupHashTable(tagArgs, "ps");
      if(argValue) ps = argValue;

      argValue = msLookupHashTable(tagArgs, "sh");
      if(argValue) sh = argValue;
      argValue = msLookupHashTable(tagArgs, "sf");
      if(argValue) sf = argValue;

      argValue = msLookupHashTable(tagArgs, "buffer");
      if(argValue) {
        buffer = atof(argValue);
        if(strstr(argValue, "px")) bufferUnits = MS_PIXELS; /* may support others at some point */
      }

      argValue = msLookupHashTable(tagArgs, "precision");
      if(argValue) precision = atoi(argValue);

      argValue = msLookupHashTable(tagArgs, "centroid");
      if(argValue) 
        if(strcasecmp(argValue,"true") == 0) centroid = MS_TRUE;

      argValue = msLookupHashTable(tagArgs, "proj");
      if(argValue) projectionString = argValue;
    }

    /* build the per point format strings (version 1 contains the coordinate seperator, version 2 doesn't) */
    pointFormatLength = strlen("xh") + strlen("xf") + strlen("yh") + strlen("yf") + strlen("cs") + 10 + 1;
    pointFormat1 = (char *) malloc(pointFormatLength);
    snprintf(pointFormat1, pointFormatLength, "%s%%.%dlf%s%s%%.%dlf%s%s", xh, precision, xf, yh, precision, yf, cs); 
    pointFormat2 = (char *) malloc(pointFormatLength); 
    snprintf(pointFormat2, pointFormatLength, "%s%%.%dlf%s%s%%.%dlf%s", xh, precision, xf, yh, precision, yf); 
 
    /* make a copy of the original shape or compute a centroid if necessary */
    msInitShape(&tShape);
    if(centroid == MS_TRUE) {
      pointObj p;

      p.x = (shape->bounds.minx + shape->bounds.maxx)/2;
      p.y = (shape->bounds.miny + shape->bounds.maxy)/2;

      tShape.type = MS_SHAPE_POINT;
      tShape.line = (lineObj *) malloc(sizeof(lineObj));
      tShape.numlines = 1;
      tShape.line[0].point = NULL; /* initialize the line */
      tShape.line[0].numpoints = 0;

      msAddPointToLine(&(tShape.line[0]), &p);      
    } 
#ifdef USE_GEOS
    else if(buffer != 0 && bufferUnits != MS_PIXELS) {
      shapeObj *bufferShape=NULL;

      bufferShape = msGEOSBuffer(shape, buffer);
      if(!bufferShape) return(MS_FAILURE); /* buffer failed */
      msCopyShape(bufferShape, &tShape);
      msFreeShape(bufferShape);
    } 
#endif 
    else {
      status = msCopyShape(shape, &tShape);
      if(status != 0) return(MS_FAILURE); /* copy failed */
    }

    /* no big deal to convert from file to image coordinates, but what are the image parameters */
    if(projectionString && strcasecmp(projectionString,"image") == 0) {
      precision = 0;

      /* if necessary, project the shape to match the map */
      if(msProjectionsDiffer(&(layer->projection), &(layer->map->projection)))
        msProjectShape(&layer->projection, &layer->map->projection, &tShape);
      
      switch(tShape.type) {
      case(MS_SHAPE_POINT):
        /* at this point we only convert the first point of the first shape */
        tShape.line[0].point[0].x = MS_MAP2IMAGE_X(tShape.line[0].point[0].x, layer->map->extent.minx, layer->map->cellsize);
        tShape.line[0].point[0].y = MS_MAP2IMAGE_Y(tShape.line[0].point[0].y, layer->map->extent.maxy, layer->map->cellsize);
        break;
      case(MS_SHAPE_LINE):
        msClipPolylineRect(&tShape, layer->map->extent);
        break;
      case(MS_SHAPE_POLYGON):
        msClipPolygonRect(&tShape, layer->map->extent);
        break;
      default:
        /* TO DO: need an error message here */
        return(MS_FAILURE);
        break;
      }
      msTransformShapeToPixel(&tShape, layer->map->extent, layer->map->cellsize);

#ifdef USE_GEOS
      if(buffer != 0 && bufferUnits == MS_PIXELS) {
        shapeObj *bufferShape=NULL;

	bufferShape = msGEOSBuffer(&tShape, buffer);
	if(!bufferShape) return(MS_FAILURE); /* buffer failed */
	msFreeShape(&tShape); /* avoid memory leak */
	msCopyShape(bufferShape, &tShape);
	msFreeShape(bufferShape);
      }
#endif

    } else if(projectionString) {
       projectionObj projection;
       msInitProjection(&projection);

       status = msLoadProjectionString(&projection, projectionString);
       if(status != MS_SUCCESS) return MS_FAILURE;

       if(msProjectionsDiffer(&(layer->projection), &projection)) 
         msProjectShape(&layer->projection, &projection, &tShape);
    }
      
    /* TODO: add thinning support here */
      
    /* build the coordinate string */
    if(strlen(sh) > 0) coords = msStringConcatenate(coords, sh);
    for(i=0; i<tShape.numlines; i++) { /* e.g. part */

      /* skip degenerate parts, really should only happen with pixel output */ 
      if((tShape.type == MS_SHAPE_LINE && tShape.line[i].numpoints < 2) ||
        (tShape.type == MS_SHAPE_POLYGON && tShape.line[i].numpoints < 3))
        continue;

      if(strlen(ph) > 0) coords = msStringConcatenate(coords, ph);
      for(j=0; j<tShape.line[i].numpoints-1; j++) {
        snprintf(point, 128, pointFormat1, tShape.line[i].point[j].x, tShape.line[i].point[j].y);
        coords = msStringConcatenate(coords, point);  
      }
      snprintf(point, 128, pointFormat2, tShape.line[i].point[j].x, tShape.line[i].point[j].y);
      coords = msStringConcatenate(coords, point);  
      if(strlen(pf) > 0) coords = msStringConcatenate(coords, pf);
      if((i < tShape.numlines-1) && (strlen(ps) > 0)) coords = msStringConcatenate(coords, ps);
    }
    if(strlen(sf) > 0) coords = msStringConcatenate(coords, sf);

    msFreeShape(&tShape);
    
    /* find the end of the tag */
    tagEnd = findTagEnd(tagStart);
    tagEnd++;

    /* build the complete tag so we can do substitution */
    tagLength = tagEnd - tagStart;
    tag = (char *) malloc(tagLength + 1);
    strncpy(tag, tagStart, tagLength);
    tag[tagLength] = '\0';

    /* do the replacement */
    *line = msReplaceSubstring(*line, tag, coords);

    /* clean up */
    free(tag); tag = NULL;
    msFreeHashTable(tagArgs); tagArgs=NULL;
    free(pointFormat1);
    free(pointFormat2);
    free(coords);

    if((*line)[tagOffset] != '\0')
      tagStart = findTag(*line+tagOffset+1, "shpxy");
    else
      tagStart = NULL;  
  }

  return(MS_SUCCESS);
}

/*!
 * this function process all metadata
 * in pszInstr. ht mus contain all corresponding
 * metadata value.
 * 
 * this function return a modified pszInstr
*/
int processMetadata(char** pszInstr, hashTableObj *ht)
{
/* char *pszNextInstr = pszInstr; */
   char *pszEnd, *pszStart;
   char *pszMetadataTag;
   char *pszHashName;
   char *pszHashValue;
   int nLength, nOffset;

   hashTableObj *metadataArgs = NULL;

   if(!*pszInstr) {
     msSetError(MS_WEBERR, "Invalid pointer.", "processMetadata()");
     return MS_FAILURE;
   }

   /* set position to the begining of metadata tag */
   pszStart = findTag(*pszInstr, "metadata");

   while (pszStart) {
      /* get metadata args */
      if(getTagArgs("metadata", pszStart, &metadataArgs) != MS_SUCCESS)
        return MS_FAILURE;

      pszHashName = msLookupHashTable(metadataArgs, "name");
      pszHashValue = msLookupHashTable(ht, pszHashName);
      
      nOffset = pszStart - *pszInstr;

      if(pszHashName && pszHashValue) {
           /* set position to the end of metadata start tag */
           pszEnd = strchr(pszStart, ']');
           pszEnd++;

           /* build the complete metadata tag ([metadata all_args]) */
           /* to replace it by the corresponding value from ht */
           nLength = pszEnd - pszStart;
           pszMetadataTag = (char*)malloc(nLength + 1);
           strncpy(pszMetadataTag, pszStart, nLength);
           pszMetadataTag[nLength] = '\0';

           *pszInstr = msReplaceSubstring(*pszInstr, pszMetadataTag, pszHashValue);

           free(pszMetadataTag);
           pszMetadataTag=NULL;
      }

      msFreeHashTable(metadataArgs);
      metadataArgs=NULL;


      /* set position to the begining of the next metadata tag */
      if((*pszInstr)[nOffset] != '\0')
        pszStart = findTag(*pszInstr+nOffset+1, "metadata");
      else
        pszStart = NULL;
   }

   return MS_SUCCESS;
}

/*!
 * this function process all icon tag
 * from pszInstr.
 * 
 * This func return a modified pszInstr.
*/
int processIcon(mapObj *map, int nIdxLayer, int nIdxClass, char** pszInstr, char* pszPrefix)
{
   int nWidth, nHeight, nLen;
   char szImgFname[1024], *pszFullImgFname=NULL, *pszImgTag;
   char szPath[MS_MAXPATHLEN];
   hashTableObj *myHashTable=NULL;
   FILE *fIcon;
   
   if(!map || 
       nIdxLayer > map->numlayers || 
       nIdxLayer < 0 ) {
     msSetError(MS_WEBERR, "Invalid pointer.", "processIcon()");
     return MS_FAILURE;
   }

   /* find the begining of tag */
   pszImgTag = strstr(*pszInstr, "[leg_icon");
   
   while (pszImgTag) {
      int i;
      char szStyleCode[512] = "";
      classObj *thisClass=NULL;

      /* It's okay to have no classes... we'll generate an empty icon in this case */
      if(nIdxClass >= 0 && nIdxClass < GET_LAYER(map, nIdxLayer)->numclasses)
          thisClass = GET_LAYER(map, nIdxLayer)->class[nIdxClass];

      if(getTagArgs("leg_icon", pszImgTag, &myHashTable) != MS_SUCCESS)
        return MS_FAILURE;

      /* if no specified width or height, set them to map default */
      if(!msLookupHashTable(myHashTable, "width") || !msLookupHashTable(myHashTable, "height")) {
         nWidth = map->legend.keysizex;
         nHeight= map->legend.keysizey;
      }
      else {
         nWidth  = atoi(msLookupHashTable(myHashTable, "width"));
         nHeight = atoi(msLookupHashTable(myHashTable, "height"));
      }

      /* Create a unique and predictable filename to cache the legend icons.
       * Include some key parameters from the first 2 styles 
       */
      for(i=0; i<2 && thisClass && i<thisClass->numstyles; i++) {
          styleObj *style;
          char *pszSymbolNameHash = NULL;
          style = thisClass->styles[i];
          if(style->symbolname)
              pszSymbolNameHash = msHashString(style->symbolname);

          snprintf(szStyleCode+strlen(szStyleCode), 255,
                   "s%d_%x_%x_%x_%d_%s_%g",
                   i, MS_COLOR_GETRGB(style->color), MS_COLOR_GETRGB(style->backgroundcolor), MS_COLOR_GETRGB(style->outlinecolor),
                   style->symbol, pszSymbolNameHash?pszSymbolNameHash:"", 
                   style->angle);
          msFree(pszSymbolNameHash);
      }

      snprintf(szImgFname, 1024, "%s_%d_%d_%d_%d_%s.%s%c", 
               pszPrefix, nIdxLayer, nIdxClass, nWidth, nHeight, 
               szStyleCode, MS_IMAGE_EXTENSION(map->outputformat),'\0');

      pszFullImgFname = strdup(msBuildPath3(szPath, map->mappath, 
                                            map->web.imagepath, szImgFname));
      
      /* check if icon already exist in cache */
      if((fIcon = fopen(pszFullImgFname, "r")) != NULL)
      {
         /* File already exists. No need to generate it again */
         fclose(fIcon);
      }
      else
      {
         /* Create an image corresponding to the current class */
          imageObj *img=NULL;

         if(thisClass == NULL)
         {
             /* Nonexistent class.  Create an empty image */
             img = msCreateLegendIcon(map, NULL, NULL, nWidth, nHeight);
         }
         else
         {
            img = msCreateLegendIcon(map, GET_LAYER(map, nIdxLayer), 
                                     thisClass, nWidth, nHeight);
         }

         if(!img) {
            if(myHashTable)
              msFreeHashTable(myHashTable);

            msSetError(MS_GDERR, "Error while creating GD image.", "processIcon()");
            return MS_FAILURE;
         }
         
         /* save it with a unique file name */
         if(msSaveImage(map, img, pszFullImgFname) != MS_SUCCESS) {
            if(myHashTable)
              msFreeHashTable(myHashTable);

            msFree(pszFullImgFname);
            msFreeImage(img);

            msSetError(MS_IOERR, "Error saving GD image to disk (%s).", "processIcon()", pszFullImgFname);
            return MS_FAILURE;
         }
         
         msFreeImage(img);
      }

      msFree(pszFullImgFname);
      pszFullImgFname = NULL;

      nLen = (strchr(pszImgTag, ']') + 1) - pszImgTag;
   
      if(nLen > 0) {
         char *pszTag;

         /* rebuid image tag ([leg_class_img all_args]) */
         /* to replace it by the image url */
         pszTag = (char*)malloc(nLen + 1);
         strncpy(pszTag, pszImgTag, nLen);
            
         pszTag[nLen] = '\0';

         pszFullImgFname = (char*)malloc(strlen(map->web.imageurl) + strlen(szImgFname) + 1);
         strcpy(pszFullImgFname, map->web.imageurl);
         strcat(pszFullImgFname, szImgFname);

         *pszInstr = msReplaceSubstring(*pszInstr, pszTag, pszFullImgFname);

         msFree(pszFullImgFname);
         pszFullImgFname = NULL;
         msFree(pszTag);

         /* find the begining of tag */
         pszImgTag = strstr(*pszInstr, "[leg_icon");
      }
      else {
         pszImgTag = NULL;
      }
      
      if(myHashTable)
      {
         msFreeHashTable(myHashTable);
         myHashTable = NULL;
      }
   }

   return MS_SUCCESS;
}

/*!
 * Replace all tags from group template
 * with correct value.
 * 
 * this function return a buffer containing
 * the template with correct values.
 * 
 * buffer must be freed by caller.
*/
int generateGroupTemplate(char* pszGroupTemplate, mapObj *map, char* pszGroupName, hashTableObj *oGroupArgs, char **pszTemp, char* pszPrefix)
{
   hashTableObj *myHashTable;
   char pszStatus[3];   
   char *pszClassImg;
   char *pszOptFlag = NULL;
   int i, j;
   int nOptFlag = 15;
   int bShowGroup;

   *pszTemp = NULL;
   
   if(!pszGroupName || !pszGroupTemplate) {
     msSetError(MS_WEBERR, "Invalid pointer.", "generateGroupTemplate()");
     return MS_FAILURE;
   }

   /*
    * Get the opt_flag is any.
    */
   if(oGroupArgs)
       pszOptFlag = msLookupHashTable(oGroupArgs, "opt_flag");

   if(pszOptFlag)
       nOptFlag = atoi(pszOptFlag);

   /*
    * Check all layers, if one in the group
    * should be visible, print the group.
    * (Check for opt_flag)
    */
   bShowGroup = 0;
   for (j=0; j<map->numlayers; j++)
   {
       if(GET_LAYER(map, map->layerorder[j])->group && 
           strcmp(GET_LAYER(map, map->layerorder[j])->group, pszGroupName) == 0)
       {
           /* dont display layer is off. */
           if( (nOptFlag & 2) == 0 && 
               GET_LAYER(map, map->layerorder[j])->status == MS_OFF )
               bShowGroup = 0;
           else
               bShowGroup = 1;

           /* dont display layer is query. */
           if( (nOptFlag & 4) == 0  && 
               GET_LAYER(map, map->layerorder[j])->type == MS_LAYER_QUERY )
               bShowGroup = 0;

           /* dont display layer is annotation. */
           if( (nOptFlag & 8) == 0 && 
               GET_LAYER(map, map->layerorder[j])->type == MS_LAYER_ANNOTATION )
               bShowGroup = 0;
               

           /* dont display layer if out of scale. */
           if((nOptFlag & 1) == 0)
           {
               if(map->scaledenom > 0) {
                   if((GET_LAYER(map, map->layerorder[j])->maxscaledenom > 0) && 
                      (map->scaledenom > GET_LAYER(map, map->layerorder[j])->maxscaledenom))
                       bShowGroup = 0;
                   if((GET_LAYER(map, map->layerorder[j])->minscaledenom > 0) && 
                      (map->scaledenom <= GET_LAYER(map, map->layerorder[j])->minscaledenom))
                       bShowGroup = 0;
               }
           }

           /* The group contains one visible layer */
           /* Draw the group */
           if( bShowGroup )
               break;
       }
   }

   if( ! bShowGroup )
       return MS_SUCCESS;
   
   /*
    * Work from a copy
    */
   *pszTemp = (char*)malloc(strlen(pszGroupTemplate) + 1);
   strcpy(*pszTemp, pszGroupTemplate);
         
   /*
    * Change group tags
    */
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_group_name]", pszGroupName);

   
   /*
    * Create a hash table that contain info
    * on current layer
    */
   myHashTable = msCreateHashTable();

   /*
    * Check for the first layer
    * that belong to this group.
    * Get his status and check for if.
    */
   for (j=0; j<map->numlayers; j++)
   {
      if(GET_LAYER(map, map->layerorder[j])->group && strcmp(GET_LAYER(map, map->layerorder[j])->group, pszGroupName) == 0)
      {
         sprintf(pszStatus, "%d", GET_LAYER(map, map->layerorder[j])->status);
         msInsertHashTable(myHashTable, "layer_status", pszStatus);
         msInsertHashTable(myHashTable, "layer_visible", msLayerIsVisible(map, GET_LAYER(map, map->layerorder[j]))?"1":"0" );
         msInsertHashTable(myHashTable, "group_name", pszGroupName);

         if(processIfTag(pszTemp, myHashTable, MS_FALSE) != MS_SUCCESS)
           return MS_FAILURE;
         
         if(processIfTag(pszTemp, &(GET_LAYER(map, map->layerorder[j])->metadata), MS_FALSE) != MS_SUCCESS)
           return MS_FAILURE;

         if(processMetadata(pszTemp, &GET_LAYER(map, map->layerorder[j])->metadata) != MS_SUCCESS)
           return MS_FAILURE;

         break;
      }
   }
  
   msFreeHashTable(myHashTable);

   /*
    * Process all metadata tags
    * only web object is accessible
   */
   if(processMetadata(pszTemp, &(map->web.metadata)) != MS_SUCCESS)
     return MS_FAILURE;
   
   /*
    * check for if tag
   */
   if(processIfTag(pszTemp, &(map->web.metadata), MS_TRUE) != MS_SUCCESS)
     return MS_FAILURE;
   
   /*
    * Check if leg_icon tag exist
    * if so display the first layer first class icon
    */
   pszClassImg = strstr(*pszTemp, "[leg_icon");
   if(pszClassImg) {
      /* find first layer of this group */
      for (i=0; i<map->numlayers; i++)
        if(GET_LAYER(map, map->layerorder[i])->group && strcmp(GET_LAYER(map, map->layerorder[i])->group, pszGroupName) == 0)
          processIcon(map, map->layerorder[i], 0, pszTemp, pszPrefix);
   }      
      
   return MS_SUCCESS;
}

/*!
 * Replace all tags from layer template
 * with correct value.
 * 
 * this function return a buffer containing
 * the template with correct values.
 * 
 * buffer must be freed by caller.
*/
int generateLayerTemplate(char *pszLayerTemplate, mapObj *map, int nIdxLayer, hashTableObj *oLayerArgs, char **pszTemp, char* pszPrefix)
{
   hashTableObj *myHashTable;
   char szStatus[10];
   char szType[10];
   
   int nOptFlag=0;
   char *pszOptFlag = NULL;
   char *pszClassImg;

   char szTmpstr[128]; /* easily big enough for the couple of instances we need */

   *pszTemp = NULL;
   
   if(!pszLayerTemplate || 
       !map || 
       nIdxLayer > map->numlayers ||
       nIdxLayer < 0 ) {
     msSetError(MS_WEBERR, "Invalid pointer.", "generateLayerTemplate()");
     return MS_FAILURE;
   }

   if(oLayerArgs)
       pszOptFlag = msLookupHashTable(oLayerArgs, "opt_flag");

   if(pszOptFlag)
     nOptFlag = atoi(pszOptFlag);

   /* don't display deleted layers */
   if(GET_LAYER(map, nIdxLayer)->status == MS_DELETE)
     return MS_SUCCESS;

   /* dont display layer is off. */
   /* check this if Opt flag is not set */
   if((nOptFlag & 2) == 0 && GET_LAYER(map, nIdxLayer)->status == MS_OFF)
     return MS_SUCCESS;

   /* dont display layer is query. */
   /* check this if Opt flag is not set */
   if((nOptFlag & 4) == 0  && GET_LAYER(map, nIdxLayer)->type == MS_LAYER_QUERY)
     return MS_SUCCESS;

   /* dont display layer is annotation. */
   /* check this if Opt flag is not set       */
   if((nOptFlag & 8) == 0 && GET_LAYER(map, nIdxLayer)->type == MS_LAYER_ANNOTATION)
     return MS_SUCCESS;      

   /* dont display layer if out of scale. */
   /* check this if Opt flag is not set             */
   if((nOptFlag & 1) == 0) {
      if(map->scaledenom > 0) {
         if((GET_LAYER(map, nIdxLayer)->maxscaledenom > 0) && (map->scaledenom > GET_LAYER(map, nIdxLayer)->maxscaledenom))
           return MS_SUCCESS;
         if((GET_LAYER(map, nIdxLayer)->minscaledenom > 0) && (map->scaledenom <= GET_LAYER(map, nIdxLayer)->minscaledenom))
           return MS_SUCCESS;
      }
   }

   /*
    * Work from a copy
    */
   *pszTemp = strdup(pszLayerTemplate);

   /*
    * Change layer tags
    */
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_name]", GET_LAYER(map, nIdxLayer)->name);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_group]", GET_LAYER(map, nIdxLayer)->group);

   snprintf(szTmpstr, 128, "%d", nIdxLayer); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_index]", szTmpstr);

   snprintf(szTmpstr, 128, "%g", GET_LAYER(map, nIdxLayer)->minscaledenom); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_minscale]", szTmpstr);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_minscaledenom]", szTmpstr);
   snprintf(szTmpstr, 128, "%g", GET_LAYER(map, nIdxLayer)->maxscaledenom); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_maxscale]", szTmpstr);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_maxscaledenom]", szTmpstr);

   /*
    * Create a hash table that contain info
    * on current layer
    */
   myHashTable = msCreateHashTable();
   
   /*
    * for now, only status and type is required by template
    */
   sprintf(szStatus, "%d", GET_LAYER(map, nIdxLayer)->status);
   msInsertHashTable(myHashTable, "layer_status", szStatus);

   sprintf(szType, "%d", GET_LAYER(map, nIdxLayer)->type);
   msInsertHashTable(myHashTable, "layer_type", szType);

   msInsertHashTable(myHashTable, "layer_name", (GET_LAYER(map, nIdxLayer)->name)? GET_LAYER(map, nIdxLayer)->name : "");
   msInsertHashTable(myHashTable, "layer_group", (GET_LAYER(map, nIdxLayer)->group)? GET_LAYER(map, nIdxLayer)->group : "");
   msInsertHashTable(myHashTable, "layer_visible", msLayerIsVisible(map, GET_LAYER(map, nIdxLayer))?"1":"0" );

   if(processIfTag(pszTemp, myHashTable, MS_FALSE) != MS_SUCCESS)
      return MS_FAILURE;
   
   if(processIfTag(pszTemp, &(GET_LAYER(map, nIdxLayer)->metadata), MS_FALSE) != MS_SUCCESS)
      return MS_FAILURE;
   
   if(processIfTag(pszTemp, &(map->web.metadata), MS_TRUE) != MS_SUCCESS)
      return MS_FAILURE;

   msFreeHashTable(myHashTable);
   
   /*
    * Check if leg_icon tag exist
    * if so display the first class icon
    */
   pszClassImg = strstr(*pszTemp, "[leg_icon");
   if(pszClassImg) {
      processIcon(map, nIdxLayer, 0, pszTemp, pszPrefix);
   }      

   /* process all metadata tags
    * only current layer and web object
    * metaddata are accessible
   */
   if(processMetadata(pszTemp, &GET_LAYER(map, nIdxLayer)->metadata) != MS_SUCCESS)
      return MS_FAILURE;

   if(processMetadata(pszTemp, &(map->web.metadata)) != MS_SUCCESS)
      return MS_FAILURE;      
   
   return MS_SUCCESS;
}

/*!
 * Replace all tags from class template
 * with correct value.
 * 
 * this function return a buffer containing
 * the template with correct values.
 * 
 * buffer must be freed by caller.
*/
int generateClassTemplate(char* pszClassTemplate, mapObj *map, int nIdxLayer, int nIdxClass, hashTableObj *oClassArgs, char **pszTemp, char* pszPrefix)
{
   hashTableObj *myHashTable;
   char szStatus[10];
   char szType[10];
   
   char *pszClassImg;
   int nOptFlag=0;
   char *pszOptFlag = NULL;

   char szTmpstr[128]; /* easily big enough for the couple of instances we need */

   *pszTemp = NULL;
   
   if(!pszClassTemplate ||
       !map || 
       nIdxLayer > map->numlayers ||
       nIdxLayer < 0 ||
       nIdxClass > GET_LAYER(map, nIdxLayer)->numclasses ||
       nIdxClass < 0) {
        
     msSetError(MS_WEBERR, "Invalid pointer.", "generateClassTemplate()");
     return MS_FAILURE;
   }

   if(oClassArgs)
     pszOptFlag = msLookupHashTable(oClassArgs, "Opt_flag");

   if(pszOptFlag)
     nOptFlag = atoi(pszOptFlag);
      
   /* don't display deleted layers */
   if(GET_LAYER(map, nIdxLayer)->status == MS_DELETE)
     return MS_SUCCESS;

   /* dont display class if layer is off. */
   /* check this if Opt flag is not set */
   if((nOptFlag & 2) == 0 && GET_LAYER(map, nIdxLayer)->status == MS_OFF)
     return MS_SUCCESS;

   /* dont display class if layer is query. */
   /* check this if Opt flag is not set       */
   if((nOptFlag & 4) == 0 && GET_LAYER(map, nIdxLayer)->type == MS_LAYER_QUERY)
     return MS_SUCCESS;
      
   /* dont display class if layer is annotation. */
   /* check this if Opt flag is not set       */
   if((nOptFlag & 8) == 0 && GET_LAYER(map, nIdxLayer)->type == MS_LAYER_ANNOTATION)
     return MS_SUCCESS;
      
   /* dont display layer if out of scale. */
   /* check this if Opt flag is not set */
   if((nOptFlag & 1) == 0) {
      if(map->scaledenom > 0) {
         if((GET_LAYER(map, nIdxLayer)->maxscaledenom > 0) && (map->scaledenom > GET_LAYER(map, nIdxLayer)->maxscaledenom))
           return MS_SUCCESS;
         if((GET_LAYER(map, nIdxLayer)->minscaledenom > 0) && (map->scaledenom <= GET_LAYER(map, nIdxLayer)->minscaledenom))
           return MS_SUCCESS;
      }
   }
      
   /*
    * Work from a copy
    */
   *pszTemp = (char*)malloc(strlen(pszClassTemplate) + 1);
   strcpy(*pszTemp, pszClassTemplate);
         
   /*
    * Change class tags
    */
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_name]", GET_LAYER(map, nIdxLayer)->class[nIdxClass]->name);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_title]", GET_LAYER(map, nIdxLayer)->class[nIdxClass]->title);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_layer_name]", GET_LAYER(map, nIdxLayer)->name);

   snprintf(szTmpstr, 128, "%d", nIdxClass); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_index]", szTmpstr);

   snprintf(szTmpstr, 128, "%g", GET_LAYER(map, nIdxLayer)->class[nIdxClass]->minscaledenom); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_minscale]", szTmpstr);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_minscaledenom]", szTmpstr);
   snprintf(szTmpstr, 128, "%g", GET_LAYER(map, nIdxLayer)->class[nIdxClass]->maxscaledenom); 
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_maxscale]", szTmpstr);
   *pszTemp = msReplaceSubstring(*pszTemp, "[leg_class_maxscaledenom]", szTmpstr);

   /*
    * Create a hash table that contain info
    * on current layer
    */
   myHashTable = msCreateHashTable();
   
   /*
    * for now, only status, type, name and group are  required by template
    */
   sprintf(szStatus, "%d", GET_LAYER(map, nIdxLayer)->status);
   msInsertHashTable(myHashTable, "layer_status", szStatus);

   sprintf(szType, "%d", GET_LAYER(map, nIdxLayer)->type);
   msInsertHashTable(myHashTable, "layer_type", szType);   
   
   msInsertHashTable(myHashTable, "layer_name", 
     (GET_LAYER(map, nIdxLayer)->name)? GET_LAYER(map, nIdxLayer)->name : "");
   msInsertHashTable(myHashTable, "layer_group", 
     (GET_LAYER(map, nIdxLayer)->group)? GET_LAYER(map, nIdxLayer)->group : "");
   msInsertHashTable(myHashTable, "layer_visible", msLayerIsVisible(map, GET_LAYER(map, nIdxLayer))?"1":"0" );
   msInsertHashTable(myHashTable, "class_name", 
     (GET_LAYER(map, nIdxLayer)->class[nIdxClass]->name)? GET_LAYER(map, nIdxLayer)->class[nIdxClass]->name : "");

   if(processIfTag(pszTemp, myHashTable, MS_FALSE) != MS_SUCCESS)
      return MS_FAILURE;
   
   if(processIfTag(pszTemp, &(GET_LAYER(map, nIdxLayer)->metadata), MS_FALSE) != MS_SUCCESS)
      return MS_FAILURE;
   
   if(processIfTag(pszTemp, &(map->web.metadata), MS_TRUE) != MS_SUCCESS)
      return MS_FAILURE;

   msFreeHashTable(myHashTable);   
      
   /*
    * Check if leg_icon tag exist
    */
   pszClassImg = strstr(*pszTemp, "[leg_icon");
   if(pszClassImg) {
      processIcon(map, nIdxLayer, nIdxClass, pszTemp, pszPrefix);
   }

   /* process all metadata tags
    * only current layer and web object
    * metaddata are accessible
   */
   if(processMetadata(pszTemp, &GET_LAYER(map, nIdxLayer)->metadata) != MS_SUCCESS)
      return MS_FAILURE;
   
   if(processMetadata(pszTemp, &(map->web.metadata)) != MS_SUCCESS)
      return MS_FAILURE;      
   
   return MS_SUCCESS;
}

char *generateLegendTemplate(mapservObj *mapserv)
{
   FILE *stream;
   char *file = NULL;
   int length;
   char *pszResult = NULL;
   char *legGroupHtml = NULL;
   char *legLayerHtml = NULL;
   char *legClassHtml = NULL;
   char *legLayerHtmlCopy = NULL;
   char *legClassHtmlCopy = NULL;
   char *legGroupHtmlCopy = NULL;
   
   char *legHeaderHtml = NULL;
   char *legFooterHtml = NULL;

   char *pszPrefix = NULL;
   char *pszMapFname = NULL;
   
   struct stat tmpStat;
   
   char *pszOrderMetadata = NULL;
   char *pszOrder = NULL;
   
   int i,j,k;
   char **papszGroups = NULL;
   int nGroupNames = 0;

   int nLegendOrder = 0;
   char *pszOrderValue;
     
   hashTableObj *groupArgs = NULL;
   hashTableObj *layerArgs = NULL;
   hashTableObj *classArgs = NULL;     

   ms_regex_t re; /* compiled regular expression to be matched */ 

   int  *panCurrentDrawingOrder = NULL;
   char szPath[MS_MAXPATHLEN];

   if(ms_regcomp(&re, MS_TEMPLATE_EXPR, MS_REG_EXTENDED|MS_REG_NOSUB) != 0) {
      msSetError(MS_IOERR, "Error regcomp.", "generateLegendTemplate()");      
      return NULL;
   }

   if(ms_regexec(&re, mapserv->map->legend.template, 0, NULL, 0) != 0) { /* no match */
      msSetError(MS_IOERR, "Invalid template file name.", "generateLegendTemplate()");      
     ms_regfree(&re);
     return NULL;
   }
   ms_regfree(&re);

/* -------------------------------------------------------------------- */
/*      Save the current drawing order. The drawing order is reset      */
/*      at the end of the function.                                     */
/* -------------------------------------------------------------------- */
   if(mapserv && mapserv->map && mapserv->map->numlayers > 0)
   {
       panCurrentDrawingOrder = 
           (int *)malloc(sizeof(int)*mapserv->map->numlayers); 
      
       for (i=0; i<mapserv->map->numlayers; i++)
       {
           if(mapserv->map->layerorder)
               panCurrentDrawingOrder[i] = mapserv->map->layerorder[i];
           else
             panCurrentDrawingOrder[i] = i;  
       }
   }

   /*
    * build prefix filename
    * for legend icon creation
   */
   for(i=0;i<mapserv->request->NumParams;i++) /* find the mapfile parameter first */
     if(strcasecmp(mapserv->request->ParamNames[i], "map") == 0) break;
  
   if(i == mapserv->request->NumParams)
   {
       if( getenv("MS_MAPFILE"))
           pszMapFname = msStringConcatenate(pszMapFname, getenv("MS_MAPFILE"));
   }
   else 
   {
      if(getenv(mapserv->request->ParamValues[i])) /* an environment references the actual file to use */
        pszMapFname = msStringConcatenate(pszMapFname, getenv(mapserv->request->ParamValues[i]));
      else
        pszMapFname = msStringConcatenate(pszMapFname, mapserv->request->ParamValues[i]);
   }
   
   if(pszMapFname)
   {
       if(stat(pszMapFname, &tmpStat) != -1)
       {
           int nLen;

           nLen = (mapserv->map->name?strlen(mapserv->map->name):0)  + 50;
           pszPrefix = (char*)malloc((nLen+1) * sizeof(char));
           if(pszPrefix == NULL) {

           }
           snprintf(pszPrefix, nLen, "%s_%ld_%ld", 
                    mapserv->map->name,
                    (long) tmpStat.st_size, 
                    (long) tmpStat.st_mtime);
           pszPrefix[nLen] = '\0';
       }
   
       free(pszMapFname);
       pszMapFname = NULL;
   }
   else
   {
/* -------------------------------------------------------------------- */
/*      map file name may not be avaible when the template functions    */
/*      are called from mapscript. Use the time stamp as prefix.        */
/* -------------------------------------------------------------------- */
       char pszTime[20];
       
       snprintf(pszTime, 20, "%ld", (long)time(NULL));      
       pszPrefix = msStringConcatenate(pszPrefix, pszTime);
   }

       /* open template */
   if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, mapserv->map->legend.template), "r")) == NULL) {
      msSetError(MS_IOERR, "Error while opening template file.", "generateLegendTemplate()");
      return NULL;
   } 

   fseek(stream, 0, SEEK_END);
   length = ftell(stream);
   rewind(stream);
   
   file = (char*)malloc(length + 1);

   if(!file) {
     msSetError(MS_IOERR, "Error while allocating memory for template file.", "generateLegendTemplate()");
     return NULL;
   }
   
   /*
    * Read all the template file
    */
   fread(file, 1, length, stream);
   file[length] = '\0';

   if(msValidateContexts(mapserv->map) != MS_SUCCESS) return NULL; /* make sure there are no recursive REQUIRES or LABELREQUIRES expressions */

   /*
    * Seperate header/footer, groups, layers and class
    */
   getInlineTag("leg_header_html", file, &legHeaderHtml);
   getInlineTag("leg_footer_html", file, &legFooterHtml);
   getInlineTag("leg_group_html", file, &legGroupHtml);
   getInlineTag("leg_layer_html", file, &legLayerHtml);
   getInlineTag("leg_class_html", file, &legClassHtml);
   
   /*
    * Retrieve arguments of all three parts
    */
   if(legGroupHtml) 
     if(getTagArgs("leg_group_html", file, &groupArgs) != MS_SUCCESS)
       return NULL;
   
   if(legLayerHtml) 
     if(getTagArgs("leg_layer_html", file, &layerArgs) != MS_SUCCESS)
       return NULL;
   
   if(legClassHtml) 
     if(getTagArgs("leg_class_html", file, &classArgs) != MS_SUCCESS)
       return NULL;

      
   mapserv->map->cellsize = msAdjustExtent(&(mapserv->map->extent), 
                                         mapserv->map->width, 
                                         mapserv->map->height);
   if(msCalculateScale(mapserv->map->extent, mapserv->map->units, 
                       mapserv->map->width, mapserv->map->height, 
                       mapserv->map->resolution, &mapserv->map->scaledenom) != MS_SUCCESS)
     return(NULL);
   
   /* start with the header if present */
   if(legHeaderHtml) pszResult = msStringConcatenate(pszResult, legHeaderHtml);

   /********************************************************************/

   /*
    * order layers if order_metadata args is set
    * If not, keep default order
    */
   pszOrderMetadata = msLookupHashTable(layerArgs, "order_metadata");
      
   if(sortLayerByMetadata(mapserv->map, pszOrderMetadata) != MS_SUCCESS)
     goto error;
   
/* -------------------------------------------------------------------- */
/*      if the order tag is set to ascending or descending, the         */
/*      current order will be changed to correspond to that.            */
/* -------------------------------------------------------------------- */
   pszOrder = msLookupHashTable(layerArgs, "order");
   if(pszOrder && ((strcasecmp(pszOrder, "ASCENDING") == 0) ||
                    (strcasecmp(pszOrder, "DESCENDING") == 0)))
   {
       if(sortLayerByOrder(mapserv->map, pszOrder) != MS_SUCCESS)
         goto error;
   }
   
   if(legGroupHtml) {
      /* retrieve group names */
      papszGroups = msGetAllGroupNames(mapserv->map, &nGroupNames);

      for (i=0; i<nGroupNames; i++) {
         /* process group tags */
         if(generateGroupTemplate(legGroupHtml, mapserv->map, papszGroups[i], groupArgs, &legGroupHtmlCopy, pszPrefix) != MS_SUCCESS)
         {
            if(pszResult)
              free(pszResult);
            pszResult=NULL;
            goto error;
         }
            
         /* concatenate it to final result */
         pszResult = msStringConcatenate(pszResult, legGroupHtmlCopy);

/*         
         if(!pszResult)
         {
            if(pszResult)
              free(pszResult);
            pszResult=NULL;
            goto error;
         }
*/
         
         if(legGroupHtmlCopy)
         {
           free(legGroupHtmlCopy);
           legGroupHtmlCopy = NULL;
         }
         
         /* for all layers in group */
         if(legLayerHtml) {
           for (j=0; j<mapserv->map->numlayers; j++) {
              /*
               * if order_metadata is set and the order
               * value is less than 0, dont display it
               */
              pszOrderMetadata = msLookupHashTable(layerArgs, "order_metadata");
              if(pszOrderMetadata) {
                 pszOrderValue = msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->metadata), pszOrderMetadata);
                 if(pszOrderValue) {
                    nLegendOrder = atoi(pszOrderValue);
                    if(nLegendOrder < 0)
                      continue;
                 }
              }

              if(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->group && strcmp(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->group, papszGroups[i]) == 0) {
                 /* process all layer tags */
                 if(generateLayerTemplate(legLayerHtml, mapserv->map, mapserv->map->layerorder[j], layerArgs, &legLayerHtmlCopy, pszPrefix) != MS_SUCCESS)
                 {
                    if(pszResult)
                      free(pszResult);
                    pszResult=NULL;
                    goto error;
                 }
              
                  
                 /* concatenate to final result */
                 pszResult = msStringConcatenate(pszResult, legLayerHtmlCopy);

                 if(legLayerHtmlCopy)
                 {
                    free(legLayerHtmlCopy);
                    legLayerHtmlCopy = NULL;
                 }
                 
            
                 /* for all classes in layer */
                 if(legClassHtml) {
                    for (k=0; k<GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->numclasses; k++) {
                       /* process all class tags */
                       if(!GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->class[k]->name)
                         continue;

                       if(generateClassTemplate(legClassHtml, mapserv->map, mapserv->map->layerorder[j], k, classArgs, &legClassHtmlCopy, pszPrefix) != MS_SUCCESS)
                       {
                          if(pszResult)
                            free(pszResult);
                          pszResult=NULL;
                          goto error;
                       }
                 
               
                       /* concatenate to final result */
                       pszResult = msStringConcatenate(pszResult, legClassHtmlCopy);

                       if(legClassHtmlCopy) {
                         free(legClassHtmlCopy);
                         legClassHtmlCopy = NULL;
                       }
                    }
                 }
              }
           }
         }
         else
         if(legClassHtml){ /* no layer template specified but class and group template */
           for (j=0; j<mapserv->map->numlayers; j++) {
              /*
               * if order_metadata is set and the order
               * value is less than 0, dont display it
               */
              pszOrderMetadata = msLookupHashTable(layerArgs, "order_metadata");
              if(pszOrderMetadata) {
                 pszOrderValue = msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->metadata), pszOrderMetadata);
                 if(pszOrderValue) {
                    nLegendOrder = atoi(pszOrderValue);
                    if(nLegendOrder < 0)
                      continue;
                 }
              }

              if(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->group && strcmp(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->group, papszGroups[i]) == 0) {
                 /* for all classes in layer */
                 if(legClassHtml) {
                    for (k=0; k<GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->numclasses; k++) {
                       /* process all class tags */
                       if(!GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->class[k]->name)
                         continue;

                       if(generateClassTemplate(legClassHtml, mapserv->map, mapserv->map->layerorder[j], k, classArgs, &legClassHtmlCopy, pszPrefix) != MS_SUCCESS)
                       {
                          if(pszResult)
                            free(pszResult);
                          pszResult=NULL;
                          goto error;
                       }
                 
               
                       /* concatenate to final result */
                       pszResult = msStringConcatenate(pszResult, legClassHtmlCopy);

                       if(legClassHtmlCopy) {
                         free(legClassHtmlCopy);
                         legClassHtmlCopy = NULL;
                       }
                    }
                 }
              }
           }
         }
      }
   }
   else {
      /* if no group template specified */
      if(legLayerHtml) {
         for (j=0; j<mapserv->map->numlayers; j++) {
            /*
             * if order_metadata is set and the order
             * value is less than 0, dont display it
             */
            pszOrderMetadata = msLookupHashTable(layerArgs, "order_metadata");
            if(pszOrderMetadata) {
               pszOrderValue = msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->metadata), pszOrderMetadata);
               if(pszOrderValue) {
                  nLegendOrder = atoi(pszOrderValue);
                  if(nLegendOrder < 0)
                    continue;
               }
               else
                  nLegendOrder=0;
            }

            /* process a layer tags */
            if(generateLayerTemplate(legLayerHtml, mapserv->map, mapserv->map->layerorder[j], layerArgs, &legLayerHtmlCopy, pszPrefix) != MS_SUCCESS)
            {
               if(pszResult)
                 free(pszResult);
               pszResult=NULL;
               goto error;
            }
              
            /* concatenate to final result */
            pszResult = msStringConcatenate(pszResult, legLayerHtmlCopy);

            if(legLayerHtmlCopy) {
               free(legLayerHtmlCopy);
               legLayerHtmlCopy = NULL;
            }
            
            /* for all classes in layer */
            if(legClassHtml) {
               for (k=0; k<GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->numclasses; k++) {
                  /* process all class tags */
                  if(!GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->class[k]->name)
                    continue;

                  if(generateClassTemplate(legClassHtml, mapserv->map, mapserv->map->layerorder[j], k, classArgs, &legClassHtmlCopy, pszPrefix) != MS_SUCCESS)
                  {
                     if(pszResult)
                       free(pszResult);
                     pszResult=NULL;
                     goto error;
                  }
          
               
                  /* concatenate to final result */
                  pszResult = msStringConcatenate(pszResult, legClassHtmlCopy);
  
                  if(legClassHtmlCopy) {
                    free(legClassHtmlCopy);
                    legClassHtmlCopy = NULL;
                  }
               }
            }         
         }
      }
      else { /* if no group and layer template specified */
         if(legClassHtml) {
            for (j=0; j<mapserv->map->numlayers; j++) {
               /*
                * if order_metadata is set and the order
                * value is less than 0, dont display it
                */
               pszOrderMetadata = msLookupHashTable(layerArgs, "order_metadata");
               if(pszOrderMetadata) {
                  pszOrderValue = msLookupHashTable(&(GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->metadata), pszOrderMetadata);
                  if(pszOrderValue) {
                     nLegendOrder = atoi(pszOrderValue);
                     if(nLegendOrder < 0)
                       continue;
                  }
               }

               for (k=0; k<GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->numclasses; k++) {
                  if(!GET_LAYER(mapserv->map, mapserv->map->layerorder[j])->class[k]->name)
                    continue;
                  
                  if(generateClassTemplate(legClassHtml, mapserv->map, mapserv->map->layerorder[j], k, classArgs, &legClassHtmlCopy, pszPrefix) != MS_SUCCESS)
                  {
                     if(pszResult)
                       free(pszResult);
                     pszResult=NULL;
                     goto error;
                  }
      
               
                  pszResult = msStringConcatenate(pszResult, legClassHtmlCopy);

                  if(legClassHtmlCopy) {
                    free(legClassHtmlCopy);
                    legClassHtmlCopy = NULL;
                  }
               }
            }
         }
      }
   }
   
   /* finish with the footer if present */
   if(legFooterHtml) pszResult = msStringConcatenate(pszResult, legFooterHtml);

   /*
    * if we reach this point, that mean no error was generated.
    * So check if template is null and initialize it to <space>.
    */
   if(pszResult == NULL)
   {
      pszResult = msStringConcatenate(pszResult, " ");
   }
   
   
   /********************************************************************/
      
   error:
      
   if(papszGroups) {
      for (i=0; i<nGroupNames; i++)
        msFree(papszGroups[i]);

      msFree(papszGroups);
   }
   
   msFreeHashTable(groupArgs);
   msFreeHashTable(layerArgs);
   msFreeHashTable(classArgs);
   
   msFree(file);
     
   msFree(legGroupHtmlCopy);
   msFree(legLayerHtmlCopy);
   msFree(legClassHtmlCopy);
      
   msFree(legHeaderHtml);
   msFree(legFooterHtml);

   msFree(legGroupHtml);
   msFree(legLayerHtml);
   msFree(legClassHtml);
   msFree(pszPrefix);

   fclose(stream);

/* -------------------------------------------------------------------- */
/*      Reset the layerdrawing order.                                   */
/* -------------------------------------------------------------------- */
   if(panCurrentDrawingOrder && mapserv->map->layerorder)
   {
       for (i=0; i<mapserv->map->numlayers; i++)
          mapserv->map->layerorder[i] =  panCurrentDrawingOrder[i];

       free(panCurrentDrawingOrder);
   }
   
   return pszResult;
}

char *processOneToManyJoin(mapservObj* mapserv, joinObj *join)
{
  int records=MS_FALSE;
  FILE *stream=NULL;
  char *outbuf; 
  char line[MS_BUFFER_LENGTH], *tmpline;
  char szPath[MS_MAXPATHLEN];

  if((outbuf = strdup("")) == NULL) return(NULL); /* empty at first */

  msJoinPrepare(join, &(mapserv->resultshape)); /* execute the join */
  while(msJoinNext(join) == MS_SUCCESS) {
    /* First time through, deal with the header (if necessary) and open the main template. We only */
    /* want to do this if there are joined records. */
    if(records == MS_FALSE) { 
      if(join->header != NULL) {
        if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, join->header), "r")) == NULL) {
          msSetError(MS_IOERR, "Error while opening join header file %s.", "processOneToManyJoin()", join->header);
          return(NULL);
        }

        /* echo file to the output buffer, no substitutions */
        while(fgets(line, MS_BUFFER_LENGTH, stream) != NULL) outbuf = msStringConcatenate(outbuf, line);

        fclose(stream);
      }

      if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, join->template), "r")) == NULL) {
        msSetError(MS_IOERR, "Error while opening join template file %s.", "processOneToManyJoin()", join->template);
        return(NULL);
      }      
      
      records = MS_TRUE;
    }
    
    while(fgets(line, MS_BUFFER_LENGTH, stream) != NULL) { /* now on to the end of the template */
      if(strchr(line, '[') != NULL) {
        tmpline = processLine(mapserv, line, NULL, QUERY); /* no multiline tags are allowed in a join */
        if(!tmpline) return NULL;
        outbuf = msStringConcatenate(outbuf, tmpline);
        free(tmpline);
      } else /* no subs, just echo */
        outbuf = msStringConcatenate(outbuf, line);
    }
      
    rewind(stream);
  } /* next record */

  if(records==MS_TRUE && join->footer) {    
    if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, join->footer), "r")) == NULL) {
      msSetError(MS_IOERR, "Error while opening join footer file %s.", "processOneToManyJoin()", join->footer);
      return(NULL);
    }

    /* echo file to the output buffer, no substitutions */
    while(fgets(line, MS_BUFFER_LENGTH, stream) != NULL) outbuf = msStringConcatenate(outbuf, line);
    
    fclose(stream);
  }

  /* clear any data associated with the join */
  msFreeCharArray(join->values, join->numitems);
  join->values = NULL;

  return(outbuf);
}

/*
** Process a single line in the template. A few tags (e.g. [resultset]...[/resultset]) can be multi-line so
** we pass the filehandle to look ahead if necessary.
*/
static char *processLine(mapservObj *mapserv, char *instr, FILE *stream, int mode)
{
  int i, j;
#define PROCESSLINE_BUFLEN 5120
  char repstr[PROCESSLINE_BUFLEN], substr[PROCESSLINE_BUFLEN], *outstr; /* repstr = replace string, substr = sub string */
  struct hashObj *tp=NULL;
  char *encodedstr;
   
#ifdef USE_PROJ
  rectObj llextent;
  pointObj llpoint;
#endif

  outstr = strdup(instr); /* work from a copy */

  if(strstr(outstr, "[version]")) outstr = msReplaceSubstring(outstr, "[version]",  msGetVersion());

  snprintf(repstr, PROCESSLINE_BUFLEN, "%s%s%s.%s", mapserv->map->web.imageurl, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
  outstr = msReplaceSubstring(outstr, "[img]", repstr);
  snprintf(repstr, PROCESSLINE_BUFLEN, "%s%sref%s.%s", mapserv->map->web.imageurl, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
  outstr = msReplaceSubstring(outstr, "[ref]", repstr);

  if(strstr(outstr, "[errmsg")) {
    char *errmsg = msGetErrorString(";");
    if(!errmsg) errmsg = strdup("Error message buffer is empty."); /* should never happen, but just in case... */
    outstr = msReplaceSubstring(outstr, "[errmsg]", errmsg);
    encodedstr = msEncodeUrl(errmsg);
    outstr = msReplaceSubstring(outstr, "[errmsg_esc]", encodedstr);
    free(errmsg);
    free(encodedstr);
  }
  
  if(strstr(outstr, "[legend]")) {
     /* if there's a template legend specified, use it */
     if(mapserv->map->legend.template) {
        char *legendTemplate;

        legendTemplate = generateLegendTemplate(mapserv);
        if(legendTemplate) {
          outstr = msReplaceSubstring(outstr, "[legend]", legendTemplate);
     
           free(legendTemplate);
        }
        else /* error already generated by (generateLegendTemplate()) */
          return NULL;
     }
     else { /* if not display gif image with all legend icon */
        snprintf(repstr, PROCESSLINE_BUFLEN, "%s%sleg%s.%s", mapserv->map->web.imageurl, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
        outstr = msReplaceSubstring(outstr, "[legend]", repstr);
     }
  }
   
  snprintf(repstr, PROCESSLINE_BUFLEN, "%s%ssb%s.%s", mapserv->map->web.imageurl, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
  outstr = msReplaceSubstring(outstr, "[scalebar]", repstr);

  if(mapserv->savequery) {
    snprintf(repstr, PROCESSLINE_BUFLEN, "%s%s%s%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_QUERY_EXTENSION);
    outstr = msReplaceSubstring(outstr, "[queryfile]", repstr);
  }
  
  if(mapserv->savemap) {
    snprintf(repstr, PROCESSLINE_BUFLEN, "%s%s%s.map", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id);
    outstr = msReplaceSubstring(outstr, "[map]", repstr);
  }

  if(getenv("HTTP_HOST")) {
    snprintf(repstr, PROCESSLINE_BUFLEN, "%s", getenv("HTTP_HOST")); 
    outstr = msReplaceSubstring(outstr, "[host]", repstr);
  }
  if(getenv("SERVER_PORT")) { 
    snprintf(repstr, PROCESSLINE_BUFLEN, "%s", getenv("SERVER_PORT"));
    outstr = msReplaceSubstring(outstr, "[port]", repstr);
  }
  
  snprintf(repstr, PROCESSLINE_BUFLEN, "%s", mapserv->Id);
  outstr = msReplaceSubstring(outstr, "[id]", repstr);
  
  repstr[0] = '\0'; /* Layer list for a "POST" request */
  for(i=0;i<mapserv->NumLayers;i++) {    
    strlcat(repstr, mapserv->Layers[i], sizeof(repstr));
    strlcat(repstr, " ", sizeof(repstr));
  }
  msStringTrimBlanks(repstr);
  outstr = msReplaceSubstring(outstr, "[layers]", repstr);

  encodedstr = msEncodeUrl(repstr);
  outstr = msReplaceSubstring(outstr, "[layers_esc]", encodedstr);
  free(encodedstr);

  strcpy(repstr, ""); /* list of ALL layers that can be toggled */
  repstr[0] = '\0';
  for(i=0;i<mapserv->map->numlayers;i++) {
    if(GET_LAYER(mapserv->map, i)->status != MS_DEFAULT && GET_LAYER(mapserv->map, i)->name != NULL) {
      strlcat(repstr, GET_LAYER(mapserv->map, i)->name, sizeof(repstr));
      strlcat(repstr, " ", sizeof(repstr));
    }
  }
  msStringTrimBlanks(repstr);
  outstr = msReplaceSubstring(outstr, "[toggle_layers]", repstr);

  encodedstr = msEncodeUrl(repstr);
  outstr = msReplaceSubstring(outstr, "[toggle_layers_esc]", encodedstr);
  free(encodedstr);
  
  for(i=0;i<mapserv->map->numlayers;i++) { /* Set form widgets (i.e. checkboxes, radio and select lists), note that default layers don't show up here */
    if(isOn(mapserv, GET_LAYER(mapserv->map, i)->name, GET_LAYER(mapserv->map, i)->group) == MS_TRUE) {
      if(GET_LAYER(mapserv->map, i)->group) {
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_select]", GET_LAYER(mapserv->map, i)->group);
        outstr = msReplaceSubstring(outstr, substr, "selected=\"selected\"");
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_check]", GET_LAYER(mapserv->map, i)->group);
        outstr = msReplaceSubstring(outstr, substr, "checked=\"checked\"");
      }
      if(GET_LAYER(mapserv->map, i)->name) {
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_select]", GET_LAYER(mapserv->map, i)->name);
        outstr = msReplaceSubstring(outstr, substr, "selected=\"selected\"");
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_check]", GET_LAYER(mapserv->map, i)->name);
        outstr = msReplaceSubstring(outstr, substr, "checked=\"checked\"");
      }
    } else {
      if(GET_LAYER(mapserv->map, i)->group) {
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_select]", GET_LAYER(mapserv->map, i)->group);
        outstr = msReplaceSubstring(outstr, substr, "");
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_check]", GET_LAYER(mapserv->map, i)->group);
        outstr = msReplaceSubstring(outstr, substr, "");
      }
      if(GET_LAYER(mapserv->map, i)->name) {
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_select]", GET_LAYER(mapserv->map, i)->name);
        outstr = msReplaceSubstring(outstr, substr, "");
        snprintf(substr, PROCESSLINE_BUFLEN, "[%s_check]", GET_LAYER(mapserv->map, i)->name);
        outstr = msReplaceSubstring(outstr, substr, "");
      }
    }
  }

  for(i=-1;i<=1;i++) { /* make zoom direction persistant */
    if(mapserv->ZoomDirection == i) {
      sprintf(substr, "[zoomdir_%d_select]", i);
      outstr = msReplaceSubstring(outstr, substr, "selected=\"selected\"");
      sprintf(substr, "[zoomdir_%d_check]", i);
      outstr = msReplaceSubstring(outstr, substr, "checked=\"checked\"");
    } else {
      sprintf(substr, "[zoomdir_%d_select]", i);
      outstr = msReplaceSubstring(outstr, substr, "");
      sprintf(substr, "[zoomdir_%d_check]", i);
      outstr = msReplaceSubstring(outstr, substr, "");
    }
  }
  
  for(i=MINZOOM;i<=MAXZOOM;i++) { /* make zoom persistant */
    if(mapserv->Zoom == i) {
      sprintf(substr, "[zoom_%d_select]", i);
      outstr = msReplaceSubstring(outstr, substr, "selected=\"selected\"");
      sprintf(substr, "[zoom_%d_check]", i);
      outstr = msReplaceSubstring(outstr, substr, "checked=\"checked\"");
    } else {
      sprintf(substr, "[zoom_%d_select]", i);
      outstr = msReplaceSubstring(outstr, substr, "");
      sprintf(substr, "[zoom_%d_check]", i);
      outstr = msReplaceSubstring(outstr, substr, "");
    }
  }

  /* allow web object metadata access in template */
  
  /* 
   * reworked by SG to use HashTable methods
   */
  
  if(&(mapserv->map->web.metadata) && strstr(outstr, "web_")) {
    for (j=0; j<MS_HASHSIZE; j++) {
      if(mapserv->map->web.metadata.items[j] != NULL) {
        for(tp=mapserv->map->web.metadata.items[j]; tp!=NULL; tp=tp->next) {
          snprintf(substr, PROCESSLINE_BUFLEN, "[web_%s]", tp->key);
          outstr = msReplaceSubstring(outstr, substr, tp->data);  
          snprintf(substr, PROCESSLINE_BUFLEN, "[web_%s_esc]", tp->key);

          encodedstr = msEncodeUrl(tp->data);
          outstr = msReplaceSubstring(outstr, substr, encodedstr);
          free(encodedstr);
        }
      }
    }
  }

  /* allow layer metadata access in template */
  for(i=0;i<mapserv->map->numlayers;i++) {
    if(&(GET_LAYER(mapserv->map, i)->metadata) && GET_LAYER(mapserv->map, i)->name && strstr(outstr, GET_LAYER(mapserv->map, i)->name)) {
      for(j=0; j<MS_HASHSIZE; j++) {
        if(GET_LAYER(mapserv->map, i)->metadata.items[j] != NULL) {
          for(tp=GET_LAYER(mapserv->map, i)->metadata.items[j]; tp!=NULL; tp=tp->next) {
            snprintf(substr, PROCESSLINE_BUFLEN, "[%s_%s]", GET_LAYER(mapserv->map, i)->name, tp->key);
            if(GET_LAYER(mapserv->map, i)->status == MS_ON)
              outstr = msReplaceSubstring(outstr, substr, tp->data);
            else
              outstr = msReplaceSubstring(outstr, substr, "");
            snprintf(substr, PROCESSLINE_BUFLEN, "[%s_%s_esc]", GET_LAYER(mapserv->map, i)->name, tp->key);
            if(GET_LAYER(mapserv->map, i)->status == MS_ON) {
              encodedstr = msEncodeUrl(tp->data);
              outstr = msReplaceSubstring(outstr, substr, encodedstr);
              free(encodedstr);
            } else
              outstr = msReplaceSubstring(outstr, substr, "");
          }
        }
      }
    }
  }

  sprintf(repstr, "%f", mapserv->mappnt.x);
  outstr = msReplaceSubstring(outstr, "[mapx]", repstr);
  sprintf(repstr, "%f", mapserv->mappnt.y);
  outstr = msReplaceSubstring(outstr, "[mapy]", repstr);
  
  sprintf(repstr, "%f", mapserv->map->extent.minx); /* Individual mapextent elements for spatial query building, deprecated. */
  outstr = msReplaceSubstring(outstr, "[minx]", repstr);
  sprintf(repstr, "%f", mapserv->map->extent.maxx);
  outstr = msReplaceSubstring(outstr, "[maxx]", repstr);
  sprintf(repstr, "%f", mapserv->map->extent.miny);
  outstr = msReplaceSubstring(outstr, "[miny]", repstr);
  sprintf(repstr, "%f", mapserv->map->extent.maxy);
  outstr = msReplaceSubstring(outstr, "[maxy]", repstr);

  if(processExtentTag(mapserv, &outstr, "mapext", &(mapserv->map->extent)) != MS_SUCCESS)
    return(NULL);
  if(processExtentTag(mapserv, &outstr, "mapext_esc", &(mapserv->map->extent)) != MS_SUCCESS) /* depricated */
    return(NULL);
   
  sprintf(repstr, "%f", (mapserv->map->extent.maxx-mapserv->map->extent.minx)); /* useful for creating cachable extents (i.e. 0 0 dx dy) with legends and scalebars */
  outstr = msReplaceSubstring(outstr, "[dx]", repstr);
  sprintf(repstr, "%f", (mapserv->map->extent.maxy-mapserv->map->extent.miny));
  outstr = msReplaceSubstring(outstr, "[dy]", repstr);

  sprintf(repstr, "%f", mapserv->RawExt.minx); /* Individual raw extent elements for spatial query building, deprecated. */
  outstr = msReplaceSubstring(outstr, "[rawminx]", repstr);
  sprintf(repstr, "%f", mapserv->RawExt.maxx);
  outstr = msReplaceSubstring(outstr, "[rawmaxx]", repstr);
  sprintf(repstr, "%f", mapserv->RawExt.miny);
  outstr = msReplaceSubstring(outstr, "[rawminy]", repstr);
  sprintf(repstr, "%f", mapserv->RawExt.maxy);
  outstr = msReplaceSubstring(outstr, "[rawmaxy]", repstr);

  if(processExtentTag(mapserv, &outstr, "rawext", &(mapserv->RawExt)) != MS_SUCCESS)
    return(NULL);
  if(processExtentTag(mapserv, &outstr, "rawext_esc", &(mapserv->RawExt)) != MS_SUCCESS) /* depricated */
    return(NULL);
  
#ifdef USE_PROJ
  if((strstr(outstr, "lat]") || strstr(outstr, "lon]") || strstr(outstr, "lon_esc]"))
     && mapserv->map->projection.proj != NULL
     && !pj_is_latlong(mapserv->map->projection.proj) ) {
    llextent=mapserv->map->extent;
    llpoint=mapserv->mappnt;
    msProjectRect(&(mapserv->map->projection), &(mapserv->map->latlon), &llextent);
    msProjectPoint(&(mapserv->map->projection), &(mapserv->map->latlon), &llpoint);

    sprintf(repstr, "%f", llpoint.x);
    outstr = msReplaceSubstring(outstr, "[maplon]", repstr);
    sprintf(repstr, "%f", llpoint.y);
    outstr = msReplaceSubstring(outstr, "[maplat]", repstr);
    
    sprintf(repstr, "%f", llextent.minx); /* map extent as lat/lon */
    outstr = msReplaceSubstring(outstr, "[minlon]", repstr);
    sprintf(repstr, "%f", llextent.maxx);
    outstr = msReplaceSubstring(outstr, "[maxlon]", repstr);
    sprintf(repstr, "%f", llextent.miny);
    outstr = msReplaceSubstring(outstr, "[minlat]", repstr);
    sprintf(repstr, "%f", llextent.maxy);
    outstr = msReplaceSubstring(outstr, "[maxlat]", repstr);    

    if(processExtentTag(mapserv, &outstr, "mapext_latlon", &(llextent)) != MS_SUCCESS) 
      return(NULL);
    if(processExtentTag(mapserv, &outstr, "mapext_latlon_esc", &(llextent)) != MS_SUCCESS) /* depricated */
      return(NULL);
  }
#endif

  /* submitted by J.F (bug 1102) */
  if(mapserv->map->reference.status == MS_ON) {
    sprintf(repstr, "%f", mapserv->map->reference.extent.minx); /* Individual reference map extent elements for spatial query building, depricated. */
    outstr = msReplaceSubstring(outstr, "[refminx]", repstr);
    sprintf(repstr, "%f", mapserv->map->reference.extent.maxx);
    outstr = msReplaceSubstring(outstr, "[refmaxx]", repstr);
    sprintf(repstr, "%f", mapserv->map->reference.extent.miny);
    outstr = msReplaceSubstring(outstr, "[refminy]", repstr);
    sprintf(repstr, "%f", mapserv->map->reference.extent.maxy);
    outstr = msReplaceSubstring(outstr, "[refmaxy]", repstr);

    if(processExtentTag(mapserv, &outstr, "refext", &(mapserv->map->reference.extent)) != MS_SUCCESS)
      return(NULL);
    if(processExtentTag(mapserv, &outstr, "refext_esc", &(mapserv->map->reference.extent)) != MS_SUCCESS) /* depricated */
      return(NULL);
  }

  sprintf(repstr, "%d %d", mapserv->map->width, mapserv->map->height);
  outstr = msReplaceSubstring(outstr, "[mapsize]", repstr);
   
  encodedstr = msEncodeUrl(repstr);
  outstr = msReplaceSubstring(outstr, "[mapsize_esc]", encodedstr);
  free(encodedstr);

  sprintf(repstr, "%d", mapserv->map->width);
  outstr = msReplaceSubstring(outstr, "[mapwidth]", repstr);
  sprintf(repstr, "%d", mapserv->map->height);
  outstr = msReplaceSubstring(outstr, "[mapheight]", repstr);
  
  sprintf(repstr, "%f", mapserv->map->scaledenom);
  outstr = msReplaceSubstring(outstr, "[scale]", repstr);
  outstr = msReplaceSubstring(outstr, "[scaledenom]", repstr);
  sprintf(repstr, "%f", mapserv->map->cellsize);
  outstr = msReplaceSubstring(outstr, "[cellsize]", repstr);
  
  sprintf(repstr, "%.1f %.1f", (mapserv->map->width)/2.0, (mapserv->map->height)/2.0); /* not subtracting 1 from image dimensions (see bug 633) */
  outstr = msReplaceSubstring(outstr, "[center]", repstr);
  sprintf(repstr, "%.1f", (mapserv->map->width)/2.0);
  outstr = msReplaceSubstring(outstr, "[center_x]", repstr);
  sprintf(repstr, "%.1f", (mapserv->map->height)/2.0);
  outstr = msReplaceSubstring(outstr, "[center_y]", repstr);      

  /* These are really for situations with multiple result sets only, but often used in header/footer   */
  sprintf(repstr, "%d", mapserv->NR); /* total number of results */
  outstr = msReplaceSubstring(outstr, "[nr]", repstr);  
  sprintf(repstr, "%d", mapserv->NL); /* total number of layers with results */
  outstr = msReplaceSubstring(outstr, "[nl]", repstr);

  if(mapserv->resultlayer) {    
    if(strstr(outstr, "[items]") != NULL) {
      char *itemstr=NULL;

      itemstr = msJoinStrings(mapserv->resultlayer->items, mapserv->resultlayer->numitems, ",");
      outstr = msReplaceSubstring(outstr, "[items]", itemstr);
      free(itemstr);
    }

    sprintf(repstr, "%d", mapserv->NLR); /* total number of results within this layer */
    outstr = msReplaceSubstring(outstr, "[nlr]", repstr);
    sprintf(repstr, "%d", mapserv->RN); /* sequential (eg. 1..n) result number within all layers */
    outstr = msReplaceSubstring(outstr, "[rn]", repstr);
    sprintf(repstr, "%d", mapserv->LRN); /* sequential (eg. 1..n) result number within this layer */
    outstr = msReplaceSubstring(outstr, "[lrn]", repstr);
    outstr = msReplaceSubstring(outstr, "[cl]", mapserv->resultlayer->name); /* current layer name     */
    /* if(resultlayer->description) outstr = msReplaceSubstring(outstr, "[cd]", resultlayer->description); // current layer description     */
  }

  if(mode != QUERY) {
    if(processResultSetTag(mapserv, &outstr, stream) != MS_SUCCESS) return(NULL);
  }

  if(mode == QUERY) { /* return shape and/or values  */

    /* allow layer metadata access in a query template, within the context of a query no layer name is necessary */
    if(&(mapserv->resultlayer->metadata) && strstr(outstr, "[metadata_")) {
      for(i=0; i<MS_HASHSIZE; i++) {
        if(mapserv->resultlayer->metadata.items[i] != NULL) {
          for(tp=mapserv->resultlayer->metadata.items[i]; tp!=NULL; tp=tp->next) {
            snprintf(substr, PROCESSLINE_BUFLEN, "[metadata_%s]", tp->key);
            outstr = msReplaceSubstring(outstr, substr, tp->data);
     
            snprintf(substr, PROCESSLINE_BUFLEN, "[metadata_%s_esc]", tp->key);
            encodedstr = msEncodeUrl(tp->data);
            outstr = msReplaceSubstring(outstr, substr, encodedstr);
            free(encodedstr);
          }
        }
      }
    }
    
    sprintf(repstr, "%f %f", (mapserv->resultshape.bounds.maxx + mapserv->resultshape.bounds.minx)/2, (mapserv->resultshape.bounds.maxy + mapserv->resultshape.bounds.miny)/2); 
    outstr = msReplaceSubstring(outstr, "[shpmid]", repstr);
    sprintf(repstr, "%f", (mapserv->resultshape.bounds.maxx + mapserv->resultshape.bounds.minx)/2);
    outstr = msReplaceSubstring(outstr, "[shpmidx]", repstr);
    sprintf(repstr, "%f", (mapserv->resultshape.bounds.maxy + mapserv->resultshape.bounds.miny)/2);
    outstr = msReplaceSubstring(outstr, "[shpmidy]", repstr);
    
    if(processExtentTag(mapserv, &outstr, "shpext", &(mapserv->resultshape.bounds)) != MS_SUCCESS)
      return(NULL);
    if(processExtentTag(mapserv, &outstr, "shpext_esc", &(mapserv->resultshape.bounds)) != MS_SUCCESS) /* depricated */
      return(NULL);

    sprintf(repstr, "%d", mapserv->resultshape.classindex);
    outstr = msReplaceSubstring(outstr, "[shpclass]", repstr);

    if(processShpxyTag(mapserv->resultlayer, &outstr, &mapserv->resultshape) != MS_SUCCESS)
      return(NULL);

    sprintf(repstr, "%f", mapserv->resultshape.bounds.minx);
    outstr = msReplaceSubstring(outstr, "[shpminx]", repstr);
    sprintf(repstr, "%f", mapserv->resultshape.bounds.miny);
    outstr = msReplaceSubstring(outstr, "[shpminy]", repstr);
    sprintf(repstr, "%f", mapserv->resultshape.bounds.maxx);
    outstr = msReplaceSubstring(outstr, "[shpmaxx]", repstr);
    sprintf(repstr, "%f", mapserv->resultshape.bounds.maxy);
    outstr = msReplaceSubstring(outstr, "[shpmaxy]", repstr);
    
    sprintf(repstr, "%ld", mapserv->resultshape.index);
    outstr = msReplaceSubstring(outstr, "[shpidx]", repstr);
    sprintf(repstr, "%d", mapserv->resultshape.tileindex);
    outstr = msReplaceSubstring(outstr, "[tileidx]", repstr);  

    /* return ALL attributes in one delimeted list */
    if(strstr(outstr, "[values]") != NULL) {
      char *valuestr=NULL;

      valuestr = msJoinStrings(mapserv->resultshape.values, mapserv->resultlayer->numitems, ",");
      outstr = msReplaceSubstring(outstr, "[values]", valuestr);
      free(valuestr);
    }

    for(i=0;i<mapserv->resultlayer->numitems;i++) {
      /* by default let's encode attributes for HTML presentation */
      snprintf(substr, PROCESSLINE_BUFLEN, "[%s]", mapserv->resultlayer->items[i]);
      if(strstr(outstr, substr) != NULL) {
        encodedstr = msEncodeHTMLEntities(mapserv->resultshape.values[i]);
        outstr = msReplaceSubstring(outstr, substr, encodedstr);
        free(encodedstr);
      }

      /* of course you might want to embed that data in URLs */
      snprintf(substr, PROCESSLINE_BUFLEN, "[%s_esc]", mapserv->resultlayer->items[i]);
      if(strstr(outstr, substr) != NULL) {
        encodedstr = msEncodeUrl(mapserv->resultshape.values[i]);
        outstr = msReplaceSubstring(outstr, substr, encodedstr);
        free(encodedstr);
      }

      /* or you might want to access the attributes unaltered */
      snprintf(substr, PROCESSLINE_BUFLEN, "[%s_raw]", mapserv->resultlayer->items[i]);
      if(strstr(outstr, substr) != NULL)
        outstr = msReplaceSubstring(outstr, substr, mapserv->resultshape.values[i]);
    }
    
    if(processItemTag(mapserv->resultlayer, &outstr, &mapserv->resultshape) != MS_SUCCESS)
      return(NULL);

    /* handle joins in this next section */
    for(i=0; i<mapserv->resultlayer->numjoins; i++) {
      if(mapserv->resultlayer->joins[i].values) { /* join has data */
        for(j=0;j<mapserv->resultlayer->joins[i].numitems;j++) {
          /* by default let's encode attributes for HTML presentation */
          snprintf(substr, PROCESSLINE_BUFLEN, "[%s_%s]", mapserv->resultlayer->joins[i].name, mapserv->resultlayer->joins[i].items[j]);        
          if(strstr(outstr, substr) != NULL) {
            encodedstr = msEncodeHTMLEntities(mapserv->resultlayer->joins[i].values[j]);
            outstr = msReplaceSubstring(outstr, substr, encodedstr);
            free(encodedstr);
          }

          /* of course you might want to embed that data in URLs */
          snprintf(substr, PROCESSLINE_BUFLEN, "[%s_%s_esc]", mapserv->resultlayer->joins[i].name, mapserv->resultlayer->joins[i].items[j]);
          if(strstr(outstr, substr) != NULL) {
            encodedstr = msEncodeUrl(mapserv->resultlayer->joins[i].values[j]);
            outstr = msReplaceSubstring(outstr, substr, encodedstr);
            free(encodedstr);
          }

          /* or you might want to access the attributes unaltered */
          snprintf(substr, PROCESSLINE_BUFLEN, "[%s_%s_raw]", mapserv->resultlayer->joins[i].name, mapserv->resultlayer->joins[i].items[j]);
          if(strstr(outstr, substr) != NULL)
            outstr = msReplaceSubstring(outstr, substr, mapserv->resultlayer->joins[i].values[j]);
        }
      } else if(mapserv->resultlayer->joins[i].type ==  MS_JOIN_ONE_TO_MANY){ /* one-to-many join */
        char *joinTemplate=NULL;

        snprintf(substr, PROCESSLINE_BUFLEN, "[join_%s]", mapserv->resultlayer->joins[i].name);        
        if(strstr(outstr, substr) != NULL) {
          joinTemplate = processOneToManyJoin(mapserv, &(mapserv->resultlayer->joins[i]));
          if(joinTemplate) {
            outstr = msReplaceSubstring(outstr, substr, joinTemplate);     
            free(joinTemplate);
          } else
            return NULL;
        }
      }
    } /* next join */

  } /* end query mode specific substitutions */

  if(processIncludeTag(mapserv, &outstr, stream, mode) != MS_SUCCESS)
    return(NULL);

  for(i=0;i<mapserv->request->NumParams;i++) {
    /* Replace [variable] tags using values from URL. We cannot offer a
     * [variable_raw] option here due to the risk of XSS
     */
    snprintf(substr, PROCESSLINE_BUFLEN, "[%s]", mapserv->request->ParamNames[i]);
    encodedstr = msEncodeHTMLEntities(mapserv->request->ParamValues[i]);
    outstr = msReplaceSubstring(outstr, substr, encodedstr);
    free(encodedstr);

    snprintf(substr, PROCESSLINE_BUFLEN, "[%s_esc]", mapserv->request->ParamNames[i]);
    encodedstr = msEncodeUrl(mapserv->request->ParamValues[i]);
    outstr = msReplaceSubstring(outstr, substr, encodedstr);
    free(encodedstr);
  }

  return(outstr);
}

#define MS_TEMPLATE_BUFFER 1024 /* 1k */

int msReturnPage(mapservObj *mapserv, char *html, int mode, char **papszBuffer)
{
  FILE *stream;
  char line[MS_BUFFER_LENGTH], *tmpline;
  int   nBufferSize = 0;
  int   nCurrentSize = 0;
  int   nExpandBuffer = 0;

  ms_regex_t re; /* compiled regular expression to be matched */ 
  char szPath[MS_MAXPATHLEN];

  if(ms_regcomp(&re, MS_TEMPLATE_EXPR, MS_REG_EXTENDED|MS_REG_NOSUB) != 0) {
    msSetError(MS_REGEXERR, NULL, "msReturnPage()");
    return MS_FAILURE;
  }

  if(ms_regexec(&re, html, 0, NULL, 0) != 0) { /* no match */
    ms_regfree(&re);
    msSetError(MS_WEBERR, "Malformed template name (%s).", "msReturnPage()", html);
    return MS_FAILURE;
  }
  ms_regfree(&re);

  if((stream = fopen(msBuildPath(szPath, mapserv->map->mappath, html), "r")) == NULL) {
    msSetError(MS_IOERR, html, "msReturnPage()");
    return MS_FAILURE;
  } 

  if(papszBuffer) {
    if((*papszBuffer) == NULL) {
      (*papszBuffer) = (char *)malloc(MS_TEMPLATE_BUFFER);
      (*papszBuffer)[0] = '\0';
      nBufferSize = MS_TEMPLATE_BUFFER;
      nCurrentSize = 0;
      nExpandBuffer = 1;
    } else {
      nCurrentSize = strlen((*papszBuffer));
      nBufferSize = nCurrentSize;
      nExpandBuffer = (nCurrentSize/MS_TEMPLATE_BUFFER) + 1;
    }
  }

  while(fgets(line, MS_BUFFER_LENGTH, stream) != NULL) { /* now on to the end of the file */

    if(strchr(line, '[') != NULL) {
      tmpline = processLine(mapserv, line, stream, mode);
      if(!tmpline)
        return MS_FAILURE;

      if(papszBuffer) {
        if(nBufferSize <= (int)(nCurrentSize + strlen(tmpline) + 1)) {
          nExpandBuffer = (strlen(tmpline) /  MS_TEMPLATE_BUFFER) + 1;
          nBufferSize = MS_TEMPLATE_BUFFER*nExpandBuffer + strlen((*papszBuffer));
          (*papszBuffer) = (char *) realloc((*papszBuffer),sizeof(char)*nBufferSize);
        }
        strcat((*papszBuffer), tmpline);
        nCurrentSize += strlen(tmpline);   
      } else
        msIO_fwrite(tmpline, strlen(tmpline), 1, stdout);

      free(tmpline);
    } else {
      if(papszBuffer) {
        if(nBufferSize <= (int)(nCurrentSize + strlen(line))) {
          nExpandBuffer = (strlen(line) /  MS_TEMPLATE_BUFFER) + 1;
          nBufferSize = MS_TEMPLATE_BUFFER*nExpandBuffer + strlen((*papszBuffer));
          (*papszBuffer) = (char *)realloc((*papszBuffer),sizeof(char)*nBufferSize);
        }
        strcat((*papszBuffer), line);
        nCurrentSize += strlen(line);
      } else 
        msIO_fwrite(line, strlen(line), 1, stdout);
    }
    if(!papszBuffer)
      fflush(stdout);
  } /* next line */

  fclose(stream);

  return MS_SUCCESS;
}

int msReturnURL(mapservObj* ms, char* url, int mode)
{
  char *tmpurl;

  if(url == NULL) {
    msSetError(MS_WEBERR, "Empty URL.", "msReturnURL()");
    return MS_FAILURE;
  }

  tmpurl = processLine(ms, url, NULL, mode); /* URL templates can't handle multi-line tags, hence the NULL file pointer */

  if(!tmpurl)
    return MS_FAILURE;

  msRedirect(tmpurl);
  free(tmpurl);

  return MS_SUCCESS;
}

/*
** Legacy query template parsing where you use headers, footers and such...
*/
int msReturnNestedTemplateQuery(mapservObj* mapserv, char* pszMimeType, char **papszBuffer)
{
  int status;
  int i,j,k;
  char buffer[1024];
  int nBufferSize =0;
  int nCurrentSize = 0;
  int nExpandBuffer = 0;

  char *template;

  layerObj *lp=NULL;

  if(papszBuffer) {
    (*papszBuffer) = (char *)malloc(MS_TEMPLATE_BUFFER);
    (*papszBuffer)[0] = '\0';
    nBufferSize = MS_TEMPLATE_BUFFER;
    nCurrentSize = 0;
    nExpandBuffer = 1;
  }
  
  msInitShape(&(mapserv->resultshape));

  if((mapserv->Mode == ITEMQUERY) || (mapserv->Mode == QUERY)) { /* may need to handle a URL result set since these modes return exactly 1 result */

    for(i=(mapserv->map->numlayers-1); i>=0; i--) {
      lp = (GET_LAYER(mapserv->map, i));

      if(!lp->resultcache) continue;
      if(lp->resultcache->numresults > 0) break;
    }

    if(i >= 0) { /* at least if no result found, mapserver will display an empty template. */

      if(lp->resultcache->results[0].classindex >= 0 && lp->class[(int)(lp->resultcache->results[0].classindex)]->template) 
        template = lp->class[(int)(lp->resultcache->results[0].classindex)]->template;
      else 
        template = lp->template;

      if( template == NULL ) {
        msSetError(MS_WEBERR, "No template for layer %s or it's classes.", "msReturnNestedTemplateQuery()", lp->name );
        return MS_FAILURE;
      }

      if(TEMPLATE_TYPE(template) == MS_URL) {
        mapserv->resultlayer = lp;

        status = msLayerOpen(lp);
        if(status != MS_SUCCESS) return status;
        
        status = msLayerGetItems(lp); /* retrieve all the item names */
        if(status != MS_SUCCESS) return status;

        status = msLayerGetShape(lp, &(mapserv->resultshape), lp->resultcache->results[0].tileindex, lp->resultcache->results[0].shapeindex);
        if(status != MS_SUCCESS) return status;

        if(lp->numjoins > 0) {
          for(k=0; k<lp->numjoins; k++) { 
            status = msJoinConnect(lp, &(lp->joins[k]));
            if(status != MS_SUCCESS) return status;  

            msJoinPrepare(&(lp->joins[k]), &(mapserv->resultshape));
            msJoinNext(&(lp->joins[k])); /* fetch the first row */
          }
        }

        if(papszBuffer == NULL) {
          if(msReturnURL(mapserv, template, QUERY) != MS_SUCCESS) return MS_FAILURE;
        }

        msFreeShape(&(mapserv->resultshape));
        msLayerClose(lp);
        mapserv->resultlayer = NULL;
          
        return MS_SUCCESS;
      }
    }
  }

  /*
  ** Now we know we're making a template sandwich
  */
  mapserv->NR = mapserv->NL = 0;
  for(i=0; i<mapserv->map->numlayers; i++) { /* compute some totals */
    lp = (GET_LAYER(mapserv->map, i));

    if(!lp->resultcache) continue;

    if(lp->resultcache->numresults > 0) { 
      mapserv->NL++;
      mapserv->NR += lp->resultcache->numresults;
    }
  }

  /*
  ** Is this step really necessary for buffered output? Legend and browse templates don't deal with mime-types
  ** so why should this. Note that new-style templates don't buffer the mime-type either.
  */
  if(papszBuffer && mapserv->sendheaders) {
    sprintf(buffer, "Content-type: %s%c%c", pszMimeType, 10, 10);
    if(nBufferSize <= (int)(nCurrentSize + strlen(buffer) + 1)) {
      nExpandBuffer++;
      (*papszBuffer) = (char *)realloc((*papszBuffer), MS_TEMPLATE_BUFFER*nExpandBuffer);
      nBufferSize = MS_TEMPLATE_BUFFER*nExpandBuffer;
    }
    strcat((*papszBuffer), buffer);
    nCurrentSize += strlen(buffer);
  } else if(mapserv->sendheaders) {
    msIO_printf("Content-type: %s%c%c", pszMimeType, 10, 10);
    fflush(stdout);
  }

  if(mapserv->map->web.header) {
    if(msReturnPage(mapserv, mapserv->map->web.header, BROWSE, papszBuffer) != MS_SUCCESS) return MS_FAILURE;
  }

  mapserv->RN = 1; /* overall result number */
  for(i=(mapserv->map->numlayers-1); i>=0; i--) {
    mapserv->resultlayer = lp = (GET_LAYER(mapserv->map, i));

    if(!lp->resultcache) continue;
    if(lp->resultcache->numresults <= 0) continue;

    mapserv->NLR = lp->resultcache->numresults; 

    status = msLayerOpen(lp); /* open this layer */
    if(status != MS_SUCCESS) return status;

    status = msLayerGetItems(lp); /* retrieve all the item names */
    if(status != MS_SUCCESS) return status;
    
    if(lp->numjoins > 0) { /* open any necessary JOINs here */
      for(k=0; k<lp->numjoins; k++) {
        status = msJoinConnect(lp, &(lp->joins[k]));
        if(status != MS_SUCCESS) return status;        
      }
    }  

    if(lp->header) { 
      if(msReturnPage(mapserv, lp->header, BROWSE, papszBuffer) != MS_SUCCESS) return MS_FAILURE;
    }

    mapserv->LRN = 1; /* layer result number */
    for(j=0; j<lp->resultcache->numresults; j++) {
      status = msLayerGetShape(lp, &(mapserv->resultshape), lp->resultcache->results[j].tileindex, lp->resultcache->results[j].shapeindex);
      if(status != MS_SUCCESS) return status;

      /* prepare any necessary JOINs here (one-to-one only) */
      if(lp->numjoins > 0) {
        for(k=0; k<lp->numjoins; k++) {
          if(lp->joins[k].type == MS_JOIN_ONE_TO_ONE) {
            msJoinPrepare(&(lp->joins[k]), &(mapserv->resultshape));
            msJoinNext(&(lp->joins[k])); /* fetch the first row */
          }
        }
      }

      if(lp->resultcache->results[j].classindex >= 0 && lp->class[(int)(lp->resultcache->results[j].classindex)]->template) 
        template = lp->class[(int)(lp->resultcache->results[j].classindex)]->template;
      else 
        template = lp->template;

      if(msReturnPage(mapserv, template, QUERY, papszBuffer) != MS_SUCCESS) return MS_FAILURE;

      msFreeShape(&(mapserv->resultshape)); /* init too */

      mapserv->RN++; /* increment counters */
      mapserv->LRN++;
    }

    if(lp->footer) {
      if(msReturnPage(mapserv, lp->footer, BROWSE, papszBuffer) != MS_SUCCESS) return MS_FAILURE;
    }

    msLayerClose(lp);
    mapserv->resultlayer = NULL;
  }

  if(mapserv->map->web.footer) 
    return msReturnPage(mapserv, mapserv->map->web.footer, BROWSE, papszBuffer);

  return MS_SUCCESS;
}


mapservObj *msAllocMapServObj()
{
  mapservObj *mapserv = malloc(sizeof(mapservObj));
   
  mapserv->savemap=MS_FALSE;
  mapserv->savequery=MS_FALSE; /* should the query and/or map be saved  */

  mapserv->sendheaders = MS_TRUE;

  mapserv->request = msAllocCgiObj();

  mapserv->map=NULL;

  mapserv->NumLayers=0; /* number of layers specfied by a user */
  mapserv->MaxLayers=0; /* allocated size of Layers[] array */
  mapserv->Layers = NULL;

  mapserv->icon = NULL;

  mapserv->RawExt.minx=-1;
  mapserv->RawExt.miny=-1;
  mapserv->RawExt.maxx=-1;
  mapserv->RawExt.maxy=-1;

  mapserv->fZoom=1;
  mapserv->Zoom=1; /* default for browsing */
   
  mapserv->resultlayer=NULL;
   
  mapserv->UseShapes=MS_FALSE;

  mapserv->mappnt.x=-1;
  mapserv->mappnt.y=-1;

  mapserv->ZoomDirection=0; /* whether zooming in or out, default is pan or 0 */

  mapserv->Mode=BROWSE; /* can be BROWSE, QUERY, etc. */

  sprintf(mapserv->Id, "%ld%d", (long)time(NULL), (int)getpid());
   
  mapserv->CoordSource=NONE;
  mapserv->ScaleDenom=0;
   
  mapserv->ImgRows=-1;
  mapserv->ImgCols=-1;
   
  mapserv->ImgExt.minx=-1;
  mapserv->ImgExt.miny=-1;
  mapserv->ImgExt.maxx=-1;
  mapserv->ImgExt.maxy=-1;
   
  mapserv->ImgBox.minx=-1;
  mapserv->ImgBox.miny=-1;
  mapserv->ImgBox.maxx=-1;
  mapserv->ImgBox.maxy=-1;
   
  mapserv->RefPnt.x=-1;
  mapserv->RefPnt.y=-1;
  mapserv->ImgPnt.x=-1;
  mapserv->ImgPnt.y=-1;

  mapserv->Buffer=0;

  /* 
  ** variables for multiple query results processing 
  */
  mapserv->RN=0; /* overall result number */
  mapserv->LRN=0; /* result number within a layer */
  mapserv->NL=0; /* total number of layers with results */
  mapserv->NR=0; /* total number or results */
  mapserv->NLR=0; /* number of results in a layer */
   
  return mapserv;
}

void msFreeMapServObj(mapservObj* mapserv)
{
  int i;

  if(mapserv) {
    msFreeMap(mapserv->map);

    msFreeCgiObj(mapserv->request);
    mapserv->request = NULL;

    for(i=0;i<mapserv->NumLayers;i++) 
      msFree(mapserv->Layers[i]);
    msFree(mapserv->Layers);

    msFree(mapserv->icon);

    msFree(mapserv);
  }
}

/*
** Ensure there is at least one free entry in the Layers array.
**
** This function is safe to use for the initial allocation of the Layers[]
** array as well (i.e. when MaxLayers==0 and Layers==NULL)
**
** Returns MS_SUCCESS/MS_FAILURE
*/
int msGrowMapservLayers( mapservObj* mapserv )
{
  /* Do we need to increase the size of Layers[] by MS_LAYER_ALLOCSIZE? */
  if(mapserv->NumLayers == mapserv->MaxLayers) {
    int i;

    if(mapserv->MaxLayers == 0) {
      /* initial allocation of array */
      mapserv->MaxLayers += MS_LAYER_ALLOCSIZE;
      mapserv->NumLayers = 0;
      mapserv->Layers = (char**)malloc(mapserv->MaxLayers*sizeof(char*));
    } else {
      /* realloc existing array */
      mapserv->MaxLayers += MS_LAYER_ALLOCSIZE;
      mapserv->Layers = (char**)realloc(mapserv->Layers, mapserv->MaxLayers*sizeof(char*));
    }

    if(mapserv->Layers == NULL) {
      msSetError(MS_MEMERR, "Failed to allocate memory for Layers array.", "msGrowMappservLayers()");
      return MS_FAILURE;
    }

    for(i=mapserv->NumLayers; i<mapserv->MaxLayers; i++) {
      mapserv->Layers[i] = NULL;
    }
  }

  return MS_SUCCESS;
}

/*
** Utility function to generate map, legend, scalebar and reference images.
**
** Parameters:
**   - mapserv: mapserv object (used to extract the map object).
**   - bQueryMap: if set to TRUE a query map will be created instead of a regular map.
**   - bReturnOnError: if set to TRUE, the function will return on the first error, else it will try to generate all the images.
*/
int msGenerateImages(mapservObj *mapserv, int bQueryMap, int bReturnOnError)
{
  char buffer[1024];
    
  if(mapserv) {

    /* render the map OR query map */
    if((!bQueryMap && mapserv->map->status == MS_ON) || (bQueryMap && mapserv->map->querymap.status == MS_ON)) {
      imageObj *image = NULL;
      image = msDrawMap(mapserv->map, bQueryMap);

      if(image) { 
        sprintf(buffer, "%s%s%s.%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));

        if(msSaveImage(mapserv->map, image, buffer) != MS_SUCCESS && bReturnOnError) {
          msFreeImage(image);
          return MS_FALSE;
        }
        msFreeImage(image);
      } else if(bReturnOnError)
        return MS_FALSE;
    }

    /* render the legend */
    if(mapserv->map->legend.status == MS_ON) {
      imageObj *image = NULL;
      image = msDrawLegend(mapserv->map, MS_FALSE);
      if(image) { 
        sprintf(buffer, "%s%sleg%s.%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
                
        if(msSaveImage(mapserv->map, image, buffer) != MS_SUCCESS && bReturnOnError) {
          msFreeImage(image);
          return MS_FALSE;
        }
        msFreeImage(image);
      } else if(bReturnOnError)
        return MS_FALSE;
    }

    /* render the scalebar */
    if(mapserv->map->scalebar.status == MS_ON) {
      imageObj *image = NULL;
      image = msDrawScalebar(mapserv->map);
      if(image) {
        sprintf(buffer, "%s%ssb%s.%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
        if(msSaveImage(mapserv->map, image, buffer) != MS_SUCCESS && bReturnOnError) {
          msFreeImage(image);
          return MS_FALSE;
        }
        msFreeImage(image);
      } else if(bReturnOnError)
        return MS_FALSE;
    }

    /* render the reference map */
    if(mapserv->map->reference.status == MS_ON) {
      imageObj *image;
      image = msDrawReferenceMap(mapserv->map);
      if(image) { 
        sprintf(buffer, "%s%sref%s.%s", mapserv->map->web.imagepath, mapserv->map->name, mapserv->Id, MS_IMAGE_EXTENSION(mapserv->map->outputformat));
        if(msSaveImage(mapserv->map, image, buffer) != MS_SUCCESS && bReturnOnError) {
          msFreeImage(image);
          return MS_FALSE;
        }
        msFreeImage(image);
      } else if(bReturnOnError)
        return MS_FALSE;
    }
        
  }
    
  return MS_TRUE;
}

/*
** Utility function to open a template file, process it and 
** and return into a buffer the processed template. Uses the 
** template file from the web object. Returns NULL if there is 
** an error.
*/ 
char *msProcessTemplate(mapObj *map, int bGenerateImages, char **names, char **values, int numentries)
{
  char *pszBuffer = NULL;

  if(map) {

    /* Initialize object and set appropriate defaults. */
    mapservObj  *mapserv  = NULL;
    mapserv = msAllocMapServObj();

    mapserv->map = map;
    mapserv->Mode = BROWSE;

    if(names && values && numentries > 0) {
      mapserv->request->ParamNames = names;
      mapserv->request->ParamValues = values;
      mapserv->request->NumParams = numentries;    
    }

    /*
    ** ISSUE/TODO : some of the name/values should be extracted and
    ** processed (ex imgext, layers, ...) as it is done in function
    ** loadform.
    */

    if(bGenerateImages)
      msGenerateImages(mapserv, MS_FALSE, MS_FALSE);

    /*
    ** Process the template.
    **
    ** TODO : use web minscaledenom/maxscaledenom depending on the scale.
    */
    if(msReturnPage(mapserv, mapserv->map->web.template, BROWSE, &pszBuffer) != MS_SUCCESS) {
      msFree(pszBuffer);
      pszBuffer = NULL;
    }

    /* Don't free the map and names and values arrays since they were passed by reference. */
    mapserv->map = NULL;
    mapserv->request->ParamNames = mapserv->request->ParamValues = NULL;
    mapserv->request->NumParams = 0;
    msFreeMapServObj(mapserv);
  }

  return pszBuffer;
}

/*
** Utility method to process the legend template.
*/
char *msProcessLegendTemplate(mapObj *map, char **names, char **values, int numentries)
{
  char *pszOutBuf = NULL;

  if(map && map->legend.template) {

    /* Initialize object and set appropriate defaults. */
    mapservObj  *mapserv  = NULL;
    mapserv = msAllocMapServObj();

    mapserv->map = map;
    mapserv->Mode = BROWSE;

    if(names && values && numentries > 0) {
      mapserv->request->ParamNames = names;
      mapserv->request->ParamValues = values;
      mapserv->request->NumParams = numentries;    
    }

    pszOutBuf = generateLegendTemplate(mapserv);

    /* Don't free the map and names and values arrays since they were passed by reference. */
    mapserv->map = NULL;
    mapserv->request->ParamNames = mapserv->request->ParamValues = NULL;
    mapserv->request->NumParams = 0;
    msFreeMapServObj(mapserv);
  }

  return pszOutBuf;
}

/*
** Utility function that process a template file(s) used in the
** query and return the processed template(s) in a buffer.
*/
char *msProcessQueryTemplate(mapObj *map, int bGenerateImages, char **names, char **values, int numentries)
{
  char *pszBuffer = NULL;

  if(map) {

    /* Initialize object and set appropriate defaults. */
    mapservObj *mapserv = NULL;
    mapserv = msAllocMapServObj();

    mapserv->map = map;
    mapserv->Mode = QUERY;

    if(names && values && numentries > 0) {
      mapserv->request->ParamNames = names;
      mapserv->request->ParamValues = values;
      mapserv->request->NumParams = numentries;    
    }

    if(bGenerateImages)
      msGenerateImages(mapserv, MS_TRUE, MS_FALSE);

    mapserv->sendheaders = MS_FALSE;
    msReturnTemplateQuery(mapserv, mapserv->map->web.queryformat, &pszBuffer);

    mapserv->map = NULL;
    mapserv->request->ParamNames = mapserv->request->ParamValues = NULL;
    mapserv->request->NumParams = 0;
    msFreeMapServObj(mapserv);
  }

  return pszBuffer;
}
