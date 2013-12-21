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
Simple Queue for http operation
1. can push http packet
2. can pull http header and data after merging packets
3. can decode http chunked transfer mode
*********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_DataStructure.h"

//////////////////////////////////////////////////////
//macro start
#define JS_Q_TYPE_SIMPLE			1
#define JS_Q_TYPE_REORDER			2
#define JS_Q_TYPE_CHUNKED_DECODE	16
#define JS_Q_TYPE_CHUNKED_BYPASS	32
#define JS_Q_TYPE_INPUT				64

#define JS_Q_TR_ZERO		0
#define JS_Q_TR_CHECK_SIZE	1
#define JS_Q_TR_SKIP_CRLF	2
#define JS_Q_TR_COUNT		3
#define JS_Q_TR_CHECK_ECR	4
#define JS_Q_TR_SKIP_EOF	5
#define JS_Q_TR_END			6

//////////////////////////////////////////////////////
//local type start
typedef struct JSSimpleQItemTag {
	UINT32	nSentOffset;
	UINT32  nBuffSize;
	char *  pItemBuff;
	int		nEos;
	int		nNeedToClose;
}JSSimpleQItem;

typedef struct JSSimpleQTag {
	int nMask;
	HTTPSIZE_T nHttpLength;
	HTTPSIZE_T nTotalRcvd;
	HTTPSIZE_T nTotalSent;
	int		nDataSize;
	JS_HANDLE hList;
	unsigned int nChunkedTransferSize;
	int nChunkedTransferStatus;
	int nChunkedTransferCounter;
}JSSimpleQ;
static char * JS_SimpleQ_FilterForChunkedTransfer(JSSimpleQ * pQueue, const char * pData, unsigned int nSize, unsigned int * pnNewSize);
//local type end
///////////////////////////////////////////////////////

//////////////////////////////////////////////////////
//function implementations start

int  JS_Q_GetType(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		return pQueue->nMask&0x3;
	}else
		return 0;	
}

int  JS_Q_IsInput(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		return pQueue->nMask&JS_Q_TYPE_INPUT;
	}else
		return 0;
}

int  JS_Q_IsChunked(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		return (pQueue->nMask&JS_Q_TYPE_CHUNKED_DECODE)|(pQueue->nMask&JS_Q_TYPE_CHUNKED_BYPASS);
	}else
		return 0;
}


JS_HANDLE JS_SimpleQ_Create(int nIsChunked, int nIsInput)
{
	int nRet = 0;
	JSSimpleQ * pQueue = NULL;
	pQueue = (JSSimpleQ *)JS_ALLOC(sizeof(JSSimpleQ));
	if(pQueue==NULL) {
		DBGPRINT("simpleq: mem error1\n");
		nRet = -1;
		goto SIMPLEQ_CREATE_EXIT;
	}
	memset((char*)pQueue,0,sizeof(JSSimpleQ));
	pQueue->hList = JS_List_Create(pQueue,NULL);
	if(pQueue->hList==NULL) {
		DBGPRINT("simpleq: mem error2\n");
		nRet = -2;
		goto SIMPLEQ_CREATE_EXIT;
	}
	pQueue->nMask = JS_Q_TYPE_SIMPLE;
	if(nIsChunked)
		pQueue->nMask |= JS_Q_TYPE_CHUNKED_BYPASS;
	if(nIsInput)
		pQueue->nMask |= JS_Q_TYPE_INPUT;
SIMPLEQ_CREATE_EXIT:
	if(nRet<0) {
		if(pQueue)
			JS_SimpleQ_Destroy(pQueue);
		pQueue = NULL;
	}
	return (JS_HANDLE)pQueue;
}


void JS_SimpleQ_Destroy(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		JS_SimpleQ_Reset(hQ);
		if(pQueue->hList)
			JS_List_Destroy(pQueue->hList);
		JS_FREE(hQ);
	}
}

