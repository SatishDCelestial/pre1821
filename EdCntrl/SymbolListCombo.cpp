// SymbolListCombo.cpp : implementation file
//

#include "stdafxed.h"
#include "expansion.h"
#include "SymbolListCombo.h"
#include "VACompletionBox.h"
#include "FilterableSet.h"
#include "DevShellAttributes.h"
#include "..\common\TempAssign.h"
#include "StringUtils.h"
#include "PROJECT.H"
#include "VAWorkspaceViews.h"
#include "WindowUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static FilteredStringList s_data(128000);
const int kThreadFilterTimerId = 200;

/////////////////////////////////////////////////////////////////////////////
// SymbolListCombo

static WTString kDefaultTitle;

SymbolListCombo::SymbolListCombo()
    : CFastComboBox(true), mLoader(SisLoader, this, "SisLoader", false, false), mStopThread(false)
{
	s_data.Empty();
	if (kDefaultTitle.IsEmpty())
	{
		if (gShellAttr->IsMsdev())
			kDefaultTitle = "[Symbols in Workspace]";
		else
			kDefaultTitle = "[Symbols in Solution]";
	}
}

SymbolListCombo::~SymbolListCombo()
{
	StopThreadAndWait();
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(SymbolListCombo, CFastComboBox)
//{{AFX_MSG_MAP(SymbolListCombo)
ON_WM_TIMER()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

void SymbolListCombo::Init()
{
	CFastComboBox::Init();
	SetItemCount(20);
	SetItem(kDefaultTitle.c_str(), ICONIDX_SIW);
}

/////////////////////////////////////////////////////////////////////////////
// SymbolListCombo message handlers
WTString SymbolListCombo::GetItemTip(int nRow) const
{
	WTString def;
	if (GetDroppedState())
	{
		LPCTSTR pStr = s_data.GetAt(nRow);
		const WTString record(::GetNlDelimitedRecord(pStr));
		token t = record;
		WTString sym = t.read(DB_FIELD_DELIMITER_STR);
		def = t.read(DB_FIELD_DELIMITER_STR);
		WTString cmnt = GetCommentForSym(sym);

		CStringW file;
		int line;
		::GetDsItemSymLocation(pStr, file, line);
		if (file.GetLength())
		{
			CStringW tmp;
			CString__FormatW(tmp, L"\nFile: %ls", (LPCWSTR)file);
			cmnt += tmp;
		}

		if (cmnt.GetLength())
			def += WTString("\n") + cmnt;
	}
	return def;
}

CStringW SymbolListCombo::GetItemTipW(int nRow) const
{
	return CStringW(GetItemTip(nRow).Wide());
}

void SymbolListCombo::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kThreadFilterTimerId)
	{
		if (mLoader.HasStarted() && !mLoader.IsFinished())
			return;

		KillTimer(nIDEvent);
		UpdateFilter();
	}
	else
		__super::OnTimer(nIDEvent);
}

