#if !defined(AFX_STATUSWND_H__7D097D32_272D_11D2_8CB2_000000000000__INCLUDED_)
#define AFX_STATUSWND_H__7D097D32_272D_11D2_8CB2_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// StatusWnd.h : header file
//
#include "IVaShellService.h"

/////////////////////////////////////////////////////////////////////////////
// StatusWnd window
#ifndef RAD_STUDIO
class StatusWnd : public CWnd
{
	// Construction
  public:
	StatusWnd();

	enum
	{
		TxtBufLen = 512
	};

	// Attributes
  protected:
	HWND m_StatBar;
	// single buffer - no reallocs for cross thread sharing without locks
	char mTxtBuf[TxtBufLen + 1];
	IVaShellService::StatusAnimation mAnimType;
	CComPtr<EnvDTE::StatusBar> mDteStatusBar;
	// Operations
  public:
	void SetStatusBarHwnd(HWND statBar)
	{
		m_StatBar = statBar;
	}
	void SetStatusText(LPCTSTR txt);
	void SetAnimation(IVaShellService::StatusAnimation type, bool asynchronous = true);
	void ClearAnimation(bool asynchronous = true);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(StatusWnd)
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~StatusWnd();

	// Generated message map functions
  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT OnSetAnimation(IVaShellService::StatusAnimation type);
	LRESULT OnClearAnimation();
	//{{AFX_MSG(StatusWnd)
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()
};
#else
class StatusWnd : public CWnd
{
  public:
	enum
	{
		TxtBufLen = 512
	};

	StatusWnd();
	~StatusWnd();

	void SetStatusBarHwnd(HWND statBar) {}
	void SetStatusText(LPCTSTR txt);
	void SetAnimation(IVaShellService::StatusAnimation type, bool asynchronous = true) {}
	void ClearAnimation(bool asynchronous = true) {}
};
#endif


#define ID_STATUS 0x0000e801 // id of vc status bar
extern StatusWnd* g_statBar;

extern void SetStatus(UINT nID, ...);
void SetStatusQueued(const char* format, ...);
inline void SetStatus(LPCTSTR str)
{
	if (g_statBar)
		g_statBar->SetStatusText(str);
}
inline void SetStatus(const WTString& str)
{
	if (g_statBar)
		g_statBar->SetStatusText(str.c_str());
}

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STATUSWND_H__7D097D32_272D_11D2_8CB2_000000000000__INCLUDED_)
