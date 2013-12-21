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
ajax helper addon

1. provide long polling framework
	-cgi can call javascript function in browser
2. javascript to javascript rpc
	-browser1 can call browser2's event
3. provide cross domain ajax call
	-broswe belong host1 can call host2's cgi
**********************************************************************/

#include "JS_Config.h"

#ifdef JS_CONFIG_USE_ADDON_AJAXHELPER
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////////
//macro start
#define MAX_FUNC_ID		256
#define MAX_FUNC_NAME	128
#define MAX_VIEW_ITEM	32

//////////////////////////////////////////////////////
//local types
typedef struct JS_CrossDomainCallEntryTag {
	char  * pWebViewName;
	char  * pEventName;
	JS_HANDLE hHttpAutoClient;
}JS_CrossDomainCallEntry;

typedef struct JS_EventCallEntryTag {
	char  * pWebViewName;
	char  * pParamBuffer;
	JS_HANDLE hBroadcastList;
	UINT64	nKeepAlive;
}JS_EventCallEntry;

typedef struct JS_WebViewEntryTag {
	char  * pName;
	UINT64	nKeepAlive;
}JS_WebViewEntry;

typedef struct JS_AjaxHelper_GlobalTag {
	JS_HANDLE	hEventCallList;
	JS_HANDLE	hWebViewMap;
	JS_HANDLE	hMutexWebViewMap;
	JS_HANDLE	hMutexForEventList;
	int			nNeedExit;
	JS_HANDLE	hJose;
}JS_AjaxHelper_Global;

//////////////////////////////////////////////////////
//local variables
static JS_AjaxHelper_Global * g_pGlobal = NULL;

//////////////////////////////////////////////////////
//extern function
extern JS_HANDLE JS_GetAjaxHelperHandle(JS_HANDLE hJose);

//////////////////////////////////////////////////////
//function declarations
static int JS_AjaxHelper_DIRECTAPI_Command (JS_HANDLE hSession);
static int JS_AjaxHelper_RmWebViewItem (void * pOwner, void * pData);
static int JS_AjaxHelper_RmEventCallItem (void * pOwner, void * pData);
static int JS_AjaxHelper_HashWebViewItem (void * pOwner, void * pDataItem, void * pKey);
static int JS_AjaxHelper_FindWebViewItem (void * pOwner, void * pDataItem, void * pKey);
static int JS_AjaxHelper_FindBroadcastItem (void * pOwner, void * pDataItem, void * pKey);

//////////////////////////////////////////////////////
//function implementations
JS_HANDLE JS_AjaxHelper_Create(JS_HANDLE hJose)
{
	int nRet = 0;
	JS_AjaxHelper_Global * pGlobal;

	if(g_pGlobal != NULL) {
		DBGPRINT("ajaxhelper: can't make ajaxhelper cause it is one instance member\n");
		return NULL;
	}
	pGlobal = (JS_AjaxHelper_Global*)JS_ALLOC(sizeof(JS_AjaxHelper_Global));
	if(pGlobal==NULL) {
		nRet = -1;
		DBGPRINT("ajaxhelper: mem error(global)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pGlobal,0,sizeof(JS_AjaxHelper_Global));
	pGlobal->hMutexForEventList = JS_UTIL_CreateMutex();
	pGlobal->hMutexWebViewMap = JS_UTIL_CreateMutex();
	if(pGlobal->hMutexForEventList == NULL || pGlobal->hMutexWebViewMap == NULL) {
		nRet = -1;
		DBGPRINT("ajaxhelper: mem error(mutex)\n");
		goto LABEL_CATCH_ERROR;
	}
	pGlobal->hEventCallList = JS_List_Create(pGlobal, JS_AjaxHelper_RmEventCallItem);
	pGlobal->hWebViewMap = JS_HashMap_Create(pGlobal, JS_AjaxHelper_RmWebViewItem, JS_AjaxHelper_HashWebViewItem, 64, 1);
	if(pGlobal->hEventCallList==NULL || pGlobal->hWebViewMap) {
		nRet = -1;
		DBGPRINT("ajaxhelper: mem error(map)\n");
		goto LABEL_CATCH_ERROR;
	}
	nRet = JS_HttpServer_RegisterDirectAPI(hJose,"ajaxhelper",JS_AjaxHelper_DIRECTAPI_Command);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_AjaxHelper_Destroy(pGlobal);
		pGlobal = NULL;
		g_pGlobal = NULL;
	}else
		g_pGlobal = pGlobal;
	return pGlobal;
}

int JS_AjaxHelper_Destroy(JS_HANDLE hAjaxHelper)
{
	JS_AjaxHelper_Global * pGlobal = (JS_AjaxHelper_Global *)hAjaxHelper;
	if(pGlobal) {
		JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
		if(pGlobal->hEventCallList) {
			JS_List_Destroy(pGlobal->hEventCallList);
		}
		JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);

		JS_UTIL_LockMutex(pGlobal->hMutexWebViewMap);
		if(pGlobal->hWebViewMap) {
			JS_HashMap_Destroy(pGlobal->hWebViewMap);
		}
		JS_UTIL_UnlockMutex(pGlobal->hMutexWebViewMap);	
		if(pGlobal->hMutexForEventList) {
			JS_UTIL_DestroyMutex(pGlobal->hMutexForEventList);
		}
		if(pGlobal->hMutexWebViewMap) {
			JS_UTIL_DestroyMutex(pGlobal->hMutexWebViewMap);
		}
		JS_FREE(pGlobal);
		g_pGlobal = NULL;
	}
	return 0;
}

