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

/*********************************************************************
Base class for TCP server: there are two kinds of threads (listenthread, iothread).
A listenthread listens TCP port and handover client connection to the iothread.
There can be one or more Iothreads which process client sockets event by polling method.
All event handler function should not blcok iothread during process the socket event (such as recv).
*********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#include "JS_EventLoop.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////////
//macro start
#define JS_SERVERLOOP_CHECK_INTVAL_MS	10000
#define	JS_SERVERLOOP_STATUS_ZERO	0
#define	JS_SERVERLOOP_STATUS_READY	1
#define	JS_SERVERLOOP_STATUS_INLOOP	2
#define	JS_SERVERLOOP_STATUS_EXIT	3

//////////////////////////////////////////////////////
//local type start
typedef struct   JS_ServerLoopTag
{
	JS_HANDLE hJose;
	int nStatus;
	int nIOThreadNum;
	int nNeedToExit;
	JS_SOCKET_T nServerSocket;
	unsigned short nServerPort;
	int nRRcounter;
	int nMaxFd;
	unsigned int nThreadID;
	unsigned int nMaxPoolItemSize;
	JS_HANDLE hListenThread;
	JS_EventLoopHandler * pDefaultHandler;
	JS_EventLoopHandler arrHandler[JS_CONFIG_MAX_HANDLER+4];
	JS_EventLoop	arrIOThreadPool[JS_CONFIG_MAX_ARRAYSIZEFORIOTHREAD+4];
}JS_ServerLoop;

///////////////////////////////////////////////
static void * _JS_EventLoop_ListenThread_(void * pParam);
static void * _JS_EventLoop_IOThread_(void * pParam);

static int JS_EventLoop_Pool_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase);
static int JS_EventLoop_DoIO(JS_EventLoop * pIO , JS_FD_T * pRDSet, JS_FD_T * pWRSet);
static int JS_EventLoop_PrepareIOLoop(JS_ServerLoop * pServer);
static int JS_EventLoop_ClearIOLoop(JS_ServerLoop * pServer);
static int JS_EventLoop_StopIOThreads(JS_ServerLoop * pServer);
static int JS_EventLoop_StartIOThreads(JS_ServerLoop * pServer);
////server loop functions impl

int JS_EventLoop_IsBusy(JS_HANDLE hServer)
{
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	if(pServer==NULL)
		return 0;
	if(pServer->nStatus == JS_SERVERLOOP_STATUS_INLOOP)
		return 1;
	else
		return 0;
}

JS_HANDLE JS_EventLoop_PrepareServerLoop(JS_HANDLE hJose, int nIOThreadNum)
{
	int nRet = 0;
	JS_ServerLoop * pServer = NULL;
	JS_EventLoop * pIO = NULL;

	if(hJose==NULL || nIOThreadNum==0)
		return NULL;
	if(nIOThreadNum>=JS_CONFIG_MAX_ARRAYSIZEFORIOTHREAD) {
		DBGPRINT("create loop: error too many io threads (max=%u,request=%u)\n",JS_CONFIG_MAX_ARRAYSIZEFORIOTHREAD,nIOThreadNum);
		nIOThreadNum = JS_CONFIG_MAX_ARRAYSIZEFORIOTHREAD;
	}
	pServer = (JS_ServerLoop *)JS_ALLOC(sizeof(JS_ServerLoop));
	if(pServer==NULL) {
		DBGPRINT("server loop create fail mem error\n");
		return NULL;
	}
	memset((char*)pServer,0,sizeof(JS_ServerLoop));
	pServer->nIOThreadNum = nIOThreadNum;
	pServer->hJose = hJose;
	if(nRet<0) {
		JS_EventLoop_DestroyServerLoop((JS_HANDLE)pServer);
		pServer = NULL;
	}else {
		pServer->nStatus = JS_SERVERLOOP_STATUS_READY;
	}
	return (JS_HANDLE)pServer;
}

int JS_EventLoop_RegisterHandler(JS_HANDLE hServer, JS_EventLoopHandler * pEventHandler, int nIsDefaultHandler)
{
	int nIndex;
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	if(pServer==NULL || pEventHandler==NULL)
		return -1;
	nIndex = pEventHandler->nDataID;
	if(pServer->nStatus != JS_SERVERLOOP_STATUS_READY) {
		DBGPRINT("register handler: wrong server status error data id %u\n",nIndex);
		return -1;
	}
	if(nIndex<0 || nIndex>=JS_CONFIG_MAX_HANDLER) {
		DBGPRINT("register handler: error data id is wrong %u\n",nIndex);
		return -1;
	}
	if(pServer->arrHandler[nIndex].pfDoIO != NULL){
		DBGPRINT("register handler: error already registered for data id= %u\n",nIndex);
		return -1;
	}
	if(nIsDefaultHandler && pServer->pDefaultHandler) {
		DBGPRINT("register handler: error already default handler registered for data id= %u\n",pServer->pDefaultHandler->nDataID);
		return -1;
	}
	pServer->arrHandler[nIndex] = *pEventHandler;
	if(pEventHandler->nPoolItemSize > pServer->nMaxPoolItemSize) {
		pServer->nMaxPoolItemSize = pEventHandler->nPoolItemSize;
	}
	if(nIsDefaultHandler)
		pServer->pDefaultHandler = pEventHandler;
	return 0;
}

int JS_EventLoop_TransferSessionItemToOtherHandler(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem,  JS_SOCKET_T nInSocket, JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp, int nDataID)
{
	JS_ServerLoop * pServer;
	if(pIO==NULL || pPoolItem==NULL)
		return -1;
	pServer = (JS_ServerLoop *)pIO->pParent;
	if(pServer==NULL)
		return -1;
	if(nDataID<0 || nDataID>=JS_CONFIG_MAX_HANDLER) {
		DBGPRINT("transfer handler: error data id is wrong %u (max=%u)\n",nDataID,JS_CONFIG_MAX_HANDLER);
		return -1;
	}
	if(pServer->arrHandler[nDataID].pfTransferIO==NULL) {
		DBGPRINT("transfer handler: error no handover func %u (max=%u)\n",nDataID,JS_CONFIG_MAX_HANDLER);
		return -1;
	}
	return pServer->arrHandler[nDataID].pfTransferIO(pIO,pPoolItem,nInSocket, pReq,pRsp);
}

int JS_EventLoop_StartServerLoop(JS_HANDLE hServer,unsigned short nDefaultPort, int nIsAutoPort)
{
	JS_SOCKET_T nTmpSock=0;
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	int nRet = 0;
    struct sockaddr_in rcSrvAddr;
    unsigned int nOpt;
    int nRpt=0;
	JS_EventLoop * pIO = NULL;
	if(pServer==NULL)
		return -1;
	if(pServer->nMaxPoolItemSize<=0) {
		DBGPRINT("start server: can't start cause no event handler is registered\n");
		return -1;
	}
	if(pServer->pDefaultHandler==NULL) {
		DBGPRINT("start server: can't start cause there is no default handler\n");
		return -1;
	}
	if(JS_EventLoop_PrepareIOLoop(pServer)<0) {
		DBGPRINT("start server: can't make ioloop \n");
		return -1;
	}
	////bind socket first
	nTmpSock = socket(AF_INET, SOCK_STREAM, 0);
	if(JS_UTIL_CheckSocketValidity(nTmpSock)<0) {
		DBGPRINT("start server: Can't make server socket!\n");
		return -1;
	}
	nOpt = 1;
	setsockopt(nTmpSock, SOL_SOCKET, SO_REUSEADDR, (char*)&nOpt, sizeof(nOpt));
LABEL_BEGIN_BINDING:
	memset((char*)&rcSrvAddr, 0, sizeof(rcSrvAddr));
	rcSrvAddr.sin_family = AF_INET;
	rcSrvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	rcSrvAddr.sin_port = htons(nDefaultPort);
	nRet = bind(nTmpSock, (struct sockaddr *)&rcSrvAddr, sizeof(rcSrvAddr));
	if (nRet<0) {
		DBGPRINT("start server: Listen Server bind error %u@!\n",nDefaultPort);
		if(pServer->nNeedToExit)
			goto START_SERVER_EXIT;
		if(nIsAutoPort) {
			nDefaultPort++;
			nRpt++;
			DBGPRINT("start server: try next port=%u,%u\n",nDefaultPort,nRpt);
			goto LABEL_BEGIN_BINDING;
		}else
			goto START_SERVER_EXIT;
	}
	pServer->nServerPort = nDefaultPort;
	pServer->nServerSocket = nTmpSock;
	DBGPRINT("start server: port=%u ok\n",nDefaultPort);
	////second, make listen thread
	pServer->nThreadID = JS_ThreadPool_StartThread(&pServer->hListenThread, _JS_EventLoop_ListenThread_,hServer);
	if(pServer->hListenThread)
		return 0;
	else {
		DBGPRINT("start server: can't make thread!\n");
	}
START_SERVER_EXIT:
	JS_UTIL_SocketClose(nTmpSock);
	pServer->nServerSocket = 0;
	return nRet;
}

int JS_EventLoop_StopServerLoop(JS_HANDLE hServer)
{
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	if(pServer==NULL)
		return -1;
	pServer->nNeedToExit = 1;
	if(JS_UTIL_CheckSocketValidity(pServer->nServerSocket)>=0)
		JS_UTIL_SocketClose(pServer->nServerSocket);
	pServer->nServerSocket = 0;
	JS_ThreadPool_WaitForEndOfThread(pServer->hListenThread,60000,pServer->nThreadID);
	DBGPRINT("JS_EventLoop_StopServerLoop: Check Exited!\n");
	return 0;
}

int JS_EventLoop_DestroyServerLoop(JS_HANDLE hServer)
{
	JS_EventLoop * pIO = NULL;
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	if(pServer) {
		JS_EventLoop_ClearIOLoop(pServer);
		JS_FREE(pServer);
	}
	return 0;
}

unsigned short JS_EventLoop_GetMyPort(JS_HANDLE hServer)
{
	JS_ServerLoop * pServer = (JS_ServerLoop * )hServer;
	if(pServer==NULL)
		return 0;
	return pServer->nServerPort;
}

int JS_EventLoop_SetInputFd(JS_EventLoop * pIO, JS_SOCKET_T nSock, int nEnable, int nLock)
{
	if(JS_UTIL_CheckSocketValidity(nSock)<0)
		return -1;
	if(nLock)
		JS_UTIL_LockMutex(pIO->hMutexForFDSet);
	if(nEnable) {
		JS_FD_SET(nSock,pIO->pReadFdSet);
		if(nSock>=pIO->nMaxFd)
			pIO->nMaxFd = nSock+1;
		pIO->nInputNum++;
	}else {
		JS_FD_CLR(nSock,pIO->pReadFdSet);
		pIO->nInputNum--;
	}
	if(nLock)
		JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
	return 0;
}

int JS_EventLoop_SetOutputFd(JS_EventLoop * pIO, JS_SOCKET_T nSock, int nEnable, int nLock)
{
	if(nLock)
		JS_UTIL_LockMutex(pIO->hMutexForFDSet);
	if(nEnable) {
		pIO->nOutputNum++;
	}else {
		if(pIO->nOutputNum>0)
			pIO->nOutputNum--;
	}
	if(nLock)
		JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
	return 0;
}

int JS_EventLoop_AddThread(JS_POOL_ITEM_T * pPoolItem)
{
	pPoolItem->nRefCnt++;
	return 0;
}

int JS_EventLoop_DelThread(JS_POOL_ITEM_T * pPoolItem)
{
	if(pPoolItem->nRefCnt>0)
		pPoolItem->nRefCnt--;
	return 0;
}

void JS_EventLoop_SetPollWaitTime(JS_EventLoop * pIO, int nWaitMs)
{
	pIO->nWaitMs = nWaitMs;
}

///////////////////////////////////////////////////////////////////////////////////////
//inner functions
static int JS_EventLoop_Pool_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nDataID;
	JS_EventLoopHandler * pEventLoop;
	JS_EventLoop * pIO = (JS_EventLoop *)pOwner;
	JS_ServerLoop * pServer;
	if(pIO==NULL)
		return -1;	
	pServer = (JS_ServerLoop *)pIO->pParent;
	if(pServer==NULL)
		return -1;
	nDataID = JS_3Pool_MaskGetDataID(pPoolItem);
	if(nDataID<0 || nDataID>=JS_CONFIG_MAX_HANDLER) {
		DBGPRINT("evenloop phasechanger: error data id is wrong %u\n",nDataID);
		return -1;
	}
	pEventLoop = &(pServer->arrHandler[nDataID]);
	if(pEventLoop->pfPhase == NULL) {
		DBGPRINT("evenloop phasechanger: there's no item handler for data id %u\n",nDataID);
		return -1;
	}
	////bypass to handler
	return pEventLoop->pfPhase(pOwner, pPoolItem, nNewPhase);
}

static int JS_EventLoop_PrepareIOLoop(JS_ServerLoop * pServer)
{
	int nRet = 0;
	int nCnt;
	JS_EventLoop * pIO = NULL;
	for(nCnt=0; nCnt<pServer->nIOThreadNum; nCnt++) {
		pIO = &pServer->arrIOThreadPool[nCnt];
		pIO->hIOPool = JS_3Pool_Create(pIO, 64, pServer->nMaxPoolItemSize, 0, JS_EventLoop_Pool_PhaseChange);
		if(pIO->hIOPool==NULL) {
			DBGPRINT("server loop can't alloc io pool\n");
			nRet = -1;
			break;
		}
		JS_3Pool_AttachMutexArray(pIO->hIOPool,JS_CONFIG_MAX_SERVERMUTEX);
		pIO->pReadFdSet = (JS_FD_T*)JS_ALLOC(sizeof(JS_FD_T));
		if(pIO->pReadFdSet==NULL) {
			DBGPRINT("Cant' alloc rd fd set %d\n", nCnt);
			nRet = -1;
			break;
		}
		pIO->pWriteFdSet = (JS_FD_T*)JS_ALLOC(sizeof(JS_FD_T));
		if(pIO->pWriteFdSet==NULL) {
			DBGPRINT("Cant' alloc rd fd set %d\n", nCnt);
			nRet = -1;
			break;
		}
		pIO->hMutexForFDSet = JS_UTIL_CreateMutex();
		pIO->nIOIndex = nCnt;
		pIO->nInputNum = 0;
		pIO->nOutputNum = 0;
		pIO->nWaitMs = JS_CONFIG_TIME_MSEC_POLL;
		pIO->pParent = (void *)pServer;
		pIO->pOwner = pServer->hJose;
		JS_FD_ZERO(pIO->pReadFdSet);
		JS_FD_ZERO(pIO->pWriteFdSet);
		pIO->hEvent = JS_UTIL_SimpleEventCreate(NULL,0,(void*)pIO->pReadFdSet,&pIO->nMaxFd);
		if(pIO->hEvent==NULL) {
			DBGPRINT("Cant' alloc event %d\n", nCnt);
			nRet = -1;
			break;
		}
	}
	if(nRet<0)
		JS_EventLoop_ClearIOLoop(pServer);
	return nRet;
}

static int JS_EventLoop_ClearIOLoop(JS_ServerLoop * pServer)
{
	int nRet = 0;
	int nCnt;
	JS_EventLoop * pIO = NULL;
	for(nCnt=0; nCnt<pServer->nIOThreadNum; nCnt++) {
		pIO = &pServer->arrIOThreadPool[nCnt];
		if(pIO->hIOPool)
			JS_3Pool_Destroy(pIO->hIOPool);
		if(pIO->pReadFdSet) {
			JS_FD_ZERO(pIO->pReadFdSet);
			JS_FREE(pIO->pReadFdSet);
		}
		if(pIO->pWriteFdSet) {
			JS_FD_ZERO(pIO->pWriteFdSet);
			JS_FREE(pIO->pWriteFdSet);
		}
		if(pIO->hEvent)
			JS_UTIL_SimpleEventDestroy(pIO->hEvent);
		if(pIO->hMutexForFDSet)
			JS_UTIL_DestroyMutex(pIO->hMutexForFDSet);
	}
	memset((char*)pServer->arrIOThreadPool,0,sizeof(pServer->arrIOThreadPool));
	return 0;
}

static int JS_EventLoop_StopIOThreads(JS_ServerLoop * pServer)
{
	int nIOCnt;
	JS_EventLoop	* pIO = NULL;
	for(nIOCnt=0; nIOCnt<pServer->nIOThreadNum; nIOCnt++) {
		pIO = &pServer->arrIOThreadPool[nIOCnt];
		////wake up the io thread
		JS_UTIL_SimpleEventSend(pIO->hEvent,"exitthread", 3);
		JS_ThreadPool_WaitForEndOfThread(pIO->hIOThread,60000,pIO->nThreadID);
		DBGPRINT("ServerLoop IOStop end\n");
	}
	return 0;
}

static int JS_EventLoop_StartIOThreads(JS_ServerLoop * pServer)
{
	int nIOCnt;
	JS_EventLoop	* pIO = NULL;
	for(nIOCnt=0; nIOCnt<pServer->nIOThreadNum; nIOCnt++) {
		pIO = &pServer->arrIOThreadPool[nIOCnt];
		pIO->nStatus = JS_IO_READY;
		pIO->nThreadID = JS_ThreadPool_StartThread(&pIO->hIOThread,_JS_EventLoop_IOThread_,(void *)pIO);
		if(pIO->hIOThread==NULL) {
			DBGPRINT("can't start IO thread %d!\n",nIOCnt);
			return -1;
		}
	}
	return 0;
}

static int JS_EventLoop_AcceptAndAdd(JS_ServerLoop	* pServer)
{
	int nRet = 0;
	int nIOCnt;
	JS_SOCKET_T nTmpSock;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	struct sockaddr_in rcAddr;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	int nAddrLen;
#else
	unsigned int nAddrLen;
#endif
	JS_EventLoop	* pIO = NULL;

	nAddrLen = sizeof(rcAddr);
	nTmpSock = accept(pServer->nServerSocket,(struct sockaddr *)&rcAddr, &nAddrLen);
	if(JS_UTIL_CheckSocketValidity(nTmpSock)<0)
		return -1;
	////round robin io threads
	nIOCnt = pServer->nRRcounter;
	pIO = &pServer->arrIOThreadPool[nIOCnt];
	pServer->nRRcounter++;
	////count up for round robin
	if(pServer->nRRcounter>=pServer->nIOThreadNum)
		pServer->nRRcounter = 0;
	////make session item
	JS_UTIL_LockMutex(pIO->hMutexForFDSet);
	pPoolItem = JS_3Pool_ActivateAnyFreeItem(pIO->hIOPool);
	if(pPoolItem==NULL) {
		DBGPRINT("server loop:can't alloc session Item(mem error)\n");
		nRet = -1;
		goto LABEL_ENDOF_ADDSESSION;
	}
	////set data id as default
	JS_3Pool_MaskSetDataID(pPoolItem,pServer->pDefaultHandler->nDataID);
	////callback to user library which own this
	nRet = pServer->pDefaultHandler->pfAddIO(pIO,pPoolItem, nTmpSock);
	/////update pool item to use
	JS_3Pool_FinishInitItem(pIO->hIOPool,nRet,pPoolItem);
	if(nRet<0) {
		DBGPRINT("server loop:add callback error happend\n");
		JS_3Pool_FinishInitItem(pIO->hIOPool,nRet,pPoolItem);
		nRet = -1;
		goto LABEL_ENDOF_ADDSESSION;
	}
	//////callback end	
LABEL_ENDOF_ADDSESSION:
	JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
	if(nRet<0) {
		if(JS_UTIL_CheckSocketValidity(nTmpSock)>=0)
			JS_UTIL_SocketClose(nTmpSock);
	}else{
		JS_EventLoop_SetInputFd(pIO,nTmpSock,1,1);
		////wake up the io thread
		JS_UTIL_SimpleEventSend(pIO->hEvent,"wakeup", 3);
	}
	return nRet;
}

static int JS_EventLoop_DoIO(JS_EventLoop * pIO , JS_FD_T * pRDSet, JS_FD_T * pWRSet)
{
	int nDataID;
	JS_HANDLE hIterPos = NULL;
	int nNeedToRemoveItem = 0;
	JS_POOL_ITEM_T * pPoolItem;
	JS_ServerLoop * pServer = (JS_ServerLoop *)pIO->pParent;
	JS_EventLoopHandler * pEventHandler = NULL;

	hIterPos = JS_GetHead(pIO->hIOPool,JS_POOL_PHASE_HOT);
	while(1) {
		nNeedToRemoveItem = 0;
		JS_UTIL_LockMutex(pIO->hMutexForFDSet);
		hIterPos = JS_3Pool_GetNext(pIO->hIOPool,hIterPos);
		JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
		if(hIterPos==NULL)
			break;
		JS_UTIL_LockMutex(pIO->hMutexForFDSet);
		pPoolItem = JS_3Pool_GetDataFromIterateItem(pIO->hIOPool,hIterPos);
		JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
		if(pPoolItem==NULL)
			break;
		////check data id
		nDataID = JS_3Pool_MaskGetDataID(pPoolItem);
		if(nDataID<0 || nDataID>=JS_CONFIG_MAX_HANDLER)
			nNeedToRemoveItem = 1;
		else {
			JS_EventLoopHandler * pEventHandler = &(pServer->arrHandler[nDataID]);
			if(pEventHandler->pfDoIO==NULL)
				nNeedToRemoveItem = 1;
			else if(pEventHandler->pfDoIO(pIO, pPoolItem,pRDSet,pWRSet)<0)
				nNeedToRemoveItem = 1;
		}
		////check need to remove poolitem
		if(nNeedToRemoveItem) {
			JS_UTIL_LockMutex(pIO->hMutexForFDSet);
			JS_3Pool_FreeItem(pIO->hIOPool,pPoolItem,1);
			JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
		}
	}
	JS_UTIL_LockMutex(pIO->hMutexForFDSet);
	JS_3Pool_ClearIterationHandler(pIO->hIOPool,hIterPos);
	JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
	JS_3Pool_CheckStatus(pIO->hIOPool,JS_SERVERLOOP_CHECK_INTVAL_MS,pIO->hMutexForFDSet);
	return 0;
}

static void * _JS_EventLoop_IOThread_(void * pParam)
{
	JS_EventLoop	* pIO = NULL;
	JS_ServerLoop * pServer = NULL;
	JS_FD_T rcTmpRDSet;
	JS_FD_T rcTmpWRSet;
	struct timeval	rcTime;
	int nMaxFd;
	int nTmpRet;
	int nSelectRet;
	char strBuff[64];

	pIO = (JS_EventLoop *) pParam;
	pIO->nStatus = JS_IO_READY;
	pServer = (JS_ServerLoop *)pIO->pParent;
	DBGPRINT("ServerLoop IO-%d start!\n",pIO->nIOIndex);
	while(1) {
		rcTime.tv_sec = 0;
		if(pIO->nOutputNum>0)
			rcTime.tv_usec = 30000;
		else
			rcTime.tv_usec = pIO->nWaitMs*1000;
		JS_UTIL_LockMutex(pIO->hMutexForFDSet);
		memcpy((char*)&rcTmpRDSet,(char*)pIO->pReadFdSet,sizeof(JS_FD_T));
		memcpy((char*)&rcTmpWRSet,(char*)pIO->pWriteFdSet,sizeof(JS_FD_T));
		nMaxFd = pIO->nMaxFd;
		JS_UTIL_UnlockMutex(pIO->hMutexForFDSet);
		nSelectRet = select(nMaxFd,&rcTmpRDSet, &rcTmpWRSet, NULL, &rcTime);
		if(nSelectRet<0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
			errno = WSAGetLastError();
#endif
			DBGPRINT("IOThread%d:Exit cause select error %d\n",pIO->nIOIndex,errno);
			goto IOTHREAD_EXIT;
		}else if(nSelectRet>0) {
			nTmpRet = JS_UTIL_SimpleEventRcv(pIO->hEvent,strBuff,32,(void*)&rcTmpRDSet);
			if(nTmpRet<0) {
				DBGPRINT("IOThread%d: ev recv err %d\n",pIO->nIOIndex, errno);
				goto IOTHREAD_EXIT;
			}
			if(pServer->nNeedToExit==0)
				JS_EventLoop_DoIO(pIO,&rcTmpRDSet,&rcTmpWRSet);
		}else {
			if(pServer->nNeedToExit==0)
				JS_EventLoop_DoIO(pIO,NULL,NULL);
		}
		if(pServer->nNeedToExit)
			break;
	}
IOTHREAD_EXIT:
	DBGPRINT("ServerLoop IOThread%d:exit now\n",pIO->nIOIndex);
	pIO->nStatus = JS_IO_EXIT;
	return NULL;
}

static void * _JS_EventLoop_ListenThread_(void * pParam)
{
	int nRet;
	JS_FD_T	fdRead, tmpFdRead;
	struct timeval	rcTime;
	int nSelectRet;
    JS_ServerLoop	* pServer = (JS_ServerLoop *)pParam;
    int nRpt=0;

    pServer->nStatus = JS_SERVERLOOP_STATUS_ZERO;
	nRet = listen(pServer->nServerSocket, 200);
	if (nRet<0) {
	  DBGPRINT("Server listen error @!\n");
	  goto EXIT_SERVERLOOP;
	}
	////start io thread
	JS_EventLoop_StartIOThreads(pServer);
	////register to the global variable
	pServer->nStatus = JS_SERVERLOOP_STATUS_READY;
	pServer->nMaxFd = pServer->nServerSocket+1;
	JS_FD_ZERO(&fdRead);
	JS_FD_SET(pServer->nServerSocket,&fdRead);
	////start loop
	while(1) {
		pServer->nStatus = JS_SERVERLOOP_STATUS_INLOOP;
		if(pServer->nNeedToExit==1) {
			DBGPRINT("break server loop\n");
			goto EXIT_SERVERLOOP;
		}
		rcTime.tv_sec = 1;
		rcTime.tv_usec = 0;
		memcpy((char*)&tmpFdRead,(char*)&fdRead,sizeof(JS_FD_T));
		nSelectRet = select(pServer->nMaxFd, &tmpFdRead, NULL, NULL, &rcTime);
		if(nSelectRet<0) {
			DBGPRINT("select error %d\n",errno);
			goto EXIT_SERVERLOOP;
		}else if(nSelectRet>0 && JS_FD_ISSET(pServer->nServerSocket,&tmpFdRead)) {
			if(JS_EventLoop_AcceptAndAdd(pServer) < 0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
				errno = WSAGetLastError();
#endif
				DBGPRINT("can't accept incomming socket %d\n",errno);
			}
		}
	}
EXIT_SERVERLOOP:
	JS_EventLoop_StopIOThreads(pServer);
	DBGPRINT("Server loop exited!\n");
	if(pServer->nServerSocket>0) {
		JS_UTIL_SocketClose(pServer->nServerSocket);
		pServer->nServerSocket = 0;
	}
	pServer->nStatus = JS_SERVERLOOP_STATUS_EXIT;
	return NULL;
}

