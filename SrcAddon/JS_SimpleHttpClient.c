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
simple http client api

1. provide nonblocking httpclient api
2. provide bloking api too
3. keep connection pool to reduce new TCP connection
**********************************************************************/

#include "JS_Config.h"

#ifdef JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_ThreadPool.h"
#include "JS_DataStructure.h"
#include "JS_SimpleHttpClient.h"

//////////////////////////////////////////////////
///macro start
//#define JS_HTTPCLIENT_IDLING_TEST


///macro end
//////////////////////////////////////////////////

//////////////////////////////////////////////////
///type definition start
typedef struct JS_SimpleHttpClientItemTag
{
	////key
	char *  pHost;
	UINT32  nHostIP;
	UINT16  nHostPort;
	////connection
	JS_SOCKET_T		nSocket;
	int		nWarmCount;
	////status
	int		nError;
	int		nChunkedBypass;
	int 	nStatus;
	int		nRedirectStatus;
	int		nRedirectCnt;
	int		nRetry;
	int		nFromIdleItem;
	int		nZeroRxCnt;
	////connection monitoring
	int		nConnectTimeout;
	UINT64	nConnectTimeStart;
	////properties
	int					* pnMaxFd;
	JS_FD_T				* pOrgRDSet;
	JS_FD_T				* pOrgWRSet;
	int					  nReqStrLen;
	int					  nReqOffset;
	char				* pReqString;
	int					  nIsReqOwn;
	HTTPSIZE_T			nRangeStart;
	HTTPSIZE_T			nRangeLen;
	HTTPSIZE_T			nRcvSize;
	int					nRcvCount;
	JS_HTTP_Request		* pReq;
	JS_HTTP_Response	* pRsp;
	JS_HANDLE	hOwner;
	JS_HANDLE	hMutexForFDSet;
	int			nNeedStop;
	char	*	pNewLocation;
	JS_POOL_ITEM_T * pPoolItem;
}JS_SimpleHttpClientItem;

typedef struct JS_SimpleHttpClientGlobalTag
{
	int nNeedExit;
	JSUINT nMonitorIntval;
	JS_HANDLE hPool;
	JS_HANDLE hMutex;
	JS_HANDLE hWorkQ;
	JS_HANDLE hDnsMutex;
	JS_HANDLE hDnsCache;
	JS_HANDLE hDnsWorkQ;
}JS_SimpleHttpClientGlobal;
///type definition end
//////////////////////////////////////////////////

//////////////////////////////////////////////////
////extern variable declarations
JS_HANDLE JS_SimpleHttpClient_GetConnectionEX(JS_HTTP_Request	* pReq, int nIsOwnReq);

//////////////////////////////////////////////////
////static functions, variables
__inline static JS_SimpleHttpClientItem * _RET_MYDATA_(JS_POOL_ITEM_T* pItem);
static UINT32 JS_SimpleHttpClient_Resamble (void * pOwner, JS_POOL_ITEM_T * pPoolItem, void * pCompData);
static int JS_SimpleHttpClient_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase);
static int JS_SimpleHttpClient_StatusChange(JS_SimpleHttpClientItem * pItem, int nNewStatus);
static int JS_DNSCache_Init(JS_SimpleHttpClientGlobal * pGlobal);
static int JS_DNSCache_Clear(JS_SimpleHttpClientGlobal * pGlobal);

static JS_HANDLE g_hConnectionPool = NULL;
//////////////////////////////////////////////////
////nonblocking httpclient functions start

//////////////////////////////////////////////////
////globaly allocate connection pool
JS_HANDLE JS_SimpleHttpClient_CreateConnectionPool(int nLifeTimeMs)
{
	int nRet = 0;
	JS_SimpleHttpClientGlobal * pPool = NULL;
	if(g_hConnectionPool) {
		DBGPRINT("detect error:duplicate attempt to init gloabl\n");
		return NULL;
	}
//	DBGPRINT("TMP:Size %u JS_SimpleHttpClientItem\n",sizeof(JS_SimpleHttpClientItem));
//	DBGPRINT("TMP:Size %u JS_SimpleHttpClientGlobal\n",sizeof(JS_SimpleHttpClientGlobal));
	pPool = (JS_SimpleHttpClientGlobal *)JS_ALLOC(sizeof(JS_SimpleHttpClientGlobal));
	if(pPool==NULL) {
		nRet = -1;
		DBGPRINT("simple httpclient create: mem error(global)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pPool,0,sizeof(JS_SimpleHttpClientGlobal));
	pPool->nMonitorIntval = nLifeTimeMs/2;
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////3 phases (hot,warm,cold) pool will be created. item in hot,warm phase keep tcp socket
	pPool->hPool = JS_3Pool_CreateEx(pPool, 4, sizeof(JS_SimpleHttpClientItem), nLifeTimeMs, JS_CONFIG_MAX_HTTPCONNECTIONPOOL, JS_SimpleHttpClient_PhaseChange,NULL);
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////
	if(pPool->hPool==NULL) {
		nRet = -1;
		DBGPRINT("simple httpclient create: mem error(pool)\n");
		goto LABEL_CATCH_ERROR;
	}
	//mutex is used when accessing 3phase pool
	pPool->hMutex = JS_UTIL_CreateMutex();
	if(pPool->hMutex==NULL) {
		nRet = -1;
		DBGPRINT("simple httpclient create: mem error(mutex)\n");
		goto LABEL_CATCH_ERROR;
	}
	//workq for blocking api, it has 4~16 maximum threads at the same time.
	pPool->hWorkQ = JS_ThreadPool_CreateWorkQueue(JS_CONFIG_MAX_DOWNLOADWORKS);
	if(pPool->hWorkQ==NULL) {
		nRet = -1;
		DBGPRINT("simple httpclient create: mem error(workq)\n");
		goto LABEL_CATCH_ERROR;
	}
	g_hConnectionPool = pPool;
	nRet = JS_DNSCache_Init(pPool);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_SimpleHttpClient_DestroyConnectionPool(pPool);
		pPool = NULL;
	}
	return pPool;
}

//////////////////////////////////////////////////
////stop all connections before destroy pool
void JS_SimpleHttpClient_StopAllConnection(void)
{
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal *)g_hConnectionPool;
	if(pGlobal)
		pGlobal->nNeedExit = 1;	////every nonblocking functions should watch this flag
}

//////////////////////////////////////////////////
////destroy http connection pool
void JS_SimpleHttpClient_DestroyConnectionPool(JS_HANDLE hPool)
{
	JS_SimpleHttpClientGlobal * pPool = (JS_SimpleHttpClientGlobal *)hPool;
	JS_SimpleHttpClient_StopAllConnection();
	if(pPool) {
		if(pPool->hPool)
			JS_3Pool_Destroy(pPool->hPool);
		if(pPool->hMutex)
			JS_UTIL_DestroyMutex(pPool->hMutex);
		if(pPool->hWorkQ)
			JS_ThreadPool_DestroyWorkQueue(pPool->hWorkQ);
		JS_DNSCache_Clear(pPool);
		JS_FREE(pPool);
	}
}

//////////////////////////////////////////////////
////check if the status is idle
int JS_SimpleHttpClient_IsIdleState(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem && pItem->nStatus == JS_HTTPCLIENT_STATUS_IDLE)
		return 1;
	else
		return 0;
}

//////////////////////////////////////////////////
////get error code of httpclient item
int JS_SimpleHttpClient_GetError(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem)
		return pItem->nError;
	else
		return 0;
}

//////////////////////////////////////////////////
////get rsp instance of httpclient item
////rsp instance is kept only in hot phase
JS_HTTP_Request  * JS_SimpleHttpClient_GetReq(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem)
		return pItem->pReq;
	else
		return NULL;
}

JS_HTTP_Response * JS_SimpleHttpClient_GetRsp(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem)
		return pItem->pRsp;
	else
		return NULL;
}

int JS_SimpleHttpClient_GetStatus(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem) {
		return pItem->nStatus;
	}else
		return 0;
}

int JS_SimpleHttpClient_GetConnectTimeout(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem) {
		return pItem->nConnectTimeout;
	}else
		return 0;
}

UINT JS_SimpleHttpClient_GetHostIP(JS_HANDLE hHttpClient)
{
	UINT nRet = 0;
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem)
		nRet = pItem->nHostIP;
	return nRet;
}
//////////////////////////////////////////////////
////get tcp socket from item
JS_SOCKET_T JS_SimpleHttpClient_GetSocket(JS_HANDLE hHttpClient)
{
	JS_SimpleHttpClientItem * pItem;
	pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(pItem)
		return pItem->nSocket;
	else
		return 0;
}

