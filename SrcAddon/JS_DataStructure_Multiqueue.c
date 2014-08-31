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
Reordering queue for turgogate proxy

1. Allocate a queue item per a http connection
2. Push data to an item whose range is the http connection's range
3. Aggregate and reorder the data
4. Pop data and send them to the client socket
**********************************************************************/

#include "JS_Config.h"
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"
#include "JS_ThreadPool.h"
#include "JS_EventLoop.h"
#include "JS_MediaProxy.h"

#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif

//////////////////////////////////////////////////////
//reordering queue

typedef struct JS_ReorderingQ_QueueTag {
	UINT64	nStartTick;
	UINT64	nLastTick;
	UINT32	nInSpeed;
	UINT32	nOutSpeed;
	UINT32	nQueueRate;
	UINT64	nTotalSent;
	UINT64	nTotalRecv;
	int nMax;
	int nNum;
	int nCompleteNum;
	int nHead;
	int nTail;
	int nRoundCount;
	HTTPSIZE_T	 nTotalLen;
	HTTPSIZE_T	 nNextOffsetInStream;
	HTTPSIZE_T	 nRemainAllocSize;
	HTTPSIZE_T	 nTotalPumpOut;
	UINT32		 nGrowingChunkSize;
	JS_RQ_ITEM_T arrRQ[JS_CONFIG_MAX_MULTIQUEUEITEM+1];
}JS_ReorderingQ_Queue;

static __inline int _JS_ROUND_ADD_(JS_ReorderingQ_Queue* pQueue, int nIndex)
{
	return ((nIndex+1)%pQueue->nMax);
}

JS_HANDLE JS_ReorderingQ_Create(int nMaxItem)
{
	int nCnt;
	JS_ReorderingQ_Queue *	pRQ = NULL;
	JS_RQ_ITEM_T * pItem;
	if(nMaxItem<=0) {
		DBGPRINT("JS_RQ: can't make queue in which maxitem=0\n");
		return NULL;
	}
	pRQ = (JS_ReorderingQ_Queue*)JS_ALLOC(sizeof(JS_ReorderingQ_Queue));
	if(pRQ==NULL) {
		DBGPRINT("JS_RQ: no mem error\n");
		return NULL;
	}
	memset((void*)pRQ,0,sizeof(JS_ReorderingQ_Queue));
	pRQ->nGrowingChunkSize = JS_CONFIG_QUEUEITEM_MIN_SIZE;
	pRQ->nMax = nMaxItem;
	for(nCnt=0; nCnt<JS_CONFIG_MAX_MULTIQUEUEITEM; nCnt++) {
		pItem = &(pRQ->arrRQ[nCnt]);
		pItem->nMyIndex = nCnt+1;
		JS_STRCPY(pItem->arrSig,"RQT");
	}
	return (JS_HANDLE)pRQ;
}

UINT64 JS_ReorderingQ_GetNotAllocatedSizeFromTotal(JS_HANDLE hRQ)
{
	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return 0;
	return pRQ->nRemainAllocSize;
}

UINT64 JS_ReorderingQ_GetRemainDataNotSent(JS_HANDLE hRQ)
{
	UINT64 nTotal;
	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return 0;
	nTotal = pRQ->nTotalLen - pRQ->nTotalPumpOut;
	return nTotal;
}

UINT64 JS_ReorderingQ_GetRemainDataNotRcvd(JS_HANDLE hRQ)
{
	UINT64	nRemain;
	int nCnt;
	int nIndex;
	JS_RQ_ITEM_T * pItem;
	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return 0;
	nRemain = pRQ->nRemainAllocSize;
	if(pRQ->nNum>0) {
		nIndex = pRQ->nHead;
		for(nCnt=0; nCnt<pRQ->nNum; nCnt++) {
			pItem = &(pRQ->arrRQ[nIndex]);
			nRemain = nRemain + (pItem->nItemBuffSize-pItem->nPumpInOffset);
			nIndex = _JS_ROUND_ADD_(pRQ,nIndex);
		}
	}
	return nRemain;
}

