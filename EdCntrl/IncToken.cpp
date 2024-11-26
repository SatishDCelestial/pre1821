#include "stdafxed.h"
#include "incToken.h"
#include "TokenW.h"
#include "Project.h"
#include "timer.h"
#include "settings.h"
#include "file.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "assert_once.h"
#include "log.h"
#include "settings.h"
#include "RegKeys.h"
#include "SolutionFiles.h"
#include "ProjectInfo.h"
#include "FileId.h"
#include <unordered_map>
#include "SemiColonDelimitedString.h"
#include "LogElapsedTime.h"
#include <filesystem>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CStringW IncludeDirs::m_additionalIncludes = L"";
CStringW IncludeDirs::m_sysInclude = L"";
CStringW IncludeDirs::m_solutionPrivateSysIncludes = L"";
CStringW IncludeDirs::m_importDirs = L"";
CStringW IncludeDirs::m_srcDirs = L"";
WTString IncludeDirs::mPlatformKey = "";
RWLock IncludeDirs::mLock;

const WTString kCustom = "Custom";
static const WTString kSlashCustom = WTString("\\") + kCustom;
const WTString kProjectDefinedPlatform = "Project defined"; // #ProjectDefinedLiteral

using DirIdToStatusMap = std::unordered_map<UINT, TriState>;
DirIdToStatusMap gDirIdStatusMap;

bool IncludeDirs::IsCustom()
{
	return !kCustom.Compare(Psettings->m_platformIncludeKey);
}

bool IncludeDirs::IsSetup() const
{
	bool isSetup = true;
	// require reinit if sysInclude is empty - but allow empty if custom
	if (!m_sysInclude.GetLength() && !IsCustom())
		isSetup = false;
	return isSetup;
}

void IncludeDirs::Reset()
{
	{
		RWLockWriter l(mLock);
		if (kProjectDefinedPlatform == mPlatformKey)
			return;

		m_sysInclude.Empty();
		m_solutionPrivateSysIncludes.Empty();
		m_additionalIncludes.Empty();
		m_importDirs.Empty();
		m_srcDirs.Empty();
		mPlatformKey.Empty();
		gDirIdStatusMap.clear();
	}

	IncludeDirs dirs; // calls Init()
}

CStringW ValidateDirList(LPCWSTR dirLst)
{
	CStringW lst;
	CStringW tlst(dirLst);
	if (tlst.IsEmpty() || tlst == L";")
		return lst;

	_ASSERTE(wcsstr(L"\\;", dirLst) == 0);
	tlst.Replace(L"\\;", L";"); // no dirs should end with a \ 

	TokenW t = tlst;
	while (t.more() > 1)
	{
		CStringW dir = t.read(L";");
		if (dir.GetLength())
		{
			dir = MSPath(dir);
			if (GetFileAttributesW(dir) & CFile::directory)
				lst += dir + L";";
		}
	}
	return lst;
}

static CStringW s_sysInclude;
static CStringW s_importDirs;
static CStringW s_srcDirs;

void IncludeDirs::SetupPlatform(LPCSTR thePlatform)
{
	RWLockWriter l(mLock);
	gDirIdStatusMap.clear();

	if (m_sysInclude.GetLength() && mPlatformKey == thePlatform)
		return;

	if (kCustom == thePlatform)
		return; // this should only occur if called from VaOptions

	if (kProjectDefinedPlatform == thePlatform)
	{
		SetupProjectPlatform();
		return;
	}

	mPlatformKey = thePlatform;
	m_sysInclude.Empty();
	m_solutionPrivateSysIncludes.Empty();
	m_additionalIncludes.Empty();
	m_importDirs.Empty();
	m_srcDirs.Empty();

	WTString keyNamePrefix(gShellAttr->GetPlatformDirectoryKeyName(mPlatformKey.c_str()));
	CString secondChanceKeyName(gShellAttr->GetPlatformDirectoryKeyName2(mPlatformKey.c_str()));
	if (keyNamePrefix.IsEmpty())
	{
		if (secondChanceKeyName.IsEmpty())
			return;
		keyNamePrefix = secondChanceKeyName;
		secondChanceKeyName.Empty();
	}

	CStringW sysInclude;
#if defined(RAD_STUDIO)
	_ASSERTE(gVaRadStudioPlugin);
	if (gFileIdManager)
	{
		FileList dirList;
		CStringW pth;
		SemiColonDelimitedString dirs;
		if (gShellAttr->GetDefaultPlatform() == thePlatform)
		{
			dirs = gVaRadStudioPlugin->GetSystemIncludeDirs();
			while (dirs.HasMoreItems())
			{
				dirs.NextItem(pth);
				dirList.AddUniqueNoCase(pth);
			}
		}
		else
		{
			LPCSTR valNames[] = {"IncludePath_Clang32", "IncludePath"};
			for (const auto& it : valNames)
			{
				CStringW tmp(::GetRegValueW(HKEY_CURRENT_USER, keyNamePrefix.c_str(), it));
				if (tmp.IsEmpty())
					continue;

				tmp = ::WTExpandEnvironmentStrings(tmp);
				dirs = tmp;
				while (dirs.HasMoreItems())
				{
					dirs.NextItem(pth);
					dirList.AddUniqueNoCase(pth);
				}
				break;
			}
		}

		for (const auto& it : dirList)
			sysInclude += it.mFilename + L";";
	}
	else
		sysInclude = gVaRadStudioPlugin->GetSystemIncludeDirs();
#else
	sysInclude = GetRegValueW(HKEY_CURRENT_USER, keyNamePrefix.c_str(), ID_RK_MSDEV_INC_VALUE) +
	             GetRegValueW(HKEY_LOCAL_MACHINE, keyNamePrefix.c_str(), ID_RK_MSDEV_INC_VALUE);
#endif
	if (!sysInclude.GetLength() && secondChanceKeyName.GetLength())
	{
		sysInclude = GetRegValueW(HKEY_CURRENT_USER, secondChanceKeyName, ID_RK_MSDEV_INC_VALUE) +
		             GetRegValueW(HKEY_LOCAL_MACHINE, secondChanceKeyName, ID_RK_MSDEV_INC_VALUE);
	}

	if (s_sysInclude != sysInclude || !m_sysInclude.GetLength())
	{
		s_sysInclude = sysInclude;
		sysInclude = WTExpandEnvironmentStrings(sysInclude);
		m_sysInclude = ValidateDirList(sysInclude);
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
		            ID_RK_SYSTEMINCLUDE, m_sysInclude);
	}

