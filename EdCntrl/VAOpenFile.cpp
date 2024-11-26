// VAOpenFile.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "VAOpenFile.h"
#include "project.h"
#include "edcnt.h"
#include "expansion.h"
#include "VAFileView.h"
#include "FilterableSet.h"
#include "file.h"
#include "TempSettingOverride.h"
#include "FileTypes.h"
#include "Settings.h"
#include "DevShellAttributes.h"
#include "WindowUtils.h"
#include "ProjectInfo.h"
#include "MenuXP\Tools.h"
#include "Registry.h"
#include "RegKeys.h"
#include "StringUtils.h"
#include "VAThemeDraw.h"
#include <regex>
#include "FileId.h"
#include "includesDb.h"
#include "LogElapsedTime.h"
#include "GotoDlg1.h"
#include "SemiColonDelimitedString.h"
#include "VAAutomation.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CStringW sOfisFilterString; // filter combined from contents of two edit controls
static CStringW
    sOfisFilterStringPartial; // filter of the first edit control that is always visible and not retained in registry
static int sSortingColumn = 0;
static bool sSortReverse = false;
static bool sFilterDuplicates = true;
static int sDisplayProjectColumn = 0;

#define ID_TOGGLE_DUPE_DISPLAY 0x201
#define ID_TOGGLE_CASE_SENSITIVITY 0x202
#define ID_COPY_FILENAME 0x203
#define ID_COPY_FULL_PATH 0x204
#define ID_OPEN_CONTAINING_FOLDER 0x205
#define ID_TOGGLE_TOOLTIPS 0x206
#define ID_TOGGLE_IncludeSolution 0x207
#define ID_TOGGLE_IncludePrivateSystem 0x208
#define ID_TOGGLE_IncludeWindowList 0x209
#define ID_TOGGLE_IncludeSystem 0x20a
#define ID_TOGGLE_IncludeExternal 0x20b
#define ID_TOGGLE_AugmentSolutionFilesByDiskSearch 0x20c
#define ID_TOGGLE_IncludeHiddenExtensions 0x20d
#define ID_TOGGLE_DisplayPersistentFilter 0x20e
#define ID_TOGGLE_ApplyPersistentFilter 0x20f
#define ID_SELECT_ALL 0x210

#define NUGET_REPOSITORY_MESSAGE_NAME "OfisNuGetRespository"

enum VOF_TimerID
{
	VOF_TimerID_Loading,
	VOF_TimerID_NugetMsg,
	VOF_TimerID_Filter,
	VOF_TimerID_ChangeTitleToLoading
};

CStringW StripFilterTextOfLineNumber(CStringW strippedFilter)
{
	// [case: 32171]
	std::wstring stdStrippedFilter((LPCWSTR)strippedFilter);
	// allow ":xx" anywhere; only allow "(xx, yy)" when it is of the form ".aa(xx, yy)"
	// regex duplicated in VAOpenFile::OnOK()
	std::wregex lnNumRegex(L":(\\d+)(\\s|$)|(\\.\\S+)\\s*\\((\\d+)(\\,\\s*(\\d+))*\\)(\\s|$)",
	                       std::regex::ECMAScript | std::regex::optimize);
	stdStrippedFilter = std::regex_replace(stdStrippedFilter, lnNumRegex, L"$3");
	strippedFilter = stdStrippedFilter.c_str();
	strippedFilter.Trim();
	return strippedFilter;
}

bool HaveSameExclusionFilters(CStringW f1, CStringW f2)
{
	WideStrVector set1, set2;
	WtStrSplitW(f1, set1, L" ");
	WtStrSplitW(f2, set2, L" ");

	// remove from vectors items that don't contain '-'
	WideStrVector::iterator it1, it2;
	for (it1 = set1.begin(); it1 != set1.end();)
	{
		if ((*it1).Find(L'-') == -1)
			it1 = set1.erase(it1);
		else
			++it1;
	}

	for (it2 = set2.begin(); it2 != set2.end();)
	{
		if ((*it2).Find(L'-') == -1)
			it2 = set2.erase(it2);
		else
			++it2;
	}

	if (set1.size() != set2.size())
	{
		// different number of exclusion filters
		return false;
	}

	it1 = set1.begin();
	it2 = set2.begin();
	for (; it1 != set1.end(); ++it1, ++it2)
	{
		if ((*it1) != (*it2))
		{
			// this exclusion filter doesn't match
			return false;
		}
	}

	return true;
}

#include "VA_MRUs.h"

typedef FilterableSet<wchar_t, FileData*> FilterableFileData;

class FileDataList : public FilterableFileData
{
	int mSortColumn;
	bool mReverseSort;
	using FileIdSet = std::set<UINT>;
	FileIdSet mAllFileIds; // [case: 111851] fileId cache to speed up AugmentSolutionFiles

  public:
	FileDataList() : FilterableFileData(), mSortColumn(sSortingColumn), mReverseSort(sSortReverse)
	{
	}

	virtual ~FileDataList()
	{
		Empty();
	}

	virtual int GetSuggestionIdx() const
	{
		AutoLockCs l(mCs);
		if (Psettings->mSelectRecentItemsInNavigationDialogs && g_VA_MRUs &&
		    mFilteredSetCount < 1000) // prevent slowdown of large lists
		{
			std::list<CStringW>::iterator it;
			for (it = g_VA_MRUs->m_FIS.begin(); it != g_VA_MRUs->m_FIS.end(); it++)
			{
				CStringW file = *it;
				for (int n = 0; n < mFilteredSetCount; n++)
				{
					if (mFilteredSet[n]->mFilename == file)
					{
						//						if (g_currentEdCnt && g_currentEdCnt->FileName() == file)
						//							continue;
						return n;
					}
				}
			}
		}
		return __super::GetSuggestionIdx();
	}

	void AddItem(FileData* item)
	{
		AutoLockCs l(mCs);
		mAllFileIds.insert(item->mFileId);
		__super::AddItem(item);
	}

	void Empty() override
	{
		AutoLockCs l(mCs);
		mAllFileIds.clear();
		for (int n = 0; n < mAllItemCount; n++)
			delete GetOrgAt(n);
		FilterableFileData::Empty();
	}

	void HandleDuplicates()
	{
		AutoLockCs l(mCs);
		if (!mAllItemCount)
			return;

		int j = 0;
		const int count = mAllItemCount;
		for (int i = 1; i < count; i++)
		{
			if (GetOrgAt(j)->mFileId == GetOrgAt(i)->mFileId)
			{
				CStringW prjLower(mAllItems[i]->mProjectBaseName);
				prjLower.MakeLower();
				if (-1 != prjLower.Find(L".shared"))
				{
					// [case: 80212] put .shared projects first in list
					mAllItems[j]->mProjectBaseName =
					    mAllItems[i]->mProjectBaseName + L"; " + mAllItems[j]->mProjectBaseName;
				}
				else
					mAllItems[j]->mProjectBaseName += L"; " + mAllItems[i]->mProjectBaseName;

				delete mAllItems[i];
			}
			else
				mAllItems[++j] = mAllItems[i];
		}

		_ASSERTE(j + 1 <= mCapacity);
		mAllItemCount = j + 1;
	}

	bool Contains(UINT fileId) const
	{
		AutoLockCs l(mCs);
		return Contains_NoLock(fileId);
	}

	// only use this if you know that access is single-threaded, read-only multi-threaded or protected by lock
	bool Contains_NoLock(UINT fileId) const
	{
		return mAllFileIds.find(fileId) != mAllFileIds.end();
	}

	bool fullPathSearch = false;

  protected:
	virtual std::wstring_view ItemToText(FileData* item, std::wstring &storage) const override
	{
		const wchar_t* begin = item->mFilename.GetString();
		const wchar_t* end = item->mFilename.GetString() + item->mFilename.GetLength();
		if (!fullPathSearch)
		{
			begin = ::GetBaseName(begin);
			assert((begin >= item->mFilename.GetString()) && (begin < end));
		}
		return std::wstring_view{begin, end};
	}
	virtual bool IsItemToTextMultiThreadSafe() const override
	{
		return true;
	}
};

