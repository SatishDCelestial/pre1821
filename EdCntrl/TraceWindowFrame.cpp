#include "StdAfxEd.h"
#include "TraceWindowFrame.h"
#include "TreeCtrlTT.h"
#include "resource.h"
#include "VaService.h"
#include "Settings.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "MenuXP\Tools.h"
#include "DevShellService.h"
#include "WindowUtils.h"
#include "expansion.h"
#include "Lock.h"
#include "PROJECT.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// command ids
#define ID_TOGGLE_TRACE 0xE100

const UINT WM_VA_TRACEFRAMEEXECASYNC = ::RegisterWindowMessageA("WM_VA_TRACEFRAMEEXECASYNC");

TraceWindowFrame::TraceWindowFrame(HWND hWndParent)
    : VADialog(IDD_FILEOUTLINE, CWnd::FromHandle(hWndParent), fdAll, flAntiFlicker | flNoSavePos), mTree(NULL),
      mParent(gShellAttr->IsMsdevOrCppBuilder() ? NULL : hWndParent), mTraceEnabled(true)
{
	// temporarily use the outline template
	Create(IDD_FILEOUTLINE, CWnd::FromHandle(hWndParent));
	SetWindowText("VA Trace"); // needed for vc6 tab
	mParentItems.push(TVI_ROOT);
}

