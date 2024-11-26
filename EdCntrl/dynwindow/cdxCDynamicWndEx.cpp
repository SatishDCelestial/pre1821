// cdxCDynamicWndEx.cpp: implementation of the cdxCDynamicWndEx class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafxed.h"
#include "../DpiCookbook/VsUIDpiHelper.h"
#include "cdxCDynamicWndEx.h"
#include "../RegKeys.h"
#include "../RedirectRegistryToVA.h"
#include <algorithm>
using namespace std;
#include "../Settings.h"
#include "DpiCookbook/VsUIDpiAwareness.h"
#include "DebugStream.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// Some static variables (taken from cdxCDynamicControlsManager)
/////////////////////////////////////////////////////////////////////////////

#define	REGVAL_NOSTATE		-1
#define	REGVAL_VISIBLE		1
#define	REGVAL_HIDDEN		0
#define	REGVAL_MAXIMIZED	1
#define	REGVAL_ICONIC		0
#define	REGVAL_INVALID		0
#define	REGVAL_VALID		1

/*
 * registry value names
 * (for StoreWindowPosition()/RestoreWindowPosition())
 */

static LPCTSTR	lpszRegVal_Left		=	_T("Left"),
					lpszRegVal_Right		=	_T("Right"),
					lpszRegVal_Top			=	_T("Top"),
					lpszRegVal_Bottom		=	_T("Bottom"),
					lpszRegVal_Visible	=	_T("Visibility"),
					lpszRegVal_State		=	_T("State"),
					lpszRegVal_Valid		=	_T("(valid)"),
					lpszRegVal_SameMonitor	=	_T("SameMonitor");

LPCTSTR	cdxCDynamicWndEx::M_lpszAutoPosProfileSection	=	_T("WindowPositions");
CString profile_suffix;

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicWndEx
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicWndEx stretches windows
/////////////////////////////////////////////////////////////////////////////

static inline CString _makeFullProfile(LPCTSTR lpszBase, const CString & str)
{
	CString	s	=	lpszBase;

	if(s.GetLength() && (s[s.GetLength()-1] != _T('\\')))
		s	+=	_T('\\');

	s	+=	str;
	return s;
}

void cdxCDynamicWndEx::OnInitialized()
{
	ASSERT(IsWindow());

	if(!m_strAutoPos.IsEmpty() && !(m_nFlags & flNoSavePos))
	{
		CRedirectRegistryToVA rreg;

		if(!RestoreWindowPosition(_makeFullProfile(M_lpszAutoPosProfileSection + profile_suffix,m_strAutoPos), rflg_none/*rflg_all*/))
		{
			Window()->CenterWindow();
			StretchWindow(10);
		}
	}
}

void cdxCDynamicWndEx::OnDestroying()
{
	if(!m_strAutoPos.IsEmpty() && IsWindow() && !(m_nFlags & flNoSavePos)) {
		CRedirectRegistryToVA rreg;

		StoreWindowPosition(_makeFullProfile(M_lpszAutoPosProfileSection + profile_suffix,m_strAutoPos));
	}
}

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicWndEx stretches windows
/////////////////////////////////////////////////////////////////////////////

/*
 * stretches the window by szDelta (i.e. if szDelta is 100, the window is enlarged by 100 pixels)
 * stretching means that the center point of the window remains
 *
 * returns false if the window would be smaller than (1,1)
 *
 * NOTE: this function does NOT care of the min/max dimensions of a window
 *			Use MoveWindow() if you need to take care of it.
 *
 * STATIC
 */

