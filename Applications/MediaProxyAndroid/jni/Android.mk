LOCAL_PATH := $(call my-dir)
 
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../SrcAddon
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../SrcEssential
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DJS_CONFIG_OS=3
LOCAL_MODULE := joseservernative
LOCAL_LDLIBS := -llog

JOSESERVER_SRCS := josesrv_main.c \
	../../../SrcEssential/JS_Interface.c \
	../../../SrcEssential/JS_DataStructure_List.c \
	../../../SrcEssential/JS_DataStructure_Map.c \
	../../../SrcEssential/JS_DataStructure_Pool.c \
	../../../SrcEssential/JS_DataStructure_Queue.c \
	../../../SrcEssential/JS_EventLoop.c \
	../../../SrcEssential/JS_HttpServer.c \
	../../../SrcEssential/JS_ThreadPool.c \
	../../../SrcEssential/JS_UTIL_String.c \
	../../../SrcEssential/JS_UTIL_Network.c \
	../../../SrcEssential/JS_UTIL_Http.c \
	../../../SrcEssential/JS_UTIL_Misc.c \		

JOSESERVER_SRCS += ../../../SrcAddon/JS_DataStructure_Multiqueue.c \
	../../../SrcAddon/JS_MediaProxy.c \
	../../../SrcAddon/JS_MediaProxyTurbo.c \
	../../../SrcAddon/JS_SimpleHttpClient.c \
	../../../SrcAddon/JS_AutoTrafficControl.c
				
LOCAL_SRC_FILES := $(JOSESERVER_SRCS)
 
include $(BUILD_SHARED_LIBRARY)

