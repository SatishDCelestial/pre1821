#pragma once

#include "Log.h"

void VAProcessMessages(UINT msg_start = 0, UINT msg_end = WM_USER - 1);

interface ILockable
{
	virtual ~ILockable() = default;
	virtual void Lock() = 0;
	virtual void Unlock() = 0;
};

// Simple object for scoped auto lock/unlock
template <class LockType> class AutoLockBlock
{
	LockType& mLock;

  public:
	AutoLockBlock(LockType& lock) : mLock(lock)
	{
		mLock.Lock();
	}
	~AutoLockBlock()
	{
		mLock.Unlock();
	}
};

template <class LockType> class AutoLockPtrBlock
{
	LockType* mLock;

  public:
	AutoLockPtrBlock() : mLock(nullptr)
	{
	}
	AutoLockPtrBlock(LockType* lock) : mLock(lock)
	{
		Lock();
	}
	~AutoLockPtrBlock()
	{
		if (mLock)
			mLock->Unlock();
	}

	void Lock()
	{
		if (mLock)
			mLock->Lock();
	}
	void Lock(LockType* lock)
	{
		_ASSERTE(!mLock);
		_ASSERTE(lock);
		mLock = lock;
		mLock->Lock();
	}
	void Unlock()
	{
		if (mLock)
			mLock->Unlock();
		mLock = nullptr;
	}
};

typedef AutoLockBlock<CCriticalSection> AutoLockCs;
typedef AutoLockPtrBlock<CCriticalSection> AutoLockCsPtr;
typedef AutoLockBlock<ILockable> AutoLockable;
typedef AutoLockPtrBlock<ILockable> AutoLockablePtr;

// Object for scoped auto lock/unlock that calls VAProcessMessages while
// waiting for lock acquisition.
template <class LockType> class CLockBlockMsg
{
	LockType& mLock;

	template <class LockType> DWORD GetLockTimeout(); // no default body

	template <> DWORD GetLockTimeout<CMutex>()
	{
		return 500;
	}

	// CCriticalSections block indefinitely (and assert in vs2003 if INFINITE is not used).
	template <> DWORD GetLockTimeout<CCriticalSection>()
	{
		return INFINITE;
	}

  public:
	CLockBlockMsg(LockType& theLock) : mLock(theLock)
	{
		//		vLog(("Lck 0x%x", GetCurrentThreadId()));
		// if mainthread, we need to process messages in case the thread we
		// are waiting for is waiting for main wnd to set status.
		int lcount;
		for (lcount = 100; lcount && !mLock.Lock(GetLockTimeout<LockType>()); lcount--)
			VAProcessMessages();
		if (!lcount)
		{
			Log("Warning: Timeout in CLockBlockMsg");
			ASSERT(FALSE);
		}
	}

	~CLockBlockMsg()
	{
		mLock.Unlock();
	}
};

typedef CLockBlockMsg<CCriticalSection> CLockMsgCs;
typedef CLockBlockMsg<CMutex> CLockMsgMtx;

// Lock a block of text
// so we dont need to call unlock on every return path
#define LOCKBLOCK(l) CLockMsgCs l##__(l)
