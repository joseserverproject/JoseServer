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

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"


/*********************************************************************
Three state pool : base data structure for thread pool and connection pool
1. An item in pool can be 3 phase: cold, warm or hot
2. hot item is currently used and has some system resource such as TCP connection or thread
3. warm item is not used but keep system resource for a while to reuse it
3. cold item has no system resource and can be deleted from memory
*********************************************************************/

//////////////////////////////////////////////////
///local types
typedef struct JS_3POOL_MutexItemTag {
	int nIndex;
	JS_HANDLE hMutex;
}JS_3POOL_MutexItem;

typedef struct JS_3POOL_StructureTag {
	void * pOwner;
	int nInitialNum;
	int nCurNum;
	int nZombieCount;
	UINT32 nItemSize;
	UINT32 nLifeIntvalMS;
	UINT64 nLastCheckTick;
	JS_HANDLE	hColdList;
	JS_HANDLE	hWarmList;
	JS_HANDLE	hZombieList;
	JS_HANDLE	hHotMap;
	int	nMutexArrayMax;
	int	nMutexArrayRoundRobinCnt;
	JS_HANDLE	hMutexMap;
	JS_FT_PHASECHANGE_CALLBACK pfPhaseChange;
	JS_FT_HASH_ITEM_CALLBACK pfHash;
}JS_3POOL_Structure;

//////////////////////////////////////////////////
///extern functions
extern JS_HANDLE JS_List_MakeIterationHandler(JS_HANDLE hList, int nIsFront);
extern JS_HANDLE JS_HashMap_MakeIterationHandler(JS_HANDLE hMap);


//////////////////////////////////////////////////
///function implementations
static int JS_3Pool_MutexItemHash (void * pOwner, void * pData, void * pKey)
{
	int nRet = 0;
	JS_3POOL_MutexItem * pMutexItem;
	if(pData != NULL) {
		pMutexItem = (JS_3POOL_MutexItem * )pData;
		nRet = pMutexItem->nIndex;
	}else
		nRet = (int)pKey;
	return nRet;
}

static int JS_3Pool_MutexItemFind (void * pOwner, void * pData, void * pKey)
{
	int nOrg,nKey;
	JS_3POOL_MutexItem * pMutexItem;
	pMutexItem = (JS_3POOL_MutexItem * )pData;
	nOrg = pMutexItem->nIndex;
	nKey = (int)pKey;
	if(nOrg==nKey)
		return 1;
	else
		return 0;
}

static int JS_3Pool_DefaultHash (void * pOwner, void * pData, void * pCompData)
{
	int nRet = 0;
	int nPointerVal=0;
	if(pData != NULL) {
		nPointerVal = (int)pData;
	}else
		nPointerVal = (int)pCompData;
	return JS_HashMap_CalculateHashValue((void*)&nPointerVal,sizeof(void *),0);
}

static int _JS_3Pool_Dummy_RM_Func_ (void * pOwner, void * pData)
{
	////do nothing
	return 0;
}

static int _JS_3Pool_Increase_Buffer_ (JS_3POOL_Structure * p3Pool)
{
	int nRet = 0;
	unsigned int nSize;
	int nCnt;
	int nInitialNumber;
	UINT32 nItemSize;
	int nCurNum;
	char * pData;
	JS_POOL_ITEM_T * pPoolItem;

	nInitialNumber = p3Pool->nInitialNum;
	nItemSize = p3Pool->nItemSize;
	nCurNum = p3Pool->nCurNum;
	nSize = sizeof(JS_POOL_ITEM_T)+nItemSize;
	for(nCnt=0; nCnt<nInitialNumber; nCnt++) {
		pData = (char *)JS_ALLOC(nSize);
		if(pData==NULL) {
			DBGPRINT("Can't make 3pool buffer mem error\n");
			nRet = -1;
			goto LABEL_3POOL_BUFFER_EXIT;
		}
		memset((char*)pData, 0, nSize);
		pPoolItem = (JS_POOL_ITEM_T *)pData;
		pPoolItem->nPhase = JS_POOL_PHASE_COLD;
		pPoolItem->pMyData = pData+sizeof(JS_POOL_ITEM_T);
		JS_List_PushBack(p3Pool->hColdList,pPoolItem);
	}
	p3Pool->nCurNum += nInitialNumber;
LABEL_3POOL_BUFFER_EXIT:
	return nRet;
}

int JS_3Pool_MaskGetOldPhase(JS_POOL_ITEM_T * pPoolItem) 
{
	return (pPoolItem->nMask&0xF);
}

