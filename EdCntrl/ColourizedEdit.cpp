#include "stdafxed.h"
#include "Colourizer.h"
#include "SyntaxColoring.h"
#include <windowsx.h>
#include "utils_goran.h"
#include "DoubleBuffer.h"
#include "Settings.h"
#include "ColorSyncManager.h"
#include "WtException.h"
#include "ColorListControls.h"
#include "IdeSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class EdCnt;

class CColourizedEdit : public CColourizedControl
{
  public:
	static CColourizedControlPtr Instance(HWND hwnd)
	{
		return CColourizedControlPtr(new CColourizedEdit(hwnd));
	}

	static void Register()
	{
		::RegisterColourizedControlClass("EDIT", &Instance);
	}

	virtual const char* GetClassName() const
	{
		return "EDIT";
	}

  public:
	CColourizedEdit(HWND hwnd) : CColourizedControl(hwnd), db(CColourizedControl::_hwnd)
	{
		deactivate_getdc = false;
		getdc_refreshed = false;
		inside_paint = false;
		last_scope = 0;
		mPaintType = PaintType::ListBox;
	}

	virtual ~CColourizedEdit()
	{
	}

	virtual patch WhatToPatch() const
	{
		return patch(patch_wndproc | patch_getdc | patch_releasedc);
	}

  protected:
	CCriticalSection cs;
	stdext::hash_map<HDC, HRGN> tracked_dcs;
	bool deactivate_getdc;
	bool getdc_refreshed;
	bool inside_paint;
	char last_scope;
	CDoubleBuffer db;

	virtual bool ShouldColourize() const
	{
		if (!Psettings->m_ActiveSyntaxColoring)
			return false;

		bool doColor = false;
		if (PaintType::ListBox == mPaintType || PaintType::None == mPaintType)
		{
			// edit controls in Add Member, Add similar member and Change
			// signature dialogs are colored due to this block
			doColor = Psettings->m_bEnhColorListboxes;
		}
		else if (PaintType::WizardBar == mPaintType)
		{
			doColor = Psettings->m_bEnhColorWizardBar;
		}

		return doColor;
	}

	virtual HDC GetDCHook(HWND hwnd, HDC(WINAPI* GetDCHookNext)(HWND hwnd))
	{
		HDC ret = GetDCHookNext(hwnd);

		__lock(cs);
		if (deactivate_getdc)
		{
			// if internal paint happens, we'll set clip to empty so nothing will be drawn (ok since we'll properly
			// refresh it later)
			getdc_refreshed = true;
			HRGN rgn = ::CreateRectRgn(0, 0, 0, 0);
			::GetClipRgn(ret, rgn);
			HRGN emptyrgn = ::CreateRectRgn(0, 0, 0, 0);
			::SelectClipRgn(ret, emptyrgn);
			::DeleteObject(emptyrgn);
			tracked_dcs[ret] = rgn;
		}
		return ret;
	}

	virtual int ReleaseDCHook(HWND hwnd, HDC hdc, int(WINAPI* ReleaseDCHookNext)(HWND hwnd, HDC hdc))
	{
		{
			__lock(cs);
			stdext::hash_map<HDC, HRGN>::iterator it = tracked_dcs.find(hdc);
			if (it != tracked_dcs.end())
			{
				::SelectClipRgn(hdc, it->second);
				::DeleteObject(it->second);
				tracked_dcs.erase(it);
			}
		}
		return ReleaseDCHookNext(hwnd, hdc);
	}

