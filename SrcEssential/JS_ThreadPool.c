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
Thread pool

1. Os indipendent thread manipulation
2. Reuse thread without additional create/destroy
3. Work Queue limits the number of threads for specific job such as CGI, downloads
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"

#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#if (JS_CONFIG_OS!=JS_CONFIG_OS_WIN32)
#include <pthread.h>
#endif

//////////////////////////////////////////////////
///macro definitions
#define MAX_MONITOR_INVAL	10000
#define MAX_IDLE_INVAL		30000

//////////////////////////////////////////////////
///local types
typedef struct JS_ThreadPoolItemTag {
	JS_HANDLE	hPool;
	volatile int nStatus;
	volatile int nNeedExit;
	volatile int nNeedRun;
	JS_FT_ThreadFunc pFunc;
	int nRunCnt;
	UINT32 nThreadID;
	void * pParam;
}JS_ThreadPoolItem;

typedef struct JS_ThreadPoolGlobalTag {
	int nInMonitor;
	UINT32	nThreadIDCounter;
	JS_HANDLE   hMutex;
	JS_HANDLE	h3Pool;
	volatile int nNeedExit;
}JS_ThreadPoolGlobal;

static int g_nThreadPoolGlobalInit = 0;

//////////////////////////////////////////////////
///functions

__inline static JS_ThreadPoolItem * _RET_MYDATA_(JS_POOL_ITEM_T* pItem)
{
	return (JS_ThreadPoolItem * )pItem->pMyData;
}

#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
static DWORD __stdcall _JS_ThreadFunc_(void * pParam)
#else
static void * _JS_ThreadFunc_(void * pParam)
#endif
{
	JS_ThreadPoolGlobal * pPool;
	JS_POOL_ITEM_T * pPoolItem = (JS_POOL_ITEM_T *)pParam;
	JS_ThreadPoolItem * pItem = _RET_MYDATA_(pPoolItem);
	pPool = (JS_ThreadPoolGlobal *)pItem->hPool;
LABEL_START_THREAD:
	////wait until initializing item
	while(pItem->nNeedRun == 0 && pPool->nNeedExit==0) {
		JS_UTIL_Usleep(10000);
	}
	pItem->nNeedRun = 0;
	pItem->nRunCnt++;
	pItem->nStatus = JS_THREAD_STATUS_RUN;
	if(pPool->nNeedExit)
		goto LABEL_END_OF_THREADFUNC;
	////near atomic schedule
	if(pItem->pFunc==NULL)
		JS_UTIL_Usleep(1000);
	//////////////////////////////////////////////////////
	///call user function
	pItem->pFunc(pItem->pParam);	////will be block
	///end of user function
	//////////////////////////////////////////////////////
	if(g_nThreadPoolGlobalInit==0)
		goto LABEL_END_OF_THREADFUNC;
	if(pPool->nNeedExit)
		goto LABEL_END_OF_THREADFUNC;
	pItem->nStatus = JS_THREAD_STATUS_IDLE;
	JS_UTIL_LockMutex(pPool->hMutex);
	JS_3Pool_FreeItem(pPool->h3Pool,pPoolItem,0);
	JS_UTIL_UnlockMutex(pPool->hMutex);
	if(pPool->nNeedExit==0) {
		pPool->nInMonitor = 1;
		JS_3Pool_CheckStatus(pPool->h3Pool,MAX_MONITOR_INVAL,pPool->hMutex);
		pPool->nInMonitor = 0;
	}
	while(1) {
		if(pItem->nNeedExit) {
			pItem->nNeedExit = 0;
			pItem->nStatus = JS_THREAD_STATUS_NOTSTARTED;
			break;
		}
		if(pItem->nNeedRun) {
			goto LABEL_START_THREAD;
		}
		JS_UTIL_Usleep(10000);
	}
LABEL_END_OF_THREADFUNC:
#if (JS_CONFIG_OS == JS_CONFIG_OS_WIN32)
	return 0;
#else
	return NULL;
#endif
}

