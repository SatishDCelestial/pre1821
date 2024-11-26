// ListCtrl2008.cpp : implementation file
//

#include "stdafxed.h"
#include "ListCtrl2008.h"
#include "utils_goran.h"
#include "ArgToolTip.h"
#include "RedirectRegistryToVA.h"
#include "..\common\ThreadName.h"
#include "Settings.h"
#include "WindowUtils.h"
#include "DoubleBuffer.h"
#include "WtException.h"
#include "IdeSettings.h"
#include "PROJECT.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define LB_DisableAll 0xffffffff
#define LB_DisableFancyBorder 0x0002
#define LB_DisableHorizResize 0x0004
#define LB_DisableVertResize 0x0008
#define LB_DisableCtrlFade 0x0010
#define LB_DisableDoubleBuffer 0x0020
#define LB_DisableProps 0x0040
#define LB_DisableOnSizing 0x0080
#define LB_DisableHitTest 0x0100
#define LB_DisableGranularVert 0x0200
#define LB_DisableSaveBits 0x0400

static const int timer_id = 4751;
extern const uint WM_RETURN_FOCUS_TO_EDITOR = ::RegisterWindowMessage("WM_RETURN_FOCUS_TO_EDITOR");
const uint WM_REDRAW_SCROLLBAR = ::RegisterWindowMessage("WM_REDRAW_SCROLLBAR");
extern const uint WM_POSTPONED_SET_FOCUS = ::RegisterWindowMessage("WM_POSTPONED_SET_FOCUS");

void SetLayeredWindowAttributes_update(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags)
{
	BYTE current_alpha = (BYTE)~bAlpha;
	GetLayeredWindowAttributes(hwnd, NULL, &current_alpha, NULL);

	if (current_alpha == bAlpha)
		return;

	SetLayeredWindowAttributes(hwnd, crKey, bAlpha, dwFlags);
	::RedrawWindow(hwnd, NULL, NULL,
	               /*RDW_ERASE | RDW_INVALIDATE |*/ RDW_FRAME | RDW_ALLCHILDREN /*| RDW_ERASENOW*/ | RDW_UPDATENOW);
}

// CListCtrl2008

IMPLEMENT_DYNAMIC(CListCtrl2008, CColorVS2010ListCtrl)

CListCtrl2008::CListCtrl2008() : mDblBuff(new CDoubleBuffer(m_hWnd))
{
	transition = 0;
	transparent = false;
	aborted = false;
	skip_me = false;

	fader_thread = NULL;
	fader_exit = false;
	make_visible_after_first_wmpaint = false;
}

