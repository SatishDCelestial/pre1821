#ifndef settings_h
#define settings_h

#include <wtypes.h>
#include <string>
#include "VaColors.cs"

#define REPARSE_IF_NEEDED_DEFAULT_VAL 20000

// NOTE: Recompile VAssist.dll if this struct changes
//  All the options in here are being set, even though they might not be used
#define COLOR_ELEMENTNAME_LEN 36

typedef struct tagEditColors
{
	char m_elementName[COLOR_ELEMENTNAME_LEN] = "";
	COLORREF c_fg = 0;
	COLORREF c_bg = 0;
} EditColorStr;

class DW8x16
{
	__if_not_exists(DWORD)
	{
		typedef unsigned long DWORD;
	};

	DWORD m_data = 0;

  public:
	DW8x16(DWORD dw = 0) : m_data(dw)
	{
	}
	operator DWORD&()
	{
		return m_data;
	}
	operator DWORD() const
	{
		return m_data;
	}

	DWORD* operator&()
	{
		return &m_data;
	}

	DWORD get(DWORD column) const
	{
		_ASSERTE(int(column) >= 0 && column <= 7);

		DWORD shift = column * 4;
		DWORD index = (m_data & ((DWORD)0xf << shift)) >> shift;
		return index;
	}

	void set(DWORD column, DWORD val)
	{
		_ASSERTE(int(column) >= 0 && column <= 7);
		_ASSERTE(int(val) >= 0 && val <= 0xf);

		DWORD shift = column * 4;
		m_data &= ~((DWORD)0xf << shift);
		m_data |= (DWORD)val << shift;
	}
};

