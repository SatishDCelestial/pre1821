#include "stdafxed.h"
#include "VAThemeDraw.h"
#include "IdeSettings.h"
#include "FontSettings.h"
#include <windowsx.h>
#include "ImageListManager.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include <algorithm>
#include "TextOutDc.h"
#include "MenuXP\Tools.h"

#pragma push_macro("SelectFont")
#undef SelectFont
#include <afxshelllistctrl.h>
#pragma pop_macro("SelectFont")
//#include <afxdrawmanager.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifndef TVM_SETEXTENDEDSTYLE
#define TVM_SETEXTENDEDSTYLE 4396
#endif

#ifndef TVS_EX_DOUBLEBUFFER
#define TVS_EX_DOUBLEBUFFER 0x0004
#endif

#ifndef TVS_EX_FADEINOUTEXPANDOS
#define TVS_EX_FADEINOUTEXPANDOS 0x0040
#endif

using namespace ThemeUtils;

NS_THEMEDRAW_BEGIN

VADlgIDE11Draw::VADlgIDE11Draw() : ThemeRenderer<CDialog>(f_DiscardEraseBG | f_BorderInNCPaint | f_PreMessages)
{
	// NewProjectDialog: Background or BackgroundLowerRegion
	crBG = g_IdeSettings ? g_IdeSettings->GetNewProjectDlgColor(L"BackgroundLowerRegion", FALSE)
	                     : ::GetSysColor(COLOR_BACKGROUND);

	crTxt = g_IdeSettings ? g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE) : ::GetSysColor(COLOR_WINDOWTEXT);

	enabled = false;
	size_box = nullptr;
	size_box_visible_on_attach = false;
}

void VADlgIDE11Draw::PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CDialog>& context)
{
	if (enabled)
	{
		CBrush myBrush(crBG); // dialog background color
		CBrush* pOld = dc.SelectObject(&myBrush);
		dc.PatBlt(0, 0, rect.Width(), rect.Height(), PATCOPY);
		dc.SelectObject(pOld); // restore old brush
		context.SetResult(TRUE, !context.IsPaintMessage());
	}
}

void VADlgIDE11Draw::PrePaintForeground(CDC& dc, CRect& rect, ThemeContext<CDialog>& context)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(::WindowFromDC(dc));

	if (size_box)
	{
		ShowSizeBox(nullptr, false);

		dc.FillSolidRect(rect, crBG);

		static SIZE grip_size = {};
		if (grip_size.cx == 0 && grip_size.cy == 0)
		{
			HBITMAP hBmp = LoadBitmap(NULL, MAKEINTRESOURCE(OBM_SIZE));
			BITMAP bmp = {};
			if (GetObject(hBmp, sizeof(BITMAP), &bmp))
			{
				grip_size.cx = bmp.bmWidth;
				grip_size.cy = bmp.bmHeight;
			}
			DeleteObject(hBmp);
		}

		int wdth = VsUI::DpiHelper::LogicalToDeviceUnitsX(8);
		int hght = VsUI::DpiHelper::LogicalToDeviceUnitsY(8);
		int px = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
		int py = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);

		POINT o = {rect.right - wdth - px, rect.bottom - hght - py};
		const char* grip = "   x"
		                   "  xx"
		                   " xxx"
		                   "xxxx";

		CPen grip_x(0, px, InterpolateColor(crBG, RGB(255, 255, 255), 0.5));
		CPen grip_o(0, px, InterpolateColor(crBG, RGB(0, 0, 0), 0.5));

		int xOffset = wdth / 4;
		int yOffset = hght / 4;

		AutoSaveRestoreDC asr(dc);
		for (int y = 0; y < 4; y++)
		{
			for (int x = 0; x < 4; x++)
			{
				int i = y * 4 + x;
				char ch = grip[i];

				if (ch != ' ')
				{
					dc.SelectObject(&grip_x);
					dc.MoveTo(o.x + x * xOffset, o.y + y * yOffset);
					dc.LineTo(o.x + x * xOffset + 1, o.y + y * yOffset);

					dc.SelectObject(&grip_o);
					dc.MoveTo(o.x + x * xOffset - px, o.y + y * yOffset - py);
					dc.LineTo(o.x + x * xOffset + 1 - px, o.y + y * yOffset - py);
				}
			}
		}
	}
}

void VADlgIDE11Draw::Attach(CDialog* wnd, ThemeContext<CDialog>& context)
{
	__super::Attach(wnd, context);

	wnd->ModifyStyle(0, WS_CLIPCHILDREN);

	enabled = g_IdeSettings != nullptr;
	if (!enabled)
		return;

	ShowSizeBox(wnd, false);
}

void VADlgIDE11Draw::Detach(CDialog* wnd, ThemeContext<CDialog>& context)
{
	enabled = false;

	ShowSizeBox(nullptr, true);

	size_box = nullptr;
	size_box_visible_on_attach = false;

	__super::Detach(wnd, context);
}

void VADlgIDE11Draw::OnStateChanged(const ItemState& state, CWnd* wnd)
{
	wnd->RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
}

void VADlgIDE11Draw::WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CDialog* wnd,
                                   ThemeContext<CDialog>& context)
{
	if (message == WM_SIZE)
	{
		wnd->RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
	}

	// [case: 82332] - added HTBOTTOMRIGHT for size box
	else if (message == WM_NCHITTEST && size_box)
	{
		CRect r;
		wnd->GetClientRect(&r);
		int size = ::GetSystemMetrics(SM_CXVSCROLL);
		r.left = r.right - size;
		r.top = r.bottom - size;

		POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		wnd->ScreenToClient(&point);

		if (r.PtInRect(point))
		{
			context.SetResult(HTBOTTOMRIGHT);
		}

		return;
	}

	__super::WindowProcPre(message, wParam, lParam, wnd, context);
}

void VADlgIDE11Draw::ShowSizeBox(CDialog* wnd, bool show)
{
	if (size_box == nullptr && wnd != nullptr)
	{
		struct data
		{
			HWND parent;
			HWND size_box;
		};

		struct EnumWnds
		{
			static BOOL CALLBACK Proc(HWND hWnd, LPARAM lParam)
			{
				data* d = (data*)lParam;

				if (GetParent(hWnd) != d->parent)
					return TRUE;

				char class_name[128];
				::GetClassNameA(hWnd, class_name, 128);
				if (_strcmpi(class_name, "ScrollBar") == 0)
				{
					if (::GetWindowLong(hWnd, GWL_STYLE) & SBS_SIZEBOX)
					{
						d->size_box = hWnd;
						return FALSE;
					}
				}
				return TRUE;
			}
		};

		data d = {wnd->GetSafeHwnd(), nullptr};
		EnumChildWindows(d.parent, &EnumWnds::Proc, (LPARAM)&d);

		if (d.size_box != 0 && ::IsWindow(d.size_box))
		{
			size_box = d.size_box;
			size_box_visible_on_attach = IsWindowVisible(size_box) != FALSE;
		}
	}

	if (size_box && ::IsWindow(size_box))
	{
		bool is_visible = IsWindowVisible(size_box) != FALSE;
		bool should_be = size_box_visible_on_attach && show;
		if (should_be != is_visible)
			::ShowWindow(size_box, should_be ? SW_SHOW : SW_HIDE);
	}
}

HBRUSH VADlgIDE11Draw::HandleCtlColor(CDialog* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CDialog>& context)
{
	pDC->SetTextColor(crTxt);
	pDC->SetBkColor(crBG);
	return BrushMap(context).GetHBRUSH(crBG);
}

void BtnIDE11Draw::Attach(CButton* wnd, ThemeContext<CButton>& context)
{
	__super::Attach(wnd, context);

	if (g_IdeSettings)
	{
		styles.InitFromWnd(wnd);

		if (modifyStyles)
			styles.ModifyStyle(wnd, 0, BS_OWNERDRAW);

		button = wnd;

		focus_border = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);

#ifdef AVR_STUDIO
		// normal state BG/text/border
		colors[0] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", FALSE); // Button
		colors[1] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", TRUE);  // Button
		colors[2] = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxBorder", FALSE);                 // ButtonBorder

		// mouse over state BG/text/border
		colors[3] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", FALSE); // ButtonMouseOver
		colors[4] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", TRUE);  // ButtonMouseOver
		colors[5] = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxMouseOverBorder", FALSE); // ButtonMouseOverBorder

		// pressed state BG/text/border
		colors[6] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", FALSE); // ButtonPressed
		colors[7] = g_IdeSettings->GetNewProjectDlgColor(L"WonderbarTreeInactiveSelected", TRUE);  // ButtonPressed
		colors[8] = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE); // ButtonPressedBorder

		// disabled state BG/text/border
		colors[9] = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxDisabled", FALSE);        // ButtonDisabled
		colors[10] = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxDisabled", TRUE);        // ButtonDisabled
		colors[11] = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxDisabledBorder", FALSE); // ButtonDisabledBorder
#else
		colors[0] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"Button", FALSE);
		colors[1] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"Button", TRUE);
		colors[2] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonBorder", FALSE);

		colors[3] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOver", FALSE);
		colors[4] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOver", TRUE);
		colors[5] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOverBorder", FALSE);

		colors[6] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressed", FALSE);
		colors[7] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressed", TRUE);
		colors[8] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressedBorder", FALSE);

		colors[9] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonDisabled", FALSE);
		colors[10] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonDisabled", TRUE);
		colors[11] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonDisabledBorder", FALSE);
#if defined(RAD_STUDIO)
		colors[12] = g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonFocused", FALSE);
#endif
#endif
	}
}

void BtnIDE11Draw::Detach(CButton* wnd, ThemeContext<CButton>& context)
{
	if (button && modifyStyles)
		styles.RevertChanges(button);

	button = nullptr;

	__super::Detach(wnd, context);
}

void BtnIDE11Draw::PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context)
{
	if (button == nullptr || m_dpiHandler == nullptr)
		return;

	COLORREF bg, txt, border;
	bg = colors[0];
	txt = colors[1];
	border = colors[2];

	if (context.State.IsMouseDown())
	{
		bg = colors[6];
		txt = colors[7];
		border = colors[8];
	}
	else if (context.State.IsInactive())
	{
		bg = colors[9];
		txt = colors[10];
		border = colors[11];
	}
	else if (context.State.IsMouseOver())
	{
		bg = colors[3];
		txt = colors[4];
		border = colors[5];
	}
	else if (context.State.IsFocused())
	{
#if defined(RAD_STUDIO)
		bg = colors[12];
#endif
	}

	if (button->GetCheck())
		border = colors[8];

	dc.FillSolidRect(rect, bg);
	//	CBrush brush(border);
	//	dc.FrameRect(rect, &brush);
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*button);
	if (gShellAttr && gShellAttr->IsCppBuilder())
	{
		if (context.State.IsMouseOver() && !context.State.IsMouseDown())
		{
			// thinner border for mouseover
			CRect hotRect = rect;
			hotRect.InflateRect(0, 0, 2, 2);
			ThemeUtils::DrawFocusRect(dc, hotRect, focus_border, false);
		}
		else if (context.State.IsFocused() && context.State.IsFocusVisible() && !context.State.IsMouseDown())
		{
			// thicker border for focus
			ThemeUtils::FrameRectDPI(dc, rect, focus_border, RECT{2, 2, 2, 2});
		}
		else
		{
			// standard border drawing
			ThemeUtils::FrameRectDPI(dc, rect, border);
		}
	}
	else
		ThemeUtils::FrameRectDPI(dc, rect, border);

	CString strTemp;
	button->GetWindowText(strTemp);
	AutoTextColor atc(dc, txt);

	AutoSelectGDIObj afont(dc, m_dpiHandler->GetDpiAwareFont());

	if (context.State.IsPrefixVisible())
		dc.DrawText(strTemp, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	else
		dc.DrawText(strTemp, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);

	if (context.State.IsFocused() && context.State.IsFocusVisible() && !context.State.IsMouseDown() &&
	    (!gShellAttr || !gShellAttr->IsCppBuilder()))
	{
		CRect focusRect = rect;
		focusRect.InflateRect(-2, -2);
		ThemeUtils::DrawFocusRect(dc, focusRect, focus_border);
	}
}

