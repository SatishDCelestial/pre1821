#include "stdafxed.h"
#include "RegKeys.h"
#include "log.h"
#include "LogVersionInfo.h"
#include "resource.h"
#include "FontSettings.h"
#include "BuildDate.h"
#include "Foo.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "Registry.h"
#include "Directories.h"
#include "addin\BuyTryDlg.h"
#include "File.h"
#include "PROJECT.H"
#include "..\WTKeyGen\WTValidate.h"
#include "IVaLicensing.h"
#include "MenuXP\Tools.h"
#include "Sanctuary\ISanctuaryClient.h"
#include "Sanctuary\IVaAuxiliaryDll.h"
#include "Addin\Register.h"
#include "DllNames.h"
#include "FileVerInfo.h"

#pragma warning(disable : 4996)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CString GetVerInfo(const CStringW& dll, bool showProdVer = false);

bool LogVersionInfo::mX64 = false;
bool LogVersionInfo::mForceLoad = true;

LogVersionInfo::LogVersionInfo()
{
	if (mForceLoad)
		CollectInfo();
}

LogVersionInfo::LogVersionInfo(bool all, bool os)
{
	_ASSERTE(!(all && os));
	if (os)
		GetOSInfo();
	else if (all || mForceLoad)
		CollectInfo();
}

CString GetVaVersionInfoLong()
{
	CString tmp;

#if defined(VAX_CODEGRAPH)
	tmp += "CG ";
#endif

#if defined(AVR_STUDIO)
	tmp += "Atmel ";
#endif

	tmp += "built " + GetBuildDate();

#ifdef _DEBUG
	tmp += " DEBUG";
#endif // _DEBUG
	tmp = GetVerInfo(GetMainDllName(), false) + tmp;
	tmp.Replace("  ", " ");
	return tmp;
}

CString GetVaVersionInfo()
{
	CString tmp(GetVaVersionInfoLong());
	const CString kSearchSubStr(_T(".dll file version "));
	int pos = tmp.Find(kSearchSubStr);
	if (pos != -1)
	{
		CString vaVersion(tmp.Mid(pos + kSearchSubStr.GetLength()));
		tmp = _T("Version ") + vaVersion;
	}

	return tmp;
}

void LogVersionInfo::CollectInfo()
{
	mForceLoad = false;
	CString tmp;
	mInfo.Empty();

	if (g_FontSettings)
	{
		// only report license info if fully loaded since sanctuary is not
		// available at this point if logging was enabled via registry
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO) && !defined(NO_ARMADILLO)
		const bool isSanct = gVaLicensingHost ? !!gVaLicensingHost->IsSanctuaryLicenseInstalled() : false;
		WTString user(gVaLicensingHost ? gVaLicensingHost->GetLicenseInfoUser() : L"");
		const CString key(gVaLicensingHost ? gVaLicensingHost->GetLicenseInfoKey() : "");
		if (key.GetLength() && (isSanct || !strchr("79UusS65", key[0])))
		{
			tmp = "License: ";
			if (!isSanct && user.GetLength())
				tmp += user.c_str();
			else
			{
				if (gVaLicensingHost)
				{
					LPCSTR s = gVaLicensingHost->GetLicenseExpirationDate();
					if (s && *s)
					{
#if defined(SANCTUARY)
						if (gAuxDll)
						{
							auto sanctuaryClient = gAuxDll->GetSanctuaryClient();
							if (sanctuaryClient)
							{
								if (sanctuaryClient->HasProductInfo())
								{
									if (sanctuaryClient->IsConcurrentLicenseInstalled())
										tmp += "Concurrent / ";
									else if (sanctuaryClient->IsNamedNetworkLicenseInstalled())
										tmp += "Named Network User / ";
								}

								if (!sanctuaryClient->checkStatus())
								{
									// this shouldn't be possible...
									tmp += "Inactive ";
								}

								if (sanctuaryClient->IsSuperkeyLicenseInstalled())
									tmp += "Superkey";
								else if (sanctuaryClient->IsRenewableLicense())
									tmp += "Standard";
								else if (sanctuaryClient->IsPersonalLicense())
									tmp += "Non-renewable Personal";
								else if (sanctuaryClient->IsAcademicLicense())
								{
									if (sanctuaryClient->IsConcurrentLicenseInstalled() || sanctuaryClient->IsNamedNetworkLicenseInstalled())
										tmp += "Academic";
									else
										tmp += "Non-renewable Academic";
								}
								else
								{
									vLog("ERROR: LVI:CI unknown");
									_ASSERTE(!"unknown sanctuary license type");
								}

								tmp += " (";
								if (user.GetLength())
								{
									tmp += user.c_str();

									if (!sanctuaryClient->IsSuperkeyLicenseInstalled())
										tmp += " / ";
								}

								if (!sanctuaryClient->IsSuperkeyLicenseInstalled())
									tmp += key;

								tmp += ") ";
							}
						}
#endif
						tmp += "Support ends ";
						tmp += s;
					}
				}
			}

#if !defined(VAX_CODEGRAPH)
			int license = gVaLicensingHost->GetLicenseStatus();
			if (LicenseStatus_Invalid == license && gVaLicensingHost->GetLicenseUserCount() > 0)
				license = LicenseStatus_Expired;

			if (LicenseStatus_Invalid == license)
				tmp += _T("\nTrial mode"); // ??
			else if (LicenseStatus_Expired == license)
				tmp += _T("\nTrial extension mode");
			else if (LicenseStatus_NoFloatingSeats == license)
				tmp += _T("\nTrial extension mode"); // not possible...
			else if (LicenseStatus_SanctuaryError == license)
				tmp += _T("\nTrial extension mode"); // not possible...
			else if (LicenseStatus_SanctuaryElcError == license)
				tmp += _T("\nTrial extension mode"); // not possible...
#endif
		}
		else
			tmp = _T("License: trial");

		mInfo += tmp;
		mInfo += _T("\n");
#endif
	}
	else
	{
		// force a refresh for next time assuming that VA init has completed by then
		mForceLoad = true;
	}

	mInfo += ::GetVaVersionInfoLong();
	mInfo += _T("\n");
