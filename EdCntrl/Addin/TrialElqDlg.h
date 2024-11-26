#pragma once

#include "..\VAThemeDraw.h"

class TrialElqDlg : public CHtmlDialog
{
  public:
	TrialElqDlg(CWnd* pParent = NULL);

	bool ShowElq();
	bool IsTrialActivated();

	// Dialog Data
	//{{AFX_DATA(TrialElqDlg)
	enum
	{
		IDD = IDD_TRIAL_ELQ
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(TrialElqDlg)
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual BOOL OnInitDialog();
	virtual void _onHtmlCmd(UINT cmd, LPCTSTR params);

	// Generated message map functions
	//{{AFX_MSG(TrialElqDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	bool mTrialActivated = false;
};

bool DisplayTrialElqDlg(CWnd* pParent);
