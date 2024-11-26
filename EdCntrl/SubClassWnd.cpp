// SubClassWnd.cpp : implementation file
//

#include "stdafxed.h"
#include "EdDll.h"
#include "EdCnt.h"
#include "SubClassWnd.h"
#include "BorderCWnd.h"
#include "vatree.h"
#include "VaMessages.h"
#include "../addin/dscmds.h"
#include "FontSettings.h"
#include "ToolTipEditCombo.h"
#include "expansion.h"
#include "VACompletionSet.h"
#include "project.h"
#include "ArgToolTip.h"
#include "LicenseMonitor.h"
#include "VaTimers.h"
#include "VAWorkspaceViews.h"
#include "AutotextManager.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "VaService.h"
#include "VaOptions.h"
#include "FindReferencesResultsFrame.h"
#include "Registry.h"
#include "WindowUtils.h"
#include "Settings.h"
#include "RegKeys.h"
#include "BCMenu.h"
#include "Directories.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "AutoUpdate/WTAutoUpdater.h"
#include "LiveOutlineFrame.h"
#include "VAParse.h"
#include "SolutionFiles.h"
#include "IdeSettings.h"
#include "ScreenAttributes.h"
#include "MenuXP/Tools.h"
#include "LogVersionInfo.h"
#include "workspacetab.h"
#include "TipOfTheDay.h"
#include "VAAutomation.h"
#include "../common/TempAssign.h"
#include "../common/ScopedIncrement.h"
#include "inheritanceDb.h"
#include "includesDb.h"
#include "ParseThrd.h"
#include "VaAddinClient.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CStringW s_SplitFile;

CCriticalSection gUiThreadTasksLock;
std::vector<std::function<void()>> gUiThreadTasks;

// from the Platform SDK
#ifndef WM_APPCOMMAND
#define WM_APPCOMMAND 0x0319
#define APPCOMMAND_BROWSER_BACKWARD 1
#define APPCOMMAND_BROWSER_FORWARD 2
#define GET_APPCOMMAND_LPARAM(lParam) ((short)(HIWORD(lParam) & ~FAPPCOMMAND_MASK))
#define FAPPCOMMAND_MASK 0xF000
#endif // WM_APPCOMMAND

//////////////////////////////////////////////////////////////////////////
// NAVIGATE STUFF

BOOL g_ignoreNav = FALSE;

class NavClass
{
#define HISTSZ 60
  public:
	NavClass()
	{
		m_cur = 0;
		m_inNav = FALSE;
	}

	void Add(const CStringW& file, int ln, int col, BOOL force = FALSE)
	{
		if (m_inNav || g_ignoreNav || file.IsEmpty())
			return;

		int delta = force ? 0 : 3;
		if (m_list[m_cur].file == file && (m_list[m_cur].line > ln - delta && m_list[m_cur].line < ln + delta))
		{
			m_list[m_cur].col = col;
			m_list[m_cur].line = ln;
		}
		else
		{
			if (m_cur < HISTSZ)
				m_cur++;
			else
				m_cur = 0;
			m_list[m_cur].col = col;
			m_list[m_cur].file = file;
			m_list[m_cur].line = ln;
			m_list[(m_cur < HISTSZ) ? m_cur + 1 : 0].file.Empty();
		}
	}

	int NavGo(bool back = true)
	{
		int idx = m_cur; // Goto m_cur if not already there
		{
			EdCntPtr ed(g_currentEdCnt);
			if (ed && ed->FileName() == m_list[idx].file &&
			    (!m_list[m_cur].line || m_list[m_cur].line == ed->CurLine()))
			{
				idx = back ? m_cur - 1 : m_cur + 1;
			}
		}
		if (idx < 0)
			idx = HISTSZ - 1;
		if (idx >= HISTSZ)
			idx = 0;
		if (!m_list[idx].file.GetLength())
			return 0;
		int line = m_list[idx].line;
		int col = m_list[idx].col;
		TempTrue tt(m_inNav);
		EdCntPtr openedEd = DelayFileOpen(m_list[idx].file, line); // goto no scroll to center
		m_cur = idx;

		{
			EdCntPtr ed(g_currentEdCnt);
			_ASSERTE(ed == openedEd);
			if (ed) // Update line, in case there are no longer that many lines in file
			{
				m_list[m_cur].line = ed->CurLine();  // set it to actual line opened
				m_list[m_cur].file = ed->FileName(); // set it to actual line opened
			}
		}

		// go to col
		if (col > 1)
		{
			int pos = TERRCTOLONG(line, col);
			if (gShellAttr->IsDevenv10OrHigher())
			{
				// [case: 97613]
				if (openedEd)
					openedEd->SetPos((uint)pos);
			}
			else
			{
				HWND h = GetFocus();
				EdCnt* ed = (EdCnt*)SendMessage(h, WM_VA_GET_EDCNTRL, 0, 0);
				if (ed)
				{
					extern int g_dopaint;
					g_dopaint = FALSE;
					ed->SetPos((uint)pos);
					// fix flicker with col indicator
					g_dopaint = TRUE;
					ed->Invalidate(FALSE);
				}
			}
		}

		return 1;
	}

  protected:
  private:
	BOOL m_inNav;
	int m_cur;

	class NavObj
	{
	  public:
		CStringW file;
		int line, col;
	};
	NavClass::NavObj m_list[HISTSZ + 1];
};

extern BOOL g_DisplayFrozen;
static NavClass s_NavClass;

void NavAdd(const CStringW& file, uint ln, bool force /*= FALSE*/)
{
	if (!g_DisplayFrozen)
		s_NavClass.Add(file, (int)TERROW(ln), (int)TERCOL(ln), force);
	return;
}

void NavGo(bool back /*= true*/)
{
	s_NavClass.NavGo(back);
	return;
}

// NAVIGATE STUFF
//////////////////////////////////////////////////////////////////////////

extern int EditHelp(HWND h);

/////////////////////////////////////////////////////////////////////////////
// SubClassWnd message handlers
bool g_inMacro = false;

