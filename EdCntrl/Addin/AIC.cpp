#include "stdafx.h"
#include "..\RegKeys.h"
#include "..\..\Addin\AIC.h"
#include "io.h"
#include "log.h"
#include "..\DevShellAttributes.h"
#include "DllNames.h"
#include "FileVerInfo.h"

///////////////////////////////////////////////////////////////////////////////
// the following is dependent on the AddInComm.dll header file
#ifndef DECLARE_OPAQUE
#define DECLARE_OPAQUE(name)    typedef struct { int unused; } name##__ ; \
				typedef const name##__ * name; \
				typedef name*  LP##name
#endif

// handle to add-in
DECLARE_OPAQUE(HADDIN);

// this is the prototype of the command handling function for your add-in
typedef int (AddinCmdHandler_t)(LPCTSTR pCmd);

typedef HADDIN __declspec(dllimport) (*RegAddinFuncType)(LPCTSTR pName, int iVerMaj, int iVerMin, int iVerExtra, AddinCmdHandler_t *pCmdFn);
typedef bool __declspec(dllimport) (*UnregAddinFuncType)(HADDIN hAddIn);
typedef HADDIN __declspec(dllimport) (*GetAddinFuncType)(LPCTSTR pName);
typedef int __declspec(dllimport) (*AddinCmdFuncType)(HADDIN hAddIn, LPCTSTR pCommand);
typedef void __declspec(dllexport) (*SetAddInVersionStringFuncType)(HADDIN hAddIn, LPCTSTR pszVerStr);	// non-critical introduced in AIC 1.2.0

// end AddInComm.dll dependency
///////////////////////////////////////////////////////////////////////////////
#define AICDLL	"AIC.dll"
#define AICDLL2	"AIC.mod"

static RegAddinFuncType		gpfnRegAddin;
static UnregAddinFuncType	gpfnUnregAddin;
static GetAddinFuncType		gpfnGetAddin;
static AddinCmdFuncType		gpfnAddinCmd;
static SetAddInVersionStringFuncType	g_pfnSetVerStr;
static HMODULE g_hAICDll;
static HADDIN g_hVA;
int AICAddInCallback(LPCTSTR pCmd);

void WtAicRegister()
{
	if (!gShellAttr->SupportsAIC())
		return;
	if (g_hAICDll)
		return;
	CString aicDllName(AICDLL2);
	if (!::GetModuleHandleA(aicDllName) && !::GetModuleHandleA(AICDLL))
	{
		static bool once = true;
		if (once)
		{
			// look in the msdev addins directory - only once
			once = false;

			bool found = false;
			// starting in AIC 1.0.4 and WndTabs 3.0 there's a regkey for
			//  the aic module
			{ // from AIC 1.0.4 AICLoader.cpp
			// the following comments apply to the code within this brace block

			/***************************************************************************/
			/* NOTE:                                                                   */
			/* This document is copyright (c) by Oz Solomonovich, and is bound by the  */
			/* MIT open source license (www.opensource.org/licenses/mit-license.html). */
			/* See License.txt for more information.                                   */
			/***************************************************************************/


			/*  
			This Work Is Copyright (c) 1999-2000 by Oz Solomonovich

			Permission is hereby granted, free of charge, to any person obtaining a copy 
			of this software and associated documentation files (the "Software"), to deal
			in the Software without restriction, including without limitation the rights
			to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
			copies of the Software, and to permit persons to whom the Software is 
			furnished to do so, subject to the following conditions:

			The above copyright notice and this permission notice shall be included in 
			all copies or substantial portions of the Software.

			THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
			IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
			FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
			AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
			LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
			OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
			SOFTWARE.
			*/
				
			HKEY hKey;
			char path[_MAX_PATH];
			DWORD dwtype, dwLen = sizeof(path);

			if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE,
				"Software\\WndTabs.com\\AddInComm", 0, KEY_READ, &hKey) == 
				ERROR_SUCCESS)
			{
				if (::RegQueryValueEx(hKey, "Path", 0, &dwtype, 
					(LPBYTE)path, &dwLen) == ERROR_SUCCESS)
				{
					const size_t len = strlen(path);
					if (len && path[len-1] != '\\')
						strcat(path, "\\");
					strcat(path, AICDLL2);
					if (!_access(path, 00))
					{
						found = true;
						aicDllName = path;
					}
				}
				::RegCloseKey(hKey);
			}
			}	// end AICLoader.cpp code

			if (!found)
			{
				HMODULE hVA = ::GetModuleHandleA(IDS_MSDEV_EXE);
				if (!hVA)
				{
					hVA = ::GetModuleHandleA(IDS_EVC_EXE);
					if (!hVA)
						return;
				}
				std::vector<char> pAicDllNameVec((MAX_PATH*2)+1);
				LPTSTR pAicDllName = &pAicDllNameVec[0];
				if (!::GetModuleFileNameA(hVA, pAicDllName, MAX_PATH*2))
					return;
				char* pos = _tcsrchr(pAicDllName, '\\');
				if (!pos)
					return;
				*pos = '\0';
				pos = _tcsrchr(pAicDllName, '\\');
				if (!pos)
					return;
				*++pos = '\0';
				_tcscat(pAicDllName, "Addins\\" AICDLL2);
				if (_access(pAicDllName, 00))
				{
					*pos = '\0';
					_tcscat(pAicDllName, "Addins\\" AICDLL);
					if (_access(pAicDllName, 00))
						return;
				}
				aicDllName = pAicDllName;
			}
		} else
			return;
	}
	// will only get here if we know where the addinComm.dll is
	g_hAICDll = LoadLibraryA(aicDllName);
	if (!g_hAICDll)
	{
		g_hAICDll = LoadLibraryA(AICDLL);
		if (!g_hAICDll)
			return;
	}
	gpfnRegAddin = (RegAddinFuncType)(intptr_t)GetProcAddress(g_hAICDll, "AICRegisterAddIn");
	gpfnUnregAddin = (UnregAddinFuncType)(intptr_t)GetProcAddress(g_hAICDll, "AICUnregisterAddIn");
	gpfnGetAddin = (GetAddinFuncType)(intptr_t)GetProcAddress(g_hAICDll, "AICGetAddIn");
	gpfnAddinCmd = (AddinCmdFuncType)(intptr_t)GetProcAddress(g_hAICDll, "AICSendCommand");
	g_pfnSetVerStr = (SetAddInVersionStringFuncType)(intptr_t)GetProcAddress(g_hAICDll, "AICSetAddInVersionString");
	if (gpfnRegAddin)
	{
		CString verStr;

		FileVersionInfo fvi;
		if (fvi.QueryFile(IDS_ADDINDLL, FALSE))
			CString__FormatA(verStr, "%s %s", (const char *)fvi.GetProdVerString(), (const char *)fvi.GetModuleComment());

		g_hVA = gpfnRegAddin("VisualAssist60", fvi.GetProdVerMSHi(), fvi.GetProdVerMSLo(),
			fvi.GetProdVerLSHi(), AICAddInCallback);
		if (g_hVA && g_pfnSetVerStr && verStr.GetLength())
			g_pfnSetVerStr(g_hVA, verStr);
	}
	if (!(g_hVA && gpfnRegAddin && gpfnUnregAddin && gpfnGetAddin && gpfnAddinCmd))
		WtAicUnregister();
}

