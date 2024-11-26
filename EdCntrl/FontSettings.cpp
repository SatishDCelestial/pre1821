// FontSettings.cpp: implementation of the FontSettings class.
//
//////////////////////////////////////////////////////////////////////
#include "stdafxed.h"
#include "EdCnt.h"
#include "FontSettings.h"
#include "settings.h"
//#include <MultiMon.h>
#include "DevShellAttributes.h"
#include "Registry.h"
#include "RegKeys.h"
#include "PROJECT.H"
#include "IdeSettings.h"
#include "HookCode.h"
#include "VaService.h"
#include "uilocale.h"

#ifdef RAD_STUDIO
#include "RadStudioPlugin.h"
#endif

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

FontSettings* g_FontSettings;
VaFontInfo vaFonts[(int)VaFontType::Count];

VaFontTypeFlags UpdateVaFonts(bool forceUpdate)
{
	static bool initialised = false;

	if (!forceUpdate && initialised)
		return VAFTF_None;

	VaFontTypeFlags changes = VAFTF_None;
	UINT defaultDpi = (UINT)VsUI::DpiHelper::GetDefault()->GetDeviceDpiY();

	// get any font only once per call
	LOGFONTW envTwipsFont, iconTitleTwipsFont, guiTwipsFont;
	bool haveEnvFont = GetEnvironmentFontTwips(envTwipsFont);
	bool haveIconTitleFont = GetIconTitleGUIFontTwips(iconTitleTwipsFont);
	GetDefaultGUIFontTwips(guiTwipsFont);

	{
		NONCLIENTMETRICSW nm;
		memset(&nm, 0, sizeof(nm));
		nm.cbSize = sizeof(NONCLIENTMETRICSW);
		bool haveNCMetrics = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, nm.cbSize, &nm, 0);

		if (haveNCMetrics)
		{
			// use tooltip font if available [LOGICAL]
			if (vaFonts[(int)VaFontType::TTdisplayFont].UpdateFromLogs(nm.lfStatusFont, defaultDpi))
				AddFontFlag(changes, VaFontType::TTdisplayFont);
		}
		else
		{
			// otherwise use default font [TWIPS]
			if (vaFonts[(int)VaFontType::TTdisplayFont].UpdateFromTwips(guiTwipsFont))
				AddFontFlag(changes, VaFontType::TTdisplayFont);
		}

		if (haveEnvFont)
		{
			// use IDE environment font for menu if available [TWIPS]
			if (vaFonts[(int)VaFontType::MenuFont].UpdateFromTwips(envTwipsFont))
				AddFontFlag(changes, VaFontType::MenuFont);
		}
		else if (haveNCMetrics)
		{
			// use system menu font if available [LOGICAL]
			if (vaFonts[(int)VaFontType::MenuFont].UpdateFromLogs(nm.lfMenuFont, defaultDpi))
				AddFontFlag(changes, VaFontType::MenuFont);
		}
		else if (haveIconTitleFont)
		{
			// use icon title font if available [TWIPS]
			if (vaFonts[(int)VaFontType::MenuFont].UpdateFromTwips(iconTitleTwipsFont))
				AddFontFlag(changes, VaFontType::MenuFont);
		}
		else
		{
			// otherwise use default font [TWIPS]
			if (vaFonts[(int)VaFontType::MenuFont].UpdateFromTwips(guiTwipsFont))
				AddFontFlag(changes, VaFontType::MenuFont);
		}
	}
	{
		LOGFONTW complLogFont;
		ZeroMemory((PVOID)&complLogFont, sizeof(LOGFONTW));

		bool haveComplFont = g_IdeSettings && g_IdeSettings->GetStatementCompletionFont(&complLogFont);
		if (haveComplFont)
		{
			// use IDE expansion box font if available [LOGICAL]
			if (vaFonts[(int)VaFontType::ExpansionFont].UpdateFromLogs(complLogFont, defaultDpi))
				AddFontFlag(changes, VaFontType::ExpansionFont);
		}
		else if (haveIconTitleFont)
		{
			// use icon title font if available [TWIPS]
			if (vaFonts[(int)VaFontType::ExpansionFont].UpdateFromTwips(iconTitleTwipsFont))
				AddFontFlag(changes, VaFontType::ExpansionFont);
		}
		else
		{
			// otherwise use default font [TWIPS]
			if (vaFonts[(int)VaFontType::ExpansionFont].UpdateFromTwips(guiTwipsFont))
				AddFontFlag(changes, VaFontType::ExpansionFont);
		}

		{
			if (haveEnvFont)
			{
				// use IDE environment font for menu if available [TWIPS]
				if (vaFonts[(int)VaFontType::EnvironmentFont].UpdateFromTwips(envTwipsFont))
					AddFontFlag(changes, VaFontType::EnvironmentFont);
			}
			else if (haveComplFont)
			{
				// use IDE expansion box font if available [LOGICAL]
				if (vaFonts[(int)VaFontType::EnvironmentFont].UpdateFromLogs(complLogFont, defaultDpi))
					AddFontFlag(changes, VaFontType::EnvironmentFont);
			}
			else if (haveIconTitleFont)
			{
				// use icon title font if available [TWIPS]
				if (vaFonts[(int)VaFontType::EnvironmentFont].UpdateFromTwips(iconTitleTwipsFont))
					AddFontFlag(changes, VaFontType::EnvironmentFont);
			}
			else
			{
				// otherwise use default font [TWIPS]
				if (vaFonts[(int)VaFontType::EnvironmentFont].UpdateFromTwips(guiTwipsFont))
					AddFontFlag(changes, VaFontType::EnvironmentFont);
			}
		}

		{
			if (haveEnvFont)
			{
				// use IDE environment font for menu if available [TWIPS]
				if (vaFonts[(int)VaFontType::MiniHelpFont].UpdateFromTwips(envTwipsFont))
					AddFontFlag(changes, VaFontType::MiniHelpFont);
			}
			else
			{
				// use default font with m_minihelpFontSize [TWIPS]
				LOGFONTW mhTwipsFont = haveIconTitleFont ? iconTitleTwipsFont : guiTwipsFont;
				mhTwipsFont.lfHeight = -(Psettings ? (LONG)Psettings->m_minihelpFontSize * 20 : 180);
				mhTwipsFont.lfWidth = 0;
				if (vaFonts[(int)VaFontType::MiniHelpFont].UpdateFromTwips(iconTitleTwipsFont))
					AddFontFlag(changes, VaFontType::MiniHelpFont);
			}
		}
	}

	initialised = true;
	return changes;
}

