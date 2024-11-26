// WorkSpaceTab.cpp : implementation file
//

#include "stdafxed.h"
#include "WorkSpaceTab.h"
#include "token.h"
#include "WTString.h"
#include "RegKeys.h"
#if _MSC_VER <= 1200
#include <..\mfc\src\afximpl.h>
#else
#include <../atlmfc/src/mfc/afximpl.h>
#endif
#include "VAWorkspaceViews.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "WindowUtils.h"
#include "RegKeys.h"
#include "wt_stdlib.h"
#include "VaService.h"
#include "TempSettingOverride.h"
#include "project.h"
#include "EdDll.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// WorkSpaceTab

WorkSpaceTab* g_WorkSpaceTab = NULL;
static HWND g_hWorkspaceWnd = NULL;
static WNDPROC g_workspaceProc;

HWND GetChildWindowFromTitle(HWND h, LPCSTR title)
{
	HWND chld = ::GetWindow(h, GW_CHILD);
	while (chld)
	{
		char name[30];
		GetWindowText(chld, name, 29);
		if (strcmp(name, title) == 0)
			return chld;
		chld = GetWindow(chld, GW_HWNDNEXT);
	}
	return NULL;
}

class CVC6WorkspaceView : public CWnd
{
  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (WM_DESTROY == message)
		{
			LRESULT retval = CWnd::WindowProc(message, wParam, lParam);
			delete this;
			return retval;
		}
		if (message == WM_KEYDOWN && (wParam == VK_NEXT || wParam == VK_PRIOR) && (GetKeyState(VK_CONTROL) & 0x1000))
		{
			g_WorkSpaceTab->SwitchTab(wParam == VK_NEXT);
			return TRUE;
		}
		return CWnd::WindowProc(message, wParam, lParam);
	}

  public:
	CVC6WorkspaceView(HWND hView)
	{
		SubclassWindow(hView);
	}
};

enum // IDB_DEVCLVW image indexes
{
	idxResourceView,
	idxClassView,
	idxFileView,
	idxDataView,
	idxVaView,
	idxVaOutline
};

WorkSpaceTab::WorkSpaceTab(CWnd* parent) : CTabCtrl(), m_keyDown(0), mHasLiveOutline(false)
{
	CRect r;
	parent->GetClientRect(&r);
	//	HINSTANCE oldMod = AfxGetResourceHandle();
	CBitmap bmpTB;
	bmpTB.LoadBitmap(IDB_DEVCLVW);

	static CImageList imgLst;
	imgLst.Create(16, 16, ILC_MASK | ILC_COLOR32, 0, 0);
	imgLst.Add(&bmpTB, RGB(0, 255, 0));

	Create(TCS_BOTTOM | WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH | TCS_FOCUSNEVER | TCS_TOOLTIPS | TCS_FORCELABELLEFT |
	           WS_TABSTOP,
	       r, parent, 101);
	SetImageList(&imgLst);
	m_font.CreatePointFont(80, "MS Sans Serif");
	SetFont(&m_font); // VC 6 only, no need for DPI handling
	SetTimer(100, 1000, NULL);

	UpdateCurrentTab();
	UpdateWindow();

	g_hWorkspaceWnd = parent->m_hWnd;
	g_workspaceProc = (WNDPROC)SetWindowLongPtr(g_hWorkspaceWnd, GWLP_WNDPROC, (LONG_PTR)&WorkspaceSubclassProc);
	VAWorkspaceViews::CreateWorkspaceView(parent->m_hWnd);

	if (gVaService)
	{
		gVaService->PaneCreated(g_hWorkspaceWnd, "VaOutline", 0);
		mHasLiveOutline = true;
	}
}

