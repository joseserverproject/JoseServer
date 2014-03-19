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
Utility functions 
**********************************************************************/

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"
#include "JS_Util.h"
#include "JS_ThreadPool.h"
#include "JS_DataStructure.h"
#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonInternal.h"
#endif 
#if (JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
#include <string.h>
#include <jni.h>
#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "JET-INAPI", __VA_ARGS__))
#endif
#if (JS_CONFIG_OS!=JS_CONFIG_OS_WIN32)
#include <pthread.h>
#endif

//////////////////////////////////////////////////////
//macro start
#define JS_UTIL_INIT_ID		0x12345678
#define MAX_MEM_REPORT_ITEM		80000

//////////////////////////////////////////////////
///local types
typedef struct JS_UTIL_SimpleEventTypeTag {
	JS_FD_T	* pReadFdSet;
	JS_SOCKET_T nSocket;
	char	* pName;
	UINT16	 nPort;
	struct sockaddr_in rcEvnetAddr;
}JS_UTIL_SimpleEventType;

typedef struct {
	void * pMem;
	int  nSize;
}JS_MemReportItem;

//////////////////////////////////////////////////
///local variables
JS_UTIL_Global g_rcGlobal;

//////////////////////////////////////////////////
///inner functions
static JS_HANDLE   JS_UTIL_FileOpenBinaryEx(const char * strName, int nIsRead, int nIsDefaultDir, const char * pOption);

//////////////////////////////////////////////////
///global function implementations
int JS_UTIL_Init(void)
{
    if(g_rcGlobal.nInit != JS_UTIL_INIT_ID) {	
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		WSADATA wsaData;
		WSAStartup( MAKEWORD(2,2), &wsaData );
#endif
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
		g_rcGlobal.rcConfig.nMaxTurboConnection = JS_CONFIG_NORMAL_TURBOCONNECTION;
		g_rcGlobal.rcConfig.nUseJoseAgent = JS_CONFIG_USE_PROXYAGENTASJOSE;
#endif
		g_rcGlobal.hEventLock = JS_UTIL_CreateMutex();
		g_rcGlobal.nEventPort = JS_CONFIG_PORT_SIMPLEEVENT;
#if (JS_CONFIG_DEBUGMEMORY==1)
		g_rcGlobal.pMemReport = malloc(sizeof(JS_MemReportItem)*MAX_MEM_REPORT_ITEM);
		g_rcGlobal.nTotalAlloc = 0;
		memset((char*)g_rcGlobal.pMemReport,0,sizeof(JS_MemReportItem)*MAX_MEM_REPORT_ITEM);
#endif
		g_rcGlobal.nDbgLevel = 1;
#if 1
		////make thread pool
		g_rcGlobal.hThreadPool = JS_ThreadPool_CreatePool(0);
		if(g_rcGlobal.hThreadPool==NULL)
			return -1;
#endif
#ifdef JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
		g_rcGlobal.hConnectionPool = JS_SimpleHttpClient_CreateConnectionPool(JS_CONFIG_TIME_MSEC_LIFEHTTP);
		if(g_rcGlobal.hConnectionPool==NULL)
			return -1;
#endif
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
		JS_AutoTrafficControl_Init();
#endif
		g_rcGlobal.nInit = JS_UTIL_INIT_ID;
		return 0;
	}
	return 0;
}

int JS_UTIL_Clear(void)
{
    if(g_rcGlobal.nInit == JS_UTIL_INIT_ID) {	
		g_rcGlobal.nInit = 0;
#if 1
		if(g_rcGlobal.hThreadPool)
			JS_ThreadPool_Destroy(g_rcGlobal.hThreadPool);
		g_rcGlobal.hThreadPool = NULL;
#endif
#ifdef JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
		if(g_rcGlobal.hConnectionPool)
			JS_SimpleHttpClient_DestroyConnectionPool(g_rcGlobal.hConnectionPool);
#endif
#ifdef JS_CONFIG_USE_ADDON_MEDIAPROXY
		JS_AutoTrafficControl_Clear();
#endif

#if (JS_CONFIG_DEBUGMEMORY==1)
		DBGPRINT("Total Mem Remain: %u\n",g_rcGlobal.nTotalAlloc);
		g_rcGlobal.nTotalAlloc = 0;
		if(g_rcGlobal.pMemReport)
			free(g_rcGlobal.pMemReport);
		g_rcGlobal.pMemReport = NULL;
#endif
		if(g_rcGlobal.hEventLock)
			JS_UTIL_DestroyMutex(g_rcGlobal.hEventLock);
		g_rcGlobal.hEventLock = NULL;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		WSACleanup();
#endif
	}
	return 0;
}

