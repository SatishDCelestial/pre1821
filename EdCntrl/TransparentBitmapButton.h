#ifndef TransparentBitmapButton_h__
#define TransparentBitmapButton_h__

#include "VAThemeDraw.h"
#include "ImageParameters140.h"

class TransparentBitmapButton : public CBitmapButton
{
  public:
	TransparentBitmapButton()
	{
	}

	void LoadBitmap(int idImageRes, int idSelectedImageRes = 0, int idFocusedImageRes = 0, int idDisabledImageRes = 0)
	{
		HBITMAP hBit = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idImageRes), IMAGE_BITMAP, 0, 0,
		                                    LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
		m_bitmap.m_hObject = hBit;

		if (idSelectedImageRes)
		{
			hBit = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idSelectedImageRes), IMAGE_BITMAP, 0, 0,
			                            LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
			m_bitmapSel.m_hObject = hBit;
		}

		if (idFocusedImageRes)
		{
			hBit = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idFocusedImageRes), IMAGE_BITMAP, 0, 0,
			                            LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
			m_bitmapFocus.m_hObject = hBit;
		}

		if (idDisabledImageRes)
		{
			hBit = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idDisabledImageRes), IMAGE_BITMAP, 0, 0,
			                            LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
			m_bitmapDisabled.m_hObject = hBit;
		}
	}
};

class CheckableBitmapButton : public TransparentBitmapButton, public ThemeDraw::IStateHandler
{
	friend class FindUsageDlg;
	friend class VaHashtagsFrame;

  public:
	CheckableBitmapButton() : mIsCheckbox(FALSE), mChecked(FALSE), mTheming(FALSE)
	{
		memset(mThemeColors, 0, sizeof(mThemeColors));
	}

	static bool IsThemingSupported();
	void EnableCheckboxBehavior(BOOL b)
	{
		mIsCheckbox = b;
	}

	void UpdateThemeColors();
	void SetBarRect(const CRect& rect)
	{
		mBarRect = rect;
	}
	bool HasThemeColors() const
	{
		return mThemeColors[btn_bg_count] != 0;
	}

	enum color_id
	{
		btn_bg_count,
		btn_bg0,
		btn_bg1,
		btn_bg2,
		btn_bg3,
		btn_bg_border,
		btn_sel_count,
		btn_sel0,
		btn_sel1,
		btn_sel2,
		btn_sel3,
		btn_sel_border,
		btn_down_count,
		btn_down0,
		btn_down1,
		btn_down2,
		btn_down3,
		btn_down_border,
		btn_over_count,
		btn_over0,
		btn_over1,
		btn_over2,
		btn_over3,
		btn_over_border,
		btn_sel_over_count,
		btn_sel_over0,
		btn_sel_over1,
		btn_sel_over2,
		btn_sel_over3,
		btn_sel_over_border,

		btn_num_colors
	};

	bool GetThemeColor(color_id id, COLORREF& color);

	void DrawCommandbarBgEx(CDC* pDC, const CRect& rect, int count_id, bool border, bool deflate_interior);
	void DrawCommandbarBg(CDC* pDC, const CRect& rect);
	void DrawCommandbarButtonBg(CDC* pDC, const CRect& rect, int count_id = btn_bg_count);

	void Substitute(CDialog* parent, CToolTipCtrl* tooltips, /* can be nullptr */
	                int placeholderDlgId, LPCSTR buttonTxt, int idImageRes, int idSelectedImageRes,
	                int idFocusedImageRes, int idDisabledImageRes, bool isCheckbox = false, bool isFocusable = false);

  protected:
	DECLARE_MESSAGE_MAP()
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);

	void DrawItemEx(CDC* pDC, const CRect& rect);

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	// ThemeDraw::ItemState [BEGIN]
	virtual bool IsEnabled() const
	{
		return mTheming != FALSE;
	}
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd)
	{
		_ASSERTE(mTheming != FALSE);
		wnd->RedrawWindow(NULL, NULL, RDW_INVALIDATE);
	}
	// ThemeDraw::ItemState [END]

  private:
	BOOL mIsCheckbox;
	BOOL mChecked;
	BOOL mTheming;

	COLORREF mThemeColors[btn_num_colors];
	CRect mBarRect;

	ThemeDraw::ItemState mState;

	LRESULT OnSetCheck(WPARAM wParam, LPARAM lParam);
	LRESULT OnGetCheck(WPARAM wParam, LPARAM lParam);

	CBitmap m_bitmapPushed;
};

#endif // TransparentBitmapButton_h__