#ifdef AVR_STUDIO
	mInfo += "  AtmelStudio";
	mInfo += _T("\n");
#endif

	GetDevStudioInfo();

	FileVersionInfo fvi;
	fvi.QueryFile(L"Comctl32.dll");
	tmp = fvi.GetFileVerString();
	if (tmp.GetLength())
	{
		CString buf;
		CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
		mInfo += buf;
		mInfo += _T("\n");
	}

	GetOSInfo();
	GetRegionInfo();

	if (g_loggingEnabled)
		MyLog(mInfo);
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "AboutInfo", mInfo);

#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO)
	if (g_loggingEnabled && gVaLicensingHost)
	{
		DWORD chkSum;
		HashFile(VaDirs::GetDllDir() + IDS_VAX_DLLW, &chkSum);
		CString__FormatA(mInfo, "TI: %d-%d %d %lx", gVaLicensingHost->GetArmDaysInstalled(),
		                 gVaLicensingHost->GetArmDaysLeft(), gVaLicensingHost->IsArmExpired(), chkSum);
		MyLog(mInfo);
	}
#endif

	mInfo.Empty();
}

CString GetVerInfo(const CStringW& dll, bool showProdVer /* = false */)
{
	CString retval;
	TCHAR strBuf[MAX_PATH];
	CString buf;
	DWORD infoSize, tmp;
	LPVOID pBlock = NULL, pVerInfo = NULL;
	HINSTANCE hMod = GetModuleHandleW(dll);
	bool doComment = false;

	if (!hMod)
	{
		buf.FormatMessage(IDS_VER_ERROR_NOT_LOADED, (const TCHAR*)CString(dll));
		goto BAILOUT_VER;
	}

	GetModuleFileNameA(hMod, strBuf, MAX_PATH);
	infoSize = GetFileVersionInfoSizeA(strBuf, &tmp);
	buf = strBuf;

	if (!infoSize)
	{
		buf.FormatMessage(IDS_VER_NONE, (const TCHAR*)CString(dll), 1);
		goto BAILOUT_VER;
	}

	pBlock = calloc(infoSize, sizeof(DWORD));
	if (!GetFileVersionInfoA(strBuf, 0, infoSize, pBlock))
	{
		buf.FormatMessage(IDS_VER_NONE, (const TCHAR*)CString(dll), 2);
		goto BAILOUT_VER;
	}

	if (!VerQueryValue(pBlock, _T("\\"), &pVerInfo, (UINT*)&infoSize))
	{
		buf.FormatMessage(IDS_VER_NONE, (const TCHAR*)CString(dll), 3);
		goto BAILOUT_VER;
	}

	if (showProdVer)
	{
		CString__FormatA(buf, "%s ", _T(IDS_APPNAME));
		retval += buf;
		buf.FormatMessage(IDS_VER_SPEC2, HIWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwProductVersionMS),
		                  LOWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwProductVersionMS),
		                  HIWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwProductVersionLS),
		                  LOWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwProductVersionLS));
		retval += buf;
		retval += _T("\n");
		buf.Empty();
	}

	buf.FormatMessage(
	    IDS_VER_SPEC_FILE, (const TCHAR*)CString(dll), HIWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionMS),
	    LOWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionMS), HIWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionLS),
	    LOWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionLS));

	doComment = true;

