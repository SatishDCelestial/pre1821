/*|*\
|*|  File:      DimEditCtrl.cpp
|*|
|*|  By:        James R. Twine, TransactionWorks, Inc.
|*|             Copyright 2000, TransactionWorks, inc.
|*|  Date:      Thursday, September 21, 2000
|*|
|*|  Notes:     This Is The Implementation Of A "Dim Edit Control".
|*|             It Provides Visual Instructions Within The Edit
|*|             Control Itself.  It Can Be Used To Indicate Special
|*|             Properties Of A Edit Control Used On A Crowded
|*|             Interface
|*|
|*|             May Be Freely Incorporated Into Projects Of Any Type
|*|             Subject To The Following Conditions:
|*|
|*|             o This Header Must Remain In This File, And Any
|*|               Files Derived From It
|*|             o Do Not Misrepresent The Origin Of This Code
|*|               (IOW, Do Not Claim You Wrote It)
|*|
|*|             A "Mention In The Credits", Or Similar Acknowledgement,
|*|             Is *NOT* Required.  It Would Be Nice, Though! :)
\*|*/
#include "stdafxed.h"
#include "DimEditCtrl.h"
#include "IdeSettings.h"
#include "VAThemeUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define MES_UNDO _T("&Undo")
#define MES_CUT _T("Cu&t")
#define MES_COPY _T("&Copy")
#define MES_PASTE _T("&Paste")
#define MES_PASTE_RECENT _T("Pa&ste recent clipboard")
#define MES_PASTE0 _T("Paste 0")
#define MES_PASTE1 _T("Paste 1")
#define MES_PASTE2 _T("Paste 2")
#define MES_PASTE3 _T("Paste 3")
#define MES_PASTE4 _T("Paste 4")
#define MES_PASTE5 _T("Paste 5")
#define MES_PASTE6 _T("Paste 6")
#define MES_PASTE7 _T("Paste 7")
#define MES_DELETE _T("&Delete")
#define MES_SELECTALL _T("Select &All")
#define ME_SELECTALL WM_USER + 0x7000
//#define WM_PASTE0   WM_USER + 0x7001
//#define WM_PASTE1   WM_USER + 0x7002
//#define WM_PASTE2   WM_USER + 0x7003
//#define WM_PASTE3   WM_USER + 0x7004
//#define WM_PASTE4   WM_USER + 0x7005
//#define WM_PASTE5   WM_USER + 0x7006
//#define WM_PASTE6   WM_USER + 0x7007
//#define WM_PASTE7   WM_USER + 0x7008

/////////////////////////////////////////////////////////////////////////////
// CDimEditCtrl

CDimEditCtrl::CDimEditCtrl()
    : m_crDimTextColor(RGB(0x00, 0x00, 0x00)), // No "Hard" Dim Text Color
      m_bShowDimText(
          true),                               // Set The Dim Flag
                                               //	m_cRedOS( -0x40 ), 										// Set The Default Dim Offset Colors
                                               //	m_cGreenOS( -0x40 ),									// Set The Default Dim Offset Colors
                                               //	m_cBlueOS( -0x40 ),										// Set The Default Dim Offset Colors
                                               //	m_bUseDimOffset( true ),								// Use The Offset Colors
      m_iDimTextLen(0),                        // No Dim Text Set Yet
      m_bUseCustomColors(false),               // whether to use theme to draw control
      m_crCustomBG(GetSysColor(COLOR_WINDOW)), // Theme background color
      m_crCustomText(GetSysColor(COLOR_WINDOWTEXT)),               // Theme text color
      m_crCustomBorder(GetSysColor(COLOR_ACTIVEBORDER)),           // Theme border color
      m_crCustomBGDisabled(GetSysColor(COLOR_BTNFACE)),            // Theme disabled background color
      m_crCustomTextDisabled(GetSysColor(COLOR_GRAYTEXT)),         // Theme disabled text color
      m_crCustomBorderDisabled(GetSysColor(COLOR_INACTIVEBORDER)), // Theme disabled border color
      m_bNCAreaUpdated(false)                                      // Initial WM_NCCALCSIZE status
{
	m_caDimText[0] = _T('\0');         // Terminate The Buffer
	SetDimOffset(-0x40, -0x40, -0x40); // Set The Dim Offset

	return; // Done!
}

