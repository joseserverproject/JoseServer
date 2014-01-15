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
Http server based on JS_EventLoop.c

1. based on nonblocking io
2. provide embeded CGI (direct api)

TBD:
1. etag operation
2. fast cgi
3. websocket
4. ssl
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#include "JS_EventLoop.h"
#include "JS_HttpServer.h"
#include "JS_HttpMsg.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////////
//macro start
#define MAX_NETWORK_DEVICES		16

#define JS_CGITYPE_UNKNOWN			0
#define JS_CGITYPE_DIRECTAPI		1
#define JS_CGITYPE_ASYNCAPI		2
#define JS_CGITYPE_FASTCGI			3
#define JS_CGITYPE_FILE				4
#define JS_HTTPSERVER_CHECKREQ_OPT_NORCV		1
#define JS_HTTPSERVER_CHECKREQ_OPT_INFASTIO		2
#define JS_HTTPSERVER_QITEM_SIZE	(JS_CONFIG_MAXBUFFSIZE+8000)
//macro end
//////////////////////////////////////////////////////

//////////////////////////////////////////////////////
//local typedef
typedef struct JS_HttpServer_DirectAPIItemTag {
	int nType;
	char * pResourceName;
	JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK	pfDirectAPI;
}JS_HttpServer_DirectAPIItem;

typedef struct JS_HttpServer_MimeTypeTag{
	char * pExtension;
	int	 nExtLen;
	char * pMimeType;
}JS_HttpServer_MimeType;

typedef struct JS_HttpServer_SessionItemTag
{
	////neccesary fields
	JS_SOCKET_T nInSock;
	JS_HTTP_Request	* pReq;
	JS_HTTP_Response * pRsp;
	JS_POOL_ITEM_T * pPoolItem;
	JS_EventLoop * pIO;
	void * pHttpObject;
	int nCGIType;

	////resource fields
	JS_HANDLE  hFile;
	char * pFileName;
	JS_HttpServer_DirectAPIItem * pTmpRes;
	JSUINT	nWorkID;

	////additional
	int nIsFastIOThread;
	JS_HANDLE hFastThread;
	int nReadCnt;
	int nForceDownload;

	////TBD fix this
	char * pBoundary;
	char * pBoundaryEof;
	char * pVarData;
	int    nVarDataLen;
	JS_HANDLE	hPostFileList;
	char   strIP[20];
	int	   nIsProxyReq;
	int	   nZeroRxCnt;
	int	   nRcvDbgSize;
}JS_HttpServer_SessionItem;

typedef struct   JS_HttpServerGlobalTag
{
	int nNeedToExit;
	int nFastIOThreads;
	JS_HANDLE hDirectAPIMap;
	JS_HANDLE hDirectAPIWorkQ;
	JS_HANDLE hMimeMap;
	JS_HANDLE hGlobalMutex;
	char * pUploadDir;
	char * pDownloadDir;
	JS_HANDLE hJose;
	int    nNetworkDeviceNum;
	char   strHostName[JS_CONFIG_MAX_SMALLURL];
	UINT32 arrHostIP[MAX_NETWORK_DEVICES];
}JS_HttpServerGlobal;

///////////////////////////////////////////////
//local variables
static JS_EventLoopHandler g_httpEventHandler;

///////////////////////////////////////////////
//extern functions
extern JS_HANDLE JS_GetServerLoopHandle(JS_HANDLE hJose);
extern JS_HANDLE JS_GetHttpServerHandle(JS_HANDLE hJose);

static int JS_HttpServer_BuildDefaultMsg(JS_HttpServer_SessionItem * pItem, UINT64 nLength);
////event handlers for server loop
static JS_SOCKET_T JS_HttpServer_GetSocket(JS_POOL_ITEM_T * pPoolItem);
static int JS_HttpServer_AddItem(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem, JS_SOCKET_T nInSock);
static int JS_HttpServer_IOHandler(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet);

////session pool callback functions
__inline static JS_HttpServer_SessionItem * _RET_SESSIONITEM_(JS_POOL_ITEM_T* pItem);
static int JS_HttpServer_SessionItem_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase);

////direct api callback functions
static int JS_HttpServer_DirectAPIItem_HashCallback (void * pOwner, void * pData, void * pKey);
static int JS_HttpServer_DirectAPIItem_FindCallback (void * pOwner, void * pData, void * pKey);
static int JS_HttpServer_DestroyAPIMap(JS_HttpServerGlobal * pHttpServer);

////mimemap callback functions
static int JS_HttpServer_MimeItem_HashCallback (void * pOwner, void * pData, void * pKey);
static int JS_HttpServer_MimeItem_FindCallback (void * pOwner, void * pData, void * pKey);
static int JS_HttpServer_MimeLoadDefault(JS_HttpServerGlobal * pHttpServer);
////mime retrieve from file extension
static const char * JS_HttpServer_GetMimeType(JS_HttpServerGlobal * pHttpServer, const char * strPath, JS_HttpServer_SessionItem * pItem);
static int JS_HttpServer_DestroyMimeMap(JS_HttpServerGlobal * pHttpServer);
static int JS_HttpServer_ClearSessionRef(JS_HttpServer_SessionItem * pSession);

////send data function using queue between cgi and eventloop thread
static int JS_HttpServer_SendRspQueue(JS_HttpServer_SessionItem * pItem, char * pData, int nSize);

static int JS_HttpServer_SendFileReqHeader(JS_EventLoop * pIO, JS_HttpServer_SessionItem * pItem, char * strPath, char * strTemp, int nBuffSize);
static int JS_HttpServer_SendErrorPageWithErrorString(JS_HttpServer_SessionItem * pItem, int nError, char * pBuffer, int nBuffLen, int nNeedToClose, const char * pErrorString);
static int JS_HttpServer_SendErrorPage(JS_HttpServer_SessionItem * pItem, int nError, char * pBuffer, int nBuffLen, int nNeedToClose);

static int JS_HttpServer_CheckResource(JS_HttpServerGlobal * pHttpServer, JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, char * pPathBuff, char * pBuffer, int nBuffLen, int nIsFromCGI);
static int JS_HttpServer_CheckDuplicateResource(JS_HttpServerGlobal * pHttpServer, const char * strResourceName);
static int JS_HttpServer_TryToSend(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, int nOption);
static int JS_HttpServer_TryToRead(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, int nOption);

static int JS_HttpServer_ResetSession(JS_HttpServer_SessionItem * pItem, JS_HTTP_Request * pReq);
static int JS_HttpServer_RegisterAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfDirect, int nIsAsync);

static void * JS_DirectAPI_WorkQFunc (void * pParam);
static int JS_HttpServer_DirectAPIWorkQEventFunc (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff);
static void JS_HttpServer_CheckMyNetworkInfo(JS_HttpServerGlobal * pHttpServer);

/////ext function impls
JS_EventLoopHandler * JS_HttpServer_GetEventHandler(void)
{
	g_httpEventHandler.nDataID = JS_DATAID_HTTPSERVER;
	g_httpEventHandler.nPoolItemSize = sizeof(JS_HttpServer_SessionItem);
	g_httpEventHandler.pfAddIO = JS_HttpServer_AddItem;
	g_httpEventHandler.pfDoIO = JS_HttpServer_IOHandler;
	g_httpEventHandler.pfPhase = JS_HttpServer_SessionItem_PhaseChange;
	g_httpEventHandler.pfGetSock = JS_HttpServer_GetSocket;
	g_httpEventHandler.pfTransferIO = NULL;
	return &g_httpEventHandler;
}

JS_HANDLE JS_HttpServer_Create(JS_HANDLE hJose, unsigned short nPort, int nIsAutoPort)
{
	int nRet = 0;
	JS_HttpServerGlobal * pHttpServer = NULL;
	
	pHttpServer = (JS_HttpServerGlobal *)JS_ALLOC(sizeof(JS_HttpServerGlobal));
	if(pHttpServer==NULL) {
		DBGPRINT("httpserver: create JS_ALLOC error\n");
		return NULL;
	}
	memset((char*)pHttpServer,0,sizeof(JS_HttpServerGlobal));
	pHttpServer->hJose = hJose;
	////make resource pool
	pHttpServer->hMimeMap = JS_HashMap_Create(pHttpServer,NULL,JS_HttpServer_MimeItem_HashCallback,JS_CONFIG_NORMAL_MIMEMAP,1);
	if(pHttpServer->hMimeMap==NULL) {
		DBGPRINT("http server:cant' alloc mime map\n");
		nRet = -1;
		goto LABEL_EXIT_CREATEHTTPSERVER;
	}
	////check mime types
	JS_HttpServer_MimeLoadDefault(pHttpServer);
	////make resource pool
	pHttpServer->hDirectAPIMap = JS_HashMap_Create(pHttpServer,NULL,JS_HttpServer_DirectAPIItem_HashCallback,JS_CONFIG_NORMAL_DIRECTAPI,1);
	if(pHttpServer->hDirectAPIMap==NULL) {
		DBGPRINT("http server:cant' alloc resource pool\n");
		nRet = -1;
		goto LABEL_EXIT_CREATEHTTPSERVER;
	}
	pHttpServer->hGlobalMutex = JS_UTIL_CreateMutex();
	if(pHttpServer->hGlobalMutex==NULL) {
		DBGPRINT("http server: can't alloc mutex\n");
		nRet = -1;
		goto LABEL_EXIT_CREATEHTTPSERVER;
	}
	pHttpServer->hDirectAPIWorkQ = JS_ThreadPool_CreateWorkQueue(JS_CONFIG_MAX_DIRECTAPI_WORKS);
	if(pHttpServer->hDirectAPIWorkQ == NULL) {
		DBGPRINT("http server: can't alloc workq\n");
		nRet = -1;
		goto LABEL_EXIT_CREATEHTTPSERVER;
	}
	JS_HttpServer_CheckMyNetworkInfo(pHttpServer);
LABEL_EXIT_CREATEHTTPSERVER:
	if(nRet<0) {
		JS_HttpServer_Destroy(pHttpServer);
		pHttpServer = NULL;
	}
	return pHttpServer;
}