void JS_SimpleQ_Reset(JS_HANDLE hQ)
{
	JS_HANDLE hItemPos;
	JSSimpleQItem * pItem;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		hItemPos = NULL;
		while(1) {
			hItemPos = JS_List_GetNext(pQueue->hList,hItemPos);
			if(hItemPos==NULL)
				break;
			pItem = (JSSimpleQItem *)JS_List_GetDataFromIterateItem(hItemPos);
			if(pItem) {
				if(pItem->pItemBuff)
					JS_FREE(pItem->pItemBuff);
				JS_List_PopPosition(pQueue->hList,hItemPos);
				JS_FREE(pItem);
			}
		}
		JS_List_ClearIterationHandler(hItemPos);
		pQueue->nHttpLength = 0;
		pQueue->nTotalRcvd = 0;
		pQueue->nTotalSent = 0;
		pQueue->nDataSize = 0;
		pQueue->nChunkedTransferStatus = 0;
		pQueue->nChunkedTransferSize = 0;
		pQueue->nChunkedTransferCounter = 0;
	}
}

void JS_SimpleQ_ResetTotallSize(JS_HANDLE hQ, HTTPSIZE_T nOriginalLen)
{
	JSSimpleQItem * pItem;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		pItem = (JSSimpleQItem *)JS_List_GetFront(pQueue->hList);
		pQueue->nHttpLength = nOriginalLen;
		if(pItem)
			pQueue->nTotalRcvd = pItem->nBuffSize-pItem->nSentOffset;
		else
			pQueue->nTotalRcvd = 0;
		pQueue->nTotalSent = 0;
	}
}

void JS_SimpleQ_SetChunkedTransferDecoding(JS_HANDLE hQ, int nEnable)
{
	unsigned int nNewSize;
	unsigned int nItemSize;
	unsigned int nDiff;
	JS_HANDLE hItemPos = NULL;
	char * pMem = NULL;
	JSSimpleQItem * pItem = NULL;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		if(nEnable) {
			pQueue->nMask &= ~JS_Q_TYPE_CHUNKED_BYPASS;
			pQueue->nMask |= JS_Q_TYPE_CHUNKED_DECODE;
			pQueue->nChunkedTransferStatus = JS_Q_TR_CHECK_SIZE;
			hItemPos = NULL;
			while(1) {
				hItemPos = JS_List_GetNext(pQueue->hList,hItemPos);
				if(hItemPos==NULL)
					break;
				pItem = (JSSimpleQItem *)JS_List_GetDataFromIterateItem(hItemPos);
				nItemSize = pItem->nBuffSize-pItem->nSentOffset;
				pMem = JS_SimpleQ_FilterForChunkedTransfer(pQueue,pItem->pItemBuff+pItem->nSentOffset,nItemSize,&nNewSize);
				if(pMem==NULL && pQueue->nChunkedTransferStatus<0) {
					DBGPRINT("simple q: transfer chunked decoding fail(first exception)\n");
					break;
				}else {
					nDiff = nItemSize-nNewSize;
					pQueue->nDataSize -= nDiff;
					if(pQueue->nDataSize<0)
						pQueue->nDataSize = 0;
					if(pQueue->nTotalRcvd>nDiff)
						pQueue->nTotalRcvd -= nDiff;
					else
						pQueue->nTotalRcvd = 0;
					pItem->nBuffSize -= nDiff;
					if(pMem==NULL) {
						if(pItem->nBuffSize<=0) {
							JS_FREE(pItem->pItemBuff);
							JS_FREE(pItem);
							JS_List_PopPosition(pQueue->hList,hItemPos);
						}
					}else {
						memcpy(pItem->pItemBuff+pItem->nSentOffset,pMem,nNewSize);
						JS_FREE(pMem);
					}
				}
			}
			JS_List_ClearIterationHandler(hItemPos);
		}
	}
}

void JS_SimpleQ_SetChunkedTransferBypass(JS_HANDLE hQ, int nEnable)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		pQueue->nMask &= ~JS_Q_TYPE_CHUNKED_DECODE;
		pQueue->nMask |= JS_Q_TYPE_CHUNKED_BYPASS;
	}
}