///////////////////////////////////////////////////////////////////////////////
LRESULT
VaAddinClient::PreDefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (!mLicenseInit && WM_VA_EXEC_UI_THREAD_TASKS != message)
			return FALSE;
		if (!gLicenseCountOk && Psettings && Psettings->m_enableVA)
			Psettings->m_enableVA = FALSE;
		if (!Psettings || (Psettings && !Psettings->m_validLicense) || !gLicenseCountOk)
			return FALSE;
		if (message > WM_MOUSEFIRST && message <= WM_MOUSELAST)
			vCatLog("Editor.Events", "VaEventME Mpredwp msg=0x%x wp=0x%zx lp=0x%zx", message, (uintptr_t)wParam, (uintptr_t)lParam);

		RunAllPendingInMainThread();

		switch (message)
		{
		case WM_VA_MAIN_THREAD_CB: {
			// Used by RunFromMainThread
			LPVOID** args = (LPVOID**)lParam;
			if (!args || args[0] == NULL)
			{
				typedef LRESULT(WINAPI * LRESULTCB)();
				LRESULTCB method = (LRESULTCB)wParam;
				return method();
			}
			else if (args[1] == NULL)
			{
				typedef LRESULT(WINAPI * LRESULTCB1)(void*);
				LRESULTCB1 method = (LRESULTCB1)wParam;
				return method(*args[0]);
			}
			else if (args[2] == NULL)
			{
				typedef LRESULT(WINAPI * LRESULTCB2)(void*, void*);
				LRESULTCB2 method2 = (LRESULTCB2)wParam;
				return method2(*args[0], *args[1]);
			}
			_ASSERTE(!"WM_VA_MAIN_THREAD_CB: Bad arg count.");
		}
			return 0;
		case WM_VA_EXEC_UI_THREAD_TASKS:
			return RunUiThreadTasks();
		case WM_VA_POST_EXEC_UI_THREAD_TASKS:
			if (E_PENDING == RunUiThreadTasks())
			{
				// busy, repost to try later
				PostMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_POST_EXEC_UI_THREAD_TASKS, 0, 0);
			}
			return 1;
		case WM_VA_THREAD_AST_GETLIST:
			// Called by AST automation to get the contents of the completion list
			// (a buffer is passed).
			if (wParam)
			{
				// Gets the contents of the completion list
				WTString* pwtStr = (WTString*)wParam;
				*pwtStr = g_CompletionSet->ToString(!!lParam);
			}
			return true;
		case WM_VA_THREAD_AUTOMATION_PEEK: {
			// https://msdn.microsoft.com/en-us/library/windows/desktop/ms644940%28v=vs.85%29.aspx
			// Retrieves the type of messages found in the calling thread's message queue.
			// was QS_ALLEVENTS; don't want QS_TIMER | QS_PAINT; (QS_SENDMESSAGE is not included in QS_ALLEVENTS)
			DWORD qsEvents = QS_KEY | QS_MOUSEBUTTON | QS_HOTKEY /*| QS_POSTMESSAGE*/;
			DWORD ret = GetQueueStatus(qsEvents);

			// https://msdn.microsoft.com/en-us/library/windows/desktop/ms644935%28v=vs.85%29.aspx
			// Determines whether there are mouse-button or keyboard messages in the calling thread's message queue
			// BOOL ret = GetInputState();

			// MSG msg;
			// BOOL ret = ::PeekMessage(&msg, 0, 0, WM_USER, PM_NOREMOVE);
			_ASSERTE(wParam);
			if (wParam)
			{
				BOOL* pRet = (BOOL*)wParam;
				*pRet = !!ret;
			}
		}
			return true;
		case WM_VA_THREAD_PROCESS_REF_QUEUE:
			g_ScreenAttrs.ProcessQueue_AutoReferences();
			return true;
		case WM_KILLFOCUS: {
			extern ArgToolTip* g_pLastToolTip;
			if (g_pLastToolTip && g_pLastToolTip->GetSafeHwnd() && g_pLastToolTip->IsWindowVisible() &&
			    !myGetProp(g_pLastToolTip->m_hWnd, "__VA_dont_close_on_kill_focus")) // catch stray tooltip
			{
				if (!g_currentEdCnt || g_currentEdCnt->m_ttParamInfo != g_pLastToolTip || HasVsNetPopup(false))
					g_pLastToolTip->ShowWindow(SW_HIDE);
			}
		}
		break;
		case WM_WINDOWPOSCHANGED:
			if (g_FontSettings)
				g_FontSettings->DualMonitorCheck();
			break;
		case WM_SETTINGCHANGE:
		case WM_SYSCOLORCHANGE:
			if (g_FontSettings)
				g_FontSettings->Update(FALSE);
			break;
		case WM_DISPLAYCHANGE: // [case: 142324] update image lists
			if (gImgListMgr)
				gImgListMgr->Update();
			break;
		case WM_VA_THREAD_SET_VS_OPTION: {
			token2 t = (LPCSTR)wParam;
			WTString lang = t.read(':');
			WTString prop = t.read(':');
			WTString val = t.read(':');
			if (t.more())
			{
				// [case: 63366] if the val is supposed to have a colon in it, the read chopped it
				val += t.Str();
			}
			CString* prevVal = (CString*)lParam;
			g_IdeSettings->SetEditorOption(lang.c_str(), prop.c_str(), val.c_str(), prevVal);
		}
			return TRUE;
		case WM_VA_OPTREBUILD:
#ifndef AVR_STUDIO
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000 && GetAsyncKeyState(VK_CONTROL) & 0x8000)
			{
				if (GlobalProject && GlobalProject->GetFileItemCount())
				{
					auto func = []() -> void {
						IncludesDb::Close();
						InheritanceDb::Close();
						if (GetSysDic())
							GetSysDic()->ReleaseTransientData(false);
						if (g_pGlobDic)
							g_pGlobDic->ReleaseTransientData(false);

						WtMessageBox("Released transient data.", IDS_APPNAME, MB_OK);
					};

					g_ParserThread->QueueParseWorkItem(new FunctionWorkItem(func));
				}
				else
				{
					// ctrl+shift purges only project dbs;
					// not .net, mfc, imports, type hist dbs (which are purged by regular rebuild)
					VaDirs::PurgeProjectDbs();
				}

				return TRUE;
			}
