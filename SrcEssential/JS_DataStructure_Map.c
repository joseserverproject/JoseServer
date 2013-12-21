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

#include <math.h>

/*********************************************************************
simple hash map which has below fetures
1. fast add, fast remove
2. provide simple cache function whose size is fixed
3. can be tested in unittestapp
*********************************************************************/

//////////////////////////////////////////////////
///local types
typedef struct JS_HashEntryTag {
	void * pData;
	void * pItemPos;
}JS_HashEntry;

typedef struct JS_HashMapTag {
	/////fields for hashmap
	void * pOwner;
	int nMapNum;
	int	nHashSpace;
	int nHasData;
	JS_HANDLE	hAllList;
	JS_HANDLE	* pHashLists;
	JS_FT_HASH_ITEM_CALLBACK	pfHash;
	JS_FT_RM_ITEM_CALLBACK		pfRemove;
	/////fields for simple cache
	int nCacheSize;
	UINT64 nAvgTick;
	void * pOldOwnerForSimpleCache;
	JS_FT_HASH_ITEM_CALLBACK	pfOldHashForSimpleCache;
	JS_FT_RM_ITEM_CALLBACK		pfOldRemoveForSimpleCache;
	JS_FT_COMPARE_ITEM_CALLBACK  pfOldFindForSimpleCache;
}JS_HashMap;

//////////////////////////////////////////////////
///extern functions
extern JS_HANDLE JS_List_MakeIterationHandler(JS_HANDLE hList, int nIsFront);
extern void * JS_List_PushBackForMap(JS_HANDLE hList, void * pData);
extern void JS_List_RemoveForMap(JS_HANDLE hList, void * pListItem);
extern void * JS_List_PopForMap(JS_HANDLE hList, void * pData, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc, int nDonotRemove, int nForMap);

//////////////////////////////////////////////////
///function implementations
void * JS_HashMap_GetEntryData(void * pParam)
{
	JS_HashEntry * pEntry = (JS_HashEntry *)pParam;
	if(pEntry==NULL)
		return NULL;
	return pEntry->pData;
}
int JS_HashMap_CalculateHashValue(void * pKey, int nKeySize, int nIsString)
{
	int nRet = 0;
    int nChar;
	int nCnt;
	unsigned long nHash = 5381;
	const char * pStr = (const char*)pKey;
	if(pKey==NULL)
		return -1;
	nCnt = 0;
    while (1) {
    	nChar = pStr[nCnt];
    	if(nIsString && nChar==0)
    		break;
    	nHash = ((nHash << 5) + nHash) + nChar;
		nCnt++;
		if(nKeySize != 0 && nCnt>=nKeySize)
			break;
    }
    nRet = nHash;
    if(nRet<0)
    	nRet = -nRet;
	return nRet;

}

int JS_HashMap_DebugPerformance(JS_HANDLE hMap)
{
	int nTotal;
	int nAvg;
	int nDiff;
	int nCnt;
	int nMax=0;
	int nMin=0xFFFFFFF;
	double dbVar;

	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL||pHashMap->nMapNum<=0)
		return 0;
	nTotal = pHashMap->nMapNum;
	nAvg = nTotal/pHashMap->nHashSpace;
	dbVar = 0.0;
	for(nCnt=0; nCnt<pHashMap->nHashSpace; nCnt++) {
		nDiff= JS_List_GetSize(pHashMap->pHashLists[nCnt]);
		if(nDiff<0)
			nDiff = nDiff;
		if(nDiff>nMax)
			nMax = nDiff;
		if(nDiff<nMin)
			nMin = nDiff;
		nDiff = nDiff-nAvg;
		dbVar += nDiff*nDiff;
	}
	dbVar = dbVar/pHashMap->nHashSpace;
	dbVar = sqrt(dbVar);
	printf("Hash List Size: avg=%u, max=%u, min=%u stdev=%f\n",nAvg,nMax,nMin,dbVar);
	nAvg = nAvg*100/(nMax-nMin);
	return nAvg;
}

int JS_HashMap_SetResizeOption(JS_HANDLE hMap, int nResizeMax, int nMinFactorPercent)
{
	int nRet = 0;
	////TBD
	return nRet;
}

