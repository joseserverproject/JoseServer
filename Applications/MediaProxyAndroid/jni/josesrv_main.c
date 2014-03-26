#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include "com_titikaka_mediaproxyandroid_NDKGateWay.h"

#include "JS_Config.h"
#include "JS_OS.h"
#include "JS_Interface.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "JET-NDK", __VA_ARGS__))
#define ANDROID_APP_DIR	"/data/data/com.titikaka.mediaproxyandroid/files"

static JavaVM *g_VM = NULL;
static JNIEnv *g_Env = NULL;
static jclass  g_NativesCls = NULL;
static pthread_mutex_t g_rcMutex;
static JS_HANDLE g_hJose = NULL;

static int JS_Android_DIRECTAPI_JavaCommand(JS_HANDLE hSession);

static void JS_Android_NativeToJavaGateway(const char * strArg, int nArgLen, char * strRet, int nBuffSize);


jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	jclass  tmpCls;
    if ((*vm)->GetEnv(vm, (void**)(&g_Env), JNI_VERSION_1_6) != JNI_OK) {
    	LOGI("No Env");
        return -1;
    }
    g_VM = vm;
    // Get jclass with env->FindClass.
    // Register methods with env->RegisterNatives.
	if(g_NativesCls == NULL ) {
		tmpCls = (*g_Env)->FindClass(g_Env,"com/titikaka/mediaproxyandroid/NDKGateWay");
		if(tmpCls) {
			g_NativesCls = (jclass)(*g_Env)->NewGlobalRef(g_Env,tmpCls);
			if(g_NativesCls)
				LOGI("Yo Class Found");
		}
	}

    return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL Java_com_titikaka_mediaproxyandroid_NDKGateWay_sendCommand(JNIEnv * jEnv, jobject jObj, jstring jStrCmd, jstring jStrParam)
{
	jint nRet = 0;
	const char *nativeString1;
	const char *nativeString2;
	char strCmd[256];
	char strParam[4000];

	nativeString1  = (*jEnv)->GetStringUTFChars(jEnv, jStrCmd, 0);
	snprintf(strCmd,sizeof(strCmd),"%s",nativeString1);
	(*jEnv)->ReleaseStringUTFChars(jEnv, jStrCmd, nativeString1);

	nativeString2  = (*jEnv)->GetStringUTFChars(jEnv, jStrParam, 0);
	snprintf(strParam,sizeof(strParam),"%s",nativeString2);
	(*jEnv)->ReleaseStringUTFChars(jEnv, jStrParam, nativeString2);

	LOGI("sendCommand");
	if(strcmp(strCmd,"changecontnum")==0) {
		int nCont = atoi(strParam);
		LOGI("changecontnum");
		JS_ChangeConfigOption(g_hJose,JS_CONFIG_MAX_TURBOCONNECTION,nCont);
	}
	return nRet;
}

JNIEXPORT jint JNICALL Java_com_titikaka_mediaproxyandroid_NDKGateWay_prepareAll
  (JNIEnv * jEnv, jobject jObj, jstring jStrRootDir, jstring jStrDownDir, jstring jStrDev , jstring jStrPhone)
{
	jint nRet = 0;
	const char *nativeString1;
	const char *nativeString2;
	const char *nativeStringDownDir;
	char strTemp[4000];
	char strDownDir[4000];
	
	if(JS_InitGlobal() < 0 ) {
		JS_ClearGlobal();
		return -1;
	}
	g_hJose = JS_CreateJose(JS_SERVICE_HTTP|JS_SERVICE_PROXY, JS_CONFIG_PORT_FRONTGATE, 0);
	if(g_hJose==NULL) {
		JS_ClearGlobal();
		return -1;
	}
	nativeString1  = (*jEnv)->GetStringUTFChars(jEnv, jStrRootDir, 0);
	sprintf(strTemp,"%s",nativeString1);
	(*jEnv)->ReleaseStringUTFChars(jEnv, jStrRootDir, nativeString1);

	nativeStringDownDir  = (*jEnv)->GetStringUTFChars(jEnv, jStrDownDir, 0);
	sprintf(strDownDir,"%s",nativeStringDownDir);
	(*jEnv)->ReleaseStringUTFChars(jEnv, jStrDownDir, nativeStringDownDir);

	JS_HttpServer_RegisterDocumentRoot(g_hJose, strTemp);
	JS_HttpServer_RegisterUploadRoot(g_hJose, strDownDir);
	JS_HttpServer_RegisterDirectAPI(g_hJose, "directapi_javacmd",JS_Android_DIRECTAPI_JavaCommand);
	pthread_mutex_init(&g_rcMutex, NULL);
	return nRet;
}

