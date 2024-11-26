#include "stdafxed.h"
#include "resource.h"
#include "Edcnt.h"
#include "VAParse.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "ParseThrd.h"
#include "FileTypes.h"
#include "TempSettingOverride.h"
#include "Settings.h"
#include "FontSettings.h"
#include "VaService.h"
#include "SyntaxColoring.h"
#include "WindowUtils.h"
#include <usp10.h>
#include "mainThread.h"
#include "HookCode.h"
#include "ColorSyncManager.h"
#include "PROJECT.H"
#include "Library.h"
#include <mutex>

extern int g_PaintLock;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define GLYFLEN 32000

BOOL g_PaintLock;
long ColorInstCounter::s_WrapperCount = 0;
int PaintType::in_WM_PAINT = 0;
int PaintType::inPaintType = 0;
int PaintType::SetPaintType(int type)
{
	inPaintType = type;

	_ASSERTE(Psettings);
	if (!Psettings)
		return FALSE;

	switch (type)
	{
	case SourceWindow:
		//		return Psettings->m_bEnhColorSourceWindows;
		return Psettings->m_ActiveSyntaxColoring; // Needed for bolding
	case ObjectBrowser:
		return Psettings->m_bEnhColorObjectBrowser;
	case ToolTip:
		return Psettings->m_bEnhColorTooltips;
	case VS_ToolTip:
		return Psettings->m_bEnhColorTooltips;
	case View:
		return Psettings->m_bEnhColorViews;
	case ListBox:
		return Psettings->m_bEnhColorListboxes;
	case WizardBar:
		return Psettings->m_bEnhColorWizardBar;
	case FindInFiles:
		return Psettings->m_bEnhColorFindResults;
#ifdef _DEBUG
	case AssemblyView:
		return Psettings->m_bEnhColorListboxes; // TODO: does this need an option
#endif                                          // _DEBUG
	}
	return FALSE;
}

//////////////////////////////////////////////////////////////////////////
// Glyph Patch
GlyfBuffer g_GlyfBuffer;

#ifdef _DEBUG
HRESULT(WINAPI* RealScriptTextOut)
(const HDC hdc, SCRIPT_CACHE* psc, int x, int y, UINT fuOptions, const RECT* lprc, const SCRIPT_ANALYSIS* psa,
 const WCHAR* pwcReserved, int iReserved, const WORD* pwGlyphs, int cGlyphs, const int* piAdvance, const int* piJustify,
 const GOFFSET* pGoffset) = NULL;
HRESULT WINAPI VAScriptTextOut(const HDC hdc, SCRIPT_CACHE* psc, int x, int y, UINT fuOptions, const RECT* lprc,
                               const SCRIPT_ANALYSIS* psa, const WCHAR* pwcReserved, int iReserved,
                               const WORD* pwGlyphs, int cGlyphs, const int* piAdvance, const int* piJustify,
                               const GOFFSET* pGoffset)
{
	return RealScriptTextOut(hdc, psc, x, y, fuOptions, lprc, psa, pwcReserved, iReserved, pwGlyphs, cGlyphs, piAdvance,
	                         piJustify, pGoffset);
}
#endif // _DEBUG

HRESULT(WINAPI* RealScriptShape)
(HDC hdc, SCRIPT_CACHE* psc, const WCHAR* pwcChars, int cChars, int cMaxGlyphs, SCRIPT_ANALYSIS* psa, WORD* pwOutGlyphs,
 WORD* pwLogClust, SCRIPT_VISATTR* psva, int* pcGlyphs) = NULL;
