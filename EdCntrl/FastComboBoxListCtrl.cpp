// FastComboBoxListCtrl.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "FastComboBoxListCtrl.h"
#include "FastComboBox.h"
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
#include "VAThemeUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define ICONWIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(20))

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
// CFastComboBoxListCtrl

CFastComboBoxListCtrl::CFastComboBoxListCtrl()
    : m_nLastItem(-1), m_clipEnd(false), m_pComboParent(NULL), mHasColorableContent(false)
{
	background_gradient_cache.reset(new CGradientCache);
}

CFastComboBoxListCtrl::~CFastComboBoxListCtrl()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CFastComboBoxListCtrl, CListCtrl)
//{{AFX_MSG_MAP(CFastComboBoxListCtrl)
ON_WM_SETFOCUS()
ON_WM_KILLFOCUS()
ON_WM_LBUTTONDOWN()
ON_WM_MOUSEMOVE()
ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
// xxx_sean unicode: this isn't being invoked...
ON_NOTIFY_REFLECT(LVN_GETDISPINFOW, OnGetdispinfoW)

//}}AFX_MSG_MAP
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_POP, 0, 0xFFFF, OnToolTipText)
ON_NOTIFY_EX_RANGE(TTN_SHOW, 0, 0xFFFF, OnToolTipText)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CFastComboBoxListCtrl message handlers

void CFastComboBoxListCtrl::OnSetFocus(CWnd* pOldWnd)
{
	CListCtrl::OnSetFocus(pOldWnd);
	::NcActivateParents(m_pComboParent);
}

void CFastComboBoxListCtrl::OnKillFocus(CWnd* pNewWnd)
{
	m_pComboParent->SetFocus();
	CListCtrl::OnKillFocus(pNewWnd);
}

void CFastComboBoxListCtrl::Init(CFastComboBox* pComboParent, bool hasColorableContent)
{
	gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgComboDropdown, LVSIL_NORMAL);
	SetFontType(pComboParent->GetFontType());

	mHasColorableContent = hasColorableContent;
	if (!hasColorableContent)
		mySetProp(m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	m_pComboParent = pComboParent;
	CWnd* originalOwner = GetOwner();
	CWnd* desktop = GetDesktopWindow();
	SetParent(desktop);
	SetOwner(originalOwner);
	ModifyStyle(WS_CHILD, 0);
	ModifyStyleEx(0, WS_EX_TOOLWINDOW);

	InsertColumn(0, "", LVCFMT_IMAGE, ICONWIDTH);
	InsertColumn(1, "img", LVCFMT_COL_HAS_IMAGES);
	EnableTrackingToolTips(TRUE);
	CToolTipCtrl* tips = GetToolTips();
	if (tips)
		tips->ModifyStyle(0, TTS_ALWAYSTIP);
}

