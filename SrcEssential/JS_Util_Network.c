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
#include "JS_ThreadPool.h"
#include "JS_DataStructure.h"

/*********************************************************************
Utility functions for TCP networks
**********************************************************************/

//#define JS_SOCKET_DEBUGGING

/////////////////////////////////////////////////////////////
///tcp functions start
JS_SOCKET_T JS_UTIL_TCP_TryConnect(unsigned int nTargetIP, unsigned short nPort)
{
	JS_SOCKET_T nSocketNew = -1;
    int newSockStat;
    struct sockaddr_in rcAddr;
    unsigned int nAddrSize;

	newSockStat = 0;
    nSocketNew = socket(AF_INET, SOCK_STREAM, 0);
    if(JS_UTIL_CheckSocketValidity(nSocketNew)<0) {
    	DBGPRINT("TryConnect: error no socket\n");
    	return -1;
    }
	JS_UTIL_SetSocketBlockingOption(nSocketNew,0);
    memset((char*)&rcAddr, 0, sizeof(rcAddr));
    rcAddr.sin_family = AF_INET;
    rcAddr.sin_addr.s_addr = nTargetIP;
    rcAddr.sin_port = htons(nPort);
    nAddrSize = sizeof(rcAddr);
    if(connect(nSocketNew, (struct sockaddr *)&rcAddr, nAddrSize) < 0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
        if (errno != EINPROGRESS)
#endif
        	goto LABEL_CONNECT_ERROR;
    }
    return nSocketNew;
LABEL_CONNECT_ERROR:
	if(nSocketNew>0)
		JS_UTIL_SocketClose(nSocketNew);
	return -1;
}

int JS_UTIL_TCP_GetSockError(JS_SOCKET_T nSock)
{
	int nRet = 0;
	int nTmp = 0;
    int nError = 0;
    int nESize;
    nESize = sizeof(int);
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
    if ((nTmp = getsockopt(nSock, SOL_SOCKET, SO_ERROR, (char*)&nError, &nESize)) < 0)
#else
    if ((nTmp = getsockopt(nSock, SOL_SOCKET, SO_ERROR, &nError, (socklen_t *)&nESize)) < 0)
#endif
    	nRet = -1;
    if(nError) {
        DBGPRINT("Connect: Socket error!\n");
        nRet = nError;
    }
    return nRet;
}

int JS_UTIL_TCP_CheckConnection(JS_SOCKET_T nSock)
{
	int nRet = 0;
	int nTmp = 0;
    int nError = 0;
    int nESize;
    nESize = sizeof(int);
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
    if ((nTmp = getsockopt(nSock, SOL_SOCKET, SO_ERROR, (char*)&nError, &nESize)) < 0)
#else
    if ((nTmp = getsockopt(nSock, SOL_SOCKET, SO_ERROR, &nError, (socklen_t *)&nESize)) < 0)
#endif
    	nRet = -1;
    if(nError) {
        DBGPRINT("Connect: Socket error!\n");
        nRet = -1;
    }
    return nRet;
}

