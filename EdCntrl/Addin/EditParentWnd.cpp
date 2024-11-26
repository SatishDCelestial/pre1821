// EditParentWnd.cpp : implementation file
//

#include "stdafx.h"
#include <afxmt.h>
#include <richedit.h>
#include "EditParentWnd.h"
#include "log.h"
#include "../VaMessages.h"
#include "../../addin/aic.h"
#include "..\DevShellAttributes.h"
#include "..\project.h"
#include "../settings.h"
#include "..\WindowUtils.h"
#include "../FileTypes.h"
#include "../DpiCookbook/VsUIDpiHelper.h"
#include "../VaService.h"

#if _MSC_VER <= 1200
#include <objmodel/textauto.h>
#include <objmodel/appauto.h>
#else
#include "../../../../3rdParty/Vc6ObjModel/textauto.h"
#include "../../../../3rdParty/Vc6ObjModel/appauto.h"
#endif
#include "ColorListControls.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define ID_DESTROY 1
#define ID_ASKSHUTDOWN 2
#define ID_DESTRUCTOR 3
#define ID_CLOSE 4

CCriticalSection g_EdParentWndDeleteListLock;
std::vector<EditParentWnd*> g_EdParentWndDeleteList;
static void CALLBACK DeleteTimerProc(HWND, UINT, UINT_PTR, DWORD);
static void FixMDIClientEdge(HWND hMDIClient, HWND oldWnd);

static BOOL g_inSize = FALSE;
HWND g_hOurEditWnd = NULL;
HWND g_hLastEditWnd = NULL;

//#define DISABLE_BUGFIX_10517

/////////////////////////////////////////////////////////////////////////////
// EditParentWnd

EditParentWnd::EditParentWnd(const CStringW& file)
{
	_ASSERTE(file.GetLength());
	vCatLog("Editor", "EPW::EPW %s", (LPCTSTR)CString(file));
	m_fileName = file;
	m_fType = GetFileType(file);
	m_pWnd = NULL;
	m_pDoc = NULL;
	m_hDevEdit = NULL;
	m_wizHeight = 0;
	m_insideMyMove = 0;
}

EditParentWnd::~EditParentWnd()
{
	vCatLog("Editor", "EPW::~EPW");

	if (m_pWnd)
	{
		m_pWnd->Release();
		m_pWnd = NULL;
	}
	if (m_pDoc)
	{
		m_pDoc->Release();
		m_pDoc = NULL;
	}
	m_fileName.Empty();
	UnInit(ID_DESTRUCTOR);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(EditParentWnd, CWnd)
//{{AFX_MSG_MAP(EditParentWnd)
ON_WM_CLOSE()
ON_WM_SIZE()
ON_WM_PAINT()
ON_WM_SETFOCUS()
ON_WM_DESTROY()
ON_WM_SYSCOMMAND()
ON_MESSAGE(WM_VA_MINIHELPW, OnSetHelpTextW)
ON_MESSAGE(WM_VA_MINIHELP_SYNC, OnSetHelpTextSync)
ON_WM_NCACTIVATE()
ON_WM_WINDOWPOSCHANGING()
ON_WM_WINDOWPOSCHANGED()
//}}AFX_MSG_MAP
ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipText)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// EditParentWnd message handlers

#define ID_CLOSEPREVMINIHELP (WM_USER + 1031)

LRESULT EditParentWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_MOUSEMOVE)
		::SendMessage(m_hDevEdit, message, wParam, lParam);
	if (message == WM_VA_DEVEDITCMD && wParam == WM_CLOSE)
	{
		// [case: 43873]
		if (!gShellAttr->IsDevenv10OrHigher() || lParam != 0xdead)
			Display(FALSE);
	}
	if (gShellIsUnloading && message == 0x210)
	{
		CRect r;
		::GetClientRect(::GetParent(m_hWnd), &r);
		++m_insideMyMove;
		MoveWindowIfNeeded(this, &r, FALSE);
		--m_insideMyMove;
		m_miniHelp = NULL;
		if (g_pMiniHelpFrm)
		{
			Display(FALSE);
			g_pMiniHelpFrm->DestroyWindow();
			delete g_pMiniHelpFrm;
			g_pMiniHelpFrm = NULL;
		}
		delete this;
		return TRUE;
	}
	if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE)
	{
		if (!gShellAttr->IsDevenv())
			return FALSE;
	}
	if (message == WM_DESTROY)
	{
		//		int i = 123;
	}
	if (message == WM_WINDOWPOSCHANGING && gShellAttr->IsDevenv() && !g_inSize)
	{
		__super::WindowProc(WM_WINDOWPOSCHANGED, wParam, lParam);
	}
	LRESULT retval = __super::WindowProc(message, wParam, lParam);

	if (WM_VA_CheckMinihelp == message)
	{
		// reparent the one va nav bar if we have text that we previously displayed.
		// occurs during SetFocus before Scope is run to reduce period of time
		// during which placeholder is displayed by editor if switching from
		// one file to another in the same position (for example, via alt+o).
		if (!m_miniHelp && !mLastContext.IsEmpty())
			Display(true);
	}
	else if (message == WM_COMMAND && wParam == ID_CLOSEPREVMINIHELP)
	{
		Display(false);
	}
	else if (message == WM_COMMAND && wParam == WM_SIZE /*&& !g_inSize*/)
	{
		if (gShellAttr->IsMsdev())
		{
			CRect r;
			GetClientRect(&r);
			MoveWindowIfNeeded(&m_afxFrameCwnd, &r);
			RedoLayout();
		}
		if (gShellAttr->IsDevenv() && !gShellAttr->IsDevenv10OrHigher())
		{
			CWnd* p = GetParent()->GetParent();
			if (p)
			{
				CRect r;
				p->GetWindowRect(&r);
				if (p->GetParent())
				{
					p->GetParent()->ScreenToClient(&r);
					++m_insideMyMove;
					r.InflateRect(0, 0, -1, -1);
					MoveWindowIfNeeded(p, &r);
					r.InflateRect(0, 0, 1, 1);
					MoveWindowIfNeeded(p, &r);
					--m_insideMyMove;
				}
			}
			RedoLayout();
		}
	}
	else if (message == WM_COMMAND && wParam == ID_DESTRUCTOR)
	{
		delete this;
		return TRUE;
	}
	else if (WM_SETFOCUS == message)
	{
		GetChildEditWithFocus();
		g_hOurEditWnd = m_hDevEdit;
	}
	return retval;
}

BOOL EditParentWnd::Init(HWND replaceMe, HWND hAfxMDIfrm, HWND devEdit, IGenericWindow* pWnd, ITextDocument* pTextDoc)
{
	vCatLog("Editor", "EPW::Init\t%p", replaceMe);
	BOOL retval;

	SetWindowLongPtr(replaceMe, GWLP_USERDATA, (GetWindowLongPtr(replaceMe, GWLP_USERDATA) | VA_WND_DATA));
	{
		AICAddinSubclass fixAddins(replaceMe);
		retval = SubclassWindow(replaceMe);
	}

	m_hDevEdit = devEdit;

	m_miniHelp = NULL;

	Display();

	if (gShellAttr->IsDevenv())
	{
		Display(FALSE);
		Display();
		CRect r;
		GetWindowRect(&r);
		SendMessage(WM_SIZE, SIZE_RESTORED, MAKELONG(r.Width(), r.Height()));
	}

	m_pWnd = pWnd;
	if (m_pWnd)
		m_pWnd->AddRef();
	m_pDoc = pTextDoc;
	if (m_pDoc)
		m_pDoc->AddRef();

#ifndef DISABLE_BUGFIX_10517
	if (gShellAttr->IsDevenv())
		m_afxFrameCwnd.SubclassWindow(hAfxMDIfrm);
	else
#endif
		m_afxFrameCwnd.m_hWnd = hAfxMDIfrm;

	RedoLayout();
	return retval;
}

void EditParentWnd::OnClose()
{
	vCatLog("Editor.Events", "EPW::OCl");
	if (m_hDevEdit && IsWindow(m_hDevEdit))
		::SendMessage(m_hDevEdit, WM_VA_FLUSHFILE, 0, 0);
	CWnd::OnClose();
	try
	{
		if (!IsWindow(this->m_hWnd)) // user may have cancled the close
			delete this;
	}
	catch (...)
	{
		VALOGEXCEPTION("EPW:");
		//		ASSERT(FALSE);
	}
}

