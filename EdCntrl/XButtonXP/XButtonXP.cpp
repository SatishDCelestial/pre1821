// XButtonXP.cpp  Version 1.3
//
// Author:  Hans Dietrich
//          hdietrich@gmail.com
//
// Description:
//     XButtonXP implements CXButtonXP, an ownerdraw button that is XP
//     theme-aware and may be used as a pushbutton or a toggle button.
//     An icon and/or text may be displayed on the button.  Please see
//     article at www.codeproject.com.
//
//     This code is based partially on Ewan Ward's article
//     "Native Win32 Theme aware Owner-draw Controls without MFC"
//     http://www.codeproject.com/KB/buttons/nativewin32xpthemes.aspx
//
// History
//     Version 1.3 - 2008 January 19
//     - Fixed bug when button is released and mouse is moved, reported by 
//       rootdial.
//     - Fixed problem with drawing as default button, reported by fredwobus 
//       and programbyran.
//     - Fixed resource leak in DrawIcon(), reported by grin.
//     - Fixed transparency bug for toolbar style, reported by Pandele Florin.
//     - Fixed bug where WM_COMMAND was being sent twice when return pressed, 
//       reported by fredwobus.
//     - Implemented WM_MOUSELEAVE handler, suggested by ksk, Roman Komary. 
//       fredwobus, 630596399 and EIEN.
//     - Added multi-line support, requested by jlatorref.
//     - Added grayscale support for disabled icon, requested by programbyran.
//     - Added support for custom text and background colors, requested by 
//       PatLeCat and Alex Evans.
//
//     Version 1.2 - 2005 April 20
//     - Fixed bug with hot state, reported by ksk
//     - Added SetDrawToolbar() and GetDrawToolbar(), suggested by Dominik Reichl
//
//     Version 1.1 - 2005 March 30
//     - Fixed bug in drawing toggle button, reported by Dominik Reichl
//     - Added SetToggleState()
//     - Removed #include "XButtonXPTest.h"
//
//     Version 1.0 - 2005 March 22
//     - Initial public release
//
// Public APIs:
//          NAME                              DESCRIPTION
//     ------------------    --------------------------------------------------
//     EnableTheming()       Enable visual themes for button
//     GetBackgroundColor()  Retrieve button custom background color 
//                           (XBUTTONXP_NO_COLOR if none) 
//     GetDrawToolbar()      Get toolbar style (TRUE = use toolbar style) 
//     GetTextColor()        Retrieve button custom text color 
//                           (XBUTTONXP_NO_COLOR if none) 
//     GetToggle()           Get toggle style (TRUE = button acts like toggle) 
//     GetToggleState()      Get toggle state (TRUE = button is pressed).
//     IsThemed()            Get theming state (TRUE = visual theme is being 
//                           used) 
//     SetBackgroundColor()  Set button custom background color. Default is 
//                           XBUTTONXP_NO_COLOR 
//     SetDrawToolbar()      Set toolbar style.  Default is FALSE 
//     SetIcon()             Set icon from resource id 
//     SetIcon()             Set icon from HICON 
//     SetIconAlignment()    Set icon alignment on button.  Default is LEFT 
//     SetTextColor()        Set button custom text color. Default is 
//                           XBUTTONXP_NO_COLOR 
//     SetToggle()           Set toggle style. Default is FALSE 
//     SetToggleState()      Set toggle state.  Default is FALSE 
//
// License:
//     This software is released under the Code Project Open License (CPOL),
//     which may be found here:  http://www.codeproject.com/info/eula.aspx
//     You are free to use this software in any way you like, except that you 
//     may not sell this source code.
//
//     This software is provided "as is" with no expressed or implied warranty.
//     I accept no liability for any damage or loss of business that this 
//     software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafxed.h"
#include "XButtonXP.h"
#include "XThemeHelper.h"
#include "CreateGrayscaleIcon.h"
#include "../SaveDC.h"
#include "../ColorListControls.h"
#include "../DevShellAttributes.h"
#include "vsshell100.h"
#include "../IdeSettings.h"
#include "../VaService.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifndef __noop
#if _MSC_VER < 1300
#define __noop ((void)0)
#endif
#endif