int JS_3Pool_MaskSetOldPhase(JS_POOL_ITEM_T * pPoolItem, int nPhase)
{
	return (pPoolItem->nMask|(nPhase&0xF));
}

int JS_3Pool_MaskGetDataID(JS_POOL_ITEM_T * pPoolItem) 
{
	int nMask = pPoolItem->nMask;
	nMask = (nMask>>4)&0xFF;
	return nMask;
}

int JS_3Pool_MaskSetDataID(JS_POOL_ITEM_T * pPoolItem, int nDataID)
{
	int nMask = pPoolItem->nMask;
	nMask = nMask&(~0xFF0);
	pPoolItem->nMask = nMask|((nDataID<<4)&0xFF0);
	return pPoolItem->nMask;
}

int JS_3Pool_MaskGetZombie(JS_POOL_ITEM_T * pPoolItem)
{
	int nMask = pPoolItem->nMask;
	nMask = (nMask>>12)&0xF;
	return nMask;
}

int JS_3Pool_MaskSetZombie(JS_POOL_ITEM_T * pPoolItem, int nEnable)
{
	int nMask = pPoolItem->nMask;
	nMask = nMask&(~0xF000);
	pPoolItem->nMask = nMask|((nEnable<<12)&0xF000);
	return pPoolItem->nMask;
}


void JS_3Pool_AttachMutexArray(JS_HANDLE hPool, int nMaxMutexes)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(p3Pool==NULL || nMaxMutexes<=0) 
		return;
	if(p3Pool->nMutexArrayMax>0) {
		DBGPRINT("attach mutex array: can't reattach array\n");
		return;
	}
	p3Pool->nMutexArrayMax = nMaxMutexes;
	p3Pool->nMutexArrayRoundRobinCnt = 0;
	p3Pool->hMutexMap = JS_HashMap_Create(p3Pool,NULL,JS_3Pool_MutexItemHash,nMaxMutexes,1);
}

JS_HANDLE JS_3Pool_Create(void * pOwner, int nInitialNumber, UINT32 nItemSize, UINT32 nLifeTime, JS_FT_PHASECHANGE_CALLBACK pPhaseFunc)
{
	return JS_3Pool_CreateEx(pOwner, nInitialNumber, nItemSize, nLifeTime, nInitialNumber*16, pPhaseFunc, NULL);	
}

JS_HANDLE JS_3Pool_CreateEx(void * pOwner, int nInitialNumber, UINT32 nItemSize, UINT32 nLifeTime, int nHashSpace, JS_FT_PHASECHANGE_CALLBACK pPhaseFunc, JS_FT_HASH_ITEM_CALLBACK pfHash)
{
	JS_3POOL_Structure * p3Pool = NULL;

	p3Pool = (JS_3POOL_Structure *)JS_ALLOC(sizeof(JS_3POOL_Structure));
	if(p3Pool==NULL) {
		DBGPRINT("Can't make 3pool mem error\n");
		goto LABEL_3POOL_CREATE_ERROR;
	}
	if(pfHash==NULL)
		pfHash = JS_3Pool_DefaultHash;
	if(pPhaseFunc==NULL || pfHash==NULL) {
		DBGPRINT("Can't make 3pool without phase callback and hash callback\n");
		goto LABEL_3POOL_CREATE_ERROR;
	}

	memset((char*)p3Pool, 0, sizeof(JS_3POOL_Structure));
	p3Pool->nInitialNum = nInitialNumber;
	p3Pool->nZombieCount = 0;
	p3Pool->nCurNum = 0;
	p3Pool->nItemSize = nItemSize;
	p3Pool->nLifeIntvalMS = nLifeTime;
	p3Pool->pOwner = pOwner;
	p3Pool->pfPhaseChange = pPhaseFunc;
	p3Pool->nLastCheckTick = 0;
	p3Pool->pfHash = pfHash;
	p3Pool->nMutexArrayRoundRobinCnt = 0;
	p3Pool->nMutexArrayMax = 0;
	p3Pool->hMutexMap = NULL;
	//DBGPRINT("TMP:ItemSize=%d\n",p3Pool->nItemSize);
	p3Pool->hZombieList = JS_List_Create(p3Pool,_JS_3Pool_Dummy_RM_Func_);
	p3Pool->hColdList = JS_List_Create(p3Pool,NULL);	////del from coldlist means memory release...
	p3Pool->hWarmList = JS_List_Create(p3Pool,_JS_3Pool_Dummy_RM_Func_);	///no memory release when warm to X
	p3Pool->hHotMap = JS_HashMap_Create(p3Pool,_JS_3Pool_Dummy_RM_Func_,pfHash,nHashSpace,1);	///no memory release when hot to X
	if(p3Pool->hZombieList==NULL || p3Pool->hColdList==NULL || p3Pool->hWarmList==NULL || p3Pool->hHotMap==NULL) {
		DBGPRINT("Can't make 3pool lists mem error\n");
		goto LABEL_3POOL_CREATE_ERROR;
	}
	if(_JS_3Pool_Increase_Buffer_(p3Pool)<0) {
		DBGPRINT("serious 3pool mem error\n");
		goto LABEL_3POOL_CREATE_ERROR;
	}
 	return p3Pool;
LABEL_3POOL_CREATE_ERROR:
	if(p3Pool) {
		if(p3Pool->hColdList)
			JS_List_Destroy(p3Pool->hColdList);
		if(p3Pool->hWarmList)
			JS_List_Destroy(p3Pool->hWarmList);
		if(p3Pool->hHotMap)
			JS_HashMap_Destroy(p3Pool->hHotMap);
	}
	if(p3Pool)
		JS_FREE(p3Pool);
	return NULL;
}