#endif // !AVR_STUDIO

			VaDirs::FlagForDbDirPurge();
			WtMessageBox(_T("You must restart your IDE before your changes take effect."), _T("Restart Required"),
			             MB_OK | MB_ICONEXCLAMATION);
			return TRUE;
		case WM_SETCURSOR:
			if (gShellAttr->IsDevenv10OrHigher())
				break;

			{
				EdCntPtr ed(g_currentEdCnt);
				if (ed && ::IsWindow(*ed))
				{
					CPoint pt;
					::GetCursorPos(&pt);
					::ScreenToClient(*ed, &pt);
					::SendMessage(*ed, WM_MOUSEMOVE, 0, (LPARAM)MAKELPARAM(pt.x, pt.y));
				}
			}

			if (GetActiveBCGMenu())
			{
				if ((GetKeyState(VK_LBUTTON) & 0x1000) || (GetKeyState(VK_RBUTTON) & 0x1000))
				{
					CPoint pt;
					GetCursorPos(&pt);
					::EnableWindow(MainWndH, TRUE);
					if (WindowFromPoint(pt) != GetActiveBCGMenu())
						::PostMessage(GetActiveBCGMenu(), WM_CLOSE, 0, 0);
				}
			}
			break;
		case WM_VA_GETVASERVICE2: {
			IVaService** ppSvc = (IVaService**)wParam;
			_ASSERTE(ppSvc);
			if (ppSvc)
				*ppSvc = gVaService;
		}
			return TRUE;
		case WM_VA_GOTOCLASSVIEWITEM:
			RefreshHcb();
			return TRUE;
			//	case WM_VA_MAKEUSERTYPEDAT:
			//		{
			//			// Get path to usertype.dat
			//			HINSTANCE inst = GetModuleHandleA("DevShl.dll");
			//			if(inst){
			//				WTString datFile, bakFile;
			//				char buf[MAX_PATH];
			//				GetModuleFileNameA(inst, buf, MAX_PATH);
			//				datFile = Path(buf) + "\\usertype.dat";
			//				bakFile = Path(buf) + "\\usertype.bak";
			//				// backup file
			//				if(wParam && IsFile(datFile)){
			//					remove(bakFile);
			//					rename(datFile, bakFile);
			//				}
			//				ofstream ofs(datFile, ios::app);
			//				g_pMFCDic->MakeUserType(&ofs, 0x3);
			//				if(lParam)
			//					g_pGlobDic->MakeUserType(&ofs, 0x3);
			//			}
			//			return TRUE;
			//		}
		case WM_VA_HISTORYFWD:
			if (gVaService)
				gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateForward);
			return TRUE;
		case WM_VA_HISTORYBWD:
			if (gVaService)
				gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateBack);
			return TRUE;
		case WM_VA_DEVEDITCMD: {
			CWnd* foc = CWnd::GetFocus();
			if (foc && foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0))
				foc->SendMessage(VAM_DSM_COMMAND, wParam, lParam);

			if ((wParam & 0xffff) == WM_VA_HISTORYFWD)
			{
				if (gVaService)
					gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateForward);
				return TRUE;
			}
			else if ((wParam & 0xffff) == WM_VA_HISTORYBWD)
			{
				if (gVaService)
					gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateBack);
				return TRUE;
			}
			else if ((wParam & 0xffff) == WM_VA_CODETEMPLATEMENU)
			{
				gAutotextMgr->Load(gTypingDevLang);
				return TRUE;
			}
			// don't sinkme after goto
			::KillTimer(::GetFocus(), ID_SINKME_TIMER);
		}
		// why not return TRUE; ?
		break;
		case WM_APPCOMMAND:
			if (Psettings && Psettings->mBrowserAppCommandHandling)
			{
				// support for browser forward and backward buttons on internet
				//  enabled keyboards
				CWnd* foc = CWnd::GetFocus();
				const int cmd = GET_APPCOMMAND_LPARAM(lParam);

				switch (cmd)
				{
				case APPCOMMAND_BROWSER_BACKWARD:
					if (1 == Psettings->mBrowserAppCommandHandling)
					{
						if (foc && foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0))
							foc->SendMessage(VAM_DSM_COMMAND, WM_VA_HISTORYBWD, 0);
						if (gVaService)
							gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateBack);
						return TRUE;
					}
					else if (2 == Psettings->mBrowserAppCommandHandling)
					{
						static const LPCSTR kBackCmd = "View.NavigateBackward";
						PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kBackCmd, 0);
						return TRUE;
					}
					break;
				case APPCOMMAND_BROWSER_FORWARD:
					if (1 == Psettings->mBrowserAppCommandHandling)
					{
						if (foc && foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0))
							foc->SendMessage(VAM_DSM_COMMAND, WM_VA_HISTORYFWD, 0);
						if (gVaService)
							gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateForward);
						return TRUE;
					}
					else if (2 == Psettings->mBrowserAppCommandHandling)
					{
						static const LPCSTR kForwardCmd = "View.NavigateForward";
						PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kForwardCmd, 0);
						return TRUE;
					}
					break;
				}
			}
			return FALSE;
		case WM_TIMER:
			if (wParam == IDT_EDDLL_ONIDLE)
			{
				extern CEdDllApp sVaDllApp;
				sVaDllApp.OnIdle(1);
				return TRUE;
			}
			if (wParam == IDT_CheckSolutionProjectsForUpdates)
			{
				KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_CheckSolutionProjectsForUpdates);
				ProjectReparse::ProcessQueuedProjects();
				return TRUE;
			}
			if (IDT_SolutionWorkspaceIndexerPing == wParam)
			{
				if (g_vaManagedPackageSvc)
				{
					KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionWorkspaceIndexerPing);
					vLog("ASQI:RAWC ping");
					gSqi.RequestAsyncWorkspaceCollection(false);
				}
				return TRUE;
			}
			if (IDT_SolutionWorkspaceForceRequest == wParam)
			{
				// [case: 141130]
				KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionWorkspaceIndexerPing);
				KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionWorkspaceForceRequest);
				if (g_vaManagedPackageSvc)
				{
					vLog("ASQI:RAWC force request now that pkb svc is loaded");
					gSqi.RequestAsyncWorkspaceCollection(true);
				}
				else
				{
					// need to retry later, and can't return normal WM_TIMER result (0) since
					// VA callers need to see TRUE to know message was handled by us.
					// So we KillTimer and SetTimer
					::SetTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionWorkspaceForceRequest, 5000, nullptr);
				}
				return TRUE;
			}
			if (IDT_SolutionReload == wParam)
			{
				KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionReload);
				if (GlobalProject && !GlobalProject->IsBusy())
					GlobalProject->LaunchProjectLoaderThread(CStringW());
				return TRUE;
			}
			if (IDT_SolutionReloadRetry == wParam)
			{
				KillTimer(gVaMainWnd->GetSafeHwnd(), IDT_SolutionReloadRetry);
				if (GlobalProject)
					GlobalProject->LaunchProjectLoaderThread(CStringW());
				return TRUE;
			}
			return FALSE;
		case WM_VA_THREAD_DELAYOPENFILE:
			if (::DelayFileOpen((LPCWSTR)wParam, (int)lParam, NULL, TRUE))
			{
				EdCntPtr ed(g_currentEdCnt);
				if (ed)
					ed->GetBuf(TRUE);
			}
			return true;
		default: {
			static const unsigned int WM_VA_VC6_BEFOREBUILDSTART = ::RegisterWindowMessage("WM_VA_VC6_BEFOREBUILDSTART");
			static const unsigned int WM_VA_CLOSE_FINDREF = ::RegisterWindowMessage("WM_VA_CLOSE_FINDREF");
			if (message == WM_VA_VC6_BEFOREBUILDSTART)
			{
				HWND frw = gShellSvc->GetFindReferencesWindow();
				if (frw && ::IsWindow(frw))
				{
					::PostMessage(frw, WM_VA_CLOSE_FINDREF, 0, 0);
				}
			}
		}
		break;
		}

		if (message == WM_COMMAND)
		{
			CWnd* foc = CWnd::GetFocus();
			uint cmd = (uint)wParam & 0xffff;
#define MINIHELPID 0x64
			int id = foc ? foc->GetDlgCtrlID() : 0;
			WTString cls = GetWindowClassString(::GetFocus());
			if (cmd == DSM_CANCEL && foc)
			{
				if (CTipOfTheDay::CloseTipOfTheDay())
					return TRUE;
				if (gShellAttr->IsDevenv())
					return FALSE;

				if (Psettings->m_incrementalSearch)
					Psettings->m_incrementalSearch = false;
				if (gShellAttr->RequiresFindResultsHack() && !g_currentEdCnt && gActiveFindRefsResultsFrame &&
				    gActiveFindRefsResultsFrame->IsPrimaryResultAndIsVisible())
				{
					gActiveFindRefsResultsFrame->HidePrimaryResults();
					return TRUE;
				}
				if (IsMinihelpDropdownVisible())
				{
					foc->PostMessage(WM_CHAR, VK_ESCAPE, 0);
					return TRUE;
				}
				if (g_CompletionSet->ProcessEvent(NULL, VK_ESCAPE))
					return TRUE;
				HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(foc->GetSafeHwnd(), GWLP_HINSTANCE);
				if (inst == gVaDllApp->GetVaAddress() || (id >= 100 && id <= 102) || cls == "Edit" ||
				    cls == "SysListView32")
				{
					::ReleaseCapture();
					foc->PostMessage(WM_CHAR, VK_ESCAPE, 0);
					EdCntPtr ed(g_currentEdCnt);
					if (ed)
						ed->SetFocusParentFrame();
					return TRUE;
				}
				if (::ClearAutoHighlights())
					return TRUE;
			}
			if ((id >= 100 && id <= 102) || cls == "Edit" || cls == "SysListView32")
			{
				if (cmd == ID_EDIT_COPY)
				{
					foc->SendMessage(WM_COPY, 0, 0);
					return TRUE;
				}
				else if (cmd == ID_EDIT_PASTE)
				{ // do
					foc->PostMessage(WM_PASTE, 0, 0);
					return TRUE;
				}
				else if (cmd == DSM_CANCEL)
				{ // do
					foc->PostMessage(WM_CHAR, VK_ESCAPE, 0);
					return TRUE;
				}
				else if (cmd == ID_EDIT_CLEAR)
				{ // do
					::SendMessage(foc->m_hWnd, WM_KEYDOWN, VK_DELETE, 0x001c0001);
					//	Log("Delete 2");
					::SendMessage(foc->m_hWnd, WM_KEYUP, VK_DELETE, 0x001c0001);
					//					foc->PostMessage(WM_COMMAND, ID_EDIT_CLEAR, 0);
					return TRUE;
				}
			}

			// broke goto def key assignments
			if (cmd == DSM_LISTMEMBERS)
			{
				static const WTString suppressVSNetCompletions = (const char*)GetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "suppressVSNetCompletions");
				if (gShellAttr->IsDevenv() && suppressVSNetCompletions != "Yes")
				{
					static volatile long sProcessingListMembers = 0;
					// sanity check to make sure we do not get caught in a loop
					if (InterlockedIncrement(&sProcessingListMembers) > 1)
					{
						_ASSERTE(!"SubClassWnd DSM_LISTMEMBERS recursion sanity check");
						InterlockedDecrement(&sProcessingListMembers);
						return TRUE;
					}

					const bool macro = g_inMacro;
					g_inMacro = TRUE;
					g_IgnoreBeepsTimer = GetTickCount() + 1000;
					LRESULT r = 0;
#if !defined(SEAN)
					try
#endif // !SEAN
					{
						r = SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.ListMembers"), 0);
					}
#if !defined(SEAN)
					catch (...)
					{
						VALOGEXCEPTION("SCW:ListMembers:");
					}
#endif // !SEAN
					g_inMacro = macro;
					InterlockedDecrement(&sProcessingListMembers);
					return r;
				}
			}
			if (cmd == DSM_PARAMINFO && gShellAttr->IsDevenv())
			{
				static volatile long sProcessingParamInfo = 0;
				// sanity check to make sure we do not get caught in a loop
				if (InterlockedIncrement(&sProcessingParamInfo) > 1)
				{
					InterlockedDecrement(&sProcessingParamInfo);
					return TRUE;
				}

				const bool macro = g_inMacro;
				g_inMacro = TRUE;
				g_IgnoreBeepsTimer = GetTickCount() + 1000;
				LRESULT r = 0;
#if !defined(SEAN)
				try
#endif // !SEAN
				{
					r = SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.ParameterInfo"), 0);
				}
#if !defined(SEAN)
				catch (...)
				{
					VALOGEXCEPTION("SCW:ParamInfo:");
				}
#endif // !SEAN
				g_inMacro = macro;
				InterlockedDecrement(&sProcessingParamInfo);
				return r;
			}

			if (gShellAttr->RequiresFindResultsHack() && gActiveFindRefsResultsFrame)
			{
				if (gActiveFindRefsResultsFrame->IsWindowFocused())
				{
					switch (cmd)
					{
					case DSM_FIND:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsFind);
						return TRUE;
					case DSM_FINDNEXT:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsFindNext);
						return TRUE;
					case DSM_FINDPREV:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsFindPrev);
						return TRUE;
					case DSM_ERROR_NEXT:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsNext);
						return TRUE;
					case DSM_ERROR_PREV:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsPrev);
						return TRUE;
					case ID_EDIT_CLEAR:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsDelete);
						return TRUE;
					case ID_EDIT_COPY:
						// only works in secondary windows
						// not primary window
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsCopy);
						return TRUE;
					case ID_EDIT_CUT:
						// does not work in primary window
						// only works in secondary windows if selection in editor
						// 						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsCut);
						// 						return TRUE;
						break;
					}
				}

				if (gActiveFindRefsResultsFrame->IsPrimaryResultAndIsVisible())
				{
					// this only works when the output window has content that could
					// respond to DSM_ERROR_NEXT or DSM_ERROR_PREV
					switch (cmd)
					{
					case DSM_ERROR_NEXT:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsNext);
						return TRUE;
					case DSM_ERROR_PREV:
						gActiveFindRefsResultsFrame->Exec(icmdVaCmd_RefResultsPrev);
						return TRUE;
					case DSM_OUTPUTWND:
					case DSM_OUTPUTWND_2:
					case DSM_RESULTSLIST:
					case DSM_FINDINFILES:
						gActiveFindRefsResultsFrame->HidePrimaryResults();
						break;
					}
				}
			}

			if (ID_EDIT_CLEAR == cmd && gShellAttr->IsMsdev() && gVaService)
			{
				// The cut, copy and paste cmd bindings do not work in the
				// outline - the commands are disabled when focus is in outline

				LiveOutlineFrame* frm = gVaService->GetOutlineFrame();
				if (frm && frm->IsWindowFocused())
				{
					if (frm->QueryStatus(icmdVaCmd_OutlineDelete))
						frm->Exec(icmdVaCmd_OutlineDelete);
					return TRUE;
				}
			}

			EdCnt* edFoc = NULL;
			if (foc)
			{
				edFoc = (EdCnt*)foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0);
				if (IsBadReadPtr(edFoc, sizeof(EdCnt)))
					edFoc = NULL;
			}

			if (DSM_MACRO_STOPREC <= cmd && cmd <= DSM_LASTCMD)
			{
				if (cmd == DSM_MACRO_STARTREC)
				{
					// we handle start/stop logic in vassistnet.dll for VS
					// but for vc6, we just toggle g_inMacro here
					_ASSERTE(gShellAttr->IsMsdev() || !g_inMacro);
					g_inMacro = !g_inMacro;
					if (!g_inMacro && !gShellAttr->IsDevenv10OrHigher())
						::SetCursor(NULL);
				}
				else if (cmd == DSM_MACRO_STOPREC)
					g_inMacro = false;
				else if (DSM_QMACRO_PLAY <= cmd && cmd <= DSM_MACRO_33)
				{
					if (edFoc || gShellAttr->IsDevenv())
						g_inMacro = true;
				}

				if (!g_inMacro && edFoc)
					foc->SendMessage(VAM_DSM_COMMAND, DSM_MACRO_1, lParam);

				return FALSE;
			}

			if (edFoc)
			{
				switch (cmd)
				{
				case ID_EDIT_SELECT_ALL:
					if (gShellAttr->IsMsdev())
					{
						// Do a manual SelectAll
						edFoc->SetSelection(-1, 0);
						return TRUE; // because we handled it
					}
					break;
				case 0x3656: // Paradigm split window vertical/horizontal...
				case 0x3655: {
					// this is a hack for paradigm, they will resubclass this control when the command gets run.
					// unsubclass us, and relaunch when command has finnished. -Jer
					s_SplitFile = edFoc->FileName();
					::SendMessage(edFoc->m_hWnd, WM_VA_GET_EDCNTRL, WM_CLOSE, 0);
				}
					return FALSE;
				case WM_VA_HISTORYBWD:
					edFoc->SendMessage(VAM_DSM_COMMAND, WM_VA_HISTORYBWD, 0);
					if (gVaService)
						gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateBack);
					return TRUE;
				case WM_VA_HISTORYFWD:
					edFoc->SendMessage(VAM_DSM_COMMAND, WM_VA_HISTORYFWD, 0);
					if (gVaService)
						gVaService->Exec(IVaService::ct_global, icmdVaCmd_NavigateForward);
					return TRUE;
				case 0xe146: // WM_HELP:
					if (::EditHelp(edFoc->m_hWnd))
						return TRUE;
					return FALSE;
				case ID_FILE_PRINT: {
					extern EdCnt* g_printEdCnt;
					g_printEdCnt = edFoc;
				}
				break;
				case ID_FILE_CLOSE:
					//  cause addin side does not send flushfile on file/close
					edFoc->SendMessage(WM_VA_FLUSHFILE);
					break;
				case ID_EDIT_PASTE:
				case WM_PASTE:
					Psettings->m_incrementalSearch = false;
					// let us know that a paste is going to happen
					edFoc->SendMessage(VAM_DSM_COMMAND, WM_PASTE, 1); // 1 is flag to pre paste
					break;
				case DSM_AUTOCOMPLETE:
				case DSM_LISTMEMBERS:
				case DSM_TAB:
				case DSM_REVTAB:
				case DSM_CANCEL:
				case DSM_ENTER:
				case ID_EDIT_CUT:
					Psettings->m_incrementalSearch = false;
					// fall through
				case DSM_PARAMINFO:
				case DSM_TYPEINFO:
				case DSM_BACKSPACE:
				case ID_EDIT_COPY:
				case ID_EDIT_UNDO:
				case ID_EDIT_REDO:
				case DSM_TOGGLE_BKMK:
					edFoc->SetTimer(ID_ADDMETHOD_TIMER, 1000, NULL);
				case DSM_PAGEUP:
				case DSM_PAGEDOWN:
				case DSM_HOMETEXT:
				case DSM_END:
				case DSM_RIGHT:
				case DSM_LEFT:
				case DSM_UP:
				case DSM_DOWN:
				case DSM_TOGGLE_INSERT:

				case ID_EDIT_CLEAR: // DELETE KEY
				case DSM_SCROLLDOWN:
				case DSM_SCROLLUP:
				case DSM_SELLEFT:
				case DSM_SELRIGHT:

					// Pass expand to us first to see if we forward it to them as well
					{
						if (edFoc->SendMessage(VAM_DSM_COMMAND, wParam, lParam))
							return TRUE;
					}
					break;
				case DSM_FINDBRACEMATCH:
				case DSM_FINDCURRENTWORDBWD:
				case DSM_FINDCURRENTWORDFWD:
				case DSM_ERROR_NEXT:
				case DSM_ERROR_PREV:
				case DSM_GOTONEXTBKMK:
				case DSM_GOTOPREVBKMK:
				case DSM_FINDPREV:
				case DSM_FINDNEXT:
				case DSM_FIND:
				case DSM_FINDAGAIN:
					Psettings->m_incrementalSearch = false;
					edFoc->SetTimer(ID_CURSCOPEWORD_TIMER, 10, NULL);
					edFoc->SetTimer(ID_ADDMETHOD_TIMER, 200, NULL);
					edFoc->SetTimer(ID_TIMER_NUKEPOPUPS, 200, NULL);
					break;
				case DSM_PREVWINDOW:
					//					::SendMessage(MainWndH, WM_SETREDRAW, FALSE, 0);
					break;
				}
			}

			return FALSE; // run pass to therm
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		char buf[255];
		sprintf(buf, "Error: Exception caught in PreProc(0x%x, 0x%zx, 0x%zx)", message, (uintptr_t)wParam,
		        (uintptr_t)lParam);
		LOG(buf);
		VALOGEXCEPTION("SCW:");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in MainPreDefWindowProc");
		}
	}
