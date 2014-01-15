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
Api entry points

1. life cycle: initglobal-> create -> start -> stop -> destroy -> clearglobal
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#include "JS_EventLoop.h"
#include "JS_HttpServer.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////
///local types
typedef struct JS_MainStructTag {
	unsigned short nPort;
	int nIsAutoPort;
	JS_HANDLE	hServerLoop;
	JS_HANDLE   hHttpServer;
	JS_HANDLE	hProxyServer;
	JS_HANDLE	hAjaxHelper;
}JS_MainStruct;

//////////////////////////////////////////////////
///functions

int JS_InitGlobal(void)
{
	JS_UTIL_Init();
	return 0;
}

int JS_ClearGlobal(void)
{
	JS_UTIL_Clear();
	return 0;
}

JS_HANDLE JS_GetServerLoopHandle(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	return pJose->hServerLoop;
}

JS_HANDLE JS_GetHttpServerHandle(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	return pJose->hHttpServer;
}

JS_HANDLE JS_GetProxyServerHandle(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	return pJose->hProxyServer;
}

JS_HANDLE JS_GetAjaxHelperHandle(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	return pJose->hAjaxHelper;
}

JS_HANDLE JS_CreateJose(int nServiceMask, unsigned short nPort, int nIsAutoPort)
{
	int nRet = 0;
	JS_MainStruct * pJose = NULL;
	pJose = (JS_MainStruct *)JS_ALLOC(sizeof(JS_MainStruct));
	if(pJose==NULL) {
		DBGPRINT("horse api: create JS_ALLOC error\n");
		return NULL;
	}
	memset((char*)pJose,0,sizeof(JS_MainStruct));
	////make server
	pJose->hServerLoop = JS_EventLoop_PrepareServerLoop((JS_HANDLE)pJose, JS_CONFIG_MAX_IOTHREAD);
	if(pJose->hServerLoop==NULL) {
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	pJose->nPort = nPort;
	pJose->nIsAutoPort = nIsAutoPort;
	if(JS_SERVICE_HTTP&nServiceMask) {
		pJose->hHttpServer = JS_HttpServer_Create((JS_HANDLE)pJose, nPort, nIsAutoPort);
		if(pJose->hHttpServer == NULL) {
			DBGPRINT("horse api: can't make http server\n");
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
		if(JS_EventLoop_RegisterHandler(pJose->hServerLoop,JS_HttpServer_GetEventHandler(),1)<0) {
			DBGPRINT("horse api: can't register http event handler\n");
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
	}
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
	if(JS_SERVICE_PROXY&nServiceMask) {
		pJose->hProxyServer = JS_MediaProxy_Create((JS_HANDLE)pJose);
		if(pJose->hProxyServer == NULL) {
			DBGPRINT("horse api: can't make proxy server\n");
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
		if(JS_EventLoop_RegisterHandler(pJose->hServerLoop,JS_MediaProxy_GetEventHandler(),0)<0) {
			DBGPRINT("horse api: can't register proxy event handler\n");
			nRet = -1;
			goto LABEL_CATCH_ERROR;
		}
	}
	JS_HttpServer_RegisterDirectAPI(pJose,"gateinfo",JS_MediaProxy_DIRECTAPI_Information);	
#endif
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_DestroyJose(pJose);
		pJose = NULL;
	}
	return (JS_HANDLE)pJose;
}

void JS_DestroyJose(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	if(pJose) {
		if(pJose->hHttpServer)
			JS_HttpServer_Destroy(pJose->hHttpServer);
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
		if(pJose->hProxyServer)
			JS_MediaProxy_Destroy(pJose->hProxyServer);
#endif
		if(pJose->hServerLoop)
			JS_EventLoop_DestroyServerLoop(pJose->hServerLoop);
		JS_FREE(pJose);
	}
}

int JS_StartJose(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	if(pJose && pJose->hServerLoop)
		return JS_EventLoop_StartServerLoop(pJose->hServerLoop,pJose->nPort,pJose->nIsAutoPort);
	return -1;
}

int JS_StopJose(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	if(pJose && pJose->hServerLoop)
		return JS_EventLoop_StopServerLoop(pJose->hServerLoop);
	return -1;
}

unsigned short JS_GetJosePort(JS_HANDLE hJose)
{
	JS_MainStruct * pJose = (JS_MainStruct *)hJose;
	if(pJose && pJose->hServerLoop)
		return JS_EventLoop_GetMyPort(pJose->hServerLoop);
	return 0;
}

int JS_SetJoseDebugOption(JS_HANDLE hJose, int nOption)
{
	////TBD
	return 0;
}

int JS_ChangeConfigOption(JS_HANDLE hJose, int nConfigID, UINT64 nNewValue)
{
	////TBD
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
	if(nConfigID==JS_CONFIG_MAX_TURBOCONNECTION) {
		JSUINT nMaxCon = (JSUINT)nNewValue;
		if(nMaxCon>=JS_CONFIG_MAX_TURBOCONNECTION)
			nMaxCon = JS_CONFIG_MAX_TURBOCONNECTION-1;
		DBGPRINT("turbogate: max connection is changed to %u\n",nMaxCon);
		JS_UTIL_GetConfig()->nMaxTurboConnection = nMaxCon;
	}else if(nConfigID==JS_CONFIG_USE_PROXYAGENTASJOSE) {
		int nOption = (int)nNewValue;
		if(nOption!=0)
			nOption = 1;
		JS_UTIL_GetConfig()->nUseJoseAgent = nOption;
	}
#endif
	return 0;
}

int JS_JoseCommand(JS_HANDLE hJose, const char * strCmd, char * strResult, int nBuffLen)
{
	if(JS_UTIL_StrCmp(strCmd,"dbgoff",0,0,1) == 0) {
		JS_UTIL_SetDbgLevel(0);
		JS_STRPRINTF(strResult,nBuffLen,"ok");
	}else if(JS_UTIL_StrCmp(strCmd,"dbgon",0,0,1) == 0) {
		JS_UTIL_SetDbgLevel(1);
		JS_STRPRINTF(strResult,nBuffLen,"ok");
	}
#if (JS_CONFIG_USE_ADDON==1)
#if defined(JS_CONFIG_USE_ADDON_SIMPLEDISCCOVERY)
	else if(JS_UTIL_StrCmp(strCmd,"getmyip",0,0,1)==0) {
		const char * pIP = JS_SimpleDiscovery_GetMyIP();
		if(pIP[0]==0) {
			JS_STRPRINTF(strResult,nBuffLen,"unknown");
		}else {
			JS_STRPRINTF(strResult,nBuffLen,"%s",pIP);
		}
	}else if(JS_UTIL_StrCmp(strCmd,"getmyname",0,0,1)==0) {
		const char * pName = JS_SimpleDiscovery_GetMyName();
		if(pName[0]==0) {
			JS_STRPRINTF(strResult,nBuffLen,"unknown");
		}else {
			JS_STRPRINTF(strResult,nBuffLen,"%s",pName);
		}
	}
#endif
#endif
	else 
		return -1;
	return 0;
}

int JS_HttpServer_GetVariableFromReqWithURLDecode(JS_HANDLE hSession, const char * strKey, char * pBuffer, int nBuffLen)
{
	int nSize;
	nSize = JS_HttpServer_GetVariableFromReq(hSession, strKey, pBuffer, nBuffLen);
	if(nSize>0) {
		char * pDest = (char*)JS_ALLOC(nSize+128);
		if(pDest) {
			JS_HttpServer_URLDecode(pBuffer, pDest, nSize);
			nSize=strlen(pDest);
			memcpy(pBuffer,pDest,nSize);
			pBuffer[nSize] = 0;
			JS_FREE(pDest);
		}else {
			pBuffer[0] = 0;
			nSize = 0;
		}
	}
	return nSize;
}

int JS_HttpServer_AsyncWorkCompleted(JS_HANDLE hSession)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_ASYNCDONE,0,NULL,0,NULL,0);
}

int JS_HttpServer_GetVariableFromReq(JS_HANDLE hSession, const char * strKey, char * pBuffer, int nBuffLen)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_GETVARIABLE,0,strKey,0,pBuffer,nBuffLen);
}

