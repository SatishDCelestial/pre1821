// MiniHelpFrm.cpp : implementation file
//

#include "stdafx.h"
#include "EditParentWnd.h"
#include "MiniHelpFrm.h"
#include "../resource.h"
#include "..\VaMessages.h"
#include "..\..\addin\DSCmds.h"
#include "TooltipEditView.h"
#include "../wtcsym.h"
#include "../Mparse.h"
#include "..\DevShellAttributes.h"
#include "..\Settings.h"
#include "..\FileTypes.h"
#include "..\WindowUtils.h"
#include "..\PROJECT.H"
#include "..\EDCNT.H"
#include "..\FILE.H"
#include "..\ColorListControls.h"
#include "..\KeyBindings.h"
#include "vsshell100.h"
#include "..\SaveDC.h"
#include "..\IdeSettings.h"
#include "..\VaService.h"
#include "..\VaAfxW.h"
#include "AutoUpdate\WTAutoUpdater.h"
#include "DoubleBuffer.h"
#include "WtException.h"
#include "MenuXP/MenuXP.h"
#include "ToolTipEditCombo.h"
#include "FontSettings.h"
#include "ScreenAttributes.h"
#include "EdcntWPF.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// MiniHelpFrm

#define IDT_SET_MINIHELP_TEXT_ASYNC 300
#define IDT_MINIHELP_RECALC_LAYOUT 400
#define IDT_MINIHELP_UPDATE_HEIGHT 500
#define MINIHELP_UPDATE_HEIGHT_INTERVAL 1000

#define IDC_GOTO_BUTTON 301

MiniHelpFrm* g_pMiniHelpFrm = nullptr;

void FreeMiniHelp()
{
	if (!g_pMiniHelpFrm)
		return;

	auto tmp = g_pMiniHelpFrm;
	g_pMiniHelpFrm = nullptr;
	if (IsWindow(tmp->m_hWnd))
		tmp->DestroyWindow();
	delete tmp;
}

MiniHelpFrm::MiniHelpFrm()
{
	m_hWnd = NULL;
	m_parent = NULL;
	m_edParent = NULL;
	m_context = m_def = m_project = NULL;
	m_gotoButton = NULL;
	m_layoutVersion = 0;
	m_lastException = 0;

	// if this assert fires, then you've created more than one MiniHelpFrm
	// this is a singleton class
	ASSERT(!g_pMiniHelpFrm); // this is an unprotected singleton class
}

void MiniHelpFrm::OnDpiChanged(CWndDpiAware::DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);
}

void MiniHelpFrm::UpdateHeight(bool resetTimer /*= true*/)
{
	if (!::IsWindow(m_hWnd))
		return;

#if MINIHELP_UPDATE_HEIGHT_INTERVAL
	if (resetTimer)
	{
		KillTimer(IDT_MINIHELP_UPDATE_HEIGHT);
		SetTimer(IDT_MINIHELP_UPDATE_HEIGHT, MINIHELP_UPDATE_HEIGHT_INTERVAL, nullptr);
	}
#endif

	UINT height = (UINT)GetIdealMinihelpHeight(m_hWnd);

	if (gVaInteropService && height && Psettings->m_minihelpHeight != height)
	{
		// #MiniHelp_Height
		Psettings->m_minihelpHeight = height;

		DWORD oldLayout = m_layoutVersion;

		gVaInteropService->CheckMinihelpHeight();

		if (oldLayout == m_layoutVersion)
		{
			RecalcLayout();
		}
	}
}

