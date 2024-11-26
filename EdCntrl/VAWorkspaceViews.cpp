#include "stdafxed.h"
#include "Edcnt.h"
#include "VAWorkspaceViews.h"
#include "VaMessages.h"
#include "VAClassView.h"
#include "WorkSpaceTab.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "RegKeys.h"
#include "wt_stdlib.h"
#include "VsToolWindowPane.h"
#include "IdeSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

VAWorkspaceViews* VAWorkspaceViews::sViews = NULL;
static const UINT_PTR kTimerId_GotoFiles = 200u;
static const UINT_PTR kTimerId_GotoSymbols = 201u;
static const UINT_PTR kTimerId_GotoOutline = 202u;
static const UINT_PTR kTimerId_GotoHcb = 203u;
static const UINT_PTR kTimerId_GotoMru = 204u;

void VAWorkspaceViews::CreateWorkspaceView(HWND parent)
{
	if (parent)
	{
		if (gShellAttr->IsDevenv())
		{
			static VsToolWindowPane pw(parent);
		}
		if (!sViews)
		{
			_ASSERTE(!g_CVAClassView);
			sViews = new VAWorkspaceViews(CWnd::FromHandle(parent));
			_ASSERTE(g_CVAClassView);
		}
	}
	else
	{
		VAWorkspaceViews* ows = sViews;
		sViews = NULL;
		if (ows)
			delete ows;
	}
}

void VAWorkspaceViews::GotoWorkspace(UINT_PTR timerId, TIMERPROC lpTimerFunc, bool vaView /*= true*/)
{
	if (gShellAttr->IsMsdev())
	{
		if (!g_WorkSpaceTab)
			VAWorkspaceSetup_VC6();
		if (g_WorkSpaceTab)
		{
			if (vaView)
				g_WorkSpaceTab->SelectVaView();
			else
			{
				_ASSERTE(gShellAttr->IsMsdev());
				g_WorkSpaceTab->SelectVaOutline();
			}
		}
	}

	// need to use timers since our view setup is generally deferred
	if (sViews)
		sViews->SetTimer(timerId, gShellAttr->IsMsdev() ? 300u : 100u, NULL);
	else
		::SetTimer(NULL, timerId, 100u, lpTimerFunc);
}

void VAWorkspaceViews::GotoVaOutline()
{
	_ASSERTE(gShellAttr->IsMsdevOrCppBuilder());
	GotoWorkspace(kTimerId_GotoOutline, PrecreateGotoOutlineTimer, false);
}

void VAWorkspaceViews::GotoFilesInWorkspace()
{
	if (sViews)
		sViews->mFocusTarget = ftFis;
	GotoWorkspace(kTimerId_GotoFiles, PrecreateGotoFilesTimer);
}

void VAWorkspaceViews::GotoSymbolsInWorkspace()
{
	if (sViews)
		sViews->mFocusTarget = ftSis;
	GotoWorkspace(kTimerId_GotoSymbols, PrecreateGotoSymbolsTimer);
}

void VAWorkspaceViews::GotoHcb()
{
	if (sViews)
		sViews->mFocusTarget = ftHcb;
	GotoWorkspace(kTimerId_GotoHcb, PrecreateGotoHcbTimer);
}

void VAWorkspaceViews::GotoMru()
{
	if (sViews)
		sViews->mFocusTarget = ftMru;
	GotoWorkspace(kTimerId_GotoMru, PrecreateGotoMruTimer);
}

void VAWorkspaceViews::Activated()
{
	if (sViews)
	{
		switch (sViews->mFocusTarget)
		{
		case ftHcb:
			if (g_CVAClassView)
				g_CVAClassView->FocusHcb();
			break;
		case ftHcbLock:
			if (g_CVAClassView)
				g_CVAClassView->FocusHcbLock();
			break;
		case ftMru:
			sViews->m_VAFileView.SetFocusToMru();
			break;
		case ftSis:
			sViews->m_VAFileView.SetFocusToSis();
			break;
		case ftFis:
		default:
			// old default behavior
			sViews->m_VAFileView.SetFocusToFis();
			break;
		}
	}
}