//////////////////////////////////////////////////
////free heap when hot->warm,cold or error case
static void JS_SimpleHttpClient_ClearItem(JS_SimpleHttpClientItem * pItem, int nNeedNewConnection, int nClearReq, int nRmFdSet, int nRmHost)
{
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal *)g_hConnectionPool;
	if(pItem->pReqString) {
		JS_FREE(pItem->pReqString);
		pItem->pReqString = NULL;
	}
	if(nClearReq) {
		/////if reques instance is shared among other owners, do not free req
		if(pItem->nIsReqOwn && pItem->pReq) {
			JS_UTIL_HTTP_DeleteRequest(pItem->pReq);
			pItem->pReq = NULL;
		}
	}
	if(pItem->pRsp) {
		JS_UTIL_HTTP_DeleteResponse(pItem->pRsp);
		pItem->pRsp = NULL;
	}

	if(JS_UTIL_CheckSocketValidity(pItem->nSocket)>=0 && pGlobal->nNeedExit == 0 && nRmFdSet) {
		JS_UTIL_LockMutex(pItem->hMutexForFDSet);
		if(pItem->pOrgRDSet)
			JS_FD_CLR(pItem->nSocket,pItem->pOrgRDSet);
		if(pItem->pOrgWRSet)
			JS_FD_CLR(pItem->nSocket,pItem->pOrgWRSet);
		JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
	}
	if(nRmHost && pItem->pHost) {
		JS_FREE(pItem->pHost);
		pItem->pHost = NULL;
	}
	if(nNeedNewConnection) {
		////when hot to cold or retry over some errors
		if(pItem->pNewLocation)
			JS_FREE(pItem->pNewLocation);
		pItem->pNewLocation = NULL;
		pItem->nHostIP = 0;
		pItem->nHostPort = 0;
		pItem->nStatus = JS_HTTPCLIENT_STATUS_ZERO;
		if(JS_UTIL_CheckSocketValidity(pItem->nSocket)>=0) {
			JS_UTIL_SocketClose(pItem->nSocket);
			pItem->nSocket = 0;
		}
		pItem->nZeroRxCnt = 0;
	}
	pItem->nReqStrLen = 0;
	pItem->nReqOffset = 0;
	pItem->nNeedStop  = 0;
	pItem->nFromIdleItem = 0;
	pItem->nRcvCount = 0;
	pItem->nRcvSize = 0;
}

void JS_SimpleHttpClient_Reset(JS_HANDLE hHttpClient, int nNeedNewConnection)
{
	int nClearReq=0;
	int nRmFdSet=0;
	int nRmHost = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hHttpClient;
	if(nNeedNewConnection) {
		nRmFdSet = 1;
	}
	JS_SimpleHttpClient_ClearItem(pItem,nNeedNewConnection,nClearReq,nRmFdSet,nRmHost);
}

//////////////////////////////////////////////////
////alloc httpclient connection or get warm httpclient item
JS_HANDLE JS_SimpleHttpClient_GetConnectionByURL(const char * strURL, const char * strData, const char * strMethod)
{
	JS_HTTP_Request		* pReq;
	pReq = JS_UTIL_HTTP_BuildDefaultReq(NULL,strURL,strData,strMethod);
	if(pReq)
		return JS_SimpleHttpClient_GetConnectionEX(pReq,1);
	else
		return NULL;
}

//////////////////////////////////////////////////
////get connection by shared request items.
////in this case,do not free the request
JS_HANDLE JS_SimpleHttpClient_GetConnectionByReq(JS_HTTP_Request	* pReq)
{
	return JS_SimpleHttpClient_GetConnectionEX(pReq,0);
}

//////////////////////////////////////////////////
////get connection 
static int JS_SimpleHttpClient_CheckIdleConnection(JS_SimpleHttpClientItem * pItem)
{
	if(JS_UTIL_CheckSocketValidity(pItem->nSocket)>=0) {
		char strBuff[8];
		if(JS_UTIL_TCP_Recv(pItem->nSocket,strBuff,4,10)<0) {
			JS_UTIL_SocketClose(pItem->nSocket);
			pItem->nSocket = 0;
			pItem->nStatus = JS_HTTPCLIENT_STATUS_ZERO;
			DBGPRINT("simplehttpclient:idle connection keepalive time expired, reset ...\n");
		}else {
			pItem->nFromIdleItem = 1;
		}
	}else {
		pItem->nSocket = 0;
		pItem->nStatus = JS_HTTPCLIENT_STATUS_ZERO;
	}
	return 0;
}

JS_HANDLE JS_SimpleHttpClient_GetConnectionEX(JS_HTTP_Request	* pReq, int nIsOwnReq)
{
	int nRet = 0;
	JS_POOL_ITEM_T * pPoolItem;
	char * pHost;
	JS_SimpleHttpClientItem * pItem = NULL;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	if(pGlobal == NULL)
		return NULL;
	if(pGlobal->nNeedExit)
		return NULL;
	////1. check pool: free old warm items, free too many cold items
	JS_3Pool_CheckStatus(pGlobal->hPool,pGlobal->nMonitorIntval,pGlobal->hMutex);

	pHost = pReq->pHost;
	JS_UTIL_LockMutex(pGlobal->hMutex);
	////2. find warm connection if available
	pPoolItem = JS_3Pool_ActivateSimliarWarmItem(pGlobal->hPool,JS_SimpleHttpClient_Resamble,(void*)pHost);
	if(pPoolItem==NULL) ////3. get a free cold item
		pPoolItem = JS_3Pool_ActivateColdFreeItem(pGlobal->hPool);
	JS_UTIL_UnlockMutex(pGlobal->hMutex);
	if(pPoolItem==NULL) {
		nRet = -1;
		DBGPRINT("simple httpclient get connection: mem error (poolitem)\n");
		goto LABEL_CATCH_ERROR;
	}
	pItem =  _RET_MYDATA_(pPoolItem);
	////4. set the host to this item
	JS_UTIL_LockMutex(pGlobal->hMutex);
	if(pItem->pHost==NULL) {
		pItem->pHost = JS_UTIL_StrDup(pHost);
	}
	pItem->pReq = pReq;
	pItem->nIsReqOwn = nIsOwnReq;
	if(pItem->pHost)
		JS_3Pool_FinishInitItem(pGlobal->hPool,0,pPoolItem);	////should call finishitem after init
	else
		JS_3Pool_FinishInitItem(pGlobal->hPool,1,pPoolItem);
	if(pItem->nStatus==JS_HTTPCLIENT_STATUS_IDLE)
		JS_SimpleHttpClient_CheckIdleConnection(pItem);
	JS_UTIL_UnlockMutex(pGlobal->hMutex);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		pItem = NULL;
	}
	return pItem;
}

//////////////////////////////////////////////////
////set properties for nonblocking operation see bellow JS_SimpeHttpClient_WorkQEvent
int JS_SimpleHttpClient_SetOwner(JS_HANDLE hClient, JS_HANDLE hOwner, JS_HANDLE hMutexForFDSet, JS_FD_T * pOrgRDSet, JS_FD_T * pOrgWRSet, int * pnMaxFd)
{
	int nNewRfd = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		if(JS_UTIL_CheckSocketValidity(pItem->nSocket)>=0) {
			if(pItem->pOrgRDSet != NULL && pItem->pOrgRDSet != pOrgRDSet) {
				JS_UTIL_LockMutex(pItem->hMutexForFDSet);
				JS_FD_CLR(pItem->nSocket,pItem->pOrgRDSet);
				JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
				nNewRfd = 1;
			}
			if(pItem->pOrgWRSet != NULL && pItem->pOrgWRSet != pOrgWRSet) {
				JS_UTIL_LockMutex(pItem->hMutexForFDSet);
				JS_FD_CLR(pItem->nSocket,pItem->pOrgWRSet);
				JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
			}
		}
		pItem->hOwner = hOwner;
		pItem->hMutexForFDSet = hMutexForFDSet;
		pItem->pOrgRDSet = pOrgRDSet;
		pItem->pOrgWRSet = pOrgWRSet;
		pItem->pnMaxFd = pnMaxFd;
		if(nNewRfd) {
			DBGPRINT("TMP: set new rdset pointer=%x\n",(int) pItem->pOrgRDSet);
			JS_UTIL_LockMutex(pItem->hMutexForFDSet);
			JS_FD_SET(pItem->nSocket,pItem->pOrgRDSet);
			JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
			if(((unsigned int)(*pnMaxFd)) < pItem->nSocket)
				*pnMaxFd = pItem->nSocket;
		}
	}
	return 0;
}

int JS_SimpleHttpClient_SetRsp(JS_HANDLE hClient, JS_HTTP_Response	* pRsp)
{
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		pItem->pRsp = pRsp;
	}
	return 0;
}

int JS_SimpleHttpClient_SetConnectTimeout(JS_HANDLE hClient, int nConnectTimeoutMsec)
{
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		pItem->nConnectTimeout = nConnectTimeoutMsec;
	}
	return 0;
}

