// MediaProxy.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <stdio.h>
#include <string.h>
#include <Shldisp.h>
#include <Shellapi.h>
#include "MediaProxy.h"
#include "JS_ControlMain.h"

#pragma comment(lib, "Shell32")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wininet.lib")
#define MAX_LOADSTRING 100

#define JS_WIN32_WINMAX 1
#define JS_WIN32_WINMIN 2
#define JS_WIN32_WINNORMAL 0

#define WM_LOADING_TIMER (WM_USER+333)
#define WM_TRAY_CALLBACK (WM_USER+411)

#define IDM_SETTING		(199)

HANDLE  g_hMutex = NULL;
class JS_Win32_GlobalData {
public:
	int nPosX;
	int nPosY;
	int nWinWidth;
	int nWinHeight;
	int nExistProfile;
	int nWinState;
	char strDefaultCacheDir[2000];
	HWND	hWnd;
};
HWND g_hwndGlobal = NULL;
static int JS_Win32_InitProfile(void);
static int JS_Win32_SaveProfile(void);
static void JS_Win32_CreateTray();
static void JS_Win32_ChangeTray();
static void JS_Win32_DeleteTray();
static void JS_Win32_PopupMenu(HWND hWnd);
static void JS_Wind32_PopupSettingHtml();
BOOL CheckResource(void);

static JS_Win32_GlobalData	g_rcGlobalData;
// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);

LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

 	// TODO: Place code here.
	MSG msg;
	HACCEL hAccelTable;

	msg.wParam=0;
	g_hMutex = CreateMutex(NULL, FALSE, _T("NODEJSSUCK9988_SoMyP**GOGOMEDIAPROXY")); 
	if(GetLastError() == ERROR_ALREADY_EXISTS)
		return FALSE;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	sprintf_s(szWindowClass,"MediaProxy_1.1");
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		goto LABEL_EXIT_TMAIN;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MEDIAPROXY));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);

			DispatchMessage(&msg);
		}
	}

LABEL_EXIT_TMAIN:
	JS_Win32_DeleteTray();
	if(g_hMutex)
		CloseHandle(g_hMutex);
	return (int) msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= NULL;//LoadIcon(hInstance, MAKEINTRESOURCE(IDI_JSWIN32));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= NULL;//MAKEINTRESOURCE(IDC_JSWIN32);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= NULL;//LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