static bool SortFunc(FileData*& v1, FileData*& v2)
{
	FileData *n1, *n2;
	int equal = 0;

	n1 = v1;
	n2 = v2;

	if (n1 == NULL || n2 == NULL)
		return 0;

	const int column = sDisplayProjectColumn ? sSortingColumn : (sSortingColumn ? sSortingColumn + 1 : 0);
	switch (column)
	{
	case 0:
		equal = wcscmp(GetBaseName(n1->mFilenameLower), GetBaseName(n2->mFilenameLower));
		if (!equal)
		{
			equal = wcscmp(n1->mFilenameLower, n2->mFilenameLower);
			if (!equal)
			{
				// [case:80212] if same file exists in multiple projects, sort on project
				equal = _wcsicmp(n1->mProjectBaseName, n2->mProjectBaseName);
			}
		}
		break;
	case 1:
		equal = _wcsicmp(n1->mProjectBaseName, n2->mProjectBaseName);
		if (!equal)
		{
			// sort on column 0 for files in same project
			equal = wcscmp(GetBaseName(n1->mFilenameLower), GetBaseName(n2->mFilenameLower));
			if (!equal)
				equal = wcscmp(n1->mFilenameLower, n2->mFilenameLower);
		}
		break;
	case 2:
		equal = wcscmp(n1->mFilenameLower, n2->mFilenameLower);
		if (!equal)
		{
			// [case:80212] if same file exists in multiple projects, sort on project
			equal = _wcsicmp(n1->mProjectBaseName, n2->mProjectBaseName);
		}
		break;
	case 3:
		if (!n1->mFiletime.GetTime())
		{
			FILETIME ft;
			if (GetFTime(n1->mFilename, &ft))
				n1->mFiletime = ft;
		}
		if (!n2->mFiletime.GetTime())
		{
			FILETIME ft;
			if (GetFTime(n2->mFilename, &ft))
				n2->mFiletime = ft;
		}
		equal = (n1->mFiletime == n2->mFiletime) ? 0 : (n1->mFiletime < n2->mFiletime) ? 1 : -1;
		if (!equal)
		{
			// if timestamps are same, then sort on file
			equal = wcscmp(n1->mFilenameLower, n2->mFilenameLower);
		}
		break;
	}

	if (sSortReverse)
		equal *= -1;

	return equal < 0;
}

/////////////////////////////////////////////////////////////////////////////
//	File dialog

void VAOpenFileDlg()
{
	if (GlobalProject->IsBusy())
	{
		// if solution is loading, then we don't have enough information to hide
		// project column, so display by default
		sDisplayProjectColumn = true;
	}
	else
	{
		RWLockReader lck;
		const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
		sDisplayProjectColumn = projMap.size() > 1;

		if (!sDisplayProjectColumn)
		{
			// [case: 1029]
			// always create project column since we don't destroy and recreate
			// the dialog when user toggles 'show only files in current solution'
			sDisplayProjectColumn = true;
		}

		if (!sDisplayProjectColumn)
		{
			RWLockReader lck2;
			const FileList& hdrs = GlobalProject->GetSolutionPrivateSystemHeaders(lck2);
			if (hdrs.size())
			{
				// [case: 79296]
				// display project column even if there is only one project if there
				// are nuget package headers
				sDisplayProjectColumn = true;
			}
		}
	}

	TempSettingOverride<bool> ov(&Psettings->m_bEnhColorTooltips);
	VAOpenFile dlg;
	dlg.DoModal();
}

VAOpenFile::VAOpenFile(CWnd* pParent /*=NULL*/)
    : FindInWkspcDlg(sDisplayProjectColumn ? "FisDlgA" : "FisDlgB", VAOpenFile::IDD, pParent, true, true),
      mFileData(new FileDataList), mLoader(OfisLoader, this, "OfisLoader", false, false)
{
	static const CString valueName{/*this->GetBaseRegName() +*/ "FilterDuplicates"};
	sFilterDuplicates = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, valueName, sFilterDuplicates);
	SetHelpTopic("dlgOfis");
	//{{AFX_DATA_INIT(VAOpenFile)
	//}}AFX_DATA_INIT
}

VAOpenFile::~VAOpenFile()
{
	delete mFileData;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VAOpenFile, FindInWkspcDlg)
//{{AFX_MSG_MAP(VAOpenFile)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_COMMAND(ID_TOGGLE_DUPE_DISPLAY, OnToggleDupeDisplay)
ON_COMMAND(ID_TOGGLE_CASE_SENSITIVITY, OnToggleCaseSensitivity)
ON_COMMAND(ID_TOGGLE_TOOLTIPS, OnToggleTooltips)
ON_COMMAND(ID_COPY_FILENAME, OnCopyFilename)
ON_COMMAND(ID_COPY_FULL_PATH, OnCopyFullPath)
ON_COMMAND(ID_OPEN_CONTAINING_FOLDER, OnOpenContainingFolder)
ON_COMMAND(ID_TOGGLE_IncludeSolution, OnToggleIncludeSolution)
ON_COMMAND(ID_TOGGLE_IncludePrivateSystem, OnToggleIncludePrivateSystem)
ON_COMMAND(ID_TOGGLE_IncludeWindowList, OnToggleIncludeWindowList)
ON_COMMAND(ID_TOGGLE_IncludeSystem, OnToggleIncludeSystem)
ON_COMMAND(ID_TOGGLE_IncludeExternal, OnToggleIncludeExternal)
ON_COMMAND(ID_TOGGLE_AugmentSolutionFilesByDiskSearch, OnToggleAugmentSolution)
ON_COMMAND(ID_TOGGLE_IncludeHiddenExtensions, OnToggleIncludeHiddenExtensions)
ON_COMMAND(ID_TOGGLE_DisplayPersistentFilter, OnToggleDisplayPersistentFilter)
ON_COMMAND(ID_TOGGLE_ApplyPersistentFilter, OnToggleApplyPersistentFilter)
ON_COMMAND(ID_SELECT_ALL, OnSelectAll)
ON_EN_CHANGE(IDC_FILTEREDIT2, OnChangeFilterEdit)
ON_WM_DESTROY()
ON_NOTIFY(HDN_ITEMCLICKW, 0, OnClickHeaderItem)
ON_NOTIFY(LVN_GETDISPINFO, IDC_FILELIST, OnGetdispinfo)
// xxx_sean unicode: this isn't being invoked...
ON_NOTIFY(LVN_GETDISPINFOW, IDC_FILELIST, OnGetdispinfoW)
ON_WM_TIMER()
ON_WM_SHOWWINDOW()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

IMPLEMENT_MENUXP(VAOpenFile, FindInWkspcDlg);
/////////////////////////////////////////////////////////////////////////////
// VAOpenFile message handlers

void VAOpenFile::OnOK()
{
	StopThreadAndWait();
	FindInWkspcDlg::OnOK();
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	CStringW file;
	const uint selCnt = mFilterList.GetSelectedCount();
	int lineNum = 0;
	int columnNum = 0;
	if (selCnt < 2)
	{
		// [case: 32171]
		// [case: 112737] added support for column (actually char offset from start of line)
		std::wstring stdStrippedFilter((LPCWSTR)sOfisFilterString);
		// regex duplicated in StripFilterTextOfLineNumber()
		std::wregex lnNumRegex(L":(\\d+)(\\s|$)|(\\.\\S+)\\s*\\((\\d+)(\\,\\s*(\\d+))*\\)(\\s|$)",
		                       std::regex::ECMAScript | std::regex::optimize);

		std::wsmatch sm;
		std::regex_search(stdStrippedFilter, sm, lnNumRegex);

		if (sm.size() == 8)
		{
			if (sm[1].matched)
			{
				lineNum = _wtoi(std::wstring(sm[1]).c_str());
			}
			else if (sm[4].matched)
			{
				lineNum = _wtoi(std::wstring(sm[4]).c_str());

				if (sm[6].matched)
					columnNum = _wtoi(std::wstring(sm[6]).c_str());
			}
		}

		if (selCnt == 0)
		{
			// [case: 32171]
			const CStringW strippedFilter(::StripFilterTextOfLineNumber(sOfisFilterString));
			if (strippedFilter.GetLength() > 3 && strippedFilter[1] == L':' && ::IsFile(strippedFilter))
			{
				::VAProjectAddFileMRU(strippedFilter, mruFileSelected);
				::DelayFileOpenLineAndChar(strippedFilter, lineNum, columnNum, nullptr);
			}
			else
			{
				// maybe case 108196 ?
				vLog("ERROR: OFIS::OnOk selCnt == 0, match failed: %s\n", (LPCTSTR)CString(strippedFilter));
			}

			return;
		}
	}

	if (!p)
	{
		vLog("ERROR: OFIS::OnOk !p\n");
		return;
	}

	for (uint n = 0; n < selCnt; n++)
	{
		FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);

		mFilterList.GetNextSelectedItem(p);

		if (ptr)
		{
			if (n < 10)
			{
				// only do this for the first 10 (?) items so that MRU lists don't
				// get blown out with likely one time mass file open
				VAProjectAddFileMRU(ptr->mFilename, mruFileSelected);
			}
			EdCntPtr ptrEdCnt = DelayFileOpenLineAndChar(ptr->mFilename, lineNum, columnNum, NULL);

			if (!ptrEdCnt)
			{
				if (!IsFile(ptr->mFilename))
				{
					// [case: 117483]
					// trying to open deleted file
					CString msgTxt;
					CString__FormatA(msgTxt, _T("Failed to open %s because the file does not exist."),
					                 (LPCTSTR)CString(ptr->mFilename));
					WtMessageBox(msgTxt, IDS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
				}
				else
				{
					vLog("ERROR: OFIS::OnOk DelayLoad failed: %s\n", (LPCTSTR)CString(ptr->mFilename));
				}
			}
		}
	}
}

