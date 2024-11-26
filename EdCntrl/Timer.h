#ifndef timer_h
#define timer_h

// I wrapped timers so they only fire if logging is enabled,
// so we don't need to make a special build for timing info.

//#define CTIMER
#undef CTIMER

#ifdef CTIMER
#include "log.h"
#define TIME_ALL_THREADS 1
#define DEFBASICTIMER(t, note)                                                                                         \
	;                                                                                                                  \
	static CTimer t(#t);                                                                                               \
	CBasicTimerWrapper inst_##t(&t, g_loggingEnabled ? note : "");
#define DEFTIMERNOTE(t, note)                                                                                          \
	;                                                                                                                  \
	DEFBASICTIMER(t##_Total, note);                                                                                    \
	static CTimer t(#t);                                                                                               \
	CTimerInstWrapper inst_##t(&t, g_loggingEnabled ? note : "");
#define DEFTIMER(t)                                                                                                    \
	;                                                                                                                  \
	DEFTIMERNOTE(t, NULL);

#ifdef _DEBUG
extern BOOL g_doTimers;
#define doTimers (g_doTimers || g_loggingEnabled)
#else
#define doTimers g_loggingEnabled
#endif // _DEBUG

extern DWORD g_loggingEnabled;

class CTimer;
static CTimer* g_last_active_timer = NULL;
static DWORD g_last_active_thread_timer = NULL;
extern DWORD g_nAllocs, g_nAllocsSz;
class CTimer
{
  public:
	static WTString m_lfile;
	DWORD m_total;
	DWORD m_avg;
	DWORD m_max;
	DWORD m_start;
	DWORD m_nAlloc;
	DWORD m_nAllocSz;
	DWORD m_nAllocStart;
	DWORD m_nAllocSzStart;
	CTimer* m_subtimer;
	WTString m_name;
	int m_deep;
	int m_count;
	DWORD m_curthread;
	CTimer(LPCSTR name = NULL)
	{
		m_name = name;
		m_total = 0;
		m_max = 0;
		m_deep = 0;
		m_count = 0;
		m_nAlloc = 0;
		m_nAllocSz = 0;
	}
	void Start()
	{
		ASSERT(m_deep < 99);
		m_start = GetTickCount();
		m_count++;
		m_deep++;
		m_nAllocStart = g_nAllocs;
		m_nAllocSzStart = g_nAllocsSz;
	};
	void Stop(LPCSTR tname = NULL)
	{
		try
		{
			if (!m_deep)
				return;
			m_deep--;
			DWORD t = GetTickCount() - m_start;
			m_total += t;
			if (t > m_max)
			{
				m_max = t;
			}
			m_avg = m_total / m_count;
			if (g_nAllocs > m_nAllocStart) // make sure it didnt roll over
				m_nAlloc += (g_nAllocs - m_nAllocStart);
			if (g_nAllocsSz > m_nAllocSzStart) // make sure it didnt roll over
				m_nAllocSz += (g_nAllocsSz - m_nAllocSzStart);
			if (t && /*(t - m_avg) > 1000 &&*/ (/*t == m_max ||*/ t > 1000))
			{
				CString logstr;
				CString__FormatA(logstr, "MaxTimeHit time %d, avg %d, max %d, tot %d, count %d for %s %s", t, m_avg,
				                 m_max, m_total, m_count, m_name, tname ? tname : "");
				extern const DWORD g_mainThread;
				if (g_mainThread == GetCurrentThreadId())
				{
					Log("MaxtimerMainthread");
				}
				Log(logstr);
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("TMR:");
			ASSERT(FALSE);
		}
		return;
	}
	~CTimer()
	{
		if (doTimers)
		{
			WTofstream timer("c:/va.log", VS_STD::ios::app);
			char buf[512];
			sprintf_s(buf, "Tot %ld, Agv %ld, Max %ld, Allocs %ld, sz %ld, Count %d %s\n", m_total, m_avg, m_max,
			          m_nAlloc, m_nAllocSz, m_count, m_name.c_str());
			timer << buf;
		}
	}
};
extern CCriticalSection g_timerCritSect;
/*
    basic timer that includes all subtimer
*/
class CBasicTimer
{
	CTimer* m_pTimer;
	WTString m_note;
	int m_deep;

  public:
	CBasicTimer(CTimer* timer, LPCSTR note = NULL)
	{
		g_timerCritSect.Lock();
		if (note)
			m_note = note;
		m_pTimer = timer;
		m_deep = m_pTimer->m_deep;
		if (!m_deep)
			m_pTimer->Start();
		g_timerCritSect.Unlock();
	}
	~CBasicTimer()
	{
		g_timerCritSect.Lock();
		if (!m_deep)
			m_pTimer->Stop(m_note);
		g_timerCritSect.Unlock();
	}
};
class CBasicTimerWrapper
{
	CBasicTimer* m_timer;

  public:
	CBasicTimerWrapper(CTimer* timer, LPCSTR note = NULL)
	{
		m_timer = doTimers ? new CBasicTimer(timer, note) : NULL;
	}
	~CBasicTimerWrapper()
	{
		if (m_timer)
		{
			try
			{
				delete m_timer;
			}
			catch (...)
			{
				VALOGEXCEPTION("TMR:");
			}
		}
	}
};

class CTimerInst
{
	CTimer* m_pTimer;
	CTimer* m_pLastTimer;
	bool m_glob;
	bool m_inmainthread;
	WTString m_note;

  public:
	CTimerInst(CTimer* timer, LPCSTR note = NULL)
	{
		try
		{
#ifdef TIME_ALL_THREADS
			m_inmainthread = TRUE;
#else
			m_inmainthread = (g_mainThread == GetCurrentThreadId());
#endif // TIME_ALL_THREADS
			if (!m_inmainthread)
				return;
			g_timerCritSect.Lock();

			m_pTimer = timer;
			m_note = note;
			m_pLastTimer = g_last_active_timer;
			if (m_pLastTimer)
				m_pLastTimer->Stop();
			g_last_active_timer = timer;
			m_pTimer->Start();
			g_timerCritSect.Unlock();
		}
		catch (...)
		{
			VALOGEXCEPTION("TMR:");
			ASSERT(FALSE);
		}
	}
	void Stop()
	{
		try
		{
			g_timerCritSect.Lock();
			if (m_pTimer)
				m_pTimer->Stop(m_note);
			m_pTimer = NULL;
			if (m_pLastTimer)
				m_pLastTimer->Start();
			g_last_active_timer = m_pLastTimer;
			g_timerCritSect.Unlock();
		}
		catch (...)
		{
			VALOGEXCEPTION("TMR:");
			ASSERT(FALSE);
		}
	}
	~CTimerInst()
	{
		if (m_inmainthread)
		{
			g_timerCritSect.Lock();
			if (m_pTimer)
				Stop();
			g_timerCritSect.Unlock();
		}
	}
};

class CTimerInstWrapper
{
	CTimerInst* m_timer;

  public:
	CTimerInstWrapper(CTimer* timer, LPCSTR note = NULL)
	{
		m_timer = doTimers ? new CTimerInst(timer, note) : NULL;
	}
	~CTimerInstWrapper()
	{
		try
		{
			if (m_timer)
			{
				delete m_timer;
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("TMR:");
			ASSERT(FALSE);
		}
	}
};

#else
#define DEFTIMER(t)
#define DEFTIMERNOTE(t, note)
#endif
#endif
