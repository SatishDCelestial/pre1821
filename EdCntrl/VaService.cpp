#include "StdAfxEd.h"
#include <algorithm>
#include "VaService.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "FindReferencesResultsFrame.h"
#include "VAWorkspaceViews.h"
#include "VAOpenFile.h"
#include "VABrowseSym.h"
#include "VaOptions.h"
#include "SubClassWnd.h"
#include "Edcnt.h"
#include "VARefactor.h"
#include "VATimers.h"
#include "ParseThrd.h"
#include "FileTypes.h"
#include "Settings.h"
#include "ScreenAttributes.h"
#include "LiveOutlineFrame.h"
#include "RegKeys.h"
#include "Registry.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "AutotextManager.h"
#include "VaMessages.h"
#include "FindReferencesThread.h"
#include "Vc6RefResultsCloneToolwnd.h"
#include "SymbolRemover.h"
#include "TraceWindowFrame.h"
#include "file.h"
#include "project.h"
#include "TokenW.h"
#include "WPF_ViewManager.h"
#include "SyntaxColoring.h"
#include "MEFCompletionSet.h"
#include <vsshell80.h>
#include "myspell\WTHashList.h"
#include "VAAutomation.h"
#include "Addin\MiniHelpFrm.h"
#include "ColorListControls.h"
#include "CodeGraph.h"
#include "VACompletionSet.h"
#include "GetFileText.h"
#include "ShellListener.h"
#include "IdeSettings.h"
#include "SolutionListener.h"
#include <locale>
#include "utils_goran.h"
#include "FontSettings.h"
#include "ColorSyncManager.h"
#include "VsSnippetManager.h"
#include "ImageListManager.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "TipOfTheDay.h"
#include "AutoUpdate\WTAutoUpdater.h"
#if defined(SANCTUARY)
#include "Sanctuary\RegisterSanctuary.h"
#else
#include "Addin\Register.h"
#endif
#include "AboutDlg.h"
#include "Addin\BuyTryDlg.h"
#include "StringUtils.h"
#include "LogVersionInfo.h"
#include "focus.h"
#ifdef _WIN64
#include "..\VaManagedComLib\VaManagedComLib64_h.h"
#else
#include "..\VaManagedComLib\VaManagedComLib_h.h"
#endif
#include "DebuggerTools\DebuggerToolsCpp.h"
#include "VaHashtagsFrame.h"
#include "..\common\ScopedIncrement.h"
#include "IVaLicensing.h"
#include "VaAddinClient.h"
#include "KeyBindings.h"
#include "IVaMenuItemCollection.h"
#include "Directories.h"
#include "SolutionFiles.h"
#include "FileFinder.h"
#include "BuildInfo.h"
#include "DpiCookbook\VsUIDpiAwareness.h"
#include "..\common\ThreadStatic.h"
#include "Addin\FileVerInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

VaService* gVaService = NULL;
IVaShellService* gVaShellService = NULL;
IVaInteropService* gVaInteropService = NULL;
static PrimaryResultsFrame* gPrimaryFindRefsResultsFrame = NULL;
CComQIPtr<IVaManagedComService> g_managedInterop;
CComQIPtr<IServiceProvider> gPkgServiceProvider;
CComQIPtr<IVsTextManager> gVsTextManager;
CComQIPtr<IVsHiddenTextManager> gVsHiddenTextManager;
CComPtr<IVsRunningDocumentTable> gVsRunningDocumentTable;
CComQIPtr<IVaManagedPackageService> g_vaManagedPackageSvc;
BOOL gHasMefParamInfo = FALSE;
BOOL gHasMefQuickInfo = FALSE;
LONG gExecActive = 0;
DWORD gCurrExecCmd = 0;
static EdCntPtr sLastEdCnt;

FileVersionInfo* gIdeVersion = NULL;
FileVersionInfo ideVersionInfo;
void InitIdeVersion()
{
	if (!gIdeVersion)
	{
		ideVersionInfo.QueryFile(GetModuleHandleA(0));
		if (ideVersionInfo.IsValid())
		{
			gIdeVersion = &ideVersionInfo;
		}
	}
}

// [case: 56073] VsVim compatibility.
// Do not confuse with g_inMacro, Psettings->m_incrementalSearch or EdCnt::m_typing.
// State is set via MEF component watching caret visibility.
bool gTypingAllowed = true;

static void Vc6RefResultsFocus();
static bool IsEditorSpecificCommand(DWORD cmdId);
static bool IsValidUnfocusedEditorCommand(DWORD cmdId);
static bool IsDynamicCommand(DWORD cmdId);
static bool IsRefactorCommand(DWORD cmdId);
static void RenewMaintenance();
static void EmailToTechnicalSupport();
static void EmailToTechnicalSupportFeatureRequest();
static void EMailToLicenseSupport();
#if !defined(VA_CPPUNIT)
static void LaunchRenewPage();
#endif

VaService::VaService()
    : mOutlineFrame(NULL), mTraceFrame(NULL), mHashtagsFrame(NULL), mShellIsModal(false), mInitAborted(false)
{
	_ASSERTE(gVaService == NULL);
	InitIdeVersion();
}

VaService::~VaService()
{
	sLastEdCnt = nullptr;
	SetVaShellService(NULL);

	for (NotifierIter it = mNotifiers.begin(); it != mNotifiers.end(); ++it)
		(*it)->VaServiceShutdown();

	if (g_vaManagedPackageSvc)
	{
		g_vaManagedPackageSvc->OnShutdown();
		g_vaManagedPackageSvc = nullptr;
	}

	if (gVaInteropService)
		gVaInteropService->OnShutdown();

	for (FindReferencesWndMap::iterator mapIt = mFindRefPanes.begin(); mapIt != mFindRefPanes.end(); mapIt = mFindRefPanes.begin())
		FindReferencesPaneDestroyed((*mapIt).first);

	delete mOutlineFrame;
	delete mTraceFrame;
	::DeleteSpentFindReferencesThreads();
}

// Interface IVaService
void VaService::RegisterNotifier(IVaServiceNotifier* notifier)
{
	NotifierIter it = std::find(mNotifiers.begin(), mNotifiers.end(), notifier);

	if (it == mNotifiers.end())
		mNotifiers.push_back(notifier);
}

void VaService::UnregisterNotifier(IVaServiceNotifier* notifier)
{
	NotifierIter it = std::find(mNotifiers.begin(), mNotifiers.end(), notifier);

	if (it != mNotifiers.end())
		mNotifiers.remove(notifier);
}

void VaService::SetVaShellService(IVaShellService* svc)
{
	gVaShellService = svc;

	if (svc)
	{
		if (!gPkgServiceProvider)
		{
			gPkgServiceProvider = svc->GetServiceProvider();
			_ASSERTE(gPkgServiceProvider);

			if (gPkgServiceProvider)
				VsServicesReady();
		}
	}
	else
	{
		delete gColorSyncMgr;
		_ASSERTE(!gColorSyncMgr);

		// VS2013 12.0.20513.1 - 12.0.20617.1 previews have a bad IServiceProvider ref cnt - don't release
		// still occurs with vs2015
		if (!gShellAttr || !gShellAttr->IsDevenv12OrHigher())
			gPkgServiceProvider.Release();

		gVsTextManager.Release();
		gVsHiddenTextManager.Release();
		gVsRunningDocumentTable.Release();
		sLastEdCnt = nullptr;
	}
}

void VaService::SetVaInteropService(IVaInteropService* svc)
{
#ifndef VAX_CODEGRAPH
	gVaInteropService = svc;

	if (gVaInteropService)
	{
		gVaInteropService->SetLoggingEnabled(!!g_loggingEnabled);
		gVaInteropService->SetAstEnabled(gTestsActive);

		if (g_FontSettings && gShellAttr && gShellAttr->IsDevenv10OrHigher())
			g_FontSettings->Update();

		if (gColorSyncMgr)
		{
			// [case: 66898] init colors from VS now that MEF component has loaded
			::RunFromMainThread(
			    [] {
				    if (gColorSyncMgr)
					    gColorSyncMgr->CompleteInit();
			    },
			    false);
		}
	}
#endif
}

void VaService::SetVaManagedPackageService(
#ifdef _WIN64
    intptr_t pIVaManagedPackageService
#else
    int pIVaManagedPackageService
#endif
)
{
	if (g_vaManagedPackageSvc)
		g_vaManagedPackageSvc.Release();

	g_vaManagedPackageSvc = (IUnknown*)pIVaManagedPackageService;

#if !defined(VAX_CODEGRAPH) && !defined(RAD_STUDIO)
	// [case: 118111]
	// show automatic display on 15u80OrHigher not on startup but only when g_vaManagedPackageSvc is finally set
	if (g_vaManagedPackageSvc && gShellAttr && gShellAttr->IsDevenv15u8OrHigher())
		CheckForKeyBindingUpdate();
#endif
}

IVaInteropService* VaService::GetVaInteropService()
{
	return gVaInteropService;
}

bool VaService::PaneCreated(HWND hWnd, LPCSTR paneName, UINT windowId)
{
	if (!gImgListMgr || !g_statBar)
	{
		// [case: 109147]
		// still setting up on this side, caller will retry later
		return false;
	}

	_ASSERTE(mPanes.find(hWnd) == mPanes.cend());

	auto paneType = GetWindowPaneType(paneName);
	if (paneType != wptEmpty)
	{
		_ASSERTE(paneType == wptFindRefResultsClone || !windowId);
		mPanes[hWnd] = paneType;

		switch (paneType)
		{
		case VaService::wptVaView:
			VaViewPaneCreated(hWnd);
			return true;
		case VaService::wptVaOutline:
			LiveOutlineCreated(hWnd);
			return true;
		case VaService::wptFindRefResults:
			FindReferencesPaneCreated(hWnd);
			return true;
		case VaService::wptFindRefResultsClone:
			FindReferencesClonePaneCreated(hWnd, windowId);
			return true;
		case VaService::wptTraceWindow:
			TracePaneCreated(hWnd);
			return true;
		case VaService::wptHashtags:
			HashtagsPaneCreated(hWnd);
			return true;
		default:
			break;
		}
	}

	_ASSERTE(!"unhandled pane creation event");

	return false;
}

bool VaService::PaneHandleChanged(HWND hWnd, LPCSTR paneName)
{
	auto paneType = GetWindowPaneType(paneName);

	for (auto it = mPanes.begin(); it != mPanes.end(); ++it)
	{
		if (it->second == paneType)
		{
			mPanes.erase(it);
			mPanes[hWnd] = paneType;
			return true;
		}
	}

	_ASSERTE(!"unhandled pane handle changed event");

	return false;
}

bool VaService::PaneActivated(HWND hWnd)
{
	PaneMap::const_iterator it = mPanes.find(hWnd);

	if (it == mPanes.end())
		return false;  // caller needs to call PaneCreated first

	switch ((*it).second)
	{
	case wptVaView:
		VaViewActivated();
		break;
	case wptFindRefResults:
	case wptFindRefResultsClone:
		FindReferencesPaneActivated(hWnd);
		break;
	case wptVaOutline:
		LiveOutlineActivated();
		break;
	case wptTraceWindow:
		TracePaneActivated();
		break;
	case wptHashtags:
		HashtagsPaneActivated();
		break;
	default:
		_ASSERTE(!"unhandled pane activation event");
		return false;
	}

	return true;
}

void VaService::PaneDestroy(HWND hWnd)
{
	_ASSERTE(mPanes[hWnd] != wptEmpty);
	switch (mPanes[hWnd])
	{
	case wptVaView:
		VaViewDestroyed();
		break;
	case wptFindRefResults:
	case wptFindRefResultsClone:
		FindReferencesPaneDestroyed(hWnd);
		break;
	case wptVaOutline:
		LiveOutlineDestroyed();
		break;
	case wptTraceWindow:
		TracePaneDestroyed();
		break;
	case wptHashtags:
		HashtagsPaneDestroyed();
		break;
	default:
		_ASSERTE(!"unhandled pane destruction event");
		break;
	}

	mPanes.erase(hWnd);
}

LPCWSTR
VaService::GetPaneCaption(HWND hWnd)
{
	PaneMap::const_iterator it = mPanes.find(hWnd);
	if (it == mPanes.end())
		return NULL; // caller needs to call PaneCreated first

	switch ((*it).second)
	{
	case wptVaView:
	case wptVaOutline:
	case wptTraceWindow:
	case wptHashtags:
		break;

	case wptFindRefResultsClone:
	case wptFindRefResults:
	{
		FindReferencesResultsFrame* frm = mFindRefPanes[hWnd];
		if (frm)
		{
			static CStringW sTxt;
			WTString txt(frm->GetCaption());
			sTxt = txt.Wide();
			return sTxt;
		}
	}
	break;

	default:
		_ASSERTE(!"unhandled pane caption request");
		return NULL;
	}

	return NULL;
}

