#include "stdafxed.h"
#include "WPF_ViewManager.h"
#include "Log.h"
#include "Edcnt.h"
#include "Settings.h"
#include "VaMessages.h"
#include "VaService.h"
#include "PROJECT.H"
#include "WindowUtils.h"
#include "VACompletionSet.h"
#include "LiveOutlineFrame.h"
#include "..\common\ScopedIncrement.h"
#include "FILE.H"
#include "EdDll.h"
#include "RegKeys.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class Junk
{
  public:
	Junk(...)
	{
	}
};
//#define OdsDbg	MyLog
#define OdsDbg Junk

static HWND CreateVSWindow(HWND hParent, LPCSTR wndClass)
{
	_ASSERTE(hParent);
	GetDefaultVaWndCls(wndClass);

	// Create the window
	CRect r;
	CWnd* par = CWnd::FromHandle(hParent);
	if (par->GetSafeHwnd())
		par->GetClientRect(&r);

	// [case: 43205] When the third arg was wndClass, I was seeing that the static
	// control displayed the class name (ie, prints "GenericPane" in the inactive
	// minihelp margin).  CreateWindowEx fails on x64 when passed "" for 3rd arg.
	HWND hwnd = nullptr;
	for (uint cnt = 0; cnt < 10; ++cnt)
	{
		hwnd = ::CreateWindowEx(0, wndClass, NULL, WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, r.left,
		                        r.top, r.right, r.bottom, hParent, NULL, AfxGetInstanceHandle(), NULL);

		if (hwnd != nullptr)
			break;

		// add retry for [case: 111864]
		DWORD err = GetLastError();
		vLog("WARN: CreateVSWindow call to CreateWindowEx failed, 0x%08lx\n", err);
		Sleep(100 + (cnt * 50));
	}

	if (!hwnd)
	{
		vLog("ERROR: CreateVSWindow call to CreateWindowEx failed\n");
		_ASSERTE(hwnd);
	}

	return hwnd;
}

//////////////////////////////////////////////////////////////////////////
// WPF_ParentWrapper forwards messages to EdCntWPF
// If EdCntWPF doesn't swallow the message, it's DefWindowProc
// will call WPFWindowProc to send it to the main WPF window.

LONG WPF_ParentWrapper::sRecurseCnt = 0;

WPF_ParentWrapper::WPF_ParentWrapper(HWND wpfWnd) : m_pActiveView(0)
{
	SubclassWindowW(wpfWnd);
}

ULONG
WPF_ParentWrapper::GetGestureStatus(CPoint /*ptTouch*/)
{
	// [case: 111020]
	// https://support.microsoft.com/en-us/help/2846829/how-to-enable-tablet-press-and-hold-gesture-in-mfc-application
	return 0;
}

BOOL WPF_ParentWrapper::SubclassWindowW(HWND hWnd)
{
	if (!Attach(hWnd))
		return FALSE;

	// allow any other subclassing to occur
	PreSubclassWindow();

	// now hook into the AFX WndProc
	WNDPROC* lplpfn = GetSuperWndProcAddr();
	WNDPROC oldWndProc = (WNDPROC)::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)AfxGetAfxWndProc());
	ASSERT(oldWndProc != AfxGetAfxWndProc());

	if (*lplpfn == NULL)
		*lplpfn = oldWndProc; // the first control of that type created
#ifdef _DEBUG
	else if (*lplpfn != oldWndProc)
	{
		TRACE_((int)traceAppMsg, 0, "Error: Trying to use SubclassWindow with incorrect CWnd\n");
		TRACE_((int)traceAppMsg, 0, "\tderived class.\n");
		TRACE_((int)traceAppMsg, 0, "\thWnd = $%p (nIDC=$%p) is not a %hs.\n", hWnd, hWnd,
		       GetRuntimeClass()->m_lpszClassName);
		ASSERT(FALSE);
		// undo the subclassing if continuing after assert
		::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
	}
#endif

	return TRUE;
}

// UnsubclassWindow is not actually used -- created simply for parity with custom SubclassWindow.
// WPF_ParentWrapper lifetime matches actual HWND
HWND WPF_ParentWrapper::UnsubclassWindow()
{
	_ASSERTE(!"WPF_ParentWrapper::UnsubclassWindow hasn't been tested");
	ASSERT(::IsWindow(m_hWnd));
	_ASSERTE(::IsWindowUnicode(m_hWnd));

	// set WNDPROC back to original value
	WNDPROC* lplpfn = GetSuperWndProcAddr();
	SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
	*lplpfn = NULL;

	// and Detach the HWND from the CWnd object
	return Detach();
}