JS_SOCKET_T JS_UTIL_TCP_ForceConnect(unsigned int nTargetIP, unsigned short nPort, unsigned int nWaitMSec, int * pnExitCmd)
{
    int newSockStat;
    int orgSockStat;
    int nRet, nTmp;
    JS_FD_T  rset, wset;
    struct timeval rcTval;
    JS_SOCKET_T nSocketNew;
    int nError = 0;
    int nESize;
    struct sockaddr_in rcAddr;
    unsigned int nAddrSize;
    int nLoopNumber;
    int nCnt;

	newSockStat = 0;
	orgSockStat = 0;
    nSocketNew = socket(AF_INET, SOCK_STREAM, 0);
    if(JS_UTIL_CheckSocketValidity(nSocketNew)<0) {
    	DBGPRINT("Connect: error no socket\n");
    	return -1;
    }
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	{
		u_long nMode = 1;
		ioctlsocket(nSocketNew, FIONBIO, &nMode);
	}
#else
    if ((newSockStat = fcntl(nSocketNew, F_GETFL, NULL)) < 0) {
        DBGPRINT("Connect: F_GETFL error\n");
        nError = -1;
        goto LABEL_FORCECONNECT_EXIT;
    }
    orgSockStat = newSockStat;
    newSockStat |= O_NONBLOCK;
    if(fcntl(nSocketNew, F_SETFL, newSockStat) < 0) {
    	DBGPRINT("Connect: F_SETLF error\n");
        nError = -1;
        goto LABEL_FORCECONNECT_EXIT;
    }
#endif
    memset((char*)&rcAddr, 0, sizeof(rcAddr));
    rcAddr.sin_family = AF_INET;
    rcAddr.sin_addr.s_addr = nTargetIP;
    rcAddr.sin_port = htons(nPort);
    nAddrSize = sizeof(rcAddr);
    if((nRet = connect(nSocketNew, (struct sockaddr *)&rcAddr, nAddrSize)) < 0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			errno = WSAGetLastError();
#else
        if (errno != EINPROGRESS) {
#endif
            nError = errno;
            goto LABEL_FORCECONNECT_EXIT;
        }
    }
    if (nRet == 0) {
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		{
			u_long nMode = 0;
			ioctlsocket(nSocketNew, FIONBIO, &nMode);
		}
#else
        fcntl(nSocketNew, F_SETFL, orgSockStat);
#endif
        return nSocketNew;
    }
    nLoopNumber = nWaitMSec/200;
    for(nCnt=0; nCnt<nLoopNumber; nCnt++) {
		JS_FD_ZERO(&rset);
		JS_FD_SET(nSocketNew, &rset);
		wset = rset;
		rcTval.tv_sec   = 0;
		rcTval.tv_usec    = 200000;
		nTmp = select(nSocketNew+1, &rset, &wset, NULL, &rcTval);
		if(nTmp != 0)
			break;
		if(pnExitCmd && *pnExitCmd==1){
			nError = ETIMEDOUT;
			break;
	    }
    }
    if(nTmp>0) {
        nESize = sizeof(int);
        nError = 59;
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		getsockopt(nSocketNew, SOL_SOCKET, SO_ERROR, (char*)&nError, &nESize);
#else
		getsockopt(nSocketNew, SOL_SOCKET, SO_ERROR, &nError, (socklen_t *)&nESize);
#endif        
    }else if(nTmp==0) {
    	nError = ETIMEDOUT;
    }else {
    	nError = -1;
    	DBGPRINT("TMP:Connection error select\n");
    }
LABEL_FORCECONNECT_EXIT:
    if(nError) {
        DBGPRINT("Connect: Socket error 0x%X errorcode=%d!\n",nTargetIP,nError);
    	if(nSocketNew>0)
    		JS_UTIL_SocketClose(nSocketNew);
    	nSocketNew = -1;
    }
    return nSocketNew;
}


int JS_UTIL_TCP_Recv(JS_SOCKET_T nSock, char * strBuff, int nLen, unsigned int nWaitMsec)
{
	JS_FD_T  rdSet;
	struct timeval rcTval;
	int nRet;
	int nErr=0;
	if(nWaitMsec != JS_RCV_WITHOUTSELECT) {
		JS_FD_ZERO(&rdSet);
		JS_FD_SET(nSock,&rdSet);
		rcTval.tv_sec   = nWaitMsec/1000;
		rcTval.tv_usec    = (nWaitMsec%1000)*1000;
		nRet = select(nSock+1, &rdSet, NULL, NULL, &rcTval);
	}else
		nRet = 1;
	if(nRet==0)
		return 0;
	else if(nRet>0) {
		nRet = recv(nSock,strBuff,nLen,0);
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		if(nRet < 0) {
			nErr = WSAGetLastError();
			if (nErr == WSAEWOULDBLOCK)
				nRet = 0;
			else {
				nRet = -1;
			}
		}
#else
		if(nRet < 0) {
			nErr = errno;
			if (nErr != EWOULDBLOCK && nErr != EAGAIN))
        		nRet = -1;
		}
#endif
	}else {
		nRet = -1;
		nErr = -9999;
	}

#ifdef JS_SOCKET_DEBUGGING
		if(nRet<0) {
			DBGPRINT("recv tcp error code = %d\n",nErr);
		}else if(nRet == 0) {
			DBGPRINT("recv tcp ret zero that means no data or rst/fin\n");
		}
#endif
		return nRet;
}

int JS_UTIL_TCP_SendTimeout(JS_SOCKET_T nSock, char * strBuff, int nLen, unsigned int nTimeMs)
{
	JS_FD_T  wrSet;
	struct timeval rcTval;
	int nRet;
	int nSendLen = 0;
	JS_FD_ZERO(&wrSet);
	//JS_FD_SET(nSock,&wrSet);
	FD_SET(nSock,&wrSet);
	rcTval.tv_sec = 0;
	rcTval.tv_usec = nTimeMs*1000;
	nRet = select(nSock+1, NULL, &wrSet, NULL, &rcTval);
	if(nRet>0 && JS_FD_ISSET(nSock,&wrSet))
		nSendLen = send(nSock,strBuff,nLen,MSG_NOSIGNAL);
	else if(nRet==0)
		return 0;
	if(nSendLen<=0 || nRet<0) {
#ifdef JS_SOCKET_DEBUGGING
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
		int nErr = WSAGetLastError();
#else
		int nErr = errno;
#endif
		DBGPRINT("send tcp error code = %d\n",nErr);
#endif
	//	DBGPRINT("TCPSEND:Send Error %d\n", errno);
		return -1;
	}
	return nSendLen;
}


