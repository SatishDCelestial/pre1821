#include "stdafx.h"
#pragma warning(push)
#pragma warning(disable : 5054)
#include "msdbg.h"
#pragma warning(pop)
#include "interfaces.h"
#ifdef _WIN64
#include "..\VaManagedComLib\VaManagedComLib64_h.h"
#else
#include "..\VaManagedComLib\VaManagedComLib_h.h"
#endif
#include "Settings.h"
#include "DevShellAttributes.h"
#include "WindowUtils.h"
#include "DebuggerToolsCpp.h"
#include "TraceWindowFrame.h"
#include "PROJECT.H"
#include "FileVerInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool gDebugEnginePatchesActive = false;

inline bool IsHandleNull(HANDLE h)
{
	return (h == nullptr) || (h == INVALID_HANDLE_VALUE);
}

template <typename TYPE> inline bool is_readable(const TYPE& value)
{
	return !::IsBadReadPtr(&(typename std::remove_volatile<TYPE>::type&)value, sizeof(value));
}

class DebugEnginePatchesBase
{
  public:
	virtual ~DebugEnginePatchesBase()
	{
	}
	virtual bool InstallPatches() = 0;

  protected:
	bool installed = false;
	bool givenup = false;
};

class VS2013NativePatches : public DebugEnginePatchesBase
{
  public:
	VS2013NativePatches()
	{
		InstallPatches();
	}

	virtual ~VS2013NativePatches()
	{
		UninstallPatches();
	}

	void UninstallPatches()
	{
		if (installed)
		{
			// do not actually uninstall the patches as the dll may already be unloaded
			origCppEE__CStepFilter__GetStepAction = nullptr;
			// do not touch origCppEE__CStepFilter__GetStepAction_PreviousDebugSession
			installed = false;
		}

		cppdebug_dll = nullptr;
		DllGetClassObject = nullptr;
	}

