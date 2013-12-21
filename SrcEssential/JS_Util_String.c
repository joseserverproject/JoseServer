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
Utility functions for string manipulation
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_ThreadPool.h"
#include "JS_DataStructure.h"


/////////////////////////////////////////////////////////////
///string functions start
static char * JS_UTIL_StrJsonBuildStructFieldAll(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, const char * strFieldValue, int nIsString);

int JS_UTIL_StrCmp(const char * strBuff, const char * strPattern, int nBuffLen, int nPatternLen, int nIgnoreCase)
{
	int nCnt;
	int nUPA, nUPB;
	int nShort;
	if(nBuffLen<=0)
		nBuffLen = strlen(strBuff);
	if(nPatternLen<=0)
		nPatternLen = strlen(strPattern);
	if(nBuffLen<nPatternLen)
		nShort = nBuffLen;
	else
		nShort = nPatternLen;
	for(nCnt=0;nCnt<nShort; nCnt++) {
		if(nIgnoreCase) {
			nUPA = toupper(strBuff[nCnt]);
			nUPB = toupper(strPattern[nCnt]);
		}else {
			nUPA = (strBuff[nCnt]);
			nUPB = (strPattern[nCnt]);
		}
		if(nUPA != nUPB)
			break;
	}
	if(nCnt==nShort)
		return 0;
	else
		return -1;
}

int JS_UTIL_StrCmpRestrict(const char * strBuff, const char * strPattern, int nBuffLen, int nPatternLen, int nIgnoreCase)
{
	if(strBuff == NULL || strPattern == NULL)
		return -1;
	if(nBuffLen<=0)
		nBuffLen = strlen(strBuff);
	if(nPatternLen<=0)
		nPatternLen = strlen(strPattern);
	if(nBuffLen!=nPatternLen)
		return -2;
	return JS_UTIL_StrCmp(strBuff, strPattern, nBuffLen, nPatternLen, nIgnoreCase);
}

int JS_UTIL_FindPatternBinary(const char * strBuff, const char * strFindPattern, int nBuffLen, int nPatternLen, int nIgnoreCase)
{
	int nRet = -1;
	int nCnt, nPatternCnt;
	int nUPA, nUPB;
	if(nPatternLen>nBuffLen) {
		return -2;
	}
	if(nPatternLen<=0)
		nPatternLen = strlen(strFindPattern);
	if(nBuffLen<=0)
		nBuffLen = strlen(strBuff);
	for(nCnt=0; nCnt<=nBuffLen-nPatternLen; nCnt++) {
		for(nPatternCnt=0; nPatternCnt<nPatternLen; nPatternCnt++) {
			if(nIgnoreCase) {
				nUPA = toupper(strBuff[nCnt+nPatternCnt]);
				nUPB = toupper(strFindPattern[nPatternCnt]);
			}else {
				nUPA = strBuff[nCnt+nPatternCnt];
				nUPB = strFindPattern[nPatternCnt];
			}
			if(nUPA != nUPB)
				break;
		}
		if(nPatternCnt == nPatternLen) {
			nRet = nCnt;
			break;
		}
	}
	return nRet;
}

int JS_UTIL_FindPattern(const char * strBuff, const char * strFindPattern, int nBuffLen, int nPatternLen, int nIgnoreCase)
{
	int nRet = -1;
	int nCnt, nPatternCnt;
	int nUPA, nUPB;

	if(nPatternLen<=0)
		nPatternLen = strlen(strFindPattern);
	if(nBuffLen<=0)
		nBuffLen = strlen(strBuff);
	if(nPatternLen>nBuffLen) {
		return -2;
	}
	for(nCnt=0; nCnt<=nBuffLen-nPatternLen; nCnt++) {
		if(strBuff[nCnt]==0)
			break;
		for(nPatternCnt=0; nPatternCnt<nPatternLen; nPatternCnt++) {
			if(nIgnoreCase) {
				nUPA = toupper(strBuff[nCnt+nPatternCnt]);
				nUPB = toupper(strFindPattern[nPatternCnt]);
			}else {
				nUPA = strBuff[nCnt+nPatternCnt];
				nUPB = strFindPattern[nPatternCnt];
			}
			if(nUPA != nUPB)
				break;
		}
		if(nPatternCnt == nPatternLen) {
			nRet = nCnt;
			break;
		}
	}
	return nRet;
}

