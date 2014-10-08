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
Media turbo proxy is workque functions for media proxy
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
#include "JS_HttpMsg.h"
#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////////
//macro start
#define JS_MAIN_CONTID	0
//#define JS_CONFIG_DUMP_SEND	
#define JS_NOT_USE_KEEPALIVE_IN_TURBO_MODE	1
#define JS_USE_ADAPTIVESTREAM_TRACE			1
#define JS_TEST_NO_REDUCE_CONT				0
//////////////////////////////////////////////////////
//local types
typedef struct JS_TurboGate_HttpConnectionTag
{
	int nUse;
	JS_HANDLE hHttp;
	JS_RQ_ITEM_T * rqItem;
}JS_TurboGate_HttpConnection;

typedef struct JS_TurboGate_SessionItemTag
{
	////neccesary fields
	UINT64	  nID;
	JS_HANDLE hRootWorkQ;
	JS_SOCKET_T nInSock;
	JS_HANDLE	hReorderingQueue;
	JS_HTTP_Request	* pReq;
	char * pRspString;
	int	   nChunked;
	int    nRspOffset;
	int    nRspSize;
	int	   nRspCount;
	int	   nZeroRxCnt;
	int	   nKeepAliveCnt;
	int	   nGetNewReq;
	int	* pnExitFlag;
	HTTPSIZE_T	nRangeStartOffset;
	HTTPSIZE_T	nRangeLen;
	////resource fields
	JSUINT	nWorkID;
	int  nConnectionNum;
	int  nTargetContNum;
	int	  nMaxFd;
	JS_FD_T	* pReadFdSet;
	JS_FD_T	* pWriteFdSet;
	JSUINT	nAvgRtt;
	JSUINT	nAvgSwnd;
	JSUINT	nAvgLoss;
	unsigned int nHostIP;
	JS_TurboGate_HttpConnection arrCont[JS_CONFIG_MAX_TURBOCONNECTION + 1];
#ifdef	JS_CONFIG_DUMP_SEND
	JS_HANDLE	hDumpFile;
#endif
	UINT32	nManualChunkSize;
}JS_TurboGate_SessionItem;

extern int JS_MediaProxy_CheckTCPInfo(JS_SOCKET_T nSock);

//////////////////////////////////////////////////////
//function declarations
static int JS_TurboGate_WorkQEvent (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff);
static int JS_TurboGate_DoConnection(JS_TurboGate_SessionItem * pItem, int nContID, JS_FD_T * pRdSet, JS_FD_T * pWrSet);
static void * JS_TurboGate_WorkFunction (void * pParam);
static int JS_TurboGate_CheckHttpClientSocket(JS_TurboGate_SessionItem * pItem);
static int JS_TurboGate_ResetItem(JS_TurboGate_SessionItem * pItem);
static int JS_TurboGate_CheckStreamingSpeed(JS_TurboGate_SessionItem * pItem) ;
static int JS_TurboGate_DecideStartConnectionNumber(JS_TurboGate_SessionItem * pItem, HTTPSIZE_T nRangeLen);
static int JS_TurboGate_ChangeConnectionNumber(JS_TurboGate_SessionItem * pItem, int nContNum);

//////////////////////////////////////////////////////
//function implementations
int JS_TurboGate_Handover(JS_HANDLE hWorkQ, JS_HANDLE hHttpClient, int nConnectionNum, JS_SOCKET_T nInSock, JS_HTTP_Request	* pReq, const char * strRsp, int * pnExitFlag)
{
	int nRet = 0;
	int nCnt;
	JS_HTTP_Response * pRsp = NULL;
	JS_TurboGate_SessionItem * pItem;
	pItem = (JS_TurboGate_SessionItem*)JS_ALLOC(sizeof(JS_TurboGate_SessionItem));
	if(pItem==NULL) {
		nRet = -1;
		DBGPRINT("turbogate: mem error(pItem)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pItem,0,sizeof(JS_TurboGate_SessionItem));
	pItem->hRootWorkQ = hWorkQ;
	pItem->hReorderingQueue = JS_ReorderingQ_Create(JS_CONFIG_MAX_MULTIQUEUEITEM);
	if(pItem->hReorderingQueue == NULL) {
		nRet = -1;
		DBGPRINT("turbogate: mem error(rqueue)\n");
		goto LABEL_CATCH_ERROR;
	}
	if(strRsp) {
		pItem->nRspSize = strlen(strRsp);
		pItem->pRspString = JS_UTIL_StrDup(strRsp);
		if(pItem->pRspString==NULL) {
			nRet = -1;
			DBGPRINT("turbogate: mem error(rsp string)\n");
			goto LABEL_CATCH_ERROR;
		}
		DBGPRINT("(id=%d) turbogate:TMP rsp=%s\n\n",pItem->nWorkID,strRsp);
	}
	if(hHttpClient) {
		pRsp = JS_SimpleHttpClient_GetRsp(hHttpClient);
		if(pRsp->nChunked) {
			JS_SimpleHttpClient_ReturnConnection(hHttpClient);
			hHttpClient = NULL;
		}
		if(pRsp->nRangeLen>0) {
			DBGPRINT("(id=%d) TMP: frt setRange=%llu\n",pItem->nWorkID,pRsp->nRangeLen);
			JS_ReorderingQ_SetTotallSize(pItem->hReorderingQueue,pRsp->nRangeLen);
		}else
			JS_ReorderingQ_SetTotallSize(pItem->hReorderingQueue,JS_REORDERINGQ_UNKNOWNSIZE);
	}
	if(hHttpClient)
		pItem->arrCont[JS_MAIN_CONTID].hHttp = hHttpClient;
	pItem->pReq = pReq;
	pItem->nInSock = nInSock;
	JS_TurboGate_DecideStartConnectionNumber(pItem,pRsp->nRangeLen);
	pItem->pnExitFlag = pnExitFlag;
	if(pRsp) {
		pItem->nRangeLen = pRsp->nRangeLen;
		pItem->nRangeStartOffset = pRsp->nRangeStartOffset;
		pItem->nChunked = pRsp->nChunked;
	}else {
		pItem->nRangeLen = 0;
		pItem->nRangeStartOffset = 0;
		pItem->nChunked = 0;
	}
	////change status 
	if(pItem->pRspString) {
		pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
#ifdef	JS_CONFIG_DUMP_SEND
		pItem->hDumpFile = JS_UTIL_OpenDumpFile("c:\\temp\\rr", pReq->pURL, "&range=", "&");
#endif
	}else {
		pReq->nQueueStatus = JS_REQSTATUS_WAITREQ;
	}
	DBGPRINT("(id=%d) turbogate: handover from frontgate url=%s\n",pItem->nWorkID,pReq->pURL);
	pItem->nWorkID = JS_ThreadPool_AddWorkQueue(hWorkQ,JS_TurboGate_WorkFunction,(void*)pItem,JS_TurboGate_WorkQEvent);
	if(pItem->nWorkID==0) {
		DBGPRINT("turbogate: mem error(workq)\n");
		pReq->nQueueStatus = JS_REQSTATUS_WAITREQ;
		nRet = -1;
	}
	pItem->nHostIP = JS_SimpleHttpClient_GetHostIP(hHttpClient);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pItem) {
			if(pItem->hReorderingQueue)
				JS_ReorderingQ_Destroy(pItem->hReorderingQueue);
			if(pItem->pRspString)
				JS_FREE(pItem->pRspString);
			for(nCnt=0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
				if (pItem->arrCont[nCnt].hHttp != hHttpClient)
					JS_SimpleHttpClient_ReturnConnection(pItem->arrCont[nCnt].hHttp);
			}
			JS_FREE(pItem);
		}
	}
	return nRet;
}

