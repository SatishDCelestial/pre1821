#if !defined(AFX_GenericComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_)
#define AFX_GenericComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// GenericComboBox.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBox window
#include "GenericComboBoxListBox.h"
#include "ArgToolTip.h"
#include "ColorListControls.h"

class CGenericComboBox;

extern const UINT WM_DRAW_MY_BORDER;

template <typename BASE> class CWndSubW : public BASE
{
  public:
	BOOL SubclassWindowW(HWND hWnd)
	{
		if (!BASE::Attach(hWnd))
			return FALSE;

		// allow any other subclassing to occur
		BASE::PreSubclassWindow();

		// returns correct unicode text of control
		WNDPROC* lplpfn = BASE::GetSuperWndProcAddr();
		WNDPROC oldWndProc;
		if (::IsWindowUnicode(hWnd))
			oldWndProc = (WNDPROC)::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)AfxGetAfxWndProc());
		else
			oldWndProc = (WNDPROC)::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)AfxGetAfxWndProc());

		ASSERT(oldWndProc != ::AfxGetAfxWndProc());

		if (*lplpfn == NULL)
			*lplpfn = oldWndProc; // the first control of that type created
#ifdef _DEBUG
		else if (*lplpfn != oldWndProc)
		{
			//			TRACE(traceAppMsg, 0, "Error: Trying to use SubclassWindow with incorrect CWnd\n");
			//			TRACE(traceAppMsg, 0, "\tderived class.\n");
			// TRACE(traceAppMsg, 0, "\thWnd = $%08X (nIDC=$%08X) is not a %hs.\n", (UINT)(UINT_PTR)hWnd,
			// _AfxGetDlgCtrlID(hWnd), GetRuntimeClass()->m_lpszClassName);
			ASSERT(FALSE);
			// undo the subclassing if continuing after assert
			if (::IsWindowUnicode(hWnd))
				::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
			else
				::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
		}
