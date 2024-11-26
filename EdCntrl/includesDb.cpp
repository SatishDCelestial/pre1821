#include "StdAfxEd.h"
#include "includesDb.h"
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
#include "FileId.h"
#include "serial_for.h"
#include "Settings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace IncludesDb
{

const int kIncDbSplits = 10;

enum class IncludeDbType
{
	Includes,
	IncludeBys,
	Count
};

CCriticalSection gIncludeDbsArrayLock;
SqliteDbPtr gIncludeDbs[(int)IncludeDbType::Count][DbTypeId_Count][kIncDbSplits];
bool gIncludeDbIndexSet[(int)IncludeDbType::Count][DbTypeId_Count][kIncDbSplits];

int IncludeDbTypeFromSymType(int symType)
{
	switch (symType)
	{
	case vaInclude:
		return (int)IncludeDbType::Includes;
	case vaIncludeBy:
		return (int)IncludeDbType::IncludeBys;
	default:
		_ASSERTE(!"invalid symType -- no conversion to IncludeDbType");
		return (int)IncludeDbType::Count;
	}
}

void SolutionLoaded()
{
	Close();
}

void Close()
{
	LogElapsedTime tt("IncDb close", 1000);
	AutoLockCs l(gIncludeDbsArrayLock);

#ifdef _DEBUG
	int dbCur = 0, dbHigh = 0;
	sqlite3_status(SQLITE_STATUS_MEMORY_USED, &dbCur, &dbHigh, 0);
	// watch window:
	// (dbCur / 1024) / 1024,d
	// (dbHigh / 1024) / 1024,d
#endif

	for (auto& type : gIncludeDbs)
		for (auto& typeId : type)
			for (auto& split : typeId)
				split = nullptr;

	for (auto& type : gIncludeDbIndexSet)
		for (auto& typeId : type)
			for (auto& split : typeId)
				split = false;
}

//
// Open/Load support
//

CStringW GetIncludesDbFile(DbTypeId dbId, int symType, int splitNumber)
{
	CStringW dbFileW;
	switch (dbId)
	{
	case DbTypeId_Cpp:
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
		_ASSERTE(!"unhandled DbTypeId includes db");
		return dbFileW;
	}

	if (vaInclude == symType)
		dbFileW += L"inc-A-";
	else if (vaIncludeBy == symType)
		dbFileW += L"inc-B-";
	else
	{
		_ASSERTE(!"bad symType in GetIncludesDbFile");
		dbFileW.Empty();
		return dbFileW;
	}

	CStringW tmp;
	CString__FormatW(tmp, L"%02d.sdb", splitNumber);
	dbFileW += tmp;
	return dbFileW;
}

SqliteDbPtr OpenIncludesDb(const CStringW& dbFileW, int symType, DbTypeId dbId, int splitNumber, bool create)
{
	SqliteDbPtr db;
	if (DbTypeId_LocalsParseAll == dbId)
		dbId = DbTypeId_Solution;

	const int symTypeDb = IncludeDbTypeFromSymType(symType);

	{
		AutoLockCs l(gIncludeDbsArrayLock);
		db = gIncludeDbs[symTypeDb][dbId][splitNumber];
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

				cmd += "create table if not exists includes ( "
				       "HashId unsigned int, "
				       "Sym varchar(15), "
				       "Def varchar(15), "
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
			}

			gIncludeDbs[symTypeDb][dbId][splitNumber] = db;
		}
	}

	if (!create)
	{
		// !create means read-access, so make sure indexes are present
		AutoLockCsPtr l(db->GetTransactionLock());
		if (vaInclude == symType)
		{
			if (!gIncludeDbIndexSet[symTypeDb][dbId][splitNumber])
			{
				db->execDML("create index if not exists IxFileId on includes (FileId);");
				gIncludeDbIndexSet[symTypeDb][dbId][splitNumber] = true;
			}
		}
		else if (vaIncludeBy == symType)
		{
			if (!gIncludeDbIndexSet[symTypeDb][dbId][splitNumber])
			{
				db->execDML("create index if not exists IxHashId on includes (HashId);");
				gIncludeDbIndexSet[symTypeDb][dbId][splitNumber] = true;
			}
		}
	}

	return db;
}

