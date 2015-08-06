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
Media Proxy is a proxy server focused on video streaming and download
It can accelerate the speed with adding more connection like flashget app

1. proxy server based on nonblocking io
2. reduce buffering time for streaming
3. speed up downloading 
**********************************************************************/

#include "JS_Config.h"

#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#include "JS_EventLoop.h"
#include "JS_MediaProxy.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif


//////////////////////////////////////////////////////
////front gate structure
typedef struct JS_MediaProxy_SessionItemTag
{
	////neccesary fields
	JS_SOCKET_T nInSock;
	JS_HANDLE	hSendQueue;
	JS_HTTP_Request	* pReq;
	JS_HTTP_Response * pRsp;
	JS_POOL_ITEM_T * pPoolItem;
	JS_EventLoop * pIO;
	void * pProxyObject;
	int				nKeepAliveCnt;
	////resource fields
	JS_HANDLE hHttpClient;
	int	nZeroRxCnt;
	int nRspCount;
	int nError;
}JS_MediaProxy_SessionItem;

typedef struct JS_StreamInfoItemTag {
	char   strID[JS_CONFIG_MAX_STREAMID];
	int	   nConnection;
	char * pURL;
}JS_StreamInfoItem;

typedef struct JS_MediaStatsTag
{
	JSUINT	nAvgRtt;
	JSUINT	nAvgSwnd;
	JSUINT	nAvgLoss;	
}JS_MediaStats;

typedef struct   JS_MediaProxyGlobalTag
{
	int nNeedToExit;
	JS_HANDLE hGlobalMutex;
	JS_HANDLE hTurboWorkQ;
	JS_HANDLE hStreamInfoCache;
	JS_HANDLE hJose;
	JS_MediaStats rcMediaStats;
}JS_MediaProxyGlobal;

extern int JS_MediaProxy_CheckTCPInfo(JS_SOCKET_T nSock);
extern JS_HANDLE JS_GetProxyServerHandle(JS_HANDLE hJose);

static JS_MediaProxyGlobal * g_pGlobal = NULL;
static JS_EventLoopHandler g_httpEventHandler;
static JS_SOCKET_T JS_MediaProxy_GetSocket(JS_POOL_ITEM_T * pPoolItem);
static int JS_MediaProxy_AddItem(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem, JS_SOCKET_T nInSock);
static int JS_MediaProxy_IOHandler(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet);
static int JS_MediaProxy_TryToSend(JS_EventLoop * pIO,JS_MediaProxy_SessionItem * pItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet);
static int JS_MediaProxy_TryToRead(JS_EventLoop * pIO,JS_MediaProxy_SessionItem * pItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet);
__inline static JS_MediaProxy_SessionItem * _RET_SESSIONITEM_(JS_POOL_ITEM_T* pItem);
static int JS_MediaProxy_SessionItem_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase);
static int JS_MediaProxy_HandOverItem(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem,  JS_SOCKET_T nInSocket, JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp);
static int JS_MediaProxy_CheckBypassMode(JS_MediaProxy_SessionItem * pItem, JS_HTTP_Request * pReq);

static int JS_Cache_RmCallback (void * pOwner, void * pData);
static int JS_Cache_HashCallback (void * pOwner, void * pData, void * pParamKey);
static int JS_Cache_FindCallback (void * pOwner, void * pData, void * pParamKey);
static int JS_MediaProxy_MakeStreamID(char * pStreamID, unsigned int nHostIP, const char * strURL);

JS_EventLoopHandler * JS_MediaProxy_GetEventHandler(void)
{
	g_httpEventHandler.nDataID = JS_DATAID_PROXY;
	g_httpEventHandler.nPoolItemSize = sizeof(JS_MediaProxy_SessionItem);
	g_httpEventHandler.pfAddIO = JS_MediaProxy_AddItem;
	g_httpEventHandler.pfDoIO = JS_MediaProxy_IOHandler;
	g_httpEventHandler.pfPhase = JS_MediaProxy_SessionItem_PhaseChange;
	g_httpEventHandler.pfGetSock = JS_MediaProxy_GetSocket;
	g_httpEventHandler.pfTransferIO = JS_MediaProxy_HandOverItem;
	return &g_httpEventHandler;
}

