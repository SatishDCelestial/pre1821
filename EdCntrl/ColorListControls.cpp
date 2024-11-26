#include "stdafxed.h"
#include "ColorListControls.h"
#include "SyntaxColoring.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "VaService.h"
#include "FontSettings.h"
#include <vsshell80.h>
#include "vsshell100.h"
#include "MenuXP/Draw.h"
#include "SaveDC.h"
#include <windowsx.h>
#include "IdeSettings.h"
#include "ColorSyncManager.h"
#include "TextOutDC.h"
#include "VAThemeUtils.h"
#include "DebugStream.h"

#pragma comment(lib, "uxtheme.lib")
using namespace std::placeholders;
#define countof(x) (sizeof((x)) / sizeof((x)[0]))

#ifndef TVS_EX_DOUBLEBUFFER
#define TVS_EX_DOUBLEBUFFER 0x0004
#endif
#ifndef TVS_EX_FADEINOUTEXPANDOS
#define TVS_EX_FADEINOUTEXPANDOS 0x0040
#endif
#ifndef TVIS_EX_DISABLED
#define TVIS_EX_DISABLED 0x0002
#endif

volatile LONG CGradientCache::invalidate_gradient_caches = 0;
const wchar_t* const CVS2010Colours::VS2010_THEME_NAME = L"Explorer";
extern const uint WM_VA_ONSCROLLWINDOWEX = ::RegisterWindowMessage("WM_VA_ONSCROLLWINDOWEX");
#if defined(RAD_STUDIO)
static constexpr bool kRadStudioTheming = true;
#endif

CVAThemeHelper::CVAThemeHelper()
{
}

CVAThemeHelper::~CVAThemeHelper()
{
}

#pragma warning(push)
#pragma warning(disable : 4996)
bool CVAThemeHelper::AreThemesAvailable() const
{
	static bool first = true;
	static bool ret = false;
	if (first)
	{
		first = false;
#if 1
		OSVERSIONINFOEXA osvi;
		memset(&osvi, 0, sizeof(osvi));
		osvi.dwOSVersionInfoSize = sizeof(osvi);
		::GetVersionExA((OSVERSIONINFOA*)&osvi);
		ret = (osvi.dwMajorVersion >= 6) &&
		      ((osvi.wProductType != VER_NT_SERVER) ||
		       gShellAttr->IsDevenv11OrHigher() || 
				  gShellAttr->IsCppBuilder()); // allow Vista and 7, but not Server 2003/2008 on pre-VS2012
#else
		// requires Win8.1 SDK include dirs
		// #include <VersionHelpers.h>
		if (gShellAttr->IsDevenv11OrHigher())
			ret = true; // always allow vs2012+
		else if (!::IsWindowsServer())
			ret = ::IsWindowsVistaOrGreater(); // allow Vista and 7, but not Server 2003/2008
#endif
	}
	return ret;
}
#pragma warning(pop)

// VS themes used for views and listboxes starting in dev11
bool CVS2010Colours::IsExtendedThemeActive()
{
#if defined(RAD_STUDIO)
	return kRadStudioTheming;
#else
	static bool first = true;
	static bool ret = false;
	if (first && gShellAttr && Psettings)
	{
		ret = CVS2010Colours::IsVS2010ColouringActive() && gShellAttr->IsDevenv11OrHigher() &&
		      Psettings->mUseNewTheme11Plus;
		first = false;
	}
	return ret;
#endif
}

bool CVS2010Colours::IsVS2010ColouringActive()
{
#if defined(RAD_STUDIO)
	return kRadStudioTheming;
#else
	static bool first = true;
	static bool ret = false;
	if (first && gShellAttr)
	{
		ret = gShellAttr->IsDevenv10OrHigher();
		first = false;
	}
	return ret;
#endif
}

bool CVS2010Colours::IsVS2010AutoCompleteActive()
{
	return IsExtendedThemeActive();
}

bool CVS2010Colours::IsVS2010NavBarColouringActive()
{
	return IsVS2010ColouringActive();
}

bool CVS2010Colours::IsVS2010CommandBarColouringActive()
{
	return IsVS2010ColouringActive();
}

bool CVS2010Colours::IsVS2010FindRefColouringActive()
{
	return IsExtendedThemeActive();
}

bool CVS2010Colours::IsVS2010VAOutlineColouringActive()
{
	return IsExtendedThemeActive();
}

bool CVS2010Colours::IsVS2010VAViewColouringActive()
{
	return IsExtendedThemeActive();
}

// bool CVS2010Colours::IsVS2010PaneBackgroundActive()
// {
// 	return IsVS2010ColouringActive();
// }

COLORREF CVS2010Colours::GetVS2010Colour(int index)
{
	if (!IsVS2010ColouringActive())
		return CLR_INVALID;

	// enable for testing the feature in VS9
	// 	switch(index)
	// 	{
	// 	case VSCOLOR_COMMANDBAR_SELECTED_BORDER:
	// 		return RGB(229, 195, 101);
	// 	case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_BEGIN:
	// 		return RGB(255, 252, 244);
	// 	case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE1:
	// 		return RGB(255, 243, 205);
	// 	case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_MIDDLE2:
	// 	case VSCOLOR_COMMANDBAR_MOUSEOVER_BACKGROUND_END:
	// 		return RGB(255, 236, 181);
	// 	case VSCOLOR_COMMANDBAR_MENU_ICONBACKGROUND:
	// 		return RGB(233, 236, 238);
	// 	case VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN:
	// 		return RGB(233, 236, 238);
	// 	case VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTEND:
	// 		return RGB(208, 215, 226);
	// 	case VSCOLOR_COMMANDBAR_MENU_SEPARATOR:
	// 		return RGB(190, 195, 203);
	// 	case VSCOLOR_COMMANDBAR_TEXT_ACTIVE:
	// 		return RGB(27, 41, 62);
	// 	case VSCOLOR_COMMANDBAR_TEXT_SELECTED:
	// 		return RGB(0, 0, 0);
	// 	case VSCOLOR_COMMANDBAR_TEXT_INACTIVE:
	// 		return RGB(128, 128, 128);
	// 	case VSCOLOR_COMMANDBAR_MENU_BORDER:
	// 		return RGB(155, 167, 183);
	// 	case VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON:
	// 		return RGB(255, 252, 244);
	// 	case VSCOLOR_COMMANDBAR_SELECTED:
	// 		return RGB(255, 239, 187);
	// 	case VSCOLOR_COMMANDBAR_HOVEROVERSELECTEDICON_BORDER:
	// 		return RGB(229, 195, 101);
	// 	default:
	// 		assert(!"CVS2010ColorEnum::GetVS2010Colour");
	// 		return RGB(0, 255, 0);
	// 	}

	if (g_IdeSettings)
		return g_IdeSettings->GetColor(index);
	return CLR_INVALID;
}

COLORREF CVS2010Colours::GetThemeTreeColour(LPCWSTR itemname, BOOL foreground)
{
	if (!IsExtendedThemeActive())
		return CLR_INVALID;

	if (g_IdeSettings)
		return g_IdeSettings->GetThemeTreeColor(itemname, foreground);
	return CLR_INVALID;
}

/////////////////
// CColorTreeCtrl
/////////////////

IMPLEMENT_DYNAMIC(CColorVS2010TreeCtrl, CTreeCtrl)

CColorVS2010TreeCtrl::CColorVS2010TreeCtrl()
{
	vs2010_active = false;
}

CColorVS2010TreeCtrl::~CColorVS2010TreeCtrl()
{
}

void CColorVS2010TreeCtrl::OnDpiChanged(DpiChange change, bool& handled)
{
	// case: 142249 set indent depending on new DPI
	if (change == CDpiAware::DpiChange::BeforeParent)
	{
		auto helper = GetDpiHelper();
		if (helper)
		{
			SetIndent((UINT)helper->LogicalToDeviceUnitsX(16) + 3);
		}
	}

	__super::OnDpiChanged(change, handled);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CColorVS2010TreeCtrl, CTreeCtrl)