LRESULT
WPF_ParentWrapper::WPFWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_loggingEnabled && message > WM_MOUSEFIRST && message <= WM_MOUSELAST)
		vCatLog("Editor.Events", "VaEventME Wpw::Wpwp hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_hWnd, message, (uintptr_t)wParam,
		     (uintptr_t)lParam);

	const ForwardStatus fs = ShouldForward(message);
	if (WPF_ConditionalEvent == fs)
		// Process only if VA calls DefWindowProc to insert char, or mouse event.
		return CWnd::WindowProc(message, wParam, lParam);
	return (LRESULT)fs;
}

WPF_ParentWrapper::ForwardStatus WPF_ParentWrapper::ShouldForward(UINT message)
{
	if (WM_CHAR == message)
	{
		// [case: 110643]
		// it might be the case that change 14216 should have removed WM_CHAR from ShouldForward.
		// replicate aggregate focus change 14229 here from WM_CHAR handling in PeekMessageWHook.
		if (WPF_ViewManager::Get() && WPF_ViewManager::Get()->HasAggregateFocus())
			return WPF_ConditionalEvent; // Process only if VA doesn't swallow it.

		return WPF_Ignore;
	}

	if (message == WM_LBUTTONDOWN)
		return WPF_ConditionalEvent; // Process only if VA doesn't swallow it.

	if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || (message >= WM_KEYFIRST && message <= WM_KEYLAST))
		return WPF_ForwardEvent;

	return WPF_Ignore;
}

//////////////////////////////////////////////////////////////////////////
// VS_Document/VS_View is a mimic of the VS2008 window layout

class VS_View : public CWnd
{
	HWND m_VsEditPane;

  public:
	CComPtr<IVsTextView> m_IVsTextView;

	VS_View(HWND parent, IVsTextView* pView)
	{
		_ASSERTE(parent && "VS_View: no parent");
		_ASSERTE(pView && "VS_View: no view");

		m_IVsTextView = pView;
		if (parent)
			m_VsEditPane = CreateVSWindow(parent, "VsEditPane");
		if (m_VsEditPane)
			m_hWnd = CreateVSWindow(m_VsEditPane, "VsTextEditPane");
	}

	~VS_View()
	{
		if (m_VsEditPane)
			::DestroyWindow(m_VsEditPane);
		DestroyWindow();
	}
};

class VS_Document : public CWnd
{
	HWND m_genericPane;

  public:
	VS_Document(HWND parent)
	{
		_ASSERTE(parent && "VS_Document: no parent");

		m_views = 0;
		m_genericPane = CreateVSWindow(parent, "GenericPane");
		if (m_genericPane)
			m_hWnd = CreateVSWindow(m_genericPane, "VsSplitterRoot");
	}

	~VS_Document()
	{
		_ASSERTE(!m_views);
		// gets cleaned up by WPF HwndHost
		// ::DestroyWindow(m_genericPane);
		DestroyWindow();
	}

	int m_views;
};

//////////////////////////////////////////////////////////////////////////
// WPF_ViewManager

WPF_ViewManager::WPF_ViewManager()
{
}

WPF_ViewManager::~WPF_ViewManager()
{
	if (m_DocMapMap.size() || m_ViewMap.size() || 1 < m_WPF_ParentMap.size())
	{
#ifdef _DEBUG
		CString msg;
		CString__FormatA(msg, "WPF_ViewManager leak:\r\ndocMap %zu\r\nviewMap %zu\r\nparentMap %zu", m_DocMapMap.size(),
		                 m_ViewMap.size(), m_WPF_ParentMap.size());
		MessageBox(NULL, msg, IDS_APPNAME, 0);
#endif // _DEBUG
	}
}

