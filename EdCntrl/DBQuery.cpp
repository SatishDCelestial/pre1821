
#include "stdafxed.h"
#include "edcnt.h"
#include "mparse.h"
#include "foo.h"
#include "DBQuery.h"
#include "DBLock.h"
#include "Lock.h"
#include "VAHashTable.h"
#include "FDictionary.h"
#include "PROJECT.H"
#include "serial_for.h"
#include "Settings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// Helper class to query db[s] HashTable for a DTypeList of matching symbols
class DB_Query_Itorator
{
  public:
	virtual ~DB_Query_Itorator() = default;
	// 	DTypeListIterator is the only public methods
  protected:
	DBQuery* m_DBQry;

	int IterateDBs(DBQuery* mp, int dbs /*= DB_ALL*/)
	{
		m_DBQry = mp;
		DB_READ_LOCK;
		if (dbs & MultiParse::DB_VA && m_DBQry->ShouldContinue())
		{
			CLockMsgCs lck(*GetDFileMP(mp->GetMP()->FileType())->LDictionary()->GetDBLock());
			IterateDB(GetDFileMP(gTypingDevLang)->LDictionary()->GetHashTable());
		}
		if (dbs & MultiParse::DB_LOCAL && m_DBQry->ShouldContinue())
		{
			CLockMsgCs lck(*GetDFileMP(mp->GetMP()->FileType())->LDictionary()->GetDBLock());
			IterateDB(mp->GetMP()->LDictionary()->GetHashTable());
		}
		if (dbs & MultiParse::DB_GLOBAL && m_DBQry->ShouldContinue())
			IterateDB(g_pGlobDic->GetHashTable());
		if (dbs & MultiParse::DB_SYSTEM && m_DBQry->ShouldContinue())
			IterateDB(mp->GetMP()->SDictionary()->GetHashTable());
		return m_DBQry->Count();
	}

	virtual void IterateDB(VAHashTable* table)
	{
		// Iterate each row in db.
		uint sz = table->TableSize();
		bool stopLoop = false;
		auto iterateFn = [&](uint row) {
			if (StopIt || stopLoop)
				return;

			if (!m_DBQry->ShouldContinue())
				return;

			if (!IterateRow(table, row))
				stopLoop = true;
		};

#pragma warning(push)
#pragma warning(disable : 4127)
		if (true || Psettings->mUsePpl)
			Concurrency::parallel_for((uint)0, sz, iterateFn);
		else
			::serial_for<uint>((uint)0, sz, iterateFn);
#pragma warning(pop)
	}

	virtual BOOL IterateRow(VAHashTable* table, uint row)
	{
		// Iterate each DType in row.
		int idx = 0;
		for (DefObj* cur = table->GetRowHead(row); NULL != cur; cur = cur->next)
		{
			if (++idx == 512)
			{
				if (!m_DBQry->ShouldContinue())
					return FALSE;
				idx = 0;
			}

			if (!OnDType(cur))
				return FALSE;
		}
		return TRUE;
	}

	uint HashToRow(VAHashTable* table, uint hv)
	{
		return (hv & HASHCASEMASK) % table->TableSize();
	}

	virtual BOOL OnDType(DType* cur)
	{
		return TRUE;
	}

	void AddDType(DType* cur)
	{
		m_DBQry->Add(cur);
	}
};

// Query db[s] for a list of all symbols in scope
class DB_FindAllSymbolsInScopeList : public DB_Query_Itorator
{
	ScopeHashAry m_curscope;
	virtual BOOL OnDType(DType* cur)
	{
		// Note: excludes globals
		if (cur->ScopeHash() && m_curscope.Contains(cur->ScopeHash()))
			AddDType(cur);
		return TRUE;
	}

  public:
	DB_FindAllSymbolsInScopeList(DBQuery* mp, LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL)
	    : m_curscope(scope, bcl, NULLSTR)
	{
		IterateDBs(mp, dbs);
	}
	// 	DTypeListIterator methods are the only public methods
};