static int JS_TurboGate_WorkQEvent (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff)
{
	int nCnt;
	JS_TurboGate_SessionItem * pItem = (JS_TurboGate_SessionItem*)pParam;
	////free worker's parameter
	if(pItem) {
		if(nEvent == JS_WORKQ_EVENT_TOSTRING) {
			char * pJsonStruct;
			int nBuffSize = 256;
			int nOffset = 0;
			UINT32 nInSpeed;
			UINT32 nOutSpeed;
			pJsonStruct = JS_UTIL_StrJsonBuildStructStart(nBuffSize,&nOffset);
			if(pJsonStruct==NULL) {
				JS_FREE(pStringBuff->pBuff);
				pStringBuff->pBuff = NULL;
				goto LABEL_CATCH_ERROR;
			}
			JS_ReorderingQ_GetSpeed(pItem->hReorderingQueue,&nInSpeed,&nOutSpeed,NULL);
			pJsonStruct = JS_UTIL_StrJsonBuildStructField(pJsonStruct,&nBuffSize,&nOffset,"url",pItem->pReq->pURL);
			if(pJsonStruct)
				pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"connection",pItem->nConnectionNum);
			if(pJsonStruct)
				pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"inspeed",nInSpeed);
			if(pJsonStruct)
				pJsonStruct = JS_UTIL_StrJsonBuildStructFieldInterger(pJsonStruct,&nBuffSize,&nOffset,"outspeed",nOutSpeed);
			if(pJsonStruct) {
				JS_UTIL_StrJsonBuildStructEnd(pJsonStruct,&nBuffSize,&nOffset);
				pStringBuff->pBuff = JS_UTIL_StrJsonBuildArrayItem(pStringBuff->pBuff,&pStringBuff->nBuffSize,&pStringBuff->nOffset,pJsonStruct);
				JS_FREE(pJsonStruct);
			}
		}else {
			DBGPRINT("(id=%d) TMP: turbogate item finished %s\n",pItem->nWorkID,pItem->pReq->pURL);
			//JS_MediaProxy_RemoveStreamInfo(pItem->nHostIP,pItem->pReq->pURL);
			if(JS_UTIL_CheckSocketValidity(pItem->nInSock)>=0)
				JS_UTIL_SocketClose(pItem->nInSock);
			if(pItem->hReorderingQueue)
				JS_ReorderingQ_Destroy(pItem->hReorderingQueue);
			for(nCnt=0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
				if (pItem->arrCont[nCnt].hHttp)
					JS_SimpleHttpClient_ReturnConnection(pItem->arrCont[nCnt].hHttp);
			}
			if(pItem->pReq)
				JS_UTIL_HTTP_DeleteRequest(pItem->pReq);
			if(pItem->pRspString)
				JS_FREE(pItem->pRspString);
			if(pItem->pReadFdSet)
				JS_FREE(pItem->pReadFdSet);
			if(pItem->pWriteFdSet)
				JS_FREE(pItem->pWriteFdSet);			
			JS_FREE(pItem);
		}
	}
LABEL_CATCH_ERROR:
	return 0;
}

