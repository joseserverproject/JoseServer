#ifndef _JS_HTMLVIEW_H_
#define _JS_HTMLVIEW_H_

#ifdef __cplusplus
extern "C" {
#endif

#define WEBPAGE_GOBACK		0
#define WEBPAGE_GOFORWARD	1
#define WEBPAGE_GOHOME		2
#define WEBPAGE_SEARCH		3
#define WEBPAGE_REFRESH		4
#define WEBPAGE_STOP		5

void JS_HTMLView_DoPageAction(HWND hwnd, DWORD action);
LRESULT CALLBACK JS_HTMLView_FilterWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
long JS_HTMLView_DisplayHTMLPage(HWND hwnd, LPTSTR webPageName);
long JS_HTMLView_DisplayHTMLStr(HWND hwnd, LPCTSTR string);
void JS_HTMLView_DoCallJavascript(HWND hwnd, const char * strFunctioinName, const char * strArg) ;
#ifdef __cplusplus
}
#endif
#endif /* _JS_HTMLVIEW_H_ */
