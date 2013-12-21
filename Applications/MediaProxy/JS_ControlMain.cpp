#include "stdafx.h"
#include "Wininet.h"
#include <winsock.h>
#include <iostream>
#include <list>
#include <queue>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <string.h>
#include <Shldisp.h>
#include "MediaProxy.h"
#include "JS_Config.h"
#include "JS_Interface.h"
#include "JS_ControlMain.h"

#define CONTROL_STATE_STARTED	0x1234567

class JS_ControlMain {
public:
		int nInit;
		int nProxyOn;
		int nBrowserLoadFinished;
		CRITICAL_SECTION  rcMutex;
		string stgMyDir;
		JS_HANDLE hJose;
};

static JS_ControlMain g_oneInstance;
static int JS_Control_DIRECTAPI_ProxyControl (JS_HANDLE hSession);
static int JS_Control_SetProxyToMe(int nIsEnable);
static BOOL _UnZip2Folder(BSTR lpZipFile , BSTR lpFolder);

void JS_DBGPRINT(const char* format, ... )
{
    va_list args;
	char strTemp[5000];
	va_start(args, format);
	vsnprintf(strTemp,4800,format, args );
	OutputDebugString((LPCSTR)strTemp);
	va_end(args);
}
 
static void cplus_split(const string & str, const string & delim, vector<string>& result)
{
    size_t start_pos = 0;
    size_t match_pos;
    size_t substr_length;
    while((match_pos = str.find(delim, start_pos)) != string::npos)
    {
        substr_length = match_pos - start_pos;
        if (substr_length > 0)
        {
            result.push_back(str.substr(start_pos, substr_length));
        }
        start_pos = match_pos + delim.length();
    }
    substr_length = str.length() - start_pos;
    if (substr_length > 0)
    {
        result.push_back(str.substr(start_pos, substr_length));
    }
}


int JS_Control_Destroy(void)
{
	int nRet = 0;
	if(g_oneInstance.nInit==CONTROL_STATE_STARTED) {
		if(g_oneInstance.hJose) {
			JS_StopJose(g_oneInstance.hJose);
			JS_DestroyJose(g_oneInstance.hJose);
		}
		g_oneInstance.hJose = NULL;
		JS_ClearGlobal();
		DeleteCriticalSection(&g_oneInstance.rcMutex);
		if(g_oneInstance.nProxyOn) {
			JS_Control_SetProxyToMe(0);
		}
	}
	g_oneInstance.nInit = 0;
	return 0;
}

int JS_Control_Create(void)
{
	int nRet = 0;
	TCHAR szMaxCurDir[2800];
	if(g_oneInstance.nInit!=CONTROL_STATE_STARTED) {
		if(JS_InitGlobal() < 0 ) {
			JS_ClearGlobal();
			return -1;
		}
		g_oneInstance.hJose = JS_CreateJose(JS_SERVICE_HTTP|JS_SERVICE_PROXY, JS_CONFIG_PORT_FRONTGATE, 0);
		if(g_oneInstance.hJose==NULL) {
			return -1;
		}
		InitializeCriticalSection(&g_oneInstance.rcMutex);
		GetCurrentDirectory(2000, szMaxCurDir);
		///check default directory
		g_oneInstance.stgMyDir = szMaxCurDir;
		
		strcat_s(szMaxCurDir,2000,"\\html");
		JS_HttpServer_RegisterDocumentRoot(g_oneInstance.hJose,szMaxCurDir);
		strcat_s(szMaxCurDir,2000,"\\upload");
		JS_HttpServer_RegisterUploadRoot(g_oneInstance.hJose,szMaxCurDir);

		JS_HttpServer_RegisterDirectAPI(g_oneInstance.hJose,"proxycmd",JS_Control_DIRECTAPI_ProxyControl);
		
		g_oneInstance.nInit = CONTROL_STATE_STARTED;
		nRet =  JS_StartJose(g_oneInstance.hJose);
	}
	return nRet;
}


