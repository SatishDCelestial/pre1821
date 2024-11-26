#include "StdAfxEd.h"

#if defined(SANCTUARY)

#include "WebRegistrationPrompt.h"
#include "WindowUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CWebRegistrationPrompt::CWebRegistrationPrompt(CWnd* parent, CString serialNum, int regCode)
    : CDialog(CWebRegistrationPrompt::IDD, parent)
{
	//{{AFX_DATA_INIT(CWebRegistrationPrompt)
	//}}AFX_DATA_INIT
	if (regCode)
		CString__FormatW(mUrl,
		                 L"https://www.wholetomato.com/support/tooltip.asp?option=Activation&serialNumber=%s&key=%d",
		                 (const wchar_t*)(serialNum.IsEmpty() ? CStringW() : CStringW(serialNum)), regCode);
	else
		CString__FormatW(mUrl,
		                 L"https://www.wholetomato.com/support/tooltip.asp?option=Activation&serialNumber=%s&key=",
		                 (const wchar_t*)(serialNum.IsEmpty() ? CStringW() : CStringW(serialNum)));
}

CWebRegistrationPrompt::~CWebRegistrationPrompt()
{
}

void CWebRegistrationPrompt::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CWebRegistrationPrompt)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CWebRegistrationPrompt, CDialog)
//{{AFX_MSG_MAP(CWebRegistrationPrompt)
ON_BN_CLICKED(IDC_BUTTON_COPY, OnCopyUrl)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWebRegistrationPrompt message handlers

BOOL CWebRegistrationPrompt::OnInitDialog()
{
	const BOOL retval = CDialog::OnInitDialog();

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	SetDlgItemTextW(GetSafeHwnd(), IDC_EDIT_URL, mUrl);

	return retval;
}

void CWebRegistrationPrompt::OnOK()
{
	__super::OnOK();
	::ShellExecuteW(nullptr, L"open", mUrl, nullptr, nullptr, SW_SHOW);
}

void CWebRegistrationPrompt::OnCopyUrl()
{
	::SetClipText(mUrl);
}

#endif
