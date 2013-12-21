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

#ifndef JS_INTERFACE_H_
#define JS_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define JS_SERVICE_HTTP				1
#define JS_SERVICE_PROXY				2
#define JS_SERVICE_SIMPLEDISCOVERY		4

typedef void * JS_HANDLE;
typedef int (*JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK) (JS_HANDLE hSession);

int JS_InitGlobal(void);
int JS_ClearGlobal(void);

JS_HANDLE JS_CreateJose(int nServiceMask, unsigned short nPort, int nIsAutoPort);
void JS_DestroyJose(JS_HANDLE hJose);
int JS_StartJose(JS_HANDLE hJose);
int JS_StopJose(JS_HANDLE hJose);
unsigned short JS_GetJosePort(JS_HANDLE hJose);
int JS_SetJoseDebugOption(JS_HANDLE hJose, int nOption);
int JS_JoseCommand(JS_HANDLE hJose, const char * strCmd, char * strResult, int nBuffLen);
int JS_ChangeConfigOption(JS_HANDLE hJose, int nConfigID, UINT64 nNewValue);

////functions for http management
int JS_HttpServer_ChangeMimeType(JS_HANDLE hJose, const char * strExtention, const char * strMime);
int JS_HttpServer_RegisterDocumentRoot(JS_HANDLE hJose, const char * strDirName);
int JS_HttpServer_RegisterUploadRoot(JS_HANDLE hJose, const char * strUploadDirName);
int JS_HttpServer_RegisterAccessControl(JS_HANDLE hJose, const char * strFilePath, int nEnable);
int JS_HttpServer_RegisterDirectAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfDirect);
int JS_HttpServer_RegisterAsyncRawAPI(JS_HANDLE hJose, const char * strResourceName, JS_FT_HORSEHTTP_DIRECTAPI_CALLBACK pfAsync);

////functions for http session (used in the direct api function or async api function)
int JS_HttpServer_AsyncWorkCompleted(JS_HANDLE hSession);
int JS_HttpServer_GetVariableFromReq(JS_HANDLE hSession, const char * strKey, char * pBuffer, int nBuffLen);
int JS_HttpServer_GetVariableFromReqWithURLDecode(JS_HANDLE hSession, const char * strKey, char * pBuffer, int nBuffLen);
int JS_HttpServer_GetFileListUploaded(JS_HANDLE hSession, char * pBuffer, int nBuffLen);
int JS_HttpServer_GetPeerIP(JS_HANDLE hSession, char * strIP, int nBuffLen);
int JS_HttpServer_SendHeaderRaw(JS_HANDLE hSession, const char * strHeader, int nHeaderLen);
int JS_HttpServer_SendBodyData(JS_HANDLE hSession, const char * strBodyData, int nBodyLen);
int JS_HttpServer_SendQuickXMLRsp(JS_HANDLE hSession,const char * strRsp);
int JS_HttpServer_SendQuickTextRsp(JS_HANDLE hSession,const char * strRsp);
int JS_HttpServer_SendQuickJsonRsp(JS_HANDLE hSession,const char * strRsp);
int JS_HttpServer_SendQuickErrorRsp(JS_HANDLE hSession,int nErrorCode, const char * strErrorString);
int JS_HttpServer_SendFileWithHeader(JS_HANDLE hSession, const char * strPath);
int JS_HttpServer_SetMimeTypeAsDownloadable(JS_HANDLE hSession);

/////string functions
int JS_HttpServer_URLDecode(char *strSrc, char *strDest, int nBuffSize);
int JS_HttpServer_URLEncode(char *strSrc, char *strDest, int nBuffSize);


#if JS_CONFIG_USE_ADDON==1
#include "JS_AddonExternal.h"
#endif 

#ifdef __cplusplus
}
#endif
#endif /* JS_API_H_ */