#endif // !SEAN
	return FALSE;
}

#if !defined(RAD_STUDIO)
bool LaunchEdCnt(HWND hwnd)
{
	if (!hwnd)
		return false;
	int id = ::GetDlgCtrlID(hwnd);
	TCHAR wndClass[MAX_PATH];
	if (::GetClassName(hwnd, wndClass, MAX_PATH))
	{
		if ((id & 0xffffff00) == 0xe900 && !_tcscmp(_T("Afx:400000:8"), wndClass))
		{
			LRESULT g = ::SendMessage(hwnd, WM_VA_GET_EDCNTRL, 0, 0);
			HWND p = ::GetDlgItem(::GetParent(hwnd), 0xe900);
			if (p && !g)
			{
				EdCnt* ed = (EdCnt*)::SendMessage(p, WM_VA_GET_EDCNTRL, 0, 0);
				if (ed)
				{
					gVaAddinClient.AttachEditControl(hwnd, ed->FileName(), ed->m_ITextDoc);
					return true;
				}
			}
			// this is the top edit wnd
			//			::GetFocus(); - doesn't do anything
		}
	}
	return false;
}
#endif

static const LPCSTR kToggleVa = "VAssistX.EnableDisable";
static UINT_PTR sFixResizeTimerId = 0;

void CALLBACK AutoReenableTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	// [case: 46555]
	_ASSERTE(sFixResizeTimerId == idEvent);
	sFixResizeTimerId = 0;
	KillTimer(hWnd, idEvent);
	if (!Psettings || Psettings->m_enableVA)
		return;

	Log("Auto-enable VA (case 46555)");
	SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kToggleVa, 0);
	Psettings->m_enableVA = true;

	if (g_currentEdCnt->GetSafeHwnd())
		g_currentEdCnt->Invalidate(TRUE);
	if (gMainWnd->GetSafeHwnd())
		gMainWnd->Invalidate(TRUE);

	// [case: 92444]
	_ASSERTE(gShellAttr && gShellAttr->IsDevenv() && !gShellAttr->IsDevenv10OrHigher());
	if (GlobalProject && GlobalProject->CheckForNewSolution())
		GlobalProject->LaunchProjectLoaderThread(CStringW());
}

