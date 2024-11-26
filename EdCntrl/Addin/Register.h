#if !defined(AFX_REGISTER_H__CC7DD9A4_BEB2_11D1_8BE9_000000000000__INCLUDED_)
#define AFX_REGISTER_H__CC7DD9A4_BEB2_11D1_8BE9_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

// Register.h : header file
//

#include "../resource.h"

/////////////////////////////////////////////////////////////////////////////
// CRegisterArmadillo dialog

class PasteInterceptEdit;

class CRegisterArmadillo : public CDialog
{
	// Construction
  public:
	CRegisterArmadillo(CWnd* parent); // standard constructor
	~CRegisterArmadillo();

	// Dialog Data
	//{{AFX_DATA(CRegisterArmadillo)
	enum
	{
		IDD = IDD_REGISTER_SINGLE_ENTRY
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRegisterArmadillo)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  private:
	CString mRegInfo;
	PasteInterceptEdit* mUserInputCtrl;

	void OnRegister(void);
	void RandomizeRecommendRenewalCheckbox();
	HINSTANCE LaunchHelp();

	// Generated message map functions
	//{{AFX_MSG(CRegisterArmadillo)
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg BOOL OnHelpInfo(HELPINFO* info);
	afx_msg void OnSysCommand(UINT, LPARAM);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

void InitLicensingHost();

#if defined(SANCTUARY)
__interface IVaAuxiliaryDll;
extern IVaAuxiliaryDll* gAuxDll;
#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_REGISTER_H__CC7DD9A4_BEB2_11D1_8BE9_000000000000__INCLUDED_)
