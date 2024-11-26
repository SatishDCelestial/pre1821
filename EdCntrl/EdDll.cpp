// EdDll.cpp : Defines the initialization routines for the DLL.
//

#include "stdafxed.h"
#include "EdDll.h"
#include "WTString.h"
#include "Directories.h"
#if !defined(VA_CPPUNIT)
#include "RegKeys.h"
#include <shlwapi.h>
#include "AddIn/BuyTryDlg.h"
#include "file.h"
#include "LicenseMonitor.h"
#include "DevShellAttributes.h"
#include "log.h"
#include "token.h"
#include "AutotextManager.h"
#include "Registry.h"

#include "VASeException/VASeException.h"
#include "../VaPkg/VaPkg/PkgSetupId.h"
#endif // !VACPP_UNIT
#include "DevShellService.h"
#include "DllNames.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if !defined(_WIN64)
static void UnRegisterDSAddin(LPCSTR addinKeyPath);
static void RegisterDSAddin(LPCSTR addinKeyPath);
static void DeleteFontsAndColorsCache(CString vers);
static const char* const pkgVerStr7 = "PackageVersion";
static const char* const pkgVerStr8 = "PackageVersionVC8";
static const char* const pkgVerStr9 = "PackageVersionVC9";
static const char* const pkgVerStr10 = "PackageVersionVC10";
#endif

BOOL RunProcess(LPCWSTR cmdlineIn, WORD showWindowArg /*= SW_HIDE*/)
{
	BOOL rval = TRUE;
	try
	{
		STARTUPINFOW sinfo;
		PROCESS_INFORMATION pinfo;
		const std::unique_ptr<WCHAR[]> cmdLineVec(new WCHAR[2056]);
		WCHAR* mutableCmdLine = &cmdLineVec[0];
		wcscpy(mutableCmdLine, cmdlineIn);

		ZeroMemory(&sinfo, sizeof(STARTUPINFOW));
		ZeroMemory(&pinfo, sizeof(PROCESS_INFORMATION));
		sinfo.cb = sizeof(STARTUPINFOW);
		sinfo.dwFlags = STARTF_USESHOWWINDOW;
		sinfo.wShowWindow = showWindowArg;
		const CStringW dllDir = VaDirs::GetDllDir();

		if (CreateProcessW(NULL, mutableCmdLine, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | HIGH_PRIORITY_CLASS,
		                   NULL, dllDir, &sinfo, &pinfo))
		{
			DWORD dwResult = WaitForSingleObject(pinfo.hProcess, 60 * 1000);
			if (dwResult == WAIT_TIMEOUT)
				rval = FALSE;
			CloseHandle(pinfo.hThread);
			CloseHandle(pinfo.hProcess);
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("EDL:");
		rval = FALSE;
	}
	return rval;
}

#if defined(VA_CPPUNIT)

void ExitLicense()
{
}

#else

//
//	Note!
//
//		If this DLL is dynamically linked against the MFC
//		DLLs, any functions exported from this DLL which
//		call into MFC must have the AFX_MANAGE_STATE macro
//		added at the very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//

/////////////////////////////////////////////////////////////////////////////
// CEdDllApp

BEGIN_MESSAGE_MAP(CEdDllApp, CWinApp)
//{{AFX_MSG_MAP(CEdDllApp)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEdDllApp construction

CEdDllApp::CEdDllApp() : VaApp(false)
{
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CEdDllApp object

CEdDllApp sVaDllApp;

/////////////////////////////////////////////////////////////////////////////
// CEdDllApp initialization

//#include "Stackwalker.h"

BOOL CEdDllApp::InitInstance()
{
#if !defined(RAD_STUDIO)
	DoInit();
#endif
	return TRUE;
}

void CEdDllApp::DoInit()
{
// 	while(!::IsDebuggerPresent())
// 		::Sleep(10);

#if !defined(RAD_STUDIO)
	// do this before logging starts
	// "altDir" is an old no longer documented reg entry -- for internal use only at this point
	CStringW altDir = GetRegValueW(HKEY_CURRENT_USER, ID_RK_WT_KEY, "altDir");
	if (!altDir.IsEmpty())
	{
		VaDirs::UseAlternateDir(altDir);
		// auto-delete altDir value
		SetRegValue(HKEY_CURRENT_USER, ID_RK_WT_KEY, "altDir", "");
	}
	else
	{
		// "UserDataDir" :  https://docs.wholetomato.com/?W332
		// see also https://docs.wholetomato.com/default.asp?W689
		altDir = GetRegValueW(HKEY_CURRENT_USER, ID_RK_WT_KEY, "UserDataDir");
		if (!altDir.IsEmpty())
			VaDirs::UseAlternateDir(altDir);
	}
#endif

	// no logging before VaApp::Start()
	//	OnlyInstallUnhandeldExceptionFilter();
	VaApp::Start();
	VASetSeTranslator();
	VALOGMETHOD("InitInst:");
}

void ExitLicense()
{
	StopLicenseMonitor();
}

int CEdDllApp::ExitInstance()
{
#if !defined(RAD_STUDIO)
	DoExit();
#endif
	return TRUE;
}

void CEdDllApp::DoExit()
{
	VALOGMETHOD("ExitInst:");
	Log("ExitInstance EdDll unloading\n");
	VaApp::Exit();
}

int CEdDllApp::DoMessageBox(LPCTSTR lpszPrompt, UINT nType, UINT nIDPrompt)
{
	if (!lpszPrompt || !*lpszPrompt)
		lpszPrompt = "An error has occurred.  Please report the problem to http://www.wholetomato.com/contact";
	CString logMsg;
	CString__FormatA(logMsg, "AfxMessageBox: %s %x %x", lpszPrompt, nType, nIDPrompt);
	VALOGERROR(logMsg);
	static int sCnt = 0;
	if (sCnt++ < 5 || (MB_OK != nType && (MB_OK | MB_ICONERROR) != nType && (MB_OK | MB_ICONWARNING) != nType &&
	                   (MB_OK | MB_SYSTEMMODAL) != nType && (MB_OK | MB_ICONERROR | MB_SYSTEMMODAL) != nType &&
	                   (MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL) != nType))
	{
		return __super::DoMessageBox(lpszPrompt, nType, nIDPrompt);
	}
	return MB_OK;
}

#if !defined(_WIN64)
#if !defined(RAD_STUDIO)
#define ADDINREGNAME "VisualAssist.DSAddin.1"
static void RegisterDSAddin(LPCSTR addinKeyPath)
{
	CString addinKey = CString(addinKeyPath) + "\\AddIns\\" + ADDINREGNAME;
	CString textKey = CString(addinKeyPath) + "\\Text Editor";
	DWORD enable = FALSE;
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_AUTOCOMMENTS, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_PARAMINFO, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_AUTOCOMPLETE, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_QUICKINFO, enable);

	SetRegValue(HKEY_CURRENT_USER, addinKey, NULL, "1");
	SetRegValue(HKEY_CURRENT_USER, addinKey, "Description", "Visual Assist");
	SetRegValue(HKEY_CURRENT_USER, addinKey, "DisplayName", "Visual Assist");
	SetRegValue(HKEY_CURRENT_USER, addinKey, "FileName", VaDirs::GetDllDir() + L"VAssist.dll");
}

static void RegisterVSNETAddin(LPCTSTR ver, LPCTSTR comCls = "VAssistNET.Connect")
{
	const CString addinKey = CString("Software\\Microsoft\\VisualStudio\\") + ver + CString("\\Addins\\") + comCls;
	SHDeleteKey(HKEY_CURRENT_USER, addinKey);
	// Using HKLM only to register addin, registering both in 2005 causes it to get confused
	SetRegValue(HKEY_LOCAL_MACHINE, addinKey, "CommandLineSafe", (DWORD)0);
	SetRegValue(HKEY_LOCAL_MACHINE, addinKey, "CommandPreload", (DWORD)0);
	SetRegValue(HKEY_LOCAL_MACHINE, addinKey, "Description", "Visual Assist");
	SetRegValue(HKEY_LOCAL_MACHINE, addinKey, "LoadBehavior", (DWORD)1);
	SetRegValue(HKEY_LOCAL_MACHINE, addinKey, "FriendlyName", "Visual Assist");
}

static void UnRegisterDSAddin(LPCSTR addinKeyPath)
{
	CString addinReg = CString(addinKeyPath) + "\\AddIns\\" + ADDINREGNAME;
	SHDeleteKey(HKEY_CURRENT_USER, addinReg);
	SHDeleteKey(HKEY_LOCAL_MACHINE, addinReg);

	CString textKey = CString(addinKeyPath) + "\\" + "Text Editor";
	DWORD enable = TRUE;
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_AUTOCOMMENTS, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_PARAMINFO, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_AUTOCOMPLETE, enable);
	SetRegValue(HKEY_CURRENT_USER, textKey, ID_RK_QUICKINFO, enable);
}
#endif
#endif

