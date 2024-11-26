#pragma once

// DatabaseDirectoryLock
// ----------------------------------------------------------------------------
// This is a per-process lock used to protect writes to the IDE specific
// VA database directory (c:\program files\visual assist\[vc6|vc7|vc8]_x.
// It helps ensure that only one thread at a time writes to the db files.
//
class DatabaseDirectoryLock
{
  public:
	DatabaseDirectoryLock();
	~DatabaseDirectoryLock();
	static int GetLockCount()
	{
		return sDirectoryLockCount;
	}
	static DWORD GetOwningThreadID()
	{
		return (DWORD)(UINT_PTR)sDirectoryCs.m_sect.OwningThread;
	}

  private:
	void SetupLock();

  private:
	static int sDirectoryLockCount; // outstanding calls to lock directory from current process (handles recursion)
	static CCriticalSection sDirectoryCs; // used to manage this class in the current process
};