class DB_FindVariablesInScopeList : public DB_Query_Itorator
{
	ScopeHashAry m_curscope;
	virtual BOOL OnDType(DType* cur)
	{
		// Note: excludes globals
		if (cur->ScopeHash() && cur->MaskedType() == VAR && m_curscope.Contains(cur->ScopeHash()))
			AddDType(cur);
		return TRUE;
	}

  public:
	DB_FindVariablesInScopeList(DBQuery* mp, LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL)
	    : m_curscope(scope, bcl, NULLSTR)
	{
		IterateDBs(mp, dbs);
	}
	// 	DTypeListIterator methods are the only public methods
};

// Query db[s] for a list of all symbols in scope and file
class DB_FindAllSymbolsInScopeAndFileList : public DB_Query_Itorator
{
	ScopeHashAry m_curscope;
	virtual BOOL OnDType(DType* cur)
	{
		if (cur->FileId() == FileID && m_curscope.Contains(cur->ScopeHash()))
			AddDType(cur);
		return TRUE;
	}
	uint FileID;

  public:
	DB_FindAllSymbolsInScopeAndFileList(DBQuery* mp, LPCSTR scope, LPCSTR bcl, uint fileID,
	                                    int dbs = MultiParse::DB_ALL)
	    : m_curscope(scope, bcl, NULLSTR), FileID(fileID)
	{
		IterateDBs(mp, dbs);
	}
	// 	DTypeListIterator methods are the only public methods
};

// Query db[s] for a list of all instances of "sym", any scope
// class DB_FindAllSymsList : public DB_Query_Itorator
// {
// protected:
// 	WTString m_sym;
// 	uint m_symHash;
// public:
// 	DB_FindAllSymsList(DBQuery *mp, LPCSTR sym, int dbs = MultiParse::DB_ALL )
// 		: m_sym(sym),
// 		m_symHash(WTHashKey(sym))
// 	{
// 		IterateDBs(mp, dbs);
// 	}
// 	virtual void IterateDB(VAHashTable *table)
// 	{
// 		// Just query row that contain sym
// 		IterateRow(table, HashToRow(table, m_symHash));
// 	}
// 	virtual BOOL OnDType(DType *cur)
// 	{
// 		if(cur->SymHash() == m_symHash)
// 			AddDType(cur);
// 		return TRUE;
// 	}
// };

// Query db[s] for a list of all exact instances of symscope
class DB_FindExactList : public DB_Query_Itorator
{
	uint m_symHash;
	uint m_scopeHash;

  public:
	DB_FindExactList(DBQuery* mp, LPCSTR sym, uint scopeHash, int dbs = MultiParse::DB_ALL)
	    : m_symHash(WTHashKey(sym)), m_scopeHash(scopeHash)
	{
		IterateDBs(mp, dbs);
	}
	DB_FindExactList(DBQuery* mp, uint symHash, uint scopeHash, int dbs = MultiParse::DB_ALL)
	    : m_symHash(symHash), m_scopeHash(scopeHash)
	{
		IterateDBs(mp, dbs);
	}
	virtual void IterateDB(VAHashTable* table)
	{
		// Just query row that contain sym
		IterateRow(table, HashToRow(table, m_symHash));
	}
	virtual BOOL OnDType(DType* cur)
	{
		if (HashEqualAC(cur->SymHash(), m_symHash) && HashEqualAC(cur->ScopeHash(), m_scopeHash))
			AddDType(cur);
		return TRUE;
	}
};