#if !defined(RAD_STUDIO)
STDAPI DllRegisterServer(void)
{
#if defined(AVR_STUDIO) || defined(_WIN64)
	return TRUE;
#else

	VALOGMETHOD("DllReg:");

	VaDirs::RemoveOldDbDirs();

	RegisterDSAddin("Software\\Microsoft\\DevStudio\\5.0");
	RegisterDSAddin("Software\\Microsoft\\DevStudio\\6.0");
	RegisterDSAddin("Software\\Microsoft\\CEStudio\\3.0");
	RegisterDSAddin("Software\\Microsoft\\CEStudio\\4.0");
	RegisterDSAddin("Software\\Microsoft\\Platform Builder\\4.00");

	RegisterVSNETAddin("7.0");
	RegisterVSNETAddin("7.1");
	RegisterVSNETAddin("8.0", "VAssistNET.Connect8");
	RegisterVSNETAddin("9.0", "VAssistNET.Connect9");
	//	RegisterVSNETAddin("10.0", "VAssistNET.Connect10");

	RegisterVSNETAddin("7.0Exp");
	RegisterVSNETAddin("7.1Exp");
	RegisterVSNETAddin("8.0Exp", "VAssistNET.Connect8");
	RegisterVSNETAddin("9.0Exp", "VAssistNET.Connect9");
	//	RegisterVSNETAddin("10.0Exp", "VAssistNET.Connect10");

	bool isUnattendedInstall = !!GetRegDword(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, "VA_X_Setup_UnattendedInstall", FALSE);

	// test and reset vc6 toolbars
	{
		WTString regMsg = "Do you want a new toolbar for Visual Assist in Microsoft Visual C++? Press Yes if you are "
		                  "missing a toolbar or want the latest version. If Yes, you have also the option to assign "
		                  "shortcuts for Visual Assist the next time you start the IDE. None of the assignments "
		                  "conflict with default assignments and none of your other custom assignments are lost.";
#define TBVER "No9"
		if (GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "CreateToolbarVC6") == TBVER ||
		    GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "CreateToolbarEVC") == TBVER)
		{
			// reinstalling same version
			if (isUnattendedInstall ||
			    WtMessageBox(NULL, regMsg, "Rebuild Toolbar in Microsoft Visual C++ 6.0?", MB_YESNO) == IDYES)
			{
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "CreateMenu", "Yes");
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "CreateToolbarVC6", "Yes");
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "CreateToolbarEVC", "Yes");
			}
		}
	}

	// register dll's
	const CStringW dllDir = VaDirs::GetDllDir();
	CStringW cmd;
	CString__FormatW(cmd, L"RegSvr32.exe /s \"%sVAssist.dll\"", (LPCWSTR)dllDir);
	RunProcess(cmd);
	const CStringW vsnetpath70 =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\7.0", "InstallDir");
	const CStringW vsnetpath71 =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\7.1", "InstallDir");
	const CStringW vsnetpath80 =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\8.0", "InstallDir");
	const CStringW vsnetpath90 =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\9.0", "InstallDir");
	const CStringW vsnetpath100 =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\10.0", "InstallDir");

	if (vsnetpath80.GetLength())
	{
		// unregister old VAssistNet8.dll
		CString__FormatW(cmd, L"RegSvr32.exe /s /u \"%sVAssistNET8.dll\"", (LPCWSTR)dllDir);
		RunProcess(cmd);
#define VSADDIN8_Old "Software\\Microsoft\\VisualStudio\\8.0\\Addins\\VAssistNET.Connect"
#define VSADDIN8Exp_Old "Software\\Microsoft\\VisualStudio\\8.0Exp\\Addins\\VAssistNET.Connect"

		SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN8_Old);
		SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN8Exp_Old);
	}

	// leave manifest file in place, per case:1542

	const WTString vsip = _tgetenv("ENVSDK_REGKEY");

	if (vsnetpath70.GetLength() || vsnetpath71.GetLength())
	{
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%s%s\"", (LPCWSTR)dllDir, IDS_VASSISTNET_DLLW);
		RunProcess(cmd);
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%sVaPkg.dll\"", (LPCWSTR)dllDir);
		RunProcess(cmd);

		const WTString regMsg = "This version of Visual Assist includes changes to its menus and toolbars.\n\n"
		                        "Registering these changes with Microsoft Visual Studio .NET 2003 will cause all IDE "
		                        "menus and toolbars to reset to their default state. If you have custom modifications, "
		                        "you may skip this step, but you may not have access to the latest features.\n\n"
		                        "Would you like to update the Visual Assist menu and toolbar?";

		WTString pkgVer7 = (const char*)GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr7);
		if (pkgVer7.IsEmpty())
			pkgVer7 = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr7);
		SetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr7, PKGBLDID);

		if (pkgVer7 == "" ||
		    (pkgVer7 != PKGBLDID && (isUnattendedInstall || WtMessageBox(NULL, regMsg, "Update Menus and Toolbars?",
		                                                                 MB_YESNO | MB_DEFBUTTON2) == IDYES)))
		{
			if (vsnetpath70.GetLength())
			{
				SHDeleteKey(
				    HKEY_LOCAL_MACHINE,
				    "SOFTWARE\\Microsoft\\VisualStudio\\7.0\\InstalledProducts\\Visual Assist X"); // keep old name
				CStringW regcmd(CStringW(L"\"") + vsnetpath70 + L"DevEnv.exe\" /setup /RootSuffix");
				RunProcess(regcmd);
				if (vsip.contains("7.0"))
				{
					SHDeleteKey(
					    HKEY_LOCAL_MACHINE,
					    "SOFTWARE\\Microsoft\\VisualStudio\\7.0Exp\\InstalledProducts\\Visual Assist X"); // keep old
					                                                                                      // name
					CStringW regcmd2(CStringW(L"\"") + vsnetpath70 + L"DevEnv.exe\" /setup /RootSuffix Exp");
					RunProcess(regcmd2);
				}
			}
			if (vsnetpath71.GetLength())
			{
				SHDeleteKey(
				    HKEY_LOCAL_MACHINE,
				    "SOFTWARE\\Microsoft\\VisualStudio\\7.1\\InstalledProducts\\Visual Assist X"); // keep old name
				CStringW regcmd(CStringW(L"\"") + vsnetpath71 + L"DevEnv.exe\" /setup /RootSuffix");
				RunProcess(regcmd);
				if (vsip.contains("7.1"))
				{
					SHDeleteKey(
					    HKEY_LOCAL_MACHINE,
					    "SOFTWARE\\Microsoft\\VisualStudio\\7.1Exp\\InstalledProducts\\Visual Assist X"); // keep old
					                                                                                      // name
					CStringW regcmd2(CStringW(L"\"") + vsnetpath71 + L"DevEnv.exe\" /setup /RootSuffix Exp");
					RunProcess(regcmd2);
				}
			}
		}
	}

	if (vsnetpath80.GetLength())
	{
		// Install vsip package
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%s%s\"", (LPCWSTR)dllDir, IDS_VASSISTNET_DLLW);
		RunProcess(cmd);
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%sVaPkg.dll\"", (LPCWSTR)dllDir);
		RunProcess(cmd);

		WTString pkgVer8 = (const char*)GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr8);
		if (pkgVer8.IsEmpty())
			pkgVer8 = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr8);
		SetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr8, PKGBLDID);
		DeleteFontsAndColorsCache("8.0");

		if (pkgVer8 != PKGBLDID)
		{
			SHDeleteKey(HKEY_LOCAL_MACHINE,
			            "SOFTWARE\\Microsoft\\VisualStudio\\8.0\\InstalledProducts\\Visual Assist X"); // keep old name
			if (vsip.contains("8.0"))
			{
				SHDeleteKey(
				    HKEY_LOCAL_MACHINE,
				    "SOFTWARE\\Microsoft\\VisualStudio\\8.0Exp\\InstalledProducts\\Visual Assist X"); // keep old name
				CStringW regcmd(CStringW(L"\"") + vsnetpath80 + L"DevEnv.exe\" /setup /RootSuffix Exp");
				RunProcess(regcmd);
			}
			CStringW regcmd(CStringW(L"\"") + vsnetpath80 + L"DevEnv.exe\" /setup /RootSuffix");
			RunProcess(regcmd);
		}
	}

	if (vsnetpath90.GetLength())
	{
		// Install vsip package
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%s%s\"", (LPCWSTR)dllDir, IDS_VASSISTNET_DLLW);
		RunProcess(cmd);
		CString__FormatW(cmd, L"RegSvr32.exe /s \"%sVaPkg.dll\"", (LPCWSTR)dllDir);
		RunProcess(cmd);

		WTString pkgVer9 = (const char*)GetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr9);
		if (pkgVer9.IsEmpty())
			pkgVer9 = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr9);
		SetRegValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr9, PKGBLDID);
		DeleteFontsAndColorsCache("9.0");

		if (pkgVer9 != PKGBLDID)
		{
			SHDeleteKey(HKEY_LOCAL_MACHINE,
			            "SOFTWARE\\Microsoft\\VisualStudio\\9.0\\InstalledProducts\\Visual Assist X"); // keep old name
			// new vs2008 beta 2 command line param for setup: /nosetupvstemplates
			// http://blogs.msdn.com/aaronmar/archive/2007/07/19/devenv-setup-performance.aspx
			if (vsip.contains("9.0"))
			{
				SHDeleteKey(
				    HKEY_LOCAL_MACHINE,
				    "SOFTWARE\\Microsoft\\VisualStudio\\9.0Exp\\InstalledProducts\\Visual Assist X"); // keep old name
				CStringW regcmd(CStringW(L"\"") + vsnetpath90 +
				                L"DevEnv.exe\" /setup /nosetupvstemplates /RootSuffix Exp");
				RunProcess(regcmd);
			}

			// [case: 31894] cache reg value
			CString valueToRestore =
			    GetRegValue(HKEY_CURRENT_USER, "Software\\Microsoft\\VisualStudio\\9.0\\HTML Editor",
			                "InsertAttrValueQuotesTyping");

			CStringW regcmd(CStringW(L"\"") + vsnetpath90 + L"DevEnv.exe\" /setup /nosetupvstemplates /RootSuffix");
			RunProcess(regcmd);

			// [case: 31894] restore reg value
			if (valueToRestore.GetLength() > 0)
				SetRegValue(HKEY_CURRENT_USER, "Software\\Microsoft\\VisualStudio\\9.0\\HTML Editor",
				            "InsertAttrValueQuotesTyping", valueToRestore);
		}
	}

	// remove old erroneous VS2010 reg entries, just in case
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Addins\\VAssistNET.Connect10");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0Exp\\Addins\\VAssistNET.Connect10");
	SHDeleteKey(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Addins\\VAssistNET.Connect10");
	SHDeleteKey(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\VisualStudio\\10.0Exp\\Addins\\VAssistNET.Connect10");

	SHDeleteValue(HKEY_LOCAL_MACHINE,
	              "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\AutoLoadPackages\\{ADFC4E64-0397-11D1-9F4E-00A0C911004F}",
	              "{44630D46-96B5-488C-8DF9-26E21DB8C1A3}");
	SHDeleteValue(HKEY_LOCAL_MACHINE,
	              "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\AutoLoadPackages\\{F1536EF8-92EC-443C-9ED7-FDADF150DA82}",
	              "{44630D46-96B5-488C-8DF9-26E21DB8C1A3}");
	SHDeleteValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Menus",
	              "{44630D46-96B5-488C-8DF9-26E21DB8C1A3}");

	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\InstalledProducts\\Visual Assist X"); // keep old name
	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Packages\\{44630D46-96B5-488C-8DF9-26E21DB8C1A3}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{44C8950D-0CA8-43EA-9B82-0760C1D67520}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{552BAC5B-09E7-4996-B965-DBC642645D16}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{565E4AB5-1F86-463C-852E-011F3421F99F}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{B7763794-9CFD-4C82-A236-A6B8D510216E}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{B7A24D49-CAD8-4436-A764-9C47418A237C}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{C6AD9332-2008-4557-AD30-503B64F56E9B}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{DB587E06-DA42-4318-9CC9-F55D704C5E1C}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{FD9BFB5D-438C-4B25-A9A8-FA2774B2FE5E}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{7E0AAF55-AE11-438A-891C-45AEE9767B93}");
	SHDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\Text Editor\\External "
	                                "Markers\\{3FB0E1D0-47D7-4201-947C-72622E26EE5A}");

	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\ToolWindows\\{32A6260A-0121-4878-95E5-80D2F08EA4CB}");
	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\ToolWindows\\{6BF43C00-192E-4279-9DD1-C19A4C0AB983}");
	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\ToolWindows\\{872017FE-B99B-44DF-9F38-F7A581288AA7}");
	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\ToolWindows\\{8E5C1D1A-8A91-4406-AA56-5BF27C97D63C}");
	SHDeleteKey(HKEY_LOCAL_MACHINE,
	            "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\ToolWindows\\{B8E0BB07-1A6D-4198-91D6-C3F40B30470A}");

	if (vsnetpath100.GetLength())
	{
		// No need to register anything here because we use VSIX for registration
	}

	// Migrate custom directories from prev version to 10.1
	{
		WTString prevPath = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_SYSTEMINCLUDE);
		if (prevPath.GetLength())
		{
			WTString newCust71 =
			    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet\\Custom", ID_RK_SYSTEMINCLUDE);
			if (!newCust71.GetLength())
			{
				prevPath = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_SYSTEMINCLUDE);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet\\Custom", ID_RK_SYSTEMINCLUDE, prevPath);

				prevPath =
				    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_ADDITIONALINCLUDE);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet\\Custom", ID_RK_ADDITIONALINCLUDE, prevPath);

				prevPath = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_GOTOSRCDIRS);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet\\Custom", ID_RK_GOTOSRCDIRS, prevPath);
			}
			WTString newCust70 =
			    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet7.0\\Custom", ID_RK_SYSTEMINCLUDE);
			if (!newCust70.GetLength())
			{
				prevPath = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_SYSTEMINCLUDE);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet7.0\\Custom", ID_RK_SYSTEMINCLUDE, prevPath);

				prevPath =
				    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_ADDITIONALINCLUDE);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet7.0\\Custom", ID_RK_ADDITIONALINCLUDE, prevPath);

				prevPath = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_GOTOSRCDIRS);
				SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet7.0\\Custom", ID_RK_GOTOSRCDIRS, prevPath);
			}
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY "\\VANet", ID_RK_SYSTEMINCLUDE, "");
		}
	}

	VALOGMETHOD("DllRegExit:");
	return TRUE;
