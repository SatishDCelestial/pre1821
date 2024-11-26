#if !defined(AFX_FastComboBoxListCtrl_H__41214ED5_3100_11D5_AB89_000000000000__INCLUDED_)
#define AFX_FastComboBoxListCtrl_H__41214ED5_3100_11D5_AB89_000000000000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// FastComboBoxListCtrl.h : header file
//
#include <afxtempl.h>
#include "WindowsHooks.h"

/////////////////////////////////////////////////////////////////////////////
// CFastComboBoxListCtrl window
class CFastComboBox;
class CGradientCache;

class CFastComboBoxListCtrl : public CWndDpiAware<CListCtrl>, protected WindowsHooks::CMouseHook
{
	// Construction
  public:
	CFastComboBoxListCtrl();

	// Attributes
  public:
	// Operations
  public:
	void Display(CRect rc);
	void Init(CFastComboBox* pComboParent, bool hasColorableContent);
	void SettingsChanged();
	virtual ~CFastComboBoxListCtrl();

	// Overrides
  protected:
	void OnMouseHookMessage(int code, UINT message, MOUSEHOOKSTRUCT* lpMHS) override; // case: 142798
	CStringW GetItemTextW(int nItem, int nSubItem) const;

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CFastComboBoxListCtrl)
  public:
  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam);
	//}}AFX_VIRTUAL
	virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const;
	virtual BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);

	// Implementation
	// Generated message map functions
	//{{AFX_MSG(CFastComboBoxListCtrl)
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
  private:
	int m_nLastItem;
	bool m_clipEnd;
	CFastComboBox* m_pComboParent;
	bool mHasColorableContent;
	std::unique_ptr<CGradientCache> background_gradient_cache;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FastComboBoxListCtrl_H__41214ED5_3100_11D5_AB89_000000000000__INCLUDED_)
