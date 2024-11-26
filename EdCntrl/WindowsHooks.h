#pragma once

#ifndef WindowsHooks_h__
#define WindowsHooks_h__

#include <afxmt.h>

#ifndef WH_HARDWARE
#define WH_HARDWARE 8
#endif

namespace WindowsHooks
{
//////////////////////////////////////////////////////////////
// base class for multiple hooking types
class CHook
{
	friend class CHookManager;
	UCHAR m_state[WH_MAX - WH_MIN + 1];

  public:
	bool BeginHook(int id_hook);
	bool IsHooking(int id_hook) const;
	void EndHook(int id_hook);
	void EndAllHooks();

  protected:
	virtual void OnHookMessage(int id_hook, int code, WPARAM wParam, LPVOID lpHookStruct) = 0;

  public:
	CHook();
	virtual ~CHook();
};

//////////////////////////////////////////////////////////////
// class template for single hooking type
// defined by WH_ constant and type of passed structure
template <int HOOK_TYPE, typename HOOK_STRUCT> class CSimpleHook : protected CHook
{
  public:
	bool IsHooking() const
	{
		return __super::IsHooking(HOOK_TYPE);
	}
	bool BeginHook()
	{
		return __super::BeginHook(HOOK_TYPE);
	}
	void EndHook()
	{
		__super::EndHook(HOOK_TYPE);
	}

  protected:
	virtual void OnHookMessage(int id_hook, int code, WPARAM wParam, LPVOID lpHookStruct)
	{
		_ASSERTE(id_hook == HOOK_TYPE);
		if (code >= 0 && lpHookStruct)
			OnHookMsg(wParam, (HOOK_STRUCT*)lpHookStruct);
	}

	virtual void OnHookMsg(WPARAM wParam, HOOK_STRUCT* lpMHS) = 0;

	CSimpleHook()
	{
	}
	virtual ~CSimpleHook()
	{
		EndHook();
	}
};

//////////////////////////////////////////////////////////////
// same as would be CSimpleHook<WH_MOUSE, MOUSEHOOKSTRUCT>
// the difference is in more meaningful method names
class CMouseHook : public CHook
{
  protected:
	bool BeginMouseHooking()
	{
		return BeginHook(WH_MOUSE);
	}
	void EndMouseHooking()
	{
		EndHook(WH_MOUSE);
	}

	virtual void OnHookMessage(int id_hook, int code, WPARAM wParam, LPVOID lpHookStruct)
	{
		(void)id_hook;
		_ASSERTE(id_hook == WH_MOUSE);
		if (code >= 0 && lpHookStruct)
			OnMouseHookMessage(code, (UINT)wParam, (MOUSEHOOKSTRUCT*)lpHookStruct);
	}

	virtual void OnMouseHookMessage(int code, UINT message, MOUSEHOOKSTRUCT* lpMHS) = 0;

	virtual ~CMouseHook()
	{
		EndMouseHooking();
	}
};

} // namespace WindowsHooks

#endif // WindowsHooks_h__
