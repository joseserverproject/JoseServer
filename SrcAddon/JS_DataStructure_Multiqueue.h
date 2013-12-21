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

#ifndef JS_DATASTRUCTURE_MULTIQUEUE_H
#define JS_DATASTRUCTURE_MULTIQUEUE_H
/////////////////////////////////////////////////
//reordering queue
typedef struct JS_RQ_ITEM_TAG {
	UINT32		 nItemBuffSize;
	UINT32		 nPumpInOffset;
	UINT32		 nPumpOutOffset;
	HTTPSIZE_T	 nOffsetInStream;
	HTTPSIZE_T	 nTotalLen;
	char	* 	 pItemBuff;
	JS_HANDLE	 hQueue;
	int			 nConnectionID;
	int			 nIsLastItem;
	int			 nMyIndex;
	char		 arrSig[4];
}JS_RQ_ITEM_T;

#define JS_RET_REORDERINGQ_EOF	0x8
#define JS_RET_NEEDCHECKSPEED	0xFF
#define JS_REORDERINGQ_UNKNOWNSIZE			0xFFFFFFFFFFFFFFFF

JS_HANDLE JS_ReorderingQ_Create(int nMaxItem);
void JS_ReorderingQ_Destroy(JS_HANDLE hRQ);
void JS_ReorderingQ_Reset(JS_HANDLE hRQ);
void JS_ReorderingQ_SetTotallSize(JS_HANDLE hRQ, HTTPSIZE_T nOriginalLen);

UINT64 JS_ReorderingQ_GetNotAllocatedSizeFromTotal(JS_HANDLE hRQ);
UINT64 JS_ReorderingQ_GetRemainDataNotRcvd(JS_HANDLE hRQ);
UINT64 JS_ReorderingQ_GetRemainDataNotSent(JS_HANDLE hRQ);

int JS_ReorderingQ_NeedNewItem(JS_RQ_ITEM_T * pRqItem);
JS_RQ_ITEM_T * JS_ReorderingQ_PumpInPushBack(JS_HANDLE hRQ, int nConnectionID, UINT32 nManualChunkSize);
unsigned int JS_ReorderingQ_PumpInCopyData(JS_RQ_ITEM_T * pRqItem, char * pData, unsigned int nSize);
unsigned int JS_ReorderingQ_PumpOutGetAvailSize(JS_HANDLE hRQ, char ** ppData, unsigned int nBlockSize);
int JS_ReorderingQ_PumpOutComplete(JS_HANDLE hRQ, unsigned int nDataSize);
void JS_ReorderingQ_GetSpeed(JS_HANDLE hRQ, UINT32 * pnInSpeed, UINT32 * pnOutSpeed, UINT32 * pnQRate);

#endif /* JS_DATASTRUCTURE_H_ */