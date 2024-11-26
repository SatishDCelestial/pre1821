#pragma once

#include "wtstring.h"
#include <vector>
#include "FileList.h"
#include "SolutionFiles.h"
#include "Settings.h"
#include <algorithm>
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
#include "RadStudioPlugin.h"
#endif

struct ProjectInfo
{
  public:
	virtual ProjectInfo* CreateNewInfo() const = 0;
	virtual ProjectInfo* Duplicate() const = 0;
	virtual ~ProjectInfo()
	{
	}

	virtual bool CheckForChanges() = 0;
	bool Parse();

	void AddFile(const CStringW& file);
	void TakeFiles(FileList& files);
	void TakeIncludeDirs(FileList& dirs);
	void RemoveFile(const CStringW& file);
	bool ContainsFile(const CStringW& file) const;
	bool ContainsFile(UINT fileID) const;
#ifdef AVR_STUDIO
	void UpdateDeviceFiles(FileList& files);
#endif // AVR_STUDIO

	const CStringW& GetProjectFile() const
	{
		return mProjectFile;
	}
	CStringW GetVsProjectName() const
	{
		return mVsProjectName;
	}
	CStringW GetPrecompiledHeaderFile() const
	{
		return mPrecompiledHeaderFile;
	}
	const FileList& GetProjectDirs() const
	{
		return mProjectDirs;
	}
	const FileList& GetIncludeDirs() const
	{
		return mIncludeDirs;
	}
	const FileList& GetUsingDirs() const
	{
		return mUsingDirs;
	}
	const FileList& GetIntermediateDirs() const
	{
		return mIntermediateDirs;
	}
	const FileList& GetFiles() const
	{
		return mFiles;
	}
	const FileList& GetPropertySheets() const
	{
		return mPropertySheets;
	}

	const FileList& GetPlatformIncludeDirs() const
	{
		return mPlatformIncludeDirs;
	}
	const FileList& GetPlatformLibraryDirs() const
	{
		return mPlatformLibraryDirs;
	}
	const FileList& GetPlatformSourceDirs() const
	{
		return mPlatformSourceDirs;
	}
	const FileList& GetPlatformExternalIncludeDirs() const
	{
		return mPlatformExternalIncludeDirs;
	}

	bool CppUsesClr() const
	{
		return mCppUsesClr;
	}
	bool CppUsesWinRT() const
	{
		return mCppUsesWinRT;
	}
	bool IsPseudoProject() const
	{
		return mPseudoProject;
	}
	bool IsDeferredProject() const
	{
		return mDeferredProject;
	}
	bool IsSharedProjectType() const;
	bool HasSharedProjectDependency() const;

	void MinimalUpdateFrom(VsSolutionInfo::ProjectSettings& newSettings);

	// caller must ensure thread-safety
	template <typename _Function> void ParallelForEachFile(const _Function& _Func)
	{
		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(mFiles.begin(), mFiles.end(), _Func);
		else
			std::for_each(mFiles.begin(), mFiles.end(), _Func);
	}

	uint GetMemSize() const;

  protected:
	ProjectInfo(const CStringW& projFile)
	    : mProjectFile(projFile), mCppUsesClr(false), mCppUsesWinRT(false), mCppIgnoreStandardIncludes(false),
	      mPseudoProject(false)
	{
		mProjectFileModTime.dwHighDateTime = mProjectFileModTime.dwLowDateTime = 0;
	}
	virtual void DoParse(const CStringW& projFilePath, const WTString& projFileTxt) = 0;
	void Copy(const ProjectInfo& rhs);
	bool CheckForChanges(bool compareFileLists);
	void ClearData();

  private:
	ProjectInfo();
	ProjectInfo(const ProjectInfo& rhs);
	ProjectInfo& operator=(const ProjectInfo& rhs);

