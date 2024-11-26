// FontSettings.h: interface for the FontSettings class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FONTSETTINGS_H__0CCFB0F2_344D_11D2_9311_000000000000__INCLUDED_)
#define AFX_FONTSETTINGS_H__0CCFB0F2_344D_11D2_9311_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "VaAfxW.h"
#include <set>

class EdCnt;

enum class VaFontType : SBYTE
{
	None = -1,

	EnvironmentFont = 0,
	MenuFont,
	TTdisplayFont,
	ExpansionFont,
	MiniHelpFont,

	Count,
	MinValue = None
};

enum VaFontTypeFlags : BYTE
{
	VAFTF_None = 0,

	VAFTF_EnvironmentFont = 1 << 0,
	VAFTF_MenuFont = 1 << 1,
	VAFTF_TTdisplayFont = 1 << 2,
	VAFTF_ExpansionFont = 1 << 3,
	VAFTF_MiniHelpFont = 1 << 4,
	// 	VAFTF_Available = 1 << 5,
	// 	VAFTF_Available = 1 << 6,
	// 	VAFTF_Available = 1 << 7,

	VAFTF_All = 0xff,
};

enum VaFontStyle : BYTE
{
	FS_None = 0,

	FS_Italic = 1 << 1,
	FS_Bold = 1 << 2,
	FS_Underline = 1 << 3,
	// 	FS_Available = 1 << 4,
	// 	FS_Available = 1 << 5,
	// 	FS_Available = 1 << 6,
	// 	FS_Available = 1 << 7,
};

enum class VaFontUpdateType
{
	Quick,  // recreate if version, DPI or style differs
	Normal, // same as Quick but checks also LOGFONT
	Full,   // recreate always

	Default = Quick
};

class VaFont : public CFontW
{
	friend class VaFontInfo;
	DWORD key = 0;    // unique hash from Type, Dpi and Style (hex: 0xSSDDDDTT)
	UINT version = 0; // version of source VaFontInfo

  public:
	VaFont();

	static DWORD CreateKey(UINT dpi, VaFontType type, VaFontStyle style)
	{
		return ((DWORD)((int)type - ((int)VaFontType::MinValue)) & 0xff) | (((DWORD)dpi & 0xffff) << 8) |
		       (((DWORD)style & 0xff) << 24);
	}

	VaFontType GetFontType() const
	{
		return (VaFontType)((int)(key & 0x000000ff) + ((int)VaFontType::MinValue));
	}

	UINT GetDPI() const
	{
		return (key & 0x00ffff00) >> 8;
	}

	VaFontStyle GetFontStyle() const
	{
		return (VaFontStyle)((key & 0xff000000) >> 24);
	}

	// returns:
	// S_OK when font has changed
	// S_FALSE when font hasn't changed
	// E_FAIL when something has failed
	HRESULT Update(UINT dpi, VaFontType newType, VaFontStyle newStyle = FS_None,
	               VaFontUpdateType update = VaFontUpdateType::Default);

	// returns:
	// S_OK when font has changed
	// S_FALSE when font hasn't changed
	// E_FAIL when something has failed
	HRESULT UpdateForWindow(HWND dpiSource, VaFontType newType, VaFontStyle newStyle = FS_None,
	                        VaFontUpdateType update = VaFontUpdateType::Default);
};

class VaFontInfo
{
	friend class VaFont;

	LOGFONTW twipFont; // sizes are in TWIPs, "twentieth of an inch point" so 1 point = 20 TWIPs
	UINT version;

	HRESULT UpdateVaFont(VaFont& refFont, UINT dpi, VaFontType fontType, VaFontStyle style,
	                     VaFontUpdateType update) const;
	static bool LogFontsEqual(const LOGFONTW& left, const LOGFONTW& right);
	static int Round(float val);

  public:
	static UINT GetSystemDpi();

	static void TwipsToLogs(LOGFONTW& lf, UINT dpi);
	static void LogsToTwips(LOGFONTW& lf, UINT dpi);