CListCtrl2008::~CListCtrl2008()
{
	_ASSERTE(!fader_is_running);
	if (!fader_exit)
		fader_exit = true;

	while (companions.size() > 0)
		RemoveCompanion(companions.begin()->first);
	delete mDblBuff;

	if (fader_thread)
	{
		::Sleep(1000);
		::CloseHandle(fader_thread);
		fader_thread = nullptr;
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CListCtrl2008, CColorVS2010ListCtrl)
ON_WM_TIMER()
ON_WM_LBUTTONDOWN()
ON_WM_RBUTTONDOWN()
ON_WM_MBUTTONDOWN()
ON_WM_MOUSEWHEEL()
ON_WM_KEYDOWN()
ON_WM_SHOWWINDOW()
ON_WM_NCHITTEST()
ON_WM_SIZING()
ON_WM_SIZE()
ON_WM_SETFOCUS()
ON_WM_NCCALCSIZE()
ON_WM_STYLECHANGING()
ON_WM_KILLFOCUS()
ON_WM_DESTROY()
ON_REGISTERED_MESSAGE(WM_REDRAW_SCROLLBAR, OnRedrawScrollbar)
ON_REGISTERED_MESSAGE(WM_POSTPONED_SET_FOCUS, OnPostponedSetFocus)
ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()
#pragma warning(pop)

// CListCtrl2008 message handlers

BOOL CListCtrl2008::CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	if (!LC2008_DisableAll())
	{
		if (LC2008_HasFancyBorder())
		{
			dwExStyle &= ~WS_EX_TOOLWINDOW; // if not removed, WinXP won't work properly
			// gmit: The following line is a bug; LVS_EX flags should be set with CListCtrl::SetExtendedStyle!
			//       Luckily, LVS_EX_DOUBLEBUFFER equals to WS_EX_TABSTOP which does nothing evil.
			//       I've decided not to fix this properly because it might break something...
			// 			if(!LC2008_UseHomeMadeDoubleBuffering())
			// 				dwExStyle |= LVS_EX_DOUBLEBUFFER;
			//			dwExStyle |= WS_EX_NOACTIVATE;
			dwStyle &= ~WS_DLGFRAME;
			dwStyle |= WS_THICKFRAME;
		}
		if (LC2008_HasCtrlFadeEffect())
		{
			dwExStyle |= WS_EX_LAYERED;
		}
	}

	BOOL ret = CColorVS2010ListCtrl::CreateEx(dwExStyle, dwStyle, rect, pParentWnd, nID);

	if (ret && !LC2008_DisableAll())
	{
		if (dwExStyle & WS_EX_LAYERED)
		{
			::SetLayeredWindowAttributes(m_hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
			aborted = true;
		}

		if (LC2008_ThreadedFade())
		{
			CWinThread* thread =
			    ::AfxBeginThread(&CListCtrl2008::FaderThread, this, THREAD_PRIORITY_HIGHEST, 0, CREATE_SUSPENDED);
			if (thread)
			{
				::DuplicateHandle(::GetCurrentProcess(), thread->m_hThread, ::GetCurrentProcess(), &fader_thread, 0,
				                  false, DUPLICATE_SAME_ACCESS);
				thread->ResumeThread();
			}
			else
			{
				vLog("ERROR: failed to start faderThread");
			}
		}
		else
		{
			SetTimer(timer_id, 10, NULL);
		}
	}

	return ret;
}

BOOL CListCtrl2008::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
                           CWnd* pParentWnd, UINT nID, CCreateContext* pContext)
{
	// avoid WS_POPUP assert

	return CWnd::CreateEx(0, lpszClassName, lpszWindowName, dwStyle, rect.left, rect.top, rect.right - rect.left,
	                      rect.bottom - rect.top, pParentWnd->GetSafeHwnd(), (HMENU)(UINT_PTR)nID, (LPVOID)pContext);
}

void CListCtrl2008::OnDestroy()
{
	if (fader_thread && LC2008_ThreadedFade())
	{
		fader_exit = true;

		DWORD exitcode;
		while (::GetExitCodeThread(fader_thread, &exitcode))
		{
			if (exitcode != STILL_ACTIVE)
				break;
			::Sleep(1);
		}

		::CloseHandle(fader_thread);
		fader_thread = NULL;
	}

	CColorVS2010ListCtrl::OnDestroy();
}

