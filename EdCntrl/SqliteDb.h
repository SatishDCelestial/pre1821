#pragma once

#include "sqlite\CppSQLite3.h"
#include <memory>

class SqliteDb : public CppSQLite3DB
{
  public:
	SqliteDb() : CppSQLite3DB()
	{
	}
	SqliteDb(const CStringW& dbFile) : CppSQLite3DB(), mDbFile(dbFile)
	{
	}
	virtual ~SqliteDb()
	{
	}

	CStringW GetDbFilePath() const
	{
		return mDbFile;
	}
	CCriticalSection* GetTransactionLock()
	{
		return &mTransactionLock;
	}

  private:
	CStringW mDbFile;

	// this could be an RWLock but multiple concurrent readers is not a common scenario
	CCriticalSection mTransactionLock;
};

typedef std::shared_ptr<SqliteDb> SqliteDbPtr;