JS_Configuration * JS_UTIL_GetConfig(void)
{
    return &g_rcGlobal.rcConfig;
}

/////////////////////////////////////////////////////////////
////time,etc functions start
void JS_UTIL_SetDbgLevel(int nLevel)
{
	g_rcGlobal.nDbgLevel = nLevel;
}

void JS_UTIL_FrequentDebugMessage(int nID, int nFrq, const char* format, ... )
{
    va_list args;
	static int nInit = 0;
	static int arrIDMap[64];
    if(g_rcGlobal.nDbgLevel<=0)
    	return;
	if(nInit==0) {
		nInit=1;
		memset((char*)arrIDMap,0,sizeof(arrIDMap));
	}
	if(nID>=64)
		return;
	if(arrIDMap[nID]++>nFrq) {
		char strTemp[5000];
		arrIDMap[nID]=0;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		{
			va_start(args, format);
			vsnprintf(strTemp,4800,format, args );
			OutputDebugString((LPCSTR)strTemp);
		}
#else
		{
#if (JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
			va_start( args, format );
			vsnprintf(strTemp,4800, format, args );
			LOGI("%s",strTemp);
#else
			va_start( args, format );
			vsnprintf(strTemp,4800, format, args );
			printf("%s",strTemp);
#endif
		}
#endif
	}
}

void JS_UTIL_DebugPrint(const char* format, ... )
{
    va_list args;
	char strTemp[5000];
    if(g_rcGlobal.nDbgLevel>0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		va_start(args, format);
		vsnprintf(strTemp,4800,format, args );
		OutputDebugString((LPCSTR)strTemp);
#else
#if (JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
		va_start( args, format );
		vsnprintf(strTemp,4800, format, args );
		LOGI("%s",strTemp);
#else
		va_start( args, format );
		vsnprintf(strTemp,4800, format, args );
		printf("%s",strTemp);
#endif
#endif
		va_end(args);
    }
}

UINT64 JS_UTIL_GetTickCount(void)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	return GetTickCount64();
#else
	UINT64	nURet64;
	struct timeval tv;
	gettimeofday (&tv, NULL);
	nURet64 = (UINT64)tv.tv_sec;
	nURet64 = nURet64 * 1000 + tv.tv_usec / 1000;
	return nURet64;
#endif
}

unsigned int JS_UTIL_GetRoundDiff(unsigned int nVal1, unsigned int nVal2)
{
	unsigned int nDiff;
	if(nVal1<=nVal2)
		nDiff = nVal2-nVal1;
	else
		nDiff = nVal1+(0xFFFFFFFF-nVal2);
	return nDiff;
}

void JS_UTIL_HexPrint(char * pData, unsigned int nSize)
{
#define JS_COL_LEN	8
#define DPR	DBGPRINT
	int nCnt;
	int nRow;
	int nCol;
	int nRCnt;
	unsigned int nLevel;
	nRow = nSize/JS_COL_LEN;
	for(nRCnt=0; nRCnt<=nRow; nRCnt++) {
		nLevel = nRCnt*JS_COL_LEN;
		if(nLevel>nSize) {
			nCol = nRow%JS_COL_LEN;
		}else {
			nCol = JS_COL_LEN;
		}
		if(nCol==0)
			break;
		DPR("%06x: ",nLevel);
		for(nCnt=0; nCnt<nCol; nCnt++) {
			DPR("%02x ",(unsigned char)pData[nLevel+nCnt]);
		}
		DPR(" : ");
		for(nCnt=0; nCnt<nCol; nCnt++) {
			if(pData[nLevel+nCnt]>=0x21 && pData[nLevel+nCnt]<=0x7E)
				DPR("%c",pData[nLevel+nCnt]);
			else
				DPR(".");
		}
		DPR("\n");
	}
}

void JS_UTIL_FileDump(const char * strFileName, char * pData, unsigned int nSize)
{
	JS_HANDLE hFile;
	hFile = JS_UTIL_FileOpenBinaryEx(strFileName,0,0,"a");
	if(hFile) {
		JS_UTIL_FileWriteBlocking(hFile,pData,nSize);
		JS_UTIL_FileDestroy(&hFile);
	}
}