MiniHelpFrm::~MiniHelpFrm()
{
	delete m_gotoButton;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(MiniHelpFrm, CWnd)
//{{AFX_MSG_MAP(MiniHelpFrm)
ON_BN_CLICKED(IDC_GOTO_BUTTON, OnGotoClick)
ON_WM_CONTEXTMENU()
ON_WM_TIMER()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// MiniHelpFrm message handlers
BOOL MiniHelpFrm::Create(CWnd* /*parent*/)
{
	BOOL ret = TRUE;
	static CString regClassName;
	if (regClassName.IsEmpty())
	{
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive())
			regClassName = ::GetDefaultVaWndCls();
		else
			regClassName = ::AfxRegisterWndClass(0, 0, (HBRUSH)(COLOR_BTNFACE + 1));
	}

	CRect rc(0, 0, 0, 0);
	// match styles used in MEF component
	const DWORD styles =
	    gShellAttr && gShellAttr->IsDevenv10OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive()
	        ? WS_CLIPCHILDREN | WS_CLIPSIBLINGS
	        : 0u;
	for (uint cnt = 0; cnt < 10; ++cnt)
	{
		ret = CWnd::Create(regClassName, _T("VA_MinihelpFrm"), WS_VISIBLE | WS_CHILD | styles, rc, m_parent, 0);
		if (ret)
			break;

		// add retry for [case: 111864]
		vLog("WARN: MiniHelpFrm::Create call to Create failed\n");
		Sleep(100 + (cnt * 50));
	}
	SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (GetWindowLongPtr(m_hWnd, GWLP_USERDATA) | VA_WND_DATA));

	Psettings->m_minihelpHeight = (DWORD)GetIdealMinihelpHeight(m_hWnd);
	
	for (uint cnt = 0; cnt < 10; ++cnt)
	{
		ret = m_wndSplitter.CreateMinihelp(this);
		if (ret)
			break;

		// add retry for [case: 111864]
		vLog("WARN: MiniHelpFrm::Create call to CreateMinihelp failed\n");
		Sleep(100 + (cnt * 50));
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	SIZE sz;
	sz.cy = VsUI::DpiHelper::LogicalToDeviceUnitsX(16);
	sz.cx = VsUI::DpiHelper::LogicalToDeviceUnitsX(300);

	CCreateContext contextT;

	auto createView = [&](int col, PartRole role) {

		// compatibility is necessary in other places as well, so check it
		_ASSERTE(col == (int)role); 

		CToolTipEditContext::Get()->SetCreationRole(role);

		for (uint cnt = 0; cnt < 10; ++cnt)
		{
			ret = m_wndSplitter.CreateView(0, col, RUNTIME_CLASS(CToolTipEditView), sz, &contextT);
			if (ret)
				break;

			// add retry for [case: 111864]
			vLog("WARN: MiniHelpFrm::Create call to CreateView 1 failed\n");
			Sleep(100 + (cnt * 50));
		}
	};

	createView(0, PartRole::Context);
	createView(1, PartRole::Definition);
	createView(2, PartRole::Project);

	int colWidth = VsUI::DpiHelper::LogicalToDeviceUnitsX(100);
	int columnWidthMin = VsUI::DpiHelper::LogicalToDeviceUnitsX(10);

	m_wndSplitter.SetRowInfo(0, sz.cy, sz.cy);
	m_wndSplitter.SetColumnInfo(0, colWidth, columnWidthMin);
	m_wndSplitter.SetColumnInfo(1, colWidth, columnWidthMin);
	m_wndSplitter.SetColumnInfo(2, colWidth, columnWidthMin);

	m_wndSplitter.SetWindowText("VA_MinihelpSplitter");

	m_context = (CToolTipEditView*)m_wndSplitter.GetPane(0, 0);
	m_context->SetFontType(VaFontType::MiniHelpFont);
	m_context->CView::SetWindowText("VA_MinihelpContext");

	m_def = (CToolTipEditView*)m_wndSplitter.GetPane(0, 1);
	m_def->SetFontType(VaFontType::MiniHelpFont);
	m_def->CView::SetWindowText("VA_MinihelpDefinition");

	m_project = (CToolTipEditView*)m_wndSplitter.GetPane(0, 2);
	m_project->SetFontType(VaFontType::MiniHelpFont);
	m_project->CView::SetWindowText("VA_MinihelpProject");

	m_gotoButton = new ToolTipButton;
	if (m_gotoButton)
		m_gotoButton->Create(rc, this, IDC_GOTO_BUTTON, IDS_BUTTON_GOTO);

	mToolTipCtrl.Create(this);
	mToolTipCtrl.Activate(TRUE);
	CToolInfo ti;
	ZeroMemory(&ti, sizeof(CToolInfo));
	ti.cbSize = sizeof(TOOLINFO);
	ti.hwnd = m_gotoButton->m_hWnd;
	ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_CENTERTIP;
	ti.uId = (UINT_PTR)m_gotoButton->m_hWnd;

#if MINIHELP_UPDATE_HEIGHT_INTERVAL
	SetTimer(IDT_MINIHELP_UPDATE_HEIGHT, MINIHELP_UPDATE_HEIGHT_INTERVAL, nullptr);
#endif

	static char sGotoButtonTooltipText[255];
	WTString binding = ::GetBindingTip("VAssistX.GotoImplementation", "Alt+G");
	sprintf(sGotoButtonTooltipText, "Goto Implementation%s", binding.c_str());
	ti.lpszText = sGotoButtonTooltipText;

	mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	::mySetProp(mToolTipCtrl.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	return ret;
}

LRESULT MiniHelpFrm::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		if (message == WM_ERASEBKGND)
		{
			HWND wpfStatic = ::GetParent(m_parent->GetSafeHwnd());
			if (TRUE != ::SendMessage(wpfStatic, VAM_ISSUBCLASSED2, 0, 0))
			{
				HDC hdc = (HDC)wParam;
				if (hdc)
				{
					CRect rect;
					GetClientRect(rect);
					CBrush brush;
					if (brush.CreateSolidBrush(GetVS2010VANavBarBkColour()))
						::FillRect(hdc, rect, brush);
				}
			}
			return 1;
		}
	}

	if (message == WM_VA_MINIHELP_WPFRESIZE)
	{
		UpdateSizeWPF((int)wParam, (int)lParam);
		return 0;
	}

	if (message == WM_VA_MINIHELP_GETHEIGHT)
	{
		if (lParam)
			return GetIdealMinihelpHeight((HWND)lParam);

		return Psettings->m_minihelpHeight;
	}


	if (message == WM_DESTROY)
	{
		KillTimer(IDT_SET_MINIHELP_TEXT_ASYNC);
		KillTimer(IDT_MINIHELP_RECALC_LAYOUT);
		KillTimer(IDT_MINIHELP_UPDATE_HEIGHT);
		Reparent(NULL, NULL);
	}

	return __super::WindowProc(message, wParam, lParam);
}

