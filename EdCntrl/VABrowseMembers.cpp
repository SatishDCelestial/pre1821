// VABrowseMembers.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "VABrowseMembers.h"
#include "edcnt.h"
#include "VACompletionBox.h"
#include "VAFileView.h"
#include "FilterableSet.h"
#include "fdictionary.h"
#include "MenuXP\Tools.h"
#include "WindowUtils.h"
#include "expansion.h"
#include "VARefactor.h"
#include "Mparse.h"
#include "VaService.h"
#include "FindReferences.h"
#include "project.h"
#include "FileTypes.h"
#include "Registry.h"
#include "RegKeys.h"
#include "VAAutomation.h"
#include "ColorSyncManager.h"
#include "StringUtils.h"
#include "ImageListManager.h"
#include <Strsafe.h>
#include "BaseClassList.h"
#include "SpinCriticalSection.h"
#include "VAWatermarks.h"

#pragma warning(disable : 4505) // vectimp inlines not used

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CStringW sMemberFilterString;
const int kThreadFilterTimerId = 200;
#define ID_REFACTOR_FINDREFS 0xE150
/*#define ID_TOGGLE_EDITOR_SELECTION_PREPOPULATE	0xE151*/
#define ID_TOGGLE_CASE_SENSITIVITY 0xe152
#define ID_TOGGLE_TOOLTIPS 0xe153

/////////////////////////////////////////////////////////////////////////////
// VABrowseMembers dialog

void VABrowseMembersDlg(DType* type)
{
	VABrowseMembers dlg(type);
	dlg.DoModal();
}

VABrowseMembers::VABrowseMembers(DType* type)
    : FindInWkspcDlg("MbrDlg", VABrowseMembers::IDD, NULL, true, true), m_data(512), m_type(type),
      mLoader(MemberLoader, this, "MemberLoader", false, false)
{
	SetHelpTopic("dlgGotoMember");

	if (gTestsActive)
	{
		mSortByType = true;
		mShowBaseMembers = true;
	}
	else
	{
		const CString valueName1(GetBaseRegName() + "ShowBaseMembers");
		mShowBaseMembers = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, valueName1, true);

		const CString valueName2(GetBaseRegName() + "SortByType");
		mSortByType = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, valueName2, true);
	}

	switch (m_type->MaskedType())
	{
	case CLASS:
	case STRUCT:
	case TYPE:
	case C_INTERFACE:
		mCanHaveBaseMembers = true;
		mCanSortByType = true;
		break;

	case NAMESPACE:
		mCanHaveBaseMembers = false;
		mCanSortByType = true;
		break;

	case C_ENUM:
	default:
		mCanHaveBaseMembers = false;
		mCanSortByType = false;
	}
}

void VABrowseMembers::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VABrowseMembers)

	int isChkShowBaseMembers = mShowBaseMembers;
	DDX_Check(pDX, IDC_SHOW_BASE_MEMBERS, isChkShowBaseMembers);

	int isChkSortByType = mSortByType;
	DDX_Check(pDX, IDC_SORT_BY_TYPE, isChkSortByType);

	//}}AFX_DATA_MAP
	if (pDX->m_bSaveAndValidate)
	{
		mSortByType = !!isChkSortByType;
		mShowBaseMembers = !!isChkShowBaseMembers;

		if (!gTestsActive)
		{
			const CString valueName1(GetBaseRegName() + "ShowBaseMembers");
			SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, valueName1, mShowBaseMembers);

			const CString valueName2(GetBaseRegName() + "SortByType");
			SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, valueName2, mSortByType);
		}
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VABrowseMembers, FindInWkspcDlg)
//{{AFX_MSG_MAP(VABrowseMembers)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_WM_TIMER()
ON_WM_DESTROY()
ON_NOTIFY(LVN_GETDISPINFO, IDC_ITEMLIST, OnGetdispinfo)
ON_NOTIFY(LVN_GETDISPINFOW, IDC_ITEMLIST, OnGetdispinfoW)
ON_COMMAND(ID_EDIT_COPY, OnCopy)
ON_COMMAND(ID_REFACTOR_FINDREFS, OnRefactorFindRefs)
ON_COMMAND(ID_TOGGLE_CASE_SENSITIVITY, OnToggleCaseSensitivity)
ON_COMMAND(ID_TOGGLE_TOOLTIPS, OnToggleTooltips)
ON_BN_CLICKED(IDC_SORT_BY_TYPE, OnToggleSortByType)
ON_BN_CLICKED(IDC_SHOW_BASE_MEMBERS, OnToggleShowBaseMembers)

