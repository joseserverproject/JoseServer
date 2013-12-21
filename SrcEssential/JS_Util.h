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

#ifndef JS_UTIL_H_
#define JS_UTIL_H_
#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////////////
////macro 
#define JS_MAX_METHODLEN	16
#define JS_RCV_WITHOUTSELECT	0xFFFFFFFF
#define DBGPRINT	JS_UTIL_DebugPrint

#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	#define JS_MUTEX_LOCK(X)		EnterCriticalSection(X)
	#define JS_MUTEX_UNLOCK(X)		LeaveCriticalSection(X)
#else
	#define JS_MUTEX_LOCK(X)	 pthread_mutex_lock(X)
	#define JS_MUTEX_UNLOCK(X)  pthread_mutex_unlock(X)
#endif

#define JS_BROWSER_UNKNOWN		0x0
#define JS_BROWSER_IE			0x1
#define JS_BROWSER_CHROME		0x2
#define JS_BROWSER_FIREFOX		0x3

#define JS_REQSTATUS_WAITREQ			0x0
#define JS_REQSTATUS_READ_SINGLEBODY	0x1
#define JS_REQSTATUS_READ_MULTIHEADER	0x2
#define JS_REQSTATUS_READ_MULTIBODY		0x3
#define JS_REQSTATUS_WAITCGI			0x4
#define JS_REQSTATUS_ENDOFACTION		0x5
#define JS_REQSTATUS_NEEDTOCLOSE		0x6
#define JS_REQSTATUS_BYPASS				0x7

#define JS_RSPCODEGROUP_SUCCESS			2
#define JS_RSPCODEGROUP_REDIRECT		3
////macro end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////typedef
typedef  struct JS_StringBuffTag{
	char * pBuff;
	int	nBuffSize;
	int nOffset;
}JS_StringBuff;

typedef  struct JS_HTTP_RequestTag {
	char * pURL;
	char * pHost;
	int		nReqLen;
	int	    nUserAgent;
	int		nURLLen;
	int		nResOffset;
	char	strMethod[JS_MAX_METHODLEN];	
	UINT32  nTargetIP;
	UINT16  nTargetPort;
	HTTPSIZE_T	nRangeStartOffset;
	HTTPSIZE_T	nRangeEndOffset;
	HTTPSIZE_T	nRangeLen;
	int		nFieldNum;
	JS_HANDLE	hFieldList;	
	int		nQueueStatus;
	JS_HANDLE	hQueue;
	int		nQSLog;
	int nConnections;
	int nExplicitProxyFlag;
}JS_HTTP_Request;

typedef  struct JS_HTTP_ResponseTag {
	int	   nRspCode;
	int	   nRspLen;
	int	   nChunked;
	int	   nKeepAlive;
	HTTPSIZE_T	nRangeStartOffset;
	HTTPSIZE_T	nRangeEndOffset;
	HTTPSIZE_T	nRangeLen;
	int			nFieldNum;
	JS_HANDLE	hFieldList;
	HTTPSIZE_T	nSentOffset;
	UINT32	nPumpoutSpeed;
	UINT32	nPumpinSpeed;
	JS_HANDLE	hQueue;
}JS_HTTP_Response;

typedef struct JS_ConfigurationTag {
	////TBD
	int nMaxTurboConnection;
	int nUseJoseAgent;
}JS_Configuration;

