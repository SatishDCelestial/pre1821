// ListCtrlFixedGrid.cpp : implementation file
//

#include "stdafxed.h"
#include "ListCtrlFixedGrid.h"
#include "DevShellAttributes.h"
#include "SyntaxColoring.h"

#include "TextOutDC.h"
#include "ColorSyncManager.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "FontSettings.h"
#include "MenuXP\Draw.h"
#include "PROJECT.H"
#include "IdeSettings.h"
#include "TraceWindowFrame.h"
#include "Settings.h"
#include "MenuXP\Tools.h"
#include "ImageListManager.h"
#include "StringUtils.h"

GUID GUID_HeaderCategory = {0x4997F547, 0x1379, 0x456E, {0xB9, 0x85, 0x2F, 0x41, 0x3C, 0xDF, 0xA5, 0x36}};
GUID GUID_CiderCategory = {0x92D153EE, 0x57D7, 0x431F, {0xA7, 0x39, 0x09, 0x31, 0xCA, 0x3F, 0x7F, 0x70}};

CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema::HeaderColorSchema()
{
	Default = GetSysColor(COLOR_BTNFACE);
	DefaultText = GetSysColor(COLOR_BTNTEXT);
	Glyph = GetSysColor(COLOR_BTNTEXT);
	MouseDown = GetSysColor(COLOR_BTNSHADOW);
	MouseDownGlyph = GetSysColor(COLOR_BTNTEXT);
	MouseDownText = GetSysColor(COLOR_BTNTEXT);
	MouseOver = GetSysColor(COLOR_BTNHIGHLIGHT);
	MouseOverGlyph = GetSysColor(COLOR_BTNTEXT);
	MouseOverText = GetSysColor(COLOR_BTNTEXT);
	SeparatorLine = GetSysColor(COLOR_ACTIVEBORDER);
}

COLORREF CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema::StateBGColor(RenderState rs) const
{
	if (rs == rs_MouseDown)
		return MouseDown;
	else if (rs == rs_MouseOver)
		return MouseOver;
	return Default;
}

COLORREF CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema::StateTextColor(RenderState rs) const
{
	if (rs == rs_MouseDown)
		return MouseDownText;
	else if (rs == rs_MouseOver)
		return MouseOverText;
	return DefaultText;
}

COLORREF CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema::StateGlyphColor(RenderState rs) const
{
	if (rs == rs_MouseDown)
		return MouseDownGlyph;
	else if (rs == rs_MouseOver)
		return MouseOverGlyph;
	return Glyph;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CListCtrlFixedGrid_HeaderCtrl, CHeaderCtrl)
ON_NOTIFY_REFLECT(HDN_BEGINTRACKA, &CListCtrlFixedGrid_HeaderCtrl::OnHdnBegintrack)
ON_NOTIFY_REFLECT(HDN_BEGINTRACKW, &CListCtrlFixedGrid_HeaderCtrl::OnHdnBegintrack)
ON_NOTIFY_REFLECT(HDN_ENDTRACKA, &CListCtrlFixedGrid_HeaderCtrl::OnHdnEndtrack)
ON_NOTIFY_REFLECT(HDN_ENDTRACKW, &CListCtrlFixedGrid_HeaderCtrl::OnHdnEndtrack)
ON_WM_ERASEBKGND()
ON_WM_MOUSEMOVE()
ON_WM_PAINT()
ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()
#pragma warning(pop)

void CListCtrlFixedGrid_HeaderCtrl::OnMouseLeave()
{
	hotItem = -1;
	hotState = rs_Default;

	Invalidate();
	UpdateWindow();

	__super::OnMouseLeave();
}

void CListCtrlFixedGrid_HeaderCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(point);

	if (!columnResizing)
	{
		HDHITTESTINFO hdHitIfo;
		memset(&hdHitIfo, 0, sizeof(HDHITTESTINFO));
		hdHitIfo.pt = point;
		int new_mouse_item = HitTest(&hdHitIfo);
		if (new_mouse_item != hotItem)
		{
			hotItem = new_mouse_item;
			hotState = rs_MouseOver;
			Invalidate();
			UpdateWindow();
		}
	}

	if (hotItem != -1)
	{
		TRACKMOUSEEVENT tme = {};
		tme.cbSize = sizeof(tme);
		tme.hwndTrack = m_hWnd;
		tme.dwFlags = TME_LEAVE;
		TrackMouseEvent(&tme);
	}

	__super::OnMouseMove(nFlags, point);
}

LRESULT CListCtrlFixedGrid_HeaderCtrl::WindowProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_MOUSELEAVE)
		UpdateRenderState(rs_Default);
	else if (msg == WM_LBUTTONDOWN)
		UpdateRenderState(rs_MouseDown);
	else if (msg == WM_MOUSEMOVE)
	{
		if (::GetKeyState(VK_LBUTTON) & 0x8000)
			UpdateRenderState(rs_MouseDown);
		else
			UpdateRenderState(rs_MouseOver);
	}
	else if (msg == WM_LBUTTONUP)
		UpdateRenderState(rs_MouseOver);

	if ((msg == WM_PAINT) || (msg == WM_DRAWITEM))
	{
		int old_in_WM_PAINT = PaintType::in_WM_PAINT;
		PaintType::in_WM_PAINT = PaintType::DontColor;

		LRESULT ret = CHeaderCtrl::WindowProc(msg, wparam, lparam);

		PaintType::in_WM_PAINT = old_in_WM_PAINT;
		return ret;
	}

	return CHeaderCtrl::WindowProc(msg, wparam, lparam);
}

BOOL CListCtrlFixedGrid_HeaderCtrl::OnEraseBkgnd(CDC* pDC)
{
	if (renderer == nullptr)
	{
		Default();
		return TRUE;
	}

	return renderer->EraseHeaderBG(pDC, colors) ? TRUE : FALSE;
}

void CListCtrlFixedGrid_HeaderCtrl::UpdateRenderState(RenderState rs)
{
	if (hotState != rs)
	{
		hotState = rs;
		Invalidate();
		UpdateWindow();
	}

#ifdef CListCtrlFixedGrid_WATERMARK // [case: 149836]
	if (CListCtrlFixedGrid::IsWatermarkEnabled())
	{
		auto parent = GetParent();
		if (parent)
		{
			parent->Invalidate(FALSE);
		}
	}
#endif
}

void CListCtrlFixedGrid_HeaderCtrl::OnPaint()
{
	if (renderer == nullptr)
	{
		Default();
		return;
	}

	CPaintDC dc(this); // device context for painting

	CRect rectItem;
	int nItems = GetItemCount();

	for (int i = 0; i < nItems; i++)
	{
		DRAWITEMSTRUCT DrawItemStruct;
		GetItemRect(i, &rectItem);

		DrawItemStruct.CtlType = 100;
		DrawItemStruct.hDC = dc.GetSafeHdc();
		DrawItemStruct.itemAction = ODA_DRAWENTIRE;
		DrawItemStruct.hwndItem = GetSafeHwnd();
		DrawItemStruct.rcItem = rectItem;
		DrawItemStruct.itemID = (UINT)i;

		renderer->DrawHeaderItem(&DrawItemStruct, i == hotItem ? hotState : rs_Default, colors);
	}
}

