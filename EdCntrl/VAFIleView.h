#if !defined(AFX_VAFILEVIEW_H__E1E5F2EE_AE6E_48F6_BEDE_5A32874D61E3__INCLUDED_)
#define AFX_VAFILEVIEW_H__E1E5F2EE_AE6E_48F6_BEDE_5A32874D61E3__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// VAFileView.h : header file
//

#include "VADialog.h"
#include "SymbolListCombo.h"
#include "FileListCombo.h"
#include "resource.h"
#include "Menuxp\MenuXP.h"

/////////////////////////////////////////////////////////////////////////////
// VAFileView dialog

class VAFileView : public VADialog
{
	DECLARE_MENUXP()

  private:
	FileListCombo m_FilesListBox;
	SymbolListCombo m_SymbolsListBox;
	UINT_PTR mTimerId;

	// Construction
  public:
	VAFileView(CWnd* pParent = NULL); // standard constructor
	~VAFileView();
	WTString m_mru_reg;
	void Insert(CStringW str, int image, LPCWSTR param, UINT flags = 0);
	int GetItemImage(int nItem);
	void LoadState(LPCSTR key, BOOL save = FALSE);
	void SettingsChanged();

	void SetFocusToFis();
	void SetFocusToSis();
	void SetFocusToMru();
	void Invalidate(BOOL bErase = TRUE)
	{
		if (m_FilesListBox.GetSafeHwnd())
			m_FilesListBox.Invalidate(bErase);
		if (m_SymbolsListBox.GetSafeHwnd())
			m_SymbolsListBox.Invalidate(bErase);
		if (GetSafeHwnd())
			VADialog::Invalidate(bErase);
	}
	void GotoFiles(bool showDropdown);
	void GotoSymbols(bool showDropdown);
	// Dialog Data
	//{{AFX_DATA(VAFileView)
	// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA


	virtual void Layout(bool dpiChange = false) override;

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VAFileView)
  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	//}}AFX_VIRTUAL

	virtual void OnDpiChanged(DpiChange change, bool& handled);

	// Implementation
  protected:
	// Generated message map functions
	//{{AFX_MSG(VAFileView)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	virtual BOOL OnInitDialog();
	afx_msg void OnMruDoubleClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMruRightClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMruLeftClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGoto();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	//}}AFX_MSG
	void DisplayContextMenu(CPoint pos);
	void OnContextMenu(CWnd* pWnd, CPoint pos);
	HTREEITEM GetItemFromPos(POINT pos) const;
	void OnHighlightStateChanged(uint newstate) const;
	DECLARE_MESSAGE_MAP()
};

// reasons ordered by decreasing priority
enum MruFileEntryReason
{
	mruFileSelected,
	mruFileEdit,
	mruFileOpen,
	mruFileFocus,
	mruFileEntryCount
};
enum MruMethodEntryReason
{
	mruMethodEdit,
	mruMethodNav,
	mruMethodEntryCount
};
void VAProjectAddFileMRU(const CStringW& file, MruFileEntryReason reason);
void VAProjectAddScopeMRU(LPCSTR scope, MruMethodEntryReason reason);
inline void VAProjectAddScopeMRU(const WTString& scope, MruMethodEntryReason reason)
{
	return VAProjectAddScopeMRU(scope.c_str(), reason);
}
void AddMRU(const CStringW& file, CStringW scope, int type, int line, BOOL modified);
void LoadProjectMRU(const CStringW& project);

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VAFILEVIEW_H__E1E5F2EE_AE6E_48F6_BEDE_5A32874D61E3__INCLUDED_)
