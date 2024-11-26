// FindInWkspcDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "FindInWkspcDlg.h"
#include "Registry.h"
#include "RegKeys.h"
#include "DevShellAttributes.h"
#include "ImageListManager.h"
#include "FilterableSet.h"
#include "Settings.h"
#include "DpiCookbook\VsUIDpiHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const LPCTSTR kColumnWidthName(_T("%sColWidth%d"));
const UINT_PTR kFilterTimerId = 100;

/////////////////////////////////////////////////////////////////////////////
//	FindInWkspcDlg dialog

FindInWkspcDlg::FindInWkspcDlg(LPCTSTR regValueNameBase, int dlgIDD, CWnd* pParent, bool defaultWorkspaceRestriction,
                               bool saveWorkspaceRestriction)
    : CThemedVADlg((uint)dlgIDD, pParent), mSaveWorkspaceRestrictionSetting(saveWorkspaceRestriction),
      mWaitingToFilter(false), mWindowRegName(regValueNameBase)
{
	CString valueName(GetBaseRegName() + "RestrictToWorkspace");
	mRestrictToWorkspaceFiles = !!GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, valueName, defaultWorkspaceRestriction);

	//{{AFX_DATA_INIT(FindInWkspcDlg)
	//}}AFX_DATA_INIT
}

FindInWkspcDlg::~FindInWkspcDlg()
{
}

void FindInWkspcDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	//{{AFX_DATA_MAP(FindInWkspcDlg)
	DDX_Check(pDX, IDC_ALL_OPEN_FILES, mRestrictToWorkspaceFiles);

	if (IsFuzzyAvailable())
	{
		if(!pDX->m_bSaveAndValidate)
		{
			CString valueName(GetBaseRegName() + "UseFuzzySearch");
			mUseFuzzySearch = !!GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, valueName, GetUseFuzzySearchDefaultValue() ? 1u : 0u);
		}
		DDX_Check(pDX, IDC_USE_FUZZY_SEARCH, mUseFuzzySearch);
	}

	if (GetDlgItem(IDC_ITEMLIST))
	{
		DDX_Control(pDX, IDC_ITEMLIST, mFilterList); // change 4675
	}
	else
	{
		DDX_Control(pDX, IDC_FILELIST, mFilterList); // changed to IDC_FILELIST to prevent coloring (change 4673)
	}
	//}}AFX_DATA_MAP

	if (pDX->m_bSaveAndValidate)
	{
		if (mSaveWorkspaceRestrictionSetting)
		{
			CString valueName(GetBaseRegName() + "RestrictToWorkspace");
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, valueName, mRestrictToWorkspaceFiles ? 1u : 0u);
		}
		if(IsFuzzyAvailable())
		{
			CString valueName(GetBaseRegName() + "UseFuzzySearch");
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, valueName, mUseFuzzySearch ? 1u : 0u);
		}
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(FindInWkspcDlg, CThemedVADlg)
//{{AFX_MSG_MAP(FindInWkspcDlg)
ON_WM_SIZING()
ON_WM_TIMER()
ON_NOTIFY(NM_DBLCLK, IDC_ITEMLIST, OnDblclkList)
ON_NOTIFY(NM_DBLCLK, IDC_FILELIST, OnDblclkList)
ON_EN_SETFOCUS(IDC_FILTEREDIT, OnSetfocusEdit)
ON_EN_CHANGE(IDC_FILTEREDIT, OnChangeFilterEdit)
ON_BN_CLICKED(IDC_ALL_OPEN_FILES, OnToggleWorkspaceRestriction)
ON_BN_CLICKED(IDC_USE_FUZZY_SEARCH, OnUseFuzzySearch)
//}}AFX_MSG_MAP
ON_NOTIFY(LVN_ODFINDITEM, IDC_ITEMLIST, OnFindItem)
ON_NOTIFY(LVN_ODFINDITEM, IDC_FILELIST, OnFindItem)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// FindInWkspcDlg message handlers

