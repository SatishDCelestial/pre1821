#include "StdAfxed.h"
#include "..\edcnt.h"
#include "DBFile.h"
#include "..\file.h"
#include "..\assert_once.h"
#include "..\FileId.h"
#include "..\DatabaseDirectoryLock.h"
#include "..\ParseThrd.h"
#include "..\ProjectLoader.h"
#include "..\mainThread.h"
#include "..\VAAutomation.h"
#include "..\LogElapsedTime.h"
#include "..\Win32Heap.h"

// this needs to be before the DEBUG_NEW block for placement new to work
DBFileTocListElem* DBFileToc::GetElem(DWORD fileID, ULONGLONG offset, int splitIndex)
{
	DBFileTocListElem* reservedElem = GetReservedElem();
	if (!reservedElem)
		return nullptr;

	DBFileTocListElem* e = new (reservedElem) DBFileTocListElem(fileID, offset, splitIndex);
	return e;
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

Win32Heap gDbFileTocHeap;

void DBFileToc::Init(const CStringW& dbFileBase)
{
	Close(); // free before setting new name
	mFilePath = dbFileBase + L".dbx";
	mFilePath.Replace(L"\\\\", L"\\");
}

void DBFileToc::Load()
{
	if (mIsLoaded)
		Close();

	CFileW f;
	if (!f.Open(mFilePath, CFile::modeRead | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone | CFile::modeNoInherit))
	{
		VALOGERROR("ERROR: DBFileToc::Load failed to open file");
		return;
	}

	_ASSERTE(f.m_hFile != INVALID_HANDLE_VALUE);
	_ASSERTE(NULL == mEntries);
	UINT count = (UINT)f.GetLength() / sizeof(DBFileTocEntry);
	if (count)
	{
		AddReserve((LONG)count);

		DBFileTocListElem** pp = &mEntries;
		while (count--)
		{
			DBFileTocListElem* p = GetReservedElem();
			if (p)
			{
				f.Read(p, sizeof(DBFileTocEntry));
				*pp = p;
				pp = &p->mNext;
			}
		}
	}

	f.Close();
	mIsLoaded = TRUE;
}

void DBFileToc::Save()
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	if (mModified)
	{
		_ASSERTE(mIsLoaded);

		CFileW f;
		if (!f.Open(mFilePath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyNone | CFile::modeNoInherit))
		{
			_ASSERTE(!"DBFileToc::Save failed to open file");
			VALOGERROR("ERROR: DBFileToc::Save failed to open file");
			return;
		}

		_ASSERTE(f.m_hFile != INVALID_HANDLE_VALUE);

#ifdef _DEBUG
		CFileW fdbg;
		fdbg.Open(mFilePath + L".dbg", CFile::modeCreate | CFile::modeWrite | CFile::shareDenyNone | CFile::modeNoInherit);
		_ASSERTE(fdbg.m_hFile != INVALID_HANDLE_VALUE);
#endif // _DEBUG

		for (DBFileTocListElem* dbx = mEntries; dbx; dbx = dbx->mNext)
		{
			f.Write(dbx, sizeof(DBFileTocEntry));
#ifdef _DEBUG
			// save info for debugging
			WTString dbgstr;
			dbgstr.WTFormat("fid=0x%lx, offset=0x%llx\n", dbx->mFileID, dbx->mOffset);
			fdbg.Write(dbgstr.c_str(), (UINT)dbgstr.GetLength());
#endif // _DEBUG
		}

		mModified = FALSE;
		f.Close();
	}
}

void DBFileToc::Close()
{
	if (!mIsLoaded)
		return;

	if (mModified)
		Save();

	mPoolEntriesCreated = 0;
	mEntries = mReserve = NULL;

	for (TocListElemBackingPool::iterator it = mPool.begin(); it != mPool.end(); ++it)
		gDbFileTocHeap.Free(*it);

	mPool.clear();
	mIsLoaded = FALSE;
}

DBFileTocListElem* DBFileToc::Find(DWORD fileID)
{
	if (!mIsLoaded)
		Load();

	DBFileTocListElem* dbx = mEntries;
	while (dbx)
	{
		if (dbx->mFileID == fileID)
			return dbx;
		dbx = dbx->mNext;
	}
	return NULL;
}

DBFileTocListElem* DBFileToc::Find(DWORD fileID, int splitIndex)
{
	if (!mIsLoaded)
		Load();

	DBFileTocListElem* dbx = mEntries;
	while (dbx)
	{
		if (dbx->mFileID == fileID && dbx->mSplitIndex == splitIndex)
			return dbx;
		dbx = dbx->mNext;
	}
	return NULL;
}

