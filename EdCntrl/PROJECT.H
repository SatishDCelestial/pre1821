#ifndef Projectinc
#define Projectinc

#include "wtstring.h"
#include "log.h"
#include <map>
#include "FileList.h"
#include "Lock.h"
#include "DBLock.h"
#include "parse.h"
#include <memory>
#include <list>
#include "EdCnt_fwd.h"

struct ProjectInfo;
typedef std::shared_ptr<ProjectInfo> ProjectInfoPtr;
typedef std::list<ProjectInfoPtr> ProjectVec;
class VsSolutionInfo;
struct ModifiedFilesInfo;

// The class appears to manage the solution rather than projects, so a more appropriate class name might be "Solution".
// It has the ProjectMap but doesn't have a single project filename, only a solution file name.
class Project
{
	friend class ProjectLoader;

  public:
	// map lower-case project file name to ProjectInfoPtr
	typedef std::map<CStringW, ProjectInfoPtr> ProjectMap;
	// map file ids to projects that contain the file
	typedef std::map<UINT, ProjectVec> MapFilesToProjects;
	// [case: 114560] - consider switching to map if more than \Engine\Plugins\* needs to be ignored; currently no need
	// for that
	typedef std::map<CStringW, std::vector<CStringW>> UEIgnoreDirs;

	Project()
	    : mIsLoading(-1), mWrapperCheckOk(true), m_loadingProjectFiles(false), mCppUsesClr(false), mCppUsesWinRT(false),
	      mFileListNeedsUpdate(1), mFileListNeedsSort(true), mFileListGeneration(0), mAddlDirListNeedsUpdate(true),
	      mMapLock(TRUE), mSolutionIsInitiallyEmpty(true), mCleanLoad(true), mProjectHash(0)
	{
		Log2("Project ctor");
		// while VS is opening, we get an openfile before the open project
	}
	~Project();

	bool LaunchProjectLoaderThread(CStringW projects);
	void LoadProject(std::shared_ptr<VsSolutionInfo> sfc);

	void CloseProject();
	bool CheckForNewSolution() const;
	void SolutionLoadStarting();
	void SolutionLoadCompleted(bool forceLoad, bool forceSync = false);
	void SolutionClosing();

	bool IsBusy() const
	{
		return mIsLoading != 0;
	}
	void ClearIfNoInit()
	{
		if (-1 == mIsLoading)
			mIsLoading = 0;
	}
	bool IsOkToIterateFiles() const;
	bool BuildFileCollectionIfReady();
	bool SortFileListIfReady();
	int GetFileItemCount();
	bool Contains(const CStringW& f);
	bool ContainsNonExternal(const CStringW& filename) const;
	void FindBaseFileNameMatches(const CStringW& basenameNoExt, FileList& matches, bool matchLengths);
	void FindBaseFileNameMatches(const CStringW& filepathToIsolateToProject, const CStringW& basenameNoExt,
	                             FileList& matches);
	bool IsArmWrapperOk() const
	{
		return mWrapperCheckOk;
	}
	void WrapperMissing();
	bool CppUsesClr() const
	{
		return mCppUsesClr;
	}
	bool CppUsesWinRT() const
	{
		return mCppUsesWinRT;
	}
	bool IsVcFastProjectLoadEnabled() const
	{
		return mVcFastProjectLoadEnabled;
	}
	bool ShouldLoadNetDb() const
	{
		return mRequiresNetDb;
	}
	bool IsSolutionInitiallyEmpty()
	{
#ifdef _DEBUG
		{
			RWLockReader lck(mFilelistLock);
		}
		_ASSERTE(mSolutionIsInitiallyEmpty || (!mSolutionIsInitiallyEmpty && GetFileItemCount()));
#endif // _DEBUG
		return mSolutionIsInitiallyEmpty;
	}
	void SetContainsUnrealEngineProject(bool containsUnrealEngineProject)
	{
		mContainsUnrealEngineProject = containsUnrealEngineProject;
	}
	bool GetContainsUnrealEngineProject() const
	{
		return mContainsUnrealEngineProject;
	}
	void SetContainsUnityEngineProject(bool containsUnityEngineProject)
	{
		mContainsUnityEngineProject = containsUnityEngineProject;
	}
	bool GetContainsUnityEngineProject() const
	{
		return mContainsUnityEngineProject;
	}
	std::map<WTString, WTString> ParseCustomGlobalSection(const WTString sectionName);

	CStringW GetProjAdditionalDirs();