BAILOUT_VER:
	retval += buf;

	// Get comments - need translation info first
	if (doComment && VerQueryValue(pBlock, _T("\\VarFileInfo\\Translation"), &pVerInfo, (UINT*)&infoSize) &&
	    infoSize >= 4)
	{
		// To get a string value must pass query in the form
		//    "\StringFileInfo\<langID><codepage>\keyname"
		// where <lang-codepage> is the languageID concatenated with the code page, in hex.
		CString query;
		CString__FormatA(query, _T("\\StringFileInfo\\%04x%04x\\%s"),
		                 LOWORD(*(DWORD*)pVerInfo), // langID
		                 HIWORD(*(DWORD*)pVerInfo), // charset
		                 _T("Comments"));

		LPCTSTR pVal;
		if (VerQueryValue(pBlock, (LPTSTR)(LPCTSTR)query, (LPVOID*)&pVal, (UINT*)&infoSize))
			retval += pVal;
	}

	if (pBlock)
		free(pBlock);

	return retval;
}

CString
LogVersionInfo::GetWinDisplayVersion()
{
	return GetRegValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", "");
}

DWORD
LogVersionInfo::GetOSInfo()
{
	CString tmp;
	// update at some point:
	// http://stackoverflow.com/questions/22303824/warning-c4996-getversionexw-was-declared-deprecated
	OSVERSIONINFOEXW os; // supported on Win2000 and higher

	os.dwOSVersionInfoSize = sizeof(os);
	GetVersionExW((OSVERSIONINFOW*)&os);

	// Windows 11 support
	// [case: 148255]
	// see https://docs.microsoft.com/en-us/windows/release-health/windows11-release-information
	if (os.dwBuildNumber >= 22000)
		os.dwMajorVersion = 11;

	if (os.dwPlatformId == VER_PLATFORM_WIN32_NT)
	{
		if (4 == os.dwMajorVersion)
			mInfo += _T("WindowsNT 4");
		else if (5 == os.dwMajorVersion)
		{
			if (os.dwMinorVersion == 0L)
				mInfo += _T("Windows 2000");
			else if (os.dwMinorVersion == 1L)
				mInfo += _T("Windows XP");
			else if (os.dwMinorVersion == 2L)
			{
				if (os.wProductType == VER_NT_WORKSTATION)
					mInfo += _T("Windows XP 2003");
				else
					mInfo += _T("Windows Server 2003");
			}
			else
				mInfo += _T("Windows XP");
		}
		else if (6 == os.dwMajorVersion)
		{
			if (4 == os.dwMinorVersion)
			{
				if (os.wProductType == VER_NT_WORKSTATION)
					mInfo += _T("Windows 10");
				else
					mInfo += _T("Windows Server 10");
			}
			else if (3 == os.dwMinorVersion)
			{
				// http://msdn.microsoft.com/en-us/library/windows/desktop/ms724834%28v=vs.85%29.aspx/html
				if (os.wProductType == VER_NT_WORKSTATION)
					mInfo += _T("Windows 8.1");
				else
					mInfo += _T("Windows Server 2012 R2");
			}
			else if (2 == os.dwMinorVersion)
			{
				if (os.dwBuildNumber > 0x000023f0)
				{
					// windows 8.1 preview lies - this does not work - it reports itself as windows 8 9200
					if (os.wProductType == VER_NT_WORKSTATION)
						mInfo += _T("Windows 8.1");
					else
						mInfo += _T("Windows Server 2012 R2");
				}
				else
				{
					if (os.wProductType == VER_NT_WORKSTATION)
						mInfo += _T("Windows 8");
					else
						mInfo += _T("Windows Server 2012");
				}
			}
			else if (1 == os.dwMinorVersion)
			{
				if (os.wProductType == VER_NT_WORKSTATION)
					mInfo += _T("Windows 7");
				else
					mInfo += _T("Windows Server 2008 R2");
			}
			else if (0 == os.dwMinorVersion)
			{
				if (os.wProductType == VER_NT_WORKSTATION)
					mInfo += _T("Windows Vista");
				else
					mInfo += _T("Windows Server 2008");
			}
			else
				mInfo += _T("Windows");
		}
		else if (10 == os.dwMajorVersion)
		{
			if (os.wProductType == VER_NT_WORKSTATION)
				mInfo += _T("Windows 10");
			else
				mInfo += _T("Windows Server 10");
		}
		else if (11 == os.dwMajorVersion) // [case: 148255]
		{
			if (os.wProductType == VER_NT_WORKSTATION)
				mInfo += _T("Windows 11");
			else
				mInfo += _T("Windows Server 11");
		}
		else
			mInfo += _T("WindowsNT");
	}
	else if (os.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
	{
		mInfo += _T("Windows");
		if (os.dwMajorVersion == 4L && os.dwMinorVersion == 10)
			mInfo += _T("98");
		else if (os.dwMajorVersion == 4L && os.dwMinorVersion == 90L)
			mInfo += _T("ME");
		else
			mInfo += _T("95");
	}
	else
		mInfo += _T("Unknown Platform");

	DWORD releaseId = ::GetWinReleaseId();
	CString__FormatA(tmp, _T(" %ld.%ld "), os.dwMajorVersion, os.dwMinorVersion);
	mInfo += tmp;
	if (releaseId > 0)
	{
		if (releaseId < 2009 && os.dwMajorVersion <= 10)
		{
			CString__FormatA(tmp, _T("%ld "), releaseId);
			mInfo += tmp;
		}
		else
		{
			// [case: 148255]
			// see https://ss64.com/nt/ver.html#:~:text=The%20Release%20ID%20is%20a,.%20for%201st%2C%202nd%20releases.
			mInfo += GetWinDisplayVersion();
			mInfo += " ";
		}
	}
	mInfo += _T("Build ");

	if (os.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		CString__FormatA(tmp, _T("%d"), LOWORD(os.dwBuildNumber));
	else
		CString__FormatA(tmp, _T("%ld"), os.dwBuildNumber);

	mInfo += tmp;

	if ((os.dwPlatformId == VER_PLATFORM_WIN32_NT && os.dwMajorVersion == 6 && os.dwMinorVersion == 4) ||
	    (os.dwPlatformId == VER_PLATFORM_WIN32_NT && os.dwMajorVersion == 10))
	{
		// add the windows 10 update build revision
		DWORD ubr =
		    GetRegDword(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR", (DWORD)-1);

		if (ubr != -1)
		{
			CString__FormatA(tmp, ".%ld", ubr);
			mInfo += tmp;
		}
	}

	if (os.szCSDVersion)
	{
		tmp = L" " + CStringW(os.szCSDVersion);
		mInfo += tmp;
	}

	const bool isRemote = ::GetSystemMetrics(SM_REMOTESESSION) != 0;
	if (isRemote)
		mInfo += _T(" (remote)");

	mInfo += _T("\n");

	if (os.dwPlatformId == VER_PLATFORM_WIN32_NT)
	{
		SYSTEM_INFO sysInf;
		memset(&sysInf, 0, sizeof(sysInf));
		typedef void(WINAPI * GetSystemInfoFUNC)(LPSYSTEM_INFO lpSystemInfo);
		GetSystemInfoFUNC myGetSystemInfo = (GetSystemInfoFUNC)(uintptr_t)GetProcAddress(
		    GetModuleHandleW(L"kernel32.dll"), "GetNativeSystemInfo");
		if (!myGetSystemInfo)
			myGetSystemInfo = ::GetSystemInfo;
		myGetSystemInfo(&sysInf);

		typedef BOOL(WINAPI * IsWow64ProcessFUNC)(HANDLE hProcess, PBOOL Wow64Process);
		IsWow64ProcessFUNC myIsWow64Process = (IsWow64ProcessFUNC)(uintptr_t)GetProcAddress(
		    GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
		BOOL wow64 = false;
		if (myIsWow64Process)
		{
			if (!myIsWow64Process(::GetCurrentProcess(), &wow64))
				wow64 = false;
			if (wow64)
				mX64 = true;
		}

		CString procarch;
		switch (sysInf.wProcessorArchitecture)
		{
		case PROCESSOR_ARCHITECTURE_AMD64:
			CString__FormatA(procarch, _T(" (x86-64%s)"), wow64 ? _T(", WOW64") : _T(""));
			break;
		case PROCESSOR_ARCHITECTURE_IA64:
			CString__FormatA(procarch, _T(" (IA64%s)"), wow64 ? _T(", WOW64") : _T(""));
			break;
		case PROCESSOR_ARCHITECTURE_INTEL:
			CString__FormatA(procarch, _T(" (x86%s)"), wow64 ? _T(", WOW64") : _T(""));
			break;
		case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64:
			procarch = _T(" (x86 on win64)");
			break;
		case PROCESSOR_ARCHITECTURE_ARM64:
			procarch = _T(" (arm64)");
			break;
		case PROCESSOR_ARCHITECTURE_UNKNOWN:
			procarch = _T("");
			break;
		}

		if (sysInf.dwNumberOfProcessors == 1)
			tmp = "Single processor";
		else
			CString__FormatA(tmp, _T("%ld processors"), sysInf.dwNumberOfProcessors);
		mInfo += tmp;
		mInfo += procarch;
		mInfo += _T("\n");
	}

	return os.dwPlatformId;
}

void LogVersionInfo::GetDevStudioInfo()
{
#if !defined(RAD_STUDIO)
	CString tmp, buf;
	FileVersionInfo fvi;

	if (fvi.QueryFile(L"DevEnv.exe", FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;

			if (gDte)
			{
				CComBSTR ed;
				if (SUCCEEDED(gDte->get_Edition(&ed)))
				{
					if (ed.Length() > 1)
					{
						mInfo += " ";
						mInfo += CString(ed);
					}
				}
			}
			mInfo += _T("\n");
		}
	}
	if (fvi.QueryFile(__T(IDS_MSDEV_EXE), FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;
			mInfo += _T("\n");
		}
	}
	if (fvi.QueryFile(_T(IDS_EVC_EXE), FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;
			mInfo += _T("\n");
		}
	}
	if (fvi.QueryFile(_T(IDS_DEVSHL_DLL), FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;
			mInfo += _T("\n");
		}
	}
	if (fvi.QueryFile(_T(IDS_DEVEDIT_PKG), FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;
			mInfo += _T("\n");
		}
	}
	if (fvi.QueryFile(L"msenv.dll", FALSE))
	{
		tmp = fvi.GetFileVerString();
		if (tmp.GetLength())
		{
			CString__FormatA(buf, "%s version %s", (const char*)fvi.GetModuleName(), (const char*)tmp);
			mInfo += buf;
			mInfo += _T("\n");
		}
	}
#endif

	// Show current font and size
	if (Psettings && g_FontSettings)
	{
		LOGFONTW lf;
		try
		{
			WTString fontInfo = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "FontInfo");
			if (g_FontSettings->m_TxtFont.m_hObject)
			{
				g_FontSettings->m_TxtFont.GetLogFont(&lf);
				fontInfo.WTFormat("Font: %s %ld (pixels)", (const char*)CString(lf.lfFaceName), -lf.lfHeight);
			}

			if (fontInfo.GetLength())
			{
				mInfo += fontInfo.c_str();
				mInfo += _T("\n");
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "FontInfo", fontInfo);
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("ABD:");
		}
	}
}

void LogVersionInfo::GetRegionInfo()
{
	CString tmp;

	// acp: http://msdn.microsoft.com/en-us/library/dd317756%28v=VS.85%29.aspx
	// langid: http://msdn.microsoft.com/en-us/library/dd318693%28v=VS.85%29.aspx
	CString__FormatA(tmp, "Language info: %u, 0x%x", GetACP(), GetSystemDefaultLangID());
	mInfo += tmp;

	if (g_loggingEnabled)
	{
		const GEOID gid = GetUserGeoID(GEOCLASS_NATION);
		if (GEOID_NOT_AVAILABLE != gid)
		{
			// geo: http://msdn.microsoft.com/en-us/library/dd374073%28v=VS.85%29.aspx
			CString__FormatA(tmp, ", 0x%lx", gid);
			mInfo += tmp;
		}
	}

	mInfo += _T("\n");
}