const uint SIZE_EMULATED = 0x10001000;

void EditParentWnd::OnSize(UINT nType, int cx, int cy)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	// do max/restore
	if (g_inSize && gShellAttr->IsDevenv())
	{
		if (nType != SIZE_EMULATED)
			CWnd::OnSize(nType, cx, cy);
	}
	else
	{
		g_inSize = TRUE;
		// prevent this or parent from redrawing
		//		HWND hPar = ::GetParent(m_hWnd);
		//		::SendMessage(hPar, WM_SETREDRAW, 0, 0);
		//		::SendMessage(m_hDevEdit, WM_SETREDRAW, 0, 0);
		if (gShellAttr->IsDevenv())
		{
			if (nType != SIZE_EMULATED)
				CWnd::OnSize(nType, cx, cy);
			if (1)
			{
				CRect r, rw;
				GetWindowRect(&rw);
				GetParent()->ScreenToClient(&rw);
				::GetClientRect(::GetParent(m_hWnd), &r);
#ifdef DISABLE_BUGFIX_10517
				if (r.bottom > rw.bottom)
					r.bottom = rw.bottom; // offset for html window s
#endif
				if (r.bottom != cy && Is_Tag_Based(m_fType))
				{
					// [case: 39842] fix for pane sizing when in split view
					// [case: 26834] regressed in vs2005 but not vs2008 due to fix for 39842
					if (nType != SIZE_EMULATED || gShellAttr->IsDevenv8())
					{
						// [case: 26834] problems with lower scrollbar
						HWND navbar = ::FindWindowExW(GetParent()->m_hWnd, NULL, L"HtmEdTabSelect", NULL);
						if (navbar && ::IsWindowVisible(navbar))
						{
							HWND splitter = ::FindWindowExW(::GetParent(navbar), NULL, L"HtmSplitterControl", NULL);
							CRect srect(0, 0, 0, 0);
							if (splitter)
								::GetWindowRect(splitter, &srect);
							if (splitter && ::IsWindowVisible(splitter) && srect.Width() && srect.Height())
							{
								// position in the upper half if splitter is present (VS2008)
								GetParent()->ScreenToClient(srect);
								r.bottom = srect.top;
							}
							else
							{
								// position at the bottom, above the source/design selector bar
								CRect nbrect;
								::GetWindowRect(navbar, &nbrect);
								r.bottom -= nbrect.Height();
							}
						}
					}
					else
						r.bottom = cy; // Add space for their HTML/XML nav bar at the bottom. Case 375
				}

				if (Psettings && Psettings->m_enableVA && !gShellIsUnloading && !Psettings->m_noMiniHelp)
				{
					if (Psettings->minihelpAtTop)
						r.top = (int)Psettings->m_minihelpHeight;
					else
						r.bottom -= (int)Psettings->m_minihelpHeight;
				}
				++m_insideMyMove;
				MoveWindowIfNeeded(m_hWnd, &r, true);
				--m_insideMyMove;

				if (m_miniHelp)
				{
					if (Psettings && Psettings->m_enableVA && !Psettings->m_noMiniHelp)
					{
						m_miniHelp->RecalcLayout();
						m_miniHelp->RedrawWindow();
					}
					else
						Display(false);
				}
			}
		}
		else if (nType != SIZE_EMULATED)
		{
			if (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
				RedoLayout(cx, cy);
			if (!(cx || cy))
			{
				CRect r;
				GetClientRect(&r);
				CWnd::OnSize(nType, r.Width(), r.Height());
			}
			else
				CWnd::OnSize(nType, cx, cy);
			RedoLayout();
			if (m_miniHelp)
				m_miniHelp->Invalidate(TRUE);
		}

		g_inSize = FALSE;
	}
}