TraceWindowFrame::~TraceWindowFrame()
{
	OnClear();
	if (m_hWnd && ::IsWindow(m_hWnd))
		DestroyWindow();

	delete mTree;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(TraceWindowFrame, VADialog)
//{{AFX_MSG_MAP(TraceWindowFrame)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnRightClickTree)
ON_COMMAND(ID_EDIT_COPY, OnCopy)
ON_COMMAND(ID_EDIT_CUT, OnCut)
ON_COMMAND(ID_EDIT_CLEAR, OnDelete)
ON_COMMAND(ID_EDIT_CLEAR_ALL, OnClear)
ON_COMMAND(ID_TOGGLE_TRACE, OnToggleTrace)
ON_REGISTERED_MESSAGE(WM_VA_TRACEFRAMEEXECASYNC, OnExecAsync)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

IMPLEMENT_MENUXP(TraceWindowFrame, VADialog);
#pragma warning(pop)

void TraceWindowFrame::IdePaneActivated()
{
	if (mTree && GetFocus() != mTree)
		mTree->SetFocus();
}

BOOL TraceWindowFrame::OnInitDialog()
{
	__super::OnInitDialog();

	CTreeCtrl* pTree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	mTree = new CTreeCtrlTT;
	mTree->SubclassWindow(pTree->m_hWnd);
	//	mTree->SetImageList(g_typeimgLst, TVSIL_NORMAL);
	mTree->ModifyStyle(TVS_INFOTIP, TVS_NONEVENHEIGHT | TVS_HASBUTTONS | TVS_DISABLEDRAGDROP | TVS_HASLINES |
	                                    TVS_SHOWSELALWAYS /*| TVS_INFOTIP*/ | WS_TABSTOP /*|TVS_LINESATROOT*/);

	// [case: 22788] can't find the right enums to use to match solution explorer
	// 	if (gDte2)
	// 	{
	// 		OLE_COLOR bk = 0;
	// 		OLE_COLOR text = 0;
	// 		if (SUCCEEDED(gDte2->GetThemeColor(EnvDTE80::vsThemeColorToolWindowBackground, &bk)))
	// 			mTree->SetBkColor(bk);
	// 		if (SUCCEEDED(gDte2->GetThemeColor(EnvDTE80::vsThemeColorPanelText, &text)))
	// 			mTree->SetTextColor(text);
	// 	}
	//	mTree->EnableToolTips();

	ModifyStyle(DS_3DLOOK, 0);
	mTree->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
	if (gShellAttr->IsMsdev())
	{
		// WS_EX_CLIENTEDGE leaves a bold edge on the left that is
		// more noticeable than the soft edge on the bottom and right
		// with WS_EX_STATICEDGE.
		mTree->ModifyStyleEx(0, WS_EX_STATICEDGE);
	}
	else if (::GetWinVersion() < wvWinXP)
	{
		// vs200x in win2000
		mTree->ModifyStyle(0, WS_BORDER);
	}

	CRect rc;
	GetParent()->GetWindowRect(&rc);
	GetParent()->ScreenToClient(&rc);
	MoveWindow(&rc);

	AddSzControl(IDC_TREE1, mdResize, mdResize);
	::mySetProp(GetDlgItem(IDC_TREE1)->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);

	return TRUE;
}

void TraceWindowFrame::OnClear()
{
	{
		AutoLockCs l(mParentsCs);
		while (!mParentItems.empty())
			mParentItems.pop();
	}
	mTree->PopTooltip();
	mTree->SelectItem(NULL); // unselects all items
	mTree->DeleteAllItems();
}

CStringW TraceWindowFrame::CopyHierarchy(HTREEITEM item, CStringW prefix)
{
	CStringW txt(prefix + CStringW(mTree->GetItemText(item)) + L"\r\n");
	if (mTree->ItemHasChildren(item))
	{
		item = mTree->GetNextItem(item, TVGN_CHILD);
		while (item)
		{
			txt += CopyHierarchy(item, prefix + CStringW(L"\t"));
			item = mTree->GetNextSiblingItem(item);
		}
	}

	return txt;
}

void TraceWindowFrame::OnCopy()
{
	HTREEITEM selItem = mTree->GetSelectedItem();
	if (!selItem)
		return;

	AutoLockCs l(mParentsCs);
	const CStringW txt(CopyHierarchy(selItem, CStringW()));
	::SaveToClipboard(m_hWnd, txt);
}

void TraceWindowFrame::OnCut()
{
	AutoLockCs l(mParentsCs);
	OnCopy();
	OnDelete();
}

void TraceWindowFrame::OnDelete()
{
	AutoLockCs l(mParentsCs);
	HTREEITEM selItem = mTree->GetSelectedItem();
	if (selItem)
		mTree->DeleteItem(selItem);
}

void TraceWindowFrame::OnToggleTrace()
{
#if 1
	mTraceEnabled = !mTraceEnabled;
#else
	// for testing
	Trace("TraceWindowFrame ");
	Trace("TraceWindowFrame 2");
	OpenParent("TraceWindowFrame 3");
	Trace("TraceWindowFrame 3a");
	Trace("TraceWindowFrame 3b");
	Trace("TraceWindowFrame 3c");
	OpenParent("3c.new parent");
	Trace("3c. child 1");
	Trace("3c. child 2");
	Trace("3c. child 3");
	CloseParent();
	CloseParent();
	Trace("TraceWindowFrame 4");
	CloseParent();
	Trace("TraceWindowFrame 5");

	TraceHelp("Test TraceHelp");
	{
		NestedTrace x("nested help");
		NestedTrace x1("nested help2");
		TraceHelp("deep trace");
	}
	TraceHelp("outside trace");
#endif
}

void TraceWindowFrame::OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	const CPoint pt(GetCurrentMessage()->pt);
	HTREEITEM it = GetItemFromPos(pt);
	if (it)
		mTree->SelectItem(it);
}

void TraceWindowFrame::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	mTree->PopTooltip();
	HTREEITEM selItem = mTree->GetSelectedItem();

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

	const DWORD kSelectionDependentItemState = selItem ? 0u : MF_GRAYED | MF_DISABLED;

	CMenu contextMenu;
	contextMenu.CreatePopupMenu();
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CUT, ICONIDX_CUT), "Cu&t");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_COPY, ICONIDX_COPY), "&Copy");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CLEAR, ICONIDX_DELETE), "&Delete");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CLEAR_ALL, ICONIDX_DELETE), "De&lete All");
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(mTraceEnabled ? MF_CHECKED : 0u, ID_TOGGLE_TRACE, "&Enable Trace");
	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

HTREEITEM
TraceWindowFrame::GetItemFromPos(POINT pos) const
{
	TVHITTESTINFO ht = {{0}};
	ht.pt = pos;
	::MapWindowPoints(HWND_DESKTOP, mTree->m_hWnd, &ht.pt, 1);

	if (mTree->HitTest(&ht) && (ht.flags & TVHT_ONITEM))
		return ht.hItem;

	return NULL;
}