static int JS_ThreadPool_PhaseChange (void * pOwner, JS_POOL_ITEM_T * pPoolItem, int nNewPhase)
{
	int nRet = 0;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	DWORD nThreadID;
#else
	pthread_attr_t	thread_attr;
	pthread_t rcThread;
#endif
	JS_ThreadPoolGlobal * pPool;
	JS_ThreadPoolItem * pThreadItem = _RET_MYDATA_(pPoolItem);

	////common initializing
	pPool = (JS_ThreadPoolGlobal *)pOwner;
	pThreadItem->nNeedExit=0;
	pThreadItem->nNeedRun=0;
	pThreadItem->pFunc=NULL;
	pThreadItem->pParam=NULL;
	////do something according to the phase
	if(nNewPhase==JS_POOL_PHASE_HOT) {
		pThreadItem->hPool=pOwner;
		pPool->nThreadIDCounter++;
		if(pPool->nThreadIDCounter==0)
			pPool->nThreadIDCounter=1;
		pThreadItem->nThreadID = pPool->nThreadIDCounter;
		if(pPoolItem->nPhase==JS_POOL_PHASE_COLD) {
			pThreadItem->nStatus=JS_THREAD_STATUS_NOTSTARTED;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
			{
				HANDLE hThread = CreateThread(NULL, JS_CONFIG_NORMAL_STACKSIZE, _JS_ThreadFunc_, (void *)pPoolItem,0,&nThreadID);
				if(hThread)
					CloseHandle(hThread); ///detach thread
			}
#else
			pthread_attr_init(&thread_attr);
			pthread_attr_setstacksize(&thread_attr, JS_CONFIG_NORMAL_STACKSIZE);
			pthread_create(&rcThread, &thread_attr, _JS_ThreadFunc_, (void *)pPoolItem);
			pthread_detach(rcThread);
			pthread_attr_destroy(&thread_attr);
#endif
		}
	}else if(nNewPhase==JS_POOL_PHASE_WARM) {
		pThreadItem->pFunc=NULL;
	}else if(nNewPhase==JS_POOL_PHASE_COLD) {
		pThreadItem->nNeedExit=1;
		DBGPRINT("Cold Thread\n");
		////wait until thread is terminated
		while(pPoolItem->nPhase==JS_POOL_PHASE_WARM && pThreadItem->nStatus != JS_THREAD_STATUS_NOTSTARTED) {
			JS_UTIL_Usleep(10000);
		}
	}
	return nRet;
}

JS_HANDLE JS_ThreadPool_CreatePool(int nMaxNum)
{
	JS_ThreadPoolGlobal * pPool = NULL;

	//DBGPRINT("TMP:Size %u JS_ThreadPoolItem\n",sizeof(JS_ThreadPoolItem));
	//DBGPRINT("TMP:Size %u JS_ThreadPoolGlobal\n",sizeof(JS_ThreadPoolGlobal));
	pPool = (JS_ThreadPoolGlobal *)JS_ALLOC(sizeof(JS_ThreadPoolGlobal));
	if(pPool==NULL) {
		DBGPRINT("JS_ThreadPool_CreatePool JS_ALLOC error 1\n");
		goto LABEL_ERROR_CREATE_POOL;
	}
	memset((char*)pPool,0,sizeof(JS_ThreadPoolGlobal));
	pPool->nInMonitor = 0;
	pPool->nNeedExit = 0;
	pPool->nThreadIDCounter = 0;
	pPool->h3Pool = JS_3Pool_Create(pPool, 32, sizeof(JS_ThreadPoolItem), MAX_IDLE_INVAL, JS_ThreadPool_PhaseChange);
	if(pPool->h3Pool==NULL) {
		DBGPRINT("JS_ThreadPool_CreatePool 3pool error 2\n");
		goto LABEL_ERROR_CREATE_POOL;
	}
	pPool->hMutex = JS_UTIL_CreateMutex();
	g_nThreadPoolGlobalInit = 1;
	return (JS_HANDLE)pPool;
LABEL_ERROR_CREATE_POOL:
	if(pPool->h3Pool) {
		JS_3Pool_Destroy(pPool->h3Pool);
	}
	if(pPool)
		JS_FREE(pPool);
	return NULL;
}

int JS_ThreadPool_Destroy(JS_HANDLE hPool)
{
	JS_ThreadPoolGlobal * pPool = (JS_ThreadPoolGlobal *)hPool;
	if(pPool==NULL)
		return 0;
	pPool->nNeedExit=1;
	JS_UTIL_LockMutex(pPool->hMutex);
	JS_3Pool_Destroy(pPool->h3Pool);
	pPool->h3Pool = NULL;
	JS_UTIL_UnlockMutex(pPool->hMutex);
	JS_UTIL_DestroyMutex(pPool->hMutex);
	g_nThreadPoolGlobalInit = 0;
	////memory release
	JS_FREE(pPool);
	return 0;
}

