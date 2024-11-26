#pragma once

#include "VADialog.h"
#include "resource.h"
#include "afxwin.h"
#include "afxcmn.h"

#include <functional>
#include <typeinfo>

#include "VAThemeDraw.h"

struct DLUSpacing
{
	static const int DlgMargins = 7;
	static const int LabelOffset = 3;
	static const int RelatedGap = 4;
	static const int UnrelatedGap = 7;
	static const int GroupBoxTopMargin = 11;
	static const int GroupBoxLeftMargin = 6;
	static const int GroupBoxBottomMargin = 7;

	static int XToPix(HWND hWnd, int value);
	static int YToPix(HWND hWnd, int value);
};

struct DLUHeight
{
	static const int LabelSpace = 8;
	static const int CheckRadio = 10;
	static const int EditButton = 14;
};

enum ctl_row_order
{
	RowOrder_TopToBottom = 0,
	RowOrder_BottomToTop = 0xFF,
	RowOrder_None = 0xFFFF
};

#define VA_GenericTreeExCtlProperty "__VA_gtree_exctl"

struct GenericTreeExtraCtl
{
	typedef std::function<void(CWnd* ctl)> CWndFnc;

	enum ctl_type
	{
		Spacer,           // adds no control to the row, but moves top offset (default height is 8 DLUs)
		Label,            // adds a CThemedStaticNormal (default height is 8 DLUs)
		CheckBox,         // adds a CThemedCheckBox control (default height is 10 DLUs)
		RadioBtn,         // adds a CThemedRadioButton (default height is 10 DLUs)
		PushBtn,          // adds a CThemedButton (default height is 14 DLUs)
		EditCtl,          // adds a CThemedEdit (default height is 14 DLUs)
		DimEditCtl,       // adds a CThemedDimEdit (default height is 14 DLUs)
		EditComboBox,     // adds a CThemedComboBox (default height is 14 DLUs)
		DropDownComboBox, // adds a CThemedComboBox (default height is 14 DLUs)
		                  // Group			// [TODO] adds a Group Box (height is calculated by content)
	};

	enum : UINT
	{
		CtlIDAuto = 0xFFFFFFFF
	};

	std::shared_ptr<CStringW> TextW;
	std::shared_ptr<WTString> TextA;

	ctl_type CtlType = Spacer;
	UINT CtlID = CtlIDAuto; // starts at mParams.mFirstCtlExId

	CRect Dynamics;  // in percents of tree size
	CRect Placement; // in DTLs
	bool PlacementRelative = false;

	CWnd* Wnd = nullptr;

	std::vector<CWndFnc> OnCreate;
	std::vector<CWndFnc> OnChange;

	GenericTreeExtraCtl& SetCustomCtlID(UINT id)
	{
		CtlID = id;
	}
	GenericTreeExtraCtl& SetType(ctl_type type)
	{
		CtlType = type;
		return *this;
	}
	GenericTreeExtraCtl& SetText(LPCSTR text)
	{
		TextW.reset();
		if (text)
			TextA.reset(new WTString(text));
		return *this;
	}
	GenericTreeExtraCtl& SetText(LPCWSTR text)
	{
		TextA.reset();
		if (text)
			TextW.reset(new CStringW(text));
		return *this;
	}

	GenericTreeExtraCtl& SetDynamics(SBYTE left, SBYTE right, SBYTE top, SBYTE bottom)
	{
		Dynamics.left = left;
		Dynamics.right = right;
		Dynamics.top = top;
		Dynamics.bottom = bottom;
		return *this;
	}

	GenericTreeExtraCtl& SetPlacement(bool relative, int left, int right, int top, int bottom)
	{
		Placement.left = left;
		Placement.right = right;
		Placement.top = top;
		Placement.bottom = bottom;
		PlacementRelative = relative;
		return *this;
	}

	GenericTreeExtraCtl& SetPlacement(bool relative, int left, int right, int height = 0)
	{
		return SetPlacement(relative, left, right, 0, height);
	}

	GenericTreeExtraCtl& SetDynamicRelativePlacement(int left, int right)
	{
		return SetPlacement(true, left, right, 0).SetDynamics((SBYTE)left, (SBYTE)right, 0, 0);
	}