void CListCtrl2008::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == timer_id)
	{
		if (!LC2008_DisableAll() && LC2008_HasCtrlFadeEffect())
		{
			std::list<std::pair<HWND, byte>> pending_updates;
			{
				__lock(cs);

				// handle mouse click in other windows
				if ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) || (::GetAsyncKeyState(VK_LBUTTON) & 0x8000))
					aborted = true;

				// handle aborting due to mouse click
				if (aborted || !IsWindowVisible())
				{
					skip_me = false;
					fadein_companions.clear();
					if (!(::GetAsyncKeyState(VK_CONTROL) & 0x8000)) // this will handle if ctrl is initially pressed
						aborted = false;

					if (transparent || transition)
					{
						transparent = false;
						transition = 1;
						goto do_fade;
					}
					return;
				}

				// detect ctrl key pressed change
				if (/*!skip_me &&*/ (transparent ^ !!(::GetAsyncKeyState(VK_CONTROL) & 0x8000)))
				{
					transparent = !transparent;
					transition = ::GetTickCount();
					skip_me = false;
					fadein_companions.clear();
				}

				// handle fade in/out
			do_fade:
				if (transition)
				{
					double phase = (::GetTickCount() - transition) / (double)LC2008_GetFadeTime_ms();
					if (phase >= 1.)
					{
						phase = 1.;
						transition = 0;
						skip_me = false;
						fadein_companions.clear();
					}

					double level =
					    (1. - LC2008_GetFadeLevel()) * (transparent ? (1. - phase) : phase) + LC2008_GetFadeLevel();

					if (!skip_me)
					{
						pending_updates.push_back(std::make_pair(m_hWnd, (byte)(level * 255)));
					}
					std::list<HWND> to_delete;
					for (stdext::hash_map<HWND, bool>::const_iterator it = companions.begin(); it != companions.end();
					     ++it)
					{
						if (::IsWindow(it->first))
						{
							if (skip_me && !contains(fadein_companions, it->first))
								continue;

							pending_updates.push_back(std::make_pair(it->first, (byte)(level * 255)));
						}
						else
						{
							to_delete.push_back(it->first);
						}
					}
					for (std::list<HWND>::const_iterator it = to_delete.begin(); it != to_delete.end(); ++it)
						companions.erase(*it);
				}
			}

			// handle fade updates outside the main lock to prevent deadlocks
			for (std::list<std::pair<HWND, byte>>::const_iterator it = pending_updates.begin();
			     it != pending_updates.end(); ++it)
			{
				::SetLayeredWindowAttributes_update(it->first, RGB(0, 0, 0), it->second, LWA_ALPHA);
				if (it->first == m_hWnd)
					RedrawScrollbar();
			}
		}
		return;
	}

	CColorVS2010ListCtrl::OnTimer(nIDEvent);
}

UINT CListCtrl2008::FaderThread(void* param)
{
	DEBUG_THREAD_NAME("VAX: CListCtrl2008::FaderThread");
	((CListCtrl2008*)param)->FaderThread();
	return 0;
}
void CListCtrl2008::FaderThread()
{
	fader_is_running = true;
	while (!gShellIsUnloading && !fader_exit)
	{
		OnTimer(timer_id);
		::Sleep(10);
	}
	fader_is_running = false;
}

void CListCtrl2008::AbortCtrlFadeEffect()
{
	if (LC2008_DisableAll() || !LC2008_HasCtrlFadeEffect())
		return;

	aborted = true;
}

void CListCtrl2008::OnLButtonDown(UINT nFlags, CPoint point)
{
	AbortCtrlFadeEffect();

	CColorVS2010ListCtrl::OnLButtonDown(nFlags, point);
}
void CListCtrl2008::OnRButtonDown(UINT nFlags, CPoint point)
{
	AbortCtrlFadeEffect();

	CColorVS2010ListCtrl::OnRButtonDown(nFlags, point);
}
void CListCtrl2008::OnMButtonDown(UINT nFlags, CPoint point)
{
	AbortCtrlFadeEffect();

	CColorVS2010ListCtrl::OnMButtonDown(nFlags, point);
}
BOOL CListCtrl2008::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	AbortCtrlFadeEffect();

	return CColorVS2010ListCtrl::OnMouseWheel(nFlags, zDelta, pt);
}
void CListCtrl2008::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	AbortCtrlFadeEffect();

	CColorVS2010ListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CListCtrl2008::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CColorVS2010ListCtrl::OnShowWindow(bShow, nStatus);

	AbortCtrlFadeEffect();
}

