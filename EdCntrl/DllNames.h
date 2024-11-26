#pragma once

#define IDS_ADDINDLL "VAssist.dll"

#if defined(RAD_STUDIO)
#define IDS_VAX_DLL "VA_X_RS.dll"
#define IDS_VAX_DLLW L"VA_X_RS.dll"
#elif defined(VAX_CODEGRAPH)
#define IDS_VAX_DLL "VA_X_CG.dll"
#define IDS_VAX_DLLW L"VA_X_CG.dll"
#else
#ifdef _ARM64
#define IDS_VAX_DLL "VA_X_arm64.dll"
#define IDS_VAX_DLLW L"VA_X_arm64.dll"
#elif defined(_WIN64)
#define IDS_VAX_DLL "VA_X64.dll"
#define IDS_VAX_DLLW L"VA_X64.dll"
#else
#define IDS_VAX_DLL "VA_X.dll"
#define IDS_VAX_DLLW L"VA_X.dll"
#endif
#endif

#ifdef ASYNC_PACKAGE
#ifdef _ARM64
#define IDS_VAPKG_DLLW L"vapkgasync_arm64.dll"
#elif defined(_WIN64)
#define IDS_VAPKG_DLLW L"vapkgasync64.dll"
#else
#define IDS_VAPKG_DLLW L"vapkgasync.dll"
#endif
#elif !defined(VISUAL_ASSIST_X) && !defined(ADDINSIDE)
// IDS_VAPKG_DLLW is only resolved properly within the VaPkg projects
#define IDS_VAPKG_DLLW L"vapkg.dll"
#endif

#ifdef _ARM64
#define IDS_VASSISTNET_DLLW L"VAssistNet_arm64.dll"
#define IDS_VASSISTNET_DLL_LOWER_W L"vassistnet_arm64.dll"
#define IDS_DBMTX_EXEW L"VaDbMtx_arm64.exe"
#define IDS_VANETOBJMD_DLLW L"VaNetObjMD_arm64.dll"
#define IDS_VAAUX_DLL "VaAux_arm64.dll"
#define IDS_VAOPSWIN_DLL "VaOpsWin_arm64.dll"
#define IDS_VAOPSWIN_DLLW L"VaOpsWin_arm64.dll"
#define IDS_VATE2_DLL "VATE2_arm64.dll"
#define IDS_VATE2_DLLW L"VATE2_arm64.dll"
#define IDS_VAX_INTEROP_DLLW L"VAX_arm64.Interop.dll" // see also:#VaInteropDllName in C# source
#define IDS_VAIPC_DLL "vaIPC_arm64.dll"
#define IDS_VAWPFSNIPPETEDITOR_DLLW L"VASnippetEditor.17.dll"
#define IDS_VAOPTKEYBINDS_DLLW L"VaOptKeyBinds.17.dll"
#define IDS_VADEBUGGERTOOLS_DLLW L"VaDebuggerToolsAsync.17.dll"
#elif defined(_WIN64)
#define IDS_VASSISTNET_DLLW L"VAssistNet64.dll"
#define IDS_VASSISTNET_DLL_LOWER_W L"vassistnet64.dll"
#define IDS_DBMTX_EXEW L"VaDbMtx64.exe"
#define IDS_VANETOBJMD_DLLW L"VaNetObjMD64.dll"
#define IDS_VAAUX_DLL "VaAux64.dll"
#define IDS_VAOPSWIN_DLL "VaOpsWin64.dll"
#define IDS_VAOPSWIN_DLLW L"VaOpsWin64.dll"
#define IDS_VATE2_DLL "VATE2_64.dll"
#define IDS_VATE2_DLLW L"VATE2_64.dll"
#define IDS_VAX_INTEROP_DLLW L"VAX64.Interop.dll" // see also:#VaInteropDllName in C# source
#define IDS_VAIPC_DLL "vaIPC64.dll"
#define IDS_VAWPFSNIPPETEDITOR_DLLW L"VASnippetEditor.17.dll"
#define IDS_VAOPTKEYBINDS_DLLW L"VaOptKeyBinds.17.dll"
#define IDS_VADEBUGGERTOOLS_DLLW L"VaDebuggerToolsAsync.17.dll"
// Code Inspection interop dll is at #CiInteropDllName in C# source
#else
#define IDS_VASSISTNET_DLLW L"VAssistNet.dll"
#define IDS_VASSISTNET_DLL_LOWER_W L"vassistnet.dll"
#define IDS_DBMTX_EXEW L"VaDbMtx.exe"
#define IDS_VANETOBJMD_DLLW L"VaNetObjMD.dll"
#define IDS_VAAUX_DLL "VaAux.dll"
#define IDS_VAOPSWIN_DLL "VaOpsWin.dll"
#define IDS_VAOPSWIN_DLLW L"VaOpsWin.dll"
#define IDS_VATE2_DLL "VATE2.dll"
#define IDS_VATE2_DLLW L"VATE2.dll"
#define IDS_VAX_INTEROP_DLLW L"VAX.Interop.dll"
#define IDS_VAIPC_DLL "vaIPC.dll"
#define IDS_VAWPFSNIPPETEDITOR_DLLW L"VASnippetEditor.dll"
#define IDS_VAOPTKEYBINDS_DLLW L"VaOptKeyBinds.dll"
#define IDS_VADEBUGGERTOOLS_DLLW L"VaDebuggerToolsAsync.dll"
#endif

#define IDS_VAAUX_EXEW L"VaAuxServer64.exe"