int JS_HttpServer_Destroy(JS_HANDLE hServer)
{
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)hServer;
	if(pHttpServer) {
		if(pHttpServer->hDirectAPIMap)
			JS_HttpServer_DestroyAPIMap(pHttpServer);
		if(pHttpServer->hMimeMap)
			JS_HttpServer_DestroyMimeMap(pHttpServer);
		if(pHttpServer->pUploadDir)
			JS_FREE(pHttpServer->pUploadDir);
		if(pHttpServer->pDownloadDir)
			JS_FREE(pHttpServer->pDownloadDir);
		if(pHttpServer->hGlobalMutex)
			JS_UTIL_DestroyMutex(pHttpServer->hGlobalMutex);
		if(pHttpServer->hDirectAPIWorkQ)
			JS_ThreadPool_DestroyWorkQueue(pHttpServer->hDirectAPIWorkQ);
		JS_FREE(pHttpServer);
	}
	return 0;
}

unsigned short JS_HttpServer_WhatIsMyPort(JS_HANDLE hServer)
{
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)hServer;
	if(pHttpServer && pHttpServer->hJose)
		return JS_GetJosePort(pHttpServer->hJose);
	else
		return 0;
}

int JS_HttpServer_RegisterDirectAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfDirect)
{
	return JS_HttpServer_RegisterAPI(hJose,strResourceName,pfDirect,0);
}

int JS_HttpServer_RegisterAsyncRawAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfAsync)
{
	return JS_HttpServer_RegisterAPI(hJose,strResourceName,pfAsync,1);
}

int JS_HttpServer_RegisterDocumentRoot(JS_HANDLE hJose,  const char * strDirName)
{
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)JS_GetHttpServerHandle(hJose);
	pHttpServer->pDownloadDir = JS_UTIL_StrDup(strDirName);
	if(pHttpServer->pDownloadDir)
		return 0;
	else
		return -1;
}

int JS_HttpServer_RegisterUploadRoot(JS_HANDLE hJose, const char * strUploadDirName)
{
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)JS_GetHttpServerHandle(hJose);
	pHttpServer->pUploadDir = JS_UTIL_StrDup(strUploadDirName);
	if(pHttpServer->pUploadDir)
		return 0;
	else
		return -1;
}

int JS_HttpServer_RegisterAccessControl(JS_HANDLE hJose, const char * strFilePath, int nEnable)
{
	int nRet = 0;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)JS_GetHttpServerHandle(hJose);
	if(JS_EventLoop_IsBusy(JS_GetServerLoopHandle(pHttpServer->hJose))) {
		DBGPRINT("Registering Resource must be done before service starts\n");
		return -1;
	}
	////TBD
	return nRet;
}

int JS_HttpServer_ChangeMimeType(JS_HANDLE hJose, const char * strExtention, const char * strMime)
{
	int nRet = 0;
	char * pOldMime;
	JS_HttpServer_MimeType * pItem;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)JS_GetHttpServerHandle(hJose);
	pItem = (JS_HttpServer_MimeType *)JS_HashMap_Find(pHttpServer->hMimeMap,(void*)strExtention,JS_HttpServer_MimeItem_FindCallback);
	if(pItem) {
		pOldMime = pItem->pMimeType;
		pItem->pMimeType = JS_UTIL_StrDup(strMime);
		if(pItem->pMimeType==NULL) {
			nRet = -1;
			DBGPRINT("change mime type: can't alloc mem error\n");
			pItem->pMimeType = pOldMime;
		}else
			JS_FREE(pOldMime);
	}else {
		pItem = (JS_HttpServer_MimeType*)JS_ALLOC(sizeof(JS_HttpServer_MimeType));
		if(pItem==NULL) {
			nRet = -1;
			DBGPRINT("change mime type: can't alloc mem error2\n");
		}else {
			pItem->pExtension = JS_UTIL_StrDup(strExtention);
			pItem->pMimeType = JS_UTIL_StrDup(strMime);
			if(pItem->pExtension==NULL || pItem->pMimeType==NULL) {
				nRet = -1;
				DBGPRINT("change mime type: can't alloc mem error3\n");
				if(pItem->pExtension)
					JS_FREE(pItem->pExtension);
				if(pItem->pMimeType)
					JS_FREE(pItem->pMimeType);
			}
			pItem->nExtLen = strlen(pItem->pExtension);
			nRet = JS_HashMap_Add(pHttpServer->hMimeMap,pItem);
			if(nRet<0) {
				DBGPRINT("change mime type: can't alloc mem error4\n");
				JS_FREE(pItem->pMimeType);
				JS_FREE(pItem->pExtension);
				JS_FREE(pItem);
			}
		}
	}
	return nRet;
}

int JS_HttpServer_DoAPICommand(JS_HANDLE hSession, int nCommand, int nIntParam, const char * strParam, int nParamSize, char * pRetBuffer, int nRetBuffSize)
{
	int nRet = 0;
	int nIndex = 0;
	char strTemp[512];
	JS_HttpServer_SessionItem * pSession = (JS_HttpServer_SessionItem *)hSession;
	JS_HANDLE hSessionLock;
	////check validity
	if(pSession==NULL ||  JS_UTIL_CheckSocketValidity(pSession->nInSock)<0) {
		nRet = -1;
		DBGPRINT("do api cmd: wrong session item, maybe zombie...\n");
		goto LABEL_CATCH_ERROR;
	}
	hSessionLock = pSession->pPoolItem->hMutex;
	////check status
	JS_UTIL_LockMutex(hSessionLock);
	if(pSession->pReq && pSession->pReq->nQueueStatus < JS_REQSTATUS_WAITCGI) {
		JS_UTIL_UnlockMutex(hSessionLock);
		DBGPRINT("do api cmd: wrong req status %d,%d maybe head method?\n",nCommand,pSession->pReq->nQueueStatus);
		goto LABEL_CATCH_ERROR;
	}
	JS_UTIL_UnlockMutex(hSessionLock);
	switch(nCommand) {
		case JS_HTTPAPI_CMD_ASYNCDONE:
			JS_HttpServer_ClearSessionRef(pSession);
			break;
		case JS_HTTPAPI_CMD_GETVARIABLE:
			JS_UTIL_LockMutex(hSessionLock);
			if(pSession->pVarData) {
				if(strlen(strParam)>0) {
					JS_STRPRINTF(strTemp,512,"%s=",strParam);
					JS_UTIL_ExtractString(pSession->pVarData,strTemp,"&",pSession->nVarDataLen,pRetBuffer,nRetBuffSize,0,&nIndex);
				}else {
					JS_UTIL_StrCopySafe(pRetBuffer,nRetBuffSize,pSession->pVarData,pSession->nVarDataLen);
				}
			}else
				pRetBuffer[0] = 0;
			JS_UTIL_UnlockMutex(hSessionLock);
			nRet = strlen(pRetBuffer);
			break;
		case JS_HTTPAPI_CMD_GETFILELIST:
			JS_UTIL_LockMutex(hSessionLock);
			if(pSession->hPostFileList && JS_List_GetSize(pSession->hPostFileList)>0) {
				JS_HANDLE hItemPosFile;
				char * pVal;
				int nStrLen = 0;
				hItemPosFile = NULL;
				while(1) {
					hItemPosFile = JS_List_GetNext(pSession->hPostFileList,hItemPosFile);
					if(hItemPosFile==NULL)
						break;
					pVal = (char *)JS_List_GetDataFromIterateItem(hItemPosFile);
					if(nStrLen<nRetBuffSize && pVal){
						if(nStrLen==0)
							JS_STRPRINTF(pRetBuffer+nStrLen,nRetBuffSize-nStrLen,"%s",pVal);
						else
							JS_STRPRINTF(pRetBuffer+nStrLen,nRetBuffSize-nStrLen,"\n%s",pVal);
						nStrLen = strlen(pRetBuffer);
					}
				}
			}
			JS_UTIL_UnlockMutex(hSessionLock);
			break;
		case JS_HTTPAPI_CMD_GETPEERIP:
			JS_STRPRINTF(pRetBuffer,nRetBuffSize,"%s",pSession->strIP);
			break;
		case JS_HTTPAPI_CMD_SENDHEADERRAW:
		case JS_HTTPAPI_CMD_SENDBODYRAW:
			if((nRet=JS_HttpServer_SendRspQueue(pSession,(char*)strParam,nParamSize))<0) {
				DBGPRINT("do http api:connection closed from client sendx\n");
			}
			break;
		case JS_HTTPAPI_CMD_SENDTEXTRSP:
			if(strParam) {
				int nSize;
				char strTemp[JS_CONFIG_MAX_SMALLURL+4];
				nSize = strlen(strParam);
				JS_HttpServer_BuildDefaultMsg(pSession,nSize);
				JS_UTIL_HTTP_AddField(pSession->pRsp->hFieldList, "Content-Type", "text/html");
				nRet = JS_UTIL_HTTP_BuildStaticRspString(pSession->pRsp,200,NULL,strTemp,JS_CONFIG_MAX_SMALLURL);
				if(nRet>=0)
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strTemp,pSession->pRsp->nRspLen);
				if(nRet>0) {
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strParam,nSize);
				}
				if(nRet<0) {
					DBGPRINT("do http api: sending text rsp failed\n");
				}
			}
			break;
		case JS_HTTPAPI_CMD_SENDJSONRSP:
			if(strParam) {
				int nSize;
				char strTemp[JS_CONFIG_MAX_SMALLURL];
				nSize = strlen(strParam);
				JS_HttpServer_BuildDefaultMsg(pSession,nSize);
				JS_UTIL_HTTP_AddField(pSession->pRsp->hFieldList, "Content-Type", "application/json");
				nRet = JS_UTIL_HTTP_BuildStaticRspString(pSession->pRsp,200,NULL,strTemp,JS_CONFIG_MAX_SMALLURL);
				if(nRet>=0)
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strTemp,pSession->pRsp->nRspLen);
				if(nRet>0) {
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strParam,nSize);
				}
				if(nRet<0) {
					DBGPRINT("do http api: sending json rsp failed\n");
				}
			}
			break;
		case JS_HTTPAPI_CMD_SENDXMLRSP:
			if(strParam) {
				int nSize;
				char strTemp[JS_CONFIG_MAX_SMALLURL+4];
				nSize = strlen(strParam);
				JS_HttpServer_BuildDefaultMsg(pSession,nSize+strlen(JS_DEFAULT_XML_DEC));
				JS_UTIL_HTTP_AddField(pSession->pRsp->hFieldList, "Content-Type", "application/xml");
				nRet = JS_UTIL_HTTP_BuildStaticRspString(pSession->pRsp,200,NULL,strTemp,JS_CONFIG_MAX_SMALLURL);
				if(nRet>=0)
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strTemp,pSession->pRsp->nRspLen);
				if(nRet>0) {
					JS_STRPRINTF(strTemp,JS_CONFIG_MAX_SMALLURL,JS_DEFAULT_XML_DEC);
					nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strTemp,strlen(strTemp));
					if(nRet>0) {
						nRet = JS_HttpServer_SendRspQueue(pSession,(char*)strParam,nSize);
					}
				}
				if(nRet<0) {
					DBGPRINT("do http api: sending xml rsp failed\n");
				}
			}
			break;
		case JS_HTTPAPI_CMD_SENDERRORRSP:
			if(strParam) {
				char strTemp[JS_CONFIG_MAX_SMALLURL];
				nRet = JS_HttpServer_SendErrorPageWithErrorString(pSession,nIntParam,strTemp,JS_CONFIG_MAX_SMALLURL,0,strParam);
				if(nRet<0) {
					DBGPRINT("do http api: sending error rsp failed\n");
				}
			}
			break;
		case JS_HTTPAPI_CMD_SENDFILE:
			if(strParam) {
				JS_EventLoop * pIO;
				JS_HttpServerGlobal * pHttpServer;
				JS_HTTP_Request * pReq;
				char strTemp[JS_CONFIG_MAX_SMALLPATH];
				char strPathBuff[JS_CONFIG_MAX_SMALLPATH];
				pIO = pSession->pIO;
				pReq = pSession->pReq;
				pHttpServer = (JS_HttpServerGlobal *)pSession->pHttpObject;
				if(pIO==NULL || pReq==NULL || pHttpServer==NULL) {
					nRet = -1;
					DBGPRINT("do http api: sending file failed (null param)\n");
					goto LABEL_CATCH_ERROR;
				}
				JS_UTIL_StrCopySafe(strPathBuff,JS_CONFIG_MAX_SMALLPATH,strParam,0);
				if(JS_HttpServer_CheckResource(pHttpServer,pIO,pSession,strPathBuff,strTemp,sizeof(strTemp),1)<0)
					return -1;
				nRet = JS_HttpServer_SendFileReqHeader(pIO,pSession,strPathBuff,strTemp,sizeof(strTemp));
			}
			break;
		case JS_HTTPAPI_CMD_SETDOWNLOADABLE:
			pSession->nForceDownload = 1;
			break;
	}
