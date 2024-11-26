#include "StdAfxEd.h"

#if defined(SANCTUARY)

#include "RegisterSanctuary.h"
#include "IVaLicensing.h"
#include "LogVersionInfo.h"
#include "RegKeys.h"
#include "Registry.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "Addin/Register.h"
#include "Addin/Register_Strings.h"
#include "WebRegistrationPrompt.h"
#include "ISanctuaryClient.h"
#include "IVaAuxiliaryDll.h"
#include "ImportLicenseFileDlg.h"
#if !defined(NO_ARMADILLO)
#include "Armadillo/Armadillo.h"
#include "../WTKeyGen/WTValidate.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CRegisterSanctuary::CRegisterSanctuary(CWnd* parent) : CDialog(CRegisterSanctuary::IDD, parent)
{
	//{{AFX_DATA_INIT(CRegisterSanctuary)
	//}}AFX_DATA_INIT
	_ASSERTE(gVaLicensingHost);

	mRegistrationCode = 0;
	if (!gAuxDll)
	{
		_ASSERTE(!"no aux dll");
		return;
	}

	auto sanct = gAuxDll->GetSanctuaryClient();
	if (sanct)
	{
		mRegistrationCode = sanct->GetRegistrationCode();
		mInputNameOrEmail = sanct->GetPortalUserName();
	}
}

CRegisterSanctuary::~CRegisterSanctuary()
{
}

void CRegisterSanctuary::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRegisterSanctuary)
	DDX_Text(pDX, IDC_EDIT_SERIAL_NUMBER, mInputSerialNumber);
	DDV_MaxChars(pDX, mInputSerialNumber, 200);
	DDX_Text(pDX, IDC_EDIT_LOGIN_NAME, mInputNameOrEmail);
	DDV_MaxChars(pDX, mInputNameOrEmail, 200);
	DDX_Text(pDX, IDC_EDIT_LOGIN_PASSWORD, mInputPassword);
	DDV_MaxChars(pDX, mInputPassword, 40);

	DDX_Control(pDX, IDC_LINK_GET_TRIAL, mLinkGetTrial);
	DDX_Control(pDX, IDC_LINK_CREATE_ACCOUNT, mLinkCreateAccount);
	DDX_Control(pDX, IDC_LINK_RESET_PASSWORD, mLinkResetPassword);
	DDX_Control(pDX, IDC_LINK_WEB_REGISTRATION, mLinkWebRegistration);
	DDX_Control(pDX, IDC_LINK_SUPPORT, mLinkSupport);
	DDX_Control(pDX, IDC_LINK_HELP_REGISTRATION, mLinkHelpRegistration);
	DDX_Control(pDX, IDC_LINK_PRIVACY_POLICY, mLinkPrivacyPolicy);
	DDX_Control(pDX, IDC_LINK_LEGACY_REGISTRATION, mLinkLegacyRegistration);
	DDX_Control(pDX, IDC_LINK_IMPORT_REGISTRATION_FILE, mLinkImportRegistrationFile);

	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CRegisterSanctuary, CDialog)
//{{AFX_MSG_MAP(CRegisterSanctuary)
ON_WM_ACTIVATE()
ON_BN_CLICKED(IDOK, OnRegister)
ON_BN_CLICKED(IDCANCEL, OnCancel)
ON_WM_HELPINFO()
ON_WM_SYSCOMMAND()
ON_EN_CHANGE(IDC_EDIT_SERIAL_NUMBER, OnInputChanged)
ON_EN_CHANGE(IDC_EDIT_LOGIN_NAME, OnInputChanged)
ON_EN_CHANGE(IDC_EDIT_LOGIN_PASSWORD, OnInputChanged)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_GET_TRIAL, IDC_LINK_GET_TRIAL, OnLinkClick)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_CREATE_ACCOUNT, IDC_LINK_CREATE_ACCOUNT, OnLinkClick)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_RESET_PASSWORD, IDC_LINK_RESET_PASSWORD, OnLinkClick)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_SUPPORT, IDC_LINK_SUPPORT, OnLinkClick)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_HELP_REGISTRATION, IDC_LINK_HELP_REGISTRATION, OnLinkClick)
ON_CONTROL_RANGE(STN_CLICKED, IDC_LINK_PRIVACY_POLICY, IDC_LINK_PRIVACY_POLICY, OnLinkClick)
ON_STN_CLICKED(IDC_LINK_LEGACY_REGISTRATION, OnLegacyRegister)
ON_STN_CLICKED(IDC_LINK_IMPORT_REGISTRATION_FILE, OnImportLicense)
ON_STN_CLICKED(IDC_LINK_WEB_REGISTRATION, OnWebActivation)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CRegisterSanctuary message handlers