void JS_UTIL_TrimWhiteSpace(char * strOrg, const char * strWhiteSpace)
{
	int nLen;
	int nPrevIndx;
	int nPostIndx;
	int nWhCnt;
	int nWhSize;
	nWhSize = strlen(strWhiteSpace);
	nLen = strlen(strOrg);
	for(nPrevIndx=0; nPrevIndx<nLen; nPrevIndx++) {
		for(nWhCnt=0; nWhCnt<nWhSize; nWhCnt++) {
			if(strOrg[nPrevIndx] == strWhiteSpace[nWhCnt])
				break;
		}
		if(nWhCnt>=nWhSize)
			break;
	}
	if(nPrevIndx==nLen) {
		strOrg[0] = 0;
		return;
	}
	memmove(strOrg,strOrg+nPrevIndx,nLen-nPrevIndx);
	nLen = nLen-nPrevIndx;
	strOrg[nLen] = 0;
	for(nPostIndx=nLen-1; nPostIndx>0; nPostIndx--) {
		for(nWhCnt=0; nWhCnt<nWhSize; nWhCnt++) {
			if(strOrg[nPostIndx] == strWhiteSpace[nWhCnt])
				break;
		}
		if(nWhCnt>=nWhSize)
			break;
	}
	if(nPostIndx<nLen-1) {
		nLen = nPostIndx+1;
		strOrg[nLen] = 0;
	}
}

char * JS_UTIL_ExtractString(const char * strOrg, const  char * strPrevPattern, const char * strPostPattern, int nOrgLen, char * strBuff, int nBuffLen, int nIgnoreCase, int * pnIndexInOrg)
{
	int nPrevLen;
	int nPostLen;
	int nPrevIndx;
	int nPostIndx;
	int nStrLen;

	nPrevLen = strlen(strPrevPattern);
	nPostLen = strlen(strPostPattern);
	if(nOrgLen<=0)
		nOrgLen = strlen(strOrg);
	nPrevIndx = JS_UTIL_FindPattern(strOrg, strPrevPattern, nOrgLen, nPrevLen, nIgnoreCase);
	if(nPrevIndx<0)
		return NULL;
	nPostIndx = -1;
	if(nPostLen>0)
		nPostIndx = JS_UTIL_FindPattern(strOrg+nPrevIndx+nPrevLen, strPostPattern, nOrgLen-(nPrevIndx+nPrevLen), nPostLen, nIgnoreCase);
	if(nPostIndx<0) {
		nPostIndx = nOrgLen-(nPrevIndx+nPrevLen);
	}		
	nPostIndx += nPrevIndx+nPrevLen;
	nStrLen = nPostIndx-nPrevIndx-nPrevLen;
	if(nStrLen>nBuffLen) {
		DBGPRINT("Buff Len not enough! reduce str size %d to %d\n",nStrLen, nBuffLen);
		nStrLen = nBuffLen;
	}
	memcpy(strBuff,strOrg+nPrevIndx+nPrevLen,nStrLen);
	strBuff[nStrLen] = 0;
	if(pnIndexInOrg)
		*pnIndexInOrg = nPrevIndx+nPrevLen;
	return strBuff;
}

static int _JS_UTIL_CheckIsWhiteSpace(int nCh, const char * strSeperate, int nSepLen)
{
	int nCnt;
	for(nCnt=0; nCnt<nSepLen; nCnt++) {
		if(nCh==strSeperate[nCnt])
			break;
	}
	if(nCnt==nSepLen)
		return 0;
	else
		return 1;
}

