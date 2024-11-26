// EditCntrlWnd.cpp : implementation file
//

#include "stdafx.h"
#include "EditCntrlWnd.h"
#include "log.h"
#include "..\DevShellAttributes.h"
#include "../settings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// AfxFrameWnd

AfxFrameWnd::AfxFrameWnd()
{
}

AfxFrameWnd::~AfxFrameWnd()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(AfxFrameWnd, CWnd)
//{{AFX_MSG_MAP(AfxFrameWnd)
//}}AFX_MSG_MAP
ON_WM_NCHITTEST()
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// AfxFrameWnd message handlers

BOOL AfxFrameWnd::Attach(HWND attachTo)
{
	BOOL ret = CWnd::Attach(attachTo);
	m_toolmsg.hwnd = m_hWnd;
	return ret;
}

HWND AfxFrameWnd::Detach()
{
	m_toolmsg.hwnd = NULL;
	return CWnd::Detach();
}

void AfxFrameWnd::MoveWindow(LPCRECT lpRect, BOOL bRepaint)
{
	CWnd::MoveWindow(lpRect, bRepaint);
	InvalidateRect(NULL);
}

CWnd* AfxFrameWnd::SetFocus()
{
	return CWnd::SetFocus();
}

LRESULT AfxFrameWnd::OnNcHitTest(CPoint point)
{
	LRESULT ret = CWnd::OnNcHitTest(point);

	switch (ret)
	{
	case HTSIZE:
	case HTBOTTOM:
	case HTBOTTOMLEFT:
	case HTBOTTOMRIGHT:
	case HTLEFT:
	case HTRIGHT:
	case HTTOP:
	case HTTOPLEFT:
	case HTTOPRIGHT:
		if (gShellAttr->IsDevenv() && Psettings->m_enableVA && !Psettings->m_noMiniHelp)
		{
			if (!Psettings->minihelpAtTop)
				ret = HTNOWHERE;
		}
		break;
	}

	return ret;
}
