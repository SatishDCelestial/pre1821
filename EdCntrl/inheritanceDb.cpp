#include "StdAfxEd.h"
#include "inheritanceDb.h"
#include "PARSE.H"
#include "FileTypes.h"
#include "StringUtils.h"
#include "File.h"
#include "Mparse.h"
#include "fdictionary.h"
#include "LogElapsedTime.h"
#include "DBFile\VADBFile.h"
#include "Directories.h"
#include "SqliteDb.h"
#include "ParseThrd.h"
#include "Project.h"
#include "EDCNT.H"
#include "SubClassWnd.h"
#include "FileId.h"
#include "serial_for.h"
#include "Settings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace InheritanceDb
{
// namespace baz { class bah { } }
// namespace foo { class bar : public baz::bah { } }
// 	Type				SymScope					Def
// 	vaInheritsFrom		:foo:bar:+InhF_bar 			:baz:bah
// 	vaInheritedBy 		:baz:bah:+InhB_bah			:foo:bar

const int kInhDbSplits = 10;
const WTString kInheritsFromStr = "+InhF_";
const WTString kInheritedByStr = "+InhB_";

enum class InheritanceDbType
{
	InheritsFrom,
	InheritedBy,
	Count
};

CCriticalSection gInheritanceDbsArrayLock;
SqliteDbPtr gInheritanceDbs[(int)InheritanceDbType::Count][DbTypeId_Count][kInhDbSplits];
bool gInheritanceDbIndexSet[(int)InheritanceDbType::Count][DbTypeId_Count][kInhDbSplits];

void UpdateInheritanceDef(DType& sym, const WTString& newDef);
void QueryInheritanceDb(DbTypeId dbType, uint fileId, uint symType, DTypeList& records);
void QueueResolverForPendingItems(DbTypeId dbId, uint slnHash);

int InheritanceDbTypeFromSymType(int symType)
{
	switch (symType)
	{
	case vaInheritedBy:
		return (int)InheritanceDbType::InheritedBy;
	case vaInheritsFrom:
		return (int)InheritanceDbType::InheritsFrom;
	default:
		_ASSERTE(!"invalid symType -- no conversion to InheritanceDbType");
		return (int)InheritanceDbType::Count;
	}
}

void SolutionLoaded()
{
	uint slnHash = 0;

	{
		AutoLockCs l(gInheritanceDbsArrayLock);
		if (GlobalProject)
			slnHash = GlobalProject->GetSolutionHash();
	}

	if (!g_ParserThread)
		return;

	QueueResolverForPendingItems(DbTypeId_Cpp, slnHash);
	QueueResolverForPendingItems(DbTypeId_Net, slnHash);

	if (slnHash)
	{
		QueueResolverForPendingItems(DbTypeId_SolutionSystemCpp, slnHash);
		QueueResolverForPendingItems(DbTypeId_ExternalOther, slnHash);
		QueueResolverForPendingItems(DbTypeId_Solution, slnHash);
	}

	class CloseInheritanceDb : public ParseWorkItem
	{
		uint mSlnHash;

	  public:
		CloseInheritanceDb(uint slnHash) : ParseWorkItem("CloseDbAfterResolvers"), mSlnHash(slnHash)
		{
		}

		virtual void DoParseWork() override
		{
			uint slnHash = 0;
			{
				AutoLockCs l(gInheritanceDbsArrayLock);
				if (GlobalProject)
					slnHash = GlobalProject->GetSolutionHash();
			}

			if (slnHash && slnHash == mSlnHash)
			{
				// this stuff is done here because we know that resolvers have completed
				// don't want to flush data that will just be read back in for inheritance resolution
				if (GetSysDic())
					GetSysDic()->ReleaseTransientData();
				if (g_pGlobDic)
					g_pGlobDic->ReleaseTransientData();
				Close();
#ifdef _DEBUG
// 				Sleep(500);
// 				DeletePendingSymDefStrs(true);
/*
 *				break here and use the following expressions in the watch wnd to see current memory usage

(_lCurAlloc / 1024) / 1024,d											our default crt heap
	in vs2015:	(__acrt_current_allocations / 1024) / 1024,d
(gWtstrCurAllocBytes / 1024) / 1024,d									WTString memory (subset of default crt usage)
gWtstrActiveAllocs,d													count of active WTStrings
(sqlite3Stat.nowValue[0] / 1024) / 1024,d								sqlite db heap, current usage
(sqlite3Stat.mxValue[0] / 1024) / 1024,d								sqlite db heap, max usage
(gDbFileTocHeap.mCurAllocBytes / 1024) / 1024,d							dbfile heap
(gSharedVaHashTableHeap.mCurAllocBytes / 1024) / 1024,d					shared VaHashTable heap for transient and small
instances (gSharedSymDefStringHeap.mCurAllocBytes / 1024) / 1024,d				The only source of
SymDefStrStruct::FixedString (mSymScope and mDef in SymDefStrStruct) (gSharedSymDefStructHeap.mCurAllocBytes / 1024) /
1024,d				The only source of SymDefStrStructs gSymDefStrsPendingDelete,d
size is number of entries, not bytes, pending removal from gSharedSymDefStructHeap
(s_pSysDic->m_pHashTable->mHeap.mCurAllocBytes / 1024) / 1024,d			SysDic hashtable
(g_pGlobDic->m_pHashTable->mHeap.mCurAllocBytes / 1024) / 1024,d		Solution hashtable
(gFileIdStringManager.mHeap.mCurAllocBytes / 1024) / 1024,d
(gFileIdMgrHeapAllocator.mCurAllocBytes / 1024) / 1024,d
(gFileFinderStringManager.mHeap.mCurAllocBytes / 1024) / 1024,d
(gFileFinderHeapAllocator.mCurAllocBytes / 1024) / 1024,d

 *
 */
//				_asm nop;
#endif

				RunFromMainThread([]() {
					EdCntPtr ed(g_currentEdCnt);
					if (ed)
					{
						// reparse for coloring update
						ed->QueForReparse();
					}
				});
			}
		}
	};

	// queue below normal so that it runs after the resolvers run (which run at priBelowNormal)
	g_ParserThread->QueueParseWorkItem(new CloseInheritanceDb(slnHash), ParserThread::priBelowNormal);
}

void Close()
{
	LogElapsedTime tt("InhDb close", 1000);
	AutoLockCs l(gInheritanceDbsArrayLock);
	for (auto& type : gInheritanceDbs)
		for (auto& typeId : type)
			for (auto& split : typeId)
				split = nullptr;

	for (auto& type : gInheritanceDbIndexSet)
		for (auto& typeId : type)
			for (auto& split : typeId)
				split = false;
}

//
// Open/Load support
//

CStringW GetInheritanceDbFile(DbTypeId dbId, int symType, int splitNumber)
{
	CStringW dbFileW;
	switch (dbId)
	{
	case DbTypeId_Cpp:
	case DbTypeId_Net:
		dbFileW = VaDirs::GetDbDir() + kDbIdDir[dbId] + L"\\";
		break;
	case DbTypeId_Solution:
	case DbTypeId_LocalsParseAll:
		dbFileW = g_pGlobDic->GetDBDir();
		break;
	case DbTypeId_SolutionSystemCpp:
		dbFileW = g_pGlobDic->GetDBDir() + kDbIdDir[dbId] + L"\\";
		break;
	case DbTypeId_ExternalOther:
		dbFileW = VaDirs::GetDbDir() + kDbIdDir[dbId] + L"\\";
		break;
	default:
		_ASSERTE(!"unhandled DbTypeId Inheritance db");
		return dbFileW;
	}

	if (vaInheritsFrom == symType)
		dbFileW += L"inh-F-";
	else if (vaInheritedBy == symType)
		dbFileW += L"inh-B-";
	else
	{
		_ASSERTE(!"bad symType in GetInheritanceDbFile");
		dbFileW.Empty();
		return dbFileW;
	}

	CStringW tmp;
	CString__FormatW(tmp, L"%02d.sdb", splitNumber);
	dbFileW += tmp;
	return dbFileW;
}

SqliteDbPtr OpenInheritanceDb(const CStringW& dbFileW, int symType, DbTypeId dbId, int splitNumber, bool create)
{
	SqliteDbPtr db;
	if (DbTypeId_LocalsParseAll == dbId)
		dbId = DbTypeId_Solution;

	const int symTypeDb = InheritanceDbTypeFromSymType(symType);

	{
		AutoLockCs l(gInheritanceDbsArrayLock);
		db = gInheritanceDbs[symTypeDb][dbId][splitNumber];
		if (db)
		{
			if (dbFileW == db->GetDbFilePath())
			{
				if (create)
					return db;
			}
			else
				db = nullptr;
		}

		if (!create && !db)
		{
			if (!IsFile(dbFileW))
				return db;
		}

		if (!db)
		{
			db = std::make_shared<SqliteDb>(dbFileW);
			if (!db)
				return db;

			db->open(dbFileW);

			if (create)
			{
				WTString cmd;

				// http://sqlite.org/pragma.html#pragma_synchronous
				cmd += "PRAGMA synchronous = OFF; ";
				// http://sqlite.org/pragma.html#pragma_journal_mode (significant performance improvement)
				cmd += "PRAGMA journal_mode = OFF; ";

				cmd += "create table if not exists inheritance ( "
				       "ScopeId unsigned int, "
				       "SymId unsigned int, "
				       "Sym varchar(2048), "
				       "Def varchar(2048), "
				       "Flags unsigned int, "
				       "Type int, "
				       "Attrs unsigned int, "
				       "Line int, "
				       "FileId unsigned int "
				       "); ";

				// don't create the indexes until they are actually needed.  They are
				// not needed during insert and negatively impact insert performance.
				// !create means read-access where indexes are of benefit.

				db->execDML(cmd.c_str());

				if (symType == vaInheritsFrom)
				{
					cmd = "create table if not exists PendingResolve ( "
					      "FileId unsigned int NOT NULL PRIMARY KEY"
					      "); ";

					db->execDML(cmd.c_str());
				}
			}

			gInheritanceDbs[symTypeDb][dbId][splitNumber] = db;
		}
	}

	if (!create)
	{
		// !create means read-access, so make sure indexes are present
		AutoLockCsPtr l(db->GetTransactionLock());
		if (!gInheritanceDbIndexSet[symTypeDb][dbId][splitNumber])
		{
			db->execDML("create index if not exists IxScopeId on inheritance (ScopeId, SymId);");
			db->execDML("create index if not exists IxFileId on inheritance (FileId);");
			if (symType == vaInheritsFrom)
				db->execDML("create index if not exists IxFileId on PendingResolve (FileId);");
			gInheritanceDbIndexSet[symTypeDb][dbId][splitNumber] = true;
		}
	}

	return db;
}

SqliteDbPtr OpenInheritanceDbForFileId(uint fileId, DbTypeId dbId, int symType, bool create)
{
	const int kSplitNumber = int(fileId % kInhDbSplits);
	const CStringW dbFileW(GetInheritanceDbFile(dbId, symType, kSplitNumber));
	if (dbFileW.IsEmpty())
		return SqliteDbPtr();

	return OpenInheritanceDb(dbFileW, symType, dbId, kSplitNumber, create);
}

SqliteDbPtr OpenInheritanceDbBySplit(int splitNumber, DbTypeId dbId, int symType)
{
	const CStringW dbFileW(GetInheritanceDbFile(dbId, symType, splitNumber));
	if (dbFileW.IsEmpty())
		return SqliteDbPtr();

	return OpenInheritanceDb(dbFileW, symType, dbId, splitNumber, false);
}

//
// Purge support
//

void PurgeInheritance(UINT fileId, DbTypeId dbType, int symType)
{
	try
	{
		WTString cmd;
		cmd.WTFormat("delete from inheritance where FileId = 0x%x;", fileId);

		SqliteDbPtr db = OpenInheritanceDbForFileId(fileId, dbType, symType, false);
		if (!db)
			return;

		AutoLockCsPtr l(db->GetTransactionLock());
		db->execDML(cmd.c_str());
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("PurgeInh:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in PurgeInheritance");
	}
}

void PurgeInheritance(const std::set<UINT>& fileIds, DbTypeId dbType, int symType)
{
	try
	{
		for (auto fileId : fileIds)
			PurgeInheritance(fileId, dbType, symType);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("PurgeInh:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in PurgeInheritance");
	}
}

void PurgeInheritance(const std::set<UINT>& fileIds, DTypeDbScope dbScp)
{
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));
	_ASSERTE(((DWORD)dbScp & (DWORD)DTypeDbScope::dbSlnAndSys));
	_ASSERTE(dbScp != DTypeDbScope::dbSystemIfNoSln);
	LogElapsedTime tt("PurgeInh list", 2000);

	struct PurgeData
	{
		DbTypeId mDbId;
		int mType;
	};
	std::vector<PurgeData> pd;

	// purge sln private sys in any case
	pd.push_back({DbTypeId_SolutionSystemCpp, vaInheritsFrom});
	pd.push_back({DbTypeId_SolutionSystemCpp, vaInheritedBy});

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
	{
		pd.push_back({DbTypeId_Solution, vaInheritsFrom});
		pd.push_back({DbTypeId_Solution, vaInheritedBy});
	}

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
	{
		pd.push_back({DbTypeId_Cpp, vaInheritsFrom});
		pd.push_back({DbTypeId_Cpp, vaInheritedBy});
		pd.push_back({DbTypeId_Net, vaInheritsFrom});
		pd.push_back({DbTypeId_Net, vaInheritedBy});
	}

	// This call had me a bit concerned.  But leave it for parity with
	// FileDic::RemoveAllDefsFrom_SinglePass which does remove file from
	// all vadbs -- regardless of on which FileDic instance the call was
	// exec'd.
	pd.push_back({DbTypeId_ExternalOther, vaInheritsFrom});
	pd.push_back({DbTypeId_ExternalOther, vaInheritedBy});

	auto fn = [&fileIds](const PurgeData& dat) { PurgeInheritance(fileIds, dat.mDbId, dat.mType); };

	if (Psettings->mUsePpl)
		concurrency::parallel_for_each(pd.cbegin(), pd.cend(), fn);
	else
		std::for_each(pd.cbegin(), pd.cend(), fn);
}

