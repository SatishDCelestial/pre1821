#ifndef VAThemeUtils_h__
#define VAThemeUtils_h__

#pragma once
#include "SyntaxColoring.h"

#include "VA_Gdiplus.h"

#include <functional>
#include <afxmt.h>

// DEFINE_VA_MESSAGE(WM_VA_APPLYTHEME);
const UINT WM_VA_APPLYTHEME = ::RegisterWindowMessageA("WM_VA_APPLYTHEME");
const UINT WM_VA_THEMEDTYPE = ::RegisterWindowMessageA("WM_VA_THEMEDTYPE");
const UINT WM_VA_GETTHEMECONTEXT = ::RegisterWindowMessageA("WM_VA_GETTHEMECONTEXT");
const UINT WM_VA_EDITSELECTALL = ::RegisterWindowMessageA("WM_VA_EDITSELECTALL");
const UINT WM_VA_DLGENTEREXITSIZEMOVE = ::RegisterWindowMessageA("WM_VA_NOTIFYSIZEMOVE");

namespace ThemeCategory11
{
static const GUID Autos = {0xA7EE6BEE, 0xD0AA, 0x4B2F, {0xAD, 0x9D, 0x74, 0x82, 0x76, 0xA7, 0x25, 0xF6}};
static const GUID Cider = {0x92D153EE, 0x57D7, 0x431F, {0xA7, 0x39, 0x09, 0x31, 0xCA, 0x3F, 0x7F, 0x70}};
static const GUID CommandWindow = {0xEE1BE240, 0x4E81, 0x4BEB, {0x8E, 0xEA, 0x54, 0x32, 0x2B, 0x6B, 0x1B, 0xF5}};
static const GUID Diagnostics = {0x6F4C1845, 0x5111, 0x4F31, {0xB2, 0x04, 0x47, 0xCB, 0x6A, 0x46, 0x6E, 0xE8}};
static const GUID EditorTooltip = {0xA9A5637F, 0xB2A8, 0x422E, {0x8F, 0xB5, 0xDF, 0xB4, 0x62, 0x5F, 0x01, 0x11}};
static const GUID Environment = {0x624ED9C3, 0xBDFD, 0x41FA, {0x96, 0xC3, 0x7C, 0x82, 0x4E, 0xA3, 0x2E, 0x3D}};
static const GUID Find = {0x4370282E, 0x987E, 0x4AC4, {0xAD, 0x14, 0x5F, 0xFE, 0xD2, 0xAD, 0x1E, 0x14}};
static const GUID FindResults = {0x5C48B2CB, 0x0366, 0x4FBF, {0x97, 0x86, 0x0B, 0xB3, 0x7E, 0x94, 0x56, 0x87}};
static const GUID FolderDifference = {0xB36B0228, 0xDBAD, 0x4DB0, {0xB9, 0xC7, 0x2A, 0xD3, 0xE5, 0x72, 0x01, 0x0F}};
static const GUID FSharpInteractive = {0x00CCEE86, 0x3140, 0x4E06, {0xA6, 0x5A, 0xA9, 0x26, 0x65, 0xA4, 0x0D, 0x6F}};
static const GUID GraphDocumentColors = {0x0CD5AA2B, 0xEF23, 0x4997, {0x80, 0xB5, 0x7D, 0x0E, 0x8F, 0xE5, 0xB3, 0x12}};
static const GUID GraphicsDebugger = {0x40CDC500, 0x10AC, 0x41DF, {0xA5, 0x33, 0x6A, 0xF2, 0xAA, 0xAA, 0x0C, 0x4B}};
static const GUID GraphicsDesigners = {0x9CFDB8B3, 0x48AA, 0x4715, {0xAC, 0x38, 0xDD, 0xE2, 0x96, 0x8D, 0x20, 0x4F}};
static const GUID Header = {0x4997F547, 0x1379, 0x456E, {0xB9, 0x85, 0x2F, 0x41, 0x3C, 0xDF, 0xA5, 0x36}};
static const GUID ImmediateWindow = {0x6BB65C5A, 0x2F31, 0x4BDE, {0x9F, 0x48, 0x8A, 0x38, 0xDC, 0x0C, 0x63, 0xE7}};
static const GUID IntelliTrace = {0xFFA74B06, 0xE011, 0x49BE, {0xA5, 0x8C, 0x3E, 0xFA, 0xA4, 0xB9, 0x57, 0xD5}};
static const GUID Locals = {0x8259ACED, 0x490A, 0x41B3, {0xA0, 0xFB, 0x64, 0xC8, 0x42, 0xCC, 0xDC, 0x80}};
static const GUID ManifestDesigner = {0xB239F458, 0x9F75, 0x4376, {0x95, 0x9B, 0x4D, 0x48, 0xB8, 0x93, 0x37, 0xF4}};
static const GUID NewProjectDialog = {0xC36C426E, 0x31C9, 0x4048, {0x84, 0xCF, 0x31, 0xC1, 0x11, 0xD6, 0x5E, 0xC0}};
static const GUID OutputWindow = {0x9973EFDF, 0x317D, 0x431C, {0x8B, 0xC1, 0x5E, 0x88, 0xCB, 0xFD, 0x4F, 0x7F}};
static const GUID PackageManagerConsole = {
    0xF9D6BCE6, 0xC669, 0x41DB, {0x8E, 0xE7, 0xDD, 0x95, 0x38, 0x28, 0x68, 0x5B}};
static const GUID PageInspector = {0x6CBC7204, 0x91B7, 0x416B, {0xA1, 0xA1, 0x34, 0xB9, 0x41, 0x42, 0x99, 0x8E}};
static const GUID ProgressBar = {0x94ACF70F, 0xA81D, 0x4512, {0xA3, 0x1D, 0x81, 0x96, 0x61, 0x67, 0x51, 0xEE}};
static const GUID ProjectDesigner = {0xEF1A2D2C, 0x5D16, 0x4DDB, {0x8D, 0x04, 0x79, 0xD0, 0xF6, 0xC1, 0xC5, 0x6E}};
static const GUID ReSharper = {0xB7F08E34, 0xAD23, 0x4855, {0x9D, 0xAF, 0xF1, 0xCF, 0x80, 0x8A, 0x7C, 0x72}};
static const GUID ReSharperHighlighting = {
    0x75A05685, 0x00A8, 0x4DED, {0xBA, 0xE5, 0xE7, 0xA5, 0x0B, 0xFA, 0x92, 0x9A}};
static const GUID SearchControl = {0xF1095FAD, 0x881F, 0x45F1, {0x85, 0x80, 0x58, 0x9E, 0x10, 0x32, 0x5E, 0xB8}};
static const GUID SharePointTools = {0x7E8DA76D, 0x24B4, 0x447C, {0x8E, 0xCE, 0xFF, 0x8E, 0x0E, 0x73, 0xF0, 0xD4}};
static const GUID SQLResultsGrid = {0x6202FF3E, 0x488E, 0x4EAD, {0x92, 0xCB, 0xBE, 0x08, 0x96, 0x59, 0xF8, 0xD7}};
static const GUID SQLResultsText = {0x587D0421, 0xE473, 0x4032, {0xB2, 0x14, 0x93, 0x59, 0xF3, 0xB7, 0xBC, 0x80}};
static const GUID TeamExplorer = {0x4AFF231B, 0xF28A, 0x44F0, {0xA6, 0x6B, 0x1B, 0xEE, 0xB1, 0x7C, 0xB9, 0x20}};
static const GUID TextEditorLanguageServiceItems = {
    0xE0187991, 0xB458, 0x4F7E, {0x8C, 0xA9, 0x42, 0xC9, 0xA5, 0x73, 0xB5, 0x6C}};
static const GUID TextEditorTextManagerItems = {
    0x58E96763, 0x1D3B, 0x4E05, {0xB6, 0xBA, 0xFF, 0x71, 0x15, 0xFD, 0x0B, 0x7B}};
static const GUID TextEditorTextMarkerItems = {
    0xFF349800, 0xEA43, 0x46C1, {0x8C, 0x98, 0x87, 0x8E, 0x78, 0xF4, 0x65, 0x01}};
static const GUID TreeView = {0x92ECF08E, 0x8B13, 0x4CF4, {0x99, 0xE9, 0xAE, 0x26, 0x92, 0x38, 0x21, 0x85}};
static const GUID VersionControl = {0x6BE84F44, 0x74E4, 0x4E5F, {0xAE, 0xE9, 0x1B, 0x93, 0x0F, 0x43, 0x13, 0x75}};
static const GUID Watch = {0x358463D0, 0xD084, 0x400F, {0x99, 0x7E, 0xA3, 0x4F, 0xC5, 0x70, 0xBC, 0x72}};
static const GUID WebClientDiagnosticTools = {
    0x2AA714AE, 0x53BE, 0x4393, {0x84, 0xE0, 0xDC, 0x95, 0xB5, 0x7A, 0x18, 0x91}};
static const GUID WorkflowDesigner = {0xE0F1945B, 0xB965, 0x47DC, {0xB2, 0x2E, 0x3E, 0x26, 0xA8, 0x95, 0xC8, 0x95}};
static const GUID WorkItemEditor = {0x2138D120, 0x456D, 0x425E, {0x80, 0xB5, 0x88, 0xD2, 0x40, 0x1F, 0xCA, 0x23}};
} // namespace ThemeCategory11

