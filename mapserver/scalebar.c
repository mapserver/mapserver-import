/******************************************************************************
 *
 * Project:  MapServer
 * Purpose:  Commandline scalebar generating utility, mostly for testing.
 * Author:   Steve Lime and the MapServer team.
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
 ******************************************************************************
 *
 * $Log$
 * Revision 1.11  2005/06/14 16:03:35  dan
 * Updated copyright date to 2005
 *
 * Revision 1.10  2004/10/21 04:30:54  frank
 * Added standardized headers.  Added MS_CVSID().
 *
 */

#include "map.h"

MS_CVSID("$Id$")

int main(int argc, char *argv[])
{
  mapObj *map=NULL;
  imageObj         *image = NULL;

  if(argc > 1 && strcmp(argv[1], "-v") == 0) {
    printf("%s\n", msGetVersion());
    exit(0);
  }

  /* ---- check the number of arguments, return syntax if not correct ---- */
  if( argc < 3 ) {
      fprintf(stdout,"Syntax: scalebar [mapfile] [output image]\n" );
      exit(0);
  }

  map = msLoadMap(argv[1], NULL);
  if(!map) { 
    msWriteError(stderr);
    exit(0);
  }

  image = msDrawScalebar(map);

  if(!image) { 
    msWriteError(stderr);
    exit(0);
  }

  msSaveImage(map, image, argv[2]);
  msFreeImage(image);

  msFreeMap(map);  
  return(MS_TRUE);
}
