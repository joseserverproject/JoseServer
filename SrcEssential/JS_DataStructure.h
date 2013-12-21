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

#ifndef JS_DATASTRUCTURE_H_
#define JS_DATASTRUCTURE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*JS_FT_RM_ITEM_CALLBACK) (void * pOwner, void * pData);
typedef int (*JS_FT_HASH_ITEM_CALLBACK) (void * pOwner, void * pDataItem, void * pKey);
typedef int (*JS_FT_COMPARE_ITEM_CALLBACK) (void * pOwner, void * pDataItem, void * pKey);
typedef void * (*JS_FT_GETKEY_CALLBACK) (void * pOwner, void * pDataItem);

///////////////////////////////////////////////////////////
////datastructure for thread safe iteration
typedef struct  JS_SimpleIterationItemTag{
	void * pPrev;
	void * pNext;
	void * pMe;
	void * pRefList;
	void * pRefMap;
	void * pRefPool;
	int nStatus;
	int nIndex;
	struct JS_SimpleIterationItemTag * pItNext;
}JS_SimpleIterationItem;

typedef struct JS_SimpleIterationTag{
	int nIterationNum;
	void * pRefList;
	JS_SimpleIterationItem * pThem;
}JS_SimpleIteration;

void JS_Iteration_Reset(JS_SimpleIteration * pItList, int nInit, void * pOwner);
void JS_Iteration_OnListRmItem(JS_SimpleIteration * pItList, JS_HANDLE hRmItem);
void JS_Iteration_RemoveIterationItem(JS_SimpleIteration * pItList, JS_SimpleIterationItem * pIt);
JS_SimpleIterationItem * JS_Iteration_AllocListIteration(JS_SimpleIteration * pItList, JS_HANDLE hItem);
////thread safe iteration end
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////
//linked list
JS_HANDLE JS_List_Create(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc);
void JS_List_SetRmFunc(JS_HANDLE hList, JS_FT_RM_ITEM_CALLBACK pRmFunc);
void JS_List_Reset(JS_HANDLE hList);
void JS_List_Destroy(JS_HANDLE hList);
void * JS_List_PopBack(JS_HANDLE hList);
void * JS_List_GetTail(JS_HANDLE hList);
int JS_List_PushBack(JS_HANDLE hList, void * pData);

void * JS_List_PopFront(JS_HANDLE hList);
void * JS_List_GetFront(JS_HANDLE hList);
int JS_List_PushFront(JS_HANDLE hList, void * pData);
int JS_List_RemoveItem(JS_HANDLE hList, void * pData);
void * JS_List_GetDataFromIterateItem(JS_HANDLE hItem);
JS_HANDLE JS_List_GetNext(JS_HANDLE hList, JS_HANDLE hItemPos);
JS_HANDLE JS_List_GetPrev(JS_HANDLE hList, JS_HANDLE hItemPos);
int JS_List_GetSize(JS_HANDLE hList);
void *  JS_List_PopPosition(JS_HANDLE hList, JS_HANDLE hIteration);
void JS_List_ClearIterationHandler(JS_HANDLE hItemPos);

JS_HANDLE JS_List_CreateEx(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc);
int JS_List_RemoveItemEx(JS_HANDLE hList, void * pKey, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc);
void * JS_List_PopItemEx(JS_HANDLE hList, void * pKey, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc, int nDonotRemove);
JS_HANDLE JS_List_IterateRaw(JS_HANDLE hList, JS_HANDLE hPosRaw, void ** ppData);
//////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
////hash map start
int JS_HashMap_DebugPerformance(JS_HANDLE hMap);
int JS_HashMap_CalculateHashValue(void * pKey, int nKeySize, int nIsString);
JS_HANDLE JS_HashMap_Create(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc, JS_FT_HASH_ITEM_CALLBACK pfHash, int nDefaultHashSpace, int nHasData);
int JS_HashMap_SetResizeOption(JS_HANDLE hMap, int nResizeMax, int nMinFactorPercent);
void JS_HashMap_Destroy(JS_HANDLE hMap);
int JS_HashMap_Add(JS_HANDLE hMap, void * pData);
int JS_HashMap_CheckAndAdd(JS_HANDLE hMap, void * pData,void * pKey, JS_FT_COMPARE_ITEM_CALLBACK  pfFind);
void JS_HashMap_Remove(JS_HANDLE hMap, void * pData);
void * JS_HashMap_Find(JS_HANDLE hMap, void * pKey, JS_FT_COMPARE_ITEM_CALLBACK  pfFind);
int JS_HashMap_GetSize(JS_HANDLE hMap);
JS_HANDLE JS_HashMap_GetNext(JS_HANDLE hMap, JS_HANDLE hItemPos);
void * JS_HashMap_GetDataFromIterateItem(JS_HANDLE hMap, JS_HANDLE hItemPos);
void JS_HashMap_ClearIterationHandler(JS_HANDLE hMap, JS_HANDLE hItemPos);
void JS_HashMap_RemoveEx(JS_HANDLE hMap, void * pKey,JS_FT_COMPARE_ITEM_CALLBACK  pfFind);

