// cdxCDynamicWndEx.h: interface for the cdxCDynamicWndEx class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_CDXCDYNAMICWNDEX_H__96C8C1D4_6524_11D3_8030_000000000000__INCLUDED_)
#define AFX_CDXCDYNAMICWNDEX_H__96C8C1D4_6524_11D3_8030_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "cdxCDynamicWnd.h"

/*
 * cdxCDynamicWndEx
 * ================
 * A class extended to offer some useful additions.
 */

class cdxCDynamicWndEx : public cdxCDynamicWnd
{
public:
	enum RestoreFlags
	{
		rflg_none			=	0,			// only load window position
		rflg_state			=	0x01,		// make window iconic/zoomed if been before
		rflg_visibility	=	0x02,		// hide/show window as been before
		rflg_all				=	rflg_state|rflg_visibility
	};

	enum ExFlags
	{
		flAutoPos	=	0x0100
	};

private:
	CString	m_strAutoPos;

protected:
	RECT m_restoreRect = { 0 };

public:
	cdxCDynamicWndEx(Freedom fd, UINT nFlags) : cdxCDynamicWnd(fd,nFlags) {}
	virtual ~cdxCDynamicWndEx() {}

	//
	// utilities
	//

	bool StretchWindow(const CSize & szDelta);
	bool StretchWindow(int iAddPcnt);
	bool RestoreWindowPosition(LPCTSTR lpszProfile, UINT restoreFlags = rflg_all);
	bool StoreWindowPosition(LPCTSTR lpszProfile);

	HMONITOR IsOnTheSameMonitor() const;

	//
	// feature one: auto-positioning :)
	// 

	void ActivateAutoPos(UINT nID) { CString__FormatA(m_strAutoPos, _T("ID=0x%08x"),nID); }
	void ActivateAutoPos(const CString & strID) { m_strAutoPos = strID; }
	void NoAutoPos() { m_strAutoPos.Empty(); }

	//
	// we need these
	//

protected:
	virtual void OnInitialized();
	virtual void OnDestroying();

	static bool CheckIfWindowIsOutOfScreen(const CRect &rect);
private:
	static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);

public:
	static LPCTSTR	M_lpszAutoPosProfileSection;
};

#endif // !defined(AFX_CDXCDYNAMICWNDEX_H__96C8C1D4_6524_11D3_8030_000000000000__INCLUDED_)
