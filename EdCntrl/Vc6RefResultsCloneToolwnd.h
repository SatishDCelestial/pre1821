#pragma once

class PrimaryResultsFrame;
class SecondaryResultsFrameVc6;

class Vc6RefResultsCloneToolwnd : public CWnd
{
  public:
	Vc6RefResultsCloneToolwnd(const PrimaryResultsFrame* refsToCopy);
	virtual ~Vc6RefResultsCloneToolwnd();

	DECLARE_MESSAGE_MAP()
	//{{AFX_MSG(Vc6RefResultsCloneToolwnd)
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnNcDestroy();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	//}}AFX_MSG

  private:
	SecondaryResultsFrameVc6* mClient; // weak reference
	bool mIsTopMost;
};