void PurgeInheritance(const CStringW& file, DTypeDbScope dbScp)
{
	const uint fileId = gFileIdManager->GetFileId(file);
	std::set<UINT> fids;
	fids.insert(fileId);
	PurgeInheritance(fids, dbScp);
}

//
// Write/Store support
//

class InheritanceTypeResolver : public ParseWorkItem
{
  public:
	InheritanceTypeResolver(uint fileId, DbTypeId dbId, uint slnHash)
	    : ParseWorkItem("InheritanceTypeResolver-Pending"), mFileId(fileId), mSolutionHash(slnHash), m_VADbId(dbId)
	{
		const CStringW f(gFileIdManager->GetFile(fileId));
		mFtype = GetFileType(f);
		// mInheritanceCache will be populated during the job via a query based on fileId and dbId
	}

	InheritanceTypeResolver(uint fileId, DbTypeId dbId, uint slnHash, int ftype, DTypeList& cache)
	    : ParseWorkItem("InheritanceTypeResolver-Cached"), mFileId(fileId), mSolutionHash(slnHash), m_VADbId(dbId),
	      mFtype(ftype)
	{
		mInheritanceCache.swap(cache);
	}

	virtual void DoParseWork()
	{
		if (!GlobalProject || mSolutionHash != GlobalProject->GetSolutionHash())
			return; // another solution loaded while we were waiting

		if (mInheritanceCache.empty())
			QueryInheritanceDb(m_VADbId, mFileId, vaInheritsFrom, mInheritanceCache);

		// purge from same db; not a general purge; same dbType and only for vaInheritedBy
		PurgeInheritance(mFileId, m_VADbId, vaInheritedBy);

		DbOutData dbOutCache;
		MultiParsePtr mp = MultiParse::Create(mFtype);
		for (auto& dt : mInheritanceCache)
		{
			if (StopIt)
				return;

			auto dtptr = &dt;
			WTString base = dtptr->Def();
			_ASSERTE(dtptr->type() == vaInheritsFrom);
			WTString cls = StrGetSym(base);
			WTString scp = StrGetSymScope(base);
			if (scp.IsEmpty())
				scp = DB_SEP_STR;
			WTString bcl;

			WTString tmpbcl = StrGetSymScope(dtptr->Scope());
			while (!tmpbcl.IsEmpty())
			{
				bcl += tmpbcl;
				bcl += '\f';
				tmpbcl = StrGetSymScope(tmpbcl);
			}

			DType* baseType = mp->FindSym(&cls, &scp, &bcl, FDF_NoConcat);
			if (baseType)
			{
				int loopCount = 0;
				while (baseType->MaskedType() == TYPE)
				{
					if (++loopCount > 10)
					{
						vLog("WARN: ITR: breaking out of typedef (%s)", baseType->SymScope().c_str());
						break;
					}

					auto types = GetTypesFromDef(baseType, TYPE, mFtype);
					token2 tk(types);
					if (!tk.more())
						break;

					WTString base2 = tk.read2("\f"); // use first def, might not be right
					if (base2.IsEmpty())
						break;

					WTString cls2 = StrGetSym(base2);
					cls2 = StripEncodedTemplates(cls2);
					WTString scp2 = StrGetSymScope(base2);
					if (scp2.IsEmpty())
						scp2 = baseType->Scope();
					if (scp2.IsEmpty())
						scp2 = DB_SEP_STR;

					auto typedefdType = mp->FindSym(&cls2, &scp2, NULL, FDF_NoConcat);
					if (typedefdType && baseType != typedefdType)
						baseType = typedefdType;
					else
						break;
				}

				if (base != baseType->SymScope())
				{
					base = baseType->SymScope();
					InheritanceDb::UpdateInheritanceDef(dt, base);
				}
			}

			WTString revSymScope = base + DB_SEP_STR + InheritanceDb::kInheritedByStr + StrGetSym(base);
			uint dbFlags = dtptr->DbFlags();
			if (dbFlags & VA_DB_BackedByDatafile)
			{
				// the new vaInheritedBy record is not a VA_DB_BackedByDatafile entry
				dbFlags ^= VA_DB_BackedByDatafile;
			}

			dbOutCache.push_back(DbOutParseData{revSymScope, dtptr->Scope(), vaInheritedBy, dtptr->Attributes(),
			                                    dtptr->Line(), dtptr->FileId(), dbFlags});
		}

		mInheritanceCache.clear();

		InheritanceDb::StoreInheritanceData(dbOutCache, m_VADbId, mFtype, vaInheritedBy, mSolutionHash);
		dbOutCache.clear();

		if (GlobalProject && GlobalProject->GetSolutionHash() == mSolutionHash)
		{
			// remove fileId from PendingResolve table if solutionHash hasn't changed
			try
			{
				SqliteDbPtr db;
				AutoLockCsPtr l;
				db = OpenInheritanceDbForFileId(mFileId, m_VADbId, vaInheritsFrom, false);
				if (db)
				{
					l.Lock(db->GetTransactionLock());
					WTString cmd;
					cmd.WTFormat("delete from PendingResolve where FileId = 0x%x;", mFileId);
					db->execDML(cmd.c_str());
				}
				else
				{
					// mFileId should have been read out of the db, so it should
					// still be there for us to remove now
					_ASSERTE(!"InheritanceTypeResolver fail to open db");
				}
			}
			catch (CppSQLite3Exception& e)
			{
				VALOGEXCEPTION((WTString("ITR:") + e.errorMessage()).c_str());
				_ASSERTE(!"Exception in InheritanceTypeResolver");
			}
		}
	}

