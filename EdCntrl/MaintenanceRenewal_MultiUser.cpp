// MaintenanceRenewal_MultiUser.cpp : implementation file
//

#include "stdafxed.h"
#include "MaintenanceRenewal_MultiUser.h"
#include "afxdialogex.h"
#include "VaService.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "IVaLicensing.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE [] = __FILE__;
#endif


// MaintenanceRenewal_MultiUser dialog

IMPLEMENT_DYNAMIC(MaintenanceRenewal_MultiUser, CDialog)

MaintenanceRenewal_MultiUser::MaintenanceRenewal_MultiUser(CWnd* pParent /*=NULL*/)
: CDialog(MaintenanceRenewal_MultiUser::IDD, pParent)
{

}

MaintenanceRenewal_MultiUser::~MaintenanceRenewal_MultiUser()
{
}

void MaintenanceRenewal_MultiUser::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CHECK_REMINDER, mRemindLater);
	DDX_Control(pDX, IDC_STATIC_MESSAGE, mMessage);
}


#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(MaintenanceRenewal_MultiUser, CDialog)
	ON_BN_CLICKED(IDC_CHECK_REMINDER, &MaintenanceRenewal_MultiUser::OnBnClickedCheckReminder)
	ON_BN_CLICKED(IDC_BUTTON_EMAIL, &MaintenanceRenewal_MultiUser::OnBnClickedButtonEmail)
	ON_BN_CLICKED(IDC_BUTTON_GETQUOTE, &MaintenanceRenewal_MultiUser::OnBnClickedButtonGetQuote)
	ON_WM_SIZE()
END_MESSAGE_MAP()
#pragma warning(pop)

BOOL MaintenanceRenewal_MultiUser::OnInitDialog()
{
	const BOOL retval = __super::OnInitDialog();

	mMessage.SetWindowText(mMessageStr);
	mRemindLater.SetCheck(TRUE);
	m_bRemaindLater = true;

 	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
 	SetIcon(hTomato, TRUE);
 	SetIcon(hTomato, FALSE);

	return retval;
}

CString GetEmailFromLicense();

void LaunchEmailToGetQuote()
{
#if !defined(VA_CPPUNIT)
	CString email = GetEmailFromLicense();

	if (email.IsEmpty())
		return;

	//CString users;
	//CString__FormatA(users, "%d", gVaLicensingHost->GetLicenseUserCount());
	CString URL = "mailto:renewals@wholetomato.com";
	URL += _T("?subject=Multi-User License Renewal Inquiry");
	URL += _T("&body=Dear Whole Tomato Renewals Team,%0A%0A");
	URL += _T("I am writing to inquire about renewing our license for Visual Assist, which is due to expire shortly. As we have found the tool to be invaluable in our development work, we are keen to ensure uninterrupted access for our team.%0A%0A");
	URL += _T("User: ");
	URL += email;
// 	URL += _T("%0A");
// 	URL += _T("Number of Seats: ");
// 	URL += users;
	URL += _T("%0A%0A");
	URL += _T("Could you please provide us with a quote for the renewal?%0A%0A");
	URL += _T("Thank you for your assistance. We look forward to your prompt response and continuing our relationship with Whole Tomato Software.");
	::ShellExecute(NULL, _T("open"), URL, NULL, NULL, SW_SHOW);
#endif
}

void MaintenanceRenewal_MultiUser::OnBnClickedCheckReminder()
{
	m_bRemaindLater = !!mRemindLater.GetCheck();
}

extern void LaunchEmailToBuyer();

void MaintenanceRenewal_MultiUser::OnBnClickedButtonEmail()
{
	LaunchEmailToBuyer();
}


extern void LaunchPurchasePage();

void MaintenanceRenewal_MultiUser::OnBnClickedButtonGetQuote()
{
	LaunchEmailToGetQuote();

	//extern void LaunchRenewPage();
	//LaunchRenewPage();
}
