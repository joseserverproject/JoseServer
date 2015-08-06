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
Utility functions for http
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_ThreadPool.h"
#include "JS_DataStructure.h"

//////////////////////////////////////////////////////
//macro start
#define MAX_STRING_MAP		100
#define MAX_RANGE_STRING	200

//////////////////////////////////////////////////////
//local types
typedef struct JS_UtilHttpSmallMapItemTag {
	char * pKey;
	char * pVal;
	int nKeyLen;
	int nValLen;
}JS_UtilHttpSmallMapItem;

//////////////////////////////////////////////////////
//inner functions declarations
static JS_HTTP_Request	* JS_UTIL_HTTP_CheckReqWithOldInstance(const char * strReq, int nReqLen, JS_HTTP_Request * pOldReq);
static const char * JS_UTIL_HTTP_FieldMap_Get(JS_HANDLE hList, const char * strKey, int nArrSize);
static int JS_UTIL_HTTP_FieldMap_Set(JS_HANDLE hList, const char * strKey, const char * strVal, int * pnArrSize);
static int JS_UTIL_HTTP_FieldMap_IsSame(JS_HANDLE hList, const char * strKey, const char * strVal, int nArrSize);
static int JS_UTIL_HTTP_FieldMap_ToString(const char * strFirstline, JS_HANDLE hList, char ** ppRet, int nArrSize);

/////////////////////////////////////////////////////////////
///http functions start
int JS_UTIL_HTTP_FiledsRmFunc (void * pOwner, void * pData)
{
	JS_UtilHttpSmallMapItem * pItem = (JS_UtilHttpSmallMapItem * )pData;
	if(pItem) {
		if(pItem->pKey)
			JS_FREE(pItem->pKey);
		if(pItem->pVal)
			JS_FREE(pItem->pVal);
		JS_FREE(pItem);
	}
	return 0;
}

HTTPSIZE_T	JS_UTIL_HTTP_ParseRange(const char * strVal, HTTPSIZE_T * pStart, HTTPSIZE_T * pEnd)
{
	HTTPSIZE_T	nLen = 0;
	int nStrLen;
	int nCnt;
	int nStatus = 0;
	char strTemp[204];
	int nStrCnt;
	nStrLen = strlen(strVal);
	if(nStrLen>MAX_RANGE_STRING)
		nStrLen = MAX_RANGE_STRING;
	for(nCnt=0; nCnt<nStrLen; nCnt++) {
		switch(nStatus) {
			case 0:
				if(strVal[nCnt]=='=' || strVal[nCnt]==' ') {
					nStrCnt = 0;
					nStatus = 1;
				}
				break;
			case 1:
				if(strVal[nCnt]=='-') {
					strTemp[nStrCnt] = 0;
					*pStart = JS_STRTOULL(strTemp,NULL,10);
					nStrCnt = 0;
					nStatus = 2;
				}else
					strTemp[nStrCnt++] = strVal[nCnt];
				break;
			case 2:
				if(nCnt==nStrLen-1) {
					strTemp[nStrCnt++] = strVal[nCnt];
					strTemp[nStrCnt] = 0;
					*pEnd = JS_STRTOULL(strTemp,NULL,10);
					nStrCnt = 0;
					nStatus = 3;
				}else
					strTemp[nStrCnt++] = strVal[nCnt];
				break;
		}
	}
	if(nStatus==3) {
		if(*pEnd>=*pStart)
			nLen = *pEnd-*pStart+1;
		else
			nLen = 0;
	}
	return nLen;
}

char * JS_UTIL_HTTPExtractHostFromURL(const char * pURL, char * pBuff, int nBuffLen)
{
	int nTokenIndex;
	const char * pStart;
	int nLen;

	nLen = strlen(pURL);
	if(nLen<3)
		return NULL;
	if(pURL[0]=='h' && pURL[1]=='t')
		pStart = pURL+7;
	else
		pStart = pURL;
	nTokenIndex = 0;
	nTokenIndex = JS_UTIL_StrToken(pStart,0,nTokenIndex,"/",pBuff,nBuffLen);
	if(nTokenIndex<=0) {
		JS_UTIL_StrCopySafe(pBuff,nBuffLen,pStart,0);
	}
	return pBuff;
}

int JS_UTIL_ParseHTTPHost(const char * pHost, UINT32 * pnTargetIP, UINT16 * pnTargetPort)
{
	int nTokenIndex;
	char strBuff[256];
	UINT16 nPort;
	UINT32 nIP;
	char * pTarget;
	int nRet=0;
	int nCnt;
	int nLen;

	nTokenIndex = 0;
	nTokenIndex = JS_UTIL_StrToken(pHost,0,nTokenIndex,":",strBuff,256);
	nTokenIndex = JS_UTIL_StrToken(pHost,0,nTokenIndex,":",strBuff,256);
	if(nTokenIndex>0) {
		nPort = atoi(strBuff);
		nTokenIndex = 0;
		JS_UTIL_StrToken(pHost,0,nTokenIndex,":",strBuff,256);
		pTarget = strBuff;
	}else {
		nPort = 80;
		pTarget = (char*)pHost;
	}
	nLen = strlen(pTarget);
	for(nCnt=0; nCnt<nLen; nCnt++) {
		if('a'<=pTarget[nCnt] && pTarget[nCnt]<='z')
			break;
		if('A'<=pTarget[nCnt] && pTarget[nCnt]<='Z')
			break;
	}
	if(nCnt==nLen) {
		struct in_addr rcIn;
		rcIn.s_addr = inet_addr(pTarget);
		nIP = (UINT32)rcIn.s_addr;
	}else {
		nIP = 0;
	}
	if(nRet==0) {
		if(pnTargetIP)
			*pnTargetIP = nIP;
		if(pnTargetPort)
			*pnTargetPort = nPort;
	}
	return nRet;
}

static void JS_UTIL_HTTP_DeleteRequestEx(JS_HTTP_Request	* pReq, int nResuse)
{
	if(pReq==NULL) {
		DBGPRINT("DeleteRequest: no req nulll\n");
		return;
	}
	if(pReq->hFieldList)
		JS_List_Destroy(pReq->hFieldList);
	if(pReq->pURL)
		JS_FREE(pReq->pURL);
	if(pReq->pHost)
		JS_FREE(pReq->pHost);
	if(nResuse) {
		pReq->hFieldList = NULL;
		pReq->pURL = NULL;
		pReq->nFieldNum = 0;
		pReq->nRangeEndOffset = 0;
		pReq->nRangeLen = 0;
		pReq->nRangeStartOffset = 0;
		pReq->nQSLog = 0;
		pReq->pHost = NULL;
		pReq->nQueueStatus = 0;
		pReq->nIsMultiPartReq = 0;
	}else {
		if(pReq->hQueue)
			JS_SimpleQ_Destroy(pReq->hQueue);
		JS_FREE(pReq);
	}
}

void JS_UTIL_HTTP_DeleteRequest(JS_HTTP_Request	* pReq)
{
	JS_UTIL_HTTP_DeleteRequestEx(pReq,0);
}