void JS_3Pool_Destroy(JS_HANDLE hPool)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	JS_HANDLE hItemPos;
	JS_POOL_ITEM_T * pPoolItem;
	if(p3Pool) {
		if(p3Pool->hWarmList) {
			hItemPos = NULL;
			while(1) {
				hItemPos = JS_List_GetNext(p3Pool->hWarmList,hItemPos);
				if(hItemPos==NULL)
					break;
				pPoolItem = (JS_POOL_ITEM_T *)JS_List_GetDataFromIterateItem(hItemPos);
				p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_COLD);
				pPoolItem->nPhase = JS_POOL_PHASE_COLD;
				JS_FREE(pPoolItem);
			}
			JS_List_ClearIterationHandler(hItemPos);
			JS_List_Destroy(p3Pool->hWarmList);
		}
		if(p3Pool->hHotMap){
			int nHotSize = JS_HashMap_GetSize(p3Pool->hHotMap);
			hItemPos = NULL;
			while(1) {
				hItemPos = JS_HashMap_GetNext(p3Pool->hHotMap,hItemPos);
				if(hItemPos==NULL)
					break;
				pPoolItem = (JS_POOL_ITEM_T *)JS_HashMap_GetDataFromIterateItem(p3Pool->hHotMap,hItemPos);
				p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_COLD);
				pPoolItem->nPhase = JS_POOL_PHASE_COLD;
				JS_FREE(pPoolItem);
			}
			JS_HashMap_ClearIterationHandler(p3Pool->hHotMap,hItemPos);
			JS_HashMap_Destroy(p3Pool->hHotMap);
		}
		if(p3Pool->hColdList)
			JS_List_Destroy(p3Pool->hColdList);
		if(p3Pool->hZombieList)
			JS_List_Destroy(p3Pool->hZombieList);
		////free mutex array
		if(p3Pool->hMutexMap) {
			JS_3POOL_MutexItem * pMutexItem;
			hItemPos = NULL;
			while(1) {
				hItemPos = JS_HashMap_GetNext(p3Pool->hMutexMap,hItemPos);
				if(hItemPos==NULL)
					break;
				pMutexItem = (JS_3POOL_MutexItem *)JS_HashMap_GetDataFromIterateItem(p3Pool->hMutexMap,hItemPos);
				if(pMutexItem) {
					if(pMutexItem->hMutex)
						JS_UTIL_DestroyMutex(pMutexItem->hMutex);
				}
			}
			JS_HashMap_ClearIterationHandler(p3Pool->hMutexMap,hItemPos);
			JS_HashMap_Destroy(p3Pool->hMutexMap);
		}
		JS_FREE(p3Pool);
	}
}

int JS_3Pool_GetSize(JS_HANDLE hPool, int nPhase)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(p3Pool==NULL)
		return 0;
	if(nPhase==JS_POOL_PHASE_COLD)
		return JS_List_GetSize(p3Pool->hColdList);
	else if(nPhase==JS_POOL_PHASE_HOT)
		return JS_HashMap_GetSize(p3Pool->hHotMap);
	else if(nPhase==JS_POOL_PHASE_WARM)
		return JS_List_GetSize(p3Pool->hWarmList);
	else
		return p3Pool->nCurNum;
}