static int JS_TurboGate_DecideStartConnectionNumber(JS_TurboGate_SessionItem * pItem, HTTPSIZE_T nRangeLen)
{
	int nContNum = 1;
	if(pItem==NULL)
		return nContNum;
#if (JS_USE_ADAPTIVESTREAM_TRACE==1)
	if (nRangeLen>0 && nRangeLen<JS_CONFIG_MIN_AUTOSTREAM) {
		////for adaptive streaming
		nContNum = JS_CONFIG_MANUAL_STREAM_CONT_NUM;
		JS_TurboGate_ChangeConnectionNumber(pItem, nContNum);
		//pItem->nManualChunkSize = (UINT32)nRangeLen/nContNum;
		pItem->nManualChunkSize = 0;
	} else 
#endif	
	{
		pItem->nConnectionNum = 1;
		pItem->arrCont[0].nUse = 1;
		pItem->nTargetContNum = 1;
		pItem->nManualChunkSize = 0;
	}
	return nContNum;
}

static int JS_TurboGate_ResetItem(JS_TurboGate_SessionItem * pItem)
{
	int nCnt;
	JS_HANDLE hClient;
	pItem->pReq->nQueueStatus = JS_REQSTATUS_WAITREQ;
	pItem->nManualChunkSize = 0;
	for(nCnt=0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
		hClient = pItem->arrCont[nCnt].hHttp;
		if(hClient) {
			JS_SimpleHttpClient_ReturnConnection(hClient);
			pItem->arrCont[nCnt].hHttp = NULL;
		}
		pItem->arrCont[nCnt].rqItem = NULL;
	}
	return 0;
}

static int JS_TurboGate_ChangeConnectionNumber(JS_TurboGate_SessionItem * pItem, int nContNum)
{
	int nCnt;
	int nDiff;
	pItem->nTargetContNum = nContNum;
	nDiff = pItem->nTargetContNum - pItem->nConnectionNum;
	if(nDiff>0) {
		for(nCnt=0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
			if (pItem->arrCont[nCnt].nUse == 0) {
				DBGPRINT("(id=%d) TMP: Increase ContNum %d\n",pItem->nWorkID, pItem->nConnectionNum);
				pItem->arrCont[nCnt].nUse = 1;
				nDiff--;
				if(nDiff<=0)
					break;
			}
		}
		pItem->nConnectionNum = pItem->nTargetContNum-nDiff;
	}
	return 0;
}

