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

#ifndef _JS_HTTPSERVER_H_
#define _JS_HTTPSERVER_H_

#define JS_HTTPAPI_CMD_ASYNCDONE			1
#define JS_HTTPAPI_CMD_GETVARIABLE			2
#define JS_HTTPAPI_CMD_GETFILELIST			3
#define JS_HTTPAPI_CMD_GETPEERIP			4
#define JS_HTTPAPI_CMD_SENDHEADERRAW		5
#define JS_HTTPAPI_CMD_SENDBODYRAW			6
#define JS_HTTPAPI_CMD_SENDTEXTRSP			7
#define JS_HTTPAPI_CMD_SENDXMLRSP			8
#define JS_HTTPAPI_CMD_SENDJSONRSP			9
#define JS_HTTPAPI_CMD_SENDERRORRSP			10
#define JS_HTTPAPI_CMD_SENDFILE				11
#define JS_HTTPAPI_CMD_SETDOWNLOADABLE		12
int JS_HttpServer_DoAPICommand(JS_HANDLE hSession, int nCommand, int nIntParam, const char * strParam, int nParamSize, char * pRetBuffer, int nRetBuffSize);

JS_EventLoopHandler * JS_HttpServer_GetEventHandler(void);
JS_HANDLE JS_HttpServer_Create(JS_HANDLE hJose, unsigned short nPort, int nIsAutoPort);
int JS_HttpServer_Destroy(JS_HANDLE hServer);
unsigned short JS_HttpServer_WhatIsMyPort(JS_HANDLE hServer);


#endif