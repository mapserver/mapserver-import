/******************************************************************************
 * $Id$
 *
 * Project:  MapServer
 * Purpose:  Various utility functions ... a real hodgepodge.
 * Author:   Steve Lime and the MapServer team.
 *
 * Notes: Some code (notably msAlphaBlend()) are directly derived from GD. See
 * the mapserver/GD-COPYING file for the GD license.  Use of this code in this
 * manner is compatible with the MapServer license.
 *
 ******************************************************************************
 * Copyright (c) 1996-2005 Regents of the University of Minnesota.
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
 *****************************************************************************/

#include <time.h>

#include "mapserver.h"
#include "maptime.h"
#include "mapthread.h"
#include "mapcopy.h"

#if defined(_WIN32) && !defined(__CYGWIN__)
# include <windows.h>
# include <tchar.h>
# include <fcntl.h>
# include <io.h>
#include <process.h>
#endif

MS_CVSID("$Id$")

extern char *msyystring_buffer;
extern int msyylex_destroy(void);
extern int yyparse(parseObj *);

int msScaleInBounds(double scale, double minscale, double maxscale) {
  if(scale > 0) {
    if(maxscale != -1 && scale >= maxscale) return MS_FALSE;
    if(minscale != -1 && scale < minscale) return MS_FALSE;
  }
  return MS_TRUE;
}

/*
** Helper functions to convert from strings to other types or objects.
*/
static int bindIntegerAttribute(int *attribute, char *value)
{
  if(!value || strlen(value) == 0) return MS_FAILURE;
  *attribute = MS_NINT(atof(value)); /*use atof instead of atoi as a fix for bug 2394*/
  return MS_SUCCESS;
}

static int bindDoubleAttribute(double *attribute, char *value)
{
  if(!value || strlen(value) == 0) return MS_FAILURE;
  *attribute = atof(value);
  return MS_SUCCESS;
}

static int bindColorAttribute(colorObj *attribute, char *value)
{
  int len;

  if(!value || ((len = strlen(value)) == 0)) return MS_FAILURE;
  
  if(value[0] == '#' && (len == 7 || len == 9)) { /* got a hex color */
    char hex[2];

    hex[0] = value[1];
    hex[1] = value[2];
    attribute->red = msHexToInt(hex);
    hex[0] = value[3];
    hex[1] = value[4];
    attribute->green = msHexToInt(hex);
    hex[0] = value[5];
    hex[1] = value[6];
    attribute->blue = msHexToInt(hex);
    if(len == 9) {
      hex[0] = value[7];
      hex[1] = value[8];
      attribute->alpha = msHexToInt(hex);
    }
    return MS_SUCCESS;
  } else { /* try a space delimited string */
    char **tokens=NULL;
    int numtokens=0;

    tokens = msStringSplit(value, ' ', &numtokens);
    if(tokens==NULL || numtokens != 3) {
      msFreeCharArray(tokens, numtokens);
      return MS_FAILURE; /* punt */
    }

    attribute->red = atoi(tokens[0]);
    attribute->green = atoi(tokens[1]);
    attribute->blue = atoi(tokens[2]);
    msFreeCharArray(tokens, numtokens);

    return MS_SUCCESS;
  }

  return MS_FAILURE; /* shouldn't get here */
}