void BtnIDE11Draw::WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                  ThemeContext<CButton>& context)
{
	__super::WindowProcPost(message, wParam, lParam, wnd, context);

	if (message == WM_UPDATEUISTATE)
		ThemeUtils::DelayedRefresh(wnd, 50);
	else if (message == WM_SETTEXT || message == BM_SETCHECK || message == WM_ENABLE)
		wnd->RedrawWindow(NULL, NULL,
		                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
}

void BtnIDE11DrawIcon::PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context)
{
	if (button == nullptr || m_dpiHandler == nullptr)
		return;

	COLORREF bg, txt, border;
	bg = colors[0];
	txt = colors[1];
	border = colors[2];

	if (context.State.IsMouseDown())
	{
		bg = colors[6];
		txt = colors[7];
		border = colors[8];
	}
	else if (context.State.IsInactive())
	{
		bg = colors[9];
		txt = colors[10];
		border = colors[11];
	}
	else if (context.State.IsMouseOver())
	{
		bg = colors[3];
		txt = colors[4];
		border = colors[5];
	}

	if (button->GetCheck())
	{
		bg = colors[3];
		border = colors[8];
	}

	dc.FillSolidRect(rect, bg);

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*button);
	ThemeUtils::FrameRectDPI(dc, rect, border);

	// try to get icon and if it exists, scale and handle this icon instead of the text as in base class
	HICON hIcon = button->GetIcon();
	if (hIcon)
	{
		// get button icon size needed to calculate scaling
		SIZE iconSize = {0, 0};
		ICONINFO ii;
		BOOL fResult = GetIconInfo(hIcon, &ii);
		if (fResult)
		{
			BITMAP bm;
			fResult = GetObject(ii.hbmMask, sizeof(bm), &bm) == sizeof(bm);
			if (fResult)
			{
				iconSize.cx = bm.bmWidth;
				iconSize.cy = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
			}
			if (ii.hbmMask)
				DeleteObject(ii.hbmMask);
			if (ii.hbmColor)
				DeleteObject(ii.hbmColor);
		}

		if (iconSize.cx > 0 && iconSize.cy > 0)
		{
			// calculate icon scaling and keep aspect ratio
			double scaleFactorX = (double)rect.Width() / iconSize.cx;
			double scaleFactorY = (double)rect.Height() / iconSize.cy;

			double scaleFactorMin = std::min(scaleFactorX, scaleFactorY);

			int calculatedWidth = (int)std::round(iconSize.cx * scaleFactorMin);
			int calculatedHeight = (int)std::round(iconSize.cy * scaleFactorMin);

			// calculate top left position for centered icon
			int calculatedX = (rect.Width() - calculatedWidth) / 2;
			int calculatedY = (rect.Height() - calculatedHeight) / 2;
			
			// draw icon
			DrawIconEx(dc, calculatedX, calculatedY, hIcon, calculatedWidth, calculatedHeight, 0, NULL, DI_NORMAL);
		}
	}
	else
	{
		// no icon, so just do the same as in base BtnIDE11Draw::PrePaintBackground
		// generally this is not needed but is here for scenario if somebody use
		// BtnIDE11DrawIcon instead of base BtnIDE11Draw so it will still work as expected
		CString strTemp;
		button->GetWindowText(strTemp);
		AutoTextColor atc(dc, txt);

		AutoSelectGDIObj afont(dc, m_dpiHandler->GetDpiAwareFont());

		if (context.State.IsPrefixVisible())
			dc.DrawText(strTemp, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		else
			dc.DrawText(strTemp, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
	}

	if (context.State.IsFocused() && context.State.IsFocusVisible() && !context.State.IsMouseDown())
	{
		CRect focusRect = rect;
		focusRect.InflateRect(-2, -2);
		ThemeUtils::DrawFocusRect(dc, focusRect, focus_border);
	}
}

void CheckIDE11Draw::Attach(CButton* wnd, ThemeContext<CButton>& context)
{
	__super::Attach(wnd, context);

	// these are custom flags within ItemState
	m_BM_SETSTATE = context.State.GetNextAvailableFlag();
	m_BM_DONT_CLICK = context.State.GetNextAvailableFlag();

	styles.InitFromWnd(wnd);

	if (styles.OldStyle() & BS_PUSHLIKE)
	{
		btnDraw.reset(new BtnIDE11Draw(false));
		btnDraw->Attach(wnd, context);
	}

	if (g_IdeSettings)
	{
		if (styles.OldStyle() & BS_AUTORADIOBUTTON)
			::mySetProp(wnd->GetSafeHwnd(), "__VA_BS_AUTORADIOBUTTON", (HANDLE)1);

		button = wnd;

		const GUID& npc = ThemeCategory11::NewProjectDialog;
		const GUID& tec = ThemeCategory11::TeamExplorer;

		// Label area BG
		colors[0] = g_IdeSettings->GetThemeColor(npc, L"BackgroundLowerRegion", FALSE);

		// focus rect
		colors[1] = g_IdeSettings->GetThemeColor(npc, L"InputFocusBorder", FALSE);

		// Default state BG, glyph, border, label text
		colors[2] = g_IdeSettings->GetThemeColor(npc, L"CheckBox", FALSE);
		colors[3] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		colors[4] = g_IdeSettings->GetThemeColor(npc, L"CheckBox", TRUE);
		colors[5] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);

		// Mouse over state BG, glyph, border, label text
		colors[6] = g_IdeSettings->GetThemeColor(npc, L"CheckBoxMouseOver", FALSE);
		colors[7] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		colors[8] = g_IdeSettings->GetThemeColor(npc, L"CheckBoxMouseOver", TRUE);
		colors[9] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);

		// Mouse down state BG, glyph, border, label text
		colors[10] = g_IdeSettings->GetThemeColor(npc, L"CheckBoxMouseOver", FALSE);
		colors[11] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		colors[12] = g_IdeSettings->GetThemeColor(npc, L"CheckBoxMouseOver", TRUE);
		colors[13] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);

		// Disabled control state BG, glyph, border, label text
#ifdef AVR_STUDIO
		colors[14] = colors[0];
		colors[15] = g_IdeSettings->GetThemeColor(npc, L"CheckBox", TRUE);
		colors[16] = g_IdeSettings->GetThemeColor(npc, L"TextBoxDisabledBorder", FALSE);
#else
		colors[14] = g_IdeSettings->GetThemeColor(tec, L"ButtonDisabled", FALSE);
		colors[15] = g_IdeSettings->GetThemeColor(tec, L"ButtonDisabled", TRUE);
		colors[16] = g_IdeSettings->GetThemeColor(tec, L"ButtonDisabledBorder", FALSE);
#endif
		colors[17] = g_IdeSettings->GetEnvironmentColor(L"GrayText", FALSE);
	}
}

void CheckIDE11Draw::Detach(CButton* wnd, ThemeContext<CButton>& context)
{
	if (btnDraw)
	{
		btnDraw->Detach(wnd, context);
		btnDraw.reset();
	}

	if (styles.OldStyle() & BS_AUTORADIOBUTTON)
		::myRemoveProp(wnd->GetSafeHwnd(), "__VA_BS_AUTORADIOBUTTON");

	if (button)
		styles.RevertChanges(button);

	button = nullptr;

	__super::Detach(wnd, context);
}

HBRUSH CheckIDE11Draw::HandleCtlColor(CButton* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CButton>& context)
{
	COLORREF bg, glyph, border, text;
	GetColors(bg, glyph, border, text, context);

	pDC->SetTextColor(text);
	pDC->SetBkColor(bg);
	return BrushMap(context).GetHBRUSH(bg);
}

void CheckIDE11Draw::PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context)
{
	if (button == nullptr || m_dpiHandler == nullptr)
		return;

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(button);
	AutoSaveRestoreDC autoDC(dc);

	COLORREF bg, glyph, border, text;
	GetColors(bg, glyph, border, text, context);

	dc.FillSolidRect(rect, colors[0]);

	AutoSelectGDIObj afont(dc, m_dpiHandler->GetDpiAwareFont());

	////////////////////////////////////////////////////////////////////////////////////////
	// Used DLU values are deduced from values specified here:
	// http://msdn.microsoft.com/en-us/library/windows/desktop/dn742486%28v=vs.85%29.aspx
	////////////////////////////////////////////////////////////////////////////////////////

	CRect margins;
	margins.bottom = 7; // check box height/width in DLUs!
	margins.right = 2;  // label offset from right edge in DLUs!

	CWnd* parent = button->GetParent();
	if (!parent)
		return;

	::MapDialogRect(parent->m_hWnd, &margins);

	CRect chbRect;
	chbRect.bottom = margins.Height();
	chbRect.right = margins.Height();

	if (chbRect.Height() < 13)
	{
		chbRect.bottom = 13;
		chbRect.right = 13;
	}

	if (chbRect.Height() > rect.Height())
	{
		chbRect.bottom = rect.Height();
		chbRect.right = rect.Width();
	}

	chbRect.OffsetRect(rect.TopLeft());

	int chbRHDiff = rect.Height() - chbRect.Height();
	if (chbRHDiff > 0)
		chbRect.OffsetRect(0, chbRHDiff / 2);

	DrawCheck(dc, bg, border, glyph, chbRect, button->GetCheck());

	CRect txtRect = rect;
	txtRect.left = chbRect.right + margins.Width();
	txtRect.top = chbRect.top;
	txtRect.bottom = chbRect.bottom;

	{
		CStringW strTemp;
		GetWindowTextW(button->GetSafeHwnd(), strTemp);
		AutoTextColor atc(dc, text);

		if (context.State.IsPrefixVisible())
			::DrawTextW(dc, strTemp, strTemp.GetLength(), txtRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
		else
			::DrawTextW(dc, strTemp, strTemp.GetLength(), txtRect,
			            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX | DT_NOCLIP);

		if (context.State.IsFocused() /*&& context.State.IsFocusVisible()*/ && !context.State.IsMouseDown())
		{
			CRect focusRect = txtRect;
			::DrawTextW(dc, strTemp, strTemp.GetLength(), focusRect,
			            DT_CALCRECT | DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX | DT_NOCLIP);
			focusRect.InflateRect(1, 1);
			focusRect.IntersectRect(rect, focusRect);

			//::DrawFocusRect(dc, focusRect);

			ThemeUtils::DrawFocusRect(dc, focusRect, colors[1]);
		}
	}

	context.SetResult(TRUE, !context.IsPaintMessage());
}

void CheckIDE11Draw::DrawCheck(CDC& dc, COLORREF bg, COLORREF border, COLORREF glyph, CRect& chbRect, int state)
{
	if (button)
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*button);

		dc.FillSolidRect(chbRect, bg);

		ThemeUtils::FrameRectDPI(dc, chbRect, border);

		if (state == 0)
			return;

		ThemeUtils::DrawSymbol(dc, chbRect, glyph, ThemeUtils::Symb_Check);
	}
}

void CheckIDE11Draw::WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                    ThemeContext<CButton>& context)
{
	__super::WindowProcPost(message, wParam, lParam, wnd, context);

	if (message == WM_SETTEXT || message == BM_SETCHECK)
		wnd->RedrawWindow(NULL, NULL,
		                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
}

void CheckIDE11Draw::WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                   ThemeContext<CButton>& context)
{
	if (btnDraw &&
	    (btnDraw->IsPaintMessage(message) || btnDraw->IsNCPaintMessage(message) || btnDraw->IsEraseBGMessage(message)))
		btnDraw->WindowProcPre(message, wParam, lParam, wnd, context);
	else
	{
		// call base method before as it also could modify CallDefault value
		__super::WindowProcPre(message, wParam, lParam, wnd, context);

		// We do not need to allow original window message to process WM_UPDATEUISTATE
		// as it would draw its stuff and it is wrong. Our state handler handled all
		// state info already, so we can dismiss this message. (hopefully)
		// I hope that it can be ignored as BS_OWNERDRAW also does not process that message.
		if (message == WM_UPDATEUISTATE || message == WM_ENABLE)
			context.SetResult(TRUE);
	}
}

void CheckIDE11Draw::GetColors(COLORREF& bg, COLORREF& glyph, COLORREF& border, COLORREF& text,
                               ThemeContext<CButton>& context)
{
	bg = colors[2];
	glyph = colors[3];
	border = colors[4];
	text = colors[5];

	if (context.State.IsMouseDown())
	{
		bg = colors[10];
		glyph = colors[11];
		border = colors[12];
		text = colors[13];
	}
	else if (context.State.IsInactive())
	{
		bg = colors[14];
		glyph = colors[15];
		border = colors[16];
		text = colors[17];
	}
	else if (context.State.IsMouseOver())
	{
		bg = colors[6];
		glyph = colors[7];
		border = colors[8];
		text = colors[9];
	}
}