LABEL_CATCH_ERROR:
	if(nRet<0 && pSession) {
		////free zombie
		JS_HttpServer_ClearSessionRef(pSession);
	}
	return nRet;
}


///////////////////////////////////////////////////////////////////////////////
/////inner functions////////////////////////
static int JS_HttpServer_BuildDefaultMsg(JS_HttpServer_SessionItem * pItem, UINT64 nLength)
{
	JS_HTTP_Response * pRsp;

	if(pItem==NULL || pItem->pRsp==NULL)
		return -1;
	pRsp = pItem->pRsp;
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Server","JoseServer 1.0");
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Accept-Ranges","bytes");
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Connection","Keep-Alive");
	JS_UTIL_HTTP_AddIntField(pRsp->hFieldList,"Content-Length",nLength);
	return 0;
}

static int JS_HttpServer_IOHandler(JS_EventLoop * pIO , JS_POOL_ITEM_T * pPoolItem, JS_FD_T * pRDSet,JS_FD_T * pWRSet)
{
	JS_SOCKET_T nTmpSock;
	int nRet = 0;
	int nIsInFDSet = 0;
	int nCnt = 0;
	JS_HttpServer_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	nTmpSock = pSessionItem->nInSock;
	if(JS_UTIL_CheckSocketValidity(nTmpSock)<0) {
		return -1;
	}
	if(pRDSet && JS_FD_ISSET(nTmpSock,pRDSet)) {
		nIsInFDSet = 1;
	}
	while(nCnt<2) {
		nRet = JS_HttpServer_TryToRead(pIO,pSessionItem,nIsInFDSet);
		if(pSessionItem->nIsProxyReq) {
			////transfer this pool item to the proxy server
			JS_HttpServer_ResetSession(pSessionItem,NULL);
			pSessionItem->nIsProxyReq = 0;
			JS_EventLoop_TransferSessionItemToOtherHandler(pIO,pSessionItem->pPoolItem,pSessionItem->nInSock,pSessionItem->pReq,pSessionItem->pRsp,JS_DATAID_PROXY);
			nRet = 0;
			break;
		}
		if(nRet>=0) {
			nRet = JS_HttpServer_TryToSend(pIO,pSessionItem,0);
		}
		////check req queue
		if(nRet<0 || pSessionItem->pReq==NULL)
			break;
		if(JS_SimpleQ_CheckAvailableData(pSessionItem->pReq->hQueue)==0) ////no data in req q 
		{
			break;
		}
		nIsInFDSet = 0;
		nCnt++;
	}
	if(nRet<0) {
		////remove socket before delete zombie object
		if(JS_UTIL_CheckSocketValidity(pSessionItem->nInSock)>0) {
			JS_UTIL_SocketClose(pSessionItem->nInSock);
			JS_EventLoop_SetOutputFd(pIO,pSessionItem->nInSock,0,1);
			JS_EventLoop_SetInputFd(pIO,pSessionItem->nInSock,0,1);
			pSessionItem->nInSock = -1;
		}
	}
	return nRet;
}


__inline static JS_HttpServer_SessionItem * _RET_SESSIONITEM_(JS_POOL_ITEM_T* pItem)
{
	return (JS_HttpServer_SessionItem * )pItem->pMyData;
}

static int JS_HttpServer_ResetSession(JS_HttpServer_SessionItem * pItem, JS_HTTP_Request * pReq)
{
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	if(pItem->nIsProxyReq==0) {
		if(pReq==NULL)
			pReq = pItem->pReq;
		if(pReq)
			pReq->nQueueStatus = JS_REQSTATUS_WAITREQ;
	}
	JS_UTIL_FileDestroy(&pItem->hFile);
	if(pItem->pBoundary)
		JS_FREE(pItem->pBoundary);
	pItem->pBoundary = NULL;
	if(pItem->pBoundaryEof)
		JS_FREE(pItem->pBoundaryEof);
	pItem->pBoundaryEof = NULL;
	if(pItem->hPostFileList)
		JS_List_Destroy(pItem->hPostFileList);
	pItem->hPostFileList = NULL;
	if(pItem->pFileName)
		JS_FREE(pItem->pFileName);
	pItem->pFileName = NULL;
	if(pItem->pVarData)
		JS_FREE(pItem->pVarData);
	pItem->pVarData = NULL;
	pItem->nVarDataLen = 0;
	if(pItem->pRsp && pItem->pRsp->hFieldList)
		JS_UTIL_HTTP_ClearFIeldList(pItem->pRsp->hFieldList);
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	return 0;
}

static int JS_HttpServer_SessionItem_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nRet = 0;
	JS_HttpServer_SessionItem * pItem =  NULL;
	JS_EventLoop	* pIO = NULL;

	////common initializing
	pItem =  _RET_SESSIONITEM_(pPoolItem);
	pIO = (JS_EventLoop	*)pOwner;

	////do something according to the phase
	if(nNewPhase==JS_POOL_PHASE_HOT) {
		memset((char*)pItem,0,sizeof(JS_HttpServer_SessionItem));
	}else if(nNewPhase==JS_POOL_PHASE_WARM) {
		; ///do nothing
	}else if(nNewPhase==JS_POOL_PHASE_COLD) {
		JS_HttpServer_ResetSession(pItem,NULL);
		if(pItem->pReq)
			JS_UTIL_HTTP_DeleteRequest(pItem->pReq);
		if(pItem->pRsp)
			JS_UTIL_HTTP_DeleteResponse(pItem->pRsp);
		memset((char*)pItem,0,sizeof(JS_HttpServer_SessionItem));
	}
	return nRet;
}


static JS_SOCKET_T JS_HttpServer_GetSocket(JS_POOL_ITEM_T * pPoolItem)
{
	JS_HttpServer_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	return pSessionItem->nInSock;
}

static int JS_HttpServer_AddItem(JS_EventLoop * pIO, JS_POOL_ITEM_T * pPoolItem, JS_SOCKET_T nInSock)
{
	int nRet = 0;
	int nSize;
	struct sockaddr_in rcAddr;
	JS_SOCKET_T nTmpSock = nInSock;
	JS_HttpServer_SessionItem * pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	////item init
	pSessionItem = _RET_SESSIONITEM_(pPoolItem);
	pSessionItem->nInSock = nTmpSock;
	pSessionItem->pPoolItem = pPoolItem;
	pSessionItem->pIO = pIO;
	pSessionItem->pHttpObject = JS_GetHttpServerHandle(pIO->pOwner);
	nSize = sizeof(struct sockaddr_in);
	if(getpeername(nTmpSock, (struct sockaddr *)&rcAddr, &nSize)==0) {
		JS_STRPRINTF(pSessionItem->strIP,sizeof(pSessionItem->strIP),"%s",inet_ntoa(rcAddr.sin_addr));
	}
	JS_3Pool_MaskSetDataID(pPoolItem,JS_DATAID_HTTPSERVER);
	return nRet;
}

