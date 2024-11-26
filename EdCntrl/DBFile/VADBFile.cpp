// #if defined __GNUC__
// #define PACKED        __attribute__((packed))
// #else
// #define PACKED
// #endif
// int ddd PACKED;
// int eee __attribute__((packed));

#include "StdAfxed.h"
#include <ppl.h>
#include "..\edcnt.h"
#include "DBFile.h"
#include "VADBFile.h"
#include "..\FileTypes.h"
#include "..\assert_once.h"
#include "../ParseThrd.h"
#include "../project.h"
#include "../DatabaseDirectoryLock.h"
#include "../mainThread.h"
#include "../ProjectLoader.h"
#include "../project.h"
#include "../FileId.h"
#include "../Lock.h"
#include "../fdictionary.h"
#include "../file.h"
#include "../wt_stdlib.h"
#include "../CodeGraph.h"
#include "../RegKeys.h"
#include "../Registry.h"
#include "../../common/ScopedIncrement.h"
#include "../Win32Heap.h"
#include "Settings.h"
#include "serial_for.h"
#include "DbTypeIdStrs.h"
#include "remote_heap.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma warning(disable : 4868)

VADatabase g_DBFiles;
SortBins g_SortBins;
extern Win32Heap gDbFileTocHeap;

// DBFilePair
// ------------------------------------------------------------------------
//

void DBFilePair::DBOut(const WTString& symscope, const WTString& def, UINT type, UINT attrs, int line, DWORD fileID)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	_ASSERTE(mLock[IdxLock].m_sect.OwningThread == (HANDLE)(UINT_PTR)::GetCurrentThreadId());
	_ASSERTE(mLock[DsLock].m_sect.OwningThread == (HANDLE)(UINT_PTR)::GetCurrentThreadId());
	bool addToSortBin = true;

	if (wFileID != fileID)
	{
		// xxx_sean: is this possible?
		_ASSERTE(!"Isn't this dangerous??");
		_ASSERTE(g_DBFiles.GetDbPairId(fileID) == g_DBFiles.GetDbPairId(wFileID));
		WTString errMsg;
		errMsg.WTFormat("ERROR: DBFP::DBOut change of file id: %s from %lx to %lx",
		                WTString(mDsFiles.FilePath()).c_str(), wFileID, fileID);
		Log(errMsg.c_str());
		mIdxFiles.OpenForWrite(fileID, TRUE);
		mDsFiles.OpenForWrite(fileID, TRUE);
		wFileID = fileID;
	}

	_ASSERTE((type & TYPEMASK) == type);
	// set bits in type to specify this db file
	type = (type & VA_DB_FLAGS_MASK) | mDbIDFlags;

#ifdef _DEBUG
//	DWORD cp = (DWORD)mDsFiles.wfile.GetPosition();
#endif // _DEBUG

	DType _SymDef;
	_SymDef.mFileID = fileID;
	uint SplitIdx = 0;
	mDsFiles.GetWriteHeadPositionCookie(SplitIdx, _SymDef.mIdxOffset);
	DType::CheckSplitIdxRange(SplitIdx);
	_SymDef.mSplitIdx = (int)SplitIdx;
	// 	ASSERT(dsFiles.GetActualOffset(fileID, SymDef.idxOffsetPos) == cp);
	_SymDef.mTypeAndDbFlags = type;
	_SymDef.mAttributes = attrs;
	_SymDef.mLine = line;
	DType::CheckLineRange(line);
	if ((attrs & V_IDX_FLAG_INCLUDE) || (attrs & V_FILENAME))
	{
		_ASSERTE(attrs & V_HIDEFROMUSER);
		addToSortBin = false;
		if (attrs & V_IDX_FLAG_INCLUDE)
		{
			_ASSERTE(::StartsWith(symscope, "+ic:", FALSE) || ::StartsWith(symscope, "+using", FALSE) ||
			         strncmp(symscope.c_str(), "fileid:", 7) == 0);
			_ASSERTE((attrs & V_IDX_FLAG_INCLUDE) && !(attrs & V_FILENAME));
			// so that we can look up includes for fileId: scope(fileID), Hash("+ic:_fileID")
			_SymDef.mScopeHash = fileID;
			_SymDef.mSymHash = WTHashKey(symscope); // WTHashKey(StrGetSym(symscope));
			_SymDef.mSymHash2 = 0;
		}
		else
		{
			_SymDef.mScopeHash = 0;
			_ASSERTE((attrs & V_FILENAME) && !(attrs & V_IDX_FLAG_INCLUDE));
			_ASSERTE(_SymDef.mFileID);
			_SymDef.mSymHash = _SymDef.mFileID;
			_SymDef.mSymHash2 = 0;
			if (symscope[0] == '+')
			{
				_ASSERTE(WTString("+projfile") == symscope);
				// not added to g_pGlobDic
			}
			else
			{
				// symscope is "fileid:xxxxx"
				_ASSERTE(symscope[0] == 'f' && symscope[1] == 'i' && symscope[2] == 'l' && symscope[3] == 'e');
				if (_SymDef.mSymHash)
					mGlobalDicCache.emplace_back(DType{_SymDef.mScopeHash, _SymDef.mSymHash, _SymDef.mSymHash2, nullptr,
					                                   nullptr, _SymDef.mTypeAndDbFlags & TYPEMASK, _SymDef.mAttributes,
					                                   _SymDef.mTypeAndDbFlags & VA_DB_FLAGS, _SymDef.mLine,
					                                   _SymDef.mFileID, _SymDef.mIdxOffset, _SymDef.mSplitIdx});
			}
		}

		if (!_SymDef.mSymHash)
			return;
	}
	else
	{
		auto pSym = StrGetSym(symscope);
		_SymDef.mSymHash = WTHashKey(pSym);
		if (!_SymDef.mSymHash)
			return;
		_ASSERTE(CachedBaseclassList != _SymDef.type() &&
		         "this shouldn't happen -- CachedBaseclassList does not get serialized");
		// if the assert fires, then mSymHash2 generation needs to be conditional
		// _SymDef.mSymHash2 = DType::GetSymHash2(symscope.c_str()); // for CachedBaseclassList
		_SymDef.mSymHash2 = DType::GetSymHash2(pSym);
		_SymDef.mScopeHash = WTHashKey(StrGetSymScope(symscope));
	}

	{
		remote_heap::stats::timing t(remote_heap::stats::stat::timing_DBOut_us,
		                             remote_heap::stats::stat::timing_DBOut_cnt);

		mIdxFiles.Write(&_SymDef, sizeof_reduced_DType);
	}