bool cdxCDynamicWndEx::StretchWindow(const CSize & szDelta)
{
	if(!IsWindow())
	{
		ASSERT(false);
		return false;
	}

	CWnd	*pWnd	=	Window();
#if !defined(_WIN32_WCE)
	WINDOWPLACEMENT	wpl;
	pWnd->GetWindowPlacement(&wpl);

	wpl.rcNormalPosition.left		-=	szDelta.cx / 2;
	wpl.rcNormalPosition.right		+=	(szDelta.cx + 1) / 2;
	wpl.rcNormalPosition.top		-=	szDelta.cy / 2;
	wpl.rcNormalPosition.bottom	+=	(szDelta.cy + 1) / 2;
//	wpl.flags	=	SW_SHOWNA|SW_SHOWNOACTIVATE;

	if((wpl.rcNormalPosition.left >= wpl.rcNormalPosition.right) ||
		(wpl.rcNormalPosition.top >= wpl.rcNormalPosition.bottom))
		return false;

	VERIFY( pWnd->SetWindowPos(NULL,
										wpl.rcNormalPosition.left,
										wpl.rcNormalPosition.top,
										wpl.rcNormalPosition.right - wpl.rcNormalPosition.left,
										wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top,
										SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER) );
#else
	RECT r;
	pWnd->GetWindowRect(&r);

	r.left		-=	szDelta.cx / 2;
	r.right		+=	(szDelta.cx + 1) / 2;
	r.top		-=	szDelta.cy / 2;
	r.bottom	+=	(szDelta.cy + 1) / 2;
//	wpl.flags	=	SW_SHOWNA|SW_SHOWNOACTIVATE;

	if((r.left >= r.right) ||
		(r.top >= r.bottom))
		return false;

	VERIFY( pWnd->SetWindowPos(NULL,
										r.left,
										r.top,
										r.right - r.left,
										r.bottom - r.top,
										SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER) );

#endif

	return true;
}

/*
 * stretch window by a percent value
 * the algorithm calculates the new size for both dimensions by:
 *
 *  newWid = oldWid + (oldWid * iAddPcnt) / 100
 *
 * NOTE: iAddPcnt may even be nagtive, but it MUST be greater than -100.
 * NOTE: this function does NOT care of the min/max dimensions of a window
 *
 * The function will return false if the new size would be empty.
 */

bool cdxCDynamicWndEx::StretchWindow(int iAddPcnt)
{
	if(!IsWindow())
	{
		ASSERT(false);
		return false;
	}

	CSize	szDelta	=	GetCurrentClientSize() + GetBorderSize();

	szDelta.cx	=	(szDelta.cx * iAddPcnt) / 100;
	szDelta.cy	=	(szDelta.cy * iAddPcnt) / 100;

	return StretchWindow(szDelta);
}


/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicWndEx registry positioning
/////////////////////////////////////////////////////////////////////////////

/*
 * stores a window's position and visiblity to the registry.
 *	return false if any error occured
 */

bool cdxCDynamicWndEx::StoreWindowPosition(LPCTSTR lpszProfile)
{
	if(!IsWindow() || !lpszProfile || !*lpszProfile)
	{
		ASSERT(false);
		return false;
	}

	VADEBUGPRINT("#DPI cdxCDynamicWndEx::StoreWindowPosition");

	CWnd	*pWnd	=	Window();

// #if !defined(_WIN32_WCE)
// 	WINDOWPLACEMENT	wpl;
// 	VERIFY( pWnd->GetWindowPlacement(&wpl) );
// #else
	RECT r;
	pWnd->GetWindowRect(&r);
//#endif

	HMONITOR same_monitor = IsOnTheSameMonitor();
	if(same_monitor)
	{
		MONITORINFO mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		if(::GetMonitorInfo(same_monitor, &mi))
			::OffsetRect(&r, -mi.rcMonitor.left, -mi.rcMonitor.top);
		else
			same_monitor = NULL;
	}

	BOOL	bVisible	=	pWnd->IsWindowVisible();
	int	iState	=	REGVAL_NOSTATE;

	if(pWnd->IsIconic())
		iState	=	REGVAL_ICONIC;
#if !defined(_WIN32_WCE)
	else
		if(pWnd->IsZoomed())
			iState	=	REGVAL_MAXIMIZED;
#endif

	CWinApp	*app	=	AfxGetApp();

	if(!app->m_pszRegistryKey || !*app->m_pszRegistryKey)
	{
		TRACE(_T("*** NOTE[cdxCDynamicWndEx::StoreWindowPosition()]: To properly store and restore a window's position, please call CWinApp::SetRegistryKey() in you app's InitInstance() !\n"));
		return false;
	}

	if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		auto helper = VsUI::DpiHelper::GetScreenHelper(pWnd->m_hWnd);
		if (helper)
		{
			helper->DeviceToLogicalUnits(&r, (LPPOINT)&r);
		}
	}
	else
	{
		VsUI::DpiHelper::DeviceToLogicalUnits(&r);
	}
	
	return	app->WriteProfileInt(lpszProfile,	lpszRegVal_Valid,	REGVAL_INVALID) &&	// invalidate first