int JS_AjaxHelper_CallJavascriptEvent(const char * strViewName, const char * strFunctionName, const char * strArg)
{
	int nRet=0;
	JS_EventCallEntry * pItem;
	char * pField = NULL;
	int nSize;
	int nBuffLen;
	int nOffset;
	int nArgSize;
	JS_AjaxHelper_Global * pGlobal;
	pGlobal = (JS_AjaxHelper_Global *)g_pGlobal;
	if(pGlobal==NULL)
		return -1;
	pItem = (JS_EventCallEntry*)JS_ALLOC(sizeof(JS_EventCallEntry));
	if(pItem==NULL) {
		nRet = -1;
		DBGPRINT("native2javascript mem error(item)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pItem,0,sizeof(JS_EventCallEntry));
	if(strViewName) {
		pItem->pWebViewName = JS_UTIL_StrDup(strViewName);
		if(pItem->pWebViewName==NULL) {
			nRet = -1;
			DBGPRINT("native2javascript mem error(viewname)\n");
			goto LABEL_CATCH_ERROR;
		}
	}else {
		pItem->hBroadcastList = JS_List_Create(pGlobal, NULL);
		if(pItem->hBroadcastList==NULL) {
			nRet = -1;
			DBGPRINT("native2javascript mem error(broadcastlist)\n");
			goto LABEL_CATCH_ERROR;
		}
	}
	nArgSize = strlen(strArg);
	nSize = nArgSize + strlen(strFunctionName) + 256;
	pField = JS_UTIL_StrJsonBuildStructStart(nSize,&nOffset);
	if(pField==NULL) {
		nRet = -1;
		DBGPRINT("native2javascript mem error(jsonfield)\n");
		goto LABEL_CATCH_ERROR;
	}
	pField = JS_UTIL_StrJsonBuildStructField(pField,&nBuffLen,&nOffset,"name", strFunctionName);
	pField = JS_UTIL_StrJsonBuildStructField(pField,&nBuffLen,&nOffset, "arg", strArg);
	JS_UTIL_StrJsonBuildStructEnd(pField,&nBuffLen,&nOffset);
	pItem->pParamBuffer = pField;
	pItem->nKeepAlive = JS_UTIL_GetTickCount();
	////add to queue
	JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
	nRet = JS_List_PushBack(pGlobal->hEventCallList,pItem);
	JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_AjaxHelper_RmEventCallItem(NULL,pItem);
	}
	return nRet;
}

void JS_AjaxHelper_CrossDomainCallBack (void * pOwner, const char * strReturn)
{
	JS_CrossDomainCallEntry * pCallEntry = (JS_CrossDomainCallEntry *)pOwner;
	if(pCallEntry) {
		if(pCallEntry->pEventName)
			JS_AjaxHelper_CallJavascriptEvent(pCallEntry->pWebViewName,pCallEntry->pEventName,strReturn);
		if(pCallEntry->pEventName)
			JS_FREE(pCallEntry->pEventName);
		if(pCallEntry->pWebViewName)
			JS_FREE(pCallEntry->pWebViewName);
		JS_FREE(pCallEntry);
	}
}

static  int  JS_AjaxHelper_DIRECTAPI_Command (JS_HANDLE hSession)
{
	int nRet = 0;
	char strBuff[JS_CONFIG_MAXBUFFSIZE];
	char strViewName[300];
	int nBuffLen;
	int nOffset;
	int nViewNameLen;
	JS_HANDLE hItemPos = NULL;
	JS_AjaxHelper_Global * pGlobal = NULL;
	JS_EventCallEntry * pEvent = NULL;
	JS_WebViewEntry   * pViewItem = NULL;
	char * pJsonBuffer = NULL;
	char * pWebViewName = NULL;
	int nNeedToRemove;
	UINT64 nCurTick;

	pGlobal = (JS_AjaxHelper_Global *)g_pGlobal;
	if(pGlobal==NULL) {
		JS_HttpServer_SendQuickErrorRsp(hSession,500,"not ready");
		return -1;
	}
	nCurTick = JS_UTIL_GetTickCount();
	JS_HttpServer_GetVariableFromReq(hSession,"viewname",strViewName,256);
	JS_HttpServer_GetVariableFromReq(hSession,"cmd",strBuff,256);
	if(strViewName[0]==0 || strBuff[0]==0) {
		JS_HttpServer_SendQuickErrorRsp(hSession,400,"no param");
		return -1;
	}
	nBuffLen = strlen(strBuff);
	nViewNameLen = strlen(strViewName);
	////check view name. if there is no item, add this view name
	JS_UTIL_LockMutex(pGlobal->hMutexWebViewMap);
	pViewItem = (JS_WebViewEntry*)JS_HashMap_Find(pGlobal->hWebViewMap,strViewName,JS_AjaxHelper_FindWebViewItem);
	JS_UTIL_UnlockMutex(pGlobal->hMutexWebViewMap);
	if(pViewItem) {
		pViewItem->nKeepAlive = nCurTick;
	}else {
		pViewItem = (JS_WebViewEntry *)JS_ALLOC(sizeof(JS_WebViewEntry));
		if(pViewItem) {
			pViewItem->pName = JS_UTIL_StrDup(strViewName);
			if(pViewItem->pName != NULL) {
				pViewItem->nKeepAlive = nCurTick;
				JS_UTIL_LockMutex(pGlobal->hMutexWebViewMap);
				nRet = JS_HashMap_Add(pGlobal->hWebViewMap,pViewItem);
				JS_UTIL_UnlockMutex(pGlobal->hMutexWebViewMap);
			}else
				nRet = -1;
		}else
			nRet = -1;
	}
	if(nRet<0) {
		if(pViewItem)
			JS_AjaxHelper_RmWebViewItem(NULL,pViewItem);
		goto LABEL_CATCH_ERROR;
	}
	////remove old view item
	if(JS_HashMap_GetSize(pGlobal->hWebViewMap)>MAX_VIEW_ITEM) {
		hItemPos = NULL;
		while(1) {
			JS_UTIL_LockMutex(pGlobal->hMutexWebViewMap);
			hItemPos = JS_HashMap_GetNext(pGlobal->hWebViewMap,hItemPos);
			JS_UTIL_UnlockMutex(pGlobal->hMutexWebViewMap);
			if(hItemPos==NULL)
				break;
			JS_UTIL_LockMutex(pGlobal->hMutexWebViewMap);
			pViewItem = (JS_WebViewEntry*)JS_HashMap_GetDataFromIterateItem(pGlobal->hWebViewMap,hItemPos);
			if(pViewItem) {
				if(nCurTick>pViewItem->nKeepAlive+60000) {
					JS_HashMap_Remove(pGlobal->hWebViewMap,pViewItem);
				}						
			}
			JS_UTIL_UnlockMutex(pGlobal->hMutexWebViewMap);
		}
	}

	///////////////////////////////////////////////////////////////
	////process command
	if(JS_UTIL_StrCmpRestrict(strBuff,"event",nBuffLen,5,0)==0) {
		////return all javascript call requests
		////check size
		if(JS_List_GetSize(pGlobal->hEventCallList)<=0) {
			JS_HttpServer_SendQuickJsonRsp(hSession,"[0]");
			goto LABEL_CATCH_ERROR;
		}
		nBuffLen = 256;
		pJsonBuffer = JS_UTIL_StrJsonBuildArrayStart(nBuffLen,&nOffset);
		if(pJsonBuffer==NULL) {
			nRet = -1;
			DBGPRINT("ajaxhelper: direct command mem error(jsonbuff)\n");
			goto LABEL_CATCH_ERROR;
		}
		hItemPos = NULL;
		while(nRet>=0) {
			nNeedToRemove = 0;
			JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
			hItemPos = JS_List_GetNext(pGlobal->hEventCallList,hItemPos);
			JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
			if(hItemPos==NULL)
				break;
			JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
			pEvent = (JS_EventCallEntry *)JS_List_GetDataFromIterateItem(hItemPos);
			if(pEvent) {
				if(pEvent->pWebViewName) {
					if(JS_UTIL_StrCmpRestrict(strViewName,pEvent->pWebViewName,nViewNameLen,0,0)==0) {
						pJsonBuffer = JS_UTIL_StrJsonBuildArrayItem(pJsonBuffer,&nBuffLen,&nOffset,pEvent->pParamBuffer);
						if(pJsonBuffer==NULL) {
							DBGPRINT("ajaxhelper: direct command mem error(jsonbuff item)\n");
							nRet = -1;
						}
						nNeedToRemove = 1;
					}
				}else {
					if(JS_List_PopItemEx(pEvent->hBroadcastList,strViewName,JS_AjaxHelper_FindBroadcastItem,1)==NULL) {
						pJsonBuffer = JS_UTIL_StrJsonBuildArrayItem(pJsonBuffer,&nBuffLen,&nOffset,pEvent->pParamBuffer);
						if(pJsonBuffer==NULL) {
							DBGPRINT("ajaxhelper: direct command mem error(jsonbuff item)\n");
							nRet = -1;
						}
						if(nRet>=0) {
							pWebViewName = JS_UTIL_StrDup(strViewName);
							if(pWebViewName==NULL) {
								DBGPRINT("ajaxhelper: direct command mem error(viewname)\n");
								nRet = -1;
							}
						}
						if(nRet>=0) {
							nRet = JS_List_PushBack(pEvent->hBroadcastList,pWebViewName);
							if(nRet<0) {
								DBGPRINT("ajaxhelper: direct command mem error(pushq)\n");
							}
						}
						if(JS_List_GetSize(pEvent->hBroadcastList)>=JS_HashMap_GetSize(pGlobal->hWebViewMap))
							nNeedToRemove = 1;
					}
				}
				////check timeout
				if(nCurTick>pEvent->nKeepAlive+60000)
					nNeedToRemove = 1;
				if(nNeedToRemove) {
					JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
					JS_List_PopPosition(pGlobal->hEventCallList,hItemPos);
					JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
					JS_AjaxHelper_RmEventCallItem(NULL,pEvent);
					pEvent = NULL;
				}
			}
			JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
		}
		JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
		JS_List_ClearIterationHandler(hItemPos);
		JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
		if(nRet>=0) {
			JS_UTIL_StrJsonBuildArrayEnd(pJsonBuffer,&nBuffLen,&nOffset);
			JS_HttpServer_SendQuickJsonRsp(hSession,pJsonBuffer);
		}
	}else if(JS_UTIL_StrCmpRestrict(strBuff,"crossview",nBuffLen,9,0)==0) {
		char strTargetView[256];
		char strEvent[256];
		JS_HttpServer_GetVariableFromReqWithURLDecode(hSession,"targetview",strTargetView,256);
		JS_HttpServer_GetVariableFromReq(hSession,"targetevent",strEvent,256);
		JS_HttpServer_GetVariableFromReq(hSession,"param",strBuff,JS_CONFIG_MAXREADSIZE);
		if(strTargetView[0] == 0 || strEvent[0] == 0  || strBuff[0] == 0)
			nRet = -1;
		else {
			JS_AjaxHelper_CallJavascriptEvent(strTargetView,strEvent,strBuff);
			JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
		}
	}else if(JS_UTIL_StrCmpRestrict(strBuff,"crossdomain",nBuffLen,11,0)==0) {
		char strURL[JS_CONFIG_MAX_SMALLURL+32];
		char strEvent[256];
		JS_CrossDomainCallEntry * pCdax;
		JS_HttpServer_GetVariableFromReqWithURLDecode(hSession,"url",strURL,JS_CONFIG_MAX_SMALLURL);
		JS_HttpServer_GetVariableFromReq(hSession,"data",strBuff,JS_CONFIG_MAXREADSIZE);
		JS_HttpServer_GetVariableFromReq(hSession,"callback",strEvent,256);
		pCdax = (JS_CrossDomainCallEntry*)JS_ALLOC(sizeof(JS_CrossDomainCallEntry));
		if(pCdax) {
			if(strEvent[0] != 0)
				pCdax->pEventName = JS_UTIL_StrDup(strEvent);
			else
				pCdax->pEventName = NULL;
			pCdax->pWebViewName = JS_UTIL_StrDup(strViewName);
			if(pCdax->pEventName == NULL || pCdax->pWebViewName==NULL)
				nRet = -1;
			else {
				pCdax->hHttpAutoClient = JS_SimpeHttpClient_SendAjaxRequest(strURL, strBuff, 0, JS_AjaxHelper_CrossDomainCallBack, pCdax, 120000, NULL);
				if(pCdax->hHttpAutoClient==NULL)
					nRet = -1;
			}
		}else 
			nRet = -1;
		if(nRet<0) {
			if(pCdax) {
				if(pCdax->pEventName)
					JS_FREE(pCdax->pEventName);
				if(pCdax->pWebViewName)
					JS_FREE(pCdax->pWebViewName);
				JS_FREE(pCdax);
			}
		}else  {
			JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
		}		
	}
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pWebViewName)
			JS_FREE(pWebViewName);
		if(pEvent) {
			JS_UTIL_LockMutex(pGlobal->hMutexForEventList);
			JS_List_RemoveItem(pGlobal->hEventCallList,pEvent);
			JS_UTIL_UnlockMutex(pGlobal->hMutexForEventList);
		}
		JS_HttpServer_SendQuickErrorRsp(hSession,500,"error");
	}
	if(pJsonBuffer)
		JS_FREE(pJsonBuffer);
	return nRet;
}

