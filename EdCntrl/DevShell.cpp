#include "StdAfxEd.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "..\addin\dscmds.h"
#include "VaMessages.h"
#include "Edcnt.h"
#include "WindowUtils.h"
#include "StatusWnd.h"
#include "Registry.h"
#include "Oleobj.h"
#include "VaService.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\vsshell80.h"
#include "VAAutomation.h"
#include "PROJECT.H"
#if defined(RAD_STUDIO)
#include "CppBuilder.h"
#include "RadStudioPlugin.h"
#endif
#include "FileVerInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

DevShellService* gShellSvc = NULL;
DevShellAttributes* gShellAttr = NULL;

uint gWtMessageBoxCount = 0;

#define ID_HELP_SEARCH 0x90b2
#define ID_HELP_INDEX_VC6 0x90c6

// DevShellService
// ----------------------------------------------------------------------------
// Default implementation
//
void DevShellService::HelpSearch() const
{
}

void DevShellService::RemoveAllBreakpoints() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_DBG_REMALLBRKPTS, 0);
}

void DevShellService::FormatSelection() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_FORMAT, 0);
}

void DevShellService::ToggleBreakpoint() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, 0x00010000 | DSM_DBG_SETBRKPT, 0);
}

void DevShellService::BreakpointProperties() const
{
}

void DevShellService::EnableBreakpoint() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, 0x00010000 | DSM_DBG_TOGGLEBRKPT, 0);
}

bool DevShellService::ClearBookmarks(HWND /*hWnd*/) const
{
	return false;
}

void DevShellService::DisableAllBreakpoints() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_DBG_DISABLEALLBRKPTS, 0);
}

void DevShellService::SelectAll(EdCnt* /*ed*/) const
{
}

LPCTSTR
DevShellService::GetText(HWND edWnd, DWORD* bufLen) const
{
	return (LPCTSTR)::SendMessage(edWnd, VAM_GETTEXT, (WPARAM)bufLen, NULL);
}

HWND DevShellService::LocateStatusBarWnd() const
{
	return ::GetDlgItem(MainWndH, ID_STATUS);
}

void DevShellService::SwapAnchor() const
{
}

void DevShellService::BreakLine() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_ENTER, 0);
}

HWND DevShellService::GetFindReferencesWindow() const
{
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_OUTPUTWND_2, 0);
	// Draw focus to OutputWindow
	HWND hOutput = ::GetFocus();
	return hOutput = ::GetParent(hOutput);
}

void DevShellService::SetFindReferencesWindow(HWND /*hWnd*/)
{
}

bool DevShellService::HasBlockModeSelection(const EdCnt* /*ed*/) const
{
	return false;
}

void DevShellService::CloneFindReferencesResults() const
{
}

void DevShellService::GotoVaOutline() const
{
}

void DevShellService::ClearCodeDefinitionWindow() const
{
}

int DevShellService::MessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const
{
	return ::MessageBoxW(hWnd, lpText, lpCaption, uType);
}

void DevShellService::ScrollLineToTop() const
{
	_ASSERTE("unsupported call to DevShellService::ScrollLineToTop");
}

// Vc5ShellService
// ----------------------------------------------------------------------------
//
class Vc5ShellService : public DevShellService
{
  public:
	Vc5ShellService() : DevShellService()
	{
	}

	virtual void HelpSearch() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, ID_HELP_SEARCH, 0);
	}
#if !defined(VA_CPPUNIT)
	virtual void SelectAll(EdCnt* ed) const
	{
		ed->m_pDoc->SelectAll();
	}
#endif // !VA_CPPUNIT
	virtual void SwapAnchor() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_SWAPANCHOR, 0);
	}
	virtual bool HasBlockModeSelection(const EdCnt* ed) const
	{
		return (ed && ed->m_pDoc) ? ed->m_pDoc->HasColumnSelection() : false;
	}
	virtual void CloneFindReferencesResults() const
	{
		if (gVaService)
			gVaService->Exec(IVaService::ct_findRefResults, icmdVaCmd_RefResultsClone);
	}
	virtual void GotoVaOutline() const
	{
		if (gVaService)
			gVaService->Exec(IVaService::ct_global, icmdPkgCmd_ShowVaOutlineWindow);
	}
};

// Vc6ShellService
// ----------------------------------------------------------------------------
//
class Vc6ShellService : public Vc5ShellService
{
  public:
	Vc6ShellService() : Vc5ShellService()
	{
	}

	virtual void HelpSearch() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, ID_HELP_INDEX_VC6, 0);
	}
};

#if defined(RAD_STUDIO)
// CppBuilderShellService
// ----------------------------------------------------------------------------
//
class CppBuilderShellService : public Vc6ShellService
{
  public:
	CppBuilderShellService() : Vc6ShellService()
	{
	}
};
#endif

// Evc3ShellService
// ----------------------------------------------------------------------------
//
class Evc3ShellService : public Vc5ShellService
{
  public:
	Evc3ShellService() : Vc5ShellService()
	{
	}
};

// Evc4ShellService
// ----------------------------------------------------------------------------
//
class Evc4ShellService : public Evc3ShellService
{
  public:
	Evc4ShellService() : Evc3ShellService()
	{
	}
};