extern void MigrateDevColorInternal(HKEY hDevKey, int idx, EditColorStr* pColors);

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

FontSettings::FontSettings()
{
	m_srcLineHeight = 0;
	m_srcCharWidth = 0;
	m_srcCharAscent = 0;
	m_settingsGeneration = 0;
	m_dpiScaleX = 1.0;
	m_dpiScaleY = 1.0;

	HDC hDc = GetDC(NULL);
	if (hDc != NULL)
	{
		int dpiX = GetDeviceCaps(hDc, LOGPIXELSX);
		int dpiY = GetDeviceCaps(hDc, LOGPIXELSY);
		ReleaseDC(NULL, hDc);
		m_dpiScaleX = dpiX / 96.0;
		m_dpiScaleY = dpiY / 96.0;
	}

	Update(FALSE);

	// need to set default fonts - under normal circumstances these will be
	//  overridden by calls to Update(const LOGFONT)
	// But if user unloads and then reloads VA, we don't get the logfont calls
	NONCLIENTMETRICSW nm;
	nm.cbSize = sizeof(NONCLIENTMETRICSW);
	::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, nm.cbSize, &nm, 0);
	wcscpy(nm.lfStatusFont.lfFaceName, L"Tahoma");
	if (gShellAttr->SupportsCustomTooltipFont())
	{
		CStringW fname = ::GetRegValueW(HKEY_CURRENT_USER, _T(ID_RK_APP), "ToopTipFont");
		if (fname.GetLength())
		{
			int fsz = ::atoi(::GetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "ToopTipSize"));
			wcscpy(nm.lfStatusFont.lfFaceName, fname);
			nm.lfStatusFont.lfHeight = -::MulDiv(fsz, ::GetDeviceCaps(NULL, LOGPIXELSY), 72);
		}
	}
}

void FontSettings::Update(BOOL invalidate /*= TRUE*/)
{
	::InterlockedIncrement(&m_settingsGeneration);

	auto indexedChanged = UpdateVaFonts(true);

	// [case: 22788] VC++ doesn't use these colors; they're illegible
	// 	if (gDte2)
	// 	{
	// 		OLE_COLOR bk = 0;
	// 		OLE_COLOR text = 0;
	// 		if (SUCCEEDED(gDte2->GetThemeColor(EnvDTE80::vsThemeColorScreentipBackground, &bk)))
	// 			Psettings->m_TTbkColor = bk;
	// 		if (SUCCEEDED(gDte2->GetThemeColor(EnvDTE80::vsThemeColorScreentipText, &text)))
	// 			Psettings->m_TTtxtColor = text;
	// 	}

	// Get current font name from reg
	HKEY hKey;
#if defined(RAD_STUDIO)
	if (gVaRadStudioPlugin && Psettings)
	{
		LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, CString(gVaRadStudioPlugin->GetBaseRegPath()) + "\\Editor\\Options", 0, KEY_QUERY_VALUE, &hKey);
		if (ERROR_SUCCESS == err)
		{
			Psettings->ReadRegString(hKey, L"Editor Font", Psettings->m_srcFontName, L"Courier", 254);
			Psettings->ReadRegDword(hKey, "Font Size", &Psettings->FontSize, 10);
			RegCloseKey(hKey);
		}

// 		const CString tabs(
// 			GetRegValue(HKEY_CURRENT_USER, CString(gVaRadStudioPlugin->GetBaseRegPath()) + "\\Editor\\Source Options\\Borland.EditOptions.C", "Tab Stops"));
// 		if (!tabs.IsEmpty())
// 			Psettings->TabSize = (DWORD)::atoi(tabs);

		Psettings->TabSize = 1; // according to new API TAB counts as 1

		DualMonitorCheck();
		UpdateFontCache();
		OnChanged(indexedChanged);
	}