void SymbolListCombo::UpdateFilter()
{
	static BOOL sInUpdate = false;
	if (sInUpdate)
		return;

	TempTrue t(sInUpdate);

	if (mLoader.IsRunning())
	{
		KillTimer(kThreadFilterTimerId);
		SetTimer(kThreadFilterTimerId, 50, NULL);
		return;
	}

	s_data.Filter(WTString(mFilterText), false);
	SetItemCount(s_data.GetFilteredSetCount());

	if (!mFilterText.IsEmpty())
	{
		const int kSuggestionItemIdx = s_data.GetSuggestionIdx();
		SetItemState(kSuggestionItemIdx, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		SetSelectionMark(kSuggestionItemIdx);
		SetHotItem(kSuggestionItemIdx);
		EnsureVisible(kSuggestionItemIdx, FALSE);
	}

	FinishDropdown();
}

void SymbolListCombo::PopulateList()
{
	if (mLoader.IsRunning())
		StopThreadAndWait();

	mFilterText.Empty();
	mLoader.StartThread();

	// wait until thread has started so that UpdateFilter logic works properly
	while (!mLoader.HasStarted())
		Sleep(10);

	UpdateFilter();
}

void SymbolListCombo::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!s_data.GetCount() || mLoader.IsRunning())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;

	LV_ITEM* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	LPCSTR p = s_data.GetAt(iItemIndex);
	if (p)
	{
		if (*p == ':')
			p++;
		if (pItem->mask & LVIF_TEXT)
		{
			int spos = 0;
			for (int i = 0; i < pItem->cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
			{
				char c = DecodeChar(p[i]);
				if (c == ':' || c == '.')
				{
					pItem->pszText[spos++] = '.';
					// strip namespace from class, C#/VB can have really long namespaces
					for (int n = i + 1; spos && p[n] && p[n] != DB_FIELD_DELIMITER; n++)
						if (p[n] == '.' || p[n] == ':')
							spos = 0;
				}
				else
					pItem->pszText[spos++] = c;
			}

			pItem->pszText[spos] = '\0';
		}

		uint type, attrs;
		::GetDsItemTypeAndAttrsAfterText(p, type, attrs);
		pItem->iImage = GetTypeImgIdx(type, attrs);
		pItem->lParam = (LPARAM)type;
	}

	*pResult = 0;
}

void SymbolListCombo::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!s_data.GetCount() || mLoader.IsRunning())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;

	LVITEMW* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	LPCSTR p = s_data.GetAt(iItemIndex);
	if (p)
	{
		if (*p == ':')
			p++;
		if (pItem->mask & LVIF_TEXT)
		{
			int spos = 0;
			for (int i = 0; i < pItem->cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
			{
				char c = DecodeChar(p[i]);
				if (c == ':' || c == '.')
				{
					pItem->pszText[spos++] = L'.';
					// strip namespace from class, C#/VB can have really long namespaces
					for (int n = i + 1; spos && p[n] && p[n] != DB_FIELD_DELIMITER; n++)
						if (p[n] == '.' || p[n] == ':')
							spos = 0;
				}
				else
					pItem->pszText[spos++] = (WCHAR)c;
			}

			pItem->pszText[spos] = L'\0';
		}

		uint type, attrs;
		::GetDsItemTypeAndAttrsAfterText(p, type, attrs);
		pItem->iImage = GetTypeImgIdx(type, attrs);
		pItem->lParam = (LPARAM)type;
	}

	*pResult = 0;
}

bool SymbolListCombo::IsVS2010ColouringActive() const
{
	return CVS2010Colours::IsVS2010VAViewColouringActive();
}

void SymbolListCombo::OnEditChange()
{
	if (!IsWindow())
		return;

	CStringW txt;
	::GetWindowTextW(GetEditCtrl()->m_hWnd, txt);

	if (kDefaultTitle.Wide() == txt)
	{
		mFilterText.Empty();
		return;
	}

	mFilterText = txt;
	if (!mLoader.HasStarted() || !s_data.GetCount())
	{
		PopulateList();
		return;
	}

	if (mLoader.IsRunning())
		return;

	WTString cursel;
	POSITION pos = GetFirstSelectedItemPosition();
	if (pos)
	{
		const int nItem = GetNextSelectedItem(pos);
		if (nItem >= 0)
			cursel = GetItemText(nItem, 1);
	}

	if (!txt.GetLength() || cursel.Wide() != txt)
		UpdateFilter();
	else
		FinishDropdown();
}

void SymbolListCombo::OnDropdown()
{
	GetEditCtrl()->SetWindowText(_T(""));
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftSis);
	if (!IsWindow())
		Init();

	PopulateList();
	// PopulateList calls UpdateFilter via timer
	// UpdateFilter calls FinishDropdown
}

void SymbolListCombo::FinishDropdown()
{
	SetSelectionMark(s_data.GetSuggestionIdx());

	if (!GetDroppedState())
	{
		CRect rc;
		GetWindowRect(&rc);
		SetDroppedWidth((UINT)rc.Width());
		DisplayList(TRUE);
	}
}

