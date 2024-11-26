#if !defined(AFX_WORKSPACETAB_H__6C056D32_63A8_11D2_8173_00207814D759__INCLUDED_)
#define AFX_WORKSPACETAB_H__6C056D32_63A8_11D2_8173_00207814D759__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// WorkSpaceTab.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// WorkSpaceTab window

class WorkSpaceTab : public CTabCtrl
{
	// Construction
  public:
	WorkSpaceTab(CWnd* parent);
	virtual ~WorkSpaceTab();

	// Attributes
  private:
	CFont m_font;
	int m_keyDown;

	// Operations
  public:
	void SelectVaView();
	void SelectVaOutline();
	void SwitchTab(bool next);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(WorkSpaceTab)
  protected:
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

	// Implementation
  private:
	static LRESULT CALLBACK WorkspaceSubclassProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);
	void UpdateCurrentTab();
	void SelectNextTab();
	void SelectPreviousTab();

	// Generated message map functions
	//{{AFX_MSG(WorkSpaceTab)
	afx_msg void OnSelchange(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()

  private:
	bool mHasLiveOutline;
};

extern WorkSpaceTab* g_WorkSpaceTab;
bool VAWorkspaceSetup_VC6();
/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_WORKSPACETAB_H__6C056D32_63A8_11D2_8173_00207814D759__INCLUDED_)
