// BuyTryDlg.cpp : implementation file
//

#include "stdafx.h"
#include <afxpriv.h>
#include "../WTString.h"
#include "..\RegKeys.h"
#include "BuyTryDlg.h"
#include "BuyTryDlg_Strings.h"
#include "log.h"
#include "../settings.h"
#if defined(SANCTUARY)
#include "../Sanctuary/RegisterSanctuary.h"
#else
#include "Register.h"
#endif
#include "../resource.h"
#include "../wt_stdlib.h"
#include "../Registry.h"
#include "../DevShellAttributes.h"
#include "../PROJECT.H"
#include "../LogVersionInfo.h"
#include "../DevShellService.h"
#include "VaAddinClient.h"
#include "IVaLicensing.h"
#include "../WTKeyGen/WTValidate.h"
#include "TrialElqDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define DELETE_EXCEPTION(e)                                                                                            \
	do                                                                                                                 \
	{                                                                                                                  \
		e->Delete();                                                                                                   \
	} while (0)
#define IDT_SETUPTRYBUTTON 100

/////////////////////////////////////////////////////////////////////////////
// BuyTryDlg dialog

class BuyTryDlg : public CDialog
{
	// Construction
  public:
	BuyTryDlg(CWnd* pParent, bool allowTry, int licenseStaus, bool nonPerpetualLicense, bool showElqTrial = false);
	~BuyTryDlg() = default;

	// Dialog Data
	//{{AFX_DATA(BuyTryDlg)
	//}}AFX_DATA
	enum
	{
		BT_CANCEL = 10,
		BT_LICENSE,
		BT_TRY
	};
	enum
	{
		IDD = IDD_LICENSEDLG
	};

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(BuyTryDlg)
	virtual INT_PTR DoModal(); // to change the return values
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  private:
	int mStatus;
	bool mAllowTry;
	bool mCreationError = false;
	bool mTimerExpired;
	bool mValidLicenseExpired;
	bool mTrialDialogTitle = true;
	bool mNonPerpetualLicense;
	bool mPainted = false;
	CString mWelcomeMsg;
	bool mShowElqTrial = false;

	// Generated message map functions
	//{{AFX_MSG(BuyTryDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnEnterLicense();
	afx_msg void OnBuyOrRenew();
	afx_msg void OnQuit();
	afx_msg void OnTry();
	afx_msg void OnOK();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnRevertBuild();
	afx_msg void OnPaint();
	//}}AFX_MSG

	int DoModal2();
	void InitTrial();
	void SetRevert();
	int SetupTryButton();

	DECLARE_MESSAGE_MAP()
};