#undef TRACE
#define TRACE __noop



//=============================================================================
// if you want to see the TRACE output, uncomment this line:
//#include "XTrace.h"
//=============================================================================


//=============================================================================	
// various defines that are needed
//=============================================================================	
#ifndef ODS_NOFOCUSRECT
#define ODS_NOFOCUSRECT     0x0200		// from WinUser.h
#endif

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)    ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)    ((int)(short)HIWORD(lp))
#endif

#ifndef WM_THEMECHANGED
#define WM_THEMECHANGED     0x031A
#endif

//=============================================================================	
#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(CXButtonXP, COddButton)
//=============================================================================	
	//{{AFX_MSG_MAP(CXButtonXP)
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
END_MESSAGE_MAP()
#pragma warning(pop)

//=============================================================================	
CXButtonXP::CXButtonXP()
//=============================================================================	
{
	m_bMouseOverButton = false;
	m_bFirstTime       = true;
	m_bIsToggle        = false;
	m_bToggled         = false;
	m_bLButtonDown     = false;
	m_bSent            = false;
	m_bEnableTheming   = true;
	m_bDrawToolbar     = false;
	m_hTheme           = nullptr;
	m_hIcon            = nullptr;
	m_hGrayIcon        = nullptr;
	m_eIconAlignment   = LEFT;
	m_pOldParentBitmap = nullptr;
	m_crBackground     = XBUTTONXP_NO_COLOR;
	m_crText           = XBUTTONXP_NO_COLOR;
	m_bNoFocusRect     = false;

	// force init of static theme state
	ThemeHelper.IsAppThemed();
}

//=============================================================================	
CXButtonXP::~CXButtonXP()
//=============================================================================	
{
	if (m_hTheme)
		ThemeHelper.CloseThemeData(m_hTheme);
	m_hTheme = NULL;

	if (m_hIcon)
		DestroyIcon(m_hIcon);
	m_hIcon = NULL;

	if (m_hGrayIcon)
		DestroyIcon(m_hGrayIcon);
	m_hGrayIcon = NULL;

	if (m_pOldParentBitmap && m_dcParent.GetSafeHdc())
		m_dcParent.SelectObject(m_pOldParentBitmap);
	m_pOldParentBitmap = NULL;

	if (m_dcParent.GetSafeHdc())
		m_dcParent.DeleteDC();

	if (m_bmpParent.GetSafeHandle())
		m_bmpParent.DeleteObject();
}

//=============================================================================	
bool CXButtonXP::IsThemed()
//=============================================================================	
{
	BOOL bAppThemed = ThemeHelper.IsAppThemed();	// user can disable theming
													// for app by right-clicking
													// on exe, then clicking on
													// Properties | Compatibility |
													// Disable visual themes
	return (m_hTheme && m_bEnableTheming && bAppThemed);
}

//=============================================================================	
// Sets icon from resource id
CXButtonXP& CXButtonXP::SetIcon(UINT nIDResource, ICON_ALIGNMENT ia /*= LEFT*/)
//=============================================================================	
{
	HICON hIcon = (HICON) LoadImage(AfxGetInstanceHandle(),
								MAKEINTRESOURCE(nIDResource),
								IMAGE_ICON,
								0,
								0,
								0);

	return SetIcon(hIcon, ia);
}

//=============================================================================	
// Sets icon from HICON
CXButtonXP& CXButtonXP::SetIcon(HICON hIcon, ICON_ALIGNMENT ia /*= LEFT*/)
//=============================================================================	
{
	if (m_hIcon)
		DestroyIcon(m_hIcon);

	m_hIcon = hIcon;
	TRACE(_T("m_hIcon=%X\n"), m_hIcon);

	// create grayscale icon for disabled button
	if (m_hGrayIcon)
		DestroyIcon(m_hGrayIcon);
	m_hGrayIcon = NULL;
	if (hIcon)
		m_hGrayIcon = CreateGrayscaleIcon(hIcon);

	m_eIconAlignment = ia;

	RedrawWindow();

	return *this;
}


//=============================================================================	
//=============================================================================	
//
// PRIVATE FUNCTIONS & MESSAGE HANDLERS
//
//=============================================================================	
//=============================================================================	


