#if !defined(FindInWkspcDlg_h__)
#define FindInWkspcDlg_h__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// FindInWkspcDlg.h : header file
//

#include "VADialog.h"
#include "FilterEdit.h"
#include "ListCtrlFixedGrid.h"

#include "DimEditCtrl.h"
#include "CtrlBackspaceEdit.h"
#include "VAThemeDraw.h"

/////////////////////////////////////////////////////////////////////////////
// FindInWkspcDlg dialog

class FindInWkspcDlg : public CThemedVADlg
{
	// Construction
  public:
	FindInWkspcDlg(LPCTSTR regValueNameBase, int dlgIDD, CWnd* pParent, bool defaultWorkspaceRestriction,
	               bool saveWorkspaceRestriction); // standard constructor
	~FindInWkspcDlg();

  protected:
	BOOL IsRestrictedToWorkspace() const
	{
		return mRestrictToWorkspaceFiles;
	}
	// Dialog Data
	//{{AFX_DATA(FindInWkspcDlg)
	FilterEdit<CtrlBackspaceEdit<CThemedDimEdit>> mEdit;
	CListCtrlFixedGrid mFilterList;
	bool mSaveWorkspaceRestrictionSetting;
	BOOL mRestrictToWorkspaceFiles;
	BOOL mUseFuzzySearch = false;
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FindInWkspcDlg)
  public:
  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual void OnOK();
	virtual void PopulateList() = 0;
	virtual void UpdateFilter(bool force = false) = 0;
	virtual int GetFilterTimerPeriod() const = 0;
	virtual void GetTooltipText(int itemRow, WTString& txt) = 0;
	virtual void GetTooltipTextW(int itemRow, CStringW& txt)
	{
		WTString str;
		GetTooltipText(itemRow, str);
		txt = str.Wide();
	}
	int GetInitialColumnWidth(int columnIdx, int defaultWidth) const;
	const CString& GetBaseRegName() const
	{
		return mWindowRegName;
	}
	void ScrollItemToMiddle(int itemIndex);
	void SetEditHelpText();
	void SetTooltipStyle(bool enableTooltips);

	static bool IsFuzzyAvailable();
	virtual bool GetUseFuzzySearchDefaultValue() const
	{
		return true;
	}
	bool IsFuzzyUsed() const;

	// Generated message map functions
	//{{AFX_MSG(FindInWkspcDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnDestroy();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSetfocusEdit();
	afx_msg void OnChangeFilterEdit();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnToggleWorkspaceRestriction();
	afx_msg void OnUseFuzzySearch();
	afx_msg void OnFindItem(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	bool mWaitingToFilter;
	const CString mWindowRegName;
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // FindInWkspcDlg_h__