BOOL CheckResource(void)
{
	HGLOBAL     hMem = NULL;
    HRSRC       hRes;
    char *      pResData;
    DWORD       nResSize;
	int			nCnt;
	int			nWrite;
	FILE	*   fp;
	if(JS_ControlMain_DirExist("html")==true)
		return TRUE;
	hRes = FindResource(hInst, MAKEINTRESOURCE(ID_HTMLRSOURCE), RT_RCDATA);
	if (!hRes)
        return FALSE;
    hMem = LoadResource(NULL, hRes);
    if (!hMem)
        return FALSE;
    pResData = (char*)LockResource(hMem);
    nResSize = SizeofResource(NULL, hRes);
	fp = fopen("htmlimsi.zip","wb");
	nCnt = 0;
	while(fp) {
		nWrite = fwrite(pResData+nCnt,1,nResSize-nCnt,fp);
		if(nWrite>0) {
			nCnt+=nWrite;
			if(nCnt>=nResSize) {
				fclose(fp);
				break;
			}
		}else {
			fclose(fp);
			break;
		}
	}
	FreeResource(hMem);
	if(nCnt>=nResSize) {
		TCHAR szMaxZipFile[2400];
		TCHAR szMaxFolder[2400];
		GetCurrentDirectory(2000, szMaxZipFile);
		GetCurrentDirectory(2000, szMaxFolder);
		strcat_s(szMaxZipFile,2200,"\\htmlimsi.zip");
		strcat_s(szMaxFolder,2200,"\\html");
		CreateDirectory(szMaxFolder,NULL);
		BOOL bRet= JS_ControlMain_Unzip2Folder(szMaxZipFile,szMaxFolder);
		DeleteFile("htmlimsi.zip");
		return bRet;
	}
	return FALSE;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
//#define WS_JSWIN32_WINDOW   (WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX)
#define WS_JSWIN32_WINDOW   (WS_OVERLAPPED|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX)
   HWND hWnd;
   hInst = hInstance; // Store instance handle in our global variable

   JS_Win32_InitProfile();
   hWnd = CreateWindow(szWindowClass, szTitle, WS_JSWIN32_WINDOW,
      g_rcGlobalData.nPosX, g_rcGlobalData.nPosY, g_rcGlobalData.nWinWidth, g_rcGlobalData.nWinHeight, NULL, NULL, hInstance, NULL);
   if (!hWnd)
   {
      return FALSE;
   }
   g_rcGlobalData.hWnd = hWnd;
   g_hwndGlobal = hWnd;
   JS_Wind32_PopupSettingHtml();
   JS_Win32_CreateTray();
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

static void JS_Wind32_PopupSettingHtml()
{
	TCHAR strTemp[2000];
	sprintf_s(strTemp, 2000, _T("http://127.0.0.1:9861/gui.html?app=win32&hit=%u"), GetTickCount());
	ShellExecute(NULL, "open", strTemp, NULL, NULL, SW_SHOWMAXIMIZED);
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	if(message>WM_USER)
		JS_Control_Command(message,wParam,lParam);
	switch (message)
	{
	case WM_CREATE:
		//JS_Control_SetProxyToMe(1);
		CheckResource();
		JS_Control_Create();
		SetTimer(hWnd,WM_LOADING_TIMER,100,NULL);
		//PostMessage(hWnd, WM_SHOWWINDOW, SW_HIDE, 0);
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		{
			switch (wmId)
			{
			case IDM_SETTING:
				JS_Wind32_PopupSettingHtml();
				break;
			case IDM_EXIT:
				DestroyWindow(hWnd);
				break;
			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code here...
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		//JS_Control_SetProxyToMe(0);
		JS_Control_Destroy();
		JS_Win32_DeleteTray();
		JS_Win32_SaveProfile();
		PostQuitMessage(0);
		break;
	case WM_KEYUP:
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	case WM_TIMER:
		ShowWindow(hWnd, SW_HIDE);
		KillTimer(hWnd, WM_LOADING_TIMER);
		break;
	case  WM_SYSCOMMAND:
		if(wParam==SC_MINIMIZE) {
			g_rcGlobalData.nWinState = JS_WIN32_WINMIN;
		}else if(wParam==SC_MAXIMIZE) {
			g_rcGlobalData.nWinState = JS_WIN32_WINMAX;
		}else if(wParam==SC_RESTORE) {
			g_rcGlobalData.nWinState = JS_WIN32_WINNORMAL;
		}
		break;
	case WM_TRAY_CALLBACK:
		if (wParam == IDR_MAINFRAME) {
			switch (lParam) {
				case WM_RBUTTONUP:
					JS_Win32_PopupMenu(hWnd);
					break;
			}
		}
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


static int JS_Win32_InitProfile(void)
{
	TCHAR szCurPath[4000] = {NULL, };
	TCHAR strData[4000];
	TCHAR strDefault[200];
	int nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	int nScreenHeight = GetSystemMetrics(SM_CYSCREEN);
	int nWinWidth = 800;
	int nWinHeight = 480;
	int nPosX, nPosY;
	int nWinState = 0;

	nPosX = (nScreenWidth-nWinWidth)/2;
	nPosY = (nScreenHeight-nWinHeight)/2;

	memset((char*)&g_rcGlobalData,0,sizeof(g_rcGlobalData));
	GetCurrentDirectory(2000, szCurPath);
	strcat_s(szCurPath, _T("\\mediaproxy.ini"));
	GetPrivateProfileString(_T("MediaProxy"), _T("Signature"), _T("NO"),strData,2000,szCurPath);
	if(strData[0] == _T('N')) {
		g_rcGlobalData.nExistProfile = 0;
	}else {
		g_rcGlobalData.nExistProfile = 1;
	}

	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), nPosX);
	GetPrivateProfileString(_T("MediaProxy"), _T("nPosX"),strDefault,strData,2000,szCurPath);
	g_rcGlobalData.nPosX = _tstoi(strData);

	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), nPosY);
	GetPrivateProfileString(_T("MediaProxy"), _T("nPosY"),strDefault,strData,2000,szCurPath);
	g_rcGlobalData.nPosY = _tstoi(strData);

	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), nWinWidth);
	GetPrivateProfileString(_T("MediaProxy"), _T("nWinWidth"),strDefault,strData,2000,szCurPath);
	g_rcGlobalData.nWinWidth = _tstoi(strData);

	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), nWinHeight);
	GetPrivateProfileString(_T("MediaProxy"), _T("nWinHeight"),strDefault,strData,2000,szCurPath);
	g_rcGlobalData.nWinHeight = _tstoi(strData);

	_stprintf_s(strDefault, _countof(strDefault), _T("C:"));
	GetPrivateProfileString(_T("MediaProxy"), _T("CacheDir"),strDefault,g_rcGlobalData.strDefaultCacheDir,sizeof(g_rcGlobalData.strDefaultCacheDir),szCurPath);
	
	return 0;
}

