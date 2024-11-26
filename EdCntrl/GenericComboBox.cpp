// GenericComboBox.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "GenericComboBox.h"
#include "resource.h"
#include "WorkSpaceTab.h"
#include "DevShellAttributes.h"
#include "WindowUtils.h"
#include "ColorListControls.h"
#include "vsshell100.h"
#include "ImageListManager.h"
#include "TextOutDc.h"
#include "SyntaxColoring.h"
#include "StringUtils.h"
#include "IdeSettings.h"
#include "DpiCookbook\VsUIDpiHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class GenericComboListHandler : public CGenericComboBoxListCtrl::Handler
{
	CGenericComboBox* m_gcb = nullptr;
	CListCtrl* m_list = nullptr;

  public:
	GenericComboListHandler(CGenericComboBox* gcb) : m_gcb(gcb)
	{
	}

	virtual ~GenericComboListHandler()
	{
	}

	virtual bool HandleLButtonDown(const CPoint& pnt)
	{
		CRect gcb_rect;
		m_gcb->GetWindowRect(&gcb_rect);

		gcb_rect.left = gcb_rect.right - ::GetSystemMetrics(SM_CXHTHUMB);

		return gcb_rect.PtInRect(pnt) != FALSE;
	}

	virtual bool GetItemInfo(int nRow, CStringW* text, CStringW* tip, int* image, UINT* state)
	{
		return m_gcb->GetItemInfo(nRow, text, tip, image, state);
	}

	virtual CWnd* GetFocusHolder() const
	{
		return m_gcb->GetEditCtrl();
	}

	virtual CWnd* GetParent() const
	{
		return m_gcb;
	}

	virtual void OnInit(CListCtrl& list)
	{
		m_list = &list;
	}

	virtual void OnSelect(int index)
	{
		m_gcb->OnSelect();
	}

	virtual void OnShow()
	{
		// nothing
	}

	virtual bool OnHide()
	{
		m_gcb->DisplayList(FALSE);
		return true;
	}

	virtual void OnVisibleChanged()
	{
		m_gcb->Invalidate();
		m_gcb->UpdateWindow();
	}

	virtual void OnSettingsChanged()
	{
		m_gcb->SettingsChanged();
	}

	virtual bool OnWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& user_result)
	{
		return m_gcb->OnListWindowProc(message, wParam, lParam, user_result);
	}

	virtual int GetItemsCount()
	{
		return m_gcb->GetItemsCount();
	}

	virtual int GetCurSel()
	{
		return m_gcb->GetCurSel();
	}

	virtual bool IsVS2010ColouringActive()
	{
		return m_gcb->IsVS2010ColouringActive();
	}

	virtual VaFontType GetFont()
	{
		return VaFontType::EnvironmentFont;
	}
};

#pragma warning(push)
#pragma warning(disable : 4191)
IMPLEMENT_DYNAMIC(GenericComboPaintSubClass, CWnd)
BEGIN_MESSAGE_MAP(GenericComboPaintSubClass, CWnd)
ON_REGISTERED_MESSAGE(WM_DRAW_MY_BORDER, OnDrawMyBorder)
ON_WM_TIMER()
END_MESSAGE_MAP()
#pragma warning(pop)

LRESULT GenericComboPaintSubClass::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	bool draw_arrow = false;
	std::optional<LRESULT> ret;
	if (mVs2010ColouringIsActive)
	{
		switch (message)
		{
			//		default:
			// 			if(message < WM_MOUSEMOVE || message > WM_MOUSELAST)
			// 				break;
			// 			// fall through
		case WM_ERASEBKGND:
			if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			{
				HDC hdc = (HDC)wParam;
				if (hdc)
				{
					CBrush br;
					if (br.CreateSolidBrush(GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour()))
					{
						CRect crect;
						GetClientRect(&crect);
						::FillRect(hdc, &crect, br);
						ret = 1;
					}
				}
			}
			// fall through
		case WM_NCPAINT:
		case WM_PAINT:
		case WM_NCACTIVATE:
		case WM_PRINT:
		case WM_PRINTCLIENT:
			draw_arrow = true;
			break;
		case WM_DESTROY:
			KillTimer(MOUSE_TIMER_ID);
			break;
		}
	}
	if (draw_arrow)
		DoPaint(true); // gmit: important to do so to prevent occasional flicker on mouse move!!

	if (!ret)
		ret = CWnd::WindowProc(message, wParam, lParam);

	if (draw_arrow)
		DoPaint();

	return *ret;
}

