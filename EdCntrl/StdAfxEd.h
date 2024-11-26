#pragma once

//#pragma warning(disable:4100)  // unreferenced parameter
//#pragma warning(disable:4183)  // Looks like a constructor
//#pragma warning(disable:4189)  // local variable is initialized but not referenced
//#pragma warning(disable:4505)  // unreferenced local function has been removed
//#pragma warning(disable:4512)  // assignment operator could not be generated
//#pragma warning(disable:4481)  // nonstandard extensions: override
//#pragma warning(disable:4127)  // conditional expression is constant
//#pragma warning(disable:4456)  // declaration of 'XX' hides previous local declaration
//#pragma warning(disable:4457)  // declaration of 'XX' hides function parameter
//#pragma warning(disable:4458)  // declaration of 'XX' hides class member
#pragma warning(push, 2)
#pragma warning(disable : 4263 4264 4266 4917 5204 5246)
#define _SILENCE_CXX17_STRSTREAM_DEPRECATION_WARNING 1
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING 1
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1
#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING 1

#pragma warning(push)

#if _MSC_VER >= 1700
// VS2012+
#define _VARIADIC_MAX 10
#endif

#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers

#ifdef _WIN64
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#else
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define WINVER _WIN32_WINNT_VISTA
#endif
#include <sdkddkver.h>


#ifdef _DEBUG
/*
 * we'll set inline optimizations in the project settings but override them here
 *  if this is a debug build.  Trying to make WTString inlines stay inline
 *  in debug builds and make sure nothing else gets inlined
 * For this to work, inline function expansion in the c/C++ project
 *  settings tabs can't be set to disable.
 */
#pragma inline_depth(0)
#endif

#if _MSC_VER >= 1400
#define _CRT_SECURE_NO_DEPRECATE
#endif

#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#define GDIPVER 0x0110

#ifdef _DEBUG
// https://msdn.microsoft.com/en-us/library/x98tx3cf%28v=vs.120%29.aspx
#ifndef _CRTDBG_MAP_ALLOC
#define _CRTDBG_MAP_ALLOC
#endif
#include <stdlib.h>
#include <CRTDbg.h>
#else
#include <stdlib.h>
#endif

#include <afx.h>
#include <afxwin.h>  // MFC core and standard components
#include <afxdisp.h> // MFC OLE automation classes
#include <afxext.h>  // MFC extensions
#include <afxpriv.h>
#include <afxhtml.h> // MFC HTML view support
#include <afxeditbrowsectrl.h>
//#include <afxcontrolbarutil.h>

// sean def'd these
#define _AFX_NO_DB_SUPPORT
#define _AFX_NO_DAO_SUPPORT

#ifndef _AFX_NO_OLE_SUPPORT
#include <afxole.h>   // MFC OLE classes
#include <afxodlgs.h> // MFC OLE dialog classes
#endif                // _AFX_NO_OLE_SUPPORT

#ifndef _AFX_NO_DB_SUPPORT
#include <afxdb.h> // MFC ODBC database classes
#endif             // _AFX_NO_DB_SUPPORT

#ifndef _AFX_NO_DAO_SUPPORT
#include <afxdao.h> // MFC DAO database classes
#endif              // _AFX_NO_DAO_SUPPORT

#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h> // MFC support for Windows Common Controls
#endif              // _AFX_NO_AFXCMN_SUPPORT

#include <shlwapi.h>
#undef STATIC_CAST
#include <imm.h>

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

// sean added these
#ifdef DEBUG
// force these inline even in debug builds
#pragma intrinsic(strcmp, strcpy, strcat, strlen, memcmp, memcpy, memset)
#else
#pragma warning(disable : 4711)
#endif

#include <afxmt.h>
#include <wtypes.h>
#include <ctype.h>
#include <richedit.h>
#include <stdio.h>
#include <io.h>
#include <mshtmhst.h>
#include <mshtml.h>
#undef CMD_ZOOM_PAGEWIDTH
#undef CMD_ZOOM_ONEPAGE
#undef CMD_ZOOM_TWOPAGES
#undef CMD_ZOOM_SELECTION
#undef CMD_ZOOM_FIT

#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>

#if _MSC_VER <= 1200
#include <iostream.h>
#include <fstream.h>
#include <strstrea.h>
#include <afxtempl.h> // HashKey
#define VS_STD
#else
#include <iostream>
#include <fstream>
#include <strstream>
#define VS_STD std
#ifndef INCL_WINSOCK_API_PROTOTYPES
#define INCL_WINSOCK_API_PROTOTYPES 0
#endif
#endif

#include "WTFStream.h"

#include <tchar.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <direct.h>
#include <process.h>
#include <stddef.h>
#include <string.h>

#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS 1
#include <hash_map>
#include <hash_set>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <vector>
#include <list>
#include <deque>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <utility>
#include <limits>
#include <numeric>
#include <numbers>
#include <assert.h>
#include <functional>
#include <regex>
#include <exception>
using namespace std::placeholders;
#include <ppl.h>
#include <optional>
#include <variant>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <concepts>
#include <string>
#include <tuple>
#include <ranges>
#include <atomic>
#include <initializer_list>
#include <chrono>
#include <compare>
#include <coroutine>
#include <random>
#include <type_traits>
#include <filesystem>
#include <bitset>

