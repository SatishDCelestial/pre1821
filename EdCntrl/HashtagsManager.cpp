#include "stdafxed.h"
#include "HashtagsManager.h"
#include "VAParse.h"
#include "PROJECT.H"
#include "file.h"
#include "Directories.h"
#include "ProjectInfo.h"
#include <codecvt>
#include "FileId.h"
#include <algorithm>
#include "RegKeys.h"
#include "DevShellService.h"

std::shared_ptr<HashtagsManager> gHashtagManager;

HashtagsManager::HashtagsManager()
{
}

void HashtagsManager::Reload()
{
	Unload();
	// Load solution rules first so HasIgnores can be used to tell if solution rules are found.
	LoadSolutionRules();
	LoadGlobalRules();
	//	LoadProjectRules();
}

void HashtagsManager::SolutionLoaded()
{
	Reload();
}

bool HashtagsManager::IsHashtagIgnored(DType& dt)
{
	return IsHashtagIgnored(dt.Sym(), dt.FileId());
}

bool HashtagsManager::IsHashtagIgnored(const WTString& tag, UINT fileId)
{
	AutoLockCs l(mRuleLock);

	bool isHashtagIgnored =
	    IsFileIgnored(fileId) || IsTagIgnored(tag) || IsDirectoryIgnored(fileId) || IsProjectIgnored(fileId);

	return isHashtagIgnored;
}

bool HashtagsManager::IgnoreTag(DType& dt, bool global)
{
	WTString tag = dt.Sym();

	return IgnoreTag(tag, global);
}
bool HashtagsManager::IgnoreTag(const WTString& tag, bool global)
{
	CStringW ruleTxt = L"t:" + tag.Wide();

	if (SaveRule(ruleTxt, global))
	{
		AutoLockCs l(mRuleLock);
		WTString txtLower = tag;
		txtLower.MakeLower();
		mIgnoredTags.push_back(txtLower);
	}

	return true;
}

bool HashtagsManager::IgnoreFile(DType& dt, bool global)
{
	CStringW filePath = dt.FilePath();

	CStringW ruleTxt = L"f:";
	if (global)
		ruleTxt += filePath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += filePath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(filePath, slnDir);
			ruleTxt += relPath;
		}
	}

	if (SaveRule(ruleTxt, global))
	{
		AutoLockCs l(mRuleLock);
		mIgnoredFileIds.push_back(dt.FileId());
	}

	return true;
}

bool HashtagsManager::IgnoreDirectory(DType& dt, bool global)
{
	CStringW dirPath = ::Path(dt.FilePath());
	dirPath = MSPath(dirPath);
	if (dirPath[dirPath.GetLength()] != L'\\')
		dirPath += "\\";

	CStringW ruleTxt = L"d:";
	if (global)
		ruleTxt += dirPath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += dirPath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(dirPath, slnDir);
			if (relPath.IsEmpty())
				relPath = ".";
			ruleTxt += relPath;
		}
	}

	if (SaveRule(ruleTxt, global))
	{
		AutoLockCs l(mRuleLock);
		CStringW txtLower = dirPath;
		txtLower.MakeLower();
		mIgnoredDirs.push_back(txtLower);
	}
	return true;
}

bool HashtagsManager::IgnoreProject(CStringW projectPath, bool global)
{
	CStringW ruleTxt = L"p:";
	if (global)
		ruleTxt += projectPath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += projectPath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(projectPath, slnDir);
			ruleTxt += relPath;
		}
	}

	if (SaveRule(ruleTxt, global))
	{
		AutoLockCs l(mRuleLock);
		CStringW txtLower = projectPath;
		txtLower.MakeLower();
		mIgnoredProjects.push_back(txtLower);
	}

	return true;
}

void HashtagsManager::Unload()
{
	AutoLockCs l(mRuleLock);
	mIgnoredFileIds.clear();
	mIgnoredDirs.clear();
	mIgnoredTags.clear();
	mIgnoredProjects.clear();
}

