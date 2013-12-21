#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_Net_Test.h"
#include <list>
#include <queue>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <string.h>
using namespace std;

static JS_HANDLE g_hHttpClient = NULL;
static char g_strDownPath[512];

static void NetTest_AjaxCallBack (void * pOwner, const char * strReturn)
{
	if(strReturn)
		printf("Net Test Result(ajax call):%s\n",strReturn);
	else
		printf("Net Test Result(ajax call): failure\n");
	g_hHttpClient = NULL;
}

static int NetTest_FileCallBack (void * pOwner, int nProgress, int nError, int nIsCompletd)
{
	if(nError)
		printf("Net Test Result(file download):failure error=%d\n",nError);
	else
		printf("Net Test Result(file download): Success path=%s\n",g_strDownPath);
	if(nError || nIsCompletd)
		g_hHttpClient = NULL;
	return 0;
}

int NetTest_CheckPreviousRequest(void)
{
	int nRet = 0;
	char strTemp[512];
	if(g_hHttpClient != NULL) {
		printf("Net Test: previous request is not finished\n");
		printf("Do you want to quit previous req?[Y/N] :");
		scanf_s("%s",strTemp,256);
		if(strTemp[0] == 'Y') {
			printf("Wait to quit the job...\n");
			JS_SimpleHttpClient_StopDownload(g_hHttpClient);
			while(g_hHttpClient) {
				printf(".");
				Sleep(1000);
			}
			printf("Request is stoped. Ready to do next req\n");
		}else {
			nRet = -1;
			printf("Wait a second for previous request\n");
		}
	}
	return nRet;
}

int NetTest_RunAjaxTest(const char * strURL) 
{
	g_hHttpClient = JS_SimpeHttpClient_SendAjaxRequest(strURL, NULL, 0, NetTest_AjaxCallBack, NULL, 0, NULL);
	return 0;
}

int NetTest_RunFileDownloadTest(const char * strURL, const char * strFileName) 
{
	sprintf_s(g_strDownPath, 256, "./html/%s", strFileName);
	g_hHttpClient = JS_SimpeHttpClient_DownloadFile(strURL, NULL, 0, g_strDownPath, NetTest_FileCallBack, NULL, 0, NULL);
	return 0;
}