void RadioIDE11Draw::DrawCheck(CDC& dc, COLORREF bg, COLORREF border, COLORREF glyph, CRect& chbRect, int state)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(button->m_hWnd);

	GDIPlusManager::EnsureGDIPlus();

	Gdiplus::Color bg_color, border_color, glyph_color;
	bg_color.SetFromCOLORREF(bg);
	border_color.SetFromCOLORREF(border);
	glyph_color.SetFromCOLORREF(glyph);

	Gdiplus::SolidBrush bg_brush(bg_color);

	int width = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
	Gdiplus::Pen border_pen(border_color, (float)width);

	Gdiplus::SolidBrush glyph_brush(glyph_color);

	Gdiplus::Graphics gr(dc);
	gr.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

	Gdiplus::RectF rect =
	    Gdiplus::RectF((float)chbRect.left,
	                   (float)(chbRect.top + width), // +1px top offset between check box and radio button
	                   (float)(chbRect.Width() -
	                           2 * width), // -1px pixel alignment, -1px difference between check box and radio button
	                   (float)(chbRect.Height() -
	                           2 * width) // -1px pixel alignment, -1px difference between check box and radio button
	    );

	gr.FillEllipse(&bg_brush, rect);
	gr.DrawEllipse(&border_pen, rect);

	if (state)
	{
		rect.Inflate(-rect.Width / 5.0f, -rect.Height / 5.0f);
		gr.FillEllipse(&glyph_brush, rect);
	}
}

void DimEditIDE11Draw::Attach(CDimEditCtrl* wnd, ThemeContext<CDimEditCtrl>& context)
{
	__super::Attach(wnd, context);

	// mouse does not change state when
	// a LEAVE method is called while mouse
	// cursor is physically over control
	context.State.SetExtendedMouseTracking(true);

	if (g_IdeSettings)
	{
		edit = wnd;
		const GUID& cat = ThemeCategory11::NewProjectDialog;

		// normal state BG/text/border/dim
		colors[0] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[1] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[2] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBorder", FALSE);
		colors[3] = ThemeUtils::InterpolateColor(colors[0], colors[1], 0.4f);

		// mouse over state BG/text/border/dim
		colors[4] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[5] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[6] = g_IdeSettings->GetThemeColor(cat, L"TextBoxMouseOverBorder", FALSE);
		colors[7] = ThemeUtils::InterpolateColor(colors[4], colors[5], 0.6f);

		// active state BG/text/border/dim
		colors[8] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[9] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[10] = g_IdeSettings->GetThemeColor(cat, L"InputFocusBorder", FALSE);
		colors[11] = ThemeUtils::InterpolateColor(colors[8], colors[9], 0.5f);

		// disabled state BG/text/border/dim
		colors[12] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabled", FALSE);
		colors[13] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabled", TRUE);
		colors[14] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabledBorder", FALSE);
		colors[15] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabled", FALSE);

		edit->SetDimColor(colors[3]);
		edit->SetCustomDisabledColors(colors[0], colors[1], colors[2], false);
		edit->SetUseCustomColors(true);
	}
}

void DimEditIDE11Draw::Detach(CDimEditCtrl* wnd, ThemeContext<CDimEditCtrl>& context)
{
	if (edit)
		edit->SetUseCustomColors(false);

	edit = nullptr;

	__super::Detach(wnd, context);
}

HBRUSH DimEditIDE11Draw::HandleCtlColor(CDimEditCtrl* wnd, CDC* pDC, UINT nCtlColor,
                                        ThemeContext<CDimEditCtrl>& context)
{
	if (edit == nullptr)
		return nullptr;

	SetColors(context.State);
	return wnd->CtlColor(pDC, nCtlColor);
}

void DimEditIDE11Draw::OnStateChanged(const ItemState& state, CWnd* wnd)
{
	// 	WTString state_str;
	// 	state.ToString(state_str);
	// 	ThemeUtils::TraceFramePrint(state_str);

	SetColors(state);
	__super::OnStateChanged(state, wnd);
}

void DimEditIDE11Draw::SetColors(const ItemState& state)
{
	if (edit == nullptr)
		return;

	if (state.IsMouseOver() && !state.IsFocused())
	{
		edit->SetDimColor(colors[7]);
		edit->SetCustomColors(colors[4], colors[5], colors[6], false);
	}
	else if (state.IsMouseDown() || state.IsFocused())
	{
		edit->SetDimColor(colors[11]);
		edit->SetCustomColors(colors[8], colors[9], colors[10], false);
	}
	else if (state.IsInactive())
	{
		edit->SetDimColor(colors[15]);
		edit->SetCustomColors(colors[12], colors[13], colors[14], false);
	}
	else
	{
		edit->SetDimColor(colors[3]);
		edit->SetCustomColors(colors[0], colors[1], colors[2], false);
	}
}

void EditIDE11Draw::PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CEdit>& context)
{
	if (edit != nullptr)
	{
		ThemeUtils::FillNonClientArea(edit, colors[(int)bg_index]);
		ThemeUtils::DrawNCBorder(edit, colors[(int)bg_index + 2]);
	}
}

COLORREF EditIDE11Draw::GetTextColor(ColorIndex index)
{
	if (overrideTextColors)
	{
		auto it = overrideTextColors->find(index);
		if (it != overrideTextColors->end())
			return it->second;
	}
	return colors[(int)index + 1];
}

void EditIDE11Draw::RemoveTextOverrideColor(ColorIndex index, bool redraw)
{
	if (overrideTextColors)
		overrideTextColors->erase(index);

	if (redraw && edit)
		edit->RedrawWindow(NULL, NULL,
		                   RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
}

void EditIDE11Draw::SetTextOverrideColor(ColorIndex index, COLORREF color, bool redraw)
{
	if (!overrideTextColors)
		overrideTextColors = std::make_shared<std::map<ColorIndex, COLORREF>>();

	(*overrideTextColors)[index] = color;

	if (redraw && edit)
		edit->RedrawWindow(NULL, NULL,
		                   RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
}

void EditIDE11Draw::Attach(CEdit* wnd, ThemeContext<CEdit>& context)
{
	__super::Attach(wnd, context);

	// mouse does not change state when
	// a LEAVE method is called while mouse
	// cursor is physically over control
	context.State.SetExtendedMouseTracking(true);

	m_ES_READONLY = context.State.GetNextAvailableFlag();

	if ((wnd->GetStyle() & ES_READONLY) == ES_READONLY)
		context.State.Add(m_ES_READONLY, wnd);

	if (g_IdeSettings)
	{
		edit = wnd;
		const GUID& cat = ThemeCategory11::NewProjectDialog;

		// normal state BG/text/border
		colors[(int)ColorIndex::Normal + 0] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[(int)ColorIndex::Normal + 1] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[(int)ColorIndex::Normal + 2] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBorder", FALSE);

		// mouse over state BG/text/border
		colors[(int)ColorIndex::MouseOver + 0] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[(int)ColorIndex::MouseOver + 1] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[(int)ColorIndex::MouseOver + 2] = g_IdeSettings->GetThemeColor(cat, L"TextBoxMouseOverBorder", FALSE);

		// active state BG/text/border
		colors[(int)ColorIndex::Active + 0] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", FALSE);
		colors[(int)ColorIndex::Active + 1] = g_IdeSettings->GetThemeColor(cat, L"TextBoxBackground", TRUE);
		colors[(int)ColorIndex::Active + 2] = g_IdeSettings->GetThemeColor(cat, L"InputFocusBorder", FALSE);

		// disabled state BG/text/border
		colors[(int)ColorIndex::Disabled + 0] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabled", FALSE);
		colors[(int)ColorIndex::Disabled + 1] = context.State.HasOption(TEXT("ActiveColorInDisabled"))
		                                            ? // CThemedEditACID
		                                            colors[(int)ColorIndex::Normal + 1]
		                                            : g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabled", TRUE);
		colors[(int)ColorIndex::Disabled + 2] = g_IdeSettings->GetThemeColor(cat, L"TextBoxDisabledBorder", FALSE);

		edit->ModifyStyle(0, WS_BORDER);
		edit->ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);
	}
}

void EditIDE11Draw::Detach(CEdit* wnd, ThemeContext<CEdit>& context)
{
	if (edit != nullptr)
	{
		edit->ModifyStyle(WS_BORDER, 0);
		edit->ModifyStyleEx(0, WS_EX_CLIENTEDGE, SWP_DRAWFRAME);
		edit = nullptr;
	}

	__super::Detach(wnd, context);
}

HBRUSH EditIDE11Draw::HandleCtlColor(CEdit* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CEdit>& context)
{
	if (edit == nullptr)
		return nullptr;

	if (nCtlColor == CTLCOLOR_EDIT || nCtlColor == CTLCOLOR_MSGBOX)
	{
		pDC->SetTextColor(GetTextColor(bg_index));
		pDC->SetBkColor(colors[(int)bg_index]);
		return BrushMap(context).GetHBRUSH(colors[(int)bg_index]);
	}
	else if (nCtlColor == CTLCOLOR_STATIC)
	{
		bg_index = ColorIndex::Disabled;
		pDC->SetTextColor(GetTextColor(bg_index));
		pDC->SetBkColor(colors[(int)bg_index]);
		return BrushMap(context).GetHBRUSH(colors[(int)bg_index]);
	}

	return nullptr;
}

void EditIDE11Draw::OnStateChanged(const ItemState& state, CWnd* wnd)
{
	if (edit == nullptr)
		return;

	if (state.IsInactive() || state.HasFlag(m_ES_READONLY))
		bg_index = ColorIndex::Disabled;
	else if (state.IsMouseOver() && !state.IsFocused())
		bg_index = ColorIndex::MouseOver;
	else if (state.IsMouseDown() || state.IsFocused())
		bg_index = ColorIndex::Active;
	else
		bg_index = ColorIndex::Normal;

	__super::OnStateChanged(state, wnd);
}

void EditIDE11Draw::PostPaintForeground(CDC& dc, CRect& rect, ThemeContext<CEdit>& context)
{
	if (edit != nullptr)
	{
		ThemeUtils::FillNonClientArea(edit, colors[(int)bg_index]);
		ThemeUtils::DrawNCBorder(edit, colors[(int)bg_index + 2]);
	}
}

void EditIDE11Draw::PostPaintBackground(CDC& dc, CRect& rect, ThemeContext<CEdit>& context)
{
	if (edit != nullptr)
	{
		ThemeUtils::FillNonClientArea(edit, colors[(int)bg_index]);
		ThemeUtils::DrawNCBorder(edit, colors[(int)bg_index + 2]);
	}
}

void EditIDE11Draw::WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CEdit* wnd, ThemeContext<CEdit>& context)
{
	if (edit)
	{
		if (message == WM_NCCALCSIZE)
		{
			context.SetResult(CenteredEditNCCalcSize(edit, wParam, lParam));
			context.CallDefault = false; // do not call Default message handler
		}
		else if (message == WM_NCHITTEST && !(edit->GetStyle() & ES_MULTILINE))
		{
			context.SetResult(CenteredEditNCHitTest(edit, wParam, lParam));
			context.CallDefault = false; // do not call Default message handler
		}
		else if (message == WM_NCLBUTTONDOWN && !(edit->GetStyle() & ES_MULTILINE) && CWnd::GetFocus() != edit)
			edit->SetFocus();
	}

	// skip default processing by not calling base class's method
	// __super::WindowProcPre(message, wParam, lParam, wnd, context);
}