UINT32  JS_ThreadPool_StartThread(JS_HANDLE *phThread, JS_FT_ThreadFunc pFunc, void * pParam)
{
	if(g_rcGlobal.hThreadPool)
		return JS_ThreadPool_StartThreadEx(g_rcGlobal.hThreadPool,phThread,pFunc,pParam);
	else
		return 0;
}

UINT32 JS_ThreadPool_StartThreadEx(JS_HANDLE hPool, JS_HANDLE *phThread, JS_FT_ThreadFunc pFunc, void * pParam)
{
	JS_POOL_ITEM_T* pPoolItem;
	JS_ThreadPoolItem * pItem = NULL;
	JS_ThreadPoolGlobal * pPool = (JS_ThreadPoolGlobal *)hPool;

	JS_UTIL_LockMutex(pPool->hMutex);
	pPoolItem = JS_3Pool_ActivateAnyFreeItem(pPool->h3Pool);
	if(pPoolItem==NULL)
		JS_3Pool_FinishInitItem(pPool->h3Pool,-1,pPoolItem);
	JS_UTIL_UnlockMutex(pPool->hMutex);
	if(pPoolItem==NULL) {
		DBGPRINT("No more thread error\n");
		*phThread =  NULL;
		goto LABEL_EXIT_STARTTHREAD;
	}
	JS_UTIL_LockMutex(pPool->hMutex);
	pItem = _RET_MYDATA_(pPoolItem);
	*phThread = (JS_HANDLE)pItem;
	pItem->pParam = pParam;
	pItem->pFunc = pFunc;
	pItem->nNeedRun = 1;
	JS_3Pool_FinishInitItem(pPool->h3Pool,0,pPoolItem);
	JS_UTIL_UnlockMutex(pPool->hMutex);

LABEL_EXIT_STARTTHREAD:
	////sleep a while to init thread.
	if(pItem) {
		int nCnt;
		for(nCnt=0; nCnt<10; nCnt++) {
			if(pItem->nStatus == JS_THREAD_STATUS_RUN)
				break;
			JS_UTIL_Usleep(20000);
		}
	}
	if(pItem)
		return pItem->nThreadID;
	else
		return 0;
}

int JS_ThreadPool_GetThreadStatus(JS_HANDLE hThread)
{
	int nRet = 0;
	JS_ThreadPoolItem * pItem = (JS_ThreadPoolItem *)hThread;
	if(pItem)
		nRet = pItem->nStatus;
	return nRet;
}

int JS_ThreadPool_WaitForEndOfThread(JS_HANDLE hThread, unsigned int nWaitMsec, UINT32 nThreadID)
{
	////TBD: fix bug calling this function very long time after kill his thread...(maybe 60s)
	int nRet = 0;
	int nMaxCnt = 0;
	int nCnt;
	JS_ThreadPoolItem * pItem = (JS_ThreadPoolItem *)hThread;
	DBGPRINT("WaitFor:%d\n",nWaitMsec);
	if(pItem) {
		nCnt = 0;
		if(nWaitMsec!=0 && nWaitMsec<20)
			nWaitMsec = 20;
		nMaxCnt = nWaitMsec/20;
		while(1) {
			if(nThreadID != 0 && pItem->nThreadID != nThreadID)
				break;
			if(pItem->nStatus != JS_THREAD_STATUS_RUN)
				break;
			if(nMaxCnt>0 && nCnt++>=nMaxCnt)
				break;
			JS_UTIL_Usleep(20000);
		}
	}
	return nRet;
}

////////////////////////////////////////////////////////////////////////////////////////////////
////work queue implementation

#define WORKQ_WAITFOR_QUEUE 0
#define WORKQ_IN_PROGRESS	1
#define WORKQ_ENDED			2

typedef struct JS_WorkQItemTag {
	JSUINT nWorkID;
	int nStatus;
	int nError;
	unsigned int nProgress;
	unsigned int nTime;
	JS_FT_ThreadFunc  pFunc;
	void * pParam;
	JS_HANDLE hWorkQ;
	JS_HANDLE hThread;
	unsigned int nThreadID;
	JS_FT_WORKQUEUE_CALLBACK pfEvent;
}JS_WorkQItem;

typedef struct JS_ThreadPool_WorkQTag {
	int nNeedExit;
	int nWorksNum;
	int nMaxConcurrent;
	JSUINT nWorkUID;
	JS_HANDLE hLock;
	JS_HANDLE hWorkList;
	JS_FT_COMPARE_ITEM_CALLBACK pfFind;
}JS_ThreadPool_WorkQ;

JS_HANDLE JS_ThreadPool_CreateWorkQueue(int nMaxConcurrentWorks)
{
	int nRet = 0;
	JS_ThreadPool_WorkQ * pWorkQ = NULL;	
	pWorkQ = (JS_ThreadPool_WorkQ*)JS_ALLOC(sizeof(JS_ThreadPool_WorkQ));
	if(pWorkQ==NULL) {
		nRet = -1;
		DBGPRINT("create workq: no mem error (workq)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pWorkQ,0,sizeof(JS_ThreadPool_WorkQ));
	pWorkQ->hLock = JS_UTIL_CreateMutex();
	if(pWorkQ->hLock==NULL) {
		nRet = -1;
		DBGPRINT("create workq: no mem error (lock)\n");
		goto LABEL_CATCH_ERROR;
	}
	pWorkQ->hWorkList = JS_List_Create(pWorkQ,NULL);
	if(pWorkQ->hWorkList==NULL) {
		nRet = -1;
		DBGPRINT("create workq: no mem error (list)\n");
		goto LABEL_CATCH_ERROR;
	}
	pWorkQ->nMaxConcurrent = nMaxConcurrentWorks;
LABEL_CATCH_ERROR:
	if(nRet<0) {
		JS_ThreadPool_DestroyWorkQueue(pWorkQ);
	}
	return pWorkQ;
}

void JS_ThreadPool_DestroyWorkQueue(JS_HANDLE hWorkQ)
{
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	if(pWorkQ) {
		if(pWorkQ->hWorkList) {
			pWorkQ->nNeedExit = 1;
			////TBD fix this
			JS_UTIL_Usleep(100);
			JS_UTIL_LockMutex(pWorkQ->hLock);
			JS_List_Destroy(pWorkQ->hWorkList);
			JS_UTIL_UnlockMutex(pWorkQ->hLock);
		}
		if(pWorkQ->hLock)
			JS_UTIL_DestroyMutex(pWorkQ->hLock);
		JS_FREE(pWorkQ);
	}
}

static void * _JS_WorkQ_Thread_(void * pParam)
{
	JS_ThreadPool_WorkQ * pWorkQ;
	JS_WorkQItem * pItem = (JS_WorkQItem *)pParam;
	pWorkQ = (JS_ThreadPool_WorkQ *)pItem->hWorkQ;
LABEL_WORKQ_LOOP:
	pItem->pFunc(pItem->pParam);
	if(g_nThreadPoolGlobalInit==0)
		return NULL;
	JS_UTIL_LockMutex(pWorkQ->hLock);
	pItem->nStatus = WORKQ_ENDED;
	if(pWorkQ->nWorksNum>0)
		pWorkQ->nWorksNum--;
	pItem->pfEvent(pItem->nWorkID,pItem->pParam,JS_WORKQ_EVENT_WORKDONE, NULL);
	JS_List_RemoveItem(pWorkQ->hWorkList,pItem);
	JS_UTIL_UnlockMutex(pWorkQ->hLock);
	pItem = (JS_WorkQItem *)JS_ThreadPool_CheckWorkQueue(pWorkQ,1);
	if(pItem)
		goto LABEL_WORKQ_LOOP;
	return NULL;
}

JS_HANDLE  JS_ThreadPool_CheckWorkQueue(JS_HANDLE hWorkQ, int nFromWorkThread)
{
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	JS_WorkQItem * pRetItem = NULL;
	JS_WorkQItem * pItem = NULL;
	JS_HANDLE hItemPos = NULL;
	if(pWorkQ->nNeedExit || JS_List_GetSize(pWorkQ->hWorkList)<=0)
		return NULL;
	JS_UTIL_LockMutex(pWorkQ->hLock);
	while(pWorkQ->nWorksNum<pWorkQ->nMaxConcurrent) {
		hItemPos = JS_List_GetNext(pWorkQ->hWorkList,hItemPos);
		if(hItemPos==NULL) 
			break;
		pItem = (JS_WorkQItem *)JS_List_GetDataFromIterateItem(hItemPos);
		if(pItem==NULL)
			break;
		if(pItem->nStatus==WORKQ_WAITFOR_QUEUE) {
			pItem->nStatus=WORKQ_IN_PROGRESS;
			if(nFromWorkThread && pRetItem==NULL) {
				pRetItem = pItem;
			}else {
				pItem->nThreadID = JS_ThreadPool_StartThread(&pItem->hThread,_JS_WorkQ_Thread_,pItem);
				if(pItem->hThread==NULL) {
					pItem->nError = 1;
					pItem->pfEvent(pItem->nWorkID,pItem->pParam,JS_WORKQ_EVENT_ERROR, NULL);
					DBGPRINT("check workq: can't make thread error\n");
					JS_List_PopPosition(pWorkQ->hWorkList,hItemPos);
					JS_FREE(pItem);
					break;
				}
			}
			pWorkQ->nWorksNum++;
		}
	}
	JS_List_ClearIterationHandler(hItemPos);
	JS_UTIL_UnlockMutex(pWorkQ->hLock);
	return pRetItem;
}

JSUINT  JS_ThreadPool_AddWorkQueue(JS_HANDLE hWorkQ, JS_FT_ThreadFunc pFunc, void * pParam, JS_FT_WORKQUEUE_CALLBACK pfEvent)
{
	JSUINT nWorkID;
	int nRet = 0;
	JS_WorkQItem * pItem;
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	if(pWorkQ==NULL)
		return 0;
	pItem = (JS_WorkQItem*)JS_ALLOC(sizeof(JS_WorkQItem));
	if(pItem==NULL) {
		nRet = -1;
		DBGPRINT("add workq: no mem error (item)\n");
		goto LABEL_CATCH_ERROR;
	}
	memset((char*)pItem,0,sizeof(JS_WorkQItem));
	pItem->pFunc = pFunc;
	pItem->pParam = pParam;
	JS_UTIL_LockMutex(pWorkQ->hLock);
	pWorkQ->nWorkUID++;
	if(pWorkQ->nWorkUID==0)
		pWorkQ->nWorkUID=1;
	nWorkID = pWorkQ->nWorkUID;
	JS_UTIL_UnlockMutex(pWorkQ->hLock);
	pItem->nWorkID = nWorkID;
	pItem->nTime = JS_UTIL_GetSecondsFrom1970(); //only for next 400 years
	pItem->hWorkQ = pWorkQ;
	pItem->pfEvent = pfEvent;
	JS_UTIL_LockMutex(pWorkQ->hLock);
	nRet = JS_List_PushBack(pWorkQ->hWorkList,pItem);
	JS_UTIL_UnlockMutex(pWorkQ->hLock);
	if(nRet>=0)
		JS_ThreadPool_CheckWorkQueue(pWorkQ,0);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pItem)
			JS_FREE(pItem);
		nWorkID = 0;
	}
	return nWorkID;
}