HRESULT WINAPI VAScriptShape(HDC hdc, SCRIPT_CACHE* psc, const WCHAR* pwcChars, int cChars, int cMaxGlyphs,
                             SCRIPT_ANALYSIS* psa, WORD* pwOutGlyphs, WORD* pwLogClust, SCRIPT_VISATTR* psva,
                             int* pcGlyphs)
{
	HRESULT r = RealScriptShape(hdc, psc, pwcChars, cChars, cMaxGlyphs, psa, pwOutGlyphs, pwLogClust, psva, pcGlyphs);
	if (GetCurrentThreadId() == g_mainThread) // case 17231
	{
		if (g_PaintLock)
			return r; // No need to save these glyfs, happens if Asian fonts are installed.

		if (pcGlyphs && cChars != *pcGlyphs)
		{
			// Glyph len does not match strlen, bail, or we may print the wrong characters.
			// case=25254 wrong characters drawn when using font with ligatures - joined together characters
			return r;
		}

		if (!psa->fNoGlyphIndex) // Fixes: case 15124, case 15548
			g_GlyfBuffer.Add(hdc, pwcChars, (LPCWSTR)pwOutGlyphs, cChars);
	}
	//	else
	//		_asm nop; // for breakpoint
	return r;
}

extern int ExtTextOutWColor(HDC dc, int x, int y, UINT style, LPCWSTR str, int len, LPCRECT prc, CONST INT* w);
extern int ExtTextOutColor(HDC dc, int x, int y, UINT style, LPCSTR str, int len, LPCRECT prc, CONST INT* w);
extern void GetFontInfo(HDC dc);
extern int g_inPaint;
extern int g_screenXOffset;
extern int g_screenMargin;
extern BOOL ShouldColorWindow(HDC dc);

BOOL(WINAPI* RealTextOut)(HDC dc, int x, int y, LPCSTR str, UINT len) = NULL;
BOOL WINAPI VATextOut(HDC dc, int x, int y, LPCSTR str, UINT len)
{
	if (GetCurrentThreadId() == g_mainThread) // case 17231
	{
		if (g_inPaint == 1 && len && !g_PaintLock)
		{
			GetFontInfo(dc);
			// Calculate g_screenXOffset
			CPoint curPT;
			::GetCurrentPositionEx(dc, &curPT);
			g_screenXOffset = curPT.x - x;
			g_screenMargin = curPT.x;
			g_inPaint = 2;
		}

		if (len)
		{
			bool docolour = !!ShouldColorWindow(dc);
			if (!docolour)
			{
				extern EdCnt* g_paintingEdCtrl;
				extern EdCnt* g_printEdCnt;
				docolour |= !g_paintingEdCtrl && g_printEdCnt && Psettings->m_ColorPrinting && gShellAttr->IsMsdev() &&
				            !::WindowFromDC(dc);
			}
			if (docolour)
				return ExtTextOutColor(dc, x, y, NULL, str, (int)len, NULL, NULL);
		}
	}
	//	else
	//		_asm nop; // for breakpoint

	return RealTextOut(dc, x, y, str, len);
}

BOOL(WINAPI* RealExtTextOut)
(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCSTR str, UINT len, CONST INT* w) = NULL;
BOOL WINAPI VAExtTextOut(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCSTR str, UINT len, CONST INT* w)
{
	if (GetCurrentThreadId() == g_mainThread) // case 17231
	{
		if (g_inPaint == 1 && len /*> 4*/ && !g_PaintLock)
		{
			// GetCurrentPositionEx only works in vc6
			if (gShellAttr->IsMsdev()) // fixes line number issue in vs.net case: 14470
			{
				GetFontInfo(dc);
				// Calculate g_screenXOffset
				CPoint curPT;
				::GetCurrentPositionEx(dc, &curPT);
				g_screenXOffset = curPT.x - x;
				g_inPaint = 2;
			}
		}
		if (len && ShouldColorWindow(dc))
		{
			if (len > 4)
				GetFontInfo(dc);
			if (!(g_inPaint && gShellAttr->IsDevenv())) // Not VSNet line numbers, case: 16038
				return ExtTextOutColor(dc, x, y, style, str, (int)len, rc, w);
		}
	}
	//	else
	//		_asm nop; // for breakpoint

	return RealExtTextOut(dc, x, y, style, rc, str, len, w);
}

