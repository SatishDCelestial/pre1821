#include "stdafxed.h"
#include "ProjectInfo.h"
#include "file.h"
#include "WTString.h"
#include "log.h"
#include "incToken.h"
#include "token.h"
#include "TokenW.h"
#include "project.h"
#include "Settings.h"
#include "parse.h"
#include "ParseThrd.h"
#include <memory>
#include "FileTypes.h"
#include "SymbolRemover.h"
#include "StringUtils.h"
#include "DevShellAttributes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::ifstream;
using std::ios;
using std::ofstream;
#endif

using OWL::string;
using OWL::TRegexp;

// Local static declarations
// ----------------------------------------------------------------------------
//
static CStringW ReadAssignment(LPCSTR p);
static CStringW ReadProperty(const WTString& propName, LPCSTR& p);
static bool AreListsIdentical(const FileList& l1, const FileList& l2);

// ProjectInfo
// ----------------------------------------------------------------------------
// Parse - public interface
//
bool ProjectInfo::Parse()
{
	ClearData();

	const bool kIsFile = ::IsFile(mProjectFile);
	const bool kIsDir = kIsFile ? false : ::IsDir(mProjectFile);
	if (kIsFile || kIsDir)
	{
		// [case: 19496] project might actually be a directory - web based project
		const CStringW ppath = ::Path(mProjectFile);

		if (kIsFile)
		{
			::GetFTime(mProjectFile, &mProjectFileModTime);

			if (gShellAttr->IsDevenv8OrHigher())
			{
				// don't manually parse projects in vs2005+
				if (!::GetFSize(mProjectFile))
				{
					CStringW f(::Basename(mProjectFile));
					// [case: 104738] allow empty CppProperties.json and CMakeLists.txt
					if (f.CompareNoCase(L"CMakeLists.txt") && f.CompareNoCase(L"CppProperties.json"))
						return false;
				}
			}
			else
			{
				const WTString prjFileBuf = ::ReadFile(mProjectFile);
				if (prjFileBuf.IsEmpty())
					return false;

				DoParse(ppath, prjFileBuf);
			}
		}

		if (mProjectDirs.AddUniqueNoCase(ppath))
		{
			vCatLog("Environment.Directories", "PrjInf projdir %s", (const char*)CString(ppath));
		}
	}

	return true;
}

void ProjectInfo::Copy(const ProjectInfo& rhs)
{
	mProjectFile = rhs.mProjectFile;
	mVsProjectName = rhs.mVsProjectName;
	mProjectFileModTime = rhs.mProjectFileModTime;
	mPropertySheetModTimes = rhs.mPropertySheetModTimes;

	mProjectDirs.clear();
	mProjectDirs.AddHead(rhs.mProjectDirs);

	mIncludeDirs.clear();
	mIncludeDirs.AddHead(rhs.mIncludeDirs);

	mIntermediateDirs.clear();
	mIntermediateDirs.AddHead(rhs.mIntermediateDirs);

	mFiles.clear();
	mFiles.AddHead(rhs.mFiles);

#ifdef AVR_STUDIO
	mDeviceFiles.clear();
	mDeviceFiles.AddHead(rhs.mDeviceFiles);
#endif // AVR_STUDIO

	mReferences.clear();
	mReferences.AddHead(rhs.mReferences);

	mPropertySheets.clear();
	mPropertySheets.AddHead(rhs.mPropertySheets);

	mCppUsesClr = rhs.mCppUsesClr;
	mCppUsesWinRT = rhs.mCppUsesWinRT;
	mCppIgnoreStandardIncludes = rhs.mCppIgnoreStandardIncludes;
	mDeferredProject = rhs.mDeferredProject;

	mPlatformIncludeDirs.clear();
	mPlatformIncludeDirs.AddHead(rhs.mPlatformIncludeDirs);
	mPlatformLibraryDirs.clear();
	mPlatformLibraryDirs.AddHead(rhs.mPlatformLibraryDirs);
	mPlatformSourceDirs.clear();
	mPlatformSourceDirs.AddHead(rhs.mPlatformSourceDirs);
	mPlatformExternalIncludeDirs.clear();
	mPlatformExternalIncludeDirs.AddHead(rhs.mPlatformExternalIncludeDirs);
}