HRESULT
WPF_ViewManager::OnSetViewFocus(IVsTextView* pView)
{
	vCatLog("Editor.Events", "OnSetViewFocus e\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());

	// [case: 164056] skip focus step if focus is in nested chat window
	if (gShellAttr && gShellAttr->IsDevenv17OrHigher() &&
		gVaInteropService && gVaInteropService->ChatWindowHasFocus())
	{
		pView = nullptr;
	}

	m_pActiveView = GetVsView(pView);
	m_pActiveWpfWnd = GetWPF_ParentWindowFromHandle(::GetFocus());
	auto pw = GetWPF_ParentWindow();
	if (pw)
		pw->SetActiveView(m_pActiveView);
	else
		vLog("ERROR: vm no parent window");

	if (m_pActiveView)
	{
		HWND hActiveView = m_pActiveView->GetSafeHwnd();
		if (m_pActiveView && hActiveView)
		{
			HRESULT res = pView->SendExplicitFocus();
			if (res != S_OK)
				vLog("ERROR: vm sef %lx\n", res);

			hActiveView = m_pActiveView->GetSafeHwnd();
			if (m_pActiveView && hActiveView) // SendExplicitFocus might cause change of activeView?
			{
				const LRESULT res2 = ::SendMessage(hActiveView, VAM_SET_IVsTextView, (WPARAM)pView, NULL);
				if (res2 != VAM_SET_IVsTextView)
					vLogUnfiltered("WARN: vm VAM_SET_IVsTextView %p", hActiveView);

				hActiveView = m_pActiveView->GetSafeHwnd();
				if (m_pActiveView &&
				    hActiveView) // check again; VAM_SET_IVsTextView can cause focus change (per change 15359)
				{
					::SendMessage(hActiveView, WM_SETFOCUS, NULL, NULL); // Will set gCurrent...
					EdCntPtr ed(g_currentEdCnt);
					if (!ed)
					{
						AutoLockCs l(g_EdCntListLock);
						vLogUnfiltered("%s: vm no focus update %p %u", VAM_SET_IVsTextView == res2 ? "WARN" : "ERROR",
						     hActiveView, (uint)g_EdCntList.size());
						int idx = 0;
						for (const auto x : g_EdCntList)
							vCatLog("Editor.Events", "edwnd %d: %p %p", ++idx, x.get(), x->m_hWnd);
					}
					else if (ed->m_hWnd != hActiveView)
					{
						AutoLockCs l(g_EdCntListLock);
						vLogUnfiltered("ERROR: vm focus mismatch %p %p %u", hActiveView, ed->m_hWnd, (uint)g_EdCntList.size());
						int idx = 0;
						for (const auto x : g_EdCntList)
							vCatLog("Editor.Events", "edwnd %d: %p %p", ++idx, x.get(), x->m_hWnd);
					}
				}
				else
					vLogUnfiltered("ERROR: vm missing view %p %p", m_pActiveView.get(), hActiveView);
			}
			else
				vLogUnfiltered("ERROR: vm after sef %p %p", m_pActiveView.get(), hActiveView);
		}
		else
			vLogUnfiltered("ERROR: vm !hActiveView");
	}
	else if (pView)
		vLogUnfiltered("ERROR: vm !m_pActiveView");

	vCatLog("Editor.Events", "OnSetViewFocus l\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());
	return S_OK;
}

HRESULT
WPF_ViewManager::OnKillViewFocus(IVsTextView* pView)
{
	vCatLog("Editor.Events", "OnKillViewFocus e\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());
	if (m_pActiveView)
	{
		_ASSERTE(GetActiveView() || !m_pActiveView->m_IVsTextView);
		if (!m_pActiveView->m_IVsTextView || m_pActiveView->m_IVsTextView == pView)
			return OnSetViewFocus(NULL);
	}
	else
		return OnSetViewFocus(NULL);

	// [case: 43873] don't invalidate m_pActiveView if we got a KillViewFocus for
	// something that never had focus to begin with

	vCatLog("Editor.Events", "OnKillViewFocus l\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());
	return S_OK;
}

HRESULT
WPF_ViewManager::OnCloseView(IVsTextView* pView)
{
	vLog("OnCloseView e\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());

	if (m_pActiveView && m_pActiveView->m_IVsTextView == pView)
		g_currentEdCnt = NULL;

	{
		// delete the view
		VS_ViewMap::iterator it = m_ViewMap.find(pView);
		if (it != m_ViewMap.end())
		{
			VS_ViewPtr viewToDelete = it->second;
			m_ViewMap.erase(it);

			if (viewToDelete)
			{
				for (WPF_ParentMap::iterator iter = m_WPF_ParentMap.begin(); iter != m_WPF_ParentMap.end(); ++iter)
				{
					WPF_ParentWrapperPtr parent = iter->second;
					if (parent && parent->GetActiveView(viewToDelete))
						parent->SetActiveView(NULL);
				}
			}

			if (m_pActiveView)
			{
				// [case: 43873] don't invalidate m_pActiveView if an inactive
				// view is being closed
				_ASSERTE(GetActiveView() || !m_pActiveView->m_IVsTextView);
				if (!m_pActiveView->m_IVsTextView || m_pActiveView->m_IVsTextView == pView)
					m_pActiveView = NULL;
			}

			_ASSERTE(!m_pActiveView || m_pActiveView != viewToDelete);
		}
	}

	// delete the document if this is the last view
	if (gVaInteropService)
	{
		HWND miniHelpHwnd = gVaInteropService->GetMiniHelpContainer(pView);
		VS_DocumentMap::iterator it = m_DocMapMap.find(miniHelpHwnd);
		if (it != m_DocMapMap.end())
		{
			VS_DocumentPtr d = it->second;
			if (d && --d->m_views <= 0)
			{
				gVaInteropService->ReleaseMiniHelpContainer(pView);
				m_DocMapMap.erase(it);
			}
		}
	}

	vLog("OnCloseView l\t %p %p %p\n", pView, m_pActiveView.get(), m_pActiveWpfWnd.get());
	return S_OK;
}

IVsTextView* WPF_ViewManager::GetActiveView() const
{
	VS_ViewPtr actView(m_pActiveView);
	if (actView)
	{
		_ASSERTE(actView.get() != (VS_View*)(uintptr_t)0xfeeefeeefeeefeeeull &&
		         actView->m_hWnd != (HWND)(uintptr_t)0xfeeefeeefeeefeeeull);
		return actView->m_IVsTextView;
	}
	return NULL;
}

LRESULT
WPF_ParentWrapper::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	ScopedIncrement si(&sRecurseCnt);
	if (sRecurseCnt > 150)
	{
		// build 1859 wer event id -1649675458
		vLog("WARN: WPW:WP: short circuit recursion at msg %x", message);
		_ASSERTE(!"is this recursion in WPF_ParentWrapper::WindowProc?");
		return FALSE;
	}

	if (message > WM_MOUSEFIRST && message <= WM_MOUSELAST)
		vCatLog("Editor.Events", "VaEventME Wpw::Wp actV=%p hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_pActiveView.get(), m_hWnd, message,
		     (uintptr_t)wParam, (uintptr_t)lParam);

	if (!Psettings || !Psettings->m_enableVA)
		return CWnd::WindowProc(message, wParam, lParam);

	// [case: 56073] VsVim compatibility
	static const uint WM_VAX_IGNORENEXTCHAR = ::RegisterWindowMessage("WM_VAX_IGNORENEXTCHAR");
	if (message == WM_VAX_IGNORENEXTCHAR)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed && ed->m_typing)
			ed->m_typing = false;
		return TRUE;
	}

	if (message == WM_DESTROY)
	{
		mDestroyedHwnd = m_hWnd;

		// Cleanup
		if (gMainWnd == this)
			gMainWnd = NULL;
		if (gVaMainWnd == this)
			gVaMainWnd = NULL;
	}

	if (WM_NCDESTROY == message)
	{
		if (gMainWnd == this)
			gMainWnd = NULL;
		if (gVaMainWnd == this)
			gVaMainWnd = NULL;
		LRESULT res = CWnd::WindowProc(message, wParam, lParam);
		if (WPF_ViewManager::Get())
			WPF_ViewManager::Get()->OnParentDestroy(mDestroyedHwnd);
		return res;
	}

	if (message == WM_VA_WPF_GETFOCUS)
	{
		if (m_pActiveView)
		{
			// VAGetFocus send this to get the HWND of the active view.
			return (LRESULT)m_pActiveView->GetSafeHwnd();
		}
		else
		{
			return 0;
		}
	}

	_ASSERTE(m_pActiveView->GetSafeHwnd() != (HWND)(uintptr_t)0xfeeefeeefeeefeeeull);
	const ForwardStatus fs = ShouldForward(message);
	if (m_pActiveView && fs > WPF_Ignore && Psettings->m_enableVA)
	{
		HWND hActiveView = m_pActiveView->GetSafeHwnd();
		if (hActiveView && ::SendMessage(hActiveView, WM_VA_GET_EDCNTRL, 0, 0))
		{
			if (g_loggingEnabled && message != WM_MOUSEMOVE)
				vCatLog("Editor.Events", "VaEventWCE Wpw::Wp actV=%p hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_pActiveView.get(), m_hWnd,
				     message, (uintptr_t)wParam, (uintptr_t)lParam);

			EdCntPtr ed(g_currentEdCnt);
			if (message == WM_MOUSEWHEEL && g_CompletionSet && g_CompletionSet->IsExpUp(ed))
			{
				g_CompletionSet->ProcessEvent(ed, NULL, WM_MOUSEWHEEL, wParam, lParam);
				return TRUE; // Need to handle here, EdCntrl::DefWindowProc is too late.
			}

			// Forward to EdCnt
			if (WPF_ConditionalEvent == fs)
			{
				if (message == WM_LBUTTONDOWN)
				{
					if (ed)
					{
						vCatLog("Editor.Events", "VaEventWCE a");
						// [change 14097] wpf docking issues
						// [case: 52453] floating windows
						CRect edRc;
						ed->vGetClientRect(&edRc);
						ed->vClientToScreen(&edRc);

						CPoint msgPt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
						ClientToScreen(&msgPt);

						if (edRc.PtInRect(msgPt))
						{
							m_pActiveView->SendMessage(message, wParam, lParam);
							// [case: 59442] bad focus in aspx after dismissing find dlg
							// don't return, let them see it too
						}
						return CWnd::WindowProc(message, wParam, lParam);
					}
					else if (gShellAttr->IsDevenv12OrHigher() && g_EdCntList.size())
					{
						vCatLog("Editor.Events", "VaEventWCE b");
						if (g_mainThread == GetCurrentThreadId())
						{
							// [case: 78056] we have edcnts but none of them have focus
							if (gDte)
							{
								CComPtr<EnvDTE::Document> pDocument;
								gDte->get_ActiveDocument(&pDocument);
								if (pDocument)
								{
									CComBSTR bstrName;
									pDocument->get_FullName(&bstrName);
									const CStringW docName(bstrName);
									if (::ShouldFileBeAttachedTo(docName))
									{
										vCatLog("Editor.Events", "VaEventWCE c");
										m_pActiveView->SendMessage(message, wParam, lParam);
										return CWnd::WindowProc(message, wParam, lParam);
									}
								}
							}

							// [case: 96808] not attached, so do not bypass via activeView
							return CWnd::WindowProc(message, wParam, lParam);
						}
						else
						{
							_ASSERTE(!"LBUTTONDOWN being handled on background thread");
							vLog("WARN: WPW:WP: bg thread msg %x", message);
						}
					}
				}
				return m_pActiveView->SendMessage(message, wParam, lParam);
			}
			else
				m_pActiveView->SendMessage(message, wParam, lParam);
		}
	}
	return CWnd::WindowProc(message, wParam, lParam);
}

VS_ViewPtr WPF_ViewManager::GetVsView(IVsTextView* pView)
{
	VS_ViewPtr pvsv;
	if (!Psettings || !gVaInteropService)
		return pvsv;

	if (!(MainWndH && pView))
		return pvsv;

	VS_ViewMap::iterator it = m_ViewMap.find(pView);
	if (it == m_ViewMap.end())
	{
		g_currentEdCnt = NULL;

		HWND miniHelpHwnd = gVaInteropService->GetMiniHelpContainer(pView);
		VS_DocumentPtr d;
		if (miniHelpHwnd)
			d = m_DocMapMap[miniHelpHwnd];
		else
			d = nullptr;

		if (!d && miniHelpHwnd)
		{
			d = std::make_shared<VS_Document>(miniHelpHwnd);
			if (d && d->GetSafeHwnd())
				m_DocMapMap[miniHelpHwnd] = d;
			else if (d)
				d = nullptr;
		}

		if (d)
		{
			if (!m_ViewMap[pView])
			{
				auto vw = std::make_shared<VS_View>(d->GetSafeHwnd(), pView);
				if (vw && vw->GetSafeHwnd())
				{
					m_ViewMap[pView] = vw;
					d->m_views++;
				}
			}
		}
		else
		{
			// [case: 38690] clear outline when focus changes to a view
			// for which we don't have a document (when g_currentEdCnt has been
			// cleared and a new document has not been created - in other IDEs,
			// this is usually handled by EditControlW).
			if (gVaService && gVaService->GetOutlineFrame())
				gVaService->GetOutlineFrame()->Clear();
		}

		it = m_ViewMap.find(pView);
		if (it == m_ViewMap.end())
			return pvsv;
	}

	return it->second;
}

WPF_ViewManager* g_VS_ViewManager = nullptr;

WPF_ViewManager* WPF_ViewManager::Get(bool assertIfNull /*= true*/)
{
	_ASSERTE(g_VS_ViewManager || !assertIfNull);
	return g_VS_ViewManager;
}

void WPF_ViewManager::Create()
{
	if (!g_VS_ViewManager)
		g_VS_ViewManager = new WPF_ViewManager();

	if (MainWndH)
	{
		WPF_ParentWrapperPtr wnd = Get()->GetWPF_ParentWindowFromHandle(MainWndH);
		if (wnd)
			gMainWnd = wnd.get();
	}
}

void WPF_ViewManager::Shutdown()
{
	if (!g_VS_ViewManager)
		return;

	auto tmp = g_VS_ViewManager;
	g_VS_ViewManager = nullptr;
	delete tmp;
}

WPF_ParentWrapperPtr WPF_ViewManager::GetWPF_ParentWindowFromHandle(HWND wpfHWnd)
{
	WPF_ParentWrapperPtr res;
	WPF_ParentMap::iterator it = m_WPF_ParentMap.find(wpfHWnd);
	if (it == m_WPF_ParentMap.end())
	{
		if (wpfHWnd)
		{
			// Create new router if it really is a WPF window;
			WTString cls = GetWindowClassString(wpfHWnd);
			if (cls.contains("DefaultDomain"))
				res = m_WPF_ParentMap[wpfHWnd] = std::make_shared<WPF_ParentWrapper>(wpfHWnd);
		}
	}
	else
	{
		res = it->second;
		_ASSERTE(res->m_hWnd != (HWND)(uintptr_t)0xfeeefeeefeeefeeeull); // [case: 41799]
	}
	return res ? res : m_pActiveWpfWnd;
}

void WPF_ViewManager::OnSetAggregateFocus(int pIWpfTextView_id)
{
	// interesting doc on some focus states:
	// https://github.com/jaredpar/VsVim/blob/master/Src/VsVim/KeyboardInputRouting.txt

	vCatLog("Editor.Events", "OnSetAggregate\t %x %p %p\n", pIWpfTextView_id, m_pActiveView.get(), m_pActiveWpfWnd.get());
	mHasAggregateFocus_id = pIWpfTextView_id;
}

void WPF_ViewManager::OnKillAggregateFocus(int pIWpfTextView_id)
{
	vCatLog("Editor.Events", "OnKillAggregate e\t %x %p %p\n", mHasAggregateFocus_id, m_pActiveView.get(), m_pActiveWpfWnd.get());

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		HWND hFoc = ::GetFocus();
		HINSTANCE inst = hFoc ? (HINSTANCE)GetWindowLongPtr(hFoc, GWLP_HINSTANCE) : 0;
		if (!hFoc || inst != gVaDllApp->GetVaAddress())
			ed->ClearAllPopups(true);
	}
	if (mHasAggregateFocus_id == pIWpfTextView_id)
		mHasAggregateFocus_id = 0;

	vCatLog("Editor.Events", "OnKillAggregate l\t %x %p %p\n", mHasAggregateFocus_id, m_pActiveView.get(), m_pActiveWpfWnd.get());
}

void WPF_ViewManager::OnParentDestroy(HWND hWnd)
{
	vCatLog("Editor", "OnParentDestroy e\t %p %p %p\n", hWnd, m_pActiveView.get(), m_pActiveWpfWnd.get());

	WPF_ParentMap::iterator it = m_WPF_ParentMap.find(hWnd);
	// [case: 44208] only clear m_pActiveView if it's associated with hWnd (similar to OnKillAggregateFocus)
	if (it != m_WPF_ParentMap.end())
	{
		if (it->second && it->second->GetActiveView(m_pActiveView))
			m_pActiveView = nullptr;
		m_WPF_ParentMap.erase(it);
	}

	HWND newWnd = ::GetFocus();
	// [case: 41799] don't create a new parent for a wnd that we were just notified is being destroyed
	if (newWnd && newWnd != hWnd)
		m_pActiveWpfWnd = GetWPF_ParentWindowFromHandle(newWnd);
	else
		m_pActiveWpfWnd = nullptr;

	vCatLog("Editor", "OnParentDestroy l\t %p %p %p\n", newWnd, m_pActiveView.get(), m_pActiveWpfWnd.get());
}
