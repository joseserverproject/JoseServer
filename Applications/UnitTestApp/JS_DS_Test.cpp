#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include <list>
#include <queue>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <string.h>
using namespace std;

class JS_DataTest {
public:
	int nCmd;
	int nRunLength;
	int nThreadNum;
	int nErrorCnt;
	JS_HANDLE hList;
	JS_HANDLE hMap;
	JS_HANDLE hPool;
	list<string> objList;
	map<string,string> objMap;
	CRITICAL_SECTION  rcMutex;
};

extern void MYDBGPRINT(const char* format, ... );
static JS_DataTest g_rcTest;
static int DSTest_PrintList(int nThread, int nIsConsole);
static int DSTest_GetRand(void);
static int DSTest_CheckList();
static void DSTest_DoAutoListTest(void * pParam);

static void DSTest_CheckMap(void);
static void DSTest_PrintMap(int nThread, int nIsConsole, int nRmIndex, int nMax);
static int DSTest_HashCallback (void * pOwner, void * pData, void * pCompData);
static int DSTest_FindCallback (void * pOwner, void * pData, void * pCompData);
static void DSTest_DoAutoMapTest(void * pParam);

static int DSTest_PoolHashCallback (void * pOwner, void * pData, void * pCompData);
static int DSTest_PoolFindCallback (void * pOwner, void * pData, void * pCompData);
static void DSTest_CheckPool(void);
static void DSTest_PrintPool(int nThread, int nIsConsole, int nRmIndex, int nMax);
static void DSTest_DoAutoPoolTest(void * pParam);


static DWORD __stdcall _JS_DS_Thread_(void * pParam);

static int DSTest_GetRand(void) 
{
	srand(GetTickCount());
	return rand();
}

static int DSTest_CheckList()
{
	char * strPos;
	JS_HANDLE hItemPos = NULL;
	string stgTmp;
	int nError = 0;
	int nCnt;

	list<string>::iterator it;
	if(JS_List_GetSize(g_rcTest.hList) == g_rcTest.objList.size()) {
		printf("CheckList: size comparison ok\n");
	}else
		printf("CheckList: size comparison failure %d<-->%d\n",JS_List_GetSize(g_rcTest.hList),g_rcTest.objList.size());
	nCnt = 0;
	hItemPos = JS_List_GetNext(g_rcTest.hList,NULL);
    for (it=g_rcTest.objList.begin();it!=g_rcTest.objList.end();it++) {
		  if(hItemPos) {
			stgTmp = *it;
			strPos = (char*)JS_List_GetDataFromIterateItem(hItemPos);
			if(stgTmp.compare(strPos) != 0) {
				nError = 1;
				printf("%d: error different member %s<-->%s\n",strPos,stgTmp.c_str());
			}
		  }          
		  if(hItemPos)
			hItemPos = JS_List_GetNext(g_rcTest.hList,hItemPos);
		  nCnt++;
     }
	if(nError==0)
		printf("CheckList: chek OK! whose size=%d\n",g_rcTest.objList.size());
	else
		printf("CheckList: some error in list whose size=%d\n",g_rcTest.objList.size());
	DSTest_PrintList(0,1);
	return 0;
}

static int DSTest_PrintList(int nThread, int nIsConsole)
{
	char * strPos;
	JS_HANDLE hItemPos = NULL;

	EnterCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("list size=%d\n",JS_List_GetSize(g_rcTest.hList)); 
	else
		MYDBGPRINT("list size=%d\n",JS_List_GetSize(g_rcTest.hList)); 
	LeaveCriticalSection(&g_rcTest.rcMutex);
	while(1) {
		EnterCriticalSection(&g_rcTest.rcMutex);
		hItemPos = JS_List_GetNext(g_rcTest.hList,hItemPos);
		LeaveCriticalSection(&g_rcTest.rcMutex);
		if(hItemPos==NULL)
			break;
		EnterCriticalSection(&g_rcTest.rcMutex);
		strPos = (char*)JS_List_GetDataFromIterateItem(hItemPos);
		if(strPos && nIsConsole)
			printf("%s ",strPos);
		LeaveCriticalSection(&g_rcTest.rcMutex);
	}
	EnterCriticalSection(&g_rcTest.rcMutex);
	JS_List_ClearIterationHandler(hItemPos);
	LeaveCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("\n");
	return 0;
}

