package com.titikaka.mediaproxyandroid;


import java.io.UnsupportedEncodingException;
import java.lang.reflect.Method;
import java.net.URLDecoder;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Queue;
import android.app.Activity;
import android.util.Log;

public class NDKGateWay {
	private static long  m_nLastTick = 0;
	private static long  m_nCurTick = 0;
	private static long  m_callIDSeq = 0;
	public  static int   m_myPort = 8999;
	private static long  m_nLastPollTick = 0;
	
	static {  
	     System.loadLibrary("joseservernative");  
	}      
	public NDKGateWay () {
		;
	}
	public native int prepareAll(String strRootDir, String strDownDir, String strDeviceName, String strPhoneNum);
	public native int start();
	public native int stop();
	public native int cleanAll();
	public native int sendCommand(String strCommand, String strParam);
    
	public static String callBackNativeToJava(String strArg) {
		String strRet = "<result>OK</result>";
		boolean bFlag;
		String strCMD;
		int nCnt;
		Map<String, String> mapParam;
		m_nCurTick = System.currentTimeMillis();		
		try {
			mapParam = HTTPWrapper.getUrlParameters(strArg);
		} catch (UnsupportedEncodingException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return strRet;
		}		
		m_nCurTick = System.currentTimeMillis();		
		strCMD = mapParam.get("TOPCMD1492");
		if(strCMD==null)
			return strRet;
		if(strCMD.equals("HELLO")){
			
		}else if(strCMD.equals("JAVACMD")) {

		}
		return strRet;
	}
}

