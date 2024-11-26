#if !defined(AFX_FileListCombo_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_)
#define AFX_FileListCombo_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FileListCombo.h : header file
//
#include "FastComboBox.h"

/////////////////////////////////////////////////////////////////////////////
// FileListCombo window

class FileListCombo : public CFastComboBox
{
	// Construction
  public:
	FileListCombo();
	virtual ~FileListCombo();

	// Attributes
  public:
	virtual void OnDropdown();

	// Operations

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FileListCombo)
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
	void UpdateFilter(const CStringW& txt);
	virtual bool IsVS2010ColouringActive() const;
	virtual void OnCloseup();

	// Generated message map functions
	//{{AFX_MSG(FileListCombo)
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FileListCombo_H__8E88DD9C_4D4E_41EA_89EE_B9E3062DF295__INCLUDED_)
