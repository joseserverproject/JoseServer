package com.titikaka.mediaproxyandroid;

import java.io.File;
import java.io.UnsupportedEncodingException;
import java.net.URLDecoder;
import java.util.Timer;
import java.util.TimerTask;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import android.media.AudioManager;
import android.media.MediaPlayer;
import android.media.MediaPlayer.OnErrorListener;
import android.media.MediaPlayer.OnPreparedListener;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.PowerManager;
import android.preference.PreferenceManager;
import android.app.Activity;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.telephony.TelephonyManager;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnTouchListener;
import android.view.WindowManager;
import android.view.ViewGroup.LayoutParams;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.MediaController;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.VideoView;
import android.webkit.JavascriptInterface;

//http://1-dot-empyrean-engine-436.appspot.com/registerserver?cmd=getip&id=demovideo

public class MainActivity extends Activity {
	private WebViewCustom m_mainWebView = null;
	private WebView m_optionWebView = null;	
	private TextView m_infoTextView = null;
	private VideoView m_mainVideoView = null;
	private FrameLayout	m_layoutVideoFrame = null;	
	private JSWebViewClient m_mainWVClient = null;	
	private NDKGateWay	m_objNDK = null;
	private HTTPWrapper m_objHttpWrapper = null;
	private TubeVidInfo m_objTubeVidInfo = null;
	private PowerManager.WakeLock m_objWakeLock = null;	
	public static String m_fullDir = null;
	public static String m_rootDir = null;
	public static String m_downDir = null;
	public static boolean m_bUseTurboGate = false;
	public static String m_demositeIP = null;
	private AudioManager m_objAudioManager = null;	
	public Handler m_objHandler = new Handler();
	private String m_strWatchUrl = "";
	private boolean m_bVideoFullMode = true;
	ProgressDialog m_dialog = null;
	private int m_nDemoMode = 0;
	private OnErrorListener m_objOnErrorListener;
	private String m_strVideoPlayBack = "";
	private int m_nSetMediaControl = 0;
	private boolean m_nInGateInfo = false;
	private int m_nDuration = 0;
	private int m_nBuffSec = 0;
	
	private boolean m_bStopTimer = false;
	private int m_nCounter = 0;
	private final int TIMER_PERIOD = 2000;
	private int m_nAvgRtt = 0;
	private int m_nAvgSwnd = 0;
	private int m_nAvgLoss = 0;
	private LowMediaController m_objMC=null;

	private ProgressDialog m_dlgWait=null;

	private class JSWebViewClient extends WebViewClient { 
        @Override
		public void onPageFinished(WebView view, String url) {
			// TODO Auto-generated method stub
        	Log.i("JETSH","Test WebView Finished..."+url);
			  m_objHandler.post(new Runnable(){
			        public void run() {        	
			        	m_mainWebView.loadUrl("javascript:js_set_appversion('android')");
			  }});	
        	super.onPageFinished(view, url);
		}

		@Override
		public void onReceivedError(WebView view, int errorCode,
				String description, String failingUrl) {
			// TODO Auto-generated method stub
			Log.i("JETSH","Test WebView Error..."+failingUrl);
			super.onReceivedError(view, errorCode, description, failingUrl);
		}

		@Override
		public boolean shouldOverrideUrlLoading(WebView view, String url) {
			// TODO Auto-generated method stub
        	//Log.i("TBA","shouldOverrideUrlLoading="+url);
			return super.shouldOverrideUrlLoading(view, url);
		}

		@Override
		public void onLoadResource(WebView view, String url) {
			// TODO Auto-generated method stub		
			if(url.contains("youtube.com/watch")) {
				Log.i("JETSH","onLoadResource="+url);
				m_strWatchUrl = url;
				m_nDemoMode = 0;
				doYoutubeVideo();
			}
			super.onLoadResource(view, url);
		}

		@Override
		public void onPageStarted(WebView view, String url, Bitmap favicon) {
			// TODO Auto-generated method stub
			//Log.i("TBA","onPageStarted="+url);
			if(url.contains("videoplayer.html")) {
				Log.i("JETSH","onLoadResource="+url);
				m_strWatchUrl = url;
				m_nDemoMode = 1;
				doDemoVideo();				
			}			
			super.onPageStarted(view, url, favicon);
		}
        
    }
	
