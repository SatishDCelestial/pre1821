#pragma once
#include "dynwindow\cdxCDynamicDialog.h"
#include "WTString.h"

#include <functional>
#include "CWndDpiAware.h"

// VADialog is used as a common base class to most dialogs in VA.
// It adds the following features to the cdxCDynamicDialog base class:
// - Adds a tomato icon to the System Menu
// - Automatically sets the dialog font to be the system (Icon) font, which VS uses.
//
class VADialog : public cdxCDynamicDialog, public CDpiAware<VADialog>
{
	DECLARE_DYNAMIC(VADialog);

  protected:
	HWND GetDpiHWND() override
	{
		return GetSafeHwnd();
	}

	virtual void OnDpiChanged(DpiChange change, bool& handled)
	{
		handled = true;
		__super::OnDpiChanged(change, handled);
	}

  public:
	VADialog(UINT idd = 0, CWnd* pParent = NULL, Freedom fd = fdAll, UINT nFlags = flDefault);
	VADialog(LPCTSTR lpszTemplateName, CWnd* pParent = NULL, Freedom fd = fdAll, UINT nFlags = flDefault);
	virtual ~VADialog();

	// set help topic for dialog
	// helpTopic is either passed as the "option" value to tooltip.asp or a full url
	void SetHelpTopic(WTString helpTopic);

	virtual INT_PTR DoModal();
	virtual BOOL Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd);
	virtual BOOL Create(UINT nIDTemplate, CWnd* pParentWnd);

  private:
	using CWnd::Create;

  public:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual ULONG GetGestureStatus(CPoint ptTouch);

	void UseEdCaretPos()
	{
		mRepositionToCaret = true;
	}

	UINT_PTR Invoke(bool synchronous, std::function<void(void)> fnc);
	UINT_PTR DelayedInvoke(unsigned int delay_ms, std::function<void(void)> fnc);
	BOOL RemoveInvoke(UINT_PTR invoke_id);
	void ClearInvokes();

  protected:
	DECLARE_MESSAGE_MAP()
	virtual BOOL OnInitDialog();
	virtual void OnInitialized();
	afx_msg BOOL OnHelpInfo(HELPINFO* info);
	afx_msg void OnSysCommand(UINT, LPARAM);

	void CreateUnicodeEditControl(int dlgId, const CStringW& txt, CEdit& ctrl,
	                              bool subclass = false // if true, uses SubclassWindow, else uses Attach
	);

	void UpdateWordBreakProc(int dlgId);

  private:
	void LaunchHelp();
	WTString mHelpUrl;
	BOOL mRepositionToCaret;
	std::map<UINT_PTR, std::function<void(void)>> mToInvoke;
	UINT_PTR mNextInvokeId = 0;
	CCriticalSection mToInvokeCS;

	static VADialog* sActiveVaDlg;
	static CStringW sActiveVaDlgCaption;
};