int JS_Win32_SetCacheDirectory(const char * strPath)
{
	_stprintf_s(g_rcGlobalData.strDefaultCacheDir, sizeof(g_rcGlobalData.strDefaultCacheDir), "%s",strPath);
	return 0;
}

static int JS_Win32_SaveProfile(void)
{
	TCHAR szCurPath[4000] = {NULL, };
	TCHAR strDefault[200];
	RECT  rcRect;

	if(g_rcGlobalData.hWnd) {
		GetWindowRect(g_rcGlobalData.hWnd,&rcRect);
		g_rcGlobalData.nPosX = rcRect.left;
		g_rcGlobalData.nPosY = rcRect.top;
		g_rcGlobalData.nWinWidth = rcRect.right-rcRect.left;
		g_rcGlobalData.nWinHeight = rcRect.bottom-rcRect.top;
	}
	GetCurrentDirectory(2000, szCurPath);
	strcat_s(szCurPath, _T("\\mediaproxy.ini"));
	WritePrivateProfileString(_T("MediaProxy"), _T("Signature"), _T("YES"), szCurPath);

	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), g_rcGlobalData.nWinState);
	WritePrivateProfileString(_T("MediaProxy"), _T("nWinState"), strDefault, szCurPath);
	if(g_rcGlobalData.nWinWidth<=0 || g_rcGlobalData.nWinHeight<=0 || g_rcGlobalData.nWinState != 0)
		return 0;
	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), g_rcGlobalData.nPosX);
	WritePrivateProfileString(_T("MediaProxy"), _T("nPosX"), strDefault, szCurPath);
	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), g_rcGlobalData.nPosY);
	WritePrivateProfileString(_T("MediaProxy"), _T("nPosY"), strDefault, szCurPath);
	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), g_rcGlobalData.nWinWidth);
	WritePrivateProfileString(_T("MediaProxy"), _T("nWinWidth"), strDefault, szCurPath);
	_stprintf_s(strDefault, _countof(strDefault), _T("%d"), g_rcGlobalData.nWinHeight);
	WritePrivateProfileString(_T("MediaProxy"), _T("nWinHeight"), strDefault, szCurPath);
	_stprintf_s(strDefault, _countof(strDefault), _T("%s"), g_rcGlobalData.strDefaultCacheDir);
	WritePrivateProfileString(_T("MediaProxy"), _T("CacheDir"), strDefault, szCurPath);
	return 0;
}


static void JS_Win32_CreateTray()
{
	NOTIFYICONDATA data;
	data.cbSize = sizeof(NOTIFYICONDATA);
	data.hWnd = g_hwndGlobal;
	data.uID = IDR_MAINFRAME;
	data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	data.uCallbackMessage = WM_TRAY_CALLBACK;
	data.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MEDIAPROXY));
	strcpy(data.szTip, "Test");
	Shell_NotifyIcon(NIM_ADD, &data);
}

static void JS_Win32_ChangeTray()
{
	NOTIFYICONDATA data;
	data.cbSize = sizeof(NOTIFYICONDATA);
	data.hWnd = g_hwndGlobal;
	data.uID = IDR_MAINFRAME;
	data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	data.uCallbackMessage = WM_TRAY_CALLBACK;
	strcpy(data.szTip, "?");
	data.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MEDIAPROXY));
	Shell_NotifyIcon(NIM_MODIFY, &data);


}

static void JS_Win32_DeleteTray()
{
	NOTIFYICONDATA data;
	data.cbSize = sizeof(NOTIFYICONDATA);
	data.hWnd = g_hwndGlobal;
	data.uID = IDR_MAINFRAME;
	Shell_NotifyIcon(NIM_DELETE, &data);
}

static void JS_Win32_PopupMenu(HWND hWnd)
{
	POINT    pt;
	GetCursorPos(&pt);
	SetForegroundWindow(hWnd);
	HMENU hPopupMenu = CreatePopupMenu();
	AppendMenu(hPopupMenu, MF_ENABLED, IDM_SETTING, _T("Settings"));
	AppendMenu(hPopupMenu, MF_ENABLED, IDM_EXIT, _T("Exit"));
	TrackPopupMenu(hPopupMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
	PostMessage(hWnd, WM_NULL, 0, 0);
	//DestroyMenu(hPopupMenu);
}