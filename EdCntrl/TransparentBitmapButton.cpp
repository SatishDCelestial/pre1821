#include "stdafxEd.h"
#include "TransparentBitmapButton.h"
#include "VAThemeUtils.h"
#include "ImageListManager.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "DevShellAttributes.h"

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CheckableBitmapButton, TransparentBitmapButton)
ON_MESSAGE(BM_SETCHECK, OnSetCheck)
ON_MESSAGE(BM_GETCHECK, OnGetCheck)
END_MESSAGE_MAP()
#pragma warning(pop)

LRESULT
CheckableBitmapButton::OnSetCheck(WPARAM wParam, LPARAM lParam)
{
	if (mIsCheckbox)
	{
		switch (wParam)
		{
		case BST_CHECKED:
			mChecked = TRUE;
			break;
		case BST_UNCHECKED:
		default:
			mChecked = FALSE;
			break;
		}
		return S_OK;
	}
	else
	{
		return Default();
	}
}

LRESULT
CheckableBitmapButton::OnGetCheck(WPARAM wParam, LPARAM lParam)
{
	if (mIsCheckbox)
	{
		if (mChecked)
			return BST_CHECKED;
		return BST_UNCHECKED;
	}
	else
	{
		return Default();
	}
}

void CheckableBitmapButton::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	if (!lpDIS)
		return;

	if (mTheming)
	{
		CRect rc(lpDIS->rcItem);
		if (rc.IsRectEmpty())
			return;
		DrawItemEx(CDC::FromHandle(lpDIS->hDC), rc);
		return;
	}

	if (!mIsCheckbox)
		return __super::DrawItem(lpDIS);

	// Adapted from CBitmapButton::DrawItem() impl

	CBitmap* pBitmap = &m_bitmap;
	UINT state = lpDIS->itemState;

	if ((state & ODS_SELECTED) && m_bitmapSel.m_hObject != NULL)
	{
		pBitmap = (!mChecked) ? &m_bitmapSel : &m_bitmap;
	}
	else if ((state & ODS_FOCUS) && m_bitmapFocus.m_hObject != NULL)
	{
		return __super::DrawItem(lpDIS);
	}
	else if ((state & ODS_DISABLED) && m_bitmapDisabled.m_hObject != NULL)
	{
		return __super::DrawItem(lpDIS);
	}
	else if (m_bitmapSel.m_hObject != NULL)
	{
		pBitmap = (mChecked) ? &m_bitmapSel : &m_bitmap;
	}

	// draw the whole button
	CDC* pDC = CDC::FromHandle(lpDIS->hDC);
	if (!pDC)
		return; // [case: 108623]

	CDC memDC;
	memDC.CreateCompatibleDC(pDC);
	CBitmap* pOld = memDC.SelectObject(pBitmap);
	if (pOld == NULL)
		return; // destructors will clean up

	CRect rect;
	rect.CopyRect(&lpDIS->rcItem);
	pDC->BitBlt(rect.left, rect.top, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOld);
}

void CheckableBitmapButton::DrawCommandbarBgEx(CDC* pDC, const CRect& rect, int count_id, bool draw_border,
                                               bool deflate_interior)
{
	_ASSERTE(mTheming != FALSE);
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (!mTheming)
		return;

	if (!HasThemeColors())
		UpdateThemeColors();

	// handle special case (-1 => flat background)

	if (count_id == -1)
	{
		COLORREF cr = mThemeColors[btn_bg0];
		pDC->FillSolidRect(rect, cr);
		return;
	}

	// handle correct gradient colors count ID

	size_t count = mThemeColors[count_id];

	if (count > 0)
	{
		std::vector<std::pair<double, COLORREF>> gradient(count);

		for (size_t i = 0; i < count; i++)
			gradient[i] = std::make_pair(double(i) / uint(count - 1), mThemeColors[count_id + btn_bg0 + i]);

		if (count_id == 4)
		{
			gradient[1].first = 0.5;
			gradient[2].first = 0.51;
		}

		CRect rc_gradient(rect);

		if (deflate_interior)
		{
			rc_gradient.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
			                        VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
		}

		::DrawVerticalVS2010Gradient(*pDC, rc_gradient, &gradient.front(), (int)gradient.size());
	}

	// draw the border of control

	if (draw_border)
	{
		COLORREF border = mThemeColors[count_id + btn_bg_border];

		if (border != CLR_NONE)
			ThemeUtils::FrameRectDPI(*pDC, rect, border);
	}
}