void JS_UTIL_HTTP_PrintRequest(JS_HTTP_Request	* pReq)
{
	void * pData;
	JS_HANDLE hPosRaw;
	JS_UtilHttpSmallMapItem * pItem;
	if(pReq==NULL) {
		DBGPRINT("DeleteRequest: no req nulll\n");
		return;
	}
	if(pReq->hFieldList) {
		hPosRaw = NULL;
		while(1) {
			hPosRaw = JS_List_IterateRaw(pReq->hFieldList,hPosRaw,&pData);
			if(hPosRaw==NULL)
				break;
			if(pData) {
				pItem = (JS_UtilHttpSmallMapItem *)pData;
				DBGPRINT("Key=%s>>Val=%s\n",pItem->pKey,pItem->pVal);
			}
		}
	}
	if(pReq->pURL)
		DBGPRINT("URL=%s\n",pReq->pURL);
}

int JS_UTIL_HTTP_IsHeadMethod(JS_HTTP_Request * pReq)
{
	if(JS_UTIL_StrCmp(pReq->strMethod,"HEAD",4,4,1)==0)
		return 1;
	else
		return 0;
}

int JS_UTIL_HTTP_IsPostMethod(JS_HTTP_Request * pReq)
{
	if(JS_UTIL_StrCmp(pReq->strMethod,"POST",4,4,1)==0)
		return 1;
	else
		return 0;
}

JS_HTTP_Request	* JS_UTIL_HTTP_PrepareRequest(void)
{
	int nRet = 0;
	////alloc memory items
	JS_HTTP_Request	* pRequest = (JS_HTTP_Request	*) JS_ALLOC(sizeof(JS_HTTP_Request));
	if(pRequest==NULL) {
		nRet = -1;
		DBGPRINT("prepare req:no req item mem error\n");
		goto LABEL_EXIT_PREPAREREQ;
	}
	memset((char*)pRequest,0,sizeof(JS_HTTP_Request));
	pRequest->hQueue = JS_SimpleQ_Create(0,1);
	if(pRequest->hQueue==NULL) {
		nRet = -1;
		DBGPRINT("prepare req:no queue mem error\n");
		goto LABEL_EXIT_PREPAREREQ;
	}
LABEL_EXIT_PREPAREREQ:
	if(nRet<0) {
		if(pRequest->hQueue) {
			JS_SimpleQ_Destroy(pRequest->hQueue);
		}
		JS_FREE(pRequest);
		pRequest = NULL;
	}
	return pRequest;
}

JS_HTTP_Request * JS_UTIL_HTTP_BuildDefaultReq(JS_HTTP_Request * pReq, const char * strURL, const char * strData, const char * strMethod)
{
	int nRet = 0;
	char pTempHost[256];
	char * pTok;
	int nIsPost = 0;
	int nTmpLen, nTmpURLLen;

	if(pReq==NULL)
		pReq = JS_UTIL_HTTP_PrepareRequest();
	if(pReq==NULL) {
		nRet = -1;
		DBGPRINT("httpreq build: mem error(preq)\n");
		goto LABEL_CATCH_ERROR;
	}
	if(strMethod) {
		JS_UTIL_StrCopySafe(pReq->strMethod,sizeof(pReq->strMethod),strMethod,0);
		if(JS_UTIL_StrCmpRestrict(strMethod,"POST",0,4,1)==0) {
			nIsPost = 1;
		}
	}
	if(nIsPost)
		pReq->pURL = JS_UTIL_StrDup(strURL);
	else {
		nTmpURLLen = strlen(strURL);
		if(strData)
			nTmpLen = nTmpURLLen+strlen(strData)+8;
		else
			nTmpLen = nTmpURLLen;
		pReq->pURL = (char*)JS_ALLOC(nTmpLen+4);
		if(pReq->pURL) {
			if(strData) {
				if(JS_UTIL_FindPattern(strURL,"?",nTmpURLLen,1,0)>0)
					JS_STRPRINTF(pReq->pURL,nTmpLen+1,"%s&%s",strURL,strData);
				else
					JS_STRPRINTF(pReq->pURL,nTmpLen+1,"%s?%s",strURL,strData);
			}else
				JS_STRPRINTF(pReq->pURL,nTmpLen+1,"%s",strURL);
		}
	}
	if(pReq->pURL==NULL){
		nRet = -1;
		DBGPRINT("httpreq build: mem error(url)\n");
		goto LABEL_CATCH_ERROR;
	}

	pTok = JS_UTIL_HTTPExtractHostFromURL(strURL, pTempHost, 256);
	if(pTok==NULL) {
		nRet = -1;
		DBGPRINT("httpreq build: wrong url error\n");
		goto LABEL_CATCH_ERROR;
	}
	pReq->pHost = JS_UTIL_StrDup(pTok);

	pReq->nURLLen = strlen(pReq->pURL);
	pReq->nResOffset = JS_UTIL_FindPattern(strURL+7,"/",pReq->nURLLen-7,1,0);
	pReq->nResOffset += 7;
	pReq->hFieldList = JS_List_Create(pReq,JS_UTIL_HTTP_FiledsRmFunc);
	pReq->nFieldNum = 0;
	pReq->nUserAgent = 0;
	if(pReq->pHost==NULL || pReq->hFieldList==NULL) {
		nRet = -1;
		DBGPRINT("httpreq build: mem error(phost)\n");
		goto LABEL_CATCH_ERROR;
	}
	if(nIsPost) {
		////push data into mem
		JSUINT nTmpDataLen = strlen(strData);
		nRet = JS_SimpleQ_PushPumpIn(pReq->hQueue,strData,nTmpDataLen);
		if(nRet<0) {
			DBGPRINT("httpreq build: mem error(push queue)\n");
			goto LABEL_CATCH_ERROR;
		}
		JS_SimpleQ_ResetTotallSize(pReq->hQueue,nTmpDataLen);
	}
	JS_UTIL_FixHTTPRequest(pReq,"User-Agent",JS_CONFIG_USERAGENT,0);
	JS_UTIL_FixHTTPRequest(pReq,"Host",pReq->pHost,0);
	JS_UTIL_FixHTTPRequest(pReq,"Connection","Keep-Alive",0);
	JS_UTIL_FixHTTPRequest(pReq,"Accept","*/*",0);
	JS_UTIL_ParseHTTPHost(pReq->pHost,&pReq->nTargetIP,&pReq->nTargetPort);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pReq) {
			JS_UTIL_HTTP_DeleteRequest(pReq);
			pReq = NULL;
		}
	}
	return pReq;
}

