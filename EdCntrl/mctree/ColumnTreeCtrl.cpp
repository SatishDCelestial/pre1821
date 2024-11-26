/*********************************************************************
* Multi-Column Tree View, version 1.4 (July 7, 2005)
* Copyright (C) 2003-2005 Michal Mecinski.
*
* You may freely use and modify this code, but don't remove
* this copyright note.
*
* THERE IS NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, FOR
* THIS CODE. THE AUTHOR DOES NOT TAKE THE RESPONSIBILITY
* FOR ANY DAMAGE RESULTING FROM THE USE OF IT.
*
* E-mail: mimec@mimec.org
* WWW: http://www.mimec.org
********************************************************************/

#include "stdafxed.h"
#include "ColumnTreeCtrl.h"
#include "LOG.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


IMPLEMENT_DYNAMIC(CColumnTreeCtrl, CTreeCtrlTT)

CColumnTreeCtrl::CColumnTreeCtrl()
{
}

CColumnTreeCtrl::~CColumnTreeCtrl()
{
}


#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(CColumnTreeCtrl, CTreeCtrlTT)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()
#pragma warning(pop)


void CColumnTreeCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	// mask left click if outside the real item's label
	HandleMouse(WM_LBUTTONDOWN, nFlags, point);
}


void CColumnTreeCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	HandleMouse(WM_LBUTTONDBLCLK, nFlags, point);
}

void CColumnTreeCtrl::OnPaint()
{
	CPaintDC dc(this);
	CDC dcMem;

	try
	{
		// use temporary bitmap to avoid flickering
		if (dcMem.CreateCompatibleDC(&dc))
		{
			CRect rcClient;
			GetClientRect(&rcClient);

			CBitmap bmpMem;
			if (bmpMem.CreateCompatibleBitmap(&dc, rcClient.Width(), rcClient.Height()))
			{
				CBitmap* pOldBmp = dcMem.SelectObject(&bmpMem);
				if (pOldBmp)
				{
					dcMem.FillSolidRect(0, 0, rcClient.Width(), rcClient.Height(), dc.GetBkColor());

					// paint the window onto the memory bitmap
					CWnd::DefWindowProc(WM_PAINT, (WPARAM) dcMem.m_hDC, 0);

					// copy it to the window's DC
					dc.BitBlt(0, 0, rcClient.right, rcClient.bottom, &dcMem, 0, 0, SRCCOPY);

					dcMem.SelectObject(pOldBmp);
				}

				bmpMem.DeleteObject();
			}

			dcMem.DeleteDC();
		}
	}
	catch (CException * e)
	{
		UNUSED_ALWAYS(e);
		VALOGERROR("ERROR: exception caught in CColumnTreeCtrl::OnPaint");
	}
}

BOOL CColumnTreeCtrl::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;	// do nothing
}


void CColumnTreeCtrl::HandleMouse(UINT message, UINT nFlags, CPoint point)
{
	UINT fFlags;
	HTREEITEM hItem = HitTest(point, &fFlags);

	// verify the hit result
	if (fFlags & (TVHT_ONITEMLABEL | TVHT_ONITEMRIGHT))
	{
		CRect rcItem;
		GetItemRect(hItem, &rcItem, TRUE);

		if (GetWindowLong(m_hWnd, GWL_STYLE) & TVS_FULLROWSELECT)
		{
			if (message == WM_LBUTTONDOWN)
				SetFocus();

			// ignore if outside all columns
			rcItem.right = m_cxTotal;
			if (!rcItem.PtInRect(point))
				return;

			// select or expand item
			if (message == WM_LBUTTONDOWN)
			{
				Select(hItem, TVGN_CARET);
			}
			else if (message == WM_LBUTTONDBLCLK)
			{
				// send the NM_DBLCLK notification
				NMHDR nmhdr;
				nmhdr.hwndFrom = m_hWnd;
				nmhdr.idFrom = (UINT)GetDlgCtrlID();
				nmhdr.code = NM_DBLCLK;
				GetParent()->SendMessage(WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);

				Expand(hItem, TVE_TOGGLE);
			}

			return;
		}
		else
		{
			// ignore if outside the first column
			rcItem.right = m_cxFirstCol;
			if (!rcItem.PtInRect(point))
			{
				if (message == WM_LBUTTONDOWN)
					SetFocus();
				return;
			}

			CString strSub;
			AfxExtractSubString(strSub, GetItemText(hItem), 0, '\t');

			CDC* pDC = GetDC();
			if (pDC)
			{
				CFont *fnt(GetFont());
				if (fnt)
				{
					HGDIOBJ pOldFont = pDC->SelectObject(fnt);
					rcItem.right = rcItem.left + pDC->GetTextExtent(strSub).cx + 6;
					pDC->SelectObject(pOldFont);
				}
				ReleaseDC(pDC);
			}

			// ignore if outside the label's rectangle
			if (!rcItem.PtInRect(point))
			{
				if (message == WM_LBUTTONDOWN)
					SetFocus();
				return;
			}
		}
	}
	else
	{
		if((fFlags & TVHT_ONITEMSTATEICON) && hItem) {
			// clicked to check/un-check
			BOOL checking = !GetCheck(hItem);
			CheckAllDescendants(hItem, checking);
		}

		// check if the button or icon is hidden
		if (point.x >= m_cxFirstCol)
		{
			if (message == WM_LBUTTONDOWN)
				SetFocus();

			// ignore if outside all columns
			if (point.x > m_cxTotal)
				return;

			// select or expand item
			if (message == WM_LBUTTONDOWN)
			{
				Select(hItem, TVGN_CARET);
			}
			else if (message == WM_LBUTTONDBLCLK)
			{
				// send the NM_DBLCLK notification
				NMHDR nmhdr;
				nmhdr.hwndFrom = m_hWnd;
				nmhdr.idFrom = (UINT)GetDlgCtrlID();
				nmhdr.code = NM_DBLCLK;
				GetParent()->SendMessage(WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);

				Expand(hItem, TVE_TOGGLE);
			}

			return;
		}
	}

	// pass message to the default procedure
	CWnd::DefWindowProc(message, nFlags, MAKELONG(point.x, point.y));
}

