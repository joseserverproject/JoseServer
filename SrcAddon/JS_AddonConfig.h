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

#ifndef JS_ADDONCONFIG_H
#define JS_ADDONCONFIG_H

#define JS_CONFIG_USE_ADDON_MEDIAPROXY
#define JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
//#define JS_CONFIG_USE_ADDON_AJAXHELPER
#define JS_CONFIG_USE_TURBOGATE						1

#if (JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
#define JS_CONFIG_MAX_DOWNLOADWORKS		4
#else
#define JS_CONFIG_MAX_DOWNLOADWORKS		16
#endif

#define JS_CONFIG_MAX_HTTPCONNECTIONPOOL	64
#define JS_CONFIG_MAX_TURBOITEM				8
#define JS_CONFIG_MAX_TURBOCONNECTION		18
#define JS_CONFIG_MAX_STATUS_CHANGE			10
#define JS_CONFIG_MAX_QRATE					90

#define JS_CONFIG_MAX_BURSTCOUNT		10

#define JS_CONFIG_NORMAL_TURBOCONNECTION	10

////min configuration
#define JS_CONFIG_MIN_TURBOVIDEOSIZE		(1024*1024)
#define JS_CONFIG_MIN_BIGFILE				(2*1024*1024)
#define JS_CONFIG_MIN_QRATE					10

#define JS_CONFIG_TIME_MSEC_SPEEDCHECK		1000

#define JS_CONFIG_TIMOUT_HTTPCONNECT		15000

////queue configuration
#define JS_CONFIG_MAX_MULTIQUEUEITEM		15
#define JS_CONFIG_QUEUEITEM_MIN_SIZE		(512*1024)
#define JS_CONFIG_QUEUEITEM_MAX_SIZE		(1*1024*1024)
#define JS_CONFIG_QUEUEITEM_GROWING_SIZE	(250*1024)

#endif /* JS_ADDONCONFIG_H */
