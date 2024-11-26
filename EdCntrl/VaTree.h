#if !defined(AFX_VATREE_H__6C056D34_63A8_11D2_8173_00207814D759__INCLUDED_)
#define AFX_VATREE_H__6C056D34_63A8_11D2_8173_00207814D759__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// VaTree.h : header file
//

#define VAT_PASTE "&Paste"
#define VAT_METHOD "Goto Recent &Method"
#define VAT_FILE "Open Recent &File"
#define VAT_BOOKMARK "Goto Bookmark"

/////////////////////////////////////////////////////////////////////////////
// VaTree window
class EdCnt;
class AttrLst;

class VaTree : public CTreeCtrl
{
	// Construction
	HTREEITEM m_files;
	bool m_fileExp;
	HTREEITEM m_methods;
	bool m_methodExp;
	HTREEITEM m_bookmarks;
	bool m_bookmarkExp;
	HTREEITEM m_copybuf;
	bool m_copyExp;

	HTREEITEM m_insOrder;
	CStringW m_currentCopyBuffer;

  public:
	VaTree();
	virtual ~VaTree();

	// Attributes
  public:
	// Operations
  public:
	void AddCopy(const CStringW& buf);
	void AddFile(const CStringW& file, int line);
	void AddMethod(LPCTSTR method, const CStringW& file, int line, bool addFileAndLine = true);
	void AddBookmark(const CStringW& file, int line)
	{
		ToggleBookmark(file, line, true);
	}
	void ToggleBookmark(const CStringW& file, int line, bool add = true);
	void RemoveAllBookmarks(const CStringW& file);
	void Navigate(bool back = true);
	void SetFileBookmarks(const CStringW& filename, AttrLst* lst);
	void OpenItemFile(HTREEITEM item, EdCnt* ed);
	void Create(CWnd* parent, int id);

  private:
	using CWnd::Create;

  public:
	CStringW GetClipboardTxt() const
	{
		return m_currentCopyBuffer;
	}
	CStringW GetClipboardItem(int idx) const;
	CStringW GetItemTextW(HTREEITEM hItem) const;
	HTREEITEM InsertItemW(const CStringW& lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent,
	                      HTREEITEM hInsertAfter);
	HTREEITEM InsertItemW(UINT nMask, const CStringW& lpszItem, int nImage, int nSelectedImage, UINT nState,
	                      UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VaTree)
  protected:
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual void OnDestroy();
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	void Shutdown();
	void DeleteItem(HTREEITEM item);
	void DeleteCopyItem(HTREEITEM item);
	void ReadPasteHistory();

	// Generated message map functions
  protected:
	//{{AFX_MSG(VaTree)
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

extern VaTree* g_VATabTree;
/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VATREE_H__6C056D34_63A8_11D2_8173_00207814D759__INCLUDED_)
