#ifndef NET_TEST_H
#define NET_TEST_H

int NetTest_CheckPreviousRequest(void);
int NetTest_RunAjaxTest(const char * strURL);
int NetTest_RunFileDownloadTest(const char * strURL, const char * strFileName);

#endif