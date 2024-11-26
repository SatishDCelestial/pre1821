#include "stdafxed.h"
#include <locale.h>
#include "settings.h"
#include "log.h"
#include "RegKeys.h"
#include <atlbase.h>
#include "mparse.h"      // for FDictionary.h
#include "FDictionary.h" // for DefCls
#include "Colors.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "Registry.h"
#include "TokenW.h"
#include "FileTypes.h"
#include "project.h"
#include "BuildInfo.h"
#include "wt_stdlib.h"
#include "Directories.h"
#include "VAAutomation.h"
#include "IdeSettings.h"
#include "DllNames.h"
#include "VaService.h"
#include "FileVerInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CSettings* Psettings = NULL;
CString ID_RK_APP = ID_RK_APP_KEY; // Will change to ;
static bool sOkToSaveVsMarkerValue = false;

void MigrateDevColor(EditColorStr* pColors);
void MigrateDevColorInternal(HKEY hDevKey, int idx, EditColorStr* pColors);
static void InitializeColors(EditColorStr* pColors);
static void MigrateAndMergeFileExtensions(HKEY hKey, LPCTSTR newName, LPCTSTR oldName, LPTSTR dest, LPCTSTR defVal,
                                          LPCTSTR oldValsToRemove, DWORD len);

#define ID_RK_SORT_DEF_LIST "SortDefList"
#define ID_RK_ENABLEPASTEMENU "EnablePasteMenu"
#define ID_RK_VAVIEWGOTODEF "VaViewGotoDefinition"
#define ID_RK_HIGHLIGHTREFERENCESBYDEFAULT "HighlightFindReferencesByDefault"
#define ID_RK_BRACEMATCHSTYLE "BraceAutoInsertStyle"
#define ID_RK_TOOLTIPSINFINDRESULTS "EnableTooltipsInFindResults"
#define ID_RK_SUGGESTIONSELECTSTYLE "ListboxSelectionStyle"
#define ID_RK_DISLPAYREFACTORINGBUTTON "DisplayRefactorButton"
#define ID_RK_AUTODISLPAYREFACTORINGBUTTON "AutoDisplayRefactorButton"
#define ID_RK_INCLUDEDEFAULTPARAMETERVALUES "IncludeDefaultParameterValues"
#define ID_RK_EXTENSIONLESSFILEISHEADER "ExtensionlessFileIsHeader"
#define ID_RK_QUICKINFOVS8 "vs8Tooltips"
#define ID_RK_CHECKFORLATESTVERSION "CheckForLatestVersion"
#define ID_RK_CHECKFORLATESTBETA "CheckForLatestBeta"
#define ID_RK_MARKCURRENTLINE "MarkCurrentLine"
#define ID_RK_CURRENTLINEMARKERSTYLE "CurrentLineMarkerStyle"
#define ID_RK_CURRENTLINEVISUALSTYLE "CurrentLineVisualStyle"
#define ID_RK_DONTINSERTSPACEAFTERCOMMENT "DontInsertSpaceAfterComment"
#define ID_RK_LINENUMBERSINFINDREFSRESULTS "ShowLineNumbersInFindRefResults"
#define ID_RK_ENABLESTATUSBARMESSAGES "EnableStatusBarMessages"
#define ID_RK_CLOSESUGGESTIONLISTONEXACTMATCH "CloseSuggestionListOnExactMatch"
#define ID_RK_NOISYEXECFAILURENOTIFICATIONS "MessageBoxFailureNotifications"
#define ID_RK_DISPLAY_COMMENT_REFERENCES "DisplayCommentAndStringReferences"
#define ID_RK_ENC_FIELD_PUBLIC_ACCESSORS "EncFieldPublicAccessors"
#define ID_RK_ENC_FIELD_MOVE_VARIABLE "EncFieldMoveVariable"
#define ID_RK_RENAME_COMMENT_REFERENCES "RenameCommentAndStringReferences"
#define ID_RK_FIND_INHERITED_REFERENCES "DisplayWiderScopeReferences"
#define ID_RK_FIND_REFERENCES_FROM_ALL_PROJECTS "DisplayReferencesFromAllProjects"
#define ID_RK_RENAME_WIDER_SCOPE_REFERENCES "RenameWiderScopeReferences"
#define ID_RK_INCLUDEREFACTORINGINLISTBOXES "IncludeRefactoringInListboxes"
#define ID_RK_AUTOSIZE_LISTBOX "AutoSizeListboxes"
#define ID_RK_LISTBOX_FLAGS "ListboxFlags"
#define ID_RK_PARSE_FILES_IN_DIR_IF_EXTERNAL "ParseFilesInDirectoryIfExternalFileOpened"
#define ID_RK_PARSE_FILES_IN_DIR_IF_EMPTY "ParseFilesInDirectoryIfSolutionEmpty2"
#define ID_RK_MARK_FIND_TEXT "MarkTextAfterFind"
#define ID_RK_PROJECT_NODE_IN_RESULTS "DisplayProjectNodesInReferencesResults"
#define ID_RK_PROJECT_NODE_IN_REN_RESULTS "DisplayProjectNodesInRenameResults"
#define ID_RK_HTML_EXTS "ExtHtml3"
#define ID_RK_CS_EXTS "ExtCS2"
#define ID_RK_VB_EXTS "ExtVB3"
#define ID_RK_ASP_EXTS "ExtAsp2"
#define ID_RK_XML_EXTS "ExtXml2"
#define ID_RK_EXTS_TO_IGNORE "ExtensionsToIgnore3"
#define ID_RK_BACKGROUND_TOMATO "EnableUiTomato"
#define ID_RK_MIF_REGIONS "MethodsInFile_Regions"
#define ID_RK_MIF_SCOPE "MethodsInFile_Scope"
#define ID_RK_MIF_DEFINES "MethodsInFile_Defines"
#define ID_RK_MIF_MEMBERS "MethodsInFile_Members"
#define ID_RK_MIF_PROPERTIES "MethodsInFile_Properties"
#define ID_RK_MIF_EVENTS "MethodsInFile_Events"
#define ID_RK_SuppressListboxes "SuppressListboxes"
#define ID_RK_FsisUseEditorSelection "PopulateFsisFilterWithEditorSelection"
#define ID_RK_SELECT_IMPLEMENTATION "SelectImplementation"
#define ID_RK_AUTO_HIGHLIGHT_REFS "AutoHighlightReferences2"
#define ID_RK_AUTO_HIGHLIGHT_REFS_THREADED "AutoHighlightReferencesEx"
#define ID_RK_OPTIMIZE_REMOTE_DESKTOP "OptimizeRemoteDesktop"
#define ID_RK_USE_MARKER_API "UseVsMarkers"
#define ID_RK_CTRL_CLICK_GOTO "GotoOnCtrlLeftClick"
#define ID_RK_MOUSE_CLICK_CMDS "MouseClickCommands"
#define ID_RK_MOUSE_WHEEL_CMDS "MouseWheelCommands"
#define ID_RK_FIX_SMART_PTR "FixSmartPtrOperators"
#define ID_RK_DEFAULTINCLUDEDELIMITER "DefaultAddIncludeDelimiter"
#define ID_RK_SUGGESTIONS_IN_MANAGED_CODE "UseVASuggestionsInManagedCode"
#define ID_RK_USE_THEMED_COMPLETIONBOX_SELECTION "UseThemedCompletionBoxSelection2"
#define ID_RK_EXTEND_COMMENT_ON_NEWLINE "ExtendCommentsOnNewline"
#define ID_RK_MAX_SCREEN_REPARSE "MaximumScreenReparseLineCount"
#define ID_RK_MEMBERS_COMPLETE_ON_ANY "CompleteMembersWithAny"
#define ID_RK_MAX_FILE_SIZE "LargeFileSizeThreshold"
#define ID_RK_ADDINCLUDE_STYLE "AddIncludeTokenStyle"
#define ID_RK_LISTBOX_OVERWRITE_BEHAVIOR "CompletionOverwriteBehavior"
#define ID_RK_Smart_Ptr_Suggest_Mode "SmartPointerSuggestionStyles"
#define ID_RK_PARAM_IN_MIF "DisplayParamsInMethodsList"
#define ID_RK_MIF_NAME_FILTER "MethodsListNameFilter"
#define ID_RK_CPP_OVERRIDE_KEYWORD "UseOverrideKeywordInImplementInterface"
#define ID_RK_CPP_VIRTUAL_KEYWORD "UseVirtualKeywordInImplementInterface"
#define ID_RK_DIALOGS_STICK_TO_MONITOR "DialogsStickToMonitor"
#define ID_RK_DIALOGS_FIT_INTO_SCREEN "DialogsFitIntoScreen"
#define ID_RK_UNC_PATHS "AllowUncPaths"
#define ID_RK_DIRTY_FIND_REFS_NAV "AllowGotoOnDirtyFindRefsNav"
#define ID_RK_PROJECT_INFO_CACHE "CacheProjectInfo"
#define ID_RK_TRACK_CARET_VISIBILITY "TrackCaretVisibility"
#define ID_RK_SCOPESUGGEST "ScopedSuggestions"
#define ID_RK_OVERRIDE_REPAIR_CASE "OverrideRepairCase1"
#define ID_RK_SUGGEST_NULLPTR "NullptrSuggestionDefault1"
#define ID_RK_SUGGEST_NULL_BY_DEFAULT "NullSuggestionDefault"
#define ID_RK_UNREAL_SUPPORT "EnableUC"
#define ID_RK_UNREAL_CPP_SUPPORT "EnableUnrealEngineC++"
#define ID_RK_PREFER_SHORTEST_ADD_INCLUDE "AddIncludePreferShortestRelativePath"
#define ID_RD_ADD_INCLUDE_PATH_PREFERENCE "AddIncludePathPreference"
#ifdef AVR_STUDIO
#define ID_RK_FORMAT_DOXYGEN_TAGS "FormatDoxygenTags2"
#else
#define ID_RK_FORMAT_DOXYGEN_TAGS "FormatDoxygenTags"
#endif // AVR_STUDIO
#define ID_RK_LINE_COMMENT_SLASH_COUNT "LineCommentSlashCount"
#define ID_RK_USE_NEW_FILE_OPEN "NewFileOpen"
#define ID_RK_COLOR_BUILD_OUTPUT "ColorBuildOutput"
#define ID_RK_COLOR_BUILD_OUTPUT_CUSTOM_BG "ColorBuildOutput_bg"
#define ID_RK_FUNCTION_CALL_PAREN_STRING "FunctionCallParens"
#define ID_RK_USE_NEW_THEME_DEV11_PLUS "EnableDev11Theme"
#define ID_RK_HANDLE_BROWSER_APP_COMMANDS "BrowserAppCommands"
#define ID_RK_ListBoxHeightInItems "ListBoxHeightInItems"
#define ID_RK_InitialSuggestionBoxHeightInItems "InitialSuggestionBoxHeightInItems"
#define ID_RK_ResizeSuggestionListOnUpDown "ResizeSuggestionListOnUpDown"
#define ID_RK_DismissSuggestionListOnUpDown "DismissSuggestionListOnUpDown"
#define ID_RK_EnableFilterStartEndTokens "EnableFilterStartAndEndTokens"
#define ID_RK_EnableIconTheme "EnableIconTheme"
#define ID_RK_EnableProjectSymbolFilter "EnableProjectSymbolFilter"
#define ID_RK_FsisAltCache "EnableFsisAltDir"
#define ID_RK_SurroundWithKeys "SurroundSelectionCharList"
#define ID_RK_ScopeTooltips "ShowScopeTooltips"
#define ID_RK_SimpleWordMatchHighlights "SimpleWordMatchHighlights"
#define ID_RK_ForceCaseInsensitveFilters "ForceCaseInsensitiveFilters"
#define ID_RK_EnableFuzzyFilters "EnableFuzzyFilters2"
#define ID_RK_FuzzyFiltersThreshold "FuzzyFiltersThreshold"
#define ID_RK_EnableFuzzyLite "EnableFuzzyLite2"
#define ID_RK_EnableFuzzyMultiThreading "EnableFuzzyMultiThreading"
#define ID_RK_ClassMemberNamingBehavior "ClassMemberNamingBehavior"
#define ID_RK_DimmedMenuItemOpacity "DimmedMenuitemOpacity"
#define ID_RK_GotoInterfaceBehavior "GotoInterfaceBehavior"
#define ID_RK_VsThemeColorBehavior "VsThemeColorBehavior"
#define ID_RK_EnumerateVsLangReferences "EnumerateVsLangReferences2"
#define ID_RK_RefactorAutoFormat "AutoFormatRefactorings"
#define ID_RK_RestrictVaToPrimaryFileTypes "RestrictVaToPrimaryFileTypes"
#define ID_RK_RestrictVaListboxesToC "RestrictVaListboxesToC/C++"
#define ID_RK_AugmentParamInfo "AugmentParamInfo"
#define ID_RK_PartialSnippetShortcutMatches "PartialSnippetShortcutMatches"
#define ID_RK_FindRefsDisplayIncludes "FindRefsDisplayIncludes"
#define ID_RK_FindRefsDisplayUnknown "FindRefsDisplayUnknown"
#define ID_RK_UsePreviewTab "UsePreviewTab"
#define ID_RK_AutoListIncludes "AutoListIncludes"
#define ID_RK_UpdateHcbOnHover "UpdateHcbOnMouseOver"
#define ID_RK_ScopeFallbackToGotodef "AllowScopeFallback"
#define ID_RK_CacheFileFinderData "CacheFileFinderData"
#define ID_RK_FindRefsAltSharedFileBehavior "FindRefsAlternateSharedFileBehavior"
#define ID_RK_ParseLocalTypesForGotoNav "ParseLocalTypesForGotoNav"
#define ID_RK_EnableWin8ColorHook "EnableWin8ColorHook"
#define ID_RK_NugetDirLookup "EnableNuGetRepositoryLookup2"
#define ID_RK_NugetDirParse "ParseNuGetRepositoryFiles2"
#define ID_RK_EnableDbgStepFilter "EnableDebuggerStepFilter" // referenced in DebuggerTools project also
#define ID_RK_RoamingStepFilterSolutionConfig "RoamingStepFilterSolutionConfig"
#define ID_RK_UseGotoRelatedForGoButton "UseGotoRelatedForGoButton"
#define ID_RK_LinesBetweenMethods "LinesBetweenMethods"
#define ID_RK_DisableFindSimilarLocation "FindSimilarLocation"
#define ID_RK_HashtagsGroupByFile "HashtagsGroupByFile"
#define ID_RK_HashtagsMinimumTagLength "HashtagsMinTagLength"
#define ID_RK_EditUserInputInEditor "EditUserInputFieldsInEditor2"
#define ID_RK_EnableCodeInspection "EnableCodeInspections" // [case: 149451] todo: disabled to backout functionality; rename to EnableCodeInspections2
#define ID_RK_InsertOpenBraceOnNewLine "InsertOpenBraceOnNewLine"
#define ID_RK_HashtagsIgnoreHexAlphaTags "HashtagsIgnoreHexAlphaTags"
#define ID_RK_HashtagsAllowHyphens "HashtagsAllowHyphens"
#define ID_RK_SurroundWithSnippetOnChar "SurroundWithSnippetOnChar"
#define ID_RK_SurroundWithSnippetOnCharIgnoreWhitespace "SurroundWithSnippetOnCharIgnoreWhitespace2"
#define ID_RK_EdMouseButtonHooks "EnableEdMouseButtonHooks"
#define ID_RK_SmartSelectPeekDuration "SmartSelectPeekDuration"
#define ID_RK_SmartSelectEnableGranularStart "SmartSelectEnableGranularStart"
#define ID_RK_SmartSelectEnableWordStart "SmartSelectEnableWordStart"
#define ID_RK_SmartSelectSplitWordByCase "SmartSelectSplitWordByCase"
#define ID_RK_SmartSelectSplitWordByUnderscore "SmartSelectSplitWordByUnderscore"
#define ID_RK_AddMissingDefaultSwitchCase "AddMissingDefaultSwitchCase"
#define ID_RK_MoveTemplatesToSourceInTwoSteps "MoveTemplateImplToSourceInTwoSteps"
#define ID_RK_AlwaysDisplayGlobalMenuInHashtagMenu "DisplayGlobalHashtagMenuItems2"
#define ID_RK_SparseSystemIncludeLoad "SparseSysDbLoad2"
#define ID_RK_VerifyDbOnLoad "VerifyDbOnLoad"
#define ID_RK_NavBarContextDisplayScopeSingleLevel "NavBarContext_SingleScope"
#define ID_RK_UseOldUncommentBehavior "UseOldUncommentBehavior"
#define ID_RK_SelectRecentItemsInNav "SelectRecentItemsInNavigationDialogs"
#define ID_RK_ReparseIfNeeded "ReparseIfNeededMaxFileSize2"
#define ID_RK_EnableSortLinesPrompt "EnableSortLinesPrompt"
#define ID_RK_UsePpl "ConcurrentParsing"
#define ID_RK_DisplayHashtagXrefs "DisplayHashtagXrefsInWindow"
#define ID_RK_ClearNonSourceFileMatches "ClearNonSourceFileMatches"
#define ID_RK_OfisTooltips "OfisTooltips"
#define ID_RK_OfisIncludeSolution "OfisIncludeSolution"
#define ID_RK_OfisIncludePrivateSystem "OfisIncludePrivateSystem"
#define ID_RK_OfisIncludeSystem "OfisIncludeSystem"
#define ID_RK_OfisIncludeExternal "OfisIncludeExternal"
#define ID_RK_OfisIncludeWindowList "OfisIncludeWindowList"
#define ID_RK_FsisTooltips "FsisTooltips"
#define ID_RK_BrowseMembersTooltips "BrowseMembersTooltips"
#define ID_RK_FsisExtraColumns "FsisExtraColumns"
#define ID_RK_GotoOverloadResolutionMode "GotoOverloadResolutionMode"
#define ID_RK_GotoRelatedOverloadResolutionMode "GotoRelatedOverloadResolutionMode"
#define ID_RK_CreationTemplateFunctionNames "CreationTemplateFunctionNames"
#define ID_RK_GotoRelatedParameterTrimming "GotoRelatedParameterTrimming"
#define ID_RK_EnableFindRefsFlagCreation "FindRefsFlagCreation"
#define ID_RK_IncludeDirectiveCompletionLists "IncludeDirectiveCompletionLists"
#define ID_RK_ForceUseOfOldVcProjectApi "ForceUseOfOldVcProjectApi"
#define ID_RK_EnhanceMacroParsing "EnhanceMacroParsing"
#define ID_RK_ForceProgramFilesDirSystem "ProgramFilesDirsAreSystemIncludes"
#define ID_RK_OfisAugmentSolution "OfisAugmentSolutionFileList"
#define ID_RK_AugmentHiddenExtensions "AugmentHiddenExtensions"
#define ID_RK_OfisIncludeHiddenExtensions "OfisIncludeHiddenExtensions"
#define ID_RK_ForceVs2017ProjectSync "ForceVs2017ProjectSync"
#define ID_RK_EnableMixedDpiScalingWorkaround "EnableMixedDpiScalingWorkaround"
#define ID_RK_ForceVcPkgInclude "ForceVcpkgInclude"
#define ID_RK_EnableVcProjectSync "EnableVcProjectSync"
#define ID_RK_WarnOnLoadOfPathWithReversedSlashes "WarnOnLoadOfPathWithReversedSlashes"
#define ID_RK_JavaDocStyle "JavaDocStyle"
#define ID_RK_AvoidDuplicatingTypeWithConvert "AvoidDuplicatingTypeWithConvert"
#define ID_RK_ALLOW_SNIPPETS_IN_UNREAL_MARKUP "AllowSnippetsInUnrealMarkup"
#define ID_RK_ALWAYS_DISPLAY_UNREAL_SYMBOLS_IN_ITALICS "AlwaysDisplayUnrealSymbolsInItalics"
#define ID_RK_INDEX_PLUGINS "IndexPluginsOpt"
#define ID_RK_INDEX_GENERATED_CODE "IndexGeneratedCode"
#define ID_RK_DISABLE_UE_AUTOFORMAT_ON_PASTE "DisableUeAutoformatOnPaste"
#define ID_RK_USE_GIT_ROOT_FOR_AUGMENT_SOLUTION "UseGitRootForAugmentSolution"
#define ID_RK_ConvertToPointerType "ConvertRefactorPointerType"
#define ID_RK_FileLoadCodePage "FileLoadCodePageBehavior"
#define ID_RK_ConvertCustomPointerName "ConvertCustomPointerName"
#define ID_RK_ConvertCustomMakeName "ConvertCustomMakeName"
#define ID_RK_UnrealEngineAutoDetect "UnrealEngineAutoDetect"
#define ID_RK_OptionsHelp "EnableOptionsHelp"
#define ID_RK_ColorInsideUnrealMarkup "ColorInsideUnrealMarkup"
#define ID_RK_FilterGeneratedSourceFiles "FilterGeneratedSourceFiles"
#define ID_RK_OfisPersistentFilter L"OFIS_PersistentFilter"
#define ID_RK_OfisDisplayPersistentFilter "OFIS_DisplayPersistentFilter"
#define ID_RK_OfisApplyPersistentFilter "OFIS_ApplyPersistentFilter"
#define ID_RK_AddIncludeSkipFirstFileLevelComment "AddIncludeSkipFirstFileLevelComment"
#define ID_RK_ThirdPartyRegex "ThirdPartyRegex"
#define ID_RK_InsertPathMode "InsertPathMode"
#define ID_RK_InsertPathFwdSlash "InsertPathFwdSlash"
#define ID_RK_ListboxCompletionRequiresSelection "ListboxCompletionRequiresSelection"
#define ID_RK_AllowSuggestAfterTab "AllowSuggestionsAfterTab"
#define ID_RK_WebEditorPmaFail "WebEditorPmaFail"
#define ID_RK_CloseHashtagToolwindowOnGoto "CloseHashtagToolwindowOnGoto"
#define ID_RK_RestrictFilesOpenedDuringRefactor "RestrictFilesOpenedDuringRefactor"
#define ID_RK_EnableGotoJumpsToImpl "EnableGotoJumpstoImpl"
#define ID_RK_EnableGotoFilterWIthOverloads "EnableGotoFilterWIthOverloads"
#define ID_RK_DPIAwareness "DPIAwareness" // [case: 142236] change also: #DPIAwarenessSetting
#define ID_RK_EnhanceVAOutlineMacroSupport "EnhanceVAOutlineMacroSupport"
#define ID_RK_RestrictGotoMemberToProject "RestrictGotoMemberToProject"
#define ID_RK_RebuildSolutionMinimumModCnt "RebuildSolutionMinimumModificationPercent"
#define ID_RK_ModifyExpressionFlags "ModifyExpressionFlags"
#define ID_RK_FORCE_EXTERNAL_INCLUDE_DIRECTORIES "ForceExternalIncludeDirectories"
#define ID_RK_RESPECT_VS_CODE_EXCLUDED_FILES "RespectVsCodeExcludedFiles_ALPHA"
#define ID_RK_DoOldStyleMainMenu "DoOldStyleMainMenu"
#define ID_RK_AddIncludeSortedMinProbability "AddIncludeSortedMinProbability"
#define ID_RK_EnableCudaSupport "EnableCudaSupport"
#define ID_RK_WatermarkProperties "WatermarkProperties"
#define ID_RK_RECOMMENDAFTEREXPIRES "RecommendUpdateAfterLicenseExpires"
#define ID_RK_RenewNotificationDays "RenewNotificationDays"
#define ID_RK_AddIncludeUnrealUseQuotation "AddIncludeUnrealUseQuotation"
#define ID_RK_FirstRunDialogStatus "FirstRunDialogStatus"
#define ID_RK_UseSlashForIncludePaths "UseSlashForIncludePaths"
#define ID_RK_MINIHELP_INFO "MinihelpInfo"
#define ID_RK_KeepAutoHighlightOnWS "KeepAutoHighlightOnWS"

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
#define ID_RK_ENC_FIELD_READ_FIELD "EncFieldReadFieldCppB"
#define ID_RK_ENC_FIELD_READ_VIRTUAL "EncFieldReadVirtualCppB"
#define ID_RK_ENC_FIELD_WRITE_FIELD "EncFieldReadFieldCppB"
#define ID_RK_ENC_FIELD_WRITE_VIRTUAL "EncFieldReadVirtualCppB"
#define ID_RK_ENC_FIELD_TYPE "EncFieldTypeCppB"
#define ID_RK_ENC_FIELD_MOVE_PROPERTY "EncFieldMovePropertyCppB"
#endif

