#include "StdAfxEd.h"
#include "GenericMultiPrompt.h"
#include "Colourizer.h"
#include "VAAutomation.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(GenericMultiPrompt, VADialog)

GenericMultiPrompt::GenericMultiPrompt(WTString title, WTString helpTopic)
    : CThemedVADlg(IDD_GenericMultiPrompt), mAllowDefaultSelectionOnInit(TRUE), mTotalOptions(0), mOptionSelected(0),
      mCaption(title)
{
	SetHelpTopic(helpTopic);
	UseEdCaretPos();
}

void GenericMultiPrompt::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_EDIT1, mEdit);
	if (pDX->m_bSaveAndValidate)
	{
		CStringW tmp;
		mEdit.GetText(tmp);
		mUserText = tmp;
	}
	else
		mEdit.SetText(mUserText.Wide());
}

BEGIN_MESSAGE_MAP(GenericMultiPrompt, CThemedVADlg)
ON_EN_CHANGE(IDC_EDIT1, OnTextChanged)
ON_BN_CLICKED(IDC_RADIOBUTTON1, OnRadioButton1Selected)
ON_BN_CLICKED(IDC_RADIOBUTTON2, OnRadioButton2Selected)
ON_BN_CLICKED(IDC_RADIOBUTTON3, OnRadioButton3Selected)
ON_BN_CLICKED(IDC_RADIOBUTTON4, OnRadioButton4Selected)
END_MESSAGE_MAP()

BOOL GenericMultiPrompt::OnInitDialog()
{
	__super::OnInitDialog();

	SetWindowText(mCaption.c_str());
	CWnd* selLabel = GetDlgItem(IDC_SELECTION_LABEL);
	selLabel->SetWindowText(mSelectionLabel.c_str());

#if 0
	// [case: 30326] do not colorize - causes paint problem.  Anyway, the symbol is undefined.
	::ColourizeControl(this, IDC_EDIT1);
#else
	::mySetProp(GetDlgItem(IDC_EDIT1)->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);
#endif

	mRadios[0].SubclassWindowW(::GetDlgItem(m_hWnd, IDC_RADIOBUTTON1));
	mRadios[1].SubclassWindowW(::GetDlgItem(m_hWnd, IDC_RADIOBUTTON2));
	mRadios[2].SubclassWindowW(::GetDlgItem(m_hWnd, IDC_RADIOBUTTON3));
	mRadios[3].SubclassWindowW(::GetDlgItem(m_hWnd, IDC_RADIOBUTTON4));

	AddSzControl(IDC_SELECTION_LABEL, mdResize, mdNone);
	AddSzControl(IDC_RADIOBUTTON1, mdResize, mdNone);
	AddSzControl(IDC_RADIOBUTTON2, mdResize, mdNone);
	AddSzControl(IDC_RADIOBUTTON3, mdResize, mdNone);
	AddSzControl(IDC_RADIOBUTTON4, mdResize, mdNone);
	AddSzControl(IDC_EDIT1_LABEL, mdResize, mdNone);
	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDOK, mdRepos, mdNone);
	AddSzControl(IDCANCEL, mdRepos, mdNone);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming(IDC_SELECTION_LABEL);
		Theme.AddDlgItemForDefaultTheming(IDC_EDIT1_LABEL);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	int idx;
	for (idx = 0; idx < mTotalOptions; ++idx)
	{
		CThemedRadioButton* pRadButton = &mRadios[idx];
		pRadButton->SetText(mOptionText[idx].Wide());
	}

	for (; idx < MAX_OPTIONS; ++idx)
	{
		CThemedRadioButton* pRadButton = &mRadios[idx];
		_ASSERTE(pRadButton);
		pRadButton->ShowWindow(SW_HIDE);
		pRadButton->EnableWindow(FALSE);
	}

	OnTextChanged();

	_ASSERTE(mTotalOptions);
	// default to first option
	CButton* pRadButton = (CButton*)GetDlgItem(IDC_RADIOBUTTON1);
	pRadButton->SetCheck(1);
	OnRadioButton1Selected();

	if (!mAllowDefaultSelectionOnInit)
	{
		if (mEdit.m_hWnd)
		{
			mEdit.SetFocus();
			mEdit.SetSel(0, 0);
			return FALSE;
		}
	}

	return TRUE; // return TRUE unless you set the focus to a control
}

void GenericMultiPrompt::OnTextChanged()
{
	CWnd* pOkButton = GetDlgItem(IDOK);
	CString edText;
	mEdit.GetWindowText(edText);
	pOkButton->EnableWindow(!edText.IsEmpty());
}

void GenericMultiPrompt::AddOption(int optionValue, WTString optionLabel, WTString textLabelForOption,
                                   WTString defaultText)
{
	_ASSERTE(optionLabel.GetLength());
	_ASSERTE(textLabelForOption.GetLength());
	_ASSERTE(mTotalOptions < MAX_OPTIONS &&
	         "I only support MAX_OPTIONS radio buttons"); // update the dlg template if you need more options
	mOptionValues[mTotalOptions] = optionValue;
	mTextLabelPerOption[mTotalOptions] = textLabelForOption;
	mOptionText[mTotalOptions] = optionLabel;
	mOptionDefaultText[mTotalOptions] = defaultText;
	if (mUserText.IsEmpty())
	{
		// first option is the default
		mUserText = defaultText;
	}

	if (gTestLogger)
	{
		WTString msg;
		msg.WTFormat("GenericMultiPrompt Option %d (%d):\r\n  option: %s\r\n  input label: %s\r\n  default: %s\r\n",
		             mTotalOptions, optionValue, optionLabel.c_str(), textLabelForOption.c_str(), defaultText.c_str());
		gTestLogger->LogStr(msg);
	}

	++mTotalOptions;
}

void GenericMultiPrompt::RadioButtonSelected(int idx)
{
	CStringW t;
	mEdit.GetText(t);
	WTString curText(t);
	if (curText.IsEmpty() || curText == mOptionDefaultText[mOptionSelected])
	{
		// update the default text is cur text is empty or if
		// cur text is the previous default (don't change if user has
		// modified the field)
		mEdit.SetText(mOptionDefaultText[idx].Wide());
	}

	mOptionSelected = idx;
	CWnd* pWnd = GetDlgItem(IDC_EDIT1_LABEL);
	pWnd->SetWindowText(mTextLabelPerOption[mOptionSelected].c_str());
}