ON_WM_SHOWWINDOW()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

IMPLEMENT_MENUXP(VABrowseMembers, FindInWkspcDlg);
/////////////////////////////////////////////////////////////////////////////
// VABrowseMembers message handlers

void VABrowseMembers::OnOK()
{
	StopThreadAndWait();
	FindInWkspcDlg::OnOK();
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;

		auto dtype = m_data.GetAt(itemRow);
		GotoSymbol(*dtype);
		//
		// 		LPCTSTR pItem = m_data.GetAt(itemRow);
		// 		const CString record(::GetNlDelimitedRecord(pItem));
		// 		token t = record;
		// 		t.ReplaceAll(".", ":");
		// 		WTString sym = t.read(DB_FIELD_DELIMITER_STR);
		// 		CStringW file;
		// 		int line;
		// 		::GetDsItemSymLocation(pItem, file, line);
		// 		// 		if (file.IsEmpty() && g_currentEdCnt)
		// 		// 		{
		// 		//			// added this for .net symbols, but seems ridiculous to wait
		// 		//			// for FSIS and then wait for browser afterwards
		// 		// 			WTString browseCmd(WTString("View.ObjectBrowserSearch ") + CleanScopeForDisplay(sym));
		// 		// 			g_currentEdCnt->SendVamMessage(VAM_EXECUTECOMMAND, (long)browseCmd.c_str(), 0);
		// 		// 		}
		// 		// 		else
		// 		if (!file.IsEmpty() && ::DelayFileOpen(file, line, StrGetSym(sym)))
		// 		{
		// 			WTString jnk;
		// 			// check to see if sym is sufficient for MRU by doing what GoToDef
		// 			// does without actually loading file.
		// 			if (::GetGoToDEFLocation(sym, file, line, jnk))
		// 			{
		// 				// this will not necessarily go to same place that
		// 				// GetDsItemSymLocation went to if there were dupes.
		// 				// The MRU doesn't handle dupes.
		// 				::VAProjectAddScopeMRU(sym);
		// 			}
		// 		}
		// 		else
		// 		{
		// 			// the old way of doing things
		// 			if (::GoToDEF(sym))
		// 				::VAProjectAddScopeMRU(sym);
		// 			else
		// 			{
		// 				CStringW file;
		// 				int line;
		// 				GetItemLocation(itemRow, file, line);
		// 				if (file.IsEmpty())
		// 					return;
		// 				::DelayFileOpen(file, line, StrGetSym(sym));
		// 			}
		// 		}
	}
}

void VABrowseMembers::PopulateList()
{
	WTString title;
	title.WTFormat("Members of %s [loading...]", CleanScopeForDisplay(m_type->Sym()).c_str());
	VAUpdateWindowTitle(VAWindowType::BrowseMembers, title, 1);
	::SetWindowTextW(m_hWnd, (LPCWSTR)title.Wide());

	if (mLoader.HasStarted() && !mLoader.IsFinished())
		StopThreadAndWait();

	//	m_data.SetSymbolTypes(mSymbolFilter);
	mLoader.StartThread();

	// wait until thread has started so that UpdateFilter logic works properly
	while (!mLoader.HasStarted())
		Sleep(10);

	UpdateFilter();
}

BOOL VABrowseMembers::OnInitDialog()
{
	// should be added before FindInWkspcDlg::OnInitDialog because it invokes initialisation
	if (CVS2010Colours::IsExtendedThemeActive())
	{
		// done in base class
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_SHOW_BASE_MEMBERS, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_SORT_BY_TYPE, this);
	}

	__super::OnInitDialog();

	GetDlgItem(IDC_SHOW_BASE_MEMBERS)->EnableWindow(mCanHaveBaseMembers);
	GetDlgItem(IDC_SORT_BY_TYPE)->EnableWindow(mCanSortByType);

	// make list control unicode
	mFilterList.SendMessage(CCM_SETUNICODEFORMAT, 1, 0);

	AddSzControl(IDC_ITEMLIST, mdResize, mdResize);
	AddSzControl(IDC_FILTEREDIT, mdResize, mdRepos);
	AddSzControl(IDC_SHOW_BASE_MEMBERS, mdNone, mdRepos);
	AddSzControl(IDC_SORT_BY_TYPE, mdHalfpossize, mdRepos);
	AddSzControl(IDC_USE_FUZZY_SEARCH, mdRepos, mdRepos);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);

	CRect rc;
	mFilterList.GetClientRect(&rc);

	int defColWidth = rc.Width() / 4;
	int width;
	width = GetInitialColumnWidth(0, defColWidth);
	mFilterList.InsertColumn(0, "Symbol", LVCFMT_LEFT, width);
	width = GetInitialColumnWidth(1, defColWidth);
	mFilterList.InsertColumn(1, "Scope", LVCFMT_LEFT, width);
	width = GetInitialColumnWidth(2, 2 * defColWidth);
	mFilterList.InsertColumn(2, "Definition", LVCFMT_LEFT, width);

	SetTooltipStyle(Psettings->mBrowseMembersTooltips);

	PopulateList();
	mFilterEdited = false;

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VABrowseMembers::OnTimer(UINT_PTR nIDEvent)
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