VAWorkspaceViews::VAWorkspaceViews(CWnd* parent)
{
	mFocusTarget = ftDefault;
	m_created = FALSE;
	CreateStatic(parent, 2, 1);
	SetWindowText("VA View");
	CRect r;
	parent->GetClientRect(&r);
	r.SetRect(0, 0, 500, 600);
	r.bottom -= 25;
	CCreateContext contextT;
	CreateView(0, 0, RUNTIME_CLASS(CWnd), CSize(r.Width(), r.Height() / 3), &contextT);
	CreateView(1, 0, RUNTIME_CLASS(CWnd), CSize(r.Width(), 2 * (r.Height() / 3)), &contextT);

	m_VAFileView.Create(UINT(gShellAttr->IsDevenv11OrHigher() ? IDD_FILEVIEWv11 : IDD_FILEVIEW), GetPane(0, 0));
	m_VAClassView.Create(UINT(gShellAttr->IsDevenv11OrHigher() ? IDD_VACLASSVIEWv11 : IDD_VACLASSVIEW), GetPane(1, 0));
	m_created = TRUE;

	m_VAFileView.ModifyStyle(DS_3DLOOK, 0);
	m_VAClassView.ModifyStyle(DS_3DLOOK, 0);

	WTString viewPos = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "VAViewSplitVal");
	if (viewPos.GetLength())
		TrackRowSize(atoi(viewPos.c_str()), 0);

	m_VAFileView.GotoFiles(false);
}

VAWorkspaceViews::~VAWorkspaceViews()
{
	if (m_hWnd && ::IsWindow(m_hWnd))
		DestroyWindow();
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VAWorkspaceViews, CFlatSplitterWnd)
//{{AFX_MSG_MAP(VAWorkspaceViews)
ON_WM_TIMER()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

void VAWorkspaceViews::OnTimer(UINT_PTR nIDEvent)
{
	KillTimer(nIDEvent);
	switch (nIDEvent)
	{
	case kTimerId_GotoFiles:
		_ASSERTE(sViews);
		sViews->m_VAFileView.GotoFiles(true);
		break;
	case kTimerId_GotoSymbols:
		_ASSERTE(sViews);
		sViews->m_VAFileView.GotoSymbols(true);
		break;
	case kTimerId_GotoOutline:
		_ASSERTE(sViews && g_WorkSpaceTab);
		g_WorkSpaceTab->SelectVaOutline();
		break;
	case kTimerId_GotoMru:
		_ASSERTE(sViews);
		sViews->m_VAFileView.SetFocusToMru();
		break;
	case kTimerId_GotoHcb:
		_ASSERTE(sViews);
		_ASSERTE(g_CVAClassView); // should have been setup from package
		if (g_CVAClassView)
			g_CVAClassView->FocusHcb();
		break;
	default:
		_ASSERTE(!"VAWorkspaceViews::OnTimer unhandled timer id");
		break;
	}
}

LRESULT
VAWorkspaceViews::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_KEYDOWN:
		return FALSE;
	default:
		break;
	}

	const LRESULT r = __super::WindowProc(message, wParam, lParam);

	switch (message)
	{
	case WM_SIZE:
	case WM_LBUTTONUP:
		SizeViews();
		return FALSE;
	default:
		break;
	}
	return r;
}

void VAWorkspaceViews::SizeViews()
{
	if (m_created)
	{
		for (HWND hview = ::GetWindow(m_hWnd, GW_CHILD); hview; hview = ::GetWindow(hview, GW_HWNDNEXT))
		{
			HWND hchild = ::GetWindow(hview, GW_CHILD);
			if (hchild)
			{
				CRect rc;
				::GetClientRect(hview, &rc);
				if (rc.Height())
					::MoveWindow(hchild, 0, 0, rc.Width(), rc.Height(), TRUE);
			}
		}
	}
}

void VAWorkspaceViews::TrackRowSize(int y, int row)
{
	ASSERT_VALID(this);
	ASSERT(m_nRows > 1);

	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "VAViewSplitVal", itos(y));

	CPoint pt(0, y);
	ClientToScreen(&pt);
	GetPane(row, 0)->ScreenToClient(&pt);
	if (row < m_nRows)
		m_pRowInfo[row + 1].nIdealSize += m_pRowInfo[row].nIdealSize - pt.y; // new size
	m_pRowInfo[row].nIdealSize = pt.y;                                       // new size
	if (pt.y < m_pRowInfo[row].nMinSize)
	{
		// resized too small
		m_pRowInfo[row].nIdealSize = 0; // make it go away
		if (GetStyle() & SPLS_DYNAMIC_SPLIT)
			DeleteRow(row);
	}
	else if (m_pRowInfo[row].nCurSize + m_pRowInfo[row + 1].nCurSize < pt.y + m_pRowInfo[row + 1].nMinSize)
	{
		// not enough room for other pane
		if (GetStyle() & SPLS_DYNAMIC_SPLIT)
			DeleteRow(row + 1);
	}
}