// #if !defined(_WIN32_WCE)
// 				app->WriteProfileInt(lpszProfile,	lpszRegVal_Left,		wpl.rcNormalPosition.left) &&
// 				app->WriteProfileInt(lpszProfile,	lpszRegVal_Right,		wpl.rcNormalPosition.right) &&
// 				app->WriteProfileInt(lpszProfile,	lpszRegVal_Top,		wpl.rcNormalPosition.top) &&
// 				app->WriteProfileInt(lpszProfile,	lpszRegVal_Bottom,	wpl.rcNormalPosition.bottom) &&
// #else
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Left,		r.left) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Right,		r.right) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Top,		r.top) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Bottom,	r.bottom) &&
//#endif
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Visible,	bVisible ? REGVAL_VISIBLE : REGVAL_HIDDEN) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_State,		iState) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_Valid,	REGVAL_VALID) &&
				app->WriteProfileInt(lpszProfile,	lpszRegVal_SameMonitor, same_monitor ? 1 : 0);		// validate position
}

HMONITOR cdxCDynamicWndEx::IsOnTheSameMonitor() const
{
	CWnd *_this = Window();
	if(!_this || !_this->m_hWnd)
		return NULL;
	HMONITOR mon1 = ::MonitorFromWindow(_this->m_hWnd, MONITOR_DEFAULTTONULL);
	CWnd *parent = _this->GetParent();
	if(!parent)
		return NULL;
	HMONITOR mon2 = ::MonitorFromWindow(parent->m_hWnd, MONITOR_DEFAULTTONULL);

	if(mon1 && mon2 && (mon1 == mon2))
		return mon1;
	else
		return NULL;
}

/*
 * load the registry data stored by StoreWindowPosition()
 * returns true if data have been found in the registry
 */

