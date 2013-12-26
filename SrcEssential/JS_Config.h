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

#ifndef JS_CONFIG_H_
#define JS_CONFIG_H_

#define JS_CONFIG_USERAGENT		"Jose 1.0"
////os type
#define JS_CONFIG_OS_LINUX		1
#define JS_CONFIG_OS_WIN32		2
#define JS_CONFIG_OS_ANDROID	3
#define JS_CONFIG_OS_IOS		4
#define JS_CONFIG_OS_MAC		5

#ifndef JS_CONFIG_OS
#if defined(_WIN32) || defined(_WIN64)
#define JS_CONFIG_OS	JS_CONFIG_OS_WIN32
#define JS_CONFIG_OS	JS_CONFIG_OS_WIN32
#elif defined(ANDROID) || defined(__ANDROID__)
#define JS_CONFIG_OS	JS_CONFIG_OS_ANDROID
#else
#define JS_CONFIG_OS	JS_CONFIG_OS_LINUX
#endif
#endif

#define JS_DATAID_HTTPSERVER	0
#define JS_DATAID_PROXY			1

#define JS_CONFIG_PORT_FRONTGATE			9861
#define JS_CONFIG_PORT_HTTPSERVER			8999
#define JS_CONFIG_PORT_SIMPLEEVENT			10131

////max configurations
#define JS_CONFIG_MAXURL		8000
#define JS_CONFIG_MAXBUFFSIZE	34000
#define JS_CONFIG_MAXREADSIZE	32000
#define JS_CONFIG_MAXSENDSIZE	JS_CONFIG_MAXREADSIZE

#define JS_CONFIG_MAXHTTPSTRING				4000
#define JS_CONFIG_MAX_SMALLURL				2000
#define JS_CONFIG_MAX_SMALLPATH				2000
#define JS_CONFIG_MAX_SMALLFILENAME			512

#define JS_CONFIG_MAX_RECVZERORET			2
#define JS_CONFIG_MAX_PROXYRETRY			20
#define JS_CONFIG_MAX_IOTHREAD				1

#define JS_CONFIG_MAX_MIME_TYPE					64
#define JS_CONFIG_MAX_INTERMAP					32
#define JS_CONFIG_MAX_GETMETHOD					(8*1024)
#define JS_CONFIG_MAX_SERVERMUTEX				64
#define JS_CONFIG_MAX_ARRAYSIZEFORIOTHREAD		64
#define JS_CONFIG_MAX_POST_DATA					(2*1024*1024)
#define JS_CONFIG_MAX_QUEUESIZE					(4*1024*1024)
#define JS_CONFIG_MAX_HANDLER					8
#define JS_CONFIG_MAX_DIRECTAPI_WORKS			1
#define JS_CONFIG_MAX_REDIRECT					20
#define JS_CONFIG_MAX_DNSERRORCOUNT				4

////normal configuration
#define JS_CONFIG_NORMAL_STACKSIZE				(800*1024)
#define JS_CONFIG_NORMAL_SENDBLOCKSIZE			(128*1024)
#define JS_CONFIG_NORMAL_HASHSIZE				64
#define JS_CONFIG_NORMAL_DIRECTAPI				32
#define JS_CONFIG_NORMAL_MIMEMAP				64
#define JS_CONFIG_NORMAL_READSIZE				(4*1024)
#define JS_CONFIG_NORMAL_DNSMAP					200
#define JS_CONFIG_NORMAL_DNSCACHE				256

////timing configuration
#define JS_CONFIG_TIME_MSEC_LIFEHTTP				45000
#define JS_CONFIG_TIME_USEC_TASKWAIT				20000
#define JS_CONFIG_TIME_USEC_MINSLEEP				10000
#define JS_CONFIG_TIME_MSEC_POLL					100
#define JS_CONFIG_MAX_MEDIAPROXYKEEPCNT				(5000/JS_CONFIG_TIME_MSEC_POLL)

#ifndef JS_CONFIG_USE_ADDON
#define JS_CONFIG_USE_ADDON				1
#endif
#define JS_CONFIG_USE_PROXYAGENTASJOSE	0
#define JS_CONFIG_DEBUGMEMORY	0
#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonConfig.h"
#endif
#endif /* JS_CONFIG_H_ */