JS_HTTP_Request	* JS_UTIL_HTTP_CheckRequestPacket(const char * strReq, int nReqLen, JS_HTTP_Request * pOldItem)
{
	#define HTTP_REQ_METHOD 0
	#define HTTP_REQ_URL	1
	#define HTTP_REQ_VERSION	2
	#define HTTP_REQ_HEAD_KEY	3
	#define HTTP_REQ_HEAD_VAL	4

	JS_HTTP_Request	* pRequest;
	int nTokenIndex;
	int nNext;
	char * pBuffer;
	char * pRes = NULL;
	char * pHost = NULL;
	char * pURL = NULL;
	char * pKey;
	char * pVal;
	char * pRedirectURL = NULL;
	int nKeyCnt;
	int nValOfMinus;
	int nStatus=HTTP_REQ_METHOD;
	char	strDeli[32] = " \t\r\n";
	unsigned int nBuffSize;
	UINT64 nRangeLen=0;
	int nBodyOffset;

	if(nReqLen<=0)
		nReqLen = strlen(strReq);
	if(pOldItem==NULL) {
		////alloc memory items
		pRequest = (JS_HTTP_Request	*) JS_ALLOC(sizeof(JS_HTTP_Request));
		if(pRequest==NULL) {
			DBGPRINT("Can't make req item\n");
			goto LABEL_EXIT_CHECKREQ;
		}
		memset((char*)pRequest,0,sizeof(JS_HTTP_Request));
	}else {
		pRequest = pOldItem;
		JS_UTIL_HTTP_DeleteRequestEx(pRequest,1);
	}

	nBodyOffset = nReqLen;
	nReqLen = nBodyOffset;
	nBuffSize = nReqLen;

	pRequest->nReqLen = nReqLen;
	pRequest->hFieldList = JS_List_Create(pRequest,JS_UTIL_HTTP_FiledsRmFunc);

	pBuffer = (char*)JS_ALLOC(nReqLen+64);
	if(pBuffer==NULL) {
		DBGPRINT("JS_UTIL_HTTPCheckIncommingRequest: Can't alloc\n");
		goto LABEL_EXIT_CHECKREQ;
	}
	if(pRequest->hQueue==NULL) {
		pRequest->hQueue = JS_SimpleQ_Create(0,1);
		if(pRequest->hQueue==NULL) {
			DBGPRINT("JS_UTIL_HTTPCheckIncommingRequest: Can't make queue\n");
			goto LABEL_EXIT_CHECKREQ;
		}
	}
	nTokenIndex = 0;
	nKeyCnt = 0;
	while(1) {
		nNext = JS_UTIL_StrToken(strReq, nReqLen, nTokenIndex, strDeli, pBuffer, nReqLen);
		if(nNext<0)
			break;
		if(nStatus==HTTP_REQ_METHOD) {
			JS_UTIL_StrCopySafe(pRequest->strMethod,16,pBuffer,0);
			//DBGPRINT("METHOD=%s\n",pBuffer);
			nStatus = HTTP_REQ_URL;
		}else if(nStatus==HTTP_REQ_URL) {
			if(pBuffer[0]=='h' && pBuffer[1]=='t' && pBuffer[2]=='t'&& pBuffer[3]=='p') {
				pURL = JS_UTIL_StrDup(pBuffer);
			}else {
				int nRedirectIndex;
				int nRedirectBy1 = 0;
				pRes = JS_UTIL_StrDup(pBuffer);
				nRedirectIndex = JS_UTIL_FindPattern(pRes,"turbogate201?url=",0,16,0);
				if(nRedirectIndex>=0)
					nRedirectBy1 = 1;
				else
					nRedirectIndex = JS_UTIL_FindPattern(pRes,"turbogate211?url=",0,16,0);
				if(nRedirectIndex>=0) {
					int nStrLen = strlen(pRes);
					pRedirectURL = (char*)JS_ALLOC(nStrLen*2);
					if(pRedirectURL) {
						if(nRedirectBy1)
							pRequest->nExplicitProxyFlag = 100;
						else
							pRequest->nExplicitProxyFlag = 1;
						JS_UTIL_StrURLDecode(pRes+nRedirectIndex+17,pRedirectURL,nStrLen*2);
					}
				}
			}
			nStatus=HTTP_REQ_VERSION;
		}else if(nStatus==HTTP_REQ_VERSION) {
			////check validity
			if(JS_UTIL_StrCmp(pBuffer,"http/1",0,6,1)!=0) {
				DBGPRINT("check request: wrong http version %s\n",pBuffer);
				break;
			}
			nStatus=HTTP_REQ_HEAD_KEY;
			////change deilim for "key: value\r\n" map
			JS_STRCPY(strDeli,"\r\n:");
		}else if(nStatus==HTTP_REQ_HEAD_KEY){
			pKey = JS_UTIL_StrDup(pBuffer);
			if(pKey) {
				JS_UTIL_TrimWhiteSpace(pKey," \r\n:");
			}
			nStatus=HTTP_REQ_HEAD_VAL;
			JS_STRCPY(strDeli,"\r\n");
		}else if(nStatus==HTTP_REQ_HEAD_VAL) {
			if(pKey) {
				pVal = JS_UTIL_StrDup(pBuffer);
				if(pVal) {
					JS_UTIL_TrimWhiteSpace(pVal," \r\n\t:");
					if(JS_UTIL_StrCmp(pKey,"HOST",0,4,1)==0) {
						pHost = JS_UTIL_StrDup(pVal);
					}
					if(nKeyCnt<MAX_STRING_MAP) {
						if(JS_UTIL_StrCmp(pKey,"RANGE",0,0,1)==0) {
							//DBGPRINT("TMP:Range\n");
							pRequest->nRangeLen = JS_UTIL_HTTP_ParseRange(pVal,&pRequest->nRangeStartOffset,&pRequest->nRangeEndOffset);
							if(pRequest->nRangeLen>0) {
								DBGPRINT("TMP: REQ RANGE: %llu %llu %llu\n",pRequest->nRangeStartOffset, pRequest->nRangeEndOffset, pRequest->nRangeLen);
								DBGPRINT("TMP: REQ RANGE: %s\n",pVal);
							}
						}else if(JS_UTIL_StrCmp(pKey,"Content-Length",0,0,1)==0) {
							nRangeLen = JS_STRTOULL(pVal,NULL,10);
							if(nRangeLen>0) {
								pRequest->nRangeLen  = nRangeLen;
								//DBGPRINT("REQ ContentLength: %llu\n", pRequest->nRangeLen);
							}
						}else if(JS_UTIL_StrCmp(pKey,"Proxy-Connection",0,0,1)==0) {
							char * pTmpKey;
							char * pTmpVal;
							pTmpKey = JS_UTIL_StrDup("Connection");
							if(JS_UTIL_StrCmp(pVal,"keep-alive",0,0,1)==0) {
								pTmpVal = JS_UTIL_StrDup("Keep-Alive");
							}else {
								pTmpVal = JS_UTIL_StrDup("Close");
							}
							JS_FREE(pKey);
							JS_FREE(pVal);
							pKey = pTmpKey;
							pVal = pTmpVal;
						}else if(JS_UTIL_StrCmp(pKey,"User-Agent",0,0,1)==0) {
							int nValLen = strlen(pVal);
							if(JS_UTIL_FindPattern(pVal,"MSIE",nValLen,4,0)>=0) {
								pRequest->nUserAgent = JS_BROWSER_IE;
							}else if(JS_UTIL_FindPattern(pVal,"Chrome",nValLen,4,0)>=0) {
								pRequest->nUserAgent = JS_BROWSER_CHROME;
							}else if(JS_UTIL_FindPattern(pVal,"Firefox",nValLen,7,0)>=0) {
								pRequest->nUserAgent = JS_BROWSER_FIREFOX;
							}
						}
						nValOfMinus = -1;
						JS_UTIL_HTTP_FieldMap_Set(pRequest->hFieldList,pKey,pVal,&nValOfMinus);
						nKeyCnt++;
					}else {
						JS_FREE(pKey);
						JS_FREE(pVal);
						DBGPRINT("Can't alloc key val over max %d\n",nKeyCnt);
					}
				}else
					JS_FREE(pKey);
			}
			nStatus=HTTP_REQ_HEAD_KEY;
			JS_STRCPY(strDeli,"\r\n:");
		}
		nTokenIndex = nNext;
	}
	pRequest->nFieldNum = nKeyCnt;
	if(pRedirectURL) {
		if(pURL)
			JS_FREE(pURL);
		pURL = pRedirectURL;
		pRequest->pURL = pURL;
		if(pHost)
			JS_FREE(pHost);
		if(pRes)
			JS_FREE(pRes);
		pHost = NULL;
		pRes = NULL;
	}else if(pURL) {
		pRequest->pURL = pURL;
	}else if(pHost && pRes){
		if(pRes[0]=='/')
			JS_STRPRINTF(pBuffer,nBuffSize,"http://%s%s",pHost,pRes);
		else
			JS_STRPRINTF(pBuffer,nBuffSize,"http://%s/%s",pHost,pRes);
		pRequest->pURL = JS_UTIL_StrDup(pBuffer);
	}
	if(pRequest->pURL) {
		pRequest->nURLLen = strlen(pRequest->pURL);
		pRequest->nResOffset = JS_UTIL_FindPattern(pRequest->pURL+7,"/",pRequest->nURLLen-7,1,0);
		pRequest->nResOffset += 7;
	}
	if(pHost)
		pRequest->pHost = pHost;
	else if(pURL){
		char * pTempHost;
		pTempHost=(char*)JS_ALLOC(512);
		if(pTempHost) {
			pRequest->pHost = JS_UTIL_HTTPExtractHostFromURL(pURL, pTempHost, 512);
		}
	}
	JS_FREE(pBuffer);
LABEL_EXIT_CHECKREQ:
	if(pHost && pRequest)
		JS_UTIL_ParseHTTPHost(pHost,&pRequest->nTargetIP,&pRequest->nTargetPort);
	else
		pRequest->nTargetPort = 80;
	if(pRes)
		JS_FREE(pRes);
	if(pRequest && (pRequest->pURL==NULL||pRequest->hQueue==NULL)) ///if fail
	{
		//DBGPRINT("TMP:DEL3 %d %d\n",nReqLen,(pRequest->pURL==NULL)?1:0);
		JS_UTIL_HTTP_DeleteRequest(pRequest);
		pRequest = NULL;
	}
	if(pRedirectURL) {
		JS_UTIL_FixHTTPRequest(pRequest,"Host",pRequest->pHost,0);
		DBGPRINT("explicit turbogate on: host=%s, url=%s\n",pRequest->pHost,pRequest->pURL);
		JS_UTIL_HTTP_PrintRequest(pRequest);
	}
	return pRequest;
}