CDimEditCtrl::~CDimEditCtrl()
{
	return; // Done!
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CDimEditCtrl, CEdit)
//{{AFX_MSG_MAP(CDimEditCtrl)
ON_CONTROL_REFLECT_EX(EN_CHANGE, OnChange)
ON_CONTROL_REFLECT_EX(EN_SETFOCUS, OnSetfocus)
ON_WM_PAINT()
ON_WM_ERASEBKGND()
ON_WM_SETTINGCHANGE()
//    ON_WM_CONTEXTMENU()
//}}AFX_MSG_MAP
ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CDimEditCtrl message handlers

void CDimEditCtrl::PreSubclassWindow()
{
	CEdit::PreSubclassWindow(); // Do Default...

	SetShowDimControl(true); // Default To Show The Dim Control

	return; // Done!
}

void CDimEditCtrl::SetDimText(LPCTSTR cpDimText)
{
	if (cpDimText) // If Dim Text Specified
	{
#if (_MSC_VER < 1400)
		_tcsncpy(m_caDimText, cpDimText, DIM_TEXT_LEN); // Copy Over The Text
#else
		strcpy_s(m_caDimText, DIM_TEXT_LEN, cpDimText);
#endif
		m_caDimText[DIM_TEXT_LEN] = _T('\0');      // Enforce Termination (I Am Paranoid, I Know!)
		m_iDimTextLen = (int)_tcslen(m_caDimText); // Store Length Of The Dim Text
	}
	else // If No Dim Text
	{
		m_caDimText[0] = _T('\0'); // Just Terminate The Buffer (No Text)
		m_iDimTextLen = 0;         // No Dim Text
	}
	if (m_bShowDimText) // If Showing Any Dim Text
	{
		OnChange(); // Draw The Dim Text
	}
	return; // Done!
}

void CDimEditCtrl::SetShowDimControl(bool bShow)
{
	m_bShowDimText = bShow; // Set The Dim Flag
	if (bShow)              // If Showing Any Dim Text
	{
		DrawDimText(); // Draw The Dim Text
	}
	return; // Done!
}

BOOL CDimEditCtrl::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
                          CWnd* pParentWnd, UINT nID, CCreateContext* pContext)
{
	BOOL bCreated = CWnd::Create(lpszClassName, lpszWindowName, dwStyle, rect, pParentWnd, nID,
	                             pContext); // Try To Create Ourselves...

	if (bCreated) // If We Got Created
	{
		OnChange(); // Show The Dim Control
	}
	return (bCreated); // Return Creation Status
}

BOOL CDimEditCtrl::OnChange()
{
	int iLen = GetWindowTextLength(); // Get Control's Text Length

	if (!iLen) // If No Text
	{
		SetShowDimControl(true); // Show The Dim Text
	}
	else // If Text Now In The Control
	{
		SetShowDimControl(false); // Disable The Dim Text
	}
	return false; // Done!
}

BOOL CDimEditCtrl::OnSetfocus()
{
	if (m_bShowDimText) // If Showing Any Dim Text
	{
		DrawDimText(); // Draw The Dim Text
	}
	return false; // Done!
}

void CDimEditCtrl::OnPaint()
{
	Default(); // Do Default Control Drawing

	if (m_bShowDimText) // If Showing Any Dim Text
	{
		DrawDimText(); // Draw The Dim Text
	}
	return; // Done!
}

void CDimEditCtrl::DrawDimText(void)
{
	if (!m_iDimTextLen) // If No Dim Text
	{
		return; // Stop Here
	}

	CClientDC dcDraw(this);
	CRect rRect;
	int iState = dcDraw.SaveDC(); // Save The DC State

	GetClientRect(&rRect); // Get Drawing Area

	rRect.OffsetRect(1, 0);                // Add Sanity Space
	dcDraw.SelectObject((*GetFont()));     // Use The Control's Current Font
	dcDraw.SetTextColor(m_crDimTextColor); // Set The Text Color
	dcDraw.SetBkColor(                     // Set The Bk Color
	    m_bUseCustomColors ? (IsWindowEnabled() ? m_crCustomBG : m_crCustomBGDisabled) : GetSysColor(COLOR_WINDOW));

	dcDraw.DrawText(m_caDimText, m_iDimTextLen, &rRect,
	                (DT_CENTER | DT_VCENTER)); // Draw The Dim Text

	if (m_bUseCustomColors)
	{
		COLORREF border_color = IsWindowEnabled() ? m_crCustomBorder : m_crCustomBorderDisabled;
		ThemeUtils::FillNonClientArea(this, dcDraw.GetBkColor(), &border_color);
	}
	else
	{
		ThemeUtils::FillNonClientArea(this, dcDraw.GetBkColor());
	}

	dcDraw.RestoreDC(iState); // Restore The DC State

	return; // Done!
}