BOOL MiniHelpFrm::Reparent(CWnd* parent, CWnd* edParent, LPCWSTR con, LPCWSTR def, LPCWSTR proj)
{
	BOOL ret = TRUE;

	if (!parent || !parent->m_hWnd || !::IsWindow(parent->m_hWnd))
	{
		_ASSERTE(m_hWnd);
		if (m_hWnd && ::IsWindow(m_hWnd) && Psettings && g_FontSettings) // [case: 110698]
		{
			auto wndMain = AfxGetMainWnd();
			if (wndMain && ::IsWindow(wndMain->m_hWnd))
			{
				ShowWindow(SW_HIDE);

				// [case:148270] loop to ensure correct parenting
				for (uint cnt1 = 0; cnt1 < 25; ++cnt1)
				{
					SetParent(wndMain);
					if (GetParent() == wndMain)
						break;

					vLog("WARN: MiniHelpFrm::Reparent call to SetParent(Main) failed\n");
					::Sleep(100 + (std::min<uint>({cnt1, 15}) * 50));
					//::WaitForInputIdle(GetCurrentProcess(), std::min<uint>({cnt1, 15}) * 10);
				}

				SetHelpText(L"", L"", L"");
			}
		}

		m_parent = nullptr;
		m_edParent = nullptr;
		return ret;
	}

	m_parent = (EditParentWnd*)parent;
	m_edParent = (EditParentWnd*)edParent;

	if (m_parent && m_parent->m_hWnd && ::IsWindow(m_parent->m_hWnd))
	{
		if (gShellAttr->IsDevenv10OrHigher())
		{
			auto firstChild = m_parent->GetWindow(GW_CHILD);
			if (!firstChild)
			{
				// Don't allow parenting when window hierarchy is broken
				m_parent = nullptr;
				m_edParent = nullptr;
				return FALSE;
			}

			auto className = GetWindowClassString(firstChild->m_hWnd);
			if (className.Compare("VsSplitterRoot") != 0)
			{
				// Don't allow parenting when window hierarchy is broken
				m_parent = nullptr;
				m_edParent = nullptr;
				return FALSE;
			}

			HWND wpfStatic = ::GetParent(m_parent->m_hWnd);
			if (wpfStatic && ::IsWindow(wpfStatic))
			{
				DoStaticSubclass(wpfStatic);
			}
		}

		if (m_hWnd)
		{
			// clear text before reparenting to reduce flicker in new window
			SetHelpText(con, def, proj, false);

			// [case:148270] loop to ensure correct parenting
			for (uint cnt2 = 0; cnt2 < 25; ++cnt2)
			{
				SetParent(m_parent);
				if (GetParent() == m_parent)
					break;

				vLog("WARN: MiniHelpFrm::Reparent call to SetParent failed\n");
				::Sleep(100 + (std::min<uint>({cnt2, 15}) * 50));
				//::WaitForInputIdle(GetCurrentProcess(), std::min<uint>({cnt2, 15}) * 10);
			}

			if (GetParent() == m_parent)
				ShowWindow(SW_SHOW);
			else
			{
				m_parent = nullptr;
				m_edParent = nullptr;
				return ret;
			}

			if (!CVS2010Colours::IsVS2010NavBarColouringActive())
			{
				m_parent->Invalidate(FALSE);
				Invalidate(FALSE);
				m_gotoButton->Invalidate(FALSE);
			}
		}
		else if (Create(m_parent))
		{
			SetHelpText(con, def, proj, false);
		}
	}

#if MINIHELP_UPDATE_HEIGHT_INTERVAL
	if (m_hWnd)
	{
		SetTimer(IDT_MINIHELP_UPDATE_HEIGHT, MINIHELP_UPDATE_HEIGHT_INTERVAL, nullptr);
	}
#endif

	if (gVaInteropService && m_hWnd)
	{
		DWORD oldLayout = m_layoutVersion;

		gVaInteropService->CheckMinihelpHeight();

		if (oldLayout == m_layoutVersion)
		{
			RecalcLayout();
		}
	}

	return ret;
}