BOOL CRegisterSanctuary::OnInitDialog()
{
	const BOOL retval = CDialog::OnInitDialog();

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	CString tmp(IDS_APPNAME " Registration");
	tmp += "  (" + ::GetVaVersionInfo() + ")";
	SetWindowText(tmp);

	CString__FormatA(tmp, "%d", mRegistrationCode);
	SetDlgItemText(IDC_EDIT_REGISTRATION_CODE, tmp);

	CWnd* tmpWnd = GetDlgItem(IDOK);
	tmpWnd->EnableWindow(FALSE);

	mLinkGetTrial.EnableWindow(FALSE);
	mLinkGetTrial.ShowWindow(SW_HIDE);

	// [case: 137541]
	mSerialNumberEdit.SubclassWindow(::GetDlgItem(m_hWnd, IDC_EDIT_SERIAL_NUMBER));
	mSerialNumberEdit.SetDimText("XXXX-XXXXXX-XXXXXX-XXXX");

#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr && ptr->checkStatus() && ptr->HasProductInfo())
		{
			if (!ptr->IsLicenseValidForThisBuild(false))
			{
				if (!ptr->IsNamedNetworkLicenseInstalled() && !ptr->IsConcurrentLicenseInstalled())
				{
					if (ptr->IsRenewableLicense())
					{
						const CStringW sn(ptr->GetSerialNumber());
						if (!sn.IsEmpty())
						{
							// [case: 141951]
							// we don't have a check for eslip, so there is a small possibility
							// that we display serial when we shouldn't.  But eslips were only
							// issued for two months in 2019.  Most have migrated to ELC.
							mSerialNumberEdit.SetText(sn);
						}
					}
				}
			}
		}
	}
#endif

	mLinkGetTrial.SizeToContent();
	mLinkCreateAccount.SizeToContent();
	mLinkResetPassword.SizeToContent();
	mLinkWebRegistration.SizeToContent();
	mLinkSupport.SizeToContent();
	mLinkHelpRegistration.SizeToContent();
	mLinkPrivacyPolicy.SizeToContent();
	mLinkLegacyRegistration.SizeToContent();
	mLinkImportRegistrationFile.SizeToContent();

	return retval;
}

void CRegisterSanctuary::LaunchHelp()
{
	::ShellExecuteW(nullptr, L"open", L"http://www.wholetomato.com/support/tooltip.asp?option=register", nullptr,
	                nullptr, SW_SHOW);
}