void HashtagsManager::AddRule(CStringW rule)
{
	rule.MakeLower();
	rule.Trim();

	CStringW prefix = rule.Left(2);
	CStringW body = rule.Mid(2);

	AutoLockCs l(mRuleLock);

	if (prefix == "//")
	{
		// comment line
	}
	else if (prefix == "d:")
	{
		CStringW solutionDir(::Path(GlobalProject->SolutionFile()));
		CStringW ignoredDir = ::BuildPath(body, solutionDir, false, true);
		if (ignoredDir.GetLength())
		{
			if (ignoredDir[ignoredDir.GetLength() - 1] != '\\')
				ignoredDir.AppendChar('\\');
			mIgnoredDirs.push_back(ignoredDir);
		}
	}
	else if (prefix == "f:")
	{
		CStringW solutionDir(::Path(GlobalProject->SolutionFile()));
		CStringW ignoredFile = ::BuildPath(body, solutionDir, true, true);
		if (ignoredFile.GetLength())
		{
			auto fileId = gFileIdManager->GetFileId(ignoredFile);
			mIgnoredFileIds.push_back(fileId);
		}
	}
	else if (prefix == "t:")
	{
		// includes '#'
		mIgnoredTags.push_back(body);
	}
	else if (prefix == "p:")
	{
		CStringW solutionDir(::Path(GlobalProject->SolutionFile()));
		CStringW ignoredProject = ::BuildPath(body, solutionDir, true, true);
		if (ignoredProject.GetLength())
		{
			mIgnoredProjects.push_back(ignoredProject);
		}
	}
	else
	{
		// error, ignore line
	}
}

CStringW HashtagsManager::GetGlobalRuleFileDir() const
{
	static CStringW dir;
	if (dir.IsEmpty())
	{
		dir = VaDirs::GetUserDir() + L"VAHashtags";
		CreateDir(dir);
		dir += L"\\Global";
		CreateDir(dir);
		dir += L"\\";
	}
	return dir;
}

CStringW HashtagsManager::GetDefaultGlobalRuleFilePath() const
{
	return GetGlobalRuleFileDir() + L".vahashtags";
}

CStringW HashtagsManager::GetUserSolutionRuleFileDir() const
{
	if (GlobalProject)
	{
		CStringW slnPath = GlobalProject->SolutionFile();
		if (!slnPath.IsEmpty())
		{
			if (IsFile(slnPath))
				slnPath = ::Path(slnPath);

			return slnPath + L"\\.va\\user\\";
		}
	}

	static CStringW dir;
	if (dir.IsEmpty())
	{
		dir = VaDirs::GetUserDir() + L"VAHashtags";
		CreateDir(dir);
		dir += L"\\EmptySln";
		CreateDir(dir);
		dir += L"\\";
	}
	return dir;
}

CStringW HashtagsManager::GetSharedSolutionRuleFileDir() const
{
	if (GlobalProject)
	{
		CStringW slnPath = GlobalProject->SolutionFile();
		if (!slnPath.IsEmpty())
		{
			if (IsFile(slnPath))
				slnPath = ::Path(slnPath);

			return slnPath + L"\\.va\\shared\\";
		}
	}

	static CStringW dir;
	if (dir.IsEmpty())
	{
		dir = VaDirs::GetUserDir() + L"VAHashtags";
		CreateDir(dir);
		dir += L"\\EmptySln";
		CreateDir(dir);
		dir += L"\\";
	}
	return dir;
}

CStringW HashtagsManager::GetDefaultSolutionRuleFilePath() const
{
	return GetUserSolutionRuleFileDir() + L".vahashtags";
}

void HashtagsManager::LoadGlobalRules()
{
	LoadRuleFilesInDirectory(GetGlobalRuleFileDir());
}

void HashtagsManager::LoadSolutionRules()
{
	LoadRuleFilesInDirectory(GetSharedSolutionRuleFileDir());
	LoadRuleFilesInDirectory(GetUserSolutionRuleFileDir());

	if (!HasIgnores())
	{
		// [case: 115362] copy any legacy rules from the solution directory to the .va\user directory and try loading
		// again
		CopyLegacyRulesToUserDir();
		LoadRuleFilesInDirectory(GetUserSolutionRuleFileDir());
	}
}

void HashtagsManager::LoadProjectRules()
{
	if (GlobalProject)
	{
		RWLockReader lck;
		const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
		for (auto item : projMap)
		{
			CStringW projectPath = item.second->GetProjectFile();
			CStringW projectDir = ::Path(projectPath);
			LoadRuleFilesInDirectory(projectDir);
		}
	}
}

