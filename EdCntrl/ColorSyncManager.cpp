#include "stdafxed.h"
#include "ColorSyncManager.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "IdeSettings.h"
#include "VaService.h"
#include "DevShellService.h"
#include "PROJECT.H"
#include "colors.h"
#include "Addin\MiniHelpFrm.h"
#include "FontSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

EXTERN_C const GUID GUID_TextEditorCategory;
static const COLORREF kDev11DarkThemeBaseColor = 0x00302d2d;
static const COLORREF kDarkThemePlainTextDefaultFg = RGB(220, 220, 220);
static const COLORREF kDarkThemePlainTextDefaultBg = RGB(30, 30, 30);
static OLE_COLOR kAutomaticColor = 0x02000000; // "Automatic" in VS F&C dialog
static const int kPostThemeChangeDeadPeriod = 2000;
ColorSyncManager* gColorSyncMgr = nullptr;

static COLORREF GetThemeBaseColor();
static void UpdateColorref(COLORREF& origClr, COLORREF newClr);
CComPtr<IVsFontAndColorStorage> GetFontAndColorStorage(BOOL readonly);

// interesting post re: dte colors:
// http://social.msdn.microsoft.com/Forums/vstudio/en-US/ebbcd9dd-636f-47c3-9184-04acb921e181/vs-2012-package-supporting-dark-themes?forum=vsx

static COLORREF RgbOnly(COLORREF clr)
{
	return (COLORREF)(clr & 0x00ffffff);
}

struct ColorSyncManager::SyncState
{
	BOOL mCompletedVsInit;
	DWORD mLastThemeUpdateTime;
	COLORREF mVsBaseThemeColor;
	UINT_PTR mWaitingForThemeChangeTimerId;
	EditColorStr mVacolors[C_NULL + 1];

	// NOTE: vaColors[C_Text].c_fg is actually IdentifierFg,
	// but vaColors[C_Text].c_bg should usually match mVsPlainTextBg
	COLORREF mVsPlainTextFg;
	COLORREF mVsPlainTextBg;
	BOOL mNeedToCommitAutomaticColors;

	SyncState()
	{
		_ASSERTE(Psettings);
		_ASSERTE(g_IdeSettings);

		mCompletedVsInit = FALSE;
		mLastThemeUpdateTime = 0;
		mWaitingForThemeChangeTimerId = 0;
		mNeedToCommitAutomaticColors = FALSE;
		mVsBaseThemeColor = ::GetThemeBaseColor();
		mVsPlainTextFg = mVsPlainTextBg = CLR_INVALID;

		// need to init mVsPlainText* so that on theme change, we know what
		// previous state was.
		GetVsPlainTextColors();

		GetVsOtherColors();
		Psettings->m_colors[C_Number].c_fg = mVacolors[C_Number].c_fg;
		Psettings->m_colors[C_Number].c_bg = mVacolors[C_Number].c_bg;
		Psettings->m_colors[C_Comment].c_fg = mVacolors[C_Comment].c_fg;
		Psettings->m_colors[C_Comment].c_bg = mVacolors[C_Comment].c_bg;
		Psettings->m_colors[C_Operator].c_fg = mVacolors[C_Operator].c_fg;
		Psettings->m_colors[C_Operator].c_bg = mVacolors[C_Operator].c_bg;
		Psettings->m_colors[C_Keyword].c_fg = mVacolors[C_Keyword].c_fg;
		Psettings->m_colors[C_Keyword].c_bg = mVacolors[C_Keyword].c_bg;
		Psettings->m_colors[C_String].c_fg = mVacolors[C_String].c_fg;
		Psettings->m_colors[C_String].c_bg = mVacolors[C_String].c_bg;

		_ASSERTE(sizeof(mVacolors) == sizeof(Psettings->m_colors));
		::memcpy(mVacolors, Psettings->m_colors, sizeof(Psettings->m_colors));
	}

	~SyncState()
	{
		KillWaitingForThemeChangeTimer();
		RestoreAutomaticColors();
	}