typedef struct JS_UTIL_GlobalTag {
	int nInit;
	int nDbgLevel;
	JS_HANDLE hEventLock;
	int nEventPort;
	JS_HANDLE	hThreadPool;
	JS_HANDLE	hConnectionPool;
	void * pMemReport;
	unsigned int nTotalAlloc;
	JS_Configuration rcConfig;
}JS_UTIL_Global;
extern JS_UTIL_Global g_rcGlobal;
////typedef end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////functions for global data
int JS_UTIL_Init(void);
int JS_UTIL_Clear(void);
JS_Configuration * JS_UTIL_GetConfig(void);
////functions for global data end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for string
char * JS_UTIL_StrDup(const char * pStr);
int JS_UTIL_StrCpy(char * strTarget,const char * strSrc);
int JS_UTIL_StrPrint(char * strTarget, unsigned int nSize, const char* format, ... );
int JS_UTIL_StrCopySafe(char * strTarget, int nBuffLen, const char * strSrc, int nSrcLen);
int JS_UTIL_StrCmp(const char * strBuff, const char * strPattern, int nBuffLen, int nPatternLen, int nIgnoreCase);
int JS_UTIL_StrCmpRestrict(const char * strBuff, const char * strPattern, int nBuffLen, int nPatternLen, int nIgnoreCase);
int JS_UTIL_StrToken(const char * strOrg, int nOrgLen, int nCurIndx, const char * strSeperate, char * strBuff, int nBuffLen);
int JS_UTIL_FindPattern(const char * strBuff, const char * strFindPattern, int nBuffLen, int nPatternLen, int nIgnoreCase);
int JS_UTIL_FindPatternBinary(const char * strBuff, const char * strFindPattern, int nBuffLen, int nPatternLen, int nIgnoreCase);
char * JS_UTIL_ExtractString(const char * strOrg, const char * strPrevPattern, const char * strPostPattern, int nOrgLen, char * strBuff, int nBuffLen, int nIgnoreCase, int * pnIndexInOrg);
void JS_UTIL_TrimWhiteSpace(char * strOrg, const char * strWhiteSpace);

int JS_UTIL_StrURLDecode(char *strSrc, char *strDest, int nBuffSize);
int JS_UTIL_StrURLEncode(char *strSrc, char *strDest, int nBuffSize);
int JS_UTIL_StrBase64Encode(char *strText, int numBytes, char **ppDest);
void JS_UTIL_EscapeXML(const char * strOrg, char * pBuff, int nLen);
UINT32	JS_UTIL_CheckSimilarString(char * pData1, char * pData2);

#define JS_JSONTYPE_INT		0
#define JS_JSONTYPE_FLOAT	1
#define JS_JSONTYPE_STRING	2
#define JS_JSONTYPE_JSON	3

char * JS_UTIL_StrJsonBuildStructStart(int nDefaultSize, int * pnOffset);
char * JS_UTIL_StrJsonBuildStructField(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, const char * strFieldValue);
char * JS_UTIL_StrJsonBuildStructFieldInterger(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strFieldName, int nVal);
void JS_UTIL_StrJsonBuildStructEnd(char * pBuffer, int * pnBuffLen, int * pnOffset);

char * JS_UTIL_StrJsonBuildArrayStart(int nDefaultSize, int * pnOffset);
char * JS_UTIL_StrJsonBuildArrayItem(char * pBuffer, int * pnBuffLen, int * pnOffset, const char * strJson);
void JS_UTIL_StrJsonBuildArrayEnd(char * pBuffer, int * pnBuffLen, int * pnOffset);

