// ToolTipEditCombo.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "ToolTipEditCombo.h"
#include "expansion.h"
#include "VACompletionBox.h"
#include "VaTimers.h"
#include "VAParse.h"
#include "fontsettings.h"
#include "addin\MiniHelpFrm.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "FileLineMarker.h"
#include "SyntaxColoring.h"
#include "FileTypes.h"
#include "project.h"
#include "Registry.h"
#include "RegKeys.h"
#include "VA_MRUs.h"
#include "SaveDC.h"
#include "vsshell100.h"
#include "VAAutomation.h"
#include "MenuXP\MenuXP.h"
#include "IdeSettings.h"
#include "StringUtils.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "Colourizer.h"
#include "MenuXP\Tools.h"
#include "AutoUpdate\WTAutoUpdater.h"
#include "VAThemeUtils.h"
#include <windowsx.h>

using namespace std::placeholders;
#undef SubclassWindow

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

uint wm_postponed_set_focus = ::RegisterWindowMessage("WM_POSTPONED_SET_FOCUS");
bool sMifMenuActive = false;
const UINT_PTR kTimerIdFilter = 1;
const UINT_PTR kTimerIdInvalidate = 2;

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditCombo

// #Minihelp_Static
static CToolTipEditCombo* s_combos[3];
#define S_COMBO(role) (s_combos[(int)role])

CToolTipEditCombo* GetToolTipEditCombo(PartRole role)
{
	return S_COMBO(role);
}

CToolTipEditCombo::CToolTipEditCombo(PartRole role)
    : CPartWithRole(role), m_subclasser(CreateSubclasser())
{
	S_COMBO(m_partRole) = this;
	readonly_combo = true;	
}

CToolTipEditCombo::~CToolTipEditCombo()
{
	CatLog("Editor", "~CToolTipEditCombo");
	_ASSERTE(!mListMemDc && !mListWndDC);

	if (S_COMBO(m_partRole) == this)
		S_COMBO(m_partRole) = nullptr;

	delete m_subclasser;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CToolTipEditCombo, CComboBoxEx)
//{{AFX_MSG_MAP(CToolTipEditCombo)
ON_CONTROL_REFLECT(CBN_DROPDOWN, OnDropdown)
ON_CONTROL_REFLECT(CBN_SELCHANGE, OnSelchange)
ON_CONTROL_REFLECT(CBN_SETFOCUS, OnSetfocus)
ON_CONTROL_REFLECT(CBN_SELENDOK, OnSelendok)
ON_CONTROL_REFLECT(CBN_CLOSEUP, OnCloseup)
ON_WM_TIMER()
//}}AFX_MSG_MAP
ON_WM_DRAWITEM()
END_MESSAGE_MAP()
#pragma warning(pop)

static HHOOK keyhook;
static HWND focused_edit;
static DWORD wm_do_context_window_key_action = ::RegisterWindowMessageW(L"WM_DO_CONTEXT_WINDOW_KEY_ACTION");
enum
{
	cwka_select_to_beginning = 100,
	cwka_select_to_ending,
	cwka_move_to_beginning,
	cwka_move_to_ending,
	cwka_select_all
};

static LRESULT CALLBACK KeyboardHook(int code, WPARAM wparam, LPARAM lparam)
{
	if ((code == HC_ACTION) && focused_edit && !(lparam & 0xc0000000))
	{
		const bool shift = !!(::GetAsyncKeyState(VK_SHIFT) & 0x8000);
		const bool alt = !!(::GetAsyncKeyState(VK_MENU) & 0x8000);
		const bool ctrl = !!(::GetAsyncKeyState(VK_CONTROL) & 0x8000);

		switch (wparam)
		{
		case VK_HOME:
			if (shift && !alt && !ctrl)
				::PostMessage(focused_edit, wm_do_context_window_key_action, (WPARAM)cwka_select_to_beginning, 0);
			else if (!shift && !alt && ctrl)
				::PostMessage(focused_edit, wm_do_context_window_key_action, (WPARAM)cwka_move_to_beginning, 0);
			else
				break;
			return 1;
		case VK_END:
			if (shift && !alt && !ctrl)
				::PostMessage(focused_edit, wm_do_context_window_key_action, (WPARAM)cwka_select_to_ending, 0);
			else if (!shift && !alt && ctrl)
				::PostMessage(focused_edit, wm_do_context_window_key_action, (WPARAM)cwka_move_to_ending, 0);
			else
				break;
			return 1;
		case 'A':
			if (!alt && ctrl)
			{
				::PostMessage(focused_edit, wm_do_context_window_key_action, (WPARAM)cwka_select_all, 0);
				return 1;
			}
			break;
		}
	}
	return ::CallNextHookEx(keyhook, code, wparam, lparam);
}

template <typename BASE> class CSubclassWndW : public BASE
{
  public:
	BOOL SubclassWindowW(HWND hWnd)
	{
		if (!BASE::Attach(hWnd))
			return FALSE;

		// allow any other subclassing to occur
		BASE::PreSubclassWindow();

		// now hook into the AFX WndProc
		WNDPROC* lplpfn = BASE::GetSuperWndProcAddr();
		WNDPROC oldWndProc;
		if (IsWindowUnicode(hWnd))
			oldWndProc = (WNDPROC)::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)AfxGetAfxWndProc());
		else
			oldWndProc = (WNDPROC)::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)AfxGetAfxWndProc());

		ASSERT(oldWndProc != AfxGetAfxWndProc());

		if (*lplpfn == NULL)
			*lplpfn = oldWndProc; // the first control of that type created
#ifdef _DEBUG
		else if (*lplpfn != oldWndProc)
		{
			TRACE_((int)traceAppMsg, 0, "Error: Trying to use SubclassWindow with incorrect CWnd\n");
			TRACE_((int)traceAppMsg, 0, "\tderived class.\n");
			// TRACE(traceAppMsg, 0, "\thWnd = $%08X (nIDC=$%08X) is not a %hs.\n", (UINT)(UINT_PTR)hWnd,
			// _AfxGetDlgCtrlID(hWnd), GetRuntimeClass()->m_lpszClassName);
			ASSERT(FALSE);
			// undo the subclassing if continuing after assert
			if (IsWindowUnicode(hWnd))
				::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
			else
				::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
		}