static int JS_HttpServer_SendOptionHeader(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, char * strTemp, int nBuffSize)
{
	int nRet = 0;
	JS_STRPRINTF(strTemp,nBuffSize,JS_HTTPSERVER_OPTION_RSP,JS_HTTPSERVER_ALLOWED_METHODS);
	////send header
	if(JS_HttpServer_SendRspQueue(pItem,strTemp,strlen(strTemp))<0) {
		DBGPRINT("send option: connection closed from client send\n");
		nRet = -1;
	}
	return nRet;
}

static int JS_HttpServer_SendErrorPageWithErrorString(JS_HttpServer_SessionItem * pItem, int nError, char * pBuffer, int nBuffLen, int nNeedToClose, const char * pErrorString) 
{
	int nRet =0;
	int nSize=0;
	int nBodySize = 0;
	char * pHeaderBuffer;
	char strEnoughBuff[JS_CONFIG_MAX_SMALLURL+4];
	JS_HTTP_Response * pRsp;
	pRsp = pItem->pRsp;
	if(pRsp==NULL)
		return -1;
	JS_STRPRINTF(strEnoughBuff,JS_CONFIG_MAX_SMALLURL,JS_HTTPSERVER_ERROR_PAGE,nError);
	nBodySize = strlen(strEnoughBuff);
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Server","JoseServer 1.0");
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Accept-Ranges","bytes");
	JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Connection","Close");
	JS_UTIL_HTTP_AddIntField(pRsp->hFieldList,"Content-Length",nBodySize);
	pHeaderBuffer = JS_UTIL_HTTP_BuildRspString(pRsp,nError,pErrorString);
	if(pHeaderBuffer==NULL)
		return -1;
	nRet = JS_HttpServer_SendRspQueue(pItem,pHeaderBuffer,pRsp->nRspLen);
	if(nRet>0)
		nRet = JS_HttpServer_SendRspQueue(pItem,strEnoughBuff,nBodySize);
	JS_FREE(pHeaderBuffer);
	if(nNeedToClose) {
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		pItem->pReq->nQueueStatus = JS_REQSTATUS_NEEDTOCLOSE;
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	}else {
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		pItem->pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	}
	return pRsp->nRspLen+nBodySize;
}

static int JS_HttpServer_SendErrorPage(JS_HttpServer_SessionItem * pItem, int nError, char * pBuffer, int nBuffLen, int nNeedToClose) 
{
	return JS_HttpServer_SendErrorPageWithErrorString(pItem,nError,pBuffer,nBuffLen,nNeedToClose,"error");
}

static int JS_HttpServer_SendFileReqHeader(JS_EventLoop * pIO, JS_HttpServer_SessionItem * pItem, char * strPath, char * strTemp, int nBuffSize)
{
	int nRet = 0;
	char strDate[64];
	char strETAG[64];
	char strLM[64];
	char strRange[128];
	time_t nCurTime;
	int nRspCode = 0;
	const char * pReqEtag;
	static char strNotModified[] =  "Not Modified";
	char * pRspStatus = NULL;
	JS_HTTP_Request * pReq;
	JS_HTTP_Response * pRsp;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)pItem->pHttpObject;

	pReq = pItem->pReq;
	pRsp = pItem->pRsp;

	JS_UTIL_HTTP_MakeETAG(strETAG,sizeof(strETAG),strPath, strLM, sizeof(strLM));
	pReqEtag = JS_UTIL_GetHTTPRequestHeader(pReq,"If-None-Match");
	if(pReqEtag) {
		if(JS_UTIL_StrCmpRestrict(pReqEtag,strETAG,0,0,0)==0) {
			////send http 304
			nRspCode = 304;
			pRspStatus = strNotModified;
			JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Server","JoseServer 1.0");
			JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Connection","Keep-Alive");
			JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Last-Modified",strLM);
			JS_UTIL_HTTP_AddField(pRsp->hFieldList,"ETag",strETAG);
			JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
			pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
			JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
			JS_UTIL_FileDestroy(&pItem->hFile);
		}
	}
	if(nRspCode==0) {
		nCurTime = JS_UTIL_GetSecondsFrom1970();
		JS_UTIL_HTTP_GmtTimeString(strDate, sizeof(strDate), &nCurTime);
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Server","JoseServer 1.0");
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Accept-Ranges","bytes");
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Connection","Keep-Alive");
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Date",strDate);
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Last-Modified",strLM);
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"ETag",strETAG);
		JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Content-Type",JS_HttpServer_GetMimeType(pHttpServer,strPath,pItem));
	}
	if(nRspCode==0) {
		if(pRsp->nRangeEndOffset > 0) {
			nRspCode = 206;
			JS_STRPRINTF(strRange,100,"bytes=%llu-%llu",pRsp->nRangeStartOffset,pRsp->nRangeStartOffset+pRsp->nRangeLen-1);
			JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Content-Range",strRange);
		}else {
			nRspCode = 200;
			JS_UTIL_HTTP_AddIntField(pRsp->hFieldList,"Content-Length",pRsp->nRangeLen);
			if(pItem->nForceDownload && pItem->pFileName) {
				int nStrLen = strlen(strTemp);
				char strURLEncode[256];
				nStrLen = nStrLen-2;
				JS_UTIL_StrURLEncode(pItem->pFileName,strURLEncode,256);
				JS_STRPRINTF(strTemp,nBuffSize,"attachment; filename=\"%s\"",strURLEncode);
				JS_UTIL_HTTP_AddField(pRsp->hFieldList,"Content-Disposition",strTemp);
			}
		}
	}
	JS_UTIL_HTTP_BuildStaticRspString(pRsp, nRspCode,  pRspStatus,strTemp, nBuffSize);
	////send header
	if(JS_HttpServer_SendRspQueue(pItem,strTemp,pRsp->nRspLen)<0) {
		DBGPRINT("connection closed from client sendx\n");
		nRet = -1;
		goto LABEL_EXIT_DOREQUEST_GET;
	}
LABEL_EXIT_DOREQUEST_GET:
	return nRet;
}



static int JS_HttpServer_SendRspQueue(JS_HttpServer_SessionItem * pItem, char * pData, int nSize)
{
	int nRet = 0;
	JS_HTTP_Response * pRsp;
	pRsp = pItem->pRsp;
	if(pRsp==NULL)
		return -1;
	////check this item is zombie or not
	if(JS_UTIL_CheckSocketValidity(pItem->nInSock)<0) {
		return -1;
	}
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	if(JS_SimpleQ_PushPumpIn(pRsp->hQueue,pData,nSize)<0)
		nRet = -1;
	else
		nRet = nSize;
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	return nRet;
}

static int JS_HttpServer_ParsePostSingleBody(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, JS_HTTP_Request * pReq, char * pBuff, int nBuffSize) 
{
	int nRet = 0;
	char * pReqHeader;
	JSUINT nAvailableSize;
	char * pReqData;
	if(JS_SimpleQ_GetDataSize(pReq->hQueue)>=pReq->nRangeLen) {
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		if(pItem->pVarData==NULL) {
			pReqData = (char*) JS_ALLOC((unsigned int)pReq->nRangeLen+4);
			if(pReqData==NULL) {
				nRet = -1;
			}
			pReqData[pReq->nRangeLen] = 0;
			pItem->pVarData=pReqData;
			pItem->nVarDataLen = 0;
		}
		pReqData = pItem->pVarData;
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		if(nRet<0) {
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
		
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		while(1) {
			pReqHeader = JS_SimpleQ_PreparePumpOut(pReq->hQueue, 0, &nAvailableSize, NULL, 0, NULL);
			if(pReqHeader==NULL) {
				break;
			}
			memcpy(pReqData+pItem->nVarDataLen,pReqHeader,nAvailableSize);
			JS_SimpleQ_FinishPumpOut(pReq->hQueue,nAvailableSize);
			pItem->nVarDataLen += nAvailableSize;
			if(pItem->nVarDataLen>=pReq->nRangeLen) {
				break;
			}
		}
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		if(pItem->nVarDataLen>=pReq->nRangeLen) {
			JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
			pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
			JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		}
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pReqData) {
			JS_FREE(pReqData);
		}
		nRet = JS_HttpServer_SendErrorPage(pItem,500,pBuff,nBuffSize,0);
	}
	return nRet;
}