char * JS_UTIL_StrJsonParseArray(char * strTarget, int * pnTargetLen, char * strBuffer, int nBuffSize);
////utils for string end
//////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////
////utils for http protocol
int JS_UTIL_HTTP_GetRspCodeGroup(JS_HTTP_Response * pRsp);
int JS_UTIL_HTTP_IsRspCriticalError(JS_HTTP_Response * pRsp);
int JS_UTIL_HTTP_IsPostMethod(JS_HTTP_Request * pReq);
int JS_UTIL_HTTP_IsHeadMethod(JS_HTTP_Request * pReq);
int JS_UTIL_ParseHTTPHost(const char * pHost, UINT32 * pnTargetIP, UINT16 * pnTargetPort);
JS_HTTP_Request	* JS_UTIL_HTTP_PrepareRequest(void);
JS_HTTP_Request * JS_UTIL_HTTP_BuildDefaultReq(JS_HTTP_Request * pReq, const char * strURL, const char * strData, const char * strMethod);
JS_HTTP_Request	* JS_UTIL_HTTP_CheckRequestPacket(const char * strReq, int nReqLen, JS_HTTP_Request * pOldItem);
void JS_UTIL_HTTP_DeleteRequest(JS_HTTP_Request	* pReq);
void JS_UTIL_HTTP_PrintRequest(JS_HTTP_Request	* pReq);
JS_HTTP_Response	* JS_UTIL_HTTP_PrepareResponse(void);
JS_HTTP_Response	* JS_UTIL_HTTP_CheckResponsePacket(const char * strRsp, int nRspLen, JS_HTTP_Response * pOldItem);
void JS_UTIL_HTTP_DeleteResponse(JS_HTTP_Response	* pRsp);
void JS_UTIL_HTTP_PrintResponse(JS_HTTP_Response	* pRsp);
int JS_UTIL_HTTPExtractRspRange(const char * pRspString, int nRspLen, HTTPSIZE_T * pnRangeLen, HTTPSIZE_T * pnRangeStart);
int JS_UTIL_HTTPExtractRspCode(const char * pRspString, int nRspLen);
char * JS_UTIL_HTTPExtractHostFromURL(const char * pURL, char * pBuff, int nBuffLen);
char * JS_UTIL_HTTP_CopyRequest(JS_HTTP_Request	* pReq, HTTPSIZE_T nRangeStart, HTTPSIZE_T nRangeLen, char * pData, int nDataLen, const char * strURL, int nIsProbe);
char * JS_UTIL_HTTP_BuildReqString(JS_HTTP_Request	* pReq, HTTPSIZE_T nRangeStart, HTTPSIZE_T nRangeLen, const char * strNewLocation, int nIsProbe);
int JS_UTIL_FixHTTPRequest(JS_HTTP_Request	* pReq, const char * strKey, const  char * strVal, int nNeedResultString);
int JS_UTIL_HTTPRequest_CompareHeader(JS_HTTP_Request	* pReq, const char * strKey, const  char * strVal);
int JS_UTIL_FixHTTPResponse(JS_HTTP_Response * pRsp, const char * strKey, const  char * strVal, int nRspCode, int nNeedResultString);
int JS_UTIL_HTTPResponse_CompareHeader(JS_HTTP_Response	* pRsp, const char * strKey, const  char * strVal);
const char * JS_UTIL_GetHTTPRequestHeader(JS_HTTP_Request	* pReq, const char * strKey);
const char * JS_UTIL_GetHTTPResponseHeader(JS_HTTP_Response	* pRsp, const char * strKey);
int JS_UTIL_HTTP_IsEndOfChunk(char * pData, int nSize);
int JS_UTIL_HTTP_GetReqResourceName(JS_HTTP_Request	* pReq, char * pBuffer, int nBuffLen);
int JS_UTIL_HTTP_CheckReqMethod(JS_HTTP_Request	* pReq, const char * strMethod);
void JS_UTIL_HTTP_GmtTimeString(char * pBuff, int nBuffLen, time_t *t);
void JS_UTIL_HTTP_MakeETAG(char *pBuff, int nBuffLen,const char * strPath, char * pLMBuff, int nLMBuffLen);
int JS_UTIL_HTTP_ConvertURLtoLocalFilePath(const char * strDir, const char * strFullResource, char * pBuffer , int nBuffLen, int nFullResLen);
////utils for http protocol end
//////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////
////utils for tcp
JS_SOCKET_T JS_UTIL_TCP_TryConnect(unsigned int nTargetIP, unsigned short nPort);
JS_SOCKET_T JS_UTIL_TCP_ForceConnect(unsigned int nTargetIP, unsigned short nPort, unsigned int nWaitMSec, int * npExitCmd);
int JS_UTIL_TCP_Recv(JS_SOCKET_T nSock, char * strBuff, int nLen, unsigned int nWaitMsec);
int JS_UTIL_TCP_SendBlock(JS_SOCKET_T nSock, char * strBuff, int nLen);
int JS_UTIL_TCP_SendTimeout(JS_SOCKET_T nSock, char * strBuff, int nLen, unsigned int nTimeMs);

