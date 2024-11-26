#pragma once

#include "WTString.h"
#include "SpinCriticalSection.h"
#include "Lock.h"

class ThreadSafeStr
{
  public:
	ThreadSafeStr()
	{
	}
	~ThreadSafeStr()
	{
	}

	void Empty()
	{
		AutoLockCs l(mLock);
		mStr.Empty();
	}

	BOOL IsEmpty() const
	{
		AutoLockCs l(mLock);
		return mStr.IsEmpty();
	}

	void Set(const WTString& str)
	{
		AutoLockCs l(mLock);
		mStr = str;
	}

	WTString Get() const
	{
		AutoLockCs l(mLock);
		return mStr;
	}

  private:
	mutable CSpinCriticalSection mLock;
	WTString mStr;
};

class ThreadSafeStrW
{
  public:
	ThreadSafeStrW()
	{
	}
	~ThreadSafeStrW()
	{
	}

	void Empty()
	{
		AutoLockCs l(mLock);
		mStr.Empty();
	}

	BOOL IsEmpty() const
	{
		AutoLockCs l(mLock);
		return mStr.IsEmpty();
	}

	void Set(const CStringW& str)
	{
		AutoLockCs l(mLock);
		mStr = str;
	}

	CStringW Get() const
	{
		AutoLockCs l(mLock);
		return mStr;
	}

  private:
	mutable CSpinCriticalSection mLock;
	CStringW mStr;
};
