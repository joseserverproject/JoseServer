#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

#include <winsock.h>
#include <iostream>
#include <list>
#include <queue>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <string.h>
using namespace std;
#include "JS_Config.h"
#include "JS_Interface.h"
#include "JS_DS_Test.h"
#include "JS_Net_Test.h"

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
		g_oneInstance.hService = JS_CreateJose(JS_SERVICE_HTTP|JS_SERVICE_PROXY, JS_CONFIG_PORT_FRONTGATE, 0);
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
	char strFile[256];
	_CrtMemState s1, s2, s3;
	_CrtMemCheckpoint( &s1 );
	JS_Control_Create();
	DSTest_Init();
	printf("server started...if you want to quit type exit\n");
	while(1) {
		scanf_s("%s",strBuff,256);
		if(strcmp(strBuff,"exit")==0 || strcmp(strBuff,"quit")==0)
			break;
		else if(strcmp(strBuff,"li")==0) {
			int nRun, nThreads;
			printf("test run size?");
			scanf_s("%s",strBuff,256);
			nRun = atoi(strBuff);
			printf("thread num?");
			scanf_s("%s",strBuff,256);
			nThreads = atoi(strBuff);
			DSTest_AutoRun_List(nThreads,nRun);
		}
		else if(strcmp(strBuff,"map")==0) {
			int nRun, nThreads;
			printf("test run size?");
			scanf_s("%s",strBuff,256);
			nRun = atoi(strBuff);
			printf("thread num?");
			scanf_s("%s",strBuff,256);
			nThreads = atoi(strBuff);
			DSTest_AutoRun_Map(nThreads,nRun);
		}else if(strcmp(strBuff,"pool")==0) {
			int nRun, nThreads;
			printf("test run size?");
			scanf_s("%s",strBuff,256);
			nRun = atoi(strBuff);
			printf("thread num?");
			scanf_s("%s",strBuff,256);
			nThreads = atoi(strBuff);
			DSTest_AutoRun_Pool(nThreads,nRun);
		}else if(strcmp(strBuff,"ajax")==0) {
			if(NetTest_CheckPreviousRequest()>=0) {
				printf("URL? :");
				scanf_s("%s",strBuff,256);
				NetTest_RunAjaxTest(strBuff);
			}
		}else if(strcmp(strBuff,"filedn")==0) {
			if(NetTest_CheckPreviousRequest()>=0) {
				printf("URL? :");
				scanf_s("%s",strBuff,256);
				printf("LocalName? :");
				scanf_s("%s",strFile,256);
				NetTest_RunFileDownloadTest(strBuff,strFile);
			}
		}
	}
	DSTest_Clear();
	JS_Control_Destroy();
	_CrtMemCheckpoint( &s2 );
	if ( _CrtMemDifference( &s3, &s1, &s2) )
	   _CrtMemDumpStatistics( &s3 );
	return 0;
}