int JS_SimpleQ_CheckAllRcvd(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		if(pQueue->nHttpLength<=pQueue->nTotalRcvd)
			return 1;
	}
	return 0;
}
int JS_SimpleQ_CheckAllDone(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue) {
		if(pQueue->nMask&(JS_Q_TYPE_CHUNKED_BYPASS|JS_Q_TYPE_CHUNKED_DECODE)) {
			if(pQueue->nDataSize<=0) {
				if(pQueue->nChunkedTransferStatus==JS_Q_TR_END)
					return 1;
			}
		}else if(pQueue->nHttpLength==pQueue->nTotalSent)
			return 1;
		else if(pQueue->nHttpLength<pQueue->nTotalSent) {
			DBGPRINT("simple q check size: error overflow totallen=%llu<-->sent=%llu\n",pQueue->nHttpLength,pQueue->nTotalSent);
		}

	}
	return 0;
}


JSUINT JS_SimpleQ_GetDataSize(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return 0;
	return pQueue->nDataSize;
}

HTTPSIZE_T JS_SimpleQ_GetTotalRcvd(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return 0;
	return pQueue->nTotalRcvd;
}

HTTPSIZE_T JS_SimpleQ_GetTotalSent(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return 0;
	return pQueue->nTotalSent;
}

int JS_SimpleQ_CheckAvailableData(JS_HANDLE hQ)
{
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return 0;
	if(JS_List_GetFront(pQueue->hList) != NULL)
		return 1;
	else 
		return 0;
}

int JS_SimpleQ_PushPumpIn(JS_HANDLE hQ, const char * pData, unsigned int nSize)
{
	int nRet = 0;
	unsigned int nNewSize;
	char * pMem = NULL;
	JSSimpleQItem * pItem = NULL;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return -1;
	if(pQueue->nMask&JS_Q_TYPE_CHUNKED_DECODE) {
		pMem = JS_SimpleQ_FilterForChunkedTransfer(pQueue,pData,nSize,&nNewSize);
		if(pMem==NULL) {
			if(pQueue->nChunkedTransferStatus<0) {
				nRet = -1;
				DBGPRINT("simple q: transfer chunked decoding fail\n");
				goto LABEL_CATCH_ERROR;
			}else
				return 0;
		}
		nSize = nNewSize;
	}else {
		pMem = (char*)JS_ALLOC(nSize+8);
		if(pMem==NULL) {
			nRet = -1;
			DBGPRINT("simple q: push error no mem error(pmem)\n");
			goto LABEL_CATCH_ERROR;
		}
	}
	pItem = (JSSimpleQItem *)JS_ALLOC(sizeof(JSSimpleQItem));
	if(pItem==NULL) {
		nRet = -1;
		DBGPRINT("simple q: push error no mem error 1\n");
		goto LABEL_CATCH_ERROR;
	}
	pItem->pItemBuff = pMem;
	nRet = JS_List_PushBack(pQueue->hList,pItem);
	if(nRet<0) {
		DBGPRINT("simple q: push error list error\n");
		goto LABEL_CATCH_ERROR;
	}
	pItem->nNeedToClose = 0;
	pItem->nEos  = 0;
	pItem->nSentOffset = 0;
	pItem->nBuffSize = nSize;
	if(!(pQueue->nMask&JS_Q_TYPE_CHUNKED_DECODE))
		memcpy(pItem->pItemBuff,pData,nSize);
	pQueue->nTotalRcvd += nSize;
	pQueue->nDataSize  += nSize;
LABEL_CATCH_ERROR:
	if(nRet<0) {
		if(pMem) {
			JS_FREE(pMem);
		}
		if(pItem) {
			JS_FREE(pItem);
		}
	}
	return nRet;
}

int JS_SimpleQ_FinishPumpOut(JS_HANDLE hQ, JSUINT nLoadedData)
{
	int nRet = 0;
	int nEos = 0;
	int nNeedToClose = 0;
	char * pData = NULL;
	JSSimpleQItem * pItem;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	if(pQueue==NULL)
		return -1;
	pItem = (JSSimpleQItem *)JS_List_GetFront(pQueue->hList);
	pItem->nSentOffset += nLoadedData;
	pQueue->nTotalSent += nLoadedData;
	pQueue->nDataSize  -= nLoadedData;
	if(pQueue->nDataSize<0)
		pQueue->nDataSize = 0;
	if(pItem->nSentOffset >= pItem->nBuffSize) {
		if(pQueue->nMask & JS_Q_TYPE_CHUNKED_BYPASS) {
			if(JS_UTIL_HTTP_IsEndOfChunk(pItem->pItemBuff,pItem->nBuffSize))
				pQueue->nChunkedTransferStatus = JS_Q_TR_END;
		}
		nNeedToClose = pItem->nNeedToClose;
		nEos = pItem->nEos;
		JS_FREE(pItem->pItemBuff);
		JS_List_PopFront(pQueue->hList);
		JS_FREE(pItem);
	}
	if(nNeedToClose)
		nRet = JS_Q_RET_NEEDTOCLOSE;
	else if(pQueue->nHttpLength>0 && pQueue->nHttpLength<=pQueue->nTotalSent)
		nRet = JS_Q_RET_FINISHED;
	else if((pQueue->nMask&(JS_Q_TYPE_CHUNKED_BYPASS)) && (pQueue->nChunkedTransferStatus==JS_Q_TR_END))
		nRet = JS_Q_RET_FINISHED;
	return nRet;
}


char * JS_SimpleQ_PreparePumpOut(JS_HANDLE hQ, JSUINT nNeedSize, JSUINT * pnBuffSize, const char * strPattern, int nPattern, int * pnFound)
{
	int nRet = 0;
	int nRemainInBuff;
	int nSentOffset;
	JSSimpleQItem * pItem;
	JSSimpleQItem * pHeadItem;
	JSSimpleQ * pQueue = (JSSimpleQ *)hQ;
	char * pData = NULL;
	if(pQueue==NULL)
		return NULL;
	pHeadItem = (JSSimpleQItem *)JS_List_GetFront(pQueue->hList);
	if(pHeadItem==NULL)
		return NULL;
	if(pHeadItem->nBuffSize<=0)
		return NULL;
	nSentOffset = pHeadItem->nSentOffset;
	pData = pHeadItem->pItemBuff+nSentOffset;
	nRemainInBuff = pHeadItem->nBuffSize-nSentOffset;
	if(nNeedSize>0 && nRemainInBuff>(int)nNeedSize)
		*pnBuffSize = nNeedSize;
	else
		*pnBuffSize = nRemainInBuff;
	if(strPattern && pnFound) {
		int nIdx, nPrevLen, nNextLen, nListCnt, nCnt, nCnt1, nCnt2, nFlag, nTotal, nBodyOffset;
		char * pPrevData;
		char * pNextData;
		JS_HANDLE hItemPos;
		pPrevData = NULL;
		nPrevLen = 0;
		nListCnt = 0;
		nBodyOffset = 0;
		nTotal = 0;
		pNextData = pData;
		nNextLen = nRemainInBuff;
		if(nPattern<=0)
			nPattern = strlen(strPattern);
		*pnFound = 0;
		hItemPos = JS_List_GetNext(pQueue->hList,NULL);
		while(1) {
			////find the pattern between listitems
			if(pPrevData && nNextLen>0) {
				for(nCnt=0; nCnt<nPattern-1; nCnt++) {
					nFlag = 0;
					if(nNextLen<=nCnt)
						break;
					for(nCnt1=nCnt; nCnt1<nPattern-1; nCnt1++) {	////check prev item  with pattern
						if(pPrevData[nCnt1]!=strPattern[nCnt1-nCnt]) {
							nFlag = 1;
							break;
						}
					}
					if(nFlag==0) {   
						for(nCnt2=0; nCnt2<nCnt; nCnt2++) {	////check next item with pattern
							if(pNextData[nCnt2]!=strPattern[(nPattern-1-nCnt)+nCnt2]) {
								nFlag = 1;
								break;
							}
						}
					}
					if(nFlag==0) {
						nCnt2++;
						nFlag = 2;
						break;
					}
				}
				if(nFlag==2) {	////pattern found
					*pnFound = 1;
					nBodyOffset = nTotal+nCnt2;
					nTotal += nNextLen;
					break;
				}
				nCnt2 = 0;
			}//find pattern between lists end
			if(nNextLen<nPattern)
				break;
			nIdx = JS_UTIL_FindPatternBinary(pNextData,strPattern,nNextLen,nPattern,0);
			if(nIdx>=0) {
				nBodyOffset = nTotal+nIdx+nPattern; ////header contains pattern (CRLFCRLF)
				nTotal += nNextLen;	
				*pnFound = 1;
				break;
			}else {
				nTotal += nNextLen;
				pPrevData = pNextData+nNextLen-nPattern+1;
				nPrevLen = nNextLen;
				hItemPos = JS_List_GetNext(pQueue->hList,hItemPos);	////iterate list until null
				if(hItemPos==NULL)
					break;
				pItem = (JSSimpleQItem *)JS_List_GetDataFromIterateItem(hItemPos);
				if(pItem==NULL)
					break;
				pNextData = pItem->pItemBuff;
				nNextLen = pItem->nBuffSize;
				nListCnt++;
			}
		}///while(1)
		JS_List_ClearIterationHandler(hItemPos);
		if(*pnFound == 1)
			*pnBuffSize = nBodyOffset;
		if(*pnFound == 1 && nListCnt>0) { ////merge data
			hItemPos = NULL;
			hItemPos = JS_List_GetNext(pQueue->hList,hItemPos);
			pItem = (JSSimpleQItem *)JS_List_GetDataFromIterateItem(hItemPos);
			pData = (char*)JS_REALLOC(pItem->pItemBuff,nSentOffset+nTotal+4);
			if(pData==NULL) {
				nRet = -1;
				goto LABEL_CATCH_ERROR;
			}
			pItem->pItemBuff = pData;
			pData = pItem->pItemBuff;
			nCnt1 = pItem->nBuffSize;
			pItem->nBuffSize = nSentOffset+nTotal;
			for(nCnt=0; nCnt<nListCnt; nCnt++) {
				////tmp dbg
				if(nCnt==nListCnt-2)
					nCnt=nListCnt-2;
				hItemPos = JS_List_GetNext(pQueue->hList,hItemPos);
				pItem = (JSSimpleQItem *)JS_List_GetDataFromIterateItem(hItemPos);
				memcpy(pData+nCnt1,pItem->pItemBuff,pItem->nBuffSize);
				nCnt1 += pItem->nBuffSize;
				JS_FREE(pItem->pItemBuff);
				JS_FREE(pItem);
				JS_List_PopPosition(pQueue->hList,hItemPos);
			}///for(nCnt=0; nCnt<nListCnt; nCnt++)
			JS_List_ClearIterationHandler(hItemPos);
			pData = pData + nSentOffset;
		}
		////check size condition
		if(nNeedSize>0 && (int)nNeedSize<nBodyOffset) {
			*pnFound = 0;
			pData = NULL;
		}
	}
LABEL_CATCH_ERROR:
	if(nRet<0)
		pData = NULL;
	return pData;
}


