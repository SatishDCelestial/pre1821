// GenericComboBoxListCtrl.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "GenericComboBoxListBox.h"
#include "GenericComboBox.h"
#include "resource.h"
#include "FontSettings.h"
#include "Settings.h"
#include "SyntaxColoring.h"
#include "WindowUtils.h"
#include "ColorListControls.h"
#include "vsshell100.h"
using namespace std::placeholders;
#include "SaveDC.h"
#include "DevShellAttributes.h"
#include "IdeSettings.h"
#include "StringUtils.h"
#include "TextOutDC.h"
#include "VACompletionBox.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include <locale>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define GCB_ICONWIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(20))

static void NcActivate(CWnd* wnd)
{
	if (wnd != NULL)
	{
		wnd->SendMessage(WM_NCACTIVATE, TRUE);
		wnd->SetRedraw(TRUE);
	}
}

static void NcActivateParents(CWnd* parent)
{
	NcActivate(parent);

	CWnd* prev = parent;
	for (int idx = 0; idx < 10; ++idx)
	{
		CWnd* tmp = prev->GetParent();
		if (tmp)
		{
			prev = tmp;
			NcActivate(prev);
		}
		else
			break;
	}

	NcActivate(parent->GetParentOwner());
}

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBoxListCtrl

CGenericComboBoxListCtrl::CGenericComboBoxListCtrl() : m_nLastItem(-1)
{
	background_gradient_cache.reset(new CGradientCache);
}

CGenericComboBoxListCtrl::~CGenericComboBoxListCtrl()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CGenericComboBoxListCtrl, CListCtrl)
//{{AFX_MSG_MAP(CGenericComboBoxListCtrl)
ON_WM_SETFOCUS()
ON_WM_KILLFOCUS()
ON_WM_LBUTTONDOWN()
ON_WM_MOUSEMOVE()
ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
// xxx_sean unicode: this isn't being invoked...
ON_NOTIFY_REFLECT(LVN_GETDISPINFOW, OnGetdispinfoW)

ON_MESSAGE(WM_SETFONT, OnSetFont)
ON_WM_MEASUREITEM_REFLECT()

ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_POP, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_SHOW, 0, 0xFFFF, OnToolTipText)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBoxListCtrl message handlers

void CGenericComboBoxListCtrl::OnSetFocus(CWnd* pOldWnd)
{
	CListCtrl::OnSetFocus(pOldWnd);

	if (m_handler)
		::NcActivateParents(m_handler->GetParent());
}

void CGenericComboBoxListCtrl::OnKillFocus(CWnd* pNewWnd)
{
	if (m_handler)
		m_handler->GetParent()->SetFocus();

	CListCtrl::OnKillFocus(pNewWnd);
}

