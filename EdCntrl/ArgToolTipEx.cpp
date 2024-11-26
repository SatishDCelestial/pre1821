// ArgToolTipEx.cpp : implementation file
//
#include "stdafxed.h"
#include "EdCnt.h"
#include "ArgToolTipEx.h"
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
#include "Expansion.h"
#include "VACompletionSet.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const UINT_PTR TT_TIMER = ::RegisterTimer("TooltipExTimer");
static const UINT_PTR TT_XY_TRACKER = ::RegisterTimer("TooltipExPosTrackTimer");

using OWL::string;
using OWL::TRegexp;

// unused #define MOUSETIMER 9741
extern WTString VA_Snippet_Edit_suffix;
static std::unique_ptr<CVAThemeHelper> th;

ArgToolTipEx::ArgToolTipEx(EdCnt* par)
    : ArgToolTip(par), mDuration(0), mMinDuration(0), mNumTimerTicks(0), mMargin(3, 2, 3, 2), mIgnoreHide(false),
      mIsTrackingPos(false)
{
	m_hWnd = NULL;
	m_currentDef = m_totalDefs = 0;
	dont_close_on_ctrl_key = false;
	avoid_hwnd = NULL;
	theme = NULL;
	if (!th)
		th.reset(new CVAThemeHelper);
}

ArgToolTipEx::ArgToolTipEx(HWND hParent)
    : ArgToolTip(hParent), mDuration(0), mMinDuration(0), mNumTimerTicks(0), mMargin(3, 2, 3, 2), mIgnoreHide(false),
      mIsTrackingPos(false)
{
	m_hWnd = NULL;
	m_currentDef = m_totalDefs = 0;
	dont_close_on_ctrl_key = false;
	avoid_hwnd = NULL;
	theme = NULL;
	if (!th)
		th.reset(new CVAThemeHelper);
}

ArgToolTipEx::~ArgToolTipEx()
{
}

#define RINDENT 30
void ArgToolTipEx::DisplayWstr(CPoint* pt, const CStringW& left, const CStringW& cur, const CStringW& right,
                               int totalDefs, int currentDef, bool reverseColor /*= false*/,
                               bool keeponscreen /*= true*/, BOOL color /*= TRUE*/, BOOL ourTip /*= TRUE*/)
{
	try
	{
		if (::GetFocus() == NULL)
			return;
		{
			WTString leftA = left;
			WTString curA = cur;
			WTString rightA = right;

			vCatLog("Editor.Events", "VaEventTT   Display l='%s', c='%s', r='%s', n=%d, clr=%d, ours=%d", leftA.c_str(), curA.c_str(),
			     rightA.c_str(), totalDefs, color, ourTip);
		}

		if (g_pLastToolTip && g_pLastToolTip != this) // catch stray tooltip
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
			static const WTString ArgToolTipExWndCls(AfxRegisterWndClass(0, LoadCursor(NULL, IDC_ARROW)));
			CreateEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE, ArgToolTipExWndCls.c_str(), _T(""), WS_POPUP, r,
			         FromHandle(m_hPar), 0);
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
		if (m_cstr.GetLength() || m_rstr.GetLength() || m_lstr.GetLength())
		{
			if (gTestLogger && gTestLogger->IsArgTooltipLoggingEnabled())
			{
				CStringW msg;
				CString__FormatW(msg, L"ArgToolTipEx: %d, %d of %d, \"%s\" \"%s\" \"%s\"", ourTip, currentDef,
				                 totalDefs, (LPCWSTR)left, (LPCWSTR)cur, (LPCWSTR)right);
				gTestLogger->LogStr(msg);
			}
			mLines.clear();
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
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTipEx::Display");
		}
	}
}

