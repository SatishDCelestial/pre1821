#pragma once

#include <map>
#include "WTString.h"
#include "Win32Heap.h"
#include "PrivateHeapAllocator.h"
#include "DBLock.h"

extern Win32Heap gFileIdMgrHeapAllocator;

class FileIdManager
{
  public:
	FileIdManager();
	~FileIdManager();

	UINT GetFileId(const CStringW& filenameAndPath);
	static UINT GetIdFromFileId(const char *fileIdStr);
	static UINT GetIdFromFileId(const WTString &fileIdStr)
	{
		return GetIdFromFileId(fileIdStr.c_str());
	}
	CString GetFileIdStr(const CStringW& filenameAndPath);
	WTString GetIncludeSymStr(const CStringW& filenameAndPath);
	static WTString GetIncludeSymStr(UINT fileId);
	CString GetIncludedByStr(const CStringW& filenameAndPath);
	static CString GetIncludedByStr(UINT fileId);
	CString GetFileIdStr(UINT fileId);
	CStringW GetFile(LPCSTR fileId);
	CStringW GetFile(const WTString& fileId)
	{
		return GetFile(fileId.c_str());
	}
	CStringW GetFile(LPCWSTR fileId)
	{
		return GetFile(WTString(fileId).c_str());
	}
	CStringW GetFile(const CStringW& fileId)
	{
		return GetFile(WTString(fileId).c_str());
	}
	CStringW GetFile(UINT fileId);
	std::pair<CStringW, CStringW> GetFile2(UINT fileId); // <mixed, lower>
	void IterateAll(std::function<void(uint fileId, const CStringW& filename)> fnc);

	void QueueSave();

	// return empty string for .va and .d* files in the dbDir
	CStringW GetFileForUser(UINT fileId);
	CStringW GetFileForUser(const CStringW& fileIdStr);

  private:
	template <typename _CharType = char, class StringIterator = ATL::ChTraitsCRT<_CharType>>
	class FileIdStrTrait : public StringIterator
	{
	  public:
		static HINSTANCE FindStringResourceInstance(UINT nID) throw()
		{
			return (AfxFindStringResourceHandle(nID));
		}

		static ATL::IAtlStringMgr* GetDefaultManager() throw()
		{
			return (GetStringManager());
		}
	};

	typedef ATL::CStringT<wchar_t, FileIdStrTrait<wchar_t>> FileIdStringW;

	static IAtlStringMgr* AFXAPI GetStringManager();
	UINT AddFile(const FileIdStringW& mixedCaseName, const FileIdStringW& lowerName);
	UINT AddFile(const FileIdStringW& mixedCaseName, const FileIdStringW& lowerName, UINT id);
	void Save();
	void DoLoad();

	class FileWriter;
	class SerializeFileIds;
	struct Filenames
	{
		FileIdStringW mFilenameLower;
		FileIdStringW mFilenameMixed;

		Filenames()
		{
		}
		Filenames(const FileIdStringW& lowerName, const FileIdStringW& mixedCase)
		    : mFilenameLower(lowerName), mFilenameMixed(mixedCase)
		{
		}
	};
	using FilenamesPtr = std::unique_ptr<Filenames>;
	// map ID to file
	typedef std::unordered_map<UINT, FilenamesPtr, std::hash<UINT>, std::equal_to<UINT>,
	                           PrivateHeapAllocator<std::pair<const UINT, FilenamesPtr>, gFileIdMgrHeapAllocator>>
	    FileIdToNameMap;
	// map file to ID
	typedef std::unordered_map<FileIdStringW, UINT, std::hash<FileIdStringW>, std::equal_to<FileIdStringW>,
	                 PrivateHeapAllocator<std::pair<const FileIdStringW, UINT>, gFileIdMgrHeapAllocator>>
	    FilenameToIdMap;

	FILETIME mLastModTime;
	RWLockLite mLock;
	FilenameToIdMap mNameToId;
	FileIdToNameMap mIdToName;
	int mModified;
};

extern FileIdManager* gFileIdManager;
extern const CStringW kFileIdDb;

void InitFileIdManager();
void ClearFileIdManager();
