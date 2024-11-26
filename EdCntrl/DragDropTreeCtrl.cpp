#include "stdafxed.h"
#include "DragDropTreeCtrl.h"
#include "resource.h"
#include "ITreeDropHandler.h"
#include "IdeSettings.h"

#define countof(x) (sizeof((x)) / sizeof((x)[0]))

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(DragDropTreeCtrl, CTreeCtrlTT)

static const int kScrollTimeLength = 50;
static const int kDragHoverTimerId = 200;

DragDropTreeCtrl::DragDropTreeCtrl(ITreeDropHandler* pDropHandler)
    : mIsTopTimerActive(false), mIsBottomTimerActive(false), mDropAfterTarget(true), mNoopErase(true),
      mItemDropTarget(NULL), mPreviousDragFlags(0), mDropHandler(pDropHandler), mDragState(dragOp_NotDragging),
      mDragImage(NULL), mShiftSelStart(NULL)
{
	mCursors[dragOp_DropNotAllowed] = ::LoadCursor(NULL, IDC_NO);
	mCursors[dragOp_Move] = ::LoadCursor(AfxGetResourceHandle(), MAKEINTRESOURCE(IDC_DRAGMOVE));
	mCursors[dragOp_Copy] = ::LoadCursor(AfxGetResourceHandle(), MAKEINTRESOURCE(IDC_DRAGCOPY));
	_ASSERTE(mDropHandler);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(DragDropTreeCtrl, CTreeCtrlTT)
//{{AFX_MSG_MAP(DragDropTreeCtrl)
ON_NOTIFY_REFLECT(TVN_BEGINDRAG, OnBegindrag)
ON_WM_MOUSEMOVE()
ON_WM_LBUTTONUP()
ON_WM_LBUTTONDOWN()
ON_WM_RBUTTONDOWN()
ON_WM_LBUTTONDBLCLK()
ON_WM_KEYDOWN()
ON_NOTIFY_REFLECT(TVN_SELCHANGED, OnSelChanged)
ON_WM_SETCURSOR()
ON_WM_TIMER()
ON_WM_KILLFOCUS()
ON_WM_SETFOCUS()
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnTreeCustomDraw)
ON_WM_ERASEBKGND()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

BOOL DragDropTreeCtrl::PreTranslateMessage(MSG* pMsg)
{
	if (IsDragging() && (pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP || pMsg->message == WM_SYSKEYDOWN ||
	                     pMsg->message == WM_SYSKEYUP))
	{
		if (pMsg->wParam == VK_ESCAPE)
		{
			CancelDrag();
		}
		else if (pMsg->wParam == VK_CONTROL)
		{
			SelectDropCursor();
		}

		POINT pt;
		GetCursorPos(&pt);
		// Cause MouseMove() (and as a result OnSetCursor()) to be called.
		SetCursorPos(pt.x, pt.y);
		return true;
	}

	return CTreeCtrlTT::PreTranslateMessage(pMsg);
}

void DragDropTreeCtrl::OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMTREEVIEW* pnmtv = (NMTREEVIEW*)pNMHDR;

	//	HTREEITEM itemOld = pnmtv->itemOld.hItem;
	HTREEITEM itemNew = pnmtv->itemNew.hItem;

	_ASSERTE(itemNew == GetSelectedItem());
	//	_ASSERTE(!itemOld || IsInSelectionList(itemOld));
	_ASSERTE(!itemNew || pnmtv->itemNew.state & TVIS_SELECTED);

	if (IsInSelectionList(itemNew))
	{
		size_t numSel = m_vSelItem.size();
		for (size_t i = 0; i < numSel; ++i)
			SetItemState(m_vSelItem[i], TVIS_SELECTED, TVIS_SELECTED);
	}
	else
	{
		ClearSelectionList();
		if (itemNew)
			AddToSelectionList(itemNew);
	}
}

void DragDropTreeCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	HTREEITEM curSelItem = GetSelectedItem();
	if (curSelItem)
	{
		if (nChar == VK_UP || nChar == VK_DOWN)
		{
			if (GetKeyState(VK_SHIFT) & 0x8000)
			{
				HTREEITEM nextItem;
				if (nChar == VK_UP)
					nextItem = GetPrevSiblingItem(curSelItem);
				else
					nextItem = GetNextSiblingItem(curSelItem);
				OnShiftClickItem(nextItem);
				return;
			}
			else
			{
				ClearSelectionList();
				SelectItem(curSelItem);
				mShiftSelStart = NULL;
			}
		}
		else if (nChar == VK_LEFT)
		{
			if (!ItemHasChildren(curSelItem) || !(TVIS_EXPANDED & GetItemState(curSelItem, TVIS_EXPANDED)))
			{
				HTREEITEM parent = GetParentItem(curSelItem);
				if (parent)
				{
					SelectItem(parent);
					return;
				}
			}
		}
		else if (nChar == VK_RIGHT)
		{
			// only use VK_RIGHT for node expansion
			if (ItemHasChildren(curSelItem) && (TVIS_EXPANDED & GetItemState(curSelItem, TVIS_EXPANDED)))
			{
				return;
			}
		}
	}
	CTreeCtrlTT::OnKeyDown(nChar, nRepCnt, nFlags);
}

void DragDropTreeCtrl::OnBegindrag(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!mDropHandler->IsAllowedToStartDrag())
		return;

	NMTREEVIEW* pnmtv;

	_ASSERTE(!IsDragging());
	mDragState = dragOp_Move;
	SetCursor(mCursors[mDragState]);
	mIsTopTimerActive = false;
	mIsBottomTimerActive = false;
	mPreviousDragFlags = 0;

	// determine the item being dragged
	pnmtv = (NMTREEVIEW*)pNMHDR;
	HTREEITEM draggedItem = pnmtv->itemNew.hItem;
	mItemDropTarget = NULL;
	mDropAfterTarget = true;

	SetCapture();
	SelectDropCursor();

	// get the image list for dragging
	mDragImage = CreateDragImage();
	// CreateDragImage() returns NULL if no image list
	// associated with the tree view control
	if (!mDragImage)
		return;

	CRect rect;
	GetItemRect(draggedItem, rect, TRUE);
	POINT pt = {rect.left + 5, rect.top + 5};
	mDragImage->BeginDrag(0, CPoint(-5, -5));
	ClientToScreen(&pt);
	mDragImage->DragEnter(NULL, pt);
}

void DragDropTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (IsDragging())
	{
		CImageList::DragShowNolock(FALSE);
		POINT pt = point;
		HTREEITEM hitem = SelectDropCursor(&point);
		if (hitem)
		{
			CRect r;
			GetItemRect(hitem, &r, TRUE);
			BOOL before = pt.y < (r.top + (r.Height() / 2));
			pt = r.TopLeft();
			pt.x -= 26;
			if (before)
			{
				pt.y = r.top - (r.Height()) / 2 - 4;
				mDropAfterTarget = false;
			}
			else
			{
				pt.y = r.top + (r.Height()) / 2 - 4;
				mDropAfterTarget = true;
			}
			ClientToScreen(&pt);
			CImageList::DragShowNolock(TRUE);
			CImageList::DragMove(pt);

			mItemDropTarget = hitem;
			KillTimer(kDragHoverTimerId);
			if (TVIS_EXPANDED != GetItemState(mItemDropTarget, TVIS_EXPANDED))
				SetTimer(kDragHoverTimerId, 500, NULL);
		}
		else
		{
			KillTimer(kDragHoverTimerId);
		}
		return; // prevent infotip processing
	}

	CTreeCtrlTT::OnMouseMove(nFlags, point);
}

