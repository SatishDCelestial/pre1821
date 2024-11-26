#include "stdafxed.h"
#include <afxwin.h> // MFC core and standard components
#include <atlbase.h>

#include "VaOptions.h"
#include "settings.h"
#include "log.h"
#include "RegKeys.h"
#include "incToken.h"
#include "LogVersionInfo.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "Directories.h"
#include "..\VAOpsWin\VaOpsWinParams.h"
#include "VaService.h"
#include "ScreenAttributes.h"
#include "SolutionFiles.h"
#include "FileTypes.h"
#include "PROJECT.H"
#include "DevShellService.h"
#include "VACompletionSet.h"
#include "ColorSyncManager.h"
#include "IdeSettings.h"
#include "WindowUtils.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "rbuffer.h"
#include "SolutionLoadState.h"
#if defined(SANCTUARY)
#include "Sanctuary/RegisterSanctuary.h"
#else
#include "Addin/Register.h"
#endif
#include "DllNames.h"
#include "Library.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class VaOptionsService : public IVaOptionsService
{
	HWND mOptsHwnd;

  public:
	VaOptionsService() : mOptsHwnd(NULL)
	{
		_ASSERTE(gVaService);
	}

	~VaOptionsService()
	{
	}

	virtual void SetOptionsHwnd(HWND h)
	{
		mOptsHwnd = h;
	}

	virtual void EditSnippets()
	{
		gVaService->Exec(IVaService::ct_global, icmdVaCmd_AutotextEdit);
		FixFocus();
	}

	virtual void RebuildDatabase()
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_OPTREBUILD, 0, 0);
		FixFocus();
	}

	virtual void ClearHistoryAndCacheFiles()
	{
		VaDirs::CleanDbTmpDirs();
		g_ExpHistory->Clear();
		g_rbuffer.Clear();
		FixFocus();
	}

	virtual void CheckForUpdate()
	{
		gVaService->Exec(IVaService::ct_help, icmdVaCmd_HelpCheckForNewVersion);
		FixFocus();
	}

	virtual void EnterLicenseKey()
	{
#if !defined(VA_CPPUNIT)
#if !defined(AVR_STUDIO)
		CWnd opts;
		opts.Attach(mOptsHwnd);

#if defined(SANCTUARY)
		CRegisterSanctuary reg(&opts);
#else
		CRegisterArmadillo reg(&opts);
#endif
		reg.DoModal();
		opts.Detach();
		FixFocus();

		// [case: 142178]
		// sets about info reg key if license changed
		LogVersionInfo info;
#endif
#endif
	}

	virtual void EnableLogging()
	{
		g_loggingEnabled = 0x7;
		::InitLogFile(mOptsHwnd);
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("VAEnableLogging"), 0);
		FixFocus();
	}

	virtual bool IsLoggingEnabled()
	{
		return !!g_loggingEnabled;
	}

	virtual void ResetVc6Toolbar()
	{
#define IDT_SETUPTOOLBAR 605
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "CreateMenu", "Yes");
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "CreateToolbarVC6", "Yes");
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "CreateToolbarEVC", "Yes");
		::SetTimer(gVaMainWnd->GetSafeHwnd(), IDT_SETUPTOOLBAR, 500, NULL);
	}

	virtual void ExploreHistoryDirectory() override
	{
		CStringW dir(VaDirs::GetHistoryDir());
		::ShellExecuteW(mOptsHwnd, L"explore", dir, nullptr, nullptr, SW_SHOWNORMAL);
	}

	virtual COLORREF GetThemeColor(REFGUID colorCategory, LPCWSTR colorName, BOOL foreground)
	{
		if (!g_IdeSettings)
		{
			return UINT_MAX;
		}

		return g_IdeSettings->GetThemeColor(colorCategory, colorName, foreground);
	}

	virtual bool GetEnvironmentFont(LOGFONTW* outFont, bool useFontInfoSize)
	{
		if (!g_IdeSettings)
		{
			return false;
		}

		if (useFontInfoSize)
		{
			return !!g_IdeSettings->GetEnvironmentFontPointInfo(outFont);
		}
		
		return !!g_IdeSettings->GetEnvironmentFont(outFont);
	}

  private:
	void FixFocus()
	{
		_ASSERTE(mOptsHwnd);
		::SetFocus(mOptsHwnd);
	}
};

static void PopulatePlatformList()
{
	IncludeDirs dirs;
	CString keyName(gShellAttr->GetPlatformListKeyName());
	if (keyName.IsEmpty())
	{
		_ASSERTE(gShellAttr->IsDevenv8OrHigher());
		const CString platform(Psettings->m_platformIncludeKey);
		// this assert can fire (safely) if running more than one instance
		_ASSERTE(platform == GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM));
		if (!platform.IsEmpty())
		{
			if (platform == kProjectDefinedPlatform.c_str())
			{
				// [case: 108576] update registry in case another instance changed it
				dirs.UpdateRegistry();
			}
			else
				dirs.SetupPlatform(platform);
		}
		return;
	}

	_ASSERTE(!gShellAttr->IsDevenv8OrHigher());
	CString platList;
	DWORD dwIndex = 0;
	long val = 0;
	HKEY hKey;

	LSTATUS err =
	    RegOpenKeyEx(gShellAttr->GetPlatformListRootKey(), keyName, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &hKey);
	if (ERROR_SUCCESS != err)
	{
		ASSERT(false);
		vLog("DBP::PPL ERROR failed to open VC platforms key %ld(0x%08lx)", err, err);
		goto LEAVE_PLATFORM;
	}

	// enumerate HKCU\Software\Microsoft\DevStudio\x.0\Build System\Components\Platforms
	TCHAR subkeyName[MAX_PATH];
	while ((val = RegEnumKey(hKey, dwIndex, subkeyName, MAX_PATH)) == ERROR_SUCCESS)
	{
		dwIndex++;
		// for each subkey enumerated, see if it has a Directories key
		CRegKey hSubKey;
		CString subKeyDirName(subkeyName); // subkeyName is "Win32 (x86)"
#if !defined(RAD_STUDIO)
		subKeyDirName += _T(IDS_PLATFORM_DIR);
#endif
		err = hSubKey.Open(hKey, subKeyDirName, KEY_READ);
		if (ERROR_SUCCESS == err)
		{
			// see if the dir key has an include value
			DWORD valNameLen = MAX_PATH;
			TCHAR strVal[MAX_PATH] = "";

#if defined(RAD_STUDIO)
			LPCSTR includePathsNames[] = {"IncludePath_Clang32", "IncludePath"};
#else
			LPCSTR includePathsNames[] = {ID_RK_MSDEV_INC_VALUE};
#endif
			for (const auto& it : includePathsNames)
			{
				err = hSubKey.QueryStringValue(it, strVal, &valNameLen);
				if ((ERROR_SUCCESS == err && _tcslen(strVal)) || ERROR_MORE_DATA == err)
				{
					// if so, add to list
					CString tmp(subkeyName);
					dirs.SetupPlatform(tmp);
					platList += tmp + ';';
					vLog("DBP::PPL add to platform list (%s)", subkeyName);
					break;
				}
				else
				{
					vLog("DBP::PPL FALED to query (%s)", (LPCTSTR)subKeyDirName);
				}
			}
		}
		else
		{
			vLog("DBP::PPL FALED to open (%s)", (LPCTSTR)subKeyDirName);
		}
		hSubKey.Close();
	}
	RegCloseKey(hKey);

LEAVE_PLATFORM:
	// write out platforms list; read by va options dlg
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "Platforms", platList);
}

// this needs to be a separate function due to SEH
void CallOptionsDlg(ShowOptionsFnW op, VaOpsWinParams* params)
{
	_ASSERTE(op && params);
	// [case: 111288]
	__try
	{
		op(params);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		vLog("ERROR: caught VaOpsWin SEH exception code=%lx", GetExceptionCode());
	}
}