void EditIDE11Draw::WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CEdit* wnd, ThemeContext<CEdit>& context)
{
	__super::WindowProcPost(message, wParam, lParam, wnd, context);

	if (message == WM_SETTEXT)
		wnd->RedrawWindow(NULL, NULL,
		                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
	else if (message == EM_SETREADONLY)
	{
		if (wParam)
			context.State.Add(m_ES_READONLY, wnd);
		else
			context.State.Remove(m_ES_READONLY, wnd);
	}
}

UINT_PTR EditBrowseDraw::TimerID()
{
	static UINT_PTR id = ::RegisterTimer("EditBrowseDraw");
	return id;
}

void EditBrowseDraw::DoRedraw(bool force /*= false*/)
{
	if (edit && ::IsWindow(edit->GetSafeHwnd()) && (force || ::GetTickCount() - last_update_ticks >= 20))
	{
		edit->RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
		last_update_ticks = ::GetTickCount();
	}
}

void EditBrowseDraw::Attach(CMFCEditBrowseCtrl* wnd, ThemeContext<CMFCEditBrowseCtrl>& context)
{
	__super::Attach(wnd, context);

	// mouse does not change state when
	// a LEAVE method is called while mouse
	// cursor is physically over control
	context.State.SetExtendedMouseTracking(true);

	m_ES_READONLY = context.State.GetNextAvailableFlag();

	if ((wnd->GetStyle() & ES_READONLY) == ES_READONLY)
		context.State.Add(m_ES_READONLY, wnd);

	edit = wnd;

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		// normal state BG/text/border
		colors[0].Init(GetColorExt({L"BG.Unfocused", L"BG.NPD.TextBoxBackground"}));
		colors[1].Init(GetColorExt({L"FG.Unfocused", L"FG.NPD.TextBoxBackground"}));
		colors[2].Init(GetColorExt({L"BG.UnfocusedBorder", L"BG.NPD.TextBoxBorder"}));

		// mouse over state BG/text/border
		colors[3].Init(GetColorExt({L"BG.MouseOverBackground", L"BG.NPD.TextBoxBackground"}));
		colors[4].Init(GetColorExt({L"FG.MouseOverBackground", L"FG.NPD.TextBoxBackground"}));
		colors[5].Init(GetColorExt({L"BG.MouseOverBorder", L"BG.NPD.TextBoxMouseOverBorder"}));

		// active state BG/text/border
		colors[6].Init(GetColorExt({L"BG.FocusedBackground", L"BG.NPD.TextBoxBackground"}));
		colors[7].Init(GetColorExt({L"FG.FocusedBackground", L"FG.NPD.TextBoxBackground"}));
		colors[8].Init(GetColorExt({L"BG.FocusedBorder", L"BG.NPD.TextBoxBorder"}));

		// disabled state BG/text/border
		colors[9].Init(GetColorExt({L"BG.Disabled", L"BG.NPD.TextBoxDisabled"}));
		colors[10].Init(context.State.HasOption(TEXT("ActiveColorInDisabled"))
		                    ? // CThemedEditACID
		                    colors[1].Color()
		                    : GetColorExt({L"FG.Disabled", L"FG.NPD.TextBoxDisabled"}));
		colors[11].Init(GetColorExt({L"BG.DisabledBorder", L"BG.NPD.TextBoxDisabledBorder"}));

		btn_colors[0] = colors[0];
		btn_colors[1] = colors[2];
		btn_colors[2].Init(GetColorExt({L"BG.ActionButtonMouseDown"}));
		btn_colors[3].Init(GetColorExt({L"BG.ActionButtonMouseOver"}));
	}
	else if (CVS2010Colours::IsVS2010ColouringActive())
	{
		// normal state BG/text/border
		colors[0].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOW));
		colors[1].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOWTEXT));
		colors[2].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_BORDER));

		// mouse over state BG/text/border
		colors[3].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOW));
		colors[4].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOWTEXT));
		colors[5].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BORDER));

		// active state BG/text/border
		colors[6].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOW));
		colors[7].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOWTEXT));
		colors[8].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEDOWN_BORDER));

		// disabled state BG/text/border
		colors[9].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_WINDOW));
		colors[10].Init(context.State.HasOption(TEXT("ActiveColorInDisabled"))
		                    ? // CThemedEditACID
		                    colors[1].Color()
		                    : CVS2010Colours::GetVS2010Colour(VSCOLOR_GRAYTEXT));
		colors[11].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_DISABLED_BORDER));

		btn_colors[0].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_BACKGROUND));
		btn_colors[1].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_BORDER));
		btn_colors[2].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEDOWN_BACKGROUND));
		btn_colors[3].Init(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_BEGIN),
		                   CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE1),
		                   CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_MIDDLE2),
		                   CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BACKGROUND_END));
	}
	else
	{
		ThemeUtils::CXTheme theme;

		auto getSysColor = [&theme](int index) {
			if (theme.IsAppThemed())
				return theme.GetSysColor(index);
			else
				return ::GetSysColor(index);
		};

		// normal state BG/text/border
		colors[0].Init(getSysColor(COLOR_WINDOW));
		colors[1].Init(getSysColor(COLOR_WINDOWTEXT));
		colors[2].Init(getSysColor(COLOR_ACTIVEBORDER));

		// mouse over state BG/text/border
		colors[3].Init(getSysColor(COLOR_WINDOW));
		colors[4].Init(getSysColor(COLOR_WINDOWTEXT));
		colors[5].Init(getSysColor(COLOR_ACTIVEBORDER));

		// active state BG/text/border
		colors[6].Init(getSysColor(COLOR_WINDOW));
		colors[7].Init(getSysColor(COLOR_WINDOWTEXT));
		colors[8].Init(getSysColor(COLOR_ACTIVEBORDER));

		// disabled state BG/text/border
		colors[9].Init(getSysColor(COLOR_WINDOW));
		colors[10].Init(getSysColor(COLOR_GRAYTEXT));
		colors[11].Init(getSysColor(COLOR_INACTIVEBORDER));

		btn_colors[0].Init(getSysColor(COLOR_WINDOW));
		btn_colors[1].Init(getSysColor(COLOR_ACTIVEBORDER));
		btn_colors[2].Init(getSysColor(COLOR_BTNSHADOW));
		btn_colors[3].Init(getSysColor(COLOR_BTNFACE));
	}
}

void EditBrowseDraw::Detach(CMFCEditBrowseCtrl* wnd, ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (edit != nullptr)
	{
		edit->ModifyStyle(WS_BORDER, 0);
		edit->ModifyStyleEx(0, WS_EX_CLIENTEDGE, SWP_DRAWFRAME);
		edit = nullptr;
	}

	__super::Detach(wnd, context);
}

HBRUSH EditBrowseDraw::HandleCtlColor(CMFCEditBrowseCtrl* wnd, CDC* pDC, UINT nCtlColor,
                                      ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (edit == nullptr)
		return nullptr;

	if (nCtlColor == CTLCOLOR_EDIT || nCtlColor == CTLCOLOR_MSGBOX)
	{
		ThemeUtils::DrawNCBorder(edit, colors[bg_index + 2].Color());
		pDC->SetTextColor(colors[bg_index + 1].Color());
		pDC->SetBkColor(colors[bg_index].Color());
		return BrushMap(context).GetHBRUSH(colors[bg_index].Color());
	}
	else if (nCtlColor == CTLCOLOR_STATIC)
	{
		bg_index = 9;
		ThemeUtils::DrawNCBorder(edit, colors[bg_index + 2].Color());
		pDC->SetTextColor(colors[bg_index + 1].Color());
		pDC->SetBkColor(colors[bg_index].Color());
		return BrushMap(context).GetHBRUSH(colors[bg_index].Color());
	}

	return nullptr;
}

void EditBrowseDraw::OnStateChanged(const ItemState& state, CWnd* wnd)
{
	if (edit == nullptr)
		return;

	// 	WTString wstr;
	// 	state.ToString(wstr);
	// 	ThemeUtils::TraceFramePrint(wstr);

	if (state.IsInactive() || state.HasFlag(m_ES_READONLY))
		bg_index = 9;
	else if (state.IsMouseOver() && !state.IsFocused())
		bg_index = 3;
	else if (state.IsMouseDownClient() || state.IsFocused())
		bg_index = 6;
	else
		bg_index = 0;

	btn_colors[0] = colors[bg_index];
	btn_colors[1] = colors[bg_index + 2];

	// in VS 2010 border of control does not change,
	// changes only button, that is why btn_colors
	// remain set.
	if (gShellAttr->IsDevenv10())
		bg_index = 0;

	DoRedraw(true);
}

void EditBrowseDraw::PostPaintForeground(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (edit)
	{
		CRect wnd_rect;
		edit->GetWindowRect(&wnd_rect);
		wnd_rect.OffsetRect(-wnd_rect.left, -wnd_rect.top);

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(edit);

		CWindowDC wdc(edit);
		ThemeUtils::CMemDC memDc(&wdc, false);
		CRgn nc_rgn;
		ThemeUtils::GetNonClientRegion(edit, nc_rgn, true);
		CBrush br(colors[bg_index].Color());
		memDc.FillRgn(&nc_rgn, &br);

		CThemedEditBrowse* teb = (CThemedEditBrowse*)edit;
		if (teb)
			teb->DrawButton(&memDc, btn_colors);

		ThemeUtils::FrameRectDPI(memDc, wnd_rect, colors[bg_index + 2].Color());
	}
}

void EditBrowseDraw::PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (edit != nullptr)
	{
		CRect wnd_rect;
		edit->GetWindowRect(&wnd_rect);
		wnd_rect.OffsetRect(-wnd_rect.left, -wnd_rect.top);

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(edit);

		CWindowDC wdc(edit);
		ThemeUtils::CMemDC memDc(&wdc, false);
		CRgn nc_rgn;
		ThemeUtils::GetNonClientRegion(edit, nc_rgn, false);
		CBrush br(colors[bg_index].Color());
		memDc.FillRgn(&nc_rgn, &br);

		CThemedEditBrowse* teb = (CThemedEditBrowse*)edit;
		if (teb)
			teb->DrawButton(&memDc, btn_colors);

		ThemeUtils::FrameRectDPI(memDc, wnd_rect, colors[bg_index + 2].Color());
	}
}

void EditBrowseDraw::PostPaintBackground(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (edit)
	{
		CRect wnd_rect;
		edit->GetWindowRect(&wnd_rect);
		wnd_rect.OffsetRect(-wnd_rect.left, -wnd_rect.top);

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(edit);

		CWindowDC wdc(edit);
		ThemeUtils::CMemDC memDc(&wdc, false);
		context.CallWndProc(WM_ERASEBKGND, (WPARAM)(HDC)wdc, 0);
		CRgn nc_rgn;
		ThemeUtils::GetNonClientRegion(edit, nc_rgn, false);
		CBrush br(colors[bg_index].Color());
		memDc.FillRgn(&nc_rgn, &br);

		CThemedEditBrowse* teb = (CThemedEditBrowse*)edit;
		if (teb)
			teb->DrawButton(&memDc, btn_colors);

		ThemeUtils::FrameRectDPI(memDc, wnd_rect, colors[bg_index + 2].Color());
	}
}

void EditBrowseDraw::WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CMFCEditBrowseCtrl* wnd,
                                   ThemeContext<CMFCEditBrowseCtrl>& context)
{
	if (message == WM_NCPAINT)
	{
		context.CallDefault = false;
		return;
	}

	if (edit)
	{
		if (message == WM_NCCALCSIZE)
		{
			CRect btn_rect;
			CThemedEditBrowse* teb = (CThemedEditBrowse*)edit;

			if (teb)
			{
				teb->GetButtonRect(btn_rect);
				btn_rect.SetRect(0, 0, -btn_rect.Width(), 0);
			}

			context.SetResult(CenteredEditNCCalcSize(edit, wParam, lParam, &btn_rect));

			context.CallDefault = false; // do not call Default message handler
		}
		else if (message == WM_NCHITTEST && !(edit->GetStyle() & ES_MULTILINE))
		{
			context.SetResult(CenteredEditNCHitTest(edit, wParam, lParam));
			context.CallDefault = false; // do not call Default message handler
		}
		else if (message == WM_NCLBUTTONDOWN && !(edit->GetStyle() & ES_MULTILINE) && CWnd::GetFocus() != edit)
			edit->SetFocus();
	}

	// skip default processing by not calling base class's method
	// __super::WindowProcPre(message, wParam, lParam, wnd, context);
}

void EditBrowseDraw::WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CMFCEditBrowseCtrl* wnd,
                                    ThemeContext<CMFCEditBrowseCtrl>& context)
{
	__super::WindowProcPost(message, wParam, lParam, wnd, context);

	if (message == WM_SETTEXT || (message >= WM_KEYFIRST && message <= WM_KEYLAST))
		DoRedraw(true);

	if (message == EM_SETREADONLY)
	{
		if (wParam)
			context.State.Add(m_ES_READONLY, wnd);
		else
			context.State.Remove(m_ES_READONLY, wnd);
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CThemedTree, CTreeCtrl)
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CThemedTree::OnNMCustomdraw)
ON_WM_NCPAINT()
ON_WM_PAINT()
ON_WM_KILLFOCUS()
ON_WM_SETFOCUS()
END_MESSAGE_MAP()
#pragma warning(pop)

void CThemedTree::OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!ThemeRendering)
	{
		*pResult = CDRF_DODEFAULT;
		return;
	}

	if (!m_in_paint && !m_erase_bg)
		return;

	m_erase_bg = false;

	TreeVS2010CustomDraw(*this, this, pNMHDR, pResult, background_gradient_cache);
	if (*pResult != CDRF_SKIPDEFAULT && *pResult != CDRF_NOTIFYITEMDRAW)
		*pResult = CDRF_SKIPDEFAULT;
}

