#if !defined(AFX_EDITPARENTWND_H__16E28601_4BDD_11D1_8ADA_000000000000__INCLUDED_)
#define AFX_EDITPARENTWND_H__16E28601_4BDD_11D1_8ADA_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// EditParentWnd.h : header file
//

#include "EditCntrlWnd.h"
#include "MiniHelpFrm.h"

interface IGenericWindow;
interface ITextDocument;

/////////////////////////////////////////////////////////////////////////////
// EditParentWnd window

class EditParentWnd : public CWndDpiAwareMiniHelp<CWnd>
{
	// Construction
  public:
	EditParentWnd(const CStringW& file);
	virtual ~EditParentWnd();
	int m_fType;
	// Attributes
  private:
	AfxFrameWnd m_afxFrameCwnd;
	CStringW m_fileName;
	IGenericWindow* m_pWnd;
	ITextDocument* m_pDoc;
	MiniHelpFrm* m_miniHelp;
	// this is the last focused edit wnd - this can change when using the wnd splitter
	HWND m_hDevEdit;
	int m_wizHeight;
	int m_insideMyMove;
	CStringW mLastContext, mLastDef, mLastProject;

	// Operations

	// Overrides

	// Implementation
  public:
	BOOL Init(HWND replaceMe, HWND hAfxMDIfrm, HWND devEdit, IGenericWindow* pWnd, ITextDocument* pTextDoc);
	inline HWND GetEditWnd()
	{
		return m_hDevEdit;
	}
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	void RedoLayout(int x = 0, int y = 0);

	// Generated message map functions
  protected:
	void UnInit(int options);
	BOOL OnToolTipText(UINT id, NMHDR* pTTTStruct, LRESULT*);
	afx_msg LRESULT OnSetHelpTextW(WPARAM con, LPARAM def);
	afx_msg LRESULT OnSetHelpTextSync(WPARAM con, LPARAM def);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	void Display(bool show = true);
	HWND GetChildEditWithFocus();
	void DoWindowPosChange(WINDOWPOS* wndpos);
	void SetHelpText(LPCWSTR con, LPCWSTR def, LPCWSTR proj, bool async);

	//{{AFX_MSG(EditParentWnd)
	afx_msg void OnClose();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnDestroy();
	afx_msg BOOL OnNcActivate(BOOL bActive);
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_EDITPARENTWND_H__16E28601_4BDD_11D1_8ADA_000000000000__INCLUDED_)
