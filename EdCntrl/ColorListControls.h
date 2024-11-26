#pragma once

#include "utils_goran.h"
#include <optional>
#include "CWndDpiAware.h"

class CVAThemeHelper
{
  public:
	CVAThemeHelper();
	virtual ~CVAThemeHelper();
	bool AreThemesAvailable() const;
};

class CVAMeasureItem 
{
	int text_height = 0;
	int icon_height = 0;
	float dpi_scale = 1;
	int font_settings_generation = -1;
	int dpi = 0;
	IDpiHandler* dpi_handler = nullptr;

  protected:
	void UpdateMetrics();

  public:
	CVAMeasureItem(IDpiHandler* handler) : dpi_handler(handler)
	{

	}

	bool InitTextMeasureDC(CDC& dc);
	CSize MeasureString(const CStringW& text);
	int MeasureItemHeight(int code = LVIR_BOUNDS, float paddingScale = 1.0f);
};

class CVS2010Colours : public CVAThemeHelper
{
  public:
	static const wchar_t* const VS2010_THEME_NAME;

	static bool IsVS2010ColouringActive();
	static bool IsVS2010AutoCompleteActive();
	static bool IsVS2010NavBarColouringActive(); // at the moment, this will also affect VAView combos!!
	static bool IsVS2010FindRefColouringActive();
	static bool IsVS2010CommandBarColouringActive();
	static bool IsVS2010VAOutlineColouringActive();
	static bool IsVS2010VAViewColouringActive();
	/*	static bool IsVS2010PaneBackgroundActive();*/ // handled in VaPkg/VaPaneBase.h/.cpp
	static COLORREF GetVS2010Colour(int index);
	static COLORREF GetThemeTreeColour(LPCWSTR itemname, BOOL foreground);
	static bool IsExtendedThemeActive();
};

class CColorVS2010TreeCtrl : public CWndDpiAware<CTreeCtrl>, protected CVS2010Colours
{
	DECLARE_DYNAMIC(CColorVS2010TreeCtrl)

  public:
	CColorVS2010TreeCtrl();
	virtual ~CColorVS2010TreeCtrl();

  protected:
	virtual void OnDpiChanged(DpiChange change, bool& handled);

	DECLARE_MESSAGE_MAP()
  public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0);

  protected:
	bool vs2010_active;

	virtual void PreSubclassWindow();
	virtual ULONG GetGestureStatus(CPoint ptTouch);
	void ActivateVS2010Style(bool force = false);
};

class CColorVS2010ListCtrl : public CWndDpiAware<CListCtrl>, protected CVS2010Colours
{
	DECLARE_DYNAMIC(CColorVS2010ListCtrl)

  public:
	CColorVS2010ListCtrl();
	virtual ~CColorVS2010ListCtrl();

  protected:
	virtual void OnDpiChanged(DpiChange change, bool& handled);

	DECLARE_MESSAGE_MAP()
  public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	DWORD SetExtendedStyle(DWORD dwNewStyle);

  protected:
	bool vs2010_active;
	bool enable_themes_beyond_vs2010;

	virtual void PreSubclassWindow();
	void ActivateVS2010Style(bool force = false);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

void GetVS2010SelectionColours(BOOL forMenu, const std::pair<double, COLORREF>*& gradients, int& gradients_count,
                               COLORREF& bordercolor);
void DrawVS2010Selection(CDC& dc, const CRect& rect, const std::pair<double, COLORREF>* gradients, int gradients_count,
                         COLORREF bordercolor, const std::function<void(CDC&, const CRect&)>& draw_background,
                         bool offset_rgn_by_windoworg = false);
void DrawVS2010MenuItemBackground(CDC& dc, const CRect& rect, bool do_clipping = true,
                                  const CRect* override_clientrect = NULL, int additional_border_width = 0,
                                  bool column_aware = false);
void DrawVerticalVS2010Gradient(CDC& dc, const CRect& rect, const std::pair<double, COLORREF>* gradients,
                                int gradients_count);

class CGradientCache;
void TreeVS2010CustomDraw(CTreeCtrl& tree, IDpiHandler* dpiHandler, NMHDR* nmhdr, LRESULT* result,
                          std::unique_ptr<CGradientCache>& background_gradient_cache,
                          const RECT* override_item_rect = nullptr, bool draw_text = true);

std::optional<LRESULT> ComboVS2010_CComboBoxEx_WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                       COLORREF bgcolour, CBrush& bgbrush_cache);
bool ComboVS2010_CComboBoxEx_OnDrawItem(CComboBoxEx* comboex, DRAWITEMSTRUCT* dis, COLORREF bgcolour,
                                        bool readonly_combo, CGradientCache& popup_background_gradient_cache);