LRESULT CThemedTree::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_VA_APPLYTHEME)
	{
		bool enable = wParam != 0;

		if (enable != ThemeRendering)
		{
			ThemeRendering = enable;

			if (ThemeRendering)
			{
				saved_style = GetStyle();
				saved_ex_style = GetExStyle();
				saved_img_list = GetImageList(TVSIL_NORMAL);

				if (g_IdeSettings)
				{
					m_focus = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);
					m_border = g_IdeSettings->GetEnvironmentColor(L"ControlOutline", FALSE);
					m_bg = g_IdeSettings->GetThemeTreeColor(L"Background", FALSE);
				}

				::VsScrollbarTheme(m_hWnd, TRUE);
				SetBkColor(m_bg);
				gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgTree, TVSIL_NORMAL);
				SendMessage(TVM_SETEXTENDEDSTYLE, TVS_EX_FADEINOUTEXPANDOS | TVS_EX_DOUBLEBUFFER,
				            TVS_EX_FADEINOUTEXPANDOS | TVS_EX_DOUBLEBUFFER);
				ModifyStyle(0, TVS_FULLROWSELECT);
				// ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);
			}
			else if (saved_img_list)
			{
				m_focus = m_border = GetSysColor(COLOR_ACTIVEBORDER);

				::VsScrollbarTheme(m_hWnd, FALSE);
				SetImageList(saved_img_list, TVSIL_NORMAL);
				::SetWindowLong(m_hWnd, GWL_STYLE, (LONG)saved_style);
				::SetWindowLong(m_hWnd, GWL_EXSTYLE, (LONG)saved_ex_style);
				ModifyStyleEx(0, 0, SWP_DRAWFRAME);
			}

			RedrawWindow(NULL, NULL,
			             RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
		}

		return TRUE;
	}

	if (ThemeRendering && message == WM_ERASEBKGND)
	{
		m_erase_bg = true;
		return TRUE;
	}

	return __super::WindowProc(message, wParam, lParam);
}

CThemedTree::CThemedTree()
    : CWndDpiAware<CTreeCtrl>(), saved_style(0), saved_ex_style(0), saved_img_list(nullptr), m_erase_bg(0),
      m_in_paint(false), m_is_focus(false), ThemeRendering(false)
{
	m_doublebuffer_support = wvWinXP == ::GetWinVersion() && gShellAttr->IsDevenv();
	m_focus = m_border = GetSysColor(COLOR_ACTIVEBORDER);
	m_bg = GetSysColor(COLOR_WINDOW);
}

void CThemedTree::OnNcPaint()
{
	if (ThemeRendering)
	{
		Default();
		ThemeUtils::DrawNCBorder(this, m_is_focus ? m_focus : m_border);
	}
	else
		Default();
}

void CThemedTree::OnPaint()
{
	if (ThemeRendering)
	{
		TmpPaintType tpt(::myGetProp(m_hWnd, "__VA_do_not_colour") ? PaintType::DontColor : PaintType::View);
		m_in_paint = true;
		Default();
		m_in_paint = false;
	}
	else
		Default();
}

void CThemedTree::OnKillFocus(CWnd* pNewWnd)
{
	CTreeCtrl::OnKillFocus(pNewWnd);

	m_is_focus = false;
	if (ThemeRendering)
		ThemeUtils::DrawNCBorder(this, m_border);
}

void CThemedTree::OnSetFocus(CWnd* pOldWnd)
{
	CTreeCtrl::OnSetFocus(pOldWnd);

	m_is_focus = true;
	if (ThemeRendering)
		ThemeUtils::DrawNCBorder(this, m_focus);
}

void IDefaultColors::ResetDefaultColors()
{
	if (g_IdeSettings)
	{
		crDefaultBG = g_IdeSettings->GetNewProjectDlgColor(L"BackgroundLowerRegion", FALSE);
		crDefaultText = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		crDefaultGrayText = g_IdeSettings->GetEnvironmentColor(L"GrayText", FALSE);
		brBefaultBG.reset(new CBrush(crDefaultBG));
	}
	else
	{
		crDefaultBG = ::GetSysColor(COLOR_BACKGROUND);
		crDefaultText = ::GetSysColor(COLOR_WINDOWTEXT);
		crDefaultGrayText = ::GetSysColor(COLOR_GRAYTEXT);
		brBefaultBG.reset(new CBrush(crDefaultBG));
	}
}

void CheckBehavior::OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                 ThemeContext<CButton>& context)
{

	switch (message)
	{
	case 0x00F8: // BM_SETDONTCLICK
		dont_click_on_focus = wParam != FALSE;
		break;

	case BM_CLICK:
		check_state = (check_state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
		wnd->SetCheck(check_state);

		if (!dont_click_on_focus)
		{
			// simulate click on this control
			CWnd* parent = wnd->GetParent();
			if (parent)
				parent->SendMessage(WM_COMMAND, MAKEWPARAM(wnd->GetDlgCtrlID(), BN_CLICKED),
				                    (LPARAM)wnd->GetSafeHwnd());
		}

		context.SetResult(TRUE); // eat message
		break;

	case WM_LBUTTONUP:
		check_state = (check_state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
		wnd->SetCheck(check_state);
		break;

	case WM_KEYUP:
		if (wParam == VK_SPACE)
		{
			check_state = (check_state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
			wnd->SetCheck(check_state);
		}
		break;
	}
}

void CheckBehavior::OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                  ThemeContext<CButton>& context)
{
	switch (message)
	{
	case BM_SETCHECK:
		check_state = (int)wParam;
		break;

	case BM_GETSTATE:
		context.Result = check_state;

		if (context.State.IsFocused())
			context.Result |= BST_FOCUS;

		if (context.State.IsMouseOver())
			context.Result |= BST_HOT;

		if (context.State.IsMouseDown())
			context.Result |= BST_PUSHED;

		break;

	case BM_GETCHECK:
		context.Result = check_state;
		break;
	}
}

void CheckBehavior::Attach(CButton* wnd, ThemeContext<CButton>& context)
{
	styles.InitFromWnd(wnd);
	__super::Attach(wnd, context);
	enabled = true;
	check_state = wnd->GetCheck();
}

void CheckBehavior::Detach(CButton* wnd, ThemeContext<CButton>& context)
{
	enabled = false;
	check_state = BST_UNCHECKED;
	__super::Detach(wnd, context);
}

void RadioBehavior::OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                 ThemeContext<CButton>& context)
{
	// DO NOT call previous handler
	// ::WindowProcPre(message, wParam, lParam, wnd, state);

	if (check_state != BST_CHECKED)
	{
		if (message == WM_LBUTTONUP)
		{
			check_state = BST_CHECKED;
			wnd->SetCheck(check_state);
		}
		else if (message == WM_SETFOCUS)
		{
			check_state = BST_CHECKED;
			wnd->SetCheck(check_state);

			if (!dont_click_on_focus)
			{
				// simulate click on this control
				CWnd* parent = wnd->GetParent();
				if (parent)
					parent->SendMessage(WM_COMMAND, MAKEWPARAM(wnd->GetDlgCtrlID(), BN_CLICKED),
					                    (LPARAM)wnd->GetSafeHwnd());
			}
		}

		// if this control is now checked
		if (check_state && (styles.OldStyle() & BS_AUTORADIOBUTTON) == BS_AUTORADIOBUTTON)
		{
			// following loop unchecks other radiobuttons in group

			CWnd* pWndParent = wnd->GetParent();
			if (pWndParent)
			{
				CWnd* pWnd = pWndParent->GetNextDlgGroupItem(wnd, TRUE);
				while (pWnd != NULL && pWnd != wnd)
				{
					if (IsAutoRadioButton(pWnd))
						pWnd->SendMessage(BM_SETCHECK, WPARAM(check_state ? 0 : 1));

					pWnd = pWndParent->GetNextDlgGroupItem(pWnd, TRUE);
				}
			}
		}
	}
}

void RadioBehavior::OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                  ThemeContext<CButton>& context)
{
	if (message == WM_GETDLGCODE)
	{
		context.Result = DLGC_BUTTON | DLGC_RADIOBUTTON;
		return;
	}
	else if (message == BM_SETCHECK)
	{
		check_state = (int)wParam;

		if (check_state)
			wnd->ModifyStyle(0, WS_TABSTOP);
		else
			wnd->ModifyStyle(WS_TABSTOP, 0);

		return;
	}

	__super::OnMessagePost(message, wParam, lParam, wnd, context);
}

bool RadioBehavior::IsAutoRadioButton(CWnd* wnd)
{
	return wnd != NULL && (wnd->SendMessage(WM_GETDLGCODE) & DLGC_RADIOBUTTON) != 0 &&
	       ((wnd->GetStyle() & BS_AUTORADIOBUTTON) || ::myGetProp(wnd->GetSafeHwnd(), "__VA_BS_AUTORADIOBUTTON"));
}

void ItemState::ToString(WTString& str) const
{
	CStringA num;
	CString__FormatA(num, "%x - ", m_value);
	str.append(num);

	if (IsFocused())
		str.append("Focused");

	if (IsMouseOverClient())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("MouseOverClient");
	}
	else if (IsMouseOverNonClient())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("MouseOverNonClient");
	}

	if (IsMouseDownClient())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("MouseDownClient");
	}
	else if (IsMouseDownNonClient())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("MouseDownNonClient");
	}

	if (IsInactive())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("Inactive");
	}

	if (IsPrefixVisible())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("PrefixVisible");
	}

	if (IsFocusVisible())
	{
		if (str.GetLength())
			str.append(" | ");

		str.append("FocusVisible");
	}
}

void CThemedEdit_PopulateContextMenu(CEdit& edit, CMenu& menu)
{
	DWORD DISABLED_ITEM = MF_GRAYED | MF_DISABLED;

	bool bReadOnly = !!(edit.GetStyle() & ES_READONLY);
	DWORD flags = edit.CanUndo() && !bReadOnly ? 0 : DISABLED_ITEM;
	menu.InsertMenu(0, MF_BYPOSITION | flags, EM_UNDO, _T("&Undo\tCtrl+Z"));
	menu.InsertMenu(1, MF_BYPOSITION | MF_SEPARATOR);

	DWORD sel = edit.GetSel();
	flags = LOWORD(sel) == HIWORD(sel) ? DISABLED_ITEM : 0;
	menu.InsertMenu(2, MF_BYPOSITION | flags, WM_COPY, _T("&Copy\tCtrl+C"));

	flags = (flags == DISABLED_ITEM || bReadOnly) ? DISABLED_ITEM : 0;
	menu.InsertMenu(2, MF_BYPOSITION | flags, WM_CUT, _T("Cu&t\tCtrl+X"));
	menu.InsertMenu(4, MF_BYPOSITION | flags, WM_CLEAR, _T("&Delete\tDel"));

	flags = IsClipboardFormatAvailable(CF_TEXT) && !bReadOnly ? 0 : DISABLED_ITEM;
	menu.InsertMenu(4, MF_BYPOSITION | flags, WM_PASTE, _T("&Paste\tCtrl+V"));
	menu.InsertMenu(6, MF_BYPOSITION | MF_SEPARATOR);

	int len = edit.GetWindowTextLength();
	flags = (!len || (LOWORD(sel) == 0 && HIWORD(sel) == len)) ? DISABLED_ITEM : 0;
	menu.InsertMenu(7, MF_BYPOSITION | flags, WM_VA_EDITSELECTALL, _T("Select &All\tCtrl+A"));
}

bool CThemedEdit_OnCommand(CEdit& edit, WPARAM wParam, LPARAM lParam, BOOL& result)
{
	if (LOWORD(wParam) == WM_VA_EDITSELECTALL)
	{
		result = (BOOL)edit.SendMessage(EM_SETSEL, 0, -1);
		return true;
	}

	switch (LOWORD(wParam))
	{
	case EM_UNDO:  // fall-through
	case WM_CUT:   // fall-through
	case WM_COPY:  // fall-through
	case WM_CLEAR: // fall-through
	case WM_PASTE: {
		result = (BOOL)edit.SendMessage(LOWORD(wParam));
		return true;
	}
	default:
		result = FALSE;
		return false;
		// return CEdit::OnCommand(wParam, lParam);
	}
}

BOOL CThemedEdit::OnCommand(WPARAM wParam, LPARAM lParam)
{
	BOOL rslt = 0;

	if (CThemedEdit_OnCommand(*this, wParam, lParam, rslt))
		return rslt;

	return __super::OnCommand(wParam, lParam);
}

void CThemedEdit::OnPopulateContextMenu(CMenu& contextMenu)
{
	CThemedEdit_PopulateContextMenu(*this, contextMenu);
}