// VA View FIS / SIS paint
// va nav bar / minihelp paint
void GenericComboPaintSubClass::DoPaint(bool validate_instead_of_paint)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	CRect clientrect;
	GetClientRect(clientrect);

	COMBOBOXINFO cbi;
	memset(&cbi, 0, sizeof(cbi));
	cbi.cbSize = sizeof(cbi);
	if (!::GetComboBoxInfo(m_hWnd, &cbi))
		return;

	CRgn rgn;
	if (!rgn.CreateRectRgnIndirect(clientrect))
		return;

	CRgn editrgn;
	if (!editrgn.CreateRectRgnIndirect(&cbi.rcItem))
		return;

	CRgn buttonrgn;
	if (!buttonrgn.CreateRectRgnIndirect(&cbi.rcButton))
		return;

	rgn.CombineRgn(&rgn, &editrgn, RGN_DIFF);
	rgn.CombineRgn(&rgn, &buttonrgn, RGN_DIFF);

	if (validate_instead_of_paint)
	{
		ValidateRect(&cbi.rcButton);
		ValidateRgn(&rgn);
		return;
	}

	if (gShellAttr->IsDevenv12OrHigher())
		OnTimer(MOUSE_TIMER_ID); // update last_highlight_state

	// draw borders
	CClientDC dc(this);
	IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
	COLORREF comboBgCol;
	if (gShellAttr->IsDevenv12OrHigher() && g_IdeSettings && comboinfo->ComboDrawsItsOwnBorder() &&
	    last_highlight_state & highlighted_draw_highlighted)
	{
		// [case: 74847] in vs2013 dark theme, the whole combo+edit should change bg color on mouseover
		// this is a start but incomplete, so revert to incorrect coloring.
		// comboBgCol = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseOverBackgroundBegin", FALSE);
		comboBgCol = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();
	}
	else
		comboBgCol = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();

	CBrush br;
	if (br.CreateSolidBrush(comboBgCol))
	{
		dc.FillRgn(&rgn, &br);
		ValidateRgn(&rgn);
	}

	if (comboinfo->ComboDrawsItsOwnBorder())
	{
		COLORREF borderColor;
		if (last_highlight_state & highlighted_draw_highlighted)
		{
			if (gShellAttr->IsDevenv12OrHigher() && g_IdeSettings)
			{
				// [case: 74727]
				borderColor = g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", FALSE);
			}
			else
				borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER);
		}
		else
			borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);

		CPen borderpen;
		if (borderpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, borderColor))
		{
			dc.SelectObject(&borderpen);
			dc.SelectObject(CBrush::FromHandle((HBRUSH)::GetStockObject(HOLLOW_BRUSH)));
			dc.Rectangle(&clientrect);
		}
	}

	// prepare drop button
	if (!gShellAttr->IsDevenv12OrHigher())
		OnTimer(MOUSE_TIMER_ID); // update last_highlight_state
	CRect buttonrect(cbi.rcButton);

	// draw button background
	CBrush buttonbrush;
	COLORREF buttonBgColor = CLR_INVALID;
	if (last_highlight_state & highlighted_button_pressed)
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			buttonBgColor = g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseDownBackground", false);
		else
			buttonBgColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEDOWN_BACKGROUND);
	}
	else if (last_highlight_state & (highlighted_mouse_inside | highlighted_edit_focused))
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		{
			buttonBgColor = g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseOverBackground", false);
		}
		else
		{
			const std::pair<double, COLORREF> gradients[] = {
			    std::make_pair(0, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_BEGIN)),
			    std::make_pair(0.49, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE1)),
			    std::make_pair(0.5, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE2)),
			    std::make_pair(1, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_END)),
			};
			DrawVerticalVS2010Gradient(dc, buttonrect, gradients, countof(gradients));
		}
	}
	else
	{
		buttonBgColor = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();
	}

	if (CLR_INVALID != buttonBgColor && buttonbrush.CreateSolidBrush(buttonBgColor))
		dc.FillRect(buttonrect, &buttonbrush);

	CPen buttonborderpen;
	COLORREF buttonBorderColor;
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		buttonBorderColor = last_highlight_state ? g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", false)
		                                         : g_IdeSettings->GetEnvironmentColor(L"DropDownBorder", false);
	else
		buttonBorderColor = last_highlight_state ? GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER)
		                                         : GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);
	if (buttonborderpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, buttonBorderColor))
	{
		dc.SelectObject(&buttonborderpen);

		// draw button borders
		if (GetVS2010ComboInfo()->HasArrowButtonRightBorder())
		{
			dc.MoveTo(buttonrect.right - 1, buttonrect.top);
			dc.LineTo(buttonrect.right - 1, buttonrect.bottom);
		}
		if (last_highlight_state)
		{ // any highlighted state has left border
			dc.MoveTo(buttonrect.left, buttonrect.top);
			dc.LineTo(buttonrect.left, buttonrect.bottom);
		}
	}

	// draw button arrow
	CPen arrowpen;
	COLORREF arrow_colour = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_GLYPH);
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
	{
		if (last_highlight_state & highlighted_button_pressed)
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseDownGlyph", false);
		else if (last_highlight_state & (highlighted_mouse_inside | highlighted_edit_focused))
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseOverGlyph", false);
		else
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuSubmenuGlyph", false);
	}

	if (arrowpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, arrow_colour))
	{
		dc.SelectObject(&arrowpen);
		CPoint arrowcenter = buttonrect.CenterPoint();
		const int arrowsize = VsUI::DpiHelper::LogicalToDeviceUnitsX(3);
		for (int i = 0; i < arrowsize; i++)
		{
			dc.MoveTo(arrowcenter.x - i, arrowcenter.y - i + arrowsize / 2);
			dc.LineTo(arrowcenter.x + i + 2, arrowcenter.y - i + arrowsize / 2);
		}
	}

	ValidateRgn(&buttonrgn);
}