ON_WM_CREATE()
END_MESSAGE_MAP()
#pragma warning(pop)

int CColorVS2010TreeCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (__super::OnCreate(lpCreateStruct) == -1)
		return -1;

	ActivateVS2010Style(true);

	return 0;
}

void CColorVS2010TreeCtrl::PreSubclassWindow()
{
	__super::PreSubclassWindow();

	ActivateVS2010Style(true);
}

ULONG CColorVS2010TreeCtrl::GetGestureStatus(CPoint ptTouch)
{
	// [case: 111020]
	// https://support.microsoft.com/en-us/help/2846829/how-to-enable-tablet-press-and-hold-gesture-in-mfc-application
	// https://connect.microsoft.com/VisualStudio/feedback/details/699523/tablet-pc-right-click-action-cannot-invoke-mfc-popup-menu
	return 0;
}

BOOL CColorVS2010TreeCtrl::ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags)
{
	ActivateVS2010Style();

	if (vs2010_active)
	{
		dwAdd &= ~(TVS_HASLINES /*| TVS_LINESATROOT*/);
		dwRemove |= TVS_HASLINES /*| TVS_LINESATROOT*/;
	}
	return __super::ModifyStyle(dwRemove, dwAdd, nFlags);
}

#ifndef TVM_SETEXTENDEDSTYLE
#define TVM_SETEXTENDEDSTYLE 4396
#endif

void CColorVS2010TreeCtrl::ActivateVS2010Style(bool force)
{
	if (!IsVS2010ColouringActive())
		return;
	if (vs2010_active && !force)
		return; // already activated

	if (AreThemesAvailable())
	{
		if (FAILED(SetWindowTheme(m_hWnd, VS2010_THEME_NAME, NULL)))
			return;
		__super::ModifyStyle(TVS_HASLINES | TVS_LINESATROOT | TVS_TRACKSELECT, 0);
		SendMessage(TVM_SETEXTENDEDSTYLE, TVS_EX_FADEINOUTEXPANDOS | TVS_EX_DOUBLEBUFFER,
		            TVS_EX_FADEINOUTEXPANDOS | TVS_EX_DOUBLEBUFFER);
		vs2010_active = true;
	}
}

/////////////////
// CColorListCtrl
/////////////////

IMPLEMENT_DYNAMIC(CColorVS2010ListCtrl, CListCtrl)

CColorVS2010ListCtrl::CColorVS2010ListCtrl()
{
	vs2010_active = false;
	enable_themes_beyond_vs2010 = false;
}

CColorVS2010ListCtrl::~CColorVS2010ListCtrl()
{
}

void CColorVS2010ListCtrl::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);

	// case: 142226 apply DPI scale to columns
	if (change == CDpiAware::DpiChange::AfterParent)
	{
		auto header = GetHeaderCtrl();
		if (header)
		{
			const size_t kMaxColumns = (size_t)header->GetItemCount();

			double scale = GetDpiChangeScaleFactor();

			std::vector<int> column_widths;
			column_widths.resize(kMaxColumns);

			for (size_t x = 0; x < kMaxColumns; ++x)
			{
				const int width = GetColumnWidth((int)x);
				if (width > 0 && width < 4000)
					column_widths[x] = WindowScaler::Scale(width, scale);
				else
					column_widths[x] = 0;
			}

			for (size_t x = 0; x < kMaxColumns; ++x)
				if (column_widths[x])
					SetColumnWidth((int)x, column_widths[x]);
		}
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CColorVS2010ListCtrl, CListCtrl)
ON_WM_CREATE()
END_MESSAGE_MAP()
#pragma warning(pop)

int CColorVS2010ListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (__super::OnCreate(lpCreateStruct) == -1)
		return -1;

	ActivateVS2010Style(true);

	return 0;
}

void CColorVS2010ListCtrl::PreSubclassWindow()
{
	CListCtrl::PreSubclassWindow();

	ActivateVS2010Style(true);
}

void CColorVS2010ListCtrl::ActivateVS2010Style(bool force)
{
	if (!enable_themes_beyond_vs2010)
	{
		if (!IsVS2010ColouringActive())
			return;
	}
	if (vs2010_active && !force)
		return; // already activated

	if (AreThemesAvailable())
	{
		if (FAILED(SetWindowTheme(m_hWnd, VS2010_THEME_NAME, NULL)))
			return;
		__super::SetExtendedStyle(LVS_EX_DOUBLEBUFFER | (GetExtendedStyle() & ~LVS_EX_GRIDLINES));
		vs2010_active = true;
	}
}

DWORD CColorVS2010ListCtrl::SetExtendedStyle(DWORD dwNewStyle)
{
	ActivateVS2010Style();

	if (vs2010_active)
	{
		dwNewStyle |= LVS_EX_DOUBLEBUFFER;
		dwNewStyle &= ~LVS_EX_GRIDLINES;
	}

	return __super::SetExtendedStyle(dwNewStyle);
}

LRESULT CColorVS2010ListCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<VAColorPaintMessages> vacpm;

	if (vs2010_active)
		vacpm = std::make_unique<VAColorPaintMessages>(
		    m_hWnd, message,
		    (Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorListboxes) ? PaintType::ListBox
		                                                                           : PaintType::DontColor);

	return __super::WindowProc(message, wParam, lParam);
}