	virtual bool InstallPatches()
	{
		if (installed)
			return true;
		else if (givenup)
			return false;

		// ?ClassId@CStepFilterContract@CppEE@@2U_GUID@@B
		static const GUID CStepFilterContract_GUID{
		    0xa5c10647, 0x03c1, 0x4688, {0xad, 0xa0, 0xe6, 0xd5, 0x0a, 0x77, 0x82, 0x13}};
		// ?ImplementedInterfaces@?3??_InternalQueryInterface@CStepFilterContract@CppEE@@IAGJABU_GUID@@PAPAX@Z@4QBU4@B
		static const GUID CStepFilter_GUID = {
		    0xa70278a3, 0xf362, 0x06f0, {0x2b, 0xf6, 0xa4, 0xa5, 0x78, 0x60, 0xcf, 0xd3}};

		if (IsHandleNull(cppdebug_dll))
		{
			cppdebug_dll = ::GetModuleHandleW(L"cppdebug.dll");
			if (IsHandleNull(cppdebug_dll))
				return false;
		}

		if (!DllGetClassObject)
		{
			DllGetClassObject = (DllGetClassObjectFUNC)(intptr_t)::GetProcAddress(cppdebug_dll, "DllGetClassObject");
			if (!DllGetClassObject)
				return false;
		}

		CComPtr<IClassFactory> cf;
		if (FAILED(DllGetClassObject(CStepFilterContract_GUID, IID_IClassFactory, (void**)&cf.p)))
			return false;

		IStepFilter* sf = nullptr;
		if (FAILED(cf->CreateInstance(nullptr, CStepFilter_GUID, (void**)&sf)))
			return false;

		if (!is_readable(sf->lpVtbl))
			return false;
		if (!is_readable(sf->lpVtbl->GetStepAction))
			return false;

		auto& CppEE__CStepFilter__GetStepAction = (CppEE__CStepFilter__GetStepActionFUNC&)sf->lpVtbl->GetStepAction;
		if ((void*)myCppEE__CStepFilter__GetStepAction != (void*)CppEE__CStepFilter__GetStepAction)
		{
			DWORD old = 0;
			::VirtualProtect(&CppEE__CStepFilter__GetStepAction, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
			::MemoryBarrier();
			origCppEE__CStepFilter__GetStepAction = origCppEE__CStepFilter__GetStepAction_PreviousDebugSession =
			    CppEE__CStepFilter__GetStepAction;
			CppEE__CStepFilter__GetStepAction = myCppEE__CStepFilter__GetStepAction;
			::MemoryBarrier();
			::VirtualProtect(&CppEE__CStepFilter__GetStepAction, sizeof(void*), old, &old);
		}
		else
		{
			// patch set up in previous session
			origCppEE__CStepFilter__GetStepAction = origCppEE__CStepFilter__GetStepAction_PreviousDebugSession;
		}

		sf->lpVtbl->Release(sf);

		installed = true;
		return true;
	}

  protected:
	typedef bool(__stdcall* CppEE__CStepFilter__GetStepActionFUNC)(IExprProcess* _this,
	                                                               const ATL__CStringW& str_process_name,
	                                                               const ATL__CStringW& str_function_name,
	                                                               IStepFilter::StepAction& action);
	static CppEE__CStepFilter__GetStepActionFUNC origCppEE__CStepFilter__GetStepAction;
	static CppEE__CStepFilter__GetStepActionFUNC origCppEE__CStepFilter__GetStepAction_PreviousDebugSession;
	static bool __stdcall myCppEE__CStepFilter__GetStepAction(IExprProcess* _this,
	                                                          const ATL__CStringW& str_process_name,
	                                                          const ATL__CStringW& str_function_name,
	                                                          IStepFilter::StepAction& out_action)
	{
		if (!origCppEE__CStepFilter__GetStepAction)
		{
			// patch no longer installed or not verified yet
			out_action = IStepFilter::StepAction::dont_skip;
			return 0;
		}

		DWORD ret = 0;

		__try
		{
			// [case: 83522] catch bad deref via SEH
			const wchar_t* s = str_function_name.s;
			if (s)
				ret = ::IsStepIntoSkipped(s);
		}
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
		                                                           : EXCEPTION_CONTINUE_SEARCH)
		{
			return origCppEE__CStepFilter__GetStepAction(_this, str_process_name, str_function_name, out_action);
		}

		if (ret)
		{
			out_action = LOWORD(ret) ? IStepFilter::StepAction::skip : IStepFilter::StepAction::dont_skip;
			return 0;
		}
		else
			return origCppEE__CStepFilter__GetStepAction(_this, str_process_name, str_function_name, out_action);

		// 		if(!gDebugEnginePatchesActive)
		// 			return origCppEE__CStepFilter__GetStepAction(_this, str_process_name, str_function_name,
		// out_action);
		//
		// 		wchar_t temp[4096];
		// 		swprintf_s(temp, L"1: %s\n\n2: %s", str_process_name.s, str_function_name.s);
		// 		switch(::MessageBox(nullptr, temp, L"skip?", MB_SYSTEMMODAL | MB_ICONQUESTION | MB_YESNOCANCEL))
		// 		{
		// 		case IDYES:
		// 			out_action = IStepFilter::StepAction::skip;
		// 			return 0;
		// 		case IDNO:
		// 			out_action = IStepFilter::StepAction::dont_skip;
		// 			return 0;
		// 		case IDCANCEL:
		// 		default:
		// 			return origCppEE__CStepFilter__GetStepAction(_this, str_process_name, str_function_name,
		// out_action);
		// 		}
	}

	HMODULE cppdebug_dll = nullptr;
	DllGetClassObjectFUNC DllGetClassObject = nullptr;
};

VS2013NativePatches::CppEE__CStepFilter__GetStepActionFUNC VS2013NativePatches::origCppEE__CStepFilter__GetStepAction =
    nullptr;
VS2013NativePatches::CppEE__CStepFilter__GetStepActionFUNC
    VS2013NativePatches::origCppEE__CStepFilter__GetStepAction_PreviousDebugSession = nullptr;

class VS2010_2013MixedPatches : public DebugEnginePatchesBase
{
  public:
	VS2010_2013MixedPatches()
	{
		InstallPatches();
	}

	virtual ~VS2010_2013MixedPatches()
	{
		UninstallPatches();
	}

	void UninstallPatches()
	{
		if (installed)
		{
			// do not actually uninstall the patches as the dll may already be unloaded
			origCDebugProgram__IsNoStepInto = nullptr;
			// do not touch origCDebugProgram__IsNoStepInto_PreviousDebugSession
			installed = false;
		}

		nativede.Release();
	}