SqliteDbPtr OpenIncludesDbForFileId(uint fileId, DbTypeId dbId, int symType, bool create)
{
	const int kSplitNumber = int(fileId % kIncDbSplits);
	const CStringW dbFileW(GetIncludesDbFile(dbId, symType, kSplitNumber));
	if (dbFileW.IsEmpty())
		return SqliteDbPtr();

	return OpenIncludesDb(dbFileW, symType, dbId, kSplitNumber, create);
}

SqliteDbPtr OpenIncludesDbBySplit(int splitNumber, DbTypeId dbId, int symType)
{
	const CStringW dbFileW(GetIncludesDbFile(dbId, symType, splitNumber));
	if (dbFileW.IsEmpty())
		return SqliteDbPtr();

	return OpenIncludesDb(dbFileW, symType, dbId, splitNumber, false);
}

//
// Purge support
//

void PurgeIncludes(UINT fileId, DbTypeId dbType, int symType)
{
	try
	{
		WTString cmd;
		cmd.WTFormat("delete from includes where FileId = 0x%x;", fileId);

		SqliteDbPtr db = OpenIncludesDbForFileId(fileId, dbType, symType, false);
		if (!db)
			return;

		AutoLockCsPtr l(db->GetTransactionLock());
		db->execDML(cmd.c_str());
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("PurgeInc:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in PurgeIncludes");
	}
}

// [case: 164163]
// PurgeIncludes version which takes map of field IDs split by kIncDbSplits delete is done here
// by combining all entries that need to be deleted in one SQL delete command
void PurgeIncludes(const std::map<int, std::vector<UINT>>& splitFileIds, DbTypeId dbType, int symType)
{
	try
	{
		// guard against exceeding max number of element in SQLlite IN list
		// 500 is very conservative value, but is sure to work correctly with it
		// (didn't find exact limit but some article mentioned 1000)
		const size_t maxNumberOfFieldsToDeleteAtOnce = 500;

		for (const auto& fileIds : splitFileIds)
		{
			
			std::vector<WTString> fileIdsForDeleteList;
			for (size_t cnt = 0; cnt < fileIds.second.size(); cnt++)
			{
				// on each start of max number add a new string into vector (cnt % maxNumberOfFieldsToDeleteAtOnce)
				if (cnt % maxNumberOfFieldsToDeleteAtOnce == 0)
					fileIdsForDeleteList.emplace_back();

				WTString id;
				id.WTFormat("0x%x,", fileIds.second[cnt]);
				// add fileId to delete at correct place in the vector
				fileIdsForDeleteList.back() += id;
			}
			
			for (auto fileIdsForDelete : fileIdsForDeleteList)
			{
				// handle the scenario when number of elements to delete at once is bigger than maxNumberOfFieldsToDeleteAtOnce
				// this guards against potential SQLlite limit for max elements in IN list
				fileIdsForDelete.TrimRightChar(',');

				WTString cmd = "delete from includes where FileId in (" + fileIdsForDelete + ");";

				{
					SqliteDbPtr db = OpenIncludesDbBySplit(fileIds.first, dbType, symType);
					if (!db)
						break;

					AutoLockCsPtr l(db->GetTransactionLock());
					db->execDML(cmd.c_str());
				}
			}
		}
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("PurgeInc:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in PurgeIncludes");
	}
}