CStringW TreeGetItemTextW(HWND hWnd, HTREEITEM hItem)
{
	ASSERT(::IsWindow(hWnd));
	TVITEMW item;
	item.hItem = hItem;
	item.mask = TVIF_TEXT;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		item.pszText = str.GetBufferSetLength(nLen);
		item.cchTextMax = nLen;
		::SendMessageW(hWnd, TVM_GETITEMW, 0, (LPARAM)&item);
		nRes = item.pszText ? (int)::wcslen(item.pszText) : 0;
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

void TreeVS2010CustomDraw(CTreeCtrl& tree, IDpiHandler* dpiHandler, NMHDR* nmhdr, LRESULT* result,
                          std::unique_ptr<CGradientCache>& background_gradient_cache, const RECT* override_item_rect,
                          bool draw_text)
{
	VsUI::CDpiHelper* dpiHelper = dpiHandler ? dpiHandler->GetDpiHelper() : VsUI::DpiHelper::GetForWindow(tree.m_hWnd, false);
	auto dpiScope = VsUI::DpiHelper::SetDefaultDirect(dpiHelper);

	InitScrollWindowExPatch();
	mySetProp(tree.m_hWnd, "__VA_do_not_scrollwindowex", (HANDLE)1);

	NMTVCUSTOMDRAW* nmtvcd = (NMTVCUSTOMDRAW*)nmhdr;
	if (nmtvcd->nmcd.dwDrawStage & CDDS_PREPAINT)
	{
		const bool kHasFocus = CWnd::GetFocus()->GetSafeHwnd() == tree.GetSafeHwnd();
		bool draw_item = !!(nmtvcd->nmcd.dwDrawStage & CDDS_ITEM);
		*result = draw_item ? CDRF_SKIPDEFAULT : CDRF_NOTIFYITEMDRAW;

		static std::pair<double, COLORREF> back_gradients[2] = {std::make_pair(0., COLORREF(0)),
		                                                        std::make_pair(1., COLORREF(0))};

		COLORREF avg_bg_colour = 0;

		if (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder())
		{
			back_gradients[0].second = back_gradients[1].second =
			    g_IdeSettings->GetThemeTreeColor(L"Background", false);
			avg_bg_colour = back_gradients[0].second;
		}
		else
		{
			back_gradients[0].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN);
			back_gradients[1].second = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_END);
			avg_bg_colour = ThemeUtils::InterpolateColor(back_gradients[0].second, back_gradients[1].second, 0.5f);
		}

		TextOutDc dc;
		dc.Attach(nmtvcd->nmcd.hdc);
		int savedc = dc.SaveDC();

		CRect backrect;
		tree.GetClientRect(backrect);
		CPoint windoworg(0, 0);
		::GetWindowOrgEx(dc.m_hDC, &windoworg); // gmit: don't use MFC here since GetWindowOrgEx sometimes fails (?!)
		if (windoworg.y)
		{
			::SetWindowOrgEx(dc.m_hDC, windoworg.x, 0, NULL);
			::OffsetRect(&nmtvcd->nmcd.rc, 0, -windoworg.y);
			backrect.OffsetRect(0, -windoworg.y);
		}
		int scrollx = tree.GetScrollPos(SB_HORZ);

		do
		{
			// draw background
			if (!background_gradient_cache || !background_gradient_cache->IsCacheValid())
				background_gradient_cache = std::make_unique<CGradientCache>();

			if (!draw_item)
			{
				background_gradient_cache->DrawVerticalVS2010Gradient_clip(dc, backrect, back_gradients,
				                                                           countof(back_gradients), &nmtvcd->nmcd.rc);
				break;
			}

			// get all info about item
			HTREEITEM hitem = (HTREEITEM)nmtvcd->nmcd.dwItemSpec;
			if (!hitem)
				break;
			IMAGEINFO ii;
			memset(&ii, 0, sizeof(ii));
			CImageList* il = tree.GetImageList(TVSIL_NORMAL);
			if (il && il->GetImageCount())
				il->GetImageInfo(0, &ii);

			CRect itemrect = override_item_rect ? *override_item_rect : nmtvcd->nmcd.rc;

			if (itemrect.IsRectEmpty() ||
			    itemrect.top > backrect.bottom ||
			    itemrect.bottom < backrect.top)
			{
				break;
			}

			if (scrollx > 0)
				itemrect.left -= scrollx;
			CStringW text = TreeGetItemTextW(tree.GetSafeHwnd(), hitem);
			CFont* font = tree.GetFont();
			UINT indent = tree.GetIndent(); // [case: 142249] do not scale

			bool enabled = true;
			{
				TVITEMEX ex_item = {};
				ex_item.hItem = hitem;
				ex_item.mask = TVIF_STATEEX;
				if (::SendMessageW(tree.m_hWnd, TVM_GETITEMW, 0, (LPARAM)&ex_item))
					enabled = (ex_item.uStateEx & TVIS_EX_DISABLED) == 0;
			}

			if (!font)
				break;
			dc.SelectObject(&font);
			CSize textsize(0, 0);
			if (!override_item_rect)
				::GetTextExtentPoint32W(dc.m_hDC, text, text.GetLength(), &textsize);

			const int left_spacing = dpiHelper->LogicalToDeviceUnitsX(3);
			const int image_spacing = dpiHelper->LogicalToDeviceUnitsX(3);
			const int right_spacing = dpiHelper->LogicalToDeviceUnitsX(4);

			int checkboxLength = (textsize.cy * 2) / 3;
			if (!CVS2010Colours::IsExtendedThemeActive() || checkboxLength < dpiHelper->LogicalToDeviceUnitsX(12))
			{
				checkboxLength = dpiHelper->LogicalToDeviceUnitsX(12);
			}

			const auto tree_style = tree.GetStyle();
			const int checkbox_spacing = (tree_style & TVS_CHECKBOXES) ? checkboxLength : 0;
			int level = nmtvcd->iLevel;
			if ((tree_style & TVS_LINESATROOT) && (tree_style & (TVS_HASBUTTONS | TVS_HASLINES)))
				level++;

			CRect selectionRect;
			if (tree_style & TVS_FULLROWSELECT)
			{
				// when fullRowSelect is enabled, save rect before itemrect is
				// shortened in the next block
				if (override_item_rect)
				{
					selectionRect = nmtvcd->nmcd.rc;
					// slight copy of logic above
					if (selectionRect.IsRectEmpty())
						selectionRect = itemrect;
					else if (scrollx > 0)
						selectionRect.left -= scrollx;
				}
				else
					selectionRect = itemrect;
			}

			if (override_item_rect)
			{
				itemrect.left -= left_spacing + checkbox_spacing + image_spacing + ii.rcImage.right - ii.rcImage.left;
				itemrect.right += right_spacing;
			}
			else
			{
				itemrect.left += indent * level;
				itemrect.right = itemrect.left + checkbox_spacing + left_spacing + image_spacing + right_spacing +
				                 ii.rcImage.right - ii.rcImage.left + textsize.cx;
			}

			if (!(tree_style & TVS_FULLROWSELECT))
			{
				// use shortened itemrect when fullRowSelect is disabled
				selectionRect = itemrect;
			}

			TVITEM item;
			memset(&item, 0, sizeof(item));
			item.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE;
			item.hItem = hitem;
			tree.GetItem(&item);
			const bool selected = !!(item.state & TVIS_SELECTED);
			const bool hot = !!(nmtvcd->nmcd.uItemState & CDIS_HOT);

			// draw background
			if (selected || (hot && (gShellAttr->IsDevenv10() || !CVS2010Colours::IsExtendedThemeActive())))
			{
				_ASSERTE((!CVS2010Colours::IsExtendedThemeActive() && (selected || hot)) ||
				         (CVS2010Colours::IsExtendedThemeActive() && selected));
				static std::pair<double, COLORREF> selection_gradients[4] = {
				    std::make_pair(0., COLORREF(0)), std::make_pair(.49, COLORREF(0)), std::make_pair(.5, COLORREF(0)),
				    std::make_pair(1., COLORREF(0))};
				COLORREF border_colour;

				if (CVS2010Colours::IsExtendedThemeActive())
				{
					COLORREF clr;

					if (kHasFocus)
					{
						clr = CVS2010Colours::GetThemeTreeColour(L"SelectedItemActive", FALSE);
						if (CLR_INVALID == clr)
							clr = CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHT);
					}
					else
					{
						clr = CVS2010Colours::GetThemeTreeColour(L"SelectedItemInactive", FALSE);
						if (CLR_INVALID == clr)
							clr = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_BEGIN);
					}

					selection_gradients[0].second = clr;
					selection_gradients[1].second = clr;
					selection_gradients[2].second = clr;
					selection_gradients[3].second = clr;
					border_colour = clr;
					if (kHasFocus && gShellAttr && gShellAttr->IsDevenv17OrHigher())
						border_colour = CVS2010Colours::GetThemeTreeColour(L"FocusVisualBorder", false);
					avg_bg_colour = clr;
				}
				else
				{
					selection_gradients[0].second =
					    CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_BEGIN);
					selection_gradients[1].second =
					    CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE1);
					selection_gradients[2].second =
					    CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_MIDDLE2);
					selection_gradients[3].second =
					    CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BACKGROUND_END);
					border_colour = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_MOUSEOVER_BORDER);
					avg_bg_colour = ThemeUtils::InterpolateColor(selection_gradients[1].second,
					                                             selection_gradients[2].second, 0.5f);
				}

				if (selected)
				{
					DrawVS2010Selection(
					    dc, selectionRect, selection_gradients, countof(selection_gradients), border_colour,
					    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, background_gradient_cache.get(), _1,
					              backrect, back_gradients, (int)countof(back_gradients)) /*, true*/);
				}
				else if (hot)
				{
					static std::pair<double, COLORREF> highlight_gradients[countof(selection_gradients)];
					for (int i = 0; i < countof(highlight_gradients); i++)
					{
						highlight_gradients[i].first = selection_gradients[i].first;
						highlight_gradients[i].second = HLS_TRANSFORM(selection_gradients[i].second, 60, 30);
					}

					const COLORREF highlight_border_colour = HLS_TRANSFORM(border_colour, 60, 30);
					DrawVS2010Selection(
					    dc, selectionRect, highlight_gradients, countof(highlight_gradients), highlight_border_colour,
					    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, background_gradient_cache.get(), _1,
					              backrect, back_gradients, (int)countof(back_gradients)) /*, true*/);
				}
			}

			// draw buttons
			CPen buttonpen;
			CBrush buttonbrush;
			if (level > 0)
			{
				CRect buttonrect(LONG(itemrect.left - indent), itemrect.top, itemrect.left, itemrect.bottom);

				if (item.state & TVIS_EXPANDED && tree.ItemHasChildren(hitem))
				{
					COLORREF bordercolor = CLR_INVALID;
					COLORREF fillcolor = CLR_INVALID;
					if (CVS2010Colours::IsExtendedThemeActive())
					{
						if (selected && kHasFocus)
						{
							bordercolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemActiveGlyphMouseOver" : L"SelectedItemActiveGlyph", FALSE);
							fillcolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemActive" : L"SelectedItemActiveGlyph", FALSE);
						}
						else if (selected && !kHasFocus)
							fillcolor = bordercolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemInactiveGlyphMouseOver" : L"SelectedItemInactiveGlyph", FALSE);
						else
							fillcolor = bordercolor =
							    CVS2010Colours::GetThemeTreeColour(hot ? L"GlyphMouseOver" : L"Glyph", FALSE);
					}

					if ((bordercolor == CLR_INVALID) || (fillcolor == CLR_INVALID))
					{
						bordercolor = RGB(38, 38, 38);
						fillcolor = RGB(89, 89, 89);
					}

					// render arrow using GDI+
					ThemeUtils::DrawTreeExpandGlyph(dc, buttonrect, bordercolor, fillcolor, true);
				}
				else if (tree.ItemHasChildren(hitem))
				{
					COLORREF bordercolor = CLR_INVALID;
					COLORREF fillcolor = CLR_INVALID;

					if (CVS2010Colours::IsExtendedThemeActive())
					{
						if (selected && kHasFocus)
						{
							bordercolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemActiveGlyphMouseOver" : L"SelectedItemActiveGlyph", FALSE);
							fillcolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemActiveGlyphMouseOver" : L"SelectedItemActive", FALSE);
						}
						else if (selected && !kHasFocus)
						{
							bordercolor = CVS2010Colours::GetThemeTreeColour(
							    hot ? L"SelectedItemInactiveGlyphMouseOver" : L"SelectedItemInactiveGlyph", FALSE);
							fillcolor = CVS2010Colours::GetThemeTreeColour(L"SelectedItemInactive", FALSE);
						}
						else
						{
							bordercolor = CVS2010Colours::GetThemeTreeColour(hot ? L"GlyphMouseOver" : L"Glyph", FALSE);
							fillcolor = CVS2010Colours::GetThemeTreeColour(L"Background", FALSE);
						}
					}

					if ((bordercolor == CLR_INVALID) || (fillcolor == CLR_INVALID))
					{
						bordercolor = RGB(0, 0, 0);
						fillcolor = RGB(255, 255, 255);
					}

					// render arrow using GDI+
					ThemeUtils::DrawTreeExpandGlyph(dc, buttonrect, bordercolor, fillcolor, false);
				}
			}

			if (checkbox_spacing)
			{
				// draw checkbox
				static CVAThemeHelper th;
				do
				{
					int top_offset = (itemrect.Height() - checkboxLength) / 2;
					CRect chkboxRect(CPoint(itemrect.left + left_spacing, itemrect.top + top_offset),
					                 CSize(checkboxLength, checkboxLength));

					if (CVS2010Colours::IsExtendedThemeActive())
					{
						COLORREF crBackImg;
						if (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder())
							crBackImg = g_IdeSettings->GetThemeTreeColor(L"Background", false);
						else
							crBackImg =
							    CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BACKGROUND_GRADIENTBEGIN);

						COLORREF crActive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE);
						COLORREF crInactive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_INACTIVE);
						COLORREF crBox =
						    RGB(GetRValue(crInactive) + ((GetRValue(crActive) - GetRValue(crInactive)) / 2),
						        GetGValue(crInactive) + ((GetGValue(crActive) - GetGValue(crInactive)) / 2),
						        GetBValue(crInactive) + ((GetBValue(crActive) - GetBValue(crInactive)) / 2));
						if (0 == crBox && crActive)
							crBox = crActive;
						else if (crBox > 0xffffff)
							crBox = crActive;

						if (!enabled)
						{
							crBox = ThemeUtils::InterpolateColor(crBackImg, crBox, 0.5);
							crActive = ThemeUtils::InterpolateColor(crBackImg, crActive, 0.5);
						}

						ThemeUtils::Symbol symb =
						    enabled ? (tree.GetCheck(hitem) ? ThemeUtils::Symb_Check : ThemeUtils::Symb_None)
						            : ThemeUtils::Symb_None;
						ThemeUtils::DrawCheckBoxEx(dc, chkboxRect, symb, crBox, crBackImg, crActive);
					}
					else if (th.AreThemesAvailable())
					{
						HTHEME theme = ::OpenThemeData(tree.m_hWnd, L"BUTTON");
						if (theme)
						{
							::DrawThemeBackground(theme, dc, BP_CHECKBOX,
							                      tree.GetCheck(hitem) ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL,
							                      chkboxRect, nullptr);
							::CloseThemeData(theme);
							break;
						}
					}
					else
						dc.DrawFrameControl(chkboxRect, DFC_BUTTON,
						                    DFCS_BUTTONCHECK | DFCS_FLAT | (tree.GetCheck(hitem) ? DFCS_CHECKED : 0u));
				} while (false);
			}

			// draw icon
			CRect iconrect(CPoint(itemrect.left + left_spacing + checkbox_spacing, itemrect.top),
			               CSize(ii.rcImage.right - ii.rcImage.left, itemrect.Height()));

			CPoint iconPt(iconrect.left, iconrect.CenterPoint().y - (ii.rcImage.bottom - ii.rcImage.top) / 2);

			int image = (selected && (item.iSelectedImage > 0)) ? item.iSelectedImage : item.iImage;
			if (il && (image > 0) && 30 /* ICONIDX_BLANK */ != image)
			{
				if (enabled)
				{
#ifdef _WIN64
					if (selected && kHasFocus && !(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()))
					{
						bool image_resolved = false;
						auto pMon = ImageListManager::GetMoniker(image, true, true);
						if (pMon)
						{
							CBitmap bmp;
							if (SUCCEEDED(ImageListManager::GetMonikerImage(bmp, *pMon, avg_bg_colour, 0)))
							{
								image_resolved = ThemeUtils::DrawImage(dc, bmp, iconPt);
							}
						}

						if (!image_resolved)
						{
							ThemeUtils::DrawImageThemedForBackground(dc, il, image, iconPt, avg_bg_colour);
						}
					}
					else
#endif
					{
						il->Draw(&dc, image, iconPt, ILD_TRANSPARENT);
					}
				}
				else
				{
					ThemeUtils::DrawImageGray(dc, il, image, iconPt, 0.5f);
				}
			}

			// draw text
			if (draw_text)
			{
				bool overridePaintType = false;
				COLORREF textColor = 0;

				if (CVS2010Colours::IsExtendedThemeActive())
				{
					if (selected && Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorViews)
					{
						// [case: 65047] don't color selected items in dev11
						overridePaintType = true;
					}

					if (selected && kHasFocus && !(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()))
						textColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHTTEXT);
					else
						textColor = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE);
				}
				else
					textColor = RGB(0, 0, 0);

				if (enabled)
					dc.SetTextColor(textColor);
				else
				{
					dc.SetTextColor(ThemeUtils::InterpolateColor(avg_bg_colour, textColor, 0.5));
					overridePaintType = true;
				}

				dc.SetBkMode(TRANSPARENT);
				CRect textrect(iconrect.right + image_spacing, itemrect.top,
				               iconrect.right + image_spacing + textsize.cx + right_spacing, itemrect.bottom);
				TempPaintOverride t(overridePaintType);
				dc.DrawTextW(text, textrect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		} while (false);

		dc.RestoreDC(savedc);
		dc.Detach();
	}
}

