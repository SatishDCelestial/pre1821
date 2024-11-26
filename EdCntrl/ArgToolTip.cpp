// ArgToolTip.cpp : implementation file
//
#include "stdafxed.h"
#include "EdCnt.h"
#include "ArgToolTip.h"
#include "VaMessages.h"
#include "wtcsym.h"
#include "fontsettings.h"
#include "FileTypes.h"
#include "Settings.h"
#include "VAClassView.h"
#include "ScreenAttributes.h"
#include "SyntaxColoring.h"
#include <list>
#include <string>
#include "WindowUtils.h"
#include "project.h"
#include "DevShellAttributes.h"
#include "VACompletionBox.h"
#include "VAAutomation.h"
#include "IdeSettings.h"
#include "ImageListManager.h"
#include "MenuXP\Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

/////////////////////////////////////////////////////////////////////////////
// ArgToolTip
ArgToolTip* g_pLastToolTip = NULL;
ArgToolTip* s_ttip = NULL;

HWND sActiveToolTipWrapperTipOwner = NULL;
DWORD sToolTipWrapperStartTicks = 0;
CPoint sToolTipWrapperStartPt;

// unused #define MOUSETIMER 9741
extern WTString VA_Snippet_Edit_suffix;
static std::unique_ptr<CVAThemeHelper> th;

void ArgToolTip::UpdateFonts()
{
	UINT dpi = VsUI::CDpiAwareness::GetDpiForWindow(m_hWnd);
	if (S_OK == mFont.Update(dpi, VaFontType::TTdisplayFont))
	{
		// update those only if normal has changed
		mFontB.Update(dpi, VaFontType::TTdisplayFont, FS_Bold);
		mFontU.Update(dpi, VaFontType::TTdisplayFont, FS_Underline);
	}
}

ArgToolTip::ArgToolTip(EdCnt* par) : mColorOverride(false), mFgOverride(0), mBgOverride(0), mBorderOverride(0)
{
	m_hPar = par->m_hWnd;
	m_ed = par;
	m_hWnd = NULL;
	m_currentDef = m_totalDefs = 0;
	m_reverseColors = false;
	mRedisplayOnMouseUp = do_hide_on_mouse_up = false;
	dont_close_on_ctrl_key = false;
	avoid_hwnd = NULL;
	theme = NULL;
	if (!th)
		th.reset(new CVAThemeHelper);
}

ArgToolTip::ArgToolTip(HWND hParent) : mColorOverride(false), mFgOverride(0), mBgOverride(0), mBorderOverride(0)
{
	m_hPar = hParent;
	m_ed = NULL;
	m_hWnd = NULL;
	m_currentDef = m_totalDefs = 0;
	m_reverseColors = false;
	mRedisplayOnMouseUp = do_hide_on_mouse_up = false;
	dont_close_on_ctrl_key = false;
	avoid_hwnd = NULL;
	theme = NULL;
	if (!th)
		th.reset(new CVAThemeHelper);
}

ArgToolTip::~ArgToolTip()
{
	UseTheme(false);
	if (this == g_pLastToolTip)
		g_pLastToolTip = NULL;

	if (m_hWnd && ::IsWindow(m_hWnd))
		DestroyWindow();
	m_hWnd = NULL;

	try
	{
		if (s_ttip)
		{
			//			s_ttip->DestroyWindow();
			ArgToolTip* tt = s_ttip;
			s_ttip = NULL;
			delete tt;
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("ATT");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTip::~ArgToolTip");
		}
	}
}

const uint WM_REPAINTBENEATH = ::RegisterWindowMessage("WM_REPAINTBENEATH");

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(ArgToolTip, CWnd)
//{{AFX_MSG_MAP(ArgToolTip)
ON_WM_ACTIVATE()
ON_WM_MOUSEACTIVATE()
ON_WM_PAINT()
ON_WM_NCDESTROY()
ON_REGISTERED_MESSAGE(WM_REPAINTBENEATH, OnRepaintBeneath)
ON_REGISTERED_MESSAGE(WM_VA_SELFDELETE, OnVASelfDelete)
//}}AFX_MSG_MAP
ON_WM_LBUTTONUP()
ON_WM_RBUTTONUP()
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// ArgToolTip message handlers

void ArgToolTip::OnActivate(UINT, CWnd*, BOOL)
{
	if (m_ed)
		m_ed->vSetFocus();
}
#define TT_TIMER 49735
LPARAM
ArgToolTip::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_PAINT)
		SetTimer(TT_TIMER, 500, NULL);
	if (message == WM_TIMER && wParam == TT_TIMER)
	{
		KillTimer(TT_TIMER);
		if (IsWindowVisible())
		{
			if (!::GetFocus())
				ShowWindow(SW_HIDE);
			else
				SetTimer(TT_TIMER, 500, NULL);
		}
	}
	if (message == WM_SHOWWINDOW)
	{
		if (wParam == SW_HIDE)
			PostMessage(WM_REPAINTBENEATH);
	}
	// unused 	if(message == WM_TIMER && wParam == MOUSETIMER)
	// 	{
	// 		KillTimer(wParam);
	// 		ShowWindow(SW_HIDE);
	// 		return TRUE;
	// 	}
	//	if(::GetFocus() == NULL && IsWindowVisible())
	//		ShowWindow(SW_HIDE);
	return CWnd::DefWindowProc(message, wParam, lParam);
}
#include "VACompletionSet.h"
#include "expansion.h"
int ArgToolTip::OnMouseActivate(CWnd*, UINT, UINT msg)
{
	if (m_totalDefs < 2)
	{
		if (g_CompletionSet && m_lstr.contains(VA_Snippet_Edit_suffix))
			do_hide_on_mouse_up = true;
		else
			DoHideOnActivate();
	}
	else if (m_ed)
	{
		if (msg == WM_LBUTTONDOWN)
		{
			// like a VK_DOWN
			if (m_totalDefs == m_currentDef)
				m_ed->m_tootipDef = 1;
			else
				m_ed->m_tootipDef++;
			mRedisplayOnMouseUp = true; // [case: 32265] delay to fix weird overload scrolling
		}
		else if (msg == WM_RBUTTONDOWN)
		{
			// like a VK_UP
			if (m_currentDef == 1)
				m_ed->m_tootipDef = m_totalDefs;
			else if (m_ed->m_tootipDef > 1)
				m_ed->m_tootipDef--;
			mRedisplayOnMouseUp = true; // [case: 32265] delay to fix weird overload scrolling
		}
	}
	return MA_NOACTIVATEANDEAT;
}

