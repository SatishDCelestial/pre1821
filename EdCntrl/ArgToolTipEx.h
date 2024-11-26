#if !defined(AFX_ArgToolTipExEX_H__302740C9_99EB_442C_A451_54D29CBCF77E__INCLUDED_)
#define AFX_ArgToolTipExEX_H__302740C9_99EB_442C_A451_54D29CBCF77E__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "ArgToolTip.h"
class EdCnt;

/////////////////////////////////////////////////////////////////////////////
// ArgToolTipEx window
class ArgToolTipEx : public ArgToolTip
{
	friend class ArgToolTipExNoHide;

	CFontW mFontOverride;
	FormattedTextLines mLines;
	DWORD mDuration;
	DWORD mMinDuration;
	DWORD mNumTimerTicks;
	CRect mMargin;
	int mIgnoreHide;
	std::function<bool(CPoint& pt)> mPosTrackingFunc;
	bool mIsTrackingPos;

  public:
	std::map<BYTE, FormatRenderer> renderers;
	std::function<void(FormattedTextLines&)> preprocess_textlines;

	bool mSmartSelectMode; // special layout for Smart Select

	// Helps to keep tooltip visible during redraws of EdCtrl
	class KeepVisible
	{
		friend class ArgToolTipEx;
		std::shared_ptr<ArgToolTipEx> m_tt;

	  public:
		KeepVisible(std::shared_ptr<ArgToolTipEx>& tt) : m_tt(tt)
		{
			if (m_tt)
				m_tt->mIgnoreHide++;
		}

		~KeepVisible()
		{
			if (m_tt && m_tt->mIgnoreHide > 0)
				m_tt->mIgnoreHide--;
		}
	};

	ArgToolTipEx(EdCnt* par);
	ArgToolTipEx(HWND hParent);
	virtual ~ArgToolTipEx();

	// Implementation
  public:
	void Display(CPoint* pt, LPCWSTR left, LPCWSTR cur, LPCWSTR right, int totalDefs, int currentDef,
	             bool reverseColor = false, bool keeponscreen = true, BOOL color = TRUE, BOOL ourTip = TRUE);
	void DisplayWstr(CPoint* pt, const CStringW& left, const CStringW& cur, const CStringW& right, int totalDefs,
	                 int currentDef, bool reverseColor = false, bool keeponscreen = true, BOOL color = TRUE,
	                 BOOL ourTip = TRUE);

	// Direct means, that DecodeScope method is not applied!!!
	void DisplayWstrDirect(CPoint* pt, const CStringW& left, const CStringW& cur, const CStringW& right, int totalDefs,
	                       int currentDef, bool reverseColor = false, bool keeponscreen = true, BOOL color = TRUE,
	                       BOOL ourTip = TRUE);
	void SetDuration(DWORD duration, DWORD minimum = 0);
	void OverrideFont(const CFontW& font);

	// defines margins in logical units
	void SetMargin(UINT left, UINT top, UINT right, UINT bottom);

	// Lambda takes reference to point which contains coordinates being used to position window.
	// - return "true" when coordinates of the point are OK to move window
	// - return "false" to disallow such movement
	// Note: Lambda can modify point and return "true" to specify new window position
	void TrackPosition(std::function<bool(CPoint& pt)> pos_func, int duration);

	const CRect& Margin() const
	{
		return mMargin;
	}
	const CFontW& FontOverride() const
	{
		return mFontOverride;
	}
	const EdCnt* GetEd() const
	{
		return m_ed;
	}
	HWND GetParentHWND() const
	{
		return m_hPar;
	}

	virtual void Layout(bool in_paint) override;
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
};

extern DWORD sToolTipWrapperStartTicks;
extern CPoint sToolTipWrapperStartPt;

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ArgToolTipExEX_H__302740C9_99EB_442C_A451_54D29CBCF77E__INCLUDED_)
