#include "StdAfxEd.h"
#include "Vc6RefResultsCloneToolwnd.h"
#include "VaService.h"
#include "FindReferencesResultsFrame.h"
#include "Registry.h"
#include "RegKeys.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static UINT sMultiToolWndId = 0u;
static CRect sToolWndPos(10, 10, 700, 300);
static const UINT IDM_TOGGLETOPMOST = 300u;
#define ID_RK_VC6CloneWndTopMost "ClonedRefResultsOnTop"

Vc6RefResultsCloneToolwnd::Vc6RefResultsCloneToolwnd(const PrimaryResultsFrame* refsToCopy)
    : mClient(NULL), mIsTopMost(::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_VC6CloneWndTopMost, true))
{
	sMultiToolWndId++;

	DWORD exStyle = WS_EX_TOOLWINDOW;
	if (mIsTopMost)
		exStyle |= WS_EX_TOPMOST;

	if (0)
	{
		// this causes a leak of 6 region objects...
		CreateEx(exStyle, AfxRegisterWndClass(0), "", WS_VISIBLE | WS_THICKFRAME | WS_SYSMENU, sToolWndPos, NULL, 0);
	}
	else
	{
		HWND hWnd = ::CreateWindowExA(exStyle, AfxRegisterWndClass(0), "", WS_VISIBLE | WS_THICKFRAME | WS_SYSMENU,
		                              sToolWndPos.left, sToolWndPos.top, sToolWndPos.Width(), sToolWndPos.Height(),
		                              NULL, 0, 0, NULL);
		SubclassWindow(hWnd);
	}

	gActiveFindRefsResultsFrame = mClient = new SecondaryResultsFrameVc6(this, refsToCopy);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		pSysMenu->AppendMenu(MF_SEPARATOR);
		pSysMenu->AppendMenu(mIsTopMost ? MF_CHECKED : 0u, IDM_TOGGLETOPMOST, "&Stay on top");
	}

	if (gVaService)
		gVaService->PaneCreated(mClient->m_hWnd, "FindReferencesClone", sMultiToolWndId);
}

Vc6RefResultsCloneToolwnd::~Vc6RefResultsCloneToolwnd()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(Vc6RefResultsCloneToolwnd, CWnd)
//{{AFX_MSG_MAP(ArgToolTip)
ON_WM_SYSCOMMAND()
ON_WM_ACTIVATE()
ON_WM_DESTROY()
ON_WM_NCDESTROY()
ON_WM_SIZE()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

void Vc6RefResultsCloneToolwnd::OnActivate(UINT, CWnd*, BOOL)
{
	if (gVaService && mClient)
		gVaService->PaneActivated(mClient->m_hWnd);
}

void Vc6RefResultsCloneToolwnd::OnDestroy()
{
	GetWindowRect(&sToolWndPos);
	CWnd::OnDestroy();

	if (mClient)
	{
		SecondaryResultsFrameVc6* client = mClient;
		mClient = NULL;
		// paneDestroy is responsible for deleting mClient
		if (gVaService)
			gVaService->PaneDestroy(client->m_hWnd);
	}
}

void Vc6RefResultsCloneToolwnd::OnNcDestroy()
{
	CWnd::OnNcDestroy();
	_ASSERTE(!mClient); // if this fires, then there's a leak
	delete this;
}

void Vc6RefResultsCloneToolwnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (mClient)
	{
		CRect rc;
		GetClientRect(&rc);
		mClient->MoveWindow(&rc, false);
		mClient->OnSize();
	}
}

void Vc6RefResultsCloneToolwnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID == IDM_TOGGLETOPMOST)
	{
		mIsTopMost = !mIsTopMost;
		::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_VC6CloneWndTopMost, mIsTopMost);
		if (mIsTopMost)
			ModifyStyleEx(0, WS_EX_TOPMOST);
		else
			ModifyStyleEx(WS_EX_TOPMOST, 0);

		SetWindowPos(mIsTopMost ? &wndTopMost : &wndNoTopMost, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);

		CMenu* pMenu = GetSystemMenu(FALSE);
		if (pMenu != NULL)
			pMenu->CheckMenuItem(IDM_TOGGLETOPMOST, MF_BYCOMMAND | mIsTopMost ? MF_CHECKED : 0u);
	}
	else
	{
		__super::OnSysCommand(nID, lParam);
	}
}