CSettings::CSettings()
{
	LOG2("CSettings ctor");
	Init();
}

void CSettings::Commit()
{
#if defined(VA_CPPUNIT)
	// do not commit to registry during unit tests
	return;
#else
	// save options to reg
	HKEY hKey;
	DWORD tmp = 0;
#if !defined(RAD_STUDIO)
	SaveRegColors(); // this will create key if it doesn't exist
	SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "InitColoringOptions", "No");
#endif
	SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "ResetOptions", "No");

	LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_WRITE, &hKey);
	if (ERROR_SUCCESS != err)
	{
		// create base tree
		err = RegCreateKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, (LPSTR) "", REG_OPTION_NON_VOLATILE,
		                     KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, &tmp);
		err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_WRITE, &hKey);
	}

	if (ERROR_SUCCESS == err)
	{
#ifdef AVR_STUDIO
		SaveRegBool(hKey, _T(ID_RK_ENABLED), &m_enableVA);
#endif // AVR_STUDIO
		SaveRegDword(hKey, _T("FastProjectOpen2"), &m_FastProjectOpen);
#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T("SurroundSelection"), &m_SelectionOnChar);
		SaveRegDword(hKey, _T("UnderlineSpellingErrors"), &m_spellFlags);
		SaveRegBool(hKey, _T(ID_RK_RTFCOPY), &m_RTFCopy);
		SaveRegBool(hKey, _T(ID_RK_COLORPRINTING), &m_ColorPrinting);
		SaveRegBool(hKey, _T(ID_RK_ENHANCEDFORMAT), &m_ActiveSyntaxColoring);
		SaveRegBool(hKey, _T(ID_RK_MINIHELPPLACEMENT), &minihelpAtTop);
		SaveRegBool(hKey, _T("HideContextDefWindow"), &m_noMiniHelp);
		SaveRegBool(hKey, _T(ID_RK_BOLDBRACEMATCH), &boldBraceMatch);
		SaveRegBool(hKey, _T(ID_RK_AUTOMATCH), &AutoMatch);
		SaveRegBool(hKey, _T(ID_RK_CORRECTCASE), &CaseCorrect);
		SaveRegBool(hKey, _T(ID_RK_ONESTEPHELP), &oneStepHelp);
		SaveRegBool(hKey, _T(ID_RK_ASCIIW32API), &m_AsciiW32API);
		SaveRegBool(hKey, _T(ID_RK_AUTOSUGGEST), &m_autoSuggest);
		SaveRegBool(hKey, _T(ID_RK_SCOPESUGGEST), &mScopeSuggestions);
		SaveRegBool(hKey, _T(ID_RK_OVERRIDE_REPAIR_CASE), &mOverrideRepairCase);
		SaveRegBool(hKey, _T(ID_RK_SUGGEST_NULLPTR), &mSuggestNullptr);
		SaveRegBool(hKey, _T(ID_RK_SUGGEST_NULL_BY_DEFAULT), &mSuggestNullInCppByDefault);
		SaveRegBool(hKey, _T(ID_RK_PREFER_SHORTEST_ADD_INCLUDE), &mAddIncludePreferShortest);
		SaveRegDword(hKey, _T(ID_RD_ADD_INCLUDE_PATH_PREFERENCE), (DWORD*)&mAddIncludePath);
		SaveRegBool(hKey, _T(ID_RK_FORMAT_DOXYGEN_TAGS), &mFormatDoxygenTags);
		SaveRegBool(hKey, _T(ID_RK_USE_NEW_FILE_OPEN), &mUseNewFileOpen);
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			SaveRegBool(hKey, _T(ID_RK_USE_NEW_THEME_DEV11_PLUS), &mUseNewTheme11Plus);
		SaveRegDword(hKey, _T(ID_RK_HANDLE_BROWSER_APP_COMMANDS), &mBrowserAppCommandHandling);
		SaveRegDword(hKey, _T(ID_RK_LINE_COMMENT_SLASH_COUNT), &mLineCommentSlashCount);
		// SaveRegBool(hKey, _T(ID_RK_UNREAL_SUPPORT), &mUnrealSupport);
		SaveRegBool(hKey, _T(ID_RK_UNREAL_CPP_SUPPORT), &mUnrealEngineCppSupport);
		SaveRegBool(hKey, _T(ID_RK_TABINVOKESINTELLISENSE), &m_tabInvokesIntellisense);
#ifdef _DEBUG
		SaveRegBool(hKey, _T(ID_RK_VAWORKSPACE), &m_VAWorkspace);
#endif
		SaveRegBool(hKey, _T(ID_RK_SMARTPASTE), &m_smartPaste);
		SaveRegBool(hKey, _T(ID_RK_AUTOBACKUP), &m_autoBackup);
		SaveRegString(hKey, _T(ID_RK_IFDEFSTR), m_ifdefString);
		SaveRegDword(hKey, _T(ID_RK_FNPARENGAP), &m_fnParenGap);
		SaveRegDword(hKey, _T(ID_RK_WORKSPACEITEMCNT), &m_RecentCnt);
		SaveRegDword(hKey, _T(ID_RK_CLIPBOARDITEMCNT), &m_clipboardCnt);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_SKIPLOCALMACROS), &m_skipLocalMacros);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_MOUSEOVERS), &m_mouseOvers);
		if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
			SaveRegBool(hKey, _T(ID_RK_QUICKINFOVS8), &mQuickInfoInVs8);
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			SaveRegBool(hKey, _T(ID_RK_EnableIconTheme), &mEnableIconTheme);
		SaveRegBool(hKey, _T(ID_RK_MULTIPLEPROJSUPPORT), &m_multipleProjSupport);
		SaveRegBool(hKey, _T(ID_RK_KEEPBOOKMARKS), &m_keepBookmarks);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_MODTHREAD), &m_doModThread);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T("DefGuess2"), &m_defGuesses);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_AGGRESSIVEMATCH), &m_aggressiveFileMatching);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_FIXPTROP), &m_fixPtrOp);
		SaveRegBool(hKey, _T(ID_RK_SHOWSCOPE), &m_showScopeInContext);
		SaveRegBool(hKey, _T("BoldNonInheritedMembers"), &m_bBoldNonInheritedMembers);
		SaveRegBool(hKey, _T("ShrinkMemberListboxes"), &m_bShrinkMemberListboxes);
		SaveRegDword(hKey, _T("MRUOptions"), &m_nMRUOptions);
		SaveRegDword(hKey, _T("HCBOptions"), &m_nHCBOptions);
		SaveRegBool(hKey, _T("CompleteWithTab"), &m_bCompleteWithTab);
		SaveRegBool(hKey, _T("CompleteWithReturn"), &m_bCompleteWithReturn);
		SaveRegBool(hKey, _T("CompleteWithAny"), &m_bCompleteWithAny);
		SaveRegBool(hKey, _T("CompleteWithAnyVsOverride"), &m_bCompleteWithAnyVsOverride);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EnableProjectSymbolFilter), &mEnableProjectSymbolFilter);
		SaveRegBool(hKey, _T(ID_RK_FsisAltCache), &mFsisAltCache);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_ScopeTooltips), &mScopeTooltips);
		SaveRegBool(hKey, _T(ID_RK_SimpleWordMatchHighlights), &mSimpleWordMatchHighlights);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_ForceCaseInsensitveFilters), &mForceCaseInsensitiveFilters);
// 		SaveRegBool(hKey, _T(ID_RK_EnableFuzzyFilters), &mEnableFuzzyFilters);
// 		SaveRegDword(hKey, _T(ID_RK_FuzzyFiltersThreshold), &mFuzzyFiltersThreshold);
#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T(ID_RK_ClassMemberNamingBehavior), &mClassMemberNamingBehavior);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_DimmedMenuItemOpacity), &mDimmedMenuItemOpacityValue);
		SaveRegDword(hKey, _T(ID_RK_GotoInterfaceBehavior), &mGotoInterfaceBehavior);
#if !defined(RAD_STUDIO)
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			SaveRegDword(hKey, _T(ID_RK_VsThemeColorBehavior), &mVsThemeColorBehavior);
		SaveRegBool(hKey, _T("EnhColorListboxes"), &m_bEnhColorListboxes);
		SaveRegBool(hKey, _T(ID_RK_EnumerateVsLangReferences), &mEnumerateVsLangReferences);
#ifndef AVR_STUDIO
		SaveRegBool(hKey, _T(ID_RK_RestrictVaToPrimaryFileTypes), &mRestrictVaToPrimaryFileTypes);
		SaveRegBool(hKey, _T(ID_RK_RestrictVaListboxesToC), &mRestrictVaListboxesToC);
#endif // !AVR_STUDIO
		SaveRegBool(hKey, _T(ID_RK_AugmentParamInfo), &mVaAugmentParamInfo);
		SaveRegBool(hKey, _T(ID_RK_PartialSnippetShortcutMatches), &mPartialSnippetShortcutMatches);
		SaveRegBool(hKey, _T(ID_RK_FindRefsDisplayIncludes), &mFindRefsDisplayIncludes);
		SaveRegBool(hKey, _T(ID_RK_FindRefsDisplayUnknown), &mFindRefsDisplayUnknown);
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
			SaveRegBool(hKey, _T(ID_RK_UsePreviewTab), &mUsePreviewTab);
		SaveRegBool(hKey, _T(ID_RK_AutoListIncludes), &mAutoListIncludes);
		SaveRegBool(hKey, _T(ID_RK_UpdateHcbOnHover), &mUpdateHcbOnHover);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_ScopeFallbackToGotodef), &mScopeFallbackToGotodef);
		SaveRegBool(hKey, _T(ID_RK_CacheFileFinderData), &mCacheFileFinderData);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_FindRefsAltSharedFileBehavior), &mFindRefsAlternateSharedFileBehavior);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_ParseLocalTypesForGotoNav), &mParseLocalTypesForGotoNav);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_EnableWin8ColorHook), &mEnableWin8ColorHook);
		SaveRegBool(hKey, _T(ID_RK_NugetDirLookup), &mLookupNuGetRepository);
		SaveRegBool(hKey, _T(ID_RK_NugetDirParse), &mParseNuGetRepository);
#ifndef AVR_STUDIO
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			SaveRegBool(hKey, _T(ID_RK_EnableDbgStepFilter), &mEnableDebuggerStepFilter);
			SaveRegBool(hKey, _T(ID_RK_RoamingStepFilterSolutionConfig), &mRoamingStepFilterSolutionConfig);
		}
#endif // !AVR_STUDIO

		SaveRegBool(hKey, _T("EnhColorSourceWindows"), &m_bEnhColorSourceWindows);
		SaveRegBool(hKey, _T("EnhColorObjectBrowser"), &m_bEnhColorObjectBrowser);
		SaveRegBool(hKey, _T("EnhColorTooltips"), &m_bEnhColorTooltips);
		SaveRegBool(hKey, _T("EnhColorViews"), &m_bEnhColorViews);
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			SaveRegBool(hKey, _T("EnhColorFindResults"), &m_bEnhColorFindResults);
		if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
			SaveRegBool(hKey, _T(ID_RK_RefactorAutoFormat), &mRefactorAutoFormat);
		SaveRegBool(hKey, _T("EnhColorWizardBar"), &m_bEnhColorWizardBar);
		SaveRegBool(hKey, _T("LocalSymbolsInBold"), &m_bLocalSymbolsInBold);
		SaveRegBool(hKey, _T("GiveCommentsPrecedence"), &m_bGiveCommentsPrecedence);
		SaveRegBool(hKey, _T("StableSymbolsInItalics"), &m_bStableSymbolsInItalics);
		SaveRegBool(hKey, _T("ListNonInheritedMembersFirst"), &m_bListNonInheritedMembersFirst);
		SaveRegDword(hKey, _T("DisplayXSuggestions"), &m_nDisplayXSuggestions);
		SaveRegBool(hKey, _T("SupressUnderlines"), &m_bSupressUnderlines);

		SaveRegDword(hKey, _T(ID_RK_IMPORTTIMEOUT), &m_ImportTimeout);
		SaveRegBool(hKey, _T(ID_RK_RAPIDFIRE), &m_rapidFire);
		SaveRegBool(hKey, _T(ID_RK_COLINDICATOR), &m_colIndicator);
		SaveRegBool(hKey, _T(ID_RK_PARAMINFO), &m_ParamInfo);
		SaveRegBool(hKey, _T(ID_RK_AUTOCOMPLETE), &m_AutoComplete);
		SaveRegBool(hKey, _T(ID_RK_AUTOCOMMENTS), &m_AutoComments);
		SaveRegDword(hKey, _T(ID_RK_COLINDICATORCOLS), &m_colIndicatorColPos);
		SaveRegBool(hKey, _T(ID_RK_CODETEMPTOOL), &m_codeTemplateTooltips);
		SaveRegBool(hKey, _T(ID_RK_AUTOM_), &m_auto_m_);
		SaveRegBool(hKey, _T(ID_RK_UNDERLINETYPOS), &m_underlineTypos);
		SaveRegBool(hKey, _T(ID_RK_CONTEXTPREFIX), &m_contextPrefix);
		SaveRegBool(hKey, _T(ID_RK_BRACEMISMATCH), &m_braceMismatches);
		SaveRegBool(hKey, _T(ID_RK_MARGINMENU), &m_menuInMargin);
		SaveRegBool(hKey, _T(ID_RK_ENABLEPASTEMENU), &m_EnablePasteMenu);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_SORT_DEF_LIST), &m_sortDefPickList);

		SaveRegString(hKey, _T(ID_RK_EXTHDR), m_hdrExts);
		SaveRegString(hKey, _T(ID_RK_EXTSRC), m_srcExts);
		SaveRegBool(hKey, _T(ID_RK_EXTSRC_ISUPDATED), &m_srcExts_IsUpdated);
		SaveRegString(hKey, _T(ID_RK_EXTRES), m_resExts);
		SaveRegString(hKey, _T(ID_RK_EXTIDL), m_idlExts);
#if !defined(RAD_STUDIO)
		SaveRegString(hKey, _T(ID_RK_EXTBIN), m_binExts);
		SaveRegString(hKey, _T(ID_RK_EXTJAV), m_javExts);
#endif // !RAD_STUDIO
		SaveRegString(hKey, _T(ID_RK_EXTS_TO_IGNORE), mExtensionsToIgnore);
#if !defined(RAD_STUDIO)
		SaveRegString(hKey, _T(ID_RK_HTML_EXTS), m_htmlExts);
		SaveRegString(hKey, _T(ID_RK_VB_EXTS), m_vbExts);
		SaveRegString(hKey, _T("ExtPHP"), m_phpExts);
		SaveRegString(hKey, _T("ExtJS"), m_jsExts);
		SaveRegString(hKey, _T(ID_RK_CS_EXTS), m_csExts);
		SaveRegString(hKey, _T("ExtPerl"), m_perlExts);
		SaveRegString(hKey, _T("ExtVbs"), m_vbsExts);
		SaveRegString(hKey, _T(ID_RK_ASP_EXTS), m_aspExts);
		SaveRegString(hKey, _T(ID_RK_XML_EXTS), m_xmlExts);
		SaveRegString(hKey, _T("ExtPlainText"), m_plainTextExts);
		SaveRegString(hKey, _T("ExtXaml"), m_xamlExts);
		SaveRegString(hKey, _T(ID_RK_SHADER_EXTS), m_ShaderExts);
		SaveRegBool(hKey, _T("UseDefaultIntellisense"), &m_bUseDefaultIntellisense);
		SaveRegString(hKey, _T(ID_RK_SurroundWithKeys), mSurroundWithKeys);

		SaveRegBool(hKey, _T("DisplayFilteringToolbar"), &m_bDisplayFilteringToolbar);
		SaveRegBool(hKey, _T("AllowShorthand"), &m_bAllowShorthand);
		SaveRegBool(hKey, _T("AllowAcronyms"), &m_bAllowAcronyms);
