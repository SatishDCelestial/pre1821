#include "stdafxed.h"
#include "IdeSettings.h"
#include "VaMessages.h"
#include "PROJECT.H"
#include "VaService.h"
#include "vsshell110.h"
#include "initguid.h"
#include "ColorListControls.h"
#include "DevShellAttributes.h"
#include "WTComBSTR.h"
#include "LOG.H"
#include "Settings.h"
#include "Usage.h"
#include "textmgr2.h"
#include "Registry.h"
#include "VSIP\16.0\VisualStudioIntegration\Common\Inc\textmgr110.h"

#ifdef RAD_STUDIO
#include "RadStudioPlugin.h"
#include "CppBuilder.h"
#endif

IdeSettings* g_IdeSettings = NULL;

DEFINE_GUID(GUID_TextEditorCategory, 0xA27B4E24, 0xA735, 0x4D1D, 0xb8, 0xe7, 0x97, 0x16, 0xE1, 0xE3, 0xD8, 0xE0);
DEFINE_GUID(GUID_StatementCompletionCategory, 0xC1614BB1, 0x734F, 0x4A31, 0xBD, 0x42, 0x5A, 0xE6, 0x27, 0x5E, 0x16,
            0xD2);
DEFINE_GUID(GUID_VsTreeViewCategory, 0x92ecf08e, 0x8b13, 0x4cf4, 0x99, 0xe9, 0xae, 0x26, 0x92, 0x38, 0x21, 0x85);
DEFINE_GUID(GUID_VsEnvironmentCategory, 0x624ed9c3, 0xbdfd, 0x41fa, 0x96, 0xc3, 0x7c, 0x82, 0x4e, 0xa3, 0x2e, 0x3d);
DEFINE_GUID(GUID_EditorTooltipCategory, 0xA9A5637F, 0xB2A8, 0x422E, 0x8F, 0xB5, 0xDF, 0xB4, 0x62, 0x5F, 0x01, 0x11);
DEFINE_GUID(GUID_VsEnvironmentFontCategory, 0x1F987C00, 0xE7C4, 0x4869, 0x8a, 0x17, 0x23, 0xFD, 0x60, 0x22, 0x68, 0xB0);
DEFINE_GUID(GUID_VsNewProjectDlgCategory, 0xc36c426e, 0x31c9, 0x4048, 0x84, 0xcf, 0x31, 0xc1, 0x11, 0xd6, 0x5e, 0xc0);
DEFINE_GUID(GUID_CppLang, 0xB2F072B0, 0xABC1, 0x11D0, 0x9D, 0x62, 0x00, 0xC0, 0x4F, 0xD9, 0xDF, 0xD9);

DEFINE_GUID(GUID_ThemeDark, 0x1ded0138, 0x47ce, 0x435e, 0x84, 0xef, 0x9e, 0xc1, 0xf4, 0x39, 0xb7, 0x49);
DEFINE_GUID(GUID_ThemeLight, 0xde3dbbcd, 0xf642, 0x433c, 0x83, 0x53, 0x8f, 0x1d, 0xf4, 0x37, 0x0a, 0xba);
DEFINE_GUID(GUID_ThemeBlue, 0xa4d6a176, 0xb948, 0x4b29, 0x8c, 0x66, 0x53, 0xc9, 0x7a, 0x1e, 0xd7, 0xd0);
DEFINE_GUID(GUID_ThemeAdditionalContrast, 0xce94d289, 0x8481, 0x498b, 0x8c, 0xa9, 0x9b, 0x61, 0x91, 0xa3, 0x15, 0xb9);

#ifdef AVR_STUDIO
DEFINE_GUID(GUID_GccLang, 0xBC47FA77, 0xB642, 0x4aba, 0xAB, 0xBF, 0x80, 0x12, 0xC3, 0xFD, 0xD0, 0x96);
#endif

IdeSettings::IdeSettings() : mMainThreadInitComplete(false), mVcSquigglesEnabled(false)
{
#ifndef AVR_STUDIO
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		mVcSquigglesEnabled = true; // by default, will confirm on file open
#endif
}

