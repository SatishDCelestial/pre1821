#if !defined(AFX_VABROWSESYM_H__29C9E3E5_A85B_4C9C_92AE_6C793C0851F2__INCLUDED_)
#define AFX_VABROWSESYM_H__29C9E3E5_A85B_4C9C_92AE_6C793C0851F2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// VABrowseSym.h : header file
//

#include "FindInWkspcDlg.h"
#include "FilterableSet.h"
#include "Menuxp\MenuXP.h"
#include "Foo.h"
#include "PooledThreadBase.h"
#include "afxwin.h"
#include "resource.h"

class DType;

/////////////////////////////////////////////////////////////////////////////
// VABrowseSym dialog

class VABrowseSym : public FindInWkspcDlg
{
	DECLARE_MENUXP()

	friend void VABrowseSymDlg();
	// Construction
  private:
	VABrowseSym(bool isRedisplay, CWnd* pParent = NULL); // standard constructor
	FilteredStringList m_data;

  protected:
	virtual void PopulateList();
	virtual void UpdateFilter(bool force = false);

  private:
	void UpdateTitle(uint64_t timing_filter_ms = 0, uint64_t timing_listview_ms = 0);
	void GetItemLocation(int itemRow, CStringW& file, int& lineNo);
	void GetItemLocation(int itemRow, CStringW& file, int& lineNo, WTString& sym);
	int GetWorkingSetCount() const;
	void GetDataForCurrentSelection();

	// Dialog Data
	//{{AFX_DATA(VABrowseSym)
	enum
	{
		IDD = IDD_VABROWSESYM
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VABrowseSym)
  public:
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	static void FsisLoader(LPVOID pVaBrowseSym);
	void PopulateListThreadFunc();
	virtual int GetFilterTimerPeriod() const;
	virtual void GetTooltipText(int itemRow, WTString& txt);
	virtual void GetTooltipTextW(int itemRow, CStringW& txt);
	void StopThreadAndWait();

	virtual bool GetUseFuzzySearchDefaultValue() const override
	{
		return false;
	}

	// Generated message map functions
	//{{AFX_MSG(VABrowseSym)
	virtual void OnOK();
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnCopy();
	afx_msg void OnDestroy();
	afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnRefactorFindRefs();
	afx_msg void OnToggleTypesFilter();
	afx_msg void OnToggleEditorSelectionOption();
	afx_msg void OnToggleCaseSensitivity();
	afx_msg void OnToggleTooltips();
	afx_msg void OnToggleExtraColumns();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#if defined(RAD_STUDIO)
	afx_msg void OnOpenFileDlg();
	afx_msg void OnVaOptions();
#endif
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
  private:
	DTypeList mDtypes;
	FilteredStringList::SymbolTypeFilter mSymbolFilter;
	FunctionThread mLoader;
	bool mFilterEdited;
	bool mStopThread;
	bool mIsRedisplayed = false;
};

extern void VABrowseSymDlg();

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VABROWSESYM_H__29C9E3E5_A85B_4C9C_92AE_6C793C0851F2__INCLUDED_)
