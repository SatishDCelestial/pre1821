#include "stdafxed.h"
#include <mbctype.h>
#include <ppl.h>
#include "SolutionFiles.h"
#include "Project.h"
#include "log.h"
#include "parse.h"
#include "settings.h"
#include "oleobj.h"
#include "expansion.h"
#include "resource.h"
#include "StatusWnd.h"
#include "fontsettings.h"
#include "WorkSpaceTab.h"
#include "VATree.h"
#include "DBLock.h"
#include "timer.h"
#include "HeapLook.h"
#include "SubClassWnd.h"
#include <winbase.h>
#include "VAClassView.h"
#include <vector>
#include "rbuffer.h"
#include "VACompletionSet.h"
#include "VaOptions.h"
#include "EdDll.h"
#include "../Addin/AIC.h"
#include "../Addin/DSCmds.h"
#include "ParseThrd.h"
#include "VAWorkspaceViews.h"
#include "AutotextManager.h"
#include "FindReferencesResultsFrame.h"
#include "..\common\ThreadName.h"
#include "GotoDlg1.h"
#include "DevShellAttributes.h"
#include "Addin\MiniHelpFrm.h"
#include "VaService.h"
#include "ProjectLoader.h"
#include "DBFile/VADBFile.h"
#include "ProjectInfo.h"
#include <algorithm>
#include "Import.h"
#include "Registry.h"
#include "wt_stdlib.h"
#include "mainThread.h"
#include "SymbolRemover.h"
#include "ReferenceImporter.h"
#include "RegKeys.h"
#include "TempSettingOverride.h"
#include "DatabaseDirectoryLock.h"
#include "Lock.h"
#include "FileId.h"
#include "../../../3rdParty/Vc6ObjModel/appauto.h"
#include "WtException.h"
#include "file.h"
#include "Directories.h"
#include "LiveOutlineFrame.h"
#include "AutoUpdate/WTAutoUpdater.h"
#include "TraceWindowFrame.h"
#include "TipOfTheDay.h"
#include "Usage.h"
#include "WrapCheck.h"
#include "addin\BuyTryDlg.h"
#include "FeatureSupport.h"
#include <float.h>
#include "TokenW.h"
#include "StringUtils.h"
#include "VAParse.h"
#include "ScreenAttributes.h"
#include "VASeException/VASeException.h"
#include "FileFinder.h"
#include "WPF_ViewManager.h"
#include "VACompletionSetEx.h"
#include "VaTimers.h"
#include "VA_MRUs.h"
#include "IdeSettings.h"
#include "AutoReferenceHighlighter.h"
#include "VAAutomation.h"
#include "KeyBindings.h"
#include "LogElapsedTime.h"
#include "CodeGraph.h"
#include "vsshell100.h"
#include "utils_goran.h"
#include "ColorSyncManager.h"
#include "VsSnippetManager.h"
#include "ImageListManager.h"
#include "WindowUtils.h"
#include "SyntaxColoring.h"
#include "EdcntWPF.h"
#include "VaHashtagsFrame.h"
#include "DTypeDbScope.h"
#include "includesDb.h"
#include "HashtagsManager.h"
#include "inheritanceDb.h"
#include "BuildInfo.h"
#include "BCMenu.h"
#include <ppltasks.h>
#include "Win32Heap.h"
#include "VASmartSelect.h"
#include "DebuggerTools/DebuggerToolsCpp.h"
#include "IVaVsAddinClient.h"
#include "IVaLicensing.h"
#include "VaAddinClient.h"
#include "Addin/Register.h"
#include "../common/ScopedIncrement.h"
#include "SemiColonDelimitedString.h"
#ifndef NOSMARTFLOW
#include "SmartFlow/phdl.h"
#endif
#include "SolutionListener.h"
#include "../vaIPC/vaIPC/IpcClient.h"
#include "../vaIPC/vaIPC/IPC stats.h"
#pragma warning(push, 3)
#include "miloyip-rapidjson\include\rapidjson\document.h"
#pragma warning(pop)
#include <filesystem>
#include "RadStudioPlugin.h"
#include "FirstRunDlg.h"
#include "../vaIPC/vaIPC/common/string_utils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// global classes
Project* GlobalProject = NULL;
StatusWnd* g_statBar = NULL;
VA_MRUs* g_VA_MRUs = NULL;

// the main IDE window HWND -- for parenting dialogs, message boxes, painting, dpi, fonts
HWND MainWndH = NULL;
// MFC wrapper for MainWndH
CWnd* gMainWnd = NULL;
// backing store for gMainWnd
static CWnd sMainWnd;

// in RadStudio, this window is created solely for VA; in all other IDEs, gVaMainWnd is the same as gMainWnd
// -- for VA threading, messages, and timers (see VaAddinClient::PreDefWindowProc).
// Use as gVaMainWnd->GetSafeHwnd() to maintain compatibility with previous use of MainWndH (which is null in unit
// tests). Continue to use MainWndH and gMainWnd for parenting dialogs, message boxes, dpi, etc.
CWnd* gVaMainWnd = nullptr;

IApplication* g_pApp = NULL;
HANDLE gProcessLock = NULL;
static HANDLE sHistoryDirCountLock = NULL;
volatile BOOL gShellIsUnloading = FALSE;
FileDic* s_pSysDic =
    NULL; // used when the cpp and cs dics haven't been set up yet; shares their hashtable (is the hashtable owner)
volatile int gTypingDevLang = -1; // Do not default to Src for Web Projects. See assert in GetSysDic()
bool WrapperCheck::sWrapperCheckDecoy = false;
DWORD gArmadilloSecurityStringValue = 0;
CComQIPtr<EnvDTE::_DTE> gDte;
CComQIPtr<EnvDTE80::DTE2> gDte2;
bool g_DollarValid = true;
CharsTable chars_table;
extern bool DidHelpFlag;
AutoReferenceHighlighter* gAutoReferenceHighlighter = NULL;
static volatile LONG sMfcLoading = 0;
static volatile bool sOkToLoadProjFiles = true;
#if defined(VA_CPPUNIT)
extern bool gOverrideClr;
extern bool gOverrideWinRT;
#endif // VA_CPPUNIT
CCriticalSection ProjectLoader::sLock;
ProjectLoaderPtr ProjectLoader::g_pl = nullptr;
PooledThreadBase::ThreadState gBackgroundInitThreadState = PooledThreadBase::ThreadState::ts_Finished;
CStringW kWinProgFilesDir;
CStringW kWinProgFilesDirX86;
static CStringW kIdeInstallDir;
static CStringW kMsSdksInstallDir;
static CStringW kMsSdksInstallDirX86;
static CStringW kWindowsKitsInstallDir;
static CStringW kWindowsKitsInstallDirX86;
static CStringW sUE4Project;

#ifdef _DEBUG
// #define DEBUG_MEM_STATS
#endif

#ifdef DEBUG_MEM_STATS
CMemoryState gMemStart;
#endif

void PatchW32Functions(bool patch);
extern void VATreeSetup(bool init);
extern void BCGInit(BOOL init);
void InitSysHeaders(MultiParsePtr mp, CStringW& stdafx);
void RunTriggeredCommand(LPCTSTR valName, const CStringW& arg);

void OutputDebugStringA2(const char *str)
{
//	::OutputDebugStringA(str);
}

bool DirectoryExists(const CStringW& dirName)
{
	DWORD attribs = ::GetFileAttributesW(dirName.GetString());
	if (attribs == INVALID_FILE_ATTRIBUTES)
	{
		return false;
	}
	return (attribs & FILE_ATTRIBUTE_DIRECTORY);
}

class NuGetScan : public ParseWorkItem
{
  public:
	NuGetScan(bool delayStart) : ParseWorkItem("NuGetScan"), mStartTicks(delayStart ? ::GetTickCount() : 0)
	{
		if (GlobalProject)
			mSolutionFileWhenCreated = GlobalProject->SolutionFile();
	}

	virtual void DoParseWork()
	{
		if (!GlobalProject || mSolutionFileWhenCreated.IsEmpty())
			return;

		const CStringW curSlnFile(GlobalProject->SolutionFile());
		if (curSlnFile != mSolutionFileWhenCreated)
			return;

		bool needInvalidate = false;
		if (Psettings->mLookupNuGetRepository)
		{
			FileList incDirs, curDirs;
			::GetRepositoryIncludeDirs(curSlnFile, incDirs);

			{
				RWLockReader l;
				const FileList& dirs = GlobalProject->GetSolutionPrivateSystemIncludeDirs(l);
				curDirs.Add(dirs);
			}

			if (curDirs != incDirs)
			{
				needInvalidate = true;
				GlobalProject->SwapSolutionRepositoryIncludeDirs(incDirs);
				if (!gShellIsUnloading && !StopIt)
				{
					// update sys include dirs
					IncludeDirs::ProjectLoaded();
				}
			}
		}

		const bool parsedAnyFiles = GlobalProject->ParsePrivateSystemHeaders();

		if (gFileFinder && (needInvalidate || parsedAnyFiles))
		{
			// clear file lookup cache since dirs have changed
			gFileFinder->Invalidate();

			// [case: 141911]
			RunFromMainThread([] {
				EdCntPtr ed(g_currentEdCnt);
				if (ed)
					ed->QueForReparse();
			});
		}
	}

	virtual bool CanRunNow() const
	{
		if (!mStartTicks)
			return true;

		const DWORD now = ::GetTickCount();
		return ((now - mStartTicks) > 15000) || (now < mStartTicks);
	}

  private:
	CStringW mSolutionFileWhenCreated;
	const DWORD mStartTicks;
};

int LoadIdxesInDir(const CStringW& dir, int devLang, uint dbFlags)
{
	DEFTIMER(LoadIdxesInDirimer);
	MultiParsePtr mp = MultiParse::Create(devLang);
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
#if !defined(VA_CPPUNIT)
	_ASSERTE(g_mainThread != GetCurrentThreadId());
	_ASSERTE((GlobalProject && GlobalProject->IsBusy()) ||
	         (g_ParserThread && g_ParserThread->GetThreadId() == GetCurrentThreadId()));
#endif // !VA_CPPUNIT
	int count = 0;

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		// Load new files
		FileList files;
		FindFiles(dir, L"*Idx.db", files);
		CreateDir(dir);

		// #seanConsiderConcurrentLoop -- concurrent loop might improve load time
		// of very large solutions, but need to measure that concurrency doesn't
		// cause slowdown due to contention among the threads.
		// also, potential impact on namespace resolution; see:#namespaceResolutionOrderDependency
		for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
		{
			CStringW file = (*it).mFilename;
			mp->ReadDBFile(file);
			count++;
		}

		// [case: 97154]
		// now that the directory load is complete, check for missing includes
		mp->ProcessPendingIncludes(dbFlags);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("LoadIdxesInDir exception");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in LoadIdxesInDir");
		}
	}
#endif // !SEAN

	return count;
}

BOOL ShouldParseGlobals(int ftype)
{
	if (ftype == Idl)
	{
		// [case: 80763] change to TRUE after repercussions are fully understood
		return FALSE;
	}

	// Added parsing of HTML files so:
	// In JS/CS files, goto of Document.MyField goes to html file <tag name=MyField>
	// Warning: Parsing large XML and HTML files may slow down the parsing.
	// Needs more thinking...
	//

	return (ftype == Header || ftype == CS || Is_VB_VBS_File(ftype) || ftype == RC || ftype == UC || ftype == JS ||
	        ftype == PHP || Is_Tag_Based(ftype));
}

CStringW VerifyPathList(const CStringW pathList)
{
	SemiColonDelimitedString t = WTExpandEnvironmentStrings(pathList);
	FileList pathsList;
	WCHAR full[MAX_PATH + 1];
	LPWSTR fname;
	CStringW npth;

	while (t.NextItem(npth))
	{
		if (GetFullPathNameW(npth, MAX_PATH, full, &fname) > 0)
		{
			npth = full;
			if (npth.GetLength() > 1)
			{
				if (!pathsList.ContainsNoCase(npth))
				{
#if defined(VA_CPPUNIT)
					pathsList.Add(npth);
#else
					if (IsDir(npth))
						pathsList.Add(npth);
					else
						vLog("VerifyPathList error FNF: %s\n", (LPCTSTR)CString(npth));
#endif
				}
			}
		}
	}

	CStringW filteredPaths;
	for (FileList::const_iterator it = pathsList.cbegin(); it != pathsList.cend(); ++it)
	{
		const CStringW file((*it).mFilename);
		filteredPaths += file;
		filteredPaths += L';';
	}
	return filteredPaths;
}

bool IsOnlyInstance()
{
#if defined(VAX_CODEGRAPH)
	CStringW lockName(L"CG_" + VaDirs::GetHistoryDir());
#else
	CStringW lockName(L"VA_" + VaDirs::GetHistoryDir());
#endif
	lockName.Replace(L"\\", L"_");
	lockName.Replace(L":", L"_");
	lockName.Replace(L"/", L"_");
	if (sHistoryDirCountLock)
		CloseHandle(sHistoryDirCountLock);
	sHistoryDirCountLock = CreateSemaphoreW(NULL, 1, 15, lockName);
	DWORD err = GetLastError();
	return err == 0;
}

void CheckForUnsavedFiles()
{
	if (Psettings->m_autoBackup && IsOnlyInstance())
	{
		// look for unsaved files
		CStringW hdir = VaDirs::GetHistoryDir();
		FileList t;
		FindFiles(hdir, L"*_LastMod_*", t);
		CStringW unsavedfiles;
		for (FileList::const_iterator it = t.begin(); it != t.end(); ++it)
		{
			// move _lastmod_ files to ...UnSaved... to keep them from being nuked
			CStringW file = MSPath((*it).mFilename);
			if (file.GetLength())
			{
				CStringW newfile = file;
				newfile.Replace(L"_lastmod_", L"_UnSaved_");
				::_wremove(newfile); // remove prev unsaved file
				::_wrename(file, newfile);
				unsavedfiles += newfile + L";\n";
			}
		}

		/*
		if (unsavedfiles.GetLength())
		{
			WTString msg("Visual Assist has detected files that were not closed properly.  In the event that a crash "
			             "occurred in a previous Visual C++ instance, you may be able to recover lost changes by "
			             "inspecting the following file(s):\n\n");
			msg += CString(unsavedfiles);
			msg += "\nIf you suspect the Visual Assist symbol database is corrupt, you can rebuild it on the Setup tab "
			       "of the Visual Assist options dialog.";
			ErrorBox(msg);
		}
		*/
	}
}

static void DoCleanup()
{
	Log1("ComCleanup");
	if (!Psettings)
		return; // not setup.,,

#if !defined(VAX_CODEGRAPH)
	CheckForUnsavedFiles();
#endif // VAX_CODEGRAPH
	FreeTheGlobals();
	ExitLicense();
#if !defined(VAX_CODEGRAPH)
	WtAicUnregister();
#endif // VAX_CODEGRAPH
	if (g_pApp)
	{
		g_pApp->Release();
		g_pApp = NULL;
	}
	if (gDte)
		gDte.Release();
	if (gDte2)
		gDte2.Release();
}

VaAddinClient gVaAddinClient;

#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO)
// [case: 100743]
// http://www.developer.com/net/cplus/article.php/632041/Convert-modal-dialogs-to-modeless.htm
class CLicenseDlgThread : public CWinThread
{
	DECLARE_DYNCREATE(CLicenseDlgThread)
	CLicenseDlgThread() = default;
	~CLicenseDlgThread() = default;

	virtual BOOL InitInstance()
	{
		// http://stackoverflow.com/questions/3699633/webbrowser-control-mfc-created-in-seperate-thread-working-in-windows-7-and-vis
		::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

		_ASSERTE(gVaLicensingHost);
		if (gVaLicensingHost && gVaLicensingHost->InitLicense(&gVaAddinClient))
		{
			// this is dependent upon addin dll calling PreDefWindowProc or calling GetSettings
			RunFromMainThread([] {
				gVaAddinClient.LicenseInitComplete();
				if (gVaAddinClient.HasAddinAlreadyTriedSetup())
					gVaAddinClient.SetupVa(nullptr);
			});
		}
		else
			gVaAddinClient.LicenseInitFailed();

		::CoUninitialize();
		return FALSE;
	}
};

IMPLEMENT_DYNCREATE(CLicenseDlgThread, CWinThread);
#endif

#if !defined(VA_CPPUNIT) && !defined(RAD_STUDIO)
extern "C"
{
	_declspec(dllexport) void* VaConnectSetup(void* app, void* /*vaVsaddinHost*/)
	{
		DEBUG_THREAD_NAME("VAX:IDE Main Thread");
		if (g_mainThread != GetCurrentThreadId())
			g_mainThread = GetCurrentThreadId();
#ifdef DEBUG_MEM_STATS
		gMemStart.Checkpoint();
#endif
#ifdef DEBUG_WRAP_CHECK
		g_loggingEnabled = 0x7;
#endif

		if (app)
		{
			// CComQIPtr will QI the pointer passed to it, so we don't need to
			// know which environment we are actually in at this point.
			if (!gDte)
			{
				IUnknown* iunk = reinterpret_cast<IUnknown*>(app);
				gDte = iunk;
				if (gDte)
					gDte2 = gDte;
			}

			// If gDte didn't find the DTE interface, then we must be in vc6
			if (!gDte && !g_pApp)
			{
				g_pApp = reinterpret_cast<IApplication*>(app);
				g_pApp->AddRef();
			}

			_ASSERTE(g_pApp || gDte);
		}

		if (Psettings)
		{
			if (gShellAttr && gShellAttr->IsDevenv15OrHigher())
			{
				// reinit now that gDte is set due to registry virtualization
				Psettings->Init();
			}
		}
		else
			Psettings = new CSettings;

#if !defined(VA_CPPUNIT)
		if (gShellAttr && gShellAttr->IsDevenv14OrHigher() && gDte)
		{
			// [case: 91279]
			const CString vaVers(FILE_VERSION_STRING);
			const char* const vaRegValCheckName = "DefeatVsExtensionAutoUpdate";
			const CString lastCheck(GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, vaRegValCheckName));

			CComBSTR regRoot;
			gDte->get_RegistryRoot(&regRoot);
			CStringW regRootW = (const wchar_t*)regRoot;
			if (!regRootW.IsEmpty())
			{
				regRootW += L"\\ExtensionManager\\ExtensionAutoUpdateEnrollment";

				// extension identity ID as listed in vsix manifest
				CString regValName("44630d46-96b5-488c-8df926e21db8c1a3");
				regValName += L",";
				regValName += vaVers;

				const CString curVal(GetRegValue(HKEY_CURRENT_USER, CString(regRootW), regValName, "-1"));
				// [case: 98162]
				// Install from VS Gallery automatically creates the value before VA has ever run.
				// It will even change a pre-created value of 0 to 1, so there doesn't seem to be
				// any point of creating the value now for future builds.
				// Install from VS Gallery removes the entry for the currently installed version, so
				// process repeats on rollback.
				if (curVal == "-1" || (curVal != "0" && lastCheck != vaVers))
				{
					SetRegValue(HKEY_CURRENT_USER, CString(regRootW), regValName, "0");

					// note already done for this build
					SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, vaRegValCheckName, vaVers);
				}
			}
		}
#endif

		// if the other dll was to be the licensing host, then this
		// is where gVaLicensingHost would be initialized
		// _ASSERTE(vaVsaddinHost);
		// _ASSERTE(!gVaLicensingHost);
		// gVaLicensingHost = vaVsaddinHost;
		// (but as it is now, va_x.dll is the host)

		// [case: 100996]
		InitLicensingHost();

#if defined(RAD_STUDIO) || defined(NO_ARMADILLO) || defined(AVR_STUDIO)
		gVaAddinClient.LicenseInitComplete();
		return &gVaAddinClient;
#else

		if (
#ifdef _DEBUG
		    gShellAttr->IsDevenv14OrHigher()
#else
		    gShellAttr->IsDevenv15OrHigher()
#endif
		)
		{
			// [case: 100743]
			::AfxBeginThread(RUNTIME_CLASS(CLicenseDlgThread));
			return &gVaAddinClient;
		}
		else
		{
			_ASSERTE(gVaLicensingHost);
			if (gVaLicensingHost && gVaLicensingHost->InitLicense(&gVaAddinClient))
			{
				gVaAddinClient.LicenseInitComplete();
				return &gVaAddinClient;
			}

			gVaAddinClient.LicenseInitFailed();
			return nullptr;
		}
#endif
	}
}
#endif // !defined(VA_CPPUNIT) && !defined(RAD_STUDIO)

void VaAddinClient::LicenseInitFailed()
{
	mServiceProviderUnk = nullptr;

	// in dev15, this is dependent upon addin dll calling PreDefWindowProc
	RunFromMainThread([] {
		if (g_statBar)
		{
			if (::IsWindow(g_statBar->m_hWnd))
				g_statBar->UnsubclassWindow();
			delete g_statBar;
			g_statBar = NULL;
		}

		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		{
			// [case: 63872] don't delete VaService in dev11+ so that MEF calls get handled properly
			if (gVaService)
				gVaService->AbortInit();
		}
		else
		{
			delete gVaService;
			gVaService = nullptr;
		}

		delete GlobalProject;
		GlobalProject = nullptr;

		if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
		{
			if (gMainWnd && gMainWnd->m_hWnd)
				gMainWnd->Detach();
		}

		if (gShellAttr && gShellAttr->RequiresWin32ApiPatching())
			::PatchW32Functions(false);

		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 63872] don't delete Psettings in dev10+ so that
			// MEF and debugger calls get handled properly
			if (Psettings)
			{
				Psettings->m_validLicense = Psettings->m_enableVA = FALSE;

				// fire options updated so that MEF components get notice of
				// change of state since they default to enabled
				VaOptionsUpdated();
			}
		}
		else
		{
			delete Psettings;
			Psettings = nullptr;
		}

		delete g_IdeSettings;
		g_IdeSettings = nullptr;
	});
}

BOOL VaAddinClient::SetupVa(IUnknown* serviceProviderUnk)
{
	// [case: 100743]
	_ASSERTE(g_mainThread == ::GetCurrentThreadId());
	if (!mLicenseInit)
	{
		// license buy/try dialog hasn't closed yet
		mServiceProviderUnk = serviceProviderUnk;
		mSetupAttemptedBeforeInit = 1;
		return 1;
	}
	else if (!serviceProviderUnk)
		serviceProviderUnk = mServiceProviderUnk;

	try
	{
		const CStringW dbDir(VaDirs::GetDbDir());

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO) && !defined(VA_CPPUNIT)
		void MigrateLMtoCU();
		MigrateLMtoCU();
#endif

		if (g_loggingEnabled)
		{
			InitLogFile();
			LogUnfiltered("InitLogFile"); // not error but so basic that it should be always logged
		}

		CatLog("LowLevel", "VCC");
		CLEARERRNO;

		const CStringW tmpProjectDbPath = dbDir + L"Proj\\";
		CreateDir(tmpProjectDbPath);
		g_DBFiles.Init(dbDir, tmpProjectDbPath);
		g_DBFiles.SolutionLoadCompleted();

		// this will FlagForDbDirPurge() if a new build is installed
		VaDirs::CheckForNewDb();

		IncludeDirs incTok;
		if (!incTok.IsSetup() && !gShellAttr->IsDevenv())
		{
			WTString msg =
			    IDS_APPNAME " was unable to find a system include path.  Press ok to add an include path manually.";
			if (WtMessageBox(msg.c_str(), "Cannot set default values in the Directories node", MB_OKCANCEL) == IDOK)
			{
				DoVAOptions("C/C++ Directories"); // open to directory tab
				VaDirs::FlagForDbDirPurge();
			}
		}

		if (VaDirs::IsFlaggedForDbDirPurge())
			VaDirs::PurgeDbDir();

#if !defined(VAX_CODEGRAPH)
		if (IsOnlyInstance())
			CheckAutoSaveHistory();
#endif // VAX_CODEGRAPH

		AllocTheGlobals();
		gVaAddinClient.SettingsUpdated(VA_UpdateSetting_Init);

		if (serviceProviderUnk)
		{
			if (!gPkgServiceProvider)
			{
				gPkgServiceProvider = serviceProviderUnk;
				if (gPkgServiceProvider && gVaService)
					gVaService->VsServicesReady();
			}

			mServiceProviderUnk = nullptr;
		}

#if !defined(VAX_CODEGRAPH) && !defined(RAD_STUDIO)
		// [case: 118111]
		// don't show automatic display on 15u80OrHigher here but later when g_vaManagedPackageSvc is finally set
		if (!gShellAttr->IsDevenv15u8OrHigher())
			CheckForKeyBindingUpdate();

#if !defined(VA_CPPUNIT)
		if (gShellAttr->IsDevenv())
		{
			// [case:149451] if LaunchFirstRunDlg is shown, don't show tip of the day
			// if not, do same logic as before
			//if (!FirstRunDlg::LaunchFirstRunDlg()) // [case: 149451] todo: disabled to backout functionality
			//{
				CTipOfTheDay::LaunchTipOfTheDay();
				CInfoOfTheDay::LaunchInfoOfTheDay();
			//}
		}
#endif

#ifndef _DEBUG
		CheckForLatestVersion(FALSE);
#endif // _DEBUG
#endif // !VAX_CODEGRAPH && !RAD_STUDIO

#if !defined(VA_CPPUNIT) && !defined(RAD_STUDIO)
		CheckNavBarNextReminder();
#endif

		if (g_statBar && gShellAttr && gShellAttr->IsDevenv14OrHigher())
		{
			COleDateTime dateTime = COleDateTime::GetCurrentTime();

			if (dateTime.GetMonth() == 1 && dateTime.GetDay() == 1)
				g_statBar->SetStatusText("Visual Assist extension loaded. Happy New Year!");
			else
				g_statBar->SetStatusText("Visual Assist extension loaded");
		}

		return 1;
	}
	catch (...)
	{
		MessageBox(NULL, "A critical error occurred while initializing Visual Assist.", "Visual Assist",
		           MB_OK | MB_ICONERROR);
		mServiceProviderUnk = nullptr;
		return 0;
	}
}

void VaAddinClient::Shutdown()
{
	if (gVaLicensingHost)
	{
		auto tmp = gVaLicensingHost;
		gVaLicensingHost = nullptr;
		tmp->Shutdown();
	}

	if (Psettings && Psettings->m_catchAll)
	{
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			DoCleanup();
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("CC:");
		}
#endif // !SEAN
	}
	else
		DoCleanup();
}

tagSettings* VaAddinClient::GetSettings()
{
	// [case: 100743]
	// check !mServiceProviderUnk; if not clear, then init is still occurring
	if (!mLicenseInit || mServiceProviderUnk)
	{
#if defined(VA_CPPUNIT)
		_ASSERTE(!"unexpected call during unit tests");
#else
		if (g_mainThread == ::GetCurrentThreadId())
		{
			// check for unprocessed tasks from license thread
			::RunUiThreadTasks();
		}
#endif
		return nullptr;
	}

	return Psettings;
}

void VaAddinClient::SettingsUpdated(DWORD option)
{
	if (!Psettings || !Psettings->m_enableVA || !gShellAttr || !mLicenseInit)
		return;

	switch (option)
	{
	case DSM_OPTIONS:
		// VS Options dialog just closed
		{
			if (g_IdeSettings)
				g_IdeSettings->ResetCache();

			bool potentialColorChange = true;
			if (gColorSyncMgr)
				potentialColorChange = gColorSyncMgr->PotentialVsColorChange();

			if (potentialColorChange)
			{
				if (g_FontSettings)
					g_FontSettings->Update();
				if (g_pMiniHelpFrm)
					g_pMiniHelpFrm->SettingsChanged();
			}
		}

		if (gShellAttr && gShellAttr->IsDevenv() && !gShellAttr->IsDevenv10OrHigher())
			SetupVcEnvironment();
		if (g_CompletionSet)
			g_CompletionSet->RebuildExpansionBox();
		if (gVaInteropService)
			gVaInteropService->OptionsUpdated();
		return;

	case VA_UpdateSetting_CodeSnippetMgr:
		if (gVsSnippetMgr)
			gVsSnippetMgr->Refresh();
		return;

	case DSM_BUILD:
		_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new NuGetScan(true), ParserThread::priLow);
		return;

	case VA_UpdateSetting_ManageNuget:
		_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new NuGetScan(false));
		return;

	case DSM_WORKSPACE:
	case DSM_INFOVIEWER_SYNC:
		if (gShellAttr->IsMsdev() && !g_WorkSpaceTab)
			VAWorkspaceSetup_VC6();
		return;

	case VA_UpdateSetting_Init:
	case VA_UpdateSetting_AddinInit:
	case VA_UpdateSetting_Reset:
		IncludeDirs::Reset();
		if (g_FontSettings)
		{
			if (VA_UpdateSetting_Init != option)
			{
				// updates rev if colors have changed...
				g_FontSettings->Update();
			}
		}

		if (g_IdeSettings && VA_UpdateSetting_AddinInit != option)
		{
			// don't reset IDE settings cache during VA_UpdateSetting_AddinInit
			// because that breaks theming of the TOTD dialog when it is on
			// background thread (uncached IDESetting queries fail if not
			// on ui thread).
			g_IdeSettings->ResetCache();
		}

#if !defined(RAD_STUDIO)
		// signal windows to reformat
		if (MainWndH)
			::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
#endif
	}
}

int EditHelp(HWND h)
{
	if (Psettings && Psettings->oneStepHelp)
	{
		::SendMessage(h, WM_COMMAND, WM_HELP, 0);
		return DidHelpFlag; // since we don't have a class handle
	}
	return 0;
}