void ArgToolTip::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (do_hide_on_mouse_up)
	{
		DoHideOnActivate();
		do_hide_on_mouse_up = false;
	}

	CWnd::OnLButtonUp(nFlags, point);

	if (mRedisplayOnMouseUp)
	{
		mRedisplayOnMouseUp = false;
		m_ed->DisplayToolTipArgs(true);
	}
}

void ArgToolTip::OnRButtonUp(UINT nFlags, CPoint point)
{
	CWnd::OnRButtonUp(nFlags, point);

	if (mRedisplayOnMouseUp)
	{
		mRedisplayOnMouseUp = false;
		m_ed->DisplayToolTipArgs(true);
	}
}

void ArgToolTip::DoHideOnActivate()
{
	ShowWindow(SW_HIDE);

	if (g_CompletionSet && m_lstr.contains(VA_Snippet_Edit_suffix))
		g_CompletionSet->FilterListType(ICONIDX_MODFILE | VA_TB_CMD_FLG);
	m_currentDef = m_totalDefs = 0;
	m_lstr.Empty();
	m_cstr.Empty();
	m_rstr.Empty();
}

#define RINDENT 30
void ArgToolTip::Display(CPoint* pt, LPCSTR left, LPCSTR cur, LPCSTR right, int totalDefs, int currentDef,
                         bool reverseColor /*= false*/, bool keeponscreen /*= true*/, BOOL color /*= TRUE*/,
                         BOOL ourTip /*= TRUE*/)
{
	try
	{
		if (::GetFocus() == NULL)
			return;
		vCatLog("Editor.Events", "VaEventTT   Display l='%s', c='%s', r='%s', n=%d, clr=%d, ours=%d", left, cur, right, totalDefs, color,
		     ourTip);
		if (g_pLastToolTip) // catch stray tooltip
		{
			if (!g_currentEdCnt || g_currentEdCnt->m_ttParamInfo != g_pLastToolTip || HasVsNetPopup(false))
				g_pLastToolTip->ShowWindow(SW_HIDE);
		}
		m_ourTip = ourTip;
		m_argInfo = (right && *right);
		g_pLastToolTip = this;
		if (!m_hWnd)
		{
			CRect r(10, 10, 20, 20);
			static const WTString argToolTipWndCls(AfxRegisterWndClass(0, LoadCursor(NULL, IDC_ARROW)));
			CreateEx(WS_EX_TOPMOST, argToolTipWndCls.c_str(), _T(""), WS_POPUP, r, FromHandle(m_hPar), 0);
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, GetWindowLongPtr(m_hWnd, GWLP_USERDATA) | VA_WND_DATA);
			vLog2("ATT::D %p", m_hWnd);
		}

		m_color = color && Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorTooltips;
		if (m_color)
			myRemoveProp(m_hWnd, "__VA_do_not_colour");
		else
			mySetProp(m_hWnd, "__VA_do_not_colour", (HANDLE)1);

		m_keeponscreen = keeponscreen;
		m_totalDefs = totalDefs;
		if (currentDef <= totalDefs)
			m_currentDef = currentDef;
		else if (m_currentDef > totalDefs)
			m_currentDef = 1;
		m_cstr = DecodeScope(cur);
		m_rstr = DecodeScope(right);
		m_pt = *pt;
		m_lstr = DecodeScope(left);
		m_reverseColors = reverseColor;
		if (m_cstr.length() || m_rstr.length() || m_lstr.length())
		{
			if (gTestLogger && gTestLogger->IsArgTooltipLoggingEnabled())
			{
				WTString msg;
				msg.WTFormat("ArgToolTip: %d, %d of %d, \"%s\" \"%s\" \"%s\"", ourTip, currentDef, totalDefs, left, cur,
				             right);
				gTestLogger->LogStr(msg);
			}
			Layout(false);
		}
		//		if(!m_argInfo) // leave arginfo for ever...
		//			SetTimer(MOUSETIMER, 2000, NULL);
	}
	catch (...)
	{
		VALOGEXCEPTION("ATT");
		// someone deleted m_ttSuggestWord
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTip::Display");
		}
	}
}

int ArgToolTip::BeautifyDefs(token& defTok)
{
	return ::BeautifyDefs(defTok);
}