BOOL CDimEditCtrl::OnEraseBkgnd(CDC* pDC)
{
	BOOL bStatus = FALSE;
	if (m_bUseCustomColors)
	{
		CRect rRect;
		GetClientRect(&rRect); // Get Drawing Area

		COLORREF border_color = IsWindowEnabled() ? m_crCustomBorder : m_crCustomBorderDisabled;
		COLORREF bg_color = IsWindowEnabled() ? m_crCustomBG : m_crCustomBGDisabled;

		pDC->FillSolidRect(rRect, bg_color);
		ThemeUtils::FillNonClientArea(this, bg_color, &border_color);

		bStatus = TRUE;
	}
	else
	{
		ThemeUtils::FillNonClientArea(this, ::GetSysColor(COLOR_WINDOW));
		bStatus = CEdit::OnEraseBkgnd(pDC);
	}

	if ((bStatus) && (m_bShowDimText)) // If All Good, And Showing Any Dim Text
	{
		DrawDimText(); // Draw The Dim Text
	}
	return (bStatus); // Return Erase Status
}

void CDimEditCtrl::SetDimOffset(char cRedOS, char cGreenOS, char cBlueOS)
{
	COLORREF crWindow =
	    m_bUseCustomColors ? (IsWindowEnabled() ? m_crCustomBG : m_crCustomBGDisabled) : GetSysColor(COLOR_WINDOW);
	BYTE btRedOS = BYTE(GetRValue(crWindow) + cRedOS);
	BYTE btGreenOS = BYTE(GetGValue(crWindow) + cGreenOS);
	BYTE btBlueOS = BYTE(GetBValue(crWindow) + cBlueOS);

	m_bUseDimOffset = true; // Set The Flag
	m_cRedOS = cRedOS;      // Store Red Offset
	m_cGreenOS = cGreenOS;  // Store Green Offset
	m_cBlueOS = cBlueOS;    // Store Blue Offset
	m_crDimTextColor = RGB((BYTE)btRedOS, (BYTE)btGreenOS,
	                       (BYTE)btBlueOS); // Build The New Dim Color

	return; // Done!
}

void CDimEditCtrl::SetDimColor(COLORREF crColor)
{
	m_bUseDimOffset = false;               // Unset The Flag
	m_crDimTextColor = crColor;            // Set The New Dim Color
	m_cRedOS = m_cGreenOS = m_cBlueOS = 0; // No Offset

	return; // Done!
}

void CDimEditCtrl::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CEdit::OnSettingChange(uFlags, lpszSection);

	if (m_bUseDimOffset) // If Using An Offset For The Dim Color
	{
		COLORREF crWindow =
		    m_bUseCustomColors ? (IsWindowEnabled() ? m_crCustomBG : m_crCustomBGDisabled) : GetSysColor(COLOR_WINDOW);

		m_crDimTextColor = RGB(GetRValue(crWindow) + m_cRedOS, GetGValue(crWindow) + m_cGreenOS,
		                       GetBValue(crWindow) + m_cBlueOS); // Rebuild The Dim Color
	}
	return; // Done!
}

void CDimEditCtrl::AppendWindowText(LPCTSTR lpString)
{
	int nStartChar = -1;
	int nEndChar = -1;
	GetSel(nStartChar, nEndChar);
	int nLength = GetWindowTextLength();

	// If they're equal it means nothing is selected
	if (nStartChar != nEndChar)
	{
		ReplaceSel(lpString);
	}
	else if (nStartChar != nLength) // caret is somewhere in the middle of a current string
	{
		SetSel(nStartChar, nEndChar); // Start and End are equal
		ReplaceSel(lpString);
	}
	else // nothing is selected and caret is at the end so append everything
	{
		SetSel(nLength, nLength);
		ReplaceSel(lpString);
	}
}