	virtual bool InstallPatches()
	{
		if (installed)
			return true;
		else if (givenup)
			return false;

		// NatDbgDE.dll
		static const GUID CLSID_VsApartmentLoader = {
		    0xCA554A15, 0x4410, 0x45C9, {0xB5, 0xC1, 0x20, 0xDE, 0x05, 0x2D, 0x9C, 0xD3}};
		static const GUID CLSID_FreeNativeOnlyEngine = {
		    0x3B476D36, 0x0A401, 0x11D2, {0xAA, 0xD4, 0x00, 0xC0, 0x4F, 0x99, 0x01, 0x71}};

		/*//	const wchar_t* pNativeEngineKey =
		   L"Software\\Microsoft\\VisualStudio\\10.0\\CLSID\\{3B476D36-A401-11D2-AAD4-00C04F990171}"; const wchar_t*
		   pNativeEngineKey =
		   L"Software\\Microsoft\\VisualStudio\\12.0Exp_Config\\CLSID\\{3B476D36-A401-11D2-AAD4-00C04F990171}"; CRegKey
		   key; WCHAR modulename[256]; DWORD n(255);
		        //	key.Open(HKEY_LOCAL_MACHINE, pNativeEngineKey);
		        key.Open(HKEY_CURRENT_USER, pNativeEngineKey);
		        key.QueryStringValue(L"InprocServer32", modulename, &n);*/

		if (!nativede)
		{
			wchar_t modulename[512];
			modulename[0] = 0;
			::GetModuleFileNameW(nullptr, modulename, _countof(modulename));
			::PathRemoveFileSpecW(modulename);
			::PathAppendW(modulename, L"..\\Packages\\Debugger\\NatDbgDE.dll");
			wchar_t modulename2[512];
			modulename2[0] = 0;
			::PathCanonicalizeW(modulename2, modulename);
			if (!::PathFileExistsW(modulename2))
			{
				givenup = true;
				return false;
			}

			CComPtr<IVsLoader> loader;
			if (FAILED(loader.CoCreateInstance(CLSID_VsApartmentLoader)))
				return false;
			CComPtr<IUnknown> nativede_unk;
			if (FAILED(loader->Load(modulename2, &CLSID_FreeNativeOnlyEngine, 0, CLSCTX_ALL, &nativede_unk)))
				return false;

			nativede = nativede_unk;
			if (!nativede)
			{
				givenup = true;
				return false;
			}
		}

		CComPtr<IEnumDebugPrograms2> pEnumPrograms;
		if (FAILED(nativede->EnumPrograms(&pEnumPrograms)))
			return false;
		ULONG cnt = UINT_MAX;
		if (FAILED(pEnumPrograms->GetCount(&cnt)))
			return false;
		if (cnt <= 0)
			return false;
		CComPtr<IDebugProgram2> dp2;
		ULONG fetched = UINT_MAX;
		if (FAILED(pEnumPrograms->Next(1, &dp2, &fetched)))
			return false;

		void*** dp2_IExprProcess = (void***)(dp2.p + 2);
		if (!is_readable(*dp2_IExprProcess))
			return false;
		void** dp2_IExprProcess_vtbl = *dp2_IExprProcess;

		static int sNoStepFuncVtableOffset = 19;
		if (gShellAttr->IsDevenv10())
		{
			// [case: 83522]
			static bool once = true;
			if (once)
			{
				FileVersionInfo fvi;
				if (fvi.QueryFile(L"Devenv.exe", FALSE))
				{
					once = false;
					_ASSERTE(fvi.GetFileVerMSHi() == 10);
					_ASSERTE(fvi.GetFileVerMSLo() == 0);
					if (fvi.GetFileVerLSHi() < 40219)
						sNoStepFuncVtableOffset = 18;
				}
				else
					return false;
			}
		}

		volatile auto& CDebugProgram__IsNoStepInto =
		    (CDebugProgram__IsNoStepIntoFUNC&)dp2_IExprProcess_vtbl[sNoStepFuncVtableOffset];
		if (!is_readable(CDebugProgram__IsNoStepInto))
			return false;

		if ((void*)myCDebugProgram__IsNoStepInto != (void*)CDebugProgram__IsNoStepInto)
		{
			DWORD old = 0;
			::VirtualProtect((void*)&CDebugProgram__IsNoStepInto, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
			::MemoryBarrier();
			origCDebugProgram__IsNoStepInto = origCDebugProgram__IsNoStepInto_PreviousDebugSession =
			    CDebugProgram__IsNoStepInto;
			CDebugProgram__IsNoStepInto = myCDebugProgram__IsNoStepInto;
			::MemoryBarrier();
			::VirtualProtect((void*)&CDebugProgram__IsNoStepInto, sizeof(void*), old, &old);
		}
		else
		{
			// patch set up in previous session
			origCDebugProgram__IsNoStepInto = origCDebugProgram__IsNoStepInto_PreviousDebugSession;
		}

		installed = true;
		return true;
	}

  protected:
	// Callstack at the point stepinto flags are processed. GetStepAction is the first function with function name
	// string. cppdebug.dll!CppEE::CStepFilter::GetStepAction(class ATL::CStringT<unsigned short, class
	// ATL::StrTraitATL<unsigned short, class ATL::ChTraitsCRT<unsigned short> > > const &, class ATL::CStringT<unsigned
	// short, class ATL::StrTraitATL<unsigned short, class ATL::ChTraitsCRT<unsigned short> > > const &, enum
	// CppEE::CStepFilter::StepAction &)	Unknown cppdebug.dll!CppEE::CStepFilter::GetStepIntoFlags(class
	// Microsoft::VisualStudio::Debugger::Evaluation::DkmLanguageInstructionAddress *, enum
	// Microsoft::VisualStudio::Debugger::Stepping::DkmLanguageStepIntoFlags::e *)	Unknown
	// vsdebugeng.dll!dispatcher::Evaluation::DkmLanguageInstructionAddress::GetStepIntoFlags(enum
	// dispatcher::Stepping::DkmLanguageStepIntoFlags::e *)	Unknown
	// vsdebugeng.impl.dll!SymProvider::CNativeSymModule::GetStepFilterFlags(class
	// Microsoft::VisualStudio::Debugger::Native::DkmNativeInstructionSymbol *, class
	// Microsoft::VisualStudio::Debugger::DkmInstructionAddress *, enum
	// Microsoft::VisualStudio::Debugger::Stepping::DkmLanguageStepIntoFlags::e *)	Unknown
	typedef bool(__fastcall* CDebugProgram__IsNoStepIntoFUNC)(IExprProcess* _this, int dummy, const dbg__CStringW& str);

	static CDebugProgram__IsNoStepIntoFUNC origCDebugProgram__IsNoStepInto;
	static CDebugProgram__IsNoStepIntoFUNC origCDebugProgram__IsNoStepInto_PreviousDebugSession;
	static bool __fastcall myCDebugProgram__IsNoStepInto(IExprProcess* _this, int dummy, const dbg__CStringW& str)
	{
		if (!origCDebugProgram__IsNoStepInto)
		{
			// patch no longer installed or not verified yet
			return 0;
		}

		DWORD ret = 0;

		__try
		{
			// [case: 83522] catch bad deref via SEH
			const wchar_t* s = str.s;
			if (s)
				ret = ::IsStepIntoSkipped(s);
		}
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
		                                                           : EXCEPTION_CONTINUE_SEARCH)
		{
			return origCDebugProgram__IsNoStepInto(_this, dummy, str);
		}

		if (ret)
			return !!(LOWORD(ret));
		else
			return origCDebugProgram__IsNoStepInto(_this, dummy, str);

		// 		if(!gDebugEnginePatchesActive)
		// 			return origCDebugProgram__IsNoStepInto(_this, dummy, str);
		//
		// 		switch(::MessageBox(nullptr, str.s, L"skip?", MB_SYSTEMMODAL | MB_ICONQUESTION | MB_YESNOCANCEL))
		// 		{
		// 		case IDYES:
		// 			return true;
		// 		case IDNO:
		// 			return false;
		// 		case IDCANCEL:
		// 		default:
		// 			return origCDebugProgram__IsNoStepInto(_this, dummy, str);
		// 			break;
		// 		}
	}

