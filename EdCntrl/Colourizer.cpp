#include "stdafxed.h"
#include "Colourizer.h"
#include "utils_goran.h"
#include "WindowUtils.h"
#include "HookCode.h"
#include "mainThread.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include <mutex>
#include <array>
#include <unordered_map>
static constexpr uint32_t max_trampolines = 64;
static std::array<WNDPROC, max_trampolines> tramp_wndprocs; // store of fifth parameters

using trampoline_t = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

template <uint32_t INSTANCE = 0> static LRESULT CALLBACK trampoline(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	extern LRESULT CALLBACK ControlWndProc(trampoline_t orig_wndproc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	return ControlWndProc(tramp_wndprocs[INSTANCE], hwnd, msg, wparam, lparam);
}

template <uint32_t INSTANCE = 0>
static constexpr std::array<std::pair<trampoline_t, uint32_t>, max_trampolines> init_trampolines()
{
	// will instantiate all trampoline templates and return an array made of them
	if constexpr (INSTANCE < max_trampolines)
	{
		auto ret = init_trampolines<INSTANCE + 1>();
		ret[INSTANCE] = {&trampoline<INSTANCE>, INSTANCE};
		return ret;
	}
	else
		return {};
}
static constexpr decltype(init_trampolines<>()) all_trampolines = init_trampolines<>();
static std::mutex tramp_mutex;
static std::unordered_map<trampoline_t, uint32_t> tramp_free(all_trampolines.begin(), all_trampolines.end());
static std::unordered_map<trampoline_t, uint32_t> tramp_used;

static trampoline_t alloc_trampoline(WNDPROC wp)
{
	std::lock_guard l(tramp_mutex);
	assert(!tramp_free.empty());
	if (tramp_free.empty())
		return nullptr;

	auto n = tramp_free.extract(tramp_free.begin());
	assert(n);
	auto t = n.key();
	tramp_wndprocs[n.mapped()] = wp;
	tramp_used.insert(std::move(n));
	return t;
}
static void free_trampoline(trampoline_t t)
{
	std::lock_guard l(tramp_mutex);
	assert(tramp_used.contains(t));
	auto n = tramp_used.extract(t);
	if (n)
	{
		tramp_wndprocs[n.mapped()] = nullptr;
		tramp_free.insert(std::move(n));
	}
}
static void change_trampoline_ptr(trampoline_t t, WNDPROC wp)
{
	std::lock_guard l(tramp_mutex);
	assert(tramp_used.contains(t));
	auto it = tramp_used.find(t);
	if (it != tramp_used.end())
		tramp_wndprocs[it->second] = wp;
}

class CColourizer;
auto_instance<CColourizer, false> colourizer;

class CColourizer
{
  public:
	CColourizer()
	{
		GetDCHookNext = NULL;
		ReleaseDCHookNext = NULL;
		cwphook = ::SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, NULL, ::GetCurrentThreadId());
	}
	~CColourizer()
	{
		if (cwphook)
			::UnhookWindowsHookEx(cwphook);
		_ASSERTE(!GetDCHookNext);
		_ASSERTE(!ReleaseDCHookNext);
		ReleaseHooks();

		if (patches_getdc.size())
		{
			_ASSERTE(!"patches_getdc should have been empty");
			patches_getdc.clear();
		}

		if (patches_releasedc.size())
		{
			_ASSERTE(!"patches_releasedc should have been empty");
			patches_releasedc.clear();
		}
	}

	CColourizedControlPtr ColourizeControl(HWND hwnd)
	{
		if (!hwnd)
			return nullptr;

		__lock(cs);
		_ASSERTE(!controls.contains(hwnd));
		if (controls.contains(hwnd))
			return nullptr;

		char classname[512];
		classname[0] = 0;
		::GetClassName(hwnd, classname, countof(classname));
		if (contains(classes, classname))
		{
			CColourizedControlPtr cc = classes[classname](hwnd);
			if (cc)
			{
				controls[hwnd] = cc;
				mySetProp(hwnd, "__VA_do_not_colour", (HANDLE)1);
				UpdatePatches(cc, true);
				return cc;
			}
			_ASSERTE(cc);
		}

		return nullptr;
	}

	void ReleaseHooks()
	{
		// [case: 79980]
		if (GetDCHookNext)
		{
			_ASSERTE(!::WtIsHookInUse((PVOID*)&GetDCHookNext));
			::WtUnhookCode(GetDCHook, (void**) & GetDCHookNext);
			GetDCHookNext = nullptr;
		}

		if (ReleaseDCHookNext)
		{
			_ASSERTE(!::WtIsHookInUse((PVOID*)&ReleaseDCHookNext));
			::WtUnhookCode(ReleaseDCHook, (void**) & ReleaseDCHookNext);
			ReleaseDCHookNext = nullptr;
		}
	}

  protected:
	std::map<std::string, CColourizedControlPtr (*)(HWND hwnd), iless> classes;
	CCriticalSection cs;
	std::unordered_map<HWND, CColourizedControlPtr> controls;
	std::unordered_map<WNDPROC, std::tuple<std::list<CColourizedControlPtr>, WNDPROC, trampoline_t>> patches_wndproc;
	std::unordered_map<CColourizedControlPtr, WNDPROC> patches_wndproc2;
	std::list<CColourizedControlPtr> patches_getdc;
	std::list<CColourizedControlPtr> patches_releasedc;

	HHOOK cwphook = nullptr;

	void UpdatePatches(CColourizedControlPtr cc, bool patch)
	{
		__lock(cs);
		if (cc->WhatToPatch() & CColourizedControl::patch_wndproc)
		{
			if (patch)
			{
				USES_CONVERSION;
				WNDPROC wp = nullptr;
				if (::IsWindowUnicode(cc->_hwnd))
				{
					WNDCLASSW wc;
					memset(&wc, 0, sizeof(wc));
					if (::GetClassInfoW(NULL, A2CW(cc->GetClassName()), &wc))
						wp = wc.lpfnWndProc;
				}
				else
				{
					WNDCLASSA wc;
					memset(&wc, 0, sizeof(wc));
					if (::GetClassInfoA(NULL, cc->GetClassName(), &wc))
						wp = wc.lpfnWndProc;
				}

				_ASSERTE(wp);
				if (wp)
				{
					static constexpr uintptr_t specialhandle_mask = (uintptr_t)(void*)~0xffffull;
					if (specialhandle_mask == (uintptr_t(wp) & specialhandle_mask))
					{
						// [case: 81645]
						// server side/kernel WNDPROC handle that we can't thunk since it must
						// be called via CallWindowProc rather than via direct execution.

						// "WNDPROC is actually either the address of a window or dialog box procedure,
						// or a special internal value meaningful only to CallWindowProc."
						// http://msdn.microsoft.com/en-us/library/windows/desktop/ms633571%28v=vs.85%29.aspx

						// The 0xff* value is an implementation detail that could change in the future
						// and cause us new problems.

						vLog("ERROR: CColourizer::UpdatePatches skipping wp %p", wp);
						return;
					}
					else
					{
						auto& [controls2, wndproc, t] =
						    patches_wndproc[wp]; // std::tuple<std::list<CColourizedControlPtr>, WNDPROC, trampoline_t>
						mySetProp(cc->_hwnd, "__VA_patched_wndproc", wp);
						if (controls2.empty())
						{
							// [case: 81645]
							// This code assumes that wp is the address of a WNDPROC.
							// However, it might be HANDLE to a WNDPROC (the if condition above).
							// This should be updated to exec wp via CallWindowProc rather than exec it.
							// Then the 0xff* check can be removed.

							t = alloc_trampoline(wp);
							if (!t)
							{
								// [case: 108626]
								vLog("ERROR: CColourizer::UpdatePatches get_trampoline fail");
								_ASSERTE(!"CColourizer::UpdatePatches get_trampoline fail");
								return;
							}

							::WtHookCode(wp, t, (void**)&wndproc);
							change_trampoline_ptr(
							    t,
							    wndproc); // to call original WNDPROC, we have to give thunk which madCodeHook returns!
						}
						controls2.push_back(cc);
						patches_wndproc2[cc] = wndproc;
					}
				}
			}
			else
			{
				WNDPROC origwndproc = (WNDPROC)::myGetProp(cc->_hwnd, "__VA_patched_wndproc");
				if (origwndproc)
				{
					// [case: 84223] Manually remove the __VA_patched_wndproc prop.
					// Props are normally automatically cleared in a WNDPROC hook during WM_NCDESTROY,
					// but __VA_patched_wndproc is left so that this WNDPROC hook can pull the patched
					// wndproc (either before or after the prop WNDPROC hook).
					::myRemoveProp(cc->_hwnd, "__VA_patched_wndproc");
					auto it = patches_wndproc.find(origwndproc);
					if (it != patches_wndproc.end())
					{
						auto& [controls2, wndproc, t] =
						    it->second; // std::tuple<std::list<CColourizedControlPtr>, WNDPROC, trampoline_t>

						auto it2 = std::find(controls2.begin(), controls2.end(), cc);
						if (it2 != controls2.end())
						{
							patches_wndproc2.erase(cc);
							controls2.erase(it2);
							if (controls2.empty())
							{
								::WtUnhookCode(t, (void**)&wndproc);
								free_trampoline(t);
								wndproc = nullptr;
								t = nullptr;
							}
						}
					}
				}
			}
		}

		if (cc->WhatToPatch() & CColourizedControl::patch_getdc)
		{
			if (patch)
			{
				if (!patches_getdc.size() && !GetDCHookNext)
					::WtHookCode(GetProcAddress(GetModuleHandleA("user32.dll"), "GetDC"), GetDCHook,
					             (void**)&GetDCHookNext);
				patches_getdc.push_back(cc);
			}
			else
			{
				std::list<CColourizedControlPtr>::iterator it = find(patches_getdc.begin(), patches_getdc.end(), cc);
				if (it != patches_getdc.end())
					patches_getdc.erase(it);
			}
		}

		if (cc->WhatToPatch() & CColourizedControl::patch_releasedc)
		{
			if (patch)
			{
				if (!patches_releasedc.size() && !ReleaseDCHookNext)
					::WtHookCode(GetProcAddress(GetModuleHandleA("user32.dll"), "ReleaseDC"),
					             ReleaseDCHook, (void**)&ReleaseDCHookNext);
				patches_releasedc.push_back(cc);
			}
			else
			{
				std::list<CColourizedControlPtr>::iterator it =
				    find(patches_releasedc.begin(), patches_releasedc.end(), cc);
				if (it != patches_releasedc.end())
					patches_releasedc.erase(it);
			}
		}
	}

	static LRESULT CALLBACK CallWndProc(int code, WPARAM wparam, LPARAM lparam)
	{
		return colourizer->CallWndProc2(code, wparam, lparam);
	}

	LRESULT CallWndProc2(int code, WPARAM wparam, LPARAM lparam)
	{
		if (code >= 0)
		{
			CWPSTRUCT& cwp = *(CWPSTRUCT*)lparam;
			// [case: 84223] WM_NCDESTROY instead of WM_DESTROY
			if (cwp.message == WM_NCDESTROY)
			{
				__lock(cs);
				std::unordered_map<HWND, CColourizedControlPtr>::iterator it = controls.find(cwp.hwnd);
				if (it != controls.end())
				{
					CColourizedControlPtr cc = it->second;
					controls.erase(it);
					if (cc)
						UpdatePatches(cc, false);
				}
			}
		}

		if (cwphook)
			return ::CallNextHookEx(cwphook, code, wparam, lparam);
		return 0;
	}

	HDC(WINAPI* GetDCHookNext)(HWND hwnd);
	static HDC WINAPI GetDCHook(HWND hwnd)
	{
		return colourizer->GetDCHook2(hwnd);
	}
	HDC WINAPI GetDCHook2(HWND hwnd)
	{
		if (g_mainThread == GetCurrentThreadId())
		{
			__lock(cs);
			if (controls.contains(hwnd) && contains(patches_getdc, controls[hwnd]))
				return controls[hwnd]->GetDCHook(hwnd, GetDCHookNext);
		}
		if (GetDCHookNext)
			return GetDCHookNext(hwnd);
		return nullptr;
	}

	int(WINAPI* ReleaseDCHookNext)(HWND hwnd, HDC hdc);
	static int WINAPI ReleaseDCHook(HWND hwnd, HDC hdc)
	{
		return colourizer->ReleaseDCHook2(hwnd, hdc);
	}
	int WINAPI ReleaseDCHook2(HWND hwnd, HDC hdc)
	{
		if (g_mainThread == GetCurrentThreadId())
		{
			__lock(cs);
			if (controls.contains(hwnd) && contains(patches_releasedc, controls[hwnd]))
				return controls[hwnd]->ReleaseDCHook(hwnd, hdc, ReleaseDCHookNext);
		}
		if (ReleaseDCHookNext)
			return ReleaseDCHookNext(hwnd, hdc);
		return 0;
	}

	LRESULT CALLBACK ControlWndProc2(trampoline_t orig_wndproc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		__lock(cs);
		if (controls.contains(hwnd) /*&& patches_wndproc2.contains(controls[hwnd])*/)
		{
			auto cc = controls[hwnd];
			if (cc)
			{
				auto proc2 = patches_wndproc2[cc];
				if (proc2)
					return cc->ControlWndProc(hwnd, msg, wparam, lparam, proc2);
			}
		}
		return orig_wndproc(hwnd, msg, wparam, lparam);
	}

	friend void RegisterColourizedControlClass(const char* classname, CColourizedControlPtr (*Instance)(HWND hwnd));
	friend LRESULT CALLBACK ControlWndProc(trampoline_t orig_wndproc, HWND hwnd, UINT msg, WPARAM wparam,
	                                       LPARAM lparam);
};

LRESULT CALLBACK ControlWndProc(trampoline_t orig_wndproc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	return colourizer->ControlWndProc2(orig_wndproc, hwnd, msg, wparam, lparam);
}

void RegisterColourizedControlClass(const char* classname, CColourizedControlPtr (*Instance)(HWND hwnd))
{
	_ASSERTE(classname);
	_ASSERTE(Instance);
	if (!classname || !Instance)
		return;

	colourizer->classes[classname] = Instance;
}

CColourizedControlPtr ColourizeControl(CWnd* parent, int id)
{
	_ASSERTE(parent);
	_ASSERTE(id);
	if (!parent || !id)
		return nullptr;

	CWnd* control = parent->GetDlgItem(id);
	return ColourizeControl(control);
}

CColourizedControlPtr ColourizeControl(CWnd* control)
{
	if (!control || !control->m_hWnd)
	{
		_ASSERTE(control->GetSafeHwnd());
		return nullptr;
	}

	return colourizer->ColourizeControl(control->m_hWnd);
}

void ReleaseColourizerHooks()
{
	colourizer->ReleaseHooks();
}