void VABrowseMembers::UpdateFilter(bool force /*= false*/)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		KillTimer(kThreadFilterTimerId);
		SetTimer(kThreadFilterTimerId, 50, NULL);
		return;
	}

	CWaitCursor curs;
	const CStringW previousFilter(sMemberFilterString);
	mEdit.GetText(sMemberFilterString);
	sMemberFilterString = StrMatchOptions::TweakFilter(sMemberFilterString);
	if (previousFilter != sMemberFilterString)
		mFilterEdited = true;
	m_data.Filter(WTString(sMemberFilterString), IsFuzzyUsed(), force);
	if (mFilterList.GetItemCount() < 50000)
	{
		// when list is very large, deleteAllItems has horrible performance.
		// DeleteAllItems is required for ScrollItemToMiddle to work for letters
		// beyond the first typed.
		// ScrollItemToMiddle not functional until item count is < xxK items.
		// case=20182
		mFilterList.DeleteAllItems();
	}
	const int kSetCount = m_data.GetFilteredSetCount();
	mFilterList.SetItemCount(kSetCount);
	gImgListMgr->SetImgListForDPI(mFilterList, ImageListManager::bgList, LVSIL_SMALL);
	mFilterList.Invalidate();
	if (kSetCount)
	{
		POSITION p = mFilterList.GetFirstSelectedItemPosition();
		// clear selection
		for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
		{
			mFilterList.SetItemState((int)(intptr_t)p - 1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			mFilterList.GetNextSelectedItem(p);
		}
		const int kSuggestedItem = m_data.GetSuggestionIdx();
		mFilterList.SetItemState(kSuggestedItem, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		// [case: 19262] scroll to middle if suggestion is beyond middle
		ScrollItemToMiddle(kSuggestedItem);
	}

	UpdateTitle();
}

// Windows is asking for the text to display, at item(iItemIndex), subitem(pItem->iSubItem)
void VABrowseMembers::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFOA* pDispInfo = (LV_DISPINFOA*)pNMHDR;
	LV_ITEMA* pItem = &pDispInfo->item;
	auto dtype = m_data.GetAt(pItem->iItem);

	if (dtype)
	{
		dtype->LoadStrs();
		switch (pItem->iSubItem)
		{
		case 0:
			if (pItem->mask & LVIF_TEXT)
			{
				WTString tmp = DecodeScope(dtype->Sym());
				::StringCchCopyA(pItem->pszText, (uint)pItem->cchTextMax, tmp.c_str());
			}
			if (pItem->mask & LVIF_IMAGE)
				pItem->iImage = GetTypeImgIdx(dtype->MaskedType(), dtype->Attributes());
			break;

		case 1:
			if (pItem->mask & LVIF_TEXT)
			{
				WTString tmp = CleanScopeForDisplay(dtype->Scope());
				if (g_currentEdCnt && IsCFile(g_currentEdCnt->m_ftype))
					tmp.ReplaceAll(".", "::");
				::StringCchCopyA(pItem->pszText, (uint)pItem->cchTextMax, tmp.c_str());
			}
			break;

		case 2:
			if (pItem->mask & LVIF_TEXT)
			{
				token2 tk(dtype->Def());
				WTString def = tk.read2("\f");
				def = ::CleanDefForDisplay(def, gTypingDevLang);
				::StringCchCopyA(pItem->pszText, (uint)pItem->cchTextMax, def.c_str());
			}
			break;
		}
	}

	*pResult = 0;
}