static void DSTest_DoAutoListTest(void * pParam)
{
	int nCnt = 0;
	int nRand;
	int nCmd;
	int nSleep;
	char * pData;
	char * pStr = (char*)pParam;
	for(nCnt=0; nCnt<g_rcTest.nRunLength; nCnt++) {
		nRand = DSTest_GetRand();
		nCmd = nRand%8;
		switch(nCmd) {
			case 0:
			case 1:
			case 2:
				pData = (char*)malloc(64);
				sprintf_s(pData,64,"%s_%d",pStr,nCnt);
				EnterCriticalSection(&g_rcTest.rcMutex);
				JS_List_PushBack(g_rcTest.hList,pData);
				g_rcTest.objList.push_back(string(pData));
				printf("%d: JS_List_PushBack %s\n",nCnt,pData);
				LeaveCriticalSection(&g_rcTest.rcMutex);				
				break;
			case 3:
			case 4:
			case 5:
				pData = (char*)malloc(64);
				sprintf_s(pData,64,"%s_%d",pStr,nCnt);
				EnterCriticalSection(&g_rcTest.rcMutex);
				JS_List_PushFront(g_rcTest.hList,pData);
				g_rcTest.objList.push_front(string(pData));
				printf("%d: JS_List_PushFront %s\n",nCnt,pData);
				LeaveCriticalSection(&g_rcTest.rcMutex);
				break;
			case 6:
				EnterCriticalSection(&g_rcTest.rcMutex);
				pData = (char*)JS_List_PopBack(g_rcTest.hList);
				if(g_rcTest.objList.size()>0)
					g_rcTest.objList.pop_back();
				LeaveCriticalSection(&g_rcTest.rcMutex);
				if(pData==NULL)
					printf("%d: JS_List_PopBack no data\n",nCnt);
				else { 
					printf("%d: JS_List_PopBack %s\n",nCnt,pData);
					free(pData);
				}
				break;
			case 7:
				EnterCriticalSection(&g_rcTest.rcMutex);
				pData = (char*)JS_List_PopFront(g_rcTest.hList);
				if(g_rcTest.objList.size()>0)
					g_rcTest.objList.pop_front();
				LeaveCriticalSection(&g_rcTest.rcMutex);
				if(pData==NULL)
					printf("%d: JS_List_PopFront no data\n",nCnt);
				else  {
					printf("%d: JS_List_PopFront %s\n",nCnt,pData);
					free(pData);
				}
				break;
		}
		DSTest_PrintList(0,0);
		nRand = DSTest_GetRand();
		nSleep = nRand%50;
		Sleep(nSleep);
	}
	if(pStr)
		free(pStr);
	EnterCriticalSection(&g_rcTest.rcMutex);
	g_rcTest.nThreadNum--;
	LeaveCriticalSection(&g_rcTest.rcMutex);
}

static DWORD __stdcall _JS_DS_Thread_(void * pParam)
{
	if(g_rcTest.nCmd==1)
		DSTest_DoAutoListTest(pParam);
	else if(g_rcTest.nCmd==2)
		DSTest_DoAutoMapTest(pParam);
	else if(g_rcTest.nCmd==3)
		DSTest_DoAutoPoolTest(pParam);
	ExitThread(0);
	return 0;
}

int DSTest_AutoRun_List(int nThreadNum, int nRunLength) 
{
	int nCnt;
	char * pStr;
	DWORD nThreadID;
	g_rcTest.hList = JS_List_Create(NULL,NULL);
	g_rcTest.nRunLength =  nRunLength;
	g_rcTest.nThreadNum = nThreadNum;
	g_rcTest.objList.clear();
	g_rcTest.nCmd = 1;
	for(nCnt=0; nCnt<nThreadNum; nCnt++) {
		pStr = (char*)malloc(64);
		sprintf_s(pStr,64,"TA%d",nCnt);
		CreateThread(NULL, 0, _JS_DS_Thread_, (void *)pStr,0,&nThreadID);
	}
	while(1) {
		if(g_rcTest.nThreadNum<=0)
			break;
		Sleep(50);
	}
	DSTest_CheckList();
	g_rcTest.objList.clear();
	JS_List_Destroy(g_rcTest.hList);
	return 0;
}