//=============================================================================	
void CXButtonXP::PrepareImageRect(bool bHasText,
								  CRect& rectItem,
								  CRect& rectText,
								  bool bIsPressed,
								  bool bIsThemed,
								  DWORD dwWidth,
								  DWORD dwHeight,
								  CRect& rectImage)
//=============================================================================	
{
	rectImage.CopyRect(&rectItem);

	if (bHasText)
	{
		// image must be placed just inside the focus rect
		int nTextWidth = rectText.right - rectText.left;
		if (m_eIconAlignment == RIGHT)
		{
			rectText.right = int(nTextWidth - dwWidth - dwWidth/2);
			rectText.left  = int(dwWidth/2);
			rectImage.left = int(rectItem.right - dwWidth - dwWidth/2);
		}
		else	// icon is on the left
		{
			rectText.right = int(nTextWidth - dwWidth/2);
			rectText.left  = int(2 * dwWidth);
			rectImage.left = int(dwWidth/2);
		}
	}
	else	// no text
	{
		// center image horizontally
		int nImageWidth = rectImage.right - rectImage.left;
		rectImage.left += ((nImageWidth - dwWidth)/2);
	}

	// center image vertically
	int nImageHeight = rectImage.bottom - rectImage.top;
	rectImage.top += ((nImageHeight - dwHeight)/2);

	// if button is pressed then press image also
	if (bIsPressed && !bIsThemed)
		rectImage.OffsetRect(1, 1);
}

//=============================================================================	
void CXButtonXP::SaveParentBackground(bool only_if_position_changed)
//=============================================================================	
{
	GetWindowRect(&m_rectButton);
	if(only_if_position_changed && (m_lastButtonWindowRect == m_rectButton))
		return;
	m_lastButtonWindowRect = m_rectButton;
	CRect rectClient(m_rectButton);
	GetParent()->ScreenToClient(&rectClient);

	// repaint rect for button on parent's window without button,
	// to get the original background of the parent
	bool bVisible = !!(GetStyle() & WS_VISIBLE);
	ModifyStyle(WS_VISIBLE, 0);
	GetParent()->InvalidateRect(&rectClient);
	GetParent()->UpdateWindow();

	// get button position in parent window coordinates
	CRect rectParent;
	GetParent()->GetWindowRect(&rectParent);
	int left = m_rectButton.left - rectParent.left;
	int top = m_rectButton.top - rectParent.top;
	int w = m_rectButton.Width();
	int h = m_rectButton.Height();
	m_rectButton.left = left;
	m_rectButton.top  = top;
	m_rectButton.right = left + w;
	m_rectButton.bottom  = top + h;
	//TRACERECT(m_rectButton);

	if (m_pOldParentBitmap)
		m_dcParent.SelectObject(m_pOldParentBitmap);
	m_pOldParentBitmap = NULL;

	if (m_dcParent.GetSafeHdc())
		m_dcParent.DeleteDC();

	if (m_bmpParent.GetSafeHandle())
		m_bmpParent.DeleteObject();

	// get dc for parent's window
	CDC *pdcParent = GetParent()->GetWindowDC();
	ASSERT(pdcParent);

	if (pdcParent)
	{
		// make a copy of the bits in the parent's window that will be
		// covered by this button
		m_dcParent.CreateCompatibleDC(pdcParent);
		m_bmpParent.CreateCompatibleBitmap(pdcParent, m_rectButton.Width(), m_rectButton.Height());
		m_pOldParentBitmap = m_dcParent.SelectObject(&m_bmpParent);
		m_dcParent.BitBlt(0, 0, m_rectButton.Width(), m_rectButton.Height(),
			pdcParent, m_rectButton.left, m_rectButton.top, SRCCOPY);
		ReleaseDC(pdcParent);
	}

	// restore visibility
	if (bVisible)
	{
		ModifyStyle(0, WS_VISIBLE);
		GetParent()->UpdateWindow();
	}
}

//=============================================================================	
void CXButtonXP::DrawIcon(CDC *pDC,
						  bool bHasText,
						  CRect& rectItem,		// from LPDRAWITEMSTRUCT
						  CRect& rectText,
						  bool bIsPressed,
						  bool bIsThemed,
						  bool bIsDisabled)