BOOL FindInWkspcDlg::OnInitDialog()
{
	__super::OnInitDialog();
	mFilterList.ModifyStyle(0, LVS_SHAREIMAGELISTS);
	mFilterList.EnableToolTips();
	gImgListMgr->SetImgListForDPI(mFilterList, ImageListManager::bgOsWindow, LVSIL_SMALL);
	mFilterList.SetExtendedStyle(LVS_EX_INFOTIP | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

	mEdit.SubclassWindowW(::GetDlgItem(m_hWnd, IDC_FILTEREDIT));
	mEdit.SetListBuddy(mFilterList.m_hWnd);
	SetEditHelpText();
	mEdit.SetDimColor(::GetSysColor(COLOR_GRAYTEXT));

	if (gShellAttr->IsCppBuilder())
	{
		CWnd* cntrl = GetDlgItem(IDC_ALL_OPEN_FILES);
		CString wndTxt;
		cntrl->GetWindowText(wndTxt);
		wndTxt.Replace("current solution", "project group");
		cntrl->SetWindowText(wndTxt);
	}

	if (gShellAttr->IsMsdev())
	{
		CWnd* cntrl = GetDlgItem(IDC_ALL_OPEN_FILES);
		CString wndTxt;
		cntrl->GetWindowText(wndTxt);
		wndTxt.Replace("solution", "workspace");
		cntrl->SetWindowText(wndTxt);
	}
	else if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_ALL_OPEN_FILES, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_USE_FUZZY_SEARCH, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	if(!IsFuzzyAvailable())
	{
		CWnd *fuzzy_checkbox = GetDlgItem(IDC_USE_FUZZY_SEARCH);
		if(fuzzy_checkbox)
			fuzzy_checkbox->ShowWindow(SW_HIDE);
	}

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void FindInWkspcDlg::OnOK()
{
	if (mWaitingToFilter)
	{
		mWaitingToFilter = false;
		UpdateFilter();
	}
	__super::OnOK();
}

void FindInWkspcDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMITEMACTIVATE* phdn = (NMITEMACTIVATE*)pNMHDR;
	LVHITTESTINFO ht;
	ht.pt = phdn->ptAction;
	int n = ListView_SubItemHitTest(mFilterList.m_hWnd, &ht);
	if (n != -1)
		mFilterList.SetItemState(n, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	OnOK();
	*pResult = 0;
}

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

void FindInWkspcDlg::OnSizing(UINT fwSide, LPRECT pRect)
{
	pRect->right = max(pRect->right, pRect->left + 300);
	pRect->bottom = max(pRect->bottom, pRect->top + 300);
	__super::OnSizing(fwSide, pRect);
}

void FindInWkspcDlg::OnSetfocusEdit()
{
	mEdit.SetSel(0, -1);
}

void FindInWkspcDlg::OnDestroy()
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	const int kMaxColumns = mFilterList.GetHeaderCtrl()->GetItemCount();
	for (int x = 0; x < kMaxColumns; ++x)
	{
		const int width = mFilterList.GetColumnWidth(x);
		if (width > 0 && width < 4000)
		{
			CString valueName;
			CString__FormatA(valueName, kColumnWidthName, (const TCHAR*)GetBaseRegName(), x);
			::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, valueName,
			              (DWORD)VsUI::DpiHelper::DeviceToLogicalUnitsX(width));
		}
	}

	__super::OnDestroy();
}

int FindInWkspcDlg::GetInitialColumnWidth(int columnIdx, int defaultWidth) const
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	CString valueName;
	CString__FormatA(valueName, kColumnWidthName, (const TCHAR*)GetBaseRegName(), columnIdx);
	int width = (int)::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, valueName);
	if (width < 1)
		width = defaultWidth;
	width = VsUI::DpiHelper::LogicalToDeviceUnitsX(width);
	return width;
}