BOOL(WINAPI* RealExtTextOutW)
(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCWSTR str, UINT len, CONST INT* w) = NULL;
BOOL WINAPI VAExtTextOutW(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCWSTR str, UINT len, CONST INT* w)
{
	if (GetCurrentThreadId() == g_mainThread && len) // case 17231
	{
		if (g_inPaint == 1 && !g_PaintLock)
		{
			GetFontInfo(dc);
			if (rc)
				g_FontSettings->SetLineHeight(rc->bottom - rc->top);
			if (rc && rc->left > 0) // >0 for disabled margin, case:16328
			{
				// If window is not hscrolled and offset is not what we think...
				if (g_screenXOffset == g_screenMargin && g_screenMargin != rc->left)
					g_screenMargin = NULL; // oops, wrong val, nuke to recalculate below
				// Calculate g_screenXOffset
				if (!g_screenMargin)
				{
					g_screenMargin = rc->left;
					// Gotsta recalculate offset with new margin
					if (g_currentEdCnt && g_currentEdCnt->m_IVsTextView)
					{
						long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit;
						g_currentEdCnt->m_IVsTextView->GetScrollInfo(SB_HORZ, &iMinUnit, &iMaxUnit, &iVisibleUnits,
						                                             &iFirstVisibleUnit);
						g_screenXOffset = g_screenMargin - (iFirstVisibleUnit * g_vaCharWidth['z']);
					}
				}
				g_inPaint = 2;
			}
		}

		if (ShouldColorWindow(dc))
		{
			if (g_inPaint && !(style & ETO_GLYPH_INDEX) && gShellAttr->IsDevenv())
			{
				// In vsnet some fonts(consolas) print non glyphed spaces before printing _'s.
				// The spaces break our underlining and styles.
				// This fix assumes that all colorable text out is glyphed in vsnet.
				// Fixes underlining in case 15912
				return RealExtTextOutW(dc, x, y, style, rc, str, len, w);
			}
			LPCWSTR nonGlyfStr = (style & ETO_GLYPH_INDEX) ? g_GlyfBuffer.GetBuf(dc, str, (int)len) : str;
			if (nonGlyfStr)
				return ExtTextOutWColor(dc, x, y, style & ~ETO_GLYPH_INDEX, nonGlyfStr, (int)len, rc, w);
		}
	}
	//	else
	//		_asm nop; // for breakpoint

	return RealExtTextOutW(dc, x, y, style, rc, str, len, w); // Call the real one
}

BOOL(WINAPI* RealTextOutW)(HDC dc, int x, int y, LPCWSTR str, UINT len) = NULL;
BOOL WINAPI VATextOutW(HDC dc, int x, int y, LPCWSTR str, int len)
{
	if (GetCurrentThreadId() == g_mainThread) // case 17231
	{
		if (len && Psettings && ShouldColorWindow(dc))
			return ExtTextOutWColor(dc, x, y, NULL, str, (int)len, NULL, NULL);
	}
	//	else
	//		_asm nop; // for breakpoint

	return RealTextOutW(dc, x, y, str, (uint)len);
}

HWND g_DefWindowProcHWND = NULL;

LRESULT(WINAPI* RealDispatchMessageW)(IN CONST MSG* lpMsg) = NULL;
extern "C" 
#if !defined(RAD_STUDIO)
_declspec(dllexport) 
#endif
LRESULT WINAPI DispatchMessageWHook(IN MSG* lpMsg)
{
	if (lpMsg->message == WM_PAINT || lpMsg->message == WM_NCPAINT || lpMsg->message == WM_DRAWITEM)
	{
		if (GetCurrentThreadId() == g_mainThread)
		{
			// Cache the HWND for Bitmap TextOut paints
			g_DefWindowProcHWND = lpMsg->hwnd;
			LRESULT r = RealDispatchMessageW(lpMsg);
			g_DefWindowProcHWND = nullptr;
			return r;
		}
	}

	return RealDispatchMessageW(lpMsg);
}

HMODULE GetUsp10Module();

void PatchTextOutMethods(BOOL doPatch)
{
	if (doPatch)
	{
		if (!RealTextOut)
			WtHookCode((FARPROC)(uintptr_t)::TextOutA, VATextOut, (PVOID*)&RealTextOut);
		if (!RealTextOutW)
			WtHookCode((FARPROC)(uintptr_t)::TextOutW, VATextOutW, (PVOID*)&RealTextOutW);
		if (!RealExtTextOut)
			WtHookCode((FARPROC)(uintptr_t)::ExtTextOutA, VAExtTextOut, (PVOID*)&RealExtTextOut);
		if (!RealExtTextOutW)
			WtHookCode((FARPROC)(uintptr_t)::ExtTextOutW, VAExtTextOutW, (PVOID*)&RealExtTextOutW);
		if (!RealScriptShape)
			WtHookCode((FARPROC)(uintptr_t)GetProcAddress(GetUsp10Module(), "ScriptShape"), VAScriptShape,
			           (void**)&RealScriptShape);
#ifndef _ARM64
		// there is no room for detour jump; "Cache the HWND for Bitmap TextOut paints" won't be working on ARM
		if (!RealDispatchMessageW)
			WtHookCode((FARPROC)(uintptr_t)::DispatchMessageW, DispatchMessageWHook, (void**)&RealDispatchMessageW);
#endif
#ifdef _DEBUG
		if (!RealScriptTextOut)
			WtHookCode((FARPROC)(uintptr_t)GetProcAddress(GetUsp10Module(), "ScriptTextOut"), VAScriptTextOut,
			           (void**)&RealScriptTextOut);
#endif // _DEBUG
	}
	else
	{
		WtUnhookCode(VATextOut, (PVOID*) & RealTextOut);
		RealTextOut = NULL;
		WtUnhookCode(VATextOutW, (PVOID*) & RealTextOutW);
		RealTextOutW = NULL;
		WtUnhookCode(VAExtTextOut, (PVOID*) & RealExtTextOut);
		RealExtTextOut = NULL;
		WtUnhookCode(VAExtTextOutW, (PVOID*) & RealExtTextOutW);
		RealExtTextOutW = NULL;
	}
}

