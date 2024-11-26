#pragma once

#include "WTString.h"

// PooledThreadBase
// ----------------------------------------------------------------------------
// Pure virtual base class for waitable thread classes.
// Does not autostart thread since derived classes won't be fully constructed
// during thread start if started in base class ctor.
// Derived classes can call StartThread() in their ctor.
// Running instances block shutdown (via live thread count that is checked by ArePooledThreadsRunning).
//
// If instantiated on the stack, autoDelete MUST be false.
// If instantiated in the heap, autoDelete should probably be true.
// Can use AddRef() and Release() to help govern lifetime of autoDeletable instantiations.
// AddRef and Release require autoDelete be true.
//
class PooledThreadBase
{
  public:
	enum ThreadState
	{
		ts_Constructed,
		ts_Running,
		ts_Finished
	};
	PooledThreadBase(const char* thrdName, bool autoDelete = true);
	virtual ~PooledThreadBase();

	virtual bool StartThread(ULONG thrdFlags = WT_EXECUTEINLONGTHREAD);

	DWORD Wait(DWORD timeOut, bool waitForThreadStart = true) const;

	void AddRef()
	{
		_ASSERTE(mAutoDelete);
		InterlockedIncrement(&mRefCnt);
	}
	void Release()
	{
		_ASSERTE(mAutoDelete);
		_ASSERTE(mRefCnt > 0);
		if (InterlockedDecrement(&mRefCnt) <= 0)
			delete this;
	}

	bool HasStarted() const
	{
		return mThreadState != ts_Constructed;
	}
	bool IsRunning() const
	{
		return mThreadState == ts_Running;
	}
	bool IsFinished() const
	{
		return mThreadState == ts_Finished;
	}
	DWORD GetThreadId() const
	{
		return mThreadId;
	}

  protected:
	static DWORD WINAPI ThreadProc(LPVOID pVoid);

	virtual void Run() = 0;

  private:
	ThreadState mThreadState;
	HANDLE mDoneEvent;
	WTString mThreadName;
	bool mAutoDelete;
	volatile long mRefCnt;
	DWORD mThreadId;
};

// FunctionThread
// ----------------------------------------------------------------------------
// Wraps a standalone function in a thread so that Wait can be called on the
// instantiated object.
// The function signature should be: void Function(LPVOID param)
// Can be instantiated on heap or stack (same restrictions as base class).
// Defaults to thread autostart.
// Running instances block shutdown (via live thread count that is checked by ArePooledThreadsRunning).
//
class FunctionThread : public PooledThreadBase
{
  public:
	typedef void (*THREADPROC)(LPVOID);

	// ctor for NULL thread proc param
	FunctionThread(THREADPROC proc, const char* name, bool autoDelete, bool autoStart = true,
	               ULONG autoStartThrdFlags = WT_EXECUTEINLONGTHREAD)
	    : PooledThreadBase(name, autoDelete), mProc(proc), mVoidParam(NULL)
	{
		if (autoStart)
			StartThread(autoStartThrdFlags);
	}

	// ctor that copies void pointer
	FunctionThread(THREADPROC proc, LPVOID pVoidParam, const char* name, bool autoDelete, bool autoStart = true,
	               ULONG autoStartThrdFlags = WT_EXECUTEINLONGTHREAD)
	    : PooledThreadBase(name, autoDelete), mProc(proc), mVoidParam(pVoidParam)
	{
		if (autoStart)
			StartThread(autoStartThrdFlags);
	}

	// ctor that makes local copy of a string param
	FunctionThread(THREADPROC proc, LPCSTR pStringParam, const char* name, bool autoDelete, bool autoStart = true,
	               ULONG autoStartThrdFlags = WT_EXECUTEINLONGTHREAD)
	    : PooledThreadBase(name, autoDelete), mProc(proc), mStringParamCopy(pStringParam)
	{
		mVoidParam = (LPVOID)(LPCSTR)mStringParamCopy.c_str();
		if (autoStart)
			StartThread(autoStartThrdFlags);
	}

	// ctor that makes local copy of a string param
	FunctionThread(THREADPROC proc, LPCWSTR pStringParam, const char* name, bool autoDelete, bool autoStart = true,
	               ULONG autoStartThrdFlags = WT_EXECUTEINLONGTHREAD)
	    : PooledThreadBase(name, autoDelete), mProc(proc), mStringParamCopyW(pStringParam)
	{
		mVoidParam = (LPVOID)(LPCWSTR)mStringParamCopyW;
		if (autoStart)
			StartThread(autoStartThrdFlags);
	}

  protected:
	virtual void Run()
	{
		mProc(mVoidParam);
	}

  private:
	THREADPROC mProc;
	LPVOID mVoidParam;
	WTString mStringParamCopy;
	CStringW mStringParamCopyW;
};

// LambdaThread
// ----------------------------------------------------------------------------
// Wraps a lambda (captured as std::function<void()>) in a thread so that
// Wait can be called on the instantiated object.
// The lambda signature should be: [...]() -> void
// Can be instantiated on heap or stack (same restrictions as base class).
// Defaults to thread autostart.
// Running instances block shutdown (via live thread count that is checked by ArePooledThreadsRunning).
//
class LambdaThread : public PooledThreadBase
{
  public:
	LambdaThread(std::function<void()> fnc, const char* name, bool autoDelete, bool autoStart = true,
	             ULONG autoStartThrdFlags = WT_EXECUTEINLONGTHREAD)
	    : PooledThreadBase(name, autoDelete), mFunc(fnc)
	{
		if (autoStart)
			StartThread(autoStartThrdFlags);
	}

  protected:
	virtual void Run()
	{
		mFunc();
	}

  private:
	std::function<void()> mFunc;
};

// Checks live thread count for non-zero value
bool ArePooledThreadsRunning();
