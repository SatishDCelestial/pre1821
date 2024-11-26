#if !defined(AFX_BORDERCWND_H__7C63B2D2_E4F8_11D1_92F6_00805FC948C0__INCLUDED_)
#define AFX_BORDERCWND_H__7C63B2D2_E4F8_11D1_92F6_00805FC948C0__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
#include "CWndDpiAware.h"
// BorderCWnd.h : header file
//
#define MK_VA_SELLINE 0x40000
#define BORDER_SELECT_LINE 1

class EdCnt;
class BCMenu;

/////////////////////////////////////////////////////////////////////////////
// BorderCWnd window
class BorderCWnd : public CWndDpiAwareMiniHelp<CWnd>
{
	std::weak_ptr<EdCnt> m_weakEd;
	EdCnt* m_ed;
	bool m_colored;
	BCMenu* m_ContextMenu;
	CPoint m_menuPt;
	CStringW m_openFileName;
	int m_line;

	// Construction
  public:
	BorderCWnd(EdCntPtr ed);
	virtual ~BorderCWnd();

	// Operations
  public:
	BOOL DisplayMenu(CPoint point);
	void Flash(COLORREF color = RGB(0, 0, 255), DWORD duration = 3000);

	// Implementation
  public:
	void DrawBkGnd(bool dopaint = false);

  private:
	inline void DeleteMenu();
	void TrackPopup(const CPoint& pt);
	void PopulateAutotextMenu(BCMenu* menu, int itemsPerCol, bool standaloneMenu);
	void PopulateStandardContextMenu(const WTString& selstr, int& itemCnt, const int itemsPerCol);
	void PopulatePasteMenu(int& itemCnt, const int itemsPerCol);
	void AddRefactoringItems();
	//{{AFX_MSG(BorderCWnd)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnPaint();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	//}}AFX_MSG
	afx_msg void OnContextMenuSelection(UINT pick);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_BORDERCWND_H__7C63B2D2_E4F8_11D1_92F6_00805FC948C0__INCLUDED_)