void EditParentWnd::RedoLayout(int x, int y)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (!m_hWnd /* || !g_mainWnd*/)
		return;
	if (m_miniHelp)
	{
		if (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
			m_miniHelp->RecalcLayout();
		else
			Display(false);
	}

	if (Psettings->m_noMiniHelp && m_afxFrameCwnd.GetSafeHwnd())
	{
		CRect r;
		GetClientRect(&r);
		CRect rf;
		m_afxFrameCwnd.GetWindowRect(&rf);
		if (m_afxFrameCwnd)
		{
			if (gShellAttr->IsDevenv())
				MoveWindowIfNeeded(m_afxFrameCwnd.GetParent(), &r);
			else
				MoveWindowIfNeeded(m_afxFrameCwnd.m_hWnd, &r, true);
		}
		return;
	}

	if (gShellAttr->IsDevenv())
	{
		if (gVaInteropService)
			gVaInteropService->CheckMinihelpHeight();
		return;
	}

	CRect rp(0, 0, x, y);
	if (!(x || y))
		GetClientRect(&rp);

	// the subclassed edit and our edit
	rp.left = 0; // VA_EDIT_LEFT_EDGE;
	if (Psettings->minihelpAtTop)
	{
		HWND chld = ::GetWindow(m_hWnd, GW_CHILD);
		if (gShellAttr->IsDevenv())
		{
			while (chld && chld != m_hWnd)
			{
				int miniHeight = (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
				                     ? (int)Psettings->m_minihelpHeight
				                     : 0;
				WTString cls = GetWindowClassString(chld);
				CRect r;
				::GetWindowRect(chld, &r);
				ScreenToClient(&r);
				++m_insideMyMove;
				if (cls == "Static")
				{
					m_wizHeight = r.Height();
					::MoveWindowIfNeeded(chld, r.left, miniHeight, r.Width(), r.Height(), TRUE);
				}
				else if (cls == "VsEditPane")
				{
					int top = miniHeight + m_wizHeight;
					if (r.top < top)
						::MoveWindowIfNeeded(chld, r.left, top, r.Width(), r.Height() - miniHeight, TRUE);
				}
				--m_insideMyMove;
				chld = ::GetWindow(chld, GW_HWNDNEXT);
			}
		}
		else
			while (chld && chld != m_hWnd)
			{
				CRect r;
				::GetWindowRect(chld, &r);
				ScreenToClient(&r);
				if (gShellAttr->IsDevenv())
				{
					if (r.Height() != (int)Psettings->m_minihelpHeight)
					{
						if (r.top >= VsUI::DpiHelper::LogicalToDeviceUnitsY(1))
							r.top -= VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
						if (r.top < ((int)Psettings->m_minihelpHeight -
						             VsUI::DpiHelper::LogicalToDeviceUnitsY(4)) ||
						    r.top == VsUI::DpiHelper::LogicalToDeviceUnitsY(0x1c))
						{
							if (r.Height() > VsUI::DpiHelper::LogicalToDeviceUnitsY(100) && Psettings->m_enableVA &&
							    !Psettings->m_noMiniHelp)
								r.bottom -= (int)Psettings->m_minihelpHeight;
							++m_insideMyMove;
							if (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
								::MoveWindowIfNeeded(
								    chld, r.left,
								    r.top + (int)Psettings->m_minihelpHeight,
								    r.Width(), r.Height(), TRUE);
							else
								::MoveWindowIfNeeded(chld, r.left, r.top, r.Width(), r.Height(), TRUE);
							--m_insideMyMove;
						}
					}
				}
				else
				{
					CPoint rs(r.left + VsUI::DpiHelper::LogicalToDeviceUnitsX(2),
					          r.top - VsUI::DpiHelper::LogicalToDeviceUnitsY(10));
					::WindowFromPoint(rs);
					ClientToScreen(&rs);
					if (!Psettings->m_enableVA && !Psettings->m_noMiniHelp)
						Invalidate(TRUE);
					else if (r.Height() > (int)Psettings->m_minihelpHeight &&
					         (r.top < ((int)Psettings->m_minihelpHeight +
					                   VsUI::DpiHelper::LogicalToDeviceUnitsY(10)) ||
					          this->m_hWnd == ::WindowFromPoint(rs)))
					{
						r.top = Psettings->m_noMiniHelp
						            ? 0
						            : (int)Psettings->m_minihelpHeight;
						++m_insideMyMove;
						::MoveWindowIfNeeded(chld, r.left, r.top, r.Width(), r.Height(), TRUE);
						--m_insideMyMove;
					}
				}
				chld = ::GetWindow(chld, GW_HWNDNEXT);
			}

		if (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
			rp.top = (int)Psettings->m_minihelpHeight;
		return;
	}
	else
	{
		if (Psettings->m_enableVA && !Psettings->m_noMiniHelp)
			rp.bottom -=
			    Psettings->m_noMiniHelp ? 0 : (int)Psettings->m_minihelpHeight;
	}

	if (m_afxFrameCwnd.m_hWnd && Psettings->m_enableVA && !Psettings->m_noMiniHelp)
		MoveWindowIfNeeded(m_afxFrameCwnd.m_hWnd, &rp);
}

void EditParentWnd::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CWnd::OnActivate(nState, pWndOther, bMinimized);

	if (nState != WA_INACTIVE)
	{
		vCatLog("Editor.Events", "EPW::OA\tf(0x%p)", ::GetFocus());
		if (m_afxFrameCwnd.m_hWnd)
		{
			GetChildEditWithFocus();
			g_hOurEditWnd = m_hDevEdit;
		}
		else
		{
			if (g_hOurEditWnd)
				g_hLastEditWnd = g_hOurEditWnd;
			g_hOurEditWnd = NULL;
		}
	}
}

void EditParentWnd::OnSetFocus(CWnd* pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);

	if (m_afxFrameCwnd.m_hWnd)
	{
		GetChildEditWithFocus();
		g_hOurEditWnd = m_hDevEdit;
	}
	else
	{
		vCatLog("Editor.Events", "EPW::OSF WARN strange...");
		if (g_hOurEditWnd)
			g_hLastEditWnd = g_hOurEditWnd;
		g_hOurEditWnd = NULL;
	}
}

void EditParentWnd::OnDestroy()
{
	vCatLog("Editor.Events", "EPW::OD");
	// this is now the default closer...
	// this is the only way we know if they didnt cancel on the onclose

	if (gShellAttr->IsDevenv())
		UnInit(ID_DESTROY);
	else
	{
		if (m_miniHelp)
			Display(false);
		m_afxFrameCwnd.DestroyWindow();
		CWnd::OnDestroy();
	}
}

static UINT_PTR s_timerID = 0;
void EditParentWnd::UnInit(int option)
{
	vCatLog("Editor", "EPW::UnInit %d", option);
	if (m_afxFrameCwnd.m_hWnd && ::IsWindow(m_afxFrameCwnd.m_hWnd))
	{
		m_afxFrameCwnd.UnsubclassWindow();
		m_afxFrameCwnd.m_hWnd = NULL;
	}

	if (m_miniHelp)
		Display(false);

	if (option == ID_ASKSHUTDOWN)
		return;

	HWND oldWnd = m_hWnd;
	HWND hMDIClient = ::GetParent(m_hWnd);
	// protect against MFC asserts
	// don't unsubclass if the original wnd is already gone
	if (m_hWnd && ::IsWindow(m_hWnd))
	{
		SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (GetWindowLongPtr(m_hWnd, GWLP_USERDATA) ^ VA_WND_DATA));
		{
			AICAddinSubclass fixAddins(oldWnd);
			UnsubclassWindow();
		}
	}
	m_hWnd = NULL;

	switch (option)
	{
	case ID_DESTROY:
		::PostMessage(oldWnd, WM_DESTROY, 0, 0);
		break;
	case ID_CLOSE:
		::PostMessage(oldWnd, WM_CLOSE, 0, 0);
		break;
	case ID_DESTRUCTOR:
		return;
	default:
		vLogUnfiltered("  EPW::UnInit ERROR unexpected option %d", option);
	}
	// is this needed anymore?  Its enumchldwin causes resource hit on 98 closing windows
	// This is needed.  Here's the testcase:
	// 1. note client edge border around msdev client area
	// 2. maximize a window
	// 3. close all windows
	// 4. the msdev client area lost the client edge
	FixMDIClientEdge(hMDIClient, oldWnd);

	{
		AutoLockCs _l(g_EdParentWndDeleteListLock);
		g_EdParentWndDeleteList.push_back(this);
	}

	if (s_timerID)
		::KillTimer(NULL, s_timerID);
	s_timerID = ::SetTimer(NULL, 0, 50u, (TIMERPROC)&DeleteTimerProc);
}