	BOOL UpdateVsFontsAndColorsFromVax(BOOL doBraces);
	void UpdateVaxFontsAndColorsFromVsChange(BOOL doBraces);
	void GetVsPlainTextColors(bool force = false);
	void GetVsOtherColors();
	BOOL UpdateVsIfVaChanged(ColorableItemInfo& vsItem, VAColors idx, bool forceUpdate = false, bool doFg = true,
	                         bool doBg = true);
	BOOL CommitAutomaticColor(ColorableItemInfo& vsItem, VAColors idx, bool doFg = true, bool doBg = true);
	void HandleVsColorChange(bool themeChanged);
	BOOL FixBraces(bool themeChanged, COLORREF prevPlainTextFg, COLORREF prevPlainTextBg);
	void RestoreAutomaticColors();
	bool PotentialVsColorChange();

	static void CALLBACK WaitingForThemeChangeTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD);
	void WaitingForThemeChangeTimerFired();
	void KillWaitingForThemeChangeTimer();
};

ColorSyncManager::ColorSyncManager() : mState(new SyncState)
{
	_ASSERTE(!gColorSyncMgr);
	_ASSERTE(gShellAttr->IsDevenv11OrHigher());
	gColorSyncMgr = this;
}

ColorSyncManager::~ColorSyncManager()
{
	_ASSERTE(Psettings);
	gColorSyncMgr = nullptr;
}

void ColorSyncManager::PotentialThemeChange()
{
	mState->KillWaitingForThemeChangeTimer();
	COLORREF curId = ::GetThemeBaseColor();
	if (curId == mState->mVsBaseThemeColor)
		return; // no theme change

	mState->mVsBaseThemeColor = curId;
	mState->HandleVsColorChange(true);

	::memcpy(Psettings->m_colors, mState->mVacolors, sizeof(Psettings->m_colors));
	mState->mLastThemeUpdateTime = ::GetTickCount();
}

void ColorSyncManager::PotentialVaColorChange()
{
	if (!mState->mCompletedVsInit)
	{
		// [case: 66898]
		// don't accept va color changes until we have completed init
		// vs doesn't properly report colors until MEF component has loaded
		::memcpy(Psettings->m_colors, mState->mVacolors, sizeof(Psettings->m_colors));
		return;
	}

	if (0 == memcmp(mState->mVacolors, Psettings->m_colors, sizeof(Psettings->m_colors)))
		return; // no va color change

	mState->GetVsPlainTextColors();
	const BOOL vaChangedBrace =
	    Psettings->m_colors[C_MatchedBrace].c_fg != mState->mVacolors[C_MatchedBrace].c_fg ||
	    Psettings->m_colors[C_MismatchedBrace].c_fg != mState->mVacolors[C_MismatchedBrace].c_fg;
	::memcpy(mState->mVacolors, Psettings->m_colors, sizeof(Psettings->m_colors));
	mState->UpdateVsFontsAndColorsFromVax(vaChangedBrace);
}

bool ColorSyncManager::PotentialVsColorChange()
{
	if (mState->mCompletedVsInit && gShellAttr->IsDevenv12OrHigher())
	{
		// [case: 75079]
		// in vs2012, theme change is broadcast before options dlg is closed.
		// in vs2013, theme change is broadcast after options dlg is closed.
		// delay acting on this notification until VS2013 has had a chance to
		// broadcast a theme change (which it won't if there wasn't a theme chg).
		mState->KillWaitingForThemeChangeTimer();
		mState->mWaitingForThemeChangeTimerId =
		    ::SetTimer(NULL, 0, 500, (TIMERPROC)&SyncState::WaitingForThemeChangeTimerProc);
		return false;
	}

	return mState->PotentialVsColorChange();
}

COLORREF
ColorSyncManager::GetVsEditorTextFg() const
{
	return mState->mVsPlainTextFg;
}

void ColorSyncManager::CompleteInit()
{
	if (mState->mCompletedVsInit)
		return;

	PotentialVsColorChange();
	_ASSERTE(mState->mCompletedVsInit);
}