void JS_ReorderingQ_Reset(JS_HANDLE hRQ)
{
	int nCnt;
	int nHead;
	JS_RQ_ITEM_T * pRqItem;
	UINT32	nInSpeed;
	UINT32	nOutSpeed;
	UINT32	nQueueRate;
	int nMax;

	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return;
	nHead = pRQ->nHead;
	for(nCnt=0; nCnt<pRQ->nNum; nCnt++) {
		pRqItem = &(pRQ->arrRQ[nHead]);
		if(pRqItem->pItemBuff)
			JS_FREE(pRqItem->pItemBuff);
		nHead = _JS_ROUND_ADD_(pRQ,nHead);
	}
	nInSpeed = pRQ->nInSpeed;
	nOutSpeed = pRQ->nOutSpeed;
	nQueueRate = pRQ->nQueueRate;
	nMax = pRQ->nMax;
	memset((void*)pRQ,0,sizeof(JS_ReorderingQ_Queue));
	pRQ->nMax = nMax;
	pRQ->nInSpeed = nInSpeed;
	pRQ->nOutSpeed = nOutSpeed;
	pRQ->nQueueRate = nQueueRate;
	pRQ->nGrowingChunkSize = JS_CONFIG_QUEUEITEM_MIN_SIZE;
}

void JS_ReorderingQ_Destroy(JS_HANDLE hRQ)
{
	int nCnt;
	int nHead;
	JS_RQ_ITEM_T * pRqItem;
	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return;
	nHead = pRQ->nHead;
	for(nCnt=0; nCnt<pRQ->nNum; nCnt++) {
		pRqItem = &(pRQ->arrRQ[nHead]);
		if(pRqItem->pItemBuff)
			JS_FREE(pRqItem->pItemBuff);
		nHead = _JS_ROUND_ADD_(pRQ,nHead);
	}
	JS_FREE(pRQ);
}

void JS_ReorderingQ_SetTotallSize(JS_HANDLE hRQ, HTTPSIZE_T nOriginalLen)
{
	JS_ReorderingQ_Queue *	pRQ = (JS_ReorderingQ_Queue *)hRQ;
	if(pRQ==NULL)
		return;
	if(nOriginalLen>pRQ->nTotalLen)
		pRQ->nRemainAllocSize += (nOriginalLen-pRQ->nTotalLen);
	else {
		UINT64 nDiff = pRQ->nTotalLen-nOriginalLen;
		if(nDiff<pRQ->nRemainAllocSize)
			pRQ->nRemainAllocSize -= nDiff;
		else
			pRQ->nRemainAllocSize = 0;
	}
	pRQ->nTotalLen = nOriginalLen;
}