BOOL IdeSettings::GetVsFont(REFGUID vsCategory, LOGFONTW* outFont, bool useFontInfoSize /*= false*/)
{
	if (!gPkgServiceProvider)
	{
		vLogUnfiltered("ERROR: GetVsFont no pkgSvcProvider");
		return FALSE;
	}

	IUnknown* tmp = NULL;

	// get the item from the font and color storage
	tmp = NULL;
	HRESULT res =
	    gPkgServiceProvider->QueryService(SID_SVsFontAndColorStorage, IID_IVsFontAndColorStorage, (void**)&tmp);
	if (tmp)
	{
		CComQIPtr<IVsFontAndColorStorage> fontColorStorage = tmp;
		if (fontColorStorage)
		{
			// we are only reading, no need for propagation
			// note here we don't want AutoColors - just concrete colors - different than when we update VS
			// flags: http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.__fcstorageflags.aspx
			res = fontColorStorage->OpenCategory(vsCategory, FCSF_LOADDEFAULTS | FCSF_READONLY | FCSF_NOAUTOCOLORS);
			if (SUCCEEDED(res))
			{
				res = fontColorStorage->GetFont(outFont, nullptr);
				if (SUCCEEDED(res))
				{
					CStringW face(outFont->lfFaceName);
					if (face.IsEmpty() || face == L"Automatic" || face == L" ")
					{
						res = E_FAIL;
						vLog("ERROR: GetVsFont GetFont fail(a) %s", (const char*)CString(face));
					}

					if (useFontInfoSize)
					{
						// [case: 74697]
						// don't use this for dialog templates (?)
						FontInfo fi;
						ZeroMemory(&fi, sizeof(fi));
						HRESULT res2 = fontColorStorage->GetFont(NULL, &fi);
						if (SUCCEEDED(res2) && fi.bPointSizeValid)
							outFont->lfHeight = fi.wPointSize;
						else
							outFont->lfHeight = 9;

						if (SUCCEEDED(res2) && fi.bFaceNameValid)
						{
							// [case: 102960]
							::SysFreeString(fi.bstrFaceName);
						}
					}
				}

				if (!SUCCEEDED(res))
				{
					FontInfo fi;
					ZeroMemory(&fi, sizeof(fi));
					res = fontColorStorage->GetFont(NULL, &fi);
					if (SUCCEEDED(res) && fi.bFaceNameValid)
					{
						// http://msdn.microsoft.com/en-us/library/vstudio/microsoft.visualstudio.shell.interop.ivsfontandcolorstorage.getfont%28v=vs.100%29.aspx
						// Either pLOGFONT or pInfo may be NULL (C++) or nullptr (C#).
						// The only data retrieved is the face name, character set, and point size.
						// In C++, the face name is a BSTR that must be freed by the client.

						CStringW face(fi.bstrFaceName);
						if (face.IsEmpty() || face == L"Automatic" || face == L" ")
						{
							res = E_FAIL;
							vLog("ERROR: GetVsFont GetFont fail(b1) %s", (const char*)CString(face));
						}
						else
							::wcscpy_s(outFont->lfFaceName, LF_FACESIZE, face);

						if (fi.bPointSizeValid)
							outFont->lfHeight = fi.wPointSize;
						else
							outFont->lfHeight = 9;

						::SysFreeString(fi.bstrFaceName);
					}
					else
					{
						res = E_FAIL;
						vLog("ERROR: GetVsFont GetFont fail(b)");
					}
				}

				fontColorStorage->CloseCategory();
				if (SUCCEEDED(res))
				{
					vCatLog("LowLevel", "GetVsFont font %s", (const char*)CString(outFont->lfFaceName));
					return TRUE;
				}

				vLogUnfiltered("ERROR: GetVsFont GetFont fail(c) %lx", res);
			}
			else
			{
				vLogUnfiltered("ERROR: GetVsFont OpenCategory fail %lx", res);
			}
		}
	}
	else
	{
		vLogUnfiltered("ERROR: GetVsFont no VsFontAndColorStorage %lx", res);
	}

	return FALSE;
}

BOOL IdeSettings::GetStatementCompletionFont(LOGFONTW* outFont)
{
	return GetVsFont(GUID_StatementCompletionCategory, outFont);
}

BOOL IdeSettings::GetEnvironmentFont(LOGFONTW* outFont)
{
	return GetVsFont(GUID_VsEnvironmentFontCategory, outFont);
}

// [case: 74697] hacky workaround for non-dialog template controls (?)
BOOL IdeSettings::GetEnvironmentFontPointInfo(LOGFONTW* outFont)
{
	return GetVsFont(GUID_VsEnvironmentFontCategory, outFont, true);
}

BOOL IdeSettings::GetEditorFont(LOGFONTW* outFont)
{
	return GetVsFont(GUID_TextEditorCategory, outFont);
}

bool IdeSettings::IsLocalized() const
{
	LCID lcid = GetLocaleID();
	WORD lang_id = LANGIDFROMLCID(lcid);

	return LANG_ENGLISH != PRIMARYLANGID(lang_id) || SUBLANG_ENGLISH_US != SUBLANGID(lang_id);
}

LCID IdeSettings::GetLocaleID() const
{
	static long lcid = -1;

	if (lcid == -1)
	{
		if (gDte != nullptr)
			gDte->get_LocaleID(&lcid);
		else
			lcid = 1033; // default to en-us
	}

	return (LCID)lcid;
}