	virtual LRESULT ControlWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC WndProcNext)
	{
		hold_value(deactivate_getdc);
		{
			__lock(cs);
			switch (msg)
			{
			case WM_PAINT:
			case WM_PRINTCLIENT: {
				hold_value(inside_paint);
				inside_paint = true;
				EDIT_WM_Paint(hwnd, (HDC)wparam, true);
			}
				return 0;
				//			case WM_NCPAINT:
			case WM_ERASEBKGND:
				return 0;
			}
			deactivate_getdc = true;
			//			getdc_refreshed = false;
		}

		LRESULT ret = WndProcNext(hwnd, msg, wparam, lparam);

		if (getdc_refreshed && !inside_paint)
		{
			::InvalidateRect(hwnd, NULL, true);
			//			::RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_INTERNALPAINT | RDW_ERASENOW |
			// RDW_UPDATENOW | RDW_ALLCHILDREN);
			getdc_refreshed = false;
		}
		return ret;
	}

	void EDIT_WM_Paint(HWND hwnd, HDC hdc, bool force_paint)
	{
		HFONT old_font = 0;
		RECT rcLine;
		RECT rcRgn;

		const uint style = GetWindowStyle(hwnd);
		bool rev = ::IsWindowEnabled(hwnd) && ((::GetFocus() == hwnd) || (style & ES_NOHIDESEL));
		PAINTSTRUCT ps;
		memset(&ps, 0, sizeof(ps));
		HDC _dc = hdc ? hdc : ::BeginPaint(hwnd, &ps);
		if (!_dc)
		{
			VALOGERROR("ERROR: EDIT_WM_Paint no dc");
			return;
		}

		int savedc = ::SaveDC(_dc);
		try
		{
			CDoubleBufferedDC dc(db, _dc,
			                     gShellAttr->IsDevenv11OrHigher() ? g_IdeSettings->GetEnvironmentColor(L"Window", false)
			                                                      : ::GetSysColor(COLOR_WINDOW));
			::SetBkMode(dc, TRANSPARENT);

			CRect rcClient;
			::GetClientRect(hwnd, &rcClient);

			if (force_paint)
			{
				HRGN rgn = ::CreateRectRgnIndirect(rcClient);
				::SelectClipRgn(dc, rgn);
				::DeleteObject(rgn);
			}

			// get the background brush
			HBRUSH brush = EDIT_NotifyCtlColor(hwnd, dc);

			// paint the border and the background
			::IntersectClipRect(dc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

			if (style & WS_BORDER)
			{
				int bw = ::GetSystemMetrics(SM_CXBORDER);
				int bh = ::GetSystemMetrics(SM_CYBORDER);
				CRect rc = rcClient;
				if (style & ES_MULTILINE)
				{
					if (style & WS_HSCROLL)
						rc.bottom += bh;
					if (style & WS_VSCROLL)
						rc.right += bw;
				}

				// Draw the frame. Same code as in nonclient.c
				CBrush newBrush;
				HBRUSH old_brush;
				if (CVS2010Colours::IsExtendedThemeActive() && g_IdeSettings)
				{
					COLORREF col;
					if (::GetFocus() == hwnd)
						col = g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);
					else
						col = g_IdeSettings->GetNewProjectDlgColor(L"TextBoxBorder", FALSE);

					newBrush.CreateSolidBrush(col);
					old_brush = (HBRUSH)newBrush;
				}
				else
					old_brush = (HBRUSH)::SelectObject(dc, ::GetSysColorBrush(COLOR_WINDOWFRAME));
				::PatBlt(dc, rc.left, rc.top, rc.right - rc.left, bh, PATCOPY);
				::PatBlt(dc, rc.left, rc.top, bw, rc.bottom - rc.top, PATCOPY);
				::PatBlt(dc, rc.left, rc.bottom - 1, rc.right - rc.left, -bw, PATCOPY);
				::PatBlt(dc, rc.right - 1, rc.top, -bw, rc.bottom - rc.top, PATCOPY);
				::SelectObject(dc, old_brush);

				// Keep the border clean
				::IntersectClipRect(dc, rc.left + bw, rc.top + bh, std::max(rc.right - bw, rc.left + bw),
				                    std::max(rc.bottom - bh, rc.top + bh));
			}

			CRect rc;
			::GetClipBox(dc, &rc);
			::FillRect(dc, &rc, brush);

			CRect format_rect;
			Edit_GetRect(hwnd, &format_rect);
			::IntersectClipRect(dc, format_rect.left, format_rect.top, format_rect.right, format_rect.bottom);
			if (style & ES_MULTILINE)
			{
				rc = rcClient;
				::IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
			}
			HFONT font = (HFONT)::SendMessage(hwnd, WM_GETFONT, NULL, NULL);
			if (font)
				old_font = (HFONT)::SelectObject(dc, font);

			if (!::IsWindowEnabled(hwnd))
			{
				if (CVS2010Colours::IsExtendedThemeActive() && g_IdeSettings)
					::SetTextColor(dc, g_IdeSettings->GetEnvironmentColor(L"GrayText", FALSE));
				else
					::SetTextColor(dc, ::GetSysColor(COLOR_GRAYTEXT));
			}
			::GetClipBox(dc, &rcRgn);
			if (style & ES_MULTILINE)
			{
				TEXTMETRICW tm;
				::GetTextMetricsW(dc, &tm);
				const int line_height = tm.tmHeight;
				const int y_offset = Edit_GetFirstVisibleLine(hwnd);
				const int line_count = Edit_GetLineCount(hwnd);

				INT vlc = (format_rect.bottom - format_rect.top) / line_height;
				for (int i = y_offset; i <= std::min(y_offset + vlc, y_offset + line_count - 1); i++)
				{
					EDIT_GetLineRect(hwnd, dc, i, 0, -1, &rcLine, format_rect);
					if (::IntersectRect(&rc, &rcRgn, &rcLine))
						EDIT_PaintLine(hwnd, dc, i, rev, format_rect);
				}
			}
			else
			{
				EDIT_GetLineRect(hwnd, dc, 0, 0, -1, &rcLine, format_rect);
				if (::IntersectRect(&rc, &rcRgn, &rcLine))
					EDIT_PaintLine(hwnd, dc, 0, rev, format_rect);
			}
			if (font)
				::SelectObject(dc, old_font);
		}
		catch (const WtException& e)
		{
			UNUSED_ALWAYS(e);
			VALOGERROR("ERROR: EDIT_WM_Paint exception caught 1");
		}
		catch (CException* e)
		{
			UNUSED_ALWAYS(e);
			VALOGERROR("ERROR: EDIT_WM_Paint exception caught 2");
		}

		::RestoreDC(_dc, savedc);

		if (!hdc)
			::EndPaint(hwnd, &ps);
	}

	HBRUSH EDIT_NotifyCtlColor(HWND hwnd, HDC hdc)
	{
		UINT msg;
		if ((get_app_version() >= 0x40000) && (!::IsWindowEnabled(hwnd) || (GetWindowStyle(hwnd) & ES_READONLY)))
			msg = WM_CTLCOLORSTATIC;
		else
			msg = WM_CTLCOLOREDIT;

		HBRUSH hbrush = (HBRUSH)::SendMessageW(::GetParent(hwnd), msg, (WPARAM)hdc, (LPARAM)hwnd);
		if (!hbrush)
			hbrush = (HBRUSH)::DefWindowProcW(::GetParent(hwnd), msg, (WPARAM)hdc, (LPARAM)hwnd);
		return hbrush;
	}

	void EDIT_GetLineRect(HWND hwnd, HDC dc, INT line, INT scol, INT ecol, LPRECT rc, const CRect& format_rect)
	{
		INT line_index = Edit_LineIndex(hwnd, line);
		TEXTMETRICW tm;
		::GetTextMetricsW(dc, &tm);
		const int line_height = tm.tmHeight;

		if (GetWindowStyle(hwnd) & ES_MULTILINE)
			rc->top = format_rect.top + (line - Edit_GetFirstVisibleLine(hwnd)) * line_height;
		else
			rc->top = format_rect.top;
		rc->bottom = rc->top + line_height;
		// gmit: wrap not true in posfromchar something-something
		rc->left = (scol == 0) ? format_rect.left
		                       : (short)LOWORD(::SendMessage(hwnd, EM_POSFROMCHAR, WPARAM(line_index + scol), 0));
		rc->right = (ecol == -1) ? format_rect.right
		                         : (short)LOWORD(::SendMessage(hwnd, EM_POSFROMCHAR, WPARAM(line_index + ecol), 0));
	}

	void EDIT_PaintLine(HWND hwnd, HDC dc, INT line, BOOL rev, const CRect& format_rect)
	{
		INT s = (short)LOWORD(Edit_GetSel(hwnd));
		INT e = (short)HIWORD(Edit_GetSel(hwnd));
		const int y_offset = Edit_GetFirstVisibleLine(hwnd);
		const int line_count = Edit_GetLineCount(hwnd);
		TEXTMETRICW tm;
		::GetTextMetricsW(dc, &tm);
		const int line_height = tm.tmHeight;

		if (GetWindowStyle(hwnd) & ES_MULTILINE)
		{
			INT vlc = (format_rect.bottom - format_rect.top) / line_height;
			if ((line < y_offset) || (line > y_offset + vlc) || (line >= line_count))
				return;
		}
		else if (line)
			return;

		LRESULT pos = ::SendMessage(hwnd, EM_POSFROMCHAR, (WPARAM)Edit_LineIndex(hwnd, line), 0);
		int x = (short)LOWORD(pos);
		int y = (short)HIWORD(pos);
		int li = Edit_LineIndex(hwnd, line);
		int ll = Edit_LineLength(hwnd, li);
		s = std::min((short)LOWORD(Edit_GetSel(hwnd)), (short)HIWORD(Edit_GetSel(hwnd)));
		e = std::max((short)LOWORD(Edit_GetSel(hwnd)), (short)HIWORD(Edit_GetSel(hwnd)));
		s = std::min(li + ll, std::max(li, s));
		e = std::min(li + ll, std::max(li, e));

		hold_value(last_scope);
		last_scope = 0;

		bool do_colourize = ShouldColourize();
		if (rev && (s != e) && ((::GetFocus() == hwnd) || (GetWindowStyle(hwnd) & ES_NOHIDESEL)))
		{
			x += EDIT_PaintText(hwnd, dc, x, y, line, 0, s - li, FALSE, format_rect, do_colourize);
			x += EDIT_PaintText(hwnd, dc, x, y, line, s - li, e - s, TRUE, format_rect, false);
			x += EDIT_PaintText(hwnd, dc, x, y, line, e - li, li + ll - e, FALSE, format_rect, do_colourize);
		}
		else
			x += EDIT_PaintText(hwnd, dc, x, y, line, 0, ll, FALSE, format_rect, do_colourize);
	}

	INT EDIT_PaintText(HWND hwnd, HDC dc, INT x, INT y, INT line, INT col, INT count, BOOL rev,
	                   const CRect& format_rect, bool do_colourize)
	{
		//		HFONT hUnderline = NULL;
		//		HFONT old_font = NULL;

		if (count < 1)
			return 0;
		int BkMode = ::GetBkMode(dc);
		COLORREF BkColor = ::GetBkColor(dc);
		COLORREF TextColor = ::GetTextColor(dc);
		if (rev)
		{
			//			if(es->composition_len == 0) {
			if (CVS2010Colours::IsExtendedThemeActive() && g_IdeSettings)
			{
				::SetBkColor(dc, CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHT));
				::SetTextColor(dc, CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHTTEXT));
			}
			else
			{
				::SetBkColor(dc, ::GetSysColor(COLOR_HIGHLIGHT));
				::SetTextColor(dc, ::GetSysColor(COLOR_HIGHLIGHTTEXT));
			}
			::SetBkMode(dc, OPAQUE);
			//			} else {
			//				HFONT current = ::GetCurrentObject(dc, OBJ_FONT);
			//				LOGFONTW underline_font;
			//				::GetObjectW(current, sizeof(LOGFONTW), &underline_font);
			//				underline_font.lfUnderline = TRUE;
			//				hUnderline = ::CreateFontIndirectW(&underline_font);
			//				old_font = ::SelectObject(dc, hUnderline);
			//			}
		}
		int li = Edit_LineIndex(hwnd, line);
		INT ret;
		// gmit: tabs and passwords not implemented
		int max_count = GetWindowTextLength(hwnd) + 1;
		const std::unique_ptr<wchar_t[]> textVec(new wchar_t[(size_t)max_count]);
		wchar_t* text = &textVec[0];
		::GetWindowTextW(hwnd, text, max_count);
		const std::unique_ptr<char[]> textAVec(new char[(size_t)max_count]);
		char* textA = &textAVec[0];
		::GetWindowTextA(hwnd, textA, max_count);
		if (GetWindowStyle(hwnd) & ES_MULTILINE)
		{
			_ASSERTE(!"gmit: shouldn't be supported yet");
			ret = (INT)LOWORD(::TabbedTextOutW(dc, x, y, text + li + col, count, 0 /*es->tabs_count*/, 0 /*es->tabs*/,
			                                   format_rect.left - 0 /*es->x_offset*/));
		}
		else
		{
			//			LPWSTR text = EDIT_GetPasswordPointer_SL(es);

			/*			if(do_colourize) {
			                extern EdCnt *g_currentEdCnt;
			                extern EdCnt *g_paintingEdCtrl;
			                extern BOOL g_IsOurDlg;

			                hold_value(g_paintingEdCtrl);
			                hold_value(g_IsOurDlg);
			                g_paintingEdCtrl = g_currentEdCnt;
			                g_IsOurDlg = TRUE;

			//				BOOL WINAPI OurTextOut_ColorPrint(HDC dc, int x, int y, UINT style, LPCSTR str, int len,
			LPCRECT prc, CONST INT *w, char *outer_lastScope);
			//				OurTextOut_ColorPrint(dc, x, y, 0, textA + li + col, count, NULL, NULL, &last_scope);
			            } else {
			                ::TextOutW(dc, x, y, text + li + col, count);
			            }*/

			int prev_in_WM_PAINT = PaintType::in_WM_PAINT;
			PaintType::in_WM_PAINT = do_colourize ? mPaintType : PaintType::None;
			int prev_inPaintType = PaintType::inPaintType;
			PaintType::SetPaintType(PaintType::in_WM_PAINT);

			::TextOutW(dc, x, y, text + li + col, count);

			PaintType::in_WM_PAINT = prev_in_WM_PAINT;
			PaintType::SetPaintType(prev_inPaintType);

			SIZE size;
			::GetTextExtentPoint32W(dc, text + li + col, count, &size);
			ret = size.cx;
			//			if(GetWindowStyle(hwnd) & ES_PASSWORD)
			//				HeapFree(GetProcessHeap(), 0, text);
		}
		if (rev)
		{
			//			if(es->composition_len == 0) {
			::SetBkColor(dc, BkColor);
			::SetTextColor(dc, TextColor);
			::SetBkMode(dc, BkMode);
			//			} else {
			//				if(old_font)
			//					::SelectObject(dc,old_font);
			//				if(hUnderline)
			//					::DeleteObject(hUnderline);
			//			}
		}
		return ret;
	}

	// update at some point:
	// http://stackoverflow.com/questions/22303824/warning-c4996-getversionexw-was-declared-deprecated
#pragma warning(push)
#pragma warning(disable : 4996)
	static DWORD get_app_version()
	{
		static DWORD version;
		if (!version)
		{
			DWORD dwEmulatedVersion;
			OSVERSIONINFOW info;
			DWORD dwProcVersion = ::GetProcessVersion(0);

			info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
			::GetVersionExW(&info);
			dwEmulatedVersion = (DWORD)MAKELONG(info.dwMinorVersion, info.dwMajorVersion);
			version = dwProcVersion < dwEmulatedVersion ? dwProcVersion : dwEmulatedVersion;
		}
		return version;
	}
#pragma warning(pop)
};

#ifndef RAD_STUDIO // No coloring in CppBuilder
namespace
{
const volatile int myBOOST_JOIN(__ccregister, __LINE__) = (CColourizedEdit::Register(), 5);
}
#endif