static int JS_3Pool_LinkMutexToPoolItem(JS_3POOL_Structure * p3Pool, JS_POOL_ITEM_T * pPoolItem)
{
	int nRet = 0;
	JS_3POOL_MutexItem * pMutexItem = NULL;
	if(p3Pool->nMutexArrayMax > 0 && p3Pool->hMutexMap) {
		int nArraySize = JS_HashMap_GetSize(p3Pool->hMutexMap);
		if(nArraySize<p3Pool->nMutexArrayMax) {
			////make new mutex and link
			pMutexItem = (JS_3POOL_MutexItem*)JS_ALLOC(sizeof(JS_3POOL_MutexItem));
			pMutexItem->nIndex = nArraySize;
			pMutexItem->hMutex = JS_UTIL_CreateMutex();
			if(pMutexItem->hMutex==NULL) {
				nRet = -1;
				goto LABEL_LINK_MUTEX;
			}
			nRet = JS_HashMap_Add(p3Pool->hMutexMap,pMutexItem);
		}else {
			pMutexItem = (JS_3POOL_MutexItem *)JS_HashMap_Find(p3Pool->hMutexMap,(void*)p3Pool->nMutexArrayRoundRobinCnt,JS_3Pool_MutexItemFind);
			if(pMutexItem==NULL) {
				nRet = -1;
				goto LABEL_LINK_MUTEX;
			}
			p3Pool->nMutexArrayRoundRobinCnt++;
			if(p3Pool->nMutexArrayRoundRobinCnt>=p3Pool->nMutexArrayMax)
				p3Pool->nMutexArrayRoundRobinCnt = 0;
		}
		if(nRet>=0) {
			pPoolItem->hMutex = pMutexItem->hMutex;
		}
	}
LABEL_LINK_MUTEX:
	if(nRet<0) {
		if(pMutexItem) {
			if(pMutexItem->hMutex)
				JS_UTIL_DestroyMutex(pMutexItem->hMutex);
			JS_FREE(pMutexItem);
		}
	}
	return nRet;
}

JS_POOL_ITEM_T * JS_3Pool_ActivateColdFreeItem(JS_HANDLE hPool)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	int nColdNum=0;
	int nRet = 0;
	if(p3Pool==NULL)
		return NULL;
LABEL_3POOL_COLDFREE_START:
	nColdNum = JS_List_GetSize(p3Pool->hColdList);
	if(nColdNum>0) {
		pPoolItem = (JS_POOL_ITEM_T *)JS_List_PopFront(p3Pool->hColdList);
	}
	if(pPoolItem) {
		int nOldPhase = pPoolItem->nPhase;
		JS_3Pool_MaskSetOldPhase(pPoolItem,nOldPhase);
		nRet = JS_3Pool_LinkMutexToPoolItem(p3Pool,pPoolItem);
		if(nRet>=0) {
			pPoolItem->nHotTick = JS_UTIL_GetTickCount();
			nRet = p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_HOT);
			if(nRet>=0)
				pPoolItem->nPhase = JS_POOL_PHASE_HOT;
		}
		if(nRet<0) {
			DBGPRINT("State function error can't make pool item\n");
			JS_List_PushBack(p3Pool->hColdList,pPoolItem);
			pPoolItem = NULL;
		}
	}else {
		///alloc new chunk...
		nRet = _JS_3Pool_Increase_Buffer_(p3Pool);
		if(nRet>=0)
			goto LABEL_3POOL_COLDFREE_START;
	}
	return pPoolItem;
}

JS_POOL_ITEM_T * JS_3Pool_ActivateAnyFreeItem(JS_HANDLE hPool)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	int nColdNum=0;
	int nWarmNum=0;
	int nIsFromWarm = 0;
	int nRet = 0;
	if(p3Pool==NULL)
		return NULL;
LABEL_3POOL_ANYFREE_START:
	nWarmNum = JS_List_GetSize(p3Pool->hWarmList);
	if(nWarmNum==0)
		nColdNum = JS_List_GetSize(p3Pool->hColdList);
	if(nWarmNum>0) {
		pPoolItem = (JS_POOL_ITEM_T *)JS_List_PopFront(p3Pool->hWarmList);
		nIsFromWarm = 1;
	}else if(nColdNum>0) {
		pPoolItem = (JS_POOL_ITEM_T *)JS_List_PopFront(p3Pool->hColdList);
	}
	if(pPoolItem) {
		int nOldPhase = pPoolItem->nPhase;
		JS_3Pool_MaskSetOldPhase(pPoolItem,nOldPhase);
		nRet = JS_3Pool_LinkMutexToPoolItem(p3Pool,pPoolItem);
		if(nRet>=0) {
			pPoolItem->nHotTick = JS_UTIL_GetTickCount();
			nRet = p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_HOT);
			if(nRet>=0)
				pPoolItem->nPhase = JS_POOL_PHASE_HOT;
		}
		if(nRet<0) {
			DBGPRINT("State function error can't make pool item\n");
			if(nIsFromWarm)
				JS_List_PushBack(p3Pool->hWarmList,pPoolItem);
			else
				JS_List_PushBack(p3Pool->hColdList,pPoolItem);
			pPoolItem = NULL;
		}
	}else {
		///alloc new chunk...
		nRet = _JS_3Pool_Increase_Buffer_(p3Pool);
		if(nRet>=0)
			goto LABEL_3POOL_ANYFREE_START;
	}
	return pPoolItem;
}