bool VaService::GetBoolSetting(const char* settingIn) const
{
	bool result = false;
	const CString setting(settingIn);

	switch (setting[0])
	{
	case 'A':
		if (setting == "ActiveSyntaxColoring")
			result = Psettings ? Psettings->m_ActiveSyntaxColoring : false;
		else if (setting == "AutoSuggest")
			result = Psettings ? Psettings->m_autoSuggest : false;
		break;
	case 'C':
		if (setting == "CaseCorrect")
			result = Psettings ? Psettings->CaseCorrect : false;
		else if (setting == "CurrentLineMarker")
		{
			result = Psettings ? Psettings->mMarkCurrentLine : false;
			if (result && g_IdeSettings && g_IdeSettings->GetEditorBoolOption("General", "HighlightCurrentLine"))
				result = FALSE;
		}
		else if (setting == "ColumnIndicator")
			result = Psettings ? Psettings->m_colIndicator : false;
		else if (setting == "ColorBuildOutput")
			result = Psettings ? Psettings->mColorBuildOutput : false;
		break;
	case 'D':
		if (setting == "DoOldStyleMainMenu")
			result = Psettings ? Psettings->mDoOldStyleMainMenu : false;
		break;
	case 'E':
		if (setting == "EnhColorSourceWindows")
			result = Psettings ? Psettings->m_bEnhColorSourceWindows : false;
		else if (setting == "EnhColorObjectBrowser")
			result = Psettings ? Psettings->m_bEnhColorObjectBrowser : false;
		else if (setting == "EnhColorTooltips")
			result = Psettings ? Psettings->m_bEnhColorTooltips : false;
		else if (setting == "EnhColorViews")
			result = Psettings ? Psettings->m_bEnhColorViews : false;
		else if (setting == "EnhColorFindResults")
			result = Psettings ? Psettings->m_bEnhColorFindResults : false;
		else if (setting == "EnhColorListboxes")
			result = Psettings ? Psettings->m_bEnhColorListboxes : false;
		else if (setting == "EnhColorWizardBar")
			result = Psettings ? Psettings->m_bEnhColorWizardBar : false;
		else if (setting == "ExtensionlessFileIsHeader")
			result = Psettings ? Psettings->mExtensionlessFileIsHeader : false;
		else if (setting == "EnableShaderSupport")
			result = Psettings ? Psettings->mEnableShaderSupport && gShellAttr->IsDevenv14OrHigher() : false;
		break;
	case 'F':
		if (setting == "FormatDoxygenTags")
			result = Psettings ? Psettings->mFormatDoxygenTags : false;
		break;
	case 'H':
		if (setting == "HVL")
			result = Psettings ? Psettings->m_validLicense != false : false;
		break;
	case 'K':
		if (setting == "SaveBookmarks")
			result = Psettings ? Psettings->m_keepBookmarks : false;
		break;
	case 'L':
		if (setting == "LocalSymbolsInBold")
			result = Psettings ? Psettings->m_bLocalSymbolsInBold : false;
		break;
	case 'M':
		if (setting == "MultipleProjectSupport")
			result = Psettings ? Psettings->m_multipleProjSupport : false;
		else if (setting == "MarkTextAfterFind")
			result = Psettings ? Psettings->mMarkFindText : false;
		else if (setting == "MiniHelpEnabled")
			result = Psettings ? !Psettings->m_noMiniHelp : false;
		else if (setting == "MiniHelpAtTop")
			result = Psettings ? Psettings->minihelpAtTop : false;
		break;
	case 'S':
		if (setting == "StableSymbolsInItalics")
			result = Psettings ? Psettings->m_bStableSymbolsInItalics : false;
		else if (setting == "SuppressUnderlines")
			result = Psettings ? Psettings->m_bSupressUnderlines : false;
		else if (setting == "SimpleWordMatchHighlights")
			result = Psettings ? Psettings->mSimpleWordMatchHighlights : false;
		else if (setting == "ShowQuickActionsOnHover")
			result = Psettings ? Psettings->mDisplayRefactoringButton : false;
		break;
	case 'U':
		if (setting == "UseTomatoBackground")
			result = Psettings ? Psettings->mUseTomatoBackground : true;
		break;
	case 'V':
		if (setting == "VaEnabled")
			result = Psettings ? Psettings->m_enableVA : false;
		break;
	case 'W':
		if (setting == "WebEditorPerMonitorAwareness")
		{
			// [case: 141042] [case: 141827]
			if (Psettings->mWebEditorPmaFail)
			{
				// user override to restore old behavior
				return false;
			}

			result = gShellAttr->IsDevenv16OrHigher();
		}
		break;
	default:
		break;
	}

	return result;
}

DWORD
VaService::GetDwordSetting(const char* settingIn) const
{
	DWORD result = 0xffffffff;
	const CString setting(settingIn);

	switch (setting[0])
	{
	case 'C':
		if (setting == "CurrentLineBorderStyle")
			result = Psettings ? Psettings->mCurrentLineBorderStyle : 4;
		else if (setting == "CurrentLineVisualStyle")
			result = Psettings ? Psettings->mCurrentLineVisualStyle : 0x2100;
		else if (setting == "ColumnIndicatorColumn")
			result = Psettings ? Psettings->m_colIndicatorColPos : 0;
		else if (setting == "ColorTextForeground")
			result = Psettings ? Psettings->m_colors[C_Text].c_fg : 0;
		else if (setting == "ColorTextBackground")
			result = Psettings ? Psettings->m_colors[C_Text].c_bg : 0;
		else if (setting == "ColorBraceForeground")
			result = Psettings ? Psettings->m_colors[C_MatchedBrace].c_fg : 0;
		else if (setting == "ColorBraceBackground")
			result = Psettings ? Psettings->m_colors[C_MatchedBrace].c_bg : 0;
		else if (setting == "ColorBraceErrorForeground")
			result = Psettings ? Psettings->m_colors[C_MismatchedBrace].c_fg : 0;
		else if (setting == "ColorBraceErrorBackground")
			result = Psettings ? Psettings->m_colors[C_MismatchedBrace].c_bg : 0;
		else if (setting == "ColorFindRefForeground")
			result = Psettings ? Psettings->m_colors[C_Reference].c_fg : 0;
		else if (setting == "ColorFindRefBackground")
			result = Psettings ? Psettings->m_colors[C_Reference].c_bg : 0;
		else if (setting == "ColorFindRefModForeground")
			result = Psettings ? Psettings->m_colors[C_ReferenceAssign].c_fg : 0;
		else if (setting == "ColorFindRefModBackground")
			result = Psettings ? Psettings->m_colors[C_ReferenceAssign].c_bg : 0;
		else if (setting == "ColorFindResultForeground")
			result = Psettings ? Psettings->m_colors[C_FindResultsHighlight].c_fg : 0;
		else if (setting == "ColorFindResultBackground")
			result = Psettings ? Psettings->m_colors[C_FindResultsHighlight].c_bg : 0;
		else if (setting == "ColorBuildOutput_bg")
			result = Psettings ? Psettings->mColorBuildOutput_CustomBg : DWORD(-1);
		break;

	case 'M':
		if (setting == "MiniHelpHeight")
		{
			// #MiniHelp_Height

			result = 0;  // case: 142774 - changed to zero indicating that minihelp does not exist

			// It is good to wait for a valid handle,
			// because this setting is only used for setting the height of the margin.

			if (Psettings && !Psettings->m_noMiniHelp && g_pMiniHelpFrm->GetSafeHwnd() /*&& g_pMiniHelpFrm->IsWindowVisible() breaks restore after va re-enabled*/)
			{
				result = (uint)Psettings->m_minihelpHeight;
			}
		}
		else if (setting == "MouseClickCmds")
		{
			result = Psettings ? (uint)Psettings->mMouseClickCmds : 0u;
		}
		else if (setting == "MouseWheelCmds")
		{
			result = Psettings ? (uint)Psettings->mMouseWheelCmds : 0u;
		}
		break;
	}

	return result;
}

char* VaService::GetStringSetting(const char* setting) const
{
	if (setting)
	{
		if (!strcmp(setting, "ExtHeader"))
			return Psettings ? ::StrDupA(Psettings->m_hdrExts) : nullptr;
		else if (!strcmp(setting, "ExtShader"))
			return Psettings ? ::StrDupA(Psettings->m_ShaderExts) : nullptr;
	}

	return nullptr;
}

void VaService::ToggleBoolSetting(const char* settingIn)
{
	vLog("VaEventUE ToggleBoolSetting '%s'", settingIn);

	if (!Psettings)
		return;

	const CString setting(settingIn);

	switch (setting[0])
	{
	case 'A':
		if (setting == "ActiveSyntaxColoring")
			Psettings->m_ActiveSyntaxColoring = !Psettings->m_ActiveSyntaxColoring;
		break;

	case 'C':
		if (setting == "CaseCorrect")
			Psettings->CaseCorrect = !Psettings->CaseCorrect;
		else if (setting == "Completions")
		{
			bool newVal = true;

			if (Psettings->m_autoSuggest)
				newVal = false;
			Psettings->m_codeTemplateTooltips = Psettings->m_autoSuggest = newVal;
		}
		break;

	case 'E':
		if (setting == "Enable")
		{
			if (Psettings->m_validLicense)
				Psettings->m_enableVA = !Psettings->m_enableVA;

			extern void EnableVB9Filtering(BOOL enable);

			if (!Psettings->m_enableVA)
				EnableVB9Filtering(true); // re-enable filtering when VA is disabled
		}
		break;

	case 'S':
		if (setting == "SuppressUnderlines")
		{
			Psettings->m_bSupressUnderlines = !Psettings->m_bSupressUnderlines;

			if (!Psettings->m_bSupressUnderlines && g_currentEdCnt)
			{
				// [case: 30569] need to restore underlines
				g_currentEdCnt->KillTimer(ID_UNDERLINE_ERRORS);
				g_currentEdCnt->SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
			}
		}
		break;

	default:
		break;
	}

	::VaOptionsUpdated();
}

DWORD
VaService::GetDwordState(const char* stateIn) const
{
	DWORD result = 0xffffffff;
	const CString state(stateIn);

	switch (state[0])
	{
	case 'I':
		if (state == "IdeVersion")
		{
			result = 0;

			if (gShellAttr)
			{
				// #newVsVersion
				if (gShellAttr->IsDevenv17())
					result = 17;
				else if (gShellAttr->IsDevenv16())
					result = 16;
				else if (gShellAttr->IsDevenv15())
					result = 15;
				else if (gShellAttr->IsDevenv14())
					result = 14;
				else if (gShellAttr->IsDevenv12())
					result = 12;
				else if (gShellAttr->IsDevenv11())
					result = 11;
				else if (gShellAttr->IsDevenv10())
					result = 10;
				else if (gIdeVersion)
				{
					result = gIdeVersion->GetFileMajor();
				}
			}
		}
		else if (state == "IdeVersionMinor")
		{
			result = 0;

			if (gShellAttr)
			{
				// #IdeVersionMinor
				if (gShellAttr->IsDevenv15())
				{
					if (gShellAttr->IsDevenv15u9OrHigher())
						result = 9;
					else if (gShellAttr->IsDevenv15u8OrHigher())
						result = 8;
					else if (gShellAttr->IsDevenv15u7OrHigher())
						result = 7;
					else if (gShellAttr->IsDevenv15u6OrHigher())
						result = 6;
					// all vs versions prior to 15.6 always use 0 for minor version
				}
				else if (gIdeVersion)
				{
					result = gIdeVersion->GetFileMinor();
				}
				// #newVsVersion
			}
		}
		else if (state == "IdeTheme")
		{
			result = 0;
			if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && gColorSyncMgr)
				result = gColorSyncMgr->GetActiveVsTheme();
		}
		break;

	case 'F':
		if (state == "FoundReferenceCount")
		{
			if (gActiveFindRefsResultsFrame)
				result = gActiveFindRefsResultsFrame->GetFoundUsageCount();
			else
				result = 0;
		}
		break;

	case 'S':
		if (state == "ShouldOverrideDefaultIntellisense")
		{
			// case=40505
			EdCntPtr ed(g_currentEdCnt);
			if (::ShouldSuppressVaListbox(ed))
				return 0;

			return 1;
		}
		break;
	}

	return result;
}

DWORD
VaService::QueryStatus(CommandTargetType cmdTarget, DWORD cmdId) const
{
	DWORD enable = 0;
	// -1 : OLECMDF_SUPPORTED | OLECMDF_INVISIBLE
	//  1 : OLECMDF_SUPPORTED | OLECMDF_ENABLED
	//  0 : OLECMDF_SUPPORTED (and disabled)
	if (!Psettings || !Psettings->m_enableVA)
		return enable;

	switch (cmdTarget)
	{
	case ct_editor:
		if (g_currentEdCnt)
			enable = g_currentEdCnt->QueryStatus(cmdId);
		break;

	case ct_vaview:
		if (IsDynamicCommand(cmdId))
			enable = UINT_MAX; // don't populate
		else if (IsValidUnfocusedEditorCommand(cmdId) && g_currentEdCnt)
			enable = g_currentEdCnt->QueryStatus(cmdId);
		else if (IsEditorSpecificCommand(cmdId))
			enable = 0; // not enabled
		else if (IsRefactorCommand(cmdId))
			enable = VAWorkspaceViews::QueryStatus(cmdId);
		else
			enable = (DWORD)-2; // ask someone else
		break;

	case ct_outline:
		if (IsDynamicCommand(cmdId))
			enable = UINT_MAX;
		else if (IsValidUnfocusedEditorCommand(cmdId) && g_currentEdCnt)
			enable = g_currentEdCnt->QueryStatus(cmdId);
		else if (IsEditorSpecificCommand(cmdId))
			enable = 0;
		else if (mOutlineFrame)
			enable = mOutlineFrame->QueryStatus(cmdId);
		break;

	case ct_tracePane:
		if (mTraceFrame)
			enable = mTraceFrame->QueryStatus(cmdId);
		break;

	case ct_hashtags:
		enable = TRUE;
		// 		if (mHashtagsFrame)
		// 			enable = mHashtagsFrame->QueryStatus(cmdId);
		break;

	case ct_findRefResults:
		if (icmdVaCmd_RefResultsFocus == cmdId && gShellAttr->IsMsdev())
			enable = 1;
		else if (icmdVaCmd_RefResultsClone == cmdId && gPrimaryFindRefsResultsFrame)
			enable = gPrimaryFindRefsResultsFrame->QueryStatus(cmdId);
		else if (gActiveFindRefsResultsFrame)
			enable = gActiveFindRefsResultsFrame->QueryStatus(cmdId);
		break;

	case ct_refactor: {
		if (!g_currentEdCnt)
		{
			// hide disabled refactor command so that ide context menu is not polluted if va
			// has not attached to active document.
			enable = UINT_MAX;
			break;
		}

		RefactorFlag flg = VARef_Count;
		switch (cmdId)
		{
		case icmdVaCmd_FindReferences:
		case icmdVaCmd_FindReferencesInFile:
			// Always enable find references unless find references is running.
			// Reduces the number of spurious calls made to Scope() during IDE UI update.
			if (gPrimaryFindRefsResultsFrame && !gPrimaryFindRefsResultsFrame->QueryStatus(cmdId))
				enable = 0;
			else
				enable = 1;
			break;
		case icmdVaCmd_RefactorPopupMenu:
			enable = g_currentEdCnt != NULL;
			break;
		case icmdVaCmd_RefactorModifyExpression:
			enable = gShellAttr && gShellAttr->IsDevenv10OrHigher() && CanSmartSelect(cmdId);
			break;
		case icmdVaCmd_RefactorRename:
			flg = VARef_Rename;
			break;
		case icmdVaCmd_RefactorExtractMethod:
			flg = VARef_ExtractMethod;
			break;
		case icmdVaCmd_RefactorEncapsulateField:
			flg = VARef_EncapsulateField;
			break;
		case icmdVaCmd_RefactorMoveImplementation:
			flg = VARef_MoveImplementationToSrcFile;
			break;
		case icmdVaCmd_RefactorDocumentMethod:
			flg = VARef_CreateMethodComment;
			break;
		case icmdVaCmd_RefactorCreateImplementation:
			flg = VARef_CreateMethodImpl;
			break;
		case icmdVaCmd_RefactorCreateDeclaration:
			flg = VARef_CreateMethodDecl;
			break;
		case icmdVaCmd_RefactorAddMember:
			flg = VARef_AddMember;
			break;
		case icmdVaCmd_RefactorAddSimilarMember:
			flg = VARef_AddSimilarMember;
			break;
		case icmdVaCmd_RefactorChangeSignature:
			flg = VARef_ChangeSignature;
			break;
		case icmdVaCmd_RefactorChangeVisibility:
			flg = VARef_ChangeVisibility;
			break;
		case icmdVaCmd_RefactorAddInclude:
			flg = VARef_AddInclude;
			break;
		case icmdVaCmd_RefactorCreateFromUsage:
			flg = VARef_CreateFromUsage;
			break;
		case icmdVaCmd_RefactorImplementInterface:
			flg = VARef_ImplementInterface;
			break;
		case icmdVaCmd_RefactorExpandMacro:
			flg = VARef_ExpandMacro;
			break;
		case icmdVaCmd_RefactorRenameFiles:
			flg = VARef_RenameFilesFromMenuCmd;
			break;
		case icmdVaCmd_RefactorCreateFile:
			flg = VARef_CreateFile;
			break;
		case icmdVaCmd_RefactorMoveSelToNewFile:
			flg = VARef_MoveSelectionToNewFile;
			break;
		case icmdVaCmd_RefactorIntroduceVariable:
			flg = VARef_IntroduceVariable;
			break;
		case icmdVaCmd_RefactorAddRemoveBraces:
			flg = VARef_AddRemoveBraces;
			break;
		case icmdVaCmd_RefactorAddBraces:
			flg = VARef_AddBraces;
			break;
		case icmdVaCmd_RefactorRemoveBraces:
			flg = VARef_RemoveBraces;
			break;
		case icmdVaCmd_RefactorCreateMissingCases:
			flg = VARef_CreateMissingCases;
			break;
		case icmdVaCmd_RefactorMoveImplementationToHdr:
			flg = VARef_MoveImplementationToHdrFile;
			break;
		case icmdVaCmd_RefactorConvertBetweenPointerAndInstance:
			flg = VARef_ConvertBetweenPointerAndInstance;
			break;
		case icmdVaCmd_RefactorSimplifyInstanceDeclaration:
			flg = VARef_SimplifyInstance;
			break;
		case icmdVaCmd_RefactorAddForwardDeclaration:
			flg = VARef_AddForwardDeclaration;
			break;
		case icmdVaCmd_RefactorConvertEnum:
			flg = VARef_ConvertEnum;
			break;
		case icmdVaCmd_RefactorMoveClassToNewFile:
			flg = VARef_MoveClassToNewFile;
			break;
		case icmdVaCmd_RefactorSortClassMethods:
			flg = VARef_SortClassMethods;
			break;
		}

		if (flg != VARef_Count)
		{
			enable = (DWORD)::CanRefactor(flg);

			if (!enable && !::DisplayDisabledRefactorCommand(flg))
				enable = UINT_MAX; // hide disabled command
		}
	}
	break;
	case ct_help:
		switch (cmdId)
		{
		case icmdVaCmd_HelpDocumentation:
		case icmdVaCmd_HelpTipOfTheDay:
		case icmdVaCmd_HelpAboutVA:
			enable = 1;
			break;

		case icmdVaCmd_LicenseCheckout:
		case icmdVaCmd_LicenseCheckin:
#if defined(SANCTUARY)
			if (gVaLicensingHost)
				enable = gVaLicensingHost->LicenseCommandQueryStatus(
				    icmdVaCmd_LicenseCheckin == cmdId ? IVaLicensing::cmdCheckin : IVaLicensing::cmdCheckout);
			else
#endif
				enable = UINT_MAX; // hide disabled command
			break;

		case icmdVaCmd_HelpDiscord:
		case icmdVaCmd_HelpTechSupport:
		case icmdVaCmd_HelpEnterKey:
		case icmdVaCmd_HelpCheckForNewVersion:
		case icmdVaCmd_HelpTop10Features:
		case icmdVaCmd_HelpWhatIsNew:
		case icmdVaCmd_HelpEMailLicenseAndActivationSupport:
#if defined(AVR_STUDIO)
			enable = UINT_MAX; // hide disabled command
#else
			enable = 1;
#endif
			break;

#if defined(AVR_STUDIO)
		case icmdVaCmd_HelpSubmitRequest:
		case icmdVaCmd_HelpPurchaseLicense:
		case icmdVaCmd_HelpRenewMaintenance:
			enable = UINT_MAX; // hide disabled command
			break;
#else
#if !defined(VA_CPPUNIT)
		case icmdVaCmd_HelpSubmitRequest:
			_ASSERTE(gVaLicensingHost);
			enable = gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdSubmitHelp);
			break;

		case icmdVaCmd_HelpPurchaseLicense:
			_ASSERTE(gVaLicensingHost);
			enable = gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdPurchase);
			break;

		case icmdVaCmd_HelpRenewMaintenance:
			_ASSERTE(gVaLicensingHost);
			enable = gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdRenew);
			break;
#endif
#endif
		default:
			_ASSERTE(!"VaService::QueryStatus unknown cmdId for ct_help");
			enable = 0;
		}
		break;
	case ct_global:
		switch (cmdId)
		{
		case icmdVaCmd_Options:
		case icmdVaCmd_OpenFileInWorkspaceDlg:
		case icmdVaCmd_FindSymbolDlg:
		case icmdVaCmd_ReloadSolution:
			enable = 1;
			break;
		case icmdVaCmd_ViewFilesInWorkspace:
		case icmdVaCmd_ViewSymbolsInWorkspace:
		case icmdVaCmd_VaViewHcb:
		case icmdVaCmd_VaViewMru:
		case icmdVaCmd_VaViewToggleHcbLock:
		case icmdVaCmd_NavigateBack:
		case icmdVaCmd_NavigateForward:
		case icmdPkgCmd_ShowVaOutlineWindow:
		case icmdPkgCmd_ShowTraceWindow:
		case icmdPkgCmd_ShowVaHashtagsWindow:
#if defined(RAD_STUDIO)
			enable = UINT_MAX; // hide disabled command
#else
			enable = 1;
#endif
			break;
		case icmdVaCmd_AutotextEdit:
		case icmdVaCmd_EditRefactorSnippets:
			enable = gAutotextMgr != NULL;
			break;
		case icmdVaCmd_ShortcutsDialog:
#if !defined(RAD_STUDIO)
			enable = ::QueryStatusVaKeyBindingsDialog();
#endif
			break;
		}
		break;
	default:
		_ASSERTE(!"VaService::QueryStatus unknown target");
	}

	return enable;
}