#include "parallel_hashmap/phmap.h"
#define XXH_INLINE_ALL 1
#include "xxHash/xxhash.h"



#pragma warning(disable : 4815)

#pragma warning(pop)

// Import EnvDTE
#pragma warning(disable : 4278)
#import "libid:80cc9f66-e7d8-4ddd-85b6-d9e6cd0e93e2" version("8.0") lcid("0") raw_interfaces_only named_guids rename("ULONG_PTR", "ULONG_PTRDTE")
#pragma warning(default : 4278)

// Import EnvDTE80
#import "libid:1A31287A-4D7D-413e-8E32-3B374931BD89" version("8.0") lcid("0") raw_interfaces_only named_guids rename("ULONG_PTR", "ULONG_PTRDTE")

#define chSTR(x) #x
#define chSTR2(x) chSTR(x)
#define NOTE(desc) message(__FILE__ "(" chSTR2(__LINE__) "): NOTE: " #desc)
// usage: #pragma NOTE(any text to appear during builds) but don't use apostrophes

// *** Moved from SolutionFiles.cpp!
#import "..\VassistNET\vslangproj.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\vslangproj2.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\vslangproj80.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\vslangproj90.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\vslangproj100.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\vslangproj110.olb" raw_interfaces_only raw_native_types
#import "..\VassistNET\webproperties.tlb" raw_interfaces_only raw_native_types

#pragma warning(disable : 4278)
#import "VcProjectEngine/VCProjectEngine7.dll" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary7") rename("Log", "vsLog")
#import "VcProjectEngine/VCProjectEngine8.dll" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary8") rename("Log", "vsLog")
#import "VcProjectEngine/VCProjectEngine9.dll" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary9") rename("Log", "vsLog")
#import "VcProjectEngine/vcp10b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary10") rename("Log", "vsLog")
#import "VcProjectEngine/vcp11b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary11") rename("Log", "vsLog")
#import "VcProjectEngine/vcp12b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary12") rename("Log", "vsLog")
#import "VcProjectEngine/vcp14b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary14") rename("Log", "vsLog")
#import "VcProjectEngine/vcp15b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary15") rename("Log", "vsLog")
#import "VcProjectEngine/vcp15_8b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary15_8") rename("Log", "vsLog")
#import "VcProjectEngine/VCInterfaces15.tlb" raw_interfaces_only raw_native_types rename_namespace("VCppInterfaces15")
#import "VcProjectEngine/VCInterfaces15_8.tlb" raw_interfaces_only raw_native_types rename_namespace("VCppInterfaces15_8")
#import "VcProjectEngine/vcp16b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary16") rename("Log", "vsLog")
#import "VcProjectEngine/VCInterfaces16.tlb" raw_interfaces_only raw_native_types rename_namespace("VCppInterfaces16")
#ifdef _WIN64
#import "VcProjectEngine/vcp17b2.tlb" raw_interfaces_only raw_native_types rename_namespace("VCProjectEngineLibrary17") rename("Log", "vsLog")
#import "VcProjectEngine/VCInterfaces17.tlb" raw_interfaces_only raw_native_types rename_namespace("VCppInterfaces17")
#endif
// #newVsVersion
#pragma warning(default : 4278)

#ifdef AVR_STUDIO
#import "mscorlib.tlb" rename("ReportEvent", "msReportEvent")

// I can't get AvrGcc.tlb to repeatedly, consistently import.
// use this command to generate the tlb:
//	tlbexp.exe "c:\Atmel\Studio\7.0\extensions\Application\AvrGCC.dll" /asmpath:"C:\Program Files\Microsoft Visual
// Studio 14.0\VSSDK\VisualStudioIntegration\Common\Assemblies\v2.0" /asmpath:"C:\Program Files\Microsoft Visual
// Studio 14.0\VSSDK\VisualStudioIntegration\Common\Assemblies\v4.0"
/// asmpath:"C:\Atmel\Studio\7.0\extensions\Application" /asmpath:"C:\Program Files\Reference
// Assemblies\Microsoft\Framework\.NETFramework\v4.5" /asmpath:"C:\Program Files\Common Files\microsoft
// shared\MSEnv\PublicAssemblies" /asmpath:"C:\Program Files\Microsoft Visual Studio 14.0\Common7\IDE\PublicAssemblies"
/// asmpath:"C:\Program Files\MSBuild\14.0\Bin"
// Then uncomment the #imports to somehow get avrgcc.tlh to be created.
//#import "VcProjectEngine/avr/AvrGcc.tlb" raw_interfaces_only raw_native_types
// rename_namespace("AvrProjectEngineLibrary7")

// This is used to skip the auto-generated tlh -- it is now checked in.
#include "VcProjectEngine/avr/AvrGcc.tlh"
#endif

#ifdef VISUAL_ASSIST_X
#ifndef ADDINSIDE
#include "vsshell100.h"
//#	include "vsshell110.h"

#include "VA_Gdiplus.h"
#endif
#endif

#pragma warning(pop)

#ifdef VISUAL_ASSIST_X
#ifndef ADDINSIDE
// utils
#include "assert_once.h"
#include "SaveDC.h"
#include "TempSettingOverride.h"
#endif
#endif

#undef min
#undef max

#include "utils3264.h"
