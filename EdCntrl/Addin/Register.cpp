// Register.cpp : implementation file
//

#include "stdafx.h"
#include "log.h"
#include "Register.h"
#include "Register_Strings.h"
#include "../token.h"
#include "../RegKeys.h"
#include <shlwapi.h>
#include "../Registry.h"
#include "../LogVersionInfo.h"
#include "../DevShellService.h"
#include "../Settings.h"
#include "../DevShellAttributes.h"
#include "IVaLicensing.h"
#include "LicenseMonitor.h"
#include "BuildDate.h"
#include "../WTKeyGen/WTValidate.h"
#ifndef NOSMARTFLOW
#include "SmartFlow/phdl.h"
#endif
#include "FILE.H"
#include "Sanctuary/ISanctuaryClient.h"
#include "Sanctuary/IVaAuxiliaryDll.h"
#include "Directories.h"
#include "PROJECT.H"
#include "Sanctuary/CheckoutDurationDlg.h"
#include "DllNames.h"
#include "Library.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::TRegexp;

static void RemoveMailto(CString& str);
static void StripTrailingWhitespace(CString& str);
void SplitRegistrationInput(const char* inputStr, const char*& userOut, const char*& keyOut);

static OWL::string sUser;
static OWL::string sKey;

class PasteInterceptEdit : public CWnd
{
	WTString mTxt;

  public:
	PasteInterceptEdit(HWND hCtrl) : CWnd()
	{
		SubclassWindow(hCtrl);
		AttemptLicensePaste();
	}

	void ParentActivated()
	{
		CString name;
		GetWindowText(name);
		if (name.IsEmpty())
			AttemptLicensePaste();
	}

  private:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (WM_PASTE == message)
		{
			if (AttemptLicensePaste())
				return TRUE;
		}
		return CWnd::WindowProc(message, wParam, lParam);
	}

	bool AttemptLicensePaste()
	{
		bool retval = false;

		if (OpenClipboard())
		{
			WTString clipData;
			HANDLE hglb = ::GetClipboardData(CF_TEXT);
			if (hglb)
			{
				clipData = (LPCTSTR)::GlobalLock(hglb);
				::GlobalUnlock(hglb);
			}
			CloseClipboard();

			if (clipData == mTxt)
				return retval;

			mTxt = clipData;
			token clipTxt(mTxt);
			clipTxt.ReplaceAll("\t", " ");
			clipTxt.ReplaceAll("\r", "\n");
			clipTxt.ReplaceAll(TRegexp("[\n]+"), OWL::string("\001"));
			clipTxt.ReplaceAll("\001", "\n");
			clipTxt.ReplaceAll(TRegexp("^ +"), OWL::string(""));
			clipTxt.ReplaceAll(TRegexp("^[ \n]+"), OWL::string(""));
			clipTxt.ReplaceAll(TRegexp("[ ]+$"), OWL::string(""));
			clipTxt.ReplaceAll(TRegexp("[ ]+"), OWL::string("\001"));
			clipTxt.ReplaceAll("\001", " ");

			// multi-line edit control needs \r\n
			clipTxt.ReplaceAll("\n", "\001");
			clipTxt.ReplaceAll("\001", "\r\n");

			CString tmpTxt(clipTxt.c_str());
			if (-1 != tmpTxt.Find("\n") &&
			    (-1 != tmpTxt.Find("license") || -1 != tmpTxt.Find("License") || -1 != tmpTxt.Find("Trial")))
			{
				SetWindowText(tmpTxt);
				retval = true;
			}
		}

		return retval;
	}
};

/////////////////////////////////////////////////////////////////////////////
// CRegisterArmadillo dialog

CRegisterArmadillo::CRegisterArmadillo(CWnd* parent) : CDialog(CRegisterArmadillo::IDD, parent), mUserInputCtrl(NULL)
{
	//{{AFX_DATA_INIT(CRegisterArmadillo)
	//}}AFX_DATA_INIT
	_ASSERTE(gVaLicensingHost);
}

CRegisterArmadillo::~CRegisterArmadillo()
{
	delete mUserInputCtrl;
}

void CRegisterArmadillo::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRegisterArmadillo)
	DDX_Text(pDX, IDC_KEY, mRegInfo);
	DDV_MaxChars(pDX, mRegInfo, 512);
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CRegisterArmadillo, CDialog)
//{{AFX_MSG_MAP(CRegisterArmadillo)
ON_WM_ACTIVATE()
ON_BN_CLICKED(IDOK, OnRegister)
ON_BN_CLICKED(IDCANCEL, OnCancel)
ON_WM_HELPINFO()
ON_WM_SYSCOMMAND()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CRegisterArmadillo message handlers

BOOL CRegisterArmadillo::OnInitDialog()
{
	const BOOL retval = CDialog::OnInitDialog();

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	mUserInputCtrl = new PasteInterceptEdit(GetDlgItem(IDC_KEY)->m_hWnd);
	CString caption("Enter Key");
	caption += "  (" + ::GetVaVersionInfo() + ")";
	SetWindowText(caption);
	return retval;
}

HINSTANCE
CRegisterArmadillo::LaunchHelp()
{
	HINSTANCE hInst = ShellExecute(NULL, _T("open"), "http://www.wholetomato.com/support/tooltip.asp?option=enterKey",
	                               NULL, NULL, SW_SHOW);
	return hInst;
}

void CRegisterArmadillo::OnSysCommand(UINT msg, LPARAM lParam)
{
	if (msg == SC_CONTEXTHELP)
		LaunchHelp();
	else
		__super::OnSysCommand(msg, lParam);
}

BOOL CRegisterArmadillo::OnHelpInfo(HELPINFO* info)
{
	LaunchHelp();
	return TRUE;
}

void CRegisterArmadillo::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CDialog::OnActivate(nState, pWndOther, bMinimized);
	if (WA_INACTIVE != nState && mUserInputCtrl)
		mUserInputCtrl->ParentActivated();
}

CString CheckKeyForMissingDash(CString key)
{
	if (key.GetLength() != 75)
		return key;

	CString adjustedKey;
	for (int idx = 0; idx < key.GetLength(); ++idx)
	{
		if (idx && idx < 75 && 6 == (idx % 7) && key[idx] != '-')
		{
			adjustedKey += '-';
			adjustedKey += key.Mid(idx);
			break;
		}

		adjustedKey += key[idx];
	}

	return adjustedKey;
}

