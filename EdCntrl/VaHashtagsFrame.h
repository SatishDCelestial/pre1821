#pragma once

#include "VAThemeDraw.h"
#include "ParseWorkItem.h"
#include "ReferencesTreeSubclass.h"
#include "mctree\ColumnTreeCtrl.h"
#include "TransparentBitmapButton.h"
#include "FOO.H"
#include "EdCnt_fwd.h"
#include "tree_state_hashtags.h"
#include "VsToolWindowPane.h"
#include <mutex>
#include "CtrlBackspaceEdit.h"
#include "FilterEdit.h"
#include "CWndDpiAware.h"

class HashtagsColumnTreeWnd : public CColumnTreeWnd2
{
  public:
	HashtagsColumnTreeWnd(VaHashtagsFrame& parent) : m_parent(parent)
	{
	}
	~HashtagsColumnTreeWnd()
	{
		DestroyWindow();
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
	afx_msg void OnSelect(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()

	virtual void OnDpiChanged(DpiChange change, bool& handled);

  private:
	VaHashtagsFrame& m_parent;
};

class EditSearchCtrl : public CThemedEditBrowse
{
  public:
	void ThemeChanged();

	afx_msg BOOL OnChange();

	virtual void OnBrowse() override
	{
		// clear search box
		SetWindowTextW(*this, L"");
		SetFocus();
	}

	virtual void PreSubclassWindow() override
	{
		__super::PreSubclassWindow();
		EnableBrowseButton();
		SetDrawButtonBorder(false);
		ThemeChanged();
	}

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(EditSearchCtrl, CThemedEditBrowse)
	ON_CONTROL_REFLECT_EX(EN_CHANGE, OnChange)
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)
};

class VaHashtagsFrame : public VADialog
{
	DECLARE_MENUXP()

  public:
	VaHashtagsFrame(HWND hWndParent);
	virtual ~VaHashtagsFrame();

	// synchronous update
	void IdePaneActivated();

	DWORD QueryStatus(DWORD cmdId) const;
	HRESULT Exec(DWORD cmdId);

	void RefreshWorker(EdCntPtr ed, int edModCookie);
	void RefreshAsync();
	void Clear();

	static std::vector<DType> GetUnsortedHashtagItems();

	void OnDoubleClickTree();
	void ThemeUpdated();

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

	bool GoToSelectedItem();
	HTREEITEM mtree__GetChildItem(HTREEITEM item);

	DECLARE_MESSAGE_MAP()

  protected:
	virtual BOOL OnInitDialog() override;
	virtual void DoDataExchange(CDataExchange* pDX) override;
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) override;

	void RecalcLayout(bool init);

	afx_msg void OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnItemExpanding(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR idEvent);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);
	void OnChangeNameEdit();

	virtual void OnDpiChanged(DpiChange change, bool& handled);

  private:
	void SubstituteButton(int buttonIdx, int idRes, LPCSTR buttonTxt, int idImageRes, int idSelectedImageRes = 0,
	                      int idFocusedImageRes = 0, int idDisabledImageRes = 0, bool isCheckbox = false);
	void PrepareImages();
	bool GoToItem(HTREEITEM item);
	WTString GetTooltipText(HTREEITEM item);

	void GotoNextItem();
	void GotoPreviousItem();
	void GotoNextItemInGroup();
	void GotoPreviousItemInGroup();
	void OnToggleGroupByFile();

	void SelectItem(HTREEITEM item);
	void BuildNavigationList(HTREEITEM item);
	void Repopulate();
	void DoRepopulateByTag(const StrMatchOptions&);
	void DoRepopulateByFile(const StrMatchOptions&);
	bool ItemFilterTest(CStringW thisHashtag, const StrMatchOptions&);
	void SortHashtags(std::vector<DType>& hashtags);
	void RunExclusionRules(std::vector<DType>& hashtags);

	LRESULT OnInsert(WPARAM wp, LPARAM lp);

	VsToolWindowPane mParent;

	HashtagsColumnTreeWnd m_treeSubClass;
	CColumnTreeCtrl2& m_tree; // m_treeSubClass doesn't derive from CTreeCtrl any more; CTreeCtrl is aggregated now

	mutable CCriticalSection mVecLock;
	std::vector<DType> m_hashtags;
	bool mShowDimHiddenItems;
	bool mShowTooltips;

	enum
	{
		RefsButtonCount = 5
	};
	CheckableBitmapButton mButtons[RefsButtonCount];
	CToolTipCtrl mTooltips;
	FilterEdit<CtrlBackspaceEdit<EditSearchCtrl>> mFilterCtrl;

	EdCntPtr mLastEd = nullptr;
	int mLastEdModCookie = 0;
	std::unique_ptr<tree_state_hashtags::storage> mTreeStateStorage;

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

	CStringW mCurFilter;
	bool mIsMultiFilter = false;
	bool mActivated = false;

	typedef std::vector<std::tuple<CStringW, int, uint>> postponed_items_t; // <item text, icon, id>
	std::unordered_map<HTREEITEM, postponed_items_t> postponed_items;
	std::recursive_mutex postponed_items_mutex;
	typedef std::lock_guard<std::recursive_mutex> postponed_items_mutex_lock_t;
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

// RefreshHashtags
// ----------------------------------------------------------------------------
// Use for asynchronous update.
// ex: g_ParserThread->QueueParseWorkItem(new RefreshHashtags());
//
class RefreshHashtags : public ParseWorkItem
{
  public:
	RefreshHashtags(EdCntPtr ed);
	virtual void DoParseWork();

  private:
	RefreshHashtags();

	EdCntPtr mEd;
	int mEdModCookie;
};