LRESULT
VaAddinClient::PostDefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (!mLicenseInit)
			return FALSE;

		if (message > WM_MOUSEFIRST && message <= WM_MOUSELAST)
			vCatLog("Editor.Events", "VaEventME Mpostdwp msg=0x%x wp=0x%zx lp=0x%zx", message, (uintptr_t)wParam, (uintptr_t)lParam);

		if (message == WM_PAINT && gShellAttr->IsMsdev())
		{
			if (!g_WorkSpaceTab)
				VAWorkspaceSetup_VC6();
			static BOOL once = TRUE;
			if (once)
			{
				once = FALSE;
				CTipOfTheDay::LaunchTipOfTheDay();
			}
		}

		if (WM_WINDOWPOSCHANGED == message && lParam && gShellAttr && Psettings)
		{
			static bool sCheckForResizeFix = false;
			static bool once = true;
			if (once)
			{
				once = false;
				if (gShellAttr->IsDevenv() && !gShellAttr->IsDevenv10OrHigher() && GetWinVersion() >= wvWinXP)
				{
					LogVersionInfo t(false, true);
					sCheckForResizeFix = t.IsX64();
				}
			}

			if (sCheckForResizeFix && Psettings->m_enableVA)
			{
				WINDOWPOS* pWp = (WINDOWPOS*)lParam;
				bool fixResize = false;
				static const DWORD SWP_STATECHANGED = 0x8000;
				if (pWp->flags & (SWP_FRAMECHANGED | SWP_STATECHANGED))
				{
					WINDOWPLACEMENT wp;
					ZeroMemory(&wp, sizeof(WINDOWPLACEMENT));
					wp.length = sizeof(WINDOWPLACEMENT);
					if (GetWindowPlacement(MainWndH, &wp))
					{
						if (SW_SHOWMAXIMIZED == wp.showCmd || SW_SHOWNORMAL == wp.showCmd)
						{
							// resized via maximize or restore
							fixResize = true;
						}
					}
				}
				else if (!(pWp->flags & SWP_NOSIZE))
				{
					// resizing (resize by left/top border are also moves)
					fixResize = true;
				}

				if (fixResize)
				{
					// [case: 46555] an improvement to resize on winx64
					if (sFixResizeTimerId)
						KillTimer(NULL, sFixResizeTimerId);
					Log("Auto-disable VA (case 46555)");
					PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kToggleVa, 0);
					sFixResizeTimerId = ::SetTimer(NULL, 0, 50, (TIMERPROC)&AutoReenableTimerProc);
				}
			}
		}

		if (WM_COMMAND == message)
		{
			uint cmd = (uint)wParam & 0xffff;
			CWnd* foc = CWnd::GetFocus();

			switch (cmd)
			{
			case 0x3656:
			case 0x3655:
				gVaAddinClient.AttachEditControl(::GetFocus(), s_SplitFile, NULL);
				return FALSE;
			}
			if (foc && foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0))
			{
				// make check to see if the command scrolled the screen
				foc->SetTimer(ID_TIMER_CHECKFORSCROLL, 10, NULL);
				switch (cmd)
				{
				case ID_FILE_PRINT: {
					extern EdCnt* g_printEdCnt;
					g_printEdCnt = NULL;
				}
				break;
				case ID_EDIT_COPY:
				case ID_EDIT_CUT:
					break;
				case ID_EDIT_CLEAR: // DELETE KEY
				case ID_EDIT_CLEAR_ALL:
				case DSM_DELBLANKLINE:
				case DSM_DELHORWHITESPACE:
				case DSM_DELLINE:
				case DSM_CUTLINE:
				case DSM_DELTOEOL:
				case DSM_DELTOEOS:
				case DSM_DELTOSTARTOFLINE:
				case DSM_DELWORDLEFT:
				case DSM_DELWORDRIGHT:
					// so we know the doc changed, and we should reload
					foc->SendMessage(VAM_DSM_COMMAND, ID_EDIT_UNDO, lParam);
					break;
				case DSM_OPTIONS:
					// Check for font changes
					foc->SendMessage(VAM_DSM_COMMAND, DSM_OPTIONS, lParam);
					break;
				case ID_EDIT_PASTE:
				case WM_PASTE: {
					// call us after paste
					foc->SendMessage(VAM_DSM_COMMAND, WM_PASTE, 0);
					foc->SendMessage(VAM_DSM_COMMAND, VAM_GETBUF);
				}
					return TRUE;
				case DSM_WIZ_ADDMSGHANDLER:
				case DSM_WIZ_ADDVIRTFN:
				case DSM_WIZ_ADDMEMBERFN2:
				case DSM_WIZ_ADDVARIABLE:
					foc->SendMessage(WM_VA_REPARSEFILE, 0, 0);
					break;
				case DSM_PREVWINDOW:
					break;
				case DSM_INCSEARCHBWD:
				case DSM_INCSEARCHFWD:
					if (Psettings)
						Psettings->m_incrementalSearch = true;

					{
						EdCntPtr ed(g_currentEdCnt);
						if (ed && ::IsWindow(*ed))
							ed->ClearAllPopups(false);
					}
					break;
				case DSM_FORMAT:
					// so we know the doc changed, and we should reload
					foc->SendMessage(VAM_DSM_COMMAND, DSM_MACRO_1, lParam);
					return TRUE;
				}

				if (DSM_QMACRO_PLAY <= cmd && cmd <= DSM_MACRO_33)
				{
					if (g_inMacro && gShellAttr->IsMsdev())
					{
						// so we know the doc changed, and we should reload
						g_inMacro = false;
						foc->SendMessage(VAM_DSM_COMMAND, DSM_MACRO_1, lParam);
					}
					return TRUE;
				}

				// let us know that some command has just finished,
				// so that if cursor has changed, we can nuke popups
				foc->SendMessage(VAM_DSM_COMMAND, WM_COMMAND, lParam);
			}

			if (g_inMacro && gShellAttr->IsMsdev())
			{
				if (DSM_QMACRO_PLAY <= cmd && cmd <= DSM_MACRO_33)
				{
					g_inMacro = false;
				}
			}
		}