void JS_UTIL_HTTP_DeleteResponseEx(JS_HTTP_Response	* pRsp, int nReuse)
{
	if(pRsp==NULL) {
		//DBGPRINT("JS_UTIL_HTTP_DeleteResponse: no rsp null\n");
		return;
	}
	if(pRsp->hFieldList)
		JS_List_Destroy(pRsp->hFieldList);
	if(nReuse) {
		pRsp->hFieldList = NULL;
		pRsp->nChunked = 0;
		pRsp->nFieldNum = 0;
		pRsp->nKeepAlive = 0;
		pRsp->nRangeLen = 0;
		pRsp->nSentOffset = 0;
		pRsp->nRspCode = 0;
	}else {
		if(pRsp->hQueue)
			JS_SimpleQ_Destroy(pRsp->hQueue);
		JS_FREE(pRsp);
	}
}

void JS_UTIL_HTTP_DeleteResponse(JS_HTTP_Response	* pRsp)
{
	JS_UTIL_HTTP_DeleteResponseEx(pRsp,0);
}

void JS_UTIL_HTTP_PrintResponse(JS_HTTP_Response	* pRsp)
{
	void * pData;
	JS_HANDLE hPosRaw;
	JS_UtilHttpSmallMapItem * pItem;
	if(pRsp==NULL) {
		DBGPRINT("JS_UTIL_HTTP_PrintResponse: no req nulll\n");
		return;
	}
	if(pRsp->hFieldList) {
		hPosRaw = NULL;
		while(1) {
			hPosRaw = JS_List_IterateRaw(pRsp->hFieldList,hPosRaw,&pData);
			if(hPosRaw==NULL)
				break;
			if(pData) {
				pItem = (JS_UtilHttpSmallMapItem *)pData;
				DBGPRINT("Key=%s>>Val=%s\n",pItem->pKey,pItem->pVal);
			}
		}
	}
}

JS_HTTP_Response * JS_UTIL_HTTP_PrepareResponse(void)
{
	int nRet = 0;
	JS_HTTP_Response * pRsp = NULL;
	////alloc memory items
	pRsp = (JS_HTTP_Response	*) JS_ALLOC(sizeof(JS_HTTP_Response));
	if(pRsp==NULL) {
		nRet = -1;
		DBGPRINT("Can't make rsp item\n");
		goto LABEL_EXIT_PREPARERSP;
	}
	memset((char*)pRsp,0,sizeof(JS_HTTP_Response));
	pRsp->hQueue = JS_SimpleQ_Create(0,0);
	if(pRsp->hQueue==NULL) {
		nRet = -1;
		DBGPRINT("prepare rsp:no queue mem error\n");
		goto LABEL_EXIT_PREPARERSP;
	}
	pRsp->hFieldList = JS_List_Create(pRsp,JS_UTIL_HTTP_FiledsRmFunc);
	if(pRsp->hFieldList==NULL) {
		nRet = -1;
		DBGPRINT("prepare rsp:no fieldlist mem error\n");
		goto LABEL_EXIT_PREPARERSP;
	}
LABEL_EXIT_PREPARERSP:
	if(nRet<0) {
		if(pRsp->hQueue) 
			JS_SimpleQ_Destroy(pRsp->hQueue);
		if(pRsp)
			JS_FREE(pRsp);
		pRsp = NULL;
	}
	return pRsp;
}