////////////////////////////////////////////////////
////set the range  to the item
int JS_SimpleHttpClient_SetRange(JS_HANDLE hClient, HTTPSIZE_T	nRangeStart, HTTPSIZE_T	 nRangeLen)
{
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		pItem->nRangeStart = nRangeStart;
		pItem->nRangeLen = nRangeLen;
	}
	return 0;
}

HTTPSIZE_T JS_SimpleHttpClient_GetRangeLen(JS_HANDLE hClient) 
{
	HTTPSIZE_T nRangeLen = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		nRangeLen =	pItem->nRangeLen;
		if(pItem->pRsp)
			DBGPRINT("TMP: simple http: rangestart=%llu, len=%llu, qrcv=%llu, retry=%u\n", pItem->nRangeStart, nRangeLen, JS_SimpleQ_GetTotalRcvd(pItem->pRsp->hQueue), pItem->nRetry);
	}
	return nRangeLen;
}

HTTPSIZE_T JS_SimpleHttpClient_GetSentSize(JS_HANDLE hClient)
{
	HTTPSIZE_T nSentSize = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		if(pItem->pRsp)
			nSentSize =	JS_SimpleQ_GetTotalSent(pItem->pRsp->hQueue);
	}
	return nSentSize;
}

////////////////////////////////////////////////////
////set data bypass if rsp header has chunked transfer mode
int JS_SimpleHttpClient_SetChunkedBypass(JS_HANDLE hClient, int nEnable)
{
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		pItem->nChunkedBypass = nEnable;
	}
	return 0;
}
////////////////////////////////////////////////////
////add retry counter and return that
int JS_SimpleHttpClient_CheckRetryCount(JS_HANDLE hClient)
{
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem)
		return pItem->nRetry++;
	else
		return 0;
}

////////////////////////////////////////////////////
////prepare pnewlocation item
////if previous doit function return redirecting header, it should be called before next doit call
int JS_SimpleHttpClient_PrepareRedirect(JS_HANDLE hClient)
{
	int nRet = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	JS_HTTP_Response * pRsp = NULL;
	const char * pLocation = NULL;
	char * pNewLocation = NULL;
	char strHost[JS_CONFIG_MAX_SMALLURL];

	if(pItem->nRedirectCnt>JS_CONFIG_MAX_REDIRECT) {
		DBGPRINT("simplehttpclient: too many redirect error %d\n",pItem->nRedirectCnt);
		return -1;
	}
	pItem->nRedirectCnt++;
	pItem->nRedirectStatus = 1;
	pRsp = pItem->pRsp;
	if(pRsp==NULL) {
		nRet = -1;
		DBGPRINT("Can't parse redirect header\n");
		goto LABEL_CATCH_ERROR;
	}
	pLocation = JS_UTIL_GetHTTPResponseHeader(pRsp,"Location");
	if(pLocation==NULL) {
		nRet = -1;
		DBGPRINT("Can't get location from rsp\n");
		goto LABEL_CATCH_ERROR;
	}
	pNewLocation = JS_UTIL_StrDup(pLocation);
	if(pNewLocation==NULL) {
		nRet = -1;
		DBGPRINT("redirect: mem error(pnewlocation)\n");
		goto LABEL_CATCH_ERROR;
	}
	if(JS_UTIL_HTTPExtractHostFromURL(pLocation,strHost,JS_CONFIG_MAX_SMALLURL)==NULL) {
		nRet = -1;
		DBGPRINT("Can't extract host from url\n");
		goto LABEL_CATCH_ERROR;
	}
	nRet = JS_UTIL_ParseHTTPHost(strHost,&pItem->nHostIP,&pItem->nHostPort);
	if(nRet<0) {
		DBGPRINT("Can't get ip from location\n");
		goto LABEL_CATCH_ERROR;
	}
	////close connection
	JS_SimpleHttpClient_ClearItem(pItem,1,0,1,1);
	////dup host and url
	pItem->pHost = JS_UTIL_StrDup(strHost);
	if(pItem->pHost==NULL) {
		nRet = -1;
		DBGPRINT("redirect: mem error(phost)\n");
		goto LABEL_CATCH_ERROR;
	}
	pItem->pNewLocation = pNewLocation;
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pNewLocation)
			JS_FREE(pNewLocation);
	}
	return nRet;
}

////test
int JS_SimpleHttpClient_GetRcvSize(JS_HANDLE hClient, HTTPSIZE_T * pRcvSize)
{
	int nRet = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	if(pItem) {
		nRet = pItem->nRcvCount;
		if(pRcvSize) {
			*pRcvSize = pItem->nRcvSize;
		}
	}
	return nRet;
}