void CListCtrl2008::AddCompanion(HWND hwnd, cdxCDynamicWnd::Mode horanchor, cdxCDynamicWnd::Mode veranchor,
                                 bool restore_initial_transparency)
{
	if (LC2008_DisableAll() ||
	    !(LC2008_HasCtrlFadeEffect() || LC2008_HasHorizontalResize() || LC2008_HasVerticalResize()))
		return;
	if (!::IsWindow(hwnd))
		return;
	LONG exstyle;
	{
		__lock(cs);
		if (contains(companions, hwnd))
		{
			// just update resizing type
			if (Psettings->mListboxFlags & LB_DisableProps)
				return;

			mySetProp(hwnd, "__va_horanchor", (HANDLE)horanchor);
			mySetProp(hwnd, "__va_veranchor", (HANDLE)veranchor);
			return;
		}

		exstyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
		companions[hwnd] = !!(exstyle & WS_EX_LAYERED);
	}

	if (LC2008_HasCtrlFadeEffect() && !(exstyle & WS_EX_LAYERED))
		::SetWindowLong(hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);

	if (!(Psettings->mListboxFlags & LB_DisableProps))
	{
		mySetProp(hwnd, "__va_horanchor", (HANDLE)horanchor);
		mySetProp(hwnd, "__va_veranchor", (HANDLE)veranchor);
	}

	if (LC2008_HasCtrlFadeEffect())
	{
		unsigned char alpha = 255;
		if (restore_initial_transparency)
			::GetLayeredWindowAttributes(m_hWnd, NULL, &alpha, NULL);
		::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), alpha, LWA_ALPHA);
	}

	::SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
}

void CListCtrl2008::RemoveCompanion(HWND hwnd)
{

	bool was_layered;
	{
		__lock(cs);
		stdext::hash_map<HWND, bool>::iterator it = companions.find(hwnd);
		if (it == companions.end())
			return;
		was_layered = it->second;
		companions.erase(it);
	}

	if (LC2008_HasCtrlFadeEffect())
	{
		if (::IsWindow(hwnd))
		{
			if (!was_layered)
			{
				::SetWindowLong(hwnd, GWL_EXSTYLE, ::GetWindowLong(hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
			}
			else
			{
				::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
			}
		}
	}
}

void CListCtrl2008::FadeInCompanion(HWND hwnd)
{
	if (LC2008_DisableAll())
		return;
	{
		__lock(cs);
		if (transition || transparent)
			return;
		if (!contains(companions, hwnd))
			return;

		transition = ::GetTickCount();
		transparent = false;
		skip_me = true;
		fadein_companions.insert(hwnd);
	}

	OnTimer(timer_id);
}

NCHT_RET CListCtrl2008::OnNcHitTest(CPoint point)
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableHitTest)
		return CColorVS2010ListCtrl::OnNcHitTest(point);

	// handle growbox at the place of bottom arrow
	CRect client;
	GetClientRect(client);
	ClientToScreen(client);
	//	if(GetStyle() & WS_HSCROLL)
	//		client.bottom += ::GetSystemMetrics(SM_CYHSCROLL);
	if (GetStyle() & WS_VSCROLL)
		client.right += ::GetSystemMetrics(SM_CXVSCROLL);
	client.left = client.right - ::GetSystemMetrics(SM_CXVSCROLL);
	client.top = client.bottom - ::GetSystemMetrics(SM_CYHSCROLL);
	if (client.PtInRect(point))
		return (GetStyle() & WS_VSCROLL) ? HTVSCROLL : HTCLIENT;

	NCHT_RET ret = CColorVS2010ListCtrl::OnNcHitTest(point);

	if (LC2008_DisableAll())
	{
		switch (ret)
		{
		case HTBOTTOM:
		case HTBOTTOMLEFT:
		case HTBOTTOMRIGHT:
		case HTGROWBOX:
		case HTLEFT:
		case HTRIGHT:
		case HTTOP:
		case HTTOPLEFT:
		case HTTOPRIGHT:
			ret = HTBORDER;
			break;
		}
	}
	else
	{
		switch (ret)
		{
		case HTBOTTOM:
		case HTTOP:
			if (!LC2008_HasVerticalResize())
				ret = HTBORDER;
			break;
		case HTGROWBOX:
			ret = HTVSCROLL;
			break;
		case HTBOTTOMLEFT:
		case HTBOTTOMRIGHT:
		case HTTOPLEFT:
		case HTTOPRIGHT:
			if (!LC2008_HasVerticalResize())
			{
				if (!LC2008_HasHorizontalResize())
					ret = HTBORDER;
				//				else
				//					ret = ((ret == HTBOTTOMLEFT) || (ret == HTTOPLEFT)) ? HTLEFT : HTRIGHT;
				//			} else {
				//				if(!LC2008_HasHorizontalResize())
				//					ret = ((ret == HTTOPLEFT) || (ret == HTTOPRIGHT)) ? HTTOP : HTBOTTOM;
			}
			break;
		case HTLEFT:
		case HTRIGHT:
			if (!LC2008_HasHorizontalResize())
				ret = HTBORDER;
			break;
		}
	}

	return ret;
}