#else
	CString msdevKeynamePrefix(gShellAttr->GetFormatSourceWindowKeyName());
	// 	CString msdevKeynamePrefixOutputWnd(gShellAttr->GetFormatOutputWindowKeyName());
	if (msdevKeynamePrefix.IsEmpty() /*|| msdevKeynamePrefixOutputWnd.IsEmpty()*/)
	{
		DualMonitorCheck();
		UpdateFontCache();
		OnChanged(indexedChanged);
		return;
	}

	// read msdev's font settings
	LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, msdevKeynamePrefix, 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		Psettings->ReadRegString(hKey, ID_RK_FONTFACE, Psettings->m_srcFontName, L"Courier", 254);
		Psettings->ReadRegDword(hKey, _T(ID_RK_FONTSIZE), &Psettings->FontSize, 10);
		// nab text bg for bold bracing
		MigrateDevColorInternal(hKey, C_Text, Psettings->m_colors);
		MigrateDevColorInternal(hKey, C_Comment, Psettings->m_colors);
		MigrateDevColorInternal(hKey, C_String, Psettings->m_colors);
		MigrateDevColorInternal(hKey, C_Operator, Psettings->m_colors);
		MigrateDevColorInternal(hKey, C_Number, Psettings->m_colors);
		MigrateDevColorInternal(hKey, C_Keyword, Psettings->m_colors);
		RegCloseKey(hKey);
	}

	DualMonitorCheck();
	UpdateFontCache();
	OnChanged(indexedChanged);

	if (invalidate && MainWndH)
	{
		CRect r;
		GetWindowRect(MainWndH, &r);
		::InvalidateRect(MainWndH, &r, FALSE);
	}
#endif
}

void FontSettings::DualMonitorCheck()
{
	CRect workAreaRC;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRC, 0);

	if (MainWndH)
	{
		HMONITOR hMonitor = MonitorFromWindow(MainWndH, MONITOR_DEFAULTTONULL);
		if (hMonitor && hMonitor != MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY))
		{
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			if (GetMonitorInfo(hMonitor, &mi))
			{
				workAreaRC = mi.rcWork;
			}
		}
	}

	const int frameWidth = GetSystemMetrics(SM_CXFRAME) * 2;
	workAreaRC.DeflateRect(frameWidth, frameWidth); // gmit: why deflate vertical dimension?!

	m_origDesktopRightEdge = workAreaRC.right;
	m_origDesktopBottomEdge = workAreaRC.bottom;
	m_origDesktopTopEdge = workAreaRC.top;
	m_origDesktopLeftEdge = workAreaRC.left;
	m_tooltipWidth = (workAreaRC.Width() /* * .6 */); // tooltip could be full width of screen
}

FontSettings::~FontSettings()
{
}

CRect FontSettings::GetDesktopEdges(HWND hwnd) const
{
	CRect ret(m_origDesktopLeftEdge, m_origDesktopTopEdge, m_origDesktopRightEdge, m_origDesktopBottomEdge);

	if (hwnd)
	{
		HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
		if (mon)
		{
			MONITORINFO mi;
			memset(&mi, 0, sizeof(mi));
			mi.cbSize = sizeof(mi);
			if (::GetMonitorInfo(mon, &mi))
			{
				ret = mi.rcWork;

				CRect wrect;
				::GetWindowRect(hwnd, wrect);
				if ((wrect & ret) != wrect)
				{ // we're in trouble; window is displayed on two monitors!
					CWnd* wnd = CWnd::FromHandle(hwnd);
					if (wnd && wnd->GetRuntimeClass()->IsDerivedFrom(RUNTIME_CLASS(EdCnt)))
					{ // caret API is weird and incomplete, so, we'll constrain us only to editor windows
						CPoint caret(0, 0);
						if (::GetCaretPos(&caret) && (caret.x || caret.y))
						{
							wnd->ClientToScreen(&caret);
							if (wrect.PtInRect(caret))
							{ // caret pos is within the editor window; try to obtain monitor from caret point
								mon = ::MonitorFromPoint(caret, MONITOR_DEFAULTTONULL);
								if (mon)
								{
									MONITORINFO mi2 = {sizeof(mi2)};
									if (::GetMonitorInfo(mon, &mi2))
										ret = mi2.rcWork;
								}
							}
						}
					}
				}
			}
		}
	}

	return ret;
}

