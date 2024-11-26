#pragma once

#include "resource.h"
#include "VADialog.h"
#include "afxwin.h"
#include "CtrlBackspaceEdit.h"
#include "VAThemeDraw.h"

class CColumnTreeCtrl;
template <typename TREECTRL> class IFindTargetWithTree;

class FindTextDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(FindTextDlg)
  public:
	FindTextDlg(IFindTargetWithTree<CColumnTreeCtrl>* theFindTarget);
	virtual ~FindTextDlg();

	virtual BOOL OnInitDialog();

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support

	void UpdateGUI();
	void HandleFind(bool next);
	void UpdateMarkAll();

	DECLARE_MESSAGE_MAP()

  private:
	enum
	{
		IDD = IDD_FINDDLG
	};

  public:
	afx_msg void OnClose();

  protected:
	CThemedComboBox m_find;
	// CtrlBackspaceEdit<CWndSubW<CWnd>> m_find_edit;
	CThemedCheckBox m_markall;
	CThemedButton m_find_next;
	CThemedButton m_find_prev;
	IFindTargetWithTree<CColumnTreeCtrl>& mSearchTarget;
	CThemedButton m_goto;
	CThemedCheckBox m_matchcase;

  public:
	afx_msg void OnBnClickedFindNext();
	afx_msg void OnBnClickedFindPrev();
	afx_msg void OnCbnEditchangeFindedit();
	afx_msg void OnCbnSelchangeFindedit();
	afx_msg void OnBnClickedGoto();
	virtual BOOL DestroyWindow();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedMarkall();
	afx_msg void OnBnClickedMatchCase();
};
