#ifndef JS_CONTROL_MAIN_H
#define JS_CONTROL_MAIN_H

int JS_Control_Destroy(void);
int JS_Control_Create(void);
int JS_Control_Command(UINT message, WPARAM wParam, LPARAM lParam);
int JS_ControlMain_CheckBrowserLoading(void);

#include <string>
using namespace std;

bool JS_ControlMain_DirExist(const string& dirName_in);
bool JS_ControlMain_FileExist(const string& fileName_in);
WCHAR * JS_CotrolMain_MultiByteToWide(const char * strSrc, int nIsUTF8);
BOOL JS_ControlMain_Unzip2Folder(LPCTSTR strZipFile, LPCTSTR strFolder);
#endif