	CComQIPtr<IDebugEngine2> nativede;
};

VS2010_2013MixedPatches::CDebugProgram__IsNoStepIntoFUNC VS2010_2013MixedPatches::origCDebugProgram__IsNoStepInto =
    nullptr;
VS2010_2013MixedPatches::CDebugProgram__IsNoStepIntoFUNC
    VS2010_2013MixedPatches::origCDebugProgram__IsNoStepInto_PreviousDebugSession = nullptr;

// CComAutoCriticalSection natvis_append_cs;
// std::wstring natvis_append_text;
// int natvis_append_new = 0;
// int natvis_append_last = -1;
//
// void UpdateNatStepFilter(const wchar_t *definitions)
// {
// 	if(!definitions)
// 		return;
//
// 	CComCritSecLock<CComAutoCriticalSection> lock(natvis_append_cs);
// 	std::wstring new_text = definitions;
// 	if(natvis_append_text != new_text)
// 	{
// 		//		::MessageBoxW(nullptr, new_text.c_str(), L"UpdateNatStepFilter", MB_OK | MB_SYSTEMMODAL);
//
// 		natvis_append_text = new_text;
// 		natvis_append_new++;
// 	}
// }

typedef std::tuple<std::wstring, bool, std::wregex> StepFilterEntry;
typedef std::vector<StepFilterEntry> StepFilterEntries;
StepFilterEntries gStepFilterDefinitions;
CComAutoCriticalSection gStepFilterDefinitionsLock;