#if defined(RAD_STUDIO)
		else if (VAM_EXECUTECOMMAND == message)
		{
			_ASSERTE(!"Ignored VAM_EXECUTECOMMAND message sent to MainWnd in C++Builder");
			vLog("WARN: CPPB: VAM_EXECUTECOMMAND message sent to MainWnd Ignored %zx %zx", (uintptr_t)wParam, (uintptr_t)lParam);
		}
#endif
	}
#if !defined(SEAN)
	catch (...)
	{
		char buf[255];
		sprintf(buf, "Error: Exception caught in PostProc(0x%x, 0x%zx, 0x%zx)", message, (uintptr_t)wParam,
		        (uintptr_t)lParam);
		LOG(buf);
		VALOGEXCEPTION("SCW:");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in MainPostDefWindowProc");
		}
	}
#endif // !SEAN
	return FALSE;
}

std::atomic<int> gProcessingMessagesOnUiThread;

void RunUiThreadTasksUnchecked()
{
	std::vector<std::function<void()>> tsks;

	{
		AutoLockCs l(gUiThreadTasksLock);
		tsks.swap(gUiThreadTasks);
	}

	for (auto& func : tsks)
	{
		try
		{
			func();
		}
		catch (...)
		{
		}
	}
}

LRESULT
RunUiThreadTasks()
{
	_ASSERTE(g_mainThread == ::GetCurrentThreadId());
	ScopedIncrementAtomic si(&gProcessingMessagesOnUiThread);
	if (gProcessingMessagesOnUiThread > 1)
		return E_PENDING;

	RunUiThreadTasksUnchecked();
	return 1;
}