void HashtagsManager::CopyLegacyRulesToUserDir()
{
	if (GlobalProject)
	{
		CStringW slnPath = GlobalProject->SolutionFile();

		if (!slnPath.IsEmpty())
		{
			if (IsFile(slnPath))
				slnPath = Path(slnPath);

			FileList files;
			FindFiles(slnPath, L"*.vahashtags", files, false);

			if (files.size())
			{
				CStringW vaUserDir = slnPath + L"\\.va\\user\\";

				if (CreateDir(vaUserDir) != 0)
					SetFileAttributesW(slnPath + L"\\.va\\", FILE_ATTRIBUTE_HIDDEN);

				for (const auto& fileInfo : files)
					DuplicateFile(fileInfo.mFilename, vaUserDir + GetBaseName(fileInfo.mFilename));
			}
		}
	}
}

void HashtagsManager::LoadRuleFilesInDirectory(CStringW directory)
{
	if (directory.IsEmpty())
		return;

	FileList files;
	::FindFiles(directory, L"*.vahashtags", files, false);
	for (const auto& fileInfo : files)
		LoadRuleFile(fileInfo.mFilename);
}

void HashtagsManager::LoadRuleFile(CStringW ruleFilePath)
{
	std::wifstream ifs(ruleFilePath);
	if (!ifs)
		return;

	// Read as UTF8, consume BOM
	ifs.imbue(std::locale(ifs.getloc(), new std::codecvt_utf8<wchar_t, 0x10ffff, std::consume_header>()));

	std::wstring line;
	while (std::getline(ifs, line))
	{
		AddRule(CStringW(line.c_str()));
	}
}

bool HashtagsManager::SaveRule(CStringW rule, bool global)
{
	CStringW ruleFilePath = global ? GetDefaultGlobalRuleFilePath() : GetDefaultSolutionRuleFilePath();

	if (ruleFilePath.IsEmpty())
	{
		_ASSERTE(!"shouldn't happen");
		return false;
	}

	CStringW ruleFileDir = ::Path(ruleFilePath);

	if (CreateDir(ruleFileDir) != 0)
	{
		int startOfVaDir = ruleFileDir.Find(L"\\.va\\");

		if (startOfVaDir != -1)
		{
			ruleFileDir = ruleFileDir.Mid(0, startOfVaDir);
			ruleFileDir += L"\\.va\\";
			SetFileAttributesW(ruleFileDir, FILE_ATTRIBUTE_HIDDEN);
		}
	}

	std::wofstream ofs;
	ofs.open(ruleFilePath, std::ofstream::out | std::ios_base::app);
	if (ofs)
	{
		// write as UTF8, no BOM
		ofs.imbue(std::locale(ofs.getloc(), new std::codecvt_utf8<wchar_t, 0x10ffff, std::consume_header>()));
		ofs << std::endl << (LPCWSTR)rule;
		return true;
	}
	else
	{
		CStringW msg;
		CString__FormatW(msg, L"Unable to write to rule file %s", (LPCWSTR)ruleFilePath);
		::WtMessageBox(msg, CStringW(IDS_APPNAME), MB_OK | MB_ICONERROR);
		return false;
	}
}

bool HashtagsManager::DeleteRuleFromFile(CStringW rule, bool global)
{
	CStringW ruleFilePath = global ? GetDefaultGlobalRuleFilePath() : GetDefaultSolutionRuleFilePath();

	if (ruleFilePath.IsEmpty())
	{
		_ASSERTE(!"shouldn't happen");
		return false;
	}

	std::vector<std::wstring> file;
	std::wstring temp;

	std::wifstream ifs(ruleFilePath);
	while (!ifs.eof())
	{
		std::getline(ifs, temp);
		if (temp != L"")
		{
			file.push_back(temp);
		}
	}
	ifs.close();

	std::wstring item(rule);

	for (size_t i = 0; i < file.size(); i++)
	{
		if (_wcsicmp(file[i].substr(0, file[i].size()).c_str(), item.c_str()) == 0)
		{
			file.erase(file.begin() + (int)i);
			i = 0;
		}
	}

	std::wofstream ofs;
	ofs.open(ruleFilePath, std::ofstream::out | std::ios::trunc);
	if (ofs)
	{
		// write as UTF8, no BOM
		ofs.imbue(std::locale(ofs.getloc(), new std::codecvt_utf8<wchar_t, 0x10ffff, std::consume_header>()));
		for (std::vector<std::wstring>::const_iterator itVect = file.begin(); itVect != file.end(); itVect++)
		{
			ofs << std::endl << *itVect;
		}
		ofs.close();

		return true;
	}
	else
	{
		CStringW msg;
		CString__FormatW(msg, L"Unable to write to rule file %s", (LPCWSTR)ruleFilePath);
		::WtMessageBox(msg, CStringW(IDS_APPNAME), MB_OK | MB_ICONERROR);
		return false;
	}
}