void CListCtrlFixedGrid_HeaderCtrl::OnHdnBegintrack(NMHDR* pNMHDR, LRESULT* pResult)
{
	columnResizing = true;
	*pResult = 0;
}

void CListCtrlFixedGrid_HeaderCtrl::OnHdnEndtrack(NMHDR* pNMHDR, LRESULT* pResult)
{
	columnResizing = false;
	*pResult = 0;

	// We need to redraw our ListView control due to
	// leaving glitches during Remote Desktop session.
	// See post 31.3.2014 14:22 by Sean in [case: 66901]
	HWND parent = ::GetParent(m_hWnd);
	if (parent && ::IsWindow(parent))
	{
		::InvalidateRect(parent, NULL, TRUE);
		::UpdateWindow(parent);
	}
}

// CListCtrlFixedGrid

IMPLEMENT_DYNAMIC(CListCtrlFixedGrid, CColorVS2010ListCtrl)

CListCtrlFixedGrid::CListCtrlFixedGrid()
{
	m_in_paintEvent = false;

	UseIDEThemeColors = false;

	HotItemIndex = -1;
	HotTrack = false;
	OwnerDraw = true;
	DrawIcons = true;
	SortArrow = false;
	SortReverse = false;
	SortColumn = -1;
	m_simpleListMode = false;

	SelectedItemSyntaxHL = SelectedItemNoFocusSyntaxHL = HotItemSyntaxHL = false;

	ThemeRendering = CXTheme::IsAppThemed();
	DrawFocusRectangle = false;

	//////////////////////////////////////////////////////////////////////////
	// define default colors to system color indices
	BorderColor = RGB(0, 0, 0);
	BorderColorFocused = BorderColor;

	ItemBGColor = ::GetSysColor(COLOR_WINDOW);
	ItemTextColor = ::GetSysColor(COLOR_WINDOWTEXT);

	GridLinesColor = ::GetSysColor(COLOR_INACTIVEBORDER);
	DrawGridLinesFlags = dgl_Vertical;

	SelectedItemBGColor = SelectedItemBGBorderColor = ::GetSysColor(COLOR_HIGHLIGHT);
	SelectedItemTextColor = ::GetSysColor(COLOR_HIGHLIGHTTEXT);

	SelectedItemNoFocusBGColor = SelectedItemNoFocusBGBorderColor = ::GetSysColor(COLOR_BTNFACE);
	SelectedItemNoFocusTextColor = ::GetSysColor(COLOR_BTNTEXT);

	HotItemOverlayBGColor = HotItemOverlayBGBorderColor = ::GetSysColor(COLOR_WINDOW);
	HotItemTextColor = ::GetSysColor(COLOR_HOTLIGHT);

	SelectedItemBGOpacity = 1.0f;
	SelectedItemNoFocusBGOpacity = 1.0f;
	HotItemBGOverlayOpacity = 1.0f;

	// [case: 9948]
	m_winxp_doublebuffer_fix = wvWinXP == ::GetWinVersion() && gShellAttr->IsDevenv();
}

CListCtrlFixedGrid::~CListCtrlFixedGrid()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CListCtrlFixedGrid, CColorVS2010ListCtrl)
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CListCtrlFixedGrid::OnNMCustomdraw)
// 	ON_NOTIFY(TTN_SHOW, 0, &CListCtrlFixedGrid::OnToolTipShow)
// 	ON_NOTIFY(TTN_POP, 0, &CListCtrlFixedGrid::OnToolTipPop)
ON_WM_CREATE()
ON_WM_ERASEBKGND()
ON_WM_HSCROLL()
ON_WM_MOUSELEAVE()
ON_WM_MOUSEMOVE()
ON_WM_NCPAINT()
ON_WM_PAINT()
ON_WM_VSCROLL()
ON_WM_TIMER()
ON_WM_SETFOCUS()
ON_WM_KILLFOCUS()
ON_WM_WINDOWPOSCHANGING()
END_MESSAGE_MAP()
#pragma warning(pop)

// CListCtrlFixedGrid message handlers

// void CListCtrlFixedGrid::OnToolTipShow(NMHDR *pNMHDR, LRESULT *pResult)
// {
//
// }
//
// void CListCtrlFixedGrid::OnToolTipPop(NMHDR *pNMHDR, LRESULT *pResult)
// {
//
// }

// Note: is not called!!!
int CListCtrlFixedGrid::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CColorVS2010ListCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;

	if (m_winxp_doublebuffer_fix)
		SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);

	headerctrl.SubclassWindow(GetHeaderCtrl()->m_hWnd);

	InitAppearanceSchema();
	return 0;
}

void CListCtrlFixedGrid::PreSubclassWindow()
{
	CColorVS2010ListCtrl::PreSubclassWindow();

	if (m_winxp_doublebuffer_fix)
		SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);

	headerctrl.SubclassWindow(GetHeaderCtrl()->m_hWnd);
	InitAppearanceSchema();
}

ULONG
CListCtrlFixedGrid::GetGestureStatus(CPoint ptTouch)
{
	// [case: 111020]
	// https://support.microsoft.com/en-us/help/2846829/how-to-enable-tablet-press-and-hold-gesture-in-mfc-application
	// https://connect.microsoft.com/VisualStudio/feedback/details/699523/tablet-pc-right-click-action-cannot-invoke-mfc-popup-menu
	return 0;
}

void CListCtrlFixedGrid::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	Default();

	if (m_winxp_doublebuffer_fix && ::GetKeyState(VK_LBUTTON) & 0x8000)
	{
		// Invalidate();
		UpdateWindow();
	}
}

void CListCtrlFixedGrid::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	Default();

	if (m_winxp_doublebuffer_fix && ::GetKeyState(VK_LBUTTON) & 0x8000)
	{
		// Invalidate();
		UpdateWindow();
	}
}

void DPDebugString(LPCTSTR lpszFormat, ...)
{
	// 	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));
	// 	WTString trace_str;
	// 	va_list argList;
	// 	va_start(argList, lpszFormat);
	// 	trace_str.FormatV(lpszFormat, argList);
	// 	va_end(argList);
	// 	TraceHelp th(trace_str);
}

