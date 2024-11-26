#include "stdafxed.h"
#include "FileId.h"
#include "WTString.h"
#include "fdictionary.h"
#include "Lock.h"
#include "file.h"
#include "project.h"
#include "..\common\call_mem_fun.h"
#include <algorithm>
#include "wt_stdlib.h"
#include "Directories.h"
#include "CFileW.h"
#include "FileTypes.h"
#include "StringUtils.h"
#include "ParseWorkItem.h"
#include "ParseThrd.h"
#include "Mparse.h"
#include "PrivateHeapStringMgr.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

FileIdManager* gFileIdManager = NULL;
const CStringW kFileIdDb("fileids.va");
#define FILE_ID_DATA_FILE (VaDirs::GetDbDir() + kFileIdDb)
Win32Heap gFileIdMgrHeapAllocator;
PrivateHeapStringMgr gFileIdStringManager(2, "FidStrs");

void InitFileIdManager()
{
	if (!gFileIdMgrHeapAllocator.IsCreated())
		gFileIdMgrHeapAllocator.Create(1 * 1024 * 1024, "FidMap");
	if (!gFileIdManager)
		gFileIdManager = new FileIdManager;
}

void ClearFileIdManager()
{
	auto tmp = gFileIdManager;
	gFileIdManager = nullptr;
	delete tmp;
#if !defined(VA_CPPUNIT)
	gFileIdMgrHeapAllocator.Close();
#endif
}

FileIdManager::FileIdManager() : mModified(0)
{
	mLastModTime.dwHighDateTime = mLastModTime.dwLowDateTime = 0xffffffff;
	_ASSERTE(!gFileIdManager);
	DoLoad();
}

FileIdManager::~FileIdManager()
{
	if (this == gFileIdManager)
		gFileIdManager = nullptr;

	if (mModified)
		Save();
}

UINT FileIdManager::GetFileId(const CStringW& filenameAndPath)
{
	if (filenameAndPath.IsEmpty())
		return 0;

	if (filenameAndPath[1] != L':' && filenameAndPath[0] != L'\\' && filenameAndPath[0] != L'/')
	{
		// hack for .NET symbols used in GlobalDataFile macro
		// filenameAndPath in this case is not a file but something like
		// System.Something.Another
		return ::WTHashKeyW(filenameAndPath);
	}

	// cast to LPCWSTR to force private allocation
	const FileIdStringW mixedFilename((LPCWSTR)filenameAndPath, filenameAndPath.GetLength(), GetStringManager());
	FileIdStringW filenameLower(mixedFilename);
	filenameLower.MakeLower();
	const UINT id = ::WTHashKeyW(filenameLower);

	FileIdStringW tmp;

	{
		RWLockLiteReader l(mLock);
		auto it = mIdToName.find(id);
		if (it != mIdToName.end())
			tmp = it->second->mFilenameLower;
	}

	if (tmp.IsEmpty())
	{
		// no entry exists, so add new entry using default id
		RWLockLiteWriter l(mLock);

		// re-check because we had to get the write lock -- another writer might have beat us
		auto it = mIdToName.find(id);
		if (it != mIdToName.end())
			tmp = it->second->mFilenameLower;

		if (tmp.IsEmpty())
		{
			_ASSERTE(!mNameToId[filenameLower]); // integrity check
			AddFile(mixedFilename, filenameLower, id);
			_ASSERTE(!mIdToName[id]->mFilenameLower.IsEmpty()); // success check
			return id;
		}
	}

	if (tmp == filenameLower)
		return id; // found match

	// id collision - do reverse lookup by name to see if already set up
	RWLockLiteWriter l(mLock);
	auto it = mNameToId.find(filenameLower);
	if (it != mNameToId.end())
		return it->second; // already in db

	// create non-colliding id and add entry
	vLog("fid::GFid '%S' collision with '%S'", (LPCWSTR)filenameLower,
	     (LPCWSTR)mIdToName[id]->mFilenameLower);
	const UINT altId = AddFile(mixedFilename, filenameLower);
	_ASSERTE(mNameToId[filenameLower] == altId);
	_ASSERTE(mIdToName[altId]->mFilenameLower == filenameLower);
	return altId;
}

UINT FileIdManager::GetIdFromFileId(const char *fileIdStr)
{
	assert(fileIdStr);
	if (strncmp(fileIdStr, "fileid:", 7))
		return 0;

	fileIdStr += 7;
	return strtoul(fileIdStr, nullptr, 16);
}

std::pair<CStringW, CStringW> FileIdManager::GetFile2(UINT fileId)
{
	FileIdStringW f;
	RWLockLiteReader l(mLock);
	// use find instead of operator[] so that size of both maps is the same
	auto it = mIdToName.find(fileId);
	if (it != mIdToName.end())
		return {it->second->mFilenameMixed, it->second->mFilenameLower};
	return {};
}
CStringW FileIdManager::GetFile(UINT fileId)
{
	auto [mixed, _] = GetFile2(fileId);
	return mixed;
}

void FileIdManager::IterateAll(std::function<void(uint fileId, const CStringW& filename)> fnc)
{
	// need to take write lock since we don't know if the caller will need either read or write lock
	// (re-entrancy is supported, but write must be taken before read)
	RWLockLiteWriter l(mLock);

	auto MapItemAdapter = [&](const std::pair<const UINT, FilenamesPtr>& pr) { fnc(pr.first, pr.second->mFilenameMixed); };

	// don't make concurrent because so many operations call back into the FileIdManager
	std::for_each(mIdToName.begin(), mIdToName.end(), MapItemAdapter);
}

CStringW FileIdManager::GetFileForUser(UINT fileId)
{
	const auto [file, file_lower] = GetFile2(fileId);
	const int ftype = GetFileType(file_lower);
	if (Other == ftype)
	{
		// [case=13907] don't return db files
		static CStringW sDirLower = VaDirs::GetDbDir();
		static bool once = true;
		if (once)
		{
			once = false;
			sDirLower.MakeLower();
		}

		if (::StrStrW(file_lower, sDirLower))
			return CStringW();
	}

	return file;
}

CStringW FileIdManager::GetFileForUser(const CStringW& fileId)
{
	CStringW fileIdStr(fileId);
	int pos = -1;
	if (fileIdStr[0] == L'f')
	{
		pos = fileIdStr.Find(L"fileid:");
		if (pos != -1)
			fileIdStr = fileIdStr.Mid(pos + 7);
	}

	UINT fId = 0;
	::swscanf(fileIdStr, L"%x", &fId);
	if (fId)
		return GetFileForUser(fId);
	return CStringW();
}

CStringW FileIdManager::GetFile(LPCSTR fileIdStr)
{
	UINT fId = GetIdFromFileId(fileIdStr);
	if (fId)
		return GetFile(fId);
	return CStringW();
}

UINT FileIdManager::AddFile(const FileIdStringW& mixedCaseName, const FileIdStringW& lowerName)
{
	mLock.AssertWriterIsThisThread();
	// create new id
	UINT id = ::WTHashKeyW(lowerName);
	for (int idx = 0; idx < 100; ++idx, ++id)
	{
		FilenamesPtr &fptr = mIdToName[id];
		const FileIdStringW curLower(fptr ? fptr->mFilenameLower : FileIdStringW());
		if (curLower.IsEmpty())
			return AddFile(mixedCaseName, lowerName, id);
		_ASSERTE(curLower != lowerName);
	}

	vLog("ERROR: fid::AF failed id for '%S'", (LPCWSTR)lowerName);
	_ASSERTE(!"FileIdManager::AddFile failed to create fileId");
	return (UINT)-1;
}

UINT FileIdManager::AddFile(const FileIdStringW& mixedCaseName, const FileIdStringW& lowerName, UINT id)
{
	mLock.AssertWriterIsThisThread();
	_ASSERTE(id);
	++mModified;
	_ASSERTE(mixedCaseName[mixedCaseName.GetLength() - 1] != L'\r' &&
	         mixedCaseName[mixedCaseName.GetLength() - 1] != L'\n');
	mIdToName[id] = std::make_unique<Filenames>(lowerName, mixedCaseName);
	mNameToId[lowerName] = id;
	_ASSERTE("FileIdManager::AddFile()" && mNameToId.size() == mIdToName.size());
	return id;
}

class FileIdManager::FileWriter
{
	CFileW mFile;
	CStringW mLn;
	FileIdManager::FileIdToNameMap& mIdsToName;

