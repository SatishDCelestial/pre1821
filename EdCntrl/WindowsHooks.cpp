#include "stdafxed.h"
#include "WindowsHooks.h"
#include "utils_goran.h"
#include "WTString.h"
#include "mainThread.h"

#include <memory>
#include <map>
#include <list>
#include <set>

#ifdef _DEBUG
#define WINDOWSHOOKS_TRACE 0
#else
#define WINDOWSHOOKS_TRACE 0
#endif

#if defined(WINDOWSHOOKS_TRACE) && (WINDOWSHOOKS_TRACE == 1)

#include "TraceWindowFrame.h"

void __DbgTrace(LPCTSTR lpszFormat, ...)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	WTString trace_str;
	va_list argList;
	va_start(argList, lpszFormat);
	trace_str.FormatV(lpszFormat, argList);
	va_end(argList);
	TraceHelp th(trace_str);
}

#define DbgTrace(format, ...)                                                                                          \
	if (false)                                                                                                         \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__);                                                               \
	else                                                                                                               \
		__DbgTrace(format, __VA_ARGS__)

#else
#define DbgTrace(s, ...) ((void)0)
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace WindowsHooks
{
static DWORD num_hooks = 0;

struct HookData
{
	typedef std::shared_ptr<HookData> Ptr;

	CCriticalSection cs;
	std::list<CHook*> hookers;
	std::set<CHook*> busy_add;
	std::set<CHook*> busy_remove;
	HHOOK hook = nullptr;
	DWORD num_busy = 0;

	bool remove_hooker(CHook* _hook)
	{
		for (auto it = hookers.cbegin(); it != hookers.cend(); ++it)
		{
			if (*it == _hook)
			{
				hookers.erase(it);
				return true;
			}
		}
		return false;
	}

	void add_hooker(CHook* _hook)
	{
		remove_hooker(_hook);
		hookers.push_back(_hook);
	}

	~HookData()
	{
		__lock(cs);

		if (hook)
		{
			DbgTrace(_T("~HookData: unhooking"));

			::UnhookWindowsHookEx(hook);
			InterlockedDecrement(&num_hooks);
			hook = nullptr;
		}
	}
};

class CHookState
{
	UCHAR& m_state;

	enum hook_state
	{
		none = 0x00,
		hooking = 0x01,
		ending = 0x02,
		starting = 0x04,
	};

	void set(hook_state s, bool val = true)
	{
		if (val)
			m_state |= (UCHAR)s;
		else
			m_state &= ~(UCHAR)s;
	}

	bool get(hook_state s) const
	{
		return (m_state & (UCHAR)s) == (UCHAR)s;
	}

  public:
	CHookState(UCHAR& s) : m_state(s)
	{
	}

	WTString ToString() const
	{
		if (IsEmpty())
			return _T("Empty");

		WTString str;

		auto append = [&](LPCTSTR s) {
			if (!str.IsEmpty())
				str += '|';
			str += s;
		};

		if (IsHooking())
			append(_T("Hooking"));

		if (IsEnding())
			append(_T("Ending"));

		if (IsStarting())
			append(_T("Starting"));

		return str;
	}

	bool IsEmpty() const
	{
		return m_state == none;
	}
	bool IsHooking() const
	{
		return get(hooking);
	}
	bool IsEnding() const
	{
		return get(ending);
	}
	bool IsStarting() const
	{
		return get(starting);
	}

	void Clear()
	{
		m_state = none;
	}

	void SetHooking(bool val = true)
	{
		set(hooking, val);
	}
	void SetEnding(bool val = true)
	{
		set(starting, !val);
		set(ending, val);
	}
	void SetStarting(bool val = true)
	{
		set(starting, val);
		set(ending, !val);
	}
};

class CHookManager
{
	static CCriticalSection hook_data_map_cs;
	static std::map<int, HookData::Ptr> hook_data_map;

  public:
	static HookData::Ptr GetHookData(int id_hook, bool allow_new)
	{
		__lock(hook_data_map_cs);

		if (allow_new)
		{
			HookData::Ptr& ptr = hook_data_map[id_hook];

			if (ptr.get() == nullptr)
				ptr.reset(new HookData());

			_ASSERTE(ptr);

			return ptr;
		}
		else
		{
			auto found = hook_data_map.find(id_hook);

			if (found != hook_data_map.end())
				return found->second;

			return HookData::Ptr();
		}
	}

	static void EraseHookData(int id_hook)
	{
		__lock(hook_data_map_cs);

		DbgTrace(_T("EraseHookData: ID: %d"), id_hook);

		hook_data_map.erase(id_hook);
	}

	static void CallHookMessages(HookData::Ptr hdata, int id_hook, int code, WPARAM wparam, LPARAM lparam)
	{
		_ASSERTE(hdata);

		if (!hdata)
			return;

		for (CHook* hook : hdata->hookers)
		{
			try
			{
				if (hook)
				{
					CHookState hookState(hook->m_state[id_hook + 1]);

					if (hookState.IsHooking() && !hookState.IsEnding())
						hook->OnHookMessage(id_hook, code, (UINT_PTR)wparam, (LPVOID)lparam);
				}
			}
			catch (const std::exception& ex)
			{
				const char* what = ex.what();

				_ASSERTE(!"Unhandled exception in hook message!");

				vLog("WindowsHooks: Exception caught in hook message: %s", what);
			}
			catch (...)
			{
				_ASSERTE(!"Unhandled exception in hook message!");

				Log("WindowsHooks: Exception caught in hook message!");
			}
		}
	}

	static void ResolveHookers(HookData::Ptr hdata, int id_hook)
	{
		_ASSERTE(hdata);

		if (!hdata || hdata->num_busy != 1)
			return;

		try
		{
			DbgTrace(_T("ResolveHookers: ID: %d, Add: %d, Remove: %d"), id_hook, (int)hdata->busy_add.size(),
			         (int)hdata->busy_remove.size());

			for (CHook* hook : hdata->busy_add)
			{
				CHookState hookState(hook->m_state[id_hook + 1]);

				if (!hookState.IsStarting())
				{
					_ASSERTE(0);
					hookState.Clear();
					continue;
				}

				DbgTrace(_T("\tAdding: %x State: %s"), hook, hookState.ToString().c_str());

				hdata->add_hooker(hook);
				hookState.Clear();
				hookState.SetHooking(true);
			}

			hdata->busy_add.clear();

			for (CHook* hook : hdata->busy_remove)
			{
				CHookState hookState(hook->m_state[id_hook + 1]);

				if (!hookState.IsEnding())
				{
					_ASSERTE(0);
					hookState.Clear();
					continue;
				}

				DbgTrace(_T("\tRemoving: %x State: %s"), hook, hookState.ToString().c_str());

				hdata->remove_hooker(hook);
				hookState.Clear();
			}

			hdata->busy_remove.clear();

			DbgTrace(_T("\tHookers count: %d"), (int)hdata->hookers.size());

			if (hdata->hookers.empty() && hdata->hook)
			{
				DbgTrace(_T("ResolveHookers: Unhooking"));

				::UnhookWindowsHookEx(hdata->hook);
				InterlockedDecrement(&num_hooks);
				hdata->hook = nullptr;

				CHookManager::EraseHookData(id_hook);
			}
			else if (!hdata->hook)
			{
				DbgTrace(_T("ResolveHookers: hooking"));
				hdata->hook = ::SetWindowsHookEx(id_hook, GetProc(id_hook), AfxGetApp()->m_hInstance, g_mainThread);
				if (hdata->hook)
					InterlockedIncrement(&num_hooks);
			}
		}
		catch (const std::exception& ex)
		{
			const char* what = ex.what();

			_ASSERTE(!"Unhandled exception in RemoveEndingHooks!");

			vLog("WindowsHooks: Exception caught in CHookManager::ResolveHookers: %s", what);
		}
		catch (...)
		{
			_ASSERTE(!"Unhandled exception in RemoveEndingHooks!");

			Log("WindowsHooks: Exception caught in CHookManager::ResolveHookers!");
		}
	}

	static HOOKPROC GetProc(int id_hook);
	static bool IsValidHookId(int id_hook);
};

template <int ID_HOOK> class CHookProc
{
  public:
	static LRESULT CALLBACK Proc(int code, WPARAM wparam, LPARAM lparam)
	{
		HookData::Ptr hdata = CHookManager::GetHookData(ID_HOOK, false);

		_ASSERTE(hdata);

		if (!hdata)
			return 0;

		__lock(hdata->cs);

		struct busy_handler
		{
			HookData::Ptr p;

			busy_handler(HookData::Ptr _p) : p(_p)
			{
				InterlockedIncrement(&p->num_busy);
			}

			~busy_handler()
			{
				_ASSERTE(p->num_busy);

				if (p->num_busy)
					InterlockedDecrement(&p->num_busy);
			}
		} _auto_busy(hdata);

		if (lparam)
			CHookManager::CallHookMessages(hdata, ID_HOOK, code, wparam, lparam);

		LRESULT rslt = ::CallNextHookEx(hdata->hook, code, wparam, lparam);

		if (hdata->num_busy == 1 && (hdata->busy_add.size() || hdata->busy_remove.size()))
			CHookManager::ResolveHookers(hdata, ID_HOOK);

		return rslt;
	}
};

CCriticalSection CHookManager::hook_data_map_cs;
std::map<int, HookData::Ptr> CHookManager::hook_data_map;

HOOKPROC CHookManager::GetProc(int id_hook)
{
	_ASSERTE(CHookManager::IsValidHookId(id_hook));

	switch (id_hook)
	{
	case WH_MSGFILTER:
		return CHookProc<WH_MSGFILTER>::Proc;
	case WH_JOURNALRECORD:
		return CHookProc<WH_JOURNALRECORD>::Proc;
	case WH_JOURNALPLAYBACK:
		return CHookProc<WH_JOURNALPLAYBACK>::Proc;
	case WH_KEYBOARD:
		return CHookProc<WH_KEYBOARD>::Proc;
	case WH_GETMESSAGE:
		return CHookProc<WH_GETMESSAGE>::Proc;
	case WH_CALLWNDPROC:
		return CHookProc<WH_CALLWNDPROC>::Proc;
	case WH_CBT:
		return CHookProc<WH_CBT>::Proc;
	case WH_SYSMSGFILTER:
		return CHookProc<WH_SYSMSGFILTER>::Proc;
	case WH_MOUSE:
		return CHookProc<WH_MOUSE>::Proc;
	case WH_HARDWARE:
		return CHookProc<WH_HARDWARE>::Proc;
	case WH_DEBUG:
		return CHookProc<WH_DEBUG>::Proc;
	case WH_SHELL:
		return CHookProc<WH_SHELL>::Proc;
	case WH_FOREGROUNDIDLE:
		return CHookProc<WH_FOREGROUNDIDLE>::Proc;
	case WH_CALLWNDPROCRET:
		return CHookProc<WH_CALLWNDPROCRET>::Proc;
	case WH_KEYBOARD_LL:
		return CHookProc<WH_KEYBOARD_LL>::Proc;
	case WH_MOUSE_LL:
		return CHookProc<WH_MOUSE_LL>::Proc;
	}

	return nullptr;
}

bool CHookManager::IsValidHookId(int id_hook)
{
	return id_hook >= WH_MIN && id_hook <= WH_MAX;
}

CHook::CHook()
{
	memset(m_state, 0, sizeof(m_state));
}

CHook::~CHook()
{
}

bool CHook::BeginHook(int id_hook)
{
	try
	{
		_ASSERTE(CHookManager::IsValidHookId(id_hook));

		HookData::Ptr hdata = CHookManager::GetHookData(id_hook, true);

		_ASSERTE(hdata);

		if (hdata)
		{
			__lock(hdata->cs);

			DbgTrace(_T("BeginHook: ID: %d"), id_hook);

			CHookState hookState(m_state[id_hook + 1]);

			if (hookState.IsHooking() || hookState.IsStarting())
				return true;

			if (hdata->num_busy)
			{
				// hook is currently busy, so we can not
				// start it now but must start it after the call.
				if (!hookState.IsStarting())
				{
					hookState.SetStarting(true);
					hdata->busy_add.insert(this);
					hdata->busy_remove.erase(this);
					DbgTrace(_T("\tStarting: %x State: %s"), this, hookState.ToString().c_str());
				}

				return true;
			}

			if (!hdata->hook)
			{
				DbgTrace(_T("BeginHook: hooking"));
				hdata->hook =
				    ::SetWindowsHookEx(id_hook, CHookManager::GetProc(id_hook), AfxGetApp()->m_hInstance, g_mainThread);
				if (hdata->hook)
					InterlockedIncrement(&num_hooks);
			}

			if (hdata->hook)
			{
				hookState.Clear();
				hookState.SetHooking(true);

				DbgTrace(_T("\tAdding: %x State: %s"), this, hookState.ToString().c_str());

				hdata->add_hooker(this);

				DbgTrace(_T("\tHookers count: %d"), (int)hdata->hookers.size());

				return true;
			}
		}
	}
	catch (const std::exception& ex)
	{
		const char* what = ex.what();

		_ASSERTE(!"Unhandled exception in CHook::BeginHook!");

		vLog("WindowsHooks: Exception caught in CHook::BeginHook: %s", what);
	}
	catch (...)
	{
		_ASSERTE(!"Unhandled exception in CHook::BeginHook!");

		Log("WindowsHooks: Exception caught in CHook::BeginHook!");
	}

	return false;
}

bool CHook::IsHooking(int id_hook) const
{
	try
	{
		// Note: num_hooks counts ALL hooks,
		// not only those specific for id_hook!!!
		if (!num_hooks)
			return false;

		_ASSERTE(CHookManager::IsValidHookId(id_hook));

		HookData::Ptr hdata = CHookManager::GetHookData(id_hook, false);

		if (hdata)
		{
			__lock(hdata->cs);

			UCHAR state = m_state[id_hook + 1];
			CHookState hookState(state);

			return hookState.IsHooking() || hookState.IsStarting();
		}
	}
	catch (const std::exception& ex)
	{
		const char* what = ex.what();

		_ASSERTE(!"Unhandled exception in CHook::IsHooking!");

		vLog("WindowsHooks: Exception caught in CHook::IsHooking: %s", what);
	}
	catch (...)
	{
		_ASSERTE(!"Unhandled exception in CHook::IsHooking!");

		Log("WindowsHooks: Exception caught in CHook::IsHooking!");
	}

	return false;
}

void CHook::EndHook(int id_hook)
{
	try
	{
		// Note: num_hooks counts ALL hooks,
		// not only those specific for id_hook!!!
		if (!num_hooks)
			return;

		_ASSERTE(CHookManager::IsValidHookId(id_hook));

		HookData::Ptr hdata = CHookManager::GetHookData(id_hook, false);

		if (hdata)
		{
			__lock(hdata->cs);

			DbgTrace(_T("EndHook: ID: %d"), id_hook);

			CHookState hookState(m_state[id_hook + 1]);

			if (!hookState.IsHooking() || hookState.IsEnding())
			{
				// we need to clear the IsStarting bit,
				// otherwise we could get added deleted hooker
				if (hookState.IsStarting())
				{
					hookState.SetStarting(false);
					hdata->busy_add.erase(this);
				}

				return;
			}

			if (hdata->num_busy)
			{
				// hook is currently busy, so we can not
				// end it now but must end it after the call.
				if (!hookState.IsEnding())
				{
					hookState.SetEnding(true);
					hdata->busy_remove.insert(this);
					hdata->busy_add.erase(this);
					DbgTrace(_T("\tEnding: %x State: %s"), this, hookState.ToString().c_str());
				}

				return;
			}

			DbgTrace(_T("\tRemoving: %x State: %s"), this, hookState.ToString().c_str());

			hookState.Clear();
			hdata->remove_hooker(this);

			DbgTrace(_T("\tHookers count: %d"), (int)hdata->hookers.size());

			if (hdata->hookers.empty() && hdata->hook)
			{
				DbgTrace(_T("EndHook: unhooking"));

				::UnhookWindowsHookEx(hdata->hook);
				InterlockedDecrement(&num_hooks);
				hdata->hook = nullptr;
				CHookManager::EraseHookData(id_hook);
			}
		}
	}
	catch (const std::exception& ex)
	{
		const char* what = ex.what();

		_ASSERTE(!"Unhandled exception in CHook::EndHook!");

		vLog("WindowsHooks: Exception caught in CHook::EndHook: %s", what);
	}
	catch (...)
	{
		_ASSERTE(!"Unhandled exception in CHook::EndHook!");

		Log("WindowsHooks: Exception caught in CHook::EndHook!");
	}
}

void CHook::EndAllHooks()
{
	// Note: num_hooks counts ALL hooks,
	// not only those specific for id_hook!!!
	if (!num_hooks)
		return;

	for (int h = WH_MIN; h <= WH_MAX; h++)
		EndHook(h);
}
} // namespace WindowsHooks
