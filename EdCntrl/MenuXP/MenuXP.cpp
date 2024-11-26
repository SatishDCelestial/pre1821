///////////////////////////////////////////////////////////////////////////////
//
// MenuXP.cpp : implementation file
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "MenuXP.h"
#include "Tools.h"
#include "Draw.h"
#include "../FontSettings.h"
#include "../WindowUtils.h"
#include "../ColorListControls.h"
#include <functional>
using namespace std::placeholders;
#include "../DevShellAttributes.h"
#include "vsshell100.h"
#include "../VAAutomation.h"
#include "../FILE.H"
#include "../ImageListManager.h"
#include "../IdeSettings.h"
#include "StringUtils.h"
#include "ArgToolTip.h"
#include "VAThemeUtils.h"
#include "WindowsHooks.h"
#include "TextOutDc.h"

// #VaTheming_MenuIcons
#define THEME_MENU_ICONS 1

#define countof(x) (sizeof((x)) / sizeof((x)[0]))

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;

#endif

//#ifndef DWORD_PTR
//    typedef ULONG DWORD_PTR;
//    typedef long LONG_PTR;
//#endif

#ifndef ODS_HOTLIGHT
    #define ODS_HOTLIGHT 0x0040
#endif
#ifndef ODS_INACTIVE
    #define ODS_INACTIVE 0x0080
#endif

#ifndef ODS_NOACCEL
    #define ODS_NOACCEL 0x0100
#endif

#ifndef DT_HIDEPREFIX
    #define DT_HIDEPREFIX 0x00100000
#endif

#ifndef SPI_GETKEYBOARDCUES
    #define SPI_GETKEYBOARDCUES 0x100A
#endif

// From <winuser.h>
#ifndef OBM_CHECK
    #define OBM_CHECK 32760
#endif

#pragma region UndocumentedStuff
////////////////////////////////////
// menu OS theme specific messages
#define WM_UAHDESTROYWINDOW 0x0090
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092
#define WM_UAHINITMENU 0x0093
#define WM_UAHMEASUREMENUITEM 0x0094
#define WM_UAHNCPAINTMENUPOPUP 0x0095
	
////////////////////////////////////
// menu window messages
#define MN_SETHMENU 0x01E0
//#define MN_GETHMENU 0x01E1	// known already
#define MN_SIZEWINDOW 0x01E2
#define MN_OPENHIERARCHY 0x01E3
#define MN_CLOSEHIERARCHY 0x01E4
#define MN_SELECTITEM 0x01E5
#define MN_CANCELMENUS 0x01E6
#define MN_SELECTFIRSTVALIDITEM 0x01E7
#define MN_GETPPOPUPMENU 0x01EA
#define MN_FINDMENUWINDOWFROMPOINT 0x01EB
#define MN_SHOWPOPUPWINDOW 0x01EC
#define MN_BUTTONDOWN 0x01ED
#define MN_MOUSEMOVE 0x01EE
#define MN_BUTTONUP 0x01EF
#define MN_SETTIMERTOOPENHIERARCHY 0x01F0
#define MN_DBLCLK 0x01F1
#define MN_ENDMENU_1 0x01F2		// MN_ENDMENU was twice, so I defined them with index 
#define MN_DODRAGDROP 0x01F3
#define MN_ENDMENU_2 0x01F4		// MN_ENDMENU was twice, so I defined them with index 

////////////////////////////////////
// menu timers
#define MF_ALLSTATE         0x00FF
#define MF_MAINMENU         0xFFFF
#define IDSYS_MNANIMATE     0xFFFBL

////////////////////////////////////
// menu special indexes 
// for items or windows in hierarchy
#define MFMWFP_OFFMENU      0
#define MFMWFP_MAINMENU     0x0000FFFF
#define MFMWFP_NOITEM       0xFFFFFFFF
#define MFMWFP_UPARROW      0xFFFFFFFD
#define MFMWFP_DOWNARROW    0xFFFFFFFC
#define MFMWFP_ALTMENU      0xFFFFFFFB
#define MFMWFP_FIRSTITEM    0
#define MFMWFP_MINVALID		0xFFFFFFFC
#pragma endregion

#define MNU_EFFECTS_VAR(effects_var, default_expr) ( CMenuXPEffects::sActiveEffects ? CMenuXPEffects::sActiveEffects-> ## effects_var : default_expr )
#define IMGWIDTH MNU_EFFECTS_VAR(img_width, VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16))
#define IMGHEIGHT MNU_EFFECTS_VAR(img_height, VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(16))
#define IMGPADDING MNU_EFFECTS_VAR(img_padding, 6)
#define TEXTPADDING MNU_EFFECTS_VAR(text_padding, 8)
#define TEXTPADDING_MNUBR MNU_EFFECTS_VAR(text_padding_mnubr, 4)
#define SM_CXSHADOW 4

const TCHAR _WndPropName_OldProc[] = _T("XPWndProp_OldProc");
const TCHAR _WndPropName_MenuXP[] = _T("XPWndProp_MenuXP");

static UINT_PTR sSelectedItemId = UINT_PTR_MAX;

static const UINT_PTR MENUXP_TIMER = ::RegisterTimer("MenuXP");
static const int MENUXP_TIMER_DURATION = 10;

static CCriticalSection inside_menu_msg_cs;
static std::list<HWND> inside_menu_msg;

static HIMAGELIST g_hImgList = NULL;
static UINT g_menuDpi = 0;
static VaFont g_menuFont;

///////////////////////////////////////////////////////////////////////////////
// Menu item management class
//
class CMenuItem : protected CVS2010Colours
{
protected:
    MENUITEMINFO m_miInfo;
    CStringW     m_sCaption;
    CImgDesc     m_ImgDesc;
//     HIMAGELIST   m_hImgList;
//     int          m_nIndex;
	FormattedTextLines m_lines;

public:
    CMenuItem ();
    CMenuItem (HMENU hMenu, UINT_PTR uItem, bool fByPosition = true);
   ~CMenuItem ();

// Properties
public:
    int   GetCaption   (CStringW& sCaption) const;
    int   GetShortCut  (CStringW& sShortCut) const;
    bool  GetSeparator () const;
    bool  GetChecked   () const;
    bool  GetRadio     () const;
    bool  GetDisabled  () const;
    bool  GetDefault   () const;
    HMENU GetPopup     () const;
	UINT_PTR  GetID    () const;

// Methods
public:
    CSize GetCaptionSize  (CDC* pDC) const;
    int  GetShortCutWidth (CDC* pDC) const;
    int  GetHeight        (CDC* pDC) const;
    bool Draw             (CDC* pDC, LPCRECT pRect, bool bSelected, bool bMenuBar /*= false*/, bool bHotLight /*= false*/, bool bInactive /*= false*/, bool bNoAccel /*= false*/, bool has_submenu, bool &skip_arrow) const;

//	static void DrawMenuItemBackground(CDC &dc, const CRect &rect, const CVS2010Colours &vs2010ce, bool do_clipping = true, const CRect *override_clientrect = NULL, int additional_border_width = 0);

	////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas
////////////////////////////////////////
//	void DrawGradient     (CDC* pDC, COLORREF clrStart, COLORREF clrEnd, LPCRECT pRect) const;
////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
////////////////////////////////////////

public:
    static BYTE ms_nCheck;
    static CRect ms_rcMRUMenuBarItem;
};


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
BYTE CMenuItem::ms_nCheck = 0;
CRect CMenuItem::ms_rcMRUMenuBarItem (0, 0, 0, 0);

///////////////////////////////////////////////////////////////////////////////
CMenuItem::CMenuItem ()
{
    memset (&m_miInfo, 0, sizeof(MENUITEMINFO));
}

///////////////////////////////////////////////////////////////////////////////
CMenuItem::CMenuItem (HMENU hMenu, UINT_PTR uItem, bool fByPosition)
{
    memset (&m_miInfo, 0, sizeof(MENUITEMINFO));
    m_miInfo.cbSize = sizeof(MENUITEMINFO);
    m_miInfo.fMask = MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE|MIIM_DATA|MIIM_ID;
    VERIFY (::GetMenuItemInfo (hMenu, (UINT)uItem, fByPosition, &m_miInfo));

    if ( !(m_miInfo.fType & MFT_SEPARATOR) )
    {
        if ( m_miInfo.hSubMenu != NULL )
        {
            CMenuXP::ms_sSubMenuCaptions.Lookup (m_miInfo.hSubMenu, m_sCaption);
            CMenuXP::ms_SubMenuImages.Lookup (m_miInfo.hSubMenu, m_ImgDesc);
			CMenuXP::ms_sSubMenuLines.Lookup(m_miInfo.hSubMenu, m_lines);
		}
        else
        {
            CMenuXP::ms_sCaptions.Lookup (m_miInfo.wID, m_sCaption);
            CMenuXP::ms_Images.Lookup (m_miInfo.wID, m_ImgDesc);
			CMenuXP::ms_sLines.Lookup(m_miInfo.wID, m_lines);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
CMenuItem::~CMenuItem ()
{
}

///////////////////////////////////////////////////////////////////////////////
int CMenuItem::GetCaption (CStringW& sCaption) const
{
    ASSERT(m_miInfo.fMask & MIIM_TYPE);
    sCaption = m_sCaption;

    int nShortCutPos = sCaption.Find ('\t');

    if ( nShortCutPos != -1 )
	    sCaption = sCaption.Left (nShortCutPos);

	return sCaption.GetLength();
}

///////////////////////////////////////////////////////////////////////////////
int CMenuItem::GetShortCut (CStringW& sShortCut) const
{
    ASSERT(m_miInfo.fMask & MIIM_TYPE);
    WTString sCaption(m_sCaption);

    int nShortCutPos = sCaption.Find ('\t');

    if ( nShortCutPos == -1 )
    {
        sShortCut = "";
        return 0;
    }
    int nLength = sCaption.GetLength()-nShortCutPos-1;

    sShortCut = sCaption.Right(nLength).Wide();

    return nLength;
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuItem::GetSeparator () const
{
    ASSERT(m_miInfo.fMask & MIIM_TYPE);
    return (m_miInfo.fType & MFT_SEPARATOR) == MFT_SEPARATOR;
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuItem::GetChecked () const
{
    ASSERT(m_miInfo.fMask & MIIM_STATE);
    return (m_miInfo.fState & MFS_CHECKED) == MFS_CHECKED;
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuItem::GetRadio () const
{
    ASSERT(m_miInfo.fMask & MIIM_TYPE);
    return (m_miInfo.fType & MFT_RADIOCHECK) == MFT_RADIOCHECK;
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuItem::GetDisabled () const
{
    ASSERT(m_miInfo.fMask & MIIM_STATE);
    return (m_miInfo.fState & MFS_GRAYED) == MFS_GRAYED;
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuItem::GetDefault () const
{
    ASSERT(m_miInfo.fMask & MIIM_STATE);
    return (m_miInfo.fState & MFS_DEFAULT) == MFS_DEFAULT;
}

///////////////////////////////////////////////////////////////////////////////
HMENU CMenuItem::GetPopup () const
{
    ASSERT(m_miInfo.fMask & MIIM_SUBMENU);
    return m_miInfo.hSubMenu;
}

///////////////////////////////////////////////////////////////////////////////
UINT_PTR CMenuItem::GetID () const
{
    ASSERT(m_miInfo.fMask & MIIM_ID);
    return m_miInfo.wID;
}

///////////////////////////////////////////////////////////////////////////////
CSize CMenuItem::GetCaptionSize (CDC* pDC) const
{
    if ( GetSeparator() )
    {
        return 0;
    }
    CStringW sCaption;
    CSize sz;

    if ( GetCaption (sCaption) > 0 )
    {
//         int nPos = sCaption.Find ('&');
		CFontDC font (*pDC, g_menuFont);

//         if ( nPos >= 0 )
//         {
//             sCaption = sCaption.Left (nPos) + sCaption.Right (sCaption.GetLength()-nPos-1);
//         }

		FormattedTextLines lines;
		ParseFormattedTextMarkup(sCaption, lines);

		CRect ln;
		for (auto& item : lines)
		{
			CFormatDC fmt(*pDC,
				(item._flags & line_bold) != 0,
				(item._flags & line_italic) != 0,
				(item._flags & line_underlined) != 0);
			CDimDC dim(*pDC, (item._flags & line_dim) != 0);

			const auto& str = item._txt;
			CRect spanRc;
			::DrawTextW(*pDC, str, str.GetLength(), &spanRc, DT_LEFT | DT_CALCRECT);

			spanRc.MoveToXY(ln.right, ln.top);
			item._rc = spanRc;

			ln.right = spanRc.right;
			ln.bottom = ln.top + std::max<int>(spanRc.Height(), ln.Height());

			sz.cx = std::max<INT>(sz.cx, ln.right);
			sz.cy = ln.bottom;

			if ((item._flags & line_no_new_line))
				continue;

			// new line
			ln.left = 0;
			ln.right = 0;
			ln.top = ln.bottom;
		}

		if (m_miInfo.hSubMenu)
			CMenuXP::ms_sSubMenuLines.SetAt(m_miInfo.hSubMenu, lines);
		else
			CMenuXP::ms_sLines.SetAt(m_miInfo.wID, lines);

		sz.cy += 4; // add vert padding
	}
    return sz;
}

///////////////////////////////////////////////////////////////////////////////
int CMenuItem::GetShortCutWidth (CDC* pDC) const
{
    if ( GetSeparator() )
    {
        return 0;
    }
    CStringW sShortCut;
    int nLength = 0;

    if ( GetShortCut (sShortCut) > 0 )
    {
		CFontDC font(*pDC, g_menuFont);
        CBoldDC bold (*pDC, GetDefault());

//      nLength = pDC->GetTextExtent (sShortCut).cx;
		CSize size;
		::GetTextExtentPoint32W(*pDC, sShortCut, sShortCut.GetLength(), &size);
		nLength = size.cx;
	}
    return nLength;
}

///////////////////////////////////////////////////////////////////////////////
int CMenuItem::GetHeight (CDC* pDC) const
{
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas
///////////////////////////////////////////////////////////////////////////////
// void CMenuItem::DrawGradient(CDC* pDC, COLORREF clrStart, COLORREF clrEnd, LPCRECT pRect) const
// {
// 	CBufferDC memDC(pDC->GetSafeHdc());
// 
// 	int r = GetRValue(clrEnd)-GetRValue(clrStart);
// 	int g = GetGValue(clrEnd)-GetGValue(clrStart);
// 	int b = GetBValue(clrEnd)-GetBValue(clrStart);
// 
// 	if (!r && !g && !b)
// 	{
// 		memDC.FillSolidRect(pRect, clrStart);
// 		return;
// 	}
// 
// 	int nSteps = max(abs(r), max(abs(g), abs(b)));	
// 
// 	float fStep = (float) (pRect->right-pRect->left)/(float) nSteps;
// 	float rStep = r/(float) nSteps;
// 	float gStep = g/(float) nSteps;
// 	float bStep = b/(float) nSteps;
// 
// 	r = GetRValue(clrStart);
// 	g = GetGValue(clrStart);
// 	b = GetBValue(clrStart);
// 
// 	CRect rcFill;
// 	int iOnBand = 0;
// 	for (; iOnBand < nSteps; iOnBand++) 
// 	{
// 		rcFill.SetRect(pRect->left+(int) (iOnBand*fStep), 0, pRect->left+(int) ((iOnBand+1)*fStep), pRect->bottom);
// 		if (!rcFill.Width())
// 			continue;
// 
// 		memDC.FillSolidRect(&rcFill, RGB(r+rStep*iOnBand, g+gStep*iOnBand, b+bStep*iOnBand));
// 	}
// 	if (rcFill.right < pRect->right)
// 	{
// 		rcFill.right=pRect->right;
// 
// 		memDC.FillSolidRect(&rcFill, RGB(r+rStep*iOnBand, g+gStep*iOnBand, b+bStep*iOnBand));
// 	}
// }
///////////////////////////////////////////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
///////////////////////////////////////////////////////////////////////////////


void DrawVerticalVS2010Gradient(CDC &dc, const CRect &rect, const std::pair<double, COLORREF> *gradients, int gradients_count)
{
	switch (gradients_count)
	{
	case 3:
		if (gradients[0].second != gradients[2].second)
			break;
		[[fallthrough]];
	case 2:
		if (gradients[0].second != gradients[1].second)
			break;
		[[fallthrough]];
	case 1:
		dc.FillSolidRect(&rect, gradients[0].second);
		return;
	case 0:
		return;
	}

	std::vector<TRIVERTEX> vertices;
	for(int i = 0; i < (gradients_count - 1); i++)
	{
		TRIVERTEX v;
		v.x = rect.left;
		v.y = rect.top + (LONG)(rect.Height() * gradients[i].first);
		v.Red = COLOR16(GetRValue(gradients[i].second) << 8);
		v.Green = COLOR16(GetGValue(gradients[i].second) << 8);
		v.Blue = COLOR16(GetBValue(gradients[i].second) << 8);
		v.Alpha = 0xff/*(gradients[i].second >> 24) << 8*/;
		vertices.push_back(v);		// top left
		v.x = rect.right;
		v.y = rect.top + (LONG)(rect.Height() * gradients[i + 1].first);
		v.Red = COLOR16(GetRValue(gradients[i + 1].second) << 8);
		v.Green = COLOR16(GetGValue(gradients[i + 1].second) << 8);
		v.Blue = COLOR16(GetBValue(gradients[i + 1].second) << 8);
		v.Alpha = 0xff/*(gradients[i + 1].second >> 24) << 8*/;
		vertices.push_back(v);		// bottom right
	}
	std::vector<GRADIENT_RECT> rects;
	for(uint i = 0; i < (vertices.size() - 1); i += 2)
	{
		GRADIENT_RECT r;
		r.LowerRight = i;
		r.UpperLeft = i + 1;
		rects.push_back(r);
	}

	dc.GradientFill(&vertices.front(), (ULONG)vertices.size(), &rects.front(), (ULONG)rects.size(), GRADIENT_FILL_RECT_V);
}

void
DrawVS2010Selection(CDC &dc, const CRect &rect, const std::pair<double, COLORREF> *gradients, int gradients_count, COLORREF bordercolor, const std::function<void (CDC &, const CRect &)> &draw_background, bool offset_rgn_by_windoworg)
{
	// draw background
	// [case: 62648] square corners in dev11+
	const bool squareCorners = gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()) ? true : false;
	const int kDiameter = squareCorners ? 1 : VsUI::DpiHelper::LogicalToDeviceUnitsX(4);
	const CPoint round(kDiameter, kDiameter);
	int saved = dc.SaveDC();

	try
	{
		CRgn roundrectrgn;
		if (squareCorners)
			roundrectrgn.CreateRectRgn(rect.left, rect.top, rect.right + VsUI::DpiHelper::LogicalToDeviceUnitsX(1), rect.bottom + VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
		else
			roundrectrgn.CreateRoundRectRgn(rect.left, rect.top, rect.right + VsUI::DpiHelper::LogicalToDeviceUnitsX(1), rect.bottom + VsUI::DpiHelper::LogicalToDeviceUnitsY(1), round.x, round.y);
		CPoint windoworg(0, 0);
		if(offset_rgn_by_windoworg)
		{
			::GetWindowOrgEx(dc.m_hDC, &windoworg);		// gmit: don't use MFC here since GetWindowOrgEx sometimes fails (?!)
			roundrectrgn.OffsetRgn(-windoworg);
		}
		dc.SelectObject(&roundrectrgn);
		DrawVerticalVS2010Gradient(dc, rect, gradients, gradients_count);
	
		// draw background outside selection (round border corners!)
		CRgn itemrgn;
		itemrgn.CreateRectRgnIndirect(rect);
		if(offset_rgn_by_windoworg)
			itemrgn.OffsetRgn(-windoworg);
		roundrectrgn.CombineRgn(&itemrgn, &roundrectrgn, RGN_DIFF);
		dc.SelectObject(&roundrectrgn);
		draw_background(dc, rect);
	
		// draw selection rect
		dc.SelectObject(&itemrgn);
		CPen pen(PS_INSIDEFRAME, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), bordercolor);
		dc.SelectObject(&pen);
		dc.SetBkMode(TRANSPARENT);
		dc.SelectObject((HBRUSH)::GetStockObject(NULL_BRUSH));
		if (squareCorners)
			dc.Rectangle(rect);
		else
			dc.RoundRect(rect, round);
	}
	catch (CException *e)
	{
		// [case: 88222]
		e->Delete();
		VALOGERROR("ERROR: DrawVS2010Selection exception caught 1");
		_ASSERTE(!"DrawVS2010Selection exception");
	}

	dc.RestoreDC(saved);
}

void GetVS2010SelectionColours(BOOL forMenu, const std::pair<double, COLORREF> *&gradients, int &gradients_count, COLORREF &bordercolor)
{
	if (gShellAttr && (gShellAttr->IsDevenv10OrHigher() || gShellAttr->IsCppBuilder()))
	{
		static COLORREF _bordercolor;
		static std::pair<double, COLORREF> _gradients[4] = {
			std::make_pair(0., 0u),
			std::make_pair(.49, 0u),
			std::make_pair(.5, 0u),
			std::make_pair(1., 0u)
		};

		if ((gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder()) && forMenu && g_IdeSettings)
		{
			_gradients[3].second = _gradients[2].second = _gradients[1].second = _gradients[0].second = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuItemMouseOver", FALSE);
		}
		else
		{
			_gradients[0].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_BEGIN);
			_gradients[1].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE1);
			_gradients[2].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE2);
			_gradients[3].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_END);
		}

		if (gShellAttr->IsDevenv11OrHigher() && forMenu)
		{
			// [case: 62648] no border for menu selection in dev11+
			_bordercolor = _gradients[0].second;

			if (gShellAttr->IsDevenv17OrHigher() && g_IdeSettings)
			{
				auto menuItemMouseOverBG = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuItemMouseOverBorder", FALSE);
				if (menuItemMouseOverBG != UINT_MAX)
				{
					_bordercolor = menuItemMouseOverBG;				
				}
			}
		}
		else
		{
			_bordercolor = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_SELECTED_BORDER);
		}

		gradients = _gradients;
		gradients_count = countof(_gradients);
		bordercolor = _bordercolor;
	}
	else
	{
		static const std::pair<double, COLORREF> gradients_2[] = {
			std::make_pair(0, RGB(255, 252, 244)),
			std::make_pair(.49, RGB(255, 243, 205)),
			std::make_pair(.5, RGB(255, 236, 181)),
			std::make_pair(1, RGB(255, 236, 181))
		};
		static const COLORREF bordercolor_2 = RGB(229, 195, 101);
		gradients = gradients_2;
		gradients_count = countof(gradients_2);
		bordercolor = bordercolor_2;
	}
}


void DrawVS2010MenuItemBackground(CDC &dc, const CRect &rect, bool do_clipping, const CRect *override_clientrect, int additional_border_width, bool column_aware)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

	const std::pair<double, COLORREF> gradients[] = {
		std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN)),
		std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTEND))