static int JS_ThreadPool_WorkQFind (void * pOwner, void * pDataItem, void * pKey)
{
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)pOwner;
	JS_WorkQItem * pItem = (JS_WorkQItem *)pDataItem;
	if(pWorkQ && pItem && pWorkQ->pfFind)
		return pWorkQ->pfFind(NULL,pItem->pParam,pKey);
	else
		return 0;
}

int JS_ThreadPool_GetWorksNum(JS_HANDLE hWorkQ)
{
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	if(pWorkQ)
		return pWorkQ->nWorksNum;
	else 
		return 0;
}

int JS_ThreadPool_CancelWaiting(JS_HANDLE hWorkQ,JSUINT nWorkID, int *pnIsCanceled)
{
	int nRet = 0;
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	JS_WorkQItem * pItem = NULL;
	JS_HANDLE hItemPos = NULL;

	if(pnIsCanceled)
		*pnIsCanceled = 0;
	if(JS_List_GetSize(pWorkQ->hWorkList)>0) {
		while(1) {
			JS_UTIL_LockMutex(pWorkQ->hLock);
			hItemPos = JS_List_GetNext(pWorkQ->hWorkList,hItemPos);
			JS_UTIL_UnlockMutex(pWorkQ->hLock);
			if(hItemPos==NULL)
				break;
			JS_UTIL_LockMutex(pWorkQ->hLock);
			pItem = (JS_WorkQItem *)JS_List_GetDataFromIterateItem(hItemPos);
			if(pItem && pItem->nWorkID == nWorkID && pItem->nStatus==WORKQ_WAITFOR_QUEUE) {
				nRet = 1;
				if(pnIsCanceled)
					*pnIsCanceled = 1;
				JS_List_PopPosition(pWorkQ->hWorkList,hItemPos);
				JS_FREE(pItem);
			}
			JS_UTIL_UnlockMutex(pWorkQ->hLock);
			if(nRet>0)
				break;
		}
		JS_UTIL_LockMutex(pWorkQ->hLock);
		JS_List_ClearIterationHandler(hItemPos);
		JS_UTIL_UnlockMutex(pWorkQ->hLock);
	}
	return nRet;
}

