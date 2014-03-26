package com.titikaka.mediaproxyandroid;

public interface AppEventListener {
	class JS_FunctionCallEntry {
		String stgFunctionEntry;
		Object	objCallback;
		long	nCallID;
		long	nCallTick;		
	}	
	public String onEvent(String strParam);		
}