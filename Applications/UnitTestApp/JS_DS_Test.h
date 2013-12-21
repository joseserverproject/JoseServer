#ifndef DS_TEST_H
#define DS_TEST_H
int DSTest_Init(void);
int DSTest_Clear(void);
int DSTest_AutoRun_List(int nThreadNum, int nRunLength) ;
int DSTest_AutoRun_Map(int nThreadNum, int nRunLength);
int DSTest_AutoRun_Pool(int nThreadNum, int nRunLength);

#endif