CString IdeSettings::GetEditorOption(LPCSTR lang, LPCSTR prop)
{
	CString val;
#if 0
	if (!strcmp("C/C++", lang) && gPkgServiceProvider)
	{
		CComPtr<IVsTextManager2> vsTextManager2;
		HRESULT hRes = gPkgServiceProvider->QueryService(SID_SVsTextManager, IID_IVsTextManager2, (void**) &vsTextManager2);
		if (vsTextManager2)
		{
			LANGPREFERENCES2 cppLangpreferences2;
			ZeroMemory(&cppLangpreferences2, sizeof(LANGPREFERENCES2));
			cppLangpreferences2.guidLang = GUID_CppLang;
			hRes = vsTextManager2->GetUserPreferences2(nullptr, nullptr, &cppLangpreferences2, nullptr);
			if (SUCCEEDED(hRes))
			{
				if (!strcmp("TabSize", prop))
				{
					val.Format("%d", cppLangpreferences2.uTabSize);
					return val;
				}
				else if (!strcmp("AutoListMembers", prop))
				{
					if (cppLangpreferences2.fAutoListMembers)
						val = "1";
					else
						val = "0";
					return val;
				}
				else if (!strcmp("AutoListParams", prop))
				{
					if (cppLangpreferences2.fAutoListParams)
						val = "1";
					else
						val = "0";
					return val;
				}
			}
			else
			{
				WTString msg;
				msg.Format("GCC: GetUserPrefs2 error %x", hRes);
				vLog(msg);
			}
		}
	}
#endif

	if (gShellAttr->IsDevenv16OrHigher())
	{
		// [case: 138638]
		const CStringW langW(lang);
		if (langW == L"General" && gPkgServiceProvider)
		{
			// "Text Editor_General"
			CComPtr<IVsTextManager3> vsTextManager3;
			HRESULT hRes =
			    gPkgServiceProvider->QueryService(SID_SVsTextManager, IID_IVsTextManager3, (void**)&vsTextManager3);
			if (vsTextManager3)
			{
				VIEWPREFERENCES3 viewPreferences3;
				ZeroMemory(&viewPreferences3, sizeof(VIEWPREFERENCES3));
				hRes = vsTextManager3->GetUserPreferences3(&viewPreferences3, nullptr, nullptr, nullptr);
				if (SUCCEEDED(hRes))
				{
					if (!strcmp("HighlightCurrentLine", prop))
					{
						CString__FormatA(val, "%d", viewPreferences3.fHighlightCurrentLine);
						return val;
					}
				}
				else
				{
					WTString msg;
					msg.WTFormat("GCC: GetUserPrefs2 error %lx", hRes);
					vLog("%s", msg.c_str());
				}
			}
		}
	}

	CComPtr<EnvDTE::Properties> pProperties;
	if (gDte && SUCCEEDED(gDte->get_Properties(CComBSTR(L"TextEditor"), CComBSTR(lang), &pProperties)) && pProperties)
	{
		CComPtr<EnvDTE::Property> pProperty;
		if (SUCCEEDED(pProperties->Item(CComVariant(prop), &pProperty)) && pProperty)
		{
			CComVariant value;
			if (SUCCEEDED(pProperty->get_Value(&value)))
			{
				val = value;
				return val;
			}
		}
	}

#ifdef AVR_STUDIO
	if (!strcmp("GCC", lang) && gPkgServiceProvider)
	{
		CComPtr<IVsTextManager2> vsTextManager2;
		HRESULT hRes =
		    gPkgServiceProvider->QueryService(SID_SVsTextManager, IID_IVsTextManager2, (void**)&vsTextManager2);
		if (vsTextManager2)
		{
			LANGPREFERENCES2 gccLangpreferences2;
			ZeroMemory(&gccLangpreferences2, sizeof(LANGPREFERENCES2));
			gccLangpreferences2.guidLang = GUID_GccLang;
			hRes = vsTextManager2->GetUserPreferences2(nullptr, nullptr, &gccLangpreferences2, nullptr);
			if (SUCCEEDED(hRes))
			{
				if (!strcmp("TabSize", prop))
				{
					CString__FormatA(val, "%d", gccLangpreferences2.uTabSize);
					return val;
				}
				else if (!strcmp("AutoListMembers", prop))
				{
					if (gccLangpreferences2.fAutoListMembers)
						val = "1";
					else
						val = "0";
					return val;
				}
				else if (!strcmp("AutoListParams", prop))
				{
					if (gccLangpreferences2.fAutoListParams)
						val = "1";
					else
						val = "0";
					return val;
				}
			}
			else
			{
				WTString msg;
				msg.WTFormat("GCC: GetUserPrefs2 error %x", hRes);
				vLog("%s", msg.c_str());
			}
		}
	}
#endif

	if (g_loggingEnabled)
	{
		CString msg;
		CString__FormatA(msg, "warn: unsupported editor option: %s %s\n", lang, prop);
		vLog("%s", (const char*)msg);
	}

	val = "Unsupported";
	return val;
}

BOOL IdeSettings::SetEditorOption(LPCSTR lang, LPCSTR prop, LPCSTR val, CString* orgVal /*= NULL*/)
{
	if (g_mainThread != GetCurrentThreadId())
	{
		WTString langProp;
		langProp.WTFormat("%s:%s:%s", lang, prop, val);
		if (gVaMainWnd)
			return (BOOL)SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_SET_VS_OPTION, (WPARAM)langProp.c_str(), (LPARAM)orgVal);
		_ASSERTE(!"ide setting not going to be set");
		return FALSE;
	}

	if (orgVal)
		*orgVal = GetEditorOption(lang, prop);

	if (gShellAttr->IsDevenv16OrHigher())
	{
		const CStringW langW(lang);
		if (langW == L"General" && gPkgServiceProvider)
		{
			// [case: 138638]
			// "Text Editor_General"
			CComPtr<IVsTextManager3> vsTextManager3;
			HRESULT hRes =
			    gPkgServiceProvider->QueryService(SID_SVsTextManager, IID_IVsTextManager3, (void**)&vsTextManager3);
			if (vsTextManager3)
			{
				VIEWPREFERENCES3 viewPreferences3;
				ZeroMemory(&viewPreferences3, sizeof(VIEWPREFERENCES3));
				hRes = vsTextManager3->GetUserPreferences3(&viewPreferences3, nullptr, nullptr, nullptr);
				if (SUCCEEDED(hRes))
				{
					if (!strcmp("HighlightCurrentLine", prop))
					{
						viewPreferences3.fHighlightCurrentLine = CString(val) == "FALSE" ? 0u : 1u;
						hRes = vsTextManager3->SetUserPreferences3(&viewPreferences3, nullptr, nullptr, nullptr);
						if (SUCCEEDED(hRes))
						{
							ResetCache();
							return TRUE;
						}
					}
				}
				else
				{
					WTString msg;
					msg.WTFormat("GCC: GetUserPrefs2 error %lx", hRes);
					vLog("%s", msg.c_str());
				}
			}
		}
	}

	CComPtr<EnvDTE::Properties> pProperties;
	if (gDte && SUCCEEDED(gDte->get_Properties(CComBSTR(L"TextEditor"), CComBSTR(lang), &pProperties)) && pProperties)
	{
		CComPtr<EnvDTE::Property> pProperty;
		if (SUCCEEDED(pProperties->Item(CComVariant(prop), &pProperty)) && pProperty)
		{
			CComVariant value(val);
			if (SUCCEEDED(pProperty->put_Value(value)))
			{
				ResetCache();
				return TRUE;
			}
		}
	}
	return FALSE;
}

void IdeSettings::SaveEditorStringOption(const WTString& val, const std::string& key, const std::string& subKey)
{
	if (val == "Unsupported")
	{
		mEditorStringOptions[std::make_pair(key, subKey)] = NULLSTR;
	}
	else
	{
		mEditorStringOptions[std::make_pair(key, subKey)] = val;
	}
}

void IdeSettings::SaveVsStringOption(const CStringW& val, const std::string& key, const std::string& subKey)
{
	if (val == L"Unsupported")
	{
		mVsStringOptions[std::make_pair(key, subKey)] = CString();
	}
	else
	{
		mVsStringOptions[std::make_pair(key, subKey)] = val;
	}
}

