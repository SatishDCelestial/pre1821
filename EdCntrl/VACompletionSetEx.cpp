
#include "stdafxed.h"
#include "resource.h"
#include "VACompletionBox.h"
#include "VACompletionSet.h"
#include "VACompletionSetEx.h"
#include "FileTypes.h"
#include <atlcomcli.h>
#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\textmgr2.h"
#include "VAParse.h"
#include "VAParse.h"
#include "VaService.h"

#if defined(_DEBUG) && defined(SEAN)
#include "..\common\PerfTimer.h"
#endif // _DEBUG && SEAN
#include "PROJECT.H"
#include "DevShellAttributes.h"
#include "WTComBSTR.h"
#include "Guesses.h"
#include "AutotextManager.h"
#include "StringUtils.h"
#include "VaMessages.h"

extern int g_imgFilter[255];
VACompletionSetEx* g_CompletionSetEx;
class FindCompletion
{
  public:
	FindCompletion(IVsCompletionSet* cset) : mIVsCompletionSet(cset)
	{
		_ASSERTE(g_currentEdCnt && gShellAttr);
		EdCntPtr ed(g_currentEdCnt);
		if (ed && gShellAttr)
		{
			if (IsCFile(ed->m_ScopeLangType))
			{
				m_bUpperFirst = TRUE;
				if (!gShellAttr->IsDevenv8()) // not for 2005, (2003? not tested)
				{
					m_bUndescoreLast = TRUE;
					m_bInitialUndescoreLast = TRUE;
				}
			}
			if (CS == ed->m_ScopeLangType && gShellAttr->IsDevenv8())
				m_bUpperFirst = TRUE; // Fixes vs2005 exception in VAAutoTest:ScopeSuggestCS/vs2005 "string"->"String"
			if (JS == ed->m_ScopeLangType)
				m_bUpperFirst = TRUE;
			if (Is_VB_VBS_File(ed->m_ScopeLangType))
				m_bInitialUndescoreLast = TRUE;
			if (Is_Tag_Based(gTypingDevLang))
				m_bUnSortedList =
				    TRUE; // Binary sort fails if their list isn't sorted alphabetically i.e. XAML case=40890
		}
	}

	WCHAR GetCharSortVal(WCHAR c)
	{
		if (gShellAttr->IsDevenv10OrHigher())
		{
			switch (c)
			{
			case L'_':
				if (m_bUndescoreLast)
					return L'z' + 1; // c++: after alnum
				return c;
			case L'!':
				// operator!() is after operator >(), see "std::",
				// Not really sure what the priority is, but putting after alnum seems to work?
				// Should this be c++ only?
				return L'z' + 1;
			case L'~':
				// C#: ~'s appear after _'s, Should this be c# only?
				return L'_' + 1; // after _'s
			case 0x2605:
				// [case: 118434]
				// IntelliCode '(star)'
				return L'0' - 1;
			}
		}
		else
		{
			if (c == L'~')       // C#: ~'s appear after _'s, Should this be c# only?
				return L'_' + 1; // after _'s
				                 // 200x uses standard ascii sorting
		}
		return towlower(c);
	}

	int OurStrCmpW(const WCHAR* s1, const WCHAR* s2, bool matchCase)
	{
		// We do a binary search in VS lists, so our sort needs to match theirs.
		// There are subtle differences in C#/VB/C++ in 2010/2008
		// see case=39937 VS2010: No default intellisense for "w<ctrl+space>"
		if (m_bInitialUndescoreLast)
		{
			if (*s1 == '_' || *s2 == '_')
			{
				// VB and vc(200x) does a two pass filter
				// putting items starting with _'s at the end, yet following _'s are sorted normally
				int d = towupper(*s1) - towupper(*s2); // upper so '_' is after 'Z'
				if (d)
					return d;
			}
		}

		WCHAR c1, c2;
		int rval = 0;
		for (;;)
		{
			c1 = (*s1++);
			c2 = (*s2++);
			if (c1 != c2)
			{
				WCHAR lc1 = GetCharSortVal(c1);
				WCHAR lc2 = GetCharSortVal(c2);
				if (lc1 == lc2) // same char, different case
				{
					if (!rval && matchCase)
					{
						if (m_bUpperFirst)
							rval = (c1 > c2) ? 1 : -1; // Upper case first in C++
						else
							rval = (c1 > c2) ? -1 : 1; // Lower case first
					}
					// Continue on to see if case is the only difference
				}
				else
					return (lc1 > lc2) ? 1 : -1;
			}
			if (c1 == 0)
				return rval; // return case mismatch if any
		}
	}

#ifdef Jer
	BOOL TestSort()
	{
		if (mIVsCompletionSet)
		{
			const int count = mIVsCompletionSet->GetCount();
			CStringW lastStr;
			for (int i = 0; i < count; i++)
			{
				GetItem(i);
				if (OurStrCmpW(lastStr, mWText, false) > 0)
					return FALSE; // Oops...
				lastStr = mWText;
			}
		}
		return TRUE; // OK
	}
#endif // Jer
	BOOL SeqFind(LPCWSTR startswith, bool matchCase = true)
	{
		// Sequential search to find first match.
		// Returns true if it finds an exact match.
		const int count = mIVsCompletionSet->GetCount();
		if (!startswith || !startswith[0])
			return GetItem(0);
		for (int i = mIdx + 1; i < count; i++)
		{
			if (GetItem(i))
			{
				if (OurStrCmpW(startswith, mWText, matchCase) == 0)
					return TRUE;
			}
		}
		mWText.Empty();
		return FALSE;
	}

	BOOL SeqIntellicodeFind(const CStringW& startswith, bool matchCase = true)
	{
		mIdx = -1;
		mWText.Empty();

		if (startswith.IsEmpty())
			return FALSE;

		// IntelliCode '(star)'
		_ASSERTE(0x2605 == startswith[0]);

		// Sequential search to find first match.
		// Returns true if it finds an exact match.
		const int count = mIVsCompletionSet->GetCount();
		for (int i = mIdx + 1; i < count; i++)
		{
			if (GetItem(i))
			{
				if (mWText.IsEmpty() || 0x2605 != mWText[0])
					break;

				if (OurStrCmpW(startswith, mWText, matchCase) == 0)
					return TRUE;
			}
		}

		mWText.Empty();
		return FALSE;
	}

	BOOL Find(LPCWSTR startswith, bool matchCase = true)
	{
		// Binary search to find first match.
		// Returns true if it finds an exact match.
		const int count = mIVsCompletionSet->GetCount();
		if (!count)
			return FALSE;

		int high = count - 1;
		int low = mIdx + 1;
		if (m_bUnSortedList || count < 1000) // Only do binary sorts on large lists. [case=12324]
			return SeqFind(startswith, matchCase);
		if (!startswith || !startswith[0])
			return GetItem(0);
		mWText.Empty();
		while (low <= high)
		{
			int middle = (high + low) / 2;
			if (!GetItem(middle))
				return FALSE;

			// TODO: !!!VS list is not sorted with wcscmp, ~'s at top and _'s below, breaks some searches!!!
			int cmp = OurStrCmpW(startswith, mWText, matchCase);
			if (cmp > 0)
				low = middle + 1;
			else if (cmp < 0)
				high = middle - 1;
			else
				return TRUE;
		}

		if (low < count)
		{
			if (!GetItem(low))
				return FALSE;
		}

		if (mWText.IsEmpty())
			return FALSE;

		// [case: 102674]
		if (matchCase)
			return StrCmpW(startswith, mWText) == 0;

		return StrCmpIW(startswith, mWText) == 0;
	}

	BOOL GetNextMatch(WTString pattern)
	{
		// Returns true if it finds an acronym/substring/startswith match.
		WTString str(pattern);
		if (!pattern.IsEmpty() && pattern[0] == '<')
			pattern = "";
		if (!mbFullSearch)
		{
			if (!pattern.IsEmpty())
			{
				// Look for next startsWith
				if (mIdx == -1)
					Find(pattern.Wide(), false); // Binary Search to first match
				else
					GetItem(mIdx + 1); // Get next item

				const WTString sym(Sym());
				if (!sym.IsEmpty() && StartsWithNC(sym, pattern, FALSE))
					return TRUE; // Found next startsWith match
			}

			// StartsWith not found, do a full search from the top to find substring matches
			mbFullSearch = TRUE;
			mIdx = -1; // Set to -1 so FindSubSetMatch gets the first one, it starts at (mIdx+1). case=41246
		}

		if (mbFullSearch)
			return FindSubSetMatch(pattern.c_str());

		return FALSE;
	}

	// Info on found item
	WTString Sym()
	{
		if (!mWText.IsEmpty())
			return WTString(mWText);
		else
			return WTString();
	}

	int Type()
	{
		return IMG_IDX_TO_TYPE(mImg) | VSNET_TYPE_BIT_FLAG;
	}
	int Idx()
	{
		return mIdx;
	}
	WTString GetDescriptionText()
	{
		CComBSTR bstr;
		HRESULT res = mIVsCompletionSet->GetDescriptionText(mIdx, &bstr);
		if (SUCCEEDED(res))
			return WTString(CStringW(bstr));
		return NULLSTR;
	}

	BOOL GetItem(int idx)
	{
		mIdx = idx;
		LPCWSTR txt = nullptr;
		HRESULT res = mIVsCompletionSet->GetDisplayText(mIdx, &txt, &mImg);
		mWText = txt;
		if (S_OK == res && mWText.IsEmpty())
			res = E_FAIL; // [case: 100685]
		if (S_OK != res)
			mIdx = -1;
		return res == S_OK;
	}

  private:
	BOOL FindSubSetMatch(LPCSTR pattern)
	{
		const int high = mIVsCompletionSet->GetCount();
		if (high)
		{
			int pos = mIdx + 1;
			for (; pos < high; pos++)
			{
				if (GetItem(pos) && ContainsSubset(Sym().c_str(), pattern, SUBSET))
					return true;
			}
		}
		return FALSE;
	}

	int mIdx = -1;
	long mImg = 0;
	CStringW mWText;
	BOOL mbFullSearch = 0;
	CComPtr<IVsCompletionSet> mIVsCompletionSet;

	// Sorting flags
	BOOL m_bUpperFirst = false;
	BOOL m_bUndescoreLast = false;
	BOOL m_bInitialUndescoreLast = false;
	BOOL m_bUnSortedList = false;
};

#define DisplayTimerID 100
const uint kDisplayTimerTime = 100;

void CALLBACK DisplayTimerCallback(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	KillTimer(hWnd, idEvent);
	g_CompletionSetEx->DisplayTimerCallback();
}