void CGenericComboBoxListCtrl::Init(std::shared_ptr<Handler> handler)
{
	if (!handler)
	{
		_ASSERTE(!"Handler must be valid");
		return;
	}

	if (!handler->ColorableContent)
		mySetProp(m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	m_handler = handler;

	CWnd* originalOwner = GetOwner();
	CWnd* desktop = GetDesktopWindow();
	SetParent(desktop);
	SetOwner(originalOwner);

	ModifyStyle(WS_CHILD, 0);
	ModifyStyleEx(0, WS_EX_TOOLWINDOW);

	SetFontType(VaFontType::EnvironmentFont);

	InsertColumn(0, "", LVCFMT_IMAGE, GCB_ICONWIDTH);
	InsertColumn(1, "img", LVCFMT_COL_HAS_IMAGES);
	EnableTrackingToolTips(TRUE);
	CToolTipCtrl* tips = GetToolTips();
	if (tips)
		tips->ModifyStyle(0, TTS_ALWAYSTIP);

	m_handler->OnInit(*this);
}
void CGenericComboBoxListCtrl::Display(CRect rc)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (!m_handler)
		return;

	m_nLastItem = m_handler->GetCurSel();

	const int nCount = GetItemCount();

	int nHeight = rc.Height();
	CRect rcItem;
	if (nCount)
	{
		GetItemRect(0, &rcItem, LVIR_BOUNDS);
		nHeight = nCount * rcItem.Height();
	}
	else
		nHeight = VsUI::DpiHelper::LogicalToDeviceUnitsY(20);

	rc.bottom = rc.top + nHeight + VsUI::DpiHelper::LogicalToDeviceUnitsY(4);

	CWnd* to_focus = m_handler->GetFocusHolder();
	if (to_focus == nullptr)
		to_focus = m_handler->GetParent();

	_ASSERTE(to_focus != nullptr);

	if (to_focus)
		to_focus->SetFocus();

	m_handler->OnShow();
	SetWindowPos(&wndTopMost, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW | SWP_NOACTIVATE);

	int nColumnWidth = rc.Width();
	if (GetStyle() & WS_THICKFRAME)
		nColumnWidth -= (2 * ::GetSystemMetrics(SM_CXFRAME));
	else
		nColumnWidth -= (2 * ::GetSystemMetrics(SM_CXBORDER));

	if (GetStyle() & WS_VSCROLL)
		nColumnWidth -= GetSystemMetrics(SM_CXVSCROLL);

	//	nColumnWidth -= ::GetSystemMetrics(SM_CXVSCROLL);
	SetColumnWidth(0, GCB_ICONWIDTH);
	SetColumnWidth(1, nColumnWidth - GCB_ICONWIDTH);
	int nCurSel = m_handler->GetCurSel();
	SetItemState(nCurSel, LVIS_SELECTED | LVIS_FOCUSED, (UINT)-1);

	BeginMouseHooking();

	::NcActivateParents(m_handler->GetParent());
}

void CGenericComboBoxListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (!m_handler)
		return;

	ReleaseCapture();

	UINT uFlags;
	HitTest(point, &uFlags);
	if ((uFlags & LVHT_ONITEMICON) | (uFlags & LVHT_ONITEMLABEL) //|
	                                                             //( uFlags & LVHT_ONITEMSTATEICON )
	)
	{
		POSITION pos = GetFirstSelectedItemPosition();
		int nItem = GetNextSelectedItem(pos);
		m_nLastItem = nItem;
		m_handler->OnSelect(nItem);
	}
	else
	{
		CPoint pt;
		GetCursorPos(&pt);
		HWND hClkWnd = ::WindowFromPoint(pt);
		if (m_hWnd != hClkWnd)
		{
			if (m_handler->OnHide())
			{
				ShowWindow(SW_HIDE);
				::SetFocus(hClkWnd);
			}
			return;
		}
		else
			CListCtrl::OnLButtonDown(nFlags, point);
	}

	if (m_handler->OnHide())
	{
		ShowWindow(SW_HIDE);
	}
}

void CGenericComboBoxListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	UINT uFlags;
	static CPoint lpt(-1, -1);
	if (lpt == point)
		return;
	lpt = point;
	int nItem = HitTest(point, &uFlags);
	if (nItem != -1)
		CListCtrl::SetItemState(nItem, LVIS_SELECTED | LVIS_FOCUSED, (UINT)-1);

	CListCtrl::OnMouseMove(nFlags, point);
}

LRESULT CGenericComboBoxListCtrl::CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam)
{
	return CWnd::WindowProc(message, wparam, lparam);
}

