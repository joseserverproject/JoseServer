package com.titikaka.mediaproxyandroid;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.Method;
import java.net.HttpRetryException;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLDecoder;
import java.net.URLEncoder;
import java.util.HashMap;
import java.util.Map;

import org.apache.http.HttpResponse;
import org.apache.http.HttpVersion;
import org.apache.http.client.HttpClient;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.impl.client.DefaultHttpClient;
import org.apache.http.impl.client.DefaultHttpRequestRetryHandler;
import org.apache.http.params.CoreProtocolPNames;


import android.app.Activity;
import android.app.ProgressDialog;
import android.os.AsyncTask;
import android.os.Handler;
import android.util.Log;

public class HTTPWrapper {
	String m_strRootDir= "";
	String m_strFullDir= "";
	private ProgressDialog m_objProgressDialog = null;
	private Activity m_objActivity = null;
	String [] m_arrDbgFileList = null;
	//HTTPAsyncRequestJob m_objMyAsyc = null;
	public final static int ASYNC_DBG_JOB_INTVAL = 24000;
	boolean m_bCancel = false;
	public int nCacheDisableCounter = 213;
	public final static int ASYNC_CMD_SENDAJAX	 = 1;
	public final static int ASYNC_CMD_DOWNLOADFILE = 2;
	public final static int ASYNC_CMD_GETDBGFILE = 3;
	public final static int ASYNC_CMD_CHECKALIVE = 4;
	private Handler m_objHandler = null;
	private Runnable m_objPperiodicTask = null;
	
	private class JS_AsyncReqEntry {
		int nCmd;
		public String strURL;
		public String strData;
		public Object objListener;			
	};
	
	public HTTPWrapper(Activity objActivity, String strRootDir, String strFullDir) {
		m_objActivity = objActivity;
		m_strRootDir = strRootDir;
		m_strFullDir = strFullDir;
		m_objHandler = new Handler();
		m_objPperiodicTask = new Runnable() {
	        public void run() {
	            Log.i("JETSH","Awake");
	            startBackgroundDebugJob();
	            m_objHandler.postDelayed(m_objPperiodicTask, ASYNC_DBG_JOB_INTVAL);
	        }
	    };
	}
	

	public boolean startDownloadFile(String strURL) {
		Log.i("JETSH", "Download Start = "+ strURL);
		JS_AsyncReqEntry objParam = new JS_AsyncReqEntry();
		objParam.strURL = strURL;
		objParam.strData = null;
		objParam.objListener = null;
		objParam.nCmd = ASYNC_CMD_DOWNLOADFILE;	
		HTTPAsyncRequestJob objMyAsyc = new HTTPAsyncRequestJob();
		if(objMyAsyc != null)
			objMyAsyc.execute(objParam);
		return true;
	}
	
	public boolean checkLinkAliveAsync(String strURL, Object objListener) {
		boolean bRet = true;
		Log.i("JETSH", "Check URL = "+ strURL);
		JS_AsyncReqEntry objParam = new JS_AsyncReqEntry();
		objParam.strURL = strURL;
		objParam.strData = null;
		objParam.objListener = objListener;
		objParam.nCmd = ASYNC_CMD_CHECKALIVE;
		HTTPAsyncRequestJob objMyAsyc = new HTTPAsyncRequestJob();
		if(objMyAsyc != null)
			objMyAsyc.execute(objParam);	
		return bRet;
	}
	
	public boolean callShortAjaxRequestAsync(String strURL, String strData, Object objListener) {
		boolean bRet = true;
		Log.i("JETSH", "Ajax Start = "+ strURL);
		JS_AsyncReqEntry objParam = new JS_AsyncReqEntry();
		objParam.strURL = strURL;
		objParam.strData = strData;
		objParam.objListener = objListener;
		objParam.nCmd = ASYNC_CMD_SENDAJAX;
		HTTPAsyncRequestJob objMyAsyc = new HTTPAsyncRequestJob();
		if(objMyAsyc != null)
			objMyAsyc.execute(objParam);	
		return bRet;
	}
	
	public void setBGDebugJob(String strFileList) {
		m_arrDbgFileList = strFileList.split(",");
		if(m_arrDbgFileList.length>0) {
			m_objHandler.postDelayed(m_objPperiodicTask, ASYNC_DBG_JOB_INTVAL);
		}
	}
	public void startBackgroundDebugJob() {		
		Log.i("JETSH", "Start dbg...");
		JS_AsyncReqEntry objParam = new JS_AsyncReqEntry();
		objParam.strURL = null;
		objParam.strData = null;
		objParam.objListener = null;
		objParam.nCmd = ASYNC_CMD_GETDBGFILE;		
		HTTPAsyncRequestJob objMyAsyc = new HTTPAsyncRequestJob();
		if(objMyAsyc != null)
			objMyAsyc.execute(objParam);
	}
	