#if !defined(RAD_STUDIO)
	CStringW importDirs = GetRegValueW(HKEY_CURRENT_USER, keyNamePrefix.c_str(), ID_RK_MSDEV_LIB_VALUE) +
	                      GetRegValueW(HKEY_LOCAL_MACHINE, keyNamePrefix.c_str(), ID_RK_MSDEV_LIB_VALUE);
	if (!importDirs.GetLength() && secondChanceKeyName.GetLength())
	{
		importDirs = GetRegValueW(HKEY_CURRENT_USER, secondChanceKeyName, ID_RK_MSDEV_LIB_VALUE) +
		             GetRegValueW(HKEY_LOCAL_MACHINE, secondChanceKeyName, ID_RK_MSDEV_LIB_VALUE);
	}

	if (s_importDirs != importDirs || !m_importDirs.GetLength())
	{
		s_importDirs = importDirs;
		importDirs = WTExpandEnvironmentStrings(importDirs);
		m_importDirs = ValidateDirList(importDirs);
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
		            ID_RK_MSDEV_LIB_VALUE, m_importDirs);
	}
#endif

	CStringW srcDirs;
#if defined(RAD_STUDIO)
	_ASSERTE(gVaRadStudioPlugin);
	if (gFileIdManager)
	{
		FileList dirList;
		CStringW pth;
		SemiColonDelimitedString dirs;
		// if (gShellAttr->GetDefaultPlatform() == thePlatform)
		{
			dirs = gVaRadStudioPlugin->GetSystemSourceDirs();
			while (dirs.HasMoreItems())
			{
				dirs.NextItem(pth);
				dirList.AddUniqueNoCase(pth);
			}
		}
#ifdef _DEBUGxx
		else
		{
			LPCSTR valNames[] = {"BrowsingPath_Clang32", "BrowsingPath"};
			for (const auto& it : valNames)
			{
				CStringW tmp = ::GetRegValueW(HKEY_CURRENT_USER, keyNamePrefix.c_str(), it);
				if (tmp.IsEmpty())
					continue;

				tmp = ::WTExpandEnvironmentStrings(tmp);
				dirs = tmp;
				while (dirs.HasMoreItems())
				{
					dirs.NextItem(pth);
					dirList.AddUniqueNoCase(pth);
				}
				break;
			}
		}
#endif

		for (const auto& it : dirList)
			srcDirs += it.mFilename + L";";
	}
	else
		srcDirs = gVaRadStudioPlugin->GetSystemSourceDirs();
#else
	srcDirs = GetRegValueW(HKEY_CURRENT_USER, keyNamePrefix.c_str(), ID_RK_MSDEV_SRC_VALUE) +
	          GetRegValueW(HKEY_LOCAL_MACHINE, keyNamePrefix.c_str(), ID_RK_MSDEV_SRC_VALUE);
	if (!srcDirs.GetLength() && secondChanceKeyName.GetLength())
	{
		srcDirs = GetRegValueW(HKEY_CURRENT_USER, secondChanceKeyName, ID_RK_MSDEV_SRC_VALUE) +
		          GetRegValueW(HKEY_LOCAL_MACHINE, secondChanceKeyName, ID_RK_MSDEV_SRC_VALUE);
	}
#endif

	if (srcDirs != s_srcDirs || !m_srcDirs.GetLength())
	{
		s_srcDirs = srcDirs;
		srcDirs = WTExpandEnvironmentStrings(srcDirs);
		m_srcDirs = ValidateDirList(srcDirs);
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(), ID_RK_GOTOSRCDIRS,
		            m_srcDirs);
	}
}