static char * JS_HttpServer_PasteVariable(char * pData, char * pNew, int nIsKey) 
{
	int nRet = 0;
	int nIsNew = 0;
	int nNewLen;
	int nLastPos = 0;
	char * pDest;
	nNewLen = strlen(pNew);
	pDest = (char*)JS_ALLOC(nNewLen*2);
	if(pDest==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	nNewLen = JS_UTIL_StrURLEncode(pNew,pDest,nNewLen*2);
	if(nNewLen<0) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	if(pData==NULL) {
		nIsNew = 1;
		pData = (char*)JS_ALLOC(nNewLen+4);
		nLastPos = 0;
	}else {
		nLastPos = strlen(pData);
		pData = (char*)JS_REALLOC(pData, nLastPos+nNewLen+4);
	}
	if(pData==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	if(nIsNew==0 && nIsKey) {
		pData[nLastPos] = '&';
		nLastPos++;
	}else if(nIsKey==0) {
		pData[nLastPos] = '=';
		nLastPos++;
	}
	JS_UTIL_StrCopySafe(pData+nLastPos,nNewLen+1,pDest,nNewLen);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		pData = NULL;
	}
	if(pDest)
		JS_FREE(pDest);
	return pData;
}

static int JS_HttpServer_ParsePostMultiPart(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, JS_HTTP_Request * pReq,  char * pBuff, int nBuffSize) 
{
	char * pReqData;
	char * pTok;
	char * pDisposition;
	int nError = 0;
	int nIndex=0;
	int nEofLen;
	unsigned int nWriteSize;
	JSUINT nAvailableSize;
	int nRet = 0;
	char strPath[JS_CONFIG_MAX_SMALLPATH];
	int nIsNew;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)pItem->pHttpObject;

	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	if(pReq->nQueueStatus == JS_REQSTATUS_READ_MULTIHEADER) {
		pReqData = JS_SimpleQ_PreparePumpOut(pReq->hQueue, 0, &nAvailableSize, "\r\n\r\n", 0, &nIsNew);
		if(pReqData==NULL)
			goto LABEL_CATCH_ERROR;
		if(nIsNew) {
			////get form data type
			pTok = JS_UTIL_ExtractString(pReqData,"Content-Disposition","\r\n",nAvailableSize,pBuff,nBuffSize,0,&nIndex);
			if(pTok==NULL) {
				DBGPRINT("content-disposition: there's no key value\n");
				nError = 1;
				goto LABEL_CATCH_ERROR;
			}
			pDisposition = JS_UTIL_StrDup(pTok);
			if(pDisposition==NULL) {
				DBGPRINT("parse multipart: no mem error 1\n");
				nError = 1;
				goto LABEL_CATCH_ERROR;
			}
			pTok = JS_UTIL_ExtractString(pDisposition,"filename=\"","\"",0,pBuff,nBuffSize,1,&nIndex);
			if(pTok) {
				//DBGPRINT("TMP:multipart file name=%s %u\n",pTok,strlen(pTok));
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
				JS_STRPRINTF(strPath,JS_CONFIG_MAX_SMALLPATH,"%s\\%s",pHttpServer->pUploadDir,pTok);
#else
				JS_STRPRINTF(strPath,JS_CONFIG_MAX_SMALLPATH,"%s/%s",pHttpServer->pUploadDir,pTok);
#endif
				pItem->hFile = JS_UTIL_FileOpenBinary(strPath,0,0);
				if(pItem->hFile==NULL) {
					DBGPRINT("parse multipart: can't write tmp file check permission!\n");
					nError = 1;
					goto LABEL_CATCH_ERROR;
				}
				pTok = JS_UTIL_StrDup(strPath);
				if(pTok)
					JS_List_PushBack(pItem->hPostFileList,pTok);
				else {
					DBGPRINT("parse multipart: no mem error (filepath)!\n");
					nError = 1;
					goto LABEL_CATCH_ERROR;
				}
			}else {
				pTok = JS_UTIL_ExtractString(pDisposition,"name=\"","\"",0,pBuff,nBuffSize,1,&nIndex);
				if(pTok) {
					pItem->pVarData = JS_HttpServer_PasteVariable(pItem->pVarData,pBuff,1);
					if(pItem->pVarData==NULL) {
						nError = 1;
						goto LABEL_CATCH_ERROR;
					}
					pItem->nVarDataLen = strlen(pItem->pVarData);
				}else {
					DBGPRINT("parse multipart: no mem error (key)!\n");
					nError = 1;
					goto LABEL_CATCH_ERROR;
				}
				pItem->hFile=NULL;
			}
			////clear temp pDisposition
			JS_FREE(pDisposition);
			pDisposition = NULL;
			////clear read item frome queue
			JS_SimpleQ_FinishPumpOut(pReq->hQueue,nAvailableSize);
			pReq->nQueueStatus = JS_REQSTATUS_READ_MULTIBODY;
		}
	}else if(pReq->nQueueStatus == JS_REQSTATUS_READ_MULTIBODY) {
		pReqData = JS_SimpleQ_PreparePumpOut(pReq->hQueue, 0, &nAvailableSize, pItem->pBoundary, 0, &nIsNew);
		if(pReqData==NULL)
			goto LABEL_CATCH_ERROR;
		if(nIsNew && pItem->hFile==NULL) {
			pReqData[nAvailableSize-2-strlen(pItem->pBoundary)] = 0; ///skip CRLF
			pItem->pVarData = JS_HttpServer_PasteVariable(pItem->pVarData,pReqData,0);
			if(pItem->pVarData==NULL) {
				nError = 1;
				goto LABEL_CATCH_ERROR;
			}
			pItem->nVarDataLen = strlen(pItem->pVarData);
			JS_SimpleQ_FinishPumpOut(pReq->hQueue,nAvailableSize);
		}else if(pItem->hFile!=NULL){
			////file operation
			if(nIsNew)
				nWriteSize = nAvailableSize-2-strlen(pItem->pBoundary);
			else
				nWriteSize = nAvailableSize;
			if(nIsNew)
				nWriteSize = JS_UTIL_FileWriteBlocking(pItem->hFile,pReqData,nWriteSize);
			else
				nWriteSize = JS_UTIL_FileWriteSome(pItem->hFile,pReqData,nWriteSize);
			if(nWriteSize<0) {
				nError = 1;
				DBGPRINT("multipart: file write error\n");
				goto LABEL_CATCH_ERROR;
			}
			if(nIsNew)
				JS_SimpleQ_FinishPumpOut(pReq->hQueue,nAvailableSize);
			else
				JS_SimpleQ_FinishPumpOut(pReq->hQueue,nWriteSize);
		}
		if(nIsNew) { ////go to header status
			pReq->nQueueStatus = JS_REQSTATUS_READ_MULTIHEADER;
			if(pItem->hFile)
				JS_UTIL_FileDestroy(&pItem->hFile);
			////check eof
			nEofLen = strlen(pItem->pBoundaryEof);
			JS_SimpleQ_PreparePumpOut(pReq->hQueue,nEofLen+2, &nAvailableSize,pItem->pBoundaryEof, nEofLen, &nIsNew);
			if(nIsNew) {
				pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
			}
		}
	}
LABEL_CATCH_ERROR:
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	if(nError) {
		if(pDisposition)
			JS_FREE(pDisposition);
		nRet = JS_HttpServer_SendErrorPage(pItem,500,pBuff,nBuffSize,1);	///will be closed after send error pagge
	}
	return nRet;
}

