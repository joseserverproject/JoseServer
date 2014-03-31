/*
Copyright (c) <2013> <joseprojectteam>

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef JS_ADDONINTERNAL_H
#define JS_ADDONINTERNAL_H

#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
#include "JS_MediaProxy.h"
#include "JS_MediaProxyTurbo.h"
#include "JS_DataStructure_Multiqueue.h"
#endif

#ifdef JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
#include "JS_SimpleHttpClient.h"
#endif

#ifdef JS_CONFIG_USE_ADDON_SIMPLEDISCCOVERY
#include "JS_SimpleDiscovery.h"
#endif

#ifdef JS_CONFIG_USE_ADDON_SIMPLECACHE
#include "JS_Cache.h"
#endif

#ifdef JS_CONFIG_USE_ADDON_AJAXHELPER
#include "JS_AjaxHelper.h"
#endif

#endif /* JS_ADDONINTERNAL_H */