afx_msg void CListCtrlFixedGrid::OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!m_in_paintEvent)
		return;

	NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	//	int iIndex = -1;

	// 	DPDebugString("stage=%x, subitem=%x, part=%x, state=%x, itemType=%x, iconPhase=%x, iconEffect=%x, uState=%d",
	// 		 pLVCD->nmcd.dwDrawStage, pLVCD->iSubItem, pLVCD->iPartId, pLVCD->iStateId, pLVCD->dwItemType,
	// pLVCD->iIconEffect, pLVCD->iIconPhase, pLVCD->nmcd.uItemState);

	// TRACE("\nhwnd=%x id=%d code=%d\n", pNMHDR->hwndFrom, pNMHDR->idFrom, pNMHDR->code);

	// OutputDebugStringA()

	*pResult = CDRF_DODEFAULT;

	if (OwnerDraw == false)
		return;

	if (CDDS_PREPAINT == pLVCD->nmcd.dwDrawStage)
	{
		//InvalidateVisibleArea();
		CustomDraw(pLVCD);
		*pResult = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
	}
	else if (CDDS_ITEMPREPAINT == pLVCD->nmcd.dwDrawStage)
	{
		CustomDraw(pLVCD);
		*pResult = CDRF_SKIPDEFAULT;
	}
	else if (CDDS_POSTPAINT == pLVCD->nmcd.dwDrawStage)
	{
		isInvalidated = false;
	}
}