void IncludeDirs::SetupProjectPlatform()
{
#if !defined(VA_CPPUNIT)
	_ASSERTE(gShellAttr->IsDevenv10OrHigher());
#endif
#ifdef _DEBUG
	_ASSERTE(mLock.GetWritingThread() == ::GetCurrentThreadId());
#endif
	if (m_sysInclude.GetLength() && mPlatformKey == kProjectDefinedPlatform)
	{
		gDirIdStatusMap.clear();
		return;
	}

	mPlatformKey = kProjectDefinedPlatform;
	::strcpy(Psettings->m_platformIncludeKey, kProjectDefinedPlatform.c_str());

	size_t projectsLoaded = 0;
	size_t deferredProjCnt = 0;
	if (GlobalProject)
	{
		FileList platformSysIncludeList, platformLibList, platformSrcList;

		{
			m_solutionPrivateSysIncludes.Empty();
			RWLockReader lck1;
			const FileList& solPrivateIncDirs = GlobalProject->GetSolutionPrivateSystemIncludeDirs(lck1);
			for (auto f : solPrivateIncDirs)
			{
				platformSysIncludeList.AddUniqueNoCase(f.mFilename);
				m_solutionPrivateSysIncludes += f.mFilename + L";";
			}
		}

		{
			RWLockReader lck;
			const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
			projectsLoaded = projMap.size();
			Project::ProjectMap::const_iterator projIt;

			// iterate over projects
			for (projIt = projMap.begin(); projIt != projMap.end(); ++projIt)
			{
				const ProjectInfoPtr projInf = (*projIt).second;
				if (!projInf)
					continue;

				if (projInf->IsDeferredProject())
					++deferredProjCnt;

				platformSysIncludeList.AddUnique(projInf->GetPlatformIncludeDirs());
				if ((gShellAttr && gShellAttr->IsDevenv17OrHigher()) ||
				    (Psettings && Psettings->mForceExternalIncludeDirectories))
				{
					// [case: 145995]
					platformSysIncludeList.AddUnique(projInf->GetPlatformExternalIncludeDirs());
				}
				platformLibList.AddUnique(projInf->GetPlatformLibraryDirs());
				platformSrcList.AddUnique(projInf->GetPlatformSourceDirs());
			}
		}

		if (projectsLoaded)
		{
			CStringW tmpDirs;
			FileList::const_iterator it;

			for (it = platformSysIncludeList.begin(); it != platformSysIncludeList.end(); ++it)
				tmpDirs += ((*it).mFilename + L";");
			tmpDirs = ValidateDirList(tmpDirs);
			m_sysInclude = tmpDirs;

			// [case: 150119] find all relevant Unreal Engine include paths
			if (Psettings->mUnrealEngineCppSupport)
			{
				std::pair<int, int> ueVersionNumber = GlobalProject->GetUnrealEngineVersionAsNumber();
				if ((ueVersionNumber.first > 5 || ueVersionNumber.first == 5 && ueVersionNumber.second >= 3) /* >= 5.3 */ &&
					GlobalProject->GetUnrealBinaryVersionPath().contains(GlobalProject->GetUnrealEngineVersion()))
				{
					// build structure of dirs by going down until Public or Classes dir
					//		for \Engine\Source\Developer
					//		for \Engine\Source\Editor
					//		for \Engine\Source\Runtime
					// add those paths to m_sysInclude

					std::function<void(const CStringW&, std::list<CStringW>&)> SearchUEFoldersForIncPath = [&](const CStringW& path, std::list<CStringW>& includePathList) {
						for (const auto& entry : std::filesystem::directory_iterator(path.GetString()))
						{
							if (entry.is_directory())
							{
								WTString dirPath = entry.path().wstring().c_str();

								if (dirPath.EndsWith(L"\\Public") || dirPath.EndsWith(L"\\Classes"))
								{
									includePathList.emplace_back(dirPath.Wide());
								}
								else
								{
									SearchUEFoldersForIncPath(dirPath.Wide(), includePathList);
								}
							}
						}
					};
					
					CStringW pathToBinaryEngine = GlobalProject->GetUnrealBinaryVersionPath().at(GlobalProject->GetUnrealEngineVersion());
					std::list<CStringW> includePathList;
					
					CStringW path = pathToBinaryEngine + LR"(\Engine\Source\Developer)";
					SearchUEFoldersForIncPath(path, includePathList);

					path = pathToBinaryEngine + LR"(\Engine\Source\Editor)";
					SearchUEFoldersForIncPath(path, includePathList);

					path = pathToBinaryEngine + LR"(\Engine\Source\Runtime)";
					SearchUEFoldersForIncPath(path, includePathList);

					GlobalProject->SetUnrealIncludePathList(includePathList);
					
					std::wstring compactIncPath;
					for (const auto& includePath : includePathList)
					{
						compactIncPath += includePath + L";";
					}

					m_sysInclude += compactIncPath.c_str();
				}
			}

			tmpDirs.Empty();
			for (it = platformLibList.begin(); it != platformLibList.end(); ++it)
				tmpDirs += ((*it).mFilename + L";");
			tmpDirs = ValidateDirList(tmpDirs);
			m_importDirs = tmpDirs;

			tmpDirs.Empty();
			for (it = platformSrcList.begin(); it != platformSrcList.end(); ++it)
				tmpDirs += ((*it).mFilename + L";");
			tmpDirs = ValidateDirList(tmpDirs);
			m_srcDirs = tmpDirs;

			WriteDirsToRegistry();
			m_additionalIncludes.Empty();
		}
		else
		{
			m_sysInclude.Empty();
			m_solutionPrivateSysIncludes.Empty();
			m_srcDirs.Empty();
			m_importDirs.Empty();
			m_additionalIncludes.Empty();
		}

		if (m_sysInclude.IsEmpty() && projectsLoaded && GlobalProject->IsFolderBasedSolution())
		{
			// [case: 96662] [case: 102080] [case: 102486]
			// if no include paths, use default platform rather than Project defined (custom platform restored elsewhere
			// afterwards)
			const CString defPlatform(gShellAttr->GetDefaultPlatform());
			::strcpy(Psettings->m_platformIncludeKey, defPlatform);
			SetupPlatform(defPlatform);

			if (gShellAttr->IsDevenv15OrHigher())
			{
				// [case: 101742]
				// use settings from last solution loaded
				// see:#vcProjectEngineTypeLibFail
				m_sysInclude = GetRegValueW(HKEY_CURRENT_USER,
				                            (WTString(ID_RK_APP) + WTString("\\") + kProjectDefinedPlatform).c_str(),
				                            ID_RK_SYSTEMINCLUDE);
				if (!m_sysInclude.IsEmpty())
				{
					SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
					            ID_RK_SYSTEMINCLUDE, m_sysInclude);
					SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
					            ID_RK_MSDEV_INC_VALUE, m_sysInclude);
				}

				m_importDirs = GetRegValueW(HKEY_CURRENT_USER,
				                            (WTString(ID_RK_APP) + WTString("\\") + kProjectDefinedPlatform).c_str(),
				                            ID_RK_MSDEV_LIB_VALUE);
				if (!m_importDirs.IsEmpty())
					SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
					            ID_RK_MSDEV_LIB_VALUE, m_importDirs);

				m_srcDirs = GetRegValueW(HKEY_CURRENT_USER,
				                         (WTString(ID_RK_APP) + WTString("\\") + kProjectDefinedPlatform).c_str(),
				                         ID_RK_GOTOSRCDIRS);
				if (!m_srcDirs.IsEmpty())
				{
					SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
					            ID_RK_GOTOSRCDIRS, m_srcDirs);
					SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
					            ID_RK_MSDEV_SRC_VALUE, m_srcDirs);
				}
			}
		}
	}

	if (m_sysInclude.IsEmpty())
	{
		if (mPlatformKey != kProjectDefinedPlatform || projectsLoaded < 2 || deferredProjCnt)
		{
			// check previous project by looking in reg
			m_sysInclude = GetRegValueW(
			    HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(), ID_RK_SYSTEMINCLUDE);
			m_importDirs =
			    GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
			                 ID_RK_MSDEV_LIB_VALUE);
			m_srcDirs = GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
			                         ID_RK_GOTOSRCDIRS);

			if (m_sysInclude.IsEmpty())
			{
				const CString defPlatform(gShellAttr->GetDefaultPlatform());
				::strcpy(Psettings->m_platformIncludeKey, defPlatform);
				SetupPlatform(defPlatform);
			}
		}
	}

	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM, mPlatformKey.c_str());
	gDirIdStatusMap.clear();
}