DWORD
IsStepIntoSkipped(const wchar_t* functionnameIn)
{
	_ASSERTE(functionnameIn);
	if (!Psettings || !Psettings->m_enableVA || !Psettings->mEnableDebuggerStepFilter)
		return 0;

	// [case: 83522]
	// avoid use of IsBadStringPtrW
	// hope this assign causes AV caught by SEH in caller if str is invalid
	const std::wstring functionname(functionnameIn);

	CComCritSecLock<CComAutoCriticalSection> l(gStepFilterDefinitionsLock);
	for (const auto& d : gStepFilterDefinitions)
	{
		try
		{
			if (std::regex_match(functionname, std::get<2>(d)))
				return 0xf0000000 | (std::get<1>(d) ? 1 : 0);
		}
		catch (const std::regex_error&)
		{
#ifdef _DEBUG
			::MessageBoxW(nullptr, L"Regex match failed!", L"VA Error", MB_APPLMODAL | MB_ICONERROR | MB_OK);
#endif // _DEBUG
		}
	}

	return 0;
}

const IID IID_IVaDebuggerToolService = {0x97e3c51c, 0x046d, 0x4a4a, {0xb5, 0x49, 0xef, 0xdf, 0x52, 0x50, 0x98, 0xc0}};

class VaDebuggerService : public IVaDebuggerToolService
{
  public:
	virtual ~VaDebuggerService()
	{
		gDebugEnginePatchesActive = false;
	}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
	{
		if (riid == IID_IUnknown)
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		else if (riid == IID_IVaDebuggerToolService)
		{
			*ppvObject = static_cast<IVaDebuggerToolService*>(this);
			AddRef();
			return S_OK;
		}

#if 0
		// c3fcc19e-a970-11d2-8b5a-00a0c9b7c9c4 IManagedObject
		// b196b283-bab4-101a-b69c-00aa00341d07 IProvideClassInfo
		// af86e2e0-b12d-4c6a-9c5a-d7aa65101e90 IInspectable
		// ecc8691b-c1db-4dc0-855e-65f6c551af49 INoMarshal
		// 94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90 IAgileObject
		// 00000003-0000-0000-c000-000000000046 IMarshal
		// 00000144-0000-0000-c000-000000000046	IRpcOptions
		CStringW msg;
		msg.Format(L"sean QI E_NOINTERFACE: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", 
			riid.Data1, riid.Data2, riid.Data3, 
			riid.Data4[0], riid.Data4[1], 
			riid.Data4[2], riid.Data4[3], riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
		OutputDebugStringW(msg);
#endif

		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return (ULONG)InterlockedIncrement(&mRefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		const LONG cRef = InterlockedDecrement(&mRefCount);
		if (cRef == 0)
		{
			if (gShellAttr->IsDevenv16OrHigher())
			{
				// [case: 141881] [case: 142074]
				// deliberate leak due to not understood RCW/COM/GC behavior
				// in vs2019 and no steps to repro 16.5 crash reported by user
				// and difficult to repro 16.6 crash after full ast run
			}
			else
				delete this;
		}
		return (ULONG)cRef;
	}

	virtual void STDMETHODCALLTYPE InstallPatches(void)
	{
		if (!mDebugEnginePatches.size())
		{
			mDebugEnginePatches.emplace_back(std::make_shared<VS2013NativePatches>());
			mDebugEnginePatches.emplace_back(std::make_shared<VS2010_2013MixedPatches>());
			gDebugEnginePatchesActive = true;
		}
		else if (StepFilterIsEnabled())
		{
			for (auto& it : mDebugEnginePatches)
				it->InstallPatches();
		}
	}

	virtual void STDMETHODCALLTYPE BeginUpdateStepFilter(void)
	{
		if (!StepFilterIsEnabled())
			return;

		gStepFilterDefinitionsLock.Lock();
		gStepFilterDefinitions.clear();
	}

	// language is ignored for now
	virtual void STDMETHODCALLTYPE AddStepFilter(wchar_t* language, wchar_t* definition, int skip_enabled)
	{
		if (!StepFilterIsEnabled())
			return;

		if (!definition)
			return;

		// 	CStringW d = definition;
		// 	d.Replace(L"\\(", L"#@#");		// don't touch ( characters; temporarily rename
		// 	d.Replace(L"(", L"(?:");		// make all groups to be noncapture groups
		// 	d.Replace(L"#@#", L"\\(");		// restore back

		try
		{
			gStepFilterDefinitions.emplace_back(
			    definition, !!skip_enabled,
			    std::wregex(L"^" + std::wstring(definition) + L"$", std::regex_constants::ECMAScript |
			                                                            std::regex_constants::nosubs |
			                                                            std::regex_constants::optimize));
		}
		catch (const std::regex_error&)
		{
#ifdef _DEBUG
			::MessageBoxW(nullptr, L"Regex construction failed!\n\n" + (CStringW)definition, L"VA Error",
			              MB_APPLMODAL | MB_ICONERROR | MB_OK);
#endif // _DEBUG
		}
	}

	virtual void STDMETHODCALLTYPE EndUpdateStepFilter(void)
	{
		if (!StepFilterIsEnabled())
			return;

		gStepFilterDefinitionsLock.Unlock();
	}

	virtual int STDMETHODCALLTYPE StepFilterIsEnabled(void)
	{
		return Psettings && Psettings->m_enableVA && Psettings->mEnableDebuggerStepFilter;
	}

	virtual int STDMETHODCALLTYPE RoamingSolutionConfig()
	{
		return Psettings && Psettings->mRoamingStepFilterSolutionConfig;
	}

	virtual void STDMETHODCALLTYPE EndDebugSession()
	{
		gDebugEnginePatchesActive = false;
		mDebugEnginePatches.clear();

		::ClearDebuggerGlobals();
	}

	virtual int STDMETHODCALLTYPE VaIsEnabled(void)
	{
		return Psettings && Psettings->m_enableVA;
	}

	virtual void STDMETHODCALLTYPE DebuggerEvent(DebugEventType det, wchar_t* msg)
	{
#ifdef _DEBUGxx
		WTString txt;
		txt.WTFormat("DbgEvt: %d: ", det);
		txt += WTString(msg);
		TraceHelp t(txt);
#endif
	}

	virtual int STDMETHODCALLTYPE IsStepIntoSkipped(wchar_t* functionName) override
	{
		return (int)::IsStepIntoSkipped(functionName);
	}

	void Close()
	{
		mDebugEnginePatches.clear();
	}

  private:
	long mRefCount = 0;
	std::list<std::shared_ptr<DebugEnginePatchesBase>> mDebugEnginePatches;
};

VaDebuggerService* gVaDbgSvc = nullptr;

IVaDebuggerToolService* VaDebuggerToolService(int managed)
{
	if (!Psettings || !gShellAttr || gShellIsUnloading)
		return nullptr;

	if (!Psettings->m_enableVA)
		return nullptr;

	// #newVsVersion
	if (gShellAttr->IsDevenv18OrHigher())
	{
		_ASSERTE(!"need to test debug engine patches in new version of vs");
		return nullptr;
	}

	CComCritSecLock<CComAutoCriticalSection> l(gStepFilterDefinitionsLock);
	if (!gVaDbgSvc)
	{
		gVaDbgSvc = new VaDebuggerService();
		if (gVaDbgSvc)
		{
			// ref for the native global holder
			gVaDbgSvc->AddRef();
		}
	}

	if (gVaDbgSvc && managed)
	{
		// [case: 141881]
		// managed interface marshaler assumes AddRef()'d like QueryInterface() and DllGetClassObject()
		// https://docs.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-dllgetclassobject
		gVaDbgSvc->AddRef();
	}

	return gVaDbgSvc;
}

void ClearDebuggerGlobals(bool clearDbgService /*= false*/)
{
	CComCritSecLock<CComAutoCriticalSection> l(gStepFilterDefinitionsLock);
	gStepFilterDefinitions.clear();

	if (clearDbgService)
	{
		if (gVaDbgSvc)
		{
			gVaDbgSvc->Close();
			// release our global holder
			gVaDbgSvc->Release();
			gVaDbgSvc = nullptr;
		}
	}
}
