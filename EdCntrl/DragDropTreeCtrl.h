#pragma once

#include "TreeCtrlTT.h"
#include "SyntaxColoring.h"

class ITreeDropHandler;

#include <vector>
typedef std::vector<HTREEITEM> TreeItemVec;
class CGradientCache;

class DragDropTreeCtrl : public VAColorWrapper<CTreeCtrlTT, PaintType::View>
{
	DECLARE_DYNAMIC(DragDropTreeCtrl)
  public:
	DragDropTreeCtrl(ITreeDropHandler* dropHandler);

	BOOL IsDragging() const
	{
		return mDragState != dragOp_NotDragging;
	}

	int GetSelectedItemCount() const
	{
		return GetSelectedItem() ? (int)m_vSelItem.size() : 0;
	}
	const TreeItemVec& GetSelectedItems() const
	{
		return m_vSelItem;
	}
	bool IsSingleSelected() const
	{
		return GetSelectedItemCount() == 1;
	}
	bool IsMultipleSelected() const
	{
		return GetSelectedItemCount() > 1;
	}
	HTREEITEM GetSelectedItemIfSingleSelected() const
	{
		return IsSingleSelected() ? GetSelectedItem() : nullptr;
	}
	void CancelDrag();
	BOOL DeleteAllItems();

	afx_msg void OnBegindrag(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg BOOL OnEraseBkgnd(CDC*);

	DECLARE_MESSAGE_MAP()

  protected:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	void SetNoopErase(bool enable)
	{
		mNoopErase = enable;
	}

  private:
	BOOL SelectVisibleItem(HTREEITEM item);
	BOOL IsChildNodeOf(HTREEITEM child, HTREEITEM suspectedParent) const;
	HTREEITEM SelectDropCursor(CPoint* pPoint = NULL);
	CImageList* CreateDragImage();
	void DragCleanup();

	BOOL IsTripleClick(POINT pt) const;

	void OnControlClickItem(HTREEITEM hItem, BOOL forceSelected);
	void OnShiftClickItem(HTREEITEM hItem);
	void AddToSelectionList(HTREEITEM hItem);
	void RemoveFromSelectionList(HTREEITEM hItem);
	void ClearSelectionList();
	void RepaintSelectedItems();

	bool IsInSelectionList(HTREEITEM hItem) const;
	bool IsAChildOfASelectedItem(HTREEITEM hItem) const;
	bool IsAParentOfASelectedItem(HTREEITEM hItem) const;
	LRESULT SendNotify(LPNMHDR pNMHDR);

	enum DragState
	{
		dragOp_Move,           // has a cursor
		dragOp_Copy,           // .
		dragOp_DropNotAllowed, // .
		dragOp_CursorCount,
		dragOp_NotDragging // not a cursor
	};

	bool mIsTopTimerActive;
	bool mIsBottomTimerActive;
	bool mDropAfterTarget;
	bool mNoopErase;
	HTREEITEM mItemDropTarget;
	HCURSOR mCursors[dragOp_CursorCount];
	UINT mPreviousDragFlags;
	ITreeDropHandler* mDropHandler;
	DragState mDragState;
	CImageList* mDragImage;

	DWORD mLastDblClickTime;
	POINT mLastDblClickPt;

	TreeItemVec m_vSelItem;
	HTREEITEM mShiftSelStart;

	std::unique_ptr<CGradientCache> background_gradient_cache;

  public:
	std::function<void(NMHDR*, LRESULT*)> outer_OnTreeCustomDraw;

	friend class LiveOutlineFrame;
};