void DoVAOptions(const char* tab)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	Log("VAOpt");
	CWaitCursor curs;
	SetupVcEnvironment(false);
	const CString kPrevPlatform(Psettings->m_platformIncludeKey);
	bool doReloadProject = false;
	PopulatePlatformList();
	if (gShellAttr->IsDevenv10OrHigher())
	{
		// init m_srcFontName in case snippet editor invoked from options dlg
		LOGFONTW envLf;
		ZeroMemory((PVOID)&envLf, sizeof(LOGFONTW));
		g_IdeSettings->GetEditorFont(&envLf);
		if (envLf.lfFaceName[0])
		{
			const CStringW fnt(envLf.lfFaceName);
			if (!fnt.IsEmpty() && fnt.GetLength() < 255)
				::wcscpy(Psettings->m_srcFontName, fnt);
		}
	}

	Psettings->m_spellFlags = (Psettings->m_spellFlags & 0x1); // ops dlg is expecting BOOL
	Psettings->m_MainWndHnd = MainWndH;

	const CStringW file = (VaDirs::GetDllDir() + IDS_VAOPSWIN_DLLW);
	static Library hOpt((CString)file);
	if (!hOpt.IsLoaded())
	{
		Log((const char*)CString(file));
		hOpt.LoadFromVaDirectory(IDS_VAOPSWIN_DLL);
		if (!hOpt.IsLoaded())
		{
			DWORD err = GetLastError();
			vLog("ERROR Loading: %S %ld", (LPCWSTR)file, err);
		}
	}

	{
		// sets about info reg key
		LogVersionInfo info;
	}

	if (hOpt.IsLoaded())
	{
		Log("hOpt");
		ShowOptionsFnW op;
		hOpt.GetFunction("ShowOptionsW", op);
		if (op)
		{
			Log("op");
			// save some current state so that we can react to changes
			const bool kPrevHclVal = Psettings->mMarkCurrentLine;
			const bool kPrevAutoHighlightRefs = Psettings->mAutoHighlightRefs;
			const bool kPrevColorViews = Psettings->m_bEnhColorViews;
			const bool kPrevCheckForUpdates = Psettings->mCheckForLatestVersion;
			const bool kPrevBraceNewLine = Psettings->mInsertOpenBraceOnNewLine;
			const DWORD kPrevBraceMatchStyle = Psettings->mBraceAutoMatchStyle;
			const bool kPrevAutoMatch = Psettings->AutoMatch;
			const bool kPrevExtendCommentOnNewline = Psettings->mExtendCommentOnNewline;
			const bool kPrevSSGranularStart = Psettings->mSmartSelectEnableGranularStart;
			const bool kPrevSSWordStart = Psettings->mSmartSelectEnableWordStart;
			const bool kPrevSSWordCase = Psettings->mSmartSelectSplitWordByCase;
			const bool kPrevSSWordUnderscore = Psettings->mSmartSelectSplitWordByUnderscore;
			const DWORD kPrevUnrealEngineAutoDetect = Psettings->mUnrealEngineAutoDetect;
			const bool kPrevAlwaysDisplayUnrealSymbolsInItalics = Psettings->mAlwaysDisplayUnrealSymbolsInItalics;
			const DWORD kPrevMouseClickCmds = Psettings->mMouseClickCmds;

			const CStringW kUserDir(VaDirs::GetUserDir());
			const CStringW kAutotextDir(VaDirs::GetUserDir() + L"Autotext\\");
			VaOpsWinParams::IdeVersion vers;
			// #newVsVersion
			if (gShellAttr->IsDevenv17())
				vers = VaOpsWinParams::vs17;
			else if (gShellAttr->IsDevenv16())
				vers = VaOpsWinParams::vs16;
			else if (gShellAttr->IsDevenv15())
				vers = VaOpsWinParams::vs15;
			else if (gShellAttr->IsDevenv14())
				vers = VaOpsWinParams::vs14;
			else if (gShellAttr->IsDevenv12())
				vers = VaOpsWinParams::vs12;
			else if (gShellAttr->IsDevenv11())
				vers = VaOpsWinParams::vs11;
			else if (gShellAttr->IsDevenv10())
				vers = VaOpsWinParams::vs10;
			else if (gShellAttr->IsDevenv9())
				vers = VaOpsWinParams::vs9;
			else if (gShellAttr->IsDevenv8())
				vers = VaOpsWinParams::vs8;
			else if (gShellAttr->IsDevenv())
				vers = VaOpsWinParams::vs7;
			else if (gShellAttr->IsMsdev())
				vers = VaOpsWinParams::vc6;
			else
			{
				_ASSERTE(!"unhandled version of vs -- new version of vs??");
				vers = VaOpsWinParams::noIde;
			}
			VaOpsWinParams::IdeExe exe;
#ifdef AVR_STUDIO
			exe = VaOpsWinParams::AVR;
#elif defined(RAD_STUDIO)
			exe = VaOpsWinParams::CppB;
#else
			exe = VaOpsWinParams::VS;
#endif
			VaOptionsService optSvc;
			VaOpsWinParams params(&optSvc, Psettings, kUserDir,
#if defined(RAD_STUDIO)
			                      ID_RK_APP, ID_RK_APP,
#else
			                      ID_RK_WT_KEY, ID_RK_APP_KEY,
#endif
			                      vers, exe, g_loggingEnabled, tab);
			curs.Restore();

			{
				// [case: 137636]
				VsUI::CDpiScope dpi;
				CallOptionsDlg(op, &params);
			}

			if (g_currentEdCnt && gShellAttr->IsDevenv10OrHigher() && Is_Tag_Based(g_currentEdCnt->m_ftype))
			{
				// [case: 41874] force a change of focus when html/asp/xml is active
				MessageBoxTimeout(gMainWnd->GetSafeHwnd(), ".", ".", MB_OK, 0, 1);
			}

			CWaitCursor curs2;
			Psettings->CheckForConflicts();

#if !defined(RAD_STUDIO)
			if (kPrevHclVal != Psettings->mMarkCurrentLine && gShellAttr->IsDevenv11OrHigher() && g_IdeSettings)
			{
				// [case: 62645] user changed Highlight current line setting - check for conflict w/ dev11+
				const bool vsHcl = g_IdeSettings->GetEditorBoolOption("General", "HighlightCurrentLine");
				if (Psettings->mMarkCurrentLine == vsHcl)
				{
#ifdef AVR_STUDIO
#define SHELL_NAME "Atmel Studio"
#else
#define SHELL_NAME "Visual Studio"
#endif
					LPCSTR msgTxt;
					if (Psettings->mMarkCurrentLine)
						msgTxt = "The Visual Assist Highlight current line feature was enabled and the " SHELL_NAME
						         " version is also enabled.\r\n\r\nDisable the " SHELL_NAME " feature?";
					else
						msgTxt = "The Visual Assist Highlight current line feature was disabled.\r\n\r\nEnable "
						         "the " SHELL_NAME " version?";

					curs2.Restore();
					const int res = WtMessageBox(msgTxt, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION);
					if (res == IDYES)
						g_IdeSettings->SetEditorOption("General", "HighlightCurrentLine",
						                               Psettings->mMarkCurrentLine ? "FALSE" : "TRUE");
				}
			}

#ifndef AVR_STUDIO
			if (kPrevAutoHighlightRefs != Psettings->mAutoHighlightRefs && gShellAttr->IsDevenv10OrHigher() &&
			    g_IdeSettings)
			{
				// [case: 66210] user changed Auto highlight refs setting - check for conflict w/ dev10+
				bool vsAhr = false;
				if (gShellAttr->IsDevenv11OrHigher() && !strstr(Psettings->mExtensionsToIgnore, ".cpp;"))
					vsAhr |= (!g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "DisableReferenceHighlighting"));

				if (!strstr(Psettings->mExtensionsToIgnore, ".cs;"))
					vsAhr |= g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "HighlightReferences");

				if (!Psettings->mRestrictVaToPrimaryFileTypes)
				{
					if (!strstr(Psettings->mExtensionsToIgnore, ".vb;"))
						vsAhr |= g_IdeSettings->GetEditorBoolOption("Basic-Specific", "EnableHighlightReferences");
				}

				if (Psettings->mAutoHighlightRefs == vsAhr)
				{
					LPCSTR msgTxt;
					if (Psettings->mAutoHighlightRefs)
						msgTxt = "The Visual Assist Automatically highlight references feature was enabled and the "
						         "Visual Studio version is also enabled.\r\n\r\nDisable the Visual Studio feature?";
					else
						msgTxt = "The Visual Assist Automatically highlight references feature was "
						         "disabled.\r\n\r\nEnable the Visual Studio version?";

					curs2.Restore();
					const int res = WtMessageBox(msgTxt, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION);
					if (res == IDYES)
					{
						if (gShellAttr->IsDevenv11OrHigher() && !strstr(Psettings->mExtensionsToIgnore, ".cpp;"))
							g_IdeSettings->SetEditorOption("C/C++ Specific", "DisableReferenceHighlighting",
							                               Psettings->mAutoHighlightRefs ? "TRUE" : "FALSE");

						if (!strstr(Psettings->mExtensionsToIgnore, ".cs;"))
							g_IdeSettings->SetEditorOption("CSharp-Specific", "HighlightReferences",
							                               Psettings->mAutoHighlightRefs ? "0" : "1");

						if (!Psettings->mRestrictVaToPrimaryFileTypes)
						{
							if (!strstr(Psettings->mExtensionsToIgnore, ".vb;"))
								g_IdeSettings->SetEditorOption("Basic-Specific", "EnableHighlightReferences",
								                               Psettings->mAutoHighlightRefs ? "FALSE" : "TRUE");
						}
					}
				}
			}

			if (((kPrevAutoMatch != Psettings->AutoMatch) || Psettings->mBraceAutoMatchStyle != kPrevBraceMatchStyle) &&
			    gShellAttr->IsDevenv12OrHigher() && g_IdeSettings)
			{
				int vsAutoMatch = 0;
				if (CS == gTypingDevLang)
					vsAutoMatch = g_IdeSettings->GetEditorIntOption("CSharp", "BraceCompletion");
				else
					vsAutoMatch = g_IdeSettings->GetEditorIntOption("C/C++", "BraceCompletion");

				if (vsAutoMatch && kPrevAutoMatch != Psettings->AutoMatch)
				{
					// [case: 76840]
					if (Psettings->AutoMatch)
					{
						const TCHAR* infoMsg =
						    "Visual Assist brace completion is enabled (on the Editor page of the Visual Assist "
						    "Options dialog).\r\n\r\n"
						    "Note that Visual Studio Automatic brace completion is currently enabled and has "
						    "precedence over Visual Assist (it is enabled by default).\r\n"
						    "The Visual Studio setting can be found at:\r\n"
						    "Tools | Options | Text Editor | <Language> | General\r\n\r\n"
						    "Would you like Visual Assist to disable the Visual Studio setting?";
						if (IDYES == ::WtMessageBox(infoMsg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
						{
							if (CS == gTypingDevLang)
								g_IdeSettings->SetEditorOption("CSharp", "BraceCompletion", "0");
							else
								g_IdeSettings->SetEditorOption("C/C++", "BraceCompletion", "0");
						}
					}
					else
					{
						const TCHAR* infoMsg = "Visual Assist brace completion is disabled (on the Editor page of the "
						                       "Visual Assist Options dialog).\r\n\r\n"
						                       "Note that Visual Studio Automatic brace completion is currently "
						                       "enabled (it is enabled by default).\r\n"
						                       "The Visual Studio setting can be found at:\r\n"
						                       "Tools | Options | Text Editor | <Language> | General";
						::WtMessageBox(infoMsg, IDS_APPNAME, MB_ICONQUESTION | MB_OK);
					}
				}
				else if (vsAutoMatch && Psettings->AutoMatch && Psettings->mBraceAutoMatchStyle != kPrevBraceMatchStyle)
				{
					// [case: 91361]
					const TCHAR* infoMsg = "Visual Assist closing brace insertion style changed (on the Editor page of "
					                       "the Visual Assist Options dialog).\r\n\r\n"
					                       "Note that Visual Studio Automatic brace completion is currently enabled "
					                       "and has precedence over Visual Assist (it is enabled by default).\r\n"
					                       "The Visual Studio setting can be found at:\r\n"
					                       "Tools | Options | Text Editor | <Language> | General\r\n\r\n"
					                       "Would you like Visual Assist to disable the Visual Studio setting?";
					if (IDYES == ::WtMessageBox(infoMsg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
					{
						if (CS == gTypingDevLang)
							g_IdeSettings->SetEditorOption("CSharp", "BraceCompletion", "0");
						else
							g_IdeSettings->SetEditorOption("C/C++", "BraceCompletion", "0");
					}
				}
			}

			if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && !Psettings->mCheckForLatestVersion &&
			    kPrevCheckForUpdates)
			{
				// [case: 73456]
				const TCHAR* infoMsg =
				    _T("Visual Assist will notify you of all updates and warn you "
				       "if you must renew software maintenance before installing one. "
				       "The Microsoft extension manager, on the other hand, will notify you and let you "
				       "install updates without regard for your software maintenance.\r\n\r\n"
				       "Are you sure you want to disable notifications from Visual Assist?");
				if (IDNO ==
				    ::OneTimeMessageBox(_T("Vs2010+CheckForUpdateWarning"), infoMsg, MB_YESNO | MB_ICONQUESTION))
					Psettings->mCheckForLatestVersion = true;
			}

			if (g_IdeSettings && gShellAttr && gShellAttr->IsDevenv10OrHigher() &&
			    kPrevBraceNewLine != Psettings->mInsertOpenBraceOnNewLine)
			{
				// [case: 89901]
				// if user changed the va setting, update the corresponding vs setting
				if (!strstr(Psettings->mExtensionsToIgnore, ".cs;"))
					g_IdeSettings->SetEditorOption("CSharp-Specific", "NewLines_Braces_ControlFlow",
					                               Psettings->mInsertOpenBraceOnNewLine ? "1" : "0");

				// don't change C++ setting; it is a tri-state that doesn't map cleanly.
				// also, format selection in C++ operates differently than in C#.
				// C++ won't move brace to previous line unless you select the previous line.
				// C# moves brace from selection to previous line even though previous line wasn't selected.
				// g_IdeSettings->SetEditorOption("C/C++ Specific", "NewlineControlBlockBrace",
				// Psettings->mInsertOpenBraceOnNewLine ? "1" : "0");
			}

			if (g_IdeSettings && gShellAttr && gShellAttr->IsDevenv14u2OrHigher() &&
			    kPrevExtendCommentOnNewline != Psettings->mExtendCommentOnNewline)
			{
				if (gShellAttr && gShellAttr->IsDevenv14u3OrHigher())
				{
					bool doCppExtendCommentOnNewline = false;
					if (gShellAttr->IsDevenv16u8OrHigher())
					{
						// [case: 144301]
						// handle VC C++ option ContinueCommentsOnEnter and disable it if possible (VS 16.8 and higher)
						bool cppInsertAsterisk =
						    g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "ContinueCommentsOnEnter");
						if (cppInsertAsterisk == Psettings->mExtendCommentOnNewline)
							doCppExtendCommentOnNewline = true;
					}

					bool doCsExtendCommentOnNewline = false;
					if (!strstr(Psettings->mExtensionsToIgnore, ".cs;"))
					{
						// [case: 95605]
						// if user changed the va setting, update the corresponding vs setting
						int csInsertAsterisk = g_IdeSettings->GetEditorIntOption(
						    "CSharp-Specific", "AutoInsertAsteriskForNewLinesOfBlockComments");
						if (csInsertAsterisk == (int)Psettings->mExtendCommentOnNewline)
							doCsExtendCommentOnNewline = true;
					}

					CString msgTxtCpp16u7 = "Both the Visual Assist 'Extend multi-line comments' feature and the "
					                        "Visual Studio version are enabled. "
					                        "Since the version in the currently installed version (16.7) of Visual "
					                        "Studio cannot be disabled, the "
					                        "Visual Assist version of the feature will internally be disabled for C++ "
					                        "to avoid incorrect results.";

					if (doCppExtendCommentOnNewline || doCsExtendCommentOnNewline)
					{
						CString lang, langVersion, langFeature;
						if (doCppExtendCommentOnNewline && doCsExtendCommentOnNewline)
						{
							lang = "C++ and C#";
							langVersion = " versions";
							langFeature = "Studio features";
						}
						else if (doCppExtendCommentOnNewline)
						{
							lang = "C++";
							langVersion = " version";
							langFeature = "C++ feature";
						}
						else
						{
							lang = "C#";
							langVersion = " version";
							langFeature = "C# feature";
						}

						CString msgTxt;
						if (Psettings->mExtendCommentOnNewline)
						{
							msgTxt =
							    "Both the Visual Assist 'Extend multi-line comments' feature and the Visual Studio " +
							    lang + langVersion + " are enabled.\r\n\r\nDo you want to disable the Visual " +
							    langFeature + " and use only the Visual Assist version?";
							if (gShellAttr->IsDevenv16u7()) // inform user about VS 16.7 problem with C++ feature
								msgTxt += "\r\n\r\n" + msgTxtCpp16u7;
						}
						else
						{
							msgTxt =
							    "The Visual Assist 'Extend multi-line comments' feature is disabled. Visual Studio " +
							    lang + " contains a similar feature.\r\n\r\nDo you want to enable the Visual Studio " +
							    lang + langVersion + "?";
						}

						curs2.Restore();
						const int res = WtMessageBox(msgTxt, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION);
						if (res == IDYES)
						{
							if (doCppExtendCommentOnNewline)
								g_IdeSettings->SetEditorOption("C/C++ Specific", "ContinueCommentsOnEnter",
								                               Psettings->mExtendCommentOnNewline ? "false" : "true");

							if (doCsExtendCommentOnNewline)
								g_IdeSettings->SetEditorOption("CSharp-Specific",
								                               "AutoInsertAsteriskForNewLinesOfBlockComments",
								                               Psettings->mExtendCommentOnNewline ? "0" : "1");
						}
					}
					else if (Psettings->mExtendCommentOnNewline && gShellAttr->IsDevenv16u7())
					{
						// [case: 144301]
						// we need to cover case when C# message is not shown so we need to inform user about VS 16.7
						// problem with C++ feature
						curs2.Restore();
						WtMessageBox(msgTxtCpp16u7, IDS_APPNAME, MB_OK | MB_ICONWARNING);
					}
				}
				else if (gShellAttr && gShellAttr->IsDevenv14u2OrHigher() && !kPrevExtendCommentOnNewline &&
				         Psettings->mExtendCommentOnNewline)
				{
					// [case: 95471]
					// user enabled extendCommentOnNewline
					LPCSTR msgTxt =
					    "You enabled the \"Extend multi-line comments\" feature.\r\n\r\n"
					    "Visual Studio has a related feature that can cause incorrect results in C# source when both "
					    "features are enabled.\r\n\r\n"
					    "Please ensure that \"Insert * at the start of new lines when writing /**/ comments\" is "
					    "unchecked on the Text Editor | C# | Advanced page of the Visual Studio Options dialog.";
					curs2.Restore();
					WtMessageBox(msgTxt, IDS_APPNAME, MB_OK | MB_ICONINFORMATION);
				}
			}
#endif // !AVR_STUDIO

			if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			{
				if (kPrevColorViews != Psettings->m_bEnhColorViews)
				{
					// state of views changed, update FindResults state to match
					Psettings->m_bEnhColorFindResults = Psettings->m_bEnhColorViews;
				}
			}

			if (kPrevSSGranularStart != Psettings->mSmartSelectEnableGranularStart ||
			    kPrevSSWordStart != Psettings->mSmartSelectEnableWordStart ||
			    kPrevSSWordCase != Psettings->mSmartSelectSplitWordByCase ||
			    kPrevSSWordUnderscore != Psettings->mSmartSelectSplitWordByUnderscore)
			{
				extern void CleanupSmartSelectStatics();
				CleanupSmartSelectStatics();
			}

			// [case: 116799] Allow user to deactivate VS feature
			if (kPrevMouseClickCmds != Psettings->mMouseClickCmds && gShellAttr && gShellAttr->IsDevenv15OrHigher() &&
			    gDte)
			{
				CComBSTR regRoot;
				if (SUCCEEDED(gDte->get_RegistryRoot(&regRoot)))
				{
					CStringW regRootW{regRoot};
					if (!regRootW.IsEmpty())
					{
						auto get_DWORD = [](const CString& str, DWORD defaultValue) -> DWORD {
							int typeStart = str.Find('*');
							if (typeStart >= 0)
							{
								int valueStart = str.ReverseFind('*');
								if (valueStart >= 0)
								{
									WTString type = (const char*)str.Mid(typeStart + 1, valueStart - (typeStart + 1));
									if (type.MatchRE("System.UInt(?:16|32|64)"))
									{
										CString value = str.Mid(valueStart + 1);
										return (DWORD)_tcstoui64(str.Mid(valueStart + 1), nullptr, 10);
									}
									else if (type.MatchRE("System.Int(?:16|32|64)"))
									{
										CString value = str.Mid(valueStart + 1);
										return (DWORD)_tcstoi64(str.Mid(valueStart + 1), nullptr, 10);
									}
								}
							}

							return defaultValue;
						};

						// vs2017
						const CString regValName("Enable Clickable Goto Definition");
						// NOTE: Typo "wiht" is in original value name (suspect to change in future)
						const CString regValNameModif = "Modifier Key Used wiht Mouse Click for Goto Definition";
						const CStringW reg(regRootW + L"\\ApplicationPrivateSettings\\TextEditor\\General");
						const CString curVal(GetRegValue(HKEY_CURRENT_USER, CString(reg), regValName, "unset"));
						switch (get_DWORD(curVal, 1))
						{
						case 1: {
							DWORD conflict = 0;
							DWORD conflictBinding = 0;

							const CString curValModif(
							    GetRegValue(HKEY_CURRENT_USER, CString(reg), regValNameModif, "unset"));
							switch (get_DWORD(curValModif, 0))
							{
							case 0:
								// CTRL key is used by VS
								conflictBinding = (DWORD)VaMouseCmdBinding::CtrlLeftClick;
								conflict = Psettings->mMouseClickCmds.get(conflictBinding);
								break;
							case 1:
								// ALT key is used by VS
								conflictBinding = (DWORD)VaMouseCmdBinding::AltLeftClick;
								conflict = Psettings->mMouseClickCmds.get(conflictBinding);
								break;
							case DWORD_ERROR:
								vLog("WARN: did not read Modifier Key Used wiht Mouse Click for Goto Definition value "
								     "(%s)",
								     (LPCTSTR)curValModif);
								break;
							}

							if (conflict)
							{
								LPCTSTR conflictBindingName(conflictBinding == (DWORD)VaMouseCmdBinding::CtrlLeftClick
								                                ? TEXT("Ctrl+Left-Click")
								                                : TEXT("Alt+Left-Click"));

								CString infoMsg;
								CString__FormatA(
								    infoMsg,
								    "Visual Assist mouse action %s has been enabled (on the Mouse page of the Visual "
								    "Assist Options dialog).\r\n\r\n"
								    "Note that Visual Studio \"mouse click to perform Go to Definition\" is currently "
								    "enabled for the same action.\r\n"
								    "The Visual Studio setting can be found at:\r\n"
								    "Tools | Options | Text Editor | General\r\n\r\n"
								    // "Would you like Visual Assist to disable the Visual Studio setting?"
								    "Would you like to open the Visual Studio Options dialog to change it?",
								    (LPCTSTR)conflictBindingName);

								if (IDYES == ::WtMessageBox(infoMsg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
								{
									// The SetRegValue call takes immediate effect if no editor has been opened and the
									// VS Options dialog has not been opened.
									// If either is not true, then it has no effect until VS restart.
									// Need to find a way to prod the editor into settings reload -- maybe ilspy would
									// help. SetRegValue(HKEY_CURRENT_USER, CString(reg), regValName,
									// "0*System.Int64*0");
									PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Tools.Options", 0);
								}
							}
						}
						break;
						case 0:
							break;
						case DWORD_ERROR:
							vLog("WARN: did not read Enable Clickable Goto Definition value (%s)", (LPCTSTR)curVal);
							break;
						}
					}
				}
			}
#endif // !RAD_STUDIO

			VaOptionsUpdated();
			if (!params.mLoggingEnabled)
				g_loggingEnabled = 0;
			if (gVaInteropService)
				gVaInteropService->SetLoggingEnabled(!!g_loggingEnabled);

			// [case: 113964]
			if (Psettings->mUnrealEngineAutoDetect != kPrevUnrealEngineAutoDetect)
			{
				if (Psettings->mUnrealEngineAutoDetect == 0)
					Psettings->mUnrealEngineCppSupport = false;

				doReloadProject = true;
			}

			if (Psettings->mUnrealEngineAutoDetect == 2)
				Psettings->mUnrealEngineCppSupport = true;

			// [case: 114813]
			if (Psettings->mAlwaysDisplayUnrealSymbolsInItalics != kPrevAlwaysDisplayUnrealSymbolsInItalics)
				doReloadProject = true;
		}
		else
		{
			vLog("ERROR vaopswin entry point not found");
			const CString errMsg("Missing function export in " + file);
			curs.Restore();
			WtMessageBox(errMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		}
	}

	// options dlg should update both the registry and the settings struct
	// this assert can fire (safely) if running more than one instance
	_ASSERTE(GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM) == Psettings->m_platformIncludeKey);
	if (kPrevPlatform != Psettings->m_platformIncludeKey || // if platform changed
	    kCustom == Psettings->m_platformIncludeKey) // or if platform is custom (since user might have modified dirs)
		IncludeDirs::Reset();

	Psettings->Commit();

	if (doReloadProject)
	{
		if (GlobalProject)
			GlobalProject->SolutionLoadCompleted(true);

		::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_UPDATE_SOLUTION_LOAD, SolutionLoadState::slsLoadComplete, 0);
	}
}

void VaOptionsUpdated()
{
	if (gColorSyncMgr)
		gColorSyncMgr->PotentialVaColorChange();

	// update vapkg and MEF component state so that they do not need to continuously query
	if (gVaShellService)
		gVaShellService->OptionsUpdated();

	if (gVaInteropService)
		gVaInteropService->OptionsUpdated();

	g_ScreenAttrs.OptionsUpdated();

	if (g_CompletionSet)
		g_CompletionSet->RebuildExpansionBox();
}