WorkSpaceTab::~WorkSpaceTab()
{
	if (HFONT(m_font))
		m_font.DeleteObject();

	if (!(g_hWorkspaceWnd && ::IsWindow(g_hWorkspaceWnd)))
		return;

	_ASSERTE(g_workspaceProc);
	if (!g_workspaceProc)
		return;

	SetWindowLongPtr(g_hWorkspaceWnd, GWLP_WNDPROC, (LONG_PTR)g_workspaceProc);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(WorkSpaceTab, CTabCtrl)
//{{AFX_MSG_MAP(WorkSpaceTab)
ON_NOTIFY_REFLECT(TCN_SELCHANGE, OnSelchange)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// WorkSpaceTab message handlers
static HWND lastTop;

void WorkSpaceTab::OnSelchange(NMHDR* pNMHDR, LRESULT* pResult)
{
	CWnd* chld = GetParent()->GetWindow(GW_CHILD);
	UINT_PTR n = pNMHDR->idFrom;
	while (chld && n--)
		chld = chld->GetWindow(GW_HWNDNEXT);

	if (chld)
		chld->ShowWindow(SW_NORMAL);
	*pResult = 0;
}

LRESULT WorkSpaceTab::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_DESTROY)
	{
		if (mHasLiveOutline && gVaService)
			gVaService->PaneDestroy(g_hWorkspaceWnd);
		return TRUE;
	}

	_ASSERTE((HWND) * this);
	if (message == WM_SIZE)
	{
		UpdateCurrentTab();
	}
	if (message == WM_PAINT)
	{
		HWND top = ::GetWindow(m_hWnd, GW_HWNDFIRST);
		if (lastTop != top)
			SetTimer(200, 100, NULL);
		if (!m_keyDown)
			lastTop = top;
	}

	if (message == WM_LBUTTONDOWN || WM_RBUTTONUP == message)
	{
		SetTimer(200, 100, NULL);
	}
	if (message == WM_TIMER)
	{
		KillTimer((UINT_PTR)wParam);
		m_keyDown = ((GetKeyState(VK_NEXT) & 0x1000) || (GetKeyState(VK_PRIOR) & 0x1000));

		TCITEM item;
		item.mask = TCIF_PARAM;
		//		HWND h = ::GetFocus();
		int i = GetCurSel();
		GetItem(i, &item);
		long cmd = 0x8f1b + i;
		const int count = GetItemCount();

		if (i < (count - (mHasLiveOutline ? 2 : 1)))
		{
			try
			{
				HWND hv = (HWND)item.lParam;
				HWND hVa = GetChildWindowFromTitle(::GetParent(hv), "VA View");
				HWND hOutline = GetChildWindowFromTitle(::GetParent(hv), "VA Outline");

				HWND htop = ::GetWindow(hv, GW_HWNDFIRST);
				::ShowWindow(hVa, SW_HIDE);
				if (hOutline)
					::ShowWindow(hOutline, SW_HIDE);
				lastTop = hv;
				if (htop != hv)
					::ShowWindow(htop, SW_HIDE);

				::EnableWindow(hv, TRUE);
				::ShowWindow(hv, SW_NORMAL);
				::SetFocus(hv);

				LRESULT r = ::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, (WPARAM)cmd, 0);
				if (hv != ::GetWindow(hv, GW_HWNDFIRST))
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, (WPARAM)cmd, 0);
				if (hv != ::GetWindow(hv, GW_HWNDFIRST))
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, (WPARAM)cmd, 0);
				if (hv != ::GetWindow(hv, GW_HWNDFIRST))
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, (WPARAM)cmd, 0);
				//				HWND hCur = ::GetWindow(hv, GW_HWNDFIRST);
				if (!r && wParam != 200)
					SetTimer(201, 1000, NULL);
				else
					KillTimer(wParam);
			}
			catch (...)
			{
				VALOGEXCEPTION("WTS:");
				_ASSERTE(!"exception in WorkSpaceTab::DefWindowProc");
			}
		}
		else
		{
			UpdateCurrentTab();
			HWND hv = (HWND)item.lParam;
			while (::GetWindow(hv, GW_CHILD))
				hv = ::GetWindow(hv, GW_CHILD);
			::SetFocus(hv);
		}

		// save active tab
		WTString CurTabKey;
		CurTabKey.WTFormat("VAViewCurTab%d", GetItemCount());
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, CurTabKey.c_str(), itos(GetCurSel()).c_str());
	}

	return CTabCtrl::DefWindowProc(message, wParam, lParam);
}