#if defined(VA_CPPUNIT)
	uint fileid = 0;
#else
	uint fileid = _SymDef.mFileID;
#endif
	WTString dsEntry;
	if ((symscope.GetLength() + def.GetLength()) < 1024)
	{
		dsEntry.WTFormat("\n%d" DB_FIELD_DELIMITER_STR "%s" DB_FIELD_DELIMITER_STR "%s" DB_FIELD_DELIMITER_STR
		                 "%08x" DB_FIELD_DELIMITER_STR "%x" DB_FIELD_DELIMITER_STR "%x" DB_FIELD_DELIMITER_STR "%x",
		                 _SymDef.mAttributes & V_HIDEFROMUSER ? 0 : 1, symscope.c_str(), def.c_str(),
		                 _SymDef.mTypeAndDbFlags, _SymDef.mAttributes, fileid, _SymDef.mLine);
	}
	else
	{
		// Format will truncate very long strings, so build entry via concat
		dsEntry = _SymDef.mAttributes & V_HIDEFROMUSER ? "\n0" DB_FIELD_DELIMITER_STR : "\n1" DB_FIELD_DELIMITER_STR;
		dsEntry += symscope;
		dsEntry += DB_FIELD_DELIMITER_STR;
		dsEntry += def;

		WTString tmp;
		tmp.WTFormat(DB_FIELD_DELIMITER_STR "%08x" DB_FIELD_DELIMITER_STR "%x" DB_FIELD_DELIMITER_STR
		                                    "%x" DB_FIELD_DELIMITER_STR "%x",
		             _SymDef.mTypeAndDbFlags, _SymDef.mAttributes, fileid, _SymDef.mLine);

		dsEntry += tmp;
	}
	mDsFiles.Write(dsEntry.c_str(), (UINT)dsEntry.GetLength());

#if defined(VA_CPPUNIT) || defined(_DEBUG)
	WTString dbgInfo;
#if defined(VA_CPPUNIT)
	if (!strchr(symscope.c_str(), '-'))
		dbgInfo.WTFormat(DB_FIELD_DELIMITER_STR "%s" DB_FIELD_DELIMITER_STR "%s" DB_FIELD_DELIMITER_STR
		                                        "%x" DB_FIELD_DELIMITER_STR "%d",
		                 StrGetSymScope(symscope).c_str(), (LPCTSTR)StrGetSym(symscope), _SymDef.mScopeHash,
		                 _SymDef.mLine);
	else
		dbgInfo.WTFormat(DB_FIELD_DELIMITER_STR "%d", _SymDef.mLine);
#else
	dbgInfo.WTFormat(DB_FIELD_DELIMITER_STR " ln=%d, sp=0x%x, rel=0x%x, act=0x%x, tid=0x%lx", line, _SymDef.mSplitIdx,
	                 _SymDef.mIdxOffset, 0 /*cp*/, GetCurrentThreadId());
#endif // VA_CPPUNIT
	mDsFiles.Write(dbgInfo.c_str(), (UINT)dbgInfo.GetLength());

	VerifyIndex(mIdxFiles.CurWriteDbx());
#endif // VA_CPPUNIT || _DEBUG

	if (addToSortBin && !strchr(symscope.c_str(), '-'))
		mSortBinCache.emplace_back(StrGetSym(symscope), _SymDef.mScopeHash, _SymDef.mTypeAndDbFlags,
		                           _SymDef.mAttributes);
}