static int DSTest_HashCallback (void * pOwner, void * pData, void * pCompData)
{
	int nRet = 0;
	void * pKey = NULL;
	if(pData != NULL) {
		pKey = pData;
	}else
		pKey = pCompData;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int DSTest_FindCallback (void * pOwner, void * pData, void * pCompData)
{
	char * pStr1;
	char * pStr2;
	if(pData==NULL || pCompData==NULL)
		return 0;
	pStr1 = (char*)pData;
	pStr2 = (char*)pCompData;
	if(strcmp(pStr1,pStr2)==0)
		return 1;
	else
		return 0;
}

static void DSTest_CheckMap(void)
{
	char * strPos;
	JS_HANDLE hItemPos = NULL;
	string stgTmp;
	string stgVal;
	int nCnt;
	list<string> listOrg;
	list<string> listComp;
	map<string,string>::iterator Iter_Pos;
	list<string>::iterator ItOrg;
	list<string>::iterator ItComp;
	////check size
	if(JS_HashMap_GetSize(g_rcTest.hMap) == g_rcTest.objMap.size()) {
		printf("CheckMap: size comparison OK! (%d)\n",JS_HashMap_GetSize(g_rcTest.hMap));
	}else {
		printf("CheckMap: size comparison error (%d<-->%d)\n",JS_HashMap_GetSize(g_rcTest.hMap),g_rcTest.objMap.size());
		return;
	}
	nCnt = 0;
	////comparison
	hItemPos = NULL;
	listOrg.clear();
	while(1) {
		hItemPos = JS_HashMap_GetNext(g_rcTest.hMap,hItemPos);
		if(hItemPos==NULL)
			break;
		strPos = (char*)JS_List_GetDataFromIterateItem(hItemPos);
		if(strPos) {
			stgVal = strPos;
			listOrg.push_back(strPos);
		}
	}
	listComp.clear();
	Iter_Pos = g_rcTest.objMap.begin();
	while(1) {
		if(Iter_Pos == g_rcTest.objMap.end())
			break;
		stgVal = Iter_Pos->second;
		listComp.push_back(stgVal);
		Iter_Pos++;
	}
	listOrg.sort();
	listComp.sort();
	ItOrg = listOrg.begin();
	ItComp = listComp.begin();
	while(1) {
		if(ItOrg==listOrg.end())
			break;
		if(ItComp==listComp.end())
			break;
		stgTmp = *ItOrg;
		stgVal = *ItComp;
		if(stgTmp.compare(stgVal)!=0) {
			printf("CheckMap: find error %s<-->%s\n",stgTmp.c_str(),stgVal.c_str());
			g_rcTest.nErrorCnt++;
		}
		ItOrg++;
		ItComp++;

	}

	listOrg.clear();
	listComp.clear();
	printf("CheckMap: total error=%d,performance=%d%% results are like bellow\n",g_rcTest.nErrorCnt,JS_HashMap_DebugPerformance(g_rcTest.hMap));
	DSTest_PrintMap(0,1,0,10);
}

static void DSTest_PrintMap(int nThread, int nIsConsole, int nRmIndex, int nMax)
{
	char * strPos;
	char strRet[256];
	JS_HANDLE hItemPos = NULL;
	map<string,string>::iterator Iter_Pos;
	int nCnt=0;
	int nBreakNow = 0;

	EnterCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("map size=%d\n",JS_HashMap_GetSize(g_rcTest.hMap)); 

	LeaveCriticalSection(&g_rcTest.rcMutex);
	while(1) {
		EnterCriticalSection(&g_rcTest.rcMutex);
		hItemPos = JS_HashMap_GetNext(g_rcTest.hMap,hItemPos);
		LeaveCriticalSection(&g_rcTest.rcMutex);
		if(hItemPos==NULL) {
			break;
		}
		strRet[0] = 0;
		EnterCriticalSection(&g_rcTest.rcMutex);
		strPos = (char*)JS_HashMap_GetDataFromIterateItem(g_rcTest.hMap,hItemPos);
		sprintf_s(strRet,"%s",strPos);
		if(strRet[0]!=0 && nIsConsole)
			printf("%s ",strRet);
		if(nRmIndex>0 && nCnt==nRmIndex) {
			if(strPos) {
				if(g_rcTest.objMap.size()>0) {
					printf("JS_HashMap_PopPosition %s\n",strRet); 
					g_rcTest.objMap.erase(strRet);
					JS_HashMap_Remove(g_rcTest.hMap,strPos);
				}
			}
			nBreakNow = 1;
		}
		LeaveCriticalSection(&g_rcTest.rcMutex);
		nCnt++;
		if(nBreakNow)
			break;
		if(nMax>0&&nCnt>=nMax)
			break;			
	}
	EnterCriticalSection(&g_rcTest.rcMutex);
	JS_HashMap_ClearIterationHandler(g_rcTest.hMap,hItemPos);
	LeaveCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("\n");
}

static void DSTest_DoAutoMapTest(void * pParam)
{
	int nCnt = 0;
	int nRand;
	int nCmd;
	int nSleep;
	char strTemp[256];
	char strRet[256];
	string stgKey, stgVal;
	char * pData;
	char * pRet;
	char * pStr = (char*)pParam;
	map<string,string>::iterator it;

	for(nCnt=0; nCnt<g_rcTest.nRunLength; nCnt++) {
		nRand = DSTest_GetRand();
		nCmd = nRand%7;
		switch(nCmd) {
			case 0:
			case 1:
			case 5:
			case 6:
				pData = (char*)malloc(64);
				sprintf_s(pData,64,"%s_%d",pStr,nCnt);
				EnterCriticalSection(&g_rcTest.rcMutex);
				JS_HashMap_Add(g_rcTest.hMap,pData);
				stgKey = pData;
				stgVal = pData;
				g_rcTest.objMap[stgKey] = stgVal;
				printf("%d: JS_HashMap_Add %s\n",nCnt,pData);
				LeaveCriticalSection(&g_rcTest.rcMutex);				
				break;
			case 2:
				nRand = DSTest_GetRand();
				if(nCnt>0)
					nRand = nRand%nCnt;
				else
					nRand = 0;
				sprintf_s(strTemp,64,"%s_%d",pStr,nRand);
				EnterCriticalSection(&g_rcTest.rcMutex);
				JS_HashMap_RemoveEx(g_rcTest.hMap, strTemp,DSTest_FindCallback);
				if(g_rcTest.objMap.size()>0)
					g_rcTest.objMap.erase(strTemp);
				printf("%d: JS_HashMap_Remove %s\n",nCnt,strTemp);
				LeaveCriticalSection(&g_rcTest.rcMutex);
				break;
			case 3:
				stgVal = "";
				strRet[0] = 0;
				nRand = DSTest_GetRand();
				if(nCnt>0)
					nRand = nRand%nCnt;
				else
					nRand = 0;
				sprintf_s(strTemp,64,"%s_%d",pStr,nRand);
				EnterCriticalSection(&g_rcTest.rcMutex);
				pRet = (char*)JS_HashMap_Find(g_rcTest.hMap, strTemp,DSTest_FindCallback);
				if(pRet)
					sprintf_s(strRet,"%s",pRet);
				if(g_rcTest.objMap.size()>0) {
					it = g_rcTest.objMap.find(strTemp);
					if(it!=g_rcTest.objMap.end())
						stgVal = it->second;
				}
				LeaveCriticalSection(&g_rcTest.rcMutex);
				if(stgVal.compare(strRet)!=0) {
					g_rcTest.nErrorCnt++;
					printf("%d: JS_HashMap_Find error %s<-->%s!\n",nCnt,strRet,stgVal.c_str());
				}else {
					printf("%d: JS_HashMap_Find OK %s\n",nCnt,strRet);
				}
				break;
			case 4:
				DSTest_PrintMap(0,0,5,0);
				break;
		}
		//DSTest_PrintMap(0,0,0,0);
		nRand = DSTest_GetRand();
		//nSleep = nRand%50;
		nSleep = 10;
		Sleep(nSleep);
	}
	if(pStr)
		free(pStr);
	EnterCriticalSection(&g_rcTest.rcMutex);
	g_rcTest.nThreadNum--;
	LeaveCriticalSection(&g_rcTest.rcMutex);
}


int DSTest_AutoRun_Map(int nThreadNum, int nRunLength) 
{
	int nCnt;
	char * pStr;
	DWORD nThreadID;
	MYDBGPRINT("Start\n");
	g_rcTest.hMap = JS_HashMap_Create(NULL,NULL,DSTest_HashCallback,1024,1);
	g_rcTest.nRunLength =  nRunLength;
	g_rcTest.nThreadNum = nThreadNum;
	g_rcTest.objMap.clear();
	g_rcTest.nCmd = 2;
	g_rcTest.nErrorCnt = 0;
	for(nCnt=0; nCnt<nThreadNum; nCnt++) {
		pStr = (char*)malloc(64);
		sprintf_s(pStr,64,"TA%d",nCnt);
		CreateThread(NULL, 0, _JS_DS_Thread_, (void *)pStr,0,&nThreadID);
	}
	while(1) {
		if(g_rcTest.nThreadNum<=0)
			break;
		Sleep(50);
	}
	DSTest_CheckMap();
	g_rcTest.objMap.clear();
	JS_HashMap_Destroy(g_rcTest.hMap);
	return 0;
}

typedef struct DSTestEntryTag{
	int nType;
	int nLen;
	char * pString;
}DSTestEntry;

static void DSTest_CheckPool(void)
{
	JS_POOL_ITEM_T * pPoolItem;
	JS_HANDLE hItemPos = NULL;
	string stgTmp;
	string stgVal;
	int nCnt;
	list<string> listOrg;
	list<string> listComp;
	map<string,string>::iterator Iter_Pos;
	list<string>::iterator ItOrg;
	list<string>::iterator ItComp;
	DSTestEntry * pEntry;
	////check size
	if(JS_3Pool_GetSize(g_rcTest.hPool,JS_POOL_PHASE_HOT) == g_rcTest.objMap.size()) {
		printf("CheckPool: size comparison OK! (%d)\n",g_rcTest.objMap.size());
	}else {
		printf("CheckPool: size comparison error (%d<-->%d)\n",JS_3Pool_GetSize(g_rcTest.hPool,JS_POOL_PHASE_HOT),g_rcTest.objMap.size());
		return;
	}
	nCnt = 0;
	////comparison
	hItemPos = JS_GetHead(g_rcTest.hPool,JS_POOL_PHASE_HOT);
	listOrg.clear();
	while(1) {
		hItemPos = JS_3Pool_GetNext(g_rcTest.hPool,hItemPos);
		if(hItemPos==NULL)
			break;
		pPoolItem = JS_3Pool_GetDataFromIterateItem(g_rcTest.hPool,hItemPos);
		if(pPoolItem) {
			pEntry = (DSTestEntry *) pPoolItem->pMyData;
			if(pEntry && pEntry->pString) {
				stgVal = pEntry->pString;
				listOrg.push_back(pEntry->pString);
			}
		}
	}
	listComp.clear();
	Iter_Pos = g_rcTest.objMap.begin();
	while(1) {
		if(Iter_Pos == g_rcTest.objMap.end())
			break;
		stgVal = Iter_Pos->second;
		listComp.push_back(stgVal);
		Iter_Pos++;
	}
	listOrg.sort();
	listComp.sort();
	ItOrg = listOrg.begin();
	ItComp = listComp.begin();
	while(1) {
		if(ItOrg==listOrg.end())
			break;
		if(ItComp==listComp.end())
			break;
		stgTmp = *ItOrg;
		stgVal = *ItComp;
		if(stgTmp.compare(stgVal)!=0) {
			printf("CheckPool: find error %s<-->%s\n",stgTmp.c_str(),stgVal.c_str());
			g_rcTest.nErrorCnt++;
		}
		ItOrg++;
		ItComp++;
	}

	listOrg.clear();
	listComp.clear();
	printf("CheckPool: total error=%d results are like bellow\n",g_rcTest.nErrorCnt);
	DSTest_PrintPool(0,1,0,10);
}

static void DSTest_PrintPool(int nThread, int nIsConsole, int nRmIndex, int nMax)
{
	DSTestEntry * pEntry;
	char strRet[256];
	JS_POOL_ITEM_T * pPoolItem;
	JS_HANDLE hItemPos = NULL;
	int nCnt=0;
	int nBreak = 0;

	EnterCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("pool size=%d\n",JS_3Pool_GetSize(g_rcTest.hPool,JS_POOL_PHASE_HOT)); 
	else
		MYDBGPRINT("pool size=%d\n",JS_3Pool_GetSize(g_rcTest.hPool,JS_POOL_PHASE_HOT)); 
	hItemPos = JS_GetHead(g_rcTest.hPool,JS_POOL_PHASE_HOT);
	LeaveCriticalSection(&g_rcTest.rcMutex);
	while(1) {
		EnterCriticalSection(&g_rcTest.rcMutex);
		hItemPos = JS_3Pool_GetNext(g_rcTest.hPool,hItemPos);
		LeaveCriticalSection(&g_rcTest.rcMutex);
		if(hItemPos==NULL)
			break;
		strRet[0] = 0;
		EnterCriticalSection(&g_rcTest.rcMutex);
		pPoolItem = JS_3Pool_GetDataFromIterateItem(g_rcTest.hPool,hItemPos);
		if(pPoolItem) {
			pEntry = (DSTestEntry*) pPoolItem->pMyData;
			sprintf_s(strRet,"%s",pEntry->pString);
			nCnt++;
			if(strRet[0]!=0 && nIsConsole)
				printf("%s ",strRet);
			if(nMax>0&&nCnt>=nMax)
				nBreak = 1;
			if(nRmIndex>0 && nCnt==nRmIndex) {
				if(g_rcTest.objMap.size()>0) {
					JS_3Pool_FreeItem(g_rcTest.hPool,pPoolItem,0);
					g_rcTest.objMap.erase(strRet);
				}
				printf("JS_HashMap_PopPosition %s\n",strRet); 
				nBreak = 1;
			}
		}
		LeaveCriticalSection(&g_rcTest.rcMutex);
		if(nBreak)
			break;
	}
	EnterCriticalSection(&g_rcTest.rcMutex);
	JS_3Pool_ClearIterationHandler(g_rcTest.hPool,hItemPos);
	LeaveCriticalSection(&g_rcTest.rcMutex);
	if(nIsConsole)
		printf("\n");
}

static void DSTest_DoAutoPoolTest(void * pParam)
{
	int nCnt = 0;
	int nRand;
	int nCmd;
	int nSleep;
	char strTemp[256];
	char strRet[256];
	string stgKey, stgVal,stgTmp;
	char * pData;
	char * pStr = (char*)pParam;
	JS_POOL_ITEM_T * pPoolItem;
	map<string,string>::iterator it;
	DSTestEntry * pEntry;

	for(nCnt=0; nCnt<g_rcTest.nRunLength; nCnt++) {
		nRand = DSTest_GetRand();
		nCmd = nRand%7;
		switch(nCmd) {
			case 0:
			case 1:
			case 5:
			case 6:
				pData = (char*)malloc(64);
				sprintf_s(pData,64,"%s_%d",pStr,nCnt);
				stgTmp = pData;
				EnterCriticalSection(&g_rcTest.rcMutex);
				pPoolItem = JS_3Pool_ActivateAnyFreeItem(g_rcTest.hPool);
				pEntry = (DSTestEntry *)pPoolItem->pMyData;
				pEntry->pString= pData;
				JS_3Pool_FinishInitItem(g_rcTest.hPool,0,pPoolItem);
				stgKey = pData;
				stgVal = pData;
				g_rcTest.objMap[stgKey] = stgVal;				
				LeaveCriticalSection(&g_rcTest.rcMutex);
				printf("%d: JS_3Pool_ActivateAnyFreeItem %s\n",nCnt,stgTmp.c_str());
				break;
			case 2:
				nRand = DSTest_GetRand();
				if(nCnt>0)
					nRand = nRand%nCnt;
				else
					nRand = 0;
				sprintf_s(strTemp,64,"%s_%d",pStr,nRand);
				EnterCriticalSection(&g_rcTest.rcMutex);
				pPoolItem = JS_3Pool_FindItem(g_rcTest.hPool, strTemp, DSTest_PoolFindCallback);
				if(pPoolItem) {
					JS_3Pool_FreeItem(g_rcTest.hPool, pPoolItem,0);
					JS_3Pool_CheckStatus(g_rcTest.hPool,500,NULL);
				}
				if(g_rcTest.objMap.size()>0)
					g_rcTest.objMap.erase(strTemp);
				LeaveCriticalSection(&g_rcTest.rcMutex);
				printf("%d: JS_3Pool_FreeItem %s\n",nCnt,strTemp);
				break;
			case 3:
				stgVal = "";
				strRet[0] = 0;
				nRand = DSTest_GetRand();
				if(nCnt>0)
					nRand = nRand%nCnt;
				else
					nRand = 0;
				sprintf_s(strTemp,64,"%s_%d",pStr,nRand);
				EnterCriticalSection(&g_rcTest.rcMutex);
				pPoolItem = JS_3Pool_FindItem(g_rcTest.hPool, strTemp, DSTest_PoolFindCallback);
				if(pPoolItem) {
					pEntry = (DSTestEntry *)pPoolItem->pMyData;
					sprintf_s(strRet,"%s",(char*)pEntry->pString);
				}
				if(g_rcTest.objMap.size()>0) {
					it = g_rcTest.objMap.find(strTemp);
					if(it!=g_rcTest.objMap.end())
						stgVal = it->second;
				}
				LeaveCriticalSection(&g_rcTest.rcMutex);
				if(stgVal.compare(strRet)!=0) {
					g_rcTest.nErrorCnt++;
					printf("%d: JS_3Pool_FindItem error %s<-->%s!\n",nCnt,strRet,stgVal.c_str());
				}else {
					printf("%d: JS_3Pool_FindItem OK %s\n",nCnt,strRet);
				}
				break;
			case 4:
				DSTest_PrintPool(0,0,5,0);
				break;
		}
		nRand = DSTest_GetRand();
		nSleep = 10;
		Sleep(nSleep);
	}
	if(pStr)
		free(pStr);
	EnterCriticalSection(&g_rcTest.rcMutex);
	g_rcTest.nThreadNum--;
	LeaveCriticalSection(&g_rcTest.rcMutex);
}

static int DSTest_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nRet = 0;
	DSTestEntry * pEntry;
	char * pData;
	
	pEntry = (DSTestEntry*)pPoolItem->pMyData;
	pData = pEntry->pString;

	////do something according to the phase
	if(nNewPhase==JS_POOL_PHASE_HOT) {
		;
	}else if(nNewPhase==JS_POOL_PHASE_WARM) {
		; ///do nothing
	}else if(nNewPhase==JS_POOL_PHASE_COLD) {
		if(pData)
			free(pData);
	}
	return nRet;
}

