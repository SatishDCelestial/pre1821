#include "StdAfxEd.h"
#include "resource.h"
#include "VaHashtagsFrame.h"
#include "VaService.h"
#include "PROJECT.H"
#include "Mparse.h"
#include "ImageListManager.h"
#include "RedirectRegistryToVA.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "MenuXP\Draw.h"
#include "KeyBindings.h"
#include "EDCNT.H"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include <vector>
#include "DBFile\VADBFile.h"
#include "ParseThrd.h"
#include "FILE.H"
#include "Expansion.h"
#include "HashtagsManager.h"
#include "ProjectInfo.h"
#include "Registry.h"
#include "RegKeys.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// from FindUsageDlg.cpp
extern WTString GetSurroundingLineText(const CStringW& fileName, int centerLine, int contextLines);
extern void TrimIncompleteCstyleComments(WTString& text);
extern void HandleTooltipWhitespace(WTString& text);

static CStringW dummy_item_text = L"dummy";

// messages
#define WM_VA_INSERTITEMS WM_USER + 20

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(HashtagsColumnTreeWnd, CColumnTreeWnd2)
ON_NOTIFY(NM_RETURN, TreeID, OnSelect)
ON_NOTIFY(NM_CLICK, TreeID, OnClickTree)
ON_NOTIFY(NM_DBLCLK, TreeID, OnDblClickTree)
ON_WM_SIZE()
END_MESSAGE_MAP()
#pragma warning(pop)

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

LRESULT
HashtagsColumnTreeWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT r = __super::WindowProc(message, wParam, lParam);

	if (WM_KILLFOCUS == message || WM_KEYDOWN == message)
	{
		if ((message == WM_KILLFOCUS && (::GetKeyState(VK_ESCAPE) & 0x1000)) ||
		    (message == WM_KEYDOWN && wParam == VK_ESCAPE))
		{
			//			m_parent.OnTreeEscape();
		}

		if (WM_KILLFOCUS == message)
			GetTreeCtrl().PopTooltip();
	}

	return r;
}

void HashtagsColumnTreeWnd::OnDblClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	m_parent.OnDoubleClickTree();
}

void HashtagsColumnTreeWnd::OnSize(UINT nType, int cx, int cy)
{
	//	__super::OnSize(nType, cx, cy);

	CWnd::OnSize(nType, cx, cy);

	HDITEM item;
	item.mask = HDI_WIDTH;
	item.cxy = cx - 4;
	GetHeaderCtrl().SetItem(0, &item);
	UpdateColumns();
}

void HashtagsColumnTreeWnd::OnDpiChanged(DpiChange change, bool& handled)
{
	// throw std::logic_error("The method or operation is not implemented.");
}

void HashtagsColumnTreeWnd::OnClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	DWORD dwpos = ::GetMessagePos();
	TVHITTESTINFO ht = {{0}};
	ht.pt.x = GET_X_LPARAM(dwpos);
	ht.pt.y = GET_Y_LPARAM(dwpos);
	::MapWindowPoints(HWND_DESKTOP, pNMHDR->hwndFrom, &ht.pt, 1);

	TreeView_HitTest(pNMHDR->hwndFrom, &ht);

	if (TVHT_ONITEMSTATEICON & ht.flags)
	{
		// clicked to check/un-check
		BOOL checking = !m_Tree.GetCheck(ht.hItem);
		HTREEITEM refItems = m_parent.mtree__GetChildItem(ht.hItem);
		// if file check/un-check children
		while (refItems)
		{
			m_Tree.SetCheck(refItems, checking);
			refItems = m_Tree.GetNextSiblingItem(refItems);
		}
	}
}

void HashtagsColumnTreeWnd::OnSelect(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	if (!m_parent.GoToSelectedItem())
	{
		// the selected item wasn't navigable
		// toggle open/close state of the item instead
		// this makes enter/return behave the same as double-click
		HTREEITEM item = m_Tree.GetSelectedItemIfSingleSelected();
		if (item)
			m_Tree.Expand(item, TVE_TOGGLE);
	}
}

// columns definitions
static const struct
{
	const char* name;
	int default_width;
	bool default_hidden;
} column_definitions[__column_countof] = {{"Reference", 500, false},
#ifdef ALLOW_MULTIPLE_COLUMNS
                                          {"Line", 80, false},
                                          {"Type", 80, false},
                                          {"Context", 200, false}
#endif
};

#define ID_RK_HASHTAGS_FILTER "HashtagsFilter"
#define ID_RK_HASHTAGS_SHOWHIDDENDIMMED "HashtagsShowHiddenAsDimmed"
#define ID_RK_HASHTAGS_SHOWTOOLTIPS "HashtagsShowTooltips"

VaHashtagsFrame::VaHashtagsFrame(HWND hWndParent)
    : VADialog(IDD_VAHASHTAGS, CWnd::FromHandle(hWndParent), fdAll, flAntiFlicker | flNoSavePos),
      mParent(gShellAttr->IsMsdev() ? NULL : hWndParent), m_treeSubClass(*this), m_tree(m_treeSubClass.GetTreeCtrl())
{
	mCurFilter = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_FILTER);
	mShowDimHiddenItems = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_SHOWHIDDENDIMMED, false);
	mShowTooltips = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_SHOWTOOLTIPS, true);

	Create(IDD_VAHASHTAGS, CWnd::FromHandle(hWndParent));
	SetWindowText("VA Hashtags"); // needed for vc6 tab
	mTreeStateStorage = std::make_unique<tree_state_hashtags::storage>();
}

VaHashtagsFrame::~VaHashtagsFrame()
{
	::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_FILTER, mCurFilter);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_SHOWHIDDENDIMMED, mShowDimHiddenItems);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_HASHTAGS_SHOWTOOLTIPS, mShowTooltips);

	if (m_hWnd && ::IsWindow(m_hWnd))
		DestroyWindow();
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VaHashtagsFrame, VADialog)
//{{AFX_MSG_MAP(VaHashtagsFrame)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_WM_TIMER()
ON_WM_DESTROY()
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnRightClickTree)
ON_NOTIFY(TVN_ITEMEXPANDING, IDC_TREE1, OnItemExpanding)
ON_MESSAGE(WM_VA_INSERTITEMS, OnInsert)

ON_BN_CLICKED(IDC_NEXT, GotoNextItem)
ON_BN_CLICKED(IDC_PREVIOUS, GotoPreviousItem)
ON_BN_CLICKED(IDC_NEXT_IN_GROUP, GotoNextItemInGroup)
ON_BN_CLICKED(IDC_PREV_IN_GROUP, GotoPreviousItemInGroup)
ON_BN_CLICKED(IDC_HASHTAGS_GROUPBYFILE, OnToggleGroupByFile)
ON_EN_CHANGE(IDC_HASHTAGS_SEARCH, OnChangeNameEdit)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

void VaHashtagsFrame::OnTimer(UINT_PTR idEvent)
{
	if (idEvent == 1)
	{
		KillTimer(1);

		Repopulate();
	}

	__super::OnTimer(idEvent);
}

void VaHashtagsFrame::IdePaneActivated()
{
	if (::GetFocus() != (HWND)mFilterCtrl)
		mFilterCtrl.SetFocus();

	if (!mActivated)
	{
		mActivated = true;
		RefreshAsync();
	}
}