void PurgeIncludes(const std::set<UINT>& fileIds, DbTypeId dbType, int symType)
{
	try
	{
		std::map<int, std::vector<UINT>> splitFileIds;
		
		for (auto fileId : fileIds)
		{
			// [case: 164163] split fields by kIncDbSplits (which indicate the file) and
			// compose a map of vectors that will be used in new PurgeIncludes function
			int ftype = GetFileType(gFileIdManager->GetFile(fileId));
			if (!IsCFile(ftype))
				continue;

			const auto kSplitNumber = int(fileId % kIncDbSplits);
			splitFileIds[kSplitNumber].push_back(fileId);
		}

		// [case: 164163] instead of original PurgeIncludes which was inside the loop
		PurgeIncludes(splitFileIds, dbType, symType);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("PurgeInc:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in PurgeIncludes");
	}
}

void PurgeIncludes(const std::set<UINT>& fileIds, DTypeDbScope dbScp)
{
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));
	_ASSERTE(((DWORD)dbScp & (DWORD)DTypeDbScope::dbSlnAndSys));
	_ASSERTE(dbScp != DTypeDbScope::dbSystemIfNoSln);
	LogElapsedTime tt("PurgeInc list", 2000);

	struct PurgeData
	{
		DbTypeId mDbId;
		int mType;
	};
	std::vector<PurgeData> pd;

	// purge sln private sys in any case
	pd.push_back({DbTypeId_SolutionSystemCpp, vaInclude});
	pd.push_back({DbTypeId_SolutionSystemCpp, vaIncludeBy});

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
	{
		pd.push_back({DbTypeId_Solution, vaInclude});
		pd.push_back({DbTypeId_Solution, vaIncludeBy});
	}

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
	{
		pd.push_back({DbTypeId_Cpp, vaInclude});
		pd.push_back({DbTypeId_Cpp, vaIncludeBy});
	}

	// This call had me a bit concerned.  But leave it for parity with
	// FileDic::RemoveAllDefsFrom_SinglePass which does remove file from
	// all vadbs -- regardless of on which FileDic instance the call was
	// exec'd.
	pd.push_back({DbTypeId_ExternalOther, vaInclude});
	pd.push_back({DbTypeId_ExternalOther, vaIncludeBy});

	auto fn = [&fileIds](const PurgeData& dat) { PurgeIncludes(fileIds, dat.mDbId, dat.mType); };

	if (Psettings->mUsePpl)
		concurrency::parallel_for_each(pd.cbegin(), pd.cend(), fn);
	else
		std::for_each(pd.cbegin(), pd.cend(), fn);
}

void PurgeIncludes(const CStringW& file, DTypeDbScope dbScp)
{
	int ftype = GetFileType(file);
	if (!(IsCFile(ftype)))
		return;

	const uint fileId = gFileIdManager->GetFileId(file);
	std::set<UINT> fids;
	fids.insert(fileId);
	PurgeIncludes(fids, dbScp);
}

//
// Write/Store support
//

void StoreIncludeData(DbOutData& includesRecords, DbTypeId dbId, int symType)
{
	if (!includesRecords.size())
		return;

	_ASSERTE(symType == (int)(includesRecords.begin()->mType & TYPEMASK));

	try
	{
		uint lastFileId = 0;
		WTString cmd;
		SqliteDbPtr db;
		AutoLockCsPtr l;
		// in general, all the records will be from the same fileId
		const bool doAsTransaction = includesRecords.size() > 3;

		for (const auto& it : includesRecords)
		{
			const uint curFileId = it.mFileId;
			if (curFileId != lastFileId || !db)
			{
				if (db)
				{
					if (doAsTransaction)
						db->execDML("end transaction;");
					l.Unlock();
				}

				db = OpenIncludesDbForFileId(curFileId, dbId, symType, true);
				if (!db)
				{
					_ASSERTE(!"StoreIncludeData fail to open db");
					break;
				}

				l.Lock(db->GetTransactionLock());
				if (doAsTransaction)
					db->execDML("begin transaction;");
			}

			uint dbFlags = (it.mType & VA_DB_FLAGS_MASK);
			dbFlags |= VADatabase::DbFlagsFromDbId(dbId);
			_ASSERTE(!(dbFlags & VA_DB_BackedByDatafile));
			// dbFlags &= ~VA_DB_BackedByDatafile;
			_ASSERTE(!(it.mAttrs & V_SYSLIB) || (dbFlags & VA_DB_Cpp));
			cmd.WTFormat("insert into includes values (0x%x, '%s', '%s', 0x%x, %d, 0x%x, %d, 0x%lx);",
			             WTHashKey(it.mSym), it.mSym.c_str(), it.mDef.c_str(), dbFlags, (int)(it.mType & TYPEMASK),
			             it.mAttrs, it.mLine, it.mFileId);
			_ASSERTE(db);
			db->execDML(cmd.c_str());

			lastFileId = curFileId;
		}

		if (doAsTransaction && db)
			db->execDML("end transaction;");
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("SID-Inc:") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in StoreIncludeData");
	}
}