////simple cache functions
int JS_SimpleCache_SetHashMap(JS_HANDLE hMap, int nCacheSize, JS_FT_COMPARE_ITEM_CALLBACK pfFind);
int JS_SimpleCache_Add(JS_HANDLE hMap, void * pData);
void * JS_SimpleCache_Find(JS_HANDLE hMap, void * pKey);
void * JS_SimpleCache_GetDataFromIterateItem(JS_HANDLE hMap, JS_HANDLE hItemPos);
void JS_SimpleCache_RemoveEx(JS_HANDLE hMap, void * pKey);
void JS_SimpleCache_Destroy(JS_HANDLE hMap);
////hash map end
/////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////
//3 state pool
#define JS_POOL_PHASE_COLD		0
#define JS_POOL_PHASE_WARM		1
#define JS_POOL_PHASE_HOT		2
#define JS_POOL_PHASE_UNKOWN	0xF

#define JS_POOL_OPTION_STATIC	1

#define JS_POOL_RET_TOOSIMILAR	0xFFFFF

typedef struct JS_POOL_ITEM_TAG {
	int    nPhase;
	int	   nMask;
	int	   nRefCnt;
	UINT64 nHotTick;
	JS_HANDLE	hMutex;
	void * pMyData;
}JS_POOL_ITEM_T;

typedef int (*JS_FT_PHASECHANGE_CALLBACK) (void * pOwner, JS_POOL_ITEM_T * pItem, int nNewPhase);
typedef int (*JS_FT_TOSTRING_CALLBACK) (void * pOwner, JS_POOL_ITEM_T * pItem, char * pBuffer, int nLen);
typedef int (*JS_FT_FROMSTRING_CALLBACK) (void * pOwner, char * pBuffer, int nLen, JS_POOL_ITEM_T * pItem);
typedef UINT32 (*JS_FT_RESAMBLANCE_CALLBACK) (void * pOwner, JS_POOL_ITEM_T * pItem, void * pCompData);

JS_HANDLE JS_3Pool_Create(void * pOwner, int nInitialNumber, UINT32 nItemSize, UINT32 nLifeTime, JS_FT_PHASECHANGE_CALLBACK pPhaseFunc);
void JS_3Pool_AttachMutexArray(JS_HANDLE hPool, int nMaxMutex);
void JS_3Pool_Destroy(JS_HANDLE hPool);
void JS_3Pool_AttachMutexArray(JS_HANDLE hPool, int nMaxMutexes);
JS_POOL_ITEM_T * JS_3Pool_ActivateAnyFreeItem(JS_HANDLE hPool);
JS_POOL_ITEM_T * JS_3Pool_ActivateColdFreeItem(JS_HANDLE hPool);
JS_POOL_ITEM_T * JS_3Pool_ActivateSimliarWarmItem(JS_HANDLE hPool, JS_FT_RESAMBLANCE_CALLBACK pSimlilarFunc, void * pCompData);
int JS_3Pool_FinishInitItem(JS_HANDLE hPool, int nError, JS_POOL_ITEM_T * pItem);