#define NS_THEMEUTILS_BEGIN                                                                                            \
	namespace ThemeUtils                                                                                               \
	{
#define NS_THEMEUTILS_END }

NS_THEMEUTILS_BEGIN

class CBrushMap;
class CPenMap;

void ApplyThemeInProcess(BOOL use_theme, DWORD pid = GetCurrentProcessId());
void ApplyThemeInWindows(BOOL use_theme, HWND parent_node);

void ForEachChild(HWND parent_node, std::function<bool(HWND)> func);

// replacement of unsafe GetWindow() calls which may end up in endless loop
void ForEachTopLevelChild(HWND parent_node, HWND start_node_or_null, std::function<bool(HWND)> func);

// Fixes Z order of children
void ReorderChildren(HWND parent, bool childrenOfChildren = false);

COLORREF InterpolateColor(COLORREF backColor, COLORREF frontColor, float frontOpacity);
void InterpolateGradientColors(COLORREF* out_colors, int out_colors_size, const std::pair<double, COLORREF>* gradient,
                               int num_gradient);
void InterpolateGradientColorsVS2010(COLORREF* out_colors, int out_colors_size, const COLORREF* colors, int num_colors);
void Rect_CenterAlign(LPCRECT fixed, LPRECT align, bool horizontal = true, bool vertical = true);
bool Rect_RestrictToRect(LPCRECT constraint, LPRECT toRestrict);

void Rect_AdjustToFit(CRect& rectToMove, const CRect& boundingRect);


void DrawNCBorder(CWnd* wnd, COLORREF color);
void FillNonClientArea(CWnd* wnd, COLORREF color, const COLORREF* border = nullptr);
void DrawNCBorderClientEdge(CWnd* wnd, COLORREF border, COLORREF bg, bool force_bg = false);
void GetNonClientRegion(CWnd* wnd, CRgn& wrgn, bool border);
void GetClientRegion(CWnd* wnd, CRgn& wrgn, bool relative_to_client, bool clip_children);

CDialog* FindParentDialog(CWnd* wnd, bool only_WS_POPUP = true);

LRESULT CenteredEditNCCalcSize(CEdit* edit, WPARAM wParam, LPARAM lParam, LPRECT offset = nullptr);
LRESULT CenteredEditNCHitTest(CEdit* edit, WPARAM wParam, LPARAM lParam);

enum Symbol
{
	Symb_None,   // empty (no drawing)
	Symb_Check,  // normal check
	Symb_Cross,  // crossed check box
	Symb_Rect,   // indeterminate state of check box
	Symb_Ellipse // checked radio button
};

enum Box
{
	Box_None,   // empty (no drawing)
	Box_Rect,   // rectangular box (as in check box)
	Box_Ellipse // elliptical box (as in radio button)
};

void DrawSymbol(HDC dc, LPRECT rect, COLORREF color, Symbol symb = Symb_Check, float pen_width = 0.0f);
void DrawThemedCheckBox(CDC& dc, CRect& rect, Symbol symb, bool enabled);
void DrawCheckBoxEx(CDC& dc, CRect& rect, Symbol symb, COLORREF border, COLORREF bg, COLORREF check);
void FillClientRect(CWnd* wnd, CDC* dc, COLORREF color);
void FrameClientRect(CWnd* wnd, CDC* dc, COLORREF outline, COLORREF fill = CLR_INVALID);
void FrameRectDPI(CDC& dc, const CRect& rect, COLORREF outline, RECT sides = {1, 1, 1, 1});
void DrawTreeExpandGlyph(CDC& dc, CRect& buttonrect, COLORREF bordercolor, COLORREF fillcolor, bool expanded);

void FillRectAlpha(HDC dc, LPCRECT rect, COLORREF rgb, BYTE alpha);
bool DrawImageGray(HDC dc, CImageList* list, int index, const POINT& pnt, float opacity = 1.0f, float offset = 0.0f);
bool DrawImageColorMatrix(HDC dc, CImageList* list, int index, const POINT& pnt, float matrix[5][5]);
bool DrawImageOpacity(HDC dc, CImageList* list, int index, const POINT& pnt, float opacity = 1.0f);
void DrawPolygon(CDC& dc, const POINT* points, int num_points, COLORREF fill, COLORREF border, int pen_style = PS_SOLID,
                 int pen_width = 0);
bool DrawImageHighliteTheme17(HDC dc, CImageList* list, int index, const POINT& pnt, COLORREF color);
bool DrawImageThemedForBackground(HDC dc, CImageList* list, int index, const POINT& pnt, COLORREF bgColor);
void DelayedRefresh(CWnd* wnd, UINT ms_duration);

// invokes workFnc once the waitFnc returns 0, waitFnc is always being executed on background thread
// and returns count of milliseconds to be waited before another waitFnc call, 
// if waitFnc returns negative value this_thread::yield() is used after waiting for inverted count of milliseconds, 
// once waitFnc returns 0, workFnc is executed depending on the mainThread parameter.
// You can specify custom name of waiter thread using threadName parameter.
void DelayedInvoke(bool mainThread, std::function<int()> waitFnc, std::function<void()> workFnc, const char* threadName = "VA_DelayedInvoke");

bool DrawImage(HDC dc, CBitmap& bmp, const POINT& pnt);

CStringA GetWindowMessageName(UINT msg);
CStringA GetWindowMessageString(UINT msg, WPARAM wParam, LPARAM lParam);

CStringA GetTooltipMessageName(UINT msg);
CStringA GetTooltipMessageString(UINT msg, WPARAM wParam, LPARAM lParam);

CStringA GetListMessageName(UINT msg);
CStringA GetListMessageString(UINT msg, WPARAM wParam, LPARAM lParam);

void TraceFrameDbgPrintMSG(UINT msg, WPARAM wParam, LPARAM lParam);

void DrawRoundedRect(HDC dc, LPRECT rect, COLORREF color, int radius);

void DrawFocusRect(HDC dc, LPRECT rect, COLORREF color, bool dashed = true);

void TraceFramePrint(LPCTSTR lpszFormat /*, ...*/);

bool TransformRectColors(CDC& dc, const CRect& rect, std::function<void(int x, int y, COLORREF& color)> transform);

bool MakeBitmapTransparent(CBitmap& bitmap, COLORREF clrTransparent = CLR_INVALID);

void ThemeBitmap(CBitmap& bitmap, COLORREF bgClr);
void LogicalToDeviceBitmap(CWnd& wnd, CBitmap& bmp, double quality = 3.0);
void LogicalToDeviceBitmap2(CBitmap& bmp, double quality = 3.0);

bool GetTomatoBitmap(CBitmap& bitmap, int size, COLORREF bgColor);

void GDIBuffer_Clear(bool brushes = true, bool pens = true);

CBrushMap& GDIBuffer_BrushMap();
CPenMap& GDIBuffer_PenMap();
CBrush& GDIBuffer_GetBrush(COLORREF color);
CPen& GDIBuffer_GetPen(COLORREF color, int style = PS_SOLID, int width = 0);

BYTE ColorBrightness(COLORREF rgb);
BYTE ColorRGBAverage(COLORREF rgb);
void ColorToAlpha(COLORREF rgb, COLORREF toAlpha, UINT& r, UINT& g, UINT& b, UINT& a);

// ranges:
// hue in range 0 - 360
// saturation in range 0 - 100
// luminance in range 0 - 100
void RGBtoHSL(COLORREF rgb, UINT& h, UINT& s, UINT& l);
COLORREF HSLtoRGB(const UINT& h, const UINT& s, const UINT& l);
COLORREF BrightenColor(COLORREF rgb, UINT amount);
COLORREF DarkenColor(COLORREF rgb, UINT amount);
COLORREF DesaturateColor(COLORREF rgb, BYTE percents);

// argb - DWORDs in form 0xAARRGGBB, incompatible with COLORREF which is 0x00BBGGRR!!!
// count - count of DWORDs
// backColor - background color used for changes consideration
bool ThemeColors(DWORD* argb, int count, COLORREF backColor);

HRESULT ThemeDIBits(
    DWORD dwBitmapLength,
    BYTE *pBitmap,
    DWORD dwPixelWidth,
    DWORD dwPixelHeight,
    VARIANT_BOOL fIsTopDownBitmap,
    COLORREF crBackground);

// DT_CALCRECT does not work correctly in Windows 8
bool GetTextSizeW(HDC dc, LPCWSTR str, int str_len, LPSIZE out_size);

//////////////////////////////////////////////////
// CMemDC - memory DC
//
// Author: Keith Rule
// Email:  keithr@europa.com
// Copyright 1996-2002, Keith Rule
//
// You may freely use or modify this code provided this
// Copyright is included in all derived versions.
//
// History - 10/3/97 Fixed scrolling bug.
//               Added print support. - KR
//
//       11/3/99 Fixed most common complaint. Added
//            background color fill. - KR
//
//       11/3/99 Added support for mapping modes other than
//            MM_TEXT as suggested by Lee Sang Hun. - KR
//
//       02/11/02 Added support for CScrollView as supplied
//             by Gary Kirkham. - KR
//
// This class implements a memory Device Context which allows
// flicker free drawing.

class CMemDC : public CDC
{
  private:
	CBitmap m_bitmap;     // Offscreen bitmap
	CBitmap* m_oldBitmap; // bitmap originally found in CMemDC
	CDC* m_pDC;           // Saves CDC passed in constructor
	CRect m_rect;         // Rectangle of drawing area.
	BOOL m_bMemDC;        // TRUE if CDC really is a Memory DC.
  public:
	CMemDC(CDC* pDC, const CRect* pRect = NULL);
	CMemDC(CDC* pDC, bool fill_bg, const CRect* pRect = NULL);

	~CMemDC();

	// Allow usage as a pointer
	CMemDC* operator->()
	{
		return this;
	}

	// Allow usage as a pointer
	operator CMemDC*()
	{
		return this;
	}
};

class CBrushMap
{
	typedef std::shared_ptr<CBrush> brush_ptr;
	typedef std::map<COLORREF, brush_ptr> brush_map;
	brush_map m_map;
	CCriticalSection m_cs;

  public:
	CBrush& GetBrush(COLORREF color);
	CBrush* GetBrushP(COLORREF color);
	HBRUSH GetHBRUSH(COLORREF color);
	void Erase(COLORREF color);
	void Clear();
};

class CPenMap
{
	struct pen_index
	{
		COLORREF color;
		int style;
		int width;

		pen_index(COLORREF c, int s, int w);
		bool operator<(const pen_index& rhs) const;
	};

	typedef std::shared_ptr<CPen> pen_ptr;
	typedef std::map<pen_index, pen_ptr> pen_map;
	pen_map m_map;
	CCriticalSection m_cs;

  public:
	CPen& GetPen(COLORREF color, int style = PS_SOLID, int width = 0);
	CPen* GetPenP(COLORREF color, int style = PS_SOLID, int width = 0);
	HPEN GetHPEN(COLORREF color, int style = PS_SOLID, int width = 0);
	void Erase(COLORREF color, int style = PS_SOLID, int width = 0);
	void Clear();
};

class AutoFnc
{
	std::function<void(void)> m_func;

  public:
	AutoFnc(std::function<void(void)> fnc) : m_func(fnc)
	{
	}
	~AutoFnc()
	{
		m_func();
	}
};

class AutoWM
{
  public:
	HWND wnd;
	UINT msg;
	LPARAM lp;
	WPARAM wp;

	AutoWM(HWND hWnd, UINT d_msg, WPARAM d_wParam, LPARAM d_lParam) : wnd(hWnd), msg(d_msg), lp(d_lParam), wp(d_wParam)
	{
	}

	AutoWM(HWND hWnd, UINT c_msg, WPARAM c_wParam, LPARAM c_lParam, UINT d_msg, WPARAM d_wParam, LPARAM d_lParam)
	    : wnd(hWnd), msg(d_msg), lp(d_lParam), wp(d_wParam)
	{
		if (c_msg)
			SendMessage(wnd, c_msg, c_wParam, c_lParam);
	}

	~AutoWM()
	{
		if (msg)
			SendMessage(wnd, msg, wp, lp);
	}
};

class AutoLockRedrawHWND : public AutoWM
{
  public:
	AutoLockRedrawHWND(HWND wnd) : AutoWM(wnd, WM_SETREDRAW, FALSE, 0, WM_SETREDRAW, TRUE, 0)
	{
	}
};

class AutoLockUpdate
{
	CWnd& m_wnd;
	bool m_lock_redraw;

  public:
	AutoLockUpdate(CWnd& wnd, bool lock_redraw = true) : m_wnd(wnd), m_lock_redraw(lock_redraw)
	{
		m_wnd.LockWindowUpdate();
		if (lock_redraw)
			m_wnd.SetRedraw(FALSE);
	}
	~AutoLockUpdate()
	{
		m_wnd.UnlockWindowUpdate();
		if (m_lock_redraw)
			m_wnd.SetRedraw(TRUE);
	}
};

class AutoTextAlign
{
	UINT m_align;
	HDC m_dc;

  public:
	AutoTextAlign(HDC dc, UINT align) : m_dc(dc)
	{
		m_align = ::GetTextAlign(dc);
		::SetTextAlign(dc, align);
	}

	~AutoTextAlign()
	{
		::SetTextAlign(m_dc, m_align);
	}
};

class AutoSelectGDIObj
{
	HDC m_dc;
	HGDIOBJ m_prev_obj;

  public:
	AutoSelectGDIObj(HDC dc, HGDIOBJ to_select) : m_dc(dc), m_prev_obj(::SelectObject(dc, to_select))
	{
	}
	~AutoSelectGDIObj()
	{
		::SelectObject(m_dc, m_prev_obj);
	}
};

class AutoSaveRestoreDC
{
	int m_state;
	HDC m_dc;

  public:
	AutoSaveRestoreDC(HDC dc) : m_state(SaveDC(dc)), m_dc(dc)
	{
	}
	~AutoSaveRestoreDC()
	{
		RestoreDC(m_dc, m_state);
	}
};

class AutoTextColor
{
	CDC& m_dc;
	COLORREF m_prev_color;
	int m_prev_bk_mode;

  public:
	AutoTextColor(CDC& dc, COLORREF to_set, int bkMode = TRANSPARENT)
	    : m_dc(dc), m_prev_color(dc.SetTextColor(to_set)), m_prev_bk_mode(dc.SetBkMode(bkMode))
	{
	}
	~AutoTextColor()
	{
		m_dc.SetBkMode(m_prev_bk_mode);
		m_dc.SetTextColor(m_prev_color);
	}
};

class AutoMapMode
{
	CDC& m_dc;
	INT m_prev_mm;

  public:
	AutoMapMode(CDC& dc, INT mm) : m_dc(dc), m_prev_mm(dc.SetMapMode(mm))
	{
	}
	~AutoMapMode()
	{
		m_dc.SetMapMode(m_prev_mm);
	}
};

struct TmpNoHL
{
	int m_old_paint;
	TmpNoHL() : m_old_paint(PaintType::in_WM_PAINT)
	{
		PaintType::in_WM_PAINT = PaintType::DontColor;
	}
	~TmpNoHL()
	{
		PaintType::in_WM_PAINT = m_old_paint;
	}
};

struct TmpPaintType
{
	int m_old_paint;
	TmpPaintType(int paint_type) : m_old_paint(PaintType::in_WM_PAINT)
	{
		PaintType::in_WM_PAINT = paint_type;
	}
	~TmpPaintType()
	{
		PaintType::in_WM_PAINT = m_old_paint;
	}
};

// logic stolen from VsUIGdiplusImage.cpp
class GDIPlusManager final
{
	GDIPlusManager() : m_GdiplusToken(0)
	{
	}

  public:
	~GDIPlusManager()
	{
		release();
	}

	static void EnsureGDIPlus()
	{
		s_instance.init();
	}

  private:
	bool init();
	void release();

	static GDIPlusManager s_instance;

	ULONG_PTR interlockedExchangeToken(ULONG_PTR token);
	volatile ULONG_PTR m_GdiplusToken;
};

class CXTheme
{
	HTHEME m_theme = nullptr;
	CStringW m_classList;
	HWND m_hWnd = nullptr;

  public:
	CXTheme()
	{
	}
	CXTheme(HWND hWnd, LPCWSTR pszClassList)
	{
		OpenThemeData(hWnd, pszClassList);
	}

	~CXTheme()
	{
		CloseThemeData();
	}

	bool IsOpen() const
	{
		return m_theme != nullptr;
	}
	HTHEME& Handle()
	{
		return m_theme;
	}
	const HTHEME& Handle() const
	{
		return m_theme;
	}
	HWND WindowHandle() const
	{
		return m_hWnd;
	}
	LPCWSTR ClassList() const
	{
		return m_classList;
	}

	void CloseThemeData();
	void OpenThemeData(HWND hWnd, LPCWSTR pszClassList);
	// invoke on WM_THEMECHANGED
	void OnThemeChanged();
	COLORREF GetSysColor(int colorId);
	HRESULT DrawParentBGIfTransparent(HDC dc, int iPartId, int iStateId, LPCRECT rect, LPCRECT clipRect) const;
	HRESULT DrawBG(HDC dc, int iPartId, int iStateId, LPCRECT rect, LPCRECT clipRect, bool parent = true) const;
	HRESULT DrawBGEx(HDC dc, int iPartId, int iStateId, LPCRECT rect, const DTBGOPTS* opts, bool parent = true) const;
	HRESULT DrawBGEx(HDC dc, int iPartId, int iStateId, LPCRECT rect, bool parent = true, bool omitBorder = false,
	                 bool omitContent = false, LPCRECT clipRect = nullptr) const;
	static bool IsAppThemed();
	bool IsThemePartDefined(int iPartId, int iStateId);
	HRESULT DrawEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BDR_flags, UINT BF_flags,
	                 LPRECT opt_outContentRect = nullptr);
	HRESULT DrawBumpEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                     LPRECT opt_outContentRect = nullptr);
	HRESULT DrawEtchedEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                       LPRECT opt_outContentRect = nullptr);
	HRESULT DrawSunkenEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                       LPRECT opt_outContentRect = nullptr);
	HRESULT DrawFlatOuterEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                          LPRECT opt_outContentRect = nullptr);
	HRESULT DrawFlatInnerEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                          LPRECT opt_outContentRect = nullptr);
	HRESULT DrawMonoOuterEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                          LPRECT opt_outContentRect = nullptr);
	HRESULT DrawMonoInnerEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
	                          LPRECT opt_outContentRect = nullptr);
	bool IsBGPartiallyTransparent(int iPartId, int iStateId) const;
};