void CheckableBitmapButton::DrawItemEx(CDC* pDC, const CRect& rect)
{
	ASSERT(m_bitmap.m_hObject != NULL); // required
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	// draw button background

	CRect btn_rc(rect);
	btn_rc.left += VsUI::DpiHelper::LogicalToDeviceUnitsX(1); // space between buttons

	if (mState.IsMouseDown() || (GetState() & BST_PUSHED))
		DrawCommandbarButtonBg(pDC, btn_rc, btn_down_count);
	else if (mState.IsMouseOver() || (mState.IsFocused() && mState.IsFocusVisible()))
		DrawCommandbarButtonBg(pDC, btn_rc, mChecked ? btn_sel_over_count : btn_over_count);
	else if (mChecked)
		DrawCommandbarButtonBg(pDC, btn_rc, btn_sel_count);

	// draw button image

	CBitmap* pBitmap = &m_bitmap;

	if (mState.IsFocused() && mState.IsFocusVisible() && m_bitmapFocus.m_hObject)
		pBitmap = &m_bitmapFocus;
	else if (mState.IsInactive())
		pBitmap = &m_bitmapDisabled;
	else if (mState.IsMouseDown() || (GetState() & BST_PUSHED))
	{
		if (m_bitmapPushed.m_hObject)
			pBitmap = &m_bitmapPushed;
		else
			pBitmap = (!mChecked) ? &m_bitmapSel : &m_bitmap;
	}
	else if (m_bitmapSel.m_hObject)
		pBitmap = (mChecked) ? &m_bitmapSel : &m_bitmap;

	if (!pBitmap->m_hObject)
		pBitmap = &m_bitmap;

	if (pBitmap->m_hObject)
	{
		VsUI::GdiplusImage img;
		img.Attach(*pBitmap);

		Gdiplus::Graphics gr(*pDC);

		CRect img_rc;
		img_rc.right = img.GetWidth();
		img_rc.bottom = img.GetHeight();
		ThemeUtils::Rect_CenterAlign(&btn_rc, &img_rc);

		gr.DrawImage(img, (int)img_rc.left, (int)img_rc.top);	
	}
}

LRESULT CheckableBitmapButton::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	mState.OnMessage(message, wParam, lParam, this);

	if (message == WM_ERASEBKGND)
	{
		return 1;
	}
	else if (message == WM_PAINT)
	{
		CPaintDC dc(this);
		ThemeUtils::CMemDC memDC(&dc, false);

		CRect rc;
		GetClientRect(&rc);

		if (mTheming)
		{
			if (!mBarRect.IsRectNull())
			{
				// draw command bar gradient

				CRect bg_rect = mBarRect;
				bg_rect.left = rc.left;
				bg_rect.right = rc.right;
				DrawCommandbarBg(&memDC, bg_rect);
			}
			else
			{
				// draw the flat background

				DrawCommandbarBgEx(&memDC, rc, -1, false, false);
			}
			DrawItemEx(&memDC, rc);
		}
		else
		{
			DRAWITEMSTRUCT dis = {};

			if (GetState() & BST_FOCUS)
				dis.itemState = ODS_FOCUS;
			else if (!IsWindowEnabled())
				dis.itemState = ODS_DISABLED;
			else if (GetState() & BST_PUSHED)
				dis.itemState = ODS_SELECTED;
			else
				dis.itemState = ODS_DEFAULT;

			dis.CtlID = (uint)GetDlgCtrlID();
			dis.CtlType = ODT_BUTTON;
			dis.hDC = memDC;
			dis.itemAction = ODA_DRAWENTIRE;
			dis.hwndItem = m_hWnd;
			dis.rcItem = rc;
			dis.itemID = 0;

			__super::WindowProc(WM_ERASEBKGND, (WPARAM)dis.hDC, 0);

			DrawItem(&dis);
		}

		return 0;
	}

	return __super::WindowProc(message, wParam, lParam);
}