DWORD
TraceWindowFrame::QueryStatus(DWORD cmdId) const
{
	switch (cmdId)
	{
	case icmdVaCmd_TraceWindowCut:
	case icmdVaCmd_TraceWindowCopy:
	case icmdVaCmd_TraceWindowDelete:
		return mTree->GetSelectedItem() != NULL;
	case icmdVaCmd_TraceWindowToggleTrace:
		return 1 | (mTraceEnabled ? 0x80000000 : 0); // latching command
	case icmdVaCmd_TraceWindowDeleteAll:
		return 1;
	}

	return (DWORD)-2;
}

HRESULT
TraceWindowFrame::Exec(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_TraceWindowToggleTrace:
		OnToggleTrace();
		break;
	case icmdVaCmd_TraceWindowCopy:
		OnCopy();
		break;
	case icmdVaCmd_TraceWindowCut:
		OnCut();
		break;
	case icmdVaCmd_TraceWindowDelete:
		OnDelete();
		break;
	case icmdVaCmd_TraceWindowDeleteAll:
		OnClear();
		break;
	default:
		return OLECMDERR_E_NOTSUPPORTED;
	}

	return S_OK;
}

bool TraceWindowFrame::IsWindowFocused() const
{
	if (m_hWnd && ::IsWindow(m_hWnd) && IsWindowVisible())
	{
		CWnd* foc = GetFocus();
		if (foc)
		{
			if (foc == this || foc == mTree)
			{
				return true;
			}
		}
	}
	return false;
}

LRESULT
TraceWindowFrame::OnExecAsync(WPARAM wParam, LPARAM lParam)
{
	AutoLockCs l(mAsyncActionsCs);
	if (!mAsyncActions.empty())
	{
		for (auto& func : mAsyncActions)
			func();

		mAsyncActions.clear();
	}
	return 0;
}

void TraceWindowFrame::PushAsyncAction(std::function<void()> fnc)
{
	AutoLockCs l(mAsyncActionsCs);
	mAsyncActions.push_back(fnc);

	// post message only if this action is the first
	// in the queue so that other calls don't lock
	// mAsyncActionsCs pointlessly.

	if (mAsyncActions.size() == 1)
		PostMessage(WM_VA_TRACEFRAMEEXECASYNC);
}

void TraceWindowFrame::Trace(const WTString txt)
{
	if (!mTraceEnabled)
	{
		vCatLog("LogSystem.Trace", "Trace: %s\n", txt.c_str());
		return;
	}

	PushAsyncAction([this, txt] { Trace(txt, false); });
}

void TraceWindowFrame::OpenParent(const WTString txt)
{
	if (!mTraceEnabled)
	{
		vCatLog("LogSystem.Trace", "Trace: [Open] %s\n", txt.c_str());
		return;
	}

	PushAsyncAction([this, txt] { Trace(txt, true); });
}

void TraceWindowFrame::Trace(const WTString& txt, bool createAsParent)
{
	vCatLog("LogSystem.Trace", "Trace: %s\n", txt.c_str());
	if (gShellIsUnloading)
		return;

	_ASSERTE(mTraceEnabled);
	HTREEITEM parent;
	{
		AutoLockCs l(
		    mParentsCs); // don't lock tree access since it uses SendMessage and multiple threads may be tracing
		parent = mParentItems.size() ? mParentItems.top() : TVI_ROOT;
	}

	HTREEITEM newItem = mTree->InsertItem(txt.c_str(), /*ICONIDX_BULLET, ICONIDX_BULLET,*/ parent);
	if (!newItem && parent != TVI_ROOT)
	{
		// parent deleted from tree by user before it was closed?
		newItem = mTree->InsertItem(txt.c_str(), /*ICONIDX_BULLET, ICONIDX_BULLET,*/ TVI_ROOT);
	}

	if (createAsParent)
	{
		mTree->SetItemState(newItem, TVIS_EXPANDED, TVIS_EXPANDED);
		AutoLockCs l(mParentsCs);
		mParentItems.push(newItem);
	}

	if (newItem)
	{
		mTree->EnsureVisible(newItem);
		mTree->SetScrollPos(SB_HORZ, 0);
	}
}

void TraceWindowFrame::CloseParent()
{
	if (!mTraceEnabled)
		return;

	PushAsyncAction([this] {
		AutoLockCs l(mParentsCs);
		mParentItems.pop();
	});
}