#endif
		return TRUE;
	}

	HWND UnsubclassWindowW()
	{
		ASSERT(::IsWindow(BASE::m_hWnd));

		// set WNDPROC back to original value
		WNDPROC* lplpfn = BASE::GetSuperWndProcAddr();
		if (::IsWindowUnicode(BASE::m_hWnd))
			SetWindowLongPtrW(BASE::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		else
			SetWindowLongPtrA(BASE::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		*lplpfn = NULL;

		// and Detach the HWND from the CWnd object
		return BASE::Detach();
	}
};

class GenericComboPaintSubClass : public CWnd
{
	DECLARE_DYNAMIC(GenericComboPaintSubClass)
  public:
	enum highlight
	{
		highlighted_none = 0x0,
		highlighted_mouse_inside = 0x1,
		highlighted_button_pressed = 0x2,
		highlighted_edit_focused = 0x4,

		highlighted_draw_highlighted = highlighted_mouse_inside | highlighted_button_pressed | highlighted_edit_focused
	};

  protected:
	static const int MOUSE_TIMER_ID = 1284;

  public:
	GenericComboPaintSubClass()
	{
		last_highlight_state = highlighted_none;
		mVs2010ColouringIsActive = CVS2010Colours::IsVS2010VAViewColouringActive();
		mComboEx = nullptr;
	}

	void SetVS2010ColouringActive(bool act)
	{
		mVs2010ColouringIsActive = act;
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	LRESULT OnDrawMyBorder(WPARAM wparam, LPARAM lparam);

	void DoPaint(bool validate_instead_of_paint = false);
	CWnd* GetSplitterWnd();
	IVS2010ComboInfo* GetVS2010ComboInfo();
	highlight last_highlight_state;

	DECLARE_MESSAGE_MAP()
	virtual void PreSubclassWindow();

  public:
	afx_msg void OnTimer(UINT_PTR nIDEvent);

  private:
	bool mVs2010ColouringIsActive;
	CComboBoxEx* mComboEx;
};

class GenericComboSubClass3 : public ToolTipWrapper<CWndSubW<GenericComboPaintSubClass>>
{
	typedef ToolTipWrapper<CWndSubW<GenericComboPaintSubClass>> BASE;

  protected:
	virtual BOOL OkToDisplayTooltip();
	// for processing Windows messages
	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  public:
	GenericComboSubClass3();
	void Init(CGenericComboBox* combo);
	CGenericComboBox* m_combo;
};

class GenericComboEditSubClass3 : public ToolTipWrapper<CWndSubW<CWnd>>
{
  protected:
	DECLARE_DYNAMIC(GenericComboEditSubClass3)
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual BOOL OkToDisplayTooltip();

  public:
	GenericComboEditSubClass3();
	void Init(CGenericComboBox* combo);
	CGenericComboBox* m_combo;
};

class CGenericComboBox : public CWndSubW<CComboBoxEx>, public IVS2010ComboInfo
{
	// Construction
  public:
	CGenericComboBox();

	virtual ~CGenericComboBox();

	// Attributes

	// Operations
  public:
	BOOL GetDroppedState() const;
	void DisplayList(BOOL bDisplay = TRUE);
	virtual void OnDropdown() = 0;
	virtual void OnCloseup()
	{
	}
	void SettingsChanged();
	void SetEditControlReadOnly(BOOL readOnly = TRUE)
	{
		m_edSub.m_readOnly = readOnly;
	}
	void SetEditControlTipText(LPCSTR tipText)
	{
		m_edSub.SetTipText(tipText);
	}

	void SetHasColorableContent(bool value)
	{
		m_hasColorableContent = value;
	}

	// must be set before control is created
	void SetPopupDropShadow(bool value)
	{
		m_popupDropShadow = value;
	}

	std::function<void(uint)> highlight_state_change_event;

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGenericComboBox)
  protected:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PreSubclassWindow();
	virtual void OnEditChange() = 0;
	//}}AFX_VIRTUAL

	// Implementation
	int GetDroppedHeight() const;
	int GetDroppedWidth() const;
	int SetDroppedHeight(int nHeight);
	int SetDroppedWidth(int nWidth);

	virtual void Init();

	virtual bool GetItemInfo(int nRow, CStringW* text, CStringW* tip, int* image, UINT* state) = 0;

	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void GetDefaultTitleAndIconIdx(CString& title, int& iconIdx) const = 0;
	virtual void OnSelect() = 0;
	virtual void SelectItem(int index, bool append = false) = 0;

	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

	// CListCtrl interface for derived classes
	void SetItemCount(int nItems);
	void SetImageList(CImageList* pImageList, int nImageListType);
	virtual int GetItemsCount() const;
	virtual WTString GetItemTip(int nRow) const = 0;
	virtual CStringW GetItemTipW(int nRow) const = 0;
	POSITION GetFirstSelectedItemPosition() const;
	int GetNextSelectedItem(POSITION& pos) const;
	CString GetItemText(int nItem, int nSubItem) const;
	CStringW GetItemTextW(int nItem, int nSubItem) const;
	BOOL GetItem(LVITEM* pItem) const;
	BOOL SetItemState(int nItem, UINT nState, UINT nMask);
	BOOL EnsureVisible(int nItem, BOOL bPartialOK);
	BOOL IsWindow() const;
	int SetSelectionMark(int iIndex);
	int SetHotItem(int iIndex);
	virtual bool IsVS2010ColouringActive() const
	{
		return false;
	}
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual COLORREF GetVS2010ComboBackgroundColour() const;
	virtual bool ComboDrawsItsOwnBorder() const;
	virtual bool IsListDropped() const;
	virtual bool AllowPopup() const
	{
		return true;
	}
	virtual void OnHighlightStateChange(uint new_state) const;
	virtual bool HasArrowButtonRightBorder() const;
	void SetDrawImages(bool value)
	{
		m_drawImages = value;
	}
	bool GetDrawImages() const
	{
		return m_drawImages;
	}

	CWnd& EditSubclasser()
	{
		return m_edSub;
	}
	CWnd& ListSubclasser()
	{
		return m_lstCombo;
	}
	CWnd& ComboSubclasser()
	{
		return m_comboSub;
	}

	virtual bool OnEditWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		return false;
	}
	virtual bool OnListWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		return false;
	}
	virtual bool OnComboWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		return false;
	}

	// Generated message map functions
  protected:
	//{{AFX_MSG(CGenericComboBox)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
#if _MSC_VER <= 1200
	void OnCreate(LPCREATESTRUCT lpCreateStruct);
#else
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
#endif

	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()

  protected:
	int m_nDroppedHeight;
	int m_nDroppedWidth;

	friend class GenericComboSubClass3;
	friend class GenericComboEditSubClass3;
	friend class CGenericComboBoxListCtrl;
	friend class GenericComboListHandler;

	CGenericComboBoxListCtrl m_lstCombo;
	GenericComboSubClass3 m_comboSub;
	GenericComboEditSubClass3 m_edSub;
	HWND m_hCombo;
	bool m_hasColorableContent;
	bool m_drawImages;
	bool m_popupDropShadow;
	CBrush bgbrush_cache;

	std::unique_ptr<CGradientCache> popup_bgbrush_cache;
};

class CGenericComboBoxPassive : public CGenericComboBox
{
  public:
	CGenericComboBoxPassive(bool hasColorableContent) : CGenericComboBox()
	{
		SetHasColorableContent(hasColorableContent);
	}
	virtual void OnDropdown()
	{
	}
	virtual void OnEditChange()
	{
	}
	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
	{
		_ASSERTE(!"OnGetdispinfo not implemented!");
	}
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
	{
		_ASSERTE(!"OnGetdispinfoW not implemented!");
	}
	virtual void GetDefaultTitleAndIconIdx(CString& title, int& iconIdx) const
	{
	}
	virtual void OnSelect()
	{
	}
	virtual WTString GetItemTip(int nRow) const
	{
		return "";
	}
	virtual CStringW GetItemTipW(int nRow) const
	{
		return L"";
	}
	virtual bool AllowPopup() const
	{
		return GetItemsCount() > 0;
	}

	friend class CVAClassView;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GenericComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_)