	VaFontInfo() : twipFont(), version(0)
	{
	}

	bool UpdateFromLogs(const LOGFONTW& inLogFont, UINT dpi);
	bool UpdateFromTwips(const LOGFONTW& inTwipFont);

	void GetLogFont(LOGFONTW& logFont, UINT dpi, VaFontStyle style) const;
	HFONT GetCachedFont(UINT dpi, VaFontStyle style, bool lookupOnly) const;
};

class FontSettings
{
  public:
	CFontW m_TxtFont; // VS Editor font
	CFontW m_TxtFontBOLD;
	CFontW m_TxtFontItalic;

	struct Listener
	{
		Listener();
		virtual ~Listener();
		virtual void OnFontSettingsChanged(VaFontTypeFlags changed) = 0;
	};

	int m_tooltipWidth, m_origDesktopLeftEdge, m_origDesktopTopEdge, m_origDesktopRightEdge, m_origDesktopBottomEdge;

	CRect GetDesktopEdges(HWND hwnd = NULL) const;
	CRect GetDesktopEdges(EdCnt* ed) const;

	// There is a distinction between character height and line height that we
	// are not strictly enforcing.  The character height is font-specific.  The
	// line height app specific (ie, it might additional space between rows).
	// We are using the font's height as the default value for line height, then
	// update the value when the IDE actually tells us what the spacing is.
	// GetCharHeight returns this value, which should actually represent line height.
	//
	BOOL UpdateTextMetrics(HDC dc);
	BOOL CharSizeOk() const
	{
		return m_srcLineHeight && m_srcCharWidth && m_srcCharAscent;
	}
	LONG GetCharHeight() const
	{ /*_ASSERTE(0 != m_srcLineHeight);*/
		return m_srcLineHeight;
	}
	LONG GetCharWidth() const
	{ /*_ASSERTE(0 != m_srcCharWidth);*/
		return m_srcCharWidth;
	}
	LONG GetCharAscent() const
	{ /*_ASSERTE(0 != m_srcCharAscent);*/
		return m_srcCharAscent;
	}
	void SetLineHeight(LONG px)
	{
		if (px > 5)
			m_srcLineHeight = px;
	}

	// use VsUI::DpiHelper::LogicalToDeviceUnitsX() for scaling
	double GetDpiScaleX() const
	{
		return m_dpiScaleX;
	}
	double GetDpiScaleY() const
	{
		return m_dpiScaleY;
	}

	FontSettings();
	virtual ~FontSettings();
	void Update(BOOL invalidate = TRUE);
	void DualMonitorCheck();
	void RestrictRectToDesktop(RECT& rc) const;
	bool RestrictRectToMonitor(RECT& rc, HWND hwnd) const;
	bool RestrictRectToMonitor(RECT& rc, EdCnt* ed) const;
	bool RestrictRectToRect(RECT& rc, const CRect& constraint) const;
	void SetVS10FontSettings()
	{
		m_srcCharWidth = 0xa;
		m_srcLineHeight = 0x14;
		m_srcCharAscent = 0x1;
		extern int g_screenXOffset;
		g_screenXOffset = 0x22;
		extern int g_vaCharWidth[256];
		for (int i = 0; i < 256; i++)
			g_vaCharWidth[i] = 0xa;
	}

	int GetSettingsGeneration() const
	{
		return m_settingsGeneration;
	}

  private:
	LONG m_srcLineHeight;
	LONG m_srcCharWidth;
	LONG m_srcCharAscent; // in pixels, from top of font to font baseline.
	double m_dpiScaleX;
	double m_dpiScaleY;
	volatile LONG m_settingsGeneration; // will increase on each update

	CCriticalSection m_listenersCS;
	std::set<Listener*> m_listeners;

	void OnChanged(VaFontTypeFlags changed);
	void AddListener(Listener* listener);
	void RemoveListener(Listener* listener);
};

