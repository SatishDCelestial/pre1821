#pragma once

#include "HTMLDialog\HtmlDialog.h"

class VaHtmlDlg : public CHtmlDialog
{
  public:
	VaHtmlDlg(UINT dlgResId, CWnd* pParent, UINT nID_HTML, UINT n_ID_static);
	~VaHtmlDlg();

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VaHtmlDlg)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual void _onHtmlCmd(UINT cmd, LPCTSTR params);
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual BOOL PrepareContentFile() = 0;

	// Generated message map functions
	//{{AFX_MSG(VaHtmlDlg)
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  protected:
	CStringW mContentFile;
};