std::optional<LRESULT> ComboVS2010_CComboBoxEx_WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                       COLORREF bgcolour, CBrush& bgbrush_cache)
{
	switch (message)
	{
	case WM_ERASEBKGND:
		if (CVS2010Colours::IsExtendedThemeActive() && wParam)
			return 1;
		return std::nullopt;
	case WM_CTLCOLOR:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX: {
		::SetBkColor((HDC)wParam, bgcolour);
		if (!bgbrush_cache.m_hObject)
			bgbrush_cache.CreateSolidBrush(bgcolour);
		if (CVS2010Colours::IsExtendedThemeActive())
		{
			// gmit: note this would probably work for VS2010 too (but noone complained yet)
			::SetTextColor((HDC)wParam, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
		}
		return (LRESULT)(HBRUSH)bgbrush_cache.m_hObject;
	}
	break;
	}
	return std::nullopt;
}

const int kHorizBorder = 4; // VsUI::DpiHelper::LogicalToDeviceUnitsX(4);
const int kVertBorder = 1;  // VsUI::DpiHelper::LogicalToDeviceUnitsY(1);

bool ComboVS2010_CComboBoxEx_OnDrawItem(CComboBoxEx* comboex, DRAWITEMSTRUCT* dis, COLORREF bgcolour,
                                        bool readonly_combo, CGradientCache& popup_background_gradient_cache)
{
	if (!comboex->GetSafeHwnd())
		return false;

	if (dis->CtlType != ODT_COMBOBOX)
		return false;

	if (dis->itemState & ODS_COMBOBOXEDIT)
	{
		// draw item in edit control
		CBrush br;
		if (br.CreateSolidBrush(bgcolour))
			::FillRect(dis->hDC, &dis->rcItem, (HBRUSH)br.m_hObject);
		dis->itemAction |= ODA_DRAWENTIRE;
		dis->itemAction &= ~ODA_FOCUS;
		return false; // do original drawing
	}
	else
	{
		// this block executes when the list is dropped.
		// it seems that we only draw icons in dropped list, not when list is closed.
		// [wer] event id -1876336553, -1878036524 crash in comboex->GetComboBoxCtrl()
		HWND hBox = (HWND)::SendMessage(comboex->GetSafeHwnd(), CBEM_GETCOMBOCONTROL, 0, 0);
		if (!hBox)
			return false;

		CComboBox* boxCtrl = (CComboBox*)CComboBox::FromHandle(hBox);
		if (!boxCtrl->GetSafeHwnd())
			return false;

		// draw combo's listbox popup
		TextOutDc dc;
		if (!dc.Attach(dis->hDC))
			return false;

		int savedc = dc.SaveDC();

		const bool selected = dis->itemState & ODS_SELECTED;
		COMBOBOXINFO cbi;
		memset(&cbi, 0, sizeof(cbi));
		cbi.cbSize = sizeof(cbi);
		boxCtrl->GetComboBoxInfo(&cbi);
		CRect rect;
		::GetClientRect(cbi.hwndList, rect);
		CRect itemrect = dis->rcItem;
		CRgn itemrgn;
		if (!itemrgn.CreateRectRgnIndirect(&itemrect))
			return false;
		// gmit: this is a complete mystery to me why is this needed (discovered by accident), but, it seems that dc is
		// offset by -1,-1 pixel while drawing each item for the first time (!!)
		CPoint windoworg(0, 0);
		::GetWindowOrgEx(dc.m_hDC, &windoworg); // gmit: don't use MFC here since GetWindowOrgEx sometimes fails (?!)
		itemrgn.OffsetRgn(-windoworg);
		dc.SelectObject(&itemrgn);

		InitScrollWindowExPatch();
		mySetProp(cbi.hwndList, "__VA_do_not_scrollwindowex", (HANDLE)1);

		const std::pair<double, COLORREF> back_gradients[] = {
		    std::make_pair(0, GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN)),
		    std::make_pair(1, GetVS2010ComboColour(POPUP_BACKGROUND_END))
		    // 			std::make_pair(0, RGB(255, 0, 0)),
		    // 			std::make_pair(1, RGB(0, 255, 0))
		};

		bool overridePaintType = false;
		if (selected)
		{
			if (Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorWizardBar &&
			    (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
			{
				// [case: 65047] don't color selected items in dev11
				overridePaintType = true;
			}

			const std::pair<double, COLORREF> selection_gradients[] = {
			    std::make_pair(0, GetVS2010ComboColour(MOUSEOVER_BACKGROUND_BEGIN)),
			    std::make_pair(.49, GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE1)),
			    std::make_pair(.5, GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE2)),
			    std::make_pair(1, GetVS2010ComboColour(MOUSEOVER_BACKGROUND_END))};
			const COLORREF border_colour = GetVS2010ComboColour(MOUSEOVER_BORDER);

			DrawVS2010Selection(dc, itemrect, selection_gradients, countof(selection_gradients), border_colour,
			                    std::bind(&CGradientCache::DrawVerticalVS2010Gradient, &popup_background_gradient_cache,
			                              _1, rect, back_gradients, (int)countof(back_gradients)),
			                    true);
		}
		else
			popup_background_gradient_cache.DrawVerticalVS2010Gradient(dc, rect, back_gradients,
			                                                           (int)countof(back_gradients));

		if (-1 != dis->itemID)
		{
			itemrect.DeflateRect(kHorizBorder, kVertBorder, kHorizBorder, kVertBorder);

			WCHAR itemtext[256] = {0};

			CImageList* il = comboex->GetImageList();
			COMBOBOXEXITEMW cbexi;
			memset(&cbexi, 0, sizeof(cbexi));
			cbexi.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
			cbexi.iItem = (INT_PTR)(UINT_PTR)dis->itemID;
			cbexi.pszText = (LPWSTR)(LPCWSTR)itemtext;
			cbexi.cchTextMax = 256;
			::SendMessage(*comboex, CBEM_GETITEMW, 0, (LPARAM)&cbexi);

			if (il)
			{
				const int image = selected && cbexi.iSelectedImage != -1 ? cbexi.iSelectedImage : cbexi.iImage;
				IMAGEINFO ii;
				memset(&ii, 0, sizeof(ii));
				if ((image != I_IMAGECALLBACK) && (image != I_IMAGENONE))
				{
					il->GetImageInfo(image, &ii);
					il->Draw(&dc, image,
					         CPoint(itemrect.left,
					                itemrect.top + (itemrect.Height() - (ii.rcImage.bottom - ii.rcImage.top)) / 2),
					         UINT(CVS2010Colours::IsExtendedThemeActive() ? ILD_IMAGE : ILD_TRANSPARENT));
				}
				else
					il->GetImageInfo(0, &ii);
				itemrect.left += ii.rcImage.right - ii.rcImage.left + kHorizBorder;
			}

			CFont* font = comboex->GetFont();
			if (font && font->m_hObject)
				dc.SelectObject(font);
			if (selected)
				dc.SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_HOVER));
			else
				dc.SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
			dc.SetBkMode(TRANSPARENT);
			TempPaintOverride t(overridePaintType);
			dc.DrawTextW(itemtext, itemrect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}

		dc.RestoreDC(savedc);
		dc.Detach();
		return true;
	}
}