static void bindStyle(layerObj *layer, shapeObj *shape, styleObj *style, int querymapMode) {
  if(style->numbindings > 0) {
    if(style->bindings[MS_STYLE_BINDING_SYMBOL].index != -1) {
      style->symbol = msGetSymbolIndex(&(layer->map->symbolset), shape->values[style->bindings[MS_STYLE_BINDING_SYMBOL].index], MS_TRUE);
      if(style->symbol == -1) style->symbol = 0; /* a reasonable default (perhaps should throw an error?) */
    }
    if(style->bindings[MS_STYLE_BINDING_ANGLE].index != -1) {
      style->angle = 360.0;
      bindDoubleAttribute(&style->angle, shape->values[style->bindings[MS_STYLE_BINDING_ANGLE].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_SIZE].index != -1) {
      style->size = 1;
      bindDoubleAttribute(&style->size, shape->values[style->bindings[MS_STYLE_BINDING_SIZE].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_WIDTH].index != -1) {
      style->width = 1;
      bindDoubleAttribute(&style->width, shape->values[style->bindings[MS_STYLE_BINDING_WIDTH].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_COLOR].index != -1 && (querymapMode != MS_TRUE)) {
      MS_INIT_COLOR(style->color, -1,-1,-1,255);
      bindColorAttribute(&style->color, shape->values[style->bindings[MS_STYLE_BINDING_COLOR].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_OUTLINECOLOR].index != -1 && (querymapMode != MS_TRUE)) {
      MS_INIT_COLOR(style->outlinecolor, -1,-1,-1,255);
      bindColorAttribute(&style->outlinecolor, shape->values[style->bindings[MS_STYLE_BINDING_OUTLINECOLOR].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].index != -1) {
        style->outlinewidth = 1;
        bindDoubleAttribute(&style->outlinewidth, shape->values[style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_OPACITY].index != -1) {
      style->opacity = 100;
      bindIntegerAttribute(&style->opacity, shape->values[style->bindings[MS_STYLE_BINDING_OPACITY].index]);

      if(style->opacity < 100) {
        int alpha;
	alpha = MS_NINT(style->opacity*2.55);

	style->color.alpha = alpha;
	style->outlinecolor.alpha = alpha;
	style->backgroundcolor.alpha = alpha;

	style->mincolor.alpha = alpha;
	style->maxcolor.alpha = alpha;
      }
    }
    if(style->bindings[MS_STYLE_BINDING_OFFSET_X].index != -1) {
        style->offsetx = 0;
        bindDoubleAttribute(&style->offsetx, shape->values[style->bindings[MS_STYLE_BINDING_OFFSET_X].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_OFFSET_Y].index != -1) {
        style->offsety = 0;
        bindDoubleAttribute(&style->offsety, shape->values[style->bindings[MS_STYLE_BINDING_OFFSET_Y].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_POLAROFFSET_PIXEL].index != -1) {
        style->polaroffsetpixel = 0;
        bindDoubleAttribute(&style->polaroffsetpixel, shape->values[style->bindings[MS_STYLE_BINDING_POLAROFFSET_PIXEL].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_POLAROFFSET_ANGLE].index != -1) {
        style->polaroffsetangle = 0;
        bindDoubleAttribute(&style->polaroffsetangle, shape->values[style->bindings[MS_STYLE_BINDING_POLAROFFSET_ANGLE].index]);
    }
    if(style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].index != -1) {
        style->outlinewidth = 1;
        bindDoubleAttribute(&style->outlinewidth, shape->values[style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].index]);
    }

  }
}

static void bindLabel(layerObj *layer, shapeObj *shape, labelObj *label, int querymapMode) {
  int i;

  /* check the label styleObj's (TODO: do we need to use querymapMode here? */
  for(i=0; i<label->numstyles; i++) {
    bindStyle(layer, shape, label->styles[i], querymapMode);
  }

  if(label->numbindings > 0) {
    if(label->bindings[MS_LABEL_BINDING_ANGLE].index != -1) {
      label->angle = 0.0;
      bindDoubleAttribute(&label->angle, shape->values[label->bindings[MS_LABEL_BINDING_ANGLE].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_SIZE].index != -1) {
      label->size = 1;
      bindDoubleAttribute(&label->size, shape->values[label->bindings[MS_LABEL_BINDING_SIZE].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_COLOR].index != -1) {
      MS_INIT_COLOR(label->color, -1,-1,-1,255);
      bindColorAttribute(&label->color, shape->values[label->bindings[MS_LABEL_BINDING_COLOR].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_OUTLINECOLOR].index != -1) {
      MS_INIT_COLOR(label->outlinecolor, -1,-1,-1,255);
      bindColorAttribute(&label->outlinecolor, shape->values[label->bindings[MS_LABEL_BINDING_OUTLINECOLOR].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_FONT].index != -1) {
      msFree(label->font);
      label->font = msStrdup(shape->values[label->bindings[MS_LABEL_BINDING_FONT].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_PRIORITY].index != -1) {
      label->priority = MS_DEFAULT_LABEL_PRIORITY;
      bindIntegerAttribute(&label->priority, shape->values[label->bindings[MS_LABEL_BINDING_PRIORITY].index]);
    }

    if(label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].index != -1) { 
      label->shadowsizex = 1; 
      bindIntegerAttribute(&label->shadowsizex, shape->values[label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].index]); 
    } 
    if(label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].index != -1) { 
      label->shadowsizey = 1; 
      bindIntegerAttribute(&label->shadowsizey, shape->values[label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].index]); 
    } 

    if(label->bindings[MS_LABEL_BINDING_POSITION].index != -1) {
      int tmpPosition;
      bindIntegerAttribute(&tmpPosition, shape->values[label->bindings[MS_LABEL_BINDING_POSITION].index]);
      if(tmpPosition != 0) { /* is this test sufficient? */
        label->position = tmpPosition;
      } else { /* Integer binding failed, look for strings like cc, ul, lr, etc... */
        if(strlen(shape->values[label->bindings[MS_LABEL_BINDING_POSITION].index]) == 2) {
          char *vp = shape->values[label->bindings[MS_LABEL_BINDING_POSITION].index];
          if(!strncasecmp(vp,"ul",2))
            label->position = MS_UL;
          else if(!strncasecmp(vp,"lr",2))
            label->position = MS_LR;     
          else if(!strncasecmp(vp,"ur",2))
            label->position = MS_UR;     
          else if(!strncasecmp(vp,"ll",2))
            label->position = MS_LL;     
          else if(!strncasecmp(vp,"cr",2))
            label->position = MS_CR;     
          else if(!strncasecmp(vp,"cl",2))
            label->position = MS_CL;     
          else if(!strncasecmp(vp,"uc",2))
            label->position = MS_UC;     
          else if(!strncasecmp(vp,"lc",2))
            label->position = MS_LC;     
          else if(!strncasecmp(vp,"cc",2))
            label->position = MS_CC;     
        }       
      }
    }
  }
}

/*
** Function to bind various layer properties to shape attributes.
*/
int msBindLayerToShape(layerObj *layer, shapeObj *shape, int querymapMode)
{
  int i, j;

  if(!layer || !shape) return MS_FAILURE;

  for(i=0; i<layer->numclasses; i++) {
    /* check the styleObj's */
    for(j=0; j<layer->class[i]->numstyles; j++) {
      bindStyle(layer, shape, layer->class[i]->styles[j], querymapMode);
    }

    /* check the labelObj's */
    for(j=0; j<layer->class[i]->numlabels; j++) {
      bindLabel(layer, shape, layer->class[i]->labels[j], querymapMode);
    }
  } /* next classObj */

  return MS_SUCCESS;
}

/*
 * Used to get red, green, blue integers separately based upon the color index
 */
int getRgbColor(mapObj *map,int i,int *r,int *g,int *b) {
/* check index range */
    int status=1;
    *r=*g=*b=-1;
    if ((i > 0 ) && (i <= map->palette.numcolors) ) {
       *r=map->palette.colors[i-1].red;
       *g=map->palette.colors[i-1].green;
       *b=map->palette.colors[i-1].blue;
       status=0;
    }
    return status;
}

static int searchContextForTag(mapObj *map, char **ltags, char *tag, char *context, int requires)
{
  int i;

  if(!context) return MS_FAILURE;

  /*  printf("\tin searchContextForTag, searching %s for %s\n", context, tag); */

  if(strstr(context, tag) != NULL) return MS_SUCCESS; /* found the tag */

  /* check referenced layers for the tag too */
  for(i=0; i<map->numlayers; i++) {
    if(strstr(context, ltags[i]) != NULL) { /* need to check this layer */
      if(requires == MS_TRUE) {
        if(searchContextForTag(map, ltags, tag, GET_LAYER(map, i)->requires, MS_TRUE) == MS_SUCCESS) return MS_SUCCESS;
      } else {
        if(searchContextForTag(map, ltags, tag, GET_LAYER(map, i)->labelrequires, MS_FALSE) == MS_SUCCESS) return MS_SUCCESS;      
      }
    }
  }

  return MS_FAILURE;
}

/*
** Function to take a look at all layers with REQUIRES/LABELREQUIRES set to make sure there are no 
** recursive context requirements set (e.g. layer1 requires layer2 and layer2 requires layer1). This
** is bug 1059.
*/
int msValidateContexts(mapObj *map) 
{
  int i;
  char **ltags;
  int status = MS_SUCCESS;

  ltags = (char **) msSmallMalloc(map->numlayers*sizeof(char *));
  for(i=0; i<map->numlayers; i++) {
    if(GET_LAYER(map, i)->name == NULL) {
      ltags[i] = msStrdup("[NULL]");
    } else {
      ltags[i] = (char *) msSmallMalloc(sizeof(char)*strlen(GET_LAYER(map, i)->name) + 3);
      sprintf(ltags[i], "[%s]", GET_LAYER(map, i)->name);
    }
  }

  /* check each layer's REQUIRES and LABELREQUIRES parameters */
  for(i=0; i<map->numlayers; i++) { 
    /* printf("working on layer %s, looking for references to %s\n", GET_LAYER(map, i)->name, ltags[i]); */
    if(searchContextForTag(map, ltags, ltags[i], GET_LAYER(map, i)->requires, MS_TRUE) == MS_SUCCESS) {
      msSetError(MS_PARSEERR, "Recursion error found for REQUIRES parameter for layer %s.", "msValidateContexts", GET_LAYER(map, i)->name);
      status = MS_FAILURE;
      break;
    }
    if(searchContextForTag(map, ltags, ltags[i], GET_LAYER(map, i)->labelrequires, MS_FALSE) == MS_SUCCESS) {
      msSetError(MS_PARSEERR, "Recursion error found for LABELREQUIRES parameter for layer %s.", "msValidateContexts", GET_LAYER(map, i)->name);
      status = MS_FAILURE;
      break;
    }
    /* printf("done layer %s\n", GET_LAYER(map, i)->name); */
  }

  /* clean up */
  msFreeCharArray(ltags, map->numlayers);

  return status;
}

int msEvalContext(mapObj *map, layerObj *layer, char *context)
{
  int i, status;
  char *tag=NULL;

  expressionObj e;
  parseObj p;

  if(!context) return(MS_TRUE);

  /* initialize a temporary expression (e) */
  initExpression(&e);

  e.string = msStrdup(context);
  e.type = MS_EXPRESSION; /* todo */

  for(i=0; i<map->numlayers; i++) { /* step through all the layers */
    if(layer->index == i) continue; /* skip the layer in question */    
    if (GET_LAYER(map, i)->name == NULL) continue; /* Layer without name cannot be used in contexts */

    tag = (char *)msSmallMalloc(sizeof(char)*strlen(GET_LAYER(map, i)->name) + 3);
    sprintf(tag, "[%s]", GET_LAYER(map, i)->name);

    if(strstr(e.string, tag)) {
      if(msLayerIsVisible(map, (GET_LAYER(map, i))))
        e.string = msReplaceSubstring(e.string, tag, "1");
      else
        e.string = msReplaceSubstring(e.string, tag, "0");
    }

    free(tag);
  }

  msTokenizeExpression(&e, NULL, NULL);

  p.shape = NULL;
  p.expr = &e;
  p.expr->curtoken = p.expr->tokens; /* reset */
  p.type = MS_PARSE_TYPE_BOOLEAN;

  status = yyparse(&p);

  freeExpression(&e);

  if (status != 0) {
    msSetError(MS_PARSEERR, "Failed to parse context", "msEvalContext");
    return MS_FALSE; /* error in parse */
  }

  return p.result.intval;
}

/* msEvalExpression()
 *
 * Evaluates a mapserver expression for a given set of attribute values and
 * returns the result of the expression (MS_TRUE or MS_FALSE)
 * May also return MS_FALSE in case of parsing errors or invalid expressions
 * (check the error stack if you care)
 *
 */
int msEvalExpression(layerObj *layer, shapeObj *shape, expressionObj *expression, int itemindex)
{
  if(!expression->string) return MS_TRUE; /* empty expressions are ALWAYS true */

  switch(expression->type) {
  case(MS_STRING):
    if(itemindex == -1) {
      msSetError(MS_MISCERR, "Cannot evaluate expression, no item index defined.", "msEvalExpression()");
      return MS_FALSE;
    }
    if(itemindex >= layer->numitems) {
      msSetError(MS_MISCERR, "Invalid item index.", "msEvalExpression()");
      return MS_FALSE;
    }
    if(expression->flags & MS_EXP_INSENSITIVE) {
      if(strcasecmp(expression->string, shape->values[itemindex]) == 0) return MS_TRUE; /* got a match */
    } else {
      if(strcmp(expression->string, shape->values[itemindex]) == 0) return MS_TRUE; /* got a match */
    }
    break;
  case(MS_EXPRESSION):
    {
      int status;
      parseObj p;

      p.shape = shape;
      p.expr = expression;
      p.expr->curtoken = p.expr->tokens; /* reset */
      p.type = MS_PARSE_TYPE_BOOLEAN;

      status = yyparse(&p);

      if (status != 0) {
        msSetError(MS_PARSEERR, "Failed to parse expression: %s", "msEvalExpression", expression->string);
        return MS_FALSE;
      }

      return p.result.intval;
      break;
    }
  case(MS_REGEX):
    if(itemindex == -1) {
      msSetError(MS_MISCERR, "Cannot evaluate expression, no item index defined.", "msEvalExpression()");
      return MS_FALSE;
    }
    if(itemindex >= layer->numitems) {
      msSetError(MS_MISCERR, "Invalid item index.", "msEvalExpression()");
      return MS_FALSE;
    }

    if(!expression->compiled) {
      if(expression->flags & MS_EXP_INSENSITIVE) {
        if(ms_regcomp(&(expression->regex), expression->string, MS_REG_EXTENDED|MS_REG_NOSUB|MS_REG_ICASE) != 0) { /* compile the expression */
          msSetError(MS_REGEXERR, "Invalid regular expression.", "msEvalExpression()");
          return MS_FALSE;
        }
      } else {
        if(ms_regcomp(&(expression->regex), expression->string, MS_REG_EXTENDED|MS_REG_NOSUB) != 0) { /* compile the expression */
          msSetError(MS_REGEXERR, "Invalid regular expression.", "msEvalExpression()");
          return MS_FALSE;
        }
      }
      expression->compiled = MS_TRUE;
    }

    if(ms_regexec(&(expression->regex), shape->values[itemindex], 0, NULL, 0) == 0) return MS_TRUE; /* got a match */
    break;
  }

  return MS_FALSE;
}

int *msAllocateValidClassGroups(layerObj *lp, int *nclasses)
{
    int *classgroup = NULL;
    int nvalidclass = 0, i=0;

    if (!lp || !lp->classgroup || lp->numclasses <=0 || !nclasses)
      return NULL;

    classgroup = (int *)msSmallMalloc(sizeof(int)*lp->numclasses);       
    nvalidclass = 0;
    for (i=0; i<lp->numclasses; i++)
    {
        if (lp->class[i]->group && strcasecmp(lp->class[i]->group, lp->classgroup) == 0)
        {
            classgroup[nvalidclass] = i;
            nvalidclass++;
        }
    }
    if (nvalidclass > 0)
    {
        classgroup = (int *)msSmallRealloc(classgroup, sizeof(int)*nvalidclass);
        *nclasses = nvalidclass;
        return classgroup;
    }

    if (classgroup)
      msFree(classgroup);
    
    return NULL;
        
}       

int msShapeGetClass(layerObj *layer, mapObj *map, shapeObj *shape, int *classgroup, int numclasses)
{
  int i, iclass;

  if (layer->numclasses > 0) {
    if (classgroup == NULL || numclasses <=0)
      numclasses = layer->numclasses;

    for(i=0; i<numclasses; i++) {
      if (classgroup)
        iclass = classgroup[i];
      else
        iclass = i;

       if (iclass < 0 || iclass >= layer->numclasses)        
         continue; /* this should never happen but just in case */

       if(map->scaledenom > 0) { /* verify scaledenom here  */
         if((layer->class[iclass]->maxscaledenom > 0) && (map->scaledenom > layer->class[iclass]->maxscaledenom))
           continue; /* can skip this one, next class */
         if((layer->class[iclass]->minscaledenom > 0) && (map->scaledenom <= layer->class[iclass]->minscaledenom))
           continue; /* can skip this one, next class */
        }

       if (layer->connectiontype == MS_INLINE)
           return (iclass);

       /* verify the minfeaturesize */
       if ((shape->type == MS_SHAPE_LINE || shape->type == MS_SHAPE_POLYGON) && (layer->class[iclass]->minfeaturesize > 0))
       {
           double minfeaturesize = Pix2LayerGeoref(map, layer,
                                                   layer->class[iclass]->minfeaturesize);
           if (msShapeCheckSize(shape, minfeaturesize) == MS_FALSE)
               continue; /* skip this one, next class */
       }

       if(layer->class[iclass]->status != MS_DELETE && msEvalExpression(layer, shape, &(layer->class[iclass]->expression), layer->classitemindex) == MS_TRUE)
	 return(iclass);
    }
  }

  return(-1); /* no match */
}

static char *evalTextExpression(expressionObj *expr, shapeObj *shape) {
  char *result=NULL;

  if(!expr->string) return result; /* nothing to do */

  switch(expr->type) {
  case(MS_STRING):
    {
      char *target=NULL;
      tokenListNodeObjPtr node=NULL;
      tokenListNodeObjPtr nextNode=NULL;

      result = msStrdup(expr->string);

      node = expr->tokens;
      if(node) {
        while(node != NULL) {
          nextNode = node->next;
          if(node->token == MS_TOKEN_BINDING_DOUBLE || node->token == MS_TOKEN_BINDING_INTEGER || node->token == MS_TOKEN_BINDING_STRING || node->token == MS_TOKEN_BINDING_TIME) {
            target = (char *) msSmallMalloc(strlen(node->tokenval.bindval.item) + 3);
            sprintf(target, "[%s]", node->tokenval.bindval.item);
            result = msReplaceSubstring(result, target, shape->values[node->tokenval.bindval.index]);
            msFree(target);
          }
          node = nextNode;
        }
      }
      if(!strlen(result)) {
         msFree(result);
         result = NULL;
      }
    }
    break;
  case(MS_EXPRESSION):
    {
      int status;
      parseObj p;

      p.shape = shape;
      p.expr = expr;
      p.expr->curtoken = p.expr->tokens; /* reset */
      p.type = MS_PARSE_TYPE_STRING;

      status = yyparse(&p);

      if (status != 0) {
        msSetError(MS_PARSEERR, "Failed to process text expression: %s", "evalTextExpression", expr->string);
        return NULL;
      }

      result = p.result.strval;        
      break;
    }
  default:
    break;
  }
  if(result && !strlen(result)) {
     msFree(result);
     result = NULL;
  }
  return result;
}

int msShapeGetAnnotation(layerObj *layer, shapeObj *shape) {
  int i, j;

  /* RFC77 TODO: check and throw some errors here... */
  if(!layer || !shape) return MS_FAILURE;

  i = shape->classindex;
  for(j=0; j<layer->class[i]->numlabels; j++) {
     labelObj *lbl = layer->class[i]->labels[j]; /* shortcut */

    lbl->status = MS_ON;
    if(layer->map->scaledenom > 0) {
      if((lbl->maxscaledenom != -1) && (layer->map->scaledenom >= lbl->maxscaledenom)) {
        lbl->status = MS_OFF;
        continue; /* next label */
      }
      if((lbl->minscaledenom != -1) && (layer->map->scaledenom < lbl->minscaledenom)) {
        lbl->status = MS_OFF;
        continue; /* next label */
      }
    }
    if(msEvalExpression(layer, shape, &(lbl->expression), -1) != MS_TRUE) {
      lbl->status = MS_OFF;
      continue; /* next label */
    }

    msFree(lbl->annotext);
    lbl->annotext = NULL;

    if(lbl->text.string) {    
      lbl->annotext = evalTextExpression(&(lbl->text), shape);
    } else if(layer->class[i]->text.string) {
      lbl->annotext = evalTextExpression(&(layer->class[i]->text), shape);
    } else {
      if (shape->values && layer->labelitemindex >= 0 && shape->values[layer->labelitemindex] && strlen(shape->values[layer->labelitemindex]) )
        lbl->annotext = msStrdup(shape->values[layer->labelitemindex]);
      else if(shape->text)
        lbl->annotext = msStrdup(shape->text); /* last resort but common with iniline features */
    }

    if(lbl->annotext && (lbl->encoding || lbl->wrap || lbl->maxlength)) {
       char *newtext = msTransformLabelText(layer->map , lbl, lbl->annotext);
       free(lbl->annotext);
       lbl->annotext = newtext;
    }
  }

  return MS_SUCCESS;
}

/* Check if the shape is enough big to be drawn with the
   layer::minfeaturesize setting. The minfeaturesize parameter should be
   the value in geo ref (not in pixel) and should have been multiplied by
   the resolution factor.
 */
int msShapeCheckSize(shapeObj *shape, double minfeaturesize)
{
    double dx = (shape->bounds.maxx-shape->bounds.minx);
    double dy = (shape->bounds.maxy-shape->bounds.miny);

    if (pow(minfeaturesize,2.0) > (pow(dx,2.0)+pow(dy,2.0)))
        return MS_FALSE;
    
    return MS_TRUE;
}

/*
** Adjusts an image size in one direction to fit an extent.
*/
int msAdjustImage(rectObj rect, int *width, int *height)
{
  if(*width == -1 && *height == -1) {
    msSetError(MS_MISCERR, "Cannot calculate both image height and width.", "msAdjustImage()");
    return(-1);
  }

  if(*width > 0)
    *height = MS_NINT((rect.maxy - rect.miny)/((rect.maxx - rect.minx)/(*width)));
  else
    *width = MS_NINT((rect.maxx - rect.minx)/((rect.maxy - rect.miny)/(*height)));

  return(0);
}

/*
** Make sure extent fits image window to be created. Returns cellsize of output image.
*/
double msAdjustExtent(rectObj *rect, int width, int height)
{
  double cellsize, ox, oy;

  cellsize = MS_MAX(MS_CELLSIZE(rect->minx, rect->maxx, width), MS_CELLSIZE(rect->miny, rect->maxy, height));

  if(cellsize <= 0) /* avoid division by zero errors */
    return(0);

  ox = MS_MAX(((width-1) - (rect->maxx - rect->minx)/cellsize)/2,0); /* these were width-1 and height-1 */
  oy = MS_MAX(((height-1) - (rect->maxy - rect->miny)/cellsize)/2,0);

  rect->minx = rect->minx - ox*cellsize;
  rect->miny = rect->miny - oy*cellsize;
  rect->maxx = rect->maxx + ox*cellsize;
  rect->maxy = rect->maxy + oy*cellsize;

  return(cellsize);
}

/*
** Rect must always contain a portion of bounds. If not, rect is 
** shifted to overlap by overlay percent. The dimensions of rect do
** not change but placement relative to bounds can.
*/
int msConstrainExtent(rectObj *bounds, rectObj *rect, double overlay) 
{
  double offset=0;

  /* check left edge, and if necessary the right edge of bounds */
  if(rect->maxx <= bounds->minx) {
    offset = overlay*(rect->maxx - rect->minx);
    rect->minx += offset; /* shift right */
    rect->maxx += offset;
  } else if(rect->minx >= bounds->maxx) {
    offset = overlay*(rect->maxx - rect->minx);
    rect->minx -= offset; /* shift left */
    rect->maxx -= offset;
  }

  /* check top edge, and if necessary the bottom edge of bounds */
  if(rect->maxy <= bounds->miny) {
    offset = overlay*(rect->maxy - rect->miny);
    rect->miny -= offset; /* shift down */
    rect->maxy -= offset;
  } else if(rect->miny >= bounds->maxy) {
    offset = overlay*(rect->maxy - rect->miny);
    rect->miny += offset; /* shift up */
    rect->maxy += offset;
  }

  return(MS_SUCCESS);
}

/*
** Generic function to save an image to a file.
**
** Note that map may be NULL. If it is set, then it is used for two things:
** - Deal with relative imagepaths (compute absolute path relative to map path)
** - Extract the georeferenced extents and coordinate system
**   of the map for writing out with the image when appropriate 
**   (primarily this means via msSaveImageGDAL() to something like GeoTIFF). 
**
** The filename is NULL when the image is supposed to be written to stdout. 
*/

int msSaveImage(mapObj *map, imageObj *img, char *filename)
{
    int nReturnVal = MS_FAILURE;
    char szPath[MS_MAXPATHLEN];
    struct mstimeval starttime, endtime;

    if(map && map->debug >= MS_DEBUGLEVEL_TUNING) {
        msGettimeofday(&starttime, NULL);
    }

    if (img)
    {
#ifdef USE_GDAL
        if( MS_DRIVER_GDAL(img->format) )
        {
           if (map != NULL && filename != NULL )
             nReturnVal = msSaveImageGDAL(map, img,
                                          msBuildPath(szPath, map->mappath, 
                                                      filename));
           else
             nReturnVal = msSaveImageGDAL(map, img, filename);
        } else
#endif
       if (MS_RENDERER_PLUGIN(img->format)) {
            rendererVTableObj *renderer = img->format->vtable;
            FILE *stream = NULL;
            if(filename) {
               if(map)
                  stream = fopen(msBuildPath(szPath, map->mappath, filename),"wb");
               else
                  stream = fopen(filename,"wb");

               if(!stream) {
                  msSetError(MS_IOERR, 
                             "Failed to create output file (%s).", 
                             "msSaveImage()", (map?szPath:filename) );
                  return MS_FAILURE;
               }
 
            } else {
                if ( msIO_needBinaryStdout() == MS_FAILURE )
                    return MS_FAILURE;
                stream = stdout;
            }

           if(renderer->supports_pixel_buffer) {
                rasterBufferObj data;
                if(renderer->getRasterBufferHandle(img,&data) != MS_SUCCESS)
                   return MS_FAILURE;

                nReturnVal = msSaveRasterBuffer(map,&data,stream,img->format );
            } else {
                nReturnVal = renderer->saveImage(img, map, stream, img->format);
            }
            if( stream != stdout )
                fclose(stream);

        }
        else if( MS_DRIVER_IMAGEMAP(img->format) )
            nReturnVal = msSaveImageIM(img, filename, img->format);
        else
            msSetError(MS_MISCERR, "Unknown image type", 
                       "msSaveImage()"); 
    }

    if(map && map->debug >= MS_DEBUGLEVEL_TUNING) {
      msGettimeofday(&endtime, NULL);
      msDebug("msSaveImage(%s) total time: %.3fs\n", 
              (filename ? filename : "stdout"), 
              (endtime.tv_sec+endtime.tv_usec/1.0e6)-
              (starttime.tv_sec+starttime.tv_usec/1.0e6) );
    }

    return nReturnVal;
}

/*
** Generic function to save an image to a byte array.
** - the return value is the pointer to the byte array 
** - size_ptr contains the number of bytes returned
** - format: the desired output format
**
** The caller is responsible to free the returned array
** The function returns NULL if the output format is not supported. 
*/

unsigned char *msSaveImageBuffer(imageObj* image, int *size_ptr, outputFormatObj *format)
{
    *size_ptr = 0;
    if( MS_RENDERER_PLUGIN(image->format)){
        rasterBufferObj data;
        rendererVTableObj *renderer = image->format->vtable;
        if(renderer->supports_pixel_buffer) {
            bufferObj buffer;
            msBufferInit(&buffer);
            renderer->getRasterBufferHandle(image,&data);
            msSaveRasterBufferToBuffer(&data,&buffer,format);
            *size_ptr = buffer.size;
            return buffer.data;
            /* don't free the bufferObj as we don't own the bytes anymore */
        } else {
            /* check if the renderer supports native buffer output */
            if (renderer->saveImageBuffer)
                return renderer->saveImageBuffer(image, size_ptr, format);

	        msSetError(MS_MISCERR, "Unsupported image type", "msSaveImageBuffer()");
            return NULL;
        }
    }
	msSetError(MS_MISCERR, "Unsupported image type", "msSaveImage()");
    return NULL;
}

/**
 * Generic function to free the imageObj
 */
void msFreeImage(imageObj *image)
{
    if (image)
    {
       if(MS_RENDERER_PLUGIN(image->format)) {
          rendererVTableObj *renderer = image->format->vtable;
          tileCacheObj *next,*cur = image->tilecache;
          while(cur) {
             msFreeImage(cur->image);
             next = cur->next;
             free(cur);
             cur = next;
          }
          image->ntiles = 0;
          renderer->freeImage(image);
        } else if( MS_RENDERER_IMAGEMAP(image->format) )
            msFreeImageIM(image);
        else if( MS_RENDERER_RAWDATA(image->format) )
            msFree(image->img.raw_16bit);
        else
            msSetError(MS_MISCERR, "Unknown image type", 
                       "msFreeImage()"); 

        if (image->imagepath)
            free(image->imagepath);
        if (image->imageurl)
            free(image->imageurl);

        if( --image->format->refcount < 1 )
            msFreeOutputFormat( image->format );

        image->imagepath = NULL;
        image->imageurl = NULL;

        msFree( image->img_mask );
        image->img_mask= NULL;

        msFree( image );
    }     
}

/*
** Return an array containing all the layer's index given a group name.
** If nothing is found, NULL is returned. The nCount is initalized
** to the number of elements in the returned array.
** Note : the caller of the function should free the array.
*/
int *msGetLayersIndexByGroup(mapObj *map, char *groupname, int *pnCount)
{
    int         i;
    int         iLayer = 0;
    int         *aiIndex;

    if(!groupname || !map || !pnCount)
    {
        return NULL;
    }

    aiIndex = (int *)msSmallMalloc(sizeof(int) * map->numlayers);

    for(i=0;i<map->numlayers; i++)
    {
        if(!GET_LAYER(map, i)->group) /* skip it */
            continue;
        if(strcmp(groupname, GET_LAYER(map, i)->group) == 0)
        {
            aiIndex[iLayer] = i;
            iLayer++;
        }
    }

    if (iLayer == 0)
    {
        free(aiIndex);
        aiIndex = NULL;
        *pnCount = 0;
    }
    else
    {
        aiIndex = (int *)msSmallRealloc(aiIndex, sizeof(int)* iLayer);
        *pnCount = iLayer;
    }

  return aiIndex;
}

/* ==================================================================== */
/*      Measured shape utility functions.                               */
/* ==================================================================== */


/************************************************************************/
/*        pointObj *msGetPointUsingMeasure(shapeObj *shape, double m)   */
/*                                                                      */
/*      Using a measured value get the XY location it corresonds        */
/*      to.                                                             */
/*                                                                      */
/************************************************************************/
pointObj *msGetPointUsingMeasure(shapeObj *shape, double m)
{
#ifdef USE_POINT_Z_M
    pointObj    *point = NULL;
    lineObj     line;
    double      dfMin = 0;
    double      dfMax = 0;
    int         i,j = 0;
    int         bFound = 0;
    double      dfFirstPointX = 0;
    double      dfFirstPointY = 0;
    double      dfFirstPointM = 0;
    double      dfSecondPointX = 0;
    double      dfSecondPointY = 0;
    double      dfSecondPointM = 0;
    double      dfCurrentM = 0;
    double      dfFactor = 0;

    if (shape &&  shape->numlines > 0)
    {
/* -------------------------------------------------------------------- */
/*      check fir the first value (min) and the last value(max) to      */
/*      see if the m is contained between these min and max.            */
/* -------------------------------------------------------------------- */
        line = shape->line[0];
        dfMin = line.point[0].m;
        line = shape->line[shape->numlines-1];
        dfMax = line.point[line.numpoints-1].m;

        if (m >= dfMin && m <= dfMax)
        {
            for (i=0; i<shape->numlines; i++)
            {
                line = shape->line[i];
                
                for (j=0; j<line.numpoints; j++)
                {
                    dfCurrentM = line.point[j].m;
                    if (dfCurrentM > m)
                    {
                        bFound = 1;
                        
                        dfSecondPointX = line.point[j].x;
                        dfSecondPointY = line.point[j].y;
                        dfSecondPointM = line.point[j].m;
                        
/* -------------------------------------------------------------------- */
/*      get the previous node xy values.                                */
/* -------------------------------------------------------------------- */
                        if (j > 0) /* not the first point of the line */
                        {
                            dfFirstPointX = line.point[j-1].x;
                            dfFirstPointY = line.point[j-1].y;
                            dfFirstPointM = line.point[j-1].m;
                        }
                        else /* get last point of previous line */
                        {
                            dfFirstPointX = shape->line[i-1].point[0].x;
                            dfFirstPointY = shape->line[i-1].point[0].y;
                            dfFirstPointM = shape->line[i-1].point[0].m;
                        }
                        break;
                    }
                }
            }
        }

        if (!bFound) 
          return NULL;

/* -------------------------------------------------------------------- */
/*      extrapolate the m value to get t he xy coordinate.              */
/* -------------------------------------------------------------------- */

        if (dfFirstPointM != dfSecondPointM) 
          dfFactor = (m-dfFirstPointM)/(dfSecondPointM - dfFirstPointM); 
        else
          dfFactor = 0;

        point = (pointObj *)msSmallMalloc(sizeof(pointObj));
        
        point->x = dfFirstPointX + (dfFactor * (dfSecondPointX - dfFirstPointX));
        point->y = dfFirstPointY + 
            (dfFactor * (dfSecondPointY - dfFirstPointY));
        point->m = dfFirstPointM + 
            (dfFactor * (dfSecondPointM - dfFirstPointM));
        
        return point;
    }

    return NULL;
#else
    msSetError(MS_MISCERR, 
               "The \"m\" parameter for points is unavailable in your build.",
               "msGetPointUsingMeasure()");
    return NULL;
#endif /* USE_POINT_Z_M */
}


/************************************************************************/
/*       IntersectionPointLinepointObj *p, pointObj *a, pointObj *b)    */
/*                                                                      */
/*      Retunrs a point object corresponding to the intersection of     */
/*      point p and a line formed of 2 points : a and b.                */
/*                                                                      */
/*      Alorith base on :                                               */
/*      http://www.faqs.org/faqs/graphics/algorithms-faq/               */
/*                                                                      */
/*      Subject 1.02:How do I find the distance from a point to a line? */
/*                                                                      */
/*          Let the point be C (Cx,Cy) and the line be AB (Ax,Ay) to (Bx,By).*/
/*          Let P be the point of perpendicular projection of C on AB.  The parameter*/
/*          r, which indicates P's position along AB, is computed by the dot product */
/*          of AC and AB divided by the square of the length of AB:     */
/*                                                                      */
/*          (1)     AC dot AB                                           */
/*              r = ---------                                           */
/*                  ||AB||^2                                            */
/*                                                                      */
/*          r has the following meaning:                                */
/*                                                                      */
/*              r=0      P = A                                          */
/*              r=1      P = B                                          */
/*              r<0      P is on the backward extension of AB           */
/*              r>1      P is on the forward extension of AB            */
/*              0<r<1    P is interior to AB                            */
/*                                                                      */
/*          The length of a line segment in d dimensions, AB is computed by:*/
/*                                                                      */
/*              L = sqrt( (Bx-Ax)^2 + (By-Ay)^2 + ... + (Bd-Ad)^2)      */
/*                                                                      */
/*          so in 2D:                                                   */
/*                                                                      */
/*              L = sqrt( (Bx-Ax)^2 + (By-Ay)^2 )                       */
/*                                                                      */
/*          and the dot product of two vectors in d dimensions, U dot V is computed:*/
/*                                                                      */
/*              D = (Ux * Vx) + (Uy * Vy) + ... + (Ud * Vd)             */
/*                                                                      */
/*          so in 2D:                                                   */
/*                                                                      */
/*              D = (Ux * Vx) + (Uy * Vy)                               */
/*                                                                      */
/*          So (1) expands to:                                          */
/*                                                                      */
/*                  (Cx-Ax)(Bx-Ax) + (Cy-Ay)(By-Ay)                     */
/*              r = -------------------------------                     */
/*                                L^2                                   */
/*                                                                      */
/*          The point P can then be found:                              */
/*                                                                      */
/*              Px = Ax + r(Bx-Ax)                                      */
/*              Py = Ay + r(By-Ay)                                      */
/*                                                                      */
/*          And the distance from A to P = r*L.                         */
/*                                                                      */
/*          Use another parameter s to indicate the location along PC, with the */
/*          following meaning:                                          */
/*                 s<0      C is left of AB                             */
/*                 s>0      C is right of AB                            */
/*                 s=0      C is on AB                                  */
/*                                                                      */
/*          Compute s as follows:                                       */
/*                                                                      */
/*                  (Ay-Cy)(Bx-Ax)-(Ax-Cx)(By-Ay)                       */
/*              s = -----------------------------                       */
/*                              L^2                                     */
/*                                                                      */
/*                                                                      */
/*          Then the distance from C to P = |s|*L.                      */
/*                                                                      */
/************************************************************************/
pointObj *msIntersectionPointLine(pointObj *p, pointObj *a, pointObj *b)
{
    double r = 0;
    double L = 0;
    pointObj *result = NULL;

    if (p && a && b)
    {
        L = sqrt(((b->x - a->x)*(b->x - a->x)) + 
                 ((b->y - a->y)*(b->y - a->y)));

        if (L != 0)
          r = ((p->x - a->x)*(b->x - a->x) + (p->y - a->y)*(b->y - a->y))/(L*L);
        else
          r = 0;

        result = (pointObj *)msSmallMalloc(sizeof(pointObj));
/* -------------------------------------------------------------------- */
/*      We want to make sure that the point returned is on the line     */
/*                                                                      */
/*              r=0      P = A                                          */
/*              r=1      P = B                                          */
/*              r<0      P is on the backward extension of AB           */
/*              r>1      P is on the forward extension of AB            */
/*                    0<r<1    P is interior to AB                      */
/* -------------------------------------------------------------------- */
        if (r < 0)
        {
            result->x = a->x;
            result->y = a->y;
        }
        else if (r > 1)
        {
            result->x = b->x;
            result->y = b->y;
        }
        else
        {
            result->x = a->x + r*(b->x - a->x);
            result->y = a->y + r*(b->y - a->y);
        }
#ifdef USE_POINT_Z_M
        result->m = 0;
#endif
    }

    return result;
}


/************************************************************************/
/*         pointObj *msGetMeasureUsingPoint(shapeObj *shape, pointObj   */
/*      *point)                                                         */
/*                                                                      */
/*      Calculate the intersection point betwwen the point and the      */
/*      shape and return the Measured value at the intersection.        */
/************************************************************************/
pointObj *msGetMeasureUsingPoint(shapeObj *shape, pointObj *point)
{       
    double      dfMinDist = 1e35;
    double      dfDist = 0;
    pointObj    oFirst;
    pointObj    oSecond;
    int         i, j = 0;
    lineObj     line;
    pointObj    *poIntersectionPt = NULL;
    double      dfFactor = 0;
    double      dfDistTotal, dfDistToIntersection = 0;

    if (shape && point)
    {
        for (i=0; i<shape->numlines; i++)
        {
            line = shape->line[i];
/* -------------------------------------------------------------------- */
/*      for each line (2 consecutive lines) get the distance between    */
/*      the line and the point and determine which line segment is      */
/*      the closeset to the point.                                      */
/* -------------------------------------------------------------------- */
            for (j=0; j<line.numpoints-1; j++)
            {
                dfDist = msDistancePointToSegment(point, &line.point[j], &line.point[j+1]);
                if (dfDist < dfMinDist)
                {
                    oFirst.x = line.point[j].x;
                    oFirst.y = line.point[j].y;
#ifdef USE_POINT_Z_M
                    oFirst.m = line.point[j].m;
#endif
                    
                    oSecond.x =  line.point[j+1].x;
                    oSecond.y =  line.point[j+1].y;
#ifdef USE_POINT_Z_M
                    oSecond.m =  line.point[j+1].m;
#endif

                    dfMinDist = dfDist;
                }
            }
        }
/* -------------------------------------------------------------------- */
/*      once we have the nearest segment, look for the x,y location     */
/*      which is the nearest intersection between the line and the      */
/*      point.                                                          */
/* -------------------------------------------------------------------- */
        poIntersectionPt = msIntersectionPointLine(point, &oFirst, &oSecond);
        if (poIntersectionPt)
        {
            dfDistTotal = sqrt(((oSecond.x - oFirst.x)*(oSecond.x - oFirst.x)) + 
                               ((oSecond.y - oFirst.y)*(oSecond.y - oFirst.y)));

            dfDistToIntersection = sqrt(((poIntersectionPt->x - oFirst.x)*
                                         (poIntersectionPt->x - oFirst.x)) + 
                                        ((poIntersectionPt->y - oFirst.y)*
                                         (poIntersectionPt->y - oFirst.y)));

            dfFactor = dfDistToIntersection / dfDistTotal;

#ifdef USE_POINT_Z_M
            poIntersectionPt->m = oFirst.m + (oSecond.m - oFirst.m)*dfFactor;
#endif

            return poIntersectionPt;
        }
    
    }
    return NULL;
}

/* ==================================================================== */
/*   End   Measured shape utility functions.                            */
/* ==================================================================== */


char **msGetAllGroupNames(mapObj *map, int *numTok)
{
    char        **papszGroups = NULL;
    int         bFound = 0;
    int         nCount = 0;
    int         i = 0, j = 0;

    *numTok = 0;
   
    if (!map->layerorder)
    {
       map->layerorder = (int*)msSmallMalloc(map->numlayers * sizeof(int));

       /*
        * Initiate to default order
        */
       for (i=0; i<map->numlayers; i++)
         map->layerorder[i] = i;   
    }
   
    if (map != NULL && map->numlayers > 0)
    {
        nCount = map->numlayers;
        papszGroups = (char **)msSmallMalloc(sizeof(char *)*nCount);

        for (i=0; i<nCount; i++)
            papszGroups[i] = NULL;
       
        for (i=0; i<nCount; i++)
        {
            layerObj *lp;
            lp = (GET_LAYER(map, map->layerorder[i]));

            bFound = 0;
            if (lp->group && lp->status != MS_DELETE)
            {
                for (j=0; j<*numTok; j++)
                {
                    if (papszGroups[j] &&
                        strcmp(lp->group, papszGroups[j]) == 0)
                    {
                        bFound = 1;
                        break;
                    }
                }
                if (!bFound)
                {
                    /* New group... add to the list of groups found */
                    papszGroups[(*numTok)] = msStrdup(lp->group);
                    (*numTok)++;
                }
            }
        }

    }
   
    return papszGroups;
}

/************************************************************************/
/*                         msForceTmpFileBase()                         */
/************************************************************************/

static int tmpCount = 0;
static char *ForcedTmpBase = NULL;

void msForceTmpFileBase( const char *new_base )
{
/* -------------------------------------------------------------------- */
/*      Clear previous setting, if any.                                 */
/* -------------------------------------------------------------------- */
    if( ForcedTmpBase != NULL )
    {
        free( ForcedTmpBase );
        ForcedTmpBase = NULL;
    }
    
    tmpCount = -1;

    if( new_base == NULL )
        return;

/* -------------------------------------------------------------------- */
/*      Record new base.                                                */
/* -------------------------------------------------------------------- */
    ForcedTmpBase = msStrdup( new_base );
    tmpCount = 0;
}

/**********************************************************************
 *                          msTmpFile()
 *
 * Generate a Unique temporary file.
 * 
 * Returns char* which must be freed by caller.
 **********************************************************************/
char *msTmpFile(mapObj *map, const char *mappath, const char *tmppath, const char *ext)
{
    char szPath[MS_MAXPATHLEN];
    const char *fullFname;
    char *tmpFileName; /* big enough for time + pid + ext */
    char *tmpBase = NULL;

    tmpBase = msTmpPath(map, mappath, tmppath);
    tmpFileName = msTmpFilename(ext);

    fullFname = msBuildPath(szPath, tmpBase, tmpFileName);

    free(tmpFileName);
    free(tmpBase);

    if (fullFname)
        return msStrdup(fullFname);

    return NULL;
}

/**********************************************************************
 *                          msTmpPath()
 *
 * Return the temporary path based on the platform.
 * 
 * Returns char* which must be freed by caller.
 **********************************************************************/
char *msTmpPath(mapObj *map, const char *mappath, const char *tmppath)
{
    char szPath[MS_MAXPATHLEN];
    const char *fullPath;
    const char *tmpBase;
#ifdef _WIN32
    DWORD dwRetVal = 0;
    TCHAR lpTempPathBuffer[MAX_PATH];
#endif

    if( ForcedTmpBase != NULL )
        tmpBase = ForcedTmpBase;
    else if (tmppath != NULL)
        tmpBase = tmppath;
    else if (getenv("MS_TEMPPATH"))
        tmpBase = getenv("MS_TEMPPATH");
    else if (map && map->web.temppath)
        tmpBase = map->web.temppath;
    else /* default paths */
    {
#ifndef _WIN32        
        tmpBase = "/tmp/";
#else
        dwRetVal =  GetTempPath(MAX_PATH,          /* length of the buffer */
                                lpTempPathBuffer); /* buffer for path */
        if (dwRetVal > MAX_PATH || (dwRetVal == 0))
        {
            tmpBase = "C:\\";
        } 
        else
        {
            tmpBase = (char*)lpTempPathBuffer;
        }
#endif
    }

    fullPath = msBuildPath(szPath, mappath, tmpBase);
    return strdup(fullPath);
}

/**********************************************************************
 *                          msTmpFilename()
 *
 * Generate a Unique temporary filename.
 * 
 * Returns char* which must be freed by caller.
 **********************************************************************/
char *msTmpFilename(const char *ext)
{
    char *tmpFname;
    int tmpFnameBufsize;
    char *fullFname;
    char tmpId[128]; /* big enough for time + pid + ext */

    snprintf(tmpId, sizeof(tmpId), "%lx_%x",(long)time(NULL),(int)getpid());

    if (ext == NULL)  ext = "";
    tmpFnameBufsize = strlen(tmpId) + 10 + strlen(ext) + 1;
    tmpFname = (char*)msSmallMalloc(tmpFnameBufsize);

    msAcquireLock( TLOCK_TMPFILE );
    snprintf(tmpFname, tmpFnameBufsize, "%s_%x.%s", tmpId, tmpCount++, ext);
    msReleaseLock( TLOCK_TMPFILE );

    fullFname = strdup(tmpFname);
    free(tmpFname);

    return fullFname;
}

/**
 *  Generic function to Initalize an image object.
 */
imageObj *msImageCreate(int width, int height, outputFormatObj *format, 
                        char *imagepath, char *imageurl, double resolution,
                        double defresolution, colorObj *bg)
{
    imageObj *image = NULL;
    if(MS_RENDERER_PLUGIN(format)) {
        
    	image = format->vtable->createImage(width,height,format,bg);
        if (image == NULL)
        {
            msSetError(MS_MEMERR, "Unable to create new image object.", "msImageCreate()");
            return NULL;
        }

    	image->format = format;
        format->refcount++;

        image->width = width;
        image->height = height;
        image->imagepath = NULL;
        image->imageurl = NULL;
        image->tilecache = NULL;
        image->ntiles = 0;
        image->resolution = resolution;
        image->resolutionfactor = resolution/defresolution;

        if (imagepath)
            image->imagepath = msStrdup(imagepath);
        if (imageurl)
            image->imageurl = msStrdup(imageurl);
    }
    else if( MS_RENDERER_RAWDATA(format) )
    {
        if( format->imagemode != MS_IMAGEMODE_INT16
            && format->imagemode != MS_IMAGEMODE_FLOAT32 
            && format->imagemode != MS_IMAGEMODE_BYTE )
        {
            msSetError(MS_IMGERR, 
                       "Attempt to use illegal imagemode with rawdata renderer.",
                       "msImageCreate()" );
            return NULL;
        }

        image = (imageObj *)calloc(1,sizeof(imageObj));
        if (image == NULL)
        {
            msSetError(MS_MEMERR, "Unable to create new image object.", "msImageCreate()");
            return NULL;
        }

        if( format->imagemode == MS_IMAGEMODE_INT16 )
            image->img.raw_16bit = (short *) 
                msSmallCalloc(sizeof(short),width*height*format->bands);
        else if( format->imagemode == MS_IMAGEMODE_FLOAT32 )
            image->img.raw_float = (float *) 
                msSmallCalloc(sizeof(float),width*height*format->bands);
        else if( format->imagemode == MS_IMAGEMODE_BYTE )
            image->img.raw_byte = (unsigned char *) 
                msSmallCalloc(sizeof(unsigned char),width*height*format->bands);

        if( image->img.raw_16bit == NULL )
        {
            msFree( image );
            msSetError(MS_IMGERR, 
                       "Attempt to allocate raw image failed, out of memory.",
                       "msImageCreate()" );
            return NULL;
        }

        image->img_mask = msAllocBitArray( width*height );
            
        image->format = format;
        format->refcount++;

        image->width = width;
        image->height = height;
        image->imagepath = NULL;
        image->imageurl = NULL;
        image->resolution = resolution;
        image->resolutionfactor = resolution/defresolution;

        if (imagepath)
            image->imagepath = msStrdup(imagepath);
        if (imageurl)
            image->imageurl = msStrdup(imageurl);

        /* initialize to requested nullvalue if there is one */
        if( msGetOutputFormatOption(image->format,"NULLVALUE",NULL) != NULL )
        {
            int i = image->width * image->height * format->bands;
            const char *nullvalue = msGetOutputFormatOption(image->format,
                                                            "NULLVALUE",NULL);

            if( atof(nullvalue) == 0.0 )
                /* nothing to do */;
            else if( format->imagemode == MS_IMAGEMODE_INT16 )
            {
                short nv = atoi(nullvalue);
                for( ; i > 0; )
                    image->img.raw_16bit[--i] = nv;
            }
            else if( format->imagemode == MS_IMAGEMODE_FLOAT32 )
            {
                float nv = atoi(nullvalue);
                for( ; i > 0; )
                    image->img.raw_float[--i] = nv;
            }
            else if( format->imagemode == MS_IMAGEMODE_BYTE )
            {
                unsigned char nv = (unsigned char) atoi(nullvalue);

                memset( image->img.raw_byte, nv, i );
            }
        }
    }
    else if( MS_RENDERER_IMAGEMAP(format) )
    {
        image = msImageCreateIM(width, height, format,
                                imagepath, imageurl, resolution, defresolution);
        if( image != NULL ) msImageInitIM( image );
    }
    else 
    {
        msSetError(MS_MISCERR, 
                   "Unsupported renderer requested, unable to initialize image.", 
                   "msImageCreate()");
        return NULL;
    }

    if(!image) 
        msSetError(MS_GDERR, "Unable to initialize image.", "msImageCreate()");
    image->refpt.x = image->refpt.y = 0;
    return image;
}


/**
 * Generic function to transorm a point.
 * 
 */
void  msTransformPoint(pointObj *point, rectObj *extent, double cellsize, 
                       imageObj *image)   
{
    /*We should probabaly have a function defined at all the renders*/
    if (image != NULL && MS_RENDERER_PLUGIN(image->format) && 
        image->format->renderer == MS_RENDER_WITH_KML)
      return;

    point->x = MS_MAP2IMAGE_X(point->x, extent->minx, cellsize);
    point->y = MS_MAP2IMAGE_Y(point->y, extent->maxy, cellsize);
}




/*
** Helper functions supplied as part of bug #2868 solution. Consider moving these to
** mapprimitive.c for more general use.
*/

/* vector difference */
static pointObj point_diff(const pointObj a, const pointObj b) {
  pointObj retv;
  retv.x = a.x-b.x;
  retv.y = a.y-b.y;
#ifdef USE_POINT_Z_M
  retv.z = a.z-b.z;
  retv.m = a.m-b.m;
#endif
  return retv;
}

/* vector sum */
static pointObj point_sum(const pointObj a, const pointObj b) {
  pointObj retv;
  retv.x = a.x+b.x;
  retv.y = a.y+b.y;
#ifdef USE_POINT_Z_M
  retv.z = a.z+b.z;
  retv.m = a.m+b.m;
#endif
  return retv;
}

/* vector multiply */
static pointObj point_mul(const pointObj a, double b) {
  pointObj retv;
  retv.x = a.x*b;
  retv.y = a.y*b;
#ifdef USE_POINT_Z_M
  retv.z = a.z*b;
  retv.m = a.m*b;
#endif
  return retv;
}

/* vector ??? */
static double point_abs2(const pointObj a) {
#ifdef USE_POINT_Z_M
  return a.x*a.x+a.y*a.y+a.z*a.z+a.m*a.m;
#else
  return a.x*a.x+a.y*a.y;
#endif
}

/* vector normal */
static pointObj point_norm(const pointObj a) {
  double lenmul;
  pointObj retv;

#ifdef USE_POINT_Z_M
  if (a.x==0 && a.y==0 && a.z==0 && a.m==0) 
#else
  if (a.x==0 && a.y==0) 
#endif
    return a;

  lenmul=1.0/sqrt(point_abs2(a));  /* this seems to be the costly operation */

  retv.x = a.x*lenmul;
  retv.y = a.y*lenmul;
#ifdef USE_POINT_Z_M
  retv.z = a.z*lenmul;
  retv.m = a.m*lenmul;
#endif

  return retv;
}

/* rotate a vector 90 degrees */
static pointObj point_rotz90(const pointObj a) {
  double nx=-1.0*a.y, ny=a.x;
  pointObj retv=a;
  retv.x=nx; retv.y=ny;
  return retv;
}

/* vector cross product (warning: z and m dimensions are ignored!) */
static double point_cross(const pointObj a, const pointObj b) {
  return a.x*b.y-a.y*b.x;
}

/* 
** For offset corner point calculation 1/sin() is used
** to avoid 1/0 division (and long spikes) we define a 
** limit for sin().
*/
#define CURVE_SIN_LIMIT 0.3

shapeObj *msOffsetPolyline(shapeObj *p, double offsetx, double offsety) {
  int i, j, first,idx;

  shapeObj *ret = (shapeObj*)msSmallMalloc(sizeof(shapeObj));
  msInitShape(ret);
  ret->numlines = p->numlines;
  ret->line=(lineObj*)msSmallMalloc(sizeof(lineObj)*ret->numlines);
  for(i=0;i<ret->numlines;i++) {
    ret->line[i].numpoints=p->line[i].numpoints;
    ret->line[i].point=(pointObj*)msSmallMalloc(sizeof(pointObj)*ret->line[i].numpoints);
  }

  if(offsety == -99) { /* complex calculations */
    for (i = 0; i < p->numlines; i++) {
      pointObj old_pt, old_diffdir, old_offdir;
      /* initialize old_offdir and old_diffdir, as gcc isn't smart enough to see that it
       * is not an error to do so, and prints a warning */
      old_offdir.x=old_offdir.y=old_diffdir.x=old_diffdir.y = 0;

      idx=0;
      first = 1;

      /* saved metrics of the last processed point */
      if (p->line[i].numpoints>0)
        old_pt=p->line[i].point[0];
      for(j=1; j<p->line[i].numpoints; j++) {
        const pointObj pt = p->line[i].point[j]; /* place of the point */
        const pointObj diffdir = point_norm(point_diff(pt,old_pt)); /* direction of the line */
        const pointObj offdir = point_rotz90(diffdir); /* direction where the distance between the line and the offset is measured */
        pointObj offpt; /* this will be the corner point of the offset line */

        /* offset line points computation */
        if(first == 1) { /* first point */
          first = 0;
          offpt = point_sum(old_pt,point_mul(offdir,offsetx));
        } else { /* middle points */
          /* curve is the angle of the last and the current line's direction (supplementary angle of the shape's inner angle) */
          double sin_curve = point_cross(diffdir,old_diffdir);
          double cos_curve = point_cross(old_offdir,diffdir);
          if ((-1.0)*CURVE_SIN_LIMIT < sin_curve && sin_curve < CURVE_SIN_LIMIT) {
            /* do not calculate 1/sin, instead use a corner point approximation: average of the last and current offset direction and length */

            /* 
	    ** TODO: fair for obtuse inner angles, however, positive and negative
            ** acute inner angles would need special handling - similar to LINECAP
            ** to avoid drawing of long spikes
            */
            offpt = point_sum(old_pt, point_mul(point_sum(offdir, old_offdir),0.5*offsetx));
          } else {
            double base_shift = -1.0*(1.0+cos_curve)/sin_curve;
            offpt = point_sum(old_pt, point_mul(point_sum(point_mul(diffdir,base_shift),offdir), offsetx));
	  }
        }
        ret->line[i].point[idx]=offpt;
        idx++;
        old_pt=pt; old_diffdir=diffdir; old_offdir=offdir;
      }
	            
      /* last point */
      if(first == 0) {
        pointObj offpt=point_sum(old_pt,point_mul(old_offdir,offsetx));
        ret->line[i].point[idx]=offpt;
        idx++;
      }
            
      if(idx != p->line[i].numpoints) {
        /* printf("shouldn't happen :(\n"); */
        ret->line[i].numpoints=idx;
        ret->line=msSmallRealloc(ret->line,ret->line[i].numpoints*sizeof(pointObj));
      }
    }
  } else { /* normal offset (eg. drop shadow) */
    for (i = 0; i < p->numlines; i++) {
      for(j=0; j<p->line[i].numpoints; j++) {
        ret->line[i].point[j].x=p->line[i].point[j].x+offsetx;
        ret->line[i].point[j].y=p->line[i].point[j].y+offsety;
      }
    }
  }

  return ret;
}

/*
-------------------------------------------------------------------------------
 msSetup()

 Contributed by Jerry Pisk in bug 1203.  Heads off potential race condition
 in initializing GD font cache with multiple threads.  Should be called from
 mapscript module initialization code.
-------------------------------------------------------------------------------
*/

int msSetup()
{
#ifdef USE_THREAD
   msThreadInit();
#endif

  /* Use MS_ERRORFILE and MS_DEBUGLEVEL env vars if set */
  if (msDebugInitFromEnv() != MS_SUCCESS)
    return MS_FAILURE;

#ifdef USE_GD_FT
  if (gdFontCacheSetup() != 0) {
    return MS_FAILURE;
   }
#endif

#ifdef USE_GEOS
  msGEOSSetup();
#endif

  return MS_SUCCESS;
}
  
/* This is intended to be a function to cleanup anything that "hangs around"
   when all maps are destroyed, like Registered GDAL drivers, and so forth. */
void msCleanup()
{
  msForceTmpFileBase( NULL );
  msConnPoolFinalCleanup();
  /* Lexer string parsing variable */
  if (msyystring_buffer != NULL)
  {
    msFree(msyystring_buffer);
    msyystring_buffer = NULL;
  }
  msyylex_destroy();

#ifdef USE_OGR
  msOGRCleanup();
#endif    
#ifdef USE_GDAL
  msGDALCleanup();
#endif    
#ifdef USE_PROJ
#  if PJ_VERSION >= 480
  pj_clear_initcache();
#  endif
  pj_deallocate_grids();
  msSetPROJ_LIB( NULL, NULL );
#endif
#if defined(USE_CURL)
  msHTTPCleanup();
#endif

#ifdef USE_GD_FT
  gdFontCacheShutdown(); 
#endif

#ifdef USE_GEOS
  msGEOSCleanup();
#endif
  
  msIO_Cleanup();

  msResetErrorList();

  /* Close/cleanup log/debug output. Keep this at the very end. */
  msDebugCleanup();

  /* Clean up the vtable factory */
  msPluginFreeVirtualTableFactory();
}

/************************************************************************/
/*                            msAlphaBlend()                            */
/*                                                                      */
/*      Function to overlay/blend an RGBA value into an existing        */
/*      RGBA value using the Porter-Duff "over" operator.               */
/*      Primarily intended for use with rasterBufferObj                 */
/*      raster rendering.  The "src" is the overlay value, and "dst"    */
/*      is the existing value being overlaid. dst is expected to be     */
/*      premultiplied, but the source should not be.                    */
/*                                                                      */
/*      NOTE: alpha_dst may be NULL.                                    */
/************************************************************************/

void msAlphaBlend( unsigned char red_src, unsigned char green_src,
                    unsigned char blue_src, unsigned char alpha_src, 
                    unsigned char *red_dst, unsigned char *green_dst,
                    unsigned char *blue_dst, unsigned char *alpha_dst )
{
/* -------------------------------------------------------------------- */
/*      Simple cases we want to handle fast.                            */
/* -------------------------------------------------------------------- */
    if( alpha_src == 0 )
        return;
    
    if( alpha_src == 255 )
    {
        *red_dst = red_src;
        *green_dst = green_src;
        *blue_dst = blue_src;
        if( alpha_dst )
            *alpha_dst = 255;
        return;
    }

/* -------------------------------------------------------------------- */
/*      Premultiple alpha for source values now.                        */
/* -------------------------------------------------------------------- */
    red_src   = red_src * alpha_src / 255;
    green_src = green_src * alpha_src / 255;
    blue_src  = blue_src * alpha_src / 255;

/* -------------------------------------------------------------------- */
/*      Another pretty fast case if there is nothing in the             */
/*      destination to mix with.                                        */
/* -------------------------------------------------------------------- */
    if( alpha_dst && *alpha_dst == 0) {
       *red_dst = red_src;
       *green_dst = green_src;
       *blue_dst = blue_src;
       *alpha_dst = alpha_src;
       return;
    }

/* -------------------------------------------------------------------- */
/*      Cases with actual blending.                                     */
/* -------------------------------------------------------------------- */
    if(!alpha_dst || *alpha_dst == 255) 
    {
        int weight_dst = 256 - alpha_src;

        *red_dst   = (256 * red_src   + *red_dst   * weight_dst) >> 8;
        *green_dst = (256 * green_src + *green_dst * weight_dst) >> 8;
        *blue_dst  = (256 * blue_src  + *blue_dst  * weight_dst) >> 8;
    } 
    else 
    {
        int   weight_dst = (256 - alpha_src);

        *red_dst   = (256 * red_src   + *red_dst   * weight_dst) >> 8;
        *green_dst = (256 * green_src + *green_dst * weight_dst) >> 8;
        *blue_dst  = (256 * blue_src  + *blue_dst  * weight_dst) >> 8;

        *alpha_dst = (256 * alpha_src + *alpha_dst * weight_dst) >> 8;
    }
}

/************************************************************************/
/*                           msAlphaBlendPM()                           */
/*                                                                      */
/*      Same as msAlphaBlend() except that the source RGBA is           */
/*      assumed to already be premultiplied.                            */
/************************************************************************/

void msAlphaBlendPM( unsigned char red_src, unsigned char green_src,
                     unsigned char blue_src, unsigned char alpha_src, 
                     unsigned char *red_dst, unsigned char *green_dst,
                     unsigned char *blue_dst, unsigned char *alpha_dst )
{
/* -------------------------------------------------------------------- */
/*      Simple cases we want to handle fast.                            */
/* -------------------------------------------------------------------- */
    if( alpha_src == 0 )
        return;
    
    if( alpha_src == 255 )
    {
        *red_dst = red_src;
        *green_dst = green_src;
        *blue_dst = blue_src;
        if( alpha_dst )
            *alpha_dst = 255;
        return;
    }

/* -------------------------------------------------------------------- */
/*      Another pretty fast case if there is nothing in the             */
/*      destination to mix with.                                        */
/* -------------------------------------------------------------------- */
    if( alpha_dst && *alpha_dst == 0) {
       *red_dst = red_src;
       *green_dst = green_src;
       *blue_dst = blue_src;
       *alpha_dst = alpha_src;
       return;
    }

/* -------------------------------------------------------------------- */
/*      Cases with actual blending.                                     */
/* -------------------------------------------------------------------- */
    if(!alpha_dst || *alpha_dst == 255) 
    {
        int weight_dst = 256 - alpha_src;

        *red_dst   = (256 * red_src   + *red_dst   * weight_dst) >> 8;
        *green_dst = (256 * green_src + *green_dst * weight_dst) >> 8;
        *blue_dst  = (256 * blue_src  + *blue_dst  * weight_dst) >> 8;
    } 
    else 
    {
        int   weight_dst = (256 - alpha_src);

        *red_dst   = (256 * red_src   + *red_dst   * weight_dst) >> 8;
        *green_dst = (256 * green_src + *green_dst * weight_dst) >> 8;
        *blue_dst  = (256 * blue_src  + *blue_dst  * weight_dst) >> 8;

        *alpha_dst = (256 * alpha_src + *alpha_dst * weight_dst) >> 8;
    }
}

/*
 RFC 24: check if the parent pointer is NULL and raise an error otherwise
*/
int msCheckParentPointer(void* p, char *objname) {
    char* fmt="The %s parent object is null";
    char* msg=NULL;
    if (p == NULL) {
        if(objname != NULL) {
            msg=malloc( sizeof(char) * ( ( strlen(fmt)+strlen(objname) ) ) );
            if(msg == NULL) {
                msg="A required parent object is null";
            } else {
                sprintf(msg, "The %s parent object is null", objname);
            }
        } else {
            msg="A required parent object is null";
        }
        msSetError(MS_NULLPARENTERR, msg, "");
        return MS_FAILURE;
    }
    return MS_SUCCESS;
}

void msBufferInit(bufferObj *buffer) {
    buffer->data=NULL;
    buffer->size=0;
    buffer->available=0;
    buffer->_next_allocation_size = MS_DEFAULT_BUFFER_ALLOC;
}

void msBufferResize(bufferObj *buffer, size_t target_size){
    while(buffer->available <= target_size) {
        buffer->data = msSmallRealloc(buffer->data,buffer->available+buffer->_next_allocation_size);
        buffer->available += buffer->_next_allocation_size;
        buffer->_next_allocation_size *= 2;
    }
}

void msBufferAppend(bufferObj *buffer, void *data, size_t length) {
    if(buffer->available < buffer->size+length) {
        msBufferResize(buffer,buffer->size+length);
    }
    memcpy(&(buffer->data[buffer->size]),data,length);
    buffer->size += length;
}

void msBufferFree(bufferObj *buffer) {
    if(buffer->available>0)
        free(buffer->data);
}


void msFreeRasterBuffer(rasterBufferObj *b) {
    switch(b->type) {
    case MS_BUFFER_BYTE_RGBA:
		msFree(b->data.rgba.pixels);
		b->data.rgba.pixels = NULL;
		break;
    case MS_BUFFER_BYTE_PALETTE:
    	msFree(b->data.palette.pixels);
    	msFree(b->data.palette.palette);
    	b->data.palette.pixels = NULL;
    	b->data.palette.palette = NULL;
    	break;
    case MS_BUFFER_GD:
    	gdImageDestroy(b->data.gd_img);
        b->data.gd_img = NULL;
        break;
    }
}

/*
** Issue #3043: Layer extent comparison short circuit.
**
** msExtentsOverlap()
**
** Returns MS_TRUE if map extent and layer extent overlap, 
** MS_FALSE if they are disjoint, and MS_UNKNOWN if there is 
** not enough info to calculate a deterministic answer.
**
*/
int msExtentsOverlap(mapObj *map, layerObj *layer)
{
    rectObj map_extent;
    rectObj layer_extent;
    int haveOwnProjection = MS_FALSE;
    projectionObj projection;
    const char *value;
    
    /* No extent info? Nothing we can do, return MS_UNKNOWN. */
    /* Otherwise copy extents and leave the originals intact, */
    /* beacuse we will need to transform our rectangles for comparison. */
    if( (map->extent.minx == -1) && (map->extent.miny == -1) && (map->extent.maxx == -1 ) && (map->extent.maxy == -1) ) return MS_UNKNOWN;
    else MS_COPYRECT(&map_extent, &(map->extent) );

    /* For the layer extent, do a second try to read the extent */
    /* from wms_extent or layer data */
    if( (layer->extent.minx == -1) && (layer->extent.miny == -1) && (layer->extent.maxx == -1 ) && (layer->extent.maxy == -1) ) {
      if (msOWSGetLayerExtent(map, layer, "MO", &layer_extent) != MS_SUCCESS) {
        return MS_UNKNOWN;
      }
    }
    else MS_COPYRECT(&layer_extent, &(layer->extent) );
        
#ifdef USE_PROJ

    /* No map projection? Let someone else sort this out. */
    if( ! (map->projection.numargs > 0) ) 
        return MS_UNKNOWN;

    /* No layer projection? Try to get it from wms_srs. */
    if( ! (layer->projection.numargs > 0) ) {
      value = (char*)msOWSGetEPSGProj(NULL, &(layer->metadata), "MO", MS_TRUE);
      if (value != NULL) {
        msInitProjection(&projection);
        haveOwnProjection = MS_TRUE;
        msLoadProjectionStringEPSG(&projection, (char *)value);
      }
    }

    /* Agaian no layer projection? Return MS_UNKNOWN. We can not perform */
    /* naive comparison, because in WMS mode, map->extent is expressed in */
    /* the request SRS. */
    if( ! (layer->projection.numargs > 0) && ! (projection.numargs > 0) ) {
        if ( MS_TRUE == haveOwnProjection ) msFreeProjection(&projection);
        return MS_UNKNOWN;
    }
    
    /* Transform map extents into geographics for comparison. */
    if( msProjectRect(&(map->projection), &(map->latlon), &map_extent) ) {
        if ( MS_TRUE == haveOwnProjection ) msFreeProjection(&projection);
        return MS_UNKNOWN;
    }
        
    /* Transform layer extents into geographics for comparison. */
    /* First case: using layer projection */
    /* Second case: using temporarily read projection from wms_srs */
    if( MS_FALSE == haveOwnProjection ) {
      if( msProjectRect(&(layer->projection), &(map->latlon), &layer_extent) ) 
          return MS_UNKNOWN;
    }
    else {
      if( msProjectRect(&(projection), &(map->latlon), &layer_extent) ) {
          if ( MS_TRUE == haveOwnProjection ) msFreeProjection(&projection);
          return MS_UNKNOWN;
      }
    }

    /* Finally free the possibly created projectionObj agaian */
    if ( MS_TRUE == haveOwnProjection ) msFreeProjection(&projection);

    /* Simple case? Return simple answer. */
    if ( map_extent.minx < map_extent.maxx && layer_extent.minx < layer_extent.maxx )
        return msRectOverlap( &(map_extent), &(layer_extent) );
        
    /* Uh oh, one of the rects crosses the dateline!
    ** Let someone else handle it. */
    return MS_UNKNOWN;
   
#else
    /* No proj? Naive comparison. */
    if( msRectOverlap( &(map_extent), &(layer_extent) ) ) return MS_TRUE;
    return MS_FALSE;
#endif

}

/************************************************************************/
/*                             msSmallMalloc()                          */
/************************************************************************/

/* Safe version of malloc(). This function is taken from gdal/cpl. */

void *msSmallMalloc( size_t nSize )
{
    void        *pReturn;

    if( nSize == 0 )
        return NULL;

    pReturn = malloc( nSize );
    if( pReturn == NULL )
    {
        msIO_fprintf(stderr, "msSmallMalloc(): Out of memory allocating %ld bytes.\n",
                (long) nSize );
        exit(1);
    }

    return pReturn;
}

/************************************************************************/
/*                             msSmallRealloc()                         */
/************************************************************************/

/* Safe version of realloc(). This function is taken from gdal/cpl. */

void * msSmallRealloc( void * pData, size_t nNewSize )
{
    void        *pReturn;

    if ( nNewSize == 0 )
        return NULL;

    pReturn = realloc( pData, nNewSize );

    if( pReturn == NULL )
    {
        msIO_fprintf(stderr, "msSmallRealloc(): Out of memory allocating %ld bytes.\n",
                (long)nNewSize );
        exit(1);
    }

    return pReturn;
}

/************************************************************************/
/*                             msSmallCalloc()                         */
/************************************************************************/

/* Safe version of calloc(). This function is taken from gdal/cpl. */

void *msSmallCalloc( size_t nCount, size_t nSize )
{
    void  *pReturn;

    if( nSize * nCount == 0 )
        return NULL;
    
    pReturn = calloc( nCount, nSize );
    if( pReturn == NULL )
    {
        msIO_fprintf(stderr, "msSmallCalloc(): Out of memory allocating %ld bytes.\n",
                (long)(nCount*nSize));
        exit(1);
    }

    return pReturn;
}

/*
** msBuildOnlineResource()
**
** Try to build the online resource (mapserv URL) for this service.
** "http://$(SERVER_NAME):$(SERVER_PORT)$(SCRIPT_NAME)?"
** (+append the map=... param if it was explicitly passed in QUERY_STRING)
**
** Returns a newly allocated string that should be freed by the caller or
** NULL in case of error.
*/
char *msBuildOnlineResource(mapObj *map, cgiRequestObj *req)
{
    char *online_resource = NULL;
    const char *value, *hostname, *port, *script, *protocol="http", *mapparam=NULL;
    int mapparam_len = 0;

    hostname = getenv("SERVER_NAME");
    port = getenv("SERVER_PORT");
    script = getenv("SCRIPT_NAME");

    /* HTTPS is set by Apache to "on" in an HTTPS server ... if not set */
    /* then check SERVER_PORT: 443 is the default https port. */
    if ( ((value=getenv("HTTPS")) && strcasecmp(value, "on") == 0) ||
	 ((value=getenv("SERVER_PORT")) && atoi(value) == 443) )
    {
        protocol = "https";
    }

    /* If map=.. was explicitly set then we'll include it in onlineresource
     */
    if (req->type == MS_GET_REQUEST)
    {
        int i;
        for(i=0; i<req->NumParams; i++)
        {
            if (strcasecmp(req->ParamNames[i], "map") == 0)
            {
                mapparam = req->ParamValues[i];
                mapparam_len = strlen(mapparam)+5; /* +5 for "map="+"&" */
                break;
            }
        }
    }

    if (hostname && port && script) 
    {
        size_t buffer_size;
        buffer_size = strlen(hostname)+strlen(port)+strlen(script)+mapparam_len+10;
        online_resource = (char*)msSmallMalloc(buffer_size);
        if ((atoi(port) == 80 && strcmp(protocol, "http") == 0) ||
            (atoi(port) == 443 && strcmp(protocol, "https") == 0) )
          snprintf(online_resource, buffer_size, "%s://%s%s?", protocol, hostname, script);
        else
          snprintf(online_resource, buffer_size, "%s://%s:%s%s?", protocol, hostname, port, script);

        if (mapparam)
        {
            int baselen;
            baselen = strlen(online_resource);
            snprintf(online_resource+baselen, buffer_size-baselen, "map=%s&", mapparam);
        }
    }
    else 
    {
        msSetError(MS_CGIERR, "Impossible to establish server URL.", "msBuildOnlineResource()");
        return NULL;
    }

    return online_resource;
}


/************************************************************************/
/*                             msIntegerInArray()                        */
/************************************************************************/

/* Check if a integer is in a array */
int msIntegerInArray(const int value, int *array, int numelements)
{
    int i;
    for (i=0;i<numelements;++i) {
        if (value == array[i])
            return MS_TRUE;
    }
    return MS_FALSE;
}


/************************************************************************
 *                            msMapSetProjections                       *
 *                                                                      * 
 *   Ensure that all the layers in the map file have a projection       *
 *   by copying the map-level projection to all layers than have no     *
 *   projection.                                                        *
 ************************************************************************/

int msMapSetLayerProjections(mapObj* map) {

  char *mapProjStr = NULL;
  int i;
    
  if (map->projection.numargs <= 0) {
    msSetError(MS_WMSERR, "Cannot set new SRS on a map that doesn't "
                          "have any projection set. Please make sure your mapfile "
                          "has a PROJECTION defined at the top level.", 
                          "msTileSetProjectionst()");
    return(MS_FAILURE);
  }

  for(i=0; i<map->numlayers; i++) {
    /* This layer is turned on and needs a projection? */
    if (GET_LAYER(map, i)->projection.numargs <= 0 &&
        GET_LAYER(map, i)->status != MS_OFF &&
        GET_LAYER(map, i)->transform == MS_TRUE) {
   
      /* Fetch main map projection string only now that we need it */
      if (mapProjStr == NULL)
        mapProjStr = msGetProjectionString(&(map->projection));
      
      /* Set the projection to the map file projection */  
      if (msLoadProjectionString(&(GET_LAYER(map, i)->projection), mapProjStr) != 0) {
        msSetError(MS_CGIERR, "Unable to set projection on layer.", "msTileSetProjectionst()");
        return(MS_FAILURE);
      }
      GET_LAYER(map, i)->project = MS_TRUE;
    }
  }
  msFree(mapProjStr);
  return(MS_SUCCESS);
}