bool ProjectInfo::CheckForChanges(bool compareFileLists)
{
#if defined(RAD_STUDIO)
	return false;
#else
	_ASSERTE(!gShellAttr->IsDevenv8OrHigher());

	// we need this for MsDev for file add/remove (no events to subscribe to)
	// we use this old manual parse for VS7 just for laziness (didn't test new apis in vs7)
	std::unique_ptr<ProjectInfo> oldInfo(Duplicate());
	std::unique_ptr<ProjectInfo> newInfo(CreateNewInfo());
	if (!oldInfo || !newInfo)
		return false;

	if (!compareFileLists)
	{
		// make the file lists identical (instead of changing all the following
		// to ignore mFiles differences)
		_ASSERTE(!gShellAttr->IsMsdev());
		_ASSERTE(!newInfo->mFiles.size());
		newInfo->mFiles.Add(oldInfo->mFiles);
		_ASSERTE(newInfo->mFiles.size() == oldInfo->mFiles.size());

		newInfo->mProjectDirs.clear();
		newInfo->mProjectDirs.Add(oldInfo->mProjectDirs);
		_ASSERTE(newInfo->mProjectDirs.size() == oldInfo->mProjectDirs.size());
	}

	if (oldInfo->mProjectFileModTime == newInfo->mProjectFileModTime &&
	    std::equal(oldInfo->mPropertySheetModTimes.begin(), oldInfo->mPropertySheetModTimes.end(),
	               newInfo->mPropertySheetModTimes.begin()))
		return false;

	bool diff = false;
	FileList currentFileList;
	currentFileList.AddHead(newInfo->mFiles);

	// compare lists
	// iterate through new list - remove items that are in both lists
	if (oldInfo->mFiles.size() && newInfo->mFiles.size())
	{
		for (FileList::iterator it = oldInfo->mFiles.begin(); it != oldInfo->mFiles.end();)
		{
			const CStringW curFile((*it).mFilenameLower);
			if (newInfo->mFiles.Remove(curFile))
				oldInfo->mFiles.erase(it++);
			else
				++it;
		}
	}

	// for each item left in oldList, remove all defs
	if (oldInfo->mFiles.size())
	{
		diff = true;
		std::set<UINT> filesToRemove;
		for (FileList::const_iterator it = oldInfo->mFiles.begin(); it != oldInfo->mFiles.end(); ++it)
			filesToRemove.insert(it->mFileId);

		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new SymbolRemover(filesToRemove));
	}

	// for each item left in newList, queue item
	if (newInfo->mFiles.size() && !Psettings->m_FastProjectOpen)
	{
		diff = true;
		for (FileList::const_iterator it = newInfo->mFiles.begin(); it != newInfo->mFiles.end(); ++it)
			g_ParserThread->QueueNewProjectFile((*it).mFilename);
	}

	if (!diff)
	{
		if (oldInfo->mFiles.size() == newInfo->mFiles.size() &&
		    oldInfo->mProjectDirs.size() == newInfo->mProjectDirs.size() &&
		    oldInfo->mIncludeDirs.size() == newInfo->mIncludeDirs.size() &&
		    oldInfo->mIntermediateDirs.size() == newInfo->mIntermediateDirs.size() &&
		    oldInfo->mPropertySheets.size() == newInfo->mPropertySheets.size())
		{
			bool identical = ::AreListsIdentical(oldInfo->mPropertySheets, newInfo->mPropertySheets);
			if (identical)
				identical = ::AreListsIdentical(oldInfo->mIncludeDirs, newInfo->mIncludeDirs);
			if (identical)
				identical = ::AreListsIdentical(oldInfo->mUsingDirs, newInfo->mUsingDirs);
			if (identical)
				identical = ::AreListsIdentical(oldInfo->mIntermediateDirs, newInfo->mIntermediateDirs);
			if (identical)
				identical = ::AreListsIdentical(oldInfo->mProjectDirs, newInfo->mProjectDirs);
			if (identical)
				identical = ::AreListsIdentical(oldInfo->mFiles, newInfo->mFiles);
			diff = !identical;
		}
		else
			diff = true;
	}

	if (diff)
	{
		Copy(*newInfo);                  // update by copying the newInfo
		mFiles.AddHead(currentFileList); // the newInfo filelist is hammered above - restore cached copy
		return true;
	}
	else
	{
		// no difference that we care about - just update times
		mProjectFileModTime = newInfo->mProjectFileModTime;
		mPropertySheetModTimes.swap(newInfo->mPropertySheetModTimes);
		return false;
	}
#endif
}

void ProjectInfo::AddFile(const CStringW& file)
{
	const CStringW pth = ::Path(file);
	if (mLastDirAdded != pth)
	{
		mLastDirAdded = pth;
		mProjectDirs.AddUniqueNoCase(pth);
	}
	mFiles.AddHead(file);
}

void ProjectInfo::TakeFiles(FileList& files)
{
	// incoming list is most likely bigger than what we have, so swap first
	mFiles.swap(files);

	// if we had any files to begin with, take them back
	for (FileList::iterator it1 = files.begin(); it1 != files.end();)
	{
		mFiles.AddUnique(*it1);
		files.erase(it1++);
	}

	_ASSERTE(!files.size());

	// update projectDirs
	for (FileList::const_iterator it = mFiles.begin(); it != mFiles.end(); ++it)
	{
		const CStringW pth = ::Path((*it).mFilename);
		if (mLastDirAdded != pth)
		{
			mLastDirAdded = pth;
			mProjectDirs.AddUniqueNoCase(pth);
		}
	}
}

void ProjectInfo::TakeIncludeDirs(FileList& dirs)
{
	// incoming list is most likely bigger than what we have, so swap first
	mIncludeDirs.swap(dirs);

	// if we had any dirs to begin with, take them back
	for (FileList::iterator it1 = dirs.begin(); it1 != dirs.end();)
	{
		mIncludeDirs.AddUnique(*it1);
		dirs.erase(it1++);
	}

	_ASSERTE(!dirs.size());
}

