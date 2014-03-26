package com.titikaka.mediaproxyandroid;


import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.webkit.WebView;

public class WebViewCustom extends WebView {
	public static int m_nFullWidth = 0;
	public static int m_nFullHeight = 0;
	public static int m_nDisplayLength = 0;
	public WebViewCustom(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
		// TODO Auto-generated constructor stub
	}

	public WebViewCustom(Context context, AttributeSet attrs) {
		super(context, attrs);
		// TODO Auto-generated constructor stub
	}

	@Override
	@Deprecated
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		// TODO Auto-generated method stub
		super.onMeasure(widthMeasureSpec, heightMeasureSpec);
	}

	@Override
	protected void onSizeChanged(int w, int h, int ow, int oh) {
		// TODO Auto-generated method stub
		Log.v("TBA","SizeChanged: w="+w+",h="+h+",ow="+ow+",oh="+oh);
		super.onSizeChanged(w, h, ow, oh);
		if(w>0) {
			m_nFullWidth = w;
			m_nFullHeight = h;
			if(m_nFullWidth<m_nFullHeight)
				m_nDisplayLength = m_nFullWidth;
			else
				m_nDisplayLength = m_nFullHeight;
		}
	}

	public WebViewCustom(Context context) {
		super(context);
		// TODO Auto-generated constructor stub
	}

}