bool EnumerateProjectFiles(std::function<void(const FileInfo&, const CStringW&)> AddFile, LONG* generation)
{
	LogElapsedTime let("OFIS::EnumerateProjectFiles", 500);
	if (generation)
	{
		LONG current_generation = GlobalProject->GetFilesListGeneration();
		if (current_generation <= *generation)
			return false;
		*generation = current_generation;
	}

	RWLockReader lck;
	const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
	for (Project::ProjectMap::const_iterator projIt = projMap.begin(); projIt != projMap.end(); ++projIt)
	{
		const ProjectInfoPtr projInf = (*projIt).second;
		if (projInf)
		{
			CStringW projectFile(projInf->GetProjectFile());
			CStringW currentProject(::GetBaseNameNoExt(projectFile));
			const bool isCmake = !currentProject.CompareNoCase(L"CMakeLists");
			if (!currentProject.CompareNoCase(L"CppProperties") || isCmake)
			{
				// [case: 102080] special handling of CppProperties.json
				// [case: 102486] and cmake
				currentProject.Empty();
				projectFile = Path(projectFile);
			}

			if (currentProject.IsEmpty())
			{
				// [case: 63095]
				if (projectFile.GetLength() && projectFile[projectFile.GetLength() - 1] == L'\\')
					projectFile = projectFile.Left(projectFile.GetLength() - 1);
				currentProject = ::Basename(projectFile);
				if (currentProject.IsEmpty())
				{
					// [case: 19496] directory instead of file
					currentProject = projectFile;
				}
			}

			if (projInf->IsDeferredProject())
				currentProject += " (deferred)";

			const FileList& projFiles = projInf->GetFiles();
			for (const auto& projFile : projFiles)
			{
				if (isCmake && -1 != projFile.mFilenameLower.Find(L"\\build\\"))
				{
					if (-1 != projFile.mFilenameLower.Find(L"\\out\\build\\") ||
					    -1 != projFile.mFilenameLower.Find(L"\\build\\x64-debug") ||
					    -1 != projFile.mFilenameLower.Find(L"\\build\\x64-release") ||
					    -1 != projFile.mFilenameLower.Find(L"\\build\\x86-debug") ||
					    -1 != projFile.mFilenameLower.Find(L"\\build\\x86-release"))
					{
						// hide cmake crap by default, will show up if user enables all files in soln directories
						continue;
					}
				}

				AddFile(projFile, currentProject);
			}
		}
		else
			_ASSERTE(!"project map iterator has NULL proj info");
	}
	return true;
}

void VAOpenFile::AugmentSolutionFiles()
{
	// [case: 106017]
	LogElapsedTime let("OFIS::AugmentSolutionFiles total", 500);
	FileList dirs;

	// solution
	CStringW rootDir(GlobalProject->SolutionFile());
	if (rootDir.IsEmpty())
		return;
	if (::IsFile(rootDir))
		rootDir = ::Path(rootDir);
	if (rootDir.GetLength() <= 3)
		return; // watch out for "c:\" or "//" or '\\'
	if (rootDir[rootDir.GetLength() - 1] == '\\' || rootDir[rootDir.GetLength() - 1] == '/')
		rootDir = rootDir.Left(rootDir.GetLength() - 1);

	// ascend from rootDir looking for git repos
	CStringW gitRootDir(rootDir);
	bool doLookForGit = Psettings->mUseGitRootForAugmentSolution;

	while (doLookForGit)
	{
		int lastSlash = gitRootDir.ReverseFind(L'\\');

		if (lastSlash != -1 && lastSlash > 2)
		{
			gitRootDir = gitRootDir.Mid(0, lastSlash);
			const CStringW searchSpec(gitRootDir + L"\\*.*");
			WIN32_FIND_DATAW fileData;
			HANDLE hFile = FindFirstFileW(searchSpec, &fileData);

			if (hFile != INVALID_HANDLE_VALUE)
			{
				do
				{
					if (fileData.dwFileAttributes != INVALID_FILE_ATTRIBUTES)
					{
						if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						{
							if (AreSimilar(fileData.cFileName, L".git"))
							{
								// set base dir of "include all files" [case: 111480]
								rootDir = gitRootDir;
								doLookForGit = false;
							}
						}
					}
				} while (doLookForGit && FindNextFileW(hFile, &fileData));
			}

			FindClose(hFile);
		}
		else
		{
			doLookForGit = false;
		}
	}

	dirs.AddUniqueNoCase(rootDir);

	// projects in solution
	{
		RWLockReader lck;
		LogElapsedTime let2("OFIS::AugmentSolutionFiles 1", 500);
		const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
		for (const auto& projIt : projMap)
		{
			const ProjectInfoPtr projInf = projIt.second;
			if (projInf && !projInf->IsPseudoProject())
			{
				CStringW projDir = projInf->GetProjectFile();
				if (::IsFile(projDir))
					projDir = ::Path(projDir);
				else if (!::IsDir(projDir))
					continue;

				if (projDir.GetLength() <= 3)
					continue;
				; // watch out for "c:\" or "//" or '\\'

				projDir.MakeLower();

				for (auto f : dirs)
				{
					if (0 == projDir.Find(f.mFilenameLower))
					{
						projDir.Empty();
						break;
					}
				}

				if (!projDir.IsEmpty())
					dirs.AddUniqueNoCase(projDir);
			}
		}
	}

	// Tools | Options | Projects and Solutions | VC++ Project Settings | Extensions to Hide
	CStringW excludeExtensions;
	if (!Psettings->mOfisIncludeHiddenExtensions)
	{
		const CStringW defaultExcludeExtensions(
		    L".suo;.sln;.ncb;.sdf;.vcxproj;.csproj;.user;.vbproj;.scc;.vsscc;.vspscc;.old;.filters;.vcxitems;");
		excludeExtensions = g_IdeSettings->GetVsStringOption("Projects", "VCGeneral", "ExtensionsToHide");
		if (excludeExtensions.IsEmpty() && gDte)
		{
			CComBSTR regRoot;
			gDte->get_RegistryRoot(&regRoot);
			CStringW regRootW = (const wchar_t*)regRoot;
			if (!regRootW.IsEmpty())
			{
				regRootW += L"\\VC";
				const CString regValName("Extensions To Hide");
				excludeExtensions =
				    ::GetRegValueW(HKEY_CURRENT_USER, CString(regRootW), regValName, defaultExcludeExtensions);
			}
		}

		if (excludeExtensions.IsEmpty())
			excludeExtensions = defaultExcludeExtensions;

		if (Psettings->mAugmentHiddenExtensions)
		{
			// this is a minor candidate for user customization; user can edit the list in VC++ Project Settings or
			// disable hiding
			const CStringW vaExcludeExtensions(
			    L";.vssscc;.vcproj;.tlog;.obj;.tlb;.exe;.dll;.exp;.res;.pdb;.pch;.lib;.idb;.ilk;.lastbuildstate;.aps;."
			    L"ipch;.unsuccessfulbuild;.i_cs;.cache;.lref;");
			excludeExtensions += vaExcludeExtensions;
		}
	}

	CStringW ext;
	RecursiveFileCollection rfc(dirs, false, false, (int*)&mLoadControl);
	LogElapsedTime let2("OFIS::AugmentSolutionFiles step 3", 500);
	FileList& foundFiles = rfc.GetFoundFiles();
	for (auto f : foundFiles)
	{
		if (ListLoadState::LoadNormal != mLoadControl)
			break;

		// #ofisNotConcurrent with respect to the AddFile call
		if (!mFileData->Contains_NoLock(f.mFileId))
		{
			if (!Psettings->mOfisIncludeHiddenExtensions)
			{
				LPCWSTR extension = ::GetBaseNameExt(f.mFilenameLower);
				// initial check is simple, can result in false positives but requires no allocation/deallocation
				if (-1 != excludeExtensions.Find(extension))
				{
					// double-check via more exclusive check
					ext = CStringW(L".") + extension + CStringW(L";");
					if (-1 != excludeExtensions.Find(ext))
						continue;
				}
			}

			if (-1 != f.mFilenameLower.Find(L"\\out\\build\\"))
				AddFile(f, L"[CMake output]");
			else
				AddFile(f, L"[Solution]");
		}
	}
}

void VAOpenFile::PopulateList()
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
		StopThreadAndWait();

	mLoadControl = ListLoadState::LoadNormal;
	mIsLongLoad = false;
	if (mLoader.StartThread())
	{
		// wait until thread has started so that UpdateFilter logic works properly
		while (!mLoader.HasStarted())
			Sleep(10);
	}

	// [case: 104968]
	CWnd* cancel_wnd = GetDlgItem(IDCANCEL);
	if (cancel_wnd)
	{
		HWND cancel_hWnd = cancel_wnd->m_hWnd;

		// wait for a while and change text of button,
		// user now can click Cancel to close the dialog
		DelayedInvoke(100, [this, cancel_hWnd]() {
			// delayed because it was causing behavior similar to flicker when there
			// was quick reaction from search thread. It looked like bug as it
			// twice changed text very quickly. When that lambda is called,
			// it checks if mLoader.IsRunning() and if so, then text
			// of button is changed.  That prevents from blink.
			if (::IsWindow(cancel_hWnd) && mLoader.IsRunning())
			{
				mIsLongLoad = true;

				if (ButtonTextId::txtStop != mButtonTxtId)
				{
					// change IDCANCEL text from Cancel to Stop
					mButtonTxtId = ButtonTextId::txtStop;
					::SetWindowTextA(cancel_hWnd, "S&top");
				}

				// ensure that Stop button is enabled
				::EnableWindow(cancel_hWnd, TRUE);
			}
		});
	}

	SetTimer(VOF_TimerID_ChangeTitleToLoading, 100, NULL);

	UpdateFilter();
}