JS_POOL_ITEM_T * JS_3Pool_ActivateSimliarWarmItem(JS_HANDLE hPool, JS_FT_RESAMBLANCE_CALLBACK pSimlilarFunc, void * pCompData)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	UINT32 nMaxScore = 0;
	UINT32 nScore;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	JS_POOL_ITEM_T * pPoolItemMaxPos = NULL;
	int nWarmNum=0;
	int nIsFromWarm = 0;
	JS_HANDLE hItemPos=NULL;
	int nRet;
	if(p3Pool==NULL)
		return NULL;
	nWarmNum = JS_List_GetSize(p3Pool->hWarmList);
	if(nWarmNum>0) {
		hItemPos = NULL;
		do{
			hItemPos = JS_List_GetNext(p3Pool->hWarmList,hItemPos);
			if(hItemPos) {
				pPoolItem = (JS_POOL_ITEM_T *)JS_List_GetDataFromIterateItem(hItemPos);
				nScore = pSimlilarFunc(p3Pool->pOwner,pPoolItem,pCompData);
				if(nScore>nMaxScore) {
					pPoolItemMaxPos = pPoolItem;
					nMaxScore = nScore;
				}
				if(nScore == JS_POOL_RET_TOOSIMILAR)
					break;
			}
		}while(hItemPos);
		JS_List_ClearIterationHandler(hItemPos);
		if(pPoolItemMaxPos) {
			nIsFromWarm = 1;
			JS_List_RemoveItem(p3Pool->hWarmList,pPoolItemMaxPos);
			pPoolItem = pPoolItemMaxPos;
		}else
			pPoolItem = NULL;
	}
	if(pPoolItem) {
		int nOldPhase = pPoolItem->nPhase;
		JS_3Pool_MaskSetOldPhase(pPoolItem,nOldPhase);
		nRet = JS_3Pool_LinkMutexToPoolItem(p3Pool,pPoolItem);
		if(nRet>=0) {
			nRet = p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_HOT);
			if(nRet>=0)
				pPoolItem->nPhase = JS_POOL_PHASE_HOT;
		}
		if(nRet<0){
			DBGPRINT("State function error can't make pool item\n");
			if(nIsFromWarm)
				JS_List_PushBack(p3Pool->hWarmList,pPoolItem);
			else
				JS_List_PushBack(p3Pool->hColdList,pPoolItem);
			pPoolItem = NULL;
		}
	}
	return pPoolItem;
}

int JS_3Pool_FinishInitItem(JS_HANDLE hPool, int nError, JS_POOL_ITEM_T * pPoolItem)
{
	int nRet = 0;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(nError==0)
		nRet = JS_HashMap_Add(p3Pool->hHotMap,pPoolItem);	
	else
		nRet = -1;
	if(nRet<0){
		DBGPRINT("Can't make pool item error\n");
		p3Pool->pfPhaseChange(p3Pool->pOwner,pPoolItem,JS_POOL_PHASE_COLD);
		pPoolItem->nPhase = JS_POOL_PHASE_COLD;
		JS_List_PushBack(p3Pool->hColdList,pPoolItem);
	}
	return 0;
}