//=============================================================================	
{
	if (m_hIcon)
	{
		// first get size of icon

		DWORD dwWidth = 32;		// assume 32x32
		DWORD dwHeight = 32;
		ICONINFO iconinfo;

		if (GetIconInfo(m_hIcon, &iconinfo))
		{
			CBitmap* pBitmap = CBitmap::FromHandle(iconinfo.hbmColor);
			if (pBitmap)
			{
				BITMAP bm;
				pBitmap->GetBitmap(&bm);
				dwWidth = (DWORD)bm.bmWidth;
				dwHeight = (DWORD)bm.bmHeight;
			}

			if (iconinfo.hbmColor) 
				::DeleteObject(iconinfo.hbmColor);
			if (iconinfo.hbmMask) 
				::DeleteObject(iconinfo.hbmMask);
		}

		CRect rectImage(rectItem);

		PrepareImageRect(bHasText, rectItem, rectText, bIsPressed, bIsThemed,
			dwWidth, dwHeight, rectImage);

		HICON hIcon = m_hIcon;
		int nFlags = bIsDisabled ? DSS_DISABLED : DSS_NORMAL;

		if (bIsDisabled && m_hGrayIcon)
		{
			hIcon = m_hGrayIcon;
			nFlags = DSS_NORMAL;
		}

		nFlags |= DST_ICON;

		pDC->DrawState(CPoint(rectImage.left, rectImage.top),
					   CSize(rectImage.right - rectImage.left, rectImage.bottom - rectImage.top),
					   hIcon,
					   (uint)nFlags,
					   (CBrush *) nullptr);
	}
}

//=============================================================================	
void CXButtonXP::DrawText(CDC *pDC,
						  LPCTSTR lpszText,
						  CRect& rect,
						  bool bIsPressed,
						  bool bIsThemed,
						  bool bIsDisabled)
