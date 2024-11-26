#pragma once

#include "ColorListControls.h"
#include "VAThemeUtils.h"

using namespace ThemeUtils;

#ifndef RAD_STUDIO
#define CListCtrlFixedGrid_WATERMARK // [case: 149836]
#endif

class CListCtrlFixedGrid_HeaderCtrl : public CHeaderCtrl
{
  public:
	enum RenderState
	{
		rs_Default,
		rs_MouseDown,
		rs_MouseOver
	};

	class HeaderColorSchema
	{
	  public:
		COLORREF Default;
		COLORREF DefaultText;
		COLORREF Glyph;
		COLORREF MouseDown;
		COLORREF MouseDownGlyph;
		COLORREF MouseDownText;
		COLORREF MouseOver;
		COLORREF MouseOverGlyph;
		COLORREF MouseOverText;
		COLORREF SeparatorLine;

		HeaderColorSchema();

		COLORREF StateBGColor(RenderState rs) const;
		COLORREF StateTextColor(RenderState rs) const;
		COLORREF StateGlyphColor(RenderState rs) const;
	};

	struct Renderer
	{
		virtual ~Renderer() = default;
		virtual bool EraseHeaderBG(CDC* dcP, const HeaderColorSchema& colors) = 0;
		virtual void DrawHeaderItem(LPDRAWITEMSTRUCT lpDrawItemStruct, RenderState rs,
		                            const HeaderColorSchema& colors) = 0;
	};

	Renderer* renderer;
	RenderState hotState;
	int hotItem;
	bool columnResizing;
	HeaderColorSchema colors;

  public:
	CListCtrlFixedGrid_HeaderCtrl()
	    : renderer(nullptr), hotState(rs_Default), hotItem(-1), columnResizing(false), colors()
	{
	}

	virtual ~CListCtrlFixedGrid_HeaderCtrl()
	{
	}

  protected:
	virtual LRESULT WindowProc(UINT msg, WPARAM wparam, LPARAM lparam);
	void UpdateRenderState(RenderState rs);
	DECLARE_MESSAGE_MAP()