void SplitRegistrationInput(const char* inputStr, const char*& userOut, const char*& keyOut)
{
	CString input(inputStr);
	input.Replace("-----------------------Do not include this line-----------------------", "");
	CString user, key;
	token clipTxt(input);
	// need to use [0-9]+ for year since owlMatch asserts on RE length
	const OWL::string kExpDateRegEx("[0-9]+[.][0-9][0-9][.][0-9][0-9]");

	// Trial and Fix Clock are the only cases when we don't expect to see exp date
	bool expectLicenseExpiry = -1 == input.Find("Trial") && -1 == input.Find("Fix Clock");

	clipTxt.ReplaceAll("\t", " ");
	clipTxt.ReplaceAll("\r", "\n");
	clipTxt.ReplaceAll(TRegexp("[\n]+"), OWL::string("\001"));
	clipTxt.ReplaceAll("\001", "\n");
	clipTxt.ReplaceAll(TRegexp("^ +"), OWL::string(""));
	clipTxt.ReplaceAll(TRegexp("^[ \n]+"), OWL::string(""));
	clipTxt.ReplaceAll(TRegexp("[ ]+$"), OWL::string(""));
	clipTxt.ReplaceAll(TRegexp("[ ]+"), OWL::string("\001"));
	clipTxt.ReplaceAll("\001", " ");

	if (expectLicenseExpiry)
	{
		// check for improper line termination after username
		OWL::string tmp("s ");
		tmp += kExpDateRegEx;
		tmp += "$";
		TRegexp dateRe(tmp.c_str());
		OWL::string foundSubstr(clipTxt.SubStr(dateRe));
		if (!foundSubstr.length())
		{
			size_t pos;

			tmp = "s ";
			tmp += kExpDateRegEx;
			dateRe = tmp.c_str();
			pos = 0;
			foundSubstr = clipTxt.SubStr(dateRe, &pos);
			if (!foundSubstr.length())
			{
				tmp = "^";
				tmp += kExpDateRegEx;
				dateRe = tmp.c_str();
				pos = 0;
				foundSubstr = clipTxt.SubStr(dateRe, &pos);
			}

			const size_t kLenFoundSubstr = foundSubstr.length();
			if (kLenFoundSubstr)
			{
				// found exp date without linebreak, insert it
				tmp = clipTxt.c_str();
				OWL::string newCliptxt = tmp.substr(0, pos + kLenFoundSubstr);
				newCliptxt += "\n";
				newCliptxt += tmp.substr(pos + kLenFoundSubstr, tmp.length() - (pos + kLenFoundSubstr));
				clipTxt = newCliptxt;

				clipTxt.ReplaceAll(TRegexp("^ +"), OWL::string(""));
				clipTxt.ReplaceAll(TRegexp("^[ \n]+"), OWL::string(""));
				clipTxt.ReplaceAll(TRegexp("[ ]+$"), OWL::string(""));
			}
			else
			{
				// hack for AutoRegPrevLicense - for old licenses that don't
				// have visible expirations - treat '$' as name/key delimiter
				pos = 0;
				dateRe = "[$]";
				foundSubstr = clipTxt.SubStr(dateRe, &pos);
				if (foundSubstr.length())
				{
					tmp = clipTxt.c_str();
					OWL::string newCliptxt = tmp.substr(0, pos);
					newCliptxt += "\n";
					newCliptxt += tmp.substr(pos + 1, tmp.length() - (pos + 1));
					clipTxt = newCliptxt;
					expectLicenseExpiry = false;
				}
			}
		}
	}

	input = clipTxt.c_str();
	if (-1 != input.Find("\n"))
	{
		user = clipTxt.read("\n").c_str();
		if (expectLicenseExpiry)
		{
			// see if exp date is in user
			token tmpTok(user);
			OWL::string tmp("s ");
			tmp += kExpDateRegEx;
			tmp += "$";
			TRegexp dateRe(tmp.c_str());
			OWL::string foundSubstr(tmpTok.SubStr(dateRe));
			if (!foundSubstr.length())
			{
				foundSubstr = clipTxt.SubStr(dateRe);
				if (!foundSubstr.length())
				{
					tmp = "^";
					tmp += kExpDateRegEx;
					dateRe = tmp.c_str();
					foundSubstr = clipTxt.SubStr(dateRe);
				}

				if (foundSubstr.length())
				{
					// found exp date in next line - append to user
					user += CString((WTString(" ") + clipTxt.read("\n")).c_str());
				}
			}
		}

		// join rest of lines
		input = clipTxt.c_str();
		if (-1 != input.Find("-"))
		{
			// keys uses dashes, so just remove linebreaks
			clipTxt.ReplaceAll("\n", "");
			// remove embedded spaces
			clipTxt.ReplaceAll(" ", "");
		}
		else
		{
			// add spaces to separate groups that were on different lines
			clipTxt.ReplaceAll("\n", " ");
			clipTxt.ReplaceAll(TRegexp("^ +"), OWL::string(""));
		}
		key = clipTxt.read("\n").c_str();
	}

	if (user.GetLength())
		RemoveMailto(user);
	if (user.GetLength())
		StripTrailingWhitespace(user);
	if (key.GetLength())
		StripTrailingWhitespace(key);

#pragma warning(disable : 4309)
	// check for soft-hyphen (editor won't display it, but edit control does)
	if (-1 != key.Find(0xad))
		key.Replace(0xad, '-');
#pragma warning(default : 4309)

	if (key.GetLength() == 75)
		key = CheckKeyForMissingDash(key);

	if (user.GetLength() && key.GetLength())
	{
		sUser = (LPCSTR)user;
		userOut = sUser.c_str();

		sKey = (LPCSTR)key;
		keyOut = sKey.c_str();
	}
	else
		userOut = keyOut = NULL;
}

void HideCheckboxForRenewableLicences()
{
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "HiddenRecommendUpdate", "");
}

void UnhideCheckboxForRenewableSanctuaryLicense()
{
#if !defined(VA_CPPUNIT)
	// Never show the dialog for corporate licenses
	if (!gVaLicensingHost->IsNonRenewableLicenseInstalled())
		return;

	// Retrieve the current license expiration date
	LPCSTR currentExpDate = gVaLicensingHost->GetLicenseExpirationDate();

	// Retrieve the saved expiration date from the registry
	CString savedExpDate = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "SavedSancLicenseExpiryDate");

	// Compare the current and saved expiration dates
	if (savedExpDate != CString(currentExpDate))
	{
		// If different, update the relevant registry entries to (re)enable the renew license notifier system
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "HiddenRecommendUpdate", "No");
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "SavedSancLicenseExpiryDate", CString(currentExpDate));
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder", "");
	}