void VABrowseMembers::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;
	LV_ITEMW* pItem = &(pDispInfo)->item;
	auto dtype = m_data.GetAt(pItem->iItem);

	if (dtype)
	{
		dtype->LoadStrs();
		switch (pItem->iSubItem)
		{
		case 0:
			if (pItem->mask & LVIF_TEXT)
			{
				WTString tmp = DecodeScope(GetSymOperatorSafe(dtype));
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, tmp.Wide());
			}
			if (pItem->mask & LVIF_IMAGE)
				pItem->iImage = GetTypeImgIdx(dtype->MaskedType(), dtype->Attributes());
			break;

		case 1:
			if (pItem->mask & LVIF_TEXT)
			{
				WTString tmp = CleanScopeForDisplay(dtype->Scope());
				if (g_currentEdCnt && IsCFile(g_currentEdCnt->m_ftype))
					tmp.ReplaceAll(".", "::");
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, tmp.Wide());
			}
			break;

		case 2:
			if (pItem->mask & LVIF_TEXT)
			{
				token2 tk(dtype->Def());
				WTString def = tk.read2("\f");
				def = ::CleanDefForDisplay(def, gTypingDevLang);
				CStringW defW = def.Wide();
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, defW);
			}
			break;
		}
	}

	if (pItem->mask & LVIF_INDENT)
	{
		pItem->iIndent = 0;
	}

	*pResult = 0;
}

void VABrowseMembers::OnDestroy()
{
	StopThreadAndWait();
	mEdit.GetText(sMemberFilterString);
	__super::OnDestroy();
}

void VABrowseMembers::GetItemLocation(int itemRow, CStringW& file, int& lineNo)
{
	auto dtype = m_data.GetAt(itemRow);
	file = dtype->FilePath();
	if (file.IsEmpty())
	{
		lineNo = 0;
		return;
	}
	lineNo = dtype->Line();
}

void VABrowseMembers::GetTooltipText(int itemRow, WTString& txt)
{
	CStringW tmp;
	int line;
	GetItemLocation(itemRow, tmp, line);
	if (tmp.IsEmpty())
		txt = mFilterList.GetItemText(itemRow, 0);
	else
		txt.WTFormat("%s\n\nFile: %s:%d", (LPCTSTR)mFilterList.GetItemText(itemRow, 0), WTString(tmp).c_str(), line);
}

void VABrowseMembers::GetTooltipTextW(int itemRow, CStringW& txt)
{
	CStringW tmp;
	int line;
	GetItemLocation(itemRow, tmp, line);
	if (tmp.IsEmpty())
		txt = mFilterList.GetItemTextW(itemRow, 0);
	else
		CString__FormatW(txt, L"%s\n\nFile: %s:%d", (LPCWSTR)mFilterList.GetItemTextW(itemRow, 0), (LPCWSTR)tmp, line);
}

void VABrowseMembers::UpdateTitle()
{
	WTString title;
	title.WTFormat("Members of %s [%d of %d]", CleanScopeForDisplay(m_type->Sym()).c_str(),
	               m_data.GetFilteredSetCount(), m_data.GetCount());
	VAUpdateWindowTitle(VAWindowType::BrowseMembers, title, 2);
	::SetWindowTextW(m_hWnd, (LPCWSTR)title.Wide());

	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		WTString logStr;
		logStr.WTFormat("Browse Members dlg: sym(%s) title(%s)\r\n", m_type ? m_type->SymScope().c_str() : "no data",
		                title.c_str());
		gTestLogger->LogStr(logStr);
	}
}

int VABrowseMembers::GetFilterTimerPeriod() const
{
	const int kWorkingSetCount = GetWorkingSetCount();
	if (kWorkingSetCount < 1000)
		return 0;
	else if (kWorkingSetCount < 10000)
		return 50;
	else if (kWorkingSetCount < 25000)
		return 100;
	else if (kWorkingSetCount < 50000)
		return 150;
	else if (kWorkingSetCount < 100000)
		return 200;
	else if (kWorkingSetCount < 200000)
		return 250;
	else if (kWorkingSetCount < 500000)
		return 300;
	else
		return 400;
}

void VABrowseMembers::OnCopy()
{
	CStringW clipTxt;

	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;

		clipTxt += CStringW(mFilterList.GetItemText(itemRow, 0));
		if (n > 1)
			clipTxt += L"\r\n";
	}

	if (clipTxt.GetLength() && g_currentEdCnt)
		::SaveToClipboard(m_hWnd, clipTxt);
}

