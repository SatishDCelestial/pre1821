#pragma once

#if defined(SANCTUARY)

#include "../resource.h"

class CWebRegistrationPrompt : public CDialog
{
	// Construction
  public:
	CWebRegistrationPrompt(CWnd* parent, CString serialNum, int regCode); // standard constructor
	~CWebRegistrationPrompt();

	// Dialog Data
	//{{AFX_DATA(CWebRegistrationPrompt)
	enum
	{
		IDD = IDD_WEB_ACTIVATION_PROMPT
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CWebRegistrationPrompt)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  private:
	void OnOK() override;

	// Generated message map functions
	//{{AFX_MSG(CWebRegistrationPrompt)
	afx_msg void OnCopyUrl();
	//}}AFX_MSG

	CStringW mUrl;

	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif
