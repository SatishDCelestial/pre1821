
#pragma once

#if defined(SANCTUARY)

#include "../resource.h"
#include "../CWndTextW.h"

class CImportLicenseFileDlg : public CDialog
{
	// Construction
  public:
	CImportLicenseFileDlg(CWnd* parent); // standard constructor
	~CImportLicenseFileDlg();

	CStringW GetLicenseFileAttemptedToLoad() const
	{
		return mLicenseFile;
	}

	// Dialog Data
	//{{AFX_DATA(CImportLicenseFileDlg)
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CImportLicenseFileDlg)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  private:
	void OnOK() override;
	void OnInputChanged();
	void LaunchHelp();

	// Generated message map functions
	//{{AFX_MSG(CImportLicenseFileDlg)
	afx_msg void OnBrowse();
	afx_msg BOOL OnHelpInfo(HELPINFO* info);
	afx_msg void OnSysCommand(UINT, LPARAM);
	//}}AFX_MSG

	CWndTextW<CEdit> mEdit_subclassed;
	CStringW mLicenseFile;

	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif
