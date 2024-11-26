#pragma once

#ifndef _ARM64
#include "../../../3rdParty/madCodeHook/madCodeHook3/madCHook.h"
#else
#include "../../../3rdParty/madCodeHook/Detours-4.0.1/src/detours.h"
#endif


inline BOOL WtHookCode(PVOID pCode, PVOID pCallbackFunc, PVOID* pNextHook, DWORD dwFlags = 0)
{
	_ASSERTE(pNextHook && !*pNextHook);
	if (!pNextHook || *pNextHook)
		return FALSE;

#ifndef _ARM64
	return HookCode(pCode, pCallbackFunc, pNextHook, dwFlags);
#else
	std::ignore = dwFlags;
	*pNextHook = pCode;
	BOOL ret = DetourTransactionBegin() == NO_ERROR;
	if(ret)
	{
		ret &= DetourAttach(pNextHook, pCallbackFunc) == NO_ERROR;
		if (!ret)
			DetourTransactionAbort();
		else
			ret &= DetourTransactionCommit() == NO_ERROR;
	}
	_ASSERTE(ret);
	return ret;
#endif
}

inline BOOL WtUnhookCode(PVOID pCallbackFunc, PVOID* pNextHook)
{
	_ASSERTE(pNextHook && *pNextHook);
	if (!pNextHook || !*pNextHook)
		return false;

#ifndef _ARM64
	std::ignore = pCallbackFunc;
	return UnhookCode(pNextHook);
#else
	BOOL ret = DetourTransactionBegin() == NO_ERROR;
	if(ret)
	{
		ret &= DetourDetach(pNextHook, pCallbackFunc) == NO_ERROR;
		if (!ret)
			DetourTransactionAbort();
		else
			ret &= DetourTransactionCommit() == NO_ERROR;

		if(ret)
			*pNextHook = nullptr;
	}
	_ASSERTE(ret);
	return ret;
#endif
}

inline DWORD WtIsHookInUse(PVOID* pNextHook)
{
#ifndef _ARM64
	return IsHookInUse(pNextHook);
#else
	std::ignore = pNextHook;
	return false;
#endif
}

#ifdef _ARM64
inline void InitializeMadCHook() {}
inline void FinalizeMadCHook() {}
#endif