extern FontSettings* g_FontSettings;

// 1 point = 20 twips
bool GetEnvironmentFontTwips(LOGFONTW& outTwipFont);
bool GetIconTitleGUIFontTwips(LOGFONTW& outTwipFont);
bool GetDefaultGUIFontTwips(LOGFONTW& outTwipFont);

void UpdateFontCache();
void ClearFontCache();
HFONT GetFontVariant(HFONT font, VaFontStyle style = FS_None);
HFONT LookupFontVariant(HFONT font, VaFontStyle style = FS_None);
HFONT GetCachedFont(HFONT font);
HFONT GetCachedFont(const LOGFONTW& logFont);
HFONT LookupCachedFont(HFONT font);
HFONT LookupCachedFont(const LOGFONTW& logFont);
HFONT GetBoldFont(HFONT font);
HFONT GetItalicFont(HFONT font);

void ScaleSystemLogFont(LOGFONTW& logFont, UINT dpi);
void GetLogFont(LOGFONTW& logFont, UINT dpi, VaFontType fontType, VaFontStyle style = FS_None);
HFONT GetCachedFont(UINT dpi, VaFontType fontType, VaFontStyle style = FS_None);
HFONT LookupCachedFont(UINT dpi, VaFontType fontType, VaFontStyle style = FS_None);
HFONT GetCachedFontForWindow(HWND hWnd, VaFontType fontType, VaFontStyle style = FS_None);
HFONT LookupCachedFontForWindow(HWND hWnd, VaFontType fontType, VaFontStyle style = FS_None);
UINT GetIdealMinihelpHeight(HWND hWnd);

bool IsFontAffected(VaFontTypeFlags flags, VaFontType fontType);
void AddFontFlag(VaFontTypeFlags& flags, VaFontType fontType);
VaFontTypeFlags FontFlagFromType(VaFontType fontType);
void RemoveFontFlag(VaFontTypeFlags& flags, VaFontType fontType);

// turns positive lfHeight representing a cell size into height without internal leading
// returns true if conversion went good, returns false otherwise
// if lfHeight is negative, does nothing and returns true
bool NormalizeLogFontHeight(LOGFONTW& lf);

inline bool operator<(const LOGFONTW& left, const LOGFONTW& right)
{
	return memcmp(&left, &right, sizeof(left)) < 0;
}

inline bool operator==(const LOGFONTW& left, const LOGFONTW& right)
{
	return !memcmp(&left, &right, sizeof(left));
}

namespace stdext
{
#if _MSC_VER < 1400
// pre-VS2005
template <class _Init> inline size_t _Hash_value(_Init _Begin, _Init _End)
{ // hash range of elements
	size_t _Val = 2166136261U;
	while (_Begin != _End)
		_Val = 16777619U * _Val ^ (size_t)*_Begin++;
	return (_Val);
}
#endif

#if _MSC_VER < 1700
// pre-VS2012
template <> inline size_t hash_value<LOGFONTW>(const LOGFONTW& key)
{
	return _Hash_value((const unsigned int*)&key, (const unsigned int*)(&key + 1));
}
#elif _MSC_VER < 1911
// VS2012+
template <> inline size_t hash_value<LOGFONTW>(const LOGFONTW& key)
{
	return std::_Hash_seq((const unsigned char*)&key, sizeof(LOGFONTW));
}
#else
// VS2017 15.3+
// https://github.com/Microsoft/VCSamples/tree/master/VC2015Samples/_Hash_seq

#include "..\common\fnv1a.hpp"

template <> inline size_t hash_value<LOGFONTW>(const LOGFONTW& key)
{
	return fnv1a_hash_bytes((const unsigned char*)&key, sizeof(LOGFONTW));
}
#endif
} // namespace stdext

#endif // !defined(AFX_FONTSETTINGS_H__0CCFB0F2_344D_11D2_9311_000000000000__INCLUDED_)