#if defined(RAD_STUDIO)
// MainVaWnd exists solely for processing of VA internal messages and timers that
// for VS we handle in gMainWnd via subclassing the IDE main window.
// Jerry's prototype worked by setting MainWndH to the main editor and calling
// gVaAddinClient.PreDefWindowProc from EdCnt::DefWindowProc.
// But the main editor is not created until a file is opened.
// And also don't want to subclass the RadStudio window like we do for VS.
class MainVaWnd : public CWnd
{
  public:
	MainVaWnd() = default;
	~MainVaWnd() = default;

	virtual LRESULT DefWindowProc(UINT msg, WPARAM p1, LPARAM p2)
	{
		if (gVaAddinClient.PreDefWindowProc(msg, p1, p2))
			return TRUE;
		return __super::DefWindowProc(msg, p1, p2);
	}
};
#endif

void VaAddinClient::SetMainWnd(HWND h)
{
	if (!h || gMainWnd)
	{
		_ASSERTE(!h || h == MainWndH);
		_ASSERTE(gMainWnd == gVaMainWnd);
		return;
	}

	MainWndH = h;
	_ASSERTE(!gMainWnd);

#if defined(RAD_STUDIO)
	gMainWnd = &sMainWnd;
	gMainWnd->Attach(MainWndH);

	gVaMainWnd = new MainVaWnd();
	gVaMainWnd->CreateEx(WS_EX_NOPARENTNOTIFY | WS_EX_NOACTIVATE, "STATIC", "VA_Main", WS_CHILD, {0, 0, 0, 0}, gMainWnd,
	                     0);
#else
	_ASSERTE(gShellAttr);

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		WPF_ViewManager::Create();
		_ASSERTE(gMainWnd && gMainWnd->m_hWnd);
	}
	else
	{
		gMainWnd = &sMainWnd;
		gMainWnd->Attach(MainWndH);
	}

	gVaMainWnd = gMainWnd;
	_ASSERTE(gVaMainWnd && gVaMainWnd->GetSafeHwnd());

	// Get Statusbar from MainWnd
	if (!g_statBar)
		g_statBar = new StatusWnd;
	HWND hStat = GetDlgItem(h, ID_STATUS);
	if (!hStat)
		hStat = GetDlgItem(h, 0x64);
	g_statBar->SetStatusBarHwnd(hStat);
#endif

	if (g_FontSettings)
		g_FontSettings->Update(FALSE);
}

int VaAddinClient::ErrorBox(const char* msg, unsigned int options)
{
	return ::ErrorBox(msg, options);
}

void VaAddinClient::CloseWorkspace()
{
	if (GlobalProject)
		GlobalProject->CloseProject();
}

void VaAddinClient::LoadWorkspace(const WCHAR* projFiles)
{
	if (Psettings && Psettings->m_enableVA && mLicenseInit)
	{
#if !defined(VA_CPPUNIT)
		_ASSERTE(gShellAttr && (gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder()));
#endif
		GlobalProject->LaunchProjectLoaderThread(projFiles);
	}
}

BOOL VaAddinClient::CheckSolution()
{
	if (Psettings && Psettings->m_enableVA && mLicenseInit)
	{
		if (gShellAttr && gShellAttr->IsDevenv15OrHigher() && !::IsInOpenFolderMode())
		{
			// [case: 116692]
			// CheckSolution is called in vs2017+ after View.Branch is
			// executed, so that VA can rescan directories upon change of
			// git branch.  Rescan only necessary when in open folder mode.
			// Return without doing anything when normal solution is loaded.
			// (In open folder mode, CheckForNewSolution will return false
			// but trigger an async check and reload if necessary).
			return false;
		}

		// [case: 105291] lightweight solution load support would require
		// changes in here, but since this block isn't hit in vs2010+, none made
		_ASSERTE(gShellAttr && gShellAttr->IsDevenv() &&
		         (!gShellAttr->IsDevenv10OrHigher() || (gShellAttr->IsDevenv15OrHigher() && ::IsInOpenFolderMode())));
		Log("CheckSolution");
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			if (!GlobalProject || GlobalProject->CheckForNewSolution())
				return true;
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("CheckSolution:");
		}
#endif // !SEAN
	}

	return false;
}

BOOL VaAddinClient::LoadSolution()
{
	if (!Psettings || !Psettings->m_enableVA || !GlobalProject || !mLicenseInit)
		return FALSE;

	_ASSERTE(gShellAttr && gShellAttr->IsDevenv() &&
	         (!gShellAttr->IsDevenv10OrHigher() || gShellAttr->IsDevenv15OrHigher()));
	Log("LoadSolution");
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		return GlobalProject->LaunchProjectLoaderThread(CStringW());
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("LoadSolution:");
	}
	return FALSE;
#endif // !SEAN
}

void VaAddinClient::AddFileToProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile)
{
	if (GlobalProject)
		GlobalProject->AddFile(pProject, pfile, nonbinarySourceFile);
}

void VaAddinClient::RemoveFileFromProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile)
{
	if (GlobalProject)
		GlobalProject->RemoveFile(pProject, pfile, nonbinarySourceFile);
}

void VaAddinClient::RenameFileInProject(const WCHAR* pProject, const WCHAR* pOldfilename, const WCHAR* pNewfilename)
{
	if (GlobalProject)
		GlobalProject->RenameFile(pProject, pOldfilename, pNewfilename);
}

int VaAddinClient::GetTypingDevLang()
{
	return gTypingDevLang;
}

void VaAddinClient::ExecutingDteCommand(int execing, LPCSTR command)
{

	if (execing)
	{
		mDteExecutingCommandsCount++;

		std::lock_guard<std::mutex> lck(mDteExecutingCommandsMutex);
		mDteExecutingCommands.emplace_back(command);
	}
	else
	{
		mDteExecutingCommandsCount--;

		std::lock_guard<std::mutex> lck(mDteExecutingCommandsMutex);
		auto it = std::find(mDteExecutingCommands.cbegin(), mDteExecutingCommands.cend(), command);

		_ASSERTE(it != mDteExecutingCommands.cend());

		if (it != mDteExecutingCommands.cend())
		{
			mDteExecutingCommands.erase(it);
			_ASSERTE(mDteExecutingCommands.size() == (uint32_t)(int)mDteExecutingCommandsCount);
		}
	}
}

void VaAddinClient::SaveBookmark(LPCWSTR filename, int lineNo, BOOL clearAllPrevious)
{
	if (!g_VATabTree || !filename || !*filename)
		return;

	if (clearAllPrevious)
	{
		// only clearAllPrevious if VA was actually active for filename since
		// this will be called for any text doc window that is closed.  If VA
		// wasn't attached to the window, then it didn't get a chance to set
		// the saved bookmarks in the first place.
		AutoLockCs l(g_EdCntListLock);
		for (EdCntPtr ed : g_EdCntList)
		{
			if (ed && !ed->FileName().CompareNoCase(filename))
			{
				g_VATabTree->RemoveAllBookmarks(CStringW(filename));
				break;
			}
		}
	}

	if (lineNo > 0)
		g_VATabTree->AddBookmark(CStringW(filename), lineNo - 1);
}

void 
VaAddinClient::CheckForLicenseExpirationIfEnabled()
{
	if (gTestsActive)
		return;

	if (!gVaLicensingHost)
	{
		// [case: 117498] / [case: 100743]
		return;
	}

#if defined(AVR_STUDIO) || defined(RAD_STUDIO) || defined(VA_CPPUNIT)
#else
	WTAutoUpdater updater;
	bool sancLicense = gVaLicensingHost->IsSanctuaryLicenseInstalled();

	CString insantActivation = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "InstantEnableRenewalNotif");
	if (insantActivation == "yes")
	{
		if (sancLicense)
		{
			void UnhideCheckboxForRenewableSanctuaryLicense();
			UnhideCheckboxForRenewableSanctuaryLicense();
		}

		if (!sancLicense && gVaLicensingHost->GetLicenseUserCount() == 1)
		{
			CString armUser = gVaLicensingHost->GetLicenseInfoUser(true);
			bool UnhideCheckboxForRenewableArmadilloLicences(const LPCSTR userIn);
			if (armUser != "")
			{
				UnhideCheckboxForRenewableArmadilloLicences(armUser);
			}
		}
	}
	CString h = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "HiddenRecommendUpdate");
	if (Psettings && Psettings->mRecommendAfterExpires && h == "No")
		updater.CheckForLicenseExpiration();
#endif
}


CComPtr<IVsThreadedWaitDialog2> GetVsThreadedWaitDialog2()
{
	CComQIPtr<IVsThreadedWaitDialog2> vsDlg;
	if (!gPkgServiceProvider)
		return vsDlg;

	IUnknown* tmp = NULL;
	gPkgServiceProvider->QueryService(SID_SVsThreadedWaitDialog, IID_IVsThreadedWaitDialog2, (void**)&tmp);
	if (!tmp)
		return vsDlg;

	vsDlg = tmp;
	return vsDlg;
}

Project::~Project()
{
	mSolutionEventListener.reset();
	CloseProject();
}

void Project::CloseProject()
{
	const bool kCalledOnUiThread = ::GetCurrentThreadId() == g_mainThread;
	NestedTrace t("Closing solution and projects");
	_ASSERTE(_CrtCheckMemory());

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (kCalledOnUiThread)
		{
			// clear ClassBrowser on UI thread
			ClassViewSym.Empty();
			if (g_CVAClassView && g_CVAClassView->m_hWnd && IsWindow(g_CVAClassView->m_hWnd))
			{
				g_CVAClassView->ClearHcb();
				g_CVAClassView->m_fileView = FALSE;
			}

			::LoadProjectMRU(L"");
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("Pr::LPLT:");
	}
#endif // !SEAN

	if (kCalledOnUiThread)
	{
		ProjectReparse::ClearProjectReparseQueue();
		if (gVaService && gVaService->GetOutlineFrame())
			gVaService->GetOutlineFrame()->Clear();
		if (gVaService && gVaService->GetHashtagsFrame())
			gVaService->GetHashtagsFrame()->Clear();
	}

	// Clean using namespaces from previous project
	MultiParse::ResetDefaultNamespaces();
	if (kCalledOnUiThread)
	{
		// stop loader thread if still running (when this isn't running from it)
		ProjectLoaderRef pl;
		if (pl)
			pl->Stop();
	}

	const int kMaxThreadCount =
	    kCalledOnUiThread ? 0 : 1; // if this is the projectLoad thread, then expect g_threadCount == 1
	if (g_threadCount > kMaxThreadCount)
	{
		// wait for other threads to exit before closing db's
		bool once = true;
		const DWORD startTicks = ::GetTickCount();
		for (int i = 400; i && g_threadCount > kMaxThreadCount; i--)
		{
#if defined(VA_CPPUNIT)
			Sleep(10);
#else
			Sleep(25);
#endif // !VA_CPPUNIT
			Log("Project::CloseProject waiting for active threads");

			if (once && (::GetTickCount() - startTicks) > 1000)
			{
				if (kCalledOnUiThread)
				{
					// don't wait more than 1 sec on ui thread since SendMessage will be blocked if we spin here
					break;
				}

				// only assert once in this loop, but not within the first second
				once = false;
#if !defined(VA_CPPUNIT)
				_ASSERTE(!"Project::CloseProject waiting for active threads");
#endif // !VA_CPPUNIT
			}
		}
	}

	{
		RWLockWriter lck(mFilelistLock);
		mProjectFileItemList.clear();
		if (kCalledOnUiThread)
		{
			mRawProjectList.Empty();
			SetSolutionFile(CStringW());
		}
		mReferencesList.clear();
		mSolutionPrivateSystemHeaders.clear();
		mSolutionPrivateSystemIncludeDirs.clear();
	}

	if (gFileFinder)
		gFileFinder->SaveAndClear();

	mUsingDirs.clear();
	ClearMap();
	mAddlDirListNeedsUpdate = true;
	FilesListNeedsUpdate();

	if (gFileIdManager)
		gFileIdManager->QueueSave();

	if (gShellIsUnloading)
		mProjectHash = UINT_MAX;

	_ASSERTE(_CrtCheckMemory());
}

bool Project::LaunchProjectLoaderThread(CStringW projects)
{
	CatLog("Environment.Project", "LoadProject");
	if (RefactoringActive::IsActive())
	{
		// [case: 43506]
		Log1("reject - refactoring active");
		vLog("Project::LaunchProjectLoaderThread schedule reload of solution\n");
		if (gVaMainWnd)
		{
			gVaMainWnd->KillTimer(IDT_SolutionReloadRetry);
			gVaMainWnd->SetTimer(IDT_SolutionReloadRetry, 2000, NULL);
		}
		return false;
	}

	CComPtr<IVsThreadedWaitDialog2> waitDlg;

#ifdef RAD_STUDIO
	RadStatus radStatus(L"ProjectLoader", L"VA: Loading projects", L"VA: Projects loaded");
#else
	if (gShellAttr->IsDevenv11OrHigher() && gDte)
	{
		try
		{
			CComPtr<EnvDTE::_Solution> pSolution;
			gDte->get_Solution(&pSolution);
			if (pSolution)
			{
				const CStringW solutionFile = ::GetSolutionFileOrDir();
				if (solutionFile.GetLength() > 1)
				{
					waitDlg = ::GetVsThreadedWaitDialog2();
					if (waitDlg)
					{
						CComVariant jnk;
						waitDlg->StartWaitDialog(CComBSTR(CStringW(IDS_APPNAME)),
						                         CComBSTR(L"Reading solution projects..."), nullptr, jnk, nullptr, 2,
						                         VARIANT_FALSE, VARIANT_FALSE);
					}
				}
			}
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%lX (%s) in (%s)\n"), e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in Project::LaunchProjectLoaderThread 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in Project::LaunchProjectLoaderThread");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in Project::LaunchProjectLoaderThread 2");
			}
		}
#endif // !SEAN
	}
#endif // !RAD_STUDIO

	CloseProject();
	bool msgAboutThoroughReferences = Psettings->mEnumerateVsLangReferences;
	std::shared_ptr<VsSolutionInfo> sfc;
	if (gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder())
	{
		msgAboutThoroughReferences = false;
#if !defined(RAD_STUDIO)
		if (!g_WorkSpaceTab)
			VAWorkspaceSetup_VC6();
#endif
	}
	else
	{
#if defined(VA_CPPUNIT)
		extern std::shared_ptr<VsSolutionInfo> gFakeSfc;
		if (gFakeSfc)
		{
			_ASSERTE(projects.IsEmpty());
			sfc = gFakeSfc;
			sfc->GetSolutionInfo();
			projects = sfc->GetProjectListW();
			SetSolutionFile(sfc->GetSolutionFile());
		}
#else
		_ASSERTE(projects.IsEmpty());
		_ASSERTE(gDte);

		if (gShellAttr && gShellAttr->IsDevenv15OrHigher() && gDte)
		{
			mVcFastProjectLoadEnabled = true; // default to enabled
			CComBSTR regRoot;
			gDte->get_RegistryRoot(&regRoot);
			CStringW regRootW = (const wchar_t*)regRoot;
			if (!regRootW.IsEmpty())
			{
				const CString regValName("EnableProjectCaching");
				const CStringW reg(regRootW + L"\\ApplicationPrivateSettings\\LanguageService\\C/C++\\Roaming");
				const CString curVal(GetRegValue(HKEY_CURRENT_USER, CString(reg), regValName, "unset"));
				if (curVal == "unset")
				{
					// vs2017 15.3 preview 2 (to XX ??)
					const CStringW reg2(regRootW + L"\\VC");
					const DWORD curVal2(GetRegDword(HKEY_CURRENT_USER, CString(reg2), regValName, (DWORD)-1));
					if (curVal2 != (DWORD)-1)
					{
						if (!curVal2)
							mVcFastProjectLoadEnabled = false;
					}
					else
					{
						vLogUnfiltered("WARN: did not read EnableProjectCaching value (%s)", (LPCTSTR)curVal);
					}
				}
				else
				{
					// vs2017 15.3 preview 1 and earlier
					// vs2017 15.8+
					if (curVal == "0*System.Boolean*True")
					{
					}
					else if (curVal == "0*System.Boolean*False")
					{
						mVcFastProjectLoadEnabled = false;
					}
					else
					{
						vLogUnfiltered("ERROR: unknown EnableProjectCaching value (%s)", (LPCTSTR)curVal);
					}
				}
			}
		}

		sfc.reset(::CreateVsSolutionInfo());
		const DWORD stTime = ::GetTickCount();
		sfc->GetSolutionInfo();
		if (msgAboutThoroughReferences)
		{
			const DWORD getSolInfoDuration = ::GetTickCount() - stTime;
			if (getSolInfoDuration < (DWORD)(gShellAttr->IsDevenv15() ? 7500 : 15000))
				msgAboutThoroughReferences = false;
		}
		projects = sfc->GetProjectListW();
		SetSolutionFile(sfc->GetSolutionFile());

#if 0 // loader thread now calls to ui thread
		if (mVcFastProjectLoadEnabled)
		{
			// [case: 100735]
			// finish asynchronously on ui thread when C++ Faster Project Load is enabled
			vLog("synchronous solution load due to C++ Faster Project Load");
			sfc->FinishGetSolutionInfo(true);
		}
#endif
#endif
		mSolutionEventListener = sfc;
	}

	mIsLoading = true;
	m_loadingProjectFiles = true;
	mRawProjectList = projects;

	LoadProjectMRU(projects);
	//	new FunctionThread(LoadProjectMRU, projects, "LoadProjectMRU", true);

	IncludeDirs::Reset();
	mProjectHash = WTHashKeyW(mRawProjectList);
	mProjectDbDir = VaDirs::GetDbDir() + L"Proj";
	if (sfc)
		sfc->SetSolutionHash(mProjectHash);
	if (mProjectHash)
		mProjectDbDir += L"_" + utosw(mProjectHash);
	mProjectDbDir += L"\\";
	CreateDir(mProjectDbDir);
	// always create thread so that db dirs can be updated via the db dir lock
	new ProjectLoader(sfc);

	// set watchDir so that project modifications are picked up even if no
	// file has been opened yet (affects ofis and platform in vs2010)
	if (projects.GetLength() > 1 && projects[1] == L':')
	{
		CStringW dir = projects.Mid(0, 2) + L"\\";
		extern WCHAR g_watchDir[];
		wcscpy(g_watchDir, dir);
	}
	_ASSERTE(_CrtCheckMemory());

	if (waitDlg)
	{
		BOOL jnk = FALSE;
		waitDlg->EndWaitDialog(&jnk);
	}

	// [case: 98091]
	if (msgAboutThoroughReferences && (gShellAttr->IsDevenv8() || gShellAttr->IsDevenv9() || gShellAttr->IsDevenv15()))
	{
		// see if all projects are native without clr or winrt
		if (sfc)
		{
			{
				RWLockReader lck(mFilelistLock);
				if (mReferencesList.size() || sfc->GetReferences().size())
					msgAboutThoroughReferences = false;
			}

			if (msgAboutThoroughReferences)
			{
				AutoLockCs lck(sfc->GetLock());
				VsSolutionInfo::ProjectSettingsMap& solInfoMap = sfc->GetInfoMap();
				for (VsSolutionInfo::ProjectSettingsMap::iterator solit = solInfoMap.begin(); solit != solInfoMap.end();
				     ++solit)
				{
					VsSolutionInfo::ProjectSettingsPtr prj = (*solit).second;
					if ((!prj->mIsVcProject && ::IsFile(prj->mProjectFile)) || prj->mReferences.size() ||
					    prj->mCppIsWinRT || prj->mCppHasManagedConfig)
					{
						msgAboutThoroughReferences = false;
						break;
					}
				}

				if (!solInfoMap.size())
					msgAboutThoroughReferences = false;
			}
		}

		if (msgAboutThoroughReferences)
		{
			const TCHAR* infoMsg =
			    "Solution load time was excessive.  This may have been due to thorough assembly reference "
			    "evaluation.\r\n\r\n"
			    "If you work only in native C/C++ projects, solution load time can be improved by disabling thorough "
			    "reference evaluation on the Projects and Files page of the Visual Assist Options dialog.\r\n\r\n"
			    "Would you like Visual Assist to disable the option?"
			    "\r\n\r\n(This message will not be displayed again.)";

			if (IDYES == ::OneTimeMessageBox(_T("SlowSolutionLoad"), infoMsg, MB_YESNO | MB_ICONQUESTION))
				Psettings->mEnumerateVsLangReferences = false;
		}
	}

	return true;
}

void DumpMemStatistics(const WCHAR* msg)
{
#ifdef DEBUG_MEM_STATS
	CMemoryState memEnd, memDiff;
	memEnd.Checkpoint();
	memDiff.Difference(gMemStart, memEnd);
	OutputDebugStringW(msg);
	memDiff.DumpStatistics();

	// _lCurAlloc is a static in dbgheap.c that has no accessor but you can
	// put it in the watch window to see allocation total at breakpoints.
	// use this expression for MB display: (_lCurAlloc / 1024) / 1024,d
	// adding up bytes of all 5 blocks dumped by CMemoryState::DumpStatistics
	// will give the same number of bytes.
	// in vs2015 the variable is __acrt_current_allocations.
	// use (__acrt_current_allocations / 1024) / 1024,d
#endif
}

void SimpleFileScanThread(LPVOID)
{
	// [case: 1029] for Psettings->mOfisIncludeSystem support
	SimpleRecursiveFileScan srfs;
}

struct ModifiedFilesInfo
{
	bool mCollected = false;
	bool mCheckNetDbLoaded = false;

	std::set<UINT> deletedProjFilesThatNeedRemoval;
	std::vector<CStringW> projGlobalsThatNeedUpdate;
	std::vector<CStringW> projSrcThatNeedUpdate;
	std::vector<CStringW> solSysFilesThatNeedUpdate;

	// these do not impact decision to rebuild since they don't affect the project data dir
	std::vector<CStringW> sysFilesThatNeedUpdate;

	// these do not impact decision to rebuild since they are simple parse (not remove or update of stale data)
	std::vector<CStringW> projGlobalsThatNeedLoad;
	std::vector<CStringW> projSrcThatNeedLoad;
	std::vector<CStringW> sysFilesThatNeedLoad;
	std::vector<CStringW> solSysFilesThatNeedLoad;

	int GetModifiedProjectFileCount() const
	{
		return int(deletedProjFilesThatNeedRemoval.size() + projGlobalsThatNeedUpdate.size() +
		           projSrcThatNeedUpdate.size() + solSysFilesThatNeedUpdate.size());
	}
};

// not static only so that value can be read in dumps
std::atomic<int> sPendingLoads;

