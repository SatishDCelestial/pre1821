#include "stdafxed.h"
#include "DBLock.h"
#include "mainThread.h"
#include "PROJECT.H"

RWLock::RWLock()
    : mActiveReaders(0), mActiveWriters(0)
#ifdef _DEBUG
      ,
      mWritingThread(NULL), mAllowWriterRecursion(FALSE)
#endif // _DEBUG
{
}

RWLock::RWLock(BOOL allowWriterRecursion)
    : mActiveReaders(0), mActiveWriters(0)
#ifdef _DEBUG
      ,
      mWritingThread(NULL), mAllowWriterRecursion(allowWriterRecursion)
#endif // _DEBUG
{
}

void RWLock::StartWriting()
{
#ifdef _DEBUG
	const DWORD curTid = ::GetCurrentThreadId();
	bool refactoringAllowance = false;

	// even though the lock is re-entrant, it shouldn't be used
	// as such - that would mean the locking isn't granular enough
	_ASSERTE(mAllowWriterRecursion || GetWritingThread() != curTid || g_pDBLock != this ||
	         (!mAllowWriterRecursion && GlobalProject->IsBusy() && g_pDBLock == this));
#endif // _DEBUG

	mWriterLock.Lock();
	for (;;)
	{
		mCountsLock.Lock();
		bool waitForActiveReaders = !!mActiveReaders;
		if (waitForActiveReaders && RefactoringActive::IsActive() && ::GetCurrentThreadId() == g_mainThread)
		{
			waitForActiveReaders = false;
#ifdef _DEBUG
			refactoringAllowance = true;
#endif
		}
		if (waitForActiveReaders)
		{
			mCountsLock.Unlock();
			::Sleep(5);
			continue;
		}
		else
		{
			mActiveWriters++;
#ifdef _DEBUG
			mWritingThread = curTid;
#endif // _DEBUG
			mCountsLock.Unlock();
			break;
		}
	}

	// This is NOT possible!
	_ASSERTE(!mActiveReaders || refactoringAllowance);
}

RWLock* g_pDBLock = NULL;
