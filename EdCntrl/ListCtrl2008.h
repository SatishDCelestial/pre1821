#pragma once

#include "dynwindow/cdxCDynamicWnd.h"
#include "ColorListControls.h"

#if (_MSC_VER < 1400)
#define NCHT_RET UINT
#else
#define NCHT_RET LRESULT
#endif

class CDoubleBuffer;

// CListCtrl2008

class CListCtrl2008 : public CColorVS2010ListCtrl
{
	DECLARE_DYNAMIC(CListCtrl2008)

  public:
	CListCtrl2008();
	virtual ~CListCtrl2008();

	virtual bool LC2008_DisableAll() const;
	virtual bool LC2008_HasFancyBorder() const;
	virtual bool LC2008_HasHorizontalResize() const;
	virtual bool LC2008_HasVerticalResize() const;
	virtual bool LC2008_HasCtrlFadeEffect() const;
	virtual bool LC2008_HasToolbarFadeInEffect() const;
	virtual unsigned int LC2008_GetFadeTime_ms() const;
	virtual double LC2008_GetFadeLevel() const;
	virtual bool LC2008_HasGranularVerticalResize() const; // this will hide horizontal scrollbar (as in VS2008)
	// 	virtual unsigned int LC2008_GetMinimumVisibleItems() const;
	// 	virtual unsigned int LC2008_GetMaximumVisibleItems() const;
	virtual bool LC2008_DoRestoreLastWidth() const;
	virtual bool LC2008_DoRestoreLastHeight() const;
	virtual bool LC2008_DoMoveTooltipOnVScroll() const;
	virtual bool LC2008_EnsureVisibleOnResize() const;
	virtual bool LC2008_DoSetSaveBits() const;
	virtual bool LC2008_ThreadedFade() const;
	virtual bool LC2008_UseHomeMadeDoubleBuffering() const;
	virtual bool LC2008_DoFixAutoFitWidth() const; // should be false for now

	void AbortCtrlFadeEffect();
	void AddCompanion(HWND hwnd, cdxCDynamicWnd::Mode horanchor = cdxCDynamicWnd::mdRepos,
	                  cdxCDynamicWnd::Mode veranchor = cdxCDynamicWnd::mdRepos,
	                  bool restore_initial_transparency = true); // currently, only mdNone and mdRepos are supported!
	void RemoveCompanion(HWND hwnd);
	void FadeInCompanion(HWND hwnd);
	BOOL SetColumnWidth(int nCol, int cx);

  protected:
	CCriticalSection cs;
	unsigned int transition;                  // fade in/out in progress when not null
	bool transparent;                         // if true, fade out is in progress
	bool aborted;                             // abort fade on next timer message
	bool skip_me;                             // fade only companion windows; don't react on ctrl key
	stdext::hash_map<HWND, bool> companions;  // <hwnd, original_was_layered>
	stdext::hash_set<HWND> fadein_companions; // companions that are currently initially faded in

	HANDLE fader_thread;
	volatile bool fader_exit;
	bool fader_is_running = false;

	CDoubleBuffer* mDblBuff;

	void DoEnsureVisible();
	void RedrawScrollbar();
	static UINT FaderThread(void* param);
	void FaderThread();

	DECLARE_MESSAGE_MAP()
  public:
	virtual BOOL CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  private:
	using CWnd::CreateEx;

  public:
	virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
	                    CWnd* pParentWnd, UINT nID, CCreateContext* pContext);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg NCHT_RET OnNcHitTest(CPoint point);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
	afx_msg void OnStyleChanging(int styletype, LPSTYLESTRUCT ss);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnRedrawScrollbar(WPARAM wparam, LPARAM lparam);
	afx_msg LRESULT OnPostponedSetFocus(WPARAM wparam, LPARAM lparam);
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	void GetSizingRect(UINT fwSide, LPRECT pRect);

	volatile bool make_visible_after_first_wmpaint;

  protected:
	virtual LRESULT WindowProc(UINT msg, WPARAM wparam, LPARAM lparam);
};

void SetLayeredWindowAttributes_update(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags);