JS_HANDLE JS_HashMap_Create(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc, JS_FT_HASH_ITEM_CALLBACK pfHash, int nDefaultHashSpace, int nHasData) 
{
	int nRet = 0;
	int nCnt;
	JS_HANDLE hList;
	JS_HashMap * pHashMap = NULL;
	pHashMap = (JS_HashMap *)JS_ALLOC(sizeof(JS_HashMap));
	if(pHashMap==NULL) {
		DBGPRINT("HashMapCreate: mem error1\n");
		return NULL;
	}
	if(nDefaultHashSpace==0)
		nDefaultHashSpace = JS_CONFIG_NORMAL_HASHSIZE;
	memset((char*)pHashMap,0,sizeof(JS_HashMap));
	pHashMap->pOwner = pOwner;
	pHashMap->pfRemove = pRmFunc;
	pHashMap->pfHash = pfHash;
	pHashMap->nMapNum = 0;
	pHashMap->nHashSpace = nDefaultHashSpace;
	if(nHasData) {
		pHashMap->nHasData = 1;
		pHashMap->hAllList = JS_List_Create(pHashMap,pRmFunc);
		if(pHashMap->hAllList==NULL) {
			DBGPRINT("HashMapCreate:mem error2\n");
			nRet = -1;
			goto LABEL_EXIT_HASHMAPCREATE;
		}
	}else {
		pHashMap->nHasData = 0;
		pHashMap->hAllList = NULL;
	}

	pHashMap->pHashLists = (JS_HANDLE*)JS_ALLOC(sizeof(JS_HANDLE)*nDefaultHashSpace);
	if(pHashMap->pHashLists==NULL) {
		DBGPRINT("HashMapCreate:mem error3\n");
		nRet = -1;
		goto LABEL_EXIT_HASHMAPCREATE;
	}
	for(nCnt=0; nCnt<nDefaultHashSpace; nCnt++) {
		hList = JS_List_Create(pHashMap,NULL);
		if(hList==NULL) {
			DBGPRINT("HashMapCreate:list create mem error\n");
			nRet = -1;
			goto LABEL_EXIT_HASHMAPCREATE;
		}
		pHashMap->pHashLists[nCnt] = hList;
	}
LABEL_EXIT_HASHMAPCREATE:
	if(nRet<0 && pHashMap) {
		if(pHashMap->hAllList)
			JS_List_Destroy(pHashMap->hAllList);
		if(pHashMap->pHashLists) {
			for(nCnt=0; nCnt<nDefaultHashSpace; nCnt++) {
				hList = pHashMap->pHashLists[nCnt];
				if(hList)
				  JS_List_Destroy(hList);
			}
			JS_FREE(pHashMap->pHashLists);
			pHashMap->pHashLists = NULL;
		}
		JS_FREE(pHashMap);
		pHashMap = NULL;
	}
	return (JS_HANDLE)pHashMap;
}

void JS_HashMap_Destroy(JS_HANDLE hMap)
{
	int nCnt;
	JS_HANDLE hList;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL)
		return;
	for(nCnt=0; nCnt<pHashMap->nHashSpace; nCnt++) {
		hList = pHashMap->pHashLists[nCnt];
		if(hList)
			JS_List_Destroy(hList);
	}
	if(pHashMap->hAllList)
		JS_List_Destroy(pHashMap->hAllList);
	JS_FREE(pHashMap->pHashLists);
	JS_FREE(pHashMap);
}

int JS_HashMap_CheckAndAdd(JS_HANDLE hMap, void * pData,void * pKey, JS_FT_COMPARE_ITEM_CALLBACK  pfFind)
{
	if(JS_HashMap_Find(hMap,pKey,pfFind))
		return -1;
	return JS_HashMap_Add(hMap,pData);
}