#endif // !RAD_STUDIO

		SaveRegString(hKey, _T(ID_RK_PLATFORM), m_platformIncludeKey);
		SaveRegDword(hKey, _T(ID_RK_LOCALECHANGE), &m_doLocaleChange);
		SaveRegBool(hKey, _T(ID_RK_MACROPARSE1), &m_limitMacroParseLocal);
		SaveRegBool(hKey, _T(ID_RK_MACROPARSE2), &m_limitMacroParseSys);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_VAVIEWGOTODEF), &mVaviewGotoDef);
		SaveRegBool(hKey, _T(ID_RK_HIGHLIGHTREFERENCESBYDEFAULT), &mHighlightFindReferencesByDefault);
		SaveRegDword(hKey, _T(ID_RK_BRACEMATCHSTYLE), &mBraceAutoMatchStyle);
		// Don't expose by writing this out (with new name in 10.4)
		// SaveRegDword(hKey, _T(ID_RK_SUGGESTIONSELECTSTYLE), &mSuggestionSelectionStyle);
		SaveRegBool(hKey, _T(ID_RK_TOOLTIPSINFINDRESULTS), &mUseTooltipsInFindReferencesResults);
		SaveRegBool(hKey, _T(ID_RK_DISLPAYREFACTORINGBUTTON), &mDisplayRefactoringButton);
		//		SaveRegBool(hKey, _T(ID_RK_AUTODISLPAYREFACTORINGBUTTON), &mAutoDisplayRefactoringButton); // removed
		SaveRegBool(hKey, _T(ID_RK_INCLUDEDEFAULTPARAMETERVALUES), &mIncludeDefaultParameterValues);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EXTENSIONLESSFILEISHEADER), &mExtensionlessFileIsHeader);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_MARKCURRENTLINE), &mMarkCurrentLine);
		SaveRegDword(hKey, _T(ID_RK_CURRENTLINEMARKERSTYLE), &mCurrentLineBorderStyle);
		SaveRegDword(hKey, _T(ID_RK_CURRENTLINEVISUALSTYLE), &mCurrentLineVisualStyle);
		SaveRegBool(hKey, _T(ID_RK_CHECKFORLATESTVERSION), &mCheckForLatestVersion);
		SaveRegBool(hKey, _T(ID_RK_CHECKFORLATESTBETA), &mCheckForLatestBetaVersion);
		SaveRegBool(hKey, _T(ID_RK_RECOMMENDAFTEREXPIRES), &mRecommendAfterExpires);
		SaveRegBool(hKey, _T(ID_RK_DONTINSERTSPACEAFTERCOMMENT), &mDontInsertSpaceAfterComment);
		SaveRegBool(hKey, _T(ID_RK_LINENUMBERSINFINDREFSRESULTS), &mLineNumbersInFindRefsResults);
		SaveRegBool(hKey, _T(ID_RK_ENABLESTATUSBARMESSAGES), &mEnableStatusBarMessages);
		SaveRegBool(hKey, _T(ID_RK_CLOSESUGGESTIONLISTONEXACTMATCH), &mCloseSuggestionListOnExactMatch);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_NOISYEXECFAILURENOTIFICATIONS), &mNoisyExecCommandFailureNotifications);
		SaveRegBool(hKey, _T(ID_RK_DISPLAY_COMMENT_REFERENCES), &mDisplayCommentAndStringReferences);
		SaveRegBool(hKey, _T(ID_RK_ENC_FIELD_PUBLIC_ACCESSORS), &mEncFieldPublicAccessors);
		SaveRegDword(hKey, _T(ID_RK_ENC_FIELD_MOVE_VARIABLE), (DWORD*)&mEncFieldMoveVariable);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_FIND_INHERITED_REFERENCES), &mDisplayWiderScopeReferences);
		SaveRegBool(hKey, _T(ID_RK_FIND_REFERENCES_FROM_ALL_PROJECTS), &mDisplayReferencesFromAllProjects);
		SaveRegBool(hKey, _T(ID_RK_RENAME_COMMENT_REFERENCES), &mRenameCommentAndStringReferences);
		SaveRegBool(hKey, _T(ID_RK_RENAME_WIDER_SCOPE_REFERENCES), &mRenameWiderScopeReferences);
		SaveRegBool(hKey, _T(ID_RK_INCLUDEREFACTORINGINLISTBOXES), &mIncludeRefactoringInListboxes);
		SaveRegBool(hKey, _T(ID_RK_AUTOSIZE_LISTBOX), &mAutoSizeListBox);
		SaveRegBool(hKey, _T(ID_RK_PARSE_FILES_IN_DIR_IF_EMPTY), &mParseFilesInDirectoryIfEmptySolution);
		SaveRegBool(hKey, _T(ID_RK_PARSE_FILES_IN_DIR_IF_EXTERNAL), &mParseFilesInDirectoryIfExternalFile);
		SaveRegDword(hKey, _T(ID_RK_LISTBOX_FLAGS), &mListboxFlags);
		SaveRegBool(hKey, _T(ID_RK_MARK_FIND_TEXT), &mMarkFindText);
		SaveRegBool(hKey, _T(ID_RK_PROJECT_NODE_IN_RESULTS), &mIncludeProjectNodeInReferenceResults);
		SaveRegBool(hKey, _T(ID_RK_PROJECT_NODE_IN_REN_RESULTS), &mIncludeProjectNodeInRenameResults);
		SaveRegBool(hKey, _T(ID_RK_BACKGROUND_TOMATO), &mUseTomatoBackground);
		SaveRegBool(hKey, _T(ID_RK_MIF_REGIONS), &mMethodInFile_ShowRegions);
		SaveRegBool(hKey, _T(ID_RK_MIF_SCOPE), &mMethodInFile_ShowScope);
		SaveRegBool(hKey, _T(ID_RK_MIF_DEFINES), &mMethodInFile_ShowDefines);
		SaveRegBool(hKey, _T(ID_RK_MIF_PROPERTIES), &mMethodInFile_ShowProperties);
		SaveRegBool(hKey, _T(ID_RK_MIF_MEMBERS), &mMethodInFile_ShowMembers);
		SaveRegBool(hKey, _T(ID_RK_MIF_EVENTS), &mMethodInFile_ShowEvents);
		SaveRegBool(hKey, _T(ID_RK_SuppressListboxes), &mSuppressAllListboxes);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_FsisUseEditorSelection), &mFindSymbolInSolutionUsesEditorSelection);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_SELECT_IMPLEMENTATION), &mSelectImplementation);
		SaveRegBool(hKey, _T(ID_RK_AUTO_HIGHLIGHT_REFS), &mAutoHighlightRefs);
		SaveRegBool(hKey, _T(ID_RK_AUTO_HIGHLIGHT_REFS_THREADED), &mUseAutoHighlightRefsThread);
		SaveRegBool(hKey, _T(ID_RK_PARAM_IN_MIF), &mParamsInMethodsInFileList);
		SaveRegBool(hKey, _T(ID_RK_OPTIMIZE_REMOTE_DESKTOP), &mOptimizeRemoteDesktop);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_ReparseIfNeeded), (DWORD*)&mReparseIfNeeded);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_EnableSortLinesPrompt), &mEnableSortLinesPrompt);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_UsePpl), &mUsePpl);
		SaveRegBool(hKey, _T(ID_RK_DisplayHashtagXrefs), &mDisplayHashtagXrefs);
		SaveRegBool(hKey, _T(ID_RK_ClearNonSourceFileMatches), &mClearNonSourceFileMatches);
		SaveRegBool(hKey, _T(ID_RK_OfisTooltips), &mOfisTooltips);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludeSolution), &mOfisIncludeSolution);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludePrivateSystem), &mOfisIncludePrivateSystem);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludeSystem), &mOfisIncludeSystem);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludeExternal), &mOfisIncludeExternal);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludeWindowList), &mOfisIncludeWindowList);
		SaveRegBool(hKey, _T(ID_RK_FsisTooltips), &mFsisTooltips);
		SaveRegBool(hKey, _T(ID_RK_BrowseMembersTooltips), &mBrowseMembersTooltips);
		SaveRegBool(hKey, _T(ID_RK_FsisExtraColumns), &mFsisExtraColumns);
#if !defined(RAD_STUDIO)
		if (sOkToSaveVsMarkerValue)
			SaveRegBool(hKey, _T(ID_RK_USE_MARKER_API), &mUseMarkerApi);

		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			SaveRegDword(hKey, _T(ID_RK_MOUSE_WHEEL_CMDS), &mMouseWheelCmds);

		SaveRegDword(hKey, _T(ID_RK_MOUSE_CLICK_CMDS), &mMouseClickCmds);
		if (RegValueExists(hKey, _T(ID_RK_CTRL_CLICK_GOTO)))
		{
			// backward compatibility
			DWORD dw = mMouseClickCmds.get(0);
			if (dw > 2) // old setting was 0, 1, or 2
				dw = 0;
			SaveRegDword(hKey, _T(ID_RK_CTRL_CLICK_GOTO), &dw);
		}

		SaveRegBool(hKey, _T(ID_RK_FIX_SMART_PTR), &m_fixSmartPtrOp);
		SaveRegBool(hKey, _T(ID_RK_SUGGESTIONS_IN_MANAGED_CODE), &m_UseVASuggestionsInManagedCode);
		SaveRegBool(hKey, _T(ID_RK_EXTEND_COMMENT_ON_NEWLINE), &mExtendCommentOnNewline);
#ifdef AVR_STUDIO
		SaveRegBool(hKey, _T(ID_RK_MEMBERS_COMPLETE_ON_ANY), &mMembersCompleteOnAny);
#else
		if (!gShellAttr || !gShellAttr->IsDevenv11OrHigher())
			SaveRegBool(hKey, _T(ID_RK_MEMBERS_COMPLETE_ON_ANY), &mMembersCompleteOnAny);
#endif
		SaveRegDword(hKey, _T(ID_RK_USE_THEMED_COMPLETIONBOX_SELECTION),
		             (DWORD*)&m_alwaysUseThemedCompletionBoxSelection);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_MAX_SCREEN_REPARSE), (DWORD*)&mMaxScreenReparse);
		SaveRegDword(hKey, _T(ID_RK_MAX_FILE_SIZE), (DWORD*)&mLargeFileSizeThreshold);
#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T(ID_RK_ADDINCLUDE_STYLE), (DWORD*)&mAddIncludeStyle);
		SaveRegDword(hKey, _T(ID_RK_LISTBOX_OVERWRITE_BEHAVIOR), (DWORD*)&mListboxOverwriteBehavior);
		SaveRegDword(hKey, _T(ID_RK_Smart_Ptr_Suggest_Mode), (DWORD*)&mSmartPtrSuggestModes);
		SaveRegBool(hKey, _T(ID_RK_MIF_NAME_FILTER), &mMethodsInFileNameFilter);
		SaveRegBool(hKey, _T(ID_RK_CPP_OVERRIDE_KEYWORD), &mUseCppOverrideKeyword);
		SaveRegBool(hKey, _T(ID_RK_CPP_VIRTUAL_KEYWORD), &mUseCppVirtualKeyword);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_DIALOGS_STICK_TO_MONITOR), &mDialogsStickToMonitor);
		SaveRegBool(hKey, _T(ID_RK_DIALOGS_FIT_INTO_SCREEN), &mDialogsFitIntoScreen);
		SaveRegBool(hKey, _T(ID_RK_UNC_PATHS), &mAllowUncPaths);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_DIRTY_FIND_REFS_NAV), &mAlternateDirtyRefsNavBehavior);
		SaveRegBool(hKey, _T(ID_RK_PROJECT_INFO_CACHE), &mCacheProjectInfo);
		SaveRegBool(hKey, _T(ID_RK_TRACK_CARET_VISIBILITY), &mTrackCaretVisibility);
		SaveRegString(hKey, _T(ID_RK_FUNCTION_CALL_PAREN_STRING), mFunctionCallParens);
		SaveRegBool(hKey, _T(ID_RK_COLOR_BUILD_OUTPUT), &mColorBuildOutput);
		SaveRegDword(hKey, _T(ID_RK_ListBoxHeightInItems), (DWORD*)&mListBoxHeightInItems);
		SaveRegDword(hKey, _T(ID_RK_InitialSuggestionBoxHeightInItems), (DWORD*)&mInitialSuggestionBoxHeightInItems);
		SaveRegBool(hKey, _T(ID_RK_ResizeSuggestionListOnUpDown), &mResizeSuggestionListOnUpDown);
		SaveRegBool(hKey, _T(ID_RK_DismissSuggestionListOnUpDown), &mDismissSuggestionListOnUpDown);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EnableFilterStartEndTokens), &mEnableFilterStartEndTokens);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_UseGotoRelatedForGoButton), &mUseGotoRelatedForGoButton);
		SaveRegDword(hKey, _T(ID_RK_LinesBetweenMethods), &mLinesBetweenMethods);
		SaveRegBool(hKey, _T(ID_RK_DisableFindSimilarLocation), &mFindSimilarLocation);
		SaveRegBool(hKey, _T(ID_RK_HashtagsGroupByFile), &mHashtagsGroupByFile);
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			SaveRegBool(hKey, _T(ID_RK_EditUserInputInEditor), &mEditUserInputFieldsInEditor);
			// saved by the code inspection package see also:#codeInspectionSetting
			// SaveRegBool(, _T(ID_RK_EnableCodeInspection), &mCodeInspection);
		}
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_HashtagsMinimumTagLength), &mMinimumHashtagLength);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_InsertOpenBraceOnNewLine), &mInsertOpenBraceOnNewLine);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_HashtagsIgnoreHexAlphaTags), &mIgnoreHexAlphaHashtags);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_SurroundWithSnippetOnChar), &mEnableSurroundWithSnippetOnChar);
		SaveRegBool(hKey, _T(ID_RK_SurroundWithSnippetOnCharIgnoreWhitespace),
		            &mSurroundWithSnippetOnCharIgnoreWhitespace);
		SaveRegBool(hKey, _T(ID_RK_EdMouseButtonHooks), &mEnableEdMouseButtonHooks);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_HashtagsAllowHyphens), &mHashtagsAllowHypens);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_AddMissingDefaultSwitchCase), &mAddMissingDefaultSwitchCase);
		SaveRegBool(hKey, _T(ID_RK_MoveTemplatesToSourceInTwoSteps), &mTemplateMoveToSourceInTwoSteps);
		SaveRegBool(hKey, _T(ID_RK_AlwaysDisplayGlobalMenuInHashtagMenu), &mHashTagsMenuDisplayGlobalAlways);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_SparseSystemIncludeLoad), &mSparseSysLoad);
		SaveRegBool(hKey, _T(ID_RK_VerifyDbOnLoad), &mVerifyDbOnLoad);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_NavBarContextDisplayScopeSingleLevel), &mNavBarContext_DisplaySingleScope);
		SaveRegBool(hKey, _T(ID_RK_UseOldUncommentBehavior), &mOldUncommentBehavior);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_SelectRecentItemsInNav), &mSelectRecentItemsInNavigationDialogs);

#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T(ID_RK_SmartSelectPeekDuration), &mSmartSelectPeekDuration);
		SaveRegBool(hKey, _T(ID_RK_SmartSelectEnableGranularStart), &mSmartSelectEnableGranularStart);
		SaveRegBool(hKey, _T(ID_RK_SmartSelectEnableWordStart), &mSmartSelectEnableWordStart);
		SaveRegBool(hKey, _T(ID_RK_SmartSelectSplitWordByCase), &mSmartSelectSplitWordByCase);
		SaveRegBool(hKey, _T(ID_RK_SmartSelectSplitWordByUnderscore), &mSmartSelectSplitWordByUnderscore);

		const CString tmp2(mDefaultAddIncludeDelimiter);
		SaveRegString(hKey, _T(ID_RK_DEFAULTINCLUDEDELIMITER), tmp2);

		SaveRegDword(hKey, _T("IVMOptions"), &mImplementVirtualMethodsOptions);
		SaveRegDword(hKey, _T(ID_RK_GotoOverloadResolutionMode), (DWORD*)&mGotoOverloadResolutionMode);
		SaveRegDword(hKey, _T(ID_RK_GotoRelatedOverloadResolutionMode), (DWORD*)&mGotoRelatedOverloadResolutionMode);
		SaveRegString(hKey, _T(ID_RK_CreationTemplateFunctionNames), mCreationTemplateFunctions);
		SaveRegBool(hKey, _T(ID_RK_GotoRelatedParameterTrimming), &mGotoRelatedParameterTrimming);
		SaveRegBool(hKey, _T(ID_RK_EnableFindRefsFlagCreation), &mFindRefsFlagCreation);
		SaveRegBool(hKey, _T(ID_RK_IncludeDirectiveCompletionLists), &mIncludeDirectiveCompletionLists);
		SaveRegBool(hKey, _T(ID_RK_ForceUseOfOldVcProjectApi), &mForceUseOldVcProjectApi);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EnhanceMacroParsing), &mEnhanceMacroParsing);
		SaveRegBool(hKey, _T(ID_RK_ForceProgramFilesDirSystem), &mForceProgramFilesDirsSystem);
		SaveRegBool(hKey, _T(ID_RK_OfisAugmentSolution), &mOfisAugmentSolution);
		SaveRegBool(hKey, _T(ID_RK_AugmentHiddenExtensions), &mAugmentHiddenExtensions);
		SaveRegBool(hKey, _T(ID_RK_OfisIncludeHiddenExtensions), &mOfisIncludeHiddenExtensions);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_ForceVs2017ProjectSync), &mForceVs2017ProjectSync);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EnableMixedDpiScalingWorkaround), &mEnableMixedDpiScalingWorkaround);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_ForceVcPkgInclude), &mForceVcPkgInclude);
		SaveRegBool(hKey, _T(ID_RK_EnableVcProjectSync), &mEnableVcProjectSync);
		SaveRegDword(hKey, _T(ID_RK_WarnOnLoadOfPathWithReversedSlashes),
		             (DWORD*)&mWarnOnLoadOfPathWithReversedSlashes);
		SaveRegDword(hKey, _T(ID_RK_JavaDocStyle), &mJavaDocStyle);
		SaveRegBool(hKey, _T(ID_RK_AvoidDuplicatingTypeWithConvert), &mUseAutoWithConvertRefactor);
		SaveRegBool(hKey, _T(ID_RK_ALLOW_SNIPPETS_IN_UNREAL_MARKUP), &mAllowSnippetsInUnrealMarkup);
		SaveRegBool(hKey, _T(ID_RK_ALWAYS_DISPLAY_UNREAL_SYMBOLS_IN_ITALICS), &mAlwaysDisplayUnrealSymbolsInItalics);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_INDEX_PLUGINS), &mIndexPlugins);
		SaveRegBool(hKey, _T(ID_RK_INDEX_GENERATED_CODE), &mIndexGeneratedCode);
		SaveRegBool(hKey, _T(ID_RK_DISABLE_UE_AUTOFORMAT_ON_PASTE), &mDisableUeAutoforatOnPaste);
		SaveRegBool(hKey, _T(ID_RK_USE_GIT_ROOT_FOR_AUGMENT_SOLUTION), &mUseGitRootForAugmentSolution);
#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T(ID_RK_ConvertToPointerType), &mConvertToPointerType);
#endif // !RAD_STUDIO
		SaveRegDword(hKey, _T(ID_RK_FileLoadCodePage), &mFileLoadCodePageBehavior);
#if !defined(RAD_STUDIO)
		SaveRegString(hKey, _T(ID_RK_ConvertCustomPointerName), mConvertCustomPtrName);
		SaveRegString(hKey, _T(ID_RK_ConvertCustomMakeName), mConvertCustomMakeName);
		SaveRegDword(hKey, _T(ID_RK_UnrealEngineAutoDetect), &mUnrealEngineAutoDetect);
		SaveRegBool(hKey, _T(ID_RK_OptionsHelp), &mOptionsHelp);
		SaveRegBool(hKey, _T(ID_RK_ColorInsideUnrealMarkup), &mColorInsideUnrealMarkup);
		SaveRegBool(hKey, _T(ID_RK_EnableShaderSupport), &mEnableShaderSupport);
		SaveRegBool(hKey, _T(ID_RK_EnableCudaSupport), &mEnableCudaSupport);
		SaveRegBool(hKey, _T(ID_RK_AddIncludeUnrealUseQuotation), &mAddIncludeUnrealUseQuotation);
		SaveRegBool(hKey, _T(ID_RK_UseSlashForIncludePaths), &mUseSlashForIncludePaths);
		SaveRegBool(hKey, _T(ID_RK_KeepAutoHighlightOnWS), &mKeepAutoHighlightOnWS);