	// these modify state after initial project open
	void AddFile(const CStringW& project, const CStringW& file, BOOL nonbinarySourceFile = true);
	void RemoveFile(const CStringW& project, const CStringW& file, BOOL nonbinarySourceFile = true);
	void RemoveFileWithoutPath(const CStringW& projName, CStringW basename);
	void RenameFile(const CStringW& project, const CStringW& oldname, const CStringW& newname);
	void AddExternalFile(const CStringW& file);
	void CheckForChanges(const CStringW& prjFile);
	void CollectModificationInfo(ModifiedFilesInfo& info);
	void UpdateModifiedFiles(ModifiedFilesInfo& info);
	void CollectAndUpdateModifiedFiles();
	void SwapSolutionRepositoryIncludeDirs(FileList& incDirs);
	bool ParsePrivateSystemHeaders();
	void AdditionalDirsNeedsUpdate()
	{
		mAddlDirListNeedsUpdate = true;
	}
	void FilesListNeedsUpdate();
	LONG GetFilesListGeneration() const;

	CStringW GetProjDbDir() const
	{
		return mProjectDbDir;
	}
	uint GetSolutionHash() const
	{
		return mProjectHash;
	}
	void DumpToLog();

	const FileList& GetFilesSortedByName(RWLockReader& reader)
	{
		_ASSERTE(IsOkToIterateFiles());
		reader.SetLock(&mFilelistLock);
		return mProjectFileItemList;
	}
	const ProjectMap& GetProjectsForRead(RWLockReader& reader) const
	{
		reader.SetLock(&mMapLock);
		return mMap;
	}
	const FileList& GetUsingDirs()
	{
		return mUsingDirs;
	}
	const FileList& GetSolutionPrivateSystemIncludeDirs(RWLockReader& reader)
	{
		reader.SetLock(&mFilelistLock);
		return mSolutionPrivateSystemIncludeDirs;
	}
	const FileList& GetSolutionPrivateSystemHeaders(RWLockReader& reader)
	{
		reader.SetLock(&mFilelistLock);
		return mSolutionPrivateSystemHeaders;
	}

	CStringW SolutionFile() const
	{
		return mSolutionFile;
	}
	void SetSolutionFile(const CStringW& val);
	bool IsFolderBasedSolution() const
	{
		return mIsFolderBasedSln;
	}

	ProjectVec GetProjectForFile(const CStringW& file);
	ProjectVec GetProjectForFile(UINT fileId);

	bool IsFilePathInProject(const CStringW& filePath) const;
	CStringW GetPCHFileName() const;

	const UEIgnoreDirs& GetUEIgnorePluginsDirs() const
	{
		return mUEIgnorePluginsDirs;
	}
	const std::vector<CStringW>& GetReferencedUEPlugins() const
	{
		return mReferencedPlugins;
	}

	void SetFileExclusionRegexes(std::vector<std::wregex>&& fileExclusionRegexes)
	{
		mFileExclusionRegexes = std::move(fileExclusionRegexes);
	}

	const std::vector<std::wregex>& GetFileExclusionRegexes() const
	{
		return mFileExclusionRegexes;
	}

	// return version as unrefined string (whatever was read from registry)
	const CStringW& GetUnrealEngineVersion() const
	{
		return mUnrealEngineVersion;
	}

	// return version as pair of ints <major, minor>
	// <-1, -1> if version cant be converted to numbers
	const std::pair<int, int>& GetUnrealEngineVersionAsNumber()
	{
		// define as static to allow lazy evaluation
		static std::pair<int, int> retVal{-1, -1};
		static bool isParsed = false;

		// already parsed so just return what we have
		if (isParsed)
			return retVal;

		int major, minor;
		int conversionCount = swscanf_s(mUnrealEngineVersion.GetString(), L"%d.%d", &major, &minor);
		
		if (conversionCount == 2)
			retVal = {major, minor};
			
		isParsed = true;
		return retVal;
	}

	const std::map<CStringW, CStringW>& GetUnrealBinaryVersionPath() const
	{
		return mUnrealBinaryVersionPath;
	}

	void SetUnrealBinaryVersionPath(const CStringW& version, const CStringW& path)
	{
		mUnrealBinaryVersionPath[version] = path;
	}

	const std::list<CStringW>& GetUnrealIncludePathList() const
	{
		return mUnrealIncludePathList;
	}

	void SetUnrealIncludePathList(const std::list<CStringW>& list)
	{
		mUnrealIncludePathList = list;
	}

