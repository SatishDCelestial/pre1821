#pragma once

enum class HowToShow
{
	ShowAsIs,     //	The displayed lines remain the same unless it is necessary to move the display to show the text. If
	              // the span is larger, bottom is preferred.
	ShowAsIsTop,  //	The displayed lines remain the same unless it is necessary to move the display to show the text. If
	              // the span is larger, top is preferred.
	ShowCentered, //	Centers the text pane around the indicated text.
	ShowTop,      //	Puts the first line at the top of the page.
};

// IEdCnt is an editor abstraction interface
interface IEdCnt
{
  public:
	virtual ~IEdCnt() = default;

	virtual CPoint vGetCaretPos() const = 0;
	virtual void vGetClientRect(LPRECT lpRect) const = 0;
	virtual void vClientToScreen(LPPOINT lpPoint) const = 0;
	virtual void vClientToScreen(LPRECT lpRect) const = 0;
	virtual void vScreenToClient(LPPOINT lpPoint) const = 0;
	virtual void vScreenToClient(LPRECT lpRect) const = 0;
	virtual CWnd* vGetFocus() const = 0;
	virtual CWnd* vSetFocus() = 0;
	virtual CPoint GetCharPos(long lChar) = 0;
	virtual int CharFromPos(POINT * pt, bool resolveVc6Tabs = false) = 0;
	virtual long GetFirstVisibleLine() = 0;
	virtual void GetVisibleLineRange(long& first, long& last) = 0;
	virtual void ResetZoomFactor() = 0;
	virtual bool TryToShow(long lStartPt, long lEndPt, HowToShow howToShow = HowToShow::ShowAsIsTop) = 0;
	virtual bool HasBlockOrMultiSelection() const = 0;
	virtual bool IsInitialized() const = 0;  // [case:146320]
};
