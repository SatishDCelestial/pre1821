// GotoDlg1.cpp : implementation file
//

#include "stdafxed.h"
#include <ppl.h>
#include "GotoDlg1.h"
#include "StatusWnd.h"
#include "Registry.h"
#include "FileTypes.h"
#include "RegKeys.h"
#include "Settings.h"
#include "project.h"
#include "TokenW.h"
#include "WtException.h"
#include "FDictionary.h"
#include "Mparse.h"
#include "FILE.H"
#include "DTypeDbScope.h"
#include "FileId.h"
#include "LogElapsedTime.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

ScanSystemSourceDirs::ScanSystemSourceDirs()
{
	LOG2("ScanSystemSourceDirs ctor");
	mExtensionsList = L";" + CStringW(Psettings->m_srcExts) + L".inl;";

	IncludeDirs incTok;
	CStringW tmp(L";" + incTok.getSourceDirs());
	if (tmp.GetLength() < 3)
	{
		// if nothing from incTok, directly check registry
		tmp = L";" + ::GetRegValueW(HKEY_CURRENT_USER,
		                            (WTString(ID_RK_APP) + WTString("\\") + Psettings->m_platformIncludeKey).c_str(),
		                            ID_RK_GOTOSRCDIRS);
	}
	tmp = WTExpandEnvironmentStrings(tmp);
	TokenW dirTok = tmp;
	while (dirTok.more() > 2 && !gShellIsUnloading)
	{
		CStringW searchDir = dirTok.read(L";\r\n");
		mDirs.push_back(searchDir);
	}

	SetStatus(WTString("Finding system source files..."));
	BuildListOfFiles();

	if (!gShellIsUnloading && mFiles.size())
		ParseFiles();

	SetStatus("Done.");
}

void ScanDirsForFiles::BuildListOfFiles()
{
	LogElapsedTime let("BuildListOfFiles", 1000);
	_ASSERTE(mExtensionsList.IsEmpty() || mExtensionsList[0] == L';');
	for (auto dir : mDirs)
		SearchDir(dir);
}

void ScanDirsForFiles::SearchDir(CStringW searchDir)
{
	if (!mCollectFiles && StopIt)
		return;

	if (mCancellationMonitor && *mCancellationMonitor)
		return;

	if (searchDir.IsEmpty())
	{
		_ASSERTE(!"don't pass empty strings to SearchDir");
		return;
	}

	const UINT dirId = gFileIdManager->GetFileId(searchDir);
	_ASSERTE(dirId);
	if (mSearchedDirs.find(dirId) != mSearchedDirs.end())
		return;
	mSearchedDirs.insert(dirId);

	const CStringW searchSpec(searchDir + L"\\*.*");
	WIN32_FIND_DATAW fileData;
	HANDLE hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	CStringW searchDirLower(searchDir);
	searchDirLower.MakeLower();

	do
	{
		if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
			continue;

		if (FILE_ATTRIBUTE_HIDDEN & fileData.dwFileAttributes)
			continue;

		if (fileData.cFileName[0] == L'.')
		{
			// .vs / .suo / .gitignore / etc
			continue;
		}

		if (FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes)
		{
			if (mRecurseDirs)
			{
				const CStringW curFile(fileData.cFileName);
				if (curFile == L"." || curFile == L"..")
					continue;

				if (mCollectFiles && mSkipSomeBoostDirs && -1 != searchDirLower.Find(L"boost"))
				{
					if (!curFile.CompareNoCase(L"detail") || !curFile.CompareNoCase(L"aux") ||
					    !curFile.CompareNoCase(L"predef") || !curFile.CompareNoCase(L"preprocessed"))
					{
						// don't do full recursive search of boost
						continue;
					}
				}

				SearchDir(searchDir + L"\\" + curFile);
			}

			continue;
		}

		if (mCollectFiles && !mExtensionsList.IsEmpty())
		{
			CStringW ext(L".");
			ext += ::GetBaseNameExt(fileData.cFileName);
			if (-1 == mExtensionsList.Find(L";" + ext + L";"))
				continue;
		}

		CStringW fileAndPath;
		CString__FormatW(fileAndPath, L"%s\\%s", (LPCWSTR)searchDir, fileData.cFileName);
		if (mCollectFiles)
		{
			if (mConfirmUniqueAdd)
				mFiles.AddUniqueNoCase(fileAndPath);
			else
				mFiles.Add(fileAndPath);
		}
		else if (gFileIdManager)
			gFileIdManager->GetFileId(fileAndPath);

		if (mCancellationMonitor && *mCancellationMonitor)
			break;
	} while (FindNextFileW(hFile, &fileData) && !gShellIsUnloading);

	FindClose(hFile);
}

void ScanSystemSourceDirs::ParseFiles()
{
	SetStatus(WTString("Parsing system source files..."));

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		auto psr = [](const FileInfo& fi) {
			if (gShellIsUnloading)
				return;

			MultiParsePtr mp = MultiParse::Create();
			mp->ParseFileForGoToDef(fi.mFilename, TRUE);
		};

		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(mFiles.cbegin(), mFiles.cend(), psr);
		else
			std::for_each(mFiles.cbegin(), mFiles.cend(), psr);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("SSD_Run:");
		_ASSERTE(!"exception during ScanSourceDir");
	}