  private:
	const uint mFileId;
	const uint mSolutionHash;
	const DbTypeId m_VADbId;
	int mFtype;
	DTypeList mInheritanceCache;
};

void StoreInheritanceData(DbOutData& inheritanceRecords, DbTypeId dbId, int fileType, int symType, uint slnHash)
{
	if (!inheritanceRecords.size())
		return;

	_ASSERTE(symType == (int)(inheritanceRecords.begin()->mType & TYPEMASK));
	DTypeList inheritanceCache;

	try
	{
		uint lastFileId = 0;
		WTString cmd;
		SqliteDbPtr db;
		AutoLockCsPtr l;
		// in general, all the records will be from the same fileId
		const bool doAsTransaction = inheritanceRecords.size() > 3;

		for (auto& it : inheritanceRecords)
		{
			const uint curFileId = it.mFileId;
			if (curFileId != lastFileId || !db)
			{
				if (vaInheritsFrom == symType && lastFileId)
				{
					// note in PendingResolve db that entries from fileId need to be resolved
					_ASSERTE(db);
					cmd.WTFormat("insert into PendingResolve values (0x%x);", lastFileId);
					try
					{
						db->execDML(cmd.c_str());
					}
					catch (CppSQLite3Exception& e)
					{
						// unique constraint will cause throw if fileId is already in table
						const char* errMsg = e.errorMessage();
						if (errMsg && !strstr(errMsg, "SQLITE_CONSTRAINT"))
							VALOGEXCEPTION((WTString("SID-Inh:PR1:") + errMsg).c_str());
					}

					if (g_ParserThread)
						g_ParserThread->QueueParseWorkItem(
						    new InheritanceTypeResolver(lastFileId, dbId, slnHash, fileType, inheritanceCache),
						    ParserThread::priBelowNormal);
				}

				if (db)
				{
					if (doAsTransaction)
						db->execDML("end transaction;");
					l.Unlock();
				}

				db = OpenInheritanceDbForFileId(curFileId, dbId, symType, true);
				if (!db)
				{
					_ASSERTE(!"StoreInheritanceData fail to open db");
					break;
				}

				l.Lock(db->GetTransactionLock());
				if (doAsTransaction)
					db->execDML("begin transaction;");
			}

			_ASSERTE((it.mType & VA_DB_FLAGS_MASK) == it.mType);
			_ASSERTE(!(it.mType & VA_DB_BackedByDatafile)); // redundant since mType should not have any DbFlags...
			it.mDbFlags = VADatabase::DbFlagsFromDbId(dbId);
			const uint scopeHash = WTHashKey(StrGetSymScope(it.mSym));
			const uint symHash = WTHashKey(StrGetSym(it.mSym));

			cmd.WTFormat("insert into inheritance values (0x%x, 0x%x, '%s', '%s', 0x%x, %d, 0x%x, %d, 0x%lx);",
			             scopeHash, symHash, it.mSym.c_str(), it.mDef.c_str(), it.mDbFlags, (int)it.mType, it.mAttrs,
			             it.mLine, it.mFileId);
			_ASSERTE(db);
			db->execDML(cmd.c_str());

			if (vaInheritsFrom == symType)
				inheritanceCache.push_back({scopeHash, symHash, 0, it.mSym, it.mDef, vaInheritsFrom, it.mAttrs,
				                            it.mDbFlags, it.mLine, it.mFileId});

			lastFileId = curFileId;
		}

		if (g_ParserThread && lastFileId && vaInheritsFrom == symType)
		{
			_ASSERTE(db);
			cmd.WTFormat("insert into PendingResolve values (0x%x);", lastFileId);
			try
			{
				db->execDML(cmd.c_str());
			}
			catch (CppSQLite3Exception& e)
			{
				// unique constraint will cause throw if fileId is already in table
				const char* errMsg = e.errorMessage();
				if (errMsg && !strstr(errMsg, "SQLITE_CONSTRAINT"))
					VALOGEXCEPTION((WTString("SID-Inh:PR2:") + errMsg).c_str());
			}

			g_ParserThread->QueueParseWorkItem(
			    new InheritanceTypeResolver(lastFileId, dbId, slnHash, fileType, inheritanceCache),
			    ParserThread::priBelowNormal);
		}

		if (doAsTransaction && db)
			db->execDML("end transaction;");
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("SID-Inh:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in StoreInheritanceData");
	}
}

