// ToolTipButton.cpp : implementation file
//

#include "stdafx.h"
#include "../resource.h"
#include "ToolTipButton.h"
#include "..\VaMessages.h"
#include "..\XButtonXP\XThemeHelper.h"
#include "..\DevShellAttributes.h"
#include "..\ColorListControls.h"
#include "vsshell100.h"
#include "..\IdeSettings.h"
#include "..\ImageListManager.h"
#include "..\DpiCookbook\VsUIDpiHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// ToolTipButton

ToolTipButton::ToolTipButton()
{
	if (IsVS2010DrawingEnabled())
	{
		m_bEnableTheming = false;
		//		m_bDrawToolbar = true;
	}
}

ToolTipButton::~ToolTipButton()
{
}

BEGIN_MESSAGE_MAP(ToolTipButton, CXButtonXP)
//{{AFX_MSG_MAP(ToolTipButton)
// NOTE - the ClassWizard will add and remove mapping macros here.
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// ToolTipButton message handlers

static void ChangeBitmapColour2(HBITMAP bitmap, CDC& tempdc, COLORREF srcclr, COLORREF destclr)
{
	// first pixel is used for transparency; we'll make sure our dest colour differs a bit
	tempdc.SelectObject(bitmap);
	if (destclr == tempdc.GetPixel(0, 0))
		destclr = RGB(GetRValue(destclr), GetGValue(destclr), GetBValue(destclr) ^ 1);

	static CBitmap dummy_bitmap;
	if (!dummy_bitmap.m_hObject)
		dummy_bitmap.LoadOEMBitmap(OBM_CHECK);
	tempdc.SelectObject(&dummy_bitmap); // load dummy bitmap since bitmap can be selected in only one DC

	ChangeBitmapColour(bitmap, srcclr, destclr);
}

BOOL ToolTipButton::Create(const RECT& rect, CWnd* pParent, UINT nID, UINT nIDStr)
{
	PrepareBitmaps();
	CString cap;
	cap.LoadString(nIDStr);
	BOOL ret = CXButtonXP::Create(cap, WS_VISIBLE | WS_CHILD /*|BS_ICON*/ | BS_FLAT | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
	                              rect, pParent, nID);
	SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (GetWindowLongPtr(m_hWnd, GWLP_USERDATA) | VA_WND_DATA));
	//	SetIcon(LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_GOTO)));
	SetWindowText("");
	m_bNoFocusRect = true;
	EnableDefault(false);
	EnableTheming(ThemeHelper.IsAppThemed() && ThemeHelper.IsThemeActive() && gShellAttr->IsDevenv());
	ShowWindow(SW_SHOW);
	return ret;
}

void ToolTipButton::DrawIcon(CDC* pDC, bool bHasText, CRect& _rectItem, CRect& rectText, bool bIsPressed,
                             bool bIsThemed, bool bIsDisabled)
{
	if (!bitmapsize.cx || !bitmapsize.cy)
		return;

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	CRect rectItem(_rectItem);
	rectItem.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(2), VsUI::DpiHelper::LogicalToDeviceUnitsY(2));

	//	if(IsThemed())
	rectItem.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
	if (bIsPressed && !IsVS2010DrawingEnabled())
		rectItem.OffsetRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));

	int savedc = pDC->SaveDC();
	pDC->SetStretchBltMode(HALFTONE);

	CDC tempdc;
	tempdc.CreateCompatibleDC(pDC);
	CBitmap tempbitmap;
	tempbitmap.CreateCompatibleBitmap(pDC, bitmapsize.cx, bitmapsize.cy);
	tempdc.SelectObject(&tempbitmap);
	tempdc.SetStretchBltMode(HALFTONE);

	// we need transparent blt + stretch with bilinear filtering; however, transparent blt doesn't do bilinear
	// filtering, so, we do this trick:
	// 1. we bring background to the size of our bitmap
	// 2. then we transparently paste our bitmap to the enlarged background
	// 3. and then we scale background to its original dimensions
	tempdc.StretchBlt(0, 0, bitmapsize.cx, bitmapsize.cy, pDC, rectItem.left, rectItem.top, rectItem.Width(),
	                  rectItem.Height(), SRCCOPY);
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		bitmapdc.SelectObject(bIsPressed ? &bitmap_pressed : &bitmap);
	else
		bitmapdc.SelectObject((bIsPressed || m_bMouseOverButton) ? &bitmap_pressed : &bitmap);
	COLORREF transparent = bitmapdc.GetPixel(0, 0);
	tempdc.TransparentBlt(0, 0, bitmapsize.cx, bitmapsize.cy, &bitmapdc, 0, 0, bitmapsize.cx, bitmapsize.cy,
	                      transparent);
	pDC->StretchBlt(rectItem.left, rectItem.top, rectItem.Width(), rectItem.Height(), &tempdc, 0, 0, bitmapsize.cx,
	                bitmapsize.cy, SRCCOPY);

	pDC->RestoreDC(savedc);
}