int BeautifyDefs(token& defTok)
{
	static string spacer("\t");
	static string asterisk("*");
	// strip stuff out and replace with our spacer
	defTok.ReplaceAll(TRegexp("^ "), OWL_NULLSTR);
	defTok.ReplaceAll(TRegexp(" "), spacer);
	defTok.ReplaceAll("ACMAPI", "", TRUE);
	defTok.ReplaceAll(TRegexp("_AFX[A-Z]*_INLINE"), spacer);
	defTok.ReplaceAll("AFXAPI", "", TRUE);
	defTok.ReplaceAll("AFX_CDECL", "", TRUE);
	defTok.ReplaceAll("APIENTRY", "", TRUE);
	defTok.ReplaceAll(TRegexp("[_]*CRTIMP"), spacer);
	defTok.ReplaceAll("D3DAPI", "", TRUE);
	defTok.ReplaceAll("FAR", "", TRUE);
	defTok.ReplaceAll("INTSHCUTAPI", "", TRUE);
	defTok.ReplaceAll(TRegexp("NT[A-Z]*API"), spacer);
	defTok.ReplaceAll("PASCAL", "", TRUE);
	defTok.ReplaceAll(TRegexp("STDMETHOD[A-Z]*"), spacer);
	defTok.ReplaceAll(TRegexp("WIN[A-Z]*API[V]*"), spacer);
	defTok.ReplaceAll("afx_msg", "", TRUE);
	defTok.ReplaceAll("extern \"C\"", "");
	defTok.ReplaceAll("\"C\"", "");
	defTok.ReplaceAll("extern", "", TRUE);
	defTok.ReplaceAll("far", "", TRUE);
	//	Not sure why we took out the word static, but Case 261: is a request to put it back.
	//	defTok.ReplaceAll(TRegexp("static\t"), spacer);
	//	defTok.ReplaceAll(TRegexp("virtual\t"), spacer);
	//	defTok.ReplaceAll(TRegexp("[_]*inline"), spacer);
	defTok.ReplaceAll("_cdecl", "", TRUE);
	defTok.ReplaceAll("cdecl", "", TRUE);
	defTok.ReplaceAll("CDECL", "", TRUE);
	defTok.ReplaceAll("__stdcall", "", TRUE);
	defTok.ReplaceAll("__thiscall", "", TRUE);

	// replace all of our spacers with real spaces
	defTok.ReplaceAll(TRegexp("[\t]+"), OWL_SPACESTR);

	// replace any " \f " sequences with just "\f" so that we can compare defs correctly
	defTok.ReplaceAll(TRegexp("[ ]*\f[ ]*"), spacer);
	defTok.ReplaceAll(TRegexp("\t"), string("\f"));
	defTok.ReplaceAll("\r\n", "\f");
	defTok.ReplaceAll("\n", "\f");
	// strip everything after the closing paren for each fn
	WTString defs;
	int retval = 0;
	while (defTok.more())
	{
		WTString oneDef = defTok.read("\f");
		int pos = max(oneDef.ReverseFind(')'), oneDef.ReverseFind('"')); // find last ) or '"'
		WTString newDef;
		if (pos == -1 || oneDef.Find("#define") != -1) // dont strip on #defines
			newDef = oneDef;
		else
			newDef = oneDef.Left(pos + 1);

		// make sure unique before adding to def list string
		if (defs.Find(newDef) == -1)
		{
			defs += newDef;
			defs += "\f";
			retval++;
		}
	}

	defTok = defs.c_str();
	ASSERT(defs.GetTokCount('\f') == retval);
	return retval;
}

// finds last occurrence of any of listed chars within a substring
static int my_find_last_of(const wchar_t* str, int start, int size, const wchar_t* chars)
{
	while (size-- > 0)
	{
		if (wcschr(chars, str[start + size]))
			return start + size;
	}
	return NPOS;
}

// return true if given path looks like directory
BOOL IsPathDirectory(const CStringW& path)
{
	if (path.GetLength() < 3)
		return false;
	if (!(((path[0] >= 'a') && (path[0] <= 'z')) || ((path[0] >= 'A') && (path[0] <= 'Z'))))
		return false;
	if (path[1] != ':')
		return false;
	if ((path[2] != '\\') && (path[2] != '/'))
		return false;
	// 	if((path[path.size() - 1] != '\\') && (path[path.size() - 1] != '/'))
	// 		return false;
	// 	return true;
	return ::PathIsDirectoryW(path);
}