void VABrowseMembers::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	uint selItemCnt = mFilterList.GetSelectedCount();

	_ASSERTE((selItemCnt > 0 && p) || (!selItemCnt && !p));

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && p)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (mFilterList.GetItemRect((int)(intptr_t)p - 1, &rc, LVIR_ICON))
		{
			mFilterList.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}
	else if (!selItemCnt && !p && pos == CPoint(-1, -1))
	{
		// [case: 78216] nothing selected and not a click
		mFilterList.GetClientRect(&rc);
		mFilterList.ClientToScreen(&rc);
		pos.x = rc.left + 16;
		pos.y = rc.top + 16;
	}

	const DWORD kSelectionDependentItemState = (p) ? 0u : MF_GRAYED | MF_DISABLED;

	CMenu contextMenu;
	contextMenu.CreatePopupMenu();
	contextMenu.AppendMenu(kSelectionDependentItemState | MF_DEFAULT, MAKEWPARAM(IDOK, ICONIDX_REFERENCE_GOTO_DEF),
	                       "&Goto");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_COPY, ICONIDX_COPY), "&Copy\tCtrl+C");

	GetDataForCurrentSelection();
	if (mDtypes.size())
	{
		DType sym(*mDtypes.begin());
		const BOOL kCanFindRefs = VARefactorCls::CanFindReferences(&sym);
		if (kCanFindRefs)
			contextMenu.AppendMenu(0, MAKEWPARAM(ID_REFACTOR_FINDREFS, ICONIDX_REFERENCE_FIND_REF),
			                       "Find &References\tCtrl+F");
	}
	contextMenu.AppendMenu(MF_SEPARATOR);
	// command text corresponds to inverted setting
	contextMenu.AppendMenu(UINT(Psettings->mForceCaseInsensitiveFilters ? MF_ENABLED : MF_ENABLED | MF_CHECKED),
	                       MAKEWPARAM(ID_TOGGLE_CASE_SENSITIVITY, 0),
	                       "&Match case of search strings that contain uppercase letters");
	contextMenu.AppendMenu(Psettings->mBrowseMembersTooltips ? MF_CHECKED : 0u, ID_TOGGLE_TOOLTIPS, "&Tooltips");

	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

BOOL VABrowseMembers::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == mFilterList.m_hWnd || pMsg->hwnd == mEdit.m_hWnd)
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if ((GetKeyState(VK_CONTROL) & 0x1000))
			{
				if (pMsg->wParam == 'C')
				{
					// Ctrl+C
					if (pMsg->hwnd == mEdit.m_hWnd)
					{
						// [case: 65917]
						// don't handle as OnCopy() if edit ctrl has selected text
						int p1, p2;
						p1 = p2 = 0;
						mEdit.GetSel(p1, p2);
						if (p1 != p2)
							return __super::PreTranslateMessage(pMsg);
					}

					OnCopy();
					return false;
				}

				if (pMsg->wParam == 'F')
				{
					// Ctrl+F
					OnRefactorFindRefs();
					return false;
				}

				if (pMsg->wParam == 'A' && pMsg->hwnd == mEdit.m_hWnd)
				{
					// Ctrl+A
					// I was getting some strange behavior with default Ctrl+A behavior
					// caret in strange position and if text had a '-' sometimes GetWindowText
					// would come back empty.
					mEdit.SetSel(0, -1);
					return false;
				}
			}
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

void VABrowseMembers::OnRefactorFindRefs()
{
	GetDataForCurrentSelection();
	if (gVaService && mDtypes.size())
	{
		OnCancel();
		DType sym(*mDtypes.begin());
		gVaService->FindReferences(FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars,
		                           GetTypeImgIdx(sym.MaskedType(), sym.Attributes()), sym.SymScope());
		mDtypes.clear();
	}
}

void VABrowseMembers::GetDataForCurrentSelection()
{
	mDtypes.clear();
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;
		auto dtype = m_data.GetAt(itemRow);
		if (dtype)
		{
			mDtypes.push_back(*dtype);
			return;
		}
	}
}

int VABrowseMembers::GetWorkingSetCount() const
{
	CStringW newFilter;
	mEdit.GetText(newFilter);
	bool filterCurrentSet = false;
	if (m_data.GetFilteredSetCount() && sMemberFilterString.GetLength() &&
	    sMemberFilterString.GetLength() <= newFilter.GetLength() && newFilter.Find(sMemberFilterString) == 0 &&
	    newFilter.Find(L'-') == -1)
	{
		filterCurrentSet = true;
	}

	return filterCurrentSet ? m_data.GetFilteredSetCount() : m_data.GetCount();
}