int JS_HashMap_Add(JS_HANDLE hMap, void * pData)
{
	int nRet=0;
	JS_HashEntry * pEntry = NULL;
	void * pItemPos=NULL;
	UINT32 nIndex;
	JS_HANDLE hList;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL || pHashMap->pHashLists==NULL)
		return -1;
	if(pHashMap->pHashLists && pHashMap->pfHash) {
		nIndex = pHashMap->pfHash(pHashMap->pOwner, pData, NULL);
		nIndex = nIndex%pHashMap->nHashSpace;
		hList = pHashMap->pHashLists[nIndex];
	}else {
		nIndex = 0;
		hList = pHashMap->pHashLists[nIndex];
	}
	if(pHashMap->hAllList) {
		pItemPos = JS_List_PushBackForMap(pHashMap->hAllList,pData);
		if(pItemPos==NULL)
			return -1;
	}
	pEntry = (JS_HashEntry*)JS_ALLOC(sizeof(JS_HashEntry));
	if(pEntry==NULL)
		goto HASHMAP_ADD_ERROR;
	pEntry->pData = pData;
	pEntry->pItemPos = pItemPos;
	nRet = JS_List_PushBack(hList,pEntry);
	if(nRet<0)
		goto HASHMAP_ADD_ERROR;
	pHashMap->nMapNum++;
	return nRet;
HASHMAP_ADD_ERROR:
	if(pEntry)
		JS_FREE(pEntry);
	JS_List_RemoveItem(pHashMap->hAllList,pData);
	return nRet;
}

void JS_HashMap_RemoveEx(JS_HANDLE hMap, void * pKey,JS_FT_COMPARE_ITEM_CALLBACK  pfFind)
{
	JS_HashEntry * pEntry = NULL;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	UINT32 nIndex;
	JS_HANDLE hList;
	if(pHashMap==NULL||pfFind==NULL)
		return;
	if(pHashMap->pHashLists && pHashMap->pfHash) {
		nIndex = pHashMap->pfHash(pHashMap->pOwner, NULL, pKey);
		nIndex = nIndex%pHashMap->nHashSpace;
		hList = pHashMap->pHashLists[nIndex];
	}else {
		nIndex = 0;
		hList = pHashMap->pHashLists[nIndex];
	}
	pEntry = (JS_HashEntry *)JS_List_PopForMap(hList,pKey,pfFind,0,1);
	if(pEntry) {
		if(pHashMap->nMapNum>0)
			pHashMap->nMapNum--;
		if(pHashMap->hAllList) {
			JS_List_RemoveForMap(pHashMap->hAllList,pEntry->pItemPos);
		}
		JS_FREE(pEntry);
	}
}

void JS_HashMap_Remove(JS_HANDLE hMap, void * pData)
{
	JS_HashEntry * pEntry = NULL;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	UINT32 nIndex;
	JS_HANDLE hList;
	if(pHashMap==NULL)
		return;
	if(pHashMap->pHashLists && pHashMap->pfHash) {
		nIndex = pHashMap->pfHash(pHashMap->pOwner, pData, NULL);
		nIndex = nIndex%pHashMap->nHashSpace;
		hList = pHashMap->pHashLists[nIndex];
	}else {
		nIndex = 0;
		hList = pHashMap->pHashLists[nIndex];
	}
	pEntry = (JS_HashEntry *)JS_List_PopForMap(hList,pData,NULL,0,1);
	if(pEntry) {
		if(pHashMap->nMapNum>0)
			pHashMap->nMapNum--;
		if(pHashMap->hAllList) {
			JS_List_RemoveForMap(pHashMap->hAllList,pEntry->pItemPos);
		}
		JS_FREE(pEntry);
	}
}

void * JS_HashMap_Find(JS_HANDLE hMap, void * pKey, JS_FT_COMPARE_ITEM_CALLBACK  pfFind)
{
	UINT32 nIndex;
	void * pData = NULL;
	JS_HANDLE hList;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	JS_HashEntry * pEntry = NULL;

	if(pHashMap==NULL || pHashMap->pHashLists==NULL || pHashMap->nMapNum<=0 || pfFind==NULL)
		return NULL;
	if(pHashMap->pHashLists && pHashMap->pfHash) {
		nIndex = pHashMap->pfHash(pHashMap->pOwner, NULL, pKey);
		nIndex = nIndex%pHashMap->nHashSpace;
		hList = pHashMap->pHashLists[nIndex];
	}else {
		nIndex = 0;
		hList = pHashMap->pHashLists[nIndex];
	}
	if(hList) {
		pEntry = (JS_HashEntry *)JS_List_PopForMap(hList,pKey,pfFind,1,1);
	}
	if(pEntry)
		return pEntry->pData;
	else
		return NULL;
}