void DBFilePair::Init(const CStringW& baseDir, const CStringW& projDbDir, DbTypeId dbID, int splitNo)
{
	AutoLockCs idxLock(mLock[IdxLock]);
	AutoLockCs dsLock(mLock[DsLock]);

	switch (dbID)
	{
	case DbTypeId_Net:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_Net;
		break;
	case DbTypeId_Cpp:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_Cpp;
		break;
	case DbTypeId_Solution:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_Solution;
		break;
	case DbTypeId_LocalsParseAll:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_LocalsParseAll;
		break;
	case DbTypeId_ExternalOther:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_ExternalOther;
		break;
	case DbTypeId_SolutionSystemCpp:
		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_Cpp | VA_DB_SolutionPrivateSystem;
		break;
		// 	case DbTypeId_SolutionSystemNet:
		// 		mDbIDFlags = VA_DB_BackedByDatafile | VA_DB_Net | VA_DB_SolutionPrivateSystem;
		// 		break;
	default:
		_ASSERTE(!"unhandled dbID");
		break;
	}

	switch (dbID)
	{
	case DbTypeId_Solution:
		_ASSERTE(projDbDir);
		mDbDir = projDbDir;
		break;
	case DbTypeId_LocalsParseAll:
		_ASSERTE(projDbDir);
		mDbDir = projDbDir + kDbIdDir[dbID] + L"\\";
		break;
	case DbTypeId_SolutionSystemCpp:
		_ASSERTE(projDbDir);
		mDbDir = projDbDir + kDbIdDir[dbID] + L"\\";
		break;
		// 	case DbTypeId_SolutionSystemNet:
		// 		_ASSERTE(projDbDir);
		// 		mDbDir = projDbDir + kDbIdDir[dbID] + L"\\";
		// 		break;
	default:
		mDbDir = baseDir + kDbIdDir[dbID] + L"\\";
	}

	::CreateDir(mDbDir);
	CStringW splitStr;
	CString__FormatW(splitStr, L"Db%d", splitNo);
	mDbFileBase = mDbDir + splitStr;
	mIdxFiles.Init(mDbFileBase + L"Idx");
	mDsFiles.Init(mDbFileBase + L"Ds");
}

void DBFilePair::GetSymDefStrs(DWORD fileID, DWORD splitNo, DWORD splitOffset, WTString* outSym, WTString* outDef)
{
	if (mActiveRemove)
	{
		if (g_mainThread == ::GetCurrentThreadId() && g_DBFiles.GetOkToIgnoreGetSymStrs())
		{
			// [case: 63058] don't block UI thread for coloring while db is being heavily modified
			return;
		}
	}

	const size_t MAX_READ_ATTEMPTS = 4;
	for (size_t readAttempts = 1; readAttempts <= MAX_READ_ATTEMPTS; ++readAttempts)
	{
		const size_t kBufLen = 512 * (readAttempts * readAttempts);
		const std::unique_ptr<char[]> bufVec(new char[kBufLen + 1]);
		char* buf = &bufVec[0];
		{
			// Lock Seek/Read
			AutoLockCs dsLock(mLock[DsLock]);
			if (!mDsFiles.ReadFromCookie(splitNo, splitOffset, fileID, buf, (uint)kBufLen))
				return;
		}

		if (buf[0] != '\n')
		{
			const DWORD tid = ::GetCurrentThreadId();
			DWORD parserTid = 0;
			DWORD prjLdrTid = 0;
			bool parserThrdActive = false;
			bool parserThrdActiveNormal = false;

			try
			{
				if (g_ParserThread)
				{
					parserTid = g_ParserThread->GetThreadId();
					parserThrdActive = g_ParserThread->IsActive();
					parserThrdActiveNormal = g_ParserThread->IsNormalJobActiveOrPending();
				}

				ProjectLoaderRef pl;
				if (pl)
					prjLdrTid = pl->IsFinished() ? 0 : pl->GetThreadId();
			}
			catch (...)
			{
			}

			if (tid == parserTid || tid == prjLdrTid)
			{
				// is there any reason offsets would be wrong during project
				// load or on the ParserThread?
				WTString errMsg;
				errMsg.WTFormat(
				    "GetSymDefStrs idx %s is corrupt, fid=0x%lx, sp=0x%lx, rel=0x%lx, act=0x%lx tid=%ld uiTid=%ld",
				    WTString(mDsFiles.FilePath()).c_str(), fileID, splitNo, splitOffset,
				    mDsFiles.GetActualOffset(fileID, splitNo, splitOffset), tid, g_mainThread);
				VALOGERROR(errMsg.c_str());
				// Sean, if you get here, zip and send dsFiles.mFilePath along with startup.log containing the above
				// message.
				ASSERT_ONCE(!"GetSymDefStrs found corrupt db");
			}
			else
			{
				// offsets may be wrong on UI, scope and underlining threads -
				// this is not necessarily a sign of corruption - but could be...
				// they would be wrong on these threads if for example the parser
				// thread was active loading external changes.
				// we don't block the UI from painting while the project is loading...

				// warn on ui, scope and underline threads.
				// error on concurrent parse threads but not currently differentiated here.
				WTString errMsg;
				errMsg.WTFormat("WARN: GetSymDefStrs idx %s in use(%d:%d:%ld), fid=0x%lx, sp=0x%lx, rel=0x%lx, "
				                "act=0x%lx tid=%ld uiTid=%ld",
				                WTString(mDsFiles.FilePath()).c_str(), parserThrdActive, parserThrdActiveNormal,
				                g_threadCount, fileID, splitNo, splitOffset,
				                mDsFiles.GetActualOffset(fileID, splitNo, splitOffset), tid, g_mainThread);
				Log(errMsg.c_str());
			}

			*outSym = ' ';
			if (outDef)
				*outDef = ' ';
			return;
		}
		buf[kBufLen] = '\0';

		// skip newline, user visibility and first delimiter
		const int kStrOffset = 3;
		LPCSTR p = &buf[kStrOffset];
		size_t fieldLen = 0;
		for (; p[fieldLen] && p[fieldLen] != DB_FIELD_DELIMITER; fieldLen++)
			;

		if (fieldLen >= kBufLen)
		{
			// didn't read in enough, retry
			continue;
		}

		*outSym = WTString(p, (int)fieldLen);

		if (!outDef)
			break;

		p += fieldLen + 1;
		for (fieldLen = 0; p[fieldLen] && p[fieldLen] != DB_FIELD_DELIMITER; fieldLen++)
			;

		*outDef = WTString(p, (int)fieldLen);

		// [case: 68099]
		// if p[fieldLen] is NULL then that means we didn't read the whole record - try again
		// otherwise break
		// only try MAX_READ_ATTEMPTS times
		if (p[fieldLen])
			break;

		if (readAttempts == MAX_READ_ATTEMPTS)
		{
			vLog("ERROR: DBFP::GSDStrs retry fail %s\n", outSym->c_str());
		}
	}
}