LRESULT CThemedEdit::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!m_bNCAreaUpdated && message == WM_NCPAINT)
	{
		if ((GetStyle() & ES_MULTILINE) == ES_MULTILINE)
			// if multi-line, set it to false directly,
			// as we are sure, that centering is not applied
			m_bNCAreaUpdated = true;
		else
		{
			// force WM_NCCALCSIZE to be applied
			SetWindowPos(NULL, 0, 0, 0, 0,
			             SWP_ASYNCWINDOWPOS | SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE);
			return 0;
		}
	}
	else if (message == WM_NCCALCSIZE)
	{
		// renderer knows nothing about this field,
		// so it must be handled here anyway
		m_bNCAreaUpdated = true;
	}

	if ((Theme.IsEnabled && Theme.Renderer) || (GetStyle() & ES_MULTILINE) == ES_MULTILINE)
	{
		// in case of active theme or multi-line,
		// pass to default handler
		return __super::WindowProc(message, wParam, lParam);
	}
	else
	{
		// single-line non-themed

		if (message == WM_NCCALCSIZE)
			return ThemeUtils::CenteredEditNCCalcSize(this, wParam, lParam);
		else if (message == WM_NCHITTEST && !(GetStyle() & ES_MULTILINE))
			return ThemeUtils::CenteredEditNCHitTest(this, wParam, lParam);
		else if (message == WM_NCLBUTTONDOWN && !(GetStyle() & ES_MULTILINE) && GetFocus() != this)
			SetFocus();

		LRESULT rslt = __super::WindowProc(message, wParam, lParam);

		if (message == WM_NCPAINT || message == WM_PAINT || message == WM_ERASEBKGND)
		{
			bool editable = IsWindowEnabled() && (GetStyle() & ES_READONLY) == 0;

			ThemeUtils::FillNonClientArea(this, editable ? ::GetSysColor(COLOR_WINDOW) : ::GetSysColor(COLOR_BTNFACE));
		}

		return rslt;
	}
}

BOOL CThemedEditBrowse::OnCommand(WPARAM wParam, LPARAM lParam)
{
	BOOL rslt = 0;

	if (CThemedEdit_OnCommand(*this, wParam, lParam, rslt))
		return rslt;

	return __super::OnCommand(wParam, lParam);
}

void CThemedEditBrowse::OnPopulateContextMenu(CMenu& contextMenu)
{
	CThemedEdit_PopulateContextMenu(*this, contextMenu);
}

void ThemeDraw::CThemedEditBrowse::SetKeyEvent(BYTE virtual_key, KeyEvent func)
{
	if (func)
		m_keyEvents[virtual_key] = func;
	else
		m_keyEvents.erase(virtual_key);
}

ThemeDraw::CThemedEditBrowse::KeyEvent ThemeDraw::CThemedEditBrowse::GetKeyEvent(BYTE virtual_key)
{
	auto it = m_keyEvents.find(virtual_key);
	if (it != m_keyEvents.end())
		return it->second;
	return nullptr;
}

void CThemedEditBrowse::DrawButton(CDC* dc, ColorSet* colors)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (m_Mode == BrowseMode_None)
	{
		return;
	}

	if (m_sizeImage.cx == 0 && m_sizeImage.cy == 0 && m_drawGlyph)
	{
		m_sizeImage.cx = m_defaultButtonSize;
		m_sizeImage.cy = m_defaultButtonSize;
		m_nBrowseButtonWidth = m_defaultButtonSize;
	}

	CRect rectWindow;
	GetWindowRect(rectWindow);

	m_rectBtn = rectWindow;
	m_rectBtn.left = m_rectBtn.right - m_nBrowseButtonWidth;

	CRect rectClient;
	GetClientRect(rectClient);
	ClientToScreen(&rectClient);

	m_rectBtn.OffsetRect(rectClient.right + m_nBrowseButtonWidth - rectWindow.right, 0);
	m_rectBtn.top += rectClient.top - rectWindow.top;
	m_rectBtn.bottom -= rectWindow.bottom - rectClient.bottom;

	CRect rect = m_rectBtn;
	rect.OffsetRect(-rectWindow.left, -rectWindow.top);

	bool isHot = false;
	bool isDown = false;

	CPoint pt_mouse;
	::GetCursorPos(&pt_mouse);

	if (m_rectBtn.PtInRect(pt_mouse))
	{
		isHot = true;
		isDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
	}

	if (rect.Height() < rectWindow.Height() - VsUI::DpiHelper::LogicalToDeviceUnitsX(2))
	{
		int amount = rectWindow.Height() - rect.Height() - VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		int offset1 = amount / 2;
		int offset2 = amount - offset1;
		rect.left -= offset1;
		rect.right += offset2;
		rect.top -= offset1;
		rect.bottom += offset2;
	}

	int bgId = 0;
	if (isDown)
		bgId = 2;
	else if (isHot)
		bgId = 3;

	CRgn rgnClip;
	rgnClip.CreateRectRgnIndirect(&rect);

	dc->SelectClipRgn(&rgnClip);

	m_btnBkColorSet = &colors[bgId];
	m_btnBorder = colors[1].Color();
	OnDrawBrowseButton(dc, rect, isDown ? TRUE : FALSE, isHot ? TRUE : FALSE);
	m_btnBkColorSet = nullptr;
	m_btnBorder = 0;

	if (!m_drawGlyph)
	{
		if (gShellAttr->IsDevenv10() && isHot)
		{
			ThemeUtils::FrameRectDPI(*dc, rect, colors[1].Color());
		}
		else if (m_bDrawButtonBorder)
		{
			CPen pen(0, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), colors[1].Color());
			ThemeUtils::AutoSelectGDIObj penDC(*dc, pen);
			dc->MoveTo(rect.left, rect.top);
			dc->LineTo(rect.left, rect.bottom);
		}
	}

	dc->SelectClipRgn(NULL);

	ScreenToClient(&m_rectBtn);
}

void CThemedEditBrowse::OnChangeLayout()
{
	ASSERT_VALID(this);
	ENSURE(GetSafeHwnd() != NULL);

	m_nBrowseButtonWidth = __max(m_defaultButtonSize, m_sizeImage.cx + 8);

	SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE);

	if (m_Mode != BrowseMode_None)
	{
		GetWindowRect(m_rectBtn);
		m_rectBtn.left = m_rectBtn.right - m_nBrowseButtonWidth;

		ScreenToClient(&m_rectBtn);
	}
	else
	{
		m_rectBtn.SetRectEmpty();
	}
}

LRESULT CThemedEditBrowse::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (m_defaultButtonSize == 0)
	{
		CRect rect;
		GetClientRect(&rect);
		m_defaultButtonSize = rect.Height();
		OnChangeLayout();
	}

	if (message == WM_CHAR)
	{
		KeyEvent kev = GetKeyEvent((BYTE)wParam);
		if (kev)
		{
			kev();
			return 0;
		}
	}

	return __super::WindowProc(message, wParam, lParam);
}

void ThemeDraw::CThemedEditBrowse::OnDpiChanged(DpiChange change, bool& handled)
{
	if (change == CDpiAware::DpiChange::BeforeParent)
	{
		double scale = GetDpiChangeScaleFactor();
		m_defaultButtonSize = WindowScaler::Scale(m_defaultButtonSize, scale);
		m_nBrowseButtonWidth = WindowScaler::Scale(m_nBrowseButtonWidth, scale);
		WindowScaler::ScaleSize(m_sizeImage, scale);
	}
}

ThemeDraw::CThemedEditBrowse::CThemedEditBrowse()
{
	m_nBrowseButtonWidth = m_defaultButtonSize;
	SetKeyEvent(VK_ESCAPE, [this] {
		SetSel(0, -1);
		Clear();
	});                            // clear on ESC key
	SetKeyEvent(VK_RETURN, [] {}); // just prevent beep on ENTER
}

void CThemedEditBrowse::SetDrawButtonBorder(bool draw)
{
	m_bDrawButtonBorder = draw;
	RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
}

void ThemeDraw::CThemedEditBrowse::SetSearchDrawGlyphFnc()
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	SetDrawGlyphFnc([this](DrawGlyphContext& c) {
		CRect wndRect;
		GetWindowRect(&wndRect);
		CPoint mousePt;
		::GetCursorPos(&mousePt);

		bool has_focus = ::GetFocus() == m_hWnd;
		bool disabled = ::IsWindowEnabled(m_hWnd) == FALSE;
		bool mouse_over = wndRect.PtInRect(mousePt) != FALSE;

		CRect rect = c.rect;
		int size = min(rect.Width(), rect.Height());
		rect.left = rect.CenterPoint().x - size / 2;
		rect.top = rect.CenterPoint().y - size / 2;
		rect.right = rect.left + size;
		rect.bottom = rect.top + size;

		COLORREF glyph = GetSysColor(COLOR_GRAYTEXT);

		if (CVS2010Colours::IsExtendedThemeActive())
		{
			if (disabled)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonDisabledGlyph", FALSE);
			else if (c.pressed)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonMouseDownGlyph", FALSE);
			else if (c.hot)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonMouseOverGlyph", FALSE);
			else
			{
				if (gShellAttr->IsDevenv12OrHigher())
				{
					if (has_focus)
						glyph =
						    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"FocusedSearchGlyph", FALSE);
					else if (mouse_over)
						glyph = g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"MouseOverSearchGlyph",
						                                     FALSE);
					else
						glyph = g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"SearchGlyph", FALSE);
				}
				else
				{
					glyph = g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"SearchGlyph", FALSE);
				}
			}
		}
		else if (CVS2010Colours::IsVS2010ColouringActive())
		{
			rect.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsX(1));

			glyph = CVS2010Colours::GetVS2010Colour(VSCOLOR_GRAYTEXT);
		}

		if ((c.hot || c.pressed) && CVS2010Colours::IsExtendedThemeActive())
			c.bg_colors->DrawVertical(*c.dc, rect);

		/*static*/ ThemeUtils::Canvas searchGlyph(160, 160);
		searchGlyph.UsePenMinWidth(0, 1.5f).Ellipse(68, 28, 132, 92).UsePenMinWidth(1, 2.0f).Line(80, 80, 30, 130);

		rect.DeflateRect(rect.Width() / 10, rect.Height() / 10);

		searchGlyph.SetPen(0, 20, glyph).SetPen(1, 25, glyph).Paint(*c.dc, rect);
	});
}

void ThemeDraw::CThemedEditBrowse::SetClearDrawGlyphFnc()
{
	SetDrawGlyphFnc([this](DrawGlyphContext& c) {
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

		CRect wndRect;
		GetWindowRect(&wndRect);
		CPoint mousePt;
		::GetCursorPos(&mousePt);

		bool has_focus = ::GetFocus() == m_hWnd;
		bool disabled = ::IsWindowEnabled(m_hWnd) == FALSE;
		bool mouse_over = wndRect.PtInRect(mousePt) != FALSE;

		CRect rect = c.rect;
		int size = min(rect.Width(), rect.Height());
		rect.left = rect.CenterPoint().x - size / 2;
		rect.top = rect.CenterPoint().y - size / 2;
		rect.right = rect.left + size;
		rect.bottom = rect.top + size;

		COLORREF glyph = c.pressed ? GetSysColor(COLOR_WINDOW) : GetSysColor(COLOR_GRAYTEXT);

		if (CVS2010Colours::IsExtendedThemeActive())
		{
			if (disabled)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonDisabledGlyph", FALSE);
			else if (c.pressed)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonMouseDownGlyph", FALSE);
			else if (c.hot)
				glyph =
				    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ActionButtonMouseOverGlyph", FALSE);
			else
			{
				if (gShellAttr->IsDevenv12OrHigher())
				{
					if (has_focus)
						glyph =
						    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"FocusedClearGlyph", FALSE);
					else if (mouse_over)
						glyph =
						    g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"MouseOverClearGlyph", FALSE);
					else
						glyph = g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ClearGlyph", FALSE);
				}
				else
				{
					glyph = g_IdeSettings->GetThemeColor(ThemeCategory11::SearchControl, L"ClearGlyph", FALSE);
				}
			}
		}
		else if (CVS2010Colours::IsVS2010ColouringActive())
		{
			rect.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));

			glyph = CVS2010Colours::GetVS2010Colour(VSCOLOR_GRAYTEXT);
		}

		if (c.hot || c.pressed)
		{
			c.bg_colors->DrawVertical(*c.dc, rect);

			if (gShellAttr->IsDevenv10())
				ThemeUtils::FrameRectDPI(*c.dc, rect, c.border);
		}

		/*static*/ ThemeUtils::Canvas clearGlyph(140, 140);
		clearGlyph.UsePenFixed(0).Line(40, 40, 100, 100).Line(40, 100, 100, 40);

		if (rect.Height() < 14)
		{
			rect.InflateRect((14 - rect.Height()) / 2, (14 - rect.Width()) / 2);
		}
		else if (rect.Height() > 24)
		{
			rect.DeflateRect((rect.Height() - 24) / 2, (rect.Width() - 24) / 2);
		}

		clearGlyph.SetPen(0, 1.6f, glyph).Paint(*c.dc, rect);
	});
}