HRESULT
VaService::Exec(CommandTargetType cmdTarget, DWORD cmdId)
{
	vLog("VaS::Exec %x %lx", cmdTarget, cmdId);
	ScopedIncrement si(&gExecActive);
	HRESULT hr = S_OK;
	if (!Psettings || !Psettings->m_enableVA)
		return hr;

	ScopedValue sv(gCurrExecCmd, cmdId, (DWORD)0);

#if !defined(SEAN)
	try
#endif
	{
		switch (cmdTarget)
		{
		case ct_editor:
			if (g_currentEdCnt)
				hr = g_currentEdCnt->Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_vaview:
			if (IsValidUnfocusedEditorCommand(cmdId) && g_currentEdCnt)
				hr = g_currentEdCnt->Exec(cmdId);
			else if (IsRefactorCommand(cmdId))
				hr = VAWorkspaceViews::Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_outline:
			if (IsValidUnfocusedEditorCommand(cmdId) && g_currentEdCnt)
				hr = g_currentEdCnt->Exec(cmdId);
			else if (mOutlineFrame)
				hr = mOutlineFrame->Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_tracePane:
			if (mTraceFrame)
				hr = mTraceFrame->Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_hashtags:
			if (mHashtagsFrame)
				hr = mHashtagsFrame->Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_findRefResults:
			if (icmdVaCmd_RefResultsFocus == cmdId)
			{
				// not expected to be called in VS
				_ASSERTE(!gShellAttr->IsDevenv());
				::Vc6RefResultsFocus();
			}
			else if (icmdVaCmd_RefResultsClone == cmdId && gPrimaryFindRefsResultsFrame)
			{
				gActiveFindRefsResultsFrame = NULL;
				if (gShellAttr->IsMsdev())
				{
					// deleted by the results frame that it creates
					new Vc6RefResultsCloneToolwnd(gPrimaryFindRefsResultsFrame);
				}
				else
					gPrimaryFindRefsResultsFrame->Exec(cmdId);

				_ASSERTE(gActiveFindRefsResultsFrame);
			}
			else if (gActiveFindRefsResultsFrame)
				gActiveFindRefsResultsFrame->Exec(cmdId);
			else
				hr = E_UNEXPECTED;
			break;
		case ct_refactor: {
			static const char kCantFindRefs[] = "Find References is not available because the symbol is unrecognized.";
			RefactorFlag flg = VARef_Count;
			switch (cmdId)
			{
			case icmdVaCmd_RefactorPopupMenu:
				if (gVaInteropService && Psettings->mCodeInspection)
					gVaInteropService->DisplayQuickActionMenu(false);
				else if (g_ScreenAttrs.m_VATomatoTip)
					g_ScreenAttrs.m_VATomatoTip->DisplayTipContextMenu(false);
				break;
			case icmdVaCmd_FindReferences:
			case icmdVaCmd_FindReferencesInFile: {
				RefactorFlag tmpFlg;
				switch (cmdId)
				{
				default:
					_ASSERTE(!"update cmdId switch in VaService.Exec");
				case icmdVaCmd_FindReferences:
					tmpFlg = VARef_FindUsage;
					break;
				case icmdVaCmd_FindReferencesInFile:
					tmpFlg = VARef_FindUsageInFile;
					break;
				}

				// confirm cmd is allowed before actually executing
				// since QueryStatus always returns enabled.
				if (CanRefactor(tmpFlg))
					flg = tmpFlg;
				/*else if (Psettings && Psettings->mNoisyExecCommandFailureNotifications)
				{
					if (gTestLogger)
					{
						WTString msg;
						msg.WTFormat("MsgBox: %s\r\n", kCantFindRefs);
						gTestLogger->LogStr(msg);
					}
					else
						WtMessageBox(kCantFindRefs, IDS_APPNAME, MB_OK | MB_ICONERROR);
				}*/
				else
				{
					WTString msg;
					msg.WTFormat("Status: %s\r\n", kCantFindRefs);

					if (gTestLogger)
						gTestLogger->LogStr(msg);

					SetStatus(kCantFindRefs);
					VALOGERROR(msg.c_str());
					
// 					::MessageBeep(0xffffffff);
				}
			}
			break;
			case icmdVaCmd_RefactorModifyExpression:
				SmartSelect(cmdId);
				break;
			case icmdVaCmd_RefactorRename:
				flg = VARef_Rename;
				break;
			case icmdVaCmd_RefactorExtractMethod:
				flg = VARef_ExtractMethod;
				break;
			case icmdVaCmd_RefactorEncapsulateField:
				flg = VARef_EncapsulateField;
				break;
			case icmdVaCmd_RefactorMoveImplementation:
				flg = VARef_MoveImplementationToSrcFile;
				break;
			case icmdVaCmd_RefactorDocumentMethod:
				flg = VARef_CreateMethodComment;
				break;
			case icmdVaCmd_RefactorCreateImplementation:
				flg = VARef_CreateMethodImpl;
				break;
			case icmdVaCmd_RefactorCreateDeclaration:
				flg = VARef_CreateMethodDecl;
				break;
			case icmdVaCmd_RefactorAddMember:
				flg = VARef_AddMember;
				break;
			case icmdVaCmd_RefactorAddSimilarMember:
				flg = VARef_AddSimilarMember;
				break;
			case icmdVaCmd_RefactorChangeSignature:
				flg = VARef_ChangeSignature;
				break;
			case icmdVaCmd_RefactorChangeVisibility:
				flg = VARef_ChangeVisibility;
				break;
			case icmdVaCmd_RefactorAddInclude:
				flg = VARef_AddInclude;
				break;
			case icmdVaCmd_RefactorCreateFromUsage:
				flg = VARef_CreateFromUsage;
				break;
			case icmdVaCmd_RefactorImplementInterface:
				flg = VARef_ImplementInterface;
				break;
			case icmdVaCmd_RefactorExpandMacro:
				flg = VARef_ExpandMacro;
				break;
			case icmdVaCmd_RefactorRenameFiles:
				flg = VARef_RenameFilesFromMenuCmd;
				break;
			case icmdVaCmd_RefactorCreateFile:
				flg = VARef_CreateFile;
				break;
			case icmdVaCmd_RefactorMoveSelToNewFile:
				flg = VARef_MoveSelectionToNewFile;
				break;
			case icmdVaCmd_RefactorIntroduceVariable:
				flg = VARef_IntroduceVariable;
				break;
			case icmdVaCmd_RefactorAddRemoveBraces:
				flg = VARef_AddRemoveBraces;
				break;
			case icmdVaCmd_RefactorAddBraces:
				flg = VARef_AddBraces;
				break;
			case icmdVaCmd_RefactorRemoveBraces:
				flg = VARef_RemoveBraces;
				break;
			case icmdVaCmd_RefactorCreateMissingCases:
				flg = VARef_CreateMissingCases;
				break;
			case icmdVaCmd_RefactorMoveImplementationToHdr:
				flg = VARef_MoveImplementationToHdrFile;
				break;
			case icmdVaCmd_RefactorConvertBetweenPointerAndInstance:
				flg = VARef_ConvertBetweenPointerAndInstance;
				break;
			case icmdVaCmd_RefactorSimplifyInstanceDeclaration:
				flg = VARef_SimplifyInstance;
				break;
			case icmdVaCmd_RefactorAddForwardDeclaration:
				flg = VARef_AddForwardDeclaration;
				break;
			case icmdVaCmd_RefactorConvertEnum:
				flg = VARef_ConvertEnum;
				break;
			case icmdVaCmd_RefactorMoveClassToNewFile:
				flg = VARef_MoveClassToNewFile;
				break;
			case icmdVaCmd_RefactorSortClassMethods:
				flg = VARef_SortClassMethods;
				break;
			default:
				hr = E_UNEXPECTED;
			}

			if (flg != VARef_Count)
				Refactor(flg);
		}
		break;
		case ct_help:
			switch (cmdId)
			{
#if defined(AVR_STUDIO)
			case icmdVaCmd_HelpDocumentation:
				::ShellExecute(NULL, _T("open"), "https://www.microchip.com/webdoc/visualassist/index.html", NULL, NULL,
				               SW_SHOW);
				break;
#else
			case icmdVaCmd_HelpDocumentation:
				::ShellExecute(NULL, _T("open"), "http://www.wholetomato.com/support/tooltip.asp?option=HelpDocs", NULL,
				               NULL, SW_SHOW);
				break;
			case icmdVaCmd_HelpTop10Features:
				::ShellExecute(NULL, _T("open"), "https://www.wholetomato.com/support/tooltip.asp?option=top10", NULL,
				               NULL, SW_SHOW);
				break;
			case icmdVaCmd_HelpDiscord:
				::ShellExecute(NULL, _T("open"), "https://discord.gg/mxQqAj5PHQ",
				               NULL, NULL, SW_SHOW);
				break;
			case icmdVaCmd_HelpTechSupport:
				EmailToTechnicalSupport();
				break;
			case icmdVaCmd_HelpEMailLicenseAndActivationSupport:
				EMailToLicenseSupport();
				break;
			case icmdVaCmd_HelpWhatIsNew: {
				if (mWhatIsNewUrl.IsEmpty())
				{
					CString__FormatA(mWhatIsNewUrl, TEXT("https://www.wholetomato.com/features/whats-new.aspx?v=%u"),
					                 VA_VER_BUILD_NUMBER);

					if (gVaLicensingHost)
					{
						const CString exp(gVaLicensingHost->GetLicenseExpirationDate());
						if (!exp.IsEmpty())
							CString__AppendFormatA(mWhatIsNewUrl, _T("&e=%s"), (LPCTSTR)exp);
					}
				}

				::ShellExecute(nullptr, _T("open"), (LPCTSTR)mWhatIsNewUrl, nullptr, nullptr, SW_SHOW);
				break;
			}
			case icmdVaCmd_HelpSubmitRequest:
				EmailToTechnicalSupportFeatureRequest();
				break;
			case icmdVaCmd_HelpRenewMaintenance:
				::RenewMaintenance();
				break;
#endif // !AVR_STUDIO

#if !defined(VA_CPPUNIT)
#if !defined(AVR_STUDIO)
			case icmdVaCmd_HelpPurchaseLicense:
				_ASSERTE(gVaLicensingHost);
				if (1 == gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdPurchase))
				{
					if (gVaLicensingHost->IsNonRenewableLicenseInstalled())
						::ShellExecute(NULL, _T("open"), "http://www.wholetomato.com/purchase/default.asp", NULL, NULL,
						               SW_SHOW);
					else
						::ShellExecute(NULL, _T("open"), "http://www.wholetomato.com/purchase/default.asp?v=1", NULL,
						               NULL, SW_SHOW);
				}
				break;
			case icmdVaCmd_LicenseCheckout:
			case icmdVaCmd_LicenseCheckin:
#if defined(SANCTUARY)
				if (gVaLicensingHost)
					gVaLicensingHost->LicenseCommandExec(icmdVaCmd_LicenseCheckin == cmdId ? IVaLicensing::cmdCheckin
					                                                                       : IVaLicensing::cmdCheckout);
#endif
				break;
			case icmdVaCmd_HelpEnterKey: {
#if defined(SANCTUARY)
				CRegisterSanctuary reg(gMainWnd);
#else
				CRegisterArmadillo reg(gMainWnd);
#endif
				reg.DoModal();
			}
			break;
			case icmdVaCmd_HelpCheckForNewVersion:
#if !defined(RAD_STUDIO)
				::CheckForLatestVersion(TRUE);
#endif
				break;
#endif
			case icmdVaCmd_HelpTipOfTheDay:
				CTipOfTheDay::LaunchTipOfTheDay(TRUE);
				break;
#endif // !VA_CPPUNIT
			case icmdVaCmd_HelpAboutVA:
				::DisplayAboutDlg();
				break;
			default:
				_ASSERTE(!"VaService::Exec unknown cmdId for ct_help");
				hr = E_UNEXPECTED;
			}
			break;
		case ct_global:
			switch (cmdId)
			{
			case icmdVaCmd_Options:
				g_inMacro = false;
				if (!gTestsActive)
					::DoVAOptions(nullptr); // open to last active tab
				else
				{
					if (!gTestVaOptionsPage.IsEmpty())
						::DoVAOptions(gTestVaOptionsPage); // open to AST specified page
					else
						::DoVAOptions(nullptr); // open to last active tab
				}

#if !defined(RAD_STUDIO)
				extern void SendMessageToAllEdcnt(UINT Msg, WPARAM wParam, LPARAM lParam);
				SendMessageToAllEdcnt(WM_COMMAND, WM_SIZE, 0);
#endif
				break;
			case icmdVaCmd_ReloadSolution:
				if (GlobalProject)
				{
					// [case: 142332]
					GlobalProject->SolutionLoadStarting();
					GlobalProject->SolutionLoadCompleted(true);
				}
				break;
			case icmdVaCmd_ShortcutsDialog:
#if !defined(RAD_STUDIO)
				::ShowVAKeyBindingsDialog();
#endif
				break;
			case icmdVaCmd_OpenFileInWorkspaceDlg:
				::VAOpenFileDlg();
				break;
			case icmdVaCmd_FindSymbolDlg:
				::VABrowseSymDlg();
				break;
			case icmdVaCmd_VaViewToggleHcbLock:
				if (g_CVAClassView)
					g_CVAClassView->OnToggleSymbolLock();
				break;
			case icmdVaCmd_VaViewHcb:
				VAWorkspaceViews::GotoHcb();
				break;
			case icmdVaCmd_VaViewMru:
				VAWorkspaceViews::GotoMru();
				break;
			case icmdVaCmd_ViewFilesInWorkspace:
				VAWorkspaceViews::GotoFilesInWorkspace();
				break;
			case icmdVaCmd_ViewSymbolsInWorkspace:
				VAWorkspaceViews::GotoSymbolsInWorkspace();
				break;
			case icmdVaCmd_NavigateForward:
				::NavGo(FALSE);
				break;
			case icmdVaCmd_NavigateBack:
				::NavGo(TRUE);
				break;
			case icmdVaCmd_AutotextEdit:
				gAutotextMgr->Edit(gTypingDevLang);
				break;
			case icmdVaCmd_EditRefactorSnippets:
				gAutotextMgr->EditRefactorSnippets(gTypingDevLang);
				break;
			case icmdPkgCmd_ShowVaOutlineWindow:
				_ASSERTE(gShellAttr->IsMsdevOrCppBuilder());
				VAWorkspaceViews::GotoVaOutline();
				break;
			case icmdPkgCmd_ShowVaHashtagsWindow:
				_ASSERTE(gShellAttr->IsMsdev());
				_ASSERTE(!"not implemented");
				// VAWorkspaceViews::GotoVaHashtags();
				break;
			default:
				if (icmdVaCmd_DynamicSelectionFirst <= cmdId && icmdVaCmd_DynamicSelectionLast > cmdId)
				{
					return ExecDynamicSelectionCommand(cmdId);
				}

				hr = E_UNEXPECTED;
			}
			break;
		default:
			_ASSERTE(!"VaService::Exec unknown target");
			hr = E_UNEXPECTED;
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("VAS:");
		WTString errMsg;
		errMsg.WTFormat("VaService::Exec Exception %x %lx", cmdTarget, cmdId);
		VALOGERROR(errMsg.c_str());
		_ASSERTE(
		    !"caught exception during user invoked command - this is bad, especially if edit was supposed to occur");
	}
#endif

	return hr;
}

void VaService::FindReferencesPaneCreated(HWND hWnd)
{
	gShellSvc->SetFindReferencesWindow(hWnd);
	// gFindReferencesResultsFrame is invalid/incorrect at this point
}

void VaService::FindReferencesFrameCreated(HWND refWnd, FindReferencesResultsFrame* frm)
{
	FindReferencesResultsFrame* oldFrm = mFindRefPanes[refWnd];
	_ASSERTE(!oldFrm);
	std::ignore = oldFrm;
	mFindRefPanes[refWnd] = frm;
}

void VaService::FindReferencesPaneActivated(HWND hWnd)
{
	gActiveFindRefsResultsFrame = mFindRefPanes[hWnd];
	gShellSvc->SetFindReferencesWindow(hWnd);

	if (gActiveFindRefsResultsFrame && ::IsWindow(gActiveFindRefsResultsFrame->m_hWnd))
		gActiveFindRefsResultsFrame->FocusFoundUsage();
}

bool VaService::FindReferencesPaneDestroyed(HWND hWnd)
{
	FindReferencesResultsFrame* frm = mFindRefPanes[hWnd];
	// This assert will happen if you have a find ref results window that
	// was created by the IDE at startup but without having run find refs.
	// The toolwindow gets created, but we didn't do anything with it -
	// so it doesn't appear in mFindRefPanes.
	//	_ASSERTE(gShellIsUnloading || (hWnd && frm));
	if (frm == gActiveFindRefsResultsFrame)
		gActiveFindRefsResultsFrame = NULL;
	if (frm == gPrimaryFindRefsResultsFrame)
		gPrimaryFindRefsResultsFrame = NULL;

	::DeleteSpentFindReferencesThreads();
	mFindRefPanes.erase(hWnd);

	if (frm)
	{
		// doing this will prevent the scary CWnd messages about Destroy
		// not being called during the delete call, but it screws up something
		// else such that reconstruction of the next find refs run fails.
		// 		if (frm->GetSafeHwnd() && ::IsWindow(frm->GetSafeHwnd()))
		// 			frm->DestroyWindow();
		delete frm;
	}

	return frm != NULL;
}

void VaService::FindReferencesClonePaneCreated(HWND hWnd, UINT /*windowId*/)
{
	if (gShellAttr->IsMsdev())
		return;

	_ASSERTE(gPrimaryFindRefsResultsFrame);

	if (!gPrimaryFindRefsResultsFrame)
		return;

	gShellSvc->SetFindReferencesWindow(hWnd);
	gActiveFindRefsResultsFrame = new SecondaryResultsFrameVs(gPrimaryFindRefsResultsFrame);
	_ASSERTE(mFindRefPanes[hWnd] == gActiveFindRefsResultsFrame);
}

void VaService::VaViewPaneCreated(HWND hWnd)
{
	if (gShellAttr->IsDevenv())
		VAWorkspaceViews::CreateWorkspaceView(hWnd);
}

void VaService::VaViewActivated() const
{
	VAWorkspaceViews::Activated();
}

void VaService::VaViewDestroyed()
{
}

void VaService::LiveOutlineCreated(HWND hWnd)
{
	_ASSERTE(!mOutlineFrame);
	mOutlineFrame = new LiveOutlineFrame(hWnd);
}

void VaService::LiveOutlineActivated() const
{
	_ASSERTE(mOutlineFrame);

	if (mOutlineFrame)
		mOutlineFrame->IdePaneActivated();
}

void VaService::LiveOutlineDestroyed()
{
	delete mOutlineFrame;
	mOutlineFrame = NULL;
}

void VaService::HashtagsPaneCreated(HWND hWnd)
{
	_ASSERTE(!mHashtagsFrame);
	mHashtagsFrame = new VaHashtagsFrame(hWnd);
}

void VaService::HashtagsPaneActivated() const
{
	_ASSERTE(mHashtagsFrame);
	if (mHashtagsFrame)
		mHashtagsFrame->IdePaneActivated();
}

void VaService::HashtagsPaneDestroyed()
{
	delete mHashtagsFrame;
	mHashtagsFrame = NULL;
}

void VaService::TracePaneCreated(HWND hWnd)
{
	_ASSERTE(!mTraceFrame);
	mTraceFrame = new TraceWindowFrame(hWnd);
}

void VaService::TracePaneActivated() const
{
	_ASSERTE(mTraceFrame);

	if (mTraceFrame)
		mTraceFrame->IdePaneActivated();
}

void VaService::TracePaneDestroyed()
{
	delete mTraceFrame;
	mTraceFrame = NULL;
}

static CStringW sParseFileList;
void CALLBACK ParseTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	if (RefactoringActive::IsActive())
		return;

	::KillTimer(hWnd, idEvent);
	TokenW lst = sParseFileList;

	while (lst.more() > 1)
	{
		CStringW filename = lst.read(L";");
		int ftype = GetFileType(filename);

		if (
#if !defined(RAD_STUDIO)
			    gVaShellService && 
#endif
			    Is_C_CS_VB_File(ftype) && !ShouldIgnoreFile(filename, true))
		{
			const WTString txt(PkgGetFileTextW(filename));
			g_ParserThread->QueueFile(filename, txt, NULL);
		}
	}

	sParseFileList.Empty();
}