std::optional<LRESULT> ComboVS2010_ComboPopup_WndProc(CWnd& wnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                      bool readonly_combo,
                                                      CGradientCache& popup_background_gradient_cache,
                                                      const std::function<LRESULT()>& default_WndProc)
{
	LRESULT ret;
	if (message == WM_ERASEBKGND)
	{
		if (CVS2010Colours::IsExtendedThemeActive() && wparam)
		{
			// this prevents flash of COLOR_WINDOW (white) when list is first dropped
			// but presence causes flicker when typing filter (MIF list handles
			// WM_ERASEBKGND independently to prevent flicker)
			HDC hdc = (HDC)wparam;
			CRect rect;
			GetClientRect(wnd.m_hWnd, &rect);
			CBrush br;
			if (br.CreateSolidBrush(GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN)))
				::FillRect(hdc, rect, br);
			return 1;
		}
		else
			ret = 1;
	}
	else
		ret = default_WndProc();

	switch (message)
	{
	case WM_NCPAINT:
	case WM_PAINT:
	case WM_ERASEBKGND:
	case WM_NCACTIVATE:
	case WM_PRINT:
	case WM_PRINTCLIENT:
		CWindowDC dc(&wnd);
		CSaveDC savedc(dc);

		const std::pair<double, COLORREF> back_gradients[] = {
		    std::make_pair(0, GetVS2010ComboColour(POPUP_BACKGROUND_BEGIN)),
		    std::make_pair(1, GetVS2010ComboColour(POPUP_BACKGROUND_END))
		    // 			std::make_pair(0, RGB(255, 0, 0)),
		    // 			std::make_pair(1, RGB(0, 255, 0))
		};

		// calculate client rect in non-client coordinates
		CRect crect;
		wnd.GetClientRect(crect);
		wnd.ClientToScreen(crect);
		CRect wrect;
		wnd.GetWindowRect(wrect);
		const CPoint offset(crect.left - wrect.left, crect.top - wrect.top);

		// draw border
		CRect brect = wrect;
		brect.MoveToXY(0, 0);
		CRgn brgn;
		if (brgn.CreateRectRgnIndirect(brect))
			dc.SelectObject(&brgn);
		dc.SetBkMode(TRANSPARENT);
		::SelectObject(dc.m_hDC, ::GetStockObject(HOLLOW_BRUSH));
		const COLORREF popup_border_colour = GetVS2010ComboColour(POPUP_BORDER);
		CPen pen;
		if (pen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, popup_border_colour))
		{
			dc.SelectObject(&pen);
			dc.Rectangle(brect);
		}

		// draw background parts outside items
		CRect rect(offset, crect.Size());
		CRgn rgn;
		if (rgn.CreateRectRgnIndirect(rect))
		{
			for (int i = ListBox_GetTopIndex(wnd.m_hWnd), cnt = ListBox_GetCount(wnd.m_hWnd); i < cnt; i++)
			{
				CRect itemrect;
				ListBox_GetItemRect(wnd.m_hWnd, i, &itemrect);
				CRect itemrect_w = itemrect;
				wnd.ClientToScreen(itemrect_w);
				if (itemrect_w.top >= crect.bottom)
					break;
				itemrect.OffsetRect(offset); // client to non-client coords
				CRgn itemrgn;
				if (itemrgn.CreateRectRgnIndirect(itemrect))
					rgn.CombineRgn(&rgn, &itemrgn, RGN_DIFF);
			}
			dc.SelectObject(&rgn);
		}

		popup_background_gradient_cache.DrawVerticalVS2010Gradient(dc, rect, back_gradients, countof(back_gradients));
	}

	return ret;
}