LRESULT CGenericComboBoxListCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	LRESULT user_result = 0;
	CWnd* parent = m_handler ? m_handler->GetParent() : nullptr;

	if (m_handler && m_handler->OnWindowProc(message, wParam, lParam, user_result))
		return user_result;

	if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
	{
		ShowWindow(SW_HIDE);
		return TRUE;
	}
	else if (message == WM_KEYDOWN && wParam == VK_RETURN)
	{
		POSITION pos = GetFirstSelectedItemPosition();
		int nItem = GetNextSelectedItem(pos);
		m_nLastItem = nItem;
		parent->SetFocus();
		ShowWindow(SW_HIDE);
		return TRUE;
	}
	else if (message == WM_KEYDOWN && wParam == VK_LEFT)
	{
		wParam = VK_UP;
	}
	else if (message == WM_CHAR)
	{
		parent->SetFocus();
		parent->SendMessage(message, wParam, lParam);
		return TRUE;
	}
	else if (message == WM_KEYDOWN && wParam == VK_RIGHT)
	{
		wParam = VK_DOWN;
	}
	else if (message == WM_KEYDOWN && (wParam == VK_DOWN || wParam == VK_UP))
	{
		LRESULT r = CWnd::WindowProc(message, wParam, lParam);
		//		SetComboItem();
		return r;
	}
	else if ((message == WM_SHOWWINDOW && wParam == FALSE) || message == WM_NCDESTROY)
		EndMouseHooking();

	bool draw_border = false;
	LRESULT ret = 0;
	switch (message)
	{
	case WM_NCPAINT:
	case WM_PAINT:
	case WM_ERASEBKGND:
	case WM_NCACTIVATE:
	case WM_PRINT:
	case WM_PRINTCLIENT:
		if (m_handler && m_handler->IsVS2010ColouringActive())
			draw_border = true;
		break;
	}

	if ((message == WM_ERASEBKGND) && m_handler && m_handler->IsVS2010ColouringActive())
	{
		CRect rect;
		GetClientRect(rect);
		HDC hdc = (HDC)wParam;
		const std::pair<double, COLORREF> back_gradients[] = {
		    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN)),
		    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_END))
		    // 			std::make_pair(0, RGB(255, 0, 0)),
		    // 			std::make_pair(1, RGB(0, 255, 0))
		};
		background_gradient_cache->CGradientCache::DrawVerticalVS2010Gradient(*CDC::FromHandle(hdc), rect,
		                                                                      back_gradients, countof(back_gradients));
		ret = 1;
		goto do_draw_border;
	}

	{
		const int type = (m_handler && m_handler->ColorableContent) ? PaintType::ListBox : PaintType::DontColor;
		VAColorPaintMessages pw(message, type);
		ret = CWnd::WindowProc(message, wParam, lParam);
	}

	if (WM_CREATE == message)
		::VsScrollbarTheme(m_hWnd);

do_draw_border:
	if (draw_border)
	{
		CWindowDC dc(this);
		CSaveDC savedc(dc);

		// calculate client rect in non-client coordinates
		CRect crect;
		GetClientRect(crect);
		ClientToScreen(crect);
		CRect wrect;
		GetWindowRect(wrect);
		const CPoint offset(crect.left - wrect.left, crect.top - wrect.top);

		// draw border
		CRect brect = wrect;
		brect.MoveToXY(0, 0);
		CRgn brgn;
		brgn.CreateRectRgnIndirect(brect);
		dc.SelectObject(&brgn);
		dc.SetBkMode(TRANSPARENT);
		::SelectObject(dc.m_hDC, ::GetStockObject(HOLLOW_BRUSH));
		const COLORREF popup_border_colour = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BORDER);
		CPen pen;
		pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), popup_border_colour);
		dc.SelectObject(&pen);
		dc.Rectangle(brect);
	}

	if (message == WM_SHOWWINDOW && m_handler)
		m_handler->OnVisibleChanged();

	return ret;
}

INT_PTR CGenericComboBoxListCtrl::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
	POSITION pos = GetFirstSelectedItemPosition();
	if (!pos)
		return -1;
	int nItem = GetNextSelectedItem(pos);
	CRect rcItem;
	GetItemRect(nItem, rcItem, LVIR_BOUNDS);

	pTI->hwnd = m_hWnd;
	pTI->uId = (UINT)(nItem + 1);
	pTI->lpszText = LPSTR_TEXTCALLBACK;

	pTI->rect = rcItem;

	return (INT_PTR)pTI->uId;
}

