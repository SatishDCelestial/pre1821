#if !defined(AFX_VAFINDUSAGE_H__00ED2A46_285D_4847_81D3_69C3581FB17A__INCLUDED_)
#define AFX_VAFINDUSAGE_H__00ED2A46_285D_4847_81D3_69C3581FB17A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FindUsageDlg.h : header file
//

#include "IFindTargetWithTree.h"
#include "ReferencesWndBase.h"
#include "resource.h"
#include "TransparentBitmapButton.h"
#include <functional>

/////////////////////////////////////////////////////////////////////////////
// FindUsageDlg dialog
/*
We used to have a single class named FindUsageDlg that was used for Rename
references and Find references.

I split up FindUsageDlg so that Rename and Find are now independent classes
(RenameReferencesDlg and FindUsageDlg).  They share a base class ReferencesWndBase
that has the common implementation - there is no more mIsRenameDlg member.

I created the IFindTargetWithTree interface so that the FindTextDlg could
work with any class that implements the interface - not just FindUsageDlg.
We won't need a separate Find dlg for the outline when we start adding find
to it.

The Rename references class is a complete window - it includes a dialog frame.
The FindUsageDlg class is not a complete window - it has no frame; it is not a
dialog.  I should have renamed it while I was changing things...

FindUsageDlg is actually hosted by one of the FindReferencesResultsFrame
derived classes.  There are different frame classes depending on IDE and
whether or not the results are the primary or secondary (cloned) results.
Most of the differences in the ResultsFrame classes are in the ctors: for vc6
we create our own window while in VS we use an IDE provided window.  The
FindUsageDlg class is pretty insulated from that.
*/
class FindUsageDlg : public ReferencesWndBase, public IFindTargetWithTree<CColumnTreeCtrl>
{
	friend class FindReferencesResultsFrame;
	friend class PrimaryResultsFrameVc6;

  public:
	// Construction
	FindUsageDlg();
	FindUsageDlg(const FindReferences& refsToCopy, bool isClone, DWORD filter = 0xffffffff);

	virtual void OnCancel();
	void Find(bool next = true);
	void PrepareImages();

	enum
	{
		IDD = IDD_VAFINDUSAGES
	};

	// Overrides
  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	virtual void OnSearchBegin();
	virtual void OnSearchComplete(int fileCount, bool wasCanceled);
	void GotoNextItem();
	void GotoPreviousItem();
	void SelectItem(HTREEITEM item);
	void Show(int showType);
	void Hide(bool changeParent = false);
	WTString GetTooltipText(HTREEITEM item);
	virtual void UpdateStatus(BOOL done, int fileCount);
	virtual void OnTreeEscape();
	virtual LRESULT OnTreeKeyDown(WPARAM wParam, LPARAM lParam);

	// Generated message map functions
	//{{AFX_MSG(FindUsageDlg)
	virtual BOOL OnInitDialog();

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	void OnToggleHighlight();
	void OnCloseButton();
	LRESULT OnColumnResized(WPARAM wparam, LPARAM lparam);
	LRESULT OnColumnShown(WPARAM wparam, LPARAM lparam);
	LRESULT OnEnableHighlight(WPARAM wparam, LPARAM lparam);
	afx_msg void OnDrawItem(int id, LPDRAWITEMSTRUCT dis);
	virtual void OnPopulateContextMenu(CMenu& contextMenu);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnTreeKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG

	void OnFind();
	bool HasFindText();
	void RemoveSelectedItem();
	void OnEnableHighlight(bool enable);
	void OnCopy();
	void OnCopyAll();
	void OnCut();
	void OnToggleTooltips();
	void OnToggleProjectNodes();
	void OnToggleLineNumbers();
	void OnToggleDirtyNav();
	void ExpandOrCollapseNodes(HTREEITEM treeItem, UINT nCode, bool applyToProjectNodes);
	void OnExpandAll();
	void OnCollapseAll();
	void OnCollapseFileNodes();
	void OnCloneResults();
	void OnFindNextCmd()
	{
		OnFindNext();
	}
	void OnFindPrevCmd()
	{
		OnFindPrev();
	}
	void OnToggleFilterInherited();
	void OnToggleAllProjects();
	void OnToggleFilterComments();
	void ReadLastSettingsFromRegistry();
	void SubstituteButton(int buttonIdx, int idRes, LPCSTR buttonTxt, int idImageRes, int idSelectedImageRes = 0,
	                      int idFocusedImageRes = 0, int idDisabledImageRes = 0, bool isCheckbox = false);
	void BuildNavigationList(HTREEITEM item);
	CStringW CopyText(HTREEITEM hItem, CStringW spacer);
	void OnCopy(HTREEITEM hItem);
	WTString GetCountString();

	virtual void OnDpiChanged(DpiChange change, bool& handled);
	virtual void OnFontSettingsChanged(VaFontTypeFlags changed, bool& handled);

  public: // IFindTargetWithTree
	virtual CWnd* GetCWnd()
	{
		return this;
	}
	virtual void OnFindNext();
	virtual void OnFindPrev();
	virtual CStringW GetFindText() const
	{
		return mFindWhat;
	}
	virtual void SetFindText(const CStringW& text)
	{
		mFindWhat = text;
		m_treeSubClass.last_markall_rtext = text;
	}
	virtual void MarkAll();
	virtual void UnmarkAll(UnmarkType what = unmark_markall);
	virtual void OnDoubleClickTree();
	virtual void GoToSelectedItem()
	{
		ReferencesWndBase::GoToSelectedItem();
	}
	virtual bool IsFindCaseSensitive() const
	{
		return mFindCaseSensitive;
	}
	virtual void SetFindCaseSensitive(bool caseSensitive)
	{
		mFindCaseSensitive = caseSensitive;
		m_treeSubClass.last_markall_matchcase = caseSensitive;
	}
	virtual bool IsFindReverse() const
	{
		return mFindInReverseDirection;
	}
	virtual void SetFindReverse(bool findReverse)
	{
		mFindInReverseDirection = findReverse;
	}
	virtual bool IsMarkAllActive() const
	{
		return mMarkAll;
	}
	virtual void SetMarkAll(bool active)
	{
		mMarkAll = active;
	}
	virtual CColumnTreeWnd& GetTree()
	{
		return m_treeSubClass;
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

  private:
	bool mInteractivelyHidden;
	const bool mHasClonedResults;
	CStringW mFindWhat;
	bool mFindCaseSensitive;
	bool mFindInReverseDirection;
	bool& mMarkAll; // currently only a flag whether to do 'markall' on newly inserted items
	enum
	{
		RefsButtonCount = 7
	};
	CheckableBitmapButton mButtons[RefsButtonCount];
	CToolTipCtrl mTooltips;
	struct NavItem
	{
		NavItem(HTREEITEM item, int refData) : mItem(item), mRefData(refData)
		{
		}
		HTREEITEM mItem;
		int mRefData;
	};
	typedef std::vector<NavItem> NavList;
	NavList mNavigationList;
	int mFileCount;
	int mFileNodesRemoved;
	int mRefNodesRemoved;
	bool mOkToUpdateCount;
	int mNumberOfTreeLevels; // [case 142050]
#ifdef RAD_STUDIO
	HWND mParent; // CPP builder docking
	std::function<void(HWND, HWND)> on_parent_changed;
#endif

	std::optional<std::chrono::time_point<std::chrono::steady_clock>> mStartTime;
	std::optional<std::chrono::time_point<std::chrono::steady_clock>> mEndTime;
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VAFINDUSAGE_H__00ED2A46_285D_4847_81D3_69C3581FB17A__INCLUDED_)