void CListCtrl2008::OnSizing(UINT side, LPRECT newrect)
{
	CColorVS2010ListCtrl::OnSizing(side, newrect);
	GetSizingRect(side, newrect);
}

void CListCtrl2008::GetSizingRect(UINT side, LPRECT newrect)
{
	if (LC2008_DisableAll() || Psettings->mListboxFlags & LB_DisableOnSizing)
		return;

	CRect oldrect;
	GetWindowRect(oldrect);

	// handle sizing restrictions
	if (!LC2008_HasHorizontalResize())
	{
		switch (side)
		{
		case WMSZ_LEFT:
		case WMSZ_TOPLEFT:
		case WMSZ_BOTTOMLEFT:
			newrect->left = newrect->right - oldrect.Width();
			break;
		case WMSZ_RIGHT:
		case WMSZ_TOPRIGHT:
		case WMSZ_BOTTOMRIGHT:
		default:
			newrect->right = newrect->left + oldrect.Width();
			break;
		}
	}
	if (!LC2008_HasVerticalResize())
	{
		switch (side)
		{
		case WMSZ_TOP:
		case WMSZ_TOPLEFT:
		case WMSZ_TOPRIGHT:
			newrect->top = newrect->bottom - oldrect.Height();
			break;
		case WMSZ_BOTTOM:
		case WMSZ_BOTTOMLEFT:
		case WMSZ_BOTTOMRIGHT:
		default:
			newrect->bottom = newrect->top + oldrect.Height();
			break;
		}
	}

	CRect clientrect;
	GetClientRect(clientrect);
	const CSize borders = oldrect.Size() - clientrect.Size();

	// prevent displaying of horizontal scrollbar
	if (LC2008_DoRestoreLastWidth())
	{
		const int oldColWidth = GetColumnWidth(0);
		const int newColWidth = newrect->right - newrect->left - borders.cx;
		if (oldColWidth != newColWidth)
			SetColumnWidth(0, newColWidth);
	}

	// handle vertical sizing granularity
	int saved_height = newrect->bottom - newrect->top;
	if (LC2008_HasGranularVerticalResize() && LC2008_HasVerticalResize())
	{
		CRect itemrect(0, 0, 100, 15);
		GetItemRect(0, itemrect, LVIR_BOUNDS);
		_ASSERTE(itemrect.Height() > 0);

		int height = newrect->bottom - newrect->top - borders.cy;
		int items = (height + itemrect.Height() / 2) / itemrect.Height();
		// 		if(items < LC2008_GetMinimumVisibleItems())
		// 			items = LC2008_GetMinimumVisibleItems();
		// 		if(items > LC2008_GetMaximumVisibleItems())
		// 			items = LC2008_GetMaximumVisibleItems();
		saved_height = items * itemrect.Height() + borders.cy;
		if (items > GetItemCount())
			items = GetItemCount();
		int delta = items * itemrect.Height() - height;
		if (delta)
		{
			switch (side)
			{
			case WMSZ_BOTTOM:
			case WMSZ_BOTTOMLEFT:
			case WMSZ_BOTTOMRIGHT:
			default:
				newrect->bottom += delta;
				//				newrect->bottom += 50;
				break;
			case WMSZ_TOP:
			case WMSZ_TOPLEFT:
			case WMSZ_TOPRIGHT:
				newrect->top -= delta;
				break;
			}
		}
	}

	// save last window size
	if (LC2008_DoRestoreLastWidth() || LC2008_DoRestoreLastHeight())
	{
		// [case: 35543] these calls cause a leak when typing in vs2005 in
		// release builds that have the armadillo wrapper enabled.
		// see also case 28167 for a list of APIs that armadillo intercepts.
		CRedirectRegistryToVA rreg;
		::AfxGetApp()->WriteProfileInt("WindowPositions", "AutoComplete_width", newrect->right - newrect->left);
		::AfxGetApp()->WriteProfileInt("WindowPositions", "AutoComplete_height", saved_height);
	}

	// handle companion windows positions
	const CSize delta = CRect(newrect).BottomRight() - oldrect.BottomRight();

	if ((delta.cx || delta.cy) && !(Psettings->mListboxFlags & LB_DisableProps))
	{
		HDWP dwp = ::BeginDeferWindowPos((int)companions.size());
		{
			__lock(cs);
			for (stdext::hash_map<HWND, bool>::const_iterator it = companions.begin(); dwp && it != companions.end();
			     ++it)
			{
				if (!::IsWindow(it->first))
					continue;

				CSize delta2 = delta;
				cdxCDynamicWnd::Mode horanchor =
				    (cdxCDynamicWnd::Mode)(uint)(uintptr_t)myGetProp(it->first, "__va_horanchor");
				cdxCDynamicWnd::Mode veranchor =
				    (cdxCDynamicWnd::Mode)(uint)(uintptr_t)myGetProp(it->first, "__va_veranchor");
				if (horanchor == cdxCDynamicWnd::mdNone)
					delta2.cx = 0;
				if (veranchor == cdxCDynamicWnd::mdNone)
					delta2.cy = 0;

				if (delta.cx || delta.cy)
				{
					CRect rect;
					::GetWindowRect(it->first, rect);
					rect.OffsetRect(delta2);
					//					::SetWindowPos(it->first, NULL, rect.left, rect.top, 0, 0, SWP_NOSIZE |
					// SWP_NOREPOSITION | SWP_NOACTIVATE);
					dwp = ::DeferWindowPos(dwp, it->first, NULL, rect.left, rect.top, 0, 0,
					                       SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);

					CWnd* tooltip = CWnd::FromHandle(it->first);
					if (tooltip)
					{ // some special handling is needed for tooltips because they will reposition on WM_PAINT (!!)
						ArgToolTip* argtooltip = dynamic_cast<ArgToolTip*>(tooltip);
						if (argtooltip)
							argtooltip->m_pt += delta2;
					}
				}
			}
		}

		if (dwp)
			::EndDeferWindowPos(dwp);
	}

	DoEnsureVisible();
}