void DBFilePair::GetSymDefStrs(DType* SymDef, bool getBoth,
                               std::function<void(const WTString& symscope, const WTString& def)> setter)
{
	WTString symscope, def;
	GetSymDefStrs(SymDef->FileId(), SymDef->GetDbSplit(), SymDef->GetDbOffset(), &symscope, getBoth ? &def : nullptr);

	if (setter)
		setter(symscope, def);
	else
	{
		if (getBoth)
			SymDef->SetStrs(symscope, def);
		else
			SymDef->SetSym(symscope);
	}
}

BOOL DBFilePair::OpenForWrite(DWORD fileID, BOOL append /*= FALSE*/)
{
#ifdef _DEBUG
	if (g_threadCount != 1)
		ASSERT_ONCE(!"DBFilePair::OpenForWrite called with g_threadCount != 1");
#endif // _DEBUG
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());

	if (!append)
	{
		// [case: 63058] [case: 74441]
		// DBFile::OpenForWrite will call RemoveFile if !append, however it won't
		// update DBFilePair::mActiveRemove which is necessary to prevent blocking of
		// UI thread.  No impact on OpenForWrite if we do this here instead.
		RemoveFile(fileID);
	}

	mLock[IdxLock].Lock();
	mLock[DsLock].Lock();
	_ASSERTE(mSortBinCache.empty());
	_ASSERTE(mGlobalDicCache.empty());
	wFileID = fileID;
	const BOOL idxRes = mIdxFiles.OpenForWrite(fileID, append);
	const BOOL dsRes = mDsFiles.OpenForWrite(fileID, append);
	if (!idxRes || !dsRes)
		StopWriting();
	return idxRes && dsRes;
}

void DBFilePair::StopWriting()
{
	SortBins::SortBinDataCache localSortBinCache;
	std::vector<DType> localProjCache;
	localSortBinCache.swap(mSortBinCache);
	localProjCache.swap(mGlobalDicCache);
	// release in opposite order acquired
	mLock[DsLock].Unlock();
	mLock[IdxLock].Unlock();

	for (auto& it : localProjCache)
	{
		// mGlobalDicCache items get added twice.
		// this is the first add.
		// the second add occurs during load of dbfile.
		// mark the first add as without dbfile backing so that these can be removed.
		// #parseFileidNotDbBacked
		_ASSERTE((it.Attributes() & V_FILENAME) && !(it.Attributes() & V_IDX_FLAG_INCLUDE));
		it.mTypeAndDbFlags &= ~VA_DB_BackedByDatafile;
		g_pGlobDic->add(&it);
	}

	g_SortBins.Add(mDbDir, localSortBinCache);
}

#if defined(VA_CPPUNIT) || defined(_DEBUG)

void DBFilePair::VerifyFilePair(DWORD fileID)
{
	WTString sym, def;
	UINT sz;

	void* rawArray = ReadIdxFile(fileID, sz);
	DType* ary = (DType*)rawArray;
	UINT count = sz / sizeof(DType);
	{
		AutoLockCs dsLock(mLock[DsLock]);
		for (UINT i = 0; i < count; i++)
			GetSymDefStrs(fileID, ary[i].GetDbSplit(), ary[i].GetDbOffset(), &sym, &def);
	}

	if (rawArray)
		free(rawArray);
}

void DBFilePair::VerifyIndex(const DBFileTocListElem* dbx)
{
	// Get SymDefIdx
	char c = 0;
	{
		DType idx; // initializes mSymData to nullptr
		UINT sz = mIdxFiles.ReadFromOffset(dbx->mOffset, &idx, sizeof_reduced_DType);
		if (sz != sizeof_reduced_DType /*|| idx.fileID == dbx->fileID*/)
			return; // Null Sized File, no pointer to ds entry

		// Make sure it points to a valid location in the ds file
		mDsFiles.ReadFromCookie((DWORD)idx.mSplitIdx, idx.mIdxOffset, dbx->mFileID, &c, 1);
	}
	if (c != '\n')
	{
		_ASSERTE(c == '\n' && "DBFilePair::VerifyIndex");
	}
}

void DBFilePair::VerifyIndexes()
{
	AutoLockCs idxLock(mLock[IdxLock]);
	AutoLockCs dsLock(mLock[DsLock]);
	for (const DBFileTocListElem* dbx = mIdxFiles.FirstDbx(); dbx; dbx = dbx->mNext)
	{
		VerifyIndex(dbx);
	}
}
#endif

void DBFilePair::Close()
{
	AutoLockCs idxLock(mLock[IdxLock]);
	AutoLockCs dsLock(mLock[DsLock]);
	wFileID = NULL;
	mIdxFiles.Close();
	mDsFiles.Close();
}

void DBFilePair::RemoveFile(DWORD fileId)
{
	ScopedIncrement c(&mActiveRemove);

	{
		AutoLockCs idxLock(mLock[IdxLock]);
		mIdxFiles.RemoveFile(fileId);
	}

	{
		AutoLockCs dsLock(mLock[DsLock]);
		mDsFiles.RemoveFile(fileId);
	}
}

void DBFilePair::InvalidateFile(DWORD fileId)
{
	{
		AutoLockCs idxLock(mLock[IdxLock]);
		mIdxFiles.InvalidateFile(fileId);
	}

	{
		AutoLockCs dsLock(mLock[DsLock]);
		mDsFiles.InvalidateFile(fileId);
	}
}

void DBFilePair::RemoveInvalidatedFiles()
{
	ScopedIncrement c(&mActiveRemove);

	{
		AutoLockCs idxLock(mLock[IdxLock]);
		mIdxFiles.RemoveInvalidatedFiles();
	}

	{
		AutoLockCs dsLock(mLock[DsLock]);
		mDsFiles.RemoveInvalidatedFiles();
	}
}

// VADatabase
// ------------------------------------------------------------------------
//

VADatabase::VADatabase()
    : mOkToIgnoreGetSymStrs(FALSE),
#if defined(VA_CPPUNIT)
      // can't rely on goldfiles to work across machines due to fileID differences (see GetDbPairId)
      mDbPairSplitCount(1)
#else
      // use ID_RK_APP_KEY instead of ID_RK_APP since this is a global class
      // that is instantiated before settings are init'd
      mDbPairSplitCount(GetRegDword(HKEY_CURRENT_USER, ID_RK_APP_KEY, "DbSplitCnt", 100))
#endif
{
	mDbs = new DBFilePair*[mDbPairSplitCount];
	for (DWORD i = 0; i < mDbPairSplitCount; i++)
		mDbs[i] = new DBFilePair[DbTypeId_Count];
}

VADatabase::~VADatabase()
{
	ReleaseDbsForExit();
}

void VADatabase::ReleaseDbsForExit()
{
	if (!mDbs)
		return;

	DBFilePair** dbs = mDbs;
	mDbs = nullptr;

	for (DWORD i = 0; i < mDbPairSplitCount; i++)
	{
		delete[] dbs[i];
		dbs[i] = nullptr;
	}

	delete[] dbs;
}

void VADatabase::Save()
{
	if (!mDbs)
		return;

	auto saver = [this](DbTypeId saveIdx) {
		for (DWORD dbn = 0; dbn < mDbPairSplitCount; dbn++)
		{
			mDbs[dbn][saveIdx].Save();
		}
	};

	if (!Psettings || Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, saver);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, saver);
}

void VADatabase::Init(const CStringW& baseDir, const CStringW& projDbDir)
{
	if (!gDbFileTocHeap.IsCreated())
		gDbFileTocHeap.Create(1 * 1024 * 1024, "DbfToc");
	mBaseDir = baseDir;
	mProjDbDir = projDbDir;

	CStringW tmp(mProjDbDir.Mid(mBaseDir.GetLength()));
	tmp = tmp.Left(tmp.GetLength() - 1);
	mProjDbInfoFileBasename = mBaseDir + L"DbInfo\\" + tmp;
	mProjDbLoadingSentinelFile = mProjDbDir + L"LoadSentinel.va";

	if (!::IsDir(mBaseDir + L"DbInfo"))
		::CreateDir(mBaseDir + L"DbInfo");

	if (::IsDir(mProjDbDir))
	{
		CheckProjDbInfo();
	}
	else
	{
		::CreateDir(mProjDbDir);
	}

	OpenDB();
}

std::shared_ptr<DbFpWriter> VADatabase::GetDBWriter(DWORD fileID, DbTypeId dbID, BOOL append /*= FALSE*/)
{
	DBFilePair* db = GetPair(fileID, dbID);
	std::shared_ptr<DbFpWriter> dbw(new DbFpWriter(db, fileID, append));
	return dbw;
}

