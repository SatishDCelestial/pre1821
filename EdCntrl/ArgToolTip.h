#if !defined(AFX_ARGTOOLTIP_H__0D809CE2_36BE_11D2_9311_000000000000__INCLUDED_)
#define AFX_ARGTOOLTIP_H__0D809CE2_36BE_11D2_9311_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// ArgToolTip.h : header file
//
// VA
#include "token.h"
#include "DevShellAttributes.h"
#include "VaAfxW.h"
#include "FontSettings.h"
#include "Project.h"

// STL
#include <string>
#include <functional>
#include <sstream>

// message sent to ArgTooltip to delete itself,
// it is needed due to thread safety as MFC maps
// are not accessible from other threads
const UINT WM_VA_SELFDELETE = ::RegisterWindowMessageA("WM_VA_SELFDELETE");

class EdCnt;

// FormattedTextMarkup
#define FTM_BOLD '\1'
#define FTM_NORMAL '\2'
#define FTM_UNDERLINE '\3'
#define FTM_ITALIC '\4'
#define FTM_DIM '\5'

// extended markup
#define FTM_EX '\6'
#define FTM_EX_MULTI "\6M"
#define FTM_EX_DONT_COLOUR "\6D"
#define FTM_EX_FG_RGBA "\6F" // follows color in HEX format RRGGBBAA
#define FTM_EX_FG_RGB "\6f"  // follows color in HEX format RRGGBB
#define FTM_EX_BG_RGBA "\6B" // follows color in HEX format RRGGBBAA
#define FTM_EX_BG_RGB "\6b"  // follows color in HEX format RRGGBB
#define FTM_EX_RN "\6R"      // follows char taken as FtmID::ToChar(my_id)

struct FtmID
{
	static CHAR ToChar(int id);
	static int FromChar(CHAR ch);
};

enum line_flags
{
	line_flags_none = 0x00,
	line_dont_colour = 0x01,
	line_bold = 0x02,
	line_no_new_line = 0x04,
	line_underlined = 0x08,
	line_italic = 0x10,
	line_dim = 0x20,
	line_wrapped = 0x40
};

template <typename chT> class FtmBuilder
{
	typedef std::basic_string<chT> strT;
	strT str;
	bool ml = false;

	FtmBuilder& Add(chT fmt)
	{
		str.append(1, fmt);
		return *this;
	}

	FtmBuilder& Add(LPCSTR fmt)
	{
		for (; *fmt; fmt++)
			str.append(1, (chT)*fmt);
		return *this;
	}

	FtmBuilder& AddHex(UCHAR fmt)
	{
		CHAR buff[5];
		sprintf_s(buff, 5, "%02X", fmt);
		Add(buff);
		return *this;
	}

  public:
	bool MultiMode() const
	{
		return ml;
	}

	FtmBuilder()
	{
	}
	virtual ~FtmBuilder()
	{
	}

	FtmBuilder& Text(const chT* txt)
	{
		if (txt)
			str += txt;
		return *this;
	}

	// safeness of this method is in fact, that it does not
	// add line ends, but instead adds a spaces
	// Note: line end causes style reset!
	FtmBuilder& TextS(const chT* txtIn)
	{
		for (const chT* txt = txtIn; txt && *txt; txt++)
		{
			if (*txt == '\r' || *txt == '\n')
			{
				if (txt > txtIn && ((txt[-1] == '\n') || (txt[-1] == '\r')) && (txt[-1] != *txt))
					continue; // handle \n\r or \r\n as a single line break

				Add(' ');
			}
			else
			{
				Add(*txt);
			}
		}
		return *this;
	}

	FtmBuilder& Char(chT ch)
	{
		return Add(ch);
	}

	// \n also clears styles!!!
	FtmBuilder& LineEnd()
	{
		return Add('\n');
	}

	template <typename PrintableT> FtmBuilder& Value(PrintableT val)
	{
		std::basic_ostringstream<chT> ss;
		ss << val;
		str += ss.str();
		return *this;
	}

	FtmBuilder& Reset()
	{
		ml = false;
		return Add(FTM_NORMAL);
	}
	FtmBuilder& Normal()
	{
		return Reset();
	}
	FtmBuilder& Bold()
	{
		return Add(FTM_BOLD);
	}
	FtmBuilder& Underline()
	{
		return Add(FTM_UNDERLINE);
	}
	FtmBuilder& Italic()
	{
		return Add(FTM_ITALIC);
	}
	FtmBuilder& Dimmed()
	{
		return Add(FTM_DIM);
	}

	// FTM_EX based

	FtmBuilder& MultiMode()
	{
		ml = true;
		return Add(FTM_EX_MULTI);
	}
	FtmBuilder& DontColour()
	{
		return Add(FTM_EX_DONT_COLOUR);
	}

	FtmBuilder& FG(BYTE r, BYTE g, BYTE b, BYTE a = 255)
	{
		if (a == 255)
		{
			Add(FTM_EX_FG_RGB);
			AddHex(r);
			AddHex(g);
			AddHex(b);
		}
		else
		{
			Add(FTM_EX_FG_RGBA);
			AddHex(r);
			AddHex(g);
			AddHex(b);
			AddHex(a);
		}
		return *this;
	}

	FtmBuilder& FG(COLORREF color, BYTE a)
	{
		return FG(GetRValue(color), GetGValue(color), GetBValue(color), a);
	}

	FtmBuilder& FG(DWORD rgba)
	{
		return FG(GetRValue(rgba), GetGValue(rgba), GetBValue(rgba), LOBYTE(rgba >> 24));
	}

	FtmBuilder& BG(BYTE r, BYTE g, BYTE b, BYTE a = 255)
	{
		if (a == 255)
		{
			Add(FTM_EX_BG_RGB);
			AddHex(r);
			AddHex(g);
			AddHex(b);
		}
		else
		{
			Add(FTM_EX_BG_RGBA);
			AddHex(r);
			AddHex(g);
			AddHex(b);
			AddHex(a);
		}
		return *this;
	}

	FtmBuilder& BG(COLORREF color, BYTE a)
	{
		return BG(GetRValue(color), GetGValue(color), GetBValue(color), a);
	}

	FtmBuilder& BG(DWORD rgba)
	{
		return BG(GetRValue(rgba), GetGValue(rgba), GetBValue(rgba), LOBYTE(rgba >> 24));
	}

	FtmBuilder& Renderer(UCHAR id)
	{
		Add(FTM_EX_RN);
		Add((chT)FtmID::ToChar((int)id));
		return *this;
	}

	const strT& Str() const
	{
		return str;
	}
	operator const strT&() const
	{
		return str;
	}
};

