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

#ifndef JS_THREADPOOL_H_
#define JS_THREADPOOL_H_
#ifdef __cplusplus
extern "C" {
#endif

#define JS_THREAD_STATUS_NOTSTARTED	0
#define JS_THREAD_STATUS_IDLE		1
#define JS_THREAD_STATUS_RUN		2
typedef void * (*JS_FT_ThreadFunc) (void *);


JS_HANDLE JS_ThreadPool_CreatePool(int nMaxNum);
int JS_ThreadPool_Destroy(JS_HANDLE hPool);
UINT32  JS_ThreadPool_StartThread(JS_HANDLE *phThread, JS_FT_ThreadFunc pFunc, void * pParam);
int JS_ThreadPool_GetThreadStatus(JS_HANDLE hThread);
int JS_ThreadPool_WaitForEndOfThread(JS_HANDLE hThread, unsigned int nWaitMsec, UINT32 nThreadID);

UINT32  JS_ThreadPool_StartThreadEx(JS_HANDLE hPool, JS_HANDLE *phThread, JS_FT_ThreadFunc pFunc, void * pParam);


#define JS_WORKQ_EVENT_ERROR		-1
#define JS_WORKQ_EVENT_WORKDONE		1
#define JS_WORKQ_EVENT_TOSTRING		2

typedef int (*JS_FT_WORKQUEUE_CALLBACK) (JSUINT nWorkID, void * pParam, int nEvent, JS_StringBuff * pStringBuff);

int JS_ThreadPool_GetWorksNum(JS_HANDLE hWorkQ);
int JS_ThreadPool_CancelWaiting(JS_HANDLE hWorkQ,JSUINT nWorkID, int *pnIsCanceled);
void * JS_ThreadPool_FindWorkQItem(JS_HANDLE hWorkQ,void * pKey, void * pFindFunc);
JS_HANDLE JS_ThreadPool_CreateWorkQueue(int nMaxConcurrentWorks);
void JS_ThreadPool_DestroyWorkQueue(JS_HANDLE hWorkQ);
JSUINT  JS_ThreadPool_AddWorkQueue(JS_HANDLE hWorkQ, JS_FT_ThreadFunc pFunc, void * pParam, JS_FT_WORKQUEUE_CALLBACK pfEvent);
JS_HANDLE  JS_ThreadPool_CheckWorkQueue(JS_HANDLE hWorkQ, int nFromWorkThread);
char * JS_ThreadPool_ToStringWorkQueue(JS_HANDLE hWorkQ, int nMaxItem);
int JS_ThreadPool_CheckIsFirstWork(JS_HANDLE hWorkQ, JSUINT nWorkID);
#ifdef __cplusplus
}
#endif
#endif /* JS_THREADPOOL_H_ */
