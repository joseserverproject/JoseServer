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
Decide the turboitem connection number automatically
TBD
1. determine connection number based on the content's bitrate
2. check content type
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

//////////////////////////////////////////////////
///macro definitions
#define JS_QS_CT_VIDEO		1
#define JS_QS_CT_BIGDATA	2
#define JS_QS_MAX_NUM		200

//////////////////////////////////////////////////
///local types
typedef struct JS_AutoTrafficItemTag
{
	UINT32 nDomainIP;
	UINT32 nInSpeed;
	UINT32 nOutSpeed;
	UINT32 nQRate;
	UINT32 nConnection;
	UINT32 nMinConnection;
	UINT32 nErrorTick;
	int    nSpeedStatus;
}JS_AutoTrafficItem;

typedef struct  JS_AutoTrafficGlobalTag
{
	JS_HANDLE hQSLock;
	JS_HANDLE hHostCache;
}JS_AutoTrafficGlobal;

//////////////////////////////////////////////////
///local variables
static int g_nQualityInit = 0;
static JS_AutoTrafficGlobal g_rcQuality;

//////////////////////////////////////////////////
///function declarations
static int JS_AutoTrafficControl_CheckContentType(JS_HTTP_Request * pReq,JS_HTTP_Response * pRsp);
static int JS_AutoTrafficControl_CheckInitialStatus(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp);
static int JS_AutoTrafficControl_FindCallback (void * pOwner, void * pData, void * pComData);
static int JS_AutoTrafficControl_HashCallback (void * pOwner, void * pData, void * pCompData);
static int JS_AutoTrafficControl_RemoveCallback (void * pOwner, void * pData);
static int JS_AutoTrafficControl_FromString (void * pOwner, char * pBuffer, int nLen, JS_POOL_ITEM_T * pItem);
static int JS_AutoTrafficControl_ToString (void * pOwner, JS_POOL_ITEM_T * pItem, char * pBuffer, int nLen);
__inline static int JS_AutoTrafficControl_SetSpeedStatus(JS_AutoTrafficItem * pItem, int nStatus);
__inline static UINT32 JS_AutoTrafficControl_ExtractCClassIP(UINT32 nIP);

//////////////////////////////////////////////////
///function implementations

int JS_AutoTrafficControl_Init(void)
{
	int nRet =0;
	if(g_nQualityInit==0) {
		g_nQualityInit = 1;
	}
	return nRet;
}

int JS_AutoTrafficControl_Clear(void)
{
	int nRet = 0;
	if(g_nQualityInit) {
		g_nQualityInit = 0;
		memset(&g_rcQuality,0,sizeof(g_rcQuality));
	}
	return nRet;
}

int JS_AutoTrafficControl_EstimateBestConnectionNumber(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp)
{
	int nConNum = 1;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	JS_AutoTrafficItem * pItem =  NULL;
	int nInitial;

#if (JS_CONFIG_FRONTGATE_TEST==1)
	return 1;
#endif
	if(pReq==NULL || pRsp==NULL)
		return 1;
	pReq->nQSLog = 0;
	nInitial = JS_AutoTrafficControl_CheckInitialStatus(pReq,pRsp);
	if(nInitial==JS_QS_RET_NEEDACCEL)
		nConNum = JS_UTIL_GetConfig()->nMaxTurboConnection;
	return nConNum;
}

int JS_AutoTrafficControl_CheckQueueSpeed(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp)
{
	int nRet = 0;
	return nRet;
}

int JS_AutoTrafficControl_ReportError(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp, int nError)
{
	int nRet = 0;
	return nRet;
}

///////////////////////////////////////////////////////////////////////////////////////////////
//static function

static int JS_AutoTrafficControl_RemoveCallback (void * pOwner, void * pData)
{
	return 0;
}

static int JS_AutoTrafficControl_HashCallback (void * pOwner, void * pData, void * pCompData)
{
	int nRet = 0;
	JS_AutoTrafficItem * pItem =  NULL;
    void * pKey = NULL;
	if(pData != NULL) {
		pItem =  (JS_AutoTrafficItem *)pData;
		if(pItem)
			pKey = &pItem->nDomainIP;
	}else {
		pKey = pCompData;
	}
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,4,0);
	else
		return 0;
}