typedef std::shared_ptr<CXTheme> CXThemePtr;

class ColorSet
{
	typedef std::vector<std::pair<double, COLORREF>> brush_colors;
	brush_colors colors;

  public:
	ColorSet();
	ColorSet& operator=(const ColorSet& other);
	ColorSet(const ColorSet& other);
	ColorSet(COLORREF color);
	ColorSet(COLORREF c1, COLORREF c2);
	ColorSet(COLORREF c1, COLORREF c2, COLORREF c3);
	ColorSet(COLORREF c1, COLORREF c2, COLORREF c3, COLORREF c4);
	ColorSet(COLORREF* in_colors, double* in_pos, size_t in_count);

	void Init(COLORREF color);
	void Init(COLORREF c1, COLORREF c2);
	void Init(COLORREF c1, COLORREF c2, COLORREF c3);
	void Init(COLORREF c1, COLORREF c2, COLORREF c3, COLORREF c4);
	void Init(COLORREF* in_colors, double* in_pos, size_t in_count);

	int ColorCount() const;
	bool IsSolid() const;
	COLORREF Color(int index = 0) const;
	double Param(int index = 0) const;
	void DrawVertical(CDC& dc, const CRect& rect) const;
	void DrawVertical(CDC& dc, const CRect& rect, float opacity) const;

