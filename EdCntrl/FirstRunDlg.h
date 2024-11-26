#pragma once

#include "resource.h"
#include "VAThemeDraw.h"

#if !defined(RAD_STUDIO)

class FirstRunDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(FirstRunDlg)

public:
	FirstRunDlg(CWnd* pParent = nullptr);
	virtual ~FirstRunDlg();

	static bool LaunchFirstRunDlg();

	// Dialog Data
	enum
	{
		IDD = IDD_FIRST_RUN
	};

protected:
	BOOL OnInitDialog() override;
	void DoDataExchange(CDataExchange* pDX) override; // DDX/DDV support
	
	void OnBnClickedPartial();
	void OnBnClickedFull();
	void OnBnClickedDetails();
	void OnBnClickedNext();

	void OnCancel() override;

	DECLARE_MESSAGE_MAP()
 
	CThemedButtonIcon mBtnPartial;
	CThemedButtonIcon mBtnFull;
	CThemedButton mBtnDetails;
	CThemedButton mBtnNext;
};

#endif