void VaService::DocumentModified(const WCHAR* filenameIn, int startLineNo, int startLineIdx, int oldEndLineNo,
                                 int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo)
{
	static CStringW sLastFilename;
	const CStringW filename(filenameIn);

	try
	{
		// #cppbTODO EditorFileChanged incompatible with FindReferences -- all of the params except for first are bogus
		for (auto & mFindRefPane : mFindRefPanes)
		{
			FindReferencesResultsFrame* refs = mFindRefPane.second;

			if (refs)
				refs->DocumentModified(filenameIn, startLineNo, startLineIdx, oldEndLineNo, oldEndLineIdx, newEndLineNo, newEndLineIdx, editNo);
		}

		// don't start ParseThrd before project loads, else hilarity ensues
		if (!GlobalProject || GlobalProject->IsBusy() || !::IsAnySysDicLoaded() || !GlobalProject->GetFileItemCount())
		{
			sLastEdCnt = nullptr;
			return;
		}

		if (sLastEdCnt && sLastFilename == filename)
		{
			try
			{
				if (!::IsWindow(sLastEdCnt->GetSafeHwnd()))
					return;
			}
			catch (...)
			{
			}
		}

		sLastEdCnt = nullptr;
		BOOL multiView = FALSE;

		{
			AutoLockCs l(g_EdCntListLock);
			for (const EdCntPtr& cur : g_EdCntList)
			{
				if (_wcsicmp(cur->FileName(), filename) == 0)
				{
#if defined(RAD_STUDIO)
					cur->SetBufState(CTer::BUF_STATE_WRONG);
					const BOOL doAsap = cur == g_currentEdCnt;
#else
					if (newEndLineNo == startLineNo && newEndLineNo == oldEndLineNo)
						cur->SetBufState(CTer::BUF_STATE_DIRTY);
					else
						cur->SetBufState(CTer::BUF_STATE_WRONG);
					constexpr BOOL doAsap = FALSE;
#endif

					// send timer so it will reget buffer
					cur->OnModified(doAsap);

					if (sLastEdCnt == nullptr)
					{
						// save for next notification
						sLastEdCnt = cur;
						sLastFilename = filename;
					}
					else
					{
						// more than one view of same doc - don't cache
						multiView = TRUE;
						sLastEdCnt = nullptr;
					}
				}
			}
		}

		if (!sLastEdCnt && !multiView)
		{
#if defined(RAD_STUDIO)
			FileModificationState state = fm_dirty;
#else
			// Goto seems to hit here before the file is opened and VA'd
			// Only do a reparse if the file is modified.
			FileModificationState state = ::PkgGetFileModState(filename);
#endif

			if (fm_dirty == state)
			{
				// Modified file that does not have an EdCnt
				if (sParseFileList.Find(filename) == -1)
					sParseFileList += (filename + L';');
				::KillTimer(NULL, ID_DOC_MODIFIED_TIMER);
				::SetTimer(NULL, ID_DOC_MODIFIED_TIMER, 500, ParseTimerProc);
			}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAS:DM:");
		sLastEdCnt = NULL;
	}

#if defined(_DEBUG)
	CString msg;
	CString__FormatA(msg, "VaService::DocModified %s %d %d %d\n", (LPCTSTR)CString(filename), startLineNo, oldEndLineNo, newEndLineNo);
	::OutputDebugString(msg);
#endif // _DEBUG
}

void VaService::DocumentSaved(const WCHAR* filename)
{
	for (FindReferencesWndMap::iterator mapIt = mFindRefPanes.begin(); mapIt != mFindRefPanes.end(); ++mapIt)
	{
		FindReferencesResultsFrame* refs = (*mapIt).second;

		if (refs)
			refs->DocumentSaved(filename);
	}
}

void VaService::DocumentClosed(const WCHAR* filename)
{
	for (FindReferencesWndMap::iterator mapIt = mFindRefPanes.begin(); mapIt != mFindRefPanes.end(); ++mapIt)
	{
		FindReferencesResultsFrame* refs = (*mapIt).second;

		if (refs)
			refs->DocumentClosed(filename);
	}
}

void VaService::DocumentSavedAs(const WCHAR* newName, const WCHAR* oldName)
{
#if defined(_DEBUG)
	CString msg;
	CString__FormatA(msg, "VaService::DocSavedAs new(%s) old(%s)\n", (LPCTSTR)CString(newName), (LPCTSTR)CString(oldName));
	::OutputDebugString(msg);
#endif // _DEBUG

	if (g_ParserThread && oldName)
	{
		const CStringW oldFile(oldName);
		if (oldFile.Find(L"/~") != -1 || oldFile.Find(L"\\~") != -1)
			g_ParserThread->QueueParseWorkItem(new SymbolRemover(oldFile));
	}
}

void CALLBACK VaService::FindReferencesCallback(HWND hWnd, UINT ignore1, UINT_PTR idEvent, DWORD ignore2)
{
	if (idEvent)
		KillTimer(hWnd, idEvent);

	if (!gPrimaryFindRefsResultsFrame)
		return;

	if (gPrimaryFindRefsResultsFrame->IsThreadRunning())
	{
		// thread is still running, repeat call
		SetTimer(NULL, 0, 250, (TIMERPROC)&FindReferencesCallback);
		return;
	}

	StartFindReferences(sFindReferenceParameters.flags, sFindReferenceParameters.typeImageIdx,
	                    sFindReferenceParameters.symScope);
}

void VaService::StartFindReferences(int flags, int typeImageIdx, WTString symScope)
{
	_ASSERTE(!gPrimaryFindRefsResultsFrame || !gPrimaryFindRefsResultsFrame->IsThreadRunning());
	HWND freezeWnd = nullptr;
	FindReferencesResultsFrame* oldFrm = gPrimaryFindRefsResultsFrame;

	if (oldFrm)
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		{
			// [case: 74848]
			freezeWnd = oldFrm->GetSafeHwnd();

			if (freezeWnd)
				::LockWindowUpdate(freezeWnd);
		}

		if (oldFrm->GetSafeHwnd() && gVaService)
			gVaService->FindReferencesPaneDestroyed(oldFrm->GetSafeHwnd());
	}

	SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("VAssistX.FindReferencesResults"), 0);

	if (gShellAttr->IsMsdev())
		gActiveFindRefsResultsFrame = gPrimaryFindRefsResultsFrame =
		    new PrimaryResultsFrameVc6(flags, typeImageIdx, symScope.IsEmpty() ? nullptr : symScope.c_str());
	else
		gActiveFindRefsResultsFrame = gPrimaryFindRefsResultsFrame =
		    new PrimaryResultsFrameVs(flags, typeImageIdx, symScope.IsEmpty() ? nullptr : symScope.c_str());

	if (freezeWnd)
		::LockWindowUpdate(NULL);
}

void VaService::FindReferences(int flags, int typeImageIdx, WTString symScope)
{
	if (gPrimaryFindRefsResultsFrame && !gPrimaryFindRefsResultsFrame->QueryStatus(icmdVaCmd_FindReferences))
		return;

	if (gPrimaryFindRefsResultsFrame && gPrimaryFindRefsResultsFrame->IsThreadRunning())
	{
		// [case: 141731]
		// stop the running instance
		gPrimaryFindRefsResultsFrame->Cancel();

		// store find reference parameters in a static member for use from callback timer
		sFindReferenceParameters.flags = flags;
		sFindReferenceParameters.typeImageIdx = typeImageIdx;
		sFindReferenceParameters.symScope = symScope;

		// set up the timer to call Find References again
		SetTimer(NULL, 0, 250, (TIMERPROC)&FindReferencesCallback);

		return;
	}

	StartFindReferences(flags, typeImageIdx, symScope);
}

void VaService::DisplayReferences(const ::FindReferences& refs)
{
#if defined(RAD_STUDIO)
	return; // Needs tool pane
#else
	if (gPrimaryFindRefsResultsFrame && !gPrimaryFindRefsResultsFrame->QueryStatus(icmdVaCmd_FindReferences))
		return;

	if (gPrimaryFindRefsResultsFrame && gPrimaryFindRefsResultsFrame->IsThreadRunning())
		return; // [case: 141731] retain old behavior for this scenario

	HWND freezeWnd = nullptr;
	FindReferencesResultsFrame* oldFrm = gPrimaryFindRefsResultsFrame;

	if (oldFrm)
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		{
			// [case: 74848]
			freezeWnd = oldFrm->GetSafeHwnd();

			if (freezeWnd)
				::LockWindowUpdate(freezeWnd);
		}

		if (oldFrm->GetSafeHwnd())
			FindReferencesPaneDestroyed(oldFrm->GetSafeHwnd());
	}

	SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("VAssistX.FindReferencesResults"), 0);

	if (gShellAttr->IsMsdev())
		gActiveFindRefsResultsFrame = gPrimaryFindRefsResultsFrame = new PrimaryResultsFrameVc6(refs);
	else
		gActiveFindRefsResultsFrame = gPrimaryFindRefsResultsFrame = new PrimaryResultsFrameVs(refs);

	if (freezeWnd)
		::LockWindowUpdate(NULL);
#endif
}

LPCWSTR
VaService::QueryStatusText(CommandTargetType cmdTarget, DWORD cmdId, DWORD* statusOut)
{
	_ASSERTE(statusOut);

	if (cmdTarget == ct_global && icmdVaCmd_DynamicSelectionFirst <= cmdId && icmdVaCmd_DynamicSelectionLast > cmdId)
	{
		if (!g_currentEdCnt || !gAutotextMgr)
		{
			*statusOut = UINT_MAX;
			return NULL;
		}

		return gAutotextMgr->QueryStatusText(cmdId, statusOut);
	}
	else if (cmdTarget == ct_refactor)
	{
		if (!g_currentEdCnt)
		{
			// hide disabled refactor command so that ide context menu is not polluted if va
			// has not attached to active document.
			*statusOut = UINT_MAX;
			return NULL;
		}

		if (cmdId == icmdVaCmd_RefactorImplementInterface)
		{
			WTString cmdText;
			*statusOut = (DWORD)::CanRefactor(VARef_ImplementInterface, &cmdText);
			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
		else if (cmdId == icmdVaCmd_RefactorAddRemoveBraces)
		{
			WTString cmdText;
			*statusOut = (DWORD)::CanRefactor(VARef_AddRemoveBraces, &cmdText);
			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
		else if (cmdId == icmdVaCmd_RefactorMoveImplementation)
		{
			WTString cmdText;

			if (::CanRefactor(VARef_MoveImplementationToSrcFile, &cmdText))
				*statusOut = TRUE;
			else if (::DisplayDisabledRefactorCommand(VARef_MoveImplementationToSrcFile))
				*statusOut = FALSE;
			else
				*statusOut = UINT_MAX; // hide disabled command

			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
		else if (cmdId == icmdVaCmd_RefactorCreateImplementation)
		{
			WTString cmdText;

			if (::CanRefactor(VARef_CreateMethodImpl, &cmdText))
				*statusOut = TRUE;
			else if (::DisplayDisabledRefactorCommand(VARef_CreateMethodImpl))
				*statusOut = FALSE;
			else
				*statusOut = UINT_MAX; // hide disabled command

			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
		else if (cmdId == icmdVaCmd_RefactorMoveImplementationToHdr)
		{
			WTString cmdText;

			if (::CanRefactor(VARef_MoveImplementationToHdrFile, &cmdText))
				*statusOut = TRUE;
			else if (::DisplayDisabledRefactorCommand(VARef_MoveImplementationToHdrFile))
				*statusOut = FALSE;
			else
				*statusOut = UINT_MAX; // hide disabled command

			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
		else if (cmdId == icmdVaCmd_RefactorConvertBetweenPointerAndInstance)
		{
			WTString cmdText;

			if (::CanRefactor(VARef_ConvertBetweenPointerAndInstance, &cmdText))
				*statusOut = TRUE;
			else if (::DisplayDisabledRefactorCommand(VARef_ConvertBetweenPointerAndInstance))
				*statusOut = FALSE;
			else
				*statusOut = UINT_MAX; // hide disabled command

			static CStringW sCmdText;
			sCmdText = cmdText.Wide();
			return sCmdText;
		}
	}

	*statusOut = UINT_MAX;
	return NULL;
}

HRESULT
VaService::ExecDynamicSelectionCommand(DWORD cmdId)
{
	_ASSERTE(icmdVaCmd_DynamicSelectionFirst <= cmdId && icmdVaCmd_DynamicSelectionLast > cmdId);
	EdCntPtr curEd(g_currentEdCnt);

	if (!curEd || !gAutotextMgr)
		return E_UNEXPECTED;

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		curEd->SetFocusParentFrame(); // case=45591

	return gAutotextMgr->Exec(curEd, cmdId);
}

VaService::WindowPaneType VaService::GetWindowPaneType(LPCSTR paneName)
{
	if (StrCmpA(paneName, "VaView") == 0)
		return wptVaView;

	if (StrCmpA(paneName, "FindReferences") == 0)
		return wptFindRefResults;

	if (StrCmpA(paneName, "FindReferencesClone") == 0)
		return wptFindRefResultsClone;

	if (StrCmpA(paneName, "VaOutline") == 0)
		return wptVaOutline;

	if (StrCmpA(paneName, "VaTrace") == 0)
		return wptTraceWindow;

	if (StrCmpA(paneName, "VaHashtags") == 0)
		return wptHashtags;

	return wptEmpty;
}

VaService::FindReferenceParametersStruct VaService::sFindReferenceParameters;

DWORD
VaService::SetActiveTextView(IVsTextView* textView, MarkerIds* ids)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		if (ids)
		{
			extern MarkerIds kMarkerIds;
			kMarkerIds = *ids;
		}

		if (WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()))
		{
			WPF_ViewManager::Get()->OnSetViewFocus(textView);
			return 1;
		}
	}
	return 0;
}

DWORD
VaService::UpdateActiveTextViewScrollInfo(IVsTextView* textView, long iBar, long iFirstVisibleUnit)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		if (g_currentEdCnt && WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()) && textView == WPF_ViewManager::Get()->GetActiveView())
			::PostMessageA(g_currentEdCnt->GetSafeHwnd(), VAM_OnChangeScrollInfo, (WPARAM)iBar, (LPARAM)iFirstVisibleUnit);

		return 1;  // No need to retry since we get scroll info when we set the view.
	}

	return 0;
}

void VaService::OnSetFocus(/* [in] */ IVsTextView* pView)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()))
		WPF_ViewManager::Get()->OnSetViewFocus(pView);
}

void VaService::OnKillFocus(/* [in] */ IVsTextView* pView)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()))
		WPF_ViewManager::Get()->OnKillViewFocus(pView);
}

void VaService::OnUnregisterView(/* [in] */ IVsTextView* pView)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()))
		WPF_ViewManager::Get()->OnCloseView(pView);
}