void UpdateInheritanceDef(DType& sym, const WTString& newDef)
{
	DbTypeId dbId = VADatabase::DBIdFromDbFlags(sym.DbFlags());
	SqliteDbPtr db;
	_ASSERTE(sym.MaskedType() == vaInheritsFrom);
	db = OpenInheritanceDbForFileId(sym.FileId(), dbId, vaInheritsFrom, false);
	if (!db)
	{
		_ASSERTE(!"failed to locate inheritance db for update");
		return;
	}

	// update the record
	WTString cmdTxt;
	cmdTxt.WTFormat("update inheritance set Def = '%s' where ScopeId = 0x%x and SymId = 0x%x and Sym = '%s' and Def = "
	                "'%s' and Type = %d and Flags = 0x%x and Attrs = 0x%x and FileId = 0x%x and Line = %d;",
	                newDef.c_str(), sym.ScopeHash(), sym.SymHash(), sym.SymScope().c_str(), sym.Def().c_str(),
	                sym.type(), sym.DbFlags(), sym.Attributes(), sym.FileId(), sym.Line());

	AutoLockCsPtr l(db->GetTransactionLock());
	db->execDML(cmdTxt.c_str());
}

//
// Read/Query support
//
void SelectPendingRecords(SqliteDb* db, std::set<uint>& records, CCriticalSection* listCs)
{
	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery("select * from PendingResolve;");
	_ASSERTE(q.numFields() == 1 || (q.numFields() == 0 && q.eof()));

	for (; !q.eof(); q.nextRow())
	{
		uint fileId = q.getUIntField(0); // uint

		AutoLockCsPtr l2(listCs);
		records.insert(fileId);
	}
}