	GenericTreeExtraCtl& SetOnCreate(CWndFnc fnc)
	{
		OnCreate.push_back(fnc);
		return *this;
	}
	GenericTreeExtraCtl& SetOnChange(CWndFnc fnc)
	{
		OnChange.push_back(fnc);
		return *this;
	}

	static GenericTreeExtraCtl* FromWindowHandle(HWND hWnd)
	{
		return (GenericTreeExtraCtl*)::myGetProp(hWnd, VA_GenericTreeExCtlProperty);
	}

	static GenericTreeExtraCtl* FromWindow(const CWnd* pWnd)
	{
		if (pWnd && pWnd->m_hWnd)
			return FromWindowHandle(pWnd->m_hWnd);

		return nullptr;
	}
};

typedef std::map<UINT, std::vector<GenericTreeExtraCtl>> ExtraCtlList;

struct ExtraCtlContainer
{
	ExtraCtlList ExtraCtls;

	template <class TCtrl> using ctl_fnc = std::function<void(TCtrl&)>;

	using btn_func = ctl_fnc<CThemedButton>;
	using chb_func = ctl_fnc<CThemedCheckBox>;
	using ed_func = ctl_fnc<CThemedEdit>;
	using ded_func = ctl_fnc<CThemedDimEdit>;

	GenericTreeExtraCtl& AddControl(UINT row, GenericTreeExtraCtl::ctl_type type);

	void AddDLUSpace(UINT row, int height)
	{
		AddControl(row, GenericTreeExtraCtl::Spacer).SetPlacement(false, 0, 0, height);
	}

	GenericTreeExtraCtl& AddButton(UINT row, LPCSTR text, btn_func on_click);
	GenericTreeExtraCtl& AddCheckBox(UINT row, LPCSTR text, chb_func on_click);
	GenericTreeExtraCtl& AddEdit(UINT row, LPCSTR text, ed_func on_text_changed);
	GenericTreeExtraCtl& AddDimEdit(UINT row, LPCSTR dimText, ded_func on_text_changed);
	GenericTreeExtraCtl& AddLabel(UINT row, LPCSTR text);
	GenericTreeExtraCtl& AddComboBox(UINT row);

	GenericTreeExtraCtl& AddButton(UINT row, LPCSTR text, btn_func on_click, btn_func on_create);
	GenericTreeExtraCtl& AddCheckBox(UINT row, LPCSTR text, chb_func on_click, chb_func on_create);
	GenericTreeExtraCtl& AddEdit(UINT row, LPCSTR text, ed_func on_text_changed, ed_func on_create);
	GenericTreeExtraCtl& AddDimEdit(UINT row, LPCSTR dimText, ded_func on_text_changed, ded_func on_create);
};

struct GroupExtraCtl : public GenericTreeExtraCtl, public ExtraCtlContainer
{
};

struct GenericTreeNodeItem
{
	enum State : BYTE
	{
		State_None = 0,

		State_Enabled = 1 << 0,
		State_Checked = 1 << 1,

		State_Owner_0 = 1 << 2,
		State_Owner_1 = 1 << 3,
		State_Owner_2 = 1 << 4,
		State_Owner_3 = 1 << 5,
		State_Owner_4 = 1 << 6,
		State_Owner_5 = 1 << 7,
	};

	typedef std::function<UINT_PTR()> TagFnc;
	typedef std::map<std::string, TagFnc> TagMap;
	typedef std::vector<GenericTreeNodeItem> NodeItems;
	typedef std::function<LPCSTR(GenericTreeNodeItem&)> TextFnc;
	typedef std::function<void(GenericTreeNodeItem&)> ModifyFnc;
	typedef std::function<bool(const GenericTreeNodeItem&)> SearchFnc;
	typedef std::function<bool(const GenericTreeNodeItem&)> ApproveFnc;
	typedef std::function<bool(const GenericTreeNodeItem&, const GenericTreeNodeItem&)> SortFnc;

	// After sorting of vector with nodes,
	// any saved pointer from vector may become invalid.
	// This ID can be used to identify specific node any time.
	UINT mUniqueID;