#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_FilterGeneratedSourceFiles), &mFilterGeneratedSourceFiles);
		SaveRegString(hKey, ID_RK_OfisPersistentFilter, mOfisPersistentFilter);
		SaveRegBool(hKey, _T(ID_RK_OfisDisplayPersistentFilter), &mOfisDisplayPersistentFilter);
		SaveRegBool(hKey, _T(ID_RK_OfisApplyPersistentFilter), &mOfisApplyPersistentFilter);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_AddIncludeSkipFirstFileLevelComment), &mAddIncludeSkipFirstFileLevelComment);
#endif // !RAD_STUDIO
		SaveRegString(hKey, CA2W(ID_RK_ThirdPartyRegex), mThirdPartyRegex);
#if !defined(RAD_STUDIO)
		SaveRegDword(hKey, _T(ID_RK_InsertPathMode), &mInsertPathMode);
		SaveRegBool(hKey, _T(ID_RK_InsertPathFwdSlash), &mInsertPathFwdSlash);
		SaveRegBool(hKey, _T(ID_RK_ListboxCompletionRequiresSelection), &mListboxCompletionRequiresSelection);
		SaveRegBool(hKey, _T(ID_RK_AllowSuggestAfterTab), &mAllowSuggestAfterTab);
		SaveRegBool(hKey, _T(ID_RK_WebEditorPmaFail), &mWebEditorPmaFail);
		SaveRegBool(hKey, _T(ID_RK_CloseHashtagToolwindowOnGoto), &mCloseHashtagToolWindowOnGoto);
		SaveRegDword(hKey, _T(ID_RK_RestrictFilesOpenedDuringRefactor), &mRestrictFilesOpenedDuringRefactor);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_EnableGotoJumpsToImpl), &mEnableJumpToImpl);
		SaveRegBool(hKey, _T(ID_RK_EnableGotoFilterWIthOverloads), &mEnableFilterWithOverloads);
#if !defined(RAD_STUDIO)
		SaveRegBool(hKey, _T(ID_RK_EnhanceVAOutlineMacroSupport), &mEnhanceVAOutlineMacroSupport);
#endif // !RAD_STUDIO
		SaveRegBool(hKey, _T(ID_RK_RestrictGotoMemberToProject), &mRestrictGotoMemberToProject);
		SaveRegDword(hKey, _T(ID_RK_RebuildSolutionMinimumModCnt), &mRebuildSolutionMinimumModPercent);
		SaveRegBool(hKey, _T(ID_RK_DoOldStyleMainMenu), &mDoOldStyleMainMenu);
		SaveRegDword(hKey, _T(ID_RK_AddIncludeSortedMinProbability), &mAddIncludeSortedMinProbability);
		//SaveRegDword(hKey, _T(ID_RK_FirstRunDialogStatus), &mFirstRunDialogStatus); // [case: 149451] todo: disabled to backout functionality

#if !defined(RAD_STUDIO)
		auto mModifyExprFlagsTmp = mModifyExprFlags & ModExpr_SaveMask;
		SaveRegDword(hKey, _T(ID_RK_ModifyExpressionFlags), &mModifyExprFlagsTmp);
		SaveRegBool(hKey, _T(ID_RK_RESPECT_VS_CODE_EXCLUDED_FILES), &mRespectVsCodeExcludedFiles);

		if (gShellAttr->IsDevenv10OrHigher())
		{
			SaveRegDword(hKey, _T(ID_RK_WatermarkProperties), &mWatermarkProps);
		}

		SaveRegString(hKey, _T(ID_RK_MINIHELP_INFO), m_minihelpInfo);
#endif // !RAD_STUDIO

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		SaveRegDword(hKey, _T(ID_RK_ENC_FIELD_TYPE), (DWORD*)&mEncFieldTypeCppB);
		SaveRegBool(hKey, _T(ID_RK_ENC_FIELD_READ_FIELD), &mEnableEncFieldReadFieldCppB);
		SaveRegBool(hKey, _T(ID_RK_ENC_FIELD_READ_VIRTUAL), &mEnableEncFieldReadVirtualCppB);
		SaveRegBool(hKey, _T(ID_RK_ENC_FIELD_WRITE_FIELD), &mEnableEncFieldWriteFieldCppB);
		SaveRegBool(hKey, _T(ID_RK_ENC_FIELD_WRITE_VIRTUAL), &mEnableEncFieldWriteVirtualCppB);
		SaveRegDword(hKey, _T(ID_RK_ENC_FIELD_MOVE_PROPERTY), (DWORD*)&mEncFieldMovePropertyCppB);
#endif
	}
	else
	{
		ASSERT(FALSE);
		vLogUnfiltered("CS::~CS open HKCU ID_RK_APP FAILED 0x%08lx", err);
	}
	RegCloseKey(hKey);
#endif // !VA_CPPUNIT
}

void CSettings::SaveRegFirstRunDialogStatus()
{
#if defined(VA_CPPUNIT)
	// do not commit to registry during unit tests
	return;
#else
	// save option to reg
	HKEY hKey;
	DWORD tmp = 0;
	LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_WRITE, &hKey);
	if (ERROR_SUCCESS != err)
	{
		// create base tree
		err = RegCreateKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, (LPSTR) "", REG_OPTION_NON_VOLATILE,
		                     KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, &tmp);
		err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_WRITE, &hKey);
	}

	if (ERROR_SUCCESS == err)
	{
		SaveRegDword(hKey, _T(ID_RK_FirstRunDialogStatus), &mFirstRunDialogStatus);
	}
	else
	{
		ASSERT(FALSE);
		vLogUnfiltered("CSettings::SaveRegFirstRunDialogStatus open HKCU ID_RK_APP FAILED 0x%08lx", err);
	}
	RegCloseKey(hKey);
#endif // !VA_CPPUNIT
}

bool CSettings::IsFirstRunAfterInstall()
{
	// we need some hack how to check if this is the first time install or if user has already
	// VA and this is just an update; let's check registry for key ID_RK_BACKGROUND_TOMATO
	// "EnableUiTomato" since this key is not present in registry on first install at the time
	// of calling this function
	HKEY hKey;
	if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE, &hKey))
	{
		// root key does not exist, something is badly wrong; this should never happen in normal scenario
		return false;
	}

	if (!hKey)
	{
		// no hKey obtained, can't continue; also should not ever happen
		return false;
	}

	DWORD dataSize = sizeof(DWORD);
	if (ERROR_SUCCESS == RegQueryValueEx(hKey, ID_RK_BACKGROUND_TOMATO, 0, 0, nullptr, &dataSize))
	{
		// ID_RK_BACKGROUND_TOMATO is already written, so we can assume that this is not the first run of VA
		return false;
	}

	// if all is fine until now, we can assume that this is a first run
	return true;
}

void CSettings::ReadRegBool(HKEY hKey, LPCTSTR name, bool* dest, bool defVal /* = true */)
{
	DWORD dataSize = sizeof(bool);
	bool bData = defVal;

	if (hKey)
	{
		LSTATUS err = RegQueryValueEx(hKey, name, 0, 0, (LPBYTE)&bData, &dataSize);
#ifdef _DEBUG
		if (ERROR_SUCCESS != err)
			vCatLog("Settings", "CS::ReadRB query %s FAILED 0x%08lx", name, err);
#endif // _DEBUG
		std::ignore = err;
	}

	*dest = bData;
}

void CSettings::SaveRegBool(HKEY hKey, LPCTSTR name, bool* origin)
{
	LSTATUS err = RegSetValueEx(hKey, name, 0, REG_BINARY, (LPBYTE)origin, sizeof(bool));
	if (ERROR_SUCCESS != err)
	{
		vLogUnfiltered("CS::SaveRB %s FAILED 0x%08lx", name, err);
		ASSERT(FALSE);
	}
}

bool CSettings::RegValueExists(HKEY hKey, LPCTSTR name) const
{
	if (!hKey)
		return false;

	DWORD dataSize = sizeof(DWORD);
	LSTATUS err = RegQueryValueEx(hKey, name, 0, 0, nullptr, &dataSize);
	return (err == ERROR_SUCCESS);
}

void CSettings::ReadRegDword(HKEY hKey, LPCTSTR name, DWORD* dest, DWORD defVal /* = 0 */)
{
	DWORD dataSize = sizeof(DWORD);
	DWORD data = defVal;

	if (hKey)
	{
		LSTATUS err = RegQueryValueEx(hKey, name, 0, 0, (LPBYTE)&data, &dataSize);
#ifdef _DEBUG
		if (ERROR_SUCCESS != err)
			vCatLog("Settings", "CS::ReadRD query %s FAILED 0x%08lx", name, err);
#endif // _DEBUG
		std::ignore = err;
	}

	*dest = data;
}

void CSettings::ReadRegString(HKEY hKey, LPCTSTR name, LPTSTR dest, LPCTSTR defVal, DWORD len)
{
	LSTATUS err = ERROR_PATH_NOT_FOUND;
	if (hKey)
		err = RegQueryValueEx(hKey, name, 0, 0, (LPBYTE)dest, &len);
	if (ERROR_SUCCESS != err || dest[0] == '\0')
	{
#ifdef _DEBUG
		if (hKey)
			vCatLog("Settings", "CS::ReadRS query %s FAILED 0x%08lx", name, err);
#endif // _DEBUG

		_tcscpy(dest, defVal);
	}
}

void CSettings::ReadRegString(HKEY hKey, LPCWSTR name, LPWSTR dest, LPCWSTR defVal, DWORD len)
{
	LSTATUS err = ERROR_PATH_NOT_FOUND;
	if (hKey)
		err = RegQueryValueExW(hKey, name, 0, 0, (LPBYTE)dest, &len);
	if (ERROR_SUCCESS != err || dest[0] == L'\0')
	{
#ifdef _DEBUG
		if (hKey)
			vCatLog("Settings", "CS::ReadRS query %s FAILED 0x%08lx", (LPCTSTR)CString(name), err);
#endif // _DEBUG

		wcscpy(dest, defVal);
	}
}

void CSettings::SaveRegString(HKEY hKey, LPCTSTR name, LPCTSTR value)
{
	LSTATUS err = RegSetValueEx(hKey, name, 0, REG_SZ, (LPBYTE)value, strlen_u(value) + 1);
	if (ERROR_SUCCESS != err)
	{
		vLogUnfiltered("CS::SaveRS %s FAILED 0x%08lx", name, err);
		ASSERT(FALSE);
	}
}

void CSettings::SaveRegString(HKEY hKey, LPCWSTR name, LPCWSTR value)
{
	LSTATUS err = RegSetValueExW(hKey, name, 0, REG_SZ, (LPBYTE)value, ((wcslen_u(value) + 1) * sizeof(WCHAR)));
	if (ERROR_SUCCESS != err)
	{
		vLogUnfiltered("CS::SaveRS %s FAILED 0x%08lx", (LPCTSTR)CString(name), err);
		ASSERT(FALSE);
	}
}

void CSettings::SaveRegDword(HKEY hKey, LPCTSTR name, DWORD* origin)
{
	LSTATUS err = RegSetValueEx(hKey, name, 0, REG_DWORD, (LPBYTE)origin, sizeof(DWORD));
	if (ERROR_SUCCESS != err)
	{
		vLogUnfiltered("CS::SaveRD %s FAILED 0x%08lx", name, err);
		ASSERT(FALSE);
	}
}

void CSettings::ReadRegColors()
{
	HKEY hKey;
	vCatLog("Settings", "RRC1");
	LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP + "\\Format"), 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS != err)
	{ // vapad, copies VA's colors
		vCatLog("Settings", "RRC2");
		err = RegOpenKeyEx(HKEY_CURRENT_USER, (WTString(gShellAttr->GetOldVaAppKeyName()) + "\\Format").c_str(), 0,
		                   KEY_QUERY_VALUE, &hKey);
	}
	vCatLog("Settings", "RRC3");
	if (ERROR_SUCCESS == err)
	{
		const int len = sizeof(EditColorStr);
		for (int idx = 0; idx < C_NULL; idx++)
		{
			vCatLog("Settings", "RRC4");
			DWORD dataSize = len - COLOR_ELEMENTNAME_LEN;
			if (!m_colors[idx].m_elementName[0])
				continue;
			err = RegQueryValueEx(hKey, m_colors[idx].m_elementName, 0, 0, (LPBYTE)&m_colors[idx].c_fg, &dataSize);
			if (ERROR_SUCCESS != err)
			{
				vCatLog("Settings", "CS::ReadRC query %d FAILED 0x%08lx", idx, err);
			}
		}
		vCatLog("Settings", "RRC5");
		RegCloseKey(hKey);
	}
	else
	{
		vCatLog("Settings", "RRC6");
		// migrate color settings from msdev
		MigrateDevColor(&m_colors[0]);
	}
}

void CSettings::SaveRegColors()
{
	HKEY hKey;
	DWORD dataSize;
	const DWORD structSize = sizeof(EditColorStr) - COLOR_ELEMENTNAME_LEN;
	LSTATUS err = RegCreateKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP + "\\Format"), 0, const_cast<char*>(""),
	                             REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, &dataSize);
	if (ERROR_SUCCESS == err)
	{
		for (int idx = 0; idx < C_NULL; idx++)
		{
			if (!m_colors[idx].m_elementName[0])
				continue;
			dataSize = structSize;
			err =
			    RegSetValueEx(hKey, m_colors[idx].m_elementName, 0, REG_BINARY, (LPBYTE)&m_colors[idx].c_fg, dataSize);
			if (ERROR_SUCCESS != err)
				vLogUnfiltered("CS::SaveRC set %d FAILED 0x%08lx", idx, err);
		}
	}
	else
	{
		vLogUnfiltered("CS::SaveRC open HKCU ID_RK_FORMAT FAILED 0x%08lx", err);
	}
	RegCloseKey(hKey);
}

void InitializeColors(EditColorStr* pColors)
{
	// default color values	  0X00BBGGRR
	const int len = sizeof(EditColorStr);

	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
	{
		// dev11 has a different default class and macro color due to non-white
		// toolwindows and so that class/type is different than keyword as with
		// default coloring.
		// This is only for the light theme.  Dark theme is ignored.
		g_colors[C_Type].m_vaColors.c_fg = RGB(0x21, 0x6f, 0x85);
		g_colors[C_Macro].m_vaColors.c_fg = RGB(0x6f, 0, 0x8a);
		g_colors[C_EnumMember].m_vaColors.c_fg = RGB(0x2f, 0x4f, 0x4f);
	}

	for (int idx = 0; idx < C_NULL; idx++)
		memcpy((pColors + idx), &g_colors[idx].m_vaColors, len);

	ZeroMemory((pColors + C_NULL), len);
}

void MigrateDevColor(EditColorStr* pColors)
{
	Log("MDC1");
	const WTString msdevKeynamePrefix(gShellAttr->GetFormatSourceWindowKeyName());
	if (msdevKeynamePrefix.IsEmpty())
		return;

	HKEY hKey;

	InitializeColors(pColors); // in case the msdev keys have not been initialized
	Log("MDC2");

	LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, msdevKeynamePrefix.c_str(), 0, KEY_QUERY_VALUE, &hKey);
	Log("MDC3");
	if (ERROR_SUCCESS == err)
	{
		for (int idx = 0; idx < C_NULL; idx++)
		{
			Log("MDC4");
			MigrateDevColorInternal(hKey, idx, pColors);
		}
	}
	else
	{
		Log("MDC5");
		pColors[C_Text].c_fg = ::GetSysColor(COLOR_WINDOWTEXT);
		pColors[C_Text].c_bg = ::GetSysColor(COLOR_WINDOW);
	}

	Log("MDC6");
	RegCloseKey(hKey);
}

void MigrateDevColorInternal(HKEY hDevKey, int idx, EditColorStr* pColors)
{
	DWORD dataSize;
	byte colorData[SIZEOF_COLOR_STRUCT];
	VAColors vaIdx = g_colors[idx].m_vaColorEnum;

	if (COLORSTRUCT::useVAColor == g_colors[vaIdx].Override || COLORSTRUCT::sentinelEntry == g_colors[vaIdx].Override)
		return; // nothing to do for current color

	// read current user color setting
	dataSize = SIZEOF_COLOR_STRUCT;
	LSTATUS err = RegQueryValueEx(hDevKey, g_colors[idx].m_devColors.keyName, 0, 0, colorData, &dataSize);
	if (ERROR_SUCCESS != err)
		return; // just use our defaults

	// check current value before overwriting if useVAColorIfIsMSDevDefault
	// if current value == default value then ok to set otherwise we don't touch it
	if (COLORSTRUCT::useVAColorIfIsMSDevDefault == g_colors[idx].Override)
	{
		// compare regData to devData
		// if different use their color
		if (memcmp(colorData, g_colors[idx].m_devColors.devValue, 4))
		{
			// copy the user's color but don't overwrite -1
			memcpy(&(pColors + vaIdx)->c_fg, colorData, 4);
		}
		if (memcmp(&colorData[4], &g_colors[idx].m_devColors.devValue[4], 4))
		{
			// copy the user's color but don't overwrite -1
			memcpy(&(pColors + vaIdx)->c_bg, &colorData[4], 4);
		}
		// otherwise we're already set by default
	}
	else if (COLORSTRUCT::useCurrentUserColor == g_colors[idx].Override)
	{
		// copy the user's color but don't overwrite -1
		//  values (they control non-display of color wheels)
		memcpy(&(pColors + vaIdx)->c_fg, colorData, 4);
		memcpy(&(pColors + vaIdx)->c_bg, &colorData[4], 4);
	}
	else
	{
		ASSERT(FALSE);
	}
}

void CSettings::Init()
{
	_ASSERTE(gShellAttr);
	WTString defPlatform(gShellAttr->GetDefaultPlatform());
	WTString msdevKeynamePrefix(gShellAttr->GetBaseShellKeyName());
	m_validLicense = TRUE;
	m_enableVA = TRUE;
	m_SymScope[0] = '\0';
	strcpy(m_RegPath, ID_RK_APP);

	//
	// These are VA settings which are persistent between sessions
	//
	HKEY hKey;
	LSTATUS err;
	// use settings from msdev
	FontSize = 10;
	wcscpy(m_srcFontName, L"Courier");

	// InstalledBuildNo/InstalledDir used by bundle installer (for uninstall, to support rollback)
	SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "InstalledBuildNo", itos(VA_VER_BUILD_NUMBER));
#if defined(RAD_STUDIO)
	err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
#else
	// untrustworthy in vs2017+ due to side-by-side VS installation
	SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "InstalledDir", VaDirs::GetDllDir());

	if (!GetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "newKey").GetLength())
	{
		SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "newKey", "Yes");
		// get VA options
		err = RegOpenKeyEx(HKEY_CURRENT_USER, gShellAttr->GetOldVaAppKeyName(), 0,
		                   KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
	}
	else
		err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP), 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);

	// If new install, look to see if VA install on another platform and use its settings
	if (ERROR_SUCCESS != err && gShellAttr->CanTryAlternatePlatformSettings())
	{
		err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(CString(ID_RK_APP_VA) + "\\vc6"), 0,
		                   KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
		if (ERROR_SUCCESS != err)
			err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(CString(ID_RK_APP_VA) + "\\vc5"), 0,
			                   KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
		if (ERROR_SUCCESS != err)
			err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(CString(ID_RK_APP_VA) + "\\evc3"), 0,
			                   KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
	}

	if (ERROR_SUCCESS != err && gShellAttr->CanTryVa4Settings())
	{
		// copy VA4's settings
		err = RegOpenKeyEx(HKEY_CURRENT_USER, _T(ID_RK_APP_VA), 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WRITE, &hKey);
	}