void ArgToolTip::Layout(bool in_paint)
{
	try
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

		//		if (::GetFocus() != m_ed->m_hWnd) {
		//			ShowWindow(SW_HIDE);
		//			return;
		//		}
		WTString wleft = m_lstr;
		CDC* dc = GetDC();
		CDC memdc;
		CBitmap bmp;
		// create memdc & create storage bitmap
		memdc.CreateCompatibleDC(dc);
		int maxh = max(1600, ::GetSystemMetrics(SM_CYFULLSCREEN)), maxw = g_FontSettings->m_tooltipWidth + 100;

		bool ret = !!bmp.CreateCompatibleBitmap(dc, maxw, maxh);
		if (!ret || !bmp.m_hObject)
		{
			std::vector<byte> biVec(1024, 0);
			BITMAPINFO* bi = (BITMAPINFO*)&biVec[0];
			bi->bmiHeader.biSize = sizeof(bi->bmiHeader);
			bi->bmiHeader.biWidth = maxw;
			bi->bmiHeader.biHeight = maxh;
			bi->bmiHeader.biPlanes = 1;
			bi->bmiHeader.biBitCount = 32;
			bi->bmiHeader.biCompression = BI_RGB;
			*(uint*)&(bi->bmiColors[0]) = 0x000000ff;
			*(uint*)&(bi->bmiColors[1]) = 0x0000ff00;
			*(uint*)&(bi->bmiColors[2]) = 0x00ff0000;
			void* bits = NULL;
			bmp.Attach(::CreateDIBSection(*dc, bi, DIB_RGB_COLORS, &bits, NULL, NULL));
		}
		memdc.SelectObject(bmp);

		// init dc
		CRect crect;
		::GetClientRect(*this, &crect);
		const bool useTheme = theme && m_hWnd && !crect.IsRectEmpty() && !m_reverseColors;
		if (!useTheme && theme && mColorOverride)
			mColorOverride = false;

		if (useTheme)
		{
			// helps make the corners look round
			CBrush brush;
			brush.CreateSolidBrush(Psettings->m_colors[C_Text].c_bg);
			memdc.FillRect(crect, &brush);
		}

		const COLORREF bkColor = mColorOverride ? mBgOverride : Psettings->m_TTbkColor;
		const COLORREF txtColor = mColorOverride ? mFgOverride : Psettings->m_TTtxtColor;

		if (useTheme)
			::DrawThemeBackground(theme, memdc, TTP_STANDARD, 0 /*TTSS_NORMAL*/, &crect, &crect);
		else if (m_reverseColors)
		{
			// show hints in reverse color
			CBrush brBG(txtColor);
			memdc.FillRect(CRect(0, 0, maxw, maxh), &brBG);
			memdc.SetTextColor(bkColor);
		}
		else
		{
			// set suggestWord and param info colors
			CBrush brBG(bkColor);
			memdc.FillRect(CRect(0, 0, maxw, maxh), &brBG);
			memdc.SetTextColor(txtColor);
			memdc.SetBkColor(bkColor);
		}

		// fonts get updated only if something has changed in their definition
		UpdateFonts();

		memdc.SetBkMode(TRANSPARENT);
		memdc.SelectObject(mFont);
		memdc.SetBkColor(bkColor);

		// prepare tooltip text
		WTString _txt = m_lstr;
		if (m_cstr.GetLength())
		{
			_txt += FTM_BOLD + m_cstr + FTM_NORMAL; // bold current argument
		}
		_txt += m_rstr;
		if (strstr(_txt.c_str(), VA_Snippet_Edit_suffix.c_str()))
		{
			_txt.ReplaceAll(VA_Snippet_Edit_suffix,
			                VA_Snippet_Edit_suffix.Left(2) + WTString(FTM_UNDERLINE) +
			                    VA_Snippet_Edit_suffix.Mid(2, VA_Snippet_Edit_suffix.GetLength() - 4) +
			                    WTString(FTM_NORMAL) + VA_Snippet_Edit_suffix.Right(2));
		}

		bool master_do_not_colour_switch = false;
		if (!(Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorTooltips))
		{
			master_do_not_colour_switch = true;
		}
		else if (!strncmp(_txt.c_str(), TT_TEXT, 30))
		{
			master_do_not_colour_switch = true;
		}
		else if (myGetProp(m_hWnd, "__VA_do_not_colour"))
		{
			master_do_not_colour_switch = true;
		}
		else if (strstr(_txt.c_str(), VA_Snippet_Edit_suffix.c_str()))
		{
			master_do_not_colour_switch = true;
		}

		FormattedTextLines lines;
		ParseFormattedTextMarkup(_txt.Wide(), lines);

		// don't colour lines with files (important is to mark before wrapping)
		for (auto it = lines.begin(); it != lines.end(); ++it)
		{
			// [case: 18867] don't hit disk if we aren't even coloring
			if (!m_color ||
			    (!wcsncmp(it->_txt, L"Accept with:", 12) || // [case: 36725] don't color this line
			     !_wcsnicmp(it->_txt, L"File: ", 6) || GetFileType(it->_txt) != Other) ||
			    IsPathDirectory(it->_txt))
			{
				it->_flags = line_flags(it->_flags | line_dont_colour);
			}
		}

		// wrap text if needed
		const int max_line_len = 100;
		const int kRemainingLenWiggleRoom = 25;
		int remaining_line_len = max_line_len;
		const CStringW wrap_indentation = L"        ";
		const wchar_t* const kWrapChars = L" \t|/\\()";
		int safetyCatch = 0; // [case: 66501]
		for (auto it = lines.begin(); it != lines.end() && safetyCatch < 500; ++it, ++safetyCatch)
		{
			if ((int)it->_txt.GetLength() <= remaining_line_len)
			{
				// whole line fits
				if ((it->_flags & line_no_new_line) && ((int)it->_txt.GetLength() != remaining_line_len))
				{
					remaining_line_len -= it->_txt.GetLength();
				}
				else
				{
					if ((it->_flags & line_no_new_line))
					{
						_ASSERTE(remaining_line_len == max_line_len);
						_ASSERTE((int)it->_txt.GetLength() == remaining_line_len);
						auto it2 = it;
						if (++it2 != lines.end())
						{
							// insert indentation to next line since this one ends perfectly
							CStringW curln(it2->_txt);
							// before inserting wrap prefix string, remove possible leading whitespace
							curln.TrimLeft();
							it2->_txt = wrap_indentation + curln;
						}
					}

					// whole line fit or no_new_line flag that fills up to the end
					remaining_line_len = max_line_len;
					it->_flags = line_flags(it->_flags & ~line_no_new_line);
				}
				continue;
			}

			int i = my_find_last_of(it->_txt, 0, remaining_line_len, kWrapChars);
			const int i2 = my_find_last_of(it->_txt, 0, remaining_line_len, L",");
			if (NPOS != i2)
			{
				// [case: 72113] give priority to comma over kWrapChars
				// "...int bar, int baz" wrap at comma, not at space after second int
				if (NPOS == i)
					i = i2;
				else if (i2 > i)
					i = i2;
				else if (i - i2 < kRemainingLenWiggleRoom)
					i = i2;

				if (i == i2)
				{
					// this means we are breaking on a comma
					// now check for whitespace abutting the comma and take it also
					int len = it->_txt.GetLength();
					if (i + 1 < len && ::wt_isspace(it->_txt[i + 1]))
						++i;
				}
			}
			else if (i != NPOS && i < kRemainingLenWiggleRoom)
			{
				if (it->_txt.GetLength() < max_line_len && remaining_line_len != max_line_len)
				{
					// see VAManualTest in test_tooltips.cpp
					remaining_line_len = i = 0;
				}
			}

			if ((i == NPOS) || (i < (remaining_line_len / 3)) ||
			    (i < remaining_line_len && remaining_line_len < kRemainingLenWiggleRoom &&
			     it->_txt.GetLength() - i >= kRemainingLenWiggleRoom))
			{
				if (safetyCatch < 100 && 0 < remaining_line_len && remaining_line_len < max_line_len &&
				    (int)it->_txt.GetLength() > remaining_line_len)
				{
					const int checkStart = (i == NPOS) ? 0 : i;
					int nextWwCh = my_find_last_of(it->_txt, checkStart, it->_txt.GetLength() - checkStart, kWrapChars);
					const int nextWwCh2 =
					    my_find_last_of(it->_txt, checkStart, it->_txt.GetLength() - checkStart, L",");
					if (NPOS != nextWwCh2)
					{
						// [case: 72113] give priority to comma over kWrapChars
						if (NPOS == nextWwCh)
							nextWwCh = nextWwCh2;
						else if (nextWwCh2 > nextWwCh)
							nextWwCh = nextWwCh2;
						else if (nextWwCh - nextWwCh2 < kRemainingLenWiggleRoom)
							nextWwCh = nextWwCh2;

						if (nextWwCh == nextWwCh2)
						{
							// this means we are breaking on a comma
							// now check for whitespace abutting the comma and take it also
							int len = it->_txt.GetLength();
							if (nextWwCh + 1 < len && ::wt_isspace(it->_txt[nextWwCh + 1]))
								++nextWwCh;
						}
					}

					if (nextWwCh && NPOS != nextWwCh && nextWwCh < max_line_len)
					{
						// [case: 64486] try to prevent line length based text clipping if next line can be clipped
						remaining_line_len = 0;
					}
				}

				// clip at max_line_len-th character if word-wrap character cannot be found
				if (i > 1 || i != i2 + 1)
					i = remaining_line_len;
			}
			else if (i < remaining_line_len)
			{
				// include word-wrap character in first line
				i++;
			}
			remaining_line_len = max_line_len;

			lines.insert(it, FormattedTextLine(it->_txt.Left(i),
			                                   line_flags((it->_flags & ~line_no_new_line) | line_wrapped),
			                                   it->_fg_rgba, it->_bg_rgba, it->_custom));

			// mark leaf line as wrapped
			it->_flags = line_flags(it->_flags | line_wrapped);

			// i chars moved from it to new item just inserted above it
			CStringW curln(it->_txt);
			// remove i chars
			curln = curln.Mid(i);
			// before inserting wrap prefix string, remove possible leading
			// whitespace left after truncation
			curln.TrimLeft();

			CStringW prefix;
			if (it != lines.begin())
			{
				auto prevIt = it;
				--prevIt;
				CStringW prevLn = prevIt->_txt;
				if (prevLn.GetLength() > 1 && prevLn[0] == '/' && (prevLn[1] == '/' || prevLn[1] == '*'))
				{
					// we've wrapped a comment.  prepend comment start so that coloring works.
					prefix = prevLn.Left(2);
					prefix += " ";
				}
			}

			if (!prefix.GetLength())
				prefix = wrap_indentation;
			it->_txt = prefix + curln;
			// rewind due to insert so that post-loop increment works
			--it;
		}

		// tack on [1 of n] if needed
		if (m_totalDefs > 1)
		{
			char buf[64];
			sprintf(buf, "[%d of %d] ", m_currentDef, m_totalDefs);
			lines.push_front({buf, line_flags(line_bold | line_no_new_line)});
		}

		// draw tooltip!
		// gmit: this is intentionally left in the code. It hasn't fixed change 13324 (case 25244), but this should be
		// the proper way of handling tooltip drawing
		//		if(!in_paint)
		// sean: added condition to fix param info flicker when typing
		if (!g_currentEdCnt || g_currentEdCnt->m_ttParamInfo != this)
			ShowWindow(SW_HIDE);
		int x = VsUI::DpiHelper::LogicalToDeviceUnitsX(4), y = VsUI::DpiHelper::LogicalToDeviceUnitsY(2), w = 0, h = 0;
		if (m_ourTip && Psettings->mUseTomatoBackground)
		{
#ifdef _WIN64
			int tomtoImageId = ICONIDX_TOMATO;
#else
			int tomtoImageId = ICONIDX_TOMATO_BACKGROUND;
#endif // _WIN64

			// Draw a pale tomato backsplash to distinguish between their and our suggestions.
			CImageList* pIl = gImgListMgr->GetImgList(ImageListManager::bgTooltip);
			if (pIl)
				pIl->Draw(&memdc, tomtoImageId,
				          CPoint(VsUI::DpiHelper::LogicalToDeviceUnitsX(2), VsUI::DpiHelper::LogicalToDeviceUnitsY(3)),
				          ILD_TRANSPARENT); // do not blend icons, makes VS.NET2005 icons look bad
			x = VsUI::DpiHelper::LogicalToDeviceUnitsX(20);
		}

		const BOOL vsNetTip =
		    (g_currentEdCnt && (g_currentEdCnt->m_ftype == CS || Is_VB_VBS_File(g_currentEdCnt->m_ftype)));

		for (auto it = lines.begin(); it != lines.end(); ++it)
		{
			if (it->_flags & line_bold)
			{
				memdc.SelectObject(mFontB);
			}
			else if (it->_flags & line_underlined)
			{
				memdc.SelectObject(mFontU);
			}
			else
			{
				memdc.SelectObject(mFont);
			}

			const CStringW& line = it->_txt;
			/* gmit: for debug purposes; to easily distinguish between VA and VS tooltips
			CBrush br;
			br.CreateSolidBrush(RGB(255, 0, 0));
			memdc.FillRect(CRect(0, 0, 1000, 100), &br);*/
			BOOL doColor = !(it->_flags & line_dont_colour) && !master_do_not_colour_switch;
			// Color members lists tips according to file type: case 15117
			// This assumes that all ArgToolTip's in CS/VB files come from VS. (Most do, but not all)
			VAColorPaintMessages pw(doColor ? (vsNetTip ? PaintType::VS_ToolTip : PaintType::ToolTip)
			                                : PaintType::DontColor); // Tooltip?
			TextOutW(memdc.m_hDC, x, y, line, line.GetLength());

			CSize csz;
			//			csz = memdc.GetTextExtent(line);
			GetTextExtentPoint32W(memdc, line, line.GetLength(), &csz);

			w = max(w, (int)(x + csz.cx));
			h = min(max(h, (int)(y + csz.cy + VsUI::DpiHelper::LogicalToDeviceUnitsY(2))),
			        maxh - VsUI::DpiHelper::LogicalToDeviceUnitsY(16));

			if (it->_flags & line_no_new_line)
			{
				x += csz.cx;
			}
			else
			{
				y = h;
				x = VsUI::DpiHelper::LogicalToDeviceUnitsX(4);
			}
		}

		if (!master_do_not_colour_switch && gShellAttr->IsDevenv10OrHigher() && !vsNetTip)
		{
			// [case: 45866]
			extern void ClearTextOutCacheComment();
			ClearTextOutCacheComment();
		}

		w += VsUI::DpiHelper::LogicalToDeviceUnitsX(3);
		h += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);

		int orig_m_pt_y = m_pt.y;
		// make sure tooltip does not obstruct refactoring button (before desktop rect check)
		if (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->GetSafeHwnd() &&
		    ::IsWindow(g_ScreenAttrs.m_VATomatoTip->m_hWnd) && g_ScreenAttrs.m_VATomatoTip->IsWindowVisible())
		{
			CRect intersection;
			SIZE szTmp = {w, h};
			CRect tooltipRc(m_pt, szTmp);
			CRect refactorButtonRc;
			g_ScreenAttrs.m_VATomatoTip->GetWindowRect(&refactorButtonRc);
			if (intersection.IntersectRect(tooltipRc, refactorButtonRc) && !intersection.IsRectEmpty())
			{
				m_pt.x = refactorButtonRc.right + 1;
				m_pt.y = refactorButtonRc.bottom + 1;
			}
		}

		// limit to desktop rect
		bool do_avoid_hwnd = avoid_hwnd && ::IsWindow(avoid_hwnd);
		const CRect desktoprect = g_FontSettings->GetDesktopEdges(g_currentEdCnt.get());
		if ((m_keeponscreen || do_avoid_hwnd) && ((m_pt.x + w) > desktoprect.right))
		{
			if (do_avoid_hwnd)
			{
				CRect avoided_rect;
				::GetWindowRect(avoid_hwnd, avoided_rect);
				int new_x = avoided_rect.left - w;
				if (new_x < desktoprect.left)
					new_x = avoided_rect
					            .right; // prefer showing tooltip on the right if it goes out of the screen on the left
				m_pt.x = new_x;
			}
			else
				m_pt.x = desktoprect.right - w - VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		}
		if (m_pt.x < desktoprect.left)
			m_pt.x = desktoprect.left; // keep on left edge of screen
		bool movedAboveline = false;
		if (m_keeponscreen && ((m_pt.y + h) > desktoprect.bottom))
		{
			m_pt.y = orig_m_pt_y - g_FontSettings->GetCharHeight() - h - VsUI::DpiHelper::LogicalToDeviceUnitsY(10);
			movedAboveline = true;
		}
		if (m_argInfo && g_CompletionSet && g_CompletionSet->IsExpUp(NULL))
		{
			// this is param info
			// Don't display param info over completionsets. case=31814
			bool listboxCollision = false;
			CRect rexp;
			g_CompletionSet->m_expBox->GetWindowRect(&rexp);
			if (((m_pt.y + h) > rexp.top && (m_pt.y + h) <= rexp.bottom) ||
			    (m_pt.y >= rexp.top && m_pt.y <= rexp.bottom))
			{
				listboxCollision = true;
			}

			bool checkForListboxTooltipCollision = false;
			if (g_CompletionSet->m_expBox->m_tooltip->GetSafeHwnd() &&
			    g_CompletionSet->m_expBox->m_tooltip->IsWindowVisible())
			{
				checkForListboxTooltipCollision = true;
			}

			if (listboxCollision && (movedAboveline || rexp.top < orig_m_pt_y))
			{
				// move param info above listbox
				m_pt.y = rexp.top - h;
			}
			else if (listboxCollision || checkForListboxTooltipCollision)
			{
				bool checkBottomEdgeAgain = false;
				if (listboxCollision)
				{
					// move param info below listbox
					m_pt.y = rexp.bottom + 1;
					checkBottomEdgeAgain = true;
				}

				if (checkForListboxTooltipCollision)
				{
					CRect compBoxTtRc;
					g_CompletionSet->m_expBox->m_tooltip->GetWindowRect(compBoxTtRc);
					const CRect thisRc(m_pt, CSize(w, h));
					CRect intersection;
					intersection.IntersectRect(thisRc, compBoxTtRc);
					if (!intersection.IsRectEmpty())
					{
						// there was a collision; move paramInfo further down
						// prevents flicker when paramInfo fights with completion box tooltip
						m_pt.y += intersection.Height() + 1;
						checkBottomEdgeAgain = true;
					}
				}

				if (checkBottomEdgeAgain && m_keeponscreen && ((m_pt.y + h) > desktoprect.bottom))
				{
					// nope, move param info above caret line
					m_pt.y = rexp.top - g_FontSettings->GetCharHeight() - h - VsUI::DpiHelper::LogicalToDeviceUnitsY(5);
				}
			}
		}
		else if (!m_argInfo && g_currentEdCnt && g_currentEdCnt->m_ttParamInfo &&
		         g_currentEdCnt->m_ttParamInfo != this && g_currentEdCnt->m_ttParamInfo->GetSafeHwnd() &&
		         g_currentEdCnt->m_ttParamInfo->IsWindowVisible() && g_CompletionSet && g_CompletionSet->m_expBox &&
		         g_CompletionSet->m_expBox->m_tooltip == this)
		{
			// this is a tooltip for the completion box
			// watch out for flicker with paramInfo
			CRect paramInfoRc;
			g_currentEdCnt->m_ttParamInfo->GetWindowRect(paramInfoRc);
			const CRect thisRc(m_pt, CSize(w, h));
			CRect intersection;
			intersection.IntersectRect(paramInfoRc, thisRc);
			if (!intersection.IsRectEmpty())
			{
				// [case: 31814] flicker when paramInfo fights with completion box
				// push param info further down
				paramInfoRc.OffsetRect(0, intersection.Height() + 1);
				g_currentEdCnt->m_ttParamInfo->MoveWindow(paramInfoRc);
			}
		}

		bool layered = !!(::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED);
		BYTE orig_alpha = 255;
		DWORD orig_flags = LWA_ALPHA;
		if (layered)
		{
			::GetLayeredWindowAttributes(m_hWnd, NULL, &orig_alpha, &orig_flags);
			::SetLayeredWindowAttributes(m_hWnd, NULL, 1,
			                             LWA_ALPHA); // gmit: this will prevent showing stale bitmap between ShowWindow
			                                         // and BitBlt calls (case 65178)
		}

		MoveWindow(m_pt.x, m_pt.y, w, h);
		ModifyStyleEx(0, WS_EX_TOPMOST);
		if (in_paint)
			ShowWindow(SW_SHOWNOACTIVATE);

		// display memdc
		dc->BitBlt(0, 0, w, h, &memdc, 0, 0, SRCCOPY);

		if (layered)
			::SetLayeredWindowAttributes(m_hWnd, NULL, orig_alpha, orig_flags);

		if (mColorOverride)
		{
			_ASSERTE(!m_reverseColors);
		}

		if (useTheme)
			ValidateRect(crect);
		else
		{
			const COLORREF borderColor = mColorOverride ? mBorderOverride : m_reverseColors ? bkColor : txtColor;
			CPenDC pen(*dc, borderColor);
			CBrushDC br(*dc);
			dc->Rectangle(0, 0, w, h);
			ValidateRect(CRect(0, 0, w, h));
		}

		memdc.DeleteDC();
		ReleaseDC(dc);

		CRect finalRc;
		GetWindowRect(&finalRc);
		FromHandle(m_hPar)->ScreenToClient(&finalRc);
		// Validate edit rect so that our paint, which gets called from
		//  EdCnt::OnPaint doesn't cause TER to repaint
		::ValidateRect(FromHandle(m_hPar)->m_hWnd, &finalRc);

		if (!in_paint)
		{
			// [case: 65178] reduce flicker by making recursive call if not called during paint
			OnPaint();
		}

		// gmit: this is intentionally left in the code. It hasn't fixed case 13324, but this should be the proper way
		// of handling tooltip drawing 		if(!in_paint) 			InvalidateRect(NULL, true);
	}
	catch (...)
	{
		VALOGEXCEPTION("ATT");
		// someone deleted m_ttSuggestWord
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTip::Layout");
		}
	}
	// No longer needed, sice we prevent this above case=31814
	// 	if(m_argInfo && g_CompletionSet->IsExpUp(NULL) && !g_CompletionSet->IsAboveCaret())
	// 	{
	// 		// case 20522: Don't display arginfo over completion lists, move list above caret.
	// 		g_CompletionSet->CalculatePlacement();
	// 		g_CompletionSet->DisplayList(TRUE);
	// 	}
	SetTimer(TT_TIMER, 500, NULL);

	return;
}

