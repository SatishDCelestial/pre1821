#include "StdafxEd.h"
#include "vsshell100.h"
#include "ShellListener.h"
#include "VaService.h"
#include "DevShellAttributes.h"
#include "SubClassWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

CComPtr<IVsShell> GetVsShell()
{
	static IUnknown* tmp = nullptr;
	CComQIPtr<IVsShell> vsShell;
	if (tmp)
	{
		vsShell = tmp;
		return vsShell;
	}

	if (!gPkgServiceProvider)
		return vsShell;

	gPkgServiceProvider->QueryService(SID_SVsShell, IID_IVsShell, (void**)&tmp);
	if (!tmp)
		return vsShell;

	vsShell = tmp;
	if (!vsShell)
	{
		tmp->Release();
		tmp = nullptr;
	}
	return vsShell;
}

ShellListener::ShellListener(IVaService* svc)
    : mVaService(svc), mShellPropertyEventChangeCookie(0), mShellBroadcastMessageCookie(0), mRefCount(0)
{
	CComPtr<IVsShell> pIVsShell(GetVsShell());
	_ASSERTE(pIVsShell);
	if (pIVsShell)
	{
		HRESULT res = pIVsShell->AdviseShellPropertyChanges(static_cast<IVsShellPropertyEvents*>(this),
		                                                    &mShellPropertyEventChangeCookie);
		_ASSERTE(SUCCEEDED(res));

		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		{
			res = pIVsShell->AdviseBroadcastMessages(static_cast<IVsBroadcastMessageEvents*>(this),
			                                         &mShellBroadcastMessageCookie);
			_ASSERTE(SUCCEEDED(res));
		}
	}

	_ASSERTE(mVaService);
	mVaService->RegisterNotifier(this);
	AddRef();
}

ShellListener::~ShellListener()
{
}

// IUnknown
HRESULT STDMETHODCALLTYPE ShellListener::QueryInterface(/* [in] */ REFIID riid,
                                                        /* [iid_is][out] */ void** ppvObject)
{
	if (riid == IID_IUnknown || riid == IID_IVsShellPropertyEvents)
	{
		*ppvObject = this;
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsBroadcastMessageEvents)
	{
		*ppvObject = static_cast<IVsBroadcastMessageEvents*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = NULL;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ShellListener::AddRef()
{
	return (ULONG)InterlockedIncrement(&mRefCount);
}

ULONG STDMETHODCALLTYPE ShellListener::Release()
{
	const LONG cRef = InterlockedDecrement(&mRefCount);
	if (cRef == 0)
		delete this;
	return (ULONG)cRef;
}

HRESULT STDMETHODCALLTYPE ShellListener::OnShellPropertyChange(VSSPROPID propid, VARIANT var)
{
	if (VSSPROPID_IsModal == propid)
	{
		if (mVaService)
			mVaService->OnShellModal(var.boolVal == VARIANT_TRUE);
	}
	return S_OK;
}

void ShellListener::VaServiceShutdown()
{
	mVaService = NULL;
	Unadvise();

	if (1 == mRefCount)
		Release();
}

void ShellListener::Unadvise()
{
	if (mVaService)
	{
		mVaService->UnregisterNotifier(this);
		mVaService = NULL;
	}

	if (mShellPropertyEventChangeCookie)
	{
		VSCOOKIE prevCookie = mShellPropertyEventChangeCookie;
		mShellPropertyEventChangeCookie = 0;
		CComPtr<IVsShell> pIVsShell(GetVsShell());
		if (pIVsShell)
			pIVsShell->UnadviseShellPropertyChanges(prevCookie);
		else if (1 < mRefCount)
			Release(); // they weren't able to release us
	}

	if (mShellBroadcastMessageCookie)
	{
		VSCOOKIE prevCookie = mShellBroadcastMessageCookie;
		mShellBroadcastMessageCookie = 0;
		CComPtr<IVsShell> pIVsShell(GetVsShell());
		if (pIVsShell)
			pIVsShell->UnadviseBroadcastMessages(prevCookie);
		else if (1 < mRefCount)
			Release(); // they weren't able to release us
	}
}

HRESULT STDMETHODCALLTYPE ShellListener::OnBroadcastMessage(UINT msg, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.ivsbroadcastmessageevents.onbroadcastmessage.aspx
	switch (msg)
	{
	case WM_SYSCOLORCHANGE:
	case WM_THEMECHANGED:
	case WM_PALETTECHANGED:
		if (mVaService)
		{
			if (gShellAttr && gShellAttr->IsDevenv15OrHigher())
			{
				// [case: 140632] delay the theme update in VS 2017+ to allow VS time to
				// update the theme colors so the correct colors may be read
				RunFromMainThread([&]() { mVaService->ThemeUpdated(); }, false);
			}
			else
				mVaService->ThemeUpdated();
		}
		break;
	}

	return S_OK;
}