#endif

	InitDefaults(hKey);

	ReadRegString(hKey, _T(ID_RK_PLATFORM), m_platformIncludeKey, defPlatform.c_str(), MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T("ExtensionsToIgnore2"), _T("ExtensionsToIgnore"), mExtensionsToIgnore,
	                                _T(".ctc;.config;"), ".css;.xaml;.vbs;", MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_EXTS_TO_IGNORE), _T("ExtensionsToIgnore2"), mExtensionsToIgnore,
	                                _T(""), ".ctc;.config;.css;.xaml;.vbs;", MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T("ExtHtml2"), _T("ExtHtml"), m_htmlExts, _T(".htm;.html;.shtml;"),
	                                _T(".asp;.aspx;.hta;.asa;.asax;.ascx;.ashx;.asmx;.master;.xaml;.xml;"), MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_HTML_EXTS), _T("ExtHtml2"), m_htmlExts, _T(".htm;.html;.shtml;"),
	                                _T(".asp;.aspx;.hta;.asa;.asax;.ascx;.ashx;.asmx;.master;.xaml;.xml;"), MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_ASP_EXTS), _T("ExtAsp"), m_aspExts,
	                                _T(".asp;.aspx;.hta;.asa;.asax;.ascx;.ashx;.asmx;.master;.vbhtml;.cshtml;"), NULL,
	                                MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T("ExtVB2"), _T("ExtVB"), m_vbExts, _T(".vb;.dsm;.bas;.cls;.frm;.dob;"),
	                                _T(".vbs;.svb;"), MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_VB_EXTS), _T("ExtVB2"), m_vbExts,
	                                _T(".vb;.dsm;.bas;.cls;.frm;.dob;"), _T(".vbs;.svb;"), MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_CS_EXTS), _T("ExtCS"), m_csExts, _T(".cs;.csx;"), NULL, MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_EXTBIN), _T("ExtBinary2"), m_binExts, _T(IDS_EXTBIN), NULL,
	                                MAX_PATH);
	::MigrateAndMergeFileExtensions(hKey, _T(ID_RK_XML_EXTS), _T("ExtXml"), m_xmlExts,
	                                _T(".xml;.resx;.config;.manifest;"), _T(""), MAX_PATH);

	mHideVc6Options = gShellAttr->IsDevenv();
	// manually set this for the new minihelp
	m_contextPrefix = false;
	m_showScopeInContext = true;

	RegCloseKey(hKey);

	// values that really aren't used anymore now that we use their editor for tabs and indent
	TabSize = 4;
	if (gShellAttr->IsMsdev())
	{
		err = RegOpenKeyEx(HKEY_CURRENT_USER, (msdevKeynamePrefix + ID_RK_MSDEV_C).c_str(), 0, KEY_QUERY_VALUE, &hKey);
		if (ERROR_SUCCESS == err)
		{
			ReadRegDword(hKey, _T(ID_RK_TABSIZE), &TabSize, 4);
			RegCloseKey(hKey);
		}
	}

	// selection margin options
	// NOTE: This is a potential source of bugs - the registry is not updated
	//   when the user changes their msdev 'Selection margin' setting.
	//   The value we read in when msdev starts will never be modified.
	err = RegOpenKeyEx(HKEY_CURRENT_USER, (msdevKeynamePrefix + "\\Text Editor").c_str(), 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		// HKCU\Software\Microsoft\DevStudio\6.0\Text Editor
		//	value MarginGlyphs	- DWORD determines whether or not margin is visible
		//	value MarginWidth	- DWORD specifies border width
		ReadRegDword(hKey, _T("MarginGlyphs"), &m_borderWidth, 1);
		if (m_borderWidth)
			ReadRegDword(hKey, _T("MarginWidth"), &m_borderWidth, 20);
		RegCloseKey(hKey);
	}
	else
	{
		// no settings saved for msdev either - use these defaults
		m_borderWidth = 20;
	}

	m_minihelpHeight = 20; // gets overridden on the addin side

	// so that iscsym calls work with high ascii chars
	char* pOldLocale = setlocale(LC_CTYPE, NULL);
	vCatLog("Settings", "CS::CS locale(%s)", pOldLocale);
	// don't need to do locale stuff since we don't have our own editor.
	// will leave code in just in case - user can enable it by setting
	//  ID_RK_LOCALECHANGE to 2.
	if (m_doLocaleChange == NPOS || m_doLocaleChange == 1)
		m_doLocaleChange = 0;
	if (m_doLocaleChange)
	{
		pOldLocale = setlocale(LC_CTYPE, "");
		if (pOldLocale)
		{
			vCatLog("Settings", "CS::CS new locale(%s)", pOldLocale);
			// m_doLocaleChange is set to NPOS only once, after that is either 1 or 0
			if (m_doLocaleChange == NPOS)
			{
				WTString loc(pOldLocale);
				if (loc.FindNoCase("kor") != -1 || loc.FindNoCase("korean") != -1 || loc.FindNoCase("chinese") != -1 ||
				    loc.FindNoCase("chs") != -1 || loc.FindNoCase("cht") != -1 || loc.FindNoCase("japanese") != -1 ||
				    loc.FindNoCase("jpn") != -1)
				{
					LogUnfiltered("WARN: CS::CS setlocale override");
					m_doLocaleChange = 0;
					// d'oh: we're overridding the setting - so restore the locale
					setlocale(LC_CTYPE, "C");
				}
				else
					m_doLocaleChange = 1; // so that we don't do this check every startup
			}
		}
		else
		{
			LogUnfiltered("ERROR: CS::CS setlocale failed");
			m_doLocaleChange = 0;
		}
	}

	//
	// These are VA settings which are not persistent
	//
	vCatLog("Settings", "CSIN1");
	m_incrementalSearch = false;

	//
	// system tooltip info maintained by the addin side
	//
	m_TTbkColor = GetSysColor(COLOR_INFOBK);
	m_TTtxtColor = GetSysColor(COLOR_INFOTEXT);
	WTString srcPath = (const char*)GetRegValue(
	    HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + m_platformIncludeKey).c_str(), ID_RK_GOTOSRCDIRS);
	// get default gotoDirs from platform source directories if not set to Custom
	if (!srcPath.GetLength() && kCustom.Compare(m_platformIncludeKey) && !gShellAttr->IsDevenv10OrHigher())
	{
		vCatLog("Settings", "CSIN3");
		WTString keyStr(gShellAttr->GetPlatformDirectoryKeyName(m_platformIncludeKey));
		const CString tmp(gShellAttr->GetPlatformDirectoryKeyName2(m_platformIncludeKey));
		if (tmp.GetLength())
			keyStr = tmp;
		CStringW dirlst = GetRegValueW(HKEY_CURRENT_USER, keyStr.c_str(), ID_RK_MSDEV_SRC_VALUE);
		dirlst += GetRegValueW(HKEY_LOCAL_MACHINE, keyStr.c_str(), ID_RK_MSDEV_SRC_VALUE);

		dirlst = WTExpandEnvironmentStrings(dirlst);
		dirlst += CStringW(L";") + WTExpandEnvironmentStrings(L";%SOURCE%;");

		TokenW tmpTok(dirlst);
		dirlst.Empty();
		while (tmpTok.more())
		{
			vCatLog("Settings", "CSIN4");
			CStringW dir = tmpTok.read(L";");
			if (dir.IsEmpty())
				continue;
			// remove trailing '\' or '/'
			const int lastChIdx = dir.GetLength() - 1;
			if (dir[lastChIdx] == L'\\')
				dir.SetAt(lastChIdx, L';');
			else if (dir[lastChIdx] == L'/')
				dir.SetAt(lastChIdx, L';');
			if (dir[lastChIdx] != L';')
				dir += ";";
			dir.MakeLower();
			if (!dir.CompareNoCase(L"%source%;"))
				continue;
			if (-1 == dirlst.Find(dir))
				dirlst += dir;
		}

		SetRegValue(HKEY_CURRENT_USER, (WTString(ID_RK_APP) + WTString("\\") + defPlatform).c_str(), ID_RK_GOTOSRCDIRS,
		            dirlst);
	}

	vCatLog("Settings", "CSIN5");
	//
	// Compatibility options
	//
	NoBackspaceAtBOL = 0;

	m_overtype = false;

	vCatLog("Settings", "CSIN6");
	InitializeColors(&m_colors[0]);
	vCatLog("Settings", "CSIN7");

	ReadRegColors(); // now see if there are custom colors set
	CatLog("Settings", "CSIN_7.1");

	m_validLicense = TRUE;

	CatLog("Settings", "CSIN_7.2");
	if (::GetSysColor(COLOR_BACKGROUND) != 0xffffff || m_colors[C_Text].c_bg != 0xffffff)
	{
		Log1("CSIN_7.3");
		if (!GetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "InitColoringOptions").GetLength())
		{
			Log1("CSIN_7.4");
			// default tooltips off
			m_bEnhColorObjectBrowser = FALSE;
			m_bEnhColorTooltips = FALSE;
			m_bEnhColorViews = FALSE;
			m_bEnhColorFindResults = FALSE;
			m_bEnhColorListboxes = FALSE;
			m_bEnhColorWizardBar = FALSE;
			m_bLocalSymbolsInBold = FALSE;
		}
	}

	m_validLicense = TRUE; // Set to true, Will validate later... -Jer
	CatLog("Settings", "CSIN_8");
	CheckForConflicts();
	CatLog("Settings", "CSIN_X");
}

static bool CheckExtStringFormat(CString& setting)
{
	bool changed = false;

	if (setting.IsEmpty())
		return false;

	vCatLog("Settings.CheckExtStringFormat", " %s\n", (LPCTSTR)setting);
	if (setting.FindOneOf(" ,") != -1)
	{
		setting.Replace(" ", ";");
		setting.Replace(",", ";");
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	if (setting[0] == ';' && setting.GetLength() > 1)
	{
		setting = setting.Mid(1);
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	if (setting[0] != '.' && setting[0] != ';')
	{
		setting = "." + setting;
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	if (setting[setting.GetLength() - 1] != ';')
	{
		setting += ";";
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	while (setting.Find(";;") != -1)
	{
		setting.Replace(";;", ";");
		changed = true;
	}

	// for each ';', if foundPos + 1 != '.' && (foundPos != Length - 1) insert '.'
	vCatLog("Settings.CheckExtStringFormat", ".");
	for (int pos = 0; (pos = setting.Find(';', pos + 1)) != -1;)
	{
		if (pos == setting.GetLength() - 1)
			break;

		if (setting[pos + 1] == '.')
			continue;

		const CString tmp(setting.Mid(pos + 1));
		setting = setting.Left(pos + 1);
		setting += ".";
		setting += tmp;
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	while (setting.GetLength() + 1 > MAX_PATH)
	{
		int pos = setting.ReverseFind(';');
		setting = setting.Left(pos);
		pos = setting.ReverseFind(';');
		setting = setting.Left(pos + 1);
		changed = true;
	}

	vCatLog("Settings.CheckExtStringFormat", ".");
	return changed;
}

void MigrateAndMergeFileExtensions(HKEY hKey, LPCTSTR newName, LPCTSTR oldName, LPTSTR dest, LPCTSTR defVal,
                                   LPCTSTR oldValsToRemove, DWORD len)
{
	DWORD readLen = len;
	LSTATUS err = RegQueryValueEx(hKey, newName, 0, 0, (LPBYTE)dest, &readLen);
	if (ERROR_SUCCESS == err && dest[0])
		return;

	// no settings exist in the new location
	// check for old reg value name
	readLen = len;
	err = RegQueryValueEx(hKey, oldName, 0, 0, (LPBYTE)dest, &readLen);
	if (ERROR_SUCCESS != err || dest[0] == '\0')
	{
		// new installation? - no previous user settings, use defaults
		_tcscpy(dest, defVal);
		return;
	}

	// merge old values with new defaults
	CString curVals(dest);
	CString newVals(defVal);
	CString oldDefaultsToRemove(oldValsToRemove);
	int delimPos;
	bool modified = false;

	::CheckExtStringFormat(curVals);

	// remove old default values
	for (delimPos = oldDefaultsToRemove.Find(';'); delimPos != -1; delimPos = oldDefaultsToRemove.Find(';'))
	{
		CString curOldDefVal = oldDefaultsToRemove.Left(delimPos + 1);
		oldDefaultsToRemove = oldDefaultsToRemove.Mid(delimPos + 1);
		if (curOldDefVal.GetLength())
		{
			modified = true;
			curVals.Replace(curOldDefVal, "");
		}
	}

	// add new default values
	for (delimPos = newVals.Find(';'); delimPos != -1; delimPos = newVals.Find(';'))
	{
		CString curNewVal = newVals.Left(delimPos + 1);
		newVals = newVals.Mid(delimPos + 1);

		if (curNewVal.GetLength() && -1 == curVals.Find(curNewVal))
		{
			modified = true;
			curVals += curNewVal;
		}
	}

	if (modified && (DWORD)curVals.GetLength() < len)
		_tcscpy(dest, curVals);

	RegSetValueEx(hKey, newName, 0, REG_SZ, (LPBYTE)(LPCSTR)curVals, DWORD(curVals.GetLength() + 1));
}

void CSettings::ValidateExtSettings()
{
	CString tmp;

	m_hdrExts[MAX_PATH - 1] = '\0';
	tmp = m_hdrExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_hdrExts, tmp);

	m_srcExts[MAX_PATH - 1] = '\0';
	tmp = m_srcExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_srcExts, tmp);

	m_resExts[MAX_PATH - 1] = '\0';
	tmp = m_resExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_resExts, tmp);

	m_idlExts[MAX_PATH - 1] = '\0';
	tmp = m_idlExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_idlExts, tmp);

	m_binExts[MAX_PATH - 1] = '\0';
	tmp = m_binExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_binExts, tmp);

	mExtensionsToIgnore[MAX_PATH - 1] = '\0';
	tmp = mExtensionsToIgnore;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(mExtensionsToIgnore, tmp);

	m_javExts[MAX_PATH - 1] = '\0';
	tmp = m_javExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_javExts, tmp);

	m_htmlExts[MAX_PATH - 1] = '\0';
	tmp = m_htmlExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_htmlExts, tmp);

	m_vbExts[MAX_PATH - 1] = '\0';
	tmp = m_vbExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_vbExts, tmp);

	m_jsExts[MAX_PATH - 1] = '\0';
	tmp = m_jsExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_jsExts, tmp);

	m_csExts[MAX_PATH - 1] = '\0';
	tmp = m_csExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_csExts, tmp);

	m_perlExts[MAX_PATH - 1] = '\0';
	tmp = m_perlExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_perlExts, tmp);

	m_phpExts[MAX_PATH - 1] = '\0';
	tmp = m_phpExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_phpExts, tmp);

	m_aspExts[MAX_PATH - 1] = '\0';
	tmp = m_aspExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_aspExts, tmp);

	m_xmlExts[MAX_PATH - 1] = '\0';
	tmp = m_xmlExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_xmlExts, tmp);

	m_plainTextExts[MAX_PATH - 1] = '\0';
	tmp = m_plainTextExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_plainTextExts, tmp);

	m_xamlExts[MAX_PATH - 1] = '\0';
	tmp = m_xamlExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_xamlExts, tmp);

	m_vbsExts[MAX_PATH - 1] = '\0';
	tmp = m_vbsExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_vbsExts, tmp);

	m_ShaderExts[MAX_PATH - 1] = '\0';
	tmp = m_ShaderExts;
	if (::CheckExtStringFormat(tmp))
		::_tcscpy(m_ShaderExts, tmp);
}

void CSettings::CheckForConflicts()
{
	ValidateExtSettings();

	// shorthand is dependent on shrinkMemberListboxes
	if (!m_bShrinkMemberListboxes)
		m_bAllowShorthand = false;

	// acronyms are dependent on shorthand
	if (!m_bAllowShorthand)
		m_bAllowAcronyms = false;

	// there is no UI option for m_mouseOvers (except in vc6 on the C/C++ 6 page)
	if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
	{
		// there is a UI option for quick info tooltips in vs2005+ (mQuickInfoInVs8)
		if (mQuickInfoInVs8 && !m_mouseOvers)
			m_mouseOvers = true;

		if (!mQuickInfoInVs8 && m_mouseOvers)
			m_mouseOvers = false;

		// m_bGiveCommentsPrecedence and mScopeTooltips don't need to be
		// handled as in pre-vs2005 since mQuickInfoInVs8 has a UI option
		// that is enables/disables the ui for m_bGiveCommentsPrecedence and
		// mScopeTooltips (no behind the scenes toggling of
		// m_bGiveCommentsPrecedence and mScopeTooltips in vs2005+).
	}
	else if (gShellAttr)
	{
		// if comments from source files are to appear in tooltips, tooltips need to be enabled
		// (there is no UI option for quick info tooltips pre-vs2005 except for vc6)
		// (mQuickInfoInVs8 is visible only for vs2005+)
		if (m_bGiveCommentsPrecedence && !m_mouseOvers)
			m_mouseOvers = true;

		if (mScopeTooltips && !m_mouseOvers)
			m_mouseOvers = true;
	}

	if (mCheckForLatestBetaVersion && !mCheckForLatestVersion)
		mCheckForLatestVersion = true;
}

bool CSettings::IsAutomatchAllowed(bool funcFixUp /*= false*/, UINT key /*= 0*/)
{
	if (!AutoMatch)
		return false;

	if (funcFixUp)
	{
		if (!g_IdeSettings || !gShellAttr || !gShellAttr->IsDevenv14OrHigher())
			return true;

		if (CS == gTypingDevLang)
		{
			// vs2015 c# does brace completion on accept of listbox item with (
			// [case: 96598] vs15 c# does brace completion on accept of listbox item with ( but not with other keys
			if (gShellAttr->IsDevenv14u2OrHigher())
			{
				if (key == '(' && g_IdeSettings->GetEditorIntOption("CSharp", "BraceCompletion"))
					return false;
				else
					return true;
			}
		}
		else
			return true;
	}

	if (gShellAttr && gShellAttr->IsDevenv12OrHigher() && g_IdeSettings)
	{
		int vsAutoMatch;
		if (IsCFile(gTypingDevLang))
		{
			vsAutoMatch = g_IdeSettings->GetEditorIntOption("C/C++", "BraceCompletion");
			if (vsAutoMatch && key == '<')
			{
				// they don't automatch < which breaks #include <
				vsAutoMatch = 0;
			}
		}
		else if (CS == gTypingDevLang)
			vsAutoMatch = g_IdeSettings->GetEditorIntOption("CSharp", "BraceCompletion");
		else if (Is_VB_VBS_File(gTypingDevLang))
			vsAutoMatch = g_IdeSettings->GetEditorIntOption("Basic", "BraceCompletion");
		else if (JS == gTypingDevLang)
			vsAutoMatch = g_IdeSettings->GetEditorIntOption("JavaScript", "BraceCompletion");
		else if (Is_Some_Other_File(gTypingDevLang))
			vsAutoMatch = g_IdeSettings->GetEditorIntOption("Plain Text", "BraceCompletion");
		else
			vsAutoMatch = 0;

		if (vsAutoMatch)
			return false;
	}

	return true;
}

// bool CSettings::IsResharperIsPresent()
// {
// 	static bool bIsResharperIsPresent = FALSE;
// 	static bool once = true;
// 	if (once)
// 	{
// 		once = false;
// 		if (GetModuleHandleW(L"JetBrains.Resharper.dll"))
// 			bIsResharperIsPresent = true;
// 		else if (GetModuleHandleW(L"JetBrains.Resharper.Vs.dll"))
// 			bIsResharperIsPresent = true;
// 		else if(gShellAttr->IsDevenv10())
// 		{
// 			// See if r# is installed in 2010 for vb and/or c#
// 			// {aac0ec61-b452-4326-b309-25b2d16a8a42} -
// JetBrains.ReSharper.VS.ProjectModel.VSFolderPropertiesExtenderProvider 			if(GetRegValue(HKEY_CURRENT_USER,
// "Software\\Microsoft\\VisualStudio\\10.0_Config\\CLSID\\{aac0ec61-b452-4326-b309-25b2d16a8a42}", NULL).GetLength())
// 				bIsResharperIsPresent = true;
// 		}
// 		else if(gShellAttr->IsDevenv11OrHigher())
// 		{
// 			// See if r# is installed in 2010 for vb and/or c#
// 			// {aac0ec61-b452-4326-b309-25b2d16a8a42} -
// JetBrains.ReSharper.VS.ProjectModel.VSFolderPropertiesExtenderProvider 			if(GetRegValue(HKEY_CURRENT_USER,
// "Software\\Microsoft\\VisualStudio\\11.0_Config\\CLSID\\{aac0ec61-b452-4326-b309-25b2d16a8a42}", NULL).GetLength())
// 				bIsResharperIsPresent = true;
// 			_ASSERTE(gShellAttr->IsDevenv10()); // Resharper logic needs updating for VS11
// 		}
// 		else if (gShellAttr->IsDevenv10OrHigher())
// 		{
// 			// Leave this block as is - it will fire when we've
// 			// forgotten to create a new block for another version of VS.
// 			// Resharper logic needs updating for VSxx
// 			_ASSERTE(gShellAttr->IsDevenv10());
// 		}
// 	}
// 	return bIsResharperIsPresent;
//}