void VACompletionSetEx::DisplayTimerCallback()
{
	if (!mWordToLeftWhenTimerStarted.IsEmpty())
	{
		const WTString leftOfCurs(m_ed->WordLeftOfCursor());
		if (leftOfCurs.IsEmpty() || 0 != mWordToLeftWhenTimerStarted.Find(leftOfCurs))
		{
			// [case: 61510] don't display if they already expanded text
			m_hasTimer = FALSE;
			return;
		}
	}

	vCatLog("Editor.Events", "VACSX::DisT  sw(%s) t(%s)", m_startsWith.c_str(), mWordToLeftWhenTimerStarted.c_str());

	if (GetCount())
		DisplayList();
	if (m_ed)
		m_ed->Scope();
	if (!m_isMembersList)
		m_ExpData.AddScopeSuggestions(m_ed); // if any
	EdCntPtr curEd(g_currentEdCnt);
	ScopeInfoPtr si = curEd ? curEd->ScopeInfoPtr() : nullptr;
	bool hassuggestions = si && si->m_ScopeSuggestions.GetLength() != 0; // see case=39936
	if (GetCount() == 0 || m_popType == ET_EXPAND_COMLETE_WORD || m_popType == ET_EXPAND_MEMBERS ||
	    m_popType == ET_EXPAND_VSNET ||
	    (!Psettings->m_UseVASuggestionsInManagedCode && !hassuggestions)) // [case=41875]
	{
		SetPopType(ET_EXPAND_VSNET); // .NET flag
		AddVSNetMembers(1000);
	}
	// this condition changed in change 15133 for case=41551.
	else if (m_expBox && (!m_expBox->GetItemCount() || m_startsWith.GetLength() > 3)) // Same logic as HintThread.
	{
		if (!hassuggestions || StartsWith().GetLength() > 2)
			AddVSNetMembers(Psettings->mInitialSuggestionBoxHeightInItems);
	}
	else if (m_expBox)
	{
		// If it only found acronym matches in suggestions, make sure we are using the whole list.
		int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
		if (m_expBox->GetItemState(item, LVIS_SELECTED) == 0 && (m_startsWith.GetLength() || !hassuggestions))
		{
			SetPopType(ET_EXPAND_VSNET); // .NET flag
			AddVSNetMembers(1000);
		}
	}

	m_hasTimer = FALSE;
}