// static void
// AddBorder(HBITMAP bitmap, COLORREF color)
//{
//	if (!bitmap)
//		return;
//	CDC dc;
//	dc.CreateCompatibleDC(NULL);
//	BITMAP bm;
//	memset(&bm, 0, sizeof(bm));
//	::GetObject(bitmap, sizeof(bm), &bm);
//	if (!bm.bmWidth || !bm.bmHeight)
//		return;
//	dc.SelectObject(bitmap);
//	CRect rc(0, 0, bm.bmWidth, bm.bmHeight);
//	ThemeUtils::FrameRectDPI(dc, rc, color);
//}

// static void
// AddHighlightBorder(HBITMAP bitmap)
//{
//	AddBorder(bitmap, g_IdeSettings->GetColor(VSCOLOR_HIGHLIGHT));
//}

void CheckableBitmapButton::Substitute(CDialog* parent, CToolTipCtrl* tooltips,
                                       /* can be nullptr */ int placeholderDlgId, LPCSTR buttonTxt, int idImageRes,
                                       int idSelectedImageRes, int idFocusedImageRes, int idDisabledImageRes,
                                       bool isCheckbox /*= false*/, bool isFocusable /*= false*/)
{
	CRect rect;

	{
		CWnd* tmp = parent->GetDlgItem(placeholderDlgId);
		if (tmp)
		{
			tmp->GetWindowRect(rect);
			parent->ScreenToClient(rect);
			tmp->DestroyWindow();
		}
	}

	// #HD_IMAGES_DEV17_CHECK
	if (gShellAttr->IsDevenv11OrHigher() && !gShellAttr->IsDevenv17OrHigher())
	{
		// this is a bit fragile - dependent upon the v11 versions of the IDB_REFS*
		// image resource ids being paired up with the non-v11 versions in resource.h:
		// #define IDB_REFSFIND                    296
		// #define IDB_REFSFINDv11                 297
		++idImageRes;
		++idSelectedImageRes;
		_ASSERTE(!idFocusedImageRes);
		++idDisabledImageRes;
	}

	// remove possible overlapping of this button with other controls
	ThemeUtils::ForEachChild(parent->GetSafeHwnd(), [&](HWND sibling) -> bool {
		if (::GetDlgCtrlID(sibling) != placeholderDlgId)
		{
			CRect sibRc;
			if (::GetWindowRect(sibling, &sibRc))
			{
				parent->ScreenToClient(&sibRc);
				rect.SubtractRect(&rect, &sibRc);
			}
		}
		return true;
	});

	Create(buttonTxt, (isFocusable ? WS_TABSTOP : 0u) | WS_CHILD | WS_VISIBLE | BS_BITMAP | BS_OWNERDRAW, rect, parent,
	       (uint)placeholderDlgId);

	EnableCheckboxBehavior(isCheckbox);

	if (IsThemingSupported())
	{
		// enable theming
		mTheming = TRUE;
		UpdateThemeColors();
		idSelectedImageRes = 0;
		idFocusedImageRes = 0;
	}

	if (mTheming)
	{
		enum _bmps_
		{
			b_normal,
			b_sel,
			b_foc,
			b_dis,
			b_push,
			b_count
		};
		CBitmap bmps[b_count];

		///////////////////////////////////
		// read bitmaps from resources

		// #HD_IMAGES_DEV17_CHECK
#ifdef _WIN64
		if (gShellAttr->IsDevenv17OrHigher())
		{
			gImgListMgr->GetMonikerImageFromResourceId(bmps[b_normal], idImageRes, mThemeColors[btn_bg0], 0);

			if (mThemeColors[btn_bg0] != mThemeColors[btn_sel0])
				gImgListMgr->GetMonikerImageFromResourceId(bmps[b_sel], idImageRes, mThemeColors[btn_sel0], 0);

			if (mThemeColors[btn_bg0] != mThemeColors[btn_down0])
				gImgListMgr->GetMonikerImageFromResourceId(bmps[b_push], idImageRes, mThemeColors[btn_down0], 0);
					
			if (mThemeColors[btn_bg0] != mThemeColors[btn_over0])
				gImgListMgr->GetMonikerImageFromResourceId(bmps[b_foc], idImageRes, mThemeColors[btn_over0], 0);

			gImgListMgr->GetMonikerImageFromResourceId(bmps[b_dis], idImageRes, mThemeColors[btn_bg0], 0, true);
		}
		else
#endif
		    if (gShellAttr->IsDevenv11OrHigher())
		{
			bmps[b_normal].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idImageRes),
			                                           IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));

			if (idSelectedImageRes && idImageRes != idSelectedImageRes)
				bmps[b_sel].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idSelectedImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));

			if (idFocusedImageRes && idImageRes != idFocusedImageRes)
				bmps[b_foc].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idFocusedImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));

			if (idDisabledImageRes && idImageRes != idDisabledImageRes)
				bmps[b_dis].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idDisabledImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
		}
		else
		{
			bmps[b_normal].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idImageRes),
			                                           IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS | LR_CREATEDIBSECTION));

			if (idSelectedImageRes && idImageRes != idSelectedImageRes)
				bmps[b_sel].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idSelectedImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS | LR_CREATEDIBSECTION));

			if (idFocusedImageRes && idImageRes != idFocusedImageRes)
				bmps[b_foc].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idFocusedImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS | LR_CREATEDIBSECTION));

			if (idDisabledImageRes && idImageRes != idDisabledImageRes)
				bmps[b_dis].Attach((HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idDisabledImageRes),
				                                        IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS | LR_CREATEDIBSECTION));
		}

		// #HD_IMAGES_DEV17_CHECK
		if (!gShellAttr->IsDevenv17OrHigher())
		{
			//////////////////////
			// theme and scale

			if (gShellAttr->IsDevenv11OrHigher() && g_IdeSettings)
			{
				for (int i = 0; i < b_count; i++)
				{
					if (bmps[i].m_hObject)
					{
						ThemeUtils::ThemeBitmap(bmps[i], mThemeColors[btn_bg0]);
						ThemeUtils::LogicalToDeviceBitmap(*parent, bmps[i]);
					}
				}
			}
			else
			{
				for (int i = 0; i < b_count; i++)
				{
					if (bmps[i].m_hObject)
					{
						// make bitmaps bg transparent
						ThemeUtils::MakeBitmapTransparent(bmps[i]);
						ThemeUtils::LogicalToDeviceBitmap(*parent, bmps[i]);
					}
				}
			}
		}

		/////////////////////////////
		// attach them

		if (m_bitmap.m_hObject)
			m_bitmap.DeleteObject();
		m_bitmap.Attach(bmps[b_normal].Detach());

		if (m_bitmapSel.m_hObject)
			m_bitmapSel.DeleteObject();
		m_bitmapSel.Attach(bmps[b_sel].Detach());

		if (m_bitmapFocus.m_hObject)
			m_bitmapFocus.DeleteObject();
		m_bitmapFocus.Attach(bmps[b_foc].Detach());

		if (m_bitmapDisabled.m_hObject)
			m_bitmapDisabled.DeleteObject();
		m_bitmapDisabled.Attach(bmps[b_dis].Detach());

		if (m_bitmapPushed.m_hObject)
			m_bitmapPushed.DeleteObject();
		m_bitmapPushed.Attach(bmps[b_push].Detach());
	}
	else
	{
		///////////////////////////////
		// non-theming bitmaps (w/o DIB)

		LoadBitmap(idImageRes, idSelectedImageRes, idFocusedImageRes, idDisabledImageRes);
	}

	if (tooltips)
	{
		TOOLINFO ti;
		memset(&ti, 0, sizeof(ti));
		ti.cbSize = sizeof(ti);
		ti.hwnd = m_hWnd;
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.lpszText = const_cast<LPSTR>(buttonTxt);
		ti.uId = (UINT_PTR)m_hWnd;
		tooltips->SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}

	mState.AddHandler(this);
}