static int DSTest_PoolHashCallback (void * pOwner, void * pData, void * pCompData)
{
	int nRet = 0;
	JS_POOL_ITEM_T * pPoolItem = (JS_POOL_ITEM_T *)pData;
	void * pKey = NULL;
	DSTestEntry * pEntry;	
	
	if(pData != NULL) {
		pEntry = (DSTestEntry*)pPoolItem->pMyData;
		pKey = pEntry->pString;
	}else
		pKey = pCompData;
	if(pKey)
		return JS_HashMap_CalculateHashValue(pKey,0,1);
	else
		return 0;
}

static int DSTest_PoolFindCallback (void * pOwner, void * pData, void * pKey)
{
	char * pStr1;
	char * pStr2;
	DSTestEntry * pEntry;
	JS_POOL_ITEM_T * pPoolItem = (JS_POOL_ITEM_T *)pData;
	if(pData==NULL || pKey==NULL)
		return 0;
	pEntry = (DSTestEntry*)pPoolItem->pMyData;
	pStr1 = (char*)pEntry->pString;
	pStr2 = (char*)pKey;
	if(strcmp(pStr1,pStr2)==0)
		return 1;
	else
		return 0;
}

int DSTest_AutoRun_Pool(int nThreadNum, int nRunLength) 
{
	int nCnt;
	char * pStr;
	DWORD nThreadID;
	HANDLE hThread;
	g_rcTest.hPool = JS_3Pool_CreateEx(NULL, 8, sizeof(DSTestEntry), 0, 1024, DSTest_PhaseChange,DSTest_PoolHashCallback);
	g_rcTest.nRunLength =  nRunLength;
	g_rcTest.nThreadNum = nThreadNum;
	g_rcTest.objMap.clear();
	g_rcTest.nCmd = 3;
	g_rcTest.nErrorCnt = 0;
	for(nCnt=0; nCnt<nThreadNum; nCnt++) {
		pStr = (char*)malloc(64);
		sprintf_s(pStr,64,"TA%d",nCnt);
		hThread = CreateThread(NULL, 0, _JS_DS_Thread_, (void *)pStr,0,&nThreadID);
		if(hThread)
			CloseHandle(hThread);
	}
	while(1) {
		if(g_rcTest.nThreadNum<=0)
			break;
		Sleep(50);
	}
	DSTest_CheckPool();
	g_rcTest.objMap.clear();
	JS_3Pool_Destroy(g_rcTest.hPool);
	return 0;
}

int DSTest_Init(void)
{
	InitializeCriticalSection(&g_rcTest.rcMutex);
	return 0;
}

int DSTest_Clear(void)
{
	DeleteCriticalSection(&g_rcTest.rcMutex);
	return 0;
}