HBRUSH CDimEditCtrl::CtlColor(CDC* pDC, UINT CtlColor)
{
	if (!m_bUseCustomColors)
		return NULL;

	if (CtlColor == CTLCOLOR_EDIT || CtlColor == CTLCOLOR_MSGBOX)
	{
		pDC->SetTextColor(m_crCustomText);
		pDC->SetBkColor(m_crCustomBG);
		return m_ctl_brushes.GetHBRUSH(m_crCustomBG);
	}
	else if (CtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetTextColor(m_crCustomTextDisabled);
		pDC->SetBkColor(m_crCustomBGDisabled);
		return m_ctl_brushes.GetHBRUSH(m_crCustomBGDisabled);
	}

	return NULL;
}

LRESULT CDimEditCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!m_bNCAreaUpdated && message == WM_NCPAINT)
	{
		// force WM_NCCALCSIZE to be applied
		SetWindowPos(NULL, 0, 0, 0, 0, SWP_ASYNCWINDOWPOS | SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE);
		return 0;
	}
	else if (message == WM_NCCALCSIZE)
	{
		m_bNCAreaUpdated = true;
		return ThemeUtils::CenteredEditNCCalcSize(this, wParam, lParam);
	}
	else if (message == WM_NCHITTEST && !(GetStyle() & ES_MULTILINE))
		return ThemeUtils::CenteredEditNCHitTest(this, wParam, lParam);
	else if (message == WM_NCLBUTTONDOWN && !(GetStyle() & ES_MULTILINE) && GetFocus() != this)
		SetFocus();

	//////////////////////////////////////////////////////////////////////////
	// overrides window's black border
	else if (message == WM_NCPAINT || message == WM_PAINT || message == WM_ERASEBKGND)
	{
		LRESULT nc_result = FALSE;

		if (m_bUseCustomColors)
		{
			nc_result = message == WM_NCPAINT ? TRUE : __super::WindowProc(message, wParam, lParam);

			COLORREF border_color = IsWindowEnabled() ? m_crCustomBorder : m_crCustomBorderDisabled;
			COLORREF bg_color = IsWindowEnabled() ? m_crCustomBG : m_crCustomBGDisabled;

			ThemeUtils::FillNonClientArea(this, bg_color, &border_color);
		}
		else
		{
			nc_result = __super::WindowProc(message, wParam, lParam);
			ThemeUtils::FillNonClientArea(this, ::GetSysColor(COLOR_WINDOW));
		}

		return nc_result;
	}

	return __super::WindowProc(message, wParam, lParam);
}

void CDimEditCtrl::SetCustomColors(COLORREF bg, COLORREF txt, COLORREF border, bool redraw /*= true*/)
{
	m_crCustomBG = bg;
	m_crCustomText = txt;
	m_crCustomBorder = border;

	if (redraw)
	{
		Invalidate();
		UpdateWindow();
	}
}

void CDimEditCtrl::SetCustomDisabledColors(COLORREF bg, COLORREF txt, COLORREF border, bool redraw /*= true*/)
{
	m_crCustomBGDisabled = bg;
	m_crCustomTextDisabled = txt;
	m_crCustomBorderDisabled = border;

	if (redraw)
	{
		Invalidate();
		UpdateWindow();
	}
}

void CDimEditCtrl::SetUseCustomColors(bool useCustomColors)
{
	m_bUseCustomColors = useCustomColors;

	if (m_bUseCustomColors)
	{
		SetWindowLong(m_hWnd, GWL_STYLE, WS_BORDER | GetWindowLong(m_hWnd, GWL_STYLE));
		ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);
	}
	else
	{
		SetWindowLong(m_hWnd, GWL_STYLE, GetWindowLong(m_hWnd, GWL_STYLE) & ~WS_BORDER);
		ModifyStyleEx(0, WS_EX_CLIENTEDGE, SWP_DRAWFRAME);
	}

	Invalidate();
	UpdateWindow();
}