void DoStaticSubclass(HWND wpfStatic)
{
	if (gShellAttr->IsDevenv10OrHigher() && CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		if (wpfStatic && TRUE != ::SendMessage(wpfStatic, VAM_ISSUBCLASSED2, 0, 0))
		{
			class StaticSubcls : public CWnd
			{
			  public:
				StaticSubcls(HWND sub) : mDrawingTimerId(777), mPosChangedTimerId(888), mBgColor(CLR_INVALID)
				{
					UpdateBgBrush();
#ifdef _DEBUG
					WTString cls(GetWindowClassString(sub));
#endif // _DEBUG
					SubclassWindow(sub);

					mDblBuf = std::make_shared<CDoubleBuffer>(m_hWnd);
					if (IsEmptyAndVisible())
						Paint();
				}

			  private:
				virtual LRESULT WindowProc(UINT msg, WPARAM wParam, LPARAM lParam)
				{
					switch (msg)
					{
					case VAM_ISSUBCLASSED2:
						return TRUE;

					case WM_DESTROY: {
						KillTimer(mDrawingTimerId);
						return CWnd::WindowProc(msg, wParam, lParam);
					}
					break;

					case WM_NCDESTROY: {
						KillTimer(mDrawingTimerId);
						LRESULT res = CWnd::WindowProc(msg, wParam, lParam);
						delete this;
						return res;
					}
					break;

					case WM_WINDOWPOSCHANGED:
						if (IsEmptyAndVisible())
						{
							// this is to catch paint of unfocused editor that
							// becomes visible after creating a new tab group.
							// The placeholder will be visible while the one
							// va nav bar frame is visible in another group.
							// We don't update right away to prevent flicker
							// when doing alt+o between two editors in the same
							// position.
							KillTimer(mPosChangedTimerId);
							SetTimer(mPosChangedTimerId, 100, NULL);
						}
						break;

					// timer is used for erase/paint operations to reduce flicker
					// of placeholder over the one va nav bar frame.
					// Most noticeable when doing repeated alt+o between two
					// editors in the same position.
					case WM_TIMER:
						if (wParam == mDrawingTimerId)
						{
							KillTimer(mDrawingTimerId);
							if (IsEmptyAndVisible())
								Paint();
							return 0;
						}

						if (wParam == mPosChangedTimerId)
						{
							KillTimer(mPosChangedTimerId);
							if (IsEmptyAndVisible())
								Paint();
							return 0;
						}
						break;

					case WM_ERASEBKGND:
					case WM_NCPAINT:
					case WM_PAINT:
					case WM_NCACTIVATE:
					case WM_PRINT:
					case WM_PRINTCLIENT:
						KillTimer(mDrawingTimerId);
						if (IsEmptyAndVisible())
						{
							if (WM_PAINT == msg)
								ValidateRect(NULL);
							SetTimer(mDrawingTimerId, 100, NULL);
							Paint();
						}
						else
						{
							if (WM_PAINT == msg)
								ValidateRect(NULL);
						}

						return WM_ERASEBKGND == msg ? 1 : 0;
						break;
					}

					return CWnd::WindowProc(msg, wParam, lParam);
				}

				void UpdateBgBrush()
				{
					COLORREF bgColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_BACKGROUND);
					if (CLR_INVALID != bgColor)
					{
						if (mBgColor != bgColor)
						{
							mBgColor = bgColor;
							mBgBrush.DeleteObject();
						}

						if (!mBgBrush.m_hObject)
							mBgBrush.CreateSolidBrush(mBgColor);
					}
				}

				// we only paint the placeholder wnd if it is visible and if
				// the one va nav bar frame is not seated in the placeholder
				BOOL IsEmptyAndVisible() const
				{
					if (!IsWindowVisible())
						return FALSE;

					if (!g_pMiniHelpFrm || !g_pMiniHelpFrm->GetSafeHwnd())
						return FALSE;

					CWnd* pWnd = g_pMiniHelpFrm->GetParent();
					if (pWnd->GetSafeHwnd())
						pWnd = pWnd->GetParent();

					if (!pWnd->GetSafeHwnd() || pWnd == this)
						return FALSE;

					return TRUE;
				}

				int Paint()
				{
					if (!IsEmptyAndVisible())
						return 0;

					try
					{
						CWindowDC wndDc(this);
						CDoubleBufferedDC dc(*mDblBuf, wndDc, mBgColor);
						CRect rect;
						GetClientRect(rect);
						CBrush brush;
						if (brush.CreateSolidBrush(GetVS2010VANavBarBkColour()))
						{
							dc.FrameRect(rect, &brush);
							UpdateBgBrush();
							if (mBgBrush.m_hObject)
							{
								// change x param to 0 so that blank minihelp matches IDE nav bar left position.
								// but causes movement when going back and forth between non-blank minihelp.
								// wasn't able to figure out where the left position of non-blank minihelp is getting
								// offset by 1 - would prefer to fix that and then set x below to 0 instead of 1.
								rect.DeflateRect(1, 1);
								dc.FillRect(rect, &mBgBrush);

								CPen borderpen;
								if (borderpen.CreatePen(PS_SOLID, 1,
								                        CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_BORDER)))
								{
									dc.SelectObject(&borderpen);
									dc.SelectObject(CBrush::FromHandle((HBRUSH)::GetStockObject(HOLLOW_BRUSH)));
									dc.Rectangle(&rect);
									return 1;
								}
							}
						}
					}
					catch (const WtException& e)
					{
						UNUSED_ALWAYS(e);
						VALOGERROR("ERROR: StaticSubCls Paint exception caught 1");
					}
					return 0;
				}

			  private:
				std::shared_ptr<CDoubleBuffer> mDblBuf;
				const UINT_PTR mDrawingTimerId, mPosChangedTimerId;
				CBrush mBgBrush;
				COLORREF mBgColor;
			};

			new StaticSubcls(wpfStatic);
		}
	}
}