BOOL VaHashtagsFrame::OnInitDialog()
{
	RecalcLayout(true);

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VaHashtagsFrame::RecalcLayout(bool init)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	HWND hTree;
	hTree = ::GetDlgItem(m_hWnd, IDC_TREE1);
	if (hTree)
	{
		CRect treeRc;
		::GetWindowRect(hTree, &treeRc);
		ScreenToClient(&treeRc);
		CRect clientRc;
		GetClientRect(&clientRc);
		if (treeRc.bottom < clientRc.bottom)
		{
			treeRc.bottom = clientRc.bottom;
			::SetWindowPos(hTree, NULL, 0, 0, treeRc.Width(), treeRc.Height(),
			               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
		}
	}

	if (init)
	{
		__super::OnInitDialog();

		// cannot subclass any more because window contains children
		CRect rect;
		hTree = ::GetDlgItem(m_hWnd, IDC_TREE1);
		if (hTree)
		{
			::GetWindowRect(hTree, &rect);
			::SendMessage(hTree, WM_CLOSE, 0, 0);
			::SendMessage(hTree, WM_DESTROY, 0, 0);
		}
		else
		{
			_ASSERTE(hTree);
		}
		ScreenToClient(rect);

		m_treeSubClass.CreateEx(0, NULL, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP, rect, this, IDC_TREE1);
	}

	const DWORD style = TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_DISABLEDRAGDROP | TVS_SHOWSELALWAYS;
	if (m_tree.IsExtendedThemeActive())
		m_tree.ModifyStyle(0, style | TVS_FULLROWSELECT);
	else if (m_tree.IsVS2010ColouringActive())
		m_tree.ModifyStyle(/*TVS_FULLROWSELECT*/ 0, style);
	else
		m_tree.ModifyStyle(0, style | TVS_FULLROWSELECT);
	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
	m_tree.SetFontType(VaFontType::EnvironmentFont);

	// 	if (gShellAttr->IsMsdev())
	// 		m_treeSubClass.ModifyStyleEx(0, WS_EX_STATICEDGE);
	// 	else if (!m_tree.IsExtendedThemeActive())
	// 		m_treeSubClass.ModifyStyle(0, WS_BORDER);

	{
		CRedirectRegistryToVA rreg;

		// setup columns
		HDITEM hditem;
		hditem.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
		hditem.fmt = HDF_LEFT | HDF_STRING;
		for (int i = 0; i < __column_countof; i++)
		{
#ifdef ALLOW_MULTIPLE_COLUMNS
			hditem.cxy = AfxGetApp()->GetProfileInt(mSettingsCategory, format("column%ld_width", i).c_str(),
			                                        column_definitions[i].default_width);
#else
			hditem.cxy = 200;
#endif
			hditem.pszText = (char*)column_definitions[i].name;
			m_treeSubClass.GetHeaderCtrl().InsertItem(i, &hditem);
			m_treeSubClass.UpdateColumns();
#ifdef ALLOW_MULTIPLE_COLUMNS
			m_treeSubClass.ShowColumn(i, !AfxGetApp()->GetProfileInt(mSettingsCategory,
			                                                         format("column%ld_hidden", i).c_str(),
			                                                         column_definitions[i].default_hidden));
#endif
		}
		m_treeSubClass.UpdateColumns();
	}

	if (init)
	{
		mTooltips.Create(this, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP);
		mySetProp(mTooltips.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	}

	PrepareImages();

	if (init)
	{
		mFilterCtrl.SubclassWindowW(::GetDlgItem(m_hWnd, IDC_HASHTAGS_SEARCH));
		mySetProp(mFilterCtrl.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	}
	// 	else
	// 	{
	// 		HWND hOldFilterWnd = mFilterCtrl.m_hWnd;
	// 		mFilterCtrl.UnsubclassWindowW();
	//
	// 		DWORD exStyle = ::GetWindowLong(hOldFilterWnd, GWL_EXSTYLE);
	// 		DWORD style = ::GetWindowLong(hOldFilterWnd, GWL_STYLE);
	// 		HINSTANCE hInst = (HINSTANCE)::GetWindowLongPtr(hOldFilterWnd, GWLP_HINSTANCE);
	// 		HWND hParent = (HWND)::GetWindowLongPtr(hOldFilterWnd, GWLP_HWNDPARENT);
	// 		WCHAR className[MAX_PATH];
	// 		::GetClassNameW(hOldFilterWnd, className, MAX_PATH);
	// 		CRect rect;
	// 		::GetWindowRect(hOldFilterWnd, &rect);
	// 		::ScreenToClient(hParent, &rect);
	//
	// 		::DestroyWindow(hOldFilterWnd);
	//
	// 		HWND hNewFilterWnd = ::CreateWindowExW(exStyle, className, L"", style, rect.left, rect.top, rect.Width(),
	// rect.Height(), hParent, (HMENU)IDC_HASHTAGS_SEARCH, hInst, nullptr);
	// mFilterCtrl.SubclassWindowW(hNewFilterWnd); 		mySetProp(hNewFilterWnd, "__VA_do_not_colour", (HANDLE)1);
	//
	// 		RemSzControl(hOldFilterWnd);
	// 		AddSzControl(hNewFilterWnd, mdResize, mdNone);
	// 	}

	mFilterCtrl.SetFontType(VaFontType::EnvironmentFont);
	mFilterCtrl.SendMessage(WM_VA_APPLYTHEME, TRUE, 0);
	mFilterCtrl.SetText(mCurFilter);
	mFilterCtrl.SetListBuddy(m_treeSubClass.GetTreeCtrl().m_hWnd);
	mFilterCtrl.WantReturn();

	{
		// *********************
		// * COMMANDBAR LAYOUT *
		// *********************

		CRect treeRect;
		m_treeSubClass.GetWindowRect(treeRect);
		ScreenToClient(treeRect);

		// We want to have each button of size H=22, W=H+1 device units,
		// where +1 in width is a space between buttons which is a part of button
		// on the right side (same it is with WPF buttons in IDE).

		CRect clientRect;
		GetClientRect(&clientRect);

		int btn_height = VsUI::DpiHelper::LogicalToDeviceUnitsY(22);
		int btn_width = VsUI::DpiHelper::LogicalToDeviceUnitsX(23);
		// 		int dpi = VsUI::DpiHelper::GetDeviceDpiX();
		//
		// 		CStringW btnSize;
		// 		btnSize.Format(L"#BTN  W: %d  H: %d DPI: %d", btn_height, btn_width, dpi);
		// 		::OutputDebugStringW(btnSize);

		int x_pos = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
		int y_pos = VsUI::DpiHelper::LogicalToDeviceUnitsY(2);

		for (int i = 0; i < RefsButtonCount; i++)
		{
			mButtons[i].MoveWindow(x_pos, y_pos, btn_width, btn_height);
			x_pos += btn_width;
		}

		// Now we want to have edit control coincided with buttons

		CRect filterRect;
		filterRect.top = y_pos;
		filterRect.bottom = y_pos + btn_height;
		filterRect.left = x_pos + VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		filterRect.right = clientRect.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(2);

		mFilterCtrl.MoveWindow(&filterRect);

		// Also reposition tree control to fit

		treeRect.top = filterRect.bottom + VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
		m_treeSubClass.MoveWindow(&treeRect);
	}

	if (init)
	{
		AddSzControl(IDC_HASHTAGS_SEARCH, mdResize, mdNone);
		AddSzControl(IDC_TREE1, mdResize, mdResize);

		m_tree.EnableToolTips(mShowTooltips);
	}

	CRect rc;
	GetParent()->GetWindowRect(&rc);
	GetParent()->ScreenToClient(&rc);
	MoveWindow(&rc);

	if (init && g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new RefreshHashtags(nullptr));
}

void VaHashtagsFrame::ThemeUpdated()
{
	PrepareImages();
	RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	mFilterCtrl.ThemeChanged();
}

LRESULT
VaHashtagsFrame::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (CheckableBitmapButton::IsThemingSupported())
	{
		if (message == WM_ERASEBKGND)
		{
			// any one is good...
			auto& btn = mButtons[0];

			COLORREF bg_color;
			if (btn.GetThemeColor(CheckableBitmapButton::btn_bg0, bg_color))
			{
				HDC hdc = (HDC)wParam;
				CRect rect;
				GetClientRect(rect);

				///////////////////////////
				// draw dialog background

				CBrush brush;
				brush.CreateSolidBrush(bg_color);
				::FillRect(hdc, rect, (HBRUSH)brush.m_hObject);

				///////////////////////////
				// draw the command bar

				CRect tree_rect;
				m_treeSubClass.GetWindowRect(&tree_rect);
				ScreenToClient(&tree_rect);
				rect.SubtractRect(rect, tree_rect);

				btn.DrawCommandbarBg(CDC::FromHandle(hdc), rect);

				// let buttons know the size of command bar,
				// so that they are able to draw their bg.

				ClientToScreen(rect);
				for (int i = 0; i < RefsButtonCount; i++)
				{
					if (mButtons[i].m_hWnd && ::IsWindow(mButtons[i].m_hWnd))
					{
						CRect rc = rect;
						mButtons[i].ScreenToClient(rc);
						mButtons[i].SetBarRect(rc);
					}
				}

				return 1;
			}
		}
	}

	return __super::WindowProc(message, wParam, lParam);
}

void VaHashtagsFrame::SubstituteButton(int buttonIdx, int idRes, LPCSTR buttonTxt, int idImageRes,
                                       int idSelectedImageRes, int idFocusedImageRes, int idDisabledImageRes,
                                       bool isCheckbox /*= false*/)
{
	_ASSERTE(buttonIdx < RefsButtonCount);

	mButtons[buttonIdx].Substitute(this, &mTooltips, idRes, buttonTxt, idImageRes, idSelectedImageRes,
	                               idFocusedImageRes, idDisabledImageRes, isCheckbox);
}

void VaHashtagsFrame::PrepareImages()
{
	WTString txt;
	txt = "Next Location" + GetBindingTip("Edit.GoToNextLocation");
	SubstituteButton(0, IDC_NEXT, txt.c_str(), IDB_REFSNEXT, IDB_REFSNEXTSEL, 0, IDB_REFSNEXTDIS);
	txt = "Previous Location" + GetBindingTip("Edit.GoToPrevLocation");
	SubstituteButton(1, IDC_PREVIOUS, txt.c_str(), IDB_REFSPREV, IDB_REFSPREVSEL, 0, IDB_REFSPREVDIS);

	SubstituteButton(2, IDC_NEXT_IN_GROUP, "Next Location in Current Group", IDB_REFSNEXTINGROUP,
	                 IDB_REFSNEXTINGROUPSEL, 0, IDB_REFSNEXTINGROUPDIS);
	SubstituteButton(3, IDC_PREV_IN_GROUP, "Previous Location in Current Group", IDB_REFSPREVINGROUP,
	                 IDB_REFSPREVINGROUPSEL, 0, IDB_REFSPREVINGROUPDIS);

	SubstituteButton(4, IDC_HASHTAGS_GROUPBYFILE, "Group by File", IDB_TOGGLE_GROUPBYFILE, IDB_TOGGLE_GROUPBYFILESEL, 0,
	                 IDB_TOGGLE_GROUPBYFILEDIS, true);
	((CButton*)GetDlgItem(IDC_HASHTAGS_GROUPBYFILE))
	    ->SetCheck(Psettings->mHashtagsGroupByFile ? BST_CHECKED : BST_UNCHECKED);

	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
}

void VaHashtagsFrame::OnToggleGroupByFile()
{
	Psettings->mHashtagsGroupByFile = !Psettings->mHashtagsGroupByFile;
	((CButton*)GetDlgItem(IDC_HASHTAGS_GROUPBYFILE))
	    ->SetCheck(Psettings->mHashtagsGroupByFile ? BST_CHECKED : BST_UNCHECKED);

	AutoLockCs l(mVecLock);
	SortHashtags(m_hashtags);
	Repopulate();
}

DWORD
VaHashtagsFrame::QueryStatus(DWORD cmdId) const
{
	switch (cmdId)
	{
	case icmdVaCmd_HashtagsRefresh:
	case icmdVaCmd_HashtagsNext:
	case icmdVaCmd_HashtagsPrev:
	case icmdVaCmd_HashtagsNextInGroup:
	case icmdVaCmd_HashtagsPrevInGroup:
	case icmdVaCmd_HashtagsGroupByFile:
	case icmdVaCmd_HashtagsSearch:
		return 1;
	}

	return (DWORD)-2;
}

HRESULT
VaHashtagsFrame::Exec(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_HashtagsRefresh:
		RefreshAsync();
		break;
	case icmdVaCmd_HashtagsNext:
		GotoNextItem();
		break;
	case icmdVaCmd_HashtagsPrev:
		GotoPreviousItem();
		break;
	case icmdVaCmd_HashtagsNextInGroup:
		GotoNextItemInGroup();
		break;
	case icmdVaCmd_HashtagsPrevInGroup:
		GotoPreviousItemInGroup();
		break;
	case icmdVaCmd_HashtagsGroupByFile:
		OnToggleGroupByFile();
		break;
	case icmdVaCmd_HashtagsSearch: {
		this->SetFocus();
		mFilterCtrl.SetFocus();
		mFilterCtrl.SetSel(0, -1); // select all
	}
	break;

	default:
		return OLECMDERR_E_NOTSUPPORTED;
	}

	return S_OK;
}

const DWORD kHashtagFlag = 0x10000000;
const DWORD kFileFlag = 0x20000000;
const DWORD kProjectFlag = 0x40000000;
const DWORD kUnusedFlag = 0x80000000;
#define IsSpecialNode(n) !!((n)&0xf0000000)

void VaHashtagsFrame::RefreshWorker(EdCntPtr ed, int edModCookie)
{
	_ASSERTE(::GetCurrentThreadId() != g_mainThread);

	if (!IsWindowVisible())
	{
		mActivated = false;
		return;
	}

	if (ed && ed == mLastEd && edModCookie == mLastEdModCookie)
		return;

	mLastEd = ed;
	mLastEdModCookie = edModCookie;

	std::vector<DType> hashtags = GetUnsortedHashtagItems();
	RunExclusionRules(hashtags);
	SortHashtags(hashtags);

	{
		AutoLockCs l(mVecLock);
		if (m_hashtags.size() == hashtags.size())
		{
			bool diff = false;

			auto it1 = m_hashtags.begin();
			auto it2 = hashtags.begin();
			for (; it1 != m_hashtags.end(); ++it1, ++it2)
			{
				// compare our previous set of hashtag items to those found in
				// the va db, but ignore db split and offset since our set is
				// detached from the db and have populated SymData that does
				// not need to be read from db
				if (!it1->IsEquivalentIgnoringDbBacking(*it2))
				{
					diff = true;
					break;
				}
			}

			if (!diff)
				return;
		}
	}

	SendMessage(WM_VA_INSERTITEMS, 0, (LPARAM)&hashtags);
}

LRESULT
VaHashtagsFrame::OnInsert(WPARAM wp, LPARAM lp)
{
	std::vector<DType>* hashtags = (std::vector<DType>*)lp;
	if (hashtags)
	{
		AutoLockCs l(mVecLock);
		m_hashtags.swap(*hashtags);
		Repopulate();
	}
	return S_OK;
}

void VaHashtagsFrame::Clear()
{
	AutoLockCs l(mVecLock);
	m_hashtags.clear();
	Repopulate();
}

void VaHashtagsFrame::Repopulate()
{
	_ASSERTE(::GetCurrentThreadId() == g_mainThread);

	AutoLockCs l(mVecLock);

	if (m_hashtags.size())
		mParent.SendMessage(VAM_Ping, 0, 0);

	StrMatchOptions opts(mCurFilter);

	m_tree.LockWindowUpdate();
	{
		mTreeStateStorage->clear();

		if (m_tree.GetCount() < 18000 /*kMaxNodesToSupportTreeStateMemory*/)
		{
			tree_state_hashtags::save action(*mTreeStateStorage);
			tree_state_hashtags::traverse(m_tree, action, 0); // this will store all at root level (expanded)
			auto selected = m_tree.GetSelectedItem();
			if (selected)
				action.do_after_children__store_selected(m_tree, selected, -1,
				                                         tree_state_hashtags::get_full_name(m_tree, selected));
			auto first_visible = m_tree.GetFirstVisibleItem();
			if (first_visible)
				action.do_after_children__store_first_visible(
				    m_tree, first_visible, -1, tree_state_hashtags::get_full_name(m_tree, first_visible));
		}

		m_tree.DeleteAllItems();
		{
			postponed_items_mutex_lock_t l2(postponed_items_mutex);
			this->postponed_items.clear();
		}
		mNavigationList.clear();

		if (Psettings->mHashtagsGroupByFile)
			DoRepopulateByFile(opts);
		else
			DoRepopulateByTag(opts);

		if (m_tree.GetCount() < 18000 /*kMaxNodesToSupportTreeStateMemory*/)
		{
			tree_state_hashtags::restore action(*mTreeStateStorage);
			tree_state_hashtags::traverse(m_tree, action, 0);
		}
	}
	m_tree.UnlockWindowUpdate();
}

void VaHashtagsFrame::RefreshAsync()
{
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new RefreshHashtags(nullptr));
}

