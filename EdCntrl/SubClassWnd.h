#pragma once

#include "VaMessages.h"

extern bool g_inMacro;
extern CWnd* gVaMainWnd;

using uint = unsigned int;

void NavAdd(const CStringW& file, uint ln, bool force = false);
void NavGo(bool back = true);

// RunFromMainThread sends a message to the MainWindow, which calls cbFunc(arg) on the main thread.
// It also ensures cbFunc and passed argument are type checked.
// Not safe to use with reference ARG_TYPEs.
template <typename RETURN_TYPE>
inline RETURN_TYPE RunFromMainThread(RETURN_TYPE(WINAPI* cbFunc)())
{
	return (RETURN_TYPE)SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_MAIN_THREAD_CB, (WPARAM)cbFunc, NULL);
}

template <typename RETURN_TYPE, typename ARG_TYPE>
inline RETURN_TYPE RunFromMainThread(RETURN_TYPE(WINAPI* cbFunc)(ARG_TYPE), ARG_TYPE arg)
{
	void* args[] = {&arg, NULL};
	return (RETURN_TYPE)SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_MAIN_THREAD_CB, (WPARAM)cbFunc, (LPARAM)&args);
}
// Same thing but with two arguments
template <typename RETURN_TYPE, typename ARG_TYPE1, typename ARG_TYPE2>
inline RETURN_TYPE RunFromMainThread(RETURN_TYPE(WINAPI* cbFunc)(ARG_TYPE1, ARG_TYPE2), ARG_TYPE1 arg1, ARG_TYPE2 arg2)
{
	void* args[] = {&arg1, &arg2, NULL};
	return (RETURN_TYPE)SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_MAIN_THREAD_CB, (WPARAM)cbFunc, (LPARAM)&args);
}

void RunFromMainThread(std::function<void()> fnc, bool synchronous = true);
LRESULT RunUiThreadTasks();
int GetPendingUiThreadTaskCount();

#ifndef VA_CPPUNIT
void RunFromMainThread2(std::move_only_function<void()> work, uint8_t merge_marker = 0);
void RunAllPendingInMainThread();
void FlushAllPendingInMainThread();
#else
inline void RunFromMainThread2(std::move_only_function<void()> work, uint8_t merge_marker = 0)
{
	(void)merge_marker;
	RunFromMainThread([&] { work(); }, true);
}
inline void RunAllPendingInMainThread() {}
inline void FlushAllPendingInMainThread() {}
#endif