void JS_UTIL_SocketClose(JS_SOCKET_T nSock);
void JS_UTIL_SetSocketBlockingOption(JS_SOCKET_T nSock, int nIsBlocking);
int JS_UTIL_TCP_CheckConnection(JS_SOCKET_T nSock);
int JS_UTIL_TCP_GetSockError(JS_SOCKET_T nSock);
UINT32 JS_UTIL_StringToIP4(const char * strIP);
char * JS_UTIL_IP4ToString(UINT32 nIP, char * pEnoughBuffer);
int JS_UTIL_CheckSocketValidity(JS_SOCKET_T nSocket);
void JS_UTIL_FdSet(JS_SOCKET_T nSock, fd_set* fdset);
void JS_UTIL_FdClr(JS_SOCKET_T nSock, fd_set* fdset);
////utils for tcp end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for time
unsigned int JS_UTIL_GetRoundDiff(unsigned int nVal1, unsigned int nVal2);
UINT32	JS_UTIL_GetAbsDiff(UINT32 nVal1, UINT32 nVal2);
UINT64 JS_UTIL_GetTickCount(void);
UINT32 JS_UTIL_GetSecondsFrom1970(void);
void JS_UTIL_Usleep(unsigned int nUsec);
////utils for time end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for debug
void JS_UTIL_HexPrint(char * pData, unsigned int nSize);
void JS_UTIL_FileDump(const char * strFileName, char * pData, unsigned int nSize);
void JS_UTIL_SetDbgLevel(int nLevel);
void JS_UTIL_DebugPrint(const char* format, ... );
void JS_UTIL_FrequentDebugMessage(int nID, int nFrq, const char* format, ... );
////utils for debug end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for thread communication
JS_HANDLE	JS_UTIL_SimpleEventCreate(const char * strEventName, int nOption, void * pFdSet, JS_SOCKET_T * pMaxFD);
int			JS_UTIL_SimpleEventDestroy(JS_HANDLE hEvent);
int			JS_UTIL_SimpleEventSend(JS_HANDLE hEvent, const char * pBuffer, int nLen);
int			JS_UTIL_SimpleEventRcv(JS_HANDLE hEvent, char * pBuffer, int nBuffLen, void * pRDSet);
////utils for thread communication end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for file operation
char * JS_UTIL_ExtractFileName(const char * strPath, char * pBuff, int nBuffSize);
JS_HANDLE   JS_UTIL_FileOpenBinary(const char * strName, int nIsRead, int nIsDefaultDir);
int			JS_UTIL_FileReadSome(JS_HANDLE hFile, char * pBuff, int nReadSize);
int			JS_UTIL_FileReadBlocking(JS_HANDLE hFile, char * pBuff, int nReadSize);
int			JS_UTIL_FileWriteBlocking(JS_HANDLE hFile, const char * pBuff, int nBuffSize);
int			JS_UTIL_FileWriteSome(JS_HANDLE hFile, char * pBuff, int nBuffSize);
void		JS_UTIL_FileDestroy(JS_HANDLE * phFile);
UINT64		JS_UTIL_GetFileSize(JS_HANDLE hFile);
UINT64		JS_UTIL_GetFileSizeByName(const char * strPath);
void		JS_UTIL_SetFilePos(JS_HANDLE hFile,UINT64 nPos);
int			JS_UTIL_FileRename(const char * strOldPath, const char * strNewPath);
int		    JS_UTIL_FIleExit(const char * strPath);
int		    JS_UTIL_FIleRemove(const char * strPath);
int			JS_UTIL_PrepareDirectory(const char * strDirectory);
////utils for file operation end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for mutex
JS_HANDLE JS_UTIL_CreateMutex(void);
void JS_UTIL_DestroyMutex(JS_HANDLE hMutex);
void JS_UTIL_LockMutex(JS_HANDLE hMutex);
void JS_UTIL_UnlockMutex(JS_HANDLE hMutex);
////utils for mutex end
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////utils for heap debugging
#if (JS_CONFIG_DEBUGMEMORY==1)
#define JS_ALLOC JS_Alloc
#define JS_FREE	 JS_Free
#define JS_REALLOC	 JS_Realloc
#else
#define JS_ALLOC malloc
#define JS_FREE	 free
#define JS_REALLOC	realloc
#endif
void * JS_Alloc(size_t nSize);
void JS_Free(void * pMem);
void * JS_Realloc(void * pMem, size_t nSize);
////utils for heap debugging end
//////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* JS_UTIL_H_ */