void CListCtrlFixedGrid::CustomDraw(_In_ LPNMLVCUSTOMDRAW lpLVCD)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(this);

	TextOutDc DC, *pDC = &DC;
	AutoSaveRestoreDC asrdc(lpLVCD->nmcd.hdc);
	DC.Attach(lpLVCD->nmcd.hdc);

	CRect rcClient;
	GetClientRect(rcClient);

	do
	{
		// create clipping region
		CRgn clipRgn;
		clipRgn.CreateRectRgnIndirect(&rcClient);
		pDC->SelectClipRgn(&clipRgn);

		//////////////////////////////////////////////////////////////////////////
		// Colors and settings used across function
		COLORREF bg_color = ItemBGColor;
		COLORREF border_color = ItemBGColor;
		COLORREF grid_color = GridLinesColor;

		// Draw hints for case when UXTheme fails to draw
		bool draw_bg = true;
		bool draw_grid = !m_simpleListMode;
		bool ux_theme = !UseIDEThemeColors && ThemeRendering && Theme.IsOpen();

		if (!(lpLVCD->nmcd.dwDrawStage & CDDS_ITEM))
		{
			CBrush brush(ItemBGColor);
			::FillRect(lpLVCD->nmcd.hdc, rcClient, brush);

			//////////////////////////////////////////////////////////////////////////
			// Get rectangles for all columns
			CArray<CRect> item_rects;
			LV_COLUMN lvc;
			lvc.mask = LVCF_FMT | LVCF_WIDTH;
			CRect rcColumn = rcClient;
			rcColumn.left = rcColumn.right = -GetScrollPos(SB_HORZ);
			for (int nColumn = 0; GetColumn(nColumn, &lvc); nColumn++)
			{
				rcColumn.left = rcColumn.right;
				rcColumn.right = rcColumn.left + lvc.cx;
				item_rects.Add(rcColumn);
			}

			//////////////////////////////////////////////////////////////////////////
			// Apply UX theme if IDE supports it
			if (ux_theme)
			{
				//////////////////////////////////////////////////////////////////////////
				// draw grid
				for (int iCol = 0; iCol < item_rects.GetCount(); iCol++)
				{
					UINT bf = BF_MONO;
					if (DrawGridLinesFlags & dgl_Vertical)
						bf |= BF_RIGHT;

					draw_grid =
					    FAILED(Theme.DrawMonoInnerEdge(DC, LVP_LISTITEM, LISS_NORMAL, item_rects.GetAt(iCol), bf));

					// if one failed, draw all again later
					if (draw_grid)
						break;
				}
			}

			if (draw_grid)
			{
				CPen gridPen(0, 1, grid_color);
				AutoSelectGDIObj auto_gridPen(DC, gridPen);

				for (int iCol = 0; iCol < item_rects.GetCount(); iCol++)
				{
					const CRect& rcCol = item_rects.GetAt(iCol);

					if (DrawGridLinesFlags & dgl_Vertical)
					{
						DC.MoveTo(rcCol.right, rcCol.top);
						DC.LineTo(rcCol.right, rcCol.bottom);
					}
				}
			}

#ifdef CListCtrlFixedGrid_WATERMARK // [case: 149836]
			if (IsWatermarkEnabled() && tomatoBG.IsLoaded())
			{
				tomatoBGRect = rcClient;
				if (tomatoBGRect.Height() < tomatoBGRect.Width())
					tomatoBGRect.left = tomatoBGRect.right - tomatoBGRect.Height();
				else
					tomatoBGRect.top = tomatoBGRect.bottom - tomatoBGRect.Width();

				auto tbsize = tomatoBGRect.Height();
				tbsize = tbsize / 3 * 2;

				auto sizeDiff = tomatoBGRect.Height() - tbsize;
				tomatoBGRect.left += sizeDiff / 2;
				tomatoBGRect.top += sizeDiff / 2;
				tomatoBGRect.right -= sizeDiff / 2;
				tomatoBGRect.bottom -= sizeDiff / 2;
				tbsize = tomatoBGRect.Width();

				if (tomatoBGProcessed.IsLoaded() && tomatoBGProcessed.GetWidth() != tbsize)
					tomatoBGProcessed.Release();

				if (!tomatoBGProcessed.IsLoaded())
				{
					tomatoBGProcessed.Create(tbsize, tbsize);
					CAutoPtr<Gdiplus::Graphics> graphics(tomatoBGProcessed.GetGraphics());
					graphics->SetSmoothingMode(Gdiplus::SmoothingModeNone);
					graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
					graphics->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
					graphics->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);

					auto alphaByte = (BYTE)(Psettings->mWatermarkProps);
					auto desaturationByte = (BYTE)(Psettings->mWatermarkProps >> 8);
					auto valueByte = (BYTE)(Psettings->mWatermarkProps >> 16);

					float opacity = alphaByte / 255.0f;                    // opacity in range 0 - 1
					float saturation = 1.0f - (desaturationByte / 255.0f); // saturation in range 0 - 1
					float offset = valueByte / 255.0f;                     // value added to RGB components in range 0 - 1

					// remove half of opacity for white background, none for black
					float bgBrightness = ThemeUtils::ColorBrightness(bg_color) / 255.0f;
					opacity -= opacity * bgBrightness * .5f;

					// calculate RGB ranges
					float red0 = std::lerp(0.299f, 0.0f, saturation);
					float green0 = std::lerp(0.587f, 0.0f, saturation);
					float blue0 = std::lerp(0.114f, 0.0f, saturation);

					float red1 = std::lerp(0.299f, 1.0f, saturation);
					float green1 = std::lerp(0.587f, 1.0f, saturation);
					float blue1 = std::lerp(0.114f, 1.0f, saturation);

					Gdiplus::ColorMatrix matrix =
						{{{red1, red0, red0, 0, 0},
						    {green0, green1, green0, 0, 0},
						    {blue0, blue0, blue1, 0, 0},
						    {0, 0, 0, opacity, 0},
						    {offset, offset, offset, 0, 1}}};

					Gdiplus::ImageAttributes attrib;
					attrib.SetColorMatrix(&matrix);
					auto bmp = tomatoBG.GetBitmap();

					graphics->DrawImage(bmp,
						                Gdiplus::Rect(0, 0, tbsize, tbsize),
						                0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(),
						                Gdiplus::UnitPixel, &attrib);
					graphics.Free();
				}

				if (tomatoBGProcessed.IsLoaded())
				{
					CAutoPtr<Gdiplus::Graphics> gr(Gdiplus::Graphics::FromHDC(lpLVCD->nmcd.hdc));
					gr->DrawImage(tomatoBGProcessed.GetBitmap(), 
						tomatoBGRect.left, tomatoBGRect.top, 
						0, 0, tomatoBGRect.Width(), tomatoBGRect.Height(), Gdiplus::UnitPixel);
				}
			}
#endif

			break;
		}

		// list item index
		int nItem = (int)lpLVCD->nmcd.dwItemSpec;

		//////////////////////////////////////////////////////////////////////////
		// Get rectangles for drawing
		CRect rcBounds, rcIcon, rcLabel;
		GetItemRect(nItem, rcBounds, LVIR_BOUNDS);
		GetItemRect(nItem, rcIcon, LVIR_ICON);
		GetItemRect(nItem, rcLabel, LVIR_LABEL);

		// if item rect is empty or out of range, ignore it
		if (rcBounds.Height() == 0 || rcBounds.top > rcClient.bottom || rcBounds.bottom < rcClient.top)
			break;

		// draw default BG
		CRect rcDefaultBG = rcBounds;
		// if item is last in list, extend rect to bottom of client
		if (nItem == GetItemCount() - 1)
			rcDefaultBG.bottom = rcClient.bottom;
		rcDefaultBG.left = rcClient.left;
		rcDefaultBG.right = rcClient.right;

		DC.FillSolidRect(rcDefaultBG, ItemBGColor);

		//////////////////////////////////////////////////////////////////////////
		// get info about current item
		LV_ITEM lvi;
		lvi.mask = LVIF_IMAGE | LVIF_STATE; // | LVIF_TEXT;
		lvi.iItem = nItem;
		lvi.iSubItem = 0;
		lvi.stateMask = 0xFFFF; // get all state flags
		GetItem(&lvi);

		// item states
		bool bDropHilite = (lvi.state & LVIS_DROPHILITED) == LVIS_DROPHILITED;
		bool bSelect = (lvi.state & LVIS_SELECTED) == LVIS_SELECTED;
		bool bFocus = DrawFocusRectangle && ((lvi.state & LVIS_FOCUSED) == LVIS_FOCUSED);
		bool bGotFocus = GetFocus() == this;
		bool bHot = HotTrack && HotItemIndex == nItem;

		//////////////////////////////////////////////////////////////////////////
		// Get rectangles for all sub items in current row
		CArray<CRect> item_rects;
		LV_COLUMN lvc;
		lvc.mask = LVCF_FMT | LVCF_WIDTH;
		CRect rcColumn = rcLabel;
		rcColumn.left = rcColumn.right = lpLVCD->nmcd.rc.left;

		if (m_simpleListMode)
		{
			rcBounds.right = rcClient.right;
			if (rcLabel.right > rcClient.right)
				rcLabel.right = rcClient.right;

			item_rects.Add(rcBounds);
		}
		else
		{
			for (int nColumn = 0; GetColumn(nColumn, &lvc); nColumn++)
			{
				rcColumn.left = rcColumn.right;
				rcColumn.right = rcColumn.left + lvc.cx;
				item_rects.Add(rcColumn);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Apply UX theme if IDE supports it
		if (ux_theme)
		{
			int BG_state = bDropHilite ? LISS_SELECTED
			                           : (bSelect ? (bGotFocus ? LISS_SELECTED : LISS_SELECTEDNOTFOCUS) : LISS_NORMAL);
			if (bHot)
				BG_state = (BG_state == LISS_NORMAL) ? LISS_HOT : LISS_HOTSELECTED;

			//////////////////////////////////////////////////////////////////////////
			// draw background
			bool omit_content = false;
			if (bFocus && bGotFocus)
			{
				if (BG_state == LISS_SELECTED)
					BG_state = LISS_HOTSELECTED;
				else if (BG_state == LISS_NORMAL)
				{
					BG_state = LISS_SELECTED;
					omit_content = true;
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// draw grid
			if (!m_simpleListMode)
			{
				for (int iCol = 0; iCol < item_rects.GetCount(); iCol++)
				{
					UINT bf = BF_MONO;
					if (DrawGridLinesFlags & dgl_Vertical)
						bf |= BF_RIGHT;
					if (DrawGridLinesFlags & dgl_Horizontal)
					{
						if (nItem > 0)
							bf |= BF_TOP;
						if (nItem == GetItemCount() - 1)
							bf |= BF_BOTTOM;
					}

					draw_grid = FAILED(Theme.DrawMonoInnerEdge(DC, LVP_LISTITEM, BG_state, item_rects.GetAt(iCol), bf));

					// if one failed, draw all again later
					if (draw_grid)
						break;

					// if item is last in list, extend grid lines to the bottom of client area
					if (nItem == GetItemCount() - 1)
					{
						CRect remaining_rect = item_rects.GetAt(iCol);
						remaining_rect.top = remaining_rect.bottom;
						remaining_rect.bottom = rcClient.bottom;
						Theme.DrawMonoInnerEdge(DC, LVP_LISTITEM, BG_state, remaining_rect, bf & ~(BF_TOP | BF_BOTTOM));
					}
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// draw theme item BG
			// if this fails, we will draw BG using set colors
			if (Theme.IsThemePartDefined(LVP_LISTITEM, BG_state))
			{
				draw_bg = FAILED(Theme.DrawBGEx(DC, LVP_LISTITEM, BG_state, rcBounds, false, false, omit_content));
				draw_grid = draw_bg && !m_simpleListMode;
			}

			// if Theme failed to draw BG, use system colors to do it
			if (draw_bg)
			{
				draw_bg = bDropHilite || bSelect || bHot;

				if (draw_bg)
					draw_grid = true;
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// fully customized background drawing
		// - uses OS or IDE colors to draw
		if (draw_bg)
		{
			if (bDropHilite || bSelect)
			{
				if (bGotFocus)
				{
					bg_color = InterpolateColor(bg_color, SelectedItemBGColor, SelectedItemBGOpacity);
					border_color = InterpolateColor(border_color, SelectedItemBGBorderColor, SelectedItemBGOpacity);

					if (SelectedItemBGOpacity != 1.0f)
						grid_color = InterpolateColor(grid_color, SelectedItemBGColor, SelectedItemBGOpacity);
				}
				else
				{
					bg_color = InterpolateColor(bg_color, SelectedItemNoFocusBGColor, SelectedItemNoFocusBGOpacity);
					border_color =
					    InterpolateColor(border_color, SelectedItemNoFocusBGBorderColor, SelectedItemBGOpacity);

					if (SelectedItemNoFocusBGOpacity != 1.0f)
						grid_color =
						    InterpolateColor(grid_color, SelectedItemNoFocusBGColor, SelectedItemNoFocusBGOpacity);
				}
			}

			if (bHot)
			{
				bg_color = InterpolateColor(bg_color, HotItemOverlayBGColor, HotItemBGOverlayOpacity);
				border_color = InterpolateColor(border_color, HotItemOverlayBGBorderColor, HotItemBGOverlayOpacity);

				if (HotItemBGOverlayOpacity != 1.0f)
					grid_color = InterpolateColor(grid_color, HotItemOverlayBGColor, HotItemBGOverlayOpacity);
			}

			DC.FillSolidRect(rcBounds, bg_color);

			if (border_color != bg_color)
			{
				CBrush border(border_color);
				DC.FrameRect(rcBounds, &border);
			}
		}

#ifdef CListCtrlFixedGrid_WATERMARK // [case: 149836]
		if (IsWatermarkEnabled() && tomatoBGProcessed.IsLoaded())
		{
			CRect tbgRect;
			if (tbgRect.IntersectRect(&rcDefaultBG, tomatoBGRect))
			{
				int srcTop = tbgRect.top - tomatoBGRect.top;
				CAutoPtr<Gdiplus::Graphics> gr(Gdiplus::Graphics::FromHDC(DC.m_hDC));
				gr->DrawImage(tomatoBGProcessed.GetBitmap(), tbgRect.left, tbgRect.top, 0, srcTop, tbgRect.Width(), tbgRect.Height(), Gdiplus::UnitPixel);
			}
		}
#endif

		//////////////////////////////////////////////////////////////////////////
		// Grid / Columns
		if (draw_grid && !m_simpleListMode)
		{
			CPen gridPen(0, 1, grid_color);
			AutoSelectGDIObj auto_gridPen(DC, gridPen);

			for (int iCol = 0; iCol < item_rects.GetCount(); iCol++)
			{
				const CRect& rcCol = item_rects.GetAt(iCol);

				if (DrawGridLinesFlags & dgl_Vertical)
				{
					DC.MoveTo(rcCol.right, rcCol.top);
					DC.LineTo(rcCol.right, rcCol.bottom);
				}

				// if item is last in list, extend grid lines to the bottom of client area
				if (nItem == GetItemCount() - 1)
				{
					CPen defaultGrid(0, 1, GridLinesColor);
					AutoSelectGDIObj auto_defaultGrid(DC, defaultGrid);
					DC.MoveTo(rcCol.right, rcCol.bottom);
					DC.LineTo(rcCol.right, rcClient.bottom);
				}

				if (DrawGridLinesFlags & dgl_Horizontal)
				{
					if (nItem > 0)
					{
						DC.MoveTo(rcCol.left, rcCol.top);
						DC.LineTo(rcCol.right, rcCol.top);
					}
					if (nItem == GetItemCount() - 1)
					{
						DC.MoveTo(rcCol.left, rcCol.bottom);
						DC.LineTo(rcCol.right, rcCol.bottom);
					}
				}
			}
		}

		if (item_rects.GetCount() == 0)
			break;

		//////////////////////////////////////////////////////////////////////////
		// Labels are offset by a certain amount
		// This offset is related to the width of a space character
		LONG item_offset = pDC->GetTextExtent(_T(" "), 1).cx;

		//////////////////////////////////////////////////////////////////////////
		// Icon
		CImageList* pImageList = GetImageList(LVSIL_SMALL);
		if (DrawIcons && pImageList)
		{
			CImageList* imgList = pImageList;

			IMAGEINFO imgNfo = {};
			imgList->GetImageInfo(lvi.iImage, &imgNfo);

			// use a rect already allocated in imgNfo
			Rect_CenterAlign(rcIcon, &imgNfo.rcImage);

			const CRect& rcCol = item_rects.GetAt(0);

			if (imgNfo.rcImage.right > (rcCol.right - item_offset))
				imgNfo.rcImage.right = (rcCol.right - item_offset);

			CSize img_size(imgNfo.rcImage.right - imgNfo.rcImage.left, imgNfo.rcImage.bottom - imgNfo.rcImage.top);

			if (img_size.cx > 0 && img_size.cy > 0)
			{
				CPoint imgPt(imgNfo.rcImage.left, imgNfo.rcImage.top);
#ifdef _WIN64
				if ((bDropHilite || bSelect) && bGotFocus && !(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()))
				{
					auto pMon = ImageListManager::GetMoniker(lvi.iImage, true, true);
					bool image_resolved = false;
					if (pMon)
					{
						CBitmap bmp;
						if (SUCCEEDED(ImageListManager::GetMonikerImage(bmp, *pMon, bg_color, 0)))
						{
							image_resolved = DrawImage(*pDC, bmp, imgPt);
						}
					}

					if (!image_resolved)
					{
						// this is here to draw images uncovered by monikers
						DrawImageThemedForBackground(*pDC, imgList, lvi.iImage, imgPt, bg_color);
					}
				}
				else
#endif // _WIN64
				{
					imgList->DrawEx(pDC, lvi.iImage, imgPt, img_size, CLR_NONE, CLR_NONE,
					                ILD_TRANSPARENT); // do not blend icons, makes VS.NET2005 icons look bad
				}
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Text
		COLORREF text_color = ItemTextColor;
		bool syntaxHL = true;

		if (bDropHilite || bSelect)
		{
			if (bGotFocus)
			{
				text_color = SelectedItemTextColor;
				syntaxHL = SelectedItemSyntaxHL;
			}
			else
			{
				text_color = SelectedItemNoFocusTextColor;
				syntaxHL = SelectedItemNoFocusSyntaxHL;
			}
		}
		else if (bHot)
		{
			text_color = HotItemTextColor;
			syntaxHL = HotItemSyntaxHL;
		}

		if (syntaxHL)
			syntaxHL = ::myGetProp(m_hWnd, "__VA_do_not_colour") == 0;

		CPen pen(0, 1, grid_color);
		AutoSelectGDIObj auto_pen(DC, pen);
		AutoTextColor atc(DC, text_color, TRANSPARENT);

		for (int iCol = 0; iCol < item_rects.GetCount(); iCol++)
		{
			const CRect& rcCol = item_rects.GetAt(iCol);

			CStringW subItemText = GetItemTextW(nItem, iCol);
			if (subItemText.GetLength() == 0)
				continue;

			CRect rcSubItemText = rcCol;
			BOOL applyColorToColumn = syntaxHL;
			if (applyColorToColumn)
			{
				WTString propName;
				propName.WTFormat("__VA_do_not_colourCol%d", iCol);
				applyColorToColumn = ::myGetProp(m_hWnd, propName) == nullptr;
			}

			if (iCol == 0)
			{
				if (DrawIcons)
					rcSubItemText.left = rcLabel.left;
				else
					rcSubItemText.left = rcIcon.left;
			}

			if (rcSubItemText.right > rcClient.right)
				rcSubItemText.right = rcClient.right;

			rcSubItemText.left += item_offset * 2;
			rcSubItemText.right -= item_offset;

			if (!applyColorToColumn)
			{
				TmpNoHL no_hl;
				VaDrawSingleLineTextWithEndEllipsisW(lpLVCD->nmcd.hdc, subItemText, rcSubItemText,
				                                     DT_VCENTER | DT_NOPREFIX);
			}
			else
			{
				VaDrawSingleLineTextWithEndEllipsisW(lpLVCD->nmcd.hdc, subItemText, rcSubItemText,
				                                     DT_VCENTER | DT_NOPREFIX);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Focus rectangle
		if (bFocus && bGotFocus && DrawFocusRectangle)
		{
			CRect focusRect = rcBounds;
			focusRect.InflateRect(-1, -1);
			DrawFocusRect(DC, focusRect);
		}
	} while (false);

	pDC->Detach();
}

void CListCtrlFixedGrid::InvalidateVisibleArea(bool force /*= false*/)
{
	if (isInvalidated && !force)
		return;

	isInvalidated = true;

	CRect client;
	GetClientRect(&client);
	CRect hdrRect;
	headerctrl.GetWindowRect(&hdrRect);
	ScreenToClient(&hdrRect);
	client.top = hdrRect.bottom;
	InvalidateRect(&client, FALSE);
}

void CListCtrlFixedGrid::ProcessNewHotItem(int new_hot_item)
{
	if (!HotTrack)
		return;

	if (new_hot_item != HotItemIndex)
	{
		if (HotItemIndex >= 0)
		{
			RECT old_rect;
			GetItemRect(HotItemIndex, &old_rect, LVIR_BOUNDS);

			RECT cr;
			GetClientRect(&cr);
			old_rect.right = cr.right;

			InvalidateRect(&old_rect, FALSE);
		}

		if (new_hot_item >= 0)
		{
			RECT new_rect;
			GetItemRect(HotItemIndex, &new_rect, LVIR_BOUNDS);

			RECT cr;
			GetClientRect(&cr);
			new_rect.right = cr.right;

			InvalidateRect(&new_rect, FALSE);
		}

		HotItemIndex = new_hot_item;
		UpdateWindow();
	}
}

void CListCtrlFixedGrid::OnMouseMove(UINT nFlags, CPoint point)
{
	if (HotTrack)
	{
		UINT flags;
		ProcessNewHotItem(HitTest(point, &flags));
	}

	UpdateWindow();
	CColorVS2010ListCtrl::OnMouseMove(nFlags, point);
}

void CListCtrlFixedGrid::OnMouseLeave()
{
	if (HotTrack)
		ProcessNewHotItem(-1);

	CColorVS2010ListCtrl::OnMouseLeave();
}

CStringW CListCtrlFixedGrid::GetItemTextW(int nItem, int nSubItem) const
{
	LVITEMW lvi;
	memset(&lvi, 0, sizeof(LVITEMW));
	lvi.iSubItem = nSubItem;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		lvi.cchTextMax = nLen;
		lvi.pszText = str.GetBufferSetLength(nLen);
		nRes = (int)::SendMessageW(m_hWnd, LVM_GETITEMTEXTW, (WPARAM)nItem, (LPARAM)&lvi);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

bool CListCtrlFixedGrid::EraseHeaderBG(CDC* dcP, const CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema& colors)
{
	CRect rcClient;
	headerctrl.GetClientRect(rcClient);

	// draw background
	CBrush backBrush(colors.Default);
	dcP->FillRect(rcClient, &backBrush);

	// draw bottom line
	CPen borderPen(0, 1, colors.SeparatorLine);
	AutoSelectGDIObj bPen(*dcP, borderPen);
	dcP->MoveTo(rcClient.left, rcClient.bottom - 1);
	dcP->LineTo(rcClient.right, rcClient.bottom - 1);

	return true; // we have processed event
}

// draw a VA-style arrow that can be seen elsewhere as well
void DrawArrow(CDC* pDC, CRect rect, bool reversed, COLORREF glyphColor)
{
	// center point for the glyph
	CPoint origin = rect.CenterPoint();

	// We need nice pyramid, so: base = 2 * height AND height = base / 2
	int base = __min(rect.Height(), VsUI::DpiHelper::LogicalToDeviceUnitsY(6));

	// We deal with pixels now, so calculate everything else to ensure nice pyramid
	int rightX = base / 2;
	int leftX = -rightX;
	int baseY = rightX / 2;
	int spikeY = baseY - rightX;

	// for down arrow, invert base and spike values
	if (reversed)
	{
		baseY = -baseY;
		spikeY = -spikeY;
	}

	// create a pyramid polygon
	POINT pts[4];
	pts[0].x = origin.x + leftX;
	pts[0].y = origin.y + baseY; // left base
	pts[1].x = origin.x;
	pts[1].y = origin.y + spikeY; // spike
	pts[2].x = origin.x + rightX;
	pts[2].y = origin.y + baseY; // right base
	pts[3].x = origin.x + leftX;
	pts[3].y = origin.y + baseY; // left base

	// draw the glyph
	CPenDC penDC(*pDC, glyphColor, 1);
	CBrushDC dc(*pDC, glyphColor);
	pDC->Polygon(pts, 4);
}

void CListCtrlFixedGrid::DrawHeaderItem(LPDRAWITEMSTRUCT lpDrawItemStruct,
                                        CListCtrlFixedGrid_HeaderCtrl::RenderState rs,
                                        const CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema& colors)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	// This code only works with header controls.
	ASSERT(lpDrawItemStruct->CtlType == ODT_HEADER);

	TextOutDc DC, *pDC = &DC;
	DC.Attach(lpDrawItemStruct->hDC);

	HDITEM hdi;
	TCHAR lpBuffer[256];

	hdi.mask = HDI_TEXT;
	hdi.pszText = lpBuffer;
	hdi.cchTextMax = 256;

	LPRECT rect = &lpDrawItemStruct->rcItem;

	headerctrl.GetItem((int)lpDrawItemStruct->itemID, &hdi);

	//	LONG height = rect->bottom - rect->top;
	//	LONG sizeY = height / 6;
	LONG spaceExtent = DC.GetTextExtent(_T(" "), 1).cx;
	LONG glyphWidth = 0;

	// Draw the button frame.
	{
		// draw background
		CBrush backBrush(colors.StateBGColor(rs));
		DC.FillRect(rect, &backBrush);

		// draw separator line
		CPen borderPen(0, 1, colors.SeparatorLine);
		AutoSelectGDIObj bPen(DC, borderPen);

		if (lpDrawItemStruct->itemID)
		{
			DC.MoveTo(rect->left, rect->top);
			DC.LineTo(rect->left, rect->bottom - 1);

			if ((int)lpDrawItemStruct->itemID == headerctrl.GetItemCount() - 1)
			{
				DC.MoveTo(rect->right, rect->top);
				DC.LineTo(rect->right, rect->bottom - 1);
			}
		}

		// draw bottom line
		DC.MoveTo(rect->left, rect->bottom - 1);
		DC.LineTo(rect->right, rect->bottom - 1);
	}

	// drawing the arrow that shows the column sorting direction
	if (SortArrow && lpDrawItemStruct->itemID == (UINT)SortColumn && GetItemCount() > 0)
	{
		CRect arect = rect;
		glyphWidth = __min(arect.Height(), VsUI::DpiHelper::LogicalToDeviceUnitsY(6));
		arect.left = arect.right - glyphWidth - 4 * spaceExtent;

		if (rs == CListCtrlFixedGrid_HeaderCtrl::rs_MouseDown)
			arect.OffsetRect(1, 1);

		DrawArrow(pDC, arect, SortReverse, colors.StateGlyphColor(rs));
	}

	// Draw the items text
	{
		AutoTextColor atc(DC, colors.StateTextColor(rs));
		AutoSelectGDIObj afont(DC, GetDpiAwareFont());
		CRect txtRect(&lpDrawItemStruct->rcItem);

		CRect rcClient;
		GetClientRect(&rcClient);

		if (txtRect.right > rcClient.right)
			txtRect.right = rcClient.right;

		txtRect.left += 2 * spaceExtent;
		txtRect.right -= glyphWidth ? glyphWidth + 3 * spaceExtent : 0;

		if (rs == CListCtrlFixedGrid_HeaderCtrl::rs_MouseDown)
			txtRect.OffsetRect(1, 1);

		{
			TmpNoHL no_hl;
			CStringW item_text = lpBuffer;
			VaDrawSingleLineTextWithEndEllipsisW(lpDrawItemStruct->hDC, item_text, txtRect, DT_SINGLELINE | DT_VCENTER);
		}
	}

	DC.Detach();
}

void CListCtrlFixedGrid::InitAppearanceSchema()
{
	auto dpiScope = SetDefaultDpiHelper();

	if (!UseIDEThemeColors || g_IdeSettings == NULL)
	{
		::VsScrollbarTheme(m_hWnd, FALSE);
		gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgOsWindow, LVSIL_SMALL);
		headerctrl.renderer = this;
	}

	//////////////////////////////////////////////////////////////////////////
	// use colors of IDE theme (VS2012 and above)
	// this option has priority
	if (UseIDEThemeColors && g_IdeSettings)
	{
		// theme scrollbars
		::VsScrollbarTheme(m_hWnd);
		gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgList, LVSIL_SMALL);

		// we do not want client edge around control, but nice border
		ModifyStyle(0, WS_BORDER);
		ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);

		BorderColor = g_IdeSettings->GetEnvironmentColor(L"ControlOutline", FALSE);
		BorderColorFocused = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);

		ItemBGColor = g_IdeSettings->GetThemeColor(GUID_CiderCategory, L"ListItem", FALSE);
		ItemTextColor = g_IdeSettings->GetThemeColor(GUID_CiderCategory, L"ListItem", TRUE);

		SelectedItemBGColor = g_IdeSettings->GetThemeTreeColor(L"SelectedItemActive", FALSE);
		SelectedItemBGBorderColor = SelectedItemBGColor;
		if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
			SelectedItemBGBorderColor = g_IdeSettings->GetThemeTreeColor(L"FocusVisualBorder", FALSE);
		SelectedItemTextColor = g_IdeSettings->GetThemeTreeColor(L"SelectedItemActive", TRUE);
		SelectedItemSyntaxHL = false;

		SelectedItemNoFocusBGBorderColor = g_IdeSettings->GetThemeTreeColor(L"SelectedItemInactive", FALSE);
		SelectedItemNoFocusBGColor = SelectedItemNoFocusBGBorderColor;
		SelectedItemNoFocusTextColor = g_IdeSettings->GetThemeTreeColor(L"SelectedItemInactive", TRUE);
		SelectedItemNoFocusSyntaxHL = false;

		HotTrack = false;

		GridLinesColor = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"SeparatorLine", FALSE);

		headerctrl.renderer = this;
		headerctrl.colors.Default = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"Default", FALSE);
		headerctrl.colors.DefaultText = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"Default", TRUE);
		headerctrl.colors.Glyph = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"Glyph", FALSE);
		headerctrl.colors.MouseDown = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseDown", FALSE);
		headerctrl.colors.MouseDownGlyph = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseDownGlyph", FALSE);
		headerctrl.colors.MouseDownText = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseDown", TRUE);
		headerctrl.colors.MouseOver = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseOver", FALSE);
		headerctrl.colors.MouseOverGlyph = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseOverGlyph", FALSE);
		headerctrl.colors.MouseOverText = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"MouseOver", TRUE);
		headerctrl.colors.SeparatorLine = g_IdeSettings->GetThemeColor(GUID_HeaderCategory, L"SeparatorLine", FALSE);
	}

	//////////////////////////////////////////////////////////////////////////
	// try to apply theme of window
	else if (CXTheme::IsAppThemed())
	{
		Theme.OpenThemeData(m_hWnd, L"LISTVIEW");
		if (Theme.IsOpen())
		{
			// if theme does not support any needed part, we will use
			// only colors from theme to draw items in standard way
			if (GetWinVersion() < wvWin7 || // do not apply LISTVIEW theme on old OSes
			    !ThemeRendering || !Theme.IsThemePartDefined(LVP_LISTITEM, LISS_SELECTED) ||
			    !Theme.IsThemePartDefined(LVP_LISTITEM, LISS_HOTSELECTED) ||
			    !Theme.IsThemePartDefined(LVP_LISTITEM, LISS_SELECTEDNOTFOCUS) ||
			    !Theme.IsThemePartDefined(LVP_LISTITEM, LISS_HOT))
			{
				// Do not apply theme, use system colors
				ThemeRendering = false;
				HotTrack = false;

				ItemBGColor = Theme.GetSysColor(COLOR_WINDOW);
				ItemTextColor = Theme.GetSysColor(COLOR_WINDOWTEXT);

				GridLinesColor = Theme.GetSysColor(COLOR_INACTIVEBORDER);

				SelectedItemBGColor = SelectedItemBGBorderColor = Theme.GetSysColor(COLOR_HIGHLIGHT);
				SelectedItemTextColor = Theme.GetSysColor(COLOR_HIGHLIGHTTEXT);

				SelectedItemNoFocusBGColor = SelectedItemNoFocusBGBorderColor = Theme.GetSysColor(COLOR_BTNFACE);
				SelectedItemNoFocusTextColor = Theme.GetSysColor(COLOR_BTNTEXT);

				HotItemOverlayBGColor = HotItemOverlayBGBorderColor = Theme.GetSysColor(COLOR_WINDOW);
				HotItemTextColor = Theme.GetSysColor(COLOR_HOTLIGHT);
			}
			else
			{
				// use theme, used color indices are little different

				HotTrack = true;
				ThemeRendering = true;

				SelectedItemSyntaxHL = Theme.IsBGPartiallyTransparent(LVP_LISTITEM, LISS_SELECTED);
				SelectedItemNoFocusSyntaxHL = Theme.IsBGPartiallyTransparent(LVP_LISTITEM, LISS_SELECTEDNOTFOCUS);
				HotItemSyntaxHL = Theme.IsBGPartiallyTransparent(LVP_LISTITEM, LISS_HOT);

				ItemBGColor = Theme.GetSysColor(COLOR_WINDOW);
				ItemTextColor = Theme.GetSysColor(COLOR_WINDOWTEXT);

				GridLinesColor = Theme.GetSysColor(COLOR_INACTIVEBORDER);

				SelectedItemBGColor = SelectedItemBGBorderColor = Theme.GetSysColor(COLOR_HIGHLIGHT);
				SelectedItemTextColor = Theme.GetSysColor(COLOR_WINDOWTEXT);

				SelectedItemNoFocusBGColor = SelectedItemNoFocusBGBorderColor = Theme.GetSysColor(COLOR_BTNFACE);
				SelectedItemNoFocusTextColor = Theme.GetSysColor(COLOR_BTNTEXT);

				HotItemOverlayBGColor = HotItemOverlayBGBorderColor = Theme.GetSysColor(COLOR_HOTLIGHT);
				HotItemTextColor = Theme.GetSysColor(COLOR_WINDOWTEXT);
			}
			// [case: 92589] Don't close theme!
			// Theme.CloseThemeData();
		}
	}
	else
	{
		// Uses default settings - system colors taken in constructor.
	}

#ifdef CListCtrlFixedGrid_WATERMARK // [case: 149836]
	if (IsWatermarkEnabled())
	{
		UINT maxDpi = 96;
		for (auto dpi : VsUI::DpiHelper::GetDPIList())
		{
			if (maxDpi < dpi)
			{
				maxDpi = dpi;
			}
		}

		if (tomatoBG.IsLoaded() && (ItemBGColor != tomatoBGColor || maxDpi != tomatoBGMaxDPI))
		{
			tomatoBG.Release();
			tomatoBGbmp.DeleteObject();
		}

		if (!tomatoBG.IsLoaded())
		{
			if (ThemeUtils::GetTomatoBitmap(tomatoBGbmp, (int)maxDpi * 3, UseIDEThemeColors ? ItemBGColor : CLR_INVALID))
			{
				tomatoBG.Attach(tomatoBGbmp);
				tomatoBGColor = ItemBGColor;
				tomatoBGMaxDPI = maxDpi;
			}
		}
	}
#endif
}

LRESULT CListCtrlFixedGrid::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_VA_APPLYTHEME)
	{
		bool enable = wParam != 0;

		if (enable != UseIDEThemeColors)
		{
			UseIDEThemeColors = enable;

			InitAppearanceSchema();
			Invalidate();
			UpdateWindow();
		}

		return TRUE;
	}
	else if (message == WM_NCCALCSIZE && m_simpleListMode && GetStyle() & WS_HSCROLL)
	{
		ModifyStyle(WS_HSCROLL, 0);
	}
	else if (message == WM_VA_DLGENTEREXITSIZEMOVE && wParam == 0)
	{
		if (m_simpleListMode && headerctrl.GetItemCount() > 0)
		{
			CRect rect;
			GetClientRect(&rect);

			if (message == WM_VA_DLGENTEREXITSIZEMOVE)
				SetColumnWidth(0, rect.Width());
		}
	}
	else if (message == WM_THEMECHANGED && Theme.IsOpen())
	{
		Theme.CloseThemeData();
		InitAppearanceSchema();
		Invalidate();
		UpdateWindow();
	}