#ifdef AVR_STUDIO
void ProjectInfo::UpdateDeviceFiles(FileList& files)
{
	if (mDeviceFiles == files)
		return;

	// remove old files
	std::set<UINT> filesToRemove;
	for (FileList::const_iterator it = mDeviceFiles.begin(); it != mDeviceFiles.end(); it++)
	{
		CStringW file = (*it).mFilename;
		RemoveFile(file);
		filesToRemove.insert(it->mFileId);
	}

	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new SymbolRemover(filesToRemove));

	mDeviceFiles.swap(files);

	// Add new device files
	for (FileList::const_iterator it = mDeviceFiles.begin(); it != mDeviceFiles.end(); it++)
	{
		CStringW file = (*it).mFilename;
		AddFile(file);

		const CStringW pth = ::Path((*it).mFilename);
		mProjectDirs.AddUniqueNoCase(pth);

		if (g_ParserThread)
			g_ParserThread->QueueFile(file);
	}

	files.clear();
}
#endif

void ProjectInfo::RemoveFile(const CStringW& file)
{
	mFiles.Remove(file);
}

bool ProjectInfo::ContainsFile(const CStringW& file) const
{
	return mFiles.ContainsNoCase(file);
}

bool ProjectInfo::ContainsFile(const UINT fileId) const
{
	return mFiles.Contains(fileId);
}

void ProjectInfo::ClearData()
{
	mProjectDirs.clear();
	mIncludeDirs.clear();
	mIntermediateDirs.clear();
	mFiles.clear();
#ifdef AVR_STUDIO
	mDeviceFiles.clear();
#endif
	mReferences.clear();
	mPropertySheets.clear();
	mPropertySheetModTimes.clear();
	mPlatformIncludeDirs.clear();
	mPlatformLibraryDirs.clear();
	mPlatformSourceDirs.clear();
	mPlatformExternalIncludeDirs.clear();
}

void ProjectInfo::MinimalUpdateFrom(VsSolutionInfo::ProjectSettings& newSettings)
{
	bool anyUpdates = false;
	bool anyPlatformUpdates = false;

	if (mCppUsesClr != newSettings.mCppHasManagedConfig)
	{
		anyUpdates = true;
		mCppUsesClr = newSettings.mCppHasManagedConfig;
	}

	if (mCppUsesWinRT != newSettings.mCppIsWinRT)
	{
		anyUpdates = true;
		mCppUsesWinRT = newSettings.mCppIsWinRT;
	}

	if (mIncludeDirs != newSettings.mConfigFullIncludeDirs)
	{
		anyUpdates = true;
		mIncludeDirs.swap(newSettings.mConfigFullIncludeDirs);
	}

	if (mUsingDirs != newSettings.mConfigUsingDirs)
	{
		anyUpdates = true;
		mUsingDirs.swap(newSettings.mConfigUsingDirs);
	}

	mCppIgnoreStandardIncludes = newSettings.mCppIgnoreStandardIncludes;

	for (FileList::iterator it1 = newSettings.mConfigForcedIncludes.begin();
	     it1 != newSettings.mConfigForcedIncludes.end();)
	{
		if (!mFiles.Contains((*it1).mFileId))
		{
			if (mFiles.AddUnique(*it1))
				GlobalProject->FilesListNeedsUpdate();
			if (mProjectDirs.AddUniqueNoCase(::Path((*it1).mFilename)))
				anyUpdates = true;
		}
		newSettings.mConfigForcedIncludes.erase(it1++);
	}

	if (mPlatformIncludeDirs != newSettings.mPlatformIncludeDirs)
	{
		mPlatformIncludeDirs.swap(newSettings.mPlatformIncludeDirs);
		anyPlatformUpdates = true;
	}

	if (mPlatformLibraryDirs != newSettings.mPlatformLibraryDirs)
	{
		mPlatformLibraryDirs.swap(newSettings.mPlatformLibraryDirs);
		anyPlatformUpdates = true;
	}

	if (mPlatformSourceDirs != newSettings.mPlatformSourceDirs)
	{
		mPlatformSourceDirs.swap(newSettings.mPlatformSourceDirs);
		anyPlatformUpdates = true;
	}

	if (mPlatformExternalIncludeDirs != newSettings.mPlatformExternalIncludeDirs)
	{
		mPlatformExternalIncludeDirs.swap(newSettings.mPlatformExternalIncludeDirs);
		anyPlatformUpdates = true;
	}

#ifdef AVR_STUDIO
	UpdateDeviceFiles(newSettings.mDeviceFiles);
#endif // AVR_STUDIO

	if (anyUpdates)
		GlobalProject->AdditionalDirsNeedsUpdate();

	if (anyPlatformUpdates && gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 56185]
		IncludeDirs::ProjectLoaded();
	}
}