void VAOpenFile::OfisLoader(LPVOID pVaOpenFile)
{
	VAOpenFile* _this = (VAOpenFile*)pVaOpenFile;
	_ASSERTE(_this->mLoadControl == ListLoadState::LoadNormal);
	_this->PopulateListThreadFunc();
}

void VAOpenFile::PopulateListThreadFunc()
{
	LogElapsedTime let("OFIS::PopulateListThreadFunc", 1000);
	if (ListLoadState::QuitLoad == mLoadControl)
		return;

	if (!GlobalProject)
	{
		SetTimer(VOF_TimerID_Loading, 250, NULL);
		return;
	}

	if (!mFileData->GetCount())
	{
		// [case: 64207] killtimer set at end of UpdateTitle
		KillTimer(VOF_TimerID_Loading);
	}

	// xxx_sean - consider caching; check project load cookie for need to update;
	// the window list presents a problem though...
	// 	static int sProjectLoadCookie = 200000;
	// 	GlobalProject->GetLoadCookie();
	mFileData->Empty();

	using namespace std::placeholders;

	void (VAOpenFile::*fp)(const FileInfo&, const CStringW&) = &VAOpenFile::AddFile;

	if (IsRestrictedToWorkspace())
	{
		EnumerateProjectFiles(std::bind(fp, this, _1, _2));
		if (Psettings->mOfisAugmentSolution && ListLoadState::LoadNormal == mLoadControl)
			AugmentSolutionFiles();

		if (ListLoadState::LoadNormal == mLoadControl)
		{
			LogElapsedTime let2("OFIS::PopulateListThreadFunc privsys (a)", 500);
			RWLockReader lck2;
			const FileList& hdrs = GlobalProject->GetSolutionPrivateSystemHeaders(lck2);
			if (hdrs.size())
				SetTimer(VOF_TimerID_NugetMsg, 250, NULL);
		}
	}
	else
	{
		if (Psettings->mOfisIncludeSolution)
			EnumerateProjectFiles(std::bind(fp, this, _1, _2));

		if (Psettings->mOfisIncludePrivateSystem && ListLoadState::LoadNormal == mLoadControl)
		{
			// add solution private system headers
			LogElapsedTime let2("OFIS::PopulateListThreadFunc privsys (b)", 500);
			//			std::wregex thirdPartyRegex(L"(third|3rd)[ ]*party", std::regex::ECMAScript |
			// std::regex::optimize | std::regex::icase);
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
			FileList curDirs;

			{
				RWLockReader lck;
				const FileList& dirs = GlobalProject->GetSolutionPrivateSystemIncludeDirs(lck);
				curDirs.Add(dirs);
			}

			if (curDirs.size())
				::DisableOneTimeMessageBox(NUGET_REPOSITORY_MESSAGE_NAME);

			RecursiveFileCollection rfc(curDirs);
			FileList& foundFiles = rfc.GetFoundFiles();
			for (auto f : foundFiles)
			{
				if (ListLoadState::LoadNormal != mLoadControl)
					break;

				// #ofisNotConcurrent with respect to the AddFile calls
				if (!mFileData->Contains_NoLock(f.mFileId))
				{
					if (thirdPartyRegex && std::regex_search((LPCWSTR)f.mFilenameLower, *thirdPartyRegex))
						AddFile(f, L"[3rd Party]"); // [case: 104857]
					else
						AddFile(f, L"[NuGet]");
				}
			}
		}

		if (Psettings->mOfisIncludeSolution && Psettings->mOfisAugmentSolution &&
		    ListLoadState::LoadNormal == mLoadControl)
			AugmentSolutionFiles();

		if (Psettings->mOfisIncludeSystem && ListLoadState::LoadNormal == mLoadControl)
		{
			// [case: 109019] hoist incDirs outside of the iterateAll loop
			SemiColonDelimitedString incDirs; // #ofisNotConcurrent
			::GetMassagedSystemDirs(incDirs);

			// [case: 1029]
			// add system files we have seen
			auto AddIfSysFile = [&](uint fileId, const CStringW& filename) {
				if (ListLoadState::LoadNormal != mLoadControl)
					return;

				// Contains_NoLock is dependent upon IterateAll not being concurrent #ofisNotConcurrent
				if (mFileData->Contains_NoLock(fileId))
					return;

				if (!IncludeDirs::IsSystemFile(filename, incDirs))
					return;

				if (GetBaseNameExt(filename) != (LPCWSTR)filename || IsFile(filename))
				{
					CStringW desc;
					FileInfo fi(filename, fileId);

					if (-1 != fi.mFilenameLower.Find(L"\\installed\\") || -1 != fi.mFilenameLower.Find(L"/installed/"))
					{
						if (-1 != fi.mFilenameLower.Find(L"-uwp\\include") ||
						    -1 != fi.mFilenameLower.Find(L"-windows\\include") ||
						    -1 != fi.mFilenameLower.Find(L"-windows-static\\include"))
						{
							// [case: 100271]
							desc = L"[Vcpkg]";
						}
					}

					if (desc.IsEmpty())
						desc = L"[System]";

					AddFile(fi, desc);
				}
			};

			LogElapsedTime let2("OFIS::PopulateListThreadFunc all", 500);
			// this call is not concurrent and the use of incDirs in AddIfSysFile
			// depends on that (each thread would need its own copy)  #ofisNotConcurrent
			gFileIdManager->IterateAll(AddIfSysFile);
		}

		if (Psettings->mOfisIncludeExternal && ListLoadState::LoadNormal == mLoadControl)
		{
			// [case: 1029]
			// search all #includes in solution that aren't already listed
			CSpinCriticalSection cs;
			FileList filesToAdd;
			auto AddIfNotPresent = [&](uint fileId) {
				if (ListLoadState::LoadNormal != mLoadControl)
					return;

				// this is concurrent, so don't use Contains_NoLock
				if (!mFileData->Contains(fileId))
				{
					const CStringW filename(gFileIdManager->GetFile(fileId));
					if (!Psettings->mOfisIncludeSolution)
					{
						if (GlobalProject->ContainsNonExternal(filename))
							return;
					}

					if (filename.IsEmpty())
						return;

					AutoLockCs l(cs);
					filesToAdd.AddUnique(FileInfo(filename, fileId));
				}
			};

			LogElapsedTime let2("OFIS::PopulateListThreadFunc all inc", 500);
			IncludesDb::IterateAllIncluded(DTypeDbScope::dbSolution, AddIfNotPresent);

			for (auto f : filesToAdd)
			{
				if (ListLoadState::LoadNormal != mLoadControl)
					break;

				AddFile(f, L"[External]");
			}
		}

		if (Psettings->mOfisIncludeWindowList && ListLoadState::LoadNormal == mLoadControl)
		{
			// add open, non-solution, files
			LogElapsedTime let2("OFIS::PopulateListThreadFunc wnd", 500);
			WCHAR buf[MAX_PATH + 1];
			LPCWSTR p = (LPCWSTR)::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_GETOPENWINDOWLISTW, 0, 0);
			if (p)
			{
				for (int i = 0; *p; p++)
				{
					if (*p == L';')
					{
						buf[i] = L'\0';
						AddFile(buf, true, L"[Open]"); // check for dupes before Add
						i = 0;
					}
					else if (i < MAX_PATH)
					{
						buf[i] = *p;
						i++;
					}
				}
			}
		}
	}

	if (ListLoadState::QuitLoad == mLoadControl)
		return;

	mFilterList.SetRedraw(TRUE);

	{
		// sort even if user interrupted load
		LogElapsedTime let2("OFIS::PopulateListThreadFunc sort", 250);
		mFileData->Sort(SortFunc);
	}

	if (sFilterDuplicates && ListLoadState::LoadNormal == mLoadControl)
	{
		// but skip duplicate handling if user interrupted load
		LogElapsedTime let2("OFIS::PopulateListThreadFunc dupes", 250);
		mFileData->HandleDuplicates();
	}

	// change IDCANCEL text from Stop back to Cancel now that the thread is finished
	if (ButtonTextId::txtStop == mButtonTxtId && ListLoadState::QuitLoad != mLoadControl)
	{
		CWnd* cancel_wnd = GetDlgItem(IDCANCEL);
		if (cancel_wnd)
		{
			// passing HWND, because CWnd could be deleted at the moment of invoke
			HWND wnd_hWhd = cancel_wnd->m_hWnd;

			if (mIsLongLoad)
			{
				// [case: 135202]
				auto enableButton = [wnd_hWhd]() {
					// disable Stop button to disallow user to mistakenly press it.
					if (::IsWindow(wnd_hWhd))
						::EnableWindow(wnd_hWhd, FALSE);
				};

				// change immediately (relatively speaking, so as to invoke from ui thread)
				DelayedInvoke(1, enableButton);
			}

			auto ResetButton = [wnd_hWhd, this]() {
				if (::IsWindow(wnd_hWhd))
				{
					if (ButtonTextId::txtCancel != mButtonTxtId)
					{
						mButtonTxtId = ButtonTextId::txtCancel;
						::SetWindowTextA(wnd_hWhd, "Cancel");
					}
					::EnableWindow(wnd_hWhd, TRUE);
				}
			};

			if (mIsLongLoad)
			{
				// wait for a while and change text of button,
				// user now can click Cancel to close the dialog
				DelayedInvoke(500, ResetButton);
			}
			else
			{
				// change immediately (relatively speaking, so as to invoke from ui thread)
				DelayedInvoke(1, ResetButton);
			}
		}
	}
}

