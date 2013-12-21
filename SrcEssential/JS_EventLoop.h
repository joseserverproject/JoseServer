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

#ifndef _JS_SERVERLOOP_H_
#define _JS_SERVERLOOP_H_

#define JS_IO_READY		0
#define JS_IO_BLOCK		1
#define JS_IO_EXIT		2

#define JS_DATAID_NULL	0xFF

typedef struct  JS_EventLoopTag
{
	int nIOIndex;
	int nStatus;
	void * pParent;
	void * pOwner;
	void * pUserData;
	int nInputNum;
	int nOutputNum;
	int nWaitMs;
	JS_SOCKET_T nMaxFd;
	JS_HANDLE hMutexForFDSet;
	JS_FD_T	* pReadFdSet;
	JS_FD_T	* pWriteFdSet;
	JS_HANDLE hIOPool;
	JS_HANDLE hIOThread;
	unsigned int nThreadID;
	JS_HANDLE hEvent;
}JS_EventLoop;

typedef JS_SOCKET_T (*JS_FT_SESSION_GETSOCK_CALLBACK)(JS_POOL_ITEM_T * pPoolItem);
typedef int (*JS_FT_SESSION_EVENT_CALLBACK)(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_FD_T * pRDSet, JS_FD_T * pWRSet);
typedef int (*JS_FT_SESSION_NEW_CALLBACK)(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_SOCKET_T nInSocket);
typedef int (*JS_FT_SESSION_TRANSFER_CALLBACK)(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem,  JS_SOCKET_T nInSocket, JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp);

typedef struct JS_EventLoopHandlerTag 
{
	int nDataID;
	unsigned int nPoolItemSize;
	JS_FT_SESSION_GETSOCK_CALLBACK pfGetSock;
	JS_FT_SESSION_EVENT_CALLBACK pfDoIO;
	JS_FT_SESSION_NEW_CALLBACK pfAddIO;
	JS_FT_SESSION_TRANSFER_CALLBACK pfTransferIO;
	JS_FT_PHASECHANGE_CALLBACK	pfPhase;
}JS_EventLoopHandler;

int JS_EventLoop_AddThread(JS_POOL_ITEM_T * pPoolItem);
int JS_EventLoop_DelThread(JS_POOL_ITEM_T * pPoolItem);
void JS_EventLoop_SetPollWaitTime(JS_EventLoop * pIO, int nWaitMs);
int JS_EventLoop_SetOutputFd(JS_EventLoop * pIO, JS_SOCKET_T nSock, int nEnable, int nLock);
int JS_EventLoop_SetInputFd(JS_EventLoop * pIO, JS_SOCKET_T nSock, int nEnable, int nLock);
int JS_EventLoop_IsBusy(JS_HANDLE hServer);
int JS_EventLoop_TransferSessionItemToOtherHandler(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem,  JS_SOCKET_T nInSocket, JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp, int nDataID);

JS_HANDLE JS_EventLoop_PrepareServerLoop(JS_HANDLE hJose, int nIOThreadNum);
int JS_EventLoop_RegisterHandler(JS_HANDLE hServer, JS_EventLoopHandler * pEventHandler, int nIsDefaultHandler);


int JS_EventLoop_StartServerLoop(JS_HANDLE hServer,unsigned short nDefaultPort, int nIsAutoPort);
int JS_EventLoop_StopServerLoop(JS_HANDLE hServer);
int JS_EventLoop_DestroyServerLoop(JS_HANDLE hServer);
unsigned short JS_EventLoop_GetMyPort(JS_HANDLE hServer);

#endif