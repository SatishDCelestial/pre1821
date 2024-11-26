#pragma once

#if defined(SANCTUARY)

#include "../resource.h"
#include "VAThemeDraw.h"
#include "CtrlBackspaceEdit.h"

class CRegisterSanctuary : public CDialog
{
	// Construction
  public:
	CRegisterSanctuary(CWnd* parent); // standard constructor
	~CRegisterSanctuary();

	// Dialog Data
	//{{AFX_DATA(CRegisterSanctuary)
	enum
	{
		IDD = IDD_REGISTER_SANCTUARY
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRegisterSanctuary)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  private:
	void OnRegister();
	void LaunchHelp();
	void OnInputChanged();
	CString GetSafeSerialNumber();

	// Generated message map functions
	//{{AFX_MSG(CRegisterSanctuary)
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg BOOL OnHelpInfo(HELPINFO* info);
	afx_msg void OnSysCommand(UINT, LPARAM);
	afx_msg void OnLinkClick(UINT id);
	afx_msg void OnWebActivation();
	afx_msg void OnLegacyRegister();
	afx_msg void OnImportLicense();
	//}}AFX_MSG

	int mRegistrationCode;
	bool mWarnOnLongSerial = true;
	CString mInputSerialNumber;
	CString mInputNameOrEmail;
	CString mInputPassword;
	CStringW mLicenseFileAttemptedToLoad;

	CtrlBackspaceEdit<CThemedDimEdit, true> mSerialNumberEdit; // // case: 142819 - 'true' to use default font
	CStaticLink mLinkGetTrial;
	CStaticLink mLinkCreateAccount;
	CStaticLink mLinkResetPassword;
	CStaticLink mLinkWebRegistration;
	CStaticLink mLinkSupport;
	CStaticLink mLinkHelpRegistration;
	CStaticLink mLinkPrivacyPolicy;
	CStaticLink mLinkLegacyRegistration;
	CStaticLink mLinkImportRegistrationFile;

	DECLARE_MESSAGE_MAP()
};

void ReportRegistrationSuccess(HWND hWnd);

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif
