#pragma once

#include "VAFileView.h"
#include "VAClassView.h"
#include "FlatSplitterWnd.h"

class VAWorkspaceViews : public CWndDpiAware<CFlatSplitterWnd>
{
	static VAWorkspaceViews* sViews;
	CVAClassView m_VAClassView;
	VAFileView m_VAFileView;
	BOOL m_created;

	VAWorkspaceViews();
	VAWorkspaceViews(CWnd* parent);
	~VAWorkspaceViews();

	static void CALLBACK PrecreateGotoFilesTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
	static void CALLBACK PrecreateGotoSymbolsTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
	static void CALLBACK PrecreateGotoOutlineTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
	static void CALLBACK PrecreateGotoHcbTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
	static void CALLBACK PrecreateGotoMruTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
	static void GotoWorkspace(UINT_PTR timerId, TIMERPROC lpTimerFunc, bool vaView = true);

	virtual CWnd* GetActivePane(int* /* pRow = NULL */, int* /* pCol = NULL */)
	{
		return NULL;
	}
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	void SizeViews();
	virtual void TrackRowSize(int y, int row);
	virtual void OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rectArg);

	//{{AFX_MSG(VAWorkspaceViews)
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	//}}AFX_MSG

  public:
	static void CreateWorkspaceView(HWND parent);

	static void GotoFilesInWorkspace();
	static void GotoSymbolsInWorkspace();
	static void GotoHcb();
	static void GotoMru();
	static void GotoVaOutline();
	static void MoveFocus(bool goReverse);
	static void Activated();
	static void ThemeChanged();

	enum FocusTarget
	{
		ftFis,
		ftDefault = ftFis,
		ftSis,
		ftMru,
		ftHcbLock,
		ftHcb,
		ftCount
	};
	static void SetFocusTarget(FocusTarget tgt);

	// called from package for IDE command integration
	static DWORD QueryStatus(DWORD cmdId);
	static HRESULT Exec(DWORD cmdId);

	DECLARE_MESSAGE_MAP()
  private:
	FocusTarget mFocusTarget;

  protected:
	virtual void OnDpiChanged(DpiChange change, bool& handled);
};
