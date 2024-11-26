// Usage.cpp: implementation of the CUsage class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafxed.h"
#include "Usage.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

FeatureUsage* g_pUsage = NULL;

FeatureUsage::FeatureUsage()
    : mFilesOpened(0), mSolutionsOpened(0), mGotos(0), mFilesMParsed(0), mCharsTyped(0), mDotToPointerConversions(0),
      mAutotextMgrInsertions(0), mJunk(0)
{
}