bool VaService::ShouldDisplayNavBar(const WCHAR* filePath)
{
	if (mInitAborted)
		return false;

	if (!Psettings || !Psettings->m_validLicense)
		return false;

	const CStringW file(filePath);
	if (!::ShouldFileBeAttachedTo(file))
		return false;

	const int fType = ::GetFileType(file);
	if (::CAN_USE_NEW_SCOPE(fType))
		return true;

	return false;
}

// this is independent of m_bEnhColorSourceWindows.
// need to attach regardless of current state m_bEnhColorSourceWindows so that
// if it is disabled, when enabled, coloring kicks in.
bool VaService::ShouldColor(const WCHAR* filePath)
{
	if (mInitAborted)
		return false;

	const CStringW file(filePath);
	if (!::ShouldFileBeAttachedTo(file))
		return false;

	// don't necessarily color all files that we attach to
	const int fType = ::GetFileType(file);
	if (RC == fType)
		return true;
	if (Is_Some_Other_File(fType))
		return false;

	switch (fType)
	{
	case UC:
		if (Psettings && Psettings->mUnrealScriptSupport)
			return true;
		else
			return false;
		break;
	case PERL:
	case Idl:
	case PHP:
		return false;
	}

	return true;
}

bool VaService::ShouldColorTooltip()
{
	if (!Psettings || !Psettings->m_enableVA || !Psettings->m_ActiveSyntaxColoring)
		return false;

	if (!Psettings->m_bEnhColorTooltips && !Psettings->mFormatDoxygenTags)
	{
		// VaQuickInfoClassifier needs to run for mFormatDoxygenTags even if !m_bEnhColorTooltips.
		// it checks the two states.
		return false;
	}

	if (mInitAborted)
		return false;

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		int ftype = ed->m_ftype;
		if (XAML == ftype || XML == ftype)
			return false; // [case: 85012]
		return true;
	}

	return false;
}

bool VaService::ShouldAttemptQuickInfoAugmentation()
{
	if (!Psettings || !Psettings->m_enableVA)
		return false;

	if (mInitAborted)
		return false;

	if (g_currentEdCnt)
		return true;

	return false;
}

bool VaService::ShouldFileBeAttachedTo(const WCHAR* filePath)
{
	if (mInitAborted)
		return false;

	const CStringW file(filePath);
	return ::ShouldFileBeAttachedTo(file);
}

// for Find result highlights and current line highlights
bool VaService::ShouldFileBeAttachedToForBasicServices(const WCHAR* filePath)
{
	if (mInitAborted)
		return false;
	if (!Psettings || !Psettings->m_validLicense)
		return false;

	const CStringW file(filePath);
	if (::HasIgnoredFileExtension(file))
		return false;

	// all files since VS Highlight Current Line is Tools | Options | Text Editor | General
	// and find highlight also is not editor specific.
	return true;
}

// this is the opposite of ShouldColor except for the handling of restricted file types
bool VaService::ShouldFileBeAttachedToForSimpleHighlights(const WCHAR* filePath)
{
	if (mInitAborted)
		return false;
	if (!Psettings || !Psettings->m_validLicense)
		return false;

	if (ShouldColor(filePath))
		return false;

	// just because ShouldColor returned false doesn't mean simple highlights are ok
	const CStringW file(filePath);
	if (::HasIgnoredFileExtension(file))
		return false;

#ifdef AVR_STUDIO
	return false;
#endif // AVR_STUDIO

	if (Psettings && Psettings->mRestrictVaToPrimaryFileTypes)
	{
		const int ftype = ::GetFileType(file, true, true);
		switch (ftype)
		{
		case Plain:
		case Tmp:
		case XML:
			// allow simple highlights in non-language file types
			break;

		default:
			// prevent simple highlights in language file types since
			// VS or another extension might support syntactical highlights
			return false;
		}
	}

	// user has disabled file type restriction, so given va carte blanche (?)
	return true;
}

WCHAR* VaService::GetCustomContentTypes()
{
	static CStringW cachedStr = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, "CustomContentTypes", L"");
	return (WCHAR*)(LPCWSTR)cachedStr;
}

int VaService::GetSymbolColor(void* textBuffer, LPCWSTR lineText, int linePos, int bufPos, int context)
{
#if defined(RAD_STUDIO)
	return 0;
#else
	CComPtr<IUnknown> pUnk = (IUnknown*)textBuffer;
	CComQIPtr<IVsTextBuffer> vsTextBuffer(pUnk);

	return WPFGetSymbolColor(vsTextBuffer, lineText, linePos, bufPos, context);
#endif
}

bool VaService::ShowVACompletionSet(intptr_t pIvsCompletionSet)
{
	return ::ShowVACompletionSet(pIvsCompletionSet);
}

extern CStringW GetCommentFromPos(int pos, int alreadyHasComment, int* commentFlags, EdCntPtr ed = {});

WCHAR* VaService::GetCommentFromPos(int pos, int alreadyHasComment, int* commentFlags)
{
	static CStringW cachedStr;

	if (Psettings && Psettings->m_AutoComments)
	{
		CStringW newStr(::GetCommentFromPos(pos, alreadyHasComment, commentFlags));

		if (gTestLogger && gTestLogger->IsMefTooltipLoggingEnabled() && !newStr.IsEmpty() && cachedStr != newStr)
		{
			// only log changes in tooltip text due inconsistent repeated calls in differing versions of vs
			gTestLogger->LogStr(WTString("MefTooltipRequest[CommentFromPos]: ") + WTString(newStr));
		}

		cachedStr = newStr;
	}
	else
		cachedStr.Empty();

	return (WCHAR*)(LPCWSTR)cachedStr;
}

WCHAR* VaService::GetDefFromPos(int pos)
{
	extern CStringW GetDefFromPos(int pos, EdCntPtr ed = {});
	static CStringW cachedStr;
	CStringW newStr(GetDefFromPos(pos));

	if (gTestLogger && gTestLogger->IsMefTooltipLoggingEnabled() && !newStr.IsEmpty() && cachedStr != newStr)
	{
		// only log changes in tooltip text due inconsistent repeated calls in differing versions of vs
		gTestLogger->LogStr(WTString("MefTooltipRequest[DefFromPos]: ") + WTString(newStr));
	}

	cachedStr = newStr;
	return (WCHAR*)(LPCWSTR)cachedStr;
}

WCHAR* VaService::GetExtraDefInfoFromPos(int pos)
{
	extern CStringW GetExtraDefInfoFromPos(int pos, EdCntPtr ed = {});
	static CStringW cachedStr;
	CStringW newStr(GetExtraDefInfoFromPos(pos));

	if (gTestLogger && gTestLogger->IsMefTooltipLoggingEnabled() && !newStr.IsEmpty() && cachedStr != newStr)
	{
		// only log changes in tooltip text due inconsistent repeated calls in differing versions of vs
		gTestLogger->LogStr(WTString("MefTooltipRequest[ExtraDefInfoFromPos]: ") + WTString(newStr));
	}

	cachedStr = newStr;
	return (WCHAR*)(LPCWSTR)cachedStr;
}

bool VaService::OnChar(int ch)
{
	return false;
}

void VaService::AfterOnChar(int ch)
{
}

void VaService::OnSetAggregateFocus(int pIWpfTextView_id)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		auto mgr = WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher());

		if (!mgr && gShellAttr->IsDevenv14OrHigher())
		{
			// [case: 139997]
			// force creation of view manager so that aggregate focus
			// state can be saved and used once va init completes
			WPF_ViewManager::Create();
			mgr = WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher());
		}

		if (mgr)
			mgr->OnSetAggregateFocus(pIWpfTextView_id);
	}
}

void VaService::OnKillAggregateFocus(int pIWpfTextView_id)
{
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get(!gShellAttr->IsDevenv14OrHigher()))
		WPF_ViewManager::Get()->OnKillAggregateFocus(pIWpfTextView_id);
}

void VaService::OnCaretVisible(int /*pIWpfTextView_id*/, bool visible)
{
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());

	// [case: 56633] [case: 58034]
	if (Psettings && Psettings->mTrackCaretVisibility)
		gTypingAllowed = visible;
}

void VaService::OnShellModal(bool isModal)
{
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
	// [case: 41611] [case: 55278] [case: 43724] [case: 44539]
	mShellIsModal = isModal;
}

void VaService::HasVSNetQuickInfo(BOOL hasPopups)
{
	gHasMefQuickInfo = hasPopups;

	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->UpdateQuickInfoState(hasPopups);
}

void VaService::HasVSNetParamInfo(BOOL hasPopups)
{
	gHasMefParamInfo = hasPopups;

	if (hasPopups)
	{
		// [case: 75087]
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
			ed->DisplayToolTipArgs(false);
	}

	// [case: 49022]
	if (hasPopups && g_CompletionSet && g_CompletionSet->IsExpUp(NULL))
		g_CompletionSet->Reposition();

	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->UpdateQuickInfoState(hasPopups);
}

void VaService::ShowSmartTagMenu(int textPos)
{
	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->DisplayTipContextMenu(false);
}

void CALLBACK CheckIncrediBuildCompatibility(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	::KillTimer(NULL, idEvent);

	if (g_managedInterop)
	{
		int major = 0;
		int majorRevision = 0;
		int minor = 0;
		int minorRevision = 0;
		int revision = 0;
		g_managedInterop->GetIncrediBuildVersion(&major, &majorRevision, &minor, &minorRevision, &revision);

		if (major == 1 && majorRevision == 0 && minor == 5 && minorRevision == 10 && revision == 10)
		{
			// [case: 141861] [compatibility] a warning should be displayed when using specific versions of incredibuild
			OneTimeMessageBox(
			    "CheckIncrediBuild",
			    "Visual Assist is incompatible with version 1.5.0.10 of the IncrediBuild Build Acceleration extension "
			    "in Visual Studio.\r\n\r\n"
			    "Version 1.5.0.13 is known to work properly with Visual Assist.\r\n\r\n"
			    "For more information see:\r\nhttp://www.wholetomato.com/support/tooltip.asp?option=IncrediBuild");
		}
	}
}

void VaService::SetVAManagedComInterop(
#ifdef _WIN64
    intptr_t pIVAManagedComInterop
#else
    int pIVAManagedComInterop
#endif
)
{
	if (g_managedInterop)
		g_managedInterop.Release();

	g_managedInterop = ((IUnknown*)pIVAManagedComInterop);

	// [case: 141861] [compatibility] a warning should be displayed when using specific versions of incredibuild
	::SetTimer(NULL, IDT_CHECK_INCREDIBUILD, 10u * 1000u, CheckIncrediBuildCompatibility);
}

// called from MEF for context menu on underlined typo
WCHAR* VaService::GetSpellingSuggestions(char* stringPointer)
{
	CStringList lst;
	::FPSSpell(stringPointer, &lst);
	WTString resultStr = "";
	int itemPos = 0;

	while (itemPos < lst.GetCount())
	{
		CString wd = lst.GetAt(lst.FindIndex(itemPos));
		++itemPos;
		resultStr += wd;
		resultStr += ";";
	}

	static CStringW cachedStr;
	cachedStr.Empty();
	cachedStr = resultStr.Wide();
	return (WCHAR*)(LPCWSTR)cachedStr;
}

void VaService::AddWordToDictionary(char* word)
{
	if (word != NULL)
		::FPSAddWord(word, false);
}

void VaService::IgnoreMisspelledWord(char* word)
{
	if (word != NULL)
		::FPSAddWord(word, true);
}

void VaService::DgmlDoubleClick(char* nodeId)
{
	// TODO: Pull from VAX and interop code...
	// CodeGraphNS::OnDoubleClick(nodeId);
}

void VaService::WriteToLog(const WCHAR* txt)
{
	if (!g_loggingEnabled)
		return;

	WTString ascTxt(txt);
	Log(ascTxt.c_str());
}

void VaService::WriteToTestLog(const WCHAR* txt, bool menuItem)
{
	if (!gTestLogger)
		return;

	if (menuItem && !gTestLogger->IsMenuLoggingEnabled())
		return;

	WTString ascTxt(txt);
	gTestLogger->LogStr(ascTxt);
}

template <typename CHAR> static inline std::basic_string<CHAR> to_backslash(std::basic_string<CHAR> str)
{
	std::replace(str.begin(), str.end(), CHAR('/'), CHAR('\\'));
	return str;
}

static CCriticalSection sColorOutput_file_cs;
static std::hash_set<std::wstring> sColorOutput_files;
static LONG sColorOutput_generation = 0;