void IdeSettings::SaveEditorBoolOption(const CString& val, const std::string& key, const std::string& subKey,
                                       bool defaultIfUnsupported /*= false*/)
{
	if (val == "-1" || val == "1")
	{
		mEditorBoolOptions[std::make_pair(key, subKey)] = true;
	}
	else if (val == "Unsupported")
	{
		mEditorBoolOptions[std::make_pair(key, subKey)] = defaultIfUnsupported;
	}
	else
	{
		_ASSERTE(val == "0");
		mEditorBoolOptions[std::make_pair(key, subKey)] = false;
	}
}

void IdeSettings::SaveEditorIntOption(const CString& val, const std::string& key, const std::string& subKey,
                                      int defaultIfUnsupported /*= 0*/)
{
	if (val == "Unsupported")
	{
		mEditorIntOptions[std::make_pair(key, subKey)] = defaultIfUnsupported;
	}
	else
	{
		const int intVal = ::atoi(val);
		mEditorIntOptions[std::make_pair(key, subKey)] = intVal;
	}
}

bool IdeSettings::GetEditorBoolOption(LPCSTR lang, LPCSTR prop)
{
	BoolOptions::const_iterator it = mEditorBoolOptions.find(std::make_pair(lang, prop));
	if (it == mEditorBoolOptions.end())
	{
		std::string key(lang), subKey(prop);
		CString tmp(GetEditorOption(key.c_str(), subKey.c_str()));
		SaveEditorBoolOption(tmp, key, subKey);
		it = mEditorBoolOptions.find(std::make_pair(key, subKey));
		if (it == mEditorBoolOptions.end())
		{
			_ASSERTE(!"failed to get bool editor option");
			return false;
		}
	}

	return it->second;
}

int IdeSettings::GetEditorIntOption(LPCSTR lang, LPCSTR prop)
{
	IntOptions::const_iterator it = mEditorIntOptions.find(std::make_pair(lang, prop));
	if (it == mEditorIntOptions.end())
	{
		std::string key(lang), subKey(prop);
		CString tmp(GetEditorOption(key.c_str(), subKey.c_str()));
		SaveEditorIntOption(tmp, key, subKey);
		it = mEditorIntOptions.find(std::make_pair(key, subKey));
		if (it == mEditorIntOptions.end())
		{
			_ASSERTE(!"failed to get int editor option");
			return 0;
		}
	}

	return it->second;
}

WTString IdeSettings::GetEditorStringOption(LPCSTR lang, LPCSTR prop)
{
	StringOptions::const_iterator it = mEditorStringOptions.find(std::make_pair(lang, prop));
	if (it == mEditorStringOptions.end())
	{
		std::string key(lang), subKey(prop);
		CString tmp(GetEditorOption(key.c_str(), subKey.c_str()));
		SaveEditorStringOption(WTString(tmp), key, subKey);
		it = mEditorStringOptions.find(std::make_pair(key, subKey));
		if (it == mEditorStringOptions.end())
		{
			_ASSERTE(!"failed to get string editor option");
			return NULLSTR;
		}
	}

	return it->second;
}

CStringW IdeSettings::GetVsStringOption(LPCSTR category, LPCSTR subCategory, LPCSTR prop)
{
	std::string key(category + std::string("+") + subCategory);
	StringOptionsW::const_iterator it = mVsStringOptions.find(std::make_pair(key, prop));
	if (it == mVsStringOptions.end())
	{
		std::string subKey(prop);
		CStringW tmp(GetVsOption(category, subCategory, prop));
		SaveVsStringOption(tmp, key, subKey);
		it = mVsStringOptions.find(std::make_pair(key, subKey));
		if (it == mVsStringOptions.end())
		{
			_ASSERTE(!"failed to get vs string option");
			return CStringW();
		}
	}

	return it->second;
}

COLORREF
IdeSettings::GetColor(int index, COLORREF defaultValue /*= 0x00ffffff*/)
{
	ColorOptions::const_iterator it;
	try
	{
		it = mColorSettings.lower_bound(index);
	}
	catch (...)
	{
		// crash in 1838 in the find(index) call below.
		// find calls lower_bound
		// disasm at point of crash:
		// 1EDA704A  mov         ebx,eax
		// 1EDA704C  mov         eax,dword ptr [eax]
		// -->1EDA704E  cmp         byte ptr [eax+15h],0
		//
		// eax was originally non-null as seen by the value in ebx at time of crash
		// second instruction updates eax with dword ptr[eax] - but that is 0
		// the cmp crashes since eax is now null
		//
		// if we get in here, the map is bad - just clear it

		mColorSettings.clear();
	}

	it = mColorSettings.find(index);
	if (it == mColorSettings.end())
	{
#if defined(RAD_STUDIO)
		CStringW msg;
		msg.Format(L"ERROR: VA Theme Uninitialized IdeSettings::GetColor index: %x\n", index);
		ASSERT_ONCE(!msg || !"value of index needs to be added to IdeSettings::CheckInit");
#ifdef _DEBUG
		OutputDebugStringW(msg);
#endif
		return defaultValue;
#else
		COLORREF clr = CLR_INVALID;
		if (!GetColorDirect(index, &clr))
			return defaultValue;

		if (gShellAttr->IsDevenv11())
		{
			LPCWSTR subst = nullptr;
			BOOL fg = FALSE;
			// [case: 63900] workaround for alpha channel messiness in dev11 menu selection
			switch (index)
			{
			case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_BEGIN:
			case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE1:
			case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE2:
			case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_END:
			case VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_BEGIN:
			case VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE1:
			case VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE2:
			case VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_END:
				subst = L"CommandBarMenuItemMouseOver";
				break;

			case VSCOLOR_DROPDOWN_MOUSEOVER_BORDER:
				subst = L"CommandBarMenuItemMouseOverBorder";
				break;
			}

			if (subst)
			{
				clr = GetEnvironmentColor(subst, fg);
				if (UINT_MAX == clr)
					return defaultValue;
			}
		}

		mColorSettings[index] = clr;
		it = mColorSettings.find(index);
		if (it == mColorSettings.end())
		{
			_ASSERTE(!"failed to get color option");
			return defaultValue;
		}
#endif
	}

	return it->second;
}