int GetPendingUiThreadTaskCount()
{
	AutoLockCs l(gUiThreadTasksLock);
	return (int)gUiThreadTasks.size();
}

void RunFromMainThread(std::function<void()> fnc, bool synchronous)
{
	{
		AutoLockCs l(gUiThreadTasksLock);
		gUiThreadTasks.emplace_back(std::move(fnc));
	}

	if (synchronous)
	{
		if (GetCurrentThreadId() == g_mainThread)
		{
			RunUiThreadTasksUnchecked();
		}
		else
		{
			while (SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_EXEC_UI_THREAD_TASKS, 0, 0) == E_PENDING)
			{
				// ui thread is in one of our PeekMessage calls.
				// do not process ui thread async tasks from PeekMessage.
				// wait and retry.
				Sleep(10);
			}
		}
	}
	else
		PostMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_EXEC_UI_THREAD_TASKS, 0, 0);
}

// CSList class manages Win32 lock-free singly-linked list (SLIST)
template <typename TYPE>
    requires std::is_default_constructible_v<TYPE>
class CSList
{
public:
	CSList()
	{
		slist = (SLIST_HEADER*)_aligned_malloc(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT);
		::InitializeSListHead(slist);
	}
	~CSList()
	{
		assert(approx_count() == 0);
		_aligned_free(slist);
		slist = nullptr;
	}

	// represents the node TYPE bundled with required SLIST header
	struct item_t : public SLIST_ENTRY, public TYPE
	{
		uint8_t merge_marker = 0; // if not 0, only the last one of that type queued will be executed
	};
	static_assert((void*)static_cast<SLIST_ENTRY*>((item_t*)nullptr) == (void*)nullptr);	// check if SLIST_ENTRY is really first

	static item_t* create_item()
	{
		item_t *ret = (item_t *)_aligned_malloc(sizeof(*ret), MEMORY_ALLOCATION_ALIGNMENT);
		construct_item(ret);
		return ret;
	}
	static void destroy_item(item_t *item, bool destruct = true)
	{
		assert(item);
		if(destruct)
			destruct_item(item);
		_aligned_free(item);
	}
	static void construct_item(item_t *item)
	{
		assert(item);
		::new (item) item_t();
	}
	static void destruct_item(item_t *item)
	{
		assert(item);
		item->~item_t();
	}

	void push_item(item_t *item)
	{
		assert(item);
		::InterlockedPushEntrySList(slist, item);
	}
	item_t *pop_item()
	{
		SLIST_ENTRY *ret = ::InterlockedPopEntrySList(slist);
		return ret ? static_cast<item_t *>(ret) : nullptr;
	}

	uint16_t approx_count() const
	{
		// counter may overflow, but won't happen for us
		return ::QueryDepthSList(slist);
	}

	SLIST_ENTRY *retrieve_all_items()
	{
		return ::InterlockedFlushSList(slist);
	}

protected:
  SLIST_HEADER* slist;
};