void FindInWkspcDlg::OnToggleWorkspaceRestriction()
{
	mEdit.SetFocus();
	UpdateData(TRUE);
	PopulateList();
}

void FindInWkspcDlg::OnUseFuzzySearch()
{
	if (!IsFuzzyAvailable())
		return;

	mEdit.SetFocus();
	UpdateData(TRUE);
	PopulateList();
}

void FindInWkspcDlg::OnChangeFilterEdit()
{
	KillTimer(kFilterTimerId);
	const UINT kTimerPeriod = (UINT)GetFilterTimerPeriod();
	if (kTimerPeriod)
	{
		mWaitingToFilter = true;
		SetTimer(kFilterTimerId, kTimerPeriod, NULL);
	}
	else
	{
		// [case: 97773]
		mWaitingToFilter = false;
		UpdateFilter();
	}
}

void FindInWkspcDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kFilterTimerId)
	{
		mWaitingToFilter = false;
		KillTimer(nIDEvent);
		UpdateFilter();
	}
	else
		__super::OnTimer(nIDEvent);
}

BOOL FindInWkspcDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNMHDR = (NMHDR*)lParam;
	if (pNMHDR->code == LVN_GETINFOTIPA)
	{
		LPNMLVGETINFOTIPA lpGetInfoTipA = (LPNMLVGETINFOTIPA)lParam;
		WTString txt;

		try
		{
			GetTooltipText(lpGetInfoTipA->iItem, txt);
		}
		catch (...)
		{
			VALOGEXCEPTION("FIWD:");
			_ASSERTE(!"FindInWkspcDlg::OnNotify exception caught");
		}

		lstrcpyn(lpGetInfoTipA->pszText, txt.c_str(), lpGetInfoTipA->cchTextMax);
		return TRUE;
	}
	else if (pNMHDR->code == LVN_GETINFOTIPW)
	{
		LPNMLVGETINFOTIPW lpGetInfoTipW = (LPNMLVGETINFOTIPW)lParam;
		CStringW txt;

		try
		{
			GetTooltipTextW(lpGetInfoTipW->iItem, txt);
		}
		catch (...)
		{
			VALOGEXCEPTION("FIWD:");
			_ASSERTE(!"FindInWkspcDlg::OnNotify exception caught");
		}

		lstrcpynW(lpGetInfoTipW->pszText, txt, lpGetInfoTipW->cchTextMax);
		return TRUE;
	}
	return __super::OnNotify(wParam, lParam, pResult);
}

// this is an implementation of less operator that works on listview items
class list_finder
{
  public:
	list_finder(HWND hwnd, const char* find, bool partial_match) : hwnd(hwnd), find(find), partial_match(partial_match)
	{
		_ASSERTE(find);
		find_len = strlen_i(find);
	}

	bool operator()(const char& _left, const char& _right) const
	{
		intptr_t left = ((intptr_t)&_left) - 1;
		intptr_t right = ((intptr_t)&_right) - 1;

		char left_match[1024];
		if (left != -1)
		{
			left_match[0] = 0;
			ListView_GetItemText(hwnd, left, 0, left_match, sizeof(left_match));
			_ASSERTE(left_match[0]);
		}
		else
		{
			strcpy(left_match, find);
		}
		char right_match[1024];
		if (right != -1)
		{
			right_match[0] = 0;
			ListView_GetItemText(hwnd, right, 0, right_match, sizeof(right_match));
			_ASSERTE(left_match[0]);
		}
		else
		{
			strcpy(right_match, find);
		}

		return ::FilteredSetStringCompareNoCase(left_match, right_match, partial_match ? find_len : 2048);
	}

  protected:
	HWND hwnd;
	const char* find;
	int find_len;
	bool partial_match;
};

#ifndef LVFI_SUBSTRING
#define LVFI_SUBSTRING 0x0004 // Same as LVFI_PARTIAL
#endif