typedef struct tagSettings
{
	// NOTE: these default values are generally not used -- defaults are set in CSettings::InitDefaults
	bool minihelpAtTop = false, boldBraceMatch = false;
	bool AutoMatch = false; // do not access directly; use IsAutomatchAllowed()
	bool CaseCorrect = false, oneStepHelp = false, m_autoSuggest = false;
	bool m_AsciiW32API = false, m_VAWorkspace = false;
	bool m_tabInvokesIntellisense = false;
	bool mAlternateDirtyRefsNavBehavior = false;
	bool m_catchAll = false;
	bool m_smartPaste = false;
	bool m_autoBackup = false;
	bool m_skipLocalMacros = false; // defaults to false
	bool m_mouseOvers = false;
	bool m_resourceAlarm = false;
	bool m_multipleProjSupport = false;
	bool m_keepBookmarks = false;
	bool mOldUncommentBehavior = false; // [case: 93841] option to restore old behavior
	bool m_doModThread = false;
	bool m_defGuesses = false; // include surrounding bits of code in suggestion listboxes
	bool m_parseImports = false;
	bool m_aggressiveFileMatching = false;
	bool m_fixPtrOp = false;
	bool m_showScopeInContext = false;
	bool m_limitMacroParseLocal = false; // defaults to true
	bool m_limitMacroParseSys = false;   // defaults to true
	bool m_rapidFire = false;
	bool m_codeTemplateTooltips = false; // include autotext/snippets in suggestion listboxes
	bool m_auto_m_ = false;
	bool m_underlineTypos = false;
	bool m_contextPrefix = false;
	bool m_braceMismatches = false;
	bool m_menuInMargin = false;
	bool m_colIndicator = false;
	bool m_AutoComplete = false; // this is updated on the addin side (in GetVSOptions) to match the IDE C/C++ setting
	bool m_ParamInfo = false; // this is updated on the addin side (in GetVSOptions) to match the IDE C/C++ setting (see
	                          // also mVaAugmentParamInfo)
	bool m_AutoComments = false;
	bool m_enableVA = false;
	bool m_ActiveSyntaxColoring = false;
	bool m_RTFCopy = false;
	bool m_ColorPrinting = false;
	bool mUseCppOverrideKeyword = false;
	bool mUseCppVirtualKeyword = false;
	bool m_EnablePasteMenu = false;
	DWORD m_sortDefPickList = 0;
	DWORD m_colIndicatorColPos = 0;
	DWORD m_doLocaleChange = 0;
	DWORD m_fnParenGap = 0;
	DWORD m_minihelpHeight = 0; // unscaled height - use with g_FontSettings->GetDpiScaleY()
	DWORD m_minihelpFontSize = 0;
	DWORD mLineCommentSlashCount = 0;
	DWORD mColorBuildOutput_CustomBg = 0;
	DWORD m_ImportTimeout = 0;

	BOOL m_overtype = FALSE;
	DWORD m_borderWidth = 0;
	DWORD mMaxScreenReparse = 0;

	// Editor options
	DWORD m_RecentCnt = 0;    // used by recent files and methods menus
	DWORD m_clipboardCnt = 0; // used by our multi-clipboard

	// Compatibility options
	DWORD NoBackspaceAtBOL = 0;

	// Tab/Language Specific Editor options - C/C++
	DWORD TabSize = 0;

	// environment
	DWORD mHideVc6Options = 0; // controls display of vc6 page in options dlg

	// font
	DWORD FontSize = 0;

	bool m_incrementalSearch = false;
	WCHAR m_srcFontName[255] = L"";
	DWORD mLargeFileSizeThreshold = 0;
	DWORD mAddIncludeStyle =
	    0; // 0 = default, 1 = force quotes, 2 = force <>, 3 = name w/ context-dependent type, 4 = "name", 5 = <name>

	// tooltip stuff
	COLORREF m_TTbkColor = 0;
	COLORREF m_TTtxtColor = 0;

	// multi-platform (WinCE) support
	char m_platformIncludeKey[MAX_PATH] = "";
	// ifdef string
	char m_ifdefString[MAX_PATH] = "";
	// colors
	EditColorStr m_colors[C_NULL + 1]{};
	// file type extensions
	char m_hdrExts[MAX_PATH] = "";
	char m_srcExts[MAX_PATH] = "";
	char m_resExts[MAX_PATH] = "";
	char m_idlExts[MAX_PATH] = "";
	char m_binExts[MAX_PATH] = "";
	char m_javExts[MAX_PATH] = "";

	char m_htmlExts[MAX_PATH] = "";
	char m_vbExts[MAX_PATH] = "";
	char m_jsExts[MAX_PATH] = "";
	char m_csExts[MAX_PATH] = "";
	char m_perlExts[MAX_PATH] = "";
	// minihelp context
	char m_SymScope[MAX_PATH] = "";
	DWORD m_spellFlags = 0;
	DWORD m_SelectionOnChar = 0; // surround selection
	DWORD m_FastProjectOpen = 0;
	bool m_noMiniHelp = false;
	bool mMembersCompleteOnAny = 0;
	HWND m_MainWndHnd = nullptr;
	char m_RegPath[MAX_PATH] = "";

	bool m_bUseDefaultIntellisense = false; // applies only to listboxes and param info
	bool m_bShrinkMemberListboxes = false;
	bool m_bBoldNonInheritedMembers = false;
	bool m_bCompleteWithTab = false;
	bool m_bCompleteWithReturn = false;
	bool m_bCompleteWithAny = false;

	bool m_bEnhColorSourceWindows = false;
	bool m_bEnhColorObjectBrowser = false;
	bool m_bEnhColorTooltips = false;
	bool m_bEnhColorViews = false;
	bool m_bEnhColorListboxes = false;
	bool m_bEnhColorWizardBar = false; // VA Navigation Bar
	bool m_bLocalSymbolsInBold = false;
	bool m_bStableSymbolsInItalics = false;
	DWORD m_nDisplayXSuggestions = 0;
	bool m_bListNonInheritedMembersFirst = false;
	bool m_bSupressUnderlines = false;
	bool m_bGiveCommentsPrecedence = false;
	DWORD m_nMRUOptions = 0;
	DWORD m_nHCBOptions = 0;
	bool m_bDisplayFilteringToolbar = false;
	bool m_bAllowShorthand = false;
	bool m_bAllowAcronyms = false;
	bool mParamsInMethodsInFileList = false;
	char mExtensionsToIgnore[MAX_PATH] =
	    ""; // list of extensions to ignore - each ext in list must start with '.' and end with ';'
	bool mVaviewGotoDef = false;
	bool mHighlightFindReferencesByDefault = false;
	DWORD mBraceAutoMatchStyle = 0; // 1 = {}	2 = {\n}	3 = {\n\t\n}
	bool mUseTooltipsInFindReferencesResults = false;
	DWORD mSuggestionSelectionStyle = 0; // 1 = no selection in definitions (default) 2 = always select
	bool mDisplayRefactoringButton = false;
	bool mExtensionlessFileIsHeader = false;
	bool mQuickInfoInVs8 = false;
	bool mMarkCurrentLine = false;
	DWORD mCurrentLineBorderStyle = 0; // 1 solid, 4 dotted (change does not take effect at runtime, restart required)
	bool mCheckForLatestVersion = false;
	bool mCheckForLatestBetaVersion = false;
	DWORD mCurrentLineVisualStyle =
	    0; // 0x2100 (default), 0x8, 0x2 (among others) (change does not take effect at runtime, restart required)
	bool mDontInsertSpaceAfterComment = false; // '// ' or '//' in comment selection
	bool mLineNumbersInFindRefsResults = false;
	bool mEnableStatusBarMessages = false;
	bool mDisplayCommentAndStringReferences = false; // used by rename dialog to display comment and string items
	BOOL m_validLicense = FALSE;
	bool mCloseSuggestionListOnExactMatch = false;
	bool mNoisyExecCommandFailureNotifications = false;
	bool mIncludeRefactoringInListboxes = false;
	bool mAutoSizeListBox = false;
	DWORD mListboxFlags = 0;
	char m_phpExts[MAX_PATH] = "";                      // Place above when optionsDlg is rebuilt
	bool mParseFilesInDirectoryIfExternalFile = false;  // parse files in dir if opening file not in solution
	bool mParseFilesInDirectoryIfEmptySolution = false; // parse files in dir if solution is empty
	bool mMarkFindText = false;
	bool mIncludeProjectNodeInReferenceResults = false;
	bool mIncludeProjectNodeInRenameResults = false;
	char m_vbsExts[MAX_PATH] = "";
	char m_aspExts[MAX_PATH] = "";
	char m_xmlExts[MAX_PATH] = "";
	char m_xamlExts[MAX_PATH] = "";
	bool mUseTomatoBackground = false; // use tomato in editor tooltips and listboxes
	bool mMethodInFile_ShowRegions = false;
	bool mSelectImplementation = false;
	bool mAutoHighlightRefs = false;
	bool mOptimizeRemoteDesktop = false;
	bool mUseMarkerApi = false;

	// Text Editor - Mouse Click Commands - due to [case: 89430]
	// Columns: 0 = Ctrl+Left-Click, 1 = Alt+Left-Click, 2 = Middle-Click, 3 = Shift+Right-Click
	// Values:  0 = none, 1 = Goto, 2 = GotoRelated, 3 = OpenContextMenu, 4 = OpenContextMenuOld, 5 = RefactorCtxMenu
	DW8x16 mMouseClickCmds{};

	bool m_fixSmartPtrOp = false;
	WCHAR mDefaultAddIncludeDelimiter = L'\0';
	bool mDisplayWiderScopeReferences = false; // used in find refs and rename
	bool mRenameWiderScopeReferences = false;  // used in rename dialog - default value of checkbox for inherited items
	bool mRenameCommentAndStringReferences =
	    false; // used in rename dialog - default value of checkbox for comment and string items
	bool m_UseVASuggestionsInManagedCode = false; // only VA Suggestions [in C#, VB,...]" applies also to C++ in vs2017+
	bool mAutoDisplayRefactoringButton = false;
	int m_alwaysUseThemedCompletionBoxSelection = 0;
	bool mExtendCommentOnNewline = false;
	bool mUseAutoHighlightRefsThread = false;
	bool mMethodsInFileNameFilter = false;
	bool mDialogsStickToMonitor = false;
	bool mDialogsFitIntoScreen = false;
	bool mAllowUncPaths = false;
	bool mCacheProjectInfo = false;
	bool mTrackCaretVisibility = false; // vs2010 for viemu compat
	bool mMethodInFile_ShowDefines = false;
	bool mMethodInFile_ShowProperties = false;
	bool mMethodInFile_ShowEvents = false;
	bool mScopeSuggestions = false;   // dependent on m_autoSuggest
	bool mOverrideRepairCase = false; // don't repair case in some circumstances
	bool mSuggestNullptr = false;
	bool mUnrealScriptSupport = false;
	bool mAddIncludePreferShortest = false;
	bool mColorBuildOutput = false;
	bool mFormatDoxygenTags = false;
	bool mUseNewFileOpen = false;
	char mFunctionCallParens[8]{0};
	bool mUseNewTheme11Plus = false;
	DWORD mBrowserAppCommandHandling = 0; // 0=none, 1=VA, 2=VS
	int mListBoxHeightInItems = 0;
	int mInitialSuggestionBoxHeightInItems = 0;
	bool mResizeSuggestionListOnUpDown = false;
	bool mDismissSuggestionListOnUpDown = false;
	bool m_bCompleteWithAnyVsOverride = false;
	bool mEnableProjectSymbolFilter = false;
	bool mEnableIconTheme = false;
	bool mFsisAltCache = false;
	bool m_bEnhColorFindResults = false;
	bool mMethodInFile_ShowScope = false;
	bool mSuppressAllListboxes = false;             // no members/completion/suggestion listboxes from VA
	bool mDisplayReferencesFromAllProjects = false; // used in find refs and rename
	bool mFindSymbolInSolutionUsesEditorSelection = false;
	bool mScopeTooltips = false;
	bool mSimpleWordMatchHighlights = false;
	bool mForceCaseInsensitiveFilters = false;
	bool mEnableFuzzyFilters = true;
	DWORD mFuzzyFiltersThreshold = 75;
	bool mEnableFuzzyLite = true;
	bool mEnableFuzzyMultiThreading = true;
	char mSurroundWithKeys[MAX_PATH] = "";
	enum GotoInterfaceBehavior
	{
		gib_none = 0,                   // no interface checking (pre-1836 behavior)
		gib_originalInterfaceCheck = 1, // original 1836 behavior
		gib_conditionalBcl = 2, // post-1936 default (uses original behavior or new behavior depending on context)
		gib_conditionalBclExpansive =
		    3,                 // when new behavior is used, it does so aggressively (hidden overrides become visible)
		gib_forceExpansive = 4 // force new aggressive/expansive always
	};
	DWORD mGotoInterfaceBehavior = gib_none; // see enum GotoInterfaceBehavior
	bool mEnumerateVsLangReferences = false;
	bool mRefactorAutoFormat = false;
	bool mRestrictVaToPrimaryFileTypes =
	    false; // [case: 75321] [case: 76721] VA active only in c/cpp/h/uc/cs/xaml (AVR c/cpp/h)
	bool mRestrictVaListboxesToC = false; // [case: 75322] VA members/completion/suggestion listboxes only in c/cpp/h/uc
	bool mVaAugmentParamInfo =
	    false; // [case: 75087] Prevents VA from displaying any of its own param info (see also m_ParamInfo)
	enum ClassMemberNamingBehavior
	{
		cmn_noChange = 0,             // no changes at all
		cmn_prefixDependent = 1,      // (default) make first letter upper if prefix empty or prefix ends in alpha
		cmn_onlyIfPrefixEndAlpha = 2, // make first letter upper only if prefix ends in alpha
		cmn_alwaysUpper = 3,          // always make first letter upper
		cmn_alwaysLower = 4,          // always make first letter lower
		cmn_last = cmn_alwaysLower    // for input verification
	};
	DWORD mClassMemberNamingBehavior = cmn_noChange;
	char m_plainTextExts[MAX_PATH] = "";
	bool mPartialSnippetShortcutMatches = false; // [case: 76428]
	bool mFindRefsDisplayUnknown = false;        // affects find refs only, not rename
	bool mFindRefsDisplayIncludes = false;       // affects find refs only, not rename
	bool mAutoListIncludes = false;              // [case: 7156]
	bool mUpdateHcbOnHover = false;
	bool mUsePreviewTab = false;
	enum VsThemeColorBehavior
	{
		vt_default = 0,      // light, blue, dark, unknown
		vt_unknownLight = 1, // treat non-default as if light
		vt_unknownDark = 2,  // treat non-default as if dark
		vt_forceLight = 3,   // assume light theme always
		vt_forceDark = 4     // assume dark theme always
	};
	DWORD mVsThemeColorBehavior =
	    vt_default; // [case: 78600] as of build 2036, this only affects decision to color tooltips
	bool mScopeFallbackToGotodef = false;              // [case: 79066]
	bool mCacheFileFinderData = false;                 // [case: 80692]
	bool mFindRefsAlternateSharedFileBehavior = false; // [case: 80212]
	bool mParseLocalTypesForGotoNav = false;           // [case: 81110][case: 81111]
	bool mEnableWin8ColorHook = false;                 // [case: 78670]
	bool mMethodInFile_ShowMembers = false;            // [case: 81768]
	bool mLookupNuGetRepository = false;               // [case: 79296] look for package directories
	bool mParseNuGetRepository = false;                // [case: 79296] parse package directories
	bool mIncludeDefaultParameterValues = false;       // [case: 3495]
	bool mEnableDebuggerStepFilter = false;            // [case: 73188]
	bool mRoamingStepFilterSolutionConfig =
	    false; // [case: 73188] roaming by default, option to store filter in the ".va\user\" directory
	DWORD mDimmedMenuItemOpacityValue = 0;         // [case: 83772]
	DWORD mImplementVirtualMethodsOptions = 0;     // [case: 55667]
	bool mEnableFilterStartEndTokens = false;      // [case: 5977]
	bool mUseGotoRelatedForGoButton = false;       // [case: 82578]
	DWORD mLinesBetweenMethods = 0;                // [case: 87039]
	bool mFindSimilarLocation = false;             // [case: 12454]
	bool mHashtagsGroupByFile = false;             // used in hashtags window
	bool mEditUserInputFieldsInEditor = false;     // [case: 24605]
	DWORD mMinimumHashtagLength = 0;               // [case: 86085]
	bool mInsertOpenBraceOnNewLine = false;        // [case: 84956]
	bool mIgnoreHexAlphaHashtags = false;          // [case: 89172]
	DWORD mSmartSelectPeekDuration = 0;            // [case: 87378]
	bool mEnableSurroundWithSnippetOnChar = false; // [case: 90489]
	bool mHashtagsAllowHypens = false;             // [case: 90329]
	bool mAddMissingDefaultSwitchCase = false;     // [case: 91463]
	enum ListBoxOverwriteBehavior
	{
		lbob_default,        // middle ground default
		lbob_neverOverwrite, // never overwrite text to immediate right of caret
		lbob_alwaysOverwrite // always overwrite text to immediate right of caret
	};
	DWORD mListboxOverwriteBehavior = lbob_default; // [case: 90899]
	bool mEncFieldPublicAccessors = false;          // [case: 91882]
	enum ChangeVariableVisibility
	{
		cvv_private = 0,
		cvv_protected,
		cvv_no,
		cvv_internal // C# only
	};
	int mEncFieldMoveVariable = cvv_private;                 // [case: 91192] see enum ChangeVariableVisibility
	bool mSparseSysLoad = false;                             // [case: 92495] [case: 97190]
	bool mVerifyDbOnLoad = false;                            // [case: 93136]
	bool mNavBarContext_DisplaySingleScope = false;          // [case: 82578]
	bool mSurroundWithSnippetOnCharIgnoreWhitespace = false; // [case: 93760]
	enum SmartPtrSuggestMode
	{
		spsm_parens = 0x1,       // ()
		spsm_openParen = 0x2,    // (
		spsm_parensWithEnd = 0x4 // ($end$)
	};
	DWORD mSmartPtrSuggestModes = 0; // [case: 92897] bitfield
	int mReparseIfNeeded = 0; // [case: 4568] -1: always, 0: never, other values: reparse only when the file size does
	                          // not exceed this value in bytes
	bool mTemplateMoveToSourceInTwoSteps = false;       // [case: 67727]
	bool mEnableEdMouseButtonHooks = false;             // [case: 94237]
	bool mHashTagsMenuDisplayGlobalAlways = false;      // [case: 96367]
	bool mSelectRecentItemsInNavigationDialogs = false; // [case: 96898]

	bool mSmartSelectEnableGranularStart = false; // [case: 90797] enables granular start (true by default)
	bool mSmartSelectEnableWordStart = false;     // [case: 90797] enables current word selection (true by default)
	bool mSmartSelectSplitWordByCase =
	    false; // [case: 97531] splits word by case if word start is enabled (false by default)
	bool mSmartSelectSplitWordByUnderscore =
	    false; // [case: 97531] splits word by underscore if word start is enabled (false by default)

	bool mEnableSortLinesPrompt = false; // [case: 29858] display dialog to choose sort parameters
	bool mUsePpl = false;              // [case: 96201] use ppl for parsing (does not affect ppl loops for UI commands)
	bool mDisplayHashtagXrefs = false; // [case: 96785]
	bool mFsisTooltips = false;        // [case: 98240]
	bool mOfisTooltips = false;        // [case: 98240]
	bool mBrowseMembersTooltips = false; // [case: 98240]
	bool mFsisExtraColumns = false;      // [case: 35181]
	enum GotoOverloadResolutionMode
	{
		GORM_DISABLED = 0,  // no overload resolution
		GORM_HIDE,          // hide non-compatible signatures
		GORM_USE_SEPARATOR, // put non-compatible signatures below a separator
	};
	int mGotoOverloadResolutionMode = GORM_DISABLED;        // [case: 53078] see enum GotoOverloadResolutionMode
	int mGotoRelatedOverloadResolutionMode = GORM_DISABLED; // [case: 98804] see enum GotoOverloadResolutionMode
	bool mSuggestNullInCppByDefault = false;                // [case: 98770]

	// Text Editor - Mouse Wheel Commands - due to [case: 91013]
	// Columns: 0 = Ctrl + Mouse-Wheel, 1 = Ctrl + Shift + Mouse-Wheel
	// Values:  0 = none, 1 = ZoomInOut, 2 = ExtendShrink, 3 = ExtendShrinkBlock
	DW8x16 mMouseWheelCmds{};

	bool mOfisIncludeSolution = false;      // [case: 1029] only relevant when RestrictToWorkspace is off
	bool mOfisIncludePrivateSystem = false; // [case: 1029] only relevant when RestrictToWorkspace is off
	bool mOfisIncludeWindowList = false;    // [case: 1029] only relevant when RestrictToWorkspace is off
	bool mOfisIncludeSystem = false;        // [case: 1029] only relevant when RestrictToWorkspace is off
	bool mOfisIncludeExternal = false;      // [case: 1029] only relevant when RestrictToWorkspace is off

	bool mClearNonSourceFileMatches = false; // [case: 101997]

	char mCreationTemplateFunctions[MAX_PATH] = ""; // [case: 1262]
	bool mGotoRelatedParameterTrimming = false;     // [case: 100402]
	bool mCodeInspection = false; // [case: 103675] read-only copy of state maintained by code inspection package
	bool mFindRefsFlagCreation =
	    false; // [case: 1262] the ability to completely disable the resolving and indication of references as creations
	bool mUnrealEngineCppSupport = false;          // [case: 104685] [case: 86215]
	bool mIncludeDirectiveCompletionLists = false; // [case: 105000]
	bool mForceUseOldVcProjectApi = false;         // [case: 103729] disabled by default; enable to override use of rule
	                                               // property storage in SolutionFiles.cpp for vs2017+
	bool mEnhanceMacroParsing =
	    false; // [case: 108472] disabled by default; replacement for LimitMacro and LimitMacroParsing
	bool mForceProgramFilesDirsSystem = false; // [case: 108561] enabled by default; disable to restore old behavior
	bool mOfisAugmentSolution = false;         // [case: 106017] disabled by default
	bool mAugmentHiddenExtensions = false;     // [case: 106017] enabled by default
	bool mOfisIncludeHiddenExtensions = false; // [case: 106017] disabled by default
	bool mForceVs2017ProjectSync =
	    false; // [case: 104119] disabled by default (only applicable in vs2017 && !gShellAttr->IsDevenv15u3OrHigher())
	bool mEnableMixedDpiScalingWorkaround = false; // [case: 93048] enabled by default
	bool mForceVcPkgInclude = false;               // [case: 109042] enabled by default
	bool mEnableVcProjectSync =
	    false; // [case: 104119] enabled by default (overrides mForceVs2017ProjectSync in vs2017 15.3+)
	int mWarnOnLoadOfPathWithReversedSlashes = 0; // [case: 110301] [case: 116843] 1 by default; 0 - never reload (no
	                                              // prompt), 1 - ask to reload, 2 - always reload (no prompt)
	DWORD mJavaDocStyle =
	    0; // [case: 111445] 1 by default, 2 for alternative style, any other value disables any substitution
	bool mUseAutoWithConvertRefactor =
	    false; // case: 7003 checkbox on the dialog "Convert Between Pointer and Instance"
	bool mAllowSnippetsInUnrealMarkup = false; // [case: 111552] [case: 113392] disabled by default; enable to restore
	                                           // snippet suggestions inside U* macros
	bool mAlwaysDisplayUnrealSymbolsInItalics =
	    false; // [case: 105950] enabled by default; disable to prevent special treatment of unreal installations (may
	           // still be italic depending on other rules)
	DWORD mIndexPlugins = 0; // [case: 144467] Index plugins 0: none, 1: referenced, 2: reserved, 3: reserved, 4: all
	bool mIndexGeneratedCode = false; // [case: 119653] disabled by default; enable to parse UE generated.h files
	bool mDisableUeAutoforatOnPaste = true; // [case: 141666] enabled by default; true disables autoformat on paste UE keywords
	bool mUseGitRootForAugmentSolution = false; // [case: 111480] enabled by default; disable to use the solution root
	DWORD mConvertToPointerType =
	    0; // [case: 113020] #convert_pointer_types 0: raw 1: unique_ptr 2: shared_ptr 3: custom ptr
	char mConvertCustomPtrName[MAX_PATH] = ""; // [case: 113020] last custom name of a smart pointer that user has
	                                           // entered in the Convert Instance to Pointer dialog
	char mConvertCustomMakeName[MAX_PATH] =
	    ""; // [case: 113020] last custom name of a function to create (smart) pointer, that user has entered in the
	        // Convert Instance to Pointer dialog
	DWORD mUnrealEngineAutoDetect = 0; // [case: 113964] 1 by default; 0 - disabled, 1 - enabled when solution contains
	                                   // project named UE4 or UE5, 2 - always enabled
	bool mOptionsHelp = false;         // [case: 114843] enabled by default
	bool mColorInsideUnrealMarkup = false;             // [case: 115156] disabled by default
	bool mFilterGeneratedSourceFiles = false;          // [case: 115343] enabled by default
	bool mOfisDisplayPersistentFilter = false;         // [case: 25837] enabled by default
	WCHAR mOfisPersistentFilter[MAX_PATH] = L"";       // [case: 25837] empty by default
	bool mOfisApplyPersistentFilter = false;           // [case: 117760] enabled by default
	bool mAddIncludeSkipFirstFileLevelComment = false; // [case: 117094] enabled by default
	enum AddIncludePathPreference                      // used for mAddIncludePathPreference, see below
	{
		PP_SHORTEST_POSSIBLE = 0, // default
		PP_RELATIVE_TO_FILE_OR_INCLUDE_DIR,
		PP_RELATIVE_TO_PROJECT,
	};
	DWORD mAddIncludePath =
	    PP_SHORTEST_POSSIBLE; // [case: 33851] the type of relative path that add include uses, see above enum
	enum FileLoadCodePageBehavior
	{
		FLCP_AutoDetect,
		FLCP_ForceUtf8,
		FLCP_ACP
	};
	DWORD mFileLoadCodePageBehavior = FLCP_AutoDetect; // [case: 105281]

	wchar_t mThirdPartyRegex[4096] = L""; // [case: 119844] L"(third|3rd)[ ]*party" by default

	enum InsertPathMode // [case: 114520] insert path
	{
		IPM_Absolute,
		IPM_RelativeToFile,
		IPM_RelativeToProject,
		IPM_RelativeToSolution,
	};
	DWORD mInsertPathMode = IPM_Absolute; // [case: 114520]

	bool mInsertPathFwdSlash = false;                 // true to use forward slashes
	bool mListboxCompletionRequiresSelection = false; // [case: 140535]
	bool mAllowSuggestAfterTab = false;               // [case: 140537]
	bool mWebEditorPmaFail = false;                   // [case: 141042] / [case: 141827]

	DWORD mRestrictFilesOpenedDuringRefactor = 0; // [case: 116948]

	bool mCloseHashtagToolWindowOnGoto = false; // [case: 115717]
	bool mEnableJumpToImpl =
	    false; // [case: 66885] jump directly to implementation by removing the declaration from MenuXP
	bool mEnableFilterWithOverloads =
	    false; // [case: 66885] (only when mEnableJumpToImpl is enabled) remove declarations with overloads or not
	bool mAggressiveMemoryCleanup = false; // [case: 142103]

	enum ChangeEncapsulateFieldTypeCppB
	{
		ceft_property = 0,
		ceft_geter_setter
	};
	int mEncFieldTypeCppB = ceft_property; // [case: 133418] see enum ChangeEncapsulateFieldTypeCppb

	bool mEnableEncFieldReadFieldCppB = false;    // [case: 133418]
	bool mEnableEncFieldReadVirtualCppB = false;  // [case: 133418]
	bool mEnableEncFieldWriteFieldCppB = false;   // [case: 133418]
	bool mEnableEncFieldWriteVirtualCppB = false; // [case: 133418]

	enum ChangePropertyVisibilityCppB
	{
		cpv_published = 0,
		cpv_public,
		cpv_private,
		cpv_protected,
		cpv_no
	};
	int mEncFieldMovePropertyCppB = cpv_published; // [case: 133418] see enum ChangePropertyVisibilityCppb

	DWORD mDPIAwareness = 0;
	bool mEnhanceVAOutlineMacroSupport = false;  // [case: 30076]
	bool mRestrictGotoMemberToProject = false;   // [case: 142090]
	DWORD mRebuildSolutionMinimumModPercent = 0; // [case: 142124]

	bool mForceExternalIncludeDirectories = false;

	enum ModifyExprFlags // [case: 27848] modify expression  #SharedEnum_ModifyExprFlags
	{
		ModExpr_None = 0,

		ModExpr_Invert = 0x0001,
		ModExpr_ToggleLogOps = 0x0002,
		ModExpr_ToggleRelOps = 0x0004,
		ModExpr_NotOpRemoval = 0x0008,
		ModExpr_ParensRemoval = 0x0010,
		ModExpr_ShowDiff = 0x0020, // only used in UI

		ModExpr_Default = ModExpr_Invert | ModExpr_ToggleLogOps | ModExpr_ToggleRelOps | ModExpr_NotOpRemoval |
		                  ModExpr_ParensRemoval | ModExpr_ShowDiff,

		ModExpr_SaveMask = 0x00ff, // limits what get saved

		ModExpr_IsCSharp = 0x1000, // not being saved
		ModExpr_NoDlg = 0x2000,    // not being saved
		ModExpr_Reparse = 0x4000,  // not being saved
	};
	DWORD mModifyExprFlags = ModExpr_None; // [case: 27848] see enum ModifyExprFlags
	bool m_srcExts_IsUpdated = false;      // [case: 144119]
	bool mEnableShaderSupport = true;	   // [case: 114070]
	bool mRespectVsCodeExcludedFiles = false; // [case: 144472]
	char m_ShaderExts[MAX_PATH] = "";
	bool mDoOldStyleMainMenu = false; 
	DWORD mAddIncludeSortedMinProbability; // [case: 149209]
	bool mEnableCudaSupport = false; // [case: 58450]
	DWORD mWatermarkProps = 0x00000040; // [case: 149836] 0xFFVVDDAA => FF-Flags, Disable=1, V-Value, D-Desaturation, A-Alpha
	bool mRecommendAfterExpires;        // [case: 144378]
	DWORD mRenewNotificationDays;	    // [case: 144378]
	bool mAddIncludeUnrealUseQuotation = true; // [case: 149198]
	DWORD mFirstRunDialogStatus = 0;	// [case: 149451] 0 - not shown, 1 - partial selected, 2 - full selected
	bool mUseSlashForIncludePaths = false; // [case: 164446]
	char m_minihelpInfo[0xff] = ""; // [case 164424] navbar column widths 
	bool mKeepAutoHighlightOnWS = false; // [case: 105134]
} Settings;