static int JS_HttpServer_PreparePostMultiPart(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, const char * pContentType, char * pBuff, int nBuffSize) 
{
	int nRet = 0;
	char * pTok;
	int nIndex;
	int nIsAdded = 0;
	char strBound[256];

	pTok = JS_UTIL_ExtractString(pContentType,"boundary=","",0,strBound,sizeof(strBound),0,&nIndex);
	if(pTok==NULL) {
		DBGPRINT("POST: no boundary data\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	if(pItem->pBoundary || pItem->hPostFileList) {
		DBGPRINT("prepare multipart: boundary is not deleted before error\n");
	}
	pItem->pBoundary = (char*)JS_ALLOC(strlen(pTok)+8);
	pItem->pBoundaryEof = (char*)JS_ALLOC(strlen(pTok)+8);
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	pItem->hPostFileList = JS_List_Create(pItem,NULL);
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	if(pItem->pBoundary==NULL ||
		pItem->pBoundaryEof==NULL ||
		pItem->hPostFileList==NULL) {
		 nRet = -1;
		 goto LABEL_CATCH_ERROR;
	}
	JS_STRPRINTF(pItem->pBoundary,260,"--%s",strBound);
	//JS_STRPRINTF(pItem->pBoundaryEof,260,"%s--",pItem->pBoundary);
	JS_STRPRINTF(pItem->pBoundaryEof,260,"--");
LABEL_CATCH_ERROR:
	return nRet;
}

static int JS_HttpServer_CheckResource(JS_HttpServerGlobal * pHttpServer, JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, char * pPathBuff, char * pBuffer, int nBuffLen, int nIsFromCGI)
{
	int nRet = 0;
	int nCnt;
	int nReqIsMine = 0;
	JS_HTTP_Request * pReq;
	JS_HTTP_Response * pRsp;
	char strFileName[JS_CONFIG_MAX_SMALLFILENAME];
	JS_HttpServer_DirectAPIItem * pRes = NULL;
	HTTPSIZE_T nResSize;

	pReq = pItem->pReq;
	pRsp = pItem->pRsp;

	////check req is mine or proxy's
	if(pReq->nTargetIP>0) {
		for(nCnt=0; nCnt<pHttpServer->nNetworkDeviceNum; nCnt++) {
			if(pReq->nTargetIP == pHttpServer->arrHostIP[nCnt]) {
				nReqIsMine = 1;
				break;
			}
		}
	}
	if(pReq->pHost && nReqIsMine==0) {
		if(JS_UTIL_StrCmpRestrict(pHttpServer->strHostName,pReq->pHost,0,0,0)==0)
			nReqIsMine=1;
	}
	if(nReqIsMine==0) {
		////transfer the request to the proxy
		pItem->nIsProxyReq = 1;
		nRet = 1;
		goto LABEL_CATCH_ERROR;
	}
	JS_UTIL_HTTP_GetReqResourceName(pItem->pReq,pBuffer,JS_CONFIG_MAX_SMALLPATH);
	if(nIsFromCGI == 0)
		pRes = (JS_HttpServer_DirectAPIItem *)JS_HashMap_Find(pHttpServer->hDirectAPIMap,pBuffer,JS_HttpServer_DirectAPIItem_FindCallback);
	if(pRes) {
		pItem->nCGIType = pRes->nType;
		pItem->pTmpRes = pRes;
	}else {
		if(nIsFromCGI == 0 && JS_UTIL_HTTP_CheckReqMethod(pItem->pReq,"POST")) {
			////POST not allowed for file request 
			nRet = JS_HttpServer_SendErrorPage(pItem,405,pBuffer,nBuffLen,0);
			if(nRet>=0)
				nRet =1;
		}else {
			////action for file request
			if(nIsFromCGI==0) {
				pItem->nCGIType = JS_CGITYPE_FILE;
			}
			if(pHttpServer->pDownloadDir==NULL) {
				DBGPRINT("check resource: no directory registered error\n");
				nRet = JS_HttpServer_SendErrorPage(pItem,404,pBuffer,nBuffLen,0);
				if(nRet>=0)
					nRet =1;
				goto LABEL_CATCH_ERROR;
			}
			if(nIsFromCGI==0)
				JS_UTIL_HTTP_ConvertURLtoLocalFilePath(pHttpServer->pDownloadDir,pReq->pURL+pReq->nResOffset
											,pPathBuff, JS_CONFIG_MAX_SMALLPATH,pReq->nURLLen-pReq->nResOffset);
			DBGPRINT("TMP: check resource: %s\n",pPathBuff);
			pItem->hFile = JS_UTIL_FileOpenBinary(pPathBuff,1,0);
			if(pItem->hFile==NULL)  {
				DBGPRINT("check resource: file open error\n");
				nRet = JS_HttpServer_SendErrorPage(pItem,404,pBuffer,nBuffLen,0);
				if(nRet>=0)
					nRet =1;
				goto LABEL_CATCH_ERROR;
			}
			nResSize = JS_UTIL_GetFileSize(pItem->hFile);
			if(nResSize<=0) {
				DBGPRINT("check resource: no file found error\n");
				nRet = JS_HttpServer_SendErrorPage(pItem,404,pBuffer,nBuffLen,0);
				if(nRet>=0)
					nRet =1;
				goto LABEL_CATCH_ERROR;
			}
			////check size
			if(pReq->nRangeStartOffset > 0) {
				if(pReq->nRangeEndOffset<=0) {
					pReq->nRangeEndOffset = nResSize-1;
					pReq->nRangeLen = nResSize-pReq->nRangeStartOffset;
				}
				if(pReq->nRangeStartOffset+pReq->nRangeLen>nResSize) {
					pReq->nRangeLen = nResSize-pReq->nRangeStartOffset;
					pReq->nRangeEndOffset = nResSize-1;
				}else {
					nResSize = pItem->pReq->nRangeLen;
				}
				pRsp->nRangeEndOffset = pReq->nRangeEndOffset;
				pRsp->nRangeStartOffset = pReq->nRangeStartOffset;
				pRsp->nRangeLen = pReq->nRangeLen;
				DBGPRINT("TMP: partial check resource: %llu, %llu, %llu\n",pRsp->nRangeStartOffset,pRsp->nRangeEndOffset,pRsp->nRangeLen);
				JS_UTIL_SetFilePos(pItem->hFile,pReq->nRangeStartOffset);
			}else {
				pRsp->nRangeEndOffset = 0;
				pRsp->nRangeStartOffset = 0;
				pRsp->nRangeLen = nResSize;
				JS_UTIL_SetFilePos(pItem->hFile,0);
			}
			////save file name
			strFileName[JS_CONFIG_MAX_SMALLFILENAME-1] = 0;
			JS_UTIL_ExtractFileName(pPathBuff,strFileName,JS_CONFIG_MAX_SMALLFILENAME);
			pItem->pFileName = JS_UTIL_StrDup(strFileName);
			if(pItem->pFileName==NULL) {
				nRet = -1;
				DBGPRINT("check resource: can't alloc file name, mem error\n");
				goto LABEL_CATCH_ERROR;
			}
			////set rsp q
			JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
			JS_SimpleQ_ResetTotallSize(pRsp->hQueue,pRsp->nRangeLen);
			JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		}
	}
LABEL_CATCH_ERROR:
	return nRet;
}

static int JS_HttpServer_CheckMethod(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, char * pBuffer, int nBuffLen)
{
	int nRet = 0;
	if(JS_UTIL_HTTP_CheckReqMethod(pItem->pReq,"OPTIONS")) {
		nRet = JS_HttpServer_SendOptionHeader(pIO, pItem,pBuffer,nBuffLen);
		if(nRet>=0)
			nRet =1;
	}else if(JS_UTIL_HTTP_CheckReqMethod(pItem->pReq,JS_HTTPSERVER_ALLOWED_METHODS)==0){
		nRet = JS_HttpServer_SendErrorPage(pItem,405,pBuffer,nBuffLen,0);
		if(nRet>=0)
			nRet =1;
	}
	return nRet;
}

static int JS_HttpServer_TryToRead(JS_EventLoop * pIO,JS_HttpServer_SessionItem * pItem, int nOption)
{
	int nRet = 0;
	int nSubRet = 0;
	int nRead;
	int nIsNew;
	int nOldQStatus;
	JSUINT nAvailableSize;
	unsigned int nTime;
	int nNeedToAddWrFdSet = 0;

	char strTemp[JS_CONFIG_MAXBUFFSIZE];
	char strPath[JS_CONFIG_MAX_SMALLPATH];
	char * pReqHeader;
	JS_HANDLE	hQueue;
	JS_HTTP_Request * pReq=NULL;
	JS_HTTP_Response * pRsp=NULL;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)pItem->pHttpObject;

	pItem->nReadCnt++;
	if(pItem->pReq==NULL) {
		pItem->pReq = JS_UTIL_HTTP_PrepareRequest();
		if(pItem->pReq==NULL) {
			nRet = -1;
			DBGPRINT("check req: no mem error1\n");
			goto LABEL_EXIT_DOREQUEST;
		}
	}
	if(pItem->pRsp==NULL) {
		pItem->pRsp = JS_UTIL_HTTP_PrepareResponse();
		if(pItem->pRsp==NULL) {
			nRet = -1;
			DBGPRINT("check req: no mem error2\n");
			goto LABEL_EXIT_DOREQUEST;
		}
	}
	hQueue = pItem->pReq->hQueue;
	////read data from io socket
	if(nOption==1) {
		nTime = JS_RCV_WITHOUTSELECT;
		nRead=JS_UTIL_TCP_Recv(pItem->nInSock,strTemp,JS_CONFIG_NORMAL_READSIZE,nTime);
		//nRead=JS_UTIL_TCP_Recv(pItem->nInSock,strTemp,4,nTime);
		if(nRead<0) {
			if(pItem->pReq && pItem->pReq->pURL)
				DBGPRINT("TMP: http:client off %d %s\n",errno,pItem->pReq->pURL);
			nRet = -1;
			goto LABEL_EXIT_DOREQUEST;
		}else if(nRead==0) {
			pItem->nZeroRxCnt ++;
			if(pItem->nZeroRxCnt>JS_CONFIG_MAX_RECVZERORET) {
				if(pItem->pReq && pItem->pReq->pURL)
					DBGPRINT("TMP: http: tcp session is done RST/FIN %s\n",pItem->pReq->pURL);
				nRet = -1;
				goto LABEL_EXIT_DOREQUEST;
			}
			goto LABEL_EXIT_DOREQUEST;
		}else {
			pItem->nRcvDbgSize += nRead;
			pItem->nZeroRxCnt = 0;
			nRet = JS_SimpleQ_PushPumpIn(hQueue,strTemp,nRead);	////no need to lock for req queue
			if(nRet<0) {
				DBGPRINT("check req: no mem error2\n");
				goto LABEL_EXIT_DOREQUEST;
			}
		}
	}
	pReq = pItem->pReq;
	pRsp = pItem->pRsp;
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	nOldQStatus = pReq->nQueueStatus;
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
	if(nOldQStatus==JS_REQSTATUS_WAITREQ) {	////entry point of http request
		nIsNew = 0;
		pReqHeader = JS_SimpleQ_PreparePumpOut(hQueue, 0, &nAvailableSize, "\r\n\r\n", 4, &nIsNew); ////no need to lock for req queue
		if(pReqHeader && nIsNew) {
			nNeedToAddWrFdSet = 1;
			//////////////////////////////////////////////////////////
			////1. refresh request structure with new request string
			pItem->pReq = JS_UTIL_HTTP_CheckRequestPacket(pReqHeader, nAvailableSize, pItem->pReq);
			if(pItem->pReq==NULL) {
				DBGPRINT("check req: may be mem error3\n");
				nRet = -1;
				goto LABEL_EXIT_DOREQUEST;
			}
			JS_SimpleQ_FinishPumpOut(hQueue,nAvailableSize);
			if(JS_UTIL_HTTP_IsPostMethod(pReq) && pReq->nRangeLen>0) {
				JS_SimpleQ_ResetTotallSize(pReq->hQueue,pReq->nRangeLen);
			}
			//////////////////////////////////////////////////////////
			////2. check method
			nSubRet = JS_HttpServer_CheckMethod(pIO,pItem,strTemp,sizeof(strTemp));
			if(nSubRet!=0) {
				if(nSubRet<0)
					nRet = -1;
				goto LABEL_EXIT_DOREQUEST;
			}
			//////////////////////////////////////////////////////////
			////3. check resource
			nSubRet = JS_HttpServer_CheckResource(pHttpServer, pIO,pItem,strPath,strTemp,sizeof(strTemp),0);
			if(nSubRet!=0) {
				if(nSubRet<0)
					nRet = -1;
				goto LABEL_EXIT_DOREQUEST;
			}
			/////////////////////////////////////////////////////////
			////4. status change
			if(JS_UTIL_HTTP_CheckReqMethod(pItem->pReq,"POST")) {
				const char * pContentType;
				const char * pContentLength;
				pContentType = JS_UTIL_GetHTTPRequestHeader(pReq,"Content-Type");
				pContentLength = JS_UTIL_GetHTTPRequestHeader(pReq,"Content-Length");
				if(pContentType == NULL) {
					nRet = JS_HttpServer_SendErrorPage(pItem,406,strTemp,sizeof(strTemp),0);
					goto LABEL_EXIT_DOREQUEST;
				}
				if(JS_UTIL_StrCmp(pContentType, "multipart/form-data",0,0,1)==0) {
					nRet = JS_HttpServer_PreparePostMultiPart(pIO,pItem,pContentType,strTemp,sizeof(strTemp));
					if(nRet<0) {
						nRet = JS_HttpServer_SendErrorPage(pItem,500,strTemp,sizeof(strTemp),1);
						///multi part error mustbe closed
						goto LABEL_EXIT_DOREQUEST;
					}
					/////post upload read
					pReq->nQueueStatus = JS_REQSTATUS_READ_MULTIHEADER;
				}else if(JS_UTIL_StrCmp(pContentType, "application/x-www-form-urlencoded",0,0,1)==0) {
					if(pContentLength == NULL) {
						nRet = JS_HttpServer_SendErrorPage(pItem,411,strTemp,sizeof(strTemp),0);
						goto LABEL_EXIT_DOREQUEST;
					}
					if(pReq->nRangeLen>=JS_CONFIG_MAX_POST_DATA) {
						nRet = JS_HttpServer_SendErrorPage(pItem,404,strTemp,sizeof(strTemp),0);
						goto LABEL_EXIT_DOREQUEST;
					}
					/////post data read
					pReq->nQueueStatus = JS_REQSTATUS_READ_SINGLEBODY;
				}else {
					nRet = JS_HttpServer_SendErrorPage(pItem,400,strTemp,sizeof(strTemp),0);
					goto LABEL_EXIT_DOREQUEST;					 
				}
			}else{
				int nGetDataOffset;
				nGetDataOffset = JS_UTIL_FindPattern(pReq->pURL+pReq->nResOffset,"?",pReq->nURLLen-pReq->nResOffset,1,0);
				if(nGetDataOffset>0) {
					////get or head field data is already in the url
					JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);	////lock for zombie session
					pItem->pVarData = JS_UTIL_StrDup(pReq->pURL+pReq->nResOffset+nGetDataOffset+1);
					pItem->nVarDataLen = pReq->nURLLen-pReq->nResOffset-(nGetDataOffset+1);
					JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
				}
				pReq->nQueueStatus = JS_REQSTATUS_WAITCGI;
			}
		}//if(pReqHeader && nIsNew) {
	}//if(pReq->nQueueStatus==JS_REQSTATUS_WAITCMD)
	else if(nOldQStatus==JS_REQSTATUS_WAITCGI) {
		;
	}else if(nOldQStatus==JS_REQSTATUS_READ_SINGLEBODY) {
		nRet = JS_HttpServer_ParsePostSingleBody(pIO,pItem,pReq,strTemp,sizeof(strTemp));
	}else if(nOldQStatus==JS_REQSTATUS_READ_MULTIHEADER || pReq->nQueueStatus==JS_REQSTATUS_READ_MULTIBODY) {
		nRet = JS_HttpServer_ParsePostMultiPart(pIO,pItem,pReq, strTemp,sizeof(strTemp));
	}

	//////////////////////////////////////////////////////////////////
	/////5. after reading variable, do something
	if(nRet==0) {
		if(nOldQStatus != pReq->nQueueStatus) {
			if(pReq->nQueueStatus == JS_REQSTATUS_WAITCGI) {
				////do something
				////check CGI type
				if(pItem->nCGIType == JS_CGITYPE_ASYNCAPI) {
					JS_EventLoop_AddThread(pItem->pPoolItem);
					pItem->pTmpRes->pfDirectAPI((JS_HANDLE)pItem);
				}else if(pItem->nCGIType == JS_CGITYPE_DIRECTAPI) {
					JS_EventLoop_AddThread(pItem->pPoolItem);
					pItem->nWorkID = JS_ThreadPool_AddWorkQueue(pHttpServer->hDirectAPIWorkQ,JS_DirectAPI_WorkQFunc,pItem,JS_HttpServer_DirectAPIWorkQEventFunc);
					if(pItem->nWorkID == 0) {
						DBGPRINT("http read: error when adding work to queue\n");
						nRet = JS_HttpServer_SendErrorPage(pItem,500,strTemp,sizeof(strTemp),0);
						goto LABEL_EXIT_DOREQUEST;		
					}
				}else {
					/// else if(pItem->nCGIType == JS_CGITYPE_FILE) {
					nRet = JS_HttpServer_SendFileReqHeader(pIO,pItem,strPath,strTemp,sizeof(strTemp));
				}
			}
		}
	}
