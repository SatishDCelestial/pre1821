#ifdef _MSC_VER
#pragma once
#endif

// general strings
#if defined(VAX_CODEGRAPH)
#define IDS_APPNAME "Spaghetti"
#else
#define IDS_APPNAME "Visual Assist"
#endif

#define IDS_MSDEV_APP_SEARCH5 "Microsoft Developer Studio"
#define IDS_MSDEV_APP_SEARCH6 "Microsoft Visual C++"
#define IDS_EVC_APP_SEARCH3 "Microsoft eMbedded Visual C++"
#define IDS_MSDEV_EXE "MSDev.exe"
#define IDS_EVC_EXE "evc.exe"
#define IDS_DEVSHL_DLL "Devshl.dll"
#define IDS_DEVEDIT_PKG "Devedit.pkg"
#define IDS_APP_VER_SPEC "VisualAssist.DSAddIn.1"
#define IDS_DBG_MSG_CAPTION "VAssist Debug Message"
#define IDS_APP_VER_INDPNDNT "Visual Assist Developer Studio Add-in"

// HKLM & HKCU
// do not put values in ID_RK_WT_KEY (exceptions for: "altDir", "UserDataDir" and "logging")
#ifdef AVR_STUDIO
#define ID_RK_WT_KEY "Software\\Atmel\\Whole Tomato"
#else
#define ID_RK_WT_KEY "Software\\Whole Tomato"
#endif

// base location for settings shared across IDEs
// DO NOT USE this in VA_X.dll -- use ID_RK_APP instead (except for globals 
// that need the string before gShellAttr is created)
// this is only here for other dlls
#define ID_RK_APP_KEY                                                                                                  \
	ID_RK_WT_KEY "\\Visual Assist X" // keep old name until we decide to migrate settings to new app name -- 

// legacy - do not use these in new code
#define ID_RK_APP_VA ID_RK_WT_KEY "\\Visual Assist"    // legacy
#define ID_RK_APP_VA6 ID_RK_WT_KEY "\\Visual Assist 6" // legacy
#define ID_RK_APP_VANET ID_RK_WT_KEY "\\VAnet"         // legacy

// VA HKCU values - use either ID_RK_WT_KEY or ID_RK_APP
#define ID_RK_DBDIR "Data Dir"                    // string
#define ID_RK_LOGGING "Logging"                   // dword - log mask
#define ID_RK_LOGFILE "Logfile"                   // string
#if 0                                             // defined(VAX_CODEGRAPH)
#define ID_RK_USERNAME "UserNameCG"               // string
#define ID_RK_USERKEY "UserKeyCG"                 // string
#define ID_RK_TRIALEXPIRATION "TrialExpirationCG" // string
#else
#define ID_RK_USERNAME "UserName"               // string
#define ID_RK_USERKEY "UserKey"                 // string
#define ID_RK_TRIALEXPIRATION "TrialExpiration" // string
#endif
#define ID_RK_ELQ_TRIAL "UserInfo"				// DWORD

#define ID_RK_SYSTEMINCLUDE "SystemInclude"         // string
#define ID_RK_ADDITIONALINCLUDE "AdditionalInclude" // string
#define ID_RK_GOTOSRCDIRS "GotoSourceDirs"          // string
#define ID_RK_PLATFORM "Platform"                   // string
#define ID_RK_ALLOW_DDE "AllowDDE"                  // bool
#define ID_RK_LOCALECHANGE "UseSystemLocale"        // DWORD