#pragma warning(push)
#pragma warning(disable : 4191)
extern const UINT WM_DRAW_MY_BORDER = ::RegisterWindowMessage("WM_DRAW_MY_BORDER");
IMPLEMENT_DYNAMIC(ComboPaintSubClass, CWnd)
BEGIN_MESSAGE_MAP(ComboPaintSubClass, CWnd)
ON_REGISTERED_MESSAGE(WM_DRAW_MY_BORDER, OnDrawMyBorder)
ON_WM_TIMER()
END_MESSAGE_MAP()
#pragma warning(pop)

LRESULT ComboPaintSubClass::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	bool draw_arrow = false;
	std::optional<LRESULT> ret;
	if (mVs2010ColouringIsActive)
	{
		switch (message)
		{
			//		default:
			// 			if(message < WM_MOUSEMOVE || message > WM_MOUSELAST)
			// 				break;
			// 			// fall through
		case WM_ERASEBKGND:
			if (CVS2010Colours::IsExtendedThemeActive())
				return 1;
			// fall through
		case WM_NCPAINT:
		case WM_PAINT:
		case WM_NCACTIVATE:
		case WM_PRINT:
		case WM_PRINTCLIENT:
			draw_arrow = true;
			break;
		case WM_DESTROY:
			if (::IsWindow(m_hWnd)) // ?? I had an assert here!
				::KillTimer(m_hWnd, MOUSE_TIMER_ID);
			break;
		}
	}

	if (draw_arrow)
	{
		DoPaint(true); // gmit: important to do so to prevent occasional flicker on mouse move!!
		if (!ret)
			ret = CWnd::WindowProc(message, wParam, lParam);
		DoPaint();
	}
	else if (!ret)
		ret = CWnd::WindowProc(message, wParam, lParam);

	return *ret;
}

// [case: 85333]
// enable USE_MEMDC to fix flicker of context and def drop buttons (test
// by doing new horizontal group, ensure va nav bar is set for both top and bottom
// groups, place caret on a class method, then use vertical scrollbar thumb
// to go up/down crazy).
// however, USE_MEMDC breaks display of context and def icons.  Tried to create
// clipping region for the icon to no avail.  The icon in the context and def
// windows does not appear to be drawn by us (?) - we only draw the icons in
// the drop list.
// #define USE_MEMDC

// VA View FIS / SIS paint
// va nav bar / minihelp paint
void ComboPaintSubClass::DoPaint(bool validate_instead_of_paint)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	CRect clientrect;
	GetClientRect(clientrect);

	COMBOBOXINFO cbi;
	memset(&cbi, 0, sizeof(cbi));
	cbi.cbSize = sizeof(cbi);
	if (!::GetComboBoxInfo(m_hWnd, &cbi))
		return;

	CRgn rgn;
	if (!rgn.CreateRectRgnIndirect(clientrect))
		return;

	CRgn editrgn;
	if (!editrgn.CreateRectRgnIndirect(&cbi.rcItem))
		return;

	CRgn buttonrgn;
	if (!buttonrgn.CreateRectRgnIndirect(&cbi.rcButton))
		return;

#ifdef USE_MEMDC
	CRect iconRc(cbi.rcItem);
	iconRc.DeflateRect(kHorizBorder, kVertBorder, kHorizBorder, kVertBorder);
	iconRc.right = iconRc.left + 16;
	iconRc.bottom = iconRc.top + 16;

	CRgn iconrgn;
	if (!iconrgn.CreateRectRgnIndirect(&iconRc))
		return;
#endif // USE_MEMDC

	rgn.CombineRgn(&rgn, &editrgn, RGN_DIFF);
	rgn.CombineRgn(&rgn, &buttonrgn, RGN_DIFF);
#ifdef USE_MEMDC
	rgn.CombineRgn(&rgn, &iconrgn, RGN_DIFF);
#endif // USE_MEMDC

	if (validate_instead_of_paint)
	{
		ValidateRect(&cbi.rcButton);
		ValidateRgn(&rgn);
		return;
	}

	if (gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder())
		OnTimer(MOUSE_TIMER_ID); // update last_highlight_state

		// draw borders (draw icon area and area around EDIT in combobox when both dropped and not dropped)
