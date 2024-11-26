// XButtonXP.h  Version 1.3
//
// Author:  Hans Dietrich
//          hdietrich@gmail.com
//
// License:
//     This software is released under the Code Project Open License (CPOL),
//     which may be found here:  http://www.codeproject.com/info/eula.aspx
//     You are free to use this software in any way you like, except that you 
//     may not sell this source code.
//
//     This software is provided "as is" with no expressed or implied warranty.
//     I accept no liability for any damage or loss of business that this 
//     software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef  XBUTTONXP_H 
#define  XBUTTONXP_H 

#include "OddButton.h"

//=============================================================================
// Following file is available in Microsoft Platform SDK.
//=============================================================================
#include "uxtheme.h"

#define XBUTTONXP_NO_COLOR ((COLORREF) -1)

//=============================================================================
class CXButtonXP : public COddButton
//=============================================================================
{
//=============================================================================
// Construction
//=============================================================================
public:
	CXButtonXP();
	virtual ~CXButtonXP();

//=============================================================================
// Attributes
//=============================================================================
public:
	enum ICON_ALIGNMENT
	{
		CENTER = 0,
		LEFT,
		RIGHT
	};

	CXButtonXP& SetIconAlignment(ICON_ALIGNMENT ia) 
	{ 
		m_eIconAlignment = ia; 
		RedrawWindow();
		return *this;
	}
	bool GetToggle() const { return m_bIsToggle; }
	CXButtonXP& SetToggle(bool bIsToggle) 
	{ 
		m_bIsToggle = bIsToggle;
		m_bToggled  = false;
		RedrawWindow();
		return *this;
	}
	bool GetToggleState() const { return m_bToggled; }
	CXButtonXP& SetToggleState(bool bToggled) 
	{ 
		m_bToggled = bToggled; 
		RedrawWindow(); 
		return *this;
	}
	CXButtonXP& EnableTheming(bool bEnable) 
	{
		m_bEnableTheming = bEnable; 
		if (m_bEnableTheming)
		{
			TRACE(_T("XButtonXP theming will be enabled only on XP and Vista ")
				  _T("and only if the app is themed with a manifest file.\n"));
		}
		RedrawWindow();
		return *this;
	}
	bool IsThemed();
	bool GetDrawToolbar() const { return m_bDrawToolbar; }
	CXButtonXP& SetDrawToolbar(bool bDrawToolbar) 
	{ 
		m_bDrawToolbar = bDrawToolbar; 
		m_bFirstTime = TRUE;	// force OpenThemeData() call
		RedrawWindow();
		return *this;
	}

	COLORREF GetTextColor() const { return m_crText; }
	CXButtonXP& SetTextColor(COLORREF rgb = XBUTTONXP_NO_COLOR) 
	{ 
		m_crText = rgb; 
		return *this; 
	}
	COLORREF GetBackgroundColor() const { return m_crBackground; }
	CXButtonXP& SetBackgroundColor(COLORREF rgb = XBUTTONXP_NO_COLOR) 
	{
		m_crBackground = rgb; 
		return *this; 
	}


//=============================================================================
// Operations
//=============================================================================
public:
	CXButtonXP& SetIcon(UINT nIDResource, ICON_ALIGNMENT ia = LEFT);
	CXButtonXP& SetIcon(HICON hIcon, ICON_ALIGNMENT ia = LEFT);

	bool m_bNoFocusRect;		// gmit

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CXButtonXP)
public:
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
protected:
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

//=============================================================================
// Implementation
//=============================================================================
protected:
	virtual void DrawIcon(CDC *pDC, 
				  bool bHasText, 
				  CRect& rectItem, 
				  CRect& rectText, 
				  bool bIsPressed, 
				  bool bIsThemed,
				  bool bIsDisabled);

	void DrawText(CDC *pDC,
				  LPCTSTR lpszText,
				  CRect& rect,
				  bool bIsPressed, 
				  bool bIsThemed,
				  bool bIsDisabled);

	void PrepareImageRect(bool bHasText, 
						  CRect& rectItem, 
						  CRect& rectText, 
						  bool bIsPressed, 
						  bool bIsThemed,
						  DWORD dwWidth, 
						  DWORD dwHeight, 
						  CRect& rectImage);

	void SaveParentBackground(bool only_if_position_changed);

	virtual bool IsVS2010DrawingEnabled() const {return false;}

	bool			m_bFirstTime;
	bool			m_bMouseOverButton;
	bool			m_bToggled;
	bool			m_bIsToggle;
	bool			m_bLButtonDown;
	bool			m_bSent;
	bool			m_bEnableTheming;
	bool			m_bDrawToolbar;
	HTHEME			m_hTheme;
	HICON			m_hIcon;
	HICON			m_hGrayIcon;
	ICON_ALIGNMENT	m_eIconAlignment;
	CRect			m_rectButton;		// button rect in parent window coordinates
	CRect			m_lastButtonWindowRect;		// button rect in window coordinates last time SaveParentBackground was called
	CDC				m_dcParent;
	CBitmap			m_bmpParent;
	CBitmap *		m_pOldParentBitmap;
	COLORREF		m_crBackground;		// optional button background color -
										// when a background color is specified,
										// theming is switched off
	COLORREF		m_crText;			// optional button text color -
										// when a background color is specified,
										// theming is switched off

//=============================================================================
// Generated message map functions
//=============================================================================
	//{{AFX_MSG(CXButtonXP)
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	//}}AFX_MSG
	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // XBUTTONXP_H 