// see also class IdeSettings implementation of theme tracking for case 146057
ColorSyncManager::ActiveVsTheme ColorSyncManager::GetActiveVsTheme() const
{
	if (Settings::vt_forceLight == Psettings->mVsThemeColorBehavior)
		return avtLight;

	if (Settings::vt_forceDark == Psettings->mVsThemeColorBehavior)
		return avtDark;

	switch (mState->mVsBaseThemeColor)
	{
	case 0x00e9dbd6: // dev11 blue theme base color
	case 0x00e5d6cf: // dev12 blue theme base color
		return avtBlue;

	case 0x00f2efef: // dev11 light theme base color
	case 0x00f2eeee: // dev12 light theme base color
		return avtLight;

	case 0x00c9dbe1: // vs2013 theme editor theme
	case 0x00cfdcce: // vs2013 theme editor theme
	case 0x00e1c9d2: // vs2013 theme editor theme
	case 0x00c9c9e1: // vs2013 theme editor theme
	case 0x00c1dde6: // vs2013 theme editor theme
		return avtLight;

	case kDev11DarkThemeBaseColor: // dev11 / dev12 dark theme base color
		return avtDark;

	default:
		if (Settings::vt_unknownDark == Psettings->mVsThemeColorBehavior)
			return avtDark;

		if (Settings::vt_unknownLight == Psettings->mVsThemeColorBehavior)
			return avtLight;

		const int c1 = GetRValue(mState->mVsBaseThemeColor), c2 = GetGValue(mState->mVsBaseThemeColor),
		          c3 = GetBValue(mState->mVsBaseThemeColor);
		if (c1 >= 0xc0 && c2 >= 0xc0 && c3 >= 0xc0)
		{
			vLog("WARN: unknown base theme 0X%08lx => handled as light", mState->mVsBaseThemeColor);
			return avtLight;
		}
		else if (c1 <= 0x30 && c2 <= 0x30 && c3 <= 0x30)
		{
			vLog("WARN: unknown base theme 0X%08lx => handled as dark", mState->mVsBaseThemeColor);
			return avtDark;
		}

		_ASSERTE(!"unhandled vs theme");
		vLog("ERROR: unknown base theme 0X%08lx", mState->mVsBaseThemeColor);
		return avtUnknown;
	}
}

COLORREF
GetThemeBaseColor()
{
	COLORREF clr = CLR_INVALID;
	_ASSERTE(g_IdeSettings);
	BOOL retcode = g_IdeSettings->GetColorDirect(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN, &clr);
	_ASSERTE(retcode);
	std::ignore = retcode;
	return clr;
}

void UpdateColorref(COLORREF& origClr, COLORREF newClr)
{
	// only update value if lower bits (RGB) values change.  Ignore upper bits
	// because they are used by our Options Dlg, but not by VS.
	//
	if (RgbOnly(origClr) != newClr)
		origClr = newClr;
}

void UpdateVaxColorFromColorableItemInfo(ColorableItemInfo& vsItem, EditColorStr* vaItem, bool doFg = true,
                                         bool doBg = true)
{
	if (doFg && vsItem.bForegroundValid)
	{
		OLE_COLOR clr = vsItem.crForeground;
#ifdef _DEBUG
		if (clr != RgbOnly(clr))
		{
			// [case: 62647] no fg color set by default in our MEF component
			if (CString("Reference") != vaItem->m_elementName && CString("ReferenceAssign") != vaItem->m_elementName &&
			    CString("Matched Brace/Paren") !=
			        vaItem->m_elementName && // VS2019 sets the high-bit on this member, maybe because fg color matches
			                                 // default text color?
			    CString("FindResultsHighlight") != vaItem->m_elementName)
			{
				_ASSERTE(!"vsItem.csForeground unexpectedly has high bits set");
			}
		}
#endif // _DEBUG
		UpdateColorref(vaItem->c_fg, RgbOnly(clr));
	}

	if (doBg && vsItem.bBackgroundValid)
	{
		OLE_COLOR clr = vsItem.crBackground;
		_ASSERTE(clr == RgbOnly(clr));
		UpdateColorref(vaItem->c_bg, RgbOnly(clr));
	}
}