	void MixColors(COLORREF overlay, float opacity);
	void Desaturate(BYTE percents);
	COLORREF Average() const;
};

class Canvas
{
  public:
	typedef std::shared_ptr<Gdiplus::Brush> brushP;
	typedef std::shared_ptr<Gdiplus::Pen> penP;
	typedef std::tuple<float, float> pnt;

	struct Context
	{
		brushP brush;
		penP pen;
		float xScale;
		float yScale;

		std::vector<penP>& pens;
		std::vector<brushP>& brushes;

		Gdiplus::Graphics& gr;

		bool IsScaled()
		{
			return xScale != 1.0f || yScale != 1.0f;
		}

		Context(Gdiplus::Graphics& graphics, std::vector<penP>& ps, std::vector<brushP>& brs, float x_scale,
		        float y_scale)
		    : xScale(x_scale), yScale(y_scale), pens(ps), brushes(brs), gr(graphics)
		{
		}
	};

	typedef std::function<void(Context& ctx)> action;
	typedef std::function<void(Gdiplus::Graphics& gr)> setup_gr;

  private:
	std::vector<action> m_actions;

	std::vector<penP> m_pens;
	std::vector<brushP> m_brushes;

	float m_width;
	float m_height;

  public:
	Canvas(float width, float height) : m_width(width), m_height(height)
	{
		GDIPlusManager::EnsureGDIPlus();
	}