void MiniHelpFrm::SetHelpText(LPCWSTR con, LPCWSTR def, LPCWSTR proj, bool asyncUpdate /*= true*/)
{
	UINT timerElapse;
	// Clean up scope before displaying it to context
	CStringW newContextTxt = CleanScopeForDisplayW(con);
	if (newContextTxt.IsEmpty())
	{
		// longer delay for empty text reset in case another update is pending
		// especially during reparenting
		timerElapse = 250;
	}
	else
	{
		if (Psettings->mNavBarContext_DisplaySingleScope)
		{
			// [case: 91507] remove outer scopes leaving only a single depth of scope
			int startPos = 0;
			int prevScopePos = -1;
			int oneBackScopePos = -1;

			for (;;)
			{
				const int delimPos = newContextTxt.Find(L".", startPos);
				if (-1 == delimPos)
				{
					const CStringW cur(newContextTxt.Mid(startPos));
					if (cur == L"{" || ::IsReservedWord(to_string_view(CString(cur)), gTypingDevLang))
					{
						// stop before: else if for while do switch using await {
						prevScopePos = oneBackScopePos;
					}
					break;
				}

				const int parenPos = newContextTxt.Find(L'(', startPos);
				if (-1 != parenPos && parenPos < delimPos)
					break;

				const int templatePos = newContextTxt.Find(L'<', startPos);
				if (-1 != templatePos && templatePos < delimPos)
					break;

				const CStringW cur(newContextTxt.Mid(startPos, delimPos - startPos));
				// VA turns "for each" into "foreach" which is not a keyword, so need to special case it
				if (cur == L"{" || ::IsReservedWord(to_string_view(CString(cur)), gTypingDevLang) ||
				    (!cur.IsEmpty() && cur[0] == 'f' && cur == "foreach"))
				{
					// stop before: else if for while do switch using await {
					prevScopePos = oneBackScopePos;
					break;
				}

				oneBackScopePos = prevScopePos;
				prevScopePos = startPos;
				startPos = delimPos + 1;
			}

			if (prevScopePos > 0)
				newContextTxt = newContextTxt.Mid(prevScopePos);
		}

		// Added space after context/def to fix last word from being randomly colored.
		newContextTxt += L" ";
		timerElapse = 50;
	}

	// CleanDefForDisplay needs to handle file names or we need to test before calling it.
	const CStringW newDefTxt = def; // + ' ' + CleanDefForDisplay(def);

	{
		AutoLockCs l(mTextCs);
		if (!mPendingContextText.IsEmpty() && newContextTxt == mPendingContextText &&
		    newDefTxt == mPendingDefinitionText)
			return;

		mPendingContextText = newContextTxt;
		mPendingDefinitionText = newDefTxt;
		mPendingProjectText = proj;
	}

	KillTimer(IDT_SET_MINIHELP_TEXT_ASYNC);

	if (asyncUpdate)
		SetTimer(IDT_SET_MINIHELP_TEXT_ASYNC, timerElapse, nullptr);
	else
		DoUpdateText();
}

