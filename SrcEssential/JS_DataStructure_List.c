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
simple linked list which has below fetures
1. bidirection link
2. fast iteration among threads
3. can be tested in unittestapp
*********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"

//////////////////////////////////////////////////
///local type
typedef struct  JS_SimpleListItemTag{
	void * pData;
	struct JS_SimpleListItemTag * pPrev;
	struct JS_SimpleListItemTag * pNext;
}JS_SimpleListItem;

typedef struct  JS_SimpleListTag {
	int	nNum;	
	void * pOwner;
	JS_SimpleListItem * pHead;
	JS_SimpleListItem * pTail;
	JS_SimpleIteration rcIt;
	JS_FT_RM_ITEM_CALLBACK	pFuncDelete;
	JS_FT_COMPARE_ITEM_CALLBACK pFuncFind;
}JS_SimpleList;

extern void * JS_HashMap_GetEntryData(void * pParam);

//////////////////////////////////////////////////
///function implementations
void JS_Iteration_Reset(JS_SimpleIteration * pItList, int nInit, void * pRefList)
{
	JS_SimpleIterationItem * pIt = NULL;
	if(pItList==NULL)
		return;
	if(nInit==0 && pItList->nIterationNum>0) {
		pIt = pItList->pThem;
		while(pIt != NULL) {
			pIt->pPrev = NULL;
			pIt->pNext = NULL;
			pIt->pMe = NULL;
			pIt = pIt->pItNext;
			////delete memory should be done in the iteration thread
		}
	}
	pItList->nIterationNum = 0;
	pItList->pThem = NULL;
	pItList->pRefList = pRefList;
}

void JS_Iteration_OnListRmItem(JS_SimpleIteration * pItList, JS_HANDLE hRmItem)
{
	JS_SimpleListItem * pRmItem = (JS_SimpleListItem *)hRmItem;
	JS_SimpleIterationItem * pIt = NULL;
	if(pItList->nIterationNum<=0 || pItList==NULL || hRmItem==NULL)
		return;
	//printf("IterNum = %d\n",pItList->nIterationNum);
	pIt = pItList->pThem;
	while(pIt != NULL) {
		if(pIt->pPrev == pRmItem)
			pIt->pPrev = pRmItem->pPrev;
		else if(pIt->pNext == pRmItem)
			pIt->pNext = pRmItem->pNext;
		else if(pIt->pMe == pRmItem)
			pIt->pMe = NULL;
		pIt = pIt->pItNext;
	}
}

void JS_Iteration_RemoveIterationItem(JS_SimpleIteration * pItList, JS_SimpleIterationItem * pIt)
{
	JS_SimpleIterationItem * pTmpPrev = NULL;
	JS_SimpleIterationItem * pTmpIt = NULL;
	if(pItList==NULL || pIt==NULL)
		return;
	pTmpIt = pItList->pThem;
	while(pTmpIt != NULL) {
		if(pTmpIt==pIt) {
			if(pItList->nIterationNum>0)
				pItList->nIterationNum--;
			if(pTmpPrev==NULL)
				pItList->pThem = pTmpIt->pItNext;
			else
				pTmpPrev->pItNext =	pTmpIt->pItNext;
			break;
		}
		pTmpPrev = pTmpIt;
		pTmpIt = pTmpIt->pItNext;
	}
	JS_FREE(pIt);
}

JS_SimpleIterationItem * JS_Iteration_AllocListIteration(JS_SimpleIteration * pItList, JS_HANDLE hItem)
{
	JS_SimpleListItem * pItem = (JS_SimpleListItem *)hItem;
	JS_SimpleIterationItem * pIt = NULL;
	if(pItList==NULL || hItem==NULL)
		return NULL;
	pIt = (JS_SimpleIterationItem *)JS_ALLOC(sizeof(JS_SimpleIterationItem));
	if(pIt==NULL)
		return NULL;
	pIt->pMe = pItem;
	pIt->pNext = pItem->pNext;
	pIt->pPrev = pItem->pPrev;
	pIt->nIndex = 0;
	pIt->nStatus = 0;
	pIt->pItNext = NULL;
	pIt->pRefList = pItList->pRefList;
	if(pItList->pThem == NULL) {
		pItList->pThem = pIt;
	}else {
		pIt->pItNext = pItList->pThem;
		pItList->pThem = pIt;
	}		 
	pItList->nIterationNum++;
	return pIt;
}

JS_HANDLE JS_List_CreateEx(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc)
{
	JS_HANDLE hList = JS_List_Create(pOwner,pRmFunc);
	if(hList) {
		JS_SimpleList * pList = (JS_SimpleList *)hList;
		pList->pFuncFind = pFindFunc;
		return (JS_HANDLE)pList;
	}else
		return NULL;	
}

JS_HANDLE JS_List_Create(void * pOwner, JS_FT_RM_ITEM_CALLBACK pRmFunc)
{
	JS_SimpleList * pList;
	pList = (JS_SimpleList *)JS_ALLOC(sizeof(JS_SimpleList));
	pList->pFuncDelete = pRmFunc;
	pList->pFuncFind = NULL;
	pList->nNum = 0;
	pList->pHead = NULL;
	pList->pTail = NULL;
	JS_Iteration_Reset(&(pList->rcIt),1,(void*)pList);
	pList->pOwner = pOwner;
	return (JS_HANDLE)pList;
}

void JS_List_SetRmFunc(JS_HANDLE hList, JS_FT_RM_ITEM_CALLBACK pRmFunc)
{
	JS_SimpleList * pList = (JS_SimpleList * )hList;
	pList->pFuncDelete = pRmFunc;
}

void JS_List_Reset(JS_HANDLE hList)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleListItem * pTmp = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	pItem = pList->pHead;
	while(pItem != NULL) {
		if(pList->pFuncDelete)
			pList->pFuncDelete(pList->pOwner,pItem->pData);
		else
			JS_FREE(pItem->pData);
		pTmp = pItem;
		pItem = pItem->pNext;
		JS_FREE(pTmp);
	}
	pList->nNum = 0;
	pList->pHead = NULL;
	pList->pTail = NULL;

	JS_Iteration_Reset(&(pList->rcIt),0,(void*)pList);
}

void JS_List_Destroy(JS_HANDLE hList)
{
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	JS_List_Reset(hList);
	JS_FREE(pList);
}

void * JS_List_PushBackForMap(JS_HANDLE hList, void * pData)
{
	int nRet = 0;
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	pItem = (JS_SimpleListItem *) JS_ALLOC(sizeof(JS_SimpleListItem));
	if(pItem==NULL) {
		//DBGPRINT("JSLIST: no JS_ALLOC item error\n");
		return NULL;
	}
	pItem->pNext = NULL;
	pItem->pPrev = NULL;
	pItem->pData = pData;
	if(pList->pTail==NULL) {
		pList->pHead = pItem;
		pList->pTail = pItem;
	}else {
		pList->pTail->pNext = pItem;
		pItem->pPrev = pList->pTail;
		pList->pTail = pItem;
	}
	pList->nNum++;
	return pItem;
}

int JS_List_PushBack(JS_HANDLE hList, void * pData)
{
	if(JS_List_PushBackForMap(hList,pData)==NULL)
		return -1;
	else
		return 0;
}

void * JS_List_PopBack(JS_HANDLE hList)
{
	void * pData;
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	if(pList==NULL || pList->pTail==NULL) {
//		DBGPRINT("JSLIST: no item error\n");
		return NULL;
	}
	pItem = pList->pTail;
	pData = pItem->pData;
	pList->pTail = pItem->pPrev;
	if(pList->pTail)
		pList->pTail->pNext = NULL;
	else {
		pList->pHead = NULL;
	}
	if(pList->nNum>0)
		pList->nNum--;
	JS_Iteration_OnListRmItem(&(pList->rcIt),pItem);
	JS_FREE(pItem);
	return pData;
}

void * JS_List_PopFront(JS_HANDLE hList)
{
	void * pData;
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	if(pList==NULL || pList->pHead==NULL) {
//		DBGPRINT("JSLIST: no item error\n");
		return NULL;
	}
	pItem = pList->pHead;
	pData = pItem->pData;
	pList->pHead = pItem->pNext;
	if(pList->pHead)
		pList->pHead->pPrev = NULL;
	else {
		pList->pTail = NULL;
	}
	if(pList->nNum>0)
		pList->nNum--;
	JS_Iteration_OnListRmItem(&(pList->rcIt),pItem);
	JS_FREE(pItem);
	return pData;
}

void * JS_List_GetFront(JS_HANDLE hList)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	if(pList==NULL || pList->pHead==NULL) {
		return NULL;
	}
	pItem = pList->pHead;
	return pItem->pData;
}

void * JS_List_GetTail(JS_HANDLE hList)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	if(pList==NULL || pList->pTail==NULL) {
		return NULL;
	}
	pItem = pList->pTail;
	return pItem->pData;
}

JS_HANDLE JS_List_IterateRaw(JS_HANDLE hList, JS_HANDLE hPosRaw, void ** ppData)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	if(hPosRaw==NULL) {
		pItem = pList->pHead;
		if(pItem)
			*ppData = pItem->pData;
	}else {
		pItem = (JS_SimpleListItem *)hPosRaw;
		pItem = pItem->pNext;
		if(pItem) {
			*ppData = pItem->pData;
		}
	}
	return pItem;
}

int JS_List_PushFront(JS_HANDLE hList, void * pData)
{
	int nRet = 0;
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;

	pItem = (JS_SimpleListItem *) JS_ALLOC(sizeof(JS_SimpleListItem));
	if(pItem==NULL) {
		//DBGPRINT("JSLIST: no JS_ALLOC item error\n");
		return -1;
	}
	pItem->pNext = NULL;
	pItem->pPrev = NULL;
	pItem->pData = pData;
	if(pList->pHead==NULL) {
		pList->pHead = pItem;
		pList->pTail = pItem;
	}else {
		pList->pHead->pPrev = pItem;
		pItem->pNext = pList->pHead;
		pList->pHead = pItem;
	}
	pList->nNum++;
	return nRet;
}

int JS_List_RemoveItem(JS_HANDLE hList, void * pData)
{
	return JS_List_RemoveItemEx(hList,pData,NULL);
}

void JS_List_RemoveForMap(JS_HANDLE hList, void * pListItem)
{
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	JS_SimpleListItem * pItem = (JS_SimpleListItem *)pListItem;

	if(pList->pFuncDelete)
		pList->pFuncDelete(pList->pOwner,pItem->pData);
	else
		JS_FREE(pItem->pData);

	if(pItem->pPrev)
		pItem->pPrev->pNext = pItem->pNext;
	if(pItem->pNext)
		pItem->pNext->pPrev = pItem->pPrev;
	if(pItem==pList->pHead)
		pList->pHead = pItem->pNext;
	if(pItem==pList->pTail)
		pList->pTail = pItem->pPrev;
	JS_Iteration_OnListRmItem(&(pList->rcIt),pItem);
	JS_FREE(pItem);
	if(pList->nNum>0)
		pList->nNum--;
}

void * JS_List_PopForMap(JS_HANDLE hList, void * pData, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc, int nDonotRemove, int nForMap)
{
	int nFound = 0;
	void * pOrgData=NULL;
	void * pDataFound=NULL;
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	pItem = pList->pHead;
	while(pItem!=NULL) {
		if(nForMap)
			pOrgData = JS_HashMap_GetEntryData(pItem->pData);
		else
			pOrgData = pItem->pData;
		if(pFindFunc) {
			nFound = pFindFunc(pList->pOwner,pOrgData,pData);
		}else if(pList->pFuncFind) {
			nFound = pList->pFuncFind(pList->pOwner,pOrgData,pData);
		}else {
			if(pOrgData==pData)
				nFound = 1;
		}
		if(nFound) {
			pDataFound = pItem->pData;
			if(nDonotRemove==0) {
				if(pItem->pPrev)
					pItem->pPrev->pNext = pItem->pNext;
				if(pItem->pNext)
					pItem->pNext->pPrev = pItem->pPrev;
				if(pItem==pList->pHead)
					pList->pHead = pItem->pNext;
				if(pItem==pList->pTail)
					pList->pTail = pItem->pPrev;
				JS_Iteration_OnListRmItem(&(pList->rcIt),pItem);
				JS_FREE(pItem);
				if(pList->nNum>0)
					pList->nNum--;
			}
			break;
		}
		pItem = pItem->pNext;
	}
	return pDataFound;
}

void * JS_List_PopItemEx(JS_HANDLE hList, void * pData, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc, int nDonotRemove)
{
	return JS_List_PopForMap(hList,pData,pFindFunc,nDonotRemove,0);
}

int JS_List_RemoveItemEx(JS_HANDLE hList, void * pData, JS_FT_COMPARE_ITEM_CALLBACK pFindFunc)
{
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	void * pDataFound = JS_List_PopItemEx(hList,pData,pFindFunc,0);
	if(pDataFound) {
		if(pList->pFuncDelete)
			pList->pFuncDelete(pList->pOwner,pDataFound);
		else
			JS_FREE(pDataFound);
		return 0;
	}else
		return -1;
}

