#if !defined(AFX_EDITCNTRLWND_H__77DC68D1_9843_11D1_8BAA_000000000000__INCLUDED_)
#define AFX_EDITCNTRLWND_H__77DC68D1_9843_11D1_8BAA_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// EditCntrlWnd.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// AfxFrameWnd window

class AfxFrameWnd : public CWnd
{
	// Construction
  public:
	AfxFrameWnd();

	// Attributes
  protected:
	MSG m_toolmsg;

	// Operations
  public:
	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(AfxFrameWnd)
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~AfxFrameWnd();
	BOOL Attach(HWND attachTo);
	HWND Detach();
	void MoveWindow(LPCRECT lpRect, BOOL bRepaint = TRUE);
	CWnd* SetFocus();

	// Generated message map functions
  protected:
	//{{AFX_MSG(AfxFrameWnd)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
  public:
	afx_msg LRESULT OnNcHitTest(CPoint point);
};

extern HWND g_hOurEditWnd;
extern INT g_EnableAddin;

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_EDITCNTRLWND_H__77DC68D1_9843_11D1_8BAA_000000000000__INCLUDED_)