struct FormattedTextLine
{
	FormattedTextLine(const CStringW& txt, line_flags flags)
	    : _txt(txt), _flags(flags), _fg_rgba(0), _bg_rgba(0), _custom(0)
	{
	}
	FormattedTextLine(const CStringW& txt, line_flags flags, DWORD fg_rgba, DWORD bg_rgba)
	    : _txt(txt), _flags(flags), _fg_rgba(fg_rgba), _bg_rgba(bg_rgba), _custom(0)
	{
	}
	FormattedTextLine(const CStringW& txt, line_flags flags, DWORD fg_rgba, DWORD bg_rgba, BYTE renderer)
	    : _txt(txt), _flags(flags), _fg_rgba(fg_rgba), _bg_rgba(bg_rgba), _custom(renderer)
	{
	}

	CStringW _txt;
	line_flags _flags;
	CRect _rc;
	DWORD _fg_rgba;
	DWORD _bg_rgba;
	BYTE _custom;
};

typedef std::list<FormattedTextLine> FormattedTextLines;
bool ParseFormattedTextMarkup(const CStringW& txt, FormattedTextLines& lines);
CStringW RemoveFormattedTextMarkup(const CStringW& txt);

struct FormatRenderer
{
	struct DrawingContext
	{
		typedef std::function<void(const FormattedTextLine*)> DrawFnc;

		CDC* dc;
		DWORD textFlags;
		CRect* dstRect;
		CRect* rsltRect;
		const FormattedTextLine* fmt;
		DrawFnc draw_default_bg;
		DrawFnc draw_default_fg;
	};

	typedef std::function<void(DrawingContext&)> RenderFnc;

	FormatRenderer(RenderFnc fnc = nullptr) : render(fnc)
	{
	}
	RenderFnc render;
};

/////////////////////////////////////////////////////////////////////////////
// ArgToolTip window
class ArgToolTip : public CWnd
{
	// Construction
  protected:
	HWND m_hPar;
	EdCnt* m_ed;

	WTString m_lstr, m_cstr, m_rstr;
	CPoint m_pt;
	bool m_reverseColors;
	bool m_keeponscreen;
	bool m_color;
	bool do_hide_on_mouse_up;
	bool mRedisplayOnMouseUp;
	bool mColorOverride;
	COLORREF mFgOverride;
	COLORREF mBgOverride;
	COLORREF mBorderOverride;
	VaFont mFont, mFontB, mFontU;

	void UpdateFonts();

  public:
	BOOL m_argInfo;
	BOOL m_ourTip;
	int m_totalDefs, m_currentDef;
	bool dont_close_on_ctrl_key; // don't close tooltip if ctrl key is pressed (to fade completion box)
	HWND avoid_hwnd;             // try to not overlap with this window