void IncludeDirs::Init()
{
	if (!Psettings)
		Psettings = new CSettings;

	bool needInit = false;
	{
		RWLockReader l(mLock);
		if (m_sysInclude.IsEmpty() || mPlatformKey.IsEmpty() || mPlatformKey != Psettings->m_platformIncludeKey)
			needInit = true;
	}

	if (needInit)
	{
		DEFTIMER(IncDirInitTimer);
		LOG2("IncTok::Init");

		RWLockWriter l(mLock);
		if (IsCustom())
		{
			SetupCustom();
			return;
		}

		SetupPlatform(Psettings->m_platformIncludeKey);
		if (kProjectDefinedPlatform == mPlatformKey)
			return;

		if (m_sysInclude.IsEmpty())
		{
			m_sysInclude = ";";
			return;
		}

		CStringW customInc =
		    GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_SYSTEMINCLUDE);
		if (!customInc.GetLength())
		{
			// set default; never overwrite
			SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_SYSTEMINCLUDE,
			            m_sysInclude);
			SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_ADDITIONALINCLUDE,
			            m_additionalIncludes);
			SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_MSDEV_LIB_VALUE,
			            m_importDirs);
			SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_GOTOSRCDIRS, m_srcDirs);
		}
	}
}

static CStringW sPrevSysinclude;
static CStringW sPrevAddlInc;
static CStringW sPrevImportDir;
static CStringW sPrevSrcDirs;

void IncludeDirs::SetupCustom()
{
#ifdef _DEBUG
	_ASSERTE(mLock.GetWritingThread() == ::GetCurrentThreadId());
#endif
	CStringW sysInclude =
	    GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_SYSTEMINCLUDE);
	if (sPrevSysinclude != sysInclude || m_sysInclude.IsEmpty())
	{
		sPrevSysinclude = sysInclude;
		sysInclude = WTExpandEnvironmentStrings(sysInclude);
		sysInclude = ValidateDirList(sysInclude);
		m_sysInclude = sysInclude;
	}

	CStringW addlInc =
	    GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_ADDITIONALINCLUDE);
	if (sPrevAddlInc != addlInc || m_additionalIncludes.IsEmpty())
	{
		sPrevAddlInc = addlInc;
		addlInc = WTExpandEnvironmentStrings(addlInc);
		addlInc = ValidateDirList(addlInc);
		m_additionalIncludes = addlInc;
	}

	CStringW importDir =
	    GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_MSDEV_LIB_VALUE);
	if (sPrevImportDir != importDir || m_importDirs.IsEmpty())
	{
		sPrevImportDir = importDir;
		importDir = WTExpandEnvironmentStrings(importDir);
		importDir = ValidateDirList(importDir);
		m_importDirs = importDir;
	}

	CStringW srcDirs = GetRegValueW(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + kSlashCustom).c_str(), ID_RK_GOTOSRCDIRS);
	if (sPrevSrcDirs != srcDirs || m_srcDirs.IsEmpty())
	{
		sPrevSrcDirs = srcDirs;
		srcDirs = WTExpandEnvironmentStrings(srcDirs);
		srcDirs = ValidateDirList(srcDirs);
		m_srcDirs = srcDirs;
	}

	mPlatformKey = Psettings->m_platformIncludeKey;
	gDirIdStatusMap.clear();
}