#endif
		return TRUE;
	}

	HWND UnsubclassWindowW()
	{
		ASSERT(::IsWindow(BASE::m_hWnd));

		// set WNDPROC back to original value
		WNDPROC* lplpfn = BASE::GetSuperWndProcAddr();
		if (IsWindowUnicode(BASE::m_hWnd))
			SetWindowLongPtrW(BASE::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		else
			SetWindowLongPtrA(BASE::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		*lplpfn = NULL;

		// and Detach the HWND from the CWnd object
		return BASE::Detach();
	}
};

class ComboEditSubClass : public CSubclassWndW<CWnd>
{
  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_PASTE)
		{
			m_combo->ShowDropDown(TRUE);
			LRESULT r = CWnd::WindowProc(message, wParam, lParam);
			m_combo->KillTimer(kTimerIdFilter);
			m_combo->SetTimer(kTimerIdFilter, 10, NULL);
			return r;
		}

		if (m_combo->HasRole(PartRole::Project))
		{
			if (message == WM_SETCURSOR)
			{
				::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_ARROW));
				return TRUE;
			}

			if (message == WM_LBUTTONDOWN)
			{
				m_combo->ShowDropDown(TRUE);
				return 0;
			}

			if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
				return 0;
		}

		if (message == WM_SETFOCUS)
		{
			if (m_combo->HasRole(PartRole::Context))
				focused_edit = m_hWnd;
		}
		else if (message == WM_KILLFOCUS)
		{
			if (focused_edit == m_hWnd)
				focused_edit = NULL;
		}
		if ((message == wm_do_context_window_key_action) && m_combo->HasRole(PartRole::Context))
		{
			CEdit* edit = m_combo->GetEditCtrl();
			switch (wParam)
			{
			case cwka_select_to_beginning:
				edit->SetSel(0, LOWORD(edit->GetSel()));
				break;
			case cwka_select_to_ending:
				edit->SetSel(LOWORD(edit->GetSel()), -1);
				break;
			case cwka_move_to_beginning:
				edit->SetSel(0, 0);
				break;
			case cwka_move_to_ending:
				edit->SetSel(30000, 30000);
				break;
			case cwka_select_all:
				edit->SetSel(0, -1);
				break;
			}
		}

		if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_COMMAND)
		{
			if (!m_combo->HasRole(PartRole::Context) && message == WM_CHAR)
				return TRUE;

			if (WM_CHAR == message && VK_ESCAPE == wParam && gShellAttr->IsMsdev())
			{
				m_combo->ShowDropDown(FALSE);
				if (g_currentEdCnt)
					g_currentEdCnt->vSetFocus();
				return 0;
			}
			else if (WM_KEYDOWN == message)
			{
				switch (wParam)
				{
				case VK_BACK:
					CWnd::WindowProc(message, wParam, lParam);
					m_combo->KillTimer(kTimerIdFilter);
					m_combo->SetTimer(kTimerIdFilter, 10, NULL);
					return 0;
				case VK_ESCAPE:
					m_combo->ShowDropDown(FALSE);
					if (g_currentEdCnt)
						g_currentEdCnt->vSetFocus();
					return 0;
				case VK_TAB:
				case VK_RETURN:
					m_combo->GotoCurMember();
					return 0;
				case VK_UP:
				case VK_DOWN:
					m_combo->ShowDropDown(TRUE);
					return m_combo->GetComboBoxCtrl()->SendMessage(message, wParam, lParam);
				}
			}

			LRESULT res = CWnd::WindowProc(message, wParam, lParam);
			if (wParam != 0x8215 && wParam != VK_SHIFT // prevent flicker holding Shift
			    && wParam != VK_CONTROL && wParam != VK_LWIN && wParam != VK_RWIN && wParam != VK_APPS &&
			    wParam != VK_HOME && wParam != VK_LEFT && wParam != VK_RIGHT && wParam != VK_END)
			{
				m_combo->KillTimer(kTimerIdFilter);
				m_combo->SetTimer(kTimerIdFilter, 10, NULL);
			}
			return res;
		}

		// combos on Vista sometime refresh internally on various messages (WM_SETFOCUS and WM_LMOUSEMOVE, for example)
		int clr = (m_combo && m_combo->HasColorableText()) ? PaintType::WizardBar : PaintType::DontColor;
		VAColorPaintMessages m(message, clr);
		LRESULT r = CWnd::WindowProc(message, wParam, lParam);
		return r;
	}

  public:
	void Init(CToolTipEditCombo* combo)
	{
		m_combo = combo;
		if (combo->GetEditCtrl())
			SubclassWindowW(combo->GetEditCtrl()->m_hWnd);

		if (!keyhook)
			keyhook = ::SetWindowsHookExA(WH_KEYBOARD, KeyboardHook, NULL, ::GetCurrentThreadId());
	}
	CToolTipEditCombo* m_combo;
};

COLORREF GetVS2010VANavBarBkColour()
{
	// gmit: we won't use gradients since we have to draw background in three nontrivial steps
	const COLORREF vanavbar_top_background_cache =
	    CVS2010Colours::GetVS2010Colour(VSCOLOR_ENVIRONMENT_BACKGROUND_GRADIENTBEGIN);
	const COLORREF vanavbar_bottom_background_cache =
	    CVS2010Colours::GetVS2010Colour(VSCOLOR_ENVIRONMENT_BACKGROUND_GRADIENTMIDDLE1);
	return (Psettings && Psettings->minihelpAtTop) ? vanavbar_top_background_cache : vanavbar_bottom_background_cache;
}

class ComboPopupSubClass : public CWnd
{
	DECLARE_MENUXP()
	DECLARE_DYNAMIC(ComboPopupSubClass)
  public:
	ComboPopupSubClass(bool supportsMenu) : mCombo(NULL), mSupportsMenu(supportsMenu), mInPaint(false)
	{
	}

	void Init(CToolTipEditCombo* combo)
	{
		mCombo = combo;
		COMBOBOXINFO cbi;
		memset(&cbi, 0, sizeof(cbi));
		cbi.cbSize = sizeof(cbi);
		_ASSERTE(combo->GetComboBoxCtrl());
		combo->GetComboBoxCtrl()->GetComboBoxInfo(&cbi);
		_ASSERTE(cbi.hwndList && ::IsWindow(cbi.hwndList));
		if (cbi.hwndList && ::IsWindow(cbi.hwndList))
			SubclassWindow(cbi.hwndList);
	}

	void SetDropRect(const CRect& rect)
	{
		mDropRect = rect;
	}
  protected:
	LRESULT CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam);
	LRESULT WindowProc(UINT message, WPARAM wparam, LPARAM lparam);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);


	CToolTipEditCombo* mCombo;
	bool mSupportsMenu;
	bool mInPaint;
	CRect mDropRect;

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(ComboPopupSubClass, CWnd);

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(ComboPopupSubClass, CWnd)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()
#pragma warning(pop)

IMPLEMENT_MENUXP(ComboPopupSubClass, CWnd);

LRESULT ComboPopupSubClass::CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam)
{
	return CWnd::WindowProc(message, wparam, lparam);
}