// see also class ColorSyncManager implementation of theme tracking
GUID IdeSettings::GetThemeID()
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);

	if (!gShellAttr || !gShellAttr->IsDevenv11OrHigher())
		return GUID_NULL;

	if (IsEqualGUID(mThemeGuid, GUID_NULL))
	{
		CString regRoot = gShellAttr->GetBaseShellKeyName();
		CStringW strThemeGUID;

		if (gShellAttr->IsDevenv14OrHigher())
		{
			auto regPath = regRoot + R"(\ApplicationPrivateSettings\Microsoft\VisualStudio)";

			strThemeGUID = GetRegValueW(HKEY_CURRENT_USER, regPath, "ColorThemeNew");
			if (strThemeGUID.IsEmpty())
				strThemeGUID = GetRegValueW(HKEY_CURRENT_USER, regPath, "ColorTheme");
		}
		else
		{
			auto regPath = regRoot + R"(\General)";
			strThemeGUID = GetRegValueW(HKEY_CURRENT_USER, regRoot, "CurrentTheme");
		}

		if (!strThemeGUID.IsEmpty())
		{
			// .NET registry values are serialized and start with index and type like: "0*System.String*"
			int typeSeparator = strThemeGUID.ReverseFind(L'*');
			if (typeSeparator != -1)
				strThemeGUID = strThemeGUID.Mid(typeSeparator + 1);
		}

		if (strThemeGUID.IsEmpty() || !SUCCEEDED(CLSIDFromString(strThemeGUID, &mThemeGuid)))
		{
			mThemeGuid = GUID_ThemeBlue; // set default value
		}
	}

	return mThemeGuid;
}

// see also class ColorSyncManager implementation of theme tracking
bool IdeSettings::IsBlueVSColorTheme15()
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);

	if (!gShellAttr || !gShellAttr->IsDevenv15OrHigher())
		return false;

	auto themeId = GetThemeID();

	return IsEqualGUID(themeId, GUID_ThemeBlue) || IsEqualGUID(themeId, GUID_ThemeAdditionalContrast);
}

bool IdeSettings::IsDarkVSColorTheme()
{
	_ASSERTE(GetCurrentThreadId() == g_mainThread);

	if (!gShellAttr || !gShellAttr->IsDevenv11OrHigher())
		return false;

	auto themeId = GetThemeID();

	return !!IsEqualGUID(themeId, GUID_ThemeDark);
}

COLORREF
IdeSettings::GetThemeColor(REFGUID colorCategory, LPCWSTR colorName, BOOL foreground)
{
	COLORREF ret = UINT_MAX;
	DwordOptions::const_iterator it;
	std::wstring cacheName((BSTR)(WTComBSTR)colorCategory);
	cacheName += colorName;
	cacheName += foreground ? L"Fg" : L"Bg";

	CSingleLock lock(&mThemeColors_cs, true);

	it = mThemeColors.find(cacheName);
	if (it == mThemeColors.end())
	{
		lock.Unlock();

		{
#if defined(RAD_STUDIO)
			CStringW msg;
			msg.Format(L"ERROR: VA Theme Uninitialized IdeSettings::GetThemeColor value: %s\n", cacheName.c_str());
			ASSERT_ONCE(!msg || !"color value needs to be added to IdeSettings::CheckInit");
#ifdef _DEBUG
			OutputDebugStringW(msg);
#endif
			ret = UINT_MAX;
#else
			if (!gPkgServiceProvider)
				return UINT_MAX;

			CComPtr<IVsUIShell> uishell;
			if (FAILED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)))
				return UINT_MAX;
			if (!uishell)
				return UINT_MAX;

			CComQIPtr<IVsUIShell5> uishell5(uishell);
			if (!uishell5)
				return UINT_MAX;

			if (SUCCEEDED(uishell5->GetThemedColor(colorCategory, CStringW(colorName),
			                                       foreground ? TCT_Foreground : TCT_Background, (VS_RGBA*)&ret)))
			{
				// toss alpha channel for now...
				ret = RGB(GetRValue(ret), GetGValue(ret), GetBValue(ret));
			}
			else
			{
				// this fails in the vs2012 light theme for tooltip colors
				vLog("ERROR: IVsUIShell5::GetThemedColor failed (%s) (%d)", (const char*)CString(colorName),
				     foreground);
				return UINT_MAX;
			}
#endif
		}

		lock.Lock();
		mThemeColors[cacheName] = ret;
	}
	else
		ret = it->second;

	return ret;
}

COLORREF
IdeSettings::GetThemeTreeColor(LPCWSTR colorName, BOOL foreground)
{
	return GetThemeColor(GUID_VsTreeViewCategory, colorName, foreground);
}

COLORREF
IdeSettings::GetEnvironmentColor(LPCWSTR colorName, BOOL foreground)
{
	return GetThemeColor(GUID_VsEnvironmentCategory, colorName, foreground);
}

COLORREF
IdeSettings::GetNewProjectDlgColor(LPCWSTR colorName, BOOL foreground)
{
	return GetThemeColor(GUID_VsNewProjectDlgCategory, colorName, foreground);
}

#ifdef RAD_STUDIO
COLORREF 
IdeSettings::GetRSSysColor(int index, COLORREF defaultValue /*= CLR_INVALID*/)
{
	if (index < 0 || index >= (int)mRSSysColors.size())
	{
		_ASSERTE(!"Invalid system color index!");
		return defaultValue;
	}

	if (mRSSysColors[(size_t)index] == CLR_INVALID)
	{
		_ASSERTE(!"Unresolved RAD Studio's system color!");
		return defaultValue;
	}

	return mRSSysColors[(size_t)index];
}
#endif

COLORREF
IdeSettings::GetEditorColor(LPCWSTR colorName, BOOL foreground)
{
	return GetThemeColor(GUID_TextEditorCategory, colorName, foreground);
}