bool VaService::ColourBuildOutputLine(const wchar_t* line, int& start_colour_index, int& end_colour_index,
                                      bool& matched)
{
	if (mInitAborted)
		return false;

	if (!Psettings || !Psettings->mColorBuildOutput || !Psettings->m_enableVA)
		return false;

	{
		LONG generation_copy = sColorOutput_generation; // postpone files_cs lock
		std::list<std::wstring> files2;
		// gmit: EnumerateProjectFiles is called instead of GetFilesSortedByName since we need them to be hashed + we
		// don't want to call SortListIfReady() which is potentially slow
		if (EnumerateProjectFiles(
		        [&](const FileInfo& fi, const CStringW&) {
			        files2.push_back(to_backslash((std::wstring)fi.mFilenameLower));
		        },
		        &generation_copy))
		{
			__lock(sColorOutput_file_cs);
			sColorOutput_files.clear();
			sColorOutput_files.insert(files2.begin(), files2.end()); // sort outside the mMapLock lock
			sColorOutput_generation = generation_copy;
		}
	}

	// typical examples
	// 1>          c:\code\devstudio10\vc\include\utility(174) : see reference to class template instantiation
	// 'std::_Pair_base<_Ty1,_Ty2>' being compiled c:\Users\Goran\Documents\Visual Studio
	// 2010\Projects\WindowsFormsApplication1\WindowsFormsApplication1\Program.cs(9,29): error CS0115:
	// 'WindowsFormsApplication1.abc.BinarySearch(object, int)': no suitable method found to override
	// c:\users\goran\documents\visual studio 2010\Projects\WindowsApplication1\WindowsApplication1\Form1.vb(2) : error
	// BC30188: Declaration expected.

	static const std::wstring prefix(L"(?:[[:digit:]]+>)?[[:space:]]*"); // 1>
	static const std::wstring disk_path_prefix(L"[[:alpha:]]:[\\\\]");   // C:\ 
	static const std::wstring network_path_prefix(L"[\\\\]{2}");							// \\ 
	static const std::wstring path_prefix(L"(?:" + disk_path_prefix + L"|" + network_path_prefix + L")");
	static const std::wstring path(path_prefix + L".+?");
	static const std::wstring suffix_start(L"[[:space:]]*[(][[:space:]]*[[:digit:]]+");
	static const std::wstring suffix_end(L"[[:space:]]*[)][[:space:]]*:");
//	static const std::wstring suffix(suffix_start + suffix_end);							// ( 123 ) :
	static const std::wstring suffix2(suffix_start + L"(?:[[:space:]]*,[[:space:]]*[[:digit:]]+)*" + suffix_end); // ( 123 , 456 ) :
	static const std::wstring ending(L".*[\r\n]*");
	static const std::wstring full_path(prefix + L"(" + path + L"(?=" + suffix2 + L"))" + suffix2 + ending);
	static const std::wstring full_path_net(L"[[:space:]]*(" + path + L"(?=" + suffix2 + L"))" + suffix2 + ending);

	try
	{
		static const std::wregex r(full_path, std::regex_constants::ECMAScript | std::regex_constants::optimize);
		std::wcmatch m;

		if (std::regex_match(line, m, r))
		{
			if (m.size() == 2)
			{
			matched:
				const int matched_group = 1;
				std::wstring file = m[matched_group].str();
				file = to_backslash(file);
				std::use_facet<std::ctype<wchar_t>>(std::locale()).tolower(&file.front(), &file.back() + 1);

				start_colour_index = (int)m.position(matched_group);
				end_colour_index = start_colour_index + (int)m.length(matched_group);
				__lock(sColorOutput_file_cs);
				matched = contains(sColorOutput_files, file);
				return true;
			}
		}

#ifdef AVR_STUDIO
		// C:\Users\sean.e\Documents\Atmel Studio\7.0\GccApplication1\GccApplication1\GccApplication1.cpp(16,2): error:
		// #error fooo message C:\Users\sean.e\Documents\Atmel
		// Studio\7.0\GccApplication1\GccApplication1\GccApplication1.cpp(17,1): error: 'a' was not declared in this
		// scope C:\Users\sean.e\Documents\Atmel Studio\7.0\GccApplication1\GccApplication1\GccApplication1.cpp(22,1):
		// warning: unused variable 'x'
		static const std::wregex quick_gcc_test(L": (error|warning): ", std::regex_constants::ECMAScript | std::regex_constants::optimize);

		if (std::regex_search(line, quick_gcc_test))
		{
			static const std::wregex r_net(full_path_net, std::regex_constants::ECMAScript | std::regex_constants::optimize);
			m = std::wcmatch();

			if (std::regex_match(line, m, r_net))
			{
				if (m.size() == 2)
					goto matched;
			}
		}
#else
		const bool enable_net = false;

		if (enable_net)
		{
			static const std::wregex quick_net_test(L"(error|warning) (CS|BC)", std::regex_constants::ECMAScript | std::regex_constants::optimize);

			if (std::regex_search(line, quick_net_test))
			{
				static const std::wregex r_net(full_path_net, std::regex_constants::ECMAScript | std::regex_constants::optimize);
				m = std::wcmatch();

				if (std::regex_match(line, m, r_net))
				{
					if (m.size() == 2)
						goto matched;
				}
			}
		}
#endif
	}
	catch (std::regex_error e)
	{
		_ASSERTE(!"Build output regex exception!");
	}

	return false;
}

void VaService::ThemeUpdated()
{
	// clear the cache so that new queries get the new values
	if (g_IdeSettings)
		g_IdeSettings->ResetCache();
	// let syncMgr handle change of theme first
	if (gColorSyncMgr)
		gColorSyncMgr->PotentialThemeChange();
	// then update the theming of icons before updating windows
	if (gImgListMgr)
		gImgListMgr->ThemeUpdated();
	if (g_IdeSettings)
		g_IdeSettings->ResetCache();
	if (g_FontSettings)
		g_FontSettings->Update();
	if (g_pMiniHelpFrm)
		g_pMiniHelpFrm->SettingsChanged();
	if (g_CompletionSet)
		g_CompletionSet->RebuildExpansionBox();
	if (g_pMiniHelpFrm && g_pMiniHelpFrm->GetSafeHwnd())
		g_pMiniHelpFrm->RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	VAWorkspaceViews::ThemeChanged();
	if (GetOutlineFrame() && GetOutlineFrame()->GetSafeHwnd())
		GetOutlineFrame()->ThemeUpdated();
	if (gPrimaryFindRefsResultsFrame && gPrimaryFindRefsResultsFrame->GetSafeHwnd())
		gPrimaryFindRefsResultsFrame->ThemeUpdated();
	if (GetHashtagsFrame() && GetHashtagsFrame()->GetSafeHwnd())
		GetHashtagsFrame()->ThemeUpdated();
}

void VaService::SubclassStatic(HWND hwnd)
{
	DoStaticSubclass(hwnd);
}

void VaService::VsServicesReady()
{
#ifndef VAX_CODEGRAPH
	HRESULT hRes = S_OK;

	if (!gVsRunningDocumentTable)
	{
		_ASSERTE(gPkgServiceProvider);
		hRes = gPkgServiceProvider->QueryService(SID_SVsRunningDocumentTable, IID_IVsRunningDocumentTable, (void**)&gVsRunningDocumentTable);
	}

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 50915] force update of va view, outline, refs - they can use shell colors now
		ThemeUpdated();

		if (gShellAttr->IsDevenv11OrHigher())
		{
			if (!gColorSyncMgr)
			{
				new ColorSyncManager();

				if (gVaInteropService && gColorSyncMgr)
				{
					// [case: 66898] init colors from VS
					gColorSyncMgr->CompleteInit();
				}
			}

#ifdef AVR_STUDIO
			// [case: 87602] AtmelStudio 7 gcc snippet support is incomplete (without va enabled),
			// so don't suggest them (by not creating VsSnippetManager)
#else

			if (!gVsSnippetMgr)
				gVsSnippetMgr = new VsSnippetManager;
#endif
		}
	}

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		new ShellListener(this);
		new SolutionListener(this);
	}
#endif // VAX_CODEGRAPH
}

void VaService::ActiveProjectItemChanged(
	IVsHierarchy* pHierOld, VSITEMID itemidOld, IVsMultiItemSelect* pMISOld, ISelectionContainer* pSCOld, 
	IVsHierarchy* pHierNew, VSITEMID itemidNew, IVsMultiItemSelect* pMISNew, ISelectionContainer* pSCNew)
{
#if !defined(RAD_STUDIO) && !defined(VA_CPPUNIT)
	if (!Psettings || !Psettings->m_enableVA)
		return;
	
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		// NOTE: itemidNew is an id of open file, not necessarily project, 
		// but can be CPP file or CS file, whatever in the project

		ed->UpdateProjectInfo(pHierNew);

		if (g_pMiniHelpFrm)
		{
			CStringW prjText = ed->ProjectName();
			g_pMiniHelpFrm->SetProjectText(prjText);
		}
	}
#endif
}

DWORD VaService::GetIdealMinihelpHeight(HWND hWnd)
{
	return ::GetIdealMinihelpHeight(hWnd);
}

void VaService::UpdateInspectionEnabledSetting(bool enabled)
{
	// #codeInspectionSetting va_x notified of state change
	if (Psettings)
		Psettings->mCodeInspection = enabled;
}

bool VaService::IsFindReferencesRunning()
{
	if (gPrimaryFindRefsResultsFrame)
		return gPrimaryFindRefsResultsFrame->IsThreadRunning();

	return false;
}

void VaService::DbgBrk_2016_05()
{
	if (gVaMainWnd->GetSafeHwnd())
	{
		static const char* sCmd = "File.Exit";
		SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)sCmd, 0);
	}

	static bool sExiting = false;

	if (sExiting)
		return;

	sExiting = true;
	LOG("Dude!! don't mix dlls from different builds of VA!!");

	if (!gVaMainWnd->GetSafeHwnd())
		__debugbreak();

	gVaAddinClient.Shutdown();
	sExiting = false;
}

bool VaService::IsProcessPerMonitorDPIAware()
{
	return VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled();
}

void VaService::MoveFocusInVaView(bool goReverse)
{
	VAWorkspaceViews::MoveFocus(goReverse);
}

BOOL VaService::UpdateCompletionStatus(intptr_t pCompSet, DWORD dwFlags)
{
	EdCntPtr ed(g_currentEdCnt);

	if (ed)
	{
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			// watch out for intermediate window
			if (!ed->HasFocus())
				return FALSE;
		}
		else
		{
			HWND h = ::VAGetFocus();

			if (h != ed->GetSafeHwnd())
				return FALSE;
		}

		CComQIPtr<IVsCompletionSet> pCS((IUnknown*)pCompSet);

		if (pCS)
			return ed->UpdateCompletionStatus(pCS, dwFlags);
	}

	return FALSE;
}

IVaDebuggerToolService* VaService::GetVaDebuggerService(int managed)
{
	return ::VaDebuggerToolService(managed);
}

int gExternalMenuCount = 0;

class VaMenuItem : public IVaMenuItem
{
  public:
	VaMenuItem(MenuItemLmb* item) : _menuItemLmb(item)
	{
	}
	VaMenuItem(const VaMenuItem &) = default;
	VaMenuItem& operator=(const VaMenuItem&) = default;
	virtual ~VaMenuItem() override
	{
		if (_menuItemIcon)
			::DestroyIcon(_menuItemIcon);
	}

	virtual const wchar_t* Text() const override
	{
		return (LPCWSTR)_menuItemLmb->Text;
	}

	virtual void* Icon(bool moniker) override
	{
		if (moniker)
		{
#ifdef _WIN64
			return (void*)ImageListManager::GetMoniker((int)_menuItemLmb->Icon, true, true);
#else
			return nullptr;
#endif
		}

		if (!_menuItemIcon)
		{
			auto imgList = gImgListMgr->GetImgList(ImageListManager::bgMenu);

			if (imgList)
				_menuItemIcon = imgList->ExtractIcon((int)_menuItemLmb->Icon);
		}

		return (void*)_menuItemIcon;
	}

	virtual bool IsSeparator() const override
	{
		return _menuItemLmb->IsSeparator();
	}

	virtual bool IsEnabled() const override
	{
		return _menuItemLmb->IsEnabled();
	}

	virtual void Invoke() override
	{
		_ASSERTE(_menuItemLmb->Command);
		if (_menuItemLmb->Command) // [case: 112180]
			_menuItemLmb->Command();
	}

  private:
	MenuItemLmb* _menuItemLmb;
	HICON _menuItemIcon = nullptr;
};

class VaMenuItemCollection : public IVaMenuItemCollection
{
  public:
	VaMenuItemCollection(const PopupMenuLmb& mnu) : _mnu(mnu)
	{
		++gExternalMenuCount;
		for (auto& item : _mnu.Items)
		{
			Items.push_back(&item);
		}
	}

	virtual ~VaMenuItemCollection() = default;

	virtual int Count() const override
	{
		return (int)Items.size();
	}

	virtual IVaMenuItem* Get(int n) override
	{
		if (n >= 0 && n < (int)Items.size())
			return &Items[(uint)n];

		return nullptr;
	}

	virtual void Release() override
	{
		--gExternalMenuCount;
		delete this;
	}

  private:
	PopupMenuLmb _mnu;
	std::vector<VaMenuItem> Items;
};

bool VaService::CanDisplayRefactorMenu()
{
	if (!VATomatoTip::CanDisplay())
		return false;

	auto ed = g_currentEdCnt;

	if (!ed)
		return false;

	if (Psettings && Psettings->mCodeInspection)
		return true;

	return false;
}

bool VaService::ShouldDisplayRefactorMenuAtCursor()
{
	auto ed = g_currentEdCnt;

	if (!ed)
		return false;

	if (!g_ScreenAttrs.m_VATomatoTip)
		return false;

	// In comment or string?
	if (!g_ScreenAttrs.m_VATomatoTip->ShouldDisplayAtCursor(ed))
		return false;

	if (Psettings && Psettings->mCodeInspection)
		return true;

	return false;
}

IVaMenuItemCollection* VaService::BuildRefactorMenu()
{
	_ASSERTE(Psettings && Psettings->mCodeInspection);
	try
	{
		PopupMenuLmb mnu;
		size_t find_refs_index = 0;
		DTypePtr mSym;
		FillVAContextMenuWithRefactoring(mnu, find_refs_index, mSym, false);
		auto* c = new VaMenuItemCollection(mnu);
		return c;
	}
	catch (...)
	{
		VALOGEXCEPTION("VAS:BRM:");
	}
	return nullptr;
}

WCHAR* VaService::GetDbDir()
{
	return (WCHAR*)(LPCWSTR)VaDirs::GetDbDir();
}

void VaService::AsyncWorkspaceCollectionComplete(int reqId, const WCHAR* solutionFilepath, const WCHAR* workspaceFiles)
{
	// AsyncWorkspaceCollectionComplete should only be called if the g_vaManagedPackageSvc has already been set.
	// assert added to confirm that assumption.
	_ASSERTE(g_vaManagedPackageSvc);
	gSqi.AsyncWorkspaceCollectionComplete(reqId, solutionFilepath, workspaceFiles);
}

void VaService::OnWorkspacePropertyChanged(int newState)
{
	RunFromMainThread(
	    [newState]() {
		    if (newState == -2)
		    {
			    gSqi.VsWorkspaceIndexerFinishedBeforeLoad();
		    }
		    else
		    {
			    gSqi.RequestAsyncWorkspaceCollection(false);
			    if (-1 != newState)
				    gSqi.VsWorkspaceIndexerFinished();
		    }
	    },
	    false);
}