////////////////////////////////////////////////////////////////////////
////main non blocking function for simple httpclient
////read some data and do something according to its state machine
int JS_SimpleHttpClient_DoSomething(JS_HANDLE hClient, JS_HTTP_Response ** ppRsp, char * pDataBuffer, int *pnBuffSize, JS_FD_T * pRDSet, JS_FD_T * pWRSet)
{
	int nRet = 0;
	int nIsNew;
	int nSent;
	int nRecv;
	int nRspCode=0;
	int nOldStatus;
	int nRspLen;
	int nDnsRet;
	int nBuffSize;
	JSUINT nAvailableSize;
	char * pReqData;
	char strTemp[JS_CONFIG_NORMAL_READSIZE];
	JS_HTTP_Request * pReq;
	JS_HTTP_Response * pRsp;
	char * pRspHeader;
	char * pRspBody;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	
	if(pnBuffSize) {
		nBuffSize = *pnBuffSize;
		*pnBuffSize = 0;
	}
	if(pGlobal == NULL)
		return -1;
	if(pGlobal->nNeedExit)
		return -1;
	if(pItem==NULL) {
		DBGPRINT("simple httpclient do asyncjob: wrong param error\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	if(pItem->pRsp==NULL) {
		pItem->pRsp = JS_UTIL_HTTP_PrepareResponse();
		if(pItem->pRsp==NULL) {
			nRet = -1;
			DBGPRINT("simple httpclient: no mem error(pRsp)\n");
			goto LABEL_CATCH_ERROR;
		}
	}
	pRsp = pItem->pRsp;
	pReq = pItem->pReq;
	nOldStatus = pItem->nStatus;
	////recv some data and push to the rsp queue
	if(pRDSet && JS_FD_ISSET(pItem->nSocket,pRDSet)) {
		nRecv=JS_UTIL_TCP_Recv(pItem->nSocket,strTemp,JS_CONFIG_NORMAL_READSIZE,JS_RCV_WITHOUTSELECT);
		if(nRecv<0) {
			DBGPRINT("simple httpclient: connection off (during recv, status=%d, rebirthcnt=%d)\n",nOldStatus,pItem->nWarmCount);
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}else if(nRecv==0) {
			pItem->nZeroRxCnt ++;
#ifndef JS_HTTPCLIENT_IDLING_TEST
			if(pItem->nZeroRxCnt>JS_CONFIG_MAX_RECVZERORET) {
				if(pReq->nQueueStatus != JS_REQSTATUS_BYPASS)
					DBGPRINT("simple httpclient: tcp session is done RST/FIN(status=%d, rebirthcnt=%d)\n",nOldStatus,pItem->nWarmCount);
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
#endif
		}else {
			pItem->nZeroRxCnt = 0;
		}
		if(nRecv>0 && (nOldStatus == JS_HTTPCLIENT_STATUS_RCVHEADER || nOldStatus==JS_HTTPCLIENT_STATUS_RCVBODY)) {
			//JS_UTIL_FileDump("recvq.txt",strTemp,nRecv);
			//DBGPRINT("TMP: httpclient rcv sock %u\n",nRecv);
			nRet = JS_SimpleQ_PushPumpIn(pRsp->hQueue,strTemp,nRecv);
			if(nRet<0) {
				DBGPRINT("simple httpclient: mem error (during header recv)\n");
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
		}
	}
	////if there is post data, send some until tcp send q size eq zero
	if((nOldStatus == JS_HTTPCLIENT_STATUS_RCVHEADER || nOldStatus == JS_HTTPCLIENT_STATUS_RCVBODY) && JS_SimpleQ_GetDataSize(pReq->hQueue)>0) {
		////get some post data and send to the server
		if(pReq->nQueueStatus == JS_REQSTATUS_BYPASS || (JS_UTIL_HTTP_IsPostMethod(pReq) && JS_SimpleQ_CheckAllDone(pReq->hQueue)==0)) {
			pReqData = JS_SimpleQ_PreparePumpOut(pReq->hQueue, 0, &nAvailableSize, NULL, 0, NULL);
			if(pReqData) {
				nSent = JS_UTIL_TCP_SendTimeout(pItem->nSocket,pReqData,nAvailableSize,20);
				if(nSent>0)
					JS_SimpleQ_FinishPumpOut(pReq->hQueue,nSent);
				else if(nSent<0) {
					DBGPRINT("simple httpclient: error in sending post data\n");
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
			}
		}
	}
#ifndef JS_HTTPCLIENT_IDLING_TEST
	////if idle --> connect automatically
	if(nOldStatus==JS_HTTPCLIENT_STATUS_IDLE) {
		JS_UTIL_LockMutex(pItem->hMutexForFDSet);	////this mutex can be null
		JS_FD_SET(pItem->nSocket,pItem->pOrgRDSet);
		if(*pItem->pnMaxFd < (int)pItem->nSocket)
			*pItem->pnMaxFd = pItem->nSocket+1;
		JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);	
		JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_CONNECTING);
	}
#endif
	switch(pItem->nStatus) {
		case JS_HTTPCLIENT_STATUS_ZERO:
		case JS_HTTPCLIENT_STATUS_RESOLVING:
			{
				JS_SOCKET_T nOutSock;
				if(nOldStatus==JS_HTTPCLIENT_STATUS_ZERO)
					pItem->nConnectTimeStart = GetTickCount();
				JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RESOLVING);				
				////1. check name resolving first
				if(pItem->nHostIP==0) {
					nDnsRet = JS_DNSCache_Resolve(pItem->pHost,&pItem->nHostIP);
					if(nDnsRet<0) {
						nRet = -1;
						goto LABEL_CATCH_ERROR;
					}else if(nDnsRet==0) {
						/////in this case async dns resolving job will be launched
						//DBGPRINT("TMP: simple httpclient: name resolving start %s\n",pItem->pHost);
						goto LABEL_CATCH_ERROR;
					}
				}
				////2. try to connect
				pItem->nHostPort = pReq->nTargetPort;
				nOutSock = JS_UTIL_TCP_TryConnect(pItem->nHostIP, pItem->nHostPort);
				DBGPRINT("TMP: hostip=%x, hostport=%d\n",pItem->nHostIP, pItem->nHostPort);
				if(JS_UTIL_CheckSocketValidity(nOutSock)<0) {
					DBGPRINT("simple httpclient do asyncjob: can't connect to target %x, %d\n",pItem->nHostIP,pItem->nHostPort);
					nRet = -1;
					if(pItem->pHost)
						JS_DNSCache_ReportError(pItem->pHost);
					goto LABEL_CATCH_ERROR;
				}
				pItem->nSocket = nOutSock;
				////link this socket to the fdset
				JS_UTIL_LockMutex(pItem->hMutexForFDSet);	////this mutex can be null
				JS_FD_SET(nOutSock,pItem->pOrgRDSet);
				JS_FD_SET(nOutSock,pItem->pOrgWRSet);
				if(*pItem->pnMaxFd < (int)nOutSock)
					*pItem->pnMaxFd = nOutSock+1;
				JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
				//DBGPRINT("TMP: simple httpclient: go ok %x %d %d, %d\n",pItem->nHostIP,nOutSock,*pItem->pnMaxFd,(int)errno);
				////status change to connecting
				JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_CONNECTING);
			}
		break;
		case JS_HTTPCLIENT_STATUS_CONNECTING:
			{
				////check this socket is connected
				if(nOldStatus!=JS_HTTPCLIENT_STATUS_IDLE && (pRDSet==NULL||pWRSet==NULL)) {
					goto LABEL_CATCH_ERROR;					
				}
				if((nOldStatus==JS_HTTPCLIENT_STATUS_IDLE || JS_FD_ISSET(pItem->nSocket,pRDSet) || JS_FD_ISSET(pItem->nSocket,pWRSet))
					&& JS_UTIL_TCP_CheckConnection(pItem->nSocket)>=0) {
					////build req string
					pItem->pReqString = JS_UTIL_HTTP_BuildReqString(pItem->pReq,pItem->nRangeStart,pItem->nRangeLen,pItem->pNewLocation,0);
					if(pItem->pReqString==NULL) {
						DBGPRINT("simple httpclient: mem error(req string)\n");
						nRet = -1;
						goto LABEL_CATCH_ERROR;
					}
					//DBGPRINT("TMP: hostip=%x, hostport=%d REQ=%s!!\n",pItem->nHostIP, pItem->nHostPort, pItem->pReqString);
					pItem->nReqOffset = 0;
					pItem->nReqStrLen = strlen(pItem->pReqString);
					////send request data with 20ms timeout
					nSent = JS_UTIL_TCP_SendTimeout(pItem->nSocket,pItem->pReqString,pItem->nReqStrLen,20);
					if(nSent<0) {
						DBGPRINT("simple httpclient: connection off (during reqsend)\n");
						nRet = -1;
						goto LABEL_CATCH_ERROR;
					}
					if(nSent<pItem->nReqStrLen) {	//tcp send q is not enough
						pItem->nReqOffset = nSent;
						JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_SENDREQ);
					}else {
						if(pReq->nQueueStatus == JS_REQSTATUS_BYPASS)
							JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RCVBODY);
						else
							JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RCVHEADER);
						JS_UTIL_LockMutex(pItem->hMutexForFDSet);
						JS_FD_CLR(pItem->nSocket,pItem->pOrgWRSet);	////wrfdset is not needed during rcv header and body
						JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
					}
				}
			}
		break;
		case JS_HTTPCLIENT_STATUS_SENDREQ:
			{
				nSent = 0;
				if(pItem->nReqOffset<pItem->nReqStrLen) {
					nSent = JS_UTIL_TCP_SendTimeout(pItem->nSocket,pItem->pReqString+pItem->nReqOffset,pItem->nReqStrLen-pItem->nReqOffset,20);
				}
				if(nSent<0) {
					DBGPRINT("simple httpclient: connection off (during defered reqsend)\n");
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				pItem->nReqOffset += nSent;
				if(pItem->nReqOffset>=pItem->nReqStrLen) {
					JS_UTIL_LockMutex(pItem->hMutexForFDSet);
					JS_FD_CLR(pItem->nSocket,pItem->pOrgWRSet);
					JS_UTIL_UnlockMutex(pItem->hMutexForFDSet);
					if(pReq->nQueueStatus == JS_REQSTATUS_BYPASS)
						JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RCVBODY);
					else
						JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RCVHEADER);
				}
			}
		break;
		case JS_HTTPCLIENT_STATUS_RCVHEADER:
			{
				////check header recvd
				pRspHeader = JS_SimpleQ_PreparePumpOut(pRsp->hQueue, 0, &nAvailableSize, "\r\n\r\n", 4, &nIsNew);
				if(pRspHeader && nIsNew) {
					//DBGPRINT("TMP: httpclient send head %u\n",nAvailableSize);
					nRspLen = strlen(pRspHeader);
					////check the rsp instance
					pItem->pRsp = JS_UTIL_HTTP_CheckResponsePacket(pRspHeader,nRspLen,pRsp);
					pRsp = pItem->pRsp;
					if(pRsp==NULL) {
						nRet = JS_RET_CRITICAL_ERROR;
						DBGPRINT("simple httpclient: can't find rsp code\n");
						goto LABEL_CATCH_ERROR;
					}
					if(pRsp->nChunked)
						pRsp->nChunked = 1;
					if((int)nAvailableSize<=nBuffSize) {
						memcpy(pDataBuffer,pRspHeader,nAvailableSize);
						*pnBuffSize = nAvailableSize;
					}else {
						memcpy(pDataBuffer,pRspHeader,nBuffSize);
						*pnBuffSize = nBuffSize;
					}
					JS_SimpleQ_FinishPumpOut(pRsp->hQueue,nAvailableSize);
					if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) > JS_RSPCODEGROUP_REDIRECT) {
						DBGPRINT("TMP: simple error %d, %s , %s\n",pRsp->nRspCode,pReq->pURL,pItem->pHost);
						//JS_UTIL_HTTP_PrintRequest(pReq);
					}else {
						DBGPRINT("TMP: simple httpclient recv header code=%d\n",pRsp->nRspCode);
						if(pRsp->nRspCode != 200) {
							JS_UTIL_HTTP_PrintResponse(pRsp);
						}
					}
					nRspCode = pRsp->nRspCode;
					////setup rsp queue by transfer method
					if(pRsp->nChunked) {
						if(pItem->nChunkedBypass)
							JS_SimpleQ_SetChunkedTransferBypass(pRsp->hQueue,1);
						else
							JS_SimpleQ_SetChunkedTransferDecoding(pRsp->hQueue,1);
					} 
					JS_SimpleQ_ResetTotallSize(pRsp->hQueue,pRsp->nRangeLen);
					////status change
					JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_RCVBODY);
				}
			}
		break;
		case JS_HTTPCLIENT_STATUS_RCVBODY:
			{
				if(JS_UTIL_HTTP_IsHeadMethod(pReq)) {
					pItem->nRcvCount = 0;
					JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_IDLE);
					goto LABEL_CATCH_ERROR;
				}else if(pRsp->nRangeLen<=0 && pRsp->nChunked == 0) {
					pItem->nRcvCount = 0;
					JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_IDLE);
					goto LABEL_CATCH_ERROR;
				}
				pItem->nRcvCount++;
				pRspBody = JS_SimpleQ_PreparePumpOut(pRsp->hQueue, nBuffSize, &nAvailableSize, NULL, 0, NULL);
				if(pRspBody) {
					//DBGPRINT("TMP: httpclient send body %u\n",nAvailableSize);
					pItem->nRcvSize += nAvailableSize;
					memcpy(pDataBuffer,pRspBody,nAvailableSize);
					*pnBuffSize = nAvailableSize;
					JS_SimpleQ_FinishPumpOut(pRsp->hQueue,nAvailableSize);	
				}
				if(pReq->nQueueStatus != JS_REQSTATUS_BYPASS && JS_SimpleQ_CheckAllDone(pRsp->hQueue)) {
					//DBGPRINT("TMP: httpclient send done status=%u,qsize=%llu\n",pReq->nQueueStatus,JS_SimpleQ_GetTotalSent(pRsp->hQueue));
					nRspCode = 200;
					pItem->nRcvCount = 0;
					////status change
					JS_SimpleHttpClient_StatusChange(pItem,JS_HTTPCLIENT_STATUS_IDLE);
				}
			}
		break;
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pItem->nFromIdleItem ==1 && nOldStatus<=JS_HTTPCLIENT_STATUS_RCVHEADER) {
			JS_SimpleHttpClient_ClearItem(pItem,1,0,1,0);
			DBGPRINT("simple httpclient: rm old keepalive sock -> retry (%d)\n",nOldStatus);
			nRet = 0;
		}else {
			pItem->nError = nRet;
			JS_SimpleHttpClient_ClearItem(pItem,1,0,1,0);
			return nRet;
		}
	}
	////check cont timeout
	if(pItem->nStatus == JS_HTTPCLIENT_STATUS_RESOLVING || pItem->nStatus == JS_HTTPCLIENT_STATUS_CONNECTING) {
		 UINT64 nCurTime = GetTickCount();
		 if(nCurTime-pItem->nConnectTimeStart>pItem->nConnectTimeout) {
			 DBGPRINT("simple httpclient:connect timeout status=%d,time=%llu,timeout=%d\n",pItem->nStatus, nCurTime-pItem->nConnectTimeStart,pItem->nConnectTimeout);
			 JS_SimpleHttpClient_ClearItem(pItem,1,0,1,0);
			 pItem->nError = -1;
			 return -1;
		 }
	}
	////check the return code
	if(nOldStatus == JS_HTTPCLIENT_STATUS_CONNECTING && pItem->nStatus > JS_HTTPCLIENT_STATUS_CONNECTING)
		nRet = JS_HTTP_RET_CONNECTED;
	else if(nOldStatus == JS_HTTPCLIENT_STATUS_RCVHEADER && nRspCode>0)
		nRet = JS_HTTP_RET_RCVHEADER;
	else if(nOldStatus == JS_HTTPCLIENT_STATUS_RCVBODY)
		nRet = JS_HTTP_RET_RCVBODY;

	if(nOldStatus != JS_HTTPCLIENT_STATUS_IDLE && pItem->nStatus == JS_HTTPCLIENT_STATUS_IDLE)
		nRet |= JS_HTTP_RET_EOFMASK;