int JS_HttpServer_GetFileListUploaded(JS_HANDLE hSession, char * pBuffer, int nBuffLen)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_GETFILELIST,0,NULL,0,pBuffer,nBuffLen);
}

int JS_HttpServer_GetPeerIP(JS_HANDLE hSession, char * strIP, int nBuffLen)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_GETPEERIP,0,NULL,0,strIP,nBuffLen);
}

int JS_HttpServer_SendHeaderRaw(JS_HANDLE hSession, const char * strHeader, int nHeaderLen)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDHEADERRAW,0, strHeader,nHeaderLen,NULL,0);
}

int JS_HttpServer_SendBodyData(JS_HANDLE hSession, const char * strBodyData, int nBodyLen)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDBODYRAW,0, strBodyData,nBodyLen,NULL,0);
}

int JS_HttpServer_SendQuickXMLRsp(JS_HANDLE hSession,const char * strRsp)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDXMLRSP,0, strRsp,0,NULL,0);
}

int JS_HttpServer_SendQuickTextRsp(JS_HANDLE hSession,const char * strRsp)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDTEXTRSP,0, strRsp,0,NULL,0);

}

int JS_HttpServer_SendQuickJsonRsp(JS_HANDLE hSession,const char * strRsp)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDJSONRSP,0, strRsp,0,NULL,0);
}

int JS_HttpServer_SendQuickErrorRsp(JS_HANDLE hSession,int nErrorCode, const char * strErrorString)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDERRORRSP,nErrorCode, strErrorString,0,NULL,0);
}

int JS_HttpServer_SendFileWithHeader(JS_HANDLE hSession, const char * strPath)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SENDFILE,0, strPath,0,NULL,0);
}

int JS_HttpServer_SetMimeTypeAsDownloadable(JS_HANDLE hSession)
{
	return JS_HttpServer_DoAPICommand(hSession,JS_HTTPAPI_CMD_SETDOWNLOADABLE,0, NULL,0,NULL,0);
}