void SelectInheritanceRecords(SqliteDb* db, uint scopeId, uint symId, int type, DTypeList& records,
                              CCriticalSection* listCs)
{
	WTString qryTxt, sym, def;
	qryTxt.WTFormat("select * from inheritance where ScopeId = 0x%x and SymId = 0x%x;", scopeId, symId);

	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery(qryTxt.c_str());
	_ASSERTE(q.numFields() == 9 || (q.numFields() == 0 && q.eof()));

	for (; !q.eof(); q.nextRow())
	{
		_ASSERTE(q.getUIntField(0) == scopeId); // uint
		_ASSERTE(q.getUIntField(1) == symId);   // uint
		sym = q.getStringField(2);              // varchar
		def = q.getStringField(3);              // varchar
		uint dbFlags = q.getUIntField(4);       // uint
		_ASSERTE(type == q.getIntField(5));     // int
		uint attrs = q.getUIntField(6);         // uint
		int line = q.getIntField(7);            // int
		uint fileId = q.getUIntField(8);        // uint

		{
			AutoLockCsPtr l2(listCs);
			records.push_back({scopeId, symId, 0, sym, def, (uint)type, attrs, dbFlags, line, fileId});
		}
	}
}

void SelectInheritanceRecords(SqliteDb* db, uint fileId, int type, DTypeList& records, CCriticalSection* listCs)
{
	WTString qryTxt, sym, def;
	qryTxt.WTFormat("select * from inheritance where FileId = 0x%x;", fileId);

	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery(qryTxt.c_str());
	_ASSERTE(q.numFields() == 9 || (q.numFields() == 0 && q.eof()));

	for (; !q.eof(); q.nextRow())
	{
		uint scopeId = q.getUIntField(0);
		uint symId = q.getUIntField(1);
		sym = q.getStringField(2);          // varchar
		def = q.getStringField(3);          // varchar
		uint dbFlags = q.getUIntField(4);   // uint
		_ASSERTE(type == q.getIntField(5)); // int
		uint attrs = q.getUIntField(6);     // uint
		int line = q.getIntField(7);        // int

		{
			AutoLockCsPtr l2(listCs);
			records.push_back({scopeId, symId, 0, sym, def, (uint)type, attrs, dbFlags, line, fileId});
		}
	}
}

