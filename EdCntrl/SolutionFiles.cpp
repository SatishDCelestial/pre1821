#include "stdafxed.h"
#include <atlbase.h>
#include <atlcom.h>
#include <list>
#include "SolutionFiles.h"
#include "Log.h"
#include "Lock.h"
#include "RegKeys.h"
#include "Registry.h"
#include "DevShellAttributes.h"
#include "project.h"
#include "Settings.h"
#include "FILE.H"
#include "TokenW.h"
#include "SubClassWnd.h"
#include "VaTimers.h"
#include "ProjectInfo.h"
#include "WtException.h"
#include "Directories.h"
#include <functional>
#include "FileTypes.h"
#include "FileFinder.h"
#include "CodeGraph.h"
#include "TraceWindowFrame.h"
#include "FOO.H"
#include "LogElapsedTime.h"
#include "MenuXP\Tools.h"
#include "StringUtils.h"
#include "SolutionListener.h"
#include <set>
#include "FileId.h"
#include "..\common\ScopedIncrement.h"
#include "ParseWorkItem.h"
#include "ParseThrd.h"
#pragma warning(push, 3)
#include "miloyip-rapidjson\include\rapidjson\document.h"
#pragma warning(pop)
#include "vsshell150.h"
#include "SemiColonDelimitedString.h"
#include "vsshell.h"
#include "FileVerInfo.h"

#pragma warning(disable : 4127)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#undef PropertySheet

// #newVsVersion
// 2FA4AAA3-0285-495E-AC12-8239B65791EA // again didn't change, same value as Dev15 and Dev16?
extern const GUID LIBID_VCProjectEngineLibrary17 = {
    0x2FA4AAA3, 0x0285, 0x495E, {0xac, 0x12, 0x82, 0x39, 0xB6, 0x57, 0x91, 0xEA}};

// 2FA4AAA3-0285-495E-AC12-8239B65791EA // didn't change for 16.0 preview 1 ??
extern const GUID LIBID_VCProjectEngineLibrary16 = {
    0x2FA4AAA3, 0x0285, 0x495E, {0xac, 0x12, 0x82, 0x39, 0xB6, 0x57, 0x91, 0xEA}};

// 2FA4AAA3-0285-495E-AC12-8239B65791EA
extern const GUID LIBID_VCProjectEngineLibrary15 = {
    0x2FA4AAA3, 0x0285, 0x495E, {0xac, 0x12, 0x82, 0x39, 0xB6, 0x57, 0x91, 0xEA}};

// E809F780-D1CD-4DF0-BFEC-F37CEE4C381F
extern const GUID LIBID_VCProjectEngineLibrary14 = {
    0xe809f780, 0xd1cd, 0x4df0, {0xbf, 0xec, 0xf3, 0x7c, 0xee, 0x4c, 0x38, 0x1f}};

// 59a9abbd-b8ef-4262-9a2a-9d043ee8752c
extern const GUID LIBID_VCProjectEngineLibrary12 = {
    0x59a9abbd, 0xb8ef, 0x4262, {0x9a, 0x2a, 0x9d, 0x04, 0x3e, 0xe8, 0x75, 0x2c}};

// 39cf17c6-7b3c-47f0-bceb-c7b923594b73
extern const GUID LIBID_VCProjectEngineLibrary11 = {
    0x39cf17c6, 0x7b3c, 0x47f0, {0xbc, 0xeb, 0xc7, 0xb9, 0x23, 0x59, 0x4b, 0x73}};

// 0CD36BB6-D828-4DB9-91BF-AD493EE76B79
extern const GUID LIBID_VCProjectEngineLibrary10 = {
    0x0CD36BB6, 0xD828, 0x4DB9, {0x91, 0xBF, 0xAD, 0x49, 0x3E, 0xE7, 0x6B, 0x79}};

// 7b932c1e-942e-4f8f-a71a-015a41ff634b
extern const GUID LIBID_VCProjectEngineLibrary9 = {
    0x7b932c1e, 0x942e, 0x4f8f, {0xa7, 0x1a, 0x01, 0x5a, 0x41, 0xff, 0x63, 0x4b}};

extern const GUID LIBID_VCProjectEngineLibrary8 = {
    0xfbbf3c60, 0x2428, 0x11d7, {0x8b, 0xf6, 0x00, 0xb0, 0xd0, 0x3d, 0xaa, 0x06}};

extern const GUID LIBID_VCProjectEngineLibrary7 = {
    0x6194e01d, 0x71a1, 0x419f, {0x85, 0xe3, 0x47, 0xba, 0x62, 0x83, 0xdd, 0x1d}};

static int GetSolutionRootProjectList(CComPtr<EnvDTE::_DTE> dte, VsProjectList& projectsOut);
static bool IsAppropriateProject(EnvDTE::Project* pProject);
static bool IsAppropriateProject(const CStringW& project);
static CStringW GetProjectFullFilename(EnvDTE::Project* pProject);
static void NormalizeProjectList(CStringW& projectList);
static void GetProjectItemName(EnvDTE::ProjectItem* pItem, FileList& projFileList, FileList& propsFileList);
static void TokenizedAddToFileList(const CComBSTR& str, FileList& flist);
static void TokenizedAddToFileList(const CComBSTR& str, FileList& flist, const CStringW& relativeToDir,
                                   bool forFile = true, bool mustExist = true);
static void TokenizedAddToFileListUnique(const CComBSTR& str, FileList& flist, const FileList& chkList,
                                         const CStringW& relativeToDir, bool forFile = true, bool mustExist = true);
static void TokenizedAddToEitherFileListUnique(const CComBSTR& str, FileList& flist, FileList& chkList,
                                               const CStringW& relativeToDir, bool forFile = true,
                                               bool mustExist = true);
static bool GetVcIntellisenseInfo(VsSolutionInfo::ProjectSettingsPtr pCurProjSettings, int requestId,
                                  DWORD& intellisenseLoadRetryDuration);

// [case: 87629]
// only one thread at a time should be interacting with vcprojectengine
std::atomic<int> gSolutionInfoActiveGathering;

// const std::wregex kThirdPartyRegex(L"(third|3rd)[ ]*party", std::regex::ECMAScript | std::regex::optimize |
// std::regex::icase);
const CStringW kVcpkgIncEnvironmentVar(L"$(VcpkgRoot)");
const CStringW kVcpkgIncPropertyName(L"VcpkgRoot");

class RetrySolutionLoad : public ParseWorkItem
{
	const DWORD mStartTicks;

  public:
	RetrySolutionLoad() : ParseWorkItem("RetrySolutionLoad"), mStartTicks(::GetTickCount())
	{
	}

	virtual void DoParseWork()
	{
		auto RetryFn = [] {
			if (GlobalProject)
				GlobalProject->SolutionLoadCompleted(false);
		};
		RunFromMainThread(RetryFn);
	}

	virtual bool CanRunNow() const
	{
		const DWORD now = ::GetTickCount();
		return ((now - mStartTicks) > 1000) || (now < mStartTicks);
	}

	virtual bool ShouldRunAtShutdown() const
	{
		return false;
	}
};

struct __declspec(uuid("250225c7-3404-49f6-8447-b251639208e8")) IVcProjectEngineEventSink : IUnknown
{
	virtual ~IVcProjectEngineEventSink() = default;
	virtual void ManageSync(IUnknown* e, bool advise) = 0;
	virtual bool IsSynced() const = 0;
};

static IVcProjectEngineEventSink* CreateVcProjectEngineEventSink();

class CacheLoadException : public WtException
{
  public:
	CacheLoadException() : WtException("Invalid project cache")
	{
	}
	CacheLoadException(LPCTSTR msg) : WtException(msg)
	{
	}
};

class CacheSaveException : public WtException
{
  public:
	CacheSaveException(LPCTSTR msg) : WtException(msg)
	{
	}
};

class ProjectLoadException : public WtException
{
  public:
	ProjectLoadException() : WtException("Project enumeration failure")
	{
	}
	ProjectLoadException(LPCTSTR msg) : WtException(msg)
	{
	}
};

AsyncSolutionQueryInfo gSqi;
CCriticalSection AsyncSolutionQueryInfo::sInfoLock;
int AsyncSolutionQueryInfo::sRequestIdCounter = 0;

bool AsyncSolutionQueryInfo::IsSolutionInfoCacheValid(const CStringW& solutionFile, int deferredProjectCnt) const
{
	AutoLockCs l(sInfoLock);

	if (!deferredProjectCnt)
	{
		// [case: 102486]
		// don't use sln info cache if no projects were deferred
		vLog("ASQI:ISICV 0 0");
		return false;
	}

	if (!mRequestId)
	{
		vLog("ASQI:ISICV 0 1");
		return false;
	}

	if (deferredProjectCnt != mDeferredProjectCount)
	{
		vLog("ASQI:ISICV 0 2");
		return false;
	}

	if (solutionFile.CompareNoCase(mRequestedSolutionFile))
	{
		vLog("ASQI:ISICV 0 3");
		return false;
	}

	if (mSolutionInfo.empty())
	{
		vLog("ASQI:ISICV 0 4");
		return false;
	}

	// do not check mProjectsWithoutFiles -- that is dealt with via
	// async notification of Workspace State change to complete

	vLog("ASQI:ISICV 1");
	return true;
}

bool AsyncSolutionQueryInfo::RequestAsyncWorkspaceCollection(const CStringW& solutionFile, int deferredProjectCnt,
                                                             bool forceLoad)
{
	{
		AutoLockCs l(sInfoLock);
		if (mRequestedSolutionFile != solutionFile)
		{
			vLog("ASQI:RAWC 1");
			Reset();
			_ASSERTE(forceLoad);
			mRequestedSolutionFile = solutionFile;
			mDeferredProjectCount = deferredProjectCnt;
		}
		else if (mDeferredProjectCount != deferredProjectCnt)
		{
			vLog("ASQI:RAWC 2");
			mSolutionInfo.clear();
			mDeferredProjectCount = deferredProjectCnt;
			// change in deferred project count does not affect mWorkspaceIndexerIsFinished
			_ASSERTE(forceLoad);
		}
		else if (mSolutionInfo.empty() && !forceLoad)
		{
			vLog("ASQI:RAWC 3");
			forceLoad = true;
		}
		else
		{
			// cache is valid, but need to check project files again
			vLog("ASQI:RAWC 4");
		}

		mRequestId = ++sRequestIdCounter;
		mForceLoadOnComplete = forceLoad;
	}

	if (g_vaManagedPackageSvc)
	{
		Log("sf::check: async request");
		g_vaManagedPackageSvc->CollectWorkspaceInfoAsync(mRequestId, solutionFile, true);
		return true;
	}
	else
	{
		Log("ERROR: sf::check: no mng pkg");
		gVaMainWnd->KillTimer(IDT_SolutionWorkspaceForceRequest);
		gVaMainWnd->SetTimer(IDT_SolutionWorkspaceForceRequest, 5000, nullptr);
		return false;
	}
}

bool AsyncSolutionQueryInfo::RequestAsyncWorkspaceCollection(bool forceLoad)
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);
	int deferredProjects = ::GetSolutionDeferredProjectCount();
	if (!gDte)
		return false;

	CComPtr<EnvDTE::_Solution> pSolution;
	gDte->get_Solution(&pSolution);
	if (!pSolution)
		return false;

	const CStringW solutionFile = ::GetSolutionFileOrDir();
	if (solutionFile.IsEmpty())
	{
		vLog("ASQI:RAWCa 0");
		return false;
	}

	vLog("ASQI:RAWCa 1");
	if (IsSolutionInfoCacheValid(solutionFile, deferredProjects))
	{
		if (!mProjectsWithoutFiles)
		{
			// cache is valid and all projects have files, no need to check again
			vLog("ASQI:RAWCa 2");
			return false;
		}

		_ASSERTE(!forceLoad); // If this assert fires, why is caller overriding cache?

		if (mWorkspaceIndexerIsFinished)
		{
			if (!forceLoad && mLoadedSinceIndexerFinished)
			{
				// cache is valid, but some projects do not have files, but don't
				// check again because we already received the completed notification
				vLog("ASQI:RAWCa 3");
				return false;
			}
		}

		// cache is valid, but some projects do not have files, check again to
		// see if any changed
		vLog("ASQI:RAWCa 4");
	}

	vLog("ASQI:RAWCa doReq");
	return RequestAsyncWorkspaceCollection(solutionFile, deferredProjects, forceLoad);
}

void AsyncSolutionQueryInfo::AsyncWorkspaceCollectionComplete(int reqId, const WCHAR* solutionFilepath,
                                                              const WCHAR* workspaceFiles)
{
	CStringW files(workspaceFiles);

	auto threadFunc = [this, reqId, files]() mutable -> void {
		bool doSolutionLoad = mForceLoadOnComplete;

		bool indexer_failed = files == "-";
		if (indexer_failed)
			files = L"";

		{
			AutoLockCs l(sInfoLock);
			if (reqId != GetCurrentRequestId())
			{
				// the request is no longer current (assume one was made later, so wait for it to be handled)
				vLog("warn: AsyncWorkspaceCollectionComplete skipped for later request");
				return;
			}

			::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			SemiColonDelimitedString wksp(files);

			VsSolutionInfo::ProjectSettingsMap solutionInfo;
			VsSolutionInfo::ProjectSettingsPtr pCurProjSettings;
			LPCWSTR pCurItem;
			int curItemLen;
			int projectsWithoutFiles = 0;
			int foundIntellisenseInfo = 0;
			int missingIntellisenseInfo = 0;
			while (wksp.NextItem(pCurItem, curItemLen))
			{
				CStringW file(pCurItem, curItemLen);
				if (file.IsEmpty())
				{
					_ASSERTE(!"this shouldn't happen");
					continue;
				}

				if (file[0] == L'P' && ::StartsWith(file, L"PROJ:", FALSE))
				{
					if (pCurProjSettings)
					{
						if (::IsFile(pCurProjSettings->mProjectFile))
						{
							if (pCurProjSettings->mFiles.empty())
								++projectsWithoutFiles;
						}
					}

					pCurProjSettings = std::make_shared<VsSolutionInfo::ProjectSettings>();
					file = file.Mid(5);
					CStringW target;
					if (file.Replace(L":::", L";"))
					{
						// [case: 102486]
						// cmake projects have target appended after project file
						// va pkg replaces ';' with ':::' to the ';' don't break parsing of workspaceFiles param
						if (file[file.GetLength() - 1] == L';')
							file = file.Left(file.GetLength() - 1);
						else
						{
							int pos = file.Find(';');
							if (-1 != pos)
							{
								target = file.Mid(pos);
								file = file.Left(pos);
							}
						}
					}

					file = ::MSPath(file);
					solutionInfo[file + target] = pCurProjSettings;
					pCurProjSettings->mProjectFile = file + target;
					pCurProjSettings->mProjectFileDir = ::Path(file) + L"\\";

					file = ::GetBaseNameExt(file);
					if (!file.CompareNoCase(L"vcxproj"))
					{
						pCurProjSettings->mIsVcProject = true;
						pCurProjSettings->mVcMissingConfigs = true;
					}
					else
					{
						pCurProjSettings->mPseudoProject = true;
					}

					if (::GetVcIntellisenseInfo(pCurProjSettings, reqId, mIntellisenseLoadRetryDuration))
						++foundIntellisenseInfo;
					else
						++missingIntellisenseInfo;
					continue;
				}

				if (file[0] == L'S' && ::StartsWith(file, L"SRC:", FALSE))
				{
					_ASSERTE(pCurProjSettings);
					file = file.Mid(4);
					file = ::MSPath(file);

					// filter out crud that came from workspace service
					if (IsDir(file))
						continue;

					if (!IsFile(file))
					{
						CStringW tmp(file);
						tmp.MakeLower();
						if (-1 != tmp.Find(L"microsoft"))
							continue;
					}

					pCurProjSettings->mFiles.AddUniqueNoCase(file);
					continue;
				}

				_ASSERTE(!"unhandled item in AsyncWorkspaceCollectionComplete");
				vLog("ERROR: unhandled item in AsyncWorkspaceCollectionComplete");
			}

			if (!doSolutionLoad && projectsWithoutFiles != mProjectsWithoutFiles)
			{
				vLog("ASQI:AWCC force 1 f(%d) m(%d)", foundIntellisenseInfo, missingIntellisenseInfo);
				doSolutionLoad = true;
			}

			if (!doSolutionLoad)
			{
				if (foundIntellisenseInfo && mIntellisenseInfoFoundCount != foundIntellisenseInfo &&
				    mWorkspaceIndexerIsFinished && !mLoadedSinceIndexerFinished)
				{
					vLog("ASQI:AWCC force 2 f(%d) m(%d)", foundIntellisenseInfo, missingIntellisenseInfo);
					doSolutionLoad = true;
				}
			}

			if (doSolutionLoad)
			{
				mIntellisenseInfoFoundCount = foundIntellisenseInfo;
				mSolutionInfo.swap(solutionInfo);
				mProjectsWithoutFiles = projectsWithoutFiles;
				if (mWorkspaceIndexerIsFinished)
					mLoadedSinceIndexerFinished = true;
			}
		}

		if (doSolutionLoad)
		{
			vLog("ASQI:AWCC 1");
			if (GlobalProject)
				GlobalProject->SolutionLoadStarting();

			// async callback can happen on background thread or ui thread,
			// but call to SolutionLoadCompleted must happen on ui thread.
			RunFromMainThread(
			    []() {
				    if (GlobalProject)
					    GlobalProject->SolutionLoadCompleted(true, true);
			    },
			    false);

			AutoLockCs l(sInfoLock);
			if ((mProjectsWithoutFiles || mSolutionInfo.empty()) &&
			    (!mWorkspaceIndexerIsFinished || !mLoadedSinceIndexerFinished))
			{
				if (gVaMainWnd->GetSafeHwnd())
				{
					vLog("ASQI:AWCC start ping");
					gVaMainWnd->KillTimer(IDT_SolutionWorkspaceIndexerPing);
					if (!indexer_failed)
						gVaMainWnd->SetTimer(IDT_SolutionWorkspaceIndexerPing, 30000, nullptr);
					else
					{
						vLog("ASQI:Indexer failed; skipping ping");
						VsWorkspaceIndexerFinishedBeforeLoad();
					}
				}
			}
		}

		::CoUninitialize();
	};

	new LambdaThread(threadFunc, "WorkspaceCollectorCallback", true, true, 0);
}

void AsyncSolutionQueryInfo::VsWorkspaceIndexerFinished()
{
	if (gVaMainWnd->GetSafeHwnd())
		gVaMainWnd->KillTimer(IDT_SolutionWorkspaceIndexerPing);

	vLog("ASQI:VSIF");
	AutoLockCs l(sInfoLock);
	_ASSERTE(!mWorkspaceIndexerIsFinished); // not a critical condition, but is unexpected
	mWorkspaceIndexerIsFinished = true;
}

void AsyncSolutionQueryInfo::VsWorkspaceIndexerFinishedBeforeLoad()
{
	AutoLockCs l(sInfoLock);
	if (!mWorkspaceIndexerIsFinished)
	{
		VsWorkspaceIndexerFinished();
		mLoadedSinceIndexerFinished = true;
	}
}

void AsyncSolutionQueryInfo::Reset()
{
	vCatLog("Environment.Solution", "ASQI:Reset");
	AutoLockCs l(sInfoLock);
	mRequestedSolutionFile.Empty();
	mDeferredProjectCount = mProjectsWithoutFiles = 0;
	mRequestId = 0;
	mForceLoadOnComplete = true;
	mSolutionInfo.clear();
	mWorkspaceIndexerIsFinished = false;
	mLoadedSinceIndexerFinished = false;
	mIntellisenseInfoFoundCount = 0;
	mIntellisenseLoadRetryDuration = 0;
}

bool AsyncSolutionQueryInfo::IsWorkspaceIndexerActive() const
{
	if (!gShellAttr || !gShellAttr->IsDevenv15OrHigher() || !gDte)
		return false;

	CComPtr<EnvDTE::_Solution> pSolution;
	gDte->get_Solution(&pSolution);
	if (!pSolution)
		return false;

	int deferredProjects = 0;
	const bool isOpenFolder = ::IsInOpenFolderMode();
	// [case: 102486]
	// use workspace indexer for folder-based solutions
	if (!isOpenFolder)
	{
		deferredProjects = ::GetSolutionDeferredProjectCount();
		if (!deferredProjects)
			return false;
	}

	const CStringW solutionFile = ::GetSolutionFileOrDir();
	if (solutionFile.IsEmpty() && !isOpenFolder)
		return false;

	AutoLockCs l(sInfoLock);
	if (!mRequestId)
		return false;

	if (!isOpenFolder && !IsSolutionInfoCacheValid(solutionFile, deferredProjects))
		return true;

	return !mWorkspaceIndexerIsFinished;
}

VsSolutionInfo::ProjectSettingsMap AsyncSolutionQueryInfo::GetSolutionInfo() const
{
	VsSolutionInfo::ProjectSettingsMap ret;
	AutoLockCs l(sInfoLock);
	for (auto cur : mSolutionInfo)
	{
		// must make a separate, unshared copy for cache to remain
		// valid because Project uses swap on the list members
		ret[cur.first] = std::make_shared<VsSolutionInfo::ProjectSettings>(cur.second.get());
	}
	return ret;
}

void GetProjectItemName(EnvDTE::ProjectItem* pItem, FileList& projFileList, FileList& propsFileList)
{
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		CComBSTR name, kind, fullnameB;
		pItem->get_Name(&name);
		pItem->get_FileNames(1, &fullnameB);
		pItem->get_Kind(&kind);
		kind.ToUpper();
		CStringW nameW(name);
		if (kind != "{6BB5F8F0-4483-11D3-8BCF-00C04F8EC28C}" && // project folder
		    kind != "{6BB5F8EF-4483-11D3-8BCF-00C04F8EC28C}" && // Properties
		    kind !=
		        "{66A2671F-8FB5-11D2-AA7E-00C04F688DDE}" && // External File that happen to be open when project loads
		    fullnameB.Length())
		{
			const CStringW fullname(fullnameB);
			if (gShellAttr->IsDevenv10OrHigher())
			{
				static const CStringW kFiltersExtension = L".filters";
				const int pos = fullname.Find(kFiltersExtension);
				if (-1 != pos && pos == (fullname.GetLength() - kFiltersExtension.GetLength()))
					return;
			}

			const CStringW ext(::GetBaseNameExt(fullname));
			if (!ext.CompareNoCase(L"projitems"))
			{
				// [case: 80931]
				// add projitems dependency as prop sheet (just checks time and content hash)
				propsFileList.Add(fullname);
			}
			else
			{
				// [case: 80212]
				// files from shared projects can appear multiple times so change to unique add
				projFileList.AddUniqueNoCase(fullname);
			}
		}
		else if (!nameW.IsEmpty() && nameW[0] == L'<' && -1 != nameW.Find(L".Shared>"))
		{
			// [case: 80931]
			// add shproj dependency as prop sheet (just checks time and content hash)
			CStringW shProj(fullnameB);
			shProj += nameW.Mid(1, nameW.GetLength() - 2);
			shProj += L".shproj";
			if (IsFile(shProj))
				propsFileList.Add(shProj);
		}
		else
		{
#ifdef _DEBUGxx
			CStringW msg;
			CString__FormatW(msg, L"VaSlnLoad: skipped project item: [%ls] [%ls] [%ls]\n", nameW, CStringW(fullnameB),
			                 CStringW(kind));
			::OutputDebugStringW(msg);
#endif
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("GetProjectItemName:");
	}
#endif // !SEAN
}

bool IsProjectCacheValid(const CStringW& projectList)
{
	// [case: 116673]
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv8OrHigher() && Psettings && Psettings->mCacheProjectInfo);
	std::shared_ptr<VsSolutionInfo> sfc(::CreateVsSolutionInfo());
	if (!sfc)
	{
		_ASSERTE(sfc);
		return false;
	}

	// check project file timestamps
	SemiColonDelimitedString projs(projectList);
	CStringW projFile;
	while (projs.NextItem(projFile))
	{
		if (!sfc->IsProjectCacheValid(projFile))
			return false;
	}

	return true;
}

bool CheckForNewSolution(EnvDTE::_DTE* pDTE, CStringW previousSolFile, CStringW previousProjectPaths)
{
	if (!pDTE)
		return false;

	CComPtr<EnvDTE::_Solution> pSolution;
	pDTE->get_Solution(&pSolution);
	if (!pSolution)
	{
		Log("sf::check: no solution");
		gSqi.Reset();
		return true;
	}

	const CStringW solutionFile(::GetSolutionFileOrDir());
	if (solutionFile.IsEmpty())
	{
		Log("sf::check: need load - empty solution file");
		gSqi.Reset();
		return true;
	}

	bool asyncRequest = false;
	const int deferredProjectCnt = ::GetSolutionDeferredProjectCount();
	const bool openFolderMode = ::IsInOpenFolderMode();
	if (g_vaManagedPackageSvc && !solutionFile.IsEmpty() && (deferredProjectCnt || openFolderMode))
	{
		if (openFolderMode)
			_ASSERTE(::IsDir(solutionFile));

		// [case: 105291]
		if (gSqi.IsSolutionInfoCacheValid(solutionFile, deferredProjectCnt))
		{
			Log("sf::check: async cache valid");
		}
		else
		{
			if (gSqi.RequestAsyncWorkspaceCollection(solutionFile, deferredProjectCnt, true))
				asyncRequest = true;
			else
				gSqi.Reset();
		}
	}
	else
	{
		if (!openFolderMode && !solutionFile.IsEmpty())
			_ASSERTE(!::IsDir(solutionFile));

		_ASSERTE(!gShellAttr->IsDevenv10OrHigher() || g_vaManagedPackageSvc ||
		         (!deferredProjectCnt && !openFolderMode));
		gSqi.Reset();
	}

	if (previousSolFile.CompareNoCase(solutionFile))
	{
		vLog("sf::check: need load - different solution file old(%s) new(%s)", (LPCTSTR)CString(previousSolFile),
		     (LPCTSTR)CString(solutionFile));
		if (asyncRequest)
		{
			// async callback will load solution
			Log("sf::check: async 1");
			return false;
		}

		// different solution, don't need to iterate over it
		return true;
	}

	if (asyncRequest)
	{
		// async callback will load solution
		Log("sf::check: async 2");
		return false;
	}

	TimeTrace tr("CheckForNewSolution project iterate");
	VsProjectList vsProjects;
	BuildSolutionProjectList(vsProjects);

	CStringW projectList;
	if (solutionFile.GetLength() > 3 && IsDir(solutionFile))
	{
		// dev15 folder-based solution
		projectList = solutionFile + L";";
	}

	if (!vsProjects.size() && projectList.IsEmpty())
	{
		Log("sf::check: no projects");
		return true;
	}

	for (auto vsPrj : vsProjects)
	{
		const CStringW projFilePath(GetProjectFullFilename(vsPrj));
		if (projFilePath.IsEmpty())
			continue;

		projectList += projFilePath + L";";
	}

	if (deferredProjectCnt && !solutionFile.IsEmpty() && !solutionFile.CompareNoCase(gSqi.GetRequestedSolutionFile()))
	{
		int foundDeferred = 0;
		CStringW lowerProjects(projectList);
		lowerProjects.MakeLower();
		VsSolutionInfo::ProjectSettingsMap asyncInfo = gSqi.GetSolutionInfo();
		for (auto ai : asyncInfo)
		{
			// skip if project already in list
			CStringW curProj(ai.first + L";");
			curProj.MakeLower();
			int pos = lowerProjects.Find(curProj);
			if (-1 != pos)
				continue;

			++foundDeferred;
			ai.second->mDeferredProject = true;
			projectList += ai.first + L"(deferred);";
		}
	}

	NormalizeProjectList(projectList);

	if (projectList.IsEmpty() || previousProjectPaths.GetLength() != projectList.GetLength())
	{
		Log("sf::check: need load 1");
		return true;
	}

	if (previousProjectPaths != projectList)
	{
		if (solutionFile.GetLength() > 3 && IsDir(solutionFile))
		{
			if (-1 != previousProjectPaths.Find(solutionFile + L";"))
			{
				// vs2017+ folder-based solutions could trigger projectList
				// mismatch since CheckForNewSolution doesn't hit
				// CreateProjectFromDirectory (relies solely on DTE)

				Log("sf::check: clean (folder-based)");
				return false;
			}
		}

		Log("sf::check: need load 2");
		return true;
	}

	if (gShellAttr && gShellAttr->IsDevenv8OrHigher() && Psettings && Psettings->mCacheProjectInfo)
	{
		// [case: 116673]
		if (!IsProjectCacheValid(projectList))
		{
			Log("sf::check: need load 3");
			return true;
		}
	}

	Log("sf::check: clean");
	return false;
}

CStringW GetProjectFullFilename(EnvDTE::Project* pProject)
{
	CStringW projFile;
	if (!pProject)
		return projFile;

	CComBSTR tmpB;
	pProject->get_FileName(&tmpB);
	projFile = tmpB;

	if (projFile.IsEmpty())
	{
		pProject->get_FullName(&tmpB);
		const CStringW tmp(tmpB);
		projFile = tmp;
	}
	else if (-1 == projFile.FindOneOf(L"/\\"))
	{
		pProject->get_FullName(&tmpB);
		const CStringW tmp(tmpB);
		if (tmp.GetLength() > projFile.GetLength() && -1 != tmp.FindOneOf(L"/\\"))
		{
			// AVR and Beckhoff return name without path in FileName; full path in FullName
			projFile = tmp;
		}
	}

	return projFile;
}

bool IsAppropriateProject(EnvDTE::Project* pProject)
{
	if (!pProject)
		return false;

	const CStringW projName(GetProjectFullFilename(pProject));
	if (IsAppropriateProject(projName))
		return true;

	CComBSTR projKindB;
	pProject->get_Kind(&projKindB);
	if (projKindB == L"{E24C65DC-7377-472b-9ABA-BC803B73C61A}") // 2005 Web project
		return true;

	vLog("Project: reject %s\n", (LPCTSTR)CString(projName));
	return false;
}

bool IsAppropriateProject(const CStringW& project)
{
	const WCHAR* projExt = PathFindExtensionW(project);

	if (-1 != project.Find(L"\\SingleFileISense\\_sfi_"))
	{
		// [case: 92706]
		return false;
	}

#ifdef AVR_STUDIO
	if (projExt &&
	    (!_wcsicmp(projExt, L".avrgccproj") || !_wcsicmp(projExt, L".cproj") || !_wcsicmp(projExt, L".cppproj")))
	{
		return true;
	}
#else
	if ((projExt &&
	     (!_wcsicmp(projExt, L".vcproj") || !_wcsicmp(projExt, L".csproj") || !_wcsicmp(projExt, L".vbproj") ||
	      !_wcsicmp(projExt, L".vcxproj") || !_wcsicmp(projExt, L".csxproj") || !_wcsicmp(projExt, L".vbxproj") ||
	      !_wcsicmp(projExt, L".icproj") || !_wcsicmp(projExt, L".shproj") ||
	      !_wcsicmp(projExt, L".tsproj") ||  // [case: 84121]
	      !_wcsicmp(projExt, L".plcproj") || // [case: 84121]
	      !_wcsicmp(projExt, L".vcxitems") || (Psettings->mUnrealScriptSupport && !_wcsicmp(projExt, L".ucproj")) ||
	      !_wcsicmp(projExt, L".androidproj") || !_wcsicmp(projExt, L".iosproj") || !_wcsicmp(projExt, L".pbxproj") ||
	      !_wcsicmp(projExt, L".pyproj") || !_wcsicmp(projExt, L".etp"))) ||
	    project.IsEmpty())
	{
		return true;
	}
#endif

	// many project guids listed here:
	// http://www.mztools.com/articles/2008/MZ2008017.aspx
	// need to see if projExt differs or are we good using ext above

	if (projExt && *projExt)
	{
		// [case: 24606]
		// unpublished back door for getting files from custom projects
		// to appear in OFIS - do not publish this except through support.
		// Use at own risk - no warranty, etc...
		// List extensions as ".ext1;" or ".ext1;.ext2;"
		static CStringW sCustomProjectExtensions;
		static bool once = true;
		if (once)
		{
			once = false;
			sCustomProjectExtensions = GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, "CustomProjectExtensions");
			if (!sCustomProjectExtensions.IsEmpty())
				sCustomProjectExtensions += L";";
		}

		if (!sCustomProjectExtensions.IsEmpty() && sCustomProjectExtensions.Find(projExt + CStringW(L";")) != -1)
			return true;
	}

	return false;
}

void NormalizeProjectList(CStringW& projectList)
{
	WideStrVector projs;
	TokenW t = projectList;
	while (t.more())
	{
		CStringW proj = t.read(L";");
		if (proj.IsEmpty())
			continue;

		if (projs.end() == std::find(projs.begin(), projs.end(), proj))
			projs.push_back(proj);
	}

	// [case: 84121] workaround inconsistent IVsSolution project enumeration
	std::sort(projs.begin(), projs.end());

	projectList.Empty();
	for (auto p : projs)
		projectList += p + L";";
}

static void CleanPathString(CStringW& str)
{
	str.Replace(L"\r", L"");
	str.Replace(L"\n", L"");
	str.Trim();
	if (str[0] == L'"')
	{
		str = str.Mid(1);
		if (str[str.GetLength() - 1] == L'"')
			str = str.Left(str.GetLength() - 1);
	}

	str.Replace(L"&quot", L"");

	if (2 < str.Find(L"\\\\"))
		str.Replace(L"\\\\", L"\\");
}

// void
// TokenizedAddToFileList(const CComBSTR & str,
//					   FileList & flist)
//{
//	TokenW t(str);
//	while (t.more())
//	{
//		CStringW cur(t.read(L";\r\n"));
//		if (!cur.IsEmpty())
//		{
//			CleanPathString(cur);
//			if (!cur.IsEmpty())
//				flist.AddUniqueNoCase(cur);
//		}
//	}
//}

void TokenizedAddToFileList(const CComBSTR& str, FileList& flist, const CStringW& relativeToDir,
                            bool forFile /*= true*/, bool mustExist /*= true*/)
{
	_ASSERTE(!relativeToDir.IsEmpty());
	TokenW t(str);
	while (t.more())
	{
		CStringW cur(t.read(L";\r\n"));
		if (!cur.IsEmpty())
		{
			const CStringW orig(cur);
			CleanPathString(cur);

			if (gShellAttr && gShellAttr->IsDevenv15OrHigher() && !forFile && !mustExist)
			{
				if (cur == L"Include\\um")
				{
					const CStringW tmp(BuildPath(cur, relativeToDir, false, false));
					if (!IsDir(tmp))
					{
						// [case: 99758]
						// dev15p4 platform noise
						continue;
					}
				}
			}

			cur = BuildPath(cur, relativeToDir, forFile, mustExist);
			if (cur.IsEmpty() && forFile && Binary == ::GetFileType(orig))
			{
				// for references
				cur = gFileFinder->ResolveReference(orig, relativeToDir);
			}

			if (!cur.IsEmpty())
				flist.AddUniqueNoCase(cur);
		}
	}
}

void TokenizedAddToFileListUnique(const CComBSTR& str, FileList& flist, const FileList& chkList,
                                  const CStringW& relativeToDir, bool forFile /*= true*/, bool mustExist /*= true*/)
{
	_ASSERTE(!relativeToDir.IsEmpty());
	TokenW t(str);
	while (t.more())
	{
		CStringW cur(t.read(L";\r\n"));
		if (cur.IsEmpty())
			continue;

		CleanPathString(cur);
		cur = BuildPath(cur, relativeToDir, forFile, mustExist);
		if (cur.IsEmpty())
			continue;

		if (!chkList.ContainsNoCase(cur))
			flist.AddUniqueNoCase(cur);
	}
}

// [case: 100271]
void TokenizedAddToEitherFileListUnique(const CComBSTR& str, FileList& flist, FileList& chkList,
                                        const CStringW& relativeToDir, bool forFile /*= true*/,
                                        bool mustExist /*= true*/)
{
	_ASSERTE(!relativeToDir.IsEmpty());
	TokenW t(str);
	while (t.more())
	{
		CStringW cur(t.read(L";\r\n"));
		if (cur.IsEmpty())
			continue;

		CleanPathString(cur);
		cur = BuildPath(cur, relativeToDir, forFile, mustExist);
		if (cur.IsEmpty())
			continue;

		if (chkList.ContainsNoCase(cur))
			continue;

		bool isVcpkgInstallDir = false;
		CStringW curLower(cur);
		curLower.MakeLower();
		if (-1 != curLower.Find(L"\\installed\\") || -1 != curLower.Find(L"/installed/"))
		{
			const CStringW bn(Basename(curLower));
			if (bn == "include")
			{
				const int len = curLower.GetLength();
				const int baseLen = 8; // "\include"
				// check available triplets
				// https://blogs.msdn.microsoft.com/vcblog/2016/09/19/vcpkg-a-tool-to-acquire-and-build-c-open-source-libraries-on-windows/
				if ((len - 4 - baseLen) == curLower.Find(L"-uwp") ||
				    (len - 8 - baseLen) == curLower.Find(L"-windows") ||
				    (len - 15 - baseLen) == curLower.Find(L"-windows-static"))
				{
					CStringW projDir(relativeToDir);
					projDir.MakeLower();

					// if is not a sub-dir of solution, then treat as system
					if (-1 == curLower.Find(projDir))
						isVcpkgInstallDir = true;
				}
			}
		}

		if (isVcpkgInstallDir)
			chkList.AddUniqueNoCase(cur);
		else
			flist.AddUniqueNoCase(cur);
	}
}

static CStringW GetCacheFile(const CStringW& projPath)
{
	CStringW cacheFile;
	CString__FormatW(cacheFile, L"%lsProjectInfo\\%08x.va", (LPCWSTR)VaDirs::GetDbDir(),
	                 gFileIdManager->GetFileId(projPath));
	return cacheFile;
}

void InvalidateProjectCache(const CStringW& projfile)
{
	const CStringW cacheFile(GetCacheFile(projfile));
	if (IsFile(cacheFile))
	{
		vLog("InvalidateProjectCache: %s (%s)\n", (LPCTSTR)CString(projfile), (LPCTSTR)CString(cacheFile));
		DeleteFileW(cacheFile);
	}
}

void VsSolutionInfo::ClearData()
{
	AutoLockCs l(mLock);
	mProjects.Empty();
	mProjectMap.clear();
	mReferences.clear();
	mSolutionPackageIncludes.clear();
}

template <int projectEngineVersion, typename VCProjectEngineT, typename VCProjectT, typename VCCollectionT,
          typename VCConfigurationT, typename VCPropertySheetT, typename VCCLCompilerToolT, typename VCNMakeToolT,
          typename VCRulePropertyStorageT, typename VCPlatformT, typename VCSdkReferenceT, typename VCReferencesT,
          typename VCReferenceT, typename VCCompileAsManagedOptionsT, typename VCConfiguration2T,
          typename VCIntelliSenseInfoT>
class VsSolutionInfoImpl : public VsSolutionInfo
{
  public:
	VsSolutionInfoImpl(EnvDTE::_DTE* pDte) : mDte(pDte)
	{
	}

	virtual ~VsSolutionInfoImpl()
	{
		ClearProjectEventSinks();
	}

  private:
	bool mCompleteInfo = true;
	bool mDidVcEnvironmentSetup = false;
	bool mUseIntellisenseInfo = false;              // [case: 102486]
	bool mUseVcProjectConfigRuleProperties = false; // [case: 103729]
	long mRootLevelProjectCnt = 0;                  // [case: 68124]
	long mWalkedProjectCnt = 0;
	CComPtr<EnvDTE::_DTE> mDte;
	ProjectSettingsPtr mCurSettings;
	std::list<ProjectSettingsPtr> mSettingsStack; // std::stack not available in vs2005
	std::list<ProjectSettingsPtr> mSharedVcProjectSource;
	CComPtr<IUnknown> mVcProjectItemsEvents; // CComPtr<VCProjectEngineEventsT>
	CComPtr<IVcProjectEngineEventSink> mVcProjectItemsEventsSink;
	CStringW mLastVcProjectConfigPlatform;
	bool mForcedVcpkgInclude = false; // [case: 109042]
	CStringW mVcpkgIncDir;            // [case: 109042]
	std::vector<std::pair<CAdapt<CComPtr<VCProjectT>>, CAdapt<ProjectSettingsPtr>>> mVcProjects;

  public:
	void GetMinimalProjectInfo(const CStringW& proj, int rootProjectCount, const VsProjectList& rootProjects)
	{
		mCompleteInfo = false;
		mRootLevelProjectCnt = rootProjectCount;
		mWalkedProjectCnt = 0;

		_ASSERTE(GetCurrentThreadId() == g_mainThread);
		AutoLockCs l(mLock);
		ClearData();
		if (!mDte)
			return;

		LogElapsedTime et("GetMinProjInf", 1000);
		mDidVcEnvironmentSetup = false;

		try
		{
			for (auto pProject : rootProjects)
			{
				const CStringW projPath(GetProjectFullFilename(pProject));
				if (projPath.GetLength())
				{
					if (!proj.CompareNoCase(projPath))
					{
						WalkProject(pProject);
						break;
					}
				}
			}

			_ASSERTE(mSettingsStack.empty());
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::GetMinimalProjectInfo 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::GetMinimalProjectInfo");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::GetMinimalProjectInfo 2");
			}
		}
#endif // !SEAN
	}

	void GetSolutionInfo()
	{
		// GetSolutionInfo must happen on ui thread.
		// If called on non-ui thread:
		//		get_Solution succeeds but get_FileName returns empty string.
		//		get_Projects succeeds but get_Count returns 0.
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.vcprojectengine%28VS.100%29.aspx
		_ASSERTE(GetCurrentThreadId() == g_mainThread);
		ScopedIncrementAtomic si(&gSolutionInfoActiveGathering);
		if (gSolutionInfoActiveGathering > 1)
		{
			// [case: 87629]
			vLog("warn: GetSolutionInfo exit -- someone else active -- retry later");
			if (g_ParserThread)
				g_ParserThread->QueueParseWorkItem(new RetrySolutionLoad());
			return;
		}

		AutoLockCs l(mLock);
		ClearData();
		if (!mDte)
			return;

		TimeTrace tr("Solution Interrogation");
		ClearProjectEventSinks(); // [case: 39946]
		mDidVcEnvironmentSetup = false;
		mRootLevelProjectCnt = 0;
		mWalkedProjectCnt = 0;

		if (!Psettings || !GlobalProject)
			return;

		try
		{
			CComPtr<EnvDTE::_Solution> pSolution;
			mDte->get_Solution(&pSolution);
			if (!pSolution)
				return;

			if (!Psettings->mForceUseOldVcProjectApi && gShellAttr->IsDevenv15u3OrHigher() &&
			    GlobalProject->IsVcFastProjectLoadEnabled())
			{
				// [case: 103729]
				// only use rule props if FPL is enabled since cache will be incomplete
				// if we rely on read-only FPL projects
				mUseVcProjectConfigRuleProperties = true;
			}

			mSolutionFile = ::GetSolutionFileOrDir();
			vCatLog("Environment.Solution", "GetSolutionInfo begin: (%s)\n", (LPCTSTR)CString(mSolutionFile));

			VsProjectList rootProjects;
			mRootLevelProjectCnt = ::GetSolutionRootProjectList(mDte, rootProjects);
			for (auto p : rootProjects)
				WalkProject(p);

			if (!mSolutionFile.IsEmpty() && !mSolutionFile.CompareNoCase(gSqi.GetRequestedSolutionFile()))
			{
				int foundDeferred = 0;
				// get solution info cache
				VsSolutionInfo::ProjectSettingsMap asyncInfo = gSqi.GetSolutionInfo();
				std::vector<CStringW> mapItemsToRemove;
				for (auto ai : asyncInfo)
				{
					const CStringW& curProjFile(ai.first);
					const CStringW baseProjFilename(::GetBaseNameNoExt(curProjFile));
					// skip if project already in map
					bool keepCurAi = true;
					for (auto proj : mProjectMap)
					{
						if (!proj.first.CompareNoCase(curProjFile))
						{
							keepCurAi = false;
							break;
						}

						if (proj.second->mPseudoProject && proj.second->mProjectFile.IsEmpty() &&
						    proj.second->mFiles.empty())
						{
							if (!proj.first.Compare(baseProjFilename))
							{
								// this is the stub for the deferred project, so remove it.
								mapItemsToRemove.push_back(baseProjFilename);
								if (ai.second->mVsProjectName != baseProjFilename)
									ai.second->mVsProjectName = baseProjFilename;
							}
							else if (!ai.second->mVsProjectName.IsEmpty() &&
							         !proj.first.Compare(ai.second->mVsProjectName))
							{
								// this is the stub for the deferred project, so remove it.
								mapItemsToRemove.push_back(ai.second->mVsProjectName);
							}
						}
					}

					if (!keepCurAi)
						continue;

					if (IsDir(mSolutionFile))
					{
						ai.second->mPseudoProject = true;
					}
					else
					{
						++foundDeferred;
						ai.second->mDeferredProject = true;
						mProjects += curProjFile + L"(deferred);";
					}

					mProjectMap[curProjFile] = ai.second;
				}

				for (auto cur : mapItemsToRemove)
					mProjectMap.erase(cur);
			}

			if (projectEngineVersion >= 15 && mSolutionFile.GetLength() > 3 && IsDir(mSolutionFile))
			{
				// [case: 104857]
				// dev15 folder-based solution
				mProjects += mSolutionFile + L";";
			}

			vCatLog("Environment.Solution", "GetSolutionInfo end: %ld root projects, walked %ld\n", mRootLevelProjectCnt, mWalkedProjectCnt);

			_ASSERTE(mSettingsStack.empty());
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::GetSolutionInfo 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::GetSolutionInfo");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::GetSolutionInfo 2");
			}
		}
#endif // !SEAN

		::NormalizeProjectList(mProjects);
	}

	struct FolderProject
	{
		CStringW markerFile;
		std::vector<std::wregex> fileExclusionRegexes;
		FileList fileList;
	};

	using FolderProjectPtr = std::shared_ptr<FolderProject>;
	using FolderProjects = std::vector<FolderProjectPtr>;

	struct CollectFolderProjectsData
	{
		WIN32_FIND_DATAW fileData;
		HANDLE hFile = nullptr;
		CStringW searchSpec;
		CStringW fileAndPath;

		~CollectFolderProjectsData()
		{
			if (hFile && hFile != INVALID_HANDLE_VALUE)
				::FindClose(hFile);
		}
	};

	// [case: 144472] Support VS Code settings.json files.exclude setting.
	std::wregex CompileRegexFromWildcard(CStringW wildcard, CStringW workspaceDir)
	{
		// Support for the following formats.
		// 
		// **/.DS_Store
		// **/.git/objects/**
		// node_modules			(only exclude from the root of the workspace)

		if (!wildcard.IsEmpty())
		{
			// Convert all slashes.
			wildcard.Replace(LR"(/)", LR"(\)");
			// Escape all slashes.
			wildcard.Replace(LR"(\)", LR"(\\)");
			// Escape all periods.
			wildcard.Replace(LR"(.)", LR"(\.)");
			// Clean double wildcard by converting it to single.
			wildcard.Replace(LR"(**)", LR"(*)");
			// Convert wildcard statement (double above or single) to match anything.
			wildcard.Replace(LR"(*)", LR"(.*)");
			// Add start of string anchor.
			wildcard.Insert(0, LR"(^)");

			if (wildcard.GetAt(1) != L'.' || wildcard.GetAt(2) != L'*')
			{
				// Only exclude this directory from the root of the workspace.
				workspaceDir.Replace(LR"(\)", LR"(\\)");
				workspaceDir.Append(LR"((.*\\)?)");	// [case: 149167] fix for "test_shallow_exclude" (maybe not a bug but aligning with the VS behavior)
				wildcard.Insert(1, workspaceDir);
			}

			if (wildcard.GetAt(wildcard.GetLength() - 2) != L'.' || wildcard.GetAt(wildcard.GetLength() - 1) != L'*')
			{
				// Whatever we add here should be inside the following ()?
				wildcard.Append(LR"(()");

				if (wildcard.GetAt(wildcard.GetLength() - 2) != L'\\' || wildcard.GetAt(wildcard.GetLength() - 1) != L'\\' )
				{
					// Add slashes to end the directory name and avoid matching "dirnameOtherDir".
					wildcard.Append(LR"(\\)");
				}

				// Match anything at the end of the string.
				wildcard.Append(LR"(.*)");

				// Closing ()?
				wildcard.Append(LR"()?)");
			}

			// Add end of string anchor.
			wildcard.Append(LR"($)");
		}

		return std::wregex(wildcard, std::wregex::ECMAScript | std::wregex::optimize | std::wregex::icase);
	}

	// [case: 144472] Support VS Code settings.json files.exclude setting.
	std::vector<std::wregex> CompileFileExclusionRegexesFromVsCodeSettings(const CStringW& searchDir)
	{
		std::vector<std::wregex> regexes;
		namespace rjs = RAPIDJSON_NAMESPACE;
		const CStringW vsCodeSettings = searchDir + L".vscode\\settings.json";

		if (IsFile(vsCodeSettings))
		{
			WTString text;
			text.ReadFile(vsCodeSettings);
			
			if (!text.IsEmpty())
			{
				rjs::Document doc;
				doc.Parse<rjs::ParseFlag::kParseCommentsFlag | rjs::ParseFlag::kParseTrailingCommasFlag>(text.c_str());

				if (!doc.HasParseError())
				{
					if (doc.HasMember("files.exclude"))
					{
						rjs::Value& exclusions = doc["files.exclude"];

						if (exclusions.IsObject())
						{
							for (auto it = exclusions.MemberBegin(); it != exclusions.MemberEnd(); ++it)
							{
								const auto& exclusion = *it;

								if (exclusion.value.IsBool() && exclusion.value.GetBool())
								{
									// Convert the exclusion to regex and add to our list to test files later.
									CStringW wildcard = exclusion.name.GetString();
									wildcard.TrimRight();
									std::wregex regex = CompileRegexFromWildcard(wildcard, searchDir);
									regexes.push_back(regex);
								}
							}
						}
					}
				}
				else
				{
					vLog("ERROR: failed to parse vs code settings (%s)\n", (LPCTSTR)CString(vsCodeSettings));
				}
			}
		}
		return regexes;
	}

	void CollectFolderProjs(FolderProjects& projs, FolderProject* curProj, CStringW searchDir)
	{
		auto locals = std::make_unique<CollectFolderProjectsData>();
		if (!locals)
		{
			vLog("ERROR: CollectFolderProjs out of memory\n");
			return;
		}

		if (searchDir.IsEmpty())
			return;

		vLog("CollectFolderProjs: %s\n", (LPCTSTR)CString(searchDir));

		const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
		if (kLastChar == L'\\' || kLastChar == L'/')
			searchDir = searchDir.Left(searchDir.GetLength() - 1);

		searchDir.Append(L"\\");

		CStringW projectMarker(searchDir + L"CppProperties.json");
		if (IsFile(projectMarker))
		{
			// create a new folder project
			vLog("prj: %s\n", (LPCTSTR)CString(projectMarker));
			auto p = std::make_shared<FolderProject>(FolderProject({projectMarker}));
			projs.push_back(p);
			curProj = p.get();
		}
		else
		{
			// [case: 102486] cmake
			projectMarker = searchDir + L"CMakeLists.txt";

			if (IsFile(projectMarker))
			{
				std::vector<std::wregex> fileExclusionRegexes;

				if (Psettings && Psettings->mRespectVsCodeExcludedFiles)
				{
					// [case: 144472] respect excluded files in VS Code settings files
					fileExclusionRegexes = CompileFileExclusionRegexesFromVsCodeSettings(searchDir);
				}

				// create a new folder project 
				vLog("prj: %s\n", (LPCTSTR)CString(projectMarker));
				auto p = std::make_shared<FolderProject>(FolderProject({projectMarker, fileExclusionRegexes}));
				projs.push_back(p);
				curProj = p.get();
			}
		}

		locals->searchSpec = searchDir + L"*.*";
		locals->hFile = FindFirstFileW(locals->searchSpec, &locals->fileData);
		if (locals->hFile != INVALID_HANDLE_VALUE)
		{
			do
			{
				const CStringW curFile(locals->fileData.cFileName);
				if (curFile.IsEmpty() || curFile[0] == L'.')
				{
					// treat files and directories that start with '.' as hidden
					// do not descend into hidden directories
					continue;
				}

				const DWORD isDir = FILE_ATTRIBUTE_DIRECTORY & locals->fileData.dwFileAttributes;

				locals->fileAndPath = searchDir + curFile;
				if (!isDir)
				{
					if (!curProj)
					{
						auto p = std::make_shared<FolderProject>(FolderProject({projectMarker}));
						projs.push_back(p);
						curProj = p.get();
					}

					bool addFile = true;

					for (auto regex : curProj->fileExclusionRegexes)
					{
						if (std::regex_match(std::wstring(locals->fileAndPath), regex))
						{
							addFile = false;
							break;
						}
					}

					if (addFile)
					{
						curProj->fileList.Add(locals->fileAndPath);
					}
				}

				if (isDir)
					CollectFolderProjs(projs, curProj, locals->fileAndPath);
			} while (FindNextFileW(locals->hFile, &locals->fileData));
		}
	}

	void FinishGetSolutionInfo(bool runningAsync /*= true*/) override
	{
		ScopedIncrementAtomic si(&gSolutionInfoActiveGathering);
		// [case: 96945]
		if (gSolutionInfoActiveGathering > (runningAsync ? 1 : 2))
		{
			// [case: 87629]
			// ignore -- if someone else is already gathering info, let them finish
			// because only one of us will be in here meaning that someone else is
			// starting a new load
			vLog("warn: FinishGetSolutionInfo exit -- someone else active");
			return;
		}

		NestedTrace t("FinishGetSolutionInfo");
		_ASSERTE(projectEngineVersion >= 10 || !mVcProjects.size());
		_ASSERTE(mSettingsStack.empty() && !mCurSettings);
		bool isCleanRun = true;

		for (auto it = mVcProjects.begin(); it != mVcProjects.end() && !StopIt; ++it)
		{
			ProjectSettingsPtr curSettings = (*it).second;
			_ASSERTE(curSettings && curSettings->mIsVcProject);
			mCurSettings = curSettings;
			try
			{
				auto vcProject = (CComPtr<VCProjectT>)it->first;
				if (vcProject)
				{
					auto WalkProj = [&]() { WalkVcProject(vcProject); };

					// [case: 100735] [case: 102486]
					// CollectIntellisenseInfo should be run on bg thread
					if (runningAsync && !mUseIntellisenseInfo && projectEngineVersion >= 15)
					{
						::RunFromMainThread(WalkProj);
						::Sleep(25);
					}
					else
					{
						WalkProj();
					}

					if (mUseVcProjectConfigRuleProperties)
					{
						// not required to run on UI thread
						WalkVcProjectBg(vcProject);
					}

					if (!StopIt && projectEngineVersion >= 10)
					{
						auto GetAsmRefs = [&]() {
							CComPtr<IDispatch> idis;
							vcProject->get_Object(&idis);
							if (idis)
							{
								CComQIPtr<EnvDTE::Project> vsProject{idis};
								if (vsProject)
								{
									GetReferences(vsProject, mCurSettings->mReferences);
								}
							}
						};

						if (runningAsync && projectEngineVersion >= 15)
						{
							// [case: 100735]
							// get assembly references on ui thread
							::RunFromMainThread(GetAsmRefs);
							// [case: 116492]
							// handle project references independently of vc references with a break
							// between them so that UI thread is a bit more responsive
							::Sleep(25);
						}
						else
						{
							// [case: 98091]
							// get assembly references on project loader thread
							GetAsmRefs();
						}
					}

					if (!StopIt && projectEngineVersion >= 11 /*&& !mUseIntellisenseInfo*/)
					{
						auto GetVcRefs = [&]() { GetVcReferences(vcProject, mCurSettings->mReferences); };

						if (runningAsync && projectEngineVersion >= 15)
						{
							// [case: 100735]
							// get vc references on ui thread
							::RunFromMainThread(GetVcRefs);
							::Sleep(25);
						}
						else
						{
							// [case: 98091]
							// on project loader thread
							GetVcRefs();
						}
					}
				}

				if (runningAsync && !GlobalProject || GlobalProject->GetSolutionHash() != mSolutionHash)
				{
					_ASSERTE(mSolutionHash);
					isCleanRun = false;
					mCurSettings = nullptr;
					break;
				}
			}
			catch (const WtException& e)
			{
				WTString msg;
				msg.WTFormat("FinishGetSolutionInfo exception caught (%d) : ", StopIt);
				msg += e.GetDesc();
				VALOGEXCEPTION(msg.c_str());
				Log(msg.c_str());
				isCleanRun = false;
			}
			mCurSettings = nullptr;

			if (mCompleteInfo && Psettings && Psettings->mCacheProjectInfo && isCleanRun && !StopIt)
			{
				try
				{
					// [case: 80931] couldn't find a property on vc++ platform
					// projects to get shared project, so if there are any c++
					// shared projects, we assume all in solution are dependent
					// (only affects cache so that reload of solution forces
					// invalidation if shared project modified outside of vs).
					for (auto p : mSharedVcProjectSource)
					{
						if (p->mProjectFile != curSettings->mProjectFile)
							curSettings->mPropertySheets.Add(p->mProjectFile);
					}

					curSettings->SaveProjectCache();
				}
				catch (const CacheSaveException& e)
				{
					vLog("ProjectInfoCache: (FGSI) failed to save cache (%s) (%s) (%s)\n",
					     (LPCTSTR)CString(curSettings->mProjectFile),
					     (LPCTSTR)CString(::GetCacheFile(curSettings->mProjectFile)), e.GetDesc().c_str());
				}
			}
		}

		_ASSERTE(mSettingsStack.empty() && !mCurSettings);
		mVcProjects.clear();

		if (projectEngineVersion >= 15 && mSolutionFile.GetLength() > 3 && IsDir(mSolutionFile) && !StopIt)
		{
			// [case: 96662] [case: 102080]
			// vs2017 folder-based solution
			TimeTrace tr("ProjectsFromDirectory");
			FolderProjects cppFolderProjs;
			CollectFolderProjs(cppFolderProjs, nullptr, mSolutionFile);
			if (!cppFolderProjs.empty())
			{
				// [case: 104857] [case: 109903]
				_ASSERTE(-1 != mProjects.Find(mSolutionFile + L";"));
			}
			vLog("DirProj cnt(%zu)\n", cppFolderProjs.size());
			for (const auto& folder : cppFolderProjs)
				CreateProjectFromDirectory(mSolutionFile, folder->markerFile, folder->fileList);
		}

		if (mCompleteInfo && isCleanRun && !StopIt)
		{
			if (Psettings->mLookupNuGetRepository)
			{
				::GetRepositoryIncludeDirs(GetSolutionFile(), mSolutionPackageIncludes);
				// list is not cached (there is no sln cache)
			}

			if (g_loggingEnabled)
			{
				for (auto dir : mSolutionPackageIncludes)
					vLog("sln package include: %s\n", (LPCTSTR)CString(dir.mFilename));
			}

			UpdateReferences();

			// [case: 149171] collect file exclusions here if they exist in .vscode folder
			if (GlobalProject && Psettings && Psettings->mRespectVsCodeExcludedFiles)
			{
				if (!IsDir(mSolutionFile))
				{
					CStringW slnDir = ::Path(mSolutionFile);
					slnDir += L"\\";
					std::vector<std::wregex> fileExclusionRegexes = CompileFileExclusionRegexesFromVsCodeSettings(slnDir);
					GlobalProject->SetFileExclusionRegexes(std::move(fileExclusionRegexes));
				}
			}
		}
	}

	bool IsProjectCacheValid(const CStringW& projFile) override
	{
		// [case: 116673]
		try
		{
			ProjectSettingsPtr projSettings = std::make_shared<ProjectSettings>();
			LoadCachedProjectSettings(projSettings, projFile);
			return true;
		}
		catch (const CacheLoadException&)
		{
			return false;
		}
	}

  private:
	void WalkProject(EnvDTE::Project* pProject)
	{
		try
		{
			if (!::IsAppropriateProject(pProject))
				return;

			TimeTrace tr("WalkProject");
			mSettingsStack.push_front(mCurSettings);
			ProjectSettingsPtr curSettings(new ProjectSettings);
			mCurSettings = curSettings;
			bool okToSave = true;

			bool needToWalk = true;
			CStringW projFile(GetProjectFullFilename(pProject));

			if (projFile.IsEmpty())
			{
				// don't use cache for "projects" that don't have a file
				CComBSTR projName;
				pProject->get_Name(&projName);
				projFile = projName;
				if (projFile.IsEmpty())
					projFile = L"Solution Items";
				ProjectSettingsPtr otherSettings = mProjectMap[projFile];
				if (otherSettings)
				{
					// multiple items with same name - stick all together
					curSettings = mCurSettings = otherSettings;
				}
				else
				{
					if (!mSolutionFile.IsEmpty())
						mCurSettings->mProjectFileDir = ::Path(mSolutionFile) + L"\\";
					mProjectMap[projFile] = mCurSettings;
				}

				curSettings->mPseudoProject = true;
			}
			else
			{
				mProjects += projFile + L";";

				try
				{
					if (!Psettings || !Psettings->mCacheProjectInfo)
					{
						// no caching in AVR - always walk for toolchain devices.
						throw CacheLoadException();
					}

					if (7 == projectEngineVersion)
						throw CacheLoadException(); // no caching in vs7

					needToWalk = false;
					LoadCachedProjectSettings(curSettings, projFile);
				}
				catch (const CacheLoadException&)
				{
					if (projectEngineVersion >= 8 && Psettings && Psettings->mCacheProjectInfo)
					{
						vLog("ProjectInfoCache: invalid or not created (%s) (%s)\n", (LPCTSTR)CString(projFile),
						     (LPCTSTR)CString(::GetCacheFile(projFile)));
					}

					needToWalk = true;
					mCurSettings.reset(new ProjectSettings);
					curSettings = mCurSettings;

					mCurSettings->mProjectFile = projFile;
					::GetFTime(projFile, &mCurSettings->mProjectFileTimestamp);
					if (!projFile.IsEmpty())
					{
						if ((projFile.GetLength() - 7) == ::FindNoCase(projFile, L".shproj"))
						{
							// [case: 80931]
							// add shproj dependency as prop sheet (just checks time and content hash)
							CStringW shProj(projFile.Left(projFile.GetLength() - 7));
							shProj += L".projItems";
							if (IsFile(shProj))
								mCurSettings->mPropertySheets.Add(shProj);
						}
						else if ((projFile.GetLength() - 9) == ::FindNoCase(projFile, L".vcxitems"))
						{
							// [case: 80931]
							// add vcxitems dependency as prop sheet (just checks time and content hash)
							mSharedVcProjectSource.push_back(mCurSettings);
						}
					}
				}

				mCurSettings->mProjectFileDir = ::Path(projFile) + L"\\";

				// usually mProjectMap[projFile] will be NULL, but it could be
				// non-null if LoadCachedProjectSettings threw an exception while
				// processing multiple sub-projects.
				mProjectMap[projFile] = mCurSettings;
			}

			if (needToWalk)
			{
				bool needReferences = mCompleteInfo;
				++mWalkedProjectCnt;
				if (mCompleteInfo)
				{
					try
					{
						CComPtr<EnvDTE::ProjectItems> pItems;
						pProject->get_ProjectItems(&pItems);
						if (pItems)
							GetProjectItems(pItems, mCurSettings->mFiles, mCurSettings->mPropertySheets);
					}
					catch (const WtException& e)
					{
						WTString msg;
						msg.WTFormat("WalkProject-GetProjectItems exception caught (%d) : ", StopIt);
						msg += e.GetDesc();
						VALOGEXCEPTION(msg.c_str());
						Log(msg.c_str());
						okToSave = false;
					}
				}

#ifdef AVR_STUDIO
				WalkAvrProject(pProject);
#else
				CComPtr<IDispatch> pIdispProject;
				pProject->get_Object(&pIdispProject);
				if (pIdispProject)
				{
					CComQIPtr<VCProjectT> pVcProject{pIdispProject};
					if (pVcProject)
					{
						if (!mDidVcEnvironmentSetup && mCompleteInfo)
						{
							mDidVcEnvironmentSetup = true;
							::SetupVcEnvironment();
						}

						if (projectEngineVersion >= 10)
						{
							// [case: 68333]
							// WalkVcProject on project thread, not UI
							mCurSettings->mIsVcProject = true;
							mVcProjects.push_back(
							    std::make_pair<CAdapt<CComPtr<VCProjectT>>, CAdapt<ProjectSettingsPtr>>(pVcProject,
							                                                                            mCurSettings));

							// [case: 98091]
							needReferences = false;
						}
						else
						{
							try
							{
								WalkVcProject(pVcProject);
							}
							catch (const WtException& e)
							{
								WTString msg;
								msg.WTFormat("WalkVcProject < 10 exception caught (%d) : ", StopIt);
								msg += e.GetDesc();
								VALOGEXCEPTION(msg.c_str());
								Log(msg.c_str());
								okToSave = false;
							}
						}
					}
				}
				else
					okToSave = false;
#endif

				if (needReferences)
					GetReferences(pProject, mCurSettings->mReferences);
			}

			_ASSERTE(!mSettingsStack.empty());
			mCurSettings = mSettingsStack.front();
			mSettingsStack.pop_front();

			_ASSERTE(curSettings);
			if (mCompleteInfo && curSettings->mIsVcProject)
			{
				if (!mVcProjectItemsEventsSink || !mVcProjectItemsEventsSink->IsSynced())
					SetupProjectEventSinks(pProject);
			}

			if (okToSave && needToWalk && mCompleteInfo && Psettings && Psettings->mCacheProjectInfo)
			{
				if (!curSettings->mIsVcProject || projectEngineVersion < 10)
				{
					try
					{
						curSettings->SaveProjectCache();
					}
					catch (const CacheSaveException& e)
					{
						vLog("ProjectInfoCache: (WP) failed to save cache (%s) (%s) (%s)\n", (LPCTSTR)CString(projFile),
						     (LPCTSTR)CString(::GetCacheFile(projFile)), e.GetDesc().c_str());
					}
				}
				else
				{
					// cache is saved in FinishGetSolutionInfo
				}
			}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkProject exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkProject 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkProject");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkProject 2");
			}
		}
