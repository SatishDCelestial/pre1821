#include "stdafxed.h"
#include "../PooledThreadBase.h"
#include "../Registry.h"
#include "../Settings.h"
#include "WTAutoUpdater.h"
#include "../Directories.h"
#include "../DevShellAttributes.h"
#include "../FILE.H"
#include "../DevShellService.h"
#include "../PROJECT.H"
#include "../VaPkg/VaPkgUI/PkgCmdID.h"
#include "IdeSettings.h"
#include "NavigationBarQuestion.h"
#include "FileTypes.h"
#include "VAAutomation.h"
#include "VaAddinClient.h"
#include "MenuXP\Tools.h"
#include "LogVersionInfo.h"
#include "DllNames.h"
#include "RegKeys.h"
#include "FileVerInfo.h"
#include "MaintenanceRenewal_MultiUser.h"
#include "MaintenanceRenewal.h"

// REG strings
#define CHECK_VERSION_DATE "checkVersionDate" // Next date we will check
#define CHECK_VERSION_LAST "checkVersionLast" // prevent asking for the same version twice
#define CHECK_EVERY_N_DAYS 7                  // version update
#define REMIND_IN_N_DAYS 7                    // navbar reminder
#define FIRST_REMINDER 4                      // navbar reminder

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

static CString PV_GetIdeVerStr();

#if !defined(RAD_STUDIO)
WTString GetCheckForUpdateRegPath()
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		return WTString(
		    ID_RK_APP); // Since each IDE will have it's own install, we need to notify each VANet10/VANet11/...
	return WTString(ID_RK_APP); // Previous IDE's share the same install RegPath
}

WTAutoUpdater::WTAutoUpdater(void)
{
	FileVersionInfo fvi(::GetMainDllName());
	mThisBuild = fvi.GetProdVerLSHi();
	mThisBuildSpecial = fvi.GetProdVerLSLo();
	mSupportExp = ""; // empty for trial users

	if (gVaLicensingHost)
		mSupportExp = gVaLicensingHost->GetLicenseExpirationDate();
}

WTAutoUpdater::~WTAutoUpdater(void)
{
}