#ifdef __cplusplus
// wrapper for settings struct
class CSettings : public tagSettings
{
	void SaveRegBool(HKEY hKey, LPCTSTR name, bool* origin);
	void ReadRegBool(HKEY hKey, LPCTSTR name, bool* dest, bool defVal = true);
	void SaveRegDword(HKEY hKey, LPCTSTR name, DWORD* origin);
	void SaveRegString(HKEY hKey, LPCTSTR name, LPCTSTR value);
	void SaveRegString(HKEY hKey, LPCWSTR name, LPCWSTR value);
	void ReadRegColors();
	void SaveRegColors();
	bool RegValueExists(HKEY hKey, LPCTSTR name) const;

  public:
	void Init();
	void InitDefaults(HKEY hKey);
	void ReadRegDword(HKEY hKey, LPCTSTR name, DWORD* dest, DWORD defVal = 0);
	void ReadRegString(HKEY hKey, LPCTSTR name, LPTSTR dest, LPCTSTR defVal, DWORD len);
	void ReadRegString(HKEY hKey, LPCWSTR name, LPWSTR dest, LPCWSTR defVal, DWORD len);
	void CheckForConflicts();
	void ValidateExtSettings();
	bool IsAutomatchAllowed(bool funcFixUp = false, UINT key = 0);
	//	bool IsResharperIsPresent();
	bool UsingResharperSuggestions(int fileType, bool setTrue);
	void Commit();
	void SaveRegFirstRunDialogStatus(); // [case:149451]
	bool IsFirstRunAfterInstall(); // [case:149451]

	CSettings();
	CSettings(const CSettings &) = default;
	~CSettings();
	CSettings &operator =(const CSettings &) = default;
};

#ifndef ADDINSIDE
extern CSettings* Psettings;
#endif

const WCHAR* GetMainDllName();

#endif /* __cplusplus */

#endif