#endif
}

bool UnhideCheckboxForRenewableArmadilloLicences(const LPCSTR userIn)
{
#if !defined(VA_CPPUNIT)
	CString user(userIn);
	// #nonRenewableLiteral
	if (user == "" || user.Find("Trial License") != -1 || user.Find("Trial Extension") != -1 || user.Find("Non-renewable") != -1 || user.Find("classroom license") != -1)
	{
		HideCheckboxForRenewableLicences();
		return false;
	}

	if (user.Find("@") == -1) // we don't recommend renewals for licenses without email as per case 79781
	{
		HideCheckboxForRenewableLicences();
		return false;
	}

	_ASSERTE(gVaLicensingHost);
	if (!gVaLicensingHost || gVaLicensingHost->GetLicenseUserCount() == -1 || gVaLicensingHost->GetLicenseUserCount() > 1) // don't recommend for potentially corporate users
	{
		HideCheckboxForRenewableLicences();
		return false;
	}

	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "HiddenRecommendUpdate", "No");
	return true;
#else
	return false;
#endif
}

void CRegisterArmadillo::RandomizeRecommendRenewalCheckbox()
{
	int userCount = gVaLicensingHost->GetLicenseUserCount();
	Psettings->mRecommendAfterExpires = (userCount <= 10) || (userCount < 100 && (rand() % userCount) < 10);
}