#endif // !SEAN
	}

	void ReadIncludeDirsFromJson(const CStringW& jsonFilepath, FileList& incDirs)
	{
		_ASSERTE(::IsFile(jsonFilepath));
		WTString txt;
		txt.ReadFile(jsonFilepath);

		namespace rapidJson = RAPIDJSON_NAMESPACE;
		rapidJson::Document jsDoc;
		if (jsDoc.Parse(txt.c_str()).HasParseError())
		{
			vLog("ERROR: failed to parse json (%s)\n", (LPCTSTR)CString(jsonFilepath));
			return;
		}

		if (!jsDoc.HasMember("configurations"))
			return;

		rapidJson::Value& cfgs = jsDoc["configurations"];
		if (!cfgs.IsArray())
			return;

		for (rapidJson::Value::ConstValueIterator cfgItr = cfgs.Begin(); cfgItr != cfgs.End(); ++cfgItr)
		{
			const rapidJson::Value& cfg = *cfgItr;
			if (!cfg.HasMember("includePath"))
				continue;

			const rapidJson::Value& incs = cfg["includePath"];
			if (!incs.IsArray())
				continue;

			for (rapidJson::Value::ConstValueIterator incItr = incs.Begin(); incItr != incs.End(); ++incItr)
			{
				const rapidJson::Value& inc = *incItr;
				if (inc.IsString())
				{
					CStringW curInc = inc.GetString();
					if (!curInc.IsEmpty())
						incDirs.AddUniqueNoCase(curInc);
				}
			}
		}
	}

	void ReadIncludeDirsFromCmakeList(const CStringW& cmakeListFile, FileList& incDirs)
	{
		// [case: 102486]
		// simple brain-dead support for trivial include_directories.
		// does not support scripted examples (like the answer in
		// http://stackoverflow.com/questions/7018185/cmake-include-subdirectories ).
		// https://blogs.msdn.microsoft.com/vcblog/2016/10/05/cmake-support-in-visual-studio/
		// https://blogs.msdn.microsoft.com/vcblog/2016/11/16/cmake-support-in-visual-studio-the-visual-studio-2017-rc-update/
		// https://cmake.org/examples/
		// http://preetisblog.com/programming/how-to-write-cmakelists-txt
		//
		// https://cmake.org/cmake/help/v3.0/command/include_directories.html
		// include_directories([AFTER|BEFORE] [SYSTEM] dir1 [dir2 ...])
		//
		// include_directories(${PCL_INCLUDE_DIRS})
		// include_directories("G:/Matlab/extern/include")

		_ASSERTE(::IsFile(cmakeListFile));
		CStringW txt;
		::ReadFileW(cmakeListFile, txt);

		std::wstring outerTxt(txt);
		// [case: 146429] exclude comments via expression: (?!\\s*#)
		// [case: 146429] exclude target_include_directories via expression: \\binclude_directories\\b
		std::wregex outerRegex(L"^(?!\\s*#)\\s*\\binclude_directories\\b\\(\\s?(?:AFTER|BEFORE)?\\s?(?:SYSTEM)?\\s?(.*)\\)",
		                       std::wregex::ECMAScript | std::wregex::optimize | std::wregex::icase);
		std::wcmatch outerMatch;

		LPCWSTR outerStartPos = outerTxt.c_str();
		LPCWSTR outerEndPos = outerStartPos + outerTxt.size();

		while (std::regex_search(outerStartPos, outerEndPos, outerMatch, outerRegex))
		{
			if (outerMatch.size() > 0 && outerMatch[1].matched)
			{
				std::wstring innerTxt(outerMatch[1].str());

				std::wregex innerRegex(L"\\s?(?:\"(.+?)\"|([$][{].+?[}]))",
				                       std::wregex::ECMAScript | std::wregex::optimize | std::wregex::icase);
				std::wcmatch innerMatch;

				LPCWSTR innerStartPos = innerTxt.c_str();
				LPCWSTR innerEndPos = innerStartPos + innerTxt.size();

				while (std::regex_search(innerStartPos, innerEndPos, innerMatch, innerRegex))
				{
					if (innerMatch.size() > 0)
					{
						std::wstring dirTxt;
						if (innerMatch[1].matched)
						{
							// quoted match does not include quotes
							dirTxt = innerMatch[1].str();
						}
						else if (innerMatch.size() > 1 && innerMatch[2].matched)
						{
							// environment variable match includes ${...}
							dirTxt = innerMatch[2].str();
						}

						if (!dirTxt.empty())
						{
							CStringW dir(dirTxt.c_str());
							int pos = dir.Find(L"\\\\");
							if (pos == 0)
								pos = dir.Find(L"\\\\", 1);
							if (-1 != pos)
								dir.Replace(L"\\\\", L"\\");
							incDirs.AddUniqueNoCase(dir);
						}
					}

					innerStartPos = innerMatch[0].second;
				}
			}

			outerStartPos = outerMatch[0].second;
		}
	}

	void ReadIncludeDirsFromProjectMarker(const CStringW& markerFile, FileList& incDirs)
	{
		const CStringW baseFile(::Basename(markerFile));
		if (!baseFile.CompareNoCase(L"CppProperties.json"))
			ReadIncludeDirsFromJson(markerFile, incDirs);
		else if (!baseFile.CompareNoCase(L"CMakeLists.txt"))
			ReadIncludeDirsFromCmakeList(markerFile, incDirs);
		else
			_ASSERTE(!"unhandled projectMarkerFile in ReadIncludeDirsFromProjectMarker");
	}

	void CreateProjectFromDirectory(const CStringW& solutionRootDir, const CStringW& dirOrMarkerFile, FileList& files)
	{
		try
		{
			std::optional<std::wregex> thirdPartyRegex;
			if (Psettings->mThirdPartyRegex[0])
			{
				try
				{
					thirdPartyRegex = std::wregex(Psettings->mThirdPartyRegex,
					                              std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
				}
				catch (const std::regex_error&)
				{
				}
			}

			TimeTrace tr("CreateProjectFromDirectory");
			mSettingsStack.push_front(mCurSettings);
			ProjectSettingsPtr curSettings(new ProjectSettings);
			mCurSettings = curSettings;

			// no caching of folder-based solutions
			CStringW solutionRootDirLower(solutionRootDir);
			solutionRootDirLower.MakeLower();
			CStringW projectRootDir;
			if (IsDir(dirOrMarkerFile))
			{
				// [case: 96662]
				// folder with no properties
				projectRootDir = dirOrMarkerFile;
				vLog("CreateProjectFromDirectory: %s\n", (LPCTSTR)CString(projectRootDir));

				if (thirdPartyRegex && std::regex_search((LPCWSTR)dirOrMarkerFile, *thirdPartyRegex))
				{
					// [case: 104857]
					if (::strchr("/\\", projectRootDir[projectRootDir.GetLength() - 1]))
						projectRootDir = projectRootDir.Left(projectRootDir.GetLength() - 1);
					vLog("  3rdparty dir (a): %s\n", (LPCTSTR)CString(projectRootDir));
					mSolutionPackageIncludes.AddUniqueNoCase(projectRootDir);

					_ASSERTE(!mSettingsStack.empty());
					mCurSettings = mSettingsStack.front();
					mSettingsStack.pop_front();
					return;
				}
			}
			else
			{
				// [case: 102080]
				// folder with properties json file
				CStringW vsInstallDir;
				_ASSERTE(IsFile(dirOrMarkerFile));
				projectRootDir = Path(dirOrMarkerFile) + L"\\";
				vLog("CreateProjectFromDirectory: %s\n", (LPCTSTR)CString(dirOrMarkerFile));

				FileList incDirs;
				ReadIncludeDirsFromProjectMarker(dirOrMarkerFile, incDirs);

				for (auto it : incDirs)
				{
					std::wstring sdir((LPCWSTR)it.mFilename);
					std::wregex re(L"\\$\\{workspaceroot\\}",
					               std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
					sdir = std::regex_replace(sdir, re, (LPCWSTR)solutionRootDir);
					re.assign(L"\\$\\{projectroot\\}",
					          std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
					sdir = std::regex_replace(sdir, re, (LPCWSTR)projectRootDir);

					re.assign(L"\\$\\{vsinstalldir\\}",
					          std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
					if (std::regex_search(sdir, re))
					{
						if (vsInstallDir.IsEmpty())
						{
							FileVersionInfo fvi;
							if (fvi.QueryFile(L"DevEnv.exe", FALSE))
							{
								// returns: foo\bar\common7\ide\devenv.exe
								vsInstallDir = fvi.GetModuleFullPathW();
								// resolve to: "foo\bar\"
								vsInstallDir = Path(vsInstallDir) + L"\\..\\..\\";
							}
						}

						sdir = std::regex_replace(sdir, re, (LPCWSTR)vsInstallDir);
					}

					// environment variables
					// convert ${env.FOODIR} to %FOODIR%
					re.assign(L"\\$\\{env\\.(\\S+)\\}",
					          std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
					sdir = std::regex_replace(sdir, re, L"%$1%");

					re.assign(L"\\%vsinstalldir%", std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
					if (std::regex_search(sdir, re))
					{
						if (vsInstallDir.IsEmpty())
						{
							FileVersionInfo fvi;
							if (fvi.QueryFile(L"DevEnv.exe", FALSE))
							{
								// returns: foo\bar\common7\ide\devenv.exe
								vsInstallDir = fvi.GetModuleFullPathW();
								// resolve to: "foo\bar\"
								vsInstallDir = Path(vsInstallDir) + L"\\..\\..\\";
							}
						}

						sdir = std::regex_replace(sdir, re, (LPCWSTR)vsInstallDir);
					}

					CStringW dir(sdir.c_str());

					// resolve relative dirs
					dir = ::BuildPath(dir, projectRootDir, false, false);

					// determine if dir should be considered platform or project
					if (thirdPartyRegex && std::regex_search((LPCWSTR)dir, *thirdPartyRegex))
					{
						// [case: 104857]
						if (::strchr("/\\", dir[dir.GetLength() - 1]))
							dir = dir.Left(dir.GetLength() - 1);
						vLog("  3rdparty dir (b): %s\n", (LPCTSTR)CString(dir));
						mSolutionPackageIncludes.AddUniqueNoCase(dir);
						continue;
					}

					bool isPlatformDir = false;
					if (Psettings->mForceProgramFilesDirsSystem && !IncludeDirs::IsCustom() &&
					    ((!kWinProgFilesDirX86.IsEmpty() &&
					      ::_wcsnicmp(kWinProgFilesDirX86, dir, (uint)kWinProgFilesDirX86.GetLength()) == 0) ||
					     (!kWinProgFilesDir.IsEmpty() &&
					      ::_wcsnicmp(kWinProgFilesDir, dir, (uint)kWinProgFilesDir.GetLength()) == 0)))
					{
						// [case: 109500]
						isPlatformDir = true;
					}

					if (isPlatformDir)
					{
						// includes dirs outside of rootDir go to PlatformIncludeDirs
						mCurSettings->mPlatformIncludeDirs.AddUniqueNoCase(dir);
					}
					else
					{
						// all other includes dirs go in ConfigFullIncludeDirs
						mCurSettings->mConfigFullIncludeDirs.AddUniqueNoCase(dir);
					}
				}
			}

			if (thirdPartyRegex && std::regex_search((LPCWSTR)projectRootDir, *thirdPartyRegex))
			{
				// [case: 104857]
				// don't add 3rd party cmakelists.txt as projects
				if (::strchr("/\\", projectRootDir[projectRootDir.GetLength() - 1]))
					projectRootDir = projectRootDir.Left(projectRootDir.GetLength() - 1);
				vLog("  3rdparty dir (c): %s\n", (LPCTSTR)CString(projectRootDir));
				mSolutionPackageIncludes.AddUniqueNoCase(projectRootDir);
			}
			else
			{
				if (-1 == mProjects.Find(dirOrMarkerFile + L";"))
					mProjects += dirOrMarkerFile + L";";

				auto it = mProjectMap.find(dirOrMarkerFile);
				if (it == mProjectMap.end())
				{
					mCurSettings->mPseudoProject = true;
					mCurSettings->mProjectFile = dirOrMarkerFile;
					mCurSettings->mProjectFileDir = projectRootDir;
					mCurSettings->mFiles.swap(files);
					mProjectMap[dirOrMarkerFile] = mCurSettings;
				}
				else
				{
					// merge mCurSettings with ProjectSettings at it
					ProjectSettingsPtr prj = it->second;
					if (!prj->mPseudoProject)
						prj->mPseudoProject = true;
					if (prj->mProjectFile != dirOrMarkerFile)
						prj->mProjectFile = dirOrMarkerFile;
					if (prj->mProjectFileDir != projectRootDir)
						prj->mProjectFileDir = projectRootDir;
					if (!files.empty())
						prj->mFiles.AddUnique(files);
					if (!mCurSettings->mConfigFullIncludeDirs.empty())
						prj->mConfigFullIncludeDirs.AddUnique(mCurSettings->mConfigFullIncludeDirs);
					if (!mCurSettings->mPlatformIncludeDirs.empty())
						prj->mPlatformIncludeDirs.AddUnique(mCurSettings->mPlatformIncludeDirs);
					if (!mCurSettings->mPlatformExternalIncludeDirs.empty())
						prj->mPlatformExternalIncludeDirs.AddUnique(mCurSettings->mPlatformExternalIncludeDirs);
				}

				++mWalkedProjectCnt;
			}

			_ASSERTE(!mSettingsStack.empty());
			mCurSettings = mSettingsStack.front();
			mSettingsStack.pop_front();
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("CreateProjectFromDirectory exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::CreateProjectFromDirectory 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::CreateProjectFromDirectory");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::CreateProjectFromDirectory 2");
			}
		}
#endif // !SEAN
	}

	void LoadCachedProjectSettings(ProjectSettingsPtr settings, const CStringW& _projPath)
	{
		_ASSERTE(Psettings && Psettings->mCacheProjectInfo);
		settings->LoadProjectCache(_projPath);

		// check sub-project dependencies
		CStringW subProjects;
		for (FileList::const_iterator it = settings->mSubProjects.begin(); it != settings->mSubProjects.end(); ++it)
		{
			const CStringW projPath((*it).mFilename);
			subProjects += projPath + L";";

			ProjectSettingsPtr curSettings(new ProjectSettings);
			// This call could throw
			LoadCachedProjectSettings(curSettings, projPath);

			_ASSERTE(curSettings->mProjectFile == projPath);
			::GetFTime(projPath, &curSettings->mProjectFileTimestamp);
			curSettings->mProjectFileDir = ::Path(projPath) + L"\\";
			_ASSERTE(!mProjectMap[projPath]);
			mProjectMap[projPath] = curSettings;
		}

		// success - now safe to update mProjects with accumulated projects
		if (!subProjects.IsEmpty())
			mProjects += subProjects;

		_ASSERTE(!settings->mProjectFile.CompareNoCase(_projPath));
		vLog("ProjectInfoCache: loaded from cache (%s) (%s)\n", (LPCTSTR)CString(settings->mProjectFile),
		     (LPCTSTR)CString(::GetCacheFile(_projPath)));
	}

	void GetProjectItems(EnvDTE::ProjectItems* pItems, FileList& projFileList, FileList& propsFileList)
	{
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			long icount, readItemCnt = 0;
			pItems->get_Count(&icount);

			CComPtr<IUnknown> pUnk;
			HRESULT hres = pItems->_NewEnum(&pUnk);
			if (SUCCEEDED(hres) && pUnk != NULL)
			{
				CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
				if (pNewEnum)
				{
					VARIANT varItem;
					VariantInit(&varItem);
					CComQIPtr<EnvDTE::ProjectItem> pItem;
					ULONG enumVarCnt = 0;
					while (pNewEnum->Next(1, &varItem, &enumVarCnt) == S_OK)
					{
						_ASSERTE(varItem.vt == VT_DISPATCH || varItem.vt == VT_UNKNOWN);
						pItem = varItem.pdispVal;
						VariantClear(&varItem);
						if (!pItem)
							continue;

						++readItemCnt;
						::GetProjectItemName(pItem, projFileList, propsFileList);
						CComPtr<EnvDTE::Project> psProject;
						pItem->get_SubProject(&psProject);
						if (psProject)
						{
							if (::IsAppropriateProject(psProject))
							{
								const CStringW projPath(GetProjectFullFilename(psProject));
								if (projPath.GetLength())
									mCurSettings->mSubProjects.Add(projPath);

								WalkProject(psProject);
							}
						}

						CComPtr<EnvDTE::ProjectItems> ppItems;
						pItem->get_ProjectItems(&ppItems);
						if (ppItems)
							GetProjectItems(ppItems, projFileList, propsFileList);
						// 					{
						// 						CComPtr<EnvDTE::FileCodeModel> fcm;
						// 						pItem->get_FileCodeModel(&fcm);
						// 						if(fcm)
						// 						{
						// 							CComPtr<EnvDTE::CodeElements> pCodeElements;
						// 							fcm->get_CodeElements(&pCodeElements);
						// 							if(pCodeElements)
						// 								ScanElements(pCodeElements);
						// 						}
						// 					}
					}
				}
			}
			else
			{
				vLog("ERROR: GetProjectItems NewEnum %lx\n", hres);
			}

			_ASSERTE(icount == readItemCnt);
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("VsSolutionInfoImpl::GetProjectItems:");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::GetProjectItems");
			}

			throw ProjectLoadException();
		}
#endif // !SEAN
	}

	void WalkVcProject(VCProjectT* pVcProject)
	{
		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
			TimeTrace tr("WalkVcProject");
			CComBSTR projectNameB;
			CStringW msg;
			bool getProjectName = !!g_loggingEnabled;
#ifdef _DEBUG
			getProjectName = true;
#endif
			if (getProjectName)
			{
				pVcProject->get_Name(&projectNameB);
				CString__FormatW(msg, L"WalkVcProject: %ls\n", (LPCWSTR)CStringW(projectNameB));
				vCatLog("Environment.Project", "%s", WTString(msg).c_str());
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
			}

			mCurSettings->mIsVcProject = true;

			// ignore FPL state if we are using the old project API, since when
			// using the old API, we can rely on the prop sheet dependencies and
			// don't need to invalidate a load from our cache
			mCurSettings->mVcFplEnabledWhenCacheCreated =
			    !Psettings->mForceUseOldVcProjectApi && GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled();

			CComBSTR projDirB;
			if (!SUCCEEDED(pVcProject->get_ProjectDirectory(&projDirB)))
				throw ProjectLoadException("VcProject query 1 failed");

			if (!projDirB)
				throw ProjectLoadException("VcProject query 2 failed");

			_ASSERTE(!CStringW(projDirB).CompareNoCase(mCurSettings->mProjectFileDir));

			CComPtr<VCCollectionT> pConfigs;
			pVcProject->get_Configurations((IDispatch**)&pConfigs);
			if (!pConfigs)
				return;

			// iterate over project configurations
			long configCnt = 0, readConfigCnt = 0;
			pConfigs->get_Count(&configCnt);
			vCatLog("Environment.Project", "WalkVcProject: %ld configs\n", configCnt);
			TimeTrace tr2("WalkVcProjectConfigs");

			CComPtr<IUnknown> pUnk;
			HRESULT hres = pConfigs->_NewEnum(&pUnk);
			if (SUCCEEDED(hres) && pUnk != NULL)
			{
				CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
				if (pNewEnum)
				{
					VARIANT varConfigTmp;
					VariantInit(&varConfigTmp);
					CComPtr<IUnknown> pConfigTmp;
					for (long cfgIdx = 1; !gShellIsUnloading && pNewEnum->Next(1, &varConfigTmp, NULL) == S_OK;
					     ++cfgIdx)
					{
						_ASSERTE(varConfigTmp.vt == VT_DISPATCH || varConfigTmp.vt == VT_UNKNOWN);
						pConfigTmp = varConfigTmp.punkVal;
						VariantClear(&varConfigTmp);
						if (!pConfigTmp)
							continue;

						CComQIPtr<VCConfigurationT> pConfig{pConfigTmp};
						if (!pConfig)
							continue;

						++readConfigCnt;
						if (mUseVcProjectConfigRuleProperties)
							WalkVcProjectConfigViaRuleProperties(pConfig, configCnt, cfgIdx);
						else
						{
							WalkVcProjectConfig(pConfig, configCnt, cfgIdx);

							if ((gShellAttr && gShellAttr->IsDevenv17OrHigher()) ||
							    (Psettings && Psettings->mForceExternalIncludeDirectories))
							{
								// [case: 145995] - not able to get external include directories by walking through the
								// config, so special function was needed that fetch it via config rule
								WalkVcProjectConfigForExternalIncludeDir(pConfig, configCnt, cfgIdx);
							}
						}

						if (Psettings->mForceVcPkgInclude)
						{
							// [case: 109042]
							// in vs2017, vcpkgIncludeDir is added as property of file rather than project as in vs2015.
							// don't hit file since it will force real project load.
							if (!mForcedVcpkgInclude)
							{
								// [case: 142493] VS2017, VS2019 and VS2022 - VA cannot find vcpkg directories
								mVcpkgIncDir = L"";
								mForcedVcpkgInclude = true;
								const CStringW root(::VcEngineEvaluate(kVcpkgIncEnvironmentVar));
								const CStringW inc(::VcEngineEvaluate(L"$(ExternalIncludePath)"));

								TokenW tokenizer(inc);
								CStringW token;

								while (tokenizer.more())
								{
									token = tokenizer.read(L";");

									if (token.Find(root) != -1)
									{
										mVcpkgIncDir = token;
										break;
									}
								}

								const CStringW root2(::VcEngineEvaluate(L"$(VcpkgManifestRoot)"));
								if (!root2.IsEmpty())
								{
									TokenW tokenizer(inc);
									CStringW token;

									while (tokenizer.more())
									{
										token = tokenizer.read(L";");

										if (token.Find(root2) != -1)
										{
											if (!mVcpkgIncDir.IsEmpty())
												mVcpkgIncDir += L";";
											mVcpkgIncDir += token;
											break;
										}
									}
								}
							}

							if (!mVcpkgIncDir.IsEmpty())
							{
								CComBSTR str(mVcpkgIncDir);
								::TokenizedAddToEitherFileListUnique(str, mCurSettings->mConfigFullIncludeDirs,
								                                     mCurSettings->mPlatformIncludeDirs,
								                                     mCurSettings->mProjectFileDir, false, false);
							}
						}
					}
				}
			}
			else
			{
				vLog("ERROR: WalkVcProject NewEnum %lx\n", hres);
			}

			_ASSERTE(configCnt == readConfigCnt);

#ifdef _DEBUG
			::OutputDebugStringW(L"  project IncludeDirectories:\n");
			for (FileList::const_iterator it = mCurSettings->mConfigFullIncludeDirs.begin();
			     it != mCurSettings->mConfigFullIncludeDirs.end(); ++it)
			{
				CString__FormatW(msg, L"    %ls\n", (LPCWSTR)(*it).mFilename);
				::OutputDebugStringW(msg);
			}

			::OutputDebugStringW(L"  project forceIncludes:\n");
			for (FileList::const_iterator it = mCurSettings->mConfigForcedIncludes.begin();
			     it != mCurSettings->mConfigForcedIncludes.end(); ++it)
			{
				CString__FormatW(msg, L"    %ls\n", (LPCWSTR)(*it).mFilename);
				::OutputDebugStringW(msg);
			}

			CString__FormatW(msg, L"  end project %ls\n", (LPCWSTR)CStringW(projectNameB));
			::OutputDebugStringW(msg);
#endif // _DEBUG
		}
		catch (const WtException&)
		{
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProject 1");
			}

			throw ProjectLoadException("WalkVcProject error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProject");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProject 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectBg(VCProjectT* pVcProject)
	{
		_ASSERTE(mUseVcProjectConfigRuleProperties);

		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
			TimeTrace tr("WalkVcProjectBg");
#ifdef _DEBUG
			CComBSTR projectNameB;
			pVcProject->get_Name(&projectNameB);
#endif // _DEBUG

			_ASSERTE(mCurSettings->mIsVcProject);

			CComPtr<VCCollectionT> pConfigs;
			pVcProject->get_Configurations((IDispatch**)&pConfigs);
			if (!pConfigs)
				return;

			// iterate over project configurations
			long configCnt = 0, readConfigCnt = 0;
			pConfigs->get_Count(&configCnt);
			vCatLog("Environment.Project", "WalkVcProjectBg: %ld configs\n", configCnt);
			TimeTrace tr2("WalkVcProjectConfigsBg");

			CComPtr<IUnknown> pUnk;
			HRESULT hres = pConfigs->_NewEnum(&pUnk);
			if (SUCCEEDED(hres) && pUnk != nullptr)
			{
				CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
				if (pNewEnum)
				{
					VARIANT varConfigTmp;
					VariantInit(&varConfigTmp);
					CComPtr<IUnknown> pConfigTmp;
					for (long cfgIdx = 1; !gShellIsUnloading && pNewEnum->Next(1, &varConfigTmp, nullptr) == S_OK;
					     ++cfgIdx)
					{
						_ASSERTE(varConfigTmp.vt == VT_DISPATCH || varConfigTmp.vt == VT_UNKNOWN);
						pConfigTmp = varConfigTmp.punkVal;
						VariantClear(&varConfigTmp);
						if (!pConfigTmp)
							continue;

						CComQIPtr<VCConfigurationT> pConfig{pConfigTmp};
						if (!pConfig)
							continue;

						++readConfigCnt;
						WalkVcProjectImports(pConfig, configCnt, cfgIdx);
					}
				}
			}
			else
			{
				vLog("ERROR: WalkVcProjectBg NewEnum %lx\n", hres);
			}

			_ASSERTE(configCnt == readConfigCnt);
		}
		catch (const WtException&)
		{
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectBg 1");
			}

			throw ProjectLoadException("WalkVcProjectBg error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectBg");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectBg 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfig(VCConfigurationT* pConfig, long totalConfigs, long curCfgIdx)
	{
		// [case: 68124]
		// skip iterating over every config if there are more than 50 projects
		// in solution.  but always do the first 4.
		if (!(curCfgIdx < 5 || (mRootLevelProjectCnt < 50 && totalConfigs < 40 && mWalkedProjectCnt < 50)))
		{
			vCatLog("Environment.Project", "WalkVcProjectConfig: skip\n");
			return;
		}

		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
#ifdef _DEBUG
			CComBSTR configNameB;
			pConfig->get_Name(&configNameB); // (Debug|Win32)
			CStringW msg;
			CString__FormatW(msg, L"cfg: %ls\n", (LPCWSTR)CStringW(configNameB));
			::OutputDebugStringW(msg);
#endif

			if (mCompleteInfo)
			{
				_ASSERTE(mCurSettings);
				CComBSTR intermedDirB;
				pConfig->get_IntermediateDirectory(&intermedDirB);
				if (intermedDirB.Length())
				{
					pConfig->Evaluate(intermedDirB, &intermedDirB);
					if (intermedDirB.Length())
					{
						CStringW intermedDir =
						    ::BuildPath(CStringW(intermedDirB), mCurSettings->mProjectFileDir, false, false);
						if (!intermedDir.IsEmpty())
						{
							mCurSettings->mConfigIntermediateDirs.AddUniqueNoCase(intermedDir);
#ifdef _DEBUG
							CString__FormatW(msg, L"  cfg IntermediateDirectory: %ls\n", (LPCWSTR)intermedDir);
							::OutputDebugStringW(msg);
#endif // _DEBUG
						}
					}
				}
			}

			// Config -> Platform
			CComPtr<IDispatch> pVCPlatformTmp;
			pConfig->get_Platform(&pVCPlatformTmp);
			if (pVCPlatformTmp)
			{
				CComQIPtr<VCPlatformT> pVCPlatform{pVCPlatformTmp};
				if (pVCPlatform)
					WalkVcProjectConfigPlatform(pVCPlatform, totalConfigs, curCfgIdx);
			}

			bool collectedViaIntellisense = false;
			if (projectEngineVersion >= 15 && GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled() &&
			    Psettings && !Psettings->mForceUseOldVcProjectApi)
			{
				// [case: 102486]
				// #todoCase102486 might also need to check state of default intellisense??
				collectedViaIntellisense = CollectViaIntellisense(pConfig);
			}

			if (!collectedViaIntellisense)
			{
				// Config -> Tools Collection -> Compiler and NMake Tools
				CComPtr<IDispatch> pToolsTmp;
				pConfig->get_Tools(&pToolsTmp);
				if (pToolsTmp)
				{
					CComQIPtr<VCCollectionT> pCollection{pToolsTmp};
					if (pCollection)
						WalkVcProjectConfigTools(pConfig, pCollection);
				}

				WalkVcProjectConfigSheets(pConfig);
			}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectConfig exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfig 1");
			}

			throw ProjectLoadException("WalkVcProjectConfig error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfig");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfig 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigPlatform(VCPlatformT* pVCPlatform, long totalConfigs, long cfgIdx)
	{
		try
		{
			CComBSTR platformNameB;
			if (!SUCCEEDED(pVCPlatform->get_Name(&platformNameB)))
			{
				vLogUnfiltered("ERROR: WalkVcProjectConfigPlatform: get_Name failed cfgIdx (%ld)\n", cfgIdx);
				throw ProjectLoadException("WalkVcProjectConfigPlatform get_Name failed");
			}

			const CStringW platformNameW(platformNameB);

			if (cfgIdx > 1 && mRootLevelProjectCnt > 40 && totalConfigs > 10 && mWalkedProjectCnt > 40 &&
			    mLastVcProjectConfigPlatform == platformNameW)
			{
				// [case: 68124] [case: 78821]
				vCatLog("Environment.Project", "WalkVcProjectConfigPlatform: repeat platform (%s) cfgIdx (%ld)\n",
				     (LPCTSTR)CString(platformNameW), cfgIdx);
				return;
			}

			mLastVcProjectConfigPlatform = platformNameW;

			CComBSTR tmp, incpath;
			pVCPlatform->get_IncludeDirectories(&tmp);
			if (tmp.Length())
			{
				pVCPlatform->Evaluate(tmp, &incpath);
				::TokenizedAddToFileList(incpath, mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
				                         false, false);
				tmp.Empty();
			}

			CComBSTR libDirs;
			pVCPlatform->get_LibraryDirectories(&tmp);
			if (tmp.Length())
			{
				pVCPlatform->Evaluate(tmp, &libDirs);
				::TokenizedAddToFileList(libDirs, mCurSettings->mPlatformLibraryDirs, mCurSettings->mProjectFileDir,
				                         false, false);
				tmp.Empty();
			}

			CComBSTR srcDirs;
			pVCPlatform->get_SourceDirectories(&tmp);
			if (tmp.Length())
			{
				pVCPlatform->Evaluate(tmp, &srcDirs);
				::TokenizedAddToFileList(srcDirs, mCurSettings->mPlatformSourceDirs, mCurSettings->mProjectFileDir,
				                         false, false);
				tmp.Empty();
			}

			bool logPlatform = !!g_loggingEnabled;
#ifdef _DEBUG
			logPlatform = true;
#endif
			if (logPlatform)
			{
				CStringW msg;
				CString__FormatW(msg, L"platform: %s\n", (LPCWSTR)platformNameW);
				CatLog("Environment.Project", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"	platform IncludeDirectories: %s\n", (LPCWSTR)CStringW(incpath));
				vCatLog("Environment.Directories", "%s", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"  platform LibraryDirectories: %s\n", (LPCWSTR)CStringW(libDirs));
				vCatLog("Environment.Directories", "%s", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"  platform SourceDirectories: %s\n", (LPCWSTR)CStringW(srcDirs));
				vCatLog("Environment.Directories", "%s", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
			}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectConfigPlatform exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigPlatform 1");
			}

			throw ProjectLoadException("WalkVcProjectConfigPlatform error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigPlatform");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigPlatform 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigPlatformProperties(VCRulePropertyStorageT* pStore, CStringW platformName, long totalConfigs,
	                                           long cfgIdx)
	{
		try
		{
			if (cfgIdx > 1 && mRootLevelProjectCnt > 40 && totalConfigs > 10 && mWalkedProjectCnt > 40 &&
			    mLastVcProjectConfigPlatform == platformName)
			{
				// [case: 68124] [case: 78821]
				vCatLog("Environment.Project", "WalkVcProjectConfigPlatformProperties: repeat platform (%s) cfgIdx (%ld)\n",
				     (LPCTSTR)CString(platformName), cfgIdx);
				return;
			}

			mLastVcProjectConfigPlatform = platformName;

			CComBSTR incpath;
			pStore->GetEvaluatedPropertyValue(CComBSTR(L"IncludePath"), &incpath);
			if (incpath.Length())
				::TokenizedAddToFileList(incpath, mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
				                         false, false);

			CComBSTR libDirs;
			pStore->GetEvaluatedPropertyValue(CComBSTR(L"LibraryPath"), &libDirs);
			if (libDirs.Length())
				::TokenizedAddToFileList(libDirs, mCurSettings->mPlatformLibraryDirs, mCurSettings->mProjectFileDir,
				                         false, false);

			CComBSTR srcDirs;
			pStore->GetEvaluatedPropertyValue(CComBSTR(L"SourcePath"), &srcDirs);
			if (srcDirs.Length())
				::TokenizedAddToFileList(srcDirs, mCurSettings->mPlatformSourceDirs, mCurSettings->mProjectFileDir,
				                         false, false);

			if ((gShellAttr && gShellAttr->IsDevenv17OrHigher()) ||
			    (Psettings && Psettings->mForceExternalIncludeDirectories))
			{
				// [case: 145995]
				CComBSTR extIncPath;
				pStore->GetEvaluatedPropertyValue(CComBSTR(L"ExternalIncludePath"), &extIncPath);
				if (extIncPath.Length())
					::TokenizedAddToFileList(extIncPath, mCurSettings->mPlatformExternalIncludeDirs,
					                         mCurSettings->mProjectFileDir, false, false);
			}

			bool logPlatform = !!g_loggingEnabled;
#ifdef _DEBUG
			logPlatform = true;
#endif
			if (logPlatform)
			{
				CStringW msg;
				CString__FormatW(msg, L"platform: %s (props)\n", (LPCWSTR)platformName);
				CatLog("Environment.Project", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"  platform IncludeDirectories: %s\n", (LPCWSTR)CStringW(incpath));
				LOGBYSPLITTING("Environment.Directories", msg); // [case: 146245]
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"  platform LibraryDirectories: %s\n", (LPCWSTR)CStringW(libDirs));
				vCatLog("Environment.Directories", "%s", (const char*)CString(msg));
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
				CString__FormatW(msg, L"  platform SourceDirectories: %s\n", (LPCWSTR)CStringW(srcDirs));
				LOGBYSPLITTING("Environment.Directories", msg); // [case: 146245]
#ifdef _DEBUG
				::OutputDebugStringW(msg);
#endif
			}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectConfigPlatformProperties exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigPlatformProperties 1");
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigPlatformProperties");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigPlatformProperties 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigTools(VCConfigurationT* pConfig, VCCollectionT* pCollection)
	{
		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
			long colCnt = 0;
			pCollection->get_Count(&colCnt);
			for (long idx = 0; !gShellIsUnloading && idx < colCnt; ++idx)
			{
				CComPtr<IDispatch> curTool;
				pCollection->Item(CComVariant(idx + 1), &curTool);
				if (!curTool)
					continue;

				// Config -> Tools Collection -> Compiler Tool
				CComQIPtr<VCCLCompilerToolT> pTool{curTool};
				if (pTool)
				{
					WalkVcProjectConfigClTool(pConfig, pTool);
				}
				else
				{
					// Config -> Tools Collection -> NMake Tool
					CComQIPtr<VCNMakeToolT> pNmTool{curTool};
					if (pNmTool)
						WalkVcProjectConfigNmTool(pConfig, pNmTool);
				}
			}
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfigTools 1");
			}

			throw ProjectLoadException("WalkVcProjectConfigTools error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigTools");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfigTools 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigClTool(VCConfigurationT* pConfig, VCCLCompilerToolT* pTool)
	{
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.vcprojectengine.vcclcompilertool%28VS.80%29.aspx
		if (gShellIsUnloading)
			throw UnloadingException();

		CComBSTR tmp;
		CComBSTR incPathB;
		pTool->get_FullIncludePath(&tmp);
		if (tmp.Length())
		{
			pConfig->Evaluate(tmp, &incPathB);
			::TokenizedAddToEitherFileListUnique(incPathB, mCurSettings->mConfigFullIncludeDirs,
			                                     mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
			                                     false, false);
			tmp.Empty();
		}

		if (projectEngineVersion < 10)
		{
			CComBSTR forcedIncsB;
			HRESULT res = pTool->get_ForcedIncludeFiles(&tmp);
			if (tmp.Length())
			{
				pConfig->Evaluate(tmp, &forcedIncsB);
				::TokenizedAddToFileList(forcedIncsB, mCurSettings->mConfigForcedIncludes,
				                         mCurSettings->mProjectFileDir);
				tmp.Empty();
			}

			CComBSTR usingDirsB;
			res = pTool->get_AdditionalUsingDirectories(&tmp);
			if (tmp.Length())
			{
				pConfig->Evaluate(tmp, &usingDirsB);
				::TokenizedAddToFileList(usingDirsB, mCurSettings->mConfigUsingDirs, mCurSettings->mProjectFileDir,
				                         false, false);
				tmp.Empty();
			}

			if (mCompleteInfo)
			{
				CComBSTR forcedUsings;
				res = pTool->get_ForcedUsingFiles(&tmp);
				if (tmp.Length())
				{
					pConfig->Evaluate(tmp, &forcedUsings);
					::TokenizedAddToFileList(forcedUsings, mCurSettings->mReferences, mCurSettings->mProjectFileDir);
					tmp.Empty();
				}
			}
		}

		WalkVcRulePropertyStorage(pTool);

		if (projectEngineVersion < 14)
		{
			VARIANT_BOOL ignoreStd(VARIANT_FALSE);
			pTool->get_IgnoreStandardIncludePath(&ignoreStd);
			if (VARIANT_TRUE == ignoreStd)
				mCurSettings->mCppIgnoreStandardIncludes = true;
		}

		const bool isManaged = IsVcProjectConfigManaged<VCCLCompilerToolT>(pTool);
		if (isManaged)
			mCurSettings->mCppHasManagedConfig = true;
	}

	void WalkVcProjectConfigNmTool(VCConfigurationT* pConfig, VCNMakeToolT* pTool)
	{
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.vcprojectengine.vcnmaketool_members%28VS.80%29.aspx
		CComBSTR tmp;
		CComBSTR incPathB;
		pTool->get_IncludeSearchPath(&tmp);
		if (tmp.Length())
		{
			pConfig->Evaluate(tmp, &incPathB);
			::TokenizedAddToEitherFileListUnique(incPathB, mCurSettings->mConfigFullIncludeDirs,
			                                     mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
			                                     false, false);
			tmp.Empty();
		}

		if (projectEngineVersion < 10) // template constant
		{
			CComBSTR forcedIncsB;
			pTool->get_ForcedIncludes(&tmp);
			if (tmp.Length())
			{
				pConfig->Evaluate(tmp, &forcedIncsB);
				::TokenizedAddToFileList(forcedIncsB, mCurSettings->mConfigForcedIncludes,
				                         mCurSettings->mProjectFileDir);
				tmp.Empty();
			}
		}

		WalkVcRulePropertyStorage(pTool);

		const bool isManaged = IsVcProjectConfigManaged<VCNMakeToolT>(pTool);
		if (isManaged)
			mCurSettings->mCppHasManagedConfig = true;
	}

	void WalkVcRulePropertyStorage(IUnknown* curTool)
	{
		if (projectEngineVersion < 10)
			return;

		if (gShellIsUnloading)
			throw UnloadingException();

		CComQIPtr<VCRulePropertyStorageT> pPropStore{curTool};
		if (!pPropStore)
			return;

		CComBSTR propB, propName;
		propName = L"ForcedIncludeFiles";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			::TokenizedAddToFileList(propB, mCurSettings->mConfigForcedIncludes, mCurSettings->mProjectFileDir);
			propB.Empty();
		}

		propName = L"ForcedUsingFiles";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			::TokenizedAddToFileList(propB, mCurSettings->mReferences, mCurSettings->mProjectFileDir);
			propB.Empty();
		}

		if (projectEngineVersion > 10)
		{
			propName = L"NMAKEForcedIncludes";
			pPropStore->GetEvaluatedPropertyValue(propName, &propB);
			if (propB.Length())
			{
				::TokenizedAddToFileList(propB, mCurSettings->mConfigForcedIncludes, mCurSettings->mProjectFileDir);
				propB.Empty();
			}
		}

		propName = L"AdditionalIncludeDirectories";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			if (projectEngineVersion >= 14)
				::TokenizedAddToEitherFileListUnique(propB, mCurSettings->mConfigFullIncludeDirs,
				                                     mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
				                                     false, false);
			else
				::TokenizedAddToFileListUnique(propB, mCurSettings->mConfigFullIncludeDirs,
				                               mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir, false,
				                               false);
			propB.Empty();
		}

		propName = L"AdditionalUsingDirectories";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			::TokenizedAddToFileList(propB, mCurSettings->mConfigUsingDirs, mCurSettings->mProjectFileDir, false,
			                         false);
			propB.Empty();
		}

		if (projectEngineVersion > 10)
		{
			propName = L"NMAKEIncludeSearchPath";
			pPropStore->GetEvaluatedPropertyValue(propName, &propB);
			if (propB.Length())
			{
				if (projectEngineVersion >= 14)
					::TokenizedAddToEitherFileListUnique(propB, mCurSettings->mConfigFullIncludeDirs,
					                                     mCurSettings->mPlatformIncludeDirs,
					                                     mCurSettings->mProjectFileDir, false, false);
				else
					::TokenizedAddToFileListUnique(propB, mCurSettings->mConfigFullIncludeDirs,
					                               mCurSettings->mPlatformIncludeDirs, mCurSettings->mProjectFileDir,
					                               false, false);
				propB.Empty();
			}

			propName = L"CompileAsWinRT";
			pPropStore->GetEvaluatedPropertyValue(propName, &propB);
			if (propB.Length())
			{
				// in theory one proj config could use WinRT, and another doesn't.
				propB.ToLower();
				if (propB == L"true" || propB == L"1")
					mCurSettings->mCppIsWinRT = true;
				propB.Empty();
			}
		}

		propName = L"CompileAsManaged";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			propB.ToLower();
			if (propB != L"false" && propB != L"0" && propB != L"notset")
				mCurSettings->mCppHasManagedConfig = true;
			propB.Empty();
		}

		if (projectEngineVersion >= 14)
		{
			propName = L"IgnoreStandardIncludePath";
			pPropStore->GetEvaluatedPropertyValue(propName, &propB);
			if (propB.Length())
			{
				propB.ToLower();
				if (propB == L"true" || propB == L"1")
					mCurSettings->mCppIgnoreStandardIncludes = true;
				propB.Empty();
			}
		}

	    // Retrieve the PrecompiledHeader property
		propName = L"PrecompiledHeader";
		pPropStore->GetEvaluatedPropertyValue(propName, &propB);
		if (propB.Length())
		{
			propB.ToLower();
			if (propB == L"use")
			{
				// If precompiled headers are being used, retrieve the precompiled header file
				propName = L"PrecompiledHeaderFile";
				pPropStore->GetEvaluatedPropertyValue(propName, &propB);
				if (propB.Length())
				{
					mCurSettings->mPrecompiledHeaderFile = propB.m_str;
				}
			}
			propB.Empty();
		}
	}

	void WalkVcProjectConfigViaRuleProperties(VCConfigurationT* pConfig, long totalConfigs, long curCfgIdx)
	{
		_ASSERTE(projectEngineVersion >= 14);
		// [case: 68124]
		// skip iterating over every config if there are more than 50 projects
		// in solution.  but always do the first 4.
		if (!(curCfgIdx < 5 || (mRootLevelProjectCnt < 50 && totalConfigs < 40 && mWalkedProjectCnt < 50)))
		{
			vCatLog("Environment.Project", "WalkVcProjectConfigViaRuleProperties: skip\n");
			return;
		}

		try
		{
			CComBSTR configNameB;
			pConfig->get_Name(&configNameB); // (Debug|Win32)
#ifdef _DEBUG
			CStringW msg;
			CString__FormatW(msg, L"cfg: %ls\n", (LPCWSTR)CStringW(configNameB));
			::OutputDebugStringW(msg);
#endif

			_ASSERTE(mCurSettings);
			CComPtr<VCCollectionT> pRules;
			pConfig->get_Rules((VCCollectionT**)&pRules);
			if (!pRules)
			{
				mCurSettings->mVcMissingConfigs = true;
				vLogUnfiltered("ERROR: WalkVcProjectConfigViaRuleProperties failed to get rules collection %s\n",
				     (LPCTSTR)CString(configNameB));
				return;
			}

			HRESULT res;
			CComVariant itemName(L"");
			CComPtr<IDispatch> pRuleDisp;
			CComBSTR platformName;

			// C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V140\1033\general.xml
			itemName = L"ConfigurationGeneral";
			res = pRules->Item(itemName, &pRuleDisp);
			if (pRuleDisp)
			{
				CComQIPtr<VCRulePropertyStorageT> pPropStore{pRuleDisp};
				if (pPropStore)
				{
					CComBSTR propName;
					propName = L"Platform";
					pPropStore->GetEvaluatedPropertyValue(propName, &platformName);

					// VCNMakeTool.CompileAsManaged is accessed as property in ConfigurationGeneral rule (unlike
					// VCCLCompilerTool.CompileAsManaged)
					CComBSTR propB;
					propName = L"CompileAsManaged";
					pPropStore->GetEvaluatedPropertyValue(propName, &propB);
					if (propB.Length())
					{
						propB.ToLower();
						if (propB != L"false" && propB != L"0" && propB != L"notset")
							mCurSettings->mCppHasManagedConfig = true;
						propB.Empty();
					}

					if (mCompleteInfo)
					{
						CComBSTR intDirB;
						propName = L"IntDir";
						pPropStore->GetEvaluatedPropertyValue(propName, &intDirB);
						if (intDirB.Length())
						{
							CStringW intermedDir =
							    ::BuildPath(CStringW(intDirB), mCurSettings->mProjectFileDir, false, false);
							if (!intermedDir.IsEmpty())
							{
								mCurSettings->mConfigIntermediateDirs.AddUniqueNoCase(intermedDir);
#ifdef _DEBUG
								CString__FormatW(msg, L"  cfg IntermediateDirectory: %ls\n", (LPCWSTR)intermedDir);
								::OutputDebugStringW(msg);
#endif // _DEBUG
							}
						}
					}
				}

				pRuleDisp.Release();
			}
			else
				vLogUnfiltered("ERROR: WalkVcProjectConfigViaRuleProperties failed to get ConfigurationGeneral %s\n",
				     (LPCTSTR)CString(configNameB));

			itemName = L"ConfigurationDirectories";
			res = pRules->Item(itemName, &pRuleDisp);
			if (pRuleDisp)
			{
				CComQIPtr<VCRulePropertyStorageT> pPropStore{pRuleDisp};
				if (pPropStore)
					WalkVcProjectConfigPlatformProperties(pPropStore, CStringW(platformName), totalConfigs, curCfgIdx);

				pRuleDisp.Release();
			}
			else
				vLogUnfiltered("ERROR: WalkVcProjectConfigViaRuleProperties failed to get ConfigurationDirectories for %s\n",
				     (LPCTSTR)CString(configNameB));

			itemName = L"CL";
			res = pRules->Item(itemName, &pRuleDisp);
			if (pRuleDisp)
			{
				CComQIPtr<VCRulePropertyStorageT> pPropStore{pRuleDisp};
				if (pPropStore)
					WalkVcRulePropertyStorage(pPropStore);

				pRuleDisp.Release();
			}
			else
				vLogUnfiltered("WARN: WalkVcProjectConfigViaRuleProperties failed to get CL properties for %s\n",
				     (LPCTSTR)CString(configNameB));

			try
			{
				itemName = L"ConfigurationNMake";
				res = pRules->Item(itemName, &pRuleDisp);
				if (pRuleDisp)
				{
					CComQIPtr<VCRulePropertyStorageT> pPropStore{pRuleDisp};
					if (pPropStore)
						WalkVcRulePropertyStorage(pPropStore);

					pRuleDisp.Release();
				}
				else
					vLogUnfiltered("WARN: WalkVcProjectConfigViaRuleProperties failed to get NMake properties for %s\n",
					     (LPCTSTR)CString(configNameB));
			}
			catch (const _com_error& /*e*/)
			{
				vLogUnfiltered("WARN: WalkVcProjectConfigViaRuleProperties failed to get NMake properties for %s (2)\n",
				     (LPCTSTR)CString(configNameB));
			}

			//		if (mUseVcProjectConfigRuleProperties)
			// 		{
			// 			// property sheets are needed for cache invalidation,
			// 			// but query of propertysheets causes load of real (not read-only) project in vs2017.
			// 			WalkVcProjectConfigSheets(pConfig);
			// 		}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectConfigViaRuleProperties exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigViaRuleProperties 1");
			}

			throw ProjectLoadException("WalkVcProjectConfigViaRuleProperties error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigViaRuleProperties");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigViaRuleProperties 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectImports(VCConfigurationT* pConfig, long totalConfigs, long curCfgIdx)
	{
		_ASSERTE(projectEngineVersion >= 14);
		// this needs to happen on a background thread
		_ASSERTE(GetCurrentThreadId() != g_mainThread);
		// [case: 68124]
		// skip iterating over every config if there are more than 50 projects
		// in solution.  but always do the first 4.
		if (!(curCfgIdx < 5 || (mRootLevelProjectCnt < 50 && totalConfigs < 40 && mWalkedProjectCnt < 50)))
		{
			vCatLog("Environment.Project", "WalkVcProjectImports: skip\n");
			return;
		}

		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
			_ASSERTE(mCurSettings);
			// [case: 103729]
			bool gotImports = false;
			CComQIPtr<VCConfiguration2T> pConfig2{pConfig};
			if (pConfig2)
			{
				// __int64 mVcEvaluationId;  pConfig2->get_EvaluationId(vcEvaluationId)

				SAFEARRAY* arr = nullptr;
				HRESULT hr = pConfig2->get_AllImports(&arr);
				if (S_OK == hr)
				{
					gotImports = true;
					if (arr)
					{
						const UINT dims = ::SafeArrayGetDim(arr);
						for (long idx = 0; idx < (long)dims; ++idx)
						{
							CComBSTR importFilePathBstr;
							hr = ::SafeArrayGetElement(arr, &idx, &importFilePathBstr);
							if (S_OK != hr || !importFilePathBstr)
								continue;

							if (!importFilePathBstr.Length())
								continue;

							::TokenizedAddToFileList(importFilePathBstr, mCurSettings->mPropertySheets,
							                         mCurSettings->mProjectFileDir);
#ifdef _DEBUG
							{
								CStringW msg;
								CString__FormatW(msg, L"  import: %ls\n", (LPCWSTR)CStringW(importFilePathBstr));
								::OutputDebugStringW(msg);
							}
#endif
						}

						hr = ::SafeArrayDestroy(arr);
#if defined(_DEBUG)
						CStringW msg;
						CString__FormatW(msg, L"SafeArraryDestroy returned %lx\n", hr);
						OutputDebugStringW(msg);
#endif
					}
				}
			}

			if (!gotImports)
			{
				// property sheets are needed for cache invalidation,
				// but query of propertysheets causes load of real (not read-only) project in vs2017 pre-update 3.
				// WalkVcProjectConfigSheets(pConfig);
				vLog("ERROR: WalkVcProjectImports failed to get imports from VcConfig2");
			}
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectImports exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectImports 1");
			}

			throw ProjectLoadException("WalkVcProjectImports error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectImports");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectImports 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigSheets(VCConfigurationT* pConfig)
	{
		switch (projectEngineVersion)
		{
		case 7:
			// VC7 does not support property sheets
			// does support style sheets (renamed for vs2005:
			// http://msdn.microsoft.com/en-us/library/4ac9ya6b%28VS.80%29.aspx ) StyleSheet API is different enough
			// that I won't bother
			break;

		case 8:
		case 9: {
			CComBSTR inheritedSheetsB;
			pConfig->get_InheritedPropertySheets(&inheritedSheetsB);
			::TokenizedAddToFileList(inheritedSheetsB, mCurSettings->mPropertySheets, mCurSettings->mProjectFileDir);

#ifdef _DEBUG
			CStringW msg;
			CString__FormatW(msg, L"  cfg inherited: %ls\n", (LPCWSTR)CStringW(inheritedSheetsB));
			::OutputDebugStringW(msg);
#endif // _DEBUG
		}
			// fall-through
		default: {
			CComPtr<IDispatch> pSheetsTmp;
			pConfig->get_PropertySheets(&pSheetsTmp);
			if (pSheetsTmp)
			{
				CComQIPtr<VCCollectionT> pCollection{pSheetsTmp};
				if (pCollection)
					WalkVcProjectConfigSheets(pConfig, pCollection);
			}
		}
		}
	}

	void WalkVcProjectConfigSheets(VCConfigurationT* pConfig, VCCollectionT* pCollection)
	{
		if (gShellIsUnloading)
			throw UnloadingException();

		try
		{
			long colCnt = 0;
			pCollection->get_Count(&colCnt);
			for (long idx = 0; !gShellIsUnloading && idx < colCnt; ++idx)
			{
				CComPtr<IDispatch> curIdisp;
				pCollection->Item(CComVariant(idx + 1), &curIdisp);
				if (!curIdisp)
					continue;

				CComQIPtr<VCPropertySheetT> pPropSheet{curIdisp};
				if (!pPropSheet)
					continue;

				CComBSTR propShtName;
				pPropSheet->get_PropertySheetFile(&propShtName);
				::TokenizedAddToFileList(propShtName, mCurSettings->mPropertySheets, mCurSettings->mProjectFileDir);
#ifdef _DEBUG
				{
					CStringW msg;
					CString__FormatW(msg, L"  propSheet: %ls\n", (LPCWSTR)CStringW(propShtName));
					::OutputDebugStringW(msg);
				}
#endif // _DEBUG

				if (projectEngineVersion < 10)
				{
					CComBSTR inheritedSheetsB;
					pPropSheet->get_InheritedPropertySheets(&inheritedSheetsB);
					::TokenizedAddToFileList(inheritedSheetsB, mCurSettings->mPropertySheets,
					                         mCurSettings->mProjectFileDir);
#ifdef _DEBUG
					CStringW msg;
					CString__FormatW(msg, L"    inherited: %ls\n", (LPCWSTR)CStringW(inheritedSheetsB));
					::OutputDebugStringW(msg);
#endif // _DEBUG
				}

				CComPtr<IDispatch> pSheetsTmp;
				pPropSheet->get_PropertySheets(&pSheetsTmp);
				if (pSheetsTmp)
				{
					CComQIPtr<VCCollectionT> pMoreSheets{pSheetsTmp};
					if (pMoreSheets)
						WalkVcProjectConfigSheets(pConfig, pMoreSheets);
				}

				if (projectEngineVersion < 10)
				{
					CComPtr<IDispatch> pToolsTmp;
					pPropSheet->get_Tools(&pToolsTmp);
					if (pToolsTmp)
					{
						CComQIPtr<VCCollectionT> pToolsCollection{pToolsTmp};
						if (pToolsCollection)
							WalkVcProjectConfigTools(pConfig, pToolsCollection);
					}
				}
			}
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfigSheets 1");
			}

			throw ProjectLoadException("WalkVcProjectConfigSheets error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigSheets");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(
				    !"Fix the bad code that caused this exception in VsSolutionInfoImpl::WalkVcProjectConfigSheets 2");
			}
		}
#endif // !SEAN
	}

	void WalkVcProjectConfigForExternalIncludeDir(VCConfigurationT* pConfig, long totalConfigs, long curCfgIdx)
	{
		_ASSERTE(projectEngineVersion >= 14);
		// [case: 68124]
		// skip iterating over every config if there are more than 50 projects
		// in solution.  but always do the first 4.
		if (!(curCfgIdx < 5 || (mRootLevelProjectCnt < 50 && totalConfigs < 40 && mWalkedProjectCnt < 50)))
		{
			vCatLog("Environment.Project", "WalkVcProjectConfigForExternalIncludeDir: skip\n");
			return;
		}

		try
		{
			CComBSTR configNameB;
			pConfig->get_Name(&configNameB); // (Debug|Win32)
#ifdef _DEBUG
			CStringW msg;
			CString__FormatW(msg, L"cfg: %ls\n", (LPCWSTR)CStringW(configNameB));
			::OutputDebugStringW(msg);
#endif

			_ASSERTE(mCurSettings);
			CComPtr<VCCollectionT> pRules;
			pConfig->get_Rules((VCCollectionT**)&pRules);
			if (!pRules)
			{
				vLog("ERROR: WalkVcProjectConfigForExternalIncludeDir failed to get rules collection %s\n",
				     (LPCTSTR)CString(configNameB));
				return;
			}

			HRESULT res;
			CComVariant itemName(L"");
			CComPtr<IDispatch> pRuleDisp;
			CComBSTR platformName;

			itemName = L"ConfigurationDirectories";
			res = pRules->Item(itemName, &pRuleDisp);
			if (pRuleDisp)
			{
				CComQIPtr<VCRulePropertyStorageT> pPropStore{pRuleDisp};
				if (pPropStore)
				{
					CComBSTR extIncPath;
					pPropStore->GetEvaluatedPropertyValue(CComBSTR(L"ExternalIncludePath"), &extIncPath);
					if (extIncPath.Length())
						::TokenizedAddToFileList(extIncPath, mCurSettings->mPlatformExternalIncludeDirs,
						                         mCurSettings->mProjectFileDir, false, false);
				}

				pRuleDisp.Release();
			}
			else
				vLog("ERROR: WalkVcProjectConfigForExternalIncludeDir failed to get ConfigurationDirectories for %s\n",
				     (LPCTSTR)CString(configNameB));
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("WalkVcProjectConfigForExternalIncludeDir exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			throw;
		}
		catch (const _com_error& e)
		{
			CString sMsg;
			CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
			                 __FUNCTION__);

			VALOGEXCEPTION(sMsg);
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigForExternalIncludeDir 1");
			}

			throw ProjectLoadException("WalkVcProjectConfigForExternalIncludeDir error");
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::WalkVcProjectConfigForExternalIncludeDir");
			if (!Psettings->m_catchAll)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in "
				          "VsSolutionInfoImpl::WalkVcProjectConfigForExternalIncludeDir 2");
			}
		}