	private class AndroidBridge {
		@JavascriptInterface
	    public void	changeConnectionNumber(String strParam) {
	    	Log.i("JETSH","changeConnectionNumber num-"+strParam);
	    	m_objNDK.sendCommand("changecontnum", strParam);	    	
	    }
		@JavascriptInterface
	    public void setMediaProxy(String strParam) {
	    	if(strParam.compareTo("0")==0) {
	    		Log.i("JETSH","setMediaProxy false-"+strParam);
	    		m_bUseTurboGate = false;
	    	}else {
	    		Log.i("JETSH","setMediaProxy true-"+strParam);
	    		m_bUseTurboGate = true;
	    	}
	    }
		@JavascriptInterface
	    public void setFullScreen(String strParam) {
	    	boolean bOld = m_bVideoFullMode;
	    	if(strParam.compareTo("0")==0) {
	    		Log.i("JETSH","setFullScreen false-"+strParam);
	    		m_bVideoFullMode = false;
	    	}else {
	    		Log.i("JETSH","setFullScreen true-"+strParam);
	    		m_bVideoFullMode = true;
	    	}
	    	if(bOld != m_bVideoFullMode) {
			   m_objHandler.post(new Runnable(){
			        public void run() { 	    		
			        	doChangeVideoSize(m_bVideoFullMode);
			        }
			   });
	    	}
	    }
		@JavascriptInterface
	    public void doExit(String strParam) {
		   m_objHandler.post(new Runnable(){
		        public void run() { 	    		
		        	finish();
		        }
		   });	    	
	    }
	}
	
	private class LowMediaController extends MediaController {
		boolean m_bShow = true;
	    public LowMediaController(Context context, AttributeSet attrs) {
	        super(context, attrs);
	    }

	    public LowMediaController(Context context, boolean useFastForward) {
	        super(context, useFastForward);
	    }

	    public LowMediaController(Context context) {
	        super(context);
	    }
		@Override
	    public void hide() {
	        // Do Nothing to keep the show the controller all times
			//if(m_bShow==false)
				super.hide();
	    }
		public void superToggle() {
			if(m_bShow) {
				m_bShow = false;
			}else {
				m_bShow = true;
			}
		}
	}	
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		Context objContext;
        objContext =  getApplicationContext();
        clearCache();

        m_rootDir = objContext.getExternalFilesDir("DocRoot").toString();
        File objExtStorage = Environment.getExternalStorageDirectory();
        m_downDir = objExtStorage.getAbsolutePath()+"/Music";
		m_fullDir = m_rootDir+"/"+"JetShare0980";
        m_objHttpWrapper  = new HTTPWrapper(this,m_rootDir,m_fullDir);
        m_objTubeVidInfo  = new TubeVidInfo();
        m_objTubeVidInfo.setHttpWrapper(m_objHttpWrapper);
		Log.i("JETSH", m_rootDir);
		File path = new File(m_downDir);
	    if(! path.isDirectory()) {
            path.mkdirs();
            path = null;
            Log.i("JETSH","Make Asset Directory");
	    }	
		path = new File(m_fullDir);
	    if(! path.isDirectory()) {
             path.mkdirs();
             path = null;
             Log.i("JETSH","Make Asset Directory");
	    }
		String strDev = Build.MANUFACTURER +"-" + Build.MODEL;
		String strPhone = "XXX";
		
		Log.i("JETSH",strDev);
		m_objNDK = new NDKGateWay();
		if(m_objNDK.prepareAll(m_fullDir, m_downDir, strDev, strPhone)>=0) {
			if(m_objNDK.start()>=0) {
				Log.i("JETSH","OK Gogo-->"+strPhone);
			}
		}
		m_objAudioManager = (AudioManager)getSystemService(Context.AUDIO_SERVICE);
		
		m_infoTextView = (TextView)findViewById(R.id.textview_info);
		m_infoTextView.setVisibility(View.GONE);
		m_infoTextView.setTextSize(18.0f);
		