BOOL CGenericComboBoxListCtrl::OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!m_handler)
		return FALSE;

	// need to handle both ANSI and UNICODE versions of the message
	//	TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pNMHDR;
	//	TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pNMHDR;
	//	TOOLTIPTEXT *pTTT = (TOOLTIPTEXT *)pNMHDR;
	int nID = (int)pNMHDR->idFrom;

	if (nID == 0)
	{
		POSITION pos = GetFirstSelectedItemPosition();
		if (!pos)
			return -1;
		nID = GetNextSelectedItem(pos);
	}

	const int kBufSize = 1024;
	static WCHAR szText[kBufSize + 1];
	LPNMTTDISPINFOW lpnmtdi = (LPNMTTDISPINFOW)pNMHDR;

	*pResult = 0;
	switch (pNMHDR->code)
	{
	case TTN_NEEDTEXTA: {
		// for some reason, the linebreaks in the tooltip text is ignored
		// unless the maxtipwidth has been set (just for the list control?)
		const int kMaxWidth = (int)::SendMessage(pNMHDR->hwndFrom, TTM_GETMAXTIPWIDTH, 0, 0);
		if (-1 == kMaxWidth)
			::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, g_FontSettings->m_tooltipWidth);

		WTString tipText;
		if (m_handler->GetItemTip(nID, tipText))
		{
			lpnmtdi->lpszText = szText;
			lstrcpyn((LPSTR)szText, tipText.c_str(), kBufSize);
		}
	}
	break;
	case TTN_NEEDTEXTW: {
		// for some reason, the linebreaks in the tooltip text is ignored
		// unless the maxtipwidth has been set (just for the list control?)
		const int kMaxWidth = (int)::SendMessage(pNMHDR->hwndFrom, TTM_GETMAXTIPWIDTH, 0, 0);
		if (-1 == kMaxWidth)
			::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, g_FontSettings->m_tooltipWidth);
		CStringW tipText;
		if (m_handler->GetItemTip(nID, tipText))
		{
			lpnmtdi->lpszText = szText;
			lstrcpynW(szText, tipText, kBufSize);
		}
	}
	break;
	case TTN_POP:
		return FALSE;
	case TTN_SHOW: {
		CRect rt;
		::GetWindowRect(pNMHDR->hwndFrom, &rt);
		if (rt.left == 0 && rt.top == 0)
		{
			// This happens if not using the mouse to scroll through list.
			// The tooltip appears in the top left corner of the desktop.
			// Tried repositioning in here to no avail.
			// But resizing does work so we hide it by changing the size.
			//				CPoint pt;
			//				GetCursorPos(&pt);
			//				rt.MoveToXY(pt);
			rt.bottom = rt.right = 0;
		}

		::SetWindowPos(pNMHDR->hwndFrom, wndTopMost.GetSafeHwnd(), rt.left, rt.right, rt.Width(), rt.Height(),
		               SWP_NOREPOSITION | SWP_NOACTIVATE);
	}
	break;
	}

	return TRUE; // message was handled
}

void CGenericComboBoxListCtrl::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
#ifndef _UNICODE
	if (m_handler)
		m_handler->GetDispInfoA(pNMHDR, pResult);
#else
	// nothing, OnGetdispinfoW should be called
#endif
}

void CGenericComboBoxListCtrl::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!m_handler)
		return;

	m_handler->GetDispInfoW(pNMHDR, pResult);
}

#define ICON_SPACING (VsUI::DpiHelper::LogicalToDeviceUnitsX(4))

LRESULT CGenericComboBoxListCtrl::OnSetFont(WPARAM wParam, LPARAM)
{
	LRESULT res = Default();

	CRect rc;
	GetWindowRect(&rc);

	WINDOWPOS wp;
	wp.hwnd = m_hWnd;
	wp.cx = rc.Width();
	wp.cy = rc.Height();
	wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
	SendMessage(WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);

	return res;
}

void CGenericComboBoxListCtrl::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	LOGFONT lf;
	GetFont()->GetLogFont(&lf);

	if (lf.lfHeight < 0)
		lpMeasureItemStruct->itemHeight = (UINT)-lf.lfHeight;
	else
		lpMeasureItemStruct->itemHeight = (UINT)lf.lfHeight;
}

void CGenericComboBoxListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	int nItem = (int)lpDrawItemStruct->itemID;
	CImageList* pImageList;

	// Save dc state
	int nSavedDC = pDC->SaveDC();

	if (m_handler && m_handler->GetFont() != m_font_type)
	{
		m_font_type = m_handler->GetFont();
		UpdateFonts(VAFTF_All, (UINT)VsUI::DpiHelper::GetDeviceDpiX());
	}

	HFONT oldFont = (HFONT)pDC->SelectObject(m_font);

	// Get item state info
	LV_ITEM lvi;
	lvi.mask = LVIF_STATE | LVIF_IMAGE | LVIF_PARAM;
	lvi.iItem = nItem;
	lvi.iSubItem = 0;
	lvi.stateMask = 0xffff; // get all state flags
	GetItem(&lvi);

	const long icon = lvi.iImage;
	const UINT type = (UINT)lvi.lParam;
	_ASSERTE((type & TYPEMASK) == type);

	// Should the item be highlighted
	bool bHighlight = !!(lvi.state & LVIS_SELECTED);

	// Get rectangles for drawing
	CRect rcHighlight, rcLabel, rcIcon;
	GetItemRect(nItem, rcHighlight, LVIR_BOUNDS);
	GetItemRect(nItem, rcLabel, LVIR_LABEL);
	GetItemRect(nItem, rcIcon, LVIR_ICON);
	CRect rcCol(rcHighlight);
	rcLabel.right = rcHighlight.right;
	CRect crect;
	GetClientRect(crect);

	if (!m_handler || (m_handler && !m_handler->DrawImages))
	{
		rcLabel.left = rcIcon.left;
		rcHighlight.left = 0;
	}
	else
	{
		// highlight full width of text column
		rcHighlight.left = rcLabel.left;
		rcLabel.left += ICON_SPACING; // add space between label and icon
		rcLabel.right += ICON_SPACING;
	}

	// Set clip region
	CRgn rgn;
	rgn.CreateRectRgnIndirect(&rcCol);
	pDC->SelectClipRgn(&rgn);
	rgn.DeleteObject();

	if (m_handler && m_handler->IsVS2010ColouringActive())
	{
		InitScrollWindowExPatch();
		mySetProp(m_hWnd, "__VA_do_not_scrollwindowex", (HANDLE)1);
	}

	// Draw the background color
	// TODO: could save these in settings.cpp so that GetSysColor isn't called everytime
	// pDC->SelectObject(&g_FontSettings->m_EnvironmentFont);
	if (bHighlight)
	{
		COLORREF bgcolor = ::GetSysColor(COLOR_HIGHLIGHT);
		if (m_handler && m_handler->ColorableContent && Psettings->m_bEnhColorListboxes &&
		    Psettings->m_ActiveSyntaxColoring)
		{
#define DELTA 25
			bgcolor = ::GetSysColor(COLOR_WINDOW);
			bgcolor = RGB((GetRValue(bgcolor) < 125) ? GetRValue(bgcolor) + DELTA * 2 : GetRValue(bgcolor) - DELTA,
			              (GetGValue(bgcolor) < 125) ? GetGValue(bgcolor) + DELTA * 2 : GetGValue(bgcolor) - DELTA,
			              (GetBValue(bgcolor) < 125) ? GetBValue(bgcolor) + DELTA * 2 : GetBValue(bgcolor) - DELTA);
			pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
		}
		else
			pDC->SetTextColor(::GetSysColor(COLOR_HIGHLIGHTTEXT));
		if (m_handler && !m_handler->IsVS2010ColouringActive())
			pDC->FillSolidRect(rcHighlight, bgcolor);
		else
		{
			const std::pair<double, COLORREF> back_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_POPUP_BACKGROUND_BEGIN)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_POPUP_BACKGROUND_END))
			    // 				std::make_pair(0, RGB(255, 0, 0)),
			    // 		 		std::make_pair(1, RGB(0, 255, 0))
			};
			const std::pair<double, COLORREF> selection_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_BEGIN)),
			    std::make_pair(.49, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE1)),
			    std::make_pair(.5, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE2)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_END))};

			COLORREF border_colour = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BORDER);

			DrawVS2010Selection(*pDC, rcCol, selection_gradients, countof(selection_gradients), border_colour,
			                    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, &*background_gradient_cache, _1,
			                              crect, back_gradients, (int)countof(back_gradients)) /*, true*/);
			pDC->SetTextColor(RGB(0, 0, 0));
		}
		pDC->SetBkColor(bgcolor);
	}
	else
	{
		if (m_handler && !m_handler->IsVS2010ColouringActive())
		{
			pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
			pDC->FillSolidRect(rcHighlight, ::GetSysColor(COLOR_WINDOW));
		}
		else
		{
			const std::pair<double, COLORREF> back_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_POPUP_BACKGROUND_BEGIN)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_POPUP_BACKGROUND_END))
			    // 				std::make_pair(0, RGB(255, 0, 0)),
			    // 				std::make_pair(1, RGB(0, 255, 0))
			};
			background_gradient_cache->CGradientCache::DrawVerticalVS2010Gradient(*pDC, crect, back_gradients,
			                                                                      countof(back_gradients));
			pDC->SetTextColor(RGB(0, 0, 0));
		}
	}

	if (m_handler && m_handler->IsVS2010ColouringActive())
		pDC->SetTextColor(g_IdeSettings->GetEnvironmentColor(L"ComboBoxText", false));

	if (m_handler && m_handler->ColorableContent)
	{
		const COLORREF clr = WTColorFromType((int)type);
		if (clr != 0xdead && Psettings->m_bEnhColorListboxes && Psettings->m_ActiveSyntaxColoring &&
		    !(g_currentEdCnt && g_currentEdCnt->m_txtFile))
		{
			// make sure color is visible against selected bg color
			COLORREF bgclr = pDC->GetBkColor();
			int cdiff = abs(GetRValue(clr) - GetRValue(bgclr)) + abs(GetGValue(clr) - GetGValue(bgclr)) +
			            abs(GetBValue(clr) - GetBValue(bgclr));
			if (cdiff >= 100)
				pDC->SetTextColor(clr);
		}
	}

	if (!m_handler || (m_handler && m_handler->DrawImages))
	{
		// Draw normal and overlay icon
		pImageList = GetImageList(LVSIL_SMALL);
		if (pImageList)
		{
			//			UINT nOvlImageMask = lvi.state & LVIS_OVERLAYMASK;
			//			UINT blend = (bHighlight ? ILD_BLEND50 : 0u);
			pImageList->Draw(pDC, icon, CPoint(rcIcon.left, rcIcon.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(1)),
			                 /*blend |*/ ILD_TRANSPARENT /*| nOvlImageMask */); // do not blend icons, makes VS.NET2005
			                                                                    // icons look bad
		}
	}

	// Draw item label - Column 0
	CStringW str;
	if (m_handler)
	{
		m_handler->GetItemText(nItem, str);
		str.Replace(L"\n", L"\\n");
		// this loop is duplicated in VACompletionBox.cpp
		std::locale loc("");
		for (int i = 0; i < str.GetLength(); i++)
		{
			if ((str[i] & 0x80) || !std::isprint(str[i], loc))
			{
				if (str[i] == '\n')
					str.SetAt(i, 0x1f);
				else if (str[i] & 0x80)
					str.SetAt(i, str[i]); // allow accent characters and copyright symbol
				else
					str.SetAt(i, ' ');
			}
		}
	}

	rcLabel.left += VsUI::DpiHelper::LogicalToDeviceUnitsX(2);

	::VaDrawTextW(pDC->GetSafeHdc(), str, rcLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP | DT_VCENTER);

	// Restore dc
	pDC->SelectObject(oldFont);
	pDC->RestoreDC(nSavedDC);
	if (m_handler && m_handler->ColorableContent && bHighlight && Psettings->m_bEnhColorListboxes &&
	    Psettings->m_ActiveSyntaxColoring && !m_handler->IsVS2010ColouringActive())
	{
		rcHighlight.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		pDC->DrawFocusRect(rcHighlight);
	}
}