void GlyfBuffer::Add(HDC dc, LPCWSTR str, LPCWSTR glyf, int len)
{
	if (dc != m_dc)
		Reset(dc);
	wchar_t* GlyfXrefBuffer = GetGlyfXrefBuffer();
	if (GlyfXrefBuffer)
	{
		for (int i = 0; i < len && i < GLYFLEN; i++)
		{
			UINT g = glyf[i];
			if (g >= GLYFLEN)
				g &= 0x0fff;
			if (/*g >= 0 &&*/ g < GLYFLEN)
			{
				if (!g)
				{
					// Per MSDN doc: "...the application should check the output for missing glyphs."
					// In this case, the control will call exttexout using the
					//  original wstr and ETO_GLYPH_INDEX, even though this is not a glyph?
					GlyfXrefBuffer[g] = 0x0; // Null it out so GetBuf returns NULL(See below)
				}
				else if (str[i] == 0xd || str[i] == 0xa) // Map \r\n's to ' ',  case: 15124
					GlyfXrefBuffer[g] = ' ';
				// no longer needed
				// 				else if(str[i] < ' ' && str[i] != 0x9)
				// 				{
				// 					// This happens in such cases such as trying to display
				// 					// uninitialized vars (eg, CString) in the Locals window.
				// 					// We don't paint the Locals window, but this code gets
				// 					// run anyway.
				// 					m_GlyfXrefBuffer[g] = str[i]; // map to space
				// 				}
				else if (str[i] == 0xfeff || str[i] == 0xfffe)
				{
					// ??
					GlyfXrefBuffer[g] = ' '; // case:16879  unicode byte order markers
				}
				else
				{
					//#ifdef _DEBUG
					//					if (!(!GlyfXrefBuffer[g] || GlyfXrefBuffer[g] == str[i]))
					//					{
					//						// Glyph code seems to be working correctly, removing annoying assert.
					// case=16879
					//						// ASSERT_ONCE(!GlyfXrefBuffer[g] || GlyfXrefBuffer[g] == str[i]);
					//						_asm nop;
					//					}
					//#endif // _DEBUG
					GlyfXrefBuffer[g] = str[i];
				}
			}
		}
	}

	if (m_lastPrintPos)
	{
		m_lastPrintPos = 0;
		m_p1 = m_p2 = NULL;
	}
	m_lastPrintPos = 0;
	if (str < m_p1 || size_t(str - m_p1) > 4096)
	{
		m_p1 = str;
		m_p1Char = *m_p1;
	}
	else if (m_p1 && m_p2 && str == m_p1 && m_p1Char != *m_p1)
	{
		// [case:15374] new string at same memory location
		if (m_p2 > (str + len))
			m_p2 = NULL;
		m_p1Char = *m_p1;
	}

	if (m_p2 < (str + len) || size_t(m_p2 - str) > 4096)
		m_p2 = str + len;
}

LPCWSTR GlyfBuffer::GetBuf(HDC dc, LPCWSTR glyf, int len)
{
	if (dc != m_dc)
		return NULL;
	wchar_t* GlyfXrefBuffer = GetGlyfXrefBuffer();
	if (!GlyfXrefBuffer)
		return NULL;
#ifdef _DEBUG
	CString glyfTxt;
	for (int z = 0; z < len && glyf[z] < GLYFLEN; z++)
		glyfTxt += GlyfXrefBuffer[glyf[z]];
#endif // _DEBUG

	for (int i = 0; i < len; i++)
	{
		if (glyf[i] >= GLYFLEN || !GlyfXrefBuffer[glyf[i]])
		{
			// See: missing glyph above in Add()
			// return null so we call the original exttxtout with the glyph bit set.
			return NULL;
		}
	}

again:
	//	int linelen = m_p2 - m_p1 - m_lastPrintPos;
	int lp = m_lastPrintPos;
	for (; m_p1 && (m_p1 + lp) < m_p2; lp++)
	{
		int i = 0;
		for (; i < len && m_p1[lp + i] == GlyfXrefBuffer[glyf[i]]; i++)
			;
		if (i == len)
		{
			m_lastPrintPos = lp + len;
			return &m_p1[lp];
		}
	}
	if (m_lastPrintPos)
	{
		m_lastPrintPos = 0;
		goto again;
	}
	return NULL;
}
// a simple wrapper class for arrays (so they can be stored in a container)
template <typename TYPE, uint COUNT> class array
{
  public:
	TYPE& operator[](int index)
	{
		_ASSERTE((index >= 0) && (index < COUNT));
		return value[index];
	}
	const TYPE& operator[](int index) const
	{
		_ASSERTE((index >= 0) && (index < COUNT));
		return value[index];
	}

	operator TYPE*()
	{
		return value;
	}
	operator const TYPE*() const
	{
		return value;
	}

	TYPE* operator&()
	{
		return value;
	}
	const TYPE* operator&() const
	{
		return value;
	}

  protected:
	TYPE value[COUNT];
};

// a simple wrapper class that will fill objects with zeroes on initialization
template <typename TYPE> class blank : public TYPE
{
  public:
	blank()
	{
		memset(this, 0, sizeof(*this));
	}
};

