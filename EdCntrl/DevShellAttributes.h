#pragma once

// DevShellAttributes
// ----------------------------------------------------------------------------
// Class that defines attributes of the shell VA is currently running in.
// It is all state (no operations) and mostly accessed inline.
//
class DevShellAttributes
{
  protected:
	DevShellAttributes(const CString& baseShellKeyName = "");

  public:
	virtual ~DevShellAttributes()
	{
	}

	virtual CString GetPlatformDirectoryKeyName(LPCTSTR /*platform*/)
	{
		return CString();
	}
	virtual CString GetPlatformDirectoryKeyName2(LPCTSTR /*platform*/) const
	{
		return CString();
	}

	// funky logic to replace macros that used to be in Settings.h
	// #newVsVersion
	bool IsDevenv18() const
	{
		return ShellEnvType_Devenv18 == mEnvType;
	}
	bool IsDevenv18OrHigher() const
	{
		return IsDevenv18() /*|| IsDevenv19OrHigher()*/;
	}

	bool IsDevenv17() const
	{
		return ShellEnvType_Devenv17 == mEnvType;
	}
	bool IsDevenv17OrHigher() const
	{
		return IsDevenv17() || IsDevenv18OrHigher();
	}
	bool IsDevenv17u7OrHigher() const;
	bool IsDevenv17u8OrHigher() const;

	bool IsDevenv16u11OrHigher() const;
	bool IsDevenv16u10OrHigher() const;
	bool IsDevenv16u9OrHigher() const;
	bool IsDevenv16u8OrHigher() const;
	bool IsDevenv16u7OrHigher() const;
	bool IsDevenv16u7() const;
	bool IsDevenv16() const
	{
		return ShellEnvType_Devenv16 == mEnvType;
	}
	bool IsDevenv16OrHigher() const
	{
		return IsDevenv16() || IsDevenv17OrHigher();
	}

	bool IsDevenv15u9OrHigher() const;
	bool IsDevenv15u8OrHigher() const;
	bool IsDevenv15u7OrHigher() const;
	bool IsDevenv15u6OrHigher() const;
	bool IsDevenv15u3OrHigher() const;
	bool IsDevenv15() const
	{
		return ShellEnvType_Devenv15 == mEnvType;
	}
	bool IsDevenv15OrHigher() const
	{
		return IsDevenv15() || IsDevenv16OrHigher();
	}

	bool IsDevenv14u3OrHigher() const;
	bool IsDevenv14u2OrHigher() const;
	bool IsDevenv14() const
	{
		return ShellEnvType_Devenv14 == mEnvType;
	}
	bool IsDevenv14OrHigher() const
	{
		return IsDevenv14() || IsDevenv15OrHigher();
	}

	bool IsDevenv12() const
	{
		return ShellEnvType_Devenv12 == mEnvType;
	}
	bool IsDevenv12OrHigher() const
	{
		return IsDevenv12() || IsDevenv14OrHigher();
	}

	bool IsDevenv11() const
	{
		return ShellEnvType_Devenv11 == mEnvType;
	}
	bool IsDevenv11OrHigher() const
	{
		return IsDevenv11() || IsDevenv12OrHigher();
	}

	bool IsDevenv10() const
	{
		return ShellEnvType_Devenv10 == mEnvType;
	}
	bool IsDevenv10OrHigher() const
	{
		return IsDevenv10() || IsDevenv11OrHigher();
	}

	bool IsDevenv9() const
	{
		return ShellEnvType_Devenv9 == mEnvType;
	}
	bool IsDevenv9OrHigher() const
	{
		return IsDevenv9() || IsDevenv10OrHigher();
	}

	bool IsDevenv8() const
	{
		return ShellEnvType_Devenv8 == mEnvType;
	}
	bool IsDevenv8OrHigher() const
	{
		return IsDevenv8() || IsDevenv9OrHigher();
	}

	bool IsDevenv7() const
	{
		return ShellEnvType_Devenv7 == mEnvType;
	}
	bool IsCppBuilder() const
	{
		return ShellEnvType_CppBuilder == mEnvType;
	}
	bool IsDevenv() const
	{
#if defined(RAD_STUDIO)
		// explicit check rather than "return true" so that Find References sees the value
		return ShellEnvType_CppBuilder == mEnvType;
#else
		return IsDevenv7() || IsDevenv8OrHigher();
#endif
	}
	bool IsMsdev() const
	{
		return ShellEnvType_Msdev == mEnvType;
	}

	bool IsMsdevOrCppBuilder() const
	{
		return ShellEnvType_Msdev == mEnvType ||
		       ShellEnvType_CppBuilder == mEnvType;
	}

	int GetRegistryVersionNumber() const
	{
#ifdef AVR_STUDIO
		return 7;
#else
		return mEnvType;
#endif
	}