static int JS_TurboGate_DoConnection(JS_TurboGate_SessionItem * pItem, int nContID, JS_FD_T * pRdSet, JS_FD_T * pWrSet)
{
	int nRet = 0;
	int nRetry;
	int nClientRet;
	int nClientEof;
	int nBuffSize = 0;
	int nOldStatus = 0;
	JSUINT nRetFromQ;
	JS_HANDLE hClient;
	JS_RQ_ITEM_T * pRqItem;
	JS_HTTP_Request	* pReq;
	JS_HTTP_Response * pRsp;
	HTTPSIZE_T nRangeStart;
	HTTPSIZE_T nRangeLen;
	char strMaxBuff[JS_CONFIG_NORMAL_READSIZE+32];
	
	pReq = pItem->pReq;
	hClient = pItem->arrCont[nContID].hHttp;
	pRqItem = pItem->arrCont[nContID].rqItem;
	if(pReq->nQueueStatus == JS_REQSTATUS_WAITREQ) {
		////check whether there is new req from client socket
		if(pItem->nGetNewReq==0)
			return 0;
	}
	if(hClient == NULL) {
		hClient = JS_SimpleHttpClient_GetConnectionByReq(pReq);
		if(hClient==NULL) {
			nRet = JS_RET_CRITICAL_ERROR;
			DBGPRINT("(id=%d) turbogate: mem error(httpclient %u)\n",pItem->nWorkID, nContID);
			goto LABEL_CATCH_ERROR;
		}
		JS_SimpleHttpClient_SetOwner(hClient,pItem,NULL,pItem->pReadFdSet,pItem->pWriteFdSet,&pItem->nMaxFd);
		pItem->arrCont[nContID].hHttp = hClient;
	}
	if(hClient) {
		HTTPSIZE_T	nRcvSize = 0;
		int nRcvCount = 0;
		nRcvCount = JS_SimpleHttpClient_GetRcvSize(hClient,&nRcvSize);
		if (nRcvCount>2000 && pItem->nConnectionNum>1) {
			;// DBGPRINT("(id=%d) TMP: (cid=%d) rcvcount=%d, rcvsize=%llu, status=%d, rqnull=%d\n", pItem->nWorkID, nContID, nRcvCount, nRcvSize, JS_SimpleHttpClient_GetStatus(hClient), pRqItem == NULL);
		}
	}
	if(*(pItem->pnExitFlag))
		return -1;
	if(pRqItem==NULL) {
		pRqItem = JS_ReorderingQ_PumpInPushBack(pItem->hReorderingQueue,nContID,pItem->nManualChunkSize);
		if(pRqItem==NULL) {
			////all data had been allocated
			if(JS_ReorderingQ_GetNotAllocatedSizeFromTotal(pItem->hReorderingQueue)<=0) {
				////free http client
				JS_SimpleHttpClient_ReturnConnection(hClient);
				pItem->arrCont[nContID].hHttp = NULL;
				pItem->arrCont[nContID].rqItem = NULL;
				//DBGPRINT("TMP: all done\n");
				return 0;
			}
			////busy all items, wait a second
			//DBGPRINT("TMP: busy all items, wait a second\n");
			return 0;
		}
		pItem->arrCont[nContID].rqItem = pRqItem;
		////1. set range
		if(pReq->nQueueStatus==JS_REQSTATUS_WAITREQ) {
			nRangeStart = pReq->nRangeStartOffset;
			nRangeLen = pReq->nRangeLen;
		}else {
			nRangeStart = pRqItem->nOffsetInStream+pItem->nRangeStartOffset;
			nRangeLen = pRqItem->nItemBuffSize;
		}
		nOldStatus = JS_SimpleHttpClient_GetStatus(hClient);
		if(nOldStatus==JS_HTTPCLIENT_STATUS_ZERO || nOldStatus==JS_HTTPCLIENT_STATUS_IDLE) {
			JS_SimpleHttpClient_SetRange(hClient,nRangeStart,nRangeLen);
			DBGPRINT("(id=%d) turbogate: set range contid=%d, %llu\n",pItem->nWorkID,nContID, nRangeLen);
		}else {
			DBGPRINT("(id=%d) turbogate: can't setrange contid=%d, ralen=%llu, status=%d\n",pItem->nWorkID,nContID, nRangeLen,nOldStatus);
			if(nContID != JS_MAIN_CONTID) {
				JS_SimpleHttpClient_Reset(hClient,1);
				JS_SimpleHttpClient_SetRange(hClient,nRangeStart,nRangeLen);
			}
		}
	}
	////2. do action
	pRsp = NULL;
	nBuffSize = JS_CONFIG_NORMAL_READSIZE;
	nClientRet = JS_SimpleHttpClient_DoSomething(hClient, &pRsp, strMaxBuff, &nBuffSize, pRdSet,pWrSet);
	if(nClientRet>0) {
		nClientEof = JS_HTTPRET_CHECKEOF(nClientRet);
		nClientRet = JS_HTTPRET_CHECKRET(nClientRet);
	}else
		nClientEof = 0;
	//DBGPRINT("TMP: turbogate worker:dosomething (%u) %d %d %x\n",nContID,nClientRet,nBuffSize,(int)pRdSet);
	////3. anal return val
	if(nClientRet==JS_HTTP_RET_RCVHEADER) {
		////tskim tmp
		//JS_UTIL_HTTP_PrintResponse(pRsp);
		////check rsp header
		if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) == JS_RSPCODEGROUP_REDIRECT) {
			////redirect rsp codes, reset is done in the prepare function
			nRet = JS_SimpleHttpClient_PrepareRedirect(hClient);
			if(nRet<0) {
				nRet = JS_RET_CRITICAL_ERROR;
				DBGPRINT("turbogate worker: can't prepare redirect item\n");
				goto LABEL_CATCH_ERROR;
			}
		}else if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) == JS_RSPCODEGROUP_SUCCESS) {
			////success rsp code
			if(pReq->nQueueStatus==JS_REQSTATUS_WAITREQ) {
				strMaxBuff[nBuffSize] = 0;
				pItem->pRspString = JS_UTIL_StrDup(strMaxBuff);
				if(pItem->pRspString == NULL) {
					nRet = JS_RET_CRITICAL_ERROR;
					DBGPRINT("turbogate worker: mem error(rsp string)\n");
					goto LABEL_CATCH_ERROR;
				}
				pItem->nRspOffset = 0;
				pItem->nRspSize = nBuffSize;
				if(pRsp->nRangeLen>0) {
					////set size
					DBGPRINT("(id=%d) TMP: set range=%llu\n",pItem->nWorkID,pRsp->nRangeLen);
					JS_ReorderingQ_SetTotallSize(pItem->hReorderingQueue,pRsp->nRangeLen);
				}else
					JS_ReorderingQ_SetTotallSize(pItem->hReorderingQueue,JS_REORDERINGQ_UNKNOWNSIZE);
				pItem->nRangeLen = pRsp->nRangeLen;
				pItem->nRangeStartOffset = pRsp->nRangeStartOffset;
				pItem->nChunked = pRsp->nChunked;
				////change status 
				pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
				JS_TurboGate_DecideStartConnectionNumber(pItem,pRsp->nRangeLen);
#ifdef	JS_CONFIG_DUMP_SEND
				pItem->hDumpFile = JS_UTIL_OpenDumpFile("c:\\temp\\rr", pReq->pURL, "&range=", "&");
#endif
				pItem->nConnectionNum = JS_MediaProxy_CheckNeedTurbo(pReq,pRsp);
				//DBGPRINT("TMP: turbogate get new rsp(%s) contnum=%d\n",pReq->pHost,pItem->nConnectionNum);
			}
		}else {
			if(JS_UTIL_HTTP_IsRspCriticalError(pRsp)) {
				nRet = JS_RET_CRITICAL_ERROR;
				JS_SimpleHttpClient_Reset(hClient,1);
				DBGPRINT("(id=%d) turbogate: critical error code %d\n",pItem->nWorkID,pRsp->nRspCode);
				goto LABEL_CATCH_ERROR;
			}else {
				////error happended, retry to doit
				nRet = -1;
				JS_SimpleHttpClient_Reset(hClient,1);
				goto LABEL_CATCH_ERROR;
			}
		}
	}
	if(nClientRet==JS_HTTP_RET_RCVBODY && nBuffSize>0 ) {
		////get some body data
		//DBGPRINT("TMP: turbogate push data(buffsize=%u,contid=%d)\n",nBuffSize,nContID);
		nRetFromQ = JS_ReorderingQ_PumpInCopyData(pRqItem,strMaxBuff,nBuffSize);
		if(pRqItem->nItemBuffSize<=pRqItem->nPumpInOffset && nContID == JS_MAIN_CONTID) {
			if(pItem->nConnectionNum>1 && JS_SimpleHttpClient_GetRangeLen(hClient)==0) {
				////reset first httpclient to get next rqitem
				JS_SimpleHttpClient_Reset(hClient,1);
			}
		}
		////check reordering queue condition
		if((int)nRetFromQ<nBuffSize) {
			if(nContID == JS_MAIN_CONTID && pItem->nConnectionNum==1) {
				pRqItem = JS_ReorderingQ_PumpInPushBack(pItem->hReorderingQueue,nContID,0);
				if(pRqItem==NULL) {
					DBGPRINT("turbogate mem error(rqitem)\n");
					nRet = JS_RET_CRITICAL_ERROR;
					goto LABEL_CATCH_ERROR;
				}
				pItem->arrCont[nContID].rqItem = pRqItem;
				JS_ReorderingQ_PumpInCopyData(pRqItem,strMaxBuff+nRetFromQ,nBuffSize-nRetFromQ);
			}else if(nContID != JS_MAIN_CONTID && pItem->nConnectionNum>1){
				nRet = JS_RET_CRITICAL_ERROR;
				DBGPRINT("turbogate: not alloced size %llu\n",JS_ReorderingQ_GetNotAllocatedSizeFromTotal(pItem->hReorderingQueue));
				DBGPRINT("turbogate mem error(pumpin %u<->%u,httplen=%llu,rqitemsize=%u, rqoffset=%u, rqrangestart=%llu)\n",nRetFromQ,nBuffSize,JS_SimpleHttpClient_GetRangeLen(hClient),pRqItem->nItemBuffSize,pRqItem->nPumpInOffset, pRqItem->nOffsetInStream+pItem->nRangeStartOffset);
				goto LABEL_CATCH_ERROR;
			}
		}
		////maybe chunked case
		if(pItem->nConnectionNum<=1 && nClientEof) {
			pRqItem->nIsLastItem = 1;
		}
		if(JS_ReorderingQ_NeedNewItem(pRqItem)) {
			////reset buffer
			DBGPRINT("(id=%d) TMP: cont id=%d rqitem done\n", pItem->nWorkID, nContID);
			pItem->arrCont[nContID].rqItem = NULL;
		}
	}			
	if(nClientRet<0) {
		nRet = nClientRet;
		goto LABEL_CATCH_ERROR;
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(nRet != JS_RET_CRITICAL_ERROR) {
			////retry, reset http client
			nRetry = JS_SimpleHttpClient_CheckRetryCount(hClient);
			if(nRetry>JS_CONFIG_MAX_PROXYRETRY) {
				DBGPRINT("TMP: turbogate worker: too many retry, return error (%u)\n",nContID);
				JS_SimpleHttpClient_ReturnConnection(hClient);
				pItem->arrCont[nContID].hHttp = NULL;
				nRet = JS_RET_CRITICAL_ERROR;
			}else {
				DBGPRINT("TMP: turbogate worker: retry(%u)\n",nContID);
				if(pReq->nQueueStatus!=JS_REQSTATUS_WAITREQ) {				
					nRangeStart = pRqItem->nOffsetInStream+pItem->nRangeStartOffset+pRqItem->nPumpInOffset;
					nRangeLen = pRqItem->nItemBuffSize-pRqItem->nPumpInOffset;
					JS_SimpleHttpClient_SetRange(hClient,nRangeStart,nRangeLen);
				}
				nRet = 0;
			}
		}else {
			DBGPRINT("turbogate worker: critical error (%u)\n",nContID);
		}
	}
	if (nRet >= 0 && nContID != JS_MAIN_CONTID && pItem->arrCont[nContID].rqItem == NULL
		&& pItem->nTargetContNum<pItem->nConnectionNum ) {
		JS_SimpleHttpClient_ReturnConnection(hClient);
		pItem->arrCont[nContID].hHttp = NULL;
		pItem->arrCont[nContID].nUse = 0;
		pItem->nConnectionNum--;
		DBGPRINT("(id=%d) Reduce ContNum %d(contid=%d)\n",pItem->nWorkID,pItem->nConnectionNum,nContID);
	}
	return nRet;
}

