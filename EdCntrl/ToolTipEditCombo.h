#if !defined(AFX_TOOLTIPEDITCOMBO_H__651FBA55_250A_4E8E_9C1E_BCBF371A29BF__INCLUDED_)
#define AFX_TOOLTIPEDITCOMBO_H__651FBA55_250A_4E8E_9C1E_BCBF371A29BF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ToolTipEditCombo.h : header file
//

#include "utils_goran.h"
#include "ColorListControls.h"
#include "CWndDpiAware.h"
#include "Addin/ToolTipEditView.h"

class CColourizedControl;
typedef std::shared_ptr<CColourizedControl> CColourizedControlPtr;

namespace ThemeUtils
{
class CMemDC;
}

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditCombo window

class CToolTipEditCombo : public CWndDpiAwareMiniHelp<CComboBoxEx>, public IVS2010ComboInfo, public CPartWithRole
{
	// Construction
  public:
	CToolTipEditCombo(PartRole role);
	virtual BOOL Create(_In_ DWORD dwStyle, _In_ const RECT& rect, _In_ CWnd* pParentWnd, _In_ UINT nID);

	struct ISubclasser
	{
		virtual bool IsSubclassed() const = 0;
		virtual void Subclass(CToolTipEditCombo* combo) = 0;
		virtual void SetDropRect(const CRect& rect) = 0;
		virtual void Unsubclass() = 0;
		virtual ~ISubclasser() = default;
	};

  private:
	using CWnd::Create;

  public:
	virtual void OnDpiChanged(DpiChange change, bool& handled);

	// Attributes
  public:
	// Operations
  public:
	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CToolTipEditCombo)
  protected:
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~CToolTipEditCombo();
	INT GetContextList(WTString filter = WTString());
	INT GetProjectList();
	void RecalcDropWidth();
	void AdjustDropRectangle(const CRect& restrictTo, int dropWidth);
	void GotoCurMember();
	void FilterList();
	void ClearEditAndDrop();
	void SetColorableText(bool isColorable);
	bool HasColorableText() const
	{
		return mHasColorableText;
	}
	void SettingsChanged();
	virtual COLORREF GetVS2010ComboBackgroundColour() const;
	virtual bool ComboDrawsItsOwnBorder() const;
	virtual ISubclasser* CreateSubclasser();

	void ShowDropDown(BOOL bShowIt);
	void BeginPopupListPaint(CWnd* lst);
	void EndPopupListPaint();
	BOOL GetLBTextW(int index, CStringW& text);

	CGradientCache popup_background_gradient_cache;

	// Generated message map functions
  protected:
	//{{AFX_MSG(CToolTipEditCombo)
	afx_msg void OnDropdown();
	afx_msg void OnSelchange();
	afx_msg void OnSetfocus();
	afx_msg void OnSelendok();
	afx_msg void OnCloseup();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDrawItem(int id, LPDRAWITEMSTRUCT dis);
	//}}AFX_MSG

  private:
	ISubclasser* m_subclasser;
	bool mFilteringActive = false;
	bool mHasColorableText = true;
	CStringW mCurFilter;
	CBrush bgbrush_cache;
	mutable std::optional<COLORREF> combo_background_cache;
	CColourizedControlPtr mColourizedEdit;
	LineMarkersPtr mGotoMarkers;
	std::unique_ptr<std::vector<std::tuple<CStringW, int>>> mProjects;
	CStringW mTextOnDrop;
	CCriticalSection mTextLock;

	// DCs used for flicker free drawing of popup list
	// (in this class because the owner draws the items, not the control)
	CClientDC* mListWndDC = nullptr;
	ThemeUtils::CMemDC* mListMemDc = nullptr;

	DECLARE_MESSAGE_MAP()
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CallBaseWndProcHelper(UINT message, WPARAM wparam, LPARAM lparam);
};

/////////////////////////////////////////////////////////////////////////////

void GotoMethodList();
bool IsMinihelpDropdownVisible();
void HideMinihelpDropdown();
CToolTipEditCombo* GetToolTipEditCombo(PartRole role);

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TOOLTIPEDITCOMBO_H__651FBA55_250A_4E8E_9C1E_BCBF371A29BF__INCLUDED_)