void IncludeDirs::ProjectLoaded()
{
#if !defined(VA_CPPUNIT)
	if (!gShellAttr->IsDevenv10OrHigher())
		return;
#endif

	bool kWasCustom;

	// Reload project settings
	{
		RWLockWriter l(mLock);
		kWasCustom = IsCustom();
		mPlatformKey.Empty();
		SetupProjectPlatform(); // switches current platform to Project Defined
	}

	// Restore previous platform if it was Custom
	// [case: 58810] [case: 61583]
	if (kWasCustom && kCustom != Psettings->m_platformIncludeKey)
	{
		{
			RWLockWriter l(mLock);
			mPlatformKey = kCustom;
			::strcpy(Psettings->m_platformIncludeKey, kCustom.c_str());
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PLATFORM, kCustom.c_str());
		}

		// Reset might take the read lock (by way of Init()).
		// that will block if we own the write lock
		IncludeDirs::Reset();
	}
}

void IncludeDirs::UpdateRegistry()
{
#if !defined(VA_CPPUNIT)
	if (!gShellAttr->IsDevenv10OrHigher())
		return;
#endif

	RWLockReader l(mLock);
	WriteDirsToRegistry();
}

bool IncludeDirs::IsSolutionPrivateSysFile(const CStringW& file)
{
	IncludeDirs incTok;
	TokenW itok = incTok.getSolutionPrivateSysIncludeDirs();

	while (itok.more())
	{
		std::wstring_view pth = itok.read_sv(L";");
		if (!pth.empty() && _wcsnicmp(pth.data(), file, pth.size()) == 0)
			return true;
	}

	return false;
}

// The result is dependent upon the current set of include paths.
// In vs2010, a file might be in the sysdb but not considered a system file
// if the include path no longer applies to that file.
bool IncludeDirs::IsSystemFile(const CStringW& file)
{
	const CStringW dir(::Path(file));
	const UINT fid = gFileIdManager->GetFileId(dir);
	TriState st = IsSystemFile_CheckCache(fid);

	if (st == TriState::uninit)
	{
		IncludeDirs incTok;
		// consider using GetMassagedSystemDirs here
		CStringW dirs(incTok.getSysIncludes() + incTok.getSourceDirs());
		dirs.MakeLower();
		SemiColonDelimitedString incDirs(dirs);
		st = IsSystemFile_UpdateCache(file, fid, incDirs);
	}

	if (st == TriState::on)
		return true;

	return false;
}

// [case: 109019] overload for improved performance in loops
bool IncludeDirs::IsSystemFile(const CStringW& file, SemiColonDelimitedString& incDirs)
{
	const CStringW dir(::Path(file));
	const UINT fid = gFileIdManager->GetFileId(dir);
	TriState st = IsSystemFile_CheckCache(fid);

	if (st == TriState::uninit)
		st = IsSystemFile_UpdateCache(file, fid, incDirs);

	if (st == TriState::on)
		return true;

	return false;
}

// [case: 140857] overload for FindSym perf
bool IncludeDirs::IsSystemFile(UINT fid)
{
	TriState st = IsSystemFile_CheckCache(fid);

	if (st == TriState::uninit)
	{
		// check the dir
		const CStringW file(gFileIdManager->GetFile(fid));
		const CStringW dir(::Path(file));
		const UINT dirFid = gFileIdManager->GetFileId(dir);
		st = IsSystemFile_CheckCache(dirFid);

		if (st == TriState::uninit)
		{
			IncludeDirs incTok;
			// consider using GetMassagedSystemDirs here
			CStringW dirs(incTok.getSysIncludes() + incTok.getSourceDirs());
			dirs.MakeLower();
			SemiColonDelimitedString incDirs(dirs);
			st = IsSystemFile_UpdateCache(file, fid, incDirs);
		}

		// update cache for the file independently of the dir
		RWLockWriter l(mLock);
		gDirIdStatusMap[fid] = st;
	}

	if (st == TriState::on)
		return true;

	return false;
}

TriState IncludeDirs::IsSystemFile_CheckCache(const UINT fid)
{
	RWLockReader l(mLock);
	auto it = gDirIdStatusMap.find(fid);
	if (it != gDirIdStatusMap.end())
		return it->second;
	return TriState::uninit;
}

// case-insensitive compare that assumes:
// - the second parameter is already lower-case
// - the first parameter is longer than the second
BOOL StartsWithLenNC(LPCWSTR buf, LPCWSTR begStrLower, int begStrLen)
{
	_ASSERTE(buf && begStrLower);
	int i = 0;
	for (; towlower(buf[i]) == begStrLower[i] && i < begStrLen; i++)
		;
	return i == begStrLen;
}

TriState IncludeDirs::IsSystemFile_UpdateCache(const CStringW& file, const UINT fid, SemiColonDelimitedString& incDirs)
{
	TriState st = TriState::off;
	LPCWSTR pCurDir;
	int curDirLen;

	incDirs.Reset();
	while (incDirs.NextItem(pCurDir, curDirLen))
	{
		// only need to compare when the incdir len is shorter than the file len
		if (curDirLen < file.GetLength())
		{
			if (::StartsWithLenNC(file, pCurDir, curDirLen))
			{
				st = TriState::on;
#ifdef AVR_STUDIO
				CStringW lfile(file);
				lfile.MakeLower();
				if (lfile.Find(L"\\avr\\include\\avr\\") != -1 || lfile.Find(L"\\avr32\\include\\avr32\\") != -1)
					st = TriState::off; // Put AVR device files in project DB.
#endif                                  // AVR_STUDIO
				break;
			}
		}
	}

	// check sysdb for fileinfo
	// Not clear if this is necessary or even good...
	// I initially thought that vs2010 project define platforms would make
	// this necessary but seems like it could be bad in some circumstances
	// (a project that is local but is used as a sys include in another project).
	// 	if (checkSysDb && st == TriState::clear)
	// 	{
	// 		if (GetSysDic()->GetFileData(file))
	// 			st = TriState::set;
	// 	}

	RWLockWriter l(mLock);
	gDirIdStatusMap[fid] = st;
	return st;
}