void CGenericComboBoxListCtrl::SettingsChanged()
{
	if (m_handler && !m_handler->IsVS2010ColouringActive())
		return;

	background_gradient_cache.reset(new CGradientCache);

	if (m_handler)
		m_handler->OnSettingsChanged();
}

void CGenericComboBoxListCtrl::OnMouseHookMessage(int code, UINT message, MOUSEHOOKSTRUCT* lpMHS)
{
	if (message == WM_MBUTTONDOWN || message == WM_NCMBUTTONDOWN || message == WM_RBUTTONDOWN ||
	    message == WM_NCRBUTTONDOWN)
	{
		if (m_handler->OnHide())
		{
			ShowWindow(SW_HIDE);
			EndMouseHooking();
		}
	}

	if ((message == WM_LBUTTONDOWN || message == WM_NCLBUTTONDOWN) && lpMHS->hwnd != m_hWnd)
	{
		if (!m_handler || !m_handler->HandleLButtonDown(lpMHS->pt))
		{
			if (m_handler->OnHide())
			{
				ShowWindow(SW_HIDE);
				EndMouseHooking();
			}
		}
	}
}

void CGenericComboBoxListCtrl::Handler::GetDispInfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;
	LV_ITEMW* pItem = &(pDispInfo)->item;

	CStringW item_str;

	if (pItem->mask & LVIF_TEXT)
		pItem->pszText[0] = '\0';

	if (pItem->mask & LVIF_IMAGE)
		pItem->iImage = -1;

	if (pItem->mask & LVIF_STATE)
		pItem->state = 0;

	if (pItem->iItem >= 0 && GetItemsCount() > pItem->iItem)
	{
		if (GetItemInfo(pItem->iItem, (pItem->mask & LVIF_TEXT) ? &item_str : nullptr, nullptr,
		                (pItem->mask & LVIF_IMAGE) ? &pItem->iImage : nullptr,
		                (pItem->mask & LVIF_STATE ? &pItem->state : nullptr)))
		{
			if (pItem->mask & LVIF_TEXT)
				::wcscpy_s(pItem->pszText, (uint)pItem->cchTextMax, (LPCWSTR)item_str);
		}
	}

	*pResult = 0;
}