#endif // !SEAN
	}

	void UpdateReferences()
	{
		for (ProjectSettingsMap::iterator it = mProjectMap.begin(); it != mProjectMap.end(); ++it)
		{
			ProjectSettingsPtr cur = (*it).second;
			if (cur->mReferences.size())
			{
				// [case: 100735]
				// changed to AddUnique because if Faster Project Load is enabled,
				// then FinishGetSolutionInfo is called twice --
				// once on ui thread, once on project loader thread.
				mReferences.AddUnique(cur->mReferences);
			}
		}
	}

	void SetupProjectEventSinks(EnvDTE::Project* pProject)
	{
		if (!Psettings->mEnableVcProjectSync ||
		    (gShellAttr && gShellAttr->IsDevenv15() && !gShellAttr->IsDevenv15u3OrHigher() &&
		     !Psettings->mForceVs2017ProjectSync && // [case: 104119]
		     GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled()))
		{
			// [case: 103990]
			vLog("WARN: no vc project event sync -- case 103990/case 104119");
			return;
		}

		_ASSERTE(pProject);
		_ASSERTE(!mVcProjectItemsEventsSink);
		CComPtr<IDispatch> pIdispProject;
		pProject->get_Object(&pIdispProject);
		if (pIdispProject)
		{
			CComQIPtr<VCProjectT> pVcProject{pIdispProject};
			if (pVcProject)
			{
				CComPtr<VCProjectEngineT> projEng;
				pVcProject->get_VCProjectEngine((IDispatch**)&projEng);
				if (projEng)
				{
					if (SUCCEEDED(projEng->get_Events((IDispatch**)&mVcProjectItemsEvents)) && mVcProjectItemsEvents)
					{
						mVcProjectItemsEventsSink = ::CreateVcProjectEngineEventSink();
						if (mVcProjectItemsEventsSink)
							mVcProjectItemsEventsSink->ManageSync((IUnknown*)mVcProjectItemsEvents.p, true);
					}
				}
			}
		}
	}

	void ClearProjectEventSinks()
	{
		if (mVcProjectItemsEventsSink && mVcProjectItemsEvents && mVcProjectItemsEventsSink->IsSynced())
			mVcProjectItemsEventsSink->ManageSync(mVcProjectItemsEvents, false);

		mVcProjectItemsEventsSink = NULL;
		mVcProjectItemsEvents = NULL;
	}