JS_HTTP_Response * JS_UTIL_HTTP_CheckResponsePacket(const char * strRsp, int nRspLen, JS_HTTP_Response * pOldItem)
{
	JS_HTTP_Response * pRsp = NULL;
	#define HTTP_RSP_STATUS 0
	#define HTTP_REQ_HEAD_KEY	3
	#define HTTP_REQ_HEAD_VAL	4

	int nBodyStart;
	int nTokenIndex;
	int nNext;
	char * pBuffer;
	char * pKey;
	char * pVal;
	int nKeyCnt;
	int nHitRange = 0;
	int nHitLength = 0;
	int nRspCnt=0;
	int nStatus=HTTP_RSP_STATUS;
	int nValOfMinus;
	char	strDeli[32] = " \t\r\n";
	HTTPSIZE_T	nRangeLen;

	if(nRspLen<0)
		nRspLen = strlen(strRsp);
	////check if this protocol is HTTP or not
	if(nRspLen<8 || strRsp[0] != 'H' || strRsp[1] != 'T' || strRsp[2] != 'T' || strRsp[3] != 'P') {
		DBGPRINT("Error: not HTTP protocol %d %c %c %c\n",nRspLen, strRsp[0], strRsp[1], strRsp[2]);
		goto LABEL_EXIT_CHECKRSP;
	}
	nBodyStart = JS_UTIL_FindPattern(strRsp,"\r\n\r\n",nRspLen,4,0);
	if(nBodyStart<0) {
		DBGPRINT("JS_UTIL_HTTP_CheckResponsePacket: Not http protocol\n");
		goto LABEL_EXIT_CHECKRSP;
	}
	nRspLen = nBodyStart+4;
	if(pOldItem) {
		JS_UTIL_HTTP_DeleteResponseEx(pOldItem,1);
		pRsp = pOldItem;
	}else {
		////alloc memory items
		pRsp = (JS_HTTP_Response	*) JS_ALLOC(sizeof(JS_HTTP_Response));
		if(pRsp==NULL) {
			DBGPRINT("Can't make rsp item\n");
			goto LABEL_EXIT_CHECKRSP;
		}
		memset((char*)pRsp,0,sizeof(JS_HTTP_Response));
		pRsp->nRspLen = nRspLen;
		pRsp->hQueue = JS_SimpleQ_Create(0,0);
		if(pRsp->hQueue==NULL) {
			DBGPRINT("Can't make rsp queue\n");
			goto LABEL_EXIT_CHECKRSP;
		}
	}
	pRsp->hFieldList = JS_List_Create(pRsp,JS_UTIL_HTTP_FiledsRmFunc);
	pBuffer = (char*)JS_ALLOC(nRspLen+64);
	if(pBuffer==NULL) {
		DBGPRINT("JS_UTIL_HTTP_CheckResponsePacket: Can't alloc\n");
		goto LABEL_EXIT_CHECKRSP;
	}
	pRsp->nKeepAlive = 1;	////TBD: check performance...
	nTokenIndex = 0;
	nKeyCnt = 0;
	nRspCnt = 0;
	while(1) {
		nNext = JS_UTIL_StrToken(strRsp, nRspLen, nTokenIndex, strDeli, pBuffer, nRspLen);
		if(nNext<0)
			break;
		if(nStatus==HTTP_RSP_STATUS) {
			if(nRspCnt==1) {
				pRsp->nRspCode = atoi(pBuffer);
				//if(JS_UTIL_HTTP_GetRspCodeGroup(pRsp) != JS_RSPCODEGROUP_SUCCESS)
					//DBGPRINT("RspStatus=%s,%u\n",pBuffer,pRsp->nRspCode);
				if(strRsp[nNext]=='\r') {
					//DBGPRINT("TMP:No RSP String\n");
					nStatus = HTTP_REQ_HEAD_KEY;
					JS_STRCPY(strDeli,"\r\n:");
				}else
					JS_STRCPY(strDeli,"\r\n");
			}else if(nRspCnt==2) {
				nStatus = HTTP_REQ_HEAD_KEY;
				JS_STRCPY(strDeli,"\r\n:");
			}
			nRspCnt++;
		}else if(nStatus==HTTP_REQ_HEAD_KEY){
			pKey = JS_UTIL_StrDup(pBuffer);
			if(pKey) {
				JS_UTIL_TrimWhiteSpace(pKey," \r\n:");
			}
			nStatus=HTTP_REQ_HEAD_VAL;
			JS_STRCPY(strDeli,"\r\n");
		}else if(nStatus==HTTP_REQ_HEAD_VAL) {
			if(pKey) {
				pVal = JS_UTIL_StrDup(pBuffer);
				if(pVal) {
					JS_UTIL_TrimWhiteSpace(pVal," \r\n\t:");
					if(nKeyCnt<MAX_STRING_MAP-4) {
						nValOfMinus = -1;
						JS_UTIL_HTTP_FieldMap_Set(pRsp->hFieldList,pKey,pVal,&nValOfMinus);
						if(JS_UTIL_StrCmp(pKey,"CONTENT-RANGE",0,0,1)==0) {
							nHitRange = 1;
							nRangeLen = JS_UTIL_HTTP_ParseRange(pVal,&pRsp->nRangeStartOffset,&pRsp->nRangeEndOffset);
							if(nRangeLen>0) {
								pRsp->nRangeLen = nRangeLen;
								//DBGPRINT("RSP RANGE: %llu %llu %llu\n",pRsp->nRangeStartOffset, pRsp->nRangeEndOffset, pRsp->nRangeLen);
							}
						}else if(JS_UTIL_StrCmp(pKey,"CONTENT-LENGTH",0,0,1)==0) {
							nHitLength = 1;
							nRangeLen = JS_STRTOULL(pVal,NULL,10);
							if(nRangeLen>0) {
								pRsp->nRangeLen  = nRangeLen;
								//DBGPRINT("RSP ContentLength: %llu\n", pRsp->nRangeLen);
							}
						}else if(JS_UTIL_StrCmp(pKey,"Transfer-Encoding",0,0,1)==0) {
							if(JS_UTIL_StrCmp(pVal,"Chunked",0,0,1)==0) {
								pRsp->nChunked = 1;
							}
						}else if(JS_UTIL_StrCmp(pKey,"Connection",0,0,1)==0) {
							if(JS_UTIL_StrCmp(pVal,"Keep-Alive",0,0,1)==0)
								pRsp->nKeepAlive = 1;
							else
								pRsp->nKeepAlive = 0;
						}
						nKeyCnt++;
					}else {
						JS_FREE(pKey);
						JS_FREE(pVal);
						DBGPRINT("RSP Can't alloc key val over max %d\n",nKeyCnt);
					}
				}else
					JS_FREE(pKey);
			}
			nStatus=HTTP_REQ_HEAD_KEY;
			JS_STRCPY(strDeli,"\r\n:");
		}
		nTokenIndex = nNext;
	}
	pRsp->nFieldNum = nKeyCnt;
	JS_FREE(pBuffer);
	if(pRsp->nRangeLen>0)
		JS_SimpleQ_ResetTotallSize(pRsp->hQueue,pRsp->nRangeLen);
	if(pRsp->nChunked)
		DBGPRINT("CheckRsp: chunked detected size=%llu\n",pRsp->nRangeLen);
LABEL_EXIT_CHECKRSP:
	if(pRsp && nRspCnt<2) ///if fail
	{
		JS_UTIL_HTTP_DeleteResponse(pRsp);
	}
	if(nHitRange && nHitLength) {
		//JS_UTIL_HTTP_PrintResponse(pRsp);
		;
	}
	return pRsp;
}

int JS_UTIL_HTTP_GetRspCodeGroup(JS_HTTP_Response * pRsp)
{
	return (pRsp->nRspCode/100);
}

int JS_UTIL_HTTP_IsRspCriticalError(JS_HTTP_Response * pRsp)
{
	int nRet = 0;
	if(pRsp->nRspCode/100 == 4) {
		if(pRsp->nRspCode==400 || pRsp->nRspCode==403 || pRsp->nRspCode==405 || pRsp->nRspCode==408 || pRsp->nRspCode==409)
			return 0;
		else
			return 1;
	}else if(pRsp->nRspCode==505)
		return 1;
	return 0;
}