void CListCtrl2008::OnSize(UINT nType, int cx, int cy)
{
	CColorVS2010ListCtrl::OnSize(nType, cx, cy);

	DoEnsureVisible();
	RedrawScrollbar();

	// handled in OnSetFocus
	//	if(!LC2008_DisableAll())
	//		PostMessage(WM_RETURN_FOCUS_TO_EDITOR);
}

void CListCtrl2008::DoEnsureVisible()
{
	if (!LC2008_EnsureVisibleOnResize())
		return;

	int sel = GetNextItem(-1, LVNI_SELECTED);
	if (sel != -1 && GetItemCount() > GetCountPerPage())
	{
		int first = GetScrollPos(SB_VERT);
		first = max(min(first, GetItemCount() - GetCountPerPage()), 0);
		EnsureVisible(first, false);

		EnsureVisible(sel, false);
	}
}

void CListCtrl2008::OnSetFocus(CWnd* pOldWnd)
{
	CColorVS2010ListCtrl::OnSetFocus(pOldWnd);

	if (!LC2008_DisableAll() && (!pOldWnd || (pOldWnd->m_hWnd != m_hWnd)))
	{
		RedrawScrollbar();

		PostMessage(WM_RETURN_FOCUS_TO_EDITOR);
	}
}

void CListCtrl2008::OnKillFocus(CWnd* pNewWnd)
{
	CColorVS2010ListCtrl::OnKillFocus(pNewWnd);

	if (!LC2008_DisableAll())
		RedrawScrollbar();
}