void VABrowseMembers::OnToggleSortByType()
{
	mEdit.SetFocus();
	if (mFilterEdited)
		mEdit.SetSel(-1, -1); // [case: 52462]
	UpdateData(TRUE);
	PopulateList();
}

void VABrowseMembers::OnToggleShowBaseMembers()
{
	mEdit.SetFocus();
	if (mFilterEdited)
		mEdit.SetSel(-1, -1); // [case: 52462]
	UpdateData(TRUE);
	PopulateList();
}

void VABrowseMembers::MemberLoader(LPVOID pVaBrowseSym)
{
	VABrowseMembers* _this = (VABrowseMembers*)pVaBrowseSym;
	_this->PopulateListThreadFunc();
	_this->mStopThread = false;
}

void VABrowseMembers::PopulateListThreadFunc()
{
	if (mStopThread)
		return;

	m_data.Empty();
	m_results.clear();

	if (!GlobalProject || (!GlobalProject->GetFileItemCount() && GlobalProject->SolutionFile().IsEmpty()))
	{
		return;
	}

	DTypeList& members = m_results;
	EdCntPtr ed(g_currentEdCnt);
	// [case: 100459]
	MultiParsePtr mp = ed ? ed->GetParseDb() : MultiParse::Create();
	WTString bcl;

	// don't get base classes of namespaces, it returns wildcard
	if (m_type->MaskedType() == NAMESPACE)
	{
		bcl = m_type->SymScope();
	}
	else
	{
		if (mCanHaveBaseMembers && mShowBaseMembers)
		{
			BaseClassFinder bcf(gTypingDevLang);
			bcl = bcf.GetBaseClassList(mp, m_type->SymScope());
			if (bcl == "\f:\f")
			{
				// [case: 100600]
				// retry, ignoring cached bcl
				bcl = bcf.GetBaseClassList(mp, m_type->SymScope(), TRUE);
			}

			bcl.ReplaceAll(":System:Object\f", "");
		}
		else
		{
			bcl = m_type->SymScope();
		}
	}

	ScopeHashAry hashArr;
	hashArr.Init(NULLSTR, bcl, "");
	CSpinCriticalSection memCs;

	auto fn = [&hashArr, &members, &memCs](DType* dt, bool checkInvalidSys) {
		if (dt->IsHideFromUser())
			return;

		if (dt->MaskedType() == TEMPLATETYPE)
			return;

		// skip C# "base"
		static const uint kBaseHash = DType::HashSym("base");
		if (dt->MaskedType() == VAR && dt->SymHash() == kBaseHash)
			return;

		if (!dt->ScopeHash())
			return;

		if (!hashArr.Contains(dt->ScopeHash()))
			return;

		// [case: 65910]
		// moved from FileFic::ForEach due to poor performance there
		if (checkInvalidSys && ::IsFromInvalidSystemFile(dt))
			return;

		AutoLockCs l(memCs);
		members.push_back(dt);
	};

	mp->ForEach(fn, mStopThread);
	//	m_data.ReadDB(IsRestrictedToWorkspace());

	if (mStopThread)
		return;

	if (Psettings && Psettings->mRestrictGotoMemberToProject)
	{
		members.FilterNonActive();
	}
	else
	{
		// [case: 142090] do not filter symbols from other projects
		members.FilterNonActiveSystemDefs();
	}

	if (mStopThread)
		return;

	for (auto& dt : members)
	{
		m_data.AddItem(&dt);
	}

	if (mStopThread)
		return;

	m_data.PreSort();

	if (mStopThread)
		return;

	m_data.RemoveDuplicates();

	if (mStopThread)
		return;

	m_data.PostSort(mCanSortByType && mSortByType);
}

void VABrowseMembers::StopThreadAndWait()
{
	if (!mLoader.IsFinished())
	{
		mStopThread = true;
		CWaitCursor curs;
		if (::GetCurrentThreadId() == g_mainThread)
			mLoader.Wait(10000);
		else
			mLoader.Wait(INFINITE);
	}
}

void VABrowseMembers::OnToggleCaseSensitivity()
{
	Psettings->mForceCaseInsensitiveFilters = !Psettings->mForceCaseInsensitiveFilters;
	SetEditHelpText();
	UpdateFilter(true);
}

void VABrowseMembers::OnToggleTooltips()
{
	Psettings->mBrowseMembersTooltips = !Psettings->mBrowseMembersTooltips;
	SetTooltipStyle(Psettings->mBrowseMembersTooltips);
}