#ifdef AVR_STUDIO
	void WalkAvrProject(EnvDTE::Project* pProject)
	{
		CComPtr<EnvDTE::Properties> props;
		pProject->get_Properties(&props);
		if (!props)
			return;

		long propCnt = 0;
		props->get_Count(&propCnt);
		for (long idx = 1; idx <= propCnt; ++idx)
		{
			CComPtr<EnvDTE::Property> curProp;
			props->Item(CComVariant(idx), &curProp);
			if (!curProp)
				continue;

			CComBSTR propName;
			curProp->get_Name(&propName);
			if (propName != L"ToolchainData")
				continue;

			CComVariant toolchainDataVar;
			curProp->get_Value(&toolchainDataVar);
			if (toolchainDataVar.pdispVal == NULL)
				continue;

			CComQIPtr<AvrProjectEngineLibrary7::_AutomationToolchainData> pToolchainData{toolchainDataVar.pdispVal};
			if (pToolchainData)
			{
				HRESULT hr;
				CComBSTR includeDirs;

				// SYSTEM INCLUDES
				hr = pToolchainData->GetPropertyValue(CComBSTR(L"avrgcc.toolchain.directories.IncludePaths"),
				                                      &includeDirs);
				if (!includeDirs.Length())
					includeDirs = CComBSTR(GetDefaultToolchainIncludeDirs(false));
				if (includeDirs.Length())
				{
					// includeDirs is a comma-delimited list -- change , to ;
					CStringW tmp = includeDirs;
					tmp.Replace(L',', L';');
					includeDirs = tmp;

					AtmelCppWorkaround(tmp, includeDirs);

					::TokenizedAddToFileList(includeDirs, mCurSettings->mPlatformIncludeDirs,
					                         mCurSettings->mProjectFileDir, false, false);
					includeDirs.Empty();
				}

				hr = pToolchainData->GetPropertyValue(CComBSTR(L"avr32gcc.toolchain.directories.IncludePaths"),
				                                      &includeDirs);
				if (!includeDirs.Length())
					includeDirs = CComBSTR(GetDefaultToolchainIncludeDirs(true));
				if (includeDirs.Length())
				{
					// includeDirs is a comma-delimited list -- change , to ;
					CStringW tmp = includeDirs;
					tmp.Replace(L',', L';');
					includeDirs = tmp;

					AtmelCppWorkaround(tmp, includeDirs);

					::TokenizedAddToFileList(includeDirs, mCurSettings->mPlatformIncludeDirs,
					                         mCurSettings->mProjectFileDir, false, false);
					includeDirs.Empty();
				}

				// PROJECT INCLUDES
				hr = pToolchainData->GetPropertyValue(CComBSTR(L"avrgcc.compiler.directories.IncludePaths"),
				                                      &includeDirs);
				if (includeDirs.Length())
				{
					// includeDirs is a comma-delimited list -- change , to ;
					CStringW tmp = includeDirs;
					tmp.Replace(L',', L';');
					includeDirs = tmp;
					::TokenizedAddToFileListUnique(includeDirs, mCurSettings->mConfigFullIncludeDirs,
					                               mCurSettings->mPlatformIncludeDirs,
					                               mCurSettings->mProjectFileDir + CStringW("\\Dummy"), false, false);
					includeDirs.Empty();
				}

				hr = pToolchainData->GetPropertyValue(CComBSTR(L"avr32gcc.compiler.directories.IncludePaths"),
				                                      &includeDirs);
				if (includeDirs.Length())
				{
					// includeDirs is a comma-delimited list -- change , to ;
					CStringW tmp = includeDirs;
					tmp.Replace(L',', L';');
					includeDirs = tmp;
					::TokenizedAddToFileListUnique(includeDirs, mCurSettings->mConfigFullIncludeDirs,
					                               mCurSettings->mPlatformIncludeDirs,
					                               mCurSettings->mProjectFileDir + CStringW("\\Dummy"), false, false);
					includeDirs.Empty();
				}

				// Read AVR toolchain device files
				hr = pToolchainData->GetPropertyValue(CComBSTR(L"avr32gcc.toolchain.directories.ToolchainDeviceFiles"),
				                                      &includeDirs);
#ifdef _DEBUG
				if (!includeDirs.Length())
					includeDirs = L"C:\\Program Files (x86)\\Atmel\\AVR Studio 5.0\\extensions\\Application\\AVR "
					              L"Toolchain2\\avr32\\include\\avr32\\uc3l032.h;"
					              L"C:\\Program Files (x86)\\Atmel\\AVR Studio 5.0\\extensions\\Application\\AVR "
					              L"Toolchain\\avr32\\include2\\avr32\\ap7000.h;";
#endif // _DEBUG
				if (includeDirs.Length())
				{
					// includeDirs is a comma-delimited list -- change , to ;
					CStringW tmp = includeDirs;
					tmp.Replace(L',', L';');

					TokenW files(tmp);
					while (files.more() > 2)
					{
						CStringW f = files.read(L",;");
						if (IsFile(f))
						{
							f = ::MSPath(f);
							mCurSettings->mDeviceFiles.Add(f);
						}
					}
				}
			}
		}
	}

	void AtmelCppWorkaround(const CStringW& tmp, CComBSTR& includeDirs)
	{
		int curPos = 0;
		for (;;)
		{
			CStringW resToken = tmp.Tokenize(L";", curPos);
			if (!resToken.GetLength())
				break;

			WCHAR newPath[MAX_PATH] = {0};
			PathCombineW(newPath, resToken, L"c++");
			if (IsDir(newPath))
			{
				includeDirs += L";";
				includeDirs += newPath;
			}
		}
	}

	static CStringW GetDefaultToolchainIncludeDirs(bool is32bit)
	{
		CStringW includeDirs;
		CStringW appExtDir = ::GetRegValueW(HKEY_CURRENT_USER, "Software\\Atmel\\AvrStudio\\5.0_Config\\Initialization",
		                                    "ApplicationExtensionsFolder");
		if (appExtDir.GetLength())
		{
			if (is32bit)
				includeDirs = appExtDir + L"\\Application\\AVR Toolchain\\avr32\\include;"; // 32-bit includes
			else
				includeDirs = appExtDir + L"\\Application\\AVR Toolchain\\avr\\include;"; // 8-bit includes
		}
		return includeDirs;
	}

#endif

	void GetReferences(EnvDTE::Project* pProject, FileList& refsList)
	{
		LogElapsedTime et("GetReferences", 400);
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			CComPtr<IDispatch> idis;
			pProject->get_Object(&idis);

			CComQIPtr<VSLangProj::VSProject> vsProject{idis};
			if (vsProject)
			{
				CComPtr<VSLangProj::References> references;
				vsProject->get_References(&references);
				if (references)
				{
					long refCount = 0, readRefCount = 0;
					references->get_Count(&refCount);

					if (refCount && !Psettings->mEnumerateVsLangReferences)
					{
						vCatLog("Environment.Solution", "references: skipped %ld references due to disabled enumeration", refCount);
						refCount = 0;
					}
					else
					{
						CatLog("Environment.Solution", "references:");
					}

					CComPtr<IUnknown> pUnk;
					HRESULT hres = references->_NewEnum(&pUnk);
					if (refCount && SUCCEEDED(hres) && pUnk != NULL)
					{
						CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
						if (pNewEnum)
						{
							VARIANT varReference;
							VariantInit(&varReference);
							CComQIPtr<VSLangProj::Reference> curReference;
							ULONG enumVarCnt = 0;
							while (pNewEnum->Next(1, &varReference, &enumVarCnt) == S_OK)
							{
								_ASSERTE(varReference.vt == VT_DISPATCH);
								curReference = varReference.pdispVal;
								VariantClear(&varReference);
								if (!curReference)
									continue;

								++readRefCount;
								CComQIPtr<VSLangProj80::Reference3> curReference80{curReference};
								if (curReference80)
								{
									// [case: 98091]
									VARIANT_BOOL vb = VARIANT_FALSE;
									if (!(SUCCEEDED(curReference80->get_Resolved(&vb))))
									{
										vCatLog("Environment.Solution", "skip VsProject reference: resolve fail");
										continue;
									}

									if (vb == VARIANT_FALSE)
									{
										vCatLog("Environment.Solution", "skip VsProject reference: unresolved");
										continue;
									}
								}

								CComBSTR path;
								curReference->get_Path(&path);
								if (!path.Length())
								{
									vCatLog("Environment.Solution", "skip VsProject reference: no path");
									continue;
								}

								// Skip reference to other projects in the solution.
								CComPtr<EnvDTE::Project> srcProj;
								curReference->get_SourceProject(&srcProj);
								if (srcProj)
								{
#if defined(_DEBUG)
									CString pth = _T("VsProject Reference: Skipping: ") + CString(path) + _T("\n");
									OutputDebugString(pth);
#endif
									vCatLog("Environment.Solution", "skip VsProject reference: %s", (LPCTSTR)CString(path));
								}
								else
								{
#if 0
								// [case: 67129]
								// attempt at getting WinJS files in js metro project
								// but jsproj doesn't implement Reference5 in the RC
								if (::IsDir(path))
								{
									MessageBox(NULL, "Ref is dir", "sean", 0);
									CComQIPtr<VSLangProj110::Reference5> ref5{curReference};
									if (ref5)
									{
										MessageBox(NULL, "Reference5", "sean", 0);
										SAFEARRAY *arr = NULL;
										HRESULT hr = ref5->get_ExpandedSdkReferences(&arr);
										if (S_OK == hr && arr)
										{
											MessageBox(NULL, "ExpandedSdkRefs", "sean", 0);
											const UINT dims = ::SafeArrayGetDim(arr);
											for (long idx = 0; idx < (long)dims; ++idx)
											{
												CComPtr<IDispatch> iDisp;
												hr = ::SafeArrayGetElement(arr, &idx, &iDisp);
												if (S_OK != hr || !iDisp)
													continue;

// 												CComQIPtr<VSLangProj::Reference> expandedVsRef = iDisp;
// 												if (!expandedVsRef)
// 													continue;
// 
// 												CComBSTR fullPathBstr;
// 												expandedVsRef->get_Path(&fullPathBstr);
// 												const CStringW fullPath(fullPathBstr);
// 												if (fullPath.IsEmpty())
// 													continue;
// 
// 												if (refsList.AddUniqueNoCase(fullPath))
// 												{
// 													Log(CString(fullPath));
// #if defined(_DEBUG)
// 													CStringW msg = L"VsProject SDK Reference: " + fullPath + L"\n";
// 													OutputDebugStringW(msg);
// #endif
// 												}
											}

											hr = ::SafeArrayDestroy(arr);
										}
									}
								}
#endif // 0

									if (refsList.AddUniqueNoCase(CStringW(path)))
									{
										CatLog("Environment.Directories", (const char*)CString(path));
									}
#if defined(_DEBUG)
									CString pth = _T("VsProject Reference: ") + CString(path) + _T("\n");
									OutputDebugString(pth);
#endif
								}
							}
						}
					}
					else if (refCount)
					{
						vLog("ERROR: GetReferences (a) NewEnum %lx\n", hres);
					}

					_ASSERTE(refCount == readRefCount);
				}
			}

			// Website Projects (VS2005+)
			// [case: 100545] due to very long pause during solution load in dev15p5
			CComQIPtr<VSWebSite::VSWebSite> vsWebSite{projectEngineVersion >= 15 ? nullptr : idis};
			if (vsWebSite)
			{
				LogElapsedTime et2("WebReferences", 400);
				CComPtr<VSWebSite::AssemblyReferences> asmReferences;
				vsWebSite->get_References(&asmReferences);
				if (asmReferences)
				{
					Log("webreferences:");
					long refCount = 0, readRefCount = 0;
					asmReferences->get_Count(&refCount);

					CComPtr<IUnknown> pUnk;
					HRESULT hres = asmReferences->_NewEnum(&pUnk);
					if (refCount && SUCCEEDED(hres) && pUnk != NULL)
					{
						CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
						if (pNewEnum)
						{
							VARIANT varReference;
							VariantInit(&varReference);
							CComQIPtr<VSWebSite::AssemblyReference> curReference;
							ULONG enumVarCnt = 0;
							while (pNewEnum->Next(1, &varReference, &enumVarCnt) == S_OK)
							{
								_ASSERTE(varReference.vt == VT_DISPATCH);
								curReference = varReference.pdispVal;
								VariantClear(&varReference);
								if (!curReference)
									continue;

								++readRefCount;
								CComBSTR path;
								curReference->get_FullPath(&path);
								if (!path.Length())
									continue;

								// Skip reference to other projects in the solution.
								CComPtr<EnvDTE::Project> srcProj;
								curReference->get_ReferencedProject(&srcProj);
								if (srcProj)
								{
#if defined(_DEBUG)
									CString pth = _T("VsProject Reference: Skipping: ") + CString(path) + _T("\n");
									OutputDebugString(pth);
#endif
								}
								else
								{
									if (refsList.AddUniqueNoCase(CStringW(path)))
									{
										Log((const char*)CString(path));
									}
#if defined(_DEBUG)
									CString pth = _T("VsProject Reference: ") + CString(path) + _T("\n");
									OutputDebugString(pth);
#endif
								}
							}
						}
					}
					else if (refCount)
					{
						vLog("ERROR: GetReferences (b) NewEnum %lx\n", hres);
					}

					_ASSERTE(refCount == readRefCount);
				}
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("GetReferences:");
		}
#endif // !SEAN
	}

	void GetVcReferences(VCProjectT* vcProject, FileList& refsList)
	{
		// check for VS11+ SDK references (WinMD references)
		_ASSERTE(projectEngineVersion > 10);
		_ASSERTE(vcProject);

		if (!::WinVersionSupportsWinRT())
			return;

		if (gShellIsUnloading)
			throw UnloadingException();

		LogElapsedTime et("GetVcReferences", 400);

		CComPtr<IDispatch> vcReferencesDisp;
		vcProject->get_VCReferences(&vcReferencesDisp);
		if (!vcReferencesDisp)
			return;

		CComQIPtr<VCReferencesT> vcReferences{vcReferencesDisp};
		if (!vcReferences)
			return;

		CatLog("Environment.Solution", "VCreferences:");
		long vcRefCount = 0, readVcRefCount = 0;
		vcReferences->get_Count(&vcRefCount);
		CComPtr<IUnknown> pUnk;
		HRESULT hres = vcReferences->_NewEnum(&pUnk);
		if (vcRefCount && SUCCEEDED(hres) && pUnk != NULL)
		{
			CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
			if (pNewEnum)
			{
				VARIANT varVcRef;
				VariantInit(&varVcRef);
				CComPtr<IDispatch> vcRef;
				ULONG enumVarCnt = 0;
				while (pNewEnum->Next(1, &varVcRef, &enumVarCnt) == S_OK)
				{
					_ASSERTE(varVcRef.vt == VT_DISPATCH);
					vcRef = varVcRef.pdispVal;
					VariantClear(&varVcRef);
					if (!vcRef)
						continue;

					++readVcRefCount;
					CComQIPtr<VCSdkReferenceT> sdkRef{vcRef};
					if (!sdkRef)
						continue;

					SAFEARRAY* arr = NULL;
					HRESULT hr = sdkRef->get_ExpandedReferences(&arr);
					if (S_OK == hr && arr)
					{
						const UINT dims = ::SafeArrayGetDim(arr);
						for (long idx = 0; !gShellIsUnloading && idx < (long)dims; ++idx)
						{
							CComPtr<IDispatch> iDisp;
							hr = ::SafeArrayGetElement(arr, &idx, &iDisp);
							if (S_OK != hr || !iDisp)
								continue;

							CComQIPtr<VCReferenceT> expandedVcRef{iDisp};
							if (!expandedVcRef)
								continue;

							CComBSTR fullPathBstr;
							expandedVcRef->get_FullPath(&fullPathBstr);
							const CStringW fullPath(fullPathBstr);
							if (fullPath.IsEmpty())
								continue;

							if (refsList.AddUniqueNoCase(fullPath))
							{
								Log((const char*)CString(fullPath));
#if defined(_DEBUG)
								CStringW msg = L"VcProject SDK Reference: " + fullPath + L"\n";
								OutputDebugStringW(msg);
#endif
							}
						}

						hr = ::SafeArrayDestroy(arr);
#if defined(_DEBUG)
						CStringW msg;
						CString__FormatW(msg, L"SafeArraryDestroy returned %lx\n", hr);
						OutputDebugStringW(msg);
#endif
					}
				}
			}
		}
		else if (vcRefCount)
		{
			vLog("ERROR: GetVcReferences NewEnum %lx\n", hres);
		}
		_ASSERTE(vcRefCount == readVcRefCount);
	}

	bool CollectViaIntellisense(VCConfigurationT*)
	{
		return false;
	}

	template <typename ToolT> bool IsVcProjectConfigManaged(ToolT* pTool)
	{
		// assumes that VCProjectEngineLibraryXX::managedNotSet is 0 in all versions of VCProjectEngineLibrary (true for
		// 7-15)
		VCCompileAsManagedOptionsT mngOpt = (VCCompileAsManagedOptionsT)0;
		pTool->get_CompileAsManaged(&mngOpt);
		return mngOpt != (VCCompileAsManagedOptionsT)0;
	}

}; // end template VsSolutionInfoImpl

typedef VsSolutionInfoImpl<1, // VCProjectEngine for vs2003 is version 1
                           VCProjectEngineLibrary7::VCProjectEngine, VCProjectEngineLibrary7::VCProject,
                           VCProjectEngineLibrary7::IVCCollection, VCProjectEngineLibrary7::VCConfiguration,
                           VCProjectEngineLibrary7::VCStyleSheet, VCProjectEngineLibrary7::VCCLCompilerTool,
                           VCProjectEngineLibrary7::VCNMakeTool, VCProjectEngineLibrary10::IVCRulePropertyStorage,
                           VCProjectEngineLibrary7::VCPlatform, VCProjectEngineLibrary11::VCSdkReference,
                           VCProjectEngineLibrary7::VCReferences, VCProjectEngineLibrary7::VCReference,
                           VCProjectEngineLibrary7::compileAsManagedOptions, VCProjectEngineLibrary15::VCConfiguration2,
                           VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo7;

typedef VsSolutionInfoImpl<8, // VCProjectEngine for vs2005 is version 8
                           VCProjectEngineLibrary8::VCProjectEngine, VCProjectEngineLibrary8::VCProject,
                           VCProjectEngineLibrary8::IVCCollection, VCProjectEngineLibrary8::VCConfiguration,
                           VCProjectEngineLibrary8::VCPropertySheet, VCProjectEngineLibrary8::VCCLCompilerTool,
                           VCProjectEngineLibrary8::VCNMakeTool, VCProjectEngineLibrary10::IVCRulePropertyStorage,
                           VCProjectEngineLibrary8::VCPlatform, VCProjectEngineLibrary11::VCSdkReference,
                           VCProjectEngineLibrary8::VCReferences, VCProjectEngineLibrary8::VCReference,
                           VCProjectEngineLibrary8::compileAsManagedOptions, VCProjectEngineLibrary15::VCConfiguration2,
                           VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo8;

typedef VsSolutionInfoImpl<9, // VCProjectEngine for vs2008 is version 9
                           VCProjectEngineLibrary9::VCProjectEngine, VCProjectEngineLibrary9::VCProject,
                           VCProjectEngineLibrary9::IVCCollection, VCProjectEngineLibrary9::VCConfiguration,
                           VCProjectEngineLibrary9::VCPropertySheet, VCProjectEngineLibrary9::VCCLCompilerTool,
                           VCProjectEngineLibrary9::VCNMakeTool, VCProjectEngineLibrary10::IVCRulePropertyStorage,
                           VCProjectEngineLibrary9::VCPlatform, VCProjectEngineLibrary11::VCSdkReference,
                           VCProjectEngineLibrary9::VCReferences, VCProjectEngineLibrary9::VCReference,
                           VCProjectEngineLibrary9::compileAsManagedOptions, VCProjectEngineLibrary15::VCConfiguration2,
                           VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo9;

typedef VsSolutionInfoImpl<10, // VCProjectEngine for vs2010 is version 10
                           VCProjectEngineLibrary10::VCProjectEngine, VCProjectEngineLibrary10::VCProject,
                           VCProjectEngineLibrary10::IVCCollection, VCProjectEngineLibrary10::VCConfiguration,
                           VCProjectEngineLibrary10::VCPropertySheet, VCProjectEngineLibrary10::VCCLCompilerTool,
                           VCProjectEngineLibrary10::VCNMakeTool, VCProjectEngineLibrary10::IVCRulePropertyStorage,
                           VCProjectEngineLibrary10::VCPlatform, VCProjectEngineLibrary11::VCSdkReference,
                           VCProjectEngineLibrary10::VCReferences, VCProjectEngineLibrary10::VCReference,
                           VCProjectEngineLibrary10::compileAsManagedOptions,
                           VCProjectEngineLibrary15::VCConfiguration2, VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo10;

typedef VsSolutionInfoImpl<11, // VCProjectEngine for vs2012 is version 11
                           VCProjectEngineLibrary11::VCProjectEngine, VCProjectEngineLibrary11::VCProject,
                           VCProjectEngineLibrary11::IVCCollection, VCProjectEngineLibrary11::VCConfiguration,
                           VCProjectEngineLibrary11::VCPropertySheet, VCProjectEngineLibrary11::VCCLCompilerTool,
                           VCProjectEngineLibrary11::VCNMakeTool, VCProjectEngineLibrary11::IVCRulePropertyStorage,
                           VCProjectEngineLibrary11::VCPlatform, VCProjectEngineLibrary11::VCSdkReference,
                           VCProjectEngineLibrary11::VCReferences, VCProjectEngineLibrary11::VCReference,
                           VCProjectEngineLibrary11::compileAsManagedOptions,
                           VCProjectEngineLibrary15::VCConfiguration2, VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo11;

typedef VsSolutionInfoImpl<12, // VCProjectEngine for vs2013 is version 12
                           VCProjectEngineLibrary12::VCProjectEngine, VCProjectEngineLibrary12::VCProject,
                           VCProjectEngineLibrary12::IVCCollection, VCProjectEngineLibrary12::VCConfiguration,
                           VCProjectEngineLibrary12::VCPropertySheet, VCProjectEngineLibrary12::VCCLCompilerTool,
                           VCProjectEngineLibrary12::VCNMakeTool, VCProjectEngineLibrary12::IVCRulePropertyStorage,
                           VCProjectEngineLibrary12::VCPlatform, VCProjectEngineLibrary12::VCSdkReference,
                           VCProjectEngineLibrary12::VCReferences, VCProjectEngineLibrary12::VCReference,
                           VCProjectEngineLibrary12::compileAsManagedOptions,
                           VCProjectEngineLibrary15::VCConfiguration2, VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo12;

typedef VsSolutionInfoImpl<14, // VCProjectEngine for vs2015 is version 14
                           VCProjectEngineLibrary14::VCProjectEngine, VCProjectEngineLibrary14::VCProject,
                           VCProjectEngineLibrary14::IVCCollection, VCProjectEngineLibrary14::VCConfiguration,
                           VCProjectEngineLibrary14::VCPropertySheet, VCProjectEngineLibrary14::VCCLCompilerTool,
                           VCProjectEngineLibrary14::VCNMakeTool, VCProjectEngineLibrary14::IVCRulePropertyStorage,
                           VCProjectEngineLibrary14::VCPlatform, VCProjectEngineLibrary14::VCSdkReference,
                           VCProjectEngineLibrary14::VCReferences, VCProjectEngineLibrary14::VCReference,
                           VCProjectEngineLibrary14::compileAsManagedOptions,
                           VCProjectEngineLibrary15::VCConfiguration2, VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo14;

typedef VsSolutionInfoImpl<15, // VCProjectEngine for vs2017 is version 15
                           VCProjectEngineLibrary15::VCProjectEngine, VCProjectEngineLibrary15::VCProject,
                           VCProjectEngineLibrary15::IVCCollection, VCProjectEngineLibrary15::VCConfiguration,
                           VCProjectEngineLibrary15::VCPropertySheet, VCProjectEngineLibrary15::VCCLCompilerTool,
                           VCProjectEngineLibrary15::VCNMakeTool, VCProjectEngineLibrary15::IVCRulePropertyStorage,
                           VCProjectEngineLibrary15::VCPlatform, VCProjectEngineLibrary15::VCSdkReference,
                           VCProjectEngineLibrary15::VCReferences, VCProjectEngineLibrary15::VCReference,
                           VCProjectEngineLibrary15::compileAsManagedOptions,
                           VCProjectEngineLibrary15::VCConfiguration2, VCppInterfaces15::IIntelliSenseInfo>
    VsSolutionInfo15;