const bool toggle_child_checkboxes_on_space = true;
void CColumnTreeCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {

	// [case: 82047]
	NMTVKEYDOWN kdn;
	ZeroMemory(&kdn, sizeof(kdn));
	kdn.hdr.code = TVN_KEYDOWN;
	kdn.hdr.hwndFrom = *this;
	kdn.hdr.idFrom = (UINT)GetDlgCtrlID();
	kdn.flags = nFlags;
	kdn.wVKey = (WORD)nChar;
	GetParent()->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM) &kdn);
	if (nChar && !kdn.wVKey)
		return;

	CTreeCtrlTT::OnKeyDown(nChar, nRepCnt, nFlags);

	if(toggle_child_checkboxes_on_space) {
		if(nChar == VK_SPACE) {
			if(GetStyle() & TVS_CHECKBOXES) {
				HTREEITEM item = GetSelectedItem();
				if(item) {
					BOOL checked = GetCheck(item);
					CheckAllDescendants(item, checked);
				}
			}
		}
	}
}

void
CColumnTreeCtrl::CheckAllDescendants(HTREEITEM item, BOOL checked) {
	HTREEITEM childItem = GetChildItem(item);
	while (childItem) {
		SetCheck(childItem, checked);
		if (ItemHasChildren(childItem))
			CheckAllDescendants(childItem, checked);
		childItem = GetNextSiblingItem(childItem);
	}
}



















IMPLEMENT_DYNAMIC(CColumnTreeCtrl2, DragDropTreeCtrl)

CColumnTreeCtrl2::CColumnTreeCtrl2() : DragDropTreeCtrl(this)
{
}

CColumnTreeCtrl2::~CColumnTreeCtrl2()
{
}


bool CColumnTreeCtrl2::IsAllowedToStartDrag()
{
	return false;
}

bool CColumnTreeCtrl2::IsAllowedToDrop(HTREEITEM target, bool afterTarget)
{
	return false;
}

void CColumnTreeCtrl2::CopyDroppedItem(HTREEITEM target, bool afterTarget)
{
	return;
}

void CColumnTreeCtrl2::MoveDroppedItem(HTREEITEM target, bool afterTarget)
{
	return;
}

void CColumnTreeCtrl2::OnTripleClick()
{
	return;
}

#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(CColumnTreeCtrl2, DragDropTreeCtrl)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()
#pragma warning(pop)


void CColumnTreeCtrl2::OnLButtonDown(UINT nFlags, CPoint point)
{
	DragDropTreeCtrl::OnLButtonDown(nFlags, point);

	// mask left click if outside the real item's label
	HandleMouse(WM_LBUTTONDOWN, nFlags, point);
}


void CColumnTreeCtrl2::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	DragDropTreeCtrl::OnLButtonDblClk(nFlags, point);

	HandleMouse(WM_LBUTTONDBLCLK, nFlags, point);
}

void CColumnTreeCtrl2::OnPaint()
{
	CPaintDC dc(this);
	CDC dcMem;

	try
	{
		// use temporary bitmap to avoid flickering
		if (dcMem.CreateCompatibleDC(&dc))
		{
			CRect rcClient;
			GetClientRect(&rcClient);

			CBitmap bmpMem;
			if (bmpMem.CreateCompatibleBitmap(&dc, rcClient.Width(), rcClient.Height()))
			{
				CBitmap* pOldBmp = dcMem.SelectObject(&bmpMem);
				if (pOldBmp)
				{
					dcMem.FillSolidRect(0, 0, rcClient.Width(), rcClient.Height(), dc.GetBkColor());

					// paint the window onto the memory bitmap
					CWnd::DefWindowProc(WM_PAINT, (WPARAM) dcMem.m_hDC, 0);

					// copy it to the window's DC
					dc.BitBlt(0, 0, rcClient.right, rcClient.bottom, &dcMem, 0, 0, SRCCOPY);

					dcMem.SelectObject(pOldBmp);
				}

				bmpMem.DeleteObject();
			}

			dcMem.DeleteDC();
		}
	}
	catch (CException * e)
	{
		UNUSED_ALWAYS(e);
		VALOGERROR("ERROR: exception caught in CColumnTreeCtrl2::OnPaint");
	}
}