void FindInWkspcDlg::OnFindItem(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!pResult)
		return;
	*pResult = -1;

	NMLVFINDITEM* fi = (NMLVFINDITEM*)pNMHDR;
	if (!(fi->lvfi.flags & LVFI_STRING))
		return;
	if (!fi->lvfi.psz || !fi->lvfi.psz[0])
		return;

	int count = ListView_GetItemCount(pNMHDR->hwndFrom);
	int start = fi->iStart;
	if ((start == -1) || (start == count))
		start = 0;
	count -= start;
	bool allow_wrap = (fi->lvfi.flags & LVFI_WRAP) && (start > 0);

wrap_search:
	// list item ordinals are used as iterators. however, because stl's internals try to dereference iterator (to obtain
	// a reference to a value), we just cast ordinal to char * which performs the same. however2, to prevent
	// null-pointer assert, all is increased by 1.
	char* it_begin = (char*)(intptr_t)(start + 1);
	char* it_end = (char*)(intptr_t)(start + count + 1);
	char* it_find = (char*)0; // special marker which marks an item we're trying to find
	list_finder pred(pNMHDR->hwndFrom, fi->lvfi.psz, !!(fi->lvfi.flags & (LVFI_PARTIAL | LVFI_SUBSTRING)));
	char* it_found = std::lower_bound(it_begin, it_end, *it_find, pred);
	if ((it_found != it_end) &&
	    ((it_found != it_begin) || (it_found == it_begin && it_begin == (char*)1) || !pred(*it_find, *it_begin)))
	{
		*pResult = (int)(intptr_t)it_found - 1;
		return;
	}

	if (allow_wrap)
	{
		allow_wrap = false;
		count = start;
		start = 0;
		goto wrap_search;
	}
}

void FindInWkspcDlg::ScrollItemToMiddle(int itemIndex)
{
	// [case: 19262] scroll to middle if suggestion is beyond middle
	mFilterList.EnsureVisible(itemIndex, FALSE);
	const int pageCnt = mFilterList.GetCountPerPage();
	if (pageCnt < mFilterList.GetItemCount())
	{
		const int firstVis = mFilterList.GetTopIndex();
		if ((itemIndex - firstVis) > (pageCnt / 2))
		{
			int delta = itemIndex - (firstVis + (pageCnt / 2));
			if (delta > 0)
				mFilterList.EnsureVisible(itemIndex + delta, TRUE);
		}
	}
}

void FindInWkspcDlg::SetEditHelpText()
{
	if (Psettings->mForceCaseInsensitiveFilters)
	{
		//		mEdit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"substring andSubstring , orSubstring -exclude
		//.beginWith endWith.");
		mEdit.SetDimText("substring andSubstring -exclude .beginWith endWith.");
	}
	else
	{
		//		mEdit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"substring andsubstring , orsubstring matchCase
		//-exclude .beginwith endwith.");
		mEdit.SetDimText("substring andsubstring matchCase -exclude .beginwith endwith.");
	}
	mEdit.Invalidate();
}

void FindInWkspcDlg::SetTooltipStyle(bool enableTooltips)
{
	mFilterList.EnableToolTips(enableTooltips);
	DWORD sty = mFilterList.GetExtendedStyle();
	if (enableTooltips)
		sty |= LVS_EX_INFOTIP | LVS_EX_FULLROWSELECT; // [case: 98314]
	else
		sty &= ~LVS_EX_INFOTIP;
	mFilterList.SetExtendedStyle(sty);
}

bool FindInWkspcDlg::IsFuzzyAvailable()
{
	return Psettings && Psettings->mEnableFuzzyFilters && (Psettings->mFuzzyFiltersThreshold > 0);
}

bool FindInWkspcDlg::IsFuzzyUsed() const
{
	if(!IsFuzzyAvailable())
		return false;

	assert(GetDlgItem(IDC_USE_FUZZY_SEARCH));
	return IsDlgButtonChecked(IDC_USE_FUZZY_SEARCH) == BST_CHECKED;
}
