#include "stdafxed.h"
#include "edcnt.h"
#include "timer.h"

#ifdef CTIMER
DWORD g_nAllocs, g_nAllocsSz;
CCriticalSection g_timerCritSect;
BOOL g_doTimers = TRUE;
#endif
