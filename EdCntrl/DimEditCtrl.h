/*|*\
|*|  File:      DimEditCtrl.cpp
|*|
|*|  By:        James R. Twine, TransactionWorks, Inc.
|*|             Copyright 2000, TransactionWorks, inc.
|*|  Date:      Thursday, September 21, 2000
|*|
|*|  Notes:     This Is The Implementation Of A "Dim Edit Control".
|*|             It Provides Visual Instructions Within The Edit
|*|             Control Itself.  It Can Be Used To Indicate Special
|*|             Properties Of A Edit Control Used On A Crowded
|*|             Interface
|*|
|*|             May Be Freely Incorporated Into Projects Of Any Type
|*|             Subject To The Following Conditions:
|*|
|*|             o This Header Must Remain In This File, And Any
|*|               Files Derived From It
|*|             o Do Not Misrepresent The Origin Of This Code
|*|               (IOW, Do Not Claim You Wrote It)
|*|
|*|             A "Mention In The Credits", Or Similar Acknowledgement,
|*|             Is *NOT* Required.  It Would Be Nice, Though! :)
\*|*/
#if !defined(AFX_DIMEDITCTRL_H__CF8D88FB_6945_11D4_8AC4_00C04F6092F9__INCLUDED_)
#define AFX_DIMEDITCTRL_H__CF8D88FB_6945_11D4_8AC4_00C04F6092F9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include "VAThemeUtils.h"
// DimEditCtrl.h : header file
//
//#include "..\vate.h"
//
//	This Specifies The Length Of The Dim Text Buffer...
//
static const int DIM_TEXT_LEN = 145; // Dim Text Buffer Length

/////////////////////////////////////////////////////////////////////////////
// CDimEditCtrl window

class CDimEditCtrl : public CEdit
{
	// Construction
  public:
	/**/ CDimEditCtrl(); // Constructor

	// Attributes
  public:
	// Operations
  public:
	void SetShowDimControl(bool bShow);  // Show Or Hide The Dim Control
	void SetDimText(LPCTSTR cpText);     // Set The Dim Text
	void SetDimColor(COLORREF crDColor); // Set The Dim Color
	void SetDimOffset(char cRedOS, char cGreenOS,
	                  char cBlueOS);                         // Set The Dim Color Offset
	void AppendWindowText(/*HWND hWnd, */ LPCTSTR lpString); // Append Text

	void SetCustomColors(COLORREF bg, COLORREF txt, COLORREF border, bool redraw = true);
	void SetCustomDisabledColors(COLORREF bg, COLORREF txt, COLORREF border, bool redraw = true);
	void SetUseCustomColors(bool useCustomColors);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDimEditCtrl)
  public:
	virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
	                    CWnd* pParentWnd, UINT nID, CCreateContext* pContext = NULL);

  protected:
	virtual void PreSubclassWindow();
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~CDimEditCtrl();

	// Generated message map functions
  protected:
	//{{AFX_MSG(CDimEditCtrl)
	afx_msg BOOL OnChange();
	afx_msg BOOL OnSetfocus();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	//    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
	void DrawDimText(void);              // Draw The Dim Text
	COLORREF m_crDimTextColor;           // "Hard" Dim Text Color
	TCHAR m_caDimText[DIM_TEXT_LEN + 1]; // Dim Text Buffer
	bool m_bShowDimText;                 // Are We Showing The Dim Text?
	bool m_bUseDimOffset;                // Are We Using The Offset Colors (Not Hard Color)?
	char m_cRedOS;                       // Red Color Dim Offset
	char m_cGreenOS;                     // Green Color Dim Offset
	char m_cBlueOS;                      // Blue Color Dim Offset
	int m_iDimTextLen;                   // Length Of Dim Text

	bool m_bUseCustomColors;           // Whether to use custom colors to draw control
	COLORREF m_crCustomBG;             // Custom background color
	COLORREF m_crCustomText;           // Custom text color
	COLORREF m_crCustomBorder;         // Custom border color
	COLORREF m_crCustomBGDisabled;     // Custom disabled background color
	COLORREF m_crCustomTextDisabled;   // Custom disabled text color
	COLORREF m_crCustomBorderDisabled; // Custom disabled border color

	bool m_bNCAreaUpdated; // Initial WM_NCCALCSIZE status

	ThemeUtils::CBrushMap m_ctl_brushes;

  public:
	afx_msg HBRUSH CtlColor(CDC* /*pDC*/, UINT /*nCtlColor*/);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DIMEDITCTRL_H__CF8D88FB_6945_11D4_8AC4_00C04F6092F9__INCLUDED_)