bool VACompletionSetEx::SetIvsCompletionSet(IVsCompletionSet* pIVsCompletionSet)
{
	EdCntPtr curEd(g_currentEdCnt);
	if (::ShouldSuppressVaListbox(curEd))
	{
		m_pIVsCompletionSet2 = nullptr;
		m_pIVsCompletionSet = nullptr;
		m_ed = nullptr;
		return false;
	}

	if (gShellAttr->IsDevenv15OrHigher() && Psettings->m_bUseDefaultIntellisense && IsCFile(gTypingDevLang) &&
	    !Psettings->m_autoSuggest &&
	    !Psettings->m_codeTemplateTooltips) // snippet suggestions are independent of normal suggestions
	{
		// [case: 100685][case: 114018]
		m_pIVsCompletionSet2 = nullptr;
		m_pIVsCompletionSet = nullptr;
		m_ed = nullptr;
		return false;
	}

	if (m_pIVsCompletionSet2 == pIVsCompletionSet)
		return true;

	bool combineWithActiveList = false;
	bool listIsActive = false;
	bool activeCompStatusCmd = false;
	bool didCheckIsMembersList = false;
	ScopeInfoPtr si(curEd->ScopeInfoPtr());

	if (::GetTickCount() > gLastUserExpandCommandTick && ::GetTickCount() < (gLastUserExpandCommandTick + 750))
		activeCompStatusCmd = true;

	if (IsCFile(gTypingDevLang) && !Psettings->m_bUseDefaultIntellisense)
	{
		// never use vs C++ completionset if 'use default intellisense' is off
		if (pIVsCompletionSet)
			pIVsCompletionSet->Dismiss();

		if (m_ed != curEd)
		{
			if (IsExpUp(NULL))
				Dismiss();
			m_ed = curEd;
		}

		if (!IsExpUp(NULL))
		{
			KillTimer(NULL, DisplayTimerID);
			curEd->KillTimer(DSM_VA_LISTMEMBERS);
			m_hasTimer = FALSE;
			DisplayList();
		}

		return true;
	}
	else if (m_ed == curEd && m_expBox && IsExpUp(NULL))
	{
		listIsActive = true;

		if (gShellAttr->IsDevenv14OrHigher() && pIVsCompletionSet && !m_pIVsCompletionSet && !m_pIVsCompletionSet2)
		{
			if (!(IsCFile(gTypingDevLang)) && ::ExptypeHasPriorityOverVsCompletion(GetPopType()))
			{
				const WTString pwd2 = m_ed->CurWord(-2);
				if (pwd2 != "new" && pwd2 != "throw" && pwd2 != "As")
				{
					const WTString pwd = m_ed->CurWord(-1);
					if (pwd != "(") // ideally we should check signature of pwd2 to see if it takes a predicate (linq)
					{
						if (!CheckIsMembersList(m_ed))
						{
							// [case: 85074]
							// we already have a list up with a higher priority than VS
							// do not dismiss our list
							// combine with their completionset
							combineWithActiveList = true;
						}
					}
				}
			}
			else if (gShellAttr->IsDevenv15OrHigher() && IsCFile(gTypingDevLang))
			{
				const auto pt = GetPopType();
				if (ET_EXPAND_INCLUDE == pt ||
				    ((ET_EXPAND_TEXT == pt || ET_EXPAND_MEMBERS == pt) && si->m_suggestionType & SUGGEST_FILE_PATH))
				{
					if (!Psettings->mIncludeDirectiveCompletionLists)
					{
						if (IsExpUp(nullptr))
							Dismiss();
						return false;
					}

					// #include completion in dev15 -- only VA
					if (pIVsCompletionSet)
					{
						// necessary for typing to not wipe out inserted text
						pIVsCompletionSet->Dismiss();
					}
					return true;
				}
				else if (::ExptypeHasPriorityOverVsCompletion(pt))
				{
					if (!activeCompStatusCmd)
					{
						if (Psettings->m_UseVASuggestionsInManagedCode)
						{
							// ignore their suggestion
							if (pIVsCompletionSet)
								pIVsCompletionSet->Dismiss();
							return true;
						}
					}

					CheckIsMembersList(m_ed);
					didCheckIsMembersList = true;
					if (!m_isMembersList)
					{
						// we already have a list up
						// do not dismiss our list
						// combine with their completionset
						combineWithActiveList = true;
					}
				}
			}
		}
	}
	else if (gShellAttr->IsDevenv15OrHigher() && IsCFile(gTypingDevLang) && si->m_suggestionType & SUGGEST_FILE_PATH)
	{
		KillTimer(NULL, DisplayTimerID);
		curEd->KillTimer(DSM_VA_LISTMEMBERS);
		m_hasTimer = FALSE;

		if (!Psettings->mIncludeDirectiveCompletionLists)
		{
			if (IsExpUp(nullptr))
				Dismiss();
			return false;
		}

		// #include completion in dev15 -- only VA
		if (pIVsCompletionSet)
		{
			// necessary for typing to not wipe out inserted text
			pIVsCompletionSet->Dismiss();
		}

		if (IsExpUp(nullptr))
			Dismiss();

		mSpaceReserved = FALSE;
		DoCompletion(curEd, ET_EXPAND_INCLUDE, false);
		return true;
	}
	else if (gShellAttr->IsDevenv15OrHigher() && IsCFile(gTypingDevLang))
	{
		if (g_currentEdCnt && g_currentEdCnt->m_typing && Psettings->m_autoSuggest &&
		    Psettings->m_UseVASuggestionsInManagedCode && !activeCompStatusCmd)
		{
			// [case: 102216]
			m_ed = curEd;
			CheckIsMembersList(m_ed);
			didCheckIsMembersList = true;
			if (!m_isMembersList)
			{
				// ignore their suggestion
				if (pIVsCompletionSet)
					pIVsCompletionSet->Dismiss();
				return true;
			}
		}
	}

	if (combineWithActiveList)
	{
		// [case: 85074] [case: 95744]
		_ASSERTE(listIsActive);
		_ASSERTE(!m_pIVsCompletionSet2);
	}
	else if (!m_pIVsCompletionSet2)
	{
		// dismiss previous suggestions case=37498
		Dismiss();
	}
	else if (m_pIVsCompletionSet2 && pIVsCompletionSet != m_pIVsCompletionSet2)
	{
		// Filtering VB listboxes passes a NULL completionset to clear the list,
		// don't clear our list (via our Dismiss) so VA can do substring matches on them. case=40259
		m_pIVsCompletionSet2->Dismiss(); // Need to dismiss theirs before we release or VB intellisense stops working.
		m_pIVsCompletionSet2.Release();
	}

	m_ed = curEd;
	m_pIVsCompletionSet2 = pIVsCompletionSet;
	if (pIVsCompletionSet)
	{
		//////////////////////////////////////////////////////////////////////////
		// Get images
		HANDLE imgLst;
		pIVsCompletionSet->GetImageList(&imgLst);
		if (imgLst)
		{
			HWND dpiWnd = m_ed ? m_ed->m_hWnd : MainWndH;
			auto dpi = VsUI::DpiHelper::SetDefaultForWindow(dpiWnd, false);

			m_hVsCompletionSetImgList = imgLst;

			UpdateImageList((HIMAGELIST)imgLst);
		}
		SetImageList(mCombinedImgList);
		//////////////////////////////////////////////////////////////////////////
		// Get Initial Extent from their IVSCompletionSet
		{
			const WTString bb(m_ed->GetBuf(TRUE));
			SetInitialExtent(-1, -1); // reset to update m_p2's val
			UpdateCurrentPos(m_ed);
			CalculatePlacement();
			// Get p1/p2 from them
			long line, c1 = 0, c2;
			// HTML returns NO_IMPL, yet returns the correct value
			if (S_OK == pIVsCompletionSet->GetInitialExtent(&line, &c1, &c2) || c1 != 0)
			{
				long p1 = m_ed->GetBufIndex(bb, TERRCTOLONG((line + 1), (c1 + 1)));
				long p2 = m_ed->GetBufIndex(bb, TERRCTOLONG((line + 1), (c2 + 1)));
				if (p1 <= p2)
					SetInitialExtent(p1, p2);
			}
			m_typeMask = 0;
			ZeroMemory(g_imgFilter, sizeof(g_imgFilter));
		}

		{
			// We need to cache the GetBestMatch because it often fails the second time we call it.
			CStringW bstr(m_startsWith.Wide());
			long bm_item = 0;
			DWORD flags = 0;
			try
			{
				if (pIVsCompletionSet->GetBestMatch(bstr, bstr.GetLength(), &bm_item, &flags) == S_OK)
				{
					m_IvsBestMatchCached = bm_item;
					m_IvsBestMatchFlagsCached = flags;
				}
			}
			catch (const ATL::CAtlException& e)
			{
				// [case: 72245]
				// vs2012 Update 2 CTP html completion set throws an E_INVALIDARG
				// exception that VS used to catch itself.
				vLogUnfiltered("ERROR VACSX::SetIvsc  sw(%s) hr(%lx)", m_startsWith.c_str(), e.m_hr);
				return false;
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Set "All" in VB Common/All tabs. case=4558
		if (m_ed->m_ScopeLangType == VB)
		{
			CComQIPtr<IVsCompletionSetEx> pCompSetEx = pIVsCompletionSet;
			if (pCompSetEx)
				pCompSetEx->IncreaseFilterLevel(1);
		}
		//////////////////////////////////////////////////////////////////////////
		// Get IsMembersList
		MultiParsePtr mp(m_ed->GetParseDb());
		m_isDef = (mp->m_isDef || !m_ed->m_isValidScope);
		if (!didCheckIsMembersList)
			CheckIsMembersList(m_ed);

		if (m_isMembersList)
			ClearContents(); // Clear previous snippets/suggestions
		// Get g_baseExpScope for bold and sorting
		GetBaseExpScope();
	}

#ifdef Jer
	{
		FindCompletion item(m_pIVsCompletionSet2);
		_ASSERTE(item.TestSort());
	}
#endif // Jer
	KillTimer(NULL, DisplayTimerID);
	m_hasTimer = TRUE;
	mWordToLeftWhenTimerStarted = m_ed->WordLeftOfCursor();
	if (!mWordToLeftWhenTimerStarted.IsEmpty() &&
	    ('=' == mWordToLeftWhenTimerStarted[0] || '"' == mWordToLeftWhenTimerStarted[0]))
	{
		// [case: 63267] the fix for case=61510 may not be necessary anymore.
		// not able to repro original problem now - possibly due to fixes to
		// bad suggestions after end quotes?  at any rate, not removing that fix,
		// working around it for case 63267 by clearing mWordToLeftWhenTimerStarted
		mWordToLeftWhenTimerStarted.Empty();
	}

	if (gShellAttr->IsDevenv16OrHigher() && !(IsCFile(gTypingDevLang)))
	{
		// [case: 137431]
		// I don't know why this was originally set up to use a timer, but it's async now in vs2019
		// and we always use default intellisense for these languages
		// see change 14287, no fb case.
		DisplayTimerCallback();
	}
	// [case: 24159] don't use timer if keystrokes are pending
	else if (Psettings->m_UseVASuggestionsInManagedCode && !listIsActive &&
	         (!activeCompStatusCmd || !(IsCFile(gTypingDevLang))) && !::EdPeekMessage(*m_ed, TRUE))
	{
		// [case: 39051] change 14805
		SetTimer(NULL, DisplayTimerID, gShellAttr->IsDevenv16OrHigher() ? kDisplayTimerTime / 2u : kDisplayTimerTime,
		         ::DisplayTimerCallback);
	}
	else
	{
		if (gShellAttr->IsDevenv15OrHigher() && IsCFile(gTypingDevLang) && activeCompStatusCmd)
		{
			// change poptype to fix annoying behavior where ctrl+j and ctrl+space don't
			// do anything when a va list is visible
			SetPopType(ET_EXPAND_VSNET);
		}

		DisplayTimerCallback();
	}

	return true;
}

void VACompletionSetEx::DoCompletion(EdCntPtr ed, int popType, bool fixCase)
{
	if (::ShouldSuppressVaListbox(ed))
		return;

	if (gShellAttr->IsDevenv15OrHigher() && Psettings->m_bUseDefaultIntellisense && IsCFile(gTypingDevLang) &&
	    !Psettings->m_autoSuggest &&
	    !Psettings->m_codeTemplateTooltips) // snippet suggestions are independent of normal suggestions
	{
		// [case: 100685][case: 114018]
		return;
	}

	if (m_pIVsCompletionSet2)
	{
		m_ed = ed;
		UpdateCurrentPos(ed);
		CalculatePlacement();
		if (popType == ET_EXPAND_COMLETE_WORD || popType == ET_EXPAND_MEMBERS)
			SetPopType(ET_EXPAND_VSNET); // .NET flag
		KillTimer(NULL, DisplayTimerID);
		mWordToLeftWhenTimerStarted = m_ed->WordLeftOfCursor();
		SetTimer(NULL, DisplayTimerID, kDisplayTimerTime, ::DisplayTimerCallback);
		return;
	}
	else
		__super::DoCompletion(ed, popType, fixCase);
}

BOOL VACompletionSetEx::ShouldItemCompleteOn(symbolInfo* sinf, long c)
{
	BOOL res = FALSE;
	if (m_pIVsCompletionSet2 && m_expBox)
	{
		int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
		if (m_expBox->GetItemState(item, LVIS_SELECTED) != 0 &&
		    GetCompleteOnCharSet(sinf, m_ed->m_ScopeLangType).Find((char)c) !=
		        -1) // Verify c is in list of chars to expand [case=38549]
			res = TRUE;
	}

	if (!res)
		res = __super::ShouldItemCompleteOn(sinf, c);

	if (res && ' ' == c && m_ed && VB == m_ed->m_ScopeLangType)
	{
		// [case: 70663]
		// don't insert on space in "for" and "for each"
		const WTString prevWd(m_ed->CurWord(-2));
		if (0 == prevWd.CompareNoCase("for") || 0 == prevWd.CompareNoCase("each"))
		{
			res = FALSE;
		}
	}

	return res;
}

void VACompletionSetEx::Dismiss()
{
	// protect against reentrant calls
	if (mIsDismissing)
		return;

	mIsDismissing = true;
	m_hasTimer = FALSE;
	if (m_pIVsCompletionSet2)
	{
		if (gVaInteropService)
		{
			// ends up calling Dismiss() again, by way of KillAggregateFocus
			gVaInteropService->DismissCompletionSession();
		}
		m_pIVsCompletionSet2->Dismiss();
	}
	m_pIVsCompletionSet2 = NULL;
	__super::Dismiss();
	mIsDismissing = false;
}

void VACompletionSetEx::FixCase()
{
	_ASSERTE(m_pIVsCompletionSet2);
	if (m_pIVsCompletionSet2)
	{
		SymbolInfoPtr sinf = GetSymbolInfo();
		if (sinf && sinf->m_type != ET_AUTOTEXT && sinf->m_type != ET_SCOPE_SUGGESTION &&
		    sinf->m_type != ET_AUTOTEXT_TYPE_SUGGESTION && sinf->m_type != ET_VS_SNIPPET)
		{
			FindCompletion item(m_pIVsCompletionSet2);
			if (item.Find(sinf->mSymStr.Wide(), false))
			{
				// SetSel is causing the listbox to be cleared if suggestions
				// are disabled; save string before calling SetSel
				const WTString symStr(item.Sym());
				vCatLog("Editor.Events", "VACSX::FC  s(%s) sw(%s)", symStr.c_str(), m_startsWith.c_str());
				if (!symStr.CompareNoCase(m_startsWith) && symStr != m_startsWith)
				{
					SelectStartsWith();
					// [case: 29869] InsertText causes p1 and p2 to change in msdev
					const long p1 = m_p1;
					const long p2 = m_p2;
					gAutotextMgr->InsertText(m_ed, symStr, false);
					// if suggestions are disabled, we don't see the selection go away - force it (case=5906):
					m_ed->SetSel(min(p1, p2) + symStr.GetLength(), min(p1, p2) + symStr.GetLength());
				}
			}
		}
	}
}

WTString VACompletionSetEx::GetDescriptionTextOrg(/* [in] */ long iIndex, bool& shouldColorText)
{
	if (m_pIVsCompletionSet2)
	{
		SymbolInfoPtr sinf = GetSymbolInfo();
		if (sinf && sinf->m_type != ET_AUTOTEXT && sinf->m_type != ET_VS_SNIPPET &&
		    sinf->m_type != ET_SCOPE_SUGGESTION && sinf->m_type != ET_AUTOTEXT_TYPE_SUGGESTION)
		{
			FindCompletion item(m_pIVsCompletionSet2);
			if (item.Find(sinf->mSymStr.Wide()))
			{
				shouldColorText =
				    true; // case=39431: first line of text, code, in listbox tooltip no longer coloured as code
				if (sinf->m_type == ET_SUGGEST_BITS ||
				    (sinf->m_type == IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME) && !(sinf->m_type & VSNET_TYPE_BIT_FLAG)))
					shouldColorText = false;
				return item.GetDescriptionText();
			}
		}
	}
	return __super::GetDescriptionTextOrg(iIndex, shouldColorText);
}

void VACompletionSetEx::SetImageList(HANDLE hImages)
{
	if (m_pIVsCompletionSet2)
		__super::SetImageList(mCombinedImgList);
	else
		__super::SetImageList(hImages);
}

void VACompletionSetEx::OverrideSelectMode(symbolInfo* sinf, BOOL& unselect, BOOL& bFocusRectOnly)
{
	// TODO: get current setting from IDE and handle "Edit.ToggleConsumeFirstCompletionMode".
	// Note: command is not available in C++.
	static BOOL consumeFirst = FALSE;
	if (consumeFirst && !unselect && !(IsCFile(gTypingDevLang)))
		bFocusRectOnly = true;

	if (m_pIVsCompletionSet2)
	{
		const WTString cStr(sinf ? GetCStr(sinf->mSymStr) : "");
		const WTString tokField(sinf ? TokenGetField(sinf->mSymStr, WTString(AUTOTEXT_SHORTCUT_SEPARATOR)) : "");
		FindCompletion item(m_pIVsCompletionSet2);
		if (m_startsWith.GetLength() == 0 && !IsMembersList())
		{
			// [case: 55566] type "string foo = new<space>" with suggestions disabled
			if (Psettings->m_autoSuggest || !sinf || 0 != tokField.Find(cStr) || !m_ed)
				bFocusRectOnly = TRUE;
			else
			{
				const WTString prevWord(m_ed->CurWord(-1));
				if (prevWord != "new")
					bFocusRectOnly = TRUE;
			}
			return;
		}

		if (sinf)
		{
			WTString lastCstr(sinf->mSymStr);
			int pos = lastCstr.ReverseFind('.');
			if (-1 == pos)
				pos = lastCstr.ReverseFind(':');
			if (-1 != pos)
			{
				// [case: 70658] allow "bl" to select "Color.Black"
				lastCstr = GetCStr(lastCstr.Mid(pos + 1));
				const WTString lastTokField(
				    TokenGetField(sinf->mSymStr.Mid(pos + 1), WTString(AUTOTEXT_SHORTCUT_SEPARATOR)));

				if (!lastCstr.IsEmpty() && lastCstr == lastTokField &&
				    StrCmpNI(lastCstr.c_str(), m_startsWith.c_str(), m_startsWith.GetLength()) == 0 &&
				    (!IsMembersList() || Psettings->m_bAllowAcronyms))
					return;
			}
		}

		if (sinf && sinf->m_type == ET_AUTOTEXT && !m_startsWith.IsEmpty() && IsCFile(gTypingDevLang))
		{
			// [case: 100685]
			int autoTextShortcut = sinf->mSymStr.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
			if (autoTextShortcut != -1 && sinf->mSymStr.Mid(autoTextShortcut + 1) == m_startsWith)
			{
				if (m_isDef)
				{
					if (!unselect)
						unselect = TRUE;
					if (!bFocusRectOnly)
						bFocusRectOnly = TRUE;
				}
				else
				{
					if (unselect)
						unselect = FALSE;
					if (bFocusRectOnly)
						bFocusRectOnly = FALSE;
				}
				return;
			}
		}

		if (sinf && cStr != tokField && (cStr + "<>") != tokField) // "<>" check is made for [case: 53861]
		{
			bFocusRectOnly = TRUE;
			return;
		}

		if (sinf && StrCmpNI(sinf->mSymStr.c_str(), m_startsWith.c_str(), m_startsWith.GetLength()) != 0 &&
		    (!IsMembersList() || !Psettings->m_bAllowAcronyms)) // case=43470
		{
			bFocusRectOnly = TRUE;
			return;
		}

		if (((sinf && sinf->mSymStr != m_startsWith) || !sinf) && item.Find(m_startsWith.Wide()) &&
		    item.Sym() == m_startsWith)
		{
			bFocusRectOnly = TRUE; // Exact match already in list; [case=37472]
			return;
		}

		if (sinf && (sinf->m_type == ET_AUTOTEXT || sinf->m_type == ET_VS_SNIPPET) && m_ed && ISCSYM(m_ed->m_cwd[0]) &&
		    m_ed->GetSymScope().GetLength())
		{
			bFocusRectOnly =
			    TRUE; // Logic from ShouldItemCompleteOn to prevent "<a " from inserting va snippet.  case=41759
			return;
		}
	}
	else if (IsCFile(gTypingDevLang) && sinf && sinf->m_type == ET_VS_SNIPPET)
	{
		// [case: 73604] match VC++ default behavior for their snippets
		bFocusRectOnly = TRUE;
		return;
	}
}

BOOL VACompletionSetEx::ExpandCurrentSel(char key /*= '\0'*/, BOOL* didVSExpand /*= NULL*/)
{
	if (m_pIVsCompletionSet2)
	{
		// Set sinf->m_idx for original ExpandCurrentSel
		//		int count = m_pIVsCompletionSet2->GetCount();

		SymbolInfoPtr sinf = GetSymbolInfo();
		if (sinf && !m_startsWith.IsEmpty() // [case: 44023]
		    && sinf->mSymStr != m_startsWith && m_ed->m_typing)
		{
			// What the user typed does not exactly match currently selected.
			// Add all exact matches from m_pIVsCompletionSet2 before completing.
			// [case = 43501]
			FindCompletion item(m_pIVsCompletionSet2);
			if (item.Find(m_startsWith.Wide()) && AddToExpandData(&item))
			{
				DisplayList();
				sinf = GetSymbolInfo();
			}
		}

		if (m_pIVsCompletionSet2 && sinf && sinf->m_type != ET_AUTOTEXT && sinf->m_type != ET_VS_SNIPPET &&
		    sinf->m_type != ET_AUTOTEXT_TYPE_SUGGESTION)
		{
			FindCompletion item(m_pIVsCompletionSet2);
			if (item.Find(sinf->mSymStr.Wide()))
				sinf->m_idx = item.Idx();
			else if (!IS_IMG_TYPE(sinf->m_type) || IS_VSNET_IMG_TYPE(sinf->m_type)) // [case: 58753]
			{
				bool changeToAutotext = true;
				if (gShellAttr && gShellAttr->IsDevenv16OrHigher() && !sinf->mSymStr.IsEmpty())
				{
					// [case: 142352]
					// I think we're now getting m_pIVsCompletionSet2 more often than we used to now

					if (sinf->mSymStr.GetLength() > 3)
					{
						if (0xe2 == (unsigned char)sinf->mSymStr[0] && 0x98 == (unsigned char)sinf->mSymStr[1] &&
						    0x85 == (unsigned char)sinf->mSymStr[2])
						{
							if (item.SeqIntellicodeFind(sinf->mSymStr.Wide()))
							{
								// [case: 142351]
								// this is a total hack workaround for the failure of the Find
								// call further up due to some change in vs2019u7 intellicode/intellisense
								sinf->m_idx = item.Idx();
								changeToAutotext = false;
							}
						}
					}

					if (changeToAutotext)
					{
						if (sinf->m_type == FUNC && sinf->m_idx != -1)
						{
							// [case: 142352]
							// the fix for case 58753 was probably too narrow a fix (see change 17987)
							changeToAutotext = false;
						}
					}
				}

				if (changeToAutotext)
				{
					// This was in the original version of the file in change 14200 with an
					// unguarded else.
					// I have no idea what the purpose is.  I think it happens when count == 0;
					// (this assignment frequently breaks things; see case 58753, case 142351, case 142352)
					sinf->m_type = ET_AUTOTEXT;
				}
			}
		}
	}

	return __super::ExpandCurrentSel(key, didVSExpand);
}

IVsCompletionSet* VACompletionSetEx::GetIVsCompletionSet()
{
	return m_pIVsCompletionSet2 ? m_pIVsCompletionSet2.p : __super::GetIVsCompletionSet();
}
//////////////////////////////////////////////////////////////////////////
extern UINT g_baseExpScope;

bool VACompletionSetEx::AddString(BOOL sort, WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
                                  bool needDecode /*= true*/)
{
	if (m_pIVsCompletionSet2)
	{
		FindCompletion item(m_pIVsCompletionSet2);
		if (item.Find(str.Wide()) && AddToExpandData(&item))
			return false;
		else if (!IS_IMG_TYPE(type) && !IS_OBJECT_TYPE(type) && type != ET_SUGGEST_BITS)
		{
			if (RESWORD == type && gTypingDevLang == CS && Psettings->IsAutomatchAllowed() && str == "new" && m_ed &&
			    m_ed->GetSubString((ulong)m_p2, ulong(m_p2 + 1)) == ")")
			{
				// [case: 55775] auto match of parens causes C# completion set
				// behavior to change; it excludes new.  Add it back.
			}
			else
				return false;
		}
		// else, it is a suggestion from outside the using statements?
	}
	if (scopeHash == 1 && g_baseExpScope)
		scopeHash = GetMemberScopeID(str.c_str());
	return __super::AddString(sort, str, type, attrs, symID, scopeHash, needDecode);
}

bool VACompletionSetEx::AddToExpandData(FindCompletion* item, BOOL sorted /*= true*/)
{
	uint scopeID = 1;
	const WTString sym(item->Sym());
	if (sym.IsEmpty())
		return false;
	if (GetCount() < 1000) // Speed c++ global members list by not doing a db->find on every symbol
		scopeID = GetMemberScopeID(sym.c_str());
	return __super::AddString(sorted, sym, (uint)item->Type(), 0, WTHashKey(sym), scopeID, false);
}

void VACompletionSetEx::AddVSNetMembers(int nItems)
{
	CComPtr<IVsCompletionSet> pIVsCompletionSet2 = m_pIVsCompletionSet2;
	if (!pIVsCompletionSet2)
		return;

	vCatLog("Editor.Events", "VACSX::AVSNM  items(%d)", nItems);
	SetExpFlags(GetExpFlags() | VSNET_TYPE_BIT_FLAG);

	GetBaseExpScope();

	int nAdded = 0;
	WTString startswith = StartsWith();
	FindCompletion item(pIVsCompletionSet2);
	if (nItems >= 1000 && pIVsCompletionSet2->GetCount() > nItems)
	{
		// if list contains more items than we are adding, make sure the best match is added
		if (m_IvsBestMatchCached != -1)
		{
			if (item.GetItem(m_IvsBestMatchCached) && AddToExpandData(&item))
			{
				nAdded++;
				g_Guesses.SetMostLikely(item.Sym());
			}
			// Add surrounding items as well...
			for (int x = 1; x < 100 && item.GetItem(m_IvsBestMatchCached + x) && AddToExpandData(&item); x++)
				nAdded++; // add next 100 items
			for (int x = 1; x < 100 && item.GetItem(m_IvsBestMatchCached - x) && AddToExpandData(&item); x++)
				nAdded++; // add previous 100 items
		}
	}

	while (nItems == -1 || nAdded < nItems)
	{
		if (!item.GetNextMatch(m_startsWith))
			break;
		if (AddToExpandData(&item))
			nAdded++;
	}

	if (nAdded)
	{
		if (Psettings->m_bListNonInheritedMembersFirst && g_baseExpScope)
			m_ExpData.SortByScopeID(g_baseExpScope);

		if (m_hVsCompletionSetImgList)
		{
			if (CImageList::FromHandle((HIMAGELIST)m_hVsCompletionSetImgList)->GetImageCount() != g_ExImgCount)
			{
				HWND dpiWnd = m_ed ? m_ed->m_hWnd : MainWndH;
				auto dpi = VsUI::DpiHelper::SetDefaultForWindow(dpiWnd, false);

				if (UpdateImageList(
				        (HIMAGELIST)m_hVsCompletionSetImgList)) // 2010 c++, their list is dynamically loaded, update
				                                                // our list to match theirs before Display. [case=38891]
					SetImageList(mCombinedImgList);
			}
		}
		else if (g_ExImgCount != 0 || m_hCurrentImgListInUse != GetDefaultImageList())
			SetImageList(GetDefaultImageList());

		DisplayList();
	}
	else if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && m_scope == "String" && m_ed && Psettings->m_defGuesses)
	{
		// [case: 48252] pressed ctrl+space in a comment and their completion
		// set didn't have any matches
		KillTimer(NULL, DisplayTimerID);
		SetIvsCompletionSet(NULL);
		DoCompletion(m_ed, ET_EXPAND_TEXT, false);
	}
}

BOOL VACompletionSetEx::HasSelection()
{
	BOOL r = __super::HasSelection();
	if (r && m_hasTimer)
		return FALSE; // List is not complete...
	return r;
}