JS_RQ_ITEM_T * JS_ReorderingQ_PumpInPushBack(JS_HANDLE hRQ, int nConnectionID, UINT32 nManualChunkSize)
{
	UINT32  nReservedSize;
	JS_RQ_ITEM_T * pItem = NULL;
	JS_ReorderingQ_Queue * pQueue = (JS_ReorderingQ_Queue *)hRQ;
	if(pQueue==NULL || pQueue->nTotalLen==0) {
		DBGPRINT("Queue Not Ready: may be reset\n");
		return NULL;
	}
	if(pQueue->nNum>=pQueue->nMax) {
		DBGPRINT("RQ no remain item in Q %d,%d\n",pQueue->nNum,pQueue->nMax);
		return NULL;
	}
	if(pQueue->nRemainAllocSize<=0) {
		//DBGPRINT("RQ completed\n");
		return NULL;
	}
	pItem = (JS_RQ_ITEM_T *)&(pQueue->arrRQ[pQueue->nTail]);
	if(pItem->nItemBuffSize>pItem->nPumpOutOffset) 
		return NULL;
	////tmp dbg
	DBGPRINT("TMP: mqueue: push q: index=%d, old contid=%d, new contid=%d, old size=%u:%u:%u, qnum=%u\n",pQueue->nTail, pItem->nConnectionID,nConnectionID,pItem->nItemBuffSize,pItem->nPumpOutOffset,pItem->nPumpInOffset,pQueue->nNum);
	memset((void*)pItem,0,sizeof(JS_RQ_ITEM_T));	////make it null
	if(nManualChunkSize>0) {
		if(nManualChunkSize<pQueue->nRemainAllocSize)
			pItem->nItemBuffSize = nManualChunkSize;
		else
			pItem->nItemBuffSize = (UINT32)pQueue->nRemainAllocSize;
	}else {
		nReservedSize = pQueue->nGrowingChunkSize;
		if(nReservedSize<pQueue->nRemainAllocSize)
			pItem->nItemBuffSize = nReservedSize;
		else
			pItem->nItemBuffSize = (UINT32)pQueue->nRemainAllocSize;
	}
	pItem->pItemBuff = (char*)JS_ALLOC(pItem->nItemBuffSize+32);
	if(pItem->pItemBuff==NULL) {
		DBGPRINT("RQ can't alloc item in Q\n");
		pItem->nItemBuffSize = 0;
		return NULL;
	}
	pItem->hQueue = hRQ;
	pItem->nPumpInOffset = 0;
	pItem->nPumpOutOffset = 0;
	pItem->nOffsetInStream = pQueue->nNextOffsetInStream;
	pItem->nTotalLen = pQueue->nTotalLen;
	pItem->nConnectionID = nConnectionID;
	pQueue->nTail = _JS_ROUND_ADD_(pQueue,pQueue->nTail);
	pQueue->nNum++;
	pQueue->nRemainAllocSize -= pItem->nItemBuffSize;
	pQueue->nNextOffsetInStream += pItem->nItemBuffSize;
	//DBGPRINT("TMP offsetinstream=%llu remain=%llu\n",pQueue->nNextOffsetInStream,pQueue->nRemainAllocSize);
	if(nManualChunkSize==0 && pQueue->nGrowingChunkSize<JS_CONFIG_QUEUEITEM_MAX_SIZE) {
		if(pQueue->nRoundCount%5==4)
				pQueue->nGrowingChunkSize += JS_CONFIG_QUEUEITEM_GROWING_SIZE;
	}
	if(pQueue->nRoundCount>0) {
		;//DBGPRINT("FG m=%u u=%llu\n",pItem->nChunkSize,pQueue->nRemainLen);
	}
	pQueue->nRoundCount++;
	if(pQueue->nRemainAllocSize==0)
		pItem->nIsLastItem = 1;
	else
		pItem->nIsLastItem = 0;
	return pItem;
}

int JS_ReorderingQ_NeedNewItem(JS_RQ_ITEM_T * pRqItem)
{
	if(pRqItem==NULL)
		return 1;
	if(pRqItem->nPumpInOffset>=pRqItem->nItemBuffSize)
		return 1;
	else
		return 0;
}

unsigned int JS_ReorderingQ_PumpInCopyData(JS_RQ_ITEM_T * pRqItem, char * pData, unsigned int nSize)
{
	unsigned int nRet=0;
	unsigned int nCopySize;
	unsigned int nTrCnt = 0;
	JS_ReorderingQ_Queue * pQueue = (JS_ReorderingQ_Queue *)pRqItem->hQueue;

	if(pQueue==NULL)
		return 0;
	if(pRqItem->nPumpInOffset+nSize>pRqItem->nItemBuffSize) {
		nCopySize = pRqItem->nItemBuffSize-pRqItem->nPumpInOffset;
	}else
		nCopySize = nSize;
	if(nCopySize>0) {
		memcpy(pRqItem->pItemBuff+pRqItem->nPumpInOffset,pData,nCopySize);
		pRqItem->nPumpInOffset += nCopySize;
	}
	nRet = nCopySize;

	////calculate speed
	if(pRqItem->nPumpInOffset>=pRqItem->nItemBuffSize)
		pQueue->nCompleteNum ++;
	pQueue->nTotalRecv += nRet;
	return nRet;
}

