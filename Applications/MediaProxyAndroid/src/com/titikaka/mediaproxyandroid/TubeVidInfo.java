package com.titikaka.mediaproxyandroid;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.ProtocolException;
import java.net.URL;
import java.net.URLDecoder;
import java.util.ArrayList;
import java.util.StringTokenizer;

import org.apache.http.util.ByteArrayBuffer;


import android.util.Log;


public class TubeVidInfo  
{
	String m_strVID;
	int m_nEnable=0;
	int		m_nDone = 0;	
	String m_resultString = "";
	public static final int DATA_LEN = 1024*2048;
	static final String URLPREFIX_GETVIDEO = "http://www.youtube.com/get_video_info?video_id=";
	static final int	REASON_UNKONWN	= 0;
	static final int	REASON_VEVO	= 0;
	int		m_nFailureReason;
	String m_strPost = "";
	int		m_nDisplayResolution;
	int		m_nRetryCnt = 0;
	HTTPWrapper	m_objHttpWrapper = null;
	
	private class TubeEntry {
		public String strURL;
		public String strType;
		public String strQuality;
		public String strSignature;
		public int		nBitrate;
		public boolean bCheck;
		public TubeEntry() {
			nBitrate = 1;
			strURL = "";
			strType = "";
			strQuality = "";
			strSignature = "";
			bCheck = true;
		}
		public void printEntry() {
			LogString("url="+strURL);
			LogString("type="+strType);
			LogString("bitrate="+nBitrate);
			LogString("quality="+strQuality);
		}
		public void logInfoEntry() {
			Log.i("TBA","url="+strURL);
			Log.i("TBA","type="+strType);
			Log.i("TBA","bitrate="+nBitrate);
			Log.i("TBA","quality="+strQuality);
		}
	}
	ArrayList<TubeEntry> m_listTubeEntry;
	static public void LogString(String strFormat) {
		Log.i("JETSH",strFormat);
	}	
	public void setDisplayResolution(int nResolution) {
		if(nResolution>0) {
			m_nDisplayResolution = nResolution;
		}
	}
	public int getStatus() {
		if(m_nEnable==0)
			return 1;
		return m_nDone;
	}
	public void setStatus(int nEnable) {
		m_nEnable = nEnable;
		if(m_nEnable==0) {
			m_resultString = "";
		}else
			m_nDone = 0;
	}
	public String getResult() {
		return m_resultString;
	}
	public void setDeadLink(String strURL) {
		int nSize;
		int nCnt;
		TubeEntry rTempEntry;
		TubeEntry bestTubeEntry;
		nSize = m_listTubeEntry.size();
		if(nSize<=0)
			return;
		for(nCnt=0; nCnt<nSize; nCnt++) {
			rTempEntry = m_listTubeEntry.get(nCnt);
			if(rTempEntry.strURL.compareTo(strURL)==0) {
				rTempEntry.bCheck = false;
				break;
			}
		}
       ///determine best video!!
       bestTubeEntry = doDetermineBestContent();       
       LogString("OK");
       if(bestTubeEntry != null)
    	   m_resultString =  bestTubeEntry.strURL;
       else {
    	   m_resultString = "";	
       }
	}
	private TubeEntry doDetermineBestContent() {
		int nSize;
		int nCnt;
		int nHighBitrate=0;
		int nMatchScore;
		int nIndex = -1;
		TubeEntry rTempEntry;
		nSize = m_listTubeEntry.size();
		if(nSize<=0)
			return null;
		if(nSize==1)
			return m_listTubeEntry.get(0);
		for(nCnt=0; nCnt<nSize; nCnt++) {
			rTempEntry = m_listTubeEntry.get(nCnt);
			if(rTempEntry.bCheck==false)
				continue;
			nMatchScore = 0;
			if(rTempEntry.nBitrate>0)
				nMatchScore = rTempEntry.nBitrate/1000000;
			if(rTempEntry.strType != "" && rTempEntry.strType.regionMatches(true, 0, "audio", 0, 5))
				continue;
			if(rTempEntry.strType != "" && rTempEntry.strType.regionMatches(true, 0, "video/webm", 0, 9))
				nMatchScore += 100;
			else if(rTempEntry.strType != "" && rTempEntry.strType.regionMatches(true, 0, "video/mp4", 0, 9))
				nMatchScore += 120;
			else if(rTempEntry.strType != "" && rTempEntry.strType.regionMatches(true, 0, "video/mp4", 0, 9))
				nMatchScore += 120;

			if(m_nDisplayResolution>=720 && rTempEntry.strQuality != "" && rTempEntry.strQuality.regionMatches(true, 0, "hd720", 0, 5))
				nMatchScore += 600;
			else if(m_nDisplayResolution>=1080 && rTempEntry.strQuality != "" && rTempEntry.strQuality.regionMatches(true, 0, "hd1080", 0, 5))
				nMatchScore += 800;
			else if(rTempEntry.strQuality != "" && rTempEntry.strQuality.regionMatches(true, 0, "medium", 0, 5))
				nMatchScore += 400;
			else if(rTempEntry.strQuality != "" && rTempEntry.strQuality.regionMatches(true, 0, "large", 0, 5))
				nMatchScore += 300;			
			if(nMatchScore>nHighBitrate) {
				nIndex = nCnt;
				nHighBitrate = nMatchScore;
			}
		}
		if(nIndex>=0) {
			LogString("Fount Item="+nIndex);
			m_listTubeEntry.get(nIndex).printEntry();
			return m_listTubeEntry.get(nIndex);
		}else
			return null;
	}
	private boolean doParseVidInfo(String strBuff)
	{
		String strTemp;
		String strPrintTmp;
		String [] arrStr1stSplt;
		String [] arrStr2ndSplt;
		String [] arrStrEntrySplt;
		int nEntryNum;
		int nEntryCnt;
		int nLength;
		int nCnt;
		boolean bVidUrl;
		boolean bHaveField;
		TubeEntry rTempTubeEntry;
		String strTmpToken2;
		String strFmtMap="";
		StringTokenizer rTmpTokenizer = new StringTokenizer(strBuff, "&=" );
		
		////get url_encoded_fmt_stream_map
		while(rTmpTokenizer.hasMoreTokens()) {
			strTmpToken2 = rTmpTokenizer.nextToken();
			if(strTmpToken2.compareToIgnoreCase("url_encoded_fmt_stream_map")==0) {
				strFmtMap = rTmpTokenizer.nextToken();
				break;
			}
		}
		if(strFmtMap=="") {
			LogString("No FMT MAP!");
			LogString(strBuff);
			if(strBuff.contains("=VEVO"))
				m_nFailureReason = REASON_VEVO;
			return false;
		}
		try {
			strTemp = URLDecoder.decode(strFmtMap,"UTF-8");
		} catch (UnsupportedEncodingException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return false;
		}
		
		m_listTubeEntry = new ArrayList<TubeEntry>();
		
		arrStrEntrySplt = strTemp.split(",");
		nEntryNum = arrStrEntrySplt.length;
		for(nEntryCnt=0; nEntryCnt<nEntryNum; nEntryCnt++) {
			arrStr1stSplt = arrStrEntrySplt[nEntryCnt].split("&");
			nLength = arrStr1stSplt.length;
			LogString("");
			LogString("-----------------------");
			for(nCnt=0; nCnt<nLength; nCnt++) {
				strPrintTmp = "";
				try {
					strPrintTmp = URLDecoder.decode(arrStr1stSplt[nCnt],"UTF-8");
				} catch (UnsupportedEncodingException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
				LogString(strPrintTmp);
			}
			LogString("-----------------------");
			LogString("");
			rTempTubeEntry = new TubeEntry();
			/////split entry elements
			for(nCnt=0; nCnt<nLength; nCnt++) {
				bVidUrl = false;
				bHaveField = false;
				if(arrStr1stSplt[nCnt].contains("videoplayback?"))
					bVidUrl = true;
				if(arrStr1stSplt[nCnt].contains("="))
					bHaveField = true;
				if(bHaveField) {
					arrStr2ndSplt = arrStr1stSplt[nCnt].split("=");
					if(arrStr2ndSplt[0].compareToIgnoreCase("url")==0) {
						try {
							String strTmpToken=URLDecoder.decode(arrStr2ndSplt[1],"UTF-8");
							strTmpToken=URLDecoder.decode(strTmpToken,"UTF-8");
							rTempTubeEntry.strURL = strTmpToken;
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
						};
					}else if(arrStr2ndSplt[0].compareToIgnoreCase("type")==0) {
						try {
							rTempTubeEntry.strType = URLDecoder.decode(arrStr2ndSplt[1],"UTF-8");
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
						}
					}else if(arrStr2ndSplt[0].compareToIgnoreCase("quality")==0) {
						try {
							rTempTubeEntry.strQuality = URLDecoder.decode(arrStr2ndSplt[1],"UTF-8");
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
						}						
					}else if(arrStr2ndSplt[0].compareToIgnoreCase("sig")==0 || arrStr2ndSplt[0].compareToIgnoreCase("s")==0) {
						try {
							rTempTubeEntry.strSignature = URLDecoder.decode(arrStr2ndSplt[1],"UTF-8");
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
						}						
					}else if(arrStr2ndSplt[0].compareToIgnoreCase("bitrate")==0) {
						String strTmp2="";
						try {
							strTmp2 = URLDecoder.decode(arrStr2ndSplt[1],"UTF-8");
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
						}
						StringTokenizer values = new StringTokenizer(strTmp2, "?&,=" );
						if(values.hasMoreTokens()) {
							rTempTubeEntry.nBitrate = Integer.parseInt(values.nextToken());
						}
					}
				}else if(bVidUrl) {
					rTempTubeEntry.strURL = arrStr1stSplt[0];
				}				
			}///for entry element
			if(rTempTubeEntry.strSignature!="")
				rTempTubeEntry.strURL = rTempTubeEntry.strURL+"&signature="+rTempTubeEntry.strSignature;
			rTempTubeEntry.printEntry();
			m_listTubeEntry.add(rTempTubeEntry);
		}	
		return true;
	}
	
	public boolean doGetVideoInfoAsync(String strURL, final AppEventListener objListener) {
		String strToken;
		String strWatchURL;
		String strVID = null;
		m_strPost = "";
		
		LogString("Start to fetch!");
		m_nFailureReason = REASON_UNKONWN;
		StringTokenizer values = new StringTokenizer( strURL, "?&" );
		while(values.hasMoreTokens()) {
			strToken = values.nextToken();
			if(strToken.regionMatches(true, 0, "v=", 0, 2)) {
				strVID = strToken.substring(2);
				break;
			}
		}
		if(strVID == null) {
			m_nDone = 1;
			return false;
		}
		LogString("VID="+strVID);
		strWatchURL = new String(URLPREFIX_GETVIDEO+strVID+m_strPost);
		m_objHttpWrapper.callShortAjaxRequestAsync(strWatchURL, null, new AppEventListener() {
			@Override
			public String onEvent(String strParam) {
				String tempString;
				TubeEntry	bestTubeEntry=null;
				// TODO Auto-generated method stub
				if(strParam==null || strParam.length()<=0) {
					Log.i("JETSH","doFetchVidPlaybackURL failure");
					objListener.onEvent(null);
				}
				tempString = strParam;
				Log.i("JETSH", "Rcv Done="+tempString.length());
		        ////parse all entries
				if(doParseVidInfo(tempString)) {
			       ///determine best video!!
			       bestTubeEntry = doDetermineBestContent();       
			       Log.i("JETSH","OK");
			       if(bestTubeEntry != null)
			    	   m_resultString =  bestTubeEntry.strURL;
			       else {
			    	   m_resultString = "";
			       }
		        }
				m_nDone = 1;
				objListener.onEvent(m_resultString);
				return null;
			}			
		});
		return true;
	}
	
	public void setHttpWrapper(HTTPWrapper objHttp) {
		m_objHttpWrapper = objHttp;		
	}

}