void CListCtrl2008::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	if (!LC2008_DisableAll())
	{
		if (GetStyle() & WS_HSCROLL)
			ShowScrollBar(SB_HORZ, false);
	}

	CColorVS2010ListCtrl::OnNcCalcSize(bCalcValidRects, lpncsp);
}

void CListCtrl2008::OnStyleChanging(int styletype, LPSTYLESTRUCT ss)
{
	CColorVS2010ListCtrl::OnStyleChanging(styletype, ss);

	if (!LC2008_DisableAll())
	{
		if (ss && (styletype == GWL_STYLE))
		{
			ss->styleNew &= ~WS_HSCROLL;
		}
	}
}

void CListCtrl2008::RedrawScrollbar()
{
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
	{
		// [case: 71131]
		return;
	}

	if (LC2008_DisableAll())
		return;

	// deadlocks occur if handled immediately
	PostMessage(WM_REDRAW_SCROLLBAR);
}

LRESULT CListCtrl2008::OnRedrawScrollbar(WPARAM wparam, LPARAM lparam)
{

	MSG msg;
	if (::PeekMessage(&msg, m_hWnd, WM_REDRAW_SCROLLBAR, WM_REDRAW_SCROLLBAR, PM_NOREMOVE))
		return 0; // don't do anything until only one WM_REDRAW_SCROLLBAR is waiting

	// fix for 'growbox sometimes appear drawn over bottom scrollbar arrow'
	SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_DRAWFRAME);
	return 0;
}

const UINT WM_MAKE_OPAQUE = ::RegisterWindowMessageW(L"WM_MAKE_OPAQUE");
LRESULT CListCtrl2008::WindowProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (!LC2008_DisableAll() && LC2008_UseHomeMadeDoubleBuffering())
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			if (wparam)
				mDblBuff->HandleEraseBkgndForNicerResize((HDC)wparam);
			// fall-through
		case WM_PAINT:
			//		case WM_PRINTCLIENT:
			{
				LRESULT ret = 0;
				HDC hdc = (HDC)wparam;
				if (msg == WM_ERASEBKGND)
				{
					hdc = NULL; // this will force painting only into offscreen buffer
				}
				HDC hdc2 = hdc;
				PAINTSTRUCT ps;
				if (!hdc2)
				{
					if (msg == WM_PAINT)
					{
						//						hdc2 = ::GetDC(m_hWnd);
						memset(&ps, 0, sizeof(ps));
						hdc2 = ::BeginPaint(m_hWnd, &ps);
					}
					else
						hdc2 = ::CreateCompatibleDC(NULL);
				}

				try
				{
					CDoubleBufferedDC dc(*mDblBuff, hdc2,
					                     gShellAttr->IsDevenv11OrHigher()
					                         ? g_IdeSettings->GetEnvironmentColor(L"Window", false)
					                         : ::GetSysColor(COLOR_WINDOW));
					ret = CColorVS2010ListCtrl::WindowProc(msg, (WPARAM)dc.m_hDC, lparam);
				}
				catch (const WtException& e)
				{
					UNUSED_ALWAYS(e);
					VALOGERROR("ERROR: CListCtrl2008 exception caught 1");
				}

				if (!hdc)
				{
					if (msg == WM_PAINT)
						::EndPaint(m_hWnd, &ps);
					else
						::DeleteDC(hdc2);
				}

				if ((msg == WM_PAINT) && make_visible_after_first_wmpaint)
				{
					make_visible_after_first_wmpaint = false;
					PostMessage(WM_MAKE_OPAQUE, 0, (LPARAM)::GetTickCount());
				}
				return ret;
			}
		}
		if (msg == WM_MAKE_OPAQUE)
		{
			const uint show_delayed_ms = 30;
			if ((::GetTickCount() - (uint)lparam) > show_delayed_ms)
				SetLayeredWindowAttributes_update(m_hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
			else
				PostMessage(WM_MAKE_OPAQUE, 0, lparam);
			return 0;
		}
	}

	return CColorVS2010ListCtrl::WindowProc(msg, wparam, lparam);
}