LRESULT ComboPopupSubClass::WindowProc(UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_WINDOWPOSCHANGING && lparam && !mDropRect.IsRectNull())
	{
		auto pWP = (LPWINDOWPOS)lparam;
		pWP->x = mDropRect.left;
		pWP->y = mDropRect.top;
		pWP->cx = mDropRect.Width();
		pWP->cy = mDropRect.Height();
	}

	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		// [case: 85333]
		// all drawing handled here instead of in ComboVS2010_ComboPopup_WndProc
		// to deal with flicker
		if (WM_PAINT == message && CVS2010Colours::IsExtendedThemeActive())
		{
			if (!mInPaint)
			{
				mInPaint = true;
				mCombo->BeginPopupListPaint(this);
				__super::WindowProc(message, wparam, lparam);
				mCombo->EndPopupListPaint();
				mInPaint = false;
			}
			return 0;
		}
		else if (WM_ERASEBKGND == message && CVS2010Colours::IsExtendedThemeActive())
		{
			if (mInPaint)
			{
				// fix flicker when typing filter
				return 1;
			}

			if (wparam)
			{
				// this prevents flash of COLOR_WINDOW (white) when list is first dropped
				HDC hdc = (HDC)wparam;
				CRect rect;
				GetClientRect(&rect);
				CBrush br;
				bool readonly_combo(mCombo->readonly_combo);
				if (br.CreateSolidBrush(GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN)))
					::FillRect(hdc, rect, br);
				return 1;
			}
		}
		else if (WM_NCPAINT == message && CVS2010Colours::IsExtendedThemeActive())
		{
			const uint style = GetWindowStyle(m_hWnd);
			// call base for scrollbar theming but only if there is a
			// scrollbar because it can cause flicker of list border (it
			// will cause border to be drawn in wndclass brush(?) and then
			// we fix it below.  No good having us draw it first - they just
			// overwrite it.  Most noticeable on win8.
			if (style & WS_VSCROLL || style & WS_HSCROLL)
				__super::WindowProc(message, wparam, lparam);

			CWindowDC dc(this);
			CSaveDC savedc(dc);

			// calculate client rect in non-client coordinates
			CRect crect;
			GetClientRect(crect);
			ClientToScreen(crect);
			CRect wrect;
			GetWindowRect(wrect);

			// draw border
			CRect brect = wrect;
			brect.MoveToXY(0, 0);
			CRgn brgn;
			if (brgn.CreateRectRgnIndirect(brect))
				dc.SelectObject(&brgn);
			dc.SetBkMode(TRANSPARENT);
			::SelectObject(dc.m_hDC, ::GetStockObject(HOLLOW_BRUSH));
			bool readonly_combo(mCombo->readonly_combo);
			const COLORREF popup_border_colour = GetVS2010ComboColour(POPUP_BORDER);
			CPen pen;
			if (pen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, popup_border_colour))
			{
				dc.SelectObject(&pen);
				dc.Rectangle(brect);
			}

			return 0;
		}
		else if (WM_NCACTIVATE == message && CVS2010Colours::IsExtendedThemeActive())
			return FALSE;
		else if (WM_PRINT == message && CVS2010Colours::IsExtendedThemeActive())
			return 0;
		else if (WM_PRINTCLIENT == message && CVS2010Colours::IsExtendedThemeActive())
			return 0;

		std::optional<LRESULT> ret = ComboVS2010_ComboPopup_WndProc(
		    *this, message, wparam, lparam, mCombo->readonly_combo, mCombo->popup_background_gradient_cache,
		    std::bind(&ComboPopupSubClass::CallBaseWndProcHelper, this, message, wparam, lparam));
		if (ret)
			return *ret;
	}

	return __super::WindowProc(message, wparam, lparam);
}

void ComboPopupSubClass::OnContextMenu(CWnd* pWnd, CPoint pos)
{
	if (!mSupportsMenu)
		return;

	sMifMenuActive = true;
	mCombo->ShowDropDown(FALSE);

	enum
	{
		CmdAlphaSort = 1,
		CmdShowParams,
		CmdShowRegions,
		CmdShowScope,
		CmdShowDefines,
		CmdShowMembers,
		CmdShowProperties,
		CmdShowEvents,
		CmdFilterNames
	};

	PopupMenuXP xpmenu;
	xpmenu.AddMenuItem(CmdAlphaSort, MF_BYPOSITION | (Psettings->m_sortDefPickList ? MF_CHECKED : 0u),
	                   "&Sort alphabetically");
	xpmenu.AddMenuItem(CmdShowParams, MF_BYPOSITION | (Psettings->mParamsInMethodsInFileList ? MF_CHECKED : 0u),
	                   "&Display method parameters");
	xpmenu.AddMenuItem(CmdShowDefines, MF_BYPOSITION | (Psettings->mMethodInFile_ShowDefines ? MF_CHECKED : 0u),
	                   "Include de&fines");
	xpmenu.AddMenuItem(CmdShowEvents, MF_BYPOSITION | (Psettings->mMethodInFile_ShowEvents ? MF_CHECKED : 0u),
	                   "Include &events");
	xpmenu.AddMenuItem(CmdShowMembers, MF_BYPOSITION | (Psettings->mMethodInFile_ShowMembers ? MF_CHECKED : 0u),
	                   "Include &members");
	xpmenu.AddMenuItem(CmdShowProperties, MF_BYPOSITION | (Psettings->mMethodInFile_ShowProperties ? MF_CHECKED : 0u),
	                   "Include &properties");
	xpmenu.AddMenuItem(CmdShowRegions, MF_BYPOSITION | (Psettings->mMethodInFile_ShowRegions ? MF_CHECKED : 0u),
	                   "&Include regions");
	xpmenu.AddMenuItem(CmdShowScope, MF_BYPOSITION | (Psettings->mMethodInFile_ShowScope ? MF_CHECKED : 0u),
	                   "Include s&cope");
	xpmenu.AddMenuItem(CmdFilterNames,
	                   MF_BYPOSITION | (Psettings->mMethodsInFileNameFilter ? MF_CHECKED : 0u) |
	                       (Psettings->mMethodInFile_ShowScope ? 0 : MFS_DISABLED),
	                   "&Reduce display of namespace scopes");

	const int result = xpmenu.TrackPopupMenuXP(gMainWnd, pos.x, pos.y);
	sMifMenuActive = false;
	switch (result)
	{
	case CmdAlphaSort:
		Psettings->m_sortDefPickList = !Psettings->m_sortDefPickList;
		break;
	case CmdShowRegions:
		Psettings->mMethodInFile_ShowRegions = !Psettings->mMethodInFile_ShowRegions;
		break;
	case CmdShowDefines:
		Psettings->mMethodInFile_ShowDefines = !Psettings->mMethodInFile_ShowDefines;
		break;
	case CmdShowScope:
		Psettings->mMethodInFile_ShowScope = !Psettings->mMethodInFile_ShowScope;
		break;
	case CmdShowMembers:
		Psettings->mMethodInFile_ShowMembers = !Psettings->mMethodInFile_ShowMembers;
		break;
	case CmdShowProperties:
		Psettings->mMethodInFile_ShowProperties = !Psettings->mMethodInFile_ShowProperties;
		break;
	case CmdShowEvents:
		Psettings->mMethodInFile_ShowEvents = !Psettings->mMethodInFile_ShowEvents;
		break;
	case CmdShowParams:
		Psettings->mParamsInMethodsInFileList = !Psettings->mParamsInMethodsInFileList;
		break;
	case CmdFilterNames:
		Psettings->mMethodsInFileNameFilter = !Psettings->mMethodsInFileNameFilter;
		break;
	default:
		return;
	}

	// drop popup
	mCombo->SetWindowText("");
	mCombo->GetEditCtrl()->SetFocus();
	mCombo->ShowDropDown(TRUE);
}