void IncludeDirs::RemoveSysIncludeDir(CStringW dir)
{
	if (dir.GetLength() < 2)
		return;

	if (dir[dir.GetLength() - 1] != L';')
		dir += L';';

	RWLockWriter l(mLock);
	CStringW tmp(m_sysInclude);
	tmp.MakeLower();
	dir.MakeLower();
	int pos = tmp.Find(dir);
	if (-1 == pos)
		return;

	tmp = m_sysInclude.Left(pos);
	tmp += m_sysInclude.Mid(pos + dir.GetLength());
	m_sysInclude = tmp;
}

void IncludeDirs::RemoveSourceDir(CStringW dir)
{
	if (dir.GetLength() < 2)
		return;

	if (dir[dir.GetLength() - 1] != L';')
		dir += L';';

	RWLockWriter l(mLock);
	CStringW tmp(m_srcDirs);
	tmp.MakeLower();
	dir.MakeLower();
	int pos = tmp.Find(dir);
	if (-1 == pos)
		return;

	tmp = m_srcDirs.Left(pos);
	tmp += m_srcDirs.Mid(pos + dir.GetLength());
	m_srcDirs = tmp;
}

void IncludeDirs::AddSysIncludeDir(CStringW dir)
{
	if (dir.GetLength() < 2)
		return;

	if (dir[dir.GetLength() - 1] != L';')
		dir += L';';

	if (::wcschr(L"\\/", dir[dir.GetLength() - 2]))
		dir = dir.Left(dir.GetLength() - 2) + L";";

	RWLockWriter l(mLock);
	CStringW tmp(m_sysInclude);
	CStringW tmp2(dir);
	tmp.MakeLower();
	tmp2.MakeLower();
	int pos = tmp.Find(tmp2);
	if (-1 != pos)
		return;

	tmp = m_sysInclude;
	tmp += dir;
	m_sysInclude = tmp;
}

void IncludeDirs::AddSolutionPrivateSysIncludeDir(CStringW dir)
{
	if (dir.GetLength() < 2)
		return;

	if (dir[dir.GetLength() - 1] != L';')
		dir += L';';

	if (::wcschr(L"\\/", dir[dir.GetLength() - 2]))
		dir = dir.Left(dir.GetLength() - 2) + L";";

	RWLockWriter l(mLock);
	CStringW tmp(m_solutionPrivateSysIncludes);
	CStringW tmp2(dir);
	tmp.MakeLower();
	tmp2.MakeLower();
	int pos = tmp.Find(tmp2);
	if (-1 != pos)
		return;

	tmp = m_solutionPrivateSysIncludes;
	tmp += dir;
	m_solutionPrivateSysIncludes = tmp;
}

void IncludeDirs::AddSourceDir(CStringW dir, bool checkIncDirs /*= false*/)
{
	if (dir.GetLength() < 2)
		return;

	if (dir[dir.GetLength() - 1] != L';')
		dir += L';';

	if (::wcschr(L"\\/", dir[dir.GetLength() - 2]))
		dir = dir.Left(dir.GetLength() - 2) + L";";

	RWLockWriter l(mLock);
	CStringW tmp(m_srcDirs);
	CStringW tmp2(dir);
	tmp.MakeLower();
	tmp2.MakeLower();
	int pos = tmp.Find(tmp2);
	if (-1 != pos)
		return;

	if (checkIncDirs)
	{
		// don't add to source if already in sys include
		tmp = m_sysInclude;
		tmp.MakeLower();
		pos = tmp.Find(tmp2);
		if (-1 != pos)
			return;
	}

	tmp = m_srcDirs;
	tmp += dir;
	m_srcDirs = tmp;
}

void DumpList(CString msg, CStringW semiColonDelimList)
{
	vCatLog("Environment.Directories", "%s:\n", (const char*)msg);
	TokenW t = semiColonDelimList;
	while (t.more())
	{
		const CStringW curDir(t.read(L";"));
		if (!curDir.IsEmpty())
			vCatLog("Environment.Directories", "  %s\n", (const char*)CString(curDir));
	}
}

void IncludeDirs::DumpToLog() const
{
	if (!g_loggingEnabled)
		return;

	::DumpList("Platform Includes", getSysIncludes());
	const CStringW tmp(getSolutionPrivateSysIncludeDirs());
	if (!tmp.IsEmpty())
		::DumpList("Solution Private System Includes", tmp);
	::DumpList("Platform Source", getSourceDirs());
	::DumpList("Platform Addl Includes", getAdditionalIncludes());
}

CStringW IncludeDirs::getSysIncludes() const
{
	RWLockReader l(mLock);
	CStringW tmp(m_sysInclude);
	return tmp;
}

CStringW IncludeDirs::getSolutionPrivateSysIncludeDirs() const
{
	RWLockReader l(mLock);
	CStringW tmp(m_solutionPrivateSysIncludes);
	return tmp;
}

