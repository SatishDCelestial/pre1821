// cdxCDynamicChildDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "cdxCDynamicDialog.h"
#include <CWndDpiAware.h>
#include "DebugStream.h"
#include "VAThemeUtils.h"
#include "DpiCookbook/VsUIDpiAwareness.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicDialog dialog
/////////////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(cdxCDynamicDialog,CDialog);

/////////////////////////////////////////////////////////////////////////////
// message map
/////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(cdxCDynamicDialog, CDialog)
	//{{AFX_MSG_MAP(cdxCDynamicDialog)
#if !defined(_WIN32_WCE)
	ON_WM_GETMINMAXINFO()
	ON_WM_PARENTNOTIFY()
	ON_WM_SIZING()
#endif
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicDialog message handlers: redirect stuff to my class :)=
/////////////////////////////////////////////////////////////////////////////

BOOL cdxCDynamicDialog::OnInitDialog() 
{
	BOOL	bOK	=	CDialog::OnInitDialog();
	DoInitWindow(*this);
	return bOK;
}

UINT cdxCDynamicDialog::GetCurrentDPI() const
{
	if (IsWindow())
		return VsUI::CDpiAwareness::GetDpiForWindow(Window()->m_hWnd);

	return 96;
}

BOOL cdxCDynamicDialog::DestroyWindow()
{
	DoOnDestroy();
	return CDialog::DestroyWindow();
}

#if !defined(_WIN32_WCE)
void cdxCDynamicDialog::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI) 
{
	CDialog::OnGetMinMaxInfo(lpMMI);
	DoOnGetMinMaxInfo(lpMMI);
}
#endif

void cdxCDynamicDialog::OnDestroy() 
{
	DoOnDestroy();
	CDialog::OnDestroy();
}

#if !defined(_WIN32_WCE)
void cdxCDynamicDialog::OnParentNotify(UINT message, LPARAM lParam) 
{
	CDialog::OnParentNotify(message, lParam);
	DoOnParentNotify(message, lParam);
}
#endif

void cdxCDynamicDialog::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);
	DoOnSize(nType, cx, cy);
}

#if !defined(_WIN32_WCE)
void cdxCDynamicDialog::OnSizing(UINT fwSide, LPRECT pRect) 
{
	CDialog::OnSizing(fwSide, pRect);
	DoOnSizing(fwSide, pRect);
}
#endif

void cdxCDynamicDialog::OnTimer(UINT_PTR idEvent)
{
	CDialog::OnTimer(idEvent);
	DoOnTimer(idEvent);
}

LRESULT cdxCDynamicDialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_DPICHANGED)
	{
		if (m_initialized)
		{
			UINT dpi = GetCurrentDPI();
			if (dpi != m_previousDpi)
			{
				DoOnDPIChange(message, m_previousDpi, dpi);
				m_previousDpi = dpi;
			}
		}
		else
		{
			if (!m_previousDpi)
				m_previousDpi = LOWORD(wParam);
			
			return 0;
		}
	}
	else if (message == WM_DPICHANGED_BEFOREPARENT || message == WM_DPICHANGED_AFTERPARENT)
	{
		UINT dpi = GetCurrentDPI();
		if (dpi != m_previousDpi)
		{
			DoOnDPIChange(message, m_previousDpi, dpi);
			m_previousDpi = dpi;
		}
		return 0;
	}

	LRESULT rslt = CDialog::WindowProc(message, wParam, lParam);

	if (message == WM_INITDIALOG)
	{
		if (m_previousDpi)
		{
			UINT dpi = GetCurrentDPI();
			if (dpi != m_previousDpi)
			{
				CRect rect = m_restoreRect;

				if (!rect.IsRectEmpty())
				{
					// WM_DPICHANGED will resize child controls and set their fonts
					// Since we pass current rectangle, it SHOULD not resize the dialog
					// but since it does not work properly all time, we then use SetWindowPos for sure
					DefWindowProc(WM_DPICHANGED, MAKEWORD(m_previousDpi, m_previousDpi), (LPARAM)& rect);
					DoOnDPIChange(WM_DPICHANGED, m_previousDpi, dpi);
					SetWindowPos(nullptr, 0, 0, rect.Width(), rect.Height(), SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE);

				}

				m_previousDpi = dpi;
			}
		}
		else
		{
			m_previousDpi = GetCurrentDPI();
		}

		m_initialized = true;
	}

	return rslt;
}

/////////////////////////////////////////////////////////////////////////////
// cdxCDynamicChildDlg dialog
/////////////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(cdxCDynamicChildDlg,cdxCDynamicDialog);

/////////////////////////////////////////////////////////////////////////////
// message map
/////////////////////////////////////////////////////////////////////////////