DBFileTocListElem* DBFileToc::AddEntry(DWORD fileID, ULONGLONG offset)
{
	if (!mIsLoaded)
		Load();

	mModified = TRUE;
	if (mEntries)
	{
		DBFileTocListElem* pLast = NULL;
		int splitIndex = 0;
		for (DBFileTocListElem* p = mEntries; p; p = p->mNext)
		{
			if (p->mFileID == fileID)
				splitIndex++;
			pLast = p;
		}

		static int sHighestIndex = 0;
		if (splitIndex > sHighestIndex)
		{
			sHighestIndex = splitIndex;
			if (sHighestIndex > 0x3f)
			{
				vLog("Info: DBF::AE high split %d", sHighestIndex);
			}
		}

		_ASSERTE(offset >= pLast->mOffset);

		pLast->mNext = GetElem(fileID, offset, splitIndex);
		return pLast->mNext;
	}
	else
	{
		mEntries = GetElem(fileID, offset);
		return mEntries;
	}
}

void DBFileToc::RemoveEntry(DBFileTocListElem* dbx)
{
	_ASSERTE(mIsLoaded);

	if (dbx == mEntries)
		mEntries = dbx->mNext;
	else
	{
		for (DBFileTocListElem* prev = mEntries; prev; prev = prev->mNext)
		{
			if (prev->mNext == dbx)
			{
				prev->mNext = dbx->mNext;
				break;
			}
		}
	}

	ReleaseElem(dbx);
	mModified = TRUE;
}

FILETIME* DBFileToc::GetFileTime(DWORD fileID)
{
	if (!mIsLoaded)
		Load();

	DBFileTocListElem* dbx = Find(fileID);
	if (dbx)
		return &dbx->mFt;
	return NULL;
}

void DBFileToc::SetFileTime(DWORD fileID, const FILETIME* ft)
{
	if (!mIsLoaded)
		Load();

	DBFileTocListElem* dbx = Find(fileID);
	if (dbx)
	{
		dbx->mFt = *ft;
		mModified = TRUE;
	}
}

#ifdef _DEBUG
int gMaxDbFilePoolEntriesCreated = 0;
#endif

DBFileTocListElem* DBFileToc::GetReservedElem()
{
	if (!mReserve)
		AddReserve(25);

	DBFileTocListElem* e = mReserve;
	if (e)
	{
		mReserve = e->mNext;
		e->mNext = nullptr;
	}
	return e;
}

void DBFileToc::ReleaseElem(DBFileTocListElem* p)
{
	p->~DBFileTocListElem();
	p->mNext = NULL;
	AddToReserve(p, p);
}

void DBFileToc::AddReserve(LONG itemCountToAdd)
{
	byte* newElems;
	try
	{
		newElems = gDbFileTocHeap.Alloc(itemCountToAdd * sizeof(DBFileTocListElem));
		if (!newElems)
			throw new CMemoryException();

		mPool.push_back(newElems);
	}
	catch (CMemoryException*)
	{
		vLog("ERROR: DBFileTOC::AddReserve CMemoryException (%ld)\n", itemCountToAdd);
		return;
	}

	DBFileTocListElem* last = NULL;
	for (LONG idx = 0; idx < itemCountToAdd; ++idx)
	{
		DBFileTocListElem* curObj = reinterpret_cast<DBFileTocListElem*>(&newElems[idx * sizeof(DBFileTocListElem)]);
		curObj->mNext = last;
		last = curObj;
	}

	DBFileTocListElem* head = reinterpret_cast<DBFileTocListElem*>(newElems);
	AddToReserve(last, head);
	mPoolEntriesCreated += itemCountToAdd;

#ifdef _DEBUG
	if (gMaxDbFilePoolEntriesCreated < mPoolEntriesCreated)
		gMaxDbFilePoolEntriesCreated = mPoolEntriesCreated;
#endif
}