void CRegisterArmadillo::OnRegister()
{
	CWaitCursor curs;
	LogVersionInfo::ForceNextUpdate();
	UpdateData(TRUE);
	// slowdown automated dictionary attacks
	static int sRegCount = 1;
	Sleep(786u * sRegCount++);

	const char* userOut = NULL;
	const char* keyOut = NULL;
	mRegInfo.Replace("\t", "\r\n");
	::SplitRegistrationInput(mRegInfo, userOut, keyOut);
	const CString user(userOut);
	const CString key(keyOut);

	CString msgMsg, msgTitle;
	IVaLicensing::RegisterResult res = IVaLicensing::ErrInvalidKey;
	if (key.GetLength() && user.GetLength())
	{
		res = gVaLicensingHost->RegisterArmadillo(m_hWnd, user, key, SplitRegistrationInput);
		switch (res)
		{
		case IVaLicensing::ErrShortInvalidKey:
			msgTitle = kMsgBoxTitle_ShortInvalidKey;
			msgMsg = kMsgBoxText_ShortInvalidKey;
			break;
		case IVaLicensing::ErrNonPerpetualLicenseExpired:
			msgTitle = kMsgBoxTitle_NonPerpetualKeyExpired;
			msgMsg = kMsgBoxText_NonPerpetualKeyExpired;
			break;
		case IVaLicensing::ErrRenewableLicenseExpired:
		case IVaLicensing::ErrNonRenewableLicenseExpired:
			msgTitle = kMsgBoxTitle_KeyExpired;
			if (IVaLicensing::ErrRenewableLicenseExpired == res)
				msgMsg = kMsgBoxText_RenewableKeyExpired;
			else
				msgMsg = kMsgBoxText_NonrenewableKeyExpired;
			HideCheckboxForRenewableLicences();
			break;
		case IVaLicensing::ErrIneligibleRenewal:
			// renewal key but failed to validate previous
			msgTitle = kMsgBoxTitle_IneligibleRenewal;
			msgMsg = kMsgBoxText_IneligibleRenewal;
			break;
		case IVaLicensing::ErrInvalidKey:
			// failed to install
			msgTitle = kMsgBoxTitle_InvalidKey;
			msgMsg = kMsgBoxText_InvalidKey;
			break;
		case IVaLicensing::OkTrial:
			msgTitle = kMsgBoxTitle_InvalidKey;
			msgMsg = kMsgBoxText_InvalidKey;
			break;
		case IVaLicensing::OkLicenseAccepted:
			msgTitle = kMsgBoxTitle_KeyAccepted;
			msgMsg = kMsgBoxText_KeyAccepted;
			if (UnhideCheckboxForRenewableArmadilloLicences(user))
				RandomizeRecommendRenewalCheckbox();
			break;
		default:
			_ASSERTE(!"unhandled register status");
			break;
		}
	}
	else if (user.IsEmpty() && key.IsEmpty() && mRegInfo.GetLength() > 60 && mRegInfo.GetLength() < 80 &&
	         (-1 == mRegInfo.Find(kExpirationLabel_Perpetual) || -1 == mRegInfo.Find(kExpirationLabel_NonPerpetual)))
	{
		// [case: 86743]
		msgTitle = kMsgBoxTitle_MissingUser;
		msgMsg = kMsgBoxText_MissingUser;
	}
	else
	{
		msgTitle = kMsgBoxTitle_InvalidKey;
		msgMsg = kMsgBoxText_InvalidKey;
	}

	curs.Restore();
	if (IVaLicensing::OkTrial != res)
	{
		WtMessageBox(m_hWnd, msgMsg, msgTitle, MB_OK);
		if (IVaLicensing::OkLicenseAccepted != res)
			return;

		if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && Psettings && !Psettings->mCheckForLatestVersion)
		{
			// [case: 73456]
			msgMsg = _T("Visual Assist will notify you of software updates included in your maintenance, "
			            "and warn you if you must renew before installing one. The Microsoft "
			            "extension manager will notify you of all updates, unaware of maintenance.\r\n\r\n"
			            "Do you want Visual Assist to notify you of updates? "
			            "(If yes, you should ignore notifications from Microsoft.)");
			if (IDYES == WtMessageBox(m_hWnd, msgMsg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
				Psettings->mCheckForLatestVersion = true;
		}
	}

	OnOK();

	// update license info for query status
	gVaLicensingHost->UpdateLicenseStatus();
}

void RemoveMailto(CString& str)
{
	// remove " <mailto:...>"
	int pos1, pos2;
	pos1 = str.Find(" <mailto:");
	if (pos1 == -1)
	{
		pos1 = str.Find(" [mailto:");
		if (pos1 == -1)
		{
			pos1 = str.Find("<mailto:");
			if (pos1 == -1)
			{
				pos1 = str.Find("[mailto:");
				if (pos1 == -1)
					return;
			}
		}
	}

	pos2 = str.Find('>');
	if (pos2 == -1)
	{
		pos2 = str.Find(']');
		if (pos2 == -1)
			return;
	}

	if (pos2 <= pos1)
		return;

	const int len = str.GetLength();
	char* newStr = _tcsdup(str);
	pos2++;
	int idx;
	for (idx = pos1; pos2 < len;)
		newStr[idx++] = str[pos2++];
	newStr[idx] = '\0';

	str = newStr;
	free(newStr);
}

void StripTrailingWhitespace(CString& str)
{
	token tok(str);
	tok.ReplaceAll(TRegexp("[ \t\r\n]+$"), OWL::string(""));
	str = tok.c_str();
}

// from here to the end of the file could be moved to a different dll
// (aside from the definition of gVaLicensingHost which would move to Project.cpp)

#if defined(SANCTUARY)
Library gAuxLib;
IVaAuxiliaryDll* gAuxDll = nullptr;
#endif

#include "IVaLicensing.h"

class VaLicensingHost : public IVaLicensing, public IVaAccessFromAux
{
  public:
	VaLicensingHost() = default;
	virtual ~VaLicensingHost() = default;

	virtual int GetArmDaysLeft() override;
	virtual int GetArmDaysInstalled() override;
	virtual LPCSTR GetArmString(const LPCSTR str) override;
	virtual int IsArmExpired() override;
	virtual int GetArmClock() override;
	virtual BOOL InitLicense(IVaLicensingUi* vaLicUi) override;
	virtual int GetLicenseUserCount() override;
	virtual int GetLicenseStatus() override;
	virtual LPCSTR GetErrorMessage() override;
	virtual LPCSTR GetLicenseExpirationDate() override;
	virtual BOOL IsNonRenewableLicenseInstalled() override;
	virtual BOOL IsNonPerpetualLicenseInstalled() override;
	virtual BOOL IsAcademicLicenseInstalled() override;
	virtual BOOL IsVaxNetworkLicenseCountSupported() override;
	virtual BOOL IsVaxNetworkLicenseCountOk() override;
	virtual RegisterResult RegisterArmadillo(HWND hWnd, const LPCSTR user, const LPCSTR key,
	                                         SplitUserLicenseInputFn splitter) override;
	virtual void UpdateLicenseStatus() override;
	virtual DWORD LicenseCommandQueryStatus(LicenseCommands cmd) override;
	virtual void LicenseCommandExec(LicenseCommands cmd) override;
	virtual LPCWSTR GetLicenseInfoUser(bool forceLoad = false) override;
	virtual LPCSTR GetLicenseInfoKey(bool forceLoad = false) override;
	virtual BOOL IsSanctuaryLicenseInstalled(bool checkValid = false) override;

	virtual void LogStr(const LPCSTR txt) override
	{
		if (txt)
			vCatLog("Licensing.LogStr", "%s", txt);
	}

	virtual void GetBuildDate(int& buildYear, int& buildMon, int& buildDay) override
	{
		::GetBuildDate(buildYear, buildMon, buildDay);
	}

	virtual LPCWSTR GetDllDir() override
	{
		return VaDirs::GetDllDir();
	}

	virtual LPCWSTR GetUserDir() override
	{
		return VaDirs::GetUserDir();
	}

	virtual void Shutdown() override
	{
#if defined(SANCTUARY)
		auto tmp = gAuxDll;
		gAuxDll = nullptr;
		if (tmp)
			tmp->VaUnloading();
		if (gAuxLib.IsLoaded())
			gAuxLib.Unload();
#endif
	}
};

VaLicensingHost gLicensingHost;
IVaLicensing* gVaLicensingHost(&gLicensingHost);

#include "BuildInfo.h"
#include "../../../3rdParty/Armadillo/Armadillo.h"
#include "../../WTkeygen/WTValidate.h"
#include "3dayExtension.h"
#include "BuyTryDlg.h"
#include "BuyTryDlg_Strings.h"

bool AutomaticLicenseRegistration();

namespace Armadillo
{
inline CString GetEnvCString(LPCSTR env)
{
	return Armadillo::GetEnv<CString>(env);
}
} // namespace Armadillo

void InitLicensingHost()
{
	if (!gVaLicensingHost)
		gVaLicensingHost = &gLicensingHost;
}

int VaLicensingHost::GetArmDaysLeft()
{
#ifdef _DEBUG
	return 3;
#else
	return ::atoi(Armadillo::GetEnvCString("DAYSLEFT"));
#endif
}

int VaLicensingHost::GetArmDaysInstalled()
{
	return ::atoi(Armadillo::GetEnvCString("DAYSINSTALLED"));
}

LPCSTR
VaLicensingHost::GetArmString(const LPCSTR str)
{
	static CString sSec;
	sSec = Armadillo::GetEnvCString(str);
	return sSec;
}

int VaLicensingHost::IsArmExpired()
{
#ifdef NDEBUG
	CString tmp(Armadillo::GetEnvCString("EXPIRED"));
	if (tmp.GetLength())
	{
		int res = ::atoi(tmp);
		if (res)
			return res;
		return -1;
	}
#endif

	return 0;
}

int VaLicensingHost::GetArmClock()
{
#ifdef NDEBUG
	if (Armadillo::GetEnvCString("CLOCKBACK").GetLength())
		return 1;
	if (Armadillo::GetEnvCString("CLOCKFORWARD").GetLength())
		return 1;
#endif

	return 0;
}

int VaLicensingHost::GetLicenseUserCount()
{
	if (IsSanctuaryLicenseInstalled(true))
		return 1;

	return ::GetLicenseUserCount();
}

int VaLicensingHost::GetLicenseStatus()
{
#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			const bool licensed = ptr->checkStatus();
			if (ptr->HasProductInfo())
			{
				if (licensed)
				{
					if (ptr->IsLicenseValidForThisBuild(false))
					{
						vCatLog("Licensing.GetLicenseStatus", "sanctgls: 0");
						return (int)LicenseStatus_Valid;
					}
					else
					{
						vCatLog("Licensing.GetLicenseStatus", "sanctgls: 2a");
						return (int)LicenseStatus_Expired;
					}
				}

				const CString err(GetErrorMessage());
				if (err.GetLength() > 2)
				{
					if (-1 != err.Find("Maximum number of users already reached"))
					{
						vCatLog("Licensing.GetLicenseStatus", "sanctgls: 6");
						return (int)LicenseStatus_NoFloatingSeats;
					}

					if (ptr->IsConcurrentLicenseInstalled() || ptr->IsNamedNetworkLicenseInstalled())
					{
						vCatLog("Licensing.GetLicenseStatus", "sanctgls: 4");
						return (int)LicenseStatus_SanctuaryElcError;
					}

					vCatLog("Licensing.GetLicenseStatus", "sanctgls: 5");
					return (int)LicenseStatus_SanctuaryError;
				}

				// assume no error message and !licensed means expired.
				// this can also occur for cases where there is an error but sanctuary doesn't give error info (see case
				// 138160, case 138603). this could happen for expired eSlip because it appears that they don't support
				// perpetual license (?) assume not possible to have expired sanctuary maintenance but valid armadillo
				vLogUnfiltered("WARN: sanctgls: 6 no err msg");
				return (int)LicenseStatus_Expired;
			}
			else if (licensed)
			{
				vLogUnfiltered("ERROR: sanctgls: 7 missing info");
			}
		}
	}