// PlatformBuilder4ShellService
// ----------------------------------------------------------------------------
//
class PlatformBuilder4ShellService : public Evc4ShellService
{
  public:
	PlatformBuilder4ShellService() : Evc4ShellService()
	{
	}
};

// Vs70ShellService
// ----------------------------------------------------------------------------
//
class Vs70ShellService : public DevShellService
{
  public:
	Vs70ShellService() : DevShellService(), mFindReferencesWnd(NULL)
	{
	}

	virtual void RemoveAllBreakpoints() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.ClearAllBreakpoints"), 0);
	}
	virtual void FormatSelection() const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.FormatSelection"), 0);
	}
	virtual void ToggleBreakpoint() const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.ToggleBreakpoint"), 0);
	}
	virtual void BreakpointProperties() const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.BreakpointProperties"), 0);
	}
	virtual void EnableBreakpoint() const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.EnableBreakpoint"), 0);
	}
	virtual bool ClearBookmarks(HWND /*hWnd*/) const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.ClearBookmarks"), 0);
		return true;
	}
	virtual void DisableAllBreakpoints() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.DisableAllBreakpoints"), 0);
	}
	virtual void SelectAll(EdCnt* ed) const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.SelectAll"), 0);
	}
	virtual LPCTSTR GetText(HWND edWnd, DWORD* bufLen) const
	{
		_ASSERTE(!"Vs70ShellService.GetText is unreliable.");
		return (LPCTSTR)SendVamMessageToCurEd(VAM_GETTEXT, (WPARAM)bufLen, NULL);
	}
	virtual HWND LocateStatusBarWnd() const
	{
		HWND h = ::GetWindow(MainWndH, GW_CHILD);
		while (h)
		{
			WTString cls = ::GetWindowClassString(h);
			if (cls == "VsStatusBar")
				return ::GetWindow(h, GW_CHILD);
			h = ::GetWindow(h, GW_HWNDNEXT);
		}
		return NULL;
	}
	virtual void SwapAnchor() const
	{
		SendVamMessageToCurEd(VAM_SWAPANCHOR, 0, 0);
	}
	virtual void BreakLine() const
	{
		SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.BreakLine"), 0);
	}
	virtual HWND GetFindReferencesWindow() const
	{
		return mFindReferencesWnd;
	}
	virtual void SetFindReferencesWindow(HWND hWnd)
	{
		mFindReferencesWnd = hWnd;
	}
	virtual bool HasBlockModeSelection(const EdCnt* /*ed*/) const
	{
		return SendVamMessageToCurEd(VAM_GETSELECTIONMODE, 0, 0) == 11;
	}
	virtual void CloneFindReferencesResults() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("VAssistX.CloneFindReferencesResults"), 0);
	}
	virtual void GotoVaOutline() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("VAssistX.VAOutline"), 0);
	}
	virtual void ScrollLineToTop() const
	{
#if defined(RAD_STUDIO)
		if (gRadStudioHost)
			gRadStudioHost->ExScrollLineToTopInActiveView();
#else
		if (gDte)
			gDte->ExecuteCommand(CComBSTR(L"Edit.ScrollLineTop"), CComBSTR((LPCWSTR)L""));
#endif
	}

  private:
	HWND mFindReferencesWnd;
};

// Vs71ShellService
// ----------------------------------------------------------------------------
//
class Vs71ShellService : public Vs70ShellService
{
  public:
	Vs71ShellService() : Vs70ShellService()
	{
	}
};

// Vs8ShellService
// ----------------------------------------------------------------------------
//
class Vs8ShellService : public Vs71ShellService
{
  public:
	Vs8ShellService() : Vs71ShellService()
	{
	}

	virtual void RemoveAllBreakpoints() const
	{
		::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) _T("Debug.DeleteAllBreakpoints"), 0);
	}
	virtual void ClearCodeDefinitionWindow() const
	{
		if (!gPkgServiceProvider)
			return;

		IUnknown* tmp = NULL;
		gPkgServiceProvider->QueryService(SID_SVsCodeDefView, IID_IVsCodeDefView, (void**)&tmp);
		if (!tmp)
			return;

		CComQIPtr<IVsCodeDefView> codeDefView(tmp);
		if (!codeDefView)
			return;

		if (codeDefView->IsVisible() != S_OK)
			return;

		codeDefView->SetContext(NULL);
		codeDefView->ForceIdleProcessing();
	}

	virtual int MessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const
	{
		// Use Shell MessageBox only for AST, for now.
		// 		if (gTestsActive)
		// 			return ShellMessageBox(hWnd, lpText, lpCaption, uType);
		return __super::MessageBox(hWnd, lpText, lpCaption, uType);
	}

  protected:
	int ShellMessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const
	{
		if (gPkgServiceProvider)
		{
			IUnknown* tmp = NULL;
			gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&tmp);
			CComQIPtr<IVsUIShell> uiShell(tmp);
			if (uiShell)
			{
				GUID rclsidComp = {0, 0, 0, {0}};
				OLEMSGBUTTON msgbtn = (OLEMSGBUTTON)(uType & 0xF);
				OLEMSGDEFBUTTON msgdefbtn = (OLEMSGDEFBUTTON)((uType >> 8) & 0x000f);
				OLEMSGICON msgicon = (OLEMSGICON)((uType >> 4) & 0x000f);
				bool fSysAlert = !!((uType >> 12) & 0x000f);
				LONG result = 0;
				uiShell->ShowMessageBox(0, rclsidComp, CComBSTR(lpCaption), CComBSTR(lpText), NULL, 0, msgbtn,
				                        msgdefbtn, msgicon, fSysAlert, &result);
				return result;
			}
		}
		return ::MessageBoxW(hWnd, lpText, lpCaption, uType);
	}
};