int HashtagsManager::GetIgnoredRuleLocation(CStringW rule, CStringW prefix)
{
	rule.MakeLower();

	int retVal = IgnoreStatuses::NotIgnored;

	std::wstring ruleTemp(rule);
	std::wstring prefixTemp(prefix);

	std::vector<std::wstring> fileLocal;
	std::wstring tempLocal;
	std::wifstream ifsLocal(GetDefaultSolutionRuleFilePath());
	if (!ifsLocal.fail())
	{
		while (!ifsLocal.eof())
		{
			std::getline(ifsLocal, tempLocal);
			if (tempLocal != L"" && tempLocal.substr(0, 2) == prefixTemp)
			{
				std::wstring tmpElem = tempLocal.substr(2, tempLocal.size() - 2);
				std::transform(tmpElem.begin(), tmpElem.end(), tmpElem.begin(), ::towlower);
				fileLocal.push_back(tmpElem);
			}
		}
	}
	if (ifsLocal.is_open())
	{
		ifsLocal.close();
	}

	std::vector<std::wstring> fileGlobal;
	std::wstring tempGlobal;
	std::wifstream ifsGlobal(GetDefaultGlobalRuleFilePath());
	if (!ifsGlobal.fail())
	{
		while (!ifsGlobal.eof())
		{
			std::getline(ifsGlobal, tempGlobal);
			if (tempGlobal != L"" && tempGlobal.substr(0, 2) == prefixTemp)
			{
				std::wstring tmpElem = tempGlobal.substr(2, tempGlobal.size() - 2);
				std::transform(tmpElem.begin(), tmpElem.end(), tmpElem.begin(), ::towlower);
				fileGlobal.push_back(tmpElem);
			}
		}
	}
	if (ifsGlobal.is_open())
	{
		ifsGlobal.close();
	}

	if (prefix == L"t:") // tag
	{
		for (const std::wstring& element : fileLocal)
		{
			if (element == ruleTemp)
			{
				retVal |= IgnoreStatuses::IgnoredLocal;
				break;
			}
		}

		for (const std::wstring& element : fileGlobal)
		{
			if (element == ruleTemp)
			{
				retVal |= IgnoreStatuses::IgnoredGlobal;
				break;
			}
		}
	}
	else if (prefix == L"f:") // file
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		slnDir.MakeLower();
		CStringW filePath;
		if (slnDir.IsEmpty())
		{
			filePath = rule;
		}
		else
		{
			filePath = BuildRelativePath(rule, slnDir);
		}

		for (const std::wstring& element : fileLocal)
		{
			if (element == std::wstring(filePath))
			{
				retVal |= IgnoreStatuses::IgnoredLocal;
				break;
			}
		}

		filePath = ::BuildPath(rule, slnDir);

		for (const std::wstring& element : fileGlobal)
		{
			if (element == std::wstring(filePath))
			{
				retVal |= IgnoreStatuses::IgnoredGlobal;
				break;
			}
		}
	}
	else if (prefix == L"d:") // directory
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		slnDir.MakeLower();
		CStringW dirPath;
		if (slnDir.IsEmpty())
		{
			dirPath = rule;
		}
		else
		{
			dirPath = BuildRelativePath(rule, slnDir);
			if (dirPath.IsEmpty())
				dirPath = ".";
		}

		for (const std::wstring& element : fileLocal)
		{
			if (element == std::wstring(dirPath))
			{
				retVal |= IgnoreStatuses::IgnoredLocal;
				break;
			}
		}

		dirPath = ::BuildPath(rule, slnDir, false) + L"\\";

		for (const std::wstring& element : fileGlobal)
		{
			if (element == std::wstring(dirPath))
			{
				retVal |= IgnoreStatuses::IgnoredGlobal;
				break;
			}
		}
	}
	else if (prefix == L"p:") // project
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		slnDir.MakeLower();
		CStringW solutionPath;
		if (slnDir.IsEmpty())
		{
			solutionPath = rule;
		}
		else
		{
			solutionPath = BuildRelativePath(rule, slnDir);
		}

		for (const std::wstring& element : fileLocal)
		{
			if (element == std::wstring(solutionPath))
			{
				retVal |= IgnoreStatuses::IgnoredLocal;
				break;
			}
		}

		solutionPath = ::BuildPath(rule, slnDir);

		for (const std::wstring& element : fileGlobal)
		{
			if (element == std::wstring(solutionPath))
			{
				retVal |= IgnoreStatuses::IgnoredGlobal;
				break;
			}
		}
	}

	return retVal;
}