BOOL EditParentWnd::OnToolTipText(UINT, NMHDR*, LRESULT*)
{
	return FALSE;
}

LRESULT
EditParentWnd::OnSetHelpTextW(WPARAM count, LPARAM strs)
{
	if (count == 3)
	{
		auto* arr = (LPCWSTR*)strs;
		SetHelpText(arr[0], arr[1], arr[2], true);	
	}
	return 1;
}

LRESULT
EditParentWnd::OnSetHelpTextSync(WPARAM count, LPARAM strs)
{
	if (count == 3)
	{
		auto* arr = (LPCWSTR*)strs;
		// [case: 89977]
		SetHelpText(arr[0], arr[1], arr[2], false);
	}
	return 1;
}

void EditParentWnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	CWnd::OnSysCommand(nID, lParam);
	switch (nID)
	{
	case SC_NEXTWINDOW:
	case SC_PREVWINDOW:
	case SC_MINIMIZE:
		if (g_hOurEditWnd)
			g_hLastEditWnd = g_hOurEditWnd;
		g_hOurEditWnd = NULL;
		break;
	case SC_SIZE:
	case SC_MOVE:
	case SC_MAXIMIZE:
		GetChildEditWithFocus();
		g_hOurEditWnd = m_hDevEdit;
		break;
	}
}

