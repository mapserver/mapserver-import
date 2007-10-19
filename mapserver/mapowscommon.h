/******************************************************************************
 *
 * Name:     mapowscommon.h
 * Project:  MapServer
 * Purpose:  OGC OWS Common Implementation include file
 * Author:   Tom Kralidis (tomkralidis@hotmail.com)
 *
 ******************************************************************************
 * Copyright (c) 2006, Tom Kralidis
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

#ifndef MAPOWSCOMMON_H
#define MAPOWSCOMMON_H

#ifdef USE_SOS_SVR

#include<libxml/parser.h>
#include<libxml/tree.h>

/* W3C namespaces */

#define MS_OWSCOMMON_W3C_XLINK_NAMESPACE_URI      "http://www.w3.org/1999/xlink"
#define MS_OWSCOMMON_W3C_XLINK_NAMESPACE_PREFIX   "xlink"

#define MS_OWSCOMMON_W3C_XSI_NAMESPACE_URI        "http://www.w3.org/2001/XMLSchema-instance"
#define MS_OWSCOMMON_W3C_XSI_NAMESPACE_PREFIX     "xsi"

#define MS_OWSCOMMON_W3C_XS_NAMESPACE_URI         "http://www.w3.org/2001/XMLSchema"
#define MS_OWSCOMMON_W3C_XS_NAMESPACE_PREFIX      "xs"

/* OGC namespaces */

#define MS_OWSCOMMON_OGC_NAMESPACE_URI      "http://www.opengis.net/ogc"
#define MS_OWSCOMMON_OGC_NAMESPACE_PREFIX   "ogc"

#define MS_OWSCOMMON_OWS_NAMESPACE_URI      "http://www.opengis.net/ows"
#define MS_OWSCOMMON_OWS_NAMESPACE_PREFIX   "ows"

/* OGC URNs */

#define MS_OWSCOMMON_URN_OGC_CRS_4326 "urn:opengis:def:crs:OGC:2:84"

/* default OGC Schemas Location */

#define MS_OWSCOMMON_SCHEMAS_LOCATION "http://schemas.opengis.net"

/* OGC codespace */

#define MS_OWSCOMMON_OGC_CODESPACE "OGC"

/* function prototypes */

xmlNodePtr msOWSCommonServiceIdentification(mapObj *map, const char *servicetype, const char *version);

xmlNodePtr msOWSCommonServiceProvider(mapObj *map);

xmlNodePtr msOWSCommonOperationsMetadata();

xmlNodePtr msOWSCommonOperationsMetadataOperation(char *name, int method, char *url);

xmlNodePtr msOWSCommonOperationsMetadataDomainType(char *elname, char *name, char *values);

xmlNodePtr msOWSCommonExceptionReport(const char *schemas_location, const char *version, const char *language, const char *exceptionCode, const char *locator, const char *ExceptionText);

xmlNodePtr msOWSCommonBoundingBox(const char *crs, int dimensions, double minx, double miny, double maxx, double maxy);

xmlNodePtr msOWSCommonWGS84BoundingBox(int dimensions, double minx, double miny, double maxx, double maxy);

#endif /* defined(USE_SOS_SVR) */

#endif /* MAPOWSCOMMON_H */