CRect FontSettings::GetDesktopEdges(EdCnt* ed) const
{
	if (!ed)
		return GetDesktopEdges((HWND)NULL);
	if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
		return GetDesktopEdges(ed->m_hWnd);

	CRect ret(m_origDesktopLeftEdge, m_origDesktopTopEdge, m_origDesktopRightEdge, m_origDesktopBottomEdge);

	if (!ed->m_hWnd)
		return ret;
	CRect wrect;
	ed->vGetClientRect(&wrect);
	ed->vClientToScreen(&wrect);
	HMONITOR mon = ::MonitorFromRect(&wrect, MONITOR_DEFAULTTONULL);
	if (!mon)
		return ret;

	MONITORINFO mi = {sizeof(mi)};
	if (::GetMonitorInfo(mon, &mi))
	{
		ret = mi.rcWork;

		if ((wrect & ret) != wrect)
		{ // we're in trouble; window is displayed on two monitors!
			CPoint caret = ed->vGetCaretPos();
			if (caret.x || caret.y)
			{
				ed->vClientToScreen(&caret);
				if (wrect.PtInRect(caret))
				{ // caret pos is within the editor window; try to obtain monitor from caret point
					mon = ::MonitorFromPoint(caret, MONITOR_DEFAULTTONULL);
					if (mon)
					{
						MONITORINFO mi2 = {sizeof(mi2)};
						if (::GetMonitorInfo(mon, &mi2))
							ret = mi2.rcWork;
					}
				}
			}
		}
	}

	return ret;
}

void FontSettings::RestrictRectToDesktop(RECT& rc) const
{
	// make sure rc is not offscreen or obscured by the taskbar
	RestrictRectToMonitor(rc, (HWND)NULL);
}

bool FontSettings::RestrictRectToMonitor(RECT& rc, HWND hwnd) const
{
	return RestrictRectToRect(rc, GetDesktopEdges(hwnd));
}

bool FontSettings::RestrictRectToMonitor(RECT& rc, EdCnt* ed) const
{
	return RestrictRectToRect(rc, GetDesktopEdges(ed));
}

bool FontSettings::RestrictRectToRect(RECT& rc, const CRect& constraint) const
{
	bool retval = false;

	if (rc.top < constraint.top)
	{
		::OffsetRect(&rc, 0, constraint.top - rc.top);
		retval = true;
	}

	if (rc.bottom > constraint.bottom)
	{
		retval = true;
		::OffsetRect(&rc, 0, -(rc.bottom - constraint.bottom));
		if (rc.top < constraint.top)
		{
			rc.top = constraint.top;
			rc.bottom = constraint.bottom;
		}
	}

	if (rc.left < constraint.left)
	{
		retval = true;
		::OffsetRect(&rc, constraint.left - rc.left, 0);
	}

	if (rc.right > constraint.right)
	{
		retval = true;
		::OffsetRect(&rc, -(rc.right - constraint.right), 0);
		if (rc.left < constraint.left)
		{
			rc.left = constraint.left;
			rc.right = constraint.right;
		}
	}

	return retval;
}

void FontSettings::OnChanged(VaFontTypeFlags changed)
{
	if (g_FontSettings != this) // mostly null check
		return;

	if (::InterlockedCompareExchange(&m_settingsGeneration, 0, 0) <= 1)
		return;

	AutoLockCs _lock(m_listenersCS);

	for (auto listener : m_listeners)
		listener->OnFontSettingsChanged(changed);
}

void FontSettings::AddListener(Listener* listener)
{
	AutoLockCs _lock(m_listenersCS);
	m_listeners.insert(listener);
}

void FontSettings::RemoveListener(Listener* listener)
{
	AutoLockCs _lock(m_listenersCS);
	m_listeners.erase(listener);
}

BOOL FontSettings::UpdateTextMetrics(HDC dc)
{
	TEXTMETRICW tm;
	BOOL b = GetTextMetricsW(dc, &tm);
	if (b)
	{
		if (gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder())
			m_srcLineHeight = tm.tmHeight + tm.tmExternalLeading;
		m_srcCharWidth = tm.tmAveCharWidth;
		m_srcCharAscent = tm.tmAscent;
	}
	else
	{
		_ASSERTE(!"FontSettings::UpdateTextMetrics");
		m_srcLineHeight = 0;
		m_srcCharWidth = 0;
		m_srcCharAscent = 0;
	}
	return b;
}