static int JS_AutoTrafficControl_FindCallback (void * pOwner, void *pData, void * pCompData)
{
	int nRet = 0;
	JS_POOL_ITEM_T * pPoolItem = (JS_POOL_ITEM_T *)pData;
	JS_AutoTrafficItem * pItem =  NULL;
	UINT32 nDomainIP_Comp = 0;
	UINT32 nDomainIP_Org = 0;

	pItem =  (JS_AutoTrafficItem *)pData;
	nDomainIP_Org = JS_AutoTrafficControl_ExtractCClassIP(pItem->nDomainIP);
	nDomainIP_Comp = JS_AutoTrafficControl_ExtractCClassIP(*(UINT32*)pCompData);
	if(nDomainIP_Org == nDomainIP_Comp)
		nRet = 1;
	return nRet;
}


static int JS_AutoTrafficControl_FromString (void * pOwner, char * pBuffer, int nLen, JS_POOL_ITEM_T * pItem)
{
	int nRet = 0;
	return nRet;
}

static int JS_AutoTrafficControl_ToString (void * pOwner, JS_POOL_ITEM_T * pItem, char * pBuffer, int nLen)
{
	int nRet = 0;
	return nRet;
}

__inline static UINT32 JS_AutoTrafficControl_ExtractCClassIP(UINT32 nIP)
{
	return nIP&htonl(0xFFFFFF00);
}

static int JS_AutoTrafficControl_CheckContentType(JS_HTTP_Request * pReq,JS_HTTP_Response * pRsp)
{
	int nURLLen;
	int nCTLen;
	const char * strURL;
	const char * strContentType;

	if(pReq==NULL || pRsp==NULL)
		return JS_QS_RET_UNKNOWN;
	strURL = pReq->pURL;
	strContentType = JS_UTIL_GetHTTPResponseHeader(pRsp,"Content-Type");
	if(strURL && strContentType) {
		nURLLen = strlen(strURL);
		nCTLen = strlen(strContentType);
		//DBGPRINT("TMP:Type=%s %llu\n",strContentType,pRsp->nRangeLen);
		if(nURLLen<10 || nCTLen < 6)
			return JS_QS_RET_UNKNOWN;
		if(JS_UTIL_StrCmp(strContentType,"video/",6,6,1)==0) {
			return JS_QS_CT_VIDEO;
		}
		if(JS_UTIL_FindPattern(strContentType, "stream", nCTLen, 6, 1)>=0) {
			int nRet = 0;
			if(JS_UTIL_FindPattern(strURL, "video", nURLLen, 5, 1)>=0)
				nRet = JS_QS_CT_VIDEO;
			if(nRet == JS_QS_CT_VIDEO)
				return nRet;
		}
	}
	if(pRsp->nRangeLen>JS_CONFIG_MIN_BIGFILE)
		return JS_QS_CT_BIGDATA;
	else
		return JS_QS_RET_UNKNOWN;
}

static int JS_AutoTrafficControl_CheckInitialStatus(JS_HTTP_Request * pReq, JS_HTTP_Response * pRsp)
{
	int nRet = JS_QS_RET_UNKNOWN;
	int nQS = 0;

	if(pReq==NULL)
		return nRet;
#if 0
	if(JS_UTIL_FindPattern(pReq->pURL,"videoplayback?",0,0,0)>0)
		return JS_QS_RET_NEEDACCEL;
	else
		return JS_QS_RET_UNKNOWN;
#endif
	nQS = JS_AutoTrafficControl_CheckContentType(pReq,pRsp);
	pReq->nQSLog = nQS;
#if 1
	if(nQS==JS_QS_CT_VIDEO && pRsp->nRangeLen<JS_CONFIG_MIN_TURBOVIDEOSIZE) {
		//DBGPRINT("TMP: too small video bypass %llu\n",pRsp->nRangeLen);
		return nRet;
	}
#endif
	if(pRsp->nChunked)
		return nRet;
	if(pReq->nQSLog==JS_QS_RET_UNKNOWN)
		return nRet;
	nRet = JS_QS_RET_NEEDACCEL;
	return nRet;
}

__inline static int JS_AutoTrafficControl_SetSpeedStatus(JS_AutoTrafficItem * pItem, int nStatus)
{
	int nRet = 0;
	if(pItem==NULL)
		return 0;
	if(nStatus<0)
		pItem->nSpeedStatus--;
	else if(nStatus>0)
		pItem->nSpeedStatus++;
	if(pItem->nSpeedStatus>JS_CONFIG_MAX_STATUS_CHANGE||pItem->nSpeedStatus<-JS_CONFIG_MAX_STATUS_CHANGE) {
		pItem->nSpeedStatus = 0;
		nRet = 1;
	}
	return nRet;
}

#endif