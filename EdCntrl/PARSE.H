#ifndef INCPARSEH
#define INCPARSEH

#include "OWLDefs.h"
#include "incToken.h"

#define BORDER_WIDTH 15
#define FRAME_WIDTH 2
#define LMARGIN (FRAME_WIDTH + BORDER_WIDTH)

class FileDic;

extern const WTString TabStr;

FileDic* GetSysDic(int lang); // sets and returns current system db
FileDic* GetSysDic();         // returns current system db
bool IsAnySysDicLoaded(bool inStateOfLoadingIsOk = false);

extern FileDic* g_pMFCDic;
extern FileDic* g_pCSDic;
extern FileDic* g_pGlobDic;
extern FileDic* g_pEmptyDic;
#endif