int ToolTipButton::CalculateWidthFromHeight(int height) const
{
	_ASSERTE(bitmap.m_hObject);

	if (bitmapsize.cy)
		return height * bitmapsize.cx / bitmapsize.cy;
	return 0;
}

bool ToolTipButton::IsVS2010DrawingEnabled() const
{
	return CVS2010Colours::IsVS2010NavBarColouringActive();
}

void ToolTipButton::PrepareBitmaps(BOOL init /*= TRUE*/)
{
	if (!init)
	{
		_ASSERTE(gShellAttr->IsDevenv11OrHigher());
		bitmapdc.DeleteDC();
		bitmap.DeleteObject();
		bitmap_pressed.DeleteObject();
	}

	// 	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
	// 	{
	// 		bitmap.LoadBitmap(IDB_GO11);
	// 		bitmap_pressed.LoadBitmap(IDB_GO11);
	// 	}
	// 	else
	{
		bitmap.LoadBitmap(IDB_GO);
		bitmap_pressed.LoadBitmap(IDB_GO);
	}

	bitmapdc.CreateCompatibleDC(NULL);
	bitmapdc.SelectObject(&bitmap);
	BITMAP bm;
	memset(&bm, 0, sizeof(bm));
	bitmap.GetObject(sizeof(bm), &bm);
	bitmapsize.SetSize(bm.bmWidth, bm.bmHeight);
	bitmapdc.SetStretchBltMode(HALFTONE);

	static const COLORREF text_colour_to_replace = RGB(255, 0, 255);
	static const COLORREF non_vs2010_text_colour = RGB(64, 64, 64);
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		ChangeBitmapColour2((HBITMAP)bitmap.m_hObject, bitmapdc, text_colour_to_replace,
		                    g_IdeSettings->GetEnvironmentColor(L"CommandBarTextActive", false));
		ChangeBitmapColour2((HBITMAP)bitmap_pressed.m_hObject, bitmapdc, text_colour_to_replace,
		                    g_IdeSettings->GetEnvironmentColor(L"CommandBarTextMouseDown", false));
	}
	else if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		ChangeBitmapColour2((HBITMAP)bitmap.m_hObject, bitmapdc, text_colour_to_replace, non_vs2010_text_colour);
		ChangeBitmapColour2((HBITMAP)bitmap_pressed.m_hObject, bitmapdc, text_colour_to_replace,
		                    CVS2010Colours::GetVS2010Colour(VSCOLOR_TOOLWINDOW_BUTTON_ACTIVE_GLYPH));
	}
	else
	{
		ChangeBitmapColour2((HBITMAP)bitmap.m_hObject, bitmapdc, text_colour_to_replace, non_vs2010_text_colour);
		ChangeBitmapColour2((HBITMAP)bitmap_pressed.m_hObject, bitmapdc, text_colour_to_replace,
		                    non_vs2010_text_colour);
	}
}