  public:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnHdnBegintrack(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHdnEndtrack(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseLeave();
};

// CListCtrlFixedGrid

class CListCtrlFixedGrid : public CColorVS2010ListCtrl, public CListCtrlFixedGrid_HeaderCtrl::Renderer
{
	friend class CListCtrlFixedGrid_HeaderCtrl;

	DECLARE_DYNAMIC(CListCtrlFixedGrid)
	bool m_in_paintEvent;
	bool m_winxp_doublebuffer_fix;
	bool m_simpleListMode;

  public:
	CListCtrlFixedGrid();
	virtual ~CListCtrlFixedGrid();

	enum DrawGridLinesFlag
	{
		dgl_None = 0,
		dgl_Horizontal = 0x1,
		dgl_Vertical = 0x2,
		dgl_Both = dgl_Vertical | dgl_Horizontal
	};

	bool OwnerDraw;
	CXTheme Theme;
	bool ThemeRendering;
	bool UseIDEThemeColors;

	bool DrawFocusRectangle; // defaults to false
	bool HotTrack;           // defaults to false
	bool DrawIcons;          // defaults to true
	bool SortArrow; // defaults to false. true showing the sorting direction of the column and is set up by the user of
	                // the class, see VAOpenFile for sample code
	bool SortReverse;
	int SortColumn;

	// if WS_BORDER is set, BorderColor is used to draw border,
	// defaults RGB(0,0,0)
	COLORREF BorderColor;
	COLORREF BorderColorFocused; // defaults to BorderColor

	COLORREF ItemBGColor;    // defaults to COLOR_WINDOW
	COLORREF ItemTextColor;  // defaults to COLOR_WINDOWTEXT
	COLORREF GridLinesColor; // defaults to COLOR_INACTIVEBORDER
	UINT DrawGridLinesFlags; // defaults to ibf_Vertical (does not test LVS_EX_GRIDLINES style)

	COLORREF SelectedItemBGBorderColor; // defaults to COLOR_HIGHLIGHT
	COLORREF SelectedItemBGColor;       // defaults to COLOR_HIGHLIGHT
	COLORREF SelectedItemTextColor;     // defaults to COLOR_WINDOWTEXT
	bool SelectedItemSyntaxHL;          // defaults to false

	COLORREF SelectedItemNoFocusBGBorderColor; // defaults to COLOR_BTNFACE
	COLORREF SelectedItemNoFocusBGColor;       // defaults to COLOR_BTNFACE
	COLORREF SelectedItemNoFocusTextColor;     // defaults to COLOR_WINDOWTEXT
	bool SelectedItemNoFocusSyntaxHL;          // defaults to false

	COLORREF HotItemOverlayBGBorderColor; // defaults to COLOR_HOTLIGHT
	COLORREF HotItemOverlayBGColor;       // defaults to COLOR_HOTLIGHT
	COLORREF HotItemTextColor;            // defaults to COLOR_WINDOWTEXT
	bool HotItemSyntaxHL;                 // defaults to false

	float SelectedItemBGOpacity;        // defaults to 1.0
	float SelectedItemNoFocusBGOpacity; // defaults to 1.0
	float HotItemBGOverlayOpacity;      // defaults to 1.0

#ifdef CListCtrlFixedGrid_WATERMARK
	CBitmap tomatoBGbmp;					// tomato pixel data source with default size
	COLORREF tomatoBGColor = 0;
	UINT tomatoBGMaxDPI = 0;
	VsUI::GdiplusImage tomatoBG;			// tomato pixel data proxy used for processing
	VsUI::GdiplusImage tomatoBGProcessed;	// sized, transparent, saturated image prepared for drawing
	CRect tomatoBGRect;						// location of tomato within control's client area
	static bool IsWatermarkEnabled()
	{
		// 0xFFVVDDAA => FF-Flags, Disable=1, V-Value, D-Desaturation, A-Alpha

		if (!gShellAttr->IsDevenv10OrHigher())
			return false;

		// we check Alpha and Disable bit

		return Psettings && 
			(BYTE)Psettings->mWatermarkProps && 
			!((Psettings->mWatermarkProps >> 24) & 0x01);
	}
#endif

	bool isInvalidated = false;

  protected:
	CListCtrlFixedGrid_HeaderCtrl headerctrl;
	int HotItemIndex;

	DECLARE_MESSAGE_MAP()

  public:
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnMouseLeave();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult);
	// 	afx_msg void OnToolTipShow(NMHDR *pNMHDR, LRESULT *pResult);
	// 	afx_msg void OnToolTipPop(NMHDR *pNMHDR, LRESULT *pResult);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	CStringW GetItemTextW(int nItem, int nSubItem) const;
	void ApplySimpleListMode();

  protected:
	virtual void PreSubclassWindow();
	virtual ULONG GetGestureStatus(CPoint ptTouch);
	virtual void InitAppearanceSchema();
	virtual void CustomDraw(_In_ LPNMLVCUSTOMDRAW lpLVCD);
	virtual void ProcessNewHotItem(int new_hot_item);
	void InvalidateVisibleArea(bool force = false);

	void DelayedRefresh(int ms_duration);

	// Interpolates color between two colors, frontOpacity is in range: 0.0 - 1.0
	static COLORREF InterpolateColor(COLORREF backColor, COLORREF frontColor, float frontOpacity)
	{
		if (frontOpacity == 1.0f)
			return frontColor;

		float backOpacity = 1.0f - frontOpacity;
		BYTE R = (BYTE)((float)GetRValue(backColor) * backOpacity + (float)GetRValue(frontColor) * frontOpacity);
		BYTE G = (BYTE)((float)GetGValue(backColor) * backOpacity + (float)GetGValue(frontColor) * frontOpacity);
		BYTE B = (BYTE)((float)GetBValue(backColor) * backOpacity + (float)GetBValue(frontColor) * frontOpacity);
		return RGB(R, G, B);
	}

	virtual void DrawHeaderItem(LPDRAWITEMSTRUCT lpDrawItemStruct, CListCtrlFixedGrid_HeaderCtrl::RenderState rs,
	                            const CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema& colors);
	virtual bool EraseHeaderBG(CDC* dcP, const CListCtrlFixedGrid_HeaderCtrl::HeaderColorSchema& colors);
	virtual void DrawBorder();

  public:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnNcPaint();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
};