int JS_ControlMain_CheckBrowserLoading(void)
{
	return g_oneInstance.nBrowserLoadFinished;
}

int JS_Control_Command(UINT message, WPARAM wParam, LPARAM lParam)
{
	int nRet = 0;

	return nRet;
}

static  int  JS_Control_DIRECTAPI_ProxyControl (JS_HANDLE hSession)
{
	char pBuffer[JS_CONFIG_MAX_SMALLPATH];
	int nFileOK = 0;
	int nRet = 0;
	int nCont = 0;
	int nOption = 0;
	pBuffer[0] = 0;
	JS_HttpServer_GetVariableFromReq(hSession,"cmd",pBuffer,256);
	if(strcmp(pBuffer,"startproxy")==0) {
		JS_Control_SetProxyToMe(1);
		JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
	}else if(strcmp(pBuffer,"stopproxy")==0) {
		JS_Control_SetProxyToMe(0);
		JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
	}else if(strcmp(pBuffer,"changemax")==0) {
		JS_HttpServer_GetVariableFromReq(hSession,"connection",pBuffer,256);
		nCont = atoi(pBuffer);
		if(nCont>0)
			JS_ChangeConfigOption(NULL,JS_CONFIG_MAX_TURBOCONNECTION,nCont);
		JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
	}else if(strcmp(pBuffer,"changeua")==0) {
		JS_HttpServer_GetVariableFromReq(hSession,"option",pBuffer,256);
		nOption = atoi(pBuffer);
		JS_ChangeConfigOption(NULL,JS_CONFIG_USE_PROXYAGENTASJOSE,nOption);
		JS_HttpServer_SendQuickJsonRsp(hSession,"{\"result\":\"ok\"}");
	}else
		JS_HttpServer_SendQuickErrorRsp(hSession,403,"not found");

	return 0;
}

bool JS_ControlMain_DirExist(const string& dirName_in)
{
  DWORD ftyp = GetFileAttributesA(dirName_in.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES)
    return false;  //something is wrong with your path!

  if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
    return true;   // this is a directory!

  return false;    // this is not a directory!
}

bool JS_ControlMain_FileExist(const string& fileName_in)
{
  DWORD ftyp = GetFileAttributesA(fileName_in.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES)
    return false;  //something is wrong with your path!

  if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
    return false;   // this is a directory!

  return true;    // this is not a directory!
}

static int JS_Control_SetProxyToMe(int nIsEnable)
{ 
	char buff[256] = "http=http://127.0.0.1:9861"; 
	INTERNET_PER_CONN_OPTION_LIST    List; 
	INTERNET_PER_CONN_OPTION         Option[3]; 
	unsigned long                    nSize = sizeof(INTERNET_PER_CONN_OPTION_LIST); 

	g_oneInstance.nProxyOn = nIsEnable;
	Option[0].dwOption = INTERNET_PER_CONN_PROXY_SERVER; 
	Option[0].Value.pszValue = buff; 

	Option[1].dwOption = INTERNET_PER_CONN_FLAGS; 
	if(nIsEnable) {
		Option[1].Value.dwValue = PROXY_TYPE_PROXY; 
		Option[1].Value.dwValue |= PROXY_TYPE_DIRECT; 
	}else {
		Option[1].Value.dwValue = PROXY_TYPE_DIRECT;
	}

	Option[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS; 
	Option[2].Value.pszValue = "<local>"; 

 

	List.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LIST); 
	List.pszConnection = NULL; 
	List.dwOptionCount = 3; 
	List.dwOptionError = 0; 
	List.pOptions = Option; 

	if(!InternetSetOption(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, nSize)) 
		printf("InternetSetOption failed! (%d)\n", GetLastError()); 

	InternetSetOption(NULL, INTERNET_OPTION_REFRESH, NULL,NULL); 
	return 0;
}