CComPtr<IVsFontAndColorStorage> GetFontAndColorStorage(BOOL readonly)
{
	_ASSERTE(gPkgServiceProvider);
	CComQIPtr<IVsFontAndColorStorage> fontColorStorage;

	// obtain the VS automatic color
	IUnknown* tmp = nullptr;
	gPkgServiceProvider->QueryService(SID_SVsFontAndColorStorage, IID_IVsFontAndColorUtilities, (void**)&tmp);
	if (!tmp)
	{
		_ASSERTE(!"failed to get f&c util");
		return nullptr;
	}

	CComQIPtr<IVsFontAndColorUtilities> fontColorUtilities = tmp;
	if (!fontColorUtilities)
	{
		_ASSERTE(!"failed to get f&c util");
		return nullptr;
	}

	// obtain the VS automatic color
	fontColorUtilities->EncodeAutomaticColor(&kAutomaticColor);

	// get the item from the font and color storage
	tmp = nullptr;
	gPkgServiceProvider->QueryService(SID_SVsFontAndColorStorage, IID_IVsFontAndColorStorage, (void**)&tmp);
	if (!tmp)
	{
		_ASSERTE(!"failed to get f&c service");
		return nullptr;
	}

	fontColorStorage = tmp;
	if (!fontColorStorage)
		return nullptr;

	if (readonly)
	{
		// we are only reading, no need for propagation
		// note here we don't want AutoColors - just concrete colors - different than when we update VS
		// do not use FCSF_READONLY - causes problems in dev11 reading defaults
		// flags: http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.__fcstorageflags.aspx
		if (!SUCCEEDED(fontColorStorage->OpenCategory(GUID_TextEditorCategory, FCSF_LOADDEFAULTS | FCSF_NOAUTOCOLORS)))
		{
			_ASSERTE(!"failed to get readonly f&c category");
			return nullptr;
		}
	}
	else
	{
		// since we might update VS, need to open w/ propagation
		// flags: http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.__fcstorageflags.aspx
		if (!SUCCEEDED(
		        fontColorStorage->OpenCategory(GUID_TextEditorCategory, FCSF_LOADDEFAULTS | FCSF_PROPAGATECHANGES)))
		{
			_ASSERTE(!"failed to get f&c category for propagation");
			return nullptr;
		}
	}

	return fontColorStorage;
}

BOOL ColorSyncManager::SyncState::UpdateVsIfVaChanged(ColorableItemInfo& vsItem, VAColors idx,
                                                      bool forceUpdate /*= false*/, bool doFg /*= true*/,
                                                      bool doBg /*= true*/)
{
	OLE_COLOR clrNew;
	OLE_COLOR clrOld;

	bool changedFg = false;
	bool changedBg = false;

	if (doFg)
	{
		clrNew = RgbOnly(mVacolors[idx].c_fg);
		clrOld = vsItem.crForeground;
		if (clrOld == kAutomaticColor && !doBg && !forceUpdate)
			; // no change to either fg or bg
		else if (forceUpdate || clrNew != clrOld)
		{
			vsItem.crForeground = clrNew;
			changedFg = true;
		}
	}

	if (doBg)
	{
		clrNew = RgbOnly(mVacolors[idx].c_bg);
		clrOld = vsItem.crBackground;
		if (clrOld == kAutomaticColor && !changedFg && !forceUpdate)
			; // no change to either fg or bg
		else if (forceUpdate || clrNew != clrOld)
		{
			vsItem.crBackground = clrNew;
			changedBg = true;
		}
	}

	if (!doFg && changedBg && vsItem.crForeground != mVsPlainTextFg)
		vsItem.crForeground = mVsPlainTextFg;

	if (!doBg && changedFg && vsItem.crBackground != mVsPlainTextBg)
		vsItem.crBackground = mVsPlainTextBg;

	return changedBg || changedFg;
}