uint ProjectInfo::GetMemSize() const
{
	uint sz = 0;
	sz += mProjectFile.GetLength() * sizeof(wchar_t);
	sz += mVsProjectName.GetLength() * sizeof(wchar_t);
	sz += mLastDirAdded.GetLength() * sizeof(wchar_t);

	sz += mProjectDirs.GetMemSize();
	sz += mIncludeDirs.GetMemSize();
	sz += mUsingDirs.GetMemSize();
	sz += mIntermediateDirs.GetMemSize();
	sz += mFiles.GetMemSize();
	sz += mReferences.GetMemSize();
	sz += mPropertySheets.GetMemSize();
	sz += mPlatformIncludeDirs.GetMemSize();
	sz += mPlatformLibraryDirs.GetMemSize();
	sz += mPlatformSourceDirs.GetMemSize();
	sz += mPlatformExternalIncludeDirs.GetMemSize();

	return sz;
}

bool ProjectInfo::IsSharedProjectType() const
{
	CStringW projExt(GetBaseNameExt(mProjectFile));
	projExt.MakeLower();
	if (projExt == L"shproj" || projExt == L"vcxitems" || projExt == L"projitems")
		return true;
	return false;
}

bool ProjectInfo::HasSharedProjectDependency() const
{
	for (FileList::const_iterator it = mPropertySheets.begin(); it != mPropertySheets.end(); ++it)
	{
		const CStringW propExt(GetBaseNameExt((*it).mFilename));
		if (!propExt.CompareNoCase(L"shproj") || !propExt.CompareNoCase(L"vcxitems") ||
		    !propExt.CompareNoCase(L"projitems"))
			return true;
	}

	return false;
}

class VisualStudioProject : public Vsproject
{
  public:
	VisualStudioProject(const CStringW& projFile) : Vsproject(projFile)
	{
	}
	virtual ProjectInfo* Duplicate() const;

  protected:
	virtual void CheckForClrUse(const WTString& fileTxt);
	virtual void ParseIntermediateDirs(const CStringW& projectFilePath, const WTString& projectFileText);
	virtual void ParsePropertySheets(const CStringW& parentPath, const CStringW& projectFilePath,
	                                 const WTString& projectFileText);
};

class MsBuildProject : public Vsproject
{
  public:
	MsBuildProject(const CStringW& projFile) : Vsproject(projFile)
	{
	}
	virtual ProjectInfo* Duplicate() const;

  protected:
	virtual void DoParse(const CStringW& /*projFilePath*/, const WTString& /*projFileTxt*/)
	{
	}
	virtual void CheckForClrUse(const WTString& /*fileTxt*/)
	{
		_ASSERTE(!"MsBuildProject::CheckForClrUse shouldn't be called");
	}
	virtual void ParseIntermediateDirs(const CStringW& /*projectFilePath*/, const WTString& /*projectFileText*/)
	{
		_ASSERTE(!"MsBuildProject::ParseIntermediateDirs shouldn't be called");
	}
	virtual void ParsePropertySheets(const CStringW& /*parentPath*/, const CStringW& /*projectFilePath*/,
	                                 const WTString& /*projectFileText*/)
	{
		_ASSERTE(!"MsBuildProject::ParsePropertySheets shouldn't be called");
	}
};

// Vsproject
// ----------------------------------------------------------------------------
//
const CStringW Vsproject::kExternalFilesProjectName(L"External Files");

Vsproject* Vsproject::CreateExternalFileProject()
{
	if (gShellAttr->IsDevenv8OrHigher())
		return new MsBuildProject(kExternalFilesProjectName);
	else
		return new VisualStudioProject(kExternalFilesProjectName);
}