BOOL CColumnTreeCtrl2::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;	// do nothing
}


void CColumnTreeCtrl2::HandleMouse(const UINT message, const UINT nFlags, const CPoint point)
{
	UINT fFlags;
	HTREEITEM hItem = HitTest(point, &fFlags);

	// verify the hit result
	if (fFlags & (TVHT_ONITEMLABEL | TVHT_ONITEMRIGHT))
	{
		CRect rcItem;
		GetItemRect(hItem, &rcItem, TRUE);

		if (GetWindowLong(m_hWnd, GWL_STYLE) & TVS_FULLROWSELECT)
		{
			if (message == WM_LBUTTONDOWN)
				SetFocus();

			// ignore if outside all columns
			rcItem.right = m_cxTotal;
			if (!rcItem.PtInRect(point))
				return;

			// select or expand item
			if (message == WM_LBUTTONDOWN)
			{
				Select(hItem, TVGN_CARET);
			}
			else if (message == WM_LBUTTONDBLCLK)
			{
				// send the NM_DBLCLK notification
				NMHDR nmhdr;
				nmhdr.hwndFrom = m_hWnd;
				nmhdr.idFrom = (UINT)GetDlgCtrlID();
				nmhdr.code = NM_DBLCLK;
				GetParent()->SendMessage(WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);

				Expand(hItem, TVE_TOGGLE);
			}

			return;
		}
		else
		{
			// ignore if outside the first column
			rcItem.right = m_cxFirstCol;
			if (!rcItem.PtInRect(point))
			{
				if (message == WM_LBUTTONDOWN)
					SetFocus();
				return;
			}

			CString strSub;
			AfxExtractSubString(strSub, GetItemText(hItem), 0, '\t');

			CDC* pDC = GetDC();
			if (pDC)
			{
				CFont *fnt(GetFont());
				if (fnt)
				{
					HGDIOBJ pOldFont = pDC->SelectObject(fnt);
					rcItem.right = rcItem.left + pDC->GetTextExtent(strSub).cx + 6;
					pDC->SelectObject(pOldFont);
				}
				ReleaseDC(pDC);
			}

			// ignore if outside the label's rectangle
			if (!rcItem.PtInRect(point))
			{
				if (message == WM_LBUTTONDOWN)
					SetFocus();
				return;
			}
		}
	}
	else
	{
		if((fFlags & TVHT_ONITEMSTATEICON) && hItem) {
			// clicked to check/un-check
			BOOL checking = !GetCheck(hItem);
			CheckAllDescendants(hItem, checking);
		}

		// check if the button or icon is hidden
		if (point.x >= m_cxFirstCol)
		{
			if (message == WM_LBUTTONDOWN)
				SetFocus();

			// ignore if outside all columns
			if (point.x > m_cxTotal)
				return;

			// select or expand item
			if (message == WM_LBUTTONDOWN)
			{
				Select(hItem, TVGN_CARET);
			}
			else if (message == WM_LBUTTONDBLCLK)
			{
				// send the NM_DBLCLK notification
				NMHDR nmhdr;
				nmhdr.hwndFrom = m_hWnd;
				nmhdr.idFrom = (UINT)GetDlgCtrlID();
				nmhdr.code = NM_DBLCLK;
				GetParent()->SendMessage(WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);

				Expand(hItem, TVE_TOGGLE);
			}

			return;
		}
	}

	// pass message to the default procedure
// 	CWnd::DefWindowProc(message, nFlags, MAKELONG(point.x, point.y));
}

void CColumnTreeCtrl2::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {

	// [case: 82047]
	NMTVKEYDOWN kdn;
	ZeroMemory(&kdn, sizeof(kdn));
	kdn.hdr.code = TVN_KEYDOWN;
	kdn.hdr.hwndFrom = *this;
	kdn.hdr.idFrom = (UINT)GetDlgCtrlID();
	kdn.flags = nFlags;
	kdn.wVKey = (WORD)nChar;
	GetParent()->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM) &kdn);
	if (nChar && !kdn.wVKey)
		return;

	DragDropTreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);

	if(toggle_child_checkboxes_on_space) {
		if(nChar == VK_SPACE) {
			if(GetStyle() & TVS_CHECKBOXES) {
				HTREEITEM item = GetSelectedItemIfSingleSelected();
				if(item) {
					BOOL checked = GetCheck(item);
					CheckAllDescendants(item, checked);
				}
			}
		}
	}
}

void
CColumnTreeCtrl2::CheckAllDescendants(HTREEITEM item, BOOL checked) {
	HTREEITEM childItem = GetChildItem(item);
	while (childItem) {
		SetCheck(childItem, checked);
		if (ItemHasChildren(childItem))
			CheckAllDescendants(childItem, checked);
		childItem = GetNextSiblingItem(childItem);
	}
}