void ArgToolTipEx::DisplayWstrDirect(CPoint* pt, const CStringW& left, const CStringW& cur, const CStringW& right,
                                     int totalDefs, int currentDef, bool reverseColor /*= false*/,
                                     bool keeponscreen /*= true*/, BOOL color /*= TRUE*/, BOOL ourTip /*= TRUE*/)
{
	try
	{
		if (::GetFocus() == NULL)
			return;
		{
			WTString leftA = left;
			WTString curA = cur;
			WTString rightA = right;

			vCatLog("Editor.Events", "VaEventTT   Display l='%s', c='%s', r='%s', n=%d, clr=%d, ours=%d", leftA.c_str(), curA.c_str(),
			     rightA.c_str(), totalDefs, color, ourTip);
		}

		if (g_pLastToolTip && g_pLastToolTip != this) // catch stray tooltip
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
			static const WTString ArgToolTipExWndCls = AfxRegisterWndClass(0, LoadCursor(NULL, IDC_ARROW));
			CreateEx(WS_EX_TOPMOST, ArgToolTipExWndCls.c_str(), _T(""), WS_POPUP, r, FromHandle(m_hPar), 0);
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
		m_cstr = cur;
		m_rstr = right;
		m_pt = *pt;
		m_lstr = left;
		m_reverseColors = reverseColor;
		if (m_cstr.GetLength() || m_rstr.GetLength() || m_lstr.GetLength())
		{
			if (gTestLogger && gTestLogger->IsArgTooltipLoggingEnabled())
			{
				CStringW msg;
				CString__FormatW(msg, L"ArgToolTipEx: %d, %d of %d, \"%s\" \"%s\" \"%s\"", ourTip, currentDef,
				                 totalDefs, (LPCWSTR)left, (LPCWSTR)cur, (LPCWSTR)right);
				gTestLogger->LogStr(msg);
			}
			mLines.clear();
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
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTipEx::Display");
		}
	}
}

void ArgToolTipEx::Display(CPoint* pt, LPCWSTR left, LPCWSTR cur, LPCWSTR right, int totalDefs, int currentDef,
                           bool reverseColor /*= false*/, bool keeponscreen /*= true*/, BOOL color /*= TRUE*/,
                           BOOL ourTip /*= TRUE*/)
{
	DisplayWstr(pt, left, cur, right, totalDefs, currentDef, reverseColor, keeponscreen, color, ourTip);
}

