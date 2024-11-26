#pragma once

typedef HRESULT(STDAPICALLTYPE* DllGetClassObjectFUNC)(REFCLSID rclsid, REFIID riid, LPVOID FAR* ppv);

// used in old DE, VS2013/mixed mode, VS2010
MIDL_INTERFACE("10E4254C-6D73-4C38-B011-E0049B2E0A0F")
IVsLoader : public IDispatch
{
	STDMETHOD(Load)
	(BSTR strModule, const GUID* rclsid, IUnknown* UnkOuter, unsigned long dwClsContext, IUnknown** ppunk);
};

class IExprProcess;

struct dbg__CStringW
{
	const wchar_t* s;
};

// used in new DE, VS2013/native mode
struct ATL__CStringW
{
	const wchar_t* s;
};

class Microsoft__VisualStudio__Debugger__Evaluation__DkmLanguageInstructionAddress;
enum Microsoft__VisualStudio__Debugger__Stepping__DkmLanguageStepIntoFlags__e : uint32_t;
enum CppEE__CStepFilter__StepAction : uint32_t;

// cppdebug.dll
interface IStepFilter;
typedef struct IStepFilterVtbl
{
	BEGIN_INTERFACE
	HRESULT(STDMETHODCALLTYPE* QueryInterface)(IStepFilter* This, REFIID riid, void** ppvObject);
	ULONG(STDMETHODCALLTYPE* AddRef)(IStepFilter* This);
	ULONG(STDMETHODCALLTYPE* Release)(IStepFilter* This);
	HRESULT(STDMETHODCALLTYPE* GetStepIntoFlags)
	(IStepFilter* This, Microsoft__VisualStudio__Debugger__Evaluation__DkmLanguageInstructionAddress*,
	 Microsoft__VisualStudio__Debugger__Stepping__DkmLanguageStepIntoFlags__e*);
	HRESULT(STDMETHODCALLTYPE* EnsureInitialized)
	(IStepFilter* This, Microsoft__VisualStudio__Debugger__Evaluation__DkmLanguageInstructionAddress*);
	HRESULT(STDMETHODCALLTYPE* ParseDocument)(IStepFilter* This, struct IXMLDOMDocument*);
	HRESULT(STDMETHODCALLTYPE* GetStepAction)
	(IStepFilter* This, const ATL__CStringW&, const ATL__CStringW&, CppEE__CStepFilter__StepAction&);
	HRESULT(STDMETHODCALLTYPE* InitializeRegexKeywords)(IStepFilter* This);
	HRESULT(STDMETHODCALLTYPE* ReplaceRegexKeywords)(IStepFilter* This, const wchar_t*, ATL__CStringW&);
	END_INTERFACE
} IStepFilterVtbl;
interface IStepFilter
{
	CONST_VTBL struct IStepFilterVtbl* lpVtbl;

	// taken from CppEE::CStepFilter::GetStepIntoFlags
	enum StepAction
	{
		skip = 1,
		dont_skip = 0
	};
};
