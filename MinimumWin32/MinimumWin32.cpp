#include <SDKDDKVer.h>
#include <stdio.h>
#include <tchar.h>
#include <winsock.h>
#include <string>
#include <stdio.h>
#include <string.h>
using namespace std;
#include "JS_Config.h"
#include "JS_Interface.h"

#pragma comment(lib, "Ws2_32.lib")

#define CONTROL_STATE_STARTED 0x99
class JS_ControlMain {
public:
	int nInit;
	CRITICAL_SECTION  rcMutex;
	string stgMyDir;
	JS_HANDLE hService;
};

static JS_ControlMain g_oneInstance;

void MYDBGPRINT(const char* format, ... )
{
    va_list args;
	char strTemp[5000];
	va_start(args, format);
	vsnprintf(strTemp,4800,format, args );
	OutputDebugString((LPCSTR)strTemp);
	va_end(args);
}

int JS_DirectAPI_UploadCommand (JS_HANDLE hSession)
{
	JS_HttpServer_SendQuickXMLRsp(hSession,"<result>ok</result>");
	return 0;
}

int JS_DirectAPI_RawCommand (JS_HANDLE hSession)
{
	JS_HttpServer_SendQuickXMLRsp(hSession,"<result>no item found</result>");
	return 0;
}

int JS_Control_Destroy(void)
{
	int nRet = 0;
	if(g_oneInstance.nInit==CONTROL_STATE_STARTED) {
		if(g_oneInstance.hService) {
			JS_StopJose(g_oneInstance.hService);
			JS_DestroyJose(g_oneInstance.hService);
		}
		g_oneInstance.hService = NULL;
		JS_ClearGlobal();
		DeleteCriticalSection(&g_oneInstance.rcMutex);
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
		g_oneInstance.hService = JS_CreateJose(JS_SERVICE_HTTP, 80, 0);
		if(g_oneInstance.hService==NULL) {
			return -1;
		}
		InitializeCriticalSection(&g_oneInstance.rcMutex);
		GetCurrentDirectory(2000, szMaxCurDir);
		///check default directory
		g_oneInstance.stgMyDir = szMaxCurDir;
		
		strcat_s(szMaxCurDir,2000,"\\html");
		JS_HttpServer_RegisterDocumentRoot(g_oneInstance.hService,szMaxCurDir);
		strcat_s(szMaxCurDir,2000,"\\upload");
		JS_HttpServer_RegisterUploadRoot(g_oneInstance.hService,szMaxCurDir);

		JS_HttpServer_RegisterDirectAPI(g_oneInstance.hService,"rawcmd",JS_DirectAPI_RawCommand);
		JS_HttpServer_RegisterDirectAPI(g_oneInstance.hService,"uploadtest",JS_DirectAPI_UploadCommand);
		
		g_oneInstance.nInit = CONTROL_STATE_STARTED;
		nRet =  JS_StartJose(g_oneInstance.hService);
	}
	return nRet;
}

int _tmain(int argc, _TCHAR* argv[])
{
	char strBuff[256];
	JS_Control_Create();
	printf("server started...if you want to quit type exit\n");
	while(1) {
		scanf_s("%s",strBuff,256);
		if(strcmp(strBuff,"exit")==0 || strcmp(strBuff,"quit")==0)
			break;
	}
	JS_Control_Destroy();
	return 0;
}

