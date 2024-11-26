#if !defined(AFX_SYMBOLLISTCOMBO_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_)
#define AFX_SYMBOLLISTCOMBO_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// SymbolListCombo.h : header file
//
#include "FastComboBox.h"
#include "PooledThreadBase.h"

/////////////////////////////////////////////////////////////////////////////
// SymbolListCombo window

class SymbolListCombo : public CFastComboBox
{
	// Construction
  public:
	SymbolListCombo();
	virtual ~SymbolListCombo();

	// Attributes
  public:
	virtual void OnDropdown();

	// Operations

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(SymbolListCombo)
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual void OnSelect();
	virtual WTString GetItemTip(int nRow) const;
	virtual CStringW GetItemTipW(int nRow) const;
	virtual void OnEditChange();
	virtual void Init();
	virtual void GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const;
	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);
	virtual bool IsVS2010ColouringActive() const;
	virtual void OnCloseup();

  private:
	void UpdateFilter();
	void PopulateList();
	static void SisLoader(LPVOID pVaBrowseSym);
	void PopulateListThreadFunc();
	void StopThreadAndWait();
	void FinishDropdown();

	// Generated message map functions
	//{{AFX_MSG(SymbolListCombo)
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()

  private:
	CStringW mFilterText;
	FunctionThread mLoader;
	bool mStopThread;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SYMBOLLISTCOMBO_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_)
