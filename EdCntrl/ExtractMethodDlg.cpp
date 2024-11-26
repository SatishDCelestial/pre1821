// ExtractMethodDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "WTString.h"
#include "ExtractMethodDlg.h"
#include "RegKeys.h"
#include "Registry.h"
#include "VAAutomation.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

#define ID_RK_EXTRECT_TO_SOURCE "ExtractToSource"
#define ID_RK_EXTRACT_AS_FREE_FUNCTION "ExtractAsFreeFunction"

// ExtractMethodDlg dialog

IMPLEMENT_DYNAMIC(ExtractMethodDlg, VADialog)
ExtractMethodDlg::ExtractMethodDlg(CWnd* pParent, const WTString& defText, const WTString& impText,
                                   const WTString& impText_free, BOOL canExtractToSrource /*= NULL*/,
                                   BOOL canExtractAsFreeFunction /*= FALSE*/,
                                   const WTString& customTitle /*= WTString()*/)
    : CThemedVADlg(/*"ExtractMethodDlg",*/ ExtractMethodDlg::IDD, pParent),
      m_customTitle(customTitle)
{
	m_newName = m_orgName = defText;
	m_orgSignature = impText;
	m_orgSignature_Free = impText_free;
	m_canExtractToSrc = canExtractToSrource;
	m_canExtractAsFreeFunction = canExtractAsFreeFunction;
	m_extractToSrc = FALSE;
	m_extractAsFreeFunction = FALSE;
	if (canExtractToSrource && !gTestsActive)
		m_extractToSrc = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_EXTRECT_TO_SOURCE, false);
	if (canExtractAsFreeFunction && !gTestsActive)
		m_extractAsFreeFunction = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_EXTRACT_AS_FREE_FUNCTION, false);

	if (m_extractAsFreeFunction)
		m_newSignature = impText_free;
	else
		m_newSignature = impText;

	SetHelpTopic("dlgExtractMethod");
}

ExtractMethodDlg::~ExtractMethodDlg()
{
	if (m_canExtractToSrc && !gTestsActive) // Remember setting
		::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_EXTRECT_TO_SOURCE, !!m_extractToSrc);
	if (m_canExtractAsFreeFunction && !gTestsActive) // Remember setting
		::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_EXTRACT_AS_FREE_FUNCTION, !!m_extractAsFreeFunction);
}

void ExtractMethodDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	// name of new method
	DDX_Control(pDX, IDC_EDIT1, m_newName_ctrl);
	DWORD sel = 0;
	CEdit* ed = (CEdit*)GetDlgItem(IDC_EDIT1);
	if (ed)
		sel = ed->GetSel();

	if (pDX->m_bSaveAndValidate)
	{
		CStringW tmp;
		m_newName_ctrl.GetText(tmp);
		m_newName = tmp;
	}
	else
		m_newName_ctrl.SetText(m_newName.Wide());
	if (ed)
		ed->SetSel(sel);

	// checkboxes
	if (m_canExtractToSrc)
		DDX_Check(pDX, IDC_EXTRACT_TO_SOURCE, m_extractToSrc);
	else
		GetDlgItem(IDC_EXTRACT_TO_SOURCE)->ShowWindow(SW_HIDE);

	if (m_canExtractAsFreeFunction)
		DDX_Check(pDX, IDC_EXTRACT_AS_FREE_FUNC, m_extractAsFreeFunction);
	else
		GetDlgItem(IDC_EXTRACT_AS_FREE_FUNC)->ShowWindow(SW_HIDE);

	// signature of new method
	DDX_Control(pDX, IDC_EDIT2, m_newSignature_ctrl);
	if (pDX->m_bSaveAndValidate)
	{
		CStringW tmp;
		m_newSignature_ctrl.GetText(tmp);
		m_newSignature = tmp;
	}
	else
		m_newSignature_ctrl.SetText(m_newSignature.Wide());

	EnableDisableExtractToSource();
}

BEGIN_MESSAGE_MAP(ExtractMethodDlg, CThemedVADlg)
ON_BN_CLICKED(IDOK, OnBnClickedOk)
ON_BN_CLICKED(IDC_EXTRACT_AS_FREE_FUNC, OnCheckFreeFunction)
ON_EN_CHANGE(IDC_EDIT1, OnNameChanged)
END_MESSAGE_MAP()