void QueryInheritanceDb(DbTypeId dbType, uint fileId, uint symType, DTypeList& records)
{
	try
	{
		CCriticalSection listCs;
		SqliteDbPtr db = OpenInheritanceDbForFileId(fileId, dbType, (int)symType, false);
		if (db)
			SelectInheritanceRecords(db.get(), fileId, (int)symType, records, &listCs);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("QueryInh: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in QueryInh");
	}
}

void QueryPendingInheritanceDb(DbTypeId dbType, std::set<uint>& records)
{
	try
	{
		CCriticalSection listCs;
		auto spr = [&listCs, dbType, &records](int idx) {
			SqliteDbPtr db = OpenInheritanceDbBySplit(idx, dbType, vaInheritsFrom);
			if (db)
				SelectPendingRecords(db.get(), records, &listCs);
		};

		if (Psettings->mUsePpl)
			concurrency::parallel_for(0, kInhDbSplits, spr);
		else
			::serial_for<int>(0, kInhDbSplits, spr);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("QueryInh: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in QueryInh");
	}
}

void QueueResolverForPendingItems(DbTypeId dbId, uint slnHash)
{
	std::set<uint> records;
	QueryPendingInheritanceDb(dbId, records);
	for (auto fileId : records)
	{
		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new InheritanceTypeResolver(fileId, dbId, slnHash),
			                                   ParserThread::priBelowNormal);
	}
}

