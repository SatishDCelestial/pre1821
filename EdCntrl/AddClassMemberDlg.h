#pragma once

#include "VADialog.h"
#include "resource.h"
#include "CtrlBackspaceEdit.h"
#include "VAThemeDraw.h"

// AddClassMemberDlg dialog

class AddClassMemberDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(AddClassMemberDlg)

  public:
	enum DlgType
	{
		DlgAddMember,
		DlgAddSimilarMember,
		DlgChangeSignature,
		DlgCreateMethod,
		DlgIntroduceVariable
	};
	AddClassMemberDlg(DlgType dlgType, LPCSTR signature = NULL, LPCSTR extraText = NULL); // standard constructor
	virtual ~AddClassMemberDlg();

	// Dialog Data
	enum
	{
		IDD = IDD_CREATEMETHODDLG
	};

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support

	DECLARE_MESSAGE_MAP()
  public:
	afx_msg void OnEnChangeEdit1();
	afx_msg void OnBnClickedOk();
	virtual BOOL OnInitDialog();
	afx_msg void OnDestroy();

	void OverrideDefaultSelection()
	{
		mAllowDefaultSelectionOnInit = FALSE;
	}
	void SelectLastWord()
	{
		mSelectLastWord = TRUE;
	}
	WTString GetUserText() const
	{
		return mText;
	}

  private:
	const DlgType mDlgType;
	WTString mText;
	WTString mOptionalLabelText;
	BOOL mAllowDefaultSelectionOnInit;
	BOOL mSelectLastWord;
	CtrlBackspaceEdit<CThemedEditSHL<>> m_edit; // doesn't have to exist on dialog!!
};