static char * JS_SimpleQ_FilterForChunkedTransfer(JSSimpleQ * pQueue, const char * pData, unsigned int nSize, unsigned int * pnNewSize)
{
	unsigned int nFilterSize = 0;
	unsigned int nTrSize;
	int nTrStatus;
	unsigned int nTrCnt;
	char * pChunkData=NULL;
	unsigned int nTrueChunkCnt;
	int nCopyCnt = 0;
	int nCurData;

	nTrSize = pQueue->nChunkedTransferSize;
	nTrStatus = pQueue->nChunkedTransferStatus;
	nTrueChunkCnt = pQueue->nChunkedTransferCounter;
	if(nTrStatus<0)
		goto LABEL_END_OF_CHUNKFILTER;
	////alloc temp memory
	pChunkData = (char*)JS_ALLOC(nSize);
	if(pChunkData==NULL) {
		DBGPRINT("filter chunk: no mem error(chunkdata)\n");
		pQueue->nChunkedTransferStatus = -1;
		goto LABEL_END_OF_CHUNKFILTER;
	}
	////check every byte for 'chunked' decoding
	for(nTrCnt=0; nTrCnt<nSize; nTrCnt++) {
		nCurData = pData[nTrCnt];
		if(nTrStatus==JS_Q_TR_CHECK_SIZE) {
			if(nCurData=='\r') {
				if(nTrSize==0)
					nTrStatus = JS_Q_TR_END;
				else
					nTrStatus = JS_Q_TR_SKIP_CRLF;
				nTrueChunkCnt = 0;
			}else {
				nTrSize = nTrSize * 16;
				if(toupper(nCurData) >= 'A' && toupper(nCurData) <= 'F')
					nTrSize += toupper(nCurData)-'A'+10;
				else if(nCurData >= '0' && nCurData <= '9')
					nTrSize += nCurData-'0';
				else {
					DBGPRINT("filter chunk: no proper chunk character %c %c\n", pData[nTrCnt],pData[nTrCnt+1]);
					pQueue->nChunkedTransferStatus = -1;
					goto LABEL_END_OF_CHUNKFILTER;
				}
			}
		}else if(nTrStatus==JS_Q_TR_SKIP_CRLF) {
			if(nCurData!='\n') {
				DBGPRINT("filter chunk: no CRCF %c\n",nCurData);
				pQueue->nChunkedTransferStatus = -1;
				goto LABEL_END_OF_CHUNKFILTER;
			}
			nTrueChunkCnt = 0;
			nTrStatus = JS_Q_TR_COUNT;
		}else if(nTrStatus==JS_Q_TR_COUNT) {
			pChunkData[nCopyCnt++] = (char)nCurData;
			nTrueChunkCnt++;
			if(nTrSize<=nTrueChunkCnt) {
				nTrStatus = JS_Q_TR_CHECK_ECR;
				nTrSize = 0;
				nTrueChunkCnt = 0;
			}
		}else if(nTrStatus==JS_Q_TR_CHECK_ECR) {
			if(nCurData != '\r') {
				DBGPRINT("filter chunk: no EoCR %c\n",nCurData);
				pQueue->nChunkedTransferStatus = -1;
				goto LABEL_END_OF_CHUNKFILTER;
			}else
				nTrStatus = JS_Q_TR_SKIP_EOF;
		}else if(nTrStatus == JS_Q_TR_SKIP_EOF) {
			if(nCurData=='\n') {
				nTrSize = 0;
				nTrueChunkCnt = 0;
				nTrStatus = JS_Q_TR_CHECK_SIZE;
			}else {
				DBGPRINT("filter chunk: no LF %c\n",nCurData);
				pQueue->nChunkedTransferStatus = -1;
				goto LABEL_END_OF_CHUNKFILTER;
			}
		}
		if(nTrStatus==JS_Q_TR_END) {
			break;
		}
	}
	pQueue->nChunkedTransferSize = nTrSize;
	pQueue->nChunkedTransferStatus = nTrStatus;
	pQueue->nChunkedTransferCounter = nTrueChunkCnt;
	nFilterSize = nCopyCnt;
	*pnNewSize = nFilterSize;
LABEL_END_OF_CHUNKFILTER:
	if(nFilterSize==0) {
		if(pChunkData)
			JS_FREE(pChunkData);
		pChunkData = NULL;
		*pnNewSize = 0;
	}
	return pChunkData;
}