class DisableComboBoxAnimation
{
  public:
	DisableComboBoxAnimation()
	{
		changed = false;
		if (gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive())
		{
			BOOL enabled = false;
			::SystemParametersInfo(SPI_GETCOMBOBOXANIMATION, 0, &enabled, 0);
			if (enabled)
			{
				::SystemParametersInfo(SPI_SETCOMBOBOXANIMATION, 0, (void*)false, 0);
				changed = true;
			}
		}
	}
	virtual ~DisableComboBoxAnimation()
	{
		if (changed)
			::SystemParametersInfo(SPI_SETCOMBOBOXANIMATION, 0, (void*)true, 0);
	}

  protected:
	bool changed;
};

class ComboSubClass : public ComboPaintSubClass
{
	DECLARE_DYNAMIC(ComboSubClass)
  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_CHAR)
		{
			static BOOL inChar = FALSE;
			if (!inChar && m_combo->GetEditCtrl())
			{
				inChar = TRUE;
				m_combo->GetEditCtrl()->SetWindowText("");
				m_combo->GetEditCtrl()->SetFocus();
				LRESULT res = m_combo->GetEditCtrl()->SendMessage(message, wParam, lParam);
				inChar = FALSE;
				return res;
			}
		}
		if (message == WM_MOUSEMOVE)
		{
			LRESULT r = ComboPaintSubClass::WindowProc(message, wParam, lParam);
			Invalidate(FALSE);
			return r;
		}
		if (message == wm_postponed_set_focus)
		{
			m_combo->GetEditCtrl()->SetFocus();
			return 0;
		}

		if (message == WM_SETCURSOR && m_combo->HasRole(PartRole::Project))
		{
			::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_ARROW));
			return TRUE;
		}

		std::shared_ptr<DisableComboBoxAnimation> disable_animation;
		if (message == WM_LBUTTONDOWN)
		{
			COMBOBOXINFO cbi;
			memset(&cbi, 0, sizeof(cbi));
			cbi.cbSize = sizeof(cbi);
			GetComboBoxInfo(*this, &cbi);

			if (CRect(cbi.rcButton).PtInRect(CPoint(lParam)))
				disable_animation.reset(new DisableComboBoxAnimation);
		}

		VAColorPaintMessages m(message, PaintType::WizardBar);
		return ComboPaintSubClass::WindowProc(message, wParam, lParam);
	}

  public:
	void Init(CToolTipEditCombo* combo)
	{
		SetVS2010ColouringActive(CVS2010Colours::IsVS2010NavBarColouringActive());
		m_combo = combo;
		SubclassWindow(combo->GetComboBoxCtrl()->m_hWnd);
	}

	CToolTipEditCombo* m_combo;

	DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNAMIC(ComboSubClass, ComboPaintSubClass)
BEGIN_MESSAGE_MAP(ComboSubClass, ComboPaintSubClass)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditCombo message handlers
void GotoMethodList()
{
	if (Psettings->m_noMiniHelp)
	{
		MessageBeep(0xffffffff);
		SetStatus("'List Methods' is dependent upon the 'Context and Definition' fields which are currently disabled.");
		return;
	}

	auto ctxCombo = GetToolTipEditCombo(PartRole::Context);
	if (ctxCombo && g_currentEdCnt && ctxCombo->GetEditCtrl())
	{
		if (ctxCombo->IsWindowVisible())
		{
			ctxCombo->ClearEditAndDrop();
		}
		else
		{
			Log("GotoMethodList called on hidden minihelp");
			SetStatus("'List Methods' is not available in this view.");
		}
	}
	else
	{
#ifdef _DEBUG
		WTString msg;
		msg.WTFormat("DEBUG: %p, %p, %d, %p", ctxCombo, g_currentEdCnt.get(), ctxCombo->GetContextList(),
		             ctxCombo->GetEditCtrl());
		SetStatus(msg);
		WtMessageBox(msg, "GotoList broken or not attached to current window", MB_OK);
#endif // _DEBUG
	}
}

bool IsMinihelpDropdownVisible()
{
	for (auto& cb : s_combos)
		if (cb && cb->GetSafeHwnd() && cb->GetDroppedState())
			return true;

	return false;
}

void HideMinihelpDropdown()
{
	for (auto& cb : s_combos)
	{
		if (cb && cb->GetSafeHwnd() && cb->GetDroppedState())
		{
			cb->SendMessage(CB_SHOWDROPDOWN, FALSE);
			cb->PostMessage(CB_SHOWDROPDOWN, FALSE);
		}	
	}

	::Sleep(150);
	EdCntPtr ed = g_currentEdCnt;
	if (ed)
		ed->PostMessage(wm_postponed_set_focus);
	::Sleep(150);
}

struct CToolTipEditComboSubclasser : public CToolTipEditCombo::ISubclasser
{
	CToolTipEditComboSubclasser(bool supportMenu)
	    : edit(), popup(supportMenu), combo()
	{
	}

	~CToolTipEditComboSubclasser() override 
	{
		_ASSERTE(!IsSubclassed());
	}

	ComboEditSubClass edit;
	ComboPopupSubClass popup;
	ComboSubClass combo;

	bool IsSubclassed() const override
	{
		return ::IsWindow(edit.m_hWnd);
	} 

	void Subclass(CToolTipEditCombo* ttecb) override
	{
		if (ttecb->HasRole(PartRole::Context))
		{
			if (!IsSubclassed())
			{
				edit.Init(ttecb);
				ttecb->readonly_combo = false;
				combo.Init(ttecb);
				popup.Init(ttecb);
			}
		}
		else
		{
			if (!IsSubclassed())
			{
				edit.Init(ttecb);
				if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive())
				{
					// [case: 71196] this affects mouse over coloring
					ttecb->readonly_combo = false;
				}
				else
					ttecb->readonly_combo = true;
				combo.Init(ttecb);
				combo.SetVS2010ColouringActive(CVS2010Colours::IsVS2010NavBarColouringActive());
				popup.Init(ttecb);
			}
		}
	}

	void Unsubclass() override
	{
		if (edit.GetSafeHwnd() && ::IsWindow(edit.m_hWnd))
			edit.UnsubclassWindowW();

		if (combo.GetSafeHwnd() && ::IsWindow(combo.m_hWnd))
			combo.UnsubclassWindow();

		if (popup.GetSafeHwnd() && ::IsWindow(popup.m_hWnd))
			popup.UnsubclassWindow();
	}

	void SetDropRect(const CRect& rect) override
	{
		popup.SetDropRect(rect);
	}
};