/*
 * Class:     com_titikaka_mediaproxyandroid_NDKGateWay
 * Method:    start
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_titikaka_mediaproxyandroid_NDKGateWay_start
  (JNIEnv * jEnv, jobject jObj)
{
	jint nRet = 0;

	nRet = JS_StartJose(g_hJose);
	return nRet;
}

/*
 * Class:     com_titikaka_mediaproxyandroid_NDKGateWay
 * Method:    stop
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_titikaka_mediaproxyandroid_NDKGateWay_stop
  (JNIEnv * jEnv, jobject jObj)
{
	jint nRet = 0;
	nRet = JS_StopJose(g_hJose);
	return nRet;
}

/*
 * Class:     com_titikaka_mediaproxyandroid_NDKGateWay
 * Method:    cleanAll
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_titikaka_mediaproxyandroid_NDKGateWay_cleanAll
  (JNIEnv * jEnv, jobject jObj)
{
	jint nRet = 0;
	if(g_hJose) {
		JS_DestroyJose(g_hJose);
		g_hJose = NULL;
		nRet = JS_ClearGlobal();
	}
	pthread_mutex_destroy(&g_rcMutex);
	return nRet;
}

static int JS_Android_DIRECTAPI_JavaCommand(JS_HANDLE hSession)
{
	char pBuffer[16*1024];
	char pCmd[16*1024];
	char strRemoteIP[32];
	int nRet = 0;
	int nIndex = 0;

	pBuffer[0] = 0;
	//LOGI("NREQLEN=%u",nReqLen);
	JS_HttpServer_GetVariableFromReq(hSession,"",pBuffer,12*1024);
	JS_HttpServer_GetPeerIP(hSession, strRemoteIP, 16);
	snprintf(pCmd,sizeof(pCmd),"TOPCMD1492=JAVACMD&%s&strhostip=%s",pBuffer,strRemoteIP);
	JS_Android_NativeToJavaGateway(pCmd,0,pBuffer,sizeof(pBuffer));
	//LOGI("NRSPLEN=%u",strlen(pBuffer));
	JS_HttpServer_SendQuickJsonRsp(hSession,pBuffer);
	return nRet;
}


static void JS_Android_NativeToJavaGateway(const char * strArg, int nArgLen, char * strRet, int nBuffSize)
{
	//LOGI("NDKEvent Start");
	if(g_VM && g_NativesCls) {
		jmethodID method;
		jstring js, jsRet;
		const char * strout;
		pthread_mutex_lock(&g_rcMutex);
#ifdef JNI_VERSION_1_2
		(*g_VM)->AttachCurrentThread (g_VM,(void**)&g_Env, NULL);
#else
    	(*g_VM)->AttachCurrentThread (g_VM,&g_Env, NULL);
#endif
		js = (*g_Env)->NewStringUTF(g_Env,strArg);
		method = (*g_Env)->GetStaticMethodID(g_Env,g_NativesCls, "callBackNativeToJava", "(Ljava/lang/String;)Ljava/lang/String;");
		if(method) {
			jboolean isCopy = 1;
			//LOGI("TMP Start Callback");
			jsRet = (jstring)(*g_Env)->CallStaticObjectMethod(g_Env,g_NativesCls, method, js);
			//LOGI("TMP End Callback");
			strout = (*g_Env)->GetStringUTFChars(g_Env,jsRet, &isCopy);
			if(strout) {
				snprintf(strRet,nBuffSize,"%s",strout);
			}
		}
		(*g_Env)->DeleteLocalRef(g_Env,js);
		//(*g_Env)->DeleteLocalRef(g_Env,method);
		(*g_Env)->DeleteLocalRef(g_Env,jsRet);
		(*g_VM)->DetachCurrentThread(g_VM);
		pthread_mutex_unlock(&g_rcMutex);
	}
	//LOGI("NDKEvent End");
}