#ifdef USE_MEMDC
	CClientDC dcOrig(this);
	ThemeUtils::CMemDC dc(&dcOrig);
#else
	CClientDC dc(this);
#endif

	IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
	COLORREF comboBgCol;
	if ((gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder()) && g_IdeSettings && 
		comboinfo->ComboDrawsItsOwnBorder() && last_highlight_state & highlighted_draw_highlighted)
	{
		// [case: 74847] in vs2013 dark theme, the whole combo+edit should change bg color on mouseover
		// this is a start but incomplete, so revert to incorrect coloring.
		// comboBgCol = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseOverBackgroundBegin", FALSE);
		comboBgCol = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();
	}
	else
		comboBgCol = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();

	CBrush br;
	if (br.CreateSolidBrush(comboBgCol))
	{
		dc.FillRgn(&rgn, &br);
		ValidateRgn(&rgn);
	}

#ifdef USE_MEMDC
	if (CVS2010Colours::IsVS2010NavBarColouringActive() && CVS2010Colours::IsExtendedThemeActive())
	{
		CBrush br;
		if (br.CreateSolidBrush(GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour()))
			dc.FillRect(&clientrect, &br);
	}
#endif // USE_MEMDC

	if (comboinfo->ComboDrawsItsOwnBorder())
	{
		COLORREF borderColor;
		if (last_highlight_state & highlighted_draw_highlighted)
		{
			if ((gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder()) && g_IdeSettings)
			{
				// [case: 74727]
				borderColor = g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", FALSE);
			}
			else
				borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER);
		}
		else
			borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);

		CPen borderpen;
		if (borderpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, borderColor))
		{
			dc.SelectObject(&borderpen);
			dc.SelectObject(CBrush::FromHandle((HBRUSH)::GetStockObject(HOLLOW_BRUSH)));
			dc.Rectangle(&clientrect);
		}
	}

	// prepare drop button
	if (!gShellAttr->IsDevenv12OrHigher() && !gShellAttr->IsCppBuilder())
		OnTimer(MOUSE_TIMER_ID); // update last_highlight_state
	CRect buttonrect(cbi.rcButton);
	CPen buttonborderpen;
	COLORREF buttonBorderColor;
	if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
		buttonBorderColor = last_highlight_state ? g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", false)
		                                         : g_IdeSettings->GetEnvironmentColor(L"DropDownBorder", false);
	else
		buttonBorderColor = last_highlight_state ? GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER)
		                                         : GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);
	if (buttonborderpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, buttonBorderColor))
	{
		dc.SelectObject(&buttonborderpen);

		// draw button borders
		if (GetVS2010ComboInfo()->HasArrowButtonRightBorder())
		{
			dc.MoveTo(buttonrect.right - 1, buttonrect.top);
			dc.LineTo(buttonrect.right - 1, buttonrect.bottom);
			--buttonrect.right;
		}
		if (last_highlight_state)
		{ // any highlighted state has left border
			dc.MoveTo(buttonrect.left, buttonrect.top);
			dc.LineTo(buttonrect.left, buttonrect.bottom);
			++buttonrect.left;
		}
	}

	// draw button background
	CBrush buttonbrush;
	COLORREF buttonBgColor = CLR_INVALID;
	if (last_highlight_state & highlighted_button_pressed)
	{
		if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
			buttonBgColor = g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseDownBackground", false);
		else
			buttonBgColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEDOWN_BACKGROUND);
	}
	else if (last_highlight_state & (highlighted_mouse_inside | highlighted_edit_focused))
	{
		if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
		{
			buttonBgColor = g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseOverBackground", false);
		}
		else
		{
			const std::pair<double, COLORREF> gradients[] = {
			    std::make_pair(0, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_BEGIN)),
			    std::make_pair(0.49, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE1)),
			    std::make_pair(0.5, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_MIDDLE2)),
			    std::make_pair(1, GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BACKGROUND_END)),
			};
			DrawVerticalVS2010Gradient(dc, buttonrect, gradients, countof(gradients));
		}
	}
	else
	{
		buttonBgColor = GetVS2010ComboInfo()->GetVS2010ComboBackgroundColour();
	}

	if (CLR_INVALID != buttonBgColor && buttonbrush.CreateSolidBrush(buttonBgColor))
		dc.FillRect(buttonrect, &buttonbrush);

	// draw button arrow
	CPen arrowpen;
	COLORREF arrow_colour = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_GLYPH);
	if (gShellAttr && (gShellAttr->IsDevenv11OrHigher() || gShellAttr->IsCppBuilder()))
	{
		if (last_highlight_state & highlighted_button_pressed)
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseDownGlyph", false);
		else if (last_highlight_state & (highlighted_mouse_inside | highlighted_edit_focused))
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseOverGlyph", false);
		else
			arrow_colour = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuSubmenuGlyph", false);
	}

	if (arrowpen.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, arrow_colour))
	{
		dc.SelectObject(&arrowpen);
		CPoint arrowcenter = buttonrect.CenterPoint();
		const int arrowsize = VsUI::DpiHelper::LogicalToDeviceUnitsX(3);
		for (int i = 0; i < arrowsize; i++)
		{
			dc.MoveTo(arrowcenter.x - i, arrowcenter.y - i + arrowsize / 2);
			dc.LineTo(arrowcenter.x + i + 2, arrowcenter.y - i + arrowsize / 2);
		}
	}

	ValidateRgn(&buttonrgn);
}

// minihelp / va nav bar border paint
LRESULT ComboPaintSubClass::OnDrawMyBorder(WPARAM wparam, LPARAM lparam)
{
	if (!mVs2010ColouringIsActive)
		return 0;

	CDC& dc = *(CDC*)wparam;
	CRect rect = *(const CRect*)lparam;

	CSaveDC savedc(dc);
	CPen pen_inner, pen_outer;
	if (!pen_outer.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, GetVS2010VANavBarBkColour()))
		return 0;

	COLORREF borderColor;
	if (last_highlight_state)
	{
		if ((gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder()) && g_IdeSettings)
		{
			// [case: 74727]
			borderColor = g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", FALSE);
		}
		else
			borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(MOUSEOVER_BORDER);
	}
	else
		borderColor = GetVS2010ComboInfo()->GetVS2010ComboColour(BORDER);
	if (!pen_inner.CreatePen(PS_SOLID, 0 /*VsUI::DpiHelper::LogicalToDeviceUnitsX(1)*/, borderColor))
		return 0;

	dc.SelectObject(&pen_outer);
	dc.SelectObject(::GetStockObject(HOLLOW_BRUSH));
	dc.Rectangle(rect);
	dc.SelectObject(&pen_inner);
	// 	rect.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsY(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
	// 		VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
	rect.DeflateRect(1, 1, 1, 1);
	dc.Rectangle(rect);
	return 1;
}

CWnd* ComboPaintSubClass::GetSplitterWnd()
{
	CWnd* comboex = GetParent();
	if (!comboex)
		return NULL;
	CWnd* pane = comboex->GetParent();
	if (!pane)
		return NULL;
	return pane->GetParent();
}

IVS2010ComboInfo* ComboPaintSubClass::GetVS2010ComboInfo()
{
	CWnd* parent = GetParent();
	_ASSERTE(parent && dynamic_cast<IVS2010ComboInfo*>(parent));
	return parent ? dynamic_cast<IVS2010ComboInfo*>(parent) : NULL;
}

void ComboPaintSubClass::PreSubclassWindow()
{
	CWnd::PreSubclassWindow();

	if (mVs2010ColouringIsActive)
	{
		SetTimer(MOUSE_TIMER_ID, 50, NULL);

		// gmit: this is necessary for an initial update
		IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
		if (!comboinfo->ComboDrawsItsOwnBorder())
		{
			CWnd* splitter = GetSplitterWnd();
			if (splitter)
				splitter->PostMessage(WM_UPDATE_MY_BORDER, (WPARAM)m_hWnd);
		}
	}
}