void CToolTipEditCombo::OnDropdown()
{
	if (g_currentEdCnt)
		g_currentEdCnt->KillTimer(ID_TIMER_MOUSEMOVE);
	else
		return;

	CheckIDENavigationBar();

	bool getList = HasRole(PartRole::Context, PartRole::Project);

	if (getList && !mFilteringActive)
	{
		CStringW tmp;
		::GetWindowTextW(GetEditCtrl()->GetSafeHwnd(), tmp);
		if (!tmp.IsEmpty())
		{
			AutoLockCs l(mTextLock);
			mTextOnDrop = tmp;
		}

		SetWindowText(""); // [case: 38841] clear text before populating list so that MRU works

		if (HasRole(PartRole::Context))
			GetContextList();
		else if (HasRole(PartRole::Project))
			GetProjectList();
		
		if (GetCount())
		{
			GetComboBoxCtrl()->PostMessage(wm_postponed_set_focus);
		}
		else
		{
			{
				AutoLockCs l(mTextLock);
				mTextOnDrop.Empty();
			}
			COMBOBOXEXITEMW cbi;
			ZeroMemory(&cbi, sizeof(COMBOBOXEXITEMW));
			SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi);
			SetWindowText("");
		}
	}

	if (getList)
		DoMultipleDPIsWorkaround(m_hWnd);

	RecalcDropWidth();
}

void CToolTipEditCombo::OnSelchange()
{
	if (GetEditCtrl() && !HasRole(PartRole::Context, PartRole::Project))
		GetEditCtrl()->SetSel(0, 0);
	if (!GetDroppedState() && GetFocus() == GetEditCtrl() && !(GetKeyState(VK_ESCAPE) & 0x1000))
	{
		if (!gShellAttr
		         ->IsDevenv10OrHigher()) // Alt+M, the ALT again causes list to endlessly update in vs10.  case=30842
			ShowDropDown(TRUE);
	}
}

INT CToolTipEditCombo::GetContextList(const WTString filter /*= WTString()*/)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
	{
		ResetContent();
		return 0;
	}

	mCurFilter = StrMatchOptions::TweakFilter(filter);

	CStringW curTextW;
	int curSel = -1;
	if (GetEditCtrl())
		::GetWindowTextW(GetEditCtrl()->GetSafeHwnd(), curTextW);

	ResetContent();

	ed->ClearMinihelp(false);
	// #seanPerformanceHotSpotMif
	// 2/3 of mshtml.h time is the parse
	LineMarkersPtr mkrs = mGotoMarkers = ed->GetMethodsInFile();
	if (!mkrs || !mkrs->Root().GetChildCount())
		return 0;

	StrMatchOptions opts(mCurFilter);
	const bool kIsMultiPattern = StrIsMultiMatchPattern(mCurFilter);
	int itemCnt = 0;
	COMBOBOXEXITEMW cbi;
	ZeroMemory(&cbi, sizeof(COMBOBOXEXITEM));
	cbi.mask = CBEIF_IMAGE | CBEIF_TEXT | CBEIF_LPARAM;
	cbi.iItem = -1;

	CStringW curItemText;
	for (uint i = 0; i < mkrs->Root().GetChildCount(); ++i)
	{
		const FileLineMarker& mkr = *(mkrs->Root().GetChild(i));
		curItemText = mkr.mText;
		if (curItemText.IsEmpty())
			continue;

		curItemText.Replace(L"::", L".");

		if (mCurFilter.GetLength())
		{
			if (kIsMultiPattern)
			{
				if (!::StrMultiMatchRanked(curItemText, mCurFilter, false))
					continue;
			}
			else
			{
				if (!::StrMatchRankedW(curItemText, opts, false))
					continue;
			}
		}

		cbi.pszText = (LPWSTR)(LPCWSTR)mkr.mText;
		if (curTextW == mkr.mText)
			curSel = itemCnt;
		itemCnt++;
		cbi.iImage = mkr.GetIconIdx();
		cbi.lParam = reinterpret_cast<LPARAM>(&mkr);
		// #seanPerformanceHotSpotMif
		// 1/3 of mshtml.h time is due to CBEM_INSERTITEMW; no GetDispInfo callback?
		SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi);

		if (gTestLogger)
		{
			WTString msg;
			msg.WTFormat("Alt+M: [%d] %s", cbi.iImage, WTString(mkr.mText).c_str());
			gTestLogger->LogStr(msg);
		}
	}

	if (g_VA_MRUs && Psettings->mSelectRecentItemsInNavigationDialogs)
	{
		if (itemCnt < 300) // Don't slow down huge lists
		{
			const DWORD startTime = ::GetTickCount();
			for (std::list<WTString>::iterator it = g_VA_MRUs->m_MIF.begin(); it != g_VA_MRUs->m_MIF.end(); it++)
			{
				const CStringW method((*it).Wide());
				for (int i = 0; i < itemCnt; ++i)
				{
					FileLineMarker* mkr = reinterpret_cast<FileLineMarker*>(GetItemData(i));
					if (mkr && mkr->mText == method)
					{
						SetCurSel(i);
						return 1;
					}
					else if (!(i % 100))
					{
						if ((::GetTickCount() - startTime) > 300)
							break;
					}
				}
			}
		}
	}

	if (curSel != -1 && mCurFilter.IsEmpty())
		SetCurSel(curSel);
	else
		SetCurSel(0);
	return 1;
}

INT CToolTipEditCombo::GetProjectList()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
	{
		ResetContent();
		return 0;
	}

	CStringW curTextW = ed->ProjectName();
	int curSel = -1;

	ed->ClearMinihelp(false);

	if (GetEditCtrl())
		GetEditCtrl()->SetWindowTextA("");

	ResetContent();

	int count = GetCount();
	for (int i = 0; i < count; i++)
		DeleteItem(0);

	if (!mProjects)
		mProjects = std::make_unique<std::vector<std::tuple<CStringW, int>>>();

	mProjects->clear();
	GetProjectNamesForFile(ed->FileName(), *mProjects);

	int itemCnt = 0;
	COMBOBOXEXITEMW cbi;
	ZeroMemory(&cbi, sizeof(COMBOBOXEXITEM));
	cbi.mask = CBEIF_IMAGE | CBEIF_TEXT | CBEIF_LPARAM;
	cbi.iItem = -1;

	for (uint i = 0; i < mProjects->size(); ++i)
	{
		auto curItem = mProjects->at(i);

		if (std::get<0>(curItem).IsEmpty())
			continue;

		cbi.pszText = (LPWSTR)(LPCWSTR)std::get<0>(curItem);
		if (curTextW == std::get<0>(curItem))
			curSel = itemCnt;
		itemCnt++;
		cbi.iImage = std::get<1>(curItem);
		cbi.lParam = (LPARAM)i;

		SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi);
	}

	if (curSel != -1 && mCurFilter.IsEmpty())
		SetCurSel(curSel);
	else
		SetCurSel(0);

	return 1;
}