typedef VsSolutionInfoImpl<16, // VCProjectEngine for vs2019 is version 16
                           VCProjectEngineLibrary16::VCProjectEngine, VCProjectEngineLibrary16::VCProject,
                           VCProjectEngineLibrary16::IVCCollection, VCProjectEngineLibrary16::VCConfiguration,
                           VCProjectEngineLibrary16::VCPropertySheet, VCProjectEngineLibrary16::VCCLCompilerTool,
                           VCProjectEngineLibrary16::VCNMakeTool, VCProjectEngineLibrary16::IVCRulePropertyStorage,
                           VCProjectEngineLibrary16::VCPlatform, VCProjectEngineLibrary16::VCSdkReference,
                           VCProjectEngineLibrary16::VCReferences, VCProjectEngineLibrary16::VCReference,
                           VCProjectEngineLibrary16::compileAsManagedOptions,
                           VCProjectEngineLibrary16::VCConfiguration2, VCppInterfaces16::IIntelliSenseInfo>
    VsSolutionInfo16;

#ifdef _WIN64
typedef VsSolutionInfoImpl<17, // VCProjectEngine for vs2022 is version 17
                           VCProjectEngineLibrary17::VCProjectEngine, VCProjectEngineLibrary17::VCProject,
                           VCProjectEngineLibrary17::IVCCollection, VCProjectEngineLibrary17::VCConfiguration,
                           VCProjectEngineLibrary17::VCPropertySheet, VCProjectEngineLibrary17::VCCLCompilerTool,
                           VCProjectEngineLibrary17::VCNMakeTool, VCProjectEngineLibrary17::IVCRulePropertyStorage,
                           VCProjectEngineLibrary17::VCPlatform, VCProjectEngineLibrary17::VCSdkReference,
                           VCProjectEngineLibrary17::VCReferences, VCProjectEngineLibrary17::VCReference,
                           VCProjectEngineLibrary17::compileAsManagedOptions,
                           VCProjectEngineLibrary17::VCConfiguration2, VCppInterfaces17::IIntelliSenseInfo>
    VsSolutionInfo17;
#endif

// #newVsVersion

typedef VsSolutionInfo14 AvrSolutionInfo7;

VsSolutionInfo* CreateVsSolutionInfo()
{
	CComPtr<EnvDTE::_DTE> pDte(gDte);
	if (pDte)
	{
#ifdef AVR_STUDIO
		CComBSTR regRoot;
		pDte->get_RegistryRoot(&regRoot);
		CStringW regRootW = regRoot;
		if (regRootW == L"Software\\Atmel\\AtmelStudio\\7.0")
		{
			return new AvrSolutionInfo7(pDte); // VS2015 Shell
		}
#else
		CComBSTR vers;
		pDte->get_Version(&vers);
		const CStringW vers2(vers);

		// #newVsVersion
#ifdef _WIN64
		if (vers2[0] == L'1' && vers2[1] == L'7')
			return new VsSolutionInfo17(pDte);
#endif // _WIN64

		if (vers2[0] == L'1' && vers2[1] == L'6')
			return new VsSolutionInfo16(pDte);

		if (vers2[0] == L'1' && vers2[1] == L'5')
			return new VsSolutionInfo15(pDte);

		if (vers2[0] == L'1' && vers2[1] == L'4')
			return new VsSolutionInfo14(pDte);

		if (vers2[0] == L'1' && vers2[1] == L'2')
			return new VsSolutionInfo12(pDte);

		if (vers2[0] == L'1' && vers2[1] == L'1')
			return new VsSolutionInfo11(pDte);

		if (vers2[0] == L'1' && vers2[1] == L'0')
			return new VsSolutionInfo10(pDte);

		if (vers2[0] == L'9')
			return new VsSolutionInfo9(pDte);

		if (vers2[0] == L'8')
			return new VsSolutionInfo8(pDte);

		if (vers2[0] == L'7')
			return new VsSolutionInfo7(pDte);

#endif // !AVR_STUDIO

		_ASSERTE(!"failed to create VsSolutionInfoImpl");
	}

	return NULL;
}

template<>
void VsSolutionInfo7::WalkVcProject(VCProjectEngineLibrary7::VCProject* pVcProject)
{
	// VS7 flat out not tested - uses old ProjectInfo manual file parsing
}

// Not invoked since WalkVcProject is empty
template <>
void VsSolutionInfo7::WalkVcProjectConfigNmTool(VCProjectEngineLibrary7::VCConfiguration* /*pConfig*/,
                                                VCProjectEngineLibrary7::VCNMakeTool* /*pTool*/)
{
	// VC7 VCNMakeTool doesn't support include search path or forced includes
}

template <>
void VsSolutionInfo7::WalkVcProjectConfigViaRuleProperties(VCProjectEngineLibrary7::VCConfiguration* /*pConfig*/,
                                                           long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo7::WalkVcProjectConfigViaRuleProperties should not be called");
}

template <>
void VsSolutionInfo8::WalkVcProjectConfigViaRuleProperties(VCProjectEngineLibrary8::VCConfiguration* /*pConfig*/,
                                                           long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo8::WalkVcProjectConfigViaRuleProperties should not be called");
}

template <>
void VsSolutionInfo9::WalkVcProjectConfigViaRuleProperties(VCProjectEngineLibrary9::VCConfiguration* /*pConfig*/,
                                                           long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo9::WalkVcProjectConfigViaRuleProperties should not be called");
}

template <>
void VsSolutionInfo7::WalkVcProjectConfigForExternalIncludeDir(VCProjectEngineLibrary7::VCConfiguration* /*pConfig*/,
                                                               long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo7::WalkVcProjectConfigForExternalIncludeDir should not be called");
}

template <>
void VsSolutionInfo8::WalkVcProjectConfigForExternalIncludeDir(VCProjectEngineLibrary8::VCConfiguration* /*pConfig*/,
                                                               long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo8::WalkVcProjectConfigForExternalIncludeDir should not be called");
}

template <>
void VsSolutionInfo9::WalkVcProjectConfigForExternalIncludeDir(VCProjectEngineLibrary9::VCConfiguration* /*pConfig*/,
                                                               long /*totalConfigs*/, long /*curCfgIdx*/)
{
	_ASSERTE(0 && !"VsSolutionInfo9::WalkVcProjectConfigForExternalIncludeDir should not be called");
}

template<>
bool VsSolutionInfo15::CollectViaIntellisense(VCProjectEngineLibrary15::VCConfiguration* pConfig)
{
	// #todoCase102486
	_ASSERTE(/*projectEngineVersion >= 15 &&*/
	         // might also need to check state of default intellisense??
	         GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled());

	// [case: 102486]
	// 	CComPtr<VCIntelliSenseInfoT> pInfo;
	// 	HRESULT res = pConfig->CollectIntelliSenseInfo((IUnknown**)&pInfo);
	// 	if (SUCCEEDED(res) && pInfo)
	// 	{
	// 		return true;
	// 	}

	return false;
}

template<>
bool VsSolutionInfo16::CollectViaIntellisense(VCProjectEngineLibrary16::VCConfiguration* pConfig)
{
	// #todoCase102486
	_ASSERTE(/*projectEngineVersion >= 15 &&*/
	         // might also need to check state of default intellisense??
	         GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled());

	// [case: 102486]
	// 	CComPtr<VCIntelliSenseInfoT> pInfo;
	// 	HRESULT res = pConfig->CollectIntelliSenseInfo((IUnknown**)&pInfo);
	// 	if (SUCCEEDED(res) && pInfo)
	// 	{
	// 		return true;
	// 	}

	return false;
}

#ifdef _WIN64
template<>
bool VsSolutionInfo17::CollectViaIntellisense(VCProjectEngineLibrary17::VCConfiguration* pConfig)
{
	// #todoCase102486
	_ASSERTE(/*projectEngineVersion >= 17 &&*/
	         // might also need to check state of default intellisense??
	         GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled());

	// [case: 102486]
	// 	CComPtr<VCIntelliSenseInfoT> pInfo;
	// 	HRESULT res = pConfig->CollectIntelliSenseInfo((IUnknown**)&pInfo);
	// 	if (SUCCEEDED(res) && pInfo)
	// 	{
	// 		return true;
	// 	}

	return false;
}
#endif

// #newVsVersion

/////////////////////////////////////

template <typename VCProjectContextT, typename VCIntelliSenseInfoT, typename VCIntelliSenseFileT>
bool GetVcIntellisenseInfo(VsSolutionInfo::ProjectSettingsPtr pCurProjSettings, int requestId,
                           DWORD& intellisenseLoadRetryDuration)
{
	if (!g_vaManagedPackageSvc)
	{
		_ASSERTE(g_vaManagedPackageSvc);
		return false;
	}

	uintptr_t projectContextIunk =
	    (uintptr_t)g_vaManagedPackageSvc->GetProjectContext(requestId, pCurProjSettings->mProjectFile);
	if (!projectContextIunk)
		return false;

	if ((DWORD)projectContextIunk == 0x80004002) // 0x80004002 == interface not supported
		return false;

	if ((DWORD)projectContextIunk == 0x8007000E) // 0x8007000E == not enough storage is available
		return false;

	if (gShellIsUnloading)
		throw UnloadingException();

	HRESULT res;
	CComBSTR bstr;
	CComPtr<IUnknown> pUnk{(IUnknown*)projectContextIunk};
	CComQIPtr<VCProjectContextT> projectContext{pUnk};
	if (!projectContext)
		return false;

	pCurProjSettings->mIsVcProject = true;

	projectContext->get_DisplayName(&bstr);
	if (bstr.Length())
	{
		_ASSERTE(pCurProjSettings->mVsProjectName.IsEmpty());
		pCurProjSettings->mVsProjectName = bstr;
		bstr.Empty();
	}

#ifdef _DEBUG
	projectContext->get_Name(&bstr);
	if (bstr.Length())
	{
		CStringW nm(bstr);
		bstr.Empty();
	}

	projectContext->get_ProjectFilePath(&bstr);
	if (bstr.Length())
	{
		CStringW nm(bstr);
		bstr.Empty();
	}

	projectContext->get_ProjectDirectory(&bstr);
	if (bstr.Length())
	{
		CStringW nm(bstr);
		if (nm[nm.GetLength() - 1] != L'\\')
			nm += L"\\";
		_ASSERTE(0 == nm.Find(pCurProjSettings->mProjectFileDir));
		bstr.Empty();
	}

	projectContext->get_ConfigurationName(&bstr);
	if (bstr.Length())
	{
		CStringW nm(bstr);
		// "x86-Debug" "Windows x64"
		bstr.Empty();
	}

	int ifiles = 0;
	res = projectContext->get_IntelliSenseFilesCount(&ifiles);
	if (ifiles)
	{
		VCIntelliSenseFileT* isFile = nullptr;
		res = projectContext->GetIntelliSenseFile(0, &isFile);
		if (isFile)
		{
			isFile->get_FullPath(&bstr);
			if (bstr.Length())
			{
				CStringW nm(bstr);
				bstr.Empty();
			}
		}
	}
#endif

	CComQIPtr<VCIntelliSenseInfoT> info;
	res = projectContext->GetIntelliSenseInfo(&info);
	if (!info)
	{
		vLog("ERROR: IProjectContext::GetIntelliSenseInfo return %lu [d(%lu)]", res, intellisenseLoadRetryDuration);
		gVaMainWnd->KillTimer(IDT_SolutionWorkspaceIndexerPing);

		if (gSqi.IsWorkspaceIndexerActive() && intellisenseLoadRetryDuration < (60 * 60 * 1000))
		{
			uint timerDuration = 30000u;
			if (intellisenseLoadRetryDuration > (5 * 60 * 1000))
				timerDuration = 60000u;
			intellisenseLoadRetryDuration += timerDuration;
			gVaMainWnd->SetTimer(IDT_SolutionWorkspaceIndexerPing, timerDuration, nullptr);
		}
		return false;
	}

	res = info->get_FrameworkIncludePath(&bstr);
	if (bstr.Length())
	{
		::TokenizedAddToFileList(bstr, pCurProjSettings->mPlatformIncludeDirs, pCurProjSettings->mProjectFileDir, false,
		                         false);
		bstr.Empty();
	}

	res = info->get_ReferencesPath(&bstr);
	if (bstr.Length())
	{
		::TokenizedAddToFileList(bstr, pCurProjSettings->mPlatformLibraryDirs, pCurProjSettings->mProjectFileDir, false,
		                         false);
		bstr.Empty();
	}

	res = info->get_IncludePath(&bstr);
	if (bstr.Length())
	{
		::TokenizedAddToEitherFileListUnique(bstr, pCurProjSettings->mConfigFullIncludeDirs,
		                                     pCurProjSettings->mPlatformIncludeDirs, pCurProjSettings->mProjectFileDir,
		                                     false, false);
		bstr.Empty();
	}

	// additional include dirs have to be pulled from command line /I
	int cmdLines = 0;
	info->get_CommandLinesCount(&cmdLines);
	for (int idx = 0; !gShellIsUnloading && idx < cmdLines; ++idx)
	{
		CComBSTR bFiles, bSwitches;
		info->GetCommandLine(idx, &bFiles, &bSwitches);
		if (!bSwitches.Length())
			continue;

		int startPos = 0, endPos;
		const CStringW switches(bSwitches);
		for (;;)
		{
			startPos = switches.Find(L"/I", startPos);
			if (-1 == startPos)
				break;
			if (startPos + 3 >= switches.GetLength())
				break;

			startPos += 2;
			if (switches[startPos] == L'"')
			{
				// delimited by quotes
				++startPos;
				endPos = switches.Find(L"\"", startPos + 1);
			}
			else
			{
				// delimited by space
				endPos = switches.Find(L" ", startPos + 1);
			}

			if (-1 == endPos)
				break;

			const CStringW inc(switches.Mid(startPos, endPos - startPos));
			::TokenizedAddToEitherFileListUnique(CComBSTR((LPCWSTR)inc), pCurProjSettings->mConfigFullIncludeDirs,
			                                     pCurProjSettings->mPlatformIncludeDirs,
			                                     pCurProjSettings->mProjectFileDir, false, false);
			startPos = endPos;
		}
	}

	return true;
}

bool GetVcIntellisenseInfo(VsSolutionInfo::ProjectSettingsPtr pCurProjSettings, int requestId,
                           DWORD& intellisenseLoadRetryDuration)
{
	_ASSERTE(GetCurrentThreadId() != g_mainThread);
	if (!g_vaManagedPackageSvc)
	{
		_ASSERTE(!"GetVcIntellisenseInfo missing managed package");
		vLog("ERROR: missing mngd svc");
		return false;
	}

	// #newVsVersion
#ifdef _WIN64
	if (gShellAttr->IsDevenv17())
		return GetVcIntellisenseInfo<VCppInterfaces17::IProjectContext, VCppInterfaces17::IIntelliSenseInfo,
		                             VCppInterfaces17::IIntelliSenseFile>(pCurProjSettings, requestId,
		                                                                  intellisenseLoadRetryDuration);
#endif

	if (gShellAttr->IsDevenv16())
		return GetVcIntellisenseInfo<VCppInterfaces16::IProjectContext, VCppInterfaces16::IIntelliSenseInfo,
		                             VCppInterfaces16::IIntelliSenseFile>(pCurProjSettings, requestId,
		                                                                  intellisenseLoadRetryDuration);

	if (gShellAttr->IsDevenv15())
		return GetVcIntellisenseInfo<VCppInterfaces15::IProjectContext, VCppInterfaces15::IIntelliSenseInfo,
		                             VCppInterfaces15::IIntelliSenseFile>(pCurProjSettings, requestId,
		                                                                  intellisenseLoadRetryDuration);

	if (gShellAttr->IsDevenv14())
		return false;

	if (gShellAttr->IsDevenv12())
		return false;

	if (gShellAttr->IsDevenv11())
		return false;

	if (gShellAttr->IsDevenv10())
		return false;

	if (gShellAttr->IsDevenv9())
		return false;

	if (gShellAttr->IsDevenv8())
		return false;

	if (gShellAttr->IsDevenv7())
		return false;

	_ASSERTE(!"failed to run GetVcIntellisenseInfo");
	return false;
}

template <typename VCProjectT, typename VCCollectionT, typename VCConfigurationT, typename VCConfiguration2T>
CStringW VcEngineEvaluate(LPCWSTR expressionIn)
{
	// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.vcprojectengine.vcconfiguration.evaluate%28VS.100%29.aspx
	CStringW retval;

	CComPtr<EnvDTE::_DTE> mDte(gDte);
	if (!mDte)
		return retval;

	if (gShellIsUnloading)
		throw UnloadingException();

	const CStringW expr(expressionIn);

	try
	{
		CComPtr<EnvDTE::Projects> pProjects;

		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			// this doesn't work from project loader thread on vs2005/2008
			CComPtr<EnvDTE::_Solution> pSolution;
			mDte->get_Solution(&pSolution);
			if (!pSolution)
				return retval;

			pSolution->get_Projects(&pProjects);
		}
		else
		{
			// this doesn't work in vs2010 beta 2 - get 0 for project count
			// TODO: check to see if this works in the RC when it comes out
			mDte->GetObject((_bstr_t)L"VCProjects", (IDispatch**)&pProjects);
		}

		if (!pProjects)
			return retval;

		// iterate over solution projects
		long projCnt = 0;
		pProjects->get_Count(&projCnt);

		for (long projIdx = 1; !gShellIsUnloading && projIdx <= projCnt; ++projIdx)
		{
			CComPtr<EnvDTE::Project> pProject;
			pProjects->Item(CComVariant(projIdx), &pProject);
			if (!pProject)
				continue;

			CComPtr<IDispatch> pIdispProject;
			pProject->get_Object(&pIdispProject);
			if (!pIdispProject)
				continue;

			CComQIPtr<VCProjectT> pVcProject{pIdispProject};
			if (!pVcProject)
				continue;

			CComPtr<VCCollectionT> pConfigs;
			pVcProject->get_Configurations((IDispatch**)&pConfigs);
			if (!pConfigs)
				continue;

			// iterate over project configurations
			long configCnt = 0;
			pConfigs->get_Count(&configCnt);
			for (long cfgIdx = 1; cfgIdx <= configCnt; ++cfgIdx)
			{
				CComPtr<IDispatch> pConfigTmp;
				pConfigs->Item(CComVariant(cfgIdx), &pConfigTmp);
				if (!pConfigTmp)
					continue;

				CComQIPtr<VCConfigurationT> pConfig{pConfigTmp};
				if (!pConfig)
					continue;

				CComBSTR comBstr;
				CComQIPtr<VCConfiguration2T> pConfig2{pConfigTmp};
				if (pConfig2)
				{
					// [case: 116247]
					// try GetEvaluatedPropertyValue() first because Evaluate()
					// forces load of real vc project while GetEvaluatedPropertyValue()
					// is supported by the readonly lightweight cached vc project
					CStringW expr2(expr);
					if (expr2 == kVcpkgIncEnvironmentVar)
						expr2 = kVcpkgIncPropertyName;

					// GetEvaluatedPropertyValue does not evaluate env vars
					if (-1 == expr2.FindOneOf(L"%$"))
					{
						pConfig2->GetEvaluatedPropertyValue(CComBSTR(expr2), &comBstr);
						if (comBstr.Length() && CStringW(comBstr) == expr2)
							comBstr.Empty();
					}
				}

				if (!comBstr.Length() || CStringW(comBstr) == expr)
					pConfig->Evaluate(CComBSTR(expr), &comBstr);

				if (comBstr.Length() && CStringW(comBstr) != expr)
				{
					// return on first successful eval - this has
					// implications: the same macro MIGHT evaluate differently
					// in another project (within this solution) or in another
					// configuration (in this project or another project within
					// this solution (maybe concat multiple evals if this is
					// an issue?)
					retval = comBstr;
					return retval;
				}
			}
		}
	}
	catch (const _com_error& e)
	{
		CString sMsg;
		CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
		                 __FUNCTION__);

		VALOGEXCEPTION(sMsg);
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in Evaluate 1");
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("Unexpected exception (2) in Evaluate");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in Evaluate 2");
		}
	}
#endif // !SEAN

	return retval;
}

CStringW VcEngineEvaluate(LPCWSTR expressionIn)
{
	// #newVsVersion
#ifdef _WIN64
	if (gShellAttr->IsDevenv17())
		return VcEngineEvaluate<VCProjectEngineLibrary17::VCProject, VCProjectEngineLibrary17::IVCCollection,
		                        VCProjectEngineLibrary17::VCConfiguration, VCProjectEngineLibrary17::VCConfiguration>(
		    expressionIn);
#endif

	if (gShellAttr->IsDevenv16())
		return VcEngineEvaluate<VCProjectEngineLibrary16::VCProject, VCProjectEngineLibrary16::IVCCollection,
		                        VCProjectEngineLibrary16::VCConfiguration, VCProjectEngineLibrary16::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv15())
		return VcEngineEvaluate<VCProjectEngineLibrary15::VCProject, VCProjectEngineLibrary15::IVCCollection,
		                        VCProjectEngineLibrary15::VCConfiguration, VCProjectEngineLibrary15::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv14())
		return VcEngineEvaluate<VCProjectEngineLibrary14::VCProject, VCProjectEngineLibrary14::IVCCollection,
		                        VCProjectEngineLibrary14::VCConfiguration, VCProjectEngineLibrary14::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv12())
		return VcEngineEvaluate<VCProjectEngineLibrary12::VCProject, VCProjectEngineLibrary12::IVCCollection,
		                        VCProjectEngineLibrary12::VCConfiguration, VCProjectEngineLibrary12::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv11())
		return VcEngineEvaluate<VCProjectEngineLibrary11::VCProject, VCProjectEngineLibrary11::IVCCollection,
		                        VCProjectEngineLibrary11::VCConfiguration, VCProjectEngineLibrary11::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv10())
		return VcEngineEvaluate<VCProjectEngineLibrary10::VCProject, VCProjectEngineLibrary10::IVCCollection,
		                        VCProjectEngineLibrary10::VCConfiguration, VCProjectEngineLibrary15::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv9())
		return VcEngineEvaluate<VCProjectEngineLibrary9::VCProject, VCProjectEngineLibrary9::IVCCollection,
		                        VCProjectEngineLibrary9::VCConfiguration, VCProjectEngineLibrary15::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv8())
		return VcEngineEvaluate<VCProjectEngineLibrary8::VCProject, VCProjectEngineLibrary8::IVCCollection,
		                        VCProjectEngineLibrary8::VCConfiguration, VCProjectEngineLibrary15::VCConfiguration>(
		    expressionIn);

	if (gShellAttr->IsDevenv7())
		return VcEngineEvaluate<VCProjectEngineLibrary7::VCProject, VCProjectEngineLibrary7::IVCCollection,
		                        VCProjectEngineLibrary7::VCConfiguration, VCProjectEngineLibrary15::VCConfiguration>(
		    expressionIn);

	_ASSERTE(!"failed to run VcEngineEvaluate -- new version of VS??");
	return CStringW();
}

template <typename VCProjectT, typename VCCollectionT, typename VCProjectEngineT, typename VCPlatformT>
void SetupVcEnvironment()
{
	LogElapsedTime tr("SetupVcEnv");

	CComPtr<EnvDTE::_DTE> pDte(gDte);
	if (!Psettings || !pDte)
		return;

	if (gShellIsUnloading)
		throw UnloadingException();

	try
	{
		CComPtr<EnvDTE::Projects> pProjects;
		HRESULT hres = pDte->GetObject(_bstr_t(L"VCProjects"), (IDispatch**)&pProjects);
		if (!(SUCCEEDED(hres) && pProjects))
		{
			vLog("ERROR: SetupVcEnvironment GetVcProjects %lx\n", hres);
			return;
		}

		CComPtr<EnvDTE::Properties> props;
		hres = pProjects->get_Properties(&props);
		if (!(SUCCEEDED(hres) && props))
		{
			// [case: 101742] fails in vs2017rc with:
			// 0x80131165 : Typelib export: Type library is not registered
			// #vcProjectEngineTypeLibFail
			vLog("ERROR: SetupVcEnvironment GetVcProps %lx\n", hres);
			return;
		}

		long propCnt = 0;
		hres = props->get_Count(&propCnt);
		if (!(SUCCEEDED(hres) && propCnt))
			return;

		CComPtr<IUnknown> pUnk;
		hres = props->_NewEnum(&pUnk);
		if (!(SUCCEEDED(hres) && pUnk != NULL))
		{
			vLog("ERROR: SetupVcEnvironment pUnk %lx\n", hres);
			return;
		}

		CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
		if (!pNewEnum)
		{
			vLog("ERROR: SetupVcEnvironment NewEnum %lx\n", hres);
			return;
		}

		CStringW platformWhitelist = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, "PlatformWhitelist",
		                                            gShellAttr->IsDevenv10OrHigher() ? L"Win32;WIN32;x64;X64" : L"");
		if (platformWhitelist.GetLength())
		{
			vLog("SetupVcEnv: wl %s\n", (LPCTSTR)CString(platformWhitelist));
			platformWhitelist = L";" + platformWhitelist + L";";
		}

		bool includesChanged = false;
		VARIANT varProp;
		VariantInit(&varProp);
		CComQIPtr<EnvDTE::Property> curProp;
		ULONG enumVarCnt = 0;
		while (pNewEnum->Next(1, &varProp, &enumVarCnt) == S_OK)
		{
			if (varProp.vt != VT_DISPATCH)
			{
				vLog("ERROR: SetupVcEnvironment prop not VT_DISPATCH: %x\n", varProp.vt);
				_ASSERTE(varProp.vt == VT_DISPATCH);
				continue;
			}

			curProp = varProp.pdispVal;
			VariantClear(&varProp);
			if (!curProp)
				continue;

			CComBSTR propName;
			hres = curProp->get_Name(&propName);
			if (!(SUCCEEDED(hres)) || propName != L"VCProjectEngine")
				continue;

			CComPtr<VCProjectEngineT> pVcProjectEngine;
			hres = curProp->get_Object((IDispatch**)&pVcProjectEngine);
			if (!(SUCCEEDED(hres) && pVcProjectEngine))
				continue;

			CComPtr<VCCollectionT> pVcPlatforms;
			hres = pVcProjectEngine->get_Platforms((IDispatch**)&pVcPlatforms);
			if (!(SUCCEEDED(hres) && pVcPlatforms))
				continue;

			CStringW platforms;
			long platformCnt = 0;
			hres = pVcPlatforms->get_Count(&platformCnt);
			if (!(SUCCEEDED(hres) && platformCnt))
				continue;

			for (long platformIdx = 1; platformIdx <= platformCnt; ++platformIdx)
			{
				// had to split this out for VS2010, not sure why.
				CComPtr<IDispatch> pVCPlatformTmp;
				hres = pVcPlatforms->Item(CComVariant(platformIdx), &pVCPlatformTmp);
				if (!(SUCCEEDED(hres) && pVCPlatformTmp))
					continue;

				CComQIPtr<VCPlatformT> pVCPlatform{pVCPlatformTmp};
				if (!pVCPlatform)
					continue;

				CComBSTR name, path, fullpath;
				hres = pVCPlatform->get_Name(&name);
				if (!(SUCCEEDED(hres) && name.Length()))
					continue;

				if (platformWhitelist.GetLength() && -1 == platformWhitelist.Find(L";" + CStringW(name) + L";"))
				{
					vLog("SetupVcEnv: reject platform %ld %s\n", platformIdx, (LPCTSTR)CString(name));
					continue;
				}

				if (!gShellAttr->IsDevenv10OrHigher() && CStringW(name) == L"ARM64")
				{
					// [case: 91655] reject ARM64 installed by WDK 10
					vLog("SetupVcEnv: ARM64 reject platform %ld\n", platformIdx);
					continue;
				}

				vLog("SetupVcEnv: platform %ld %s\n", platformIdx, (LPCTSTR)CString(name));
				platforms += CStringW(name) + L";";
				const CString rPath = ID_RK_APP + "\\" + CString(name);
				hres = pVCPlatform->get_IncludeDirectories(&path);
				if (SUCCEEDED(hres) && path.Length())
				{
					hres = pVCPlatform->Evaluate(path, &fullpath);
					if (SUCCEEDED(hres) && fullpath.Length())
					{
						if (fullpath[fullpath.Length()] != ';')
							fullpath += ";";
						const CString tmp1 = ::GetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_MSDEV_INC_VALUE));
						::SetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_MSDEV_INC_VALUE), CStringW(fullpath));
						const CString tmp2 = ::GetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_SYSTEMINCLUDE));
						if (tmp1.IsEmpty() || tmp2.IsEmpty() || tmp1 == tmp2)
						{
							if (tmp1.IsEmpty() || tmp2.IsEmpty())
								includesChanged = true;
							::SetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_SYSTEMINCLUDE), CStringW(fullpath));
						}
					}
				}

				hres = pVCPlatform->get_LibraryDirectories(&path);
				if (SUCCEEDED(hres) && path.Length())
				{
					hres = pVCPlatform->Evaluate(path, &fullpath);
					if (fullpath.Length())
						::SetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_MSDEV_LIB_VALUE), CStringW(fullpath));
				}

				hres = pVCPlatform->get_SourceDirectories(&path);
				if (SUCCEEDED(hres) && path.Length())
				{
					hres = pVCPlatform->Evaluate(path, &fullpath);
					if (SUCCEEDED(hres) && fullpath.Length())
						::SetRegValue(HKEY_CURRENT_USER, rPath, CString(ID_RK_MSDEV_SRC_VALUE), CStringW(fullpath));
				}
			}

			if (platforms.GetLength())
			{
				if (gShellAttr->IsDevenv8OrHigher())
					::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "Platforms", platforms);

				const CStringW tmp = ::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM);
				if (tmp.IsEmpty() && platforms.Find(L"Win32;") != -1)
					::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM, CStringW(L"Win32"));
			}

			if (includesChanged)
			{
				// [case: 105070]
				IncludeDirs::Reset();
			}

			break;
		}
	}
	catch (const _com_error& e)
	{
		CString sMsg;
		CString__FormatA(sMsg, _T("Unexpected exception 0x%X (%s) in %s()\n"), (uint)e.Error(), e.ErrorMessage(),
		                 __FUNCTION__);

		VALOGEXCEPTION(sMsg);
		_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::SetupIncludePaths 1");
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("Unexpected exception (2) in VsSolutionInfoImpl::SetupIncludePaths");
		_ASSERTE(!"Fix the bad code that caused this exception in VsSolutionInfoImpl::SetupIncludePaths 2");
	}