// Vs9ShellService
// ----------------------------------------------------------------------------
//
class Vs9ShellService : public Vs8ShellService
{
  public:
	Vs9ShellService() : Vs8ShellService()
	{
	}
};

// Vs10ShellService
// ----------------------------------------------------------------------------
//
class Vs10ShellService : public Vs9ShellService
{
  public:
	Vs10ShellService() : Vs9ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2010";
	}
};

// Avr5ShellService
// ----------------------------------------------------------------------------
//
class Avr5ShellService : public Vs10ShellService
{
  public:
	Avr5ShellService() : Vs10ShellService()
	{
	}
};

class Avr51ShellService : public Avr5ShellService
{
  public:
	Avr51ShellService() : Avr5ShellService()
	{
	}
};

class Avr6ShellService : public Vs10ShellService
{
  public:
	Avr6ShellService() : Vs10ShellService()
	{
	}
};

class Avr61ShellService : public Avr6ShellService
{
  public:
	Avr61ShellService() : Avr6ShellService()
	{
	}
};

class Avr62ShellService : public Avr61ShellService
{
  public:
	Avr62ShellService() : Avr61ShellService()
	{
	}
};

// Vs11ShellService
// ----------------------------------------------------------------------------
//
class Vs11ShellService : public Vs10ShellService
{
  public:
	Vs11ShellService() : Vs10ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2012";
	}

	virtual int MessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const
	{
		// 		return ShellMessageBoxW(hWnd, lpText, lpCaption, uType);
		return __super::MessageBox(hWnd, lpText, lpCaption, uType);
	}
};

// Vs12ShellService
// ----------------------------------------------------------------------------
//
class Vs12ShellService : public Vs11ShellService
{
  public:
	Vs12ShellService() : Vs11ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2013";
	}
};

class Avr7ShellService : public Vs12ShellService
{
  public:
	Avr7ShellService() : Vs12ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Atmel Studio\\7.0";
	}
};

// Vs14ShellService
// ----------------------------------------------------------------------------
//
class Vs14ShellService : public Vs12ShellService
{
  public:
	Vs14ShellService() : Vs12ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2015";
	}

	virtual int MessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const
	{
		// 		// commented out due to hangs during ShellMessageBox in vs2015
		// 		if (gTestsActive)
		// 			return ShellMessageBoxW(hWnd, lpText, lpCaption, uType);
		return __super::MessageBox(hWnd, lpText, lpCaption, uType);
	}
};

// Vs15ShellService
// ----------------------------------------------------------------------------
//
class Vs15ShellService : public Vs14ShellService
{
  public:
	Vs15ShellService() : Vs14ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2017";
	}
};

// Vs15_8ShellService
// ----------------------------------------------------------------------------
//
class Vs15_8ShellService : public Vs15ShellService
{
  public:
	Vs15_8ShellService() : Vs15ShellService()
	{
	}

	virtual bool HasBlockModeSelection(const EdCnt* ed) const
	{
		// [case: 117499]
		return ed && ed->HasBlockOrMultiSelection();
	}
};

// Vs16ShellService
// ----------------------------------------------------------------------------
//
class Vs16ShellService : public Vs15_8ShellService
{
  public:
	Vs16ShellService() : Vs15_8ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2019";
	}
};

// Vs17ShellService
// ----------------------------------------------------------------------------
//
class Vs17ShellService : public Vs16ShellService
{
  public:
	Vs17ShellService() : Vs16ShellService()
	{
	}
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return L"Visual Studio 2022";
	}
};

// #newVsVersion

// DummyShellService
// ----------------------------------------------------------------------------
//
class DummyShellService : public DevShellService
{
  public:
	DummyShellService() : DevShellService()
	{
	}

	virtual void HelpSearch() const
	{
	}
	virtual void RemoveAllBreakpoints() const
	{
	}
	virtual void FormatSelection() const
	{
	}
	virtual void ToggleBreakpoint() const
	{
	}
	virtual void BreakpointProperties() const
	{
	}
	virtual void EnableBreakpoint() const
	{
	}
	virtual bool ClearBookmarks(HWND /*hWnd*/) const
	{
		return true;
	}
	virtual void DisableAllBreakpoints() const
	{
	}
	virtual void SelectAll(EdCnt* /*ed*/) const
	{
	}
	virtual LPCTSTR GetText(HWND /*edWnd*/, DWORD* /*bufLen*/) const
	{
		return NULL;
	}
	virtual HWND LocateStatusBarWnd() const
	{
		return NULL;
	}
	virtual void OpenOutputWindow() const
	{
	}
	virtual void SwapAnchor() const
	{
	}
	virtual void BreakLine() const
	{
	}
};

#include "Regkeys.h"
#pragma comment(lib, "version.lib")