#endif // !SEAN
}

ScanSolutionPrivateSystemHeaderDirs::ScanSolutionPrivateSystemHeaderDirs(const FileList& dirs)
{
	LOG2("ScanSolutionPrivateSystemHeaderDirs ctor");
	mRecurseDirs = true;

	for (auto f : dirs)
		mDirs.push_back(f.mFilename);

	mExtensionsList = L";" + CStringW(Psettings->m_hdrExts);

	BuildListOfFiles();
}

bool ScanSolutionPrivateSystemHeaderDirs::ParseFiles()
{
	bool retval = false;
	SetStatus(WTString("Parsing solution package include files..."));

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		std::set<UINT> fileIds;

		std::for_each(mFiles.cbegin(), mFiles.cend(), [&fileIds](const FileInfo& fi) {
			if (gShellIsUnloading || StopIt)
				return;

			try
			{
				// extract of IsIncluded
				DType* fileData = GetSysDic()->GetFileData(fi.mFilename);
				if (fileData && !fileData->IsDbSolutionPrivateSystem())
				{
					fileIds.insert(fi.mFileId);
					vLog("WARN: removing solution private system header from general cpp %s",
					     (LPCTSTR)CString(fi.mFilename));
				}
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
				CString__FormatA(msg, "VA ERROR exception caught SSHD::PF %d\n", __LINE__);
				OutputDebugString(msg);
				_ASSERTE(!"ScanSolutionPrivateSystemHeaderDirs::ParseFiles exception");
				VALOGEXCEPTION("SSHD::PF:");
			}
#endif // !SEAN
		});

		if (fileIds.size())
		{
			MultiParsePtr mp = MultiParse::Create(Src);
			mp->RemoveAllDefs(fileIds, DTypeDbScope::dbSlnAndSys);
		}

		auto prsr = [&retval](const FileInfo& fi) {
			if (gShellIsUnloading || StopIt)
				return;

			try
			{
				// extract of IsIncluded
				DType* fileData = GetSysDic()->GetFileData(fi.mFilename);
				if (fileData)
					return;

				if (-1 != fi.mFilenameLower.Find(L"boost"))
				{
					// don't preparse boost dirs
					return;
				}

				retval = true;
				MultiParsePtr mp2 = MultiParse::Create();
				mp2->FormatFile(fi.mFilename, V_SYSLIB, ParseType_Globals, false, nullptr,
				                VA_DB_SolutionPrivateSystem | VA_DB_Cpp);
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
				CString__FormatA(msg, "VA ERROR exception caught SSHD::PF %d\n", __LINE__);
				OutputDebugString(msg);
				_ASSERTE(!"ScanSolutionPrivateSystemHeaderDirs::ParseFiles exception");
				VALOGEXCEPTION("SSHD::PF:");
			}
#endif // !SEAN
		};

		if (Psettings->mUsePpl)
			Concurrency::parallel_for_each(mFiles.cbegin(), mFiles.cend(), prsr);
		else
			std::for_each(mFiles.cbegin(), mFiles.cend(), prsr);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("SSHD::PF:");
		_ASSERTE(!"exception during ScanSolutionPrivateSystemHeaderDirs");
	}
#endif // !SEAN

	SetStatus("Done.");
	return retval;
}

SimpleRecursiveFileScan::SimpleRecursiveFileScan() : ScanDirsForFiles()
{
	LOG2("SimpleRecursiveFileScan ctor");
	// [case: 1029] for Psettings->mOfisIncludeSystem support
	// just populate fild id manager with fileids of files in these directories; no parse
	mCollectFiles = false;
	mRecurseDirs = true;
	mExtensionsList = L";";

	IncludeDirs id;
	CStringW tmp(L";");
	tmp += id.getSysIncludes();
	_ASSERTE(tmp[tmp.GetLength() - 1] == L';');
	tmp += id.getAdditionalIncludes();
	_ASSERTE(tmp[tmp.GetLength() - 1] == L';');
	tmp += id.getSourceDirs();
	_ASSERTE(tmp[tmp.GetLength() - 1] == L';');
	tmp += id.getImportDirs();
	_ASSERTE(tmp[tmp.GetLength() - 1] == L';');

	TokenW dirTok = tmp;
	while (dirTok.more() > 2 && !gShellIsUnloading)
	{
		CStringW searchDir = dirTok.read(L";\r\n");
		mDirs.push_back(searchDir);
	}

	if (StopIt)
		return;

	BuildListOfFiles();
}

RecursiveFileCollection::RecursiveFileCollection(const FileList& dirs, bool skipBoostDirs /*= true*/,
                                                 bool confirmUniqueAdd /*= true*/, int* cancelMonitor /*= nullptr*/)
{
	LOG2("RecursiveFileCollection ctor");
	mCollectFiles = true;
	mConfirmUniqueAdd = confirmUniqueAdd;
	mSkipSomeBoostDirs = skipBoostDirs;
	mRecurseDirs = true;
	mExtensionsList.Empty();
	mCancellationMonitor = cancelMonitor;

	for (auto cur : dirs)
		mDirs.push_back(cur.mFilename);

	if (StopIt)
		return;

	BuildListOfFiles();
}