void ThemeDraw::CThemedEditBrowse::OnDrawBrowseButton(CDC* pDC, CRect rect, BOOL bIsButtonPressed, BOOL bHighlight)
{
	ASSERT(m_Mode != BrowseMode_None);
	ASSERT_VALID(pDC);

	int iImage = 0;

	if (m_drawGlyph)
	{
		if (m_btnBkColorSet == nullptr)
		{
			// not our call - go away
			return;
		}

		if (m_bDefaultImage)
		{
			switch (m_Mode)
			{
			case BrowseMode_Folder:
				iImage = 0;
				break;

			case BrowseMode_File:
				iImage = 1;
				break;

			default:
				break;
			}
		}

		DrawGlyphContext ctx = {
		    this,                                 // 	CThemedEditBrowse * wnd;
		    pDC,                                  // 	CDC * dc;
		    m_btnBkColorSet,                      // 	ThemeUtils::ColorSet * bg_colors;
		    m_btnBorder,                          // 	COLORREF border;
		    rect,                                 // 	CRect rect;
		    &m_ImageBrowse,                       // 	CImageList * img_list;
		    iImage,                               // 	int img_id;
		    m_sizeImage,                          // 	CSize img_size;
		    m_bIsButtonPressed ? true : false,    // 	bool pressed;
		    m_bIsButtonHighlighted ? true : false // 	bool hot;
		};

		m_drawGlyph(ctx);

		return;
	}
	else if (m_btnBkColorSet)
	{
		if (gShellAttr->IsDevenv10())
			rect.DeflateRect(1, 1);

		if (m_btnBkColorSet->ColorCount())
			m_btnBkColorSet->DrawVertical(*pDC, rect);

		CBrush br(m_btnBorder);
		pDC->FrameRect(rect, &br);
	}

	if (m_ImageBrowse.GetSafeHandle() != NULL)
	{
		if (m_bDefaultImage)
		{
			switch (m_Mode)
			{
			case BrowseMode_Folder:
				iImage = 0;
				break;

			case BrowseMode_File:
				iImage = 1;
				break;

			default:
				break;
			}
		}

		CPoint ptImage;
		ptImage.x = rect.CenterPoint().x - m_sizeImage.cx / 2;
		ptImage.y = rect.CenterPoint().y - m_sizeImage.cy / 2;

		if (bIsButtonPressed)
		{
			ptImage.x++;
			ptImage.y++;
		}

		m_ImageBrowse.Draw(pDC, iImage, ptImage, ILD_NORMAL);
	}
}
BOOL CThemedDimEdit::OnCommand(WPARAM wParam, LPARAM lParam)
{
	BOOL rslt = 0;

	if (CThemedEdit_OnCommand(*this, wParam, lParam, rslt))
		return rslt;

	return __super::OnCommand(wParam, lParam);
}

void CThemedDimEdit::OnPopulateContextMenu(CMenu& contextMenu)
{
	CThemedEdit_PopulateContextMenu(*this, contextMenu);
}

void CThemedComboBox::OnDropdown()
{
	CWaitCursor curs;
	if (!IsWindow())
		Init();

	SetItemCount((int)Items->Count());

	CRect rc;
	GetWindowRect(&rc);
	SetDroppedWidth(rc.Width());
	ShowDropDown(FALSE);
	DisplayList(TRUE);

	// 	SetItemState(sFileData.GetSuggestionIdx(), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	// 	EnsureVisible(sFileData.GetSuggestionIdx(), FALSE);
}

void CThemedComboBox::OnEditChange()
{
	if (!IsWindow())
		return;

	GetText(Text);

	WndEventArgsT args;
	args.sender = this;
	TextChanged.invoke(args);
}

void CThemedComboBox::GetDefaultTitleAndIconIdx(CString& title, int& iconIdx) const
{
	title = "";
	iconIdx = -1;
}

void CThemedComboBox::OnSelect()
{
	POSITION pos = GetFirstSelectedItemPosition();
	if (pos)
	{
		SelectedItems.clear();
		int nItem = -1;
		while ((nItem = GetNextSelectedItem(pos)) >= 0)
			SelectedItems.insert(nItem);

		if (SelectedItems.size() == 1)
			SetText(Items->At((size_t)*SelectedItems.begin()).Text);
		else
			SetText(CStringW());

		WndEventArgsT args;
		args.sender = this;
		SelectionChanged.invoke(args);
	}
}

WTString CThemedComboBox::GetItemTip(int nRow) const
{
	_ASSERTE(nRow >= 0 && nRow < (int)Items->Count());

#ifndef _UNICODE
	const ItemData& item = Items->At((uint)nRow);
	return WTString(item.Tip);
#else
	return GetItemTipW(nRow);
#endif
}

CStringW CThemedComboBox::GetItemTipW(int nRow) const
{
	if (nRow < 0 || nRow >= (int)Items->Count())
		return CStringW();

	const ItemData& item = Items->At((uint)nRow);
	return item.Tip;
}

void CThemedComboBox::InsertItemW(int index, const CStringW& text, const CStringW& tip /*= CStringW()*/,
                                  int image /*= -1*/)
{
	Items->Insert((uint)index, ItemData(text, tip, image));
}

bool CThemedComboBox::RemoveItemAt(int index)
{
	return Items->Remove((uint)index);
}

int CThemedComboBox::FindItemW(const CStringW& text, bool case_sensitive /*= true*/) const
{
	return Items->Find(ItemData(text), case_sensitive ? cmpText : cmpTextNoCase);
}

void CThemedComboBox::AddItemW(const CStringW& text, const CStringW& tip /*= CStringW()*/, int image /*= -1*/)
{
	Items->Add(ItemData(text, tip, image));
}

void CThemedComboBox::AddItemA(const CStringA& text, const CStringA& tip /*= CStringA()*/, int image /*= -1*/)
{
	Items->Add(ItemData(text, tip, image));
}

struct ItemDataPred
{
	typedef CThemedComboBox::ItemData ItemData;
	typedef CThemedComboBox::CompareOpts CompareOpts;
	typedef std::function<bool(const ItemData&, const ItemData&, CompareOpts)> CompareFunc;

	const ItemData& m_data;
	const CompareOpts m_opts;
	const CompareFunc m_cmpFnc;

	ItemDataPred(const ItemData& idata, const CompareFunc& func, DWORD opts)
	    : m_data(idata), m_opts((CompareOpts)opts), m_cmpFnc(func)
	{
	}

	bool operator()(const CThemedComboBox::ItemData& other) const
	{
		if (m_cmpFnc)
			return m_cmpFnc(m_data, other, m_opts);
		else
			return m_data.Compare(other, m_opts) == 0;
	};
};

int CThemedComboBox::IItemSource::Find(const ItemData& item, CompareOpts opts /*= ItemData::cmpText*/) const
{
	if (UserCompareFnc)
	{
		for (size_t i = 0; i < Count(); i++)
		{
			if (UserCompareFnc(item, At(i), opts) == 0)
				return (int)i;
		}

		return -1;
	}

	for (size_t i = 0; i < Count(); i++)
	{
		if (item.Compare(At(i), opts) == 0)
			return (int)i;
	}

	return -1;
}

class STDVecItemSource : public CThemedComboBox::IItemSource
{
	std::vector<CThemedComboBox::ItemData> m_data;

  public:
	STDVecItemSource(CThemedComboBox& parent) : CThemedComboBox::IItemSource(parent)
	{
	}

	~STDVecItemSource()
	{
	}

	virtual void Clear()
	{
		m_data.clear();
		OnCountChanged();
	}

	virtual size_t Count() const
	{
		return m_data.size();
	}

	virtual bool Insert(size_t index, const CThemedComboBox::ItemData& item)
	{
		m_data.insert(m_data.begin() + (int)index, item);
		OnCountChanged();
		return true;
	}

	virtual bool Add(const CThemedComboBox::ItemData& item)
	{
		m_data.push_back(item);
		OnCountChanged();
		return true;
	}

	virtual bool Remove(size_t index)
	{
		m_data.erase(m_data.begin() + (int)index);
		OnCountChanged();
		return true;
	}

	virtual CThemedComboBox::ItemData& At(size_t index)
	{
		return m_data.at(index);
	}

	virtual const CThemedComboBox::ItemData& At(size_t index) const
	{
		return m_data.at(index);
	}

	virtual int Find(const CThemedComboBox::ItemData& item,
	                 CThemedComboBox::CompareOpts opts /* = ItemData::cmpText*/) const
	{
		ItemDataPred pred(item, UserCompareFnc, opts);

		std::vector<CThemedComboBox::ItemData>::const_iterator it = std::find_if(m_data.begin(), m_data.end(), pred);

		if (it == m_data.end())
			return -1;

		return (int)std::distance(m_data.begin(), it);
	}
};

CThemedComboBox::CThemedComboBox(bool has_colorable_content /*= false*/)
    : Items(new STDVecItemSource(*this)), ThemingActive(false)
{
	SetHasColorableContent(has_colorable_content);
}

void CThemedComboBox::OnCloseup()
{
	if (GetItemsCount() == 0)
		GetEditCtrl()->SetSel(0, -1);
}

void CThemedComboBox::Init()
{
	CGenericComboBox::Init();
}

int CThemedComboBox::GetItemsCount() const
{
	return (int)Items->Count();
}

bool CThemedComboBox::OnEditWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	CWnd& sub = EditSubclasser();

	WndProcEventArgs<CThemedComboBox> event_args;

	event_args.wnd = &sub;
	event_args.sender = this;
	event_args.msg = message;
	event_args.wParam = wParam;
	event_args.lParam = lParam;
	event_args.result = 0;
	event_args.handled = false;

	EditWindowProc.invoke(event_args);

	if (event_args.handled)
	{
		result = event_args.result;
		return true;
	}

	return __super::OnEditWindowProc(message, wParam, lParam, result);
}

bool CThemedComboBox::OnListWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	CWnd& sub = ListSubclasser();
	WndProcEventArgs<CThemedComboBox> event_args;

	event_args.wnd = &sub;
	event_args.sender = this;
	event_args.msg = message;
	event_args.wParam = wParam;
	event_args.lParam = lParam;
	event_args.result = 0;
	event_args.handled = false;

	ListWindowProc.invoke(event_args);

	if (event_args.handled)
	{
		result = event_args.result;
		return true;
	}

	return __super::OnListWindowProc(message, wParam, lParam, result);
}

bool CThemedComboBox::OnComboWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	CWnd& sub = ComboSubclasser();

	WndProcEventArgs<CThemedComboBox> event_args;

	event_args.wnd = &sub;
	event_args.sender = this;
	event_args.msg = message;
	event_args.wParam = wParam;
	event_args.lParam = lParam;
	event_args.result = 0;
	event_args.handled = false;

	ComboWindowProc.invoke(event_args);

	if (event_args.handled)
	{
		result = event_args.result;
		return true;
	}

	return __super::OnComboWindowProc(message, wParam, lParam, result);
}

struct CtrlBackspaceEditHandler : public CThemedComboBox::WndProcEvent
{
	bool is_first = true;

	virtual bool invoke(CThemedComboBox::WndProcEventArgsT& args)
	{
		if (is_first)
		{
			is_first = false;
			::SHAutoComplete(args.wnd->GetSafeHwnd(), SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF);
		}
		return false;
	}
};

void CThemedComboBox::AddCtrlBackspaceEditHandler()
{
	EditWindowProc.add(WndProcEvent::Ptr(new CtrlBackspaceEditHandler()));
}

bool CThemedComboBox::IsVS2010ColouringActive() const
{
	return ThemingActive; // CVS2010Colours::IsExtendedThemeActive();
}

void CThemedComboBox::SubclassDlgItemAndInit(HWND dlg, INT id)
{
	SubclassWindowW(::GetDlgItem(dlg, id));
	Init();
}

bool CThemedComboBox::GetText(CStringW& text) const
{
	return CWndTextHelp::GetText(GetEditCtrl(), text);
}

bool CThemedComboBox::GetText(CStringA& text) const
{
	CStringW wide;
	if (GetText(wide))
	{
		text = ::WideToMbcs(wide, wide.GetLength()).c_str();
		return true;
	}
	return false;
}
bool CThemedComboBox::SetText(const CStringW& text)
{
	return CWndTextHelp::SetText(GetEditCtrl(), text);
}

bool CThemedComboBox::SetText(const CStringA& text)
{
	return SetText(::MbcsToWide(text, text.GetLength()));
}

bool CThemedComboBox::GetItemInfo(int nRow, CStringW* text, CStringW* tip, int* image, UINT* state)
{
	if (nRow >= 0 && Items->Count() > (size_t)nRow)
	{
		ItemData& data = Items->At((size_t)nRow);

		if (text)
			*text = data.Text;

		if (tip)
			*tip = data.Tip;

		if (image)
			*image = data.Image;

		if (state)
			*state = SelectedItems.find(nRow) != SelectedItems.end() ? (LVIS_SELECTED | LVIS_FOCUSED) : 0u;

		return true;
	}
	return false;
}