void DeleteWnds()
{
	::KillTimer(NULL, 50);

	if (g_EdParentWndDeleteList.size())
	{
		AutoLockCs _l(g_EdParentWndDeleteListLock);
		for (std::vector<EditParentWnd*>::iterator it = g_EdParentWndDeleteList.begin();
		     it != g_EdParentWndDeleteList.end(); ++it)
		{
			delete *it;
		}

		g_EdParentWndDeleteList.clear();
	}

	if (s_timerID)
		::KillTimer(NULL, s_timerID);
}

void CALLBACK DeleteTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	KillTimer(hWnd, idEvent);
	s_timerID = 0;
	DeleteWnds();
}

BOOL EditParentWnd::OnNcActivate(BOOL bActive)
{
	if (!bActive)
	{
		if (g_hOurEditWnd)
			g_hLastEditWnd = g_hOurEditWnd;
		g_hOurEditWnd = NULL;
	}
	else if (!g_hOurEditWnd)
	{
		GetChildEditWithFocus();
		g_hOurEditWnd = m_hDevEdit;
	}
	return CWnd::OnNcActivate(bActive);
}

typedef struct
{
	HWND m_parent;
	HWND m_this;
} ChildEnumStr;

static BOOL CALLBACK MDIClientChildEnum(HWND hwnd, LPARAM lParam)
{
	ChildEnumStr* ptr = (ChildEnumStr*)lParam;
	if (hwnd && hwnd != ptr->m_this && GetParent(hwnd) == ptr->m_parent)
		return FALSE;
	return TRUE;
}

void FixMDIClientEdge(HWND hMDIClient, HWND oldWnd)
{
	if (!hMDIClient)
		return;

	// if hMDIClient has no children, then ensure that it has WS_EX_CLIENTEDGE style
	ChildEnumStr enumArg;
	enumArg.m_parent = hMDIClient;
	enumArg.m_this = oldWnd;
	if (!MDIClientChildEnum(::GetWindow(hMDIClient, GW_CHILD), (LPARAM)&enumArg) ||
	    !EnumChildWindows(hMDIClient, MDIClientChildEnum, (LPARAM)&enumArg))
		return;

	LONG style = GetWindowLong(hMDIClient, GWL_EXSTYLE);
	if (style & WS_EX_CLIENTEDGE)
		return;

	SetWindowLong(hMDIClient, GWL_EXSTYLE, style | WS_EX_CLIENTEDGE);
	// to make the style change take effect, call SetWindowPos
	SetWindowPos(hMDIClient, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE);
}