BOOL ColorSyncManager::SyncState::UpdateVsFontsAndColorsFromVax(BOOL doBraces)
{
	CComPtr<IVsFontAndColorStorage> fontColorStorage = GetFontAndColorStorage(FALSE);
	if (!fontColorStorage)
		return false;

	int missingItems = 0;
	int itemsChanged = 0;
	ColorableItemInfo itemInfo;
	LPCWSTR itemName;

	if (doBraces)
	{
		// when user changes brace colors via va options dlg, force background
		// to be automatic
		itemName = L"VA Brace Matching";
		if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
		{
			itemInfo.bForegroundValid = TRUE;
			itemInfo.crForeground = mVacolors[C_MatchedBrace].c_fg;
			itemInfo.bBackgroundValid = TRUE;
			itemInfo.crBackground = kAutomaticColor;
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
		else
			missingItems++;

		itemName = L"VA Brace Error";
		if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
		{
			itemInfo.bForegroundValid = TRUE;
			itemInfo.crForeground = mVacolors[C_MismatchedBrace].c_fg;
			itemInfo.bBackgroundValid = TRUE;
			itemInfo.crBackground = kAutomaticColor;
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
		else
			missingItems++;
	}

	itemName = L"VA Find Reference";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Reference, false, true, true))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
			if (CommitAutomaticColor(itemInfo, C_Reference))
				fontColorStorage->SetItem(itemName, &itemInfo);
		}
	}
	else
		missingItems++;

	itemName = L"VA Find Reference (Modified)";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_ReferenceAssign, false, true, true))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
			if (CommitAutomaticColor(itemInfo, C_ReferenceAssign))
				fontColorStorage->SetItem(itemName, &itemInfo);
		}
	}
	else
		missingItems++;

	itemName = L"VA Spelling Error";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		// don't set background value -- it is used for Syntax Error
		OLE_COLOR clrNew = RgbOnly(mVacolors[C_TypoError].c_fg);
		OLE_COLOR clrOld = RgbOnly(itemInfo.crForeground);
		if (clrNew != clrOld)
		{
			itemInfo.crForeground = clrNew;
			itemInfo.crBackground = kAutomaticColor;
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Syntax Error";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		// don't set foreground value -- it is used for Spelling Error
		OLE_COLOR clrNew = RgbOnly(mVacolors[C_TypoError].c_bg); // note BG, not FG
		OLE_COLOR clrOld = RgbOnly(itemInfo.crForeground);
		if (clrNew != clrOld)
		{
			itemInfo.crForeground = clrNew;
			itemInfo.crBackground = kAutomaticColor;
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Find Result";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_FindResultsHighlight, false, true, true))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
			if (CommitAutomaticColor(itemInfo, C_FindResultsHighlight))
				fontColorStorage->SetItem(itemName, &itemInfo);
		}
	}
	else
		missingItems++;

	itemName = L"VA Class";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Type, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Method";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Function, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Macro";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Macro, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Variable";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Var, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Current Line";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_HighlightCurrentLine))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Column Indicator";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_ColumnIndicator, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Enum Member";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_EnumMember, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	itemName = L"VA Namespace";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_Namespace, false, true, false)) // fg only
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}
	else
		missingItems++;

	_ASSERTE(!missingItems);
	fontColorStorage->CloseCategory();
	if (itemsChanged)
		mNeedToCommitAutomaticColors = TRUE;
	return missingItems == 0;
}

void ColorSyncManager::SyncState::UpdateVaxFontsAndColorsFromVsChange(BOOL doBraces)
{
	CComPtr<IVsFontAndColorStorage> fontColorStorage = GetFontAndColorStorage(TRUE);
	if (!fontColorStorage)
		return;

	ColorableItemInfo itemInfo;

	if (doBraces)
	{
		if (SUCCEEDED(fontColorStorage->GetItem(L"VA Brace Matching", &itemInfo)))
			UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_MatchedBrace]);

		if (SUCCEEDED(fontColorStorage->GetItem(L"VA Brace Error", &itemInfo)))
			UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_MismatchedBrace]);
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Find Reference", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Reference]);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Find Reference (Modified)", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_ReferenceAssign]);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Spelling Error", &itemInfo)))
	{
		// don't set background value -- it is used for Syntax Error
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_TypoError], true, false);
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Syntax Error", &itemInfo)))
	{
		// don't set foreground value -- it is used for Spelling Error
		if (itemInfo.bForegroundValid)
		{
			OLE_COLOR clr = itemInfo.crForeground;
			UpdateColorref(mVacolors[C_TypoError].c_bg, RgbOnly(clr)); // Note BG, not FG
		}
		ZeroMemory(&itemInfo, sizeof(ColorableItemInfo));
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Find Result", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_FindResultsHighlight]);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Class", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Type], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Method", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Function], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Variable", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Var], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Macro", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Macro], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Current Line", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_HighlightCurrentLine]);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Column Indicator", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_ColumnIndicator], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Enum Member", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_EnumMember], true, false);

	if (SUCCEEDED(fontColorStorage->GetItem(L"VA Namespace", &itemInfo)))
		UpdateVaxColorFromColorableItemInfo(itemInfo, &mVacolors[C_Namespace], true, false);

	fontColorStorage->CloseCategory();
}

