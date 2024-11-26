#include "StdAfxEd.h"
#include <process.h>
#include "PooledThreadBase.h"
#include "..\common\ThreadName.h"
#include "Log.h"
#include "mainThread.h"
#include "VASeException\VASeException.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

long gActivePooledThreadCount = 0;

PooledThreadBase::PooledThreadBase(const char* thrdName, bool autoDelete /*= true*/)
    : mThreadState(ts_Constructed), mThreadName(thrdName), mAutoDelete(autoDelete), mRefCnt(0), mThreadId(0)
{
	mDoneEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	vCatLog("Parser.PooledThread", "+PooledThread: %s", mThreadName.c_str());
	VASetSeTranslator();
}

PooledThreadBase::~PooledThreadBase()
{
	_ASSERTE(!mRefCnt);
	if (mDoneEvent && mDoneEvent != INVALID_HANDLE_VALUE)
		::CloseHandle(mDoneEvent);
	vCatLog("Parser.PooledThread", "-PooledThread: %s", mThreadName.c_str());
}

bool PooledThreadBase::StartThread(ULONG thrdFlags /*= WT_EXECUTEINLONGTHREAD*/)
{
	InterlockedIncrement(&gActivePooledThreadCount);
	mThreadId = 0;
	if (ts_Finished == mThreadState)
	{
		// restarting, so no longer finished
		mThreadState = ts_Constructed;
		::ResetEvent(mDoneEvent);
		_ASSERTE(!mAutoDelete);
	}
	else
	{
		_ASSERTE(ts_Constructed == mThreadState);
	}

	if (mAutoDelete)
	{
		// balanced with the Release call in ThreadProc or with the failure
		// of QueueUserWorkItem below
		AddRef();
	}

	if (::QueueUserWorkItem(ThreadProc, (LPVOID)this, thrdFlags))
	{
		return true;
	}
	else
	{
		_ASSERTE(!"Failed to queue ThreadProc");
		vLog(" PooledThread: failed to queue thread(%s)", mThreadName.c_str());

		// set the done event in case caller blocks on Wait
		::SetEvent(mDoneEvent);
		// update threadId for debugging
		mThreadId = (DWORD)-1;

		if (mAutoDelete)
		{
			// thread didn't start, so need to balance with the AddRef above
			Release();
		}
		InterlockedDecrement(&gActivePooledThreadCount);
		return false;
	}
}

DWORD
PooledThreadBase::Wait(DWORD timeOut, bool waitForThreadStart /*= true*/) const
{
	_ASSERTE(mDoneEvent && mDoneEvent != INVALID_HANDLE_VALUE);

	// if WaitForSingleObject is not sufficient, make sure whatever it
	// is replaced with is compatible with the flag used in the call to
	// Start.  Alertable waits are not compatible with WT_EXECUTEINIOTHREAD.
	// Waits: http://msdn.microsoft.com/en-us/library/ms687069%28VS.85%29.aspx
	// APCs: http://msdn.microsoft.com/en-us/library/ms681951%28VS.85%29.aspx

	DWORD retval = ::WaitForSingleObject(mDoneEvent, timeOut);

	// stack based thread objects might bail - need to ensure thread has
	// actually started before allowing the object to go out of scope.
	// same argument holds for heap based objects; need to ensure the thread
	// has actually started and incremented its refCnt before Release is
	// called by a caller that has enabled refCounting on the thread object

	// previous comment should no longer apply since the AddRef has been moved from
	// the ThreadProc to StartThread.  But leaving this logic here so that
	// we will log calls to Wait which time out before the thread has even started.

	// [case: 39932] made check for start optional so that UI doesn't sputter
	if (waitForThreadStart)
	{
		for (int cnt = 5; !HasStarted() && cnt; --cnt)
			retval = ::WaitForSingleObject(mDoneEvent, timeOut);
	}
	else
	{
		// if !waitForThreadStart and !HasStarted, retval should be WAIT_TIMEOUT
		_ASSERTE(HasStarted() || WAIT_TIMEOUT == retval);
	}

	if (!HasStarted())
	{
		if (waitForThreadStart)
		{
			vLog(" PooledThread: wait called but thread(%s) hasn't started: wait(%lx)", mThreadName.c_str(), retval);
		}
		else
		{
			vLog(" PooledThread: thread(%s) hasn't started: wait(%lx)", mThreadName.c_str(), retval);
		}
	}

	return retval;
}

DWORD WINAPI PooledThreadBase::ThreadProc(LPVOID pVoid)
{
	PooledThreadBase* _this = reinterpret_cast<PooledThreadBase*>(pVoid);
	// save some state locally since if we are autoDeleted, we can't access it at exit
	VASetSeTranslator();
	WTString thrdName("VAX:");

#if !defined(_DEBUG)
	try
#endif // !_DEBUG
	{
		thrdName += _this->mThreadName;
	}
#if !defined(_DEBUG)
	catch (...)
	{
		// case=10181 (case=10946)
		const CString msg("PooledThreadBase::ThreadProc VERRRY bad exception caught in unknown thread - check log for "
		                  "last thread that started");
		VALOGEXCEPTION(msg);
		InterlockedDecrement(&gActivePooledThreadCount);
		return UINT_MAX;
	}
#endif // !_DEBUG

	const DWORD startTime = ::GetTickCount();
	_this->mThreadId = ::GetCurrentThreadId();
	DEBUG_THREAD_NAME_IF_NOT(thrdName.c_str(), g_mainThread);
	const bool autoDelete = _this->mAutoDelete;
	_this->mThreadState = ts_Running;

#if !defined(_DEBUG)
	try
#endif // !_DEBUG
	{
		_this->Run();
	}
#if !defined(_DEBUG)
	catch (...)
	{
		CString msg;
		CString__FormatA(msg, "PooledThreadBase::ThreadProc exception caught %s", thrdName.c_str());
		VALOGEXCEPTION(msg);
	}
#endif // !_DEBUG

	_this->mThreadState = ts_Finished;
	const DWORD endTime = ::GetTickCount();
	::SetEvent(_this->mDoneEvent);
	// Not safe to use _this after the SetEvent if _this is not autoDeletable.
	// If it is not autoDeletable, the SetEvent may result in _this
	// going out of scope (if it is stack based).
	if (autoDelete)
		_this->Release();

	WTString msg;
	msg.WTFormat("PooledThreadProc %s exit tid 0x%lx in %lu ticks\n", thrdName.c_str(), ::GetCurrentThreadId(),
	             endTime - startTime);
	vCatLog("Parser.PooledThread", "%s", msg.c_str());
#if !defined(NDEBUG)
	::OutputDebugString(msg.c_str());
#endif
	DEBUG_THREAD_NAME_IF_NOT("VAX:ThreadPoolIdleThread", g_mainThread);
	InterlockedDecrement(&gActivePooledThreadCount);
	return 0;
}

bool ArePooledThreadsRunning()
{
	long activeThreadCnt = InterlockedIncrement(&gActivePooledThreadCount);
	InterlockedDecrement(&gActivePooledThreadCount);
	return activeThreadCnt > 1;
}