typedef array<wchar_t, GLYFLEN + 1> GlyphWcharArray;
typedef stdext::hash_map<LOGFONTW, blank<GlyphWcharArray>*> LogfontGlyphMap;
static LogfontGlyphMap s_GlyfXrefBuffers; // only access from g_mainThread

wchar_t* GlyfBuffer::GetGlyfXrefBuffer()
{
	HFONT font = (HFONT)::GetCurrentObject(m_dc, OBJ_FONT);
	if (font)
	{
		static HFONT lastFont = NULL;
		static wchar_t* lastBuf = NULL;
		if (lastFont != font || gShellIsUnloading)
		{
			LOGFONTW lf;
			ZeroMemory(&lf, sizeof(lf));
			if (::GetObjectW(font, sizeof(LOGFONTW), &lf))
			{
				if (s_GlyfXrefBuffers.size() > 15)
					Cleanup();

				auto buf = s_GlyfXrefBuffers[lf];
				if (!buf && !gShellIsUnloading)
				{
					buf = new blank<GlyphWcharArray>();
					s_GlyfXrefBuffers[lf] = buf;
				}

				if (buf)
					lastBuf = *buf;
				else
					lastBuf = nullptr;

				lastFont = font;
			}
			else
			{
				lastFont = NULL;
				lastBuf = NULL;
			}
		}
		return lastBuf;
	}
	return NULL;
}

void GlyfBuffer::Reset(HDC dc /*= NULL*/)
{
	m_dc = dc;
	m_p1 = m_p2 = NULL;
	m_p1Char = L'\0';
	m_lastPrintPos = 0;
}

void GlyfBuffer::Cleanup()
{
	for (auto iter = s_GlyfXrefBuffers.begin(); iter != s_GlyfXrefBuffers.end(); ++iter)
	{
		delete iter->second;
	}
	s_GlyfXrefBuffers.clear();
}

// AST: <SetACP:codepage> Spoof IDE into thinking we changed the system codepage.
static UINT s_acp = CP_ACP;
ULONG(WINAPI* RealGetACP)(void) = NULL;
ULONG WINAPI VAGetACP(void)
{
	return s_acp;
}

UINT SpoofACP(UINT acp)
{
	UINT lastACP = GetACP();
	s_acp = acp;
#ifndef _WIN64
	if (!RealGetACP)
		WtHookCode((FARPROC)(uintptr_t)::GetACP, VAGetACP, (void**)&RealGetACP);
#endif
	return lastACP;
}

// [case: 93048]
extern const uint wm_do_multiple_dpis_workaround_cleanup =
    ::RegisterWindowMessage("WM_DO_MULTIPLE_DPIS_WORKAROUND_CLEANUP");
static std::mutex temp_windows_mutex;
static std::unordered_map<uint, HWND> temp_windows;
static std::atomic<uint> last_temp_windows_id;
#if _MSC_VER < 1920
typedef uint DEVICE_SCALE_FACTOR;
const DEVICE_SCALE_FACTOR DEVICE_SCALE_FACTOR_INVALID = (DEVICE_SCALE_FACTOR)0;
#endif
typedef HRESULT(__stdcall* GetScaleFactorForMonitorFUNC)(HMONITOR hMon, DEVICE_SCALE_FACTOR* pScale);
static GetScaleFactorForMonitorFUNC GetScaleFactorForMonitorFunc;

