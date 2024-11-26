#pragma once

#if defined(SANCTUARY)

#include "../resource.h"
#include "../VAThemeDraw.h"
#include "../CtrlBackspaceEdit.h"

class CheckoutDurationDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(CheckoutDurationDlg)
  public:
	CheckoutDurationDlg(CWnd* parent); // standard constructor
	~CheckoutDurationDlg() = default;

	int GetDuration() const
	{
		return mDuration;
	}

	// Dialog Data
	//{{AFX_DATA(CheckoutDurationDlg)
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CheckoutDurationDlg)
  protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  private:
	// Generated message map functions
	//{{AFX_MSG(CheckoutDurationDlg)
	//}}AFX_MSG

	int mDuration = 24;
	CtrlBackspaceEdit<CThemedEditSHL<>> mEdit;

	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}

#endif