UINT32	JS_UTIL_GetAbsDiff(UINT32 nVal1, UINT32 nVal2)
{
	if(nVal1<nVal2)
		return nVal2-nVal1;
	else
		return nVal1-nVal2;
}


UINT32 JS_UTIL_GetSecondsFrom1970(void)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	time_t   current_time;
	time( &current_time);
	return (UINT32)current_time;
#else
	UINT32 nRet = 0;
	struct timeval tv;
	gettimeofday (&tv, NULL);
	nRet = (UINT64)tv.tv_sec;
	return nRet;
#endif
}

JS_HANDLE JS_UTIL_CreateMutex(void)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	CRITICAL_SECTION	* pSection;
	pSection = (CRITICAL_SECTION*) JS_ALLOC (sizeof(CRITICAL_SECTION));
	if(pSection)
		InitializeCriticalSection(pSection);
	return pSection;
#else
	pthread_mutex_t * pMutex;
	int nRet;
	pMutex = (pthread_mutex_t *) JS_ALLOC (sizeof(pthread_mutex_t));
	if(pMutex) {
		nRet = pthread_mutex_init(pMutex, NULL);
		if(nRet!=0) {
			JS_FREE(pMutex);
			pMutex = NULL;
		}
	}
	return (JS_HANDLE)pMutex;
#endif
}

void JS_UTIL_DestroyMutex(JS_HANDLE hMutex)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	CRITICAL_SECTION	* pSection = (CRITICAL_SECTION	*)hMutex;
	if(pSection) {
		DeleteCriticalSection(pSection);
		JS_FREE(pSection);
	}
#else
	pthread_mutex_t * pMutex=(pthread_mutex_t * )hMutex;
	if(pMutex) {
		pthread_mutex_destroy(pMutex);
		JS_FREE(pMutex);
	}
#endif
}

void JS_UTIL_LockMutex(JS_HANDLE hMutex)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	CRITICAL_SECTION	* pMutex = (CRITICAL_SECTION	*)hMutex;
#else
	pthread_mutex_t * pMutex=(pthread_mutex_t * )hMutex;
#endif
	if(pMutex)
		JS_MUTEX_LOCK(pMutex);

}

void JS_UTIL_UnlockMutex(JS_HANDLE hMutex)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	CRITICAL_SECTION	* pMutex = (CRITICAL_SECTION	*)hMutex;
#else
	pthread_mutex_t * pMutex=(pthread_mutex_t * )hMutex;
#endif
	if(pMutex)
		JS_MUTEX_UNLOCK(pMutex);
}


void JS_UTIL_Usleep(unsigned int nUsec)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_LINUX || JS_CONFIG_OS==JS_CONFIG_OS_ANDROID)
	usleep(nUsec);
#elif (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	Sleep(nUsec/1000);
#endif
}

JS_HANDLE	JS_UTIL_SimpleEventCreate(const char * strEventName, int nOption, void * pFdSet, JS_SOCKET_T * pMaxFD)
{
	JS_UTIL_SimpleEventType * pEvent = NULL;
	JS_SOCKET_T nSock;
	int nCnt = 0;
	int nOpt;
	int nSockOK = 0;
	struct sockaddr_in serveraddr;

	pEvent = (JS_UTIL_SimpleEventType *)JS_ALLOC(sizeof(JS_UTIL_SimpleEventType));
	if(pEvent) {
	    nSock = socket(AF_INET, SOCK_DGRAM, 0);
	    if(JS_UTIL_CheckSocketValidity(nSock)<0) {
			DBGPRINT("Cant' Make event socket %d\n",nCnt);
			JS_FREE(pEvent);
			pEvent = NULL;
			goto LABEL_END_OF_CREATEEVENT;
	    }
		memset((char*)&serveraddr,0, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
		nOpt = 1;
		setsockopt(nSock, SOL_SOCKET, SO_REUSEADDR, (char*)&nOpt, sizeof(nOpt));

		for(nCnt=0; nCnt<500; nCnt++) {
			JS_UTIL_LockMutex(g_rcGlobal.hEventLock);
			serveraddr.sin_port = htons(g_rcGlobal.nEventPort++);
			pEvent->nPort = serveraddr.sin_port;
			if(bind(nSock, (struct sockaddr *)&serveraddr,sizeof(serveraddr))>=0) {
				nSockOK = 1;
				JS_UTIL_UnlockMutex(g_rcGlobal.hEventLock);
				break;
			}
			JS_UTIL_UnlockMutex(g_rcGlobal.hEventLock);
			DBGPRINT("Can't bind event socket %d\n",nCnt);
		}
		if(nSockOK==0) {
			JS_UTIL_SocketClose(nSock);
			DBGPRINT("Event Failure!\n");
			JS_FREE(pEvent);
			pEvent = NULL;
		}else {
			if(strEventName)
				pEvent->pName = JS_UTIL_StrDup(strEventName);
			else
				pEvent->pName = NULL;
			pEvent->nSocket = nSock;
			pEvent->pReadFdSet = (JS_FD_T*)pFdSet;
			if(pMaxFD && *pMaxFD<nSock)
				*pMaxFD = nSock+1;
			if(pFdSet)
				JS_FD_SET(nSock,pEvent->pReadFdSet);
			pEvent->rcEvnetAddr = serveraddr;
			pEvent->rcEvnetAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
		}
	}
LABEL_END_OF_CREATEEVENT:
	return (JS_HANDLE)pEvent;
}

int	JS_UTIL_SimpleEventDestroy(JS_HANDLE hEvent)
{
	JS_UTIL_SimpleEventType * pEvent = (JS_UTIL_SimpleEventType *)hEvent;
	if(pEvent) {
		if(pEvent->nSocket>0) {
			JS_UTIL_SocketClose(pEvent->nSocket);
		}
		if(pEvent->pName) {
			JS_FREE(pEvent->pName);
		}
		JS_FREE(pEvent);
	}
	return 0;
}

int	JS_UTIL_SimpleEventSend(JS_HANDLE hEvent, const char * pBuffer, int nLen)
{
	JS_UTIL_SimpleEventType * pEvent = (JS_UTIL_SimpleEventType *)hEvent;
	if(pEvent && pEvent->nSocket>0)
		sendto(pEvent->nSocket,pBuffer, nLen, 0,
		    ( struct sockaddr*)&pEvent->rcEvnetAddr, sizeof(pEvent->rcEvnetAddr));
	return 0;
}

int	JS_UTIL_SimpleEventRcv(JS_HANDLE hEvent, char * pBuffer, int nBuffLen, void * pRDSet)
{
	JS_FD_T * prcRDSet = (JS_FD_T *)pRDSet;
	JS_UTIL_SimpleEventType * pEvent = (JS_UTIL_SimpleEventType *)hEvent;
	int nRcvOK = 0;
	int nRet = 0;
	struct sockaddr_in rcSenderAddr;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	int nRcvLen;
#else
	unsigned int nRcvLen;
#endif

	if(pEvent && pEvent->nSocket>0) {
		if(prcRDSet && JS_FD_ISSET(pEvent->nSocket,prcRDSet))
			nRcvOK = 1;
		else if(prcRDSet==NULL)
			nRcvOK = 1;
		if(nRcvOK) {
			nRcvLen = sizeof (rcSenderAddr);
			nRet = recvfrom(pEvent->nSocket, pBuffer, nBuffLen, 0, (struct sockaddr*)&rcSenderAddr, &nRcvLen);
			if(nRet<0) {
				DBGPRINT("EventSock: ev recv err\n");
			}
		}
	}
	return nRet;
}

char * JS_UTIL_ExtractFileName(const char * strPath, char * pBuff, int nBuffSize)
{
	int nLen = strlen(strPath);
	int nOffset;

	for(nOffset = nLen-1; nOffset>=0; nOffset--) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		if(strPath[nOffset] == '\\')
#else
		if(strPath[nOffset] == '/')
#endif
			break;
	}
	nOffset++;
	JS_UTIL_StrCopySafe(pBuff,nBuffSize,strPath+nOffset,nLen-nOffset);
	return pBuff;
}

#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
static wchar_t* JS_UTIL_UTFToWide(const char * strSrc)
{
	int iRet;
	int iDestBufferSize;
	LPWSTR  pwszStr;
	iDestBufferSize = MultiByteToWideChar( CP_UTF8, 0, strSrc, -1, NULL, 0 );
	pwszStr =  (WCHAR*)JS_ALLOC(sizeof(WCHAR)*(iDestBufferSize+1));
	if( NULL == pwszStr) {
		return FALSE;
	} 
	iRet = MultiByteToWideChar( CP_UTF8, 0, strSrc, -1, pwszStr, iDestBufferSize );
	if( 0 < iRet ) {
		(pwszStr)[iRet - 1]= 0;
	} 
	iDestBufferSize = iRet;
	return pwszStr;
}
#endif

JS_HANDLE   JS_UTIL_FileOpenBinary(const char * strName, int nIsRead, int nIsDefaultDir)
{
	return JS_UTIL_FileOpenBinaryEx(strName,nIsRead,nIsDefaultDir,NULL);
}

static JS_HANDLE   JS_UTIL_FileOpenBinaryEx(const char * strName, int nIsRead, int nIsDefaultDir, const char * pOption)
{
	FILE * pFile=NULL;
	char strTag[8];
	if(pOption)
		JS_STRPRINTF(strTag,4,"%s",pOption);
	else if(nIsRead) {
		JS_STRPRINTF(strTag,4,"rb");
	}else {
		JS_STRPRINTF(strTag,4,"wb");
	}
	pFile = fopen(strName,strTag);
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	if(pFile==NULL) {
		wchar_t * pWideFileName  = JS_UTIL_UTFToWide(strName);
		if(pWideFileName) {
			if(nIsRead) {
				pFile = _wfopen(pWideFileName,L"rb");
			}else {
				pFile = _wfopen(pWideFileName,L"wb");
			}
			JS_FREE(pWideFileName);
		}
	}
#endif
	return (JS_HANDLE)pFile;
}

void	JS_UTIL_FileDestroy(JS_HANDLE * phFile)
{
	FILE * pFile;
	if(phFile==NULL)
		return;
	pFile = (FILE*)*phFile;
	if(pFile) {
		fclose(pFile);
		*phFile = NULL;
	}
}

int	JS_UTIL_FileReadSome(JS_HANDLE hFile, char * pBuff, int nReadSize)
{
	FILE * pFile;
	int nSize;

	pFile = (FILE*)hFile;
	nSize = fread(pBuff,1,nReadSize,pFile);
	return nSize;
}

int	JS_UTIL_FileWriteSome(JS_HANDLE hFile, char * pBuff, int nBuffSize)
{
	FILE * pFile;
	int nSize;

	pFile = (FILE*)hFile;
	nSize = fwrite(pBuff,1,nBuffSize,pFile);
	return nSize;
}

int	JS_UTIL_FileReadBlocking(JS_HANDLE hFile, char * pBuff, int nReadSize)
{
	FILE * pFile;
	int nRet = 0;
	int nErrCnt = 0;
	int nSize;
	int nRemain = nReadSize;
	int nOffset = 0;

	pFile = (FILE*)hFile;
	while(1) {
		nSize = fread(pBuff+nOffset,1,nRemain,pFile);
		if(nSize<=0) {
			nErrCnt++;
		}
		if(nErrCnt>10) {
			nRet = -1;
			break;
		}
		nRemain -= nSize;
		nOffset += nSize;
		if(nRemain<=0)
			break;
	}
	nRet = nReadSize;
	return nRet;
}

int JS_UTIL_FileWriteBlocking(JS_HANDLE hFile, const char * pBuff, int nBuffSize)
{
	FILE * pFile;
	int nRet = 0;
	int nErrCnt = 0;
	int nSize;
	int nRemain = nBuffSize;
	int nOffset = 0; 

	pFile = (FILE*)hFile;
	while(1) {
		nSize = fwrite(pBuff+nOffset,1,nRemain,pFile);
		if(nSize<=0) {
			nErrCnt++;
		}
		if(nErrCnt>10) {
			nRet = -1;
			break;
		}
		nRemain -= nSize;
		nOffset += nSize;
		if(nRemain<=0)
			break;
	}
	nRet = nBuffSize;
	return nRet;
}

UINT64 JS_UTIL_GetFileSizeByName(const char * strPath)
{
	UINT64 nSize = 0;
	FILE * pFile = fopen(strPath,"rb");
	if(pFile==NULL)
		return 0;
	////check size
	fseek(pFile,0,SEEK_END);
	nSize = ftell(pFile);
	fseek(pFile,0,SEEK_SET);
	fclose(pFile);
	return nSize;
}

UINT64 JS_UTIL_GetFileSize(JS_HANDLE hFile)
{
	UINT64 nSize = 0;
	FILE * pFile = (FILE*) hFile;
	////check size
	fseek(pFile,0,SEEK_END);
	nSize = ftell(pFile);
	fseek(pFile,0,SEEK_SET);
	return nSize;
}

void JS_UTIL_SetFilePos(JS_HANDLE hFile,UINT64 nPos)
{
	FILE * pFile = (FILE*) hFile;

	fseek(pFile,(long)nPos,SEEK_SET);
}

int	JS_UTIL_FileRename(const char * strOldPath, const char * strNewPath)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	MoveFile(strOldPath,strNewPath);
#else
	return rename(strOldPath,strNewPath);
#endif
	return 0;
}

int JS_UTIL_FIleExit(const char * strPath)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	DWORD nFType = GetFileAttributesA(strPath);
	if (nFType == INVALID_FILE_ATTRIBUTES)
		return 0;
	return 1; 
#else
	if(access( strPath, F_OK ) != -1 ) {
		return 1;
	} else {
		return 0;
	}
#endif
}

int JS_UTIL_FIleRemove(const char * strPath)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	if(DeleteFile(strPath) != 0) {
		DWORD nRet = GetLastError();
		if(nRet == 0)
			nRet = 0;
	}
#else
	remove(strPath);
#endif
	return 0;
}

int	JS_UTIL_PrepareDirectory(const char * strDirectory) 
{
	if(JS_UTIL_FIleExit(strDirectory))
		return 0;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	CreateDirectory(strDirectory,NULL);
#else
	mkdir(strDirectory,S_IRUSR|S_IWUSR);
#endif
	return 0;
}

void * JS_Realloc(void * pMem, size_t nSize)
{
	void * pNewMem = realloc(pMem,nSize);
#if (JS_CONFIG_DEBUGMEMORY==1)
	if(g_rcGlobal.pMemReport) {
		int nCnt;
		JS_MemReportItem * pReport = (JS_MemReportItem * )g_rcGlobal.pMemReport;
		JS_UTIL_LockMutex(g_rcGlobal.hEventLock);
		for(nCnt=0; nCnt<MAX_MEM_REPORT_ITEM; nCnt++) {
			if(pReport[nCnt].pMem == pMem)
				break;
		}
		if(nCnt<MAX_MEM_REPORT_ITEM) {
			g_rcGlobal.nTotalAlloc -= pReport[nCnt].nSize;
			g_rcGlobal.nTotalAlloc += nSize;
			pReport[nCnt].pMem = pNewMem;
			pReport[nCnt].nSize = nSize;
		}
		JS_UTIL_UnlockMutex(g_rcGlobal.hEventLock);
	}
#endif
	return pNewMem;
}


void * JS_Alloc(size_t nSize)
{
	void * pRet = malloc(nSize);
#if (JS_CONFIG_DEBUGMEMORY==1)
	if(pRet && g_rcGlobal.pMemReport) {
		int nCnt;
		JS_MemReportItem * pReport = (JS_MemReportItem * )g_rcGlobal.pMemReport;
		JS_UTIL_LockMutex(g_rcGlobal.hEventLock);
		for(nCnt=0; nCnt<MAX_MEM_REPORT_ITEM; nCnt++) {
			if(pReport[nCnt].pMem == NULL)
				break;
		}
		if(nCnt<MAX_MEM_REPORT_ITEM) {
			pReport[nCnt].pMem = pRet;
			pReport[nCnt].nSize = nSize;
			g_rcGlobal.nTotalAlloc += nSize;
		}else {
			DBGPRINT("out of dbg alloc\n");
		}
		JS_UTIL_UnlockMutex(g_rcGlobal.hEventLock);
		if(nSize==44)
			nSize = 44;
	}
#endif
	return pRet;
}

void JS_Free(void * pMem)
{
	free(pMem);
#if (JS_CONFIG_DEBUGMEMORY==1)
	if(g_rcGlobal.pMemReport) {
		int nCnt;
		JS_MemReportItem * pReport = (JS_MemReportItem * )g_rcGlobal.pMemReport;
		JS_UTIL_LockMutex(g_rcGlobal.hEventLock);
		for(nCnt=0; nCnt<MAX_MEM_REPORT_ITEM; nCnt++) {
			if(pReport[nCnt].pMem == pMem)
				break;
		}
		if(nCnt<MAX_MEM_REPORT_ITEM) {
			g_rcGlobal.nTotalAlloc -= pReport[nCnt].nSize;
			pReport[nCnt].pMem = NULL;
			pReport[nCnt].nSize = 0;
		}
		JS_UTIL_UnlockMutex(g_rcGlobal.hEventLock);
	}
#endif
}


////time,etc functions start
/////////////////////////////////////////////////////////////