#endif // !SEAN
}

// SetupVcEnvironment
// ------------------
// Call this:
//	after closing IDE options dlg
//	when loading solution that has C++ projects (during GetSolutionInfo)
//	before opening VA options dlg (but only if not previously set)
//	when opening C_File with empty solution (but only if not previously set)
void SetupVcEnvironment(bool force /*= true*/)
{
	static bool sDoneOnce = false;
	if (!force && sDoneOnce)
		return;

	sDoneOnce = true;

#if defined(RAD_STUDIO)
	return;
#else
	// #newVsVersion
#ifdef _WIN64
	if (gShellAttr->IsDevenv17())
		SetupVcEnvironment<VCProjectEngineLibrary17::VCProject, VCProjectEngineLibrary17::IVCCollection,
		                   VCProjectEngineLibrary17::VCProjectEngine, VCProjectEngineLibrary17::VCPlatform>();
	else
#endif
	    if (gShellAttr->IsDevenv16())
		SetupVcEnvironment<VCProjectEngineLibrary16::VCProject, VCProjectEngineLibrary16::IVCCollection,
		                   VCProjectEngineLibrary16::VCProjectEngine, VCProjectEngineLibrary16::VCPlatform>();
	else if (gShellAttr->IsDevenv15())
		SetupVcEnvironment<VCProjectEngineLibrary15::VCProject, VCProjectEngineLibrary15::IVCCollection,
		                   VCProjectEngineLibrary15::VCProjectEngine, VCProjectEngineLibrary15::VCPlatform>();
	else if (gShellAttr->IsDevenv14())
		SetupVcEnvironment<VCProjectEngineLibrary14::VCProject, VCProjectEngineLibrary14::IVCCollection,
		                   VCProjectEngineLibrary14::VCProjectEngine, VCProjectEngineLibrary14::VCPlatform>();
	else if (gShellAttr->IsDevenv12())
		SetupVcEnvironment<VCProjectEngineLibrary12::VCProject, VCProjectEngineLibrary12::IVCCollection,
		                   VCProjectEngineLibrary12::VCProjectEngine, VCProjectEngineLibrary12::VCPlatform>();
	else if (gShellAttr->IsDevenv11())
		SetupVcEnvironment<VCProjectEngineLibrary11::VCProject, VCProjectEngineLibrary11::IVCCollection,
		                   VCProjectEngineLibrary11::VCProjectEngine, VCProjectEngineLibrary11::VCPlatform>();
	else if (gShellAttr->IsDevenv10())
		SetupVcEnvironment<VCProjectEngineLibrary10::VCProject, VCProjectEngineLibrary10::IVCCollection,
		                   VCProjectEngineLibrary10::VCProjectEngine, VCProjectEngineLibrary10::VCPlatform>();
	else if (gShellAttr->IsDevenv9())
		SetupVcEnvironment<VCProjectEngineLibrary9::VCProject, VCProjectEngineLibrary9::IVCCollection,
		                   VCProjectEngineLibrary9::VCProjectEngine, VCProjectEngineLibrary9::VCPlatform>();
	else if (gShellAttr->IsDevenv8())
		SetupVcEnvironment<VCProjectEngineLibrary8::VCProject, VCProjectEngineLibrary8::IVCCollection,
		                   VCProjectEngineLibrary8::VCProjectEngine, VCProjectEngineLibrary8::VCPlatform>();
	else if (gShellAttr->IsDevenv7())
		SetupVcEnvironment<VCProjectEngineLibrary7::VCProject, VCProjectEngineLibrary7::IVCCollection,
		                   VCProjectEngineLibrary7::VCProjectEngine, VCProjectEngineLibrary7::VCPlatform>();
	else if (gShellAttr->IsDevenv())
		_ASSERTE(!"failed SetupVcEnvironment init -- new version of VS??");
#endif
}

namespace ProjectReparse
{
std::set<CStringW> gProjectsToReparse;
CCriticalSection gProjectReparseLock;
} // namespace ProjectReparse

void ProjectReparse::QueueProjectForReparse(const CStringW& project)
{
	if (g_CodeGraphWithOutVA)
		return;
	_ASSERTE(gVaMainWnd);
	if (!gVaMainWnd)
		return;
	gVaMainWnd->KillTimer(IDT_CheckSolutionProjectsForUpdates);
	{
		AutoLockCs l(gProjectReparseLock);
		gProjectsToReparse.insert(project);
	}
	gVaMainWnd->SetTimer(IDT_CheckSolutionProjectsForUpdates, 2000, NULL);
}

void ProjectReparse::ClearProjectReparseQueue()
{
	if (g_CodeGraphWithOutVA)
		return;
	if (gVaMainWnd->GetSafeHwnd())
		gVaMainWnd->KillTimer(IDT_CheckSolutionProjectsForUpdates);

	AutoLockCs l(gProjectReparseLock);
	gProjectsToReparse.clear();
}

static ProjectInfoPtr LookupVaProject(const CStringW& proj)
{
	RWLockReader lck;
	_ASSERTE(GlobalProject);
	const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
	for (Project::ProjectMap::const_iterator projIt = projMap.begin(); projIt != projMap.end(); ++projIt)
	{
		CStringW projName((*projIt).first);
		if (!projName.CompareNoCase(proj))
			return (*projIt).second;
	}

	return NULL;
}

void ProjectReparse::ProcessQueuedProjects()
{
	_ASSERTE(gShellAttr->IsDevenv8OrHigher());

	if (gShellIsUnloading)
		throw UnloadingException();

	TimeTrace tr("ProcessQueuedProjects");
	std::set<CStringW> tmp;

	{
		AutoLockCs l(gProjectReparseLock);
		tmp.swap(gProjectsToReparse);
	}

	std::unique_ptr<VsSolutionInfo> info(::CreateVsSolutionInfo());
	if (info && GlobalProject)
		info->SetSolutionHash(GlobalProject->GetSolutionHash());
	AutoLockCs lck(info->GetLock());

	ScopedIncrementAtomic si(&gSolutionInfoActiveGathering);
	if (gSolutionInfoActiveGathering > 1)
	{
		// [case: 87629]
		// ignore -- if someone else is already gathering info, let them finish
		// no need to requeue since they are doing a load
		vLog("warn: ProcessQueuedProjects exit -- someone else active");
		return;
	}

	VsProjectList rootProjects;
	int rootLevelProjectCnt = GetSolutionRootProjectList(gDte, rootProjects);
	for (auto it : tmp)
		info->GetMinimalProjectInfo(it, rootLevelProjectCnt, rootProjects);

	// [case: 73816] [case: 96945]
	info->FinishGetSolutionInfo(false);

	VsSolutionInfo::ProjectSettingsMap& solInfoMap = info->GetInfoMap();
	for (VsSolutionInfo::ProjectSettingsMap::iterator solit = solInfoMap.begin(); solit != solInfoMap.end(); ++solit)
	{
		CStringW projName = (*solit).first;
		ProjectInfoPtr prjInf = ::LookupVaProject(projName);
		if (prjInf)
		{
			VsSolutionInfo::ProjectSettingsPtr curSettings((*solit).second);
			if (curSettings)
				prjInf->MinimalUpdateFrom(*curSettings);
		}
	}

	if (GlobalProject)
	{
		// [case=73812]
		// Not just AVR because vs2005/vs2008 don't fire the
		// VcProjectPropertyChangedNew event either (introduced in vs2010).
		GlobalProject->GetProjAdditionalDirs();
	}
}

#pragma warning(push)
#pragma warning(disable : 4555)
template <int projectEngineVersion, typename VCProjectT, const IID* dispVCProjectEngineEventsT,
          const GUID* LIBID_VCProjectEngineLibraryT, typename VCConfigurationT, typename VCFileT>
class VCProjectEngineEventsSink
    : public CComObjectRootEx<CComSingleThreadModel>,
      public IVcProjectEngineEventSink,
      // 	public IDispatchImpl<IVcProjectEngineEventSink>,
      public IDispEventImpl<1,
                            VCProjectEngineEventsSink<projectEngineVersion, VCProjectT, dispVCProjectEngineEventsT,
                                                      LIBID_VCProjectEngineLibraryT, VCConfigurationT, VCFileT>,
                            dispVCProjectEngineEventsT, LIBID_VCProjectEngineLibraryT, projectEngineVersion, 0>
{
#pragma warning(pop)
	long mRefCnt = 0;
	int mAdvised = 0;

  public:
	VCProjectEngineEventsSink()
	{
	}

	virtual ~VCProjectEngineEventsSink()
	{
		_ASSERTE(!mRefCnt);
		_ASSERTE(!mAdvised);
	}

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	STDMETHOD_(ULONG, AddRef)() throw()
	{
		::InterlockedIncrement(&mRefCnt);
		return (ULONG)mRefCnt;
	}

	STDMETHOD_(ULONG, Release)() throw()
	{
		if (::InterlockedDecrement(&mRefCnt) == 0)
		{
			delete this;
			return 0;
		}
		return (ULONG)mRefCnt;
	}

	STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) throw()
	{
		if (iid == IID_IUnknown)
		{
			*ppvObject = static_cast<IUnknown*>(this);
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual void ManageSync(IUnknown* e, bool advise)
	{
		if (advise)
		{
			__super::DispEventAdvise(e);
			++mAdvised;
		}
		else if (mAdvised)
		{
			--mAdvised;
			__super::DispEventUnadvise(e);
		}
	}

	virtual bool IsSynced() const
	{
		return mAdvised > 0;
	}

#if (_MSC_VER >= 1900) // vs2015
#undef SINK_ENTRY_INFO
#define SINK_ENTRY_INFO(id, iid, dispid, fn, info)                                                                     \
	{id,                                                                                                               \
	 &iid,                                                                                                             \
	 (int)(INT_PTR)(reinterpret_cast<ATL::_IDispEventLocator<id, &iid>*>((_atl_event_classtype*)8)) - 8,               \
	 dispid,                                                                                                           \
	 (void (__stdcall _atl_event_classtype::*)())fn,                                                                   \
	 info},
#endif

#pragma warning(push)
#pragma warning(disable : 4191 4640 4946)
	BEGIN_SINK_MAP(VCProjectEngineEventsSink)
	SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x113, ItemAdded)
	SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x114, ItemRemoved)
	SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x115, ItemRenamed)
	// 		SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x116, ItemMoved) - doesn't fire in vs2010, but does fire in
	// vs2019
	SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x117, ItemPropertyChanged)
	SINK_ENTRY_EX(1, *dispVCProjectEngineEventsT, 0x11d, VcProjectPropertyChangedNew)
	END_SINK_MAP()
#pragma warning(pop)

	void __stdcall ItemAdded(IDispatch* item, IDispatch* /*itemParent*/)
	{
		const CStringW itemName(GetItemName(item));
		if (itemName.IsEmpty())
			return;
		const CStringW projName(GetProjectNameFromFile(item));
		if (-1 != projName.Find(L"\\SingleFileISense\\_sfi_"))
		{
			// [case: 100216]
			return;
		}

		if (GlobalProject)
			GlobalProject->AddFile(projName, itemName, true);
	}

	void __stdcall ItemRemoved(IDispatch* item, IDispatch* /*itemParent*/)
	{
		const CStringW itemName(GetItemName(item));
		if (itemName.IsEmpty())
			return;
		const CStringW projName(GetProjectNameFromFile(item));
		if (-1 != projName.Find(L"\\SingleFileISense\\_sfi_"))
		{
			// [case: 100216]
			return;
		}

		if (GlobalProject)
			GlobalProject->RemoveFile(projName, itemName, true);
	}

	void __stdcall ItemRenamed(IDispatch* item, IDispatch* /*itemParent*/, BSTR oldName)
	{
		const CStringW newItemName(GetItemName(item));
		if (newItemName.IsEmpty())
			return;
		const CStringW projName(GetProjectNameFromFile(item));

		CStringW oldItemName(oldName);
		if (oldItemName.FindOneOf(L"\\/") == -1)
		{
			// oldName does not include path - take path from newName
			oldItemName = newItemName;
			int pos1 = oldItemName.ReverseFind(L'\\');
			int pos2 = oldItemName.ReverseFind(L'/');
			if (pos1 == -1 && pos2 == -1)
				return;

			oldItemName = oldItemName.Left(max(pos1, pos2) + 1);
			oldItemName += oldName;
		}

		if (GlobalProject)
		{
			if (oldItemName.IsEmpty())
				GlobalProject->AddFile(projName, newItemName, true);
			else
				GlobalProject->RenameFile(projName, oldItemName, newItemName);
		}
	}

	void __stdcall ItemMoved(IDispatch* item, IDispatch* /*NewParent*/, IDispatch* OldParent)
	{
		// Not tested - didn't fire in VS2010
		// fires in vs2019 when filed moved from one filter to another
		// 		const CStringW newItemName(GetItemName(item));
		// 		if (newItemName.IsEmpty())
		// 			return;
		// 		const CStringW projName(GetProjectNameFromFile(item));
		// 		const CStringW basename(Basename(newItemName));
		//
		// 		CStringW oldItemName(GetItemName(OldParent));
		// 		if (!oldItemName.IsEmpty())
		// 			oldItemName += basename;
		//
		// 		if (GlobalProject && !oldItemName.IsEmpty())
		// 			GlobalProject->RemoveFile(projName, oldItemName, true);
		// 		if (GlobalProject)
		// 			GlobalProject->AddFile(projName, newItemName, true);
	}

	void __stdcall ItemPropertyChanged(IDispatch* item, IDispatch* /*Tool*/, long propertyId)
	{
		// [case: 19933] file moved via drag and drop in sol exp
		// http://msdn.microsoft.com/en-us/library/Aa713017.aspx
		if (402 != propertyId)
			return;

		const CStringW newItemName(GetItemName(item));
		if (newItemName.IsEmpty())
			return;

		const CStringW projName(GetProjectNameFromFile(item));
		const CStringW basename(Basename(newItemName));
		if (GlobalProject && !basename.IsEmpty())
			GlobalProject->RemoveFileWithoutPath(projName, basename);
		if (GlobalProject)
			GlobalProject->AddFile(projName, newItemName, true);
	}

	void __stdcall VcProjectPropertyChangedNew(IDispatch* Item, BSTR bstrPropertySheet, BSTR bstrItemType,
	                                           BSTR bstrPropertyName)
	{
		bool careAboutTheChange = false;
		const CStringW propName(bstrPropertyName);
		if (!propName.CompareNoCase(L"AdditionalIncludeDirectories"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"IncludeSearchPath"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"NMakeIncludeSearchPath"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"ForcedIncludeFiles"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"ForcedIncludes"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"NMakeForcedIncludes"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"CompileAsManaged") || !propName.CompareNoCase(L"CLRSupport"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"CompileAsWinRT"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"IncludePath"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"SourcePath"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"LibraryPath"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"WindowsTargetPlatformVersion"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"PlatformToolset"))
			careAboutTheChange = true;
		else if (!propName.CompareNoCase(L"ExternalIncludePath") &&
				((gShellAttr && gShellAttr->IsDevenv17OrHigher()) || (Psettings && Psettings->mForceExternalIncludeDirectories)))
			careAboutTheChange = true;

		if (!careAboutTheChange)
			return;

		CStringW projName;
		projName = GetProjectNameFromConfig(Item);
		if (projName.IsEmpty())
		{
			projName = GetProjectNameFromFile(Item);
			if (projName.IsEmpty())
			{
				// _ASSERTE(GlobalProject->IsBusy() && "project engine property change - failed to get project name");
				return;
			}
		}

		vLog("VcProjPropChg %s %s\n", (LPCTSTR)CString(propName), (LPCTSTR)CString(projName));

		RWLockReader lck;
		const Project::ProjectMap& map = GlobalProject->GetProjectsForRead(lck);
		for (Project::ProjectMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
		{
			if (projName.CompareNoCase((*iter).first))
				continue;

			ProjectInfoPtr prjInf = (*iter).second;
			if (!prjInf)
				continue;

			::InvalidateProjectCache(projName);

			if (gFileFinder)
			{
				// [case: 64681] clear file lookup cache since dirs have changed
				gFileFinder->Invalidate();
			}

			prjInf->CheckForChanges();
			break;

			// code for property sheet match up
			// 			const FileList & propSheets(prjInf->GetPropertySheets());
			// 			for (FileList::const_iterator it = propSheets.begin(); it != propSheets.end(); ++it)
			// 			{
			// 				const CStringW cur((*it).mFilename);
			// 				if (!cur.CompareNoCase(prjFile))
			// 					prjInf->CheckForChanges();
			// 				break;
			// 			}
		}
	}

  private:
	CComBSTR GetItemName(IDispatch* item)
	{
		CComBSTR name;
		CComQIPtr<VCFileT> vcFile{item};
		if (vcFile)
			vcFile->get_FullPath(&name);
		return name;
	}

	CComBSTR GetProjectNameFromConfig(IDispatch* item)
	{
		CComBSTR projName;
		CComQIPtr<VCConfigurationT> vcFileCfg{item};
		if (vcFileCfg)
		{
			IDispatch* dispProject = NULL;
			vcFileCfg->get_project(&dispProject);
			if (dispProject)
			{
				CComQIPtr<VCProjectT> vcProject{dispProject};
				if (vcProject)
					vcProject->get_ProjectFile(&projName);
			}
		}
		return projName;
	}

	CComBSTR GetProjectNameFromFile(IDispatch* item)
	{
		CComBSTR projName;
		CComQIPtr<VCFileT> vcFile{item};
		if (vcFile)
		{
			IDispatch* dispProject = NULL;
			vcFile->get_project(&dispProject);
			if (dispProject)
			{
				CComQIPtr<VCProjectT> vcProject{dispProject};
				if (vcProject)
					vcProject->get_ProjectFile(&projName);
			}
		}
		return projName;
	}
};

IVcProjectEngineEventSink* CreateVcProjectEngineEventSink()
{
	// #newVsVersion
#ifdef _WIN64
	if (gShellAttr->IsDevenv17())
		return new VCProjectEngineEventsSink<17, // VCProjectEngine for VS2022
		                                     VCProjectEngineLibrary17::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary17::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary17, VCProjectEngineLibrary17::VCConfiguration,
		                                     VCProjectEngineLibrary17::VCFile>();
#endif

	if (gShellAttr->IsDevenv16())
		return new VCProjectEngineEventsSink<16, // VCProjectEngine for VS2019
		                                     VCProjectEngineLibrary16::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary16::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary16, VCProjectEngineLibrary16::VCConfiguration,
		                                     VCProjectEngineLibrary16::VCFile>();

	if (gShellAttr->IsDevenv15())
		return new VCProjectEngineEventsSink<15, // VCProjectEngine for VS2017
		                                     VCProjectEngineLibrary15::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary15::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary15, VCProjectEngineLibrary15::VCConfiguration,
		                                     VCProjectEngineLibrary15::VCFile>();

	if (gShellAttr->IsDevenv14())
		return new VCProjectEngineEventsSink<14, // VCProjectEngine for VS2015
		                                     VCProjectEngineLibrary14::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary14::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary14, VCProjectEngineLibrary14::VCConfiguration,
		                                     VCProjectEngineLibrary14::VCFile>();

	if (gShellAttr->IsDevenv12())
		return new VCProjectEngineEventsSink<12, // VCProjectEngine for VS2013
		                                     VCProjectEngineLibrary12::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary12::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary12, VCProjectEngineLibrary12::VCConfiguration,
		                                     VCProjectEngineLibrary12::VCFile>();

	if (gShellAttr->IsDevenv11())
		return new VCProjectEngineEventsSink<11, // VCProjectEngine for VS2012
		                                     VCProjectEngineLibrary11::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary11::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary11, VCProjectEngineLibrary11::VCConfiguration,
		                                     VCProjectEngineLibrary11::VCFile>();

	if (gShellAttr->IsDevenv10())
		return new VCProjectEngineEventsSink<10, // VCProjectEngine for VS2010
		                                     VCProjectEngineLibrary10::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary10::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary10, VCProjectEngineLibrary10::VCConfiguration,
		                                     VCProjectEngineLibrary10::VCFile>();

	if (gShellAttr->IsDevenv9())
		return new VCProjectEngineEventsSink<9, VCProjectEngineLibrary9::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary9::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary9, VCProjectEngineLibrary9::VCConfiguration,
		                                     VCProjectEngineLibrary9::VCFile>();

	if (gShellAttr->IsDevenv8())
		return new VCProjectEngineEventsSink<8, VCProjectEngineLibrary8::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary8::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary8, VCProjectEngineLibrary8::VCConfiguration,
		                                     VCProjectEngineLibrary8::VCFile>();

	if (gShellAttr->IsDevenv7())
		return new VCProjectEngineEventsSink<1, // VCProjectEngine for vs2003 is version 1
		                                     VCProjectEngineLibrary7::VCProject,
		                                     &__uuidof(VCProjectEngineLibrary7::_dispVCProjectEngineEvents),
		                                     &LIBID_VCProjectEngineLibrary7, VCProjectEngineLibrary7::VCConfiguration,
		                                     VCProjectEngineLibrary7::VCFile>();

	if (gShellAttr->IsDevenv())
		_ASSERTE(!"failed CreateVcProjectEngineEventSink init -- new version of VS??");

	return NULL;
}

class ProjectSettingsFileWriter
{
	CFileW mFile;
	CStringW mLn;

  public:
	ProjectSettingsFileWriter(const CStringW& oFile)
	{
		if (!mFile.Open(oFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
		{
			vLog("ProjectInfoCache: failed to create file (%s)\n", (LPCTSTR)CString(oFile));
			throw CacheSaveException("failed to create file");
		}
	}
	~ProjectSettingsFileWriter()
	{
		mFile.Close();
	}

	void WriteFileInfo(const WCHAR* tag, const FileInfo& it)
	{
		_ASSERTE(tag && *tag && 4 == wcslen(tag));
		CString__FormatW(mLn, L"%ls:%ls\n", tag, (LPCWSTR)it.mFilename);
		mFile.Write(mLn, mLn.GetLength() * sizeof(WCHAR));
	}

	void Write(const WCHAR* tag, const CStringW& txt)
	{
		_ASSERTE(tag && *tag && 4 == wcslen(tag));
		CString__FormatW(mLn, L"%ls:%ls\n", tag, (LPCWSTR)txt);
		mFile.Write(mLn, mLn.GetLength() * sizeof(WCHAR));
	}

	void WriteBool(const WCHAR* tag, bool val)
	{
		_ASSERTE(tag && *tag && 4 == wcslen(tag));
		CString__FormatW(mLn, L"%ls:%d\n", tag, val);
		mFile.Write(mLn, mLn.GetLength() * sizeof(WCHAR));
	}
};

//
// binary_function variation of call_mem_fun_t
//
template <class mem_fun_t,  // member function pointer type
          class instance_t, // class type containing member function
          class param_t1,   // first parameter type of member function
          class param_t2>   // second parameter type of member function
struct call_mem_fun_t       /*: public std::binary_function<param_t1, param_t2, void> */
{
	// constructor
	//  pmf - pointer to member function
	//  inst - instance to use when calling member function
	//  p1 - value to pass as first arg to each call to m_pmf
	explicit call_mem_fun_t(mem_fun_t pmf, instance_t* inst, param_t1 p1) : m_pmf(pmf), m_inst(inst), mP1(p1)
	{
	}

	// functor
	void operator()(param_t2& val2) const
	{
		(m_inst->*m_pmf)(mP1, val2);
	}

  private:
	mem_fun_t m_pmf;    // pointer to member function
	instance_t* m_inst; // pointer to instance in order to call member function
	param_t1 mP1;       // value to be passed as first arg to every m_pmf
};

template <class param_t1, // param_t goes first since compiler can't figure it out
          class param_t2,
          class mem_fun_t, // member function pointer type
          class instance_t>
inline call_mem_fun_t<mem_fun_t, instance_t, param_t1, param_t2> // return type
call_mem_fun(mem_fun_t pmf, instance_t* inst, param_t1 p1)
{
	return call_mem_fun_t<mem_fun_t, instance_t, param_t1, param_t2>(pmf, inst, p1);
}

CCriticalSection gCacheFileLock;

void VsSolutionInfo::ProjectSettings::SaveProjectCache()
{
	_ASSERTE(Psettings && Psettings->mCacheProjectInfo);
	_ASSERTE(!mDeferredProject);
	if (mProjectFile.IsEmpty())
		throw CacheSaveException("no proj file");
	if (!::IsFile(mProjectFile))
		throw CacheSaveException("proj not a file");
	if (!::IsProjectCacheable(mProjectFile))
		throw CacheSaveException("don't cache .net core or empty project");

	const CStringW dbDir(VaDirs::GetDbDir());
	if (!::IsDir(dbDir + L"ProjectInfo"))
		::CreateDir(dbDir + L"ProjectInfo");
	const CStringW cacheFile(::GetCacheFile(mProjectFile));

	AutoLockCs l(gCacheFileLock);
	{
		ProjectSettingsFileWriter fw(cacheFile);
		// don't do mProjectFileDir or mProjectFileTimestamp

		CStringW hashStr;
		GetFileHashStr(mProjectFile, hashStr);
		fw.Write(L"HASH", hashStr);
		if (-1 != mProjectFile.Find(L"4"))
		{
			const WTString tmp(mProjectFile);
			if (-1 != tmp.FindNoCase("ue4") || -1 != tmp.FindNoCase("ue5"))
			{
				// [case: 140975]
				::GetFileLineSortedHashStr(mProjectFile, hashStr);
				fw.Write(L"HSH2", hashStr);
			}
		}
		fw.Write(L"PROJ", mProjectFile);
		fw.WriteBool(L"CISI", mCppIgnoreStandardIncludes);
		fw.WriteBool(L"CLR_", mCppHasManagedConfig);
		fw.WriteBool(L"WRT_", mCppIsWinRT);
		if (mIsVcProject)
		{
			fw.WriteBool(L"VCPJ", mIsVcProject);
			fw.WriteBool(L"FPL_", mVcFplEnabledWhenCacheCreated);

			if ((mConfigFullIncludeDirs.empty() && mPlatformIncludeDirs.empty()) || mConfigIntermediateDirs.empty())
			{
				// assume missing config if we have no include dirs or no config intermediate dirs
				mVcMissingConfigs = true;
			}

			fw.WriteBool(L"VCMC", mVcMissingConfigs);
		}
		
		if (!mPrecompiledHeaderFile.IsEmpty())
			fw.Write(L"PCHF", mPrecompiledHeaderFile);

		std::for_each(
		    mReferences.begin(), mReferences.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"MREF"));

		std::vector<CStringW> propSheetsAndTimes;
		CStringW tmp;
		for (FileList::const_iterator it = mPropertySheets.begin(); it != mPropertySheets.end(); ++it)
		{
			FILETIME ts;
			const CStringW curFile((*it).mFilename);
			::GetFTime(curFile, &ts);
			CStringW curFileHash;
			GetFileHashStr(curFile, curFileHash);
			CString__FormatW(tmp, L"%ls:%lx.%lx:%ls", (LPCWSTR)curFile, ts.dwHighDateTime, ts.dwLowDateTime,
			                 (LPCWSTR)curFileHash);
			propSheetsAndTimes.push_back(tmp);
		}

		std::for_each(propSheetsAndTimes.begin(), propSheetsAndTimes.end(),
		              call_mem_fun<const WCHAR*, const CStringW>(&ProjectSettingsFileWriter::Write, &fw, L"PROP"));

		std::for_each(
		    mSubProjects.begin(), mSubProjects.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"SUBP"));

		std::for_each(
		    mConfigFullIncludeDirs.begin(), mConfigFullIncludeDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"CFID"));

		std::for_each(
		    mConfigIntermediateDirs.begin(), mConfigIntermediateDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"CID_"));

		std::for_each(
		    mConfigForcedIncludes.begin(), mConfigForcedIncludes.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"CFI_"));

		std::for_each(
		    mConfigUsingDirs.begin(), mConfigUsingDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"CUD_"));

		std::for_each(
		    mPlatformIncludeDirs.begin(), mPlatformIncludeDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"PID_"));

		std::for_each(
		    mPlatformLibraryDirs.begin(), mPlatformLibraryDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"PLD_"));

		std::for_each(
		    mPlatformSourceDirs.begin(), mPlatformSourceDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"PSD_"));

		std::for_each(
		    mFiles.begin(), mFiles.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"FILE"));

		std::for_each(
		    mPlatformExternalIncludeDirs.begin(), mPlatformExternalIncludeDirs.end(),
		    call_mem_fun<const WCHAR*, const FileInfo>(&ProjectSettingsFileWriter::WriteFileInfo, &fw, L"PEID"));
	}

	::SetFTime(cacheFile, &mProjectFileTimestamp);
}

VsSolutionInfo::ProjectSettings::ProjectSettings(const ProjectSettings* rhs)
{
	mProjectFile = rhs->mProjectFile;
	mProjectFileDir = rhs->mProjectFileDir;
	mProjectFileTimestamp = rhs->mProjectFileTimestamp;
	mVsProjectName = rhs->mVsProjectName;

	mFiles.Add(rhs->mFiles);
#ifdef AVR_STUDIO
	mDeviceFiles.Add(rhs->mDeviceFiles);
#endif
	mReferences.Add(rhs->mReferences);
	mPropertySheets.Add(rhs->mPropertySheets);
	mSubProjects.Add(rhs->mSubProjects);

	mConfigFullIncludeDirs.Add(rhs->mConfigFullIncludeDirs);
	mConfigIntermediateDirs.Add(rhs->mConfigIntermediateDirs);
	mConfigForcedIncludes.Add(rhs->mConfigForcedIncludes);
	mConfigUsingDirs.Add(rhs->mConfigUsingDirs);

	mPlatformIncludeDirs.Add(rhs->mPlatformIncludeDirs);
	mPlatformLibraryDirs.Add(rhs->mPlatformLibraryDirs);
	mPlatformSourceDirs.Add(rhs->mPlatformSourceDirs);
	mPlatformExternalIncludeDirs.Add(rhs->mPlatformExternalIncludeDirs);

	mCppHasManagedConfig = rhs->mCppHasManagedConfig;
	mCppIsWinRT = rhs->mCppIsWinRT;
	mCppIgnoreStandardIncludes = rhs->mCppIgnoreStandardIncludes;
	mIsVcProject = rhs->mIsVcProject;
	mVcFplEnabledWhenCacheCreated = rhs->mVcFplEnabledWhenCacheCreated;
	mVcMissingConfigs = rhs->mVcMissingConfigs;
	mPseudoProject = rhs->mPseudoProject;
	mDeferredProject = rhs->mDeferredProject;
}