void CheckableBitmapButton::UpdateThemeColors()
{
	_ASSERTE(mTheming != FALSE);

	memset(mThemeColors, 0, sizeof(mThemeColors));

	if (g_IdeSettings && CVS2010Colours::IsExtendedThemeActive())
	{
		//////////////////////////
		// VS2012+ theme (dynamic)

		mThemeColors[btn_bg_border] = CLR_NONE;
		mThemeColors[btn_bg0] = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", FALSE);
		mThemeColors[btn_bg1] = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientMiddle", FALSE);
		mThemeColors[btn_bg2] = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientEnd", FALSE);
		mThemeColors[btn_bg_count] = 3;

		mThemeColors[btn_sel_border] = g_IdeSettings->GetEnvironmentColor(L"CommandBarSelectedBorder", FALSE);
		mThemeColors[btn_sel0] = g_IdeSettings->GetEnvironmentColor(L"CommandBarSelected", FALSE);
		mThemeColors[btn_sel_count] = 1;

		mThemeColors[btn_down_border] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBorder", FALSE);
		mThemeColors[btn_down0] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBackgroundBegin", FALSE);
		mThemeColors[btn_down1] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBackgroundMiddle", FALSE);
		mThemeColors[btn_down2] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBackgroundEnd", FALSE);
		mThemeColors[btn_down_count] = 3;

		mThemeColors[btn_over_border] = g_IdeSettings->GetEnvironmentColor(L"CommandBarBorder", FALSE);
		mThemeColors[btn_over0] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundBegin", FALSE);
		mThemeColors[btn_over1] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundMiddle1", FALSE);
		mThemeColors[btn_over2] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundMiddle2", FALSE);
		mThemeColors[btn_over3] = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundEnd", FALSE);
		mThemeColors[btn_over_count] = 4;

		mThemeColors[btn_sel_over_border] =
		    g_IdeSettings->GetEnvironmentColor(L"CommandBarHoverOverSelectedIconBorder", FALSE);
		mThemeColors[btn_sel_over0] = g_IdeSettings->GetEnvironmentColor(L"CommandBarHoverOverSelectedIcon", FALSE);
		mThemeColors[btn_sel_over_count] = 1;
	}
	else if (CVS2010Colours::IsVS2010CommandBarColouringActive())
	{
		/////////////////////////
		// VS2010 theme (dynamic)

		mThemeColors[btn_bg_border] = CLR_NONE;
		mThemeColors[btn_bg0] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);
		mThemeColors[btn_bg1] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_MIDDLE);
		mThemeColors[btn_bg2] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_END);
		mThemeColors[btn_bg_count] = 3;

		mThemeColors[btn_sel_border] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_SELECTED_BORDER);
		mThemeColors[btn_sel0] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_SELECTED);
		mThemeColors[btn_sel_count] = 1;

		mThemeColors[btn_down_border] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEDOWN_BORDER);
		mThemeColors[btn_down0] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEDOWN_BACKGROUND_BEGIN);
		mThemeColors[btn_down1] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEDOWN_BACKGROUND_MIDDLE);
		mThemeColors[btn_down2] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEDOWN_BACKGROUND_END);
		mThemeColors[btn_down_count] = 3;

		mThemeColors[btn_over_border] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_BORDER);
		mThemeColors[btn_over0] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_BEGIN);
		mThemeColors[btn_over1] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE1);
		mThemeColors[btn_over2] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE2);
		mThemeColors[btn_over3] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_END);
		mThemeColors[btn_over_count] = 4;

		mThemeColors[btn_sel_over_border] =
		    CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON_BORDER);
		mThemeColors[btn_sel_over0] = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON);
		mThemeColors[btn_sel_over_count] = 1;
	}
	else
	{
		ThemeUtils::CXTheme theme;

		auto getSysColor = [&theme](int index) {
			if (theme.IsAppThemed())
				return theme.GetSysColor(index);
			else
				return ::GetSysColor(index);
		};

		auto shiftColor = [](COLORREF rgb, int amount) {
			int r = __max(0, __min(255, (GetRValue(rgb) + amount)));
			int g = __max(0, __min(255, (GetGValue(rgb) + amount)));
			int b = __max(0, __min(255, (GetBValue(rgb) + amount)));
			return RGB(r, g, b);
		};

		mThemeColors[btn_bg_border] = CLR_NONE;
		mThemeColors[btn_bg0] = getSysColor(COLOR_BTNFACE);
		mThemeColors[btn_bg_count] = 1;

		COLORREF hl = getSysColor(COLOR_HIGHLIGHT);

		mThemeColors[btn_sel_border] = ThemeUtils::InterpolateColor(getSysColor(COLOR_ACTIVEBORDER), hl, 0.5f);
		mThemeColors[btn_sel0] = ThemeUtils::InterpolateColor(getSysColor(COLOR_BTNFACE), hl, 0.1f);
		mThemeColors[btn_sel_count] = 1;

		mThemeColors[btn_down_border] = hl;
		mThemeColors[btn_down0] = ThemeUtils::InterpolateColor(getSysColor(COLOR_BTNFACE), hl, 0.4f);
		mThemeColors[btn_down_count] = 1;

		mThemeColors[btn_over_border] = hl;
		mThemeColors[btn_over0] = ThemeUtils::InterpolateColor(getSysColor(COLOR_BTNFACE), hl, 0.2f);
		mThemeColors[btn_over_count] = 1;

		mThemeColors[btn_sel_over_border] = hl;
		mThemeColors[btn_sel_over0] = ThemeUtils::InterpolateColor(getSysColor(COLOR_BTNFACE), hl, 0.2f);
		mThemeColors[btn_sel_over_count] = 1;
	}
}

