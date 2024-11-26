#pragma once

#include "ColorListControls.h"

class CTreeCtrlTT : public CColorVS2010TreeCtrl
{
	DECLARE_DYNAMIC(CTreeCtrlTT)
  public:
	CTreeCtrlTT();

	void PopTooltip();
	bool OkToShowTooltip() const;

	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnNMClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* result);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	INT_PTR OnToolHitTest(CPoint point, TOOLINFO* ti) const;

	virtual HTREEITEM InsertItem(LPTVINSERTSTRUCTA lpInsertStruct);
	virtual HTREEITEM InsertItem(UINT nMask, LPCSTR lpszItem, int nImage, int nSelectedImage, UINT nState,
	                             UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter);
	virtual HTREEITEM InsertItem(LPCSTR lpszItem, HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
	virtual HTREEITEM InsertItem(LPCSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent = TVI_ROOT,
	                             HTREEITEM hInsertAfter = TVI_LAST);

	virtual HTREEITEM InsertItemW(UINT nMask, LPCWSTR lpszItem, int nImage, int nSelectedImage, UINT nState,
	                              UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter);
	virtual HTREEITEM InsertItemW(LPCWSTR lpszItem, HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
	virtual HTREEITEM InsertItemW(LPCWSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent = TVI_ROOT,
	                              HTREEITEM hInsertAfter = TVI_LAST);

	BOOL SetItemTextW(_In_ HTREEITEM hItem, _In_z_ LPCWSTR lpszItem);
	CStringW GetItemTextW(_In_ HTREEITEM hItem);

	virtual void OnItemJustInserted(HTREEITEM item);

	DECLARE_MESSAGE_MAP()

  private:
	CFont mFont;
	HWND mLastTooltips;
	CPoint mPoppedTooltipPos;
};