int JS_UTIL_StrToken(const char * strOrg, int nOrgLen, int nCurIndx, const char * strSeperate, char * strBuff, int nBuffLen)
{
	int nTokenLen;
	int nStartPoint;
	int nCnt;
	int nSepLen;

	if(nOrgLen<=0)
		nOrgLen = strlen(strOrg);
	if(nCurIndx>=nOrgLen)
		return -1;
	if(nCurIndx<0)
		return -1;
	nSepLen = strlen(strSeperate);
	for(nStartPoint=nCurIndx; nStartPoint<nOrgLen; nStartPoint++) {
		if(_JS_UTIL_CheckIsWhiteSpace(strOrg[nStartPoint], strSeperate, nSepLen)==0)
			break;
	}
	if(nStartPoint>=nOrgLen)
		return -2;
	for(nCnt=nStartPoint; nCnt<nOrgLen; nCnt++) {
		if(_JS_UTIL_CheckIsWhiteSpace(strOrg[nCnt], strSeperate, nSepLen)==1)
			break;
	}
	nTokenLen = nCnt-nStartPoint;
	if(nTokenLen>nBuffLen) {
		DBGPRINT("WAK not enough token buff %d-->%d\n",nTokenLen, nBuffLen);
		nTokenLen = nBuffLen;
	}
	memcpy(strBuff,strOrg+nStartPoint,nTokenLen);
	strBuff[nTokenLen] = 0;
	return nCnt;
}

char * JS_UTIL_StrDup(const char * pStr)
{
	int nLen;
	char * pCopy;
	if(pStr==NULL)
		return NULL;
	nLen = strlen(pStr);
	pCopy = (char*)JS_ALLOC(nLen+4);
	if(pCopy) {
		JS_STRCPY(pCopy,pStr);
	}
	return pCopy;
}

int JS_UTIL_StrCpy(char * strTarget,const char * strSrc)
{
	int nLen;
	if(strTarget==NULL || strSrc==NULL)
		return -1;
	nLen = strlen(strSrc);
	memcpy(strTarget,strSrc,nLen);
	strTarget[nLen] = 0;
	return 0;
}

int JS_UTIL_StrPrint(char * strTarget, unsigned int nSize, const char* format, ... )
{
	va_list args;
	int nRet = 0;
	va_start( args, format );
	////TBD fix this: put zero at the end of string
	//nRet = vsnprintf(strTarget,nSize,format, args );
	nRet = vsnprintf (strTarget,nSize, format, args );
	va_end( args );
	return nRet;
}

int JS_UTIL_StrCopySafe(char * strTarget, int nBuffLen, const char * strSrc, int nSrcLen)
{
	if(nSrcLen<=0)
		nSrcLen = strlen(strSrc);
	if(nBuffLen<=nSrcLen)
		nSrcLen = nBuffLen-1;
	memcpy(strTarget,strSrc,nSrcLen);
	strTarget[nSrcLen] = 0;
	return nSrcLen;
}

int JS_UTIL_StrURLDecode(char *strSrc, char *strDest, int nBuffSize)
{
    int num=0, i, index=0;
    int retval=0;
	unsigned char nSrc;
    while((nSrc=(unsigned char)*strSrc)!=0) {
        if (nSrc == '%') {
            num = 0;
            retval = 0;
            for (i = 0; i < 2; i++) {
                strSrc++;
				nSrc = (unsigned char)*strSrc;
                if (nSrc < ':') {
                    num = nSrc - 48;
                }
                else if (nSrc > '@' && nSrc < '[') {
                    num = (nSrc - 'A')+10;
                }
                else {
                    num = (nSrc - 'a')+10;
                }
                if ((16*(1-i))) 
                    num = (num*16);
                retval += num;
            }
            strDest[index] = retval;
        }else {
            strDest[index] = *strSrc;
        }
		index++;
		if(nBuffSize<=index)
			break;
        strSrc++;
    }
	strDest[index] = 0;
    return index;
}

