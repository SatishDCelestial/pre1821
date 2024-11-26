#if !defined(AFX_VACOMPLETIONBOX_H__8F76379A_A0C4_4D63_A6B2_30365CBC1F99__INCLUDED_)
#define AFX_VACOMPLETIONBOX_H__8F76379A_A0C4_4D63_A6B2_30365CBC1F99__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// VACompletionBox.h : header file
//

#include "ListCtrl2008.h"

class VACompletionSet;
class ArgToolTip;
class CDoubleBuffer;

// [case 142193] owner control to handle WM_MEASUREITEM
class VACompletionBoxOwner : public CWnd
{
	int label_height = 0;
	int icon_height = 0;
	int bounds_height = 0;
	VaFont font_obj;
	int font_settings_generation = -1;
	int dpi = 0;

  public:
	int GetItemHeight(int code = LVIR_BOUNDS);
	void UpdateMetrics();

  protected:
	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
};

/////////////////////////////////////////////////////////////////////////////
// VACompletionBox window
class VACompletionBox : public CListCtrl2008
{
	VaFont m_boldFont;
#ifdef _WIN64
	CBitmap m_tomato;
	UINT m_tomatoDpi = 0;
#endif
	// Construction
  public:
	VACompletionSet* m_compSet;
	VACompletionBox();
	ArgToolTip* m_tooltip;

	virtual bool UpdateFonts(VaFontTypeFlags changed, UINT dpi = 0);

	const VaFont& GetDpiAwareFont(bool bold)
	{
		if (bold)
			return m_boldFont;
		return m_font;
	}

	// Attributes
  public:
	enum timer_ids : UINT_PTR
	{
		Timer_DismissIfCaretMoved = 1,
		Timer_UpdateTooltip,
		Timer_RepositionToolTip,
		Timer_ReDisplay
	};

	// Operations
  public:
	bool UseVsTheme() const;
	void HideTooltip();

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VACompletionBox)
  public:
	virtual BOOL Create(CWnd* pParentWnd);

  private:
	using CListCtrl2008::Create;

  public:
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual void DrawItem(CDC* pDC, int nItem, CRect* updated_region = NULL);

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

	// Implementation
  public:
	void CloseExp();
	virtual ~VACompletionBox();

	POSITION GetFocusedItem() const
	{
		ASSERT(::IsWindow(m_hWnd));
		return (POSITION)(DWORD_PTR)(1 + GetNextItem(-1, LVIS_FOCUSED));
	}

	// Generated message map functions
  protected:
	//{{AFX_MSG(VACompletionBox)
	afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	//}}AFX_MSG

	CDoubleBuffer* mDblBuff;

	DECLARE_MESSAGE_MAP()
  public:
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnReturnFocusToEditor(WPARAM wparam = 0, LPARAM lparam = 0);
	int GetFrameWidth();
};

/////////////////////////////////////////////////////////////////////////////
// In the expansion box, we may use an icon as a type like file icons and autotext
#define IMAGE_TYPE_BIT_FLAG 0x77000000
#define VSNET_TYPE_BIT_FLAG 0x00800000
#define IS_IMG_TYPE(type) (IMAGE_TYPE_BIT_FLAG == (type & 0xff000000))
#define IS_VSNET_IMG_TYPE(type) ((type & VSNET_TYPE_BIT_FLAG) && IS_IMG_TYPE(type))
#define IMG_IDX_TO_TYPE(img) (IMAGE_TYPE_BIT_FLAG | TYPE_TO_IMG_IDX(img))

// Do not use TYPE_TO_IMG_IDX without first checking IS_IMG_TYPE (or IS_VSNET_IMG_TYPE)
#define TYPE_TO_IMG_IDX(type) (int)((type)&0x000fffff)

#define VSNET_SNIPPET_IDX 0xCD
#define VSNET_RESERVED_WORD_IDX 0xCE

int GetTypeImgIdx(uint type, uint symAttributes, bool icon_for_completion_box = false);

COLORREF WTColorFromType(int type);
//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VACOMPLETIONBOX_H__8F76379A_A0C4_4D63_A6B2_30365CBC1F99__INCLUDED_)