BOOL DBFileToc::InvalidateFile(DWORD fileID)
{
	DBFileTocListElem* dbx = Find(fileID);
	if (!dbx)
		return FALSE;

	do
	{
		dbx->mFileID = 0;
		// [case: 116689]
		// check for more toc elements
	} while ((dbx = Find(fileID)) != nullptr);

	mModified = TRUE;
	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
// DBFile
////////////////////////////////////////////////////////////////////////////////

void DBFile::Init(const CStringW& dbFileBase)
{
	mInvalidatedFileCnt = 0;
	const CStringW oldFilePath(mFilePath);
	mFilePath = dbFileBase + L".db";
	if (oldFilePath != mFilePath || mToc.IsLoaded())
	{
		Close(); // will save if modified
		mToc.Init(dbFileBase);
	}
}

WTString DBFile::ReadFileText(DWORD fileID)
{
	UINT len;
	LPVOID p = ReadFile(fileID, len);
	if (p)
	{
		WTString txt((LPCSTR)p, (int)len);
		free(p);
		return txt;
	}

	return WTString();
}

LPVOID
DBFile::ReadFile(DWORD fileID, UINT& rlen)
{
	char* buf = NULL;
	rlen = 0;

	OpenForRead();

	DBFileTocListElem* dbx = mToc.Find(fileID);
	while (dbx)
	{
		ULONGLONG nextoffset = dbx->mNext ? dbx->mNext->mOffset : cfile.GetLength();
		if ((UINT)nextoffset >= dbx->mOffset)
		{
			UINT sz = (UINT)(nextoffset - dbx->mOffset);
			_ASSERTE(!(sz & 0xf0000000));
			if (buf)
				buf = (char*)realloc(buf, rlen + sz + 1);
			else
				buf = (char*)malloc(sz + 1);
			cfile.Seek((LONGLONG)dbx->mOffset, CFile::begin);

			rlen += cfile.Read(&buf[rlen], sz);
			buf[rlen] = '\0';
			do
			{
				dbx = dbx->mNext;
			} while (dbx && dbx->mFileID != fileID);
		}
		else
		{
			// Idx is corrupt
			WTString errMsg;
			errMsg.WTFormat("ReadFile dbx %s is corrupt, fid=0x%lx.", (LPCTSTR)CString(mFilePath), fileID);
			VALOGERROR(errMsg.c_str());
			// Sean, if you get here, zip and send both both mFilePath.db and mFilePath.dbx files along with startup.log
			// with message above.
			_ASSERTE(!"DBFile::ReadFile db corruption");
			return buf;
		}
	}
	return buf;
}

BOOL DBFile::OpenForRead()
{
	if (!mToc.IsLoaded())
		mToc.Load();

	if (cfile.m_hFile == INVALID_HANDLE_VALUE)
	{
		if (g_loggingEnabled && ::IsFile(mFilePath))
		{
			const DWORD fs = ::GetFSize(mFilePath);
			if (fs > (20 * 1024 * 1024))
			{
				WTString msg;
				msg.WTFormat("size threshold: DBFile::OpenForRead %s %lu", (LPCTSTR)CString(mFilePath), fs);
				Log(msg.c_str());
			}
		}

		if (!cfile.Open(mFilePath, CFile::modeCreate | CFile::modeRead | CFile::modeNoTruncate | CFile::shareDenyNone |
		                               CFile::modeNoInherit))
		{
			VALOGERROR("ERROR: DBFile::OpenForRead failed to open file");
			return FALSE;
		}
	}
	_ASSERTE(cfile.m_hFile != INVALID_HANDLE_VALUE);
	return TRUE;
}

// TODO: convert to filemap on x64?
BOOL DBFile::OpenForWrite(DWORD fileID, BOOL append /*= FALSE*/)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	if (!mToc.IsLoaded())
		mToc.Load();

	if (!append)
		RemoveFile(fileID);

	if (wfile.m_hFile == INVALID_HANDLE_VALUE)
	{
		if (g_loggingEnabled && ::IsFile(mFilePath))
		{
			const DWORD fs = ::GetFSize(mFilePath);
			if (fs > (20 * 1024 * 1024))
			{
				WTString msg;
				msg.WTFormat("size threshold: DBFile::OpenForWrite %s %lu", (LPCTSTR)CString(mFilePath), fs);
				Log(msg.c_str());
			}
		}

		if (!wfile.Open(mFilePath, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate |
		                               CFile::shareDenyNone | CFile::modeNoInherit))
		{
			VALOGERROR("ERROR: DBFile::OpenForWrite failed to open file");
			return FALSE;
		}
	}
	_ASSERTE(wfile.m_hFile != INVALID_HANDLE_VALUE);

	wfile.Seek(0, CFile::end);
	mCurWriteDbx = mToc.AddEntry(fileID, wfile.GetLength());
	return TRUE;
}

bool DBFile::SeekToReadCookie(DWORD splitNo, DWORD splitOffset, DWORD fileID)
{
	DWORD actualOffset = GetActualOffset(fileID, splitNo, splitOffset);
	if (0xffffffff == actualOffset)
		return false;
	cfile.Seek(actualOffset, CFile::begin);
	return true;
}

