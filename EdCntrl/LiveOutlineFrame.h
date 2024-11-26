#pragma once

#include "VADialog.h"
#include "VsToolWindowPane.h"
#include "WTString.h"
#include "ParseWorkItem.h"
#include "Menuxp\MenuXP.h"
#include "ITreeDropHandler.h"
#include "FileOutlineFlags.h"
#include "FileLineMarker.h"
#include "EdCnt_fwd.h"

class DragDropTreeCtrl;

class LiveOutlineFrame : public VADialog, public ITreeDropHandler
{
	DECLARE_MENUXP()

  public:
	LiveOutlineFrame(HWND hWndParent);
	~LiveOutlineFrame();

	// synchronous update
	void IdePaneActivated();

	void Clear();
	void Insert(LineMarkers* pMarkers);
	bool IsAutoUpdateEnabled() const
	{
		return mAutoUpdate;
	}
	bool IsWindowFocused() const;
	bool IsMsgTarget(HWND hWnd) const;

	void HighlightItemAtLine(int line);
	void RequestRefresh(UINT delay = 50u);

	void OnEdCntClosing(const EdCntPtr& edcnt);
	void ThemeUpdated();
	CStringW GetOutlineState() const;
	LineMarkersPtr GetMarkers() const;

	// called from package for IDE command integration
	DWORD QueryStatus(DWORD cmdId) const;
	HRESULT Exec(DWORD cmdId);

	afx_msg void OnGoto();
	afx_msg void OnCopy();
	afx_msg void OnCut();
	afx_msg void OnPaste();
	afx_msg void OnDelete();
	afx_msg void OnSelectItemInEditor();
	afx_msg void OnDoubleClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnToggleAutoupdate();
	afx_msg void OnToggleAllowDrag();
	afx_msg void OnToggleAutoExpand();
	afx_msg void OnFilterAll();
	afx_msg void OnFilterNone();
	afx_msg void OnFilterToggleIncludes();
	afx_msg void OnFilterToggleComments();
	afx_msg void OnFilterToggleGlobals();
	afx_msg void OnFilterToggleTags();
	afx_msg void OnFilterToggleIfdefs();
	afx_msg void OnFilterToggleGroups();
	afx_msg void OnFilterToggleFunctions();
	afx_msg void OnFilterToggleVariables();
	afx_msg void OnFilterToggleMacros();
	afx_msg void OnFilterToggleRegions();
	afx_msg void OnFilterToggleEnums();
	afx_msg void OnFilterToggleTypes();
	afx_msg void OnFilterToggleNamespaces();
	afx_msg void OnFilterToggleMsgMap();
	afx_msg void OnSelectLayoutTree();
	afx_msg void OnSelectLayoutList();
	afx_msg void OnSaveAsFilter1();
	afx_msg void OnSaveAsFilter2();
	afx_msg void OnLoadFilter1();
	afx_msg void OnLoadFilter2();
	afx_msg void OnRefreshNow();
	afx_msg void OnRefactorRename();
	afx_msg void OnRefactorFindRefs();
	afx_msg void OnRefactorAddMember();
	afx_msg void OnRefactorAddSimilarMember();
	afx_msg void OnRefactorChangeSignature();
	afx_msg void OnRefactorCreateImplementation();
	afx_msg void OnRefactorCreateDeclaration();
	afx_msg void OnSurroundCommentC();
	afx_msg void OnSurroundUncommentC();
	afx_msg void OnSurroundCommentCPP();
	afx_msg void OnSurroundUncommentCPP();
	afx_msg void OnSurroundIfdef();
	afx_msg void OnSurroundRegion();
	afx_msg void OnSurroundNamespace();
	afx_msg void OnSurroundReformat();
	afx_msg void OnLogOutline();

	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg LRESULT OnInsert(WPARAM wp, LPARAM lp);
	afx_msg void OnToggleDisplayTooltips();