// DevShellAttributes
// ----------------------------------------------------------------------------
// Default implementation
//
DevShellAttributes::DevShellAttributes(const CString& baseShellKeyName)
    : mEnvType(ShellEnvType_None), mBaseShellKeyName(baseShellKeyName), mDbSubDir(), mExeName(),
      mRequiresHeapLookaside(false), mHelpOpenAttempts(20), mSupportsHelpEditClassName(false),
      mFormatSourceWindowKeyName(), mPlatformListRootKey(HKEY_CURRENT_USER), mPlatformListKeyName(), mOldVaAppKeyName(),
      mSupportsNetFrameworkDevelopment(false), mDefaultCharWidth(1), mDefaultPlatform(IDS_DEF_PLATFORM),
      mCanTryAlternatePlatformSettings(false), mCanTryVa4Settings(true), mDefaultCorrectCaseSetting(true),
      mDefaultBraceMismatchSetting(true), mIsFastProjectOpenAllowed(true), mSetupRootKey(HKEY_LOCAL_MACHINE),
      mSetupKeyName(), mCompletionListBottomOffset(0), mSupportsBreakpoints(true), mSupportsBreakpointProperties(false),
      mSupportsBookmarks(true), mSupportsCustomTooltipFont(false), mSupportsAIC(false), mSupportsDspFiles(false),
      mRequiresPositionConversion(false), mShouldUseXpVisualManager(false), mSupportsSelectionReformat(true),
      mSupportsCImportDirective(false), mRequiresWin32ApiPatching(true), mRequiresFindResultsHack(false),
      mDefaultAutobackupSetting(true)
{
	ID_RK_APP = ID_RK_APP_KEY;
}

// Evc3ShellAttributes
// ----------------------------------------------------------------------------
//
class Evc3ShellAttributes : public DevShellAttributes
{
  public:
	Evc3ShellAttributes(const CString& baseShellKeyName = _T(ID_RK_EVC30)) : DevShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Msdev;
		ID_RK_APP = ID_RK_APP_KEY "\\VACE3";
		mDbSubDir = L"evc3";
		mExeName = L"EVC.exe";
		mRequiresHeapLookaside = true;
		mHelpOpenAttempts = 50;
		mSupportsHelpEditClassName = true;
		mFormatSourceWindowKeyName = GetBaseShellKeyName() + _T(ID_RK_MSDEV_FORMAT);
		mPlatformListKeyName = GetBaseShellKeyName() + _T(ID_RK_MSDEV_INC_KEY);
		mDefaultPlatform = _T("Win32 (WCE ARM)");
		mSupportsAIC = true;
		mSupportsDspFiles = true;
		mSupportsCImportDirective = true;
		mRequiresFindResultsHack = true;
	}

	virtual CString GetPlatformDirectoryKeyName(LPCTSTR platform)
	{
		return GetBaseShellKeyName() + _T(ID_RK_MSDEV_INC_KEY) + platform + _T(IDS_PLATFORM_DIR);
	}
};

// Evc4ShellAttributes
// ----------------------------------------------------------------------------
//
class Evc4ShellAttributes : public Evc3ShellAttributes
{
  public:
	Evc4ShellAttributes(const CString& baseShellKeyName = _T(ID_RK_EVC40)) : Evc3ShellAttributes(baseShellKeyName)
	{
		ID_RK_APP = ID_RK_APP_KEY "\\VACE4";
		mDbSubDir = L"evc4";
		mDefaultPlatform = _T("Win32 (WCE ARMV4)");
	}
};

// PlatformBuilder4ShellAttributes
// ----------------------------------------------------------------------------
//
class PlatformBuilder4ShellAttributes : public Evc4ShellAttributes
{
  public:
	PlatformBuilder4ShellAttributes(const CString& baseShellKeyName = _T(ID_RK_PLATFORMBUILDER))
	    : Evc4ShellAttributes(baseShellKeyName)
	{
		ID_RK_APP = ID_RK_APP_KEY "\\VAPB";
		mDbSubDir = L"pb4";
	}
};

// Vc5ShellAttributes
// ----------------------------------------------------------------------------
//
class Vc5ShellAttributes : public DevShellAttributes
{
  public:
	Vc5ShellAttributes(const CString& baseShellKeyName = _T(ID_RK_MSDEV) _T("5.0"))
	    : DevShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Msdev;
		ID_RK_APP = ID_RK_APP_KEY "\\VA6";
		mDbSubDir = L"vc5";
		mExeName = L"MSDEV.exe";
		mRequiresHeapLookaside = true;
		mSupportsHelpEditClassName = true;
		mFormatSourceWindowKeyName = GetBaseShellKeyName() + _T(ID_RK_MSDEV_FORMAT);
		mPlatformListKeyName = GetBaseShellKeyName() + _T(ID_RK_MSDEV_INC_KEY);
		mOldVaAppKeyName = _T(ID_RK_APP_VA6);
		mSupportsAIC = true;
		mSupportsDspFiles = true;
		mSupportsCImportDirective = true;
		mRequiresFindResultsHack = true;
	}

	virtual CString GetPlatformDirectoryKeyName(LPCTSTR platform)
	{
		return GetBaseShellKeyName() + _T(ID_RK_MSDEV_INC_KEY) + platform + _T(IDS_PLATFORM_DIR);
	}
};