JS_HANDLE JS_MediaProxy_Create(JS_HANDLE hJose)
{
	int nRet = 0;
	JS_MediaProxyGlobal * pMediaProxy = NULL;
	
	//DBGPRINT("TMP:Size %u JS_MediaProxy_SessionItem\n",sizeof(JS_MediaProxy_SessionItem));
	//DBGPRINT("TMP:Size %u JS_MediaProxyGlobal\n",sizeof(JS_MediaProxyGlobal));
	pMediaProxy = (JS_MediaProxyGlobal *)JS_ALLOC(sizeof(JS_MediaProxyGlobal));
	if(pMediaProxy==NULL) {
		DBGPRINT("proxyserver: create JS_ALLOC error\n");
		return NULL;
	}
	memset((char*)pMediaProxy,0,sizeof(JS_MediaProxyGlobal));
	pMediaProxy->hJose = hJose;
	pMediaProxy->hGlobalMutex = JS_UTIL_CreateMutex();
	if(pMediaProxy->hGlobalMutex==NULL) {
		DBGPRINT("proxyserver: can't alloc mutex\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	pMediaProxy->hTurboWorkQ = JS_ThreadPool_CreateWorkQueue(JS_CONFIG_MAX_TURBOITEM);
	if(pMediaProxy->hTurboWorkQ == NULL) {
		nRet = -1;
		DBGPRINT("proxyserver: mem error(workq)\n");
		goto LABEL_CATCH_ERROR;
	}
	pMediaProxy->hStreamInfoCache = JS_HashMap_Create(pMediaProxy,JS_Cache_RmCallback,JS_Cache_HashCallback,32,1);
	if(pMediaProxy->hStreamInfoCache ==NULL) {
		nRet = -1;
		DBGPRINT("proxy streaminfo init: mem error(map)\n");
		goto LABEL_CATCH_ERROR;
	}
	JS_SimpleCache_SetHashMap(pMediaProxy->hStreamInfoCache,32,JS_Cache_FindCallback);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_MediaProxy_Destroy(pMediaProxy);
		pMediaProxy = NULL;
	}
	g_pGlobal = pMediaProxy;
	return pMediaProxy;
}

int JS_MediaProxy_Destroy(JS_HANDLE hProxy)
{
	JS_MediaProxyGlobal * pMediaProxy = (JS_MediaProxyGlobal *)hProxy;
	if(pMediaProxy) {
		if(pMediaProxy->hTurboWorkQ) {
			if(JS_ThreadPool_GetWorksNum(pMediaProxy->hTurboWorkQ)>0) {
				pMediaProxy->nNeedToExit = 1;
				JS_UTIL_Usleep(200000);
			}
			JS_ThreadPool_DestroyWorkQueue(pMediaProxy->hTurboWorkQ);
		}
		if(pMediaProxy->hStreamInfoCache) {
			JS_UTIL_LockMutex(pMediaProxy->hGlobalMutex);
			JS_SimpleCache_Destroy(pMediaProxy->hStreamInfoCache);
			JS_UTIL_UnlockMutex(pMediaProxy->hGlobalMutex);
		}
		if(pMediaProxy->hGlobalMutex)
			JS_UTIL_DestroyMutex(pMediaProxy->hGlobalMutex);
		JS_FREE(pMediaProxy);
	}
	g_pGlobal = NULL;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
/////inner functions////////////////////////
static int JS_MediaProxy_IOHandler(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet)
{
	JS_SOCKET_T nTmpSock;
	int nRet = 0;
	int nCnt = 0;
	JS_MediaProxy_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	nTmpSock = pSessionItem->nInSock;
	if(JS_UTIL_CheckSocketValidity(nTmpSock)<0) {
		return -1;
	}
	nRet = pSessionItem->nError;
	if(nRet>=0)
		nRet = JS_MediaProxy_TryToRead(pIO,pSessionItem,pRDSet,pWRSet);
	if(nRet>=0)
		nRet = JS_MediaProxy_TryToSend(pIO,pSessionItem,pRDSet,pWRSet);
	if(nRet<0) {
		////remove socket before delete zombie object
		if(JS_UTIL_CheckSocketValidity(pSessionItem->nInSock)>0) {
			JS_UTIL_SocketClose(pSessionItem->nInSock);
			JS_EventLoop_SetOutputFd(pIO,pSessionItem->nInSock,0,1);
			JS_EventLoop_SetInputFd(pIO,pSessionItem->nInSock,0,1);
			if(pSessionItem->hHttpClient) {
				JS_SimpleHttpClient_ReturnConnection(pSessionItem->hHttpClient);
				pSessionItem->hHttpClient=NULL;
			}
			pSessionItem->nInSock = -1;
		}
	}
	return nRet;
}


__inline static JS_MediaProxy_SessionItem * _RET_SESSIONITEM_(JS_POOL_ITEM_T* pItem)
{
	return (JS_MediaProxy_SessionItem * )pItem->pMyData;
}

static int JS_MediaProxy_ResetSession(JS_MediaProxy_SessionItem * pItem, JS_HTTP_Request * pReq)
{
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	if(pReq==NULL)
		pReq = pItem->pReq;
	if(pReq)
		pReq->nQueueStatus = JS_REQSTATUS_WAITREQ;
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	return 0;
}

static int JS_MediaProxy_SessionItem_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nRet = 0;
	JS_MediaProxy_SessionItem * pItem =  NULL;
	JS_EventLoop	* pIO = NULL;

	////common initializing
	pItem =  _RET_SESSIONITEM_(pPoolItem);
	pIO = (JS_EventLoop	*)pOwner;

	////do something according to the phase
	if(nNewPhase==JS_POOL_PHASE_HOT) {
		memset((char*)pItem,0,sizeof(JS_MediaProxy_SessionItem));
	}else if(nNewPhase==JS_POOL_PHASE_WARM) {
		; ///do nothing
	}else if(nNewPhase==JS_POOL_PHASE_COLD) {
		JS_MediaProxy_ResetSession(pItem,NULL);
		if(pItem->pReq)
			JS_UTIL_HTTP_DeleteRequest(pItem->pReq);
		if(pItem->pRsp)
			JS_UTIL_HTTP_DeleteResponse(pItem->pRsp);
		if(pItem->hSendQueue)
			JS_SimpleQ_Destroy(pItem->hSendQueue);
		memset((char*)pItem,0,sizeof(JS_MediaProxy_SessionItem));
	}
	return nRet;
}

static JS_SOCKET_T JS_MediaProxy_GetSocket(JS_POOL_ITEM_T * pPoolItem)
{
	JS_MediaProxy_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	return pSessionItem->nInSock;
}


static int JS_MediaProxy_AddItem(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem, JS_SOCKET_T nInSock)
{
	int nRet = 0;
	JS_MediaProxyGlobal * pGlobal;
	JS_SOCKET_T nTmpSock = nInSock;
	JS_MediaProxy_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	////socket tunning
	JS_UTIL_SetSocketBlockingOption(nTmpSock,0);
	////item init
	pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	pSessionItem->nInSock = nTmpSock;
	pSessionItem->pPoolItem = pPoolItem;
	pSessionItem->pIO = pIO;
	pSessionItem->nKeepAliveCnt = 0;
	pGlobal = (JS_MediaProxyGlobal *)JS_GetProxyServerHandle(pIO->pOwner);
	////test
	//if(JS_ThreadPool_GetWorksNum(pGlobal->hTurboWorkQ)>0)
		//return -1;
	pSessionItem->pProxyObject = pGlobal;
	pSessionItem->hSendQueue = JS_SimpleQ_Create(0,0);
	if(pSessionItem->hSendQueue==NULL) {
		nRet = -1;
		DBGPRINT("proxy add item: no mem error(send q)\n");
	}else
		JS_3Pool_MaskSetDataID(pPoolItem,JS_DATAID_PROXY);
	return nRet;
}

static int JS_MediaProxy_TryToRead(JS_EventLoop * pIO,JS_MediaProxy_SessionItem * pItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet)
{
	int nRet = 0;
	int nRead;
	int nOldQStatus;
	int nIsNew;
	char * pReqHeader;
	int nNeedToAddWrFdSet = 0;
	JSUINT nAvailableSize;
	char strTemp[JS_CONFIG_NORMAL_READSIZE+64];
	JS_HTTP_Request * pReq;
	JS_HANDLE	hQueue;

	if(pItem->pReq==NULL) {
		pItem->pReq = JS_UTIL_HTTP_PrepareRequest();
		if(pItem->pReq==NULL) {
			nRet = -1;
			DBGPRINT("check proxy read: no mem error(req)\n");
			goto LABEL_CATCH_ERROR;
		}
	}
	pReq = pItem->pReq;
	hQueue = pReq->hQueue;
	////read data from io socket
	if(pRDSet && JS_FD_ISSET(pItem->nInSock,pRDSet)) {
		nRead=JS_UTIL_TCP_Recv(pItem->nInSock,strTemp,JS_CONFIG_NORMAL_READSIZE,JS_RCV_WITHOUTSELECT);
		if(nRead<0) {
			if(pItem->pReq && pItem->pReq->pURL)
				DBGPRINT("TMP: proxy connection off %d %s\n",errno,pReq->pURL);
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}else if(nRead==0) {
			pItem->nZeroRxCnt ++;
			if(pItem->nZeroRxCnt>JS_CONFIG_MAX_RECVZERORET) {
				DBGPRINT("proxy: tcp session is done RST/FIN(status=%d:%d,%s)\n",pReq->nQueueStatus,JS_SimpleHttpClient_GetStatus(pItem->hHttpClient),pReq->pURL);
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
		}else {
			pItem->nKeepAliveCnt=0;
			pItem->nZeroRxCnt = 0;
			if(pReq->nQueueStatus != JS_REQSTATUS_BYPASS) {
				if(pReq->nQueueStatus == JS_REQSTATUS_WAITCGI) {
					DBGPRINT("check proxy read: recv req when rsp is not ready(qsize=%u)\n",JS_SimpleQ_GetDataSize(hQueue));
				}
			}
			nRet = JS_SimpleQ_PushPumpIn(hQueue,strTemp,nRead);	////no need to lock for req queue
			if(nRet<0) {
				DBGPRINT("check proxy read: no mem error(pushq)\n");
				goto LABEL_CATCH_ERROR;
			}
		}
	}else if(pRDSet==NULL)
		pItem->nKeepAliveCnt++;
	////check status
	nOldQStatus = pReq->nQueueStatus;
	if(nOldQStatus==JS_REQSTATUS_WAITREQ) {	////entry point of http request
		nIsNew = 0;
		pReqHeader = JS_SimpleQ_PreparePumpOut(hQueue, 0, &nAvailableSize, "\r\n\r\n", 4, &nIsNew); ////no need to lock for req queue
		if(pReqHeader && nIsNew) {
			pItem->nKeepAliveCnt=0;
			pItem->nRspCount++;
			nNeedToAddWrFdSet = 1;
			pItem->pReq = JS_UTIL_HTTP_CheckRequestPacket(pReqHeader, nAvailableSize, pItem->pReq);
			if(pItem->pReq==NULL) {
				DBGPRINT("check proxy read:no mem error(req)\n");
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
			pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
			JS_SimpleQ_FinishPumpOut(hQueue,nAvailableSize);
			if(JS_UTIL_HTTP_IsPostMethod(pReq) && pReq->nRangeLen>0) {
				JS_SimpleQ_ResetTotallSize(pReq->hQueue,pReq->nRangeLen);
			}
			JS_MediaProxy_CheckBypassMode(pItem,pReq);
		}else if(pItem->nKeepAliveCnt>JS_CONFIG_MAX_MEDIAPROXYKEEPCNT) {
			//DBGPRINT("check proxy read:close connection keepalive timeout sock=%d,%s\n",pItem->nInSock,pReq->pURL);
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
	}
LABEL_CATCH_ERROR:
	if(nRet>=0) {
		if(nNeedToAddWrFdSet)
			JS_EventLoop_SetOutputFd(pIO,pItem->nInSock,1,1);
	}
	return nRet;
}

static int JS_MediaProxy_TryToSend(JS_EventLoop * pIO,JS_MediaProxy_SessionItem * pItem, JS_FD_T * pRDSet, JS_FD_T * pWRSet)
{
	int nRet = 0;
	int nSent = 0;
	int nBuffSize = 0;
	int nPumpOutResult = 0;
	int nClientRet = 0;
	int nClientEof = 0;
	char * pData = NULL;
	JSUINT nAvailableSize = 0;
	JS_HTTP_Request * pReq = NULL;
	JS_HTTP_Response * pRsp = NULL;
	JS_HANDLE	hClient = NULL;
	JS_HANDLE	hQueue = NULL;
	char strTemp[JS_CONFIG_MAX_GETMETHOD+32];
	JS_MediaProxyGlobal * pGlobal = NULL;

	////set local variables
	pGlobal = (JS_MediaProxyGlobal*)pItem->pProxyObject;
	pReq = pItem->pReq;
	////if req is not ready, no data to send to client
	if(pReq==NULL || pReq->nQueueStatus == JS_REQSTATUS_WAITREQ)
		return 0;
	////get the connection from http connection pool
	if(pItem->hHttpClient==NULL) {
		pItem->hHttpClient = JS_SimpleHttpClient_GetConnectionByReq(pReq);
		if(pItem->hHttpClient==NULL) {
			nRet = -1;
			DBGPRINT("proxy send: no mem error(httpclient)\n");
			goto LABEL_CATCH_ERROR;
		}
		JS_SimpleHttpClient_SetOwner(pItem->hHttpClient,pItem->pProxyObject,pIO->hMutexForFDSet,pIO->pReadFdSet,pIO->pWriteFdSet,(int*)&pIO->nMaxFd);
		JS_SimpleHttpClient_SetChunkedBypass(pItem->hHttpClient,1);
		if(pItem->pRsp) {
			JS_SimpleHttpClient_SetRsp(pItem->hHttpClient,pItem->pRsp);
			pItem->pRsp = NULL;
		}
	}
	hQueue = pItem->hSendQueue;
	hClient = pItem->hHttpClient;
	nBuffSize = JS_CONFIG_MAX_GETMETHOD;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////do nonblocking http client job
	if(pReq->nQueueStatus == JS_REQSTATUS_WAITCGI || pReq->nQueueStatus == JS_REQSTATUS_BYPASS) {
		nClientRet = JS_SimpleHttpClient_DoSomething(hClient, &pRsp, strTemp, &nBuffSize, pRDSet, pWRSet);
		if(JS_UTIL_GetConfig()->nMaxTurboConnection==1) {
			JS_MediaProxy_CheckTCPInfo(JS_SimpleHttpClient_GetSocket(hClient));
		}
	}else
		nClientRet = 0;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////check the httpclient job status
	if(nClientRet>0) {
		nClientEof = JS_HTTPRET_CHECKEOF(nClientRet);
		nClientRet = JS_HTTPRET_CHECKRET(nClientRet);
	}
#if (JS_CONFIG_USE_TURBOGATE==1)
	if(JS_UTIL_GetConfig()->nMaxTurboConnection>1 && nClientRet==JS_HTTP_RET_RCVHEADER && pRsp && JS_UTIL_HTTP_GetRspCodeGroup(pRsp) == JS_RSPCODEGROUP_SUCCESS) {
		int nConnection = 1;
		if((nConnection=JS_MediaProxy_CheckNeedTurbo(pReq,pRsp))>1) {
			////transfer item to turbogate
			if(JS_UTIL_HTTPResponse_CompareHeader(pRsp,"Accept-Ranges","none")==0 && JS_ThreadPool_GetWorksNum(pGlobal->hTurboWorkQ)<JS_CONFIG_MAX_TURBOITEM) {
				strTemp[nBuffSize] = 0;
				JS_EventLoop_SetOutputFd(pIO,pItem->nInSock,0,1);
				JS_EventLoop_SetInputFd(pIO,pItem->nInSock,0,1);
				JS_TurboGate_Handover(pGlobal->hTurboWorkQ,hClient,nConnection,pItem->nInSock,pReq,strTemp,&(pGlobal->nNeedToExit));
				pItem->nInSock = 0;
				pItem->pReq = NULL;
				pItem->hHttpClient = NULL;
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}else
				nConnection = 1;
		}
	}
#endif
	if((nClientRet==JS_HTTP_RET_RCVHEADER || nClientRet==JS_HTTP_RET_RCVBODY) && nBuffSize>0 ) {
		////push rcvd data to rsp queue
		//JS_UTIL_FileDump("sendq.txt",strTemp,nBuffSize);
		//DBGPRINT("TMP: proxy: push queue size=%u\n",nBuffSize);
		nRet = JS_SimpleQ_PushPumpIn(hQueue,strTemp,nBuffSize);	////no need to lock for req queue
		if(nRet<0) {
			DBGPRINT("proxy send: no mem error(pushq)\n");
			goto LABEL_CATCH_ERROR;
		}
	}	
	if(nClientEof) {
		//DBGPRINT("TMP: mproxy EOF\n");
		pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
	}else if(nClientRet<0) {
		////no retry when front gate
		nRet = -1;
		if(pReq && pReq->pURL && pReq->nQueueStatus != JS_REQSTATUS_BYPASS)
			DBGPRINT("proxy send: exit due to recv error(url=%s,method=%s,rspcnt=%d)\n",pReq->pURL,pReq->strMethod,pItem->nRspCount);
		goto LABEL_CATCH_ERROR;
	}
	////send some data to socket
	nPumpOutResult = 0;
	pData = JS_SimpleQ_PreparePumpOut(hQueue, 0, &nAvailableSize, NULL, 0, NULL);
	if(pData) {
		nSent = JS_UTIL_TCP_SendTimeout(pItem->nInSock,pData,nAvailableSize,10);
		if(nSent>0) {
			if(nSent<nAvailableSize) 
				DBGPRINT("TMP: proxy send: sendq is not enough nSent=%u,nAvailableSize=%u\n",nSent,nAvailableSize);
			nPumpOutResult = JS_SimpleQ_FinishPumpOut(hQueue, nSent);
		}else if(nSent<0) {
			DBGPRINT("proxy send: exit due to sending error(url=%s,method=%s,rspcnt=%d,nBuffSize=%u,nAvailableSize=%u)\n",pReq->pURL,pReq->strMethod,pItem->nRspCount,nBuffSize,nAvailableSize);
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}else {
			DBGPRINT("proxy send: buffer is full\n");
		}
	}
	if(pReq->nQueueStatus == JS_REQSTATUS_ENDOFACTION && JS_SimpleQ_GetDataSize(hQueue)<=0) {
		////all data has sent to the client socket
		////reset req q status to recv new request from client socket
		JS_MediaProxy_ResetSession(pItem,pReq);
		if(pRsp)
			JS_SimpleQ_Reset(pRsp->hQueue);
		JS_SimpleQ_Reset(hQueue);
		JS_EventLoop_SetOutputFd(pIO,pItem->nInSock,0,1);
		pItem->nKeepAliveCnt = 0;
		if(pItem->hHttpClient) {
			JS_SimpleHttpClient_ReturnConnection(pItem->hHttpClient);
			pItem->hHttpClient=NULL;
		}
	}
LABEL_CATCH_ERROR:
	if(nRet<0)
		pItem->nError = nRet;
	return nRet;
}

static int JS_MediaProxy_CheckBypassMode(JS_MediaProxy_SessionItem * pItem, JS_HTTP_Request * pReq)
{
	int nRet = 0;
	////only get and head requests are processed int media filter
	if (pItem->pReq==NULL || JS_UTIL_HTTP_CheckReqMethod(pItem->pReq, "GET") || JS_UTIL_HTTP_CheckReqMethod(pItem->pReq, "HEAD"))
		return nRet;
	if (JS_UTIL_HTTP_CheckReqMethod(pItem->pReq, "POST") && pItem->pReq->nIsMultiPartReq==0) {
		DBGPRINT("TMP:JS_MediaProxy_CheckBypassMode single part POST url=%s\n",pReq->pURL);
		return nRet;
	}
	DBGPRINT("TMP:JS_MediaProxy_CheckBypassMode method=%s %\n", pReq->pURL);
	pReq->nQueueStatus = JS_REQSTATUS_BYPASS;
	return nRet;
}

static int JS_MediaProxy_HandOverItem(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem,  JS_SOCKET_T nInSocket, JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp)
{
	int nRet = 0;
	JS_MediaProxy_SessionItem * pItem = _RET_SESSIONITEM_(pPoolItem);
	memset((char*)pItem,0,sizeof(JS_MediaProxy_SessionItem));
	if(JS_MediaProxy_AddItem(pIO,pPoolItem,nInSocket)<0) {
		return -1;
	}
	pItem->pReq = pReq;
	pItem->pRsp = pRsp;
	pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
//	DBGPRINT("TMP: proxy handover: sock=%d\n",nInSocket);
	if(JS_UTIL_GetConfig()->nUseJoseAgent)
		JS_UTIL_FixHTTPRequest(pItem->pReq,"User-Agent",JS_CONFIG_USERAGENT,0);
	//JS_UTIL_FixHTTPRequest(pItem->pReq,"Connection","Close",0);
	JS_MediaProxy_CheckBypassMode(pItem,pReq);
	nRet = JS_MediaProxy_TryToSend(pIO,pItem, NULL,NULL);
	return nRet;
}

////url=gateinfo
int JS_MediaProxy_CheckTCPInfo(JS_SOCKET_T nSock)
{
#if (JS_CONFIG_OS!=JS_CONFIG_OS_WIN32)
	socklen_t  nTcpInfoLen;
	struct tcp_info rcTcpInfo;
	JS_SOCKET_T nHttpSock = nSock;
	if(JS_UTIL_CheckSocketValidity(nHttpSock)>0) {
		if(getsockopt(nHttpSock, IPPROTO_TCP, TCP_INFO, (void *)&rcTcpInfo, &nTcpInfoLen)==0) {
			//JS_UTIL_FrequentDebugMessage(21,100,"!Check RTT %d\n",rcTcpInfo.tcpi_rcv_rtt);
			if(rcTcpInfo.tcpi_rcv_rtt>0 && rcTcpInfo.tcpi_rcv_rtt<1000000) {
				g_pGlobal->rcMediaStats.nAvgRtt = (g_pGlobal->rcMediaStats.nAvgRtt>>1) + (rcTcpInfo.tcpi_rcv_rtt>>1);
				g_pGlobal->rcMediaStats.nAvgSwnd = (g_pGlobal->rcMediaStats.nAvgSwnd>>1) + ((rcTcpInfo.tcpi_snd_cwnd*rcTcpInfo.tcpi_snd_wscale)>>11);
				g_pGlobal->rcMediaStats.nAvgLoss = (g_pGlobal->rcMediaStats.nAvgLoss>>1) + (rcTcpInfo.tcpi_rttvar>>1);
			}
		}
	}	
#endif
	return 0;
}

int JS_MediaProxy_DIRECTAPI_Information (JS_HANDLE hSession)
{
	char pBuffer[JS_CONFIG_MAX_SMALLPATH];
	char * pJson;
	pBuffer[0] = 0;
	if(g_pGlobal==NULL)
		return -1;
	JS_HttpServer_GetVariableFromReq(hSession,"cmd",pBuffer,256);
	if(strcmp(pBuffer,"all")==0) {
		pJson = JS_ThreadPool_ToStringWorkQueue(g_pGlobal->hTurboWorkQ,8);
		if(pJson) {
			JS_HttpServer_SendQuickJsonRsp(hSession,pJson);
			JS_FREE(pJson);
		}else {
			JS_HttpServer_SendQuickErrorRsp(hSession,500,"internal error");
		}
	}else if(strcmp(pBuffer,"stat")==0) {
		char * pJsonStruct;
		int nBuffSize = 256;
		int nOffset = 0;
		pJsonStruct = JS_UTIL_StrJsonBuildStructStart(nBuffSize,&nOffset);
		if(pJsonStruct)
			pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"avgrtt",g_pGlobal->rcMediaStats.nAvgRtt);
		if(pJsonStruct)
			pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"avgswnd",g_pGlobal->rcMediaStats.nAvgSwnd);
		if(pJsonStruct)
			pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"avgloss",g_pGlobal->rcMediaStats.nAvgLoss);
		if(pJsonStruct) {
			JS_UTIL_StrJsonBuildStructEnd(pJsonStruct,&nBuffSize,&nOffset);
			JS_HttpServer_SendQuickJsonRsp(hSession,pJsonStruct);			
			JS_FREE(pJsonStruct);
		}else {
			JS_HttpServer_SendQuickErrorRsp(hSession,500,"internal error");	
		}		
	}else
		JS_HttpServer_SendQuickErrorRsp(hSession,403,"not found");
	return 0;
}

static int JS_Cache_RmCallback (void * pOwner, void * pData)
{
	JS_StreamInfoItem * pItem = (JS_StreamInfoItem *)pData;
	if(pItem) {
		if(pItem->pURL)
			JS_FREE(pItem->pURL);
		JS_FREE(pItem);
	}
	return 0;
}

static int JS_Cache_HashCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	JS_StreamInfoItem * pItem=  (JS_StreamInfoItem *)pData;
    void * pKey = NULL;
	if(pItem != NULL) {
		pKey = pItem->strID;
	}else if(pParamKey)
		pKey = pParamKey;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int JS_Cache_FindCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	char * strCompRes;
	JS_StreamInfoItem * pItem =  (JS_StreamInfoItem *)pData;

	strCompRes = (char*)pParamKey;
	if(JS_UTIL_StrCmpRestrict(pItem->strID,strCompRes,0,0,0)==0)
		nRet = 1;
	return nRet;
}