void VADatabase::GetSymDefStrs(DType* symDef, BOOL getBoth,
                               std::function<void(const WTString& symscope, const WTString& def)> setter)
{
	DbTypeId dbID = DBIdFromDbFlags(symDef->DbFlags());
	DBFilePair* pr = GetPair(symDef->FileId(), dbID);
	pr->GetSymDefStrs(symDef, getBoth, std::move(setter));
}

DbTypeId VADatabase::DBIdFromDbFlags(UINT dbFlags)
{
	if (dbFlags & VA_DB_LocalsParseAll)
		return DbTypeId_LocalsParseAll;
	if (dbFlags & VA_DB_SolutionPrivateSystem)
	{
		if (dbFlags & VA_DB_Cpp)
			return DbTypeId_SolutionSystemCpp;
		// 		if (dbFlags & VA_DB_Net)
		// 			return DbTypeId_SolutionSystemNet;

		_ASSERTE(!"bad use of VA_DB_SolutionPrivateSystem");
	}
	if (dbFlags & VA_DB_Cpp)
		return DbTypeId_Cpp;
	if (dbFlags & VA_DB_Net)
		return DbTypeId_Net;
	if (dbFlags & VA_DB_Solution)
		return DbTypeId_Solution;
	return DbTypeId_ExternalOther;
}

uint VADatabase::DbFlagsFromDbId(DbTypeId dbId)
{
	// note: VA_DB_BackedByDatafile is not set
	switch (dbId)
	{
	case DbTypeId_Error:
		return 0;
	case DbTypeId_Net:
		return VA_DB_Net;
	case DbTypeId_Cpp:
		return VA_DB_Cpp;
	case DbTypeId_ExternalOther:
		return VA_DB_ExternalOther;
	case DbTypeId_Solution:
		return VA_DB_Solution;
	case DbTypeId_LocalsParseAll:
		return VA_DB_LocalsParseAll;
	case DbTypeId_SolutionSystemCpp:
		return VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
		// 	case DbTypeId_SolutionSystemNet:
		// 		return VA_DB_SolutionPrivateSystem | VA_DB_Net;
	case DbTypeId_Count:
		return 0;
	default:
		_ASSERTE(!"unhandled DbTypeId");
		break;
	}

	return 0;
}

DbTypeId VADatabase::DBIdFromParams(int fType, DWORD fileID, BOOL sysFile, BOOL parseAll, uint dbFlags /*= 0*/)
{
	if (dbFlags & VA_DB_ExternalOther)
		return DbTypeId_ExternalOther;
	if (dbFlags & VA_DB_SolutionPrivateSystem)
	{
		_ASSERTE(dbFlags & VA_DB_Cpp);
		return DbTypeId_SolutionSystemCpp;
		// 		if (dbFlags & VA_DB_Net);
		// 			return DbTypeId_SolutionSystemNet;
	}
	if (parseAll)
		return DbTypeId_LocalsParseAll;
	if (sysFile)
	{
		if (Defaults_to_Net_Symbols(fType))
			return DbTypeId_Net;
		return DbTypeId_Cpp;
	}
	if (GlobalProject && !GlobalProject->IsBusy())
	{
		DBFilePair* dbpair = &mDbs[GetDbPairId(fileID)][DbTypeId_Solution];
		if (!dbpair->Contains(fileID))
		{
			// Any files newly parsed after project load go into cache.
			// assumes that project causes all files to be added to db and that
			// files added to project after load are forced into db properly
			// (see dbw->DBOut("+projfile") in FileParserWorkItem::DoParseWork).
			// This prevents external symbols from being loaded at next solution load.
			return DbTypeId_ExternalOther;
		}
	}
	return DbTypeId_Solution;
}

void VADatabase::SetFileTime(DWORD fileID, DbTypeId dbID, const FILETIME* ft)
{
	DBFilePair* db = GetPair(fileID, dbID);
	if (db)
		db->SetFileTime(fileID, ft);
}

FILETIME* VADatabase::GetFileTime(DWORD fileID, DbTypeId dbID)
{
	if (dbID == DbTypeId_Error)
	{
		dbID = DBIdContainingFileID(fileID);
		if (dbID == DbTypeId_Error)
			return NULL;
	}

	DBFilePair* db = GetPair(fileID, dbID);
	if (db)
		return db->GetFileTime(fileID);

	return nullptr;
}

void VADatabase::InvalidateFileTime(const CStringW& file)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	UINT fileID = gFileIdManager->GetFileId(file);
	FILETIME ft;
	ft.dwHighDateTime = ft.dwLowDateTime = 0;
	for (int i = 0; i < DbTypeId_Count; i++)
	{
		DBFilePair* dbpair = &mDbs[GetDbPairId(fileID)][i];
		dbpair->SetFileTime(fileID, &ft);
	}
}