// Vc6ShellAttributes
// ----------------------------------------------------------------------------
//
class Vc6ShellAttributes : public Vc5ShellAttributes
{
  public:
	Vc6ShellAttributes(const CString& baseShellKeyName = _T(ID_RK_MSDEV) _T("6.0"))
	    : Vc5ShellAttributes(baseShellKeyName)
	{
		mDbSubDir = L"vc6";
		mHelpOpenAttempts = 50;
		mSupportsHelpEditClassName = false;
	}
};

// Vs70ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs70ShellAttributes : public DevShellAttributes
{
  public:
	Vs70ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\7.0"))
	    : DevShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv7;
		ID_RK_APP = ID_RK_APP_KEY "\\VANet7.0";
		mDbSubDir = L"vs70";
		mExeName = L"DEVENV.exe";
		mPlatformListRootKey = HKEY_LOCAL_MACHINE;
		mPlatformListKeyName = GetBaseShellKeyName() + _T("\\VC\\VC_OBJECTS_PLATFORM_INFO\\");
		mOldVaAppKeyName = _T(ID_RK_APP_VANET);
		mSupportsNetFrameworkDevelopment = true;
		mDefaultCharWidth = 16;
		mDefaultPlatform = _T("Win32");
		mSetupKeyName = GetBaseShellKeyName() + _T("\\Setup\\");
		mSupportsBreakpointProperties = true;
		mSupportsBookmarks = false;
		mSupportsCustomTooltipFont = true;
		mCanTryVa4Settings = false;
		mRequiresPositionConversion = true;
		mShouldUseXpVisualManager = true;
	}

	virtual CString GetPlatformDirectoryKeyName(LPCTSTR platform)
	{
		return CString(ID_RK_APP + _T("\\") + platform);
	}
	virtual CString GetPlatformDirectoryKeyName2(LPCTSTR platform) const
	{
		return GetPlatformListKeyName() + platform + _T(IDS_PLATFORM_DIR);
	}
};

#if defined(RAD_STUDIO)
// CppBuilderShellAttributes
// ----------------------------------------------------------------------------
//
class CppBuilderShellAttributes : public Vc6ShellAttributes
{
  public:
	CppBuilderShellAttributes(const CString& baseShellKeyName = "")
	    : Vc6ShellAttributes(baseShellKeyName)
	{
		// override baseShellName but first assert, so don't change default ctor param empty value
		_ASSERTE(gVaRadStudioPlugin);
		mBaseShellKeyName = gVaRadStudioPlugin->GetBaseRegPath();

		mEnvType = ShellEnvType_CppBuilder;
		mExeName = L"bds.exe";
		mDbSubDir = L"CppB";
		mFormatSourceWindowKeyName.Empty();
		mDefaultPlatform = _T("Win32");
		mPlatformListRootKey = HKEY_CURRENT_USER;
		mPlatformListKeyName = GetBaseShellKeyName() + _T("\\C++\\Paths\\");
		// GetBaseShellKeyName() has RadStudio version number in it, so we don't need to version va reg path
		ID_RK_APP = GetBaseShellKeyName() + "\\VisualAssist";
		mDefaultAutobackupSetting = false;

		mSupportsDspFiles = false;
		mSupportsAIC = false;
		mSupportsCImportDirective = false;

		mRequiresHeapLookaside = false;
		mOldVaAppKeyName.Empty();
		mSetupKeyName = GetBaseShellKeyName();
		mSetupRootKey = HKEY_CURRENT_USER;
	}

	virtual CString GetPlatformDirectoryKeyName(LPCTSTR platform)
	{
		return GetPlatformListKeyName() + platform;
	}
};
#endif