void Project::LoadProject(std::shared_ptr<VsSolutionInfo> sfc)
{
	FileFinder_ProjectLoadingStarted();

	++sPendingLoads;
	StopIt = true;
	DatabaseDirectoryLock lck;
	--sPendingLoads;
	if (sPendingLoads)
	{
		mCleanLoad = false;
		throw WtException("multiple pending project loads");
	}

	if (gShellIsUnloading)
		throw UnloadingException();

#if !defined(SEAN)
	if (!gShellAttr->IsDevenv15())
	{
		// Launch AutoTest before load so that stats are logged during db build
		// Find In Files fails in 15.6 and 15.7 when this block is used.
		// VS2017 AST is started at #AutoStartAST
		static bool once = true;
		if (once && !mRawProjectList.IsEmpty())
		{
			CString cmdLine = ::GetCommandLine();
			cmdLine.MakeLower();
			if (cmdLine.Find("vaautomationtests") != -1)
			{
				if (cmdLine.Find("\\vaautomationtests\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_cppb\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_ci\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_ue\\vaautomationtests_") != -1)
				{
					// Find in files will fail if exec'd before VS has completed
					// loading sln; wait and block all VA processing
					::Sleep(10000);
					::RunFromMainThread<void, BOOL>(RunVAAutomationTest, TRUE);
				}
			}
			once = false;
		}
	}
#endif // !SEAN

	::DumpMemStatistics(L"\nVaMemStats: LoadProject enter\n");

#ifdef DEBUG_MEM_STATS
	static CMemoryState sLastMemCheckpoint;
	static bool once = true;
	if (once)
	{
		once = false;
		sLastMemCheckpoint.Checkpoint();
	}
#endif

	::OutputDebugStringA2("FEC: load 1");
	CloseProject();
	::OutputDebugStringA2("FEC: load 2");

#ifdef _DEBUG
	bool defaultSchedulerPolicyStillOverridden = false;
	try
	{
		Concurrency::Scheduler::SetDefaultSchedulerPolicy(Concurrency::SchedulerPolicy(
		    2, Concurrency::MinConcurrency, 1, Concurrency::MaxConcurrency, Concurrency::MaxExecutionResources));
		_ASSERTE(!"unexpected - defaultSchedulerPolicy was reset?");
	}
	catch (const Concurrency::default_scheduler_exists&)
	{
		defaultSchedulerPolicyStillOverridden = true;
	}

	if (!defaultSchedulerPolicyStillOverridden)
	{
		vCatLog("Environment.Project", "Pr::LP unexpected defaultSchedulerPolicy - reset to default\n");
	}
#endif // _DEBUG

	mCleanLoad = true;
	if (!gShellIsUnloading)
		StopIt = false;
	TimeTrace tr("Loading solution and projects");
	WrapperCheckDecoy chk(g_pUsage->mSolutionsOpened);
	DEFTIMERNOTE(LoadProjectTimer, NULL);

#ifdef RAD_STUDIO
	RadStatus radStatus(L"LoadProject", L"VA - Loading projects", L"VA - Projects loaded");
#endif

	mAddlDirListNeedsUpdate = true;
	mIsLoading = true;
	m_loadingProjectFiles = true;
	mSolutionIsInitiallyEmpty = false;
#if defined(VA_CPPUNIT)
	mCppUsesClr = gOverrideClr;
	mCppUsesWinRT = gOverrideWinRT;
#else
	mCppUsesClr = false;
	mCppUsesWinRT = false;
#endif // VA_CPPUNIT
	mRequiresNetDb = true;
	SetContainsUnrealEngineProject(false);
	SetContainsUnityEngineProject(false);

	{
		RWLockWriter lck2(mFilelistLock);
		mProjectFileItemList.clear();
		FilesListNeedsUpdate();
		mSolutionPrivateSystemHeaders.clear();
		mSolutionPrivateSystemIncludeDirs.clear();
		sUE4Project.Empty();
	}

	if (sfc)
	{
		std::vector<CStringW> ueInstallDirs;

		if (gShellAttr && gShellAttr->IsDevenv14OrHigher() && gDte)
		{
			// [case: 143077] improve detection for UE4 project by using custom VA GlobalSection
			std::map<WTString, WTString> ueVAParameters =
			    ParseCustomGlobalSection("44630d46-96b5-488c-8df926e21db8c1a3");
			vCatLog("Environment.Project", "UE4: List all UE parameters from VA global section if found:\n");
			for (auto& param : ueVAParameters)
			{
				vCatLog("Environment.Project", "UE4:    %s=%s\n", param.first.c_str(), param.second.c_str());

				if (param.first.CompareNoCase(L"ueInstallDir") == 0 && !param.second.IsEmpty())
				{
					// [case: 141741] read ueInstallDir from solution file if exists
					param.second.MakeLower();
					ueInstallDirs.emplace_back(param.second.Wide());
					continue;
				}

				if (param.first.CompareNoCase(L"ueVersion") == 0 && !param.second.IsEmpty())
				{
					vCatLog("Environment.Project", "UE4:        solution has been detected as UE solution\n");
					SetContainsUnrealEngineProject(true);
					continue;
				}
			}
		}

		if (!GetContainsUnrealEngineProject()) // ueSolution not detected as parameter from reading GlobalSection; go
		                                       // with legacy detection for UE4
		{
			vCatLog("Environment.Project", "UE4: ueVersion parameter not found, check if there is UE4 named project (old style detection)\n");

			// [case: 114560]
			SemiColonDelimitedString unrealDirectories = GetUe4Dirs();

			// [case: 141741] if ueInstallDir has not been found in Solution parameters, get it from registry
			if (ueInstallDirs.empty())
			{
				LPCWSTR directory = nullptr;
				int directoryLength = 0;
				unrealDirectories.Reset();
				while (unrealDirectories.NextItem(directory, directoryLength))
				{
					if (directoryLength > 0)
					{
						CStringW ueInstallPath(directory, directoryLength);
						ueInstallPath.MakeLower();
						ueInstallPath.TrimRight(L"engine");
						ueInstallDirs.emplace_back(ueInstallPath);
					}
				}
			}

			for (auto projectInfo : sfc->GetInfoMap())
			{
				if (IsFile(projectInfo.second->mProjectFile))
				{
					// [case: 113964] detect UE4 project
					CStringW projectName =
					    projectInfo.second->mProjectFile.Mid(projectInfo.second->mProjectFileDir.GetLength());
					if (strstrWholeWord((const wchar_t*)projectName, L"ue4", FALSE) ||
					    strstrWholeWord((const wchar_t*)projectName, L"ue5", FALSE))
					{
						// found project named UE4
						SetContainsUnrealEngineProject(true);
						// save project so that GetUe4Dirs returns it even if solution never loaded from unreal editor
						sUE4Project = projectInfo.second->mProjectFile;
						vCatLog("Environment.Project", "UE4: project named UE4 found\n");
					}
					else
					{
						// check for any project from a UE4 source directory
						vCatLog("Environment.Project", "UE4: check for any project from a UE4 source directory\n");
						LPCWSTR directory = nullptr;
						int directoryLength = 0;
						unrealDirectories.Reset();
						while (unrealDirectories.NextItem(directory, directoryLength))
						{
							if (directoryLength > 0)
							{
								if (_wcsnicmp(directory, projectInfo.second->mProjectFile, (uint)directoryLength) == 0)
								{
									vCatLog("Environment.Project", "UE4:    found project %s\n",
									     (LPCTSTR)CString(projectInfo.second->mProjectFile));
									SetContainsUnrealEngineProject(true);
									break;
								}
							}
						}
					}
					if (GetContainsUnrealEngineProject())
						break;
				}
			}
		}

		if (Psettings->mUnrealEngineAutoDetect == 1)
		{
			// [case: 113964]
			const bool oldVal = Psettings->mUnrealEngineCppSupport;
			Psettings->mUnrealEngineCppSupport = GetContainsUnrealEngineProject();
			if (oldVal != GetContainsUnrealEngineProject())
				ReloadDFileMP(Src); // [case: 116049]
		}

		sfc->FinishGetSolutionInfo();
		if (gShellIsUnloading)
		{
			mCleanLoad = false;
			throw UnloadingException();
		}

		if (Psettings->mUnrealEngineCppSupport)
		{
			// [case: 149728] check .uproject file to find which version of UE is installed
			CStringW slnLocation = ::Path(sfc->GetSolutionFile());
			FileList uprojFiles;
			FindFiles(slnLocation, L"*.uproject", uprojFiles);
			for (const auto& uprojFile : uprojFiles)
			{
				// normally there will be only one .uproject file here but if for any reason more of them are found, VA is
				// going to try to read them one by one and use the version from the first file that have this information
				WTString uprojFileText;
				if(!uprojFileText.ReadFile(uprojFile.mFilename))
					continue;

				namespace rapidJson = RAPIDJSON_NAMESPACE;
				rapidJson::Document jsDoc;
				if (!jsDoc.Parse(uprojFileText.c_str()).HasParseError())
				{
					if (jsDoc.HasMember("EngineAssociation"))
					{
						rapidJson::Value& engas = jsDoc["EngineAssociation"];
						if (engas.IsString())
						{
							mUnrealEngineVersion = engas.GetString();
							break;
						}
					}
				}
			}
			
			// [case: 114560] create here UE folders which will be ignored during parse; do it here since we want to
			// avoid string manipulation during parse time
			for (const auto& ueInstDir : ueInstallDirs)
			{
				// populate map of plugin dirs
				std::vector<CStringW> ignorePluginsPathsList;

				CStringW pathPlugins = ueInstDir;
				pathPlugins.Append(LR"(\Engine\Plugins\)");
				pathPlugins.Replace(LR"(\\)", LR"(\)");
				pathPlugins.MakeLower();
				ignorePluginsPathsList.emplace_back(pathPlugins);

				mUEIgnorePluginsDirs.emplace(ueInstDir, ignorePluginsPathsList);
			}

			LogElapsedTime et("Proj::FixUE4Incs", 1000);

			auto AddMissingPublicInclude = [](const FileInfo& buildFile, FileList& includes) {
				// [case: 141985] [ue4] add include paths are incorrect in 4.25 when bLegacyPublicIncludePaths = true
				//
				// The expected public include directories are missing in 4.25 when bLegacyPublicIncludePaths = true
				// (the default), causing bad Add Include paths. Adding all of the necessary includes to support legacy
				// includes is slow, and legacy includes are unexpectedly still enabled while developers may expect it
				// to be disabled. Instead of supporting legacy includes, add a single include so that Add Include will
				// function as if legacy includes were disabled. If users wish to use legacy style includes, they can
				// set add include style to limit to filename.
				int lastSlashIdx = buildFile.mFilenameLower.ReverseFind(L'\\');
				if (lastSlashIdx != -1)
				{
					CStringW publicInclude = buildFile.mFilename.Mid(0, lastSlashIdx + 1) + L"Public";
					includes.AddUniqueNoCase(publicInclude);
				}
			};

			auto AddMissingGeneratedInclude = [](const FileInfo& buildFile, FileList& includes) {
				// [case: 118239] [ue4] add include dirs missing from Unreal Engine 4.20.1 game projects
				//
				// example path to *.Build.cs:		VehicleGame\Source\VehicleGame\VehicleGame.Build.cs
				// exmaple path to *.generated.h:
				// VehicleGame\Intermediate\Build\Win64\UE4Editor\Inc\VehicleGame\*.generated.h
				//
				// Folders inside the Inc directory, which contain the *.generated.h files, always share a
				// name with a *.Build.cs file.
				int lastSlashIdx = buildFile.mFilenameLower.ReverseFind(L'\\');
				if (lastSlashIdx != -1)
				{
					// get a name of the file and strip '.Build.cs' from it; what is left is also the name of the folder for '.generated.h' files 
					CStringW filename = buildFile.mFilename.Mid(lastSlashIdx + 1, buildFile.mFilenameLower.GetLength() -
					                                                                  lastSlashIdx - 10);
					int sourceIdx = buildFile.mFilenameLower.Find(L"\\source\\");
					if (sourceIdx != -1)
					{
						CStringW includeDir = buildFile.mFilename.Left(sourceIdx);
						
						// [case: 164542] in UE5 the location of .generated.h files has changed so we need to get correct location
						int ueMajorVersion = 4; // default as was before
						if (GlobalProject)
						{
							std::pair<int, int> ueVersionNumber = GlobalProject->GetUnrealEngineVersionAsNumber();
							ueMajorVersion = ueVersionNumber.first;
						}

						if (ueMajorVersion >= 5)
							includeDir.Append(L"\\Intermediate\\Build\\Win64\\UnrealEditor\\Inc\\");
						else
							includeDir.Append(L"\\Intermediate\\Build\\Win64\\UE4Editor\\Inc\\");

						includeDir.Append(filename);
						if (ueMajorVersion >= 5)
							includeDir.Append(L"\\UHT");
						includes.AddUnique(includeDir);
					}
				}
			};

			std::list<CStringW> pluginList;
			std::vector<CStringW> projectPathList;

			// Iterate through each project and find the *.Build.cs files that they contain. Each *.Build.cs file is a
			// seperate compilation by the Unreal Build Tool with the potential to create *.generated.h files. The path
			// to the *.Build.cs file can be used to construct important include directories that may be missing.
			for (auto projectInfo : sfc->GetInfoMap())
			{
				if (IsFile(projectInfo.second->mProjectFile))
				{
					const CStringW ext(::GetBaseNameExt(projectInfo.second->mProjectFile));
					if (ext.CompareNoCase(L"vcxproj") == 0)
					{
						GetUEPluginList(projectInfo.second->mProjectFile, pluginList, projectPathList);

						for (const FileInfo& fileInfo : projectInfo.second->mFiles)
						{
							if (fileInfo.mFilenameLower.Right(9) == L".build.cs")
							{
								if (projectInfo.second->mPlatformIncludeDirs.size() >
								    projectInfo.second->mConfigFullIncludeDirs.size())
								{
									// 4.25 or newer include structure. Includes appear in mPlatformIncludeDirs, but add
									// the includes to mConfigFullIncludeDirs anyway to avoid making more work for the
									// fix for case 142235.
									AddMissingPublicInclude(fileInfo, projectInfo.second->mConfigFullIncludeDirs);
									AddMissingGeneratedInclude(fileInfo, projectInfo.second->mConfigFullIncludeDirs);
								}
								else
								{
									// Pre-4.25 include structure. Includes appear in mConfigFullIncludeDirs.
									AddMissingGeneratedInclude(fileInfo, projectInfo.second->mConfigFullIncludeDirs);
								}
							}
						}
					}
				}
			}

			std::list<CStringW> existingPluginPaths;
			GetUEExistingPlugins(projectPathList, existingPluginPaths);

			std::map<CStringW, std::vector<CStringW>> pluginsReferencedByPlugin;
			ParseUEUpluginFiles(existingPluginPaths, pluginList, pluginsReferencedByPlugin);

			if (pluginList.size())
			{
				const CStringW solFilePath(::Path(sfc->GetSolutionFile()).MakeLower());
				PopulateUEReferencedPluginList(pluginList, pluginsReferencedByPlugin, existingPluginPaths, solFilePath);
			}
		}

		// is this a C# project?
		auto endsWithCsproj = [](const CStringW& str) -> bool {
			int len = 7; // Length of the substring ".csproj"
			CStringW suffix = str.Right(len);
			return suffix.CompareNoCase(L".csproj") == 0;
		};

		SemiColonDelimitedString extraIncludes;
		bool extraIncludesFetched = false;
		for (auto projectInfo : sfc->GetInfoMap())
		{
			VsSolutionInfo::ProjectSettingsPtr projectSettings = projectInfo.second;

			if (IsFile(projectSettings->mProjectFile))
			{
				if (gShellAttr->IsDevenv16OrHigher() && endsWithCsproj(projectSettings->mProjectFile)) // VS2019+ and C# [case: 149993]
				{
					if (!extraIncludesFetched)
					{
						extraIncludesFetched = true;
						extraIncludes = GetUnityEngineExtraIncludes();
					}

					CStringW projectName =
					    projectSettings->mProjectFile.Mid(projectSettings->mProjectFileDir.GetLength());
					WTString projectContents;
					projectContents.ReadFile(projectName);

					if (projectContents.FindNoCase("unityengine.dll") != -1)
					{
						vCatLog("Environment.Project", "Unity Engine: found project %s\n", (LPCTSTR)CString(projectSettings->mProjectFile));
						extraIncludes.Reset();
						CStringW include;

						while (extraIncludes.HasMoreItems())
						{
							extraIncludes.NextItem(include);
							projectSettings->mPlatformIncludeDirs.AddUnique(include);
						}

						SetContainsUnityEngineProject(true);
					}
				}
			}
		}

		if (GetContainsUnityEngineProject())
		{
			extraIncludes.Reset();

			if (!extraIncludes.HasMoreItems())
			{
				vLogUnfiltered("WARNING: unable to find extra Unity Engine includes");
			}
		}

		if (sPendingLoads)
		{
			mCleanLoad = false;
			throw WtException("pending project load (post get)");
		}

		if (sfc->GetSolutionHash() != mProjectHash)
		{
			mCleanLoad = false;

			if (mProjectHash && !mIsLoading && !gShellIsUnloading && gVaMainWnd)
			{
				vCatLog("Environment.Project", "Project::LoadProject schedule reload of solution\n");
				gVaMainWnd->KillTimer(IDT_SolutionReloadRetry);
				gVaMainWnd->SetTimer(IDT_SolutionReloadRetry, 2000, NULL);
			}

			throw WtException("solution hash mismatch (post get)");
		}
	}
	::OutputDebugStringA2("FEC: load 3");

	IncludesDb::Close(); // before VADatabase close because it records file info
	InheritanceDb::Close();
	g_pGlobDic->Flush();
	if (g_pMFCDic)
		g_pMFCDic->Flush();
	if (g_pCSDic)
		g_pCSDic->Flush();
	g_DBFiles.Close();
	if (gFileFinder)
		gFileFinder->SaveAndClear();

	// reinit namespaces
	MultiParse::ResetDefaultNamespaces();
	::ClearMpStatics();
	::ClearFdicStatics();
	_ASSERTE(_CrtCheckMemory());
	//
	// 	if (GetRegValue(HKEY_CURRENT_USER, ID_RK_APP_KEY, "TestFlag", 0))
	// 		::_heapmin();

#ifdef DEBUG_MEM_STATS
	{
		CMemoryState memEnd, memDiff;
		memEnd.Checkpoint();
		memDiff.Difference(sLastMemCheckpoint, memEnd);
		OutputDebugString("\nMemStats since last call to LoadProject (to confirm reasonable start of current load):\n");
		memDiff.DumpStatistics();
		// sMemStart.DumpAllObjectsSince();
		sLastMemCheckpoint.Checkpoint();

		::DumpMemStatistics(L"\nMemStats since VaConnectClient (emptied project):\n");
	}
#endif

	::OutputDebugStringA2("FEC: load 4");
	g_DBFiles.Init(VaDirs::GetDbDir(), GetProjDbDir());
	g_pGlobDic->SetDBDir(GetProjDbDir());

	g_WTL_FLAG = (GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "WTLhack") != "");

	// Parse project files
	FileList dirsToLookInForStdafxOverride;
	SetStatus(IDS_READING_PROJECT_INFO);
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	if (gVaRadStudioPlugin)
	{
		SetSolutionFile(gVaRadStudioPlugin->GetProjectGroupFile());
		const uint kProjCnt = gVaRadStudioPlugin->GetProjectCount();
		for (uint idx = 0; idx < kProjCnt; ++idx)
		{
			VaRadStudioPlugin::RsProjectDataPtr cur(gVaRadStudioPlugin->GetProjectData(idx));
			_ASSERTE(cur);
			if (cur)
			{
				TraceHelp("Load: " + WTString(CString(cur->mProject)));
				ProjectInfoPtr prjInf(RadStudioProject::CreateRadStudioProject(cur));
				if (prjInf)
				{
					CStringW prjFileLower(cur->mProject);
					prjFileLower.MakeLower();
					mMap[prjFileLower] = prjInf;
				}
			}
		}
	}
	else
#endif
	    if (gShellAttr->IsMsdev())
	{
		_ASSERTE(gShellAttr->SupportsDspFiles());
		TokenW t = mRawProjectList;
		while (t.more())
		{
			CStringW dsp = t.read(L";");
			if (dsp.IsEmpty() || !IsFile(dsp))
				continue;

			TraceHelp("Load: " + WTString(CString(dsp)));
			ProjectInfoPtr prjInf(DspProject::CreateDspProject(dsp));
			if (prjInf)
			{
				CStringW prjFileLower(dsp);
				prjFileLower.MakeLower();
				mMap[prjFileLower] = prjInf;
			}
		}
	}
	else
	{
#if defined(VA_CPPUNIT)
		extern FileList gFakeProjectFileItems;
		extern FileList gFakeProjectFileItems2;
		extern FileList gAdditionalIncDirs;
		bool hadAnyTestItems = false;
		if (gFakeProjectFileItems.size())
		{
			hadAnyTestItems = true;
			CStringW testProjectName(L"testproject.vcproj");
			ProjectInfoPtr prjInf(Vsproject::CreateVsproject(testProjectName, NULL));
			if (prjInf)
			{
				mMap[testProjectName] = prjInf;
				prjInf->TakeFiles(gFakeProjectFileItems);
				_ASSERTE(!gFakeProjectFileItems.size());

				if (gAdditionalIncDirs.size())
				{
					mMap[testProjectName] = prjInf;
					prjInf->TakeIncludeDirs(gAdditionalIncDirs);
					_ASSERTE(!gAdditionalIncDirs.size());
				}
			}
		}
		if (gFakeProjectFileItems2.size())
		{
			hadAnyTestItems = true;
			CStringW testProjectName(L"testproject2.vcproj");
			ProjectInfoPtr prjInf(Vsproject::CreateVsproject(testProjectName, NULL));
			if (prjInf)
			{
				mMap[testProjectName] = prjInf;
				prjInf->TakeFiles(gFakeProjectFileItems2);
				_ASSERTE(!gFakeProjectFileItems2.size());
			}
		}

		extern FileList gExternalProjectFileItems;
		if (gExternalProjectFileItems.size())
		{
			hadAnyTestItems = true;
			ProjectInfoPtr prjInf(Vsproject::CreateExternalFileProject());
			if (prjInf)
			{
				mMap[Vsproject::kExternalFilesProjectName] = prjInf;
				prjInf->TakeFiles(gExternalProjectFileItems);
				_ASSERTE(!gExternalProjectFileItems.size());
			}
		}

		extern FileList gFakeReferences;
		if (gFakeReferences.size())
		{
			hadAnyTestItems = true;
			RWLockWriter lck2(mFilelistLock);
			mReferencesList.swap(gFakeReferences);
			_ASSERTE(!gFakeReferences.size());
		}
#endif
		if (sfc)
		{
#if defined(VA_CPPUNIT)
			_ASSERTE(!hadAnyTestItems);
#endif
			AutoLockCs lck2(sfc->GetLock());
			VsSolutionInfo::ProjectSettingsMap& solInfoMap = sfc->GetInfoMap();
			// iterate over our map - take the files from mSolutionFileCollector
			for (VsSolutionInfo::ProjectSettingsMap::iterator solit = solInfoMap.begin(); solit != solInfoMap.end();
			     ++solit)
			{
				CStringW projName = (*solit).first;

				TraceHelp("Load: " + WTString(CString(projName)));
				ProjectInfoPtr prjInf(Vsproject::CreateVsproject(projName, (*solit).second));
				if (prjInf)
				{
					CStringW prjFileLower(projName);
					prjFileLower.MakeLower();
					mMap[prjFileLower] = prjInf;
				}
			}

			{
				RWLockWriter lck3(mFilelistLock);
				mReferencesList.swap(sfc->GetReferences());
				_ASSERTE(!sfc->GetReferences().size());
			}

			FileList incDirs;
			sfc->GetSolutionPackageIncludeDirs().swap(incDirs);
			SwapSolutionRepositoryIncludeDirs(incDirs);
			_ASSERTE(!incDirs.size());

			const CStringW solFile(sfc->GetSolutionFile());
			if (::IsFile(solFile))
				dirsToLookInForStdafxOverride.AddUniqueNoCase(::Path(solFile));
		}
		else
		{
#if !defined(VA_CPPUNIT)
			_ASSERTE(sfc);
#endif
		}

		IncludeDirs::ProjectLoaded();
	}

	::OutputDebugStringA2("FEC: load 5");
	SetStatus(IDS_GETPROJFILES_DONE);

	m_loadingProjectFiles = false;
	// force build of cache on this thread (ProjectLoader thread)
	GetProjAdditionalDirs();
	::OutputDebugStringA2("FEC: load 51");
	if (gFileFinder)
		gFileFinder->Load(GetProjDbDir());
	int lastDevLang = 0;
	::OutputDebugStringA2("FEC: load 6");

	// Now Parse the files
#ifdef _DEBUG
	uint projInfoSz = 0;
#endif
	std::vector<CStringW> projectFiles;
	{
		// [case: 25215] don't do any work while the lock is owned - just grab what
		// we need - might need to build mfc db
		RWLockReader lck2(mMapLock);
		for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

#ifdef _DEBUG
			projInfoSz += prjInf->GetMemSize();
#endif
			const CStringW& prjFile = prjInf->GetProjectFile();
			if (prjFile != Vsproject::kExternalFilesProjectName)
				projectFiles.push_back(prjFile);
			if (prjInf->CppUsesClr())
				mCppUsesClr = true;
			if (prjInf->CppUsesWinRT())
				mCppUsesWinRT = true;
		}
	}

	bool hasOnlyCpp = true;
	std::vector<int> slnLangs;
	for (std::vector<CStringW>::iterator iter = projectFiles.begin(); iter != projectFiles.end(); ++iter)
	{
		const CStringW curFile = *iter;
		const CStringW projDir(::Path(curFile));
		dirsToLookInForStdafxOverride.AddUniqueNoCase(projDir);
		// [case: 115362]
		dirsToLookInForStdafxOverride.AddUniqueNoCase(projDir + L"\\.va\\shared");
		dirsToLookInForStdafxOverride.AddUniqueNoCase(projDir + L"\\.va\\user");

		const CStringW ext(GetBaseNameExt(curFile));
#if defined(RAD_STUDIO)
		if (!ext.CompareNoCase(L"cbproj")) // CppBuilder
		{
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
		}
#else
		if (!ext.CompareNoCase(L"dsp"))
		{
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
		}
		else if (!ext.CompareNoCase(L"dproj")         // Delphi project cannot contain cpp files.
		         || !ext.CompareNoCase(L"groupproj")) // CppBuilder solution file
		{
		}
		else if (!ext.CompareNoCase(L"vcproj") || !ext.CompareNoCase(L"icproj") || !ext.CompareNoCase(L"vcxproj") ||
		         !ext.CompareNoCase(L"vcxitems") || !ext.CompareNoCase(L"ucproj") ||
		         !ext.CompareNoCase(L"cbproj") ||      // CppBuilder
		         !ext.CompareNoCase(L"androidproj") || // [case: 87696] vs2015 android
		         !ext.CompareNoCase(L"iosproj") ||     // [case: 87696] vs2015 ios
		         !ext.CompareNoCase(L"pbxproj") ||     // vs2015 ios
		         !ext.CompareNoCase(L"pyproj") ||      // vs2015 python
		         !ext.CompareNoCase(L"tsproj") ||      // [case: 84121] Beckhoff Automation
		         !ext.CompareNoCase(L"plcproj") ||     // [case: 84121] Beckhoff Automation
		         !ext.CompareNoCase(L"avrgccproj") ||  // AVR Studio 5.0/5.1
		         !ext.CompareNoCase(L"cproj") ||       // Atmel Studio 6.0
		         !ext.CompareNoCase(L"cppproj"))       // Atmel Studio 6.0
		{
			// managed C++ projects should have references for .NET
			// assemblies, which will be loaded below
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
		}
		else if (!ext.CompareNoCase(L"vbproj") || !ext.CompareNoCase(L"vbxproj"))
		{
			if (lastDevLang != VB)
			{
				slnLangs.push_back(VB);
				lastDevLang = VB;
				hasOnlyCpp = false;
			}
		}
		else if (!ext.CompareNoCase(L"csproj") || !ext.CompareNoCase(L"csxproj") || !ext.CompareNoCase(L"shproj"))
		{
			if (lastDevLang != CS)
			{
				slnLangs.push_back(CS);
				lastDevLang = CS;
				hasOnlyCpp = false;
			}
		}
		else if (!ext.CompareNoCase(L"jsproj"))
		{
			// JS Metro app
			if (lastDevLang != JS)
			{
				slnLangs.push_back(JS);
				lastDevLang = JS;
				hasOnlyCpp = false;
			}
		}
		else if (!ext.CompareNoCase(L"etp"))
		{
			// do nothing; etp is solution
		}
		else if (!ext.CompareNoCase(L"json") && !::Basename(curFile).CompareNoCase(L"cppproperties.json"))
		{
			// [case: 102080]
			// C++ non-msbuild project
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
		}
		else if (!ext.CompareNoCase(L"txt") && !::Basename(curFile).CompareNoCase(L"cmakelists.txt"))
		{
			// [case: 102486]
			// C++ non-msbuild project
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
		}
#endif
		else if (::IsFile(curFile))
		{
#if defined(VA_CPPUNIT)
			if (lastDevLang != Src)
			{
				slnLangs.push_back(Src);
				lastDevLang = Src;
			}
#else
			_ASSERTE(!"unknown project ext");
			hasOnlyCpp = false;
#endif
		}
	}
	::OutputDebugStringA2("FEC: load 7");

	// [case: 115362] use va directory for solution settings/config
	CStringW slnDir;

	if (IsDir(mSolutionFile))
		slnDir = mSolutionFile;
	else
		slnDir = ::Path(mSolutionFile);

	dirsToLookInForStdafxOverride.AddUniqueNoCase(slnDir + L"\\.va\\shared");
	dirsToLookInForStdafxOverride.AddUniqueNoCase(slnDir + L"\\.va\\user");

	// set mRequiresNetDb before calling GetSysDic
	if (hasOnlyCpp && !mReferencesList.size() && !CppUsesClr() && !CppUsesWinRT() && projectFiles.size())
		mRequiresNetDb = false;

	for (auto l : slnLangs)
	{
		GetSysDic(l);
		lastDevLang = l;
	}

	if (!::IsAnySysDicLoaded())
	{
		// [case: 10401] Loading a 2008 web project
		// [case: 17840] hang due to new db mem mgmt
		GetSysDic(CS); //  something must be created to prevent parsethread from hanging
		lastDevLang = CS;
		mRequiresNetDb = true;
	}

	if (mReferencesList.size() || CppUsesClr() || CppUsesWinRT())
	{
		// [case: 58822]
		// load of C++/clr solution (without any .net projects) can cause hang if
		// net db not init'd on loader thread
		GetSysDic(CS);
	}

	FileList overrides;
	{
		// [case: 111779]
		const CStringW stdafx(VaDirs::GetParserSeedFilepath(L"va_stdafx.h"));
		if (::IsFile(stdafx))
			overrides.AddUniqueNoCase(stdafx);
	}

	for (FileList::const_iterator it = dirsToLookInForStdafxOverride.begin();
	     it != dirsToLookInForStdafxOverride.end() && !StopIt; ++it)
	{
		const CStringW userLocalStdafx((*it).mFilename + L"\\va_stdafx.h");
		if (!::IsFile(userLocalStdafx))
			continue;

		overrides.AddUniqueNoCase(userLocalStdafx);
	}
	::OutputDebugStringA2("FEC: load 8");

#if defined(VA_CPPUNIT)
	extern FileList gFakeStdafxOverrideFileItems;
	overrides.swap(gFakeStdafxOverrideFileItems);
#endif // VA_CPPUNIT

	if (!g_CodeGraphWithOutVA)
	{
		if (mSolutionPrivateSystemIncludeDirs.size())
		{
			GetSysDic(Src);
			lastDevLang = Src;
			int privPreload = LoadIdxesInDir(mProjectDbDir + kDbIdDir[DbTypeId_SolutionSystemCpp] + L"\\", lastDevLang,
			                                 VA_DB_SolutionPrivateSystem | VA_DB_Cpp);
			MultiParsePtr mp = MultiParse::Create();

			// [case: 113791]
			// load va_stdafx overrides for private system includes
			for (FileList::const_iterator it = overrides.begin(); it != overrides.end() && !StopIt; ++it)
			{
				const CStringW userLocalStdafx((*it).mFilename);
				_ASSERTE(::IsFile(userLocalStdafx));

				if (privPreload)
				{
					vCatLog("Environment.Project", "Pr::LP va_stdafx privSys skip due to privPreLoad: %s\n", (LPCTSTR)CString(userLocalStdafx));
				}
				else
				{
					vCatLog("Environment.Project", "Pr::LP load va_stdafx privSys: %s\n", (LPCTSTR)CString(userLocalStdafx));
					mp->FormatFile(userLocalStdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals, false, nullptr,
					               VA_DB_SolutionPrivateSystem | VA_DB_Cpp);
				}
			}

			ParsePrivateSystemHeaders();
		}
	}

	sOkToLoadProjFiles = true;

	Sleep(50);
	if (gShellIsUnloading)
		throw UnloadingException();

	_ASSERTE(::IsAnySysDicLoaded());
	BuildFileCollectionIfReady();
	// #ifdef _DEBUG
	//	uint sz = mProjectFileItemList.GetMemSize();
	// #endif
	//  set after list initially built
	mSolutionIsInitiallyEmpty = !GetFileItemCount();

	CatLog("Environment.Project", "Project::LoadProject");
	DumpToLog();
	SetStatus("Loading project symbols...");
	::OutputDebugStringA2("FEC: load 9");

	int preLoaded = 0;
	ModifiedFilesInfo modifiedFilesInfo;

	if (!StopIt && mProjectHash)
	{
		TimeTrace tt("Load solution symbols");
		preLoaded = LoadIdxesInDir(mProjectDbDir, lastDevLang, VA_DB_Solution);
		g_pGlobDic->LoadComplete(); // complete as far as indexes in dir are concerned

		if (preLoaded && gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 142124]
			CollectModificationInfo(modifiedFilesInfo);
			const int modCnt = modifiedFilesInfo.GetModifiedProjectFileCount();
			const size_t totalFileCnt = mProjectFileItemList.size() + mSolutionPrivateSystemHeaders.size();
			vCatLog("Environment.Project", "Pr::LP sln db preload total(%zu) mod(%d) min(%d)\n", totalFileCnt, modCnt,
			     Psettings ? (int)Psettings->mRebuildSolutionMinimumModPercent : -1);
			if (Psettings && Psettings->mRebuildSolutionMinimumModPercent && modCnt >= 50)
			{
#if 0 // [case: 142406]
				const int percentMod = ::MulDiv(modCnt, 100, totalFileCnt);
				if (percentMod >= int(Psettings->mRebuildSolutionMinimumModPercent))
				{
					// clear and release db, then restart the load
					vCatLog("Environment.Project", "Pr::LP rebuild sln db due to external modifications\n");
					g_DBFiles.SolutionLoadCompleted();

					IncludesDb::Close();
					InheritanceDb::Close();
					g_pGlobDic->Flush();
					if (g_pMFCDic)
						g_pMFCDic->Flush();
					if (g_pCSDic)
						g_pCSDic->Flush();
					g_DBFiles.Close();
					if (gFileFinder)
						gFileFinder->SaveAndClear();

					::MyRmDir(mProjectDbDir, true);
					::Sleep(500);

					if (!gShellIsUnloading && gMainWnd)
					{
						gMainWnd->KillTimer(IDT_SolutionReloadRetry);
						gMainWnd->SetTimer(IDT_SolutionReloadRetry, 1000, nullptr);
					}

					mCleanLoad = false;
					throw WtException("request solution rebuild due to > 50% modified files");
				}
#endif
			}
		}

		// [case: 67079] check for project / solution defined va_stdafx.h overrides
		// does not work if preloaded (due to ordering?)
		// updates to va_stdafx.h require delete of proj_* data dirs
		MultiParsePtr mp = MultiParse::Create();

		for (FileList::const_iterator it = overrides.begin(); it != overrides.end() && !StopIt; ++it)
		{
			const CStringW userLocalStdafx((*it).mFilename);
			_ASSERTE(::IsFile(userLocalStdafx));

			if (preLoaded)
			{
				vCatLog("Environment.Project", "Pr::LP va_stdafx skip due to preLoad: %s\n", (LPCTSTR)CString(userLocalStdafx));
			}
			else
			{
				vCatLog("Environment.Project", "Pr::LP load va_stdafx: %s\n", (LPCTSTR)CString(userLocalStdafx));
				mp->FormatFile(userLocalStdafx, V_INPROJECT | V_VA_STDAFX, ParseType_Globals);
			}
		}
	}
	else
		g_pGlobDic->LoadComplete(); // complete as far as indexes in dir are concerned
	::OutputDebugStringA2("FEC: load 10");

	if (!Psettings->m_FastProjectOpen && !preLoaded && !StopIt)
	{
		FileList projFileItemsSrc[2], projFileItemsGlobals[2];

		{
			RWLockReader lck2(mFilelistLock);
			for (const auto& fi : mProjectFileItemList)
			{
				const int ftype = GetFileType(fi.mFilename);
				if (ftype == Src)
					projFileItemsSrc[1].Add(fi);
				else if (ShouldParseGlobals(ftype))
					projFileItemsGlobals[1].Add(fi);
			}
		}

		// split projFileItemsGlobals and projFileItemsSrc into two each so that ReleaseDbBackedStrings can be called
		// 1/2 between each set
		auto ListSplitter = [](FileList* arr) {
			const size_t sz = arr[1].size();
			if (sz < 1000)
				return;

			const size_t splitSz = sz / 2;
			FileList::iterator it = arr[1].begin();
			for (size_t idx = 0; idx < splitSz && it != arr[1].end(); ++it, ++idx)
				;

			while (it != arr[1].end())
			{
				arr[0].Add(*it);
				arr[1].erase(it++);
			}
		};

		ListSplitter(projFileItemsGlobals);
		ListSplitter(projFileItemsSrc);

		// headers first
		{
			TimeTrace tt("Parse declarations");
			for (const auto& files : projFileItemsGlobals)
			{
				if (files.empty())
					continue;

				auto prsr = [](const FileInfo& fi) {
					if (StopIt)
						return;

					const CStringW f(fi.mFilename);
					if (!MultiParse::IsIncluded(f))
					{
						MultiParsePtr mpl = MultiParse::Create();
						mpl->FormatFile(f, V_INPROJECT, ParseType_Globals);
					}
				};

				if (Psettings->mUsePpl)
					Concurrency::parallel_for_each(files.begin(), files.end(), prsr);
				else
					std::for_each(files.begin(), files.end(), prsr);
			}
		}

		// source files next
		{
			TimeTrace tt("Parse implementations");
			for (const auto& files : projFileItemsSrc)
			{
				if (files.empty())
					continue;

				auto prsr = [](const FileInfo& fi) {
					if (StopIt)
						return;

					const CStringW f(fi.mFilename);
					// scan source files for gotodef
					if (!MultiParse::IsIncluded(f))
					{
						MultiParsePtr mpl = MultiParse::Create();
						mpl->FormatFile(f, V_INPROJECT, ParseType_GotoDefs);
					}
				};

				if (Psettings->mUsePpl)
					Concurrency::parallel_for_each(files.begin(), files.end(), prsr);
				else
					std::for_each(files.begin(), files.end(), prsr);
			}
		}
	}

