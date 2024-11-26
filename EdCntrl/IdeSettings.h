#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include "WTString.h"
#include "SpinCriticalSection.h"

class IdeSettings
{
  public:
	IdeSettings();
	~IdeSettings()
	{
	}

	// Cached values
	bool GetEditorBoolOption(LPCSTR lang, LPCSTR prop);
	int GetEditorIntOption(LPCSTR lang, LPCSTR prop);
	WTString GetEditorStringOption(LPCSTR lang, LPCSTR prop);
	CStringW GetVsStringOption(LPCSTR category, LPCSTR subCategory, LPCSTR prop);
	COLORREF GetColor(int index, COLORREF defaultValue = 0x00ffffff);
	COLORREF GetDteEditorColor(LPCWSTR colorName, BOOL foreground);
	COLORREF GetThemeTreeColor(LPCWSTR colorName, BOOL foreground);                    // VS11
	COLORREF GetEnvironmentColor(LPCWSTR colorName, BOOL foreground);                  // VS11
	COLORREF GetEditorColor(LPCWSTR colorName, BOOL foreground);                       // VS11
	COLORREF GetToolTipColor(BOOL foreground);                                         // VS11
	COLORREF GetThemeColor(REFGUID colorCategory, LPCWSTR colorName, BOOL foreground); // VS11
	COLORREF GetNewProjectDlgColor(LPCWSTR colorName, BOOL foreground);                // VS12
#ifdef RAD_STUDIO
	COLORREF GetRSSysColor(int index, COLORREF defaultValue = CLR_INVALID);
#endif
	GUID GetThemeID();
	void ResetCache();
	void CheckInit(bool force = false);
	bool AreVcSquigglesEnabled() const
	{
		return mVcSquigglesEnabled;
	}
	bool IsLocalized() const;
	LCID GetLocaleID() const;
	bool IsBlueVSColorTheme15(); // VS17
	bool IsDarkVSColorTheme();	// VS11+

	// Non cached values, might be slow
	CString GetEditorOption(LPCSTR lang, LPCSTR prop);
	BOOL SetEditorOption(LPCSTR lang, LPCSTR prop, LPCSTR val, CString* orgVal = NULL);
	BOOL GetStatementCompletionFont(LOGFONTW* outFont);
	BOOL GetEnvironmentFont(LOGFONTW* outFont);
	BOOL GetEnvironmentFontPointInfo(LOGFONTW* outFont);
	BOOL GetEditorFont(LOGFONTW* outFont);
	BOOL GetColorDirect(int index, COLORREF* defaultValue);
	CStringW GetVsOption(LPCSTR category, LPCSTR subCategory, LPCSTR prop);

  private:
	void SaveEditorBoolOption(const CString& val, const std::string& key, const std::string& subKey,
	                          bool defaultIfUnsupported = false);
	void SaveEditorIntOption(const CString& val, const std::string& key, const std::string& subKey,
	                         int defaultIfUnsupported = 0);
	void SaveEditorStringOption(const WTString& val, const std::string& key, const std::string& subKey);
	void SaveVsStringOption(const CStringW& val, const std::string& key, const std::string& subKey);
	BOOL GetVsFont(REFGUID vsCategory, LOGFONTW* outFont, bool useFontInfoSize = false);

  private:
	typedef std::pair<std::string, std::string> StringPair;
	typedef std::map<StringPair, WTString> StringOptions;
	StringOptions mEditorStringOptions;

	typedef std::map<StringPair, bool> BoolOptions;
	BoolOptions mEditorBoolOptions;

	typedef std::map<int, COLORREF> ColorOptions;
	ColorOptions mColorSettings;

	typedef std::map<StringPair, int> IntOptions;
	IntOptions mEditorIntOptions;

	typedef std::unordered_map<std::wstring, DWORD> DwordOptions;
	DwordOptions mThemeColors;
	CSpinCriticalSection mThemeColors_cs;

	typedef std::map<StringPair, CStringW> StringOptionsW;
	StringOptionsW mVsStringOptions;

#ifdef RAD_STUDIO
	std::vector<COLORREF> mRSSysColors;
#endif

	bool mMainThreadInitComplete;
	bool mVcSquigglesEnabled; // special-case so that parser thread doesn't hit cache
	GUID mThemeGuid;          // vs15+
};

extern IdeSettings* g_IdeSettings;
