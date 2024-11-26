#pragma once
#include "VaService.h"

interface IVsTextView;
interface IVsTextLines;

class VS_View;
using VS_ViewPtr = std::shared_ptr<VS_View>;
class VS_Document;
using VS_DocumentPtr = std::shared_ptr<VS_Document>;

class WPF_ParentWrapper : public CWnd
{
  public:
	WPF_ParentWrapper(HWND wpfWnd);
	void SetActiveView(VS_ViewPtr view)
	{
		m_pActiveView = view;
	}
	bool GetActiveView(VS_ViewPtr view) const
	{
		return m_pActiveView == view;
	}
	BOOL SubclassWindowW(HWND hWnd);
	HWND UnsubclassWindow();
	LRESULT WPFWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual ULONG GetGestureStatus(CPoint ptTouch);

  private:
	enum ForwardStatus
	{
		WPF_Ignore = 0,
		WPF_ForwardEvent,
		WPF_ConditionalEvent
	};
	ForwardStatus ShouldForward(UINT message);
	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	VS_ViewPtr m_pActiveView;
	HWND mDestroyedHwnd = nullptr;
	static LONG sRecurseCnt;
};

using WPF_ParentWrapperPtr = std::shared_ptr<WPF_ParentWrapper>;

class WPF_ViewManager
{
  public:
	static WPF_ViewManager* Get(bool assertIfNull = true);
	static void Create();
	static void Shutdown();

	HRESULT OnSetViewFocus(IVsTextView* pView);
	HRESULT OnKillViewFocus(IVsTextView* pView);
	HRESULT OnCloseView(IVsTextView* pView);
	IVsTextView* GetActiveView() const;
	WPF_ParentWrapperPtr GetWPF_ParentWindow()
	{
		return m_pActiveWpfWnd;
	}
	void OnSetAggregateFocus(int pIWpfTextView_id);
	void OnKillAggregateFocus(int pIWpfTextView_id);
	bool HasAggregateFocus() const
	{
		return mHasAggregateFocus_id && GetActiveView() && gVaService && !gVaService->IsShellModal();
	}
	void OnParentDestroy(HWND hWnd);

  private:
	WPF_ViewManager();
	~WPF_ViewManager();

	typedef std::map<CComPtr<IVsTextView>, VS_ViewPtr> VS_ViewMap;
	typedef std::map<HWND, VS_DocumentPtr> VS_DocumentMap;
	typedef std::map<HWND, WPF_ParentWrapperPtr> WPF_ParentMap;

	VS_ViewMap m_ViewMap;
	VS_DocumentMap m_DocMapMap;
	WPF_ParentMap m_WPF_ParentMap;
	VS_ViewPtr m_pActiveView;
	WPF_ParentWrapperPtr m_pActiveWpfWnd;
	int mHasAggregateFocus_id = 0;

	VS_ViewPtr GetVsView(IVsTextView* pView);
	WPF_ParentWrapperPtr GetWPF_ParentWindowFromHandle(HWND wpfHWnd);
};