void SymbolListCombo::OnSelect()
{
	StopThreadAndWait();
	POSITION pos = GetFirstSelectedItemPosition();
	if (!pos)
		return;

	const int nItem = GetNextSelectedItem(pos);
	const LPCSTR p = s_data.GetAt(nItem);
	if (!p)
		return;

	int i;
	for (i = 0; p[i] && p[i] != DB_FIELD_DELIMITER; i++)
		;
	WTString sym(p, i);

	CStringW file;
	int line;
	::GetDsItemSymLocation(p, file, line);
	if (!file.IsEmpty() && ::DelayFileOpen(file, line, StrGetSym(sym)))
	{
		WTString jnk;
		// check to see if sym is sufficient for MRU by doing what GoToDef
		// does without actually loading file.
		if (::GetGoToDEFLocation(sym, file, line, jnk))
		{
			// this will not necessarily go to same place that
			// GetDsItemSymLocation went to if there were dupes.
			// The MRU doesn't handle dupes.
			LVITEM item;
			::memset(&item, 0, sizeof(LVITEM));

			item.mask = LVIF_IMAGE;
			item.iItem = nItem;
			item.iSubItem = 0;
			GetItem(&item);
			::SendMessage(::GetParent(m_hWnd), WM_VA_GOTOCLASSVIEWITEM, (WPARAM)item.iImage, (LPARAM)sym.c_str());
		}
	}
	else if (::GoToDEF(sym))
	{
		LVITEM item;
		::memset(&item, 0, sizeof(LVITEM));

		item.mask = LVIF_IMAGE;
		item.iItem = nItem;
		item.iSubItem = 0;
		GetItem(&item);
		::SendMessage(::GetParent(m_hWnd), WM_VA_GOTOCLASSVIEWITEM, (WPARAM)item.iImage, (LPARAM)sym.c_str());
	}
	else
	{
		const WTString record(::GetNlDelimitedRecord(p));
		token t = record;
		WTString tmp = t.read(DB_FIELD_DELIMITER_STR);
		tmp.ReplaceAll(".", ":");
		int lineNo;
		const CStringW file2(::GetSymbolLoc(tmp, lineNo));
		if (file2.IsEmpty())
			return;
		::DelayFileOpen(file2, lineNo, StrGetSym(sym), TRUE);
	}
}

void SymbolListCombo::GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const
{
	title = kDefaultTitle;
	iconIdx = ICONIDX_SIW;
}

void SymbolListCombo::SisLoader(LPVOID pSymbolListCombo)
{
	SymbolListCombo* _this = (SymbolListCombo*)pSymbolListCombo;
	_this->PopulateListThreadFunc();
	_this->mStopThread = false;
}

void SymbolListCombo::PopulateListThreadFunc()
{
	if (mStopThread)
		return;

	if (!GlobalProject || (!GlobalProject->GetFileItemCount() && GlobalProject->SolutionFile().IsEmpty()))
	{
		s_data.Empty();
		return;
	}

	s_data.ReadDB();
	if (mStopThread)
		return;

	s_data.Sort(::FilteredSetStringCompareNoCase);
	if (mStopThread)
		return;

	s_data.RemoveDuplicates();
}

void SymbolListCombo::StopThreadAndWait()
{
	if (mLoader.IsRunning())
	{
		mStopThread = true;
		CWaitCursor curs;
		if (::GetCurrentThreadId() == g_mainThread)
			mLoader.Wait(10000);
		else
			mLoader.Wait(INFINITE);
	}
}

void SymbolListCombo::OnCloseup()
{
	StopThreadAndWait();
	s_data.Empty();

	if (GetItemCount() == 0)
		GetEditCtrl()->SetSel(0, -1);

	// sets initial height for next time list is dropped
	SetItemCount(20);
}
