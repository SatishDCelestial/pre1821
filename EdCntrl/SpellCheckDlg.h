#if !defined(AFX_SPELLCHECKDLG_H__16812424_AFF1_4DAB_974B_6A2C7E638283__INCLUDED_)
#define AFX_SPELLCHECKDLG_H__16812424_AFF1_4DAB_974B_6A2C7E638283__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// SpellCheckDlg.h : header file
//

#include "VADialog.h"
#include "VAThemeDraw.h"

/////////////////////////////////////////////////////////////////////////////
// CSpellCheckDlg dialog

class CSpellCheckDlg : public CThemedVADlg
{
	// Construction
  public:
	CSpellCheckDlg(CWnd* pParent = NULL, const WTString& text = L""); // standard constructor

	// Dialog Data
	//{{AFX_DATA(CSpellCheckDlg)
	enum
	{
		IDD = IDD_SPELL
	};
	CThemedListBox m_suggListCtl;
	CThemedButton m_changeBtn;
	CThemedEdit mEdit;
	//}}AFX_DATA

	WTString mOriginalWord;
	WTString m_changeTo;
	CStringList m_suggestList;

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CSpellCheckDlg)
  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	                                                 //}}AFX_VIRTUAL

	// Implementation
  protected:
	// Generated message map functions
	//{{AFX_MSG(CSpellCheckDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnAdd();
	afx_msg void OnChange();
	afx_msg void OnIgnoreAll();
	afx_msg void OnOptions();
	afx_msg void OnSuggestionListSelChange();
	afx_msg void OnChangeEdit1();
	afx_msg void OnDblclkSuggestionList();
	afx_msg void OnIgnore();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	//}}AFX_MSG

	void UseOriginalWord();
	DECLARE_MESSAGE_MAP()
};

WTString SpellWordDlg(const WTString& text);

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SPELLCHECKDLG_H__16812424_AFF1_4DAB_974B_6A2C7E638283__INCLUDED_)