void VsSolutionInfo::ProjectSettings::LoadProjectCache(const CStringW& projPath)
{
	_ASSERTE(Psettings && Psettings->mCacheProjectInfo);
	const CStringW cacheFile(::GetCacheFile(projPath));

	AutoLockCs l(gCacheFileLock);
	if (!::IsFile(cacheFile))
		throw CacheLoadException();

	CStringW hashStr;

	// can't check file time while CFileW has it open
	if (!::FileTimesAreEqual(cacheFile, projPath))
	{
		// even if the file times don't match, the content might not have
		// changed.  Therefore calculate the hash of the project file, and
		// compare with cached value below.  throw if can't calculate hash.
		if (!GetFileHashStr(projPath, hashStr))
			throw CacheLoadException();
	}

	CStringW projectSettings;

	// read in the file
	{
		CFileW file;
		if (!file.Open(cacheFile, CFile::modeRead | CFile::shareExclusive | CFile::modeNoInherit))
			throw CacheLoadException();

		const ULONG size = (ULONG)file.GetLength();
		if (!size)
			throw CacheLoadException();

		LPWSTR pBuf = projectSettings.GetBufferSetLength(int(size / sizeof(WCHAR)) + 1);
		if (!pBuf)
			throw CacheLoadException();

		const UINT read = file.Read(pBuf, size);
		file.Close();
		pBuf[size / sizeof(WCHAR)] = L'\0';
		projectSettings.ReleaseBuffer();
		if (read != size)
			throw CacheLoadException();
	}

	bool hashRead = false;
	CStringW tag;
	CStringW curItem;

	// parse the content (ignoring mProjectFileDir and mProjectFileTimestamp)
	int prevLinebreakPos = 0;
	int lineNo = 0;
	for (int linebreakPos = projectSettings.Find(L'\n'); linebreakPos != -1;
	     linebreakPos = projectSettings.Find(L'\n', linebreakPos), lineNo++)
	{
		tag = projectSettings.Mid(prevLinebreakPos, linebreakPos - prevLinebreakPos);
		++linebreakPos;
		prevLinebreakPos = linebreakPos;

		if (tag.GetLength() < 5)
		{
			_ASSERTE(!"short line read in VsSolutionInfo::ProjectSettings::LoadProjectCache");
			continue;
		}

		_ASSERTE(tag[4] == L':');
		curItem = tag.Mid(5);
		tag = tag.Left(4);

		if (tag.IsEmpty() || curItem.IsEmpty())
		{
			_ASSERTE(!"no content in VsSolutionInfo::ProjectSettings::LoadProjectCache");
			continue;
		}

		// "HASH" must be on first line
		if (lineNo == 0)
		{
			if (tag != L"HASH")
				throw CacheLoadException("hash not first");

			// if file times matched, then we didn't calculate the hash,
			// therefore trust that the cached hash is correct
			if (!hashStr.IsEmpty())
			{
				if (hashStr != curItem)
				{
					// [case: 140975]
					// hash did not match
					// check for HSH2 to compare hash of file sorted by line
					// HSH2 is usually not present

					// get next item
					int nextLinebreakPos = projectSettings.Find(L'\n', linebreakPos);
					if (-1 == nextLinebreakPos)
						throw CacheLoadException();

					tag = projectSettings.Mid(prevLinebreakPos, nextLinebreakPos - prevLinebreakPos);
					if (tag.GetLength() < 5)
						throw CacheLoadException();

					_ASSERTE(tag[4] == L':');
					curItem = tag.Mid(5);
					tag = tag.Left(4);

					if (tag != L"HSH2")
						throw CacheLoadException();

					if (curItem.IsEmpty())
						throw CacheLoadException();

					// now that we've confirmed HSH2 is next item, calc hash on sorted contents of project
					::GetFileLineSortedHashStr(projPath, hashStr);

					// last chance to stick with cached info
					if (hashStr != curItem)
						throw CacheLoadException();

					++lineNo;
					linebreakPos = nextLinebreakPos;
					++linebreakPos;
					prevLinebreakPos = linebreakPos;
				}
			}

			hashRead = true;
			continue;
		}

		bool tagRead = true;

		if (tag[0] == L'F')
		{
			if (tag == L"FILE")
				mFiles.Add(curItem);
			else if (tag == L"FPL_")
			{
				mVcFplEnabledWhenCacheCreated = ::_wtoi(curItem) != 0;
				if (mVcFplEnabledWhenCacheCreated && GlobalProject && !GlobalProject->IsVcFastProjectLoadEnabled())
				{
					// [case: 103729]
					// invalidate because FPL prevents finding all prop sheet dependencies.
					// get_imports only returns a single prop sheet in 15.3 compared to
					// multiple sheets when directly querying config prop sheets.
					// property sheets are used for cache invalidation, but query of
					// property sheets causes load of real (not read-only) project in vs2017.
					// When FPL is enabled for both store and load, that's ok.
					throw CacheLoadException("FPL disabled: invalidates cache created when FPL enabled");
				}
			}
			else
				tagRead = false;
		}
		else if (tag[0] == L'P')
		{
			if (tag == L"PROP")
			{
				// curItem is "filepath:%x.%x:hash"
				int pos = curItem.ReverseFind(L':');
				if (-1 == pos)
					throw CacheLoadException();
				CStringW cachedPropFileHash = curItem.Mid(pos + 1);
				curItem = curItem.Left(pos);

				pos = curItem.ReverseFind(L':');
				if (-1 == pos)
					throw CacheLoadException();
				CString tmp{(const wchar_t*)curItem.Mid(pos + 1)};
				curItem = curItem.Left(pos);

				if (!::IsFile(curItem))
					throw CacheLoadException();

				FILETIME ft1 = {0, 0};
				sscanf(tmp, "%lx.%lx", &ft1.dwHighDateTime, &ft1.dwLowDateTime);
				if (!::FileTimesAreEqual(&ft1, curItem))
				{
					CStringW propFileHash;
					if (!GetFileHashStr(curItem, propFileHash))
						throw CacheLoadException();

					if (cachedPropFileHash != propFileHash)
						throw CacheLoadException();
				}

				mPropertySheets.Add(curItem);
			}
			else if (tag == L"PID_")
				mPlatformIncludeDirs.Add(curItem);
			else if (tag == L"PSD_")
				mPlatformSourceDirs.Add(curItem);
			else if (tag == L"PLD_")
				mPlatformLibraryDirs.Add(curItem);
			else if (tag == L"PROJ")
				mProjectFile = curItem;
			else if (tag == L"PEID")
				mPlatformExternalIncludeDirs.Add(curItem);
			else if (tag == L"PCHF")
				mPrecompiledHeaderFile = curItem;
			else
				tagRead = false;
		}
		else if (tag[0] == L'C')
		{
			if (tag == L"CFI_")
				mConfigForcedIncludes.Add(curItem);
			else if (tag == L"CID_")
				mConfigIntermediateDirs.Add(curItem);
			else if (tag == L"CFID")
				mConfigFullIncludeDirs.Add(curItem);
			else if (tag == L"CUD_")
				mConfigUsingDirs.Add(curItem);
			else if (tag == L"CLR_")
				mCppHasManagedConfig = ::_wtoi(curItem) != 0;
			else if (tag == L"CISI")
				mCppIgnoreStandardIncludes = ::_wtoi(curItem) != 0;
			else
				tagRead = false;
		}
		else if (tag[0] == L'M' && tag == L"MREF")
			mReferences.Add(curItem);
		else if (tag[0] == L'S' && tag == L"SUBP")
			mSubProjects.Add(curItem);
		else if (tag[0] == L'V')
		{
			if (tag == L"VCPJ")
				mIsVcProject = ::_wtoi(curItem) != 0;
			else if (tag == L"VCMC")
			{
				mVcMissingConfigs = ::_wtoi(curItem) != 0;
				if (mVcMissingConfigs)
					throw CacheLoadException("missing configs at cache creation invalidates cache");
			}
			else
				tagRead = false;
		}
		else if (tag[0] == L'W' && tag == L"WRT_")
			mCppIsWinRT = ::_wtoi(curItem) != 0;
		else if (tag[0] == L'H')
		{
			if (tag == L"HSH2")
			{
				// ignore here, only handled above directly after "HASH"
			}
			else if (tag == L"HASH")
			{
				// must be on first line
				if (lineNo != 0)
					throw CacheLoadException("hash line");
			}
		}
		else
			tagRead = false;

		if (!tagRead)
		{
			_ASSERTE(!"unknown tag in VsSolutionInfo::ProjectSettings::LoadProjectCache");
			vLog("ProjectInfoCache: unknown tag %s (%s)\n", (LPCTSTR)CString(tag), (LPCTSTR)CString(curItem));
		}
	}

	if (!hashRead)
		throw CacheLoadException("missing hash");
}

static void GetProjectItems(CComPtr<EnvDTE::ProjectItems>& pItems, VsProjectList& projectsOut);

static void AddSolutionProject(CComPtr<EnvDTE::Project>& pProject, VsProjectList& projectsOut)
{
	CStringW projFilePath(GetProjectFullFilename(pProject));
	if (projFilePath.IsEmpty())
	{
		// [case: 80236]
		// iterate over project items looking for sub-projects to descend into
		try
		{
			CComPtr<EnvDTE::ProjectItems> pItems;
			pProject->get_ProjectItems(&pItems);
			if (pItems)
				GetProjectItems(pItems, projectsOut);
		}
		catch (const WtException& e)
		{
			WTString msg;
			msg.WTFormat("AddSolutionProject exception caught (%d) : ", StopIt);
			msg += e.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
		}

		return;
	}

	if (::IsAppropriateProject(pProject))
		projectsOut.push_back(pProject);
}

static void GetProjectItems(CComPtr<EnvDTE::ProjectItems>& pItems, VsProjectList& projectsOut)
{
	CComPtr<IUnknown> pUnk;
	HRESULT hres = pItems->_NewEnum(&pUnk);
	if (SUCCEEDED(hres) && pUnk != NULL)
	{
		CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
		if (pNewEnum)
		{
			VARIANT varItem;
			VariantInit(&varItem);
			CComQIPtr<EnvDTE::ProjectItem> pItem;
			ULONG enumVarCnt = 0;
			while (pNewEnum->Next(1, &varItem, &enumVarCnt) == S_OK)
			{
				_ASSERTE(varItem.vt == VT_DISPATCH);
				pItem = varItem.pdispVal;
				VariantClear(&varItem);
				if (!pItem)
					continue;

				CComPtr<EnvDTE::Project> psProject;
				pItem->get_SubProject(&psProject);
				if (psProject)
					AddSolutionProject(psProject, projectsOut);

				CComPtr<EnvDTE::ProjectItems> ppItems;
				pItem->get_ProjectItems(&ppItems);
				if (ppItems)
					GetProjectItems(ppItems, projectsOut);
			}
		}
	}
}

int GetSolutionRootProjectList(CComPtr<EnvDTE::_DTE> dte, VsProjectList& projectsOut)
{
	long rootLevelProjectCount = 0;
	if (dte)
	{
		CComPtr<EnvDTE::_Solution> pSolution;
		dte->get_Solution(&pSolution);
		if (pSolution)
		{
			CComPtr<EnvDTE::Projects> pProjects;
			pSolution->get_Projects(&pProjects);
			if (pProjects)
			{
				// iterate over solution projects
				pProjects->get_Count(&rootLevelProjectCount);
				long projCnt = 0;
#if 1
				for (long projIdx = 1; projIdx <= rootLevelProjectCount; ++projIdx)
				{
					CComPtr<EnvDTE::Project> pProject;
					pProjects->Item(CComVariant(projIdx), &pProject);
					if (!pProject)
						continue;

					++projCnt;
					projectsOut.push_back(pProject);
				}
#else
				CComPtr<IUnknown> pUnk;
				if (SUCCEEDED(pProjects->_NewEnum(&pUnk)) && pUnk != NULL)
				{
					CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
					if (pNewEnum)
					{
						VARIANT varItem;
						VariantInit(&varItem);

						while (pNewEnum->Next(1, &varItem, NULL) == S_OK)
						{
							_ASSERTE(varItem.vt == VT_DISPATCH || varItem.vt == VT_UNKNOWN);
							CComQIPtr<EnvDTE::Project> pProject{varItem.pdispVal};
							VariantClear(&varItem);
							if (!pProject)
								continue;

							++projCnt;
							projectsOut.push_back(pProject);
						}
					}
				}
#endif

				_ASSERTE(projCnt == rootLevelProjectCount);
				vLog("RootSolutionInfo normal %ld\n", projCnt);
			}
		}
	}

	VsSolutionPtr pIVsSolution(::GetVsSolution());
	_ASSERTE(pIVsSolution);
	if (pIVsSolution)
	{
		// [case: 84121]
		CComPtr<IEnumHierarchies> pEnumHierarchies;
		if (SUCCEEDED(pIVsSolution->GetProjectEnum(EPF_ALLVIRTUAL, GUID_NULL, &pEnumHierarchies)))
		{
			int virtProjectCnt = 0;
			ULONG cnt = 0;
			IVsHierarchy* pVsHierarchy = nullptr;
			while (SUCCEEDED(pEnumHierarchies->Next(1, &pVsHierarchy, &cnt)) && cnt)
			{
				for (ULONG idx = 0; idx < cnt; ++idx, ++pVsHierarchy)
				{
					CComVariant extObj;
					pVsHierarchy->GetProperty(VSITEMID_ROOT, VSHPROPID_ExtObject, &extObj);
					if (extObj.vt == VT_DISPATCH || extObj.vt == VT_UNKNOWN)
					{
						CComQIPtr<EnvDTE::Project> proj{extObj.pdispVal};
						if (proj)
						{
							if (projectsOut.end() == std::find(projectsOut.begin(), projectsOut.end(), proj))
							{
								++virtProjectCnt;
								projectsOut.push_back(proj);
							}
						}
					}

					pVsHierarchy->Release();
				}

				cnt = 0;
				pVsHierarchy = nullptr;
			}

			if (virtProjectCnt)
				vLog("RootSolutionInfo virtProj %d\n", virtProjectCnt);
		}
	}

	return rootLevelProjectCount;
}

int GetSolutionDeferredProjectCount()
{
	if (!gShellAttr || !gShellAttr->IsDevenv15OrHigher())
		return 0;

	int deferredCnt = 0;
	VsSolutionPtr pIVsSolution(::GetVsSolution());
	_ASSERTE(pIVsSolution);
	if (!pIVsSolution)
		return 0;

	CComQIPtr<IVsSolution7> sln7{pIVsSolution};
	if (!sln7)
		return 0;

	VARIANT_BOOL isDeferred;
	HRESULT res = sln7->IsSolutionLoadDeferred(&isDeferred);
	if (SUCCEEDED(res) && VARIANT_TRUE == isDeferred)
	{
		CComVariant defCnt = 0;
		res = pIVsSolution->GetProperty(VSPROPID_DeferredProjectCount, &defCnt);
		if (SUCCEEDED(res) && defCnt.intVal > 0)
			deferredCnt = defCnt.intVal;

		vCatLog("Environment.Solution", "Solution deferredProjCnt %d\n", deferredCnt);
	}
	else
		vCatLog("Environment.Solution", "Solution not deferred\n");

	return deferredCnt;
}

bool IsInOpenFolderMode()
{
	if (!gShellAttr || !gShellAttr->IsDevenv15OrHigher())
		return false;

	VsSolutionPtr pIVsSolution(::GetVsSolution());
	_ASSERTE(pIVsSolution);
	if (!pIVsSolution)
		return false;

	CComVariant isOpenFolderMode = 0;
	HRESULT res = pIVsSolution->GetProperty(VSPROPID_IsInOpenFolderMode, &isOpenFolderMode);
	if (!SUCCEEDED(res))
		return false;

	if (VARIANT_TRUE == isOpenFolderMode.boolVal)
	{
		vLog("Solution openFolderMode\n");
		return true;
	}

	return false;
}

CStringW GetSolutionFileOrDir()
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	if (gShellAttr && gShellAttr->IsDevenv16u10OrHigher())
	{
		VsSolutionPtr pIVsSolution(::GetVsSolution());
		_ASSERTE(pIVsSolution);
		if (pIVsSolution)
		{
			CComVariant isOpenFolderMode = 0;
			HRESULT res = pIVsSolution->GetProperty(VSPROPID_IsInOpenFolderMode, &isOpenFolderMode);
			if (SUCCEEDED(res))
			{
				if (VARIANT_TRUE == isOpenFolderMode.boolVal)
				{
					// [case: 146429] 
					// CMake projects in VS2019+ (16.10+) are broken if we use either 
					// DTE::_Solution::get_FileName or DTE::_Solution::get_FullName
					CComVariant stringProp = 0;
					res = pIVsSolution->GetProperty(VSPROPID_SolutionDirectory, &stringProp);
					if (SUCCEEDED(res))
					{
						const CStringW dirName(stringProp.bstrVal);
						if (!dirName.IsEmpty())
							return dirName;
					}
				}
			}
		}
	}

	CComPtr<EnvDTE::_Solution> pSolution;
	gDte->get_Solution(&pSolution);
	if (pSolution)
	{
		CComBSTR solutionFileBstr;
#ifdef AVR_STUDIO
		pSolution->get_FullName(&solutionFileBstr);
#else
		pSolution->get_FileName(&solutionFileBstr);
#endif
		return CStringW(solutionFileBstr);
	}

	return L"";
}

#if 0 
// LSL removed in vs2017 15.5
void
EnsureProjectIsLoaded(IVsSolution* pSolution, IVsHierarchy *projectToLoad)
{
	_ASSERTE(pSolution && projectToLoad);
	CComQIPtr<IVsSolution4> sln4{pSolution};
	if (!sln4)
		return;

	GUID projGuid;
	HRESULT res = projectToLoad->GetGuidProperty(VSITEMID_ROOT, __VSHPROPID::VSHPROPID_ProjectIDGuid, &projGuid);
	if (!(SUCCEEDED(res)))
	{
		vLog("ERROR: GetGuidProperty failed %lx", res);
		return;
	}

	res = sln4->EnsureProjectIsLoaded(projGuid, __VSBSLFLAGS::VSBSLFLAGS_None);
	if (!(SUCCEEDED(res)))
		vLog("ERROR: EnsureProjectIsLoaded failed %lx", res);
}

void
ForceLoadIfSourceInDeferredProject(const CStringW& src)
{
	if (!gShellAttr || !gShellAttr->IsDevenv15OrHigher() || !GlobalProject)
		return;

	CComPtr<IVsSolution> pIVsSolution(::GetVsSolution());
	_ASSERTE(pIVsSolution);
	if (!pIVsSolution)
		return;

	CComQIPtr<IVsSolution7> sln7{pIVsSolution};
	if (!sln7)
		return;

	VARIANT_BOOL isDeferred;
	HRESULT res = sln7->IsSolutionLoadDeferred(&isDeferred);
	if (!(SUCCEEDED(res) && VARIANT_TRUE == isDeferred))
		return;

	CComVariant defCnt = 0;
	res = pIVsSolution->GetProperty(VSPROPID_DeferredProjectCount, &defCnt);
	if (!(SUCCEEDED(res) && defCnt.intVal > 0))
		return;

	ProjectVec vaPrj = GlobalProject->GetProjectForFile(src);
	if (vaPrj.empty())
	{
		vLog("warn: failed to locate project for file (%s) for force load", (LPCTSTR)CString(src));
		return;
	}

	bool anyDeferred = false;
	for (auto p : vaPrj)
	{
		if (p->IsDeferredProject())
		{
			anyDeferred = true;
			break;
		}
	}

	if (!anyDeferred)
	{
		vLog("project for file not deferred (%s)", (LPCTSTR)CString(src));
		return;
	}

	int forceCnt = 0;
	CComPtr<IEnumHierarchies> pEnumHierarchies;
	if (SUCCEEDED(pIVsSolution->GetProjectEnum(EPF_DEFERRED, GUID_NULL, &pEnumHierarchies)))
	{
		ULONG cnt = 0;
		IVsHierarchy *pVsHierarchy = nullptr;
		while (SUCCEEDED(pEnumHierarchies->Next(1, &pVsHierarchy, &cnt)) && cnt)
		{
			for (ULONG idx = 0; idx < cnt; ++idx, ++pVsHierarchy)
			{
				CComVariant extObj;
				pVsHierarchy->GetProperty(VSITEMID_ROOT, __VSHPROPID::VSHPROPID_ProjectName, &extObj);
				if (extObj.vt != VT_BSTR)
					continue;

				const CStringW ivsProjName(extObj.bstrVal);
				if (ivsProjName.IsEmpty())
					continue;

				for (auto vp : vaPrj)
				{
					const CStringW curProjName(vp->GetVsProjectName());
					if (!curProjName.IsEmpty() && !curProjName.CompareNoCase(ivsProjName))
					{
						vLog("force deferred load: match name %s", (LPCTSTR)CString(ivsProjName));
						++forceCnt;
						EnsureProjectIsLoaded(pIVsSolution, pVsHierarchy);
					}
					else
					{
						const CStringW projFile(vp->GetProjectFile());
						// not an error, but log it
						if (!projFile.IsEmpty())
						{
							vLog("force deferred load: fail name match (%s) (%s)", (LPCTSTR)CString(ivsProjName), (LPCTSTR)CString(Basename(projFile)));
						}
						else
						{
							vLog("force deferred load: fail name match (%s)", (LPCTSTR)CString(ivsProjName));
						}
					}
				}

				pVsHierarchy->Release();
			}

			cnt = 0;
			pVsHierarchy = nullptr;
		}
	}

	if (forceCnt != (int)vaPrj.size())
	{
		vLog("ERROR: force deferred load: %d of %d", forceCnt, vaPrj.size());
	}
	else
	{
		vLog("force deferred load: %d of %d", forceCnt, vaPrj.size());
	}
}
#endif

void BuildSolutionProjectList(VsProjectList& projectsOut)
{
	if (!gShellAttr->IsDevenv())
	{
		_ASSERTE(!"only devenv");
		return;
	}

	VsProjectList rootProjects;
	GetSolutionRootProjectList(gDte, rootProjects);
	for (auto p : rootProjects)
		AddSolutionProject(p, projectsOut);
}

void GetPackagesIncludeDirs(CStringW searchDir, FileList& dirList)
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW searchSpec;
	CStringW curDir;
	CStringW lastAddedDir;
	std::vector<CStringW> dirsToSearch;

	const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
	if (kLastChar == L'\\' || kLastChar == L'/')
		searchDir = searchDir.Left(searchDir.GetLength() - 1);

	searchDir.Append(L"\\");
	searchSpec = searchDir + L"*.*";
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	vCatLog("Environment.Directories", "SearchPackageDir:  %s\n", (LPCTSTR)CString(searchDir));

	do
	{
		const CStringW curFile(fileData.cFileName);
		if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
			continue;

		if (FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes)
		{
			if (curFile == L"." || curFile == L"..")
				continue;

			// do not descend into bin/docs/*.redist.*
			if (curFile == L"bin" || curFile == "docs" || -1 != curFile.Find(L".redist"))
				continue;

			curDir = searchDir + curFile;

			if (!curFile.CompareNoCase(L"include"))
			{
				vLog("AddNuGetRepositoryDir:  %s\n", (LPCTSTR)CString(curDir));
				dirList.AddUniqueNoCase(curDir);
				lastAddedDir = curDir;
				// do not descend any further
			}
			else
			{
				// breadth-first search due to file search hack below in the next else
				dirsToSearch.push_back(curDir);
			}
		}
		else
		{
			// [case: 93820] hack workaround until proper parsing of .targets file is implemented in [case: 93818]
			if (lastAddedDir == searchDir)
				continue;

			if (dirList.size() && 0 == searchDir.Find(dirList.back().mFilename))
				continue;

			int fType = GetFileTypeByExtension(curFile);
			if (Header == fType)
			{
				lastAddedDir = searchDir;
				CStringW tmp(searchDir);
				tmp = tmp.Left(tmp.GetLength() - 1);
				vLog("AddNuGetRepositoryDir:  %s\n", (LPCTSTR)CString(tmp));
				dirList.AddUniqueNoCase(tmp);
			}
		}
	} while (FindNextFileW(hFile, &fileData));

	FindClose(hFile);

	for (auto& dir : dirsToSearch)
		GetPackagesIncludeDirs(dir, dirList);
}

bool ProcessNugetConfigFile(const CStringW nugetCfgFile, CStringW& pkgPath, const CStringW& slnFile)
{
	if (IsFile(nugetCfgFile))
	{
		// reading config file
		bool pkgPathFound = false;
		vLog("NuGet: config file located at %s\n", (LPCTSTR)CString(nugetCfgFile));
		WTString fileBuf;
		fileBuf.ReadFile(nugetCfgFile);
		if (fileBuf.IsEmpty())
		{
			vLog("NuGet: empty config file\n");
		}
		else
		{
			// searching config method 1
			int repPath = fileBuf.FindNoCase("<repositoryPath");
			size_t tokenLen = strlen("<repositoryPath");
			if (repPath != -1)
			{
				int repPath2 = fileBuf.FindNoCase("</repositoryPath", repPath);
				if (repPath2 != -1)
				{
					WTString path = fileBuf.Mid(repPath + tokenLen, repPath2 - (repPath + tokenLen));

					int closePos = path.Find(">");
					if (closePos != -1)
						path = path.Right(path.GetLength() - 1 - closePos);
					path.TrimLeft();
					path.TrimRight();

					pkgPath = path.Wide();
					pkgPathFound = true;
				}
			}
			else
			{
				// searching config method 2 - using regex to skip possible whitespaces
				std::string fileStdBuf =
				    fileBuf.c_str(); // the config file shouldn't be too long, you don't even need to have one
				std::regex rx1("\\<add\\s+key\\s*=\\s*\"repositoryPath\"\\s+value=\\s*\"",
				               std::regex_constants::ECMAScript | std::regex_constants::icase);
				std::smatch sm;
				std::regex_search(fileStdBuf, sm, rx1);
				if (!sm.empty())
				{
					std::string s = sm.str();
					fileStdBuf = sm.suffix().str();
					std::regex rx2(".+\"");
					std::regex_search(fileStdBuf, sm, rx2);
					if (!sm.empty())
					{
						std::string s2 = sm.str();
						pkgPath = s2.c_str();
						pkgPath = pkgPath.Left(pkgPath.GetLength() - 1);
						pkgPathFound = true;
					}
				}
			}

			if (pkgPathFound)
			{
				// [case: 105958]
				pkgPath = WTExpandEnvironmentStrings2(pkgPath);
			}

			// the global config file is specified in global path so it's safe to use the solution path to make the path
			// global
			if (!IsPathAbsolute(pkgPath))
				pkgPath = Path(slnFile) + L"\\" + pkgPath;

			if (!IsDir(pkgPath))
			{
				vLog("ERROR: invalid NuGet pkgPath %s\n", (LPCTSTR)CString(pkgPath));
				return false;
			}
		}

		return pkgPathFound;
	}

	return false;
}

void GetRepositoryIncludeDirs(const CStringW& slnFile, FileList& dirList)
{
	// [case: 79296] [case: 82364]
	if (slnFile.IsEmpty())
		return;

	if (!IsFile(slnFile))
		return;

	// look for packages dir next to sln file unless we find a config file
	CStringW pkgPath(Path(slnFile) + L"\\packages"); // default directory
	const CStringW nugetCfgFile(Path(slnFile) + L"\\NuGet.config");
	if (!ProcessNugetConfigFile(nugetCfgFile, pkgPath, slnFile))
	{
		// [case: 105959]
		// look for IDE-specific global config file for global packages dir
		if (gShellAttr->IsDevenv11OrHigher())
		{
			CStringW configPath;

			if (gShellAttr->IsDevenv15OrHigher())
				configPath = "%ProgramFiles(x86)%\\NuGet\\Config\\";
			else
				configPath = L"%ProgramData%\\NuGet\\Config\\";

			configPath = WTExpandEnvironmentStrings2(configPath);
			const CStringW nugetCfgFile2(Path(configPath) + L"\\NuGet.config");
			ProcessNugetConfigFile(nugetCfgFile2, pkgPath, slnFile);
		}
	}

	if (!IsDir(pkgPath))
		return;

	vLog("NuGet: pkgPath %s\n", (LPCTSTR)CString(pkgPath));
	// recursive look for sub-dirs named include
	GetPackagesIncludeDirs(pkgPath, dirList);
}

bool IsProjectCacheable(const CStringW& prjFile)
{
	CStringW ext(::GetBaseNameExt(prjFile));
	ext.MakeLower();
	if (ext != L"csproj" && ext != L"vbproj")
		return true;

	// [case: 98052] [case: 141794] #netCoreSupport
	WTString projFileContents;
	projFileContents.ReadFile(prjFile, 1024);
	if (-1 != projFileContents.FindNoCase("<Project Sdk"))
		return false; // .net core project

	if (::GetFSize(prjFile) < (10 * 1024))
	{
		projFileContents.ReadFile(prjFile);
		if (-1 == projFileContents.FindNoCase("<Compile Include"))
			return false; // project without compile tag
	}

	return true;
}

void LogBySpliting(const CStringW& msg)
{
	// [case: 146245]
	int splitSize = 2900; // each log line has to be split under 3000 bytes because that is a limit set in MyLog
	if (msg.GetLength() <= splitSize)
	{
		MyLog(CString(msg).GetString());
	}
	else
	{
		int numSplits = msg.GetLength() / splitSize;
		for (auto i = 0; i < numSplits; i++)
		{
			MyLog(CString(msg.Mid(i * splitSize, splitSize)).GetString());
		}

		// last split if exists
		if (msg.GetLength() % splitSize != 0)
		{
			MyLog(CString(msg.Mid(splitSize * numSplits)).GetString());
		}
	}
}