	int mIconIndex = 0;
	void* mData = nullptr;

	WTString mNodeText;
	State mState;
	TagMap mTags;
	NodeItems mChildren;

	void SetTag(const std::string& name, TagFnc tag)
	{
		mTags[name] = tag;
	}

	TagFnc ReleaseTag(const std::string& name)
	{
		TagMap::iterator it = mTags.find(name);
		if (it != mTags.end())
		{
			TagFnc val = it->second;
			mTags.erase(it);
			return val;
		}
		return TagFnc();
	}

	TagFnc GetTag(const std::string& name) const
	{
		TagMap::const_iterator it = mTags.find(name);
		if (it != mTags.end())
			return it->second;
		else
			return TagFnc();
	}

	UINT_PTR GetTagValue(const std::string& name, UINT_PTR default_value = 0) const
	{
		TagMap::const_iterator it = mTags.find(name);
		if (it != mTags.end() && it->second)
			return it->second();
		else
			return default_value;
	}

	bool GetState(State bit) const
	{
#ifdef _DEBUG
		bool rslt = (mState & bit) == bit;
		return rslt;
#else
		return (mState & bit) == bit;
#endif
	}

	bool GetState(BYTE bit) const
	{
		return GetState((State)bit);
	}

	void SetState(State bit, bool value)
	{
		if (value)
			mState = (State)(mState | bit);
		else
			mState = (State)(mState & ~bit);
	}

	void SetState(BYTE bit, bool value)
	{
		SetState((State)bit, value);
	}

	bool GetEnabled() const
	{
		return GetState(State_Enabled);
	}
	bool GetChecked() const
	{
		return GetState(State_Checked);
	}

	void SetChecked(bool val)
	{
		SetState(State_Checked, val);
	}
	void SetEnabled(bool val)
	{
		SetState(State_Enabled, val);
	}

	__declspec(property(put = SetEnabled, get = GetEnabled)) bool mEnabled;
	__declspec(property(put = SetChecked, get = GetChecked)) bool mChecked;

	GenericTreeNodeItem() : mState(State_Enabled)
	{
		static UINT uniq_id = 0;
		mUniqueID = ++uniq_id;
	}
};

struct GenericTreeDlgParams : public ExtraCtlContainer
{
	WTString mCaption;
	WTString mDirectionsText;
	WTString mHelpTopic;
	ImageListManager::BackgroundType mImgList = ImageListManager::bgNone;
	GenericTreeNodeItem::NodeItems mNodeItems;
	bool mListCanBeColored;
	bool mExtended;
	UINT mFirstCtlExId;

	GenericTreeNodeItem::ApproveFnc mApproveItem;
	GenericTreeNodeItem::ModifyFnc mPreprocessItem;
	GenericTreeNodeItem::ModifyFnc mPostprocessItem;

	std::function<void(void)> mOnInitialised;  // called BEFORE items are inserted
	std::function<void(void)> mOnItemsUpdated; // called AFTER items are inserted/updated

	// filters nodes (does not filter child nodes)
	// if get_text is not specified, item text is used to filter items
	static void FilterNodes(GenericTreeNodeItem::NodeItems& nodes, WTString& filter, GenericTreeNodeItem::State state,
	                        GenericTreeNodeItem::TextFnc get_text = GenericTreeNodeItem::TextFnc());

	// filters nodes within specified level range
	// if get_text is not specified, item text is used to filter items
	static void FilterNodes(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level, WTString& filter,
	                        GenericTreeNodeItem::State state,
	                        GenericTreeNodeItem::TextFnc get_text = GenericTreeNodeItem::TextFnc());

	// filters nodes using regular expression (does not filter child nodes)
	// if get_text is not specified, item text is used to filter items
	static void FilterNodesRegex(GenericTreeNodeItem::NodeItems& nodes, WTString& filter,
	                             GenericTreeNodeItem::State state,
	                             GenericTreeNodeItem::TextFnc get_text = GenericTreeNodeItem::TextFnc());

	// filters nodes using regular expression within specified level range
	// if get_text is not specified, item text is used to filter items
	static void FilterNodesRegex(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level, WTString& filter,
	                             GenericTreeNodeItem::State state,
	                             GenericTreeNodeItem::TextFnc get_text = GenericTreeNodeItem::TextFnc());

	// sorts nodes (does not sort child nodes)
	// if sortFunc is not specified, item text is used to compare items
	static void SortNodes(GenericTreeNodeItem::NodeItems& nodes,
	                      GenericTreeNodeItem::SortFnc sortFnc = GenericTreeNodeItem::SortFnc());

	// sorts nodes within specified level range
	// if sortFunc is not specified, item text is used to compare items
	static void SortNodes(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
	                      GenericTreeNodeItem::SortFnc sortFnc = GenericTreeNodeItem::SortFnc());

	// iterates over all nodes within specified level range
	// to iterate all use: [min_level=0, max_level=-1] or [min_level=0, max_level=INT_MAX]
	static void ForEach(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
	                    GenericTreeNodeItem::ModifyFnc modifyfnc);

	// iterates over all nodes within specified level range and returns pointer to matching one
	// to iterate all use: [min_level=0, max_level=-1] or [min_level=0, max_level=INT_MAX]
	// returns 'nullptr' if item is not found
	static GenericTreeNodeItem* FindItem(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
	                                     GenericTreeNodeItem::SearchFnc searchfnc);

	GenericTreeDlgParams();
};

struct CTreeIter
{
	static HTREEITEM GetFirstItem(const CTreeCtrl& tree)
	{
		return tree.GetRootItem();
	}

	static HTREEITEM GetNextItem(const CTreeCtrl& tree, HTREEITEM hItem)
	{
		if (tree.ItemHasChildren(hItem))
			return tree.GetChildItem(hItem);
		HTREEITEM tmp;
		if ((tmp = tree.GetNextSiblingItem(hItem)) != NULL)
			return tmp;
		HTREEITEM p = hItem;
		while ((p = tree.GetParentItem(p)) != NULL)
		{
			if ((tmp = tree.GetNextSiblingItem(p)) != NULL)
				return tmp;
		}
		return NULL;
	}
};

// CTreeCtrlCB - tree control with checkbox notifications
class CTreeCtrlCB : public CThemedTree
{
	DECLARE_DYNAMIC(CTreeCtrlCB)

  public:
	CTreeCtrlCB();
	virtual ~CTreeCtrlCB();

	UINT LastUsedID = 0;
	UINT SelectedID = 0;
	UINT LastClickedID = 0;

	std::function<void(HTREEITEM)> on_check_toggle;

  protected:
	DECLARE_MESSAGE_MAP()
  public:
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnNMClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
};

class GenericTreeDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(GenericTreeDlg)
	DECLARE_MENUXP()
  public:
	GenericTreeDlg(GenericTreeDlgParams& params, CWnd* parent = NULL);
	virtual ~GenericTreeDlg();

	enum
	{
		IDD = IDD_GENERICTREEDLG
	};

	virtual BOOL OnInitDialog();
	void ExtendedInit();

	CStatic m_tree_label;
	CTreeCtrlCB m_tree;

	virtual void UpdateNodes(bool applySavedView = false, bool clearSavedView = true);
	void SaveView();
	void ApplySavedView(bool clear = true);

	GenericTreeDlgParams& Params()
	{
		return mParams;
	}

  protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	void InsertNodes(GenericTreeNodeItem::NodeItems& nodes, HTREEITEM hParent);
	HTREEITEM InsertTreeItem(LPCWSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent,
	                         HTREEITEM hInsertAfter);
	HTREEITEM InsertTreeItem(LPCWSTR lpszItem, HTREEITEM hParent, HTREEITEM hInsertAfter);
	void PropagateCheck(HTREEITEM item, bool checked);
	void OnCheckToggle(HTREEITEM item);

	DECLARE_MESSAGE_MAP()

	virtual void OnDpiChanged(DpiChange change, bool& handled);

	void CheckAllNodes(HTREEITEM hItem, BOOL val);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);

  private:
	UINT mSavedView[2];
	GenericTreeDlgParams& mParams;
};