#endif

	return (int)::ValidateVAXUserInfo();
}

LPCSTR
VaLicensingHost::GetErrorMessage()
{
#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			static CString sErrMsg;
			const CString msg(ptr->getLastErrorMsg());
			if (msg != sErrMsg)
				sErrMsg = msg;
			return sErrMsg;
		}
	}
#endif

	return nullptr;
}

LPCSTR
VaLicensingHost::GetLicenseExpirationDate()
{
	static std::string expDate;

#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			ptr->checkStatus();
			if (ptr->HasProductInfo())
			{
				LPCSTR res = ptr->GetMaintenanceEndDate();
				if (res)
					expDate = res;
				else
					expDate = "";
				return expDate.c_str();
			}
		}
	}
#endif

	::GetLicenseExpDate(expDate);
	return expDate.c_str();
}

BOOL VaLicensingHost::IsNonRenewableLicenseInstalled()
{
#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			const bool licensed = ptr->checkStatus();
			if (ptr->HasProductInfo())
			{
				if (ptr->IsPersonalLicense())
					return true;
				if (ptr->IsAcademicLicense())
					return true;
				if (ptr->IsRenewableLicense())
					return false;

				if (licensed)
					_ASSERTE(!"bad condition in VaLicensingHost::IsNonRenewableLicenseInstalled");
			}
		}
	}
#endif
	return ::IsNonrenewableLicenseInstalled();
}

BOOL VaLicensingHost::IsNonPerpetualLicenseInstalled()
{
	if (IsSanctuaryLicenseInstalled(true))
		return false; // sanctuary doesn't have non-perpetual license

	return ::IsNonPerpetualLicenseInstalled();
}

BOOL VaLicensingHost::IsAcademicLicenseInstalled()
{
#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr && ptr->checkStatus())
		{
			if (ptr->IsAcademicLicense())
				return true;
		}
	}
#endif

	CStringW user(GetLicenseInfoUser());
	if (!user.IsEmpty())
		if (user.Find(L"cademic") != -1 || user.Find(L"classroom ") != -1)
			return true;

	return false;
}

BOOL VaLicensingHost::IsVaxNetworkLicenseCountSupported()
{
	if (IsSanctuaryLicenseInstalled(true))
		return false;

	return ::IsVaxNetworkLicenseCountSupported();
}

BOOL VaLicensingHost::IsVaxNetworkLicenseCountOk()
{
	_ASSERTE(!IsSanctuaryLicenseInstalled(true) &&
	         "sanctuary bad condition -- IsVaxNetworkLicenseCountOk shouldn't have been called");
	return ::IsVaxNetworkLicenseCountOk();
}

IVaLicensing::RegisterResult VaLicensingHost::RegisterArmadillo(HWND hWnd, const LPCSTR userIn, const LPCSTR keyIn,
                                                                SplitUserLicenseInputFn splitter)
{
	CString user(userIn);
	CString key(keyIn);
	if (user == "Fix Clock")
	{
		if (Armadillo::FixClock(key))
			return OkTrial;
	}
	else if (user.Find("Trial License") != -1)
	{
		if (Armadillo::CheckCode("Trial License", key)) // ignore trailing (Jerry@WTS.com) if any
			return OkTrial;
	}
	else if (user.Find("Trial Extension") != -1)
	{
		if (Armadillo::CheckCode("Trial Extension", key)) // ignore trailing (Jerry@WTS.com) if any
			return OkTrial;
	}

	if (key.GetLength() <= 20)
	{
		// VA 4.1 key
		return ErrShortInvalidKey;
	}

	HINSTANCE hResHandle = ::AfxGetResourceHandle() ? ::AfxGetResourceHandle() : ::AfxGetInstanceHandle();
	_ASSERTE(hResHandle);
	::SetLicenseResourceHandle(hResHandle);
	bool wasRenewal = false;
	bool renewalValidationOk = true;
	bool wasRenewalInstalled;
	LicenseStatus licenseStatus = LicenseStatus_Invalid;

	// if key entered is a renewal, we can't install it until we have
	// verified the user's eligibility to install it
	if (IsVAXRenewal(user, key, wasRenewalInstalled))
	{
		wasRenewal = true;
		ASSERT(!wasRenewalInstalled);

		// [case: 118868]
		// check that installed version of VA is even suitable for the
		// renewal before validating previous license
		if (!::LicenseAppearsEligibleForCurrentBuild(user))
			return ErrRenewableLicenseExpired;

		// this was a renewal, so before installing license, verify eligibility
		licenseStatus = ValidateForRenewalLicense(hWnd, key, splitter);
		if (LicenseStatus_Valid == licenseStatus || LicenseStatus_Expired == licenseStatus)
			licenseStatus = InstallIfVAXUserInfo(user, key);
		else
			renewalValidationOk = false;
	}
	else
	{
		// wasn't a renewal - so go ahead and install license
		licenseStatus = InstallIfVAXUserInfo(user, key);
	}

	switch (licenseStatus)
	{
	case LicenseStatus_Valid:
		return OkLicenseAccepted;
	case LicenseStatus_Expired:
		if (IsNonPerpetualLicenseInstalled())
			return ErrNonPerpetualLicenseExpired;
		if (IsNonRenewableLicenseInstalled())
			return ErrNonRenewableLicenseExpired;
		return ErrRenewableLicenseExpired;
	default:
		if (wasRenewal)
		{
			if (renewalValidationOk)
			{
				// renewal key but failed to install
				return ErrInvalidKey;
			}

			// renewal key but failed to validate previous
			return ErrIneligibleRenewal;
		}

		// not renewal, failed to install
		return ErrInvalidKey;
	}
}