void * JS_ThreadPool_FindWorkQItem(JS_HANDLE hWorkQ,void * pKey, void * pFindFunc)
{
	void * pParam = NULL;
	JS_WorkQItem * pItem;
	JS_FT_COMPARE_ITEM_CALLBACK pfFind = (JS_FT_COMPARE_ITEM_CALLBACK)pFindFunc;
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	if(pWorkQ) {
		JS_UTIL_LockMutex(pWorkQ->hLock);
		pWorkQ->pfFind = pfFind;
		pItem = (JS_WorkQItem * )JS_List_PopItemEx(pWorkQ->hWorkList,pKey,JS_ThreadPool_WorkQFind,1);
		if(pItem)
			pParam = pItem->pParam;
		JS_UTIL_UnlockMutex(pWorkQ->hLock);
	}
	return pParam;
}

char * JS_ThreadPool_ToStringWorkQueue(JS_HANDLE hWorkQ, int nMaxItem)
{
	int nRet = 0;
	JS_StringBuff rcStrBuff;
	JS_ThreadPool_WorkQ * pWorkQ = (JS_ThreadPool_WorkQ *)hWorkQ;
	JS_WorkQItem * pItem = NULL;
	JS_HANDLE hItemPos = NULL;
	char * pJsonBuff = NULL;
	int nBuffSize = 512;
	int nOffset = 0;
	int nCnt = 0;
	if(pWorkQ==NULL || pWorkQ->nNeedExit || JS_List_GetSize(pWorkQ->hWorkList)<=0) {
		goto LABEL_CATCH_ERROR;
	}
	pJsonBuff = JS_UTIL_StrJsonBuildArrayStart(nBuffSize,&nOffset);
	if(pJsonBuff==NULL) {
		DBGPRINT("workq tostring: mem error(jsonbuff)\n");
		goto LABEL_CATCH_ERROR;
	}
	rcStrBuff.pBuff = pJsonBuff;
	rcStrBuff.nBuffSize = nBuffSize;
	rcStrBuff.nOffset = nOffset;
	while(1) {
		JS_UTIL_LockMutex(pWorkQ->hLock);
		hItemPos = JS_List_GetNext(pWorkQ->hWorkList,hItemPos);
		JS_UTIL_UnlockMutex(pWorkQ->hLock);
		if(hItemPos==NULL)
			break;
		JS_UTIL_LockMutex(pWorkQ->hLock);
		pItem = (JS_WorkQItem *)JS_List_GetDataFromIterateItem(hItemPos);
		if(pItem && pItem->nStatus==WORKQ_IN_PROGRESS) {
			pItem->pfEvent(pItem->nWorkID, pItem->pParam, JS_WORKQ_EVENT_TOSTRING, &rcStrBuff);
			if(rcStrBuff.pBuff == NULL) {
				pJsonBuff = NULL;
				break;
			}
		}
		JS_UTIL_UnlockMutex(pWorkQ->hLock);
		nCnt++;
		if(nMaxItem>0 && nCnt>=nMaxItem)
			break;
	}
	JS_UTIL_LockMutex(pWorkQ->hLock);
	JS_List_ClearIterationHandler(hItemPos);
	JS_UTIL_UnlockMutex(pWorkQ->hLock);
	pJsonBuff = rcStrBuff.pBuff;
	JS_UTIL_StrJsonBuildArrayEnd(pJsonBuff,&rcStrBuff.nBuffSize,&rcStrBuff.nOffset);
LABEL_CATCH_ERROR:
	return pJsonBuff;
}