BOOL DragDropTreeCtrl::IsTripleClick(POINT pt) const
{
	DWORD messageTime = (DWORD)GetMessageTime();
	DWORD difference = messageTime - mLastDblClickTime;
	if (messageTime >= mLastDblClickTime && difference <= GetDoubleClickTime())
	{
		int cx = GetSystemMetrics(SM_CXDOUBLECLK);
		int cy = GetSystemMetrics(SM_CYDOUBLECLK);
		if (abs(mLastDblClickPt.x - pt.x) <= cx && abs(mLastDblClickPt.y - pt.y) <= cy)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void DragDropTreeCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	mLastDblClickTime = (DWORD)GetMessageTime();
	mLastDblClickPt = point;
	CTreeCtrlTT::OnLButtonDblClk(nFlags, point);
}

void DragDropTreeCtrl::ClearSelectionList()
{
	const size_t nSelItemCount = m_vSelItem.size();
	for (size_t i = 0; i < nSelItemCount; ++i)
		SetItemState(m_vSelItem[i], 0, TVIS_SELECTED);
	m_vSelItem.clear();
}

void DragDropTreeCtrl::AddToSelectionList(HTREEITEM hItem)
{
	_ASSERTE(!IsInSelectionList(hItem));
	_ASSERTE(TVIS_SELECTED == (GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED));
	m_vSelItem.push_back(hItem);
}

void DragDropTreeCtrl::RemoveFromSelectionList(HTREEITEM hItem)
{
	_ASSERTE(IsInSelectionList(hItem));
	TreeItemVec::iterator itr;
	for (itr = m_vSelItem.begin(); itr != m_vSelItem.end(); ++itr)
	{
		if ((*itr) == hItem)
		{
			_ASSERTE(0 == (GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED));
			m_vSelItem.erase(itr);
			return;
		}
	}
	_ASSERTE(!"DragDropTreeCtrl::RemoveFromSelectionList() - Didn't find item");
}

bool DragDropTreeCtrl::IsInSelectionList(HTREEITEM hItem) const
{
	TreeItemVec::const_iterator itr;
	for (itr = m_vSelItem.begin(); itr != m_vSelItem.end(); ++itr)
	{
		if ((*itr) == hItem)
		{
			return true;
		}
	}
	return false;
}

void DragDropTreeCtrl::OnControlClickItem(HTREEITEM hItem, BOOL isDrag)
{
	_ASSERTE(hItem);

	// Don't allow selection of parents or children of currently selected items
	if (!IsInSelectionList(hItem))
	{
		if (IsAChildOfASelectedItem(hItem) || IsAParentOfASelectedItem(hItem))
			return;
	}

	// if isDrag, hItem is selected, not toggled.
	uint nState;
	if (isDrag)
		nState = TVIS_SELECTED;
	else
		nState = (TVIS_SELECTED & GetItemState(hItem, TVIS_SELECTED)) ? 0u : TVIS_SELECTED;
	SetItemState(hItem, nState, TVIS_SELECTED);

	if (0 == nState)
	{
		_ASSERTE(IsInSelectionList(hItem));
		RemoveFromSelectionList(hItem);

		// if we're unselecting "the" selected item, then we need to select
		// something else.
		if (GetSelectedItem() == hItem)
		{
			if (m_vSelItem.size())
				SelectItem(m_vSelItem[0]);
			else
				SelectItem(NULL);
		}
	}
	else
	{
		if (!IsInSelectionList(hItem))
			AddToSelectionList(hItem);
		SelectItem(hItem);
	}
}

void DragDropTreeCtrl::OnShiftClickItem(HTREEITEM hItemTo)
{
	HTREEITEM hItemFrom = mShiftSelStart ? mShiftSelStart : GetSelectedItem();
	if (hItemFrom && hItemTo && GetParentItem(hItemTo) == GetParentItem(hItemFrom))
	{
		mShiftSelStart = hItemFrom;
		HTREEITEM hItemFocus = hItemTo;

		SelectItem(NULL);
		if (hItemFrom != hItemTo)
		{
			HTREEITEM hTemp;

			RECT fromRect;
			GetItemRect(hItemFrom, &fromRect, FALSE);
			RECT toRect;
			GetItemRect(hItemTo, &toRect, FALSE);
			if (fromRect.top > toRect.top)
			{
				hTemp = hItemFrom;
				hItemFrom = hItemTo;
				hItemTo = hTemp;
			}

			hTemp = hItemFrom;
			for (;;)
			{
				SetItemState(hTemp, TVIS_SELECTED, TVIS_SELECTED);
				AddToSelectionList(hTemp);
				if (hTemp == hItemTo)
					break;
				hTemp = GetNextSiblingItem(hTemp);
			}
		}
		SelectItem(hItemFocus);
	}
}

void DragDropTreeCtrl::OnRButtonDown(UINT nFlags, CPoint point)
{
	UINT flags = 0;
	HTREEITEM hItem = HitTest(point, &flags);
	if (IsInSelectionList(hItem))
	{
		// make it "the" selected item.
		SelectItem(hItem);
	}
	else
	{
		CTreeCtrlTT::OnRButtonDown(nFlags, point);
	}
}

void DragDropTreeCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (IsTripleClick(point))
	{
		mDropHandler->OnTripleClick();
	}
	else
	{
		SetFocus();

		UINT flags = 0;
		HTREEITEM hItem = HitTest(point, &flags);
		if (!hItem)
		{
			// ignore
		}
		else if (flags & TVHT_ONITEMBUTTON)
		{
			__super::OnLButtonDown(nFlags, point);
		}
		else
		{
			const bool bShift = (nFlags & MK_SHIFT) != 0;
			const bool bCtrl = (nFlags & MK_CONTROL) != 0;

			if (bShift)
			{
				// selection occurs immediately
				OnShiftClickItem(hItem);
			}
			else
			{
				// non-shift ops clear this
				mShiftSelStart = NULL;

				if (bCtrl)
				{
					// selection occurs after drag-detect
				}
				else
				{
					if (!IsInSelectionList(hItem))
						SelectItem(hItem); // clears selection list, if nec.
				}
			}

			// Check if this is a click or a drag...
			CPoint ptDrag = point;
			ClientToScreen(&ptDrag);
			BOOL isDrag = DragDetect(ptDrag);

			if (bCtrl)
			{
				OnControlClickItem(hItem, isDrag);
			}

			// construct tree notification info
			NMTREEVIEW nmtv;
			nmtv.hdr.hwndFrom = m_hWnd;
			nmtv.hdr.idFrom = (UINT)::GetDlgCtrlID(m_hWnd);
			nmtv.itemNew.mask = TVIF_HANDLE | TVIF_PARAM;
			nmtv.itemNew.hItem = hItem;
			nmtv.itemNew.lParam = (LPARAM)GetItemData(hItem);

			if (isDrag)
			{
				DWORD dwStyle = GetStyle();
				if (!(dwStyle & TVS_DISABLEDRAGDROP))
				{
					nmtv.hdr.code = TVN_BEGINDRAG;
					nmtv.ptDrag = point;
					SendNotify(&nmtv.hdr);
				}
			}
			else
			{
				if (bShift)
				{
				}
				else if (bCtrl)
				{
				}
				else
				{
					// if other items selected, this clears them
					if (m_vSelItem.size() > 1)
					{
						SelectItem(NULL);
						SelectItem(hItem);
					}
				}

				nmtv.hdr.code = NM_CLICK;
				SendNotify(&nmtv.hdr);
			}
		}
	}
}