void VaLicensingHost::UpdateLicenseStatus()
{
#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO)
	if (!IsSanctuaryLicenseInstalled(false))
		::WtLicenseUpdateStatus();
#endif
	GetLicenseInfoUser(true);
	GetLicenseInfoKey(true);
}

DWORD
VaLicensingHost::LicenseCommandQueryStatus(LicenseCommands cmd)
{
#if defined(RAD_STUDIO) || defined(NO_ARMADILLO) || defined(AVR_STUDIO)
	return UINT_MAX;
#else

	if (cmdCheckout == cmd || cmdCheckin == cmd)
	{
#if defined(SANCTUARY)
		if (IsSanctuaryLicenseInstalled(true))
		{
			if (gAuxDll)
			{
				auto ptr = gAuxDll->GetSanctuaryClient();
				if (ptr)
				{
					if (!ptr->SupportsOfflineCheckout())
						return UINT_MAX;
					if (ptr->HasOfflineCheckout())
						return cmdCheckin == cmd ? 1u : 0u;
					return cmdCheckin == cmd ? 0u : 1u;
				}
			}
		}
#endif
		return UINT_MAX;
	}

	if (IsSanctuaryLicenseInstalled(false))
		return UINT_MAX;

	WtLicenseCommands wtCmd = lcRenew;
	switch (cmd)
	{
	case cmdPurchase:
		wtCmd = lcPurchase;
		break;
	case cmdRenew:
		wtCmd = lcRenew;
		break;
	case cmdSubmitHelp:
		wtCmd = lcSubmitHelp;
		break;
	default:
		_ASSERTE(!"unhandled license command");
	}

	return ::WtLicenseQueryStatus(wtCmd);
#endif
}

void VaLicensingHost::LicenseCommandExec(LicenseCommands cmd)
{
	if (cmdCheckout == cmd || cmdCheckin == cmd)
	{
#if defined(SANCTUARY)
		if (IsSanctuaryLicenseInstalled(true))
		{
			if (gAuxDll)
			{
				auto ptr = gAuxDll->GetSanctuaryClient();
				if (ptr)
				{
					if (cmdCheckout == cmd)
					{
						CheckoutDurationDlg dlg(gMainWnd);
						if (IDCANCEL == dlg.DoModal())
							return;

						int requestedHours = dlg.GetDuration();
						CString msg;
						int iconFlag = MB_ICONERROR;
						int res = 0;

						{
							CWaitCursor wt;
							res = ptr->OfflineCheckout(requestedHours);
						}

						if (res)
						{
							CString__FormatA(msg,
							                 "Checkout of license failed (error CO-1 : %d).\r\n\r\n"
							                 "For license server help, please report the error to licensing support at "
							                 "licenses@wholetomato.com.",
							                 res);
						}
						else if (!ptr->HasOfflineCheckout())
						{
							msg = "Checkout of license failed (error CO-2).\r\n\r\n"
							      "A common cause of this error is forgetting to enable license checkout/borrowing on "
							      "the license server.\r\n\r\n"
							      "For license server help, please report the error to licensing support at "
							      "licenses@wholetomato.com.";
						}
						else
						{
							const int grantedHours = ptr->GetOfflineHoursGranted();
							if (grantedHours != requestedHours)
							{
								// didn't get what we asked for
								// checkout can succeed but with cap defined on server
								CString__FormatA(
								    msg,
								    "Checkout of license succeeded but the request apparently exceeded the limit set "
								    "by the license server administrator (error CO-3).\r\n\r\n"
								    "Requested: %d hours\r\nGranted: %d hours",
								    requestedHours, grantedHours);
								iconFlag = MB_ICONINFORMATION;
							}
						}

						if (!msg.IsEmpty())
							::WtMessageBox(msg, IDS_APPNAME, uint(MB_OK | iconFlag));
					}
					else
						ptr->OfflineCheckin();
				}
			}
		}
#endif
		return;
	}

	_ASSERTE(!"unhandled license command");
}

LPCWSTR
VaLicensingHost::GetLicenseInfoUser(bool forceLoad)
{
	static CStringW sPinnedUser;
	if (forceLoad || sPinnedUser.IsEmpty())
	{
		sPinnedUser = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP_KEY, ID_RK_USERNAME);
		if (IsSanctuaryLicenseInstalled(true))
		{
			sPinnedUser.Empty();

#if defined(SANCTUARY)
			if (gAuxDll)
			{
				auto ptr = gAuxDll->GetSanctuaryClient();
				if (ptr)
					sPinnedUser = ptr->GetPortalUserName();
			}
#endif
		}
	}
	return sPinnedUser;
}

LPCSTR
VaLicensingHost::GetLicenseInfoKey(bool forceLoad)
{
	static CString sPinnedKey;
	if (forceLoad || sPinnedKey.IsEmpty())
	{
		sPinnedKey = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, ID_RK_USERKEY);

#if defined(SANCTUARY)
		if (gAuxDll)
		{
			auto ptr = gAuxDll->GetSanctuaryClient();
			if (ptr && ptr->checkStatus())
			{
				const CString sn(ptr->GetSerialNumber());
				if (!sn.IsEmpty())
				{
					if (sPinnedKey.IsEmpty())
						sPinnedKey = sn;
					else if (ptr->IsLicenseValidForThisBuild(false))
						sPinnedKey = sn;
				}
			}
		}
#endif
	}

	return sPinnedKey;
}

BOOL VaLicensingHost::IsSanctuaryLicenseInstalled(bool checkValid /*= false*/)
{
#if defined(SANCTUARY)
	if (gAuxDll)
	{
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			const bool licensed = ptr->checkStatus();
			if (ptr->HasProductInfo())
			{
				if (!checkValid)
					return true;

				if (licensed)
					if (ptr->IsLicenseValidForThisBuild(false))
						return true;
			}
		}
	}
#endif

	return false;
}

#if defined(SANCTUARY)
static void LoadAuxDll()
{
	const CString auxFilename(IDS_VAAUX_DLL);
	const CString fullVatePath(VaDirs::GetDllDir() + CStringW(auxFilename));
	gAuxLib.Load(fullVatePath);
	if (!gAuxLib.IsLoaded())
		gAuxLib.LoadFromVaDirectory(auxFilename);
}