//
// Read/Query support
//
void SelectIncludeRecords(SqliteDb* db, uint lookupId, uint fileIdParam, int type, DTypeList& records,
                          CCriticalSection* listCs = nullptr)
{
	WTString qryTxt, sym, def;
	if (vaInclude == type)
		qryTxt.WTFormat("select * from includes where FileId = 0x%x;", fileIdParam);
	else if (vaIncludeBy == type)
		qryTxt.WTFormat("select * from includes where HashId = 0x%x;", lookupId);
	else
	{
		_ASSERTE(!"unsupported type in SelectIncludeRecords");
		return;
	}

	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery(qryTxt.c_str());
	_ASSERTE(q.numFields() == 8 || (q.numFields() == 0 && q.eof()));

	for (; !q.eof(); q.nextRow())
	{
		// uint hashId = q.getUIntField(0);			// uint
		sym = q.getStringField(1);          // varchar
		def = q.getStringField(2);          // varchar
		uint dbFlags = q.getUIntField(3);   // uint
		_ASSERTE(type == q.getIntField(4)); // int
		uint attrs = q.getUIntField(5);     // uint
		int line = q.getIntField(6);        // int
		uint fileId = q.getUIntField(7);    // uint

		_ASSERTE(lookupId == WTHashKey(sym));
		AutoLockCsPtr l2(listCs);
		records.push_back({lookupId, fileIdParam, 0, sym, def, (uint)type, attrs, dbFlags, line, fileId});
	}
}

void SelectIncludedFileIds(SqliteDb* db, std::function<void(uint fileId)> fnc)
{
	WTString qryTxt, def;

	// [case: 141331]
	// select Def to get included file, don't use FileId because that is the
	// file that has an include; it is not the id of the included file.

	qryTxt.WTFormat("select distinct Def from includes where Type = 0x%x;", vaInclude);

	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery(qryTxt.c_str());
	_ASSERTE(q.numFields() == 1 || (q.numFields() == 0 && q.eof()));

	for (; !q.eof(); q.nextRow())
	{
		def = q.getStringField(0); // varchar
		UINT fileId = gFileIdManager->GetIdFromFileId(def);
		fnc(fileId);
	}
}

bool HasIncludeRecord(SqliteDb* db, uint lookupId, uint fileIdParam, int type)
{
	WTString qryTxt, sym, def;
	if (vaInclude == type)
		qryTxt.WTFormat("select Type from includes where FileId = 0x%x limit 1;", fileIdParam);
	else if (vaIncludeBy == type)
		qryTxt.WTFormat("select Type from includes where HashId = 0x%x limit 1;", lookupId);
	else
	{
		_ASSERTE(!"unsupported type in HasIncludeRecord");
		return false;
	}

	AutoLockCsPtr l(db->GetTransactionLock());
	CppSQLite3Query q = db->execQuery(qryTxt.c_str());
	return q.eof() ? false : true;
}

