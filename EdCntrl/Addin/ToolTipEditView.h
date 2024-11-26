#if !defined(AFX_TOOLTIPEDITVIEW_H__B57A4A4F_A81F_48F0_B804_CC2209B13535__INCLUDED_)
#define AFX_TOOLTIPEDITVIEW_H__B57A4A4F_A81F_48F0_B804_CC2209B13535__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include "CWndDpiAware.h"
// ToolTipEditView.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditView view

class CToolTipEditCombo;

enum class PartRole
{
	// those are used as index in arrays

	Context = 0,
	Definition = 1,
	Project = 2,
};

class CPartWithRole
{
protected:
	PartRole m_partRole;

	CPartWithRole(PartRole role = PartRole::Context)
	    : m_partRole(role)
	{
	
	}
  public:
	bool HasRole(PartRole role) const
	{
		return m_partRole == role;
	}

	bool HasRole(PartRole role, PartRole altRole) const
	{
		return HasRole(role) || HasRole(altRole);
	}
};

class CToolTipEditContext : protected CPartWithRole
{
	bool _listCreated[3] = {false, false, false};

public:
	static CToolTipEditContext* Get(); 


	void Reset()
	{
		m_partRole = PartRole::Context;
		for (auto& x : _listCreated)
			x = false;
	}
	
	PartRole CreationRole()
	{
		return m_partRole;
	}
	
	void SetCreationRole(PartRole role)
	{
		m_partRole = role;
	}

	bool& ViewListCreated(PartRole role)
	{
		return _listCreated[(size_t)role];
	}
};

class CToolTipEditView : public CWndDpiAwareMiniHelp<CView>, public CPartWithRole
{
  protected:
	CToolTipEditView(); // protected constructor used by dynamic creation
	DECLARE_DYNCREATE(CToolTipEditView)

	// Attributes
  public:
	virtual void OnDpiChanged(CWndDpiAware::DpiChange change, bool& handled);

	// Operations
  public:
	void SetWindowText(LPCWSTR lpszString, bool isColorableText);
	bool HasColorableText() const;
	void SettingsChanged();
	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

	CComboBox* GetComboBox();

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CToolTipEditView)
  protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void OnDraw(CDC* pDC); // overridden to draw this view
	                               //}}AFX_VIRTUAL

	// Implementation
  protected:
	virtual ~CToolTipEditView();
	void CheckChildren(bool sizing = false);
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

	// Generated message map functions
  protected:
	//{{AFX_MSG(CToolTipEditView)
	afx_msg int OnCreate(LPCREATESTRUCT pCreateStruc);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg void DoInvalidate();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	CToolTipCtrl mToolTipCtrl;
	CToolTipEditCombo* m_multiListBox;
	CWnd* mMethodsSpinner;
	CStringW m_txtList;
	int m_count;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TOOLTIPEDITVIEW_H__B57A4A4F_A81F_48F0_B804_CC2209B13535__INCLUDED_)