int JS_UTIL_TCP_SendBlock(JS_SOCKET_T nSock, char * strBuff, int nLen)
{
	JS_FD_T  wrSet;
	struct timeval rcTval;
	int nRet;
	int nSendLen;
	int nRptCnt = 0;
	int nTotalLen = 0;
	//DBGPRINT("TMP: TCPSEND1\n");
LABEL_TCP_SEND_REPEAT:
	JS_FD_ZERO(&wrSet);
	JS_FD_SET(nSock,&wrSet);
	rcTval.tv_sec = 0;
	rcTval.tv_usec = 100000;
	nRet = select(nSock+1, NULL, &wrSet, NULL, &rcTval);
	if(nRet>0 && JS_FD_ISSET(nSock,&wrSet))
		nSendLen = send(nSock,strBuff+nTotalLen,nLen-nTotalLen,MSG_NOSIGNAL);
	else
		nSendLen = 0;
	if(nSendLen<0 || nRet<0) {
		//DBGPRINT("TCPSEND:Send Error %d\n", errno);
		return -1;
	}
	nTotalLen += nSendLen;
	if(nSendLen==0) {
		nRptCnt++;
		if(nRptCnt>10) {
			return -1;
		}
	}else
		nRptCnt = 0;
	if(nTotalLen<nLen)
		goto LABEL_TCP_SEND_REPEAT;
	//DBGPRINT("TMP: TCPSEND2\n");
	return nTotalLen;
}

void JS_UTIL_FdSet(JS_SOCKET_T nSock, fd_set* fdset)
{
#ifdef JS_SOCKET_DEBUGGING
	DBGPRINT("fdset sock=%d, fd=%u\n",nSock, (int)fdset);
	if(nSock<=0) {
		DBGPRINT("Wrong Socket Detected!!! sock=%d, fd=%u\n",nSock, (int)fdset);
	}
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	{
		unsigned int nCnt = 0;
		for(nCnt=0; nCnt<fdset->fd_count; nCnt++) {
			if(fdset->fd_array[nCnt]==nSock) {
				DBGPRINT("duplicate sock detected\n");
				break;
			}
		}
	}
#endif
#endif
	FD_SET(nSock,fdset);
}

void JS_UTIL_FdClr(JS_SOCKET_T nSock, fd_set* fdset)
{
#ifdef JS_SOCKET_DEBUGGING
	if(nSock<=0) {
		DBGPRINT("Wrong Socket Detected!!! sock=%d, fd=%u\n",nSock, (int)fdset);
	}
	DBGPRINT("fdclr sock=%d, fd=%u\n",nSock, (int)fdset);
#endif
	FD_CLR(nSock,fdset);
}

void JS_UTIL_SocketClose(JS_SOCKET_T nSock)
{
#ifdef JS_SOCKET_DEBUGGING
	DBGPRINT("sockclose sock=%d\n",nSock);
#endif
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	closesocket(nSock);
#else
	close(nSock);
#endif
}

void JS_UTIL_SetSocketBlockingOption(JS_SOCKET_T nSock, int nIsBlocking)
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	u_long nMode;
	if(nIsBlocking)
		nMode = 0;
	else
		nMode = 1;
	ioctlsocket(nSock, FIONBIO, &nMode);
#else
	int newSockStat;
	if ((newSockStat = fcntl(nSock, F_GETFL, NULL)) > 0) {
		if(nIsBlocking)
			newSockStat &= ~O_NONBLOCK;
		else
			newSockStat |= O_NONBLOCK;
		if(fcntl(nSock, F_SETFL, newSockStat) < 0) {
			DBGPRINT("IO: F_SETLF error nOutSock\n");
		}
	}
#endif
}

UINT32 JS_UTIL_StringToIP4(const char * strIP)
{
	UINT32 nIP = 0;
	nIP = inet_addr(strIP);
	return nIP;
}

char * JS_UTIL_IP4ToString(UINT32 nIP, char * pEnoughBuffer)
{
	JS_STRPRINTF(pEnoughBuffer,32,"%d.%d.%d.%d",(nIP)&0xFF,(nIP>>8)&0xFF,(nIP>>16)&0xFF,(nIP>>24)&0xFF);
	return pEnoughBuffer;
}

int JS_UTIL_CheckSocketValidity(JS_SOCKET_T nSocket) 
{
#if (JS_CONFIG_OS==JS_CONFIG_OS_WIN32)
	if(nSocket==INVALID_SOCKET)
		return -1;
	if(nSocket<=0)
		return -1;
#else
	if(nSocket<=0)
		return -1;
#endif
	return 1;
}
///tcp functions ends
/////////////////////////////////////////////////////////////