bool CheckableBitmapButton::GetThemeColor(color_id id, COLORREF& color)
{
	if (!HasThemeColors())
		UpdateThemeColors();

	if (HasThemeColors() && id < btn_num_colors)
	{
		color = mThemeColors[id];
		return true;
	}

	return false;
}

bool CheckableBitmapButton::IsThemingSupported()
{
	// this is just easy to disable theming in pre-VS2010 IDEs

	return true; // CVS2010Colours::IsVS2010ColouringActive() || CVS2010Colours::IsExtendedThemeActive();
}

void CheckableBitmapButton::DrawCommandbarBg(CDC* pDC, const CRect& rect)
{
	return DrawCommandbarBgEx(pDC, rect, btn_bg_count, false, false);
}

void CheckableBitmapButton::DrawCommandbarButtonBg(CDC* pDC, const CRect& rect, int count_id /*= btn_bg_count*/)
{
	if (count_id < 0 || count_id > btn_num_colors)
	{
		_ASSERTE(FALSE);
		return;
	}

	if (mChecked)
	{
		DrawCommandbarBgEx(pDC, rect, count_id, true, true);
		return;
	}

	COLORREF border_color = mThemeColors[count_id + btn_bg_border];

	bool border_equals_bg = true;

	// check if border equals all colors in background of control
	// if so, then this color should be considered as transparent
	// and thus the rectangle should not be drawn.

	int count = (int)mThemeColors[btn_bg_count];

	for (int i = 0; i < count; i++)
	{
		COLORREF gr_color = mThemeColors[btn_bg0 + i];
		if (gr_color != border_color)
		{
			border_equals_bg = false;
			break;
		}
	}

	DrawCommandbarBgEx(pDC, rect, count_id, !border_equals_bg, !border_equals_bg);
}