WCHAR * JS_CotrolMain_MultiByteToWide(const char * strSrc, int nIsUTF8)
{
	int iRet;
	int iDestBufferSize;
	LPWSTR  pwszStr;
	UINT nCode;

	if(nIsUTF8)
		nCode =CP_UTF8;
	else
		nCode =CP_ACP;
	iDestBufferSize = MultiByteToWideChar(nCode, 0, strSrc, -1, NULL, 0 );
	pwszStr =  (WCHAR*)malloc(sizeof(WCHAR)*(iDestBufferSize+1));
	if( NULL == pwszStr) {
		return NULL;
	} 
	iRet = MultiByteToWideChar(nCode, 0, strSrc, -1, pwszStr, iDestBufferSize );
	if( 0 < iRet ) {
		(pwszStr)[iRet - 1]= 0;
	} 
	iDestBufferSize = iRet;
	return pwszStr;
}

BOOL JS_ControlMain_Unzip2Folder(LPCTSTR strZipFile, LPCTSTR strFolder)
{
	WCHAR * lpWZipFile = NULL;
	WCHAR * lpWFolder = NULL;
	BSTR lpZipFile = NULL;
	BSTR lpFolder = NULL;
	BOOL nRet;

	lpWZipFile = JS_CotrolMain_MultiByteToWide(strZipFile,0);
	lpWFolder = JS_CotrolMain_MultiByteToWide(strFolder,0);
	lpZipFile = ::SysAllocString(lpWZipFile);
	lpFolder = ::SysAllocString(lpWFolder);
	nRet = _UnZip2Folder(lpZipFile,lpFolder);
	::SysFreeString(lpZipFile);
	::SysFreeString(lpFolder);
	free(lpWFolder);
	free(lpWZipFile);
	return nRet;
}


static BOOL _UnZip2Folder(BSTR lpZipFile , BSTR lpFolder)
{
	IShellDispatch *pISD;

	Folder  *pZippedFile = 0L;
	Folder  *pDestination = 0L;
	long FilesCount = 0;
	IDispatch* pItem = 0L;
	FolderItems *pFilesInside = 0L;


	VARIANT Options, OutFolder, InZipFile, Item;
	CoInitialize( NULL);

	__try{
		if (CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **)&pISD) != S_OK)
			return FALSE;
		InZipFile.vt = VT_BSTR;
		InZipFile.bstrVal = lpZipFile;
		pISD->NameSpace( InZipFile, &pZippedFile);
		if (!pZippedFile)
		{
			pISD->Release();
			return FALSE;
		}

		OutFolder.vt = VT_BSTR;
		OutFolder.bstrVal = lpFolder;
		pISD->NameSpace( OutFolder, &pDestination);
		if(!pDestination)
		{
			pZippedFile->Release();
			pISD->Release();
			return FALSE;
		}
    
		pZippedFile->Items(&pFilesInside);
		if(!pFilesInside)
		{
			pDestination->Release();
			pZippedFile->Release();
			pISD->Release();
			return FALSE;
		}
	    
		pFilesInside->get_Count( &FilesCount);
		if( FilesCount < 1)
		{
			pFilesInside->Release();
			pDestination->Release();
			pZippedFile->Release();
			pISD->Release();
			return FALSE;
		}

		pFilesInside->QueryInterface(IID_IDispatch,(void**)&pItem);

		Item.vt = VT_DISPATCH;
		Item.pdispVal = pItem;

		Options.vt = VT_I4;
		Options.lVal = 1024 | 512 | 16 | 4;

		bool retval = pDestination->CopyHere( Item, Options) == S_OK;

		pItem->Release();pItem = 0L;
		pFilesInside->Release();pFilesInside = 0L;
		pDestination->Release();pDestination = 0L;
		pZippedFile->Release();pZippedFile = 0L;
		pISD->Release();pISD = 0L;

		return TRUE;

	}__finally    
	{
		CoUninitialize();
	}
}
