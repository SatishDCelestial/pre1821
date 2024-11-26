#pragma once

#include "Lock.h"
#include "SpinCriticalSection.h"

class DTypeListIterator
{
  protected:
	CSpinCriticalSection mLock;
	DTypeList m_list;
	DTypeList::iterator m_iterator;
	bool mRun;

  public:
	DTypeListIterator() : mRun(true)
	{
	}
	bool ShouldContinue() const
	{
		return mRun;
	}
	void Stop()
	{
		mRun = false;
	}
	DType* GetFirst();
	DType* GetNext();
	void Add(DType* dt)
	{
		if (ShouldContinue())
		{
			AutoLockCs l(mLock);
			m_list.push_back(dt);
		}
	}
	int Count()
	{
		AutoLockCs l(mLock);
		return (int)m_list.size();
	}
	void Clear()
	{
		if (ShouldContinue())
		{
			AutoLockCs l(mLock);
			m_list.clear();
		}
	}
	//	void SortByOccurance();
};

class DBQuery : public DTypeListIterator
{
	MultiParsePtr m_mp;

  public:
	DBQuery(MultiParsePtr mp) : m_mp(mp)
	{
	}
	MultiParsePtr GetMP()
	{
		return m_mp;
	}
	int FindAllSymbolsInScopeList(LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL);
	int FindAllVariablesInScopeList(LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL);
	int FindAllSymbolsInScopeAndFileList(LPCSTR scope, LPCSTR bcl, uint fileID, int dbs = MultiParse::DB_ALL);
	//	int FindSymbolInScopeList(LPCSTR sym, LPCSTR scope, LPCSTR bcl, int dbs = MultiParse::DB_ALL);
	int FindExactList(LPCSTR sym, uint scopeHash, int dbs = MultiParse::DB_ALL);
	int FindExactList(const WTString& sym, uint scopeHash, int dbs = MultiParse::DB_ALL)
	{
		return FindExactList(sym.c_str(), scopeHash, dbs);
	}
	//	int FindExactList(uint symHash, uint scopeHash, int dbs = MultiParse::DB_ALL );
	//	int FindAllClasses(int dbs = MultiParse::DB_ALL );

  private:
	DBQuery();
};