void ArgToolTipEx::Layout(bool in_paint)
{
	try
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);
		UpdateFonts();

		// finds last occurrence of any of listed chars within a substring
		/*static*/ auto FindLastOf = [](const wchar_t* str, int start, int size, const wchar_t* chars) -> int {
			while (size-- > 0)
			{
				if (wcschr(chars, str[start + size]))
					return start + size;
			}
			return NPOS;
		};

		// return true if given path looks like directory
		/*static*/ auto IsPathDirectory = [](const CStringW& path) -> BOOL {
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
		};

		//		if (::GetFocus() != m_ed->m_hWnd) {
		//			ShowWindow(SW_HIDE);
		//			return;
		//		}
		CDC* dc = GetDC();
		CDC memdc;
		CBitmap bmp;
		// create memdc & create storage bitmap
		memdc.CreateCompatibleDC(dc);
		int maxh = max(1600, ::GetSystemMetrics(SM_CYFULLSCREEN)), maxw = g_FontSettings->m_tooltipWidth + 100;

		if (mSmartSelectMode)
		{
			EdCntPtr ed(g_currentEdCnt);
			CRect wrc, crc;
			ed->GetWindowRect(wrc);
			ed->vGetClientRect(crc);
			ed->vClientToScreen(crc);
			crc.left = wrc.left;
			maxh = crc.Height(); // - VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.top + mMargin.bottom);
			maxw = crc.Width();  // - VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.left + mMargin.right);
		}

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

		memdc.SetBkMode(TRANSPARENT);
		memdc.SelectObject(mFont);
		memdc.SetBkColor(bkColor);

		// prepare tooltip text
		CStringW _txt = m_lstr.Wide();
		CStringW _VASE_Suffix_W = VA_Snippet_Edit_suffix.Wide();
		if (m_cstr.GetLength())
		{
			_txt += (WCHAR)FTM_BOLD + m_cstr.Wide() + (WCHAR)FTM_NORMAL; // bold current argument
		}
		_txt += m_rstr.Wide();
		if (_txt.Find(_VASE_Suffix_W) >= 0)
		{
			_txt.Replace(_VASE_Suffix_W, _VASE_Suffix_W.Left(2) + (WCHAR)FTM_UNDERLINE +
			                                 _VASE_Suffix_W.Mid(2, _VASE_Suffix_W.GetLength() - 4) + (WCHAR)FTM_NORMAL +
			                                 _VASE_Suffix_W.Right(2));
		}

		bool master_do_not_colour_switch = false;
		if (!(Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorTooltips))
		{
			master_do_not_colour_switch = true;
		}
		else if (myGetProp(m_hWnd, "__VA_do_not_colour"))
		{
			master_do_not_colour_switch = true;
		}
		else if (_txt.Find(_VASE_Suffix_W) >= 0)
		{
			master_do_not_colour_switch = true;
		}
		else
		{
			CStringW Wide_TT_Text(TT_TEXT);
			if (!wcsncmp(_txt, Wide_TT_Text, 30))
				master_do_not_colour_switch = true;
		}

		FormattedTextLines& lines = mLines;

		if (lines.empty())
		{
			ParseFormattedTextMarkup(_txt, lines);

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
			if (!mSmartSelectMode)
			{
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

					int i = FindLastOf(it->_txt, 0, remaining_line_len, kWrapChars);
					const int i2 = FindLastOf(it->_txt, 0, remaining_line_len, L",");
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
							int nextWwCh =
							    FindLastOf(it->_txt, checkStart, it->_txt.GetLength() - checkStart, kWrapChars);
							const int nextWwCh2 =
							    FindLastOf(it->_txt, checkStart, it->_txt.GetLength() - checkStart, L",");
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
								// [case: 64486] try to prevent line length based text clipping if next line can be
								// clipped
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
			}

			// tack on [1 of n] if needed
			if (m_totalDefs > 1)
			{
				char buf[64];
				sprintf(buf, "[%d of %d] ", m_currentDef, m_totalDefs);
				lines.push_front({buf, line_flags(line_bold | line_no_new_line)});
			}
		}

		// draw tooltip!
		// gmit: this is intentionally left in the code. It hasn't fixed change 13324 (case 25244), but this should be
		// the proper way of handling tooltip drawing
		//		if(!in_paint)
		// sean: added condition to fix param info flicker when typing
		if (!g_currentEdCnt || g_currentEdCnt->m_ttParamInfo != this)
			ShowWindow(SW_HIDE);
		int x = VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.left),
		    y = VsUI::DpiHelper::LogicalToDeviceUnitsY(mMargin.top), w = 0, h = 0;
		if (m_ourTip && Psettings->mUseTomatoBackground)
		{
			// Draw a pale tomato backsplash to distinguish between their and our suggestions.
			CImageList* pIl = gImgListMgr->GetImgList(ImageListManager::bgTooltip);
#ifdef _WIN64
			int tomtoImageId = ICONIDX_TOMATO;
#else
			int tomtoImageId = ICONIDX_TOMATO_BACKGROUND;
#endif // _WIN64

			if (pIl)
				pIl->Draw(&memdc, tomtoImageId,
				          CPoint(VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.left),
				                 VsUI::DpiHelper::LogicalToDeviceUnitsY(mMargin.top)),
				          ILD_TRANSPARENT); // do not blend icons, makes VS.NET2005 icons look bad
			x += VsUI::DpiHelper::LogicalToDeviceUnitsX(18);
		}

		const BOOL vsNetTip =
		    (g_currentEdCnt && (g_currentEdCnt->m_ftype == CS || Is_VB_VBS_File(g_currentEdCnt->m_ftype)));

		if (preprocess_textlines)
			preprocess_textlines(lines);

		CRect dest_rect;
		CRect rslt_rect;

		FormatRenderer::DrawingContext dctx;
		dctx.dc = &memdc;
		dctx.textFlags = 0; // not used
		dctx.dstRect = &dest_rect;
		dctx.rsltRect = &rslt_rect;

		for (auto it = lines.begin(); it != lines.end(); ++it)
		{
			// ensures that each item starts with defaults
			ThemeUtils::AutoSaveRestoreDC restore_dc(memdc);

			memdc.SelectObject((HFONT)mFontOverride ? mFontOverride : mFont);

			CFormatDC fmt(memdc, (it->_flags & line_bold) != 0, (it->_flags & line_italic) != 0,
			              (it->_flags & line_underlined) != 0);

			CSize csz;
			GetTextExtentPoint32W(memdc, it->_txt, it->_txt.GetLength(), &csz);
			csz.cy += 1; // one pixel space between lines (line under underscore)

			w = max(w, (int)(x + csz.cx));
			h = min(max(h, (int)(y + csz.cy + VsUI::DpiHelper::LogicalToDeviceUnitsY(mMargin.top))),
			        maxh - VsUI::DpiHelper::LogicalToDeviceUnitsY(16));

			it->_rc.left = x;
			it->_rc.top = y;
			it->_rc.right = x + csz.cx;
			it->_rc.bottom = y + csz.cy;

			rslt_rect = it->_rc;

			dest_rect.left = x;
			dest_rect.top = y;
			dest_rect.right = w;
			dest_rect.bottom = h;

			dctx.draw_default_bg = [&](const FormattedTextLine* itemP) {
				if (itemP->_bg_rgba)
				{
					DWORD alpha_mask = itemP->_bg_rgba & 0xFF000000;
					if (alpha_mask) // do nothing if 0
					{
						if (alpha_mask == 0xFF000000)
							memdc.FillSolidRect(&itemP->_rc, itemP->_bg_rgba & 0x00FFFFFF);
						else if (alpha_mask != 0)
							ThemeUtils::FillRectAlpha(memdc, &itemP->_rc, itemP->_bg_rgba & 0x00FFFFFF,
							                          (LOBYTE((itemP->_bg_rgba) >> 24)));
					}
				}
			};

			dctx.draw_default_fg = [&](const FormattedTextLine* itemP) {
				// handle the Foreground color (text color)
				if (itemP->_fg_rgba)
				{
					DWORD alpha_mask = itemP->_fg_rgba & 0xFF000000;

					if (alpha_mask == 0)
						return; // transparent text
					else if (alpha_mask == 0xFF000000)
						memdc.SetTextColor(itemP->_fg_rgba & 0x00FFFFFF);
					else
					{
						// get current background color in the center of the item
						COLORREF bg_color = memdc.GetPixel(itemP->_rc.CenterPoint());

						// convert alpha byte to opacity in 0 to 1 range
						float fg_opacity = (float)LOBYTE((itemP->_fg_rgba) >> 24) / 255.0f;

						// combine colors with opacity to get resulting color
						COLORREF txt_color =
						    ThemeUtils::InterpolateColor(bg_color, itemP->_fg_rgba & 0x00FFFFFF, fg_opacity);

						// apply to Text color
						memdc.SetTextColor(txt_color);
					}
				}

				const CStringW& line = itemP->_txt;

				/* gmit: for debug purposes; to easily distinguish between VA and VS tooltips
				CBrush br;
				br.CreateSolidBrush(RGB(255, 0, 0));
				memdc.FillRect(CRect(0, 0, 1000, 100), &br);*/

				CDimDC dim(memdc, (itemP->_flags & line_dim) != 0);

				BOOL doColor = itemP->_fg_rgba == 0 && !(itemP->_flags & line_dont_colour) &&
				               !(itemP->_flags & line_dim) && !master_do_not_colour_switch;
				// Color members lists tips according to file type: case 15117
				// This assumes that all ArgToolTipEx's in CS/VB files come from VS. (Most do, but not all)
				int paint_type =
				    doColor ? (vsNetTip ? PaintType::VS_ToolTip : PaintType::ToolTip) : PaintType::DontColor;

				if (mSmartSelectMode && doColor)
					paint_type = PaintType::ToolTip;

				VAColorPaintMessages pw(paint_type); // Tooltip?

				TextOutW(memdc.m_hDC, itemP->_rc.left, itemP->_rc.top, line, line.GetLength());

				CSize csz;
				GetTextExtentPoint32W(memdc, line, line.GetLength(), &csz);
				dctx.rsltRect->bottom = itemP->_rc.top + csz.cy + 1;
				dctx.rsltRect->right = itemP->_rc.left + csz.cx;
			};

			FormatRenderer* fmt_custom = nullptr;

			auto found = renderers.find(it->_custom);
			if (found != renderers.end())
			{
				fmt_custom = &found->second;
			}

			if (fmt_custom && fmt_custom->render)
			{
				dctx.fmt = &(*it);
				fmt_custom->render(dctx);
			}
			else
			{
				dctx.draw_default_bg(&(*it));
				dctx.draw_default_fg(&(*it));
			}

			if (it->_flags & line_no_new_line)
			{
				x = dctx.rsltRect->right;
			}
			else
			{
				if (it->_flags & line_wrapped)
					y = h = max(h, (int)dctx.rsltRect->bottom);
				else
				{
					y = (int)dctx.rsltRect->bottom;
					h = max(h, y);
				}

				x = VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.left);
			}

			w = max(w, (int)dctx.rsltRect->right);
		}

		if (!master_do_not_colour_switch && gShellAttr->IsDevenv10OrHigher() && !vsNetTip)
		{
			// [case: 45866]
			extern void ClearTextOutCacheComment();
			ClearTextOutCacheComment();
		}

		w += VsUI::DpiHelper::LogicalToDeviceUnitsX(mMargin.right);
		h += VsUI::DpiHelper::LogicalToDeviceUnitsY(mMargin.bottom);

		w = min(maxw, w);
		h = min(maxh, h);

		int orig_m_pt_y = m_pt.y;
		// make sure tooltip does not obstruct refactoring button (before desktop rect check)
		if (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->m_hWnd &&
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
			_ASSERTE(!"Fix the bad code that caused this exception in ArgToolTipEx::Layout");
		}
	}
	// No longer needed, sice we prevent this above case=31814
	// 	if(m_argInfo && g_CompletionSet->IsExpUp(NULL) && !g_CompletionSet->IsAboveCaret())
	// 	{
	// 		// case 20522: Don't display arginfo over completion lists, move list above caret.
	// 		g_CompletionSet->CalculatePlacement();
	// 		g_CompletionSet->DisplayList(TRUE);
	// 	}
	//
	mNumTimerTicks = 0;
	SetTimer(TT_TIMER, 50, NULL);

	return;
}