unsigned int JS_ReorderingQ_PumpOutGetAvailSize(JS_HANDLE hRQ, char ** ppData, unsigned int nBlockSize)
{
	UINT32 nDiff;
	JS_RQ_ITEM_T * pItem;
	JS_ReorderingQ_Queue * pQueue = (JS_ReorderingQ_Queue *)hRQ;

	if(pQueue->nNum<=0) {
		////no queue item
		return 0;
	}
	pItem = (JS_RQ_ITEM_T *)&(pQueue->arrRQ[pQueue->nHead]);
	if(pItem->nPumpOutOffset >= pItem->nPumpInOffset) {
		////no data in the item
		return 0;
	}
	nDiff = pItem->nPumpInOffset-pItem->nPumpOutOffset;
	if(nBlockSize>0 && nDiff>nBlockSize) {
		nDiff=nBlockSize;
	}
	*ppData = pItem->pItemBuff+pItem->nPumpOutOffset;

	return (unsigned int)nDiff;
}

int JS_ReorderingQ_PumpOutComplete(JS_HANDLE hRQ, unsigned int nDataSize)
{
	int nRet = 0;
	JS_RQ_ITEM_T * pItem;
	JS_ReorderingQ_Queue * pQueue = (JS_ReorderingQ_Queue *)hRQ;
	if(pQueue->nNum<=0) {
		////no queue item
		return 0;
	}
	pItem = (JS_RQ_ITEM_T *)&(pQueue->arrRQ[pQueue->nHead]);
	pItem->nPumpOutOffset+=nDataSize;
	pQueue->nTotalPumpOut += nDataSize;
	if(pItem->nPumpOutOffset>=pItem->nItemBuffSize){
		////pop up front this item from queue
		//DBGPRINT("TMP:nPumpOutOffset=%u, %u %u\n",pItem->nPumpOutOffset, nDataSize, pItem->nItemBuffSize);
		//DBGPRINT("TMP: multiq total size=%llu\n",pQueue->nTotalLen);
		if(pItem->nIsLastItem)
			nRet = JS_RET_REORDERINGQ_EOF;
		JS_FREE(pItem->pItemBuff);
		//memset((void*)pItem,0,sizeof(JS_RQ_ITEM_T));	////make it null
		pQueue->nHead = _JS_ROUND_ADD_(pQueue,pQueue->nHead);
		if(pQueue->nNum>0)
			pQueue->nNum--;
		if(pQueue->nCompleteNum>0)
			pQueue->nCompleteNum--;
	}

	////calculate speed
	if(nRet != JS_RET_REORDERINGQ_EOF){
		UINT64 nCurTick;
		UINT64 nDiff;
		UINT64 nSpeedS;
		UINT64 nSpeedR;
		UINT32 nSpeedQ;
		nCurTick = JS_UTIL_GetTickCount();
		pQueue->nTotalSent += nDataSize;
		if(pQueue->nStartTick==0)
			pQueue->nStartTick=nCurTick;
		else {
			nDiff = nCurTick - pQueue->nLastTick;
			if(JS_CONFIG_TIME_MSEC_SPEEDCHECK<nDiff) {
				nSpeedS = (pQueue->nTotalSent<<3)/nDiff*1000;
				nSpeedR = (pQueue->nTotalRecv<<3)/nDiff*1000;
				nSpeedQ = pQueue->nCompleteNum * 100 / pQueue->nMax;
				pQueue->nOutSpeed = (pQueue->nOutSpeed>>1) + (UINT32)(nSpeedS>>1);
				pQueue->nInSpeed = (pQueue->nInSpeed>>1) + (UINT32)(nSpeedR>>1);
				pQueue->nQueueRate = (pQueue->nQueueRate>>1) + (nSpeedQ>>1);
				////clear
				pQueue->nTotalSent = 0;
				pQueue->nTotalRecv = 0;
				pQueue->nLastTick = nCurTick;
				nRet = JS_RET_NEEDCHECKSPEED;
			}
		}
	}
	return nRet;
}

void JS_ReorderingQ_GetSpeed(JS_HANDLE hRQ, UINT32 * pnInSpeed, UINT32 * pnOutSpeed, UINT32 * pnQRate)
{
	JS_ReorderingQ_Queue * pQueue = (JS_ReorderingQ_Queue *)hRQ;
	if(pQueue==NULL)
		return;
	if(pnInSpeed)
		*pnInSpeed = pQueue->nInSpeed;
	if(pnOutSpeed)
		*pnOutSpeed = pQueue->nOutSpeed;
	if(pnQRate)
		*pnQRate = pQueue->nQueueRate;
}

#endif