Vsproject* Vsproject::CreateVsproject(const CStringW& vsprojFile, VsSolutionInfo::ProjectSettingsPtr projSettings)
{
	Vsproject* prjInf;
#if defined(VA_CPPUNIT)
	prjInf = new MsBuildProject(vsprojFile);
#else
	if (gShellAttr->IsDevenv8OrHigher())
		prjInf = new MsBuildProject(vsprojFile);
	else
		prjInf = new VisualStudioProject(vsprojFile);
#endif

	if (!prjInf->Parse())
	{
#if !defined(VA_CPPUNIT)
		delete prjInf;
		prjInf = NULL;
		_ASSERTE(!"why is this project being rejected?");
#endif // VA_CPPUNIT
	}

	if (projSettings && prjInf)
	{
		prjInf->TakeFiles(projSettings->mFiles);
#ifdef AVR_STUDIO
		prjInf->UpdateDeviceFiles(projSettings->mDeviceFiles);
#endif // AVR_STUDIO

		_ASSERTE(!projSettings->mFiles.size());
		prjInf->mReferences.swap(projSettings->mReferences);

#if !defined(VA_CPPUNIT)
		if (gShellAttr->IsDevenv8OrHigher())
#endif
		{
			prjInf->mPropertySheets.swap(projSettings->mPropertySheets);
			prjInf->mIncludeDirs.swap(projSettings->mConfigFullIncludeDirs);
			prjInf->mUsingDirs.swap(projSettings->mConfigUsingDirs);
			prjInf->mIntermediateDirs.swap(projSettings->mConfigIntermediateDirs);

			for (FileList::iterator it1 = projSettings->mConfigForcedIncludes.begin();
			     it1 != projSettings->mConfigForcedIncludes.end();)
			{
				if (!prjInf->mFiles.Contains((*it1).mFileId))
				{
					prjInf->mFiles.AddUnique(*it1);
					prjInf->mProjectDirs.AddUniqueNoCase(::Path((*it1).mFilename));
				}
				projSettings->mConfigForcedIncludes.erase(it1++);
			}

			prjInf->mCppIgnoreStandardIncludes = projSettings->mCppIgnoreStandardIncludes;
			prjInf->mCppUsesClr = projSettings->mCppHasManagedConfig;
			prjInf->mCppUsesWinRT = projSettings->mCppIsWinRT;

			prjInf->mPlatformIncludeDirs.swap(projSettings->mPlatformIncludeDirs);
			prjInf->mPlatformLibraryDirs.swap(projSettings->mPlatformLibraryDirs);
			prjInf->mPlatformSourceDirs.swap(projSettings->mPlatformSourceDirs);
			prjInf->mPlatformExternalIncludeDirs.swap(projSettings->mPlatformExternalIncludeDirs);
		}

		prjInf->mVsProjectName = projSettings->mVsProjectName;
		prjInf->mPrecompiledHeaderFile = projSettings->mPrecompiledHeaderFile;
		prjInf->mPseudoProject = projSettings->mPseudoProject;
		prjInf->mDeferredProject = projSettings->mDeferredProject;
	}

	return prjInf;
}

ProjectInfo* Vsproject::CreateNewInfo() const
{
	if (mProjectFile == kExternalFilesProjectName)
		return NULL;
	return CreateVsproject(mProjectFile, NULL);
}

void Vsproject::DoParse(const CStringW& projFilePath, const WTString& projFileTxt)
{
	ParsePropertySheets(projFilePath, projFilePath, projFileTxt);
	CommonParse(projFilePath, projFileTxt);
}

void Vsproject::CommonParse(const CStringW& projFilePath, const WTString& fileTxt)
{
	ParseIncludeDirs(projFilePath, fileTxt, "IncludeSearchPath");
	ParseIncludeDirs(projFilePath, fileTxt, "AdditionalIncludeDirectories");
	ParseForcedIncludes(projFilePath, fileTxt, "ForcedIncludeFiles");
	ParseForcedIncludes(projFilePath, fileTxt, "ForcedIncludes");
	ParseIntermediateDirs(projFilePath, fileTxt);
	CheckForClrUse(fileTxt);
}

void Vsproject::ParseIncludeDirs(const CStringW& projectFilePath, const WTString& projectFileText,
                                 const WTString& propertyName)
{
	LPCTSTR p = ::strstrWholeWord(projectFileText, propertyName);
	while (p && *p)
	{
		CStringW paths = ::ReadProperty(propertyName, p);
		if (paths.GetLength())
		{
			vLog("PrA %s : %s ->", propertyName.c_str(), (const char*)CString(paths));
			paths = ::WTExpandEnvironmentStrings(paths);
			vLog(" %s\n", (const char*)CString(paths));
			TokenW t = paths;
			while (t.more())
			{
				CStringW pth(t.read(L";,"));
				// remove leading whitespace - vsnet bug 540
				while (pth[0] == ' ')
					pth = pth.Mid(1);
				if (pth.GetLength())
					pth = ::BuildPath(pth, projectFilePath, false, false);
				if (pth.GetLength() && mIncludeDirs.AddUniqueNoCase(pth))
				{
					vLog("VspParseIncludeDirs %s", (const char*)CString(pth));
				}
			}
		}
	}
}

void Vsproject::ParseForcedIncludes(const CStringW& projectFilePath, const WTString& projectFileText,
                                    const WTString& propertyName)
{
	LPCTSTR p = strstr(projectFileText.c_str(), propertyName.c_str());
	while (p && *p)
	{
		CStringW files = ::ReadProperty(propertyName, p);
		if (files.GetLength())
		{
			vLog("PrF %s: %s ->", propertyName.c_str(), (const char*)CString(files));
			files = ::WTExpandEnvironmentStrings(files);
			vLog(" %s\n", (const char*)CString(files));
			TokenW t = files;
			while (t.more())
			{
				const CStringW file(::BuildPath(t.read(L";,"), projectFilePath));
				if (file.GetLength())
				{
					int ftype = ::GetFileType(file);
					if ((::StrStrIW(file, L"stdafx.h") || ::StrStrIW(file, L"stdatl.h")) && !g_WTL_FLAG)
					{
						const WTString txt = ::ReadFile(file);
						if (txt.FindNoCase("<atlapp.h>") != -1)
							g_WTL_FLAG = TRUE;
					}
					if (ftype != Image)
						AddFile(file);
				}
			}
		}
	}
}

