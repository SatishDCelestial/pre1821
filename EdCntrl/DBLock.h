#pragma once

#include "mainThread.h"
#include "SpinCriticalSection.h"
#include "Lock.h"
#include "RefactoringActive.h"

// RWLock
// ----------------------------------------------------------------------------
// In-process lock used to control read-write access to the in-memory database.
// Typically accessed through DBReadLock and DBWriteLock.
// Allows multiple readers to be active if there is no writer.
// Allows a single writer to be active exclusively.
// Do not attempt to acquire LockParseThread or DatabaseDirectoryLock while
// g_pDBLock is locked.
// LockParseThread and DatabaseDirectoryLock are not required to be locked when
// using g_pDBLock (or DBReadLock and DBWriteLock) since this is only used to
// protect access to the in-process/in-memory dictionaries.
//
class RWLock
{
	mutable CSpinCriticalSection mCountsLock;
	CCriticalSection mWriterLock;
	std::atomic_int mActiveReaders;
	std::atomic_int mActiveWriters;
#ifdef _DEBUG
	// for debug and assert purposes
	volatile DWORD mWritingThread;
	volatile BOOL mAllowWriterRecursion;
	std::unordered_set<DWORD> mReadingThreads;
	std::shared_mutex mReadingThreads_lock;
#endif // _DEBUG

  public:
	RWLock();
	// this ctor is no different than default ctor in release builds
	RWLock(BOOL allowWriterRecursion);

	void StartReading()
	{
		// do not lock for reading if already owned for
		// writing - this would mean the locking isn't granular enough
		_ASSERTE(GetWritingThread() != GetCurrentThreadId());
		int writer = 0;
		for (;;)
		{
			mCountsLock.Lock();
			writer = mActiveWriters;
			if (writer)
			{
				mCountsLock.Unlock();
				mWriterLock.Lock(); // wait for writer to finish...
				mWriterLock.Unlock();
				continue;
			}
			else
			{
				mActiveReaders++;
				mCountsLock.Unlock();
				break;
			}
		}

		// This is NOT possible!
		_ASSERTE(!mActiveWriters && "StartReading while write is active");

		#ifdef _DEBUG
		{
			std::lock_guard l(mReadingThreads_lock);
			mReadingThreads.insert(::GetCurrentThreadId());
		}
		#endif
	}

	void StopReading()
	{
		mActiveReaders--;

		#ifdef _DEBUG
		{
			std::lock_guard l(mReadingThreads_lock);
			mReadingThreads.erase(::GetCurrentThreadId());
		}
		#endif
	}

	// first writer in gets write lock, blocks other writers, then waits
	// for active readers to finish (if any) but allows more readers to
	// start while waiting for active readers.
	// active readers get a bit of priority over waiting writers due to writers
	// waiting with a sleep which gives a chance for more readers to become active.
	void StartWriting();

	void StopWriting()
	{
		// do in reverse order of that used in StartWriting
#ifdef _DEBUG
		if (1 == mActiveWriters)
			mWritingThread = NULL;
#endif // _DEBUG
		mActiveWriters--;
		mWriterLock.Unlock();
	}

#ifdef _DEBUG
	DWORD GetWritingThread() const
	{
		return mWritingThread;
	}
#endif // _DEBUG

	BOOL ActiveReadersOrWriters() const
	{
		AutoLockCs l(mCountsLock);
		return mActiveReaders || mActiveWriters;
	}
};

template <class T> class RWLockReaderT
{
	T* mLock;

  public:
	RWLockReaderT() : mLock(NULL)
	{
	}

	RWLockReaderT(T* lock) : mLock(lock)
	{
		mLock->StartReading();
	}

	RWLockReaderT(T& lock) : mLock(&lock)
	{
		mLock->StartReading();
	}

	void SetLock(T* lock)
	{
		_ASSERTE(!mLock);
		mLock = lock;
		mLock->StartReading();
	}

	~RWLockReaderT()
	{
		mLock->StopReading();
	}
};

typedef RWLockReaderT<RWLock> RWLockReader;

template <class T> class RWLockWriterT
{
	T* mLock;

  public:
	RWLockWriterT(T* lock) : mLock(lock)
	{
		mLock->StartWriting();
	}

	RWLockWriterT(T* lock, int checkUiThread) : mLock(lock)
	{
		if (checkUiThread)
		{
#if !defined(VA_CPPUNIT)
			// Do not write on UI thread, but allow if refactoring
			_ASSERTE(g_mainThread != ::GetCurrentThreadId() || RefactoringActive::IsActive());
#endif // !CPPUNIT
		}
		mLock->StartWriting();
	}

	RWLockWriterT(T& lock) : mLock(&lock)
	{
		mLock->StartWriting();
	}

	~RWLockWriterT()
	{
		mLock->StopWriting();
	}
};