COLORREF
IdeSettings::GetDteEditorColor(LPCWSTR colorName, BOOL foreground)
{
	COLORREF ret = UINT_MAX;
	DwordOptions::const_iterator it;
	std::wstring cacheName(L"DteEditor");
	cacheName += colorName;
	cacheName += foreground ? L"Fg" : L"Bg";

	CSingleLock lock(&mThemeColors_cs, true);

	it = mThemeColors.find(cacheName);
	if (it == mThemeColors.end())
	{
		lock.Unlock();

#if defined(RAD_STUDIO)
		CStringW msg;
		msg.Format(L"ERROR: VA Theme Uninitialized IdeSettings::GetDteEditorColor value: %s\n", cacheName.c_str());
		ASSERT_ONCE(!msg || !"color value needs to be added to IdeSettings::CheckInit");
#ifdef _DEBUG
		OutputDebugStringW(msg);
#endif
		ret = UINT_MAX;
#else
		if (gDte)
		{
			CComPtr<EnvDTE::Properties> pProperties;
			CComPtr<EnvDTE::Property> pProperty;
			CComPtr<IDispatch> pFCObject;
			CComPtr<EnvDTE::ColorableItems> pColor;

			if (!(SUCCEEDED(gDte->get_Properties(CComBSTR(L"FontsAndColors"), CComBSTR(L"TextEditor"), &pProperties)) &&
			      pProperties))
				return UINT_MAX;

			if (!(SUCCEEDED(pProperties->Item(CComVariant(L"FontsAndColorsItems"), &pProperty)) && pProperty))
				return UINT_MAX;

			if (!(SUCCEEDED(pProperty->get_Object(&pFCObject)) && pFCObject))
				return UINT_MAX;

			CComQIPtr<EnvDTE::FontsAndColorsItems> pFCs{pFCObject};
			if (!pFCs)
				return UINT_MAX;

			pFCs->Item(CComVariant(colorName), &pColor);
			if (pColor)
			{
				OLE_COLOR color = 0x0;
				if (foreground)
					pColor->get_Foreground(&color);
				else
					pColor->get_Background(&color);
				ret = color;
			}
		}
		else if (!gShellAttr || gShellAttr->IsDevenv())
			return UINT_MAX; // don't cache
		else
			ret = UINT_MAX; // okay to cache value
#endif
		lock.Lock();
		mThemeColors[cacheName] = ret;
	}
	else
		ret = it->second;

	return ret;
}

COLORREF
IdeSettings::GetToolTipColor(BOOL foreground)
{
	if (gShellAttr && (gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder()))
		return GetThemeColor(GUID_VsEnvironmentCategory, L"ToolTip", foreground);
	return GetThemeColor(GUID_EditorTooltipCategory, L"Plain Text", foreground);
}

void IdeSettings::ResetCache()
{
	// just clear the maps - they will be repopulated as needed
	mEditorStringOptions.clear();
	mEditorBoolOptions.clear();
	mEditorIntOptions.clear();
	mVsStringOptions.clear();
	mColorSettings.clear();
	mMainThreadInitComplete = false;

	{
		CSingleLock lock(&mThemeColors_cs, true);
		mThemeColors.clear();
		mThemeGuid = GUID_NULL;
	}
	CGradientCache::InvalidateAllGradientCaches();

	CheckInit();
}

BOOL IdeSettings::GetColorDirect(int index, COLORREF* defaultValue)
{
	_ASSERTE(defaultValue);
	COLORREF clr = CLR_INVALID;

#if defined(RAD_STUDIO)
	if (VSCOLOR_COMMANDBAR_GRADIENT_BEGIN == index)
	{
		if (gVaRadStudioPlugin)
		{
			NsRadStudio::TTheme rsTheme(gVaRadStudioPlugin->GetTheme());
			*defaultValue = rsTheme.FMainToolBarColor;
			return TRUE;
		}
	}
	else
	{
		CStringW msg;
		msg.Format(L"ERROR: VA Theme GetColorDirect unhandled index: %x\n", index);
		ASSERT_ONCE(!msg || !"value of index needs to be handled");
#ifdef _DEBUG
		OutputDebugStringW(msg);
#endif
	}
#else
	if (!gPkgServiceProvider)
		return FALSE;

	CComPtr<IVsUIShell> uishell;
	if (FAILED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)))
		return FALSE;
	if (!uishell)
		return FALSE;

	CComQIPtr<IVsUIShell2> uishell2(uishell);
	if (!uishell2)
		return FALSE;

	if (SUCCEEDED(uishell2->GetVSSysColorEx((VSSYSCOLOREX)index, &clr)))
	{
		_ASSERTE(clr != CLR_INVALID);
		clr = RGB(GetRValue(clr), GetGValue(clr), GetBValue(clr)); // strip unwanted info from highest byte
		*defaultValue = clr;
	}
#endif

	return clr != CLR_INVALID;
}

CStringW IdeSettings::GetVsOption(LPCSTR category, LPCSTR subCategory, LPCSTR prop)
{
	_ASSERTE(::GetCurrentThreadId() == g_mainThread); // [case: 136927
	CStringW val;

	CComPtr<EnvDTE::Properties> pProperties;
	if (gDte && SUCCEEDED(gDte->get_Properties(CComBSTR(category), CComBSTR(subCategory), &pProperties)) && pProperties)
	{
		CComPtr<EnvDTE::Property> pProperty;
		if (SUCCEEDED(pProperties->Item(CComVariant(prop), &pProperty)) && pProperty)
		{
			CComVariant value;
			if (SUCCEEDED(pProperty->get_Value(&value)))
			{
				val = value;
				return val;
			}
		}
	}

	if (g_loggingEnabled)
	{
		CString msg;
		CString__FormatA(msg, "warn: unsupported vs option: %s %s %s\n", category, subCategory, prop);
		vLog("%s", (const char*)msg);
	}

	val = L"Unsupported";
	return val;
}