// 		std::make_pair(0, RGB(255, 0, 0)),
// 		std::make_pair(1, RGB(0, 0, 255))
	};

	CRect clientrect;
	if(override_clientrect)
		clientrect = *override_clientrect;
	else
	{
		HWND current_menuxp_hwnd = CMenuXP::GetCurrentMenuWindow();

		clientrect = rect;
		if (current_menuxp_hwnd && ::IsWindow(current_menuxp_hwnd))
		{
			CRect altRc;
			if (::GetClientRect(current_menuxp_hwnd, altRc))
			{
				clientrect = altRc;
				if (altRc.bottom < rect.bottom)
				{
					// [case: 82270]
					clientrect.bottom = rect.bottom;
				}
			}
		}

	}
	if(column_aware)
	{
		clientrect.left = rect.left;
		clientrect.right = rect.right;
	}

	int saved = 0;
	CRgn rgn;
	if(do_clipping)
	{
		saved = dc.SaveDC();
		rgn.CreateRectRgnIndirect(rect);
		dc.SelectObject(&rgn);
	}

	CRect rect2 = clientrect;
	rect2.right = rect2.left + IMGWIDTH + VsUI::DpiHelper::LogicalToDeviceUnitsX(IMGPADDING + additional_border_width);
	dc.FillSolidRect(rect2, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_ICONBACKGROUND));
	rect2.left = rect2.right;
	rect2.right = clientrect.right;
	DrawVerticalVS2010Gradient(dc, rect2, gradients, countof(gradients));

	if(do_clipping)
		dc.RestoreDC(saved);
}