LRESULT CListCtrl2008::OnPostponedSetFocus(WPARAM wparam, LPARAM lparam)
{
	SetFocus();
	//	PostMessage(WM_RETURN_FOCUS_TO_EDITOR);
	return 0;
}

BOOL CListCtrl2008::SetColumnWidth(int nCol, int cx)
{
	BOOL retval = __super::SetColumnWidth(nCol, cx);
	// can't invalidate client rect - because scrollbar might not be accounted for yet
	Invalidate(false);
	return retval;
}

int CListCtrl2008::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message)
{
	if (!LC2008_DisableAll())
		return MA_NOACTIVATE;

	return CColorVS2010ListCtrl::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

bool CListCtrl2008::LC2008_DisableAll() const
{
	if (!Psettings || (Psettings->mListboxFlags & LB_DisableAll) == LB_DisableAll)
		return true;
	return false;
}
bool CListCtrl2008::LC2008_HasFancyBorder() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableFancyBorder)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_HasHorizontalResize() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableHorizResize)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_HasVerticalResize() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableVertResize)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_HasCtrlFadeEffect() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableCtrlFade)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_HasToolbarFadeInEffect() const
{
	return true;
}
unsigned int CListCtrl2008::LC2008_GetFadeTime_ms() const
{
	return 200;
	//	return 2000;
}
double CListCtrl2008::LC2008_GetFadeLevel() const
{
	return .25;
}
bool CListCtrl2008::LC2008_HasGranularVerticalResize() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableGranularVert)
		return false;
	return true;
}
// unsigned int CListCtrl2008::LC2008_GetMinimumVisibleItems() const {
// 	return 5;
// }
// unsigned int CListCtrl2008::LC2008_GetMaximumVisibleItems() const {
// 	return 15;
// }
bool CListCtrl2008::LC2008_DoRestoreLastWidth() const
{
	if (!Psettings || Psettings->mAutoSizeListBox)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_DoRestoreLastHeight() const
{
	if (!Psettings || Psettings->mAutoSizeListBox)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_DoMoveTooltipOnVScroll() const
{
	return true;
}
bool CListCtrl2008::LC2008_EnsureVisibleOnResize() const
{
	return true;
}
bool CListCtrl2008::LC2008_DoSetSaveBits() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableSaveBits)
		return false;
	return true;
}
bool CListCtrl2008::LC2008_ThreadedFade() const
{
	return true;
}
bool CListCtrl2008::LC2008_UseHomeMadeDoubleBuffering() const
{
	if (!Psettings || Psettings->mListboxFlags & LB_DisableDoubleBuffer)
		return false;
	static std::optional<bool> comctl6;

	// gmit: unfortunately, I don't know which comctl32 version is really used (devenv can easily use both versions 5
	// and 6
	//       in the same instance), so, we'll always use our double buffering
	comctl6 = false;

	if (!comctl6)
	{
		comctl6 = false;

		HMODULE comctl32 = GetModuleHandleA("comctl32.dll");
		DLLGETVERSIONPROC DllGetVersion =
		    (DLLGETVERSIONPROC)(uintptr_t)GetProcAddress(comctl32, "DllGetVersion");
		if (DllGetVersion)
		{
			DLLVERSIONINFO dvi;
			memset(&dvi, 0, sizeof(dvi));
			dvi.cbSize = sizeof(dvi);
			if (SUCCEEDED(DllGetVersion(&dvi)))
			{
#define PACKVERSION(major, minor) MAKELONG(minor, major)
				comctl6 = PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion) >= PACKVERSION(6, 0);
			}
		}
	}

	return !*comctl6;
}
bool CListCtrl2008::LC2008_DoFixAutoFitWidth() const
{
	// should not be used yet
	return false;
}
