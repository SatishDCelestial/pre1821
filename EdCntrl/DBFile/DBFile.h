#pragma once

#include <vector>
#include "..\CFileW.h"

struct DBFileTocEntry
{
	ULONGLONG mOffset;
	FILETIME mFt;
	DWORD mFileID;
	int mSplitIndex;

	DBFileTocEntry(DWORD fileId = 0, ULONGLONG offset = 0, int splitIndex = 0)
	    : mOffset(offset), mFileID(fileId), mSplitIndex(splitIndex)
	{
		mFt.dwHighDateTime = mFt.dwLowDateTime = 0;
	}
};

class DBFileTocListElem : public DBFileTocEntry
{
	friend class DBFileToc;

  private:
	DBFileTocListElem();
	DBFileTocListElem(DWORD fileID, ULONGLONG offset, int splitIndex = 0)
	    : DBFileTocEntry(fileID, offset, splitIndex), mNext(NULL)
	{
	}

  public:
	DBFileTocListElem* mNext;
};

class DBFileToc
{
  public:
	DBFileToc() : mIsLoaded(FALSE), mModified(FALSE), mEntries(NULL), mPoolEntriesCreated(0), mReserve(NULL)
	{
	}
	~DBFileToc()
	{
		Close();
	}

	void Init(const CStringW& dbFileBase);

	void Load();
	BOOL IsLoaded() const
	{
		return mIsLoaded;
	}
	void Save();
	void Close();

	DBFileTocListElem* Find(DWORD fileID);
	DBFileTocListElem* Find(DWORD fileID, int splitIndex);

	FILETIME* GetFileTime(DWORD fileID);
	void SetFileTime(DWORD fileID, const FILETIME* ft);
	BOOL InvalidateFile(DWORD fileID);

	DBFileTocListElem* AddEntry(DWORD fileID, ULONGLONG offset);
	void RemoveEntry(DBFileTocListElem* dbx);

#if defined(VA_CPPUNIT) || defined(_DEBUG)
	const DBFileTocListElem* FirstDbx() const
	{
		return mEntries;
	}
#endif

  private:
	// [case: 60806] methods for managing backing store of DBFileTocListElems
	DBFileTocListElem* GetElem(DWORD fileID, ULONGLONG offset, int splitIndex = 0);
	DBFileTocListElem* GetReservedElem();
	void ReleaseElem(DBFileTocListElem* p);
	void AddReserve(LONG itemCountToAdd);
	void AddToReserve(DBFileTocListElem* head, DBFileTocListElem* tail)
	{
		_ASSERTE(head && tail && !tail->mNext);
		tail->mNext = mReserve;
		mReserve = head;
	}

  private:
	BOOL mIsLoaded;
	BOOL mModified;
	CStringW mFilePath;
	DBFileTocListElem* mEntries;

	// [case: 60806] backing store for DBFileTocListElems to help reduce heap fragmentation
	LONG mPoolEntriesCreated;
	typedef std::vector<byte*> TocListElemBackingPool;
	TocListElemBackingPool mPool; // vector of DBFileTocListElem arrays - new/delete these
	DBFileTocListElem* mReserve;  // list of available DBFileTocListElem items - do not new/delete these
};

class DBFile
{
  public:
	void Init(const CStringW& dbFileBase);

	void Save()
	{
		mToc.Save();
	}
	void Close();

	BOOL Contains(DWORD fileID)
	{
		return mToc.Find(fileID) != NULL;
	}
	FILETIME* GetFileTime(DWORD fileID)
	{
		return mToc.GetFileTime(fileID);
	}
	void SetFileTime(DWORD fileID, const FILETIME* ft)
	{
		mToc.SetFileTime(fileID, ft);
	}

	////////////////////////////////////////////////////////////////////////////
	// WRITE methods

	BOOL OpenForWrite(DWORD fileID, BOOL append = FALSE);
	void Write(const void* lpBuf, UINT nCount);
	void GetWriteHeadPositionCookie(uint& splitNo, uint& splitOffset);
	void RemoveFile(DWORD fileID);
	void InvalidateFile(DWORD fileID);
	void RemoveInvalidatedFiles();

	////////////////////////////////////////////////////////////////////////////
	// READ methods

	// returned memory must be released by calling free
	LPVOID ReadFile(DWORD fileID, UINT& rlen);
	WTString ReadFileText(DWORD fileID);
	UINT ReadFromCookie(DWORD splitNo, DWORD splitOffset, DWORD fileID, void* lpBuf, UINT nCount);
#if defined(VA_CPPUNIT) || defined(_DEBUG)
	UINT ReadFromOffset(ULONGLONG offset, void* lpBuf, UINT nCount);
#endif

	////////////////////////////////////////////////////////////////////////////

	// only for error reporting
	DWORD GetActualOffset(DWORD fileID, DWORD splitNo, DWORD splitOffset);
	CStringW FilePath() const
	{
		return mFilePath;
	}

#if defined(VA_CPPUNIT) || defined(_DEBUG)
	const DBFileTocListElem* FirstDbx() const
	{
		return mToc.FirstDbx();
	}
	const DBFileTocListElem* CurWriteDbx() const
	{
		return mCurWriteDbx;
	}
#endif

  private:
	BOOL OpenForRead();
	bool SeekToReadCookie(DWORD splitNo, DWORD splitOffset, DWORD fileID);

	CFileW cfile;
	CFileW wfile;
	DBFileToc mToc;
	DBFileTocListElem* mCurWriteDbx;
	int mInvalidatedFileCnt;
	CStringW mFilePath;
};
