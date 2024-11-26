// EdDll.h : main header file for the EDDLL DLL
//

#if !defined(AFX_EDDLL_H__F9C3B309_CBFE_11D1_8C07_000000000000__INCLUDED_)
#define AFX_EDDLL_H__F9C3B309_CBFE_11D1_8C07_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#ifndef __AFXWIN_H__
#error include 'stdafxed.h' before including this file for PCH
#endif

#include "VaApp.h"

/////////////////////////////////////////////////////////////////////////////
// CEdDllApp
// See EdDll.cpp for the implementation of this class
//

class CEdDllApp : public CWinApp, public VaApp
{
  public:
	CEdDllApp();

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CEdDllApp)
  public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	virtual int DoMessageBox(LPCTSTR lpszPrompt, UINT nType, UINT nIDPrompt);
	//}}AFX_VIRTUAL

	void DoInit();
	void DoExit();

	//{{AFX_MSG(CEdDllApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

void ExitLicense();
BOOL RunProcess(LPCWSTR cmdline, WORD showWindowArg = SW_HIDE);

extern VaApp* gVaDllApp;

#if defined(RAD_STUDIO)
extern CEdDllApp sVaDllApp;
#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_EDDLL_H__F9C3B309_CBFE_11D1_8C07_000000000000__INCLUDED_)
