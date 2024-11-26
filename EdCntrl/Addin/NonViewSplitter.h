#if !defined(AFX_NONVIEWSPLITTER_H__39809B42_024A_11D2_8C65_000000000000__INCLUDED_)
#define AFX_NONVIEWSPLITTER_H__39809B42_024A_11D2_8C65_000000000000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// NonViewSplitter.h : header file
//

#ifndef __AFXEXT_H__
#include <afxext.h>
#endif

#include "CWndDpiAware.h"

class MiniHelpFrm;

/////////////////////////////////////////////////////////////////////////////
// NonViewSplitter window

class NonViewSplitter : public CWndDpiAware<CSplitterWnd>
{
	enum HitTestValue
	{
		noHit = 0,
		vSplitterBox = 1,
		hSplitterBox = 2,
		bothSplitterBox = 3, // just for keyboard
		vSplitterBar1 = 101,
		vSplitterBar15 = 115,
		hSplitterBar1 = 201,
		hSplitterBar15 = 215,
		splitterIntersection1 = 301,
		splitterIntersection225 = 525
	};

	struct TrackingCounter
	{
		int& _tracking;
		TrackingCounter(int& _tr)
		    : _tracking(_tr)
		{
			_tracking++;
		}
		~TrackingCounter()
		{
			_tracking--;
		}
	};

	// Construction
  public:
	NonViewSplitter();

	// Attributes
  protected:
	MiniHelpFrm* m_parent;

	// custom tracking 
	bool m_useTracker = false;		// true to show tracking bar
	CStatic m_tracker;
	bool m_directTracking = true;	// true to resize columns while tracking
	int m_trackingCol = -1;

	// proportional resizing of columns
	std::vector<float> m_colProps;
	int m_afterTracking = 0;
	int m_changedCol = -1;

	// to avoid doing updates when nothing has changed
	std::vector<int> last_columns;
	CSize last_size;

	void GetSizes(std::vector<int>& columns, CSize& size) const;
	void DpiScaleFields();
	bool HaveSizesChanged();

	void StopTracking(BOOL bAccept) override;
	void TrackColumnSize(int x, int col) override;
	void OnDpiChanged(DpiChange change, bool& handled) override;

	void SaveSettings() const;

	void OnInvertTracker(const CRect& rect) override;
	void StartTracking(int ht) override;
	void InitTracker();

	// Operations
  public:
	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(NonViewSplitter)
	//}}AFX_VIRTUAL

	// Implementation
  public:
	virtual ~NonViewSplitter();
	virtual BOOL CreateMinihelp(MiniHelpFrm* pParentWnd);
	void SettingsChanged();

  private:
	using CSplitterWnd::CreateStatic;

  public:
	bool IsLayoutValid();
	void FitColumnsToWidth();
	bool SetProps();

	//////////////////////////
	//  these 2 functions are the only reasons for this class
	virtual CWnd* GetActivePane(int* /* pRow = NULL */, int* /* pCol = NULL */)
	{
		return NULL;
	}
	void RecalcLayout(bool parent);
	virtual void RecalcLayout()
	{
		RecalcLayout(true);
	}

	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	//////////////////////////
	// due to crash that Keith experienced
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	// Generated message map functions
  protected:
	virtual void OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rect);
	virtual void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	LRESULT OnUpdateMyBorder(WPARAM wparam, LPARAM lparam);

	//{{AFX_MSG(NonViewSplitter)
	// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_NONVIEWSPLITTER_H__39809B42_024A_11D2_8C65_000000000000__INCLUDED_)