	// ITreeDropHandler implementation
	virtual bool IsAllowedToStartDrag();
	virtual bool IsAllowedToDrop(HTREEITEM target, bool afterTarget);
	virtual void CopyDroppedItem(HTREEITEM target, bool afterTarget);
	virtual void MoveDroppedItem(HTREEITEM target, bool afterTarget);
	virtual void OnTripleClick();

	DECLARE_MESSAGE_MAP()

  protected:
	virtual BOOL OnInitDialog();
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

  private:
	WTString GetTooltipText(HTREEITEM item) const;
	void MoveToEditorLine(int line) const;
	void MoveToEditorCp(int cp) const;
	void SelectLinesInEditor(int startLine, int endLine) const;
	void SelectCharsInEditor(int startChar, int endChar) const;
	void Refresh(WTString edBuf, EdCntPtr ed, int edModCookie = -1);
	void UpdateFilterSet(bool retainVertScrollPos = true);
	HTREEITEM GetItemFromPos(POINT pos) const;
	CStringW GetEditorSelection() const;
	void DeleteEditorSelection() const;
	void ClearWithoutRedraw();
	CStringW GetSelectedItemsText(bool deleteFromEditor);
	void InsertSelectedItemsText(int pasteCp, int pasteLine, bool deleteFromEditor);
	void InsertToTree(HTREEITEM parentItem, LineMarkers::Node& node, bool nestedDisplay, UINT expandToLine,
	                  HTREEITEM* bold_this_item, HTREEITEM* select_this_item);
	void DoRefactor(ULONG f);
	void PopulateSurroundMenu(CMenu& menu, FileLineMarker* mkr);
	void DoSurround(UINT msg, UINT cmd);
	void OnModificationComplete();
	DType GetContext(HTREEITEM item);
	HTREEITEM GetNextTreeItem(HTREEITEM hti) const;
	void ExpandOrCollapseAll(UINT expandFlag);
	void OnExpandAll();
	CStringW CopyHierarchy(HTREEITEM item, CStringW prefix);
	void CopyAll();
	void OnCollapseAll();
	void OnCopyAll();
	friend class RefreshFileOutline;

	DragDropTreeCtrl* mTree;
	VsToolWindowPane mParent;
	LineMarkersPtr mMarkers;
	int mEdModCookie;
	EdCntPtr mEdcnt;
	EdCntPtr mEdcnt_last_refresh; // EdCnt that initiated last refresh
	FileOutlineFlags mFilterFlags;
	FileOutlineFlags mFilterFlagsSaved1; // only necessary for context menu item logic
	FileOutlineFlags mFilterFlagsSaved2; // only necessary for context menu item logic
	bool mLayoutAsList;
	bool mAllowDragging;
	bool mAutoUpdate;
	UINT_PTR mRefreshTimerId;
	HTREEITEM mLastBolded;
	bool mInTransitionPeriod; // refresh in progress for new edcnt
	bool mUpdateIsPending;    // editor modified via outline command and outline is invalid
	int mEdModCookieAtDragStart;
	bool mSelectItemDuringRefresh; // usually don't select curitem during refresh
	bool mAutoExpand;
	bool mStripScopeFromGrpdMthds;
	bool mDisplayTooltips; // display scope tooltips - the ones that shows the source code. the one that shows lines
	                       // that don't fit in the window horizontally will always be shown
	mutable CCriticalSection mMarkersLock;
};

// RefreshFileOutline
// ----------------------------------------------------------------------------
// Parser thread work job used for tl_onEditFocus and tl_onUpdate.
// Use for asynchronous update.
// ex: g_ParserThread->QueueParseWorkItem(new RefreshFileOutline(this));
//
class RefreshFileOutline : public ParseWorkItem
{
  public:
	RefreshFileOutline(EdCntPtr ed);
	virtual void DoParseWork();

  private:
	RefreshFileOutline();

	EdCntPtr mEd;
	WTString mEdBuf;
	int mEdModCookie;
};