int JS_UTIL_HTTPExtractRspRange(const char * pRspString, int nRspLen, HTTPSIZE_T * pnRangeLen, HTTPSIZE_T * pnRangeStart)
{
	int nRet = 0;
	char strRange[256];
	char * pTok;
	HTTPSIZE_T nRangeLen =  0;
	HTTPSIZE_T nRangeStart = 0;
	HTTPSIZE_T nRangeEnd = 0;
	int nIndex;
	////anal the header
	pTok = JS_UTIL_ExtractString(pRspString, "Content-Range:", "\r\n", nRspLen, strRange, 250, 1, &nIndex);
	if(pTok) {
		nRangeLen = JS_UTIL_HTTP_ParseRange(pTok,&nRangeStart,&nRangeEnd);
	}
	*pnRangeLen = nRangeLen;
	*pnRangeStart = nRangeStart;
	return nRet;
}

int JS_UTIL_HTTPExtractRspCode(const char * pRspString, int nRspLen)
{
	int nRet = 0;
	char strCode[64];
	int nCurIndx;
	////anal the header
	strCode[0] = 0;
	nCurIndx = 0;
	nCurIndx = JS_UTIL_StrToken(pRspString,nRspLen,nCurIndx," \r\n",strCode,60);
	if(nCurIndx>0)
		nCurIndx = JS_UTIL_StrToken(pRspString,nRspLen,nCurIndx," \r\n",strCode,60);
	if(nCurIndx<0) {
		DBGPRINT("Can't find rsp code\n");
		return -1;
	}
	nRet = atoi(strCode);
	return nRet;
}

char * JS_UTIL_HTTP_BuildReqString(JS_HTTP_Request	* pReq, HTTPSIZE_T nRangeStart, HTTPSIZE_T nRangeLen, const char * strNewLocation, int nIsProbe)
{
	int nLen;
	char * pBuff;
	nLen = pReq->nReqLen+256;
	if(strNewLocation)
		nLen += strlen(strNewLocation);
	pBuff = (char*)JS_ALLOC(nLen);
	if(pBuff==NULL)
		return NULL;
	return JS_UTIL_HTTP_CopyRequest(pReq,nRangeStart,nRangeLen,pBuff,nLen,strNewLocation, nIsProbe);
}

char * JS_UTIL_HTTP_CopyRequest(JS_HTTP_Request	* pReq, HTTPSIZE_T nRangeStart, HTTPSIZE_T nRangeLen, char * pData, int nDataLen, const char * strURL, int nIsProbe)
{
	char * pRet = NULL;
	int nCnt;
	int nCumLen;
	int nURLLen;
	int nArrSize;
	char strFirstline[JS_CONFIG_MAXURL];
	char strRangeStr[256];
	char strHost[1004];
	int nRangeSet = 0;
	char * pURL;
	char strMethod[16];
	JS_HANDLE hPosRaw;
	void * pItemData;
	int  nChangeRange = 0;
	JS_UtilHttpSmallMapItem * pItem;
	nCumLen = 0;

	if(nIsProbe) {
		JS_STRCPY(strMethod,"HEAD");
	}else {
		JS_STRCPY(strMethod,pReq->strMethod);
	}
	if(nRangeStart>0 || nRangeLen>0)
		nChangeRange = 1;
	if(strURL) {
		strHost[0] = 0;
		JS_UTIL_HTTPExtractHostFromURL(strURL,strHost,1000);
		pURL = (char*)strURL;
	}else {
		pURL = pReq->pURL;
	}
	nURLLen = strlen(pURL);
	for(nCnt=7; nCnt<nURLLen; nCnt++) {
		if(pURL[nCnt]=='/')
			break;
	}
	if(nCnt==nURLLen)
		JS_STRPRINTF(strFirstline,JS_CONFIG_MAXURL,"%s / HTTP/1.1\r\n",strMethod);
	else
		JS_STRPRINTF(strFirstline,JS_CONFIG_MAXURL,"%s %s HTTP/1.1\r\n",strMethod,pURL+nCnt);

	JS_UTIL_StrCopySafe(pData,nDataLen,strFirstline,0);
	nCumLen = strlen(pData);
	if(nCumLen>=nDataLen)
		goto LABEL_EXIT_COPYREQ;
	////set range
	if(nChangeRange) {
		if(nRangeLen==0)
			JS_STRPRINTF(strRangeStr,256,"%llu-",nRangeStart);
		else
			JS_STRPRINTF(strRangeStr,256,"%llu-%llu",nRangeStart,nRangeStart+nRangeLen-1);
	}
	nArrSize = pReq->nFieldNum;
	hPosRaw = NULL;
	while(1) {
		hPosRaw = JS_List_IterateRaw(pReq->hFieldList,hPosRaw,&pItemData);
		if(hPosRaw==NULL)
			break;
		pItem = (JS_UtilHttpSmallMapItem*)pItemData;
		if(nChangeRange && JS_UTIL_StrCmpRestrict(pItem->pKey,"Range",0,0,1)==0) {
			JS_STRPRINTF(pData+nCumLen,nDataLen-nCumLen,"Range: bytes=%s\r\n",strRangeStr);
			nRangeSet = 1;
		}else if(strURL && JS_UTIL_StrCmpRestrict(pItem->pKey,"Host",0,0,1)==0) {
			JS_STRPRINTF(pData+nCumLen,nDataLen-nCumLen,"Host: %s\r\n",strHost);
		}else {
			JS_STRPRINTF(pData+nCumLen,nDataLen-nCumLen,"%s: %s\r\n",pItem->pKey,pItem->pVal);
		}
		nCumLen = strlen(pData);
		if(nCumLen>nDataLen) {
			DBGPRINT("CopyReq: Too much str error\n");
			goto LABEL_EXIT_COPYREQ;
		}
	}
	if(nChangeRange && nRangeSet==0) {
		JS_STRPRINTF(pData+nCumLen,nDataLen-nCumLen,"Range: bytes=%s\r\n",strRangeStr);
		nCumLen = strlen(pData);
	}
	JS_STRPRINTF(pData+nCumLen,nDataLen-nCumLen,"\r\n");
	pRet = pData;
LABEL_EXIT_COPYREQ:
	return pRet;
}

static const char * JS_UTIL_HTTP_FieldMap_Get(JS_HANDLE hList, const char * strKey, int nArrSize)
{
	int nFound = 0;
	int nKeyLen = 0;
	const char * pRet = NULL;
	JS_HANDLE hPosRaw = NULL;
	void * pItemData = NULL;	
	JS_UtilHttpSmallMapItem * pItem = NULL;
	nKeyLen = strlen(strKey);
	while(1) {
		hPosRaw = JS_List_IterateRaw(hList,hPosRaw,&pItemData);
		if(hPosRaw==NULL)
			break;
		pItem = (JS_UtilHttpSmallMapItem *)pItemData;
		if(pItem->nKeyLen == nKeyLen) {
			if(JS_UTIL_StrCmp(pItem->pKey,strKey,nKeyLen,nKeyLen,1) == 0) {
				nFound = 1;
				break;
			}
		}
	}
	if(nFound)
		pRet = pItem->pVal;
	return pRet;
}