void VaHashtagsFrame::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VaHashtagsFrame)
	//}}AFX_DATA_MAP
}

WTString VaHashtagsFrame::GetTooltipText(HTREEITEM item)
{
	WTString txt;
	try
	{
		if (!item)
			return txt;

		AutoLockCs l(mVecLock);
		const uint refId = (uint)m_tree.GetItemData(item);
		if (IsSpecialNode(refId))
		{
		}
		else
		{
			auto& dt = m_hashtags[refId];
			WTString surroundingText(::GetSurroundingLineText(dt.FilePath(), dt.Line(), 4));
			::HandleTooltipWhitespace(surroundingText);
			txt.WTFormat(
			    "%s%sFile: %s:%d", surroundingText.c_str(),
			    surroundingText.GetLength() && surroundingText[surroundingText.GetLength() - 1] == '\n' ? "\n" : "\n\n",
			    WTString(dt.FilePath()).c_str(), dt.Line());
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAFU:");
	}

	return txt;
}

BOOL VaHashtagsFrame::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNMHDR = (NMHDR*)lParam;

	if (pNMHDR->code == TVN_GETINFOTIPA || pNMHDR->code == TVN_GETINFOTIPW)
	{
		if (pNMHDR->code == TVN_GETINFOTIPA)
		{
			LPNMTVGETINFOTIP lpGetInfoTipw = (LPNMTVGETINFOTIP)lParam;
			const HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));

			for (int n = 0; n < lpGetInfoTipw->cchTextMax && txt[n]; n++)
			{
				lpGetInfoTipw->pszText[n] = txt[n];
				lpGetInfoTipw->pszText[n + 1] = '\0';
			}
			return TRUE;
		}
		if (pNMHDR->code == TVN_GETINFOTIPW)
		{
			LPNMTVGETINFOTIPW lpGetInfoTipw = (LPNMTVGETINFOTIPW)lParam;
			const HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));
			int len = MultiByteToWideChar(CP_UTF8, 0, txt.c_str(), txt.GetLength(), lpGetInfoTipw->pszText,
			                              lpGetInfoTipw->cchTextMax);
			lpGetInfoTipw->pszText[len] = L'\0';
			return TRUE;
		}
	}

	const BOOL retval = __super::OnNotify(wParam, lParam, pResult);
	// 	if (retval &&
	// 		pResult &&
	// 		*pResult == 0 &&
	// 		NM_CLICK == pNMHDR->code)
	// 	{
	// 		// [case: 6021] fix for handling click notification - the tree reflects
	// 		// it back to itself which causes mfc to skip the parent notify
	// 		struct AFX_NOTIFY
	// 		{
	// 			LRESULT* pResult;
	// 			NMHDR* pNMHDR;
	// 		};
	// 		AFX_NOTIFY notify;
	// 		notify.pResult = pResult;
	// 		notify.pNMHDR = pNMHDR;
	// 		HWND hWndCtrl = pNMHDR->hwndFrom;
	// 		// get the child ID from the window itself
	// 		UINT_PTR nID = ((UINT)(WORD)::GetDlgCtrlID(hWndCtrl));
	// 		int nCode = pNMHDR->code;
	// 		return OnCmdMsg((UINT)nID, MAKELONG(nCode, WM_NOTIFY), &notify, NULL);
	// 	}

	return retval;
}

void VaHashtagsFrame::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
	__super::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
	CMenuXP::SetXPLookNFeel(this, pPopupMenu->m_hMenu, CMenuXP::GetXPLookNFeel(this));
}

void VaHashtagsFrame::OnMeasureItem(int, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	CMenuXP::OnMeasureItem(lpMeasureItemStruct);
}

void VaHashtagsFrame::OnDrawItem(int id, LPDRAWITEMSTRUCT dis)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (CMenuXP::OnDrawItem(dis, m_hWnd))
		return;

	if (id != IDC_CLOSE)
	{
		__super::OnDrawItem(id, dis);
		return;
	}

	CRect rect;
	::GetClientRect(dis->hwndItem, rect);

	CPen pen;
	pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), RGB(0, 0, 0));

	CDC* dc = CDC::FromHandle(dis->hDC);
	HGDIOBJ hOldPen = dc->SelectObject(pen);
	dc->SetBkMode(TRANSPARENT);
	dc->Rectangle(rect);

	if (dis->itemState & ODS_SELECTED)
		dc->OffsetWindowOrg(-VsUI::DpiHelper::LogicalToDeviceUnitsX(1), -VsUI::DpiHelper::LogicalToDeviceUnitsY(1));

	dc->MoveTo(VsUI::DpiHelper::LogicalToDeviceUnitsX(2), VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->LineTo(rect.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(2),
	           rect.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->MoveTo(rect.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(3), VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->LineTo(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), rect.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->SelectObject(hOldPen);
}

LRESULT
VaHashtagsFrame::OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
{
	if (CMenuXP::IsOwnerDrawn(pMenu->m_hMenu))
	{
		return CMenuXP::OnMenuChar(pMenu->m_hMenu, nChar, nFlags);
	}
	return __super::OnMenuChar(nChar, nFlags, pMenu);
}

void VaHashtagsFrame::OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	const CPoint pt(GetCurrentMessage()->pt);
	TVHITTESTINFO ht = {{0}};
	ht.pt = pt;
	::MapWindowPoints(HWND_DESKTOP, m_tree.m_hWnd, &ht.pt, 1);

	if (m_tree.HitTest(&ht) && (ht.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT)))
	{
		if (ht.hItem)
			m_tree.SelectItem(ht.hItem);
	}
}