#ifndef JS_HTTPCLIENT_IDLING_TEST	
	if(pItem->nStatus == JS_HTTPCLIENT_STATUS_IDLE) {
		pItem->nRedirectCnt = 0;
		pItem->nRetry = 0;
		if(pItem->pRsp->nKeepAlive==0) {
			DBGPRINT("httpclient dosomething: keep-alive==0, close connection\n");
			JS_SimpleHttpClient_ClearItem(pItem,1,0,1,0);
		}else
			JS_SimpleHttpClient_ClearItem(pItem,0,0,1,0);
	}
#endif
	if(pItem && ppRsp)
		*ppRsp = pItem->pRsp;
	return nRet;
}

void JS_SimpleHttpClient_ReturnConnection(JS_HANDLE hClient)
{
	int nColdNow = 0;
	JS_SimpleHttpClientItem * pItem = (JS_SimpleHttpClientItem *)hClient;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	if(pGlobal == NULL)
		return;
	if(pItem) {
		if(pItem->nStatus!=JS_HTTPCLIENT_STATUS_IDLE) {
			nColdNow = 1;
		}else {
			nColdNow = 0;
		}
		JS_UTIL_LockMutex(pGlobal->hMutex);
		JS_3Pool_FreeItem(pGlobal->hPool,pItem->pPoolItem,nColdNow);
		JS_UTIL_UnlockMutex(pGlobal->hMutex);
	}
}
////nonblocking httpclient functions end
//////////////////////////////////////////////////

//////////////////////////////////////////////////
/////////////inner functions
__inline static JS_SimpleHttpClientItem * _RET_MYDATA_(JS_POOL_ITEM_T* pItem)
{
	return (JS_SimpleHttpClientItem * )pItem->pMyData;
}

static UINT32 JS_SimpleHttpClient_Resamble (void * pOwner, JS_POOL_ITEM_T * pPoolItem, void * pCompData)
{
	UINT32 nRet = 0;
	char * pKey = NULL;
	JS_SimpleHttpClientItem * pItem =  NULL;
	pKey = (char *)pCompData;
	pItem =  _RET_MYDATA_(pPoolItem);
	if(JS_UTIL_StrCmpRestrict(pKey,pItem->pHost,0,0,0)==0)
		return JS_POOL_RET_TOOSIMILAR;
	else
		return 0;
}

static int JS_SimpleHttpClient_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nRet = 0;
	JS_SimpleHttpClientItem * pItem =  NULL;
	JS_SimpleHttpClientGlobal * pPool = (JS_SimpleHttpClientGlobal*)pOwner;
	////common initializing
	pItem =  _RET_MYDATA_(pPoolItem);
	////do something according to the phase
	if(nNewPhase==JS_POOL_PHASE_HOT) {
		if(pItem->pHost==NULL) {
			memset((char*)pItem,0,sizeof(JS_SimpleHttpClientItem));
		}else {
			pItem->nError = 0;
			pItem->nRetry = 0;
			pItem->nRedirectCnt = 0;
			pItem->nRedirectStatus = 0;
			pItem->nFromIdleItem = 0;
		}
		if(pItem->nConnectTimeout==0)
			pItem->nConnectTimeout=JS_CONFIG_TIMOUT_HTTPCONNECT;
		pItem->pPoolItem = pPoolItem;
	}else if(nNewPhase==JS_POOL_PHASE_WARM) {
		pItem->nFromIdleItem = 0;
		JS_SimpleHttpClient_ClearItem(pItem,0,1,1,0);
		pItem->nError = 0;
		pItem->nRetry = 0;
		pItem->nRedirectCnt = 0;
		pItem->nRedirectStatus = 0;
		pItem->pOrgRDSet = NULL;
		pItem->pOrgWRSet = NULL;
		pItem->hMutexForFDSet = NULL;
		pItem->pnMaxFd = NULL;
		pItem->nWarmCount++;
		pItem->nRangeStart= 0;
		pItem->nRangeLen  = 0;
	}else if(nNewPhase==JS_POOL_PHASE_COLD) {
		pItem->nFromIdleItem = 0;
		JS_SimpleHttpClient_ClearItem(pItem,1,1,1,1);
		memset((char*)pItem,0,sizeof(JS_SimpleHttpClientItem));
	}
	return nRet;
}

static int JS_SimpleHttpClient_StatusChange(JS_SimpleHttpClientItem * pItem, int nNewStatus)
{
	pItem->nStatus = nNewStatus;
	return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////
////blocking functions

//////////////////////////////////////////////////
////type and declarations
typedef struct JS_SimpleHttp_AutoItemTag {
	int nType;
	int nNeedExit;
	int nStatus;
	void * pOwner;
	JS_SimpleHttpClientGlobal * pHttpGlobal;
	char * pURL;
	char * pData;
	char * pDownPath;
	int	   nIsPost;
	int		nTimeoutMs;
	JS_FT_FILEEQUEST_CALLBACK  pFileCallBack;
	JS_FT_AJAXREQUEST_CALLBACK pAjaxCallBack;
	JS_HANDLE hItemLock;
	JSUINT	nWorkID;
}JS_SimpleHttp_AutoItem;
static int JS_SimpeHttpClient_WorkQEvent (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff);
static void * JS_SimpeHttpClient_WorkFunction (void * pParam);
////////////////////////////////////////////////////


////////////////////////////////////////////////////
////blocking functions start
JS_HANDLE JS_SimpeHttpClient_SendAjaxRequest(const char * strURL, const char * strData, int nIsPost, JS_FT_AJAXREQUEST_CALLBACK pCallBack, void * pOwner, int nTimeoutMs, JS_HANDLE hItemLock)
{
	int nRet = 0;
	JS_SimpleHttp_AutoItem * pAutoItem=NULL;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	////check necessary item
	if(strURL==NULL || pCallBack==NULL)
		return NULL;
	pAutoItem = (JS_SimpleHttp_AutoItem *)JS_ALLOC(sizeof(JS_SimpleHttp_AutoItem));
	if(pAutoItem==NULL) {
		nRet = -1;
		DBGPRINT("send ajax req: mem error(param)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pAutoItem,0,sizeof(JS_SimpleHttp_AutoItem));
	pAutoItem->pURL = JS_UTIL_StrDup(strURL);
	if(strData)
		pAutoItem->pData = JS_UTIL_StrDup(strData);
	pAutoItem->nIsPost = nIsPost;
	pAutoItem->pAjaxCallBack = pCallBack;
	pAutoItem->pOwner = pOwner;
	pAutoItem->nTimeoutMs = nTimeoutMs;
	pAutoItem->hItemLock = hItemLock;
	pAutoItem->pHttpGlobal = pGlobal;
	if(pGlobal==NULL || pAutoItem->pURL==NULL) {
		DBGPRINT("send ajax req: mem error(url,data)\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	pAutoItem->nWorkID = JS_ThreadPool_AddWorkQueue(pGlobal->hWorkQ,JS_SimpeHttpClient_WorkFunction,(void*)pAutoItem,JS_SimpeHttpClient_WorkQEvent);
	if(pAutoItem->nWorkID == 0) {
		nRet = -1;
		DBGPRINT("send ajax req: mem error(worker)\n");
		goto LABEL_CATCH_ERROR;
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pAutoItem) {
			if(pAutoItem->pURL)
				JS_FREE(pAutoItem->pURL);
			if(pAutoItem->pData)
				JS_FREE(pAutoItem->pData);
			JS_FREE(pAutoItem);
		}
		pAutoItem = NULL;
	}
	return pAutoItem;
}

JS_HANDLE JS_SimpeHttpClient_DownloadFile(const char * strURL, const char * strData, int nIsPost, const char * strDownPath, JS_FT_FILEEQUEST_CALLBACK pCallBack, void * pOwner, int nTimeoutMs, JS_HANDLE hItemLock)
{
	int nRet = 0;
	JS_SimpleHttp_AutoItem * pAutoItem=NULL;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	////check necessary item
	if(pGlobal==NULL || strURL==NULL || pCallBack==NULL || strDownPath==NULL)
		return NULL;
	pAutoItem = (JS_SimpleHttp_AutoItem *)JS_ALLOC(sizeof(JS_SimpleHttp_AutoItem));
	if(pAutoItem==NULL) {
		nRet = -1;
		DBGPRINT("send file req: mem error(param)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pAutoItem,0,sizeof(JS_SimpleHttp_AutoItem));
	pAutoItem->pURL = JS_UTIL_StrDup(strURL);
	if(strData)
		pAutoItem->pData = JS_UTIL_StrDup(strData);
	pAutoItem->pDownPath = JS_UTIL_StrDup(strDownPath);
	pAutoItem->nIsPost = nIsPost;
	pAutoItem->pFileCallBack = pCallBack;
	pAutoItem->pOwner = pOwner;
	pAutoItem->nTimeoutMs = nTimeoutMs;
	pAutoItem->hItemLock = hItemLock;
	pAutoItem->pHttpGlobal = pGlobal;
	if(pAutoItem->pURL==NULL || (strData && pAutoItem->pData==NULL) || pAutoItem->pDownPath==NULL) {
		nRet = -1;
		DBGPRINT("send file req: mem error(url)\n");
		goto LABEL_CATCH_ERROR;
	}
	pAutoItem->nWorkID = JS_ThreadPool_AddWorkQueue(pGlobal->hWorkQ,JS_SimpeHttpClient_WorkFunction,(void*)pAutoItem,JS_SimpeHttpClient_WorkQEvent);
	if(pAutoItem->nWorkID == 0) {
		nRet = -1;
		DBGPRINT("send file req: mem error(worker)\n");
		goto LABEL_CATCH_ERROR;
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pAutoItem) {
			if(pAutoItem->pURL)
				JS_FREE(pAutoItem->pURL);
			if(pAutoItem->pData)
				JS_FREE(pAutoItem->pData);
			if(pAutoItem->pDownPath)
				JS_FREE(pAutoItem->pDownPath);
			JS_FREE(pAutoItem);
		}
		pAutoItem = NULL;
	}
	return pAutoItem;
}

JS_HANDLE JS_SimpeHttpClient_DownloadMultiFiles(const char * strFileListXml, JS_FT_FILEEQUEST_CALLBACK pCallBack, void * pOwner)
{
	JS_SimpleHttp_AutoItem * pItem = NULL;

	return pItem;
}

int JS_SimpeHttpClient_GetMultiDownloadStatus(JS_HANDLE hHttpAsync, char * pXMLBuff, int nBuffSize)
{
	int nRet = 0;

	return nRet;
}

void JS_SimpleHttpClient_StopDownload(JS_HANDLE hHttpClient)
{
	JS_SimpleHttp_AutoItem * pAutoItem=(JS_SimpleHttp_AutoItem *)hHttpClient;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	if(pAutoItem) {
		if(pGlobal)
			JS_ThreadPool_CancelWaiting(pGlobal->hWorkQ,pAutoItem->nWorkID,NULL);
		pAutoItem->nNeedExit = 1;
	}
}

/////////////////inner functions
static int JS_SimpeHttpClient_WorkQEvent (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff)
{
	JS_HANDLE hLock;
	JS_SimpleHttp_AutoItem * pItem = (JS_SimpleHttp_AutoItem*)pParam;
	if(nEvent != JS_WORKQ_EVENT_TOSTRING) {
		if(pItem && nEvent==JS_WORKQ_EVENT_ERROR) {
			if(pItem->pAjaxCallBack)
				pItem->pAjaxCallBack(pItem->pOwner,NULL);
			else
				pItem->pFileCallBack(pItem->pOwner,0,1,0);
		}
		////free worker's parameter
		if(pItem) {
			hLock = pItem->hItemLock;
			JS_UTIL_LockMutex(hLock);
			if(pItem->pURL)
				JS_FREE(pItem->pURL);
			if(pItem->pData)
				JS_FREE(pItem->pData);
			if(pItem->pDownPath)
				JS_FREE(pItem->pDownPath);
			JS_FREE(pItem);
			JS_UTIL_UnlockMutex(hLock);
		}
	}
	return 0;
}

static void * JS_SimpeHttpClient_WorkFunction (void * pParam)
{
	int nRet = 0;
	JS_SOCKET_T nMaxFd=0;
	int nSelectRet;
	int nClientRet;
	UINT64 nTimeStart;
	UINT64 nTimeCurrent;
	char strMethod[16];
	char strMaxBuff[JS_CONFIG_NORMAL_READSIZE+32];
	JS_FD_T rcOrgRDSet;
	JS_FD_T rcOrgWRSet;
	JS_FD_T rcTmpRDSet;
	JS_FD_T rcTmpWRSet;
	struct timeval	rcTime;
	JS_SimpleHttpClientGlobal * pHttpGlobal;
	JS_HTTP_Response * pRsp;
	JS_SimpleHttp_AutoItem * pAutoItem = (JS_SimpleHttp_AutoItem * )pParam;
	JS_HANDLE hClient=NULL;
	char * pAjaxData = NULL;
	JS_HANDLE hFile = NULL;
	int nBuffSize = 0;
	int nRetry = 0;
	int	nAjaxLen = 0;
	int nAjaxOffset = 0;
	int nProgress=0;
	HTTPSIZE_T nRangeLen = 0;
	HTTPSIZE_T nRangeOffset  = 0;
	int nCompleted = 0;
	int nClientEof = 0;

	if(pAutoItem==NULL)
		return NULL;
	pHttpGlobal = pAutoItem->pHttpGlobal;
	if(pAutoItem->nNeedExit || pHttpGlobal->nNeedExit)
		return NULL;
	if(pAutoItem->nIsPost)
		JS_UTIL_StrCopySafe(strMethod,sizeof(strMethod),"POST",4);
	else
		JS_UTIL_StrCopySafe(strMethod,sizeof(strMethod),"GET",3);
	if(pAutoItem->pAjaxCallBack) {
		////prepare ajax string variable
		pAjaxData = (char*)JS_ALLOC(JS_CONFIG_MAXBUFFSIZE);
		if(pAjaxData==NULL) {
			nRet = -1;
			DBGPRINT("httpclient worker: mem error(ajaxdata)\n");
			goto LABEL_CATCH_ERROR;
		}
		nAjaxLen = JS_CONFIG_MAXBUFFSIZE;
	}
	////get the connection from http connection pool
	hClient = JS_SimpleHttpClient_GetConnectionByURL(pAutoItem->pURL,pAutoItem->pData,strMethod);
	if(hClient==NULL) {
		nRet = -1;
		DBGPRINT("httpclient worker: can't get a connection from pool\n");
		goto LABEL_CATCH_ERROR;
	}
	////reset fdsets
	JS_FD_ZERO(&rcOrgRDSet);
	JS_FD_ZERO(&rcOrgWRSet);
	JS_FD_ZERO(&rcTmpRDSet);
	JS_FD_ZERO(&rcTmpWRSet);
	JS_SimpleHttpClient_SetOwner(hClient,pAutoItem->pOwner,NULL,&rcOrgRDSet,&rcOrgWRSet,(int*)&nMaxFd);
	nSelectRet = 0;
	pRsp = NULL;
	if(pAutoItem->nTimeoutMs>0)
		nTimeStart = JS_UTIL_GetTickCount();	////get the tick to calculate timeout
	while(1) {
		if(pHttpGlobal->nNeedExit||pAutoItem->nNeedExit) {
			nRet = -1;
			break;
		}
		if(nSelectRet>=0) {
			nBuffSize = sizeof(strMaxBuff);
			////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			////do nonblocking http client job
			nClientRet = JS_SimpleHttpClient_DoSomething(hClient, &pRsp, strMaxBuff, &nBuffSize, &rcTmpRDSet, &rcTmpWRSet);
			////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			if(nClientRet>0) {
				nClientEof = JS_HTTPRET_CHECKEOF(nClientRet);
				nClientRet = JS_HTTPRET_CHECKRET(nClientRet);
			}else
				nClientEof = 0;
			////check the httpclient job status
			if(nClientRet==JS_HTTP_RET_RCVHEADER) {
				////check rsp header
				if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) == JS_RSPCODEGROUP_REDIRECT) {
					////redirect rsp codes
					nRet = JS_SimpleHttpClient_PrepareRedirect(hClient);
					if(nRet<0) {
						nRet = -1;
						DBGPRINT("httpclient worker: can't prepare redirect item\n");
						goto LABEL_CATCH_ERROR;
					}
				}else if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) == JS_RSPCODEGROUP_SUCCESS) {
					////success rsp code
					nRangeLen = pRsp->nRangeLen;
					nRangeOffset = pRsp->nRangeStartOffset;
				}else {
					////error happended, retry to doit
					nClientRet = -1;
				}
			}else if(nClientRet==JS_HTTP_RET_RCVBODY && nBuffSize>0 ) {
				////get some body data
				if(pAutoItem->pAjaxCallBack) {
					if(nAjaxLen-nAjaxOffset<=nBuffSize) {
						////if buffer is not enough try to grow the buffer
						pAjaxData = (char*)JS_REALLOC(pAjaxData,nAjaxLen+JS_CONFIG_MAXBUFFSIZE);
						if(pAjaxData==NULL) {
							nRet = -1;
							DBGPRINT("httpclient worker: mem error(ajaxdata realloc size=%u)\n",nAjaxLen+JS_CONFIG_MAXBUFFSIZE);
							goto LABEL_CATCH_ERROR;
						}
						nAjaxLen+=JS_CONFIG_MAXBUFFSIZE;
					}
					////copy data to buffer
					memcpy(pAjaxData+nAjaxOffset,strMaxBuff,nBuffSize);
					nAjaxOffset+=nBuffSize;
				}else {
					////check if file is opened
					if(hFile==NULL) {
						hFile = JS_UTIL_FileOpenBinary(pAutoItem->pDownPath,0,0);
						if(hFile==NULL) {
							nRet = -1;
							DBGPRINT("httpclient worker: file open error(%s)\n",pAutoItem->pDownPath);
							goto LABEL_CATCH_ERROR;
						}
					}
					////write some data to file
					if(JS_UTIL_FileWriteBlocking(hFile,strMaxBuff,nBuffSize)<0) {
						nRet = -1;
						DBGPRINT("httpclient worker: file write error(%s)\n",pAutoItem->pDownPath);
						goto LABEL_CATCH_ERROR;
					}
					if(nRangeLen>0) {
						nRangeOffset +=nBuffSize;
						nProgress = (int)(nRangeOffset*100/nRangeLen);
						pAutoItem->pFileCallBack(pAutoItem->pOwner,nProgress,0,0);
					}
				}
			}
			
			if(nClientRet<0) {
				if(nClientRet != JS_RET_CRITICAL_ERROR && (nRetry=JS_SimpleHttpClient_CheckRetryCount(hClient))<=0) {
					////retry, reset body data
					if(pAutoItem->pAjaxCallBack) {
						nAjaxOffset = 0;
					}else {
						if(nRangeLen>0 && nRangeOffset>0) {
							JS_SimpleHttpClient_SetRange(hClient,nRangeOffset,nRangeLen-nRangeOffset);
						}else {
							JS_UTIL_FileDestroy(&hFile);
						}
					}
				}else {
					nRet = -1;
					DBGPRINT("httpclient worker: exit due to error(url=%s, retry=%d)\n",pAutoItem->pURL, nRetry);
					goto LABEL_CATCH_ERROR;
				}
			}
			if(nClientEof) {
				if(pAutoItem->pAjaxCallBack) {
					pAjaxData[nAjaxOffset] = 0;
					pAutoItem->pAjaxCallBack(pAutoItem->pOwner,pAjaxData);
				}else {
					pAutoItem->pFileCallBack(pAutoItem->pOwner,100,0,1);
				}
				nCompleted = 1;
#ifndef JS_HTTPCLIENT_IDLING_TEST
				break;
#endif
			}
		}
		if(JS_UTIL_CheckSocketValidity(JS_SimpleHttpClient_GetSocket(hClient))<0) {
			nSelectRet = 0;
			JS_UTIL_Usleep(100000);
		}else {
			rcTime.tv_sec = 0;
			rcTime.tv_usec = 200000;
			memcpy((char*)&rcTmpRDSet,(char*)&rcOrgRDSet,sizeof(JS_FD_T));
			memcpy((char*)&rcTmpWRSet,(char*)&rcOrgWRSet,sizeof(JS_FD_T));
			nSelectRet = select(nMaxFd,&rcTmpRDSet, &rcTmpWRSet, NULL, &rcTime);
		}
		if(nSelectRet<0) {
			nRet = -1;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
			errno = WSAGetLastError();
#endif
			DBGPRINT("httpclient worker:exit cause select error %d\n",errno);
			goto LABEL_CATCH_ERROR;
		}else if(nSelectRet==0) {
			memset((char*)&rcTmpRDSet,0,sizeof(JS_FD_T));
			memset((char*)&rcTmpWRSet,0,sizeof(JS_FD_T));
		}
		if(pAutoItem->nTimeoutMs>0) {
			nTimeCurrent = JS_UTIL_GetTickCount();
			if(nTimeCurrent>nTimeStart+pAutoItem->nTimeoutMs) {
				nRet =-1;
				DBGPRINT("httpclient worker: timeout happenend\n");
				break;
			}
		}
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pAutoItem->pAjaxCallBack)
			pAutoItem->pAjaxCallBack(pAutoItem->pOwner,NULL);
		else
			pAutoItem->pFileCallBack(pAutoItem->pOwner,0,1,0);
	}
	if(pHttpGlobal->nNeedExit==0 && hClient)
		JS_SimpleHttpClient_ReturnConnection(hClient);
	JS_UTIL_FileDestroy(&hFile);
	if(pAjaxData)
		JS_FREE(pAjaxData);
	return NULL;
}
////blocking functions end
////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////
////simple dns cache functions start
#define JS_DNSCACHE_WAIT			0
#define JS_DNSCACHE_FOUND			1
#define JS_DNSCACHE_ERROR			2