#if !defined(VAX_CODEGRAPH)
	// Spaghetti doesn't parse referenced assemblies yet.
	if (!StopIt)
	{
		// Load References
		TimeTrace tr2("Parse references");
		if (mReferencesList.size())
		{
			FileList references;

			{
				RWLockReader lck2(mFilelistLock);
				references.AddHead(mReferencesList);
			}

			auto imprt = [](const FileInfo& fi) {
				if (!StopIt)
				{
					LogElapsedTime tt(CString(fi.mFilename), 700);
					NetImportDll(fi.mFilename);
				}
			};

			if (Psettings->mUsePpl)
				Concurrency::parallel_for_each(references.begin(), references.end(), imprt);
			else
				std::for_each(references.begin(), references.end(), imprt);
		}
		CheckImplicitReferences(false);
	}

	if (!StopIt)
	{
		TimeTrace tt("Parse intermediates");
		ProcessIntermediateDirs();
	}
#endif // !VAX_CODEGRAPH
	::OutputDebugStringA2("FEC: load 11");

	// get std includes after the project is loaded
	if (g_pMFCDic && g_pMFCDic->m_loaded && !StopIt)
	{
		MultiParsePtr mp = MultiParse::Create();
		GetSysDic(Src);
		if (mMap.begin() != mMap.end())
			mp->SetFilename((*mMap.begin()).first);
		else
			_ASSERTE(!"no project loaded");
		mp->ProcessIncludeLn(WTString("#include \"stdafx.h\""), 0);
		mp->ProcessIncludeLn(WTString("#include \"stdatl.h\""), 0);
		TimeTrace tt("Sorting (1)");
		g_pMFCDic->SortIndexes();
	}

	{
		TimeTrace tt("Sorting (2)");
		if (g_pCSDic && g_pCSDic->m_loaded)
			g_pCSDic->SortIndexes();

		g_pGlobDic->ReleaseTransientData();
		g_pGlobDic->SortIndexes();
	}

	if (!Psettings->m_FastProjectOpen && preLoaded && !StopIt)
	{
		// Does this really make sense?  Fast project open means stale db for modified files?
		TimeTrace tt("Check for changes");
		UpdateModifiedFiles(modifiedFilesInfo);
	}

	if (StopIt)
		mCleanLoad = false;

	if (gFileIdManager)
		gFileIdManager->QueueSave();

	g_DBFiles.SolutionLoadCompleted();
	mIsLoading = false;
	m_loadingProjectFiles = false;

	FileFinder_ProjectLoadingFinished();

	_ASSERTE(_CrtCheckMemory());
	SetStatus(IDS_READY);

	if (gHashtagManager)
		gHashtagManager->SolutionLoaded();
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new RefreshHashtags(nullptr));
	IncludesDb::SolutionLoaded();
	InheritanceDb::SolutionLoaded();
	::DumpMemStatistics(L"\nVaMemStats: LoadProject exit\n");

	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
	{
		static bool once = true;
		if (once && !sPendingLoads && !gShellIsUnloading)
		{
			CStringW solutionFile(SolutionFile());
			if (!solutionFile.IsEmpty())
			{
				once = false;
				solutionFile = ::Basename(solutionFile);
				solutionFile.MakeLower();
				CStringW cmdLine(::GetCommandLineW());
				cmdLine.MakeLower();
				if (-1 != cmdLine.Find(solutionFile))
				{
					RWLockWriter lck2(mMapLock);
					if (mMap.size() > 1)
					{
						const size_t fileCnt = mProjectFileItemList.size();
						ProjectInfoPtr inf = mMap[Vsproject::kExternalFilesProjectName];
						if (!inf)
							mMap.erase(Vsproject::kExternalFilesProjectName);
						const size_t externCnt = inf ? inf->GetFiles().size() : 0u;
						if (!fileCnt || fileCnt == externCnt)
						{
							// [case: 73814]
							// schedule a reload on ui thread
							vCatLog("Environment.Project", "Project::LoadProject schedule reload of commandLine solution f(%zu) ext(%zu)\n",
							     fileCnt, externCnt);
							if (gVaMainWnd)
							{
								gVaMainWnd->KillTimer(IDT_SolutionReload);
								gVaMainWnd->SetTimer(IDT_SolutionReload, 20000, NULL);
							}
						}
					}
				}
			}
		}
	}

	::OutputDebugStringA2("FEC: load 13 end");
	new FunctionThread(SimpleFileScanThread, "SimpleFileScan", true);
}

void Project::FindUEPlugins(const CStringW& path, std::list<CStringW>& existingPluginPaths)
{
	if (Psettings->mIndexPlugins != 1)
		return;

	if (!DirectoryExists(path)) // guard against defined but not existing path
		return;

	bool foundPlugin = false;
	std::vector<CStringW> subDirList;

	for (const auto& entry : std::filesystem::directory_iterator(path.GetString()))
	{
		if (std::filesystem::is_regular_file(entry))
		{
			if (entry.path().extension() == L".uplugin")
			{
				foundPlugin = true;
				CStringW tempStr(entry.path().wstring().c_str());
				tempStr.MakeLower();
				existingPluginPaths.push_back(tempStr);
				break;
			}
		}
		else if (std::filesystem::is_directory(entry))
		{
			subDirList.emplace_back(entry.path().wstring().c_str());
		}
	}

	if (!foundPlugin)
	{
		for (const auto& subDir : subDirList)
		{
			FindUEPlugins(subDir, existingPluginPaths);
		}
	}
}

void Project::PopulateUEReferencedPluginList(std::list<CStringW>& pluginList,
                                             const std::map<CStringW, std::vector<CStringW>>& pluginsReferencedByPlugin,
                                             const std::list<CStringW>& existingPluginPaths,
                                             const CStringW& solFilePath)
{
	if (Psettings->mIndexPlugins != 1)
		return;

	for (;;)
	{
		// finding all additional plugins referenced in used plugins
		std::list<CStringW> additionalPluginList;
		for (const auto& plugin : pluginList)
		{
			auto iter1 = pluginsReferencedByPlugin.find(plugin);
			if (pluginsReferencedByPlugin.end() != iter1)
			{
				// project references plugin that has sub-references other plugins; find if those are already referenced
				// as plugins
				for (const auto& pluginRefByPlugin : iter1->second)
				{
					auto iter2 = std::find_if(pluginList.begin(), pluginList.end(),
					                          [&](const CStringW& v) { return v == pluginRefByPlugin; });

					if (pluginList.end() == iter2)
					{
						// not found so we need to include it in list of plugins for search
						additionalPluginList.push_back(pluginRefByPlugin);
					}
				}
			}
		}

		if (additionalPluginList.size() == 0)
		{
			// nothing found, stop search
			break;
		}
		else
		{
			// additional plugins found; add it in plugin list and try again
			additionalPluginList.sort();
			additionalPluginList.unique();

			pluginList.merge(additionalPluginList);
			pluginList.sort();
			pluginList.unique();
		}
	}

	for (const auto& plugin : pluginList)
	{
		std::vector<CStringW> dupPlugin;
		for (const auto& pluginPath : existingPluginPaths)
		{
			CStringW tmpUPlugin = L"\\" + plugin + L".uplugin";
			tmpUPlugin.MakeLower();
			if (pluginPath.Find(tmpUPlugin) >= 0)
				dupPlugin.push_back(pluginPath);
		}

		if (dupPlugin.size() == 1)
		{
			mReferencedPlugins.push_back(::Path(dupPlugin[0]));
		}
		else if (dupPlugin.size() > 1)
		{
			bool projectPluginFound = false;
			for (const auto& elem : dupPlugin)
			{
				if (elem.Find(L"\\engine\\plugins") < 0)
				{
					// plugin outside engine plugins folder found; add it to parse list and ignore other
					projectPluginFound = true;
					mReferencedPlugins.push_back(::Path(elem));
					break;
				}
			}

			if (!projectPluginFound)
			{
				// somehow we have more than one engine\plugins location so choose one; case when engine folder is
				// installed on the system but also exists in project
				std::deque<size_t> pluginLocationTopList;
				bool foundEnginePluginsInProject = false;

				for (size_t i = 0; i < dupPlugin.size(); i++)
				{
					if (dupPlugin[i].Find(solFilePath) >= 0)
					{
						// plugin is somewhere in solution's engine\plugins so put it's index in front of list and don't
						// search anymore since this one will be used
						pluginLocationTopList.push_front(i);
						foundEnginePluginsInProject = true;
						break;
					}
					else
					{
						// plugin is in installed engine location; add its index on the end
						pluginLocationTopList.push_back(i);
					}
				}

				if (foundEnginePluginsInProject)
				{
					// first element of top list is index of plugin we need
					mReferencedPlugins.push_back(::Path(dupPlugin[pluginLocationTopList.front()]));
				}
				else
				{
					// only intalled engine plugins found; add all if there is more
					for (std::deque<size_t>::iterator it = pluginLocationTopList.begin();
					     it != pluginLocationTopList.end(); ++it)
						mReferencedPlugins.push_back(::Path(dupPlugin[*it]));
				}
			}
		}
	}
}

void Project::ParseUEUpluginFiles(const std::list<CStringW>& existingPluginPaths, std::list<CStringW>& pluginList,
                                  std::map<CStringW, std::vector<CStringW>>& pluginsReferencedByPlugin)
{
	if (Psettings->mIndexPlugins != 1)
		return;

	for (const auto& pluginPath : existingPluginPaths)
	{
		WTString upluginFileText;
		upluginFileText.ReadFile(pluginPath);

		namespace rapidJson = RAPIDJSON_NAMESPACE;
		rapidJson::Document jsDoc;
		if (!jsDoc.Parse(upluginFileText.c_str()).HasParseError())
		{
			CStringW pluginName;
			int from = pluginPath.ReverseFind(L'\\') + 1;
			int length = pluginPath.ReverseFind(L'.') - from;
			if (from >= 0 && length > 0)
				pluginName = pluginPath.Mid(from, length);
			else
				continue; // should not happen ever but adding protection just in case

			// check if it is EnabledByDefault and all all also of those in the list
			if (jsDoc.HasMember("EnabledByDefault"))
			{
				rapidJson::Value::ConstMemberIterator itrEnabledByDefault = jsDoc.FindMember("EnabledByDefault");
				if (itrEnabledByDefault != jsDoc.MemberEnd())
				{
					if (itrEnabledByDefault->value.GetType() == rapidJson::Type::kTrueType)
					{
						pluginList.push_back(pluginName);
					}
				}
			}

			// since we already have uplugin file parsed here, check if plugin have referenced other plugins and build
			// dependency to avoid additional parse later
			if (jsDoc.HasMember("Plugins"))
			{
				rapidJson::Value& plugins = jsDoc["Plugins"];
				if (plugins.IsArray())
				{
					CStringW pluginDirPath = ::Path(pluginPath);
					for (rapidJson::Value::ConstValueIterator pluginItr = plugins.Begin(); pluginItr != plugins.End();
					     ++pluginItr)
					{
						const rapidJson::Value& plugin = *pluginItr;
						if (plugin.HasMember("Name") && plugin["Name"].IsString())
						{
							CStringW referencedName(plugin["Name"].GetString());
							pluginsReferencedByPlugin[pluginName].push_back(referencedName.MakeLower());
						}
					}
				}
			}
		}
	}

	pluginList.sort();
	pluginList.unique();
}

void Project::GetUEExistingPlugins(const std::vector<CStringW>& projectPathList,
                                   std::list<CStringW>& existingPluginPaths)
{
	if (Psettings->mIndexPlugins != 1)
		return;

	// get all existing plugins from project plugins location(s)
	for (const auto& projectPath : projectPathList)
	{
		CStringW projectPluginsPath = projectPath;
		projectPluginsPath.Append(LR"(\plugins\)");
		projectPluginsPath.Replace(LR"(\\)", LR"(\)");
		FindUEPlugins(projectPluginsPath, existingPluginPaths);
	}

	// get all existing plugins from ignorePlugins folder locations
	for (const auto& ignorePluginPathElement : mUEIgnorePluginsDirs)
	{
		for (const auto& ignorePath : ignorePluginPathElement.second)
		{
			FindUEPlugins(ignorePath, existingPluginPaths);
		}
	}

	// guard if two different engines are set to same location (normally should not happen but we guard it anyway)
	existingPluginPaths.sort();
	existingPluginPaths.unique();
}

void Project::GetUEPluginList(const CStringW& projectFile, std::list<CStringW>& pluginList,
                              std::vector<CStringW>& projectPathList)
{
	if (Psettings->mIndexPlugins != 1)
		return;

	LogElapsedTime et("Proj::GetUEPlugin", 2000);

	WTString projFileText;
	if (!projFileText.ReadFile(projectFile))
		return;

	// [case: 146246]
	// do straight string search before doing slow regex
	if (-1 == projFileText.FindNoCase(".uproject"))
		return;

	WTString uprojRgx = R"((Include\s*=\s*?")(.*\n?)(.uproject"))";
	int sectionLocation = projFileText.FindRENoCase(uprojRgx.c_str());
	if (sectionLocation > -1)
	{
		int pos1 = projFileText.find("\"", sectionLocation) + 1;
		int pos2 = projFileText.find("\"", pos1);
		WTString uprojPath = projFileText.substr(pos1, pos2 - pos1);

		WCHAR fullUprojPath[MAX_PATH + 1];
		LPWSTR fname;
		CStringW npth;

		if (uprojPath.Wide()[0] == L'.')
		{
			// it is relative path, prepend current dir and get full path
			int posName = projectFile.ReverseFind('\\');
			if (posName != -1)
				uprojPath = projectFile.Left(posName + 1) + uprojPath;
		}

		if (!GetFullPathNameW(uprojPath.Wide(), MAX_PATH, fullUprojPath, &fname))
			return;

		// check if uproject is in engine directory
		CStringW fullUprojPathString(fullUprojPath);
		fullUprojPathString.MakeLower();
		for (const auto& ignoreDir : mUEIgnorePluginsDirs)
		{
			if (fullUprojPathString.Find(ignoreDir.first + L"engine", 0) >= 0)
				return;
		}

		// store uproject path to list so that we have information where it is located
		projectPathList.push_back(::Path(fullUprojPathString));

		// parse only uprojects that are not in Engine folder
		WTString uprojFileText;
		uprojFileText.ReadFile(fullUprojPath);

		namespace rapidJson = RAPIDJSON_NAMESPACE;
		rapidJson::Document jsDoc;
		if (!jsDoc.Parse(uprojFileText.c_str()).HasParseError())
		{
			if (jsDoc.HasMember("Plugins"))
			{
				rapidJson::Value& plugins = jsDoc["Plugins"];
				if (plugins.IsArray())
				{
					for (rapidJson::Value::ConstValueIterator pluginItr = plugins.Begin(); pluginItr != plugins.End();
					     ++pluginItr)
					{
						const rapidJson::Value& plugin = *pluginItr;
						if (!plugin.HasMember("Name"))
							continue;

						if (plugin["Name"].IsString())
						{
							CStringW name(plugin["Name"].GetString());
							pluginList.push_back(name.MakeLower());
						}
					}
				}
			}
		}
	}
}

void Project::CheckImplicitReferences(bool asyncLoad)
{
	// VS2003 C# implicitly references mscorlib.dll, whereas VS2005+
	// explicitly references it.
	//
	// In VS2005+ C++/CLR projects, it is implicit rather than explicit.
	//
	// If the sln doesn't explicitly reference mscorlib.dll, then
	// look in the same directory as System.dll
	//
	// Note: if by (slim) chance the sln intentionally did not
	// include a reference to System.dll, but uses the implicit
	// reference to mscorlib, we won't parse mscorlib.

	bool mscorlibExplicitReference = false;
	CStringW mscorlibPath;

	if (mReferencesList.size())
	{
		RWLockReader lck(mFilelistLock);
		for (FileList::const_iterator it = mReferencesList.begin(); it != mReferencesList.end() && !StopIt; ++it)
		{
			const CStringW f((*it).mFilename);

			if (f.Find(L"mscorlib.dll") >= 0)
			{
				mscorlibExplicitReference = true;
			}
			else
			{
				int pos = f.Find(L"System.dll");
				if (pos >= 0)
				{
					mscorlibPath = f.Left(pos) + L"mscorlib.dll";
				}
			}
		}
	}

#if !defined(VA_CPPUNIT)
	if (!mscorlibExplicitReference)
	{
		if (mscorlibPath.GetLength() == 0 && CppUsesClr() && !StopIt)
		{
			CStringW dotNetInstallRoot =
			    GetRegValueW(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\.NETFramework", "InstallRoot");
			if (dotNetInstallRoot.GetLength() > 0)
			{
				CStringW findStr = dotNetInstallRoot;
				if (gShellAttr->IsDevenv10OrHigher())
					findStr += L"v4.0*";
				else if (gShellAttr->IsDevenv8OrHigher())
					findStr += L"v2.0*";
				else
					findStr += L"v1.1*";

				WIN32_FIND_DATAW findFileData;
				HANDLE hFind = FindFirstFileW(findStr, &findFileData);
				if (hFind != INVALID_HANDLE_VALUE)
				{
					mscorlibPath = dotNetInstallRoot;
					mscorlibPath += findFileData.cFileName;
					mscorlibPath += L"\\mscorlib.dll";

					FindClose(hFind);
				}
			}
			else
			{
				vLogUnfiltered("WARN: dotNetInstallRoot empty");
			}
		}

		if (mscorlibPath.GetLength() > 0 && !StopIt)
		{
			TraceHelp("Implicit Reference: " + WTString(CString(mscorlibPath)));
			if (::IsFile(mscorlibPath))
			{
				if (asyncLoad)
				{
					if (!Psettings->m_FastProjectOpen && g_ParserThread)
						g_ParserThread->QueueParseWorkItem(new ReferenceImporter(mscorlibPath));
				}
				else
				{
					NetImportDll(mscorlibPath);
				}

				RWLockWriter lck(mFilelistLock);
				mReferencesList.AddUniqueNoCase(mscorlibPath);
			}
			else
			{
				vLog("ERROR: failed to locate mscorlib at %s", (LPCTSTR)CString(mscorlibPath));
			}
		}
	}
#endif // !VA_CPPUNIT
}

void Project::ProcessIntermediateDirs()
{
	FileList lst;
	{
		RWLockReader lck(mMapLock);
		for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end() && !StopIt; ++iter)
		{
			ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			const FileList& iDirs = prjInf->GetIntermediateDirs();
			for (FileList::const_iterator it = iDirs.begin(); it != iDirs.end(); ++it)
				lst.AddUnique(*it);
		}
	}

	CStringW intermediateDirs;
	for (FileList::const_iterator it = lst.begin(); it != lst.end() && !StopIt; ++it)
	{
		const CStringW cur((*it).mFilename);
		intermediateDirs += cur + L";";
	}
	lst.clear();
	if (StopIt)
		return;

	// Look in intermediate dirs for *.tlh
	DEFTIMERNOTE(LoadProjectTLH, CString(intermediateDirs));
	// look for .tlh's to include in sysdb
	std::vector<CStringW> hdrs;

	{
		TokenW dirs = intermediateDirs;
		while (dirs.more() > 2 && !StopIt)
		{
			CStringW dir = dirs.read(L";");
			FileList files;
			FindFiles(dir, L"*.tlh", files);
			for (FileList::const_iterator it = files.begin(); it != files.end() && !StopIt; ++it)
			{
				CStringW tlh = (*it).mFilename;
				hdrs.push_back(tlh);
			}
		}
	}

	uint dbFlag = 0;
	auto parseFunc = [&dbFlag](const CStringW& hdrFile) {
		if (!StopIt && IsFile(hdrFile))
		{
			MultiParsePtr mp = MultiParse::Create(Header);
			if (!mp->IsIncluded(hdrFile))
				mp->FormatFile(hdrFile, V_SYSLIB, ParseType_Globals, false, nullptr, dbFlag);
		}
	};

	if (hdrs.size())
	{
		dbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(hdrs.cbegin(), hdrs.cend(), parseFunc);
		else
			std::for_each(hdrs.cbegin(), hdrs.cend(), parseFunc);
	}

	if (gShellAttr->IsDevenv() && g_pMFCDic && g_pMFCDic->m_loaded && !StopIt)
	{
		// in VSNet, also look in TEMP dir, because vs.net stores the tlh's there for certain #import(s).
		const CStringW tempDir(::GetTempDir());
		if (!tempDir.IsEmpty())
		{
			hdrs.clear();
			FileList files;
			FindFiles(tempDir, L"*.tlh", files);
			for (FileList::const_iterator it = files.begin(); it != files.end() && !StopIt; ++it)
			{
				CStringW tlh = (*it).mFilename;
				hdrs.push_back(tlh);
			}

			// no sense in repeating for every solution, just store in global shared db #parseTempTlhAsGlobal
			dbFlag = VA_DB_Cpp;
			if (Psettings->mUsePpl)
				Concurrency::parallel_for_each(hdrs.cbegin(), hdrs.cend(), parseFunc);
			else
				std::for_each(hdrs.cbegin(), hdrs.cend(), parseFunc);
		}
	}
}

static bool LessThanByBasename(const FileInfo& lhs, const FileInfo& rhs)
{
	LPCWSTR f1, n1;
	LPCWSTR f2, n2;

	f1 = n1 = lhs.mFilenameLower;
	f2 = n2 = rhs.mFilenameLower;

	for (; *n1; n1++)
		if (*n1 == L'/' || *n1 == L'\\')
			f1 = &n1[1];

	for (; *n2; n2++)
		if (*n2 == L'/' || *n2 == L'\\')
			f2 = &n2[1];

	int i = 0;
	for (; f1[i] == f2[i]; i++)
	{
		if (f1[i] == DB_FIELD_DELIMITER_W || !f1[i])
		{
			// basenames are equal, so compare whole path
			return lhs.mFilenameLower.Compare(rhs.mFilenameLower) < 0;
		}
	}

	return f1[i] < f2[i];
}

bool Project::SortFileListIfReady()
{
	if (!BuildFileCollectionIfReady())
		return false;

	{
		RWLockReader lck(mFilelistLock);
		if (!mFileListNeedsSort)
			return true;
	}

	// create index for FileListCombo
	// this list is sorted by filename (excluding path) solely as an
	// optimization for the benefit of the FIS list in va view
	RWLockWriter lck(mFilelistLock);
	mProjectFileItemList.sort(::LessThanByBasename);
	mFileListNeedsSort = false;

	return true;
}

bool Project::BuildFileCollectionIfReady()
{
	LogElapsedTime et("Proj::BFCIR", 1000);
	if (!IsOkToIterateFiles() || StopIt)
		return false;

	{
		RWLockReader lck(mFilelistLock);
		if (!mFileListNeedsUpdate)
			return true;
	}

	RWLockWriter lck(mFilelistLock);

	{
		RWLockReader lck2(mMapLock);

		if (!mFileListNeedsUpdate)
			return true;

		// build mProjectFileItemList
		FileList tmp;
		for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			const FileList& files(prjInf->GetFiles());
			for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
				tmp.AddUnique(*it);
		}

		{
			__lock(mFileListGeneration_cs);
			mFileListGeneration += mFileListNeedsUpdate.exchange(0);
			mProjectFileItemList.swap(tmp);
		}
	}

	return true;
}

// get Unreal Engine 4 install dirs from the registry [case: 105950]
SemiColonDelimitedString GetUe4Dirs()
{
	CStringW ue4Dirs;

	// get ue4 dirs for binary distributions
	const CStringW binKeyName = L"SOFTWARE\\EpicGames\\Unreal Engine"; // "EpicGames" is intentional.
	HKEY binKey;
	LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, binKeyName.GetString(), 0, KEY_READ | KEY_WOW64_64KEY, &binKey);

	if (status == ERROR_SUCCESS)
	{
		DWORD subkeyCount = 0;
		status = RegQueryInfoKeyW(binKey, nullptr, nullptr, nullptr, &subkeyCount, nullptr, nullptr, nullptr, nullptr,
		                          nullptr, nullptr, nullptr);

		if (status == ERROR_SUCCESS)
		{
			for (DWORD i = 0; i < subkeyCount; ++i)
			{
				WCHAR subKeyName[MAX_PATH];
				DWORD subKeyNameMax = MAX_PATH;
				status = RegEnumKeyExW(binKey, i, subKeyName, &subKeyNameMax, nullptr, nullptr, nullptr, nullptr);

				if (status == ERROR_SUCCESS)
				{
					const CStringW distroKeyName = binKeyName + L'\\' + subKeyName;
					HKEY distroKey;
					status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, distroKeyName.GetString(), 0, KEY_READ | KEY_WOW64_64KEY,
					                       &distroKey);

					if (status == ERROR_SUCCESS)
					{
						WCHAR data[MAX_PATH];
						DWORD dataMax = MAX_PATH;
						status = RegQueryValueExW(distroKey, L"InstalledDirectory", nullptr, nullptr, (LPBYTE)data,
						                          &dataMax);

						if (status == ERROR_SUCCESS)
						{
							// [case: 114965] [ue4] engine directory detection is bad for licensee solutions
							CStringW ue4Dir = data;
							ue4Dir.Replace(L'/', L'\\'); // standardize the slashes
							ue4Dir.Append(L"\\Engine");

							if (IsDir(ue4Dir))
							{
								ue4Dir += L';';
								ue4Dirs += ue4Dir;

								// [case: 150119] store all binary distribution versions and paths
								if (GlobalProject)
									GlobalProject->SetUnrealBinaryVersionPath(subKeyName, data);
							}
						}
					}
				}
			}
		}
	}

	// get ue4 dirs for source distributions
	HKEY srcKey;
	status = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Epic Games\\Unreal Engine\\Builds", 0, KEY_READ,
	                       &srcKey); // "Epic Games" is intentional.

	if (status == ERROR_SUCCESS)
	{
		DWORD valueCount = 0;
		status = RegQueryInfoKeyW(srcKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, nullptr,
		                          nullptr, nullptr, nullptr);

		if (status == ERROR_SUCCESS)
		{
			for (DWORD i = 0; i < valueCount; ++i)
			{
				WCHAR valueName[MAX_PATH];
				DWORD valueNameMax = MAX_PATH;
				status = RegEnumValueW(srcKey, i, valueName, &valueNameMax, nullptr, nullptr, nullptr, nullptr);

				if (status == ERROR_SUCCESS)
				{
					WCHAR data[MAX_PATH];
					DWORD dataMax = MAX_PATH;
					status = RegQueryValueExW(srcKey, valueName, nullptr, nullptr, (LPBYTE)data, &dataMax);

					if (status == ERROR_SUCCESS)
					{
						// [case: 114965] [ue4] engine directory detection is bad for licensee solutions
						CStringW ue4Dir = data;
						ue4Dir.Replace(L'/', L'\\'); // standardize the slashes
						ue4Dir.Append(L"\\Engine");

						if (IsDir(ue4Dir))
						{
							ue4Dir += L';';
							ue4Dirs += ue4Dir;
						}
					}
				}
			}
		}
	}

	ue4Dirs.Replace(L"\\\\", L"\\");

	if (!sUE4Project.IsEmpty())
	{
		CStringW projDir(Path(sUE4Project));
		CStringW tmp1(projDir + L"\\Engine");
		if (IsDir(tmp1))
			projDir = tmp1;

		projDir += L";";
		tmp1 = projDir;
		tmp1.MakeLower();

		CStringW tmp2(ue4Dirs);
		tmp2.MakeLower();
		if (-1 == tmp2.Find(tmp1))
			ue4Dirs += projDir;
	}

	return SemiColonDelimitedString(ue4Dirs);
}

