#pragma once
#include "stdafxed.h"
#include "wtstring.h"
#include "Lock.h"

class ExpHistory
{
	CCriticalSection mLock;

  public:
	WTString GetExpHistory(const WTString& startswith);
	void AddExpHistory(const WTString& sym);
	void Clear();
};

extern ExpHistory* g_ExpHistory;

class CRollingBuffer
{
	WTString m_buf;
	mutable CCriticalSection mLock;

  public:
	void Init();
	void Save();
	void Clear();
	void Add(const WTString& str);
	WTString GetStr() const
	{
		AutoLockCs l(mLock);
		return m_buf;
	}

	// Higher rank notes last typed, -1 = not found.
	int Rank(LPCSTR sym)
	{
		// Should be strRstrWholeWord, so we don't need to pass in " sym "
		WTString tmp;

		{
			AutoLockCs l(mLock);
			tmp = m_buf;
		}

		return tmp.rfind(sym);
	}
};

extern CRollingBuffer g_rbuffer;