void CFastComboBoxListCtrl::Display(CRect rc)
{
	// force DPI change
	SetWindowPos(nullptr, rc.top, rc.left, rc.Width(), rc.Height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

	// force scrollbars recalculation
	// this is just a proven fix for empty space at the top of the list, 
	// while scrollbar thinks that all is OK, 
	// another option is hiding scrollbars, but I consider this safer
	ModifyStyle(0, LVS_NOSCROLL, SWP_FRAMECHANGED);
	ModifyStyle(LVS_NOSCROLL, 0, SWP_FRAMECHANGED);

	// force WM_MEASUREITEM
	WINDOWPOS wp;
	ZeroMemory(&wp, sizeof(wp));
	wp.hwnd = m_hWnd;
	wp.cx = rc.Width();
	wp.cy = rc.Height();
	wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
	SendMessage(WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);

	auto dpiScope = SetDefaultDpiHelper();

	m_nLastItem = m_pComboParent->GetCurSel();

	const int nCount = GetItemCount();
	int maxItemTextWidth = 0;

	if (m_hWnd)
	{
		try
		{
			CStringW textString = L"abcdefghijklmnopqrstuwvxyzABCDEFGHIJKLMNOPQRSTUWVXYZ123456789 ";
			auto testWidth = (int)SendMessageW(m_hWnd, LVM_GETSTRINGWIDTHW, 0, (LPARAM)(LPCWSTR)textString);
			auto avgWidth = (float)testWidth / (float)textString.GetLength();
			auto minMeasure = rc.Width();
	
// 			int maxLen = 0;
			StopWatch sw;
	
			// this is just an attempt to measure as much as possible in 200ms
	
			for (int itm = 0; itm < nCount; itm++)
			{
				if ((itm % 10 == 0) && sw.ElapsedMilliseconds() > 200)
					break;
	
				auto txt = GetItemTextW(itm, 0);
	 
				auto length = txt.GetLength();
	
				if ((float)length * avgWidth < (float)minMeasure)
					continue;
	
				txt.Replace(L"\n", L"\\n");
	
				length = txt.GetLength();
				for (int i = 0; i < length; i++)
				{
					if ((txt[i] & 0x80) || !isprint(txt[i]))
					{
						if (txt[i] == L'\n')
							txt.SetAt(i, L'\x1f');
						else if (!(txt[i] & 0x80))
							txt.SetAt(i, L' ');
					}
				}
	
				auto width = (int)SendMessageW(m_hWnd, LVM_GETSTRINGWIDTHW, 0, (LPARAM)(LPCWSTR)txt);
	
				if (maxItemTextWidth < width)
					maxItemTextWidth = width;
	
				if (minMeasure < maxItemTextWidth)
					minMeasure = maxItemTextWidth;
			}
		}
		catch (...)
		{
		}	
	}
	
	bool trimVScrollWidth = false;
	int nHeight = rc.Height();
	if (nCount)
	{
		int height = m_pComboParent->MeasureItemHeight();

		nHeight = nCount * height;

		if (nHeight > rc.Height())
		{
			nHeight = height * (rc.Height() / height);
		}		

		trimVScrollWidth = nCount * height > nHeight;
	}
	else
	{
		nHeight = VsUI::DpiHelper::LogicalToDeviceUnitsY(20);	
	}

	rc.bottom = rc.top + nHeight;

	m_pComboParent->GetEditCtrl()->SetFocus();
	int nColumnWidth = rc.Width();
	if (GetStyle() & WS_THICKFRAME)
		nColumnWidth -= (2 * VsUI::DpiHelper::GetSystemMetrics(SM_CXFRAME));
	else
		nColumnWidth -= (2 * VsUI::DpiHelper::GetSystemMetrics(SM_CXBORDER));

	nColumnWidth -= ICONWIDTH;
	if (trimVScrollWidth)
	{
		nColumnWidth -= ::VsUI::DpiHelper::GetSystemMetrics(SM_CXVSCROLL);		
	}

	maxItemTextWidth += VsUI::DpiHelper::LogicalToDeviceUnitsX(8);

	if (nColumnWidth < maxItemTextWidth)
	{
		nColumnWidth = maxItemTextWidth;
	}

	if (rc.Width() < nColumnWidth)
	{
		rc.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(4);
	}

	SetColumnWidth(0, ICONWIDTH);
	SetColumnWidth(1, nColumnWidth);

	int nCurSel = m_pComboParent->GetCurSel();
	SetItemState(nCurSel, LVIS_SELECTED | LVIS_FOCUSED, (UINT)-1);

	SetWindowPos(&wndTopMost, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	::NcActivateParents(m_pComboParent);

	BeginMouseHooking();
}

void CFastComboBoxListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
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
		m_pComboParent->OnSelect();
	}
	else
	{
		CPoint pt;
		GetCursorPos(&pt);
		HWND hClkWnd = ::WindowFromPoint(pt);
		if (m_hWnd != hClkWnd)
		{
			m_pComboParent->DisplayList(FALSE);
			::SetFocus(hClkWnd);
		}
		else
			CListCtrl::OnLButtonDown(nFlags, point);
	}
	m_pComboParent->DisplayList(FALSE);
}

void CFastComboBoxListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	UINT uFlags = 0;

	static int last_item = -1;

	int nItem = HitTest(point, &uFlags);

	if (last_item == nItem)
		return;

	last_item = nItem;

	if (nItem != -1)
	{
		// select only items which are more visible than hidden
		CRect cr, ir;
		GetItemRect(nItem, &ir, LVIR_BOUNDS);
		GetClientRect(&cr);
		CRect tmp;
		tmp.IntersectRect(&ir, &cr);
		if (tmp.Height() >= ir.Height() * 3 / 4)
			CListCtrl::SetItemState(nItem, LVIS_SELECTED | LVIS_FOCUSED, (UINT)-1);
	}

	CListCtrl::OnMouseMove(nFlags, point);
}

LRESULT CFastComboBoxListCtrl::CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam)
{
	return CWnd::WindowProc(message, wparam, lparam);
}

// case: 142798 hide dropdown when user clicks outside
void CFastComboBoxListCtrl::OnMouseHookMessage(int code, UINT message, MOUSEHOOKSTRUCT* lpMHS)
{
	if (!IsWindowVisible())
	{
		EndMouseHooking();
	}
	else if (message == WM_MBUTTONDOWN || message == WM_NCMBUTTONDOWN || message == WM_RBUTTONDOWN ||
	         message == WM_NCRBUTTONDOWN)
	{
		EndMouseHooking();
		m_pComboParent->DisplayList(FALSE);
	}
	else if ((message == WM_LBUTTONDOWN || message == WM_NCLBUTTONDOWN) &&

	         // not myself
	         lpMHS->hwnd != m_hWnd && !::IsChild(m_hWnd, lpMHS->hwnd) &&

	         // not combo parent
	         lpMHS->hwnd != m_pComboParent->m_hWnd && !::IsChild(m_pComboParent->m_hWnd, lpMHS->hwnd))
	{
		EndMouseHooking();
		m_pComboParent->DisplayList(FALSE);
	}
}

CStringW CFastComboBoxListCtrl::GetItemTextW(int nItem, int nSubItem) const
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

LRESULT CFastComboBoxListCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	auto dpiScope = SetDefaultDpiHelper();
	//	if ( message == WM_KEYDOWN || message == WM_CHAR)	{
	//		int i = 23;
	//	}
	if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
	{
		ShowWindow(SW_HIDE);
		return TRUE;
	}
	else if (message == WM_KEYDOWN && wParam == VK_RETURN)
	{
		int nItem = GetSelectionMark();
		if (nItem == -1)
		{
			POSITION pos = GetFirstSelectedItemPosition();
			nItem = GetNextSelectedItem(pos);
		}
		m_nLastItem = nItem;
		m_pComboParent->SetFocus();
		ShowWindow(SW_HIDE);
		return TRUE;
	}
	else if (message == WM_KEYDOWN && wParam == VK_LEFT)
	{
		wParam = VK_UP;
	}
	else if (message == WM_CHAR)
	{
		m_pComboParent->SetFocus();
		m_pComboParent->SendMessage(message, wParam, lParam);
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

	else if (message == WM_HSCROLL && LOWORD(wParam) == SB_THUMBTRACK)
	{
		SCROLLINFO scrollNfo;
		::ZeroMemory(&scrollNfo, sizeof(scrollNfo));
		scrollNfo.cbSize = sizeof(scrollNfo);
		if (GetScrollInfo(SB_HORZ, &scrollNfo))
		{
			int maxScrollPos = scrollNfo.nMax - ((int)scrollNfo.nPage - 1);
			m_clipEnd = maxScrollPos == scrollNfo.nTrackPos;
		}
	}

#pragma warning(push)
#pragma warning(disable : 4127)
	if (0 && IsWindowVisible())
	{ // Test Focus
		HWND h = ::GetFocus();
		HWND hed = m_pComboParent->GetEditCtrl() ? m_pComboParent->GetEditCtrl()->m_hWnd : NULL;
		if (h != m_hWnd && h != m_pComboParent->m_hWnd && h != hed)
			ShowWindow(SW_HIDE);
	}
#pragma warning(pop)

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
		if (m_pComboParent && m_pComboParent->IsVS2010ColouringActive())
			draw_border = true;
		break;
	}
	if ((message == WM_ERASEBKGND) && m_pComboParent && m_pComboParent->IsVS2010ColouringActive())
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
		const int type = mHasColorableContent ? PaintType::ListBox : PaintType::DontColor;
		VAColorPaintMessages pw(message, type);
		ret = __super::WindowProc(message, wParam, lParam);
	}

	// case: 142798 - don't hook when window is hidden
	if (WM_WINDOWPOSCHANGED == message && !IsWindowVisible() && IsHooking(WH_MOUSE))
		EndMouseHooking();

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
	return ret;
}