// VA HKCU values - use ID_RK_APP
#define ID_RK_ENABLED "Enabled"                               // binary bool - 1 byte
#define ID_RK_MINIHELPPLACEMENT "MiniHelpAtTop"               // bool
#define ID_RK_ONESTEPHELP "OneStepHelp"                       // bool
#define ID_RK_CORRECTCASE "CorrectCase"                       // bool
#define ID_RK_ENHANCEDFORMAT "EnhancedFormat"                 // bool
#define ID_RK_BOLDBRACEMATCH "BoldBraceMatching"              // bool
#define ID_RK_AUTOMATCH "AutoMatch"                           // bool
#define ID_RK_ASCIIW32API "AsciiWin32Defs"                    // bool
#define ID_RK_AUTOSUGGEST "MakeSuggestions"                   // bool
#define ID_RK_VAWORKSPACE "WorkspaceTab"                      // bool
#define ID_RK_TABINVOKESINTELLISENSE "TabInvokesIntellisense" // bool
#define ID_RK_MINIHELPFONTSIZE "minihelpFontSize"             // DWORD
#define ID_RK_FNPARENGAP "FnParenGap"                         // DWORD
#define ID_RK_RENAMENCBS "RenameSysNCBs"                      // bool
#define ID_RK_SMARTPASTE "smartPaste"                         // bool
#define ID_RK_WORKSPACEITEMCNT "WorkspaceItemCount"           // DWORD
#define ID_RK_CLIPBOARDITEMCNT "ClipboardItemCount"           // DWORD
#define ID_RK_IFDEFSTR "#ifdef string"                        // string
#define ID_RK_MANUALLOADDONE "ManualLoadDone"                 // DWORD
#define ID_RK_AUTOBACKUP "AutoBackup"                         // bool
#define ID_RK_SKIPLOCALMACROS "SkipLocalMacros"               // bool
#define ID_RK_MOUSEOVERS "MouseOvers"                         // bool
#define ID_RK_RESOURCEALARM "ResourceAlarm"                   // bool
#define ID_RK_MULTIPLEPROJSUPPORT "MultipleProjSupport"       // bool
#define ID_RK_KEEPBOOKMARKS "KeepBookmarks"                   // bool
#define ID_RK_CONTEXTMENUONSHIFT "ShiftRtClickContextMenu"    // bool
#define ID_RK_CYCLETHREAD "OnFocusCycleThread"                // bool
#define ID_RK_MODTHREAD "ModThread"                           // bool
#define ID_RK_PARSEIMPORTS "ParseImports"                     // bool
#define ID_RK_AGGRESSIVEMATCH "AggressiveFileMatch"           // bool
#define ID_RK_TESTFLAG "testFlag"                             // bool
#define ID_RK_FIXPTROP "FixPtrOperators"                      // bool
#define ID_RK_SHOWSCOPE "ShowScope"                           // bool
#define ID_RK_IMPORTTIMEOUT "ImportTimeout"                   // DWORD
#define ID_RK_MACROPARSE1 "LimitMacroParsing"                 // bool
#define ID_RK_MACROPARSE2 "LimitSysMacroParsingV2"            // bool
#define ID_RK_RAPIDFIRE "rapidFire"                           // bool
#define ID_RK_CODETEMPTOOL "CodeTemplateTooltips"             // bool
#define ID_RK_AUTOM_ "AutoM_Insert"                           // bool
#define ID_RK_UNDERLINETYPOS "UnderlineTypos"                 // bool
#define ID_RK_CONTEXTPREFIX "ContextPrefix"                   // bool
#define ID_RK_BRACEMISMATCH "ShowBraceMismatches"             // bool
#define ID_RK_MARGINMENU "MarginContextMenu"                  // bool
#define ID_RK_COLINDICATOR "ColumnIndicatorDisplay"           // bool
#define ID_RK_COLINDICATORCOLS "ColumnIndicatorColumn"        // DWORD
#define ID_RK_AUTOCOMPLETE "EnableAutoComplete"               // bool		(also used by installer dll)
#define ID_RK_PARAMINFO "EnableParameterHelp"                 // bool		(also used by installer dll)
#define ID_RK_AUTOCOMMENTS "EnableAutoComments"               // bool		(also used by installer dll)
#define ID_RK_QUICKINFO "EnableQuickInfo"                     // DWORD	(used only by installer dll)
#define IDS_DEF_IFDEFSTR "_DEBUG"
#define ID_RK_CONTEXTWIDTH "ContextWindowWidth"                                 // DWORD
#define ID_RK_RTFCOPY "RTFCopy"                                                 // bool
#define ID_RK_COLORPRINTING "ColorPrinting"                                     // bool
#define ID_RK_CATCHALL "CatchAll"                                               // bool
#define ID_RK_PROMPTED_FOR_REMOTEDESKTOP "PromptedForRemoteDesktopOptimization" // bool
#define ID_RK_SHAREDSNIPPETSDIR "SharedSnippetsDir"                             // string

// HKCU file type extensions
#ifdef AVR_STUDIO
#define ID_RK_EXTHDR "ExtHeaderAvr" // string
#define ID_RK_EXTSRC "ExtSourceAvr" // string
#else
#define ID_RK_EXTHDR "ExtHeader"                     // string
#define ID_RK_EXTSRC "ExtSource"                     // string
#define ID_RK_EXTSRC_ISUPDATED "ExtSource_IsUpdated" // bool
#endif
#define ID_RK_EXTRES "ExtResource" // string
#define ID_RK_EXTBIN "ExtBinary3"  // string
#define ID_RK_EXTIDL "ExtIdl"      // string
#define ID_RK_EXTJAV "ExtJava"     // string

#define IDS_EXTHDR ".h;.hh;.hpp;.hxx;.ipp;.tlh;.inl;.p;.rh;.dh;.ih;.ph;.hm;"
#ifdef AVR_STUDIO
#define IDS_EXTSRC ".c;.cpp;.cc;.cxx;.tli;.ino;.pde;"
#else
#define IDS_EXTSRC ".c;.cpp;.cc;.cxx;.tli;"
#define IDS_EXTSRC_APPEND ".ixx;.cppm;"
#endif
#define IDS_EXTRES ".rc;.rc2;"
#define IDS_EXTBIN ".exe;.dll;.obj;.tlb;.pkg;.ocx;.olb;.netmodule;.winmd;"
#define IDS_EXTIDL ".idl;.odl;"
#define IDS_EXTJAV ".jav;.java;"