bool CSettings::UsingResharperSuggestions(int fileType, bool setTrue)
{
	static std::vector<int> sFileTypes;

	bool found = false;
	for (auto ft : sFileTypes)
	{
		if (ft == fileType)
		{
			found = true;
			break;
		}
	}

	if (setTrue && !found)
	{
		sFileTypes.push_back(fileType);
		found = true;
	}
	return found;
}

void CSettings::InitDefaults(HKEY hKey)
{
	// key "UnderlineSpellingErrors" has existing since the beginning of time.
	// If it is not present, then this is a new install (for the current version
	// of the IDE).
	const bool firstInstall = !RegValueExists(hKey, _T("UnderlineSpellingErrors"));
	const bool defaultTrueExceptInCppBuilder =
#if defined(RAD_STUDIO)
	    false;
#else
	    true;
#endif
	const bool defaultFalseExceptInCppBuilder =
#if defined(RAD_STUDIO)
	    true;
#else
	    false;
#endif

#ifdef AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_ENABLED), &m_enableVA, true);
	if (!GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "InitAvrOptions").GetLength())
	{
		// Override default settings for AVR users
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "InitAvrOptions", "Yes");
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "ShowTipOfTheDay", "No");
		mUseTomatoBackground = false;
		SaveRegBool(hKey, _T(ID_RK_BACKGROUND_TOMATO), &mUseTomatoBackground);
	}
#endif // AVR_STUDIO

	if (gShellAttr->IsFastProjectOpenAllowed())
		ReadRegDword(hKey, _T("FastProjectOpen2"), &m_FastProjectOpen, FALSE);
	else
		m_FastProjectOpen = FALSE; // default to slow open so classview works
	ReadRegDword(hKey, _T("SurroundSelection"), &m_SelectionOnChar, TRUE);
	ReadRegDword(hKey, _T("UnderlineSpellingErrors"), &m_spellFlags, TRUE);
	ReadRegBool(hKey, _T(ID_RK_CATCHALL), &m_catchAll);
#if defined(_DEBUG) && defined(SEAN)
	m_catchAll = false;
#endif
	ReadRegBool(hKey, _T(ID_RK_RTFCOPY), &m_RTFCopy, FALSE);
	ReadRegBool(hKey, _T(ID_RK_COLORPRINTING), &m_ColorPrinting, false);
	ReadRegBool(hKey, _T(ID_RK_ENHANCEDFORMAT), &m_ActiveSyntaxColoring, defaultTrueExceptInCppBuilder);
	ReadRegBool(hKey, _T(ID_RK_MINIHELPPLACEMENT), &minihelpAtTop);
	ReadRegBool(hKey, _T("HideContextDefWindow"), &m_noMiniHelp, false);
	ReadRegBool(hKey, _T(ID_RK_BOLDBRACEMATCH), &boldBraceMatch);
	ReadRegBool(hKey, _T(ID_RK_AUTOMATCH), &AutoMatch);
	ReadRegBool(hKey, _T(ID_RK_CORRECTCASE), &CaseCorrect, gShellAttr->GetDefaultCorrectCaseSetting());
	ReadRegBool(hKey, _T(ID_RK_ONESTEPHELP), &oneStepHelp);
	ReadRegBool(hKey, _T(ID_RK_ASCIIW32API), &m_AsciiW32API);
	ReadRegBool(hKey, _T(ID_RK_AUTOSUGGEST), &m_autoSuggest, true);
	ReadRegBool(hKey, _T(ID_RK_SCOPESUGGEST), &mScopeSuggestions, true);
	ReadRegBool(hKey, _T(ID_RK_OVERRIDE_REPAIR_CASE), &mOverrideRepairCase, true);
	ReadRegBool(hKey, _T(ID_RK_SUGGEST_NULLPTR), &mSuggestNullptr, gShellAttr->IsDevenv10OrHigher());
	ReadRegBool(hKey, _T(ID_RK_SUGGEST_NULL_BY_DEFAULT), &mSuggestNullInCppByDefault, true);
	ReadRegBool(hKey, _T(ID_RK_PREFER_SHORTEST_ADD_INCLUDE), &mAddIncludePreferShortest, true);
	ReadRegDword(hKey, _T(ID_RD_ADD_INCLUDE_PATH_PREFERENCE), (DWORD*)&mAddIncludePath, 0);
	if (mAddIncludePreferShortest == false &&
	    mAddIncludePath == 0) // old setting is in non-default state, new setting is in default state
	{
		mAddIncludePath =
		    PP_RELATIVE_TO_FILE_OR_INCLUDE_DIR; // this is equivalent to mAddIncludePreferShortest being false
		mAddIncludePreferShortest =
		    true; // set setting back to default so the import mechanism doesn't get triggered again
	}
#ifdef AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_FORMAT_DOXYGEN_TAGS), &mFormatDoxygenTags, false);
#else
	ReadRegBool(hKey, _T(ID_RK_FORMAT_DOXYGEN_TAGS), &mFormatDoxygenTags, true);
#endif // AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_USE_NEW_FILE_OPEN), &mUseNewFileOpen, true);
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		ReadRegBool(hKey, _T(ID_RK_USE_NEW_THEME_DEV11_PLUS), &mUseNewTheme11Plus, true);
	else
		mUseNewTheme11Plus = false;
	ReadRegDword(hKey, _T(ID_RK_HANDLE_BROWSER_APP_COMMANDS), &mBrowserAppCommandHandling, 
#if defined(RAD_STUDIO)
		0
#else
        1
#endif
	);
	ReadRegDword(hKey, _T(ID_RK_LINE_COMMENT_SLASH_COUNT), &mLineCommentSlashCount, 2);
	if (2 > mLineCommentSlashCount || 1000 < mLineCommentSlashCount)
		mLineCommentSlashCount = 2;
	ReadRegBool(hKey, _T(ID_RK_UNREAL_SUPPORT), &mUnrealScriptSupport, false);
	ReadRegBool(hKey, _T(ID_RK_UNREAL_CPP_SUPPORT), &mUnrealEngineCppSupport, false);
	ReadRegBool(hKey, _T(ID_RK_TABINVOKESINTELLISENSE), &m_tabInvokesIntellisense, false);
	ReadRegBool(hKey, _T(ID_RK_VAWORKSPACE), &m_VAWorkspace, false);
	ReadRegBool(hKey, _T(ID_RK_SMARTPASTE), &m_smartPaste, true);
	ReadRegBool(hKey, _T(ID_RK_AUTOBACKUP), &m_autoBackup, gShellAttr->GetDefaultAutobackupSetting());
	ReadRegBool(hKey, _T(ID_RK_SKIPLOCALMACROS), &m_skipLocalMacros, false);
	ReadRegBool(hKey, _T(ID_RK_MOUSEOVERS), &m_mouseOvers, defaultTrueExceptInCppBuilder);
	if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
	{
		ReadRegBool(hKey, _T(ID_RK_QUICKINFOVS8), &mQuickInfoInVs8, false);
		ReadRegBool(hKey, _T(ID_RK_ScopeTooltips), &mScopeTooltips, mQuickInfoInVs8 && m_mouseOvers);
	}
	else
	{
		mQuickInfoInVs8 = false;
		ReadRegBool(hKey, _T(ID_RK_ScopeTooltips), &mScopeTooltips, m_mouseOvers);
	}
	ReadRegBool(hKey, _T(ID_RK_RESOURCEALARM), &m_resourceAlarm);
	ReadRegBool(hKey, _T(ID_RK_MULTIPLEPROJSUPPORT), &m_multipleProjSupport);
	ReadRegBool(hKey, _T(ID_RK_KEEPBOOKMARKS), &m_keepBookmarks, false);

	if (gShellAttr->IsDevenv11OrHigher())
		ReadRegBool(hKey, _T(ID_RK_EnableIconTheme), &mEnableIconTheme, true);
	else
		mEnableIconTheme = false;

	ReadRegBool(hKey, _T(ID_RK_MODTHREAD), &m_doModThread, false);
	ReadRegBool(hKey, _T("DefGuess2"), &m_defGuesses, false);
	ReadRegBool(hKey, _T(ID_RK_PARSEIMPORTS), &m_parseImports);
	ReadRegBool(hKey, _T(ID_RK_AGGRESSIVEMATCH), &m_aggressiveFileMatching);
	ReadRegBool(hKey, _T(ID_RK_FIXPTROP), &m_fixPtrOp);
	ReadRegBool(hKey, _T("CompleteWithTab"), &m_bCompleteWithTab);
	ReadRegBool(hKey, _T("CompleteWithReturn"), &m_bCompleteWithReturn);
	ReadRegBool(hKey, _T("CompleteWithAny"), &m_bCompleteWithAny, false);
	ReadRegBool(hKey, _T("CompleteWithAnyVsOverride"), &m_bCompleteWithAnyVsOverride, false);
	ReadRegBool(hKey, _T(ID_RK_EnableProjectSymbolFilter), &mEnableProjectSymbolFilter, true);
	ReadRegBool(hKey, _T(ID_RK_FsisAltCache), &mFsisAltCache, true);
	ReadRegBool(hKey, _T(ID_RK_SimpleWordMatchHighlights), &mSimpleWordMatchHighlights, false);
	ReadRegBool(hKey, _T(ID_RK_ForceCaseInsensitveFilters), &mForceCaseInsensitiveFilters, true);
	ReadRegBool(hKey, _T(ID_RK_EnableFuzzyFilters), &mEnableFuzzyFilters, true);
	ReadRegDword(hKey, _T(ID_RK_FuzzyFiltersThreshold), &mFuzzyFiltersThreshold, 75);
	ReadRegBool(hKey, _T(ID_RK_EnableFuzzyLite), &mEnableFuzzyLite, true);
	ReadRegBool(hKey, _T(ID_RK_EnableFuzzyMultiThreading), &mEnableFuzzyMultiThreading, true);
	ReadRegDword(hKey, _T(ID_RK_ClassMemberNamingBehavior), &mClassMemberNamingBehavior, cmn_prefixDependent);
	if (mClassMemberNamingBehavior > cmn_last)
		mClassMemberNamingBehavior = cmn_prefixDependent;
	ReadRegDword(hKey, _T(ID_RK_DimmedMenuItemOpacity), &mDimmedMenuItemOpacityValue, 60);
	if (mDimmedMenuItemOpacityValue > 100)
		mDimmedMenuItemOpacityValue = 100;
	ReadRegDword(hKey, _T(ID_RK_GotoInterfaceBehavior), &mGotoInterfaceBehavior, gib_conditionalBcl);
	ReadRegDword(hKey, _T(ID_RK_VsThemeColorBehavior), &mVsThemeColorBehavior, vt_default);
#ifdef AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_EnumerateVsLangReferences), &mEnumerateVsLangReferences, false);
	mRestrictVaToPrimaryFileTypes = mRestrictVaListboxesToC = true;
#else
	ReadRegBool(hKey, _T(ID_RK_EnumerateVsLangReferences), &mEnumerateVsLangReferences, true);
	// [case: 75322] restrict listboxes to c by default for first time installs
	ReadRegBool(hKey, _T(ID_RK_RestrictVaListboxesToC), &mRestrictVaListboxesToC, firstInstall);
#if defined(VA_CPPUNIT)
	mRestrictVaToPrimaryFileTypes = false;
#else
	ReadRegBool(hKey, _T(ID_RK_RestrictVaToPrimaryFileTypes), &mRestrictVaToPrimaryFileTypes, firstInstall);
#endif // VA_CPPUNIT
#endif // AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_AugmentParamInfo), &mVaAugmentParamInfo, true);
	ReadRegBool(hKey, _T(ID_RK_PartialSnippetShortcutMatches), &mPartialSnippetShortcutMatches, false);
	ReadRegBool(hKey, _T(ID_RK_FindRefsDisplayIncludes), &mFindRefsDisplayIncludes);
	ReadRegBool(hKey, _T(ID_RK_FindRefsDisplayUnknown), &mFindRefsDisplayUnknown);
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
		ReadRegBool(hKey, _T(ID_RK_UsePreviewTab), &mUsePreviewTab);
	else
		mUsePreviewTab = false;
	ReadRegBool(hKey, _T(ID_RK_AutoListIncludes), &mAutoListIncludes);
	ReadRegBool(hKey, _T(ID_RK_UpdateHcbOnHover), &mUpdateHcbOnHover);
	ReadRegBool(hKey, _T(ID_RK_ScopeFallbackToGotodef), &mScopeFallbackToGotodef);
	ReadRegBool(hKey, _T(ID_RK_CacheFileFinderData), &mCacheFileFinderData);
	ReadRegBool(hKey, _T(ID_RK_FindRefsAltSharedFileBehavior), &mFindRefsAlternateSharedFileBehavior, false);
	ReadRegBool(hKey, _T(ID_RK_ParseLocalTypesForGotoNav), &mParseLocalTypesForGotoNav);
	ReadRegBool(hKey, _T(ID_RK_EnableWin8ColorHook), &mEnableWin8ColorHook);
#ifdef AVR_STUDIO
	mRoamingStepFilterSolutionConfig = mEnableDebuggerStepFilter = mLookupNuGetRepository = mParseNuGetRepository =
	    false;
#else
	ReadRegBool(hKey, _T(ID_RK_NugetDirLookup), &mLookupNuGetRepository,
	            gShellAttr && gShellAttr->IsDevenv10OrHigher());
	ReadRegBool(hKey, _T(ID_RK_NugetDirParse), &mParseNuGetRepository, gShellAttr && gShellAttr->IsDevenv10OrHigher());
	ReadRegBool(hKey, _T(ID_RK_EnableDbgStepFilter), &mEnableDebuggerStepFilter,
	            gShellAttr && gShellAttr->IsDevenv10OrHigher());
	ReadRegBool(hKey, _T(ID_RK_RoamingStepFilterSolutionConfig), &mRoamingStepFilterSolutionConfig, true);
#endif // AVR_STUDIO

	ReadRegBool(hKey, _T("EnhColorListboxes"), &m_bEnhColorListboxes);
	ReadRegBool(hKey, _T("EnhColorSourceWindows"), &m_bEnhColorSourceWindows);
	ReadRegBool(hKey, _T("EnhColorObjectBrowser"), &m_bEnhColorObjectBrowser);
	ReadRegBool(hKey, _T("EnhColorTooltips"), &m_bEnhColorTooltips);
	ReadRegBool(hKey, _T("EnhColorViews"), &m_bEnhColorViews);
	if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
		ReadRegBool(hKey, _T(ID_RK_RefactorAutoFormat), &mRefactorAutoFormat, true);
	else
		mRefactorAutoFormat = true;

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		ReadRegBool(hKey, _T("EnhColorFindResults"), &m_bEnhColorFindResults, m_bEnhColorViews);
	else
	{
#ifdef _DEBUG
		m_bEnhColorFindResults = m_bEnhColorViews;
#else
		m_bEnhColorFindResults = false;
#endif // _DEBUG
	}
	ReadRegBool(hKey, _T("EnhColorWizardBar"), &m_bEnhColorWizardBar);
	ReadRegBool(hKey, _T("LocalSymbolsInBold"), &m_bLocalSymbolsInBold, false);
	ReadRegBool(hKey, _T("StableSymbolsInItalics"), &m_bStableSymbolsInItalics, gShellAttr->IsDevenv10OrHigher());
	ReadRegBool(hKey, _T("ListNonInheritedMembersFirst"), &m_bListNonInheritedMembersFirst, false);
	ReadRegDword(hKey, _T("DisplayXSuggestions"), &m_nDisplayXSuggestions, 4);
	ReadRegBool(hKey, _T("SuppressUnderlines"), &m_bSupressUnderlines, false);
	ReadRegBool(hKey, _T("GiveCommentsPrecedence"), &m_bGiveCommentsPrecedence, false);

	ReadRegBool(hKey, _T(ID_RK_SHOWSCOPE), &m_showScopeInContext);
	ReadRegDword(hKey, _T(ID_RK_MINIHELPFONTSIZE), &m_minihelpFontSize, 9);
	ReadRegDword(hKey, _T(ID_RK_FNPARENGAP), &m_fnParenGap);
	ReadRegDword(hKey, _T(ID_RK_WORKSPACEITEMCNT), &m_RecentCnt, 10);
	// prior to 4.0 there was a single setting for both recent and clipboard cnts
	//  - clipboard is new, so use recent val as default
	ReadRegDword(hKey, _T(ID_RK_CLIPBOARDITEMCNT), &m_clipboardCnt, m_RecentCnt);
	ReadRegString(hKey, _T(ID_RK_IFDEFSTR), m_ifdefString, _T(IDS_DEF_IFDEFSTR), MAX_PATH);
	ReadRegDword(hKey, _T(ID_RK_IMPORTTIMEOUT), &m_ImportTimeout, 20);
	if (m_ImportTimeout < 1 || m_ImportTimeout > 600)
		m_ImportTimeout = 20;
	ReadRegBool(hKey, _T(ID_RK_RAPIDFIRE), &m_rapidFire, false);
	ReadRegBool(hKey, _T(ID_RK_CODETEMPTOOL), &m_codeTemplateTooltips, true);
	ReadRegBool(hKey, _T(ID_RK_AUTOM_), &m_auto_m_, false);
	ReadRegBool(hKey, _T(ID_RK_UNDERLINETYPOS), &m_underlineTypos,
#ifdef AVR_STUDIO
	            // [case: 138801]
	            true
#else
	            gShellAttr->IsDevenv11OrHigher() ? false : true
#endif
	);

	ReadRegBool(hKey, _T(ID_RK_CONTEXTPREFIX), &m_contextPrefix, false);
	ReadRegBool(hKey, _T(ID_RK_BRACEMISMATCH), &m_braceMismatches, gShellAttr->GetDefaultBraceMismatchSetting());
	if (gShellAttr->IsDevenv10OrHigher())
		m_menuInMargin = false;
	else
		ReadRegBool(hKey, _T(ID_RK_MARGINMENU), &m_menuInMargin, true);
	ReadRegBool(hKey, _T(ID_RK_COLINDICATOR), &m_colIndicator, false);
	ReadRegDword(hKey, _T(ID_RK_COLINDICATORCOLS), &m_colIndicatorColPos, 75);
	ReadRegBool(hKey, _T(ID_RK_PARAMINFO), &m_ParamInfo);
	ReadRegBool(hKey, _T(ID_RK_AUTOCOMPLETE), &m_AutoComplete);
	ReadRegBool(hKey, _T(ID_RK_AUTOCOMMENTS), &m_AutoComments);
	ReadRegDword(hKey, _T("MRUOptions"), &m_nMRUOptions, 1);
	ReadRegDword(hKey, _T("HCBOptions"), &m_nHCBOptions, 0);
	ReadRegBool(hKey, _T("BoldNonInheritedMembers"), &m_bBoldNonInheritedMembers, TRUE);
	ReadRegBool(hKey, _T("ShrinkMemberListboxes"), &m_bShrinkMemberListboxes, true);

	ReadRegString(hKey, _T(ID_RK_EXTHDR), m_hdrExts, _T(IDS_EXTHDR), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_EXTSRC), m_srcExts, _T(IDS_EXTSRC), MAX_PATH);
	ReadRegBool(hKey, _T(ID_RK_EXTSRC_ISUPDATED), &m_srcExts_IsUpdated, false);
	if (m_srcExts_IsUpdated == false) // not yet updated
	{
		size_t len = strlen(m_srcExts);
		if (len > 0 && m_srcExts[len - 1] != ';')
			strcat_s(m_srcExts, MAX_PATH, ";");

		strcat_s(m_srcExts, MAX_PATH, _T(IDS_EXTSRC_APPEND));
		m_srcExts_IsUpdated = true;
	}

	ReadRegString(hKey, _T(ID_RK_EXTRES), m_resExts, _T(IDS_EXTRES), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_EXTIDL), m_idlExts, _T(IDS_EXTIDL), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_EXTBIN), m_binExts, _T(IDS_EXTBIN), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_EXTJAV), m_javExts, _T(".jav;.java;.jsl;"), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_ASP_EXTS), m_aspExts,
	              _T(".asp;.aspx;.hta;.asa;.asax;.ascx;.ashx;.asmx;.master;.vbhtml;.cshtml;"), MAX_PATH);
	ReadRegString(hKey, _T("ExtVbs"), m_vbsExts, _T(".vbs;.svb;"), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_XML_EXTS), m_xmlExts, _T(".xml;.resx;.config;.manifest;"), MAX_PATH);
	ReadRegString(hKey, _T("ExtPlainText"), m_plainTextExts,
	              _T(".txt;.log;.dat;.ini;.reg;.rgs;.bak;.inf;.def;.pkgdef;.mak;.mac;.ctc;.sln;.bat;.cmd;.ps1;.va;"),
	              MAX_PATH);
	ReadRegString(hKey, _T("ExtXaml"), m_xamlExts, _T(".xaml;"), MAX_PATH);
	ReadRegString(hKey, _T("ExtPHP"), m_phpExts, _T(".php;"), MAX_PATH);
	ReadRegString(hKey, _T("ExtJS"), m_jsExts, _T(".js;"), MAX_PATH);
	ReadRegString(hKey, _T("ExtPerl"), m_perlExts, _T(".pl;.cgi;"), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_SHADER_EXTS), m_ShaderExts, _T(".usf;.ush;.hlsl;.shader;.cginc;"), MAX_PATH);
	_strlwr_s(m_ShaderExts, sizeof(m_ShaderExts));

	ReadRegString(hKey, _T(ID_RK_SurroundWithKeys), mSurroundWithKeys, _T("'/*{#("), MAX_PATH);
	ReadRegString(hKey, ID_RK_FONTFACE, m_srcFontName, L"Courier", 254);
	ReadRegDword(hKey, _T(ID_RK_FONTSIZE), &FontSize, 10);
	ReadRegBool(hKey, _T(ID_RK_ENABLEPASTEMENU), &m_EnablePasteMenu, true);
	ReadRegDword(hKey, _T(ID_RK_SORT_DEF_LIST), &m_sortDefPickList, 1);

