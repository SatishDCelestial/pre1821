#pragma once

#include "VaHtmlDlg.h"

class AboutDlg : public VaHtmlDlg
{
  public:
	AboutDlg(int IDD, CWnd* pParent = NULL);

	// Dialog Data
	//{{AFX_DATA(AboutDlg)
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(AboutDlg)
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual BOOL PrepareContentFile();

	// Generated message map functions
	//{{AFX_MSG(AboutDlg)
	afx_msg void OnCopyInfo();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	CStringW mSysInfo;
};

void DisplayAboutDlg();
