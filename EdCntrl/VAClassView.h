#if !defined(AFX_VACLASSVIEW_H__E5A0429E_3F2E_43AA_9D9C_14C6F9E8BD03__INCLUDED_)
#define AFX_VACLASSVIEW_H__E5A0429E_3F2E_43AA_9D9C_14C6F9E8BD03__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// VAClassView.h : header file
//
#include "VADialog.h"
#include "WTString.h"
#include "foo.h"
#include "resource.h"
#include "DummyCombo.h"

class TreeSubClass;
class DType;

#define TT_TEXT                                                                                                        \
	"Hovering Class Browser displays information for current symbol when you hover. Right click an entry to Goto, "    \
	"Copy or change options."

/////////////////////////////////////////////////////////////////////////////
// CVAClassView dialog

class CVAClassView : public VADialog
{
	// Construction
  public:
	CVAClassView(CWnd* pParent = NULL); // standard constructor
	~CVAClassView();

	BOOL m_fileView;
	BOOL m_lock;
	WTString m_lastSym;
	// Dialog Data
	//{{AFX_DATA(CVAClassView)
	//}}AFX_DATA

	void SetTitle(LPCSTR title, int img = 0);
	void GetOutline(BOOL force);
	void EditorClicked();
	void UpdateHcb();
	void ClearHcb(bool clearTree = true);
	void CheckHcbForDependentData();
	void ThemeUpdated();
	void Invalidate(BOOL bErase /* = TRUE */)
	{
		if (m_tree.GetSafeHwnd())
			m_tree.Invalidate(bErase);
		if (m_titlebox.GetSafeHwnd())
			m_titlebox.Invalidate(bErase);
		VADialog::Invalidate(bErase);
	}
	WTString GetLastInfoString() const
	{
		return mInfoForLogging;
	}
	void FocusHcb();
	void FocusHcbLock();
	static bool IsFileWithIncludes(int lang);

	BOOL HcbHasFocus() const
	{
		return GetFocus()->GetSafeHwnd() == m_tree.m_hWnd && m_tree.m_hWnd;
	}
	afx_msg void OnToggleSymbolLock();

	// called from package for IDE command integration
	DWORD QueryStatus(DWORD cmdId);
	HRESULT Exec(DWORD cmdId);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CVAClassView)
  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

	virtual void OnDpiChanged(DpiChange change, bool& handled);

	// Implementation
  protected:
	// Generated message map functions
	//{{AFX_MSG(CVAClassView)
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHcbDoubleClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	virtual BOOL OnInitDialog();
	afx_msg void OnHcbRightClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHcbItemExpanding(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHcbTvnSelectionChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis);
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	void GotoItemSymbol(HTREEITEM item, GoAction action = Go_Default);
	WTString GetTooltipText(HTREEITEM item);
	void DisplayContextMenu(CPoint pos, DType* data);
	void ExecContextMenuCommand(UINT menuCmd, DType* symData);
	HTREEITEM GetItemFromPos(POINT pos) const;
	DType* GetSelItemClassData();
	void HierarchicalBrowse(WTString& scope, DType* data, DType* dataScope, WTString& sym);
	void FlatBrowse();
	bool ListIncludes(HTREEITEM hParentItem, const CStringW& curFile, bool didLock);
	void ExpandItemIncludes();
	bool IsCircular(HTREEITEM hti, const CStringW& path, const CStringW& childItemPath);
	CStringW CopyHierarchy(HTREEITEM item, CStringW prefix);
	void ExpandAll();
	bool ManageWaitDialog(CComPtr<IVsThreadedWaitDialog>& waitDlg, int& expandAllCounter, DWORD& tickCount);
	void SetExpandAllMode(bool val)
	{
		mExpandAllMode = val;
	}
	HTREEITEM GetNextTreeItem(HTREEITEM hti, HTREEITEM hti_selected);
	void CopyAll();
	void RemoveExpandable(HTREEITEM item);
	HTREEITEM InsertTreeItem(LPTVINSERTSTRUCTW lpInsertStruct);
	HTREEITEM InsertTreeItem(UINT nMask, LPCWSTR lpszItem, int nImage, int nSelectedImage, UINT nState, UINT nStateMask,
	                         LPARAM lParam, HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
	HTREEITEM InsertTreeItem(LPCWSTR lpszItem, HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
	CStringW GetTreeItemText(HWND hWnd, HTREEITEM hItem);

	DummyCombo m_titlebox;
	CTreeCtrl m_tree;
	TreeSubClass* mTheSameTreeCtrl;
	WTString mInfoForLogging;
	uint mFileIdOfDependentSym;
	BOOL mIsExpanding;

	DTypeList mRootIncludeList, mRootIncludeByList;
	CStringW mIncludeCurFile;
	int mIncludeEdLine;
	bool mExpandAllMode;
};

void RefreshHcb(bool force = false);
void QueueHcbRefresh();
void QueueHcbIncludeRefresh(bool force, bool immediate = false);

extern CVAClassView* g_CVAClassView;
extern WTString ClassViewSym;

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VACLASSVIEW_H__E5A0429E_3F2E_43AA_9D9C_14C6F9E8BD03__INCLUDED_)