        m_mainWebView = (WebViewCustom) findViewById(R.id.webview_main);
        m_mainWebView.getSettings().setJavaScriptEnabled(true);
        m_mainWebView.getSettings().setAllowFileAccess(true);
        m_mainWebView.getSettings().setAppCacheEnabled(false);
        m_mainWebView.getSettings().setUseWideViewPort(false);
        m_mainWebView.addJavascriptInterface(new AndroidBridge(), "HybridApp");
        
        //m_mainWebView.setSupportMultipleWindows(true);
        m_mainWVClient = new JSWebViewClient();
        m_mainWebView.setWebViewClient(m_mainWVClient);        
        m_mainWebView.loadUrl("file:///android_asset/index.html");

        m_optionWebView = (WebView) findViewById(R.id.webview_option);
        m_optionWebView.getSettings().setJavaScriptEnabled(true);
        m_optionWebView.getSettings().setAllowFileAccess(true);
        m_optionWebView.getSettings().setAppCacheEnabled(false);
        m_optionWebView.getSettings().setUseWideViewPort(false);
        m_optionWebView.addJavascriptInterface(new AndroidBridge(), "HybridApp");
        m_optionWebView.loadUrl("file:///android_asset/option.html");
        m_optionWebView.setVisibility(View.GONE);
        