//=============================================================================	
{
	ASSERT(pDC);
	ASSERT(lpszText);

	CRect rectDraw(rect);

	// if button is pressed then "press" title also
	if (bIsPressed && !bIsThemed)
		rectDraw.OffsetRect(1, 1);

	UINT uTextAlignment = DT_LEFT;
	if (m_hIcon == NULL)
		uTextAlignment = DT_CENTER;
	uTextAlignment |= DT_VCENTER;
	uTextAlignment |= (GetStyle() & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;

	// center text vertically (DT_VCENTER does not work if BS_MULTILINE is set)
	CRect rectText = rectDraw;
	pDC->DrawText(lpszText, -1, &rectDraw, uTextAlignment | DT_CALCRECT);

	rectDraw.OffsetRect((rectText.Width() - rectDraw.Width())/2, 
		(rectText.Height() - rectDraw.Height())/2);

	if (m_bDrawToolbar || !bIsThemed)
	{
		if (bIsDisabled)
		{
			rectDraw.OffsetRect(1, 1);
			pDC->SetTextColor(::GetSysColor(COLOR_3DHILIGHT));
			pDC->DrawText(lpszText, -1, &rectDraw, uTextAlignment);
			rectDraw.OffsetRect(-1, -1);
			pDC->SetTextColor(::GetSysColor(COLOR_3DSHADOW));
			pDC->DrawText(lpszText, -1, &rectDraw, uTextAlignment);
		}
		else
		{
			if (m_crText == XBUTTONXP_NO_COLOR)
				pDC->SetTextColor(::GetSysColor(COLOR_BTNTEXT));
			else
				pDC->SetTextColor(m_crText);
			pDC->DrawText(lpszText, -1, &rectDraw, uTextAlignment);
		}
	}
	else //if (bIsThemed)
	{
		ThemeHelper.DrawThemeText(m_hTheme, pDC->m_hDC, BP_PUSHBUTTON, 
						bIsDisabled ? PBS_DISABLED : PBS_NORMAL,
						lpszText, uTextAlignment, 0, &rectDraw);
	}
}

static void BitBltHelper(CDC &destdc, const CRect &rect, CDC *srcdc)
{
	destdc.BitBlt(0, 0, rect.Width(), rect.Height(), srcdc, 0, 0, SRCCOPY);
}

//=============================================================================	
void CXButtonXP::DrawItem(LPDRAWITEMSTRUCT lpDIS)
//=============================================================================	
{
	CDC *pDC = CDC::FromHandle(lpDIS->hDC);
	CRect rectItem = lpDIS->rcItem;
	if(rectItem.IsRectEmpty())
		return;

	// set up for double buffering
	CDC memDC;
	memDC.CreateCompatibleDC(pDC);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(pDC, rectItem.Width(), rectItem.Height());
	CBitmap *pOldBitmap = memDC.SelectObject(&bmp);
	// initialize dc from existing dc
	memDC.BitBlt(0, 0, rectItem.Width(), rectItem.Height(),
		pDC, 0, 0, SRCCOPY);			
	memDC.SetBkMode(TRANSPARENT);

	if (m_bFirstTime)
	{
		// open theme for Button

		m_bFirstTime = FALSE;

		if (m_hTheme)
		{
			ThemeHelper.CloseThemeData(m_hTheme);
			m_hTheme = NULL;
		}

		if (m_bDrawToolbar)
		{
			m_hTheme = ThemeHelper.OpenThemeData(m_hWnd, _T("Toolbar"));
		}
		else
		{
			m_hTheme = ThemeHelper.OpenThemeData(m_hWnd, _T("Button"));
		}

		SaveParentBackground(false);
	}
	else if(IsVS2010DrawingEnabled())
		SaveParentBackground(true);

	CFont *pFont = GetParent()->GetFont();

	if (pFont)
		memDC.SelectObject(pFont);

	// button state

	// get the button's title
	CString strTitle = _T("");
	GetWindowText(strTitle);
	bool bHasText       = !strTitle.IsEmpty();
	bool bIsFocused     = (lpDIS->itemState & ODS_FOCUS) && !m_bNoFocusRect;
	bool bIsDisabled    = !!(lpDIS->itemState & ODS_DISABLED);
	bool bDrawFocusRect = !(lpDIS->itemState & ODS_NOFOCUSRECT) && !m_bNoFocusRect;
	bool bIsPressed     = !!(lpDIS->itemState & ODS_SELECTED);
	bool bIsDefault     = IsDefault();
	bool bIsThemed      = IsThemed();
	bool bCustomColors  = (m_crBackground != XBUTTONXP_NO_COLOR) ||
						  (m_crText != XBUTTONXP_NO_COLOR);

	if (bCustomColors)
	{
		bIsThemed = false;
	}

	if (m_bIsToggle)
	{
		if (m_bLButtonDown && !m_bMouseOverButton)
		{
			// left mouse button is down outside button
			bIsPressed = !m_bToggled;
		}
		else
		{
			bIsPressed = m_bToggled;
		}
	}

	if (m_bDrawToolbar)
	{
		// restore parent window bits
		memDC.BitBlt(0, 0, m_rectButton.Width(), m_rectButton.Height(),
			&m_dcParent, 0, 0, SRCCOPY);

		bIsFocused = false;
		bIsDefault = false;
	}

	if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()) && 
		CVS2010Colours::IsVS2010NavBarColouringActive() 
#if !defined(RAD_STUDIO)
	    && gPkgServiceProvider
#endif
		)
	{
		COLORREF clr;
		if(bIsPressed)
			clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBackgroundBegin", false);
		else if(m_bMouseOverButton)
			clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundBegin", false);
		else
			clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
		memDC.FillSolidRect(rectItem, clr);
	}
	else if (bIsThemed && !(IsVS2010DrawingEnabled() && (bIsPressed || m_bMouseOverButton)))
	{
		DWORD state = bIsPressed ? PBS_PRESSED : PBS_NORMAL;

		if (bIsDisabled)
		{
			state = PBS_DISABLED;
		}
		else if (state == PBS_NORMAL)
		{
			if (bIsFocused || bIsDefault)
			{
				state = PBS_DEFAULTED;
			}
			if (m_bMouseOverButton)
				state = PBS_HOT;
		}

//		ThemeHelper.DrawThemeParentBackground(::GetParent(m_hWnd), memDC.m_hDC, &rectItem);
		ThemeHelper.DrawThemeParentBackground(m_hWnd, memDC.m_hDC, &rectItem);

		ThemeHelper.DrawThemeBackground(m_hTheme, memDC.m_hDC, BP_PUSHBUTTON, (int)state, &rectItem, NULL);
	}
	else	// not themed
	{
		if(!IsVS2010DrawingEnabled() || !bIsThemed)
		{
			CBrush brBackground;
			if (m_crBackground == XBUTTONXP_NO_COLOR)
				brBackground.CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
			else
				brBackground.CreateSolidBrush(m_crBackground);

			if (!m_bDrawToolbar || bCustomColors)
			{
				// fill in background, if custom color or not toolbar
				CRect rectClient(rectItem);
				InflateRect(&rectClient, -1, -1);
				memDC.FillRect(&rectClient, &brBackground);
			}

			if (bIsFocused || bIsDefault)
			{
				if (!m_bIsToggle || !bIsPressed)
				{
					CBrush brush(RGB(0,0,0));
					memDC.FrameRect(&rectItem, &brush);
				}
				InflateRect(&rectItem, -1, -1);
			}

			// draw the traditional pushbutton edge
			if (bIsPressed)
			{
				memDC.DrawEdge(&rectItem, EDGE_SUNKEN, BF_RECT);
			}
			else if (!m_bDrawToolbar)
			{
				memDC.DrawEdge(&rectItem, EDGE_RAISED, BF_RECT);
			}
		}
		else
		{
			rectItem.InflateRect(-1, -1);
			CSaveDC savedc(memDC);
//			bool do_draw_border = false;
			if(bIsPressed)
			{
				const std::pair<double, COLORREF> gradients[] = {
					std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEDOWN_BACKGROUND)),
					std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEDOWN_BACKGROUND)),
				};
				const COLORREF border = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BORDER);
				DrawVS2010Selection(memDC, rectItem, gradients, countof(gradients), border,
					std::bind(BitBltHelper, std::placeholders::_1, std::placeholders::_2, &m_dcParent));
			}
			else if(m_bMouseOverButton)
			{
				const std::pair<double, COLORREF> gradients[] = {
					std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_BEGIN)),
					std::make_pair(0.49, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE1)),
					std::make_pair(0.5, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE2)),
					std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_END)),
				};
				const COLORREF border = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BORDER);
				DrawVS2010Selection(memDC, rectItem, gradients, countof(gradients), border,
					std::bind(BitBltHelper, std::placeholders::_1, std::placeholders::_2, &m_dcParent));
			}
			rectItem.InflateRect(1, 1);
		}
	}

	CRect rectDraw = lpDIS->rcItem;

	// draw the icon
	DrawIcon(&memDC, bHasText, rectItem, rectDraw, bIsPressed, bIsThemed, 
		bIsDisabled);

	// draw the button title (if any)
	if (bHasText)
	{
		DrawText(&memDC, strTitle, rectDraw, bIsPressed, bIsThemed, 
			bIsDisabled);
	}

	// draw the focus rect
	if (bIsFocused && bDrawFocusRect)
	{
		CRect rectFocus = rectItem;
		rectFocus.InflateRect(-3, -3);
		memDC.DrawFocusRect(&rectFocus);
	}

	// end double buffering
	pDC->BitBlt(0, 0, rectItem.Width(), rectItem.Height(),
		&memDC, 0, 0, SRCCOPY);			

	// swap back the original bitmap
	if (pOldBitmap)
		memDC.SelectObject(pOldBitmap);
	if (bmp.GetSafeHandle())
		bmp.DeleteObject();

	memDC.DeleteDC();
}