FontSettings::Listener::Listener()
{
	if (g_FontSettings)
		g_FontSettings->AddListener(this);
}

FontSettings::Listener::~Listener()
{
	if (g_FontSettings)
		g_FontSettings->RemoveListener(this);
}

class FontCache
{
	int mGen = 0;
	std::map<LOGFONTW, HFONT> mMap;
	std::map<DWORD, HFONT> mMapIndexed;
	CCriticalSection mCS;

  public:
	~FontCache()
	{
		Clean();
	}

	bool IsEmpty()
	{
		AutoLockCs _lock(mCS);
		return mMap.empty();
	}

	void OnFontSettingsUpdated()
	{
		if (!g_FontSettings)
			return;

		AutoLockCs _lock(mCS);

		if (mGen == g_FontSettings->GetSettingsGeneration())
			return;

		if (!mMap.empty())
		{
			for (const auto& it : mMap)
				::DeleteObject(it.second);

			mMap.clear();
		}

		mMapIndexed.clear();
		mGen = g_FontSettings->GetSettingsGeneration();
	}

	void Clean()
	{
		if (IsEmpty() || !g_FontSettings)
			return;

		AutoLockCs _lock(mCS);

		if (mMap.empty())
			return;

		for (const auto& it : mMap)
			::DeleteObject(it.second);

		mMapIndexed.clear();
		mMap.clear();
		mGen = 0;
	}

	void SetIndexed(UINT dpi, VaFontType fontType, VaFontStyle style, HFONT font)
	{
		mMapIndexed[VaFont::CreateKey(dpi, fontType, style)] = font;
	}

	HFONT GetIndexed(UINT dpi, VaFontType fontType, VaFontStyle style)
	{
		auto it = mMapIndexed.find(VaFont::CreateKey(dpi, fontType, style));

		if (it != mMapIndexed.end())
			return it->second;

		return nullptr;
	}

	HFONT GetFont(const LOGFONTW& lf, bool lookupOnly)
	{
		AutoLockCs _lock(mCS);

		if (mGen == 0 && g_FontSettings)
			mGen = g_FontSettings->GetSettingsGeneration();

		auto it = mMap.find(lf);
		if (it != mMap.end())
		{
			HFONT ret = it->second;

			if (ret && GetObjectType(ret) == OBJ_FONT)
				return ret;
		}

		if (lookupOnly)
			return nullptr;

		if (mMap.size() > 150) // just in case (should not happen with new handling)
			Clean();

		HFONT newFont = ::CreateFontIndirectW(&lf);

		if (newFont)
			mMap[lf] = newFont;

		return newFont;
	}

	HFONT GetFontVariant(HFONT font, VaFontStyle style, bool lookupOnly)
	{
		AutoLockCs _lock(mCS);

		if (mGen == 0 && g_FontSettings)
			mGen = g_FontSettings->GetSettingsGeneration();

		LOGFONTW lf;
		memset(&lf, 0, sizeof(lf));
		if (!::GetObjectW(font, sizeof(lf), &lf))
		{
			_ASSERTE(!"GetObjectW failed in GetFontVariant!");
			return font;
		}

		if ((style & FS_Bold))
			lf.lfWeight = FW_BOLD;

		if ((style & FS_Underline))
			lf.lfUnderline = 1;

		if ((style & FS_Italic))
			lf.lfItalic = 1;

		return GetFont(lf, lookupOnly);
	}
};

FontCache fontCache;

void UpdateFontCache()
{
	fontCache.OnFontSettingsUpdated();
}

void ClearFontCache()
{
	fontCache.Clean();
}

HFONT GetFontVariant(HFONT font, VaFontStyle style /*= FS_None*/)
{
	return fontCache.GetFontVariant(font, style, false);
}

HFONT LookupFontVariant(HFONT font, VaFontStyle style /*= FS_None*/)
{
	return fontCache.GetFontVariant(font, style, true);
}

HFONT GetCachedFont(HFONT font)
{
	return fontCache.GetFontVariant(font, FS_None, false);
}

HFONT GetCachedFont(const LOGFONTW& logFont)
{
	return fontCache.GetFont(logFont, false);
}

const VaFontInfo& GetVaFontData(VaFontType fontType)
{
	UpdateVaFonts(false);
	return vaFonts[(int)fontType];
}

HFONT GetCachedFont(UINT dpi, VaFontType fontType, VaFontStyle style /*= FS_None*/)
{
	HFONT hFont = fontCache.GetIndexed(dpi, fontType, style);

	if (!hFont)
	{
		UpdateVaFonts(false);
		hFont = GetVaFontData(fontType).GetCachedFont(dpi, style, true);

		if (hFont)
			fontCache.SetIndexed(dpi, fontType, style, hFont);
	}

	return hFont;
}