void MiniHelpFrm::SetProjectText(CStringW& proj)
{
	AutoLockCs l(mTextCs);
	mPendingProjectText = proj;
	KillTimer(IDT_SET_MINIHELP_TEXT_ASYNC);
	SetTimer(IDT_SET_MINIHELP_TEXT_ASYNC, 50, nullptr);

	// [case: 165029] DON'T set focus at this point
	// it is causing problems when change of project happens in Solution Explorer
}

void MiniHelpFrm::UpdateSizeWPF(int width, int height)
{
	if (gShellAttr->IsDevenv10OrHigher() && m_parent && ::IsWindow(m_parent->m_hWnd))
	{
		HWND wpfStatic = ::GetParent(m_parent->m_hWnd);
		if (wpfStatic && ::IsWindow(wpfStatic))
		{
			auto splitterRoot = GetWindow(GW_HWNDNEXT);
			if (splitterRoot)
			{
				auto className = GetWindowClassString(splitterRoot->m_hWnd);
				if (className.Compare("VsSplitterRoot") == 0)
				{
					m_parent->MoveWindow(0, 0, width, height);
					splitterRoot->MoveWindow(0, 0, width, height);
					return;
				}
			}
		}

		vLog("WARN: MiniHelpFrm::UpdateSize call failed\n");
	}
}

bool MiniHelpFrm::IsAnyPopupActive()
{
	bool anyPopups = false;

	VATomatoTip::EnumThreadPopups(
	    [&](HWND hWnd) {
		    if (!::IsWindowVisible(hWnd))
			    return true;
		    anyPopups = true;
		    return false;
	    }, 0);

	return anyPopups;
}