int JS_List_GetSize(JS_HANDLE hList)
{
	JS_SimpleList * pList = (JS_SimpleList *)hList;
	return pList->nNum;
}

JS_HANDLE JS_List_MakeIterationHandler(JS_HANDLE hList, int nIsFront)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	if(pList==NULL)
		return NULL;
	if(pList->nNum<=0)
		return NULL;
	if(nIsFront)
		pItem=pList->pHead;
	else
		pItem=pList->pTail;
	return (JS_HANDLE) JS_Iteration_AllocListIteration(&(pList->rcIt),pItem);
}

void JS_List_ClearIterationHandler(JS_HANDLE hItemPos)
{
	JS_SimpleList * pList = NULL;
	JS_SimpleIterationItem * pIt = (JS_SimpleIterationItem *)hItemPos;
	if(pIt==NULL)
		return;
	pList=(JS_SimpleList *)pIt->pRefList;
	if(pList)
		JS_Iteration_RemoveIterationItem(&(pList->rcIt), pIt);
	else
		JS_FREE(pIt);
}

JS_HANDLE JS_List_GetNext(JS_HANDLE hList, JS_HANDLE hItemPos)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleIterationItem * pIt = (JS_SimpleIterationItem *)hItemPos;
	if(hItemPos==NULL) {
		return JS_List_MakeIterationHandler(hList,1);
	}else {
		if(pIt->pNext) {
			pItem = (JS_SimpleListItem *)pIt->pNext;
			pIt->pPrev = pItem->pPrev;
			pIt->pMe = pItem;
			pIt->pNext = pItem->pNext;
		}else {
			JS_List_ClearIterationHandler(hItemPos);
			hItemPos = NULL;
		}
		return hItemPos;
	}
}

JS_HANDLE JS_List_GetPrev(JS_HANDLE hList, JS_HANDLE hItemPos)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleIterationItem * pIt = (JS_SimpleIterationItem *)hItemPos;
	if(hItemPos==NULL) {
		return JS_List_MakeIterationHandler(hList,0);
	}else {
		if(pIt->pPrev) {
			pItem = (JS_SimpleListItem *)pIt->pPrev;
			pIt->pNext = pItem->pNext;
			pIt->pMe = pItem;
			pIt->pPrev = pItem->pPrev;
		}else {
			JS_List_ClearIterationHandler(hItemPos);
			hItemPos = NULL;
		}
		return hItemPos;
	}
}

void * JS_List_GetDataFromIterateItem(JS_HANDLE hItem)
{
	JS_SimpleListItem * pItem = NULL;
	JS_SimpleIterationItem * pIt = (JS_SimpleIterationItem *)hItem;
	if(pIt==NULL)
		return NULL;
	if(pIt->pMe==NULL)
		return NULL;
	pItem = (JS_SimpleListItem *)pIt->pMe;
	return pItem->pData;
}

void *  JS_List_PopPosition(JS_HANDLE hList, JS_HANDLE hIteration)
{
	JS_SimpleList * pList = (JS_SimpleList*)hList;
	JS_SimpleIterationItem * pIt = (JS_SimpleIterationItem *)hIteration;
	JS_SimpleListItem * pRealItem = NULL;
	void * pData = NULL;
	if(pList==NULL || pIt==NULL || pIt->pMe==NULL)
		return NULL;
	if(pList != pIt->pRefList) {
		DBGPRINT("TMP:Broken List!!!\n");
		return NULL;
	}
	pRealItem = (JS_SimpleListItem *)pIt->pMe;
	if(pRealItem) {
		pData = pRealItem->pData;
		if(pRealItem==pList->pHead) {
			pList->pHead = pRealItem->pNext;
		}
		if(pRealItem==pList->pTail) {
			pList->pTail = pRealItem->pPrev;
		}
		if(pRealItem->pPrev)
			pRealItem->pPrev->pNext = pRealItem->pNext;
		if(pRealItem->pNext)
			pRealItem->pNext->pPrev = pRealItem->pPrev;
		if(pList->nNum>0)
			pList->nNum--;
		JS_Iteration_OnListRmItem(&(pList->rcIt),pRealItem);
		JS_FREE(pRealItem);
	}
	pIt->pMe = NULL;
	return pData;
}