HFONT LookupCachedFont(HFONT font)
{
	return fontCache.GetFontVariant(font, FS_None, true);
}

HFONT LookupCachedFont(const LOGFONTW& logFont)
{
	return fontCache.GetFont(logFont, true);
}

HFONT LookupCachedFont(UINT dpi, VaFontType fontType, VaFontStyle style /*= FS_None*/)
{
	HFONT hFont = fontCache.GetIndexed(dpi, fontType, style);

	if (!hFont)
	{
		UpdateVaFonts(false);
		hFont = GetVaFontData(fontType).GetCachedFont(dpi, style, true);
	}

	return hFont;
}

HFONT GetCachedFontForWindow(HWND hWnd, VaFontType fontType, VaFontStyle style /*= FS_None*/)
{
	return GetCachedFont(VsUI::CDpiAwareness::GetDpiForWindow(hWnd), VaFontType::EnvironmentFont, style);
}

HFONT LookupCachedFontForWindow(HWND hWnd, VaFontType fontType, VaFontStyle style /*= FS_None*/)
{
	return LookupCachedFont(VsUI::CDpiAwareness::GetDpiForWindow(hWnd), VaFontType::EnvironmentFont, style);
}

struct MinihelpHeightMap
{
	int mGen = -1;
	CCriticalSection mCS;
	std::map<UINT, UINT> mMap;

	UINT GetHeight(HWND hWnd)
	{
		if (g_FontSettings && gImgListMgr)
		{
			if (hWnd && ::IsWindow(hWnd))
			{
				HWND parent = ::GetParent(hWnd);
				while (parent)
				{
					hWnd = parent;
					parent = ::GetParent(hWnd);
				}
			}

			CWnd* pWnd = (hWnd && ::IsWindow(hWnd)) ? CWnd::FromHandle(hWnd) : AfxGetMainWnd();

			if (pWnd)
			{
				UINT dpi = VsUI::CDpiAwareness::GetDpiForWindow(*pWnd);

				AutoLockCs _lock(mCS);

				if (mGen == g_FontSettings->GetSettingsGeneration())
				{
					// we are good just with DPI difference, 
					// because font differences are handled by generation
					// and theme does not matter so icons depend only on DPI

					auto found = mMap.find(dpi);
					if (found != mMap.cend())
					{
						return found->second;
					}
				}
				else
				{
					mMap.clear();
					mGen = g_FontSettings->GetSettingsGeneration();
				}

				LOGFONTW lf;
				GetLogFont(lf, dpi, VaFontType::MiniHelpFont, VaFontStyle::FS_None);
				HFONT font = GetCachedFont(lf);

				if (pWnd && font)
				{
					UINT height = 0;

					CComboBoxEx tmpCombo;

					for (uint cnt = 0; cnt < 10; ++cnt)
					{
						if (tmpCombo.Create(WS_CHILD | CBS_DROPDOWN, CRect(0, 0, 100, 100), pWnd, 1))
							break;

						Sleep(100 + (cnt * 50));
					}
												
					if (tmpCombo.m_hWnd)
					{
						gImgListMgr->SetImgListForDPI(tmpCombo, ImageListManager::bgCombo);
						tmpCombo.SendMessage(WM_SETFONT, (WPARAM)font, 0);

						CWnd* combo = tmpCombo.GetComboBoxCtrl();
						if (combo)
						{
							CRect rect;
							combo->GetWindowRect(&rect);
							height = rect.Height();
							mMap.emplace(dpi, height);
						}
						else
						{
							_ASSERTE(!"Failed to get minihelp height 2");
						}

						tmpCombo.DestroyWindow();
					}
					else
					{
						_ASSERTE(!"Failed to get minihelp height 3");
					}

					return height;
				}
			}
		}

		_ASSERTE(!"Failed to get minihelp height 4");

		return 0;
	}

} minihelpHeightMap;

UINT GetIdealMinihelpHeight(HWND hWnd)
{
	return minihelpHeightMap.GetHeight(hWnd);
}

HFONT GetBoldFont(HFONT font)
{
	return fontCache.GetFontVariant(font, FS_Bold, false);
}

HFONT GetItalicFont(HFONT font)
{
	return fontCache.GetFontVariant(font, FS_Italic, false);
}

void ScaleSystemLogFont(LOGFONTW& logFont, UINT dpi)
{
	VaFontInfo pf;
	pf.UpdateFromLogs(logFont, 0); // 0 means System DPI
	pf.GetLogFont(logFont, dpi, FS_None);
}

void GetLogFont(LOGFONTW& logFont, UINT dpi, VaFontType fontType, VaFontStyle style /*= FS_None*/)
{
	return GetVaFontData(fontType).GetLogFont(logFont, dpi, style);
}

