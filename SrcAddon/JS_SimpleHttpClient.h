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

#ifndef JS_SIMPLEHTTPCLIENT_H_
#define JS_SIMPLEHTTPCLIENT_H_

#define JS_HTTPCLIENT_STATUS_ZERO			0
#define JS_HTTPCLIENT_STATUS_RESOLVING		1
#define JS_HTTPCLIENT_STATUS_CONNECTING		2
#define JS_HTTPCLIENT_STATUS_SENDREQ		3
#define JS_HTTPCLIENT_STATUS_RCVHEADER		4
#define JS_HTTPCLIENT_STATUS_RCVBODY		5
#define JS_HTTPCLIENT_STATUS_IDLE			6

#define JS_RET_CRITICAL_ERROR	(-999)
#define JS_HTTP_RET_CONNECTED	1
#define JS_HTTP_RET_RCVHEADER	2
#define JS_HTTP_RET_RCVBODY		3
#define JS_HTTP_RET_EOFMASK		0x1000

#define JS_HTTPRET_CHECKEOF(x)  (x&JS_HTTP_RET_EOFMASK)
#define JS_HTTPRET_CHECKRET(x)   (x&(~JS_HTTP_RET_EOFMASK))

typedef int (* JS_FT_HTTPCLIENT_CALLBACK) (JS_HANDLE hOwner, char * pData, unsigned int nBytes);

JS_HANDLE JS_SimpleHttpClient_CreateConnectionPool(int nLifeTimeMs);
void JS_SimpleHttpClient_DestroyConnectionPool(JS_HANDLE hPool);

///////////////////////////////////////////////////////////////////////
////non blocking httpclient functions
JS_HANDLE JS_SimpleHttpClient_GetConnectionByURL(const char * strURL, const char * strData, const char * strMethod);
JS_HANDLE JS_SimpleHttpClient_GetConnectionByReq(JS_HTTP_Request	* pReq);
void JS_SimpleHttpClient_Reset(JS_HANDLE hHttpClient, int nNeedNewConnection);
void JS_SimpleHttpClient_ReturnConnection(JS_HANDLE hClient);

///////////////////////////////////////////////////////////////////////
////main nonblocking function according to the fdset event and its state
int JS_SimpleHttpClient_DoSomething(JS_HANDLE hClient, JS_HTTP_Response ** ppRsp, char * pDataBuffer, int *pnBuffSize, JS_FD_T * pRDSet, JS_FD_T * pWRSet);

////prepare redirect option before call doit function when rsp status is 3xx
int JS_SimpleHttpClient_PrepareRedirect(JS_HANDLE hClient);

////functions for properties
int JS_SimpleHttpClient_SetOwner(JS_HANDLE hClient, JS_HANDLE hOwner, JS_HANDLE hMutexForFDSet, JS_FD_T * pOrgRDSet, JS_FD_T * pOrgWRSet, int * pnMaxFd);
int JS_SimpleHttpClient_SetRsp(JS_HANDLE hClient, JS_HTTP_Response	* pRsp);
int JS_SimpleHttpClient_SetRange(JS_HANDLE hClient, HTTPSIZE_T	nRangeStart, HTTPSIZE_T	 nRangeLen);
int JS_SimpleHttpClient_SetChunkedBypass(JS_HANDLE hClient, int nEnable);
int JS_SimpleHttpClient_SetConnectTimeout(JS_HANDLE hClient, int nConnectTimeoutMsec);

int JS_SimpleHttpClient_CheckRetryCount(JS_HANDLE hClient);
int JS_SimpleHttpClient_IsIdleState(JS_HANDLE hHttpClient);

int JS_SimpleHttpClient_GetError(JS_HANDLE hHttpClient);
int JS_SimpleHttpClient_GetStatus(JS_HANDLE hHttpClient);
HTTPSIZE_T JS_SimpleHttpClient_GetRangeLen(JS_HANDLE hClient);
HTTPSIZE_T JS_SimpleHttpClient_GetSentSize(JS_HANDLE hClient);
JS_HTTP_Response * JS_SimpleHttpClient_GetRsp(JS_HANDLE hHttpClient);
JS_HTTP_Request  * JS_SimpleHttpClient_GetReq(JS_HANDLE hHttpClient);
JS_SOCKET_T JS_SimpleHttpClient_GetSocket(JS_HANDLE hHttpClient);
UINT JS_SimpleHttpClient_GetHostIP(JS_HANDLE hHttpClient);

int JS_DNSCache_Resolve(char * strHost, UINT32 * pnIP);
void JS_DNSCache_ReportError(const char * strHost);
int JS_DNSCache_IsBusy(void);

#endif /* JS_SIMPLEHTTPCLIENT_H_ */