void WtAicUnregister()
{
	if (!g_hAICDll)
		return;
	if (g_hVA && gpfnUnregAddin)
	{
		gpfnUnregAddin(g_hVA);
		g_hVA = NULL;
	}
	gpfnRegAddin = NULL;
	gpfnUnregAddin = NULL;
	gpfnGetAddin = NULL;
	gpfnAddinCmd = NULL;
	g_pfnSetVerStr = NULL;
	FreeLibrary(g_hAICDll);
	g_hAICDll = NULL;
}

enum AICAddinCmd {HookWnd, UnhookWnd};
static int WndTabCmd(LPCTSTR addinname, AICAddinCmd cmd, HWND wnd);

typedef struct tagAddinList
{
	LPCTSTR		m_addinName;
	bool		m_bShouldSubclass;
}AddinList;

static const int g_addinCnt = 2;

// Alphabetical list of addins - looks like msdev loads addins alphabetically
//  unsubclass in reverse order
//  subclass in forward order
static AddinList g_addinList[g_addinCnt] =
{
	{"ZManagers"},		// he asked us to use ZManagers, but the addin is Useful Managers
	{"WndTabs"}
};

AICAddinSubclass::AICAddinSubclass(HWND hWnd) : m_hWnd(hWnd)
{
	try
	{
		if (!g_hAICDll)
		{
			WtAicRegister();
			if (!g_hAICDll)
				return;
		}
		// unhook in reverse order
		for (int idx = g_addinCnt-1; idx >= 0; idx--)
		{
			if (g_addinList[idx].m_addinName && *g_addinList[idx].m_addinName &&
					WndTabCmd(g_addinList[idx].m_addinName, UnhookWnd, hWnd) == 1)
				g_addinList[idx].m_bShouldSubclass = true;
			else
				g_addinList[idx].m_bShouldSubclass = false;
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("AIC:");
		ASSERT(FALSE);
	}
}

AICAddinSubclass::~AICAddinSubclass()
{
	if (!g_hAICDll)
		return;
	try
	{
		// hook in alphabetical order
		for (int idx = 0; idx < g_addinCnt; idx++)
		{
			if (g_addinList[idx].m_bShouldSubclass)
			{
				g_addinList[idx].m_bShouldSubclass = false;
				WndTabCmd(g_addinList[idx].m_addinName, HookWnd, m_hWnd);
			}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("AIC:");
		ASSERT(FALSE);
	}
}

static int WndTabCmd(LPCTSTR addinname, AICAddinCmd cmd, HWND wnd)
{
	int retval = 0;
	try {
		// don't save this handle since user could unload the addin
		HADDIN hWTabsAddin = gpfnGetAddin(addinname);
		if (!hWTabsAddin)
			return 0;
		CString cmdMsg;
		switch (cmd)
		{
		case HookWnd:
			CString__FormatA(cmdMsg, "Subclass %p", wnd);
			retval = gpfnAddinCmd(hWTabsAddin, cmdMsg);
			break;
		case UnhookWnd:
			CString__FormatA(cmdMsg, "IsSubclassed %p", wnd);
			if (gpfnAddinCmd(hWTabsAddin, cmdMsg))
			{
				CString__FormatA(cmdMsg, "Unsubclass %p", wnd);
				retval = gpfnAddinCmd(hWTabsAddin, cmdMsg);
			}
			break;
		default:
			ASSERT(FALSE);
		}
	} catch (...) {
		VALOGEXCEPTION("AIC:");
		ASSERT(FALSE);
	}
	return retval;
}

// callback for AddInComm
int AICAddInCallback(LPCTSTR pCmd)
{
	(void)pCmd;
	return -1;
}