BuyTryDlg::BuyTryDlg(CWnd* pParent, bool allowTry, int licenseStatus, bool nonPerpetualLicense, bool showElqTrial)
    : CDialog(IDD, pParent), mStatus(BT_CANCEL), mAllowTry(allowTry && !nonPerpetualLicense),
      mValidLicenseExpired(licenseStatus == LicenseStatus_Expired), mNonPerpetualLicense(nonPerpetualLicense), mShowElqTrial(showElqTrial)
{
	if (LicenseStatus_SanctuaryElcError == licenseStatus || LicenseStatus_SanctuaryError == licenseStatus)
	{
		mAllowTry = false;
		mTrialDialogTitle = false;

		const CString errMsg(gVaLicensingHost->GetErrorMessage());
		_ASSERTE(!errMsg.IsEmpty()); // licenseStatus not possible if no error message
		CString__FormatA(mWelcomeMsg, kBuyTry_SanctuaryError,
		                 LicenseStatus_SanctuaryError == licenseStatus ? "The license server"
		                                                               : "Your hosted license server",
		                 (LPCSTR)errMsg);
	}
	else if (LicenseStatus_NoFloatingSeats == licenseStatus)
	{
		mAllowTry = false;
		mTrialDialogTitle = false;
		mWelcomeMsg = kBuyTry_NoFloatingSeatsAvailable;
	}
	else
	{
		if (!mValidLicenseExpired && gVaLicensingHost->GetLicenseUserCount() > 0)
			mValidLicenseExpired = true;

		if (mValidLicenseExpired)
		{
			if (mNonPerpetualLicense)
				mWelcomeMsg = kBuyTry_NonPerpetualLicenseExpired;
			else if (gVaLicensingHost && gVaLicensingHost->IsNonRenewableLicenseInstalled())
				mWelcomeMsg = kBuyTry_TrialOver_NonRenewableLicenseExpired;
			else
				mWelcomeMsg = kBuyTry_TrialOver_RenewableLicenseExpired;
		}
		else
			mWelcomeMsg = kBuyTry_TrialOver;
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(BuyTryDlg, CDialog)
//{{AFX_MSG_MAP(BuyTryDlg)
ON_BN_CLICKED(IDC_LICENSE, OnEnterLicense)
ON_BN_CLICKED(IDC_BUYNOW, OnBuyOrRenew)
ON_BN_CLICKED(IDC_REVERTBUILD, OnRevertBuild)
ON_BN_CLICKED(IDCANCEL, OnQuit)
ON_BN_CLICKED(IDC_TRY, OnTry)
ON_BN_CLICKED(IDOK, OnOK)
ON_WM_TIMER()
ON_WM_PAINT()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// BuyTryDlg message handlers

// ripped dlgcore.cpp - CDialog::DoModal() - needed this because it
// calls PreModal which returns an incorrect parent HWND
int BuyTryDlg::DoModal2()
{
	// can be constructed with a resource template or InitModalIndirect
	ASSERT(m_lpszTemplateName != NULL || m_hDialogTemplate != NULL || m_lpDialogTemplate != NULL);

	// load resource as necessary
	LPCDLGTEMPLATE lpDialogTemplate = m_lpDialogTemplate;
	HGLOBAL hDialogTemplate = m_hDialogTemplate;
	HINSTANCE hInst = AfxGetResourceHandle();
	if (m_lpszTemplateName != NULL)
	{
		hInst = AfxFindResourceHandle(m_lpszTemplateName, RT_DIALOG);
		HRSRC hResource = ::FindResource(hInst, m_lpszTemplateName, RT_DIALOG);
		hDialogTemplate = LoadResource(hInst, hResource);
	}
	if (hDialogTemplate != NULL)
		lpDialogTemplate = (LPCDLGTEMPLATE)LockResource(hDialogTemplate);

	// return -1 in case of failure to load the dialog template resource
	if (lpDialogTemplate == NULL)
		return -1;

	// disable parent (before creating dialog)
	PreModal(); // do not use parent HWND returned by PreModal()
	AfxUnhookWindowCreate();

	TRY
	{
		// create modeless dialog
		AfxHookWindowCreate(this);
		if (CreateDlgIndirect(lpDialogTemplate, CWnd::FromHandle(NULL), hInst))
		{
			if (m_nFlags & WF_CONTINUEMODAL)
			{
				// enter modal loop
				DWORD dwFlags = MLF_SHOWONIDLE;
				if (GetStyle() & DS_NOIDLEMSG)
					dwFlags |= MLF_NOIDLEMSG;
				VERIFY(RunModalLoop(dwFlags) == m_nModalResult);
			}

			// hide the window before enabling the parent, etc.
			if (m_hWnd != NULL)
				SetWindowPos(NULL, 0, 0, 0, 0,
				             SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
		}
	}
	CATCH_ALL(e)
	{
		DELETE_EXCEPTION(e);
		m_nModalResult = -1;
	}
	END_CATCH_ALL

	// destroy modal window
	DestroyWindow();
	PostModal();

	// unlock/free resources as necessary
	if (m_lpszTemplateName != NULL || m_hDialogTemplate != NULL)
		UnlockResource(hDialogTemplate);
	if (m_lpszTemplateName != NULL)
		FreeResource(hDialogTemplate);

	return m_nModalResult;
}

INT_PTR BuyTryDlg::DoModal()
{
	const INT_PTR ret = CDialog::DoModal();
	if (-1 == ret || IDABORT == ret)
	{
		mStatus = BT_CANCEL;
		if (!mCreationError)
		{
			DWORD err = ::GetLastError();
			vLog("ERROR running dialog %ld(%08lx)", err, err);
			WtMessageBox("Error running dialog.\n" IDS_APPNAME
			             " will be disabled.\nPlease contact us at http://www.wholetomato.com/contact",
			             IDS_APPNAME, MB_OK | MB_ICONSTOP);
		}
	}
	return mStatus;
}

BOOL BuyTryDlg::OnInitDialog()
{
	if (!m_hWnd)
	{
		DWORD err = ::GetLastError();
		vLog("ERROR creating dialog %ld(%08lx)", err, err);
		WtMessageBox("Error creating dialog.\n" IDS_APPNAME
		             " will be disabled.\nPlease contact us at http://www.wholetomato.com/contact",
		             IDS_APPNAME, MB_OK | MB_ICONSTOP);
		mCreationError = true;
		return FALSE;
	}

#if defined(VAX_CODEGRAPH)
	SetWindowText(IDS_APPNAME " Trial");
#endif

	if (!mTrialDialogTitle)
		SetWindowText(IDS_APPNAME);

	SendMessageToDescendants(WM_INITIALUPDATE, 0, 0, FALSE, FALSE);
	CButton* tmpButton = (CButton*)GetDlgItem(IDC_BUYNOW);
	ASSERT(tmpButton != NULL);
	tmpButton->SetFocus();
	if (mValidLicenseExpired && !mNonPerpetualLicense && gVaLicensingHost &&
	    !gVaLicensingHost->IsNonRenewableLicenseInstalled())
		tmpButton->SetWindowText("Renew Now");

	InitTrial();
	SetRevert();

	// set static text instructions
	CStatic* tmpStatic = (CStatic*)GetDlgItem(IDC_INSTRUCTIONS);
	ASSERT(tmpStatic != NULL);
	tmpStatic->SetWindowText(mWelcomeMsg);
	return FALSE;
}

void BuyTryDlg::OnTry()
{
	if (mAllowTry)
	{
		if (mShowElqTrial)
		{
			if (DisplayTrialElqDlg(this))
			{
				// mark in registry that ELQ trial dialog has been shown and filled
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, ID_RK_ELQ_TRIAL, 1);
			}	
			else
			{
				// user closed ELQ trial dialog without entering user data
				mStatus = BT_CANCEL;
				return;
			}
		}

		mStatus = BT_TRY;
		CDialog::OnOK();
	}
	else
		mStatus = BT_CANCEL;
}

void BuyTryDlg::OnEnterLicense()
{
	if (!mTimerExpired)
		return;

	mStatus = BT_LICENSE;

#if defined(SANCTUARY)
	CRegisterSanctuary dlg(this);
#else
	CRegisterArmadillo dlg(this);
#endif
	if (IDOK == dlg.DoModal())
		OnOK();
	else
		mStatus = BT_CANCEL;
}

void BuyTryDlg::OnBuyOrRenew()
{
	CString msg(kMsgBoxText_BuyNow), caption(kMsgBoxTitle_BuyNow);
	if (mValidLicenseExpired && !mNonPerpetualLicense)
	{
		if (gVaLicensingHost && !gVaLicensingHost->IsNonRenewableLicenseInstalled())
		{
			msg = kMsgBoxText_RenewNow;
			caption = kMsgBoxTitle_BuyNow;
		}
	}

	// display message box re: net connection
	if (IDOK == WtMessageBox(m_hWnd, msg, caption, MB_OKCANCEL | MB_ICONQUESTION))
	{
		CString url;
		if (mValidLicenseExpired && !mNonPerpetualLicense && gVaLicensingHost)
		{
			if (gVaLicensingHost->IsNonRenewableLicenseInstalled())
				url = "https://www.wholetomato.com/purchase/default.asp";
			else
				url = "https://www.wholetomato.com/purchase/maintenance.asp";
		}
		else
			url = "https://www.wholetomato.com/purchase/default.asp";

		::ShellExecute(::GetDesktopWindow(), _T("open"), url, NULL, NULL, SW_SHOWNORMAL);
	}
}

void BuyTryDlg::OnPaint()
{
	CDialog::OnPaint();
	mPainted = true;
}

void BuyTryDlg::OnQuit()
{
	static int sEatLimit = 0;
	// mPainted is used to ensure that the dlg has been displayed since pressing
	// escape while the IDE is starting can cause this dialog to be closed before
	// it is displayed. case 702.
	if (!mPainted && sEatLimit++ < 4)
		return;
	mStatus = BT_CANCEL;
	OnOK();
}

void BuyTryDlg::OnRevertBuild()
{
	_ASSERTE(gVaLicensingHost);
	const CString expires(gVaLicensingHost->GetLicenseExpirationDate());
	if (expires.IsEmpty())
		return;

	// launch URL
	CString url;
	CString__FormatA(url, "http://www.wholetomato.com/downloads/revert.asp?e=%s&vsix=%c&b=%c", (const char*)expires,
	                 gShellAttr->IsDevenv10OrHigher() ? 'y' : 'n', Psettings->mCheckForLatestBetaVersion ? 'y' : 'n');
	::ShellExecute(NULL, _T("open"), url, NULL, NULL, SW_SHOWNORMAL);
}

void BuyTryDlg::OnOK()
{
	if (!mTimerExpired)
		return;

	CDialog::OnOK();
	::Sleep(899);
}

int BuyTryDlg::SetupTryButton()
{
	if (mAllowTry)
	{
		CButton* tmpButton = (CButton*)GetDlgItem(IDC_TRY);
		if (tmpButton)
		{
			tmpButton->EnableWindow(TRUE);
			tmpButton->SetFocus();
			tmpButton->SetButtonStyle(BS_DEFPUSHBUTTON);
		}
		return 1;
	}
	return 0;
}

void BuyTryDlg::InitTrial()
{
	if (mAllowTry && gVaLicensingHost)
	{
		CString tmp;
		if (mValidLicenseExpired)
		{
			if (gVaLicensingHost->IsNonRenewableLicenseInstalled())
				tmp = kBuyTry_Instructions_NonrenewableLicenseExpired;
			else
				tmp = kBuyTry_Instructions_RenewableLicenseExpired;
		}
		else
			tmp = kBuyTry_Instructions;
		const int days = gVaLicensingHost->GetArmDaysLeft();
		mWelcomeMsg.Format(tmp, days, days == 1 ? "day" : "days");
		bool displayAdditionalInfo = !days;
#ifdef _DEBUG
		displayAdditionalInfo = true;
#endif
		if (displayAdditionalInfo)
			mWelcomeMsg += "\r\n\r\n" + ::GetVaVersionInfo();
		mTimerExpired = false;
		SetTimer(IDT_SETUPTRYBUTTON, 1, NULL); // no delay
	}
	else
		mTimerExpired = true;
}

void BuyTryDlg::SetRevert()
{
	if (mValidLicenseExpired && !mNonPerpetualLicense)
	{
		// Get scale factor.  Although all buttons are evenly spaced 4 dialog units apart in the resource script,
		// the default 96 DPI introduces a scaling artifact that puts an additional pixel between the cancel button
		// and the other buttons.  That artifact is replicated here and also checks out OK under other DPI settings.
		RECT rect = {0, 0, 0, 4};
		MapDialogRect(&rect);
		int gutter = rect.bottom - rect.top - 1;

		// Rearrange buttons and show revert button
		CButton* pButton;
		int left = 0, top = 0, buttonHeight = 0;
		int buttons[] = {IDCANCEL, IDC_REVERTBUILD, IDC_BUYNOW, IDC_LICENSE, IDC_TRY};
		for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
		{
			int additionalGutter = 0;
			pButton = (CButton*)GetDlgItem(buttons[i]);
			if (IDCANCEL == buttons[i])
			{
				// set other buttons relative to the cancel button from the bottom up
				pButton->GetWindowRect(&rect);
				ScreenToClient(&rect);
				left = rect.left;
				top = rect.top;
				buttonHeight = rect.bottom - rect.top;
				additionalGutter = 1; // extra pixel between cancel and other buttons
			}
			else
			{
				pButton->SetWindowPos(NULL, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
				if (IDC_REVERTBUILD == buttons[i])
				{
					pButton->ShowWindow(SW_SHOW);
				}
			}
			top -= (buttonHeight + gutter + additionalGutter);
		}
	}
}

void BuyTryDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (IDT_SETUPTRYBUTTON == nIDEvent)
	{
		KillTimer(nIDEvent);
		mTimerExpired = true;
		SetupTryButton();
		return;
	}
	CDialog::OnTimer(nIDEvent);
}

BOOL VaAddinClient::DoTryBuy(int licenseStatus, bool nonPerpetualLicense)
{
	_ASSERTE(gVaLicensingHost);
	auto vaLicensingHost = gVaLicensingHost;
	if (!vaLicensingHost)
	{
		// [case: 104862]
		// weird shutdown state
		return FALSE;
	}

	const CString userName(gVaLicensingHost->GetLicenseInfoUser(true));

	CWnd* parent = NULL;
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		parent = gMainWnd;
	if (!parent)
		parent = AfxGetMainWnd();
	SYSTEMTIME ctm;
	GetSystemTime(&ctm);
	int days = vaLicensingHost->GetArmDaysLeft();
#ifdef NDEBUG
	if (vaLicensingHost->GetArmClock())
	{
		LicenseTraceBox1(*parent, "days set to 0 due to CLOCKBACK or CLOCKFORWARD (was %d)", days);
		days = 0;
	}
#endif // NDEBUG

	// case 703: installing 1301 over 1418 registered with armadillo keys
	// causes EXPIRED to be set even though DAYSLEFT is positive.
	// In this case and if days > 7, no dlg would appear and VA became dormant.
	bool allowTry = true;
	if (days && vaLicensingHost->IsArmExpired())
	{
		allowTry = false;
		LicenseTraceBox(*parent, "dotry set to false due to EXPIRED");
	}

	// check if we need to show up the eloqua trial dialog
	bool showElqTrial = GetRegDword(HKEY_CURRENT_USER, ID_RK_APP_KEY, ID_RK_ELQ_TRIAL) == 0;

	if (days > 7 && allowTry && !showElqTrial)
	{
		LicenseTraceBox1(*parent, "days > 7 : %d", vaLicensingHost->GetArmDaysLeft());
		return TRUE;
	}

	// this is either a red herring for crackers or old leftover code
	WTString lastday = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "LastTrialDate");

	if (userName == "error")
	{
		WTString msg("The key you entered for " IDS_APPNAME
		             " was not accepted. Please retry.\n\nKeys contains two lines. Copy them verbatim. Do not change "
		             "case, do\nnot adjust spacing and if present, include the \"(N-user license)\" part of your key.");
		CString caption("Warning: Activation Key from Whole Tomato Software");
		caption += "  (" + ::GetVaVersionInfo() + ")";
		WtMessageBox(*parent, msg, caption, MB_OK);
	}

#if defined(LICENSE_TRACING)
	{
		CString tmp;
		CString__FormatA(tmp, "dotry = %d\r\ndays = %d\r\nlicenseStatus = %d", allowTry, days, licenseStatus);
		LicenseTraceBox(*parent, tmp);
	}
#endif

	BuyTryDlg dlg(parent, allowTry && days, licenseStatus, nonPerpetualLicense, showElqTrial);
	const INT_PTR ret = dlg.DoModal();
	switch (ret)
	{
	case BuyTryDlg::BT_CANCEL:
		if (nonPerpetualLicense)
		{
			CString msg;
			CString__FormatA(msg, kMsgBoxText_BuyTryCancel_NonPerpetualKeyExpired, (LPCSTR)userName);
			::WtMessageBox(msg, kMsgBoxTitle_BuyTryCancel_NonPerpetualKeyExpired, MB_OK);
		}
		else
		{
			::WtMessageBox(gShellAttr->IsDevenv10OrHigher() ? kMsgBoxText_BuyTryCancel10 : kMsgBoxText_BuyTryCancel,
			               kMsgBoxTitle_BuyTryCancel, MB_OK);
		}
		return FALSE;
	case BuyTryDlg::BT_TRY:
		_ASSERTE(!nonPerpetualLicense);
#ifdef _DEBUG
		days = 3;
#else
		days = vaLicensingHost->GetArmDaysLeft();
#endif // _DEBUG
		if (days <= 0)
		{
			::WtMessageBox(gShellAttr->IsDevenv10OrHigher() ? kMsgBoxText_BuyTryCancel10 : kMsgBoxText_BuyTryCancel,
			               kMsgBoxTitle_BuyTryCancel, MB_OK);
			LicenseTraceBox(*parent, "days <= 0 post BuyTry");
			return FALSE;
		}
		break;
	case BuyTryDlg::BT_LICENSE:
		break;
	default:
		ASSERT(!"unhandled return status");
	}

	// this is either a red herring for crackers or old leftover code
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "LastTrialDate", itos(ctm.wDay));
	return TRUE;
}