bool DragDropTreeCtrl::IsAChildOfASelectedItem(HTREEITEM hItem) const
{
	TreeItemVec::const_iterator iter;

	for (iter = m_vSelItem.begin(); iter != m_vSelItem.end(); ++iter)
	{
		HTREEITEM suspectedParent = *iter;
		if (suspectedParent == hItem || IsChildNodeOf(hItem, suspectedParent))
		{
			return true;
		}
	}
	return false;
}

bool DragDropTreeCtrl::IsAParentOfASelectedItem(HTREEITEM hItem) const
{
	TreeItemVec::const_iterator iter;

	for (iter = m_vSelItem.begin(); iter != m_vSelItem.end(); ++iter)
	{
		HTREEITEM suspectedChild = *iter;
		if (suspectedChild == hItem || IsChildNodeOf(suspectedChild, hItem))
		{
			return true;
		}
	}
	return false;
}

void DragDropTreeCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (IsDragging())
	{
		// Dragging an item to itself or to a node of its subtree is
		// not allowed
		if (!IsAChildOfASelectedItem(mItemDropTarget) && dragOp_DropNotAllowed != mDragState &&
		    mDropHandler->IsAllowedToDrop(mItemDropTarget, mDropAfterTarget))
		{
			switch (mDragState)
			{
			case dragOp_Move:
				mDropHandler->MoveDroppedItem(mItemDropTarget, mDropAfterTarget);
				break;
			case dragOp_Copy:
				mDropHandler->CopyDroppedItem(mItemDropTarget, mDropAfterTarget);
				break;
			default:
				_ASSERTE(!"bad drag state");
			}
		}
		else
		{
			MessageBeep(0);
			SelectVisibleItem(m_vSelItem[0]);
		}

		DragCleanup();
	}

	CTreeCtrlTT::OnLButtonUp(nFlags, point);
}