// manualCheck means check for beta even if check for beta is not enabled
// manualCheck also means ignore last prompted version (last prompted version
// is used to prevent repeat notices)
void WTAutoUpdater::CheckForUpdate(BOOL checkForBeta, BOOL manualCheck)
{
	// See if we are running the latest
	INT curRelease = 0, curReleaseSpecial = 0, curBeta = 0, curBetaSpecial = 0;
	if (GetCurrentBuildNumbers(checkForBeta || manualCheck, curRelease, curReleaseSpecial, curBeta, curBetaSpecial))
	{
		INT newVersion = 0;
		if ((checkForBeta || manualCheck) &&
		    ((mThisBuild < curBeta) || (mThisBuild == curBeta && mThisBuildSpecial < curBetaSpecial)))
			newVersion = curBeta | (curBetaSpecial << 16);
		else if ((mThisBuild < curRelease) || (mThisBuild == curRelease && mThisBuildSpecial < curReleaseSpecial))
			newVersion = curRelease | (curReleaseSpecial << 16);

		const WTString regPath = GetCheckForUpdateRegPath();
		DWORD lastPromptedVersion = GetRegDword(HKEY_CURRENT_USER, regPath.c_str(), _T(CHECK_VERSION_LAST));

		if (newVersion && (manualCheck || newVersion != (int)lastPromptedVersion))
		{
			const CString msg("A new build of Visual Assist is available. Click OK for details.");
			SetRegValue(HKEY_CURRENT_USER, regPath.c_str(), _T(CHECK_VERSION_LAST), (DWORD)newVersion);
			if (WtMessageBox(msg, IDS_APPNAME, MB_ICONINFORMATION | MB_OKCANCEL) == IDOK)
			{
				CString url;
				CString__FormatA(url, "https://www.wholetomato.com/downloads/CheckForUpdate.asp?v=%d&vs=%d&e=%s&b=%c",
				                 mThisBuild, mThisBuildSpecial, (LPCTSTR)mSupportExp,
				                 (checkForBeta || manualCheck) ? 'y' : 'n');
				url += gVaLicensingHost->IsNonRenewableLicenseInstalled()   ? "&r=n"
				       : gVaLicensingHost->IsNonPerpetualLicenseInstalled() ? "r=np"
				                                                            : "&r=y";
				url += PV_GetIdeVerStr();

				FileVersionInfo fvi; // Log IDE build number. [case=39942]
				if (fvi.QueryFile(L"DevEnv.exe", FALSE))
					url += CString("&vsbld=") + ::itos(fvi.GetFileVerLSHi()).c_str();

				// Go to update asp
				ShellExecute(::GetDesktopWindow(), _T("open"), url, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		else if (manualCheck)
		{
			CString msg;
			CString__FormatA(msg, "You have the latest build.");
			WtMessageBox(msg, IDS_APPNAME, MB_ICONINFORMATION | MB_OK);
		}
	}
}

CString GetFutureDate(int days)
{
	COleDateTime curDate = COleDateTime::GetCurrentTime();
	COleDateTimeSpan span(days, 0, 0, 0);
	COleDateTime futureDate = curDate + span;
	CString futureDateStr;
	CString__FormatT(futureDateStr, _T("%04d.%02d.%02d"), futureDate.GetYear(), futureDate.GetMonth(),
	                 futureDate.GetDay());
	return futureDateStr;
}

void LaunchRenewalPage(const CString& url)
{
	ShellExecute(NULL, _T("open"), url, NULL, NULL, SW_SHOW);
}

void WTAutoUpdater::CheckForLicenseExpiration()
{
	Log("Renewmaintenance: WTAutoUpdater::CheckForLicenseExpiration()");
	_ASSERTE(gVaLicensingHost);

	// current date
	SYSTEMTIME ctm;
	GetSystemTime(&ctm);
	CString curDateStr;
	curDateStr.Format(_T("%04d.%02d.%02d"), ctm.wYear, ctm.wMonth, ctm.wDay);

	// expiration date - checks both sanc and arm
	std::string expDate(gVaLicensingHost->GetLicenseExpirationDate());

	// upcoming expiration (expDate-7 days)
	CString futureDateStr = GetFutureDate((int)Psettings->mRenewNotificationDays);

	// within last week of maintenance, or expired
	bool passedExpiration = curDateStr.Compare(expDate.c_str()) > 0;
	bool nearExpiration = futureDateStr.Compare(expDate.c_str()) > 0;

	// dialogs
	if (nearExpiration || passedExpiration)
	{
		CString nextReminder = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder");
		if (nextReminder == "No" || (!curDateStr.IsEmpty() && curDateStr.Compare(nextReminder) < 0))
			return;

		if (gVaLicensingHost->GetLicenseUserCount() != 1) // handles both sanc and arm
		{
			// multi user
			CString message = _T("Your software maintenance, which includes software updates and priority support, ");

			if (passedExpiration)
				message += _T("expired recently. ");
			else
				message += _T("is about to expire. ");

			message += _T("Would you like to get a quote to renew maintenance, or email a request to the license holder, ");

			extern CString GetEmailFromLicense();
			CString email = GetEmailFromLicense();
			if (email == "")
				return;
			message += email;
			message += "?";

			MaintenanceRenewal_MultiUser mainRen(gMainWnd);
			mainRen.mMessageStr = message;
			auto id = mainRen.DoModal();

			if (id == IDCANCEL && mainRen.m_bRemaindLater)
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder", futureDateStr);
			else
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder", "No");

			extern void LaunchEmailToBuyer();
			LaunchEmailToBuyer();
		}
		else
		{
			// single user
			MaintenanceRenewal mainRen(gMainWnd);
			CString message = _T("Your software maintenance, which includes software updates and priority support, ");

			if (passedExpiration)
				message += _T("expired recently. ");
			else
				message += _T("is about to expire. ");

			message += _T("Would you like to get a quote to renew maintenance?");

			mainRen.mMessageStr = message;
			auto id = mainRen.DoModal();
			if (id == IDOK)
			{
		        CString url;
				if (gVaLicensingHost->IsNonPerpetualLicenseInstalled())
					url = "https://www.wholetomato.com/purchase-personal";
				else
					url = "https://www.wholetomato.com/purchase-perpetual";
				LaunchRenewalPage(url);
			}

			if (id == IDCANCEL && mainRen.m_bRemaindLater)
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder", futureDateStr);
			else
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "MaintenanceRenewalReminder", "No");
		}
	}
}

static CString PV_GetIdeVerStr()
{
	CString retval("&i=");
	if (gShellAttr)
	{
		int v = gShellAttr->GetRegistryVersionNumber();
		if (v)
			retval += itos(v).c_str();
		else
			retval += "X";
	}
	else
	{
		retval += "0";
	}

	if (gDte)
	{
		CComBSTR edb;
		if (SUCCEEDED(gDte->get_Edition(&edb)))
		{
			if (edb.Length() > 1)
			{
				CString ed(edb);
				ed.MakeLower();
				retval += "&s="; // s == vs sku

				if (-1 != ed.Find("ultimate"))
					retval += "u";
				else if (-1 != ed.Find("premium"))
					retval += "p";
				else if (-1 != ed.Find("professional"))
					retval += "r";
				else if (-1 != ed.Find("standard"))
					retval += "s";
				else if (-1 != ed.Find("community"))
					retval += "c";
				else if (-1 != ed.Find("team"))
				{
					if (-1 != ed.Find("architect"))
						retval += "ta";
					else if (-1 != ed.Find("database"))
						retval += "tb";
					else if (-1 != ed.Find("test"))
						retval += "ts";
					else if (-1 != ed.Find("develop"))
						retval += "tv";
					else if (-1 != ed.Find("foundation"))
						retval += "tf";
					else
						retval += "tx";
				}
				else if (-1 != ed.Find("enterprise"))
				{
					if (-1 != ed.Find("architect"))
						retval += "ea";
					else if (-1 != ed.Find("develop"))
						retval += "ev";
					else
						retval += "ex";
				}
				else if (-1 != ed.Find("academic"))
					retval += "a";
				else
					retval += "x";
			}
		}
	}

	return retval;
}

BOOL WTAutoUpdater::GetCurrentBuildNumbers(bool getRealLatestBuild, INT& curRelease, INT& curReleaseSpecial,
                                           INT& curBeta, INT& curBetaSpecial)
{
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
	return FALSE;
#else
	// if getlatestBuild is set, then the server will return the actual latest general release
	// if it is not set, then the server will return the general release 'notify' build which
	// may or may not be the actual latest release

	if (!gVaLicensingHost)
		return FALSE;

	if (gVaLicensingHost->IsNonPerpetualLicenseInstalled())
	{
		CString expDate(gVaLicensingHost->GetLicenseExpirationDate());
		if (::HasDatePassed(expDate))
		{
			_ASSERTE(!"why is expired non-perpetual license checking for current build numbers??");
			return FALSE;
		}
	}

	DWORD chkSum;
	HashFile(VaDirs::GetDllDir() + IDS_VAX_DLLW, &chkSum);

	CString url, tmp;
	// Pass the current version and support in case we decide to do some server logic with it.
	// IDE stats report uses this url for hits.
	// unrecognized chkSum is listed as unknown build.
	CString__FormatA(url,
	                 "https://www.wholetomato.com/downloads/GetLatestVersions.asp?api=2&v=%d&vs=%d&e=%s&c=%.8lx&b=%c",
	                 mThisBuild, mThisBuildSpecial, (LPCTSTR)mSupportExp, chkSum, getRealLatestBuild ? 'y' : 'n');

	url += gVaLicensingHost->IsNonRenewableLicenseInstalled()   ? "&r=n"
	       : gVaLicensingHost->IsNonPerpetualLicenseInstalled() ? "r=np"
	                                                            : "&r=y";
	url += PV_GetIdeVerStr();

	FileVersionInfo fvi; // Log IDE build number. [case=39942]
	if (fvi.QueryFile(L"DevEnv.exe", FALSE))
		url += CString("&vsbld=") + ::itos(fvi.GetFileVerLSHi()).c_str();

	const GEOID gid = GetUserGeoID(GEOCLASS_NATION);
	if (GEOID_NOT_AVAILABLE != gid)
	{
		CString__FormatA(tmp, "&gr=%lx", gid);
		url += tmp;
	}
	CString__FormatA(tmp, "&cp=%u", GetACP());
	url += tmp;
	CString__FormatA(tmp, "&sl=%x", GetSystemDefaultLangID());
	url += tmp;

	if (!mSupportExp.GetLength())
	{
#if !defined(VA_CPPUNIT)
		// Trial
		// Pass &t=DAYSINSTALLED-DAYSLEFT
		CString armDays;
		CString__FormatA(armDays, "%d-%d", gVaLicensingHost->GetArmDaysInstalled(), gVaLicensingHost->GetArmDaysLeft());
		url += CString("&t=") + armDays;
#endif // VA_CPPUNIT

		WTString userName(gVaLicensingHost->GetLicenseInfoUser());
		if (userName.GetLength())
		{
			// If there is a user string, good chance it is a hack, pass it as an arg
			url += "&u=";
			for (int i = 0; i < 50 && i < userName.GetLength(); i++)
			{
				if (ISCSYM(userName[i]))
					url += userName[i];
				else
					url += '.'; // don't let any funky chars mess with the url
			}
		}
	}

	const int lastDevLang = (int)GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "DevLang", (DWORD)-1);
	if (-1 != lastDevLang)
	{
		CString dlStr;
		CString__FormatA(dlStr, "&l=%d", lastDevLang);
		url += dlStr;
	}

	LogVersionInfo li(false, true);
	CString__FormatA(tmp, "&wv=%d-%d", ::GetWinVersion(), LogVersionInfo::IsX64());
	url += tmp;

#define TRANSFER_SIZE 40

	if (!InternetOkay())
	{
		return FALSE;
	}

	bool bTransferSuccess = false;

	// First we must check the remote configuration file to see if an update is necessary
	CStringW url_s(url);
	HINTERNET hSession = GetSession(url_s);
	if (!hSession)
	{
		return FALSE;
	}

	BYTE pBuf[TRANSFER_SIZE + 1];
	memset(pBuf, NULL, sizeof(pBuf));
	bTransferSuccess = DownloadConfig(hSession, pBuf, TRANSFER_SIZE);
	InternetCloseHandle(hSession);
	if (!bTransferSuccess)
	{
		return FALSE;
	}
	sscanf((char*)pBuf, "%d,%d %d,%d", &curRelease, &curReleaseSpecial, &curBeta, &curBetaSpecial);

	const int kMinBuild = 1555; // autocheck introduced after 1555
	const int kMaxBuild = 5000;

	// sanity check
	if (curRelease < kMinBuild || curRelease > kMaxBuild)
	{
		return FALSE;
	}

	// 0 is valid value for curBeta
	if (curBeta && (curBeta < kMinBuild || curBeta > kMaxBuild))
		return FALSE;

	return TRUE;
#endif
}

void CheckForUpdateAutoCB(LPVOID)
{
	WTAutoUpdater upd;
	upd.CheckForUpdate(Psettings->mCheckForLatestBetaVersion, FALSE);
}

void CheckForLatestVersion(BOOL doManualCheck)
{
	if (!doManualCheck && gTestsActive)
		return;

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	if (doManualCheck)
	{
		WTAutoUpdater upd;
		upd.CheckForUpdate(Psettings->mCheckForLatestBetaVersion, TRUE);
	}
	else if (Psettings->mCheckForLatestVersion)
	{
		// Logic so we only check once every N days
		const WTString regPath = GetCheckForUpdateRegPath();
		WTString checkVersionDate =
		    (const char*)GetRegValue(HKEY_CURRENT_USER, regPath.c_str(), _T(CHECK_VERSION_DATE));
		if (checkVersionDate.length())
		{
			int d, m, y;
			sscanf(checkVersionDate.c_str(), "%d/%d/%d", &m, &d, &y);
			try
			{
				if (CTime(y, m, d, 0, 0, 0) > CTime::GetCurrentTime())
					return;
			}
			catch (...)
			{
				// bad args passed to CTime ctor
				// will happen if user sets system date to 3007 instead of 2007
			}
		}

		// save date of next check
		CTime nextChkTime = CTime::GetCurrentTime();
		nextChkTime += CTimeSpan(CHECK_EVERY_N_DAYS, 0, 0, 0); // 7 days
		checkVersionDate = nextChkTime.Format("%m/%d/%Y");
		SetRegValue(HKEY_CURRENT_USER, regPath.c_str(), _T(CHECK_VERSION_DATE), checkVersionDate.c_str());

		new FunctionThread(CheckForUpdateAutoCB, "CheckForUpdateThread", true, true);
	}
#endif // !AVR_STUDIO && !RAD_STUDIO
}

void CheckNavBarNextReminder()
{
	CString nextReminder = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder");
	if (nextReminder == "")
	{
		CString futureDateStr = GetFutureDate(FIRST_REMINDER);
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder", futureDateStr);
	}
}

void ShowNavBarQuestionIfAppropriate()
{
	// current date
	SYSTEMTIME ctm;
	GetSystemTime(&ctm);
	CString curDateStr;
	CString__FormatT(curDateStr, _T("%04d.%02d.%02d"), ctm.wYear, ctm.wMonth, ctm.wDay);

	CString nextReminder = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder");
	if (nextReminder == "")
	{
		// setting up first reminder
		CheckNavBarNextReminder();
		return;
	}

	if (nextReminder == "No")
		return; // user has opted out from notifications

	if (!curDateStr.IsEmpty() && curDateStr.Compare(nextReminder) < 0)
		return; // next reminder's date hasn't been reached yet

	NavigationBarQuestion mainRen(gMainWnd);
	auto id = mainRen.DoModal();

	if (id == IDOK)
	{
		g_IdeSettings->SetEditorOption("C/C++", "ShowNavigationBar", "FALSE");
		g_IdeSettings->SetEditorOption("CSharp", "ShowNavigationBar", "FALSE");
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder", "No");
	}

	if (id == IDCANCEL)
	{
		if (mainRen.m_bAskMeLater)
		{
			CString futureDateStr = GetFutureDate(REMIND_IN_N_DAYS);
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder", futureDateStr);
		}
		else
		{
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "IDENavigationBarReminder", "No");
		}
	}
}
#endif

void CheckIDENavigationBar()
{
	if (gTestsActive)
		return;

	if (!gVaLicensingHost)
		return;

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	if (Psettings->m_noMiniHelp == true)
		return;

	if (!gShellAttr->IsDevenv())
		return;

	if ((g_IdeSettings->GetEditorBoolOption("C/C++", "ShowNavigationBar") && IsCFile(gTypingDevLang)) ||
	    (gTypingDevLang == CS && g_IdeSettings->GetEditorBoolOption("CSharp", "ShowNavigationBar")))
	{
		bool doIt = false;
		const CStringW user(gVaLicensingHost->GetLicenseInfoUser());
		if (!user.IsEmpty() && user.Find(L"Trial License") == -1)
			doIt = true;
		else if (gVaLicensingHost->IsSanctuaryLicenseInstalled())
			doIt = true;

		if (doIt)
			ShowNavBarQuestionIfAppropriate();
	}
#endif // !AVR_STUDIO && !RAD_STUDIO
}