void ColorSyncManager::SyncState::GetVsPlainTextColors(bool force /*= false*/)
{
	if (!(force || CLR_INVALID == mVsPlainTextFg || CLR_INVALID == mVsPlainTextBg))
		return;

	COLORREF fg, bg;
	if (kDev11DarkThemeBaseColor == mVsBaseThemeColor)
	{
		fg = kDarkThemePlainTextDefaultFg;
		bg = kDarkThemePlainTextDefaultBg;
	}
	else
	{
		// default black on white
		fg = 0;
		bg = 0x00ffffff;
	}

	CComPtr<IVsFontAndColorStorage> fontColorStorage = GetFontAndColorStorage(TRUE);
	if (fontColorStorage)
	{
		ColorableItemInfo itemInfo;
		if (SUCCEEDED(fontColorStorage->GetItem(L"Plain Text", &itemInfo)))
		{
			_ASSERTE(itemInfo.bForegroundValid && itemInfo.bForegroundValid);
			if (itemInfo.bForegroundValid)
				fg = itemInfo.crForeground;

			if (itemInfo.bBackgroundValid)
				bg = itemInfo.crBackground;
		}

		fontColorStorage->CloseCategory();
	}

	_ASSERTE(fg != bg);

	mVsPlainTextFg = fg;
	mVsPlainTextBg = bg;
}

void ColorSyncManager::SyncState::GetVsOtherColors()
{
	CComPtr<IVsFontAndColorStorage> fontColorStorage = GetFontAndColorStorage(TRUE);
	if (!fontColorStorage)
		return;

	ColorableItemInfo itemInfo;
	if (SUCCEEDED(fontColorStorage->GetItem(L"Number", &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
			mVacolors[C_Number].c_fg = itemInfo.crForeground;

		if (itemInfo.bBackgroundValid)
			mVacolors[C_Number].c_bg = itemInfo.crBackground;
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"Comment", &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
			mVacolors[C_Comment].c_fg = itemInfo.crForeground;

		if (itemInfo.bBackgroundValid)
			mVacolors[C_Comment].c_bg = itemInfo.crBackground;
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"Operator", &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
			mVacolors[C_Operator].c_fg = itemInfo.crForeground;

		if (itemInfo.bBackgroundValid)
			mVacolors[C_Operator].c_bg = itemInfo.crBackground;
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"Keyword", &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
			mVacolors[C_Keyword].c_fg = itemInfo.crForeground;

		if (itemInfo.bBackgroundValid)
			mVacolors[C_Keyword].c_bg = itemInfo.crBackground;
	}

	if (SUCCEEDED(fontColorStorage->GetItem(L"String", &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
			mVacolors[C_String].c_fg = itemInfo.crForeground;

		if (itemInfo.bBackgroundValid)
			mVacolors[C_String].c_bg = itemInfo.crBackground;
	}

	fontColorStorage->CloseCategory();
}

BOOL ColorSyncManager::SyncState::CommitAutomaticColor(ColorableItemInfo& vsItem, VAColors idx, bool doFg /*= true*/,
                                                       bool doBg /*= true*/)
{
	bool changedFg = false;
	bool changedBg = false;

	if (doFg && vsItem.bForegroundValid)
	{
		if (vsItem.crForeground == mVsPlainTextFg)
		{
			vsItem.crForeground = kAutomaticColor;
			changedFg = true;
		}
	}

	if (doBg && vsItem.bBackgroundValid)
	{
		if (vsItem.crBackground == mVsPlainTextBg)
		{
			vsItem.crBackground = kAutomaticColor;
			changedBg = true;
		}
	}

	return changedBg || changedFg;
}

void ColorSyncManager::SyncState::RestoreAutomaticColors()
{
	if (!mNeedToCommitAutomaticColors)
	{
		// sync manager wasn't invoked for anything, no need to change anything
		return;
	}

	_ASSERTE(CLR_INVALID != mVsPlainTextBg && CLR_INVALID != mVsPlainTextFg);
	// read-write doesn't resolve automatic colors - which is what we want
	CComPtr<IVsFontAndColorStorage> fontColorStorage = GetFontAndColorStorage(FALSE);
	if (!fontColorStorage)
		return;

	int itemsChanged = 0;
	LPCWSTR itemName;
	ColorableItemInfo itemInfo;

	itemName = L"VA Class";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Type))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Method";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Function))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Macro";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Macro))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Variable";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Var))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Brace Matching";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_MatchedBrace))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Brace Error";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_MismatchedBrace))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Find Reference";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Reference))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Find Reference (Modified)";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_ReferenceAssign))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Find Result";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_FindResultsHighlight))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Enum Member";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_EnumMember))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	itemName = L"VA Namespace";
	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (CommitAutomaticColor(itemInfo, C_Namespace))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
		}
	}

	fontColorStorage->CloseCategory();
}