// Timer function to handle scrolling while dragging.
void DragDropTreeCtrl::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case TVHT_ABOVE:
		if (dragOp_DropNotAllowed != mDragState)
			CImageList::DragShowNolock(FALSE);
		SendMessage(WM_VSCROLL, SB_LINEUP, NULL);
		if (dragOp_DropNotAllowed != mDragState)
			CImageList::DragShowNolock(TRUE);
		return;
	case TVHT_BELOW:
		if (dragOp_DropNotAllowed != mDragState)
			CImageList::DragShowNolock(FALSE);
		SendMessage(WM_VSCROLL, SB_LINEDOWN, NULL);
		if (dragOp_DropNotAllowed != mDragState)
			CImageList::DragShowNolock(TRUE);
		return;
	case kDragHoverTimerId:
		KillTimer(kDragHoverTimerId);
		if (mItemDropTarget && TVIS_EXPANDED != GetItemState(mItemDropTarget, TVIS_EXPANDED))
		{
			CImageList::DragShowNolock(FALSE);
			Expand(mItemDropTarget, TVE_EXPAND);
			Invalidate();
			UpdateWindow();
			CImageList::DragShowNolock(TRUE);
		}
		return;
	}

	CTreeCtrlTT::OnTimer(nIDEvent);
}

// Selects the appropriate cursor in dragging mode
HTREEITEM
DragDropTreeCtrl::SelectDropCursor(CPoint* pPoint /*= NULL*/)
{
	if (!IsDragging())
		return NULL;

	HCURSOR hOldCursor = mCursors[mDragState];
	UINT uFlags = 0;
	HTREEITEM hitem = NULL;

	if (pPoint != NULL)
	{
		hitem = HitTest(*pPoint, &uFlags);

		// Set a Timer to scroll periodically, while dragging, if mouse cursor is
		// either below or above the control.

		// The timer must be set only if the flags have changed since the last call.
		if (mPreviousDragFlags != uFlags)
		{
			mPreviousDragFlags = uFlags;
			if (uFlags & TVHT_ABOVE)
			{
				SetTimer(TVHT_ABOVE, kScrollTimeLength, NULL);
				mIsTopTimerActive = true;
			}
			else if (uFlags & TVHT_BELOW)
			{
				SetTimer(TVHT_BELOW, kScrollTimeLength, NULL);
				mIsBottomTimerActive = true;
			}
			else
			{
				if (mIsTopTimerActive)
				{
					KillTimer(TVHT_ABOVE);
					mIsTopTimerActive = false;
				}
				if (mIsBottomTimerActive)
				{
					KillTimer(TVHT_BELOW);
					mIsBottomTimerActive = false;
				}
			}
		}

		// Dragging an item to itself or to a node of its subtree is
		// not allowed
		if (!hitem || IsAChildOfASelectedItem(mItemDropTarget))
		{
			mDragState = dragOp_DropNotAllowed;
			SetCursor(mCursors[mDragState]);
			return NULL;
		}
	}

	if (GetKeyState(VK_CONTROL) & 0x80000000)
		mDragState = dragOp_Copy;
	else
		mDragState = dragOp_Move;

	if (!hitem || (mDragState != dragOp_DropNotAllowed && !mDropHandler->IsAllowedToDrop(hitem, mDropAfterTarget)))
	{
		if (mDragState != dragOp_NotDragging) // may have been canceled in IsAllowedToDrop
			mDragState = dragOp_DropNotAllowed;
		hitem = NULL;
	}

	if (hOldCursor != mCursors[mDragState])
		SetCursor(mCursors[mDragState]);

	return hitem;
}

BOOL DragDropTreeCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (!IsDragging())
		return CTreeCtrlTT::OnSetCursor(pWnd, nHitTest, message);

	SelectDropCursor();
	SetCursor(mCursors[mDragState]);
	return 0;
}

BOOL DragDropTreeCtrl::IsChildNodeOf(HTREEITEM child, HTREEITEM suspectedParent) const
{
	do
	{
		if (child == suspectedParent)
			break;
	} while ((child = GetParentItem(child)) != NULL);

	return (child != NULL);
}

BOOL DragDropTreeCtrl::SelectVisibleItem(HTREEITEM item)
{
	const BOOL retval = SelectItem(item);
	EnsureVisible(item);
	SetScrollPos(SB_HORZ, 0);
	return retval;
}

void DragDropTreeCtrl::DragCleanup()
{
	if (IsDragging())
	{
		if (mIsTopTimerActive)
		{
			KillTimer(TVHT_ABOVE);
			mIsTopTimerActive = false;
		}
		if (mIsBottomTimerActive)
		{
			KillTimer(TVHT_BELOW);
			mIsBottomTimerActive = false;
		}
		KillTimer(kDragHoverTimerId);

		CImageList::DragLeave(this);
		CImageList::EndDrag();
		ReleaseCapture();
		SelectDropTarget(NULL);
		if (mDragImage != NULL)
		{
			delete mDragImage;
			mDragImage = NULL;
		}

		mDragState = dragOp_NotDragging;
		mItemDropTarget = NULL;
		mDropAfterTarget = true;
	}
	else
	{
		_ASSERTE(!mDragImage && !mIsBottomTimerActive && !mIsTopTimerActive);
	}

	Invalidate();
	UpdateWindow();
}