// minihelp / va nav bar border paint
LRESULT GenericComboPaintSubClass::OnDrawMyBorder(WPARAM wparam, LPARAM lparam)
{
	if (!mVs2010ColouringIsActive)
		return 0;

	CDC& dc = *(CDC*)wparam;
	CRect rect = *(const CRect*)lparam;

	CSaveDC savedc(dc);
	CPen pen_inner, pen_outer;
	if (!pen_outer.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, GetVS2010VANavBarBkColour()))
		return 0;

	COLORREF borderColor;
	if (last_highlight_state)
	{
		if (gShellAttr->IsDevenv12OrHigher() && g_IdeSettings)
		{
			// [case: 74727]
			borderColor = g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", FALSE);
		}
		else
			borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER);
	}
	else
		borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);
	if (!pen_inner.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, borderColor))
		return 0;

	dc.SelectObject(&pen_outer);
	dc.SelectObject(::GetStockObject(HOLLOW_BRUSH));
	dc.Rectangle(rect);
	dc.SelectObject(&pen_inner);
	// 	rect.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsY(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
	rect.DeflateRect(1, 1, 1, 1);
	dc.Rectangle(rect);
	return 1;
}

CWnd* GenericComboPaintSubClass::GetSplitterWnd()
{
	CWnd* comboex = GetParent();
	if (!comboex)
		return NULL;
	CWnd* pane = comboex->GetParent();
	if (!pane)
		return NULL;
	return pane->GetParent();
}

IVS2010ComboInfo* GenericComboPaintSubClass::GetVS2010ComboInfo()
{
	CWnd* parent = GetParent();
	_ASSERTE(parent && dynamic_cast<IVS2010ComboInfo*>(parent));
	return parent ? dynamic_cast<IVS2010ComboInfo*>(parent) : NULL;
}

void GenericComboPaintSubClass::PreSubclassWindow()
{
	CWnd::PreSubclassWindow();

	if (mVs2010ColouringIsActive)
	{
		SetTimer(MOUSE_TIMER_ID, 50, NULL);

		// gmit: this is necessary for an initial update
		IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
		if (!comboinfo->ComboDrawsItsOwnBorder())
		{
			CWnd* splitter = GetSplitterWnd();
			if (splitter)
				splitter->PostMessage(WM_UPDATE_MY_BORDER, (WPARAM)m_hWnd);
		}
	}
}

void GenericComboPaintSubClass::OnTimer(UINT_PTR nIDEvent)
{
	if ((nIDEvent == MOUSE_TIMER_ID) && mVs2010ColouringIsActive)
	{
		CWnd* parent = GetParent();

		CComboBoxEx* comboex = mComboEx == parent ? mComboEx : dynamic_cast<CComboBoxEx*>(parent);

		if (!comboex)
			return;

		mComboEx = comboex;

		CPoint mouse((LPARAM)::GetMessagePos());
		CRect rect;
		comboex->GetWindowRect(rect);
		BOOL inside = rect.PtInRect(mouse);
		if (inside)
		{
			HWND wndMouse = ::WindowFromPoint(mouse);
			inside = wndMouse == comboex->m_hWnd || ::IsChild(comboex->m_hWnd, wndMouse);
		}
		IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
		bool dropped = !!SendMessage(CB_GETDROPPEDSTATE);
		dropped |= comboinfo->IsListDropped();
		CEdit* edit = comboex->GetEditCtrl();
		bool focused = edit && (::GetFocus() == edit->m_hWnd);

		uint new_highlight_state =
		    (last_highlight_state & ~highlighted_mouse_inside) | (inside ? highlighted_mouse_inside : 0u);
		new_highlight_state =
		    (new_highlight_state & ~highlighted_button_pressed) | (dropped ? highlighted_button_pressed : 0u);
		new_highlight_state =
		    (new_highlight_state & ~highlighted_edit_focused) | (focused ? highlighted_edit_focused : 0u);
		if (new_highlight_state != (uint)last_highlight_state)
		{
			comboinfo->OnHighlightStateChange((uint)new_highlight_state);
			if (comboinfo->ComboDrawsItsOwnBorder())
				InvalidateRect(NULL, false);
			else
			{
				CWnd* splitter = GetSplitterWnd();
				if (splitter)
					splitter->PostMessage(WM_UPDATE_MY_BORDER, (WPARAM)m_hWnd);
			}

			last_highlight_state = (highlight)new_highlight_state;
		}
		return;
	}

	CWnd::OnTimer(nIDEvent);
}

#define TT_TEXT "Select an entry to Goto. Type a few characters to filter list."
GenericComboSubClass3::GenericComboSubClass3()
    : ToolTipWrapper<CWndSubW<GenericComboPaintSubClass>>(NULL, TT_TEXT, 20, FALSE)
{
}

LRESULT GenericComboSubClass3::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT user_result = 0;
	if (m_combo && m_combo->OnComboWindowProc(message, wParam, lParam, user_result))
		return user_result;

	if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK)
	{
		m_combo->PostMessage(message, wParam, lParam);
		return TRUE;
	}
	if (message == WM_KEYDOWN)
	{
		if (wParam != VK_DOWN && wParam != VK_UP && wParam != VK_NEXT && wParam != VK_PRIOR)
			m_combo->OnDropdown();
		m_combo->m_lstCombo.SendMessage(message, wParam, lParam);
		return TRUE;
	}
	return BASE::WindowProc(message, wParam, lParam);
}