#define GetVS2010ComboColour(name)                                                                                     \
	readonly_combo ? CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_##name)                                          \
	               : CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_##name)
std::optional<LRESULT> ComboVS2010_ComboPopup_WndProc(CWnd& wnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                      bool readonly_combo,
                                                      CGradientCache& popup_background_gradient_cache,
                                                      const std::function<LRESULT()>& default_WndProc);

COLORREF GetVS2010VANavBarBkColour();
void InitScrollWindowExPatch();
void UninitScrollWindowExPatch();

// used for painting Combo control embedded in ComboEx
struct IVS2010ComboInfo
{
	IVS2010ComboInfo() : readonly_combo(false)
	{
	}
	virtual ~IVS2010ComboInfo() = default;

	virtual COLORREF GetVS2010ComboBackgroundColour() const = 0;
	virtual bool ComboDrawsItsOwnBorder()
	    const = 0; // VAView combo has enough room, but VANavBar doesn't, so border is drawn on its parent.
	virtual bool IsListDropped() const
	{
		return false;
	} // possibility to override default behaviour
	virtual void OnHighlightStateChange(uint new_state) const
	{
	}
	virtual bool HasArrowButtonRightBorder() const
	{
		return true;
	}
	bool readonly_combo;
};

class ComboPaintSubClass : public CWnd
{
	DECLARE_DYNAMIC(ComboPaintSubClass)
  public:
	enum highlight
	{
		highlighted_none = 0,
		highlighted_mouse_inside = 1,
		highlighted_button_pressed = 2,
		highlighted_edit_focused = 4,

		highlighted_draw_highlighted = highlighted_mouse_inside | highlighted_button_pressed | highlighted_edit_focused
	};

  protected:
	static const int MOUSE_TIMER_ID = 1284;

  public:
	ComboPaintSubClass()
	{
		last_highlight_state = highlighted_none;
		mVs2010ColouringIsActive = CVS2010Colours::IsVS2010VAViewColouringActive();
	}

	void SetVS2010ColouringActive(bool act)
	{
		mVs2010ColouringIsActive = act;
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	LRESULT OnDrawMyBorder(WPARAM wparam, LPARAM lparam);

	void DoPaint(bool validate_instead_of_paint = false);
	CWnd* GetSplitterWnd();
	IVS2010ComboInfo* GetVS2010ComboInfo();
	highlight last_highlight_state;

	DECLARE_MESSAGE_MAP()
	virtual void PreSubclassWindow();

  public:
	afx_msg void OnTimer(UINT_PTR nIDEvent);

  private:
	bool mVs2010ColouringIsActive;
};

extern const UINT WM_UPDATE_MY_BORDER;

class CGradientCache
{
  public:
	CGradientCache() : bitmap_size(0, 0), gradient_size(0, 0), last_invalidate_gradient_cache(-1)
	{
	}

	void DrawVerticalVS2010Gradient(CDC& dc, const CRect& rect, const std::pair<double, COLORREF>* gradients,
	                                int gradients_count)
	{
		// won't use default parameter because it's used in std::bind
		DrawVerticalVS2010Gradient_clip(dc, rect, gradients, gradients_count, NULL);
	}

	void DrawVerticalVS2010Gradient_clip(CDC& dc, const CRect& rect, const std::pair<double, COLORREF>* gradients,
	                                     int gradients_count, const RECT* clip_rect);

	static void InvalidateAllGradientCaches()
	{
		::InterlockedIncrement(&invalidate_gradient_caches);
	}

	bool IsCacheValid() const
	{
		return last_invalidate_gradient_cache == invalidate_gradient_caches;
	}

  protected:
	CBitmap bitmap;
	CSize bitmap_size;
	CDC bitmap_dc;
	CSize gradient_size;
#ifdef _DEBUG
	std::vector<std::pair<double, COLORREF>> last_gradient_definitions;
#endif
	static volatile LONG invalidate_gradient_caches;
	LONG last_invalidate_gradient_cache;
};