DWORD
DBFile::GetActualOffset(DWORD fileID, DWORD splitNo, DWORD splitOffset)
{
	const DBFileTocListElem* dbx = mToc.Find(fileID, (int)splitNo);
	if (dbx)
	{
		return (DWORD)(dbx->mOffset + splitOffset);
	}
	else
	{
		const DWORD tid = ::GetCurrentThreadId();
		CString fName;
		DWORD parserTid = 0;
		DWORD prjLdrTid = 0;

		try
		{
			if (g_ParserThread)
				parserTid = g_ParserThread->GetThreadId();

			ProjectLoaderRef pl;
			if (pl)
				prjLdrTid = pl->IsFinished() ? 0 : pl->GetThreadId();
		}
		catch (...)
		{
		}

		if (g_loggingEnabled)
			fName = gFileIdManager->GetFile(fileID);

		// a common scenario is coloring while parse is active
		// don't assert on UI thread since this assert is common
		// during a parse while coloring (since parse cleans refs
		// to the file)
		if (tid == parserTid || tid == prjLdrTid)
		{
			vLog("Error: DBF::GAO fileID not located tid(%lx) %lx %s", tid, fileID, (LPCTSTR)fName);
#ifndef RAD_STUDIO
			if (!gTestsActive)
			{
				ASSERT_ONCE(!"DBFile::GetActualOffset fileID not located");
			}
#endif
		}
		else if (tid == g_mainThread)
		{
			// probably coloring while parser is active
			vLog("UI: DBF::GAO fileID not located tid(%lx) %lx %s", tid, fileID, (LPCTSTR)fName);
		}
		else
		{
			// warn on scope and underline thread.
			// error on concurrent parse thread but not currently differentiated here.
			vLog("Warn (potential error): DBF::GAO fileID not located tid(%lx) %lx %s", tid, fileID, (LPCTSTR)fName);
		}
		return 0xffffffff;
	}
}

void DBFile::GetWriteHeadPositionCookie(uint& splitNo, uint& splitOffset)
{
	ULONGLONG cp = wfile.GetPosition();
	_ASSERTE(cp == wfile.GetLength());
	splitNo = (uint)mCurWriteDbx->mSplitIndex;
	splitOffset = DWORD(cp - mCurWriteDbx->mOffset);
}

void DBFile::Close()
{
	mToc.Close();
	if (wfile.m_hFile != INVALID_HANDLE_VALUE)
		wfile.Close();
	if (cfile.m_hFile != INVALID_HANDLE_VALUE)
		cfile.Close();
}

void DBFile::Write(const void* lpBuf, UINT nCount)
{
	_ASSERTE(wfile.GetPosition() == wfile.GetLength());
	wfile.Write(lpBuf, nCount);
	//	mToc.mModified = TRUE;
}

void DBFile::InvalidateFile(DWORD fileID)
{
	if (mToc.InvalidateFile(fileID))
		++mInvalidatedFileCnt;
}

void DBFile::RemoveInvalidatedFiles()
{
	if (!mInvalidatedFileCnt)
		return;

	RemoveFile(0);
	mInvalidatedFileCnt = 0;
}