// get Unity Engine dirs which should be included but aren't supplied by the projects [case: 105950]
SemiColonDelimitedString GetUnityEngineExtraIncludes()
{
	CStringW extraIncludes;
	const CStringW binKeyName = L"SOFTWARE\\Unity Technologies\\Installer";
	HKEY binKey;
	LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, binKeyName.GetString(), 0, KEY_READ | KEY_WOW64_64KEY, &binKey);

	if (status == ERROR_SUCCESS)
	{
		DWORD subkeyCount = 0;
		status = RegQueryInfoKeyW(binKey, nullptr, nullptr, nullptr, &subkeyCount, nullptr, nullptr, nullptr, nullptr,
		                          nullptr, nullptr, nullptr);

		if (status == ERROR_SUCCESS)
		{
			for (DWORD i = 0; i < subkeyCount; ++i)
			{
				WCHAR subKeyName[MAX_PATH];
				DWORD subKeyNameMax = MAX_PATH;
				status = RegEnumKeyExW(binKey, i, subKeyName, &subKeyNameMax, nullptr, nullptr, nullptr, nullptr);

				if (status == ERROR_SUCCESS)
				{
					const CStringW distroKeyName = binKeyName + L'\\' + subKeyName;
					HKEY distroKey;
					status = RegOpenKeyExW(HKEY_CURRENT_USER, distroKeyName.GetString(), 0, KEY_READ | KEY_WOW64_64KEY,
					                       &distroKey);

					if (status == ERROR_SUCCESS)
					{
						WCHAR data[MAX_PATH];
						DWORD dataMax = MAX_PATH;
						status = RegQueryValueExW(distroKey, L"Location x64", nullptr, nullptr, (LPBYTE)data, &dataMax);

						if (status == ERROR_SUCCESS)
						{
							// [case: 114965] [ue4] engine directory detection is bad for licensee solutions
							CStringW extraInclude = data;
							extraInclude.Replace(L'/', L'\\');
							extraInclude.Append(L"\\Editor\\Data\\CGIncludes");

							if (IsDir(extraInclude))
							{
								extraInclude += L';';
								extraIncludes += extraInclude;
								// TODO: For now, only return the first CGIncludes we find. We should be returning the
								// proper GDIncludes for the version of Unity they are using.
								break;
							}
						}
					}
				}
			}
		}
	}

	return SemiColonDelimitedString(extraIncludes);
}

void EnumerateVsProjectsForFile(const CStringW& fileName, std::function<bool(struct IVsHierarchy*)> onProject)
{
	try
	{
		_ASSERT_IF_NOT_ON_MAIN_THREAD();
		_THROW_IF_NOT_ON_MAIN_THREAD("Not main thread!");

		CComPtr<IVsSolution> sln = GetVsSolution();
	
		if (sln)
		{
			// iterate all projects

			CComPtr<IEnumHierarchies> pEnumHierarchies;
			if (SUCCEEDED(sln->GetProjectEnum(EPF_ALLPROJECTS, GUID_NULL, &pEnumHierarchies)))
			{
				CComBSTR fileNameBSTR(fileName);
	
				ULONG cnt = 0;
				IVsHierarchy* pHierarchyIter = nullptr;
				while (SUCCEEDED(pEnumHierarchies->Next(1, &pHierarchyIter, &cnt)) && cnt)
				{
					for (ULONG idx = 0; idx < cnt; ++idx, ++pHierarchyIter)
					{
						CComPtr<IVsHierarchy> pCurrent;		// handle Release
						pCurrent.Attach(pHierarchyIter);	// no AddRef call
	
						CComQIPtr<IVsProject> vsproj(pCurrent);	// query interface
						if (vsproj)
						{
							VSDOCUMENTPRIORITY docPriority;
							BOOL found;
							VSITEMID itemId;
	
							// if the project contains the file, pass it to the functor
							if (SUCCEEDED(vsproj->IsDocumentInProject(fileNameBSTR, &found, &docPriority, &itemId)) && found && itemId)
							{
								if (!onProject(pCurrent))
									return;
							}
						}
					}
				}
			}
		}
	}
	catch (...)
	{
	}
}

int GetProjectNamesForFile(const CStringW& fileName, std::vector<std::tuple<CStringW, int>>& projs)
{
	int count = 0;
	EnumerateVsProjectsForFile(fileName, [&](struct IVsHierarchy* hrch) {
		projs.emplace_back(GetProjectName(hrch), GetProjectIconIdx(hrch));
		count++;
		return true;
	});
	return count;
}