void DragDropTreeCtrl::OnKillFocus(CWnd* pNewWnd)
{
	if (IsDragging())
		CancelDrag();

	CTreeCtrlTT::OnKillFocus(pNewWnd);
	RepaintSelectedItems();
}

void DragDropTreeCtrl::OnSetFocus(CWnd* pOldWnd)
{
	CTreeCtrlTT::OnSetFocus(pOldWnd);
	RepaintSelectedItems();
}

void DragDropTreeCtrl::RepaintSelectedItems()
{
	//	TreeItemVec::iterator iter;
	//	for (iter = m_vSelItem.begin(); iter != m_vSelItem.end(); ++iter)
	//	{
	//		HTREEITEM hItem = *iter;
	//		RECT rect;
	//		GetItemRect(hItem, &rect, TRUE);
	//		InvalidateRect(&rect, true);
	//	}
	InvalidateRect(nullptr, true);
}

CImageList* DragDropTreeCtrl::CreateDragImage()
{
	// Find the bounding rectangle of all the selected items
	CRect rectBounding;  // Holds rectangle bounding area for bitmap
	CRect rectFirstItem; // Holds first item's height and width
	CRect rectTextArea;  // Holds text area of image
	size_t nNumSelected; // Holds total number of selected items
	HTREEITEM hItem;
	CClientDC DraggedNodeDC(this); // To draw drag image
	CDC* pDragImageCalcDC = NULL;  // to find the drag image width and height
	CString strItemText;
	CBitmap* pBitmapOldMemDCBitmap = NULL; // Pointer to bitmap in memory
	CFont* pFontOld = NULL;                // Used for  bitmap font
	size_t nIdx = 0;
	int nMaxWidth = 0;                        // holds the maximum width to be taken to form the bounding rect
	CImageList* pImageListDraggedNode = NULL; // Holds an image list pointer

	nNumSelected = m_vSelItem.size();
	if (nNumSelected <= 0)
		return NULL;

	pDragImageCalcDC = GetDC();
	if (pDragImageCalcDC == NULL)
		return NULL;

	CImageList* pImageList = GetImageList(TVSIL_NORMAL);

	int cx, cy;
	if (pImageList)
		ImageList_GetIconSize(*pImageList, &cx, &cy);
	else
		cx = cy = 0;
	// Calculate the maximum width of the bounding rectangle
	for (nIdx = 0; nIdx < nNumSelected; nIdx++)
	{
		// Get the item's height and width one by one
		hItem = m_vSelItem[nIdx];
		strItemText = GetItemText(hItem);
		rectFirstItem.SetRectEmpty();
		pDragImageCalcDC->DrawText(strItemText, rectFirstItem, DT_CALCRECT);
		if (nMaxWidth < (rectFirstItem.Width() + cx))
			nMaxWidth = rectFirstItem.Width() + cx;
	}

	// Get the first item's height and width
	hItem = m_vSelItem[0];
	strItemText = GetItemText(hItem);
	rectFirstItem.SetRectEmpty();
	pDragImageCalcDC->DrawText(strItemText, rectFirstItem, DT_CALCRECT);
	ReleaseDC(pDragImageCalcDC);

	// Initialize textRect for the first item
	rectTextArea.SetRect(1, 1, nMaxWidth, rectFirstItem.Height());

	// Find the bounding rectangle of the bitmap
	rectBounding.SetRect(0, 0, nMaxWidth + 2, (rectFirstItem.Height() + 2) * (int)nNumSelected);

	CDC MemoryDC; // Memory Device Context used to draw the drag image
	if (!MemoryDC.CreateCompatibleDC(&DraggedNodeDC))
		return NULL;

	CBitmap DraggedNodeBmp; // Instance used for holding  dragged bitmap
	if (!DraggedNodeBmp.CreateCompatibleBitmap(&DraggedNodeDC, rectBounding.Width(), rectBounding.Height()))
		return NULL;

	pBitmapOldMemDCBitmap = MemoryDC.SelectObject(&DraggedNodeBmp);
	pFontOld = MemoryDC.SelectObject(GetFont());

	CBrush brush(RGB(255, 255, 255));
	MemoryDC.FillRect(&rectBounding, &brush);
	MemoryDC.SetBkColor(RGB(255, 255, 255));
	MemoryDC.SetBkMode(TRANSPARENT);
	MemoryDC.SetTextColor(RGB(0, 0, 0));

	// Search through array list
	for (nIdx = 0; nIdx < nNumSelected; nIdx++)
	{
		hItem = m_vSelItem[nIdx];
		int nImg = 0, nSelImg = 0;
		GetItemImage(hItem, nImg, nSelImg);
		HICON hIcon = NULL;
		if (pImageList)
			hIcon = pImageList->ExtractIcon(nImg);
		MemoryDC.MoveTo(rectTextArea.left, rectTextArea.top);
		if (nIdx != nNumSelected - 1)
			MemoryDC.LineTo(rectTextArea.left, rectTextArea.top + 18);
		else
			MemoryDC.LineTo(rectTextArea.left, rectTextArea.top + 8);
		MemoryDC.MoveTo(rectTextArea.left, rectTextArea.top + 8);
		MemoryDC.LineTo(rectTextArea.left + 5, rectTextArea.top + 8);

		int nLeft = rectTextArea.left;
		rectTextArea.left += 3;
		if (hIcon)
		{
			::DrawIconEx(MemoryDC.m_hDC, rectTextArea.left, rectTextArea.top, hIcon, 16, 16, 0, NULL, DI_NORMAL);
			::DestroyIcon(hIcon);
		}
		rectTextArea.left += cx;
		MemoryDC.Rectangle(rectTextArea);
		MemoryDC.DrawText(GetItemText(hItem), rectTextArea, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		rectTextArea.left = nLeft;
		rectTextArea.OffsetRect(0, rectFirstItem.Height() + 2);
	}

	MemoryDC.SelectObject(pFontOld);
	MemoryDC.SelectObject(pBitmapOldMemDCBitmap);
	MemoryDC.DeleteDC();

	pImageListDraggedNode = new CImageList;
	pImageListDraggedNode->Create(rectBounding.Width(), rectBounding.Height(), ILC_COLOR | ILC_MASK, 0, 1);

	pImageListDraggedNode->Add(&DraggedNodeBmp, RGB(255, 255, 255));
	return pImageListDraggedNode;
}

void DragDropTreeCtrl::CancelDrag()
{
	// Cancel Drag Mode
	DragCleanup();
}

LRESULT
DragDropTreeCtrl::SendNotify(LPNMHDR pNMHDR)
{
	_ASSERTE(::GetParent(m_hWnd));
	return ::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, (WPARAM)pNMHDR->idFrom, (LPARAM)pNMHDR);
}