	public void stopBackgroundDebugJob() {
		m_bCancel = true;
		m_objHandler.removeCallbacks(m_objPperiodicTask);
	}
	
	public String downloadSmallTextWithBlocking(String strURL) {
		String strReturn = "";
		try {			
			DefaultHttpClient httpclient = new DefaultHttpClient();
			HttpGet request = new HttpGet(strURL);
			httpclient.setHttpRequestRetryHandler(new DefaultHttpRequestRetryHandler(0, false));
			HttpResponse response = httpclient.execute(request);
			BufferedReader rd = new BufferedReader(
					new InputStreamReader(response.getEntity().getContent()));
			StringBuffer result = new StringBuffer();
			String line = "";
			while ((line = rd.readLine()) != null) {
				result.append(line);
				if(m_bCancel)
					return strReturn;						
			}
			httpclient.getConnectionManager().shutdown();
			if(m_bCancel)
				return strReturn;					
			strReturn = result.toString();	
		}catch (Exception e) {
				e.printStackTrace();
				return "";
		}
		return strReturn;
	}

	public boolean checkUrlWithBlocking(String strURL) {
		boolean bRet = false;
		try {			
			int nRsp;
			DefaultHttpClient httpclient = new DefaultHttpClient();
			HttpGet request = new HttpGet(strURL);
			httpclient.setHttpRequestRetryHandler(new DefaultHttpRequestRetryHandler(0, false));
			HttpResponse response = httpclient.execute(request);
			nRsp = response.getStatusLine().getStatusCode()/100;
			if(nRsp==2||nRsp==3) {
				Log.i("JETSH","StatusCode="+response.getStatusLine().getStatusCode());
				bRet = true;
			}
			httpclient.getConnectionManager().shutdown();
		}catch (Exception e) {
				e.printStackTrace();
				return bRet;
		}
		return bRet;
	}
	