void MiniHelpFrm::RecalcLayout()
{
	if (!m_hWnd)
		return;

	KillTimer(IDT_MINIHELP_RECALC_LAYOUT);

	if (!m_parent)
		return;

	static bool inRecalc = false;

	if (inRecalc)
		return;

	struct inRecalcRAII
	{
		bool& m_val;
		inRecalcRAII(bool& val)
		    : m_val(val)
		{
			m_val = true;
		}

		~inRecalcRAII()
		{
			m_val = false;
		}
	} _inRecalc(inRecalc);

	try
	{
		KillTimer(IDT_MINIHELP_RECALC_LAYOUT);

		if (!m_wndSplitter.IsLayoutValid())
		{
			VADEBUGPRINT("#MHLP MiniHelpFrm::RecalcLayout TIMER 1");

			SetTimer(IDT_MINIHELP_RECALC_LAYOUT, 64, nullptr);
			return;
		}

		CRect tr;
		m_parent->GetClientRect(&tr);
		if (tr.Width() < 16)
		{
			SetTimer(IDT_MINIHELP_RECALC_LAYOUT, 64, nullptr);
			return;
		}

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);
		m_layoutVersion++;

		VADEBUGPRINT("#MHLP MiniHelpFrm::RecalcLayout " << tr.Height() << std::endl);

		if (Psettings->minihelpAtTop)
			tr.bottom = (int)Psettings->m_minihelpHeight;
		else
		{
			if (m_edParent && Is_Tag_Based(m_edParent->m_fType))
			{
				// Add space for their HTML/XML nav bar at the bottom. Case 375
				CRect rEd;
				m_edParent->GetWindowRect(&rEd);
				tr.top = rEd.Height();
				tr.bottom = rEd.Height() + (int)Psettings->m_minihelpHeight;
			}
			else
				tr.top = tr.bottom - (int)Psettings->m_minihelpHeight;
		}
		if (gShellAttr->IsDevenv7())
		{
			tr.top += VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
			tr.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
		}

		MoveWindowIfNeeded(this, &tr); // causes a call to RecalcLayout - this fn
		GetClientRect(&tr);

		int gotowidth = 0;
		if (m_gotoButton)
		{
			gotowidth = m_gotoButton->CalculateWidthFromHeight(tr.Height() - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
			if (CVS2010Colours::IsVS2010NavBarColouringActive())
				tr.right -= gotowidth;
			else
				tr.right -= gotowidth + 2; // LogicalToDeviceUnitsX(2);
		}

		MoveWindowIfNeeded(&m_wndSplitter, &tr);

		// goto button rect
		if (m_gotoButton)
		{
			if (CVS2010Colours::IsVS2010NavBarColouringActive())
			{
				tr.left = tr.right - 1;    // LogicalToDeviceUnitsX(1);
				tr.right += gotowidth + 1; // LogicalToDeviceUnitsX(1);
			}
			else
			{
				tr.left = tr.right + 1; // LogicalToDeviceUnitsX(1);
				tr.right += gotowidth;
				tr.top += 1;    // LogicalToDeviceUnitsY(1);
				tr.bottom -= 1; // LogicalToDeviceUnitsY(1);
			}
			MoveWindowIfNeeded(m_gotoButton, &tr, false);
			m_gotoButton->Invalidate(TRUE);
		}
	}
	catch (...)
	{
		VADEBUGPRINT("#MHLP MiniHelpFrm::RecalcLayout EXCEPTION");

		VALOGERROR("ERROR: MiniHelpFrm RecalcLayout exception caught");

		if (::GetTickCount() - m_lastException > 500)
		{
			SetTimer(IDT_MINIHELP_RECALC_LAYOUT, 100, nullptr);	
			m_lastException = ::GetTickCount();
		}
	}
}

void MiniHelpFrm::OnTimer(UINT_PTR nIDEvent)
{
	if (IDT_SET_MINIHELP_TEXT_ASYNC == nIDEvent)
	{
		KillTimer(nIDEvent);
		if (::IsMinihelpDropdownVisible())
		{
			// [case: 89977]
			return;
		}

		DoUpdateText();
		return;
	}
	
	if (IDT_MINIHELP_RECALC_LAYOUT == nIDEvent)
	{
		KillTimer(nIDEvent);
		RecalcLayout();
		return;
	}

	if (IDT_MINIHELP_UPDATE_HEIGHT == nIDEvent)
	{
		UpdateHeight(false);
		return;
	}

	CWnd::OnTimer(nIDEvent);
}

void MiniHelpFrm::OnGotoClick()
{
	CheckIDENavigationBar();

	EdCntPtr ed(g_currentEdCnt);
	if (ed && gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 44540] restore focus before opening new wnd
		ed->vSetFocus();
		// [case: 78796] problems with window split/unsplit
		_ASSERTE(ed == g_currentEdCnt);
		ed->SendMessage(DSM_WIZ_GOTODEF, 1, 0);
	}
	else
	{
		m_parent->SetFocus();

		if (m_edParent)
			::SendMessage(m_edParent->GetEditWnd(), DSM_WIZ_GOTODEF, 1, 0);
	}
}

