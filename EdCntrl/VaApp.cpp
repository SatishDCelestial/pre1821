#include "StdAfxEd.h"
#include <atlbase.h>
#include "VaApp.h"
#include "DevShellAttributes.h"
#include "log.h"
#include "ParseThrd.h"
#include "WrapCheck.h"
#include "VASeException\VASeException.h"
#include "RegKeys.h"
#include "Registry.h"
#include "DpiCookbook\VsUIDpiAwareness.h"
#include "DllNames.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void PatchW32Functions(bool patch);
void CleanupHelp();
void DeleteWnds();

VaApp* gVaDllApp = nullptr;

#if !defined(VA_CPPUNIT)
class CVaAtlModule : public CAtlDllModuleT<CVaAtlModule>
{
  public:
	CVaAtlModule()
	{
	}
};

CVaAtlModule _AtlModule;
#endif // VA_CPPUNIT

void VaApp::Start()
{
	gVaDllApp = this;
	mVaDllLoadAddress = GetModuleHandleW(IDS_VAX_DLLW);
#ifdef _DEBUG
	int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	flags |= // _CRTDBG_CHECK_ALWAYS_DF |		// use with extreme caution - very slow
	         // _CRTDBG_DELAY_FREE_MEM_DF |		// use with caution - free/delete doesn't actually release memory
	    _CRTDBG_LEAK_CHECK_DF;
	_CrtSetDbgFlag(flags);
	m_memStart.Checkpoint();
#endif

	_ASSERTE(!CAtlBaseModule::m_bInitFailed);
	::InitShell();
	WrapperCheck::Init();
	VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled(); // [case: 137527] init per monitor DPI awareness helper

	// I tried moving this to after InitLicense in ComSetup
	// but coloring of editor failed during debugging...
	if (gShellAttr->RequiresWin32ApiPatching())
		::PatchW32Functions(true); // Make sure font settings are initialized first.

	::VaSetCrtErrorHandlers();

#if !defined(VA_CPPUNIT)
	constexpr DWORD unsetConcur = (DWORD)-3;
	CString regKey = 
#if defined(RAD_STUDIO)
	    ID_RK_APP
#else
	    ID_RK_WT_KEY
#endif
		;
	DWORD maxConcur = ::GetRegDword(HKEY_CURRENT_USER, regKey, "MaxConcurrency", unsetConcur);
	if (0 > (int)maxConcur)
	{
		// [case: 142140]
		SYSTEM_INFO sysInf;
		memset(&sysInf, 0, sizeof(sysInf));
		typedef void(WINAPI * GetSystemInfoFUNC)(LPSYSTEM_INFO lpSystemInfo);
		GetSystemInfoFUNC myGetSystemInfo = (GetSystemInfoFUNC)(uintptr_t)GetProcAddress(
		    GetModuleHandleW(L"kernel32.dll"), "GetNativeSystemInfo");
		if (!myGetSystemInfo)
			myGetSystemInfo = ::GetSystemInfo;
		myGetSystemInfo(&sysInf);
		if (sysInf.dwNumberOfProcessors > 8)
		{
			if (8 < (sysInf.dwNumberOfProcessors / 2))
				maxConcur = sysInf.dwNumberOfProcessors / 2;
			else
				maxConcur = 8;
		}
		else
			maxConcur = 0;

		::SetRegValue(HKEY_CURRENT_USER, regKey, "MaxConcurrency", maxConcur);
	}

	const auto kThreadPriority = (int)::GetRegDword(HKEY_CURRENT_USER, regKey, "ConcurrentThreadPriority",
	                                                THREAD_PRIORITY_NORMAL); // [case: 105948]
	if (maxConcur || kThreadPriority)
	{
		try
		{
			if (maxConcur)
				Concurrency::Scheduler::SetDefaultSchedulerPolicy(
				    Concurrency::SchedulerPolicy(3, Concurrency::MinConcurrency, 1, Concurrency::MaxConcurrency,
				                                 maxConcur, Concurrency::ContextPriority, kThreadPriority));
			else
				Concurrency::Scheduler::SetDefaultSchedulerPolicy(
				    Concurrency::SchedulerPolicy(1, Concurrency::ContextPriority, kThreadPriority));
		}
		catch (const Concurrency::default_scheduler_exists&)
		{
			_ASSERTE(!"Failed to SetDefaultSchedulerPolicy");
		}
	}
#endif
}

void VaApp::Exit()
{
	for (int n = 100; g_threadCount && n; n--)
		::Sleep(100);

	::DeleteWnds();
	::CleanupHelp();

#ifdef RAD_STUDIO
	extern void VaRADStudioCleanup();
	VaRADStudioCleanup();
#endif

#if !defined(VA_CPPUNIT)
	_AtlModule.Term();
#endif

#ifdef _DEBUG
	::_heapmin();

	m_memEnd.Checkpoint();
	m_memDiff.Difference(m_memStart, m_memEnd);
	m_memDiff.DumpStatistics();
#endif

	VALOGMETHOD("Exit:");
	::UnInitLogFile();
	::UninitShell();
}

#if !defined(VA_CPPUNIT)
STDAPI DllCanUnloadNow(void)
{
	HRESULT res = _AtlModule.DllCanUnloadNow();
	return res;
}

// Returns a class factory to create an object of the requested type
// [case: 83748] specifically added for Concord debugger plugin implementation
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}
#endif