CStringW IncludeDirs::getAdditionalIncludes() const
{
	RWLockReader l(mLock);
	CStringW tmp(m_additionalIncludes);
	return tmp;
}

CStringW IncludeDirs::getImportDirs() const
{
	RWLockReader l(mLock);
	CStringW tmp(m_importDirs);
	return tmp;
}

CStringW IncludeDirs::getSourceDirs() const
{
	RWLockReader l(mLock);
	CStringW tmp(m_srcDirs);
	return tmp;
}

void IncludeDirs::WriteDirsToRegistry()
{
	if ((!m_sysInclude.IsEmpty() && m_sysInclude != L";") || mPlatformKey == kProjectDefinedPlatform)
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
		            ID_RK_SYSTEMINCLUDE, m_sysInclude);

	if ((!m_sysInclude.IsEmpty() && m_sysInclude != L";") ||
	    mPlatformKey == kProjectDefinedPlatform) // this is not a typo; make setreg dependent upon includes
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(),
		            ID_RK_MSDEV_LIB_VALUE, m_importDirs);

	if ((!m_sysInclude.IsEmpty() && m_sysInclude != L";") ||
	    mPlatformKey == kProjectDefinedPlatform) // this is not a typo; make setreg dependent upon includes
		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + mPlatformKey).c_str(), ID_RK_GOTOSRCDIRS,
		            m_srcDirs);
}

CStringW WTExpandEnvironmentStrings2(LPCWSTR inputStr)
{
	DEFTIMERNOTE(WTEES2, CString(inputStr));
	vCatLog("Environment.Directories", "WTExpandEnvironmentStrings2 %s", (const char*)CString(inputStr));
	CStringW outStr, inStr(inputStr);
	inStr.Replace(L"\\\\", L"\\");
	if (inStr.FindOneOf(L"$%") < 0)
		return inStr;

	if (inStr.GetLength())
	{
#if !defined(RAD_STUDIO)
		// in VS, pass $(foo) off to the IDE (VS only)
		if (gShellAttr && gShellAttr->IsDevenv() && inStr.Find(L'$') != -1)
		{
			const CStringW tmp(::VcEngineEvaluate(inStr));
			if (tmp.GetLength())
			{
				// [case: 57973] watch out for simple removal of vars leaving "\\" in path
				// but allow if inStr came in with "\\" to begin with
				const int pos = tmp.Find(L"\\\\");
				if (-1 == pos || -1 != inStr.Find(L"\\\\"))
				{
					if (tmp.FindOneOf(L"$%") == -1)
					{
						vLog1("WTEES4: %s\n", (const char*)CString(tmp));
						return tmp;
					}
					inStr = tmp;
				}
			}
		}
#endif

		auto ConvertToPercentEnvVar = [&inStr](const WCHAR opener[2], const WCHAR closer) 
		{
			for (int startPos = -1, endPos = -1;
			     (startPos = inStr.Find(opener)) != -1 && (endPos = inStr.Find(closer, startPos)) != -1 && startPos < endPos;)
			{
				// copy everything up to the first opener "$("
				CStringW newStr = inStr.Mid(0, startPos) + L"%";
				// copy the EnvVar part of $(EnvVar)
				newStr += inStr.Mid(startPos + 2, endPos - startPos - 2) + L"%";
				// copy everything after the closer ')'
				newStr += inStr.Mid(endPos + 1);
				inStr = newStr;
			}
		};

		// convert $(EnvVar) to %EnvVar%
		ConvertToPercentEnvVar(L"$(", L')');
		// convert ${EnvVar} to %EnvVar%
		ConvertToPercentEnvVar(L"${", L'}');

		// convert %EnvVar% to a real path
		if (wcschr(inStr, L'%'))
		{
			// inStr has a % in it - convert it
			const int kBufLen = 2048;
			const std::unique_ptr<WCHAR[]> tmpVec(new WCHAR[kBufLen]);
			WCHAR* tmp = &tmpVec[0];
			// get size needed
			DWORD err = ExpandEnvironmentStringsW(inStr, tmp, kBufLen);
			if (err)
			{
				outStr = tmp;
				vCatLog("Environment.Directories", "WTEES11: %s\n", (const char*)CString(inputStr));
				vCatLog("Environment.Directories", "WTEES21: %s\n", (const char*)CString(outStr));
				if (g_loggingEnabled && !IsDir(outStr))
				{
					vLog("ERROR: WTEES::ExpandEnvironmentStrings2 %s(%08lx)", (const char*)CString(outStr), err);
				}
				return outStr;
			}
			else
			{
				err = GetLastError();
				vLog("ERROR: WTEES::ExpandEnvironmentStrings2 %ld(%08lx)", err, err);
				ASSERT(FALSE);
			}
		}
	}
	vLog1("WTEES3: %s\n", (const char*)CString(inputStr));
	outStr = inputStr;
	return outStr;
}