void VAOpenFile::StopThreadAndWait()
{
	bool waited = false;
	if (!mLoader.HasStarted())
	{
		// [case: 110347]
		mLoadControl = ListLoadState::QuitLoad;
		CWaitCursor curs;
		Sleep(5000);
		waited = true;
	}

	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		mLoadControl = ListLoadState::QuitLoad;
		CWaitCursor curs;
		if (::GetCurrentThreadId() == g_mainThread)
			mLoader.Wait(waited ? 5000u : 10000u);
		else
			mLoader.Wait(INFINITE);
	}
}

BOOL VAOpenFile::OnInitDialog()
{
	// [case: 136927] cache value on UI thread
	g_IdeSettings->GetVsStringOption("Projects", "VCGeneral", "ExtensionsToHide");

	FindInWkspcDlg::OnInitDialog();

	mEdit2.SubclassWindowW(::GetDlgItem(m_hWnd, IDC_FILTEREDIT2));
	mEdit2.SetDimColor(::GetSysColor(COLOR_GRAYTEXT));
	if (CVS2010Colours::IsExtendedThemeActive())
		ThemeUtils::ApplyThemeInWindows(TRUE, mEdit2);

	::mySetProp(mFilterList, "__VA_do_not_colour", (HANDLE)1);

	// make list control unicode
	mFilterList.SendMessage(CCM_SETUNICODEFORMAT, 1, 0);

	mFilterList.SortArrow = true;
	mFilterList.SortColumn = sSortingColumn;
	mFilterList.SortReverse = sSortReverse;

	SetEditHelpText();

	AddSzControl(IDC_FILELIST, mdResize, mdResize);
	AddSzControl(IDC_FILTEREDIT, mdResize, mdRepos);
	AddSzControl(IDC_FILTEREDIT2, mdResize, mdRepos);
	AddSzControl(IDC_ALL_OPEN_FILES, mdNone, mdRepos);
	AddSzControl(IDC_USE_FUZZY_SEARCH, mdNone, mdRepos);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);

	SetTooltipStyle(Psettings->mOfisTooltips);

	CRect rc;
	mFilterList.GetClientRect(&rc);
	rc.right -= ::GetSystemMetrics(SM_CXVSCROLL);

	int width;
	if ((GetParent()->GetDlgCtrlID() & 0xff00) == 0xe900)
	{
		width = GetInitialColumnWidth(0, 200);
		mFilterList.InsertColumn(0, "File", LVCFMT_LEFT, width);
	}
	else
	{
		// filename is 3x the size of date column (1.5 * project)
		width = GetInitialColumnWidth(0, (int)((rc.Width() / 12.0) * 3));
		mFilterList.InsertColumn(0, "File", LVCFMT_LEFT, width);

		// project
		if (sDisplayProjectColumn)
		{
			// project gets 1/6 if there is more than 1 project in the solution
			width = GetInitialColumnWidth(1, rc.Width() / 6);
			mFilterList.InsertColumn(1, "Project", LVCFMT_LEFT, width);
		}

		// path gets 1/2 (or more if no project column)
		if (sDisplayProjectColumn)
			width = rc.Width() / 2;
		else
			width = (int)((rc.Width() / 6.0) * 4);
		width = GetInitialColumnWidth(1 + sDisplayProjectColumn, width);
		mFilterList.InsertColumn(1 + sDisplayProjectColumn, "Path", LVCFMT_LEFT, width);

		// date is smallest 1/12
		width = GetInitialColumnWidth(2 + sDisplayProjectColumn, rc.Width() / 12);
		mFilterList.InsertColumn(2 + sDisplayProjectColumn, "Modified", LVCFMT_LEFT, width);
	}

	mFileData->fullPathSearch = false;

	if (sOfisFilterStringPartial.GetLength())
	{
		mEdit.SetText(sOfisFilterStringPartial);
		mEdit.SetSel(0, -1);
	}

	// [case: 25837]
	CStringW persistentFilter(Psettings->mOfisPersistentFilter);
	persistentFilter.Trim();
	if (!persistentFilter.IsEmpty())
	{
		mEdit2.SetText(persistentFilter);
		sOfisFilterString = sOfisFilterStringPartial + CStringW(L" ") + persistentFilter;
	}

	if (!Psettings->mOfisDisplayPersistentFilter)
		RearrangeForPersistentFilterEditControl();

	if (sOfisFilterString.FindOneOf(L"\\/") != -1)
		mFileData->fullPathSearch = true;
	mFileData->FilterFiles({sOfisFilterString.GetString(), sOfisFilterString.GetString() + sOfisFilterString.GetLength()}, IsFuzzyUsed(), false, mFileData->fullPathSearch);
	PopulateList();

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VAOpenFile::UpdateFilter(bool force /*= false*/)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		KillTimer(VOF_TimerID_Filter);
		SetTimer(VOF_TimerID_Filter, 50, NULL);
		return;
	}

	mEdit.GetText(sOfisFilterStringPartial);

	// [case: 25837]
	CStringW persistentFilter;
	if (mEdit2.IsWindowVisible())
	{
		mEdit2.GetText(persistentFilter);
		::wcscpy_s(Psettings->mOfisPersistentFilter, MAX_PATH, persistentFilter);
		Psettings->mOfisPersistentFilter[MAX_PATH - 1] = L'\0';
	}
	else
		persistentFilter = Psettings->mOfisPersistentFilter;

	persistentFilter.Trim();
	if (persistentFilter.IsEmpty() || !Psettings->mOfisApplyPersistentFilter)
		sOfisFilterString = sOfisFilterStringPartial;
	else
		sOfisFilterString = sOfisFilterStringPartial + CStringW(L" ") + persistentFilter;

	mFileData->fullPathSearch = false;
	if (sOfisFilterString.FindOneOf(L"\\/") != -1)
		mFileData->fullPathSearch = true;

	CStringW strippedFilter(::StripFilterTextOfLineNumber(sOfisFilterString));
	mFileData->FilterFiles({strippedFilter.GetString(), strippedFilter.GetString() + strippedFilter.GetLength()}, IsFuzzyUsed(), force, mFileData->fullPathSearch);
	if (mFilterList.GetItemCount() < 25000)
	{
		// [case=141733] see also case=20182
		// when list is very large, DeleteAllItems has horrible performance.
		// DeleteAllItems is required for ScrollItemToMiddle to work for letters
		// beyond the first typed.
		// ScrollItemToMiddle not functional until item count is < xxK items.
		mFilterList.DeleteAllItems();
	}
	const int kSetCount = mFileData->GetFilteredSetCount();
	mFilterList.SetItemCount(kSetCount);
	mFilterList.Invalidate();
	if (kSetCount)
	{
		POSITION p = mFilterList.GetFirstSelectedItemPosition();
		// clear selection
		for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
		{
			mFilterList.SetItemState((int)(intptr_t)p - 1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			mFilterList.GetNextSelectedItem(p);
		}
		const int kSuggestion = mFileData->GetSuggestionIdx();
		mFilterList.SetItemState(kSuggestion, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		// [case: 19262] scroll to middle if suggestion is beyond middle
		ScrollItemToMiddle(kSuggestion);
	}

	UpdateTitle();
}

void VAOpenFile::AddFile(const CStringW& file, bool checkUnique, const CStringW& projectName)
{
	FileInfo f(file);
	// #ofisNotConcurrent
	if (!checkUnique || !mFileData->Contains_NoLock(f.mFileId))
		AddFile(f, projectName);
}

void VAOpenFile::AddFile(const FileInfo& fInfo, const CStringW& projectName)
{
	const CStringW pth = fInfo.mFilenameLower;
	if (pth.Find(L"//dataexplorer/") != -1 || pth.Find(L"oledb:://") != -1 || pth.Find(L"sql:") != -1)
		return; // [case: 2945] should we exclude anything with "://" ?

	if (Psettings->mUnrealEngineCppSupport && !Psettings->mIndexGeneratedCode && EndsWith(pth, L".generated.h"))
	{
		// [case: 119597] hide .generated.h files from the dialog if user don't have selected option to index generated
		// code in case of UE project
		return;
	}

	if (GlobalProject && !GlobalProject->GetFileExclusionRegexes().empty())
	{
		for (const auto& regex : GlobalProject->GetFileExclusionRegexes())
		{
			if (std::regex_match(std::wstring(pth), regex))
			{
				// [case: 149171] don't show excluded files in open file dialog
				return;
			}
		}
	}

	FileData* newnode = new FileData(fInfo, projectName);
	mFileData->AddItem(newnode);
}

void VAOpenFile::OnClickHeaderItem(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// [case: 110325]
		// don't sort while thread is still populating mFileData
	}
	else
	{
		NMHEADERW* phdn = (NMHEADERW*)pNMHDR;
		if (phdn->hdr.hwndFrom == mFilterList.GetHeaderCtrl()->m_hWnd && phdn->iButton == 0)
		{
			sSortReverse = !sSortReverse && (sSortingColumn == phdn->iItem);
			sSortingColumn = phdn->iItem;
			mFilterList.SortColumn = sSortingColumn;
			mFilterList.SortReverse = sSortReverse;
			mFilterList.SetRedraw(TRUE);
			mFileData->Sort(SortFunc);
			UpdateFilter(true);
		}
	}

	*pResult = 0;
}