void WorkSpaceTab::UpdateCurrentTab()
{
	static BOOL inUpdate = FALSE;
	if (inUpdate)
		return;

	TempSettingOverride<BOOL> setInUpdate(&inUpdate, TRUE);
	KillTimer(100);
	TCITEM item;
	item.mask = TCIF_PARAM | TCIF_IMAGE;
	SetMinTabWidth(100);

	HWND hPar = GetParent()->m_hWnd;
	CRect rc;
	GetParent()->GetClientRect(&rc);
	CRect rr(rc);
	rr.top = rr.bottom - 27;
	MoveWindow(&rr);
	HWND hTop = ::GetWindow(::GetParent(m_hWnd), GW_CHILD);

	int i = GetCurSel();
	GetItem(i, &item);

	HWND h = (HWND)::GetFocus();
	HWND hActive = (HWND)item.lParam;
	if (rc.bottom < 2)
		return;
	rc.DeflateRect(3, 3, 3, 3);
	if (rc.bottom > 22)
		rc.bottom -= 22;

	for (h = hTop; h; h = ::GetWindow(h, GW_HWNDNEXT))
	{
		if (h == m_hWnd)
			continue;

		if ((GetWindowLongPtr(h, GWLP_HINSTANCE)) == (LONG_PTR)gVaDllApp->GetVaAddress())
		{
			::ShowWindow(h, SW_NORMAL);
			::MoveWindow(h, rc.left, rc.top, rc.Width(), rc.Height(), TRUE);
		}

		const BOOL top = (h == hActive);
		::EnableWindow(h, top);
		::SendMessage(h, WM_SETREDRAW, (WPARAM)top, 0);
		if (top)
		{
			HWND h2 = ::GetFocus();
			if (h2)
			{
				::InvalidateRect(h2, NULL, TRUE);
				::RedrawWindow(h2, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
			}
		}
	}

	{
		const int count = GetItemCount();
		CRect r;
		GetClientRect(&r);
		CSize oldsz;
		if (count > (mHasLiveOutline ? 2 : 1))
			oldsz.cx = r.Width() / (count)-1;
		else
			oldsz.cx = 80;
		oldsz.cy = 20;
		SetItemSize(oldsz);
	}

	{
		HWND hChld = ::GetWindow(hPar, GW_CHILD);
		int i2;
		for (i2 = 0; hChld; i2++)
			hChld = ::GetWindow(hChld, GW_HWNDNEXT);

		static int lc = 0;
		if (lc == i2)
			return;

		lc = i2;
	}
	DeleteAllItems();

	uint mask = TCIF_PARAM | TCIF_IMAGE | TCIF_TEXT;
#pragma warning(disable : 4706)

	if ((h = GetChildWindowFromTitle(hPar, "VA View")) != NULL)
		InsertItem(mask, 0, "VA View", idxVaView, (LPARAM)h);
	if ((h = GetChildWindowFromTitle(hPar, "VA Outline")) != NULL)
		InsertItem(mask, 1, "VA Outline", idxVaOutline, (LPARAM)h);

	int insPos = 0;
	for (HWND hChld = ::GetWindow(hPar, GW_CHILD); hChld; hChld = ::GetWindow(hChld, GW_HWNDNEXT))
	{
		WTString title = GetWindowTextString(hChld);
		LONG_PTR inst = (LONG_PTR)::GetWindowLongPtr(hChld, GWLP_HINSTANCE);
		if (inst != (LONG_PTR)gVaDllApp->GetVaAddress() && !FromHandlePermanent(hChld))
			new CVC6WorkspaceView(hChld);
		if (title.GetLength() && title != "VA View" && title != "VA Outline")
		{
			if (title == "ClassView" || title.contains("Klassen"))
			{
				insPos++;
				InsertItem(mask, 0, title.c_str(), idxClassView, (LPARAM)hChld);
			}
			else if (title == "ResourceView" || title.contains("Ressourcen"))
			{
				InsertItem(mask, insPos, title.c_str(), idxResourceView, (LPARAM)hChld);
				insPos++;
			}
			else if (title == "FileView" || title.contains("Dateien"))
			{
				InsertItem(mask, insPos, title.c_str(), idxFileView, (LPARAM)hChld);
			}
			else // dataview?
				InsertItem(mask, insPos, title.c_str(), idxDataView, (LPARAM)hChld);
			Log((WTString("WTTabView ") + title).c_str());
		}
	}

	const int count = GetItemCount();
	if (count > (mHasLiveOutline ? 2 : 1))
	{
		CRect r;
		GetClientRect(&r);
		CSize oldsz;
		oldsz.cx = r.Width() / (count)-1;
		oldsz.cy = 20;
		SetItemSize(oldsz);
	}

	const int kTabCnt = GetItemCount();
	int newTabItem = GetCurSel();
	WTString CurTabKey;
	CurTabKey.WTFormat("VAViewCurTab%d", count);
	WTString CurTabStr = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, CurTabKey.c_str());
	if (CurTabStr.GetLength() && atoi(CurTabStr.c_str()) < kTabCnt && atoi(CurTabStr.c_str()) != newTabItem)
	{
		newTabItem = atoi(CurTabStr.c_str());
		SetCurSel(atoi(CurTabStr.c_str()));
	}

	if (mHasLiveOutline && newTabItem == (kTabCnt - 1))
	{
		if (gVaService)
			gVaService->PaneActivated(g_hWorkspaceWnd);
	}

	SetTimer(200, 500, NULL);
}

void WorkSpaceTab::SelectVaView()
{
	const int kTabCnt = GetItemCount();
	if (GetCurSel() != kTabCnt - (mHasLiveOutline ? 2 : 1))
	{
		SetCurSel(kTabCnt - (mHasLiveOutline ? 2 : 1));
		SetTimer(200, 50, NULL);
	}
}

