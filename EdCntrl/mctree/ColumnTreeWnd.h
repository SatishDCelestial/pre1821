/*********************************************************************
* Multi-Column Tree View, version 1.4 (July 7, 2005)
* Copyright (C) 2003-2005 Michal Mecinski.
*
* You may freely use and modify this code, but don't remove
* this copyright note.
*
* THERE IS NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, FOR
* THIS CODE. THE AUTHOR DOES NOT TAKE THE RESPONSIBILITY
* FOR ANY DAMAGE RESULTING FROM THE USE OF IT.
*
* E-mail: mimec@mimec.org
* WWW: http://www.mimec.org
********************************************************************/

#pragma once

#include "ColumnTreeCtrl.h"
#include "../IFindTargetWithTree.h"


enum TreeItemFlags {
	TIF_None					= 0x00,
	TIF_ONE_CELL_ROW			= 0x01,
	TIF_DRAW_IN_BOLD			= 0x02,
	TIF_PROCESS_MARKERS			= 0x04,
	TIF_DONT_COLOUR				= 0x08,
	TIF_DONT_COLOUR_TOOLTIP		= 0x10,				// forced for ' references found' tooltips
	TIF_END_ELLIPSIS			= 0x20,				// currently works only on the main column without colouring
	TIF_PATH_ELLIPSIS			= 0x40,				// currently works only on the main column without colouring
};


#if _MSC_VER >= 1600 // VS2010
namespace std
#else
namespace stdext
#endif
{
#if _MSC_VER >= 1923
	template<class _Kty> inline
	size_t hash_value(const _Kty& _Keyval)
	{	// hash _Keyval to size_t value one-to-one
		return ((size_t)_Keyval ^ (size_t)0xdeadbeef);
	}

	inline size_t hash_value(_In_z_ const wchar_t *_Str)
	{	// hash NTWCS to size_t value
		return (_STD _Hash_array_representation(_Str, _CSTD wcslen(_Str)));
	}
#endif

//	template<typename LEFT, typename RIGHT>
	inline size_t hash_value(const std::pair<CStringW/*LEFT*/, TreeItemFlags/*RIGHT*/> &key)
	{
		return (hash_value((LPCWSTR)key.first) + hash_value(key.second)) ^ hash_value(key.second);
	}
}


class CGradientCache;

#include <array>
#include <algorithm>
#include <map>
#include <optional>
class CalcItemWidthCharsCache
{
	const HWND tree;
	HDC dc = nullptr;
	HDC dc_bold = nullptr;

	std::array<short, 256u> widths;
	std::array<short, 256u> bold_widths;
	std::unordered_map <wchar_t, std::array<short, 2u>> widths_map;		// save space for rare wchars

  public:
	CalcItemWidthCharsCache(HWND tree)
	    : tree(tree)
	{
		assert(tree && ::IsWindow(tree));
	}
	~CalcItemWidthCharsCache()
	{
		flush();
	}

	void flush();
	void start();
	void end()
	{
	}

	std::optional<int> measure_text(TreeItemFlags flags, const CStringW& strSub);
};

template<typename TREECTRL>
class CColumnTreeWndTempl : public CWndDpiAware<CWnd>
{
	class column_list;

// 	DECLARE_DYNAMIC(CColumnTreeWndTempl)
public:
	typename TREECTRL TreeCtrl_t;

	CColumnTreeWndTempl();
	virtual ~CColumnTreeWndTempl();

	long reposition_controls_pending;
	std::unique_ptr<CalcItemWidthCharsCache> chars_cache;

public:
	enum ChildrenIDs { HeaderID = 1, TreeID = 2 };

	void UpdateColumns();
	void AdjustColumnWidth(int nColumn, BOOL bIgnoreCollapsed);

	TREECTRL& GetTreeCtrl() { return m_Tree; }
	const TREECTRL &GetTreeCtrl() const { return m_Tree; }
	CHeaderCtrl& GetHeaderCtrl() { return m_Header; }
	int GetColumnWidth(int col) const;
	bool IsColumnShown(int col) const;
	void SetItemFlags(HTREEITEM item, int add_flags, int remove_flags = 0);
	void SetItemFlags2(HTREEITEM item, int add_flags, int remove_flags = 0, const HTREEITEM* hint_parent = nullptr, const int* hint_depth = nullptr, const CStringW* hint_text = nullptr);
	void ShowColumn(int column, bool show = true);

	void FindNext(CStringW text, bool match_case = false);
	void FindPrev(CStringW text, bool match_case = false);
#ifdef ONE_FIND_PER_LINE
	void FindSelect(HTREEITEM item);
#endif
	HTREEITEM GetLastFound() const {return last_find;}
	void MarkAll(const CStringW &text, bool match_case = false);
	void MarkOne(HTREEITEM item, const CStringW &text, bool match_case = false);
	void UnmarkAll(UnmarkType what = unmark_markall);

	bool IsFilenameItem(HTREEITEM item) const;
	bool ValidateCache(HTREEITEM hItem = nullptr);

protected:
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	virtual void OnDpiChanged(DpiChange change, bool& handled);
	virtual void OnFontSettingsChanged(VaFontTypeFlags changed, bool & handled);

protected:
	void UpdateScroller();
	void RepositionControls(bool skip_column_resize = false);

	int CalcItemWidth(HTREEITEM hItem, TreeItemFlags flags, const CStringW& strSub, int nColumn, int depth = -1);
	void CacheItemInfo(HTREEITEM hItem, TreeItemFlags flags, bool forceUpdate = false, const HTREEITEM *hint_parent = nullptr, const int *hint_depth = nullptr, const CStringW *hint_text = nullptr);
	TreeItemFlags GetItemFlags(HTREEITEM hItem) const ;
	BOOL m_Tree_SetItemTextW(HTREEITEM hItem, LPCWSTR lpszItem)
	{
		auto ret = m_Tree.SetItemTextW(hItem, lpszItem);
		ValidateCache(hItem);
		return ret;
	}

	int GetAllColumnsWidth() const;
	static void MeasureText(CDC &dc, const CStringW &text, CRect &rect, std::vector<int> *x_positions = NULL);

protected:
	TREECTRL m_Tree;
	CHeaderCtrl m_Header;
	int m_cyHeader;
	int m_cxTotal;
	int m_xPos;
	std::vector<int> m_arrColWidths;
	std::vector<bool> hidden_columns;
	int m_xOffset;

	enum item_info_cache_id
	{
		IICI_Parent,	// HTREEITEM of the parent node
		IICI_Flags,		// OR-ed values of enum TreeItemFlags 
		IICI_Depth,		// how many parents does the node have
		IICI_Columns,	// texts and widths of columns
	};

	struct column_info
	{
		CStringW text;
		int width = 0;
	};

#ifdef ALLOW_MULTIPLE_COLUMNS
	using column_list = std::vector<column_info>;
#else
	// this exists just to save some memory
	// also asserts if more columns are requested
	class column_list
	{
		column_info m_info = {{}, -1};

	public:
		void clear() {
			m_info = { {}, -1 };
		}

		bool empty() const {
			return m_info.width == -1;
		}

		size_t size() const { 
			return empty() ? 0u : 1u; 
		}

		column_info& operator[](size_t index) {
			_ASSERTE(!empty() && index == 0);
			return m_info;
		}

		const column_info& operator[](size_t index) const {
			_ASSERTE(!empty() && index == 0);
			return m_info;
		}

		void emplace_back(const CStringW & text, int width) {
			_ASSERTE(empty());
			m_info.text = text;
			m_info.width = width;
		}
	};
#endif

	stdext::hash_map<HTREEITEM, std::tuple<HTREEITEM, TreeItemFlags, int, column_list>> item_info_cache;
	HTREEITEM last_find;
	std::unique_ptr<CGradientCache> background_gradient_cache;

public:
	bool markall;
	CStringW last_markall_rtext;
	bool last_markall_matchcase;

protected:
	DECLARE_MESSAGE_MAP()
public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnHeaderItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHeaderDividerDblClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDeleteItem(NMHDR *nmhdr, LRESULT *result);
	afx_msg void OnSelectionChanged(NMHDR *nmhdr, LRESULT *result);
	afx_msg LRESULT OnItemJustInserted(WPARAM wparam, LPARAM lparam);
	afx_msg void OnParentNotify(UINT message, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSetFocus(CWnd* pOldWnd);

private:
	long	mCountAtLastRepositionCheck;
	bool	mMarksActive = false; // [case: 141848]
};
typedef CColumnTreeWndTempl<CColumnTreeCtrl> CColumnTreeWnd;
typedef CColumnTreeWndTempl<CColumnTreeCtrl2> CColumnTreeWnd2;



extern const unsigned int WM_COLUMN_RESIZED;
extern const unsigned int WM_COLUMN_SHOWN;

// gmit: IF NEW MARKERS ARE ADDED, DON'T FORGET TO UPDATE CColumnTreeWnd::MeasureText!!!
// also update FindReferences::GetSummary
static const char MARKER_NONE = '\1';
static const char MARKER_BOLD = '\2';
static const char MARKER_REF = '\3';
static const char MARKER_ASSIGN = '\4';
static const char MARKER_RECT = '\5';
static const char *const MARKER_RECT_STR = "\5";
static const char MARKER_INVERT = '\6';
static const char *const MARKER_INVERT_STR = "\6";
static const char MARKER_DIM = '\7';
static const char *const MARKER_DIM_STR = "\7";

static const char *const MARKERS = "\1\2\3\4\5\6\7";