// Windows is asking for the text to display, at item(iItemIndex), subitem(pItem->iSubItem)
void VAOpenFile::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;

	LV_ITEM* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	FileData* ptr = mFileData->GetAt(iItemIndex);

	if (ptr && ((pItem->mask & LVIF_TEXT) == LVIF_TEXT || (pItem->mask & LVIF_IMAGE) == LVIF_IMAGE))
	{
		if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
		{
			const int column = sDisplayProjectColumn ? pItem->iSubItem : (pItem->iSubItem ? pItem->iSubItem + 1 : 0);
			switch (column)
			{
			case 0:
				lstrcpyn(pItem->pszText, WideToMbcs(GetBaseName(ptr->mFilename), -1).c_str(), pItem->cchTextMax);
				break;
			case 1:
				lstrcpyn(pItem->pszText, WideToMbcs(ptr->mProjectBaseName, -1).c_str(), pItem->cchTextMax);
				break;
			case 2:
				lstrcpyn(pItem->pszText, WideToMbcs(ptr->mFilename, -1).c_str(), pItem->cchTextMax);
				break;
			case 3: {
				SYSTEMTIME sysT;
				FILETIME ft;
				if (::GetFTime(ptr->mFilename, &ft))
				{
					ptr->mFiletime = ft;
					ptr->mFiletime.GetAsSystemTime(sysT);
					const int kBufLen = 64;
					char dateBuf[kBufLen] = "";
					char timeBuf[kBufLen] = "";
					::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &sysT, NULL, dateBuf, kBufLen);
					::GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS | TIME_FORCE24HOURFORMAT, &sysT, NULL, timeBuf,
					                kBufLen);
					::_snprintf(pItem->pszText, (uint)pItem->cchTextMax, "%s %s", dateBuf, timeBuf);
				}
			}
			break;
			}
		}

		if ((pItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
			pItem->iImage = pItem->iSubItem != 0 ? -1 : ::GetFileImgIdx(ptr->mFilename, ICONIDX_FILE_TXT);

		*pResult = 0;
	}
}

void VAOpenFile::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;

	LVITEMW* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	FileData* ptr = mFileData->GetAt(iItemIndex);

	if (ptr && ((pItem->mask & LVIF_TEXT) == LVIF_TEXT || (pItem->mask & LVIF_IMAGE) == LVIF_IMAGE))
	{
		if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
		{
			const int column = sDisplayProjectColumn ? pItem->iSubItem : (pItem->iSubItem ? pItem->iSubItem + 1 : 0);
			switch (column)
			{
			case 0:
				lstrcpynW(pItem->pszText, GetBaseName(ptr->mFilename), pItem->cchTextMax);
				break;
			case 1:
				lstrcpynW(pItem->pszText, ptr->mProjectBaseName, pItem->cchTextMax);
				break;
			case 2:
				lstrcpynW(pItem->pszText, ptr->mFilename, pItem->cchTextMax);
				break;
			case 3: {
				SYSTEMTIME sysT;
				FILETIME ft;
				if (::GetFTime(ptr->mFilename, &ft))
				{
					ptr->mFiletime = ft;
					ptr->mFiletime.GetAsSystemTime(sysT);
					const int kBufLen = 100;
					WCHAR dateBuf[kBufLen] = L"";
					WCHAR timeBuf[kBufLen] = L"";
					::GetDateFormatW(LOCALE_USER_DEFAULT, NULL, &sysT, NULL, dateBuf, kBufLen);
					::GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS | TIME_FORCE24HOURFORMAT, &sysT, NULL, timeBuf,
					                 kBufLen);
					::_snwprintf(pItem->pszText, (uint)pItem->cchTextMax, L"%s %s", dateBuf, timeBuf);
				}
			}
			break;
			}
		}

		if ((pItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
			pItem->iImage = pItem->iSubItem != 0 ? -1 : ::GetFileImgIdx(ptr->mFilename, ICONIDX_FILE_TXT);

		*pResult = 0;
	}
}

void VAOpenFile::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case VOF_TimerID_Loading:
		KillTimer(nIDEvent);
		if (!GlobalProject)
		{
			CString wndCaption;
#if defined(RAD_STUDIO)
			wndCaption = "Open File in Project Group (Parsing...)";
#else
			CString__FormatA(wndCaption, "Open File in %s (Parsing...)",
			                 gShellAttr->IsMsdev() ? "Workspace" : "Solution");
#endif
			VAUpdateWindowTitle(VAWindowType::OFIS, wndCaption, 0);
			SetWindowText(wndCaption);
			LoadCursor(NULL, IDC_WAIT);
			SetTimer(VOF_TimerID_Loading, 250, NULL);
		}
		else
		{
			// PopulateList sets title
			PopulateList();
		}
		break;

	case VOF_TimerID_ChangeTitleToLoading:
		// [case: 104140]
		KillTimer(nIDEvent);
		if (mLoader.HasStarted() && !mLoader.IsFinished())
		{
			CString wndCaption;
#if defined(RAD_STUDIO)
			wndCaption = "Open File in Project Group [loading...]";
#else
			CString__FormatA(wndCaption, "Open File in %s [loading...]",
			                 gShellAttr->IsMsdev() ? "Workspace" : "Solution");
#endif
			VAUpdateWindowTitle(VAWindowType::OFIS, wndCaption, 1);
			SetWindowText(wndCaption);
		}
		break;

	case VOF_TimerID_Filter:
		if (mLoader.HasStarted() && !mLoader.IsFinished())
			return;

		KillTimer(nIDEvent);
		UpdateFilter();
		break;

	case VOF_TimerID_NugetMsg:
		KillTimer(nIDEvent);
		::OneTimeMessageBox(NUGET_REPOSITORY_MESSAGE_NAME,
		                    "If you would also like headers from the NuGet repository displayed in the file list, "
		                    "uncheck 'Show only files...' in the bottom left corner of the dialog.",
		                    MB_ICONINFORMATION | MB_OK, m_hWnd);
		break;

	default:
		FindInWkspcDlg::OnTimer(nIDEvent);
	}
}

void VAOpenFile::OnCancel()
{
	if (mLoader.IsRunning())
	{
		// [case: 104968]
		if (ListLoadState::InterruptLoad == mLoadControl)
		{
			// already tried to cancel but thread is still running...
			__super::OnCancel();
			return;
		}

		mLoadControl = ListLoadState::InterruptLoad;

		if (ButtonTextId::txtCancel != mButtonTxtId)
		{
			// change IDCANCEL text from Stop to Cancel
			CWnd* cancel_wnd = GetDlgItem(IDCANCEL);
			if (cancel_wnd)
			{
				mButtonTxtId = ButtonTextId::txtCancel;
				cancel_wnd->SetWindowText("Cancel");
			}
		}
	}
	else
		__super::OnCancel();
}

void VAOpenFile::OnDestroy()
{
	StopThreadAndWait();
	mEdit.GetText(sOfisFilterStringPartial);

	if (mEdit2.IsWindowVisible())
	{
		CStringW tmp;
		mEdit2.GetText(tmp);
		::wcscpy_s(Psettings->mOfisPersistentFilter, MAX_PATH, tmp);
		Psettings->mOfisPersistentFilter[MAX_PATH - 1] = L'\0';
	}

	FindInWkspcDlg::OnDestroy();
}

