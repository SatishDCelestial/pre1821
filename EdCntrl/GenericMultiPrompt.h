#pragma once

#include "VADialog.h"
#include "resource.h"
#include "CtrlBackspaceEdit.h"
#include "VAThemeDraw.h"

class GenericMultiPrompt : public CThemedVADlg
{
	DECLARE_DYNAMIC(GenericMultiPrompt)

  public:
	GenericMultiPrompt(WTString title, WTString helpTopic);
	virtual ~GenericMultiPrompt()
	{
	}

  protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog();
	afx_msg void OnTextChanged();
	afx_msg void OnRadioButton1Selected()
	{
		RadioButtonSelected(0);
	}
	afx_msg void OnRadioButton2Selected()
	{
		RadioButtonSelected(1);
	}
	afx_msg void OnRadioButton3Selected()
	{
		RadioButtonSelected(2);
	}
	afx_msg void OnRadioButton4Selected()
	{
		RadioButtonSelected(3);
	}
	void RadioButtonSelected(int idx);

  public:
	// setup
	void SetSelectionLabel(WTString text)
	{
		mSelectionLabel = text;
	}
	void OverrideDefaultSelection()
	{
		mAllowDefaultSelectionOnInit = FALSE;
	}
	void AddOption(int optionValue, WTString optionLabel, WTString textLabelForOption, WTString defaultText);
	int GetOptionCount() const
	{
		return mTotalOptions;
	}

	// results
	WTString GetUserText() const
	{
		return mUserText;
	}
	int GetOptionSelected() const
	{
		_ASSERTE(mTotalOptions);
		return mOptionValues[mOptionSelected];
	}

  private:
	GenericMultiPrompt();

  private:
	enum
	{
		MAX_OPTIONS = 4
	};
	BOOL mAllowDefaultSelectionOnInit;
	int mTotalOptions;
	int mOptionSelected;
	int mOptionValues[MAX_OPTIONS];
	WTString mOptionText[MAX_OPTIONS];
	WTString mTextLabelPerOption[MAX_OPTIONS];
	WTString mOptionDefaultText[MAX_OPTIONS];
	WTString mSelectionLabel;
	WTString mCaption;

	WTString mUserText;

	CtrlBackspaceEdit<CThemedEdit> mEdit;
	CThemedRadioButton mRadios[MAX_OPTIONS];
};