typedef struct JS_SimpleHttpDnsCacheItemTag {
	char * pHostStr;
	UINT32 nHostIP;
	int	   nErrorCnt;
	int	   nStatus;	
}JS_SimpleHttpDnsCacheItem;

static int JS_DNSCache_RmCallback (void * pOwner, void * pData)
{
	JS_SimpleHttpDnsCacheItem * pItem = (JS_SimpleHttpDnsCacheItem *)pData;
	if(pItem) {
		if(pItem->pHostStr)
			JS_FREE(pItem->pHostStr);
		JS_FREE(pItem);
	}
	return 0;
}

static int JS_DNSCache_HashCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	JS_SimpleHttpDnsCacheItem * pItem=  (JS_SimpleHttpDnsCacheItem *)pData;
    void * pKey = NULL;
	if(pItem != NULL) {
		pKey = pItem->pHostStr;
	}else if(pParamKey)
		pKey = pParamKey;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int JS_DNSCache_FindCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	char * strCompRes;
	JS_SimpleHttpDnsCacheItem * pItem =  (JS_SimpleHttpDnsCacheItem *)pData;

	strCompRes = (char*)pParamKey;
	if(JS_UTIL_StrCmpRestrict(pItem->pHostStr,strCompRes,0,0,1)==0)
		nRet = 1;
	return nRet;
}