	Gdiplus::Pen* GetPen(DWORD id)
	{
		return m_pens.size() > id ? m_pens[id].get() : nullptr;
	}
	Gdiplus::Brush* GetBrush(DWORD id)
	{
		return m_brushes.size() > id ? m_brushes[id].get() : nullptr;
	}

	Canvas& AddAction(action act);

	Canvas& UsePen(DWORD id);
	Canvas& UsePenMinWidth(DWORD id, float min_width);
	Canvas& UsePenFixed(DWORD id);
	Canvas& UseBrush(DWORD id);
	Canvas& UsePenAndBrush(DWORD id_pen, DWORD id_brush);

	Canvas& SetBrush(DWORD index, brushP brush);
	Canvas& SetSolidBrush(DWORD index, BYTE R, BYTE G, BYTE B, BYTE A = 0xFF);
	Canvas& SetSolidBrush(DWORD index, COLORREF rgb, BYTE alpha = 0xFF);

	Canvas& SetPen(DWORD index, penP pen);
	Canvas& SetPen(DWORD index, BYTE R, BYTE G, BYTE B, BYTE A = 0xFF);
	Canvas& SetPen(DWORD index, COLORREF rgb, BYTE alpha = 0xFF);
	Canvas& SetPen(DWORD index, float width, BYTE R, BYTE G, BYTE B, BYTE A = 0xFF);
	Canvas& SetPen(DWORD index, float width, COLORREF rgb, BYTE alpha = 0xFF);

	Canvas& Line(float x1, float y1, float x2, float y2);
	Canvas& Ellipse(float x1, float y1, float x2, float y2);
	Canvas& Arc(float x1, float y1, float x2, float y2, float start, float sweep);
	Canvas& Rect(float x1, float y1, float x2, float y2);

	void Paint(Gdiplus::Graphics& graphics, float x_scale = 1.0f, float y_scale = 1.0f);
	void Paint(HDC hdc, float x, float y, float cx, float cy, setup_gr sgr = nullptr);
	void Paint(HDC hdc, float x, float y, setup_gr sgr = nullptr);
	void Paint(HDC hdc, const CRect& rect, setup_gr sgr = nullptr);
};

NS_THEMEUTILS_END

#endif // VAThemeUtils_h__
