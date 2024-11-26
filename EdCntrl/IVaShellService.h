#pragma once

interface IServiceProvider;

class IVaShellService
{
  public:
	virtual ~IVaShellService() = default;

	enum StatusAnimation
	{
		sa_general,
		sa_print,
		sa_save,
		sa_deploy,
		sa_synch,
		sa_build,
		sa_find,
		sa_none
	};
	// status bar
	virtual void SetStatusText(LPCSTR txt) = 0;
	virtual void SetStatusText(LPCSTR txt, COLORREF fg, COLORREF bg) = 0;
	virtual LPCSTR GetStatusText() = 0;
	virtual void SetStatusAnimation(BOOL onOff, StatusAnimation animationType) = 0;
	virtual void SetStatusProgress(DWORD* pCookie, BOOL inProgress, LPCSTR label, ULONG completedUnits,
	                               ULONG totalUnits) = 0;

	virtual IServiceProvider* GetServiceProvider() = 0;

	// notification of options dlg update
	virtual void OptionsUpdated() = 0;
};