bool Vsproject::CheckForChanges()
{
	if (gShellAttr->IsDevenv8OrHigher())
	{
		ProjectReparse::QueueProjectForReparse(mProjectFile);
		return false;
	}

	return ProjectInfo::CheckForChanges(false);
}

// VisualStudioProject
// ----------------------------------------------------------------------------
//
void VisualStudioProject::ParsePropertySheets(const CStringW& parentPath, const CStringW& projectFilePath,
                                              const WTString& projectFileText)
{
	const WTString propName("InheritedPropertySheets");
	LPCTSTR p = ::strstrWholeWord(projectFileText, propName);
	while (p && *p)
	{
		CStringW paths = ::ReadProperty(propName, p);
		if (paths.GetLength())
		{
			vLog("InheritedPropertySheets: %s ->", (const char*)CString(paths));
			paths = ::WTExpandEnvironmentStrings(paths);
			vLog(" %s\n", (const char*)CString(paths));
			TokenW t = paths;
			while (t.more())
			{
				const CStringW partialPropFilePath(t.read(L";,"));
				CStringW propFile(::BuildPath(partialPropFilePath, parentPath));
				if (propFile.GetLength())
				{
					vLog("  found : %s\n", (const char*)CString(propFile));
					if (!mPropertySheets.AddUniqueNoCase(propFile))
						continue;

					FILETIME ft;
					::GetFTime(propFile, &ft);
					mPropertySheetModTimes.push_back(ft);
					const WTString propFileTxt = ::ReadFile(propFile);
					if (!propFileTxt.IsEmpty())
					{
						ParsePropertySheets(::Path(propFile), projectFilePath, propFileTxt);
						CommonParse(projectFilePath, propFileTxt);
					}
				}
				else
				{
					vLog("  not found : %s\n", (const char*)CString(partialPropFilePath));
				}
			}
		}
	}
}

void VisualStudioProject::ParseIntermediateDirs(const CStringW& projectFilePath, const WTString& projectFileText)
{
	// IntermediateDirectories for tlh parsing.
	const WTString propName("IntermediateDirectory");
	LPCTSTR p = ::strstrWholeWord(projectFileText, propName);
	while (p && *p)
	{
		CStringW paths = ::ReadProperty(propName, p);
		if (paths.GetLength())
		{
			vLog("IntermediateDirectory: %s ->", (const char*)CString(paths));
			paths = ::WTExpandEnvironmentStrings(paths);
			vLog(" %s\n", (const char*)CString(paths));
			TokenW t = paths;
			while (t.more())
			{
				CStringW pth(::BuildPath(t.read(L";,"), projectFilePath, false, false));
				if (pth.GetLength())
					mIntermediateDirs.AddUniqueNoCase(pth);
			}
		}
	}
}

ProjectInfo* VisualStudioProject::Duplicate() const
{
	VisualStudioProject* prjInf = new VisualStudioProject(mProjectFile);
	prjInf->Copy(*this);
	return prjInf;
}

void VisualStudioProject::CheckForClrUse(const WTString& fileTxt)
{
	if (!mCppUsesClr &&
	    (fileTxt.Find("ManagedExtensions=\"1\"") != -1 || fileTxt.Find("ManagedExtensions=\"2\"") != -1 ||
	     fileTxt.Find("ManagedExtensions=\"3\"") != -1 || fileTxt.Find("ManagedExtensions=\"4\"") != -1 ||
	     fileTxt.FindNoCase("/clr") != -1))
		mCppUsesClr = true;
}

// MsBuildProject
// ----------------------------------------------------------------------------
//
ProjectInfo* MsBuildProject::Duplicate() const
{
	MsBuildProject* prjInf = new MsBuildProject(mProjectFile);
	prjInf->Copy(*this);
	return prjInf;
}

// DspProject
// ----------------------------------------------------------------------------
//
DspProject* DspProject::CreateDspProject(const CStringW& dspFile)
{
	DspProject* prjInf = new DspProject(dspFile);
	if (!prjInf->Parse())
	{
		delete prjInf;
		prjInf = NULL;
	}
	return prjInf;
}

ProjectInfo* DspProject::CreateNewInfo() const
{
	return CreateDspProject(mProjectFile);
}

ProjectInfo* DspProject::Duplicate() const
{
	DspProject* prjInf = new DspProject(mProjectFile);
	prjInf->Copy(*this);
	return prjInf;
}

void DspProject::DoParse(const CStringW& projFilePath, const WTString& projFileTxt)
{
	ParseIncludeDirs(projFilePath);
	BuildFileList(projFilePath, projFileTxt);
	ParseIntermediateDirs(projFilePath, projFileTxt);
}

