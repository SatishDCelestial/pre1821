#pragma once

#include "VAThemeDraw.h"

// SortSelectedLinesDlg dialog

class SortSelectedLinesDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(SortSelectedLinesDlg)

  public:
	SortSelectedLinesDlg(CWnd* pParent = NULL); // standard constructor
	virtual ~SortSelectedLinesDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum
	{
		IDD = IDD_SORTSELECTEDLINES
	};
#endif

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()

  public:
	CThemedRadioButton ComboAscending;
	CThemedRadioButton ComboDescending;
	CThemedRadioButton ComboSensitive;
	CThemedRadioButton ComboInsensitive;

	enum class eAscDesc
	{
		ASCENDING,
		DESCENDING,
	};

	enum class eCase
	{
		SENSITIVE,
		INSENSITIVE,
	};

	eAscDesc AscDesc;
	eCase Case;

	afx_msg void OnBnClickedAscending();
	afx_msg void OnBnClickedDescending();
	afx_msg void OnBnClickedSensitive();
	afx_msg void OnBnClickedInsensitive();
};