void DBFile::RemoveFile(DWORD fileID)
{
	DBFileTocListElem* dbx = mToc.Find(fileID);
	if (!dbx)
		return;

	LogElapsedTime tt("DBFile::RemoveFile", 2000);
	if (wfile.m_hFile == INVALID_HANDLE_VALUE)
	{
		if (!wfile.Open(mFilePath, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate |
		                               CFile::shareDenyNone | CFile::modeNoInherit))
		{
			_ASSERTE(!"DBFile::RemoveFile failed to open file");
			VALOGERROR("ERROR: DBFile::RemoveFile failed to open file");
			return;
		}
	}
	_ASSERTE(wfile.m_hFile != INVALID_HANDLE_VALUE);
	const ULONGLONG wfileOriginalLength = wfile.GetLength();
	ULONGLONG totalRemoved = 0;

	// cap size of memory allocation required for closing gaps
	const UINT kMoveChunkSize = (1 * 1024 * 1024) - 1024;
	std::vector<CHAR> buf(kMoveChunkSize + 1);

	// dbx points to the first element that needs to be dropped

	do
	{
		const ULONGLONG curOffsetOld = dbx->mOffset;
		const ULONGLONG curOffsetNew = dbx->mOffset - totalRemoved;
		const ULONGLONG nextoffsetOld = dbx->mNext ? dbx->mNext->mOffset : wfileOriginalLength;
		const ULONGLONG curLen = nextoffsetOld - curOffsetOld;

		if (dbx->mFileID == fileID)
		{
			// drop this item
			DBFileTocListElem* tmp = dbx;
			dbx = dbx->mNext;
			mToc.RemoveEntry(tmp);
			totalRemoved += curLen;
			continue;
		}

		// dbx is first of potentially many to be kept.
		// keep by reading items and writing back to where the last skipped item was.
		if ((UINT)wfileOriginalLength >= curOffsetOld)
		{
			if (curOffsetOld == curOffsetNew)
			{
				// a skipped item had a size of 0; no file i/o is necessary
				dbx = dbx->mNext;
				continue;
			}

			if (curLen)
			{
				DBFileTocListElem* tmp = dbx;
				UINT lengthToMove = 0;

				// find end of group that we will move forward and calculate
				// total size of the block (multiple elements if possible)
				while (tmp && tmp->mFileID != fileID)
				{
					DBFileTocListElem* nxt = tmp->mNext;
					const ULONGLONG nextoffset = nxt ? nxt->mOffset : wfileOriginalLength;
					const ULONGLONG tmpLen = nextoffset - tmp->mOffset;
					_ASSERTE(lengthToMove != 0 || curLen == tmpLen);
					lengthToMove += (UINT)tmpLen;
					_ASSERTE(!(lengthToMove & 0xf0000000));
					_ASSERTE(tmp->mOffset >= totalRemoved);
					tmp->mOffset -= totalRemoved;
					tmp = nxt;
				}

				// now do actual move
				if (lengthToMove)
				{
					LONGLONG readPos = (LONGLONG)curOffsetOld;
					LONGLONG writePos = (LONGLONG)curOffsetNew;
					_ASSERTE(curOffsetOld > curOffsetNew);
					do
					{
						const UINT sz = lengthToMove > kMoveChunkSize ? kMoveChunkSize : lengthToMove;
						wfile.Seek(readPos, CFile::begin);
						wfile.Read(&buf[0], sz);
						wfile.Seek(writePos, CFile::begin);
						wfile.Write(&buf[0], sz);

						readPos += sz;
						writePos += sz;
						lengthToMove -= sz;
					} while (lengthToMove);
				}

				dbx = tmp;
				_ASSERTE(!dbx || dbx->mFileID == fileID);
			}
			else
			{
				// current item has a size of 0; no file i/o is necessary, just update its offset
				dbx->mOffset = curOffsetNew;
				dbx = dbx->mNext;
			}
		}
		else
		{
			// Idx is corrupt
			WTString errMsg;
			errMsg.WTFormat("rmfile dbx %s is corrupt, fid=0x%lx.", (LPCTSTR)CString(mFilePath), fileID);
			VALOGERROR(errMsg.c_str());
			// Sean, if you get here, zip and send both both mFilePath.db and mFilePath.dbx files along with startup.log
			// with message above.
			_ASSERTE(!"DBFile::RemoveFile db corruption");
			return;
		}
	} while (dbx);

	_ASSERTE(!mToc.Find(fileID));
	_ASSERTE(wfileOriginalLength >= totalRemoved);
	ULONGLONG wfileLengthNew = wfileOriginalLength - totalRemoved;
	if (wfileLengthNew != wfileOriginalLength)
		wfile.SetLength(wfileLengthNew);
	_ASSERTE(wfile.GetLength() == wfileLengthNew);
	wfile.Seek(0, CFile::end);

#ifdef _DEBUG
	const DBFileTocListElem* dbx2 = mToc.FirstDbx();
	while (dbx2)
	{
		_ASSERTE(!dbx2->mNext || dbx2->mNext->mOffset >= dbx2->mOffset);
		_ASSERTE(dbx2->mOffset <= wfileLengthNew);
		dbx2 = dbx2->mNext;
	}
#endif // _DEBUG
}

UINT DBFile::ReadFromCookie(DWORD splitNo, DWORD splitOffset, DWORD fileID, void* lpBuf, UINT nCount)
{
	OpenForRead();
	if (!SeekToReadCookie(splitNo, splitOffset, fileID))
	{
		((char*)lpBuf)[0] = '\0';
		return 0;
	}
	return cfile.Read(lpBuf, nCount);
}

#if defined(VA_CPPUNIT) || defined(_DEBUG)
UINT DBFile::ReadFromOffset(ULONGLONG offset, void* lpBuf, UINT nCount)
{
	OpenForRead();
	cfile.Seek((LONGLONG)offset, CFile::begin);
	return cfile.Read(lpBuf, nCount);
}
#endif