void VAOpenFile::UpdateTitle()
{
	const int displayCnt = mFileData->GetFilteredSetCount();
	CString title;
#if defined(RAD_STUDIO)
	CString__FormatA(title, "Open File in Project Group [%d of %d]", displayCnt, mFileData->GetCount());
#else
	CString__FormatA(title, "Open File in %s [%d of %d]", gShellAttr->IsMsdev() ? "Workspace" : "Solution", displayCnt,
	                 mFileData->GetCount());
#endif
	if (mLoadControl == ListLoadState::InterruptLoad)
		title += " (incomplete load)";
	VAUpdateWindowTitle(VAWindowType::OFIS, title, 2);
	SetWindowText(title);

	CWnd* pButton(GetDlgItem(IDOK));
	_ASSERTE(pButton);
	if (pButton)
	{
		pButton->EnableWindow(displayCnt);

		if (!displayCnt)
		{
			// [case: 32171]
			const CStringW strippedFilter(::StripFilterTextOfLineNumber(sOfisFilterString));
			if (strippedFilter.GetLength() > 3 && strippedFilter[1] == L':' && ::IsFile(strippedFilter))
				pButton->EnableWindow(TRUE);
		}
	}

	if (!mFileData->GetCount())
		SetTimer(VOF_TimerID_Loading, 1000, NULL);
}

int VAOpenFile::GetFilterTimerPeriod() const
{
	CStringW newFilter;
	mEdit.GetText(newFilter);

	{
		CStringW tmp;
		mEdit2.GetText(tmp);
		if (tmp.GetLength())
			newFilter += CStringW(" ") + tmp;
	}

	bool filterCurrentSet = false;
	if (mFileData->GetFilteredSetCount() && sOfisFilterString.GetLength() &&
	    sOfisFilterString.GetLength() <= newFilter.GetLength() && newFilter.Find(sOfisFilterString) == 0 &&
	    newFilter.FindOneOf(L"-,") == -1)
	{
		filterCurrentSet = true;
	}

	const int kWorkingSetCount = filterCurrentSet ? mFileData->GetFilteredSetCount() : mFileData->GetCount();
	if (kWorkingSetCount < 1000)
		return 0;
	else if (kWorkingSetCount < 10000)
		return 50;
	else if (kWorkingSetCount < 25000)
		return 100;
	else if (kWorkingSetCount < 50000)
		return 200;
	else if (kWorkingSetCount < 100000)
		return 300;
	else if (kWorkingSetCount < 200000)
		return 400;
	else if (kWorkingSetCount < 500000)
		return 600;
	else
		return 750;
}

void VAOpenFile::GetTooltipText(int itemRow, WTString& txt)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		return;
	}

	FileData* curItem = mFileData->GetAt(itemRow);
	if (curItem && curItem->mFilename.GetLength())
	{
		txt = curItem->mFilename;
	}
}

void VAOpenFile::GetTooltipTextW(int itemRow, CStringW& txt)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		return;
	}

	FileData* curItem = mFileData->GetAt(itemRow);
	if (curItem && curItem->mFilename.GetLength())
	{
		txt = curItem->mFilename;
	}
}

void VAOpenFile::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	uint selItemCnt = mFilterList.GetSelectedCount();

	_ASSERTE((selItemCnt > 0 && p) || (!selItemCnt && !p));

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && p)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (mFilterList.GetItemRect((int)(intptr_t)p - 1, &rc, LVIR_ICON))
		{
			mFilterList.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}
	else if (!selItemCnt && !p && pos == CPoint(-1, -1))
	{
		// [case: 78216] nothing selected and not a click
		mFilterList.GetClientRect(&rc);
		mFilterList.ClientToScreen(&rc);
		pos.x = rc.left + 16;
		pos.y = rc.top + 16;
	}

//	const DWORD kSelectionDependentItemState = (p) ? 0u : MF_GRAYED | MF_DISABLED;

	CMenu contextMenu;
	contextMenu.CreatePopupMenu();
	const bool hasFocus = CWnd::GetFocus()->GetSafeHwnd() == mFilterList.GetSafeHwnd();
	if (hasFocus && mFilterList.GetSelectedCount() >= 1)
	{
		contextMenu.AppendMenu(0, MAKEWPARAM(ID_COPY_FILENAME, 0), "Copy &Filename\tCtrl+F");
		contextMenu.AppendMenu(0, MAKEWPARAM(ID_COPY_FULL_PATH, 0), "Copy Full &Path\tCtrl+C");
		if (mFilterList.GetSelectedCount() == 1)
			contextMenu.AppendMenu(0, MAKEWPARAM(ID_OPEN_CONTAINING_FOLDER, 0), "&Open Containing Folder\tCtrl+E");
		contextMenu.AppendMenu(MF_SEPARATOR);
	}

	// menu item group for solution files
	if (!IsRestrictedToWorkspace())
		contextMenu.AppendMenu(Psettings->mOfisIncludeSolution ? MF_CHECKED : 0u, ID_TOGGLE_IncludeSolution,
		                       "Include &Solution Files\tCtrl+S");

	if (IsRestrictedToWorkspace() || Psettings->mOfisIncludeSolution)
	{
		contextMenu.AppendMenu(Psettings->mOfisAugmentSolution ? MF_CHECKED : 0u,
		                       ID_TOGGLE_AugmentSolutionFilesByDiskSearch,
		                       "Include all files in solution &directories\tCtrl+D");

		if (Psettings->mOfisAugmentSolution)
			contextMenu.AppendMenu(Psettings->mOfisIncludeHiddenExtensions ? MF_CHECKED : 0u,
			                       ID_TOGGLE_IncludeHiddenExtensions, "Include files &hidden by extension\tCtrl+H");
	}

	contextMenu.AppendMenu(MF_SEPARATOR);

	if (!IsRestrictedToWorkspace())
	{
		// menu item group for other sources of files
		contextMenu.AppendMenu(Psettings->mOfisIncludePrivateSystem ? MF_CHECKED : 0u, ID_TOGGLE_IncludePrivateSystem,
		                       "Include &Private System Files&\tCtrl+P");
		contextMenu.AppendMenu(Psettings->mOfisIncludeSystem ? MF_CHECKED : 0u, ID_TOGGLE_IncludeSystem,
		                       "Include Shared S&ystem Files\tCtrl+Y");
		contextMenu.AppendMenu(Psettings->mOfisIncludeExternal ? MF_CHECKED : 0u, ID_TOGGLE_IncludeExternal,
		                       "Include Ex&ternal Files\tCtrl+T");
		contextMenu.AppendMenu(Psettings->mOfisIncludeWindowList ? MF_CHECKED : 0u, ID_TOGGLE_IncludeWindowList,
		                       "Include &Open Files\tCtrl+O");

		contextMenu.AppendMenu(MF_SEPARATOR);
	}

	if (sDisplayProjectColumn)
	{
		contextMenu.AppendMenu(UINT(sFilterDuplicates ? MF_ENABLED | MF_CHECKED : MF_ENABLED),
		                       MAKEWPARAM(ID_TOGGLE_DUPE_DISPLAY, 0), "&Combine duplicate file entries");
	}

	// command text corresponds to inverted setting
	contextMenu.AppendMenu(UINT(Psettings->mForceCaseInsensitiveFilters ? MF_ENABLED : MF_ENABLED | MF_CHECKED),
	                       MAKEWPARAM(ID_TOGGLE_CASE_SENSITIVITY, 0),
	                       "&Match case of search strings that contain uppercase letters");
	contextMenu.AppendMenu(Psettings->mOfisTooltips ? MF_CHECKED : 0u, ID_TOGGLE_TOOLTIPS, "&Tooltips");
	contextMenu.AppendMenu(Psettings->mOfisApplyPersistentFilter ? MF_CHECKED : 0u, ID_TOGGLE_ApplyPersistentFilter,
	                       "Apply pe&rsistent filter\tCtrl+R");
	contextMenu.AppendMenu(Psettings->mOfisDisplayPersistentFilter ? MF_CHECKED : 0u, ID_TOGGLE_DisplayPersistentFilter,
	                       "Display &edit control for persistent filter");
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(0, MAKEWPARAM(ID_SELECT_ALL, 0), "Select &All\tCtrl+A");

	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

void VAOpenFile::OnToggleDupeDisplay()
{
	sFilterDuplicates = !sFilterDuplicates;
	static const CString valueName(GetBaseRegName() + "FilterDuplicates");
	SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, valueName, sFilterDuplicates);
	PopulateList();
}

void VAOpenFile::OnToggleCaseSensitivity()
{
	Psettings->mForceCaseInsensitiveFilters = !Psettings->mForceCaseInsensitiveFilters;
	SetEditHelpText();
	UpdateFilter(true);
}

void VAOpenFile::OnToggleTooltips()
{
	Psettings->mOfisTooltips = !Psettings->mOfisTooltips;
	SetTooltipStyle(Psettings->mOfisTooltips);
}

void VAOpenFile::OnToggleIncludeSolution()
{
	if (IsRestrictedToWorkspace())
		return;

	Psettings->mOfisIncludeSolution = !Psettings->mOfisIncludeSolution;
	PopulateList();
}