int JS_3Pool_FreeItem(JS_HANDLE hPool, JS_POOL_ITEM_T * pItem, int nIsColdNow);
int JS_3Pool_ResetItem(JS_HANDLE hPool, JS_POOL_ITEM_T * pItem, int nIsColdReset);
int JS_3Pool_GetSize(JS_HANDLE hPool, int nPhase);

JS_HANDLE JS_GetHead(JS_HANDLE hPool, int nPhase);
void JS_3Pool_ClearIterationHandler(JS_HANDLE hPool, JS_HANDLE hItemPos);
JS_HANDLE  JS_3Pool_GetNext(JS_HANDLE hPool, JS_HANDLE hItemPos);
JS_POOL_ITEM_T * JS_3Pool_GetDataFromIterateItem(JS_HANDLE hPool, JS_HANDLE hItemPos);

int JS_3Pool_CheckStatus(JS_HANDLE hPool, UINT32 nMonitorCycle, JS_HANDLE hMutex);
char * JS_3Pool_PrintPhase(int nPhase,char * strEnoughBuffer);

JS_HANDLE JS_3Pool_CreateEx(void * pOwner, int nInitialNumber, UINT32 nItemSize, UINT32 nLifeTime, int nHashSpace, JS_FT_PHASECHANGE_CALLBACK pPhaseFunc,JS_FT_HASH_ITEM_CALLBACK pfHash);
JS_POOL_ITEM_T * JS_3Pool_FindItem(JS_HANDLE hPool, void * pKey, JS_FT_COMPARE_ITEM_CALLBACK pfFind);
int JS_3Pool_SacrificeOldHotItem(JS_HANDLE hPool, int nNum);

int JS_3Pool_MaskGetDataID(JS_POOL_ITEM_T * pPoolItem);
int JS_3Pool_MaskSetDataID(JS_POOL_ITEM_T * pPoolItem, int nDataID);

int JS_3Pool_MaskGetZombie(JS_POOL_ITEM_T * pPoolItem);
int JS_3Pool_MaskSetZombie(JS_POOL_ITEM_T * pPoolItem, int nEnable);

int JS_3Pool_SetOption(JS_HANDLE hPool, int nOption);
/////////////////////////////////////////////////
//simple queue
#define JS_Q_TYPE_SIMPLE	1
#define JS_Q_TYPE_MULTIQ	2
#define JS_Q_RET_FINISHED		0xFFFF
#define JS_Q_RET_NEEDTOCLOSE	0xFF01

int  JS_Q_GetType(JS_HANDLE hQ);
int  JS_Q_IsInput(JS_HANDLE hQ);
int  JS_Q_IsChunked(JS_HANDLE hQ);

int JS_SimpleQ_CheckAllRcvd(JS_HANDLE hQ);
int JS_SimpleQ_CheckAllDone(JS_HANDLE hQ);
JS_HANDLE JS_SimpleQ_Create(int nIsChunked, int nIsInput);
void JS_SimpleQ_Destroy(JS_HANDLE hQ);
void JS_SimpleQ_Reset(JS_HANDLE hQ);
void JS_SimpleQ_ResetTotallSize(JS_HANDLE hQ, HTTPSIZE_T nOriginalLen);

void JS_SimpleQ_SetChunkedTransferDecoding(JS_HANDLE hQ, int nEnable);
void JS_SimpleQ_SetChunkedTransferBypass(JS_HANDLE hQ, int nEnable);

int JS_SimpleQ_CheckAvailableData(JS_HANDLE hQ);
JSUINT JS_SimpleQ_GetDataSize(JS_HANDLE hQ);
HTTPSIZE_T JS_SimpleQ_GetTotalRcvd(JS_HANDLE hQ);
HTTPSIZE_T JS_SimpleQ_GetTotalSent(JS_HANDLE hQ);

int JS_SimpleQ_PushPumpIn(JS_HANDLE hQ, const char * pData, JSUINT nSize);
char * JS_SimpleQ_PreparePumpOut(JS_HANDLE hQ, JSUINT nNeedSize, JSUINT * pnBuffSize, const char * strPattern, int nPattern, int * pnFound);
int JS_SimpleQ_FinishPumpOut(JS_HANDLE hQ, JSUINT nLoadedData);

#ifdef __cplusplus
}
#endif
#endif /* JS_DATASTRUCTURE_H_ */