bool VaHashtagsFrame::GoToSelectedItem()
{
	// 	if (mIgnoreItemSelect)
	// 		return;
	//
	// 	if (g_mainThread == ::GetCurrentThreadId())
	// 	{
	// 		new FunctionThread(GoToSelectedItemCB, this, "GoToSelectedItem", true);
	// 		return;
	// 	}

	HTREEITEM item = m_tree.GetSelectedItemIfSingleSelected();
	if (item)
		return GoToItem(item);
	return false;
}

void VaHashtagsFrame::OnDoubleClickTree()
{
	GoToSelectedItem();
}

bool VaHashtagsFrame::GoToItem(HTREEITEM item)
{
	AutoLockCs l(mVecLock);

	uint i = (uint)m_tree.GetItemData(item);

	if (IsSpecialNode(i))
		return false;

	if (Psettings->mCloseHashtagToolWindowOnGoto)
	{
		// [case: 115717]
		LPCSTR cmd = "Window.CloseToolWindow";
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)cmd, 0);
	}

	i &= 0x0fffffff;
	DType& dt = m_hashtags[i];
	::GotoSymbol(dt);
	return true;
}

void VaHashtagsFrame::BuildNavigationList(HTREEITEM item)
{
	if (TVI_ROOT == item)
	{
		mNavigationList.clear();
		mNavigationList.reserve(m_tree.GetCount());
	}
	else
		mNavigationList.push_back(NavItem(item, (int)m_tree.GetItemData(item)));

	HTREEITEM childItem = mtree__GetChildItem(item);
	while (childItem)
	{
		if (m_tree.ItemHasChildren(childItem))
			BuildNavigationList(childItem);
		else
			mNavigationList.push_back(NavItem(childItem, (int)m_tree.GetItemData(childItem)));
		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void VaHashtagsFrame::GotoNextItemInGroup()
{
	if (!gVaService->QueryStatus(IVaService::ct_hashtags, icmdVaCmd_HashtagsNextInGroup))
		return;

	if (m_tree.IsMultipleSelected())
	{
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}
	HTREEITEM curItem = m_tree.GetSelectedItemIfSingleSelected();
	if (!curItem)
		curItem = m_tree.GetChildItem(TVI_ROOT);
	if (!curItem)
		return;

	HTREEITEM gotoItem = NULL;
	if (m_tree.ItemHasChildren(curItem))
	{
		gotoItem = mtree__GetChildItem(curItem);
	}
	else if (!m_tree.GetParentItem(curItem))
	{
		// singleton
		gotoItem = curItem;
	}
	else
	{
		gotoItem = m_tree.GetNextSiblingItem(curItem);
		if (!gotoItem)
			gotoItem = mtree__GetChildItem(m_tree.GetParentItem(curItem));
	}
	SelectItem(gotoItem);
}

void VaHashtagsFrame::GotoPreviousItemInGroup()
{
	if (!gVaService->QueryStatus(IVaService::ct_hashtags, icmdVaCmd_HashtagsPrevInGroup))
		return;

	if (m_tree.IsMultipleSelected())
	{
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}
	HTREEITEM curItem = m_tree.GetSelectedItemIfSingleSelected();
	if (!curItem)
		curItem = m_tree.GetChildItem(TVI_ROOT);
	if (!curItem)
		return;

	HTREEITEM gotoItem = NULL;
	if (m_tree.ItemHasChildren(curItem))
	{
		gotoItem = mtree__GetChildItem(curItem);
	}
	else if (!m_tree.GetParentItem(curItem))
	{
		// singleton
		gotoItem = curItem;
	}
	else
	{
		gotoItem = m_tree.GetPrevSiblingItem(curItem);
		if (!gotoItem)
		{
			while (curItem)
			{
				gotoItem = curItem;
				curItem = m_tree.GetNextSiblingItem(curItem);
			}
		}
	}
	SelectItem(gotoItem);
}

void VaHashtagsFrame::GotoNextItem()
{
	if (!gVaService->QueryStatus(IVaService::ct_hashtags, icmdVaCmd_HashtagsNext))
		return;

	if (mNavigationList.size() != m_tree.GetCount())
		BuildNavigationList(TVI_ROOT);

	if (m_tree.IsMultipleSelected())
	{
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}

	HTREEITEM gotoItem = NULL;
	const HTREEITEM curItem = m_tree.GetSelectedItemIfSingleSelected();
	bool passedCurItem = curItem ? false : true;
	NavList::const_iterator it;
	bool makeParentVisible = curItem ? false : true;

	for (it = mNavigationList.begin(); it != mNavigationList.end(); ++it)
	{
		if (!passedCurItem)
		{
			if ((*it).mItem == curItem)
				passedCurItem = true;
			continue;
		}

		if (IsSpecialNode(it->mRefData))
			continue;

		gotoItem = (*it).mItem;
		break;
	}

	if (!gotoItem)
	{
		// wrap around
		_ASSERTE(it == mNavigationList.end());
		for (it = mNavigationList.begin(); it != mNavigationList.end(); ++it)
		{
			if (IsSpecialNode(it->mRefData))
				continue;

			gotoItem = (*it).mItem;
			makeParentVisible = true;
			break;
		}
	}

	if (makeParentVisible)
	{
		HTREEITEM root = m_tree.GetRootItem();
		// wrapped around - make parent visible (so that child isn't first visible item)
		if (root)
			m_tree.EnsureVisible(root);
	}

	SelectItem(gotoItem);
}

void VaHashtagsFrame::GotoPreviousItem()
{
	if (!gVaService->QueryStatus(IVaService::ct_hashtags, icmdVaCmd_HashtagsPrev))
		return;

	if (mNavigationList.size() != m_tree.GetCount())
		BuildNavigationList(TVI_ROOT);

	if (m_tree.IsMultipleSelected())
	{
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}

	HTREEITEM gotoItem = NULL;
	const HTREEITEM curItem = m_tree.GetSelectedItemIfSingleSelected();
	bool passedCurItem = curItem ? false : true;
	NavList::const_reverse_iterator it;
	bool makeParentVisible = curItem ? false : true;
	HTREEITEM parentItem = NULL;

	for (it = mNavigationList.rbegin(); it != mNavigationList.rend(); ++it)
	{
		if (!passedCurItem)
		{
			if ((*it).mItem == curItem)
				passedCurItem = true;
			continue;
		}

		if (IsSpecialNode(it->mRefData))
			continue;

		gotoItem = (*it).mItem;
		if (makeParentVisible)
			parentItem = m_tree.GetParentItem(gotoItem);
		else
		{
			if (m_tree.GetPrevSiblingItem(gotoItem))
			{
				HTREEITEM item = m_tree.GetPrevSiblingItem(gotoItem);
				if (!m_tree.GetPrevSiblingItem(item))
					parentItem = m_tree.GetParentItem(item);
			}
			else
			{
				HTREEITEM item = m_tree.GetParentItem(gotoItem);
				if (m_tree.GetNextItem(item, TVGN_PREVIOUS))
					parentItem = m_tree.GetNextItem(item, TVGN_PREVIOUS);
			}

			if (parentItem)
				makeParentVisible = true;
		}
		break;
	}

	if (!gotoItem)
	{
		// wrap around
		_ASSERTE(it == mNavigationList.rend());
		for (it = mNavigationList.rbegin(); it != mNavigationList.rend(); ++it)
		{
			if (IsSpecialNode(it->mRefData))
				continue;

			gotoItem = (*it).mItem;
			makeParentVisible = true;
			parentItem = m_tree.GetParentItem(gotoItem);
			break;
		}
	}

	if (makeParentVisible && parentItem)
	{
		// make parent visible for some context - depending upon
		// number of children and size of window, selecting child
		// might make this selection moot...
		m_tree.EnsureVisible(parentItem);
	}

	SelectItem(gotoItem);
}

void VaHashtagsFrame::SelectItem(HTREEITEM item)
{
	if (!item)
		return;

	m_tree.Select(item, TVGN_CARET);
	GoToSelectedItem();
}

void VaHashtagsFrame::OnChangeNameEdit()
{
	KillTimer(1);

	::GetWindowTextW(mFilterCtrl, mCurFilter);
	mCurFilter = StrMatchOptions::TweakFilter(mCurFilter);
	mIsMultiFilter = StrIsMultiMatchPattern(mCurFilter);
	SetTimer(1, 100, NULL);
}

void VaHashtagsFrame::OnDpiChanged(DpiChange change, bool& handled)
{
	RecalcLayout(false);
	handled = true;

	__super::OnDpiChanged(change, handled);
	// 	RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	// 	mFilterCtrl.ThemeChanged();
}

std::vector<DType> VaHashtagsFrame::GetUnsortedHashtagItems()
{
	MultiParsePtr mp = MultiParse::Create();
	CCriticalSection memCs;

	DTypeList hashtags;
	auto fn = [&hashtags, &memCs](DType* dt, bool checkInvalidSys) {
		if (dt->MaskedType() != vaHashtag)
			return;

		// [case: 65910]
		if (checkInvalidSys && ::IsFromInvalidSystemFile(dt))
			return;

		AutoLockCs l(memCs);
		hashtags.push_back(*dt);
	};

	bool stop = false;
	mp->ForEach(fn, stop, false);

	hashtags.FilterNonActiveSystemDefs();
	hashtags.PurgeMissingFiles(); // [case: 114055]

	// ensure strs are loaded from db since we are treating these as detached DTypes
	hashtags.GetStrs();

	// convert list to vector
	std::vector<DType> dtVec;
	dtVec.reserve(hashtags.size());
	dtVec.insert(std::end(dtVec), std::begin(hashtags), std::end(hashtags));
	return dtVec;
}

void VaHashtagsFrame::SortHashtags(std::vector<DType>& hashtags)
{
	auto sortByTagFn = [](DType& d1, DType& d2) -> bool {
		// sort by sym, then file, then line
		WTString sym1 = d1.Sym();
		WTString sym2 = d2.Sym();

		if (sym1.CompareNoCase(sym2) == 0)
		{
			if (d1.FileId() == d2.FileId())
				return d1.Line() < d2.Line();
			return d1.FilePath().CompareNoCase(d2.FilePath()) < 0;
		}
		return sym1.CompareNoCase(sym2) < 0;
	};

	auto sortByFileFn = [](DType& d1, DType& d2) -> bool {
		if (d1.FileId() == d2.FileId())
		{
			if (d1.Line() == d2.Line())
			{
				WTString sym1 = d1.Sym();
				WTString sym2 = d2.Sym();
				sym1.MakeLower();
				sym2.MakeLower();
				return sym1.CompareNoCase(sym2) < 0;
			}
			return d1.Line() < d2.Line();
		}
		return d1.FilePath().CompareNoCase(d2.FilePath()) < 0;
	};

	if (Psettings->mHashtagsGroupByFile)
		std::sort(hashtags.begin(), hashtags.end(), sortByFileFn);
	else
		std::sort(hashtags.begin(), hashtags.end(), sortByTagFn);
}

void VaHashtagsFrame::RunExclusionRules(std::vector<DType>& hashtags)
{
	if (mShowDimHiddenItems)
		return;

	auto remDt = [&](DType& dt) {
		if (gHashtagManager)
			return gHashtagManager->IsHashtagIgnored(dt);
		return true;
	};
	hashtags.erase(std::remove_if(std::begin(hashtags), std::end(hashtags), remDt), std::end(hashtags));
}

void VaHashtagsFrame::OnDestroy()
{
	mFilterCtrl.UnsubclassWindowW();
	__super::OnDestroy();
}

void VaHashtagsFrame::DoRepopulateByTag(const StrMatchOptions& strMatchOpts)
{
	uint i = 0;
	while (i < m_hashtags.size())
	{
		CStringW tag1 = m_hashtags[i].Sym().Wide();

		uint j;
		for (j = i + 1; j < m_hashtags.size(); ++j)
		{
			CStringW tag2 = m_hashtags[j].Sym().Wide();
			if (tag1.CompareNoCase(tag2) != 0)
				break;
		}

		if (!ItemFilterTest(tag1, strMatchOpts))
		{
			const uint count = j - i;
			if (count == 1)
			{
				auto& b = m_hashtags[i];
				CStringW itemText;
				CString__FormatW(itemText, L"%c%s    %c%s:%d", MARKER_BOLD, (LPCWSTR)tag1, MARKER_NONE,
				                 (LPCWSTR)::Basename(b.FilePath()), b.Line());

				WTString bDef(b.Def());
				if (!bDef.IsEmpty())
				{
					CStringW defStr;
					CString__FormatW(defStr, L"    %c%s", MARKER_DIM, (LPCWSTR)bDef.Wide());
					itemText += defStr;
				}

				bool isHidden = mShowDimHiddenItems && gHashtagManager->IsHashtagIgnored(m_hashtags[i]);
				if (isHidden)
					itemText = MARKER_DIM + itemText;
				if (Psettings->mDisplayHashtagXrefs || !m_hashtags[i].IsHashtagRef())
				{
					auto thisItem = m_tree.InsertItemW(UINT(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM),
					                                   itemText, ICONIDX_COMMENT_BULLET, ICONIDX_COMMENT_BULLET, 0, 0,
					                                   (LPARAM)i, TVI_ROOT, TVI_LAST);
					m_treeSubClass.SetItemFlags(thisItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
					                                          TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
				}
			}
			else
			{
				auto parentItem = m_tree.InsertItemW(UINT(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM),
				                                     L"", ICONIDX_COMMENT_BULLET, ICONIDX_COMMENT_BULLET, 0, 0,
				                                     kHashtagFlag, TVI_ROOT, TVI_LAST);
				m_treeSubClass.SetItemFlags(parentItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
				                                            TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);

				bool allHidden = true;
				uint hiddenXrefCnt = 0;
				// 				HTREEITEM lastAddedItem = nullptr;
				CStringW lastAddedItemText;
				DWORD lastAddedItemData = 0;
				postponed_items_t postponed_items2;
				for (uint k = i; k < j; ++k)
				{
					auto& b = m_hashtags[k];
					CStringW itemText;
					CString__FormatW(itemText, L"%s:%d", (LPCWSTR)::Basename(b.FilePath()), b.Line());

					WTString bDef(b.Def());
					if (!bDef.IsEmpty())
					{
						CStringW defStr;
						CString__FormatW(defStr, L"   %c%s", MARKER_DIM, (LPCWSTR)bDef.Wide());
						itemText += defStr;
					}

					bool isHidden = mShowDimHiddenItems && gHashtagManager->IsHashtagIgnored(b);
					if (isHidden)
						itemText = MARKER_DIM + itemText;
					int img = GetFileImgIdx(b.FilePath());

					if (Psettings->mDisplayHashtagXrefs || !m_hashtags[k].IsHashtagRef())
					{
						if ((postponed_items2.size() > 0) && (std::get<0>(postponed_items2.back()) == itemText))
							goto skipped; // duplicated tag on the same line
						lastAddedItemText = itemText;
						lastAddedItemData = k;
						postponed_items2.emplace_back(itemText, img, k);
						// 						lastAddedItem = m_tree.InsertItemW(TVIF_TEXT | TVIF_IMAGE |
						// TVIF_SELECTEDIMAGE | TVIF_PARAM, itemText, img, img, 0, 0, k, parentItem, TVI_LAST);
						// 						m_treeSubClass.SetItemFlags(lastAddedItem, TIF_ONE_CELL_ROW |
						// TIF_PROCESS_MARKERS | TIF_DONT_COLOUR | TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
					}
					else
					{
					skipped:
						isHidden = true;
						++hiddenXrefCnt;
					}

					allHidden &= isHidden;
				}

				const uint newCount = count - hiddenXrefCnt;
				if (!newCount)
				{
					_ASSERTE(hiddenXrefCnt == count);
					// all hidden by xref setting, delete parent node
					m_tree.DeleteItem(parentItem);
				}
				else if (1 == newCount)
				{
					_ASSERTE(hiddenXrefCnt);
					// delete the two nodes (parent + 1 child) and replace with a single
					// node, as when count == 1 in the outer if condition
					// 					CStringW oldTxt(m_tree.GetItemTextW(lastAddedItem));
					// 					DWORD dat = m_tree.GetItemData(lastAddedItem);

					// 					m_tree.DeleteItem(lastAddedItem);
					m_tree.DeleteItem(parentItem);

					// display as if count == 1 in the outer if condition
					CStringW newTxt;
					CString__FormatW(newTxt, L"%c%s    %c", MARKER_BOLD, (LPCWSTR)tag1, MARKER_NONE);
					newTxt += lastAddedItemText /*oldTxt*/;
					auto newItem = m_tree.InsertItemW(UINT(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM),
					                                  newTxt, ICONIDX_COMMENT_BULLET, ICONIDX_COMMENT_BULLET, 0, 0,
					                                  (LPARAM)lastAddedItemData /*dat*/, TVI_ROOT, TVI_LAST);
					m_treeSubClass.SetItemFlags(newItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
					                                         TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
				}
				else
				{
					if (postponed_items2.size() > 0)
						m_tree.InsertItemW(dummy_item_text, parentItem);
					{
						postponed_items_mutex_lock_t l(postponed_items_mutex);
						this->postponed_items[parentItem] = std::move(postponed_items2);
					}

					CStringW parentItemText;
					// hidden xrefs are not reflected in newCount (unlike other hidden instances)
					CString__FormatW(parentItemText, L"%c%s %c(%d)", MARKER_BOLD, (LPCWSTR)tag1, MARKER_NONE, newCount);
					if (allHidden)
						m_tree.SetItemTextW(parentItem, MARKER_DIM + parentItemText);
					else
						m_tree.SetItemTextW(parentItem, parentItemText);

					m_treeSubClass.ValidateCache(parentItem);
				}
			}
		}

		i = j;
	}
}

void VaHashtagsFrame::OnItemExpanding(NMHDR* pNMHDR, LRESULT* pResult)
{
	auto pnmtv = (LPNMTREEVIEW)pNMHDR;
	if (pnmtv->action == TVE_EXPAND)
	{
		HTREEITEM parent = pnmtv->itemNew.hItem;
		mtree__GetChildItem(parent); // this will insert lazy items if needed
	}

	*pResult = 0;
}

HTREEITEM
VaHashtagsFrame::mtree__GetChildItem(HTREEITEM item)
{
	// Does the same as mTree.GetChildItem(), but if the item wasn't previously expanded, this will add the missing
	// items.
	if (!item)
		return nullptr;
	if (item == TVI_ROOT)
		return m_tree.GetChildItem(TVI_ROOT);

	if (!(m_tree.GetItemState(item, TVIS_EXPANDEDONCE) & TVIS_EXPANDEDONCE))
	{
		auto l = std::make_unique<postponed_items_mutex_lock_t>(postponed_items_mutex);
		auto it = postponed_items.find(item);
		if (it != postponed_items.end())
		{
			postponed_items_t items = std::move(it->second);
			postponed_items.erase(it);
			l.reset();

			m_tree.SetItemState(item, TVIS_EXPANDEDONCE, TVIS_EXPANDEDONCE);
			HTREEITEM child = m_tree.GetChildItem(item);
			if (!child)
				return nullptr;
			_ASSERTE(m_tree.GetItemTextW(child) ==
			         dummy_item_text); // shouldn't happen any more, but even if it does, won't crash any more
			if (m_tree.GetItemTextW(child) != dummy_item_text)
				return child;

			m_tree.DeleteItem(child);

			for (const auto& i : items)
			{
				auto item2 =
				    m_tree.InsertItemW(UINT(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM), std::get<0>(i),
				                       std::get<1>(i), std::get<1>(i), 0, 0, (LPARAM)std::get<2>(i), item, TVI_LAST);
				m_treeSubClass.SetItemFlags(item2, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
				                                       TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
			}
		}
	}

	return m_tree.GetChildItem(item);
}

void VaHashtagsFrame::DoRepopulateByFile(const StrMatchOptions& strMatchOpts)
{
	UINT lastFileId = 0;
	HTREEITEM lastFileItem = nullptr;
	CStringW lastAddedItem;

	std::unordered_map<HTREEITEM, postponed_items_t> postponed_items2;
	bool allHidden = true;
	uint i = 0;
	for (auto& b : m_hashtags)
	{
		CStringW thisHashtag = b.Sym().Wide();
		bool isFiltered = ItemFilterTest(thisHashtag, strMatchOpts);
		if (!isFiltered && !Psettings->mDisplayHashtagXrefs && m_hashtags[i].IsHashtagRef())
			isFiltered = true;
		if (!isFiltered)
		{
			if (b.FileId() != lastFileId)
			{
				if (lastFileItem && allHidden)
				{
					CStringW txt = m_tree.GetItemTextW(lastFileItem);
					m_tree.SetItemTextW(lastFileItem, MARKER_DIM + txt);
				}

				allHidden = true;
				int img = GetFileImgIdx(b.FilePath());

				CStringW itemText(b.FilePath());
				int pos = itemText.ReverseFind('\\');
				if (-1 != pos++)
				{
					// 					CStringW base(itemText.Left(pos));
					// 					base += MARKER_BOLD;
					// 					base += itemText.Mid(pos);
					// 					base += MARKER_NONE;

					CStringW base;
					base += MARKER_BOLD;
					base += itemText.Mid(pos);
					base += MARKER_NONE;

					CStringW dir(itemText.Left(pos - 1));
					base += "   ";
					base += dir;

					itemText = base;
				}

				lastFileItem = m_tree.InsertItemW(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, itemText,
				                                  img, img, 0, 0, kFileFlag, TVI_ROOT, TVI_LAST);
				m_treeSubClass.SetItemFlags(lastFileItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
				                                              TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
				lastFileId = b.FileId();

				m_tree.InsertItemW(dummy_item_text, lastFileItem);
				lastAddedItem = L"";
			}

			CStringW itemText;
			CString__FormatW(itemText, L"(%d):    %s", b.Line(), (LPCWSTR)thisHashtag);

			WTString bDef(b.Def());
			if (!bDef.IsEmpty())
			{
				CStringW defStr;
				CString__FormatW(defStr, L"   %c%s", MARKER_DIM, (LPCWSTR)bDef.Wide());
				itemText += defStr;
			}

			bool isHidden = mShowDimHiddenItems && gHashtagManager->IsHashtagIgnored(b);
			if (isHidden)
				itemText = MARKER_DIM + itemText;
			allHidden &= isHidden;

			// 			auto thisItem = m_tree.InsertItemW(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM,
			// itemText, ICONIDX_COMMENT_BULLET, ICONIDX_COMMENT_BULLET, 0, 0, i, lastFileItem, TVI_LAST);
			// 			m_treeSubClass.SetItemFlags(thisItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
			// TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
			if (lastAddedItem != itemText)
			{
				postponed_items2[lastFileItem].emplace_back(itemText, ICONIDX_COMMENT_BULLET, i);
				lastAddedItem = itemText;
			}
		}
		++i;
	}

	{
		postponed_items_mutex_lock_t l(postponed_items_mutex);
		this->postponed_items = std::move(postponed_items2);
	}
}

bool VaHashtagsFrame::ItemFilterTest(CStringW hashtag, const StrMatchOptions& strMatchOpts)
{
	if (!mCurFilter.GetLength())
		return false;

	if (mIsMultiFilter)
	{
		return !::StrMultiMatchRanked(hashtag, mCurFilter, false);
	}
	else
	{
		return !::StrMatchRankedW(hashtag, strMatchOpts, false);
	}
}

void VaHashtagsFrame::OnContextMenu(CWnd* pWnd, CPoint pos)
{
	//	return __super::OnContextMenu(pWnd, pos);

	if (PopupMenuXP::IsMenuActive())
		return;

	AutoLockCs l(mVecLock);

	const auto mTree = &this->m_tree; // gmit: yes, much better!
	mTree->PopTooltip();

	HTREEITEM selItem = mTree->GetSelectedItem /*IfSingleSelected*/ ();

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (mTree->GetItemRect(selItem, &rc, TRUE))
		{
			mTree->ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

	PopupMenuLmb contextMenu;
	MenuItemLmb ignoreMenuGlobal(L"&Global", true);

	if (m_tree.IsMultipleSelected())
	{
		std::set<WTString> tags;
		for (auto item : mTree->GetSelectedItems())
		{
			try
			{
				std::set<int> item_datas;
				int item_data = (int)mTree->GetItemData(item);
				if (IsSpecialNode(item_data))
				{
					if (item_data == kHashtagFlag)
					{
						item = mtree__GetChildItem(item);
						item_data = item ? (int)mTree->GetItemData(item) : -1;
						item_datas.insert(item_data);
					}
					else if (item_data == kFileFlag)
					{
						for (item = mtree__GetChildItem(item); item; item = mTree->GetNextSiblingItem(item))
						{
							int item_data2 = item ? (int)mTree->GetItemData(item) : -1;
							if (!IsSpecialNode(item_data2))
								item_datas.insert(item_data2);
						}
					}
				}
				else
					item_datas.insert(item_data);

				for (auto item_data2 : item_datas)
				{
					if (!IsSpecialNode(item_data2))
						tags.insert(m_hashtags.at((uint)item_data2).Sym());
				}
			}
			catch (std::out_of_range)
			{
			}
		}
		if (tags.size() == 0)
			return;

		std::set<WTString> all_tags;
		std::function<void(HTREEITEM)> collect_all_tags = [&, this](HTREEITEM item) {
			while (item)
			{
				try
				{
					uint item_data = uint(item ? mTree->GetItemData(item) : -1);
					if (!IsSpecialNode(item_data))
						all_tags.insert(m_hashtags.at(item_data).Sym());
				}
				catch (std::out_of_range)
				{
				}
				collect_all_tags(mtree__GetChildItem(item));
				item = mTree->GetNextSiblingItem(item);
			}
		};
		collect_all_tags(mTree->GetNextItem(nullptr, TVGN_ROOT));
		std::set<WTString> unselected_tags;
		std::set_difference(all_tags.cbegin(), all_tags.cend(), tags.cbegin(), tags.cend(),
		                    std::inserter(unselected_tags, unselected_tags.end()));

		auto cmdIgnoreTag = [this](const std::set<WTString>& tags, bool global) {
			bool ignored = false;
			for (auto t : tags)
				ignored |= gHashtagManager->IgnoreTag(t, global);
			if (ignored)
			{
				AutoLockCs l(mVecLock);
				RunExclusionRules(m_hashtags);
				Repopulate();
			}
		};

		auto cmdUnhideTag = [this](const std::set<WTString>& tags, bool global) {
			bool cleared = false;
			for (auto t : tags)
				cleared |= gHashtagManager->ClearIgnoreTag(t, global);
			if (cleared)
			{
				AutoLockCs l(mVecLock);
				RunExclusionRules(m_hashtags);
				Repopulate();
			}
		};

		int ignoreStatus = -1; // invalid value
		for (const WTString& element : tags)
		{
			int tempStatus = gHashtagManager->GetIgnoredRuleLocation(element.Wide(), L"t:");
			if (ignoreStatus != -1 && ignoreStatus != tempStatus)
			{
				ignoreStatus = -1;
				break;
			}

			ignoreStatus = tempStatus;
		}

		CStringW itemTxt;

		if (ignoreStatus == HashtagsManager::IgnoreStatuses::NotIgnored)
		{
			CString__FormatW(itemTxt, L"Hide %zu &Tag%s", tags.size(), (tags.size() > 1) ? L"s" : L"");
			contextMenu.Items.push_back(MenuItemLmb(
			    itemTxt, true, [=]() { cmdIgnoreTag(tags, false); }, nullptr));
			if (Psettings->mHashTagsMenuDisplayGlobalAlways)
				contextMenu.Items.push_back(MenuItemLmb(
				    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdIgnoreTag(tags, true); }, nullptr));
			ignoreMenuGlobal.Items.push_back(MenuItemLmb(
			    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdIgnoreTag(tags, true); }, nullptr));
		}
		else if (ignoreStatus == HashtagsManager::IgnoreStatuses::IgnoredLocal)
		{
			CString__FormatW(itemTxt, L"Unhide %zu &Tag%s", tags.size(), (tags.size() > 1) ? L"s" : L"");
			contextMenu.Items.push_back(MenuItemLmb(
			    itemTxt, true, [=]() { cmdUnhideTag(tags, false); }, nullptr));
		}
		else if (ignoreStatus == HashtagsManager::IgnoreStatuses::IgnoredGlobal)
		{
			CString__FormatW(itemTxt, L"Unhide %zu &Tag%s", tags.size(), (tags.size() > 1) ? L"s" : L"");
			if (Psettings->mHashTagsMenuDisplayGlobalAlways)
				contextMenu.Items.push_back(MenuItemLmb(
				    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdUnhideTag(tags, true); }, nullptr));
			ignoreMenuGlobal.Items.push_back(MenuItemLmb(
			    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdUnhideTag(tags, true); }, nullptr));
		}

		if (unselected_tags.size() > 0)
		{
			itemTxt = "Hide all &unselected Tags";
			contextMenu.Items.push_back(MenuItemLmb(
			    itemTxt, true, [=]() { cmdIgnoreTag(unselected_tags, false); }, nullptr));
			if (Psettings->mHashTagsMenuDisplayGlobalAlways)
				contextMenu.Items.push_back(MenuItemLmb(
				    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdIgnoreTag(unselected_tags, true); },
				    nullptr));
			ignoreMenuGlobal.Items.push_back(MenuItemLmb(
			    itemTxt + FTM_NORMAL + L" (global)", true, [=]() { cmdIgnoreTag(unselected_tags, true); }, nullptr));
		}
	}
	else
	{
		int item_data = selItem ? (int)mTree->GetItemData(selItem) : -1;

		auto& ignoreMenuSolutionItems = contextMenu.Items;

		bool allowTag = true;
		bool allowFile = true;
		bool allowDir = true;
		bool allowProject = true;

		if (item_data == kFileFlag)
		{
			// group-by-file node
			// offer ignore file, dir, project
			selItem = mtree__GetChildItem(selItem);
			item_data = selItem ? (int)mTree->GetItemData(selItem) : -1;
			assert(item_data != -1);
			if (item_data == -1)
				return;

			// Allow tag if all nodes have the same tag
			const WTString tagToMatch = m_hashtags[(uint)item_data].Sym();
			HTREEITEM hItem = mTree->GetNextSiblingItem(selItem);
			while (hItem != nullptr)
			{
				uint idx = (uint)mTree->GetItemData(hItem);
				if (m_hashtags[idx].Sym() != tagToMatch)
				{
					allowTag = false;
					break;
				}
				hItem = mTree->GetNextSiblingItem(hItem);
			}
		}
		else if (item_data == kHashtagFlag)
		{
			// group-by-tag node
			// offer ignore tag
			selItem = mtree__GetChildItem(selItem);
			item_data = selItem ? (int)mTree->GetItemData(selItem) : -1;
			assert(item_data != -1);
			if (item_data == -1)
				return;

			// Allow if all nodes have the same file/dir/proj
			auto& dt1 = m_hashtags[(uint)item_data];
			const uint fileIdToMatch = dt1.FileId();
			const CStringW dirToMatch = ::Path(dt1.FilePath());
			const auto projectsToMatch = GlobalProject->GetProjectForFile(fileIdToMatch);

			HTREEITEM hItem = mTree->GetNextSiblingItem(selItem);
			while (hItem != nullptr)
			{
				uint idx = (uint)mTree->GetItemData(hItem);
				auto& dt2 = m_hashtags[idx];

				if (allowFile && dt2.FileId() != fileIdToMatch)
					allowFile = false;
				if (allowDir && ::Path(dt2.FilePath()) != dirToMatch)
					allowDir = false;
				if (allowProject && GlobalProject->GetProjectForFile(dt2.FileId()) != projectsToMatch)
					allowProject = false;

				hItem = mTree->GetNextSiblingItem(hItem);
			}
		}

		if (item_data >= 0 && item_data < (int)m_hashtags.size())
		{
			auto& dt = m_hashtags[(uint)item_data];

			CStringW filePath = dt.FilePath();
			CStringW fileName = ::Basename(filePath);
			CStringW dirPath = ::Path(filePath);
			CStringW dirName = ::Basename(dirPath);

			int ignoredTagRuleLocation = 0;
			int ignoredFileRuleLocation = 0;
			int ignoredDirectoryRuleLocation = 0;
			int ignoredProjectRuleLocation = 0;

			bool isTagIgnored = gHashtagManager->IsTagIgnored(dt.Sym(), &ignoredTagRuleLocation);
			bool isFileIgnored = gHashtagManager->IsFileIgnored(dt.FileId(), &ignoredFileRuleLocation);
			bool isDirectoryIgnored = gHashtagManager->IsDirectoryIgnored(dt.FileId(), &ignoredDirectoryRuleLocation);
			bool isProjectIgnored = gHashtagManager->IsProjectIgnored(dt.FileId(), &ignoredProjectRuleLocation);

			if (allowTag && !(isFileIgnored || isDirectoryIgnored || isProjectIgnored))
			{
				auto cmdIgnoreTag = [&](bool global) {
					if (gHashtagManager->IgnoreTag(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				auto cmdUnhideTag = [&](bool global) {
					if (gHashtagManager->ClearIgnoreTag(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				CStringW itemTxtLocal;
				CStringW itemTxtGlobal;
				MenuItemLmb::Cmd cmdLocal;
				MenuItemLmb::Cmd cmdGlobal;
				bool isTagLocalHidden = false;
				bool isTagGlobalHidden = false;

				if (ignoredTagRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredLocal)
				{
					itemTxtLocal = L"Unhide &Tag " + CStringW(FTM_BOLD) + dt.Sym().Wide();
					cmdLocal = [=]() { cmdUnhideTag(false); };
					isTagLocalHidden = true;
				}
				else if (!(isFileIgnored || isDirectoryIgnored || isProjectIgnored))
				{
					itemTxtLocal = L"Hide &Tag " + CStringW(FTM_BOLD) + dt.Sym().Wide();
					cmdLocal = [=]() { cmdIgnoreTag(false); };
				}

				if (ignoredTagRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredGlobal)
				{
					itemTxtGlobal = L"Unhide &Tag " + CStringW(FTM_BOLD) + dt.Sym().Wide();
					cmdGlobal = [=]() { cmdUnhideTag(true); };
					isTagGlobalHidden = true;
				}
				else if (!(isFileIgnored || isDirectoryIgnored || isProjectIgnored))
				{
					itemTxtGlobal = L"Hide &Tag " + CStringW(FTM_BOLD) + dt.Sym().Wide();
					cmdGlobal = [=]() { cmdIgnoreTag(true); };
				}

				if (isTagLocalHidden || !isTagGlobalHidden && cmdLocal != nullptr)
				{
					ignoreMenuSolutionItems.push_back(MenuItemLmb(itemTxtLocal, true, cmdLocal, nullptr));
				}

				if (isTagGlobalHidden || !isTagLocalHidden && cmdGlobal != nullptr)
				{
					ignoreMenuGlobal.Items.push_back(MenuItemLmb(itemTxtGlobal, true, cmdGlobal, nullptr));
					if (Psettings->mHashTagsMenuDisplayGlobalAlways)
						ignoreMenuSolutionItems.push_back(
						    MenuItemLmb(itemTxtGlobal + CStringW(FTM_NORMAL) + L" (global)", true, cmdGlobal, nullptr));
				}
			}

			if (allowFile && !(isDirectoryIgnored || isProjectIgnored))
			{
				auto cmdIgnoreFile = [&](bool global) {
					if (gHashtagManager->IgnoreFile(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				auto cmdUnhideFile = [&](bool global) {
					if (gHashtagManager->ClearIgnoreFile(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				CStringW itemTxtLocal;
				CStringW itemTxtGlobal;
				MenuItemLmb::Cmd cmdLocal;
				MenuItemLmb::Cmd cmdGlobal;
				bool isFileLocalHidden = false;
				bool isFileGlobalHidden = false;

				if (ignoredFileRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredLocal)
				{
					itemTxtLocal = L"Unhide &File " + CStringW(FTM_BOLD) + fileName;
					cmdLocal = [=]() { cmdUnhideFile(false); };
					isFileLocalHidden = true;
				}
				else if (!(isTagIgnored || isDirectoryIgnored || isProjectIgnored))
				{
					itemTxtLocal = L"Hide &File " + CStringW(FTM_BOLD) + fileName;
					cmdLocal = [=]() { cmdIgnoreFile(false); };
				}

				if (ignoredFileRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredGlobal)
				{
					itemTxtGlobal = L"Unhide &File " + CStringW(FTM_BOLD) + fileName;
					cmdGlobal = [=]() { cmdUnhideFile(true); };
					isFileGlobalHidden = true;
				}
				else if (!(isTagIgnored || isDirectoryIgnored || isProjectIgnored))
				{
					itemTxtGlobal = L"Hide &File " + CStringW(FTM_BOLD) + fileName;
					cmdGlobal = [=]() { cmdIgnoreFile(true); };
				}

				uint fileIcon = (uint)GetFileImgIdx(filePath);

				if (isFileLocalHidden || !isFileGlobalHidden && cmdLocal != nullptr)
				{
					ignoreMenuSolutionItems.push_back(
					    MenuItemLmb(itemTxtLocal, (UINT)MF_STRING, fileIcon, true, cmdLocal, nullptr));
				}

				if (isFileGlobalHidden || !isFileLocalHidden && cmdGlobal != nullptr)
				{
					ignoreMenuGlobal.Items.push_back(
					    MenuItemLmb(itemTxtGlobal, (UINT)MF_STRING, fileIcon, true, cmdGlobal, nullptr));
					if (Psettings->mHashTagsMenuDisplayGlobalAlways)
						ignoreMenuSolutionItems.push_back(
						    MenuItemLmb(itemTxtGlobal + CStringW(FTM_NORMAL) + L" (global)", (UINT)MF_STRING, fileIcon,
						                true, cmdGlobal, nullptr));
				}
			}

			if (allowDir && !isProjectIgnored)
			{
				auto cmdIgnoreDir = [&](bool global) {
					if (gHashtagManager->IgnoreDirectory(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				auto cmdUnhideDir = [&](bool global) {
					if (gHashtagManager->ClearIgnoreDirectory(dt, global))
					{
						AutoLockCs l(mVecLock);
						RunExclusionRules(m_hashtags);
						Repopulate();
					}
				};

				CStringW itemTxtLocal;
				CStringW itemTxtGlobal;
				MenuItemLmb::Cmd cmdLocal;
				MenuItemLmb::Cmd cmdGlobal;
				bool isDirLocalHidden = false;
				bool isDirGlobalHidden = false;

				if (ignoredDirectoryRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredLocal)
				{
					itemTxtLocal = L"Unhide &Directory " + CStringW(FTM_BOLD) + dirName;
					cmdLocal = [=]() { cmdUnhideDir(false); };
					isDirLocalHidden = true;
				}
				else if (!(isTagIgnored || isFileIgnored || isProjectIgnored))
				{
					itemTxtLocal = L"Hide &Directory " + CStringW(FTM_BOLD) + dirName;
					cmdLocal = [=]() { cmdIgnoreDir(false); };
				}

				if (ignoredDirectoryRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredGlobal)
				{
					itemTxtGlobal = L"Unhide &Directory " + CStringW(FTM_BOLD) + dirName;
					cmdGlobal = [=]() { cmdUnhideDir(true); };
					isDirGlobalHidden = true;
				}
				else if (!(isTagIgnored || isFileIgnored || isProjectIgnored))
				{
					itemTxtGlobal = L"Hide &Directory " + CStringW(FTM_BOLD) + dirName;
					cmdGlobal = [=]() { cmdIgnoreDir(true); };
				}

				if (isDirLocalHidden || !isDirGlobalHidden && cmdLocal != nullptr)
				{
					ignoreMenuSolutionItems.push_back(
					    MenuItemLmb(itemTxtLocal, MF_STRING, ICONIDX_FILE_FOLDER, true, cmdLocal, nullptr));
				}

				if (isDirGlobalHidden || !isDirLocalHidden && cmdGlobal != nullptr)
				{
					ignoreMenuGlobal.Items.push_back(
					    MenuItemLmb(itemTxtGlobal, MF_STRING, ICONIDX_FILE_FOLDER, true, cmdGlobal, nullptr));
					if (Psettings->mHashTagsMenuDisplayGlobalAlways)
						ignoreMenuSolutionItems.push_back(
						    MenuItemLmb(itemTxtGlobal + CStringW(FTM_NORMAL) + L" (global)", MF_STRING,
						                ICONIDX_FILE_FOLDER, true, cmdGlobal, nullptr));
				}
			}

			if (allowProject)
			{
				auto projects = GlobalProject->GetProjectForFile(dt.FileId());
				if (projects.size())
				{
					for (auto project : projects)
					{
						auto cmdIgnoreProject = [&, project](bool global) {
							if (gHashtagManager->IgnoreProject(project->GetProjectFile(), global))
							{
								AutoLockCs l(mVecLock);
								RunExclusionRules(m_hashtags);
								Repopulate();
							}
						};

						auto cmdUnhideProject = [&, project](bool global) {
							if (gHashtagManager->ClearIgnoreProject(project->GetProjectFile(), global))
							{
								AutoLockCs l(mVecLock);
								RunExclusionRules(m_hashtags);
								Repopulate();
							}
						};

						CStringW projectName = ::Basename(project->GetProjectFile());

						CStringW itemTxtLocal;
						CStringW itemTxtGlobal;
						MenuItemLmb::Cmd cmdLocal;
						MenuItemLmb::Cmd cmdGlobal;
						bool isProjectLocalHidden = false;
						bool isProjectGlobalHidden = false;

						if (ignoredProjectRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredLocal)
						{
							itemTxtLocal = L"Unhide &Project " + CStringW(FTM_BOLD) + projectName;
							cmdLocal = [=]() { cmdUnhideProject(false); };
							isProjectLocalHidden = true;
						}
						else if (!(isTagIgnored || isFileIgnored || isDirectoryIgnored))
						{
							itemTxtLocal = L"Hide &Project " + CStringW(FTM_BOLD) + projectName;
							cmdLocal = [=]() { cmdIgnoreProject(false); };
						}

						if (ignoredProjectRuleLocation & HashtagsManager::IgnoreStatuses::IgnoredGlobal)
						{
							itemTxtGlobal = L"Unhide &Project " + CStringW(FTM_BOLD) + projectName;
							cmdGlobal = [=]() { cmdUnhideProject(true); };
							isProjectGlobalHidden = true;
						}
						else if (!(isTagIgnored || isFileIgnored || isDirectoryIgnored))
						{
							itemTxtGlobal = L"Hide &Project " + CStringW(FTM_BOLD) + projectName;
							cmdGlobal = [=]() { cmdIgnoreProject(true); };
						}

						if (isProjectLocalHidden || !isProjectGlobalHidden && cmdLocal != nullptr)
						{
							ignoreMenuSolutionItems.push_back(
							    MenuItemLmb(itemTxtLocal, MF_STRING, ICONIDX_SCOPEPROJECT, true, cmdLocal, nullptr));
						}

						if (isProjectGlobalHidden || !isProjectLocalHidden && cmdGlobal != nullptr)
						{
							ignoreMenuGlobal.Items.push_back(
							    MenuItemLmb(itemTxtGlobal, MF_STRING, ICONIDX_SCOPEPROJECT, true, cmdGlobal, nullptr));
							if (Psettings->mHashTagsMenuDisplayGlobalAlways)
								ignoreMenuSolutionItems.push_back(
								    MenuItemLmb(itemTxtGlobal + CStringW(FTM_NORMAL) + L" (global)", MF_STRING,
								                ICONIDX_SCOPEPROJECT, true, cmdGlobal, nullptr));
						}
					}
				}
			}
		}

		auto cmdClearRuleFile = [&](bool global) {
			if (gHashtagManager->ClearIgnores(global))
			{
				m_hashtags.clear();
				RefreshAsync();
			}
		};
		ignoreMenuSolutionItems.push_back(MenuItemLmb(
		    L"&Unhide All", gHashtagManager->CanClearIgnores(false), [=]() { cmdClearRuleFile(false); }, nullptr));
		ignoreMenuGlobal.Items.push_back(MenuItemLmb(
		    L"&Unhide All", gHashtagManager->CanClearIgnores(true), [=]() { cmdClearRuleFile(true); }, nullptr));
		if (Psettings->mHashTagsMenuDisplayGlobalAlways)
			ignoreMenuSolutionItems.push_back(MenuItemLmb(
			    CStringW(L"&Unhide All") + L" (global)", gHashtagManager->CanClearIgnores(true),
			    [=]() { cmdClearRuleFile(true); }, nullptr));
	}

	auto cmdRefresh = [&]() { RefreshAsync(); };
	contextMenu.Items.push_back(MenuItemLmb(L"&Refresh", 0, ICONIDX_REPARSE, true, cmdRefresh, nullptr));
	contextMenu.Items.push_back(MenuItemLmb()); // separator

#ifdef _DEBUG
	// [case: 115717]
	// this is debug because it needs to be tested with all of the different ways a tool window can be hosted by VS.
	// also need to revisit menu text.
	auto cmdToggleCloseBehavior = [&]() {
		Psettings->mCloseHashtagToolWindowOnGoto = !Psettings->mCloseHashtagToolWindowOnGoto;
	};
	contextMenu.Items.push_back(MenuItemLmb(L"Close Tool Window on &Goto",
	                                        Psettings->mCloseHashtagToolWindowOnGoto ? MF_CHECKED : 0u, 0, true,
	                                        cmdToggleCloseBehavior, nullptr));
#endif

	auto cmdToggleFilters = [&]() {
		mShowDimHiddenItems = !mShowDimHiddenItems;
		if (gHashtagManager->HasIgnores())
			RefreshAsync();
	};
	contextMenu.Items.push_back(MenuItemLmb(L"Show &Hidden Items Dimmed", mShowDimHiddenItems ? MF_CHECKED : 0u, 0,
	                                        true, cmdToggleFilters, nullptr));

	auto cmdToggleXrefs = [&]() {
		Psettings->mDisplayHashtagXrefs = !Psettings->mDisplayHashtagXrefs;
		Repopulate();
	};
	contextMenu.Items.push_back(MenuItemLmb(L"Show &Cross-references",
	                                        Psettings->mDisplayHashtagXrefs ? MF_CHECKED : 0u, 0, true, cmdToggleXrefs,
	                                        nullptr));

	auto cmdToggleTooltips = [&]() {
		mShowTooltips = !mShowTooltips;
		m_tree.EnableToolTips(mShowTooltips);
	};
	contextMenu.Items.push_back(
	    MenuItemLmb(L"&Show Tooltips", mShowTooltips ? MF_CHECKED : 0u, 0, true, cmdToggleTooltips, nullptr));

	if (::GetKeyState(VK_SHIFT) & 0x8000 && !Psettings->mHashTagsMenuDisplayGlobalAlways)
	{
		contextMenu.Items.push_back(MenuItemLmb()); // separator
		contextMenu.Items.push_back(ignoreMenuGlobal);
	}

	contextMenu.Show(&m_tree, pos.x, pos.y); // to draw formatted menu DPI aware, it needs DPI aware parent
}

// RefreshHashtags
// ----------------------------------------------------------------------------
//
RefreshHashtags::RefreshHashtags(EdCntPtr ed) : ParseWorkItem("RefreshHashtags")
{
	try
	{
		mEdModCookie = ed ? ed->m_modCookie : 0;
		mEd = ed;
	}
	catch (...)
	{
	}
}

void RefreshHashtags::DoParseWork()
{
	if (!gVaService || !gVaService->GetHashtagsFrame() || gShellIsUnloading)
		return;

	try
	{
		gVaService->GetHashtagsFrame()->RefreshWorker(mEd, mEdModCookie);
	}
	catch (...)
	{
		VALOGEXCEPTION("RefreshHashtags:");
		_ASSERTE(!"RefreshHashtags exception caught - let sean know if this happens");
	}
}

void EditSearchCtrl::ThemeChanged()
{
	if (CVS2010Colours::IsVS2010CommandBarColouringActive())
	{
		// update theme in this control
		SendMessage(WM_VA_APPLYTHEME, 0);
		SendMessage(WM_VA_APPLYTHEME, 1);
	}

	OnChange();
}

BOOL EditSearchCtrl::OnChange()
{
	CStringW txt;
	GetWindowTextW(*this, txt);

	if (txt.GetLength() > 0)
		SetClearDrawGlyphFnc();
	else
		SetSearchDrawGlyphFnc();

	SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE);

	return FALSE;
}