int JS_UTIL_StrURLEncode(char *strSrc, char *strDest, int nBuffSize)
{
    char hex[16];
    int size = 0;
	int nIndex = 0;
	unsigned char nSrc;
	memset(strDest,0,nBuffSize);
    while((nSrc=(unsigned char)*strSrc)!=0) {
        if ((nSrc > 47 && nSrc < 57) || 
            (nSrc > 64 && nSrc < 92) ||
            (nSrc > 96 && nSrc < 123) ||
            nSrc == '-' ||nSrc == '.' || nSrc == '_')
        {
            strDest[nIndex] = (char)nSrc; 
        }
        else {
            JS_UTIL_StrPrint(hex, 16,  "%%%02X",nSrc);
            JS_UTIL_StrCopySafe(strDest+nIndex, nBuffSize-nIndex, hex,0);
			nIndex += 2;
            size += 2;
        }
        strSrc++;
		nIndex++;
        size++;
    }
    return size;
}

int JS_HttpServer_URLDecode(char *strSrc, char *strDest, int nBuffSize)
{
	return JS_UTIL_StrURLDecode(strSrc,strDest,nBuffSize);
}

int JS_HttpServer_URLEncode(char *strSrc, char *strDest, int nBuffSize)
{
	return JS_UTIL_StrURLEncode(strSrc,strDest,nBuffSize);
}

int JS_UTIL_StrBase64Encode(char *strText, int numBytes, char **ppDest)
{
	unsigned char input[3]  = {0,0,0};
	unsigned char output[4] = {0,0,0,0};
	int   index, i, j, size;
	char *p, *plen;
	static const char MimeBase64[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/'
	};
	plen           = strText + numBytes - 1;
	size           = (4 * (numBytes / 3)) + (numBytes % 3? 4 : 0) + 1;
	(*ppDest) = (char*)JS_ALLOC(size);
	if(*ppDest == NULL)
		return 0;
	j              = 0;
	for  (i = 0, p = strText;p <= plen; i++, p++) {
		index = i % 3;
		input[index] = *p;
		if (index == 2 || p == plen) {
			output[0] = ((input[0] & 0xFC) >> 2);
			output[1] = ((input[0] & 0x3) << 4) | ((input[1] & 0xF0) >> 4);
			output[2] = ((input[1] & 0xF) << 2) | ((input[2] & 0xC0) >> 6);
			output[3] = (input[2] & 0x3F);
			(*ppDest)[j++] = MimeBase64[output[0]];
			(*ppDest)[j++] = MimeBase64[output[1]];
			(*ppDest)[j++] = index == 0? '=' : MimeBase64[output[2]];
			(*ppDest)[j++] = index <  2? '=' : MimeBase64[output[3]];
			input[0] = input[1] = input[2] = 0;
		}
	}
	(*ppDest)[j] = '\0';
	return size;
}

void JS_UTIL_EscapeXML(const char * strOrg, char * pBuff, int nLen)
{
	int nCnt;
	int nBuffCnt;
    int nOrgLen = strlen(strOrg);
	
	nBuffCnt = 0;
    for(nCnt=0; nCnt<nOrgLen; nCnt++) {
    	switch(strOrg[nCnt]) {
			case '&':  
				JS_UTIL_StrCopySafe(pBuff+nBuffCnt,nLen-nBuffCnt,"&amp;",0);
				nBuffCnt += 5;
				break;
			case '\"':  
				JS_UTIL_StrCopySafe(pBuff+nBuffCnt,nLen-nBuffCnt,"&quot;",0);
				nBuffCnt += 6;
				break;
			case '\'':  
				JS_UTIL_StrCopySafe(pBuff+nBuffCnt,nLen-nBuffCnt,"&apos;",0);
				nBuffCnt += 6;
				break;
			case '<':  
				JS_UTIL_StrCopySafe(pBuff+nBuffCnt,nLen-nBuffCnt,"&lt;",0);
				nBuffCnt += 4;
				break;
			case '>':  
				JS_UTIL_StrCopySafe(pBuff+nBuffCnt,nLen-nBuffCnt,"&gt;",0);
				nBuffCnt += 4;
				break;
			default:
				pBuff[nBuffCnt] = strOrg[nCnt];
				nBuffCnt++;
				break;
    	}
		if(nBuffCnt>=nLen-1)
			break;
    }
	pBuff[nBuffCnt] = 0;
}

