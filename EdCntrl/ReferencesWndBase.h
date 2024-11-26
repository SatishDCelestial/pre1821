#ifndef ReferencesWndBase_h__
#define ReferencesWndBase_h__

#include "resource.h"
#include "VADialog.h"
#include "ReferencesTreeSubclass.h"
#include "FindReferences.h"
#include "MenuXP\MenuXP.h"
#include "FindReferencesThread.h"
#include "VAThemeDraw.h"

class CColumnTreeCtrl;

#if defined(RAD_STUDIO)
const DWORD kRADDesignerRefBit = 0x40000000;
#endif 

struct TreeReferenceInfo
{
	CStringW mText;
	int mIconType;
	HTREEITEM mParentTreeItem;
	int mRefId;
	int mType;
	const FindReference* mRef;
};

class ReferencesWndBase : public CThemedVADlg
{
	friend class ReferencesThreadObserver;
	friend class ReferencesTreeSubclass;
	DECLARE_MENUXP()

  protected:
	ReferencesWndBase(const char* settingsCategory, UINT idd, CWnd* pParent, bool displayProjectNodes,
	                  Freedom fd = fdAll, UINT nFlags = flDefault);
	ReferencesWndBase(const FindReferences& refsToCopy, const char* settingsCategory, UINT idd, CWnd* pParent,
	                  bool displayProjectNodes, Freedom fd = fdAll, UINT nFlags = flDefault, DWORD filter = 0xffffffff);
	virtual ~ReferencesWndBase();

  public:
	virtual BOOL OnInitDialog(BOOL doProgressRepos);

  private:
	using VADialog::OnInitDialog;

  public:
	virtual void UpdateStatus(BOOL done, int fileCount) = 0;
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual void OnSearchBegin();
	virtual void OnSearchComplete(int fileCount, bool wasCanceled);
	void GoToSelectedItem();
	void OnToggleFilterInherited();
	void OnToggleFilterComments();
	void OnToggleFilterIncludes();
	void OnToggleFilterUnknowns();
	void OnToggleFilterDefinitions();
	void OnToggleFilterDefinitionAssigns();
	void OnToggleFilterReferences();
	void OnToggleFilterReferenceAssigns();
	void OnToggleFilterScopeReferences();
	void OnToggleFilterJsSameNames();
	void OnToggleFilterAutoVars();
	void OnToggleFilterCreations();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);
	DWORD GetFilter();
	virtual bool GetCommentState();
	virtual void RegisterReferencesControlMovers();

	virtual void SetSearchProgressFromAnyThread(int i);

	DECLARE_MESSAGE_MAP()

  protected:
	void PopulateListFromRefs();
	virtual void FindCurrentSymbol(const WTString& symScope, int typeImgIdx);
#if defined(RAD_STUDIO)
	virtual bool RAD_FindNextSymbol(const WTString& symScope, int typeImgIdx, unsigned int delay);
#endif
	virtual void OnDoubleClickTree();
	virtual void OnTreeEscape()
	{
	}
	static void GoToSelectedItemCB(LPVOID lpParam);
	void OnRefresh();
	void InspectContents(HTREEITEM item);
	void InspectReferences();
	void RemoveAllItems();
	virtual void GoToItem(HTREEITEM item);
	virtual void OnOK();
	virtual void OnCancel();
	virtual void OnPopulateContextMenu(CMenu& contextMenu)
	{
	}
	void SetSharedFileBehavior(FindReferencesThread::SharedFileBehavior b);
	virtual LRESULT OnAddReference(WPARAM wparam, LPARAM lparam);
	virtual LRESULT OnAddFileReference(WPARAM wparam, LPARAM lparam);
	virtual LRESULT OnSearchProgress(WPARAM prog, LPARAM);

  private:
	void EnsureLastItemVisible();
	void ClearThread();
	afx_msg void OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	LRESULT OnShowProgress(WPARAM wparam, LPARAM lparam);
	LRESULT OnAddProjectGroup(WPARAM wparam, LPARAM lparam);
	LRESULT OnSearchCompleteMsg(WPARAM fileCount, LPARAM canceled);
	LRESULT OnHiddenRefCount(WPARAM hiddenRefCount, LPARAM);

  protected:
	WTString m_symScope;
	ReferencesTreeSubclass m_treeSubClass;
	CColumnTreeCtrl& m_tree; // m_treeSubClass doesn't derive from CTreeCtrl any more; CTreeCtrl is aggregated now
	FindReferencesThread* mFindRefsThread = nullptr;
	FindReferencesPtr mRefs;
	const CString mSettingsCategory;
	CEdit* mEdit = nullptr;
	CProgressCtrl* mProgressBar = nullptr;
	std::atomic_int mLastSearchPositionSent = -1;
	bool mIgnoreItemSelect = false;
	bool mDisplayProjectNodes = false;
	bool mSelectionFixed = false;
	int mReferencesCount = 0;
	int mRefTypes[FREF_Last] = {0}; // The nr of found references per type
	bool mHasOverrides = false;
	bool mHonorsDisplayTypeFilter = false;
	DWORD mDisplayTypeFilter = 0;
	HTREEITEM mLastRefAdded = nullptr;
	DWORD mHiddenReferenceCount = 0;
	bool current_file_is_in_a_project_group = false; // an info needed to estimate findref item depth

  private:
	FindReferencesThread::SharedFileBehavior mSharedFileBehavior;
};

enum
{
	column_reference,
#ifdef ALLOW_MULTIPLE_COLUMNS
	column_line,
	column_type,
	column_context,
#endif

	__column_countof
};

extern const uint WM_SHOW_PROGRESS;
extern const DWORD kFileRefBit;
#define IS_FILE_REF_ITEM(id) ((id & kFileRefBit) != 0)

#endif // ReferencesWndBase_h__