CStringW MiniHelpFrm::GetText() const
{
	CStringW def(mDefinitionText);
	if (::IsFile(def))
		def = ::GetBaseName(def);

	CStringW txt(L"Minihelp Context: " + mContextText + L"\r\nMinihelp Definition: " + def);
	return txt;
}

void MiniHelpFrm::SettingsChanged()
{
	if (m_context)
		m_context->SettingsChanged();

	if (m_def)
		m_def->SettingsChanged();

	if (m_project)
		m_project->SettingsChanged();

	if (m_gotoButton && gShellAttr && gShellAttr->IsDevenv11OrHigher())
		m_gotoButton->PrepareBitmaps(FALSE);

	m_wndSplitter.SettingsChanged();

	if (GetSafeHwnd() && IsWindowVisible())
		Invalidate(FALSE);

	RecalcLayout();
	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

void MiniHelpFrm::DoUpdateText()
{
	CStringW contextText, definitionText, projectText;
	{
		AutoLockCs l(mTextCs);
		if (mPendingContextText == mContextText && 
			mPendingDefinitionText == mDefinitionText &&
			mPendingProjectText == mProjectText)
			return;

		contextText = mContextText = mPendingContextText;
		mPendingContextText.Empty();
		definitionText = mDefinitionText = mPendingDefinitionText;
		mPendingDefinitionText.Empty();
		projectText = mProjectText = mPendingProjectText;
		mPendingProjectText.Empty();
	}

	bool hasFile = false;
	if (definitionText.GetLength() > 2)
	{
		if (definitionText[1] == ':' && (definitionText.Find(L":\\") != -1 || definitionText.Find(L":/") != -1))
			hasFile = true;
		else if (definitionText[0] == '\\' && definitionText[1] == '\\')
			hasFile = true;
	}

	// set context before def because it sets state that the def uses
	if (m_context)
	{
		bool isColorableText = !hasFile;
		if (isColorableText && definitionText.IsEmpty())
		{
			if (contextText.IsEmpty() || contextText == " ")
			{
				isColorableText = m_context->HasColorableText();
			}
			else if (-1 != contextText.Find('.'))
			{
				int ftype = GetFileTypeByExtension(contextText);
				if (ftype != Other)
					isColorableText = false;
			}
		}

		m_context->SetWindowText(contextText, isColorableText);
	}

	bool anyPopup = IsAnyPopupActive();

	EdCntPtr ed = g_currentEdCnt;

	bool edHadFocus = ed && ed->IsKeyboardFocusWithin();

	if (!anyPopup && edHadFocus)
		m_wndSplitter.SetFocus();

	if (m_def)
		m_def->SetWindowText(definitionText, !hasFile);

	if (m_gotoButton && !m_gotoButton->IsWindowEnabled())
		m_gotoButton->EnableWindow();

	if (m_project)
		m_project->SetWindowText(projectText, false);

	if (ed && edHadFocus && !anyPopup)
	{
		ed->vSetFocus();
	}
}

void MiniHelpFrm::OnContextMenu(CWnd* pWnd, CPoint pos)
{
	// [case: 91507]
	enum
	{
		CmdReduceScope = 1,
		SeparatorPos,
		CmdGoDoesGoto
	};

	PopupMenuXP xpmenu;
	xpmenu.AddMenuItem(CmdReduceScope, MF_BYPOSITION | (Psettings->mNavBarContext_DisplaySingleScope ? MF_CHECKED : 0u),
	                   "&Reduce display of scopes in context window");
	xpmenu.AddSeparator(SeparatorPos);
	// [case: 82578]
	xpmenu.AddMenuItem(CmdGoDoesGoto, MF_BYPOSITION | (Psettings->mUseGotoRelatedForGoButton ? MF_CHECKED : 0u),
	                   "&Execute Goto Related on press of Go button");

	const int result = xpmenu.TrackPopupMenuXP(gMainWnd, pos.x, pos.y);
	switch (result)
	{
	case CmdReduceScope:
		Psettings->mNavBarContext_DisplaySingleScope = !Psettings->mNavBarContext_DisplaySingleScope;
		// refresh text
		if (g_currentEdCnt)
			g_currentEdCnt->SetStatusInfo();
		break;
	case CmdGoDoesGoto:
		Psettings->mUseGotoRelatedForGoButton = !Psettings->mUseGotoRelatedForGoButton;
		break;
	default:
		return;
	}
}