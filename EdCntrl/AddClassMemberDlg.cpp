// AddClassMemberDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "AddClassMemberDlg.h"
#include "VAAutomation.h"
#include "VAWatermarks.h"

// AddClassMemberDlg dialog

IMPLEMENT_DYNAMIC(AddClassMemberDlg, VADialog)
AddClassMemberDlg::AddClassMemberDlg(DlgType dlgType, LPCSTR signature /*= NULL*/, LPCSTR extraText /*= NULL*/)
    : CThemedVADlg(AddClassMemberDlg::IDD, NULL, fdHorz), mDlgType(dlgType), mText(signature),
      mOptionalLabelText(extraText), mAllowDefaultSelectionOnInit(TRUE), mSelectLastWord(FALSE)
{
	UseEdCaretPos();
}

AddClassMemberDlg::~AddClassMemberDlg()
{
}

void AddClassMemberDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	if (pDX->m_bSaveAndValidate)
	{
		CStringW tmp;
		m_edit.GetText(tmp);
		mText = tmp;
	}
	else
		m_edit.SetText(mText.Wide());
}

BEGIN_MESSAGE_MAP(AddClassMemberDlg, CThemedVADlg)
ON_EN_CHANGE(IDC_EDIT1, OnEnChangeEdit1)
ON_BN_CLICKED(IDOK, OnBnClickedOk)
ON_WM_DESTROY()
END_MESSAGE_MAP()

// AddClassMemberDlg message handlers

void AddClassMemberDlg::OnEnChangeEdit1()
{
	CWnd* pOkButton = GetDlgItem(IDOK);
	CWnd* pEdit = GetDlgItem(IDC_EDIT1);
	_ASSERTE(pEdit->GetSafeHwnd() == m_edit.GetSafeHwnd());
	CString edText;
	pEdit->GetWindowText(edText);
	pOkButton->EnableWindow(!edText.IsEmpty());
}

void AddClassMemberDlg::OnBnClickedOk()
{
	OnOK();
}

BOOL AddClassMemberDlg::OnInitDialog()
{
	__super::OnInitDialog();

	switch (mDlgType)
	{
	case DlgChangeSignature:
		SetHelpTopic("dlgChangeSignature");
		SetWindowText("Change Signature");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Change Signature dlg"));
		break;

	case DlgAddSimilarMember:
		SetHelpTopic("dlgAddSimilarMember");
		SetWindowText("Add Similar Member");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Add Similar Member dlg"));
		break;

	case DlgAddMember:
		SetHelpTopic("dlgAddMember");
		SetWindowText("Add Member");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Add Member dlg"));
		break;

	case DlgCreateMethod:
		SetHelpTopic("dlgCreateMethod");
		SetWindowText("Create Method");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Create Method dlg"));
		break;

	case DlgIntroduceVariable:
		SetHelpTopic("dlgIntroduceVariable");
		SetWindowText("Introduce Variable");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Introduce Variable dlg"));
		break;

	default:
		_ASSERTE(!"unhandled mDlgType");
		break;
	}

	if (!mOptionalLabelText.IsEmpty())
	{
		CWnd* pLabel = GetDlgItem(IDC_EDIT1_LABEL);
		pLabel->SetWindowText(mOptionalLabelText.c_str());
		if (gTestLogger)
		{
			WTString msg;
			msg.WTFormat("\tlabel: %s", mOptionalLabelText.c_str());
			gTestLogger->LogStr(msg);
		}
	}

	if (gTestLogger)
		gTestLogger->LogStr(WTString("\t" + mText));

	if (GetDlgItem(IDC_EDIT1))
	{
		// [case: 9194] do not use DDX_Control due to ColourizeControl.
		// Subclass with colourizer before SHAutoComplete (CtrlBackspaceEdit).
		m_edit.SubclassWindow(GetDlgItem(IDC_EDIT1)->m_hWnd);
		m_edit.SetText(mText.Wide());
	}

	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDOK, mdRepos, mdNone);
	AddSzControl(IDCANCEL, mdRepos, mdNone);
	AddSzControl(IDC_EDIT1_LABEL, mdResize, mdNone);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_EDIT1_LABEL);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	OnEnChangeEdit1();

	VAUpdateWindowTitle(VAWindowType::AddClassMember, *this);

	if (!mAllowDefaultSelectionOnInit)
	{
		if (m_edit.GetSafeHwnd())
		{
			m_edit.SetFocus();
			m_edit.SetSel(0, 0);
			return FALSE;
		}
	}

	if (mSelectLastWord)
	{
		int pos = mText.ReverseFind(' ');
		if (pos != -1)
		{
			if (m_edit.GetSafeHwnd())
			{
				m_edit.SetFocus();
				// [case: 138734] len must be in utf16 elements
				m_edit.SetSel(pos + 1, mText.Wide().GetLength());
				return FALSE;
			}
		}
	}

	return TRUE; // return TRUE unless you set the focus to a control
}

void AddClassMemberDlg::OnDestroy()
{
	if (m_edit.m_hWnd)
		m_edit.UnsubclassWindow();

	VADialog::OnDestroy();
}