INT_PTR CFastComboBoxListCtrl::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
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

BOOL CFastComboBoxListCtrl::OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	// need to handle both ANSI and UNICODE versions of the message
	UINT nID = (UINT)pNMHDR->idFrom;

	if (nID == 0)
	{
		POSITION pos = GetFirstSelectedItemPosition();
		if (!pos)
			return -1;
		nID = (UINT)GetNextSelectedItem(pos);
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
		WTString tipText = m_pComboParent->GetItemTip((int)nID);
		lpnmtdi->lpszText = szText;
		lstrcpyn((LPSTR)szText, tipText.c_str(), kBufSize);
	}
	break;
	case TTN_NEEDTEXTW: {
		// for some reason, the linebreaks in the tooltip text is ignored
		// unless the maxtipwidth has been set (just for the list control?)
		const int kMaxWidth = (int)::SendMessage(pNMHDR->hwndFrom, TTM_GETMAXTIPWIDTH, 0, 0);
		if (-1 == kMaxWidth)
			::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, g_FontSettings->m_tooltipWidth);
		CStringW tipText = m_pComboParent->GetItemTipW((int)nID);
		lpnmtdi->lpszText = szText;
		lstrcpynW(szText, tipText, kBufSize);
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

void CFastComboBoxListCtrl::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	m_pComboParent->OnGetdispinfo(pNMHDR, pResult);
}

void CFastComboBoxListCtrl::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	m_pComboParent->OnGetdispinfoW(pNMHDR, pResult);
}

#define ICON_SPACING (VsUI::DpiHelper::LogicalToDeviceUnitsX(4))

void CFastComboBoxListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(this);

	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	int nItem = (int)lpDrawItemStruct->itemID;
	CImageList* pImageList = GetImageList(LVSIL_NORMAL);

	// Save dc state
	int nSavedDC = pDC->SaveDC();
	auto oldFont = pDC->SelectObject(m_pComboParent->GetDpiAwareFont());

	CStringW sym = GetItemTextW(nItem, 0);
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

	// rcCol.IntersectRect(rcCol, crect); // restrict rectangle to visible area
	if (m_clipEnd && rcCol.right > crect.right)
		rcCol.right = crect.right;

	if (pImageList && (icon != I_IMAGECALLBACK) && (icon != I_IMAGENONE))
	{
		IMAGEINFO ii;
		memset(&ii, 0, sizeof(ii));
		if (pImageList->GetImageInfo(icon, &ii))
		{
			rcLabel.left = rcIcon.left + ii.rcImage.right - ii.rcImage.left;

			if (rcLabel.top > rcIcon.top)
				rcLabel.top = rcIcon.top;

			if (rcLabel.bottom < rcIcon.bottom)
				rcLabel.bottom = rcIcon.bottom;

			CRect imgRect(ii.rcImage);
			imgRect.MoveToXY(rcIcon.left, rcIcon.top + (rcIcon.Height() - imgRect.Height()) / 2);
			rcIcon = imgRect;
		}
	}

	// highlight full width of text column
	rcLabel.left += ICON_SPACING; // add space between label and icon
	rcLabel.right += ICON_SPACING;
	rcHighlight.left = rcLabel.left;

	// Set clip region
	CRgn rgn;
	rgn.CreateRectRgnIndirect(&rcCol);
	pDC->SelectClipRgn(&rgn);
	rgn.DeleteObject();

	if (m_pComboParent->IsVS2010ColouringActive())
	{
		InitScrollWindowExPatch();
		mySetProp(m_hWnd, "__VA_do_not_scrollwindowex", (HANDLE)1);
	}

	// Draw the background color
	// TODO: could save these in settings.cpp so that GetSysColor isn't called everytime
	pDC->SelectObject(GetDpiAwareFont());
	if (bHighlight)
	{
		COLORREF bgcolor = ::GetSysColor(COLOR_HIGHLIGHT);
		if (mHasColorableContent && Psettings->m_bEnhColorListboxes && Psettings->m_ActiveSyntaxColoring)
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
		if (!m_pComboParent->IsVS2010ColouringActive())
			pDC->FillSolidRect(rcHighlight, bgcolor);
		else
		{
			const std::pair<double, COLORREF> back_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_END))
			    // 				std::make_pair(0, RGB(255, 0, 0)),
			    // 		 		std::make_pair(1, RGB(0, 255, 0))
			};
			const std::pair<double, COLORREF> selection_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_BEGIN)),
			    std::make_pair(.49, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE1)),
			    std::make_pair(.5, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE2)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_END))};
			const COLORREF border_colour = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BORDER);

			DrawVS2010Selection(*pDC, rcCol, selection_gradients, countof(selection_gradients), border_colour,
			                    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, &*background_gradient_cache, _1,
			                              crect, back_gradients, (int)countof(back_gradients)) /*, true*/);
			pDC->SetTextColor(RGB(0, 0, 0));
		}
		pDC->SetBkColor(bgcolor);
	}
	else
	{
		if (!m_pComboParent->IsVS2010ColouringActive())
		{
			pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
			pDC->FillSolidRect(rcHighlight, ::GetSysColor(COLOR_WINDOW));
		}
		else
		{
			const std::pair<double, COLORREF> back_gradients[] = {
			    std::make_pair(0, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN)),
			    std::make_pair(1, CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_END))
			    // 				std::make_pair(0, RGB(255, 0, 0)),
			    // 				std::make_pair(1, RGB(0, 255, 0))
			};
			background_gradient_cache->CGradientCache::DrawVerticalVS2010Gradient(*pDC, crect, back_gradients,
			                                                                      countof(back_gradients));
			pDC->SetTextColor(RGB(0, 0, 0));
		}
	}

	if (m_pComboParent->IsVS2010ColouringActive())
		pDC->SetTextColor(g_IdeSettings->GetEnvironmentColor(L"ComboBoxText", false));

	if (mHasColorableContent)
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

	// Draw normal and overlay icon
	if (pImageList)
	{
		//		UINT nOvlImageMask=lvi.state & LVIS_OVERLAYMASK;
		//		UINT blend = (bHighlight?ILD_BLEND50:0u);
		pImageList->Draw(
		    pDC, icon, CPoint(rcIcon.left, rcIcon.top),
		    /*blend |*/ ILD_TRANSPARENT /*| nOvlImageMask */); // do not blend icons, makes VS.NET2005 icons look bad
	}

	// Draw item label - Column 0
	WTString str(sym);
	str.ReplaceAll("\n", "\\n");
	// this loop is duplicated in VACompletionBox.cpp
	for (int i = 0; i < str.length(); i++)
	{
		if ((str[i] & 0x80) || !isprint(str[i]))
		{
			if (str[i] == '\n')
				str.SetAt(i, 0x1f);
			else if (str[i] & 0x80)
				str.SetAt(i, str[i]); // allow accent characters and copyright symbol
			else
				str.SetAt(i, ' ');
		}
	}

	const CStringW wStr(str.Wide());
	rcLabel.left += VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
	::VaDrawTextW(pDC->GetSafeHdc(), wStr, rcLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP | DT_VCENTER);

	// Restore dc
	pDC->SelectObject(oldFont);
	pDC->RestoreDC(nSavedDC);
	if (mHasColorableContent && bHighlight && Psettings->m_bEnhColorListboxes && Psettings->m_ActiveSyntaxColoring &&
	    !m_pComboParent->IsVS2010ColouringActive())
	{
		rcHighlight.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		pDC->DrawFocusRect(rcHighlight);
	}
}

void CFastComboBoxListCtrl::SettingsChanged()
{
	if (!m_pComboParent->IsVS2010ColouringActive())
		return;

	background_gradient_cache.reset(new CGradientCache);
	m_pComboParent->SettingsChanged();
}