void DspProject::BuildFileList(const CStringW& projectFilePath, const WTString& projectFileText)
{
	LPCTSTR pBegin = projectFileText.c_str(), pEnd;
	while (pBegin && *pBegin)
	{
		pEnd = strstr(pBegin, "\nSOURCE=");
		if (!pEnd)
			pEnd = strstr(pBegin, "\nSource=");
		if (!pEnd)
			break;

		pBegin = pEnd + 1;
		pEnd = strstr(pBegin, "\n");
		if (!pEnd)
		{
			ASSERT(FALSE);
			break;
		}

		WTString s(pBegin, ptr_sub__int(pEnd, pBegin));
		pBegin = pEnd + 1;

		Log(".");
		if (_tcsnicmp(s.c_str(), "SOURCE=", 7) == 0)
		{
			token tf = s.Mid(7);
			tf.ReplaceAll(TRegexp("[\"\r\n]"), string(""));
			const CStringW f(BuildPath(CStringW(tf.c_str()), projectFilePath));
			if (f.GetLength())
			{
				const int pType = GetFileType(f);
				if (pType != Image)
					AddFile(f);
			}
		}
	}
}

void DspProject::ParseIncludeDirs(const CStringW& projectFilePath)
{
	DEFTIMERNOTE(DspParseIncludeDirs, CString(mProjectFile));
	// parse current project file for additional include dirs
	// dirs found are cumulatively added to mIncludeDirs
	// closing a project will clear mIncludeDirs
	// setting a project as active in a multi-project workspace has no effect
	// this should only be called from LoadProject
	// changes to a project file by the user will not be read in
	//   until the next time the project is opened
	LOG2("DspParseIncludeDirs");

	WTString ln;
	CStringW dirSubstr;

	// additional include dirs - search .dsp file for:
	// # ADD CPP ... /I "d:\projects" /I "e:\cool" ...
	// exclude regular include dirs - search .dsp file for:
	// # ADD CPP ... /X ...
	// forced include files - search .dsp file for:
	// # ADD CPP ... /FI[ ]["]foo.h["]
#if _MSC_VER <= 1200
	ifstream ifs(mProjectFile, ios::in | ios::nocreate);
#else
	ifstream ifs(mProjectFile, ios::in | ios::_Nocreate);
#endif
	while (ifs)
	{
		ln.read_line(ifs);
#if _MSC_VER > 1200
		if (!ifs && ln.IsEmpty() && !ifs.eof())
		{
			// vc7 stream lib will set failbit if line is empty
			ifs.clear();
			ifs.seekg(2, std::ios::cur); // seek past \r\n
		}
#endif
		if (ln.Find("# ADD CPP") == 0 || ln.Find("# ADD BASE CPP") == 0)
		{
			int pos;
			const WTString origLn(ln);
			//			if (ln.Find(" /X ") != -1)
			//				m_useAllIncludes = false;

			// search for include dirs
			while (((pos = ln.Find(" /I \""))) != -1)
			{
				ln = ln.Mid(pos + strlen(" /I \""));
				pos = ln.Find("\"");
				ASSERT(pos != -1);
				dirSubstr = BuildPath(CStringW(ln.Left(pos).Wide()), projectFilePath, false, false);
				if (dirSubstr.GetLength() && mIncludeDirs.AddUniqueNoCase(dirSubstr))
				{
					vLog("DspParseIncludeDirs addl dir %s", (const char*)CString(dirSubstr));
				}

				ln = ln.Mid(pos);
			}

			// search for forced include files
			ln = origLn;
			while (((pos = ln.Find(" /FI"))) != -1)
			{
				const int fixedPos = ln.Find(" /FIXED");
				if (fixedPos == pos)
				{
					ln = ln.Mid(fixedPos + ::strlen(" /FIXED"));
					continue;
				}

				ln = ln.Mid(pos + ::strlen(" /FI"));
				// if space is next, increment
				if (ln[0] == ' ')
					ln = ln.Mid(1);
				// if quote is next, increment
				if (ln[0] == '\"')
				{
					ln = ln.Mid(1);
					// find closing quote
					pos = ln.Find('\"');
					ASSERT(pos != -1);
				}
				else
				{
					// else filename starts here
					// find end of name
					pos = ln.Find(' ');
					if (-1 == pos)
						pos = ln.GetLength();
				}

				// include file is from 0 to pos
				const CStringW f(BuildPath(CStringW(ln.Left(pos).Wide()), projectFilePath));
				if (f.GetLength())
					AddFile(f);

				ln = ln.Mid(pos);
			}
		}
	}
}

void DspProject::ParseIntermediateDirs(const CStringW& projectFilePath, const WTString& projectFileText)
{
	// IntermediateDirectories for tlh parsing.
	LPCSTR p = strstrWholeWord(projectFileText, "Output_Dir");
	while (p && *p)
	{
		Log("Output_Dir");
		CStringW path = ::ReadAssignment(p);
		path = WTExpandEnvironmentStrings(path);
		Log((const char*)CString(path));
		TokenW t = path;
		while (t.more())
		{
			CStringW pth(BuildPath(t.read(L";,"), projectFilePath, false, false));
			if (pth.GetLength())
				mIntermediateDirs.AddUniqueNoCase(pth);
		}
		p = strstrWholeWord(++p, "Output_Dir");
	}
}