// CColumnTreeWndTempl::OnTreeCustomDraw won't be called because of:
// TN062: If, in your parent window class, you supply a handler for a specific WM_NOTIFY message or a range of WM_NOTIFY
// messages, your handler will be called only if the child control sending those messages does not have a reflected
// message handler through ON_NOTIFY_REFLECT().
void DragDropTreeCtrl::OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (outer_OnTreeCustomDraw)
		return outer_OnTreeCustomDraw(pNMHDR, pResult);
	else if (IsVS2010VAOutlineColouringActive())
		TreeVS2010CustomDraw(*this, this, pNMHDR, pResult, background_gradient_cache);
	else
		*pResult = CDRF_DODEFAULT;
}

BOOL DragDropTreeCtrl::OnEraseBkgnd(CDC* dc)
{
	if (IsVS2010VAOutlineColouringActive())
	{
		if (!mNoopErase && dc)
		{
			CRect rect;
			GetClientRect(&rect);
			CBrush br;
			if (br.CreateSolidBrush(g_IdeSettings->GetThemeTreeColor(L"Background", false)))
				dc->FillRect(rect, &br);
		}
		return TRUE;
	}
	else
		return VAColorWrapper<CTreeCtrlTT, PaintType::View>::OnEraseBkgnd(dc);
}

BOOL DragDropTreeCtrl::DeleteAllItems()
{
	// don't call ClearSelectionList since it changes item state and we're simply removing all items
	m_vSelItem.clear();
	return CTreeCtrlTT::DeleteAllItems();
}