bool cdxCDynamicWndEx::RestoreWindowPosition(LPCTSTR lpszProfile, UINT restoreFlags)
{
	if(!IsWindow() || !lpszProfile || !*lpszProfile)
	{
		ASSERT(false);
		return false;
	}

	VADEBUGPRINT("#DPI cdxCDynamicWndEx::RestoreWindowPosition");

	CWnd		*pWnd	=	Window();
	CWinApp	*app	=	AfxGetApp();

	if(!app->m_pszRegistryKey || !*app->m_pszRegistryKey)
	{
		TRACE(_T("*** NOTE[cdxCDynamicWndEx::RestoreWindowPosition()]: To properly store and restore a window's position, please call CWinApp::SetRegistryKey() in you app's InitInstance() !\n"));
		return false;
	}

	//
	// first, we check whether the position had been saved successful any time before
	//

	if( app->GetProfileInt(lpszProfile,lpszRegVal_Valid,REGVAL_INVALID) != REGVAL_VALID )
		return false;

	//
	// get old position
	//

#if !defined(WIN32_WCE)
	WINDOWPLACEMENT	wpl;
	VERIFY( pWnd->GetWindowPlacement(&wpl) );
#else
	RECT r;
	pWnd->GetWindowRect(&r);
#endif

	//
	// read registry
	//

	UINT iState	=	app->GetProfileInt(lpszProfile,	lpszRegVal_State, REGVAL_NOSTATE);

	//
	// get window's previous normal position
	//

#if !defined(WIN32_WCE)
	wpl.rcNormalPosition.left		=	(LONG)app->GetProfileInt(lpszProfile,	lpszRegVal_Left,		wpl.rcNormalPosition.left);
	wpl.rcNormalPosition.right		= (LONG)app->GetProfileInt(lpszProfile,	lpszRegVal_Right,		wpl.rcNormalPosition.right);
	wpl.rcNormalPosition.top		= (LONG)app->GetProfileInt(lpszProfile,	lpszRegVal_Top,		wpl.rcNormalPosition.top);
	wpl.rcNormalPosition.bottom	= (LONG)app->GetProfileInt(lpszProfile,	lpszRegVal_Bottom,	wpl.rcNormalPosition.bottom);

	auto stickToMonitor = [&]()
	{
		bool same_monitor = !!app->GetProfileInt(lpszProfile, lpszRegVal_SameMonitor, 0);
		if (same_monitor)
		{
			CWnd* parent = Window()->GetParent();
			MONITORINFO mi;
			memset(&mi, 0, sizeof(mi));
			mi.cbSize = sizeof(mi);
			if (parent)
				::GetMonitorInfo(::MonitorFromWindow(parent->m_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);

			::OffsetRect(&wpl.rcNormalPosition, mi.rcMonitor.left, mi.rcMonitor.top);
		}
	};
	
	if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		if (Psettings->mDialogsStickToMonitor)
			stickToMonitor();

		auto helper = VsUI::DpiHelper::GetScreenHelper(&wpl.rcNormalPosition);
		if (helper)
		{
			helper->LogicalToDeviceUnits(&wpl.rcNormalPosition, (LPPOINT)&wpl.rcNormalPosition);
		}
	}
	else
	{
		VsUI::DpiHelper::LogicalToDeviceUnits(&wpl.rcNormalPosition);

		if (Psettings->mDialogsStickToMonitor)
			stickToMonitor();
	}

	m_restoreRect = wpl.rcNormalPosition;

	if(Psettings->mDialogsFitIntoScreen)
	{
		HMONITOR monitor = ::MonitorFromRect(&wpl.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
		if(monitor)
		{
			MONITORINFO mi;
			memset(&mi, 0, sizeof(mi));
			mi.cbSize = sizeof(mi);
			::GetMonitorInfo(monitor, &mi);

			if(wpl.rcNormalPosition.right > mi.rcWork.right)
				::OffsetRect(&wpl.rcNormalPosition, mi.rcWork.right - wpl.rcNormalPosition.right, 0);
			if(wpl.rcNormalPosition.bottom > mi.rcWork.bottom)
				::OffsetRect(&wpl.rcNormalPosition, 0, mi.rcWork.bottom - wpl.rcNormalPosition.bottom);
			if(wpl.rcNormalPosition.left < mi.rcWork.left)
				::OffsetRect(&wpl.rcNormalPosition, mi.rcWork.left - wpl.rcNormalPosition.left, 0);
			if(wpl.rcNormalPosition.top < mi.rcWork.top)
				::OffsetRect(&wpl.rcNormalPosition, 0, mi.rcWork.top - wpl.rcNormalPosition.top);
		}
	}

	if(wpl.rcNormalPosition.left > wpl.rcNormalPosition.right)
	{
		long	l	=	wpl.rcNormalPosition.right;
		wpl.rcNormalPosition.right	=	wpl.rcNormalPosition.left;
		wpl.rcNormalPosition.left	=	l;
	}
	if(wpl.rcNormalPosition.top > wpl.rcNormalPosition.bottom)
	{
		long	l	=	wpl.rcNormalPosition.bottom;
		wpl.rcNormalPosition.bottom	=	wpl.rcNormalPosition.top;
		wpl.rcNormalPosition.top	=	l;
	}
#else
	r.left		=	app->GetProfileInt(lpszProfile,	lpszRegVal_Left,		r.left);
	r.right		=	app->GetProfileInt(lpszProfile,	lpszRegVal_Right,		r.right);
	r.top		=	app->GetProfileInt(lpszProfile,	lpszRegVal_Top,		r.top);
	r.bottom	=	app->GetProfileInt(lpszProfile,	lpszRegVal_Bottom,	r.bottom);

	if(r.left > r.right)
	{
		long	l	=	r.right;
		r.right	=	r.left;
		r.left	=	l;
	}
	if(r.top > r.bottom)
	{
		long	l	=	r.bottom;
		r.bottom	=	r.top;
		r.top	=	l;
	}
#endif

	//
	// get restore stuff
	//

	UINT	showCmd	=	SW_SHOWNA;
	
	if(restoreFlags & rflg_state)
	{
		if(iState == REGVAL_MAXIMIZED)
			showCmd	=	SW_MAXIMIZE;
		else
			if(iState == REGVAL_ICONIC)
				showCmd	=	SW_MINIMIZE;
	}

	bool nopos = CheckIfWindowIsOutOfScreen(wpl.rcNormalPosition);

	//
	// use MoveWindow() which takes care of WM_GETMINMAXINFO
	//

#if !defined(WIN32_WCE)
	if(!nosize) {
		if(nopos) {
			CRect rect;
			pWnd->GetWindowRect(rect);
			pWnd->MoveWindow(rect.left, rect.top,
				wpl.rcNormalPosition.right - wpl.rcNormalPosition.left,
				wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top,
				showCmd == SW_SHOWNA);
		} else {
			pWnd->MoveWindow(wpl.rcNormalPosition.left, wpl.rcNormalPosition.top,
				wpl.rcNormalPosition.right - wpl.rcNormalPosition.left,
				wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top,
				showCmd == SW_SHOWNA);
		}
	} else if(!nopos) {
		pWnd->SetWindowPos(NULL, wpl.rcNormalPosition.left, wpl.rcNormalPosition.top, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOZORDER);
	}
#else
	if(!nosize) {
		pWnd->MoveWindow(	r.left,r.top,
			r.right - r.left,
			r.bottom - r.top,
			showCmd == SW_SHOWNA);

	} else {
		// TODO!!!
	}
#endif

	if((!nosize) && (showCmd != SW_SHOWNA))
	{
		// read updated position

#if !defined(WIN32_WCE)
		VERIFY( pWnd->GetWindowPlacement(&wpl) );
		wpl.showCmd	=	showCmd;
		pWnd->SetWindowPlacement(&wpl);
#else
		pWnd->GetWindowRect(&r);
#endif
	}
	
	//
	// get visiblity
	//

	if(restoreFlags & rflg_visibility)
	{
		UINT i = app->GetProfileInt(lpszProfile,	lpszRegVal_Visible, REGVAL_NOSTATE);
		if(i == REGVAL_VISIBLE)
			pWnd->ShowWindow(SW_SHOW);
		else
			if(i == REGVAL_HIDDEN)
				pWnd->ShowWindow(SW_HIDE);
	}

	return true;
}

BOOL CALLBACK cdxCDynamicWndEx::MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	(void)hMonitor;
	(void)hdcMonitor;

	pair<CRect, int> &params = *(pair<CRect, int> *)dwData;

	if(lprcMonitor) {
		CRect rect;
		CRect mrect = *lprcMonitor;
		mrect.NormalizeRect();
		if(rect.IntersectRect(&params.first, &mrect)) {
			params.second += rect.Width() * rect.Height();
		}
	}
	return true;
}

bool cdxCDynamicWndEx::CheckIfWindowIsOutOfScreen(const CRect &rect) {
	pair<CRect, int> params = make_pair(rect, 0);
	params.first.NormalizeRect();

	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&params);

	return params.second < (rect.Width() * rect.Height() / 3);
}