void GenericComboSubClass3::Init(CGenericComboBox* combo)
{
	m_combo = combo;
	SubclassWindowW(combo->m_hCombo);
}

BOOL GenericComboSubClass3::OkToDisplayTooltip()
{
	if (m_combo && m_combo->IsListDropped())
		return FALSE;
	return TRUE;
}

IMPLEMENT_DYNAMIC(GenericComboEditSubClass3, CWnd)
GenericComboEditSubClass3::GenericComboEditSubClass3() : ToolTipWrapper<CWndSubW<CWnd>>(NULL, TT_TEXT, 10, FALSE)
{
}

LRESULT GenericComboEditSubClass3::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT user_result = 0;
	if (m_combo && m_combo->OnEditWindowProc(message, wParam, lParam, user_result))
		return user_result;

	// 	if (message == WM_SETFOCUS){
	// 		CPoint pt;
	// 		GetCursorPos(&pt);
	// 		CRect rc;
	// 		GetWindowRect(&rc);
	// 		if (rc.PtInRect(pt) && (GetKeyState(VK_LBUTTON) & 0x1000))
	// 		{
	// 			LRESULT res = CWnd::WindowProc(message, wParam, lParam);
	// 			if (!m_combo->IsListDropped())
	// 				m_combo->OnDropdown();
	// 			return res;
	// 		}
	// 	}
	// 	if (message == WM_LBUTTONDOWN){
	// 		if (!m_combo->IsListDropped())
	// 		{
	// 			LRESULT res = CWnd::WindowProc(message, wParam, lParam);
	// 			m_combo->OnDropdown();
	// 			return res;
	// 		}
	// 	}

	if (m_readOnly)
	{
		if (EM_UNDO == message || EM_SETSEL == message || WM_CUT == message || WM_CLEAR == message ||
		    WM_PASTE == message)
		{
			return TRUE;
		}
	}

	if (message == WM_KILLFOCUS)
	{
		if ((HWND)wParam != m_combo->m_lstCombo.m_hWnd && (HWND)wParam != m_combo->m_hWnd)
			m_combo->DisplayList(FALSE);
	}
	if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_COMMAND)
	{
		if (gShellAttr->IsMsdev() && message == WM_KEYDOWN && (wParam == VK_NEXT || wParam == VK_PRIOR) &&
		    (GetKeyState(VK_CONTROL) & 0x1000))
		{
			_ASSERTE(g_WorkSpaceTab);
			g_WorkSpaceTab->SwitchTab(wParam == VK_NEXT);
			return TRUE;
		}

		if (wParam == VK_ESCAPE)
		{
			if (g_currentEdCnt)
			{
				bool didSetFocus(false);
				const CWnd* par = g_currentEdCnt->GetParent();
				if (par)
				{
					CWnd* par2 = par->GetParent();
					if (par2)
					{
						par2->SetFocus();
						didSetFocus = true;
					}
				}

				if (!didSetFocus)
					g_currentEdCnt->vSetFocus();
			}
			else if (gShellAttr->IsDevenv())
			{
				HWND foc = m_hWnd;
				for (int idx = 0; idx < 5; ++idx)
				{
					HWND tmp = ::GetParent(foc);
					if (tmp)
						foc = tmp;
				}
				::SetFocus(foc);
			}
			else
				AfxGetMainWnd()->SetFocus();
		}
		else if (message == WM_CHAR && wParam == VK_RETURN)
		{
			m_combo->OnSelect();
			m_combo->DisplayList(FALSE);
			return TRUE;
		}
		else if (m_readOnly)
			return TRUE;
	}
	else if (message == WM_SYSKEYDOWN && (gShellAttr->IsMsdev() || VK_DOWN == wParam || VK_UP == wParam))
	{
		// [case: 58357] support for alt+up/down opening FIS / SIS lists -
		//		but not alt by itself.  In vs, lone alt takes focus to menubar.
		// [case: 841] vc6
		if (!m_combo->GetDroppedState())
			m_combo->OnDropdown();
		return FALSE;
	}
	return CWnd::WindowProc(message, wParam, lParam);
}

void GenericComboEditSubClass3::Init(CGenericComboBox* combo)
{
	m_combo = combo;
	SubclassWindowW(combo->GetEditCtrl()->m_hWnd);
}

BOOL GenericComboEditSubClass3::OkToDisplayTooltip()
{
	if (m_combo && m_combo->IsListDropped())
		return FALSE;
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBox

CGenericComboBox::CGenericComboBox() : CWndSubW<CComboBoxEx>()
{
	m_nDroppedWidth = 0;
	m_nDroppedHeight = 0;
	m_drawImages = false;
	m_hasColorableContent = false;

	popup_bgbrush_cache.reset(new CGradientCache);
}

CGenericComboBox::~CGenericComboBox()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CGenericComboBox, CComboBox)
//{{AFX_MSG_MAP(CGenericComboBox)
ON_WM_LBUTTONDOWN()
ON_WM_CREATE()
ON_WM_LBUTTONDBLCLK()
ON_CONTROL_REFLECT(CBN_DROPDOWN, OnDropdown)
ON_CONTROL_REFLECT(CBN_EDITCHANGE, OnEditChange)
//}}AFX_MSG_MAP
ON_WM_DRAWITEM()
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBox message handlers

void CGenericComboBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (!IsWindow())
		Init();

	if (m_lstCombo.IsWindowVisible())
	{
		DisplayList(FALSE);
		if (m_edSub.m_hWnd && m_edSub.IsWindowVisible())
			m_edSub.SetFocus();
	}
	else if (m_edSub.m_hWnd == nullptr || !m_edSub.IsWindowVisible())
		OnDropdown();
	else
	{
		CRect ed_rect;
		m_edSub.GetWindowRect(ed_rect);
		ScreenToClient(ed_rect);

		CRect cb_rect;
		GetWindowRect(cb_rect);
		ScreenToClient(cb_rect);

		ed_rect.left = cb_rect.left;
		ed_rect.top = cb_rect.top;
		ed_rect.bottom = cb_rect.bottom;

		if (ed_rect.PtInRect(point))
			m_edSub.SetFocus();
		else
			OnDropdown();
	}
}

void CGenericComboBox::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	OnLButtonDown(nFlags, point);
}

void CGenericComboBox::DisplayList(BOOL bDisplay /* = TRUE*/)
{
	// 	CString defaultTitle;
	// 	int titleIconIdx;
	// 	GetDefaultTitleAndIconIdx(defaultTitle, titleIconIdx);
	//
	// 	if (!AllowPopup())
	// 		bDisplay = false;
	//
	// 	if (bDisplay)
	// 	{
	// 		CString txt;
	// 		GetEditCtrl()->GetWindowText(txt);
	// 		if (txt == defaultTitle)
	// 			SetItem("", titleIconIdx);
	// 	}
	// 	else
	// 		SetItem(defaultTitle, titleIconIdx);

	if (!bDisplay)
	{
		if (GetDroppedState())
			ReleaseCapture();
		if (IsWindow() && m_lstCombo.IsWindowVisible())
			m_lstCombo.ShowWindow(SW_HIDE);

		OnCloseup();
		return;
	}
	else if (!IsWindow())
		Init();

	if (bDisplay && GetDroppedState())
		return;

	CRect rc;
	GetWindowRect(rc);
	SetDroppedWidth(rc.Width());

	rc.top = rc.bottom;
	rc.right = rc.left + GetDroppedWidth();
	rc.bottom = rc.top + GetDroppedHeight();

	if (m_lstCombo.GetHandler())
		m_lstCombo.GetHandler()->DrawImages = GetDrawImages();

	m_lstCombo.Display(rc);
}
bool CGenericComboBox::IsListDropped() const
{
	return m_lstCombo.m_hWnd && ::IsWindow(m_lstCombo.m_hWnd) && m_lstCombo.IsWindowVisible();
}

int CGenericComboBox::GetDroppedHeight() const
{
	return m_nDroppedHeight;
}

int CGenericComboBox::GetDroppedWidth() const
{
	return m_nDroppedWidth;
}

int CGenericComboBox::SetDroppedHeight(int nHeight)
{
	m_nDroppedHeight = nHeight;
	return m_nDroppedHeight;
}

int CGenericComboBox::SetDroppedWidth(int nWidth)
{
	m_nDroppedWidth = nWidth;
	return m_nDroppedWidth;
}

BOOL CGenericComboBox::GetDroppedState() const
{
	return m_lstCombo.IsWindowVisible();
}

void CGenericComboBox::PreSubclassWindow()
{
	CComboBoxEx::PreSubclassWindow();
}

