#pragma once

#include "FileList.h"
#include <map>

typedef std::vector<CComPtr<EnvDTE::Project>> VsProjectList;

class VsSolutionInfo
{
  public:
	struct ProjectSettings
	{
		ProjectSettings()
		    : mCppHasManagedConfig(false), mCppIsWinRT(false), mCppIgnoreStandardIncludes(false), mIsVcProject(false),
		      mPseudoProject(false)
		{
			mProjectFileTimestamp.dwHighDateTime = mProjectFileTimestamp.dwLowDateTime = 0;
		}

		ProjectSettings(const ProjectSettings* rhs);

		// update LoadProjectCache and SaveProjectCache if modifying the struct members
		CStringW mProjectFile; // only needed for reading cache files outside of va
		CStringW mProjectFileDir;
		FILETIME mProjectFileTimestamp; // required for cache management
		CStringW mVsProjectName;        // not serialized, only used for deferred projects
		CStringW mPrecompiledHeaderFile; // serialized

		FileList mFiles;
#ifdef AVR_STUDIO
		FileList mDeviceFiles;
#endif
		FileList mReferences;
		FileList mPropertySheets;
		FileList mSubProjects; // required for cache management

		FileList mConfigFullIncludeDirs;
		FileList mConfigIntermediateDirs;
		FileList mConfigForcedIncludes;
		FileList mConfigUsingDirs;

		FileList mPlatformIncludeDirs;
		FileList mPlatformLibraryDirs;
		FileList mPlatformSourceDirs;
		FileList mPlatformExternalIncludeDirs;

		bool mCppHasManagedConfig;
		bool mCppIsWinRT;
		bool mCppIgnoreStandardIncludes;
		bool mIsVcProject; // required for cache management
		bool mVcFplEnabledWhenCacheCreated = false;
		bool mVcMissingConfigs = false;
		bool mPseudoProject;
		bool mDeferredProject = false;

		void LoadProjectCache(const CStringW& projectPath);
		void SaveProjectCache();
	};
	typedef std::shared_ptr<ProjectSettings> ProjectSettingsPtr;
	typedef std::map<const CStringW, ProjectSettingsPtr> ProjectSettingsMap;

  public:
	virtual ~VsSolutionInfo()
	{
		ClearData();
	}
	virtual void GetSolutionInfo()
	{
	}
	virtual void GetMinimalProjectInfo(const CStringW& proj, int rootProjectCount, const VsProjectList& rootProjects)
	{
	}
	virtual void FinishGetSolutionInfo(bool runningAsync = true)
	{
	}
	virtual bool IsProjectCacheValid(const CStringW& proj)
	{
		return false;
	}

	// This is basically a temporary holder populated on the UI thread.
	// the GlobalProject (solution) will take the file lists and leave
	// them empty (from the ProjectLoader thread).
	CCriticalSection& GetLock()
	{
		return mLock;
	}
	FileList& GetReferences()
	{
		return mReferences;
	}
	ProjectSettingsMap& GetInfoMap()
	{
		return mProjectMap;
	}
	CStringW GetProjectListW() const
	{
		return mProjects;
	}
	CStringW GetSolutionFile() const
	{
		return mSolutionFile;
	}
	FileList& GetSolutionPackageIncludeDirs()
	{
		return mSolutionPackageIncludes;
	}
	void SetSolutionHash(unsigned int slnHash)
	{
		mSolutionHash = slnHash;
	}
	unsigned int GetSolutionHash() const
	{
		return mSolutionHash;
	}

  protected:
	CStringW mSolutionFile;

	// IDE independent state
	CCriticalSection mLock;
	ProjectSettingsMap mProjectMap;
	CStringW mProjects;
	FileList mReferences;
	FileList mSolutionPackageIncludes;
	unsigned int mSolutionHash = 0;

  protected:
	void ClearData();
};

class AsyncSolutionQueryInfo
{
  public:
	AsyncSolutionQueryInfo() = default;
	bool IsSolutionInfoCacheValid(const CStringW& solutionFile, int deferredProjectCnt) const;
	bool RequestAsyncWorkspaceCollection(bool forceLoad);
	bool RequestAsyncWorkspaceCollection(const CStringW& solutionFile, int deferredProjectCnt, bool forceLoad);
	void AsyncWorkspaceCollectionComplete(int reqId, const WCHAR* solutionFilepath, const WCHAR* workspaceFiles);
	void VsWorkspaceIndexerFinished();
	void VsWorkspaceIndexerFinishedBeforeLoad();
	void Reset();
	int GetCurrentRequestId() const
	{
		return mRequestId;
	}
	bool IsWorkspaceIndexerActive() const;
	CStringW GetRequestedSolutionFile() const
	{
		return mRequestedSolutionFile;
	}
	VsSolutionInfo::ProjectSettingsMap GetSolutionInfo() const;

  private:
	CStringW mRequestedSolutionFile;
	int mDeferredProjectCount = 0;
	int mRequestId = 0;
	int mProjectsWithoutFiles = 0; // indicator that vs workspace can't be trusted
	bool mForceLoadOnComplete = true;
	bool mWorkspaceIndexerIsFinished = false;
	bool mLoadedSinceIndexerFinished = false;
	int mIntellisenseInfoFoundCount = 0;
	DWORD mIntellisenseLoadRetryDuration = 0;
	VsSolutionInfo::ProjectSettingsMap mSolutionInfo;

	static CCriticalSection sInfoLock;
	static int sRequestIdCounter;
};

extern AsyncSolutionQueryInfo gSqi;

VsSolutionInfo* CreateVsSolutionInfo();
bool CheckForNewSolution(EnvDTE::_DTE* pDTE, CStringW previousSolFile, CStringW previousProjectPaths);
CStringW VcEngineEvaluate(LPCWSTR expressionIn);
void SetupVcEnvironment(bool force = true);
void BuildSolutionProjectList(VsProjectList& projectsOut);
void InvalidateProjectCache(const CStringW& projfile);
void GetRepositoryIncludeDirs(const CStringW& slnFile, FileList& dirList);
int GetSolutionDeferredProjectCount();
bool IsInOpenFolderMode();
CStringW GetSolutionFileOrDir();
bool IsProjectCacheable(const CStringW& prjFile);
void LogBySpliting(const CStringW& msg);

#define LOGBYSPLITTING(cat, msg)                 \
	if (g_loggingEnabled)                        \
	{                                            \
		int id = GetIdByString(cat);      \
		if (gLoggingCategories.test((size_t)id)) \
		{                                        \
			MyLog(cat);                          \
			LogBySpliting(msg);                  \
		}                                        \
	}

namespace ProjectReparse
{
// This is thread-safe.
void QueueProjectForReparse(const CStringW& project);

// This must be called from the UI thread.
void ProcessQueuedProjects();

// This is thread-safe
void ClearProjectReparseQueue();
} // namespace ProjectReparse
