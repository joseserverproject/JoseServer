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

#ifndef JS_OS_H_
#define JS_OS_H_


#if (JS_CONFIG_OS==JS_CONFIG_OS_LINUX || JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <netinet/tcp.h>
#if (JS_CONFIG_OS!=JS_CONFIG_OS_ANDROID)
#include <linux/types.h>
#include <stdint.h>
#endif

typedef unsigned long long UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef unsigned int JSUINT;
typedef UINT64 HTTPSIZE_T;
typedef fd_set JS_FD_T;
typedef int    JS_SOCKET_T;
#define JS_FD_SET	FD_SET
#define JS_FD_ZERO	FD_ZERO
#define JS_FD_ISSET	FD_ISSET
#define JS_FD_CLR	FD_CLR
#define JS_STRCPY	strcpy
#define JS_STRPRINTF	JS_UTIL_StrPrint
#define JS_STRTOULL		strtoull
#elif (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

typedef UINT64 HTTPSIZE_T;
typedef fd_set JS_FD_T;
typedef unsigned int JSUINT;
typedef SOCKET    JS_SOCKET_T;

#define JS_FD_SET	FD_SET
#define JS_FD_CLR	FD_CLR

//#define JS_FD_SET	JS_UTIL_FdSet
//#define JS_FD_CLR	JS_UTIL_FdClr

#define JS_FD_ZERO	FD_ZERO
#define JS_FD_ISSET	FD_ISSET

#define JS_STRCPY	JS_UTIL_StrCpy
#define JS_STRPRINTF	JS_UTIL_StrPrint
#define JS_STRTOULL		_strtoui64
#define MSG_NOSIGNAL  0
#endif

#endif /* JS_OS_H_ */