void ColorSyncManager::SyncState::HandleVsColorChange(bool themeChanged)
{
	const COLORREF prevPlainTextFg = mVsPlainTextFg;
	const COLORREF prevPlainTextBg = mVsPlainTextBg;
	GetVsPlainTextColors(true);

	BOOL bracesUpdated = false;
	if (prevPlainTextFg != mVsPlainTextFg || prevPlainTextBg != mVsPlainTextBg)
	{
		// update braces if plain text bg or fg changed, even if no theme switch
		bracesUpdated = FixBraces(themeChanged, prevPlainTextFg, prevPlainTextBg);
	}

	GetVsOtherColors();

	UpdateVaxFontsAndColorsFromVsChange(!bracesUpdated);

	if (themeChanged)
	{
		// [case: 66898]
		// if va installed while dark theme active, then switching
		// from dark theme to light theme causes incorrect Automatic
		// foreground colors for items in our default light theme
		if (mVacolors[C_Reference].c_fg == prevPlainTextFg)
			mVacolors[C_Reference].c_fg = mVsPlainTextFg;
		if (mVacolors[C_ReferenceAssign].c_fg == prevPlainTextFg)
			mVacolors[C_ReferenceAssign].c_fg = mVsPlainTextFg;
		if (mVacolors[C_FindResultsHighlight].c_fg == prevPlainTextFg)
			mVacolors[C_FindResultsHighlight].c_fg = mVsPlainTextFg;
		if (mVacolors[C_Operator].c_fg == prevPlainTextFg)
			mVacolors[C_Operator].c_fg = mVsPlainTextFg;
		if (mVacolors[C_Number].c_fg == prevPlainTextFg)
			mVacolors[C_Number].c_fg = mVsPlainTextFg;
		if (mVacolors[C_String].c_fg == prevPlainTextFg)
			mVacolors[C_String].c_fg = mVsPlainTextFg;
	}

	if (mCompletedVsInit)
	{
		// dev11RC TODO: revisit this in the dev11 RC to see if still necessary
		// watch out for uses of automatic value in vs options dlg - they can hammer editor
		UpdateVsFontsAndColorsFromVax(FALSE);
	}
	else
	{
		// [case: 66898]
		mCompletedVsInit = TRUE;
	}
}

