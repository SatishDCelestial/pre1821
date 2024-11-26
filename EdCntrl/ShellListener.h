#pragma once

#include "IVaService.h"
#include "vsshell.h"

class ShellListener : public IVsShellPropertyEvents, public IVsBroadcastMessageEvents, public IVaServiceNotifier
{
  public:
	ShellListener(IVaService* svc);

	~ShellListener();

	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
	    /* [in] */ REFIID riid,
	    /* [iid_is][out] */ void** ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();

	// IVsShellPropertyEvents
	virtual HRESULT STDMETHODCALLTYPE OnShellPropertyChange(VSSPROPID propid, VARIANT var);

	// IVsBroadcastMessageEvents
	virtual HRESULT STDMETHODCALLTYPE OnBroadcastMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// IVaServiceNotifier
	virtual void VaServiceShutdown();

  private:
	void Unadvise();

  private:
	IVaService* mVaService;
	VSCOOKIE mShellPropertyEventChangeCookie;
	VSCOOKIE mShellBroadcastMessageCookie;
	LONG mRefCount;
};

CComPtr<IVsShell> GetVsShell();
