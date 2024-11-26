#if !defined(AFX_TOOLTIPBUTTON_H__F55DB5B2_021B_11D2_8C65_000000000000__INCLUDED_)
#define AFX_TOOLTIPBUTTON_H__F55DB5B2_021B_11D2_8C65_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// ToolTipButton.h : header file
//

#include "..\XButtonXP\XButtonXP.h"

/////////////////////////////////////////////////////////////////////////////
// ToolTipButton window

class ToolTipButton : public CXButtonXP
{
	// Construction
  public:
	ToolTipButton();

	// Attributes
  protected:
	// Operations
  public:
	virtual BOOL Create(const RECT& rect, CWnd* pParent, UINT nID, UINT nIDStr);

  private:
	using CWnd::Create;
	using CXButtonXP::Create;

  public:
	void PrepareBitmaps(BOOL init = TRUE);
	virtual int CalculateWidthFromHeight(int height) const;

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(ToolTipButton)
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~ToolTipButton();

	// Generated message map functions
  protected:
	CBitmap bitmap;
	CBitmap bitmap_pressed;
	CDC bitmapdc;
	CSize bitmapsize;

	virtual void DrawIcon(CDC* pDC, bool bHasText, CRect& rectItem, CRect& rectText, bool bIsPressed, bool bIsThemed,
	                      bool bIsDisabled);
	virtual bool IsVS2010DrawingEnabled() const;

	//{{AFX_MSG(ToolTipButton)
	// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TOOLTIPBUTTON_H__F55DB5B2_021B_11D2_8C65_000000000000__INCLUDED_)
