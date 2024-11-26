#pragma once

#include <atomic>

extern DWORD g_mainThread;
extern std::atomic<int> gProcessingMessagesOnUiThread;

#if defined(_PROCESSTHREADSAPI_H_) && !defined _ASSERT_IF_NOT_ON_MAIN_THREAD
static inline bool IsMainThread(){ return ::GetCurrentThreadId() == g_mainThread; }
#define _ASSERT_IF_NOT_ON_MAIN_THREAD() _ASSERT(IsMainThread())
#define _THROW_IF_NOT_ON_MAIN_THREAD(toThrow) if (!IsMainThread()) { throw (toThrow); }
#endif