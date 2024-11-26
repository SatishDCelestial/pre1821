#ifndef IFindTargetWithTree_h__
#define IFindTargetWithTree_h__

template <typename TREECTRL> class CColumnTreeWndTempl;

enum UnmarkType
{
	unmark_markall = 1,
	unmark_rectangle = 2,

	unmark_all = unmark_markall | unmark_rectangle
};

// IFindTargetWithTree
// ----------------------------------------------------------------------------
// Implement this interface in the window that is the parent of a CColumnTreeWnd
// which supports the find text dialog.
// FindTextDlg calls back to the parent via this interface.
//
template <typename TREECTRL> class IFindTargetWithTree
{
  public:
	virtual ~IFindTargetWithTree() = default;

	virtual CWnd* GetCWnd() = 0;
	virtual void OnFindNext() = 0;
	virtual void OnFindPrev() = 0;

	virtual CStringW GetFindText() const = 0;
	virtual void SetFindText(const CStringW& text) = 0;
	virtual bool IsFindCaseSensitive() const = 0;
	virtual void SetFindCaseSensitive(bool caseSensitive) = 0;
	virtual bool IsFindReverse() const = 0;
	virtual void SetFindReverse(bool findReverse) = 0;
	virtual bool IsMarkAllActive() const = 0;
	virtual void SetMarkAll(bool active) = 0;

	virtual CColumnTreeWndTempl<TREECTRL>& GetTree() = 0;
	virtual void MarkAll() = 0;
	virtual void UnmarkAll(UnmarkType what = unmark_markall) = 0;
	virtual void GoToSelectedItem() = 0;
};

#endif // IFindTargetWithTree_h__
