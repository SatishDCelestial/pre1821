// MaintenanceRenewal.cpp : implementation file
//

#include "stdafxed.h"
#include "MaintenanceRenewal.h"
#include "afxdialogex.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE [] = __FILE__;
#endif


// MaintenanceRenewal dialog

IMPLEMENT_DYNAMIC(MaintenanceRenewal, CDialog)

MaintenanceRenewal::MaintenanceRenewal(CWnd* pParent /*=NULL*/)
: CDialog(MaintenanceRenewal::IDD, pParent)
{

}

MaintenanceRenewal::~MaintenanceRenewal()
{
}

void MaintenanceRenewal::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_STATIC_MESSAGE, mMessage);
	DDX_Control(pDX, IDC_CHECK_REMIND_LATER, mRemindLater);
}


#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_MESSAGE_MAP(MaintenanceRenewal, CDialog)
	ON_BN_CLICKED(IDC_CHECK_REMIND_LATER, &MaintenanceRenewal::OnBnClickedCheckRemindLater)
	ON_WM_SIZE()
END_MESSAGE_MAP()
#pragma warning(pop)

BOOL MaintenanceRenewal::OnInitDialog()
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


// MaintenanceRenewal message handlers


void MaintenanceRenewal::OnBnClickedCheckRemindLater()
{
	m_bRemaindLater = !!mRemindLater.GetCheck();
}
