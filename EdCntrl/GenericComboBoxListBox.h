#if !defined(AFX_GenericComboBoxListCtrl_H__552D4FC2_CF60_4275_B53A_A7BA00BD4408__INCLUDED_)
#define AFX_GenericComboBoxListCtrl_H__552D4FC2_CF60_4275_B53A_A7BA00BD4408__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// GenericComboBoxListCtrl.h : header file
//
#include <afxtempl.h>
#include "WindowsHooks.h"
#include "CWndDpiAware.h"

/////////////////////////////////////////////////////////////////////////////
// CGenericComboBoxListCtrl window
class CGenericComboBox;
class CGradientCache;

class CGenericComboBoxListCtrl : public CWndDpiAware<CListCtrl>, protected WindowsHooks::CMouseHook
{
  public:
	struct Handler
	{
		bool DrawImages = true;
		bool ColorableContent = false;

		virtual ~Handler()
		{
		}

		// Each pointer passed as argument may be NULL, which means
		// it is not currently wanted. This method is used by all
		// other methods to get information about items.
		virtual bool GetItemInfo(int nRow, CStringW* text, CStringW* tip, int* image, UINT* state) = 0;

		// This method is called in MouseHook procedure to determine
		// if user just clicked somewhere to hide the dropdown or user
		// clicked somewhere within control owning this dropdown list.
		// If implementation returns false, OnLButtonDown of list is called,
		// which in case that point is outside of its range, causes hiding of list.
		virtual bool HandleLButtonDown(const CPoint& pnt) = 0;

		virtual CWnd* GetFocusHolder() const = 0; // return control to hold focus or nullptr
		virtual CWnd* GetParent() const = 0;      // must not return nullptr (return desktop if you have no window)
		virtual void OnInit(CListCtrl& list) = 0; // called in initialization method
		virtual void OnSelect(int index) = 0;     // called when user selects item
		virtual void OnShow() = 0;                // called when window is going to be showed and after it
		virtual bool OnHide() = 0;                // called when window is going to be hided and after it
		virtual void OnVisibleChanged(){};        // called when window is shown or hidden
		virtual void OnSettingsChanged() = 0;     // should be called when settings have changed

		// is called for each message of list, return true if message is processed
		virtual bool OnWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& user_result) = 0;

		virtual int GetItemsCount() = 0;            // return current items count
		virtual int GetCurSel() = 0;                // return currently selected item
		virtual bool IsVS2010ColouringActive() = 0; // true if extended theming is active

		// Default implementations use GetItemInfo!
		virtual void GetDispInfoW(NMHDR* pNMHDR, LRESULT* pResult);
		virtual void GetDispInfoA(NMHDR* pNMHDR, LRESULT* pResult);

		virtual VaFontType GetFont();

		// Following methods are helper wrappers around GetItemInfo,
		// so do not use those methods to implement it.
		bool GetItemText(int nRow, CStringW& str);
		bool GetItemTip(int nRow, CStringW& str);
		bool GetItemTip(int nRow, WTString& str);
		bool GetItemImage(int nRow, int& img);
	};

	Handler* GetHandler()
	{
		return m_handler.get();
	}
	void ResetHandler(const std::shared_ptr<Handler>& handler)
	{
		m_handler = handler;
		SettingsChanged();
	}

	// Construction
  public:
	CGenericComboBoxListCtrl();

	// Attributes
  public:
	// Operations
  public:
	void Display(CRect rc);
	void Init(std::shared_ptr<Handler> handler);
	void SettingsChanged();
	virtual ~CGenericComboBoxListCtrl();

	// Overrides
  protected:
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGenericComboBoxListCtrl)
  public:
  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam);
	//}}AFX_VIRTUAL
	virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const;
	virtual BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);

	// Implementation
	// Generated message map functions
	//{{AFX_MSG(CGenericComboBoxListCtrl)
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnSetFont(WPARAM wParam, LPARAM);
	afx_msg void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()

	virtual void OnMouseHookMessage(int code, UINT message, MOUSEHOOKSTRUCT* lpMHS);

  private:
	int m_nLastItem;
	std::shared_ptr<Handler> m_handler;
	std::shared_ptr<CGradientCache> background_gradient_cache;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GenericComboBoxListCtrl_H__552D4FC2_CF60_4275_B53A_A7BA00BD4408__INCLUDED_)