bool CMenuItem::Draw (CDC* pDC, LPCRECT pRect, bool bSelected, bool bMenuBar, bool bHotLight, bool bInactive, bool bNoAccel, bool has_submenu, bool &skip_arrow) const
{
///////////////////////////////////////////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas
///////////////////////////////////////////////////////////////////////////////
// 	static BOOL bGradient= FALSE; // AfxGetApp()->GetProfileInt("MenuXP", "ShowGradient", TRUE);
// 	static BOOL bGradientSel=AfxGetApp()->GetProfileInt("MenuEx", "ShowGradientSel", TRUE);
// 	static COLORREF crGradientStart=AfxGetApp()->GetProfileInt("MenuXP", "GradientStart", RGB(0xB4, 0xC4, 0xE4));
// 	static COLORREF crGradientEnd=AfxGetApp()->GetProfileInt("MenuXP", "GradientEnd", RGB(0xFF, 0xFF, 0xFF));
//	static COLORREF crGradientStart=AfxGetApp()->GetProfileInt("MenuXP", "GradientStart", -1);
//	static COLORREF crGradientEnd=AfxGetApp()->GetProfileInt("MenuXP", "GradientEnd", -1);
///////////////////////////////////////////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
///////////////////////////////////////////////////////////////////////////////

	// just to be sure that DC will be same as when passed
	ThemeUtils::AutoSaveRestoreDC restoreDC(*pDC);
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

    COLORREF crBackImg = CLR_NONE;
    bool bMenuBarItemSelected = false;

    if ( bMenuBar && bSelected )
    {
		ASSERT(false);  // [case: 112147] getpixel is slow
        CRect rc (pRect);
        CPenDC pen (*pDC, ::GetSysColor (COLOR_3DDKSHADOW));
        CBrushDC brush (*pDC, HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +20, 0));

        rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
        ms_rcMRUMenuBarItem = rc;
        bMenuBarItemSelected = true;
        pDC->Rectangle (rc);
        rc.left = rc.right;
        rc.right += VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
        pDC->FillSolidRect (rc, ::GetSysColor (COLOR_3DFACE));

        for ( int x = 0; x < SM_CXSHADOW; x++ )
        {
            for ( int y = ( x < 2 ) ? 2-x : 0; y < rc.Height()-x-((x>0)?1:2); y++ )
            {
                int nMakeSpec = 78+(3-(y==0?0:(y==1?(x<2?0:1):(y==2?(x<2?x:2):x))))*5;
                COLORREF cr = pDC->GetPixel (rc.right-x-1, rc.top+y+SM_CXSHADOW);
                COLORREF cr2 = RGB(((nMakeSpec * int(GetRValue(cr))) / 100),
			                       ((nMakeSpec * int(GetGValue(cr))) / 100),
			                       ((nMakeSpec * int(GetBValue(cr))) / 100));
			    pDC->SetPixel (rc.right-x-1, rc.top+y+SM_CXSHADOW, cr2);
            }
        }
    }
    else if ( bSelected || (bHotLight && !bInactive) )
    {
        COLORREF crHighLight = ::GetSysColor (COLOR_HIGHLIGHT);
		CPenDC pen (*pDC, crHighLight);
        CBrushDC brush (*pDC, crBackImg = GetDisabled() ? HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +73, 0) : HLS_TRANSFORM (crHighLight, +70, -57));

        if ( bMenuBar )
        {
            CRect rc (pRect);

            rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
            pDC->Rectangle (rc);
            rc.left = rc.right;
            rc.right += VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
            pDC->FillSolidRect (rc, ::GetSysColor (COLOR_3DFACE));
        }
        else
        {
////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas
////////////////////////////////////////
// 			if (bGradient && bGradientSel && pDC->GetDeviceCaps(BITSPIXEL) > 8)
// 			{
// 				DrawGradient(pDC,
// 					GetDisabled() ? crBackImg : ((crGradientStart == -1) ? HLS_TRANSFORM(::GetSysColor (COLOR_3DFACE), +20, 0) : crGradientStart),
// 					(crGradientEnd == -1) ? HLS_TRANSFORM(::GetSysColor (COLOR_3DFACE), +75, 0) : crGradientEnd,
// 					pRect);
// 
// 				CBrushDC brush(*pDC, CLR_NONE);
// 	            pDC->Rectangle(pRect);
// 			}
// 			else
////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
////////////////////////////////////////
			{
				// draw selected item
				if (IsVS2010ColouringActive())
				{
					// VS2010 gradient services doesn't offer gradient draw for menus, so we have to do it manually
					const std::pair<double, COLORREF> *gradients = NULL;
					int gradients_count = 0;
					COLORREF bordercolor = CLR_INVALID;

					GetVS2010SelectionColours(TRUE, gradients, gradients_count, bordercolor);

					DrawVS2010Selection(*pDC, *pRect, gradients, gradients_count, bordercolor, 
						std::bind(&DrawVS2010MenuItemBackground, _1, _2, false, (const CRect *)NULL, 0, true));
				}
				else
					pDC->Rectangle(pRect);
			}
		}
    }
    else if ( !bMenuBar )
    {
        CRect rc (pRect);

////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas
////////////////////////////////////////
// 		if (bGradient && pDC->GetDeviceCaps(BITSPIXEL) > 8)
// 		{
// 			static int nGradientWidth=AfxGetApp()->GetProfileInt("MenuXP", "GradientWidth", IMGWIDTH+IMGWIDTH-2);
// 
// 	        rc.right = rc.left+nGradientWidth;
// 			DrawGradient(pDC,
// 				(crGradientStart == -1) ? HLS_TRANSFORM(::GetSysColor (COLOR_3DFACE), +20, 0) : crGradientStart,
// 				(crGradientEnd == -1) ? HLS_TRANSFORM(::GetSysColor (COLOR_3DFACE), +75, 0) : crGradientEnd,
// 				rc);
// 
// 			rc.left = rc.right;
// 			rc.right = pRect->right;
// 			pDC->FillSolidRect (rc, (crGradientEnd == -1) ? HLS_TRANSFORM(::GetSysColor (COLOR_3DFACE), +75, 0) : crGradientEnd);
// 		}
// 		else
////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
////////////////////////////////////////
		{
			// draw normal item
			if (IsVS2010ColouringActive())
				DrawVS2010MenuItemBackground(*pDC, rc, true, NULL, 0, true);
			else
			{
				rc.right = rc.left + IMGWIDTH + VsUI::DpiHelper::LogicalToDeviceUnitsX(IMGPADDING);
				pDC->FillSolidRect (rc, HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +20, 0));
				rc.left = rc.right;
				rc.right = pRect->right;
				pDC->FillSolidRect (rc, HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +75, 0));
			}
		}
    }
    else
    {
        pDC->FillSolidRect (pRect, ::GetSysColor (COLOR_3DFACE));
    }

    if ( GetSeparator() )
    {
		COLORREF separator_colour;
		if (IsVS2010ColouringActive())
			separator_colour = GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_SEPARATOR);
		else
			separator_colour = HLS_TRANSFORM(::GetSysColor(COLOR_3DFACE), -18, 0);
        CPenDC pen (*pDC, separator_colour);

        pDC->MoveTo (pRect->left + IMGWIDTH + VsUI::DpiHelper::LogicalToDeviceUnitsX(IMGPADDING + TEXTPADDING),  (pRect->top + pRect->bottom) / 2);
        pDC->LineTo (pRect->right - VsUI::DpiHelper::LogicalToDeviceUnitsX(1), (pRect->top + pRect->bottom) / 2);
    }
    else
    {
        CRect rc (pRect);
        CStringW sCaption;

        if ( GetCaption (sCaption) > 0 )
        {
			if (IsVS2010ColouringActive())
			{
				if(bInactive || GetDisabled())
					pDC->SetTextColor(GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_INACTIVE));
				else if(bSelected)
					pDC->SetTextColor(GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_SELECTED));
				else
					pDC->SetTextColor(GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
			}
			else
				pDC->SetTextColor (bInactive ? ::GetSysColor (COLOR_3DSHADOW) : (GetDisabled() ? HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), -18, 0) : ::GetSysColor (COLOR_MENUTEXT)));
            pDC->SetBkMode (TRANSPARENT);

            BOOL bKeyboardCues = true;
            ::SystemParametersInfo (SPI_GETKEYBOARDCUES, 0, &bKeyboardCues, 0);
            DWORD dwHidePrefix = ( bNoAccel && !bKeyboardCues ) ? DT_HIDEPREFIX : 0u;
			bool syntax_hl = CVS2010Colours::IsExtendedThemeActive() && PaintType::in_WM_PAINT != PaintType::None;	// will cause coloring regardless of DT_NOPREFIX 



            if ( bMenuBar )
            {
                rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
//              pDC->DrawText (sCaption, rc, DT_SINGLELINE|DT_VCENTER|DT_CENTER|dwHidePrefix);
				::VaDrawTextW(*pDC, sCaption, rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | dwHidePrefix, syntax_hl);
			}
            else
            {
////////////////////////////////////////
// 9 Jul 2003 begin mods Kris Wojtas - proper for a "column break" option
////////////////////////////////////////
				rc.left += IMGWIDTH + VsUI::DpiHelper::LogicalToDeviceUnitsX(IMGPADDING + TEXTPADDING);
////////////////////////////////////////
// 9 Jul 2003 end mods Kris Wojtas
////////////////////////////////////////
//              pDC->DrawText (sCaption, rc, DT_SINGLELINE|DT_VCENTER|DT_LEFT|dwHidePrefix);
				{
					CSize lnSz = 0;

					FormatRenderer::DrawingContext dctx;
					dctx.dc = pDC;
					dctx.textFlags = DT_SINGLELINE | DT_VCENTER | DT_LEFT | dwHidePrefix;

					for (const auto& item : m_lines)
					{
						const auto & str = item._txt;
						if (!str.IsEmpty())
						{
							// ensures that each item starts with defaults
							ThemeUtils::AutoSaveRestoreDC restore_dc(*pDC);

							CRect centeredRc(rc);
							centeredRc.top += 2;
							centeredRc.top += item._rc.top;
							centeredRc.left += item._rc.left;
							centeredRc.bottom -= 2;

							dctx.dstRect = &centeredRc;
							
							CRect dummy = item._rc;
							dctx.rsltRect = &dummy;

							// default background drawing function
							dctx.draw_default_bg = [&](const FormattedTextLine* itemP)
							{
								if (itemP == nullptr)
									itemP = &item;

								if (itemP->_rc.top != item._rc.top)
									centeredRc.top = rc.top + itemP->_rc.top;

								if (itemP->_rc.left != item._rc.left)
									centeredRc.left = rc.left + itemP->_rc.left;

								// handle the Background color
								if (itemP->_bg_rgba)
								{
									DWORD alpha_mask = itemP->_bg_rgba & 0xFF000000;
									if (alpha_mask)	// do nothing if 0
									{
										if (alpha_mask == 0xFF000000)
											pDC->FillSolidRect(&itemP->_rc, itemP->_bg_rgba & 0x00FFFFFF);
										else if (alpha_mask != 0)
											ThemeUtils::FillRectAlpha(*pDC, &itemP->_rc, itemP->_bg_rgba & 0x00FFFFFF, (LOBYTE((itemP->_bg_rgba) >> 24)));
									}
								}
							};

							// default foreground drawing function
							dctx.draw_default_fg = [&](const FormattedTextLine* itemP)
							{
								if (itemP == nullptr)
									itemP = &item;

								if (itemP->_rc.top != item._rc.top)
									centeredRc.top = rc.top + itemP->_rc.top;

								if (itemP->_rc.left != item._rc.left)
									centeredRc.left = rc.left + itemP->_rc.left;

								CFormatDC fmt(*pDC,
									(itemP->_flags & line_bold) != 0,
									(itemP->_flags & line_italic) != 0,
									(itemP->_flags & line_underlined) != 0);

								// handle the Foreground color (text color)
								if (itemP->_fg_rgba)
								{
									DWORD alpha_mask = itemP->_fg_rgba & 0xFF000000;

									if (alpha_mask == 0)
										return;	// transparent text
									else if (alpha_mask == 0xFF000000)
										pDC->SetTextColor(itemP->_fg_rgba & 0x00FFFFFF);
									else
									{
										// get current background color in the center of the item
										COLORREF bg_color = pDC->GetPixel(itemP->_rc.CenterPoint());

										// convert alpha byte to opacity in 0 to 1 range
										float fg_opacity = (float)LOBYTE((itemP->_fg_rgba) >> 24) / 255.0f;

										// combine colors with opacity to get resulting color
										COLORREF txt_color = ThemeUtils::InterpolateColor(bg_color, itemP->_fg_rgba & 0x00FFFFFF, fg_opacity);

										// apply to Text color
										pDC->SetTextColor(txt_color);
									}
								}

								// moved from top, so that dimming affects also FG settings
								CDimDC dim(*pDC, (itemP->_flags & line_dim) != 0);

								if (itemP->_fg_rgba || (itemP->_flags & line_dont_colour) != 0 || (itemP->_flags & line_dim) != 0)
								{
									// if FG color is set or DON'T COLOUR flag is set, or dimming is enabled
									// turn off coloring temporarily and draw text with active Text color

									int tmp = PaintType::in_WM_PAINT;
									PaintType::in_WM_PAINT = PaintType::None;
									::DrawTextW(*pDC, itemP->_txt, itemP->_txt.GetLength(), centeredRc, dctx.textFlags);
									PaintType::in_WM_PAINT = tmp;
								}
								else
								{
									::VaDrawTextW(*pDC, itemP->_txt, centeredRc, dctx.textFlags, syntax_hl);
								}
							};

							FormatRenderer * fmt_custom = nullptr;

							// try to get custom renderer
							if (CMenuXPEffects::sActiveEffects)
							{
								auto & renderers = CMenuXPEffects::sActiveEffects->renderers;
								auto found = renderers.find(item._custom);
								if (found != renderers.end())
								{
									fmt_custom = &found->second;
								}
							}

							if (fmt_custom && fmt_custom->render)
							{
								dctx.fmt = &item;
								fmt_custom->render(dctx);
							}
							else
							{
								dctx.draw_default_bg(&item);
								dctx.draw_default_fg(&item);
							}
						}
					}
				}

				CBoldDC bold(*pDC, GetDefault());

                CStringW sShortCut;

                if ( GetShortCut (sShortCut) > 0 )
                {
                    rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING + 4);
//                  pDC->DrawText (sShortCut, rc, DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
					// #cppbTODO DT_RIGHT is not honored in RadStudio?? breaks alignment of shortcut in VA context menus (text display is same with or without DT_RIGHT in RS)
					::DrawTextW(*pDC, sShortCut, sShortCut.GetLength(), &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                }
                
				COLORREF checkMarkColor = CLR_INVALID;
                if ( GetChecked() )
                {
					COLORREF crHighLight;
					if (IsVS2010ColouringActive())
					{
						if (gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder())
						{
							// different checkbox style in dev12+
							if (bSelected)
 								checkMarkColor = g_IdeSettings->GetNewProjectDlgColor(L"ImageBorder", TRUE);
							else
 								checkMarkColor = g_IdeSettings->GetNewProjectDlgColor(L"CheckBox", TRUE);

							// no box; use same color as menu item background
							if (bSelected)
								crHighLight = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuItemMouseOver", FALSE);
							else
								crHighLight = g_IdeSettings->GetEnvironmentColor(L"Menu", FALSE);

							if (bSelected)
 								crBackImg = g_IdeSettings->GetNewProjectDlgColor(L"ImageBorder", FALSE);
							else
 								crBackImg = g_IdeSettings->GetNewProjectDlgColor(L"CheckBox", FALSE);
						}
						else if (gShellAttr->IsDevenv11())
						{
							// [case: 62648] different checkbox style in dev11+
							if (GetDisabled())
 								crHighLight = g_IdeSettings->GetEnvironmentColor(L"CommandBarCheckBoxDisabled", FALSE);
							else if (bSelected)
 								crHighLight = g_IdeSettings->GetEnvironmentColor(L"CommandBarCheckBoxMouseOver", FALSE);
							else
 								crHighLight = g_IdeSettings->GetEnvironmentColor(L"CommandBarCheckBox", FALSE);

							if (bSelected)
 								crBackImg = g_IdeSettings->GetNewProjectDlgColor(L"CheckBoxMouseOver", FALSE);
							else
 								crBackImg = g_IdeSettings->GetNewProjectDlgColor(L"CheckBox", FALSE);

							checkMarkColor = crHighLight;
						}
						else
						{
							crHighLight = GetVS2010Colour(VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON_BORDER);
							/*if(GetDisabled())
								crBackImg = GetVS2010Colour(VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON_BORDER);
							else*/ if(bSelected)
								crBackImg = GetVS2010Colour(VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON);
							else
								crBackImg = GetVS2010Colour(VSCOLOR_COMMANDBAR_SELECTED);
						}
					}
					else
					{
						crHighLight = ::GetSysColor(COLOR_HIGHLIGHT);
						crBackImg = GetDisabled() ? HLS_TRANSFORM(::GetSysColor(COLOR_3DFACE), +73, 0) : (bSelected ? HLS_TRANSFORM(crHighLight, +50, -50) : HLS_TRANSFORM(crHighLight, +70, -57));
					}

					CPenDC pen (*pDC, crHighLight);
                    CBrushDC brush (*pDC, crBackImg);
					LONG vMid = pRect->top + (pRect->bottom - pRect->top) / 2;
                    pDC->Rectangle (CRect (pRect->left + IMGWIDTH / 4 - VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
											vMid - IMGHEIGHT / 2 + VsUI::DpiHelper::LogicalToDeviceUnitsY(1),
											pRect->left + IMGWIDTH + VsUI::DpiHelper::LogicalToDeviceUnitsX(3), 
											vMid + IMGHEIGHT / 2 + VsUI::DpiHelper::LogicalToDeviceUnitsY(1)));
                }

#if defined(_WIN64) && THEME_MENU_ICONS != 0
				auto drawImageTheme17 = [&](HIMAGELIST imgList, int index, bool bOver, bool disabled) -> bool 
				{
					if (!imgList)
						return false;

					CPoint iconPt(
					    pRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX(bOver ? 4 : 3),
					    rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(bOver ? 4 : 3));

					COLORREF bgColor = pDC->GetPixel(iconPt);

					return ThemeUtils::DrawImageThemedForBackground(*pDC, CImageList::FromHandle(g_hImgList), index, iconPt, bgColor);
				};
#endif

				bool didDraw = false;
				if(m_miInfo.hSubMenu)
				{
					// Do not draw icons for items with sub-menus, In Vista it thinks the upper bits of the handle are the icon idx.
				}
                else if ( m_ImgDesc.m_hImgList != NULL && m_ImgDesc.m_nIndex != -1 && !m_miInfo.hSubMenu)
                {
                    bool bOver = !GetDisabled() && bSelected;

					// VS2010 icons don't have shadow if selected
                    if ( GetDisabled() || (bSelected && !GetChecked() && !IsVS2010ColouringActive()) )
                    {
#if defined(_WIN64) && THEME_MENU_ICONS != 0
						didDraw = drawImageTheme17(m_ImgDesc.m_hImgList, m_ImgDesc.m_nIndex, bOver, true);
#endif 
						if (!didDraw)
						{
							HICON hIcon = ImageList_ExtractIcon(NULL, m_ImgDesc.m_hImgList, m_ImgDesc.m_nIndex);
							pDC->DrawState(CPoint(
							                   pRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX(bOver ? 4 : 3),
							                   rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(bOver ? 4 : 3)),
							               CSize(IMGWIDTH, IMGHEIGHT), hIcon, DSS_MONO,
							               CBrush(bOver ? HLS_TRANSFORM(::GetSysColor(COLOR_HIGHLIGHT), +50, -66) : HLS_TRANSFORM(::GetSysColor(COLOR_3DFACE), -27, 0)));
							DestroyIcon(hIcon);
							didDraw = true;						
						}
                    }
                    if ( !GetDisabled() )
                    {
#if defined(_WIN64) && THEME_MENU_ICONS != 0
						didDraw = drawImageTheme17(m_ImgDesc.m_hImgList, m_ImgDesc.m_nIndex, bOver, false);
#endif 
						// VS2010 icons aren't offset by 1 pixel if selected
						if (!didDraw && ::ImageList_Draw(m_ImgDesc.m_hImgList, m_ImgDesc.m_nIndex, pDC->m_hDC,
						                                 pRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX((bSelected && !GetChecked() && !IsVS2010ColouringActive()) ? 2 : 3),
						                                 rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY((bSelected && !GetChecked() && !IsVS2010ColouringActive()) ? 2 : 3),
						                                 ILD_TRANSPARENT))
						{
							didDraw = true;
						}
                    }
                }
				else if ( g_hImgList != NULL && HIWORD(m_miInfo.wID))
				{
					bool bOver = !GetDisabled() && bSelected;

					// VS2010 icons don't have shadow if selected
					if ( GetDisabled() || (bSelected && !GetChecked() && !IsVS2010ColouringActive()) )
					{
#if defined(_WIN64) && THEME_MENU_ICONS != 0
						didDraw = drawImageTheme17(g_hImgList, (int) HIWORD(m_miInfo.wID), bOver, true);
#endif 
						if (!didDraw)
						{
							HICON hIcon = ImageList_ExtractIcon(NULL, g_hImgList, HIWORD(m_miInfo.wID));
							pDC->DrawState(CPoint(
							                   pRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX(bOver ? 4 : 3),
							                   rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(bOver ? 4 : 3)),
							               CSize(IMGWIDTH, IMGHEIGHT), hIcon, DSS_MONO,
							               CBrush(bOver ? HLS_TRANSFORM(::GetSysColor(COLOR_HIGHLIGHT), +50, -66) : HLS_TRANSFORM(::GetSysColor(COLOR_3DFACE), -27, 0)));
							DestroyIcon(hIcon);
							didDraw = true;
						}
					}
					if (!GetDisabled())
					{
#if defined(_WIN64) && THEME_MENU_ICONS != 0
						didDraw = drawImageTheme17(g_hImgList, (int) HIWORD(m_miInfo.wID), bOver, false);
#endif
						// VS2010 icons aren't offset by 1 pixel if selected
						if (!didDraw && ::ImageList_Draw(g_hImgList, HIWORD(m_miInfo.wID), pDC->m_hDC,
						                                 pRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX((bSelected && !GetChecked() && !IsVS2010ColouringActive()) ? 2 : 3),
						                                 rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY((bSelected && !GetChecked() && !IsVS2010ColouringActive()) ? 2 : 3),
						                                 ILD_TRANSPARENT))
						{
							didDraw = true;
						}
					}
				}
                
				if (!didDraw && GetChecked())
                {
                    // Draw the check mark
                    rc.left  = pRect->left + (IMGWIDTH / 4);
                    rc.right = rc.left + IMGWIDTH - 2;

                    if ( GetRadio() )
                    {
                        CPoint ptCenter = rc.CenterPoint();
                        COLORREF crBullet = GetDisabled() ? HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), -27, 0) : ::GetSysColor (COLOR_MENUTEXT);
                        CPenDC pen (*pDC, crBullet);
                        CBrushDC brush (*pDC, crBullet);

                        pDC->Ellipse (CRect (ptCenter.x - VsUI::DpiHelper::LogicalToDeviceUnitsX(4), ptCenter.y - VsUI::DpiHelper::LogicalToDeviceUnitsY(3), ptCenter.x + VsUI::DpiHelper::LogicalToDeviceUnitsX(3), ptCenter.y + VsUI::DpiHelper::LogicalToDeviceUnitsY(4)));
                        pDC->SetPixel (ptCenter.x + VsUI::DpiHelper::LogicalToDeviceUnitsX(1), ptCenter.y + VsUI::DpiHelper::LogicalToDeviceUnitsX(2), crBackImg);
                    }
                    else
                    {
						rc.top = rc.top + rc.Height() / 2 - rc.Width() / 2 + VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
						rc.bottom = rc.top + rc.Width() + VsUI::DpiHelper::LogicalToDeviceUnitsY(1);

						if (checkMarkColor == CLR_INVALID)
							checkMarkColor = ::GetSysColor(COLOR_MENUTEXT);

						ThemeUtils::DrawSymbol(*pDC, rc, checkMarkColor);
                    }
                }
            }
        }

		if(!bMenuBar && has_submenu && IsVS2010ColouringActive())
		{
			skip_arrow = true;

			COLORREF clr;
			if(gShellAttr->IsDevenv10())
				clr = RGB(0, 0, 0);
			else
			{
				ASSERT(gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder());
				if(bSelected)
					clr = GetVS2010Colour(VSCOLOR_HIGHLIGHT);
				else
					clr = GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_SUBMENU_GLYPH);
			}

			CBitmap system_arrow_bitmap;
			system_arrow_bitmap.Attach((HBITMAP)::LoadImage(NULL, MAKEINTRESOURCE(OBM_MNARROW), IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
			BITMAP bm;
			memset(&bm, 0, sizeof(bm));
			system_arrow_bitmap.GetObject(sizeof(bm), &bm);
			CRect arrow_rect = *pRect;
			arrow_rect.left = arrow_rect.right - bm.bmWidth;
			if(arrow_rect.Height() > bm.bmHeight)
			{
				arrow_rect.top += (arrow_rect.Height() - bm.bmHeight) / 2;
				arrow_rect.bottom = arrow_rect.top + bm.bmHeight;
			}

			CDC arrowdc;
			arrowdc.CreateCompatibleDC(pDC);
			CDC filldc;
			filldc.CreateCompatibleDC(pDC);
			CBitmap arrowbitmap;
			arrowbitmap.Attach(::CreateCompatibleBitmap(*pDC, arrow_rect.Width(), arrow_rect.Height()));
			arrowdc.SelectObject(arrowbitmap);
			CBitmap fillbitmap;
			fillbitmap.Attach(::CreateCompatibleBitmap(*pDC, arrow_rect.Width(), arrow_rect.Height()));
			filldc.SelectObject(fillbitmap);

			arrowdc.DrawFrameControl(CRect(CPoint(0, 0), arrow_rect.Size()), DFC_MENU, DFCS_MENUARROW);
			filldc.FillSolidRect(CRect(CPoint(0, 0), arrow_rect.Size()), clr);

			pDC->BitBlt(arrow_rect.left, arrow_rect.top, arrow_rect.Width(), arrow_rect.Height(), &filldc, 0, 0, SRCINVERT);
			pDC->BitBlt(arrow_rect.left, arrow_rect.top, arrow_rect.Width(), arrow_rect.Height(), &arrowdc, 0, 0, SRCAND);
			pDC->BitBlt(arrow_rect.left, arrow_rect.top, arrow_rect.Width(), arrow_rect.Height(), &filldc, 0, 0, SRCINVERT);
		}
	}


    return bMenuBarItemSelected;
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
CMap <UINT_PTR, UINT_PTR, CStringW, CStringW&> CMenuXP::ms_sCaptions;
CMap <HMENU, HMENU, CStringW, CStringW&> CMenuXP::ms_sSubMenuCaptions;

CMap <UINT_PTR, UINT_PTR, FormattedTextLines, FormattedTextLines&> CMenuXP::ms_sLines;
CMap <HMENU, HMENU, FormattedTextLines, FormattedTextLines&> CMenuXP::ms_sSubMenuLines;

CMap <UINT_PTR, UINT_PTR, CImgDesc, CImgDesc&> CMenuXP::ms_Images;
CMap <HMENU, HMENU, CImgDesc, CImgDesc&> CMenuXP::ms_SubMenuImages;

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::SetXPLookNFeel (CWnd* pWnd, bool bXPLook)
{
    if ( bXPLook )
    {
        mySetProp (pWnd->GetSafeHwnd(), _WndPropName_MenuXP, (HANDLE)TRUE);
    }
    else
    {
        myRemoveProp (pWnd->GetSafeHwnd(), _WndPropName_MenuXP);
    }
}