        m_layoutVideoFrame = (FrameLayout) findViewById(R.id.video_frame);
        m_mainVideoView = (VideoView) findViewById(R.id.video_main);
        m_mainVideoView.setVisibility(View.GONE);
        m_mainVideoView.setOnTouchListener(new OnTouchListener() {
			@Override
			public boolean onTouch(View arg0, MotionEvent arg1) {
				// TODO Auto-generated method stub
				if(m_objMC != null) {
					m_objMC.superToggle();
				}
				return false;
			}        	
        });
        m_mainVideoView.setOnPreparedListener(new OnPreparedListener() {
            @Override
            public void onPrepared(MediaPlayer mp) {
                // TODO Auto-generated method stub
            	m_nDuration=mp.getDuration()/1000;
            }
        });        
		if(m_dlgWait==null) {
			m_dlgWait = new ProgressDialog(this);
			m_dlgWait.setMessage("Buffering");
			m_dlgWait.setProgressStyle(ProgressDialog.STYLE_SPINNER);
		}
        //setVolumeControlStream(AudioManager.STREAM_MUSIC);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
		m_objWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK , "My Tag");
		m_objWakeLock.acquire();
		m_objHttpWrapper.callShortAjaxRequestAsync("http://1-dot-empyrean-engine-436.appspot.com/registerserver?cmd=getip&id=demovideo",null,
				new AppEventListener() {
					@Override
					public String onEvent(String strParam) {
					   int nStart;
					   int nEnd;					   
					   nStart = strParam.indexOf("ip=");
					   nEnd = strParam.indexOf("</body>");
					   if(nStart<0 || nEnd<0) {
						  Log.i("JETSH","return from appengine: error="+strParam);
						  return null;
					   }
					   m_demositeIP = strParam.substring(nStart+3, nEnd);
					   Log.i("JETSH","return from appengine:  IP="+m_demositeIP);
					   m_objHandler.post(new Runnable(){
					        public void run() {        	
					        	m_mainWebView.loadUrl("javascript:js_set_demositeip('"+m_demositeIP+"')");
					   }});						
					   return null;
					}					
				});
	}

	@Override
	protected void onDestroy() {
		// TODO Auto-generated method stub
		super.onDestroy();
		m_objWakeLock.release();
		if(m_objNDK != null) {
			m_objNDK.stop();
			m_objNDK.cleanAll();
			Log.i("JETSH","EOS");
		}
		m_objHttpWrapper.stopBackgroundDebugJob();
		android.os.Process.killProcess(android.os.Process.myPid());
	}
	
	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if(keyCode==KeyEvent.KEYCODE_MENU) {
			if(m_optionWebView.getVisibility() == View.GONE)
				m_optionWebView.setVisibility(View.VISIBLE);
			else
				m_optionWebView.setVisibility(View.GONE);
		}else if(keyCode==KeyEvent.KEYCODE_BACK) {
			if(m_optionWebView.getVisibility()==View.VISIBLE) {
				m_optionWebView.setVisibility(View.GONE);
			}else if(m_mainVideoView.getVisibility()==View.VISIBLE) {
				if(m_mainWebView.canGoBack())		
					m_mainWebView.goBack();
				doNormalWebMode();
			}else {
				if(m_mainWebView.canGoBack())		
					m_mainWebView.goBack();
				else
					finish();
			}
		}
		else if(keyCode==KeyEvent.KEYCODE_VOLUME_UP) {
            m_objAudioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC,
                                             AudioManager.ADJUST_RAISE, 
                                             AudioManager.FLAG_SHOW_UI);
        }else if(keyCode==KeyEvent.KEYCODE_VOLUME_DOWN) {
            m_objAudioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC,
                    AudioManager.ADJUST_LOWER, 
                    AudioManager.FLAG_SHOW_UI);
        }
		return true;
	}
	
	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		// Inflate the menu; this adds items to the action bar if it is present.
		getMenuInflater().inflate(R.menu.main, menu);
		return true;
	}

	private void clearCache() {        
        final File cacheDirFile = this.getCacheDir();        
        if (null != cacheDirFile && cacheDirFile.isDirectory()) {
            clearSubCacheFiles(cacheDirFile);
        }
    }
    private void clearSubCacheFiles(File cacheDirFile) {
        if (null == cacheDirFile || cacheDirFile.isFile()) {
            return;
        }
        for (File cacheFile : cacheDirFile.listFiles()) {
            if (cacheFile.isFile()) {
                if (cacheFile.exists()) {
                    cacheFile.delete();                    
                }
            } else {
                clearSubCacheFiles(cacheFile);
            }
        }
    }	
	private void doChangeVideoSize(boolean bFullMode) {
		if(m_mainVideoView.getVisibility() == View.VISIBLE) {
			if(bFullMode) {
				doFullVideoMode();
			}else {
				doNormalVideoMode();
			}
		}
	}
	public void doFullVideoMode() {
		Log.i("JETSH","doFullVideoMode start");
		m_mainVideoView.setVisibility(View.VISIBLE);		
		FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT,LayoutParams.MATCH_PARENT,Gravity.CENTER);
		m_layoutVideoFrame.setLayoutParams(params);
		LayoutParams lp = m_layoutVideoFrame.getLayoutParams();
		lp.width = LayoutParams.MATCH_PARENT;
		lp.height = LayoutParams.MATCH_PARENT;
		m_mainVideoView.setLayoutParams(lp);
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);	
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
				WindowManager.LayoutParams.FLAG_FULLSCREEN);
		m_mainWebView.setVisibility(View.GONE);
		if(m_nSetMediaControl==0) {
			m_nSetMediaControl = 1;
			if(m_objMC==null)
				m_objMC = new LowMediaController(this);
			m_objMC.setMediaPlayer(m_mainVideoView);
			m_mainVideoView.setMediaController(m_objMC);
		}
		Log.i("JETSH","doFullVideoMode end");
		m_infoTextView.setVisibility(View.VISIBLE);
	}
	public void doNormalVideoMode() {
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_USER);
		getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		m_mainVideoView.setVisibility(View.VISIBLE);
		FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT,LayoutParams.MATCH_PARENT,Gravity.LEFT | Gravity.TOP);
		m_layoutVideoFrame.setLayoutParams(params);
		LayoutParams lp = m_layoutVideoFrame.getLayoutParams();
		lp.width = LayoutParams.WRAP_CONTENT;
		lp.height = LayoutParams.WRAP_CONTENT;
		m_mainVideoView.setLayoutParams(lp);
		m_mainWebView.setVisibility(View.VISIBLE);
		m_mainVideoView.requestFocus();
		m_infoTextView.setVisibility(View.VISIBLE);
	}
	public void doNormalWebMode() {
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_USER);
		getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		m_mainVideoView.stopPlayback();
		m_mainVideoView.setVisibility(View.GONE);
		m_mainWebView.setVisibility(View.VISIBLE);
		m_infoTextView.setVisibility(View.GONE);
		doStopTimer();
	}
	
	public void doStreaming(String strVidPlayback) {
		m_nBuffSec = 0;
		m_objHttpWrapper.checkLinkAliveAsync(strVidPlayback, new AppEventListener() {
			@Override
			public String onEvent(String strParam) {
				// TODO Auto-generated method stub
				if(strParam.length()>0) {
					String strTurbo = "turbogate211?url=";
					Log.i("JETSH","!!URL len="+strTurbo.length()+" OK = "+strParam);
					m_strVideoPlayBack = strParam;
					Log.i("JETSH","pre videoview!!");
					m_objHandler.post(new Runnable(){
				        public void run() {
				        	m_bStopTimer = false;
				        	doStartVidInfoTimer();
				        	Log.i("JETSH","reset videoview!");
				        	m_mainVideoView.setVideoURI(Uri.parse(filterTurboURL(m_strVideoPlayBack)));
							m_mainVideoView.requestFocus();
							m_mainVideoView.resume();
							Log.i("JETSH","vidstart!");
							m_mainVideoView.start();
							Log.i("JETSH","vidstart 2!");
					}});		
				}else {
					if(m_nDemoMode==1) {
						m_objTubeVidInfo.setDeadLink(m_objTubeVidInfo.getResult());
						if(m_objTubeVidInfo.getResult().length()>0) {
							Log.i("JETSH","Redirect video again = "+m_objTubeVidInfo.getResult());
							doStreaming(m_objTubeVidInfo.getResult());
						}else {
							Log.i("JETSH","No valid video error");
						}
					}				
				}
				return null;
			}			
		});		
	}
	
	public void doYoutubeVideo() {
		if(m_objTubeVidInfo.getStatus()==0) {
			Log.i("JETSH","can't load url cause busy tubeinfo");
			return;
		}
		m_objTubeVidInfo.setStatus(1);
		m_objTubeVidInfo.setDisplayResolution(WebViewCustom.m_nDisplayLength);	
		if(m_bVideoFullMode)
			doFullVideoMode();
		else
			doNormalVideoMode();
		m_mainVideoView.suspend();
		m_dialog = ProgressDialog.show(MainActivity.this, "Prepare Streaming",
                "Please wait...", false, true);
		m_objTubeVidInfo.doGetVideoInfoAsync(m_strWatchUrl, new AppEventListener() {
			@Override
			public String onEvent(String strParam) {
				// TODO Auto-generated method stub
				m_dialog.dismiss();
				if(strParam==null) {
					Log.i("JETSH","Fetch Job Fail!");
					Toast toast = Toast.makeText(getApplicationContext(),
							   "Can't play this video", Toast.LENGTH_LONG);
					toast.setGravity(Gravity.CENTER, 0, 0);
					toast.show();					
				}else {
					Log.i("JETSH","Ok go to youtube video");
		        	doStreaming(m_objTubeVidInfo.getResult());
				}
				return null;
			}			
		});
	}
	
	public void doDemoVideo() {
		if(m_demositeIP!=null) {
			m_objHandler.post(new Runnable(){
		        public void run() {			
					String strFileName;
					String strRealURL;
					int nStart; 
					if(m_bVideoFullMode)
						doFullVideoMode();
					else
						doNormalVideoMode();
					m_mainVideoView.suspend();			
					nStart = m_strWatchUrl.indexOf("vidname=");
					strFileName = m_strWatchUrl.substring(nStart+8);
					if(strFileName.contains("http%3A")) {
						try {
							strRealURL =  URLDecoder.decode(strFileName, "UTF-8");
						} catch (UnsupportedEncodingException e) {
							// TODO Auto-generated catch block
							e.printStackTrace();
							strRealURL = "";
						}
					}else {
						strRealURL = "http://"+m_demositeIP + "/demo/videos/"+strFileName;
					}
					Log.i("JETSH","StartToCheck="+strRealURL);
					doStreaming(strRealURL);
		        }
			});
		}
	}
	
	public String filterTurboURL(String strVidURL) {
		if(m_bUseTurboGate) {
			return new String ("http://127.0.0.1:9861/turbogate211?url="+HTTPWrapper.encodeURIComponent(strVidURL));
		}else
			return strVidURL;	
			//return new String ("http://127.0.0.1:9861/turbogate201?url="+HTTPWrapper.encodeURIComponent(strVidURL));
	}
	
	private boolean m_bSpinnerToggle = false;
	public void toggleSpinner() {		
		m_objHandler.post(new Runnable(){
	        public void run() {	
	    		if(m_bSpinnerToggle==false) {
	    			m_bSpinnerToggle = true;
	    			if(m_dlgWait!=null)
	    				m_dlgWait.show();
	    		}else {
	    			m_bSpinnerToggle = false;
	    			if(m_dlgWait!=null)
	    				m_dlgWait.dismiss();
	    		}
	        }
		});		
	}

	private TimerTask m_objTimerTask = null;
	private Timer m_objTimer = null;
	private int m_nOldPos = 0;
    private void doStartVidInfoTimer() {
        // TODO Auto-generated method stub
		m_objTimerTask = new TimerTask() {
			public void run() {
            	m_nCounter++;
                Log.i("JETSH", "mCounter : " + m_nCounter);
                
                if(m_mainVideoView.isPlaying()) {
                    int nPos = m_mainVideoView.getCurrentPosition();
                	if(m_nOldPos == nPos) {
                    	m_nBuffSec += 3;
                		m_bSpinnerToggle = false;
                		toggleSpinner();
                	}else if(m_bSpinnerToggle) {
                		Log.i("JETSH","spiner hide");
                		toggleSpinner();
                	}
                	m_nOldPos = nPos;
                }else {
                	if(m_bSpinnerToggle==false) {
                		Log.i("JETSH","spiner show");
                		toggleSpinner();
                	}
                }
                if(m_nInGateInfo==true)
                	return;
                m_nInGateInfo = true;
                m_objHttpWrapper.callShortAjaxRequestAsync("http://127.0.0.1:9861/gateinfo?cmd=stat", null, 
                		new AppEventListener() {
							@Override
							public String onEvent(String strParam) {
								// TODO Auto-generated method stub
								if(strParam != null) {
									JSONObject objJson;
									Log.i("JETSH","Json="+strParam);
									try {										
										objJson = new JSONObject(strParam);
										if(objJson != null) {
											m_nAvgRtt = objJson.getInt("avgrtt");
											m_nAvgSwnd = objJson.getInt("avgswnd");
											m_nAvgLoss = objJson.getInt("avgloss");
										}										
									} catch (JSONException e) {
										// TODO Auto-generated catch block
										Log.i("JETSH","No Multi Traffic now!");
									}
								}
								m_nInGateInfo = false;
								return null;
							}});
    			m_objHandler.post(new Runnable(){
    		        public void run() {	
    		        	double fBuffPercent = 0;
    		        	if(m_nDuration!=0)
    		        		fBuffPercent = m_nBuffSec*100/m_nDuration;
    		        	if(m_bUseTurboGate) {
	    		        	String strStat  = "TurboGate On, Delay="+(m_nAvgRtt/2000)+"ms,RttVar="+(m_nAvgLoss/2000)
	    		        			+"ms\nBuffering:"+fBuffPercent+"%("+m_nBuffSec+"sec/"+m_nDuration+"sec)";
	    		        	m_infoTextView.setText(strStat);
	    		        	Log.i("JETSH",strStat);
    		        	}else {
    		        		m_infoTextView.setText("TurboGate Off\nBuffering:"+fBuffPercent+"%("+m_nBuffSec+"sec/"+m_nDuration+"sec)");
    		        	}
    		        	Log.i("JETSH","m_nBuffSec="+m_nBuffSec);
    		        }
    			});
			}
		};
		m_objTimer = new Timer();
    	m_objTimer.schedule(m_objTimerTask, 0, 3000);  
    }

    private void doStopTimer() {
        // TODO Auto-generated method stub
        m_bStopTimer = true;
        m_objTimer.cancel();
        if(m_bSpinnerToggle)
        	toggleSpinner();
    }	
}