//=============================================================================	
LRESULT CXButtonXP::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
//=============================================================================	
{
	switch (message)
	{
		case WM_GETDLGCODE:
		{
			if (lParam)
			{
				MSG *pMsg = (MSG *) lParam;
				if (pMsg)
				{
					if ((pMsg->message == WM_KEYDOWN) && (pMsg->wParam == VK_TAB))
					{
						// don't interfere with tab processing
						return 0;
					}
				}
			}
			return DLGC_WANTALLKEYS;
		}
		break;

		case WM_KEYDOWN:
			// ignore repeat messages if key is being held down
			if (lParam & 0x40000000)
				return 0;
			if ((wParam == VK_SPACE) || (wParam == VK_RETURN))
			{
				// convert to left button click
				SendMessage(WM_LBUTTONDOWN, 0, MAKELPARAM(1, 1));
			}
			break;

		case WM_KEYUP:
			if ((wParam == VK_SPACE) || (wParam == VK_RETURN))
			{
				// convert to left button click
				SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
			}
			break;

		case WM_LBUTTONUP:
		{
			m_bLButtonDown = FALSE;

			if (m_bSent)
			{
				SendMessage(BM_SETSTATE, 0);
				m_bSent = FALSE;

				CWnd* pParent = GetParent();
				if (pParent != NULL)
				{
					::SendMessage(pParent->m_hWnd, WM_COMMAND,
						WPARAM((BN_CLICKED<<16) | this->GetDlgCtrlID()),
						(LPARAM)this->m_hWnd);
				}
			}

			if (m_bIsToggle)
			{
				// if button is toggled and mouse button is released
				// outside of button, ignore the mouse click and restore
				// button state
				CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				ClientToScreen(&point);
				CRect rectButton;
				GetWindowRect(&rectButton);

				if (!rectButton.PtInRect(point))
				{
					m_bToggled = !m_bToggled;
					InvalidateRect(NULL);
				}
			}
		}
		break;

		case WM_LBUTTONDOWN:
		{
			m_bLButtonDown = TRUE;
			if (m_bIsToggle)
			{
				m_bToggled = !m_bToggled;
				InvalidateRect(NULL);
			}
		}
		break;

		case WM_LBUTTONDBLCLK:
		{
			if (m_bIsToggle)
			{
				// for toggle buttons, treat doubleclick as singleclick
				SendMessage(BM_SETSTATE, (WPARAM)m_bToggled);
			}
			else
			{
				SendMessage(BM_SETSTATE, 1);
				m_bSent = TRUE;
			}
			return 0;
		}

		case WM_THEMECHANGED:
		{
			if (IsThemed())
			{
				if (m_hTheme)
				{
					// when user changes themes, close current theme and re-open
					ThemeHelper.CloseThemeData(m_hTheme);
					m_hTheme = NULL;
					m_bFirstTime = TRUE;
				}
			}
		}
		break;
	}

	return COddButton::DefWindowProc(message, wParam, lParam);
}