void ComboPaintSubClass::OnTimer(UINT_PTR nIDEvent)
{
	if ((nIDEvent == MOUSE_TIMER_ID) && mVs2010ColouringIsActive)
	{
		if (!::IsWindow(m_hWnd))
			return; // [case: 110696]
		auto p = GetParent();
		if (!p)
			return; // [case: 110696]
		CComboBoxEx* comboex = dynamic_cast<CComboBoxEx*>(p);
		if (!comboex)
			return;
		CPoint mouse((LPARAM)::GetMessagePos());
		CRect rect;
		comboex->GetWindowRect(rect);
		BOOL inside = rect.PtInRect(mouse);
		if (inside)
		{
			HWND wndMouse = ::WindowFromPoint(mouse);
			inside = wndMouse == comboex->m_hWnd || ::IsChild(comboex->m_hWnd, wndMouse);
		}
		IVS2010ComboInfo* comboinfo = GetVS2010ComboInfo();
		bool dropped = !!SendMessage(CB_GETDROPPEDSTATE);
		dropped |= comboinfo->IsListDropped();
		CEdit* edit = comboex->GetEditCtrl();
		bool focused = edit && (::GetFocus() == edit->m_hWnd);

		uint new_highlight_state =
		    (last_highlight_state & ~highlighted_mouse_inside) | (inside ? highlighted_mouse_inside : 0u);
		new_highlight_state =
		    (new_highlight_state & ~highlighted_button_pressed) | (dropped ? highlighted_button_pressed : 0u);
		new_highlight_state =
		    (new_highlight_state & ~highlighted_edit_focused) | (focused ? highlighted_edit_focused : 0u);
		if (!!new_highlight_state != !!last_highlight_state)
		{
			comboinfo->OnHighlightStateChange((uint)new_highlight_state);
			if (comboinfo->ComboDrawsItsOwnBorder())
				InvalidateRect(NULL, false);
			else
			{
				CWnd* splitter = GetSplitterWnd();
				if (splitter)
					splitter->PostMessage(WM_UPDATE_MY_BORDER, (WPARAM)m_hWnd);
			}
		}
		last_highlight_state = (highlight)new_highlight_state;
		return;
	}

	CWnd::OnTimer(nIDEvent);
}

void CGradientCache::DrawVerticalVS2010Gradient_clip(CDC& dc, const CRect& rect,
                                                     const std::pair<double, COLORREF>* gradients, int gradients_count,
                                                     const RECT* clip_rect)
{
	if (rect.IsRectEmpty())
		return;

	CRect rect2 = rect;
	if (clip_rect)
		rect2 &= *clip_rect;

	switch (gradients_count)
	{
	case 3:
		if (gradients[0].second != gradients[2].second)
			break;
		[[fallthrough]];
	case 2:
		if (gradients[0].second != gradients[1].second)
			break;
		[[fallthrough]];
	case 1:
		dc.FillSolidRect(&rect2, gradients[0].second);
		return;
	case 0:
		return;
	}

	LONG current_invalidate_gradient_caches = invalidate_gradient_caches;
	if (!bitmap.m_hObject || (bitmap_size.cx < rect.Width()) || (bitmap_size.cy < rect.Height()) ||
	    (current_invalidate_gradient_caches != last_invalidate_gradient_cache))
	{
		bitmap_dc.DeleteDC();
		bitmap.DeleteObject();
		CSize new_bitmap_size = CSize(std::max<int>(bitmap_size.cx, rect.Width()) * 5 / 4,
		                              std::max<int>(bitmap_size.cy, rect.Height()) * 5 / 4);
		bitmap.CreateCompatibleBitmap(&dc, new_bitmap_size.cx, new_bitmap_size.cy);
		if (!bitmap.m_hObject)
			return;
		bitmap_size = new_bitmap_size;
		bitmap_dc.CreateCompatibleDC(&dc);
		bitmap_dc.SelectObject(&bitmap);
		gradient_size.SetSize(0, 0);
		last_invalidate_gradient_cache = current_invalidate_gradient_caches;
	}
	if (gradient_size != rect.Size())
	{
		gradient_size = rect.Size();
		::DrawVerticalVS2010Gradient(bitmap_dc, CRect(CPoint(0, 0), gradient_size), gradients, gradients_count);
#ifdef _DEBUG
		last_gradient_definitions.clear();
		last_gradient_definitions.insert(last_gradient_definitions.end(), gradients, gradients + gradients_count);
#endif
	}
	// gmit: we'll assume gradients/gradients_count are the same for each call; if they are not, there's no point in
	// caching...
#ifdef _DEBUG
	_ASSERTE(gradients_count == (int)last_gradient_definitions.size());
// 	_ASSERTE(std::equal(last_gradient_definitions.begin(), last_gradient_definitions.end(), gradients));
#endif

	dc.BitBlt(rect2.left, rect2.top, rect2.Width(), rect2.Height(), &bitmap_dc, rect2.left - rect.left,
	          rect2.top - rect.top, SRCCOPY);
}

bool CVAMeasureItem::InitTextMeasureDC(CDC& dc)
{
	if (dpi_handler && dpi_handler->UpdateFonts(VAFTF_All))
	{
		CClientDC clientDC(dpi_handler->GetDpiWindow());
		if (dc.CreateCompatibleDC(&clientDC))
		{
			dc.SelectObject(dpi_handler->GetDpiAwareFont());
			return true;
		}	
	}

	return false;
}

CSize CVAMeasureItem::MeasureString(const CStringW& text)
{
	CSize txtSize;
	CDC dc;
	if (InitTextMeasureDC(dc))
	{
		GetTextExtentPoint32W(dc, text, text.GetLength(), &txtSize);
	}
	return txtSize;
}

int CVAMeasureItem::MeasureItemHeight(int code /*= LVIR_BOUNDS*/, float paddingScale /*= 1*/)
{
	UpdateMetrics();

	switch (code)
	{
	case LVIR_LABEL:
		paddingScale *= dpi_scale;
		return text_height + (paddingScale ? WindowScaler::Scale(2, paddingScale) : 0);
	case LVIR_ICON:
		return icon_height;
	default:
		if (paddingScale)
		{
			paddingScale *= dpi_scale;
			return std::max<LONG>(text_height + WindowScaler::Scale(2, paddingScale), icon_height) + WindowScaler::Scale(4, paddingScale);
		}

		return std::max<LONG>(text_height, icon_height);
	}
}

void CVAMeasureItem::UpdateMetrics()
{
	if (!dpi_handler)
		return;

	auto helper = dpi_handler->GetDpiHelper();
	auto scope = VsUI::DpiHelper::SetDefaultDirect(helper);

	icon_height = VsUI::DpiHelper::GetSystemMetrics(SM_CYSMICON);
	dpi_scale = (float)VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorY();

	if (g_FontSettings && dpi_handler->GetFontType() != VaFontType::None)
	{
		if (font_settings_generation != g_FontSettings->GetSettingsGeneration() || dpi != helper->GetDeviceDpiX())
		{
			dpi = helper->GetDeviceDpiX();
			dpi_handler->UpdateFonts(VAFTF_All, (uint)dpi);

			CClientDC clientDC(dpi_handler->GetDpiWindow());

			CFontDC fontDC(clientDC, dpi_handler->GetDpiAwareFont());
			TEXTMETRIC tm;
			memset(&tm, 0, sizeof(tm));
			clientDC.GetTextMetrics(&tm);

			text_height = tm.tmHeight;
			font_settings_generation = g_FontSettings->GetSettingsGeneration();
		}
	}
	else
	{
		text_height = icon_height;
	}
}