static int JS_UTIL_HTTP_FieldMap_Set(JS_HANDLE hList, const char * strKey, const char * strVal, int * pnArrSize)
{
	int nRet = 0;
	int nFound = 0;
	int nKeyLen = 0;
	int nArrSize;
	const char * pRet = NULL;
	JS_HANDLE hPosRaw = NULL;
	void * pItemData = NULL;	
	JS_UtilHttpSmallMapItem * pItem = NULL;
	if(hList==NULL || strKey==NULL || strVal==NULL || pnArrSize==NULL)
		return -1;
	nKeyLen = strlen(strKey);
	nArrSize = *pnArrSize;
	while(nArrSize>0) {
		hPosRaw = JS_List_IterateRaw(hList,hPosRaw,&pItemData);
		if(hPosRaw==NULL)
			break;
		pItem = (JS_UtilHttpSmallMapItem *)pItemData;
		if(pItem->nKeyLen == nKeyLen) {
			if(JS_UTIL_StrCmp(pItem->pKey,strKey,nKeyLen,nKeyLen,1) == 0) {
				nFound = 1;
				if(pItem->pVal)
					JS_FREE(pItem->pVal);
				pItem->pVal = JS_UTIL_StrDup(strVal);
				if(pItem->pVal==NULL) {
					DBGPRINT("httputil: mem error (key set val)\n");
					nRet = -1;
				}
				pItem->nValLen = strlen(strVal);
				break;
			}
		}
	}
	pItem = NULL;
	if(nRet>=0) {
		if(nFound==0) {
			pItem = (JS_UtilHttpSmallMapItem*)JS_ALLOC(sizeof(JS_UtilHttpSmallMapItem));
			if(pItem==NULL) {
				nRet = -1;
				DBGPRINT("httputil: mem error (key set item)\n");
				goto LABEL_CATCH_ERROR;
			}
			memset((char*)pItem,0,sizeof(JS_UtilHttpSmallMapItem));
			if(nArrSize>=0) {
				pItem->pKey = JS_UTIL_StrDup(strKey);
				if(pItem->pKey==NULL) {
					nRet = -1;
					DBGPRINT("httputil: mem error (key set key)\n");
					goto LABEL_CATCH_ERROR;
				}
				pItem->pVal = JS_UTIL_StrDup(strVal);
				if(pItem->pVal==NULL) {
					nRet = -1;
					DBGPRINT("httputil: mem error (key set val2)\n");
					goto LABEL_CATCH_ERROR;
				}
			}else {
				pItem->pKey = (char*)strKey;
				pItem->pVal = (char*)strVal;
			}
			pItem->nKeyLen = nKeyLen;
			pItem->nValLen = strlen(strVal);
			if(JS_List_PushBack(hList,pItem)<0) {
				nRet = -1;
				DBGPRINT("httputil: mem error (key set listpush)\n");
				goto LABEL_CATCH_ERROR;
			}
			*pnArrSize = nArrSize+1;
		}
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pItem) {
			if(pItem->pKey)
				JS_FREE(pItem->pKey);
			if(pItem->pVal)
				JS_FREE(pItem->pVal);
			JS_FREE(pItem);
		}
	}
	return nRet;
}

static int JS_UTIL_HTTP_FieldMap_IsSame(JS_HANDLE hList, const char * strKey, const char * strVal, int nArrSize)
{
	int nRet = 0;
	const char * pRet = NULL;
	pRet = JS_UTIL_HTTP_FieldMap_Get(hList,strKey,nArrSize);
	if(pRet) {
		if(JS_UTIL_StrCmpRestrict(pRet,strVal,0,0,1)==0) {
			nRet = 1;
		}
	}
	return nRet;
}