void CGenericComboBox::Init()
{
	if (!m_hasColorableContent)
		mySetProp(m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	WNDCLASS wndClass;
	if (!GetClassInfo(NULL, TEXT("LV_WITH_SHADOW"), &wndClass))
	{
		GetClassInfo(NULL, WC_LISTVIEW, &wndClass);
		wndClass.style |= CS_DROPSHADOW;
		wndClass.lpszClassName = TEXT("LV_WITH_SHADOW");
		RegisterClass(&wndClass);
	}

	m_hCombo = GetComboBoxCtrl()->m_hWnd;
	if (!m_lstCombo.m_hWnd)
	{
		CRect rc(0, 0, 100, 100);
#define LVS_EX_LABELTIP 0x00004000 // listview unfolds partly hidden labels if it does not have infotip text
		const DWORD exStyle = LVS_EX_LABELTIP | LVS_EX_ONECLICKACTIVATE | LVS_EX_TRACKSELECT | LVS_EX_FULLROWSELECT;
		const DWORD dwStyle = WS_CHILD | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
		                      LVS_SHOWSELALWAYS | LVS_OWNERDRAWFIXED | LVS_OWNERDATA | LVS_SHAREIMAGELISTS;
		// |LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER|LVS_REPORT|LVS_SHAREIMAGELISTS|LVS_SINGLESEL|LVS_OWNERDRAWFIXED|LVS_OWNERDATA
#if _MSC_VER <= 1200
		m_lstCombo.CreateEx(0, m_popupDropShadow ? TEXT("LV_WITH_SHADOW") : WC_LISTVIEW, NULL, dwStyle, rc, GetParent(),
		                    0);
#else
		((CWnd&)m_lstCombo)
		    .Create(m_popupDropShadow ? TEXT("LV_WITH_SHADOW") : WC_LISTVIEW, NULL, dwStyle, rc, GetParent(), 0);
#endif
		const DWORD dwStyleEx = m_lstCombo.GetExtendedStyle();
		m_lstCombo.SetExtendedStyle(exStyle | dwStyleEx);

		std::shared_ptr<CGenericComboBoxListCtrl::Handler> hndlr(new GenericComboListHandler(this));
		hndlr->ColorableContent = m_hasColorableContent;
		hndlr->DrawImages = GetDrawImages();

		m_lstCombo.Init(hndlr);
		m_lstCombo.SetFontType(VaFontType::EnvironmentFont);

		if (!m_comboSub.m_hWnd)
		{
			m_comboSub.Init(this);
			m_comboSub.SetVS2010ColouringActive(IsVS2010ColouringActive());
		}

		if (!m_edSub.m_hWnd)
			m_edSub.Init(this);

		m_lstCombo.SendMessage(CCM_SETUNICODEFORMAT, 1, 0);
		m_comboSub.SendMessage(CCM_SETUNICODEFORMAT, 1, 0);

		CRect rcAll;
		GetDroppedControlRect(&rcAll);
		GetWindowRect(&rc);
		SetDroppedWidth(rcAll.Width());
		SetDroppedHeight(rcAll.Height() - rc.Height());
	}

	SetImageList(nullptr, 0);

	if (!m_hasColorableContent)
	{
		mySetProp(m_hCombo, "__VA_do_not_colour", (HANDLE)1);
		mySetProp(m_lstCombo.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
		mySetProp(m_comboSub.m_hWnd, "__VA_do_not_colour", (HANDLE)1);

		if (m_edSub.m_hWnd)
			mySetProp(m_edSub.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	}

	gImgListMgr->SetImgList(m_lstCombo, ImageListManager::bgComboDropdown, LVSIL_SMALL);
}

#if _MSC_VER <= 1200
void CGenericComboBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CComboBoxEx::OnCreate(lpCreateStruct);
	Init();
}
#else
int CGenericComboBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	int retval = CComboBoxEx::OnCreate(lpCreateStruct);
	Init();
	return retval;
}
#endif

BOOL CGenericComboBox::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == m_hCombo)
	{
		if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_LBUTTONUP)
		{
			SendMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
	}
	if (pMsg->message == WM_KEYDOWN &&
	    (pMsg->wParam == VK_DOWN || pMsg->wParam == VK_UP || pMsg->wParam == VK_NEXT || pMsg->wParam == VK_PRIOR))
	{
		DisplayList(TRUE);
		m_lstCombo.SendMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
		return TRUE;
	}
	return CComboBoxEx::PreTranslateMessage(pMsg);
}

void CGenericComboBox::SetItemCount(int nItems)
{
	m_lstCombo.SetItemCount(nItems);
}

void CGenericComboBox::SetImageList(CImageList* pImageList, int nImageListType)
{
	m_lstCombo.SetImageList(pImageList, nImageListType);
}

int CGenericComboBox::GetItemsCount() const
{
	return m_lstCombo.GetItemCount();
}

POSITION
CGenericComboBox::GetFirstSelectedItemPosition() const
{
	return m_lstCombo.GetFirstSelectedItemPosition();
}

int CGenericComboBox::GetNextSelectedItem(POSITION& pos) const
{
	return m_lstCombo.GetNextSelectedItem(pos);
}

CString CGenericComboBox::GetItemText(int nItem, int nSubItem) const
{
	return m_lstCombo.GetItemText(nItem, nSubItem);
}

CStringW CGenericComboBox::GetItemTextW(int nItem, int nSubItem) const
{
	LVITEMW lvi;
	memset(&lvi, 0, sizeof(LVITEMW));
	lvi.iSubItem = nSubItem;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		lvi.cchTextMax = nLen;
		lvi.pszText = str.GetBufferSetLength(nLen);
		nRes = (int)::SendMessageW(m_lstCombo.m_hWnd, LVM_GETITEMTEXTW, (WPARAM)nItem, (LPARAM)&lvi);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

BOOL CGenericComboBox::GetItem(LVITEM* pItem) const
{
	return m_lstCombo.GetItem(pItem);
}

BOOL CGenericComboBox::SetItemState(int nItem, UINT nState, UINT nMask)
{
	return m_lstCombo.SetItemState(nItem, nState, nMask);
}

BOOL CGenericComboBox::EnsureVisible(int nItem, BOOL bPartialOK)
{
	return m_lstCombo.EnsureVisible(nItem, bPartialOK);
}

BOOL CGenericComboBox::IsWindow() const
{
	return ::IsWindow(m_lstCombo.m_hWnd);
}

int CGenericComboBox::SetSelectionMark(int iIndex)
{
	return m_lstCombo.SetSelectionMark(iIndex);
}

int CGenericComboBox::SetHotItem(int iIndex)
{
	return m_lstCombo.SetHotItem(iIndex);
}

LRESULT CGenericComboBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (IsVS2010ColouringActive())
	{
		std::optional<LRESULT> ret = ComboVS2010_CComboBoxEx_WndProc(m_hWnd, message, wParam, lParam,
		                                                             GetVS2010ComboBackgroundColour(), bgbrush_cache);
		if (ret)
			return *ret;
	}

	if (CB_SETCURSEL == message && m_lstCombo.m_hWnd)
	{
		SelectItem((int)wParam, false);
		return TRUE;
	}

	return CComboBoxEx::WindowProc(message, wParam, lParam);
}