void VAOpenFile::OnToggleIncludePrivateSystem()
{
	if (IsRestrictedToWorkspace())
		return;

	Psettings->mOfisIncludePrivateSystem = !Psettings->mOfisIncludePrivateSystem;
	PopulateList();
}

void VAOpenFile::OnToggleIncludeWindowList()
{
	if (IsRestrictedToWorkspace())
		return;

	Psettings->mOfisIncludeWindowList = !Psettings->mOfisIncludeWindowList;
	PopulateList();
}

void VAOpenFile::OnToggleIncludeSystem()
{
	if (IsRestrictedToWorkspace())
		return;

	Psettings->mOfisIncludeSystem = !Psettings->mOfisIncludeSystem;
	PopulateList();
}

void VAOpenFile::OnToggleIncludeExternal()
{
	if (IsRestrictedToWorkspace())
		return;

	Psettings->mOfisIncludeExternal = !Psettings->mOfisIncludeExternal;
	PopulateList();
}

void VAOpenFile::OnToggleAugmentSolution()
{
	Psettings->mOfisAugmentSolution = !Psettings->mOfisAugmentSolution;
	PopulateList();
}

void VAOpenFile::OnToggleIncludeHiddenExtensions()
{
	Psettings->mOfisIncludeHiddenExtensions = !Psettings->mOfisIncludeHiddenExtensions;
	PopulateList();
}

void VAOpenFile::RearrangeForPersistentFilterEditControl()
{
	// [case: 25837]
	static int sGap = 0;

	Position posList, posEdit, posEdit2;
	GetControlPosition(mFilterList, posList);
	GetControlPosition(mEdit, posEdit);

	if (Psettings->mOfisDisplayPersistentFilter)
	{
		_ASSERTE(sGap);
		// resize the list to open space for the primary edit
		posList.bottom -= sGap;
		OverrideControlPosition(mFilterList, posList);

		// persistent edit goes where primary edit currently is
		mEdit2.ShowWindow(SW_SHOW);
		OverrideControlPosition(mEdit2, posEdit);

		// move primary edit to the gap that was created
		posEdit2 = posEdit;
		posEdit2.OffsetRect(0, -sGap);
		posEdit2.right = posList.right;
		OverrideControlPosition(mEdit, posEdit2);
	}
	else
	{
		// reposition primary edit to where the persistent edit was
		GetControlPosition(mEdit2, posEdit2);

		mEdit2.ShowWindow(SW_HIDE);
		OverrideControlPosition(mEdit, posEdit2);

		// resize the list to close the gap where the primary edit used to be
		sGap = posEdit.bottom - posList.bottom;
		posList.bottom = posEdit.bottom;
		OverrideControlPosition(mFilterList, posList);
	}

	Layout();
}

void VAOpenFile::OnToggleDisplayPersistentFilter()
{
	if (Psettings->mOfisDisplayPersistentFilter)
	{
		// save filter text if visible and being hidden
		CStringW persistentFilter;
		mEdit2.GetText(persistentFilter);
		::wcscpy_s(Psettings->mOfisPersistentFilter, MAX_PATH, persistentFilter);
		Psettings->mOfisPersistentFilter[MAX_PATH - 1] = L'\0';
	}

	Psettings->mOfisDisplayPersistentFilter = !Psettings->mOfisDisplayPersistentFilter;

	if (Psettings->mOfisDisplayPersistentFilter)
	{
		// restore filter text if hidden and became visible
		CStringW persistentFilter(Psettings->mOfisPersistentFilter);
		persistentFilter.Trim();
		if (!persistentFilter.IsEmpty())
			mEdit2.SetText(persistentFilter);
	}

	RearrangeForPersistentFilterEditControl();
}

void VAOpenFile::OnToggleApplyPersistentFilter()
{
	Psettings->mOfisApplyPersistentFilter = !Psettings->mOfisApplyPersistentFilter;
	PopulateList();
}

void VAOpenFile::OnSelectAll()
{
	// Select all items in the file list
	for (int i = 0; i < mFilterList.GetItemCount(); ++i)
	{
		mFilterList.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
	}
}

void VAOpenFile::SetEditHelpText()
{
	if (Psettings->mForceCaseInsensitiveFilters)
	{
		CString txt(R"(substring andSubstring , orSubstring -exclude .beginWith endWith. \searchFullPath :openAtLine)");
		mEdit.SetDimText(txt);
		mEdit2.SetDimText(txt + " (persistent)");
	}
	else
	{
		CString txt(
		    R"(substring andsubstring , orsubstring matchCase -exclude .beginwith endwith. \searchfullpath :openatline)");
		mEdit.SetDimText(txt);
		mEdit2.SetDimText(txt + " (persistent)");
	}

	mEdit.Invalidate();
	mEdit2.Invalidate();
}

void VAOpenFile::OnCopyFilename()
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		return;
	}

	CStringW clipboard;
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	const uint selCnt = mFilterList.GetSelectedCount();
	if (selCnt == 1)
	{
		FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);
		clipboard = ::Basename(ptr->mFilename);
	}
	else
	{
		for (uint n = 0; n < selCnt; n++)
		{
			FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);
			mFilterList.GetNextSelectedItem(p);

			clipboard += ::Basename(ptr->mFilename);
			clipboard += _T("\r\n");
		}
	}

	::SaveToClipboard(m_hWnd, clipboard);
}

void VAOpenFile::OnCopyFullPath()
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		return;
	}

	CStringW clipboard;
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	const uint selCnt = mFilterList.GetSelectedCount();
	if (selCnt == 1)
	{
		FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);
		clipboard = ptr->mFilename;
	}
	else
	{
		for (uint n = 0; n < selCnt; n++)
		{
			FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);
			mFilterList.GetNextSelectedItem(p);

			clipboard += ptr->mFilename;
			clipboard += _T("\r\n");
		}
	}

	::SaveToClipboard(m_hWnd, clipboard);
}

void BrowseToFile(const CStringW& filename)
{
	if (filename.GetLength())
	{
		if (gTestsActive)
		{
			if (gTestLogger)
			{
				WTString msg;
				msg.WTFormat("BrowseToFile open location of file %s", (LPCTSTR)CString(::Basename(filename)));
				gTestLogger->LogStr(msg);
			}
		}
		else
		{
#pragma warning(push)
#pragma warning(disable : 4090)
			ITEMIDLIST* pidl = ILCreateFromPathW(filename);
#pragma warning(pop)
			if (pidl)
			{
				SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
				ILFree(pidl);
			}
		}
	}
}

void VAOpenFile::OnOpenContainingFolder()
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		return;
	}

	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	FileData* ptr = mFileData->GetAt((int)(intptr_t)p - 1);
	if (ptr)
	{
		BrowseToFile(ptr->mFilename);
	}
}

BOOL VAOpenFile::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == mFilterList.m_hWnd || pMsg->hwnd == mEdit.m_hWnd || pMsg->hwnd == mEdit2.m_hWnd)
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if ((GetKeyState(VK_CONTROL) & 0x1000))
			{
				if (pMsg->wParam == 'A')
				{
					// Ctrl+A
					if (pMsg->hwnd == mFilterList.m_hWnd)
					{
						// Select all items in the list
						OnSelectAll();
						return TRUE; // Handled the message
					}
				}
				if (pMsg->wParam == 'C')
				{
					// Ctrl+C
					OnCopyFullPath();
					return false;
				}
				else if (pMsg->wParam == 'D')
				{
					// Ctrl+D
					OnToggleAugmentSolution();
					return true;
				}
				else if (pMsg->wParam == 'E')
				{
					// Ctrl+E
					if (mFilterList.GetSelectedCount() == 1)
					{
						OnOpenContainingFolder();
						return true;
					}
				}
				else if (pMsg->wParam == 'F')
				{
					// Ctrl+F
					OnCopyFilename();
					return true;
				}
				else if (pMsg->wParam == 'H')
				{
					// Ctrl+H
					OnToggleIncludeHiddenExtensions();
					return true;
				}
				else if (pMsg->wParam == 'O')
				{
					// Ctrl+O
					OnToggleIncludeWindowList();
					return true;
				}
				else if (pMsg->wParam == 'P')
				{
					// Ctrl+P
					OnToggleIncludePrivateSystem();
					return true;
				}
				else if (pMsg->wParam == 'S')
				{
					// Ctrl+S
					OnToggleIncludeSolution();
					return true;
				}
				else if (pMsg->wParam == 'T')
				{
					// Ctrl+T
					OnToggleIncludeExternal();
					return true;
				}
				else if (pMsg->wParam == 'Y')
				{
					// Ctrl+Y
					OnToggleIncludeSystem();
					return true;
				}
				else if (pMsg->wParam == 'R')
				{
					// Ctrl+R
					OnToggleApplyPersistentFilter();
					return true;
				}
			}
		}
	}

	return __super::PreTranslateMessage(pMsg);
}