void CGenericComboBoxListCtrl::Handler::GetDispInfoA(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFOA* pDispInfo = (LV_DISPINFOA*)pNMHDR;
	LV_ITEMA* pItem = &(pDispInfo)->item;

	CStringW item_str;

	if (pItem->mask & LVIF_TEXT)
		pItem->pszText[0] = '\0';

	if (pItem->mask & LVIF_IMAGE)
		pItem->iImage = -1;

	if (pItem->mask & LVIF_STATE)
		pItem->state = 0;

	if (pItem->iItem >= 0 && GetItemsCount() > pItem->iItem)
	{
		if (GetItemInfo(pItem->iItem, (pItem->mask & LVIF_TEXT) ? &item_str : nullptr, nullptr,
		                (pItem->mask & LVIF_IMAGE) ? &pItem->iImage : nullptr,
		                (pItem->mask & LVIF_STATE ? &pItem->state : nullptr)))
		{
			if (pItem->mask & LVIF_TEXT)
			{
				WTString astr = ::WideToMbcs(item_str, item_str.GetLength());
				::strcpy_s(pItem->pszText, (uint)pItem->cchTextMax, (LPCSTR)astr.c_str());
			}
		}
	}

	*pResult = 0;
}

bool CGenericComboBoxListCtrl::Handler::GetItemImage(int nRow, int& img)
{
	return GetItemInfo(nRow, nullptr, nullptr, &img, nullptr);
}

bool CGenericComboBoxListCtrl::Handler::GetItemTip(int nRow, WTString& str)
{
	CStringW wstr;
	if (GetItemTip(nRow, wstr))
	{
		str = wstr;
		return true;
	}
	return false;
}

bool CGenericComboBoxListCtrl::Handler::GetItemTip(int nRow, CStringW& str)
{
	return GetItemInfo(nRow, nullptr, &str, nullptr, nullptr);
}

bool CGenericComboBoxListCtrl::Handler::GetItemText(int nRow, CStringW& str)
{
	return GetItemInfo(nRow, &str, nullptr, nullptr, nullptr);
}

VaFontType CGenericComboBoxListCtrl::Handler::GetFont()
{
	return VaFontType::EnvironmentFont;
}