COLORREF CGenericComboBox::GetVS2010ComboBackgroundColour() const
{
	return GetVS2010ComboColour(BACKGROUND);
}

bool CGenericComboBox::ComboDrawsItsOwnBorder() const
{
	return true;
}

bool CGenericComboBox::HasArrowButtonRightBorder() const
{
	return false;
}

void CGenericComboBox::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis)
{
	// DrawItem virtual function is no good; all items in comboboxex are customdrawn, so we don't have to put flag for
	// it

	do
	{
		if (m_hWnd == nullptr)
			break;

		if (dis->CtlType != ODT_COMBOBOX)
			break;

		COLORREF bgcolour = ::GetSysColor(COLOR_WINDOW);

		bool VS2010Colouring = IsVS2010ColouringActive();

		if (VS2010Colouring)
			bgcolour = GetVS2010ComboBackgroundColour();

		if (dis->itemState & ODS_COMBOBOXEDIT)
		{
			// draw item in edit control
			CBrush br;
			if (br.CreateSolidBrush(bgcolour))
				::FillRect(dis->hDC, &dis->rcItem, (HBRUSH)br.m_hObject);
			dis->itemAction |= ODA_DRAWENTIRE;
			dis->itemAction &= ~ODA_FOCUS;
			break; // do original drawing
		}
		else
		{
			// [wer] event id -1876336553, -1878036524 crash in GetComboBoxCtrl()
			HWND hBox = (HWND)::SendMessage(GetSafeHwnd(), CBEM_GETCOMBOCONTROL, 0, 0);
			if (!hBox)
				break;

			CComboBox* boxCtrl = (CComboBox*)CComboBox::FromHandle(hBox);
			if (!boxCtrl->GetSafeHwnd())
				break;

			// draw combo's listbox popup
			TextOutDc dc;
			if (!dc.Attach(dis->hDC))
				break;

			int savedc = dc.SaveDC();

			const bool selected = dis->itemState & ODS_SELECTED;
			COMBOBOXINFO cbi;
			memset(&cbi, 0, sizeof(cbi));
			cbi.cbSize = sizeof(cbi);
			boxCtrl->GetComboBoxInfo(&cbi);
			CRect rect;
			::GetClientRect(cbi.hwndList, rect);
			CRect itemrect = dis->rcItem;
			CRgn itemrgn;
			if (!itemrgn.CreateRectRgnIndirect(&itemrect))
				break;
			// gmit: this is a complete mystery to me why is this needed (discovered by accident), but, it seems that dc
			// is offset by -1,-1 pixel while drawing each item for the first time (!!)
			CPoint windoworg(0, 0);
			::GetWindowOrgEx(dc.m_hDC,
			                 &windoworg); // gmit: don't use MFC here since GetWindowOrgEx sometimes fails (?!)
			itemrgn.OffsetRgn(-windoworg);
			dc.SelectObject(&itemrgn);

			InitScrollWindowExPatch();
			mySetProp(cbi.hwndList, "__VA_do_not_scrollwindowex", (HANDLE)1);

			COLORREF sys_color_menu = GetSysColor(COLOR_MENU);
			const std::pair<double, COLORREF> back_gradients[] = {
			    std::make_pair(0, VS2010Colouring ? GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN) : sys_color_menu),
			    std::make_pair(1, VS2010Colouring ? GetVS2010ComboColour(POPUP_BACKGROUND_END) : sys_color_menu)};

			bool overridePaintType = false;
			if (selected)
			{
				if (Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorWizardBar &&
				    gShellAttr->IsDevenv11OrHigher())
				{
					// [case: 65047] don't color selected items in dev11
					overridePaintType = true;
				}

				COLORREF sys_color_hl = GetSysColor(COLOR_HIGHLIGHT);
				const std::pair<double, COLORREF> selection_gradients[] = {
				    std::make_pair(0,
				                   VS2010Colouring ? GetVS2010ComboColour(MOUSEOVER_BACKGROUND_BEGIN) : sys_color_hl),
				    std::make_pair(.49,
				                   VS2010Colouring ? GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE1) : sys_color_hl),
				    std::make_pair(.5,
				                   VS2010Colouring ? GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE2) : sys_color_hl),
				    std::make_pair(1, VS2010Colouring ? GetVS2010ComboColour(MOUSEOVER_BACKGROUND_END) : sys_color_hl)};
				const COLORREF border_colour = VS2010Colouring ? GetVS2010ComboColour(MOUSEOVER_BORDER) : sys_color_hl;

				DrawVS2010Selection(dc, itemrect, selection_gradients, countof(selection_gradients), border_colour,
				                    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, popup_bgbrush_cache.get(),
				                              _1, rect, back_gradients, (int)countof(back_gradients)),
				                    true);
			}
			else
				popup_bgbrush_cache->DrawVerticalVS2010Gradient(dc, rect, back_gradients, countof(back_gradients));

			if (-1 != dis->itemID)
			{
				const int horiz_border = 4; // VsUI::DpiHelper::LogicalToDeviceUnitsX(4);
				const int vert_border = 1;  // VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
				itemrect.DeflateRect(horiz_border, vert_border, horiz_border, vert_border);

				if (m_drawImages)
				{
					CImageList* il = GetImageList();
					if (il)
					{
						COMBOBOXEXITEM cbexi;
						memset(&cbexi, 0, sizeof(cbexi));
						cbexi.mask = CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
						cbexi.iItem = (INT_PTR)dis->itemID;
						((CComboBoxEx*)this)->GetItem(&cbexi);
						const int image = selected && cbexi.iSelectedImage != -1 ? cbexi.iSelectedImage : cbexi.iImage;
						IMAGEINFO ii;
						memset(&ii, 0, sizeof(ii));
						if ((image != I_IMAGECALLBACK) && (image != I_IMAGENONE))
						{
							il->GetImageInfo(image, &ii);
							il->Draw(
							    &dc, image,
							    CPoint(itemrect.left,
							           itemrect.top + (itemrect.Height() - (ii.rcImage.bottom - ii.rcImage.top)) / 2),
							    ILD_TRANSPARENT);
						}
						else
							il->GetImageInfo(0, &ii);
						itemrect.left += ii.rcImage.right - ii.rcImage.left + horiz_border;
					}
				}

				CStringW itemText = GetItemTextW((int)dis->itemID, 0);

				CFont* font = GetFont();
				if (font && font->m_hObject)
					dc.SelectObject(font);
				if (selected)
					dc.SetTextColor(VS2010Colouring ? CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_HOVER)
					                                : GetSysColor(COLOR_HIGHLIGHTTEXT));
				else
					dc.SetTextColor(VS2010Colouring ? CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE)
					                                : GetSysColor(COLOR_WINDOWTEXT));
				dc.SetBkMode(TRANSPARENT);
				TempPaintOverride t(overridePaintType);

				dc.DrawTextW(itemText, itemrect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}

			dc.RestoreDC(savedc);
			dc.Detach();
			return;
		}
	} while (false);

	CComboBoxEx::OnDrawItem(nIDCtl, dis);
}