  public:
	FileWriter(const CStringW& oFile, FileIdManager::FileIdToNameMap& idsToName) : mIdsToName(idsToName)
	{
		for (int idx = 0; idx < 10; ++idx)
		{
			if (mFile.Open(oFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
				break;
			::Sleep(250);
		}
	}
	~FileWriter()
	{
		mFile.Close();
	}

	void Write(std::pair<const FileIdStringW, UINT>& it)
	{
		// serialize only the mixed case name - need to reverse lookup based
		// on given id
		const FileIdStringW mixedCaseName(mIdsToName[it.second]->mFilenameMixed);
		CString__FormatW(mLn, L"%ls" DB_FIELD_DELIMITER_STRW L"%x\n", (LPCWSTR)mixedCaseName, it.second);
		mFile.Write(mLn, mLn.GetLength() * sizeof(WCHAR));
	}
};

void FileIdManager::Save()
{
	RWLockLiteReader l(mLock);
	if (!mModified)
		return;

	_ASSERTE("FileIdManager::Save()" && mNameToId.size() == mIdToName.size());

	{
		// serialize mNameToId rather than mIdToName - the out file
		// will be sorted by name instead of ID
		FileWriter fw(FILE_ID_DATA_FILE, mIdToName);
		std::for_each(mNameToId.begin(), mNameToId.end(),
		              call_mem_fun<std::pair<const FileIdStringW, UINT>>(&FileWriter::Write, &fw));
	}

	::GetFTime(FILE_ID_DATA_FILE, &mLastModTime);
	mModified = 0;
}

void FileIdManager::DoLoad()
{
	RWLockLiteWriter l(mLock);
	if (!::IsFile(FILE_ID_DATA_FILE))
	{
		mModified = 0;
		return;
	}

	FILETIME chk;
	::GetFTime(FILE_ID_DATA_FILE, &chk);
	if (chk.dwHighDateTime == mLastModTime.dwHighDateTime && chk.dwLowDateTime == mLastModTime.dwLowDateTime)
	{
		return;
	}

	FileIdStringW fileContents;
	{
		CStringW fileContents2;
		for (int idx = 0; idx < 10; ++idx)
		{
			if (::ReadFileUtf16(FILE_ID_DATA_FILE, fileContents2))
				break;
			::Sleep(250);
		}

		const FileIdStringW fileContents3((LPCWSTR)fileContents2, fileContents2.GetLength(), GetStringManager());
		fileContents = fileContents3;
	}

	if (fileContents.GetLength())
	{
		// save the time
		mLastModTime.dwHighDateTime = chk.dwHighDateTime;
		mLastModTime.dwLowDateTime = chk.dwLowDateTime;

		mIdToName.clear();
		mNameToId.clear();
	}

	FileIdStringW fn, fnLower;
	UINT fid;
	int pos = 0;
	while (pos < fileContents.GetLength())
	{
		// read filename field
		int nextPos = fileContents.Find(DB_FIELD_DELIMITER_W, pos);
		if (-1 == nextPos)
			break;
		fn = fileContents.Mid(pos, nextPos - pos);
		_ASSERTE(fn.GetLength() && fn[fn.GetLength() - 1] != L'\r' && fn[fn.GetLength() - 1] != L'\n');
		pos = nextPos + 1;

		// read fileId field
		nextPos = fileContents.Find(L'\n', pos);
		if (-1 == nextPos)
			nextPos = fileContents.GetLength();
		fid = (uint)::wtox(fileContents.Mid(pos, nextPos - pos));
		pos = nextPos + 1;

		// insert into maps
		fnLower = fn;
		fnLower.MakeLower();

		// #startupPerfHotspot this is a startup performance hotspot (at least in debug builds with large fileids.va
		// files, like with UE)
		mNameToId[fnLower] = fid;
		mIdToName[fid] = std::make_unique<Filenames>(fnLower, fn);
	}

	_ASSERTE("FileIdManager::DoLoad()" && mNameToId.size() == mIdToName.size());
	mModified = 0;
}

CString FileIdManager::GetFileIdStr(const CStringW& filenameAndPath)
{
	return GetFileIdStr(GetFileId(filenameAndPath));
}

CString FileIdManager::GetFileIdStr(UINT fileId)
{
	CString retval;
	CString__FormatA(retval, "fileid:%x", fileId);
	return retval;
}

class FileIdManager::SerializeFileIds : public ParseWorkItem
{
  public:
	SerializeFileIds() : ParseWorkItem("SerializeFileIds")
	{
	}
	virtual bool ShouldRunAtShutdown() const
	{
		return true;
	}
	virtual void DoParseWork()
	{
#if defined(VA_CPPUNIT)
		// add a check instead of using an assert due issues testing RadStudio integration.
		// unit tests blow away file ids, so no data is lost.
		// this logic is not appropriate outside of unit tests due to potential for data loss at exit
		if (gFileIdManager)
			gFileIdManager->Save();
#else
		_ASSERTE(gFileIdManager);
		gFileIdManager->Save();
#endif
	}
};

void FileIdManager::QueueSave()
{
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new SerializeFileIds);
	else
		Save();
}

CString FileIdManager::GetIncludedByStr(const CStringW& filenameAndPath)
{
	CString retval;
	CString__FormatA(retval, "+ib:_%x", GetFileId(filenameAndPath));
	return retval;
}

CString FileIdManager::GetIncludedByStr(UINT fileId)
{
	CString retval;
	CString__FormatA(retval, "+ib:_%x", fileId);
	return retval;
}

WTString FileIdManager::GetIncludeSymStr(const CStringW& filenameAndPath)
{
	WTString retval;
	retval.WTFormat("+ic:_%x", GetFileId(filenameAndPath));
	return retval;
}

WTString FileIdManager::GetIncludeSymStr(UINT fileId)
{
	WTString retval;
	retval.WTFormat("+ic:_%x", fileId);
	return retval;
}

IAtlStringMgr* AFXAPI FileIdManager::GetStringManager()
{
	return &gFileIdStringManager;
}
