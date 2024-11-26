#include "stdafxed.h"
#include <delayimp.h>
#include <filesystem>
#include "DllNames.h"

// gmit: The problem is strange. VS don't seem to be able to load VA dependent dlls
//       that aren't in path (and VA extension dir isn't).
//       The solution is to declare dependent dll (vaIPC.dll in this initial case)
//       as delay load, install delay load library hook and when VA dll is going to
//       be loaded, prepend full path to it.
// sean: In general, Library::LoadFromVaDirectory is the way to load a dll from VA
//		 dir but requires use of GetProcAddress (no C++ symbol resolution).

// from MSDN:
// If a DLL has dependencies, the system searches for the dependent DLLs as if they
// were loaded with just their module names.This is true even if the first DLL was
// loaded by specifying a full path.

static FARPROC VADelayLoadLibrary(LPCSTR pszModuleName)
{
	if (!_strcmpi(pszModuleName, IDS_VAIPC_DLL))
	{
		HMODULE thisdll = nullptr;
		::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)VADelayLoadLibrary, &thisdll);
		assert(thisdll);
		if (thisdll)
		{
			wchar_t temp[4096];
			temp[0] = 0;
			::GetModuleFileNameW(thisdll, temp, _countof(temp));
			std::wstring dllpath = std::filesystem::path(temp).remove_filename().append(pszModuleName).wstring();

			auto ret = (FARPROC)::LoadLibraryW(dllpath.c_str());
			if (!ret)
			{
				// if failed, try by loading crt dependencies from va dir
				ret = (FARPROC)::LoadLibraryExW(dllpath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
			}
			assert(ret);
			return ret;
		}
	}
	return nullptr;
}
static FARPROC WINAPI myDliNotifyHook2(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary)
		return VADelayLoadLibrary(pdli->szDll);
	return nullptr;
}
extern "C" const PfnDliHook __pfnDliNotifyHook2 = myDliNotifyHook2;