int JS_3Pool_FreeItem(JS_HANDLE hPool, JS_POOL_ITEM_T * pItem, int nIsColdNow)
{
	int nRet = 0;
	int nNewPhase;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;

	if(hPool==NULL || pItem==NULL || pItem->nPhase != JS_POOL_PHASE_HOT)
		return -1;
	////check zombie
	if(pItem->nRefCnt>0) {
		JS_3Pool_MaskSetZombie(pItem,1);
		p3Pool->nZombieCount++;
		JS_List_PushBack(p3Pool->hZombieList,pItem);
		return 0;
	}
	if(JS_3Pool_MaskGetZombie(pItem)) {
		JS_3Pool_MaskSetZombie(pItem,0);
		JS_List_RemoveItem(p3Pool->hZombieList,pItem);
		p3Pool->nZombieCount--;
		if(p3Pool->nZombieCount<0)
			p3Pool->nZombieCount=0;
	}
	if(p3Pool->nLifeIntvalMS==0)
		nIsColdNow = 1;
	pItem->nHotTick = JS_UTIL_GetTickCount();
	if(nIsColdNow)
		nNewPhase = JS_POOL_PHASE_COLD;
	else
		nNewPhase = JS_POOL_PHASE_WARM;
	JS_HashMap_Remove(p3Pool->hHotMap,pItem);
	p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,nNewPhase);
	pItem->nPhase = nNewPhase;
	if(nIsColdNow)
		JS_List_PushBack(p3Pool->hColdList,pItem);
	else
		JS_List_PushBack(p3Pool->hWarmList,pItem);
	return nRet;
}

int JS_3Pool_ResetItem(JS_HANDLE hPool, JS_POOL_ITEM_T * pItem, int nIsColdReset)
{
	int nRet = 0;
	int nNewPhase;
	int nOldPhase;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;

	if(hPool==NULL || pItem==NULL)
		return -1;
	if(p3Pool->nLifeIntvalMS==0)
		nIsColdReset = 1;
	nOldPhase = pItem->nPhase;
	if(nIsColdReset)
		nNewPhase = JS_POOL_PHASE_COLD;
	else
		nNewPhase = JS_POOL_PHASE_WARM;
	p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,nNewPhase);
	pItem->nPhase = nNewPhase;
	nRet = p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,JS_POOL_PHASE_HOT);
	if(nRet<0 && nOldPhase!=JS_POOL_PHASE_COLD) {
		DBGPRINT("P3Pool: Wrong Reset error\n");
		if(nOldPhase==JS_POOL_PHASE_HOT)
			JS_HashMap_Remove(p3Pool->hHotMap,pItem);
		else if(nOldPhase==JS_POOL_PHASE_WARM)
			JS_List_RemoveItem(p3Pool->hWarmList,pItem);
		p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,JS_POOL_PHASE_COLD);
		pItem->nPhase = JS_POOL_PHASE_COLD;
		JS_List_PushBack(p3Pool->hColdList,pItem);
	}
	return nRet;
}

JS_HANDLE JS_GetHead(JS_HANDLE hPool, int nPhase)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(nPhase==JS_POOL_PHASE_HOT) 
		return p3Pool->hHotMap;
	else if(nPhase==JS_POOL_PHASE_WARM) 
		return p3Pool->hWarmList;
	else if(nPhase==JS_POOL_PHASE_COLD) 
		return p3Pool->hColdList;
	else
		return NULL;
}

JS_HANDLE JS_3Pool_CheckAndMakeIterationHandler(JS_HANDLE hPool, JS_HANDLE hIteration) 
{
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hIteration;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(hIteration == p3Pool->hHotMap) {
		pItItem = (JS_SimpleIterationItem *)JS_HashMap_MakeIterationHandler(p3Pool->hHotMap);
		if(pItItem) {
			pItItem->nStatus = JS_POOL_PHASE_HOT;
			pItItem->pRefPool = hPool;
			pItItem->nIndex = JS_POOL_PHASE_HOT;
		}
	}else if (hIteration == p3Pool->hWarmList) {
		pItItem = (JS_SimpleIterationItem *)JS_List_MakeIterationHandler(p3Pool->hWarmList,1);
		if(pItItem) {
			pItItem->nStatus = JS_POOL_PHASE_WARM;
			pItItem->pRefPool = hPool;
			pItItem->nIndex = JS_POOL_PHASE_WARM;
		}
	}else if(hIteration == p3Pool->hColdList) {
		pItItem = (JS_SimpleIterationItem *)JS_List_MakeIterationHandler(p3Pool->hColdList,1);
		if(pItItem) {
			pItItem->nStatus = JS_POOL_PHASE_COLD;
			pItItem->pRefPool = hPool;
			pItItem->nIndex = JS_POOL_PHASE_COLD;
		}
	}
	return pItItem;
}

void JS_3Pool_ClearIterationHandler(JS_HANDLE hPool, JS_HANDLE hItemPos)
{
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hItemPos;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(pItItem==NULL || p3Pool==NULL)
		return;
	if(pItItem->pRefPool == hPool) {
		if(pItItem->nIndex==JS_POOL_PHASE_HOT)
			JS_HashMap_ClearIterationHandler(p3Pool->hHotMap,hItemPos);
		else
			JS_List_ClearIterationHandler(hItemPos);
	}else
		JS_FREE(hItemPos);
}