void CALLBACK VAWorkspaceViews::PrecreateGotoFilesTimer(HWND, UINT, UINT_PTR id, DWORD)
{
	if (sViews)
	{
		::KillTimer(NULL, id);
		sViews->m_VAFileView.GotoFiles(true);
	}
}

void CALLBACK VAWorkspaceViews::PrecreateGotoSymbolsTimer(HWND, UINT, UINT_PTR id, DWORD)
{
	if (sViews)
	{
		::KillTimer(NULL, id);
		sViews->m_VAFileView.GotoSymbols(true);
	}
}

void CALLBACK VAWorkspaceViews::PrecreateGotoOutlineTimer(HWND, UINT, UINT_PTR idEvent, DWORD)
{
	if (sViews && g_WorkSpaceTab)
	{
		::KillTimer(NULL, idEvent);
		g_WorkSpaceTab->SelectVaOutline();
	}
}

void CALLBACK VAWorkspaceViews::PrecreateGotoHcbTimer(HWND, UINT, UINT_PTR id, DWORD)
{
	if (sViews)
	{
		::KillTimer(NULL, id);
		_ASSERTE(g_CVAClassView); // should have been setup from package
		if (g_CVAClassView)
			g_CVAClassView->FocusHcb();
	}
}

void CALLBACK VAWorkspaceViews::PrecreateGotoMruTimer(HWND, UINT, UINT_PTR id, DWORD)
{
	if (sViews)
	{
		::KillTimer(NULL, id);
		sViews->m_VAFileView.SetFocusToMru();
	}
}

DWORD
VAWorkspaceViews::QueryStatus(DWORD cmdId)
{
	if (sViews && sViews->m_VAClassView.HcbHasFocus())
		return sViews->m_VAClassView.QueryStatus(cmdId);
	return 0;
}

HRESULT
VAWorkspaceViews::Exec(DWORD cmdId)
{
	if (sViews)
		return sViews->m_VAClassView.Exec(cmdId);
	return E_UNEXPECTED;
}

void VAWorkspaceViews::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);

	if (change == CWndDpiAware::DpiChange::AfterParent)
	{
		// Update layout

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

		if (GetPreviousDpiX() > 0 && DpiChanged())
		{
			auto scale = GetDpiChangeScaleFactor();

			for (int i = 0; i < m_nRows; i++)
			{
				SetRowInfo(i, WindowScaler::Scale(m_pRowInfo[i].nIdealSize, scale),
				           WindowScaler::Scale(m_pRowInfo[i].nMinSize, scale));
			}
		}

		RecalcLayout();
		SizeViews();
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

void VAWorkspaceViews::ThemeChanged()
{
	if (sViews && sViews->GetSafeHwnd())
	{
		if (sViews->m_VAFileView.GetSafeHwnd())
			sViews->m_VAFileView.SettingsChanged();
		if (sViews->m_VAClassView.GetSafeHwnd())
			sViews->m_VAClassView.ThemeUpdated();
		sViews->CFlatSplitterWnd::Invalidate(TRUE);
		sViews->RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	}
}

void VAWorkspaceViews::OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rectArg)
{
	if (pDC && CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		COLORREF clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
		if (clr != UINT_MAX)
		{
			pDC->FillSolidRect(rectArg, clr);
			return;
		}
	}

	__super::OnDrawSplitter(pDC, nType, rectArg);
}

void VAWorkspaceViews::SetFocusTarget(FocusTarget tgt)
{
	if (sViews)
		sViews->mFocusTarget = tgt;
}

void VAWorkspaceViews::MoveFocus(bool goReverse)
{
	if (!sViews)
		return;

	switch (sViews->mFocusTarget)
	{
	case ftFis:
		sViews->mFocusTarget = goReverse ? ftHcb : ftSis;
		break;
	case ftSis:
		sViews->mFocusTarget = goReverse ? ftFis : ftMru;
		break;
	case ftMru:
		sViews->mFocusTarget = goReverse ? ftSis : ftHcbLock;
		break;
	case ftHcbLock:
		sViews->mFocusTarget = goReverse ? ftMru : ftHcb;
		break;
	case ftHcb:
		sViews->mFocusTarget = goReverse ? ftHcbLock : ftFis;
		break;
	default:
		_ASSERTE(!"unhandled va view focus target in MoveFocus");
	}

	VAWorkspaceViews::Activated();
}
