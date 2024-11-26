#pragma once

#include "WTString.h"

class EdCnt;

// DevShellService
// ----------------------------------------------------------------------------
// Interface to IDE specific implementation / IDE commands.
// No state - all virtual operations.
//
class DevShellService
{
  protected:
	// only create derived classes
	DevShellService()
	{
	}

  public:
	virtual ~DevShellService()
	{
	}

	virtual void HelpSearch() const;
	virtual void RemoveAllBreakpoints() const;
	virtual void FormatSelection() const;
	virtual void ToggleBreakpoint() const;
	virtual void BreakpointProperties() const;
	virtual void EnableBreakpoint() const;
	virtual bool ClearBookmarks(HWND hWnd) const;
	virtual void DisableAllBreakpoints() const;
	virtual void SelectAll(EdCnt* ed) const;
	virtual LPCTSTR GetText(HWND edWnd, DWORD* bufLen) const;
	virtual HWND LocateStatusBarWnd() const;
	virtual void SwapAnchor() const;
	virtual void BreakLine() const;
	virtual HWND GetFindReferencesWindow() const;
	virtual void SetFindReferencesWindow(HWND hWnd);
	virtual bool HasBlockModeSelection(const EdCnt* ed) const; // return true if there is a column selection
	virtual void CloneFindReferencesResults() const;
	virtual void GotoVaOutline() const;
	virtual void ClearCodeDefinitionWindow() const;
	virtual int MessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType) const;
	virtual const WCHAR* GetMyDocumentsProductDirectoryName() const
	{
		return nullptr;
	}
	virtual void ScrollLineToTop() const;
};

extern DevShellService* gShellSvc;
extern uint gWtMessageBoxCount;

// old for targeting specific parents
int WtMessageBox(HWND hWnd, const CStringW& lpText, const CStringW& lpCaption, UINT uType = MB_OK);
inline int WtMessageBox(HWND hWnd, const CString& lpText, LPCTSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(hWnd, CStringW(lpText), CStringW(lpCaption), uType);
}
inline int WtMessageBox(HWND hWnd, const WTString& lpText, LPCTSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(hWnd, lpText.Wide(), CStringW(lpCaption), uType);
}
inline int WtMessageBox(HWND hWnd, const WTString& lpText, const WTString& lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(hWnd, lpText.Wide(), lpCaption.Wide(), uType);
}
inline int WtMessageBox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(hWnd, WTString(lpText).Wide(), CStringW(lpCaption), uType);
}

// prefer these -- parented to application main window
int WtMessageBox(const CStringW& lpText, const CStringW& lpCaption, UINT uType = MB_OK);
inline int WtMessageBox(const CString& lpText, LPCTSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(CStringW(lpText), CStringW(lpCaption), uType);
}
inline int WtMessageBox(const WTString& lpText, LPCTSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(lpText.Wide(), CStringW(lpCaption), uType);
}
inline int WtMessageBox(const WTString& lpText, const WTString& lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(lpText.Wide(), lpCaption.Wide(), uType);
}
inline int WtMessageBox(LPCSTR lpText, LPCSTR lpCaption, UINT uType = MB_OK)
{
	return WtMessageBox(WTString(lpText).Wide(), CStringW(lpCaption), uType);
}