JS_HANDLE  JS_3Pool_GetNext(JS_HANDLE hPool, JS_HANDLE hItemPos)
{
	JS_HANDLE hNextPos = NULL;
	JS_HANDLE hTempPos = NULL;
	JS_SimpleIterationItem * pItItem;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(hPool==NULL || hItemPos==NULL)
		return NULL;
	hTempPos = JS_3Pool_CheckAndMakeIterationHandler(hPool,hItemPos);
	if(hTempPos!=hItemPos)
		return hTempPos;
	pItItem = (JS_SimpleIterationItem *)hTempPos;
	if(pItItem->pRefPool != hPool) {
		JS_3Pool_ClearIterationHandler(hPool,hItemPos);
		return NULL;
	}
	if(pItItem->nIndex == JS_POOL_PHASE_HOT) {
		hNextPos = JS_HashMap_GetNext(p3Pool->hHotMap,hItemPos);
	}else {
		hNextPos = JS_List_GetNext(pItItem->pRefList,hItemPos);
	}
	return hNextPos;
}

JS_POOL_ITEM_T * JS_3Pool_GetDataFromIterateItem(JS_HANDLE hPool, JS_HANDLE hItemPos)
{
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hItemPos;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(p3Pool==NULL || pItItem==NULL || pItItem->pRefPool != hPool)
		return NULL;
	if(pItItem->nIndex == JS_POOL_PHASE_HOT)
		return (JS_POOL_ITEM_T *)JS_HashMap_GetDataFromIterateItem(p3Pool->hHotMap,hItemPos);
	else
		return (JS_POOL_ITEM_T *)JS_List_GetDataFromIterateItem(hItemPos);
}

int JS_3Pool_CheckStatus(JS_HANDLE hPool, UINT32 nMonitorCycle, JS_HANDLE hMutex)
{
	int nRet = 0;
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	JS_POOL_ITEM_T * pItem = NULL;
	JS_HANDLE	hPos = NULL;
	int nWarmSize;
	int nColdSize;
	UINT64 nCurTick;
	int nOldPhase;
	int nCnt;

	////check arguments
	if(hPool==NULL)
		return -1;
	nCurTick = JS_UTIL_GetTickCount();
	if(nMonitorCycle>0 && nCurTick<p3Pool->nLastCheckTick+nMonitorCycle)
		return 0;
	p3Pool->nLastCheckTick = nCurTick;
	nWarmSize = JS_List_GetSize(p3Pool->hWarmList);
	////traverse all idle item and make it cold if lifetime expired
	if(nWarmSize>0) {
		do {
			JS_UTIL_LockMutex(hMutex);
			hPos = JS_List_GetNext(p3Pool->hWarmList,hPos);
			if(hPos) {
				pItem = (JS_POOL_ITEM_T *)JS_List_GetDataFromIterateItem(hPos);
				if(nCurTick>pItem->nHotTick+p3Pool->nLifeIntvalMS) {
					////make it cold
					nOldPhase = pItem->nPhase;
					if(p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,JS_POOL_PHASE_COLD)==0) {
						pItem->nPhase = JS_POOL_PHASE_COLD;
						JS_List_PopPosition(p3Pool->hWarmList,hPos);
						JS_List_PushBack(p3Pool->hColdList,pItem);
					}
				}
			}
			JS_UTIL_UnlockMutex(hMutex);
		}while(hPos);
		JS_UTIL_LockMutex(hMutex);
		JS_List_ClearIterationHandler(hPos);
		JS_UTIL_UnlockMutex(hMutex);
	}
	////check zombie item
	if(p3Pool->nZombieCount>0) {
		do {
			JS_UTIL_LockMutex(hMutex);
			hPos = JS_List_GetNext(p3Pool->hZombieList,hPos);
			if(hPos) {
				pItem = (JS_POOL_ITEM_T *)JS_List_GetDataFromIterateItem(hPos);
				if(pItem->nRefCnt<=0) {
					////make it cold
					nOldPhase = pItem->nPhase;
					JS_3Pool_MaskSetZombie(pItem,0);
					JS_HashMap_Remove(p3Pool->hHotMap,pItem);
					JS_List_PopPosition(p3Pool->hZombieList,hPos);
					p3Pool->pfPhaseChange(p3Pool->pOwner,pItem,JS_POOL_PHASE_COLD);					
					pItem->nPhase = JS_POOL_PHASE_COLD;
					JS_List_PushBack(p3Pool->hColdList,pItem);
				}
			}
			JS_UTIL_UnlockMutex(hMutex);
		}while(hPos);
		JS_UTIL_LockMutex(hMutex);
		JS_List_ClearIterationHandler(hPos);
		JS_UTIL_UnlockMutex(hMutex);		
	}
	////if too many cold items, then delete a buffer from pool
	nColdSize = JS_List_GetSize(p3Pool->hColdList);
	if(p3Pool->nCurNum>p3Pool->nInitialNum && nColdSize>(p3Pool->nInitialNum*3/2)) {
		JS_UTIL_LockMutex(hMutex);
		////decrease counter
		p3Pool->nCurNum -= p3Pool->nInitialNum;
		JS_UTIL_UnlockMutex(hMutex);
		for(nCnt=0; nCnt<p3Pool->nInitialNum;nCnt++) {
			JS_UTIL_LockMutex(hMutex);
			pItem = (JS_POOL_ITEM_T *)JS_List_PopFront(p3Pool->hColdList);
			JS_UTIL_UnlockMutex(hMutex);
			if(pItem)
				JS_FREE(pItem);
		}
	}
	return nRet;
}