	public boolean downloadFileWithBlocking(String strURL, String strFilePath, Object objProgressListener) {
		byte arrData[];
		long nTotal;
		long nLength;
		int nRead;
		Method method = null;
		Class parameterTypes []= new Class[] { Integer.TYPE };
		DefaultHttpClient objHttpclient;
		HttpGet objRequest;
		HttpResponse objResponse;
		InputStream input;
		BufferedOutputStream output;		
		if(objProgressListener != null) {
			try {
				method = objProgressListener.getClass().getMethod("publishProgress", parameterTypes);
			} catch (NoSuchMethodException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
		if(m_bCancel)
			return false;		
		try {		
		//	Log.i("JETSH","DownURL="+strURL);
			objHttpclient = new DefaultHttpClient();
			objRequest = new HttpGet(strURL);
			objHttpclient.setHttpRequestRetryHandler(new DefaultHttpRequestRetryHandler(0, false));
			objResponse = objHttpclient.execute(objRequest);
			nLength = objResponse.getEntity().getContentLength();
			input = new BufferedInputStream(objResponse.getEntity().getContent());
			output = new BufferedOutputStream (new FileOutputStream(strFilePath));			
			arrData = new byte[32*1024];			
			nTotal = 0;			
			while ((nRead = input.read(arrData)) != -1) {
				nTotal += nRead;
				if(method != null)
					method.invoke(objProgressListener, ""+(int)((nTotal*100)/nLength));
				output.write(arrData, 0, nRead);
				if(nLength != 0 && nLength<=nTotal)
					break;
				if(m_bCancel)
					break;
			}
			output.flush();
			output.close();
			input.close();
			objHttpclient.getConnectionManager().shutdown();
		}catch (Exception e) {
			e.printStackTrace();
			return false;
		}
		return true;
	}
	
	private class HTTPAsyncRequestJob extends AsyncTask<Object, String, String> {		   
		@Override
		protected void onCancelled() {
			// TODO Auto-generated method stub
			super.onCancelled();
			m_bCancel = true;
		}

		@Override
		protected void onCancelled(String result) {
			// TODO Auto-generated method stub
			super.onCancelled(result);
			m_bCancel = true;
		}

		@Override
		protected void onPostExecute(String result) {
			// TODO Auto-generated method stub
			super.onPostExecute(result);
		}

		@Override
		protected void onPreExecute() {
			// TODO Auto-generated method stub
			super.onPreExecute();
		}

		@Override
		protected void onProgressUpdate(String... values) {
			// TODO Auto-generated method stub
			super.onProgressUpdate(values);
		}

		@Override
		protected String doInBackground(Object... paramObjs) {
			String strReturn = "";
			try {
				for(int nCnt=0; nCnt<paramObjs.length; nCnt++) {
					strReturn = "";
					if(m_bCancel)
						break;
					JS_AsyncReqEntry objParam = (JS_AsyncReqEntry) paramObjs[nCnt];
					switch(objParam.nCmd) {
						case ASYNC_CMD_SENDAJAX:
						{
							String strFullURL = objParam.strURL;
							if(objParam.strData != null) {
								if(strFullURL.indexOf("?")>0)
									strFullURL += "&" + objParam.strData;
								else
									strFullURL += "?" + objParam.strData;
							}
							strFullURL += "&nocachesome="+(++nCacheDisableCounter);								
							strReturn = downloadSmallTextWithBlocking(strFullURL);
							//Log.i("JETSH","Send Ajax Completed with apache ("+strReturn+")");
						}
						break;
						case ASYNC_CMD_CHECKALIVE:
						{
							if(checkUrlWithBlocking(objParam.strURL)==true) {
								strReturn = objParam.strURL;
								Log.i("JETSH","URL is alive("+objParam.strURL+")");
							}else {
								Log.i("JETSH","URL is dead("+objParam.strURL+")");
								strReturn = "";
							}
						}
						break;
						case ASYNC_CMD_DOWNLOADFILE:
						{
							String strFileName;
							String strDestPath;
							strFileName = objParam.strURL.substring(objParam.strURL.lastIndexOf('/')+1);
							strDestPath = m_strFullDir+"/"+strFileName;
							Log.i("JETSH", "Dest="+strDestPath);
							downloadFileWithBlocking(objParam.strURL,strDestPath,this);
						}
						break;
					}
					if(m_bCancel)
						break;
					if(objParam.objListener != null) {
						Method method=null;
						try {
							Class parameterTypes []= new Class[] { String.class };
							method = objParam.objListener.getClass().getMethod("onEvent",parameterTypes);
						} catch (NoSuchMethodException e1) {
							// TODO Auto-generated catch block
							e1.printStackTrace();
						}
						if(method != null) {
							try {
								Log.i("JETSH","Invoke event listener");
								method.invoke(objParam.objListener, strReturn);
							} catch (Exception e) {
								// TODO Auto-generated catch block
								e.printStackTrace();
							}
						}					
					}
					
					
				}
			}catch (Exception e) {
				Log.e("tag", e.getMessage());
			}
			return null;
		}
	}	
	   
    public static String convertXMLString(String strOrg) {
    	StringBuffer strBuff;
    	int nLen = strOrg.length();
    	strBuff = new StringBuffer(); 
    	for(int nCnt=0; nCnt<nLen; nCnt++) {
    		switch(strOrg.charAt(nCnt)) {
            case '&':  strBuff.append("&amp;");       break;
            case '\"': strBuff.append("&quot;");      break;
            case '\'': strBuff.append("&apos;");      break;
            case '<':  strBuff.append("&lt;");        break;
            case '>':  strBuff.append("&gt;");        break;
            default:   strBuff.append(strOrg.charAt(nCnt)); break;
    		}
    	}
    	return strBuff.toString();
    }
    public static Map<String, String> getUrlParameters(String strQuery)
            throws UnsupportedEncodingException {
        Map<String, String> params = new HashMap<String, String>();
        for (String param : strQuery.split("&")) {
            String pair[] = param.split("=");
            String key = URLDecoder.decode(pair[0], "UTF-8");
            String value = "";
            if (pair.length > 1) {
                value = URLDecoder.decode(pair[1], "UTF-8");
            }
            params.put(key, value);
        }
        return params;
    } 
    
    public static String encodeURIComponent(String s) {
      String result = null;   
      try {
        result = URLEncoder.encode(s, "UTF-8")
                           .replaceAll("\\+", "%20")
                           .replaceAll("\\%21", "!")
                           .replaceAll("\\%27", "'")
                           .replaceAll("\\%28", "(")
                           .replaceAll("\\%29", ")")
                           .replaceAll("\\%7E", "~");
      }   
      // This exception should never occur.
      catch (UnsupportedEncodingException e) {
        result = s;
      }   
      return result;
    }    
}