// Vs71ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs71ShellAttributes : public Vs70ShellAttributes
{
  public:
	Vs71ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\7.1"))
	    : Vs70ShellAttributes(baseShellKeyName)
	{
		ID_RK_APP = ID_RK_APP_KEY "\\VANet";
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// Vs8ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs8ShellAttributes : public Vs71ShellAttributes
{
  public:
	Vs8ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\8.0"))
	    : Vs71ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv8;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
		mPlatformListKeyName.Empty(); // platforms are handled from addin side - so clear default value
		mCompletionListBottomOffset = 6;
		mSupportsBreakpointProperties = false; // until bug 902 is addressed
		mDefaultAutobackupSetting = false;
	}
};

// Vs9ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs9ShellAttributes : public Vs8ShellAttributes
{
  public:
	Vs9ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\9.0"))
	    : Vs8ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv9;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// Vs10ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs10ShellAttributes : public Vs9ShellAttributes
{
  public:
	Vs10ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\10.0"))
	    : Vs9ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv10;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// Vs11ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs11ShellAttributes : public Vs10ShellAttributes
{
  public:
	Vs11ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\11.0"))
	    : Vs10ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv11;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// Vs12ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs12ShellAttributes : public Vs11ShellAttributes
{
  public:
	Vs12ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\12.0"))
	    : Vs11ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv12;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// Vs14ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs14ShellAttributes : public Vs12ShellAttributes
{
  public:
	Vs14ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\14.0"))
	    : Vs12ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv14;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

#ifdef AVR_STUDIO
class Avr7ShellAttributes : public Vs14ShellAttributes
{
  public:
	Avr7ShellAttributes(const CString& baseShellKeyName = _T("Software\\Atmel\\AtmelStudio\\7.0"))
	    : Vs14ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv14;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\AtmelStudio" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"avr" + ::itosw(GetRegistryVersionNumber());
	}
};
#endif

// Vs15ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs15ShellAttributes : public Vs14ShellAttributes
{
	bool mInitFromDte = false;

  public:
	Vs15ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\15.0"))
	    : Vs14ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv15;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
		mSetupRootKey = HKEY_CURRENT_USER;
	}

	virtual CString GetBaseShellKeyName()
	{
		if (mInitFromDte || !gDte)
			return mBaseShellKeyName;

		CString keyBase;
		CComBSTR regRootBstr;
		gDte->get_RegistryRoot(&regRootBstr);
		keyBase = regRootBstr;
		if (keyBase.IsEmpty())
		{
			_ASSERTE(!"no vs base regkey");
		}
		else
		{
			mBaseShellKeyName = keyBase;
			mSetupKeyName = keyBase + _T("_Config\\Setup\\");
			mFormatSourceWindowKeyName = keyBase + _T(ID_RK_MSDEV_FORMAT);
			mInitFromDte = true;
		}

		return mBaseShellKeyName;
	}
};

// Vs16ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs16ShellAttributes : public Vs15ShellAttributes
{
	bool mInitFromDte = false;

  public:
	Vs16ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\16.0"))
	    : Vs15ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv16;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
#if defined(RAD_STUDIO_LANGUAGE)
		mDbSubDir = L"CppB" + ::itosw(GetRegistryVersionNumber());
#else
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
#endif
	}
};

// Vs17ShellAttributes
// ----------------------------------------------------------------------------
//
class Vs17ShellAttributes : public Vs16ShellAttributes
{
	bool mInitFromDte = false;

  public:
	Vs17ShellAttributes(const CString& baseShellKeyName = _T("Software\\Microsoft\\VisualStudio\\17.0"))
	    : Vs16ShellAttributes(baseShellKeyName)
	{
		mEnvType = ShellEnvType_Devenv17;
		ID_RK_APP = WTString(ID_RK_APP_KEY "\\VANet" + ::itos(GetRegistryVersionNumber())).c_str();
		mDbSubDir = L"vs" + ::itosw(GetRegistryVersionNumber());
	}
};

// #newVsVersion

// DummyShellAttributes
// ----------------------------------------------------------------------------
// Based on Vs8 but IsDevEnv, etc will all return false.
//
template <typename SHELL_ATTRIBUTES = Vs8ShellAttributes> class DummyShellAttributes : public SHELL_ATTRIBUTES
{
  public:
	DummyShellAttributes() : SHELL_ATTRIBUTES()
	{
		this->mEnvType = DevShellAttributes::ShellEnvType_None;
		this->mDbSubDir = L"noIde";
		this->mOldVaAppKeyName.Empty();
		this->mSupportsBreakpoints = false;
		this->mSupportsBreakpointProperties = false;
		this->mSupportsBookmarks = false;
		this->mSupportsCustomTooltipFont = false;
		this->mCanTryVa4Settings = false;
		this->mRequiresPositionConversion = false;
		this->mRequiresWin32ApiPatching = false;
		this->mExeName.Empty();
	}
};

#if defined(VA_CPPUNIT)
// TestShellAttributes
// ----------------------------------------------------------------------------
// Based on DummyShellAttributes with custom dir and exe.
//
template <typename SHELL_ATTRIBUTES = Vs8ShellAttributes>
class TestShellAttributes : public DummyShellAttributes<SHELL_ATTRIBUTES>
{
  public:
	TestShellAttributes() : DummyShellAttributes<SHELL_ATTRIBUTES>()
	{
		this->mDbSubDir = L"testDb";
#ifdef _WIN64
		this->mExeName = L"edtests64.exe";
#else
		this->mExeName = L"edtests.exe";
#endif
		if constexpr(std::is_same_v<SHELL_ATTRIBUTES, Vs17ShellAttributes>)
			this->mEnvType = DevShellAttributes::ShellEnvType_Devenv17;
	}
};
#endif // VA_CPPUNIT

#if !defined(RAD_STUDIO)
static DWORD GetVerInfo(LPCTSTR file, DWORD& verLoword)
{
	TCHAR strBuf[MAX_PATH];
	CString buf;
	DWORD infoSize;
	LPVOID pBlock = NULL, pVerInfo = NULL;
	DWORD retval = 0, majVer;
	HINSTANCE hMod = ::GetModuleHandleA(file);
	verLoword = 0;

	if (!hMod)
	{
		::GetModuleFileNameA(NULL, strBuf, MAX_PATH);
		buf = strBuf;
		goto BAILOUT_VER;
	}

	::GetModuleFileNameA(hMod, strBuf, MAX_PATH);
	infoSize = GetFileVersionInfoSizeA(strBuf, &majVer);
	buf = strBuf;
	if (!infoSize)
		goto BAILOUT_VER;
	pBlock = calloc(infoSize, sizeof(DWORD));
	if (!GetFileVersionInfoA(strBuf, 0, infoSize, pBlock))
		goto BAILOUT_VER;
	if (!VerQueryValue(pBlock, _T("\\"), &pVerInfo, (UINT*)&infoSize))
		goto BAILOUT_VER;
	retval = majVer = HIWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionMS);
	verLoword = majVer = LOWORD(((VS_FIXEDFILEINFO*)pVerInfo)->dwFileVersionMS);

BAILOUT_VER:
	if (pBlock)
		free(pBlock);
	return retval;
}
#endif

void InitShell()
{
#if !defined(RAD_STUDIO)
	DWORD rev;
	DWORD version;
#endif

	extern void InitIdeVersion();
	InitIdeVersion();

	_ASSERTE(!gShellAttr && !gShellSvc);

#if defined(VA_CPPUNIT)
	if (::GetModuleHandleA("edtests.exe") || ::GetModuleHandleA("edtests64.exe"))
	{
		extern std::optional<int> vsver_cmdline;

		if (!vsver_cmdline)
			gShellAttr = new TestShellAttributes<Vs8ShellAttributes>;
		else
			switch (*vsver_cmdline)
			{
			case 5:
				gShellAttr = new TestShellAttributes<Vc5ShellAttributes>;
				break;
			case 6:
				gShellAttr = new TestShellAttributes<Vc6ShellAttributes>;
				break;
			case 7:
// 			case 70:
// 				gShellAttr = new TestShellAttributes<Vs70ShellAttributes>;
				break;
// 			case 71:
// 				gShellAttr = new TestShellAttributes<Vs71ShellAttributes>;
// 				break;
			case 8:
				gShellAttr = new TestShellAttributes<Vs8ShellAttributes>;
				break;
			case 9:
				gShellAttr = new TestShellAttributes<Vs9ShellAttributes>;
				break;
			case 10:
				gShellAttr = new TestShellAttributes<Vs10ShellAttributes>;
				break;
			case 11:
				gShellAttr = new TestShellAttributes<Vs11ShellAttributes>;
				break;
			case 12:
				gShellAttr = new TestShellAttributes<Vs9ShellAttributes>;
				break;
			case 14:
				gShellAttr = new TestShellAttributes<Vs14ShellAttributes>;
				break;
			case 15:
				gShellAttr = new TestShellAttributes<Vs15ShellAttributes>;
				break;
			case 16:
				gShellAttr = new TestShellAttributes<Vs16ShellAttributes>;
				break;
			case 17:
			case 99: // #newVsVersion
				gShellAttr = new TestShellAttributes<Vs17ShellAttributes>;
				break;
			default:
				gShellAttr = new TestShellAttributes<>;
				break;
			}
		gShellSvc = new DummyShellService;
	}
#endif // VA_CPPUNIT

#ifdef AVR_STUDIO
	if (!gShellAttr && ::GetModuleHandleA("atmelstudio.exe"))
	{
		version = GetVerInfo("atmelstudio.exe", rev);
		switch (version)
		{
		case 7:
			if (rev == 0)
			{
				// Atmel Studio 7.0
				gShellAttr = new Avr7ShellAttributes;
				gShellSvc = new Avr7ShellService;
			}
			else
			{
				_ASSERTE(!"unhandled avrstudio version");
			}
			break;
		default:
			_ASSERTE(!"unhandled avrstudio version");
		}
	}
#elif defined(RAD_STUDIO)
	if (!gShellAttr && ::GetModuleHandleA("bds.exe"))
	{
		gShellAttr = new CppBuilderShellAttributes;
		gShellSvc = new CppBuilderShellService;
	}
#else
	if (!gShellAttr && ::GetModuleHandleA("DevEnv.exe"))
	{
		version = GetVerInfo("DevEnv.exe", rev);
		switch (version)
		{
		// #newVsVersion
		case 17:
			gShellAttr = new Vs17ShellAttributes;
			gShellSvc = new Vs17ShellService;
			break;
		case 16:
			gShellAttr = new Vs16ShellAttributes;
			gShellSvc = new Vs16ShellService;
			break;
		case 15:
			gShellAttr = new Vs15ShellAttributes;
			if (8 > rev)
				gShellSvc = new Vs15ShellService;
			else
				gShellSvc = new Vs15_8ShellService;
			break;
		case 14:
			gShellAttr = new Vs14ShellAttributes;
			gShellSvc = new Vs14ShellService;
			break;
		case 12:
			gShellAttr = new Vs12ShellAttributes;
			gShellSvc = new Vs12ShellService;
			break;
		case 11:
			gShellAttr = new Vs11ShellAttributes;
			gShellSvc = new Vs11ShellService;
			break;
		case 10:
			gShellAttr = new Vs10ShellAttributes;
			gShellSvc = new Vs10ShellService;
			break;
		case 9:
			gShellAttr = new Vs9ShellAttributes;
			gShellSvc = new Vs9ShellService;
			break;
		case 8:
			gShellAttr = new Vs8ShellAttributes;
			gShellSvc = new Vs8ShellService;
			break;
		case 7:
			if (10 == rev)
			{
				gShellAttr = new Vs71ShellAttributes;
				gShellSvc = new Vs71ShellService;
			}
			else
			{
				gShellAttr = new Vs70ShellAttributes;
				gShellSvc = new Vs70ShellService;
			}
			break;
		default:
			_ASSERTE(!"unhandled devenv version");
		}
	}

	if (!gShellAttr && ::GetModuleHandleA("DevShl.dll"))
	{
		version = GetVerInfo("DevShl.dll", rev);
		switch (version)
		{
		case 6:
			gShellAttr = new Vc6ShellAttributes;
			gShellSvc = new Vc6ShellService;
			break;
		case 5:
			gShellAttr = new Vc5ShellAttributes;
			gShellSvc = new Vc5ShellService;
			break;
		case 3:
			gShellAttr = new Evc3ShellAttributes;
			gShellSvc = new Evc3ShellService;
			SetRegValue(HKEY_CURRENT_USER, _T(ID_RK_APP), "", "");
			break;
		case 4:
			if (::GetModuleHandleA("cepb.exe"))
			{
				gShellAttr = new PlatformBuilder4ShellAttributes;
				gShellSvc = new PlatformBuilder4ShellService;
			}
			else
			{
				gShellAttr = new Evc4ShellAttributes;
				gShellSvc = new Evc4ShellService;
			}
			break;
		default:
			_ASSERTE(!"unhandled devshl version");
		}
	}
#endif

	if (!gShellAttr)
	{
		// create dummy
		CString cmdLine(GetCommandLine());
		cmdLine.MakeLower();
		if (-1 == cmdLine.Find("setup") && -1 == cmdLine.Find("regsvr32") && -1 == cmdLine.Find("uninst") && -1 == cmdLine.Find("vaxdllloadtest"))
		{
			_ASSERTE(!"unknown app");
		}

		gShellAttr = new DummyShellAttributes<>;
		gShellSvc = new DummyShellService;
	}

	Log((const char*)ID_RK_APP);
}

void UninitShell()
{
	delete gShellAttr;
	gShellAttr = NULL;
	delete gShellSvc;
	gShellSvc = NULL;
}

int WtMessageBox(const CStringW& lpText, const CStringW& lpCaption, UINT uType /*= MB_OK*/)
{
	return WtMessageBox(gMainWnd->GetSafeHwnd(), lpText, lpCaption, uType);
}

int WtMessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType /*= MB_OK*/)
{
	vLog("WtMsgBox: %s", WTString(lpText).c_str());
	if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && hWnd == MainWndH && nullptr != g_currentEdCnt)
	{
		// [case: 118459] keep the message box from potentially becoming lost
		uType |= MB_TOPMOST;
	}

	struct addOne
	{
		uint& val;
		addOne(uint& _val) : val(_val)
		{
			val++;
		}
		~addOne()
		{
			if (val)
				val--;
		}
	} add(gWtMessageBoxCount);

	if (gShellSvc)
		return gShellSvc->MessageBox(hWnd, lpText, lpCaption, uType);
	return ::MessageBoxW(hWnd, lpText, lpCaption, uType);
}

bool DevShellAttributes::IsDevenv17u7OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv18OrHigher())
		ret = true;
	else if (IsDevenv17())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (7 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv17u8OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv18OrHigher())
		ret = true;
	else if (IsDevenv17())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (8 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u11OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv17OrHigher())
		ret = true;
	else if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (11 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u10OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv17OrHigher())
		ret = true;
	else if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (10 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u9OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv17OrHigher())
		ret = true;
	else if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (9 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u8OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv17OrHigher())
		ret = true;
	else if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (8 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u7OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv17OrHigher())
		ret = true;
	else if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (7 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv16u7() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv16())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (7 == fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv15u9OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv16OrHigher())
		ret = true;
	else if (IsDevenv15())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (9 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv15u8OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv15u9OrHigher())
		ret = true;
	else if (IsDevenv15())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (8 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv15u7OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv15u8OrHigher())
		ret = true;
	else if (IsDevenv15())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (7 <= fvi.GetFileVerMSLo())
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv15u6OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv15u7OrHigher())
		ret = true;
	else if (IsDevenv15())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			// in vs2017, until 15.6, devenv always reported 15.0
			// assume 15.6+ if vs2017 version is not 15.0
			ret = true;
			if (0 == fvi.GetFileVerString().Find("15.0."))
				ret = false;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv15u3OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv15u6OrHigher())
		ret = true;
	else if (IsDevenv15())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			// 15.3 first preview: devenv is 15.0.26426.1007, msenv.dll is 15.0.26426.7
			// 15.2: 26430.06 and 26430.13
			// 15.3 preview 2: 26606.0
			// 15.3 preview 2.1: 26608.5
			// 15.3 preview 3:
			// 15.3 preview 4: 26711.1
			// 15.3 preview 5:
			// 15.3 preview 6: 26724.1
			// 15.3 preview 7: 26730.0
			// 15.3 preview 7.1 and release: 26730.3
			// [case: 104119] but note that 26731 could be a 15.2 update...
			if (fvi.GetFileVerString().Compare("15.0.26730.0") >= 0)
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv14u3OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv15OrHigher())
		ret = true;
	else if (IsDevenv14())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (fvi.GetFileVerString().Compare("14.0.25401.0") >= 0)
				ret = true;
		}
	}

	didInit = true;
	return ret;
}

bool DevShellAttributes::IsDevenv14u2OrHigher() const
{
	static bool didInit = false;
	static bool ret = false;

	if (didInit)
		return ret;

	if (IsDevenv14u3OrHigher())
		ret = true;
	else if (IsDevenv14())
	{
		HMODULE hVs = ::GetModuleHandleA("devenv.exe");
		FileVersionInfo fvi;
		if (hVs && fvi.QueryFile(hVs))
		{
			if (fvi.GetFileVerString().Compare("14.0.25029.0") >= 0)
				ret = true;
		}
	}

	didInit = true;
	return ret;
}