#endif
}

STDAPI DllUnregisterServer(void)
{
#if defined(AVR_STUDIO) || defined(_WIN64)
	return TRUE;
#else

	UnRegisterDSAddin("Software\\Microsoft\\DevStudio\\5.0");
	UnRegisterDSAddin("Software\\Microsoft\\DevStudio\\6.0");
	UnRegisterDSAddin("Software\\Microsoft\\CEStudio\\3.0");
	UnRegisterDSAddin("Software\\Microsoft\\CEStudio\\4.0");
	UnRegisterDSAddin("Software\\Microsoft\\Platform Builder\\4.00");

#define VSADDIN "Software\\Microsoft\\VisualStudio\\7.0\\Addins\\VAssistNET.Connect"
#define VSADDINExp "Software\\Microsoft\\VisualStudio\\7.0Exp\\Addins\\VAssistNET.Connect"
#define VSADDIN71 "Software\\Microsoft\\VisualStudio\\7.1\\Addins\\VAssistNET.Connect"
#define VSADDIN71Exp "Software\\Microsoft\\VisualStudio\\7.1Exp\\Addins\\VAssistNET.Connect"
#define VSA_ADDIN "Software\\Microsoft\\VSA\\7.0\\Addins\\VAssistNET.Connect"
#define VSA_ADDIN71 "Software\\Microsoft\\VSA\\7.1\\Addins\\VAssistNET.Connect"
#define VSADDIN8 "Software\\Microsoft\\VisualStudio\\8.0\\Addins\\VAssistNET.Connect8"
#define VSADDIN8Exp "Software\\Microsoft\\VisualStudio\\8.0Exp\\Addins\\VAssistNET.Connect8"
#define VSADDIN9 "Software\\Microsoft\\VisualStudio\\9.0\\Addins\\VAssistNET.Connect9"
#define VSADDIN9Exp "Software\\Microsoft\\VisualStudio\\9.0Exp\\Addins\\VAssistNET.Connect9"

	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDINExp);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN71);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN71Exp);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSA_ADDIN);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSA_ADDIN71);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN8);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN8Exp);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN9);
	SHDeleteKey(HKEY_LOCAL_MACHINE, VSADDIN9Exp);

	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDINExp);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN71);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN71Exp);
	SHDeleteKey(HKEY_CURRENT_USER, VSA_ADDIN);
	SHDeleteKey(HKEY_CURRENT_USER, VSA_ADDIN71);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN8);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN8Exp);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN9);
	SHDeleteKey(HKEY_CURRENT_USER, VSADDIN9Exp);

	SHDeleteKey(HKEY_CURRENT_USER, ID_RK_APP_KEY);
	SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, ID_RK_ADDITIONALINCLUDE);
	SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, ID_RK_GOTOSRCDIRS);
	SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, ID_RK_SYSTEMINCLUDE);

	VaDirs::RemoveAllDbDirs();

	// so that install causes /setup
	SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr7);
	SHDeleteValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr7);
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr8))
		DeleteFontsAndColorsCache("8.0");
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr8))
		DeleteFontsAndColorsCache("8.0");
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr9))
		DeleteFontsAndColorsCache("9.0");
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr9))
		DeleteFontsAndColorsCache("9.0");
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_LOCAL_MACHINE, ID_RK_APP_KEY, pkgVerStr10))
		DeleteFontsAndColorsCache("10.0");
	if (ERROR_SUCCESS == SHDeleteValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, pkgVerStr10))
		DeleteFontsAndColorsCache("10.0");

	// unregister dll's
	RunProcess(L"RegSvr32.exe /s /u VAssist.dll");
	RunProcess(L"RegSvr32.exe /s /u " IDS_VASSISTNET_DLLW);
	RunProcess(L"RegSvr32.exe /s /u VAssistNET8.dll");
	RunProcess(L"RegSvr32.exe /s /u VaPkg.dll");

	{
		// remove vsip package from splash screen
		const CStringW vsnetpath70 =
		    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\7.0", "InstallDir");
		const CStringW vsnetpath71 =
		    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\7.1", "InstallDir");
		const CStringW vsnetpath80 =
		    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\8.0", "InstallDir");
		const CStringW vsnetpath90 =
		    GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\VisualStudio\\9.0", "InstallDir");
		if (vsnetpath70.GetLength())
		{
			const CStringW regcmd(CStringW(L"\"") + vsnetpath70 + L"DevEnv.exe\" /setup");
			RunProcess(regcmd);
		}
		if (vsnetpath71.GetLength())
		{
			const CStringW regcmd(CStringW(L"\"") + vsnetpath71 + L"DevEnv.exe\" /setup");
			RunProcess(regcmd);
		}
		if (vsnetpath80.GetLength())
		{
			const CStringW regcmd(CStringW(L"\"") + vsnetpath80 + L"DevEnv.exe\" /setup");
			RunProcess(regcmd);
		}
		if (vsnetpath90.GetLength())
		{
			const CStringW regcmd(CStringW(L"\"") + vsnetpath90 + L"DevEnv.exe\" /setup /nosetupvstemplates");
			RunProcess(regcmd);
		}
	}

	return TRUE;