char * JS_3Pool_PrintPhase(int nPhase,char * strEnoughBuffer)
{
	if(strEnoughBuffer==NULL)
		return NULL;
	if(nPhase==JS_POOL_PHASE_COLD)
		JS_STRCPY(strEnoughBuffer,"ColdPhase");
	else if(nPhase==JS_POOL_PHASE_WARM)
		JS_STRCPY(strEnoughBuffer,"WarmPhase");
	else if(nPhase==JS_POOL_PHASE_HOT)
		JS_STRCPY(strEnoughBuffer,"HotPhase");
	else if(nPhase==JS_POOL_PHASE_UNKOWN)
		JS_STRCPY(strEnoughBuffer,"UnknownPhase");
	else
		JS_STRPRINTF(strEnoughBuffer,32,"Question:%d?",nPhase);
	return strEnoughBuffer;
}

JS_POOL_ITEM_T * JS_3Pool_FindItem(JS_HANDLE hPool, void * pCompData, JS_FT_COMPARE_ITEM_CALLBACK pfFind)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	if(p3Pool==NULL || pfFind==NULL)
		return NULL;
	if(p3Pool->pfHash != JS_3Pool_DefaultHash)
		return (JS_POOL_ITEM_T *)JS_HashMap_Find(p3Pool->hHotMap,pCompData,pfFind);
	else {
		JS_HANDLE	hItemPos = NULL;
		JS_POOL_ITEM_T * pPoolItem = NULL;
		do{
			hItemPos = JS_HashMap_GetNext(p3Pool->hHotMap,hItemPos);
			if(hItemPos) {
				pPoolItem = (JS_POOL_ITEM_T *)JS_HashMap_GetDataFromIterateItem(p3Pool->hHotMap,hItemPos);
				if(pPoolItem) {
					if(pfFind(p3Pool->pOwner,pPoolItem,pCompData)) {
						break;
					}else
						pPoolItem = NULL;
				}
			}
		}while(hItemPos);
		JS_HashMap_ClearIterationHandler(p3Pool->hHotMap,hItemPos);
		return pPoolItem;
	}
}

int JS_3Pool_SacrificeOldHotItem(JS_HANDLE hPool, int nNum)
{
	JS_3POOL_Structure * p3Pool = (JS_3POOL_Structure *)hPool;
	JS_POOL_ITEM_T * pMinPoolItem = NULL;
	JS_POOL_ITEM_T * pPoolItem = NULL;
	JS_HANDLE	hItemPos = NULL;
	int nCnt;
	UINT64 nMinHotTick;

	for(nCnt=0; nCnt<nNum; nCnt++) {
		hItemPos = NULL;
		nMinHotTick = (UINT64)-1;
		do{
			hItemPos = JS_HashMap_GetNext(p3Pool->hHotMap,hItemPos);
			if(hItemPos) {
				pPoolItem = (JS_POOL_ITEM_T *)JS_HashMap_GetDataFromIterateItem(p3Pool->hHotMap,hItemPos);
				if(pPoolItem->nHotTick < nMinHotTick) {
					nMinHotTick = pPoolItem->nHotTick;
					pMinPoolItem = pPoolItem;
				}
			}
		}while(hItemPos);
		JS_HashMap_ClearIterationHandler(p3Pool->hHotMap,hItemPos);
		if(pMinPoolItem)
			JS_3Pool_FreeItem(hPool,pMinPoolItem,0);
		else
			break;
	}
	return nCnt;
}