BOOL ColorSyncManager::SyncState::FixBraces(bool themeChanged, COLORREF prevPlainTextFg, COLORREF prevPlainTextBg)
{
	CComPtr<IVsFontAndColorStorage> fontColorStorage = ::GetFontAndColorStorage(FALSE);
	// need RW version so that automatic is resolved for our queries
	CComPtr<IVsFontAndColorStorage> fontColorStorageRw = ::GetFontAndColorStorage(TRUE);
	if (!fontColorStorage || !fontColorStorageRw)
		return false;

	// comparing to Psettings (which has old values, prior to vs change), if
	// brace color fg matches plain text fg, then change brace color fg to
	// match mVsPlainTextFg
	bool matchedBraceForced = prevPlainTextBg != mVsPlainTextBg;
	if (Psettings->m_colors[C_MatchedBrace].c_fg == prevPlainTextFg && prevPlainTextFg != mVsPlainTextFg)
	{
		// fix disappearing braces after theme switch by setting brace fg to same
		// color as plain text fg
		mVacolors[C_MatchedBrace].c_fg = mVsPlainTextFg;
		matchedBraceForced = true;
	}

	if (themeChanged && matchedBraceForced)
		matchedBraceForced = false;

	// always change brace bg to match mVsPlainTextBg
	mVacolors[C_MatchedBrace].c_bg = mVacolors[C_MismatchedBrace].c_bg = mVsPlainTextBg;

	int itemsChanged = 0;
	LPCWSTR itemName;
	ColorableItemInfo itemInfo;

	itemName = L"VA Brace Matching";
	if (themeChanged && SUCCEEDED(fontColorStorageRw->GetItem(itemName, &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
		{
			OLE_COLOR clr = itemInfo.crForeground;
			_ASSERTE(clr == RgbOnly(clr));
			UpdateColorref(mVacolors[C_MatchedBrace].c_fg, RgbOnly(clr));
		}

		if (itemInfo.bBackgroundValid)
		{
			OLE_COLOR clr = itemInfo.crBackground;
			_ASSERTE(clr == RgbOnly(clr));
			if (themeChanged)
				clr = mVsPlainTextBg;
			UpdateColorref(mVacolors[C_MatchedBrace].c_bg, RgbOnly(clr));
		}
	}

	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_MatchedBrace, matchedBraceForced))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
			if (CommitAutomaticColor(itemInfo, C_MatchedBrace))
				fontColorStorage->SetItem(itemName, &itemInfo);
		}
	}

	itemName = L"VA Brace Error";
	if (themeChanged && SUCCEEDED(fontColorStorageRw->GetItem(itemName, &itemInfo)))
	{
		if (itemInfo.bForegroundValid)
		{
			OLE_COLOR clr = itemInfo.crForeground;
			_ASSERTE(clr == RgbOnly(clr));
			UpdateColorref(mVacolors[C_MismatchedBrace].c_fg, RgbOnly(clr));
		}

		if (itemInfo.bBackgroundValid)
		{
			OLE_COLOR clr = itemInfo.crBackground;
			_ASSERTE(clr == RgbOnly(clr));
			if (themeChanged)
				clr = mVsPlainTextBg;
			UpdateColorref(mVacolors[C_MismatchedBrace].c_bg, RgbOnly(clr));
		}
	}

	if (SUCCEEDED(fontColorStorage->GetItem(itemName, &itemInfo)))
	{
		if (UpdateVsIfVaChanged(itemInfo, C_MismatchedBrace, prevPlainTextBg != mVsPlainTextBg))
		{
			fontColorStorage->SetItem(itemName, &itemInfo);
			++itemsChanged;
			if (CommitAutomaticColor(itemInfo, C_MismatchedBrace))
				fontColorStorage->SetItem(itemName, &itemInfo);
		}
	}

	if (itemsChanged)
		mNeedToCommitAutomaticColors = TRUE;
	fontColorStorage->CloseCategory();
	fontColorStorageRw->CloseCategory();

	return !!itemsChanged;
}

bool ColorSyncManager::SyncState::PotentialVsColorChange()
{
	DWORD curTime = ::GetTickCount();
	if (curTime < mLastThemeUpdateTime + kPostThemeChangeDeadPeriod)
		return false; // theme just changed, ignore this notification

	HandleVsColorChange(false);

	bool changed = (0 != memcmp(mVacolors, Psettings->m_colors, sizeof(Psettings->m_colors)));
	(void)changed;
	::memcpy(Psettings->m_colors, mVacolors, sizeof(Psettings->m_colors));
	return true;
}

void CALLBACK ColorSyncManager::SyncState::WaitingForThemeChangeTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	::KillTimer(hWnd, idEvent);
	_ASSERTE(gShellAttr->IsDevenv12OrHigher());
	SyncState* tmp = gColorSyncMgr ? gColorSyncMgr->mState.get() : nullptr;
	if (tmp)
		tmp->WaitingForThemeChangeTimerFired();
}

void ColorSyncManager::SyncState::WaitingForThemeChangeTimerFired()
{
	// a theme change did not occur, handle as a VS options change
	_ASSERTE(gShellAttr->IsDevenv12OrHigher());
	mWaitingForThemeChangeTimerId = 0;
	if (!PotentialVsColorChange())
		return;

	if (g_IdeSettings)
		g_IdeSettings->ResetCache();
	if (g_FontSettings)
		g_FontSettings->Update();
	if (g_pMiniHelpFrm)
		g_pMiniHelpFrm->SettingsChanged();
}

void ColorSyncManager::SyncState::KillWaitingForThemeChangeTimer()
{
	if (mWaitingForThemeChangeTimerId)
	{
		::KillTimer(NULL, mWaitingForThemeChangeTimerId);
		mWaitingForThemeChangeTimerId = 0;
		_ASSERTE(gShellAttr->IsDevenv12OrHigher());
	}
}