#endif
}

static void MigrateLMtoCU(LPCTSTR path, LPCTSTR key)
{
	const CString val = GetRegValue(HKEY_LOCAL_MACHINE, path, key);
	if (!val.IsEmpty())
		SetRegValue(HKEY_CURRENT_USER, path, key, val);
}

void MigrateLMtoCU()
{
	if (!GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, ID_RK_USERNAME).GetLength() &&
	    GetRegDword(HKEY_CURRENT_USER, ID_RK_APP_KEY, "VaxMigrated") == 0)
	{
		MigrateLMtoCU(ID_RK_APP_VA, ID_RK_USERKEY);
		MigrateLMtoCU(ID_RK_APP_VA, ID_RK_USERNAME);
		MigrateLMtoCU(ID_RK_APP_VANET, ID_RK_USERKEY);
		MigrateLMtoCU(ID_RK_APP_VANET, ID_RK_USERNAME);
		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_USERKEY);
		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_USERNAME);

		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_TRIALEXPIRATION);
		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_SYSTEMINCLUDE);
		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_ADDITIONALINCLUDE);
		MigrateLMtoCU(ID_RK_APP_KEY, ID_RK_GOTOSRCDIRS);
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "VaxMigrated", 1);
	}
}

void DeleteFontsAndColorsCache(CString vers)
{
	const char* const fmtStr = "Software\\Microsoft\\VisualStudio\\%s\\FontAndColors\\Cache";
	CString cacheKey;

	CString__FormatA(cacheKey, fmtStr, (LPCTSTR)vers);
	SHDeleteKey(HKEY_CURRENT_USER, cacheKey);
	vers += "Exp";
	CString__FormatA(cacheKey, fmtStr, (LPCTSTR)vers);
	SHDeleteKey(HKEY_CURRENT_USER, cacheKey);
}
#endif

#endif // !VA_CPPUNIT