// Query db[s] for a list of all symbols of IsType()
// class DB_FindTypeList : public DB_Query_Itorator
// {
// public:
// 	DB_FindTypeList(DBQuery *mp, int dbs = MultiParse::DB_ALL )
// 	{
// 		IterateDBs(mp, dbs);
// 	}
// 	virtual BOOL OnDType(DType *cur)
// 	{
// 		if(cur->IsType())
// 			AddDType(cur);
// 		return TRUE;
// 	}
// };
//
// Query db[s] for a list of best matches for sym in scope
// class DB_FindSymbolInScopeList : public DB_FindAllSymsList
// {
// 	// Note: excludes globals
// 	ScopeHashAry m_curscope;
// 	int m_rank;
// public:
//
// 	DB_FindSymbolInScopeList(DBQuery *mp, LPCSTR sym, LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL )
// 		: m_curscope(scope, bcl, NULLSTR),
// 		m_rank(-9999),
// 		DB_FindAllSymsList(mp, sym, dbs)
// 	{
// 		IterateDBs(mp, dbs);
// 	}
// 	virtual BOOL OnDType(DType *cur)
// 	{
// 		int rank = m_curscope.Contains(cur->ScopeHash());
// 		if(rank >= m_rank)
// 		{
// 			if(rank > m_rank)
// 				m_DBQry->Clear();
// 			m_rank = rank;
// 			AddDType(cur);
// 		}
// 		return TRUE;
// 	}
// };

DType* DTypeListIterator::GetFirst()
{
	m_iterator = m_list.begin();
	if (m_iterator == m_list.end())
		return NULL;
	return &(*m_iterator++);
}

DType* DTypeListIterator::GetNext()
{
	if (m_iterator == m_list.end())
		return NULL;
	return &(*(m_iterator++));
}

int DBQuery::FindAllSymbolsInScopeList(LPCSTR scope, LPCSTR bcl, int dbs /*= MultiParse::DB_ALL */)
{
	if (!ShouldContinue())
		return 0;
	DB_FindAllSymbolsInScopeList qry(this, scope, bcl, dbs);
	return Count();
}

int DBQuery::FindAllVariablesInScopeList(LPCSTR scope, LPCSTR bcl, int dbs /*= MultiParse::DB_ALL*/)
{
	if (!ShouldContinue())
		return 0;
	DB_FindVariablesInScopeList qry(this, scope, bcl, dbs);
	return Count();
}

int DBQuery::FindAllSymbolsInScopeAndFileList(LPCSTR scope, LPCSTR bcl, uint fileID, int dbs /*= MultiParse::DB_ALL */)
{
	if (!ShouldContinue())
		return 0;
	DB_FindAllSymbolsInScopeAndFileList qry(this, scope, bcl, fileID, dbs);
	return Count();
}

int DBQuery::FindExactList(LPCSTR sym, uint scopeHash, int dbs /*= MultiParse::DB_ALL */)
{
	if (!ShouldContinue())
		return 0;
	DB_FindExactList qry(this, sym, scopeHash, dbs);
	return Count();
}

// int DBQuery::FindExactList( uint symHash, uint scopeHash, int dbs /*= MultiParse::DB_ALL */ )
// {
// 	if (!ShouldContinue())
// 		return 0;
// 	DB_FindExactList qry(this, symHash, scopeHash, dbs);
// 	return Count();
// }
//
// int DBQuery::FindSymbolInScopeList( LPCSTR sym, LPCSTR scope, LPCSTR bcl, int dbs /*= MultiParse::DB_ALL*/ )
// {
// 	if (!ShouldContinue())
// 		return 0;
// 	DB_FindSymbolInScopeList qry(this, sym, scope, bcl, dbs);
// 	return Count();
// }
//
//
// bool DTypeSortOccurance(const DType& lhs, const DType& rhs)
// {
// 	if(lhs.FileId() == rhs.FileId())
// 		return lhs.Line() > rhs.Line();
// 	return lhs.FileId() > rhs.FileId();
// }
//
// void DTypeListIterator::SortByOccurance()
// {
// 	AutoLockCs l(mLock);
// 	m_list.sort(DTypeSortOccurance);
// }
//
// int DBQuery::FindAllClasses(int dbs /*= MultiParse::DB_ALL */ )
// {
// 	if (!ShouldContinue())
// 		return 0;
// 	DB_FindTypeList qry(this, dbs);
// 	return Count();
// }
