#if !defined(AFX_FastComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_)
#define AFX_FastComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FastComboBox.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CFastComboBox window
#include "FastComboBoxListCtrl.h"
#include "ArgToolTip.h"
#include "ColorListControls.h"
#include "CWndTextW.h"

class CFastComboBox;

class ComboSubClass3 : public ToolTipWrapper<CWndTextW<ComboPaintSubClass>>
{
	typedef ToolTipWrapper<ComboPaintSubClass> BASE;

  protected:
	virtual BOOL OkToDisplayTooltip();
	// for processing Windows messages
	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  public:
	ComboSubClass3();
	void Init(CFastComboBox* combo);
	CFastComboBox* m_combo;
};

class ComboEditSubClass3 : public ToolTipWrapper<CWndTextW<CEdit>>
{
  protected:
	DECLARE_DYNAMIC(ComboEditSubClass3)
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual BOOL OkToDisplayTooltip();

  public:
	ComboEditSubClass3();
	void Init(CFastComboBox* combo);
	bool m_auto_complete = false;	// case: 147843 let CTRL+BACKSPACE delete the last word
	CFastComboBox* m_combo = nullptr;
};

class CFastComboBox : public CWndDpiAware<CComboBoxEx>, public IVS2010ComboInfo, protected CVAMeasureItem
{
	// Construction
  public:
	CFastComboBox(bool hasColorableContent);
	virtual ~CFastComboBox();

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

	std::function<void(uint)> highlight_state_change_event;

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CFastComboBox)
  protected:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PreSubclassWindow();
	virtual void OnEditChange() = 0;
	//}}AFX_VIRTUAL

	// Implementation
	int GetDroppedHeight();
	int GetDroppedWidth();
	int SetDroppedHeight(UINT nHeight);
	int SetDroppedWidth(UINT nWidth);
	void SetItem(LPCSTR txt, int image);
	void SetItemW(LPCWSTR txt, int image);
	virtual void Init();
	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult) = 0;
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult) = 0;
	virtual void GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const = 0;
	virtual void OnSelect() = 0;
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);

	// CListCtrl interface for derived classes
	void SetItemCount(int nItems);
	void SetImageList(CImageList* pImageList, int nImageListType);
	virtual int GetItemCount() const;
	virtual WTString GetItemTip(int nRow) const = 0;
	virtual CStringW GetItemTipW(int nRow) const = 0;
	POSITION GetFirstSelectedItemPosition() const;
	int GetNextSelectedItem(POSITION& pos) const;
	WTString GetItemText(int nItem, int nSubItem) const;
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


	virtual void OnDpiChanged(DpiChange change, bool& handled);

	// Generated message map functions
  protected:
	//{{AFX_MSG(CFastComboBox)
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
	int m_nDroppedCount;
	int m_nDroppedWidth;

  private:
	friend class ComboSubClass3;
	friend class ComboEditSubClass3;
	friend class CFastComboBoxListCtrl;

	CFastComboBoxListCtrl m_lstCombo;
	ComboSubClass3 m_comboSub;
	ComboEditSubClass3 m_edSub;
	HWND m_hCombo;
	const bool mHasColorableContent;
	CBrush bgbrush_cache;
	std::unique_ptr<CGradientCache> popup_background_gradient_cache;
};

class CFastComboBoxPassive : public CFastComboBox
{
  public:
	CFastComboBoxPassive(bool hasColorableContent) : CFastComboBox(hasColorableContent)
	{
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
	virtual void GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const
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
		return GetItemCount() > 0;
	}

	friend class CVAClassView;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FastComboBox_H__41214ECF_3100_11D5_AB89_000000000000__INCLUDED_)