	ArgToolTip(EdCnt* par);
	ArgToolTip(HWND hParent);
	virtual ~ArgToolTip();

	WTString ToString()
	{
		return m_lstr + "[" + m_cstr + "]" + m_rstr;
	}

	// Attributes
  public:
	// Operations
  public:
	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(ArgToolTip)
	//}}AFX_VIRTUAL

	// Implementation
  public:
	void Display(CPoint* pt, LPCSTR left, LPCSTR cur, LPCSTR right, int totalDefs, int currentDef,
	             bool reverseColor = false, bool keeponscreen = true, BOOL color = TRUE, BOOL ourTip = TRUE);
	virtual void Layout(bool in_paint);
	int BeautifyDefs(token& defs);
	bool HasDisplayStr() const
	{
		if (m_lstr.GetLength() || m_cstr.GetLength() || m_rstr.GetLength())
			return true;
		else
			return false;
	}
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	void DoHideOnActivate();
	void OverrideColor(bool en, COLORREF fg, COLORREF bg, COLORREF brdr);
	COLORREF FGOverride() const
	{
		return mFgOverride;
	}
	COLORREF BGOverride() const
	{
		return mBgOverride;
	}
	COLORREF BorderOverride() const
	{
		return mBorderOverride;
	}
	void UseVsEditorTheme();
	bool UseTheme(bool use = true);

  protected:
	LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	HTHEME theme;

	// Generated message map functions
  protected:
	//{{AFX_MSG(ArgToolTip)
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnPaint();
	afx_msg void OnNcDestroy();
	//}}AFX_MSG
	afx_msg LRESULT OnRepaintBeneath(WPARAM wparam = 0, LPARAM lparam = 0);
	afx_msg LRESULT OnVASelfDelete(WPARAM wparam = 0, LPARAM lparam = 0);

	DECLARE_MESSAGE_MAP()

	friend class CListCtrl2008;
};

extern ArgToolTip* g_pLastToolTip;
extern ArgToolTip* s_ttip;
extern HWND sActiveToolTipWrapperTipOwner;
extern DWORD sToolTipWrapperStartTicks;
extern CPoint sToolTipWrapperStartPt;

#include "Settings.h"