void ArgToolTip::OnPaint()
{
	Layout(true);
}

void ArgToolTip::OnNcDestroy()
{
	if (this == g_pLastToolTip)
		g_pLastToolTip = NULL;
	CWnd::OnNcDestroy();
	OnRepaintBeneath();
}

LRESULT ArgToolTip::OnVASelfDelete(WPARAM wparam /*= 0*/, LPARAM lparam /*= 0*/)
{
	delete this;
	return TRUE;
}

LRESULT ArgToolTip::OnRepaintBeneath(WPARAM wparam, LPARAM lparam)
{
	//	if(m_ed && !m_rc.IsRectNull() && m_ed->m_hWnd) {
	//		::InvalidateRect(m_ed->m_hWnd, &m_rc, TRUE);
	//		::UpdateWindow(m_ed->m_hWnd);
	//	}

	if (MainWndH && ::IsWindow(MainWndH) && m_hWnd && ::IsWindow(m_hWnd))
	{
		RECT rect;
		GetWindowRect(&rect);
		::MapWindowPoints(NULL, MainWndH, (LPPOINT)&rect, 2);
		::RedrawWindow(MainWndH, &rect, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	}

	return 0;
}

void ArgToolTip::OverrideColor(bool en, COLORREF fg, COLORREF bg, COLORREF brdr)
{
	mColorOverride = en;
	mFgOverride = fg;
	mBgOverride = bg;
	mBorderOverride = brdr;
	UseTheme(false);
}

bool ArgToolTip::UseTheme(bool use)
{
	if (th->AreThemesAvailable())
	{
		if (!use || !gMainWnd)
		{
			if (theme)
			{
				::CloseThemeData(theme);
				theme = NULL;
			}
			return false;
		}
		else
		{
			if (!theme)
				theme = ::OpenThemeData(gMainWnd->m_hWnd, L"Tooltip");
			if (theme)
			{
				COLORREF color = 0;
				if (S_OK != ::GetThemeColor(theme, TTP_STANDARD, 0 /*TTSS_NORMAL*/, TMT_TEXTCOLOR, &color))
					color = ::GetSysColor(COLOR_INFOTEXT);
				mFgOverride = color;
				mColorOverride = true;
			}
			return !!theme;
		}
	}
	else
		return false;
}

void ArgToolTip::UseVsEditorTheme()
{
	if (gShellAttr->IsDevenv12OrHigher() && g_IdeSettings)
	{
		// [case: 74773]
		COLORREF txtFg(g_IdeSettings->GetToolTipColor(true));
		COLORREF txtBg(g_IdeSettings->GetToolTipColor(false));
		COLORREF border(g_IdeSettings->GetEnvironmentColor(L"ToolTipBorder", FALSE));

		// [case: 65870]
		OverrideColor(true, txtFg, txtBg, border);
	}
	else if (gShellAttr->IsDevenv11OrHigher() && g_IdeSettings)
	{
		COLORREF txtFg(g_IdeSettings->GetToolTipColor(true));
		COLORREF txtBg(g_IdeSettings->GetToolTipColor(false));
		COLORREF border;

		if (UINT_MAX == txtFg || UINT_MAX == txtBg)
		{
			txtFg = g_IdeSettings->GetEnvironmentColor(L"ToolWindowText", false);
			if ((txtFg & 0xffffff) == 0xf1f1f1)
				txtFg = (txtFg & 0xff000000) | 0x999999; // gmit: temporary workaround for 66900
			txtBg = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuBackgroundGradientBegin", FALSE);
			border = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuBorder", FALSE);
		}
		else
		{
			border = txtBg + 0x111111;
		}

		// [case: 65870]
		OverrideColor(true, txtFg, txtBg, border);
	}
}

bool ParseFormattedTextMarkup(const CStringW& txt, FormattedTextLines& lines)
{
	lines.push_back(FormattedTextLine("", line_flags_none));

	bool multi_mode = false;

	auto back_is_empty = [&lines]() {
		auto& last = lines.back();
		return (last._flags == line_flags_none || last._flags == line_no_new_line) && last._txt.IsEmpty() &&
		       last._custom == 0 && last._fg_rgba == 0 && last._bg_rgba == 0;
	};

	auto apply_flags = [&lines, &multi_mode, back_is_empty](line_flags flags) {
		if ((!multi_mode || flags == line_flags_none) && !back_is_empty())
		{
			lines.back()._flags = line_flags(lines.back()._flags | line_no_new_line);
			lines.push_back(FormattedTextLine("", flags));
		}
		else
		{
			lines.back()._flags = line_flags(lines.back()._flags | flags);
		}
	};

	auto parse_hex = [](LPCWSTR wstr) -> BYTE {
		WCHAR buff[3] = {wstr[0], wstr[1], 0};
		if (*buff)
			return (BYTE)wcstoul(buff, nullptr, 16);
		return 0;
	};

	auto parse_RGB = [parse_hex](LPCWSTR wstr) -> DWORD {
		BYTE r, g, b;
		r = parse_hex(wstr);
		g = parse_hex(wstr + 2);
		b = parse_hex(wstr + 4);
		return RGB(r, g, b) | 0xFF000000;
	};

	auto parse_RGBA = [parse_hex](LPCWSTR wstr) -> DWORD {
		BYTE r, g, b, a;
		r = parse_hex(wstr);
		g = parse_hex(wstr + 2);
		b = parse_hex(wstr + 4);
		a = parse_hex(wstr + 6);
		return RGB(r, g, b) | (((DWORD)a) << 24);
	};

	LPCWSTR wstr = txt;

	// split tooltip in lines
	for (int i = 0; i < txt.GetLength(); i++)
	{
		switch (txt[i])
		{
		case '\n':
		case '\r':
			if (i && ((txt[i - 1] == '\n') || (txt[i - 1] == '\r')) && (txt[i - 1] != txt[i]))
				continue; // handle \n\r or \r\n as a single line break
			multi_mode = false;
			lines.push_back(FormattedTextLine("", line_flags_none));
			break;
		default:
			lines.back()._txt.AppendChar(txt[i]);
			break;
		case FTM_BOLD: // bold text
			apply_flags(line_bold);
			break;
		case FTM_NORMAL: // normal text
			multi_mode = false;
			apply_flags(line_flags_none);
			break;
		case FTM_UNDERLINE: // underlined text
			apply_flags(line_underlined);
			break;
		case FTM_DIM: // dimmed text
			apply_flags(line_dim);
			break;
		case FTM_ITALIC: // italic text
			apply_flags(line_italic);
			break;
		case FTM_EX:
			if (++i < txt.GetLength())
			{
				switch (txt[i])
				{
				case 'M': // multi-style mode
					multi_mode = true;
					apply_flags(line_flags_none);
					break;
				case 'D': // don't apply syntax highlighting
					apply_flags(line_dont_colour);
					break;
				case 'F': // RGBA foreground
				{
					if (i + 8 < txt.GetLength())
						lines.back()._fg_rgba = parse_RGBA(&wstr[i + 1]);
					i += 8;
					break;
				}
				case 'f': // RGB foreground
				{
					if (i + 6 < txt.GetLength())
						lines.back()._fg_rgba = parse_RGB(&wstr[i + 1]);
					i += 6;
					break;
				}
				case 'B': {
					if (i + 8 < txt.GetLength())
						lines.back()._bg_rgba = parse_RGBA(&wstr[i + 1]);
					i += 8;
					break;
				}
				case 'b': {
					if (i + 6 < txt.GetLength())
						lines.back()._bg_rgba = parse_RGB(&wstr[i + 1]);
					i += 6;
					break;
				}
				case 'R': {
					if (i + 2 < txt.GetLength())
						lines.back()._custom = (BYTE)FtmID::FromChar((CHAR)txt[i + 1]);
					i += 1;
					break;
				}
				}
			}
		}
	}

	return true;
}

CStringW RemoveFormattedTextMarkup(const CStringW& txt)
{
	FormattedTextLines lines;
	if (ParseFormattedTextMarkup(txt, lines))
	{
		CStringW outStr;
		for (auto& ftl : lines)
			outStr.Append(ftl._txt);
		return outStr;
	}
	return txt;
}

CHAR FtmID::ToChar(int id)
{
	_ASSERTE(id >= 1 && id <= 61);

	if (id >= 1 && id <= 61)
	{
		if (id <= 9)
			return (CHAR)('1' + (id - 1));
		else if (id <= 35)
			return (CHAR)('A' + (id - 1));
		else if (id <= 61)
			return (CHAR)('a' + (id - 1));
	}

	return 0;
}

int FtmID::FromChar(CHAR ch)
{
	if (ch >= '1' && ch <= '9')
		return ch - '1' + 1;
	else if (ch >= 'A' && ch <= 'Z')
		return ch - 'A' + 10;
	else if (ch >= 'a' && ch <= 'z')
		return ch - 'a' + 36;

	_ASSERTE(!"Unsupported char");

	return 0;
}