void ArgToolTipEx::OverrideFont(const CFontW& font)
{
	if (mFontOverride.m_hObject)
		mFontOverride.DeleteObject();

	LOGFONTW lfont = {};
	if (font.m_hObject && ::GetObjectW(font.m_hObject, sizeof(LOGFONTW), &lfont))
		mFontOverride.CreateFontIndirect(&lfont);
}

void ArgToolTipEx::SetMargin(UINT left, UINT top, UINT right, UINT bottom)
{
	mMargin.left = (LONG)left;
	mMargin.top = (LONG)top;
	mMargin.right = (LONG)right;
	mMargin.bottom = (LONG)bottom;
}

void ArgToolTipEx::TrackPosition(std::function<bool(CPoint& pt)> pos_func, int duration)
{

	mPosTrackingFunc = pos_func;
	if (pos_func)
		SetTimer(TT_XY_TRACKER, (UINT)duration, NULL);
	else
		KillTimer(TT_XY_TRACKER);
}

LRESULT ArgToolTipEx::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_PAINT)
		SetTimer(TT_TIMER, 50, NULL);
	else if (message == WM_TIMER && wParam == (WPARAM)TT_TIMER)
	{
		DWORD timeEllapsed = ++mNumTimerTicks * 50;

		if (timeEllapsed < 200 && !IsWindowVisible())
			ShowWindow(SW_SHOW);

		bool durationExpired = mDuration > 0 && timeEllapsed >= mDuration;
		bool focusCheck = (timeEllapsed % 500) == 0;

		if (durationExpired || focusCheck)
		{
			KillTimer(TT_TIMER);

			if (IsWindowVisible())
			{
				if (durationExpired)
					ShowWindow(SW_HIDE);
				else
				{
					if (!::GetFocus())
						ShowWindow(SW_HIDE);
					else
						SetTimer(TT_TIMER, 50, NULL);
				}
			}
		}
	}
	else if (message == WM_TIMER && wParam == (WPARAM)TT_XY_TRACKER)
	{
		if (mPosTrackingFunc && IsWindowVisible())
		{
			CPoint pt;
			if (mPosTrackingFunc(pt))
			{
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
				} _scoped(mIsTrackingPos);

				CRect rc;
				GetWindowRect(&rc);
				if (rc.TopLeft() != pt)
					SetWindowPos(nullptr, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOACTIVATE);
			}
		}
	}
	else if (message == WM_NCDESTROY)
	{
		KillTimer(TT_XY_TRACKER);
		KillTimer(TT_TIMER);
	}
	else if (message == WM_WINDOWPOSCHANGING)
	{
		LPWINDOWPOS pPos = (LPWINDOWPOS)lParam;

		if (pPos)
		{
			if (mPosTrackingFunc && (pPos->flags & SWP_NOMOVE) == 0)
			{
				if (!mIsTrackingPos)
				{
					CPoint pt(pPos->x, pPos->y);
					if (mPosTrackingFunc(pt))
					{
						pPos->x = pt.x;
						pPos->y = pt.y;
					}
					else
					{
						pPos->flags |= SWP_NOMOVE; // don't move, stay on current position
					}
				}
			}

			if (pPos->flags & SWP_HIDEWINDOW)
				if (mIgnoreHide || mNumTimerTicks * 50 < mMinDuration)
					pPos->flags &= ~SWP_HIDEWINDOW;
		}
	}

	return __super::DefWindowProc(message, wParam, lParam);
}

void ArgToolTipEx::SetDuration(DWORD duration, DWORD minimum /*= 0*/)
{
	mDuration = duration;
	mMinDuration = minimum;
	if (m_hWnd && ::IsWindow(m_hWnd))
		SetTimer(TT_TIMER, 50, NULL);
}