void WorkSpaceTab::SelectVaOutline()
{
	if (!mHasLiveOutline)
		return;

	const int kTabCnt = GetItemCount();
	if (GetCurSel() != kTabCnt - 1)
	{
		SetCurSel(kTabCnt - 1);
		SetTimer(200, 50, NULL);
	}
}

void WorkSpaceTab::SwitchTab(bool next)
{
	if (next)
		SelectNextTab();
	else
		SelectPreviousTab();
}

void WorkSpaceTab::SelectNextTab()
{
	const int kTabCnt = GetItemCount();
	const int kTabItem = GetCurSel();
	if (kTabItem + 1 == kTabCnt)
		SetCurSel(0);
	else
		SetCurSel(kTabItem + 1);
	SetTimer(200, 50, NULL);
}

void WorkSpaceTab::SelectPreviousTab()
{
	const int kTabCnt = GetItemCount();
	const int kTabItem = GetCurSel();
	if (kTabItem == 0)
		SetCurSel(kTabCnt - 1);
	else
		SetCurSel(kTabItem - 1);
	SetTimer(200, 50, NULL);
}

LRESULT CALLBACK WorkSpaceTab::WorkspaceSubclassProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	LRESULT retval = CallWindowProc(g_workspaceProc, hWnd, msg, wparam, lparam);
	if (!g_WorkSpaceTab)
		return retval;

	switch (msg)
	{
	case WM_KEYDOWN:
		g_WorkSpaceTab->Invalidate(FALSE);
		break;
	case WM_SETFOCUS: {
		TCITEM item;
		item.mask = TCIF_PARAM | TCIF_IMAGE;
		int i = g_WorkSpaceTab->GetCurSel();
		if (i == (g_WorkSpaceTab->GetItemCount() - 1))
		{
			g_WorkSpaceTab->GetItem(i, &item);
			HWND ch = (HWND)item.lParam;
			while (::GetWindow(ch, GW_CHILD))
				ch = ::GetWindow(ch, GW_CHILD);
			if (ch)
			{
				::SetFocus(ch);
				return TRUE;
			}
		}
	}
	break;
	case WM_WINDOWPOSCHANGED:
	case WM_STYLECHANGED:
		g_WorkSpaceTab->UpdateCurrentTab();
		break;
	}
	return retval;
}

static BOOL CALLBACK FindWorkSpaceWnd(HWND hwnd, LPARAM pid)
{
	DWORD wndPid;
	GetWindowThreadProcessId(hwnd, &wndPid);
	if ((DWORD)pid == wndPid && (GetWindowLong(hwnd, GWL_STYLE) & WS_POPUPWINDOW))
	{
		WTString caption = GetWindowTextString(hwnd);
		if (caption == "Workspace")
		{
			g_hWorkspaceWnd = hwnd;
			return FALSE; // for a hit
		}
		else
			return TRUE;
	}
	return TRUE; // to continue
}

bool VAWorkspaceSetup_VC6()
{
	_ASSERTE(gShellAttr->IsMsdev());
	// The option "VaView" is used for both VA View and VA Outline
	static BOOL doVAVIEW = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "VaView") != "No";
	if (!doVAVIEW)
		return TRUE;

	if (g_WorkSpaceTab)
		return false;

	if (!gMainWnd || !gMainWnd->m_hWnd)
		return false;

	CWnd* tabwnd = gMainWnd->GetDescendantWindow(0x7310);
	if (!tabwnd)
	{
		// Undocked?
		HWND hmdi = ::GetWindow(gMainWnd->GetSafeHwnd(), GW_CHILD);
		while (hmdi && ::GetDlgCtrlID(hmdi) != 0xe900)
			hmdi = ::GetWindow(hmdi, GW_HWNDNEXT);
		if (hmdi)
		{
			HWND hchld = ::GetWindow(hmdi, GW_CHILD);
			while (hchld)
			{
				if (GetWindowTextString(hchld) == "Workspace")
				{
					hchld = ::GetWindow(hchld, GW_CHILD);
					if (hchld)
						tabwnd = CWnd::FromHandle(::GetWindow(hchld, GW_CHILD));
					break;
				}
				hchld = GetWindow(hchld, GW_HWNDNEXT);
			}
		}
		// look for top level windows named "Workspace" that are in this
		//  process and have WS_POPUPWINDOW style
		if (!tabwnd)
		{
			if (EnumWindows(FindWorkSpaceWnd, (LPARAM)GetCurrentProcessId()))
				return false;
			CWnd* mwnd = CWnd::FromHandle(g_hWorkspaceWnd);
			if (mwnd)
				tabwnd = mwnd->GetDescendantWindow(0x7310);
		}
	}

	if (tabwnd)
	{
		g_WorkSpaceTab = new WorkSpaceTab(tabwnd);
		return true;
	}

	g_hWorkspaceWnd = NULL;
	return false;
}