//=============================================================================	
void CXButtonXP::OnMouseMove(UINT nFlags, CPoint point)
//=============================================================================	
{
	// check to see if mouse is over button

	CPoint pt(point);
	ClientToScreen(&pt);

	CRect rect;
	GetWindowRect(&rect);

	bool bMouseOverButton = m_bMouseOverButton;

	if (rect.PtInRect(pt))
		m_bMouseOverButton = true;
	else
		m_bMouseOverButton = false;

	if (bMouseOverButton != m_bMouseOverButton)
	{
		InvalidateRect(NULL);

		if (m_bMouseOverButton)
		{
			// mouse is now over button

			TRACKMOUSEEVENT tme;
			tme.cbSize    = sizeof(tme);
			tme.dwFlags   = TME_LEAVE;
			tme.hwndTrack = m_hWnd;
			::_TrackMouseEvent(&tme);
		}
	}

	//TRACE(_T("in CXButtonXP::OnMouseMove:  m_bMouseOverButton=%d\n"), m_bMouseOverButton);

	COddButton::OnMouseMove(nFlags, point);
}

//=============================================================================	
LRESULT CXButtonXP::OnMouseLeave(WPARAM /*wParam*/, LPARAM /*lParam*/)
//=============================================================================	
{
	TRACE(_T("in CXButtonXP::OnMouseLeave\n"));

	m_bMouseOverButton = FALSE;

	RedrawWindow();

	return 0;
}

//=============================================================================	
BOOL CXButtonXP::OnEraseBkgnd(CDC* /*pDC*/)
//=============================================================================	
{
	return TRUE;
}