typedef RWLockWriterT<RWLock> RWLockWriter;

extern RWLock* g_pDBLock;

// DB_READ_LOCK
// ----------------------------------------------------------------------------
// Used to gain read access to the in-memory database through the RWLock.
// Do not attempt to acquire LockParseThread or DatabaseDirectoryLock while
// g_pDBLock is locked reading.
// Don't read too much into read access - the hashtable can be added to during a
// read lock; and strings may be assigned to as well (as in DType::GetStrs).
//
#define DB_READ_LOCK RWLockReader _dbReadLock(g_pDBLock)

// DB_WRITE_LOCK
// ----------------------------------------------------------------------------
// Used to gain write access to the in-memory database through the RWLock.
// Do not attempt to acquire LockParseThread or DatabaseDirectoryLock while
// g_pDBLock is locked writing.
// Don't read too much into write access - this is really used to lock the
// hashtable when entries are being deleted - not when created/written.
//
#define DB_WRITE_LOCK RWLockWriter _dbWriteLock(g_pDBLock, 1)

// RWLockLite
// ----------------------------------------------------------------------------
// Lighter-weight version of RWLock.
// Uses spin + critical section instead of 2 critical sections.
// Also differs in that waiting writers block readers, whereas RWLock has
// waiting writers that don't prevent new readers from reading.
// Use for very short operations (reads, assignments, etc).
//
class RWLockLite
{
	CCriticalSection mCountsLock;
	std::atomic_int mActiveReaders;
	std::atomic_int mActiveWriters;
	DWORD mActiveWriter = 0;

  public:
	RWLockLite() : mActiveReaders(0), mActiveWriters(0)
	{
	}

	void StartReading()
	{
		int writer = 0;
		for (int waitForWriterSpinCnt = 0;;)
		{
			mCountsLock.Lock();
			writer = mActiveWriters;
			if (writer)
			{
				if (::GetCurrentThreadId() == mActiveWriter)
				{
					// re-entrant read while write lock held
					// this is allowed (but the reverse is not)
					mActiveReaders++;
					mCountsLock.Unlock();
					break;
				}

				mCountsLock.Unlock();
				if (++waitForWriterSpinCnt > 4000)
				{
					waitForWriterSpinCnt = 0;
					::Sleep(1);
				}
				continue;
			}
			else
			{
				mActiveReaders++;
				mCountsLock.Unlock();
				break;
			}
		}

		// This is NOT possible!
		_ASSERTE((!mActiveWriters || (::GetCurrentThreadId() == mActiveWriter)) &&
		         "StartReading while write is active");
	}

	void StopReading()
	{
		mActiveReaders--;
	}

	// first writer in gets write lock, blocks other writers, then waits
	// for active readers to finish (if any) and doesn't let any other readers
	// start while waiting for active readers.
	// After start writing, other writers come in and then wait for active writer.
	void StartWriting()
	{
		int writers = 0;
		int readers = 0;
		const DWORD myTid = ::GetCurrentThreadId();
		mCountsLock.Lock();
		for (;;)
		{
			// wait for active readers
			for (int waitForReaderSpinCnt = 0;;)
			{
				readers = mActiveReaders;
				if (readers)
				{
					if (++waitForReaderSpinCnt > 4000)
					{
						waitForReaderSpinCnt = 0;
						::Sleep(1);
					}
				}
				else
					break;
			}

			mActiveWriters++;
			writers = mActiveWriters;
			if (1 == writers)
			{
				// we are the active writer
				mActiveWriter = myTid;
				break;
			}
			else if (myTid == mActiveWriter)
			{
				// re-entrant write lock
				break;
			}

			// if this happens, there was already a writer so we have to
			// retry after undoing the count increment.
			// just need to wait for the other writer to finish at this point.
			// no new readers can come into play at this point.
			_ASSERTE(writers > 1);
			mActiveWriters--;
			::Sleep(1);
		}
		mCountsLock.Unlock();

		// This is NOT possible!
		// if this fires, write lock needs to be acquired before read (assuming write thread already has read lock).
		// Write must be held before read is acquired on same thread.
		_ASSERTE(!mActiveReaders);
	}

	void StopWriting()
	{
		mActiveWriters--;
		if (0 == mActiveWriters)
		{
			mCountsLock.Lock();
			if (mActiveWriter == ::GetCurrentThreadId())
				mActiveWriter = 0;
			mCountsLock.Unlock();
		}
	}

#ifdef _DEBUG
	void AssertWriterIsThisThread() const
	{
		_ASSERTE(mActiveWriter == ::GetCurrentThreadId());
	}
#else
	void AssertWriterIsThisThread() const
	{
	}
#endif
};

typedef RWLockWriterT<RWLockLite> RWLockLiteWriter;
typedef RWLockReaderT<RWLockLite> RWLockLiteReader;