void CRegisterSanctuary::OnInputChanged()
{
	bool enable = false;
	CWnd* tmpWnd;
	CString tmpTxt;

	tmpWnd = GetDlgItem(IDC_EDIT_SERIAL_NUMBER);
	tmpWnd->GetWindowText(tmpTxt);
	if (!tmpTxt.IsEmpty())
	{
		if (mWarnOnLongSerial && tmpTxt.GetLength() > 50)
		{
			mWarnOnLongSerial = false;
			const char* promptTxt = "The serial number you entered is too long. If you have a two-line activation key "
			                        "generated before Feb 18, 2019, you need to enter it in a different dialog.\n\n"
			                        "Would you like to open the legacy product registration dialog now?";
			if (IDYES == WtMessageBox(m_hWnd, promptTxt, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
			{
				OnLegacyRegister();
				return;
			}
		}

		tmpTxt.Empty();
		tmpWnd = GetDlgItem(IDC_EDIT_LOGIN_NAME);
		tmpWnd->GetWindowText(tmpTxt);
		if (!tmpTxt.IsEmpty())
		{
			tmpTxt.Empty();
			tmpWnd = GetDlgItem(IDC_EDIT_LOGIN_PASSWORD);
			tmpWnd->GetWindowText(tmpTxt);
			if (!tmpTxt.IsEmpty())
				enable = true;
		}
	}

	tmpWnd = GetDlgItem(IDOK);
	tmpWnd->EnableWindow(enable);
}

CString CRegisterSanctuary::GetSafeSerialNumber()
{
	if (-1 != mInputSerialNumber.FindOneOf("@#^%$<>{}[];?/\\'\""))
		mInputSerialNumber.Empty();

	return mInputSerialNumber;
}

void CRegisterSanctuary::OnSysCommand(UINT msg, LPARAM lParam)
{
	if (msg == SC_CONTEXTHELP)
		LaunchHelp();
	else
		__super::OnSysCommand(msg, lParam);
}

BOOL CRegisterSanctuary::OnHelpInfo(HELPINFO* info)
{
	LaunchHelp();
	return TRUE;
}

void CRegisterSanctuary::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CDialog::OnActivate(nState, pWndOther, bMinimized);
}

void ResetArmadilloInfo()
{
#if !defined(NO_ARMADILLO)
	// can't rely on ValidateVAXUserInfo since user may have deleted
	// UserName or UserKey from VA registry.  That would cause
	// ValidateVAXUserInfo to return LicenseStatus_Invalid for
	// VA but armadillo network count would still see the key as
	// being active.
	LicenseStatus license = LicenseStatus_Valid; // ValidateVAXUserInfo();
	if (license == LicenseStatus_Expired || license == LicenseStatus_Valid)
	{
		// [case: 137590]
		// switch to default certificate so that network check does not
		// count potentially previously installed key from responding to
		// other instances of the same key
		Armadillo::SetDefaultKey();

		const LPCTSTR valNames[] = {"AutoRegLicenseUser",    "AutoRegLicenseKey", "AutoRegPrevLicenseUser",
		                            "AutoRegPrevLicenseKey", "UserName",          "UserKey"};

		// delete username and key to prevent auto-register of old armadillo info
		for (auto it : valNames)
			DeleteRegValue(HKEY_CURRENT_USER, ID_RK_APP, it);
	}
#endif
}

void ReportRegistrationSuccess(HWND hWnd)
{
	const CString msgTitle(kMsgBoxTitle_KeyAccepted);
	CString msgMsg(kMsgBoxText_KeyAccepted);
	WtMessageBox(hWnd, msgMsg, msgTitle, MB_OK);

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && Psettings && !Psettings->mCheckForLatestVersion)
	{
		// [case: 73456]
		msgMsg = _T("Visual Assist will notify you of software updates included in your maintenance, "
		            "and warn you if you must renew before installing one. The Microsoft "
		            "extension manager will notify you of all updates, unaware of maintenance.\r\n\r\n"
		            "Do you want Visual Assist to notify you of updates? "
		            "(If yes, you should ignore notifications from Microsoft.)");
		if (IDYES == WtMessageBox(hWnd, msgMsg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
			Psettings->mCheckForLatestVersion = true;
	}

	// update license info for query status
	gVaLicensingHost->UpdateLicenseStatus();

	void UnhideCheckboxForRenewableSanctuaryLicense();
	UnhideCheckboxForRenewableSanctuaryLicense();

	ResetArmadilloInfo();
}

void CRegisterSanctuary::OnRegister()
{
	CWaitCursor curs;
	LogVersionInfo::ForceNextUpdate();
	UpdateData(TRUE);

	CString msgMsg, msgTitle(IDS_APPNAME " Registration Failure");

	auto sanct = gAuxDll->GetSanctuaryClient();
	if (!sanct)
	{
		curs.Restore();
		::WtMessageBox(m_hWnd,
		               "A critical error occurred during product registration.\n\n"
		               "Please report error SR-1 to\n"
		               "http://www.wholetomato.com/contact",
		               msgTitle, MB_OK | MB_ICONERROR);
		return;
	}

	bool res =
	    sanct->registerSerial(CStringW(GetSafeSerialNumber()), CStringW(mInputNameOrEmail), CStringW(mInputPassword));
	static int retryCount = 0;
	Sleep(retryCount++ * 500u);
	curs.Restore();

	if (!res)
	{
		msgMsg = "Product registration failed (SR-2). The error text is:\n";
		CString sanctErrmsg(sanct->getLastErrorMsg());
		if (sanctErrmsg == "User Account has been deactivated. Registration failed.")
		{
			sanctErrmsg = "The user account for the licensing portal has not been verified or has "
			              "been deactivated. Registration failed.\n\n"
			              "If you recently created the account and have not verified it, please check your "
			              "inbox or spambox for a message from the licensing portal with an email verification link.  "
			              "Click the link to verify the account and then retry Visual Assist registration.\n\n"
			              "If the verification email can not be located, please send a verification request to the "
			              "licensing support team at licenses@wholetomato.com.";
		}
		else
		{
			CString sanctErrmsgLower(sanctErrmsg);
			sanctErrmsgLower.MakeLower();
			if (-1 != sanctErrmsgLower.Find("connect") || -1 != sanctErrmsgLower.Find("ssl") ||
			    -1 != sanctErrmsgLower.Find("certificate"))
				sanctErrmsg += "\n\nIf you continue to encounter errors, you can use your browser to download a "
				               "license file.  Click the 'Use web registration' link in the registration dialog.";
			if (-1 != sanctErrmsg.Find("contact Support."))
				sanctErrmsg.Replace("contact Support.", "contact licenses@wholetomato.com.");
			if (-1 != sanctErrmsg.Find("contact support."))
				sanctErrmsg.Replace("contact support.", "contact licenses@wholetomato.com.");
			if (-1 != sanctErrmsg.Find("telephone"))
				sanctErrmsg.Replace("telephone", "web");
		}
		msgMsg += sanctErrmsg;
		::WtMessageBox(m_hWnd, msgMsg, msgTitle, MB_OK | MB_ICONERROR);
		return;
	}

	sanct->reload();
	CString errorMsg1(sanct->getLastErrorMsg());
	res = sanct->checkStatus();
	CString errorMsg2(sanct->getLastErrorMsg());
	if (!res)
	{
		if (-1 != errorMsg1.Find("contact Support."))
			errorMsg1.Replace("contact Support.", "contact licenses@wholetomato.com.");
		if (-1 != errorMsg1.Find("contact support."))
			errorMsg1.Replace("contact support.", "contact licenses@wholetomato.com.");

		if (-1 != errorMsg2.Find("contact Support."))
			errorMsg2.Replace("contact Support.", "contact licenses@wholetomato.com.");
		if (-1 != errorMsg2.Find("contact support."))
			errorMsg2.Replace("contact support.", "contact licenses@wholetomato.com.");

		if (-1 != errorMsg1.Find("telephone"))
			errorMsg1.Replace("telephone", "web");
		if (-1 != errorMsg2.Find("telephone"))
			errorMsg2.Replace("telephone", "web");

		msgMsg = "Product registration failed (SR-3). ";
		if (!errorMsg1.IsEmpty() || !errorMsg2.IsEmpty())
		{
			msgMsg += "The error text is:\n";
			if (!errorMsg1.IsEmpty())
			{
				msgMsg += errorMsg1;
				if (!errorMsg2.IsEmpty())
					msgMsg += "\n";
			}

			if (!errorMsg2.IsEmpty())
				msgMsg += errorMsg2;

			msgMsg += "\n\n";
		}

		msgMsg += "Please report the error to licensing support at licenses@wholetomato.com.";
		::WtMessageBox(m_hWnd, msgMsg, msgTitle, MB_OK | MB_ICONERROR);
		return;
	}

	if (!sanct->IsLicenseValidForThisBuild(true, m_hWnd))
		return;

	// success, valid license good for this build
	::ReportRegistrationSuccess(m_hWnd);

	OnOK();
}

void CRegisterSanctuary::OnLinkClick(UINT id)
{
	CStringW url;

	switch (id)
	{
	case IDC_LINK_GET_TRIAL:
		url = L"https://www.wholetomato.com/support/tooltip.asp?option=GetTrialSerial";
		break;

	case IDC_LINK_CREATE_ACCOUNT:
		url = L"https://www.wholetomato.com/account";
		break;

	case IDC_LINK_RESET_PASSWORD:
		url = L"https://www.wholetomato.com/support/tooltip.asp?option=ResetEdnPassword";
		break;

	case IDC_LINK_HELP_REGISTRATION:
		url = L"https://www.wholetomato.com/support/tooltip.asp?option=register";
		break;

	case IDC_LINK_SUPPORT:
		url = L"mailto:licenses@wholetomato.com?subject=I%20need%20assistance%20with%20registration";
		break;

	case IDC_LINK_PRIVACY_POLICY:
		url = L"https://www.wholetomato.com/support/tooltip.asp?option=PrivacyPolicy";
		break;
	}

	switch (id)
	{
	case IDC_LINK_GET_TRIAL:
		CString__AppendFormatW(url, L"&rc=%d", mRegistrationCode);
		break;
	case IDC_LINK_SUPPORT:
		if (CWnd* tmpWnd = GetDlgItem(IDC_EDIT_SERIAL_NUMBER))
		{
			// [case: 137753]
			tmpWnd->GetWindowText(mInputSerialNumber);
			CString__AppendFormatW(url, L"&body=%%0D%%0A%%0D%%0Aregistration%%20code:%%20%d%%0D%%0A",
			                       mRegistrationCode);
			const CStringW safeSerial(GetSafeSerialNumber());
			if (!safeSerial.IsEmpty())
				CString__AppendFormatW(url, L"serial%%20number:%%20%s%%0D%%0A", (LPCWSTR)safeSerial);
			if (!mLicenseFileAttemptedToLoad.IsEmpty())
				CString__AppendFormatW(url, L"license%%20file:%%20%s%%0D%%0A", (LPCWSTR)mLicenseFileAttemptedToLoad);
		}
		break;
	}

	::ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOW);

	switch (id)
	{
	case IDC_LINK_RESET_PASSWORD:
		::WtMessageBox(m_hWnd,
		               // [case: 137596]
		               "The link to reset the password to the license portal has been sent to your browser.\n\n"
		               "Whole Tomato Software is an Idera, Inc. company.",
		               IDS_APPNAME, MB_OK);
		break;

	case IDC_LINK_CREATE_ACCOUNT:
		::WtMessageBox(
		    m_hWnd,
		    // [case: 137596]
		    "You need an account on the license portal if you have a serial number that did not accompany a license "
		    "file (reg*.txt or .slip). "
		    "You do not need an account if you have a license file or a legacy, two-line activation key.\n\n"
		    "The link to create an account has been sent to your browser.\n\n"
		    "After you create an account, wait for a verification email from IDERA-Licensing, licensing@idera.com. "
		    "Click the verification link within the email. "
		    "If you attempt to register your serial number before verification, registration will fail.\n\n"
		    "Whole Tomato Software is an Idera, Inc. company.",
		    IDS_APPNAME, MB_OK);
		break;
	}
}

void CRegisterSanctuary::OnWebActivation()
{
	// [case: 134172]
	CWnd* tmpWnd = GetDlgItem(IDC_EDIT_SERIAL_NUMBER);
	tmpWnd->GetWindowText(mInputSerialNumber);

	const CString safeSerial(GetSafeSerialNumber());
	if (safeSerial.IsEmpty())
	{
		// [case: 137728] [case: 137753]
		::WtMessageBox(m_hWnd,
		               "Use the 'Use web registration' link only if you have a serial number that was not extracted "
		               "from a license file (reg*.txt or .slip), and you entered that number into the dialog.",
		               IDS_APPNAME, MB_OK | MB_ICONSTOP);
		return;
	}

	CWnd* parent = GetParent();
	OnCancel();

	CWebRegistrationPrompt dlg(parent, safeSerial, mRegistrationCode);
	dlg.DoModal();
}

void CRegisterSanctuary::OnLegacyRegister()
{
	CRegisterArmadillo dlg(this);
	if (IDCANCEL == dlg.DoModal())
		OnCancel();
	else
		OnOK();
}

void CRegisterSanctuary::OnImportLicense()
{
	mLicenseFileAttemptedToLoad.Empty();
	CImportLicenseFileDlg dlg(this);
	if (IDCANCEL == dlg.DoModal())
	{
		// [case: 137753]
		// don't cancel on error, so that get assistance link knows filename
		mLicenseFileAttemptedToLoad = dlg.GetLicenseFileAttemptedToLoad();
	}
	else
		OnOK();
}

#endif