DbTypeId VADatabase::DBIdContainingFileID(DWORD fileID)
{
	for (int id = 0; id < DbTypeId_Count; id++)
	{
		DBFilePair* dbpair = &mDbs[GetDbPairId(fileID)][id];
		if (dbpair->Contains(fileID))
			return (DbTypeId)id;
	}
	return DbTypeId_Error;
}

void VADatabase::RemoveFile(DWORD fileId)
{
	auto remover = [this, fileId](DbTypeId dbId) {
		DBFilePair* db = GetPair(fileId, dbId);
		if (db)
			db->RemoveFile(fileId);
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, remover);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, remover);
}

void VADatabase::InvalidateFile(DWORD fileId)
{
	auto invalidater = [this, fileId](DbTypeId dbId) {
		DBFilePair* db = GetPair(fileId, dbId);
		if (db)
			db->InvalidateFile(fileId);
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, invalidater);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, invalidater);
}

void VADatabase::RemoveInvalidatedFiles()
{
	// this should only be called during UpdateModifiedFiles
	_ASSERTE(GlobalProject && GlobalProject->IsBusy());
	auto remover = [this](DbTypeId idx) {
		for (DWORD dbn = 0; dbn < mDbPairSplitCount; dbn++)
			mDbs[dbn][idx].RemoveInvalidatedFiles();
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, remover);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, remover);
}

void VADatabase::Close()
{
	auto closer = [this](DbTypeId closeIdx) {
		for (DWORD dbn = 0; dbn < mDbPairSplitCount; dbn++)
			mDbs[dbn][closeIdx].Close();
	};

	if (!Psettings || Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, closer);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, closer);

	if (!mProjDbDir.IsEmpty())
	{
		// [case: 74480] [case: 141298]
		CStringW dir, infFile;

		dir = mProjDbDir;
		infFile = mProjDbInfoFileBasename + L".inf.va";
		RecordProjDbInfo(dir, infFile);

		dir = mProjDbDir + kDbIdDir[DbTypeId_LocalsParseAll];
		infFile = mProjDbInfoFileBasename + L"_l.inf.va";
		RecordProjDbInfo(dir, infFile);

		dir = mProjDbDir + kDbIdDir[DbTypeId_SolutionSystemCpp];
		infFile = mProjDbInfoFileBasename + L"_s.inf.va";
		RecordProjDbInfo(dir, infFile);

		//		dir = mProjDbDir + kDbIdDir[DbTypeId_SolutionSystemNet];
		// 		infFile = mProjDbInfoFile + L"_n.inf.va";
		// 		RecordProjDbInfo(dir, infFile);
	}

	mBaseDir.Empty();
	mProjDbDir.Empty();
	mProjDbInfoFileBasename.Empty();
	mProjDbLoadingSentinelFile.Empty();

	gDbFileTocHeap.AssertEmpty();
}

void VADatabase::OpenDB()
{
	auto opener = [this](DbTypeId initIdx) {
		for (DWORD dbn = 0; dbn < mDbPairSplitCount; dbn++)
		{
			mDbs[dbn][initIdx].Init(mBaseDir, mProjDbDir, initIdx, (int)dbn);
		}
	};

	if (!Psettings || Psettings->mUsePpl)
		Concurrency::parallel_for(DbTypeId_First, DbTypeId_Count, opener);
	else
		::serial_for<DbTypeId>(DbTypeId_First, DbTypeId_Count, opener);

	_ASSERTE(!mProjDbLoadingSentinelFile.IsEmpty());
	CFileW file;
	file.Open(mProjDbLoadingSentinelFile,
	          CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit);
}