void IdeSettings::CheckInit(bool force /*= false*/)
{
	if (force)
		mMainThreadInitComplete = false;

	if (mMainThreadInitComplete || GetCurrentThreadId() != g_mainThread)
		return;

#if defined(RAD_STUDIO)
	if (gVaRadStudioPlugin && gRadStudioHost)
	{
		NsRadStudio::TTheme rsTheme(gVaRadStudioPlugin->GetTheme());

		COLORREF textColor = rsTheme.FHotSingleColor;
		COLORREF windowBackground = rsTheme.FBackground1;
		COLORREF focusedSelectionBgColor = rsTheme.FNCInActiveColor;
		COLORREF focusedSelectionTextColor = textColor;
		COLORREF gridAndBorders = rsTheme.FMainToolBarColor;
		COLORREF buttonMouseOverColor = rsTheme.FTabSetBackground;
		COLORREF buttonColor = rsTheme.FMainToolBarColor;
		COLORREF buttonDisabledColor = rsTheme.FMainToolBarColor;
		COLORREF buttonFocusedColor = buttonColor;

		// described: https://docwiki.embarcadero.com/RADStudio/Alexandria/en/Color_Constants
		// defined: https://docwiki.embarcadero.com/Libraries/Alexandria/en/Vcl.Graphics#Constants
		// found that values in definitions are same as Win32 COLOR_* definitions:
		// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor
 		constexpr DWORD kSysColorBits = 0xff000000;
 		COLORREF tmp;
// 		tmp = CLR_INVALID;
// 		if (gRadStudioHost->GetSystemColor(kSysColorBits | COLOR_WINDOW, (int*)&tmp))
// 			windowBackground = tmp;
// 		tmp = CLR_INVALID;
// 		if (gRadStudioHost->GetSystemColor(kSysColorBits | COLOR_HIGHLIGHT, (int*)&tmp))
// 			focusedSelectionBgColor = tmp;
// 		tmp = CLR_INVALID;
// 		if (gRadStudioHost->GetSystemColor(kSysColorBits | COLOR_ACTIVEBORDER, (int*)&tmp))
// 			gridAndBorders = tmp;
// 		tmp = CLR_INVALID;
// 		if (gRadStudioHost->GetSystemColor(kSysColorBits | COLOR_BTNFACE, (int*)&tmp))
// 		{
// 			buttonMouseOverColor = tmp;
// 			buttonFocusedColor = tmp;
// 		}
// 		tmp = CLR_INVALID;
// 		if (gRadStudioHost->GetSystemColor(kSysColorBits | COLOR_BTNHIGHLIGHT, (int*)&tmp))
// 			buttonColor = tmp;

		mRSSysColors.resize(COLOR_MENUBAR + 1);
		for (int idx = 0; idx <= COLOR_MENUBAR; idx++)
		{
			tmp = CLR_INVALID;
			gRadStudioHost->GetSystemColor((int)(kSysColorBits | idx), (int*)&tmp);
			mRSSysColors[(size_t)idx] = tmp;		
		}

		// colors suggested by Rodrigo
		focusedSelectionBgColor = mRSSysColors[COLOR_HIGHLIGHT];
		focusedSelectionTextColor = mRSSysColors[COLOR_HIGHLIGHTTEXT];


		// dark theme: 0x00322f2d == rsTheme.FBackground1
		// mountain mist theme: 0x00322f2d != rsTheme.FBackground1 && 0x00dddddd == rsTheme.FMainWindowBorderColor
		// light theme: 0x00322f2d != rsTheme.FBackground1 && someOtherValue == rsTheme.FMainWindowBorderColor

		// elements used by context menus
		// elements that are commented out have not been tested/not currently used

		// mColorSettings[VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON] = focusedSelectionBgColor;
		// mColorSettings[VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON_BORDER] = focusedSelectionBgColor;
		// mColorSettings[VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_BEGIN] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE1] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE2] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_END] = rsTheme.FMainToolBarColor;

		mColorSettings[VSCOLOR_COMMANDBAR_GRADIENT_BEGIN] = windowBackground;

		mColorSettings[VSCOLOR_HIGHLIGHT] = focusedSelectionBgColor;
		mColorSettings[VSCOLOR_HIGHLIGHTTEXT] = focusedSelectionTextColor;

		mColorSettings[VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN] = rsTheme.FBackground2;
		mColorSettings[VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTEND] = rsTheme.FBackground2;
		mColorSettings[VSCOLOR_COMMANDBAR_MENU_BORDER] = gridAndBorders;
		mColorSettings[VSCOLOR_COMMANDBAR_MENU_ICONBACKGROUND] = rsTheme.FBackground2;
		mColorSettings[VSCOLOR_COMMANDBAR_MENU_SEPARATOR] = gridAndBorders;
		mColorSettings[VSCOLOR_COMMANDBAR_MENU_SUBMENU_GLYPH] = textColor;

		mColorSettings[VSCOLOR_COMMANDBAR_TEXT_ACTIVE] = textColor;
		mColorSettings[VSCOLOR_COMMANDBAR_TEXT_SELECTED] = textColor;
		mColorSettings[VSCOLOR_COMMANDBAR_TEXT_INACTIVE] = rsTheme.FDisabledSingleColor;

		// mColorSettings[VSCOLOR_COMMANDBAR_SELECTED] = focusedSelectionBgColor;
		mColorSettings[VSCOLOR_COMMANDBAR_SELECTED_BORDER] = focusedSelectionBgColor;

		// mColorSettings[VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_BEGIN] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE1] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE2] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_END] = rsTheme.FMainToolBarColor;
		// mColorSettings[VSCOLOR_DROPDOWN_MOUSEOVER_BORDER] = rsTheme.FMainWindowBorderColor;

		// mColorSettings[VSCOLOR_HIGHLIGHT] = focusedSelectionBgColor;
		// mColorSettings[VSCOLOR_HIGHLIGHTTEXT] = textColor;


		// elements used by IDE-themed VA dialogs and context menus
		// elements that are commented out have not been tested/not currently used

		CSingleLock lock(&mThemeColors_cs, true);
		// ThemeCategory11::Header
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}DefaultBg"] = windowBackground;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}DefaultFg"] = textColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}GlyphBg"] = windowBackground;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseDownBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseDownFg"] = textColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseDownGlyphBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseOverBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseOverFg"] = textColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}MouseOverGlyphBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{4997F547-1379-456E-B985-2F413CDFA536}SeparatorLineBg"] = gridAndBorders;

		
		// ThemeCategory11::TeamExplorer
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonBg"] = buttonColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonBorderBg"] = buttonColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonDisabledBg"] = buttonDisabledColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonDisabledBorderBg"] = buttonDisabledColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonDisabledFg"] = rsTheme.FDisabledSingleColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonFg"] = textColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonFocusedBg"] = buttonFocusedColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonMouseOverBg"] = buttonMouseOverColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonMouseOverBorderBg"] = rsTheme.FTabSetSelectedFontColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonMouseOverFg"] = textColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonPressedBg"] = buttonColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonPressedBorderBg"] = rsTheme.FTabSetSelectedFontColor;
		mThemeColors[L"{4AFF231B-F28A-44F0-A66B-1BEEB17CB920}ButtonPressedFg"] = textColor;

		// GUID_VsEnvironmentCategory
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}ComboBoxMouseDownGlyphBg"] = rsTheme.FMainToolBarColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}ComboBoxMouseOverGlyphBg"] = rsTheme.FMainToolBarColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}ComboBoxButtonMouseDownBackgroundBg"] = rsTheme.FMainToolBarColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}ComboBoxButtonMouseOverBackgroundBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}CommandBarMenuItemMouseOverBg"] = focusedSelectionBgColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}CommandBarMenuSubmenuGlyphBg"] = rsTheme.FMainToolBarColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}DropDownBorderBg"] = rsTheme.FMainToolBarColor;
		// mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}DropDownMouseOverBorderBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}ControlOutlineBg"] = gridAndBorders;
		mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}GrayTextBg"] = windowBackground;
		mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}MenuBg"] = rsTheme.FMainToolBarColor;
		mThemeColors[L"{624ED9C3-BDFD-41FA-96C3-7C824EA32E3D}WindowTextBg"] = textColor;

		// ThemeCategory11::Cider
		mThemeColors[L"{92D153EE-57D7-431F-A739-0931CA3F7F70}ListItemBg"] = windowBackground;
		mThemeColors[L"{92D153EE-57D7-431F-A739-0931CA3F7F70}ListItemFg"] = textColor;

		// GUID_VsTreeViewCategory
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}BackgroundBg"] = windowBackground;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemActiveBg"] = focusedSelectionBgColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemActiveFg"] = focusedSelectionTextColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemInactiveBg"] = rsTheme.FUnFocusedColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemInactiveFg"] = focusedSelectionTextColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemActiveGlyphBg"] = textColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}GlyphBg"] = textColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}GlyphMouseOverBg"] = textColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemActiveGlyphMouseOverBg"] = focusedSelectionTextColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemInactiveGlyphMouseOverBg"] = focusedSelectionTextColor;
		mThemeColors[L"{92ECF08E-8B13-4CF4-99E9-AE2692382185}SelectedItemInactiveGlyphBg"] = focusedSelectionTextColor;


		// GUID_VsNewProjectDlgCategory
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}BackgroundLowerRegionBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}CheckBoxBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}CheckBoxFg"] = textColor;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}CheckBoxMouseOverBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}CheckBoxMouseOverFg"] = textColor;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}ImageBorderFg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}ImageBorderBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}InputFocusBorderBg"] = rsTheme.FTabSetSelectedFontColor;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxBackgroundBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxBackgroundFg"] = textColor;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxBorderBg"] = gridAndBorders;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxDisabledBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxDisabledBorderBg"] = windowBackground;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxDisabledFg"] = rsTheme.FDisabledSingleColor;
		mThemeColors[L"{C36C426E-31C9-4048-84CF-31C111D65EC0}TextBoxMouseOverBorderBg"] = rsTheme.FTabSetSelectedFontColor;
	}