UINT32	JS_UTIL_CheckSimilarString(char * pData1, char * pData2)
{
	UINT32	nRet=0;
	UINT32	nSum1=0;
	UINT32	nSum2=0;
	UINT32	nTotal, nDiff;
	int nLen1=0;
	int nLen2=0;
	int nCnt;

	if(nLen1<=0)
		nLen1 = strlen(pData1);
	if(nLen2<=0)
		nLen2 = strlen(pData2);
	if(nLen1<(nLen2<<1)/3 || nLen2<(nLen1<<1)/3)
		return 0;

	for(nCnt=0; nCnt<nLen1; nCnt++) {
		nSum1 += pData1[nCnt];
	}
	for(nCnt=0; nCnt<nLen2; nCnt++) {
		nSum2 += pData2[nCnt];
	}
	nTotal = nSum1 + nSum2;
	nDiff = JS_UTIL_GetAbsDiff(nSum1,nSum2);
	nRet = (nTotal-nDiff)*100/nTotal;
	return nRet;
}

char * JS_UTIL_StrJsonBuildStructStart(int nDefaultSize, int * pnOffset)
{
	char * pBuffer;
	pBuffer = (char*)JS_ALLOC(nDefaultSize);
	if(pBuffer) {
		pBuffer[0] = '{';
		pBuffer[1] = 0;
		*pnOffset = 1;
	}
	return pBuffer;
}

char * JS_UTIL_StrJsonBuildStructField(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, const char * strFieldValue)
{
	return JS_UTIL_StrJsonBuildStructFieldAll(pBuffer,pnBuffLen,pnOffset,strFieldName,strFieldValue,1);
}

char * JS_UTIL_StrJsonBuildStructFieldInterger(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, int nVal)
{
	char strIntBuff[64];
	JS_STRPRINTF(strIntBuff,60,"%d",nVal);
	return JS_UTIL_StrJsonBuildStructFieldAll(pBuffer,pnBuffLen,pnOffset,strFieldName,strIntBuff,0);
}

static char * JS_UTIL_StrJsonBuildStructFieldAll(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, const char * strFieldValue, int nIsString)
{
	int nNeedSize;
	int nBuffLen;
	int nOffset;
	int nRemain;

	nBuffLen = *pnBuffLen;
	nOffset  = *pnOffset;
	nRemain = nBuffLen-nOffset;
	nNeedSize = strlen(strFieldName);
	nNeedSize += strlen(strFieldValue) + 4;
	if(nRemain<nNeedSize+16) {
		nBuffLen = nBuffLen+(nNeedSize<<1);
		pBuffer = (char*)JS_REALLOC(pBuffer,nBuffLen);
		if(pBuffer==NULL) {
			DBGPRINT("build ajax struct item: mem error (pbuff)\n");
			return NULL;
		}
		nRemain = nBuffLen-nOffset;
	}
	if(nIsString)
		JS_STRPRINTF(pBuffer+nOffset,nRemain,"\"%s\": \"%s\",",strFieldName,strFieldValue);
	else
		JS_STRPRINTF(pBuffer+nOffset,nRemain,"\"%s\": %s,",strFieldName,strFieldValue);
	nOffset += strlen(pBuffer+nOffset);
	*pnOffset = nOffset;
	*pnBuffLen = nBuffLen;
	return pBuffer;
}