static void InitAuxDll()
{
	::LoadAuxDll();
	if (!gAuxLib.IsLoaded())
	{
		vLogUnfiltered("ERROR: vaaux failed to load 1");
		return;
	}

	using GetAuxDllFn = IVaAuxiliaryDll*(_cdecl*)();
	GetAuxDllFn pfnGetAuxDll = nullptr;
	gAuxLib.GetFunction("GetAuxDll", pfnGetAuxDll);
	if (pfnGetAuxDll)
		gAuxDll = pfnGetAuxDll();
}
#endif

BOOL VaLicensingHost::InitLicense(IVaLicensingUi* vaLicUi)
{
#if defined(SANCTUARY)
	::InitAuxDll();
	if (!gAuxDll)
	{
		vLogUnfiltered("ERROR: vaaux failed to load 2");
		return FALSE;
	}

	gAuxDll->InitSanctuaryClient(&gLicensingHost);
#endif

	_ASSERTE(vaLicUi);

	// DAYSINSTALLED refers to the last installed license
	const CString kDaysInstalled(Armadillo::GetEnvCString("DAYSINSTALLED"));
#ifndef _DEBUG
	if (kDaysInstalled.IsEmpty())
	{
		// File is not locked...
#ifndef NOSMARTFLOW
		SmartFlowPhoneHome::FireArmadilloMissing();
#endif
		return 0;
	}
#endif

	HINSTANCE hResHandle = AfxGetResourceHandle() ? AfxGetResourceHandle() : AfxGetInstanceHandle();
	_ASSERTE(hResHandle);
	SetLicenseResourceHandle(hResHandle);
	int buildYr, buildMon, buildDay;
	vaLicUi->GetBuildDate(buildYr, buildMon, buildDay);
	SetBuildDate((WORD)buildYr, (WORD)buildMon, (WORD)buildDay);
	const bool kIsNonPerpetualLicenseInstalled = IsNonPerpetualLicenseInstalled();
	(void)kIsNonPerpetualLicenseInstalled;

#if defined(_DEBUG) && !defined(SEAN) /* && 0 */
	return 1;
#else
	const bool isSanct = IsSanctuaryLicenseInstalled(false);
	LicenseStatus license;
	if (isSanct)
	{
		LicenseTraceBox(*AfxGetMainWnd(), "isSanct");
		license = (LicenseStatus)GetLicenseStatus();
	}
	else
		license = ValidateVAXUserInfo();
	bool trialExpired = Armadillo::GetEnvCString("EXPIRED").GetLength() > 0;

	// [case: 51486] don't invalidate good trial by looking for old license
	if (LicenseStatus_Invalid == license && !kIsNonPerpetualLicenseInstalled)
	{
		LicenseTraceBox(*AfxGetMainWnd(), "invalid license - checking for license info in va reg");
		// check for pre-10.6 (pre-vs2010) HKLM arm license (by reapplying user info stored in HKCU)
		std::string user, key;
		if (LoadVaxLicenseInfo(user, key))
		{
			LicenseTraceBox(*AfxGetMainWnd(), "found license info");
			if (!trialExpired && LicenseAppearsEligibleForCurrentBuild(user.c_str()))
			{
				// [case: 77766]
				// license appears to be eligible for current build
				trialExpired = true;
			}

			if (trialExpired)
			{
				// Do not perform renewal validation since we would need to
				// prompt for old license (we don't save prev license in HKCU)
				LicenseStatus licenseStatus = InstallIfVAXUserInfo(user.c_str(), key.c_str());
				if (licenseStatus == LicenseStatus_Valid)
				{
					license = ValidateVAXUserInfo();
					LicenseTraceBox1(*AfxGetMainWnd(), "installed license info; new status = %d", license);
				}
				else
					LicenseTraceBox(*AfxGetMainWnd(), "failed to install license info");
			}
		}
		else
			LicenseTraceBox(*AfxGetMainWnd(), "failed to locate license info");
	}

	if (LicenseStatus_NoFloatingSeats == license || LicenseStatus_SanctuaryElcError == license)
	{
		// [case: 142237]
		LicenseTraceBox(*AfxGetMainWnd(), "sanctuary ELC issue");
	}
	else
	{
		// see if X day trial extension should be granted
		if (LicenseStatus_Expired == license || (trialExpired && LicenseStatus_Valid != license))
		{
			// [case: 51486] grant X day extension if purchased license has expired even
			// if 30 days has not passed
			if (!kIsNonPerpetualLicenseInstalled && (atoi(kDaysInstalled) > 29 || LicenseStatus_Expired == license))
			{
				LicenseTraceBox1(*AfxGetMainWnd(), "install X day trial extension DAYSINSTALLED=%s", kDaysInstalled);
				// Prior install expired, user may be testing newer build, so give them X more days to eval.
				// X day key with no install expiration limit.
				// Key should be updated after every release.
				Armadillo::CheckCode(X_DAY_TRIAL_EXTENSION_NAME, X_DAY_TRIAL_EXTENSION_KEY);
#if defined(LICENSE_TRACING)
				CString tmp;
				CString__FormatA(tmp, "post X day extension attempt: DAYSLEFT=%s DAYSINSTALLED=%s",
				                 Armadillo::GetEnvCString("DAYSLEFT"), kDaysInstalled);
				LicenseTraceBox(*AfxGetMainWnd(), tmp);
#endif
				if (LicenseStatus_Expired == license && isSanct)
					; // don't look for armadillo license because that will change to license to Invalid if sanctuary is
					  // present
				else
					license = ValidateVAXUserInfo();
			}
			else
				LicenseTraceBox1(*AfxGetMainWnd(), "NOT install X day trial extension DAYSINSTALLED=%s",
				                 kDaysInstalled);
		}
		else
		{
			LicenseTraceBox1(*AfxGetMainWnd(), "Not expired: licensestatus = %d", license);
		}
	}

	if (license != LicenseStatus_Valid)
	{
		if (AutomaticLicenseRegistration())
		{
			if (IsSanctuaryLicenseInstalled(true)) // recheck sanctuary
				license = LicenseStatus_Valid;
			else
				license = ValidateVAXUserInfo();
		}
	}

	LicenseTraceBox1(*AfxGetMainWnd(), "license status = %d", license);
	if (LicenseStatus_Valid != license)
	{
		if (!vaLicUi->DoTryBuy(license, kIsNonPerpetualLicenseInstalled))
		{
			// User hit cancel
#ifndef NOSMARTFLOW
			if (LicenseStatus_Expired == license)
				SmartFlowPhoneHome::FireCancelExpiredLicense();
			else if (trialExpired)
				SmartFlowPhoneHome::FireCancelTrial();
#endif
			return 0;
		}

		// see if user hit Try or entered a valid key
		if (IsSanctuaryLicenseInstalled(true)) // recheck
			license = LicenseStatus_Valid;
		else
			license = ValidateVAXUserInfo();

		if (LicenseStatus_Valid != license)
		{
			if (Armadillo::GetEnvCString("CLOCKBACK").GetLength())
			{
#ifndef NOSMARTFLOW
				SmartFlowPhoneHome::FireArmadilloClockBack();
#endif
				vaLicUi->ErrorBox(kErrBoxText_ClockBack);
				return 0;
			}
			if (Armadillo::GetEnvCString("CLOCKFORWARD").GetLength())
			{
#ifndef NOSMARTFLOW
				SmartFlowPhoneHome::FireArmadilloClockForward();
#endif
				vaLicUi->ErrorBox(kErrBoxText_ClockForward);
				return 0;
			}
			if (Armadillo::GetEnvCString("EXPIRED").GetLength())
			{
#ifndef NOSMARTFLOW
				SmartFlowPhoneHome::FireArmadilloExpired();
#endif
				vaLicUi->ErrorBox(kErrBoxText_Expired);
				LicenseTraceBox1(*AfxGetMainWnd(), "EXPIRED! license status = %d", license);
				return 0;
			}
		}
	}

	if (LicenseStatus_Valid == license)
	{
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
#else
		vaLicUi->CheckForLicenseExpirationIfEnabled();
#endif
		vaLicUi->StartLicenseMonitor();
	}

	return 1;
#endif // _DEBUG && !SEAN
}