int JS_HashMap_GetSize(JS_HANDLE hMap)
{
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap) {
		return pHashMap->nMapNum;
	}else
		return 0;
}

JS_HANDLE JS_HashMap_MakeIterationHandler(JS_HANDLE hMap)
{
	JS_HashMap * pHashMap = (JS_HashMap *)hMap;
	JS_SimpleIterationItem * pItItem;	
	if(pHashMap->nMapNum<=0 || pHashMap->hAllList==NULL)
		return NULL;
	pItItem = (JS_SimpleIterationItem *)JS_List_MakeIterationHandler(pHashMap->hAllList,1);
	if(pItItem) {
		pItItem->pRefMap = hMap;
		return pItItem;
	}else
		return NULL;
}

JS_HANDLE JS_HashMap_GetNext(JS_HANDLE hMap, JS_HANDLE hItemPos)
{
	JS_HANDLE hNextPos=NULL;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hItemPos;
	if(pItItem==NULL) {
		return JS_HashMap_MakeIterationHandler(hMap);
	}else {
		if(pItItem->pRefMap != hMap)
			goto LABEL_CLEAR_ITERATION;
		else {
			hNextPos = JS_List_GetNext(pHashMap->hAllList,hItemPos);
		}
	}
	return hNextPos;
LABEL_CLEAR_ITERATION:
	if(pItItem)
		JS_HashMap_ClearIterationHandler(hMap, (JS_HANDLE)pItItem);
	return NULL;
}


void * JS_HashMap_GetDataFromIterateItem(JS_HANDLE hMap, JS_HANDLE hItemPos)
{
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hItemPos;
	if(pHashMap==NULL || pItItem==NULL || pItItem->pRefMap != hMap)
		return NULL;
	return JS_List_GetDataFromIterateItem(hItemPos);
}


void JS_HashMap_ClearIterationHandler(JS_HANDLE hMap, JS_HANDLE hItemPos)
{
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	JS_SimpleIterationItem * pItItem = (JS_SimpleIterationItem *)hItemPos;
	if(pItItem==NULL)
		return;
	if(pHashMap==NULL || pItItem->pRefMap != hMap) {
		JS_FREE(pItItem);
	}else {
		JS_List_ClearIterationHandler(hItemPos);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
////simple cache functions
typedef struct JS_SimpleCacheEntryTag{
	UINT64 nTick;
	void * pUserData;
}JS_SimpleCacheEntry;

static int JS_SimpleCache_CheckMap(JS_HashMap * pHashMap)
{
	int nCnt;
	int nHalf;
	JS_SimpleCacheEntry * pItem;
	JS_HANDLE hItemPos=NULL;
	if(pHashMap->nMapNum>=pHashMap->nCacheSize) {
		////sacrifice old mem data
		nCnt = 0;
		nHalf = pHashMap->nMapNum/2;
		while(1) {
			hItemPos = JS_List_GetNext(pHashMap->hAllList,hItemPos);
			if(hItemPos==NULL)
				break;
			pItem = (JS_SimpleCacheEntry *) JS_List_GetDataFromIterateItem(hItemPos);
			if(pItem) {
				////delete if lower than avg
				if(pItem->nTick < pHashMap->nAvgTick) {
					JS_HashMap_Remove(pHashMap,pItem);
					nCnt++;
				}
			}
			if(nCnt>=nHalf)
				break;
		}
		JS_List_ClearIterationHandler(hItemPos);
	}
	return 0;
}

static int JS_SimpleCache_HashHook (void * pOwner, void * pData, void * pKey)
{
	int nRet = 0;
	JS_HashMap * pHashMap = (JS_HashMap*)pOwner;
	JS_SimpleCacheEntry * pItem;
	if(pData != NULL) {
		pItem = (JS_SimpleCacheEntry *)pData;
		nRet = pHashMap->pfOldHashForSimpleCache(pHashMap->pOldOwnerForSimpleCache,pItem->pUserData,NULL);
	}else {
		nRet = pHashMap->pfOldHashForSimpleCache(pHashMap->pOldOwnerForSimpleCache,NULL,pKey);
	}
	return nRet;
}

static int JS_SimpleCache_FindHook (void * pOwner, void * pData, void * pKey)
{
	int nRet=0;
	JS_HashMap * pHashMap = (JS_HashMap*)pOwner;
	JS_SimpleCacheEntry * pItem = (JS_SimpleCacheEntry *)pData;
	nRet = pHashMap->pfOldFindForSimpleCache(pHashMap->pOldOwnerForSimpleCache,pItem->pUserData,pKey);
	if(nRet==1) {
		///update cache hit counter
		pItem->nTick = JS_UTIL_GetSecondsFrom1970();
		pHashMap->nAvgTick = (pHashMap->nAvgTick>>1)+(pItem->nTick>>1);
	}
	return nRet;
}

static int JS_SimpleCache_RmHook (void * pOwner, void * pData)
{
	int nRet = 0;
	JS_HashMap * pHashMap = (JS_HashMap*)pOwner;
	JS_SimpleCacheEntry * pItem = (JS_SimpleCacheEntry *)pData;
	if(pHashMap->pfOldRemoveForSimpleCache)
		nRet = pHashMap->pfOldRemoveForSimpleCache(pHashMap->pOldOwnerForSimpleCache,pItem->pUserData);
	else
		JS_FREE(pItem->pUserData);
	JS_FREE(pItem);
	return nRet;
}

int JS_SimpleCache_SetHashMap(JS_HANDLE hMap, int nMaxSize, JS_FT_COMPARE_ITEM_CALLBACK pfFind)
{
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL || nMaxSize<=0)
		return -1;
	pHashMap->nCacheSize = nMaxSize;
	pHashMap->pOldOwnerForSimpleCache = pHashMap->pOwner;
	pHashMap->pfOldHashForSimpleCache = pHashMap->pfHash;
	pHashMap->pfOldRemoveForSimpleCache = pHashMap->pfRemove;
	pHashMap->pfOldFindForSimpleCache = pfFind;
	pHashMap->pOwner = hMap;
	pHashMap->pfHash = JS_SimpleCache_HashHook;
	pHashMap->pfRemove = JS_SimpleCache_RmHook;
	JS_List_SetRmFunc(pHashMap->hAllList,JS_SimpleCache_RmHook);
	pHashMap->nAvgTick = JS_UTIL_GetSecondsFrom1970();
	return 0;
}
void JS_SimpleCache_RemoveEx(JS_HANDLE hMap, void * pKey)
{
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL)
		return;
	JS_HashMap_RemoveEx(hMap,pKey,pHashMap->pfOldFindForSimpleCache);
}