template <typename BASE = CWnd> class ToolTipWrapper : public BASE
{
  private:
	enum
	{
		TTW_TimerId = 0x343000
	};
	BOOL m_ShouldDelete;
	BOOL mSubclassed;
	int mDisplayCount;
	WTString m_ttText;
	int m_yoffset;
	CRect mOptionalTooltipValidRect;

  public:
	BOOL m_readOnly;
	ToolTipWrapper(HWND hParent, LPCSTR tipText, int yoffset = 10, BOOL ShouldDelete = TRUE)
	{
		if (hParent)
		{
			mSubclassed = TRUE;
			BASE::SubclassWindow(hParent);
		}
		else
			mSubclassed = FALSE;

		SetTipText(tipText);
		m_yoffset = yoffset;
		m_readOnly = FALSE;
		m_ShouldDelete = ShouldDelete;
	}
	virtual ~ToolTipWrapper()
	{
	}

	void SetTipText(LPCSTR tipText)
	{
		if (tipText)
		{
			m_ttText = tipText;
			mDisplayCount = 2;
		}
		else
			mDisplayCount = 0;
	}

	virtual void PreSubclassWindow()
	{
		__super::PreSubclassWindow();
		mSubclassed = TRUE;
	}

	virtual BOOL OkToDisplayTooltip()
	{
		return TRUE;
	}

  protected:
	void SetToolRect(const CRect& rc)
	{
		mOptionalTooltipValidRect = rc;
	}
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (m_readOnly)
		{
			if (WM_CHAR == message)
				return TRUE;
			if (WM_KEYDOWN == message)
			{
				if (VK_DELETE == wParam)
					return TRUE;
			}
			else if (EM_UNDO == message || WM_CUT == message || WM_CLEAR == message || WM_PASTE == message)
			{
				return TRUE;
			}
		}
		if (::GetFocus() == NULL && s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible())
			s_ttip->ShowWindow(SW_HIDE);
		if (message == WM_MOUSEMOVE || (message == WM_TIMER /*&& wParam == TTW_TimerId*/))
		{
			if (!s_ttip)
				s_ttip = new ArgToolTip(gMainWnd->GetSafeHwnd());

			CPoint curPt;
			GetCursorPos(&curPt);
			DWORD curTicks = GetTickCount();
			BOOL canDoToolTip = mDisplayCount > 0 && !m_ttText.IsEmpty();

			if (message == WM_TIMER)
			{
				BASE::KillTimer(TTW_TimerId);

				if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
				    sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
				{
					if ((curTicks - sToolTipWrapperStartTicks) > 4000)
					{
						// finished displaying
						s_ttip->ShowWindow(SW_HIDE);
						canDoToolTip = FALSE;
					}
					else if (curPt == sToolTipWrapperStartPt)
					{
						// not finished yet
						canDoToolTip = FALSE;
						BASE::SetTimer(TTW_TimerId, 1000, NULL);
					}
				}
			}
			else if (!sToolTipWrapperStartTicks && WM_MOUSEMOVE == message)
				sToolTipWrapperStartTicks = curTicks;

			if (curPt != sToolTipWrapperStartPt)
			{
				{
					CToolTipCtrl* tip =
					    (CToolTipCtrl*)CWnd::FromHandle((HWND)::SendMessage(BASE::m_hWnd, TVM_GETTOOLTIPS, 0, 0L));
					if (tip && ::IsWindow(tip->m_hWnd) && tip->IsWindowVisible())
						canDoToolTip = FALSE;
					if (::GetCapture())
						canDoToolTip = FALSE;
				}

				if (canDoToolTip && message == WM_TIMER)
				{
					if (BASE::WindowFromPoint(curPt)->GetSafeHwnd() == BASE::GetSafeHwnd())
					{
						if ((curTicks - sToolTipWrapperStartTicks) > 1000)
						{
							sToolTipWrapperStartPt = curPt;
							CPoint pt(curPt.x, curPt.y + m_yoffset);
							if (s_ttip->GetSafeHwnd())
							{
								s_ttip->DestroyWindow();
								sActiveToolTipWrapperTipOwner = NULL;
							}

							if ((!mOptionalTooltipValidRect.IsRectNull() && !mOptionalTooltipValidRect.IsRectEmpty() &&
							     !mOptionalTooltipValidRect.PtInRect(curPt)) ||
							    !m_ttText.GetLength())
							{
								canDoToolTip = false;
							}

							if (canDoToolTip)
								canDoToolTip = OkToDisplayTooltip();

							if (canDoToolTip)
							{
								if (gShellAttr->IsDevenv10OrHigher())
									s_ttip->UseTheme(true);
								sActiveToolTipWrapperTipOwner = BASE::GetSafeHwnd();
								sToolTipWrapperStartTicks = curTicks;
								s_ttip->Display(&pt, m_ttText.c_str(), "", "", 1, 1, FALSE, TRUE, FALSE, FALSE);
								BASE::SetTimer(TTW_TimerId, 1000, NULL);
								if (!--mDisplayCount)
									m_ttText.Empty(); // only show once
							}
						}
					}
					else if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
					         sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
					{
						// different wnd
						s_ttip->ShowWindow(SW_HIDE);
					}
				}
				else if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
				         sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
				{
					// mouse move or !canDoToolTip
					s_ttip->ShowWindow(SW_HIDE);
				}

				if (canDoToolTip && WM_MOUSEMOVE == message)
				{
					BASE::KillTimer(TTW_TimerId);
					BASE::SetTimer(TTW_TimerId, 1000, NULL);
				}
			}
		}
		else if (message == WM_WINDOWPOSCHANGING)
		{
			if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
			    sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
				s_ttip->ShowWindow(SW_HIDE);
		}
		else if (message == WM_SHOWWINDOW)
		{
			if (wParam == SW_HIDE)
			{
				if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
				    sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
					s_ttip->ShowWindow(SW_HIDE);
				if (s_ttip == g_pLastToolTip)
					g_pLastToolTip = NULL;
			}
		}
		else if (message == WM_DESTROY)
		{
			try
			{
				if (BASE::m_hWnd && IsWindow(BASE::m_hWnd))
				{
					if (s_ttip->GetSafeHwnd() && s_ttip->IsWindowVisible() &&
					    sActiveToolTipWrapperTipOwner == BASE::GetSafeHwnd())
					{
						s_ttip->ShowWindow(SW_HIDE);
						sActiveToolTipWrapperTipOwner = nullptr;
					}

					LPARAM r = BASE::DefWindowProc(message, wParam, lParam);
					if (mSubclassed)
						BASE::UnsubclassWindow();
					if (m_ShouldDelete)
						delete this;
					return r;
				}
			}
			catch (...)
			{
				VALOGEXCEPTION("ATT");
				if (!Psettings->m_catchAll)
				{
					_ASSERTE(!"Fix the bad code that caused this exception in ToolTipWrapper::DefWindowProc");
				}
			}
		}

		return BASE::DefWindowProc(message, wParam, lParam);
	}
};

int BeautifyDefs(token& defs);

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ARGTOOLTIP_H__0D809CE2_36BE_11D2_9311_000000000000__INCLUDED_)
