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

#pragma once
#include "../TreeCtrlTT.h"


class CColumnTreeCtrl : public CTreeCtrlTT
{
	DECLARE_DYNAMIC(CColumnTreeCtrl)
public:
	CColumnTreeCtrl();
	virtual ~CColumnTreeCtrl();

protected:
	void HandleMouse(UINT message, UINT nFlags, CPoint point);
	void CheckAllDescendants(HTREEITEM item, BOOL checked);

protected:
	int m_cxFirstCol;
	int m_cxTotal;

protected:
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

	friend class CColumnTreeView;
	template<typename> friend class CColumnTreeWndTempl;
	friend class ReferencesWndBase;
	friend class VaHashtagsFrame;
};



#include "../DragDropTreeCtrl.h"
#include "ITreeDropHandler.h"

// gmit: the same as CColumnTreeCtrl but with different base classes; merged in a template class didn't really looked nice.
class CColumnTreeCtrl2 : public DragDropTreeCtrl, protected ITreeDropHandler
{
	DECLARE_DYNAMIC(CColumnTreeCtrl2)
public:
	CColumnTreeCtrl2();
	virtual ~CColumnTreeCtrl2();


	virtual bool IsAllowedToStartDrag() override;
	virtual bool IsAllowedToDrop(HTREEITEM target, bool afterTarget) override;
	virtual void CopyDroppedItem(HTREEITEM target, bool afterTarget) override;
	virtual void MoveDroppedItem(HTREEITEM target, bool afterTarget) override;
	virtual void OnTripleClick() override;

// 	HTREEITEM GetSelectedItem() const = delete;

protected:
	void HandleMouse(const UINT message, const UINT nFlags, const CPoint point);
	void CheckAllDescendants(HTREEITEM item, BOOL checked);

protected:
	int m_cxFirstCol;
	int m_cxTotal;

protected:
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

	friend class CColumnTreeView;
	template<typename> friend class CColumnTreeWndTempl;
	friend class ReferencesWndBase;
	friend class VaHashtagsFrame;
};