// class that manages two CSList queues, one with pending work and the other with freed items to be reused (it's a plan to use this with WTStrings as well)
template <typename TYPE, size_t max_cached_items = 256>
class CSListCachedQueue
{
	using slist_t = CSList<TYPE>;
	using item_t = slist_t::item_t;

public:
	~CSListCachedQueue()
	{
		while (item_t *item = free_items.pop_item())
			slist_t::destroy_item(item, false);
		while (item_t* item = pending_work.pop_item())
			slist_t::destroy_item(item, true);
	}

	item_t *create_item()
	{
		item_t *ret = free_items.pop_item();
		if(ret)
			slist_t::construct_item(ret);
		else
			ret = slist_t::create_item();
		return ret;
	}
	void destroy_item(TYPE *item)
	{
		assert(item);
		if(item)
			destroy_item(static_cast<item_t*>(item));
	}
	item_t *destroy_item(item_t *item)
	{
		assert(item);
		auto next = item->Next;
		if (free_items.approx_count() < max_cached_items)
		{
			slist_t::destruct_item(item);
			free_items.push_item(item);
		}
		else
			slist_t::destroy_item(item);
		return next ? static_cast<item_t*>(next) : nullptr;
	}
	
	void push_item(TYPE *item)
	{
		assert(item);
		pending_work.push_item(static_cast<item_t*>(item));
	}
	TYPE *pop_item()
	{
		item_t* ret = pending_work.pop_item();
		return ret ? ret : nullptr;
	}


	// a helper class that will allow to do a ranged-for over pending work items and clean them up in the destructor
	struct self_destructing_items_collection_t
	{
		self_destructing_items_collection_t(CSListCachedQueue* queue, SLIST_ENTRY* ptr)
		    : queue(queue), ptr(ptr)
		{
		}
		self_destructing_items_collection_t() {}
		~self_destructing_items_collection_t()
		{
			for (auto it = ptr; it;)
				it = queue->destroy_item(static_cast<item_t*>(it));
		}


		// very basic iterator missing a lot of parts; good enough for now
		struct iterator
		{
			iterator(SLIST_ENTRY* ptr) : ptr(ptr) {}
			iterator() : ptr(nullptr) {}

			iterator operator++()
			{
				assert(ptr);
				ptr = ptr->Next;
				return *this;
			}
			bool operator==(const iterator& it) const
			{
				return ptr == it.ptr;
			}
			bool operator!=(const iterator& it) const
			{
				return ptr != it.ptr;
			}
			TYPE& operator*()
			{
				assert(ptr);
				return *static_cast<item_t *>(ptr);
			}

		protected:
			SLIST_ENTRY* ptr;
		};

		iterator begin()
		{
			return {ptr};
		}
		iterator end()
		{
			return {};
		}

	protected:
		CSListCachedQueue *queue = nullptr;
		SLIST_ENTRY *ptr = nullptr;
	};

	self_destructing_items_collection_t retrieve_all_items()
	{
		SLIST_ENTRY *current = pending_work.retrieve_all_items();
		if(!current)
			return {};

		// if an item has a non-zero marker, only the last one present in the queue will get executed; others are discarded
		std::vector<bool> markers(256u);
		auto process_marker = [&markers](SLIST_ENTRY *entry) {
			uint8_t marker = entry ? static_cast<item_t *>(entry)->merge_marker : 0u;
			if(!marker)
				return false;
			if (markers[marker])
				return true; // true means remove
			markers[marker] = true;
			return false;
		};
		process_marker(current); // just apply marker if needed, the first one cannot be deleted

		// quickly reverse the list as new members are inserted at the front
		// if marker is present, leave just the first of a kind (the last one queued)
		SLIST_ENTRY *prev = nullptr;
		while(true)
		{
			// get next item
			auto next = current->Next;
			while(process_marker(next))
			{
				extern std::atomic_uint64_t removed_by_marker;
				++removed_by_marker;
				next = destroy_item(static_cast<item_t*>(next));
			}

			// push at the front of the reversed list
			current->Next = prev;
			if (!next)
				break;

			prev = current;
			current = next;
		}

		return {this, current};
	}

protected:
	slist_t pending_work;
	slist_t free_items;
};

struct main_thread_work_item_t
{
	std::move_only_function<void()> work;
};

CSListCachedQueue<main_thread_work_item_t> main_thread_work_items;


// quickly queue work for main thread without going to kernel
void RunFromMainThread2(std::move_only_function<void()> work, uint8_t merge_marker)
{
	if(g_mainThread == ::GetCurrentThreadId())
	{
		work();
		return;
	}

	auto *i = main_thread_work_items.create_item();
	i->work = std::move(work);
	i->merge_marker = merge_marker;
	main_thread_work_items.push_item(i);
}

std::atomic<int64_t> total_time_running_in_main_thread_ns;
std::atomic<int64_t> total_items_running_in_main_thread;
std::atomic<int64_t> trottling_cnt_main_thread;

std::atomic_uint64_t rfmt_progress;
std::atomic_uint64_t rfmt_status;
std::atomic_uint64_t rfmt_flush;
std::atomic_uint64_t removed_by_marker;
#include <experimental/generator>
static std::atomic_uint32_t no_trottling;
static std::atomic_bool already_running_all;
void RunAllPendingInMainThread()
{
	assert(g_mainThread == ::GetCurrentThreadId());
	if (already_running_all)
		return;
	already_running_all = true;

	static constexpr auto max_processing_time = std::chrono::milliseconds(100);
	static constexpr auto trottling_time = std::chrono::milliseconds(5);

	// generator concept is abused so I can run items for some time, return to process messages and then resume later (I don't believe I could done that with await)
	static auto generator_lambda = []() -> std::experimental::generator<bool> {
		while(true)
		{
			co_yield true;
			auto t = std::chrono::high_resolution_clock::now();
			auto items = main_thread_work_items.retrieve_all_items();
			for (auto& i : items)
			{
				i.work();
				++total_items_running_in_main_thread;

				auto t2 = std::chrono::high_resolution_clock::now();
				total_time_running_in_main_thread_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t).count();
				if (!no_trottling.load() && ((t2 - t) >= max_processing_time))
				{
					++trottling_cnt_main_thread;
					do 
					{
						co_yield true;
						t = std::chrono::high_resolution_clock::now();
					} while ((t - t2) <= trottling_time);
				}
				else
					t = t2;
			}
		}
	};
	static auto generator = generator_lambda();
	static auto generator_iterator = generator.begin();

	// dummy iterator; used just to run another cycle
	++generator_iterator;

	already_running_all = false;
}

void FlushAllPendingInMainThread()
{
	if (gVaMainWnd)
	{
		++no_trottling;
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_EXEC_UI_THREAD_TASKS, 0, 0); // any message will run all pending items
		--no_trottling;
	}
}