  protected:
	CStringW mProjectFile;        // not case-adjusted
	CStringW mVsProjectName;      // only populated for some deferred projects
	CStringW mPrecompiledHeaderFile;
	FILETIME mProjectFileModTime; // FileTime of mProjectFile
	FileList mProjectDirs;        // dirs where source and headers are located
	FileList mIncludeDirs;        // include and additional include dirs
	FileList mUsingDirs;          // additional using dirs for references
	FileList mIntermediateDirs;
	FileList mFiles;
#ifdef AVR_STUDIO
	FileList mDeviceFiles;
#endif
	FileList mReferences;
	FileList mPropertySheets;
	std::vector<FILETIME> mPropertySheetModTimes;
	bool mCppUsesClr;
	bool mCppUsesWinRT;
	bool mCppIgnoreStandardIncludes;
	bool mPseudoProject;
	bool mDeferredProject = false;
	CStringW mLastDirAdded;

	FileList mPlatformIncludeDirs;
	FileList mPlatformLibraryDirs;
	FileList mPlatformSourceDirs;
	FileList mPlatformExternalIncludeDirs;
};

class DspProject : public ProjectInfo
{
  public:
	static DspProject* CreateDspProject(const CStringW& dspFile);
	virtual ProjectInfo* CreateNewInfo() const;
	virtual ProjectInfo* Duplicate() const;

	virtual bool CheckForChanges()
	{
		return ProjectInfo::CheckForChanges(true);
	}

  protected:
	DspProject(const CStringW& projFile) : ProjectInfo(projFile)
	{
	}
	virtual void DoParse(const CStringW& projFilePath, const WTString& projFileTxt);

  private:
	void BuildFileList(const CStringW& projectFilePath, const WTString& projectFileText);
	void ParseIncludeDirs(const CStringW& projectFilePath);
	void ParseIntermediateDirs(const CStringW& projectFilePath, const WTString& projectFileText);
};

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
class RadStudioProject : public ProjectInfo
{
  public:
	static RadStudioProject* CreateRadStudioProject(VaRadStudioPlugin::RsProjectDataPtr projData);
	virtual ProjectInfo* CreateNewInfo() const;
	virtual ProjectInfo* Duplicate() const;

	virtual bool CheckForChanges()
	{
		return ProjectInfo::CheckForChanges(true);
	}


  protected:
	RadStudioProject(const CStringW& projFile)
	    : ProjectInfo(projFile)
	{
	}
	virtual void DoParse(const CStringW& projFilePath, const WTString& projFileTxt);

  private:
	void Load(VaRadStudioPlugin::RsProjectData* projData);
};
#endif

class Vsproject : public ProjectInfo
{
  public:
	static const CStringW kExternalFilesProjectName;
	static Vsproject* CreateVsproject(const CStringW& vsprojFile, VsSolutionInfo::ProjectSettingsPtr projSettings);
	static Vsproject* CreateExternalFileProject();
	virtual ProjectInfo* CreateNewInfo() const;
	virtual ProjectInfo* Duplicate() const = 0;
	virtual bool CheckForChanges();

  protected:
	Vsproject(const CStringW& projFile) : ProjectInfo(projFile)
	{
	}
	void CommonParse(const CStringW& projectFilePath, const WTString& projectFileText);

	virtual void DoParse(const CStringW& projFilePath, const WTString& projFileTxt);
	virtual void CheckForClrUse(const WTString& fileTxt) = 0;
	virtual void ParseIntermediateDirs(const CStringW& projectFilePath, const WTString& projectFileText) = 0;
	virtual void ParsePropertySheets(const CStringW& parentPath, const CStringW& projectFilePath,
	                                 const WTString& projectFileText) = 0;

  private:
	void ParseIncludeDirs(const CStringW& projectFilePath, const WTString& projectFileText,
	                      const WTString& propertyName);
	void ParseForcedIncludes(const CStringW& projectFilePath, const WTString& projectFileText,
	                         const WTString& propertyName);
};