static int JS_DNSCache_Init(JS_SimpleHttpClientGlobal * pGlobal) 
{
	int nRet = 0;
	pGlobal->hDnsCache = JS_HashMap_Create(pGlobal,JS_DNSCache_RmCallback,JS_DNSCache_HashCallback,JS_CONFIG_NORMAL_DNSMAP,1);
	if(pGlobal->hDnsCache ==NULL) {
		nRet = -1;
		DBGPRINT("dnscache init: mem error(map)\n");
		goto LABEL_CATCH_ERROR;
	}
	pGlobal->hDnsWorkQ = JS_ThreadPool_CreateWorkQueue(1);
	if(pGlobal->hDnsWorkQ == NULL) {
		nRet = -1;
		DBGPRINT("dnscache init: mem error(workq)\n");
		goto LABEL_CATCH_ERROR;
	}
	pGlobal->hDnsMutex = JS_UTIL_CreateMutex();
	JS_SimpleCache_SetHashMap(pGlobal->hDnsCache,JS_CONFIG_NORMAL_DNSCACHE,JS_DNSCache_FindCallback);
LABEL_CATCH_ERROR:
	return nRet;
}

static int JS_DNSCache_Clear(JS_SimpleHttpClientGlobal * pGlobal)
{
	if(pGlobal->hDnsCache)
		JS_HashMap_Destroy(pGlobal->hDnsCache);
	pGlobal->hDnsCache = NULL;
	if(pGlobal->hDnsWorkQ)
		JS_ThreadPool_DestroyWorkQueue(pGlobal->hDnsWorkQ);
	pGlobal->hDnsWorkQ = NULL;
	if(pGlobal->hDnsMutex)
		JS_UTIL_DestroyMutex(pGlobal->hDnsMutex);
	pGlobal->hDnsMutex = NULL;
	return 0;
}


void JS_DNSCache_ReportError(const char * strHost)
{
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	JS_SimpleHttpDnsCacheItem * pItem =  NULL;
	/////check cache first
	if(pGlobal->hDnsCache) {
		JS_UTIL_LockMutex(pGlobal->hDnsMutex);
		pItem = (JS_SimpleHttpDnsCacheItem *)JS_SimpleCache_Find(pGlobal->hDnsCache,(void*)strHost);
		if(pItem) {
			JS_SimpleCache_RemoveEx(pGlobal->hDnsCache, (void*)strHost);
		}
		JS_UTIL_UnlockMutex(pGlobal->hDnsMutex);
	}
}

static int JS_DNSCache_WorkQEventFunc (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff)
{
	return 0;
}

static void * JS_DNSCache_WorkQFunc (void * pParam)
{
	int nRet = 0;
	UINT32 nHostIP = 0;
	struct hostent *hp;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	JS_SimpleHttpDnsCacheItem * pItem =  NULL;
	char * strHost = (char *)pParam;
	if(strHost==NULL || pGlobal==NULL || pGlobal->hDnsCache==NULL)
		return NULL;
	JS_UTIL_LockMutex(pGlobal->hDnsMutex);
	pItem = (JS_SimpleHttpDnsCacheItem *)JS_SimpleCache_Find(pGlobal->hDnsCache,(void*)strHost);
	JS_UTIL_UnlockMutex(pGlobal->hDnsMutex);
	if(pItem==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	////TBD: fix this change with getaddrinfo_a in linux
	hp = gethostbyname(strHost);
	if (hp == NULL) {
		DBGPRINT("dns resolving failed, can't connect dns server\n");
		nRet = -1;
	} else {
		struct in_addr * prcAddr;
		prcAddr = ( struct in_addr*)( hp -> h_addr_list[0]);
		nHostIP = (UINT32)prcAddr->s_addr;
	}
	if(pGlobal==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	////find again
	JS_UTIL_LockMutex(pGlobal->hDnsMutex);
	pItem = (JS_SimpleHttpDnsCacheItem *)JS_SimpleCache_Find(pGlobal->hDnsCache,(void*)strHost);
	JS_UTIL_UnlockMutex(pGlobal->hDnsMutex);
	if(pItem==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	if(nRet>=0) {
		pItem->nHostIP = nHostIP;
		pItem->nStatus = JS_DNSCACHE_FOUND;
		pItem->nErrorCnt = 0;
	}else {
		pItem->nHostIP = 0;
		pItem->nStatus = JS_DNSCACHE_FOUND;
		pItem->nErrorCnt = JS_CONFIG_MAX_DNSERRORCOUNT;
	}
LABEL_CATCH_ERROR:
	JS_FREE(strHost);
	return NULL;
}

int JS_DNSCache_IsBusy(void)
{
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	if(pGlobal)
		return JS_ThreadPool_GetWorksNum(pGlobal->hDnsWorkQ);
	else
		return 0;
}

int JS_DNSCache_Resolve(char * strHost, UINT32 * pnIP)
{
	UINT32 nHostIP = 0;
	int nRet = 0;
	int nNeedDoDns = 0;
	JS_SimpleHttpClientGlobal * pGlobal = (JS_SimpleHttpClientGlobal * )g_hConnectionPool;
	JS_SimpleHttpDnsCacheItem * pItem =  NULL;
	char * pHost;

	if(pGlobal==NULL || strHost==NULL || strHost[0]==0)
		return -1;

//#define DNS_TEST
#ifdef DNS_TEST
	{
		struct hostent *hp;
		hp = gethostbyname(strHost);
		if (hp == NULL) {
			DBGPRINT("dns resolving failed, can't connect dns server\n");
			nRet = -1;
		} else {
			struct in_addr * prcAddr;
			prcAddr = ( struct in_addr*)( hp -> h_addr_list[0]);
			nHostIP = (UINT32)prcAddr->s_addr;
			*pnIP = nHostIP;
		}
		return nRet;
	}
#endif
	/////check cache first
	if(pGlobal->hDnsCache) {
		JS_UTIL_LockMutex(pGlobal->hDnsMutex);
		pItem = (JS_SimpleHttpDnsCacheItem *)JS_SimpleCache_Find(pGlobal->hDnsCache,(void*)strHost);
		if(pItem) {
			if(pItem->nStatus==JS_DNSCACHE_FOUND) {
				nRet = 1;
				nHostIP = pItem->nHostIP;
			}else if(pItem->nStatus==JS_DNSCACHE_ERROR) {
				if(pItem->nErrorCnt>0) {
					pItem->nErrorCnt--;
					nRet = -1;
				}else {
					pItem->nStatus = JS_DNSCACHE_WAIT;
					nNeedDoDns = 1;
				}
			}
		}else {
			pItem = (JS_SimpleHttpDnsCacheItem *)JS_ALLOC(sizeof(JS_SimpleHttpDnsCacheItem));
			if(pItem) {
				pItem->nErrorCnt = 0;
				pItem->nStatus = JS_DNSCACHE_WAIT;
				pItem->nHostIP = 0;
				pItem->pHostStr = JS_UTIL_StrDup(strHost);
				if(pItem->pHostStr) {
					if(JS_SimpleCache_Add(pGlobal->hDnsCache,pItem)>=0)
						nNeedDoDns = 1;
				}else {
					JS_FREE(pItem);
					pItem = NULL;
				}
			}
		}
		JS_UTIL_UnlockMutex(pGlobal->hDnsMutex);
	}
	////do dns job
	if(nNeedDoDns && pItem) {
		pHost = JS_UTIL_StrDup(strHost);
		if(pHost)
			JS_ThreadPool_AddWorkQueue(pGlobal->hDnsWorkQ,JS_DNSCache_WorkQFunc,(void*)pHost,JS_DNSCache_WorkQEventFunc);
	}else if(nRet>0) {
		;//DBGPRINT("DNS Hit!\n");
	}
	if(nRet>0 && pnIP)
		*pnIP = nHostIP;
	return nRet;
}

////simple dns cache functions end
///////////////////////////////////////////////////////////////
#endif