void CToolTipEditCombo::OnSetfocus()
{
}

LRESULT CToolTipEditCombo::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if ((message == WM_PAINT || message == WM_DRAWITEM) && GetEditCtrl())
	{
		m_subclasser->Subclass(this);
	}
	if (message == WM_DRAWITEM)
	{
		// if windows animation are enabled, listbox will draw to a tmpdc and not be colored
		VAColorPaintMessages m(message, PaintType::WizardBar);
		return CComboBoxEx::DefWindowProc(message, wParam, lParam);
	}
	if (message == WM_DESTROY)
	{
		ResetContent();
		m_subclasser->Unsubclass();
	}
	return CComboBoxEx::DefWindowProc(message, wParam, lParam);
}

void CToolTipEditCombo::RecalcDropWidth()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	// Reset the dropped width
	const int nNumEntries = min(GetCount(), 500);
	int nWidth = 0;
	CStringW str;

	{
		CClientDC dc(this);
		int nSave = dc.SaveDC();
		dc.SelectObject(GetFont());
		int nScrollWidth = ::GetSystemMetrics(SM_CXVSCROLL) + VsUI::DpiHelper::LogicalToDeviceUnitsX(25);
		for (int i = 0; i < nNumEntries; i++)
		{
			SIZE size;
			if (GetLBTextW(i, str) && ::GetTextExtentPoint32W(dc, str, str.GetLength(), &size))
			{
				int nLength = size.cx + nScrollWidth;
				nWidth = max(nWidth, nLength);
			}
		}

		// Add margin space to the calculations
		nWidth += dc.GetTextExtent("0").cx;

		dc.RestoreDC(nSave);
	}

	// gmit: 67174: GetDesktopEdges already works satisfactorily
	const CRect desktoprect = g_FontSettings->GetDesktopEdges(ed->m_hWnd);

	CRect rc;
	GetWindowRect(&rc);
	if (Psettings->minihelpAtTop || Psettings->m_noMiniHelp)
	{
		rc.top = rc.top + 2 * (int)Psettings->m_minihelpHeight;
		rc.bottom = desktoprect.bottom;
	}
	else
	{
		rc.top = 0;
		rc.bottom = rc.bottom - (int)Psettings->m_minihelpHeight;
	}

	if (nNumEntries)
	{
		const int rightEdge = rc.left + nWidth;
		if (rightEdge > desktoprect.right)
		{
			const int diff = rightEdge - desktoprect.right - 2;
			nWidth -= diff;
		}

		if (Psettings->m_noMiniHelp)
		{
			CRect rc2;
			ed->GetWindowRect(&rc2);
			CPoint pt(rc2.left, Psettings->minihelpAtTop ? rc2.top : rc2.bottom);
			::ScreenToClient(MainWndH, &pt);
			if (GetParent())
			{
				// this isn't working right - depends on order of options setting changes
				// sometimes listbox is moved offscreen - works fine if minihelp is
				// hidden at IDE startup - problematic if it is hidden and not restarted
				// Part of the problem is the rect of the listbox when minihelp is
				// at the bottom - sometimes inverted sometimes not.
				GetParent()->GetWindowRect(&rc2);
				if (Psettings->minihelpAtTop)
					pt.y -= rc2.Height();
				else
					pt.y += rc2.Height();
			}
			MoveWindowIfNeeded(GetComboBoxCtrl(), pt.x, pt.y, rc.Width(), rc.Height(), TRUE);
		}
		else if (Psettings->minihelpAtTop)
			MoveWindowIfNeeded(GetComboBoxCtrl(), 0, 0, rc.Width(), rc.Height(), TRUE);

		SetDroppedWidth((uint)nWidth);
	}
	else
	{
		if (Psettings->minihelpAtTop)
			MoveWindowIfNeeded(GetComboBoxCtrl(), 0, 0, rc.Width(), 25, TRUE);
		// gmit: commented out for case 20630 (closed combo if initially no matches were present)
		//		::PostMessage(m_hWnd, CB_SHOWDROPDOWN, FALSE, 0);
	}

	// [case: 18514] [case: 76431]
	// the above MoveWindow and SetWidth calls cause
	// the icons to not appear once the list closes
	// This fixes it - similar to CToolTipEditView::OnSize (using the splitter
	// fixes it manually)
	GetWindowRect(&rc);
	MoveWindowIfNeeded(this, -2, -2, rc.Width(), rc.Height());

	// [case: 150021]
	AdjustDropRectangle(desktoprect, nWidth);
}

void CToolTipEditCombo::AdjustDropRectangle(const CRect& restrictTo, int dropWidth)
{
	// [case: 150021] fix tiny dropdown when minihelp is not on top

	if (!m_subclasser)
		return;

	auto combo = GetComboBoxCtrl();
	_ASSERTE(combo);

	if (!combo)
		return;

	auto dpiScope = SetDefaultDpiHelper();

	const int magicGap = 2; // DPI independent

	int itemHeight = GetItemHeight(-1);
	if (itemHeight == 0)
	{
		CVAMeasureItem measure(this);
		itemHeight = measure.MeasureItemHeight();

		_ASSERTE(itemHeight > 0);

		if (itemHeight == 0)
			itemHeight = VsUI::DpiHelper::LogicalToDeviceUnitsY(25);
	}

	int itemCount = GetCount();
	if (itemCount <= 0)
		itemCount = 1;

	// if we can have good visible count, prefer the bottom drop
	int goodBottomCount = 5;
// 	if (itemCount > 50)
// 		goodBottomCount = itemCount / 5;

	CRect rc;
	GetWindowRect(&rc);

	if (dropWidth < rc.Width())
		dropWidth = rc.Width();

	// define the ideal drop rectangle
	CRect dropRc(rc);
	dropRc.right = dropRc.left + dropWidth;
	dropRc.top = rc.bottom;
	dropRc.bottom = dropRc.top + (itemHeight * itemCount + magicGap);

	// restrict the drop rectangle under the combobox
	CRect rcBottom(restrictTo);
	rcBottom.top = rc.bottom;
	CRect dropBottom(dropRc);
	ThemeUtils::Rect_AdjustToFit(dropBottom, rcBottom);
	int visibleBottom = dropBottom.Height() / itemHeight;
	dropBottom.bottom = dropBottom.top + (visibleBottom * itemHeight + magicGap);
	
	// if restricted rectangle under combobox is good, use it
	if (dropBottom == dropRc || visibleBottom >= goodBottomCount)
	{
		dropRc = dropBottom;
	}
	else 
	{
		// restrict the drop rectangle above the combobox
		CRect rcTop(restrictTo);
		rcTop.bottom = rc.top;
		CRect dropTop(dropRc);
		ThemeUtils::Rect_AdjustToFit(dropTop, rcTop);
		dropTop.top = dropTop.bottom - ((dropTop.Height() / itemHeight) * itemHeight + magicGap);

		// if upper rectangle is better than bottom, use upper one
		if (dropTop.Height() == dropRc.Height() || dropBottom.Height() < dropTop.Height())
			dropRc = dropTop;
		else
			dropRc = dropBottom;
	}

 	if (m_subclasser)
 		m_subclasser->SetDropRect(dropRc);	// used in ComboPopupSubClass::WindowProc
}

