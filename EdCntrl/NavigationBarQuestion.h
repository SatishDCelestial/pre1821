#pragma once

#include "VADialog.h"
#include "resource.h"
#include "VAThemeDraw.h"

// NavigationBarQuestion dialog

class NavigationBarQuestion : public CThemedVADlg
{
	DECLARE_DYNAMIC(NavigationBarQuestion)

  public:
	NavigationBarQuestion(CWnd* pParent = NULL); // standard constructor
	virtual ~NavigationBarQuestion();

	// Dialog Data
	enum
	{
		IDD = IDD_NAVBAR_QUESTION
	};

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
  public:
	CThemedCheckBox mAskMeLater;
	CThemedStaticNormal mMessage;
	bool m_bAskMeLater;

	afx_msg void OnBnClickedAskMeLater();
};
