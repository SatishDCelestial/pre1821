#if !defined(AFX_MINIHELPFRM_H__AAAC0FA2_022C_11D2_8C65_000000000000__INCLUDED_)
#define AFX_MINIHELPFRM_H__AAAC0FA2_022C_11D2_8C65_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// MiniHelpFrm.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// MiniHelpFrm frame with splitter

#include "NonViewSplitter.h"
#include "ToolTipButton.h"
#include "..\WTString.h"
#include "CWndDpiAware.h"

class EditParentWnd;
class CToolTipEditView;
class VaDpiContext;
class CSettings;

enum class SizeUnit
{
	Logical,
	Device
};

class MiniHelpFrm : public CWndDpiAwareMiniHelp<CWnd>
{
	friend class CToolTipEditCombo;

  public:
	MiniHelpFrm();

	virtual void OnDpiChanged(CWndDpiAware::DpiChange change, bool& handled);

	// Attributes
  protected:
	CToolTipCtrl mToolTipCtrl;
	NonViewSplitter m_wndSplitter;
	CToolTipEditView *m_context, *m_def, *m_project;
	ToolTipButton* m_gotoButton;
	EditParentWnd* m_parent;
	EditParentWnd* m_edParent;
	UINT m_layoutVersion;
	DWORD m_lastException;

	BOOL Create(CWnd* parent);
	void UpdateHeight(bool resetTimer = true);

  private:
	using CWnd::Create;

  public:
	// Operations
  public:
	BOOL Reparent(CWnd* parent, CWnd* edParent = NULL, LPCWSTR con = nullptr, LPCWSTR def = nullptr, LPCWSTR proj = nullptr);
	void SetHelpText(LPCWSTR con, LPCWSTR def, LPCWSTR proj, bool asyncUpdate = true);
	void SetProjectText(CStringW& proj);
	void UpdateSizeWPF(int width, int height);
	static bool IsAnyPopupActive();

	void RecalcLayout();
	CStringW GetText() const;
	void SettingsChanged();

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(MiniHelpFrm)
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~MiniHelpFrm();
	// Generated message map functions
	//{{AFX_MSG(MiniHelpFrm)
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnGotoClick();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	void DoUpdateText();

	CCriticalSection mTextCs;
	CStringW mContextText, mPendingContextText;
	CStringW mDefinitionText, mPendingDefinitionText;
	CStringW mProjectText, mPendingProjectText;
};

/////////////////////////////////////////////////////////////////////////////
extern MiniHelpFrm* g_pMiniHelpFrm;
void FreeMiniHelp();
void DoStaticSubclass(HWND wpfStatic);

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MINIHELPFRM_H__AAAC0FA2_022C_11D2_8C65_000000000000__INCLUDED_)