void QueryInheritanceDb(DbTypeId dbType, uint scopeId, uint symId, uint symType, DTypeList& records)
{
	try
	{
		CCriticalSection listCs;
		auto sir = [&listCs, dbType, symId, scopeId, symType, &records](int idx) {
			SqliteDbPtr db = OpenInheritanceDbBySplit(idx, dbType, (int)symType);
			if (db)
				SelectInheritanceRecords(db.get(), scopeId, symId, (int)symType, records, &listCs);
		};

		if (Psettings->mUsePpl)
			concurrency::parallel_for(0, kInhDbSplits, sir);
		else
			::serial_for<int>(0, kInhDbSplits, sir);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("QueryInh: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in QueryInh");
	}
}

void QueryInheritanceDbs(DTypeDbScope dbScp, uint scopeId, uint symId, uint symType, DTypeList& records)
{
	_ASSERTE(records.empty());
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		QueryInheritanceDb(DbTypeId_Solution, scopeId, symId, symType, records);

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem ||
	    ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystemIfNoSln && !records.size()))
	{
		QueryInheritanceDb(DbTypeId_SolutionSystemCpp, scopeId, symId, symType, records);
		QueryInheritanceDb(DbTypeId_Cpp, scopeId, symId, symType, records);
		QueryInheritanceDb(DbTypeId_Net, scopeId, symId, symType, records);
	}

	QueryInheritanceDb(DbTypeId_ExternalOther, scopeId, symId, symType, records);
}