void CToolTipEditCombo::OnSelendok()
{
	if (HasRole(PartRole::Context, PartRole::Project) && !(GetKeyState(VK_ESCAPE) & 0x1000))
		GotoCurMember();
}

void CToolTipEditCombo::GotoCurMember()
{
	if (GetDroppedState())
	{
		if (HasRole(PartRole::Project))
		{
			ShowDropDown(FALSE);
			const int item = GetCurSel();

			EdCntPtr ed = g_currentEdCnt;
			if (mProjects && ed)
			{
				const size_t projId = (size_t)(LONG_PTR)(item);
				if (projId < mProjects->size())
				{
					CStringW proj = std::get<0>(mProjects->at(projId));
					
					if (ed->ProjectName() != proj)
					{
						OpenFileInSpecificProject(proj, ed.get());
					}
				}
			}
		}
		else
		{
			LineMarkersPtr mkrs(mGotoMarkers);
			const int item = GetCurSel();
			FileLineMarker* mkr = reinterpret_cast<FileLineMarker*>(GetItemData(item));
			if (mkr == (FileLineMarker*)(LONG_PTR)-1)
				mkr = nullptr; // alt+m to drop list, then alt+tab to another program results in -1 return from GetItemData
			int lineNo = -1;
			try
			{
				if (mkr)
					lineNo = (int)mkr->mGotoLine;
			}
			catch (...)
			{
				mkr = nullptr;
			}

			if (-1 != lineNo)
			{
				CString txt;
				GetLBText(item, txt);

				if (txt.GetLength())
				{
					// Get item image
					COMBOBOXEXITEMA cbi;
					ZeroMemory(&cbi, sizeof(cbi));
					cbi.iItem = item;
					cbi.mask = CBEIF_IMAGE;
					GetItem(&cbi);

					extern void SetMRUItem(LPCSTR name, int type);
					if (g_VA_MRUs)
						g_VA_MRUs->m_MIF.AddToTop(WTString(txt));
					SetMRUItem(txt, cbi.iImage);
				}
			}

			ShowDropDown(FALSE);

			if (-1 != lineNo)
			{
				EdCntPtr ed(g_currentEdCnt);
				if (ed) // build 1845 WER -1925971020, no dump
				{
					ed->vSetFocus();
					if (item != -1)
					{
						WTString sym;
						sym = mkr->mSelectText;
						// [case: 144646] added last "func" param to support selection when jumping onto constructors:
						// selection after the jump selects the first instance of the method's name By telling DelayFileOpen
						// that we are dealing with a method, we enable a selection method that works well on methods,
						// including constructors, where the method name might be the second instance of the same string
						DelayFileOpen(ed->FileName(), lineNo, sym, FALSE, mkr->mType == FUNC);
					}
				}
			}
		}

		if (GetEditCtrl())
			GetEditCtrl()->SetWindowText("");

		ResetContent();

		int count = GetCount();
		for (int i = 0; i < count; i++)
			DeleteItem(0);
	}
}

void CToolTipEditCombo::FilterList()
{
	if (!GetEditCtrl() || !g_currentEdCnt)
		return;

	int p1, p2;
	GetEditCtrl()->GetSel(p1, p2);
	if (!mFilteringActive && HasRole(PartRole::Context) && GetFocus() == GetEditCtrl() && p1 == p2)
	{
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		mFilteringActive = true;
		WTString lTxt;
		DWORD edsel = GetEditCtrl()->GetSel();
		CStringW curTxt;
		::GetWindowTextW(GetEditCtrl()->GetSafeHwnd(), curTxt);
		lTxt = curTxt;
		lTxt.MakeLower();
		GetComboBoxCtrl()->SetRedraw(FALSE);
		GetEditCtrl()->SetRedraw(FALSE);
		GetContextList(lTxt);
		if (!g_VA_MRUs || !Psettings->mSelectRecentItemsInNavigationDialogs)
			SetCurSel(0);
		if (!GetDroppedState())
			ShowDropDown(TRUE);
		::SetWindowTextW(GetEditCtrl()->GetSafeHwnd(), curTxt);

		SetColorableText(false);
		GetEditCtrl()->SetSel(edsel, 0);
		GetEditCtrl()->SetFocus();
		GetComboBoxCtrl()->SetRedraw(TRUE);
		GetEditCtrl()->SetRedraw(TRUE);
		GetComboBoxCtrl()->Invalidate(TRUE);
		mFilteringActive = false;
	}
}

void CToolTipEditCombo::OnCloseup()
{
	g_IgnoreBeepsTimer = GetTickCount() + 1000; // ignore beep in vc6

	if (HasRole(PartRole::Context, PartRole::Project) && GetEditCtrl() && !sMifMenuActive)
	{
		CStringW txt;

		{
			AutoLockCs l(mTextLock);
			txt = mTextOnDrop;
		}

		::SetWindowTextW(GetEditCtrl()->GetSafeHwnd(), txt);
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
			ed->PostMessage(wm_postponed_set_focus);
	}
}

void CToolTipEditCombo::OnTimer(UINT_PTR nIDEvent)
{
	if (kTimerIdFilter == nIDEvent)
	{
		KillTimer(nIDEvent);
		FilterList();
	}
	else if (kTimerIdInvalidate == nIDEvent)
	{
		KillTimer(nIDEvent);

		CComboBox* pBox = GetComboBoxCtrl();
		if (pBox)
		{
			COMBOBOXINFO cbi;
			ZeroMemory(&cbi, sizeof(cbi));
			cbi.cbSize = sizeof(cbi);
			pBox->GetComboBoxInfo(&cbi);
			if (cbi.hwndList)
				::InvalidateRect(cbi.hwndList, nullptr, TRUE);
		}
	}
}

COLORREF CToolTipEditCombo::GetVS2010ComboBackgroundColour() const
{
	if (!combo_background_cache)
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			combo_background_cache = g_IdeSettings->GetEnvironmentColor(L"DropDownBackground", false);
		else
			combo_background_cache = GetVS2010ComboColour(BACKGROUND);
	}
	return *combo_background_cache;
}

bool CToolTipEditCombo::ComboDrawsItsOwnBorder() const
{
	return false;
}