	virtual CString GetBaseShellKeyName()
	{
		return mBaseShellKeyName;
	}
	CStringW GetDbSubDir() const
	{
		_ASSERTE(!mDbSubDir.IsEmpty());
		return mDbSubDir;
	}
	CStringW GetExeName() const
	{
		_ASSERTE(!mExeName.IsEmpty());
		return mExeName;
	}
	bool RequiresHeapLookaside() const
	{
		return mRequiresHeapLookaside;
	}
	int GetHelpOpenAttempts() const
	{
		return mHelpOpenAttempts;
	}
	bool SupportsHelpEditClassName() const
	{
		return mSupportsHelpEditClassName;
	}
	CString GetFormatSourceWindowKeyName() const
	{
		return mFormatSourceWindowKeyName;
	}
	HKEY GetPlatformListRootKey() const
	{
		return mPlatformListRootKey;
	}
	CString GetPlatformListKeyName() const
	{
		return mPlatformListKeyName;
	}
	CString GetOldVaAppKeyName() const
	{
		return mOldVaAppKeyName;
	}
	bool SupportsNetFrameworkDevelopment() const
	{
		return mSupportsNetFrameworkDevelopment;
	}
	int GetDefaultCharWidth() const
	{
		return mDefaultCharWidth;
	}
	bool RequiresWin32ApiPatching() const
	{
		return mRequiresWin32ApiPatching;
	}
	CString GetDefaultPlatform() const
	{
		return mDefaultPlatform;
	}
	bool CanTryAlternatePlatformSettings() const
	{
		return mCanTryAlternatePlatformSettings;
	}
	bool CanTryVa4Settings() const
	{
		return mCanTryVa4Settings;
	}
	bool GetDefaultCorrectCaseSetting() const
	{
		return mDefaultCorrectCaseSetting;
	}
	bool GetDefaultBraceMismatchSetting() const
	{
		return mDefaultBraceMismatchSetting;
	}
	bool IsFastProjectOpenAllowed() const
	{
		return mIsFastProjectOpenAllowed;
	}
	HKEY GetSetupRootKey() const
	{
		return mSetupRootKey;
	}
	CString GetSetupKeyName() const
	{
		return mSetupKeyName;
	}
	int GetCompletionListBottomOffset() const
	{
		return mCompletionListBottomOffset;
	}
	bool SupportsBreakpoints() const
	{
		return mSupportsBreakpoints;
	}
	bool SupportsBreakpointProperties() const
	{
		return mSupportsBreakpointProperties;
	}
	bool SupportsBookmarks() const
	{
		return mSupportsBookmarks;
	}
	bool SupportsSelectionReformat() const
	{
		return mSupportsSelectionReformat;
	}
	bool SupportsCustomTooltipFont() const
	{
		return mSupportsCustomTooltipFont;
	}
	bool SupportsAIC() const
	{
		return mSupportsAIC;
	}
	bool SupportsDspFiles() const
	{
		return mSupportsDspFiles;
	}
	bool RequiresPositionConversion() const
	{
		return mRequiresPositionConversion;
	}
	bool ShouldUseXpVisualManager() const
	{
		return mShouldUseXpVisualManager;
	}
	bool SupportsCImportDirective() const
	{
		return mSupportsCImportDirective;
	}
	bool RequiresFindResultsHack() const
	{
		return mRequiresFindResultsHack;
	}
	bool GetDefaultAutobackupSetting() const
	{
		return mDefaultAutobackupSetting;
	}

  protected:
	// each of the following items has a default value.
	// at least one derived class modifies each default value.
	// if no derived classes modify a particular value, then it should be removed.
	// GetRegistryVersionNumber() uses the values for isolation of registry settings.
	enum ShellEnvironmentType
	{
		ShellEnvType_None,
		ShellEnvType_CppBuilder = 4,
		ShellEnvType_Msdev = 6,
		ShellEnvType_Devenv7 = 7,
		ShellEnvType_Devenv8 = 8,
		ShellEnvType_Devenv9 = 9,
		ShellEnvType_Devenv10 = 10,
		ShellEnvType_Devenv11 = 11,
		ShellEnvType_Devenv12 = 12,
		ShellEnvType_Devenv14 = 14,
		ShellEnvType_Devenv15 = 15,
		ShellEnvType_Devenv16 = 16,
		ShellEnvType_Devenv17 = 17,
		ShellEnvType_Devenv18 = 18
		// #newVsVersion
	};
	ShellEnvironmentType mEnvType;
	CString mBaseShellKeyName;
	CStringW mDbSubDir;
	CStringW mExeName;
	bool mRequiresHeapLookaside;
	int mHelpOpenAttempts;
	bool mSupportsHelpEditClassName;
	CString mFormatSourceWindowKeyName;
	HKEY mPlatformListRootKey;
	CString mPlatformListKeyName;
	CString mOldVaAppKeyName;
	bool mSupportsNetFrameworkDevelopment;
	int mDefaultCharWidth;
	CString mDefaultPlatform;
	bool mCanTryAlternatePlatformSettings;
	bool mCanTryVa4Settings;
	bool mDefaultCorrectCaseSetting;
	bool mDefaultBraceMismatchSetting;
	bool mIsFastProjectOpenAllowed;
	HKEY mSetupRootKey;
	CString mSetupKeyName;
	int mCompletionListBottomOffset;
	bool mSupportsBreakpoints;
	bool mSupportsBreakpointProperties;
	bool mSupportsBookmarks;
	bool mSupportsCustomTooltipFont;
	bool mSupportsAIC;
	bool mSupportsDspFiles;
	bool mRequiresPositionConversion;
	bool mShouldUseXpVisualManager;
	bool mSupportsSelectionReformat;
	bool mSupportsCImportDirective;
	bool mRequiresWin32ApiPatching;
	bool mRequiresFindResultsHack;
	bool mDefaultAutobackupSetting;
};

extern DevShellAttributes* gShellAttr;
void InitShell();
void UninitShell();