#ifdef AVR_STUDIO
	m_bUseDefaultIntellisense = FALSE;
#else
	if (gShellAttr->IsDevenv10OrHigher())
		// Use default by default in vs10?
		ReadRegBool(hKey, _T("UseDefaultIntellisense"), &m_bUseDefaultIntellisense, TRUE);
	else
		ReadRegBool(hKey, _T("UseDefaultIntellisense"), &m_bUseDefaultIntellisense, FALSE);
#endif

	ReadRegBool(hKey, _T("DisplayFilteringToolbar"), &m_bDisplayFilteringToolbar, true);
	ReadRegBool(hKey, _T("AllowShorthand"), &m_bAllowShorthand, true);
	ReadRegBool(hKey, _T("AllowAcronyms"), &m_bAllowAcronyms, true);

	ReadRegDword(hKey, _T(ID_RK_LOCALECHANGE), &m_doLocaleChange, 0);
	ReadRegBool(hKey, _T(ID_RK_MACROPARSE1), &m_limitMacroParseLocal);
	ReadRegBool(hKey, _T(ID_RK_MACROPARSE2), &m_limitMacroParseSys);
	ReadRegBool(hKey, _T(ID_RK_VAVIEWGOTODEF), &mVaviewGotoDef, true);
	ReadRegBool(hKey, _T(ID_RK_HIGHLIGHTREFERENCESBYDEFAULT), &mHighlightFindReferencesByDefault, false);
	ReadRegDword(hKey, _T(ID_RK_BRACEMATCHSTYLE), &mBraceAutoMatchStyle, 3);
	ReadRegDword(hKey, _T(ID_RK_SUGGESTIONSELECTSTYLE), &mSuggestionSelectionStyle, 1);
	ReadRegBool(hKey, _T(ID_RK_TOOLTIPSINFINDRESULTS), &mUseTooltipsInFindReferencesResults, true);
	ReadRegBool(hKey, _T(ID_RK_DISLPAYREFACTORINGBUTTON), &mDisplayRefactoringButton, defaultTrueExceptInCppBuilder);
	//	ReadRegBool(hKey, _T(ID_RK_AUTODISLPAYREFACTORINGBUTTON), &mAutoDisplayRefactoringButton, true);
	mAutoDisplayRefactoringButton = false;
	ReadRegBool(hKey, _T(ID_RK_INCLUDEDEFAULTPARAMETERVALUES), &mIncludeDefaultParameterValues, true);
	ReadRegBool(hKey, _T(ID_RK_EXTENSIONLESSFILEISHEADER), &mExtensionlessFileIsHeader, false);
	ReadRegBool(hKey, _T(ID_RK_MARKCURRENTLINE), &mMarkCurrentLine, false);
	if (gShellAttr->IsDevenv10OrHigher())
	{
		ReadRegDword(hKey, _T(ID_RK_CURRENTLINEMARKERSTYLE), &mCurrentLineBorderStyle, 1); // solid border
		ReadRegDword(hKey, _T(ID_RK_CURRENTLINEVISUALSTYLE), &mCurrentLineVisualStyle, 2); // background colored
	}
	else
	{
		ReadRegDword(hKey, _T(ID_RK_CURRENTLINEMARKERSTYLE), &mCurrentLineBorderStyle, 4); // dotted border
		ReadRegDword(hKey, _T(ID_RK_CURRENTLINEVISUALSTYLE), &mCurrentLineVisualStyle,
		             0x2100); // border only, span line
	}
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
	mCheckForLatestVersion = false;
	mCheckForLatestBetaVersion = false;
	mRecommendAfterExpires = false;
#else
	ReadRegBool(hKey, _T(ID_RK_CHECKFORLATESTVERSION), &mCheckForLatestVersion, true);
	ReadRegBool(hKey, _T(ID_RK_CHECKFORLATESTBETA), &mCheckForLatestBetaVersion, false);
	ReadRegBool(hKey, _T(ID_RK_RECOMMENDAFTEREXPIRES), &mRecommendAfterExpires, true);
#endif
	ReadRegBool(hKey, _T(ID_RK_DONTINSERTSPACEAFTERCOMMENT), &mDontInsertSpaceAfterComment, false);
	ReadRegBool(hKey, _T(ID_RK_LINENUMBERSINFINDREFSRESULTS), &mLineNumbersInFindRefsResults, true);
	ReadRegBool(hKey, _T(ID_RK_ENABLESTATUSBARMESSAGES), &mEnableStatusBarMessages, true);
	ReadRegBool(hKey, _T(ID_RK_CLOSESUGGESTIONLISTONEXACTMATCH), &mCloseSuggestionListOnExactMatch, false);
	ReadRegBool(hKey, _T(ID_RK_NOISYEXECFAILURENOTIFICATIONS), &mNoisyExecCommandFailureNotifications, true);
	ReadRegBool(hKey, _T(ID_RK_DISPLAY_COMMENT_REFERENCES), &mDisplayCommentAndStringReferences, false);
	ReadRegBool(hKey, _T(ID_RK_ENC_FIELD_PUBLIC_ACCESSORS), &mEncFieldPublicAccessors, true);
	ReadRegDword(hKey, _T(ID_RK_ENC_FIELD_MOVE_VARIABLE), (DWORD*)&mEncFieldMoveVariable, 0);
	ReadRegBool(hKey, _T(ID_RK_FIND_INHERITED_REFERENCES), &mDisplayWiderScopeReferences, true);

	// for new installs, search only active project, but keep old behavior (all projects) if upgrading
	ReadRegBool(hKey, _T(ID_RK_FIND_REFERENCES_FROM_ALL_PROJECTS), &mDisplayReferencesFromAllProjects, !firstInstall);

	ReadRegBool(hKey, _T(ID_RK_RENAME_COMMENT_REFERENCES), &mRenameCommentAndStringReferences, false);
	ReadRegBool(hKey, _T(ID_RK_RENAME_WIDER_SCOPE_REFERENCES), &mRenameWiderScopeReferences, true);
	ReadRegBool(hKey, _T(ID_RK_INCLUDEREFACTORINGINLISTBOXES), &mIncludeRefactoringInListboxes, true);
	ReadRegBool(hKey, _T(ID_RK_AUTOSIZE_LISTBOX), &mAutoSizeListBox, true);
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
	mParseFilesInDirectoryIfEmptySolution = false;
	mParseFilesInDirectoryIfExternalFile = false;
#else
	ReadRegBool(hKey, _T(ID_RK_PARSE_FILES_IN_DIR_IF_EMPTY), &mParseFilesInDirectoryIfEmptySolution, true);
	ReadRegBool(hKey, _T(ID_RK_PARSE_FILES_IN_DIR_IF_EXTERNAL), &mParseFilesInDirectoryIfExternalFile, false);
#endif
	ReadRegDword(hKey, _T(ID_RK_LISTBOX_FLAGS), &mListboxFlags, 0);
	ReadRegBool(hKey, _T(ID_RK_MARK_FIND_TEXT), &mMarkFindText, true);
	ReadRegBool(hKey, _T(ID_RK_PROJECT_NODE_IN_RESULTS), &mIncludeProjectNodeInReferenceResults, true);
	ReadRegBool(hKey, _T(ID_RK_PROJECT_NODE_IN_REN_RESULTS), &mIncludeProjectNodeInRenameResults, true);
	ReadRegBool(hKey, _T(ID_RK_BACKGROUND_TOMATO), &mUseTomatoBackground, defaultTrueExceptInCppBuilder);
	ReadRegBool(hKey, _T(ID_RK_MIF_REGIONS), &mMethodInFile_ShowRegions, true);
	ReadRegBool(hKey, _T(ID_RK_MIF_SCOPE), &mMethodInFile_ShowScope, true);
	ReadRegBool(hKey, _T(ID_RK_MIF_DEFINES), &mMethodInFile_ShowDefines, false);
	ReadRegBool(hKey, _T(ID_RK_MIF_PROPERTIES), &mMethodInFile_ShowProperties, true);
	ReadRegBool(hKey, _T(ID_RK_MIF_MEMBERS), &mMethodInFile_ShowMembers, false);
	ReadRegBool(hKey, _T(ID_RK_MIF_EVENTS), &mMethodInFile_ShowEvents, true);
	ReadRegBool(hKey, _T(ID_RK_SuppressListboxes), &mSuppressAllListboxes, defaultFalseExceptInCppBuilder);
	ReadRegBool(hKey, _T(ID_RK_FsisUseEditorSelection), &mFindSymbolInSolutionUsesEditorSelection, true);
	ReadRegBool(hKey, _T(ID_RK_SELECT_IMPLEMENTATION), &mSelectImplementation, true);
	ReadRegDword(hKey, _T(ID_RK_ReparseIfNeeded), (DWORD*)&mReparseIfNeeded, 0);
	ReadRegBool(hKey, _T(ID_RK_EnableSortLinesPrompt), &mEnableSortLinesPrompt, true);
	ReadRegBool(hKey, _T(ID_RK_UsePpl), &mUsePpl, true);
	ReadRegBool(hKey, _T(ID_RK_DisplayHashtagXrefs), &mDisplayHashtagXrefs, true);
	ReadRegBool(hKey, _T(ID_RK_ClearNonSourceFileMatches), &mClearNonSourceFileMatches, true);
	ReadRegBool(hKey, _T(ID_RK_OfisTooltips), &mOfisTooltips);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludeSolution), &mOfisIncludeSolution, true);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludePrivateSystem), &mOfisIncludePrivateSystem, true);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludeSystem), &mOfisIncludeSystem, true);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludeExternal), &mOfisIncludeExternal, true);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludeWindowList), &mOfisIncludeWindowList, false);
	ReadRegBool(hKey, _T(ID_RK_FsisTooltips), &mFsisTooltips);
	ReadRegBool(hKey, _T(ID_RK_BrowseMembersTooltips), &mBrowseMembersTooltips);
	ReadRegBool(hKey, _T(ID_RK_FsisExtraColumns), &mFsisExtraColumns, false);
#ifdef AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_AUTO_HIGHLIGHT_REFS), &mAutoHighlightRefs, true);
#else
	ReadRegBool(hKey, _T(ID_RK_AUTO_HIGHLIGHT_REFS), &mAutoHighlightRefs,
	            gShellAttr && gShellAttr->IsDevenv11OrHigher() ? false : true);
#endif
	ReadRegBool(hKey, _T(ID_RK_PARAM_IN_MIF), &mParamsInMethodsInFileList);
	ReadRegBool(hKey, _T(ID_RK_AUTO_HIGHLIGHT_REFS_THREADED), &mUseAutoHighlightRefsThread, false);
	if (mUseAutoHighlightRefsThread && gShellAttr->IsMsdev())
		mUseAutoHighlightRefsThread = false;

	if (hKey)
	{
		ReadRegBool(hKey, _T(ID_RK_OPTIMIZE_REMOTE_DESKTOP), &mOptimizeRemoteDesktop, false);
		if (mOptimizeRemoteDesktop)
		{
			// don't prompt if they've already go it turned on - old default was true
			// only new users will see the prompt
			SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PROMPTED_FOR_REMOTEDESKTOP, true);
		}
	}
	else if (Psettings)
	{
		// don't change value of mOptimizeRemoteDesktop while tests are running
		_ASSERTE(mOptimizeRemoteDesktop == Psettings->mOptimizeRemoteDesktop);

		// don't prompt when tests are running
		SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_PROMPTED_FOR_REMOTEDESKTOP, true);
	}

	if (RegValueExists(hKey, _T(ID_RK_MOUSE_CLICK_CMDS)))
	{
		DW8x16 dflt_val = 0;

		if (!gShellAttr->IsDevenv10OrHigher())
		{
			dflt_val.set((DWORD)VaMouseCmdBinding::ShiftRightClick,
			             (DWORD)VaMouseCmdAction::ContextMenuOld); // defaults to VA Context Menu (old)
		}

		ReadRegDword(hKey, _T(ID_RK_MOUSE_CLICK_CMDS), &mMouseClickCmds, dflt_val);
	}
	else
	{
		ReadRegDword(hKey, _T(ID_RK_CTRL_CLICK_GOTO), &mMouseClickCmds, 0);
#if defined(RAD_STUDIO)
		mMouseClickCmds.set((DWORD)VaMouseCmdBinding::ShiftRightClick, (DWORD)VaMouseCmdAction::ContextMenu);
#else
		if (!gShellAttr->IsDevenv10OrHigher())
		{
			bool shftRClickMenu = true;
			ReadRegBool(hKey, _T(ID_RK_CONTEXTMENUONSHIFT), &shftRClickMenu);
			if (shftRClickMenu)
			{
				mMouseClickCmds.set((DWORD)VaMouseCmdBinding::ShiftRightClick, (DWORD)VaMouseCmdAction::ContextMenuOld);
			}
		}
#endif
	}

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		DW8x16 dflt_wheel = 0;

		dflt_wheel.set((DWORD)VaMouseWheelCmdBinding::CtrlMouseWheel,
		               (DWORD)VaMouseWheelCmdAction::Zoom); // defaults to VS Zoom In/Out

		dflt_wheel.set((DWORD)VaMouseWheelCmdBinding::CtrlShiftMouseWheel,
		               (DWORD)VaMouseWheelCmdAction::Zoom); // defaults to VS Zoom In/Out

		ReadRegDword(hKey, _T(ID_RK_MOUSE_WHEEL_CMDS), &mMouseWheelCmds, dflt_wheel);
	}

	ReadRegBool(hKey, _T(ID_RK_FIX_SMART_PTR), &m_fixSmartPtrOp, true);
#ifdef AVR_STUDIO
	m_UseVASuggestionsInManagedCode = false;
#else
	ReadRegBool(hKey, _T(ID_RK_SUGGESTIONS_IN_MANAGED_CODE), &m_UseVASuggestionsInManagedCode, true);
#endif
	ReadRegBool(hKey, _T(ID_RK_EXTEND_COMMENT_ON_NEWLINE), &mExtendCommentOnNewline, false);
#ifdef AVR_STUDIO
	ReadRegBool(hKey, _T(ID_RK_MEMBERS_COMPLETE_ON_ANY), &mMembersCompleteOnAny, true);
#else
	if (gShellAttr->IsDevenv11OrHigher())
		mMembersCompleteOnAny = false;
	else
		ReadRegBool(hKey, _T(ID_RK_MEMBERS_COMPLETE_ON_ANY), &mMembersCompleteOnAny, true);
#endif
	int defaultVal = 1;
	if (gShellAttr->IsDevenv10OrHigher() || gShellAttr->IsCppBuilder())
		defaultVal = -2;
	ReadRegDword(hKey, _T(ID_RK_USE_THEMED_COMPLETIONBOX_SELECTION), (DWORD*)&m_alwaysUseThemedCompletionBoxSelection,
	             (DWORD)defaultVal);

	ReadRegDword(hKey, _T(ID_RK_MAX_SCREEN_REPARSE), (DWORD*)&mMaxScreenReparse, 300);
	ReadRegDword(hKey, _T(ID_RK_MAX_FILE_SIZE), (DWORD*)&mLargeFileSizeThreshold, 5000000);
	ReadRegDword(hKey, _T(ID_RK_ADDINCLUDE_STYLE), (DWORD*)&mAddIncludeStyle, 0);
	ReadRegDword(hKey, _T(ID_RK_LISTBOX_OVERWRITE_BEHAVIOR), (DWORD*)&mListboxOverwriteBehavior, lbob_default);
	ReadRegDword(hKey, _T(ID_RK_Smart_Ptr_Suggest_Mode), (DWORD*)&mSmartPtrSuggestModes, spsm_parens | spsm_openParen);
	ReadRegBool(hKey, _T(ID_RK_MIF_NAME_FILTER), &mMethodsInFileNameFilter);
	ReadRegBool(hKey, _T(ID_RK_CPP_OVERRIDE_KEYWORD), &mUseCppOverrideKeyword, gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder());
	ReadRegBool(hKey, _T(ID_RK_CPP_VIRTUAL_KEYWORD), &mUseCppVirtualKeyword, !mUseCppOverrideKeyword);
	ReadRegBool(hKey, _T(ID_RK_UNC_PATHS), &mAllowUncPaths);
	ReadRegBool(hKey, _T(ID_RK_DIRTY_FIND_REFS_NAV), &mAlternateDirtyRefsNavBehavior, false);
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
	mCacheProjectInfo = false;
#else
	ReadRegBool(hKey, _T(ID_RK_PROJECT_INFO_CACHE), &mCacheProjectInfo);