bool OpenFileInSpecificProject(const CStringW& projectName, EdCnt* ed)
{
	try
	{
		_ASSERT_IF_NOT_ON_MAIN_THREAD();
		_THROW_IF_NOT_ON_MAIN_THREAD("Not main thread!");

		extern CComQIPtr<IVsSolution> GetVsSolution();

		CComPtr<IVsSolution> sln = GetVsSolution();

		if (sln && ed && ed->m_IVsTextView)
		{
			// first we need to identify the buffer of edit control
			CComPtr<IVsTextLines> edBuff;
			if (SUCCEEDED(ed->m_IVsTextView->GetBuffer(&edBuff)))
			{
				CComBSTR fileNameBSTR(ed->FileName());
				CComBSTR projNameBSTR(projectName);

				// get enumerator of projects in solution
				CComPtr<IEnumHierarchies> pEnumHierarchies;
				if (SUCCEEDED(sln->GetProjectEnum(EPF_ALLPROJECTS, GUID_NULL, &pEnumHierarchies)))
				{
					ULONG cnt = 0;
					IVsHierarchy* pHierarchyIter = nullptr;

					// find the project by its name
					while (SUCCEEDED(pEnumHierarchies->Next(1, &pHierarchyIter, &cnt)) && cnt)
					{
						for (ULONG idx = 0; idx < cnt; ++idx, ++pHierarchyIter)
						{
							CComPtr<IVsHierarchy> pCurrent;
							pCurrent.Attach(pHierarchyIter);

							CComVariant extObj;
							pCurrent->GetProperty(VSITEMID_ROOT, VSHPROPID_ProjectName, &extObj);
							if (extObj.vt == VT_BSTR && projectName.Compare(extObj.bstrVal) == 0)
							{
								CComQIPtr<IVsProject> vsproj(pCurrent);
								if (vsproj)
								{
									VSDOCUMENTPRIORITY docPriority;
									BOOL found;
									VSITEMID itemId;

									// check if the project contains the file and get its identity
									if (SUCCEEDED(vsproj->IsDocumentInProject(fileNameBSTR, &found, &docPriority, &itemId)) && found && itemId)
									{
										GUID logId = LOGVIEWID_Primary;

										// This block is necessary when file is linked old way (pre vs2022 way)
										// but is necessary also in vs2022 for solution from previous IDE version
										HRESULT res = pCurrent->GetProperty(itemId, VSHPROPID_ExtObject, &extObj);
										if (SUCCEEDED(res) && extObj.vt == VT_DISPATCH)
										{
											try
											{
												CComQIPtr<EnvDTE::ProjectItem> projItem(extObj.pdispVal);
												if (projItem)
												{
													CComBSTR logIdStr(logId);
													CComPtr<EnvDTE::Window> pWindow;
													projItem->Open(logIdStr, &pWindow);
												}
											}
											catch(...){}
										}

										CComPtr<IVsWindowFrame> frame;

										// open the file in found project for already open buffer, 
										// this just assigns other project to the buffer while preserving its state
										// so assigned text view and buffer in the EdCnt should remain valid 
										if (SUCCEEDED(vsproj->OpenItem(itemId, logId, edBuff, &frame)))
										{
											if (SUCCEEDED(frame->Show()))
											{
												CComVariant var;

												// get the view for open document
												if (SUCCEEDED(frame->GetProperty(VSFPROPID_DocView, &var)) &&
													var.vt == VT_UNKNOWN && var.punkVal)
												{
													CComQIPtr<IVsCodeWindow> pCodeWindow(var.punkVal);
													if (pCodeWindow)
													{
														CComPtr<IVsTextView> textView;
														if (SUCCEEDED(pCodeWindow->GetPrimaryView(&textView)))
														{
															// if all worked properly, this should be true
															_ASSERT(ed->m_IVsTextView == textView);

															// assign view to the edit control
															// the main reason to do this is that it also updates the project name, 
															// but also existing buffer could not be in the same view
															ed->SetIvsTextView(textView);
															return true;
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	catch (...)
	{
	}

	return false;
}

int GetProjectIconIdx(IVsHierarchy* project)
{
	try
	{
		_ASSERT_IF_NOT_ON_MAIN_THREAD();
		_THROW_IF_NOT_ON_MAIN_THREAD("Not main thread!");
		GUID projGuid;
		if (project && SUCCEEDED(project->GetGuidProperty(VSITEMID_ROOT, __VSHPROPID::VSHPROPID_TypeGuid, &projGuid)))
		{
			static const GUID CSHARP_PROJECT_GUID = {0xFAE04EC0, 0x301F, 0x11D3, {0xBF, 0x4B, 0x00, 0xC0, 0x4F, 0x79, 0xEF, 0xBC}};
			static const GUID VB_PROJECT_GUID = {0xF184B08F, 0xC81C, 0x45F6, {0xA5, 0x7F, 0x5A, 0xBD, 0x99, 0x91, 0xF2, 0x8F}};
			static const GUID CPP_PROJECT_GUID = {0x4A76C5F7, 0x21C6, 0x4226, {0xB4, 0xA8, 0xDB, 0xBC, 0x21, 0x37, 0xF3, 0x01}};
			static const GUID ASPNET_PROJECT_GUID = {0x349C5851, 0x65DF, 0x11DA, {0x93, 0x84, 0x00, 0x06, 0x5B, 0x84, 0x6F, 0x21}};
			static const GUID WEB_APP_PROJECT_GUID = {0xE24C65DC, 0x7377, 0x472B, {0x9A, 0xBA, 0xBC, 0x80, 0x3B, 0x73, 0xC6, 0x1A}};
	
			if (IsEqualGUID(projGuid, CSHARP_PROJECT_GUID))
				return ICONIDX_FILE_CS;
			
			if (IsEqualGUID(projGuid, VB_PROJECT_GUID))
				return ICONIDX_FILE_VB;
			
			if (IsEqualGUID(projGuid, CPP_PROJECT_GUID))
				return ICONIDX_FILE_CPP;
			
			if (IsEqualGUID(projGuid, ASPNET_PROJECT_GUID))
				return ICONIDX_FILE_HTML;
			
			if (IsEqualGUID(projGuid, WEB_APP_PROJECT_GUID))
				return ICONIDX_FILE_HTML;	
		}
	}
	catch (...)
	{
	}

	return ICONIDX_FILE;
}

CStringW GetProjectName(IVsHierarchy* project)
{
	try
	{
		_ASSERT_IF_NOT_ON_MAIN_THREAD();
		_THROW_IF_NOT_ON_MAIN_THREAD("Not main thread!");
		CComVariant extObj;
		if (project && SUCCEEDED(project->GetProperty(VSITEMID_ROOT, __VSHPROPID::VSHPROPID_ProjectName, &extObj)))
		{
			if (extObj.vt == VT_BSTR)
			{
				return extObj.bstrVal;
			}
		}
	}
	catch (...)
	{
	}

	return CStringW();
}

static CCriticalSection sProjAddlDirsLock;
static CStringW sProjAddlDirs;
std::atomic<int> sProjAddlDirsActiveCnt;

// seems to be an aggregated list from all projects and it does include sys dirs as well
// it does not just return a single project's additional dirs as the name suggests.
CStringW Project::GetProjAdditionalDirs()
{
	if (sProjAddlDirsActiveCnt && g_mainThread == GetCurrentThreadId())
	{
		// [case: 100382]
		vLog("WARN: prevent ui thread block GPAD");
		return CStringW();
	}

	ScopedIncrementAtomic si(&sProjAddlDirsActiveCnt);
	AutoLockCs lck(sProjAddlDirsLock);
	if (!mAddlDirListNeedsUpdate)
		return sProjAddlDirs;

	LogElapsedTime tt("P::GPAD", 1000);

	// do not save cache if we are still loading files
	if (!m_loadingProjectFiles)
		mAddlDirListNeedsUpdate = false;

	FileList lst;
	ProjectMap::iterator iter;

	{
		RWLockReader lck2(mMapLock);
#if defined(VA_CPPUNIT)
		mCppUsesClr = gOverrideClr;
		mCppUsesWinRT = gOverrideWinRT;
#else
		mCppUsesClr = false;
		mCppUsesWinRT = false;
#endif // VA_CPPUNIT

		// first add all include dirs
		for (iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			const ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			const FileList& includeDirs(prjInf->GetIncludeDirs());
			for (FileList::const_iterator it = includeDirs.begin(); it != includeDirs.end(); ++it)
				lst.AddUnique(*it);

			if (prjInf->CppUsesClr())
				mCppUsesClr = true;
			if (prjInf->CppUsesWinRT())
				mCppUsesWinRT = true;

			const FileList& usingDirs(prjInf->GetUsingDirs());
			for (FileList::const_iterator it = usingDirs.begin(); it != usingDirs.end(); ++it)
				mUsingDirs.AddUnique(*it);
		}

		// then append all source dirs (don't intermix via a single mMap iteration)
		for (iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			const ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			const FileList& projectDirs(prjInf->GetProjectDirs());
			for (FileList::const_iterator it = projectDirs.begin(); it != projectDirs.end(); ++it)
				lst.AddUnique(*it);
		}
	}

	const bool isCustomPlatform = IncludeDirs::IsCustom();
	int collisionCnt = 0;
	IncludeDirs incDirs;
	CStringW tmp;
	CStringW privateSysDirs(incDirs.getSolutionPrivateSysIncludeDirs());
	SemiColonDelimitedString sysDirs(incDirs.getSysIncludes());
	SemiColonDelimitedString ue4Dirs;
	CStringW ueAlternateBinaryInstallPath; // [case: 149208]

	if (mSolutionFile.GetLength())
	{
#if 0 // disabled see #case105950todo
      // [case: 105950] [case: 114958]
		if (Psettings->mUnrealEngineCppSupport && Psettings->mAlwaysDisplayUnrealSymbolsInItalics &&
			kProjectDefinedPlatform == incDirs.GetPlatform())
		{
			ue4Dirs = GetUe4Dirs();
		}
#endif

		// [case: 149208]
		if (GlobalProject && GlobalProject->GetUnrealBinaryVersionPath().contains(GlobalProject->GetUnrealEngineVersion()))
		{
			try
			{
				// if GetUnrealEngineVersion is populated and there is a registry entry (in GetUnrealBinaryVersionPath) with matching
				// UE version, it can be assumed that this is binary UE distribution so VA can use this as an alternate path
				// also no need to additionally check here if this is UE project since if it is not, this will not be executed
				ueAlternateBinaryInstallPath = GlobalProject->GetUnrealBinaryVersionPath().at(GlobalProject->GetUnrealEngineVersion());
			}
			catch (const std::out_of_range&)
			{
				// should not ever happen since we check existence of element in the map, but it's here for correctness
			}
		}

		// [case: 113791]
		// check for solution folder collision with sys include dir list
		// so that user va_stdafx can overrides definitions in private system includes
		sysDirs.Reset();
		LPCWSTR pCurSysDir;
		int curDirLen;
		CStringW slnDir;
		if (IsDir(mSolutionFile))
			slnDir = mSolutionFile;
		else
			slnDir = ::Path(mSolutionFile);

		const int kInitialLen = slnDir.GetLength();
		while (sysDirs.NextItem(pCurSysDir, curDirLen))
		{
			if (!curDirLen)
				continue;

			if (curDirLen > kInitialLen && _wcsnicmp(slnDir, pCurSysDir, (uint)slnDir.GetLength()) == 0)
			{
				CStringW sysPath(pCurSysDir, curDirLen);
				if (sysPath.GetLength())
				{
					bool updateSolnPrivateSysIncDir = true;
					for (const auto& it : lst)
					{
						if (sysPath.GetLength() <= it.mFilenameLower.GetLength())
						{
							if (_wcsnicmp(sysPath, it.mFilenameLower, (uint)sysPath.GetLength()) == 0)
							{
								// [case: 142235]
								// don't add to solutionPrivateSystemIncludeDirs if already have source from projects
								updateSolnPrivateSysIncDir = false;
								break;
							}
						}
					}

					if (updateSolnPrivateSysIncDir)
					{
						vLog("WARN: sys include dir collision with sln dir: sln(%s) sys(%s)", (LPCTSTR)CString(slnDir),
						     (LPCTSTR)CString(sysPath));
						mSolutionPrivateSystemIncludeDirs.AddUniqueNoCase(sysPath);
						sysPath += L";";

						// don't remove from sys includes, since getSysIncludes() is used everywhere.
						// consider updating all uses of getSysIncludes() with additional calls to
						// getSolutionPrivateSysIncludeDirs().
						// incDirs.RemoveSysIncludeDir(sysPath);

						incDirs.AddSolutionPrivateSysIncludeDir(sysPath);
						++collisionCnt;
					}
				}
			}
			else if (Psettings->mUnrealEngineCppSupport && !isCustomPlatform)
			{
				CStringW sysPath(pCurSysDir, curDirLen);
				CStringW sysPathLower(sysPath);
				sysPathLower.MakeLower();
				if (-1 != sysPathLower.Find(LR"(\engine\intermediate\build\)") ||
				    -1 != sysPathLower.Find(LR"(\engine\plugins\)") ||
				    -1 != sysPathLower.Find(LR"(\engine\source\thirdparty\)"))
				{
					// don't keep \engine\intermediate\build\* dirs as system
					// includes unless they are in program files dir.
					// once #case105950todo is implemented, this logic will
					// need to be updated to accommodate those changes (don't
					// do this move for ue4Dirs either).
					// [case: 149208] don't keep those also for alternate binary UE install dirs
					if ((kWinProgFilesDirX86.IsEmpty() && kWinProgFilesDir.IsEmpty()) ||
					    (!kWinProgFilesDirX86.IsEmpty() &&
					     _wcsnicmp(kWinProgFilesDirX86, sysPath, (uint)kWinProgFilesDirX86.GetLength()) != 0) ||
					    (!kWinProgFilesDir.IsEmpty() &&
					     _wcsnicmp(kWinProgFilesDir, sysPath, (uint)kWinProgFilesDir.GetLength()) != 0) ||
					    (!ueAlternateBinaryInstallPath.IsEmpty() &&
					     _wcsnicmp(ueAlternateBinaryInstallPath, sysPath, (uint)ueAlternateBinaryInstallPath.GetLength()) != 0))
					{
						vLog("WARN: sys include dir collision with ue/e/[ips]/: sln(%s) sys(%s)",
						     (LPCTSTR)CString(slnDir), (LPCTSTR)CString(sysPath));
						// [case: 142235]
						// move the sys include dir to the project list since it was not an exact match
						lst.AddUnique(sysPath);
						sysPath += L";";
						incDirs.RemoveSysIncludeDir(sysPath);
						++collisionCnt;
					}
				}
			}
		}

		if (collisionCnt)
		{
			sysDirs = incDirs.getSysIncludes();
			privateSysDirs = incDirs.getSolutionPrivateSysIncludeDirs();
		}
	}

	{
		CStringW kWinProgFilesDirX86_lwr = kWinProgFilesDirX86;
		kWinProgFilesDirX86_lwr.MakeLower();
		CStringW kWinProgFilesDir_lwr = kWinProgFilesDir;
		kWinProgFilesDir_lwr.MakeLower();
		CStringW kIdeInstallDir_lwr = kIdeInstallDir;
		kIdeInstallDir_lwr.MakeLower();
		CStringW kMsSdksInstallDir_lwr = kMsSdksInstallDir;
		kMsSdksInstallDir_lwr.MakeLower();
		CStringW kMsSdksInstallDirX86_lwr = kMsSdksInstallDirX86;
		kMsSdksInstallDirX86_lwr.MakeLower();
		CStringW kWindowsKitsInstallDir_lwr = kWindowsKitsInstallDir;
		kWindowsKitsInstallDir_lwr.MakeLower();
		CStringW kWindowsKitsInstallDirX86_lwr = kWindowsKitsInstallDirX86;
		kWindowsKitsInstallDirX86_lwr.MakeLower();
		CStringW privateSysDirs_lwr = privateSysDirs;
		privateSysDirs_lwr.MakeLower();
		CStringW cur, cur_lwr;
		CStringW sysPath, sysPath_lwr;

		for (FileList::const_iterator it = lst.begin(); it != lst.end(); ++it)
		{
			cur.SetString(it->mFilename.GetString(), it->mFilename.GetLength());
			cur.AppendChar(L';');
			cur_lwr.SetString(cur.GetString(), cur.GetLength());
			cur_lwr.MakeLower();
			static const auto begins_with = [](const CStringW& str, const CStringW& beg) {
//				if ((str.GetLength() < beg.GetLength()) || !beg.GetLength())
//					return false;
//				return !memcmp(str.GetString(), beg.GetString(), (uint32_t)beg.GetLength());
				return !str.IsEmpty() && (0 == wcsncmp(str.GetString(), beg.GetString(), (uint32_t)str.GetLength()));
			};

#ifdef AVR_STUDIO
			// [case: 68487]
			// ignore system/project collisions in AVR
			// retain pre-change 20967 / bug 61788 behavior
			// see IsSystemFile
#else
			if (Psettings->mForceProgramFilesDirsSystem && !isCustomPlatform &&
			    (begins_with(kWinProgFilesDirX86_lwr, cur_lwr) || begins_with(kWinProgFilesDir_lwr, cur_lwr)))
			{
				// [case: 108561]
				vLogUnfiltered("WARN: Project dir collision 0a with program files dir: proj(%s)", (LPCTSTR)CString(cur));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (begins_with(kIdeInstallDir_lwr, cur_lwr))
			{
				// [case: 69244]
				vLogUnfiltered("WARN: Project dir collision 1a with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
				               (LPCTSTR)CString(kIdeInstallDir));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (begins_with(kMsSdksInstallDir_lwr, cur_lwr))
			{
				// [case: 73223]
				vLogUnfiltered("WARN: Project dir collision 1b with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
				               (LPCTSTR)CString(kMsSdksInstallDir));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (begins_with(kMsSdksInstallDirX86_lwr, cur_lwr))
			{
				// [case: 73223]
				vLogUnfiltered("WARN: Project dir collision 1c with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
				               (LPCTSTR)CString(kMsSdksInstallDirX86));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (begins_with(kWindowsKitsInstallDir_lwr, cur_lwr))
			{
				// [case: 73223]
				vLogUnfiltered("WARN: Project dir collision 1d with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
				               (LPCTSTR)CString(kWindowsKitsInstallDir));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (begins_with(kWindowsKitsInstallDirX86_lwr, cur_lwr))
			{
				// [case: 73223]
				vLogUnfiltered("WARN: Project dir collision 1e with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
				               (LPCTSTR)CString(kWindowsKitsInstallDirX86));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (!ueAlternateBinaryInstallPath.IsEmpty() &&
					_wcsnicmp(ueAlternateBinaryInstallPath, cur, (uint)ueAlternateBinaryInstallPath.GetLength()) == 0)
			{
				// [case: 149208]
				vLogUnfiltered("WARN: Project dir collision UEALTBIN with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			    	           (LPCTSTR)CString(ueAlternateBinaryInstallPath));
				incDirs.AddSysIncludeDir(cur);
				++collisionCnt;
				// don't add cur dir to tmp since it is a sys dir
				continue;
			}
			else if (ue4Dirs.GetBaseLength())
			{
				// this code is never exec'd due to ue4Dirs not being init above at #case105950todo
				LPCWSTR ue4Dir = nullptr;
				int ue4DirLen = 0;
				bool isUe4Dir = false;
				ue4Dirs.Reset();

				while (ue4Dirs.NextItem(ue4Dir, ue4DirLen) && !isUe4Dir)
				{
					if (ue4DirLen > 0)
					{
						if (_wcsnicmp(ue4Dir, cur, (uint)ue4DirLen) == 0)
						{
							isUe4Dir = true;
							break;
						}
					}
				}

				if (isUe4Dir)
				{
					// [case: 105950] consider ue4 dirs to be system
					vLogUnfiltered("WARN: Project dir collision UNREAL with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
					               (LPCTSTR)CString(kWindowsKitsInstallDirX86));
#if 0
				// assume not a shared system dir since Program Files are handled before considering ue4dirs
				// assume private to solution
				incDirs.AddSolutionPrivateSysIncludeDir(cur);
				_ASSERTE(cur[cur.GetLength() - 1] == ';');
				CStringW tmp = cur.Left(cur.GetLength() - 1);
				mSolutionPrivateSystemIncludeDirs.AddUniqueNoCase(tmp);
				// #case105950todo
				// populate mSolutionPrivateSystemHeaders from projectInfo for cur project
				// add member mSolutionPrivateSystemSource to class Project
				// populate mSolutionPrivateSystemSource from projectInfo for cur project
				// parse mSolutionPrivateSystemSource with VA_DB_SolutionPrivateSystem dbFlag
				// add VA_DB_SolutionPrivateSystem for c# files (ue engine source has c# files, private Sys db support not tested?)
				// update FileParserWorkItem::DoParseWork to properly handle edits to private sys files
				// make sure mSolutionPrivateSystemSource is parsed before other solution source (in Project::LoadProject)
#else
					// original implementation
					incDirs.AddSysIncludeDir(cur);
#endif
					++collisionCnt;
					// don't add cur dir to tmp since it is a sys dir
					continue;
				}
			}

			if (!privateSysDirs_lwr.IsEmpty() && !!wcsstr(privateSysDirs_lwr, cur_lwr))
			{
				++collisionCnt;
				vLogUnfiltered("WARN: Project dir collision 1f with private sys include dir: proj(%s)", (LPCTSTR)CString(cur));
				// don't add cur dir to tmp since it is a sys dir (private sys / nuget)
				continue;
			}

			// check for project dir collisions with sys include dir list
			sysDirs.Reset();
			LPCWSTR pCurSysDir;
			int curDirLen;
			const int kInitialLen = cur.GetLength();
			while (sysDirs.NextItem(pCurSysDir, curDirLen))
			{
				if (!curDirLen)
					continue;

				if ((curDirLen <= kInitialLen) && _wcsnicmp(pCurSysDir, cur, (uint)curDirLen) == 0)
				{
					sysPath.SetString(pCurSysDir, curDirLen);

					vLogUnfiltered("WARN: Project dir collision 2 with sys include dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
						            (LPCTSTR)CString(sysPath));
					sysPath += L";";
					incDirs.RemoveSysIncludeDir(sysPath);
					sysPath_lwr.SetString(sysPath.GetString(), sysPath.GetLength());
					sysPath_lwr.MakeLower();
					if (wcsncmp(sysPath_lwr, cur_lwr, (uint)sysPath_lwr.GetLength()) != 0)
					{
						// move the sys include dir to the project list since it was not an exact match
						cur += sysPath;
						cur_lwr += sysPath_lwr;
					}
					++collisionCnt;
				}
			}
#endif

			tmp += cur;
		}
	}

	sysDirs = incDirs.getSourceDirs();
	for (FileList::const_iterator it = lst.begin(); it != lst.end(); ++it)
	{
		CStringW cur((*it).mFilename + L";");
#ifdef AVR_STUDIO
		// [case: 68487]
		// ignore system/project collisions in AVR
		// retain pre-change 20967 / bug 61788 behavior
		// see IsSystemFile
#else
		if (Psettings->mForceProgramFilesDirsSystem && !isCustomPlatform &&
		    ((!kWinProgFilesDirX86.IsEmpty() &&
		      _wcsnicmp(kWinProgFilesDirX86, cur, (uint)kWinProgFilesDirX86.GetLength()) == 0) ||
		     (!kWinProgFilesDir.IsEmpty() &&
		      _wcsnicmp(kWinProgFilesDir, cur, (uint)kWinProgFilesDir.GetLength()) == 0)))
		{
			// [case: 108561]
			vLogUnfiltered("WARN: Project dir collision 0 with program files dir: proj(%s)", (LPCTSTR)CString(cur));
			// [case: 109019] if already exists in system include, don't also add to system source
			incDirs.AddSourceDir(cur, true);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}
		else if (!kIdeInstallDir.IsEmpty() && _wcsnicmp(kIdeInstallDir, cur, (uint)kIdeInstallDir.GetLength()) == 0)
		{
			// [case: 69244]
			vLogUnfiltered("WARN: Project dir collision 1 with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			     (LPCTSTR)CString(kIdeInstallDir));
			incDirs.AddSourceDir(cur);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}
		else if (!kMsSdksInstallDir.IsEmpty() &&
		         _wcsnicmp(kMsSdksInstallDir, cur, (uint)kMsSdksInstallDir.GetLength()) == 0)
		{
			// [case: 73223]
			vLogUnfiltered("WARN: Project dir collision 1b with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			     (LPCTSTR)CString(kMsSdksInstallDir));
			incDirs.AddSourceDir(cur);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}
		else if (!kMsSdksInstallDirX86.IsEmpty() &&
		         _wcsnicmp(kMsSdksInstallDirX86, cur, (uint)kMsSdksInstallDirX86.GetLength()) == 0)
		{
			// [case: 73223]
			vLogUnfiltered("WARN: Project dir collision 1c with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			     (LPCTSTR)CString(kMsSdksInstallDirX86));
			incDirs.AddSourceDir(cur);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}
		else if (!kWindowsKitsInstallDir.IsEmpty() &&
		         _wcsnicmp(kWindowsKitsInstallDir, cur, (uint)kWindowsKitsInstallDir.GetLength()) == 0)
		{
			// [case: 73223]
			vLogUnfiltered("WARN: Project dir collision 1d with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			     (LPCTSTR)CString(kWindowsKitsInstallDir));
			incDirs.AddSourceDir(cur);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}
		else if (!kWindowsKitsInstallDirX86.IsEmpty() &&
		         _wcsnicmp(kWindowsKitsInstallDirX86, cur, (uint)kWindowsKitsInstallDirX86.GetLength()) == 0)
		{
			// [case: 73223]
			vLogUnfiltered("WARN: Project dir collision 1e with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
			     (LPCTSTR)CString(kWindowsKitsInstallDirX86));
			incDirs.AddSourceDir(cur);
			++collisionCnt;
			// don't add cur dir to tmp since it is a sys dir
			continue;
		}

		// check for project dir collisions with sys source dir list
		sysDirs.Reset();
		LPCWSTR pCurDir;
		int curDirLen;
		const int kInitialLen = cur.GetLength();
		while (sysDirs.NextItem(pCurDir, curDirLen))
		{
			if (curDirLen && curDirLen <= kInitialLen && _wcsnicmp(pCurDir, cur, (uint)curDirLen) == 0)
			{
				CStringW sysPath(pCurDir, curDirLen);
				if (sysPath.GetLength())
				{
					vLogUnfiltered("WARN: Project dir collision 2 with sys source dir: proj(%s) sys(%s)", (LPCTSTR)CString(cur),
					     (LPCTSTR)CString(sysPath));
					sysPath += L";";
					incDirs.RemoveSourceDir(sysPath);
					if (_wcsnicmp(sysPath, cur, (uint)sysPath.GetLength()) != 0)
					{
						// move the sys source dir to the project list since it was not an exact match
						cur += sysPath;
					}
					++collisionCnt;
				}
			}
		}
#endif

		tmp += cur;
	}

	sProjAddlDirs = VerifyPathList(tmp);
	if (collisionCnt)
	{
		// [case: 78821]
		// do this to update va dirs list for va options dlg
		IncludeDirs::UpdateRegistry();
	}

	if (!m_loadingProjectFiles && gFileFinder)
		gFileFinder->Reload();

	return sProjAddlDirs;
}

void Project::AddFile(const CStringW& project, const CStringW& file, BOOL nonbinarySourceFile)
{
	// don't need to call SortListIfReady() here since we don't need to pre-sort
	if (!IsOkToIterateFiles())
		return;

#if !defined(RAD_STUDIO)
	if (!IsFile(file))
		return;
#endif

	vLog("Pr::AF %s %s\n", (LPCTSTR)CString(project), (LPCTSTR)CString(file));
	::RunTriggeredCommand("FileAdded", file);

	if (RefactoringActive::IsActive())
	{
		switch (RefactoringActive::GetCurrentRefactoring())
		{
		case VARef_RenameFilesFromMenuCmd:
		case VARef_RenameFilesFromRefactorTip:
		case VARef_CreateFile:
		case VARef_MoveSelectionToNewFile:
		case VARef_CreateFromUsage: // [case: 80291] create from usage can cause create file as a nested op
			break;

		default:
			// [case: 43506]
			Log1("reject - refactoring active");
			return;
		}
	}

	CWaitCursor wait;
	if (nonbinarySourceFile)
	{
		if (::ShouldIgnoreFile(file, false))
		{
			vLog("Pr::AF reject: %s\n", (LPCTSTR)CString(file));
			return;
		}

		// add to project before starting parse
		RWLockWriter lck(mMapLock);
		ProjectInfoPtr inf;
		if (!project.IsEmpty())
		{
			CStringW projNameLower(project);
			projNameLower.MakeLower();
			inf = mMap[projNameLower];
			if (!inf)
			{
				mMap.erase(projNameLower);
				vLog("Pr::AF reject project: %s (%s)\n", (LPCTSTR)CString(project), (LPCTSTR)CString(file));
				// vs2008 fires AddFile events when switching between solutions.
				// if non-null project name does not already exist, return.
				return;
			}
		}

		if (!inf)
		{
			inf = mMap[Vsproject::kExternalFilesProjectName];
			if (!inf)
			{
				// case:18918 directory-based parsing support
				inf.reset(Vsproject::CreateExternalFileProject());
				if (inf)
					mMap[Vsproject::kExternalFilesProjectName] = inf;
			}
		}

		if (inf)
		{
			if (inf->ContainsFile(file))
			{
				vLog("Pr::AF already have it\n");
				return;
			}

			inf->AddFile(file);
			// IDE is adding files to solution (not VA)
			mSolutionIsInitiallyEmpty = false;
		}
		else
		{
			vLog("Pr::AF no proj inf\n");
		}

		if (!Psettings->m_FastProjectOpen && g_ParserThread)
			g_ParserThread->QueueNewProjectFile(file);
	}
	else
	{
		const CStringW ext(::GetBaseNameExt(file));
		if (ext.CompareNoCase(L"lib"))
		{
			if (!Psettings->m_FastProjectOpen && g_ParserThread)
				g_ParserThread->QueueParseWorkItem(new ReferenceImporter(file));

			{
				RWLockWriter lck(mFilelistLock);
				mReferencesList.AddUniqueNoCase(file);
			}
			CheckImplicitReferences(true);
		}
	}

	// increment cookie so that anyone that depends on the file list
	// will know they are out of sync
	mAddlDirListNeedsUpdate = true;
	FilesListNeedsUpdate();
}

void Project::AddExternalFile(const CStringW& file)
{
	// don't need to call SortListIfReady() here since we don't need to pre-sort
	if (!IsOkToIterateFiles())
		return;

	vLog("Pr::AEF %s\n", (LPCTSTR)CString(file));
	if (::ShouldIgnoreFile(file, false))
	{
		vLog("Pr::AEF reject: %s\n", (LPCTSTR)CString(file));
		return;
	}

	// add to project before starting parse
	RWLockWriter lck(mMapLock);
	ProjectInfoPtr inf = mMap[Vsproject::kExternalFilesProjectName];
	if (!inf)
	{
		// case:18918 directory-based parsing support
		inf.reset(Vsproject::CreateExternalFileProject());
		if (inf)
			mMap[Vsproject::kExternalFilesProjectName] = inf;
	}

	if (!inf)
		return;

	inf->AddFile(file);
	mAddlDirListNeedsUpdate = true;
	FilesListNeedsUpdate();
}

void Project::RemoveFile(const CStringW& project, const CStringW& file, BOOL nonbinarySourceFile)
{
	// don't need to call SortListIfReady() here since we don't need to pre-sort
	if (!IsOkToIterateFiles())
		return;

	vLog("Pr::RF %s\n", (LPCTSTR)CString(file));
	::RunTriggeredCommand("FileRemoved", file);

	if (RefactoringActive::IsActive())
	{
		if (RefactoringActive::GetCurrentRefactoring() != VARef_RenameFilesFromMenuCmd)
		{
			// [case: 43506]
			Log1("reject - refactoring active");
			return;
		}
	}

	CWaitCursor wait;
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new SymbolRemover(file));

	if (nonbinarySourceFile)
	{
		_ASSERTE(!gShellAttr->SupportsDspFiles());

		ProjectVec projectVec = GetProjectForFile(file);
		RWLockWriter lck(mMapLock);
		for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
		{
			ProjectInfoPtr p = *iter;
			const CStringW projFile(p->GetProjectFile());
			const CStringW ext(::GetBaseNameExt(projFile));
			if (!ext.CompareNoCase(L"shproj"))
				p->RemoveFile(file);
		}

		ProjectInfoPtr inf;
		if (!project.IsEmpty())
		{
			CStringW projNameLower(project);
			projNameLower.MakeLower();
			inf = mMap[projNameLower];
			if (!inf)
				mMap.erase(projNameLower);
		}

		if (!inf || !inf->ContainsFile(file))
		{
			inf = mMap[Vsproject::kExternalFilesProjectName];
			if (!inf)
				mMap.erase(Vsproject::kExternalFilesProjectName);
		}

		if (!inf)
			return;

		inf->RemoveFile(file);
	}
	else
	{
		RWLockWriter lck(mFilelistLock);
		mReferencesList.Remove(file);
	}

	mAddlDirListNeedsUpdate = true;
	FilesListNeedsUpdate();
}

void Project::RemoveFileWithoutPath(const CStringW& projName, CStringW basename)
{
	// don't need to call SortListIfReady() here since we don't need to pre-sort
	if (!IsOkToIterateFiles())
		return;

	vLog("Pr::RF %s\n", (LPCTSTR)CString(basename));
	if (RefactoringActive::IsActive())
	{
		if (RefactoringActive::GetCurrentRefactoring() != VARef_RenameFilesFromMenuCmd)
		{
			// [case: 43506]
			Log1("reject - refactoring active");
			return;
		}
	}

	CWaitCursor wait;
	_ASSERTE(!gShellAttr->SupportsDspFiles());
	ProjectInfoPtr inf;
	CStringW file;

	{
		RWLockWriter lck(mMapLock);

		if (!projName.IsEmpty())
		{
			CStringW projNameLower(projName);
			projNameLower.MakeLower();
			inf = mMap[projNameLower];
			if (!inf)
				mMap.erase(projNameLower);
		}

		if (!inf)
			return;

		basename.MakeLower();
		const FileList& files = inf->GetFiles();
		for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
		{
			const CStringW curFile((*it).mFilenameLower);
			const CStringW curBaseLower(::Basename(curFile));
			if (curBaseLower == basename)
			{
				if (!::IsFile(curFile))
				{
					file = curFile;
					break;
				}
			}
		}

		if (file.IsEmpty())
			return;

		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new SymbolRemover(file));
	}

	ProjectVec projectVec = GetProjectForFile(file);
	RWLockWriter lck(mMapLock);
	for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
	{
		ProjectInfoPtr p = *iter;
		const CStringW projFile(p->GetProjectFile());
		const CStringW ext(::GetBaseNameExt(projFile));
		if (!ext.CompareNoCase(L"shproj"))
			p->RemoveFile(file);
	}

	inf->RemoveFile(file);

	mAddlDirListNeedsUpdate = true;
	FilesListNeedsUpdate();
}

void Project::RenameFile(const CStringW& project, const CStringW& oldname, const CStringW& newname)
{
	if (newname.IsEmpty())
		return;

	if (newname[newname.GetLength() - 1] == L'\\' || newname[newname.GetLength() - 1] == L'/')
	{
		// rename of folder
		// if there were files in it, we'll get separate notifications for them
		return;
	}

	if (!IsOkToIterateFiles())
		return;

	CStringW oldNameWithPath;

	const CStringW newBase(Basename(newname));
	if (!newBase.CompareNoCase(oldname))
	{
		// notified of file rename via rename of folder rather than the base file.
		// rename of folder will have baseName(newname) == oldname
		oldNameWithPath = oldname;

		// need to find original path by searching projects for the basename
		RWLockReader lck2(mMapLock);
		for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			ProjectInfoPtr prjInf = (*iter).second;
			if (prjInf)
			{
				const CStringW prjFile(prjInf->GetProjectFile());
				if (!prjFile.CompareNoCase(project))
				{
					// iterate over all files of prjInf
					const FileList& prjFiles = prjInf->GetFiles();
					for (const auto& f : prjFiles)
					{
						CStringW fname(f.mFilename);
						CStringW tmp(Basename(fname));
						if (!tmp.CompareNoCase(oldname))
						{
							oldNameWithPath = fname;
							break;
						}
					}

					break;
				}
			}
		}
	}
	else
	{
		// rename file in place/same dir
		oldNameWithPath = oldname;
		if (oldNameWithPath.FindOneOf(L"\\/") == -1)
		{
			// oldName does not include path - take path from newName
			oldNameWithPath = newname;
			int pos1 = oldNameWithPath.ReverseFind(L'\\');
			int pos2 = oldNameWithPath.ReverseFind(L'/');
			if (pos1 == -1 && pos2 == -1)
				return;

			oldNameWithPath = oldNameWithPath.Left(max(pos1, pos2) + 1);
			oldNameWithPath += oldname;
		}
	}

	ProjectVec sharedProjs;

	if (!oldNameWithPath.IsEmpty())
	{
		// [case: 80931]
		ProjectVec projectVec = GetProjectForFile(oldNameWithPath);
		for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
		{
			ProjectInfoPtr p = *iter;
			const CStringW projFile(p->GetProjectFile());
			if (projFile.CompareNoCase(project))
			{
				const CStringW ext(::GetBaseNameExt(projFile));
				if (!ext.CompareNoCase(L"shproj"))
					sharedProjs.push_back(p);
			}
		}

		RemoveFile(project, oldNameWithPath);
	}

	AddFile(project, newname);

	for (auto iter = sharedProjs.begin(); iter != sharedProjs.end(); ++iter)
		AddFile((*iter)->GetProjectFile(), newname);
}

// This is called from the notification thread when a project file has been modified
void Project::CheckForChanges(const CStringW& prjFile)
{
	if (!BuildFileCollectionIfReady())
		return;

	bool changed = false;

	{
		CStringW projLower(prjFile);
		projLower.MakeLower();
		RWLockWriter lck(mMapLock);
		ProjectInfoPtr inf = mMap[projLower];
		if (inf)
		{
			changed = inf->CheckForChanges();
		}
		else
		{
			mMap.erase(projLower);

			if (gShellAttr->IsDevenv() && (projLower.Find(L".vsprops") != -1 || projLower.Find(L".props") != -1))
			{
				// property sheet changed - do lookup and find all dependent projects
				for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
				{
					ProjectInfoPtr prjInf = (*iter).second;
					if (!prjInf)
						continue;

					const FileList& propSheets(prjInf->GetPropertySheets());
					for (FileList::const_iterator it = propSheets.begin(); it != propSheets.end(); ++it)
					{
						const CStringW cur((*it).mFilename);
						if (!cur.CompareNoCase(prjFile))
						{
							if (prjInf->CheckForChanges())
								changed = true;
							break;
						}
					}
				}
			}
		}
	}

	if (changed)
	{
		// if changed is true then we had synchronous updates.
		// async updates set changed to false (or no updates at all).
		mAddlDirListNeedsUpdate = true;
		FilesListNeedsUpdate();

		// clear file finder cache for synchronous updates
		GetProjAdditionalDirs();
	}
}

void Project::ClearMap()
{
	RWLockWriter lck(mMapLock);
	mMap.clear();
	mMapFilesToProjects.clear();
	mSolutionIsInitiallyEmpty = true;
}

bool FileNeedsParse(CStringW f, BOOL checkSys = FALSE)
{
	if (!MultiParse::IsIncluded(f))
		return true;

	const UINT fileId = gFileIdManager->GetFileId(f);
	const int ftype = GetFileType(f);
	DbTypeId vADbId = g_DBFiles.DBIdFromParams(ftype, fileId, false, false);
	FILETIME* ft = g_DBFiles.GetFileTime(fileId, vADbId);
	if (!ft && checkSys)
	{
		// not found - check to see if in sys db
		uint dbFlags = 0;
		DTypePtr fileDat = MultiParse::GetFileData(f);
		if (fileDat)
			dbFlags = fileDat->DbFlags();
		vADbId = g_DBFiles.DBIdFromParams(ftype, fileId, true, false, dbFlags);
		ft = g_DBFiles.GetFileTime(fileId, vADbId);
	}

	if (!ft)
		return true;

	if (!ft->dwHighDateTime && !ft->dwLowDateTime)
		return true;

	if (!FileTimesAreEqual(ft, f))
		return true;

	return false;
}

void Project::CollectAndUpdateModifiedFiles()
{
	ModifiedFilesInfo info;
	CollectModificationInfo(info);
	UpdateModifiedFiles(info);
}

void Project::CollectModificationInfo(ModifiedFilesInfo& info)
{
	_ASSERTE(DatabaseDirectoryLock::GetOwningThreadID() == ::GetCurrentThreadId());
	BuildFileCollectionIfReady();

	info.mCollected = true;

	{
		// [case: 116689]
		LogElapsedTime tt("P::CMI remove missing files", 5000);
		CSpinCriticalSection memCs;
		auto fn = [&](DType* dt, bool checkInvalidSys) {
			// look for file entries (UNDEF and FileFlag)
			if (dt->MaskedType() != UNDEF)
				return;

			if (!dt->HasFileFlag())
				return;

			const UINT fid = dt->FileId();
			const CStringW filename(gFileIdManager->GetFile(fid));
			if (IsFile(filename))
				return;

			{
				RWLockReader lck(mFilelistLock);
				if (mProjectFileItemList.Contains(fid))
					return;
			}

			AutoLockCs l(memCs);
			info.deletedProjFilesThatNeedRemoval.insert(fid);
		};

		bool jnk = false;
		g_pGlobDic->ForEach(fn, jnk);
	}

	{
		RWLockReader lck(mFilelistLock);
		CCriticalSection listsLock;
		LogElapsedTime tt("P::CMI timestamp check", 5000);
		auto updater = [&](const FileInfo& fi) {
			const CStringW f(fi.mFilename);
			const int ftype = GetFileType(f);
			if (!(ShouldParseGlobals(ftype) || ftype == Src))
				return;

			if (StopIt)
				return;

			if (!IsFile(f))
			{
				// [case: 130956]
				// don't need to repeatedly try to parse files that don't exist
				return;
			}

			if (CS == ftype)
				info.mCheckNetDbLoaded = true;

			bool isSysFile = false;
			bool isSolSysFile = false;
			const UINT fileId = gFileIdManager->GetFileId(f);
			uint dbFlags = 0;
			DbTypeId vADbId = g_DBFiles.DBIdFromParams(ftype, fileId, false, false);
			DTypePtr fileDat;
			FILETIME* ft = g_DBFiles.GetFileTime(fileId, vADbId);
			if (!ft)
			{
				// not found - check to see if in sys db
				fileDat = MultiParse::GetFileData(f);
				if (fileDat)
				{
					dbFlags = fileDat->DbFlags();
					if (dbFlags & VA_DB_SolutionPrivateSystem)
					{
						_ASSERTE(dbFlags & VA_DB_Cpp);
						isSolSysFile = true;
					}
					else
						dbFlags = 0;
				}
				vADbId = g_DBFiles.DBIdFromParams(ftype, fileId, true, false, dbFlags);
				ft = g_DBFiles.GetFileTime(fileId, vADbId);
				if (ft)
					isSysFile = true;
				else if (IncludeDirs::IsSystemFile(f))
					isSysFile = true;
			}

			if (ft && !ft->dwHighDateTime && !ft->dwLowDateTime)
				ft = NULL;

			const bool kWasIncluded = MultiParse::IsIncluded(f);
			if (!kWasIncluded && ft && FileTimesAreEqual(ft, f))
			{
				// [case: 59409] previously parsed but not currently loaded
				AutoLockCs l(listsLock);
				if (isSolSysFile)
					info.solSysFilesThatNeedLoad.push_back(f);
				else if (isSysFile)
					info.sysFilesThatNeedLoad.push_back(f);
				else
				{
					if (ShouldParseGlobals(ftype))
						info.projGlobalsThatNeedLoad.push_back(f);
					else if (ftype == Src)
						info.projSrcThatNeedLoad.push_back(f);
				}
			}
			else if (!kWasIncluded || !ft || !FileTimesAreEqual(ft, f))
			{
				if (kWasIncluded && ft)
				{
					// !FileTimesAreEqual
					// [case: 85937] check content hash
					if (!fileDat)
						fileDat = MultiParse::GetFileData(f);
					if (fileDat)
					{
						const WTString fileDef(fileDat->Def());
						StrVectorA defParts;
						WtStrSplitA(fileDef, defParts, ";");
						if (defParts.size() == 2)
						{
							WTString tmp;
							tmp.WTFormat("ContentSize:%lx;", GetFSize(f));
							if (tmp == defParts[0])
							{
								tmp = WTString("ContentHash:") + ::GetFileHashStr(f);
								if (tmp == defParts[1])
								{
									// different timestamp but same content; skip it
									return;
								}
							}
						}
					}
				}

				// stale or not previously parsed
				AutoLockCs l(listsLock);
				if (isSolSysFile)
					info.solSysFilesThatNeedUpdate.push_back(f);
				else if (isSysFile)
					info.sysFilesThatNeedUpdate.push_back(f);
				else
				{
					if (ShouldParseGlobals(ftype))
						info.projGlobalsThatNeedUpdate.push_back(f);
					else if (ftype == Src)
						info.projSrcThatNeedUpdate.push_back(f);
				}
			}
		};

		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(mProjectFileItemList.cbegin(), mProjectFileItemList.cend(), updater);
		else
			std::for_each(mProjectFileItemList.cbegin(), mProjectFileItemList.cend(), updater);
	}
}

void Project::UpdateModifiedFiles(ModifiedFilesInfo& info)
{
	_ASSERTE(DatabaseDirectoryLock::GetOwningThreadID() == ::GetCurrentThreadId());
	BuildFileCollectionIfReady();

	if (!info.mCollected)
		CollectModificationInfo(info);

	{
		// [case: 116689]
		LogElapsedTime tt("P::UMF remove missing files", 5000);
		if (!info.deletedProjFilesThatNeedRemoval.empty())
		{
			SymbolRemover sr(info.deletedProjFilesThatNeedRemoval);
			sr.DoParseWork();
		}
	}

	if (info.mCheckNetDbLoaded && (!g_pCSDic || !g_pCSDic->m_loaded))
	{
		// [case: 130956]
		// workaround for unreal engine has C# source files but CS dic not loaded?
		GetSysDic(CS);
	}

	bool reparseFlag;

	if (info.sysFilesThatNeedUpdate.size() || info.sysFilesThatNeedLoad.size() ||
	    info.solSysFilesThatNeedUpdate.size() || info.solSysFilesThatNeedLoad.size())
	{
		vLog("P::UMF sysUpdate(%zu) sysLoad(%zu)", info.sysFilesThatNeedUpdate.size(),
		     info.sysFilesThatNeedLoad.size());
		LogElapsedTime tt("P::UMF sys update", 5000);

		{
			TempSettingOverride<std::atomic_int, int> tmp(&mIsLoading, true, true);
			std::set<UINT> fileIds;

			// do removeAllDefs serially
			auto sysRemove = [&fileIds](const CStringW& f) {
				const UINT fileid = gFileIdManager->GetFileId(f);
				fileIds.insert(fileid);
			};

			std::for_each(info.sysFilesThatNeedUpdate.cbegin(), info.sysFilesThatNeedUpdate.cend(), sysRemove);
			std::for_each(info.solSysFilesThatNeedUpdate.cbegin(), info.solSysFilesThatNeedUpdate.cend(), sysRemove);

			MultiParsePtr mp = MultiParse::Create(Src);
			mp->RemoveAllDefs(fileIds, DTypeDbScope::dbSlnAndSys);
		}

		uint dbFlags = 0;
		auto sysFormat = [&reparseFlag, &dbFlags](const CStringW& f) {
			if (StopIt || !FileNeedsParse(f, TRUE))
				return;

			const int ftype = GetFileType(f);
			MultiParsePtr mp = MultiParse::Create();
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				if (ShouldParseGlobals(ftype))
					mp->FormatFile(f, V_SYSLIB, ParseType_Globals, reparseFlag, nullptr, dbFlags);
				else if (ftype == Src)
					mp->FormatFile(f, V_SYSLIB, ParseType_GotoDefs, reparseFlag, nullptr, dbFlags);
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("PR::UMF1:");
				if (!Psettings->m_catchAll)
				{
					_ASSERTE(!"Fix the bad code that caused this exception in Project::UpdateModifiedFiles1");
				}
			}
#endif // !SEAN
		};

		if (StopIt)
			return;

		reparseFlag = false;
		dbFlags = 0;
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(info.sysFilesThatNeedLoad.cbegin(), info.sysFilesThatNeedLoad.cend(),
			                               sysFormat);
		else
			std::for_each(info.sysFilesThatNeedLoad.cbegin(), info.sysFilesThatNeedLoad.cend(), sysFormat);
		dbFlags = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(info.solSysFilesThatNeedLoad.cbegin(), info.solSysFilesThatNeedLoad.cend(),
			                               sysFormat);
		else
			std::for_each(info.solSysFilesThatNeedLoad.cbegin(), info.solSysFilesThatNeedLoad.cend(), sysFormat);
		if (StopIt)
			return;

		reparseFlag = true;
		dbFlags = 0;
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(info.sysFilesThatNeedUpdate.cbegin(), info.sysFilesThatNeedUpdate.cend(),
			                               sysFormat);
		else
			std::for_each(info.sysFilesThatNeedUpdate.cbegin(), info.sysFilesThatNeedUpdate.cend(), sysFormat);
		dbFlags = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(info.solSysFilesThatNeedUpdate.cbegin(),
			                               info.solSysFilesThatNeedUpdate.cend(), sysFormat);
		else
			std::for_each(info.solSysFilesThatNeedUpdate.cbegin(), info.solSysFilesThatNeedUpdate.cend(), sysFormat);
	}

	if (StopIt)
		return;

	if (!info.projGlobalsThatNeedUpdate.size() && !info.projSrcThatNeedUpdate.size() &&
	    !info.projGlobalsThatNeedLoad.size() && !info.projSrcThatNeedLoad.size())
		return;

	vLog("P::UMF glUpdate(%zu) glLoad(%zu) srcUpdate(%zu) srcLoad(%zu)", info.projGlobalsThatNeedUpdate.size(),
	     info.projGlobalsThatNeedLoad.size(), info.projSrcThatNeedUpdate.size(), info.projSrcThatNeedLoad.size());
	TempSettingOverride<std::atomic_int, int> tmp(&mIsLoading, true);

	{
		LogElapsedTime tt("P::UMF sol rm", 5000);
		std::set<UINT> fileIds;

		// do removeAllDefs serially
		std::for_each(info.projGlobalsThatNeedUpdate.cbegin(), info.projGlobalsThatNeedUpdate.cend(),
		              [&fileIds](const CStringW& f) {
			              const UINT fileid = gFileIdManager->GetFileId(f);
			              fileIds.insert(fileid);
		              });

		std::for_each(info.projSrcThatNeedUpdate.cbegin(), info.projSrcThatNeedUpdate.cend(),
		              [&fileIds](const CStringW& f) {
			              const UINT fileid = gFileIdManager->GetFileId(f);
			              fileIds.insert(fileid);
		              });

		MultiParsePtr mp = MultiParse::Create(CS);
		mp->RemoveAllDefs(fileIds, DTypeDbScope::dbSolution);
	}

	if (StopIt)
		return;

	auto globalFormat = [&reparseFlag](const CStringW& f) {
		if (StopIt)
			return;
		// [case: 59171] recheck in case already updated via dependency of another file
		if (!FileNeedsParse(f, FALSE))
			return;

		MultiParsePtr mp = MultiParse::Create();
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			mp->FormatFile(f, V_INPROJECT, ParseType_Globals, reparseFlag);
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("PR::UMF2:");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in Project::UpdateModifiedFiles2");
			}
		}
#endif // !SEAN
	};

	if (StopIt)
		return;
	reparseFlag = false;
	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(info.projGlobalsThatNeedLoad.cbegin(), info.projGlobalsThatNeedLoad.cend(),
		                               globalFormat);
	else
		std::for_each(info.projGlobalsThatNeedLoad.cbegin(), info.projGlobalsThatNeedLoad.cend(), globalFormat);
	if (StopIt)
		return;

	reparseFlag = true;
	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(info.projGlobalsThatNeedUpdate.cbegin(), info.projGlobalsThatNeedUpdate.cend(),
		                               globalFormat);
	else
		std::for_each(info.projGlobalsThatNeedUpdate.cbegin(), info.projGlobalsThatNeedUpdate.cend(), globalFormat);

	auto finddefFormat = [&reparseFlag](const CStringW& f) {
		if (StopIt)
			return;
		// [case: 59171] recheck in case already updated via dependency of another file
		if (!FileNeedsParse(f, FALSE))
			return;

		MultiParsePtr mp = MultiParse::Create();
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			mp->FormatFile(f, V_INPROJECT, ParseType_GotoDefs, reparseFlag);
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("PR::UMF3:");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in Project::UpdateModifiedFiles3");
			}
		}
#endif // !SEAN
	};

	if (StopIt)
		return;
	reparseFlag = false;
	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(info.projSrcThatNeedLoad.cbegin(), info.projSrcThatNeedLoad.cend(),
		                               finddefFormat);
	else
		std::for_each(info.projSrcThatNeedLoad.cbegin(), info.projSrcThatNeedLoad.cend(), finddefFormat);
	if (StopIt)
		return;

	reparseFlag = true;
	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(info.projSrcThatNeedUpdate.cbegin(), info.projSrcThatNeedUpdate.cend(),
		                               finddefFormat);
	else
		std::for_each(info.projSrcThatNeedUpdate.cbegin(), info.projSrcThatNeedUpdate.cend(), finddefFormat);

	if (gFileIdManager)
		gFileIdManager->QueueSave();
}

void Project::FindBaseFileNameMatches(const CStringW& filepathToIsolateToProject, const CStringW& basenameNoExt,
                                      FileList& matches)
{
	// [case: 32543]
	{
		// see if we can isolate to a single project
		ProjectInfoPtr ownerPrj;
		RWLockReader lck2(mMapLock);
		for (ProjectMap::const_iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			ProjectInfoPtr prjInf = (*iter).second;
			if (prjInf)
			{
				const CStringW& prjFile = prjInf->GetProjectFile();
				if (prjFile != Vsproject::kExternalFilesProjectName)
				{
					if (prjInf->ContainsFile(filepathToIsolateToProject))
					{
						if (ownerPrj)
						{
							ownerPrj.reset();
							break;
						}

						ownerPrj = prjInf;
					}
				}
			}
		}

		if (ownerPrj)
		{
			const FileList& flist = ownerPrj->GetFiles();
			for (FileList::const_iterator it = flist.begin(); it != flist.end(); ++it)
			{
				const CStringW cur((*it).mFilenameLower);
				CStringW curBase(::GetBaseNameNoExt(cur));
				if (!curBase.CompareNoCase(basenameNoExt))
					matches.AddUnique(*it);
			}

			_ASSERTE(matches.size());
			matches.sort(::LessThanByBasename); // [case: 52096] use same sort as main file list
			return;
		}
	}

	// wasn't able to isolate to a single project, use whole solution list
	FindBaseFileNameMatches(basenameNoExt, matches, true);
}

void Project::FindBaseFileNameMatches(const CStringW& basenameNoExt, FileList& matches, bool matchLengths)
{
	BuildFileCollectionIfReady();
	RWLockReader lck(mFilelistLock);
	const int matchLen = basenameNoExt.GetLength();
	for (FileList::const_iterator it = mProjectFileItemList.begin(); it != mProjectFileItemList.end(); ++it)
	{
		const CStringW cur((*it).mFilenameLower);
		CStringW curBase(::GetBaseNameNoExt(cur));
		if (matchLengths && !curBase.CompareNoCase(basenameNoExt))
			matches.AddUnique(*it);
		else if (!matchLengths && !::_wcsnicmp(basenameNoExt, curBase, (uint)matchLen))
			matches.AddUnique(*it);
	}
}

void Project::WrapperMissing()
{
#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO) && !defined(RAD_STUDIO_LANGUAGE)
	mWrapperCheckOk = false;
#ifndef NOSMARTFLOW
	SmartFlowPhoneHome::FireArmadilloMissing();
#endif
#endif
}

bool Project::ContainsNonExternal(const CStringW& filename) const
{
	RWLockReader lck2(mMapLock);
	for (ProjectMap::const_iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
	{
		ProjectInfoPtr prjInf = (*iter).second;
		if (prjInf)
		{
			const CStringW& prjFile = prjInf->GetProjectFile();
			if (prjFile != Vsproject::kExternalFilesProjectName)
			{
				if (prjInf->ContainsFile(filename))
					return true;
			}
		}
	}

	return false;
}

// This happens on the UI thread
bool Project::CheckForNewSolution() const
{
	bool res = ::CheckForNewSolution(gDte, mSolutionFile, mRawProjectList);
	if (!res && !mCleanLoad)
	{
		// watch out for aborted loads
		return true;
	}
	return res;
}

void Project::DumpToLog()
{
#ifdef _DEBUG
	const CStringW cacheFile(GetProjDbDir() + L"sln.va");
	CFileW dbgFile;
	if (dbgFile.Open(cacheFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
	{
		CStringW tmp;
		tmp = SolutionFile() + L"\r\n";
		dbgFile.Write(tmp, tmp.GetLength() * sizeof(WCHAR));

		CString__FormatW(tmp, L"%x\r\n", mProjectHash);
		dbgFile.Write(tmp, tmp.GetLength() * sizeof(WCHAR));

		tmp = mRawProjectList + L"\r\n";
		dbgFile.Write(tmp, tmp.GetLength() * sizeof(WCHAR));

		RWLockReader lck(mMapLock);
		ProjectMap::const_iterator iter;
		for (iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			const ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			tmp = prjInf->GetProjectFile() + (prjInf->IsDeferredProject() ? L" (deferred)\r\n" : L"\r\n");
			dbgFile.Write(tmp, tmp.GetLength() * sizeof(WCHAR));
		}

		dbgFile.Close();
	}
#endif

	if (!g_loggingEnabled)
		return;

	IncludeDirs incDirs;
	incDirs.DumpToLog();

	size_t incDirCount = 0;
	size_t intermedDirCount = 0;
	size_t prjDirCount = 0;
	{
		size_t fileCount = 0;
		RWLockReader lck(mMapLock);
		vCatLog("Environment.DumpToLog", "Solution info: %s\n", (LPCTSTR)CString(SolutionFile()));
		ProjectMap::const_iterator iter;
		for (iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			const ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			fileCount += prjInf->GetFiles().size();
			incDirCount += prjInf->GetIncludeDirs().size();
			intermedDirCount += prjInf->GetIntermediateDirs().size();
			prjDirCount += prjInf->GetProjectDirs().size();
			vCatLog("Environment.DumpToLog", "  %s  f(%zu) inc(%zu) int(%zu) pd(%zu) clr(%d) wrt(%d) d(%d) s(%d)\n",
			     (LPCTSTR)CString(prjInf->GetProjectFile()), prjInf->GetFiles().size(), prjInf->GetIncludeDirs().size(),
			     prjInf->GetIntermediateDirs().size(), prjInf->GetProjectDirs().size(), prjInf->CppUsesClr(),
			     prjInf->CppUsesWinRT(), prjInf->IsDeferredProject(), prjInf->IsPseudoProject());
		}

		vCatLog("Environment.DumpToLog", " projects (%u), files (%u)\n", (uint)mMap.size(), (uint)fileCount);
	}

	vCatLog("Environment.DumpToLog", "Solution dirs (incs %u, proj %u, intermed %u):\n", (uint)incDirCount, (uint)prjDirCount,
	     (uint)intermedDirCount);
	TokenW t = GetProjAdditionalDirs();
	while (t.more())
	{
		const CStringW curDir(t.read(L";"));
		if (!curDir.IsEmpty())
			vCatLog("Environment.DumpToLog", "  %s\n", (LPCTSTR)CString(curDir));
	}

	vCatLog("Environment.DumpToLog", "FD cpp(%p, %d, %ld) sys(%p, %d) clr(%d) wrt(%d) net(%d)", g_pMFCDic, g_pMFCDic ? g_pMFCDic->m_loaded : 0,
	     sMfcLoading, g_pCSDic, g_pCSDic ? g_pCSDic->m_loaded : 0, CppUsesClr(), CppUsesWinRT(), ShouldLoadNetDb());
}

bool Project::IsOkToIterateFiles() const
{
	if (m_loadingProjectFiles)
		return false;
	if (!sOkToLoadProjFiles)
		return false;
	if (::IsAnySysDicLoaded(true))
		return true;

	// [case: 61291] don't require sysdic if not loading
	if (mIsLoading)
		return false; // only check mIsLoading if !sysDicLoaded

	return true;
}

void Project::SolutionLoadStarting()
{
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
	if (!Psettings || !Psettings->m_enableVA)
		return;

	mIsLoading = m_loadingProjectFiles = true;
	sOkToLoadProjFiles = false;
}

void Project::SolutionLoadCompleted(bool forceLoad, bool forceSync /*= false*/)
{
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
	Log("start SLC LoadSolution");
	if (!Psettings || !Psettings->m_enableVA)
	{
		if (Psettings && !Psettings->m_enableVA)
			CloseProject();
		return;
	}

	bool cleanUp = false;
	if (forceLoad || CheckForNewSolution())
	{
		if (forceLoad && !forceSync)
		{
			// [case: 105291]
			const int deferredProjects = ::GetSolutionDeferredProjectCount();
			const bool openFolderMode = ::IsInOpenFolderMode();
			if (deferredProjects || openFolderMode)
			{
				if (gSqi.RequestAsyncWorkspaceCollection(true))
					return;
			}

			gSqi.Reset();
		}

		LaunchProjectLoaderThread(CStringW());
	}
	else if (GlobalProject && !GlobalProject->IsBusy())
		cleanUp = true;
	else
	{
		ProjectLoaderRef pl;
		if (!pl || pl->IsFinished())
			cleanUp = true;
	}

	if (cleanUp)
	{
		Log("no LoadSolution");
		// undo what SolutionLoadStarting did
		if (!sOkToLoadProjFiles)
			sOkToLoadProjFiles = true;
		if (StopIt && !gShellIsUnloading)
			StopIt = false;
		if (mIsLoading)
			mIsLoading = false;
		if (m_loadingProjectFiles)
			m_loadingProjectFiles = false;
	}
}

void Project::SolutionClosing()
{
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv10OrHigher());
	if (!Psettings || !Psettings->m_enableVA)
		return;

	StopIt = true;
	mIsLoading = true;
	m_loadingProjectFiles = true;
	sOkToLoadProjFiles = false;
}

std::map<WTString, WTString> Project::ParseCustomGlobalSection(const WTString sectionName)
{
	std::map<WTString, WTString> globalSectionParameters;
	CStringW solutionFile = SolutionFile();

	if (!solutionFile.IsEmpty())
	{
		WTString slnFileContent;
		slnFileContent.ReadFile(solutionFile);
		if (!slnFileContent.IsEmpty())
		{
			// check if GlobalSection(*globalSectionParameters*) exists; be error proof for possible white spaces around
			// parenthesis
			WTString rgx = "GlobalSection\\s*\\(\\s*" + sectionName + "\\s*\\)";
			int sectionLocation = slnFileContent.FindRENoCase(rgx.c_str());
			if (sectionLocation > -1)
			{
				int sectionEnd = slnFileContent.FindNoCase("EndGlobalSection", sectionLocation);
				if (sectionEnd > sectionLocation)
				{
					// go through global section and create a map of parameters
					int tmpIndex = slnFileContent.find("\n", sectionLocation);
					while (tmpIndex > -1 && tmpIndex < sectionEnd)
					{
						int nextNewLine = slnFileContent.find("\n", tmpIndex + 1);
						int equalLoc = slnFileContent.find("=", tmpIndex);
						if (equalLoc < tmpIndex || equalLoc > nextNewLine)
							break;

						WTString parameterName = slnFileContent.substr(tmpIndex, equalLoc - tmpIndex);
						WTString parameterValue = slnFileContent.substr(equalLoc + 1, nextNewLine - equalLoc);
						parameterName.Trim();
						parameterValue.Trim();

						globalSectionParameters[parameterName] = parameterValue;

						tmpIndex = nextNewLine;
					}
				}
			}
		}
	}

	return globalSectionParameters;
}

LONG Project::GetFilesListGeneration() const
{
	// gmit: mFileListNeedsUpdate generally does not need a lock as we can be imprecise and return lesser generation
	// number
	__lock(mFileListGeneration_cs);
	return mFileListGeneration + mFileListNeedsUpdate;
}

void Project::SetSolutionFile(const CStringW& val)
{
	mSolutionFile = val;
	if (mSolutionFile.GetLength() > 3 && ::IsDir(mSolutionFile))
		mIsFolderBasedSln = true;
	else
		mIsFolderBasedSln = false;
}

ProjectVec Project::GetProjectForFile(const CStringW& file)
{
	if (gFileIdManager && !file.IsEmpty())
		return GetProjectForFile(gFileIdManager->GetFileId(file));

	ProjectVec v;
	return v;
}

ProjectVec Project::GetProjectForFile(UINT fileId)
{
	if (IsOkToIterateFiles() && fileId)
	{
		RWLockReader lck2(mMapLock);
		MapFilesToProjects::iterator it = mMapFilesToProjects.find(fileId);
		if (it == mMapFilesToProjects.end())
		{
			// need to set it up
			ProjectVec v;
			for (ProjectMap::iterator iter = mMap.begin(); iter != mMap.end(); ++iter)
			{
				ProjectInfoPtr prjInf = (*iter).second;
				_ASSERTE(prjInf); // how did this happen?
				if (prjInf && prjInf->GetProjectFile() != Vsproject::kExternalFilesProjectName &&
				    prjInf->ContainsFile(fileId))
				{
					if (prjInf->IsDeferredProject())
						vLogUnfiltered("warn: Project::GetProjectForFile found deferred\n");
					if (prjInf->IsPseudoProject())
						vLogUnfiltered("warn: Project::GetProjectForFile found pseudo\n");

					CStringW prjFile(prjInf->GetProjectFile());
					prjFile = GetBaseNameExt(prjFile);
					prjFile.MakeLower();
					if (prjFile == L"shproj" || prjFile == L"vcxitems")
					{
						// [case: 80212]
						// put shared project first
						v.push_front(prjInf);
					}
					else
						v.push_back(prjInf);
				}
			}

			mMapFilesToProjects[fileId] = v;
			it = mMapFilesToProjects.find(fileId);
		}

		if (it != mMapFilesToProjects.end())
		{
			ProjectVec projs = it->second;
			return projs;
		}
	}

	ProjectVec v;
	return v;
}

bool Project::IsFilePathInProject(const CStringW& filePath) const
{
	RWLockReader lck(mMapLock); // Lock the project map for thread safety

	CStringW filePathLower = filePath;
	filePathLower.MakeLower();

	for (const auto& project : mMap)
	{
		ProjectInfoPtr projectInfo = project.second;
		if (projectInfo)
		{
			// const FileList& projectFiles = projectInfo->GetFiles();
			const FileList& projectDirs = projectInfo->GetProjectDirs();
			for (const auto& file : projectDirs)
			{
				CStringW projectFilePath = file.mFilenameLower;
				if (filePathLower.Find(projectFilePath) != -1)
				{
					return true;
				}
			}
		}
	}

	return false;
}

CStringW Project::GetPCHFileName() const
{
	RWLockReader lck(mMapLock); // Lock the project map for thread safety
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return L"";

	CStringW filePathLower = ed->FileName();
	filePathLower.MakeLower();

	for (const auto& project : mMap)
	{
		ProjectInfoPtr projectInfo = project.second;
		if (projectInfo)
		{
			CStringW precompiledHeaderFile = projectInfo->GetPrecompiledHeaderFile();
			const FileList& projectFiles = projectInfo->GetFiles();
			for (const auto& file : projectFiles)
			{
				CStringW projectFilePath = file.mFilenameLower;
				if (filePathLower.Find(projectFilePath) != -1)
				{
					return precompiledHeaderFile;
				}
			}
		}
	}

	return L"";
}

void Project::FilesListNeedsUpdate()
{
	++mFileListNeedsUpdate;
	mFileListNeedsSort = true;

	// clear file to project cache
	RWLockWriter lck2(mMapLock);
	mMapFilesToProjects.clear();
}

bool Project::ParsePrivateSystemHeaders()
{
	bool retval = false;
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());

	TimeTrace tt("Handle solution private system includes");
	mFilelistLock.StartReading();
	ScanSolutionPrivateSystemHeaderDirs scn(mSolutionPrivateSystemIncludeDirs);
	mFilelistLock.StopReading();

	if (!gShellIsUnloading && Psettings->mParseNuGetRepository)
	{
		// by checking Psettings->mParseNuGetRepository we can populate OFIS
		// without necessarily incurring the parse hit of all the files listed.
		// mLookupNuGetRepository does file search.
		// mParseNuGetRepository is independent of mLookupNuGetRepository.
		TimeTrace tt1("Parse solution private system includes");
		TempSettingOverride<std::atomic_int, int> tmp(&mIsLoading, true);
		retval = scn.ParseFiles();
	}

	mFilelistLock.StartWriting();
	scn.GetFoundFiles().swap(mSolutionPrivateSystemHeaders);
	mFilelistLock.StopWriting();
	return retval;
}

void Project::SwapSolutionRepositoryIncludeDirs(FileList& incDirs)
{
	RWLockWriter lck(mFilelistLock);
	mSolutionPrivateSystemIncludeDirs.swap(incDirs);
}

int Project::GetFileItemCount()
{
	BuildFileCollectionIfReady();
	return (int)mProjectFileItemList.size();
}

bool Project::Contains(const CStringW& f)
{
	BuildFileCollectionIfReady();
	RWLockReader lck(mFilelistLock);
	return mProjectFileItemList.ContainsNoCase(f);
}

bool ProjectLoader::Stop()
{
	StopIt = true;
	Log("Killing ProjectLoader");
	/*
	 * Don't wait INFINITE
	 *  If the project loader is still running when you exit,
	 *    it could get blocked in SetStatus on UpdateWindow.
	 */
	// if we can't kill the last ProjectLoader thread after X seconds - let it go
	bool retval = false;
	if (WAIT_OBJECT_0 == Wait(10000))
	{
		retval = true;
		Log("ProjectLoader Stopped");
	}
	else
	{
		LogUnfiltered("WARN: ProjectLoader didn't stop within 10 seconds");
	}

	ClearGlobalIfThis();
	return retval;
}

void ProjectLoader::Run()
{
	remote_heap::stats::event_timing et("Project load");

	Log2("Start ProjectLoader::Run");
	bool loadNewProject = false;
	try
	{
		loadNewProject = GlobalProject->mRawProjectList.GetLength() > 0;
		GlobalProject->LoadProject(mVsSolInfo);

		// sort the big list after the dir lock has been released
		if (GlobalProject)
			GlobalProject->SortFileListIfReady();
	}
	catch (const UnloadingException&)
	{
		Log("PL::R-unloading:");
		g_DBFiles.SolutionLoadCompleted();
	}
	catch (const WtException& e)
	{
		WTString msg("PL:Run exception: ");
		msg += e.GetDesc();
		Log(msg.c_str());
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("PL::R:");
		ErrorBox(IDS_APPNAME ": Error loading project.\n\nPlease contact us at http://www.wholetomato.com/contact",
		         MB_OK | MB_ICONEXCLAMATION);
	}
#endif // !SEAN

	if (mVsSolInfo)
		mVsSolInfo.reset();

#if !defined(SEAN)
	if (gShellAttr->IsDevenv15())
	{
		// other versions of VS are started at #AutoStartAST
		// Launch AutoTest (for VS2017 due to Find in Files failures in 15.6 and 15.7)
		static bool once = true;
		if (once && loadNewProject)
		{
			CString cmdLine = ::GetCommandLine();
			cmdLine.MakeLower();
			if (cmdLine.Find("vaautomationtests") != -1)
			{
				if (cmdLine.Find("\\vaautomationtests\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_cppb\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_ci\\vaautomationtests_") != -1 ||
				    cmdLine.Find("\\vaautomationtests_ue\\vaautomationtests_") != -1)
				{
					::Sleep(10000);
					::RunFromMainThread<void, BOOL>(RunVAAutomationTest, TRUE);
				}
			}
			once = false;
		}
	}
#endif // !SEAN

	ClearGlobalIfThis();
	// match AddRef in ctor
	Release();
	Log2("End ProjectLoader::Run");
}

FileDic* GetSysDic()
{
	return GetSysDic(-1);
} // returns current system db;

FileDic* GetSysDic(int lang)
{
	DEFTIMERNOTE(GetSysDicTimer, "");

	static volatile int sLastSysDbLangRequest = -1;
	if (!s_pSysDic && StopIt && g_mainThread == GetCurrentThreadId())
		return NULL;

	while (!s_pSysDic && !gShellIsUnloading)
	{
#if defined(VA_CPPUNIT)
		Sleep(20);
#else
		Sleep(100);
#endif
	}

	if (!s_pSysDic && gShellIsUnloading)
		throw UnloadingException();

	_ASSERTE(s_pSysDic);

	// test against [case: 15392 and case: 10401]
	static volatile FileDic* pSysDic = NULL;
	static CSpinCriticalSection sSysDicLock;

	if (sLastSysDbLangRequest != lang && lang != -1 &&
	    DatabaseDirectoryLock::GetOwningThreadID() != ::GetCurrentThreadId())
	{
		// Calling GetSysDic from thread other than the one who has the lock.
		{
			AutoLockCs l(sSysDicLock);
			if (pSysDic)
				return const_cast<FileDic*>(pSysDic);
		}

		if (g_mainThread != GetCurrentThreadId() && DatabaseDirectoryLock::GetOwningThreadID())
		{
			_ASSERTE(!"Changing GetSysDic when someone else has the lock - what thread am i?"); // Do we hit here? -Jer
		}
		return s_pSysDic; // don't build db on UI thread
	}

	if (sLastSysDbLangRequest == -1 && lang == -1)
	{
		// Safety catch since we no longer default sLastSysDbLangRequest to Src
		// This happens if you open a file before opening a workspace in vc6.
		sLastSysDbLangRequest = Src;
	}

	if (IsCFile(lang) || lang == RC || lang == Idl)
	{
		if (g_pMFCDic && (g_pMFCDic->m_loaded || sMfcLoading))
		{
			AutoLockCs l(sSysDicLock);
			pSysDic = g_pMFCDic;
			sLastSysDbLangRequest = Src;
		}
		else
		{
#if !defined(VA_CPPUNIT)
			if (g_mainThread == GetCurrentThreadId())
			{
				vLog("WARN: GSD c sysDbLoad attempt on ui thread");
				return s_pSysDic; // don't build db on UI thread
			}
#endif

			vCatLog("Environment.Project", "GSD c/c++ sysDbLoad on thread %lx", GetCurrentThreadId());
			sLastSysDbLangRequest = Src;
			_ASSERTE(DatabaseDirectoryLock::GetOwningThreadID() == ::GetCurrentThreadId());
			sOkToLoadProjFiles = false; // get sys headers parsed first
			InterlockedIncrement(&sMfcLoading);
			const CStringW SysDBPath(VaDirs::GetDbDir() + kDbIdDir[DbTypeId_Cpp] + L"\\");
			CreateDir(SysDBPath);
			if (!g_pMFCDic)
				g_pMFCDic = new FileDic(SysDBPath, s_pSysDic->GetHashTable());

			{
				AutoLockCs l(sSysDicLock);
				pSysDic = g_pMFCDic;
			}

			TimeTrace tr("Load system symbols (1)");
			SetStatus("Loading system symbols...");
			LoadIdxesInDir(SysDBPath, Src, VA_DB_Cpp);
			MultiParsePtr mp = MultiParse::Create();
			CStringW stdafx = VaDirs::GetParserSeedFilepath(
			    L"stdafx.h"); // read this one first so it can override anything in stdafxva
			if (!mp->IsIncluded(stdafx))
			{
				// append internally used helpers; don't want to have them publically visible
				// related to InferType.cpp, handle_std_get
				// todo: in the future, if this is needed from more places, think of something better
				// note: db needs to be rebuilt (which will be done after installing new VA anyway)
				WTString stdafx_contents = ReadFile(stdafx);
				stdafx_contents += R"(
// helpers for handle_std_get
template<typename TYPE> TYPE __va_passthrough_type(TYPE t);
template<typename TYPE> TYPE __va_passthrough_type2();
using __va_void = void;

// helpers for handling pointer types
class __va_ptr_const {};
class __va_ptr_volatile {};
class __va_ptr_const_volatile {};
class __va_ptr_none {};
class __va_ptr_end {};
template <typename TYPE, typename CV> class __va_ptr {};
template <typename TYPE, typename END = __va_ptr_end> class __va_ptr_begin {};
)";
				mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals, false, stdafx_contents.c_str());
			}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			// check for C++ Builder hints
			stdafx = VaDirs::GetParserSeedFilepath(L"stdafx_cb.h");
			if (!mp->IsIncluded(stdafx))
				mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
#endif

			if (Psettings->mUnrealEngineCppSupport)
			{
				// [case: 118578] [ue4] load stdafxunreal.h before parse of system source
				// UE4 specific defs
				stdafx = VaDirs::GetParserSeedFilepath(L"StdafxUnreal.h");
				if (IsFile(stdafx) && !mp->IsIncluded(stdafx))
					mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);

				// [case: 149728] [ue5] load stdafxunreal50.h before parse of system source
				// UE5.0 specific defs; applies only on 5.0 and not other UE5 versions
				if (GlobalProject->GetUnrealEngineVersion() == L"5.0")
				{
					stdafx = VaDirs::GetParserSeedFilepath(L"StdafxUnreal50.h");
					if (IsFile(stdafx) && !mp->IsIncluded(stdafx))
						mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
				}
			}

#if !defined(VA_CPPUNIT)
			// stdafxva.h is the list of headers used to pre-populate the sys db
			stdafx = VaDirs::GetParserSeedFilepath(L"stdafxva.h");
			if (!mp->IsIncluded(stdafx) && !gShellIsUnloading)
			{
#if !defined(AVR_STUDIO)
				if (gShellAttr->IsDevenv() && GlobalProject->ShouldLoadNetDb())
				{
					// preload assemblies pulled in by vcclr.h before doing sys headers and source
					TimeTrace tr2("Load system symbols (2)");
					SetStatus("Loading system symbols...");

					const WCHAR* const kAsmBases[] = {L"mscorlib.dll", L"system.dll", L"System.Windows.Forms.dll",
					                                  L"System.Drawing.dll", NULL};

					std::vector<CStringW> asms;
					CStringW file;

					for (int idx = 0; kAsmBases[idx]; ++idx)
					{
						file = kAsmBases[idx];
						file = gFileFinder->ResolveReference(file, CStringW());
						if (!file.IsEmpty())
							asms.push_back(file);
					}

					if (asms.size())
					{
						GetSysDic(CS);
						auto imprtr = [](const CStringW& fi) {
							if (gShellIsUnloading)
								return;

							LogElapsedTime tt(CString(fi), 700);
							NetImportDll(fi);
						};

						if (Psettings->mUsePpl)
							Concurrency::parallel_for_each(asms.cbegin(), asms.cend(), imprtr);
						else
							std::for_each(asms.cbegin(), asms.cend(), imprtr);
						GetSysDic(Src);
					}
				}
#endif // !AVR_STUDIO

				InitSysHeaders(mp, stdafx);
				sOkToLoadProjFiles = true;
				if (GlobalProject)
					GlobalProject->BuildFileCollectionIfReady(); // don't need to wait for sys src scan for OFIS
				if (!g_CodeGraphWithOutVA)
				{
					TimeTrace tt("Parse system source files");
					ScanSystemSourceDirs scn;
				}

				if (s_pSysDic && !g_pDBLock->ActiveReadersOrWriters())
					s_pSysDic->ReleaseTransientData();
			}
#endif // !VA_CPPUNIT

			sOkToLoadProjFiles = true; // [case: 58850] not reset if sys db already built and solution is empty

			if (Psettings->mUnrealScriptSupport)
			{
				// UnrealScript specific defs
				stdafx = VaDirs::GetParserSeedFilepath(L"UnReal.uc");
				if (IsFile(stdafx) && !mp->IsIncluded(stdafx))
					mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
			}

			g_pMFCDic->LoadComplete();
			AddSpecialCppMacros();
			InterlockedDecrement(&sMfcLoading);
		}

		return g_pMFCDic;
	}
	else if (Defaults_to_Net_Symbols(lang))
	{
		static volatile LONG sCsLoading = 0;
		if (g_pCSDic && (g_pCSDic->m_loaded || sCsLoading))
		{
			AutoLockCs l(sSysDicLock);
			pSysDic = g_pCSDic;
			sLastSysDbLangRequest = lang;
		}
		else
		{
#if !defined(VA_CPPUNIT)
			if (g_mainThread == GetCurrentThreadId())
			{
				vLog("WARN: GSD net sysDbLoad attempt on ui thread");
				return s_pSysDic; // don't build db on UI thread
			}
#endif

			vLog("GSD net sysDbLoad on thread %lx", GetCurrentThreadId());
			sLastSysDbLangRequest = lang;
			_ASSERTE(DatabaseDirectoryLock::GetOwningThreadID() == ::GetCurrentThreadId());
			InterlockedIncrement(&sCsLoading);
			const CStringW SysDBPath(VaDirs::GetDbDir() + kDbIdDir[DbTypeId_Net] + L"\\");
			CreateDir(SysDBPath);
			if (!g_pCSDic)
				g_pCSDic = new FileDic(SysDBPath, s_pSysDic->GetHashTable());

			{
				AutoLockCs l(sSysDicLock);
				pSysDic = g_pCSDic;
			}

			TimeTrace tr("Load system symbols (3)");
			SetStatus("Loading system symbols...");
			if (!gShellIsUnloading)
				LoadIdxesInDir(SysDBPath, lang, VA_DB_Net);
			g_pCSDic->LoadComplete();
			AddSpecialCsMacros();
			InterlockedDecrement(&sCsLoading);
		}

		return g_pCSDic;
	}
	else if (-1 == lang)
	{
		AutoLockCs l(sSysDicLock);
		if (pSysDic && pSysDic != g_pMFCDic && pSysDic != g_pCSDic)
			pSysDic = NULL;

		if (!pSysDic)
			pSysDic = s_pSysDic;

		return const_cast<FileDic*>(pSysDic);
	}
	else
	{
		_ASSERTE(lang == Tmp || lang == Binary || lang == Image || lang == Other || lang == Plain || lang == SQL ||
		         lang == PERL || lang == XML || lang == HTML || lang == Java);
		if (lang > 0)
			sLastSysDbLangRequest = lang;

		return g_pEmptyDic;
	}
}

bool IsAnySysDicLoaded(bool inStateOfLoadingIsOk /*= false*/)
{
	if (!g_pMFCDic && !g_pCSDic)
		return false;

	if (g_pMFCDic)
	{
		if (g_pMFCDic->m_loaded)
			return true;
		if (inStateOfLoadingIsOk && sMfcLoading)
			return true;
	}

	if (g_pCSDic && g_pCSDic->m_loaded)
		return true;

	return false;
}

void BackgroundInit(LPVOID)
{
	gBackgroundInitThreadState = PooledThreadBase::ThreadState::ts_Running;

#if !defined(VA_CPPUNIT)

	extern UINT WatchDirLoop(LPVOID dir);
	// don't need to use ThreadPool for this since it does not go away until exit
	AfxBeginThread(WatchDirLoop, NULL);

	_ASSERTE(gAutotextMgr);
	const bool autoTxtLoaded = gAutotextMgr->IsLoaded();
	gAutotextMgr->InstallDefaultItems();
	if (!autoTxtLoaded)
	{
		int ftype = gTypingDevLang != -1 ? gTypingDevLang : Src;
		gAutotextMgr->Load(ftype);
	}

	_ASSERTE(gVaLicensingHost);
	if (gVaLicensingHost)
		gVaLicensingHost->UpdateLicenseStatus();
	LoadBcgBitmap();

#endif

	// static init as bg task
	if (gShellAttr)
	{
		gShellAttr->IsDevenv14u2OrHigher();
		gShellAttr->IsDevenv14u3OrHigher();
		gShellAttr->IsDevenv15u3OrHigher();
		gShellAttr->IsDevenv15u6OrHigher();
		gShellAttr->IsDevenv15u7OrHigher();
		gShellAttr->IsDevenv15u8OrHigher();
		gShellAttr->IsDevenv15u9OrHigher();
	}

	gBackgroundInitThreadState = PooledThreadBase::ThreadState::ts_Finished;
}

// [case: 116020]
std::unique_ptr<VsUI::GdiplusImage> sKeepGdiPlusLoaded;

void AllocTheGlobals()
{
	LOG("ATG");
	DEFTIMERNOTE(ATGTimer, NULL);
	StopIt = false;
	gShellIsUnloading = FALSE;
	sPendingLoads = 0;

	CString tmp;
#if defined(_DEBUG) || defined(RAD_STUDIO) || defined(NO_ARMADILLO) || defined(AVR_STUDIO)
	tmp = DEBUG_SECURITY_STRING;
#else
	tmp = gVaLicensingHost->GetArmString(SECURITY_STRING_NAME);
#endif
	gArmadilloSecurityStringValue = WTHashKey(to_string_view(tmp));
#ifdef DEBUG_WRAP_CHECK
	vLog("WCES: '%s' %x", tmp, gArmadilloSecurityStringValue);
#endif
	IsOnlyInstance(); // first call will init lock
	CheckHeapLookaside();

	LPVOID foo = NULL;
	OleInitialize(foo);

	if (!Psettings)
	{
		CatLog("Environment.Project", "ATG Ps");
		Psettings = new CSettings;
	}

	extern void init_rheap();
	init_rheap();

	concurrency::parallel_invoke(
	    [] {
		    new ParserThread; // self deletes
		    InitFileIdManager();
	    },
	    [] {
		    if (!g_IdeSettings)
		    {
			    CatLog("Environment.Project", "ATG GIS");
			    g_IdeSettings = new IdeSettings;
		    }

		    if (!g_FontSettings)
		    { // after Psettings...
			    CatLog("Environment.Project", "ATG FS");
			    g_FontSettings = new FontSettings;
		    }

#if defined(RAD_STUDIO) && !defined(VA_CPPUNIT)
		    kIdeInstallDir = GetRegValueW(gShellAttr->GetSetupRootKey(), gShellAttr->GetSetupKeyName(), "RootDir");
#else
		    if (gShellAttr && gShellAttr->IsDevenv())
			    kIdeInstallDir =
			        GetRegValueW(gShellAttr->GetSetupRootKey(), gShellAttr->GetSetupKeyName() + "VC", "ProductDir");
#endif

		    WCHAR path[MAX_PATH + 1] = L"";
		    if (SHGetSpecialFolderPathW(NULL, path, CSIDL_PROGRAM_FILES, FALSE))
		    {
			    kWinProgFilesDir = path;
			    kMsSdksInstallDir = kWindowsKitsInstallDir = path;
			    kMsSdksInstallDir += L"\\Microsoft SDKs\\";
			    kWindowsKitsInstallDir += L"\\Windows Kits\\";
		    }

		    path[0] = L'\0';
		    if (SHGetSpecialFolderPathW(NULL, path, CSIDL_PROGRAM_FILESX86, FALSE))
		    {
			    kWinProgFilesDirX86 = path;
			    if (kWinProgFilesDirX86 == kWinProgFilesDir)
			    {
				    // [case: 108561] 32-bit process on 64-bit OS
				    if (-1 == kWinProgFilesDir.Find(L" (x86)"))
				    {
					    vLog("WARN: no (x86) text in program files: %s", (LPCTSTR)CString(kWinProgFilesDir));
					    kWinProgFilesDir.Empty();
				    }
				    else
				    {
					    // create the 64-bit version by removing (x86) from the 32-bit version
					    kWinProgFilesDir.Replace(L" (x86)", L"");
				    }
			    }

			    kMsSdksInstallDirX86 = kWindowsKitsInstallDirX86 = path;
			    kMsSdksInstallDirX86 += L"\\Microsoft SDKs\\";
			    kWindowsKitsInstallDirX86 += L"\\Windows Kits\\";
		    }
	    });

	std::vector<concurrency::task<void>> tasks = {
	    concurrency::create_task([] { g_rbuffer.Init(); }),
	    concurrency::create_task([] {
		    if (!g_pDBLock)
			    g_pDBLock = new RWLock;
		    if (!g_pUsage)
			    g_pUsage = new FeatureUsage;
		    if (!g_ReservedWords)
		    {
			    CatLog("Environment.Project", "ATG gRW");
			    g_ReservedWords = new TResWords;
		    }

		    // [case: 92495] pre-populate global keyword mps to reduce heap fragmentation
		    GetDFileMP(Src);
		    GetDFileMP(CS);
		    GetDFileMP(XAML);
	    }),
	    concurrency::create_task([] {
		    // project defined globals, classes and members ??k depends on project sz
		    if (!g_pGlobDic)
		    {
			    // cleanup previous tmp files
			    CleanDir(VaDirs::GetDbDir() + L"Proj_0", L"*.*");
			    CleanDir(VaDirs::GetDbDir() + L"cache/", L"*.prt");
			    const CStringW tmpDbDir = VaDirs::GetDbDir() + L"Proj_0\\";
			    CreateDir(tmpDbDir);
			    vCatLog("Environment.Project", "ATG gGD %s", (LPCTSTR)CString(tmpDbDir));
			    g_pGlobDic = new FileDic(tmpDbDir, FALSE, 210000);
			    g_pGlobDic->LoadComplete();
		    }
	    }),
	    concurrency::create_task([] {
		    InitFileFinder();
		    if (!GlobalProject)
		    {
			    CatLog("Environment.Project", "ATG GP");
			    GlobalProject = new Project;
		    }

		    if (!g_VA_MRUs)
		    {
			    CatLog("Environment.Project", "ATG g_VA_MRUs");
			    g_VA_MRUs = new VA_MRUs;
		    }

		    if (!gVaService)
			    gVaService = new VaService;

		    if (!gHashtagManager)
			    gHashtagManager = std::make_shared<HashtagsManager>();

		    // [case: 60759] init statics used in these functions
		    ShouldIgnoreFile("", false);
		    GetFrameworkDirs();
	    }),
	    concurrency::create_task([] { AutotextManager::CreateAutotextManager(); }),
	    concurrency::create_task([] {
		    if (!s_pSysDic)
		    {
			    CatLog("Environment.Project", "ATG VaSharedSys");
			    s_pSysDic = new FileDic(VaDirs::GetDbDir() + L"VaSharedSys", TRUE, 200000);
			    s_pSysDic->LoadComplete();
		    }
	    }),
	    concurrency::create_task([] {
		    if (!g_pEmptyDic)
		    {
			    CatLog("Environment.Project", "ATG Empty");
			    g_pEmptyDic = new FileDic(VaDirs::GetDbDir() + L"Empty", TRUE, 1);
			    g_pEmptyDic->LoadComplete();
		    }

		    if (gShellAttr->IsDevenv11OrHigher())
			    sKeepGdiPlusLoaded = std::make_unique<VsUI::GdiplusImage>();
	    })};

	auto joinTask = concurrency::when_all(std::begin(tasks), std::end(tasks));

	// things that should happen on the ui thread
	if (!gImgListMgr)
		gImgListMgr = new ImageListManager;

	if (!g_CompletionSet)
		g_CompletionSet = g_CompletionSetEx = new VACompletionSetEx();

	BCGInit(TRUE);

	// setup dirs - so that if user opens options dlg before
	// opening a file or workspace, the fields aren't empty
	IncludeDirs initDirs;

	if (!g_statBar)
	{
		CatLog("Environment.Project", "ATG gsB");
		g_statBar = new StatusWnd;
	}

	VATreeSetup(true);

	// wait for the concurrent tasks to finish before starting the background init
	joinTask.wait();

	gBackgroundInitThreadState = PooledThreadBase::ThreadState::ts_Constructed;
	new FunctionThread(BackgroundInit, nullptr, true);
}

void FreeTheGlobals()
{
	LOG("FTG");

	// in vs2012 (and probably vs2010 also), this is called after the main
	// window is gone
	gShellIsUnloading = TRUE;
	// memory allocation problems calling these in DLL_DETACH_PROCESS
	//  or allowing them to self-destruct as global objects instead of
	//  global pointers to objects

	// Warning shot for all threads...
	StopIt = true;
	++sPendingLoads;

	// [case: 100997]
	CTipOfTheDay::CloseTipOfTheDay();

#if !defined(RAD_STUDIO)
	if (MainWndH && IsWindow(MainWndH))
		::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
#endif

	for (int n = 5; n; n--)
	{
		VAProcessMessages();
#if defined(VA_CPPUNIT)
		Sleep(20);
#else
		Sleep(100);
#endif // VA_CPPUNIT
	}

	if (gAutoReferenceHighlighter)
	{
		CatLog("Environment.Project", "FTG gARH");
		auto tmp = gAutoReferenceHighlighter;
		gAutoReferenceHighlighter = nullptr;
		delete tmp;
	}

	g_References = nullptr;
	extern FindReferencesPtr s_refs;
	s_refs = nullptr;

	for (int n = 0; g_threadCount || g_ParserThread ||
	                gBackgroundInitThreadState != PooledThreadBase::ThreadState::ts_Finished ||
	                ArePooledThreadsRunning();)
	{
		Sleep(250);
		if (n++ > 80)
		{
			vLogUnfiltered("WARN: FTG someone is still busy at exit: tcnt(%ld) pt(%p) bginit(%d)", g_threadCount, g_ParserThread,
			     gBackgroundInitThreadState);
			break; // take our chances after 20 seconds
		}
	}

	extern std::atomic<int> gSolutionInfoActiveGathering;
	bool solInfoActive = false;
	if (gSolutionInfoActiveGathering && GlobalProject)
	{
		solInfoActive = true;
		GlobalProject->CloseProject();
		vLogUnfiltered("WARN: FTG active solutionInfo, not taking db dir lock, leak dbs");
	}

	HANDLE thrdID = GetCurrentThread();
	int thrdPri = GetThreadPriority(thrdID);
	SetThreadPriority(thrdID, THREAD_PRIORITY_HIGHEST);
	BCGInit(FALSE);

	_ASSERTE(!gVaLicensingHost);

	extern void ClearParserGlobals();
	ClearParserGlobals();

	extern void ClearTextOutGlobals();
	ClearTextOutGlobals();
	ClearDebuggerGlobals(true);

	g_ScreenAttrs.Invalidate();
	VAWorkspaceViews::CreateWorkspaceView(NULL);
	VATreeSetup(false);
	AutotextManager::DestroyAutotextManager();
	CMenuXP::ClearGlobals();

	gHashtagManager = nullptr;

	{
		auto tmp = gVsSnippetMgr;
		gVsSnippetMgr = nullptr;
		delete tmp;
	}

	{
		auto tmp = gColorSyncMgr;
		gColorSyncMgr = nullptr;
		delete tmp;
	}

	if (GlobalProject && !solInfoActive)
	{
		CatLog("Environment.Project", "FTG gP");
		auto tmp = GlobalProject;
		GlobalProject = nullptr;
		delete tmp;
	}

	if (g_pCSDic)
	{
		CatLog("Environment.Project", "FTG gCSD");
		FileDic* sav = g_pCSDic;
		g_pCSDic = NULL;
		if (!solInfoActive)
		{
			sav->SortIndexes();
			delete sav;
		}
	}
	if (g_pMFCDic)
	{
		CatLog("Environment.Project", "FTG gMFCD");
		FileDic* sav = g_pMFCDic;
		g_pMFCDic = NULL;
		if (!solInfoActive)
		{
			sav->SortIndexes();
			delete sav;
		}
	}
	if (s_pSysDic)
	{
		// delete this guy after the MFC and CS dics since they share the
		// hashtable that this one owns (and will delete)
		CatLog("Environment.Project", "FTG VaSys");
		FileDic* sav = s_pSysDic;
		s_pSysDic = NULL;
		if (!solInfoActive)
			delete sav;
	}
	if (g_pEmptyDic)
	{
		FileDic* sav = g_pEmptyDic;
		g_pEmptyDic = NULL;
		if (!solInfoActive)
			delete sav;
	}
	if (g_pGlobDic)
	{
		CatLog("Environment.Project", "FTG gGD");
		FileDic* sav = g_pGlobDic;
		g_pGlobDic = NULL;
		if (!solInfoActive)
		{
			sav->SortIndexes();
			delete sav;
		}
	}
	if (g_ReservedWords)
	{
		CatLog("Environment.Project", "FTG gRW");
		auto tmp = g_ReservedWords;
		g_ReservedWords = nullptr;
		delete tmp;
	}
	if (g_FontSettings)
	{
		CatLog("Environment.Project", "FTG FS");
		auto tmp = g_FontSettings;
		g_FontSettings = nullptr;
		delete tmp;
	}
	if (g_statBar)
	{
		CatLog("Environment.Project", "FTG gsB");
		auto tmp = g_statBar;
		try
		{
			tmp->DestroyWindow();
		}
		catch (CException* e)
		{
			e->Delete();
		}
		g_statBar = nullptr;
		delete tmp;
	}
	if (g_pUsage)
	{
		auto tmp = g_pUsage;
		g_pUsage = nullptr;
		delete tmp;
	}
	if (g_WorkSpaceTab)
	{
		auto tmp = g_WorkSpaceTab;
		g_WorkSpaceTab = nullptr;
		delete tmp;
	}

	{
		auto tmp = gVaService;
		gVaService = nullptr;
		delete tmp;
	}

	FreeMiniHelp();
	ClearFontCache();

	if (g_CompletionSet)
	{
		g_CompletionSet->Dismiss();
		auto tmp = g_CompletionSet;
		g_CompletionSet = nullptr;
		delete tmp;
	}
	if (gImgListMgr)
	{
		auto tmp = gImgListMgr;
		gImgListMgr = nullptr;
		delete tmp;
	}

	{
		ProjectLoaderRef pl;
		if (pl)
			pl->Stop();
	}

	ClearFileFinder();

	if (g_VA_MRUs)
	{
		g_VA_MRUs->SaveALL();
		auto tmp = g_VA_MRUs;
		g_VA_MRUs = nullptr;
		delete tmp;
	}

	gVsSubWndVect.Shutdown();
	g_rbuffer.Save();
	FreeDFileMPs();
	SetThreadPriority(thrdID, thrdPri);
	OleUninitialize();
	IncludesDb::Close();
	InheritanceDb::Close();
	g_SortBins.Close();
	g_DBFiles.Close();
#if !defined(VA_CPPUNIT)
	g_DBFiles.ReleaseDbsForExit();
#endif
	CleanupSmartSelectStatics();
	extern void CleanupRefactorGlogals();
	CleanupRefactorGlogals();
	ClearFileIdManager();

	if (-1 != gTypingDevLang)
	{
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "DevLang", (DWORD)(int)gTypingDevLang);
		gTypingDevLang = -1;
	}

	// do last
	if (Psettings)
	{
		CatLog("Environment.Project", "FTG Ps");
		auto tmp = Psettings;
		Psettings = nullptr;
		delete tmp;
	}

	if (g_IdeSettings)
	{
		CatLog("Environment.Project", "FTG GIS");
		auto tmp = g_IdeSettings;
		g_IdeSettings = nullptr;
		delete tmp;
	}

	sKeepGdiPlusLoaded = nullptr;

#if !defined(VA_CPPUNIT)
	if (VaDirs::IsFlaggedForDbDirPurge())
		VaDirs::PurgeDbDir();
#endif(VA_CPPUNIT)

	if (gProcessLock)
	{
		auto tmp = gProcessLock;
		gProcessLock = NULL;
		CloseHandle(tmp);
	}

	if (sHistoryDirCountLock)
	{
		auto tmp = sHistoryDirCountLock;
		sHistoryDirCountLock = NULL;
		CloseHandle(tmp);
	}

	if (g_pDBLock)
	{
		auto tmp = g_pDBLock;
		g_pDBLock = nullptr;
		delete tmp;
	}

	if (g_managedInterop)
		g_managedInterop.Release();

	{
#if defined(RAD_STUDIO)
		// in this config, gVaMainWnd is a hidden window we created.
		auto tmp = gVaMainWnd;
		gVaMainWnd = nullptr;
		_ASSERTE(tmp);
		tmp->CloseWindow();
		tmp->DestroyWindow();
		delete tmp;
#else
		// in this config, gVaMainWnd is just a pointer to gMainWnd.
		gVaMainWnd = nullptr;
#endif
	}

	{
		auto tmp = gMainWnd;
		gMainWnd = nullptr;
		if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
		{
			if (tmp && tmp->m_hWnd)
				tmp->Detach();
		}
	}

#if !defined(VA_CPPUNIT)
	const std::wstring serverStatsW(vaIPC::stats::get_stats());
	const WTString svrStats(serverStatsW.c_str());
	vLog("%s", svrStats.c_str());

#ifdef _DEBUG
	size_t servers, connections;
	vaIPC::Client::GetServerConnectionCount(servers, connections);
	// all running servers have a connection to va, but if there is more
	// than one connection to any server, then someone unexpectedly (?) left
	// a connection open.
	_ASSERTE(servers == connections);
#endif

	vaIPC::Client::ExitAllServerInstances();
#endif

	// [case: 88151] move to end of function so that if it crashes, dbs are ok
	UninitScrollWindowExPatch();
#if !defined(VA_CPPUNIT)
	PatchW32Functions(false);
#endif

	g_GlyfBuffer.Cleanup();
	WPF_ViewManager::Shutdown();
	ReleaseAstResources();
	ClearVsSolution();

	extern void cleanup_rheap();
	cleanup_rheap();

#ifdef DEBUG_MEM_STATS
	CMemoryState memEnd, memDiff;
	memEnd.Checkpoint();
	memDiff.Difference(gMemStart, memEnd);
	memDiff.DumpAllObjectsSince();
#endif
}

BOOL CAN_USE_NEW_SCOPE(int ftype /*= NULL*/)
{
	if (!ftype && g_currentEdCnt)
		ftype = g_currentEdCnt->m_ftype;

	if (Is_Tag_Based(ftype))
		return GlobalProject->IsArmWrapperOk();

	switch (ftype)
	{
	case Src:
	case Header:
	case Idl:
	case VB:
	case VBS:
	case CS:
	case UC:
	case JS:
	case PHP:
#ifdef _DEBUG
		// 	case Java:
	case RC:
	case PERL:
#endif // _DEBUG
		return GlobalProject->IsArmWrapperOk();

	default:
		return FALSE;
	}
}

BOOL IsFeatureSupported(FeatureType feature, int langType /*= NULL*/)
{
	BOOL retval = FALSE;
	if (!Psettings || !Psettings->m_enableVA)
		return FALSE;
	if (!langType && g_currentEdCnt)
		langType = g_currentEdCnt->m_ScopeLangType;

	switch (feature)
	{
	case Feature_HoveringTips:
		if (!Psettings->m_mouseOvers)
			break;
		if (IsCFile(langType))
		{
			if (gShellAttr->IsDevenv8OrHigher())
				retval = Psettings->mQuickInfoInVs8;
			else
				retval = TRUE;
		}
		else if (CS == langType && !gShellAttr->IsDevenv10OrHigher() && Psettings->m_mouseOvers && Psettings->mScopeTooltips)
		{
			// [case: 72732]
			if (gShellAttr->IsDevenv8OrHigher())
				retval = Psettings->mQuickInfoInVs8;
			else
				retval = TRUE;
		}
		else
			retval = Is_HTML_JS_VBS_File(langType);
		break;

	case Feature_ParamTips:
		if (!Psettings->mVaAugmentParamInfo)
			break;
		if (Psettings->mSuppressAllListboxes)
			break;
		if (Psettings->mRestrictVaListboxesToC && !(IsCFile(langType)))
			break;

		// do not use Psettings->m_ParamInfo since param info can be invoked manually.
		// Psettings->m_ParamInfo is for automatic param info.
		if (IsCFile(langType))
			retval = TRUE;
		else if (CS == langType)
			retval = FALSE;
		else if (Is_HTML_JS_VBS_File(langType) && !gShellAttr->IsDevenv11OrHigher())
			retval = TRUE;
		else if (Is_VB_VBS_File(langType) && !gShellAttr->IsDevenv())
			retval = TRUE;
		break;

	case Feature_Outline:
#ifndef _DEBUG
		if (Idl == langType)
		{
			retval = FALSE;
			break;
		}
#endif
		// fall through
	case Feature_HCB:
	case Feature_Refactoring:
		retval = CAN_USE_NEW_SCOPE(langType);
		break;

	case Feature_MiniHelp:
		retval = CAN_USE_NEW_SCOPE(langType);
		break;

	case Feature_FormatAfterPaste:
		if (Is_C_CS_File(langType) || langType == JS)
			retval = Psettings->m_smartPaste;
		break;

	case Feature_Suggestions:
	case Feature_CaseCorrect:
	case Feature_SpellCheck:
	default:
		_ASSERTE(!"Not Implemented.");
		break;
	}

	if (retval && !GlobalProject->IsArmWrapperOk())
		retval = false;

	return retval;
}

void InitSysHeaders(MultiParsePtr mp, CStringW& stdafx)
{
	_ASSERTE(g_pMFCDic && !g_pMFCDic->m_loaded && sMfcLoading);
	TimeTrace tt("Parse system headers");
	WideStrVector files;

	{
		WTString contents;
		contents.ReadFile(stdafx);
		CStringW contentsWide(contents.Wide());

		WideStrVector lines;
		WtStrSplitW(contentsWide, lines, L"\n");
		if (lines.size() < 2)
		{
			lines.clear();
			WtStrSplitW(contentsWide, lines, L"\r");
		}

		const CStringW dir(Path(stdafx));

		for (WideStrVector::const_iterator it = lines.begin(); it != lines.end() && !gShellIsUnloading; ++it)
		{
			CStringW curLine(*it);
			if (curLine.Find(L"#include") != 0)
				continue;

			curLine = curLine.Mid(9);
			BOOL isSys = FALSE;
			curLine = GetIncFileStr(CString(curLine), isSys);
			if (curLine.IsEmpty())
				continue;

			CStringW file(gFileFinder->ResolveInclude(curLine, dir, TRUE));
			file = ::MSPath(file);
			if (file.IsEmpty())
				continue;

			if (Binary == ::GetFileType(file))
				continue;

			files.push_back(file);
		}
	}

	if (gShellIsUnloading)
		return;

	const UINT stdafxFileId = gFileIdManager->GetFileId(stdafx);

	{
		// extract of MultiParse::ParseFile to mark as being parsed
		FILETIME ftime;
		GetFTime(stdafx, &ftime);
		const DbTypeId VADbId = g_DBFiles.DBIdFromParams(Header, stdafxFileId, TRUE, FALSE);
		g_DBFiles.SetFileTime(stdafxFileId, VADbId, &ftime); // Update ftime before parse. case=23013

		WTString dateStr;
		dateStr.WTFormat("%lx.%lx", ftime.dwHighDateTime, ftime.dwLowDateTime);
		const CString fileIdStr = gFileIdManager->GetFileIdStr(stdafx);
		// this db entry is made so that we know if a file has been loaded/parsed - see IsIncluded()
		// it doesn't look like FTime/dateStr matters anymore - time check is done differently now - see IsIncluded()
		// The lines that follow are the equivalent of MultiParse::ImmediateDBOut
		std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(stdafxFileId, VADbId, FALSE));
		_ASSERTE(dbw);
		dbw->DBOut(WTString(fileIdStr), dateStr, UNDEF, V_FILENAME | V_SYSLIB | V_VA_STDAFX | V_HIDEFROMUSER, 0,
		           stdafxFileId);
	}

	auto parseFunc = [](const CStringW& file) {
		if (gShellIsUnloading)
			return;

		try
		{
			// extract of IsIncluded
			DType* fileData = GetSysDic()->GetFileData(file);
			if (fileData)
				return;

			// extract of ParseGlob
			MultiParsePtr mp2 = MultiParse::Create();
			mp2->FormatFile(CStringW((LPCWSTR)file), V_SYSLIB, ParseType_Globals);
		}
		catch (const UnloadingException&)
		{
			throw;
		}
#if !defined(SEAN)
		catch (...)
		{
			// ODS even in release builds in case VALOGEXCEPTION causes an exception
			CString msg;
			CString__FormatA(msg, "VA ERROR exception caught ISH %d\n", __LINE__);
			OutputDebugString(msg);
			_ASSERTE(!"InitSysHeaders exception");
			VALOGEXCEPTION("ISH:");
		}
#endif // !SEAN
	};

	const int kDefaultSerialCnt = 31;
	// parse first kDefaultSerialCnt files serially
	uint cnt = 0;
	const uint kSerialCnt = (files.size() / 6) > kDefaultSerialCnt ? kDefaultSerialCnt : uint(files.size() / 6);
	WideStrVector::const_iterator it;
	for (it = files.cbegin(); it != files.cend() && cnt < kSerialCnt; ++it, ++cnt)
		parseFunc(*it);

	// parse the rest in parallel
	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(it, files.cend(), parseFunc);
	else
		std::for_each(it, files.cend(), parseFunc);

	if (StopIt)
	{
		// do not mark va stdafx as parsed if sys header parsing was interrupted
		g_DBFiles.RemoveFile(stdafxFileId);
		return;
	}

	// finally, pass in the file itself
	mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);

#ifndef AVR_STUDIO
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && !gShellIsUnloading)
	{
		// [case: 65644]
		CStringW file(gFileFinder->ResolveInclude(L"vccorlib.h", CStringW(), FALSE));
		file = ::MSPath(file);
		if (!file.IsEmpty() && !mp->IsIncluded(file))
			mp->FormatFile(file, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
	}
#endif
}

void RunTriggeredCommand(LPCTSTR valName, const CStringW& arg)
{
	if (!Psettings || !Psettings->m_enableVA)
		return;

	const CString keyName(ID_RK_APP + _T("\\TriggeredCommands"));
	CStringW cmd(GetRegValueW(HKEY_CURRENT_USER, keyName, valName));
	if (cmd.IsEmpty())
		return;

	class RunCmd : public PooledThreadBase
	{
	  public:
		RunCmd(const CStringW cmd, const CStringW dir) : PooledThreadBase("TriggeredCommand"), mCmd(cmd), mDir(dir)
		{
			StartThread();
		}

	  protected:
		virtual void Run()
		{
			const int redirectPos = mCmd.FindOneOf(L"<>|");
			const int exePos = mCmd[0] == L'\"' ? mCmd.Find(L".exe\"") : -1;
			if (-1 == exePos || -1 != redirectPos)
			{
				::_wsystem(mCmd);
			}
			else
			{
				const CStringW exe(mCmd.Left(exePos + 5));
				const CStringW params(mCmd.Mid(exePos + 5));
				::ShellExecuteW(NULL, NULL, exe, params, mDir, SW_HIDE);
			}
		}

	  private:
		const CStringW mCmd;
		const CStringW mDir;
	};

	cmd.Replace(L"$file$", arg);
	cmd.Replace(L"$FILE$", arg);
	vLog("Queuing triggered command: %s\n", (LPCTSTR)CString(cmd));
	new RunCmd(cmd, MSPath(arg));
}

bool ContainsPseudoProject(const ProjectVec& prjs)
{
	for (ProjectVec::const_iterator it = prjs.begin(); it != prjs.end(); ++it)
	{
#ifdef _DEBUG
#if !defined(VA_CPPUNIT)
		// assert for basic understanding of projects that don't have project file
		const CStringW prjFile((*it)->GetProjectFile());
		const CStringW prjFileBase(::GetBaseName(prjFile));
		if (prjFile == prjFileBase)
			_ASSERTE((*it)->IsPseudoProject());
		else if (!prjFileBase.CompareNoCase(L"cmakelists.txt"))
			_ASSERTE((*it)->IsPseudoProject());
		else if (IsDir(prjFile))
			_ASSERTE((*it)->IsPseudoProject());
		else
			_ASSERTE(!(*it)->IsPseudoProject());
#endif // VA_CPPUNIT
#endif // _DEBUG

		if ((*it)->IsPseudoProject())
			return true;
	}

	return false;
}