LRESULT CThemedComboBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_VA_APPLYTHEME)
	{
		ThemingActive = wParam != 0;
		m_comboSub.SetVS2010ColouringActive(ThemingActive);
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
		return TRUE;
	}

	return __super::WindowProc(message, wParam, lParam);
}

void CThemedComboBox::SelectItem(int index, bool append /*= false*/)
{
	if (!append)
		SelectedItems.clear();

	SelectedItems.insert(index);

	if (SelectedItems.size() == 1)
		SetText(Items->At((size_t)*SelectedItems.begin()).Text);
	else
		SetText(CStringW());

	WndEventArgsT args;
	args.sender = this;
	SelectionChanged.invoke(args);
}
void GroupIDE11Draw::Attach(CButton* wnd, ThemeContext<CButton>& context)
{
	__super::Attach(wnd, context); // [case:149451] added (fixed) in this case but this needs to be here for every use of CThemedGroup
	
	if (g_IdeSettings)
	{
		button = wnd;
		styles.InitFromWnd(wnd);
		styles.ModifyExStyle(wnd, WS_EX_TRANSPARENT, 0);

		// Label area BG
		colors[0] = g_IdeSettings->GetNewProjectDlgColor(L"BackgroundLowerRegion", FALSE);
		colors[1] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		colors[2] = g_IdeSettings->GetEnvironmentColor(L"ControlOutline", FALSE);
	}
}

void GroupIDE11Draw::Detach(CButton* wnd, ThemeContext<CButton>& context)
{
	styles.RevertToOld(wnd);
	button = nullptr;
}

void GroupIDE11Draw::PrePaintForeground(CDC& dc, CRect& rect, ThemeContext<CButton>& context)
{
	if (button == nullptr || m_dpiHandler == nullptr)
		return;

	dc.FillSolidRect(rect, colors[0]);

	AutoSelectGDIObj afont(dc, m_dpiHandler->GetDpiAwareFont());
	CSize fntExt = dc.GetTextExtent(" ");

	CRect BorderRect = rect;
	BorderRect.top += fntExt.cy / 2;

	{
		CBrush bg_brush(colors[0]);
		CPen border_pen(PS_SOLID, 1, colors[2]);
		AutoSelectGDIObj aPen(dc, border_pen);
		AutoSelectGDIObj aBrush(dc, bg_brush);

		dc.RoundRect(BorderRect, CPoint(4, 4));
	}

	CRect lblLeft;
	lblLeft.right = 6; // in DLUs

	HWND parent = ::GetParent(button->GetSafeHwnd());
	if (parent == nullptr)
		return;

	::MapDialogRect(parent, &lblLeft);

	CRect txtRect = rect;
	txtRect.left += lblLeft.right;
	txtRect.bottom = txtRect.top + fntExt.cy;

	CStringW text;
	CWndTextHelp::GetText(button, text);
	::DrawTextW(dc, text, text.GetLength(), &txtRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);

	CRect txt_bg_rect = txtRect;
	txt_bg_rect.InflateRect(fntExt.cx / 2, 0);
	dc.FillSolidRect(txt_bg_rect, colors[0]);
	AutoTextColor atc(dc, colors[1]);

	::DrawTextW(dc, text, text.GetLength(), &txtRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

	context.SetResult(TRUE, !context.IsPaintMessage());

	if (styles.OldExStyle() & WS_EX_TRANSPARENT)
	{
		CRect wnd_rect, oth_rect, int_rect;
		button->GetWindowRect(wnd_rect);
		ThemeUtils::ForEachChild(parent, [&](HWND wnd) {
			CWnd* cwnd = CWnd::FromHandle(wnd);
			if (cwnd && cwnd != button)
			{
				cwnd->GetWindowRect(&oth_rect);
				if (int_rect.IntersectRect(wnd_rect, oth_rect))
				{
					CDC* wdc = cwnd->GetWindowDC();
					button->ScreenToClient(&oth_rect);
					dc.BitBlt(oth_rect.left, oth_rect.top, oth_rect.Width(), oth_rect.Height(), wdc, 0, 0, SRCCOPY);
					cwnd->ReleaseDC(wdc);
				}
			}
			return true;
		});
	}
}

void GroupIDE11Draw::WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                    ThemeContext<CButton>& context)
{
	__super::WindowProcPost(message, wParam, lParam, wnd, context);

	if (message == WM_UPDATEUISTATE)
		ThemeUtils::DelayedRefresh(wnd, 50);
	else if (message == WM_SETTEXT || message == BM_SETCHECK || message == WM_ENABLE)
		wnd->RedrawWindow(NULL, NULL,
		                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
}

void GroupIDE11Draw::WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
                                   ThemeContext<CButton>& context)
{
	__super::WindowProcPre(message, wParam, lParam, wnd, context);
}

HBRUSH ListBoxIDE11Draw::HandleCtlColor(CListBox* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CListBox>& context)
{
	if (ctrl != nullptr)
	{
		if (nCtlColor == CTLCOLOR_STATIC)
			pDC->SetTextColor(colors[2]);
		else
			pDC->SetTextColor(colors[1]);

		pDC->SetBkColor(colors[0]);
		return BrushMap(context).GetHBRUSH(colors[0]);
	}

	return nullptr;
}

void ListBoxIDE11Draw::Attach(CListBox* wnd, ThemeContext<CListBox>& context)
{
	__super::Attach(wnd, context);

	if (g_IdeSettings)
	{
		ctrl = wnd;

		// Label area BG
		colors[0] = g_IdeSettings->GetNewProjectDlgColor(L"BackgroundLowerRegion", FALSE);
		colors[1] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		colors[2] = g_IdeSettings->GetEnvironmentColor(L"GrayText", FALSE);
		colors[3] = g_IdeSettings->GetEnvironmentColor(L"ControlOutline", FALSE);
		colors[4] = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);

		styles.InitFromWnd(wnd);
		styles.ModifyStyle(wnd, 0, WS_BORDER);
		styles.ModifyExStyle(wnd, WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);
		::VsScrollbarTheme(wnd->m_hWnd, TRUE);
	}
}

void ListBoxIDE11Draw::Detach(CListBox* wnd, ThemeContext<CListBox>& context)
{
	ctrl = nullptr;

	::VsScrollbarTheme(wnd->m_hWnd, FALSE);
	styles.RevertToOld(wnd, SWP_DRAWFRAME);
	__super::Detach(wnd, context);
}

void ListBoxIDE11Draw::PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CListBox>& context)
{
	if (ctrl == nullptr)
		return;

	CBrush brush(context.State.IsFocused() ? colors[4] : colors[3]);
	dc.FrameRect(rect, &brush);
}

void ListBoxIDE11Draw::OnStateChanged(const ItemState& state, CWnd* wnd)
{
	if (ctrl && !state.IsFocused())
		ctrl->SetCurSel(-1);
}

void CThemedListBox::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(this);

	// TODO: Add your code to draw the specified item
	ASSERT(lpDrawItemStruct->CtlType == ODT_LISTBOX);

	CDC* dc = CDC::FromHandle(lpDrawItemStruct->hDC);
	AutoSaveRestoreDC asrDC(lpDrawItemStruct->hDC);

	CFont* font = GetFont();
	if (font)
		dc->SelectObject(font);

	bool is_focused = GetFocus() == this;

	if ((lpDrawItemStruct->itemAction | ODA_SELECT) && (lpDrawItemStruct->itemState & ODS_SELECTED))
	{
		COLORREF bg2 = is_focused ? sel_bg : unf_sel_bg;
		COLORREF txt = is_focused ? sel_text : unf_sel_text;

		dc->SetTextColor(txt);
		dc->SetBkColor(bg2);
		dc->FillSolidRect(&lpDrawItemStruct->rcItem, bg2);
	}
	else
	{
		dc->SetTextColor(text);
		dc->SetBkColor(bg);
		dc->FillSolidRect(&lpDrawItemStruct->rcItem, bg);
	}

	// Draw the text.

	CRect txt_rect = lpDrawItemStruct->rcItem;
	txt_rect.OffsetRect(5, 0);
	WTString str = (LPCTSTR)lpDrawItemStruct->itemData;
	CStringW wstr(str.Wide());
	::VaDrawTextW(*dc, wstr, &txt_rect, DT_SINGLELINE | DT_END_ELLIPSIS);

	if ((lpDrawItemStruct->itemAction | ODA_FOCUS) && (lpDrawItemStruct->itemState & ODS_FOCUS))
	{
		if (m_ext_theme)
			ThemeUtils::DrawFocusRect(*dc, &lpDrawItemStruct->rcItem, border);
		else
			dc->DrawFocusRect(&lpDrawItemStruct->rcItem);
	}
}

LRESULT CThemedListBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT rslt = __super::WindowProc(message, wParam, lParam);

	if (m_ext_theme && border != CLR_INVALID)
	{
		if (message == WM_NCPAINT || message == WM_PAINT || message == WM_ERASEBKGND)
		{
			ThemeUtils::DrawNCBorder(this, GetFocus() == this ? border : unf_border);
		}
	}

	if (message == WM_KILLFOCUS || message == WM_SETFOCUS)
	{
		Invalidate();
		UpdateWindow();
	}
	else if (message == WM_THEMECHANGED)
	{
		InitColorSchema();
		Invalidate();
		UpdateWindow();
	}

	return rslt;
}

HBRUSH CThemedListBox::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetTextColor(text);
	pDC->SetBkColor(bg);
	return ThemeUtils::GDIBuffer_GetBrush(bg);
}

void CThemedListBox::PreSubclassWindow()
{
	InitColorSchema();
	__super::PreSubclassWindow();
}

void CThemedListBox::InitColorSchema()
{
	m_ext_theme = CVS2010Colours::IsExtendedThemeActive();

	if (m_ext_theme)
	{
		bg = g_IdeSettings->GetThemeColor(ThemeCategory11::Cider, L"ListItem", FALSE);
		text = g_IdeSettings->GetThemeColor(ThemeCategory11::Cider, L"ListItem", TRUE);

		sel_bg = g_IdeSettings->GetThemeTreeColor(L"SelectedItemActive", FALSE);
		sel_text = g_IdeSettings->GetThemeTreeColor(L"SelectedItemActive", TRUE);

		unf_sel_bg = g_IdeSettings->GetThemeTreeColor(L"SelectedItemInactive", FALSE);
		unf_sel_text = g_IdeSettings->GetThemeTreeColor(L"SelectedItemInactive", TRUE);

		unf_border = g_IdeSettings->GetEnvironmentColor(L"ControlOutline", FALSE);
		border = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);

		::VsScrollbarTheme(m_hWnd, TRUE);
		ModifyStyle(0, WS_BORDER);
		ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_DRAWFRAME);
	}
	else
	{
		::VsScrollbarTheme(m_hWnd, FALSE);

		bg = ::GetSysColor(COLOR_WINDOW);
		text = ::GetSysColor(COLOR_WINDOWTEXT);

		sel_bg = ::GetSysColor(COLOR_HIGHLIGHT);
		sel_text = ::GetSysColor(COLOR_HIGHLIGHTTEXT);

		unf_sel_bg = ::GetSysColor(COLOR_BTNFACE);
		unf_sel_text = ::GetSysColor(COLOR_BTNTEXT);

		border = unf_border = CLR_INVALID;
	}
}

void CThemedListBox::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	ASSERT(lpMeasureItemStruct->CtlType == ODT_LISTBOX);

	CDC* pDC = GetDC();
	if (pDC)
	{
		CRect rect;
		GetItemRect((int)lpMeasureItemStruct->itemID, &rect);

		{
			AutoSaveRestoreDC asrDC(*pDC);

			CFont* font = GetFont();
			if (font)
				pDC->SelectObject(font);

			lpMeasureItemStruct->itemHeight = (UINT)pDC->DrawText(TEXT("Hg"), 2, rect, DT_CALCRECT);
		}

		ReleaseDC(pDC);
	}
}

bool WndProcEventArgsBase::is_notify(UINT notify_code, UINT& out_id_from)
{
	if (is_notify(notify_code))
	{
		out_id_from = (UINT)((LPNMHDR)lParam)->idFrom;
		return true;
	}
	return false;
}

bool WndProcEventArgsBase::is_notify(UINT notify_code)
{
	return msg == WM_NOTIFY && ((LPNMHDR)lParam)->code == notify_code;
}

bool WndProcEventArgsBase::is_command(UINT notify_code, UINT& out_id_from)
{
	if (is_command(notify_code))
	{
		out_id_from = LOWORD(wParam);
		return true;
	}
	return false;
}

bool WndProcEventArgsBase::is_command(UINT notify_code)
{
	return msg == WM_COMMAND && HIWORD(wParam) == notify_code;
}

NS_THEMEDRAW_END