// HKCU & HKLM used by install
#define ID_RK_MSDEV "SOFTWARE\\Microsoft\\DevStudio\\"                      // append 5.0 or 6.0
#define ID_RK_VSTUDIO "SOFTWARE\\Microsoft\\VisualStudio\\"                 // append 6.0
#define ID_RK_VSTUDIO_SETUP "SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Setup" // prepend ID_RK_VSTUDIO +6.0
#define ID_RK_EVC_SETUP "Software\\Microsoft\\CEStudio\\3.0\\Setup"
#define ID_RK_MSDEV_DIR "SOFTWARE\\Microsoft\\DevStudio\\5.0\\Directories"
#define ID_RK_MSDEV_DIR6                                                                                               \
	"SOFTWARE\\Microsoft\\DevStudio\\6.0\\Directories" // HKCU only - last resort "Install Dirs"="J:\\PROGRAM
	                                                   // FILES\\MICROSOFT VISUAL STUDIO\\COMMON\\MSDEV98\\BIN"
#define ID_RK_EVC_DIR "Software\\Microsoft\\CEStudio\\3.0\\evc\\Directories"
#define ID_RK_MSDEV_DIR2                                                                                               \
	"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msdev.exe" // in case ID_RK_MSDEV doesn't exist
#define ID_RK_MSDEV_INST "Install Dirs"                                  // i:\dev\sharedide\bin
#define ID_RK_VSTUDIO_COMMON "VsCommonDir"  // value name for ID_RK_VSTUDIO_SETUP key j:\VStudio
#define ID_RK_CESTUDIO_COMMON "CECommonDir" // C:\\Program Files\\Microsoft eMbedded Tools\\Common
#define IDS_VC6_DIR "\\MSDev98\\bin\\"      // append to ID_RK_VSTUDIO_COMMON value
#define IDS_EVC3_DIR "\\EVC\\bin\\"         // append to ID_RK_CESTUDIO_COMMON value
#define ID_RK_EVC30 "SOFTWARE\\Microsoft\\CEStudio\\3.0\\evc"
#define ID_RK_EVC40 "SOFTWARE\\Microsoft\\CEStudio\\4.0\\evc"
#define ID_RK_EVCPB "SOFTWARE\\Microsoft\\Platform Builder\\4.0"
#define ID_RK_PLATFORMBUILDER "SOFTWARE\\Microsoft\\Platform Builder\\4.00"

// HKCU to autoload the addin - both are strings
#define ID_RK_MSDEV_ADDIN                                                                                              \
	"\\AddIns" // prepend ID_RK_MSDEV + 5.0/6.0 and then add to IDS_APP_VER_SPEC \\AddIn.DSAddIn.1"	// default @="1"
#define ID_RK_MSDEV_ADDIN_FILE "Filename" // "I:\\Dev\\SharedIDE\\AddIns\\VAssist.dll"
#define ID_RK_MSDEV_ADDIN_DESC                                                                                         \
	"Description" // "Adds Visual Assist to get cool new functionality from Developer Studio."
#define ID_RK_MSDEV_ADDIN_NAME "DisplayName" // "Visual Assist Add-In"

// include directories
// HKCU
#define ID_RK_MSDEV_INC_KEY                                                                                            \
	"\\Build System\\Components\\Platforms\\" // append (Win32 (x86)\\Directories) and prepend ID_RK_MSDEV + 5.0/6.0
#define IDS_DEF_PLATFORM "Win32 (x86)"
#define IDS_PLATFORM_DIR "\\Directories"
// HKLM & HKCU
#define ID_RK_MSDEV_INC_VALUE "Include Dirs"
#define ID_RK_MSDEV_PATH_VALUE "Path Dirs"
#define ID_RK_MSDEV_LIB_VALUE "Library Dirs"
#define ID_RK_MSDEV_SRC_VALUE "Source Dirs"
#define ID_RK_HKCU_ENV_KEY "Environment"                                                      // include
#define ID_RK_HKLM_ENV_KEY "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" // include

// HKCU editor options
#define ID_RK_MSDEV_TEXTED "\\Text Editor" // prepend ID_RK_MSDEV + 5.0/6.0
// HKCU Compatibility options
#define ID_RK_NOSOLBACKSPACE "NoBackspaceAtBOL"
// HKCU tab options
#define ID_RK_MSDEV_C "\\Text Editor\\Tabs/Language Settings\\C/C++"   // prepend ID_RK_MSDEV + 5.0/6.0
#define ID_RK_MSDEV_JAVA "\\Text Editor\\Tabs/Language Settings\\Java" // prepend ID_RK_MSDEV + 5.0/6.0
#define ID_RK_TABSIZE "TabSize"

// HKCU format keys
#define ID_RK_MSDEV_FORMAT "\\Format\\Source Window" // prepend ID_RK_MSDEV + 5.0/6.0
#define ID_RK_FONTFACE L"FontFace"
#define ID_RK_FONTSIZE "FontSize"


// Shared registry keys (used in managed settings as well)
#define ID_RK_SHADER_EXTS "ExtShader2" 
#define ID_RK_EnableShaderSupport "EnableShaderSupport2"

#if !defined(MANAGED_SETTINGS)
extern CString ID_RK_APP;
#endif