void EditParentWnd::OnPaint()
{
	try
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

		CWnd::OnPaint();
		if (gShellAttr->IsDevenv())
			return;
		if (!m_miniHelp && Psettings->m_enableVA && !Psettings->m_noMiniHelp)
		{
			GetChildEditWithFocus();
			if (::GetFocus() == m_hDevEdit)
			{
				// if we have focus, create windows
				Display();
			}
			else
			{
				// just draw grey box if we dont have focus
				CDC* dc = GetDC();
				CRect r;
				GetClientRect(&r);
				if (Psettings->minihelpAtTop)
					r.bottom = (int)Psettings->m_minihelpHeight;
				else
					r.top = r.bottom - (int)Psettings->m_minihelpHeight;
				dc->FillSolidRect(&r, GetSysColor(COLOR_MENU));
				dc->Draw3dRect(&r, GetSysColor(COLOR_3DDKSHADOW), GetSysColor(COLOR_3DHILIGHT));
				ReleaseDC(dc);
			}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("EPW:");
	}
}

void EditParentWnd::Display(bool show)
{
	static HWND g_lastMiniHelp = NULL;
	if (show)
	{
		if (!m_miniHelp)
		{
			GetChildEditWithFocus();
			if (g_lastMiniHelp && ::IsWindow(g_lastMiniHelp))
				::SendMessage(g_lastMiniHelp, WM_COMMAND, ID_CLOSEPREVMINIHELP, 0);
			if (!g_pMiniHelpFrm)
			{
#ifdef _DEBUG
				// this should only be done once
				static bool once = false;
				ASSERT(!once);
				once = true;
#endif
				g_pMiniHelpFrm = new MiniHelpFrm;
			}
			m_miniHelp = g_pMiniHelpFrm;

			CStringW context(mLastContext), def(mLastDef), proj(mLastProject);
			if (gShellAttr->IsDevenv())
				m_miniHelp->Reparent(this->GetParent(), this, context, def, proj);
			else
				m_miniHelp->Reparent(this, this, context, def, proj);
			RedoLayout();
		}
		g_lastMiniHelp = m_hWnd;
	}
	else if (m_miniHelp)
	{
		m_miniHelp->Reparent(NULL);
		m_miniHelp = NULL;
		g_lastMiniHelp = NULL;

		if (CVS2010Colours::IsVS2010NavBarColouringActive())
		{
			CWnd* wnd = GetParent();
			if (wnd->GetSafeHwnd())
				wnd = wnd->GetParent();
			if (wnd->GetSafeHwnd() && ::IsWindow(wnd->GetSafeHwnd()))
				wnd->InvalidateRect(nullptr, FALSE);
		}
	}
}

// sets the m_hDevEdit wnd member based on the wnd with focus
// m_hDevEdit can change due to window splitting
HWND EditParentWnd::GetChildEditWithFocus()
{
	HWND hWnd = ::GetFocus();
	if (::GetParent(::GetParent(hWnd)) == m_hWnd)
	{
		m_hDevEdit = hWnd;
		return m_hDevEdit;
	}
	return NULL;
}

void EditParentWnd::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CWnd::OnWindowPosChanging(lpwndpos);

	DoWindowPosChange(lpwndpos);
}

void EditParentWnd::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CWnd::OnWindowPosChanged(lpwndpos);

	DoWindowPosChange(lpwndpos);

	if (lpwndpos && (lpwndpos->cx > 0) && (lpwndpos->cy > 0))
		OnSize(SIZE_EMULATED, lpwndpos->cx, lpwndpos->cy);
}

void EditParentWnd::DoWindowPosChange(WINDOWPOS* wndpos)
{
	if (!wndpos)
		return;

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

#ifndef DISABLE_BUGFIX_10517
	if (gShellAttr->IsDevenv() && !m_insideMyMove)
	{
		if (Psettings->m_enableVA && !gShellIsUnloading && !Psettings->m_noMiniHelp)
		{
			if (Psettings->minihelpAtTop)
			{
				wndpos->y = (int)Psettings->m_minihelpHeight;
			}
		}
	}
#endif
}

void EditParentWnd::SetHelpText(LPCWSTR con, LPCWSTR def, LPCWSTR proj, bool async)
{
	if (!m_miniHelp)
		Display();
	else
		RedoLayout();

	mLastContext = con;
	mLastDef = def;
	mLastProject = proj;

	if (m_miniHelp)
		m_miniHelp->SetHelpText(con, def, proj, async);
}
