#include "stdafxed.h"
#include "parse.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

volatile bool StopIt = false;
const WTString TabStr("\t");

// CPP db dir - shares hashtable with CSDic
FileDic* g_pMFCDic = NULL;
// NET db dir - shares hashtable with MFCDic
FileDic* g_pCSDic = NULL;

// project defined globals, classes and members ??k depends on project sz
FileDic* g_pGlobDic = NULL;

// Empty db so that GetSysDic callers don't need to check for NULL
FileDic* g_pEmptyDic = NULL;