int JS_SimpleCache_Add(JS_HANDLE hMap, void * pData)
{
	int nRet = 0;
	JS_SimpleCacheEntry * pItem=NULL;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	if(pHashMap==NULL || pData==NULL)
		return -1;
	JS_SimpleCache_CheckMap(pHashMap);
	pItem = (JS_SimpleCacheEntry *)JS_ALLOC(sizeof(JS_SimpleCacheEntry));
	if(pItem==NULL) {
		DBGPRINT("simplecache:mem error(pitem)\n");
		nRet = -1;
		goto LABEL_CATCH_ERROR;
	}
	pItem->pUserData = pData;
	pItem->nTick = JS_UTIL_GetSecondsFrom1970();
	nRet = JS_HashMap_Add(hMap,pItem);
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pItem)
			JS_FREE(pItem);
	}else {
		pHashMap->nAvgTick = (pHashMap->nAvgTick>>1)+(pItem->nTick>>1);
	}
	return nRet;
}

void * JS_SimpleCache_Find(JS_HANDLE hMap, void * pKey)
{
	JS_SimpleCacheEntry * pItem=NULL;
	JS_HashMap * pHashMap = (JS_HashMap*)hMap;
	pItem = (JS_SimpleCacheEntry *) JS_HashMap_Find(hMap,pKey,JS_SimpleCache_FindHook);
	if(pItem)
		return pItem->pUserData;
	else
		return NULL;
}

void * JS_SimpleCache_GetDataFromIterateItem(JS_HANDLE hMap, JS_HANDLE hItemPos)
{
	JS_SimpleCacheEntry * pItem=NULL;
	pItem = (JS_SimpleCacheEntry *)JS_HashMap_GetDataFromIterateItem(hMap,hItemPos);
	if(pItem)
		return pItem->pUserData;
	else
		return NULL;
}

void JS_SimpleCache_Destroy(JS_HANDLE hMap)
{

}