// ExtractMethodDlg message handlers
void ExtractMethodDlg::OnBnClickedOk()
{
	UpdateData(TRUE);
	OnOK();
}

BOOL ExtractMethodDlg::OnInitDialog()
{
	BOOL r = __super::OnInitDialog();

	AddSzControl(IDC_EDIT1_LABEL, mdNone, mdNone);
	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDC_EDIT2_LABEL, mdNone, mdNone);
	AddSzControl(IDC_EDIT2, mdResize, mdResize);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);
	AddSzControl(IDC_EXTRACT_TO_SOURCE, mdNone, mdRepos);
	AddSzControl(IDC_EXTRACT_AS_FREE_FUNC, mdNone, mdRepos);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_EDIT1_LABEL);
		// IDC_EDIT1 is handled by m_newName_ctrl
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_EDIT2_LABEL);
		// IDC_EDIT2 is handled by m_newSignature_ctrl
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_EXTRACT_TO_SOURCE, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_EXTRACT_AS_FREE_FUNC, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	CEdit* ed = (CEdit*)GetDlgItem(IDC_EDIT1);
	if (ed)
		ed->SetSel(0, -1);

    if (!m_customTitle.IsEmpty())
		SetWindowText(m_customTitle.c_str());
	VAUpdateWindowTitle(VAWindowType::ExtractMethod, *this);

	LogForTest();
	return r;
}

void ExtractMethodDlg::OnNameChanged()
{
	UpdateData(TRUE);
	UpdateSignature();
	GetDlgItem(IDOK)->EnableWindow(m_newName.IsEmpty() ? false : true);
}

void ExtractMethodDlg::OnCheckFreeFunction()
{
	UpdateData(TRUE);
	//	m_extractAsFreeFunction = ((CButton*)GetDlgItem(IDC_EXTRACT_AS_FREE_FUNC))->GetCheck(); // DDX_Check should do
	// this? It doesn't.
	if (m_canExtractToSrc)
		EnableDisableExtractToSource();

	UpdateSignature();
}

bool ExtractMethodDlg::IsExtractToSrc()
{
	if (!m_canExtractToSrc)
		return false;

	if (!m_extractAsFreeFunction || g_currentEdCnt->m_ftype == Header)
		return !!m_extractToSrc;
	else
		return false;
}

void ExtractMethodDlg::UpdateSignature()
{
	WTString s(m_extractAsFreeFunction ? m_orgSignature_Free : m_orgSignature);
	s.ReplaceAll(m_orgName.c_str(), m_newName.c_str(), TRUE);
	m_newSignature = s.c_str();
	UpdateData(FALSE);
	LogForTest();
}

void ExtractMethodDlg::EnableDisableExtractToSource()
{
	if (!m_extractAsFreeFunction || g_currentEdCnt->m_ftype == Header)
		GetDlgItem(IDC_EXTRACT_TO_SOURCE)->EnableWindow(true);
	else
		GetDlgItem(IDC_EXTRACT_TO_SOURCE)->EnableWindow(false);
}

void ExtractMethodDlg::LogForTest()
{
	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		WTString logStr;
		logStr.WTFormat("Extract Method dlg:\r\n	origName(%s) newName(%s)", m_orgName.c_str(), m_newName.c_str());
		gTestLogger->LogStr(logStr);

		logStr.WTFormat("	canExToSrc(%d) canExFree(%d)", m_canExtractToSrc, m_canExtractAsFreeFunction);
		gTestLogger->LogStr(logStr);

		logStr.WTFormat("	exToSrc(%d) exFree(%d)", m_extractToSrc, m_extractAsFreeFunction);
		gTestLogger->LogStr(logStr);

		logStr.WTFormat("	origSig:	%s\r\n	origSigF:	%s\r\n	newSig:		%s", m_orgSignature.c_str(),
		                m_orgSignature_Free.c_str(), m_newSignature.c_str());
		gTestLogger->LogStr(logStr);
	}
}