void JS_UTIL_StrJsonBuildStructEnd(char * pBuffer, int * pnBuffLen, int * pnOffset)
{
	char * strEnd;
	int nBuffLen;
	int nOffset;
	nBuffLen = *pnBuffLen;
	nOffset  = *pnOffset;
	strEnd = pBuffer+nOffset-1;
	if(strEnd[0]==',') {
		strEnd[0] = '}';
		pBuffer[nOffset] = 0;
	}else {
		pBuffer[nOffset] = '}';
		pBuffer[nOffset+1] = 0;
		*pnOffset = nOffset+1;
	}
}

char * JS_UTIL_StrJsonBuildArrayStart(int nDefaultSize, int * pnOffset)
{
	char * pBuffer;
	pBuffer = (char*)JS_ALLOC(nDefaultSize);
	if(pBuffer) {
		pBuffer[0] = '[';
		pBuffer[1] = 0;
		*pnOffset = 1;
	}
	return pBuffer;
}

char * JS_UTIL_StrJsonBuildArrayItem(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strJson)
{
	int nBuffLen;
	int nOffset;
	int nRemain;
	int nJsonLen;
	nBuffLen = *pnBuffLen;
	nOffset  = *pnOffset;
	nRemain = nBuffLen-nOffset;
	nJsonLen = strlen(strJson);
	if(nRemain<nJsonLen+16) {
		nBuffLen = nBuffLen+(nJsonLen<<1);
		pBuffer = (char*)JS_REALLOC(pBuffer,nBuffLen);
		if(pBuffer==NULL) {
			DBGPRINT("build ajax array item: mem error (pbuff)\n");
			return NULL;
		}
		nRemain = nBuffLen-nOffset;
	}
	JS_STRPRINTF(pBuffer+nOffset,nRemain,"%s,",strJson);
	nOffset += strlen(pBuffer+nOffset);
	*pnBuffLen = nBuffLen;
	*pnOffset  = nOffset;
	return pBuffer;
}

void JS_UTIL_StrJsonBuildArrayEnd(char * pBuffer, int * pnBuffLen, int * pnOffset)
{
	char * strEnd;
	int nBuffLen;
	int nOffset;
	nBuffLen = *pnBuffLen;
	nOffset  = *pnOffset;
	strEnd = pBuffer+nOffset-1;
	if(strEnd[0]==',') {
		strEnd[0] = ']';
		pBuffer[nOffset] = 0;
	}else {
		pBuffer[nOffset] = ']';
		pBuffer[nOffset+1] = 0;
		*pnOffset = nOffset+1;
	}
}


char * JS_UTIL_StrJsonParseArray(char * strTarget, int * pnTargetLen, char * strBuffer, int nBuffSize)
{
	int nTargetLen;
	char * strRet;
	int nCnt;
	int nCopySize;
	int nIsInString=0;
	int nIsInEscape=0;
	nTargetLen = *pnTargetLen;
	if(strTarget[0]=='[') {
		strRet=strTarget+1;
		nTargetLen = nTargetLen-1;
	}else
		strRet=strTarget;
	for(nCnt=0; nCnt<nTargetLen; nCnt++) {
		if(strRet[nCnt] == ',' && nIsInString==0) {
			break;
		}
		if(nIsInEscape==0 && strRet[nCnt] == '"') {
			if(nIsInString==0)
				nIsInString = 1;
			else
				nIsInString = 0;
		}
		nIsInEscape=0;
		if(strRet[nCnt] == '\\') {
			nIsInEscape = 1;
		}
	}
	if(nCnt>0) {
		nCopySize = nCnt;
		if(strRet[nCnt-1] == ']')
			nCopySize--;
		if(nCopySize>=nBuffSize)
			nCopySize = nBuffSize-1;
		if(nCopySize>0) {
			memcpy(strBuffer,strRet,nCopySize);
			strBuffer[nCopySize] = 0;
			strRet+=nCnt+1;
			nTargetLen-=nCnt+1;
			if(nTargetLen<0)
				nTargetLen = 0;
			*pnTargetLen = nTargetLen;
		}else
			strRet = NULL;
	}else
		strRet = NULL;
	return strRet;
}

///string functions end
/////////////////////////////////////////////////////////////