bool IsFontAffected(VaFontTypeFlags flags, VaFontType fontType)
{
	if ((int)fontType >= 0)
		return flags & (1 << (DWORD)fontType);

	return false;
}

void AddFontFlag(VaFontTypeFlags& flags, VaFontType fontType)
{
	if ((int)fontType >= 0)
		flags = (VaFontTypeFlags)((DWORD)flags | (1 << (DWORD)fontType));
}

VaFontTypeFlags FontFlagFromType(VaFontType fontType)
{
	VaFontTypeFlags flags = VAFTF_None;
	AddFontFlag(flags, fontType);
	return flags;
}

void RemoveFontFlag(VaFontTypeFlags& flags, VaFontType fontType)
{
	if ((int)fontType >= 0)
		flags = (VaFontTypeFlags)((DWORD)flags & ~(1 << (DWORD)fontType));
}

bool NormalizeLogFontHeight(LOGFONTW& lf)
{
	if (lf.lfHeight < 0)
		return true;

	bool status = false;

	HDC tmDC = ::GetDC(nullptr);
	if (tmDC)
	{
		HFONT hFont = ::CreateFontIndirectW(&lf);
		if (hFont)
		{
			auto oldMode = ::SetMapMode(tmDC, MM_TEXT);
			auto oldFont = ::SelectObject(tmDC, hFont);
			//			float px2twip = 1440.0f / (float)GetDeviceCaps(tmDC, LOGPIXELSY);

			TEXTMETRICW tm;
			::ZeroMemory(&tm, sizeof(TEXTMETRICW));

			if (::GetTextMetricsW(tmDC, &tm))
			{
				status = true;
				lf.lfHeight = -(tm.tmHeight - tm.tmInternalLeading);
			}

			::SetMapMode(tmDC, oldMode);
			::SelectObject(tmDC, oldFont);
			::DeleteObject(hFont);
		}

		::ReleaseDC(nullptr, tmDC);
	}

	return status;
}

bool GetEnvironmentFontTwips(LOGFONTW& outTwipFont)
{
	ZeroMemory((PVOID)&outTwipFont, sizeof(LOGFONTW));

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		if (gVaInteropService)
		{
			// for WPF Windows these resources are suggested way to get Environment font
			// https://docs.microsoft.com/en-us/visualstudio/extensibility/ux-guidelines/fonts-and-formatting-for-visual-studio?view=vs-2019

			// I consider this way to be safest as we get info directly in device independent pixels,
			// which can be converted according to required DPI...
			// Other ways require the device context, which is not specified.
			variant_t ff, fs;
			if (gVaInteropService->TryFindResource(L"VsFont.EnvironmentFontFamily", L"string", &ff) &&
			    gVaInteropService->TryFindResource(L"VsFont.EnvironmentFontSize", L"double", &fs))
			{
				wcscpy_s(outTwipFont.lfFaceName, LF_FACESIZE, ff.bstrVal);

				// convert to TWIPs from device independent WPF pixels
				outTwipFont.lfHeight = -(LONG)(0.5 + (fs.dblVal * (1440.0 / 96.0)));
				outTwipFont.lfWeight = FW_NORMAL;
				return true;
			}
		}

		// if other ways failed, this will work...
		// requires the device context, which is not specified, so we use screen DC (default DPI) to convert into TWIPs
		if (g_IdeSettings && g_IdeSettings->GetEnvironmentFont(&outTwipFont))
		{
			// convert to TWIPs using default DPI
			VaFontInfo::LogsToTwips(outTwipFont, (UINT)VsUI::DpiHelper::GetDefault()->GetDeviceDpiY());
			return true;
		}
	}

	return false;
}

bool GetDefaultGUIFontTwips(LOGFONTW& outTwipFont)
{
	ZeroMemory((PVOID)&outTwipFont, sizeof(LOGFONTW));

	if (::GetObjectW(::GetStockObject(DEFAULT_GUI_FONT), sizeof(outTwipFont), &outTwipFont))
	{
		VaFontInfo::LogsToTwips(outTwipFont, (UINT)VsUI::DpiHelper::GetDefault()->GetDeviceDpiY());
		return true;
	}

	// last option - not likely to happen
	auto faceName = L"MS Sans Serif";
	wcscpy_s(outTwipFont.lfFaceName, wcslen(faceName), faceName);
	outTwipFont.lfHeight = -160; // 8 points
	outTwipFont.lfWeight = FW_NORMAL;
	return false;
}

bool GetIconTitleGUIFontTwips(LOGFONTW& outTwipFont)
{
	ZeroMemory((PVOID)&outTwipFont, sizeof(LOGFONTW));

	// get LOGFONT structure for the icon font
	if (SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTW), &outTwipFont, 0))
	{
		VaFontInfo::LogsToTwips(outTwipFont, (UINT)VsUI::DpiHelper::GetDefault()->GetDeviceDpiY());
		return true;
	}

	return false;
}