static int JS_UTIL_HTTP_FieldMap_ToString(const char * strFirstline, JS_HANDLE hList, char ** ppRet, int nArrSize)
{
	int nRet=0;
	int nCumLen;
	int nKeyLen = 0;
	JS_HANDLE hPosRaw = NULL;
	void * pItemData = NULL;	
	JS_UtilHttpSmallMapItem * pItem = NULL;
	char * pRet = *ppRet;
	int nMaxLen = JS_CONFIG_MAXHTTPSTRING;

	if(pRet!=NULL) {
		nMaxLen = strlen(pRet);
		nMaxLen = nMaxLen*3/2+200;
		JS_FREE(pRet);
	}
	pRet = (char *) JS_ALLOC(nMaxLen);
	if(pRet==NULL) {
		DBGPRINT("httputil tostring: mem error(buffer alloc)\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	nCumLen = 0;
	JS_STRCPY(pRet,strFirstline);
	nCumLen = strlen(pRet);
	hPosRaw = NULL;
	while(1){
		hPosRaw = JS_List_IterateRaw(hList,hPosRaw,&pItemData);
		if(hPosRaw==NULL)
			break;
		pItem = (JS_UtilHttpSmallMapItem *)pItemData;
		if(pItem->pKey==NULL || pItem->pVal==NULL)
			continue;
		JS_STRPRINTF(pRet+nCumLen,nMaxLen-nCumLen,"%s: %s\r\n",pItem->pKey,pItem->pVal);
		nCumLen = strlen(pRet);
		if(nCumLen+200>nMaxLen) {
			nMaxLen = nMaxLen*3/2+200;
			pRet = (char *) realloc(pRet,nMaxLen);
			if(pRet==NULL) {
				DBGPRINT("httputil tostring: mem error(buffer realloc size=%u)\n",nMaxLen);
				nRet = -2;
				goto LABEL_CATCH_ERROR;
			}
		}
	}
	JS_STRPRINTF(pRet+nCumLen,nMaxLen-nCumLen,"\r\n");
LABEL_CATCH_ERROR:
	*ppRet = pRet;
	return nRet; 
}


int JS_UTIL_FixHTTPRequest(JS_HTTP_Request	* pReq, const char * strKey, const  char * strVal, int nNeedResultString)
{
	int nRet;
	if(strKey) {
		nRet = JS_UTIL_HTTP_FieldMap_Set(pReq->hFieldList,strKey,strVal,&pReq->nFieldNum);
	}else
		nRet = 0;
	return nRet;
}

const char * JS_UTIL_GetHTTPResponseHeader(JS_HTTP_Response	* pRsp, const char * strKey)
{
	const char * pRet;
	pRet = JS_UTIL_HTTP_FieldMap_Get(pRsp->hFieldList,strKey,pRsp->nFieldNum);
	return pRet;
}

const char * JS_UTIL_GetHTTPRequestHeader(JS_HTTP_Request	* pReq, const char * strKey)
{
	const char * pRet;
	pRet = JS_UTIL_HTTP_FieldMap_Get(pReq->hFieldList,strKey,pReq->nFieldNum);
	return pRet;
}
int JS_UTIL_HTTPRequest_CompareHeader(JS_HTTP_Request	* pReq, const char * strKey, const  char * strVal)
{
	int nRet;
	nRet = JS_UTIL_HTTP_FieldMap_IsSame(pReq->hFieldList,strKey,strVal,pReq->nFieldNum);
	return nRet;
}

int JS_UTIL_HTTP_BuildStaticRspString(JS_HTTP_Response	* pRsp, int nRspCode,  const char * strRspCode, char * pBuff, int nBuffSize)
{
	int nRet = 0;
	int nCumLen = 0;
	JS_HANDLE hPosRaw = NULL;
	void * pItemData = NULL;
	JS_UtilHttpSmallMapItem * pItem = NULL;

	if(nRspCode)
		pRsp->nRspCode = nRspCode;
	if(strRspCode)
		JS_STRPRINTF(pBuff,nBuffSize,"HTTP/1.1 %d %s\r\n",pRsp->nRspCode,strRspCode);
	else
		JS_STRPRINTF(pBuff,nBuffSize,"HTTP/1.1 %d OK\r\n",pRsp->nRspCode);
	nCumLen = strlen(pBuff);
	hPosRaw = NULL;
	while(1) {
		hPosRaw = JS_List_IterateRaw(pRsp->hFieldList,hPosRaw,&pItemData);
		if(hPosRaw==NULL)
			break;
		pItem = (JS_UtilHttpSmallMapItem*)pItemData;
		JS_STRPRINTF(pBuff+nCumLen,nBuffSize-nCumLen,"%s: %s\r\n",pItem->pKey,pItem->pVal);
		nCumLen += strlen(pBuff+nCumLen);
		if(nCumLen>nBuffSize) {
			DBGPRINT("build rsp string: too much str error\n");
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
	}
	JS_STRPRINTF(pBuff+nCumLen,nBuffSize-nCumLen,"\r\n");
	pRsp->nRspLen = nCumLen+2;
LABEL_CATCH_ERROR:
	return nRet;
}

char * JS_UTIL_HTTP_BuildRspString(JS_HTTP_Response	* pRsp, int nRspCode, const char * strRspCode)
{
	int nBuffSize;
	int nRet = 0;
	char * pBuff = NULL;

	nBuffSize = JS_List_GetSize(pRsp->hFieldList)*256+512;
	pBuff = (char*)JS_ALLOC(nBuffSize);
	if(pBuff==NULL)
		return NULL;
	nRet = JS_UTIL_HTTP_BuildStaticRspString(pRsp, nRspCode,  strRspCode, pBuff, nBuffSize);
	if(nRet<0) {
		JS_FREE(pBuff);
		pBuff = NULL;
	}
	return pBuff;
}

int JS_UTIL_HTTP_ClearFIeldList(JS_HANDLE hFieldList)
{
	JS_List_Reset(hFieldList);
	return 0;
}

int JS_UTIL_HTTP_AddIntField(JS_HANDLE hFieldList, const char * strKey, UINT64 nVal)
{
	char strParam[32];
	JS_STRPRINTF(strParam,32,"%llu",nVal);
	return JS_UTIL_HTTP_AddField(hFieldList,strKey,strParam);
}

int JS_UTIL_HTTP_AddField(JS_HANDLE hFieldList, const char * strKey, const  char * strVal)
{
	int nRet =0;
	int nValueOfZero = 0;
	if(strKey)
		nRet = JS_UTIL_HTTP_FieldMap_Set(hFieldList,strKey,strVal,&nValueOfZero);
	return nRet;
}


int JS_UTIL_FixHTTPResponse(JS_HTTP_Response * pRsp, const char * strKey, const  char * strVal)
{
	int nRet =0;
	if(strKey)
		nRet = JS_UTIL_HTTP_FieldMap_Set(pRsp->hFieldList,strKey,strVal,&pRsp->nFieldNum);
	return nRet;
}

int JS_UTIL_HTTPResponse_CompareHeader(JS_HTTP_Response	* pRsp, const char * strKey, const  char * strVal)
{
	int nRet;
	nRet = JS_UTIL_HTTP_FieldMap_IsSame(pRsp->hFieldList,strKey,strVal,pRsp->nFieldNum);
	return nRet;
}

int JS_UTIL_HTTP_IsEndOfChunk(char * pData, int nSize)
{
	if(nSize>=5) {
		if(JS_UTIL_StrCmp(pData+nSize-5,"0\r\n\r\n",5,5,0)==0) {
			return 1;
		}
	}
	return 0;
}

int JS_UTIL_HTTP_GetReqResourceName(JS_HTTP_Request	* pReq, char * pBuffer, int nBuffLen)
{
	int nCnt;
	int nRet = 0;

	pBuffer[0] = 0;
	for(nCnt=pReq->nResOffset+1; nCnt<pReq->nURLLen; nCnt++) {
		if(pReq->pURL[nCnt]=='?')
			break;
		if(pReq->pURL[nCnt]=='/') {
			break;
		}
	}
	if(nCnt==pReq->nResOffset+1) {
		pBuffer[0] = '/';
		pBuffer[1] = 0;
	}else {
		JS_UTIL_StrCopySafe(pBuffer,nBuffLen,pReq->pURL+pReq->nResOffset+1,nCnt-pReq->nResOffset-1);
	}
	return nRet;
}

int JS_UTIL_HTTP_CheckReqMethod(JS_HTTP_Request	* pReq, const char * strMethod)
{
	if(pReq==NULL||strMethod==NULL)
		return 0;
	if(JS_UTIL_FindPattern(strMethod,pReq->strMethod,0,0,1) >=0)
		return 1;
	else
		return 0;
}

void JS_UTIL_HTTP_GmtTimeString(char * pBuff, int nBuffLen, time_t *t) 
{
  strftime(pBuff, nBuffLen, "%a, %d %b %Y %H:%M:%S GMT", gmtime(t));
}

void JS_UTIL_HTTP_MakeETAG(char *pBuff, int nBuffLen,const char * strPath, char * pLMBuff, int nLMBuffLen) 
{
  struct stat stbuf;
  time_t nTime;
  UINT32 nDate;
  UINT64 nSize;
  stat(strPath, &stbuf);
  nSize = stbuf.st_size;
  nTime = stbuf.st_mtime;
  nDate = (UINT32)nTime;
  if(pBuff)
	  JS_UTIL_StrPrint(pBuff, nBuffLen, "\"%lx-%llu\"",nDate,nSize); 
  //DBGPRINT("TMP: MW Check %llu, %u, %d, %lu\n",nSize,nSize,nSize,nSize);
  if(pLMBuff)
	  strftime(pLMBuff, nLMBuffLen, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&nTime));
}

int JS_UTIL_HTTP_ConvertURLtoLocalFilePath(const char * strDir, const char * strFullResource, char * pBuffer , int nBuffLen, int nFullResLen)
{
	int nRet = 0;
	int nCnt=0;
	int nBuffCnt;
	////copy first dir name
	JS_UTIL_StrCopySafe(pBuffer,nBuffLen,strDir,0);
	nBuffCnt = strlen(pBuffer);
	while(1) {
		if(nCnt>=nFullResLen)
			break;
		if(nBuffCnt>=nBuffLen)
			break;
		if(strFullResource[nCnt] == '/') {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
			pBuffer[nBuffCnt] = '\\';
#else
			pBuffer[nBuffCnt] = '/';
#endif
		}else if(strFullResource[nCnt] == '?') {
			break;
		}else {
			pBuffer[nBuffCnt] = strFullResource[nCnt];
		}
		nCnt++;
		nBuffCnt++;
	}
	pBuffer[nBuffCnt] = 0;
	if(nFullResLen==1 && strFullResource[0] == '/') {
		JS_UTIL_StrCopySafe(pBuffer+nBuffCnt,nBuffLen-nBuffCnt,"index.html",0);
	}
	return nRet;
}


///http functions end
/////////////////////////////////////////////////////////////
