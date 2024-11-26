#if !defined(AFX_TIPOFTHEDAY_H__443B44AA_B3E9_446B_8B8F_701CE2F1EC76__INCLUDED_)
#define AFX_TIPOFTHEDAY_H__443B44AA_B3E9_446B_8B8F_701CE2F1EC76__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// TipOfTheDay.h : header file
//
#include "HTMLDialog\HtmlDialog.h"
#include "VAThemeDraw.h"

/////////////////////////////////////////////////////////////////////////////
// CTipOfTheDay dialog

class CTipOfTheDay : public CThemedHtmlDlg
{
	// Construction
  public:
	CTipOfTheDay(CWnd* pParent = NULL); // standard constructor

	virtual bool ShowTip();
	void DoModeless();

	static void LaunchTipOfTheDay(BOOL manual = FALSE);
	static BOOL CloseTipOfTheDay();
	static BOOL HasFocus()
	{
		return sHasFocus;
	}

	// Dialog Data
	//{{AFX_DATA(CTipOfTheDay)
	enum
	{
		IDD = IDD_TIPOFTHEDAY
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CTipOfTheDay)
  protected:
	virtual BOOL OnInitDialog();

	virtual void InitializeDayCounter();
	virtual BOOL &GetShowAtStartVar() const;
	virtual const char* GetRegValueName() const { return "ShowTipOfTheDay"; }

	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual void OnOK();
	virtual void OnCancel();
	virtual void PostNcDestroy();
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	// Generated message map functions
	//{{AFX_MSG(CTipOfTheDay)
	afx_msg void OnCheckboxTick();
	afx_msg virtual void OnBnClickedPrevtip();
	afx_msg virtual void OnBnClickedNexttip();
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  protected:
	int m_tipNo = 0;
	bool mModal;
	CWnd* mParent;
	static BOOL sHasFocus;

	static CString GetCurrDateStr();
	void RefreshRegStr();
};

class CInfoOfTheDay : public CTipOfTheDay
{
  public:
	CInfoOfTheDay(CWnd* pParent = NULL)
	    : CTipOfTheDay(pParent) {}

	virtual bool ShowTip() override;

	static void LaunchInfoOfTheDay();

  protected:
	virtual void InitializeDayCounter() override {}
	virtual BOOL OnInitDialog() override;
	virtual BOOL& GetShowAtStartVar() const override;
	virtual const char* GetRegValueName() const override { return "ShowInfoOfTheDay"; }
	afx_msg virtual void OnBnClickedPrevtip() override {}
	afx_msg virtual void OnBnClickedNexttip() override {}
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TIPOFTHEDAY_H__443B44AA_B3E9_446B_8B8F_701CE2F1EC76__INCLUDED_)
