// Usage.h: interface for the CUsage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_USAGE_H__ED3D2372_3006_11D2_9310_000000000000__INCLUDED_)
#define AFX_USAGE_H__ED3D2372_3006_11D2_9310_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

struct FeatureUsage
{
	UINT mFilesOpened;
	UINT mSolutionsOpened;
	UINT mGotos;
	UINT mFilesMParsed;
	UINT mCharsTyped;
	UINT mDotToPointerConversions;
	UINT mAutotextMgrInsertions;
	UINT mJunk;

	FeatureUsage();
};

extern FeatureUsage* g_pUsage;

#endif // !defined(AFX_USAGE_H__ED3D2372_3006_11D2_9310_000000000000__INCLUDED_)