LRESULT CToolTipEditCombo::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_SIZE && g_pMiniHelpFrm)
	{
		g_pMiniHelpFrm->UpdateHeight();
	}

	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		std::optional<LRESULT> ret = ComboVS2010_CComboBoxEx_WndProc(m_hWnd, message, wParam, lParam,
		                                                             GetVS2010ComboBackgroundColour(), bgbrush_cache);
		if (ret)
			return *ret;
	}

	return __super::WindowProc(message, wParam, lParam);
}

void CToolTipEditCombo::OnDrawItem(int id, LPDRAWITEMSTRUCT dis)
{
	// DrawItem virtual function is no good; all items in comboboxex are customdrawn, so we don't have to put flag for
	// it
	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		HDC prev(dis->hDC);
		if (GetSafeHwnd() && dis->CtlType == ODT_COMBOBOX && !(dis->itemState & ODS_COMBOBOXEDIT) &&
		    CVS2010Colours::IsExtendedThemeActive())
		{
			if (mListMemDc)
			{
				dis->hDC = mListMemDc->m_hDC;
			}
			else
			{
				// prevent flicker by not drawing the item and invalidating later
				// (tried invalidate here, but they must have validated after the fact)
				KillTimer(kTimerIdInvalidate);
				SetTimer(kTimerIdInvalidate, 10, nullptr);
				return;
			}
		}

		TempPaintOverride t(HasRole(PartRole::Project));
		bool ret = ComboVS2010_CComboBoxEx_OnDrawItem(this, dis, GetVS2010ComboBackgroundColour(), readonly_combo,
		                                              popup_background_gradient_cache);
		dis->hDC = prev;
		if (ret)
			return;
	}

	CComboBoxEx::OnDrawItem(id, dis);
}

void CToolTipEditCombo::SettingsChanged()
{
	if (!gShellAttr->IsDevenv11OrHigher() || !CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		// return in vs2010, otherwise context window is messed up after closing
		// ide options dialog.
		return;
	}

	_ASSERTE(gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive());
	combo_background_cache = g_IdeSettings->GetEnvironmentColor(L"DropDownBackground", false);
	bgbrush_cache.DeleteObject();
}

void CToolTipEditCombo::ShowDropDown(BOOL bShowIt)
{
	std::shared_ptr<DisableComboBoxAnimation> disable_animation;
	if (bShowIt)
		disable_animation.reset(new DisableComboBoxAnimation);

	__super::ShowDropDown(bShowIt);
}

BOOL CToolTipEditCombo::Create(_In_ DWORD dwStyle, _In_ const RECT& rect, _In_ CWnd* pParentWnd, _In_ UINT nID)
{
	BOOL res = __super::Create(dwStyle, rect, pParentWnd, nID);

	if (::GetWinVersion() >= wvWin8 && Psettings->mEnableWin8ColorHook)
	{
		// [case: 78670]
		// owner drawn ComboBoxEx does not provide for owner-draw of the edit control part.
		// http://support.microsoft.com/kb/82078
		// http://stackoverflow.com/questions/1955538/win32-how-to-custom-draw-an-edit-control
		mColourizedEdit = ::ColourizeControl(GetEditCtrl());
		_ASSERTE(mColourizedEdit);
		SetColorableText(mHasColorableText);
	}

	return res;
}

void CToolTipEditCombo::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);
	// 	auto edit = GetEditCtrl();
	// 	auto combo = GetComboBoxCtrl();
	//
	// 	if (change == CWndDpiAware::DpiChange::BeforeParent)
	// 	{
	//
	// 		CDpiHandler::SetFontDpiScaled(m_hWnd, MiniHelpFrm::GetFont(), CDpiHandler::SDF_SetIfNull);
	//
	// 		if (edit != nullptr)
	// 			CDpiHandler::SetFontDpiScaled(edit->m_hWnd, MiniHelpFrm::GetFont(), CDpiHandler::SDF_SetIfNull);
	//
	// 		if (combo != nullptr)
	// 			CDpiHandler::SetFontDpiScaled(combo->m_hWnd, MiniHelpFrm::GetFont(), CDpiHandler::SDF_SetIfNull);
	// 	}
	// 	else
	// 	{
	// 		if (edit)
	// 			edit->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE |
	// SWP_NOACTIVATE);
	//
	// 		if (combo)
	// 			combo->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE |
	// SWP_NOACTIVATE);
	// 	}
	//
	// 	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

void CToolTipEditCombo::SetColorableText(bool isColorable)
{
	mHasColorableText = isColorable;
	if (mColourizedEdit)
	{
		if (mHasColorableText)
			mColourizedEdit->SetPaintType(PaintType::WizardBar);
		else
			mColourizedEdit->SetPaintType(PaintType::DontColor);
	}
}

void CToolTipEditCombo::ClearEditAndDrop()
{
	CStringW txt;
	::GetWindowTextW(GetEditCtrl()->GetSafeHwnd(), txt);
	{
		AutoLockCs l(mTextLock);
		mTextOnDrop = txt;
	}
	SetWindowText("");
	GetEditCtrl()->SetFocus();
	ShowDropDown(TRUE);
}

void CToolTipEditCombo::BeginPopupListPaint(CWnd* lst)
{
	_ASSERTE(CVS2010Colours::IsVS2010NavBarColouringActive() && lst->GetSafeHwnd() &&
	         CVS2010Colours::IsExtendedThemeActive());
	_ASSERTE(!mListMemDc && !mListWndDC);
	mListWndDC = new CClientDC(lst);
	mListMemDc = new ThemeUtils::CMemDC(mListWndDC);

	CRect rect;
	::GetClientRect(lst->m_hWnd, &rect);
	CBrush br;
	if (br.CreateSolidBrush(GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN)))
		::FillRect(mListMemDc->GetSafeHdc(), rect, br);
}

void CToolTipEditCombo::EndPopupListPaint()
{
	_ASSERTE(mListMemDc && mListWndDC && CVS2010Colours::IsExtendedThemeActive());
	CDC *tmp1 = mListMemDc, *tmp2 = mListWndDC;
	mListWndDC = nullptr;
	mListMemDc = nullptr;
	delete tmp1;
	delete tmp2;
}

BOOL CToolTipEditCombo::GetLBTextW(int index, CStringW& text)
{
	ASSERT(::IsWindow(m_hWnd));

	COMBOBOXEXITEMW cbexi;
	memset(&cbexi, 0, sizeof(cbexi));
	cbexi.mask = CBEIF_TEXT;
	cbexi.cchTextMax = GetLBTextLen(index);
	cbexi.pszText = text.GetBufferSetLength(cbexi.cchTextMax);
	cbexi.iItem = index;

	BOOL result = !!SendMessage(CBEM_GETITEMW, 0, (LPARAM)&cbexi);

	text.ReleaseBuffer(result ? cbexi.cchTextMax : 0);

	return result;
}

CToolTipEditCombo::ISubclasser* CToolTipEditCombo::CreateSubclasser()
{
	return new CToolTipEditComboSubclasser(HasRole(PartRole::Context));
}