static void * JS_TurboGate_WorkFunction (void * pParam)
{
	int nRet = 0;
	int nSelectRet;
	JS_FD_T rcTmpRDSet;
	JS_FD_T rcTmpWRSet;
	struct timeval	rcTime;
	JS_HTTP_Request	* pReq;
	JS_TurboGate_SessionItem * pItem;
	char * pBuff;
	JSUINT	nAvailable=0;
	int nBuffSize = 0;
	int nRetry = 0;
	int nSent;
	int nRecv;
	int nRQStatus;
	int nCnt;
	int nOldStatus;
	int nIsNew;
	int nContNum;
	int nTickCounter;
	int nNeedCheckTCP;
	char strTemp[JS_CONFIG_NORMAL_READSIZE+4];
	JS_HANDLE hMainContClient;

	pItem = (JS_TurboGate_SessionItem * )pParam;
	if(pItem==NULL)
		return NULL;
	if(*(pItem->pnExitFlag))
		return NULL;
	////reset fdsets
	pItem->pReadFdSet = (JS_FD_T*)JS_ALLOC(sizeof(JS_FD_T));
	pItem->pWriteFdSet = (JS_FD_T*)JS_ALLOC(sizeof(JS_FD_T));
	pItem->nMaxFd = pItem->nInSock;
	JS_FD_ZERO(pItem->pReadFdSet);
	JS_FD_ZERO(pItem->pWriteFdSet);
	JS_FD_ZERO(&rcTmpRDSet);
	JS_FD_ZERO(&rcTmpWRSet);
	JS_FD_SET(pItem->nInSock,pItem->pReadFdSet);
	nSelectRet = 0;
	pReq = pItem->pReq;
	hMainContClient = pItem->arrCont[JS_MAIN_CONTID].hHttp;
	nTickCounter = 0;
	nNeedCheckTCP = 0;
	if(hMainContClient)
		JS_SimpleHttpClient_SetOwner(hMainContClient,pItem,NULL,pItem->pReadFdSet,pItem->pWriteFdSet,&pItem->nMaxFd);
	while(1) {
		nRet = 0;
		nOldStatus = pReq->nQueueStatus;
		if(*(pItem->pnExitFlag)) {
			nRet = -1;
			break;
		}
		////1. check req socket
		if(nSelectRet>0 && JS_FD_ISSET(pItem->nInSock,&rcTmpRDSet)) {
			nRecv=JS_UTIL_TCP_Recv(pItem->nInSock,strTemp,JS_CONFIG_NORMAL_READSIZE,JS_RCV_WITHOUTSELECT);
			if(nRecv<0) {
				nRet = -1;
			}else if(nRecv==0) {
				pItem->nZeroRxCnt ++;
				DBGPRINT("turbogate: zero recv\n");
				if(pItem->nZeroRxCnt>JS_CONFIG_MAX_RECVZERORET) {
					nRet = -1;
				}
			}else {
				pItem->nZeroRxCnt = 0;
			}
			if(nRet<0) {
				DBGPRINT("(id=%d) turbogate: connection off (during recv %s notsentsize=%llu)\n",pItem->nWorkID,pReq->pHost,JS_ReorderingQ_GetRemainDataNotSent(pItem->hReorderingQueue));
				goto LABEL_CATCH_ERROR;
			}
			if(nRecv>0) {
				nRet = JS_SimpleQ_PushPumpIn(pReq->hQueue,strTemp,nRecv);
				if(nRet<0) {
					DBGPRINT("turbogate: mem error (during header recv)\n");
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
			}
		}
		////2. read some from real server using nonbloking httpclient objects
		nContNum = 0;
		for(nCnt=0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
			if(pReq->nQueueStatus == JS_REQSTATUS_WAITREQ) {
				if(nCnt!=JS_MAIN_CONTID)
					break;
			}
			if (pItem->arrCont[nCnt].nUse) {
				nRet = JS_TurboGate_DoConnection(pItem, nCnt,&rcTmpRDSet,&rcTmpWRSet);
				if(nRet<0)////critical error
					goto LABEL_CATCH_ERROR;
				nContNum++;
				////check TCP info
				if (nNeedCheckTCP && pItem->arrCont[nCnt].nUse) {
					JS_MediaProxy_CheckTCPInfo(JS_SimpleHttpClient_GetSocket(pItem->arrCont[nCnt].hHttp));
				}
			}
			if(nContNum>=pItem->nConnectionNum)
				break;
		}
		////3. send header first, if available
		if(pReq->nQueueStatus == JS_REQSTATUS_WAITCGI && pItem->pRspString) {
			////send header
			DBGPRINT("TMP:sendheader=%s\n",pItem->pRspString);
			nSent = JS_UTIL_TCP_SendTimeout(pItem->nInSock, pItem->pRspString + pItem->nRspOffset, pItem->nRspSize - pItem->nRspOffset, 20);
#ifdef	JS_CONFIG_DUMP_SEND
			{
				JS_HANDLE  hTmpHead = JS_UTIL_OpenDumpFile("c:\\temp\\rhead_", pReq->pURL, "&range=", "&");
				if (hTmpHead) {
					JS_UTIL_FileWriteBlocking(hTmpHead, pItem->pRspString, strlen(pItem->pRspString));
					JS_UTIL_FileDestroy(&hTmpHead);
				}
			}
#endif
			if(nSent<0) {
				DBGPRINT("turbogate worker: broken Req TCP\n");
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}else if(nSent>0) {
				pItem->nRspOffset += nSent;
				if(pItem->nRspOffset>=pItem->nRspSize) {
					////check eof
					if(pItem->nRangeLen<=0 && pItem->nChunked==0) {
						JS_TurboGate_ResetItem(pItem);
						pItem->nKeepAliveCnt = 100;
						DBGPRINT("TMP: turbogate headersent zero ok %s\n",pItem->pRspString);
					}else if(JS_UTIL_HTTP_IsHeadMethod(pReq)) {
						JS_TurboGate_ResetItem(pItem);
						//DBGPRINT("TMP: turbogate headersent head ok %s\n",pItem->pRspString);
					}					
					pItem->nRspSize = 0;
					pItem->nRspOffset = 0;
					JS_FREE(pItem->pRspString);
					pItem->pRspString = NULL;
				}
			}
		}
		////4. send some body data, if available after header sent
		else if(pReq->nQueueStatus == JS_REQSTATUS_WAITCGI) {
			int nBurstCnt;
			nRQStatus = 0;
			nSent = 0;
			for(nBurstCnt=0; nBurstCnt<JS_CONFIG_MAX_BURSTCOUNT;nBurstCnt++) {
				nAvailable = JS_ReorderingQ_PumpOutGetAvailSize(pItem->hReorderingQueue,&pBuff,JS_CONFIG_NORMAL_SENDBLOCKSIZE>>2);
				if(nAvailable>0) {
					nSent = JS_UTIL_TCP_SendTimeout(pItem->nInSock,pBuff,nAvailable,20);
					if(nSent<0) {
						DBGPRINT("(id=%d) turbogate worker: broken req socket\n",pItem->nWorkID);
						nRet = -1;
						goto LABEL_CATCH_ERROR;
					}else if(nSent>0) {
#ifdef	JS_CONFIG_DUMP_SEND
						if(pItem->hDumpFile) {
							JS_UTIL_FileWriteBlocking(pItem->hDumpFile, pBuff, nSent);
						}
#endif
						nRQStatus = JS_ReorderingQ_PumpOutComplete(pItem->hReorderingQueue,nSent);
						//DBGPRINT("TMP: turbogate bodysent ok sent=%d,ret=%d\n",nSent,nRQStatus);
					}
				}else
					break;
				////no need to lock for Q when JS_ReorderingQ_GetRemainDataNotSent
				if(nRQStatus== JS_RET_REORDERINGQ_EOF || JS_ReorderingQ_GetRemainDataNotSent(pItem->hReorderingQueue)<=0) {
					////all data is sent to client socket, change status to idle
					JS_TurboGate_ResetItem(pItem);
					DBGPRINT("(id=%d) TMP: EOF !!!!\n",pItem->nWorkID);
					break;
					//DBGPRINT("TMP: turbogate worker: end of rsp, rqstatus=%d\n",nRQStatus);
				}else if(nRQStatus== JS_RET_NEEDCHECKSPEED) { ////check streaming speed is good, if bad, increase connection
					JS_TurboGate_CheckStreamingSpeed(pItem);
				}
			}

			if(nSent==0 ) {
				UINT64 nNotRcvd = JS_ReorderingQ_GetRemainDataNotRcvd(pItem->hReorderingQueue);
				if(nNotRcvd<=0) {
					if(JS_ReorderingQ_GetRemainDataNotSent(pItem->hReorderingQueue)>0) {
						DBGPRINT("(id=%d) TMP: Why? %llu\n",pItem->nWorkID,JS_ReorderingQ_GetRemainDataNotSent(pItem->hReorderingQueue));
					}
				}else {
					;// JS_UTIL_FrequentDebugMessage(19, 10, "(id=%d) TMP: notrcvd=%llu, notsent=%llu\n", pItem->nWorkID, nNotRcvd, JS_ReorderingQ_GetRemainDataNotSent(pItem->hReorderingQueue));
				}
			}
		}
		////5. check end of rsp
		if(nOldStatus!=JS_REQSTATUS_WAITREQ && pReq->nQueueStatus == JS_REQSTATUS_WAITREQ) {
			////clear reordering queue for next request
			JS_ReorderingQ_Reset(pItem->hReorderingQueue);
			////clear newreq flag to block rsp->httpclientjob until new request from clientsocket
			pItem->nGetNewReq = 0;
#ifdef	JS_CONFIG_DUMP_SEND
			if (pItem->hDumpFile) {
				JS_UTIL_FileDestroy(&pItem->hDumpFile);
			}
#endif
#if (JS_NOT_USE_KEEPALIVE_IN_TURBO_MODE==1)
			nRet = -1;
			goto LABEL_CATCH_ERROR;
#endif
			DBGPRINT("TMP:turboitem busy->idle host=%s, chunked=%d, rspsize=%llu\n",pReq->pHost,pItem->nChunked,pItem->nRangeLen);
		}
		////6. check whether there is new request from client or not
		if(pReq->nQueueStatus == JS_REQSTATUS_WAITREQ) {
			nIsNew = 0;
			pBuff = JS_SimpleQ_PreparePumpOut(pReq->hQueue, 0, &nAvailable, "\r\n\r\n", 4, &nIsNew);
			if(pBuff && nIsNew) {
				pItem->nKeepAliveCnt=0;
				pItem->nRspCount++;
				pItem->pReq = JS_UTIL_HTTP_CheckRequestPacket(pBuff, nAvailable, pReq);
				if(pItem->pReq==NULL) {
					DBGPRINT("turbogate proxy:no mem error(req)\n");
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				JS_SimpleQ_FinishPumpOut(pReq->hQueue,nAvailable);
				if(JS_UTIL_HTTP_CheckReqMethod(pItem->pReq,JS_HTTPSERVER_ALLOWED_METHODS)==0) {
					DBGPRINT("turbogate proxy:can't process req method error(%s)\n",pReq->strMethod);
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				if(JS_UTIL_HTTP_IsPostMethod(pReq) && pReq->nRangeLen>0) {
					JS_SimpleQ_ResetTotallSize(pReq->hQueue,pReq->nRangeLen);
				}
				JS_ReorderingQ_SetTotallSize(pItem->hReorderingQueue,JS_REORDERINGQ_UNKNOWNSIZE);
				////set newreq flag to start httpclient
				pItem->nGetNewReq = 1;
				DBGPRINT("turbogate proxy:keepalive rebirth sock=%d,http://%s(rspcount=%d)\n",pItem->nInSock,pReq->pHost,pItem->nRspCount);
			}
			else if(pItem->nKeepAliveCnt>(JS_CONFIG_MAX_MEDIAPROXYKEEPCNT<<1)) {
				DBGPRINT("turbogate proxy:close connection keepalive timeout sock=%d,%s\n",pItem->nInSock,pReq->pURL);
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
		}
		////7. polling or sleep a while
		rcTime.tv_sec = 0;
		rcTime.tv_usec = 20000;
		memcpy((char*)&rcTmpRDSet,(char*)pItem->pReadFdSet,sizeof(JS_FD_T));
		memcpy((char*)&rcTmpWRSet,(char*)pItem->pWriteFdSet,sizeof(JS_FD_T));
		if(JS_TurboGate_CheckHttpClientSocket(pItem)<0) {
			nSelectRet = 0;
			JS_UTIL_Usleep(20000);
		}else {
			//DBGPRINT("turbogate worker:selecting %d\n",pItem->nMaxFd);
			nSelectRet = select(pItem->nMaxFd+1,&rcTmpRDSet, &rcTmpWRSet, NULL, &rcTime);
		}
		nTickCounter++;
		if(nTickCounter>50) {
			nNeedCheckTCP = 1;
			nTickCounter = 0;
		}else {
			nNeedCheckTCP = 0;
		}
		if(nSelectRet<0) {
			nRet = -1;
		#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
			errno = WSAGetLastError();
		#endif
			DBGPRINT("turbogate worker:exit cause select error %d\n",errno);
			goto LABEL_CATCH_ERROR;
		}else if(nSelectRet==0) {
			memset((char*)&rcTmpRDSet,0,sizeof(JS_FD_T));
			memset((char*)&rcTmpWRSet,0,sizeof(JS_FD_T));
			if(pReq->nQueueStatus == JS_REQSTATUS_WAITREQ) {
				pItem->nKeepAliveCnt++;
			}
		}
	}
LABEL_CATCH_ERROR:
	return NULL;
}

static int JS_TurboGate_CheckStreamingSpeed(JS_TurboGate_SessionItem * pItem) 
{
	UINT32 nInSpeed;
	UINT32 nOutSpeed;
	JS_ReorderingQ_GetSpeed(pItem->hReorderingQueue,&nInSpeed,&nOutSpeed,NULL);
	if(pItem->nConnectionNum<JS_UTIL_GetConfig()->nMaxTurboConnection && (nInSpeed-(nInSpeed>>4)) < nOutSpeed) {
	//if(pItem->nConnectionNum<JS_UTIL_GetConfig()->nMaxTurboConnection) {
		if(nInSpeed<JS_CONFIG_MAX_IN_SPEED) {
			//if(pItem->pReq && pItem->pReq->pURL && JS_MediaProxy_CheckStreamInfoToAddConnection(pItem->nHostIP,pItem->pReq->pURL,pItem->nConnectionNum))
			if(pItem->pReq && pItem->pReq->pURL && JS_ThreadPool_CheckIsFirstWork(pItem->hRootWorkQ,pItem->nWorkID))
			//if(1)
				JS_TurboGate_ChangeConnectionNumber(pItem,pItem->nConnectionNum+1);
		}
	}
	else if(pItem->nConnectionNum>2 && JS_CONFIG_MAX_OUT_SPEED > nOutSpeed){
#if (JS_TEST_NO_REDUCE_CONT==1)
		;
#else
		JS_TurboGate_ChangeConnectionNumber(pItem, pItem->nConnectionNum - 1);
#endif
	}
	return 0;
}

static int JS_TurboGate_CheckHttpClientSocket(JS_TurboGate_SessionItem * pItem)
{
	int nRet = -1;
	int nCnt;
	int nCheckCnt = 0;
	JS_HANDLE hClient;
	for (nCnt = 0; nCnt<JS_CONFIG_MAX_TURBOCONNECTION; nCnt++) {
		if (pItem->arrCont[nCnt].nUse) {
			hClient = pItem->arrCont[nCnt].hHttp;
			if (JS_UTIL_CheckSocketValidity(JS_SimpleHttpClient_GetSocket(hClient)) >= 0) {
				nRet = 1;
				break;
			}
			nCheckCnt++;
		}
		if (nCheckCnt >= pItem->nConnectionNum)
			break;		
	}
	return nRet;
}


int JS_MediaProxy_CheckNeedTurbo(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp)
{
	int nRet = 1;
	int nURLLen;
	int nCTLen;
	const char * strURL;
	const char * strContentType;
	int nIsVideo = 0;

	if(pReq==NULL || pRsp==NULL)
		return nRet;
	strURL = pReq->pURL;
	strContentType = JS_UTIL_GetHTTPResponseHeader(pRsp,"Content-Type");
	if(strURL && strContentType) {
		nURLLen = strlen(strURL);
		nCTLen = strlen(strContentType);
		//DBGPRINT("TMP:Type=%s %llu\n",strContentType,pRsp->nRangeLen);
		if(nURLLen<10 || nCTLen < 6)
			return nRet;
		if(JS_UTIL_StrCmp(strContentType,"video/",6,6,1)==0) {
			nIsVideo = 1;
		}
		if(JS_UTIL_FindPattern(strContentType, "stream", nCTLen, 6, 1)>=0) {
			int nRet = 0;
			if(JS_UTIL_FindPattern(strURL, "video", nURLLen, 5, 1)>=0)
				nIsVideo = 1;
		}
	}
	if(pRsp->nRangeLen>JS_CONFIG_MIN_BIGFILE)
		return JS_UTIL_GetConfig()->nMaxTurboConnection;
	if(nIsVideo && pRsp->nRangeLen>JS_CONFIG_MIN_TURBOVIDEOSIZE) {
		if(pRsp->nChunked==0)
			return JS_UTIL_GetConfig()->nMaxTurboConnection;
	}
	return nRet;
}

#endif