void CGenericComboBox::OnHighlightStateChange(uint new_state) const
{
	::mySetProp(m_hWnd, "last_highlight_state", (HANDLE)(uintptr_t)new_state);
	if (highlight_state_change_event)
		highlight_state_change_event(new_state);
}

void CGenericComboBox::SettingsChanged()
{
	if (!IsVS2010ColouringActive())
		return;

	popup_bgbrush_cache.reset(new CGradientCache);
	// 	SetImageList(gImgListMgr->GetImgList(ImageListManager::bgComboDropdown), LVSIL_SMALL);
	if (gShellAttr->IsDevenv11OrHigher())
		bgbrush_cache.DeleteObject();
}

void CGenericComboBox::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFOA* pDispInfo = (LV_DISPINFOA*)pNMHDR;
	LV_ITEMA* pItem = &(pDispInfo)->item;

	CStringW item_str;

	if (pItem->mask & LVIF_TEXT)
		pItem->pszText[0] = '\0';

	if (pItem->mask & LVIF_IMAGE)
		pItem->iImage = -1;

	if (pItem->mask & LVIF_STATE)
		pItem->state = 0;

	if (pItem->iItem >= 0 && GetItemsCount() > pItem->iItem)
	{
		if (GetItemInfo(pItem->iItem, (pItem->mask & LVIF_TEXT) ? &item_str : nullptr, nullptr,
		                (pItem->mask & LVIF_IMAGE) ? &pItem->iImage : nullptr,
		                (pItem->mask & LVIF_STATE) ? &pItem->state : nullptr))
		{
			if (pItem->mask & LVIF_TEXT)
			{
				WTString astr = ::WideToMbcs(item_str, item_str.GetLength());
				::strcpy_s(pItem->pszText, (uint)pItem->cchTextMax, (LPCSTR)astr.c_str());
			}
		}
	}

	*pResult = 0;
}

void CGenericComboBox::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;
	LV_ITEMW* pItem = &(pDispInfo)->item;

	CStringW item_str;

	if (pItem->mask & LVIF_TEXT)
		pItem->pszText[0] = '\0';

	if (pItem->mask & LVIF_IMAGE)
		pItem->iImage = -1;

	if (pItem->mask & LVIF_STATE)
		pItem->state = 0;

	if (pItem->iItem >= 0 && GetItemsCount() > pItem->iItem)
	{
		if (GetItemInfo(pItem->iItem, (pItem->mask & LVIF_TEXT) ? &item_str : nullptr, nullptr,
		                (pItem->mask & LVIF_IMAGE) ? &pItem->iImage : nullptr,
		                (pItem->mask & LVIF_STATE) ? &pItem->state : nullptr))
		{
			if (pItem->mask & LVIF_TEXT)
				::wcscpy_s(pItem->pszText, (uint)pItem->cchTextMax, (LPCWSTR)item_str);
		}
	}

	*pResult = 0;
}