UINT VaFontInfo::GetSystemDpi()
{
	return (UINT)VsUI::DpiHelper::GetDefault()->GetDeviceDpiY();
}

bool VaFontInfo::UpdateFromTwips(const LOGFONTW& inTwipFont)
{
	if (LogFontsEqual(inTwipFont, twipFont))
		return false;

	twipFont = inTwipFont;
	version++;

	return true;
}

bool VaFontInfo::UpdateFromLogs(const LOGFONTW& inLogFont, UINT dpi /*= 0*/)
{
	LOGFONTW twFont = inLogFont;
	LogsToTwips(twFont, dpi);
	return UpdateFromTwips(twFont);
}

void VaFontInfo::GetLogFont(LOGFONTW& logFont, UINT dpi, VaFontStyle style /*= FS_None*/) const
{
	logFont = twipFont;
	TwipsToLogs(logFont, dpi);

	if ((style & FS_Bold))
		logFont.lfWeight = FW_BOLD;

	if ((style & FS_Underline))
		logFont.lfUnderline = 1;

	if ((style & FS_Italic))
		logFont.lfItalic = 1;
}

bool VaFontInfo::LogFontsEqual(const LOGFONTW& left, const LOGFONTW& right)
{
	return !memcmp(&left, &right, sizeof(right));
}

void VaFontInfo::TwipsToLogs(LOGFONTW& lf, UINT dpi /*= 0*/)
{
	if (!dpi)
		dpi = GetSystemDpi();

	float twips2px = (float)dpi / 1440.0f;

	lf.lfHeight = Round(twips2px * (float)lf.lfHeight);
	lf.lfWidth = Round(twips2px * (float)lf.lfWidth);
}

void VaFontInfo::LogsToTwips(LOGFONTW& lf, UINT dpi /*= 0*/)
{
	if (!dpi)
		dpi = GetSystemDpi();

	NormalizeLogFontHeight(lf);

	float px2twips = 1440.0f / (float)dpi;

	lf.lfHeight = Round(px2twips * (float)lf.lfHeight);
	lf.lfWidth = Round(px2twips * (float)lf.lfWidth);
}

int VaFontInfo::Round(float val)
{
	return val >= 0 ? (int)(val + 0.5) : (int)(val - 0.5);
}

HFONT VaFontInfo::GetCachedFont(UINT dpi, VaFontStyle style, bool lookupOnly) const
{
	LOGFONTW lf;
	GetLogFont(lf, dpi, style);
	return lookupOnly ? ::LookupCachedFont(lf) : ::GetCachedFont(lf);
}

HRESULT VaFontInfo::UpdateVaFont(VaFont& refFont, UINT dpi, VaFontType fontType, VaFontStyle style,
                                 VaFontUpdateType update) const
{
	if (refFont.m_hObject && refFont.version == version && refFont.GetFontType() == fontType &&
	    refFont.GetDPI() == dpi && refFont.GetFontStyle() == style)
	{
		if (update == VaFontUpdateType::Quick)
		{
			if (::GetObjectType(refFont) == OBJ_FONT)
				return S_FALSE;
		}
		else if (update == VaFontUpdateType::Normal)
		{
			LOGFONTW tmp1;
			if (::GetObjectW(refFont, sizeof(tmp1), &tmp1))
			{
				LOGFONTW tmp2;
				GetLogFont(tmp2, dpi, style);

				if (LogFontsEqual(tmp1, tmp2))
					return S_FALSE;
			}
		}
	}

	if (refFont.m_hObject)
		refFont.DeleteObject();

	LOGFONTW lf;
	GetLogFont(lf, dpi, style);

	if (refFont.Attach(CreateFontIndirectW(&lf)))
	{
		refFont.key = VaFont::CreateKey(dpi, fontType, style);
		refFont.version = version;
		return S_OK;
	}

	return E_FAIL;
}

VaFont::VaFont()
{
	if (g_FontSettings && gShellAttr && gShellAttr->IsDevenv10OrHigher() &&
	    !g_FontSettings->GetSettingsGeneration())
	{
		g_FontSettings->Update();
	}
}

HRESULT VaFont::Update(UINT dpi, VaFontType newType, VaFontStyle newStyle /*= FS_None*/, VaFontUpdateType update)
{
	return GetVaFontData(newType).UpdateVaFont(*this, dpi, newType, newStyle, update);
}

HRESULT VaFont::UpdateForWindow(HWND dpiSource, VaFontType newType, VaFontStyle newStyle /*= FS_None*/,
                                VaFontUpdateType update)
{
	return Update(VsUI::CDpiAwareness::GetDpiForWindow(dpiSource), newType, newStyle, update);
}