static int JS_MediaProxy_MakeStreamID(char * pStreamID, unsigned int nHostIP, const char * strURL)
{
	int nCnt;
	int nSum = 0;
	for(nCnt=0; nCnt<512; nCnt++) {
		if(strURL[nCnt]==0) {
			break;
		}
		nSum += strURL[nCnt];
	}
	nSum = nSum>>5;
	JS_STRPRINTF(pStreamID,JS_CONFIG_MAX_STREAMID,"%u.%u",nHostIP,nSum);
	return 0;
}

int JS_MediaProxy_RemoveStreamInfo(unsigned int nHostIP, const char * strURL)
{
	int nRet = 0;
	char strID[JS_CONFIG_MAX_STREAMID];

	JS_MediaProxy_MakeStreamID(strID,nHostIP,strURL);
	DBGPRINT("JS_MediaProxy_RemoveStreamInfo %s, %u\n",strID,nHostIP);
	JS_UTIL_LockMutex(g_pGlobal->hGlobalMutex);
	JS_SimpleCache_RemoveEx(g_pGlobal->hStreamInfoCache,(void*)strID);
	JS_UTIL_UnlockMutex(g_pGlobal->hGlobalMutex);
	return nRet;
}

int JS_MediaProxy_CheckStreamInfoToAddConnection(unsigned int nHostIP, const char * strURL, int nCurConnection)
{
	int nRet = 0;
	JS_StreamInfoItem * pItem;
	char strID[JS_CONFIG_MAX_STREAMID];

	JS_MediaProxy_MakeStreamID(strID,nHostIP,strURL);
	JS_UTIL_LockMutex(g_pGlobal->hGlobalMutex);
	pItem = (JS_StreamInfoItem *)JS_SimpleCache_Find(g_pGlobal->hStreamInfoCache,(void*)strID);
	if(pItem) {
		if(pItem->pURL && JS_UTIL_StrCmpRestrict(pItem->pURL,strURL,0,0,0)==0) {
			nRet = 1;
		}else {
			if(nCurConnection<2)
				nRet = 1;
			else
				nRet = 0;
		}
	}else {
		pItem = (JS_StreamInfoItem *)JS_ALLOC(sizeof(JS_StreamInfoItem));
		if(pItem) {
			JS_UTIL_StrCopySafe(pItem->strID,JS_CONFIG_MAX_STREAMID,strID,JS_CONFIG_MAX_STREAMID);
			pItem->nConnection = nCurConnection;
			pItem->pURL = JS_UTIL_StrDup(strURL);
			if(JS_SimpleCache_Add(g_pGlobal->hStreamInfoCache,pItem)>=0) {
				DBGPRINT("JS_MediaProxy_CheckStreamInfoToAddConnection add %s, %u\n",strID,nHostIP);
				nRet = 1;
			} else {
				DBGPRINT("check streaminfo cache: can't push to cache (mem error)\n");
				nRet = 0;
			}
		}else {
			DBGPRINT("check streaminfo cache: can't make item (mem error)\n");
			nRet = 0;
		}
	}
	JS_UTIL_UnlockMutex(g_pGlobal->hGlobalMutex);
	return nRet;
}

#endif