bool VaService::AddInclude(const WCHAR* include)
{
	VARefactorCls varef;
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
		return false;

	const uint curFileId = gFileIdManager->GetFileId(ed->FileName());
	CStringW includeFileName = gFileFinder->ResolveInclude(include, ::Path(ed->FileName()), false);

	if (includeFileName == L"")
		return false;

	const uint incFileId = gFileIdManager->GetFileId(includeFileName);
	const uint kCurPos = ed->CurPos();
	const int kUserAtLine = (int)TERROW(kCurPos);
	int line = 0;

	if (GetAddIncludeLineNumber(curFileId, incFileId, kUserAtLine, line, std::numeric_limits<int>::max()))
	{
		// 		RefineAddIncludeLineNumber(line);
		return !!DoAddInclude(line, include, true);
	}
	else
		return true;
}

static ThreadStatic<CStringW> sFileMatchRet;

const WCHAR* VaService::GetBestFileMatch(const WCHAR* file)
{
	assert(file);
	FileList matches;
	::GetBestFileMatch(file, matches);
	sFileMatchRet().Empty();

	for (const auto& fileinfo : matches)
	{
		if (fileinfo.mFilename.IsEmpty())
			continue;

		if (!sFileMatchRet().IsEmpty())
			sFileMatchRet() += ";";

		sFileMatchRet() += fileinfo.mFilename;
	}

	return sFileMatchRet();
}

bool OverrideNextSaveFileNameDialog(const wchar_t* filename);

bool VaService::OverrideNextSaveFileNameDialog(const WCHAR* filename)
{
	return ::OverrideNextSaveFileNameDialog(filename);
}

void VaService::ExecMouseCommand(int cmd, int wheel, int x, int y)
{
	EdCntPtr ed(g_currentEdCnt);

	if (ed)
	{
		CPoint pt(x, y);
		ed->vScreenToClient(&pt);

		if (wheel == 0)
			ed->OnMouseCmd((VaMouseCmdBinding)cmd, pt);
		else
			ed->OnMouseWheelCmd((VaMouseWheelCmdBinding)cmd, wheel, pt);
	}
}

WCHAR* VaService::GetCurrentEdFilename() const
{
	EdCntPtr ed(g_currentEdCnt);

	if (ed)
	{
		static CStringW ret;
		ret = ed->FileName();
		return (WCHAR*)(const WCHAR*)ret;
	}
	else
		return nullptr;
}

bool VaService::DelayFileOpenLineAndChar(const WCHAR* file, int ln, int cl, LPCSTR sym, BOOL preview)
{
	if (!file)
		return false;

	return !!::DelayFileOpenLineAndChar(file, ln, cl, sym, preview);
}

bool VaService::IsFolderBasedSolution() const
{
	if (!GlobalProject)
		return false;

	return GlobalProject->IsFolderBasedSolution();
}

int VaService::IsStepIntoSkipped(wchar_t* functionName)
{
	return (int)::IsStepIntoSkipped(functionName);
}

static void Vc6RefResultsFocus()
{
	if (gPrimaryFindRefsResultsFrame)
	{
		if (gPrimaryFindRefsResultsFrame->m_hWnd && IsWindow(gPrimaryFindRefsResultsFrame->m_hWnd))
			gShellSvc->GetFindReferencesWindow(); // ensure parent wnd is visible
	}
	else
	{
		gVaService->FindReferences(0, RESWORD, NULLSTR);
		return;
	}

	if (gPrimaryFindRefsResultsFrame)
		gPrimaryFindRefsResultsFrame->FocusFoundUsage();
}

static bool IsValidUnfocusedEditorCommand(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_ListMethods:
	case icmdVaCmd_OpenCorrespondingFile:
	case icmdVaCmd_SpellCheck:
	case icmdVaCmd_Reparse:
	case icmdVaCmd_DisplayIncludes:
		return true;
	default:
		return false;
	}
}

static bool IsEditorSpecificCommand(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_ListMethods:
	case icmdVaCmd_OpenCorrespondingFile:
	case icmdVaCmd_SpellCheckWord:
	case icmdVaCmd_FindPreviousByContext:
	case icmdVaCmd_FindNextByContext:
	case icmdVaCmd_GotoImplementation:
	case icmdVaCmd_SuperGoto:
	case icmdVaCmd_GotoMember:
	case icmdVaCmd_CommentOrUncomment:
	case icmdVaCmd_SurroundWithBraces:
	case icmdVaCmd_SurroundWithParens:
	case icmdVaCmd_SurroundWithPreprocDirective:
	case icmdVaCmd_SortLines:
	case icmdVaCmd_SpellCheck:
	case icmdVaCmd_Reparse:
	case icmdVaCmd_InsertCodeTemplate:
	case icmdVaCmd_ScopePrevious:
	case icmdVaCmd_ScopeNext:
	case icmdVaCmd_Paste:
	case icmdVaCmd_ShareWith:
	case icmdVaCmd_ShareWith2:
	case icmdVaCmd_ContextMenuOld:
	case icmdVaCmd_RefactorPopupMenu:
	case icmdVaCmd_FindReferencesInFile:
	case icmdVaCmd_ToggleLineComment:
	case icmdVaCmd_ToggleBlockComment:
	case icmdVaCmd_LineComment:
	case icmdVaCmd_LineUncomment:
	case icmdVaCmd_BlockComment:
	case icmdVaCmd_BlockUncomment:
	case icmdVaCmd_ResetEditorZoom:
	case icmdVaCmd_SmartSelectExtend:
	case icmdVaCmd_SmartSelectShrink:
	case icmdVaCmd_SmartSelectExtendBlock:
	case icmdVaCmd_SmartSelectShrinkBlock:
	case icmdVaCmd_OpenContextMenu:
		return true;
	default:
		return false;
	}
}

bool IsRefactorCommand(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_RefactorPopupMenu:
	case icmdVaCmd_RefactorRename:
	case icmdVaCmd_RefactorExtractMethod:
	case icmdVaCmd_RefactorEncapsulateField:
	case icmdVaCmd_RefactorMoveImplementation:
	case icmdVaCmd_RefactorDocumentMethod:
	case icmdVaCmd_RefactorCreateImplementation:
	case icmdVaCmd_RefactorCreateDeclaration:
	case icmdVaCmd_FindReferences:
	case icmdVaCmd_FindReferencesInFile:
	case icmdVaCmd_RefactorAddMember:
	case icmdVaCmd_RefactorAddSimilarMember:
	case icmdVaCmd_RefactorChangeSignature:
	case icmdVaCmd_RefactorChangeVisibility:
	case icmdVaCmd_RefactorAddInclude:
	case icmdVaCmd_RefactorCreateFromUsage:
	case icmdVaCmd_RefactorImplementInterface:
	case icmdVaCmd_RefactorRenameFiles:
	case icmdVaCmd_RefactorCreateFile:
	case icmdVaCmd_RefactorMoveSelToNewFile:
	case icmdVaCmd_RefactorIntroduceVariable:
	case icmdVaCmd_RefactorAddRemoveBraces:
	case icmdVaCmd_RefactorCreateMissingCases:
	case icmdVaCmd_RefactorMoveImplementationToHdr:
	case icmdVaCmd_RefactorConvertBetweenPointerAndInstance:
	case icmdVaCmd_RefactorSimplifyInstanceDeclaration:
	case icmdVaCmd_RefactorAddForwardDeclaration:
	case icmdVaCmd_RefactorConvertEnum:
	case icmdVaCmd_RefactorMoveClassToNewFile:
	case icmdVaCmd_RefactorSortClassMethods:
		return true;
	default:
		return false;
	}
}

bool IsDynamicCommand(DWORD cmdId)
{
	if (cmdId >= icmdVaCmd_DynamicSelectionFirst && cmdId <= icmdVaCmd_DynamicSelectionLast)
		return true;

	if (cmdId >= icmdVaCmd_PasteItemFirst && cmdId <= icmdVaCmd_PasteItemLast)
		return true;

	return false;
}

#if !defined(RAD_STUDIO)
_declspec(dllexport)
#endif
IVaService* GetVaService()
{
	if (!gVaService)
	{
		static bool once = true;

		if (once)
		{
			if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			{
				once = false;

				// [case: 64588]
				// [case: 62809] mef component loads before va connection.
				// create enough state for mef to make decisions about file
				// extensions and features that may be enabled after va comes up
				if (!g_IdeSettings)
					g_IdeSettings = new IdeSettings;

				if (!Psettings)
					Psettings = new CSettings;

				if (!GlobalProject)
					GlobalProject = new Project;

				gVaService = new VaService;
			}
		}
	}

	return gVaService;
}

CString GetEmailFromLicense()
{
	if (!gVaLicensingHost)
	{
		_ASSERTE(gVaLicensingHost);
		return "";
	}

	const CString user(gVaLicensingHost->GetLicenseInfoUser());

	if (user.IsEmpty())
		return "";

	if (gVaLicensingHost->IsSanctuaryLicenseInstalled(false)) // sanctuary license
	{
		return user; // this is the email
	}
	else // armadillo license
	{
		int atLoc = user.Find("@");
		int spaceLoc = user.Find(" ");
		CString email;

		if (spaceLoc != -1 && atLoc != -1)
			email = user.Left(spaceLoc);
		else
			email = "user@company.com";

		return email;
	}
}

void LaunchEmailToBuyer()
{
#if !defined(VA_CPPUNIT)
	CString email = GetEmailFromLicense();

	if (email.IsEmpty())
		return;

	CString users;
	CString__FormatA(users, "%d", gVaLicensingHost->GetLicenseUserCount());
	CString URL = "mailto:";
	URL += email;
	URL += _T("?subject=Maintenance renewal for Visual Assist");
	URL += _T("&body=I use Visual Assist to improve my development productivity, and I'd like to receive another year ")
	       _T("of software updates and technical support for the product.%0A");
	URL += _T("%0A");
	URL += _T("Can you renew maintenance for all developers of the following license?%0A");
	URL += email;
	URL += _T("%0A");
	URL += _T("%0A");
	URL += _T("You can find out what's included in software maintenance and get a quote to renew maintenance at:%0A");
	URL += _T("http://www.wholetomato.com/purchase/maintenance.asp%0A");
	URL += _T("%0A");
	URL += _T("If our company owns multiple licenses of Visual Assist, please consider using this opportunity to ")
	       _T("consolidate and co-term our licenses.%0A");
	URL += _T("%0A");
	URL += _T("Thank you.");
	::ShellExecute(NULL, _T("open"), URL, NULL, NULL, SW_SHOW);
#endif
}

void EMailToLicenseSupport()
{
	CString email = "licenses@wholetomato.com";

	CString URL = "mailto:";
	URL += email;
	URL += _T("?subject=License support for Visual Assist");
	URL += _T("&body=I use Visual Assist to improve my development productivity, and I need help with my license.%0A");
	URL += _T("%0A");
	URL += _T("Please provide assistance with the following:%0A");
	URL += _T("%0A");

	::ShellExecute(NULL, _T("open"), URL, NULL, NULL, SW_SHOW);
}


void EmailToTechnicalSupport()
{
	CString email = "support@wholetomato.com";
	CString URL = "mailto:";
	URL += email;
	URL += _T("?subject=Technical support for Visual Assist");
	URL += _T("&body=I use Visual Assist to improve my development productivity, and I need technical support.%0A");
	URL += _T("%0A");

	// Basic info
	CString user, key, sysInfo;

#if !defined(VA_CPPUNIT)
	_ASSERTE(gVaLicensingHost);

	if (gVaLicensingHost && 1 == gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdSubmitHelp))
	{
		URL += _T("Below are my basic info:%0A");

		// User Information
		user = gVaLicensingHost->GetLicenseInfoUser();
		if (!user.IsEmpty())
		{
			URL += _T("%0AUser: ");
			URL += user;
		}

		// License Key
		key = gVaLicensingHost->GetLicenseInfoKey();
		if (!key.IsEmpty())
		{
			URL += _T("%0ALicense Key: ");
			URL += key;
		}

		// System Information
		sysInfo = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, "AboutInfo", L"");
		if (!sysInfo.IsEmpty())
		{
			URL += _T("%0ASystem Info: ");
			URL += sysInfo;
		}
	}
#endif

	// opening notes for user's message
	URL += _T("%0A");
	URL += _T("%0A");
	URL += _T("Please provide assistance with the following:%0A");
	URL += _T("%0A");

	::ShellExecute(NULL, _T("open"), URL, NULL, NULL, SW_SHOW);
}

void EmailToTechnicalSupportFeatureRequest()
{
	CString email = "support@wholetomato.com";
	CString URL = "mailto:";
	URL += email;
	URL += _T("?subject=Feature request for Visual Assist");
	URL += _T("&body=I use Visual Assist to improve my development productivity, and I have a feature request.%0A");
	URL += _T("%0A");

	::ShellExecute(NULL, _T("open"), URL, NULL, NULL, SW_SHOW);
}

void RenewMaintenance()
{
#if !defined(VA_CPPUNIT)
	Log("RenewMaintenance: RenewMaintenance()");
	_ASSERTE(gVaLicensingHost);

	if (!gVaLicensingHost || 1 != gVaLicensingHost->LicenseCommandQueryStatus(IVaLicensing::cmdRenew))
		return;

	_ASSERTE(!gVaLicensingHost->IsSanctuaryLicenseInstalled(false));

	if (gVaLicensingHost->IsNonPerpetualLicenseInstalled())
	{
		_ASSERTE(!"no command should have been enabled to allow launch of purchase page with non-perptual license installed");
		return;
	}

	if (1 == gVaLicensingHost->GetLicenseUserCount())
		LaunchRenewPage();
	else
		LaunchEmailToBuyer();
#endif
}

#if !defined(VA_CPPUNIT)
void LaunchRenewPage()
{
	_ASSERTE(!gVaLicensingHost || !gVaLicensingHost->IsSanctuaryLicenseInstalled(false));
	const CString url(_T("https://www.wholetomato.com/purchase/maintenance.asp"));

	if (gVaLicensingHost && gVaLicensingHost->IsNonPerpetualLicenseInstalled())
	{
		_ASSERTE(!"no command should have been enabled to allow launch of purchase page with non-perptual license installed");
		return;
	}

	int userCount = gVaLicensingHost ? gVaLicensingHost->GetLicenseUserCount() : -1;

	if (userCount == -1)
	{
		Log("User count is -1, returning");
		return;
	}

	ShellExecute(NULL, _T("open"), url, NULL, NULL, SW_SHOW);
}
#endif