HWND CMenuXP::GetDpiSource(CWnd* pWnd)
{	
	if (pWnd && pWnd->m_hWnd && ::IsWindow(pWnd->m_hWnd))
		return pWnd->m_hWnd;

	HWND focus = ::GetFocus();
	if (focus)
		return focus;

	return ::GetActiveWindow();
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuXP::GetXPLookNFeel (const CWnd* pWnd)
{
    return myGetProp (pWnd->GetSafeHwnd(), _WndPropName_MenuXP) != NULL;
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::UpdateMenuBar (CWnd* pWnd)
{
    if ( GetXPLookNFeel (pWnd) )
    {
        HMENU hMenu = pWnd->GetMenu()->GetSafeHmenu();

        if ( hMenu != NULL )
        {
            SetXPLookNFeel (pWnd, hMenu, true, true);
        }
    }
}

void CMenuXP::ClearGlobals()
{
	g_hImgList = NULL;
	g_menuFont.DeleteObject();
	g_menuDpi = 0;
}

int CMenuXP::GetItemHeight(HWND dpiSource, int *separator_height)
{
	static int cached_height = 0;
	static int cached_separator_height = 0;

	auto scope = VsUI::DpiHelper::SetDefaultForWindow(dpiSource); // just for sure

	if (S_OK == g_menuFont.UpdateForWindow(dpiSource, VaFontType::MenuFont) ||
		(int)g_menuDpi != VsUI::DpiHelper::GetDeviceDpiX() ||
		cached_height == 0)
	{
		CClientDC cDC(AfxGetMainWnd());
		CFontDC font(cDC, g_menuFont);
		TEXTMETRIC tm;
		memset(&tm, 0, sizeof(tm));
		cDC.GetTextMetrics(&tm);

		int px2 = VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		int px4 = VsUI::DpiHelper::LogicalToDeviceUnitsX(4);

		g_menuDpi = (uint)VsUI::DpiHelper::GetDeviceDpiX();

		cached_height = max<LONG>(tm.tmHeight + px2, IMGHEIGHT) + px4;
		cached_separator_height = tm.tmHeight / 2 + px2;
	}

	_ASSERTE(g_menuFont.GetDPI() == g_menuDpi);

	if(separator_height)
		*separator_height = cached_separator_height;
	return cached_height;
}

HMENU CMenuXP::GetMenu(HWND hWnd)
{
	HMENU mnu = ::GetMenu(hWnd);
	
	if (mnu)
		return mnu;

	WNDPROC wndProc = (WNDPROC)myGetProp(hWnd, _WndPropName_OldProc);

	if (wndProc == nullptr)
		wndProc = (WNDPROC)::GetWindowLongPtrW(hWnd, GWLP_WNDPROC);

	if (wndProc)
	{
		mnu = (HMENU)::CallWindowProc(wndProc, hWnd, MN_GETHMENU, 0, 0);
	}

	// Don't use SendMessage as it may cause infinite loop

	return mnu;
}

HWND CMenuXP::GetCurrentMenuWindow()
{
	CSingleLock l(&inside_menu_msg_cs, true);
	if (inside_menu_msg.size() > 0)
		return inside_menu_msg.back();
	return nullptr;
}

UINT_PTR CMenuXP::ItemFromPoint(HWND hWnd, POINT screenPt)
{
	UINT_PTR itemHit = MFMWFP_NOITEM;

	// This should be preferred way to get item as it also confirms,
	// that window under cursor is this window and not any other window in hierarchy. 
	WNDPROC wndProc = (WNDPROC)myGetProp(hWnd, _WndPropName_OldProc);

	if (wndProc == nullptr)
		wndProc = (WNDPROC)::GetWindowLongPtrW(hWnd, GWLP_WNDPROC);

	if (wndProc)
	{
		HWND hMenuWnd = (HWND)::CallWindowProc(wndProc, hWnd, MN_FINDMENUWINDOWFROMPOINT, (WPARAM)&itemHit, MAKELONG(screenPt.x, screenPt.y));
		switch ((UINT_PTR)hMenuWnd)
		{
		case MFMWFP_OFFMENU:
		case MFMWFP_NOITEM:
		case MFMWFP_ALTMENU:
			return MFMWFP_NOITEM;

		default:
			if (hMenuWnd == hWnd)
				return itemHit;
			else
				return MFMWFP_NOITEM;
		}
	}

	// Follows not so good way to get item, 
	// so while MN_FINDMENUWINDOWFROMPOINT works, use it...
	// I don't believe that MS will stop support those messages. 

	HWND wndFromPt = ::WindowFromPoint(screenPt);
	if (wndFromPt != hWnd)
		return MFMWFP_NOITEM;

	HMENU mnu = CMenuXP::GetMenu(hWnd);
	if (mnu && ::IsMenu(mnu))
		itemHit = (UINT_PTR)::MenuItemFromPoint(nullptr, mnu, screenPt);

	return itemHit;
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::SetXPLookNFeel (CWnd* pWnd, HMENU hMenu, bool bXPLook, bool bMenuBar)
{
    if ( !bXPLook )
    {
        // TODO: Remove the ownerdraw style ?
        return;
    }
//    TRACE(_T("Referenced captions : %i\n"), ms_sCaptions.GetCount()+ms_sSubMenuCaptions.GetCount());
    // Clean up old references...
    // ... for captions
    POSITION pos = ms_sSubMenuCaptions.GetStartPosition();

    while ( pos != NULL )
    {
        HMENU hSubMenu;
        CStringW sBuff;

        ms_sSubMenuCaptions.GetNextAssoc (pos, hSubMenu, sBuff);

        if ( !::IsMenu (hSubMenu) )
        {
            ms_sSubMenuCaptions.RemoveKey (hSubMenu);
        }
    }
    // ... for images
    pos = ms_SubMenuImages.GetStartPosition();

    while ( pos != NULL )
    {
        HMENU hSubMenu;
        CImgDesc ImgDesc;

        ms_SubMenuImages.GetNextAssoc (pos, hSubMenu, ImgDesc);

        if ( !::IsMenu (hSubMenu) )
        {
            ms_SubMenuImages.RemoveKey (hSubMenu);
        }
    }
    ASSERT(hMenu != NULL);
    int nItemCount = ::GetMenuItemCount (hMenu);
    MENUITEMINFOW mii = { sizeof MENUITEMINFOW, MIIM_ID|MIIM_TYPE|MIIM_SUBMENU };
    CClientDC cDC (AfxGetMainWnd());
    int nSepHeight = 0;

	/*const*/ int nHeight = GetItemHeight(GetDpiSource(pWnd), &nSepHeight);
    int nCaptionLength = 0;
    int nShortCutLength = 0;
    CPtrList* pListControlBars = NULL;

    if ( pWnd != NULL && !bMenuBar )
    {
        if ( pWnd->IsKindOf (RUNTIME_CLASS(CMDIFrameWnd)) )
        {
            CMDIChildWnd* pActiveChild = ((CMDIFrameWnd*)pWnd)->MDIGetActive();

            if ( pActiveChild != NULL && pActiveChild->GetSystemMenu (false)->GetSafeHmenu() == hMenu )
            {
                CMenuItem::ms_rcMRUMenuBarItem.SetRectEmpty();
                return;
            }
        }
        if ( pWnd->IsKindOf (RUNTIME_CLASS(CFrameWnd)) && !((CFrameWnd*)pWnd)->m_listControlBars.IsEmpty() )
        {
            pListControlBars = &((CFrameWnd*)pWnd)->m_listControlBars;
        }
        else
        {
            CFrameWnd* pFrame = pWnd->GetParentFrame();

            if ( pFrame != NULL && pFrame->IsKindOf (RUNTIME_CLASS(CMDIChildWnd)) )
            {
                pFrame = pFrame->GetParentFrame();
            }
            if ( pFrame != NULL )
            {
                pListControlBars = &pFrame->m_listControlBars;
            }
        }
    }
    for ( int i = 0; i < nItemCount; i++ )
    {
        WCHAR sCaption[256] = L"";
        mii.dwTypeData = sCaption;
        mii.cch = 255;
        mii.fMask &= ~MIIM_DATA;
        ::GetMenuItemInfoW (hMenu, (UINT)i, true, &mii);

        if ( (mii.fType & MFT_OWNERDRAW) == 0 && (!bMenuBar || (mii.fType & MFT_BITMAP) == 0) )
        {
            mii.fType |= MFT_OWNERDRAW;

            if ( bMenuBar )
            {
                CRect rcText (0, 0, 1000, 0);

//              cDC.DrawText (sCaption, (int)_tcslen (sCaption), rcText, DT_SINGLELINE|DT_LEFT|DT_CALCRECT);
				::DrawTextW(cDC, sCaption, (int)wcslen(sCaption), rcText, DT_SINGLELINE | DT_LEFT | DT_CALCRECT);
				
				mii.dwItemData = (ULONG)MAKELONG(MAKEWORD(0, 0), rcText.Width());
                mii.fMask |= MIIM_DATA;
            }
            ::SetMenuItemInfoW (hMenu, (uint)i, true, &mii);

            if ( (mii.fType & MFT_SEPARATOR) == 0 )
            {
                CStringW sBuff(sCaption);

                if ( mii.hSubMenu != NULL )
                {
                    ms_sSubMenuCaptions.SetAt (mii.hSubMenu, sBuff);
                }
                else
                {
                    ms_sCaptions.SetAt (mii.wID, sBuff);
                }
                if ( pListControlBars != NULL )
                {
                    POSITION pos2 = pListControlBars->GetHeadPosition();

                    while ( pos2 != NULL )
                    {
                        CControlBar* pBar = (CControlBar*)pListControlBars->GetNext (pos2);
                        ASSERT(pBar != NULL);
                        TCHAR sClassName[256];

                        ::GetClassName (pBar->m_hWnd, sClassName, lengthof (sClassName));

                        if ( !_tcsicmp (sClassName, _T("ToolbarWindow32")) )
                        {
                            TBBUTTONINFO tbbi = { sizeof(TBBUTTONINFO), TBIF_COMMAND|TBIF_IMAGE };

                            if ( pBar->SendMessage (TB_GETBUTTONINFO, mii.wID, (LPARAM)&tbbi) != -1 &&
                                 (UINT)tbbi.idCommand == mii.wID && tbbi.iImage != -1 )
                            {
                                CImgDesc imgDesc ((HIMAGELIST)pBar->SendMessage (TB_GETIMAGELIST, 0, 0), tbbi.iImage);

                                if ( mii.hSubMenu != NULL )
                                {
                                    ms_SubMenuImages.SetAt (mii.hSubMenu, imgDesc);
                                }
                                else
                                {
                                    ms_Images.SetAt (mii.wID, imgDesc);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        if ( !bMenuBar )
        {
            CMenuItem mnuItem (hMenu, (UINT)i);
            CSize sz = mnuItem.GetCaptionSize(&cDC);
            if (sz.cx > nCaptionLength)
                nCaptionLength = sz.cx;
			if (sz.cy > nHeight)
				nHeight = sz.cy;

			int nWidth = mnuItem.GetShortCutWidth (&cDC);

            if ( nWidth > nShortCutLength )
            {
                nShortCutLength = nWidth;
            }
        }
    }
    if ( !bMenuBar )
    {
        for ( int j = 0; j < nItemCount; j++ )
        {
            mii.fMask = MIIM_TYPE;
            ::GetMenuItemInfoW (hMenu, (UINT)j, true, &mii);

            if ( (mii.fType & MFT_SEPARATOR) == 0 )
            {
                mii.dwItemData = (ulong)MAKELONG(MAKEWORD(nHeight, nShortCutLength), nCaptionLength);
            }
            else
            {
                mii.dwItemData = (uint)nSepHeight;
            }
            mii.fMask = MIIM_DATA;
            ::SetMenuItemInfoW (hMenu, (UINT)j, true, &mii);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuXP::IsOwnerDrawn (HMENU hMenu)
{
    MENUITEMINFO mii = { sizeof MENUITEMINFO, MIIM_TYPE };

    ::GetMenuItemInfo (hMenu, 0, true, &mii);

    return (mii.fType & MFT_OWNERDRAW) != 0;
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::SetMRUMenuBarItem (RECT& rc)
{
    CMenuItem::ms_rcMRUMenuBarItem = rc;
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::ClearMenuItemImages()
{
	ms_Images.RemoveAll();
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::SetMenuItemImage (UINT_PTR nID, HIMAGELIST hImgList, int nIndex)
{
    CImgDesc imgDesc (hImgList, nIndex);
    ms_Images.SetAt (nID, imgDesc);
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::OnMeasureItem (MEASUREITEMSTRUCT* pMeasureItemStruct)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

    if ( pMeasureItemStruct->CtlType == ODT_MENU )
    {
        pMeasureItemStruct->itemHeight = LOBYTE(LOWORD(pMeasureItemStruct->itemData));

        if ( pMeasureItemStruct->itemHeight == 0 )
        {
            // This is a menubar item
            pMeasureItemStruct->itemWidth = HIWORD(pMeasureItemStruct->itemData) + (uint)VsUI::DpiHelper::LogicalToDeviceUnitsX(TEXTPADDING_MNUBR);
        }
        else
        {
            pMeasureItemStruct->itemWidth = IMGWIDTH + (uint)VsUI::DpiHelper::LogicalToDeviceUnitsX(IMGPADDING + TEXTPADDING + TEXTPADDING + 4) + 
				HIWORD(pMeasureItemStruct->itemData) + HIBYTE(LOWORD(pMeasureItemStruct->itemData));
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
bool CMenuXP::OnDrawItem (DRAWITEMSTRUCT* pDrawItemStruct, HWND hWnd)
{
    if ( pDrawItemStruct->CtlType != ODT_MENU )
    {
        return false;
    }
    ASSERT (pDrawItemStruct->CtlType == ODT_MENU);
	bool skip_arrow = false;
	UINT_PTR prevSelectedItemId = sSelectedItemId;

	{
		CBufferDC cDC (pDrawItemStruct->hDC, pDrawItemStruct->rcItem);
		CMenuItem item ((HMENU)pDrawItemStruct->hwndItem, pDrawItemStruct->itemID, false);
		CFontDC font (cDC, g_menuFont);

		_ASSERTE(g_menuFont.GetDPI() == g_menuDpi);

		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_SUBMENU | MIIM_ID;
		::GetMenuItemInfo((HMENU)pDrawItemStruct->hwndItem, pDrawItemStruct->itemID, false, &mii);
		bool has_submenu = !!mii.hSubMenu;

		if (pDrawItemStruct->itemState & ODS_SELECTED)
			sSelectedItemId = mii.wID;

		HWND hMenuWnd = CMenuXP::GetCurrentMenuWindow();

		//ThemeUtils::TraceFramePrint(_T("CL: L=%d T=%d R=%d B=%d W=%x"), clRect.left, clRect.top, clRect.right, clRect.bottom, hMenuWnd);
		//ThemeUtils::TraceFramePrint(_T("IT: L=%d T=%d R=%d B=%d W=%x"), itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, pDrawItemStruct->hwndItem);

		CMenuXPEffects * acme = CMenuXPEffects::sActiveEffects;
		if (acme && acme->DoTransparency())
		{
			if (hMenuWnd && IsWindow(hMenuWnd))
			{
				DWORD dwExStyle = (DWORD)GetWindowLong(hMenuWnd, GWL_EXSTYLE);
				if ((dwExStyle & WS_EX_LAYERED) != WS_EX_LAYERED)
				{
					dwExStyle &= ~WS_EX_WINDOWEDGE;
					dwExStyle &= ~WS_EX_DLGMODALFRAME;
					dwExStyle |= WS_EX_LAYERED;
					SetWindowLong(hMenuWnd, GWL_EXSTYLE, (LONG)dwExStyle);

					DWORD dwStyle = (DWORD)GetWindowLong(hMenuWnd, GWL_STYLE);
					dwStyle &= ~WS_BORDER;
					SetWindowLong(hMenuWnd, GWL_STYLE, (LONG)dwStyle);

					if (acme->m_AlphaCurrent > acme->m_MaxAlpha)
						acme->m_AlphaCurrent = acme->m_MaxAlpha;
					else if (acme->m_AlphaCurrent < acme->m_MinAlpha)
						acme->m_AlphaCurrent = acme->m_MinAlpha;

					SetLayeredWindowAttributes(hMenuWnd, 0, acme->m_AlphaCurrent, LWA_ALPHA);
					acme->WndMnu_Add(hMenuWnd, (HMENU)pDrawItemStruct->hwndItem);

					if (acme->m_AlphaWnd == nullptr)
					{
						acme->m_AlphaWnd = hMenuWnd;
						acme->m_FocusAlphaWnd = hMenuWnd;
					}

					SetTimer(hMenuWnd, MENUXP_TIMER, 10, NULL);
				}
				
				if (pDrawItemStruct->itemState & ODS_SELECTED
					&&
					acme->WndMnu_TailHWND() == hMenuWnd)
				{
					acme->m_FocusAlphaWnd = hMenuWnd;
				}
			}
		}

		bool syntax_hl = false;

		if (PopupMenuLmb::sActiveItems 
			&& 
			PopupMenuLmb::sActiveItems->size() > (pDrawItemStruct->itemID - 1)
			&&
			PopupMenuLmb::sActiveItems->at(pDrawItemStruct->itemID-1).HLSyntax
			&&
			CVS2010Colours::IsExtendedThemeActive())
		{
			syntax_hl = true;
		}

		ThemeUtils::TmpPaintType paint(syntax_hl ? PaintType::View : PaintType::None);

		if ( item.Draw (&cDC, &pDrawItemStruct->rcItem, (pDrawItemStruct->itemState&ODS_SELECTED)!=0, LOBYTE(LOWORD(pDrawItemStruct->itemData))==0, (pDrawItemStruct->itemState&ODS_HOTLIGHT)!=0, (pDrawItemStruct->itemState&ODS_INACTIVE)!=0, (pDrawItemStruct->itemState&ODS_NOACCEL)!=0, has_submenu, skip_arrow) )
		{
			CRect rc;

			::GetMenuItemRect (hWnd, (HMENU)pDrawItemStruct->hwndItem, 0, rc);
			::ClientToScreen (hWnd, CMenuItem::ms_rcMRUMenuBarItem);
			CMenuItem::ms_rcMRUMenuBarItem.top = rc.top;
			CMenuItem::ms_rcMRUMenuBarItem.bottom = rc.bottom;

			if ( CWnd::FromHandle (hWnd)->IsKindOf (RUNTIME_CLASS(CDialog)) )
			{
				CMenuItem::ms_rcMRUMenuBarItem.OffsetRect (1, 0);
			}
		}
		CMenuItem::ms_nCheck++;
	}

	if(skip_arrow)
		::ExcludeClipRect(pDrawItemStruct->hDC, pDrawItemStruct->rcItem.left, pDrawItemStruct->rcItem.top, pDrawItemStruct->rcItem.right, pDrawItemStruct->rcItem.bottom);

	if (prevSelectedItemId != sSelectedItemId && PopupMenuLmb::sActiveInstance)
	{
		PopupMenuLmb::sActiveInstance->UpdateSelection(
			(HMENU)pDrawItemStruct->hwndItem,
			sSelectedItemId);
	}

    return true;
}

///////////////////////////////////////////////////////////////////////////////
LRESULT CMenuXP::OnMenuChar (HMENU hMenu, UINT nChar, UINT nFlags)
{
    if ( (nFlags & (MF_POPUP|MF_SYSMENU)) == MF_POPUP || nFlags == 0 )
    {
        int nItemCount = ::GetMenuItemCount (hMenu);

        nChar = (uint)toupper ((int)nChar);

        for ( int i = 0; i < nItemCount; i++ )
        {
            CMenuItem mnuItem (hMenu, (uint)i);
            CStringW sCaption;

			if ('\t' == nChar && !mnuItem.GetDisabled() && mnuItem.GetID() == sSelectedItemId)
			{
				// [case: 34583] support for tab
				// for TAB, if item is selected but not disabled, execute it
				return MAKELRESULT(i, MNC_EXECUTE);
			}

            mnuItem.GetCaption (sCaption);
            sCaption.MakeUpper();

            for ( int nPos = sCaption.GetLength()-2; nPos >= 0; nPos-- )
            {
                if ( sCaption[nPos] == '&' && (UINT)towupper (sCaption[nPos+1]) == nChar &&
                     (nPos == 0 || sCaption[nPos-1] != '&') )
                {
                    return MAKELRESULT(i,2);
                }
            }
        }
    }
    return 0;
}

class CMenuXPVS2010Scrollers
{
	enum States
	{
		None = 0,
		DisableUp = 0x01,
		DisableDown = 0x02,
		HotUp = 0x04,
		HotDown = 0x08,

		Initialised = 0x80
	};

	CRect m_rcUp;
	CRect m_rcDown;
	DWORD m_states = None;
	HWND  m_hWnd = nullptr;

public:
	CMenuXPVS2010Scrollers(HWND hWnd) : m_hWnd(hWnd)
	{
#if defined(RAD_STUDIO)
		// this class causes all kinds of trouble in RadStudio.
		// The final straw was:
		//		Open FSIS, FSIS list context menu, invoke OFIS, OFIS list context menu, fails to appear.
		// CResourceException in Draw at some point also.
		// see also #cppbBrokenMenuXp
		return;
#else
		if (!CVS2010Colours::IsVS2010ColouringActive())
			return;

		if (myGetProp(hWnd, "MenuXP_NoScrollers"))
			return;

		if (GetRects(hWnd, m_rcUp, m_rcDown))
		{
			m_states = Initialised;

			CPoint curPt;
			::GetCursorPos(&curPt);

			CWindowRect wrect(m_hWnd);
			curPt -= wrect.TopLeft();

			if (m_rcUp.PtInRect(curPt))
				m_states |= HotUp;

			if (m_rcDown.PtInRect(curPt))
				m_states |= HotDown;

			int sep_height = 2;
			CMenuXP::GetItemHeight(hWnd, &sep_height);

			// point under UP arrow
			CPoint ptUp((m_rcUp.left + m_rcUp.right) / 2, m_rcUp.bottom + sep_height / 2);
			ptUp += wrect.TopLeft();

			// point over DOWN arrow
			CPoint ptDn((m_rcDown.left + m_rcDown.right) / 2, m_rcDown.top - sep_height / 2);
			ptDn += wrect.TopLeft();

			HMENU menu = CMenuXP::GetMenu(m_hWnd);
			if (menu && ::IsMenu(menu))
			{
				UINT_PTR itemHitUp = CMenuXP::ItemFromPoint(m_hWnd, ptUp);
				UINT_PTR itemHitDn = CMenuXP::ItemFromPoint(m_hWnd, ptDn);

				// if item under UP arrow is first item, disable UP arrow
				if (itemHitUp == 0)
					m_states |= DisableUp;

				// if item over DOWN arrow is last item, disable DOWN arrow
				int items_count = ::GetMenuItemCount(menu);
				if (items_count && ((UINT)items_count - 1 == itemHitDn))
					m_states |= DisableDown;
			}
		}
		else
		{
			mySetProp(hWnd, "MenuXP_NoScrollers", (HANDLE)1);
		}
#endif
	}

	bool GetRects(CRect & rect_up, CRect & rect_down)
	{
		if (m_states & Initialised)
		{
			rect_up = m_rcUp;
			rect_down = m_rcDown;
			return true;
		}
		return false;
	}

	void Draw(CDC * pDC) const
	{
		if (m_states & Initialised)
		{
			CAutoPtr<CWindowDC> wDCPtr;
			if (pDC == nullptr)
			{
				// [case: 95010]
				try
				{
					wDCPtr.Attach(new CWindowDC(CWnd::FromHandle(m_hWnd)));
				}
				catch (CResourceException* e)
				{
					e->Delete();
					return;
				}
				pDC = wDCPtr;
			}

#if defined(RAD_STUDIO)
			// these calls break display of MenuXP in RadStudio (similar to #cppbBrokenMenuXp, but independent issue?)
			// no need to call DrawArrow if both are disabled (and not even visible?)
			// this needs to be tested with a menu that does have up/down arrows -- like goto
			if (!((m_states & DisableUp) && (m_states & DisableDown)))
#endif
			{
				DrawArrow(pDC, m_rcUp, MFMWFP_UPARROW, !!(m_states & HotUp), !!(m_states & DisableUp));
				DrawArrow(pDC, m_rcDown, MFMWFP_DOWNARROW, !!(m_states & HotDown), !!(m_states & DisableDown));
			}
		}
	}

	static bool GetRects(HWND hWnd, LPRECT rect_up, LPRECT rect_down)
	{
		if (!hWnd || !::IsWindow(hWnd) || (!rect_up && !rect_down))
			return false;

		CClientRect clRect(hWnd);
		::ClientToScreen(hWnd, &clRect);
		CWindowRect wdRect(hWnd);

		clRect.OffsetRect(-wdRect.TopLeft());
		wdRect.OffsetRect(-wdRect.TopLeft());

		CRect border_rect;
		if (AdjustWindowRectEx(&border_rect, (DWORD)GetWindowLong(hWnd, GWL_STYLE), FALSE, (DWORD)GetWindowLong(hWnd, GWL_EXSTYLE)))
		{
			wdRect.left -= border_rect.left;
			wdRect.top -= border_rect.top;
			wdRect.right -= border_rect.right;
			wdRect.bottom -= border_rect.bottom;
		}

		if (wdRect.EqualRect(clRect))
			return false;

		if (rect_up)
		{
			rect_up->left = wdRect.left;
			rect_up->top = wdRect.top;

			rect_up->right = wdRect.right;
			rect_up->bottom = clRect.top;	// from client
		}

		if (rect_down)
		{
			rect_down->left = wdRect.left;
			rect_down->top = clRect.bottom;	// from client

			rect_down->right = wdRect.right;
			rect_down->bottom = wdRect.bottom;
		}

		return true;
	}

	static void DrawArrow(CDC * pDC, CRect rect, UINT uArrow, bool bHot, bool bDisabled)
	{
		_ASSERTE(uArrow == MFMWFP_DOWNARROW || uArrow == MFMWFP_UPARROW);
		auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

		const float disabled_opacity = 0.333f;
		COLORREF glyphColor = CLR_INVALID;

		CBufferDC buffDC(*pDC, &rect);
		pDC = &buffDC;

		// fill the background and assign glyph color

		if (!CVS2010Colours::IsVS2010ColouringActive())
		{
			glyphColor = bDisabled ? ::GetSysColor(COLOR_GRAYTEXT) : ::GetSysColor(COLOR_MENUTEXT);
			COLORREF bgColor = ::GetSysColor(COLOR_MENU);
			pDC->FillSolidRect(&rect, bgColor);
		}
		else
		{
			COLORREF bgColor =
				uArrow == MFMWFP_DOWNARROW ?
				CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTEND) :
				CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN);

			pDC->FillSolidRect(&rect, bgColor);

			if (bHot)
			{
				if (bDisabled)
				{
					if (gShellAttr->IsDevenv10())
						glyphColor = RGB(0, 0, 0);
					else
						glyphColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_SUBMENU_GLYPH);

					glyphColor = ThemeUtils::InterpolateColor(bgColor, glyphColor, disabled_opacity);
				}
				else
				{
					if (gShellAttr->IsDevenv10())
						glyphColor = RGB(0, 0, 0);
					else
						glyphColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHT);
				}

				// VS2010 gradient services doesn't offer gradient draw for menus, so we have to do it manually
				const std::pair<double, COLORREF> *gradients = NULL;
				int gradients_count = 0;
				COLORREF bordercolor = CLR_INVALID;
				GetVS2010SelectionColours(TRUE, gradients, gradients_count, bordercolor);

				DrawVS2010Selection(*pDC, rect, gradients, gradients_count, bordercolor,
					std::bind(&DrawVS2010MenuItemBackground, _1, _2, false, (const CRect *)NULL, 0, true));
			}
			else
			{
				if (gShellAttr->IsDevenv10())
					glyphColor = RGB(0, 0, 0);
				else
					glyphColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_SUBMENU_GLYPH);

				if (bDisabled)
					glyphColor = ThemeUtils::InterpolateColor(bgColor, glyphColor, disabled_opacity);
			}
		}

		// center point for the glyph
		CPoint origin = rect.CenterPoint();

		// for sure
		if (glyphColor == CLR_INVALID)
		{
			COLORREF bg = pDC->GetPixel(origin);
			if (ThemeUtils::ColorRGBAverage(bg) <= 0x7F)
				glyphColor = RGB(255, 255, 255);
			else
				glyphColor = RGB(0, 0, 0);
		}

		// We need nice pyramid, so: base = 2 * height AND height = base / 2
		int base = __min(rect.Height(), VsUI::DpiHelper::LogicalToDeviceUnitsY(10));

		// We deal with pixels now, so calculate everything else to ensure nice pyramid 
		int rightX = base / 2;			//  5
		int leftX = -rightX;			// -5
		int baseY = rightX / 2;		//  2
		int spikeY = baseY - rightX;	// -3

		// for down arrow, invert base and spike values
		if (uArrow == MFMWFP_DOWNARROW)
		{
			baseY = -baseY;
			spikeY = -spikeY;
		}

		// create a pyramid polygon

		POINT pts[4];
		pts[0].x = origin.x + leftX;	pts[0].y = origin.y + baseY;	// left base
		pts[1].x = origin.x;			pts[1].y = origin.y + spikeY;	// spike
		pts[2].x = origin.x + rightX;	pts[2].y = origin.y + baseY;	// right base
		pts[3].x = origin.x + leftX;	pts[3].y = origin.y + baseY;	// left base 

		// draw the glyph

		CPenDC penDC(*pDC, glyphColor, 1);
		CBrushDC dc(*pDC, glyphColor);
		pDC->Polygon(pts, 4);
	}
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// CWndMenuXP class : management of the window used by system to display popup menus
//
class CWndMenuXP : protected CVS2010Colours
{
public:
    CWndMenuXP (HWND hWnd);
   ~CWndMenuXP ();

public:
    static CWndMenuXP* FromHandle (HWND hWnd, bool bPermanent = true);

protected:
    void OnWindowPosChanging (WINDOWPOS* pWP);
    BOOL OnEraseBkgnd (CDC* pDC);
    void OnPrint (CDC* pDC, bool bOwnerDrawnItems);
    void OnNcPaint ();
    void OnShowWindow (bool bShow);
    void OnNcDestroy ();
	void OnMouseMessage (UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnTimer(UINT_PTR timer_id);
	void EnsureBGBrush();

private:
    static LRESULT CALLBACK WindowsHook (int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SubClassMenuProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
protected:
    HWND m_hWnd;
    CRect m_rcMenu;
    CPoint m_ptMenu;
    CBitmap m_bmpBkGnd;
	CBrush m_bgBrush;
	UINT_PTR m_lastSelItem;

    static CMap <HWND, HWND, CWndMenuXP*, CWndMenuXP*> ms_WndMenuMap;
    static DWORD ms_dwRefCount;
    static HHOOK ms_hHookOldMenuCbtFilter;

friend class CMenuXP;
};

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::InitializeHook ()
{
    CWndMenuXP::ms_dwRefCount++;

    if ( CWndMenuXP::ms_hHookOldMenuCbtFilter == NULL )
    {
        CWndMenuXP::ms_hHookOldMenuCbtFilter = ::SetWindowsHookEx (WH_CALLWNDPROC, CWndMenuXP::WindowsHook, AfxGetApp()->m_hInstance, ::GetCurrentThreadId());
    }
}

///////////////////////////////////////////////////////////////////////////////
void CMenuXP::UninitializeHook ()
{
    if ( CWndMenuXP::ms_dwRefCount == 0 )
    {
        return;
    }
    if ( --CWndMenuXP::ms_dwRefCount == 0 )
    {
        POSITION pos = CWndMenuXP::ms_WndMenuMap.GetStartPosition();

        while ( pos != NULL )
        {
            HWND hKey;
            CWndMenuXP* pVal;

            CWndMenuXP::ms_WndMenuMap.GetNextAssoc (pos, hKey, pVal);
            delete pVal;
        }
        CWndMenuXP::ms_WndMenuMap.RemoveAll();

        if ( CWndMenuXP::ms_hHookOldMenuCbtFilter != NULL )
        {
            ::UnhookWindowsHookEx (CWndMenuXP::ms_hHookOldMenuCbtFilter);
			CWndMenuXP::ms_hHookOldMenuCbtFilter = NULL;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
CMap <HWND, HWND, CWndMenuXP*, CWndMenuXP*> CWndMenuXP::ms_WndMenuMap;
DWORD CWndMenuXP::ms_dwRefCount = 0;
HHOOK CWndMenuXP::ms_hHookOldMenuCbtFilter = NULL;

///////////////////////////////////////////////////////////////////////////////
CWndMenuXP::CWndMenuXP (HWND hWnd)
    : m_hWnd (hWnd), m_rcMenu (0, 0, 0, 0), m_ptMenu (-0xFFFF, -0xFFFF), m_lastSelItem(MFMWFP_NOITEM)
{
}

///////////////////////////////////////////////////////////////////////////////
CWndMenuXP::~CWndMenuXP ()
{
    WNDPROC oldWndProc = (WNDPROC)myGetProp (m_hWnd, _WndPropName_OldProc);

    if ( oldWndProc != NULL )
    {
        ::SetWindowLongPtr (m_hWnd, GWLP_WNDPROC, (LONG_PTR)oldWndProc);
        myRemoveProp (m_hWnd, _WndPropName_OldProc);
    }
    ms_WndMenuMap.RemoveKey (m_hWnd);
}

///////////////////////////////////////////////////////////////////////////////
CWndMenuXP* CWndMenuXP::FromHandle (HWND hWnd, bool bPermanent)
{
    CWndMenuXP* pWnd = NULL;

    if ( ms_WndMenuMap.Lookup (hWnd, pWnd) )
    {
        return pWnd;
    }
    if ( bPermanent )
    {
        return NULL;
    }
    pWnd = new CWndMenuXP (hWnd);
    ms_WndMenuMap.SetAt (hWnd, pWnd);

    return pWnd;
}

///////////////////////////////////////////////////////////////////////////////
void CWndMenuXP::OnWindowPosChanging (WINDOWPOS* pWP)
{
    if ( GetWinVersion() < wvWinXP )
    {
		auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);
		pWP->cx += VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW);
        pWP->cy += VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW);
    }
    pWP->y--;
    m_ptMenu.x = pWP->x;
    m_ptMenu.y = pWP->y;
}

///////////////////////////////////////////////////////////////////////////////
BOOL CWndMenuXP::OnEraseBkgnd (CDC* pDC)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

	// is this the best place to reset sSelectedItemId?
	sSelectedItemId = UINT_PTR_MAX;

	if (IsVS2010ColouringActive() && pDC)
	{
		CRect rect;
		GetClientRect(m_hWnd, &rect);
		CBrush br;
		if (br.CreateSolidBrush(GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN)))
		{
			pDC->FillRect(rect, &br);
			return TRUE;
		}
	}

    if ( !IsWindowVisible (m_hWnd) )
    {
        CClientRect rc (m_hWnd);

        if ( m_bmpBkGnd.m_hObject != NULL )
        {
            m_bmpBkGnd.DeleteObject();
        }
        m_bmpBkGnd.Attach (GetScreenBitmap (CRect (m_ptMenu.x, m_ptMenu.y, rc.right + m_ptMenu.x + VsUI::DpiHelper::LogicalToDeviceUnitsX(10),
                                                   rc.bottom + m_ptMenu.y + VsUI::DpiHelper::LogicalToDeviceUnitsY(10))));
    }

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
void DrawShadow (HDC hDCIn, HDC hDCOut, RECT& rc)
{
	int x = 0;
    for ( ; x < rc.right-1; x++ )
    {
        int nEnd = ( x > rc.right-SM_CXSHADOW*2 ) ? rc.right-SM_CXSHADOW-x : SM_CXSHADOW;

        for ( int y = ( x < 2 ) ? 2-x : x > rc.right-SM_CXSHADOW-3 ? x-rc.right+SM_CXSHADOW+3 : 0; y < nEnd; y++ )
        {
            int nMakeSpec = 78+(3-(x==0?0:(x==1?(y<2?0:1):(x==2?(y<2?y:2):y))))*5;
            COLORREF cr = GetPixel (hDCIn, x+SM_CXSHADOW, rc.bottom-y-1);
            COLORREF cr2 = RGB(((nMakeSpec * int(GetRValue(cr))) / 100),
			                   ((nMakeSpec * int(GetGValue(cr))) / 100),
			                   ((nMakeSpec * int(GetBValue(cr))) / 100));
			SetPixel (hDCOut, x+SM_CXSHADOW, rc.bottom-y-1, cr2);
        }
    }
    for ( x = 0; x < SM_CXSHADOW; x++ )
    {
        for ( int y = ( x < 2 ) ? 2-x : 0; y < rc.bottom-x-SM_CXSHADOW-((x>0)?1:2); y++ )
        {
            int nMakeSpec = 78+(3-(y==0?0:(y==1?(x<2?0:1):(y==2?(x<2?x:2):x))))*5;
            COLORREF cr = GetPixel (hDCIn, rc.right-x-1, y+SM_CXSHADOW);
            COLORREF cr2 = RGB(((nMakeSpec * int(GetRValue(cr))) / 100),
			                   ((nMakeSpec * int(GetGValue(cr))) / 100),
			                   ((nMakeSpec * int(GetBValue(cr))) / 100));
			SetPixel (hDCOut, rc.right-x-1, y+SM_CXSHADOW, cr2);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// this gets called to paint the menu when invoked via mouse click/button down
void CWndMenuXP::OnPrint (CDC* pDC, bool bOwnerDrawnItems)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

    CWindowRect rc (m_hWnd);
    CBrushDC br (pDC->m_hDC);

	COLORREF border_colour;
	if(!IsVS2010ColouringActive())
		border_colour = ::GetSysColor(COLOR_3DDKSHADOW);
	else
		border_colour = GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BORDER);
    CPenDC pen (pDC->m_hDC, border_colour);

    rc.OffsetRect (-rc.TopLeft());
    m_rcMenu = rc;

    if ( GetWinVersion() < wvWinXP )
    {
        rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW);
        rc.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW);
    }

	const CRect borderRc(rc);
    pDC->Rectangle (borderRc);

	if(!IsVS2010ColouringActive())
	{
		pen.Color(HLS_TRANSFORM (::GetSysColor(COLOR_3DFACE), +75, 0));
		rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
		pDC->Rectangle(rc);
		rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
		pDC->Rectangle(rc);
	}
	else
	{
		int saved = pDC->SaveDC();
		CRgn rgn;
		rgn.CreateRectRgnIndirect(rc);

		std::vector<int> column_separators;
		CRect total_area(0, 0, 0, 0);
		HMENU menu = (HMENU)::SendMessage(m_hWnd, MN_GETHMENU, 0, 0);		// gmit: why GetMenu() doesn't work here?!?!
		if(menu && ::IsMenu(menu))
		{
			const CWindowRect wrect(m_hWnd);
			int items = ::GetMenuItemCount(menu);
			CRect column_area(0, 0, 0, 0);

			for(int i = 0; i < items; i++)
			{
				CRect itemrect(0, 0, 0, 0);
				::GetMenuItemRect(m_hWnd, menu, (UINT)i, itemrect);
				if(itemrect.IsRectEmpty())
					continue;
				itemrect.OffsetRect(-wrect.TopLeft());

				if(!column_area.IsRectEmpty() && (itemrect.left >= column_area.right))
				{
					column_separators.push_back((itemrect.left + column_area.right) / 2);
					column_area.SetRectEmpty();
				}
				column_area |= itemrect;
				total_area |= itemrect;

				CRgn itemrgn;
				itemrgn.CreateRectRgnIndirect(itemrect);
				rgn.CombineRgn(&rgn, &itemrgn, RGN_DIFF);
			}
		}

		pDC->SelectObject(&rgn);
		DrawVS2010MenuItemBackground(*pDC, rc, false, &rc, 2, true);

		// draw separators
		{
			CPen pen2;
			for(std::vector<int>::const_iterator it = column_separators.begin(); it != column_separators.end(); ++it)
			{
				if(!pen2.m_hObject)
				{
					pen2.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), border_colour);
					pDC->SelectObject(&pen2);
				}
				static const int column_indicator_offset = VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
				pDC->MoveTo(*it, total_area.top + column_indicator_offset);
				pDC->LineTo(*it, total_area.bottom - column_indicator_offset);
			}
		}

		pDC->RestoreDC(saved);
		// strange behavior @ 200%
		CPenDC pendc (pDC->m_hDC, border_colour);
		pDC->Rectangle (borderRc);
	}

    if ( bOwnerDrawnItems && !CMenuItem::ms_rcMRUMenuBarItem.IsRectEmpty() &&
         CMenuItem::ms_rcMRUMenuBarItem.bottom == m_ptMenu.y+VsUI::DpiHelper::LogicalToDeviceUnitsY(1) )
    {
        pen.Color (HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +20, 0));
        pDC->MoveTo (CMenuItem::ms_rcMRUMenuBarItem.left - m_ptMenu.x - VsUI::DpiHelper::LogicalToDeviceUnitsX(3), 0);
        pDC->LineTo (CMenuItem::ms_rcMRUMenuBarItem.left - m_ptMenu.x + CMenuItem::ms_rcMRUMenuBarItem.Width() - VsUI::DpiHelper::LogicalToDeviceUnitsX(5), 0);
    }
    if ( GetWinVersion() < wvWinXP )
    {
        rc.right += VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW + 2);
        rc.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW + 2);

        CDC cMemDC;
        cMemDC.CreateCompatibleDC (pDC);
        HGDIOBJ hOldBitmap = ::SelectObject (cMemDC.m_hDC, m_bmpBkGnd);
        pDC->BitBlt(
			0, 
			rc.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW * 2), 
			VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			&cMemDC, 
			0, 
			rc.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			SRCCOPY);
        pDC->BitBlt(
			rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			rc.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			&cMemDC, 
			rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			rc.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW), 
			SRCCOPY);
        pDC->BitBlt(
			rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			0, 
			VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW * 2), 
			&cMemDC, 
			rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW), 
			0, 
			SRCCOPY);
        DrawShadow (cMemDC.m_hDC, pDC->m_hDC, rc);
        ::SelectObject (cMemDC.m_hDC, hOldBitmap);
    }
}

///////////////////////////////////////////////////////////////////////////////
// this gets called to paint menu when menu is invoked via keyboard
void CWndMenuXP::OnNcPaint ()
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

    CWindowDC cDC (CWnd::FromHandle (m_hWnd));
    CDC* pDC = &cDC;
    CWindowRect rc (m_hWnd);

    m_ptMenu.x = rc.left;
    m_ptMenu.y = rc.top;
    rc.OffsetRect (-rc.TopLeft());

//	if (rc != m_rcMenu)
    {
        m_rcMenu = rc;

		CBrushDC br (pDC->m_hDC);

		COLORREF border_colour;
		if(!IsVS2010ColouringActive())
			border_colour = ::GetSysColor(COLOR_3DDKSHADOW);
		else
			border_colour = GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BORDER);

        if ( GetWinVersion() < wvWinXP )
        {
            rc.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW);
            rc.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW);
        }
		const CRect borderRc(rc);
		CPenDC pen (pDC->m_hDC, border_colour);
        pDC->Rectangle (borderRc);

		if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
		{
			int saved = pDC->SaveDC();
			DrawVS2010MenuItemBackground(*pDC, rc, false, &rc, 2, true);
			pDC->RestoreDC(saved);
			CPenDC pen2 (pDC->m_hDC, border_colour);
			pDC->Rectangle (borderRc);
		}
		else if (IsVS2010ColouringActive())
		{
			rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
			int saved = pDC->SaveDC();
			CRgn rgn, rgn2;
			rgn.CreateRectRgnIndirect(rc);
			rgn2.CreateRectRgn(
				rc.left + VsUI::DpiHelper::LogicalToDeviceUnitsX(2), 
				rc.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(2), 
				rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(2), 
				rc.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
			rgn.CombineRgn(&rgn, &rgn2, RGN_DIFF);
			pDC->SelectObject(&rgn);
			DrawVS2010MenuItemBackground(*pDC, rc, false, &rc, 2, true);
			pDC->RestoreDC(saved);
			rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));

			{
				// strange behavior @ 200%
				CPenDC pen2 (pDC->m_hDC, border_colour);
				pDC->Rectangle (borderRc);
			}
		}
		else
		{
			pen.Color (HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +75, 0));
			rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
			pDC->Rectangle (rc);
			rc.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
			pDC->Rectangle (rc);
		}

        if ( !CMenuItem::ms_rcMRUMenuBarItem.IsRectEmpty() &&
             CMenuItem::ms_rcMRUMenuBarItem.bottom == m_ptMenu.y + VsUI::DpiHelper::LogicalToDeviceUnitsY(1))
        {
            pen.Color (HLS_TRANSFORM (::GetSysColor (COLOR_3DFACE), +20, 0));
            pDC->MoveTo (CMenuItem::ms_rcMRUMenuBarItem.left - m_ptMenu.x - VsUI::DpiHelper::LogicalToDeviceUnitsX(3), 0);
            pDC->LineTo (CMenuItem::ms_rcMRUMenuBarItem.left - m_ptMenu.x + CMenuItem::ms_rcMRUMenuBarItem.Width() - VsUI::DpiHelper::LogicalToDeviceUnitsX(5), 0);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
void CWndMenuXP::OnShowWindow (bool bShow)
{
    if ( !bShow )
    {
		if (CMenuXPEffects::sActiveEffects)
			CMenuXPEffects::sActiveEffects->OnWndDestroy(m_hWnd);

        delete this;
    }
}

///////////////////////////////////////////////////////////////////////////////
void CWndMenuXP::OnNcDestroy ()
{
	if (CMenuXPEffects::sActiveEffects)
		CMenuXPEffects::sActiveEffects->OnWndDestroy(m_hWnd);

    delete this;
}

///////////////////////////////////////////////////////////////////////////////
void CWndMenuXP::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
// 	CMenuXPEffects * acme = CMenuXPEffects::sActiveEffects;
// 	if (acme)
// 	{
// 		if (uMsg == WM_MOUSELEAVE)
// 		{
// 			POINT pnt;
// 			if (::GetCursorPos(&pnt))
// 			{
// 				HWND pnt_wnd = ::WindowFromPoint(pnt);
// 
// 				if (pnt_wnd && acme->m_hWndList.Find(pnt_wnd) && pnt_wnd != acme->m_FocusAlphaWnd)
// 					acme->m_FocusAlphaWnd = pnt_wnd;
// 			}
// 		}
// 	}
}

///////////////////////////////////////////////////////////////////////////////
void CWndMenuXP::OnTimer(UINT_PTR timer_id)
{
	if (timer_id == MENUXP_TIMER)
	{
		CMenuXPEffects * acme = CMenuXPEffects::sActiveEffects;
		if (acme)
		{
			acme->SetBestFocusWindow();
				
			if (m_hWnd == acme->m_AlphaWnd)
			{
				bool ctrl_down =	0 != (::GetKeyState(VK_CONTROL) & 0x8000);
				bool alt_down =		0 != (::GetKeyState(VK_MENU) & 0x8000);
				bool shift_down =	0 != (::GetKeyState(VK_SHIFT) & 0x8000);

				if (ctrl_down && !alt_down && !shift_down)
				{
					acme->m_AlphaCurrent = acme->m_MinAlpha;
					acme->m_FocusAlphaCurrent = acme->m_MinFocusAlpha;				
				}
				else
				{
					acme->m_FocusAlphaCurrent = acme->m_MaxFocusAlpha;
					acme->m_AlphaCurrent = acme->m_MaxAlpha;
				}
			}

			if ((::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED) == WS_EX_LAYERED)
			{
				COLORREF clrKey;
				BYTE alpha;
				DWORD flags;

				if (::GetLayeredWindowAttributes(m_hWnd, &clrKey, &alpha, &flags))
				{
					BYTE dst_alpha, min_alpha, max_alpha;
					if (acme->m_FocusAlphaWnd == m_hWnd)
					{
						dst_alpha = acme->m_FocusAlphaCurrent;
						min_alpha = acme->m_MinFocusAlpha;
						max_alpha = acme->m_MaxFocusAlpha;
					}
					else
					{
						dst_alpha = acme->m_AlphaCurrent;
						min_alpha = acme->m_MinAlpha;
						max_alpha = acme->m_MaxAlpha;
					}

					if ((flags & LWA_ALPHA) == LWA_ALPHA && alpha != dst_alpha)
					{
						UINT num_steps = acme->m_FadeSteps;

						if (num_steps == 0)
							num_steps = 1;
						else if (num_steps > 0xFF)
							num_steps = 0xFF;

						BYTE alpha_step = BYTE((max_alpha - min_alpha) / (BYTE)num_steps);
						BYTE tmp_step   = BYTE((__max(dst_alpha,alpha) - __min(dst_alpha,alpha)) / (BYTE)num_steps);

						if (tmp_step > alpha_step)
							alpha_step = tmp_step;

						if (alpha_step == 0)
							alpha_step = 1;

						if (alpha < dst_alpha)
						{
							if ((short)alpha + (short)alpha_step >= (short)dst_alpha)
								alpha = dst_alpha;
							else
								alpha += alpha_step;
						}
						else if (alpha > dst_alpha)
						{
							if ((short)alpha - (short)alpha_step <= (short)dst_alpha)
								alpha = dst_alpha;
							else
								alpha -= alpha_step;
						}

						SetLayeredWindowAttributes(m_hWnd, clrKey, alpha, flags);

						::RedrawWindow(m_hWnd, NULL, NULL, RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
					}
				}
			}
		}
	}
}

void CWndMenuXP::EnsureBGBrush()
{
	if (!(HBRUSH)m_bgBrush)
	{
		HMENU hMenu = CMenuXP::GetMenu(m_hWnd);
		if (hMenu && ::IsMenu(hMenu))
		{
			MENUINFO mi{ sizeof MENUINFO, MIM_BACKGROUND };

			m_bgBrush.CreateSolidBrush(
				CVS2010Colours::IsVS2010ColouringActive() ?
				CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN) :
				GetSysColor(COLOR_MENU));

			mi.hbrBack = m_bgBrush;
			::SetMenuInfo(hMenu, &mi);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK CWndMenuXP::WindowsHook (int code, WPARAM wParam, LPARAM lParam)
{
    CWPSTRUCT* pStruct = (CWPSTRUCT*)lParam;

    // Not a real loop (just for 'break' branchment)
    while ( code == HC_ACTION )
    {
        HWND hWnd = pStruct->hwnd;

        // Normal and special handling for menu 0x10012
        if ( pStruct->message != WM_CREATE && pStruct->message != 0x01E2 )
        {
            break;
        }

		// I've removed these checks - the hook is only set when we want a menu
		// to be hooked (right before TrackPopupMenu) and then the hook is removed.
		// It assumes XPLookNFeel is wanted.
// 		_AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
// 		if (!pThreadState)
// 			break;
// 
// 		HWND focWnd = pThreadState->m_hTrackingWindow;
// 		if (focWnd == NULL)
// 			focWnd = ::GetFocus();
// 		if (focWnd == NULL || !CMenuXP::GetXPLookNFeel(focWnd))
// 			break;
// 
// 		CWnd* pFrame = CWnd::FromHandle(focWnd);
//      if ( pFrame == NULL )
//      {
//          break;
//      }

		TCHAR sClassName[10];
        int Count = ::GetClassName (hWnd, sClassName, lengthof(sClassName));

        // Check for the menu-class
        if ( Count != 6 || _tcscmp (sClassName, _T("#32768")) != 0 )
        {
            break;
        }
        VERIFY(CWndMenuXP::FromHandle (pStruct->hwnd, false) != NULL);

        if ( myGetProp (pStruct->hwnd, _WndPropName_OldProc) != NULL )
        {
            // Already subclassed
            break;
        }
        // Subclass the window
        WNDPROC oldWndProc = (WNDPROC)(LONG_PTR)::GetWindowLongPtr (pStruct->hwnd, GWLP_WNDPROC);

        if ( oldWndProc == NULL )
        {
            break;
        }
        ASSERT(oldWndProc != SubClassMenuProc);

        if ( !mySetProp (pStruct->hwnd, _WndPropName_OldProc, oldWndProc) )
        {
            break;
        }
        if ( !SetWindowLongPtr (pStruct->hwnd, GWLP_WNDPROC,(LONG_PTR)SubClassMenuProc) )
        {
            myRemoveProp (pStruct->hwnd, _WndPropName_OldProc);
            break;
        }

        // Success !
        break;
    }
    return CallNextHookEx (CWndMenuXP::ms_hHookOldMenuCbtFilter, code, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK CWndMenuXP::SubClassMenuProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(g_menuDpi);

	class track_current_menu
	{
	public:
		track_current_menu(HWND hwnd) : hwnd(hwnd)
		{
			CSingleLock l(&inside_menu_msg_cs, true);
			inside_menu_msg.push_back(hwnd);
		}
		~track_current_menu()
		{
			CSingleLock l(&inside_menu_msg_cs, true);
			_ASSERTE(inside_menu_msg.size() > 0);
			_ASSERTE(inside_menu_msg.back() == hwnd);
			inside_menu_msg.pop_back();
		}
	protected:
		const HWND hwnd;
	} _track_current_menu(hWnd);

    WNDPROC oldWndProc = (WNDPROC)myGetProp (hWnd, _WndPropName_OldProc);
    CWndMenuXP* pWnd = NULL;

	if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
	{
		if ((pWnd = CWndMenuXP::FromHandle(hWnd)) != NULL)
		{
			pWnd->OnMouseMessage(uMsg, wParam, lParam);
		}
	}
	else if (uMsg == WM_TIMER)
	{
		if (wParam == IDSYS_MNANIMATE)
		{
			::KillTimer(hWnd, IDSYS_MNANIMATE);
			return 0;
		}

		if ((pWnd = CWndMenuXP::FromHandle(hWnd)) != NULL)
		{
			if (CVS2010Colours::IsVS2010ColouringActive())
			{
				if (wParam == MFMWFP_UPARROW || wParam == MFMWFP_DOWNARROW)
				{
					CMenuXPVS2010Scrollers scrolls(hWnd);
					LRESULT rslt = CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
					scrolls.Draw(nullptr);
					return rslt;
				}
			}

			pWnd->OnTimer(wParam);
		}
	}
	else if (uMsg == MN_SELECTITEM)
	{
		if ((pWnd = CWndMenuXP::FromHandle(hWnd)) != NULL)
		{
			LRESULT rslt = 1;

			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();

			if (pWnd->m_lastSelItem != wParam)
			{
				pWnd->m_lastSelItem = wParam;

				CMenuXPVS2010Scrollers scrolls(hWnd);

				rslt = CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);

				scrolls.Draw(nullptr);

				if (wParam < MFMWFP_MINVALID)
				{
					if (sSelectedItemId != wParam)
					{
						sSelectedItemId = wParam;
						if (PopupMenuLmb::sActiveInstance)
							PopupMenuLmb::sActiveInstance->UpdateSelection(CMenuXP::GetMenu(hWnd), sSelectedItemId);
					}
				}
				else
				{
					if (sSelectedItemId != MFMWFP_NOITEM)
					{
						sSelectedItemId = MFMWFP_NOITEM;
						if (PopupMenuLmb::sActiveInstance)
							PopupMenuLmb::sActiveInstance->UpdateSelection(CMenuXP::GetMenu(hWnd), sSelectedItemId);
					}
				}
			}

			return rslt;
		}
		return 0;
	}

	if (CVS2010Colours::IsVS2010ColouringActive())
	{
		if (uMsg == MN_DBLCLK)
		{
			LRESULT rslt = 0;
			CMenuXPVS2010Scrollers scrolls(hWnd);
			CallWindowProc(oldWndProc, hWnd, MN_BUTTONDOWN, wParam, lParam);
			CallWindowProc(oldWndProc, hWnd, MN_BUTTONUP, wParam, lParam);
			CallWindowProc(oldWndProc, hWnd, MN_BUTTONDOWN, wParam, lParam);
			CallWindowProc(oldWndProc, hWnd, MN_BUTTONUP, wParam, lParam);

			scrolls.Draw(nullptr);
			return rslt;
		}
		else if (uMsg == MN_BUTTONUP || uMsg == MN_BUTTONDOWN || uMsg == MN_FINDMENUWINDOWFROMPOINT || uMsg == MN_GETHMENU)
		{
			LRESULT rslt = 0;
			CMenuXPVS2010Scrollers scrolls(hWnd);
			rslt = CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
			scrolls.Draw(nullptr);
			return rslt;
		}
		else if (uMsg == WM_UAHINITMENU)
		{
			if ((pWnd = CWndMenuXP::FromHandle(hWnd)) != NULL)
			{
				pWnd->EnsureBGBrush();
				CMenuXPVS2010Scrollers scrolls(hWnd);
				scrolls.Draw(nullptr);
			}

			::SetWindowTheme(hWnd, L"", L"");

			return FALSE; // We don't want OS theme stuff
		}
	}

	// ThemeUtils::TraceFrameDbgPrintMSG(uMsg, wParam, lParam);


	// We can't rely on [1-9A-Z] chars, because on Unicode keyboards, 
	// keys may represent language specific Unicode chars.
	// If MapVirtualKeysToInvariantChars = true,
	// even when user has Unicode keyboard and presses key 3,
	// menu will get char '3' and not some Unicode char used by
	// specific keyboard layout for active OS language.
	if (
		PopupMenuLmb::sActiveInstance &&
		PopupMenuLmb::sActiveInstance->MapVirtualKeysToInvariantChars &&
		uMsg == WM_CHAR &&
		!(wParam >= '0' && wParam <= '9') &&
		!(wParam >= 'a' && wParam <= 'z') &&
		!(wParam >= 'A' && wParam <= 'Z') &&
		!(GetKeyState(VK_SHIFT) & 0x1000)
		)
	{
		/////////////////////////
		// handle keys 0 - 9

		for (int vk = 0x30; vk <= 0x39; vk++)
		{
			if (GetKeyState(vk) & 0x1000)
			{
				WCHAR ch = (WCHAR)('0' + vk - 0x30);
				return ::SendMessageW(hWnd, uMsg, (WPARAM)ch, lParam); // send new message
			}
		}

		/////////////////////////
		// handle keys A - Z

		for (int vk = 0x41; vk <= 0x5A; vk++)
		{
			if (GetKeyState(vk) & 0x1000)
			{
				WCHAR ch = (WCHAR)('A' + vk - 0x41);
				return ::SendMessageW(hWnd, uMsg, (WPARAM)ch, lParam); // send new message
			}
		}
	}

	if (PopupMenuLmb::sActiveEvents)
	{
		LRESULT rslt = 0;
		if (PopupMenuLmb::sActiveEvents->OnWindowMessage(rslt,hWnd,uMsg,wParam,lParam))
			return rslt;
	}

    switch ( uMsg )
    {
    case WM_NCCALCSIZE:
        {
            LRESULT lResult = CallWindowProc (oldWndProc, hWnd, uMsg, wParam, lParam);

            if ( GetWinVersion() < wvWinXP )
            {
                NCCALCSIZE_PARAMS* lpncsp = (NCCALCSIZE_PARAMS*)lParam;

                lpncsp->rgrc[0].right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(SM_CXSHADOW);
                lpncsp->rgrc[0].bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(SM_CXSHADOW);
            }
            return lResult;
        }

    case WM_WINDOWPOSCHANGING:
        if ( (pWnd=CWndMenuXP::FromHandle (hWnd)) != NULL )
        {
			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();

            pWnd->OnWindowPosChanging ((LPWINDOWPOS)lParam);
        }
        break;

    case WM_ERASEBKGND:
	{
#if defined(RAD_STUDIO) 
		// Fix for broken MenuXP in CppBuilder 10.3 (has black background with black text).  #cppbBrokenMenuXp
		break;
#else
		LRESULT rslt = 0;
		CMenuXPVS2010Scrollers scrolls(hWnd);

        if ( (pWnd=CWndMenuXP::FromHandle (hWnd)) != NULL )
        {
			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();
            
			if (pWnd->OnEraseBkgnd(CDC::FromHandle ((HDC)wParam)))
				rslt = 1;
        }

		scrolls.Draw(nullptr);
		return rslt;
#endif
	}
// 	case WM_PAINT:
// 	{
// 		CWnd * wndP = CWnd::FromHandle(hWnd);
// 		if (wndP)
// 		{
// 			CPaintDC dc(wndP);
// 			CBufferDC buff(dc);
// 			LRESULT rslt = CallWindowProc(oldWndProc, hWnd, WM_PRINTCLIENT, (WPARAM)(HDC)buff, PRF_CLIENT | PRF_CHECKVISIBLE);
// 		}
// 	}
	case WM_PRINTCLIENT:
	case WM_PAINT:
		if ((pWnd = CWndMenuXP::FromHandle(hWnd)) != NULL)
		{
			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();

			CMenuXPVS2010Scrollers scrolls(hWnd);
			LRESULT rslt = CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
			scrolls.Draw(nullptr);
			return rslt;
		}

    case WM_PRINT:
        {
            BYTE nCheck = CMenuItem::ms_nCheck;			
			LRESULT lResult = 0;

			pWnd = CWndMenuXP::FromHandle(hWnd);

			if (pWnd == nullptr)
				return CallWindowProc (oldWndProc, hWnd, uMsg, wParam, lParam);

			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();

			CMenuXPVS2010Scrollers scrolls(hWnd);

			CRect rc_up, rc_dn;
			if (scrolls.GetRects(rc_up, rc_dn))
			{
				ThemeUtils::AutoSaveRestoreDC asrdc((HDC)wParam);
				::ExcludeClipRect((HDC)wParam, rc_up.left, rc_up.top, rc_up.right, rc_up.bottom);
				::ExcludeClipRect((HDC)wParam, rc_dn.left, rc_dn.top, rc_dn.right, rc_dn.bottom);
				lResult = CallWindowProc (oldWndProc, hWnd, uMsg, wParam, lParam);
			}
			else
			{
				lResult = CallWindowProc (oldWndProc, hWnd, uMsg, wParam, lParam);
			}

			bool bOwnerDrawnItems = nCheck != CMenuItem::ms_nCheck;

			CDC * pDC = CDC::FromHandle((HDC)wParam);

            if (pWnd)
                pWnd->OnPrint (pDC, bOwnerDrawnItems);

			scrolls.Draw(pDC);
            return lResult;
        }

    case WM_NCPAINT:
        if ( (pWnd=CWndMenuXP::FromHandle (hWnd)) != NULL )
        {
			if (CVS2010Colours::IsVS2010ColouringActive())
				pWnd->EnsureBGBrush();

			CMenuXPVS2010Scrollers scrolls(hWnd);
            pWnd->OnNcPaint();
			scrolls.Draw(nullptr);
            return 0;
        }
        break;

    case WM_SHOWWINDOW:
        if ( (pWnd=CWndMenuXP::FromHandle (hWnd)) != NULL )
        {
            pWnd->OnShowWindow (wParam != 0);
        }
        break;

    case WM_NCDESTROY:
        if ( (pWnd=CWndMenuXP::FromHandle (hWnd)) != NULL )
        {
            pWnd->OnNcDestroy();
        }
        break;
    }
    return CallWindowProc (oldWndProc, hWnd, uMsg, wParam, lParam);
}

// PopupMenuXP
// ----------------------------------------------------------------------------
//
PopupMenuXP::PopupMenuXP()
{
	mnu = CreatePopupMenu();
	_ASSERTE(sActiveMenu == NULL);
	sActiveMenu = this;
	_ASSERTE(!sActiveMenuCnt);
	++sActiveMenuCnt;

	CMenuXP::ClearMenuItemImages();
}

PopupMenuXP::~PopupMenuXP()
{
	sActiveMenu = NULL;
	--sActiveMenuCnt;
	DestroyMenu(mnu);	
	for (MenuPopupList::iterator it = mPopups.begin(); it != mPopups.end(); ++it)
		delete (*it);
}

std::shared_ptr<PopupMenuXP::ReqPt> PopupMenuXP::PushRequiredPoint(const CPoint & pt)
{
	return std::shared_ptr<PopupMenuXP::ReqPt>(new PopupMenuXP::ReqPt(pt));
}

INT
PopupMenuXP::TrackPopupMenuXP(CWnd *parent, int x, int y, UINT flags)
{
	std::unique_ptr<VsUI::CDpiScope> dpi;
	if (parent)
		dpi = std::make_unique<VsUI::CDpiScope>(*parent);

	CRect r(0, 0, 10, 10);
	for (uint cnt = 0; cnt < 10; ++cnt)
	{
		if (Create("Popup", WS_CHILD, r, parent))
			break;

		// add retry for [case: 111864]
		DWORD err = GetLastError();
		vLog("WARN: TrackPopupMenuXP call to Create failed, 0x%08lx\n", err);
		Sleep(100 + (cnt * 50));
	}

	if (!m_hWnd)
	{
		vLog("ERROR: TrackPopupMenuXP call to Create failed\n");
		_ASSERTE(m_hWnd);
	}

	CMenuXP::SetXPLookNFeel(this, mnu);
	MenuXpHook hk(parent);

	if (CMenuXPEffects::sActiveEffects 
		&& 
		CMenuXPEffects::sActiveEffects->DoTransparency())
	{
		flags |= TPM_NOANIMATION;
	}

	if (!sReqPtStack.empty())
	{
		auto top = sReqPtStack.top();
		x = top.x;
		y = top.y;
	}

	DoMultipleDPIsWorkaround(::GetFocus());
	
	INT res = ::TrackPopupMenu(mnu, flags, x, y, NULL, GetSafeHwnd(), NULL) & 0xffff;

	vCatLog("Editor.Events", "VaEventPM TrackPopupMenu id=0x%x", res);
	return res;
}

void
PopupMenuXP::AddMenuItem(UINT_PTR id, UINT flags, LPCSTR text, UINT iconId /*= 0*/)
{
	CStringW textW = ::MbcsToWide(text, strlen_i(text));
	AddMenuItemW(id, flags, textW, iconId);
}

void 
PopupMenuXP::AddMenuItem(CMenu * parent, UINT_PTR id, UINT flags, LPCSTR text, UINT iconId /*= 0*/)
{
	CStringW textW = ::MbcsToWide(text, strlen_i(text));
	AddMenuItemW(parent, id, flags, textW, iconId);
}

static void EscapeMarkup(CStringW& str)
{
	str.Replace(CStringW(FTM_BOLD), L"<BOLD>");
	str.Replace(CStringW(FTM_NORMAL), L"<NORMAL>");
	str.Replace(CStringW(FTM_UNDERLINE), L"<UNDERLINE>");
	str.Replace(CStringW(FTM_ITALIC), L"<ITALIC>");
	str.Replace(CStringW(FTM_DIM), L"<DIM>");
}

static CStringW GetStringWithoutFormatting(CStringW& str)
{
	CStringW res = str;
	res.Replace(CStringW(FTM_BOLD), L"");
	res.Replace(CStringW(FTM_NORMAL), L"");
	res.Replace(CStringW(FTM_UNDERLINE), L"");
	res.Replace(CStringW(FTM_ITALIC), L"");
	res.Replace(CStringW(FTM_DIM), L"");

	return res;
}

void
PopupMenuXP::AddMenuItemW(UINT_PTR id, UINT flags, LPCWSTR text, UINT iconId /*= 0*/)
{
	AddMenuItemW(CMenu::FromHandle(mnu), id, flags, text, iconId);
}

void 
PopupMenuXP::AddMenuItemW(CMenu * parent, UINT_PTR id, UINT flags, LPCWSTR text, UINT iconId /*= 0*/)
{
	if (iconId)
		id = MAKEWPARAM(id, iconId);
	vCatLog("Editor.Events", "VaEventPM   AddMenuItemW '%s', id=0x%zx, flags=0x%x, icon=0x%x", (LPCTSTR)CString(text), id, flags, iconId);
	::AppendMenuW(parent ? *parent : mnu, flags, id, text);
	if (gTestLogger && gTestLogger->IsMenuLoggingEnabled())
	{
		CStringW txt(text);
		if (::IsFile(txt))
			txt = ::Basename(txt);
		else
		{
			int pos = txt.Find('\t');
			if (-1 != pos)
			{
				CStringW tmp(txt.Left(pos));
				tmp.TrimRight();
				if (::IsFile(GetStringWithoutFormatting(tmp)))
				{
					tmp = ::Basename(tmp);
					tmp += txt.Mid(pos);
					txt = tmp;
				}
			}
		}
		EscapeMarkup(txt);
		txt.Replace(L" (beta)", L"");
		gTestLogger->LogStrW(L"MenuItem: " + txt);
	}
}

BOOL
PopupMenuXP::AppendPopup(LPCSTR lpstrText, CMenu * popup, UINT flags)
{
	return AppendPopup(CMenu::FromHandle(mnu), lpstrText, popup, flags);
}

BOOL
PopupMenuXP::AppendPopupW(LPCWSTR lpstrText, CMenu * popup, UINT flags)
{
	return AppendPopupW(CMenu::FromHandle(mnu), lpstrText, popup, flags);
}

BOOL 
PopupMenuXP::AppendPopup(CMenu * parent, LPCSTR lpstrText, CMenu * popup, UINT flags /*= 0*/)
{
	CStringW wstr = ::MbcsToWide(lpstrText, strlen_i(lpstrText));
	return AppendPopupW(parent, wstr, popup, flags);
}

BOOL PopupMenuXP::AppendPopupW(CMenu * parent, LPCWSTR lpstrText, CMenu * popup, UINT flags /*= 0*/)
{
	_ASSERTE(popup);
	UINT_PTR nID = (UINT_PTR)popup->m_hMenu;
	mPopups.push_back(popup);
	CMenuXP::SetXPLookNFeel(this, popup->m_hMenu);
	if (gTestLogger && gTestLogger->IsMenuLoggingEnabled())
	{
		gTestLogger->LogStrW(CStringW("MenuPopup: ") + lpstrText);
		int cnt = popup->GetMenuItemCount();
		for (int i = 0; i < cnt; ++i)
		{
			MENUITEMINFOW mii = { sizeof(MENUITEMINFOW), MIIM_STRING };
			WCHAR sCaption[256] = L"";
			mii.dwTypeData = sCaption;
			mii.cch = 255;
			if (::GetMenuItemInfoW(*popup, (uint)i, true, &mii))
			{
				CStringW txt(sCaption);
				if (::IsFile(txt))
					txt = ::Basename(txt);
				else
				{
					int pos = txt.Find('\t');
					if (-1 != pos)
					{
						CStringW tmp(txt.Left(pos));
						tmp.TrimRight();
						if (::IsFile(GetStringWithoutFormatting(tmp)))
						{
							tmp = ::Basename(tmp);
							tmp += txt.Mid(pos);
							txt = tmp;
						}
						else
						{
							// CStringW txt = basename + L"   " + FTM_DIM + dirname + FTM_NORMAL;
							int pos1 = tmp.Find(L"   ");
							if (-1 != pos)
							{
								int pos2 = tmp.Find(FTM_DIM);
								if (pos2 == pos1 + 3)
								{
									int pos3 = tmp.Find(FTM_NORMAL);
									if (pos3 == tmp.GetLength() - 1)
									{
										CStringW maybeFile(tmp.Mid(pos2 + 1));
										maybeFile = maybeFile.Left(maybeFile.GetLength() - 1);
										maybeFile += CStringW(L"\\") + tmp.Left(pos1);
										if (::IsFile(maybeFile))
											txt = ::Basename(maybeFile);
									}
								}
							}
						}
					}
				}
				EscapeMarkup(txt);
				txt.Replace(L" (beta)", L"");
				gTestLogger->LogStrW(txt);
			}
		}
	}
	return ::AppendMenuW(parent == nullptr ? mnu : *parent, MF_POPUP | flags, nID, lpstrText);
}

void
PopupMenuXP::CancelActiveMenu()
{
	if (sActiveMenu)
	{
		sActiveMenu->SendMessage(WM_CANCELMODE);
		::Sleep(150u);
	}
	if (sActiveMenu)
	{
		sActiveMenu->SendMessage(WM_CLOSE);
		::Sleep(150u);
	}
}

IMPLEMENT_MENUXP(PopupMenuXP, CStatic);

#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(PopupMenuXP, CStatic)
	ON_MENUXP_MESSAGES()
END_MESSAGE_MAP()
#pragma warning(pop)

void PopupMenuXP::AddItems(std::vector<MenuItemXP> & items, AddItemsEvents & events)
{
	return AddItems(CMenu::FromHandle(mnu), items, events);
}

void PopupMenuXP::AddItems(CMenu* parent, std::vector<MenuItemXP> & items, AddItemsEvents & events)
{
	PopupMenuXP & menu = *this;
	UINT index = 1;

	std::function<void(std::vector<MenuItemXP> & items, CMenu * parent)> generate_items;
	generate_items = [&index, &menu, &generate_items, &events](std::vector<MenuItemXP> & items, CMenu * parent_mnu)
	{
		for (MenuItemXP & mi : items)
		{
			if (mi.Items.empty())
			{
				if (events.adding_item)
					events.adding_item(mi, index, *parent_mnu);

				menu.AddMenuItemW(parent_mnu, index++, mi.Flags, mi.Text, mi.Icon);
			}
			else
			{
				auto sub_menu = std::make_unique<CMenu>();
				if (sub_menu->CreateMenu())
				{
					if (events.sub_menu)
						events.sub_menu(mi, *sub_menu);

					generate_items(mi.Items, sub_menu.get());

					if (menu.AppendPopupW(parent_mnu, mi.Text, sub_menu.get(), mi.Flags))
						sub_menu.release();
					else
						_ASSERTE(!"Failed to append popup!");	
				}
			}
		}
	};

	generate_items(items, parent ? parent : CMenu::FromHandle(mnu));
}

PopupMenuXP * PopupMenuXP::sActiveMenu = NULL;
std::stack<CPoint> PopupMenuXP::sReqPtStack;

int PopupMenuXP::sActiveMenuCnt = 0;


// MenuXpHook
// ----------------------------------------------------------------------------
//
MenuXpHook::MenuXpHook(CWnd* pWnd)
{
	g_menuDpi = VsUI::CDpiAwareness::GetDpiForWindow(CMenuXP::GetDpiSource(pWnd));
	g_hImgList = gImgListMgr->GetImgList(ImageListManager::bgMenu, g_menuDpi)->GetSafeHandle();
	g_menuFont.Update(g_menuDpi, VaFontType::MenuFont);

	_ASSERTE(g_menuFont.GetDPI() == g_menuDpi);

	CMenuXP::InitializeHook();
}

MenuXpHook::~MenuXpHook()
{
	CMenuXP::UninitializeHook();
	CMenuXP::ClearGlobals();
}

void AddSeparator(std::vector<MenuItemLmb> & items, bool force /*= false*/)
{
	if (force || (!items.empty() && !items.back().IsSeparator()))
		items.push_back(MenuItemLmb(MF_SEPARATOR));
}

void NormalizeSeparators(std::vector<MenuItemLmb> & items)
{
	if (items.empty())
		return;

	// remove multiplied separators in row (preserve one for each group) 
	for (size_t i = items.size() - 1; i > 0; --i)
		if (items[i].IsSeparator() && items[i - 1].IsSeparator())
			items.erase(items.begin() + (int)i);

	if (items.empty())
		return;

	// remove first item if it is separator
	if (items.front().IsSeparator())
		items.erase(items.cbegin());

	if (items.empty())
		return;

	// remove last item if it is separator
	if (items.back().IsSeparator())
		items.erase(items.cend() - 1);
}

void GenerateUniqueAccessKeys(std::vector<MenuItemLmb> & items, bool preserveUniqueExisting)
{
	std::map<WCHAR, std::vector<MenuItemLmb*>> groups;

	for (MenuItemLmb & item : items)
	{
		if ((item.Flags & MF_SEPARATOR) == MF_SEPARATOR)
			continue;

		if (!preserveUniqueExisting)
			groups[0].push_back(&item);
		else
		{
			WCHAR key = item.GetAccessKey();
			groups[key].push_back(&item);
		}
	}

	auto make_unique = [&](std::vector<MenuItemLmb*> & g, size_t start_at)
	{
		std::vector<int> vals;
		for (size_t x = start_at; x < g.size(); x++)
		{
			CStringW str = g[x]->ComparableText();

			vals.resize((uint)str.GetLength());
			int val = 0;
			for (uint i = 0; i < (size_t)str.GetLength(); i++, val++)
			{
				WCHAR ch = str[(int)i];
				vals[i] = INT_MAX;

				if (wt_isspace(ch))
					val = -1;
				else if (wt_isalnum(ch) && groups.find(ch) == groups.end())
				{
					if (wt_islower(ch))
						vals[i] = val + 100;
					else
						vals[i] = val + 1;
				}
			}

			int min = INT_MAX;
			int min_id = -1;
			for (size_t i = 0; i < vals.size(); i++)
			{
				if (min > vals[i])
				{
					min = vals[i];
					min_id = (int)i;
				}
			}

			if (min_id != -1)
			{
				WCHAR ch = str[min_id];
				min_id = str.Find(ch);
				str.Insert(min_id, '&');
				g[x]->SetText(str, true);
				groups[ch].push_back(g[x]);
			}
		}
	};

	for (auto & kvp : groups)
	{
		auto & group = kvp.second;

		if (!kvp.first || group.size() > 1)
		{
			make_unique(group, kvp.first ? 1u : 0u);
		}
	}
}

void GenerateAlnumAccessKeys(std::vector<MenuItemLmb> & items, bool preserveExisting /*= true*/, LPCWSTR bindSepar /*= L" "*/)
{
	CStringA accKeys = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	int i = 0;
	for (auto & item : items)
	{
		if ((item.Flags & MF_SEPARATOR) == MF_SEPARATOR)
			continue;

		if (preserveExisting && item.GetAccessKey(true))
			continue;

		item.RemoveAccessKey();

		CStringW binding;

		if (bindSepar == nullptr)
			item.SetKeyBinding((LPCWSTR)nullptr);
		else
			binding = item.GetKeyBinging();

		if (bindSepar && !binding.IsEmpty())
			binding.Append(bindSepar);

		binding.AppendChar('&');
		binding.AppendChar((wchar_t)accKeys[i++ % accKeys.GetLength()]);
		
		item.SetKeyBinding(binding);
	}
}

bool AccessKeysAreUnique(std::vector<MenuItemLmb> & items)
{
	std::map<WCHAR, std::vector<CStringW>> groups;

	for (MenuItemLmb & item : items)
	{
		if ((item.Flags & MF_SEPARATOR) == MF_SEPARATOR)
			continue;

		WCHAR key = item.GetAccessKey();

		if (key == 0)
		{
			_ASSERTE(!"Missing access key!");
		}

		groups[key].push_back(item.Text);
	}

	bool retVal = true;
	for (auto & kvp : groups)
	{
		auto & group = kvp.second;

		if (group.size() > 1)
		{
			_ASSERTE(!"Same access key several times!");
			retVal = false;
		}
	}

	return retVal;
}

bool PopupMenuLmb::Show(CWnd *parent, int x, int y, Events *events)
{
	std::vector<MenuItemLmb> _items;
	PopupMenuXP menu;

	if (events && events->InitMenu)
		events->InitMenu(menu);

	std::function<void(std::vector<MenuItemLmb> & items, CMenu * parent)> generate_items;
	generate_items = [&events, &menu, &generate_items, &_items](std::vector<MenuItemLmb> & items, CMenu * parent_mnu)
	{
		for (MenuItemLmb & mi : items)
		{
			if (mi.Items.empty())
			{
				_items.push_back(mi);

				bool add_item = true;

				if (events && events->AddingItem)
					add_item = events->AddingItem(mi, *parent_mnu);

				if (add_item)
				{
					if (mi.OnAdd)
						add_item = mi.OnAdd(mi);

					if (add_item)
						menu.AddMenuItemW(parent_mnu, _items.size(), mi.Flags, mi.Text, mi.Icon);
				}
			}
			else
			{
				auto sub_menu = std::make_unique<CMenu>();
				if (sub_menu->CreateMenu())
				{
					if (events && events->AddingPopup)
						events->AddingPopup(mi, *sub_menu);

					generate_items(mi.Items, sub_menu.get());

					if (menu.AppendPopupW(parent_mnu, mi.Text, sub_menu.get(), mi.Flags))
						sub_menu.release();
					else
						_ASSERTE(!"Failed to append popup!");
				}
			}
		}
	};

	generate_items(Items, CMenu::FromHandle(menu.mnu));

	if (events && events->Opening)
	{
		if (!events->Opening(menu))
			return false;
	}

	sActiveInstance = this;
	sActiveEvents = events;
	sActiveItems = &_items;

	bool executed = false;
	auto rslt = (uint)menu.TrackPopupMenuXP(parent, x, y);

	if (rslt && ((rslt - 1) < _items.size()))
	{
		if (events && events->ItemSelected)
			executed = events->ItemSelected(_items[rslt - 1]);
		else if (_items[rslt - 1].Command)
		{
			_items[rslt - 1].Command();
			executed = true;
		}
	}

	if (events && events->Closed)
		events->Closed(menu, (int)rslt);

	sActiveInstance = nullptr;
	sActiveEvents = nullptr;
	sActiveItems = nullptr;

	return executed;
}

void PopupMenuLmb::UpdateSelection(HMENU mnu, UINT_PTR item)
{
	if (sActiveEvents)
	{
		Events & events = *sActiveEvents;
		std::vector<MenuItemLmb> & _items = *sActiveItems;

		if (events.ItemHot && item && ((item - 1) < _items.size()))
			events.ItemHot(_items[item - 1]);
	}
}

void PopupMenuLmb::AddSeparator(bool force /*= false*/)
{
	::AddSeparator(Items, force);
}

void PopupMenuLmb::NormalizeSeparators()
{
	::NormalizeSeparators(Items);
}

bool PopupMenuLmb::HasUniqueAccessKeys()
{
	return ::AccessKeysAreUnique(Items);
}

bool PopupMenuLmb::GenerateUniqueAccessKeys( bool preserveUniqueExisting /*= true*/ )
{
	::GenerateUniqueAccessKeys(Items, preserveUniqueExisting);
	return HasUniqueAccessKeys();
}

void PopupMenuLmb::GenerateAlnumAccessKeys(bool preserveExisting /*= true*/, LPCWSTR bindSepar /*= L" "*/)
{
	::GenerateAlnumAccessKeys(Items, preserveExisting, bindSepar);
}

bool MenuItemLmb::HasUniqueAccessKeys()
{
	return AccessKeysAreUnique(Items);
}

bool MenuItemLmb::GenerateUniqueAccessKeys(bool preserveUniqueExisting /*= true*/)
{
	::GenerateUniqueAccessKeys(Items, preserveUniqueExisting);
	return HasUniqueAccessKeys();
}

void MenuItemLmb::GenerateAlnumAccessKeys(bool preserveExisting /*= true*/, LPCWSTR bindSepar /*= L" "*/)
{
	::GenerateAlnumAccessKeys(Items, preserveExisting, bindSepar);
}

void MenuItemLmb::AddSeparator(bool force /*= false*/)
{
	::AddSeparator(Items, force);
}

void MenuItemLmb::NormalizeSeparators()
{
	::NormalizeSeparators(Items);
}

std::vector<MenuItemLmb> * PopupMenuLmb::sActiveItems = nullptr;
PopupMenuLmb::Events * PopupMenuLmb::sActiveEvents = nullptr;
PopupMenuLmb * PopupMenuLmb::sActiveInstance = nullptr;


CMenuXPEffects::CMenuXPEffects(BYTE min_alpha, BYTE max_alpha, UINT fade_time, bool drop_shadow, bool img_margin)
: CMenuXPEffects(min_alpha, max_alpha, min_alpha, max_alpha, fade_time, drop_shadow, img_margin)
{

}

CMenuXPEffects::CMenuXPEffects(BYTE min_alpha, BYTE max_alpha, BYTE min_alpha_focus, BYTE max_alpha_focus, UINT fade_time /*= 200*/, bool drop_shadow /*= true*/, bool img_margin /*= true*/)
{
	if (sActiveEffects != nullptr)
	{
		_ASSERTE(!"Only one instance is allowed!!");
		return;
	}

	sActiveEffects = this;

	if (img_margin)
	{
		// #DPI_HELPER_UNRESOLVED

		img_width = VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16);
		img_height = VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(16);
		img_padding = 6;
	}
	else
	{
		img_width = 0;
		img_height = VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(16);
		img_padding = 0;
	}

	text_padding = 8;
	text_padding_mnubr = 4;

	m_MinAlpha = min_alpha;
	m_MaxAlpha = max_alpha;
	m_MinFocusAlpha = min_alpha_focus;
	m_MaxFocusAlpha = max_alpha_focus;

	m_AlphaCurrent = 0xFF;
	m_AlphaWnd = nullptr;

	m_FocusAlphaCurrent = 0xFF;
	m_FocusAlphaWnd = nullptr;

	if (fade_time <= MENUXP_TIMER_DURATION)
		m_FadeSteps = 1;
	else
	{
		m_FadeSteps = fade_time / MENUXP_TIMER_DURATION;

		if (m_FadeSteps < 1)
			m_FadeSteps = 1;
		else if (m_FadeSteps > 0xFF)
			m_FadeSteps = 0xFF;
	}

	m_transparency =
		(m_MinAlpha <= m_MaxAlpha && m_MinAlpha != 0xFF)
		||
		(m_MinFocusAlpha <= m_MaxFocusAlpha && m_MinFocusAlpha != 0xFF);

	m_supported = wvWin2000 <= ::GetWinVersion();
	if (m_supported && sActiveEffects == this)
	{
		if (m_transparency)
		{
			SystemParametersInfo(SPI_GETMENUFADE, 0, &bMenuFading, 0);
			SystemParametersInfo(SPI_SETMENUFADE, 0, (LPVOID)FALSE, SPIF_SENDWININICHANGE);
			SystemParametersInfo(SPI_GETMENUANIMATION, 0, &bMenuAnim, 0);
			SystemParametersInfo(SPI_SETMENUANIMATION, 0, (LPVOID)FALSE, SPIF_SENDWININICHANGE);
		}

		SystemParametersInfo(SPI_GETDROPSHADOW, 0, &bMenuShadow, 0);

		if ((bMenuShadow ? true : false) != drop_shadow)
		{
			m_shadow = true;
			SystemParametersInfo(SPI_SETDROPSHADOW, 0, (LPVOID)(uintptr_t)(drop_shadow ? TRUE : FALSE), SPIF_SENDWININICHANGE);
		}
	}
}


CMenuXPEffects::~CMenuXPEffects()
{
	if (m_supported && sActiveEffects == this)
	{
		if (m_transparency)
		{
			SystemParametersInfo(SPI_SETMENUFADE, 0, (LPVOID)(uintptr_t)bMenuFading, SPIF_SENDWININICHANGE);
			SystemParametersInfo(SPI_SETMENUANIMATION, 0, (LPVOID)(uintptr_t)bMenuAnim, SPIF_SENDWININICHANGE);
		}

		if (m_shadow)
			SystemParametersInfo(SPI_SETDROPSHADOW, 0, (LPVOID)(uintptr_t)bMenuShadow, SPIF_SENDWININICHANGE);
	}

	sActiveEffects = nullptr;
}

bool CMenuXPEffects::WndMnu_RemoveByHWND(HWND wnd)
{

	POSITION pos = m_hWndMnuList.GetTailPosition();
	while (pos)
	{
		WndMnu & wmRef = m_hWndMnuList.GetAt(pos);
		if (wmRef.wnd == wnd)
		{
			m_hWndMnuList.RemoveAt(pos);
			return true;
		}
		m_hWndMnuList.GetPrev(pos);
	}
	return false;
}

HWND CMenuXPEffects::WndMnu_GetHWND(HMENU menu)
{
	POSITION pos = m_hWndMnuList.GetTailPosition();
	while (pos)
	{
		WndMnu & wmRef = m_hWndMnuList.GetPrev(pos);
		if (wmRef.menu == menu)
			return wmRef.wnd;
	}
	return nullptr;
}

HMENU CMenuXPEffects::WndMnu_GetHMENU(HWND wnd)
{
	POSITION pos = m_hWndMnuList.GetTailPosition();
	while (pos)
	{
		WndMnu & wmRef = m_hWndMnuList.GetPrev(pos);
		if (wmRef.wnd == wnd)
			return wmRef.menu;
	}
	return nullptr;
}

HWND CMenuXPEffects::WndMnu_TailHWND()
{
	if (m_hWndMnuList.IsEmpty())
		return nullptr;
	return m_hWndMnuList.GetTail().wnd;
}

bool CMenuXPEffects::WndMnu_IsEmpty()
{
	return m_hWndMnuList.IsEmpty() ? true : false;
}

void CMenuXPEffects::WndMnu_Add(HWND w, HMENU m)
{
	m_hWndMnuList.AddTail(WndMnu(w, m));
}

void CMenuXPEffects::SetBestFocusWindow()
{
	MENUITEMINFO mii = { sizeof MENUITEMINFO, MIIM_STATE | MIIM_FTYPE };

	CPoint cursor;
	::GetCursorPos(&cursor);

	m_FocusAlphaWnd = m_AlphaWnd;

	POSITION pos = m_hWndMnuList.GetHeadPosition();
	while (pos)
	{
		WndMnu & wmRef = m_hWndMnuList.GetNext(pos);
		if (wmRef.wnd != m_AlphaWnd)
		{
			CRect wnd_rect;
			::GetWindowRect(wmRef.wnd, &wnd_rect);

			if (wnd_rect.PtInRect(cursor))
				m_FocusAlphaWnd = wmRef.wnd;
			else
			{
				uint count = (uint)::GetMenuItemCount(wmRef.menu);
				for (uint i = 0; i < count; i++)
				{
					::GetMenuItemInfo(wmRef.menu, i, true, &mii);
					if (mii.fState & MF_HILITE)
					{
						m_FocusAlphaWnd = wmRef.wnd;
						break;
					}
				}
			}
		}
	}
}

void CMenuXPEffects::OnWndDestroy(HWND hWnd)
{
	if (hWnd == m_AlphaWnd)
		m_AlphaWnd = nullptr;

	WndMnu_RemoveByHWND(hWnd);

	if (hWnd == m_AlphaWnd)
		m_FocusAlphaWnd = WndMnu_TailHWND();
}

CMenuXPEffects * CMenuXPEffects::sActiveEffects = nullptr;


struct AltKeyRepeatHook : public WindowsHooks::CHook
{
	bool stop_on_first;

	AltKeyRepeatHook(bool _stop_on_first) : stop_on_first(_stop_on_first)
	{ 
		BeginHook(WH_GETMESSAGE); 
	}
	
	virtual ~AltKeyRepeatHook()
	{ 
		EndHook(WH_GETMESSAGE); 
	}

	virtual void OnHookMessage(int id_hook, int code, UINT_PTR wParam, LPVOID lpHookMsg)
	{
		_ASSERTE(id_hook == WH_GETMESSAGE);
		if (code >= 0 && lpHookMsg)
		{
			MSG* msg = (MSG*)lpHookMsg;
			if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_MENU)
			{
				// kill any repeated ALT down message
				if ((HIWORD(msg->lParam) & KF_REPEAT) != 0)
				{
					msg->message = WM_NULL;
					msg->wParam = 0;
					msg->lParam = 0;
				}
				else if (stop_on_first)
				{
					EndHook(WH_GETMESSAGE); 
				}
			}
		}
	}
};

CMenuXPAltRepeatFilter::CMenuXPAltRepeatFilter(bool filter /*= true*/, bool single_shot /*= true*/) 
: m_hook(filter ? new AltKeyRepeatHook(single_shot) : nullptr)
{

}

CMenuXPAltRepeatFilter::~CMenuXPAltRepeatFilter()
{
	delete m_hook;
}

PopupMenuXP::ReqPt::ReqPt(const CPoint & pt)
{
	PopupMenuXP::sReqPtStack.push(pt);
}

PopupMenuXP::ReqPt::~ReqPt()
{
	PopupMenuXP::sReqPtStack.pop();
}

MenuItemLmb & MenuItemLmb::SetText(LPCWSTR txt, bool preserve_key_binding /*= false*/)
{
	if (!preserve_key_binding)
		Text = txt ? txt : L"";
	else
	{
		int pos = Text.Find('\t');

		if (pos >= 0)
			Text.Delete(0, pos);
		else
			Text.Empty();

		Text.Insert(0, txt ? txt : L"");
	}

	return *this;
}

MenuItemLmb & MenuItemLmb::SetKeyBinding(LPCWSTR binding)
{
	int pos = Text.Find('\t');

	if (pos >= 0)
		Text.Delete(pos, Text.GetLength() - pos);

	if (binding && *binding)
	{
		Text.AppendChar('\t');
		Text.Append(binding);
	}

	return *this;
}

MenuItemLmb & MenuItemLmb::SetKeyBindingForCommand(LPCSTR cmd, LPCSTR vc6binding /*= nullptr*/)
{
	WTString binding = GetBindingTip(cmd, vc6binding, FALSE);
	return SetKeyBinding(binding.Wide());
}

CStringW MenuItemLmb::GetKeyBinging() const
{
	int pos = Text.Find('\t');

	if (pos >= 0 && pos + 1 < Text.GetLength())
		return Text.Mid(pos + 1);

	return L"";
}

wchar_t MenuItemLmb::GetAccessKey(bool in_binding /*= false*/) const
{
	LPCWSTR and_pos = FindAccessKey((LPCWSTR)Text, true);

	if (and_pos)
		return and_pos[1];

	return 0;
}

void MenuItemLmb::RemoveAccessKey()
{
	LPCWSTR and_pos = FindAccessKey((LPCWSTR)Text, false);
	while (and_pos)
	{
		int index = ptr_sub__int(and_pos, (LPCWSTR)Text);
		Text.Delete(index);
		and_pos = FindAccessKey((LPCWSTR)Text + index, false);
	}
}

bool MenuItemLmb::SetAccessKey(int index)
{
	RemoveAccessKey();

	if (index >= 0 || index < Text.GetLength())
	{
		Text.Insert(index, '&');
		return true;
	}

	return false;
}

CStringW MenuItemLmb::ComparableText(bool preserveBinding /*= false*/, bool preserveAccessKey /*= false*/)
{
	CStringW str = Text;

	if (!preserveBinding)
	{
		int tab = str.Find('\t');
		if (tab >= 0)
			str.Delete(tab, str.GetLength() - tab);
	}

	if (!preserveAccessKey)
	{
		LPCWSTR and_pos = FindAccessKey((LPCWSTR)str, false);
		while (and_pos)
		{
			int index = ptr_sub__int(and_pos, (LPCWSTR)str);
			str.Delete(index);
			and_pos = FindAccessKey((LPCWSTR)str + index, false);
		}
	}

	return str;
}