#ifdef CListCtrlFixedGrid_WATERMARK	// [case: 149836]
	if (!m_in_paintEvent && IsWatermarkEnabled() && message >= WM_KEYFIRST && message < LVM_FIRST)	
 	{
		InvalidateVisibleArea();
	}

// #ifdef _DEBUG
// 		auto msgStr = ThemeUtils::GetListMessageString(message, wParam, lParam);
// 		msgStr.Insert(0, "#LST ");
// 		::OutputDebugStringA(msgStr);
// #endif

#endif

	return __super::WindowProc(message, wParam, lParam);
}

void CListCtrlFixedGrid::OnPaint()
{
	m_in_paintEvent = true;
	Default();
	m_in_paintEvent = false;
}

BOOL CListCtrlFixedGrid::OnEraseBkgnd(CDC* pDC)
{
	InvalidateVisibleArea(true);
	return TRUE;
}

void CListCtrlFixedGrid::OnNcPaint()
{
	Default();
	DrawBorder();
}

void CListCtrlFixedGrid::OnTimer(UINT_PTR nIDEvent)
{
	__super::OnTimer(nIDEvent);
}

void CListCtrlFixedGrid::DelayedRefresh(int ms_duration)
{
	struct timer
	{
		static void __stdcall TimerProc(HWND hWnd,         // handle of CWnd that called SetTimer
		                                UINT nMsg,         // WM_TIMER
		                                UINT_PTR nIDEvent, // timer identification
		                                DWORD dwTime       // system time
		)
		{
			::InvalidateRect(hWnd, nullptr, TRUE);
			::UpdateWindow(hWnd);
			::KillTimer(hWnd, nIDEvent);
		}

		static void start(HWND hWnd, int ms_duration)
		{
			::SetTimer(hWnd, 1, (UINT)ms_duration, &timer::TimerProc);
		}
	};

	timer::start(m_hWnd, ms_duration);
}