  private:
	// all files sorted by base name
	FileList mProjectFileItemList;
	FileList mReferencesList;
	FileList mUsingDirs;
	FileList mSolutionPrivateSystemIncludeDirs; // NuGet repository include dirs
	FileList mSolutionPrivateSystemHeaders;     // NuGet repository headers
	std::atomic_int mIsLoading;
	bool mWrapperCheckOk;
	std::atomic_bool m_loadingProjectFiles;
	bool mCppUsesClr;
	bool mCppUsesWinRT;
	std::atomic_long mFileListNeedsUpdate; // if it's considered as a BOOL variable, it works exactly as before; now it
	                                       // additionally counts a number of updates done
	bool mFileListNeedsSort;
	mutable CCriticalSection mFileListGeneration_cs;
	std::atomic_long mFileListGeneration; // a number of updates done at the point list was generated
	std::atomic_bool mAddlDirListNeedsUpdate;
	CStringW mProjectDbDir;
	CStringW mSolutionFile;
	mutable RWLock mMapLock;
	mutable RWLock mFilelistLock;
	bool mSolutionIsInitiallyEmpty;
	bool mCleanLoad;
	bool mRequiresNetDb = true;
	bool mIsFolderBasedSln = false;
	bool mVcFastProjectLoadEnabled = false;
	bool mContainsUnrealEngineProject = false; // true if the solution contains an unreal engine project
	bool mContainsUnityEngineProject = false;  // true if the solution contains a unity engine project
	uint mProjectHash;
	ProjectMap mMap; // solution projects
	MapFilesToProjects mMapFilesToProjects;
	UEIgnoreDirs mUEIgnorePluginsDirs;
	std::vector<CStringW> mReferencedPlugins;
	std::vector<std::wregex> mFileExclusionRegexes;
	CStringW mUnrealEngineVersion;
	std::map<CStringW, CStringW> mUnrealBinaryVersionPath;
	std::list<CStringW> mUnrealIncludePathList;

	void FindUEPlugins(const CStringW& path, std::list<CStringW>& existingPluginPaths);
	void PopulateUEReferencedPluginList(std::list<CStringW>& pluginList,
	                                    const std::map<CStringW, std::vector<CStringW>>& pluginsReferencedByPlugin,
	                                    const std::list<CStringW>& existingPluginPaths, const CStringW& solFilePath);
	void ParseUEUpluginFiles(const std::list<CStringW>& existingPluginPaths, std::list<CStringW>& pluginList,
	                         std::map<CStringW, std::vector<CStringW>>& pluginsReferencedByPlugin);
	void GetUEExistingPlugins(const std::vector<CStringW>& projectPathList, std::list<CStringW>& existingPluginPaths);
	void GetUEPluginList(const CStringW& projectFile, std::list<CStringW>& pluginList,
	                     std::vector<CStringW>& projectPathList);

	void ProcessIntermediateDirs();
	void ClearMap();
	void CheckImplicitReferences(bool asyncLoad);
	// temporaries - used to pass from main thread to loader thread
	CStringW mRawProjectList; // used in all IDEs; used to see if new solution loaded and to determine db dir (hash of
	                          // this string)
	std::shared_ptr<VsSolutionInfo> mSolutionEventListener;
};

void FreeTheGlobals(void);
void AllocTheGlobals(void);
BOOL ShouldParseGlobals(int ftype);
extern Project* GlobalProject;
extern HWND MainWndH;
extern CWnd* gMainWnd;
extern CWnd* gVaMainWnd;
extern volatile int gTypingDevLang;
extern volatile BOOL g_WTL_FLAG;
extern volatile BOOL gShellIsUnloading;
extern HANDLE gProcessLock;
extern CStringW kWinProgFilesDirX86;
extern CStringW kWinProgFilesDir;

extern CComQIPtr<EnvDTE::_DTE> gDte;
extern CComQIPtr<EnvDTE80::DTE2> gDte2;

bool ContainsPseudoProject(const ProjectVec& prjs);
bool DirectoryExists(const CStringW& dirName);

// get Unreal Engine 4 install dirs from the registry [case: 105950]
SemiColonDelimitedString GetUe4Dirs();

// get Unity Engine dirs which should be included but aren't supplied by the projects [case: 105950]
SemiColonDelimitedString GetUnityEngineExtraIncludes();

template <typename T> BOOL Intersects(T v1, T v2)
{
	for (typename T::const_iterator it1 = v1.begin(); it1 != v1.end(); ++it1)
	{
		for (typename T::const_iterator it2 = v2.begin(); it2 != v2.end(); ++it2)
		{
			if (*it1 == *it2)
				return TRUE;
		}
	}

	return FALSE;
}

void EnumerateVsProjectsForFile(const CStringW& fileName, std::function<bool(struct IVsHierarchy*)> onProject);
int GetProjectNamesForFile(const CStringW& fileName, std::vector<std::tuple<CStringW, int>>& projs);
bool OpenFileInSpecificProject(const CStringW& projectName, EdCnt* ed);
int GetProjectIconIdx(IVsHierarchy* project);
CStringW GetProjectName(IVsHierarchy* project);


#endif
