#pragma once

#include "DBFile.h"
#include "..\CFileW.h"
#include "..\Lock.h"
#include "..\DBLock.h"
#include "..\FOO.H"
#include "DbTypeId.h"

class DType;
class FileDic;

class SortBins
{
	friend class DBFilePair;

  public:
	struct SortBinData
	{
		WTString mSym;
		uint mScopeHash;
		uint mType;
		uint mAttrs;

		SortBinData(const WTString& sym, uint scopeHash, uint type, uint attrs)
		    : mSym(sym), mScopeHash(scopeHash), mType(type), mAttrs(attrs)
		{
		}
	};
	using SortBinDataCache = std::vector<SortBinData>;

	SortBins()
	{
	}
	~SortBins()
	{
		Close();
	}

	void Close();

  private:
	void Add(const CStringW& dir, const SortBinDataCache& cache);

	CCriticalSection mSortBinLock;
	struct BinInfo
	{
		CStringW mLastDir;
		CCriticalSection mFileLock;
		CFileW mUnsortedLetterFile;
	};
	using BinMapping = std::map<WCHAR, BinInfo>;
	BinMapping mSortBins;
};

extern SortBins g_SortBins;

class DBFilePair
{
	friend class DbFpWriter;

  public:
	DBFilePair() : mDbIDFlags(0), wFileID(0), mActiveRemove(0)
	{
	}
	~DBFilePair()
	{
		Close();
	}

	void Init(const CStringW& baseDir, const CStringW& projDbDir, DbTypeId dbID, int splitNo);
	void Close();
	BOOL Contains(DWORD fileID)
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		return mIdxFiles.Contains(fileID);
	}
	void RemoveFile(DWORD fileId);
	void InvalidateFile(DWORD fileID);
	void RemoveInvalidatedFiles();

	WTString ReadDSFileText(DWORD fileID)
	{
		AutoLockCs dsLock(mLock[DsLock]);
		return mDsFiles.ReadFileText(fileID);
	}

	LPVOID ReadIdxFile(DWORD fileID, UINT& rlen)
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		return mIdxFiles.ReadFile(fileID, rlen);
	}

	void SetFileTime(DWORD fileID, const FILETIME* ft)
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		mIdxFiles.SetFileTime(fileID, ft);
	}

	FILETIME* GetFileTime(DWORD fileID)
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		return mIdxFiles.GetFileTime(fileID);
	}

	void GetSymDefStrs(DType* symDef, bool getBoth,
	                   std::function<void(const WTString& symscope, const WTString& def)> setter = {});

#if defined(VA_CPPUNIT)
	CStringW GetDSFilePath() const
	{
		AutoLockCs dsLock(mLock[DsLock]);
		return mDsFiles.FilePath();
	}

	CStringW GetIdxFilePath() const
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		return mIdxFiles.FilePath();
	}
#endif

	void Save()
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		AutoLockCs dsLock(mLock[DsLock]);
		mIdxFiles.Save();
		mDsFiles.Save();
	}

  private:
	void GetSymDefStrs(DWORD fileID, DWORD splitNo, DWORD splitOffset, WTString* outSym, WTString* outDef);
	BOOL OpenForWrite(DWORD fileID, BOOL append = FALSE);
	void StopWriting();
	void DBOut(const WTString& symscope, const WTString& def, UINT type, UINT attrs, int line, DWORD fileID);

#if defined(VA_CPPUNIT) || defined(_DEBUG)
	void VerifyFilePair(DWORD fileID);
	void VerifyIndex(const DBFileTocListElem* dbx);
	void VerifyIndexes();
#endif

	DBFile mIdxFiles;
	DBFile mDsFiles;
	// if both locks are going to be acquired, always lock index first
	enum
	{
		IdxLock = 0,
		DsLock,
		LockCnt
	};
	mutable CCriticalSection mLock[LockCnt];
	CStringW mDbFileBase;
	CStringW mDbDir;
	UINT mDbIDFlags;
	DWORD wFileID;
	SortBins::SortBinDataCache mSortBinCache;
	std::vector<DType> mGlobalDicCache;
	LONG mActiveRemove;
};

class DbFpWriter
{
  public:
	DbFpWriter(DBFilePair* dbPair, DWORD fileID, BOOL append) : mFilePair(dbPair)
	{
		_ASSERTE(mFilePair);
		if (!mFilePair->OpenForWrite(fileID, append))
			mFilePair = nullptr;
	}

	~DbFpWriter()
	{
		if (mFilePair)
			mFilePair->StopWriting();
	}

	void DBOut(const WTString& symscope, const WTString& def, UINT type, UINT attrs, int line, DWORD fileID)
	{
		if (mFilePair)
			mFilePair->DBOut(symscope, def, type, attrs, line, fileID);
	}

  private:
	DbFpWriter() = delete;

	DBFilePair* mFilePair;
};

class VADatabase
{
  public:
	VADatabase();
	~VADatabase();

	void Init(const CStringW& baseDir, const CStringW& projDbDir);
	void Save();
	void Close();
	void ReleaseDbsForExit();

	FILETIME* GetFileTime(DWORD fileID, DbTypeId dbID);
	void SetFileTime(DWORD fileID, DbTypeId dbID, const FILETIME* ft);
	void InvalidateFileTime(const CStringW& file);
	void InvalidateFile(DWORD fileID);
	void RemoveInvalidatedFiles();
	void RemoveFile(DWORD fileID);

	std::shared_ptr<DbFpWriter> GetDBWriter(DWORD fileID, DbTypeId dbID, BOOL append = FALSE);

	LPVOID ReadIdxFile(DWORD fileID, DbTypeId dbID, UINT& rlen)
	{
		return GetPair(fileID, dbID)->ReadIdxFile(fileID, rlen);
	}

	WTString ReadDSFileText(DWORD fileID, UINT dbFlags)
	{
		DbTypeId dbID = DBIdFromDbFlags(dbFlags);
		return GetPair(fileID, dbID)->ReadDSFileText(fileID);
	}

	void GetSymDefStrs(DType* symDef, BOOL getBoth,
	                   std::function<void(const WTString& symscope, const WTString& def)> setter = {});
	int GetDbPairId(DWORD fileID) const
	{
		return int(fileID % mDbPairSplitCount);
	}
	DbTypeId DBIdFromParams(int fType, DWORD fileID, BOOL sysFile, BOOL parseAll, uint dbFlags = 0);
	DWORD GetSplitCount() const
	{
		return mDbPairSplitCount;
	}
	void SetOkToIgnoreGetSymStrs(BOOL en)
	{
		mOkToIgnoreGetSymStrs = en;
	}
	BOOL GetOkToIgnoreGetSymStrs() const
	{
		return mOkToIgnoreGetSymStrs;
	}
	void SolutionLoadCompleted();
	static uint DbFlagsFromDbId(DbTypeId dbId);
	static DbTypeId DBIdFromDbFlags(UINT dbFlags);

#if defined(VA_CPPUNIT)
	// need public access for testsuite
	CStringW GetDsFilename(DWORD fileID, DbTypeId dbID)
	{
		return GetPair(fileID, dbID)->GetDSFilePath();
	}
#endif

  private:
	BOOL mOkToIgnoreGetSymStrs;
	const DWORD mDbPairSplitCount;
	DBFilePair** mDbs;
	CStringW mBaseDir, mProjDbDir, mProjDbInfoFileBasename;
	CStringW mProjDbLoadingSentinelFile; // [case: 88040]
	void OpenDB();
	void RecordProjDbInfo(const CStringW& dir, const CStringW& infFile);
	bool VerifyProjDbInfo(const CStringW& dir, const CStringW& infFile);
	void CheckProjDbInfo();
	DBFilePair* GetPair(DWORD fileID, DbTypeId dbID)
	{
		_ASSERT(dbID > DbTypeId_Error && dbID < DbTypeId_Count);
		const int prId = GetDbPairId(fileID);
		DBFilePair* dbpair = &mDbs[prId][dbID];
		return dbpair;
	}

	DbTypeId DBIdContainingFileID(DWORD fileID);
};

extern VADatabase g_DBFiles;