bool HashtagsManager::CanClearIgnores(bool global)
{
	CStringW ruleFilePath = global ? GetDefaultGlobalRuleFilePath() : GetDefaultSolutionRuleFilePath();
	return (GetFSize(ruleFilePath) > 0);
}

bool HashtagsManager::ClearIgnores(bool global)
{
	CStringW ruleFilePath = global ? GetDefaultGlobalRuleFilePath() : GetDefaultSolutionRuleFilePath();

	// truncate file, if it exists
	if (!::IsFile(ruleFilePath))
		return true;

	std::wofstream ofs;
	ofs.open(ruleFilePath, std::ofstream::out | std::ofstream::trunc);
	if (ofs)
	{
		Reload();
		return true;
	}
	else
	{
		CStringW msg;
		CString__FormatW(msg, L"Unable to write to rule file %s", (LPCWSTR)ruleFilePath);
		::WtMessageBox(msg, CStringW(IDS_APPNAME), MB_OK | MB_ICONERROR);
		return false;
	}
}

bool HashtagsManager::HasIgnores()
{
	AutoLockCs l(mRuleLock);
	return !mIgnoredFileIds.empty() || !mIgnoredDirs.empty() || !mIgnoredTags.empty() || !mIgnoredProjects.empty();
}

bool HashtagsManager::IsTagIgnored(const WTString& tag, int* pIgnoredRuleLocation)
{
	if (std::find_if(mIgnoredTags.begin(), mIgnoredTags.end(),
	                 [&](const WTString& str) { return str.CompareNoCase(tag) == 0; }) != mIgnoredTags.end())
	{
		if (pIgnoredRuleLocation != nullptr)
		{
			*pIgnoredRuleLocation = GetIgnoredRuleLocation(tag.Wide(), L"t:");
		}

		return true;
	}

	return false;
}

bool HashtagsManager::IsFileIgnored(UINT fileId, int* pIgnoredRuleLocation)
{
	if (std::find(mIgnoredFileIds.begin(), mIgnoredFileIds.end(), fileId) != mIgnoredFileIds.end())
	{
		if (pIgnoredRuleLocation != nullptr)
		{
			auto filePath = gFileIdManager->GetFile(fileId);
			*pIgnoredRuleLocation = GetIgnoredRuleLocation(filePath, L"f:");
		}

		return true;
	}

	return false;
}

bool HashtagsManager::IsDirectoryIgnored(UINT fileId, int* pIgnoredRuleLocation)
{
	if (mIgnoredDirs.size())
	{
		auto filePath = gFileIdManager->GetFile(fileId);
		if (!filePath.IsEmpty())
		{
			for (const auto& ignoredDir : mIgnoredDirs)
			{
				if (0 == StrCmpNIW(filePath, ignoredDir, ignoredDir.GetLength()))
				{
					if (pIgnoredRuleLocation != nullptr)
					{
						*pIgnoredRuleLocation = GetIgnoredRuleLocation(ignoredDir, L"d:");
					}

					return true;
				}
			}
		}
	}

	return false;
}

