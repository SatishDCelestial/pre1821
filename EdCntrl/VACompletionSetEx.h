#pragma once

#include "VACompletionSet.h"

class FindCompletion;

class VACompletionSetEx : public VACompletionSet
{
  public:
	VACompletionSetEx() : m_hasTimer(FALSE), mIsDismissing(FALSE)
	{
	}

	bool SetIvsCompletionSet(IVsCompletionSet* pIVsCompletionSet);
	void DisplayTimerCallback();

  protected:
	// overridden virtual methods
	virtual BOOL IsVACompletionSetEx()
	{
		return m_pIVsCompletionSet2 != NULL;
	}
	virtual bool AddString(BOOL sort, WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
	                       bool needDecode = true);
	virtual void SetImageList(HANDLE hImages);
	virtual void Dismiss();
	virtual WTString GetDescriptionTextOrg(long iIndex, bool& shouldColorText);
	virtual void DoCompletion(EdCntPtr ed, int popType, bool fixCase);
	virtual BOOL ShouldItemCompleteOn(symbolInfo* sinf, long c);
	virtual BOOL ExpandCurrentSel(char key = '\0', BOOL* didVSExpand = NULL);
	virtual IVsCompletionSet* GetIVsCompletionSet();
	virtual void OverrideSelectMode(symbolInfo* sinf, BOOL& unselect, BOOL& bFocusRectOnly);
	virtual BOOL HasSelection(); // Automation flag to see when it is OK to complete.
	virtual void FixCase();

  private:
	bool AddToExpandData(FindCompletion* item, BOOL sorted = true);
	void AddVSNetMembers(int nItems);

	CComPtr<IVsCompletionSet> m_pIVsCompletionSet2;
	BOOL m_hasTimer; // Flag to tell if list is complete yet.
	BOOL mIsDismissing;
	WTString mWordToLeftWhenTimerStarted;
};

extern VACompletionSetEx* g_CompletionSetEx;