#endif

	if (!g_pUsage || !g_pUsage->mFilesOpened)
	{
		// [case: 40097]
		// don't force load of VS editor settings if no files have ever been opened
		return;
	}

	mMainThreadInitComplete = true;
	if (gShellAttr->IsDevenv() && Psettings)
	{
		// [case: 79075] moved from addin dll
#ifdef AVR_STUDIO
		Psettings->m_ParamInfo = GetEditorBoolOption("GCC", "AutoListParams");
		Psettings->m_AutoComplete = GetEditorBoolOption("GCC", "AutoListMembers");
#else
		Psettings->m_ParamInfo = GetEditorBoolOption("C/C++", "AutoListParams");
		Psettings->m_AutoComplete = GetEditorBoolOption("C/C++", "AutoListMembers");
#endif
	}

#ifndef AVR_STUDIO
	if (gShellAttr->IsDevenv10OrHigher())
	{
		// init on ui thread, these are used on parse threads, so queries fail
		std::string key("C/C++ Specific"), valName;
		CString val;

		valName = "DisableIntellisense";
		val = GetEditorOption(key.c_str(), valName.c_str());
		if (val == "Unsupported")
		{
			mMainThreadInitComplete = false;
			return;
		}
		SaveEditorBoolOption(val, key, valName);

		valName = "DisableSquiggles";
		val = GetEditorOption(key.c_str(), valName.c_str());
		SaveEditorBoolOption(val, key, valName);

		mVcSquigglesEnabled = !GetEditorBoolOption(key.c_str(), "DisableIntellisense") &&
		                      !GetEditorBoolOption(key.c_str(), "DisableSquiggles");
	}
#endif
}
