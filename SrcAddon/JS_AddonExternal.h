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

#ifndef JS_ADDONEXTERNAL_H
#define JS_ADDONEXTERNAL_H

#ifdef JS_CONFIG_USE_ADDON_SIMPLEHTTPCLIENT
typedef void (*JS_FT_AJAXREQUEST_CALLBACK) (void * pOwner, const char * strReturn);
typedef int (*JS_FT_FILEEQUEST_CALLBACK) (void * pOwner, int nProgress, int nError, int nIsCompletd);
/////blocking functions
JS_HANDLE JS_SimpeHttpClient_SendAjaxRequest(const char * strURL, const char * strData, int nIsPost, JS_FT_AJAXREQUEST_CALLBACK pCallBack, void * pOwner, int nTimeoutMs, JS_HANDLE hItemLock);
JS_HANDLE JS_SimpeHttpClient_DownloadFile(const char * strURL, const char * strData, int nIsPost, const char * strDownPath, JS_FT_FILEEQUEST_CALLBACK pCallBack, void * pOwner, int nTimeoutMs, JS_HANDLE hItemLock);
JS_HANDLE JS_SimpeHttpClient_DownloadMultiFiles(const char * strFileListXml, JS_FT_FILEEQUEST_CALLBACK pCallBack, void * pOwner);
int JS_SimpeHttpClient_GetMultiDownloadStatus(JS_HANDLE hHttpAsync, char * pXMLBuff, int nBuffSize);
void JS_SimpleHttpClient_StopDownload(JS_HANDLE hHttpAsync);
void JS_SimpleHttpClient_StopAllConnection(void);
#endif

#ifdef JS_CONFIG_USE_ADDON_AJAXHELPER
int JS_AjaxHelper_CallJavascriptEvent(const char * strViewName, const char * strFunctionName, const char * strArg);
#endif

#ifdef JS_CONFIG_USE_ADDON_SIMPLEDISCCOVERY
#define JS_HELLO_OS_LINUX		1
#define JS_HELLO_OS_WIN32		2
#define JS_HELLO_OS_ANDROID		3
#define JS_HELLO_OS_IOS			4
#define JS_HELLO_OS_MAC			5
typedef int (*JS_FT_SIMPLEDISCOVERY_CALLBACK) (int nCmd, char * strDeviceName, char * strPhoneNumber, int nOS, char * strIP);
int JS_SimpleDiscovery_RegisterCallback(JS_FT_SIMPLEDISCOVERY_CALLBACK pfCallback);
int JS_SimpleDiscovery_RegisterDeviceName(const char * strDeviceName, const char * strPhoneNumber);
#endif

#ifdef JS_CONFIG_USE_ADDON_SIMPLECACHE
int JS_Cache_SetDefaultDirectory(const char * strPath);
int JS_Cache_GetCacheStatusXML(char * pBuff, int nSize);
int JS_Cache_AddExplicitCacheItem(int nNeedPermanent, const char * strURL, const char * strDir, const char * strFileName, int nOption);
int JS_Cache_AddExplicitCacheList(const char * strListXML, int nListSize);
int JS_Cache_CleanCacheLog(int nCompletedOnly);
int JS_Cache_StopAllExplicitDownload(void);
#endif

#endif /* JS_ADDONEXTERNAL_H */