void QueryIncludeDb(DbTypeId dbType, uint lookupId, uint fileIdParam, uint symType, DTypeList& records)
{
	try
	{
		if (vaInclude == symType)
		{
			SqliteDbPtr db = OpenIncludesDbForFileId(fileIdParam, dbType, (int)symType, false);
			if (db)
				SelectIncludeRecords(db.get(), lookupId, fileIdParam, (int)symType, records);
		}
		else if (vaIncludeBy == symType)
		{
			CCriticalSection listCs;

			auto setter = [&listCs, dbType, lookupId, fileIdParam, &records](int idx) {
				SqliteDbPtr db = OpenIncludesDbBySplit(idx, dbType, vaIncludeBy);
				if (db)
					SelectIncludeRecords(db.get(), lookupId, fileIdParam, vaIncludeBy, records, &listCs);
			};

			if (Psettings->mUsePpl)
				concurrency::parallel_for(0, kIncDbSplits, setter);
			else
				::serial_for<int>(0, kIncDbSplits, setter);
		}
		else
		{
			_ASSERTE(!"unhandled type in QueryIncludeDb");
		}
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("QueryInc: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in QueryInc");
	}
}

void QueryIncludeDb(DbTypeId dbType, std::function<void(uint fileId)> fnc)
{
	try
	{
		auto setter = [dbType, &fnc](int idx) {
			SqliteDbPtr db = OpenIncludesDbBySplit(idx, dbType, vaInclude);
			if (db)
				SelectIncludedFileIds(db.get(), fnc);
		};

		if (Psettings->mUsePpl)
			concurrency::parallel_for(0, kIncDbSplits, setter);
		else
			::serial_for<int>(0, kIncDbSplits, setter);
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("QueryInc: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in QueryInc");
	}
}

bool HasIncludeRecord(DbTypeId dbType, uint lookupId, uint fileIdParam, uint symType)
{
	try
	{
		if (vaInclude == symType)
		{
			SqliteDbPtr db = OpenIncludesDbForFileId(fileIdParam, dbType, (int)symType, false);
			if (db)
				return HasIncludeRecord(db.get(), lookupId, fileIdParam, (int)symType);
		}
		else if (vaIncludeBy == symType)
		{
			bool res = false;
			auto chkr = [&res, dbType, lookupId, fileIdParam](int idx) {
				SqliteDbPtr db = OpenIncludesDbBySplit(idx, dbType, vaIncludeBy);
				if (db && HasIncludeRecord(db.get(), lookupId, fileIdParam, vaIncludeBy))
					res = true;
			};

			if (Psettings->mUsePpl)
				concurrency::parallel_for(0, kIncDbSplits, chkr);
			else
				::serial_for<int>(0, kIncDbSplits, chkr);

			if (res)
				return true;
		}
		else
		{
			_ASSERTE(!"unhandled type in HasIncludeRecord");
		}
	}
	catch (CppSQLite3Exception& e)
	{
		VALOGEXCEPTION((WTString("HasIncRec: ") + e.errorMessage()).c_str());
		_ASSERTE(!"Exception in HasIncludeRec");
	}

	return false;
}

void QueryIncludeDbs(DTypeDbScope dbScp, uint lookupId, uint fileIdParam, uint symType, DTypeList& records)
{
	_ASSERTE(records.empty());
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		QueryIncludeDb(DbTypeId_Solution, lookupId, fileIdParam, symType, records);

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem ||
	    ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystemIfNoSln && !records.size()))
	{
		QueryIncludeDb(DbTypeId_SolutionSystemCpp, lookupId, fileIdParam, symType, records);
		QueryIncludeDb(DbTypeId_Cpp, lookupId, fileIdParam, symType, records);
	}

	QueryIncludeDb(DbTypeId_ExternalOther, lookupId, fileIdParam, symType, records);
}

void QueryIncludeDbs(DTypeDbScope dbScp, std::function<void(uint fileId)> fnc)
{
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));
	_ASSERTE(((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystemIfNoSln) != (DWORD)DTypeDbScope::dbSystemIfNoSln);

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		QueryIncludeDb(DbTypeId_Solution, fnc);

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
	{
		QueryIncludeDb(DbTypeId_SolutionSystemCpp, fnc);
		QueryIncludeDb(DbTypeId_Cpp, fnc);
	}

	QueryIncludeDb(DbTypeId_ExternalOther, fnc);
}