#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
// RadStudioProject
// ----------------------------------------------------------------------------
//
RadStudioProject* RadStudioProject::CreateRadStudioProject(VaRadStudioPlugin::RsProjectDataPtr projData)
{
	RadStudioProject* prjInf = new RadStudioProject(projData->mProject);
	prjInf->Load(projData.get());
	return prjInf;
}

ProjectInfo* RadStudioProject::CreateNewInfo() const
{
	_ASSERTE(!"not used in RadStudio");
	throw std::exception("not implemented for RadStudio");
}

ProjectInfo* RadStudioProject::Duplicate() const
{
	_ASSERTE(!"not used in RadStudio");
	throw std::exception("not implemented for RadStudio");
}

void RadStudioProject::DoParse(const CStringW& projFilePath, const WTString& projFileTxt)
{
	_ASSERTE(!"not used in RadStudio");
	throw std::exception("not implemented for RadStudio");
}

void RadStudioProject::Load(VaRadStudioPlugin::RsProjectData* projData)
{
	ClearData();

	const bool kIsFile = ::IsFile(mProjectFile);
// 	const bool kIsDir = kIsFile ? false : ::IsDir(mProjectFile);

	const CStringW projectFilePath = ::Path(mProjectFile);
	if (mProjectDirs.AddUniqueNoCase(projectFilePath))
	{
		vLog("PrjInf projdir %s", (const char*)CString(projectFilePath));
	}

	if (kIsFile)
		::GetFTime(mProjectFile, &mProjectFileModTime);

	TokenW t = projData->mIncludeDirs;
	while (t.more())
	{
		CStringW pth(t.read(L";"));
		if (pth.GetLength() < 2)
			continue;

		pth = ::BuildPath(pth, projectFilePath, false, false);

		if (projData->ShouldIgnorePath(pth))
			continue;

		if (pth.GetLength() && mIncludeDirs.AddUniqueNoCase(pth))
		{
			vLog("RadStudioIncludeDirs1 %s", (const char*)CString(pth));
		}
	}

	t = projData->mFileIncludeDirs;
	while (t.more())
	{
		CStringW pth(t.read(L";"));
		if (pth.GetLength() < 2)
			continue;

		pth = ::BuildPath(pth, projectFilePath, false, false);

		if (projData->ShouldIgnorePath(pth))
			continue;

		if (pth.GetLength() && mIncludeDirs.AddUniqueNoCase(pth))
		{
			vLog("RadStudioIncludeDirs2 %s", (const char*)CString(pth));
		}
	}

	for (const auto& curSrcFile : projData->mSourceFiles)
	{
		// new projects do not have files on disk, so can't enforce file existence
		const CStringW file(::BuildPath(curSrcFile, projectFilePath, true, false));
		if (file.IsEmpty())
			continue;

		if (projData->ShouldIgnorePath(file))
			continue;

		const int ftype = ::GetFileType(file);
		if (ftype != Image)
			AddFile(file);
	}
}
#endif

// local utility functions
// ----------------------------------------------------------------------------
//
CStringW ReadAssignment(LPCSTR p)
{
	CString val;
	for (; *p && *p != '"'; p++)
		;
	if (*p == '"')
		p++; // eat quote
	for (; *p && *p != '"'; p++)
		val += *p;
	return CStringW(val);
}

CStringW ReadProperty(const WTString& propName, LPCSTR& p)
{
	p += propName.GetLength();
	char prevCh = '\0';
	while (*p == '"' || *p == '>' || *p == '=' || *p == '\'' || *p == ' ')
	{
		const char curCh = *p++;
		if (prevCh == '"' && curCh == '"')
		{
			p = strstrWholeWord(p, propName);
			return CStringW();
		}
		if (prevCh == '\'' && curCh == '\'')
		{
			p = strstrWholeWord(p, propName);
			return CStringW();
		}
		prevCh = curCh;
	}

	CString val;
	for (; *p && *p != '"' && *p != '<'; p++)
		val += *p;

	// set p to next instance of propName (skipping closing tag)
	// increment past </propName> or />
	LPCSTR pos1 = strstrWholeWord(p, propName);
	LPCSTR pos2 = strstr(p, "/>");
	if (pos1 && !pos2)
		p = pos1;
	else if (pos2 && !pos1)
		p = pos2;
	else if (!pos1 && !pos2)
		p = NULL;
	else
		p = min(pos1, pos2);

	if (p && *p)
		++p;

	if (p && *p)
		p = strstrWholeWord(p, propName);

	return CStringW(val);
}

#if !defined(RAD_STUDIO)
static bool AreListsIdentical(const FileList& l1, const FileList& l2)
{
	if (l1.size() != l2.size())
		return false;

	FileList::const_iterator it1 = l1.begin();
	FileList::const_iterator it2 = l2.begin();

	while (it1 != l1.end() && it2 != l2.end())
	{
		const CStringW cur1((*it1++).mFilenameLower);
		const CStringW cur2((*it2++).mFilenameLower);
		if (cur1 != cur2)
			return false;
	}

	_ASSERTE(it1 == l1.end() && it2 == l2.end());
	return true;
}
#endif