void GetInheritanceRecords(int inhType, const WTString& scope, DTypeList& records)
{
	WTString lookupStr;
	switch (inhType)
	{
	case vaInheritedBy:
		lookupStr = kInheritedByStr + StrGetSym(scope);
		break;
	case vaInheritsFrom:
		lookupStr = kInheritsFromStr + StrGetSym(scope);
		break;
	default:
		_ASSERTE(!"invalid sym type passed to GetInheritanceRecords");
		return;
	}

	const uint scopeId = DType::HashSym(scope);
	const uint symId = DType::HashSym(lookupStr);
	QueryInheritanceDbs(DTypeDbScope::dbSlnAndSys, scopeId, symId, (uint)inhType, records);

	records.FilterNonActiveSystemDefs();

	if (records.size() > 1)
	{
		auto fn = [](DType& p1, DType& p2) -> bool {
			// sort by sym, then by scope
			WTString p1Def(p1.Def());
			WTString p2Def(p2.Def());
			WTString sym1 = StrGetSym(p1Def);
			WTString sym2 = StrGetSym(p2Def);
			if (sym1 == sym2)
			{
				WTString scope1 = StrGetSymScope(p1Def);
				WTString scope2 = StrGetSymScope(p2Def);
				return scope1.Compare(scope2) < 0;
			}
			return sym1.Compare(sym2) < 0;
		};
		records.sort(fn);
	}

	if (records.size() > 1)
	{
		auto unq = [](DType& p1, DType& p2) -> bool { return p1.Def() == p2.Def(); };
		records.unique(unq);
	}
}

} // namespace InheritanceDb