// Case 869: allow for automatic license registration during install

CStringW ReadAndMaybeDeleteRegValue(LPCSTR valName)
{
	CStringW val(GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP_KEY, valName));
	if (val.IsEmpty())
	{
		val = GetRegValueW(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, valName);
		// don't attempt reg delete in case other users need it
	}
	else
	{
		// delete reg val
		DeleteRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, valName);
	}

	return val;
}

bool SilentSanctuaryLicenseRegistration()
{
#if defined(SANCTUARY)
	if (!gAuxDll)
		return false;

	extern void ResetArmadilloInfo();

	// check for automatic slip registration
	const CStringW slipFilepath(ReadAndMaybeDeleteRegValue("AutoRegSlipFilepath"));
	if (!slipFilepath.IsEmpty() && IsFile(slipFilepath))
	{
		// based on CImportLicenseFileDlg::OnOK
		auto ptr = gAuxDll->GetSanctuaryClient();
		if (ptr)
		{
			bool res = ptr->ImportLicenseFile(slipFilepath, nullptr);
			if (res)
			{
				ptr->reload();
				res = ptr->checkStatus();
				if (res && ptr->IsLicenseValidForThisBuild(false))
				{
					ResetArmadilloInfo();
					return true;
				}
			}
		}
	}

	// check for automatic serial/name/password registration
	const CStringW serialNumber(ReadAndMaybeDeleteRegValue("AutoRegSerialNumber"));
	if (!serialNumber.IsEmpty())
	{
		const CStringW nameOrEmail(ReadAndMaybeDeleteRegValue("AutoRegPortalUser"));
		if (!nameOrEmail.IsEmpty())
		{
			const CStringW password(ReadAndMaybeDeleteRegValue("AutoRegPortalPassword"));
			if (!password.IsEmpty())
			{
				// based on CRegisterSanctuary::OnRegister
				auto ptr = gAuxDll->GetSanctuaryClient();
				if (ptr)
				{
					bool res = ptr->registerSerial(serialNumber, nameOrEmail, password);
					if (res)
					{
						ptr->reload();
						res = ptr->checkStatus();
						if (res && ptr->IsLicenseValidForThisBuild(false))
						{
							ResetArmadilloInfo();
							return true;
						}
					}
				}
			}
		}
	}

#endif
	return false;
}

bool SilentArmadilloLicenseRegistration()
{
	const LPCSTR kAutoRegUserNew = "AutoRegLicenseUser";
	const LPCSTR kAutoRegKeyNew = "AutoRegLicenseKey";
	const LPCSTR kAutoRegUserPrev = "AutoRegPrevLicenseUser";
	const LPCSTR kAutoRegKeyPrev = "AutoRegPrevLicenseKey";

	// New User and Key data are required for automatic reg
	CString user = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, kAutoRegUserNew);
	CString key = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, kAutoRegKeyNew);
	if (user.IsEmpty() || key.IsEmpty())
	{
		user = GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, kAutoRegUserNew);
		key = GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, kAutoRegKeyNew);
		if (user.IsEmpty() || key.IsEmpty())
			return false;
	}

	// AutoRegPrev is optional
	CString prevUser = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, kAutoRegUserPrev);
	CString prevKey = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, kAutoRegKeyPrev);
	if (prevUser.IsEmpty() || prevKey.IsEmpty())
	{
		prevUser = GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, kAutoRegUserPrev);
		prevKey = GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, kAutoRegKeyPrev);
	}

	HINSTANCE hResHandle = AfxGetResourceHandle() ? AfxGetResourceHandle() : AfxGetInstanceHandle();
	_ASSERTE(hResHandle);
	SetLicenseResourceHandle(hResHandle);
	LicenseStatus licenseStatus = LicenseStatus_Invalid;
	bool wasRenewalInstalled;

	// if key entered is a renewal, we can't install it until we have
	// verified the user's eligibility to install it
	if (IsVAXRenewal(user, key, wasRenewalInstalled))
	{
		ASSERT(!wasRenewalInstalled);
		// this was a renewal, so before installing license, verify eligibility
		licenseStatus = ValidateForRenewalLicense(prevUser, prevKey, key);
		if (LicenseStatus_Valid == licenseStatus || LicenseStatus_Expired == licenseStatus)
			licenseStatus = InstallIfVAXUserInfo(user, key);
		// else failed to validate renewal info
	}
	else
	{
		// wasn't a renewal - so go ahead and install license
		licenseStatus = InstallIfVAXUserInfo(user, key);
	}

	if (licenseStatus == LicenseStatus_Valid)
		UnhideCheckboxForRenewableArmadilloLicences(user);

	return (licenseStatus == LicenseStatus_Valid);
}

bool AutomaticLicenseRegistration()
{
	if (SilentSanctuaryLicenseRegistration() || SilentArmadilloLicenseRegistration())
	{
		LogVersionInfo::ForceNextUpdate();
		// update license info for query status
		gVaLicensingHost->UpdateLicenseStatus();
		return true;
	}

	return false;
}