LABEL_EXIT_DOREQUEST:
	if(nRet>=0) {
		if(pItem->nIsProxyReq == 0) {
			////check rsp queue
			if(nNeedToAddWrFdSet)
				JS_EventLoop_SetOutputFd(pIO,pItem->nInSock,1,1);
		}
	}
	return nRet;
}

static int JS_HttpServer_TryToSend(JS_EventLoop * pIO, JS_HttpServer_SessionItem * pItem, int nOption)
{
	int nRet = 0;
	int nSent;
	JS_HTTP_Response * pRsp;
	JS_HTTP_Request  * pReq;
	JS_HANDLE hQueue;
	JSUINT nBuffSize;
	int  nReadSize;
	int  nPumpOutResult;
	char * pData = NULL;
	int  nSendingLoopCnt = 0;
	int  nIsReqHEADCmd = 0;
	int  nIsFileEnd = 0;

	pReq = pItem->pReq;
	pRsp = pItem->pRsp;
	if(pRsp==NULL)
		return 0;
	if(pReq->strMethod[0] == 'H') ///HEAD method do not send body
		nIsReqHEADCmd = 1;
	hQueue = pRsp->hQueue;
	////get some file data
	if(nIsReqHEADCmd == 0 && pItem->hFile && JS_SimpleQ_CheckAllRcvd(hQueue)==0 && JS_SimpleQ_GetDataSize(hQueue)<JS_CONFIG_MAX_QUEUESIZE) {
		char strTemp[JS_CONFIG_MAXBUFFSIZE];
		nIsFileEnd = 0;
		nReadSize = JS_UTIL_FileReadSome(pItem->hFile,strTemp,JS_CONFIG_MAXREADSIZE);
		if(nReadSize<0) {
			nRet = -1;
			DBGPRINT("send some: file read error\n");
			goto LABEL_EXIT_SENDSOME;
		}else if(nReadSize<JS_CONFIG_MAXREADSIZE) {
			DBGPRINT("TMP: send some: file read size=%u\n",nReadSize);
		}
		////push some data from file into queue
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		nRet = JS_SimpleQ_PushPumpIn(hQueue,strTemp, nReadSize);
		nIsFileEnd = JS_SimpleQ_CheckAllRcvd(hQueue);
		if(nIsFileEnd) {
			DBGPRINT("TMP: send some: all data read %s\n",pReq->pURL);
			pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
		}
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		if(nRet<0) {
			nRet = -1;
			DBGPRINT("send some: file to queue error\n");
			goto LABEL_EXIT_SENDSOME;			
		}
	}
	////send data
	while(pRsp) {
		nPumpOutResult = 0;
		nSent = 0;
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		pData = JS_SimpleQ_PreparePumpOut(hQueue, 0, &nBuffSize, NULL, 0, NULL);
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		if(pData) {
			if(nBuffSize>JS_CONFIG_MAXSENDSIZE)
				nBuffSize = JS_CONFIG_MAXSENDSIZE;
			nSent = JS_UTIL_TCP_SendTimeout(pItem->nInSock,pData,nBuffSize,10);
			if(nSent>0) {
				JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
				nPumpOutResult = JS_SimpleQ_FinishPumpOut(hQueue, nSent);
				JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
				//DBGPRINT("TMP: send q size=%u\n",nSent);
			}else if(nSent==0) {
				//DBGPRINT("TMP: no sending q\n");
				break;
			}else {
				JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
				JS_SimpleQ_FinishPumpOut(hQueue, nBuffSize);
				JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
				DBGPRINT("SendSome: error %d\n",errno);
				nRet = -1;
				break;
			}
		}else
			break;
		if(nPumpOutResult<0) {
			DBGPRINT("send some: pump out error\n");
			break;
		}
		nSendingLoopCnt++;
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		if(nIsReqHEADCmd && pReq->nQueueStatus != JS_REQSTATUS_NEEDTOCLOSE) {
			pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
		}
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		if(nIsReqHEADCmd)
			break;
	}
	////check whether queue status is end of action
	JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
	if(JS_SimpleQ_GetDataSize(hQueue)<=0 && (pReq->nQueueStatus == JS_REQSTATUS_ENDOFACTION || pReq->nQueueStatus == JS_REQSTATUS_NEEDTOCLOSE)) {
		JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
		JS_HttpServer_ResetSession(pItem,pReq);
		JS_UTIL_LockMutex(pItem->pPoolItem->hMutex);
		JS_EventLoop_SetOutputFd(pIO,pItem->nInSock,0,1);
		if(pRsp->hQueue)
			JS_SimpleQ_Reset(pRsp->hQueue);
		if(pReq->nQueueStatus == JS_REQSTATUS_NEEDTOCLOSE) {
			nRet = -1;
		}
	}
	JS_UTIL_UnlockMutex(pItem->pPoolItem->hMutex);
LABEL_EXIT_SENDSOME:
	return nRet;
}

static int JS_HttpServer_CheckDuplicateResource(JS_HttpServerGlobal * pHttpServer, const char * strResourceName) 
{
	JS_HttpServer_DirectAPIItem * pResource;
	pResource = (JS_HttpServer_DirectAPIItem *)JS_HashMap_Find(pHttpServer->hDirectAPIMap,(void*)strResourceName,JS_HttpServer_DirectAPIItem_FindCallback);
	if(pResource) {
		return 1;
	}else {
		return 0;
	}
}

static int JS_HttpServer_DestroyAPIMap(JS_HttpServerGlobal * pHttpServer)
{
	JS_HANDLE	hItemPos;
	if(pHttpServer->hDirectAPIMap) {
		JS_HttpServer_DirectAPIItem * pAPIItem;
		hItemPos = NULL;
		while(1) {
			hItemPos = JS_HashMap_GetNext(pHttpServer->hDirectAPIMap,hItemPos);
			if(hItemPos==NULL)
				break;
			pAPIItem = (JS_HttpServer_DirectAPIItem *)JS_HashMap_GetDataFromIterateItem(pHttpServer->hDirectAPIMap,hItemPos);
			if(pAPIItem) {
				if(pAPIItem->pResourceName)
					JS_FREE(pAPIItem->pResourceName);
			}
		}
		JS_HashMap_ClearIterationHandler(pHttpServer->hDirectAPIMap,hItemPos);
		JS_HashMap_Destroy(pHttpServer->hDirectAPIMap);
	}
	return 0;
}