void VADatabase::RecordProjDbInfo(const CStringW& dir, const CStringW& infFile)
{
	// [case: 74480]
	CStringW lst(::BuildTextDirList(dir, FALSE, FALSE));
	CFileW file;
	if (!file.Open(infFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
	{
		vLog("ERROR: failed to record db info");
		return;
	}

	file.Write(lst, lst.GetLength() * sizeof(WCHAR));
	file.Close();
}

bool VADatabase::VerifyProjDbInfo(const CStringW& dir, const CStringW& infFile)
{
	// [case: 74480]
	if (!::IsFile(infFile))
	{
		if (!Psettings || Psettings->mVerifyDbOnLoad)
			return false;
		else
		{
			vLog("WARN: db verify disabled -- override rebuild 1: %s", (const char*)CString(dir));
			return true;
		}
	}

	CStringW previousDirInfo;
	if (!::ReadFileUtf16(infFile, previousDirInfo))
	{
		vLog("ERROR: failed to read prev db info: %s", (const char*)CString(dir));
		if (!Psettings || Psettings->mVerifyDbOnLoad)
			return false;
		else
		{
			vLog("WARN: db verify disabled -- override rebuild 2: %s", (const char*)CString(dir));
			return true;
		}
	}

	_ASSERTE(!mProjDbLoadingSentinelFile.IsEmpty());
#ifdef _DEBUG
	// [case: 141708]
	// normal verify in debug builds -- no accommodation for crash during solution load
#else
	if (::IsFile(mProjDbLoadingSentinelFile))
	{
		// [case: 88040]
		// opening a solution that previously crashed during sln load
		// do not force rebuild
		vLog("WARN: previous sln load appears to have crashed -- not forcing db rebuild: %s",
		     (const char*)CString(dir));
#if defined(SEAN) || defined(VA_CPPUNIT)
		_ASSERTE(!"sln load sentinelFile is still present");
#endif
		return true;
	}
#endif

	const CStringW actualDirInfo(::BuildTextDirList(dir, FALSE, FALSE));
	if (actualDirInfo.GetLength() != previousDirInfo.GetLength())
	{
		if (!Psettings || Psettings->mVerifyDbOnLoad)
			return false;
		else
		{
			vLog("WARN: db verify disabled -- override rebuild 3: %s", (const char*)CString(dir));
			return true;
		}
	}

	const bool res = actualDirInfo == previousDirInfo;
	if (!Psettings || Psettings->mVerifyDbOnLoad)
		return res;

	vLogUnfiltered("WARN: db verify disabled -- override rebuild 4: %s", (const char*)CString(dir));
	return true;
}

void VADatabase::CheckProjDbInfo()
{
	// [case: 74480] [case: 141298]
	CStringW dir, infFile;

	dir = mProjDbDir;
	infFile = mProjDbInfoFileBasename + L".inf.va";
	if (!VerifyProjDbInfo(dir, infFile))
	{
		vLogUnfiltered("WARN: project db 1 invalid - auto destroy");
		::CleanDir(dir, L"*.*");
	}

	dir = mProjDbDir + kDbIdDir[DbTypeId_LocalsParseAll];
	infFile = mProjDbInfoFileBasename + L"_l.inf.va";
	if (!VerifyProjDbInfo(dir, infFile))
	{
		vLogUnfiltered("WARN: project db 2 invalid - auto destroy");
		::CleanDir(dir + L"\\", L"*.*");
	}

	dir = mProjDbDir + kDbIdDir[DbTypeId_SolutionSystemCpp];
	infFile = mProjDbInfoFileBasename + L"_s.inf.va";
	if (!VerifyProjDbInfo(dir, infFile))
	{
		vLogUnfiltered("WARN: project db 3 invalid - auto destroy");
		::CleanDir(dir + L"\\", L"*.*");
	}

	// 	dir = mProjDbDir + kDbIdDir[DbTypeId_SolutionSystemNet];
	// 	infFile = mProjDbInfoFile + L"_n.inf.va";
	// 	if (!VerifyProjDbInfo(dir))
	// 	{
	// 		vLog("WARN: project db 4 invalid - auto destroy");
	// 		::CleanDir(dir + L"\\", L"*.*");
	// 	}
}

void VADatabase::SolutionLoadCompleted()
{
	::DeleteFileW(mProjDbLoadingSentinelFile);
}

void SortBins::Close()
{
	AutoLockCs l(mSortBinLock);
	for (auto& it : mSortBins)
	{
		AutoLockCs l2(it.second.mFileLock);
		it.second.mLastDir.Empty();
		if (it.second.mUnsortedLetterFile.m_hFile != INVALID_HANDLE_VALUE)
			it.second.mUnsortedLetterFile.Close();
	}
}

void SortBins::Add(const CStringW& dir, const SortBinDataCache& cache)
{
	CStringW symW;
	WTString ln;
	for (SortBins::SortBinDataCache::const_iterator it = cache.begin(); it != cache.end(); ++it)
	{
		const SortBinData& dat(*it);
		if (dat.mSym.IsEmpty() || !ISCSYM(dat.mSym[0]) || wt_isdigit(dat.mSym[0]))
			continue;

		WCHAR uc;
		if (dat.mSym[0] & 0x80)
		{
			symW = dat.mSym.Wide();
			uc = ::towupper(symW[0]);
		}
		else
			uc = (WCHAR)(char)::toupper((char)dat.mSym[0]);

		ln.WTFormat("%s" DB_FIELD_DELIMITER_STR "%d" DB_FIELD_DELIMITER_STR "%d" DB_FIELD_DELIMITER_STR "%d\n",
		            dat.mSym.c_str(), dat.mScopeHash, dat.mType, dat.mAttrs);

		// lock the bin collection
		mSortBinLock.Lock();
		// get reference to the bin we want
		BinInfo& inf = mSortBins[uc];
		// lock the bin we want
		AutoLockCs l(inf.mFileLock);
		// unlock the collection
		mSortBinLock.Unlock();

		if (dir != inf.mLastDir)
		{
			inf.mLastDir = dir;
			if (inf.mUnsortedLetterFile.m_hFile != INVALID_HANDLE_VALUE)
				inf.mUnsortedLetterFile.Close();
		}

		if (inf.mUnsortedLetterFile.m_hFile == INVALID_HANDLE_VALUE)
		{
			const CStringW file = LETTER_FILE_UNSORTED((dir + CStringW(L"\\")), CStringW(uc));
			inf.mUnsortedLetterFile.Open(file, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate |
			                                       CFile::shareDenyNone | CFile::modeNoInherit);
			inf.mUnsortedLetterFile.Seek(0, CFile::end);
		}

		inf.mUnsortedLetterFile.Write(ln.c_str(), (UINT)ln.GetLength());
	}
}