bool HasIncludeRecords(DTypeDbScope dbScp, uint lookupId, uint fileIdParam, uint symType)
{
	_ASSERTE(!((DWORD)dbScp & (DWORD)DTypeDbScope::dbLocal));

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		if (HasIncludeRecord(DbTypeId_Solution, lookupId, fileIdParam, symType))
			return true;

	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem || ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystemIfNoSln))
	{
		if (HasIncludeRecord(DbTypeId_SolutionSystemCpp, lookupId, fileIdParam, symType))
			return true;
		if (HasIncludeRecord(DbTypeId_Cpp, lookupId, fileIdParam, symType))
			return true;
	}

	if (HasIncludeRecord(DbTypeId_ExternalOther, lookupId, fileIdParam, symType))
		return true;

	return false;
}

inline bool DTypeEQ(const DType& c1, const DType& c2)
{
	return c1.FileId() == c2.FileId() && c1.Line() == c2.Line();
}

inline bool DTypeLT(const DType& c1, const DType& c2)
{
	uint id1 = c1.FileId();
	uint id2 = c2.FileId();
	if (id1 < id2)
		return true;

	if (id1 == id2)
	{
		if (c1.Line() < c2.Line())
			return true;
	}

	return false;
}

void GetIncludedBys(CStringW file, DTypeDbScope dbScp, DTypeList& fileIds)
{
	_ASSERTE(fileIds.empty());
	const WTString key(gFileIdManager->GetIncludedByStr(file));
	const uint hashId = WTHashKey(key);
	uint fileId = gFileIdManager->GetFileId(file);
	QueryIncludeDbs(dbScp, hashId, fileId, vaIncludeBy, fileIds);
	fileIds.sort(DTypeLT);
	fileIds.unique(DTypeEQ);
}

void GetIncludes(UINT fileId, DTypeDbScope dbScp, DTypeList& fileIds)
{
	_ASSERTE(fileIds.empty());
	const WTString key(FileIdManager::GetIncludeSymStr(fileId));
	_ASSERTE(key[0] != DB_SEP_CHR);
	const uint hashId = WTHashKey(key);
	QueryIncludeDbs(dbScp, hashId, fileId, vaInclude, fileIds);
	fileIds.sort(DTypeLT);
	fileIds.unique(DTypeEQ);
}

void GetIncludes(CStringW file, DTypeDbScope dbScp, DTypeList& fileIds)
{
	UINT fileId = gFileIdManager->GetFileId(file);
	IncludesDb::GetIncludes(fileId, dbScp, fileIds);
}

void IterateAllIncluded(DTypeDbScope dbScp, std::function<void(uint fileId)> fnc)
{
	QueryIncludeDbs(dbScp, fnc);
}

bool HasIncludedBys(const CStringW& file, DTypeDbScope dbScope)
{
	const WTString key(gFileIdManager->GetIncludedByStr(file));
	const uint hashId = WTHashKey(key);
	uint fileId = gFileIdManager->GetFileId(file);
	return HasIncludeRecords(dbScope, hashId, fileId, vaIncludeBy);
}

bool HasIncludes(const CStringW& file, DTypeDbScope dbScope)
{
	UINT fileId = gFileIdManager->GetFileId(file);
	return IncludesDb::HasIncludes(fileId, dbScope);
}

bool HasIncludes(UINT fileId, DTypeDbScope dbScp)
{
	const WTString key(FileIdManager::GetIncludeSymStr(fileId));
	_ASSERTE(key[0] != DB_SEP_CHR);
	const uint hashId = WTHashKey(key);
	return HasIncludeRecords(dbScp, hashId, fileId, vaInclude);
}

} // namespace IncludesDb