void CListCtrlFixedGrid::OnSetFocus(CWnd* pOldWnd)
{
	__super::OnSetFocus(pOldWnd);
	Invalidate();
	UpdateWindow();
	DrawBorder();
}

void CListCtrlFixedGrid::OnKillFocus(CWnd* pNewWnd)
{
	__super::OnKillFocus(pNewWnd);
	Invalidate();
	UpdateWindow();
	DrawBorder();
}

void CListCtrlFixedGrid::DrawBorder()
{
	if (OwnerDraw && WS_BORDER == (WS_BORDER & GetWindowLong(m_hWnd, GWL_STYLE)))
	{
		CDC* dc = GetWindowDC();

		if (dc != NULL)
		{
			HWND focusWnd = ::GetFocus();
			bool focused = focusWnd == m_hWnd || focusWnd == headerctrl.m_hWnd;
			CBrush brush(focused ? BorderColorFocused : BorderColor);
			CRect rcBorder;
			GetWindowRect(rcBorder);
			rcBorder.bottom = rcBorder.Height();
			rcBorder.right = rcBorder.Width();
			rcBorder.left = rcBorder.top = 0;
			dc->FrameRect(rcBorder, &brush);
			ReleaseDC(dc);
		}
	}
}

void CListCtrlFixedGrid::ApplySimpleListMode()
{
	CRect cr;
	GetClientRect(&cr);
	m_simpleListMode = true;
	while (DeleteColumn(0))
	{
	}
	InsertColumn(0, "", 0, cr.Width());
	DrawIcons = false;
	DelayedRefresh(50);
}

void CListCtrlFixedGrid::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	__super::OnWindowPosChanging(lpwndpos);

	// 	if (SimpleListMode && headerctrl.GetItemCount() > 0)
	// 		SetColumnWidth(0, lpwndpos->cx);
}