CStringW WTExpandEnvironmentStrings(LPCWSTR inputStr)
{
	// ExpandEnvironmentStrings is limited to 32k, lets expand each segment separately...
	if (!wcschr(inputStr, L'$') && !wcschr(inputStr, L'%') && !wcschr(inputStr, L'&'))
		return CStringW(inputStr);
	CStringW isW = inputStr;
	vLog("WTEES1: %s", (const char*)CString(inputStr));
#if !defined(RAD_STUDIO)
	static bool once = true;
	if (once && gShellAttr && gShellAttr->SupportsNetFrameworkDevelopment() && 
		(!gShellAttr->IsDevenv16u10OrHigher() || !IsInOpenFolderMode())) // [case: 146429]
	{
		once = false;
		CStringW s =
		    GetRegValueW(gShellAttr->GetSetupRootKey(), gShellAttr->GetSetupKeyName() + _T("VS"), "ProductDir");
		if (s.GetLength())
		{
			vLog("ProductDir: %s", (const char*)CString(s));
			if (!wcschr(L"\\/", s[s.GetLength() - 1]))
				s += L'\\';
			s.MakeLower();
			SetEnvironmentVariableW(L"VSInstallDir", s);
			SetEnvironmentVariableW(L"MsVCDir", s);
		}
		else
			vLog("ProductDir: (null)");

		s = GetRegValueW(gShellAttr->GetSetupRootKey(), gShellAttr->GetSetupKeyName() + _T("VC"), "ProductDir");
		if (s.GetLength())
		{
			vLog("VcProductDir: %s", (const char*)CString(s));
			if (!wcschr(L"\\/", s[s.GetLength() - 1]))
				s += L'\\';
			s.MakeLower();
			SetEnvironmentVariableW(L"VCInstallDir", s);
		}
		else
			vLog("VcProductDir: (null)");

		s = GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\.NETFramework", "sdkInstallRootv1.1");
		if (s.GetLength() && !wcschr(L"\\/", s[s.GetLength() - 1]))
			s += L'\\';
		if (!s.GetLength())
		{
			s = GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\.NETFramework", "sdkInstallRoot");
			if (s.GetLength() && !wcschr(L"\\/", s[s.GetLength() - 1]))
				s += L'\\';
		}
		vLog("frameworksdkdir: %s", (const char*)CString(s));
		s.MakeLower();
		SetEnvironmentVariableW(L"frameworksdkdir", s);

		s = GetRegValueW(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\.NETFramework", "InstallRoot");
		if (s.GetLength() && !wcschr(L"\\/", s[s.GetLength() - 1]))
			s += L'\\';
		vLog("frameworkdir: %s", (const char*)CString(s));
		s.MakeLower();
		SetEnvironmentVariableW(L"frameworkdir", s);
	}

	// john bergman has &quote$(dir)\\file.h&quot in many of his paths?
	isW.Replace(L"&quot", L";");
#endif

	CStringW ostr;
	SemiColonDelimitedString t = isW;
	LPCWSTR pCur;
	int curLen;
	if (t.NextItem(pCur, curLen))
	{
		const CStringW sc(";");
		if (pCur[curLen] == L';')
		{
			LPWSTR pWriteToConst = (LPWSTR)&pCur[curLen];
			*pWriteToConst = '\0';
		}
		ostr = WTExpandEnvironmentStrings2(pCur);
		while (t.NextItem(pCur, curLen))
		{
			if (pCur[curLen] == L';')
			{
				LPWSTR pWriteToConst = (LPWSTR)&pCur[curLen];
				*pWriteToConst = '\0';
			}
			ostr += sc + WTExpandEnvironmentStrings2(pCur);
		}
	}

	// get rid of extra \'s
	TokenW tt = ostr;
	tt.Replace(L"/", L"\\");
	tt.Replace(L"\\\\", L"\\");
	tt.Replace(L"\\\\", L"\\");
	ostr = tt.Str();
	vLog("WTEES3: %s", (const char*)CString(ostr));
	return ostr;
}

// massages dirs list reported by IncludeDirs for OFIS performance

void GetMassagedSystemDirs(SemiColonDelimitedString& incDirs)
{
	LogElapsedTime let("GetSystemDirs", 500);
	SemiColonDelimitedString tmpWalker;
	IncludeDirs incTok;
	bool addedUe = false;
	CStringW incs(incTok.getSysIncludes()), srcs(incTok.getSourceDirs()), massagedDirs;
	incs.MakeLower();
	srcs.MakeLower();

	auto walker = [&]() {
		CStringW curDir;
		while (tmpWalker.NextItem(curDir))
		{
			bool checkForEpic = false;
			if (kWinProgFilesDirX86.GetLength() && (curDir.GetLength() > kWinProgFilesDirX86.GetLength()))
			{
				if (::StartsWithLenNC(kWinProgFilesDirX86, curDir, kWinProgFilesDirX86.GetLength()))
					checkForEpic = true;
			}
			if (!checkForEpic && kWinProgFilesDir.GetLength() && (curDir.GetLength() > kWinProgFilesDir.GetLength()))
			{
				if (::StartsWithLenNC(kWinProgFilesDir, curDir, kWinProgFilesDir.GetLength()))
					checkForEpic = true;
			}

			if (checkForEpic && -1 != curDir.Find(L"\\epic games\\"))
			{
				int pos = curDir.Find(L"\\engine\\");
				if (-1 != pos)
				{
					if (addedUe)
						continue;

					addedUe = true;
					curDir = curDir.Left(pos + 8);
				}
			}

			massagedDirs += curDir + L";";
		}
	};

	tmpWalker.Load(incs);
	walker();

	if (massagedDirs.GetLength() < 300000)
	{
		tmpWalker.Load(srcs);
		walker();
	}
	else
	{
		// [case: 109019] don't add system src dirs if include dirs is already crazy big
		vLog("WARN: OFIS::Populate skipping sys src dirs %d %d\n", incs.GetLength(), srcs.GetLength());
	}

	incDirs.Load(massagedDirs);
}