bool HashtagsManager::IsProjectIgnored(UINT fileId, int* pIgnoredRuleLocation)
{
	if (mIgnoredProjects.size())
	{
		if (GlobalProject)
		{
			ProjectVec projectVec = GlobalProject->GetProjectForFile(fileId);
			for (auto project : projectVec)
			{
				CStringW projectFile = project->GetProjectFile();
				for (auto ignoredProject : mIgnoredProjects)
				{
					if (projectFile.CompareNoCase(ignoredProject) == 0)
					{
						if (pIgnoredRuleLocation != nullptr)
						{

							*pIgnoredRuleLocation = GetIgnoredRuleLocation(projectFile, L"p:");
						}

						return true;
					}
				}
			}
		}
	}

	return false;
}

bool HashtagsManager::ClearIgnoreTag(DType& dt, bool global)
{
	WTString tag = dt.Sym();

	return ClearIgnoreTag(tag, global);
}

bool HashtagsManager::ClearIgnoreTag(const WTString& tag, bool global)
{
	CStringW ruleTxt = L"t:" + tag.Wide();

	if (DeleteRuleFromFile(ruleTxt, global))
	{
		if (GetIgnoredRuleLocation(ruleTxt.Mid(2), L"t:") == IgnoreStatuses::NotIgnored)
		{
			AutoLockCs l(mRuleLock);
			WTString txtLower = tag;
			txtLower.MakeLower();
			mIgnoredTags.erase(std::remove(mIgnoredTags.begin(), mIgnoredTags.end(), txtLower), mIgnoredTags.end());
		}
	}

	return true;
}

bool HashtagsManager::ClearIgnoreFile(DType& dt, bool global)
{
	CStringW filePath = dt.FilePath();

	CStringW ruleTxt = L"f:";
	if (global)
		ruleTxt += filePath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += filePath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(filePath, slnDir);
			ruleTxt += relPath;
		}
	}

	if (DeleteRuleFromFile(ruleTxt, global))
	{
		if (GetIgnoredRuleLocation(ruleTxt.Mid(2), L"f:") == IgnoreStatuses::NotIgnored)
		{
			AutoLockCs l(mRuleLock);
			mIgnoredFileIds.erase(std::remove(mIgnoredFileIds.begin(), mIgnoredFileIds.end(), dt.FileId()),
			                      mIgnoredFileIds.end());
		}
	}

	return true;
}

bool HashtagsManager::ClearIgnoreDirectory(DType& dt, bool global)
{
	CStringW dirPath = ::Path(dt.FilePath());
	dirPath = MSPath(dirPath);
	if (dirPath[dirPath.GetLength()] != L'\\')
		dirPath += "\\";

	CStringW ruleTxt = L"d:";
	if (global)
		ruleTxt += dirPath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += dirPath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(dirPath, slnDir);
			if (relPath.IsEmpty())
				relPath = ".";
			ruleTxt += relPath;
		}
	}

	if (DeleteRuleFromFile(ruleTxt, global))
	{
		if (GetIgnoredRuleLocation(ruleTxt.Mid(2), L"d:") == IgnoreStatuses::NotIgnored)
		{
			AutoLockCs l(mRuleLock);
			CStringW txtLower = dirPath;
			txtLower.MakeLower();
			mIgnoredDirs.erase(std::remove(mIgnoredDirs.begin(), mIgnoredDirs.end(), txtLower), mIgnoredDirs.end());
		}
	}
	return true;
}

bool HashtagsManager::ClearIgnoreProject(CStringW projectPath, bool global)
{
	CStringW ruleTxt = L"p:";
	if (global)
		ruleTxt += projectPath;
	else
	{
		CStringW slnDir = ::Path(GlobalProject->SolutionFile());
		if (slnDir.IsEmpty())
		{
			ruleTxt += projectPath;
		}
		else
		{
			CStringW relPath = BuildRelativePath(projectPath, slnDir);
			ruleTxt += relPath;
		}
	}

	if (DeleteRuleFromFile(ruleTxt, global))
	{
		if (GetIgnoredRuleLocation(ruleTxt.Mid(2), L"p:") == IgnoreStatuses::NotIgnored)
		{
			AutoLockCs l(mRuleLock);
			CStringW txtLower = projectPath;
			txtLower.MakeLower();
			mIgnoredProjects.erase(std::remove(mIgnoredProjects.begin(), mIgnoredProjects.end(), txtLower),
			                       mIgnoredProjects.end());
		}
	}

	return true;
}