static int JS_HttpServer_RegisterAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfDirect, int nIsAsync)
{
	int nRet = 0;
	JS_HttpServer_DirectAPIItem	* pResource;
	JS_HttpServerGlobal * pHttpServer = (JS_HttpServerGlobal *)JS_GetHttpServerHandle(hJose);

	if(JS_EventLoop_IsBusy(JS_GetServerLoopHandle(pHttpServer->hJose))) {
		DBGPRINT("Registering Resource must be done before service starts\n");
		return -1;
	}
	if(JS_HttpServer_CheckDuplicateResource(pHttpServer, strResourceName)) {
		DBGPRINT("JS_HttpServer_RegisterDirectAPI:duplicated resource item %s\n",strResourceName);
		return -1;
	}
	pResource = (JS_HttpServer_DirectAPIItem * )JS_ALLOC(sizeof(JS_HttpServer_DirectAPIItem));
	if(pResource==NULL) {
		DBGPRINT("JS_HttpServer_RegisterDirectAPI:can' register (mem error) %s\n",strResourceName);
		return -1;
	}
	nRet = 0;
	if(nIsAsync)
		pResource->nType = JS_CGITYPE_ASYNCAPI;
	else
		pResource->nType = JS_CGITYPE_DIRECTAPI;
	pResource->pResourceName = JS_UTIL_StrDup(strResourceName);
	if(pResource->pResourceName==NULL) {
		DBGPRINT("Can't  alloc res name mem error\n");
		JS_FREE(pResource);
		return -1;
	}
	pResource->pfDirectAPI = pfDirect;
	JS_UTIL_LockMutex(pHttpServer->hGlobalMutex);
	nRet = JS_HashMap_Add(pHttpServer->hDirectAPIMap,pResource);
	JS_UTIL_UnlockMutex(pHttpServer->hGlobalMutex);
	if(nRet<0) {
		DBGPRINT("Can't  add to directapi map\n");
		JS_FREE(pResource->pResourceName);
		JS_FREE(pResource);
	}
	return nRet;
}

static int JS_HttpServer_DirectAPIItem_HashCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	JS_HttpServer_DirectAPIItem * pItem =  (JS_HttpServer_DirectAPIItem *)pData;
    void * pKey = NULL;
	if(pItem != NULL) {
		pKey = pItem->pResourceName;
	}else if(pParamKey)
		pKey = pParamKey;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int JS_HttpServer_DirectAPIItem_FindCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	char * strCompRes;
	JS_HttpServer_DirectAPIItem * pItem =  (JS_HttpServer_DirectAPIItem *)pData;

	strCompRes = (char*)pParamKey;
	if(JS_UTIL_StrCmp(pItem->pResourceName,strCompRes,0,0,1)==0)
		nRet = 1;
	return nRet;
}

static int JS_HttpServer_MimeItem_HashCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	JS_HttpServer_MimeType * pMimeItem=  (JS_HttpServer_MimeType *)pData;
    void * pKey = NULL;
	if(pMimeItem != NULL) {
		pKey = pMimeItem->pExtension;
	}else if(pParamKey)
		pKey = pParamKey;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int JS_HttpServer_MimeItem_FindCallback (void * pOwner, void * pData, void * pParamKey)
{
	int nRet = 0;
	char * strCompRes;
	JS_HttpServer_MimeType * pItem =  (JS_HttpServer_MimeType *)pData;

	strCompRes = (char*)pParamKey;
	if(JS_UTIL_StrCmpRestrict(pItem->pExtension,strCompRes,0,0,1)==0)
		nRet = 1;
	return nRet;
}

static int JS_HttpServer_DestroyMimeMap(JS_HttpServerGlobal * pHttpServer)
{
	JS_HANDLE	hItemPos;
	if(pHttpServer->hMimeMap) {
		JS_HttpServer_MimeType * pMimeItem;
		hItemPos = NULL;
		while(1) {
			hItemPos = JS_HashMap_GetNext(pHttpServer->hMimeMap,hItemPos);
			if(hItemPos==NULL)
				break;
			pMimeItem = (JS_HttpServer_MimeType *)JS_HashMap_GetDataFromIterateItem(pHttpServer->hMimeMap,hItemPos);
			if(pMimeItem) {
				if(pMimeItem->pExtension)
					JS_FREE(pMimeItem->pExtension);
				if(pMimeItem->pMimeType)
					JS_FREE(pMimeItem->pMimeType);
			}
		}
		JS_HashMap_ClearIterationHandler(pHttpServer->hMimeMap,hItemPos);
		JS_HashMap_Destroy(pHttpServer->hMimeMap);
	}
	return 0;
}

static int JS_HttpServer_MimeLoadDefault(JS_HttpServerGlobal * pHttpServer)
{
	int nRet = 0;
	int nLen;
	int nTokenIndex;
	int nNextLine;
	int nLineLen;
	int nCnt;
	char strTmpLine[512];
	char strTmpToken[256];
	char strTmpMimeType[256];
	const char * pDefaultMime = JS_HTTPSERVER_DEFAULT_MIME;
	JS_HttpServer_MimeType * pTmpMime;
		
	nLen = strlen(pDefaultMime);
	nNextLine = 0;
	strTmpMimeType[0] = 0;
	while(1) { ///line iteration
		nNextLine = JS_UTIL_StrToken(pDefaultMime, nLen, nNextLine, "\r\n", strTmpLine, sizeof(strTmpLine));
		if(nNextLine<0 || strTmpLine[0]==0)
			break;
		nLineLen = strlen(strTmpLine);
		nTokenIndex = 0;
		nCnt = 0;
		while(1) {
			nTokenIndex = JS_UTIL_StrToken(strTmpLine, nLineLen, nTokenIndex, " ", strTmpToken, sizeof(strTmpToken));
			if(nTokenIndex<0 || strlen(strTmpToken)<=0)
				break;
			if(nCnt==0) { ////this is mime type
				JS_UTIL_StrCopySafe(strTmpMimeType,sizeof(strTmpMimeType),strTmpToken,0);
			}else {
				pTmpMime = (JS_HttpServer_MimeType*)JS_ALLOC(sizeof(JS_HttpServer_MimeType));
				if(pTmpMime==NULL) {
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				memset((char*)pTmpMime,0,sizeof(JS_HttpServer_MimeType));
				pTmpMime->pExtension = JS_UTIL_StrDup(strTmpToken);
				if(pTmpMime->pExtension==NULL) {
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				pTmpMime->pMimeType = JS_UTIL_StrDup(strTmpMimeType);
				if(pTmpMime->pMimeType==NULL) {
					nRet = -1;
					goto LABEL_CATCH_ERROR;
				}
				pTmpMime->nExtLen = strlen(pTmpMime->pExtension);
				nRet = JS_HashMap_Add(pHttpServer->hMimeMap,pTmpMime);
				if(nRet<0)
					goto LABEL_CATCH_ERROR;
				pTmpMime = NULL;
			}
			nCnt ++;
		}
	}
LABEL_CATCH_ERROR:
	if(pTmpMime) {
		if(pTmpMime->pExtension)
			JS_FREE(pTmpMime->pExtension);
		if(pTmpMime->pMimeType)
			JS_FREE(pTmpMime->pMimeType);
	}
	return nRet;
}

static const char * JS_HttpServer_GetMimeType(JS_HttpServerGlobal * pHttpServer, const char * strPath, JS_HttpServer_SessionItem * pItem)
{
	int nLen;
	int nCnt;
	int nExtStart;
	JS_HttpServer_MimeType * pTmpMime;
	static char strUnknown[] = "application/octet-stream";
	nLen = strlen(strPath);
	for(nCnt=nLen-1; nCnt>0; nCnt--) {
		if(strPath[nCnt]=='.')
			break;
	}
	nExtStart = nCnt+1;
	pTmpMime = (JS_HttpServer_MimeType *)JS_HashMap_Find(pHttpServer->hMimeMap,(void*)(strPath+nExtStart),JS_HttpServer_MimeItem_FindCallback);
	if(pTmpMime==NULL)
		return (const char *)strUnknown;
	else
		return (const char *)pTmpMime->pMimeType;
}

static void * JS_DirectAPI_WorkQFunc (void * pParam)
{
	JS_HttpServer_SessionItem * pSessionItem = (JS_HttpServer_SessionItem *)pParam;
	if(pSessionItem==NULL || pSessionItem->pTmpRes==NULL)
		return NULL;
	pSessionItem->pTmpRes->pfDirectAPI(pSessionItem);
	return NULL;
}

static int JS_HttpServer_DirectAPIWorkQEventFunc (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff)
{
	if(nEvent != JS_WORKQ_EVENT_TOSTRING)
		return JS_HttpServer_AsyncWorkCompleted(pParam);
	return 0;
}

static int JS_HttpServer_ClearSessionRef(JS_HttpServer_SessionItem * pSession)
{
	int nRet = 0;
	if(pSession && pSession->pPoolItem) {
		JS_EventLoop_DelThread(pSession->pPoolItem);
		JS_UTIL_LockMutex(pSession->pPoolItem->hMutex);
		if(pSession->pReq)
			pSession->pReq->nQueueStatus = JS_REQSTATUS_ENDOFACTION;
		JS_UTIL_UnlockMutex(pSession->pPoolItem->hMutex);
	}
	return nRet;
}

static void JS_HttpServer_CheckMyNetworkInfo(JS_HttpServerGlobal * pHttpServer)
{
	int nCnt=0;
	struct hostent * pHostinfo;
	char * strIpAddr;
	struct in_addr rcAddr;
	pHttpServer->arrHostIP[0] = (UINT32)inet_addr("127.0.0.1");
	if(gethostname(pHttpServer->strHostName, sizeof(pHttpServer->strHostName)) == 0) {
        if( (pHostinfo = gethostbyname(pHttpServer->strHostName)) != NULL )  {
			DBGPRINT("TMP: MyDomain=%s\n",pHttpServer->strHostName);
            for(nCnt=0; pHostinfo->h_addr_list[nCnt] != 0 ; nCnt++) {
				rcAddr =  *(struct in_addr*)pHostinfo->h_addr_list[nCnt];
				pHttpServer->arrHostIP[nCnt+1] = (UINT32)rcAddr.s_addr;
				rcAddr.s_addr = pHttpServer->arrHostIP[nCnt+1];
				strIpAddr = inet_ntoa(rcAddr);
				DBGPRINT("TMP: MyIP(dev=%d)=%s\n",nCnt+1,strIpAddr);
				if(nCnt+1>=MAX_NETWORK_DEVICES)
					break;
			}
		}
	}
	pHttpServer->nNetworkDeviceNum = nCnt+1;
}
