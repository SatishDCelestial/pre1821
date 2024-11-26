#pragma once

#include "FindInWkspcDlg.h"
#include "FilterableSet.h"
#include "Menuxp\MenuXP.h"
#include "Foo.h"
#include "PooledThreadBase.h"
#include "afxwin.h"

class DType;

class VABrowseMembers : public FindInWkspcDlg
{
	DECLARE_MENUXP()

	friend void VABrowseMembersDlg(DType* type);
	// Construction
  private:
	VABrowseMembers(DType* type); // standard constructor
	FilteredDTypeList m_data;
	DType* m_type;
	DTypeList m_results;

  protected:
	virtual void PopulateList() override;
	virtual void UpdateFilter(bool force = false) override;

  private:
	void UpdateTitle();
	void GetItemLocation(int itemRow, CStringW& file, int& lineNo);
	int GetWorkingSetCount() const;
	void GetDataForCurrentSelection();

	enum
	{
		IDD = IDD_VABROWSEMEMBERS
	};

  protected:
	static void MemberLoader(LPVOID pVaBrowseSym);
	void PopulateListThreadFunc();
	virtual int GetFilterTimerPeriod() const;
	virtual void GetTooltipText(int itemRow, WTString& txt);
	virtual void GetTooltipTextW(int itemRow, CStringW& txt);
	void StopThreadAndWait();

	virtual void OnOK();
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnCopy();
	afx_msg void OnDestroy();
	afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnRefactorFindRefs();
	afx_msg void OnToggleSortByType();
	afx_msg void OnToggleShowBaseMembers();
	afx_msg void OnToggleCaseSensitivity();
	afx_msg void OnToggleTooltips();
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	DECLARE_MESSAGE_MAP()
  private:
	DTypeList mDtypes;
	bool mSortByType;
	bool mCanSortByType;
	bool mShowBaseMembers;
	bool mCanHaveBaseMembers;
	FunctionThread mLoader;
	bool mFilterEdited = false;
	bool mStopThread = false;
};

extern void VABrowseMembersDlg(DType* type);