static int JS_AjaxHelper_RmWebViewItem (void * pOwner, void * pData)
{
	JS_WebViewEntry * pItem = (JS_WebViewEntry *)pData;
	if(pItem) {
		if(pItem->pName)
			JS_FREE(pItem->pName);
		JS_FREE(pItem);
	}
	return 0;
}

static int JS_AjaxHelper_RmEventCallItem (void * pOwner, void * pData)
{
	JS_EventCallEntry * pItem;
	pItem = (JS_EventCallEntry *)pData;
	if(pItem) {
		if(pItem->pParamBuffer)
			JS_FREE(pItem->pParamBuffer);
		if(pItem->pWebViewName)
			JS_FREE(pItem->pWebViewName);
		if(pItem->hBroadcastList)
			JS_List_Destroy(pItem->hBroadcastList);
		JS_FREE(pItem);
	}
	return 0;
}

static int JS_AjaxHelper_HashWebViewItem (void * pOwner, void * pDataItem, void * pKey)
{
	int nRet = 0;
	JS_WebViewEntry * pItem;
	char * pString=NULL;
	if(pDataItem) {
		pItem = (JS_WebViewEntry *)pDataItem;
		pString = (char*)pItem->pName;
	}else {
		pString = (char*)pKey;
	}
	nRet = JS_HashMap_CalculateHashValue(pString,0,1);
	return nRet;
}

static int JS_AjaxHelper_FindWebViewItem (void * pOwner, void * pDataItem, void * pKey)
{
	int nRet = 0;
	JS_WebViewEntry * pItem;
	char * pDataString=NULL;
	char * pKeyString = NULL;
	pItem = (JS_WebViewEntry *)pDataItem;
	pDataString = (char*)pItem->pName;
	pKeyString = (char*)pKey;
	if(JS_UTIL_StrCmpRestrict(pDataString,pKeyString,0,0,0)==0)
		return 1;
	else
		return 0;
}

static int JS_AjaxHelper_FindBroadcastItem (void * pOwner, void * pDataItem, void * pKey)
{
	int nRet = 0;
	char * pDataString=NULL;
	char * pKeyString = NULL;
	pDataString = (char*)pDataItem;
	pKeyString = (char*)pKey;
	if(JS_UTIL_StrCmpRestrict(pDataString,pKeyString,0,0,0)==0)
		return 1;
	else
		return 0;
}

#endif