#endif
	ReadRegBool(hKey, _T(ID_RK_DIALOGS_STICK_TO_MONITOR), &mDialogsStickToMonitor, true);
	ReadRegBool(hKey, _T(ID_RK_DIALOGS_FIT_INTO_SCREEN), &mDialogsFitIntoScreen, true);
	ReadRegBool(hKey, _T(ID_RK_TRACK_CARET_VISIBILITY), &mTrackCaretVisibility, true);
	ReadRegBool(hKey, _T(ID_RK_COLOR_BUILD_OUTPUT), &mColorBuildOutput, true);
	ReadRegDword(hKey, _T(ID_RK_COLOR_BUILD_OUTPUT_CUSTOM_BG), &mColorBuildOutput_CustomBg, DWORD(-1));
	ReadRegString(hKey, _T(ID_RK_FUNCTION_CALL_PAREN_STRING), mFunctionCallParens, "()", 7);
	ReadRegDword(hKey, _T(ID_RK_ListBoxHeightInItems), (DWORD*)&mListBoxHeightInItems,
	             gShellAttr->IsDevenv10OrHigher() ? 9u : 10u);
	ReadRegDword(hKey, _T(ID_RK_InitialSuggestionBoxHeightInItems), (DWORD*)&mInitialSuggestionBoxHeightInItems, 5);
	ReadRegBool(hKey, _T(ID_RK_ResizeSuggestionListOnUpDown), &mResizeSuggestionListOnUpDown, true);
	ReadRegBool(hKey, _T(ID_RK_DismissSuggestionListOnUpDown), &mDismissSuggestionListOnUpDown, false);
	ReadRegBool(hKey, _T(ID_RK_EnableFilterStartEndTokens), &mEnableFilterStartEndTokens, true);
	ReadRegBool(hKey, _T(ID_RK_UseGotoRelatedForGoButton), &mUseGotoRelatedForGoButton, false);
	ReadRegDword(hKey, _T(ID_RK_LinesBetweenMethods), &mLinesBetweenMethods, 1);
	ReadRegBool(hKey, _T(ID_RK_DisableFindSimilarLocation), &mFindSimilarLocation, true);
	ReadRegBool(hKey, _T(ID_RK_HashtagsGroupByFile), &mHashtagsGroupByFile, false);
	ReadRegDword(hKey, _T(ID_RK_HashtagsMinimumTagLength), &mMinimumHashtagLength, 3);
	ReadRegBool(hKey, _T(ID_RK_InsertOpenBraceOnNewLine), &mInsertOpenBraceOnNewLine, true);
	ReadRegBool(hKey, _T(ID_RK_HashtagsIgnoreHexAlphaTags), &mIgnoreHexAlphaHashtags, true);
	ReadRegBool(hKey, _T(ID_RK_SurroundWithSnippetOnChar), &mEnableSurroundWithSnippetOnChar, true);
	ReadRegBool(hKey, _T(ID_RK_SurroundWithSnippetOnCharIgnoreWhitespace), &mSurroundWithSnippetOnCharIgnoreWhitespace,
	            true);
	ReadRegBool(hKey, _T(ID_RK_EdMouseButtonHooks), &mEnableEdMouseButtonHooks, defaultTrueExceptInCppBuilder);
	ReadRegBool(hKey, _T(ID_RK_HashtagsAllowHyphens), &mHashtagsAllowHypens, false);
	ReadRegBool(hKey, _T(ID_RK_AddMissingDefaultSwitchCase), &mAddMissingDefaultSwitchCase, true);
	ReadRegBool(hKey, _T(ID_RK_MoveTemplatesToSourceInTwoSteps), &mTemplateMoveToSourceInTwoSteps, true);
	ReadRegBool(hKey, _T(ID_RK_AlwaysDisplayGlobalMenuInHashtagMenu), &mHashTagsMenuDisplayGlobalAlways, true);
	ReadRegBool(hKey, _T(ID_RK_SparseSystemIncludeLoad), &mSparseSysLoad, false);
	ReadRegBool(hKey, _T(ID_RK_VerifyDbOnLoad), &mVerifyDbOnLoad, true);
	ReadRegBool(hKey, _T(ID_RK_NavBarContextDisplayScopeSingleLevel), &mNavBarContext_DisplaySingleScope, false);
	ReadRegBool(hKey, _T(ID_RK_UseOldUncommentBehavior), &mOldUncommentBehavior, false);
	ReadRegBool(hKey, _T(ID_RK_SelectRecentItemsInNav), &mSelectRecentItemsInNavigationDialogs, true);

	char tmp[4] = "";
	ReadRegString(hKey, _T(ID_RK_DEFAULTINCLUDEDELIMITER), tmp, "\\", 4);
	mDefaultAddIncludeDelimiter = (WCHAR)tmp[0];

	if (gShellAttr->IsDevenv10OrHigher())
	{
		mUseMarkerApi = true;
		sOkToSaveVsMarkerValue = false;
	}
	else if (gShellAttr->IsDevenv())
	{
		sOkToSaveVsMarkerValue = true;
		ReadRegBool(hKey, _T(ID_RK_USE_MARKER_API), &mUseMarkerApi, true);

		if (gShellAttr->IsDevenv8())
		{
			FileVersionInfo fvi;
			if (fvi.QueryFile(L"DevEnv.exe", FALSE))
			{
				const CString tmp2(fvi.GetFileVerString());
				if (tmp2 == "8.0.50727.42")
				{
					// [case: 27579] don't use markers in original vs2005 (without SP)
					sOkToSaveVsMarkerValue = false;
					mUseMarkerApi = false;
				}
			}
		}
	}
	else
	{
		sOkToSaveVsMarkerValue = mUseMarkerApi = false;
	}

	ReadRegDword(hKey, _T("IVMOptions"), &mImplementVirtualMethodsOptions, 0);
	if (gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 24605]
		ReadRegBool(hKey, _T(ID_RK_EditUserInputInEditor), &mEditUserInputFieldsInEditor, true);

		// [case: 103675]
		// [case: 149451] Code Inspection enabled by default
		mCodeInspection = false; // #codeInspectionSetting default // [case: 149451] todo: disabled to backout functionality; set to true
		HKEY hkCodeInsp;
		LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, ID_RK_APP + "\\CodeInspections", 0, KEY_QUERY_VALUE, &hkCodeInsp);
		if (ERROR_SUCCESS == err)
		{
			DWORD val = !!mCodeInspection;
			ReadRegDword(hkCodeInsp, _T(ID_RK_EnableCodeInspection), &val, val);
			RegCloseKey(hkCodeInsp);
			// #codeInspectionSetting state loaded in va_x
			mCodeInspection = !!val;
		}
	}
	else
	{
		mEditUserInputFieldsInEditor = false;
		mCodeInspection = false;
	}

	ReadRegDword(hKey, _T(ID_RK_SmartSelectPeekDuration), &mSmartSelectPeekDuration, 5000);
	ReadRegBool(hKey, _T(ID_RK_SmartSelectEnableGranularStart), &mSmartSelectEnableGranularStart, true);
	ReadRegBool(hKey, _T(ID_RK_SmartSelectEnableWordStart), &mSmartSelectEnableWordStart, true);
	ReadRegBool(hKey, _T(ID_RK_SmartSelectSplitWordByCase), &mSmartSelectSplitWordByCase, false);
	ReadRegBool(hKey, _T(ID_RK_SmartSelectSplitWordByUnderscore), &mSmartSelectSplitWordByUnderscore, false);
	ReadRegDword(hKey, _T(ID_RK_GotoOverloadResolutionMode), (DWORD*)&mGotoOverloadResolutionMode, 2);
	ReadRegDword(hKey, _T(ID_RK_GotoRelatedOverloadResolutionMode), (DWORD*)&mGotoRelatedOverloadResolutionMode, 2);
	ReadRegString(hKey, _T(ID_RK_CreationTemplateFunctionNames), mCreationTemplateFunctions, _T(""), MAX_PATH);
	ReadRegBool(hKey, _T(ID_RK_GotoRelatedParameterTrimming), &mGotoRelatedParameterTrimming, true);
	ReadRegBool(hKey, _T(ID_RK_EnableFindRefsFlagCreation), &mFindRefsFlagCreation, true);
	ReadRegBool(hKey, _T(ID_RK_IncludeDirectiveCompletionLists), &mIncludeDirectiveCompletionLists, true);
	ReadRegBool(hKey, _T(ID_RK_ForceUseOfOldVcProjectApi), &mForceUseOldVcProjectApi, false);
	ReadRegBool(hKey, _T(ID_RK_EnhanceMacroParsing), &mEnhanceMacroParsing, false);
	ReadRegBool(hKey, _T(ID_RK_ForceProgramFilesDirSystem), &mForceProgramFilesDirsSystem, true);
	ReadRegBool(hKey, _T(ID_RK_OfisAugmentSolution), &mOfisAugmentSolution, false);
	ReadRegBool(hKey, _T(ID_RK_AugmentHiddenExtensions), &mAugmentHiddenExtensions, true);
	ReadRegBool(hKey, _T(ID_RK_OfisIncludeHiddenExtensions), &mOfisIncludeHiddenExtensions, false);
	ReadRegBool(hKey, _T(ID_RK_ForceVs2017ProjectSync), &mForceVs2017ProjectSync, false);
	ReadRegBool(hKey, _T(ID_RK_EnableMixedDpiScalingWorkaround), &mEnableMixedDpiScalingWorkaround, true);
	ReadRegBool(hKey, _T(ID_RK_ForceVcPkgInclude), &mForceVcPkgInclude, gShellAttr && gShellAttr->IsDevenv15OrHigher());
	ReadRegBool(hKey, _T(ID_RK_EnableVcProjectSync), &mEnableVcProjectSync, true);
	ReadRegDword(hKey, _T(ID_RK_WarnOnLoadOfPathWithReversedSlashes), (DWORD*)&mWarnOnLoadOfPathWithReversedSlashes, 1);
	ReadRegDword(hKey, _T(ID_RK_JavaDocStyle), &mJavaDocStyle, 1);
	ReadRegBool(hKey, _T(ID_RK_AvoidDuplicatingTypeWithConvert), &mUseAutoWithConvertRefactor, true);
	ReadRegBool(hKey, _T(ID_RK_ALLOW_SNIPPETS_IN_UNREAL_MARKUP), &mAllowSnippetsInUnrealMarkup, false);
	ReadRegBool(hKey, _T(ID_RK_ALWAYS_DISPLAY_UNREAL_SYMBOLS_IN_ITALICS), &mAlwaysDisplayUnrealSymbolsInItalics, true);
	ReadRegDword(hKey, _T(ID_RK_INDEX_PLUGINS), &mIndexPlugins, 1);
	ReadRegBool(hKey, _T(ID_RK_INDEX_GENERATED_CODE), &mIndexGeneratedCode, false);
	ReadRegBool(hKey, _T(ID_RK_DISABLE_UE_AUTOFORMAT_ON_PASTE), &mDisableUeAutoforatOnPaste, true);
	ReadRegBool(hKey, _T(ID_RK_USE_GIT_ROOT_FOR_AUGMENT_SOLUTION), &mUseGitRootForAugmentSolution, true);
	ReadRegDword(hKey, _T(ID_RK_ConvertToPointerType), &mConvertToPointerType, 0);
	ReadRegDword(hKey, _T(ID_RK_FileLoadCodePage), &mFileLoadCodePageBehavior, FLCP_AutoDetect);
	ReadRegString(hKey, _T(ID_RK_ConvertCustomPointerName), mConvertCustomPtrName, _T(""), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_ConvertCustomMakeName), mConvertCustomMakeName, _T(""), MAX_PATH);
	ReadRegString(hKey, _T(ID_RK_MINIHELP_INFO), m_minihelpInfo, _T(""), sizeof(m_minihelpInfo)/sizeof(m_minihelpInfo[0]));


	if (gShellAttr->IsDevenv12OrHigher())
	{
		ReadRegDword(hKey, _T(ID_RK_UnrealEngineAutoDetect), &mUnrealEngineAutoDetect, 1);
	}
	else
	{
		ReadRegDword(hKey, _T(ID_RK_UnrealEngineAutoDetect), &mUnrealEngineAutoDetect, 0);
	}

	ReadRegBool(hKey, _T(ID_RK_OptionsHelp), &mOptionsHelp, true);
	ReadRegBool(hKey, _T(ID_RK_ColorInsideUnrealMarkup), &mColorInsideUnrealMarkup, false);
	ReadRegBool(hKey, _T(ID_RK_EnableShaderSupport), &mEnableShaderSupport, true);
	ReadRegBool(hKey, _T(ID_RK_EnableCudaSupport), &mEnableCudaSupport, false);
	ReadRegBool(hKey, _T(ID_RK_AddIncludeUnrealUseQuotation), &mAddIncludeUnrealUseQuotation, true);
	ReadRegBool(hKey, _T(ID_RK_UseSlashForIncludePaths), &mUseSlashForIncludePaths, false);
	ReadRegBool(hKey, _T(ID_RK_KeepAutoHighlightOnWS), &mKeepAutoHighlightOnWS, false);
	ReadRegBool(hKey, _T(ID_RK_FilterGeneratedSourceFiles), &mFilterGeneratedSourceFiles);
	ReadRegString(hKey, ID_RK_OfisPersistentFilter, mOfisPersistentFilter, L"", MAX_PATH);
	ReadRegBool(hKey, _T(ID_RK_OfisDisplayPersistentFilter), &mOfisDisplayPersistentFilter, false);
	ReadRegBool(hKey, _T(ID_RK_OfisApplyPersistentFilter), &mOfisApplyPersistentFilter, true);
	ReadRegBool(hKey, _T(ID_RK_AddIncludeSkipFirstFileLevelComment), &mAddIncludeSkipFirstFileLevelComment, true);

	if (gShellAttr->IsDevenv10OrHigher())
	{
		ReadRegDword(hKey, _T(ID_RK_WatermarkProperties), &mWatermarkProps, 0x00000040);
	}

	mThirdPartyRegex[0] = 0;
	ReadRegString(hKey, CA2W(ID_RK_ThirdPartyRegex), mThirdPartyRegex, L"(third|3rd)[ ]*party",
	              _countof(mThirdPartyRegex));

	ReadRegDword(hKey, _T(ID_RK_InsertPathMode), &mInsertPathMode, IPM_RelativeToFile);
	ReadRegBool(hKey, _T(ID_RK_InsertPathFwdSlash), &mInsertPathFwdSlash, false);
	ReadRegBool(hKey, _T(ID_RK_ListboxCompletionRequiresSelection), &mListboxCompletionRequiresSelection, false);
	ReadRegBool(hKey, _T(ID_RK_AllowSuggestAfterTab), &mAllowSuggestAfterTab, false);
	ReadRegBool(hKey, _T(ID_RK_WebEditorPmaFail), &mWebEditorPmaFail, false);
	ReadRegBool(hKey, _T(ID_RK_CloseHashtagToolwindowOnGoto), &mCloseHashtagToolWindowOnGoto, false);
	ReadRegDword(hKey, _T(ID_RK_RestrictFilesOpenedDuringRefactor), &mRestrictFilesOpenedDuringRefactor, 50);
	ReadRegBool(hKey, _T(ID_RK_EnableGotoJumpsToImpl), &mEnableJumpToImpl, false);
	ReadRegBool(hKey, _T(ID_RK_EnableGotoFilterWIthOverloads), &mEnableFilterWithOverloads, true);

	if (gShellAttr->IsDevenv16OrHigher())
		ReadRegDword(hKey, _T(ID_RK_DPIAwareness), &mDPIAwareness, DPI_AWARENESS_PER_MONITOR_AWARE);
	else
		mDPIAwareness = 0; // don't switch

	ReadRegDword(hKey, _T(ID_RK_ModifyExpressionFlags), &mModifyExprFlags, ModExpr_Default);

	ReadRegBool(hKey, _T(ID_RK_EnhanceVAOutlineMacroSupport), &mEnhanceVAOutlineMacroSupport, true);
	ReadRegBool(hKey, _T(ID_RK_RestrictGotoMemberToProject), &mRestrictGotoMemberToProject, false);
	ReadRegDword(hKey, _T(ID_RK_RebuildSolutionMinimumModCnt), &mRebuildSolutionMinimumModPercent, 50);
	if ((int)mRebuildSolutionMinimumModPercent < 0 || mRebuildSolutionMinimumModPercent == UINT_MAX)
		mRebuildSolutionMinimumModPercent = 0;
	else if (mRebuildSolutionMinimumModPercent > 100)
		mRebuildSolutionMinimumModPercent = 50;

	ReadRegBool(hKey, _T(ID_RK_FORCE_EXTERNAL_INCLUDE_DIRECTORIES), &mForceExternalIncludeDirectories, false);
	ReadRegBool(hKey, _T(ID_RK_RESPECT_VS_CODE_EXCLUDED_FILES), &mRespectVsCodeExcludedFiles, false);
	ReadRegBool(hKey, _T(ID_RK_DoOldStyleMainMenu), &mDoOldStyleMainMenu, false);
	ReadRegDword(hKey, _T(ID_RK_AddIncludeSortedMinProbability), &mAddIncludeSortedMinProbability, 1);
	ReadRegDword(hKey, _T(ID_RK_RenewNotificationDays), &mRenewNotificationDays, 7);
	//ReadRegDword(hKey, _T(ID_RK_FirstRunDialogStatus), &mFirstRunDialogStatus, 0); // [case: 149451] todo: disabled to backout functionality

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	ReadRegDword(hKey, _T(ID_RK_ENC_FIELD_TYPE), (DWORD*)&mEncFieldTypeCppB, tagSettings::ceft_property);
	ReadRegBool(hKey, _T(ID_RK_ENC_FIELD_READ_FIELD), &mEnableEncFieldReadFieldCppB, false);
	ReadRegBool(hKey, _T(ID_RK_ENC_FIELD_READ_VIRTUAL), &mEnableEncFieldReadVirtualCppB, false);
	ReadRegBool(hKey, _T(ID_RK_ENC_FIELD_WRITE_FIELD), &mEnableEncFieldWriteFieldCppB, false);
	ReadRegBool(hKey, _T(ID_RK_ENC_FIELD_WRITE_VIRTUAL), &mEnableEncFieldWriteVirtualCppB, false);
	ReadRegDword(hKey, _T(ID_RK_ENC_FIELD_MOVE_PROPERTY), (DWORD*)&mEncFieldMovePropertyCppB,
	             tagSettings::cpv_published);
#endif
}

CSettings::~CSettings()
{
	Commit();
}

const WCHAR* GetMainDllName()
{
	static const WCHAR* kVaxName = IDS_VAX_DLLW;
#if !defined(VAX_CODEGRAPH)
#if defined(VA_CPPUNIT)
	if (gShellAttr)
		return gShellAttr->GetExeName();
#endif
#if defined(SEAN) && !defined(NDEBUG)
	static const WCHAR* kVaxModifiableCopy = L"VA_Xd.dll";
	if (::IsDebuggerPresent())
	{
		HINSTANCE hMod = GetModuleHandleW(kVaxModifiableCopy);
		if (hMod)
			return kVaxModifiableCopy;
	}
#endif
#endif // !VAX_CODEGRAPH

	return kVaxName;
}

COLORSTRUCT g_colors[] = {{
                              COLORSTRUCT::useVAColor,
                              {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                              C_TypoError,
                              {_T("TypoError"), 0x000000ff, 0x000000ff} // Undefined Symbols/Spelling Errors
                          },
                          {COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_ContextError,
                           {_T("ContextError"), 0x0000ff00, 0x00ffffff}},
                          {// ID_RK_TEXTSELECTION - white fore, black back
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Text"), {0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x19, 0x00}},
                           C_ColumnIndicator,
                           {_T("ColumnIndicator"), 0x00000000, 0x00ffffff}},
                          {// ID_RK_TEXT - black fore, white back
                           // using text bg color for bold braces -jer
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Text"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Text,
                           {_T("Text"), 0x00000000, 0x00ffffff}},
                          {// ID_RK_TEXT - blue fore, white back
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Keyword"), {0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Keyword,
                           {_T("Keyword"), 0x00ff0000, 0x00ffffff}},
                          {// ID_RK_OPERATOR - black
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Operator"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x11, 0x00, 0x11, 0x00}},
                           C_MatchedBrace,
                           {_T("Matched Brace/Paren"), 0x00000000, 0x00ffffff}},
                          {// ID_RK_OPERATOR - red
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_MismatchedBrace,
                           {_T("Mismatched Brace/Paren"), 0x000000ff, 0x00ffffff}},
                          {// ID_RK_TEXT - blue fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Type,
                           {_T("Classes and Types"), 0x00ff0000, 0x00ffffff}},
                          {// purple fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x88, 0x00, 0x88, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Macro,
                           {_T("Macros"), 0x00A000A0, 0x00ffffff}},
                          {// navy blue fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x80, 0x80, 0x80, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Var,
                           {_T("Variable"), 0x00800000, 0x00ffffff}},
                          {// brown fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x88, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Function,
                           {_T("Functions"), 0x00000088, 0x00ffffff}},
                          {// used in rtfcopy and colorprinting
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Comment"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Comment,
                           {_T("Comment"), 0x00009900, 0x00ffffff}},
                          {// used in rtfcopy and colorprinting
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Number"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Number,
                           {_T("Number"), 0x00000000, 0x00ffffff}},
                          {// used in rtfcopy and colorprinting
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("String"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_String,
                           {_T("String"), 0x00770000, 0x00ffffff}},
                          {// used in rtfcopy and colorprinting
                           COLORSTRUCT::useCurrentUserColor,
                           {_T("Operator"), {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Operator,
                           {_T("Operator"), 0x00000000, 0x00ffffff}},
                          {// brown fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x88, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Undefined,
                           {_T("Undefined"), 0x000000ff, 0x00ffffff}},
                          // Reference highlighting
                          {// used in rtfcopy and colorprinting
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_Reference,
                           {_T("Reference"), 0x00000000, 0x00ffffe0}},
                          {// used in FindReferences
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_ReferenceAssign,
                           {_T("ReferenceAssign"), 0x00000000, 0x00e1e4ff}},
                          {COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_FindResultsHighlight,
                           {_T("FindResultsHighlight"), 0x00000000, 0x00b7ffff}},
                          {COLORSTRUCT::useVAColor,
                           {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                           C_HighlightCurrentLine,
                           {_T("HighlightCurrentLine"), 0x00C0C0C0, 0x00ebebeb}},
                          {// dark slate fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x2f, 0x4f, 0x4f, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_EnumMember,
                           {_T("EnumMembers"), 0x004f4f2f, 0x00ffffff}},
                          {// blue fore, white back
                           COLORSTRUCT::useVAColor,
                           {_T(""), {0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x13, 0x01, 0xb3, 0x00}},
                           C_Namespace,
                           {_T("Namespaces"), 0x00ff0000, 0x00ffffff}},
                          {COLORSTRUCT::sentinelEntry, {_T(""), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, C_NULL}};
