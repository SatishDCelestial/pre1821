#include "stdafxed.h"
#include "edcnt.h"
#include "resource.h"
#include "project.h"
#include "log.h"
#include "../addin/dscmds.h"
#include "VaMessages.h"
#include "bc/regexp.h"
#include "expansion.h"
#include "parsethrd.h"
#include "bordercwnd.h"
#include "usage.h"
#include "fontsettings.h"
#include "rbuffer.h"
#include "ArgToolTip.h"
#include "timer.h"
#include "VATree.h"
#include "SubClassWnd.h"
#include "wtcsym.h"
#include "VAClassView.h"
#include "LicenseMonitor.h"
#include "VAFileView.h"
#include "VACompletionSet.h"
#include "VaTimers.h"
#include "VARefactor.h"
#include "VAParse.h"
#include "AutotextManager.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "ScreenAttributes.h"
#include "PooledThreadBase.h"
#include "VaService.h"
#include <algorithm>
#include "FileTypes.h"
#include "WindowUtils.h"
#include "assert_once.h"
#include "wt_stdlib.h"
#include "StringUtils.h"
#include "mainThread.h"
#include "assert_once.h"
#include "RecursionClass.h"
#include "Settings.h"
#include "Oleobj.h"
#include "DBLock.h"
#include "FileId.h"
#include "../../../3rdParty/Vc6ObjModel/textdefs.h"
#include "EolTypes.h"
#include "BCMenu.h"
#include "FindReferences.h"
#include "file.h"
#include "Directories.h"
#include "LiveOutlineFrame.h"
#include "TraceWindowFrame.h"
#include "WrapCheck.h"
#include "FeatureSupport.h"
#include "SyntaxColoring.h"
#include "ToolTipEditCombo.h"
#include "Guesses.h"
#include "..\Common\TempAssign.h"
#include "VASeException/VASeException.h"
#include "FileFinder.h"
#include "EdcntWPF.h"
#include "EdCntCWnd.h"
#include "EdCntCWndVs.h"
#include "SolutionFiles.h"
#include "UndoContext.h"
#include "VAAutomation.h"
#include "AutoReferenceHighlighter.h"
#include "IdeSettings.h"
#include "GetFileText.h"
#include "../common/ScopedIncrement.h"
#include "LogElapsedTime.h"
#include "AutoUpdate/WTAutoUpdater.h"
#include "DpiCookbook/VsUIDpiHelper.h"
#include "VaHashtagsFrame.h"
#include "VASmartSelect.h"
#include "WindowsHooks.h"
#include "VaAddinClient.h"
#include "SortSelectedLinesDlg.h"
#include "AutotextExpansion.h"
#include "../VaPkg/VaPkgUI/PkgCmdID.h"
#ifndef NOSMARTFLOW
#include "SmartFlow/phdl.h"
#endif
#include "RegKeys.h"
#include "Registry.h"
#include "../WTKeyGen/WTValidate.h"
#include "DllNames.h"
#include "BuildInfo.h"
#include "Addin/MiniHelpFrm.h"

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
#include "RadStudioPlugin.h"
#endif

using OWL::string;
using OWL::TRegexp;
using std::ios;
using std::ofstream;

extern void PatchW32Functions(bool patch);

#define CM_EDITREDO 1
#define CM_EDITEXPAND 0x427

// VApad commands...
#define ID_EDIT_HOME 37120
#define ID_EDIT_LINE_END 37118
#define ID_EDIT_PAGE_DOWN 37116
#define ID_EDIT_PAGE_UP 37114

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

// #define _DEBUG_JER

EdCntPtr g_currentEdCnt;

extern int g_dopaint;
extern BOOL gHasMefExpansion;

static CStringW s_LastReparseFile;

#define WS_VA_COLOR 0x1000000

#define MK_VA_NOTCMD 0x8000 // informs Ed that WM_MBUTTON messages are not to be interpreted as command

#if !defined(RAD_STUDIO)
#define MK_VA_FROMHOOK 0x10000 // helps hooker to filter out messages posted from hook itself

class GetMsgHooks : protected WindowsHooks::CSimpleHook<WH_GETMESSAGE, MSG>
{
  protected:
	struct FocusTracker : protected WindowsHooks::CSimpleHook<WH_CALLWNDPROC, CWPSTRUCT>
	{
		HWND now = 0;
		DWORD time = 0;
		HWND old = 0;

		bool BeginHooking()
		{
			now = ::GetFocus();
			time = ::GetTickCount();
			old = 0;
			return __super::BeginHook();
		}

		void EndHooking()
		{
			now = 0;
			time = 0;
			old = 0;
			return __super::EndHook();
		}

		virtual void OnHookMsg(WPARAM wParam, CWPSTRUCT* msg)
		{
			// For sure, we process both messages, because focus could be
			// sent to window out of our parent IDE process.

			if (msg->message == WM_SETFOCUS)
			{
				now = msg->hwnd;
				old = (HWND)msg->wParam;
				time = ::GetTickCount();
			}
			else if (msg->message == WM_KILLFOCUS)
			{
				now = (HWND)msg->wParam;
				old = msg->hwnd;
				time = ::GetTickCount();
			}
		}

		bool in_limit(CWnd* wnd, DWORD limit = 20)
		{
			if (wnd && wnd->GetSafeHwnd() == now)
				return GetTickCount() - time <= limit;
			return false;
		}
	};

	int num_refs = 0;
	FocusTracker focusTracker;

  public:
	GetMsgHooks()
	{
	}
	virtual ~GetMsgHooks()
	{
	}

	void Attach()
	{
		if (!Psettings || !Psettings->mEnableEdMouseButtonHooks)
			return;

		_ASSERTE(num_refs == (int)g_EdCntList.size());

		if (++num_refs == 1)
		{
			CatLog("LowLevel", "GetMsgHooks: Starting hook.");

			BeginHook();

			if (gShellAttr && !gShellAttr->IsDevenv10OrHigher())
				focusTracker.BeginHooking();
		}
	}

	void Detach()
	{
		if (!Psettings || !Psettings->mEnableEdMouseButtonHooks)
			return;

		_ASSERTE(num_refs == (int)g_EdCntList.size());

		if (--num_refs == 0)
		{
			Log("GetMsgHooks: Ending hook.");

			EndHook();

			if (gShellAttr && !gShellAttr->IsDevenv10OrHigher())
				focusTracker.EndHooking();
		}
	}

  protected:
	virtual void OnHookMsg(WPARAM wParam, MSG* msg)
	{
		if (!Psettings || !Psettings->m_enableVA)
			return;

		if (HandleMiddleUpDown(msg))
			return;

		if (EatShiftRightButtonUp(msg))
			return;

		if (HandleWheel(msg))
			return;

		if (HandleDPIWorkaround(msg))
			return;
	}

	bool IsWindowInThisProcess(HWND wnd)
	{
		static DWORD sVsProcessId = 0;
		if (!sVsProcessId)
		{
			::GetWindowThreadProcessId(MainWndH, &sVsProcessId);
			_ASSERTE(sVsProcessId);
		}

		DWORD wndProcId = 0;
		GetWindowThreadProcessId(wnd, &wndProcId);
		return !wndProcId || wndProcId == sVsProcessId;
	}

	bool VsHasFocus()
	{
		if (!gShellAttr->IsDevenv10OrHigher())
			return IsWindowInThisProcess(::GetFocus());
		else if (WPF_ViewManager::Get())
			return WPF_ViewManager::Get()->HasAggregateFocus();
		else
			return false;
	}

	bool IsTopEdCntUnderMouse(EdCntPtr ed)
	{
		if (ed)
		{
			// [case: 94287]
			// implemented better way to detect which EdCnt is under cursor
			// previous implementation was checking only if mouse is inside of bounds

			if (gShellAttr->IsDevenv10OrHigher())
			{
				EdCntWPF* edwpf = static_cast<EdCntWPF*>(ed.get());
				return edwpf->IsMouseOver();
			}
			else
			{
				CPoint pnt;
				::GetCursorPos(&pnt);

				CRect wndRect;
				ed->vGetClientRect(&wndRect);
				ed->vClientToScreen(&wndRect);

				// WARNING - possible regression:
				// Don't use WindowFromPoint to ensure that ed is under mouse!
				// It causes that IDE's context menu opens together with our menu.
				// This of course applies only to non-WPF IDEs

				return !!wndRect.PtInRect(pnt);
			}
		}
		return false;
	}

	EdCntPtr TopEdCntUnderMouse()
	{
#ifdef DEBUG
		if (!g_EdCntList.empty())
		{
			int count = 0;

			AutoLockCs l(g_EdCntListLock);
			for (auto it = g_EdCntList.crbegin(); it != g_EdCntList.crend(); ++it)
				if (IsTopEdCntUnderMouse(*it))
					count++;

			_ASSERTE(count <= 1);
		}
#endif

		if (g_currentEdCnt && IsTopEdCntUnderMouse(g_currentEdCnt))
			return g_currentEdCnt;

		if (!g_EdCntList.empty())
		{
			AutoLockCs l(g_EdCntListLock);
			for (auto it = g_EdCntList.crbegin(); it != g_EdCntList.crend(); ++it)
				if (IsTopEdCntUnderMouse(*it))
					return *it;
		}

		return EdCntPtr();
	}

	bool IsVsTextEditPane(HWND wnd)
	{
		if (!gShellAttr->IsDevenv() || gShellAttr->IsDevenv10OrHigher())
			return false;

		char buff[20];

		return wnd && ::GetClassNameA(wnd, buff, 20) && StrCmpA(buff, "VsTextEditPane") == 0;
	}

	bool HandleMiddleUpDown(MSG* msg)
	{
		try
		{
			// [case: 119698] revert changes for [case: 118070]
			// 			// This method is necessary also for .NET implementation of mouse commands
			// 			// but needed only in case when menu is already open and for WM_MBUTTONDOWN
			// 			if (gShellAttr->IsDevenv15OrHigher() &&
			// 				(msg->message != WM_MBUTTONDOWN || GetCapture() == nullptr))
			// 				return false;

			if (msg->message == WM_MBUTTONDOWN || msg->message == WM_MBUTTONUP)
			{
				if ((msg->wParam & MK_VA_FROMHOOK) || // ignore messages from this hook
				    !Psettings->mMouseClickCmds.get(
				        (DWORD)VaMouseCmdBinding::MiddleClick) || // ignore messages if middle click has no binding
				    !VsHasFocus())                                // ignore messages if IDE does not own focus
				{
					return false;
				}

				// filter out unwanted messages
				if ((msg->message == WM_MBUTTONDOWN && !(msg->wParam & MK_MBUTTON)) ||
				    (msg->message == WM_MBUTTONUP && (msg->wParam & MK_MBUTTON)))
				{
					msg->message = WM_NULL;
					return true;
				}

				CPoint pt;
				::GetCursorPos(&pt);

				const HWND hWndPass = ::WindowFromPoint(pt);
				const bool isWpfIde = gShellAttr->IsDevenv10OrHigher();
				const EdCntPtr edMouse = TopEdCntUnderMouse();
				const bool isVsTextEditor = edMouse || IsVsTextEditPane(hWndPass);

				if (hWndPass)
				{
					// 	 				ThemeUtils::TraceFrameDbgPrintMSG(msg->message, msg->wParam, msg->lParam);
					// 	 				ThemeUtils::TraceFramePrint("MsgWnd:    %x", CWnd::FromHandle(msg->hwnd));
					// 	 				ThemeUtils::TraceFramePrint("Capture:   %x", CWnd::GetCapture());
					// 	 				ThemeUtils::TraceFramePrint("WndCursor: %x",
					// CWnd::FromHandle(::WindowFromPoint(pt))); ThemeUtils::TraceFramePrint("APIFocus: %x",
					// CWnd::GetFocus()); 	 				ThemeUtils::TraceFramePrint("EdMouse:   %x f:%d",
					// edMouse.get(), edMouse ? edMouse->HasFocus() : 0);
					// ThemeUtils::TraceFramePrint("EdCurrent: %x f:%d", g_currentEdCnt.get(), g_currentEdCnt ?
					// g_currentEdCnt->HasFocus() : 0);
					// 	 				ThemeUtils::TraceFramePrint("-------------------------------------");

					// try to close menu capturing mouse or skip if not our menu
					if (msg->hwnd == ::GetCapture())
					{
						if (PopupMenuXP::IsMenuActive())
						{
							PopupMenuXP::CancelActiveMenu();

							// [case: 94287] necessary for WPF
							// re-posting message causes that WpfTextView.VisualElement.IsMouseOver works,
							// else WPF still thinks that mouse is being captured and does not work
							if (isWpfIde && hWndPass != msg->hwnd)
							{
								CPoint cpt = pt;
								::ScreenToClient(hWndPass, &cpt);
								::PostMessage(hWndPass, msg->message, msg->wParam, MAKELPARAM(cpt.x, cpt.y));
								return false;
							}
						}
						else
						{
							return false;
						}

						if (g_pMiniHelpFrm && ::IsChild(g_pMiniHelpFrm->m_hWnd, msg->hwnd))
						{
							::ReleaseCapture();
						}
					}

					// IF:   TextView under mouse is not tracked by VA
					// OR:   EdCnt under mouse is NOT current EdCnt
					// OR:   EdCnt under mouse does not have focus
					// OR:   EdCnt under mouse is Win32 and has focus only few ticks
					// THEN: let VA know that we don't want execute the command

					if ((!edMouse && (isWpfIde || isVsTextEditor)) || // mouse is over view not tracked by VA
					    (edMouse && edMouse != g_currentEdCnt) ||     // mouse is over view that is not currentEdCnt
					    (edMouse && !edMouse->HasFocus()) ||          // mouse is over view that hasn't focus
					    (!isWpfIde && focusTracker.in_limit(edMouse.get()))) // [Win32] workaround for pre-focused panes
					{
						msg->wParam |= MK_VA_NOTCMD; // Not command!
					}

					// pass message to window under cursor

					::ScreenToClient(hWndPass, &pt);

					// Must be this way (posted to hWndPass) - at least in WPF IDEs,
					// because sometimes TextView is not wrapped by EdCnt and in
					// such case, IDE must handle the middle click event and focus it,
					// which causes the EdCnt creation for us, so we can work then with it.
					::PostMessage(hWndPass, msg->message, msg->wParam | MK_VA_FROMHOOK, MAKELPARAM(pt.x, pt.y));
				}

				msg->message = WM_NULL;
				return true;
			}
		}
		catch (const std::exception& ex)
		{
			const char* what = ex.what();

			_ASSERTE(!"Unhandled exception in HandleMiddleUpDown!");

			vLog("MiddleClickHandler: Exception caught in HandleMiddleUpDown: %s", what);
		}
		catch (...)
		{
			_ASSERTE(!"Unhandled exception in HandleMiddleUpDown!");

			Log("MiddleClickHandler: Exception caught in HandleMiddleUpDown!");
		}

		return false;
	}

	// [case: 91514] Eats WM_RBUTTONUP messages with MK_SHIFT
	// and calls directly only OnRButtonUp of EdCtrl.
	// Only applied when ShiftRightClick mouse command has binding.
	bool EatShiftRightButtonUp(MSG* msg)
	{
		try
		{
			// [case: 119698] revert changes for [case: 118070]
			// 			if (gShellAttr->IsDevenv15OrHigher())
			// 				return false;

			if (msg->message == WM_RBUTTONUP)
			{
				if (!Psettings->mMouseClickCmds.get(
				        (DWORD)VaMouseCmdBinding::ShiftRightClick) || // ignore messages if shift r-click has no binding
				    !(GET_KEYSTATE_WPARAM(msg->wParam) & MK_SHIFT) || // ignore messages if shift not in modifiers mask
				    !VsHasFocus())                                    // ignore messages if IDE does not own focus
				{
					return false;
				}

				const EdCntPtr edMouse = TopEdCntUnderMouse();

				_ASSERTE(!edMouse || edMouse == g_currentEdCnt);

				if (edMouse && edMouse == g_currentEdCnt)
				{
					// directly execute method of EdCtrl
					CPoint pt;
					::GetCursorPos(&pt);
					edMouse->vScreenToClient(&pt);
					edMouse->OnRButtonUp(GET_KEYSTATE_WPARAM(msg->wParam), pt);
					msg->message = WM_NULL; // eat message / avoid IDE's context menu to show
					return true;
				}
			}
		}
		catch (const std::exception& ex)
		{
			const char* what = ex.what();

			_ASSERTE(!"Unhandled exception in EatShiftRightButtonUp!");

			vLog("EatShiftRightButtonUp: Exception caught in EatShiftRightButtonUp: %s", what);
		}
		catch (...)
		{
			_ASSERTE(!"Unhandled exception in EatShiftRightButtonUp!");

			Log("EatShiftRightButtonUp: Exception caught in EatShiftRightButtonUp!");
		}

		return false;
	}

	int mWheelSum = 0;
	DWORD mLastTime = 0;
	bool HandleWheel(MSG* msg)
	{
		// [case: 91013] Ctrl + Wheel and Ctrl + Shift + Wheel overridable to use Smart Select commands or to do nothing

		// [case: 119698] revert changes for [case: 118070]
		if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher() /*|| gShellAttr->IsDevenv15OrHigher()*/)
			return false;

		DWORD modifiers = GET_KEYSTATE_WPARAM(msg->wParam);
		const short delta = GET_WHEEL_DELTA_WPARAM(msg->wParam);

		if (msg->message == WM_MOUSEWHEEL && (modifiers == MK_CONTROL || modifiers == MK_CONTROL + MK_SHIFT) &&
		    (GetKeyState(VK_MENU) & 0x1000) == 0)
		{
			auto binding = (modifiers & MK_SHIFT) ? VaMouseWheelCmdBinding::CtrlShiftMouseWheel
			                                      : VaMouseWheelCmdBinding::CtrlMouseWheel;

			// if user has set zoom (as by default), do nothing with message
			if (Psettings->mMouseWheelCmds.get((DWORD)binding) == (DWORD)VaMouseWheelCmdAction::Zoom)
				return false; // do default action

			EdCntPtr ed(g_currentEdCnt);

			if (!ed)
				return false;

			if (TopEdCntUnderMouse() == ed)
			{
				if (msg->time > mLastTime)
				{
					mLastTime = msg->time;

					if (mWheelSum * delta > 0)
						mWheelSum += delta;
					else
						mWheelSum = delta;

					if (abs(mWheelSum) >= WHEEL_DELTA)
					{
						if (!ed->OnMouseWheelCmd(binding, mWheelSum, msg->pt))
						{
							_ASSERTE(!"Unknown mouse wheel command!");
						}
						mWheelSum = 0;
					}
				}

				msg->message = WM_NULL; // eat message

				return true;
			}
		}

		return false;
	}

	bool HandleDPIWorkaround(MSG* msg)
	{
		try
		{
			if (msg->message == wm_do_multiple_dpis_workaround_cleanup)
			{
				msg->message = WM_NULL;
				DoMultipleDPIsWorkaroundCleanup(msg->wParam);
				return true;
			}
		}
		catch (const std::exception& ex)
		{
			const char* what = ex.what();

			_ASSERTE(!"Unhandled exception in HandleDPIWorkaround!");

			vLog("HandleDPIWorkaround: Exception caught in HandleDPIWorkaround: %s", what);
		}
		catch (...)
		{
			_ASSERTE(!"Unhandled exception in HandleDPIWorkaround!");

			Log("HandleDPIWorkaround: Exception caught in HandleDPIWorkaround!");
		}

		return false;
	}
};

static GetMsgHooks s_GetMsgHooks;
#endif

class VSNetExpBox : public CWnd
{
  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		ScopedIncrement si(&sRecurseCnt);
		if (sRecurseCnt > 100)
		{
			// [case: 58595]
			// build 1850 wer event id -1811206076
			// build 1859 wer event id -1646419783
			vLog("WARN: VNEB:WP: short circuit recursion at msg %x", message);
			_ASSERTE(!"is this recursion in VSNetExpBox::WindowProc?");
			return FALSE;
		}

		if (message == VAM_ISSUBCLASSED)
			return TRUE;
		if (message == WM_WINDOWPOSCHANGED)
		{
			LPWINDOWPOS wpos = (LPWINDOWPOS)lParam;
			if (wpos->cy > 25)
			{
				CRect rc;
				GetWindowRect(&rc);
				rc.bottom = rc.top + 25;
				Log("VSNEB:WP");
			}
		}
		if (message == WM_SHOWWINDOW)
		{
			extern BOOL ignoreExpand;
			if (g_currentEdCnt && !ignoreExpand)
			{
				CRect rc;
				GetWindowRect(&rc);
				rc.InflateRect(2, 2);
				EdCntPtr ed = g_currentEdCnt;
				if (ed && (CWnd::WindowFromPoint(rc.TopLeft())->GetSafeHwnd() == ed->GetSafeHwnd() ||
				           CWnd::WindowFromPoint(rc.BottomRight())->GetSafeHwnd() == ed->GetSafeHwnd() ||
				           CWnd::WindowFromPoint(CPoint(rc.right, rc.top))->GetSafeHwnd() == ed->GetSafeHwnd() ||
				           CWnd::WindowFromPoint(CPoint(rc.left, rc.bottom))->GetSafeHwnd() == ed->GetSafeHwnd()))
				{
					// only set timer if this VSNetExpBox was for one of our windows.
					// This is not the best test but is good enough - checks to see if
					// EdCnt is behind any corner of the popup.
					// vs2005 uses these popups in the find in files dlg.
					// without this check, we modified the edit window while
					// user was typing in find in files dlg [case: 1011].
					ed->SetTimer(VSNET_EXPANDED_TIMER, 100, NULL);
				}
			}
			ignoreExpand = FALSE;
		}
		if (message == WM_NCDESTROY)
		{
			CWnd::WindowProc(message, wParam, lParam);
			gVsSubWndVect.Delete(this);
			return TRUE;
		}

		return CWnd::WindowProc(message, wParam, lParam);
	}

  public:
	VSNetExpBox(HWND h)
	{
		SubclassWindow(h);
		ModifyStyleEx(0, WS_VA_COLOR, 0);
	}

	~VSNetExpBox()
	{
		if (m_hWnd && IsWindow(m_hWnd))
			UnsubclassWindow();
	}

  private:
	static LONG sRecurseCnt;
};

LONG VSNetExpBox::sRecurseCnt = 0;

extern EdCnt* g_paintingEdCtrl;

class VSNetToolTip : public VAColorWrapperSubclass<CWnd, PaintType::VS_ToolTip>
{
	static LONG sRecurseCnt; // [case: 47826] catch ast induced recursion

  public:
	VSNetToolTip(HWND h, bool shouldColor) : VAColorWrapperSubclass<CWnd, PaintType::VS_ToolTip>(h)
	{
		if (!shouldColor)
			this->m_colorType = PaintType::DontColor;
		// [case: 49022]
		if (g_CompletionSet && g_CompletionSet->IsExpUp(NULL))
			g_CompletionSet->Reposition();
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		ScopedIncrement si(&sRecurseCnt);
		if (sRecurseCnt > 40)
		{
			_ASSERTE(!"VSNetTooltip WindowProc out of control recursion");
			return 0;
		}

		// Handle Window placement
		if (message == WM_WINDOWPOSCHANGED)
		{
			//			LPWINDOWPOS wpos = (LPWINDOWPOS) lParam;
			if (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->m_hWnd &&
			    ::IsWindow(g_ScreenAttrs.m_VATomatoTip->m_hWnd) && g_ScreenAttrs.m_VATomatoTip->IsWindowVisible())
			{
				CRect intersection;
				CRect ttRc;
				CRect tomatoRc;
				GetWindowRect(&ttRc);
				g_ScreenAttrs.m_VATomatoTip->GetWindowRect(&tomatoRc);
				if (intersection.IntersectRect(ttRc, tomatoRc) && !intersection.IsRectEmpty())
				{
					ttRc.MoveToX(tomatoRc.right + 1);
					// gmit: 67174: already seems to be working fine, so, GetDesktopEdges is not updated
					EdCntPtr curEd = g_currentEdCnt;
					if (ttRc.right >= g_FontSettings->GetDesktopEdges(curEd ? curEd->m_hWnd : NULL).right)
					{
						GetWindowRect(&ttRc);
						ttRc.MoveToY(tomatoRc.bottom + 1);
					}
					MoveWindow(&ttRc);
				}
			}
		}
		else if (message == VAM_ISSUBCLASSED)
			return TRUE;
		else if (message == WM_DESTROY)
		{
			CWnd::WindowProc(message, wParam, lParam);
			gVsSubWndVect.Delete(this);
			return TRUE;
		}
		return __super::WindowProc(message, wParam, lParam);
	}
};

LONG VSNetToolTip::sRecurseCnt = 0;

void VsSubWndVect::SubclassTooltip(HWND h, bool shouldColor)
{
	VSNetToolTip* wnd = new VSNetToolTip(h, shouldColor);
	mVsWnds.push_back(wnd);
}

void VsSubWndVect::SubclassBox(HWND h)
{
	VSNetExpBox* wnd = new VSNetExpBox(h);
	mVsWnds.push_back(wnd);
}

void VsSubWndVect::Delete(CWnd* item)
{
	typedef std::vector<CWnd*>::iterator WndIter;
	WndIter iter = std::find(mVsWnds.begin(), mVsWnds.end(), item);
	if (iter != mVsWnds.end())
	{
		delete *iter;
		mVsWnds.erase(iter);
	}
}

void VsSubWndVect::Shutdown()
{
	const size_t count = mVsWnds.size();
	for (size_t i = 0; i < count; i++)
	{
		CWnd* wnd = mVsWnds[i];
		delete wnd;
		mVsWnds[i] = NULL;
	}
}

VsSubWndVect gVsSubWndVect;

BOOL HasVsNetPopup(BOOL ignoreToolTips)
{
	_ASSERTE(g_mainThread == ::GetCurrentThreadId());
	if (gShellAttr->IsDevenv10OrHigher())
	{
		if (gHasMefExpansion)
			return TRUE;
		if (!ignoreToolTips)
		{
			if (gHasMefParamInfo)
				return TRUE;

			if (gHasMefQuickInfo)
			{
				if (!gShellAttr->IsDevenv15u6OrHigher())
					return TRUE;

				// [case: 114018]
				// IsQuickInfoActive is unreliable, so only use in 15.6
				EdCntPtr ed(g_currentEdCnt);
				if (ed && ed->IsQuickInfoActive())
					return TRUE;

				gHasMefQuickInfo = false;
			}
		}
		// We need to continue to see if ReSharper's listbox is displayed. case=34592
	}

	if (!gShellAttr->IsDevenv())
		return FALSE;

	DWORD thisProc, wProc;
	::GetWindowThreadProcessId(MainWndH, &thisProc);
	// gmit: windows enumeration like this isn't safe nor recommended by MSDN - EnumDesktopWindows should be used
	for (HWND w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = ::GetWindow(w, GW_HWNDNEXT))
	{
		wProc = 0;
		::GetWindowThreadProcessId(w, &wProc);
		if (!wProc || wProc != thisProc)
			continue;

		const WTString cls = GetWindowClassString(w);
		if (cls == "VsCompletorPane")
		{
			if (Psettings->m_enableVA && !::SendMessage(w, VAM_ISSUBCLASSED, 0, 0))
				gVsSubWndVect.SubclassBox(w);
			if (IsWindowVisible(w))
				return TRUE;
		}
		else if (cls == "Auto-Suggest Dropdown")
		{
			if (Psettings->m_enableVA && !::SendMessage(w, VAM_ISSUBCLASSED, 0, 0))
				gVsSubWndVect.SubclassBox(w);
			if (IsWindowVisible(w))
				return TRUE;
		}
		else if (cls == "VsTipWindow" || cls == "VcTipWindow" || cls == "VSDebugger CDataTipWnd")
		{
			if (Psettings->m_enableVA && !::SendMessage(w, VAM_ISSUBCLASSED, 0, 0))
			{
				bool shouldColor = (cls != "VSDebugger CDataTipWnd");
				gVsSubWndVect.SubclassTooltip(w, shouldColor);
			}
			if (!ignoreToolTips && IsWindowVisible(w))
				return TRUE;
		}
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
		// ReSharper Members List WindowsForms10.LISTBOX.app6
		else if (cls.Find("WindowsForms10.Window.8.app") != -1)
		{
			WTString cap = GetWindowTextString(w);
			if (cap == "ListForm" || cap == "LookupWindow")
			{
				EdCntPtr curEd = g_currentEdCnt;
				if (curEd)
					Psettings->UsingResharperSuggestions(curEd->m_ftype, true);

				if (Psettings->m_enableVA && !::SendMessage(w, VAM_ISSUBCLASSED, 0, 0))
					gVsSubWndVect.SubclassBox(w);
				if (IsWindowVisible(w))
				{
					if (curEd)
						curEd->ClearAllPopups(true);
					return TRUE;
				}
			}
		}
#endif
	}

	return FALSE;
}

extern void RTFCopy(EdCnt* ed, WTString* txtToFormat = NULL);

static void FindNextReferenceCB(LPVOID next)
{
	EdCntPtr curEd = g_currentEdCnt;
	if (curEd)
		curEd->FindNextReference(next != NULL);
}

class ReloadLocalDic : public ParseWorkItem
{
	EdCntPtr mEd;
	int mFtype;
	const CStringW mFilename;
	WTString mBuf;
	static MultiParsePtr mActiveReloadMp;
	static CCriticalSection mActiveReloadLock;

  public:
	ReloadLocalDic(EdCntPtr ed)
	    : ParseWorkItem("ReloadLocals"), mEd(ed), mFtype(ed->m_ftype), mFilename(ed->FileName())
	{
		// calling stop from main thread, so that parseThread can
		// handle new job more quickly
		// hmmm - also, calling StopPreviousReload will have no effect if called
		// from the ParserThread since all Reloads happen on the same thread.
		StopPreviousReload();
		if (ed->Modified())
			mBuf = ed->GetBuf();
	}

	virtual void DoParseWork();

	virtual bool CanRunNow() const
	{
		return !StopIt && !GlobalProject->IsBusy();
	}

	static void StopPreviousReload()
	{
		try
		{
			AutoLockCs l(mActiveReloadLock);
			if (mActiveReloadMp)
				mActiveReloadMp->Stop();
		}
		catch (...)
		{
			VALOGEXCEPTION("RLD::SPR:");
		}
	}
};

MultiParsePtr ReloadLocalDic::mActiveReloadMp;
CCriticalSection ReloadLocalDic::mActiveReloadLock;

void ReloadLocalDic::DoParseWork()
{
	if (mFilename.IsEmpty())
		return;

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		auto mp = MultiParse::Create(mFtype);
		NestedTrace tr("Load file symbols: " + WTString(mFilename), true);
		{
			AutoLockCs l(mActiveReloadLock);
			mActiveReloadMp = mp;
		}
		mp->SetCacheable(TRUE);
		// opened file not in project, include globals into global dictionary.
		if (mFtype == Header || mFtype == CS || Is_VB_VBS_File(mFtype))
		{
			// include into global namespace if needed
			if (!mp->IsIncluded(mFilename))
				mp->FormatFile(mFilename, V_INPROJECT, ParseType_Globals, false,
				               mBuf.GetLength() ? mBuf.c_str() : NULL);
		}

		// Load misc/*.js, *.h, *... files
		::LoadInstallDirMiscFiles(mFtype);

		mp->m_showRed = TRUE;
		if (mBuf.GetLength())
			mp->FormatFile(mFilename, V_INPROJECT | V_INFILE, ParseType_Locals, false, mBuf.c_str());
		else
			mp->FormatFile(mFilename, V_INPROJECT | V_INFILE, ParseType_Locals, false);

		mp->m_showRed = TRUE;
		if (mp->IsStopped())
		{
			// other window canceled this one, nuke mp
			AutoLockCs l(mActiveReloadLock);
			mActiveReloadMp = nullptr;
		}
		else
		{
			if (::IsWindow(mEd->GetSafeHwnd()))
			{
				if (mFilename == mEd->FileName()) // make sure edcnt still has the same file open
					mEd->SetNewMparse(mp, TRUE);
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("RLD::DPW2:");
		_ASSERTE(!"caught exception in ReloadLocalDic");
	}
#endif // !SEAN

	AutoLockCs l(mActiveReloadLock);
	mActiveReloadMp = nullptr;
}

CCriticalSection g_EdCntListLock;
std::vector<EdCntPtr> g_EdCntList;

EdCntPtr GetOpenEditWnd(HWND hWnd)
{
	AutoLockCs l(g_EdCntListLock);
	for (EdCntPtr ed : g_EdCntList)
	{
		if (ed && hWnd == ed->m_hWnd)
			return ed;
	}
	return nullptr;
}

EdCntPtr GetOpenEditWnd(const LPCWSTR file)
{
	AutoLockCs l(g_EdCntListLock);
	for (EdCntPtr ed : g_EdCntList)
	{
		if (ed && _wcsicmp(ed->FileName(), file) == 0)
			return ed;
	}
	return nullptr;
}

EdCntPtr GetOpenEditWnd(const CStringW& file)
{
	AutoLockCs l(g_EdCntListLock);
	for (EdCntPtr ed : g_EdCntList)
	{
		if (ed && _wcsicmp(ed->FileName(), file) == 0)
			return ed;
	}
	return nullptr;
}

#if !defined(RAD_STUDIO)
EdCntPtr GetOpenEditWnd(IVsTextBuffer* textBuffer)
{
	if (!textBuffer)
		return nullptr;

	AutoLockCs l(g_EdCntListLock);
	for (EdCntPtr ed : g_EdCntList)
	{
		if (ed && ed->m_IVsTextView)
		{
			CComPtr<IVsTextLines> vsTextLines;
			ed->m_IVsTextView->GetBuffer(&vsTextLines);
			if (vsTextLines)
			{
				CComPtr<IUnknown> vsTextLinesUnk;
				vsTextLines->QueryInterface(IID_IUnknown, (void**)&vsTextLinesUnk);

				CComPtr<IUnknown> textBufferUnk;
				textBuffer->QueryInterface(IID_IUnknown, (void**)&textBufferUnk);

				if (vsTextLinesUnk == textBufferUnk)
				{
					return ed;
				}
			}
		}
	}

	return nullptr;
}

void SendMessageToAllEdcnt(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	AutoLockCs l(g_EdCntListLock);
	for (EdCntPtr ed : g_EdCntList)
	{
		if (ed)
			ed->SendMessage(Msg, wParam, lParam);
	}
}
#endif

CStringW GetDefaultLineCommentString()
{
	CStringW cmt;
	for (DWORD idx = 0; idx < Psettings->mLineCommentSlashCount; ++idx)
		cmt += L'/';
	return cmt;
}

//	Edit commands
#define ID_EDIT_DELETE 37000
#define ID_EDIT_DELETE_BACK 37001
#define ID_EDIT_DELETE_WORD_BACK 37002
#define ID_EDIT_TAB 37003
#define ID_EDIT_UNTAB 37004
#define ID_EDIT_SWITCH_OVRMODE 37005
#define ID_EDIT_CHAR_LEFT 37100
#define ID_EDIT_CHAR_RIGHT 37102
#define ID_EDIT_LINE_UP 37108
#define ID_EDIT_LINE_DOWN 37110
int rapidfire = 0;
MarkerIds kMarkerIds;

struct SortAscendingSensitive
{
	_locale_t m_loc_t = _wcreate_locale(LC_ALL, L"");

	bool operator()(const CStringW& a, const CStringW& b) const
	{
		LPCWSTR pA = a;
		LPCWSTR pB = b;
		int len = min(a.GetLength(), b.GetLength());

		for (int i = 0; i < len; i++)
		{
			WCHAR aCh = pA[i];
			WCHAR bCh = pB[i];

			if (aCh == bCh)
				continue;

			if (_iswalpha_l(aCh, m_loc_t) != 0 && _iswalpha_l(bCh, m_loc_t) != 0 &&
			    _iswlower_l(aCh, m_loc_t) != _iswlower_l(bCh, m_loc_t))
				return _iswlower_l(aCh, m_loc_t) == 0;
			else
				return _wcsnicoll_l(&aCh, &bCh, 1, m_loc_t) < 0;
		}

		return a.GetLength() < b.GetLength();
	}
};

struct SortAscendingInsensitive
{
	_locale_t m_loc_t = _wcreate_locale(LC_ALL, L"");

	bool operator()(const CStringW& a, const CStringW& b) const
	{
		LPCWSTR pA = a;
		LPCWSTR pB = b;
		int len = min(a.GetLength(), b.GetLength());

		for (int i = 0; i < len; i++)
		{
			WCHAR aCh = pA[i];
			WCHAR bCh = pB[i];

			if (aCh == bCh)
				continue;

			int cmp = _wcsnicoll_l(&aCh, &bCh, 1, m_loc_t);
			if (cmp == 0)
				continue;
			return cmp < 0;
		}

		return a.GetLength() < b.GetLength();
	}
};

struct SortDescendingSensitive
{
	_locale_t m_loc_t = _wcreate_locale(LC_ALL, L"");

	bool operator()(const CStringW& a, const CStringW& b) const
	{
		LPCWSTR pA = a;
		LPCWSTR pB = b;
		int len = min(a.GetLength(), b.GetLength());

		for (int i = 0; i < len; i++)
		{
			WCHAR aCh = pA[i];
			WCHAR bCh = pB[i];

			if (aCh == bCh)
				continue;

			if (_iswalpha_l(aCh, m_loc_t) != 0 && _iswalpha_l(bCh, m_loc_t) != 0 &&
			    _iswlower_l(aCh, m_loc_t) != _iswlower_l(bCh, m_loc_t))
				return _iswlower_l(aCh, m_loc_t) != 0;
			else
				return _wcsnicoll_l(&aCh, &bCh, 1, m_loc_t) > 0;
		}

		return a.GetLength() > b.GetLength();
	}
};

struct SortDescendingInsensitive
{
	_locale_t m_loc_t = _wcreate_locale(LC_ALL, L"");

	bool operator()(const CStringW& a, const CStringW& b) const
	{
		LPCWSTR pA = a;
		LPCWSTR pB = b;
		int len = min(a.GetLength(), b.GetLength());

		for (int i = 0; i < len; i++)
		{
			WCHAR aCh = pA[i];
			WCHAR bCh = pB[i];

			if (aCh == bCh)
				continue;

			int cmp = _wcsnicoll_l(&aCh, &bCh, 1, m_loc_t);
			if (cmp == 0)
				continue;
			return cmp > 0;
		}

		return a.GetLength() > b.GetLength();
	}
};

void EdCnt::SortSelectedLines()
{
	if (gShellSvc->HasBlockModeSelection(this))
	{
		::MessageBeep(0xffffffff);
		return;
	}

	SortSelectedLinesDlg::eAscDesc ascDesc = SortSelectedLinesDlg::eAscDesc::ASCENDING;
	SortSelectedLinesDlg::eCase eCase = SortSelectedLinesDlg::eCase::SENSITIVE;
	if (Psettings->mEnableSortLinesPrompt)
	{
		SortSelectedLinesDlg dlg(gMainWnd);
		int id = (int)dlg.DoModal();
		if (id == IDCANCEL)
			return;

		ascDesc = dlg.AscDesc;
		eCase = dlg.Case;
	}

	// need to get eofPos before getting p1, p2
	long eofPos = (long)LinePos(NPOS, 0) + GetLine(NPOS).GetLength();
	long pos1 = (long)CurPos(true);
	gShellSvc->SwapAnchor();
	long pos2 = (long)CurPos(true);
	gShellSvc->SwapAnchor();
	if (pos1 > pos2)
		std::swap(pos1, pos2);
	long p1 = (long)LinePos(CurLine((uint)pos1 /*CurPosBegSel()*/));
	long p2 = (long)LinePos(CurLine((uint)pos2 /*CurPos(TRUE)*/ - 2) + 1); // assumes empty last line
	//	if (p1 > p2)
	//	{
	//		gShellSvc->SwapAnchor();
	//		eofPos = (long) LinePos(NPOS, 0) + GetLine(NPOS).GetLength();
	//		p1 = (long) LinePos(CurLine(CurPosBegSel()));
	//		p2 = (long) LinePos(CurLine(CurPos(TRUE)-2) + 1);
	//	}
	SetSelection(p1, p2);
	// check to see if selection worked - might have failed
	// if last line of file was selected but was not empty
	const long p3 = (long)CurPos();
	if (p3 != p2)
		SetSelection(p1, eofPos);
	else
		eofPos = 0;

	CStringW selTxt = GetSelStringW();
	const EolTypes::EolType et = EolTypes::GetEolType(selTxt);
	if (selTxt.IsEmpty())
	{
		::MessageBeep(0xffffffff);
		return;
	}
	const CStringW lnBrk(EolTypes::GetEolStrW(et));
	if (eofPos)
		selTxt += lnBrk;
	WideStrVector ary;
	::WtStrSplitW(selTxt, ary, lnBrk);

	if (ascDesc == SortSelectedLinesDlg::eAscDesc::ASCENDING)
	{
		if (eCase == SortSelectedLinesDlg::eCase::SENSITIVE)
			std::sort(ary.begin(), ary.end(), SortAscendingSensitive());
		else
			std::sort(ary.begin(), ary.end(), SortAscendingInsensitive());
	}
	else
	{
		if (eCase == SortSelectedLinesDlg::eCase::SENSITIVE)
			std::sort(ary.begin(), ary.end(), SortDescendingSensitive());
		else
			std::sort(ary.begin(), ary.end(), SortDescendingInsensitive());
	}

	CStringW sortedTxt;
	for (WideStrVector::const_iterator it = ary.begin(); it != ary.end(); ++it)
		sortedTxt += *it;
	InsertW(sortedTxt, true, noFormat);
}

LRESULT
EdCnt::DefWindowProc(UINT msg, WPARAM p1, LPARAM p2)
{
	static LONG sRecurseCnt = 0;
	ScopedIncrement si(&sRecurseCnt);
	if (sRecurseCnt > 150)
	{
		// [case: 64604]
		vLogUnfiltered("WARN: EC:DWP: short circuit recursion %x", msg);
		_ASSERTE(!"is this recursion in EdCnt::DefWindowProc?");
		return 0;
	}

	if (!Psettings) // case=9610
		return TRUE;

	if (msg > WM_MOUSEFIRST && msg <= WM_MOUSELAST)
		vCatLog("Editor.Events", "VaEventME Ed::Dwp hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_hWnd, msg, (uintptr_t)p1, (uintptr_t)p2);

	//////////////////////////////////////////////////////////////////////////
	// Thread safe commands for refactoring threads
	// We should make these into real methods/handlers if we decide to keep this
	if (msg == WM_VA_THREAD_SETSELECTION)
	{
		SetSelection((long)p1, (long)p2);
		return TRUE;
	}

	if (msg == WM_VA_THREAD_GETBUFFER)
	{
		WTString* rstr = (WTString*)p1;
		*rstr = GetBuf(TRUE);
		return TRUE;
	}
	if (msg == WM_VA_THREAD_GETBUFINDEX)
	{
		int* rval = (int*)p2;
		if (p1 == -1)
			p1 = CurPos();
		*rval = GetBufIndex((long)p1);
		return TRUE;
	}
	if (msg == WM_VA_THREAD_GETCHARPOS)
	{
		CPoint pt(GetCharPos((long)p1));
		*(CPoint*)p2 = pt;
		return TRUE;
	}
	//////////////////////////////////////////////////////////////////////////

	if (gShellIsUnloading)
	{
		UnsubclassWindow();
		ReleaseSelfReferences();
		return FALSE;
	}

	if (msg == VAM_CREATELINEMARKER)
	{
		CreateMarkerMsgStruct* pFoo = (CreateMarkerMsgStruct*)p2;
		if (pFoo)
		{
			long markerId = MARKER_INVISIBLE;
			switch (p1)
			{
			case SA_BRACEMATCH:
				markerId = kMarkerIds.mBraceMatchMarkerId;
				break;
			case SA_MISBRACEMATCH:
				markerId = kMarkerIds.mBraceMismatchMarkerId;
				break;
			case SA_REFERENCE:
			case SA_REFERENCE_AUTO:
				markerId = kMarkerIds.mRefMarkerId;
				break;
			case SA_REFERENCE_ASSIGN:
			case SA_REFERENCE_ASSIGN_AUTO:
				markerId = kMarkerIds.mRefAssignmentMarkerId;
				break;
			case SA_UNDERLINE:
				markerId = kMarkerIds.mErrorMarkerId;
				break;
			case SA_UNDERLINE_SPELLING:
				markerId = kMarkerIds.mSpellingErrorMarkerId;
				break;
			case SA_HASHTAG:
				markerId = kMarkerIds.mHashtagMarkerId;
				break;
			default:
				markerId = MARKER_INVISIBLE;
				break;
			}

			HRESULT hr = pFoo->iLines->CreateLineMarker(markerId, pFoo->iStartLine, pFoo->iStartIndex, pFoo->iEndLine,
			                                            pFoo->iEndIndex, pFoo->pClient, pFoo->ppMarker);
			if (SUCCEEDED(hr))
				return VAM_CREATELINEMARKER;
		}
		return FALSE;
	}

	if (msg == VAM_SET_IVsTextView)
	{
		IVsTextView* pView = (IVsTextView*)p1;
		SetIvsTextView(pView);
		_ASSERTE(m_IVsTextView == pView);

		MarkerIds* pMkrs = (MarkerIds*)p2;
		if (pMkrs)
			kMarkerIds = *pMkrs;

		FindReferencesPtr globalRefs(g_References);
		if (globalRefs)
			globalRefs->Redraw();

		// return msg to ACK receipt
		return VAM_SET_IVsTextView;
	}

	if (msg == VAM_OnChangeScrollInfo)
	{
		long iBar = (long)p1;
		long iFirstVisibleUnit = (long)p2;

		if (iBar == SB_VERT)
		{
			if (gShellAttr->IsDevenv10OrHigher())
			{
				// Need to convert FirstVisibleUnit to FirstVisibleLine in 2010
				long first, last;
				iFirstVisibleUnit =
				    m_firstVisibleLine; // save m_firstVisibleLine, because GetFirstVisibleLine may update it.
				GetVisibleLineRange(first, last);
				if (iFirstVisibleUnit == first)
				{
					// ID_UNDERLINE_ERRORS fixes navbar when enabling VA. case=44542
					KillTimer(ID_UNDERLINE_ERRORS);
					if (!RefactoringActive::IsActive() && !m_FileHasTimerForFullReparse)
						SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
					return 1; // no scroll, don't ClearAllPopups() below, (word wrap: case=40071)
				}
				iFirstVisibleUnit = first; // Set to firstvisibleLINE
			}
		}

		extern int g_screenXOffset;
		extern int g_screenMargin;
		if (iBar == SB_HORZ)
		{
			int screenXOffset = g_screenMargin - (iFirstVisibleUnit * g_vaCharWidth['z']);
			if (screenXOffset == g_screenXOffset)
				return 1; // no scroll, don't ClearAllPopups() below, (word wrap: case=40784)
			g_screenXOffset = screenXOffset;
		}
		else if (!g_screenXOffset) // Initialize first time case 16266
			g_screenXOffset = g_screenMargin;

		if (gShellAttr->IsDevenv10OrHigher())
		{
			// Clear all popups when scrolling, so we don't have to do the whole caret testing
			ClearAllPopups();
			g_CompletionSet->Dismiss();
			KillTimer(ID_UNDERLINE_ERRORS);
			// set timer for new underlining with VAParse
			if (!RefactoringActive::IsActive() && !m_FileHasTimerForFullReparse)
				SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
			KillTimer(ID_TIMER_GETSCOPE);
		}

		return TRUE;
	}

	if (msg != WM_VA_SET_DB && msg != WM_VA_GET_EDCNTRL && (msg < WM_VA_FIRST || msg > WM_VA_LAST) && msg != DSM_WIZ_GOTODEF && msg != DSM_FINDCURRENTWORDBWD && msg != DSM_FINDCURRENTWORDFWD)
		DISABLECHECKCMD(CWnd::DefWindowProc(msg, p1, p2));

	////////////
	/*
	char buf[512];
	sprintf(buf, "WindowProc 0x%x, 0x%x, 0x%x\n",
	msg, p1, p2);
	SetStatus(buf);
	*/

	extern EdCnt* g_paintingEdCtrl;
	g_paintingEdCtrl = this;

	if (msg == VAM_DSM_COMMAND)
	{
		LPCSTR p = (LPCSTR)p2;

		if (p)
		{
			if (WM_VA_PRENETCMD == p1)
			{
				gUserExpandCommandState = uecsNone;

				if ('F' == p[0])
				{
					if (StartsWith(p, "File.AdvancedSaveOptions", FALSE))
						mEolTypeChecked = false; // [case: 95608]
				}
				else if ('E' == p[0])
				{
					if (StartsWith(p, "Edit.Find", FALSE))
					{
						Psettings->m_incrementalSearch = false;
						// [case: 56607]
						if (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible())
							DisplayToolTipArgs(false);
						if (Psettings->mHighlightFindReferencesByDefault && strcmp("Edit.FindNextSelected", p) == 0)
							Invalidate(TRUE);

						// Edit.FindNext / Edit.FindPrevious
						// not sure that this is a good idea, so leaving commented out for now.
						// NavAdd(FileName(), CurPos());
					}
					else if (StartsWith(p, "Edit.Replace", FALSE))
					{
						// [case: 55943]
						Psettings->m_incrementalSearch = true;
					}
					else if (StartsWith(p, "Edit.ListMembers"))
					{
						gUserExpandCommandState = uecsListMembersBefore;
						gLastUserExpandCommandState = gUserExpandCommandState;
						gLastUserExpandCommandTick = ::GetTickCount();
					}
					else if (StartsWith(p, "Edit.CompleteWord"))
					{
						gUserExpandCommandState = uecsCompleteWordBefore;
						gLastUserExpandCommandState = gUserExpandCommandState;
						gLastUserExpandCommandTick = ::GetTickCount();
					}
					else if (StartsWithNC(p, "Edit.GoTo", FALSE))
					{
						NavAdd(FileName(), CurPos());
					}
				}
				else if ('D' == p[0])
				{
					if (strncmp("Debug.Step", p, 10) == 0)
						NavAdd(FileName(), CurPos(), TRUE); // Allow navigation back to last debug step command
				}
			}
			else if (WM_VA_POSTNETCMD == p1)
			{
				gUserExpandCommandState = uecsNone;
				if ('E' == p[0])
				{
					if (StartsWith(p, "Edit.Find", FALSE))
					{
						// [case: 56607]
						if (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible())
							DisplayToolTipArgs(false);

						ClearAllPopups(true); // [case: 58642]
					}
					else if (StartsWith(p, "Edit.Replace", FALSE))
					{
						// [case: 55943]
						Psettings->m_incrementalSearch = true;
					}
					else if (StartsWith(p, "Edit.ListMembers"))
					{
						gUserExpandCommandState = uecsListMembersAfter;
						gLastUserExpandCommandState = gUserExpandCommandState;
						gLastUserExpandCommandTick = ::GetTickCount();
					}
					else if (StartsWith(p, "Edit.CompleteWord"))
					{
						gUserExpandCommandState = uecsCompleteWordAfter;
						gLastUserExpandCommandState = gUserExpandCommandState;
						gLastUserExpandCommandTick = ::GetTickCount();
					}
					else if (StartsWith(p, "Edit.LineCut"))
					{
						// can't OpenClipboard right now, so schedule it for later
						SetTimer(IDT_CopyClipboard, 25, NULL);
					}
				}
			}
		}
	}

	// [case: 109205]
	if (msg == WM_VA_UE_ENABLE_UMACRO_INDENT_FIX_IF_RELEVANT)
		UeEnableUMacroIndentFixIfRelevant();
	else if (msg == WM_VA_UE_DISABLE_UMACRO_INDENT_FIX_IF_ENABLED)
		UeDisableUMacroIndentFixIfEnabled();
	//////////////

	if (msg == (WM_APP + 600))
	{
		_ASSERTE(!"who sent this message?");
		MSG* m = (MSG*)p2;

		if (m->message == WM_KEYDOWN)
		{
			return VAProcessKey((uint)m->wParam, 0, 0xdead);
		}

		return FALSE;
	}

	if (msg == WM_VA_HANDLEKEY)
	{
		if (!Psettings->m_enableVA)
			return FALSE;

		// [case: 148382] see the two other references to this case as well
		if (p1 == ID_EDIT_SELECT_ALL)
		{
			if (p2 == 1) // BeforeExecute
			{
				WTString selection = GetSelString();
				WTString buf = GetBuf();
				selection.TrimRight();
				buf.TrimRight();

				if (selection == buf)
				{
					RestoreCaretPosition = true;
				}
				else
				{
					long beg, end;
					GetSel2(beg, end);
					SavedCaretPosition = beg;
				}
			}
			else if (p2 == 2) // AfterExecute
			{
				if (RestoreCaretPosition)
				{
					RestoreCaretPosition = false;
					SetSel(SavedCaretPosition, SavedCaretPosition);
				}
			}
		}

		if (p1 == ID_EDIT_PASTE)
		{
			if (Psettings && Psettings->mUnrealEngineCppSupport && Psettings->mDisableUeAutoforatOnPaste && g_IdeSettings)
			{
				// [case: 141666] if paste text contains UE special words like UCLASS, USTRUCT etc., disable VS formating of pasted text
				if (p2 == 0)
				{
					// paste about to happen, disable auto format on paste if needed
					CStringW clipboardText = ::GetClipboardText(m_hWnd);
					if (clipboardText.Find(L"UCLASS(") != -1 ||
					    clipboardText.Find(L"USTRUCT(") != -1 ||
					    clipboardText.Find(L"UPROPERTY(") != -1 ||
					    clipboardText.Find(L"UFUNCTION(") != -1 ||
					    clipboardText.Find(L"UENUM(") != -1 ||
					    clipboardText.Find(L"UDELEGATE(") != -1 ||
					    clipboardText.Find(L"GENERATED_UCLASS_BODY(") != -1 ||
					    clipboardText.Find(L"GENERATED_USTRUCT_BODY(") != -1 ||
					    clipboardText.Find(L"GENERATED_UINTERFACE_BODY(") != -1 ||
					    clipboardText.Find(L"GENERATED_IINTERFACE_BODY(") != -1)
					{
						mUeLastAutoFormatOnPaste2Value = g_IdeSettings->GetEditorOption("C/C++ Specific", "AutoFormatOnPaste2");
						if (!mUeLastAutoFormatOnPaste2Value.IsEmpty() && mUeLastAutoFormatOnPaste2Value != "0")
						{
							g_IdeSettings->SetEditorOption("C/C++ Specific", "AutoFormatOnPaste2", "0");
							mUeRestoreAutoFormatOnPaste2 = true;
						}
					}
				}
				else
				{
					// paste just happened, enable auto format on paste if disabled previously
					if (mUeRestoreAutoFormatOnPaste2 && !mUeLastAutoFormatOnPaste2Value.IsEmpty())
					{
						g_IdeSettings->SetEditorOption("C/C++ Specific", "AutoFormatOnPaste2", mUeLastAutoFormatOnPaste2Value);
						mUeLastAutoFormatOnPaste2Value.Empty();
						mUeRestoreAutoFormatOnPaste2 = false;
					}
				}

				// return here because we don't want any formating either from VS or VA to happen in this scenario
				return FALSE;
			}
			
			if (IsFeatureSupported(Feature_FormatAfterPaste, m_ScopeLangType) && g_VATabTree)
			{
				if (::HasUnformattableMultilineCStyleComment(::GetClipboardText(m_hWnd)))
					SetStatus("No 'Format after paste' due to block comment in pasted text");
				else
				{
					static long pastePos = 0;
					if (p2 == 0)
					{
						// paste about to happen
						pastePos = GetSelBegPos();
					}
					else
					{
						// paste just happened
						if (LineFromChar(pastePos) != LineFromChar((long)CurPos()))
						{
							SetSelection(pastePos, (long)CurPos());
							gShellSvc->FormatSelection();
							SetPos(CurPos());
						}

						GetBuf(TRUE);
						CPoint pt(pastePos, (long)CurPos());
						Reparse();
						OnModified(TRUE); // Launch reparse thread
					}
				}
			}
		}

		if (p1 == DSM_PARAMINFO && !p2) // about to show
		{
			// Suggest args template if ctrl+shift+space in definition of method
			MultiParsePtr mp(GetParseDb());

			if (IsCFile(m_ftype) && mp->m_inParamList && !mp->m_argCount && !HasVsNetPopup(FALSE))
			{
				DisplayToolTipArgs(TRUE);
				return TRUE;
			}
		}

		if (p1 == DSM_PARAMINFO && p2)
		{
			// theirs should be up, if not display ours
			SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
		}

		if (p1 == DSM_AUTOCOMPLETE || p1 == DSM_LISTMEMBERS)
		{
			const WTString selStr(GetSelString());
			if (gShellSvc->HasBlockModeSelection(this))
			{
				// [case: 64604]
				return FALSE;
			}

			const WTString bb(GetBuf());
			if (StartsWith(selStr, VA_AUTOTES_STR, FALSE) ||
			    StartsWith(bb.c_str() + GetBufIndex(bb, (long)CurPos()), VA_AUTOTES_STR, FALSE))
			{
				if (p2)
					RunVAAutomationTest();
				return TRUE;
			}

			if (Psettings->mSuppressAllListboxes)
				return FALSE;
			if (Psettings->mRestrictVaListboxesToC && !(IsCFile(m_ftype)))
				return FALSE;

			WTString cwd = CurWord();
			static uint lp = UINT_MAX;

			if (!p2 && m_ftype == UC && (m_lastScope.GetLength())) // allow text expansion in comments/strings
			{
				// do our list if theirs fails to display?
				if (CurPos() == lp)                       // don't display if they expanded
					CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
				return TRUE;
			}
#ifdef jer
			if (!p2 && m_ftype == JS && (m_lastScope.GetLength())) // allow text expansion in comments/strings
			{
				// do our list if theirs fails to display?
				if (CurPos() == lp)                       // don't display if they expanded
					CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
				return TRUE;
			}
#endif // jer
			if (p2 && !IsCFile(m_ftype) &&
			    (m_lastScope.GetLength() && m_lastScope[0] != DB_SEP_CHR)) // allow text expansion in comments/strings
			{
				// do our list if theirs fails to display?
				if (CurPos() == lp)               // don't display if they expanded
					CmEditExpand(ET_EXPAND_TEXT); // don't insert tab
				return TRUE;
			}
			if (p2 && m_ftype == CS && !HasVsNetPopup(TRUE))
			{
				if (CurPos() == lp) // don't display if they expanded
				{
					CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
					return TRUE;
				}
			}

			if (gShellAttr->IsDevenv10OrHigher() && IsCFile(m_ftype) && Psettings->m_bUseDefaultIntellisense)
			{
				// VS2010: c/c++ Allow Ctrl+space to display our list if theirs doesn't
				// Used in CLI and #undef'd code [case=36622]
				if (!p2) // about to happen
				{
					// ignore beep if their intellisense fails
					g_IgnoreBeepsTimer = GetTickCount() + 1000;
				}
				else if (gTestsActive)
					SetTimer(DSM_VA_LISTMEMBERS, 600, NULL);
				else if (gShellAttr->IsDevenv16OrHigher() && GetParseDb()->m_xref)
					SetTimer(DSM_VA_LISTMEMBERS, 400, 0); // [case: 142247]
				else
					SetTimer(DSM_VA_LISTMEMBERS, 200,
					         NULL); // Show VA members LIST, but don't expand if theirs does not appear
				return FALSE;
			}

			if (p2 && gShellAttr->IsDevenv7() && Psettings->m_bUseDefaultIntellisense)
			{
				if (IsCFile(m_ftype))
				{
					// do our list if theirs fails to display?
					if (CurPos() == lp)                       // don't display if they expanded
						CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
				}
			}

			if (!p2) // about to happen
			{
				m_typing = false;
				lp = CurPos();

				if (selStr.GetLength())
				{
					CodeTemplateMenu();
					return TRUE;
				}
				// Fire SuggestBits in HTML/ASP/JS
				{
					::ScopeInfoPtr si2 = ScopeInfoPtr();
					si2->m_suggestionType |= SUGGEST_TEXT | SUGGEST_MEMBERS;
				}
				// Fire our suggestions on ctrl+space with their intellisense [if any]
				if (m_ftype == JS || Is_Tag_Based(m_ftype))
					SetTimer(ID_TIMER_GETHINT, 50, NULL);

				if (gShellAttr->IsDevenv() /*&& (m_ftype == VB || m_ftype == CS || m_ftype == JS)*/)
				{
					if (Psettings->m_bUseDefaultIntellisense || !IsCFile(m_ftype))
					{
						bool ignoreDefault = true;
						if (IsCFile(m_ftype))
						{
							// ignore m_bUseDefaultIntellisense if doing a #include completion list
							const WTString curLineTxt(GetLine(CurLine()));
							if (m_lastScope == ":PP::" && curLineTxt.contains("#include"))
								ignoreDefault = false;
						}

						if (ignoreDefault)
							return FALSE;
					}
					else if (m_lastScope.length() && m_lastScope[0] != DB_SEP_CHR)
					{
						// expanding text w/in comment or string
						CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
						return TRUE;                          // cancel theirs
					}

					if (IsCFile(m_ftype))
					{
						// expanding text w/in comment or string
						CmEditExpand((p1 == DSM_LISTMEMBERS) ? ET_EXPAND_MEMBERS
						                                     : ET_EXPAND_COMLETE_WORD); // don't insert tab
						// display theirs if we couldn't
						// if UseDefaultIntellisense, ours was called because theirs failed, don't post
						// make sure our expansion did not expand current word and disappear
						if (cwd == CurWord() && !g_CompletionSet->IsExpUp(NULL) && !HasVsNetPopup(TRUE) &&
						    !Psettings->m_bUseDefaultIntellisense && IsCFile(m_ftype))
							return FALSE; // Ours failed, Allow theirs  // ::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND,
							              // DSM_LISTMEMBERS, NULL);
						return TRUE;      // cancel theirs
					}

					return FALSE; // let them do it
				}
			}
			//			// display ours if theirs not there?
			//			// if UseDefaultIntellisense, ours was called because theirs failed, don't post
			//			// make sure our expansion did not expand current word and disappear
			//			if(cwd == CurWord() && !g_CompletionSet->IsExpUp(NULL) && !HasVsNetPopup(TRUE) &&
			//! Psettings->m_bUseDefaultIntellisense && IsCFile(m_ftype))
			//				::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_LISTMEMBERS, NULL);
			return TRUE;
		}
		if (p1 == DSM_WORDLEFT || p1 == DSM_WORDRIGHT)
		{
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			return TRUE;
		}
		if (p1 == ID_FILE_SAVE || p1 == DSM_SAVEALL)
		{
			g_CompletionSet->Dismiss();
			if (p2 == 0) // about to save
			{
				if (Psettings->m_autoBackup)
				{
					const CStringW fname(FileName());
					DuplicateFile(fname, AutoSaveFile(fname));
				}
			}
			else
			{
				SetTimer(ID_GETBUFFER, 100, NULL);
				if (modified)
				{
					modified = FALSE;
					// force nuking of old variables on save of header files
					const CStringW fname(FileName());
					if (GetFileType(fname) == Header)
						g_ParserThread->QueueFile(fname, NULLSTR, mThis);
				}
			}
		}

		if (Psettings->m_rapidFire && !g_inMacro)
		{
			int key = (int)p1 + 0x100; // because '.' and vk_delete are equal
			if (rapidfire == key && (int)p1 == VK_UP)
			{
				rapidfire = 0;
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.LineUp"), 0);
			}
			if (rapidfire == key && (int)p1 == VK_DOWN)
			{
				rapidfire = 0;
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.LineDown"), 0);
			}
			if (rapidfire == key && (int)p1 == VK_RIGHT)
			{
				rapidfire = 0;
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.CharRight"), 0);
			}
			if (rapidfire == key && (int)p1 == VK_LEFT)
			{
				rapidfire = 0;
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.CharLeft"), 0);
			}
			rapidfire = key;
		}

		if (VK_END == p1 || VK_HOME == p1 || VK_NEXT == p1 || VK_PRIOR == p1)
		{
			// [case: 56607]
			if (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible())
				DisplayToolTipArgs(false);
		}

		return VAProcessKey((uint)p1, 0, 0xdead);
	}

#ifdef CTIMER
	char lbuf[512];
	sprintf_s(lbuf, "msg = %x, wp = %zx, lp = %zx", msg, p1, p2);
	DEFTIMERNOTE(DefWindowProcTimer, lbuf);
#endif // CTIMER

	//////////////////////////////////////////////////////////////////////////
	// rapidfire commands
	// double commands if rapid fire is set
	if (Psettings->m_rapidFire && !g_inMacro)
	{
		switch (msg)
		{
		case WM_IME_STARTCOMPOSITION:
			Psettings->m_rapidFire = false; // disable if using ime input
			break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
		case WM_LBUTTONUP:
		case WM_LBUTTONDOWN:
			rapidfire = 0;
			break;

			// don't rapidfire scroll, because we do not see the mouse up to tell us that rapidfire should stop
			//		case WM_VSCROLL:
			//			if(!(GetKeyState(VK_LBUTTON) & 0x1000))
			//				break;
			//			//	case WM_COMMAND:
			//			if(g_dopaint){
			//				if(rapidfire == 1){
			//					rapidfire = 2;
			//					CWnd::SendMessage(msg, p1, p2);
			//					rapidfire = 1;
			//				} else if(rapidfire != 2)
			//					rapidfire = 1;
			//			}
			//			break;
		case WM_CHAR:
			if (p1 && rapidfire == (int)p1)
			{
				rapidfire = 0;
				CWnd::SendMessage(msg, p1, p2);
			}
			rapidfire = (int)p1;
			break;

		case WM_COMMAND:
			switch (p1 & 0xffff)
			{
			case ID_FILE_SAVE:
			case DSM_SAVEALL:
				if (Psettings->m_autoBackup)
				{
					// backup original file before save
					const CStringW fname(FileName());
					DuplicateFile(fname, AutoSaveFile(fname));
				}
				break;
			case ID_EDIT_LINE_UP:
			case ID_EDIT_LINE_DOWN:
			case ID_EDIT_CHAR_RIGHT:
			case ID_EDIT_CHAR_LEFT:
				if (p1 && rapidfire == (int)p1)
				{
					rapidfire = 0;
					CWnd::SendMessage(msg, p1, p2);
				}
				rapidfire = (int)p1;
			}
			break;
		case ID_EDIT_LINE_DOWN:
		case ID_EDIT_LINE_UP:
		case ID_EDIT_CHAR_LEFT:
		case ID_EDIT_CHAR_RIGHT: {
			if (rapidfire == (int)p1)
			{
				rapidfire = 2;
				::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, p1, p2);
				rapidfire = (int)p1;
			}
			else if (rapidfire != 2)
				rapidfire = (int)p1;
		}
		break;
		default:
			if (msg == VAM_DSM_COMMAND)
			{
				switch (p1 & 0xffff)
				{
				case DSM_SCROLLDOWN:
				case DSM_SCROLLUP:
				case DSM_DOWN:
				case DSM_UP:
				case DSM_RIGHT:
				case DSM_LEFT:
				case DSM_BACKSPACE:
				case ID_EDIT_CLEAR:
				case DSM_SELLEFT:
				case DSM_SELRIGHT:
					if (rapidfire == (int)p1)
					{
						rapidfire = 2;
						::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, p1, p2);
						rapidfire = (int)p1;
					}
					else if (rapidfire != 2)
						rapidfire = (int)p1;
					break;
				case WM_PASTE:
					// reparse pasted code
					if (gShellAttr->IsDevenv() && !p2)
					{ // post paste
						// Reparse is no longer needed, but leaving here anyways because it does do other stuff
						// once reparse is cleaned, this should be removed or updated.
						CPoint pt((int)CurPos(TRUE), (int)CurPos(TRUE));
						OnModified();
						GetBuf(TRUE);
						Reparse();
						SetPos((uint)pt.y);
					}
					break;
				}
			}
		}
	}
	// rapidfire
	//////////////////////////////////////////////////////////////////////
	long id = long((msg != WM_COMMAND) ? msg : p1);
	switch (id)
	{
	case DSM_VA_LISTMEMBERS:
		CmEditExpand(ET_EXPAND_MEMBERS); // don't insert tab
		return TRUE;
		break;

	case VAM_GETTEXT:
		if (gShellAttr->IsDevenv())
		{
			_ASSERTE(!"VAM_GETTEXT not handled in devenv");
		}
		else
		{
			_ASSERTE(p1);
			static WTString sBufCopy;
			sBufCopy = GetBufConst();
			*(int*)p1 = sBufCopy.GetLength();
			return (LPARAM)(LPCSTR)sBufCopy.c_str();
		}
		break;

	case WM_VA_SMARTCOMMENT: {
		const WTString txt = GetSelString();
		if (txt.GetLength() == 0)
			return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK2);
		if (Is_Tag_Based(m_ScopeLangType))
		{
			if (txt.Find("<!--") == 0)
			{
				const EolTypes::EolType et = EolTypes::GetEolType(txt);
				const WTString lnBrk(EolTypes::GetEolStr(et));
				int pos = txt.Find("-->" + lnBrk);
				if (-1 != pos)
				{
					pos = txt.Find("-->" + lnBrk, pos + 4);
					if (-1 != pos)
						return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK2);
				}

				return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK);
			}

			// prefer one mass comment in tag based lang
			return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK);
		}
		if (txt.Find("//") == 0)
			return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK2);
		if (txt.Find("/*") == 0)
			return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK);
		if (EolTypes::GetEolType(txt) == EolTypes::eolNone)
			return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK);
		return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK2);
	}
	break;

	case WM_VA_COMMENTBLOCK:
		// devstu button pressed
		{
			const WTString comStr = (Is_VB_VBS_File(m_ScopeLangType)) ? "'" : (Is_Tag_Based(m_ScopeLangType) ? "<!--" : "/*");

			if (GetSelString().Find(comStr) == 0) // already commented, call uncommentblock
				return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK);
			else
				return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK);
		}

	case WM_VA_COMMENTLINE:
		// devstu button pressed
		{
			const WTString comStr = (Is_VB_VBS_File(m_ScopeLangType)) ? "'" : (Is_Tag_Based(m_ScopeLangType) ? "<!--" : "//");
			WTString sel = GetSelString();
			int curPos = -1;

			if (sel.IsEmpty())
			{
				curPos = (int)CurPos();
				int curLine = CurLine();
				long startPos = LineIndex(curLine);
				long endPos = LineIndex(curLine + 1);
				SetSel(startPos, endPos);
				sel = GetSelString();
			}

			if (sel.Find(comStr) == 0)
			{
				// already commented, call uncomment
				if (curPos != -1)
					SetPos((uint)curPos);

				return SendMessage(WM_COMMAND, VAM_UNCOMMENTBLOCK2);
			}
			else
			{
				if (curPos != -1)
					SetPos((uint)curPos);

				return SendMessage(WM_COMMAND, VAM_COMMENTBLOCK2);
			}
		}

	case WM_VA_SCOPEPREVIOUS:
		GotoNextMethodInFile(FALSE);
		return TRUE;

	case WM_VA_SCOPENEXT:
		GotoNextMethodInFile(TRUE);
		return TRUE;

	case WM_DESTROY:
		if (!m_stop)
		{
			SendMessage(WM_CLOSE);
			return true;
		}
		break;

	case WM_VA_SPELLDOC: {
		long startPos;
		long endPos;

		if (m_hasSel)
		{
			long anchorPt = 0;
			long activePt = 0;
			GetSel2(anchorPt, activePt);

			anchorPt = GetBufIndex(anchorPt);
			activePt = GetBufIndex(activePt);

			startPos = min(anchorPt, activePt) - 1;
			endPos = max(anchorPt, activePt);
		}
		else
		{
			startPos = 0;
			endPos = GetBuf().length();
		}

		MultiParsePtr mp = MultiParse::Create(m_ScopeLangType);
		mp->SpellBlock(this, startPos, endPos);
		return TRUE;
	}
	break;

		/*
		#ifdef jer
		case WM_NOTIFY:
		{
		NMHDR *note = (NMHDR*) p2;
		if(note->code == EN_PROTECTED  && needParse)
		SetTimer(ID_TIMER_REPARSE2, 500, NULL);
		return TRUE;
		}
		case WM_DESTROY:
		if (m_lborder) {
		m_lborder->DestroyWindow();
		delete m_lborder;
		m_lborder = NULL;
		}
		// CTer::OnClose();
		// CTer::DestroyWindow();
		if (m_lpRichEditOle != NULL) {
		m_lpRichEditOle->Release();
		m_lpRichEditOle = NULL;
		}
		StopColorThread();
		return TRUE;
		break;
		#endif
		*/
		// called before a file is closed,  resink our breakpoints with what they have -Jer
	case WM_VA_FLUSHFILE:
		// p1 will be set if we want to skip reparse
		if (m_ReparseScreen && !p1) // so sinkdb can run...
			Reparse();
		if (g_VATabTree) // so user can go back to file just closed
			g_VATabTree->AddFile(FileName(), CurLine() - 1);
		/*		CmQSave does a sink them, no need for timer
		if (!p1)	// p1 will be set when stepping thru code
		SetTimer(ID_SINKTHEM_TIMER, 50, NULL);
		*/
		return 1;
	case WM_PAINT:
		CTer::DefWindowProc(msg, p1, p2);
		if (m_lborder && (GetKeyState(VK_LBUTTON) & 0x1000)) // catch drag select/scroll
			m_lborder->DrawBkGnd(true);

		return 1;
		break;
		// look for reasons to close expwin
	case WM_VSCROLL:
		if (g_dopaint)
		{
			if (gShellAttr->IsDevenv())
				SetTimer(ID_TIMER_CHECKFORSCROLL, 100, NULL);
		}
	case WM_MOUSEWHEEL:
	case WM_MOVING:
	case WM_MOVE:
	case WM_SIZE:
		if (msg == WM_MOUSEWHEEL && g_CompletionSet->ProcessEvent(mThis, NULL, WM_MOUSEWHEEL, p1, p2))
			return TRUE;
#if !defined(RAD_STUDIO)
		if (g_dopaint && msg == WM_SIZE)
		{
			// Look for split windows and enable va on them
			extern bool LaunchEdCnt(HWND);
			HWND hp = ::GetParent(m_hWnd);
			bool newEdCntCreated = false;
			bool tmpBool = false;
			newEdCntCreated = LaunchEdCnt(::GetDlgItem(hp, 0xe901));
			tmpBool = LaunchEdCnt(::GetDlgItem(hp, 0xe910));
			if (tmpBool)
				newEdCntCreated = tmpBool;
			tmpBool = LaunchEdCnt(::GetDlgItem(hp, 0xe911));
			if (tmpBool)
				newEdCntCreated = tmpBool;
			if (newEdCntCreated)
				Invalidate(); // fixes some weird stuff near the caret after split
				              //			SetFocus();	- breaks the addin side - wrong window gets focus
		}
#endif
		if (g_dopaint && msg == WM_MOUSEWHEEL)
		{ // don't let our sink nuke popups
			vCatLog("Editor.Events", "EdWM:0x%x", msg);
			ClearAllPopups();
		}

	case EM_SCROLL:
		if (g_dopaint)
		{
			{
				vLog("EdSM2:0x%x", msg);
				ClearAllPopups();
			}
			{
				LRESULT ret = CTer::DefWindowProc(msg, p1, p2);
				if (m_lborder)
					m_lborder->DrawBkGnd();
				CatLog("Editor.Events", "EDC::DWP:EMSCR");

				if (g_dopaint)
					g_CompletionSet->Dismiss();
				m_ignoreScroll = true; // don't repaint if onscroll
				KillTimer(ID_UNDERLINE_ERRORS);
				// set timer for new underlining with VAParse
				if (!RefactoringActive::IsActive() && !m_FileHasTimerForFullReparse)
					SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
				KillTimer(ID_TIMER_GETSCOPE);
				// do not post if mouse did not move, creates annoying tooltips
				//				SetTimer(ID_TIMER_MOUSEMOVE, 500, NULL);
				return ret;
			}
		}
		if (KillTimer(ID_UNDERLINE_ERRORS))           // Speed up scrolling
			SetTimer(ID_UNDERLINE_ERRORS, 200, NULL); // Need to reissue so it does not get ignored.
		KillTimer(ID_TIMER_GETSCOPE);
		// do not post if mouse did not move, creates annoying tooltips
		//		SetTimer(ID_TIMER_MOUSEMOVE, 500, NULL);
		break;
	case WM_HSCROLL:
		// what should happen if tooltip is up while typing causes scroll???
		if (g_dopaint)
		{ // don't let our sink nuke popups
			vCatLog("Editor.Events", "EdSM3:0x%x", msg);
			ClearAllPopups();
		}
		break;
	case DSM_DBG_SETBRKPT: // line, enabled
		if (p1 == -1)
		{
			if (modified)
				return TRUE;
			m_LnAttr->ClearAllBrkPts(this);
		}
		else if (!p1)
		{
			// if p1 == 0, then get current line
			int ln = CurLine();
			m_LnAttr->Line(ln)->ToggleBreakPoint(ln + 1);
		}
		else
		{
			// line num is base 1, so subtract 1 since m_LnAttr is base 0
			m_LnAttr->Line(p1 ? int(p1) - 1 : 0)->AddBreakPoint((int)p1);
			// p2 (enable) will be 0 or -1
			if (!p2)
				m_LnAttr->Line(p1 ? (int)p1 - 1 : 0)->EnableBreakPoint(false);
		}
		// this causes flicker in VaTree
		// if(g_VATabTree)
		//	g_VATabTree->AddMethod(Scope(), FileName(), CurLine());
		if (m_lborder)
			m_lborder->DrawBkGnd();
		return 1;
		break;
	case DSM_DBG_TOGGLEBRKPT:
		m_LnAttr->Line(CurLine())->ToggleBreakPointEn();
		if (m_lborder)
			m_lborder->DrawBkGnd();
		return 1;
		break;
	case WM_VA_REPARSEFILE:
		QueForReparse();
		return TRUE;
	case DSM_FINDCURRENTWORDFWD:
	case DSM_FINDCURRENTWORDBWD:
		if (!::IsAnySysDicLoaded())
			::MessageBeep(0xffffffff);
		else
			new FunctionThread(FindNextReferenceCB, (LPVOID)(id == DSM_FINDCURRENTWORDFWD), "FindNextReferenceCB",
			                   true);
		return TRUE;
	case WM_SETFOCUS:
		rapidfire = 0; // to fix checkout bug typing /<file gets checked out> then another / would trigger rapidfile and
		               // insert three /'s
		// OnSetFocus((CWnd*)p1);
		break;
	case DSM_WIZ_GOTODEF: {
		NavAdd(FileName(), CurPos());
		PosLocation location = p1 ? ((int)p1 < 0 ? posAtCaret : posAtCursor) : posBestGuess;
		if (Psettings->mUseGotoRelatedForGoButton)
			return SuperGoToDef(location);
		else
			return GoToDef(location);
	}
	break;
	case EM_REPLACESEL:
		if (0xee == p1)
		{
			gShellSvc->SelectAll(this);
			ReplaceSel((char*)p2, noFormat);
			GetBuf(TRUE);
			return 1;
		}

		if (0xed == p1)
		{
			Insert((char*)p2);
			return 1;
		}
		break;
	case WM_VA_GET_EDCNTRL:
		// p1 is a preview of all commands before they happen
		if (p1 == WM_CLOSE)
		{
#define ID_DESTRUCTOR 3
			if (m_hParentMDIWnd)
				::SendMessage(m_hParentMDIWnd, WM_COMMAND, ID_DESTRUCTOR, 0);
			UnsubclassWindow();
			ReleaseSelfReferences();
			return NULL;
		}
		if (p1 == DSM_SAVEALL || p1 == ID_FILE_SAVE)
		{
			modified_ever = false; //  so we know the file is saved before it is closed
			// file is about to save, issue timer so we make sure file is saved;
			SetTimer(ID_FILE_SAVE, 500, NULL);
		}

		return (LRESULT)(void*)this;
	case WM_VA_NEW_UNDO:
		return TRUE;
	case WM_VA_SET_DB: {
		MultiParsePtr mp;
		{
			AutoLockCs l(mDataLock);
			mp.swap(mPendingMp);
		}

		if (!mp)
		{
			// WM_VA_SET_DB invoked more than once with only a single mPendingMp update
			m_isValidScope = FALSE;
			m_FileHasTimerForFullReparse = false;
			m_FileIsQuedForFullReparse = FALSE;
#if !defined(RAD_STUDIO)
			ASSERT_ONCE(FALSE); // window recycling in RadStudio is common, but not in VS
#endif
			return TRUE;
		}

		const BOOL reclassify = (BOOL)p2;

		_ASSERTE(mp->FileType());
		_ASSERTE(mp->GetCacheable());
		// reparse always, even if user has modified file since open
		//   in which case we will do a reparse of screen.
		// Use fresh db from ParseThrd
		MultiParsePtr oldmp(GetParseDb());
		const CStringW fname(FileName());
		const CStringW mpFname(mp->GetFilename());
		if (mpFname.GetLength() && mpFname != fname &&
		    !DoesFilenameCaseDiffer(mpFname, fname))
		{
#if defined(RAD_STUDIO)
			return TRUE; // Window recycling
#else
			ASSERT_ONCE(FALSE);
#endif
		}

		{
			AutoLockCs l(mDataLock);
			m_pmparse = mp;
		}

		SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
		m_isValidScope = FALSE;
		m_FileHasTimerForFullReparse = false;

		if (reclassify && gShellAttr->IsDevenv10OrHigher() && gVaInteropService && m_IVsTextView)
		{
			CComPtr<IVsTextLines> vsTextLines;
			m_IVsTextView->GetBuffer(&vsTextLines);
			if (vsTextLines)
				gVaInteropService->Reclassify(vsTextLines);
		}
		Invalidate(TRUE); // Apply coloring after parse
		// Scope(true);  // doesn't work because g_threadCount is set
		if (/*Psettings->m_ActiveSyntaxColoring &&*/ !RefactoringActive::IsActive() && !m_FileHasTimerForFullReparse)
			SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
		m_skipUnderlining = -1;
		m_FileIsQuedForFullReparse = FALSE;
		mInitialParseCompleted = true;

		if (g_ParserThread && gVaService && g_currentEdCnt.get() == this && gVaService->GetOutlineFrame() &&
		    gVaService->GetOutlineFrame()->IsAutoUpdateEnabled() &&
		    (!g_CompletionSet ||
		     !g_CompletionSet->IsExpUp(mThis))) // case=8976: reduce chances that scope fails while listbox is up
		{
			g_ParserThread->QueueParseWorkItem(new RefreshFileOutline(mThis));
		}

		if (g_ParserThread && gVaService && g_currentEdCnt.get() == this && gVaService->GetHashtagsFrame())
		{
			g_ParserThread->QueueParseWorkItem(new RefreshHashtags(mThis));
		}

		if (g_CVAClassView)
			g_CVAClassView->GetOutline(FALSE);
		return WM_VA_SET_DB;
	}
	case EM_SCROLLCARET:
		return TRUE;
	}
	if (msg == WM_COMMAND)
	{
		m_ignoreScroll = false; // repaint onscroll
		switch (p1 & 0xffff)
		{
		case VAM_GETBUF: {
			GetBuf(TRUE);
			static WTString sBufCopy;
			sBufCopy = GetBufConst();
			return (LRESULT)(LPCSTR)sBufCopy.c_str();
		}
		case VAM_SHOWGUESSES: {
			if (m_isValidScope || Is_Tag_Based(m_ScopeLangType) || m_ScopeLangType == JS)
				g_CompletionSet->ShowGueses(mThis, g_Guesses.GetGuesses(), ET_SUGGEST);
			return TRUE;
		}
		case VAM_UNCOMMENTBLOCK: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			CStringW buf(GetSelStringW());
			const EolTypes::EolType et = EolTypes::GetEolType(buf);
			const CStringW lnBrk(EolTypes::GetEolStrW(et));
			CStringW commentBegin(L"/*"), commentEnd(L"*/"), commentBeginInvalidate(L"/ *"),
			    commentEndInvalidate(L"* /");
			bool recoverSpaces = false;
			if (Is_Tag_Based(m_ScopeLangType))
			{
				commentBegin = L"<!--";
				commentEnd = L"-->";
				commentBeginInvalidate = L"< ! - -";
				commentEndInvalidate = L"- - >";
				recoverSpaces = true;
			}

			if (recoverSpaces)
				buf.Replace(L" " + commentEnd + lnBrk, L"");
			buf.Replace(commentEnd + lnBrk, L"");
			if (recoverSpaces)
				buf.Replace(commentBegin + L" " + lnBrk, L"");
			buf.Replace(commentBegin + lnBrk, L"");
			if (recoverSpaces)
				buf.Replace(L" " + commentEnd, L"");
			buf.Replace(commentEnd, L"");
			if (recoverSpaces)
				buf.Replace(commentBegin + L" ", L"");
			buf.Replace(commentBegin, L"");
			buf.Replace(commentBeginInvalidate, commentBegin);
			buf.Replace(commentEndInvalidate, commentEnd);

			long _p1, _p2;
			GetSel2(_p1, _p2);
			if (_p1 > _p2)
			{
				SwapAnchor();
				std::swap(_p1, _p2);
			}

			InsertW(buf, true, noFormat, false);
			SetSelection(_p1, (int)CurPos());
		}
			return TRUE;
		case VAM_COMMENTBLOCK: {
			CStringW selTxt(GetSelStringW());
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			CStringW commentBegin(L"/*"), commentEnd(L"*/"), commentBeginInvalidate(L"/ *"),
			    commentEndInvalidate(L"* /");
			if (Is_Tag_Based(m_ScopeLangType))
			{
				commentBegin = L"<!--";
				commentEnd = L"-->";
				commentBeginInvalidate = L"< ! - -";
				commentEndInvalidate = L"- - >";
			}

			selTxt.Replace(commentBegin, commentBeginInvalidate);
			selTxt.Replace(commentEnd, commentEndInvalidate);

			if (Is_Tag_Based(m_ScopeLangType) && !Psettings->mDontInsertSpaceAfterComment)
			{
				commentBegin = L"<!-- ";
				commentEnd = L" -->";
			}

			long _p1, _p2;
			GetSel2(_p1, _p2);
			if (_p1 > _p2)
			{
				SwapAnchor();
				std::swap(_p1, _p2);
			}

			const EolTypes::EolType et = EolTypes::GetEolType(selTxt);
			if (EolTypes::eolNone == et)
			{
				// single line selection
				InsertW(commentBegin + selTxt + commentEnd, true, noFormat, false);
			}
			else
			{
				// multi-line selection
				const bool selStartAtStartOfLine = (TERCOL(_p1) == 1);
				const bool selEndAtStartOfLine = (TERCOL(_p2) == 1);
				const CStringW lnBrk(EolTypes::GetEolStrW(et));

				CStringW insText;
				insText = commentBegin;
				if (selStartAtStartOfLine)
					insText += lnBrk;
				insText += selTxt;
				insText += commentEnd;
				if (selEndAtStartOfLine)
					insText += lnBrk;

				InsertW(insText, true, noFormat, false);
			}
			SetSelection(_p1, (int)CurPos());
		}
			return TRUE;
		case VAM_PARENBLOCK: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			CStringW sel(GetSelStringW());

			long _p1, _p2;
			GetSel2(_p1, _p2);
			if (_p1 > _p2)
			{
				SwapAnchor();
				std::swap(_p1, _p2);
			}
			const EolTypes::EolType et = EolTypes::GetEolType(sel);
			if (EolTypes::eolNone != et)
			{
				const CStringW lnBrk(EolTypes::GetEolStrW(et));
				InsertW(CStringW(L"(") + lnBrk + sel + CStringW(L")") + lnBrk, true, noFormat);
			}
			else
			{
				WTString autoTxt(gAutotextMgr->GetSource("(...) (line fragment)"));
				if (autoTxt.IsEmpty())
				{
					// do not provide a default snippet until
					// the autotextMgr supports unicode selections
					InsertW(CStringW(L"(") + sel + CStringW(L")"), true, noFormat);
				}
				else
				{
					gAutotextMgr->InsertAsTemplate(mThis, autoTxt);
					return TRUE;
				}
			}
			SetSel(_p1, (long)CurPos());
		}
			return TRUE;
		case VAM_COMMENTBLOCK2: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			CStringW sel = GetSelStringW();
			long curPos = -1;
			long curLine = -1;
			long curCol = -1;
			if (sel.IsEmpty())
			{
				curPos = (long)CurPos();
				PosToLC(GetBufConst(), curPos, curLine, curCol);
				long startPos = LineIndex(curLine);
				long endPos = LineIndex(curLine + 1);
				SetSel(startPos, endPos);
				sel = GetSelStringW();
			}

			const EolTypes::EolType et = EolTypes::GetEolType(sel);
			CStringW comment_style(::GetDefaultLineCommentString());
			if (!Psettings->mDontInsertSpaceAfterComment)
				comment_style += L" ";

			if (Is_VB_VBS_File(m_ScopeLangType))
				comment_style = L"'";
			else if (Is_Tag_Based(m_ScopeLangType))
				comment_style = Psettings->mDontInsertSpaceAfterComment ? L"<!--" : L"<!-- ";

			if (EolTypes::eolNone != et)
			{
				// Select whole lines
				GetBuf(TRUE); // ensure up to date [case: 1186]
				long eofPos = (long)LinePos(NPOS, 0) + GetLine(NPOS).GetLength();

				long pos1, pos2;
				GetSel2(pos1, pos2);
				if (pos1 > pos2)
				{
					SwapAnchor();
					std::swap(pos1, pos2);
				}

				long px1 = (long)LinePos((int)CurLine((uint)pos1 /*CurPosBegSel()*/));
				long px2 = (long)LinePos((int)CurLine((uint)pos2 /*CurPos(TRUE)*/ - 2) + 1); // assumes empty last line
				if (!(Is_Tag_Based(m_ScopeLangType)))
				{
					//						if (px1 >= px2)
					//						{
					//							// reverse selection in vsnet?
					//							gShellSvc->SwapAnchor();
					//							eofPos = (long) LinePos(NPOS, 0) + GetLine(NPOS).GetLength();
					//							px1 = (long) LinePos(CurLine(CurPos(false)/*CurPosBegSel()*/)); // new
					//							px2 = (long) LinePos(CurLine(CurPos(TRUE)-2) + 1);
					//							std::swap(px1, px2);
					//						}
					SetSelection(px1, px2);
					if (gShellAttr->IsDevenv())
					{
						const long px3 = (long)CurPos();
						if (px3 != px2)
							SetSelection(px1, eofPos);
					}
				}
				sel = GetSelStringW();
				if (Is_Tag_Based(m_ScopeLangType))
				{
					sel.Replace(L"<!--", L"< ! - -");
					sel.Replace(L"-->", L"- - >");
				}

				CStringW replaceSel(comment_style + sel);
				const CStringW lnBrk(EolTypes::GetEolStrW(et));
				if (Is_Tag_Based(m_ScopeLangType))
					replaceSel.Replace(lnBrk, (Psettings->mDontInsertSpaceAfterComment ? L"-->" : L" -->") + lnBrk);
				replaceSel.Replace(lnBrk, lnBrk + comment_style);
				const int kSelLen = sel.GetLength();
				if (sel.ReverseFind('\n') == (kSelLen - 1) || sel.ReverseFind('\r') == (kSelLen - 1))
				{
					// remove the last "//"
					replaceSel.SetAt(replaceSel.GetLength() - comment_style.GetLength(), L'\0');
				}

				InsertW(replaceSel, true, noFormat);
				SetSelection(px1, (long)CurPos(true));
			}
			else
			{
				long _p1, _p2;
				GetSel2(_p1, _p2);
				if (_p1 > _p2)
				{
					SwapAnchor();
					std::swap(_p1, _p2);
				}

				sel = comment_style + sel;
				if (Is_Tag_Based(m_ScopeLangType))
					sel += (Psettings->mDontInsertSpaceAfterComment ? L"-->" : L" -->");
				InsertW(sel, true, noFormat);
				_p1 += comment_style.GetLength();
				SetSelection(_p1, _p1);
			}
			if (curPos != -1)
				SetPos(uint(curPos + 3));
		}
			return TRUE;
		case VAM_UNCOMMENTBLOCK2: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			CStringW sel = GetSelStringW();
			long curPos = -1;
			long curLine = -1;
			long curCol = -1;
			if (sel.IsEmpty())
			{
				curPos = (long)CurPos();
				PosToLC(GetBufConst(), curPos, curLine, curCol);
				long startPos = LineIndex(curLine);
				long endPos = LineIndex(curLine + 1);
				SetSel(startPos, endPos);
				sel = GetSelStringW();
			}

			CStringW replaceSel;
			if (Is_VB_VBS_File(m_ScopeLangType))
				replaceSel = sel.Mid(1);
			else if (Is_Tag_Based(m_ScopeLangType) && sel.Find(L"<!-- ") == 0 &&
			         (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior))
				replaceSel = sel.Mid(5);
			else if (Is_Tag_Based(m_ScopeLangType) && sel.Find(L"<!--") == 0)
				replaceSel = sel.Mid(4);
			else if (sel.Find(::GetDefaultLineCommentString() + L" ") == 0 &&
			         (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior))
				replaceSel = sel.Mid((int)Psettings->mLineCommentSlashCount + 1);
			else if (sel.Find(::GetDefaultLineCommentString()) == 0)
				replaceSel = sel.Mid((int)Psettings->mLineCommentSlashCount);
			else if (Psettings->mLineCommentSlashCount != 2 && sel.Find(L"// ") == 0 &&
			         (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior))
				replaceSel = sel.Mid(3);
			else if (Psettings->mLineCommentSlashCount != 2 && sel.Find(L"//") == 0)
				replaceSel = sel.Mid(2);
			else
				replaceSel = sel;

			const EolTypes::EolType et = EolTypes::GetEolType(sel);
			if (et != EolTypes::eolNone)
			{
				const CStringW lnBrk(EolTypes::GetEolStrW(et));
				if (Is_VB_VBS_File(m_ScopeLangType))
					replaceSel.Replace(lnBrk + L"'", L"\001");
				else if (Is_Tag_Based(m_ScopeLangType))
				{
					if (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior)
						replaceSel.Replace(L" -->" + lnBrk, lnBrk);
					replaceSel.Replace(L"-->" + lnBrk, lnBrk);
					if (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior)
						replaceSel.Replace(lnBrk + L"<!-- ", L"\001");
					replaceSel.Replace(lnBrk + L"<!--", L"\001");
				}
				else
				{
					const CStringW cmt(::GetDefaultLineCommentString());
					if (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior)
						replaceSel.Replace(lnBrk + cmt + L" ", L"\001");
					replaceSel.Replace(lnBrk + cmt, L"\001");

					if (Psettings->mLineCommentSlashCount != 2)
					{
						if (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior)
							replaceSel.Replace(lnBrk + L"// ", L"\001");
						replaceSel.Replace(lnBrk + L"//", L"\001");
					}
				}
				replaceSel.Replace(L"\001", lnBrk);
			}
			else if (Is_Tag_Based(m_ScopeLangType))
			{
				if (!Psettings->mDontInsertSpaceAfterComment || Psettings->mOldUncommentBehavior)
					replaceSel.Replace(L" -->", L"");
				replaceSel.Replace(L"-->", L"");
			}

			if (Is_Tag_Based(m_ScopeLangType))
			{
				replaceSel.Replace(L"< ! - -", L"<!--");
				replaceSel.Replace(L"- - >", L"-->");
			}

			long _p1, _p2;
			GetSel2(_p1, _p2);
			if (_p1 > _p2)
			{
				SwapAnchor();
				std::swap(_p1, _p2);
			}
			InsertW(replaceSel, true, noFormat);
			SetSelection(_p1, (int)CurPos());
			if (curPos != -1)
			{
				int minus = curCol - 1;
				if (minus > 3)
					minus = 3;
				SetPos(uint(curPos - minus));
			}
		}
			return TRUE;
		case VAM_REGIONBLOCK:
		case VAM_IFDEFBLOCK: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			GetBuf(TRUE); // ensure up to date [case: 2395]

			WTString autoTxt;
			if ((p1 & 0xffff) == VAM_REGIONBLOCK || CS == m_ftype || Is_VB_VBS_File(m_ftype))
			{
				autoTxt = gAutotextMgr->GetSource("#region (VA)");
				if (autoTxt.IsEmpty())
				{
					_ASSERTE(!"default autotext for surround with region not found");
					autoTxt = "#region $end$\n$selected$\n#endregion\n";
				}
			}
			else
			{
				autoTxt = gAutotextMgr->GetSource("#ifdef (VA)");
				if (autoTxt.IsEmpty())
				{
					_ASSERTE(!"default autotext for surround with ifdef not found");
					autoTxt = "#ifdef $end$\n$selected$\n#endif\n";
				}
			}
			gAutotextMgr->InsertAsTemplate(mThis, autoTxt);
		}
			return TRUE;
		case VAM_NAMESPACEBLOCK: {
			if (gShellSvc->HasBlockModeSelection(this))
			{
				::MessageBeep(0xffffffff);
				return TRUE;
			}

			GetBuf(TRUE); // ensure up to date

			WTString autoTxt;
			autoTxt = gAutotextMgr->GetSource("namespace (VA)");
			if (autoTxt.IsEmpty())
			{
				_ASSERTE(!"default autotext for surround with namespace not found");
				autoTxt = "namespace $end$\n{\n    $selected$\n}\n";
			}

			gAutotextMgr->InsertAsTemplate(mThis, autoTxt);
		}
			return TRUE;
		case VAM_SORTSELECTION:
			SortSelectedLines();
			return TRUE;
		case VAM_ADDBRACE:
			SurroundWithBraces();
			return TRUE;
		}
	}
	// devstudio commands that need processing
	if (msg == VAM_DSM_COMMAND || msg == WM_COMMAND)
	{
		if (p1 != WM_COMMAND)
			m_ignoreScroll = false; // repaint onscroll
		switch (p1 & 0xffff)
		{
			//		case WM_VA_HISTORYFWD: // already added if we are going fwd
		case WM_VA_HISTORYBWD:
			// so back will always come mack to here
			NavAdd(FileName(), CurPos());
			break;
		case DSM_ENTER:
			Psettings->m_incrementalSearch = false;
			{ 
				// mshack to modify insert char into buffer so we don't need to GetBuffer again
				if (g_CompletionSet->ProcessEvent(mThis, VK_RETURN))
				{
					m_typing = false; // Case 8612: Accepting suggestion with Enter does not dismiss listbox in VC6
					return TRUE;
				}
				long _p1, _p2;
				GetSel(_p1, _p2);
				const WTString bb(GetBufConst());
				long offset = GetBufIndex(bb, _p2);
				if (bb[(INT)offset] == '\r' || bb[(INT)offset] == '\n')
				{
					// caret is at current index after cr,  if we move the cursor
					// the indent will be gone
					m_preventGetBuf = -1;
				}
				else
				{
					// hit cr and ds inserted the indent chars already
					// set timer to resink asap
					SetTimer(ID_GETBUFFER, 50, NULL);
				}
				// OnModified(); // BufInsert below will call OnModified
				if (ProcessReturn())
					return TRUE;
				if (offset)
					BufInsert(offset, "\n");
			}

			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
			{
				if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
					DisplayToolTipArgs(true);
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			}
			if (m_pDoc)
				return FALSE;
			break;
		case DSM_TYPEINFO:
			// VC6 quick info via shortcut <ctrl>+T
			{
				Scope(); // refresh tooltip info
				CPoint pt = vGetCaretPos();
				DisplayTypeInfo(&pt);
			}
			return TRUE;
		case DSM_PARAMINFO:
			Scope(); // refresh tooltip info
			DisplayToolTipArgs(true);
			return TRUE;
		case 0x8130:
		case 0x908a: // 		case ID_EDIT_DELETE_BACK: // VAPAD

		case ID_EDIT_UNDO:
		case ID_EDIT_REDO:
			m_doFixCase = FALSE;
			SetBufState(BUF_STATE_WRONG);
			SetTimer(ID_GETBUFFER, 100, NULL);
			SetTimer(ID_CURSCOPEWORD_TIMER, 150, NULL);
			OnModified();
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			m_autoMatchChar = '\0';
			if (m_pDoc)
				return FALSE;
			break;
		case ID_EDIT_DELETE:
		case ID_EDIT_CLEAR: // DELETE KEY
			Psettings->m_incrementalSearch = false;
			// fall through
		case ID_EDIT_DELETE_BACK:
		case DSM_BACKSPACE:
			if (m_preventGetBuf) // prevent caret jump after Enter+BkSpace in VC6: case:6263
				m_preventGetBuf = -1;
			VAProjectAddFileMRU(FileName(), mruFileEdit);
			if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
				VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
			m_autoMatchChar = '\0';
			SetBufState(BUF_STATE_WRONG);
			m_typing = false; // reset so curscope looks at whole word
			OnModified();
			if (m_ttTypeInfo)
			{
				ArgToolTip* tmp = m_ttTypeInfo;
				m_ttTypeInfo = NULL;
				delete tmp;
			}

			{ 
				// mshack to modify insert char into buffer so we don't need to GetBuffer again
				long _lp1, _lp2;
				GetSel(_lp1, _lp2);
				if (!(gShellAttr->IsDevenv()) && m_undoOnBkSpacePos == CurPos() &&
				    ((p1 & 0xffff) == DSM_BACKSPACE ||
				     (p1 & 0xffff) == 0x9089 /*|| (p1&0xffff) == ID_EDIT_DELETE || (p1&0xffff) == ID_EDIT_CLEAR*/))
				{
					m_undoOnBkSpacePos = 0;
					CTer::m_pDoc->Undo();
					if (CurPos() > (ulong)_lp2) // previous undo only undid format, next will nuke inser
						CTer::m_pDoc->Undo();
					SetTimer(ID_ARGTEMPLATE_TIMER, 10, NULL);
					SetTimer(ID_GETBUFFER, 20, NULL);
					return TRUE;
				}
				m_undoOnBkSpacePos = 0; // to fix foo{<down><bs><bs> reinserts }
				WTString bb(GetBufConst());
				const long offset = GetBufIndex(bb, _lp2);
				if (m_hasSel)
				{
					SetTimer(ID_GETBUFFER, 1, NULL);
					SetTimer(ID_TIMER_GETHINT, 10, NULL);
				}
				else if (offset && !(bb[(int)offset] == '\n' && TERCOL(_lp2) > 1)) // not backspace after CR
				{
					int blen = bb.GetLength();
					LPSTR buf = bb.GetBuffer(blen);
					// shift characters so we can insert text
					// quick stab, don't shift whole buffer.
					if ((blen - offset) > 1000)
						blen = offset + 1000;
					uint cmd = uint(p1 & 0xffff);
					if (cmd == DSM_BACKSPACE || cmd == ID_EDIT_DELETE_BACK)
					{
						if (buf[offset - 1] == '\n')
							SetTimer(ID_GETBUFFER, 500, NULL);

						for (int i = offset - 1; i < blen; i++)
							buf[i] = buf[i + 1];
					}
					else if (cmd == ID_EDIT_CLEAR)
					{
						for (int i = offset; i < blen; i++)
							buf[i] = buf[i + 1];
					}
					bb.ReleaseBuffer();
					UpdateBuf(bb, false);
				}

				// sometimes ch will be null: like BACKPACE at end of line
				//  that only has whitespace.  causes horizontal screen shift
				//  problem in TerNoScroll::~TerNoScroll.
				// So make sure ch is non-null before getting buffer.
				const char ch = CharAt(CurPos() - 1);
				// don't get buffer <CR><BS>, or it will hose cursel...
				if ((_lp2 & 0xfff) > 1 && ch && !ISCSYM(ch))
				{
					// don't get hint on backspace
					//					if((p1&0xffff) == DSM_BACKSPACE && ! HasSelection())
					//						SetTimer(ID_TIMER_GETHINT, 10, NULL); // updates g_hint
					//					else
					SetTimer(ID_CURSCOPEWORD_TIMER, 10, NULL);
					SetTimer(ID_GETBUFFER, 500, NULL);
				}
			}

			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			if (!m_hasSel)
			{ // don't validate screen if we had a selection
				// repaint only current line, validate rest
				CRect r;
				vGetClientRect(&r);
				//				ValidateRect(&r);
				r.left = (int)Psettings->m_borderWidth;
				r.top = vGetCaretPos().y;
				r.bottom = r.top + g_FontSettings->GetCharHeight() + 1;
				InvalidateRect(&r, FALSE);
			}

			if (m_pDoc)
				return FALSE;
			break;
		case 0x8110:
		case 0x810f:
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			break; // continue to cwnd::defwndprc
		case DSM_RIGHT:
		case DSM_LEFT:
			Psettings->m_incrementalSearch = false;
			// fall through
		case ID_EDIT_CHAR_LEFT:
		case ID_EDIT_CHAR_RIGHT:
			m_typing = false;
			if (m_ttTypeInfo)
			{
				ArgToolTip* tmp = m_ttTypeInfo;
				m_ttTypeInfo = NULL;
				delete tmp;
			}
			g_CompletionSet->Dismiss();
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			if (m_pDoc)
				return FALSE;
			break;
		case DSM_REVTAB:
			if (m_hasSel)
				SetTimer(ID_GETBUFFER, 50, NULL);
			break;
		case DSM_INCSEARCHFWD:
		case DSM_INCSEARCHBWD:
			Psettings->m_incrementalSearch = true;
			break;

		case ID_EDIT_TAB: {
			if (VAProcessKey(VK_TAB, 1, 0))
				return TRUE;
			break;
		}
		case ID_EDIT_HOME: {
			if (VAProcessKey(VK_HOME, 1, 0))
				return TRUE;
			break;
		}
		case ID_EDIT_LINE_END: {
			if (VAProcessKey(VK_END, 1, 0))
				return TRUE;
			break;
		}
		case ID_EDIT_LINE_UP: {
			if (VAProcessKey(VK_UP, 1, 0))
				return TRUE;
			break;
		}
		case ID_EDIT_LINE_DOWN: {
			if (VAProcessKey(VK_DOWN, 1, 0))
				return TRUE;
			break;
		}
		case ET_SUGGEST:
			CmEditExpand(ET_SUGGEST); // don't insert tab
			break;
		case DSM_TAB:
			Psettings->m_incrementalSearch = false;
			if (m_pDoc && m_pDoc->Emulation() == dsEpsilon)
			{
				static BOOL once = TRUE;
				if (once)
				{
					if (WtMessageBox("Visual Assist does not support Epsilon key bindings. Click OK to continue "
					                 "without loading Visual Assist.",
					                 "Warning", MB_OKCANCEL) != IDOK)
						Psettings->m_enableVA = FALSE;
					once = FALSE;
				}
				token t = GetSubString(LinePos(), CurPos());
				WTString pwd = t.read(" \t");
				if (!pwd.GetLength())
				{
					// let them handle this,
					//  msdev will format this on the first, and indent on the second tab
					// just mark dirty and let sink later
					SetBufState(BUF_STATE_WRONG);
					break;
				}
			}
			m_typing = false; // true;  // prevent listbox from poping up again
			SetTimer(ID_GETBUFFER, 50, NULL);
			// Not sure why this was here since tab should expand suggestions
			//	if(!g_CompletionSet->IsExpUp(NULL) || (GetParseDb()->m_isDef && g_CompletionSet->m_popType !=
			// ET_AUTOTEXT && g_CompletionSet->m_popType != ET_SUGGEST_AUTOTEXT) /*|| GetSymScope().GetLength()*/) break;
			if (g_CompletionSet->IsExpUp(NULL))
			{
				if (g_CompletionSet->ProcessEvent(mThis, VK_TAB))
					return TRUE;
			}
			break;
		case DSM_AUTOCOMPLETE:
		case DSM_LISTMEMBERS:
			Psettings->m_incrementalSearch = false;
			{
				CatLog("Editor.Events", "EDC::DWP:LM");
				SetBufState(BUF_STATE_WRONG);
#if defined(RAD_STUDIO)
				if (::ShouldSuppressVaListbox(mThis))
					return 0; // so they will do their code completion
#endif
				const WTString s = CTer::m_pDoc ? CTer::m_pDoc->GetCurSelection() : NULLSTR;
				if (!strchr(s.c_str(), '\n') && !strchr(s.c_str(), '\r'))
				{
					if ((p1 & 0xffff) == DSM_TAB || (p1 & 0xffff) == ID_EDIT_TAB)
						CmEditExpand(ET_EXPAND_TAB);
					else if ((p1 & 0xffff) == DSM_LISTMEMBERS)
					{
						if (!g_CompletionSet->IsVACompletionSetEx())
							g_CompletionSet->Dismiss();
						CmEditExpand(ET_EXPAND_MEMBERS); // don't insert tab
					}
					else
						CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab

					//			SetTimer(ID_TIMER_GETHINT, 50, NULL);
					// nuke suggest word
					if (m_ttTypeInfo)
					{
						ArgToolTip* tmp = m_ttTypeInfo;
						m_ttTypeInfo = NULL;
						delete tmp;
						return TRUE;
					}
				}
				else if (m_pDoc)
					return FALSE;
				else
					break;
			}
			return TRUE;
		case 0x810d:
			//		case ID_EDIT_LINE_UP:
		case DSM_UP:
			Psettings->m_incrementalSearch = false;
			m_ignoreScroll = true; // don't repaint if onscroll
			if (m_ReparseScreen)
				Reparse();
			if (g_CompletionSet->ProcessEvent(mThis, VK_UP))
				return TRUE;
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 1)
			{
				if (m_tootipDef > 1)
					m_tootipDef--;
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
				return TRUE;
			}
			vCatLog("Editor.Events", "EdSM5:0x%x", msg);
			ClearAllPopups(false);
			DisplayToolTipArgs(false);
			if (m_pDoc)
				return FALSE;
			break;
		case 0x810e:
			//		case ID_EDIT_LINE_DOWN:
		case DSM_DOWN:
			Psettings->m_incrementalSearch = false;
			m_ignoreScroll = true; // don't repaint if onscroll
			if (m_ReparseScreen)
				Reparse();
			if (g_CompletionSet->ProcessEvent(mThis, VK_DOWN))
				return TRUE;

			if (m_ttParamInfo && m_ttParamInfo->m_totalDefs > 1 &&
			    m_ttParamInfo->m_totalDefs != m_ttParamInfo->m_currentDef)
			{
				m_tootipDef++;
				Scope(true);
				DisplayToolTipArgs(true);
				return TRUE;
			}
			ClearAllPopups(false);
			DisplayToolTipArgs(false);
			if (m_pDoc)
				return FALSE;
			break;
		case DSM_CANCEL:
			Psettings->m_incrementalSearch = false;
			{
				FindReferencesPtr globalRefs(g_References);
				if (globalRefs && globalRefs->m_doHighlight)
					globalRefs->StopReferenceHighlights();
			}
			if (m_ttTypeInfo || (m_ttParamInfo && ::IsWindowVisible(m_ttParamInfo->m_hWnd)))
			{
				ClearAllPopups(true);
				DisplayToolTipArgs(false);
				return TRUE;
			}
			if (m_pDoc)
				return FALSE;
			break;
		case ID_EDIT_CUT:
			// nuke underlining
			Psettings->m_incrementalSearch = false;
			m_typing = true;
			VAProjectAddFileMRU(FileName(), mruFileEdit);
			SetTimer(ID_GETBUFFER, 50, NULL);
		case ID_EDIT_COPY:
			ClearAllPopups();
			CmEditCopy();
			m_doFixCase = FALSE;
			if (Psettings->m_RTFCopy)
				SetTimer(ID_TIMER_RTFCOPY, 100, NULL);
			// forward to them as well
			if (m_pDoc)
				return FALSE;
			break;
		case WM_PASTE: {
			Psettings->m_incrementalSearch = false;
			if (!Psettings->m_enableVA)
				return FALSE;
			static int lpos = 0;
			ClearAllPopups();
			if (p2)
			{
				lpos = 0;
				// paste is going to happen, save current pos
				// if(Psettings->m_smartPaste && m_hasSel)
				//	Insert(""); // nuke selected text
				if (gShellAttr->IsMsdev())
				{
					// in case column select, GetSelBegPos() ruins selection
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_SWAPANCHOR, 0);
					lpos = (int)CurPos();
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_SWAPANCHOR, 0);
				}
				else
					lpos = GetSelBegPos();
				return TRUE;
			}
			// paste just happened
			m_doFixCase = FALSE;
			SetBufState(BUF_STATE_WRONG);
			OnModified();
			long _p1 = lpos;
			long _p2 = (long)CurPos();
			int l1 = LineFromChar(_p1), l2 = LineFromChar(_p2);
			if (IsFeatureSupported(Feature_FormatAfterPaste, m_ScopeLangType) && l1 != l2 && abs(l1 - l2) < 100)
			{
				BOOL doFormat = TRUE;
				// don't format comments - have to read clipboard to find out
				//  what was actually pasted
				if (::HasUnformattableMultilineCStyleComment(::GetClipboardText(m_hWnd)))
				{
					doFormat = FALSE;
					SetStatus("No 'Format after paste' due to block comment in pasted text");
				}

				if (doFormat && m_pDoc)
				{
					int cp = (int)CurPos();
					g_dopaint = 0; // prevent flash while reformatting, invalidate later

					// save text of last line before it's formatted
					SetSelection(LineIndex(l2), LineIndex(l2 + 1));
					WTString lineTxt = m_pDoc->GetCurSelection();

					// set selection for smart format
					SetSel(LineIndex(l1), LineIndex(l2 /*+1*/)); // removed the +1 to fix Keith Berson 12/30/02 bug
					gShellSvc->FormatSelection();
					if (TERCOL(cp) > 1)
					{
						// this block is to fix the placement of the caret after
						//  smart format has added or removed tabs/spaces when
						//  the pasted text did not end in a cr/lf
						int x;
						// get col length of last line before fmt
						int lenOfLnPreSmartFmt = 0;
						for (x = 0; x < lineTxt.length(); x++)
						{
							if (lineTxt[x] == '\t')
							{
								if (Psettings->TabSize)
								{
									// must round tab down to nearest tab pos
									lenOfLnPreSmartFmt =
									    int(((lenOfLnPreSmartFmt + Psettings->TabSize) / Psettings->TabSize) *
									        Psettings->TabSize);
								}
								else
								{
									ASSERT(0);
								}
							}
							else
								lenOfLnPreSmartFmt++;
						}

						SetSelection(LineIndex(l2), LineIndex(l2 + 1));
						lineTxt = m_pDoc->GetCurSelection();

						// get col length of last line after fmt
						uint lenOfLnPostFmt = 0;
						for (x = 0; x < lineTxt.length(); x++)
						{
							if (lineTxt[x] == '\t')
							{
								if (Psettings->TabSize)
								{
									// must round tab down to nearest tab pos
									lenOfLnPostFmt = ((lenOfLnPostFmt + Psettings->TabSize) / Psettings->TabSize) *
									                 Psettings->TabSize;
								}
								else
								{
									ASSERT(0);
								}
							}
							else
								lenOfLnPostFmt++;
						}
						SetPos(cp + lenOfLnPostFmt -
						       lenOfLnPreSmartFmt); // restore selection for paste not ending in <cr>
					}
					else
						SetPos((uint)LineIndex(l2));
					g_dopaint = 1;
					Invalidate(TRUE);
					UpdateWindow();
				}
			}
			if (!gShellAttr->IsDevenv())
			{ // post paste // makes slow pastes in vc6
				m_reparseRange.x = _p1;
				m_reparseRange.y = _p2;
				//					CPoint pt(_p1, _p2);
				//					needParse = true;
				//					GetBuf(TRUE);
				//					Reparse(';', &pt);
				//					SetPos(_p2);
			}
			SetTimer(ID_GETBUFFER, 100, NULL);
			SetTimer(ID_CURSCOPEWORD_TIMER, 150, NULL);
			SetTimer(IDT_REPARSE_AFTER_PASTE, 200, NULL);
			if (m_pDoc)
				return FALSE;
			break;
		}
		case 0x9167: // VApad
			g_VATabTree->ToggleBookmark(FileName(), CurLine(), TRUE);
			break;
		case DSM_TOGGLE_BKMK:
			g_VATabTree->ToggleBookmark(FileName(), CurLine() - 1, TRUE);
			return FALSE; // forward to them as well
		case DSM_MACRO_1:
			// a macro has run, reload file and mark dirty if changed
			SetBufState(BUF_STATE_WRONG);
			SetTimer(ID_GETBUFFER, 100, NULL);
			SetTimer(ID_CURSCOPEWORD_TIMER, 150, NULL);
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
				SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
			return FALSE;
		case DSM_TOGGLE_INSERT:
			if (CTer::m_pDoc)
				CTer::m_pDoc->GetTestEditorInfo();
			return FALSE;
		case DSM_DELBLANKLINE:
		case DSM_DELHORWHITESPACE:
		case DSM_DELLINE:
		case DSM_DELTOEOL:
		case DSM_DELTOEOS:
		case DSM_DELTOSTARTOFLINE:
		case DSM_DELWORDLEFT:
		case DSM_DELWORDRIGHT:
		case DSM_HOME:
		case DSM_DOCHOME:
		case DSM_DOCEND:
		case DSM_WORDLEFT:
		case DSM_WORDRIGHT:
		case DSM_FINDAGAIN:
		case DSM_FINDCURRENTWORDBWD:
		case DSM_FINDCURRENTWORDFWD:
		case DSM_FINDNEXT:
		case DSM_FINDPREV:
			Psettings->m_incrementalSearch = false;
			break;
		case ID_EDIT_PAGE_DOWN:
		case ID_EDIT_PAGE_UP:
		case DSM_PAGEUP:
		case DSM_PAGEDOWN:
		case DSM_HOMETEXT:
		case DSM_END:
			Psettings->m_incrementalSearch = false;
			if (g_CompletionSet->ProcessEvent(mThis, long(p1 & 0xffff)))
				return TRUE;
			if (g_CompletionSet->IsExpUp(mThis))
			{
				//				if(g_ListBoxLoader->m_lastPopType == 7){
				//					g_CompletionSet->Dismiss();
				//					break;
				//				}
				long cmd = 0;
				switch (p1 & 0xffff)
				{
				case ID_EDIT_PAGE_UP:
				case DSM_PAGEUP:
					cmd = VK_PRIOR;
					break;
				case ID_EDIT_PAGE_DOWN:
				case DSM_PAGEDOWN:
					cmd = VK_NEXT;
					break;
				case DSM_HOMETEXT:
					cmd = VK_HOME;
					break;
				case DSM_END:
					cmd = VK_END;
					break;
				}
				if (cmd && g_CompletionSet->ProcessEvent(mThis, cmd))
					return TRUE;
			}
			else
			{
				vCatLog("Editor.Events", "EdSM7:0x%x", msg);
				ClearAllPopups();
			}
			break;
		case WM_COMMAND:
			// some command just finished, see if we are where we think we are...
			{
				SetTimer(ID_CURSCOPEWORD_TIMER, 100, NULL);
				SetTimer(ID_KEYUP_TIMER, 500, NULL);
			}
			break;
		case DSM_OPTIONS:
			g_FontSettings->Update();
			break;
		}
	}
	return CTer::DefWindowProc(msg, p1, p2);
}

BOOL EdCnt::ProcessReturn(bool doubleLineInsert /*= true*/)
{
	static BOOL inProcessReturn = FALSE;
	if (Psettings->IsAutomatchAllowed() && !inProcessReturn)
	{
		const WTString bb(GetBufConst());
		uint cp = (uint)GetBufIndex(bb, (long)CurPos());
		BOOL bBraceEnter = (cp > 1) && bb.Mid((int)cp - 1, 2) == "{}";
		if (!bBraceEnter && cp > 2 && bb.Mid((int)cp - 2, 3) == "{ }")
		{
			// VS2010 format puts a space between the { }'s
			bBraceEnter = TRUE;
			SetSelection((int)cp - 1, (int)cp); // select the space so it get overwritten.
		}
		if (bBraceEnter)
		{
			inProcessReturn = TRUE;
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				long offset = GetBufIndex(bb, lp2);
				if (offset)
				{
					if (doubleLineInsert)
						BufInsert(offset, "\n\n");
					else
						BufInsert(offset, "\n");
				}
				gShellSvc->BreakLine();
				BOOL bPowerToolHandledit = FALSE;
				if (m_IVsTextView && doubleLineInsert)
				{
					// If Power tools is installed, just let them handle the enter, or we will get three enters.
					// case=46640
					WTString lineText = IvsGetLineText(CurLine());
					lineText.TrimLeft();
					if (lineText[0] != '}')
						bPowerToolHandledit = TRUE;
				}
				if (!bPowerToolHandledit)
				{
					SetPos(cp);
					if (doubleLineInsert)
						gShellSvc->BreakLine();
					m_undoOnBkSpacePos = CurPos();
					if (CS == m_ScopeLangType && gShellAttr->IsDevenv() &&
					    (gShellAttr->IsDevenv10OrHigher() || !Is_Tag_Based(m_ftype)) && // [case: 69883]
					    g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "Formatting_TriggerOnBlockCompletion"))
					{
						// [case: 55483] more checks due to regression in pre-vs2010 versions
						// with block (or none) tab indent
						if (gShellAttr->IsDevenv10OrHigher() ||
						    2 == g_IdeSettings->GetEditorIntOption("CSharp", "IndentStyle"))
						{
							// Reformat C# on insertion of '}' [case=8971]
							int cl = CurLine();
							// Reformat lines above and below.
							Reformat(-1, cl - 2, -1, cl + 1);
						}
					}
				}
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("EDC:");
			}
#endif // !SEAN
			inProcessReturn = FALSE;
			return TRUE;
		}
	}

	BOOL retval = FALSE;
	if (!inProcessReturn)
	{
		inProcessReturn = TRUE;
		retval = ProcessReturnComment();
		inProcessReturn = FALSE;
	}

	return retval;
}

#define ISPRESSED(key) (GetKeyState(key) & 0x1000)

BOOL EdCnt::ProcessReturnComment()
{
	static int sConsecutiveLineComments = 0;
	static WTString sLastInsert;
	static CStringW sLastFile;

	bool doesVsFeatureConflict = false;
	if (IsCFile(m_ScopeLangType))
	{
		if (gShellAttr->IsDevenv16u7())
		{
			// Disable extend multi-line comment in 16.7 C++ files because we cannot disable the new VS feature.
			// [case 142456]
			doesVsFeatureConflict = true;
		}
		else if (gShellAttr->IsDevenv16u8OrHigher())
		{
			if (g_IdeSettings && g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "ContinueCommentsOnEnter"))
			{
				// The ability to disable the new VS feature was added in 16.8, so conditionally enable extend
				// multi-line comment in C++ files. [case: 142878]
				doesVsFeatureConflict = true;
			}
		}
	}

	MultiParsePtr mp(GetParseDb());
	if (!Psettings->mExtendCommentOnNewline || doesVsFeatureConflict || HasSelection() || ISPRESSED(VK_CONTROL) ||
	    ISPRESSED(VK_SHIFT) || ISPRESSED(VK_MENU) || mp->m_scopeType != COMMENT ||
	    !((Is_C_CS_VB_File(m_ScopeLangType) || JS == m_ScopeLangType) &&
	      (mp->m_commentType == '*' || mp->m_commentType == '\n')))
	{
		sConsecutiveLineComments = 0;
		sLastInsert.Empty();
		sLastFile.Empty();
		return FALSE;
	}

	// [case: 51991] when typing fast, buffer can get out of sync
	GetBuf(TRUE);

	// [case: 39838] copy current comment prefix and insert into new line
	// Use CurPos() to allow linebreak in middle of line - nice feature (don't prevent if caret isn't @ EOL)
	int consecutiveLineCountThreshold = 1;
	WTString curLine(GetSubString(LinePos(), CurPos()));

	if (mp->m_commentType == '*' && curLine.GetLength() > 2)
	{
		// hack for parser thinking that /* *//* */ is a single comment instead of 2 different comments
		WTString nextChars(GetSubString(CurPos(), CurPos() + 3));
		nextChars.Trim();
		if (nextChars.GetLength() == 3)
			nextChars = nextChars.Left(2);
		if (nextChars == "/*")
		{
			WTString prevChars(curLine.Right(3));
			prevChars.Trim();
			if (prevChars.GetLength() == 3)
				prevChars = prevChars.Right(2);
			if (prevChars == "*/")
			{
				// [case: 66548]
				return FALSE;
			}
		}
	}

	bool javaDocStart = false;
	WTString insertText;
	WTString tmp(curLine);
	tmp.TrimLeft();
	if (mp->m_commentType == '\n')
	{
		// line comment
		if (tmp.IsEmpty())
			curLine.Empty(); // out of sync buffer?

		// check for trailing line comments (don't auto-extend trailing line comments but do allow split)
		bool hasTrailingComment = false;
		if (Is_VB_VBS_File(m_ScopeLangType)) // like this here trailing comment
		{
			if (-1 == tmp.Find('\''))
				curLine.Empty(); // out of sync buffer?
			else if (tmp.GetLength() > 3 && tmp[0] == '\'' && tmp[1] == '\'' && tmp[2] == '\'' && tmp[3] == ' ')
				curLine.Empty(); // VB handles ''' comments on its own
			else if (tmp[0] != '\'')
				hasTrailingComment = true;
		}
		else if (CS == m_ScopeLangType && tmp.GetLength() > 3 && tmp[0] == '/' && tmp[1] == '/' && tmp[2] == '/' &&
		         tmp[3] == ' ')
		{
			// C# handles /// comments on its own
			curLine.Empty();
		}
		else if (tmp[0] != '/' || tmp[1] != '/')
		{
			if (-1 == tmp.Find("//"))
				curLine.Empty(); // out of sync buffer?
			else
				hasTrailingComment = true;
		}

		const int curLineNumber = CurLine();
		const WTString wholeCurLine(GetLine(curLineNumber));

		if (gShellAttr->IsDevenv16u9OrHigher() && CS == m_ScopeLangType && tmp.GetLength() > 2 && tmp[0] == '/' &&
		    tmp[1] == '/' && tmp[2] == ' ' && wholeCurLine != curLine)
		{
			// [case: 146064] C# handles // comments on its own if line is splitting
			curLine.Empty();
		}

		if (hasTrailingComment)
		{
			if (wholeCurLine == curLine)
			{
				// don't autoextend trailing line comments when pressing enter at end of line
				curLine.Empty();
			}
			else
			{
				// trailing line comment that is being split
				// use leading whitespace of curline
				for (int x = 0; x < curLine.GetLength(); ++x)
				{
					if (wt_isspace(curLine[x]))
						insertText += curLine[x];
					else
						break; // break on first non-whitespace
				}

				if (Is_VB_VBS_File(m_ScopeLangType))
					insertText += "'";
				else
					insertText += WTString(::GetDefaultLineCommentString());
			}
		}
		else if (!curLine.IsEmpty() && curLineNumber > 1 && curLine == wholeCurLine)
		{
			bool hasCommentAfter = false;
			const WTString nextWholeLine(GetLine(curLineNumber + 1));
			for (int idx = 0; idx < wholeCurLine.GetLength() && idx < nextWholeLine.GetLength(); ++idx)
			{
				if (wholeCurLine[idx] != nextWholeLine[idx])
					break;

				if (wholeCurLine[idx] == '\'' || wholeCurLine[idx] == '/')
				{
					hasCommentAfter = true;
					break;
				}
			}

			if (hasCommentAfter)
			{
				// autoextending inside a block has higher repeat threshold
				consecutiveLineCountThreshold = 2;
			}
			else
			{
				// auto-extend line comments if there is already a line comment
				// above the current line comment; repeat threshold unchanged
				bool hasCommentBefore = false;
				const WTString prevWholeLine(GetLine(curLineNumber - 1));
				for (int idx = 0; idx < wholeCurLine.GetLength() && idx < prevWholeLine.GetLength(); ++idx)
				{
					if (wholeCurLine[idx] != prevWholeLine[idx])
						break;

					if (wholeCurLine[idx] == '\'' || wholeCurLine[idx] == '/')
					{
						hasCommentBefore = true;
						break;
					}
				}

				if (!hasCommentBefore)
					curLine.Empty();
			}
		}
	}
	else if (mp->m_commentType == '*')
	{
		// block comment
		sConsecutiveLineComments = 0;
		if (CS == m_ScopeLangType && !gShellAttr->IsDevenv14OrHigher())
		{
			// vs2008-vs2013 do something in this case (not that it is better, but it screws us up)
			curLine.Empty();
		}
		else
		{
			int pos = tmp.Find("/*");
			if (0 == pos)
			{
				pos = tmp.Find("/**");
				if (0 == pos)
					javaDocStart = true;

				// don't duplicate /* at start of block
				pos = curLine.Find("/*");
				curLine = curLine.Left(pos);
				curLine += " ";
				curLine += tmp.Mid(1);
			}
			else if (-1 != pos)
				curLine.Empty(); /* skip trailing comments */
		}
	}

	if (insertText.IsEmpty())
	{
		bool hadPunct = false;
		bool whiteSpaceAfterPunct = false;
		char lastCh = 0;
		for (int x = 0; x < curLine.GetLength(); ++x)
		{
			if (::wt_isspace(curLine[x]))
			{
				// use leading whitespace
				lastCh = curLine[x];
				insertText += lastCh;
				if (hadPunct)
					whiteSpaceAfterPunct = true;
			}
			else if (::wt_ispunct(curLine[x]))
			{
				if (whiteSpaceAfterPunct)
					break; // already had whitespace; treat punct here as letters

				if (mp->m_commentType == '*' && lastCh == '*' && curLine[x] == '/')
				{
					// [case: 66548] don't cause block termination in the following example
					/*/ comment
					/*/
					break;
				}

				lastCh = curLine[x];
				insertText += lastCh;
				hadPunct = true;
			}
			else
				break; // break on first non-whitespace/non-punct
		}

		if (javaDocStart && -1 != insertText.Find("**"))
		{
			WTString tmp2(insertText);
			tmp2.Trim();
			if (tmp2 == "**")
			{
				// [case: 111445]
				if (Psettings->mJavaDocStyle == 1)
				{
					/** most common java doc style
					 *
					 */
					insertText.ReplaceAll("**", "*");
				}
				else if (Psettings->mJavaDocStyle == 2)
				{
					/** another java doc style
					 *
					 */
					insertText.ReplaceAll("**", " *");
				}
			}
		}
	}

	if (!insertText.IsEmpty() && -1 != insertText.find_first_not_of(" \t"))
	{
		if (mp->m_commentType == '\n' && !::wt_isspace(insertText[insertText.GetLength() - 1]))
			insertText += " "; // only for line comments

		if (sConsecutiveLineComments)
		{
			if (sLastFile != FileName())
				sConsecutiveLineComments = 0;
			else if (insertText != sLastInsert)
				sConsecutiveLineComments = 0;
			else
			{
				WTString prevLineTxt(GetLine(CurLine()));
				if (sLastInsert != prevLineTxt)
					sConsecutiveLineComments = 0; // user typed in comment, restart count
			}
		}

		if (sConsecutiveLineComments < consecutiveLineCountThreshold)
		{
			if (mp->m_commentType == '\n')
			{
				++sConsecutiveLineComments;
				sLastInsert = insertText;
				sLastFile = FileName();
			}

			long offset = GetBufIndex(lp2);
			if (offset)
				BufInsert(offset, "\n");

			// insert independently so that undo works in two steps
			if (gShellAttr->IsDevenv() && !gShellAttr->IsCppBuilder())
			{
				gShellSvc->BreakLine();
				UndoContext undo("Comment continuation");
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.DeleteHorizontalWhitespace"), 0);
				InsertW(insertText.Wide());
			}
			else
			{
				// need to manually insert line break to get around auto spacing
				// since there is no DeleteHorizontalWhitespace command in vc6.
				Insert(GetLineBreakString().c_str(), true, noFormat, false);
				Insert(insertText.c_str(), true, noFormat, false);
			}
			return TRUE;
		}
	}

	sConsecutiveLineComments = 0;
	sLastInsert.Empty();
	sLastFile.Empty();
	return FALSE;
}

LRESULT
EdCnt::WindowProc(UINT msg, WPARAM p1, LPARAM p2)
{
	ASSERT_LOOP(10);
	static BOOL s_inSplit = false;

	if (s_inSplit || !Psettings) // check psettings (case=9610)
		return TRUE;

	if (msg == 0x7c || msg == 0x7d)
	{
		s_inSplit = true;
		LRESULT r = Default();
		s_inSplit = false;
		return r;
	}

	if (msg > WM_MOUSEFIRST && msg <= WM_MOUSELAST)
		vCatLog("Editor.Events", "VaEventME Ed::wp hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_hWnd, msg, (uintptr_t)p1, (uintptr_t)p2);

	// relay key events to context menu
	if (WM_KEYFIRST <= msg && msg <= WM_KEYLAST && GetActiveBCGMenu())
		return ::PostMessage(GetActiveBCGMenu(), msg, p1, p2);

	// hide MiniHelp when va is not enabled
	if (!Psettings->m_enableVA && m_minihelpHeight != -1)
	{
		m_minihelpHeight = -1;
		// signal EditParentWnd to resize EdCnt
		if (m_hParentMDIWnd)
			::SendMessage(m_hParentMDIWnd, WM_COMMAND, WM_SIZE, 0);
	}
	else if (Psettings->m_minihelpHeight != (DWORD)m_minihelpHeight && !(m_minihelpHeight == -1 && !Psettings->m_enableVA))
	{
		m_minihelpHeight = (int)Psettings->m_minihelpHeight;
		// signal EditParentWnd to resize EdCnt
		if (m_hParentMDIWnd)
			::SendMessage(m_hParentMDIWnd, WM_COMMAND, WM_SIZE, 0);
	}

	if (msg == WM_SYSCOMMAND && p1 == SC_KEYMENU && gShellAttr->IsDevenv7())
	{
		// [case: 42408] vs2002/3 bindings defined in ctc file only work with
		// 'default settings' scheme, so need to sniff for these.
		// Handle unbound key presses Alt+{g,m,o,O}
		// VS2003 passes char always in upper case, revert to proper case.
		char c = (char)(ISPRESSED(VK_SHIFT) ? toupper((int)p2) : tolower((int)p2));

		switch (c)
		{
		case 'g':
			GoToDef(posAtCaret);
			return TRUE;
		case 'm':
			DisplayDefinitionList();
			return TRUE;
		case 'O':
			extern void VAOpenFileDlg();
			VAOpenFileDlg();
			return TRUE;
		case 'o':
			OpenOppositeFile();
			return TRUE;
		}
	}

	if (msg == WM_COMMAND && p1 == WM_SIZE)
	{
		if (m_hParentMDIWnd)
			return ::SendMessage(m_hParentMDIWnd, WM_COMMAND, WM_SIZE, 0);
		else
			return 0;
	}

	switch (msg)
	{
	case WM_IME_CHAR:
		SetTimer(ID_IME_TIMER, 100, NULL);
		break;

	case WM_CLOSE:
	case WM_DESTROY:
		if (!m_stop)
		{
			try
			{
				OnClose();
			}
			catch (...)
			{
				VALOGEXCEPTION("EDC:");
				LOG("Error: Onclose exception caught.");
			}

			return true;
		}
		break;

	case WM_SYSKEYDOWN:
		if (gShellAttr->IsDevenv7())
		{
			// [case: 42408] vs2002/3 bindings defined in ctc file only work with
			// 'default settings' scheme, so need to sniff for these.
			LONG key = (LONG)p1;

			if ((key == VK_DOWN || key == VK_UP) && ISPRESSED(VK_MENU) && !ISPRESSED(VK_CONTROL) && !ISPRESSED(VK_SHIFT))
			{
				GotoNextMethodInFile(key == VK_DOWN);
				return TRUE;
			}

			switch (key)
			{
			case VK_LEFT:
				if (ISPRESSED(VK_MENU) && !ISPRESSED(VK_CONTROL) && !ISPRESSED(VK_SHIFT))
				{
					NavGo(true);
					return TRUE;
				}
				break;

			case VK_RIGHT:
				if (ISPRESSED(VK_MENU) && !ISPRESSED(VK_CONTROL) && !ISPRESSED(VK_SHIFT))
				{
					NavGo(false);
					return TRUE;
				}
				break;
			}
		}
		break;
	}

	if (g_inMacro && gShellAttr->IsMsdev())
	{
		if ((msg >= WM_VA_FIRST && msg <= WM_VA_LAST && msg != WM_VA_WPF_GETFOCUS) ||
		    (msg == WM_COMMAND && (p1 == VAM_SORTSELECTION || p1 == VAM_PARENBLOCK || p1 == VAM_ADDBRACE)))
		{
			g_inMacro = false; // this is a message from the addin side
		}
	}

	DISABLECHECKCMD(CWnd::WindowProc(msg, p1, p2));
#ifdef _DEBUG
	// sometimes there is a long delay that does not show in logfile
	// this so we can break in w/ debugger and enable logging
	static int logall = false;

	if (logall)
	{
		static DWORD lasttime = GetTickCount();
		char buf[100];
		sprintf(buf, "Msg %x, %ld \n", msg, GetTickCount() - lasttime);
		lasttime = GetTickCount();
		LOG(buf);
	}
#endif

	static int deep = 0;
	extern EdCnt* g_paintingEdCtrl;
	g_paintingEdCtrl = this;
	deep++;
	LRESULT r = FALSE;

	if (Psettings->m_catchAll)
	{
#if !defined(SEAN)
		try
#endif // !SEAN
		{
#if _MSC_VER <= 1200
#if !defined(SEAN)
			try
#endif // !SEAN
#endif
			{
				r = CTer::WindowProc(msg, p1, p2);
			}
#if _MSC_VER <= 1200
#if !defined(SEAN)
			catch (CException e)
			{
				VALOGEXCEPTION("EDC:");
			}
#endif // !SEAN
#endif
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("EDC:");
			static bool once = true;
			if (once)
			{
				WTString err;
				try
				{
					err.WTFormat("Exception caught while processing the following message\n0x%x, 0x%zx, 0x%zx", msg,
					             (uintptr_t)p1, (uintptr_t)p2);
					once = false;
					VALOGEXCEPTION(err.c_str());
#ifdef _DEBUG
					if (ErrorBox(err, MB_OKCANCEL) != IDOK)
						r = CTer::WindowProc(msg, p1, p2);
#endif
				}
				catch (...)
				{
					WTString errmsg;
					errmsg.WTFormat("EDC:%x, %zx, %zx ", msg, (uintptr_t)p1, (uintptr_t)p2);
					VALOGEXCEPTION(errmsg.c_str());
					// stack over flow
				}
			}
		}
#endif // !SEAN
	}
	else
		r = CTer::WindowProc(msg, p1, p2);
	if (!--deep)
		g_paintingEdCtrl = NULL;
	return r;
}

EdCnt::~EdCnt()
{
	LOG("~EdCnt");

	ReleaseSelfReferences();

	if (m_vaOutlineTreeState)
	{
		delete m_vaOutlineTreeState;
		m_vaOutlineTreeState = NULL;
	}

	VASmartSelect::ActiveSettings().OnEdCntDestroy(this);

	if (m_ttParamInfo)
	{
		delete m_ttParamInfo;
		m_ttParamInfo = NULL;
	}
	if (m_ttTypeInfo)
	{
		ArgToolTip* tmp = m_ttTypeInfo;
		m_ttTypeInfo = nullptr;
		delete tmp;
	}
	if (m_lborder)
	{
		m_lborder->DestroyWindow();
		m_lborder = nullptr;
	}

	m_LnAttr = nullptr;

	if (gShellAttr && gShellAttr->IsDevenv() && m_VSNetDoc)
	{
		// free container allocated in vassistnet
		void* tmp = m_VSNetDoc;
		m_VSNetDoc = nullptr;

		// [case: 103869] watch out for thread deadlock during shutdown
		if (!gShellIsUnloading || (GetCurrentThreadId() == g_mainThread))
			SendVamMessage(VAM_DeleteDoc, (WPARAM)tmp, NULL);
		else
			; // skipping the sendmessage is a source of a leak at exit in vassistnet.dll
	}
}

#include "fontsettings.h"
long g_haltImportCnt = 0;
static uint g_statusDelay = 100u;
static CPoint s_hoverPt;

void EdCnt::OnTimer(UINT_PTR id)
{
#if !defined(SEAN)
	vCatLog("Editor.Timer", "EdTmr:%zd", id);
#endif // !SEAN
	//	DISABLECHECK();  // continue processing timer even if va is disabled
	DEFTIMERNOTE(OnTimerTimer, itos((int)id));
	if (this->m_stop)
		return; // assert dlg while closing, members may be whacked

	// I'd like to put this in GetBuf, but it needs to be thread safe, so here it is
	if (m_preventGetBuf)
	{
		// Preserves "default" indentation in VC6
		if (m_preventGetBuf == -1)
			m_preventGetBuf = (int)CurPos();
		else if (m_preventGetBuf != (int)CurPos())
		{
			m_preventGetBuf = 0;
			OnModified();
		}
	}

	switch (id)
	{
	case DSM_LISTMEMBERS:
		KillTimer(id);
		KillTimer(DSM_VA_LISTMEMBERS);
		if (gShellSvc->HasBlockModeSelection(this))
		{
			// [case: 64604]
			break;
		}

		if (!g_CompletionSet->IsExpUp(NULL)) // List our members if theirs isn't up.
			::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_LISTMEMBERS, NULL);
		if (!g_CompletionSet->GetIVsCompletionSet()) // List our members if theirs isn't up.
		{
			if (gTestsActive)
				SetTimer(DSM_VA_LISTMEMBERS, gTestsActive ? 600u : 200u, NULL);
			else if (gShellAttr->IsDevenv16OrHigher() && Psettings->m_bUseDefaultIntellisense && IsCFile(m_ftype) &&
			         GetParseDb()->m_xref)
				SetTimer(DSM_VA_LISTMEMBERS, 400u, 0); // [case: 142247]
			else
				SetTimer(DSM_VA_LISTMEMBERS, 200u, NULL);
		}
		break;
	case DSM_VA_LISTMEMBERS:
		KillTimer(id);
		if (gShellSvc->HasBlockModeSelection(this))
		{
			// [case: 64604]
			break;
		}

		if (!g_CompletionSet->GetIVsCompletionSet()) // List our members if theirs isn't up.
			CmEditExpand(ET_EXPAND_MEMBERS);         // don't insert tab
		break;

	case WM_SETFOCUS:
		KillTimer(id);
		if (!::IsMinihelpDropdownVisible())
		{
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
				g_currentEdCnt->vSetFocus();
		}
		break;
	case HOVER_CLASSVIEW_OUTLINE_TIMER:
		KillTimer(id);
		if (g_CVAClassView)
			g_CVAClassView->GetOutline(FALSE);
		break;
	case VSNET_EXPANDED_TIMER:
		KillTimer(id);
		{
			const WTString kOldBuf(GetBufConst());
			GetBuf(TRUE);
			const WTString bb(GetBufConst());
			if (bb.GetLength() != kOldBuf.GetLength() || bb != kOldBuf)
			{
				FixUpFnCall();
			}
		}
		SetTimer(ID_TIMER_GETHINT, 10, NULL);
		break;
	case ID_FILE_SAVE:
		KillTimer(id);
		{
			CStringW fname(FileName());
			// file saved, virus checker may have nuked the file,
			if (gShellAttr->IsDevenv())
			{
				LPCWSTR file = (LPCWSTR)SendVamMessage(VAM_FILEPATHNAMEW, (WPARAM)m_VSNetDoc, NULL);
				if (file) // in case save as changed name
				{
					fname = ::NormalizeFilepath(file);
					AutoLockCs l(mDataLock);
					filename = fname;
				}
			}
			if (fname.GetLength() && !IsFile(fname))
			{
				if (gShellAttr->IsDevenv())
				{
					vSetFocus();
					SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("File.SaveAll"), 0);
					fname = FileName();
					if (!IsFile(fname) && fname.GetLength() > 2 && fname[1] == L':')
					{ // don't do for http://foo.htm
						CStringW msg;
						CString__FormatW(
						    msg,
						    L"\"%s\" was not saved and no longer exists. This problem occurred due to a conflict between "
						    L"VS.NET and real-time, anti-virus software. Please be sure to save the file again.",
						    (LPCWSTR)fname);
						::WtMessageBox(msg, L"Warning", MB_ICONWARNING);
					}
				}
			}
		}
		break;
	case ID_TIMER_RELOAD_LOCAL_DB:
		// Reload Local DB
		KillTimer(id);
		{
			// make sure our buffer is up to date6
			static CWnd* lw = NULL;
			CWnd* w = FromHandle(VAGetFocus());
			const CStringW fname(FileName());
			if (w != this || (lw == w && s_LastReparseFile == fname))
				break;
			if (GlobalProject->IsBusy() || m_FileHasTimerForFullReparse)
			{
				// wait for main db to finish loading
				SetTimer(ID_TIMER_RELOAD_LOCAL_DB, 200, NULL);
				break;
			}
			lw = w;
			s_LastReparseFile = fname;
			UpdateWindow();
			GetBuf(TRUE);

			int edcntsWithRetainedDbs = 0;
			{
				AutoLockCs l(g_EdCntListLock);
				for (EdCntPtr ed : g_EdCntList)
				{
					// check Modified() because buf should no be emptied for Find References/Reparse
					// to unsaved code later
					if (this != ed.get() && !ed->Modified() && ::IsWindow(*ed))
					{
						if (ed->GetBufConst().IsEmpty())
						{
							// [case: 18826] if buf is empty, mp should already be empty, so no need to clear it
						}
						else if (gShellAttr->IsDevenv10OrHigher() && ++edcntsWithRetainedDbs < 15)
						{
							// [case: 70037]
							// each EdCnt m_pmparse takes up ~490K (in a debug build).
							// allow open edcnts to retain local db to reduce reclassification flicker.
							// cap to 15 open edcnts to limit memory impact.
						}
						else
						{
							MultiParsePtr mp = MultiParse::Create(ed->m_ftype);
							MultiParsePtr mpOld(ed->GetParseDb());
							mpOld->Stop();
							mp->m_showRed = false;
							mp->SetCacheable(TRUE);
							ed->UpdateBuf(WTString{}, false);
							ed->SetNewMparse(mp, FALSE);
						}
					}
				}
			}

			if (g_ParserThread)
				g_ParserThread->QueueParseWorkItem(new ReloadLocalDic(mThis));
		}
		break;
	case ID_TIMER_RTFCOPY:
		KillTimer(id);
		if (Psettings->m_RTFCopy && (m_ftype == Src || m_ftype == Header))
			RTFCopy(this);
		break;
	case ID_UNDERLINE_ERRORS: // underline screen
	{
		MultiParsePtr mp(GetParseDb());
		if (mp->m_showRed && g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending())
		{
			if (gAutoReferenceHighlighter && gAutoReferenceHighlighter->IsActive())
				gAutoReferenceHighlighter->Cancel();
			break; // Do not underline while other files are being reparsed
		}
		KillTimer(id);
		if (mp->m_showRed && HasFocus() && !GlobalProject->IsBusy() && !m_FileHasTimerForFullReparse)
		{
			if (::IsUnderlineThreadRequired())
			{
				SetStatusInfo();
				ReparseScreen(TRUE, TRUE);
			}
			else if (::IsAutoReferenceThreadRequired())
				new AutoReferenceHighlighter(mThis);
		}
	}
	break;
	case ID_IME_TIMER:
		KillTimer(id);
		Invalidate(TRUE);
		UpdateWindow();
		break;
	case ID_TIMER_CHECKFORSCROLL: {
		KillTimer(id);
		if (gShellAttr->IsDevenv10OrHigher())
			break; // Handled elsewhere...
		if (gShellAttr->IsDevenv())
		{
			CPoint pt = vGetCaretPos();
			if (pt.y != m_vpos) // vscroll
			{
				if (g_CompletionSet->IsExpUp(NULL))
					vLog("EdScrollx: %d:%d,  %ld:%ld", m_hpos, m_vpos, pt.x, pt.y);
				ClearAllPopups(true);
			}
			else if (pt.x != m_hpos && (abs(pt.x - m_hpos) > (g_FontSettings->GetCharWidth() * 5))) // hscroll
			{
				if (g_CompletionSet->IsExpUp(NULL))
					vLog("EdScroll2: %d:%d,  %ld:%ld", m_hpos, m_vpos, pt.x, pt.y);
			}
			m_vpos = pt.y;
			m_hpos = pt.x;
			break;
		}

		CPoint pt = GetCharPos(0);
		if (HasFocus())
			NavAdd(FileName(), CurPos());
		if (gShellAttr->IsDevenv() && (abs(pt.x - m_hpos) > (g_FontSettings->GetCharWidth() * 5)) || pt.y != m_vpos)
		{
			// some key moved caret
			if (gShellAttr->IsDevenv() ||
			    (pt.x > 0 && pt.y > 0)) // downarrow while listbox is up will cause an invalid pt.
			{
				vCatLog("Editor.Timer", "EdTmr2: %ld:%ld", pt.x, pt.y);
				ClearAllPopups(true);
			}
		}
		m_vpos = pt.y; // don't need to invalidate on vertical movements

		if (pt.x != m_hpos || pt.y != m_vpos)
		{
			m_hpos = pt.x;
			m_vpos = pt.y;
			if (!m_ignoreScroll)
			{
				// only repaint if we were not expecting scroll
				Invalidate(TRUE);
				if (m_hParentMDIWnd)
					::RedrawWindow(m_hParentMDIWnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
			}
		}
	}
		m_ignoreScroll = FALSE;
		break;
	case ID_TIMER_GETHINT: {
		if (EdPeekMessage(GetSafeHwnd()))
			break;
		KillTimer(id);
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			// http://65.61.131.26/forum/topic.asp?TOPIC_ID=437
			// introduced bug in 1091, wrap here for now till we can find it -Jer
			// ... Found it!  leaving try just in case there are others. -Jer
			if (gShellAttr->IsDevenv() && HasVsNetPopup(TRUE) && g_CompletionSet->m_popType != ET_SUGGEST)
				break;
			if (gHasMefQuickInfo)
			{
				if (!gShellAttr->IsDevenv15u6OrHigher())
					break;

				// [case: 114018]
				// IsQuickInfoActive is unreliable, so only use in 15.6
				if (IsQuickInfoActive())
					break;

				gHasMefQuickInfo = false;
			}
			GetCWHint();
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("EDC:");
			Log("GetCWHint exception caught");
			ASSERT(FALSE);
		}
#endif // !SEAN
	}
	break;
	case ID_TIMER_NUKEPOPUPS:
		KillTimer(id);
		ClearAllPopups(true);
		break;
	case ID_DOC_MODIFIED_TIMER:
		if (RefactoringActive::IsActive() || m_hasRenameSug)
			break;
		KillTimer(id);
		QueForReparse();
		break;
	case ID_GETBUFFER:
		KillTimer(id);
		if ((GetKeyState(VK_LBUTTON) & 0x1000) || (Psettings && Psettings->m_incrementalSearch))
			break;
		//////////////////////////////////////////////
		// check for save as or window recycling?
		CheckForSaveAsOrRecycling();
		/////////////////////////////////////
		/****************************************************/
		// DO NOT call HasSelection() or m_pDoc->GetCurSelection() in this condition!!
		// That's the main cause of the morning after bug.
		if (HasFocus())
		{
			if (m_hasSel)
			{
				// had a selection, reset the timer
				SetTimer(ID_GETBUFFER, 1000, NULL);
			}
			else
			{
				// Check for selection margin
				// OnSetFocus is too early to check, so we check on this
				// timer set onfocus.
				CPoint pt = GetCharPos(0);
				if (pt.x >= 0)
				{ // only if valid caret pos
					if (pt.x == 0 || pt.x < (LONG)Psettings->m_borderWidth)
						Psettings->m_borderWidth = 0;
					else
						Psettings->m_borderWidth = 20;
				}

				KillTimer(id);
				int oldlen = GetBufConst().GetLength();

				GetBuf(TRUE);
				Scope(true); // reget scope even if we are in the same pos
				// if buf has changed, force sinkdb to true
				if (oldlen && (oldlen != GetBufConst().GetLength()))
				{
					OnModified();
				}
				CurScopeWord();
			}
		}
		else
		{
			const HWND hFoc = VAGetFocus();
			//			CString msg;
			//			msg.Format("getbuf focus fail: hFoc(%p) this(%p) currented(%p) hwnd(%p)\n", hFoc, this,
			// g_currentEdCnt, m_hWnd); 			OutputDebugString(msg);
			EdCntPtr curEd = g_currentEdCnt;
			if (hFoc && curEd && curEd.get() != this && hFoc == curEd->GetSafeHwnd())
			{
				// case=10543
				// killtimer sometimes kills the timer for another window;
				// noted when using File Open to open a large file.
				// The previous ed fires the timer on focus after file open dlg is closed.
				// The new ed fires the timer in the ctor.
				// The previous ed kills the timer when it handles the timer it set.
				// The new ed doesn't get the timer.
				// Changing the delay of the timer set in the ctor helps but re-firing
				// gets more consistent results.
				curEd->SetTimer(ID_GETBUFFER, 200, NULL);
			}
		}
		break;
	case IDT_REPARSE_AFTER_PASTE:
		KillTimer(id);
		{
			int reparseType = REPARSE_LINE;
			if (g_VATabTree)
			{
				const CStringW clipTxt(g_VATabTree->GetClipboardTxt());
				if (EolTypes::eolNone != EolTypes::GetEolType(clipTxt))
				{
					// could have pasted var decs
					reparseType = REPARSE_SCREEN;
				}
			}
			Reparse();
		}
		break;
	case ID_CURSCOPEWORD_TIMER:
		KillTimer(id);
		// needParse = true; // not sure why this was set here?
		if (HasFocus()) // This was !HasFocus(), which has to be a typo? (since 2003)?
		{
			SetStatusInfo();
		}
		// CurScopeWord(0);
		break;
	case ID_ADDMETHOD_TIMER:
		KillTimer(id);
		if (!HasFocus())
		{
			// forward to window with focus...
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd && curEd.get() != this)
				curEd->SetTimer(ID_ADDMETHOD_TIMER, 50, NULL);
			break;
		}
		if (g_VATabTree)
			g_VATabTree->AddMethod(Scope().c_str(), FileName(), CurLine() - 1);
		NavAdd(FileName(), CurPos());
		break;
	case ID_TIMER_MOUSEMOVE:
		//		KillTimer(id);
		//		if(gShellAttr->IsDevenv())
		//			break;inser
		//	case 0x66: // vsnet hover
		GetCursorPos(&s_hoverPt);
		if (Psettings->mDisplayRefactoringButton &&
		    IsFeatureSupported(Feature_Refactoring, m_ScopeLangType)) // only show refactoring in c/c#/uc/vb files
		{
			if (HasFocus() || (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->m_hWnd))
			{
				if (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->m_bContextMenuUp)
				{
					KillTimer(id);
					return;
				}
				CPoint pt;
				GetCursorPos(&pt);
				vScreenToClient(&pt);
				CRect rc;
				vGetClientRect(&rc);
				m_vpos = vGetCaretPos().y; // prevent Check for scroll from dismissing icon
				if (rc.PtInRect(pt) && g_ScreenAttrs.m_VATomatoTip)
					g_ScreenAttrs.m_VATomatoTip->OnHover(mThis, pt);
			}
		}
		if (!IsFeatureSupported(Feature_HoveringTips, m_ScopeLangType) ||
		    gShellAttr->IsDevenv10OrHigher()) // Handled in MEF via GetDefFromPos() [case=42348]
		{
			KillTimer(id);
			CTer::OnTimer(id);    // Let them handle the tip
			HasVsNetPopup(FALSE); // allows for coloring
			break;
		}
		{
			char txt[255];
			::GetWindowText(MainWndH, txt, 255);

			// only let them provide tooltip if debugging or UseDefaultIntellisense is enabled
			// we need a way to determine if stopped at a breakpoint in all non-English versions...

			if (gShellAttr->IsDevenv8OrHigher())
			{
				// if title contain (xxx) and not (Running), let vs8 do debug tooltips
				if (strstr(txt, " (") && strstr(txt, ") - ") && !strstr(txt, " (Running) - "))
				{
					CTer::OnTimer(id);
					break;
				}
			}
			else if (gShellAttr->IsDevenv7())
			{
				// Looking for [break] only works in English versions
				// if title contain [xxx] and not [run], let vs.net do debug tooltips
				if (strstr(txt, "]") && !strstr(txt, "[run]"))
				{
					if (!strstr(txt, "[design]"))
					{
						CTer::OnTimer(id);
						break;
					}
				}
			}
			else if (gShellAttr->IsMsdev())
			{
				// VC6, "C++ - [file]" is not debugging, yet "C++ [break]..." is,
				// so, we use  the "++ [" for non English versions.
				if (strstr(txt, "++ [") && !strstr(txt, "[run]"))
				{
					KillTimer(id);
					break;
				}
			}
		}

		KillTimer(id);

		{
			bool passOffToDevenv = gShellAttr->IsDevenv();
			// don't display if there is a selection, our text may be out of sink
			if (HasFocus() && !m_contextMenuShowing)
			{
				if ((m_ttParamInfo && m_ttParamInfo->m_currentDef > 0))
					break; // do not display if arginfo is up. case 21040
				passOffToDevenv = false;
				if (!m_hasSel)
				{
					if (!g_CompletionSet->IsExpUp(NULL) && !HasVsNetPopup(FALSE))
						passOffToDevenv = !DisplayTypeInfo();
				}
			}

			if (passOffToDevenv)
				CTer::OnTimer(id);
		}
		break;
	case HOVER_CLASSVIEW_TIMER:
		KillTimer(id);
		if (g_CVAClassView && g_CVAClassView->m_hWnd && IsFeatureSupported(Feature_HCB, m_ScopeLangType))
		{
			CRect rw;
			CPoint pt;
			vGetClientRect(&rw);
			vClientToScreen(&rw);
			GetCursorPos(&pt);
			if (rw.PtInRect(pt) && pt == s_hoverPt)
			{
				g_CVAClassView->GetWindowRect(rw);
				HWND h = ::WindowFromPoint(rw.CenterPoint());
				if (h == g_CVAClassView->m_hWnd || ::GetParent(h) == g_CVAClassView->m_hWnd)
				{
					DisplayClassInfo(NULL);
					if (gShellAttr->IsDevenv7() || // fix for missing tooltips in vs2005
					    (Psettings->m_mouseOvers && Psettings->mQuickInfoInVs8))
						KillTimer(ID_TIMER_MOUSEMOVE);
					KillTimer(HOVER_CLASSVIEW_TIMER);
				}
			}
		}
		break;
	case ID_TIMER_CLOSE:
		KillTimer(id);
		ReleaseSelfReferences();
		return;
	case ID_ARGTEMPLATE_DISPLAY_TIMER:
		// [case:16970] delay so that VS gets a chance to display before us
		KillTimer(id);
		if (HasVsNetPopup(FALSE))
			DisplayToolTipArgs(false);
		else
			DisplayToolTipArgs(true);
		break;
	case ID_ARGTEMPLATE_CHECKVS_TIMER:
		KillTimer(id);
		if (HasVsNetPopup(FALSE))
			DisplayToolTipArgs(false);
		break;
	case ID_ARGTEMPLATE_CHECKVS_TIMER2:
		KillTimer(id);
		if (HasVsNetPopup(FALSE))
			DisplayToolTipArgs(false);
		break;
	case ID_ARGTEMPLATE_TIMER: {
		KillTimer(id);
		Scope(true);
		if (HasVsNetPopup(FALSE))
		{
			DisplayToolTipArgs(false);
			break;
		}

		MultiParsePtr mp(GetParseDb());
		if (IsFeatureSupported(Feature_ParamTips, m_ScopeLangType) && mp->m_inParamList && !mp->m_argCount)
		{
			DisplayToolTipArgs(TRUE);
			return;
		}

		if (gShellAttr->IsDevenv() &&
		    (!IsFeatureSupported(Feature_ParamTips, m_ScopeLangType) || Psettings->m_bUseDefaultIntellisense))
		{
			if (gShellAttr->IsDevenv7() &&
			    (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible())) &&
			    !HasVsNetPopup(FALSE))
			{
				::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_PARAMINFO, 0);
			}

			if (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible())
			{
				// [case: 42352] don't return if our paramInfo is actually in use
				if (HasVsNetPopup(FALSE))
				{
					// but if theirs is also up, then close ours
					DisplayToolTipArgs(false);
					return;
				}
			}
			else
				return;
		}

		// use mparses open paren location, brc1 may be wrong
		if (mp->m_inParenCount)
		{
			if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
				DisplayToolTipArgs(true);
		}
		else
			DisplayToolTipArgs(false);
	}
	break;
	case ID_ARGTEMPLATEERASE_TIMER:
		KillTimer(id);
		DisplayToolTipArgs(false, true);
		break;
	case IDT_PREPROC_KEYUP: {
		MSG msg;
		KillTimer(id);
		if (PeekMessage(&msg, *this, 0, 0xffffffff, PM_NOREMOVE))
			return;
		DWORD t = GetTickCount();
		InterlockedIncrement(&g_haltImportCnt);
		Reparse();
		InterlockedDecrement(&g_haltImportCnt);
		g_statusDelay = ((GetTickCount() - t) > 60) ? 200u : 50u;
	}
	break;
	case ID_TIMER_GETSCOPE: {
		KillTimer(id);
		if (!HasFocus())
			break;
		WTString scope = Scope(TRUE);
		if (!scope.GetLength())
			break;
	}
		// continue on to ID_KEYUP_TIMER:
	case ID_KEYUP_TIMER: {
		//			MSG msg;
		//			if(PeekMessage(&msg, *this, 0, 0xffffffff, PM_NOREMOVE))
		//				return;
		KillTimer(id);
		if (!HasFocus())
			break;
		if (g_CompletionSet->IsExpUp(mThis) && g_CompletionSet->IsFileCompletion())
			break; // [case: 105151]
		if (!HasSelection())
		{
			DWORD t = GetTickCount();
			bool typing = m_typing;
			SetStatusInfo();
			m_typing = typing;
			g_statusDelay = ((GetTickCount() - t) > 60) ? 200u : 50u;
		}
		else
			SetStatusInfo(); // display scope after find next
	}
	break;
	case ID_TIMER_CheckMinihelp:
		KillTimer(id);
		if (Psettings->m_noMiniHelp)
			return;
		if (!HasFocus())
			return;
		if (g_CompletionSet->IsExpUp(mThis))
			return;
		if (m_hParentMDIWnd)
			::SendMessage(m_hParentMDIWnd, WM_VA_CheckMinihelp, 0, 0);
		break;
	case ID_SINKME_TIMER:
		KillTimer(id);
		if (LastLn != CurLine()) // class browser or outputwin clicked
			SetTimer(ID_ADDMETHOD_TIMER, 200, NULL);

		CheckForSaveAsOrRecycling();
		// allow quick response with first SinkMe
		// and follow up with second to test for file changes
		break;
	case ID_HELP_TIMER:
		KillTimer(id);
		// repost help command...
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, 0xe146, 0);
		break;
	case IDT_CopyClipboard:
		KillTimer(id);
		// [case: 75779] copy clipboard and save
		{
			const CStringW clipTxt(::GetClipboardText(GetSafeHwnd()));
			if (g_VATabTree && Psettings->m_clipboardCnt && clipTxt.GetLength())
				g_VATabTree->AddCopy(clipTxt);
		}
		break;
	case IDT_DelayInit:
		KillTimer(id);
		if (gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 99761]
			EdCntWPF* wpfEd = dynamic_cast<EdCntWPF*>(mThis.get());
			if (wpfEd)
			{
				int ln = wpfEd->CurLine();
				if (ln++ > 0)
					wpfEd->ScrollToEnsureLinesVisible(ln, ln);
			}
		}
		break;
	default:
		// KillTimer(id);
		CTer::OnTimer(id);
	}
}

EdCnt* g_paintingEdCtrl = NULL; // TextOut will use for syntax coloring
BOOL g_inPaint = FALSE;
CPoint g_caretPoint;

// WINUSERAPI BOOL WINAPI PrintWindow(__in  HWND hwnd, __in  HDC hdcBlt, __in  UINT nFlags);
typedef BOOL(__stdcall* PrintWindowFn)(HWND hwnd, HDC hdcBlt, UINT nFlags);
PrintWindowFn PrintWindowF = NULL;

bool EnvironmentSupportsRemoteDesktopOptimization()
{
	static Library sDwmDll;
	// HRESULT DwmIsCompositionEnabled(BOOL *pfEnabled);
	typedef HRESULT(__stdcall * DwmIsCompositionEnabledFn)(BOOL * pfEnabled);
	static DwmIsCompositionEnabledFn DwmIsCompositionEnabledF = NULL;

	if (!PrintWindowF)
	{
		HMODULE hUser = GetModuleHandleA("User32.dll");
		if (hUser)
		{
			PrintWindowF = (PrintWindowFn)(uintptr_t)GetProcAddress(hUser, "PrintWindow");
			if (PrintWindowF)
			{
				sDwmDll.Load("dwmapi.dll");
				if (sDwmDll.IsLoaded())
					sDwmDll.GetFunction("DwmIsCompositionEnabled", DwmIsCompositionEnabledF);
			}
		}
	}

	if (DwmIsCompositionEnabledF)
	{
		// [case: 24610] our remote desktop flicker fix causes problems on vista to
		// vista connections when aero is enabled
		BOOL glassEnabled = FALSE;
		if (SUCCEEDED(DwmIsCompositionEnabledF(&glassEnabled)) && glassEnabled)
			return false;
	}

	return !!PrintWindowF;
}

bool ShouldOptimizeForRemoteDesktop()
{
	if (gShellAttr->IsDevenv10OrHigher() ||
		gShellAttr->IsCppBuilder())
		return false;

	const bool isRemote = ::GetSystemMetrics(SM_REMOTESESSION) != 0;
	if (!isRemote)
		return false;

	// this variable is needed after tests have completed, before shutdown, to
	// allow automated close and exit without being stopped by the stupid prompt
	static bool sAstPromptResponse = false;
	if (sAstPromptResponse)
		return true;

	if (!Psettings->mOptimizeRemoteDesktop)
	{
		static bool sPrompted = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PROMPTED_FOR_REMOTEDESKTOP, false);
		if (sPrompted)
			return false;

		if (!EnvironmentSupportsRemoteDesktopOptimization())
			return false; // don't prompt if we can't even do it

		if (gTestsActive)
		{
			sAstPromptResponse = true;
			sPrompted = true;
			Psettings->mOptimizeRemoteDesktop = true;
			return true;
		}

		// mark now to prevent recursion during MsgBox message processing
		sPrompted = true;
		SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PROMPTED_FOR_REMOTEDESKTOP, sPrompted);

		const int res = WtMessageBox(
		    "During Remote Desktop sessions, heavy flicker can occur when Visual Assist is enabled.  "
		    "Visual Assist has optimizations that drastically reduce flicker during Remote Desktop, however "
		    "they can, in rare cases, result in DRIVER_FAULT system crashes.  "
		    "The optimizations can be manually enabled or disabled via the Performance page "
		    "of the Visual Assist Options dialog.  "
		    "If you enable the optimizations and a crash occurs, either check for an update to your "
		    "display adapter driver or disable the 'Optimize screen updates' option.  "
		    "\r\n\r\nWould you like to enable Remote Desktop optimizations?",
		    IDS_APPNAME, MB_YESNO | MB_ICONEXCLAMATION);

		Psettings->mOptimizeRemoteDesktop = res == IDYES;
		return Psettings->mOptimizeRemoteDesktop;
	}

	if (!EnvironmentSupportsRemoteDesktopOptimization())
		return false;

	return true;
}

void EdCnt::OnPaint()
{
	if (gShellAttr->IsDevenv10OrHigher() ||
		gShellAttr->IsCppBuilder())
	{
		// I dont think think any this is needed in WPF
		__super::OnPaint();
		return;
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (g_inPaint && Psettings->mOptimizeRemoteDesktop && ::GetSystemMetrics(SM_REMOTESESSION))
	{
		CTer::OnPaint();
		return;
	}

	GetCursorPos(&g_CursorPos);
	vScreenToClient(&g_CursorPos);
	g_StrFromCursorPos.Empty();
	g_StrFromCursorPosUnderlined = FALSE;

	if (!Psettings->m_enableVA) // color while recording macro's
	{
		Default();
		return;
	}
	if (m_typing)
	{
		g_caretPoint = vGetCaretPos();
		g_caretPoint.x -= 1;
	}
	// let window update....
	// SetRedraw(TRUE);
	extern int g_dopaint;
	if (g_dopaint)
	{
		int firstLine = m_firstVisibleLine;
		GetParseDb()->m_firstVisibleLine = GetFirstVisibleLine();
		if (firstLine != m_firstVisibleLine)
		{
			if (!RefactoringActive::IsActive() && !m_FileHasTimerForFullReparse)
				SetTimer(ID_UNDERLINE_ERRORS, 200, NULL);
			ClearAllPopups(true);
		}

		if (HasFocus())
			SetTimer(ID_TIMER_CHECKFORSCROLL, 100, NULL);

		g_paintingEdCtrl = this; // Give TextOut a handle to our db's for ASC
		{
			TempTrue t(g_inPaint);
			VAColorPaintMessages w(PaintType::SourceWindow);
			bool callBasePaint = true;

			if (ShouldOptimizeForRemoteDesktop())
			{
				// Flicker free remote desktop case=281
				// See http://www.codeproject.com/KB/macros/VSWallpaper.aspx
				RECT rc, cr;
				::GetUpdateRect(m_hWnd, &rc, FALSE);

				// the update rectangle is not empty

				if (!::IsRectEmpty(&rc))
				{
					::GetClientRect(m_hWnd, &cr);

					HDC hdc;
					hdc = ::GetDC(m_hWnd);

					// hide the caret temporarily
					::HideCaret(m_hWnd);

					// prepare an empty bitmap
					HBITMAP img = ::CreateCompatibleBitmap(hdc, cr.right - cr.left, cr.bottom - cr.top);
					if (img)
					{
						HDC hmem = ::CreateCompatibleDC(hdc);
						if (hmem)
						{
							HBITMAP hold = (HBITMAP)::SelectObject(hmem, (HGDIOBJ)img);
							if (hold)
							{
								if (::PrintWindowF(m_hWnd, hmem, PW_CLIENTONLY))
								{
									// blt it out
									if (::BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hmem,
									             rc.left, rc.top, SRCCOPY))
										callBasePaint = false;
								}

								// clean up  and validate
								::SelectObject(hmem, hold);
							}
						}

						::DeleteObject((HGDIOBJ)img);
						if (hmem)
							::DeleteDC(hmem);
					}

					::ReleaseDC(m_hWnd, hdc);

					if (!callBasePaint)
						::ValidateRect(m_hWnd, &rc);

					::ShowCaret(m_hWnd);
				}
			}

			if (callBasePaint)
				CTer::OnPaint();
		}

		//////////////////////////////////////////////////////////////////////////
		// draw col indicator
		CPoint p = vGetCaretPos();
		if (Psettings->m_colIndicator
		    //			&& GetCaretPos().x > 1
		    && HasFocus()
		    /*&& !(GetKeyState(VK_LBUTTON) & 0x1000)*/)
		{ // draw rmargin line
			HDC dc = GetDC();
			if (dc)
			{
				long mpos = TERRCTOLONG(0, 0);
				CPoint pt = GetCharPos(mpos);
				pt.x += g_vaCharWidth['m'] * Psettings->m_colIndicatorColPos;
				static CPoint lpt;
				if (pt.x != lpt.x)
				{
					Invalidate(TRUE);
				}
				lpt = pt;
				if ((pt.x > (LONG)Psettings->m_borderWidth) && g_FontSettings->GetCharHeight())
				{
					CRect rct;
					vGetClientRect(&rct);
					CPen pen;
					pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
					              Psettings->m_colors[C_ColumnIndicator].c_fg);
					::SelectObject(dc, pen.m_hObject);
					const int char_height = abs(g_FontSettings->GetCharHeight());

					for (pt.y = rct.top; pt.y < rct.bottom; pt.y += char_height)
					{
						::MoveToEx(dc, pt.x, pt.y, NULL);
						::LineTo(dc, pt.x, pt.y + 1);
						::MoveToEx(dc, pt.x, pt.y + char_height / 2, NULL);
						::LineTo(dc, pt.x, pt.y + char_height / 2 + 1);

						//						SetPixel(dc, pt.x, pt.y, clr);
						//						SetPixel(dc, pt.x, pt.y+(g_FontSettings->GetCharHeight()/2), clr);
					}
				}
				ReleaseDC(dc);
			}
		}
		// draw col indicator
		//////////////////////////////////////////////////////////////////////////
	}
	g_caretPoint.x = -999;
}

void EdCnt::SetStatusInfo()
{
	LogElapsedTime let("Ed:SSI", 75);

	SupportedFeatureDecoy_DontReallyUseMe(Feature_MiniHelp); // decoy call - prevents linker from removing decoy body
	if (g_currentEdCnt.get() == this && gVaService && gVaService->GetOutlineFrame() &&
	    gVaService->GetOutlineFrame()->IsAutoUpdateEnabled())
	{
		LiveOutlineFrame* lof = gVaService->GetOutlineFrame();
		lof->HighlightItemAtLine(CurLine());
	}

	if (Psettings->m_noMiniHelp)
	{
		Scope(); // for bold bracing
		return;
	}
	if (g_CompletionSet->IsExpUp(mThis))
		return;
	static int LstLine = 0;
	vCatLog("Editor", "SetStatusInfo");
	// Let ScopeInfo update Minihelp, skipping all this logic below.
	Scope();
	MultiParsePtr mp(GetParseDb());
	if (mp->UpdateMiniHelp())
		return;

	bool clearMiniHelp = true;
	bool isResWord = false;
	if (true)
	{
		if (LstLine != CurLine())
			LstLine = CurLine();
		WTString s;
		// don't do word to right, hoses find by ref, per jeff
		//		if(!HasSelection() && wt_isspace(CharAt(CurPos()-1)) && ISALPHA(CharAt(CurPos())))
		//			s = CurScopeWord(1);
		//		else
		s = CurScopeWord();
		m_hasSel = GetSelString().GetLength() != 0;
		WTString scope = Scope();
		WTString context;
		Psettings->m_SymScope[0] = '\0';
		const DType dt(GetSymDtype());
		const WTString ss(GetSymScope());
		if (ss.GetLength() > 0 && ss.GetLength() < 255)
		{
			MultiParsePtr mp2 = MultiParse::Create(m_ftype);
			if (mp2->def(ss).GetLength())
			{
				if ((dt.type() == CLASS) || (dt.type() == STRUCT) || ss.ReverseFind(DB_SEP_CHR) > 0)
					strcpy(Psettings->m_SymScope, ss.c_str());
			}
		}

		if (Psettings->m_contextPrefix)
		{
			if (dt.IsSysLib())
				context = "[System] ";
			else if (dt.HasLocalFlag())
				context = "[Local] ";
			else
				context = "[Project] ";
		}

		if (!m_txtFile && scope.length() && (scope[0] != DB_SEP_CHR))
		{
		}
		else if (!m_hasSel && s.length())
		{
			uint type = dt.type();
			if (type == DEFINE && IsFile(GetSymDef().Wide()))
				type = PREPROCSTRING; // in "quoted" part of  #include "file"
			switch (type)
			{
			case UNDEF:
			case CachedBaseclassList:
			case RESWORD:
			case LVAR:
				isResWord = true;
				break;
			case PREPROCSTRING: {
				m_lastMinihelpDef = GetSymDef();
				WTString dispSymScope = DecodeScope(ss);
				WTString dispSymDef = DecodeScope(m_lastMinihelpDef);
			
				UpdateMinihelp((LPCWSTR)dispSymScope.Wide(), (LPCWSTR)dispSymDef.Wide());
				clearMiniHelp = false;
			}
			break;
			case TYPE: // show def of namespace
			           // VAParse makes typedef's of type TYPE
			           //				if(!GetSymDef().contains("namespace")){
			           //					isResWord = true;
			           //					break;	// hide all others
			           //				}
			case DEFINE:
			case FUNC:
			case VAR:
			case CLASS:
			case STRUCT:
			case C_ENUM:
			default:
				if (!CAN_USE_NEW_SCOPE())
				{
					int type2, attrs2;
					type2 = attrs2 = 0;
					WTString newDef(GetSymDef());
					mp->Tdef(GetSymScope(), newDef, 1, type2, attrs2); // update symdef to include all defs
					AutoLockCs l(mDataLock);
					SymDef = newDef;
				}
				m_lastMinihelpDef = GetSymDef();
				const WTString newDef = CleanDefForDisplay(DecodeScope(m_lastMinihelpDef), m_ScopeLangType);
				{
					AutoLockCs l(mDataLock);
					SymDef = newDef;
				}

				token def = string(" ") + newDef.c_str();
				if (DEFINE == dt.MaskedType())
				{
					// [case: 1184] for A/W macro apis, show the non-macro variant
					const WTString apiDef(GetDefsFromMacro(GetSymScope(), newDef));
					if (apiDef.GetLength() && apiDef != "\f") // [case: 93422]
						def = WTString(" ") + CleanDefForDisplay(DecodeScope(apiDef), m_ScopeLangType);
				}

				if (!IsCFile(m_ftype))
				{
					def.ReplaceAll(" :: ", ".");
					def.ReplaceAll("::", ".");
					def.ReplaceAll(" : ", "\001");
					if (gTypingDevLang != JS)
						def.ReplaceAll(":", "."); // JS member definitions use : in a different way
					def.ReplaceAll("\001", " : ");
				}
				def.ReplaceAll(TRegexp("[\t]+"), string(" "));
				def.ReplaceAll(TRegexp("\\\\[\r\n]+"), string(" "));
				def.ReplaceAll(TRegexp("[\r\n]+"), string(" "));
				bool moreThanOneDef = false;
				if (def.ReplaceAll(TRegexp("[\f]+"), string("\n")))
					moreThanOneDef = true;
				def.Strip(TRegexp("^[ ]+"));
				token t = GetSymScope().c_str();
				t.ReplaceAll(TRegexp("^:"), string(""));
				t.ReplaceAll(TRegexp(":"), string("."));

				WTString d = def.c_str();
				if (moreThanOneDef)
					d += "\n";
				t.ReplaceAll("BRC", "{", true);

				const CStringW cw((context + t.c_str()).Wide());
				const CStringW dw(d.Wide());
				UpdateMinihelp((LPCWSTR)cw, (LPCWSTR)dw);
				clearMiniHelp = false;
			}
		}
		else
		{
			///////////////////
			// display line containing open brace
			if (g_ScreenAttrs.m_brc1->mPos && CharAt(CurPos()) == '}')
			{
				token tscope = Scope();

				token ln = GetSubString(LinePos((int)g_ScreenAttrs.m_brc1->mLine),
				                        LinePos((int)g_ScreenAttrs.m_brc1->mLine + 1));
				WTString fwd(ln.peek(" \t\r\n;"));
				if (!fwd.length() || fwd[0] == '{') // select prev line
					ln =
					    GetSubString(LinePos((int)g_ScreenAttrs.m_brc1->mLine - 1), g_ScreenAttrs.m_brc1->mPos).c_str();
				m_lastMinihelpDef = ln.c_str();
				ln.Strip(TRegexp("^[ 	]+"));
				ln.ReplaceAll(TRegexp("[\t\r\n]+"), string(" "));
				tscope.ReplaceAll(TRegexp("^:"), string(""));
				tscope.Strip(TRegexp(":$"));
				tscope.ReplaceAll(TRegexp(":"), string("."));
				tscope.ReplaceAll("BRC", "{", true);
				UpdateMinihelp((LPCWSTR)WTString(tscope.c_str()).Wide(),
				               (LPCWSTR)WTString(ln.c_str()).Wide());
				clearMiniHelp = false;
			}
		}
	}
	else
		CurScopeWord();

	bool isPreProcLine = m_lastScope == DBColonToSepStr(DB_SCOPE_PREPROC.c_str());
	if (!isPreProcLine && m_lastScope == DB_SEP_STR)
	{
		// when caret is at column 0, before #include status info is incorrect (different
		// than after the # char) because m_lastScope does not get get to DB_SCOPE_PREPROC until
		// after the # char.  Compensate here.
		const WTString bb(GetBufConst());
		int pos = GetBufIndex(bb, (long)CurPos());
		if (pos > 0 && pos < bb.GetLength() && bb[pos] == '#')
			isPreProcLine = true;
	}

	if (isPreProcLine)
	{
		token2 lineTok = GetSubString(LinePos(), LinePos(CurLine() + 1));
		const bool isInclude = lineTok.contains("include");
		if (isInclude || lineTok.contains("import") || lineTok.contains("using"))
		{
			BOOL doLocalSearch = lineTok.contains("\"");
			lineTok.read("<\""); // strip off #include "
			CStringW file = lineTok.read("<>\"").Wide();
			if (Psettings->m_nHCBOptions & 2)
			{
				// [case: 7156] display includes in hcb if caret on #include
				::QueueHcbIncludeRefresh(true);
			}

			if (file.GetLength())
			{
				if (isInclude)
					file = gFileFinder->ResolveInclude(file, ::Path(FileName()), doLocalSearch);
				else
					file = gFileFinder->ResolveReference(file, ::Path(FileName()));

				if (file.GetLength())
				{
					// [case: 798] see if we have a mixed case version already in the fileId db
					UINT fid = gFileIdManager->GetFileId(file);
					if (fid)
						file = gFileIdManager->GetFile(fid);
					DTypePtr fileData = GetParseDb()->GetFileData(file);
					{
						AutoLockCs l(mDataLock);
						SymType.copyType(fileData.get());
					}
					
					// [case: 164446]
					if (Psettings->mUseSlashForIncludePaths)
						file.Replace(L'\\', L'/');

					UpdateMinihelp((LPCWSTR)::Basename(file), (LPCWSTR)file);
					return;
				}
			}
		}
	}

	if (m_hasSel || clearMiniHelp /*&& !g_ListBoxLoader->IsShowingALWAYSFALSE()*/)
	{
		WTString cwd = CurWord();
		if (Psettings->m_showScopeInContext && (!isResWord || HasSelection()))
			DisplayScope(); // chooses when to display scope
		else
			ClearMinihelp();
		m_lastMinihelpDef = "";
	}
}

void EdCnt::UpdateMinihelp(LPCWSTR context, LPCWSTR def, LPCWSTR proj, bool async /*= true*/)
{
	if (m_hParentMDIWnd)
	{
		LPCWSTR ptrs[3] = {
		    context,
		    def,
		    proj ? proj : (LPCWSTR)mProjectName
		};

		::SendMessage(m_hParentMDIWnd, (UINT)(async ? WM_VA_MINIHELPW : WM_VA_MINIHELP_SYNC), (WPARAM)3, (LPARAM)ptrs);
	}
}

void EdCnt::ClearMinihelp(bool async /*= true*/)
{
	UpdateMinihelp(L"", L"", nullptr, async);
}

BOOL EdCnt::IsKeyboardFocusWithin()
{
	if (gShellAttr->IsDevenv10OrHigher())
	{
		EdCntWPF* wpfEd = dynamic_cast<EdCntWPF*>(this);
		if (wpfEd)
			return wpfEd->IsKeyboardFocusWithin();
	}

	return vGetFocus() == this;
}

#if !defined(RAD_STUDIO)
static bool IsCodeSmartPresent()
{
	static bool sCodesmartIsPresent = false;
	static int sContinueToCheckForCodeSmart = 1;
	// [case: 39473] place cap on number of times we check for code smart
	// because the FuckArm calls are long
	if (sContinueToCheckForCodeSmart && !sCodesmartIsPresent)
	{
		if (GetModuleHandleA("AxTools.CodeSMART.Connector.dll"))
		{
			sCodesmartIsPresent = true;
			sContinueToCheckForCodeSmart = 0;
		}
		else if (++sContinueToCheckForCodeSmart > 10)
			sContinueToCheckForCodeSmart = 0;
	}

	return sCodesmartIsPresent;
}
#endif

void EdCnt::OnKillFocus(CWnd* pNewWnd)
{
#if defined(RAD_STUDIO) && defined(_DEBUG)
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP OnKillFocus: HWND(%p) %s\n", GetSafeHwnd(), (LPCWSTR)FileName());
		::OutputDebugStringW(msg);
	}
#endif

	vLog("EdKFo: %p", pNewWnd);
	DISABLECHECK();
	// need this until we stop leaving redraw turned off - drag a
	//  window over us if we don't have focus and sometimes there's no update
	LastLn = (int)TERROW(
	    m_LastPos2); // CurLine(); use cached val because if window is closed, we will return the pos of the new window
	NavAdd(FileName(), (uint)m_LastPos2); // CurPos() invalid after losing focus in vs.net, use last pos
	bool b_isListBox = FALSE;
	const WTString cls = GetWindowClassString(pNewWnd->GetSafeHwnd());
	if (pNewWnd)
	{
#if defined(RAD_STUDIO)
		// watch for File New Window -- we get no activation from host when new editor for active file is created
		if (gVaRadStudioPlugin)
		{
			if (cls == "TEditControl")
				gVaRadStudioPlugin->NewEditorViewMaybeCreated();
		}
#else
		// [case: 828] Flip between design and html/XML needs to hide minihelp
		bool hideMinihelp = false;
		if (gShellAttr->IsDevenv8OrHigher())
		{
			if (cls == "Visual_Web_Design_Server" || cls == "FrontPageEditorDocumentView" ||
			    cls.contains("ATL:")) // case 20599
				hideMinihelp = true;
		}
		else
		{
			if (cls.contains("ATL:") || cls.contains("Internet Explorer"))
				hideMinihelp = true;
		}

		if (hideMinihelp && m_hParentMDIWnd)
			::SendMessage(m_hParentMDIWnd, WM_VA_DEVEDITCMD, WM_CLOSE, 0);
#endif
	}

	if (pNewWnd && g_CompletionSet && g_CompletionSet->IsExpUp(NULL))
	{
		//		if((CWnd*)(g_CompletionSet->m_expBox) == pNewWnd || ((CWnd*)g_CompletionSet->m_expBox)->GetToolBar() ==
		// pNewWnd)
		b_isListBox = TRUE;
	}
	if (pNewWnd == this || b_isListBox)
	{
		// expansion window is up - we're not really losing focus
		// BC5 don't let them see killfocus
		if (!gShellAttr->IsDevenv())
			CTer::OnKillFocus(pNewWnd);
		return;
	}
	// 	if(!IsWindow(m_hParentMDIWnd))
	// 		return; // parent has closed we should get a WM_DESTROY soon...
	if (Psettings->m_incrementalSearch && !gShellAttr->IsDevenv())
		return;
	if (m_ReparseScreen && pNewWnd           // so sinkdb can run...
	    && pNewWnd->GetDlgCtrlID() != 0x7400 // something in Reparse causes alt tab to leave focus in menu?
	)
		Reparse();
	if (m_FileHasTimerForFullReparse)
		OnModified(TRUE);
	/*	disable our sinking, undo will be weird until re-enable
	 */
	if (g_loggingEnabled && !cls.IsEmpty())
	{
		Log(cls.c_str());
	}

	bool doClearAllPopups = true;
#if !defined(RAD_STUDIO)
	if (gShellAttr->IsDevenv())
	{
		// if(pNewWnd && GetWindowClassString(pNewWnd->m_hWnd) != "GenericPane") // not sure why, paraminfo stays up on
		// ctrl-tab with this
		if (::IsCodeSmartPresent())
		{
			// in C++, CodeSMart causes this when tab is pressed, closing our suggestions
			if (pNewWnd && GetWindowClassString(pNewWnd->m_hWnd) != "GenericPane" &&
			    (GetKeyState(VK_TAB) & 0x1000)) // not sure why, paraminfo stays up on ctrl-tab with this
				;
			else
				doClearAllPopups = false;
		}
	}
#endif
	if (doClearAllPopups)
		ClearAllPopups();

	if (!gShellAttr->IsDevenv10OrHigher())
		CTer::OnKillFocus(pNewWnd);
}

#if !defined(RAD_STUDIO)
static void CheckForResourceUpdate(LPVOID param)
{
	if (!gFileFinder)
		return;

	if (!GlobalProject || GlobalProject->SolutionFile().IsEmpty())
	{
		// [case: 73418]
		// don't force parse of resource.h if no solution loaded
		return;
	}

	_ASSERTE(param);
	LPCWSTR filename = (LPCWSTR)param;

	CStringW f(L"resource.h");
	// Look relative to the current file
	f = gFileFinder->ResolveInclude(f, Path(filename), true);
	if (!f.IsEmpty())
	{
		if (!MultiParse::IsIncluded(f, TRUE))
			g_ParserThread->QueueFile(f);
	}
}
#endif

extern HFONT g_TxtFontObj;
void EdCnt::OnSetFocus(CWnd* pOldWnd)
{
#if defined(RAD_STUDIO) && defined(_DEBUG)
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP OnSetFocus: HWND(%p) %s\n", GetSafeHwnd(), (LPCWSTR)FileName());
		::OutputDebugStringW(msg);
	}
#endif

	static CWnd* sPreviousOldWnd = NULL;
	static CWnd* sPreviousNewWnd = NULL;
	static DWORD sPreviousTime = 0;

	const DWORD curTime = GetTickCount();
	EdCntPtr curEd = g_currentEdCnt;

	if (this == sPreviousNewWnd && !gShellAttr->IsDevenv10OrHigher() && sPreviousOldWnd == pOldWnd &&
	    this == curEd.get() && curTime < (sPreviousTime + 3000))
	{
		// case=10303: we get in a bad state during ctrl+tab in vs2008
		CWnd::OnSetFocus(pOldWnd);
		return;
	}

	sPreviousNewWnd = this;
	sPreviousOldWnd = pOldWnd;
	sPreviousTime = curTime;

	const bool kNoPreviousEdCnt = curEd == NULL;
	if (!gShellAttr->IsDevenv10OrHigher())
		CWnd::OnSetFocus(pOldWnd);

	if (curEd.get() != this)
	{
		if (gShellAttr->IsDevenv10OrHigher() && curEd->GetSafeHwnd() && ::IsWindow(curEd->GetSafeHwnd()))
		{
			try
			{
				curEd->OnKillFocus(this);
			}
			catch (...)
			{
			}
		}

		// NavAdd conceptually makes sense here, but in practice yields some not quite expected
		// results when closing a file and another one becomes active.
		// NavAdd(FileName(), CurPos());

		g_currentEdCnt = mThis;
		FindReferencesPtr globalRefs(g_References);
		if (globalRefs)
			globalRefs->Redraw();
		vCatLog("Editor.Events", "VaEventED  Activate  ln=%d, %s", CurLine(), (LPCSTR)CString(GetBaseName(FileName())));
	}

	// 	CString msg;
	// 	msg.Format("focus: prev(%p) prevh(%p) new(%p) newh(%p)\n", pOldWnd, pOldWnd->m_hWnd, this, this->m_hWnd);
	// 	OutputDebugString(msg);
	vCatLog("Editor.Events", "EdSFo: %p", pOldWnd);

	if (IsCFile(m_ftype) || m_ftype == RC || m_ftype == Idl)
		gTypingDevLang = Src;
	else
		gTypingDevLang = m_ScopeLangType;

	if (g_CVAClassView)
		g_CVAClassView->CheckHcbForDependentData();

	extern WCHAR g_watchDir[];
#if defined(RAD_STUDIO)
	// this hack doesn't work in RadStudio since it doesn't change app window text to indicate mod status
#else
	// Need to check Modify state incase file was changed when we did not have focus
	char txt[255];
	::GetWindowText(MainWndH, txt, 255);
	WTString txtStr(txt);
	if (txtStr.EndsWith("*") || txtStr.EndsWith("*]")) // VC6 uses *]
		modified = TRUE;
#endif
	m_skipUnderlining = -1;
	const CStringW fname(FileName());
	if (fname.GetLength() > 1 && fname[1] == L':')
	{
		CStringW dir = fname.Mid(0, 2) + L"\\";
		wcscpy(g_watchDir, dir);
	}

	SetTimer(ID_TIMER_RELOAD_LOCAL_DB, 200, NULL);
	// reset because there is a problem with va accidentally getting wrong font
	// I think the problem is related to help in vc6?
	g_TxtFontObj = NULL;

	// PostMessage(WM_COMMAND, WM_VA_REPARSEFILE, 0);
	if (g_FontSettings)
		g_FontSettings->DualMonitorCheck();
	if (Psettings->m_incrementalSearch)
		Psettings->m_incrementalSearch = false;
	DISABLECHECK();
	if (!(GetKeyState(VK_CONTROL) & 0x1000))
		VAProjectAddFileMRU(fname, mruFileFocus);
	DEFTIMERNOTE(OnSetFocusTimer, itos((int)pOldWnd));

	// [case: 24506] compensate for global g_screenXOffset
	if (m_IVsTextView)
	{
		extern int g_screenXOffset;
		extern int g_screenMargin;

		long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit;
		m_IVsTextView->GetScrollInfo(SB_HORZ, &iMinUnit, &iMaxUnit, &iVisibleUnits, &iFirstVisibleUnit);
		g_screenXOffset = g_screenMargin - (iFirstVisibleUnit * g_vaCharWidth['z']);
	}

	LOG("SetFocus");
	SetTimer(ID_GETBUFFER, 100, NULL);
	gAutotextMgr->Load(m_ScopeLangType);
	if (CTer::m_pDoc)
	{
		// get editor info in case it has changed
		CTer::m_pDoc->GetTestEditorInfo();
	}

	if (gShellAttr->IsDevenv())
	{
		// calling timer to fix maximizing of window clicked (change 4669, build 1275, "lots of changes")
		SetTimer(ID_KEYUP_TIMER, 500, NULL); //		SetStatusInfo();

		if (gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 85333] reduce flicker by reassigning the one va nav bar
			// sooner than ID_KEYUP_TIMER using previous text if available.
			SetTimer(ID_TIMER_CheckMinihelp, 75, nullptr);
		}
	}

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	if (!RefactoringActive::IsActive())
	{
		// Look for saved changes in resource.h
		if ((m_ftype == Src || m_ftype == Header) && !(g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()))
		{
			// [case: 68558] do in thread so FindFile doesn't block UI thread on setFocus
			new FunctionThread(CheckForResourceUpdate, fname, "CheckResUpdate", true);
		}

		// Look for unsaved changes in designer.cs file
		if (gShellAttr->IsDevenv8OrHigher() && (m_ftype == CS || Is_VB_VBS_File(m_ftype)))
		{
			CStringW designerfile = fname;
			CStringW ext = CStringW(L".") + GetBaseNameExt(fname);
			designerfile.Replace(ext, CStringW(L".designer") + ext);

			FileModificationState state = ::PkgGetFileModState(designerfile);
			if (fm_dirty == state)
			{
				const WTString txt2(::PkgGetFileTextW(designerfile));
				g_ParserThread->QueueFile(designerfile, txt2);
			}
		}
	}
#endif // !AVR_STUDIO

	// commented out cause this is called from DefWindowProc
	// CTer::OnSetFocus(pOldWnd);
	// CreateCaret(false, 5, 0);

#if !defined(RAD_STUDIO) // CppBuilder handles its own recycling
	/*
	 * Even though doing this might screw up debug arrows
	 *  a worse side-effect of not doing this during debugging
	 *  is that dbl-clicking in the output window or call stack
	 *  won't move us to the right line.
	 */
	if (!SetTimer(ID_SINKME_TIMER, 50, NULL))
	{
		Log("Timer Failed");
		ASSERT(FALSE);
	}
#endif

	if (g_ParserThread && kNoPreviousEdCnt && g_currentEdCnt.get() == this && gVaService &&
	    gVaService->GetOutlineFrame() && gVaService->GetOutlineFrame()->IsAutoUpdateEnabled())
	{
		g_ParserThread->QueueParseWorkItem(new RefreshFileOutline(mThis));
	}

#if !defined(VA_CPPUNIT) && !defined(RAD_STUDIO)
	UpdateProjectInfo(nullptr);
#endif
}

#include "addin/editparentwnd.h"

HWND VaAddinClient::AttachEditControl(HWND hMSDevEdit, const WCHAR* file, void* pDoc)
{
	vCatLog("Editor.Events", "VaEventED AttachEditor");
#if defined(VAX_CODEGRAPH)
	return nullptr;
#else
	if (!mLicenseInit)
		return nullptr;
	g_currentEdCnt = NULL;
	if (gVaService && gVaService->GetOutlineFrame())
		gVaService->GetOutlineFrame()->Clear();
	if (!gLicenseCountOk)
	{
		Psettings->m_enableVA = FALSE;
		return nullptr;
	}

	CStringW filename(file);
	const int i = filename.ReverseFind(L':');
	if (i > 1)
	{
		// not sure what this is doing?  maybe it thinks the filename
		// might have a line number at the end delimited by ':' ?
		const int pos2 = filename.Find(L':');
		if (pos2 != (i - 1)) // [case: 9609] don't chop mssql::blah/blah/blah
			filename = filename.Mid(0, i);
	}
	filename = ::NormalizeFilepath(filename);

	if (::StrIsUpperCase(filename))
	{
		// [case: 100684]
		// DTE sometimes reports filename as fully upper-case.
		// Check with out fileIdManager for an alternative.
		::FixFileCase(filename);
		uint fid = gFileIdManager->GetFileId(filename);
		filename = gFileIdManager->GetFile(fid);
	}

	if (!(GetKeyState(VK_CONTROL) & 0x1000))
		VAProjectAddFileMRU(filename, mruFileFocus);

	// check if opening a file w/o a project
	GlobalProject->ClearIfNoInit();

	// [case: 144352] check if file is in UE plugins folders and notify user if indexing of those folders are not turned
	// on
	static bool showUEIndexPluginsMessage = true; // used to show this message only once per VS studio run session
	if (Psettings && Psettings->mUnrealEngineCppSupport && Psettings->mIndexPlugins < 4)
	{
		if (showUEIndexPluginsMessage && IsUEIgnorePluginsDir(filename))
		{
			CStringW truncatedFilename; // get only file name to show in MsgBox
			int pos = filename.ReverseFind(L'\\');
			if (pos == -1)
				truncatedFilename = filename;
			else
				truncatedFilename = filename.Mid(pos + 1);

			CStringW msgIndexPlugin(L"The file \"");
			msgIndexPlugin.Append(truncatedFilename);
			msgIndexPlugin.Append(L"\" is located in an Unreal Engine plugin folder. Visual Assist is currently "
			                      L"configured not to parse this plugin folder and "
			                      "so Visual Assist will not provide the functionality you may expect for this file "
			                      "and others in the same location."
			                      "\n\n"
			                      "To solve this, change the \"Index Unreal Engine plugins\" option in Visual Assist "
			                      "Options > Unreal Engine."
			                      "\n\n"
			                      "(This message will not be shown again until you restart Visual C++.)");

			showUEIndexPluginsMessage = false;
			WtMessageBox(msgIndexPlugin, CStringW(IDS_APPNAME), MB_OK | MB_ICONWARNING);
		}
	}

	// [case: 149171] check if file is set to be excluded and notify user that VA will not parse this file
	if (GlobalProject && !GlobalProject->GetFileExclusionRegexes().empty())
	{
		for (const auto& regex : GlobalProject->GetFileExclusionRegexes())
		{
			if (std::regex_match(std::wstring(filename), regex))
			{
				CStringW truncatedFilename; // get only file name to show in MsgBox
				int pos = filename.ReverseFind(L'\\');
				if (pos == -1)
					truncatedFilename = filename;
				else
					truncatedFilename = filename.Mid(pos + 1);

				CStringW msgExcludedFile(L"The file \"");
				msgExcludedFile.Append(truncatedFilename);
				msgExcludedFile.Append(L"\" is excluded by a rule defined in the \".vscode\\settings.json \" file, so Visual "
									   L"Assist will not provide the functionality you may expect for this file."
				                       L"\n\n"
				                       L"To solve this, either change the \"Do not parse files excluded by .vscode\\settings.json\" option in "
									   L"Visual Assist Options > Performance or modify the \"files.exclude\" entry in the \"settings.json\" file.");

				WtMessageBox(msgExcludedFile, CStringW(IDS_APPNAME), MB_OK | MB_ICONWARNING);

				break;
			}
		}
	}

	if (!ShouldFileBeAttachedTo(filename))
		return nullptr;

	const int fType = GetFileType(filename);
	// [case: 27947] do not use IsFeatureSupported here since it will return
	// false if VA is temporarily disabled.  Minihelp will never appear for
	// this window even once VA is enabled.
#if !defined(RAD_STUDIO)
	BOOL doMiniHelp = CAN_USE_NEW_SCOPE(fType);
#endif
	switch (fType)
	{
	case UC:
		if (!Psettings || !Psettings->mUnrealScriptSupport)
			return nullptr;
		break;
	case Src:
	case Header:
	case CS:
	case JS:
	case VB:
	case VBS:
	case ASP:
	case XAML:
	case XML:
	case HTML:
	case PHP:
	case PERL:
	case Java:
	case RC:
	case Idl:
	case Other:
		break;
	default:
		if (!::IsAnySysDicLoaded())
			return nullptr;
		if (!Psettings->m_enableVA)
			return nullptr;
	}

	if (IsCFile(fType))
		SetupVcEnvironment(false);

	int id = GetDlgCtrlID(hMSDevEdit);

	// ReSharper hack, they change the window id when they subclass it.
	WTString cls = GetWindowClassString(hMSDevEdit);
#if defined(RAD_STUDIO)
	if (cls != "TEditControl")
	{
		_ASSERTE(!"VaAddinClient::AttachEditControl: RadStudio TEditControl not passed in");
		return nullptr; // Only bind to TEditControl's or bail
	}
#else
	if (cls == "VsTextEditPane")
	{
		id = 0xe900;
	}
	else if (doMiniHelp && cls.Find("WindowsForms") == 0)
	{
		// [case: 2736] don't display minihelp with Team Foundation Annotate Power Toy
		HWND wnd = hMSDevEdit;
		for (int idx = 0; idx < 5 && wnd; ++idx)
		{
			wnd = GetParent(wnd);
			if (wnd)
				cls = GetWindowClassString(wnd);
			if (cls.Find("WindowsForms") == 0)
				continue;
			if (cls == "GenericPane")
			{
				CWnd* w = CWnd::FromHandle(wnd);
				if (w)
				{
					CString title;
					w->GetWindowText(title);
					if (title.Find(" (Annotated)") != -1)
						doMiniHelp = false;
				}
			}
			break;
		}
	}
#endif
	if (gShellAttr->IsDevenv10OrHigher())
	{
		g_FontSettings->SetVS10FontSettings();
		id = 0xe900; // force minihelp in this view
		if (::GetParent(hMSDevEdit) == NULL)
			return nullptr; // Happens while ctrl tabbing between source and design c#files
	}
	CWnd w;
	if (w.FromHandlePermanent(hMSDevEdit))
	{
#if defined(RAD_STUDIO)
		return nullptr; // Window recycling.
#else
#ifdef _DEBUG
		// We get here if another tool unsubclasses a window that VA has subclassed.
		// We need to re-subclass the window to get VA back in the message loop.
		// Happens with Intel Parallel Studio 2010 Beta. [case=46226]
		EdCnt* pEd = (EdCnt*)w.FromHandlePermanent(hMSDevEdit);
		if (pEd && !SendMessage(hMSDevEdit, WM_VA_GET_EDCNTRL, 0, 0))
		{
			_ASSERTE(!"is this workaround code that should be in release builds?");
			LOG("Re-subclassing EdCnt.");
			pEd->UnsubclassWindow();
			pEd->SubclassWindow(hMSDevEdit);
			_ASSERTE(SendMessage(hMSDevEdit, WM_VA_GET_EDCNTRL, 0, 0));
			// Reactivate to set g_currentEdCnt
			::SetFocus(gMainWnd->GetSafeHwnd());
			::SetFocus(hMSDevEdit);
			return nullptr; // Return Null so it free's duplicate pDoc
		}
#endif // _DEBUG
		VALOGERROR("Error: EdCnt window already subclassed.");
		return nullptr; // oops already subclassed???
#endif
	}

#if !defined(RAD_STUDIO)
	if (doMiniHelp /*&& (!pDoc || gShellAttr->IsDevenv())*/ && (id == 0xe900 || id == 0))
	{
		HWND hMDIChild = ::GetParent(hMSDevEdit);
		CWnd w2;
		if (w2.FromHandlePermanent(GetParent(hMDIChild)) == NULL)
		{
			EditParentWnd* newParent = new EditParentWnd(filename);
			newParent->Init(GetParent(hMDIChild), hMDIChild, hMSDevEdit, NULL, NULL);
			CatLog("Editor.Events", "MiniHelp");
		}
	}
#endif

	EdCntPtr ed;
#if defined(RAD_STUDIO)
	ed.reset(new EdCntCWnd());
#else
	if (gShellAttr->IsDevenv10OrHigher())
		ed.reset(new EdCntWPF());
	else if (gShellAttr->IsDevenv())
		ed.reset(new EdCntCWndVs());
	else
		ed.reset(new EdCntCWnd());
#endif

	if (ed)
	{
		ed->Init(ed, hMSDevEdit, pDoc, filename);
		if (g_IdeSettings && IsCFile(fType))
		{
			static bool force = true;
			g_IdeSettings->CheckInit(force);
			force = false;
		}

		if (gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 99761]
			// delay repos until CodeWindowMargin sets up va nav bar
			ed->SetTimer(IDT_DelayInit, 205, nullptr);
		}
	}

	if (ed->m_hWnd)
		CatLog("Editor", "EditControlWnd");
	else
		LogUnfiltered("NoEditcontrolWnd"); // error?

	return (HWND)1;
#endif // !VAX_CODEGRAPH
}

#define ID_MY_UNDO 0xb7f4
#define ID_MY_REDO 0xb7f5

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(EdCnt, CTer)
ON_COMMAND(DSM_GOTONEXTBKMK, CmGotoNextBookmark)
ON_COMMAND(DSM_GOTOPREVBKMK, CmGotoPrevBookmark)
ON_COMMAND(WM_HELP, CmHelp)
ON_COMMAND(CM_EDITEXPAND, CmEditExpandCmd)
ON_COMMAND(WM_VA_PASTE, CmEditPasteMenu)
ON_COMMAND(WM_VA_CODETEMPLATEMENU, CodeTemplateMenu)
ON_COMMAND(WM_VA_CONTEXTMENU, ContextMenu)
ON_COMMAND(WM_VA_DEFINITIONLIST, DisplayDefinitionList)
ON_COMMAND(WM_VA_OPENOPPOSITEFILE, OpenOppositeFile)
ON_COMMAND(ID_FILE_CLOSE, OnFileClose)
ON_COMMAND(WM_CLOSE, OnClose)
ON_MESSAGE(WM_VA_FLASHMARGIN, FlashMargin)
ON_WM_CONTEXTMENU()
ON_WM_SETFOCUS() // called manually in DefWindowProc
ON_WM_KILLFOCUS()
ON_WM_MOUSEMOVE()
ON_WM_CLOSE()
ON_WM_CHAR()
ON_WM_PAINT()
ON_WM_KEYDOWN()
ON_WM_SYSKEYDOWN()
ON_WM_TIMER()
ON_WM_KEYUP()
ON_WM_SYSKEYUP()
ON_WM_LBUTTONDOWN()
ON_WM_LBUTTONUP()
ON_WM_LBUTTONDBLCLK()
ON_WM_RBUTTONDOWN()
ON_WM_RBUTTONUP()
ON_WM_MBUTTONDOWN()
ON_WM_MBUTTONUP()
ON_WM_CTLCOLOR()
ON_WM_MEASUREITEM()
ON_REGISTERED_MESSAGE(wm_postponed_set_focus, OnPostponedSetFocus)
END_MESSAGE_MAP()
#pragma warning(pop)

void EdCnt::CmUnMarkAll()
{
	DEFTIMERNOTE(CmUnMarkAllTimer, NULL);
	CmClearAllBookmarks();
}

void EdCnt::CmEditExpand(int popType /*= 1*/)
{
	if (!gTestsActive)
	{
		ASSERT_ONCE(popType != ET_EXPAND_TAB);
	}

	if (Is_Some_Other_File(m_ftype))
	{
		const WTString selStr(GetSelString());
		const WTString bb(GetBuf());
		if (selStr.IsEmpty() && StartsWith(bb.c_str() + GetBufIndex(bb, (long)CurPos()), VA_AUTOTES_STR, FALSE))
		{
			RunVAAutomationTest();
			return;
		}
	}

	WrapperCheckDecoy chk(g_pUsage->mJunk);
	g_CompletionSet->DoCompletion(mThis, popType, false);
	return;
}

#if !defined(RAD_STUDIO)
class CheckExternalFiles : public ParseWorkItem
{
	const CStringW mFilename;
	const int mFiletype;
	DWORD mStartTicks;
	uint mSolutionHash;
	mutable bool mAbort;

  public:
	CheckExternalFiles(const CStringW& filename, int filetype)
	    : ParseWorkItem("CheckExternalFiles"), mFilename(filename), mFiletype(filetype), mStartTicks(::GetTickCount()),
	      mSolutionHash(GlobalProject->GetSolutionHash()), mAbort(false)
	{
	}

	virtual void DoParseWork()
	{
		if (mAbort)
			return;

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(VA_CPPUNIT)
#if defined(NDEBUG)
		static bool once = true;
		if (once)
		{
			static int sHitCount = 0;
			if (++sHitCount > 10 && (GetTickCount() % sHitCount) < 5)
			{
				once = false;
				CStringW user(gVaLicensingHost ? gVaLicensingHost->GetLicenseInfoUser() : L"");
				// kExpirationLabel_Perpetual
				int p = user.Find(L"ds 2");
				if (p != -1)
				{
					CString expYearStr = (const wchar_t*)user.Mid(p + 3, 4);
					int expYear = atoi(expYearStr);
					if (Psettings && expYear && expYear > (VA_VER_SIMPLIFIED_YEAR + 10))
					{
						// [case: 93323]
						// cracked with bogus license text
						Psettings->m_validLicense = Psettings->m_enableVA = FALSE;
#ifndef NOSMARTFLOW
						SmartFlowPhoneHome::FireInvalidLicenseTerminationDate();
#endif
						return;
					}
				}
				else
				{
					// kExpirationLabel_NonPerpetual
					int p2 = user.Find(L"hru 2");
					if (p2 != -1)
					{
						CString expYearStr = (const wchar_t*)user.Mid(p2 + 4, 4);
						int expYear = atoi(expYearStr);
						if (Psettings && expYear && expYear > (VA_VER_SIMPLIFIED_YEAR + 10))
						{
							// cracked with bogus license text
							Psettings->m_validLicense = Psettings->m_enableVA = FALSE;
#ifndef NOSMARTFLOW
							SmartFlowPhoneHome::FireInvalidLicenseTerminationDate();
#endif
							return;
						}
					}
				}

				WCHAR path[MAX_PATH + 1];
				HMODULE hm = nullptr;

				if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				                       L"GetVaService", &hm))
				{
					if (GetModuleFileNameW(hm, path, MAX_PATH))
					{
						CStringW dll(path);
						dll = Basename(dll);
						if (dll.CompareNoCase(IDS_VAX_DLLW))
						{
							// Psettings->m_validLicense = Psettings->m_enableVA = FALSE;
#ifndef NOSMARTFLOW
							SmartFlowPhoneHome::FireRenamedVaxDll();
#endif
						}
					}
				}

#ifndef NOSMARTFLOW
				if (gVaLicensingHost)
				{
					const int license = gVaLicensingHost->GetLicenseStatus();
					if (LicenseStatus_Valid == license)
					{
						const CString tmp = gVaLicensingHost->GetLicenseExpirationDate();
						if (tmp.GetLength() != 10 || tmp[0] != '2' || tmp[1] != '0')
							SmartFlowPhoneHome::FireInvalidLicenseTerminationDate();
					}
				}
				else
					SmartFlowPhoneHome::FireMissingHost();
#endif
			}
		}
#endif
#endif

		_ASSERTE(!GlobalProject->IsBusy());
		if (!mSolutionHash)
			mSolutionHash = GlobalProject->GetSolutionHash(); // file opened with solution?
		else if (mSolutionHash != GlobalProject->GetSolutionHash())
			return; // another solution loaded while we were waiting

		bool doGotoChk = true;
		if ((Psettings->mParseFilesInDirectoryIfEmptySolution && GlobalProject->IsSolutionInitiallyEmpty()) ||
		    (Psettings->mParseFilesInDirectoryIfExternalFile && !GlobalProject->IsSolutionInitiallyEmpty()))
		{
			// [case: 18918] support for directory-based parsing
			if (IncludeDirs::IsSystemFile(mFilename))
				return;

			CStringW tmpDir(::GetTempDir());
			tmpDir.MakeLower();
			CStringW fPath(::Path(mFilename) + L"\\");
			fPath.MakeLower();
			if (-1 != fPath.Find(tmpDir))
			{
				// [case: 62854]
				return;
			}

			// this condition might be a problem for partial solutions.  ex:
			// open a file already in project, other unloaded files in same dir won't be loaded.
			// could be good, could be bad - it's environment dependent
			// a complete solution might have orphaned files that you don't want loaded
			if (!GlobalProject->Contains(mFilename))
			{
				doGotoChk = false;
				CStringW dir = ::Path(mFilename);
				FileList files;
				::FindFiles(dir, L"*.*", files);
				bool checkForUpdates = false;
				for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
				{
					if (StopIt)
						return;

					const CStringW file = (*it).mFilename;
					const int type = ::GetFileType(file);
					if (::CAN_USE_NEW_SCOPE(type))
					{
						if (!GlobalProject->Contains(file))
						{
							// add to project but do not force a parse
							GlobalProject->AddExternalFile(file);
							checkForUpdates = true;
						}
						else if (!checkForUpdates && !g_pGlobDic->GetFileData(file))
						{
							// unparsed project file due to fast project open
							checkForUpdates = true;
						}
					}
					else if (SQL == type)
					{
						// [case: 11425] add to project but do not force a parse
						if (!GlobalProject->Contains(file))
							GlobalProject->AddExternalFile(file);
					}
				}

				if (checkForUpdates && GlobalProject->GetSolutionHash() == mSolutionHash)
					GlobalProject->CollectAndUpdateModifiedFiles();
			}
		}

		if (doGotoChk && GlobalProject->GetSolutionHash() == mSolutionHash)
		{
			// cache project info for opened file on this thread so that
			// it doesn't block UI thread during refactor QueryStatus
			GlobalProject->GetProjectForFile(mFilename);

			if (Header != mFiletype)
				return;

			// case:2943 - check for alt src file for goto def
			if (IncludeDirs::IsSystemFile(mFilename))
				return; // assume these are covered by sys src dirs

			CStringW altFile(mFilename);
			if (!::SwapExtension(altFile))
				return; // no match found

			if (GlobalProject->Contains(altFile) && g_pGlobDic->GetFileData(altFile))
				return; // not necessary if src file is in solution

			if (g_ParserThread)
			{
				EdCntPtr ed(g_currentEdCnt);
				if (!ed || ed->FileName().CompareNoCase(altFile))
					g_ParserThread->QueueFile(altFile);
			}
		}
	}

	virtual bool CanRunNow() const
	{
		if (!GlobalProject)
			return false;

		const DWORD now = ::GetTickCount();
#if defined(RAD_STUDIO)
		constexpr auto kDelay = 4000;
#else
		constexpr auto kDelay = 1000;
#endif
		if (::abs((int)(now - mStartTicks)) < kDelay)
			return false;

		const uint prjHash = GlobalProject->GetSolutionHash();
		if (mSolutionHash != prjHash)
		{
			if (::abs((int)(now - mStartTicks)) < kDelay)
			{
				// When VS loads solution with open files, we get edit control
				// attach before solution is loaded.
				// give chance for us to start solution load.
				return false;
			}

			if (!mSolutionHash)
			{
				// file opened before solution loaded by va
				//				_asm nop;
			}
			else if (!prjHash)
			{
				// solution unloaded; abort this call
				mAbort = true;
				return true;
			}
			else
			{
				// different non-zero hashes
				if (GlobalProject->IsBusy() || !GlobalProject->IsOkToIterateFiles())
				{
					// don't abort yet on chance that hash will change during load
					return false;
				}

				mAbort = true;
				return true;
			}
		}

		return !GlobalProject->IsBusy() && GlobalProject->IsOkToIterateFiles();
	}

	virtual bool ShouldRunAtShutdown() const
	{
		return false;
	}
};
#endif

bool EdCnt::SetNewMparse(MultiParsePtr mp, BOOL setFlag)
{
	{
		AutoLockCs l(mDataLock);
		mPendingMp = mp;
	}

	if (!m_hWnd)
		return false;

	if (WM_VA_SET_DB == SendMessage(WM_VA_SET_DB, 0, setFlag))
		return true;

	return false;
}

EdCnt::EdCnt()
{
#ifndef VA_CPPUNIT
#if !defined(RAD_STUDIO)
	s_GetMsgHooks.Attach();
#endif
#endif
}

extern const WTString kExtraBlankLines("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n");

void EdCnt::Init(EdCntPtr pThis, HWND hMSDevEdit, void* pDoc, const CStringW& file)
{
	mThis = pThis;
	{
		AutoLockCs l(g_EdCntListLock);
		g_EdCntList.push_back(mThis);
		g_currentEdCnt = mThis;
	}

	vCatLog("Editor", "Ed::Init %p %p", mThis.get(), hMSDevEdit);
	RestoreCaretPosition = false;
	mruFlag = FALSE;
	m_preventGetBuf = FALSE;
	m_FileIsQuedForFullReparse = FALSE;
	m_FileHasTimerForFullReparseASAP = FALSE;
	s_LastReparseFile.Empty();
	m_reparseRange.x = m_reparseRange.y = 0;
	m_contextMenuShowing = FALSE;
	PatchW32Functions(true);
	m_ftype = ::GetFileType(file, false, true);
	m_hasRenameSug = FALSE;
	m_vaOutlineTreeState = NULL;

	gTypingDevLang = m_ScopeLangType = m_ftype;
	gAutotextMgr->Load(m_ScopeLangType);
	if (g_FontSettings)
		g_FontSettings->DualMonitorCheck();
	m_ITextDoc = NULL;
	m_VSNetDoc = NULL;
	if (gShellAttr->IsDevenv())
	{
		m_VSNetDoc = pDoc;
		pDoc = NULL;
	}
	else
		m_ITextDoc = (ITextDocument*)pDoc;
	DEFTIMERNOTE(EdCntTimer, CString(file));
	m_minihelpHeight = (int)Psettings->m_minihelpHeight;
	m_doFixCase = FALSE;
	m_lborder = nullptr;
	m_undoOnBkSpacePos = NPOS;
	m_ttParamInfo = m_ttTypeInfo = NULL;
	Psettings->m_incrementalSearch = false;
	m_tootipDef = 0;
	m_FileHasTimerForFullReparse = false;
	m_lastScopePos = 0;
	m_paintCommentLine = 0;
	m_hpos = 0;
	m_typing = false;
	m_ignoreScroll = false; // repaint onscroll
	mModifClickArmedPt.SetPoint(0, 0);
	mModifClickKey = 0;
	mLastDotToArrowPos = UINT_MAX;
	mMiddleClickDoCmd = false;
	mShiftRClickDoCmd = false;
	mUeRestoreSmartIndent = false;
	mUeRestoreAutoFormatOnPaste2 = false;

	// setup java or cpp dictionaries
	switch (m_ftype)
	{
	case PERL:
	case Idl:
	case Other:
	case JS:
	case PHP:
		m_txtFile = TRUE;
		break;
	case VB:
	case VBS:
	case CS:
	case Java:
	default:
		if (Is_Tag_Based(m_ftype))
			m_txtFile = TRUE;
		else
			m_txtFile = FALSE;
	}
	// m_pDoc = pDoc ? new DocClass(pDoc) : NULL;
	m_pDoc = NULL;
	CTer::m_pDoc = pDoc ? new DocClass((ITextDocument*)pDoc) : NULL;
	m_stop = false;
	m_autoMatchChar = '\0';

	WrapperCheckDecoy chk(g_pUsage->mFilesOpened);
	CStringW fname;
	{
		AutoLockCs l(mDataLock);
		fname = filename = m_pDoc ? m_pDoc->File() : file;
		// filename.MakeLower();	// this breaks code templates that use the filename
		m_pmparse = MultiParse::Create(m_ftype);
		m_pmparse->SetCacheable(TRUE);
		m_pmparse->m_showRed = false;
	}
	mMifMarkersCookie = mMifSettingsCookie = 0;
	m_modCookie = (int)WTHashKeyW(fname);
	m_ReparseScreen = false;

	// will get deleted by openfile, but it needs to be valid in case we
	// KillFocus before it is created.
	m_LnAttr = std::make_unique<AttrLst>();

	undoLock = false;
	// set this as the saved state

	mInitialParseCompleted = false;
	modified_ever = false;
	modified = 0;
	SubclassWindow(hMSDevEdit);
#if defined(RAD_STUDIO)
	m_lborder = nullptr;
#else
	m_hParentMDIWnd = ::GetParent(::GetParent(hMSDevEdit));

	::UpdateWindow(m_hParentMDIWnd);
	CWnd::UpdateWindow();
	CRect r;
	vGetClientRect(&r);
	r.left = 0;
	// make top lower than it will be so that nc area
	//  gets updated when window is moved after init

	int miniHelpHeight = (int)Psettings->m_minihelpHeight;
	VsUI::CDpiAwareness::LogicalToDeviceUnitsY(m_hParentMDIWnd, &miniHelpHeight);
	r.top = miniHelpHeight + 1;

	// MoveWindow(&r, TRUE);

	if (gShellAttr->IsDevenv())
		SetFocusParentFrame(); // case: 22204 case: 23540
	else
		vSetFocus();

	r.SetRect(0, 0, 10, 10);
	m_lborder = nullptr;
#if !defined(TI_BUILD) && !defined(RAD_STUDIO)
	m_lborder = std::make_unique<BorderCWnd>(mThis);
	m_lborder->CreateEx(0, nullptr, "", WS_CHILD | WS_VISIBLE | WS_GROUP, r.left, r.top, r.right - r.left,
	                    r.bottom - r.top, m_hParentMDIWnd, (HMENU)1);
#endif // TI_BUILD
	/*
	m_lborder->EnableWindow(FALSE);
	*/
#endif
	LastLn = CurLine();
	if (Psettings->m_keepBookmarks && m_pDoc)
	{
		if (g_VATabTree)
		{
			TerNoScroll noscroll(this);
			g_VATabTree->SetFileBookmarks(FileName(), m_LnAttr.get());
			int line = -1, lstln = -1;
			long p1, p2;
			GetSel(p1, p2);
			while (m_LnAttr->NextBookmarkFromLine(line) && line > lstln)
			{
				CTer::m_pDoc->GoTo(line + 1);
				CTer::m_pDoc->SetBookMark();
				lstln = line;
			}
			SetSel(p1, p2);
			// perform GetBuf logic w/in the same TerNoScroll to save time and flicker
			// nabbed from GetBuf();
			long r2, c2;
			m_pDoc->CurPos(r2, c2);
			// must be same as CTer::GetBuf()
			UpdateBuf(m_pDoc->GetText() + kExtraBlankLines, false);
			m_pDoc->GoTo(r2, c2);
			SetBufState(BUF_STATE_CLEAN);
		}
		// invalidate lmargin so new bkmk's show
		CRect r3;
		vGetClientRect(&r3);
		r3.right = (long)Psettings->m_borderWidth;
		InvalidateRect(&r3);
	}
	else
	{
		m_LnAttr->ClearAllBookmarks(this);
		if (gShellAttr->IsDevenv())
			SetTimer(ID_GETBUFFER, 100, NULL);
		else
			GetBuf();
	}

	if (file && *file)
	{
		{
			AutoLockCs l(mDataLock);
			fname = filename = file;
		}
		if (Psettings->m_autoBackup && IsFile(GetTempFile(fname)))
		{
			BOOL isFileOpenInOtherPane = FALSE;
			// see if file is open in other pane
			{
				AutoLockCs l(g_EdCntListLock);
				for (EdCntPtr ed : g_EdCntList)
				{
					if (ed && ed.get() != this && fname == ed->FileName())
						isFileOpenInOtherPane = TRUE;
				}
			}

			extern bool IsOnlyInstance();
			if (!isFileOpenInOtherPane && IsOnlyInstance() && !gTestsActive)
			{
				WTString tfile = ReadFile(GetTempFile(fname));
				WTString dfile = ReadFile(fname);
				WTString msg;
				msg.WTFormat("Visual Assist has detected %s was not properly saved.\n\nWould you like to load the "
				             "unsaved version?",
				             WTString(Basename(fname)).c_str());
				RemovePadding_kExtraBlankLines(tfile);
				const WTString bb(GetBufConst());
				if (tfile != dfile && bb != tfile && ErrorBox(msg.c_str(), MB_YESNO) == IDYES)
				{
					const CStringW wbakName(::GetTempFile(fname, TRUE));
					CStringW bakContents;
					if (::IsFile(wbakName) && ::ReadFileUtf16(wbakName, bakContents))
					{
						// [case: 58071] unicode restore
						gShellSvc->SelectAll(this);
						ReplaceSelW(bakContents, noFormat);
						GetBuf(TRUE);
					}
					else
					{
						// fallback to ascii/mbcs
						SendMessage(WM_COMMAND, ID_EDIT_SELECT_ALL);
						// thread safe insert...
						SendMessage(EM_REPLACESEL, 0xee, (LPARAM)(LPCSTR)tfile.c_str());
					}
				}
				RemoveTempFile(fname);
			}
		}
		CheckForDeletedFile(fname);

#if defined(RAD_STUDIO)
		_ASSERTE(!Psettings->mParseFilesInDirectoryIfExternalFile && !Psettings->mParseFilesInDirectoryIfEmptySolution);
#else
		// case:18918 directory-based parsing
		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new CheckExternalFiles(fname, m_ftype));
#endif
	}

	CheckForSaveAsOrRecycling();

	CheckForInitialGotoParse();

#if !defined(_DEBUG) && !defined(RAD_STUDIO) && !defined(AVR_STUDIO)
	static int sInitCnt = 0;
	if (!(++sInitCnt % 50) && Psettings && Psettings->m_validLicense)
	{
		// since the normal check for latest version only occurs at IDE launch and users can
		// conceivably run without VS restart for a week or more, also check every XX file loads.
		::CheckForLatestVersion(FALSE);
	}
#endif // !_DEBUG

	NavAdd(fname, CurPos());
}

void EdCnt::CheckForInitialGotoParse()
{
	if (Psettings->m_FastProjectOpen && Src == m_ftype && GlobalProject && g_pGlobDic && g_ParserThread)
	{
		// [case: 30226] if we don't parse all project files at load, then make
		// sure this file gets a "goto" parse.
		const CStringW fname(FileName());
		if (GlobalProject->IsBusy() || // check IsBusy since we get EdCnt::Init before project is completely loaded
		    (GlobalProject->Contains(fname) && !g_pGlobDic->GetFileData(fname)))
		{
			QueForReparse();
			modified_ever = false;
		}
	}
}


uint EdCnt::CurPos(int end) const
{
	// LOG("CurPos");
	ulong p1, p2;
	GetSel(p1, p2);
	return (end ? p2 : p1);
}
// Slow CurPos calculates the beginning pos from the current selection
/*uint EdCnt::CurPosBegSel() {
// LOG("CurPos");
WTString s = GetSelString();
long p2 = CurPos(TRUE);
long idx = GetBufIndex(CurPos(TRUE));
// TODO fix this...
if(strncmp(&m_buf.c_str()[idx], s, s.GetLength()) == 0)
idx += s.GetLength();
else
idx -= s.GetLength();
long l, c;
p2 = PosToLC(m_buf, idx, l, c);
return(p2);
}*/

void EdCnt::CmEditCopy()
{
	DEFTIMERNOTE(CmEditCopyTimer, NULL);
	LOG("Copy");
	if (!(g_VATabTree && Psettings->m_clipboardCnt))
		return;

	CStringW curSelTxt = m_pDoc ? m_pDoc->GetCurSelection().c_str() : GetSelStringW();
	if (curSelTxt.GetLength())
	{
		g_VATabTree->AddCopy(curSelTxt);
	}
	else
	{
		// copy without selection must be enabled - copy current line
		TerNoScroll noscroll(this);
		const long p1 = (long)CurPos();
		const int l1 = LineFromChar(p1);
		const int l2 = LineFromChar(p1) + 1;
		SetSel(LineIndex(l1), LineIndex(l2));
		curSelTxt = m_pDoc ? m_pDoc->GetCurSelection().c_str() : GetSelStringW();
		g_VATabTree->AddCopy(curSelTxt);
		SetPos((uint)p1);
	}
}

void EdCnt::DisplayDefinitionList()
{
	if (!Psettings->m_enableVA)
		return;
	WrapperCheckDecoy chk(g_pUsage->mJunk);
	DEFTIMERNOTE(DisplayDefinitionListTimer, NULL);
	if (gShellAttr->IsDevenv10OrHigher())
		SetFocusParentFrame(); // case=45591
	NavAdd(FileName(), CurPos());
	KillTimer(ID_CURSCOPEWORD_TIMER);
	extern void GotoMethodList();
	GotoMethodList();
}

void EdCnt::ContextMenu()
{
	DEFTIMERNOTE(ContextMenuTimer, NULL);
	if (!m_lborder)
		return;

	// display contextmenu
	CPoint pt = vGetCaretPos();
	pt.y += g_FontSettings->GetCharHeight() * 2;
	m_lborder->DisplayMenu(pt);
}

void EdCnt::CloseHandler()
{
	DEFTIMERNOTE(CloseHandlerTimer, NULL);
	LOG("CloseHandler");

#if defined(RAD_STUDIO)
	if (gVaRadStudioPlugin)
		gVaRadStudioPlugin->EditorHwndClosing(m_hWnd);
#endif

	VASmartSelect::ActiveSettings().OnEdCntDestroy(this);

	m_stop = true; // case waiting for DbLoad
	if (gShellAttr->IsDevenv10OrHigher())
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed && ed.get() == this)
		{
			try
			{
				// extract of OnKillFocus
				NavAdd(FileName(), (uint)m_LastPos2);

				if (ed->GetSafeHwnd() && ::IsWindow(ed->GetSafeHwnd()))
				{
					if (m_FileHasTimerForFullReparse)
						OnModified(TRUE);
					ClearAllPopups();
				}
			}
			catch (...)
			{
			}
		}
	}

	g_ScreenAttrs.Invalidate();

	if (m_lborder)
	{
		m_lborder->DestroyWindow();
		m_lborder = nullptr;
	}

	if (m_ttParamInfo)
	{
		ArgToolTip* tmp = m_ttParamInfo;
		m_ttParamInfo = nullptr;
		delete tmp;
	}

	if (m_ttTypeInfo)
	{
		ArgToolTip* tmp = m_ttTypeInfo;
		m_ttTypeInfo = nullptr;
		delete tmp;
	}

	CTer::DestroyWindow();
	ReleaseSelfReferences();
}

void EdCnt::ReleaseSelfReferences()
{
	if (!mThis)
		return;

	// prevent any odd recursion by clearing mThis immediately
	EdCntPtr _this = mThis;
	mThis = nullptr;

	if (g_currentEdCnt == _this)
		g_currentEdCnt = nullptr;

	if (g_CompletionSet && g_CompletionSet->m_ed && g_CompletionSet->m_ed == _this)
		g_CompletionSet->m_ed.reset();

#if !defined(RAD_STUDIO)
	s_GetMsgHooks.Detach();
#endif

	{
		AutoLockCs l(g_EdCntListLock);
		for (auto it = g_EdCntList.begin(); it != g_EdCntList.end(); ++it)
		{
			auto ed = *it;
			if (_this == ed)
			{
				vLog("Ed::Rel %p %p", _this.get(), m_hWnd);
				g_EdCntList.erase(it);
				break;
			}
		}
	}

	if (gVaService && gVaService->GetOutlineFrame())
		gVaService->GetOutlineFrame()->OnEdCntClosing(_this);

	// let other windows see changes w/o having to save
	const CStringW fname(FileName());
	if (Modified() || m_FileHasTimerForFullReparse || modified_ever)
	{
		BOOL isFileOpenInOtherPane = FALSE;
#if defined(RAD_STUDIO)
		if (gVaRadStudioPlugin)
			isFileOpenInOtherPane = !gVaRadStudioPlugin->GetRunningDocText(fname).IsEmpty();
#endif
		// see if file is open in other pane
		{
			AutoLockCs l(g_EdCntListLock);
			for (EdCntPtr ed : g_EdCntList)
			{
				if (ed && fname == ed->FileName())
				{
					isFileOpenInOtherPane = TRUE;
					break;
				}
			}
		}

		if (!isFileOpenInOtherPane)
		{
			// this block is semi-duplicated in the RadStudio plugin code #EdCntReleaseSelfRefDupe
			// force a reparse
			if (StopIt || Modified())               // [case: 52158] close of modified file without save
				InvalidateFileDateThread(fname); //  To cause a reparse next time the IDE is opened
			// don't do this at shutdown
			if (gVaService && !StopIt)
			{
				DTypePtr fData = MultiParse::GetFileData(MSPath(fname));
				if (!fData || !fData->IsSysLib()) // don't reparse system files
					g_ParserThread->QueueFile(fname);
			}
		}
	}

	if (Psettings->m_autoBackup)
	{
		// file closed properly, remove temp file
		BOOL isStillOpen = FALSE;
		{
			AutoLockCs l(g_EdCntListLock);
			for (EdCntPtr ed : g_EdCntList)
			{
				if (ed && fname == ed->FileName())
				{
					isStillOpen = TRUE;
					break;
				}
			}
		}

		if (!isStillOpen)
			::RemoveTempFile(fname);
	}

	if (gShellAttr->IsDevenv() && m_VSNetDoc)
	{
		// release doc dte ptrs, but do not delete the container
		// it needs to be valid until dtor of this.
		SendVamMessage(VAM_ReleaseDocDte, (WPARAM)m_VSNetDoc, NULL);
	}
}

void EdCnt::UeEnableUMacroIndentFixIfRelevant(bool checkPrevLine)
{
	if (IsCFile(m_ScopeLangType))
	{
		const int lineNumber = checkPrevLine ? CurLine() - 1 : CurLine();
		if (lineNumber > 0)
		{
			const WTString lineText = GetLine(lineNumber);
			if (lineText.EndsWith(')'))
			{
				if (lineText.Find("UPROPERTY(") != -1 || lineText.Find("UFUNCTION(") != -1 ||
				    lineText.Find("UDELEGATE(") != -1 || lineText.Find("GENERATED_BODY(") != -1 ||
				    lineText.Find("GENERATED_UCLASS_BODY(") != -1 || lineText.Find("GENERATED_USTRUCT_BODY(") != -1 ||
				    lineText.Find("GENERATED_UINTERFACE_BODY(") != -1 ||
				    lineText.Find("GENERATED_IINTERFACE_BODY(") != -1 || lineText.Find("RIGVM_METHOD(") != -1)
				{
					if (g_IdeSettings && g_IdeSettings->GetEditorIntOption("C/C++", "IndentStyle") == 2)
					{
						// [case: 109205]
						// switch from smart indent to block indent to prevent unnecessary extra indention after a U*
						// macro
						g_IdeSettings->SetEditorOption("C/C++", "IndentStyle", "1");
						mUeRestoreSmartIndent = true;
					}
				}
			}
		}
	}
}

void EdCnt::UeDisableUMacroIndentFixIfEnabled()
{
	if (mUeRestoreSmartIndent && g_IdeSettings)
	{
		// [case: 109205]
		g_IdeSettings->SetEditorOption("C/C++", "IndentStyle", "2");
		mUeRestoreSmartIndent = false;

		if(!gShellAttr || !gShellAttr->IsDevenv17u7OrHigher())
			::SetStatus("Unreal Engine whitespace corrected");
	}
}

void EdCnt::OldLButtonDown(uint modKeys, CPoint point)
{
	int oldModCookie = m_modCookie;
	vCatLog("Editor.Events", "VaEventUE OnLButtonDown  key=0x%x", modKeys);
	WrapperCheckDecoy chk(g_pUsage->mJunk);
	Psettings->m_incrementalSearch = false;
	m_ignoreScroll = false; // repaint onscroll
	DEFTIMERNOTE(OnLButtonDownTimer, NULL);
	LOG("OnLButtonDown");
	LogElapsedTime let("Ed::LBD", 50);

	ulong preP1, preP2;
	GetSel(preP1, preP2);
	ShowCaret();
	bool hasSel = preP1 == preP2 ? false : true;
	m_typing = false;
	KillTimer(ID_TIMER_MOUSEMOVE);
	UpdateWindow(); // fix paint problem when selecting text from window in background

	g_CompletionSet->Dismiss();
	DisplayToolTipArgs(false);

	if (m_ReparseScreen && !hasSel)
		Reparse();

	if (point.x < (int)Psettings->m_borderWidth)
	{
		CTer::OnLButtonDown(modKeys, point);
		return;
	}
	else if (point.x <= (int)Psettings->m_borderWidth + 2 && !(modKeys & MK_SHIFT))
	{
		// click in area between border and start of text - place caret at beginning of line
		DefWindowProc(WM_LBUTTONDOWN, modKeys, (LPARAM)MAKELPARAM(Psettings->m_borderWidth + 3, point.y));
		return;
	}
	if (modKeys & MK_SHIFT)
	{
		// shift clik extension
		CTer::OnLButtonDown(modKeys, point);
		return;
	}

	WTString presel = GetSelString();
	if (GetSymDtypeType())
		g_CompletionSet->Dismiss();
	if (!undoLock)
	{
		undoLock = true;
		if (gShellAttr && !gShellAttr->IsDevenv10OrHigher()) // [case: 59442]/[case: 62508]
		{
			CTer::OnLButtonDown(modKeys, point);
			if (gShellAttr->IsDevenv()) // if wnd doesn't have focus, vsnet doesn't update caret until later
				SetTimer(ID_CURSCOPEWORD_TIMER, 100, NULL);
		}

		// prevent selecting eol from past eol
		ulong p1, p2;
		GetSel(p1, p2);
		if (p1 < p2 && (CharAt(p2 - 1) == '\n' || CharAt(p2 - 1) == '\r'))
		{
			CPoint crtpt = vGetCaretPos();
			CPoint mousept;
			GetCursorPos(&mousept);
			vScreenToClient(&mousept);
			if ((crtpt.x < mousept.x) && (crtpt.y <= mousept.y))
				SetSel(p1, p2 - 1);
		}

		undoLock = false;
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			SetTimer(ID_CURSCOPEWORD_TIMER, 100,
			         NULL); // Causes offset issues in vs2010, since mouse has not been moved in editor yet.
		else
			SetStatusInfo(); // moving out of noscroll
	}
	else
		CTer::OnLButtonDown(modKeys, point);
	// ScrollCaret(false);
	WTString sel = GetSelString();
	// set our selection to match new selection
	// 	if(sel.GetLength()){
	// 		long pos = GetBufIndex(CurPos(TRUE));
	// 		if(pos > sel.GetLength())
	// 			SetSel(pos, pos- sel.GetLength());
	// 	}

	if (sel.GetLength() && sel == presel && Psettings->m_enableVA)
	{
		// drag and drop
		if (IsFeatureSupported(Feature_FormatAfterPaste, m_ScopeLangType) &&
		    (sel.Find("\n") != -1 || sel.Find("\r") != -1)
		    // don't format comments
		    && sel.Find("//") == -1 && sel.Find("/*") == -1 &&
		    (gShellAttr->IsMsdev() || gShellAttr->IsDevenv()) // breaks dnd for vapad
		)
		{
			if (gShellAttr->IsDevenv())
			{
				if (oldModCookie != m_modCookie) // Only reformat if there was a mod; case=53026
					gShellSvc->FormatSelection();
			}
			else
			{
				long p2 = (long)CurPos();
				long p1 = GetSelBegPos();
				//				long p3 = TERRCTOLONG((TERROW(p1)-1), 1);
				//				long p4 = TERRCTOLONG((TERROW(p2)+1), 1);
				//				SetSel(p3, p4);
				gShellSvc->FormatSelection();
				SetSel(p1, p2);
			}
		}
		// probably a drag and drop, mark buffer dirty
		if (gShellAttr->IsDevenv())
		{
			GetBuf(TRUE);
			long p2 = (long)CurPos();
			long p1 = GetSelBegPos();
			CPoint pt((int)p1, (int)p2);
			Reparse();
		}
		else
		{
			SetBufState(BUF_STATE_WRONG);
			if (HasFocus())
			{
				OnModified();
				SetTimer(ID_GETBUFFER, 500u, NULL);
				Reparse();
			}
			else
			{
				// don't sync if we don't have focus since the noscroll
				// object won't work; update will occur when focus is returned
			}
		}
		// Need to manually set m_hasSel because after Reparse it is incorrect.
		// Fixes loss of selection after ctrl+Click and drag and drop.
		// m_hasSel must be up to date for the ID_GETBUFFER timer to know that
		// there is a selection.
		m_hasSel = true;
	}

	WTString cwd = CurWord();
	if (g_CVAClassView && (!cwd.GetLength() || !ISCSYM(cwd[0])))
		g_CVAClassView->GetOutline(TRUE);

	if (Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::CtrlLeftClick) && (modKeys & MK_CONTROL))
	{
		mModifClickKey = VK_CONTROL;
		mModifClickArmedPt = point;
	}
	else if (Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::AltLeftClick) && (GetKeyState(VK_MENU) & 1000))
	{
		mModifClickKey = VK_MENU;
		mModifClickArmedPt = point;
	}
	else
	{
		mModifClickKey = 0;
		mModifClickArmedPt.SetPoint(0, 0);
	}
}

void EdCnt::OnMouseMove(uint keys, CPoint point)
{
	DISABLECHECK();
	DEFTIMERNOTE(OnMouseMoveTimer, NULL);
	//	if(0 && gShellAttr->IsDevenv10OrHigher())
	//	{
	//		// Change into client point of WPF HWND
	//		GetCursorPos(&point);
	//		vScreenToClient(&point);
	//	}
	static CPoint lpos;
	const int maxOffset = 5; // Prevent slight mouse bump from flickering tooltips. [case=37468][case=40275]
	if (point == lpos || (m_ttTypeInfo && abs(point.x - lpos.x) < maxOffset && abs(point.y - lpos.y) < maxOffset))
	{
		if (point != lpos)
		{
			// [case: 44602] restore HCB response
			if (IsFeatureSupported(Feature_HCB, m_ScopeLangType))
			{
				GetCursorPos(&s_hoverPt);
				SetTimer(HOVER_CLASSVIEW_TIMER, 500, NULL);
			}
		}
		return;
	}

	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->OnMouseMove(keys, point);

	lpos = point;
	if (HasFocus() || (g_CompletionSet && g_CompletionSet->IsExpUp(NULL)))
		ClearAllPopups(false);
	else
		ClearAllPopups(true);

	CTer::OnMouseMove(keys, point);
	// display tooltip of sym under cursor
	KillTimer(ID_TIMER_MOUSEMOVE);
	SetTimer(ID_TIMER_MOUSEMOVE, 200u, NULL);
	//	KillTimer(HOVER_CLASSVIEW_TIMER);
	if (IsFeatureSupported(Feature_HCB, m_ScopeLangType))
		SetTimer(HOVER_CLASSVIEW_TIMER, 500u, NULL);
}

void EdCnt::OnLButtonUp(uint modKeys, CPoint point)
{
	vCatLog("Editor.Events", "VaEventUE  OnLButtonUp  key=0x%x", modKeys);
	DISABLECHECK();
	DEFTIMERNOTE(OnLButtonUpTimer, NULL);
	LOG("OnLButtonUp");
	LogElapsedTime let("Ed::LBU", 50);

	WTString sel = GetSelString();
	m_hasSel = sel.GetLength() != 0;
	KillTimer(ID_TIMER_MOUSEMOVE);

	if (m_hasSel && gShellAttr->IsDevenv() && gShellSvc->HasBlockModeSelection(this))
		g_ScreenAttrs.Invalidate();

	CTer::OnLButtonUp(modKeys, point);
	if (gShellAttr->IsMsdev() && m_hasSel && m_pDoc /*&& m_pDoc->Emulation() == dsDevStudio*/)
	{
		// to fix msdev bug where ctrl+click selecting back lies about cursor pos in dsm_swapanchor
		if (EolTypes::eolNone == EolTypes::GetEolType(sel) && modKeys)
		{
			long p1, p2;
			GetSel(p1, p2);
			const WTString bb(GetBufConst());
			const long i = GetBufIndex(bb, p1);
			if (i > sel.GetLength() &&
			    strncmp(&bb.c_str()[i - sel.GetLength()], sel.c_str(), (uint)sel.GetLength()) == 0)
			{
				SetSel(i - sel.GetLength(), i);
			}
			else if (strncmp(&bb.c_str()[i], sel.c_str(), (uint)sel.GetLength()) == 0)
				SetSel(i + sel.GetLength(), i);
		}
	}

	// if (!gShellAttr->IsDevenv10OrHigher())  // Allow hovering tips in 2010.
	{
		if (m_hasSel)
		{
			m_vpos = vGetCaretPos().y; // prevent Check for scroll from dismissing icon
			if (mModifClickArmedPt != point ||
			    (!Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::CtrlLeftClick) &&
			     !Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::AltLeftClick)))
			{
				if (g_ScreenAttrs.m_VATomatoTip)
					g_ScreenAttrs.m_VATomatoTip->OnHover(mThis, point);
			}
		}
	}

	VASmartSelect::HideToolTip();

	if (mModifClickArmedPt == point
	    // [case: 119698] revert changes for [case: 118070]
	    /*&& !gShellAttr->IsDevenv15OrHigher()*/)
	{
		if (mModifClickKey == VK_CONTROL /*&& !gShellAttr->IsCppBuilder()*/) // CppBuilder has their own ctl+click
			OnMouseCmd(VaMouseCmdBinding::CtrlLeftClick, point);
		else if (mModifClickKey == VK_MENU)
			OnMouseCmd(VaMouseCmdBinding::AltLeftClick, point);
	}

	mModifClickKey = 0;
	mModifClickArmedPt.SetPoint(0, 0);

	if (g_CVAClassView && !m_hasSel)
	{
		// [case: 65885]
		const WTString cwd = CurWord();
		if (cwd.GetLength() && ISCSYM(cwd[0]))
			g_CVAClassView->EditorClicked();
	}
}

void EdCnt::OnRButtonUp(uint modKeys, CPoint point)
{
	vCatLog("Editor.Events", "VaEventUE  OnRButtonUp  key=0x%x", modKeys);
	DISABLECHECK();

	VASmartSelect::HideToolTip();

	struct auto_reset_ShiftRClick
	{
		bool& val;
		auto_reset_ShiftRClick(bool& v) : val(v)
		{
		}
		~auto_reset_ShiftRClick()
		{
			val = false;
		}
	} _ar_src(mShiftRClickDoCmd);

	const int borderWidth = gShellAttr->IsDevenv() ? 15 : 20;
	if (m_lborder && !gShellAttr->IsDevenv10OrHigher() && ((Psettings->m_menuInMargin && point.x < borderWidth)))
	{
		m_lborder->DisplayMenu(point);
	}
	else if (
	    // [case: 119698] revert changes for [case: 118070]
	    // !gShellAttr->IsDevenv15OrHigher() &&
	    mShiftRClickDoCmd && Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::ShiftRightClick) &&
	    (GetKeyState(VK_SHIFT) & 0x1000))
	{
		OnMouseCmd(VaMouseCmdBinding::ShiftRightClick, point);
	}
	else
	{
		// [case: 34433] use our spell check menu in vs2005/vs2008 in htm, asp
		// and js even if using markers
		bool doSpellCheck = !Psettings->mUseMarkerApi;
		if (!doSpellCheck && gShellAttr->IsDevenv8OrHigher() && !gShellAttr->IsDevenv10OrHigher() &&
		    Is_HTML_JS_VBS_File(m_ScopeLangType))
		{
			doSpellCheck = true;
		}

		if (doSpellCheck)
		{
			CWnd::Invalidate(FALSE);
			CWnd::UpdateWindow();
			if (SpellCheckWord(point, false))
				return;
		}

		CTer::OnRButtonUp(modKeys, point);
	}
}

void EdCnt::OnMButtonDown(uint modKeys, CPoint point)
{
	VASmartSelect::HideToolTip();

	if (Psettings && Psettings->m_enableVA && Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::MiddleClick))
	{
		mMiddleClickDoCmd = false;

		if ((modKeys & MK_VA_NOTCMD) && !HasFocus())
			vSetFocus();

		if (!(modKeys & MK_VA_NOTCMD) && Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::MiddleClick))
		{
			// don't do command when user is closing tab or clicking on scroll bar or line numbers
			CRect rect;
			vGetClientRect(&rect);
			mMiddleClickDoCmd = rect.PtInRect(point) ? true : false;

			if (mMiddleClickDoCmd)
			{
				bool change_position = true;
				long chPos = CharFromPos(&point);

				if (HasSelection())
				{
					long sp, ep;
					GetSel2(sp, ep);
					sp = GetBufIndex(sp);
					ep = GetBufIndex(ep);

					if (sp > ep)
						std::swap(sp, ep);

					chPos = GetBufIndex(chPos);
					change_position = chPos < sp || chPos >= ep;
				}

				if (change_position)
				{
					SetPos((uint)chPos);

					if (gShellAttr->IsDevenv10OrHigher())
					{
						CPoint screenPt(point);
						vClientToScreen(&screenPt);
						EdCntWPF* wpfEd = dynamic_cast<EdCntWPF*>(this);
						if (!wpfEd || !wpfEd->MoveCaretToScreenPos(screenPt.x, screenPt.y))
							DefWindowProc(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
					}
					else if (gShellAttr->IsDevenv())
					{
						DefWindowProc(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
					}
				}
			}
		}
	}
	else
	{
		return CWnd::OnMButtonDown(modKeys, point);
	}
}

void EdCnt::OnMButtonUp(uint modKeys, CPoint point)
{
	if (Psettings && Psettings->m_enableVA && Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::MiddleClick)
	    // [case: 119698] revert changes for [case: 118070]
	    /*&& !gShellAttr->IsDevenv15OrHigher()*/)
	{
		if (mMiddleClickDoCmd && HasFocus())
		{
			mMiddleClickDoCmd = false;
			OnMouseCmd(VaMouseCmdBinding::MiddleClick, point);
		}
	}
	else
	{
		return CWnd::OnMButtonUp(modKeys, point);
	}
}

bool EdCnt::OnMouseCmd(VaMouseCmdBinding binding, CPoint pt /*= CPoint()*/)
{
	static bool in_command = false;

	if (!in_command)
	{
		// only one mouse command at a time is allowed
		struct _scoped_var
		{
			bool& m_val;
			_scoped_var(bool& val) : m_val(val)
			{
				m_val = true;
			}
			~_scoped_var()
			{
				m_val = false;
			}
		} _in_cmd_scoped(in_command);

		// index of command
		int index = (int)Psettings->mMouseClickCmds.get((DWORD)binding);

		// filter out repeated ALT key messages to allow menu stay open
		CMenuXPAltRepeatFilter alt_filter(binding == VaMouseCmdBinding::AltLeftClick, false);

		// hide tooltips
		ClearAllPopups(true);

		VATooltipBlocker vaTTBlock; // disallows refactoring menu to show
		switch ((VaMouseCmdAction)index)
		{
		case VaMouseCmdAction::Goto:
			return TRUE == GoToDef(posAtCursor);
		case VaMouseCmdAction::SuperGoto:
			return TRUE == SuperGoToDef(posAtCursor);
		case VaMouseCmdAction::ContextMenu:
			vClientToScreen(&pt);
			ShowVAContextMenu(mThis, pt, true);
			return true;
		case VaMouseCmdAction::ContextMenuOld:
			if (m_lborder)
			{
				m_lborder->DisplayMenu(pt);
				return true;
			}
			break;
		case VaMouseCmdAction::RefactorCtxMenu:
			if (gVaInteropService && Psettings->mCodeInspection)
			{
				gVaInteropService->DisplayQuickActionMenu(true);
				return true;
			}
			else if (g_ScreenAttrs.m_VATomatoTip)
			{
				g_ScreenAttrs.m_VATomatoTip->DisplayTipContextMenu(true);
				return true;
			}
			break;
		case VaMouseCmdAction::None:
		default:
			break;
		}
	}

	return false;
}

// [case: 91013] Ctrl + Wheel and Ctrl + Shift + Wheel overridable to use Smart Select commands or to do nothing
bool EdCnt::OnMouseWheelCmd(VaMouseWheelCmdBinding binding, int wheel, CPoint pt /*= CPoint()*/)
{
	static bool in_command = false;

	if (!in_command)
	{
		// only one mouse command at a time is allowed
		struct _scoped_var
		{
			bool& m_val;
			_scoped_var(bool& val) : m_val(val)
			{
				m_val = true;
			}
			~_scoped_var()
			{
				m_val = false;
			}
		} _in_cmd_scoped(in_command);

		// index of command
		int index = (int)Psettings->mMouseWheelCmds.get((DWORD)binding);

		switch ((VaMouseWheelCmdAction)index)
		{
		case VaMouseWheelCmdAction::None:
			return true; // just eat the message
		case VaMouseWheelCmdAction::Selection: {
			uint cmd = uint(wheel > 0 ? icmdVaCmd_SmartSelectExtend : icmdVaCmd_SmartSelectShrink);

			if (QueryStatus(cmd))
				Exec(cmd);

			return true;
		}
		case VaMouseWheelCmdAction::BlockSelection: {
			uint cmd = uint(wheel > 0 ? icmdVaCmd_SmartSelectExtendBlock : icmdVaCmd_SmartSelectShrinkBlock);

			if (QueryStatus(cmd))
				Exec(cmd);

			return true;
		}
		default:
			break;
		}
	}

	return false;
}

void EdCnt::OnRButtonDown(uint modKeys, CPoint point)
{
	vCatLog("Editor.Events", "VaEventUE OnRButtonDown  key=0x%x", modKeys);
	DISABLECHECK();
	DEFTIMERNOTE(OnRButtonDownTimer, NULL);
	LOG("OnRButtonDown");

	KillTimer(ID_TIMER_MOUSEMOVE);

	CWnd::Invalidate(FALSE);
	CWnd::UpdateWindow();
	if (gShellAttr->IsDevenv10OrHigher())
		ClearAllPopups(true); // Dismiss completionset
	if (gShellAttr->IsDevenv() && g_StrFromCursorPosUnderlined)
	{
		// [case: 34433] use our spell check menu in vs2005/vs2008 in htm, asp
		// and js even if using markers
		bool doSpellCheck = !Psettings->mUseMarkerApi;
		if (!doSpellCheck && gShellAttr->IsDevenv8OrHigher() && !gShellAttr->IsDevenv10OrHigher() &&
		    Is_HTML_JS_VBS_File(m_ScopeLangType))
		{
			doSpellCheck = true;
		}

		if (doSpellCheck)
		{
			PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
			PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
			return; // don't issue rdown in vsnet, caused selection problems after
		}
	}

	const int borderWidth = gShellAttr->IsDevenv() ? 15 : 20;

	const bool apply_old_behavior = m_lborder && ((Psettings->m_menuInMargin && point.x < borderWidth));

	// [case: 98310] don't exec command when mouse is over status bar or scrollbar
	bool mouse_over_this = true;
	if (gShellAttr->IsDevenv10OrHigher())
	{
		EdCntWPF* wpfCtl = static_cast<EdCntWPF*>(this);
		if (wpfCtl != nullptr)
			mouse_over_this = wpfCtl->IsMouseOver();
	}

	mShiftRClickDoCmd = mouse_over_this && // [case: 98310]
	                    Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::ShiftRightClick) &&
	                    (GetKeyState(VK_SHIFT) & 0x1000);

	if (apply_old_behavior || mShiftRClickDoCmd)
	{
		// [case: 3797] if not in margin, move caret to where the click occurred
		if (!(Psettings->m_menuInMargin && point.x < borderWidth) && !HasSelection())
		{
			if (gShellAttr->IsDevenv())
			{
				PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
#if defined(RAD_STUDIO)
				PostMessage(WM_LBUTTONUP, MK_LBUTTON,
				            MAKELPARAM(point.x, point.y)); // CppBuilder needs the up to offset the down.
#else
				PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
#endif
			}
			else
			{
				long chPos = CharFromPos(&point); // sets ptchar to actual char pos
				SetPos((uint)chPos);
			}
		}
		return;
	}
	else
	{
		CWnd::OnRButtonDown(modKeys, point);
	}
}

void EdCnt::OnKeyUp(uint key, uint repeatCount, uint flags)
{
	vCatLog("Editor.Events", "VaEventUE  OnKeyUp '%c', key=0x%x, flags=%x, pos=0x%x", key, key, flags, CurPos());
	DISABLECHECK();
#if defined(RAD_STUDIO)
	//  logic from OnChar(), since we don't get wm_char events
	SetBufState(CTer::BUF_STATE_DIRTY);
	m_ReparseScreen = true;
	if (key == VK_OEM_1) // ';'
		Reparse();
	else
		SetTimer(ID_CURSCOPEWORD_TIMER, 500, NULL);
#endif

	if (!(GetKeyState(VK_CONTROL) & 0x1000))
		VAProjectAddFileMRU(FileName(), mruFileFocus);
	rapidfire = 0;
	SetTimer(HOVER_CLASSVIEW_OUTLINE_TIMER, 500, NULL);
	if (gShellAttr->IsDevenv())
	{
		SetTimer(ID_TIMER_CHECKFORSCROLL, 100, NULL);
		if (VK_RETURN == key && !g_CompletionSet->IsExpUp(NULL))
		{
			// handle the timer immediately when VK_RETURN has been pressed and
			// the listbox is not visible to update caret pos and prevent
			// subsequent close of listbox after typing continues (due to
			// delayed CloseAllPopups) case=3928
			OnTimer(ID_TIMER_CHECKFORSCROLL);
		}
	}

	if ((VK_RETURN == key || VK_ESCAPE == key) && Psettings->m_incrementalSearch)
		Psettings->m_incrementalSearch = false;

	if (gShellAttr->IsMsdev())
	{
		g_inMacro = false;
		m_hasSel = GetSelString().GetLength() != 0;
	}
	else
	{
		_ASSERTE(!g_inMacro);
		m_hasSel = HasSelection() != FALSE; // false;  // No idea why this was false?
	}

	DEFTIMERNOTE(OnKeyUpTimer, NULL);
	LOG("OnKeyUp");

	CTer::OnKeyUp(key, repeatCount, flags);
	// typing m[shift] before an existing sym leaves _
	if (m_ReparseScreen && key == VK_SHIFT && HasSelection() && WordLeftOfCursor() == "m_" && GetSelString() == "_")
		SetPos(CurPos(true));
	SetTimer(ID_KEYUP_TIMER, g_statusDelay, NULL);
	if (!(VK_DOWN == key || VK_UP == key || VK_NEXT == key || VK_PRIOR == key /*|| g_ListBoxLoader->LstBox()*/
	      || VK_CONTROL == key || VK_MENU == key || VK_SHIFT == key))
	{
		if (Modified() && GetSymDtypeType() == DEFINE && m_lastScope == DB_SCOPE_PREPROC && key != VK_HOME &&
		    key != VK_END && key != VK_LEFT && key != VK_RIGHT && key != VK_SHIFT && key != VK_MENU &&
		    key != VK_CONTROL)
		{
			if (ISALNUM((int)key))
				SetTimer(IDT_PREPROC_KEYUP, g_statusDelay, NULL);
			else
			{
				InterlockedIncrement(&g_haltImportCnt);
				Reparse();
				InterlockedDecrement(&g_haltImportCnt);
			}
		}
	}
	if (HasFocus())
	{ // CNT+TAB caused problems
		if (Psettings->m_incrementalSearch)
			CheckForSaveAsOrRecycling();
	}
	if (m_FileHasTimerForFullReparse || m_ReparseScreen)
		SetReparseTimer();
	g_CompletionSet->ProcessEvent(mThis, (long)key, WM_KEYUP);
}

extern uint g_lastChar;
void EdCnt::OnKeyDown(uint key, uint repeatCount, uint flags)
{
	vCatLog("Editor.Events", "VaEventUE  OnKeyDown '%c', key=0x%x, flags=%x, pos=0x%x", key, key, flags, CurPos());
	DISABLECHECK();
	KillTimer(ID_UNDERLINE_ERRORS);
	KillTimer(ID_TIMER_GETSCOPE);

	DEFTIMERNOTE(OnKeyDownTimer, itos(key));
	// g_ScreenAttrs.m_VATomatoTip.Dismiss(); // Do not dismiss on any keydown, might be display binding keys.
	m_ignoreScroll = false; // repaint onscroll
	switch (key)
	{
	case VK_ESCAPE:
		ClearAllPopups(true);
		DisplayToolTipArgs(false);
		g_CompletionSet->Dismiss();
		SetStatus(IDS_READY);
		break;
	case VK_RIGHT:
	case VK_LEFT:
		if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
			SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
		break;
	case VK_TAB:
		if (gShellAttr->IsDevenv10OrHigher())
		{
			// [case: 79124]
			_ASSERTE(WPF_ViewManager::Get() && !WPF_ViewManager::Get()->HasAggregateFocus());
			return;
		}
		CmEditExpand(ET_EXPAND_TAB);
		return;
	case VK_DELETE:
	case VK_BACK:
		if (flags != 0xdead)
			CTer::OnKeyDown(key, repeatCount, flags);
		UpdateWindow();
		SetTimer(ID_ARGTEMPLATE_TIMER, 10, NULL);
		SetTimer(ID_GETBUFFER, 20, NULL);
		SetTimer(ID_TIMER_GETHINT, 10, NULL);
		m_typing = true;
		return;
	case VK_SPACE:
		if ((GetKeyState(VK_CONTROL) & 0x1000) && (GetKeyState(VK_SHIFT) & 0x1000))
		{
			DisplayToolTipArgs(true);
			return;
		}
		if ((GetKeyState(VK_CONTROL) & 0x1000))
		{                                         // DSM_AUTOCOMPLETE
			CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
			return;
		}
		break;
	}
	LOG("OnKeyDown");
	int line;
	ulong p1, p2;

	// insert "_" on VK_SHIFT for m_
	if (Psettings->m_auto_m_ && !Psettings->m_incrementalSearch && !HasVsNetPopup(TRUE) && key == VK_SHIFT &&
	    CurWord() == "m" && m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR &&
	    !gShellSvc->HasBlockModeSelection(this) && gTypingAllowed)
	{
		const bool wasListboxUp = g_CompletionSet && g_CompletionSet->IsExpUp(mThis);
		const int oldPoptype = g_CompletionSet->m_popType;
		Insert("_");
		ulong cp = (ulong)CurPos();
		SetSel(cp - 1, cp);
		if (wasListboxUp)
		{
			if (ET_SUGGEST == oldPoptype)
				g_CompletionSet->DoCompletion(mThis, -1, false);
			else
				SendMessage(WM_COMMAND, DSM_LISTMEMBERS);
		}
	}
	if (m_ttTypeInfo)
	{
		ArgToolTip* tmp = m_ttTypeInfo;
		m_ttTypeInfo = NULL;
		delete tmp;
	}
	if (m_tootipDef)
	{
		switch (key)
		{
		case VK_TAB:
		case VK_ESCAPE:
			DisplayToolTipArgs(false);
			break;
		case VK_NEXT:
			if (!(GetKeyState(VK_CONTROL) & 0x1000))
			{
				DisplayToolTipArgs(false);
				break;
			}
			if (m_ttParamInfo)
				m_ttParamInfo->OnMouseActivate(NULL, 0, WM_LBUTTONDOWN);
			return;
		case VK_DOWN:
			if (m_ttParamInfo && m_ttParamInfo->m_totalDefs > 1 &&
			    m_ttParamInfo->m_totalDefs != m_ttParamInfo->m_currentDef)
			{
				m_tootipDef++;
				DisplayToolTipArgs(true);
				return;
			}
			DisplayToolTipArgs(false);
			SetTimer(ID_ARGTEMPLATE_TIMER, 20, NULL);
			break;
		case VK_PRIOR:
			if (!(GetKeyState(VK_CONTROL) & 0x1000))
			{
				DisplayToolTipArgs(false);
				break;
			}
			if (m_ttParamInfo)
				m_ttParamInfo->OnMouseActivate(NULL, 0, WM_RBUTTONDOWN);
			return;
		case VK_UP:
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 1)
			{
				if (m_tootipDef > 1)
					m_tootipDef--;
				DisplayToolTipArgs(true);
				return;
			}
			DisplayToolTipArgs(false);
			SetTimer(ID_ARGTEMPLATE_TIMER, 20, NULL);
			break;
		case VK_RETURN:
			m_typing = true;
			DisplayToolTipArgs(false);
		default:
			if (strchr("()<,", (char)key)) // only display show tt-args on these keys
				SetTimer(ID_ARGTEMPLATE_TIMER, 100, NULL);
			break;
		}
	}

	if (Psettings->m_incrementalSearch && gShellAttr->IsMsdev())
	{
		if (key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT || key == VK_NEXT || key == VK_PRIOR)
		{
			// stop IncSearch
			::SetForegroundWindow(::GetDesktopWindow());
			Sleep(25);
			SetForegroundWindow();
			Psettings->m_incrementalSearch = false;
		}
		else
			return;
	}

	if (key == VK_RETURN)
	{
		Psettings->m_incrementalSearch = false;
		// see note in OnKeyUp
		if (gShellAttr->IsMsdev() && ISPRESSED(VK_CONTROL) &&
		    g_CompletionSet->ProcessEvent(mThis, (long)key, WM_KEYDOWN))
			return; // special handling for Ctrl+Enter to select listbox item
	}

	if ((key == VK_UP) || (key == VK_DOWN))
	{ // arrow
		// catch text that needs parsing
		if (m_ReparseScreen)
			Reparse();
		if (g_lastChar == VK_RETURN)
		{
			// last key was enter, remove auto-indent
			// select all white space left of caret
			uint aip1 = LinePos();
			uint aip2 = aip1;
			if (aip1 != aip2)
			{
				token t = GetSubString(aip1, aip2);
				if (!t.read(" \t").GetLength())
				{ // only if blank line
					SetSelection((long)aip1, (long)aip2);
					Insert("");
				}
			}
			g_lastChar = 0;
		}
		if (!(gShellAttr->IsDevenv() && key == VK_UP && CurLine() == 1))
		{
			if (flags != 0xdead)
				CTer::OnKeyDown(key, repeatCount, flags);
		}
		if (m_lborder)
			m_lborder->DrawBkGnd(); // update border while scrolling up/down
		return;
	}
	else if ((key == VK_PRIOR) || (key == VK_NEXT) || (key == VK_LEFT) || (key == VK_RIGHT))
	{
		// cancel AutoMatch typeover
		m_autoMatchChar = '\0';
		// Prevent beep at beg or end of file or line
		line = CurLine();
		GetSel(p1, p2);
		switch (key)
		{
		case VK_PRIOR:
			if (line == 0)
				return;
			break;
		case VK_NEXT:
			break;
		case VK_LEFT:
		case VK_RIGHT:
			if (VK_LEFT == key && p1 == 0 && p2 == 0)
				return;

			if (flags != 0xdead)
			{
				if (flags != 0xdead)
					CTer::OnKeyDown(key, repeatCount, flags);
			}
			return;
		}
	}

	if (key == VK_RETURN)
	{
		// set mod mark for current line if hit return
		//  in the middle of the line
		uint cp = CurPos();
		long cl = LineFromChar(-1);
		if (cp != LinePos(cl))
		{
			int modLnStartPos = LineIndex();
			if (cp < (uint)modLnStartPos)
				m_LnAttr->Line(cl)->SetModify();
		}
		return; // let OnChar take it
	}
	else if (key == VK_TAB)
	{
		GetSel(p1, p2);
		if (p1 == p2)
		{
			if (LineIndex() == (int)p1) // if tab is hit in column 0, just pass on
				InsertTabstr();
			else if (Psettings->m_tabInvokesIntellisense)
				CmEditExpand(ET_EXPAND_TAB);
			else
				InsertTabstr();
		}
		return;
	}
	else
	{
		if (flags != 0xdead)
			CTer::OnKeyDown(key, repeatCount, flags);
	}
}

void EdCnt::OnSysKeyUp(uint key, uint repeatCount, uint flags)
{
	DISABLECHECK();
	DEFTIMERNOTE(OnKeyUpTimer, itos(key));
	LOG("OnSysKeyUp");
	CTer::OnSysKeyUp(key, repeatCount, flags);
	if (!HasSelection())
	{
		// SetStatusInfo();
		int pc = CharAt(CurPos() - 1);
		(void)pc;
		int c = CharAt(CurPos());
		(void)c;
	}
}
void EdCnt::OnSysKeyDown(uint key, uint repeatCount, uint flags)
{
	vCatLog("Editor.Events", "EdOSKD:%c", key);
	DISABLECHECK();
	DEFTIMERNOTE(OnSysKeyDownTimer, itos(key));
	ClearAllPopups(); // kill tooltips on alt back/forward
	LOG("OnSysKeyDown");
	CTer::OnSysKeyDown(key, repeatCount, flags);
}

WTString EdCnt::GetSelString()
{
	DEFTIMERNOTE(GetSelStringTimer, NULL);
	if (gShellAttr->IsDevenv())
	{
		const WTString sel((LPCWSTR)SendVamMessage(VAM_GETSELSTRINGW, 0, 0));
		return sel;
	}
	if (!m_pDoc)
	{
		long p1, p2;
		GetSel(p1, p2);
		WTString sel = GetSubString((ulong)p1, (ulong)p2);
		return sel;
	}
	else
		return CTer::m_pDoc->GetCurSelection();
}

CStringW EdCnt::GetSelStringW()
{
	DEFTIMERNOTE(GetSelStringTimer, NULL);
	if (gShellAttr->IsDevenv())
	{
		CStringW sel = (LPCWSTR)SendVamMessage(VAM_GETSELSTRINGW, 0, 0);
		return sel;
	}
	else
		return CStringW(GetSelString().Wide());
}

WTString EdCnt::GetSubString(ulong p1, ulong p2)
{
	DEFTIMERNOTE(GetSubString, NULL);
	CatLog("Editor", "GetSubString");
	// swap max and min
	ulong tp1 = min(p1, p2);
	ulong tp2 = max(p1, p2);
	WTString s;
	if (tp2 != tp1)
	{
		if (tp1 == NPOS || tp1 > tp2)
		{
			ASSERT(FALSE);
			return s;
		}
		if (tp1 < tp2)
			s = CTer::GetSubString(tp1, tp2);
	}
	return s;
}

void EdCnt::CheckForDeletedFile(const CStringW& filepathname)
{
	// [case: 34356] gutted this
	DEFTIMERNOTE(CheckForDeletedFileTimer, CString(filepathname));
	if (!IsFile(filepathname))
		return;

	SetTimer(ID_TIMER_RELOAD_LOCAL_DB, 200u, NULL);
}

#include "dblock.h"

WTString EdCnt::CurScopeWord()
{
	if (!Psettings || !Psettings->m_enableVA)
		return NULLSTR;
	if (CAN_USE_NEW_SCOPE() || m_ftype == JS || m_ftype == RC || m_ftype == Idl)
	{
		Scope(false, true);
		return GetSymDef();
	}

	if (m_txtFile)
	{
		Scope();
		WTString cwd = CurWord();
		WTString tscope, tdef;
		uint type, attrs, dbFlags;
		type = attrs = dbFlags = 0;
		WTString newDef;
		WTString newScp;
		{
			AutoLockCs l(mDataLock);
			newScp = GetSymScope();
			newDef = GetSymDef();
		}
		GetParseDb()->CwScope(newScp, cwd, newDef, type, attrs, dbFlags);
		if (!type || type == CTEXT || !(attrs & V_INFILE))
		{
			UpdateSym(nullptr);
			return NULLSTR;
		}

		AutoLockCs l(mDataLock);
		SymScope = newScp;
		SymDef = newDef;
		SymType.setType(type, attrs, dbFlags);
		return newDef;
	}

	return NULLSTR;
}

WTString EdCnt::GetCurrentIndentation()
{
	// if caret is on a new line that has only auto indents, VC doesn't
	//  give us the indents when we call GetSubString().
	// This will result in text that is properly indented for the first line,
	//  but has no indentation on all following lines.  Use the Version Info
	//  code template as a testcase.
	WTString curInd = GetSubString(LinePos(), CurPos());
	if (m_pDoc && !m_hasSel && (!curInd.GetLength() || curInd.c_str()[0] == '\r') && TERCOL(CurPos()) > 1)
	{
		// current line is blank yet we are at an indented column
		gShellSvc->FormatSelection(); // make them fill line with appropriate indentation
		GetBuf(TRUE);                 // get new indentation
		curInd = GetSubString(LinePos(), CurPos());
	}
	int i;
	for (i = 0; strchr(" \t", curInd.c_str()[i]); i++)
		;
	curInd = curInd.Mid(0, i);
	return curInd;
}

void EdCnt::PreInsert(long& begSelPos, long& p1, long& p2)
{
	m_typing = FALSE;
	modified = TRUE;
	m_lastScopePos = 0; // so scope gets updated
	SetTimer(ID_GETBUFFER, 1, NULL);
	SetTimer(ID_TIMER_GETHINT, 10, NULL);

	// Added begSelPos because p1 was not always right when reformatting.
	// should p1=begSelPos and p2 = endSelPos below?
	begSelPos = GetSelBegPos();
	GetSel(p1, p2); // get selection before replace
}

BOOL EdCnt::Insert(const char* txt, bool saveUndo /* = true */, vsFormatOption format /*= vsFormatOption::noFormat*/,
                   bool handleIndentation /*= true*/)
{
	vCatLog("Editor.Events", "VaEventED   Insert '%s'", txt);
	DEFTIMERNOTE(InsertTimer, NULL);
	LOG("Insert");

	OnModified();
	long begSelPos, p1, p2;
	PreInsert(begSelPos, p1, p2);
	// format paste buffer
	WTString str(txt);
	if (str.GetLength() < 1000)
		g_rbuffer.Add(str);

	const EolTypes::EolType et = EolTypes::GetEolType(str);
	if (EolTypes::eolNone != et)
	{
		const WTString strLnBrk(EolTypes::GetEolStr(et));
		const WTString edLnBrk(GetLineBreakString());
		if (strLnBrk != edLnBrk)
			str.ReplaceAll(strLnBrk, edLnBrk);
		if (handleIndentation)
			str.ReplaceAll(edLnBrk, edLnBrk + GetCurrentIndentation());
	}

	// format in ReplaceSel for vs, otherwise in PostInsert
	BOOL rslt = ReplaceSel(str.c_str(), gShellAttr->IsDevenv8OrHigher() ? format : noFormat);
	PostInsert(begSelPos, p1, p2, gShellAttr->IsDevenv8OrHigher() ? noFormat : format, (LPVOID)(LPCSTR)str.c_str(),
	           false);
	return rslt;
}

BOOL EdCnt::InsertW(CStringW str, bool saveUndo /*= true*/, vsFormatOption format /*= vsFormatOption::noFormat*/,
                    bool handleIndentation /*= true*/)
{
	vCatLog("Editor.Events", "VaEventED   Insert '%s'", (LPCSTR)CString(str));
	DEFTIMERNOTE(InsertTimerW, NULL);
	LOG("InsertW");

	long begSelPos, p1, p2;
	PreInsert(begSelPos, p1, p2);
	// format paste buffer
	const EolTypes::EolType et = EolTypes::GetEolType(str);
	if (EolTypes::eolNone != et)
	{
		const CStringW strLnBrk(EolTypes::GetEolStrW(et));
		const CStringW edLnBrk(GetLineBreakString().Wide());
		if (strLnBrk != edLnBrk)
			str.Replace(strLnBrk, edLnBrk);
		if (handleIndentation)
			str.Replace(edLnBrk, edLnBrk + CStringW(GetCurrentIndentation().Wide()));
	}
	BOOL rslt = ReplaceSelW(str, gShellAttr->IsDevenv8OrHigher() ? format : noFormat);
	PostInsert(begSelPos, p1, p2, gShellAttr->IsDevenv8OrHigher() ? noFormat : format, (LPVOID)(LPCWSTR)str, true);
	return rslt;
}

void EdCnt::PostInsert(long begSelPos, long p1, long p2, vsFormatOption format, void* str, bool isWide)
{
	modified = TRUE;
	OnModified();
	if (format != noFormat)
	{
		_ASSERTE(!gShellAttr->IsDevenv8OrHigher());
		// this format is funny - causes selection problems if replacement
		// text is shorter than original text.  see bug 167.
		// For 167, I put a workaround in VaTree::OpenItemFile - didn't
		// want to change this behavior in case something relies upon it.
		long p3, p4;
		GetSel(p3, p4);
		SetSel(begSelPos, p4);
		gShellSvc->FormatSelection(); // make them fill line with appropriate indentation
		GetSel(p3, p4);
		SetSel(p4, p4);
	}
	if (gShellAttr->IsDevenv())
	{
		GetBuf(TRUE);
	}
	else
	{ // mshack to modify insert char into buffer so we don't need to GetBuffer again
		long offset = GetBufIndex(min(p1, p2));
		if (offset)
		{
			BufRemove(p1, p2);
			if (isWide)
				BufInsertW(offset, (LPCWSTR)str);
			else
				BufInsert(offset, (LPCSTR)str);
		}
		SetTimer(ID_GETBUFFER, 500, NULL);
	}

	if (isWide)
	{
		LPCWSTR tstr = (LPCWSTR)str;
		if (wcschr(tstr, L';') || wcschr(tstr, L'}') || wcschr(tstr, L' '))
			Reparse();
	}
	else
	{
		LPCSTR tstr = (LPCSTR)str;
		if (strchr(tstr, ';') || strchr(tstr, '}') || strchr(tstr, ' '))
			Reparse();
	}

	CWnd::UpdateWindow();
	ShowCaret();
	SetTimer(ID_CURSCOPEWORD_TIMER, 500, NULL);
}

enum
{
	ALPHA,
	SPACE,
	SPECIAL,
	OTHER,
	COLON
};
int CharType(char c)
{
	if (c == '$')
		return ALPHA; // int var1$;
	if (c == ':')
		return COLON; // fixes foo<bar>::<expansion dlg> // so cwd = "::" and not ">::"
	if (strchr("(){}[];\f\"',", c))
		return SPECIAL;
	if (ISALNUM(c))
		return ALPHA;
	if (wt_isspace(c))
		return SPACE;
	if (c & 0x80)
		return ALPHA;
	return OTHER;
}

uint EdCnt::WordPos(const WTString buf, int dir, uint offset) // NEXT, PREV
{
	DEFTIMERNOTE(WordPosTimer, NULL);
	LPCSTR p = buf.c_str();
	const char lnSplitChar = EolTypes::eolCr == mEolType ? '\r' : '\n';
	long bufIdx = GetBufIndex(buf, (long)offset);
	ASSERT(bufIdx >= 0);
	long line, col;
	PosToLC(buf, (LONG)offset, line, col);

	if (dir == BEGWORD)
	{
		const int ct = bufIdx ? CharType(p[bufIdx - 1]) : CharType(p[bufIdx]);
		while (bufIdx > 0 && CharType(p[bufIdx - 1]) == ct)
		{
			if ((p[bufIdx - 1] & 0x80))
			{
				// fix decrement of col based on GetUtf8SequenceLen.
				// rewind to first char that has high bit set.
				// assumes that all chars of utf8 code point that use more than a single byte all have high bit set
				// (unlike windows mbcs). assumes that the utf8 code point is ALPHA CharType (per high bit being set).
				int bytesRewound = 0;
				for (; (bufIdx - 1 - bytesRewound) > 0 && p[bufIdx - 1 - bytesRewound] & 0x80; ++bytesRewound)
					;

				// if high bit was set, then we should have rewound at least 2?
				_ASSERTE(bytesRewound > 1);
				if (bytesRewound)
					--bytesRewound;

				// calc number of chars that follow up to starting point of current bufIdx
				do
				{
					int len = ::GetUtf8SequenceLen(&p[bufIdx - 1 - bytesRewound]);
					if (len)
					{
						if (4 == len)
						{
							// [case: 138734]
							// surrogate pair in utf16.
							// decrease col count that we return, to return utf16
							// elements rather than chars.
							--col;
						}

						bufIdx -= len;
						bytesRewound -= len;
						_ASSERTE(bytesRewound >= -2);
						--col;
					}
					else
					{
						// error of some sort, decrement so that we break out of the outer loop at some point
						bufIdx--;
						col--;
						break;
					}
				} while (bytesRewound > -1);
			}
			else
			{
				if (lnSplitChar == p[bufIdx - 1])
				{
					line--;
					col = 0xeff;
				}

				bufIdx--;
				col--;
			}
		}
	}
	else
	{
		const int ct = CharType(p[bufIdx]);
		for (; p[bufIdx] && CharType(p[bufIdx]) == ct; bufIdx++, col++)
		{
			if (lnSplitChar == p[bufIdx])
			{
				line++;
				col = 0;
			}
			else if ((p[bufIdx] & 0x80))
			{
				int len = ::GetUtf8SequenceLen(&p[bufIdx]);
				if (len)
				{
					if (4 == len)
					{
						// [case: 138734]
						// surrogate pair in utf16.
						// increase col count that we return, to return utf16
						// elements rather than chars.
						++col;
					}

					bufIdx += len - 1;
				}
				else
				{
					_ASSERTE(!"WordPos bad utf8 seq len, breaking infinite loop");
					vLog("ERROR: WordPos bad utf8 seq len");
					break;
				}
			}
		}
	}

	if (col < 0)
		col = 0;

	return (uint)TERRCTOLONG(line, col);
}

uint EdCnt::WPos(int wc, uint cp)
{
	DEFTIMERNOTE(WPosTimer, NULL);
	LOG("WPos");
	const WTString curBuf(GetBuf());
	if (cp == NPOS)
		cp = CurPos(true) /* -1*/;
	if (cp == NPOS)
		cp = 0;
	while ((cp != NPOS) && cp && (wc < 0))
	{
		cp = WordPos(curBuf, BEGWORD, cp) - 1;
		wc++;
	}
	while (wc > 0)
	{
		cp = WordPos(curBuf, ENDWORD, cp) + 1;
		wc--;
	}
	return cp;
}

WTString EdCnt::CurWord(int wc /* =0 */, bool includeConnectedWords /*= false*/)
{
	DEFTIMERNOTE(CurWordTimer, NULL);
	long p1, p2;
	LOG("CurWord");
	const WTString curBuf(GetBuf());
	GetSel(p1, p2);
	int wc_save = wc;
	if (wc <= 0)
	{
		p1 = (long)WordPos(curBuf, BEGWORD, (uint)p2);
		for (; wc < 0; wc++)
			p1 = (long)WordPos(curBuf, BEGWORD, (uint)p1);
		// CurWord will always return whole word, using LeftOfCursor for part word
		//		if(wc_save || !m_typing || GetHiddenSel())  // only read to caret while typing
		p2 = (long)WordPos(curBuf, ENDWORD, (uint)p1);
	}
	else
	{
		for (; wc > 0; wc--)
			p2 = (long)WordPos(curBuf, ENDWORD, (uint)p2);
		p1 = (long)WordPos(curBuf, BEGWORD, (uint)p2);
	}

	if (includeConnectedWords)
	{
		int pos1 = p1, pos2 = p2;
		for (;;)
		{
			int tmp = GetBufIndex(curBuf, pos1);
			if (tmp-- < 2)
				break;

			char ch = curBuf[tmp];
			if (ch != '-') // only supports '-' connections at this time
				break;

			ch = curBuf[tmp - 1];
			if (!ISALPHA(ch))
				break;

			tmp = pos1 - 1;
			pos1 = (int)WordPos(curBuf, BEGWORD, (uint)tmp);
		}

		for (;;)
		{
			int tmp = GetBufIndex(curBuf, pos2);
			if (tmp >= curBuf.GetLength())
				break;

			char ch = curBuf[tmp];
			if (ch != '-') // only supports '-' connections at this time
				break;

			ch = curBuf[tmp + 1];
			if (!ISALPHA(ch))
				break;

			tmp = pos2 + 1;
			pos2 = (int)WordPos(curBuf, ENDWORD, (uint)tmp);
		}

		p1 = pos1;
		p2 = pos2;
	}

	p1 = GetBufIndex(curBuf, p1);
	p2 = GetBufIndex(curBuf, p2);

	// include # in current word
	if (p1 > 0 && curBuf.GetAt(p1 - 1) == '#')
		p1--;
	WTString cwd = curBuf.Mid((int)p1, int(p2 - p1));
	if (!wc_save)
		m_cwd = cwd;
	return cwd;
}

WTString EdCnt::WordLeftOfCursor()
{
	DEFTIMERNOTE(WordLeftOfCursorTimer, NULL);
	long p1, p2;
	LOG("WordLeft");
	const WTString curBuf(GetBuf());
	GetSel(p1, p2);
	p1 = (long)WordPos(curBuf, BEGWORD, (uint)p2);
	p1 = GetBufIndex(curBuf, p1);
	p2 = GetBufIndex(curBuf, p2);
	// include # in current word
	if (p1 > 0 && curBuf.GetAt(p1 - 1) == '#')
		p1--;
	m_cwdLeftOfCursor = curBuf.Mid((int)p1, int(p2 - p1));
	return m_cwdLeftOfCursor;
}

WTString EdCnt::WordRightOfCursor()
{
	DEFTIMERNOTE(WordRightOfCursorTimer, NULL);
	long p1, p2;
	LOG("WordRight");
	const WTString curBuf(GetBuf());
	GetSel(p1, p2);
	p2 = (long)WordPos(curBuf, ENDWORD, (uint)p2);
	p1 = GetBufIndex(curBuf, p1);
	p2 = GetBufIndex(curBuf, p2);
	// include # in current word
	if (p1 > 0 && curBuf.GetAt(p1 - 1) == '#')
		p1--;
	WTString rstr = curBuf.Mid((int)p1, int(p2 - p1));
	return rstr;
}

WTString EdCnt::WordRightOfCursorNext()
{
	DEFTIMERNOTE(WordRightOfCursorNextTimer, NULL);
	long p1, p2;
	LOG("WordRightNext");
	const WTString curBuf(GetBuf());
	GetSel(p1, p2);
	p1 = (long)WordPos(curBuf, ENDWORD, (uint)p2);
	p2 = (long)WordPos(curBuf, ENDWORD, (uint)p1);
	p1 = GetBufIndex(curBuf, p1);
	p2 = GetBufIndex(curBuf, p2);
	// include # in current word
	if (p1 > 0 && curBuf.GetAt(p1 - 1) == '#')
		p1--;
	WTString rstr = curBuf.Mid((int)p1, int(p2 - p1));
	return rstr;
}

BOOL EdPeekMessage(HWND h, BOOL forceCheck /*= FALSE*/)
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);
	ScopedIncrementAtomic si(&gProcessingMessagesOnUiThread);
	MSG m;
	static BOOL lval = FALSE;
	static DWORD t1 = 0;
	DWORD t2 = GetTickCount();
	if (!forceCheck && (t2 - t1) < 10)
		return lval;
	t1 = t2;
	lval = TRUE;
	if (gShellAttr->IsDevenv10OrHigher())
		h = GetFocus(); // Needs the wpf view(current focused) in vs2010

	if (PeekMessage(&m, h, WM_KEYDOWN, WM_KEYDOWN, PM_NOYIELD | PM_NOREMOVE) /* && m.message!= WM_KEYUP*/)
	{
		return TRUE;
	}
	if (PeekMessage(&m, h, WM_SYSKEYDOWN, WM_SYSKEYDOWN, PM_NOYIELD | PM_NOREMOVE) /* && m.message!= WM_KEYUP*/)
	{
		return TRUE;
	}
	// Getting sporadic mousemoves breaking #include ""
	if (PeekMessage(&m, h, WM_LBUTTONDOWN, /*WM_MOUSELAST+1*/ WM_MOUSEWHEEL,
	                PM_NOYIELD | PM_NOREMOVE) /* && m.message!= WM_KEYUP*/)
	{
		return TRUE;
	}
	//	if(PeekMessage(&m, h, WM_MOUSEFIRST,0, PM_NOREMOVE) /* && m.message!= WM_KEYUP*/){
	//		if(m.message != WM_KEYUP)
	//			return TRUE;
	//	}
	lval = FALSE;
	return lval;
}

void EdCnt::ReparseScreen(BOOL underlineErrors, BOOL runAsync)
{
	if (underlineErrors && !modified_ever && !Psettings->mAutoHighlightRefs && !Psettings->mSimpleWordMatchHighlights)
	{
		if (::IsUnderlineThreadRequired())
		{
			// [case: 108516] parse required for hashtags
			underlineErrors = false;
		}
		else
		{
			// If we don't underline, should we still do the parse? case 15054
			return;
		}
	}

	if (!Psettings->m_enableVA || !(CAN_USE_NEW_SCOPE()))
		return;
	MultiParsePtr mp(GetParseDb());
	if (mp && mp->GetFilename().IsEmpty())
		return; // [case: 53405]

	if (g_FontSettings->GetCharHeight())
	{
		long first, last;
		GetVisibleLineRange(first, last);
		if ((DWORD)(last - first) > Psettings->mMaxScreenReparse)
			last = first + (long)Psettings->mMaxScreenReparse; // watch out for extreme zoom; cap
		const WTString curBuf(GetBuf());
		::ReparseScreen(mThis, curBuf, mp, (ULONG)first, (ULONG)last, underlineErrors, runAsync);
	}
	else
	{
#if defined(RAD_STUDIO)
		// #cppbTODO restore and address assert once further editor integration is needed
#else
		// [case: 28208] tried asserting on curBuf.IsEmpty() but we pad the
		// buffer with a bunch of newlines, so it was never REALLY empty
		_ASSERTE(g_FontSettings->GetCharHeight() && "is this an empty file?");
#endif
	}
}

WTString EdCnt::Scope(bool bParent /*= false*/, bool bSugestReaactoring /*= false*/)
{
	if (!Psettings || !Psettings->m_enableVA || m_ftype == Other)
		return NULLSTR;
	DEFTIMERNOTE(ScopeTimer, NULL);

	static BOOL shouldwait = TRUE;
	for (int loop = 0; shouldwait && g_threadCount && loop < 10; loop++)
	{
		if (1 == g_threadCount && gAutoReferenceHighlighter && gAutoReferenceHighlighter->IsActive())
			break; // don't wait for auto highlight thread

		if (1 == g_threadCount && g_ParserThread && !g_ParserThread->IsNormalJobActiveOrPending())
			break;

		// if this is the UI thread and if the other thread is doing a SendMessage
		// to the edit control, this loop blocks the other thread - it will be
		// unable to proceed until the UI thread gets back to the message pump.
		Sleep(10); // give small delay waiting for other thread to exit.
	}
	shouldwait = g_threadCount == 0; // if we timeout, don't pause on every call

	if (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending())
		LOG("Scope:Timeout");

	MultiParsePtr mp(GetParseDb());
	const WTString bb(GetBufConst());
	const CStringW mpFname(mp->GetFilename());
	if (mpFname.GetLength() == 0 || GlobalProject->IsBusy() ||
#if 0 // [case: 6797]
		(g_threadCount && !mInitialParseCompleted) ||
		(g_threadCount > 1 && mInitialParseCompleted) ||
#else
	    (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()) ||
#endif // _DEBUG
	    !bb.GetLength())
	{
		bool cantRunScope = true;
		if (1 == g_threadCount && !GlobalProject->IsBusy() && !mpFname.IsEmpty())
		{
			if (gAutoReferenceHighlighter && gAutoReferenceHighlighter->IsActive())
			{
				// don't wait for auto highlight thread if it is the only running thread
				cantRunScope = false;
			}
			else if (RefactoringActive::IsActive() && gVaService && gVaService->IsFindReferencesRunning())
			{
				// [case: 67606] ok while find refs is running
				cantRunScope = false;
			}
		}

		if (cantRunScope)
		{
			KillTimer(ID_TIMER_GETSCOPE);

			if (bb.GetLength() && !(g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()) &&
			    mpFname.IsEmpty() && !m_FileIsQuedForFullReparse && !m_FileHasTimerForFullReparseASAP)
			{
				// m_pmparse->m_fileName is empty, then no parse has occurred.
				// scope will never run until a parse has happened.
				// this can happen during ast alt+o in rename tests because parseThread
				// won't run local parse if g_currentEdCnt != thisEdCnt.
				const bool prevModState = modified_ever;
				QueForReparse();
				modified_ever = prevModState;
			}

			SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
			uint prevLastScopePos = m_lastScopePos;
			if (!SamePos(prevLastScopePos))
				m_isValidScope = FALSE;
			return WTString("String");
		}
	}
	LOG("Scope");

	if (IsCFile(m_ftype) || m_ftype == RC || m_ftype == Idl)
		gTypingDevLang = Src;
	else
		gTypingDevLang = m_ScopeLangType;

	if ((SamePos(m_lastScopePos) && (!bParent)) && m_isValidScope)
	{
		BOOL instr = m_lastScope.GetLength() && m_lastScope[0] != DB_SEP_CHR; // not in comments or strings
		if (instr)
		{
			if (m_lastScope == "String")
			{
				AutoLockCs l(mDataLock);
				SymDef.Empty();
				SymScope = m_lastScope;
			}
			return m_lastScope;
		}
		if (m_typing || !GetSymDtypeType())
		{
			// Fixes scope issues while typing.
			// Really needs to use logic VAParse::DoScope()
			WTString startsWith = m_typing ? WordLeftOfCursor() : CurWord();
			DType* cd = NULL;
			if (ISCSYM(startsWith[0]))
			{
				if (mp->m_xref)
					cd = mp->FindSym(&startsWith, NULL, &mp->m_xrefScope);
				else
					cd = mp->FindSym(&startsWith, &mp->m_lastScope, &mp->m_baseClassList);
			}

			UpdateSym(cd);
		}

		if (bSugestReaactoring)
		{
			// prevents double refactoring suggestion in which second one
			// causes further reduction of suggestion list
			static WTString sPreviousRefactoringScope;
			if (sPreviousRefactoringScope != m_lastScope)
			{
				if (SuggestRefactoring())
					sPreviousRefactoringScope = m_lastScope;
			}
			else
				sPreviousRefactoringScope.Empty();
		}

		if (g_ScreenAttrs.m_VATomatoTip)
		{
			::ScopeInfoPtr si = ScopeInfoPtr();
			if (Psettings->mAutoDisplayRefactoringButton && si->m_lastErrorPos &&
			    IsCFile(m_ftype) &&  // Too many false positives in C#/VB
			    (!m_ReparseScreen || // Screen is parsed
			     (!ISCSYM(m_cwd[0]) &&
			      si->m_lastErrorPos !=
			          GetBufIndex(bb, (long)CurPos(TRUE))))) // or (not currently typing the sym, and not at caret)
			{
				g_ScreenAttrs.m_VATomatoTip->DisplaySuggestionTip(mThis, si->m_lastErrorPos);
			}
			else
				g_ScreenAttrs.m_VATomatoTip->Dismiss();
		}

		return m_lastScope; // t.Str();
	}

	int p2 = GetBufIndex(bb, (long)CurPos(TRUE)); // use end of selection if ant, cause SamePos does...
	if (p2 && CharAt((uint)p2 - 1) == '/' && strchr("/*", CharAt((uint)p2)))
		p2++; // get the * or / of the // or /*

	HasSelection(); // sets m_hasSel

	if (!(CAN_USE_NEW_SCOPE() || m_ftype == Java || m_ftype == JS || m_ftype == RC || m_ftype == Idl))
	{
		// unsupported file,
		return NULLSTR;
	}

	const CStringW fname(FileName());
	if (mp->GetFilename() != fname)
	{
		ASSERT_ONCE(FALSE);
		mp->SetFilename(fname); // Make sure they are the same
	}
	if (g_currentEdCnt.get() == this) // make sure this is still the active window
	{
		const WTString buf(GetBufConst()); // make copy on cur thread
		m_lastScope = MPGetScope(buf, mp, p2);
		KillTimer(ID_TIMER_GETSCOPE);
		if (Psettings->mAutoHighlightRefs || Psettings->mSimpleWordMatchHighlights)
			SetTimer(ID_UNDERLINE_ERRORS, 200, nullptr);
		if (!m_lastScope.GetLength()) // Parse got interrupted, set timer to try again.
		{
			m_isValidScope = FALSE;
			m_lastScope = NULLSTR;
			UpdateSym(nullptr);
			SetTimer(ID_TIMER_GETSCOPE, 150, nullptr);
		}
		else
			m_isValidScope = TRUE;
	}

	if (bSugestReaactoring)
		SuggestRefactoring();

	return m_lastScope;
}

void EdCnt::Reparse()
{
	if (!Psettings || !Psettings->m_enableVA)
		return;
	g_rbuffer.Add(CurWord());
	m_typing = false;
	m_skipUnderlining = true;
	OnModified();
	/////////////////////////////////////////////////////
	Scope(TRUE);
	ReparseScreen(FALSE, FALSE); // Reparses screen
	WTString scope = Scope(true);
	m_reparseRange.x = m_reparseRange.y = 0;
	m_ReparseScreen = FALSE;
}

void EdCnt::ScrollTo(uint pos, bool toMiddle /*= true*/)
{
	vLog("EdSTo:");
	DEFTIMERNOTE(ScrollToTimer, NULL);
	if (pos != NPOS)
		SetPos(pos);
	ClearAllPopups();
}

// special handling for vs2005 function parameter annotations [case: 963]
// special handling for function argument types [case: 475]
// special handling for template argument types [case: 893]
WTString ReadToNextParam(const WTString& input, WTString& appendTo, bool leftParams)
{
	int parens = 0, i;
	for (i = 0; i < input.GetLength(); ++i)
	{
		const char curCh = input[i];
		if (',' == curCh)
		{
			if (i == 0)
				continue;
			if (!parens)
			{
				if (leftParams)
					appendTo += curCh; // append comma when reading left of the current arg
				break;
			}
		}
		else if ('(' == curCh || '<' == curCh)
		{
			if ('<' == curCh && input.GetTokCount('<') != input.GetTokCount('>'))
				; // [case: 96707]
			else
				parens++;
		}
		else if (')' == curCh || '>' == curCh)
		{
			if ('>' == curCh && input.GetTokCount('<') != input.GetTokCount('>'))
				; // [case: 96707]
			else if (!parens--)
				break;
		}

		appendTo += curCh;
	}

	return input.Mid(i);
}

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
// #RAD_ParamCompletion
void EdCnt::RsParamCompletion()
{
	MultiParsePtr mp(GetParseDb());
	if (mp && 
		gVaRadStudioPlugin && 
		gVaRadStudioPlugin->IsInParamCompletionOrIndexSelection())
	{
		TempAssign _tmp(m_typing, true);
		WTString buff = GetBuf(TRUE); // force re-getting
		gVaRadStudioPlugin->EditorFileChanged(FileName(), buff.c_str());
		MPGetScope(buff, mp, GetSelBegPos());

		// defined in RadStudioCompletion.cpp
		extern VaRSParamCompletionData sRSParamCompletionData;

		try
		{		
			sRSParamCompletionData.errorMessage.Empty();

			std::vector<WTString> best_params; // best match params
			auto best_def = CleanDefForDisplay(mp->m_argTemplate, gTypingDevLang);

			if (best_def.IsEmpty())
			{
				//VADEBUGPRINT("#PCR EMPTY DEF");

				if (gVaRadStudioPlugin->IsInParamIndexSelection())
				{
					sRSParamCompletionData.Clear();
					sRSParamCompletionData.activeSig = 0;
					sRSParamCompletionData.activeParam = 0;
					gVaRadStudioPlugin->ParameterCompletionResult(sRSParamCompletionData);
				}
				return;
			}

			VaRSParamsHelper::ParamsFromDef(best_def, best_params);

			// populate internal and corresponding RAD structures
			if (gVaRadStudioPlugin->IsInParamCompletion())
			{
				sRSParamCompletionData.Clear();
				sRSParamCompletionData.Populate();
			}
	
			if (sRSParamCompletionData.FindBestIndex(best_params, mp->m_argCount))
			{
				// invoke index selection result
				gVaRadStudioPlugin->ParameterCompletionResult(sRSParamCompletionData);
			}
		}
		catch (std::exception& ex)
		{
			auto error_text = ex.what();
			if (error_text)
			{
				sRSParamCompletionData.errorMessage = error_text;
				sRSParamCompletionData.activeSig = -1;
				sRSParamCompletionData.activeParam = -1;
				gVaRadStudioPlugin->ParameterCompletionResult(sRSParamCompletionData);
			}
		}
		catch (...)
		{
			sRSParamCompletionData.errorMessage = "Exception thrown in RsParamCompletion!";
			sRSParamCompletionData.activeSig = -1;
			sRSParamCompletionData.activeParam = -1;
			gVaRadStudioPlugin->ParameterCompletionResult(sRSParamCompletionData);
		}
	}
}
#endif

void EdCnt::DisplayToolTipArgs(bool show, bool keepSel /*=false*/)
{
	DEFTIMERNOTE(DisplayToolTipArgsTimer, NULL);
	if (!show)
		m_curArgToolTipArg.Empty();

	static BOOL sHandlingTooltipArgs = false;
	if (sHandlingTooltipArgs)
	{
		// build 1862 wer reports show stack overflow possibly related to
		// DisplayToolTipArgs and Focus
		return;
	}
	TempTrue recursionChk(sHandlingTooltipArgs);

	if (!m_ttParamInfo->GetSafeHwnd() || !m_ttParamInfo->IsWindowVisible())
	{
		if (!IsFeatureSupported(Feature_ParamTips, m_ScopeLangType))
		{
			if (show && gShellAttr->IsDevenv() && !HasVsNetPopup(FALSE))
			{
				if (((CS == m_ScopeLangType) && g_IdeSettings->GetEditorBoolOption("CSharp", "AutoListParams")) ||
				    (IsCFile(m_ScopeLangType) && Psettings->m_ParamInfo))
				{
					static DWORD sLastParamInfoPost = 0;
					static uint sLastPos = 0;

					const DWORD kCurTick = ::GetTickCount();
					const uint kCurPos = CurPos();

					// [case: 65634] endless attempts
					if (sLastParamInfoPost + 500 < kCurTick || sLastPos != kCurPos)
					{
						sLastParamInfoPost = kCurTick;
						sLastPos = kCurPos;
						// [case: 11767] get the IDE to display theirs
						::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_PARAMINFO, 0);
					}
				}
			}
			return;
		}
	}

	if (!m_hWnd)
		return;

	if (!Psettings || !Psettings->m_enableVA)
		return;

	if (show && !m_ttParamInfo)
		m_ttParamInfo = new ArgToolTip(this);

	// cache the template
	static WTString lastArgTemplate;
	static token processedArgTemplate;
	static int totalDefs = 0;
	static int lastSelDef = -1;
	MultiParsePtr mp(GetParseDb());
	ulong pos = (ulong)mp->m_argParenOffset;

	if (show && mp->m_inParamList && !mp->m_argCount)
	{
		WTString sym, scope = mp->m_argScope, type;
		token curscope = Scope(TRUE);
		curscope.ReplaceAll(".", ":");
		WTString curscopestr = curscope.Str();
		WTString meth = curscopestr.Mid(0, curscopestr.GetLength() - 1);
		WTString args = g_pGlobDic->GetArgLists(meth.c_str());
		const WTString bb(GetBufConst());
		int idx = GetBufIndex(bb, (long)CurPos());
		LPCSTR p = &bb.c_str()[idx];
		if (idx)
			idx--;

		while (idx && bb[idx] == ' ')
			idx--; // only do if last character was a '('

		if (bb[idx] == '(' && (args.GetLength() && (*p == ')' || *p == '\n' || *p == '\r')))
		{
			_ASSERTE(!"this path should not be hit or would be good to know how to repro this type of suggestion, see "
			          "case 3495 6/19/2014 6:51 PM and 6/19/2014 7:17 PM comments");

			WTString argstr;
			bool incomment = false;
			for (int i = 0, l = args.GetLength(); i < l; i++)
			{
				if (args[i] == '=')
				{
					argstr += "/* ";
					incomment = true;
				}
				if (incomment && (strchr(",\n\001\002", args[i])))
				{
					incomment = false;
					argstr += " */";
				}
				argstr += args[i];
			}
			if (incomment)
			{
				argstr += " */";
			}
			KillTimer(ID_TIMER_GETHINT);
			g_CompletionSet->ShowGueses(mThis, CompletionSetEntry(argstr, ET_SUGGEST_BITS), ET_SUGGEST_BITS);
			return;
		}

		// suggest foo(arglist /*=...*/) if w/in 2 chars of (
		// function definition parameter list suggestion
		if ((IsCFile(m_ftype)) && bb[idx] == '(' && (*p == ')' || *p == '\n' || *p == '\r'))
		{
			const WTString functionDef = mp->m_argTemplate;
			if (!functionDef.IsEmpty() && functionDef[0] == '#' && 0 == functionDef.Find("#define"))
			{
				// [case: 109347]
				// don't suggest definition parameter list for macros
				KillTimer(ID_TIMER_GETHINT);
				return;
			}

			if (!mp->m_argScope.IsEmpty())
			{
				DTypeList dts;
				if (mp->FindExactList(mp->m_argScope, dts))
				{
					for (const auto& d : dts)
					{
						if (d.IsVaStdAfx())
						{
							// don't suggest definition parameter list for vastdafx hints
							return;
						}
					}
				}
			}

			// this condition was added in change 4286 in 2003
#pragma warning(push)
#pragma warning(disable : 4127)
			if (1 || scope.GetLength() && (scope + DB_SEP_CHR) == curscope.c_str() &&
			             mp->m_argParenOffset >= (GetBufIndex(bb, (long)CurPos()) - 2))
			{
#pragma warning(pop)
				// member definition, hint = argetemplate
				// comment default args
				// arg def, hint "arg1, arg2 /* = val */"
				WTString argList;
				token t = functionDef;
				while (t.more())
				{
					WTString args2;
					// [case: 1248]
					// eat stuff before open paren
					(void)t.read("(");
					// read rest of current entry (multiple entries if there are overloads)
					WTString s = t.read("\f");
					if (s.GetLength() && s[0] == '(')
					{
						// eat the open paren
						s = s.Mid(1);
					}

					// eat the close paren
					int closeParenPos = s.ReverseFind(')');
					if (-1 != closeParenPos)
						s = s.Left(closeParenPos);

					if (s.GetLength() && s[0] == ' ' && s[s.GetLength() - 1] != ' ')
					{
						// fix for space before arglist in defs "foo( args)" but not for "foo( args )"
						s.TrimLeft();
					}

					// comment out default params
					bool incomment = false;
					int openParens = 0;
					for (int i = 0, l = s.GetLength(); i < l; i++)
					{
						if (s[i] == '=')
						{
							if (Psettings->mIncludeDefaultParameterValues)
								args2 += "/* ";
							else
								args2.TrimRight();
							incomment = true;
						}

						if (s[i] == '(')
							openParens++;

						if (s[i] == ')')
							openParens--;

						if (incomment && (s[i] == ',') && openParens == 0)
						{
							incomment = false;
							if (Psettings->mIncludeDefaultParameterValues)
								args2 += " */";
						}
						if (!incomment || Psettings->mIncludeDefaultParameterValues)
							args2 += s[i];
					}

					if (incomment && Psettings->mIncludeDefaultParameterValues)
					{
						if (args2.GetLength() && args2[args2.GetLength() - 1] == ' ')
							args2 += "*/ ";
						else
							args2 += " */";
					}

					// NOTE: ET_SUGGEST_BITS is used regardless of Psettings->m_defGuesses
					argList += CompletionSetEntry(args2, ET_SUGGEST_BITS);
				}

				KillTimer(ID_TIMER_GETHINT);
				// NOTE: ET_SUGGEST_BITS is used regardless of Psettings->m_defGuesses
				g_CompletionSet->ShowGueses(mThis, argList, ET_SUGGEST_BITS);
				return;
			}
		}
	}

	if (show)
	{
		if (HasVsNetPopup(FALSE))
			return;

		if (lastArgTemplate != mp->m_argTemplate)
		{
			processedArgTemplate = lastArgTemplate = mp->m_argTemplate;
			if (processedArgTemplate.length())
				totalDefs = m_ttParamInfo->BeautifyDefs(processedArgTemplate);
		}
	}

	if (!show || !processedArgTemplate.length())
	{
		m_tootipDef = 0;
		lastSelDef = -1;
		if (totalDefs)
		{
			totalDefs = 0;
			lastArgTemplate.Empty();
			processedArgTemplate = OWL_NULLSTR;
		}

		if (m_ttParamInfo && (m_ttParamInfo->m_currentDef || m_ttParamInfo->m_totalDefs))
		{
			if (keepSel)
				lastSelDef = m_ttParamInfo->m_currentDef;
			m_ttParamInfo->m_currentDef = m_ttParamInfo->m_totalDefs = 0;
			m_ttParamInfo->ShowWindow(SW_HIDE);
		}

		return;
	}

#define HMARGIN 3

	token t = processedArgTemplate;
	int narg = mp->m_argCount;
	// get the n'th def from the list
	WTString tdef = t.c_str();
	if (!m_tootipDef)
		m_tootipDef = 1;
	if (lastSelDef != -1)
	{
		m_tootipDef = lastSelDef;
		lastSelDef = -1;
	}

	int defToDisplay = 0;
	for (int ndef = m_tootipDef; t.more() > 2 && ndef; ndef--)
	{
		tdef = t.read("\f");
		defToDisplay++;
		// count comas
		for (int n = 0, p = 0; p < tdef.length(); p++)
			if (tdef[p] == ',')
				n++;
		/*	// this causes problems when using the up and down keys - infinite loop is possible
		        // if num of args us greater than matching def, move to next def
		        if(ndef == 1 && narg > n)
		            ndef ++;
		*/
	}

	t = tdef.c_str();
	// get arg "...(...," THISARG, "...);"
	WTString left = t.read("(");

	if (left.Find("#define") == -1) // [case: 22603] not for macros "#define foo(x, y) bar(y, x)"
	{
		const int posFirstOpenParen = tdef.Find('(');
		if (-1 != posFirstOpenParen)
		{
			const int posFirstCloseParen = tdef.Find(')');
			if (-1 != posFirstCloseParen)
			{
				const int posFirstDoubleColonAfterCloseParen = tdef.Find("::", posFirstCloseParen + 1);
				// case=20664
				int posColonAfterCloseParen = tdef.Find(":", posFirstCloseParen + 1);
				if (-1 != posFirstDoubleColonAfterCloseParen &&
				    posColonAfterCloseParen == posFirstDoubleColonAfterCloseParen)
				{
					// case=64310
					posColonAfterCloseParen = tdef.Find(":", posFirstDoubleColonAfterCloseParen + 2);
				}
				const int posOpenParenAfterCloseParen = tdef.Find("(", posFirstCloseParen + 1);
				if (-1 != posOpenParenAfterCloseParen &&
				    (-1 == posColonAfterCloseParen || posColonAfterCloseParen > posOpenParenAfterCloseParen))
				{
					const int posSecondOpenParen = tdef.Find("(", posFirstOpenParen + 1);
					const int declPos = tdef.Find("decltype", posFirstOpenParen + 1);
					const int attrPos = tdef.Find("__attribute__", posFirstOpenParen + 1);
					// [case: 80012]
					// don't be fooled by decltype and __attribute__
					//		void Foo(params) -> decltype(stuff)
					//		void Foo(params) __attribute__((stuff))
					if (posSecondOpenParen == posOpenParenAfterCloseParen &&
					    (declPos == -1 || declPos > posSecondOpenParen) &&
					    (attrPos == -1 || attrPos > posSecondOpenParen))
					{
						// [case: 3727] watch out for parens in return type
						// STDMETHD(Function)(params);
						// SOMEAPI(type) function(params);
						if (t.more())
						{
							left += t.str[0];
							t.str = t.str.substr(1);
						}
						left += t.read("(");
					}
				}

				// [case: 96707]
				WTString funcName(mp->m_ParentScopeStr);
				int openPos = funcName.Find('(');
				if (-1 != openPos)
					funcName = funcName.Left(openPos);

				for (int idx = 0; idx < 5 && t.more(); ++idx)
				{
					if (-1 != left.Find(funcName))
						break;
					if (-1 == WTString(t.str).Find(funcName))
						break;

					left += t.str[0];
					t.str = t.str.substr(1);
					left += t.read("(");
				}
			}
		}
	}

	left.TrimLeft();
	if (t.more())
	{
		left += t.str[0];
		t.str = t.str.substr(1);
	}
	while ((narg > 0) && t.more())
	{
		narg--;
		int p = t.Str().find("...");
		if (p == NPOS || p > 2) // if next arg != "[<space>]..."
			t = ::ReadToNextParam(t.Str(), left, true);
	}

	WTString carg;
	if (t.Str().length() && t.Str()[0] != ')')
	{
		t = ::ReadToNextParam(t.Str(), carg, false);
		m_curArgToolTipArg = carg;
	}
	WTString right = t.read("\f");
	right.TrimRight();

	CPoint cPos;
	// lineup with opening paren
	cPos = GetCharPos((long)pos);
	if (cPos.x < 0)
		cPos.x = 0;
	// under cursor
	cPos.y = vGetCaretPos().y;
	vClientToScreen(&cPos);
	cPos.x += HMARGIN + 2;
	cPos.y += g_FontSettings->GetCharHeight();

	static WTString lstr;
	static CPoint lpt;
	lpt = cPos;
	lstr = left;
	if (Psettings->m_AutoComments)
	{
		// comments for arg info
		WTString cmnt = GetCommentForSym(mp->m_argScope, 6);
		if (cmnt.GetLength())
			right += "\n" + cmnt;
	}

	m_ttParamInfo->UseVsEditorTheme();
	m_ttParamInfo->Display(&cPos, left.c_str(), carg.c_str(), right.c_str(), totalDefs, defToDisplay);

	// [case: 16970] see if VS put up a tooltip after us
	SetTimer(ID_ARGTEMPLATE_CHECKVS_TIMER, 50, NULL);
	// [case: 75087] sometimes they take a long time to display; add second timer
	SetTimer(ID_ARGTEMPLATE_CHECKVS_TIMER2, 600, NULL);

	vSetFocus();
	return;
}

BOOL EdCnt::PreTranslateMessage(MSG* pMsg)
{
	return CTer::PreTranslateMessage(pMsg);
}

void EdCnt::GotoNextMethodInFile(BOOL next)
{
	// Call VAParse's FindNextScopePos
	const WTString curBuf(GetBuf());
	int spos = ::FindNextScopePos(m_ftype, curBuf, GetBufIndex(curBuf, (long)CurPos()), (ULONG)CurLine(), next);
	::DelayFileOpen(FileName(), LineFromChar(spos));
	return;
}

#include "vsshell110.h"

CComPtr<IVsUIShellOpenDocument> GetVsShellOpenDocument()
{
	CComQIPtr<IVsUIShellOpenDocument> vsSod;
	if (!gPkgServiceProvider)
		return vsSod;

	IUnknown* tmp = NULL;
	gPkgServiceProvider->QueryService(SID_SVsUIShellOpenDocument, IID_IVsUIShellOpenDocument, (void**)&tmp);
	if (!tmp)
		return vsSod;

	vsSod = tmp;
	return vsSod;
}

// Use DelayFileOpen, not VsOpen.
// This is only functional in VS2012+ and is only for certain circumstances.
// DelayFileOpen will use VsOpen as appropriate.
static BOOL VsOpen(const CStringW& filename, BOOL preview)
{
	_ASSERTE(gShellAttr->IsDevenv11OrHigher());
	CComPtr<IVsUIShellOpenDocument> openDoc(GetVsShellOpenDocument());
	if (!openDoc)
		return false;

	CComPtr<IVsNewDocumentStateContext> ndsc;
	if (preview && gShellAttr->IsDevenv11OrHigher() && Psettings->mUsePreviewTab)
	{
		CComQIPtr<IVsUIShellOpenDocument3> openDoc3;
		openDoc3 = openDoc;
		if (openDoc3)
		{
			// [case: 61829] dev11 preview tab
			GUID vaPkg = {0x44630d46, 0x96b5, 0x488c, {0x8d, 0xf9, 0x26, 0xe2, 0x1d, 0xb8, 0xc1, 0xa3}};
			openDoc3->SetNewDocumentState(NDS_Provisional, vaPkg, &ndsc);
		}
	}

	const GUID logicalView = LOGVIEWID_TextView;
	CComPtr<IServiceProvider> serviceProvider;
	CComPtr<IVsUIHierarchy> hierarchy;
	VSITEMID itemid;
	CComPtr<IVsWindowFrame> frame;
	CComBSTR bFname(filename);
	BOOL res = FALSE;

	// see also openDoc->OpenDocumentViaProjectWithSpecific() and VSSPECIFICEDITORFLAGS
	// see also GUID_VsBufferEncodingPromptOnLoad
	//		https://social.msdn.microsoft.com/Forums/en-US/cc18b75d-430b-46f0-9176-0070bf0b0b5f/opening-editor-with-particular-code-page
	if (SUCCEEDED(openDoc->OpenDocumentViaProject(bFname, logicalView, &serviceProvider, &hierarchy, &itemid, &frame)))
	{
		// [case: 95062]
		if (frame)
			frame->Show();
		res = TRUE;
	}

	if (ndsc)
		ndsc->Restore();

	return res;
}

volatile LONG gFileOpenIdx = 0;

EdCntPtr DelayFileOpenPos(const CStringW& file, UINT pos, UINT endPos /*= -1*/, BOOL preview /*= FALSE*/)
{
	EdCntPtr ed = DelayFileOpen(file, 0, NULL, preview);
	if (ed)
	{
		if (endPos == -1)
			ed->SetPos(pos);
		else
			ed->SetSel((long)pos, (long)endPos);
	}
	return ed;
}

EdCntPtr DelayFileOpen(const CStringW& file, int ln /*= 0*/, LPCSTR sym /*= NULL*/, BOOL preview, /*= FALSE*/
                       BOOL func /*= FALSE*/)
{
	vCatLog("Editor.Events", "VaEventED Navigate  ln=%d, sym='%s', %s", ln, sym, (LPCSTR)CString(GetBaseName(file)));
	IncrementOnExit inc(gFileOpenIdx);
	static CStringW s_file;
	s_file = NormalizeFilepath(file);

	const EdCntPtr prevEd = g_currentEdCnt;
	CWnd* focWnd = CWnd::GetFocus();
	if (g_CompletionSet)
		g_CompletionSet->Dismiss(); // [case: 62225]
	if (prevEd)
		NavAdd(prevEd->FileName(), prevEd->CurPos());

	// [case: 22548] clear code def window since it messes up the DTE selection that we grab from the active document
	gShellSvc->ClearCodeDefinitionWindow();

	bool useDteActivate = false;
	bool useNew = gShellAttr->IsDevenv11OrHigher() && Psettings->mUseNewFileOpen;
	if (prevEd && gShellAttr->IsDevenv10OrHigher())
	{
		if (!file.CompareNoCase(prevEd->FileName()) || !s_file.CompareNoCase(prevEd->FileName()))
		{
			// if file already active, don't use new file open method
			useNew = false;
			useDteActivate = true;
		}
	}

	extern BOOL g_ignoreNav;
	g_ignoreNav = TRUE; // Prevent the HTML editor from causing a NavAdd here. case=21074
	if (useNew && VsOpen(s_file, preview))
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_FILEOPENED, (WPARAM)ln, (LPARAM)LPCWSTR(s_file));
	else if (useDteActivate)
	{
		// [case: 61729]
		SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR) "Window.ActivateDocumentWindow", 0);
		SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_FILEOPENED, (WPARAM)ln, (LPARAM)LPCWSTR(s_file));
	}
#if defined(RAD_STUDIO)
	else
		SendVamMessageToCurEd(WM_VA_FILEOPENW, (WPARAM)ln, (LPARAM)LPCWSTR(s_file));
#else
	else
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_FILEOPENW, (WPARAM)ln, (LPARAM)LPCWSTR(s_file));
#endif
	g_ignoreNav = FALSE;

	vCatLog("Editor.Events", "DFileOpen: %s (%d) dte(%d)", (LPCTSTR)CString(s_file), ln, useDteActivate);
	focWnd = CWnd::GetFocus();
	if (focWnd && focWnd->GetSafeHwnd() != g_currentEdCnt->GetSafeHwnd())
	{
		EdCntPtr prevEd2 = g_currentEdCnt;
		if (!g_currentEdCnt || (g_currentEdCnt->FileName() != file && g_currentEdCnt->FileName() != s_file))
		{
			g_currentEdCnt = GetOpenEditWnd(s_file);

			if (!g_currentEdCnt && prevEd2)
			{
				if (prevEd2->CheckForSaveAsOrRecycling())
					g_currentEdCnt = prevEd2;
			}
		}
	}

	if (g_currentEdCnt && 0 != g_currentEdCnt->FileName().CompareNoCase(file) &&
	    0 != g_currentEdCnt->FileName().CompareNoCase(s_file) && RefactoringActive::IsActive())
	{
		const int kCurRef = RefactoringActive::GetCurrentRefactoring();
		if (VARef_RenameFilesFromMenuCmd == kCurRef || VARef_RenameFilesFromRefactorTip == kCurRef)
		{
			g_currentEdCnt->CheckForSaveAsOrRecycling();
		}
	}

	vCatLog("Editor.Events", "%sDFileOpen: f(%p) c(%p) p(%p)", focWnd ? "" : "ERROR: ", focWnd, g_currentEdCnt.get(), prevEd.get());

#if defined(RAD_STUDIO)
	if (g_currentEdCnt)
		g_currentEdCnt->CheckForSaveAsOrRecycling();
#endif

	// bug 3549 - refactoring did not work if the file names had different case
	// so allow for entire path, not just drive letter having different case
	if (g_currentEdCnt &&
	    (0 == g_currentEdCnt->FileName().CompareNoCase(file) || 0 == g_currentEdCnt->FileName().CompareNoCase(s_file)))
	{
		g_currentEdCnt->SetTimer(WM_SETFOCUS, 100, NULL);
		g_currentEdCnt->UpdateWindow();
		g_currentEdCnt->SetTimer(ID_GETBUFFER, 500, NULL);

		// Need to GetBuf before LinPos will work
		WTString bb(g_currentEdCnt->GetBuf(TRUE));
		if (ln)
		{
			long p = (long)g_currentEdCnt->LinePos(ln);
			g_currentEdCnt->SetSel(p, p);
			//			vLog("sean: DFileOpen: pos (%x) --> (%x)", p, g_currentEdCnt->CurPos());
			if (gShellAttr->IsMsdev())
			{
				// bug 1531 vc6 bug when EOF does not end in blank line
				// workaround is to move caret to end of last line in file instead
				// of leaving it at the start of the last line
				long actualPos;
				g_currentEdCnt->GetSel(actualPos, actualPos);
				if (actualPos != p && TERCOL(p) == TERCOL(actualPos) && ((long)TERROW(p) > (long)TERROW(actualPos)))
				{
					::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_END, 0);
				}
			}
		}
		if (g_currentEdCnt)
			NavAdd(g_currentEdCnt->FileName(), g_currentEdCnt->CurPos());

		if (g_currentEdCnt && sym && *sym)
		{
			// Select symbol on gotoDef... case=31463
			// TODO: use VAParse to determine symbol pos for multi-line definitions.
			long lnOffset = g_currentEdCnt->GetBufIndex(bb, (long)g_currentEdCnt->LinePos(ln));
			WTString lnText = g_currentEdCnt->GetSubString((ulong)lnOffset, g_currentEdCnt->LinePos(ln + 1));
			LPCSTR symPtr = strstrWholeWord(lnText, sym);

			if (symPtr)
			{
				if (func)
				{
					LPCSTR symPtr2 = strstrWholeWord(symPtr + 1, sym);
					if (symPtr2)
						symPtr = symPtr2;
				}
				long offset = ptr_sub__int(symPtr, lnText.c_str());
				LPCSTR decltypeHack = strstrWholeWord(lnText, "decltype");
				if (decltypeHack && decltypeHack < symPtr)
				{
					// [case: 93387]
					LPCSTR symPtr2 = strstrWholeWord(&lnText.c_str()[offset + strlen(sym)], sym);
					if (symPtr2)
						offset = ptr_sub__int(symPtr2, lnText.c_str());
				}

				g_currentEdCnt->SetSel((long)(lnOffset + offset), (long)(lnOffset + offset + strlen(sym)));
			}
		}

		if (g_currentEdCnt)
		{
			// Don't call scope directly, or it may throw off some refactorings. case=50344
			g_currentEdCnt->KillTimer(ID_TIMER_GETSCOPE);
			g_currentEdCnt->SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
		}
		return g_currentEdCnt;
	}

	// this is definitely not good
	vLogUnfiltered("ERROR: DFileOpen: failed to locate window %p %p", focWnd, g_currentEdCnt.get());

#ifdef _DEBUG
	int ftype = GetFileType(file, false, true);
	if (!(Is_Tag_Based(ftype) || JS == ftype))
	{
		// OFIS can list files to which we don't attach but do open
		if (!::IsRestrictedFileType(ftype))
		{
			_ASSERTE(!"DelayFileOpen: failed to locate window");
		}
	}
#endif // _DEBUG

	return NULL;
}

// [case: 112737] added support for column (actually char offset from start of line)
EdCntPtr DelayFileOpenLineAndChar(const CStringW& file, int ln /*= 0*/, int cl /*= 1*/, LPCSTR sym /*= NULL*/,
                                  BOOL preview /*= FALSE*/)
{
	EdCntPtr edCntPtr = DelayFileOpen(file, ln, sym, preview);

	// scoot the caret along to match the given column
	if (edCntPtr && cl > 1)
	{
		long pos;
		edCntPtr->GetSel(pos, pos);
		pos = TERRCTOLONG((long)TERROW(pos), (long)cl);
		edCntPtr->SetSel(pos, pos);
	}

	return edCntPtr;
}

BOOL EdCnt::SubclassWindow(HWND hWnd)
{
	if (!Attach(hWnd))
		return FALSE;

	// allow any other subclassing to occur
	PreSubclassWindow();

	// now hook into the AFX WndProc
	WNDPROC* lplpfn = CWnd::GetSuperWndProcAddr();
	WNDPROC oldWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)AfxGetAfxWndProc());
	ASSERT(oldWndProc != (WNDPROC)AfxGetAfxWndProc());

	// resharper hack: Force subclass so we both subclass it.
	//	if (*lplpfn == NULL)
	*lplpfn = oldWndProc; // the first control of that type created
	                      //#ifdef _DEBUG
	                      //	else if (*lplpfn != oldWndProc)
	                      //	{
	                      //		TRACE0("Error: Trying to use SubclassWindow with incorrect CWnd\n");
	                      //		TRACE0("\tderived class.\n");
	                      //		TRACE3("\thWnd = $%04X (nIDC=$%04X) is not a %hs.\n", (UINT)hWnd,
	                      //			_AfxGetDlgCtrlID(hWnd), GetRuntimeClass()->m_lpszClassName);
	                      //		ASSERT(FALSE);
	                      //		// undo the subclassing if continuing after assert
	                      //		::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
	                      //	}
	                      //#endif

	return TRUE;
}

bool EdCnt::SamePos(uint& p)
{
	const WTString bb(GetBufConst());
	long cp = GetBufIndex(bb, (long)CurPos());
	if (cp && wt_isspace(bb[(uint)cp - 1]))
	{
		// goto beginning of space
		while (cp && wt_isspace(bb[(uint)cp - 1]))
			cp--;
	}
	else if (cp && ISCSYM(bb[(uint)cp - 1]))
	{
		// goto beginning of symbol
		while (cp && ISCSYM(bb[(uint)cp - 1]))
			cp--;
	}
	if (p == (uint)cp)
		return TRUE;
	p = (uint)cp;
	return FALSE;
}

long EdCnt::GetBegWordIdxPos()
{
	const WTString bb(GetBufConst());
	long cp = GetBufIndex(bb, (long)CurPos());
	while (cp && ISCSYM(bb[(uint)cp - 1]))
		cp--;
	return cp;
}

FindReferencesPtr s_refs;

LRESULT EdCnt::FindNextReference(BOOL next /*= TRUE*/)
{
	if (!s_refs)
	{
		s_refs = std::make_shared<FindReferences>(RESWORD);
		s_refs->flags = FREF_Flg_Reference; // do not find comments
	}

	static int lmodCookie = 0;
	static WTString findSym;
	const WTString ss(GetSymScope());
	if (ss.length() && ss[0] == DB_SEP_CHR && findSym != ss)
	{
		// search this symbol
		findSym = ss;
		lmodCookie = 0;
	}
	if (lmodCookie != m_modCookie)
	{
		CWaitCursor cur;
		s_refs->Init(findSym, RESWORD);
		WTString bb(GetBufConst());
		s_refs->SearchFile(CStringW(), FileName(), &bb);
		lmodCookie = m_modCookie;
	}
	int count = (int)s_refs->Count();
	if (count)
	{
		long cpos;
		SendMessage(WM_VA_THREAD_GETBUFINDEX, (WPARAM)-1, (LPARAM)&cpos);

		WTString text;
		CStringW file;
		long npos;
		if (next)
		{
			for (int i = 0; i < count; i++)
			{
				s_refs->GetFileLine(i, file, npos, text);
				if (cpos < GetBufIndex(npos))
				{
					s_refs->GotoReference(i);
					return TRUE;
				}
			}
			s_refs->GotoReference(0);
			return TRUE;
		}
		else
		{
			for (int i = count - 1; i >= 0; i--)
			{
				s_refs->GetFileLine(i, file, npos, text);
				if (cpos > (GetBufIndex(npos) + s_refs->GetFindSym().GetLength()))
				{
					s_refs->GotoReference(i);
					return TRUE;
				}
			}
			s_refs->GotoReference(count - 1);
			return TRUE;
		}
	}
	return FALSE;
}

WTString EdCnt::GetBuf(BOOL force /*= FALSE*/)
{
	if (gShellAttr->IsMsdev())
	{
		extern BOOL g_DisplayFrozen;
		if (m_preventGetBuf || (!g_DisplayFrozen && !HasFocus())) // don't do when rename is active. case=25190
		{
			const WTString bb(GetBufConst());
			if (m_preventGetBuf) // VC6 hack only
				return bb;
			// Causes scrolling if we do not have focus in VC6
			if (bb.GetLength())
				return bb; // Just return m_buf and leave m_bufState dirty
		}
	}
#ifdef _DEBUGxxxx // without an assert, we'll never know if this works
	if (force && m_bufState != BUF_STATE_WRONG)
	{
		// prevent threading asserts in vc6
		if (gShellAttr->IsMsdev() && g_mainThread != GetCurrentThreadId())
			return CTer::GetBuf(force);

		// In this case both CurWord and StartsWith() should be the same before and after
		WTString startsWith1 = WordLeftOfCursor();
		WTString lbuf = CTer::GetBuf();
		INT pos = GetBufIndex(lbuf, CurPos());
		LPCSTR lp = lbuf.c_str();
		WTString cwd1 = CurWord();
		WTString nbuf = CTer::GetBuf(TRUE); // make sure it is up to date
		WTString startsWith2 = WordLeftOfCursor();
		WTString cwd2 = CurWord();
		if (cwd1 != cwd2 || startsWith1 != startsWith2)
		{
			// Once we fix all issues that land us here, we can change some of the GetBuf(TRUE)'s to FALSE to increase
			// performance
			Log("EdCnt::GetBuf() incorrect bufState.");
		}
		return GetBufConst();
	}
#endif // _DEBUG
	return CTer::GetBuf(force);
}

void EdCnt::QueForReparse()
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);
	if (!g_ParserThread)
		return;

	KillTimer(ID_DOC_MODIFIED_TIMER);

	const CStringW fname(FileName());
	WTString buf;
	if (gVaShellService || (gShellAttr && gShellAttr->IsCppBuilder()))
	{
		if (BUF_STATE_CLEAN != m_bufState)
		{
			// We need to call GetFileText because GetBuf() fails if it does not have focus.
			// m_buf will be missing trailing \r\n's, but since the state is dirty, it will call GetBuf() when it
			// regains focus.
			CStringW wbuf(::PkgGetFileTextW(fname));
			buf = WideToMbcs(wbuf, wbuf.GetLength());
			if (!buf.IsEmpty())
				UpdateBuf(buf + kExtraBlankLines);
		}
		if (buf.IsEmpty())
			buf = GetBuf(TRUE); // should GetFileText ever fail? Yes, if not called on the UI thread
	}
	else // VC6
	{
		if (m_bufState == BUF_STATE_CLEAN)
			buf = GetBuf(); // No need to re-get buffer, cuts back on flicker caused by TerNoScroll
		else
			buf = GetBuf(TRUE);
	}

	m_FileIsQuedForFullReparse = TRUE;
	m_FileHasTimerForFullReparseASAP = FALSE;
	modified_ever = true;
	m_ReparseScreen = FALSE;
	s_LastReparseFile.Empty();
	g_ParserThread->QueueFile(fname, buf, mThis);
	SaveBackup();
	KillTimer(ID_TIMER_RELOAD_LOCAL_DB);
}

void EdCnt::OnModified(BOOL startASAP)
{
	// 	if(time <= 100)
	// 		m_FileIsQuedForReparse = TRUE; // or we will not reparse screen as user types
	if (!m_FileHasTimerForFullReparse)
	{
		m_FileHasTimerForFullReparse = true;
		// nuke all underlines
		Invalidate(TRUE);
		UpdateWindow();
	}
	mruFlag = TRUE;
	m_modCookie++;
	// 	m_lastScopePos = 0; // causes scope to happen too often while typing a symbol
	modified_ever = true;
	if (startASAP)
		m_FileHasTimerForFullReparseASAP = TRUE;         // preserve asap status
	m_ReparseScreen = !m_FileHasTimerForFullReparseASAP; // don't reparse screen if we are just about to que the file
	SetReparseTimer();

	extern void CleanupSmartSelectStatics();
	CleanupSmartSelectStatics();
}

void EdCnt::SetReparseTimer()
{
	KillTimer(ID_DOC_MODIFIED_TIMER);
	if (m_FileHasTimerForFullReparseASAP)
		SetTimer(ID_DOC_MODIFIED_TIMER, 100, NULL);
	else
	{
		const WTString bb(GetBufConst());
		if (bb.GetLength() > (500 * 1024)) // longer delay for files over 500K
			SetTimer(ID_DOC_MODIFIED_TIMER, 3000, NULL);
		else
			SetTimer(ID_DOC_MODIFIED_TIMER, 1000, NULL);
	}
}

LRESULT EdCnt::OnPostponedSetFocus(WPARAM wParam, LPARAM lParam)
{
	vSetFocus();
	return 0;
}

// [case: 31699]
// This isn't as robust as it could be.  It only detects line comments and
// block comments that end on the current line.  It could potentially
// sniff more of the buffer.
static bool IsInComment(LPCSTR startOfBuffer, LPCSTR p)
{
	LPCSTR sol;
	LPCSTR eol;

	for (sol = p; sol > startOfBuffer && !strchr("\r\n", sol[-1]); --sol)
		;
	for (eol = p; *eol && !strchr("\r\n", eol[-1]); ++eol)
		;
	WTString lineText(sol, ptr_sub__int(eol, sol));

	int pIdx = ptr_sub__int(p, sol);
	int idx;
	idx = lineText.Find("//");
	if (idx >= 0 && idx < pIdx)
		return true;
	idx = lineText.Find("*/", pIdx);
	if (idx >= 0)
	{
		idx = lineText.Find("/*", idx);
		if (idx < 0)
			return true;
	}
	return false;
}

LPCSTR strstrn(LPCSTR sBeg, LPCSTR sFind, BOOL caseMatch = TRUE, int len = 5000);

WTString EdCnt::GetSurroundText()
{
	Log("EDC::CGWH:D4");
	WTString recentGuess;
	WTString bcl;
	const WTString bb(GetBufConst());
	const LPCSTR sp = bb.c_str();
	const LPCSTR p = &sp[(uint)GetBufIndex(bb, (long)CurPos())];
	LPCSTR bp = p;
	const LPCSTR ep = p;
	if ((*p != ')' && *p == '\n'))
		return NULLSTR; // don't suggest when modifying in the middle of a line

	// first look for a line match
	for (bp = ep; bp > sp && !strchr("\r\n", bp[-1]); bp--)
		;
	for (; bp < ep && wt_isspace(*bp); bp++)
		;

	WTString str(bp, ptr_sub__int(ep, bp));

	// look for similar pattern...
	if (EdPeekMessage(m_hWnd))
		return NULLSTR;
	extern LPCSTR strrstr(LPCSTR sBeg, LPCSTR sEnd, LPCSTR sFind, BOOL matchCase);
	LPCSTR p1 = strrstr(sp, bp, str.c_str(), TRUE); // look up
	if (!p1)
	{
		p1 = strstrn(bp, str.c_str(), TRUE, 1000); // look down
		if (!p1)
		{
			// look for foo.bar
			for (bp = ep; bp > sp && !strchr(" \t", bp[-1]); bp--)
				;
			WTString nstr(bp, ptr_sub__int(ep, bp));
			if (nstr != str)
			{
				p1 = strrstr(sp, bp, str.c_str(), TRUE); // look up
				if (!p1)
					p1 = strstrn(bp, nstr.c_str(), TRUE, 1000); // look down
				if (p1 && !strchr("[-.", *p1))
					p1 = NULL; // only suggest  ->bar, .bar, or foo[]
			}
		}
	}

	if (!p1 || IsInComment(sp, p1))
		return NULLSTR;

	MultiParsePtr mp(GetParseDb());
	Log("EDC::CGWH:D5");
	p1 += str.GetLength();
	LPCSTR p2;
	for (p2 = p1; *p2 && strchr(" \t\".-+;=!*^)/<>&", *p2); p2++)
		;
	for (; *p2 && !strchr(" \t\r\n'\".-+;=!*^)/<>&", *p2); p2++)
	{
		if (*p2 == ',' && mp->m_inParenCount)
		{ // only suggest commas if in parens
			p2++;
			if (*p2 == ' ')
				p2++;
			break;
		}
		if (*p2 == '(')
		{
			p2++;
			if (*p2 == ')')
				p2++;
			break;
		}
	}

	if ((p2 - p1) > 1)
	{
		LPCSTR p3;
		for (p3 = p1; *p3 && ISCSYM(*p3); p3++)
			;

		if (p3 == p1)
			recentGuess = CompletionSetEntry(WTString(p1, ptr_sub__int(p2, p1)), ET_SUGGEST_BITS);
		else
		{
			WTString wd(p1, ptr_sub__int(p3, p1));
			if (mp->FindSym(&wd, m_lastScope.GetLength() ? &m_lastScope : NULL, &bcl))
				recentGuess = CompletionSetEntry(WTString(p1, ptr_sub__int(p2, p1)), ET_SUGGEST_BITS);
		}
	}

	return recentGuess;
}

bool EdCnt::SuggestRefactoring()
{
	// [case: 98715]
	if (!Psettings || !Psettings->mIncludeRefactoringInListboxes || !Psettings->m_autoSuggest || gExpSession != nullptr)
		return false;

	bool didSuggest = false;
	WTString cwd = CurWord();
	WTString sym = GetSymScope();
	MultiParsePtr mp(GetParseDb());
	if (cwd != StrGetSym(sym) && ISCSYM(cwd[0]))
	{
		WTString bcl;
		DType* cd = mp->FindSym(&cwd, &m_lastScope, &bcl);
		if (cd)
			sym = cd->SymScope();
	}
	if (Is_Tag_Based(m_ScopeLangType))
		return false;
	if (Is_Tag_Based(m_ftype) && m_ScopeLangType == VBS) // Allow in VB files, not VBS (needs splitting)
		return false;
	if (Psettings->UsingResharperSuggestions(m_ftype, false))
		return false;

	//////////////////////////////////////////////////////////////////////////
	// Suggest to add using statement

	// We need to get namespace issues down before we can accurately suggest to add imports in VB
	const DType dt(GetSymDtype());
	if ((m_ftype == CS /*|| m_ftype == VB*/) && m_FileHasTimerForFullReparse && sym.GetLength() > 2 && dt.type() &&
	    !dt.infile() && !mp->m_xref && ISCSYM(cwd[0]))
	{
		WTString pwd = CurWord(-1);
		if (strchr(" \t\r\n", pwd[0]))
		{
			WTString ns = StrGetSymScope(sym);
			if (ns.GetLength() && !mp->GetGlobalNameSpaceString().contains(ns + '\f') && m_lastScope.Find(ns) != 0)
			{
				DType* nsData = mp->FindExact(ns);
				if (nsData && nsData->MaskedType() == NAMESPACE)
				{
					// [case: 87245] don't suggest using for namespace in cpp file
					const int ftype = ::GetFileType(nsData->FilePath());
					if (!(IsCFile(ftype)))
					{
						if (!g_CompletionSet->IsVACompletionSetEx())
							g_CompletionSet->Dismiss(); // Clear any suggestions
						if (Is_VB_VBS_File(m_ftype))
							g_CompletionSet->ShowGueses(
							    mThis,
							    CompletionSetEntry(WTString("Imports ") + CleanScopeForDisplay(ns) +
							                           AUTOTEXT_SHORTCUT_SEPARATOR + CurWord(),
							                       IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_INSERT_USING_STATEMENT)),
							    ICONIDX_REFACTOR_INSERT_USING_STATEMENT);
						else
							g_CompletionSet->ShowGueses(
							    mThis,
							    CompletionSetEntry(WTString("using ") + CleanScopeForDisplay(ns) + ";" +
							                           AUTOTEXT_SHORTCUT_SEPARATOR + CurWord(),
							                       IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_INSERT_USING_STATEMENT)),
							    ICONIDX_REFACTOR_INSERT_USING_STATEMENT);
						didSuggest = true;
					}
				}
			}
		}
	}
	//////////////////////////////////////////////////////////////////////////
	// Suggest to rename references
	long bwPos = GetBegWordIdxPos();
	BOOL samePos = m_lastEditPos == (ULONG)bwPos;
	if (!samePos)
		m_hasRenameSug = FALSE;
	if (!samePos && ISCSYM(CurWord()[0]))
	{
		// caret on new word, save SymScope and Pos
		m_lastEditPos = (ULONG)bwPos;
		m_lastEditSymScope = sym;
	}
	else
	{
		// Display rename suggestion if changed and isdef
		if (!m_hasRenameSug && m_lastEditSymScope.GetLength() && ISCSYM(m_cwd[0]) &&
		    m_cwd != StrGetSym(m_lastEditSymScope) && mp->m_isDef && m_typing)
		{
			const WTString bb(GetBufConst());
			const auto idx = (uint)GetBufIndex(bb, (long)CurPos());
			char nc = bb[idx];
			if (nc != '\n' && nc != '\r') // don't suggest rename while typing in a new definition
			{
				if (!mp->LDictionary()->HasMultipleDefines(m_lastEditSymScope.c_str()))
				{
					// [case: 95272]
					// save old state
					const int kLastEditPos = (int)m_lastEditPos;
					const WTString kLastEditSymScope(m_lastEditSymScope);

					// Clear any suggestions
					g_CompletionSet->Dismiss();

					// restore state potentially modified by call to Dismiss
					m_lastEditPos = (ULONG)kLastEditPos;
					m_lastEditSymScope = kLastEditSymScope;
					m_hasRenameSug = TRUE;

					g_CompletionSet->ShowGueses(
					    mThis,
					    CompletionSetEntry(WTString("Rename references"), IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME)),
					    ET_SUGGEST);
					g_CompletionSet->ShowGueses(mThis,
					                            CompletionSetEntry(WTString("Rename references with preview..."),
					                                               IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME)),
					                            ET_SUGGEST);
					didSuggest = true;
				}
			}
		}
	}
	//////////////////////////////////////////////////////////////////////////

	return didSuggest;
}

WTString EdCnt::IvsGetLineText(int line)
{
	_ASSERTE("IvsGetLineText needs m_IVsTextView" && m_IVsTextView);
	WTString text;
	CComPtr<IVsTextLines> lines;
	if (m_IVsTextView && m_IVsTextView->GetBuffer(&lines) == S_OK && lines)
	{
		LINEDATA linedata;
		if (lines->GetLineData(line - 1, &linedata, NULL) == S_OK)
		{
			text = CStringW(linedata.pszText, linedata.iLength);
			lines->ReleaseLineData(&linedata);
		}
	}
	return text;
}

void EdCnt::Reformat(int startIndex, int startLine, int endIndex, int endLine)
{
#ifdef _DEBUG
	if (IsCFile(m_ftype))
	{
		// EdCnt::Reformat exists for C#; not for use in C/C++
		_ASSERTE(gTestsActive || !"do not use this function in c/c++ files");
		OutputDebugStringW(L"Do not use this function in c/c++ files\n");
	}
#endif

#if 0 // fixed in 15.7 preview 4
	if (gShellAttr->IsDevenv15u7OrHigher() && IsCFile(m_ftype))
	{
		// [case: 115449]
		// this workaround ignores start and end index parameters
		_ASSERTE(startIndex == -1);
		_ASSERTE(endIndex == -1);

		// save starting selection
		long startP1 = 0, startP2 = 0;
		GetSel2(startP1, startP2);
		_ASSERTE(startP1 == startP2); // no selection actually expected
		if (startP1 > startP2)
			std::swap(startP1, startP2);

		long formatStartLinePos = LineIndex(startLine);
		long formatEndLinePos = LineIndex(endLine + 2);
		_ASSERTE(formatEndLinePos > formatStartLinePos);

		// select the requested lines
		SetSel(formatStartLinePos, formatEndLinePos);

		// format the new selection
		gShellSvc->FormatSelection();

		// get the new formatted selection
		long endP1 = 0, endP2 = 0;
		GetSel2(endP1, endP2);

		// try to restore the original pos
		SetSel(startP1, startP1);
		return;
	}
#endif

	// Rerformat a span of text with IVsLanguageTextOps.Format()
	if (m_IVsTextView && gPkgServiceProvider)
	{
		CComPtr<IVsTextLines> lines;
		m_IVsTextView->GetBuffer(&lines);
		CComPtr<IVsTextBuffer> buffer{lines};
		if (buffer)
		{
			GUID guid;
			buffer->GetLanguageServiceID(&guid);
			IUnknown* tmp = NULL;
			gPkgServiceProvider->QueryService(guid, IID_IVsLanguageTextOps, (void**)&tmp);
			if (tmp)
			{
				CComQIPtr<IVsLanguageTextOps> languageTextOps{tmp};
				CComQIPtr<IVsTextLayer> vsTextLayer{buffer};
				if (languageTextOps && vsTextLayer)
				{
					TextSpan span = {startIndex, startLine, endIndex, endLine};
					languageTextOps->Format(vsTextLayer, &span);
				}
			}
		}
	}
	else
	{
		_ASSERTE(!"Reformat works in .NET only.");
		vLog("ERROR: Reformat called in unsupported context");
	}
}

void EdCnt::SetSel(long p1, long p2)
{
	// Invalidate scope after SetSel, so AST will wait for scope to become valid again.
	// I think this makes sense even when not running tests. -Jer
	m_isValidScope = FALSE;
	KillTimer(ID_TIMER_GETSCOPE);
	SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
	CTer::SetSel(p1, p2);
}

void EdCnt::SurroundWithBraces()
{
	CStringW selstr(GetSelStringW());

	long p1, p2;
	GetSel2(p1, p2);
	if (p1 > p2)
	{
		SwapAnchor();
		std::swap(p1, p2);
	}

	const EolTypes::EolType et = EolTypes::GetEolType(selstr);
	if (EolTypes::eolNone == et)
	{
		WTString autoTxt(gAutotextMgr->GetSource("{...} (line fragment)"));
		if (autoTxt.IsEmpty())
		{
			// do not provide a default snippet until
			// the autotext mgr supports unicode selections
			InsertW(L"{" + selstr + L"}", true, noFormat);
		}
		else
		{
			gAutotextMgr->InsertAsTemplate(mThis, autoTxt);
			// don't try to set the selection - it'll be messed up
			return;
		}
		SetSel((long)p1, (long)CurPos());
	}
	else
	{
		CStringW currentIndent;
		const int kLen = selstr.GetLength();
		if (gShellAttr->IsDevenv12OrHigher() && kLen)
		{
			// [case: 76620]
			// fix bad format by copying indent from current selection
			int idx = 0;
			WCHAR ch = selstr[0];
			if (ch != L' ' && ch != L'\t')
			{
				// move idx to start of next line
				int pos = selstr.FindOneOf(L"\r\n");
				if (-1 != pos)
				{
					while (selstr[pos] == L'\r' || selstr[pos] == L'\n')
						++pos;

					idx = pos;
				}
			}

			for (; idx < kLen; ++idx)
			{
				ch = selstr[idx];
				if (ch == L' ' || ch == L'\t')
					currentIndent += ch;
				else
					break;
			}
		}

		const WCHAR lastCh = selstr[selstr.GetLength() - 1];
		const CStringW lnBrk(EolTypes::GetEolStrW(et));
		if (selstr.GetLength() && (lastCh != L'\r' && lastCh != L'\n'))
			InsertW(currentIndent + L"{" + lnBrk + selstr + lnBrk + currentIndent + L"}", true, noFormat);
		else
			InsertW(currentIndent + L"{" + lnBrk + selstr + currentIndent + L"}" + lnBrk, true, noFormat);
		SetSel((long)p1, (long)CurPos());
		gShellSvc->FormatSelection();
	}
}

BOOL EdCnt::UpdateCompletionStatus(IVsCompletionSet* pCompSet, DWORD dwFlags)
{
	// via UpdateCompletionStatus hook in vapkg
	UserExpandCommandState st = gUserExpandCommandState;
	switch (gUserExpandCommandState)
	{
	case uecsNone:
	case uecsListMembersBefore:
	case uecsCompleteWordBefore:
		break;
	case uecsCompleteWordAfter:
		st = gUserExpandCommandState = uecsCompleteWordAfter2;
		break;
	case uecsListMembersAfter:
		st = gUserExpandCommandState = uecsListMembersAfter2;
		break;
	case uecsCompleteWordAfter2:
	case uecsListMembersAfter2:
		gUserExpandCommandState = uecsNone;
		break;
	default:
		_ASSERTE(!"unhandled UserExpandCommandState switch state");
		gUserExpandCommandState = uecsNone;
	}

	if (::ShouldSuppressVaListbox(mThis))
		return FALSE;

	if (gShellAttr->IsDevenv11OrHigher() && Psettings->m_bUseDefaultIntellisense && IsCFile(m_ftype) &&
	    !Psettings->m_autoSuggest)
	{
		// if va suggestions are disabled, allow dev11 default behavior
		return FALSE;
	}

	if (gShellAttr->IsDevenv9OrHigher() && Is_VB_VBS_File(m_ftype) && HasVsNetPopup(TRUE))
	{
		// [case: 15154] crash in vs2008 when their listbox is already up;
		// Dismiss leaves the VB CCompletor in a bad state sometimes;
		// it ends up trying to do a Commit after our call to Dismiss;
		return FALSE;
	}

	if (CS == m_ftype && m_lastScope == "String")
	{
		// [case: 15789] don't interfere with IDE XML comment completion
		// if this is too aggressive, we might be able to back off by
		// checking m_cwd and c_cwdLeftOfCursor for "<"
		if (Scope(TRUE) == "String") // Make sure scope is up to date. case=41614
			return FALSE;
	}

	BOOL retval = TRUE;
	g_CompletionSet->DisplayVSNetMembers(mThis, (LPVOID)pCompSet, (long)dwFlags, st, retval);
	return retval;
}

int EdCnt::ShowIntroduceVariablePopupMenuXP(int nrOfItems)
{
	PopupMenuXP xpmenu;
	std::string all = std::string("&Replace ") + std::to_string(nrOfItems) + std::string(" occurrences");
	xpmenu.AddMenuItem(1, MF_STRING, "Replace &selection");
	xpmenu.AddMenuItem(2, MF_STRING, all.c_str());

	if (gShellAttr->IsDevenv10OrHigher())
		SetFocusParentFrame(); // case=45591
	CPoint pt(GetPoint(posBestGuess));
	vClientToScreen(&pt);

	PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
	TempTrue tt(m_contextMenuShowing);
	int result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
	return result;
}

double EdCnt::GetZoomFactor()
{
	if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
	{
		_ASSERTE(!"Zoom is supported only by VS2010+");
		return 0;
	}

	EdCntWPF* ed_wpf = dynamic_cast<EdCntWPF*>(this);
	if (ed_wpf != nullptr)
		return ed_wpf->GetViewZoomFactor();

	_ASSERTE(!"Cast to EdCntWPF failed!");
	return 0;
}

bool EdCnt::IsIntelliCodeVisible()
{
	try
	{
		if (m_ftype == CS && gShellAttr && gShellAttr->IsDevenv17OrHigher())
		{
			// get text from adornment which is right from the caret at the same line
			// if this text is not null or empty, we consider it to be IntelliCode suggestion

			CComVariant adornmentText;
			if (m_IVsTextView && gVaInteropService &&
			    gVaInteropService->GetIntelliCodeAdornmentText(m_IVsTextView, &adornmentText) &&
			    adornmentText.bstrVal && *adornmentText.bstrVal)
			{
				return true;
			}
		}
	}
	catch (...)
	{
	}

	return false;
}

#if !defined(RAD_STUDIO) && !defined(VA_CPPUNIT)
void EdCnt::SetIvsTextView(IVsTextView* pView)
{
	__super::SetIvsTextView(pView);
	UpdateProjectInfo(nullptr);
}

void EdCnt::UpdateProjectInfo(IVsHierarchy* pInHierarchy)
{
	_ASSERT_IF_NOT_ON_MAIN_THREAD();

	try
	{
		if (!Psettings || !Psettings->m_enableVA)
			return;

		CComPtr<IVsHierarchy> pHierarchy(pInHierarchy);
	
		mProjectName.Empty();
		mProjectIcon = ICONIDX_FILE;
	
		if (!pHierarchy && m_IVsTextView && gVsRunningDocumentTable)
		{
			CComPtr<IVsTextLines> vsTextLines;
			if (SUCCEEDED(m_IVsTextView->GetBuffer(&vsTextLines)) && vsTextLines)
			{
				CComPtr<IEnumRunningDocuments> docsEnum;
				HRESULT hr = gVsRunningDocumentTable->GetRunningDocumentsEnum(&docsEnum);
				if (!(SUCCEEDED(hr) && docsEnum))
					return;
	
				docsEnum->Reset();
	
				VSCOOKIE curItem;
	#pragma warning(suppress : 6387)
				while (SUCCEEDED(docsEnum->Next(1, &curItem, NULL)))
				{
					VSRDTFLAGS rdtFlags = RDT_NoLock;
					CComPtr<IUnknown> pDocData;
					CComPtr<IVsHierarchy> pEnumHierarchy;
	
	#pragma warning(suppress : 6387)
					hr = gVsRunningDocumentTable->GetDocumentInfo(curItem, &rdtFlags, NULL, NULL, NULL, &pEnumHierarchy, NULL, &pDocData);
					if (!SUCCEEDED(hr))
						break;
	
					CComQIPtr<IVsTextLines> currLines(pDocData);
					if (pEnumHierarchy && currLines == vsTextLines)
					{
						pHierarchy = pEnumHierarchy;
						break;
					}
				}
			}
		}
	
		if (pHierarchy)
		{
			mProjectName = GetProjectName(pHierarchy);
			mProjectIcon = GetProjectIconIdx(pHierarchy);
		}
	}
	catch (...)
	{
		vLog("ERROR: Exception caught in EdCnt::UpdateProjectInfo");
	}
}

#endif // !RAD_STUDIO && !VA_CPPUNIT


// used for unit tests
void EdCnt::CreateDummyEditorControl(int ftype, MultiParsePtr& mp)
{
	g_currentEdCnt = std::make_shared<EdCntWPF>();
	g_currentEdCnt->m_ftype = ftype;
	g_currentEdCnt->m_pmparse = mp;
	g_currentEdCnt->m_vaOutlineTreeState = nullptr;
	g_currentEdCnt->m_ttParamInfo = nullptr;
	g_currentEdCnt->m_ttTypeInfo = nullptr;
	g_currentEdCnt->m_hParentMDIWnd = nullptr;
	g_currentEdCnt->m_ITextDoc = nullptr;
}