void DoMultipleDPIsWorkaround(HWND active_hwnd)
{
	if (!Psettings || !Psettings->mEnableMixedDpiScalingWorkaround)
		return;
	if (!active_hwnd || !::IsWindow(active_hwnd))
		return;
	if (!gMainWnd)
		return;

	static Library shcore_dll;
	static bool once = true;
	if (once)
	{
		once = false;
		shcore_dll.Load("shcore.dll");
		if (shcore_dll.IsLoaded())
			shcore_dll.GetFunction("GetScaleFactorForMonitor", GetScaleFactorForMonitorFunc);
	}

	if (!GetScaleFactorForMonitorFunc)
		return;

	// see if there are monitors with different dpis.
	// we don't use the actual factor value, we just care if there are multiple values in use.
	// see comments at:
	// https://stackoverflow.com/questions/31348823/getscalefactorformonitor-value-doesnt-match-actual-scale-applied
	std::unordered_set<uint> dpis;
	::EnumDisplayMonitors(
	    nullptr, nullptr,
	    [](HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM param) -> BOOL {
#if _MSC_VER < 1920
		    DEVICE_SCALE_FACTOR dpi = DEVICE_SCALE_FACTOR_INVALID;
#else
		    DEVICE_SCALE_FACTOR dpi = SCALE_100_PERCENT;
#endif
		    GetScaleFactorForMonitorFunc(monitor, &dpi);
		    if (dpi)
			    ((std::unordered_set<uint>*)param)->insert(dpi);

		    return true;
	    },
	    (LPARAM)&dpis);

	if (dpis.size() > 1)
	{
		std::vector<HWND> windows_on_other_monitors;
		HMONITOR active_mon = ::MonitorFromWindow(active_hwnd, MONITOR_DEFAULTTONULL);

		if (active_mon)
		{
			// find VS top level windows that aren't placed on the monitor where the main window is
			std::function<void(HWND hwnd)> enum_windows_proc = [&](HWND hwnd) {
				DWORD pid;
				::GetWindowThreadProcessId(hwnd, &pid);
				if (pid != ::GetCurrentProcessId())
					return;
				if (!::IsWindowVisible(hwnd))
					return;
				if (::IsIconic(hwnd))
					return;
				HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
				if (mon == active_mon)
					return;
				windows_on_other_monitors.push_back(hwnd);
			};
			::EnumWindows(
			    [](HWND hwnd, LPARAM lparam) -> BOOL {
				    (*(std::function<void(HWND hwnd)>*)lparam)(hwnd);
				    return true;
			    },
			    (LPARAM)&enum_windows_proc);
		}

		if (windows_on_other_monitors.size() > 0)
		{
			MONITORINFO mi = {sizeof(mi)};
			::GetMonitorInfo(active_mon, &mi);

			HWND temp_hwnd = nullptr;
			for (uint cnt = 0; cnt < 10; ++cnt)
			{
				temp_hwnd =
				    ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, L"STATIC", L"", 0,
				                      mi.rcWork.left, mi.rcWork.top, 300, 300, nullptr, nullptr, nullptr, nullptr);

				if (temp_hwnd != nullptr)
					break;

				// add retry for [case: 111864]
				DWORD err = GetLastError();
				vLog("WARN: DoMultipleDPIsWorkaround call to CreateWindowEx failed, 0x%08lx\n", err);
				Sleep(100 + (cnt * 50));
			}

			::SetLayeredWindowAttributes(temp_hwnd, 0, 1, LWA_ALPHA);
			::ShowWindow(temp_hwnd, SW_SHOWNOACTIVATE);

			uint id = ++last_temp_windows_id;
			{
				std::lock_guard<std::mutex> l(temp_windows_mutex);
				temp_windows[id] = temp_hwnd;
			}
			gMainWnd->PostMessage(wm_do_multiple_dpis_workaround_cleanup, (WPARAM)id);
		}
	}
}

void DoMultipleDPIsWorkaroundCleanup(WPARAM wparam)
{
	if (!Psettings || !Psettings->mEnableMixedDpiScalingWorkaround)
		return;

	HWND temp_hwnd = nullptr;
	{
		std::lock_guard<std::mutex> l(temp_windows_mutex);
		auto it = temp_windows.find((uint)wparam);
		if (it == temp_windows.end())
			return;
		temp_hwnd = it->second;
		temp_windows.erase(it);
	}

	if (temp_hwnd && ::IsWindow(temp_hwnd))
		::DestroyWindow(temp_hwnd);
}
