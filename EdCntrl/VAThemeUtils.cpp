#include "stdafxed.h"
#include "VAThemeUtils.h"
#include <afxpriv.h>
#include "ColorListControls.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "TraceWindowFrame.h"
#include "PROJECT.H"
#include "ImageListManager.h"

#include <functional>
#include <set>

#include <afxdrawmanager.h>
#include <afxglobals.h>
#include "DpiCookbook\VsUIGdiplusImage.h"
#include "IdeSettings.h"
#include "vsshell140.h"
#include "PooledThreadBase.h"
#include "SubClassWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

NS_THEMEUTILS_BEGIN

LPCWSTR tomatoXAML_FMT = LR"XAML(<Viewbox Width="16.000" Height="16.000"
  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <Rectangle Width="16 " Height="16">
    <Shape.Fill>
      <DrawingBrush Stretch="Uniform">
        <DrawingBrush.Drawing>
          <DrawingGroup>
            <DrawingGroup x:Name="Canvas">
              <GeometryDrawing Geometry="F1 M16,16 L0,16 L0,0 L16,0" />
            </DrawingGroup>
            <DrawingGroup x:Name="level_1">
              <GeometryDrawing Brush="#COLOR0" Geometry="F1 M 8.000,3.390 C 4.390,3.390 1.470,5.790 1.470,8.760 C 1.470,11.730 4.380,14.120 8.000,14.120 C 11.620,14.120 14.530,11.720 14.530,8.760 C 14.530,5.800 11.600,3.390 8.000,3.390 Z M 8.000,4.390 C 11.000,4.390 13.530,6.390 13.530,8.760 C 13.530,11.130 11.000,13.120 8.000,13.120 C 5.000,13.120 2.470,11.120 2.470,8.760 C 2.470,6.400 4.940,4.390 8.000,4.390" />
              <GeometryDrawing Brush="#COLOR1" Geometry="F1 M 7.990,4.080 C 11.099,4.080 13.620,6.122 13.620,8.640 C 13.620,11.158 11.099,13.200 7.990,13.200 C 4.881,13.200 2.360,11.158 2.360,8.640 C 2.360,6.122 4.881,4.080 7.990,4.080 Z" />
              <GeometryDrawing Brush="#COLOR2" Geometry="F1 M 5.070,1.290 L 6.920,1.290 L 7.060,4.110 L 8.940,2.390 L 8.010,4.670 L 10.480,5.930 L 7.790,5.550 L 7.550,7.520 L 6.630,5.730 L 3.720,6.570 L 5.830,5.030 L 3.080,3.230 L 6.400,4.210 L 5.070,1.290 Z" />
            </DrawingGroup>
          </DrawingGroup>
        </DrawingBrush.Drawing>
      </DrawingBrush>
    </Shape.Fill>
  </Rectangle>
</Viewbox>)XAML";

void ApplyThemeInProcess(BOOL use_theme, DWORD pid /*= GetCurrentProcessId()*/)
{
	struct ProcData
	{
		DWORD pid;
		BOOL use_theme;
	};

	struct EnumWnds
	{
		static BOOL CALLBACK Proc(HWND hwnd, LPARAM lParam)
		{
			ProcData* data = (ProcData*)lParam;
			if (data != nullptr)
			{
				DWORD dwID = 0;
				GetWindowThreadProcessId(hwnd, &dwID);

				if (dwID == data->pid)
				{
					DWORD_PTR rslt = TRUE;
					SendMessageTimeout(hwnd, WM_VA_APPLYTHEME, (WPARAM)data->use_theme, 0, SMTO_NORMAL, 100, &rslt);
					if (rslt == FALSE)
						return FALSE;
				}
			}
			return TRUE;
		}
	};

	ProcData data = {pid, use_theme};

	EnumWindows((WNDENUMPROC)EnumWnds::Proc, (LPARAM)&data);
}

void ApplyThemeInWindows(BOOL use_theme, HWND parent_node)
{
	struct ProcData
	{
		BOOL use_theme;
	};

	struct EnumWnds
	{
		static BOOL CALLBACK Proc(HWND hwnd, LPARAM lParam)
		{
			ProcData* data = (ProcData*)lParam;
			if (data != nullptr)
			{
				DWORD_PTR rslt = TRUE;
				SendMessageTimeout(hwnd, WM_VA_APPLYTHEME, (WPARAM)data->use_theme, 0, SMTO_NORMAL, 100, &rslt);
				// if (rslt == FALSE)
				//	return FALSE;
			}
			return TRUE;
		}
	};

	ProcData data = {use_theme};

	// as first, send message to parent window
	DWORD_PTR rslt = TRUE;
	SendMessageTimeout(parent_node, WM_VA_APPLYTHEME, (WPARAM)use_theme, 0, SMTO_NORMAL, 100, &rslt);
	if (rslt == FALSE)
		return;

	// OK continue with childs
	EnumChildWindows(parent_node, (WNDENUMPROC)EnumWnds::Proc, (LPARAM)&data);
}

void ForEachChild(HWND parent_node, std::function<bool(HWND)> func)
{
	if (func && !!::GetWindow(parent_node, GW_CHILD))
	{
		struct EnumWnds
		{
			static BOOL CALLBACK Proc(HWND hwnd, LPARAM lParam)
			{
				std::function<bool(HWND)>* fnc = (std::function<bool(HWND)>*)lParam;
				return (*fnc)(hwnd) ? TRUE : FALSE;
			}
		};

		EnumChildWindows(parent_node, (WNDENUMPROC)EnumWnds::Proc, (LPARAM)&func);
	}
}

void ForEachTopLevelChild(HWND parent_node, HWND start_node_or_null, std::function<bool(HWND)> func)
{
	if (func && !!::GetWindow(parent_node, GW_CHILD))
	{
		struct EnumWnds
		{
			static BOOL CALLBACK Proc(HWND hwnd, LPARAM lParam)
			{
				auto ptr = (EnumWnds*)lParam;

				if (ptr->start)
				{
					if (hwnd != ptr->start)
						return TRUE;

					ptr->start = nullptr;
				}

				// order of windows is undocumented, but I have seen implementation
				// and function building HWND list goes to children recursively 
				// right after the parent has been added 
				// I assume they will go out in the same order as listed in Spy++

				if (::GetParent(hwnd) == ptr->parent)
				{
					return ptr->func(hwnd) ? TRUE : FALSE;
				}

				return TRUE;
			}

			std::function<bool(HWND)> func;
			HWND parent = nullptr;
			HWND start = nullptr;
		};

		EnumWnds enumWnds;
		enumWnds.func = func;
		enumWnds.parent = parent_node;
		enumWnds.start = start_node_or_null;

		EnumChildWindows(parent_node, (WNDENUMPROC)EnumWnds::Proc, (LPARAM)&enumWnds);
	}
}


COLORREF InterpolateColor(COLORREF backColor, COLORREF frontColor, float frontOpacity)
{
	if (frontOpacity == 1.0f)
		return frontColor;

	float backOpacity = 1.0f - frontOpacity;
	BYTE R = (BYTE)((float)GetRValue(backColor) * backOpacity + (float)GetRValue(frontColor) * frontOpacity);
	BYTE G = (BYTE)((float)GetGValue(backColor) * backOpacity + (float)GetGValue(frontColor) * frontOpacity);
	BYTE B = (BYTE)((float)GetBValue(backColor) * backOpacity + (float)GetBValue(frontColor) * frontOpacity);
	return RGB(R, G, B);
}

bool Rect_RestrictToRect(LPCRECT constraint, LPRECT toRestrict)
{
	bool modified = false;

	if (toRestrict->top < constraint->top)
	{
		modified = true;
		::OffsetRect(toRestrict, 0, constraint->top - toRestrict->top);
	}

	if (toRestrict->bottom > constraint->bottom)
	{
		modified = true;
		::OffsetRect(toRestrict, 0, -(toRestrict->bottom - constraint->bottom));
		if (toRestrict->top < constraint->top)
		{
			toRestrict->top = constraint->top;
			toRestrict->bottom = constraint->bottom;
		}
	}

	if (toRestrict->left < constraint->left)
	{
		modified = true;
		::OffsetRect(toRestrict, constraint->left - toRestrict->left, 0);
	}

	if (toRestrict->right > constraint->right)
	{
		modified = true;
		::OffsetRect(toRestrict, -(toRestrict->right - constraint->right), 0);
		if (toRestrict->left < constraint->left)
		{
			toRestrict->left = constraint->left;
			toRestrict->right = constraint->right;
		}
	}

	return modified;
}

void Rect_CenterAlign(LPCRECT fixed, LPRECT align, bool horizontal /*= true*/, bool vertical /*= true*/)
{
	if (horizontal)
	{
		int mid_x_fixed = fixed->left + (fixed->right - fixed->left) / 2;
		int mid_x_align = align->left + (align->right - align->left) / 2;
		int x_offset = mid_x_fixed - mid_x_align;

		align->left += x_offset;
		align->right += x_offset;
	}

	if (vertical)
	{
		int mid_y_fixed = fixed->top + (fixed->bottom - fixed->top) / 2;
		int mid_y_align = align->top + (align->bottom - align->top) / 2;
		int y_offset = mid_y_fixed - mid_y_align;

		align->top += y_offset;
		align->bottom += y_offset;
	}
}

bool GetTextSizeW(HDC dc, LPCWSTR str, int str_len, LPSIZE out_size)
{
	return TRUE ==
	       GetTextExtentExPointW(dc, str, str_len >= 0 ? str_len : (int)wcslen(str), 0, nullptr, nullptr, out_size);
}

void DrawNCBorder(CWnd* wnd, COLORREF color)
{
	if (wnd == nullptr)
		return;

	CWindowDC dc(wnd);
	if (dc.GetSafeHdc())
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*wnd);

		CRect rcBorder;
		wnd->GetWindowRect(rcBorder);
		rcBorder.bottom = rcBorder.Height();
		rcBorder.right = rcBorder.Width();
		rcBorder.left = rcBorder.top = 0;
		FrameRectDPI(dc, rcBorder, color);
	}
}

void GetNonClientRegion(CWnd* wnd, CRgn& wrgn, bool border)
{
	CRect wr, cr;
	wnd->GetWindowRect(wr);
	wnd->GetClientRect(cr);
	wnd->ClientToScreen(cr);

	cr.OffsetRect(-wr.left, -wr.top);
	wr.OffsetRect(-wr.left, -wr.top);

	if (!border)
	{
		CRect border_rect;
		if (AdjustWindowRectEx(&border_rect, wnd->GetStyle(), FALSE, wnd->GetExStyle()))
		{
			wr.left -= border_rect.left;
			wr.top -= border_rect.top;
			wr.right -= border_rect.right;
			wr.bottom -= border_rect.bottom;
		}
	}

	CRgn crgn;
	wrgn.CreateRectRgnIndirect(&wr);
	crgn.CreateRectRgnIndirect(&cr);
	wrgn.CombineRgn(&wrgn, &crgn, RGN_DIFF);
}

void GetClientRegion(CWnd* wnd, CRgn& wrgn, bool relative_to_client, bool clip_children)
{
	CRect cr;
	CSize offset;
	wnd->GetClientRect(cr);

	if (!relative_to_client)
	{
		CRect wr;
		wnd->GetWindowRect(wr);
		wnd->ClientToScreen(cr);
		cr.OffsetRect(-wr.left, -wr.top);
	}

	wrgn.CreateRectRgnIndirect(&cr);

	if (clip_children)
	{
		CRgn childs_rgn;
		childs_rgn.CreateRectRgnIndirect(CRect());

		ForEachChild(wnd->GetSafeHwnd(), [&](HWND child) {
			CRect rc;
			::GetWindowRect(child, &rc);
			wnd->ScreenToClient(rc);

			if (!relative_to_client)
				rc.OffsetRect(cr.TopLeft());

			CRgn rgn;
			rgn.CreateRectRgnIndirect(&rc);
			rgn.CombineRgn(&rgn, &wrgn, RGN_AND);
			childs_rgn.CombineRgn(&childs_rgn, &rgn, RGN_OR);

			return true;
		});

		wrgn.CombineRgn(&wrgn, &childs_rgn, RGN_DIFF);
	}
}

CDialog* FindParentDialog(CWnd* wnd, bool only_WS_POPUP /*= true*/)
{
	if (wnd == nullptr)
		return nullptr;

	auto is_dialog = [only_WS_POPUP](CWnd* wnd) {
		if (wnd == nullptr)
			return false;

		if (only_WS_POPUP && WS_POPUP != (wnd->GetStyle() & WS_POPUP))
			return false;

		return TRUE == wnd->IsKindOf(RUNTIME_CLASS(CDialog));
	};

	CWnd* pParentWnd = wnd->GetTopLevelParent();
	if (is_dialog(pParentWnd))
		return STATIC_CAST(CDialog*, pParentWnd);

	pParentWnd = wnd->GetParent();
	while (pParentWnd != nullptr)
	{
		if (is_dialog(pParentWnd))
			return STATIC_CAST(CDialog*, pParentWnd);

		pParentWnd = pParentWnd->GetParent();
	}

	return nullptr;
}

void FillNonClientArea(CWnd* wnd, COLORREF color, const COLORREF* border /*= nullptr*/)
{
	if (wnd == nullptr)
		return;

	CWindowDC dc(wnd);
	if (dc.GetSafeHdc())
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*wnd);

		CBrush brush(color);
		CRect wr, cr;
		wnd->GetWindowRect(wr);
		wnd->GetClientRect(cr);
		wnd->ClientToScreen(cr);

		cr.OffsetRect(-wr.left, -wr.top);
		wr.OffsetRect(-wr.left, -wr.top);

		if (!border)
		{
			CRect border_rect;
			if (AdjustWindowRectEx(&border_rect, wnd->GetStyle(), FALSE, wnd->GetExStyle()))
			{
				wr.left -= border_rect.left;
				wr.top -= border_rect.top;
				wr.right -= border_rect.right;
				wr.bottom -= border_rect.bottom;
			}
		}

		CRgn wrgn, crgn, fill_rgn;
		wrgn.CreateRectRgnIndirect(&wr);
		crgn.CreateRectRgnIndirect(&cr);
		wrgn.CombineRgn(&wrgn, &crgn, RGN_DIFF);

		dc.FillRgn(&wrgn, &brush);

		if (border)
			FrameRectDPI(dc, wr, *border);
	}
}

void FillClientRect(CWnd* wnd, CDC* dc, COLORREF color)
{
	if (wnd == nullptr || dc == nullptr)
		return;

	CRect rRect;
	wnd->GetClientRect(&rRect);
	dc->FillSolidRect(rRect, color);
}

void FrameClientRect(CWnd* wnd, CDC* dc, COLORREF outline, COLORREF fill /*= CLR_INVALID*/)
{
	CRect rRect;
	wnd->GetClientRect(&rRect);

	if (fill != CLR_INVALID)
		dc->FillSolidRect(rRect, fill);

	CBrush brush(outline);
	dc->FrameRect(rRect, &brush);
}

void DrawNCBorderClientEdge(CWnd* wnd, COLORREF border, COLORREF bg, bool force_bg /*= false*/)
{
	if (wnd == nullptr)
		return;

	CWindowDC dc(wnd);
	if (dc.GetSafeHdc())
	{
		CBrush brush(border);
		CRect rcBorder;
		wnd->GetWindowRect(rcBorder);
		rcBorder.bottom = rcBorder.Height();
		rcBorder.right = rcBorder.Width();
		rcBorder.left = rcBorder.top = 0;
		dc.FrameRect(rcBorder, &brush);

		if (force_bg || wnd->GetExStyle() & WS_EX_CLIENTEDGE)
		{
			rcBorder.DeflateRect(1, 1);

			CBrush brushBG(bg);
			dc.FrameRect(rcBorder, &brushBG);
		}
	}
}

void DrawSymbol(HDC dc, LPRECT rect, COLORREF color, Symbol symb /*= Symb_Check*/, float pen_width /*= 0.0f*/)
{
	GDIPlusManager::EnsureGDIPlus();

	Gdiplus::RectF g_rect((Gdiplus::REAL)rect->left, (Gdiplus::REAL)rect->top,
	                      (Gdiplus::REAL)(rect->right - rect->left), (Gdiplus::REAL)(rect->bottom - rect->top));

	Gdiplus::Color g_color;
	g_color.SetFromCOLORREF(color);

	if (pen_width == 0.0f)
		pen_width = g_rect.Width * 0.15f;

	float min_width = float(VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX() * 1.5);
	if (pen_width < min_width)
		pen_width = min_width;

	Gdiplus::Graphics gr(dc);
	gr.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

	switch (symb)
	{
	case Symb_Check: {
		g_rect.Width -= 1.0;
		g_rect.Height -= 1.0;

		g_rect.Inflate(-pen_width / 2.0f, -pen_width / 2.0f);

		Gdiplus::Pen g_pen(g_color, pen_width);
		Gdiplus::PointF pts[3];

		pts[0].X = g_rect.X + 0.15f * g_rect.Width;
		pts[0].Y = g_rect.Y + 0.45f * g_rect.Height;
		pts[1].X = g_rect.X + 0.48f * g_rect.Width;
		pts[1].Y = g_rect.Y + 0.75f * g_rect.Height;
		pts[2].X = g_rect.X + 0.85f * g_rect.Width;
		pts[2].Y = g_rect.Y + 0.15f * g_rect.Height;
		gr.DrawLines(&g_pen, pts, 3);
		break;
	}

	case Symb_Cross: {
		Gdiplus::Pen g_pen(g_color, pen_width);
		Gdiplus::PointF pts[2];

		g_rect.Width -= 1.0;
		g_rect.Height -= 1.0;

		float w_offset = 0.2f * g_rect.Width;
		float h_offset = 0.2f * g_rect.Height;

		pts[0].X = g_rect.X + w_offset;
		pts[0].Y = g_rect.Y + h_offset;
		pts[1].X = g_rect.X + g_rect.Width - w_offset;
		pts[1].Y = g_rect.Y + g_rect.Height - h_offset;
		gr.DrawLine(&g_pen, pts[0], pts[1]);
		pts[0].X = g_rect.X + w_offset;
		pts[0].Y = g_rect.Y + g_rect.Height - h_offset;
		pts[1].X = g_rect.X + g_rect.Width - w_offset;
		pts[1].Y = g_rect.Y + h_offset;
		gr.DrawLine(&g_pen, pts[0], pts[1]);
		break;
	}

	case Symb_Rect: // fall-through
	case Symb_Ellipse: {
		Gdiplus::SolidBrush g_brush(g_color);
		Gdiplus::PointF pts[2];
		g_rect.Width -= 1.0;
		g_rect.Height -= 1.0;
		float w_offset = 0.2f * g_rect.Width;
		float h_offset = 0.2f * g_rect.Height;
		pts[0].X = g_rect.X + w_offset;
		pts[0].Y = g_rect.Y + h_offset;
		pts[1].X = g_rect.X + g_rect.Width - w_offset;
		pts[1].Y = g_rect.Y + g_rect.Height - h_offset;
		if (symb == Symb_Rect)
		{
			gr.SetSmoothingMode(Gdiplus::SmoothingModeNone);
			gr.FillRectangle(&g_brush, pts[0].X, pts[0].Y, pts[1].X - pts[0].X, pts[1].Y - pts[0].Y);
		}
		else
			gr.FillEllipse(&g_brush, pts[0].X, pts[0].Y, pts[1].X - pts[0].X, pts[1].Y - pts[0].Y);
		break;
	}

	default:
		break;
	}
}

void GetRoundRectPath(Gdiplus::GraphicsPath* pPath, const Gdiplus::Rect& r, int dia)
{
	// diameter can't exceed width or height
	if (dia > r.Width)
		dia = r.Width;
	if (dia > r.Height)
		dia = r.Height;

	// define a corner
	Gdiplus::Rect Corner(r.X, r.Y, dia, dia);

	// begin path
	pPath->Reset();

	// top left
	pPath->AddArc(Corner, 180, 90);

	// top right
	Corner.X += (r.Width - dia - 1);
	pPath->AddArc(Corner, 270, 90);

	// bottom right
	Corner.Y += (r.Height - dia - 1);
	pPath->AddArc(Corner, 0, 90);

	// bottom left
	Corner.X -= (r.Width - dia - 1);
	pPath->AddArc(Corner, 90, 90);

	// end path
	pPath->CloseFigure();
}

void DrawRoundedRect(HDC dc, LPRECT rect, COLORREF color, int radius)
{
	GDIPlusManager::EnsureGDIPlus();

	Gdiplus::Rect g_rect(rect->left, rect->top, (rect->right - rect->left), (rect->bottom - rect->top));

	Gdiplus::GraphicsPath gp;
	GetRoundRectPath(&gp, g_rect, radius * 2);

	Gdiplus::Color g_color;
	g_color.SetFromCOLORREF(color);

	Gdiplus::Pen g_pen(g_color);
	g_pen.SetAlignment(Gdiplus::PenAlignmentCenter);

	Gdiplus::Graphics gr(dc);
	gr.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	gr.DrawPath(&g_pen, &gp);
}

void FillRectAlpha(HDC dc, LPCRECT rect, COLORREF rgb, BYTE alpha)
{
	GDIPlusManager::EnsureGDIPlus();

	Gdiplus::Rect g_rect(rect->left, rect->top, (rect->right - rect->left), (rect->bottom - rect->top));

	Gdiplus::Color g_color(Gdiplus::Color::MakeARGB(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb)));
	Gdiplus::SolidBrush g_brush(g_color);
	Gdiplus::Graphics gr(dc);
	gr.FillRectangle(&g_brush, g_rect);
}

bool DrawImageGray(HDC dc, CImageList* list, int index, const POINT& pnt, float opacity /*= 1.0f*/,
                   float offset /*= 0.0f*/)
{
	GDIPlusManager::EnsureGDIPlus();

	int srcW, srcH;
	if (!ImageList_GetIconSize(list->m_hImageList, &srcW, &srcH))
		return false;

	CBitmap bmp;
	VsUI::GdiplusImage img;
	img.Create(srcW, srcH, PixelFormat32bppARGB);
	bmp.Attach(img.Detach());

	{
		CDC cdc;
		cdc.CreateCompatibleDC(nullptr);
		cdc.SelectObject(bmp);
		list->Draw(&cdc, index, {0, 0}, ILD_TRANSPARENT);
	}

	img.Attach(bmp);

	Gdiplus::ImageAttributes img_attrs;

	// clang-format off
	Gdiplus::ColorMatrix colorMatrix = 
	{{
		{0.299f, 0.299f, 0.299f, 0.0f,	0.0f},
		{0.587f, 0.587f, 0.587f, 0.0f,	0.0f},
		{0.114f, 0.114f, 0.114f, 0.0f,	0.0f},
		{0.0f,   0.0f,   0.0f,	opacity, 0.0f},
		{offset,	offset,	offset,	0.0f,   1.0f}
	}};
	// clang-format on

	if (Gdiplus::Ok !=
	    img_attrs.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap))
	{
		return false;
	}

	// 	Gdiplus::Color transparent;
	// 	img.GetPixel(0, 0, &transparent);
	// 	img_attrs.SetColorKey(transparent, transparent, Gdiplus::ColorAdjustTypeBitmap);

	Gdiplus::Graphics gr(dc);
	return Gdiplus::Ok ==
	       gr.DrawImage(img,
	                    Gdiplus::Rect(pnt.x, pnt.y, (int)img.GetWidth(), (int)img.GetHeight()), // destination rectangle
	                    0, 0,                 // upper-left corner of source rectangle
	                    (int)img.GetWidth(),  // width of source rectangle
	                    (int)img.GetHeight(), // height of source rectangle
	                    Gdiplus::UnitPixel, &img_attrs);
}

bool DrawImage(HDC dc, CBitmap& bmp, const POINT& pnt)
{
	GDIPlusManager::EnsureGDIPlus();

	VsUI::GdiplusImage img;
	img.Attach(bmp);

	Gdiplus::Graphics gr(dc);
	return Gdiplus::Ok ==
	       gr.DrawImage(img,
	                    Gdiplus::Rect(pnt.x, pnt.y, (int)img.GetWidth(), (int)img.GetHeight()), // destination rectangle
	                    0, 0,                 // upper-left corner of source rectangle
	                    (int)img.GetWidth(),  // width of source rectangle
	                    (int)img.GetHeight(), // height of source rectangle
	                    Gdiplus::UnitPixel);
}

bool DrawImageThemedForBackground(HDC dc, CImageList* list, int index, const POINT& pnt, COLORREF bgClr)
{
	if (gPkgServiceProvider && Psettings && Psettings->mEnableIconTheme)
	{
		CComPtr<IVsImageService2> imageService;

#ifdef VA_CPPUNIT
		if (false)
#else
		if (SUCCEEDED(
		        gPkgServiceProvider->QueryService(SID_SVsImageService, IID_IVsImageService2, (void**)&imageService)) &&
		    imageService)
#endif
		{
			GDIPlusManager::EnsureGDIPlus();

			int srcW, srcH;
			if (!ImageList_GetIconSize(list->m_hImageList, &srcW, &srcH))
				return false;

			// see remarks: https://docs.microsoft.com/en-us/windows/win32/api/gdiplusheaders/nf-gdiplusheaders-bitmap-fromhbitmap
			// that means, "CBitmap bmp" should stay alive longer than "VsUI::GdiplusImage img"
			// it should not be problem as we don't use it out of scope, but for safety...
			CBitmap bmp; 

			// create new 32bpp bitmap
			VsUI::GdiplusImage img;
			img.Create(srcW, srcH, PixelFormat32bppARGB);
			bmp.Attach(img.Detach()); // attach new 32bpp bitmap

			{
				// draw image from imagelist to new bitmap
				CDC cdc;
				cdc.CreateCompatibleDC(nullptr);
				cdc.SelectObject(bmp);
				list->Draw(&cdc, index, {0, 0}, ILD_TRANSPARENT);
			}

			img.Attach(bmp); // attach GDI bitmap
			auto pBmp = img.GetBitmap();

			Gdiplus::Rect rect(0, 0, (INT)pBmp->GetWidth(), (INT)pBmp->GetHeight());
			Gdiplus::BitmapData bitmapData = {0};
			if (Gdiplus::Status::Ok ==
			    pBmp->LockBits(&rect,
			                   Gdiplus::ImageLockMode::ImageLockModeRead | Gdiplus::ImageLockMode::ImageLockModeWrite,
			                   PixelFormat32bppARGB, &bitmapData))
			{
				INT abs_stride = std::abs(bitmapData.Stride);
				INT size = abs_stride * (INT)bitmapData.Height;

				auto buffer = std::make_unique<BYTE[]>((size_t)size);

				// copy pixels from bitmap to buffer row by row
				// as stride can be negative and go from bottom to top
				BYTE* pRowIn = reinterpret_cast<BYTE*>(bitmapData.Scan0);
				BYTE* pRowOut = buffer.get();
				for (UINT y = 0; y < bitmapData.Height; y++, pRowIn += bitmapData.Stride, pRowOut += abs_stride)
				{
					// copy row pixels
					memcpy(pRowOut, pRowIn, (size_t)abs_stride);
				}

				// theme pixels
				// we need to have pixels in top-to-bottom order otherwise ThemeDIBits
				// causes access violation exception regardless of isTopDownBitmap setting, probably bug
				VARIANT_BOOL isThemed = VARIANT_FALSE;
				imageService->ThemeDIBits(size, buffer.get(), (INT)bitmapData.Width, (INT)bitmapData.Height,
				                          VARIANT_TRUE, bgClr, &isThemed);

				// copy themed pixels back to bitmap row by row
				// as stride can be negative and go from bottom to top
				pRowIn = reinterpret_cast<BYTE*>(bitmapData.Scan0);
				pRowOut = buffer.get();
				for (UINT y = 0; y < bitmapData.Height; y++, pRowIn += bitmapData.Stride, pRowOut += abs_stride)
				{
					// copy row pixels
					memcpy(pRowIn, pRowOut, (size_t)abs_stride);
				}

				pBmp->UnlockBits(&bitmapData);
			}

			if (img.IsLoaded())
			{
				CAutoPtr<Gdiplus::Graphics> gr(Gdiplus::Graphics::FromHDC(dc));
				auto status = gr->DrawImage((Gdiplus::Image*)img.GetBitmap(), (INT)pnt.x, (INT)pnt.y);
				return Gdiplus::Status::Ok == status;
			}
		}
	}

	return false;
}

bool DrawImageHighliteTheme17(HDC dc, CImageList* list, int index, const POINT& pnt, COLORREF color)
{
	GDIPlusManager::EnsureGDIPlus();

	int srcW, srcH;
	if (!ImageList_GetIconSize(list->m_hImageList, &srcW, &srcH))
		return false;

	VsUI::GdiplusImage img;
	img.Create(srcW, srcH, PixelFormat32bppARGB);

	CBitmap bmp;
	bmp.Attach(img.Detach());
	img.Attach(bmp);

	{
		CDC cdc;
		cdc.CreateCompatibleDC(nullptr);
		cdc.SelectObject(bmp);
		list->Draw(&cdc, index, {0, 0}, ILD_TRANSPARENT);
	}

	auto px_func = 
		g_IdeSettings->IsDarkVSColorTheme() ?
		[](COLORREF px, COLORREF color) {
		auto bw = (GetRValue(px) * 2 + GetGValue(px) * 3 + GetBValue(px)) / 6;

		if (bw < 65)
		{
			auto r = (bw * GetRValue(color)) / 255;
			auto g = (bw * GetGValue(color)) / 255;
			auto b = (bw * GetBValue(color)) / 255;
			return RGB(r, g, b);
		}

		return color;
	}
	:
		[](COLORREF px, COLORREF color) {
		auto bw = (GetRValue(px) * 2 + GetGValue(px) * 3 + GetBValue(px)) / 6;

		if (bw > 190)
		{
			auto r = 255 - (bw * GetRValue(color)) / 255;
			auto g = 255 - (bw * GetGValue(color)) / 255;
			auto b = 255 - (bw * GetBValue(color)) / 255;
			return RGB(r, g, b);
		}

		return color;
	};

	Gdiplus::Color* px = nullptr;
	COLORREF dcPx;
	img.ProcessBitmapBitsXY(img.GetBitmap(), [&](Gdiplus::ARGB* pPixelData, UINT x, UINT y) {
		px = reinterpret_cast<Gdiplus::Color*>(pPixelData);
		if (px->GetAlpha())
		{
			dcPx = GetPixel(dc, pnt.x + (LONG)x, pnt.y + (LONG)y);
			SetPixel(dc, pnt.x + (LONG)x, pnt.y + (LONG)y,
			         InterpolateColor(dcPx, px_func(px->ToCOLORREF(), color), px->GetAlpha() / 255.0f));
		}
	});

	return true;
}

bool DrawImageColorMatrix(HDC dc, CImageList* list, int index, const POINT& pnt, float matrix[5][5])
{
	GDIPlusManager::EnsureGDIPlus();

	HICON hIcon = list->ExtractIcon(index);

	if (hIcon == nullptr)
		return false;

	AutoFnc m_auto([&]() {
		if (hIcon)
			DestroyIcon(hIcon);
		hIcon = nullptr;
	});

	Gdiplus::Bitmap bmp(hIcon);

	Gdiplus::ImageAttributes img_attrs;

	if (Gdiplus::Ok != img_attrs.SetColorMatrix((Gdiplus::ColorMatrix*)matrix, Gdiplus::ColorMatrixFlagsDefault,
	                                            Gdiplus::ColorAdjustTypeBitmap))
	{
		return false;
	}

	Gdiplus::Color transparent;
	bmp.GetPixel(0, 0, &transparent);
	img_attrs.SetColorKey(transparent, transparent, Gdiplus::ColorAdjustTypeBitmap);

	Gdiplus::Graphics gr(dc);
	return Gdiplus::Ok ==
	       gr.DrawImage(&bmp,
	                    Gdiplus::Rect(pnt.x, pnt.y, (int)bmp.GetWidth(), (int)bmp.GetHeight()), // destination rectangle
	                    0, 0,                 // upper-left corner of source rectangle
	                    (int)bmp.GetWidth(),  // width of source rectangle
	                    (int)bmp.GetHeight(), // height of source rectangle
	                    Gdiplus::UnitPixel, &img_attrs);
}

bool DrawImageOpacity(HDC dc, CImageList* list, int index, const POINT& pnt, float opacity /*= 1.0f*/)
{
	float matrix[5][5] = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, opacity, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};

	return DrawImageColorMatrix(dc, list, index, pnt, matrix);
}

void DrawThemedCheckBox(CDC& dc, CRect& rect, Symbol symb, bool enabled)
{
	COLORREF crBackImg = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);
	COLORREF crActive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE);
	COLORREF crInactive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_INACTIVE);

	COLORREF crBox = InterpolateColor(crInactive, crActive, 0.5);

	if (0 == crBox && crActive)
		crBox = crActive;
	else if (crBox > 0xffffff)
		crBox = crActive;

	if (!enabled)
	{
		crBox = InterpolateColor(crBackImg, crBox, 0.5);
		crActive = InterpolateColor(crBackImg, crActive, 0.5);
	}

	DrawCheckBoxEx(dc, rect, symb, crBox, crBackImg, crActive);
}

void DrawTreeExpandGlyph(CDC& dc, CRect& buttonrect, COLORREF bordercolor, COLORREF fillcolor, bool expanded)
{
	GDIPlusManager::EnsureGDIPlus();

	Gdiplus::Graphics gr(dc);

	Gdiplus::Color innerColor;
	innerColor.SetFromCOLORREF(fillcolor);
	Gdiplus::SolidBrush innerBrush(innerColor);

	Gdiplus::Color outerColor;
	outerColor.SetFromCOLORREF(bordercolor);
	Gdiplus::SolidBrush outerBrush(outerColor);

	Gdiplus::PointF pts[] = {{}, {}, {}, {}};

	if (expanded)
	{
		int dimX = VsUI::DpiHelper::LogicalToDeviceUnitsX(7);
		int dimY = VsUI::DpiHelper::LogicalToDeviceUnitsY(7);

		pts[0].X = (float)dimX;
		pts[0].Y = 0; // right top (45�)
		pts[1].X = (float)dimX;
		pts[1].Y = (float)dimY; // right bottom (90�)
		pts[2].X = 0;
		pts[2].Y = (float)dimY; // left bottom (45�)
		pts[3].X = (float)dimX;
		pts[3].Y = 0; // closed at right top

		// translate points to fit into button
		for (int i = 0; i < countof(pts); i++)
		{
			pts[i].X += Gdiplus::REAL(buttonrect.left + (buttonrect.Width() - dimX) / 2);
			pts[i].Y += Gdiplus::REAL(buttonrect.top + (buttonrect.Height() - dimY) / 2);
		}
	}
	else
	{
		int dimX = VsUI::DpiHelper::LogicalToDeviceUnitsX(5);
		int dimY = VsUI::DpiHelper::LogicalToDeviceUnitsY(10);

		pts[0].X = 0;
		pts[0].Y = 0; // left top (45�)
		pts[1].X = 0;
		pts[1].Y = (float)dimY; // left bottom (45�)
		pts[2].X = (float)dimX;
		pts[2].Y = (float)dimY / 2.0f; // right middle (90�)
		pts[3].X = 0;
		pts[3].Y = 0; // closed at left top

		// translate points to fit into button
		for (int i = 0; i < countof(pts); i++)
		{
			pts[i].X += Gdiplus::REAL(buttonrect.left + (buttonrect.Width() - dimX) / 2);
			pts[i].Y += Gdiplus::REAL(buttonrect.top + (buttonrect.Height() - dimY) / 2);
		}
	}

	// setup graphics
	gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); // antialiasing
	gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);  // lock to pixels

	// ***********************
	// fill the outer triangle
	gr.FillPolygon(&outerBrush, pts, countof(pts));

	// *********************************************************
	// calculate inner offsets for miter from the border weights

	float XWeight = (float)VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX();
	float YWeight = (float)VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorY();

	// calculate miters
	float XMiter45 = XWeight / 0.4142f; // 0.4142 = tan(45� / 2)
	float YMiter45 = YWeight / 0.4142f; // 0.4142 = tan(45� / 2)
	float XMiter90 = XWeight / 0.7071f; // 0.7071 = sin(90� / 2)

	// *************************
	// offset points of triangle
	if (expanded)
	{
		// top right
		pts[0].X -= XWeight;
		pts[0].Y += YMiter45;

		// bottom right
		pts[1].X -= XWeight;
		pts[1].Y -= YWeight;

		// bottom left
		pts[2].X += XMiter45;
		pts[2].Y -= YWeight;

		// top right
		pts[3] = pts[0];
	}
	else
	{
		// top left
		pts[0].X += XWeight;
		pts[0].Y += YMiter45;

		// bottom left
		pts[1].X += XWeight;
		pts[1].Y -= YMiter45;

		// mid right
		pts[2].X -= XMiter90;

		// top left
		pts[3] = pts[0];
	}

	// ***********************
	// fill the inner triangle
	gr.FillPolygon(&innerBrush, pts, countof(pts));
}

void DrawCheckBoxEx(CDC& dc, CRect& rect, Symbol symb, COLORREF border, COLORREF bg, COLORREF check)
{
	dc.FillSolidRect(rect, bg);

	CBrush brBox(border);
	dc.FrameRect(rect, &brBox);

	if (symb)
		DrawSymbol(dc, rect, check, symb);
}

BYTE ColorBrightness(COLORREF rgb)
{
	UINT r = GetRValue(rgb);
	UINT g = GetGValue(rgb);
	UINT b = GetBValue(rgb);

	return (BYTE)sqrt((double)(r * r) * .241 + (double)(g * g) * .691 + (double)(b * b) * .068);
}

BYTE ColorRGBAverage(COLORREF rgb)
{
	return ((UINT)GetRValue(rgb) + (UINT)GetGValue(rgb) + (UINT)GetBValue(rgb)) / 3;
}

// This is a subfunction of HSLtoRGB
static void HSLtoRGB_Subfunction(UINT& c, const float& temp1, const float& temp2, const float& temp3)
{
	if ((temp3 * 6) < 1)
		c = (UINT)((temp2 + (temp1 - temp2) * 6 * temp3) * 100);
	else if ((temp3 * 2) < 1)
		c = (UINT)(temp1 * 100);
	else if ((temp3 * 3) < 2)
		c = (UINT)((temp2 + (temp1 - temp2) * (.66666 - temp3) * 6) * 100);
	else
		c = (UINT)(temp2 * 100);
	return;
}

// This function extracts the hue, saturation, and luminance from "color"
// and places these values in h, s, and l respectively.
void RGBtoHSL(COLORREF color, UINT& h, UINT& s, UINT& l)
{
	UINT r = (UINT)GetRValue(color);
	UINT g = (UINT)GetGValue(color);
	UINT b = (UINT)GetBValue(color);

	float r_percent = ((float)r) / 255;
	float g_percent = ((float)g) / 255;
	float b_percent = ((float)b) / 255;

	float max_color = 0;
	if ((r_percent >= g_percent) && (r_percent >= b_percent))
	{
		max_color = r_percent;
	}
	if ((g_percent >= r_percent) && (g_percent >= b_percent))
		max_color = g_percent;
	if ((b_percent >= r_percent) && (b_percent >= g_percent))
		max_color = b_percent;

	float min_color = 0;
	if ((r_percent <= g_percent) && (r_percent <= b_percent))
		min_color = r_percent;
	if ((g_percent <= r_percent) && (g_percent <= b_percent))
		min_color = g_percent;
	if ((b_percent <= r_percent) && (b_percent <= g_percent))
		min_color = b_percent;

	float L = 0;
	float S = 0;
	float H = 0;

	L = (max_color + min_color) / 2;

	if (max_color == min_color)
	{
		S = 0;
		H = 0;
	}
	else
	{
		if (L < .50)
		{
			S = (max_color - min_color) / (max_color + min_color);
		}
		else
		{
			S = (max_color - min_color) / (2 - max_color - min_color);
		}
		if (max_color == r_percent)
		{
			H = (g_percent - b_percent) / (max_color - min_color);
		}
		if (max_color == g_percent)
		{
			H = 2 + (b_percent - r_percent) / (max_color - min_color);
		}
		if (max_color == b_percent)
		{
			H = 4 + (r_percent - g_percent) / (max_color - min_color);
		}
	}
	s = (UINT)(S * 100);
	l = (UINT)(L * 100);
	H = H * 60;
	if (H < 0)
		H += 360;
	h = (UINT)H;
}

// This function converts the "color" object to the equivalent RGB values of
// the hue, saturation, and luminance passed as h, s, and l respectively
COLORREF HSLtoRGB(const UINT& h, const UINT& s, const UINT& l)
{
	UINT r = 0;
	UINT g = 0;
	UINT b = 0;

	float L = ((float)l) / 100;
	float S = ((float)s) / 100;
	float H = ((float)h) / 360;

	if (s == 0)
	{
		r = l;
		g = l;
		b = l;
	}
	else
	{
		float temp1 = 0;
		if (L < .50)
		{
			temp1 = L * (1 + S);
		}
		else
		{
			temp1 = L + S - (L * S);
		}

		float temp2 = 2 * L - temp1;

		float temp3 = 0;
		for (int i = 0; i < 3; i++)
		{
			switch (i)
			{
			case 0: // red
			{
				temp3 = H + .33333f;
				if (temp3 > 1)
					temp3 -= 1;
				HSLtoRGB_Subfunction(r, temp1, temp2, temp3);
				break;
			}
			case 1: // green
			{
				temp3 = H;
				HSLtoRGB_Subfunction(g, temp1, temp2, temp3);
				break;
			}
			case 2: // blue
			{
				temp3 = H - .33333f;
				if (temp3 < 0)
					temp3 += 1;
				HSLtoRGB_Subfunction(b, temp1, temp2, temp3);
				break;
			}
			default: {
			}
			}
		}
	}
	r = (UINT)((((float)r) / 100) * 255);
	g = (UINT)((((float)g) / 100) * 255);
	b = (UINT)((((float)b) / 100) * 255);
	return RGB(r, g, b);
}

COLORREF BrightenColor(COLORREF color, UINT amount)
{
	UINT h, s, l;

	RGBtoHSL(color, h, s, l);
	l += amount;
	if (l > 100)
	{
		l = 100;
	}
	return HSLtoRGB(h, s, l);
}

COLORREF DarkenColor(COLORREF color, UINT amount)
{
	UINT h, s, l;

	RGBtoHSL(color, h, s, l);
	if (amount >= l)
	{
		l = 0;
	}
	else
	{
		l -= amount;
	}
	return HSLtoRGB(h, s, l);
}

void DrawPolygon(CDC& dc, const POINT* points, int num_points, COLORREF fill, COLORREF border,
                 int pen_style /*= PS_SOLID*/, int pen_width /*= 0*/)
{
	CPen pen(pen_style, pen_width, border);
	CBrush brush(fill);
	CPen* old_pen = dc.SelectObject(&pen);
	CBrush* old_brush = dc.SelectObject(&brush);
	dc.Polygon(points, num_points);
	dc.SelectObject(old_brush);
	dc.SelectObject(old_pen);
}

CBrushMap GDIBuffer_brushes;
CBrush& GDIBuffer_GetBrush(COLORREF color)
{
	return GDIBuffer_brushes.GetBrush(color);
}

CPenMap GDIBuffer_pens;
CPen& GDIBuffer_GetPen(COLORREF color, int style /*= PS_SOLID*/, int width /*= 0*/)
{
	return GDIBuffer_pens.GetPen(color, style, width);
}

void GDIBuffer_Clear(bool brushes /*= true*/, bool pens /*= true*/)
{
	if (brushes)
		GDIBuffer_brushes.Clear();

	if (pens)
		GDIBuffer_pens.Clear();
}

CBrushMap& GDIBuffer_BrushMap()
{
	return GDIBuffer_brushes;
}

CPenMap& GDIBuffer_PenMap()
{
	return GDIBuffer_pens;
}

void DelayedRefresh(CWnd* wnd, UINT ms_duration)
{
	struct timer
	{
		static void __stdcall TimerProc(HWND hWnd,         // handle of CWnd that called SetTimer
		                                UINT nMsg,         // WM_TIMER
		                                UINT_PTR nIDEvent, // timer identification
		                                DWORD dwTime       // system time
		)
		{
			::KillTimer(hWnd, nIDEvent);
			::RedrawWindow(hWnd, NULL, NULL,
			               RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
		}

		static void start(HWND hWnd, UINT ms_duration)
		{
			::SetTimer(hWnd, 'RWND', ms_duration, &timer::TimerProc);
		}
	};

	if (wnd)
		timer::start(wnd->GetSafeHwnd(), ms_duration);
}

void DelayedInvoke(bool mainThread, std::function<int()> waitFnc, std::function<void()> workFnc, const char* threadName /*= "VA_DelayedInvoke"*/)
{
	try
	{
		new LambdaThread([=]() {
			int wait_ms = waitFnc();
			int latest_yield = 0;
			while (wait_ms)
			{
				if (latest_yield >= 100)
				{
					std::this_thread::yield(); // allow other threads get scheduled
					latest_yield = 0;         // we just yielded
				}

				if (wait_ms > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
					latest_yield += wait_ms;
				}
				else
				{
					// negative value forces yield after waiting
					std::this_thread::sleep_for(std::chrono::milliseconds(-wait_ms));
					std::this_thread::yield(); // allow other threads get scheduled
					latest_yield = 0;         // we just yielded
				}

				wait_ms = waitFnc();
			}

			if (mainThread)
			{
				return ::RunFromMainThread(workFnc, true);
			}

			return workFnc();
		}, threadName, true);
	}
	catch (const std::exception&)
	{
		vLog("ERROR: DelayedInvoke exception caught");
	}
}

void TraceFramePrint(LPCTSTR lpszFormat /*, ...*/)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	//	WTString trace_str;
	//	va_list argList;
	//	va_start(argList, lpszFormat);
	//	trace_str.FormatV(lpszFormat, argList);
	//	va_end(argList);
	TraceHelp th(/*trace_str*/ lpszFormat);
}

void DrawFocusRect(HDC dc, LPRECT rect, COLORREF color, bool dashed /*= true*/)
{
	GDIPlusManager::EnsureGDIPlus();

	const int width = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
	const float widthf = (float)width;

	Gdiplus::Color focus_color(0xA0, GetRValue(color), GetGValue(color), GetBValue(color));
	Gdiplus::Pen border_pen(focus_color);

	if (dashed)
	{
#if defined(RAD_STUDIO)
		Gdiplus::REAL dashes[] = {.5f, .5f};
#else
		Gdiplus::REAL dashes[] = {2.0f * widthf, 1.0f * widthf};
#endif
		border_pen.SetDashPattern(dashes, sizeof(dashes) / sizeof(dashes[0]));
	}
	border_pen.SetWidth(widthf);

	Gdiplus::Graphics gr(dc);
	gr.SetSmoothingMode(Gdiplus::SmoothingModeNone);

	CRect focusRect = rect;

	Gdiplus::Point pts[] = {{focusRect.left, focusRect.top},
	                        {focusRect.left + focusRect.Width() - width, focusRect.top},
	                        {focusRect.left + focusRect.Width() - width, focusRect.top + focusRect.Height() - width},
	                        {focusRect.left, focusRect.top + focusRect.Height() - width},
	                        {focusRect.left, focusRect.top}};
	gr.DrawLines(&border_pen, pts, 5);
}

LRESULT CenteredEditNCCalcSize(CEdit* edit, WPARAM wParam, LPARAM lParam, LPRECT offset /*= nullptr*/)
{
	if (edit)
	{
		int v_offset = 0;
		CRect border_rect;

		if (0 == (edit->GetStyle() & ES_MULTILINE))
		{
			CFont* pFont = edit->GetFont();
			if (!pFont)
				return 0;

			CRect rectText;
			rectText.SetRectEmpty();

			CDC* pDC = edit->GetDC();
			if (!pDC)
				return 0;

			CFont* pOld = pDC->SelectObject(pFont);
			pDC->DrawText("Hp", rectText, DT_CALCRECT | DT_LEFT);
			//			UINT uiVClientHeight = rectText.Height();

			pDC->SelectObject(pOld);
			edit->ReleaseDC(pDC);

			CRect rectWnd;
			edit->GetWindowRect(rectWnd);

			v_offset = (rectWnd.Height() - rectText.Height()) / 2;

			AdjustWindowRectEx(&border_rect, edit->GetStyle(), FALSE, edit->GetExStyle());
		}

		UINT right_offset = (edit->GetStyle() & WS_VSCROLL) ? GetSystemMetrics(SM_CXVSCROLL) : 0u;
		UINT bottom_offset = (edit->GetStyle() & WS_HSCROLL) ? GetSystemMetrics(SM_CYHSCROLL) : 0u;

		if (wParam == 0)
		{
			LPRECT rect = (LPRECT)lParam;
			rect->top += v_offset;
			rect->bottom -= v_offset;
			rect->right -= right_offset;
			rect->bottom -= bottom_offset;
			rect->left -= border_rect.left;
			rect->right -= border_rect.right;

			if (offset)
			{
				rect->right += offset->right;
				rect->bottom += offset->bottom;
				rect->left += offset->left;
				rect->top += offset->top;
			}

			return 0;
		}
		else if (wParam == 1)
		{
			LPNCCALCSIZE_PARAMS pncc = (LPNCCALCSIZE_PARAMS)lParam;
			pncc->rgrc[0].left = pncc->lppos->x;
			pncc->rgrc[0].right = pncc->lppos->x + pncc->lppos->cx;
			pncc->rgrc[0].top = pncc->lppos->y + v_offset;
			pncc->rgrc[0].bottom = pncc->lppos->y + pncc->lppos->cy - v_offset;
			pncc->rgrc[0].left -= border_rect.left;
			pncc->rgrc[0].right -= border_rect.right;

			pncc->rgrc[2] = pncc->rgrc[1] = pncc->rgrc[0];

			pncc->rgrc[0].right -= right_offset;
			pncc->rgrc[0].bottom -= bottom_offset;

			if (offset)
			{
				pncc->rgrc[0].right += offset->right;
				pncc->rgrc[0].bottom += offset->bottom;
				pncc->rgrc[0].left += offset->left;
				pncc->rgrc[0].top += offset->top;
			}

			return WVR_VALIDRECTS;
		}
	}

	return 1;
}

LRESULT CenteredEditNCHitTest(CEdit* edit, WPARAM wParam, LPARAM lParam)
{
	if (edit)
	{
		POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

		CRect rect;
		edit->GetWindowRect(&rect);

		if (rect.PtInRect(pt))
		{
			edit->GetClientRect(&rect);
			edit->ClientToScreen(&rect);

			if (rect.PtInRect(pt))
				return HTCLIENT;
			else
				return HTBORDER;
		}
	}

	return HTNOWHERE;
}

CStringA GetWindowMessageString(UINT msg, WPARAM wParam, LPARAM lParam)
{
	CStringA str;
	CString__FormatA(str, "%s: { WP: %#zx { HW: %u, LW: %u }, LP: %#zx { HW: %u, LW: %u } }\n",
	                 (LPCSTR)GetWindowMessageName(msg), (uintptr_t)wParam, HIWORD(wParam), LOWORD(wParam),
	                 (uintptr_t)lParam, HIWORD(lParam), LOWORD(lParam));
	return str;
}

bool GetTooltipMessageName(UINT msg, CStringA& str);

CStringA GetTooltipMessageString(UINT msg, WPARAM wParam, LPARAM lParam)
{
	CStringA msgName;

	if (!GetTooltipMessageName(msg, msgName))
		msgName = GetWindowMessageName(msg);

	CStringA str;
	CString__FormatA(str, "%s: { WP: %#zx { HW: %u, LW: %u }, LP: %#zx { HW: %u, LW: %u } }\n", (LPCSTR)msgName,
	                 (uintptr_t)wParam, HIWORD(wParam), LOWORD(wParam),
	                 (uintptr_t)lParam, HIWORD(lParam), LOWORD(lParam));
	return str;
}

bool GetListMessageName(UINT msg, CStringA& str);

CStringA GetListMessageString(UINT msg, WPARAM wParam, LPARAM lParam)
{
	CStringA msgName;

	if (!GetListMessageName(msg, msgName))
		msgName = GetWindowMessageName(msg);

	CStringA str;
	CString__FormatA(str, "%s: { WP: %#zx { HW: %u, LW: %u }, LP: %#zx { HW: %u, LW: %u } }\n", (LPCSTR)msgName,
	                 (uintptr_t)wParam, HIWORD(wParam), LOWORD(wParam),
	                 (uintptr_t)lParam, HIWORD(lParam), LOWORD(lParam));
	return str;
}

void TraceFrameDbgPrintMSG(UINT msg, WPARAM wParam, LPARAM lParam)
{
#ifdef DEBUG
	CStringA str = GetWindowMessageString(msg, wParam, lParam);
	TraceFramePrint(str);
#endif // DEBUG
}

#ifndef LVM_GETEMPTYTEXT
#define LVM_GETEMPTYTEXT (LVM_FIRST + 204)
#define LVM_GETFOOTERRECT (LVM_FIRST + 205)
#define LVM_GETFOOTERINFO (LVM_FIRST + 206)
#define LVM_GETFOOTERITEMRECT (LVM_FIRST + 207)
#define LVM_GETFOOTERITEM (LVM_FIRST + 208)
#define LVM_GETITEMINDEXRECT (LVM_FIRST + 209)
#define LVM_SETITEMINDEXSTATE (LVM_FIRST + 210)
#define LVM_GETNEXTITEMINDEX (LVM_FIRST + 211)
#endif

#ifndef LVM_SETTILEWIDTH
#define LVM_SETTILEWIDTH (LVM_FIRST + 141)
#endif

bool GetListMessageName(UINT msg, CStringA& str)
{
#define CASE_WM(wm) \
	case wm:        \
		str = #wm;  \
		return true

	switch (msg)
	{
	CASE_WM(LVM_APPROXIMATEVIEWRECT);
	CASE_WM(LVM_ARRANGE);
	CASE_WM(LVM_CANCELEDITLABEL);
	CASE_WM(LVM_CREATEDRAGIMAGE);
	CASE_WM(LVM_DELETEALLITEMS);
	CASE_WM(LVM_DELETECOLUMN);
	CASE_WM(LVM_DELETEITEM);
	CASE_WM(LVM_EDITLABELA);
	CASE_WM(LVM_EDITLABELW);
	CASE_WM(LVM_ENABLEGROUPVIEW);
	CASE_WM(LVM_ENSUREVISIBLE);
	CASE_WM(LVM_FINDITEMA);
	CASE_WM(LVM_FINDITEMW);
	CASE_WM(LVM_GETBKCOLOR);
	CASE_WM(LVM_GETBKIMAGEA);
	CASE_WM(LVM_GETBKIMAGEW);
	CASE_WM(LVM_GETCALLBACKMASK);
	CASE_WM(LVM_GETCOLUMNA);
	CASE_WM(LVM_GETCOLUMNW);
	CASE_WM(LVM_GETCOLUMNORDERARRAY);
	CASE_WM(LVM_GETCOLUMNWIDTH);
	CASE_WM(LVM_GETCOUNTPERPAGE);
	CASE_WM(LVM_GETEDITCONTROL);
	CASE_WM(LVM_GETEMPTYTEXT);
	CASE_WM(LVM_GETEXTENDEDLISTVIEWSTYLE);
	CASE_WM(LVM_GETFOCUSEDGROUP);
	CASE_WM(LVM_GETFOOTERINFO);
	CASE_WM(LVM_GETFOOTERITEM);
	CASE_WM(LVM_GETFOOTERITEMRECT);
	CASE_WM(LVM_GETFOOTERRECT);
	CASE_WM(LVM_GETGROUPCOUNT);
	CASE_WM(LVM_GETGROUPINFO);
	CASE_WM(LVM_GETGROUPINFOBYINDEX);
	CASE_WM(LVM_GETGROUPMETRICS);
	CASE_WM(LVM_GETGROUPRECT);
	CASE_WM(LVM_GETGROUPSTATE);
	CASE_WM(LVM_GETHEADER);
	CASE_WM(LVM_GETHOTCURSOR);
	CASE_WM(LVM_GETHOTITEM);
	CASE_WM(LVM_GETHOVERTIME);
	CASE_WM(LVM_GETIMAGELIST);
	CASE_WM(LVM_GETINSERTMARK);
	CASE_WM(LVM_GETINSERTMARKCOLOR);
	CASE_WM(LVM_GETINSERTMARKRECT);
	CASE_WM(LVM_GETISEARCHSTRINGA);
	CASE_WM(LVM_GETISEARCHSTRINGW);
	CASE_WM(LVM_GETITEMA);
	CASE_WM(LVM_GETITEMW);
	CASE_WM(LVM_GETITEMCOUNT);
	CASE_WM(LVM_GETITEMINDEXRECT);
	CASE_WM(LVM_GETITEMPOSITION);
	CASE_WM(LVM_GETITEMRECT);
	CASE_WM(LVM_GETITEMSPACING);
	CASE_WM(LVM_GETITEMSTATE);
	CASE_WM(LVM_GETITEMTEXTA);
	CASE_WM(LVM_GETITEMTEXTW);
	CASE_WM(LVM_GETNEXTITEM);
	CASE_WM(LVM_GETNEXTITEMINDEX);
	CASE_WM(LVM_GETNUMBEROFWORKAREAS);
	CASE_WM(LVM_GETORIGIN);
	CASE_WM(LVM_GETOUTLINECOLOR);
	CASE_WM(LVM_GETSELECTEDCOLUMN);
	CASE_WM(LVM_GETSELECTEDCOUNT);
	CASE_WM(LVM_GETSELECTIONMARK);
	CASE_WM(LVM_GETSTRINGWIDTHA);
	CASE_WM(LVM_GETSTRINGWIDTHW);
	CASE_WM(LVM_GETSUBITEMRECT);
	CASE_WM(LVM_GETTEXTBKCOLOR);
	CASE_WM(LVM_GETTEXTCOLOR);
	CASE_WM(LVM_GETTILEINFO);
	CASE_WM(LVM_GETTILEVIEWINFO);
	CASE_WM(LVM_GETTOOLTIPS);
	CASE_WM(LVM_GETTOPINDEX);
	CASE_WM(LVM_GETUNICODEFORMAT);
	CASE_WM(LVM_GETVIEW);
	CASE_WM(LVM_GETVIEWRECT);
	CASE_WM(LVM_GETWORKAREAS);
	CASE_WM(LVM_HASGROUP);
	CASE_WM(LVM_HITTEST);
	CASE_WM(LVM_INSERTCOLUMNA);
	CASE_WM(LVM_INSERTCOLUMNW);
	CASE_WM(LVM_INSERTGROUP);
	CASE_WM(LVM_INSERTGROUPSORTED);
	CASE_WM(LVM_INSERTITEMA);
	CASE_WM(LVM_INSERTITEMW);
	CASE_WM(LVM_INSERTMARKHITTEST);
	CASE_WM(LVM_ISGROUPVIEWENABLED);
	CASE_WM(LVM_ISITEMVISIBLE);
	CASE_WM(LVM_MAPIDTOINDEX);
	CASE_WM(LVM_MAPINDEXTOID);
	CASE_WM(LVM_MOVEGROUP);
	CASE_WM(LVM_MOVEITEMTOGROUP);
	CASE_WM(LVM_REDRAWITEMS);
	CASE_WM(LVM_REMOVEALLGROUPS);
	CASE_WM(LVM_REMOVEGROUP);
	CASE_WM(LVM_SCROLL);
	CASE_WM(LVM_SETBKCOLOR);
	CASE_WM(LVM_SETBKIMAGEA);
	CASE_WM(LVM_SETBKIMAGEW);
	CASE_WM(LVM_SETCALLBACKMASK);
	CASE_WM(LVM_SETCOLUMNA);
	CASE_WM(LVM_SETCOLUMNW);
	CASE_WM(LVM_SETCOLUMNORDERARRAY);
	CASE_WM(LVM_SETCOLUMNWIDTH);
	CASE_WM(LVM_SETEXTENDEDLISTVIEWSTYLE);
	CASE_WM(LVM_SETGROUPINFO);
	CASE_WM(LVM_SETGROUPMETRICS);
	CASE_WM(LVM_SETHOTCURSOR);
	CASE_WM(LVM_SETHOTITEM);
	CASE_WM(LVM_SETHOVERTIME);
	CASE_WM(LVM_SETICONSPACING);
	CASE_WM(LVM_SETIMAGELIST);
	CASE_WM(LVM_SETINFOTIP);
	CASE_WM(LVM_SETINSERTMARK);
	CASE_WM(LVM_SETINSERTMARKCOLOR);
	CASE_WM(LVM_SETITEMA);
	CASE_WM(LVM_SETITEMW);
	CASE_WM(LVM_SETITEMCOUNT);
	CASE_WM(LVM_SETITEMINDEXSTATE);
	CASE_WM(LVM_SETITEMPOSITION);
	CASE_WM(LVM_SETITEMPOSITION32);
	CASE_WM(LVM_SETITEMSTATE);
	CASE_WM(LVM_SETITEMTEXTA);
	CASE_WM(LVM_SETITEMTEXTW);
	CASE_WM(LVM_SETOUTLINECOLOR);
	CASE_WM(LVM_SETSELECTEDCOLUMN);
	CASE_WM(LVM_SETSELECTIONMARK);
	CASE_WM(LVM_SETTEXTBKCOLOR);
	CASE_WM(LVM_SETTEXTCOLOR);
	CASE_WM(LVM_SETTILEINFO);
	CASE_WM(LVM_SETTILEVIEWINFO);
	CASE_WM(LVM_SETTILEWIDTH);
	CASE_WM(LVM_SETTOOLTIPS);
	CASE_WM(LVM_SETUNICODEFORMAT);
	CASE_WM(LVM_SETVIEW);
	CASE_WM(LVM_SETWORKAREAS);
	CASE_WM(LVM_SORTGROUPS);
	CASE_WM(LVM_SORTITEMS);
	CASE_WM(LVM_SORTITEMSEX);
	CASE_WM(LVM_SUBITEMHITTEST);
	CASE_WM(LVM_UPDATE);
	}

#undef CASE_WM

	return false;
}

bool GetTooltipMessageName(UINT msg, CStringA& str)
{
#define CASE_WM(wm)                                                                                                    \
	case wm:                                                                                                           \
		str = #wm;                                                                                                     \
		return true

	switch (msg)
	{
		CASE_WM(TTM_ACTIVATE);
		CASE_WM(TTM_SETDELAYTIME);
		CASE_WM(TTM_ADDTOOLA);
		CASE_WM(TTM_ADDTOOLW);
		CASE_WM(TTM_DELTOOLA);
		CASE_WM(TTM_DELTOOLW);
		CASE_WM(TTM_NEWTOOLRECTA);
		CASE_WM(TTM_NEWTOOLRECTW);
		CASE_WM(TTM_RELAYEVENT);
		CASE_WM(TTM_GETTOOLINFOA);
		CASE_WM(TTM_GETTOOLINFOW);
		CASE_WM(TTM_SETTOOLINFOA);
		CASE_WM(TTM_SETTOOLINFOW);
		CASE_WM(TTM_HITTESTA);
		CASE_WM(TTM_HITTESTW);
		CASE_WM(TTM_GETTEXTA);
		CASE_WM(TTM_GETTEXTW);
		CASE_WM(TTM_UPDATETIPTEXTA);
		CASE_WM(TTM_UPDATETIPTEXTW);
		CASE_WM(TTM_GETTOOLCOUNT);
		CASE_WM(TTM_ENUMTOOLSA);
		CASE_WM(TTM_ENUMTOOLSW);
		CASE_WM(TTM_GETCURRENTTOOLA);
		CASE_WM(TTM_GETCURRENTTOOLW);
		CASE_WM(TTM_WINDOWFROMPOINT);
		CASE_WM(TTM_TRACKACTIVATE);
		CASE_WM(TTM_TRACKPOSITION);
		CASE_WM(TTM_SETTIPBKCOLOR);
		CASE_WM(TTM_SETTIPTEXTCOLOR);
		CASE_WM(TTM_GETDELAYTIME);
		CASE_WM(TTM_GETTIPBKCOLOR);
		CASE_WM(TTM_GETTIPTEXTCOLOR);
		CASE_WM(TTM_SETMAXTIPWIDTH);
		CASE_WM(TTM_GETMAXTIPWIDTH);
		CASE_WM(TTM_SETMARGIN);
		CASE_WM(TTM_GETMARGIN);
		CASE_WM(TTM_POP);
		CASE_WM(TTM_UPDATE);
		CASE_WM(TTM_GETBUBBLESIZE);
		CASE_WM(TTM_ADJUSTRECT);
		CASE_WM(TTM_SETTITLEA);
		CASE_WM(TTM_SETTITLEW);
		CASE_WM(TTM_POPUP);
		CASE_WM(TTM_GETTITLE);
	}

#undef CASE_WM

	return false;
}

#ifndef WM_UAHDESTROYWINDOW
#define WM_UAHDESTROYWINDOW 0x0090
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092
#define WM_UAHINITMENU 0x0093
#define WM_UAHMEASUREMENUITEM 0x0094
#define WM_UAHNCPAINTMENUPOPUP 0x0095
#endif

CStringA GetWindowMessageName(UINT msg)
{
#define CASE_WM(wm)                                                                                                    \
	case wm:                                                                                                           \
		return #wm

	switch (msg)
	{
		CASE_WM(WM_NULL);
		CASE_WM(WM_CREATE);
		CASE_WM(WM_DESTROY);
		CASE_WM(WM_MOVE);
		CASE_WM(WM_SIZE);
		CASE_WM(WM_ACTIVATE);
		CASE_WM(WM_SETFOCUS);
		CASE_WM(WM_KILLFOCUS);
		CASE_WM(WM_ENABLE);
		CASE_WM(WM_SETREDRAW);
		CASE_WM(WM_SETTEXT);
		CASE_WM(WM_GETTEXT);
		CASE_WM(WM_GETTEXTLENGTH);
		CASE_WM(WM_PAINT);
		CASE_WM(WM_CLOSE);
		CASE_WM(WM_QUERYENDSESSION);
		CASE_WM(WM_QUERYOPEN);
		CASE_WM(WM_ENDSESSION);
		CASE_WM(WM_QUIT);
		CASE_WM(WM_ERASEBKGND);
		CASE_WM(WM_SYSCOLORCHANGE);
		CASE_WM(WM_SHOWWINDOW);
		CASE_WM(WM_WININICHANGE);
		CASE_WM(WM_DEVMODECHANGE);
		CASE_WM(WM_ACTIVATEAPP);
		CASE_WM(WM_FONTCHANGE);
		CASE_WM(WM_TIMECHANGE);
		CASE_WM(WM_CANCELMODE);
		CASE_WM(WM_SETCURSOR);
		CASE_WM(WM_MOUSEACTIVATE);
		CASE_WM(WM_CHILDACTIVATE);
		CASE_WM(WM_QUEUESYNC);
		CASE_WM(WM_GETMINMAXINFO);
		CASE_WM(WM_PAINTICON);
		CASE_WM(WM_ICONERASEBKGND);
		CASE_WM(WM_NEXTDLGCTL);
		CASE_WM(WM_SPOOLERSTATUS);
		CASE_WM(WM_DRAWITEM);
		CASE_WM(WM_MEASUREITEM);
		CASE_WM(WM_DELETEITEM);
		CASE_WM(WM_VKEYTOITEM);
		CASE_WM(WM_CHARTOITEM);
		CASE_WM(WM_SETFONT);
		CASE_WM(WM_GETFONT);
		CASE_WM(WM_SETHOTKEY);
		CASE_WM(WM_GETHOTKEY);
		CASE_WM(WM_QUERYDRAGICON);
		CASE_WM(WM_COMPAREITEM);
		CASE_WM(WM_GETOBJECT);
		CASE_WM(WM_COMPACTING);
		CASE_WM(WM_COMMNOTIFY);
		CASE_WM(WM_WINDOWPOSCHANGING);
		CASE_WM(WM_WINDOWPOSCHANGED);
		CASE_WM(WM_POWER);
		CASE_WM(WM_COPYDATA);
		CASE_WM(WM_CANCELJOURNAL);
		CASE_WM(WM_NOTIFY);
		CASE_WM(WM_INPUTLANGCHANGEREQUEST);
		CASE_WM(WM_INPUTLANGCHANGE);
		CASE_WM(WM_TCARD);
		CASE_WM(WM_HELP);
		CASE_WM(WM_USERCHANGED);
		CASE_WM(WM_NOTIFYFORMAT);
		CASE_WM(WM_CONTEXTMENU);
		CASE_WM(WM_STYLECHANGING);
		CASE_WM(WM_STYLECHANGED);
		CASE_WM(WM_DISPLAYCHANGE);
		CASE_WM(WM_GETICON);
		CASE_WM(WM_SETICON);
		CASE_WM(WM_NCCREATE);
		CASE_WM(WM_NCDESTROY);
		CASE_WM(WM_NCCALCSIZE);
		CASE_WM(WM_NCHITTEST);
		CASE_WM(WM_NCPAINT);
		CASE_WM(WM_NCACTIVATE);
		CASE_WM(WM_GETDLGCODE);
		CASE_WM(WM_SYNCPAINT);
		CASE_WM(WM_UAHDESTROYWINDOW);
		CASE_WM(WM_UAHDRAWMENU);
		CASE_WM(WM_UAHDRAWMENUITEM);
		CASE_WM(WM_UAHINITMENU);
		CASE_WM(WM_UAHMEASUREMENUITEM);
		CASE_WM(WM_UAHNCPAINTMENUPOPUP);
		CASE_WM(WM_NCMOUSEMOVE);
		CASE_WM(WM_NCLBUTTONDOWN);
		CASE_WM(WM_NCLBUTTONUP);
		CASE_WM(WM_NCLBUTTONDBLCLK);
		CASE_WM(WM_NCRBUTTONDOWN);
		CASE_WM(WM_NCRBUTTONUP);
		CASE_WM(WM_NCRBUTTONDBLCLK);
		CASE_WM(WM_NCMBUTTONDOWN);
		CASE_WM(WM_NCMBUTTONUP);
		CASE_WM(WM_NCMBUTTONDBLCLK);
		CASE_WM(WM_NCXBUTTONDOWN);
		CASE_WM(WM_NCXBUTTONUP);
		CASE_WM(WM_NCXBUTTONDBLCLK);
		CASE_WM(WM_INPUT_DEVICE_CHANGE);
		CASE_WM(WM_INPUT);
		CASE_WM(WM_KEYDOWN);
		CASE_WM(WM_KEYUP);
		CASE_WM(WM_CHAR);
		CASE_WM(WM_DEADCHAR);
		CASE_WM(WM_SYSKEYDOWN);
		CASE_WM(WM_SYSKEYUP);
		CASE_WM(WM_SYSCHAR);
		CASE_WM(WM_SYSDEADCHAR);
		CASE_WM(WM_UNICHAR);
		CASE_WM(WM_IME_STARTCOMPOSITION);
		CASE_WM(WM_IME_ENDCOMPOSITION);
		CASE_WM(WM_IME_COMPOSITION);
		CASE_WM(WM_INITDIALOG);
		CASE_WM(WM_COMMAND);
		CASE_WM(WM_SYSCOMMAND);
		CASE_WM(WM_TIMER);
		CASE_WM(WM_HSCROLL);
		CASE_WM(WM_VSCROLL);
		CASE_WM(WM_INITMENU);
		CASE_WM(WM_INITMENUPOPUP);
		// CASE_WM(WM_GESTURE);
		// CASE_WM(WM_GESTURENOTIFY);
		CASE_WM(WM_MENUSELECT);
		CASE_WM(WM_MENUCHAR);
		CASE_WM(WM_ENTERIDLE);
		CASE_WM(WM_MENURBUTTONUP);
		CASE_WM(WM_MENUDRAG);
		CASE_WM(WM_MENUGETOBJECT);
		CASE_WM(WM_UNINITMENUPOPUP);
		CASE_WM(WM_MENUCOMMAND);
		CASE_WM(WM_CHANGEUISTATE);
		CASE_WM(WM_UPDATEUISTATE);
		CASE_WM(WM_QUERYUISTATE);
		CASE_WM(WM_CTLCOLORMSGBOX);
		CASE_WM(WM_CTLCOLOREDIT);
		CASE_WM(WM_CTLCOLORLISTBOX);
		CASE_WM(WM_CTLCOLORBTN);
		CASE_WM(WM_CTLCOLORDLG);
		CASE_WM(WM_CTLCOLORSCROLLBAR);
		CASE_WM(WM_CTLCOLORSTATIC);
		CASE_WM(MN_GETHMENU);
		CASE_WM(WM_MOUSEMOVE);
		CASE_WM(WM_LBUTTONDOWN);
		CASE_WM(WM_LBUTTONUP);
		CASE_WM(WM_LBUTTONDBLCLK);
		CASE_WM(WM_RBUTTONDOWN);
		CASE_WM(WM_RBUTTONUP);
		CASE_WM(WM_RBUTTONDBLCLK);
		CASE_WM(WM_MBUTTONDOWN);
		CASE_WM(WM_MBUTTONUP);
		CASE_WM(WM_MBUTTONDBLCLK);
		CASE_WM(WM_MOUSEWHEEL);
		CASE_WM(WM_XBUTTONDOWN);
		CASE_WM(WM_XBUTTONUP);
		CASE_WM(WM_XBUTTONDBLCLK);
		CASE_WM(WM_PARENTNOTIFY);
		CASE_WM(WM_ENTERMENULOOP);
		CASE_WM(WM_EXITMENULOOP);
		CASE_WM(WM_NEXTMENU);
		CASE_WM(WM_SIZING);
		CASE_WM(WM_CAPTURECHANGED);
		CASE_WM(WM_MOVING);
		CASE_WM(WM_POWERBROADCAST);
		CASE_WM(WM_DEVICECHANGE);
		CASE_WM(WM_MDICREATE);
		CASE_WM(WM_MDIDESTROY);
		CASE_WM(WM_MDIACTIVATE);
		CASE_WM(WM_MDIRESTORE);
		CASE_WM(WM_MDINEXT);
		CASE_WM(WM_MDIMAXIMIZE);
		CASE_WM(WM_MDITILE);
		CASE_WM(WM_MDICASCADE);
		CASE_WM(WM_MDIICONARRANGE);
		CASE_WM(WM_MDIGETACTIVE);
		CASE_WM(WM_MDISETMENU);
		CASE_WM(WM_ENTERSIZEMOVE);
		CASE_WM(WM_EXITSIZEMOVE);
		CASE_WM(WM_DROPFILES);
		CASE_WM(WM_MDIREFRESHMENU);
		// CASE_WM(WM_TOUCH);
		CASE_WM(WM_IME_SETCONTEXT);
		CASE_WM(WM_IME_NOTIFY);
		CASE_WM(WM_IME_CONTROL);
		CASE_WM(WM_IME_COMPOSITIONFULL);
		CASE_WM(WM_IME_SELECT);
		CASE_WM(WM_IME_CHAR);
		CASE_WM(WM_IME_REQUEST);
		CASE_WM(WM_IME_KEYDOWN);
		CASE_WM(WM_IME_KEYUP);
		CASE_WM(WM_MOUSEHOVER);
		CASE_WM(WM_MOUSELEAVE);
		CASE_WM(WM_NCMOUSEHOVER);
		CASE_WM(WM_NCMOUSELEAVE);
		CASE_WM(WM_WTSSESSION_CHANGE);
		CASE_WM(WM_TABLET_FIRST);
		CASE_WM(WM_TABLET_LAST);
		CASE_WM(WM_CUT);
		CASE_WM(WM_COPY);
		CASE_WM(WM_PASTE);
		CASE_WM(WM_CLEAR);
		CASE_WM(WM_UNDO);
		CASE_WM(WM_RENDERFORMAT);
		CASE_WM(WM_RENDERALLFORMATS);
		CASE_WM(WM_DESTROYCLIPBOARD);
		CASE_WM(WM_DRAWCLIPBOARD);
		CASE_WM(WM_PAINTCLIPBOARD);
		CASE_WM(WM_VSCROLLCLIPBOARD);
		CASE_WM(WM_SIZECLIPBOARD);
		CASE_WM(WM_ASKCBFORMATNAME);
		CASE_WM(WM_CHANGECBCHAIN);
		CASE_WM(WM_HSCROLLCLIPBOARD);
		CASE_WM(WM_QUERYNEWPALETTE);
		CASE_WM(WM_PALETTEISCHANGING);
		CASE_WM(WM_PALETTECHANGED);
		CASE_WM(WM_HOTKEY);
		CASE_WM(WM_PRINT);
		CASE_WM(WM_PRINTCLIENT);
		CASE_WM(WM_APPCOMMAND);
		CASE_WM(WM_THEMECHANGED);
		CASE_WM(WM_CLIPBOARDUPDATE);
		// CASE_WM(WM_DWMCOMPOSITIONCHANGED);
		// CASE_WM(WM_DWMNCRENDERINGCHANGED);
		// CASE_WM(WM_DWMCOLORIZATIONCOLORCHANGED);
		// CASE_WM(WM_DWMWINDOWMAXIMIZEDCHANGE);
		// CASE_WM(WM_DWMSENDICONICTHUMBNAIL);
		// CASE_WM(WM_DWMSENDICONICLIVEPREVIEWBITMAP);
		// CASE_WM(WM_GETTITLEBARINFOEX);
		CASE_WM(WM_HANDHELDFIRST);
		CASE_WM(WM_HANDHELDLAST);
		CASE_WM(WM_APP);
		CASE_WM(WM_USER);
		CASE_WM(WM_DPICHANGED);
		CASE_WM(WM_DPICHANGED_AFTERPARENT);
		CASE_WM(WM_DPICHANGED_BEFOREPARENT);
	case 0x0118:
		return "WM_SYSTIMER";
	default: {
		if (!((msg & 0x2000) != 0x2000))
		{
			CStringA str = GetWindowMessageName(msg - 0x2000);
			if (str.IsEmpty())
			{
				char buff[30];
				sprintf_s(buff, 30, "WM_REFLECT: %d", msg);
				return buff;
			}
			return (CStringA("WM_REFLECT: ") + str);
		}

		char buff[255];
		memset(buff, 0, 255);
		if (GetClipboardFormatNameA(msg, buff, 255))
			return buff;
		
		if (msg > WM_USER)
		{
			sprintf_s(buff, 255, "WM_USER + %d", msg - WM_USER);
			return buff;
		}

		sprintf_s(buff, 255, "0x%x", msg);
		return buff;
	}
	}
#undef CASE_WM
}

CStringA GetTooltipMessageName(UINT msg)
{
	CStringA result;

	if (!GetTooltipMessageName(msg, result))
		result = GetWindowMessageName(msg);

	return result;
}

CStringA GetListMessageName(UINT msg)
{
	CStringA result;

	if (!GetListMessageName(msg, result))
		result = GetWindowMessageName(msg);

	return result;
}

//---------------------------------------------------------------
// Initializes GDI+ if not already initialized
//---------------------------------------------------------------
bool GDIPlusManager::init()
{
	if (m_GdiplusToken != 0)
	{
		// Already initialized
		return true;
	}

	ULONG_PTR token = 0;
	Gdiplus::GdiplusStartupInput input;
	Gdiplus::Status status = Gdiplus::GdiplusStartup(&token, &input, NULL);
	if (status != Gdiplus::Ok)
	{
		return false;
	}

	if (interlockedExchangeToken(token) != 0)
	{
		// Initialized by another thread
		Gdiplus::GdiplusShutdown(token);
	}

	return true;
}

//---------------------------------------------------------------
// Releases GDI+ if this is the last user
//---------------------------------------------------------------
void GDIPlusManager::release()
{
	ULONG_PTR token = interlockedExchangeToken(0);
	if (token != 0)
	{
		Gdiplus::GdiplusShutdown(token);
	}
}

ULONG_PTR GDIPlusManager::interlockedExchangeToken(ULONG_PTR token)
{
	//#if !defined(_InterlockedExchangePointer) && defined(_M_IX86)
	//	return _InterlockedExchange(reinterpret_cast<long volatile*>(&m_GdiplusToken), token);
	//#else
	return reinterpret_cast<ULONG_PTR>(_InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&m_GdiplusToken),
	                                                               reinterpret_cast<void*>(token)));
	//#endif // !defined(_InterlockedExchangePointer) && defined(_M_IX86)
}

GDIPlusManager GDIPlusManager::s_instance;

HRESULT CXTheme::DrawMonoInnerEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                   LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, BDR_INNER, BF_flags | BF_MONO, opt_outContentRect);
}

HRESULT CXTheme::DrawMonoOuterEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                   LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, BDR_OUTER, BF_flags | BF_MONO, opt_outContentRect);
}

HRESULT CXTheme::DrawFlatInnerEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                   LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, BDR_INNER, BF_flags | BF_FLAT, opt_outContentRect);
}

HRESULT CXTheme::DrawFlatOuterEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                   LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, BDR_OUTER, BF_flags | BF_FLAT, opt_outContentRect);
}

HRESULT CXTheme::DrawSunkenEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, EDGE_SUNKEN, BF_flags, opt_outContentRect);
}

HRESULT CXTheme::DrawEtchedEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                                LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, EDGE_ETCHED, BF_flags, opt_outContentRect);
}

HRESULT CXTheme::DrawBumpEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BF_flags,
                              LPRECT opt_outContentRect /*= nullptr*/)
{
	return DrawEdge(dc, iPartId, iStateId, rect, EDGE_BUMP, BF_flags, opt_outContentRect);
}

HRESULT CXTheme::DrawEdge(HDC dc, int iPartId, int iStateId, LPCRECT rect, UINT BDR_flags, UINT BF_flags,
                          LPRECT opt_outContentRect /*= nullptr*/)
{
	return ::DrawThemeEdge(m_theme, dc, iPartId, iStateId, rect, BDR_flags, BF_flags, opt_outContentRect);
}

bool CXTheme::IsThemePartDefined(int iPartId, int iStateId)
{
	return ::IsThemePartDefined(m_theme, iPartId, iStateId) != FALSE;
}

bool CXTheme::IsAppThemed()
{
	return ::IsAppThemed() != FALSE;
}

HRESULT CXTheme::DrawBGEx(HDC dc, int iPartId, int iStateId, LPCRECT rect, bool parent /*= true*/,
                          bool omitBorder /*= false*/, bool omitContent /*= false*/,
                          LPCRECT clipRect /*= nullptr*/) const
{
	DTBGOPTS opts = {0};
	opts.dwSize = sizeof(DTBGOPTS);

	if (omitBorder)
		opts.dwFlags |= DTBG_OMITBORDER;
	if (omitContent)
		opts.dwFlags |= DTBG_OMITCONTENT;
	if (clipRect)
	{
		opts.dwFlags |= DTBG_CLIPRECT;
		opts.rcClip = *clipRect;
	}

	return DrawBGEx(dc, iPartId, iStateId, rect, &opts, parent);
}

HRESULT CXTheme::DrawBGEx(HDC dc, int iPartId, int iStateId, LPCRECT rect, const DTBGOPTS* opts,
                          bool parent /*= true*/) const
{
	HRESULT hr = S_FALSE;
	if (parent)
	{
		if (opts && (opts->dwFlags & DTBG_CLIPRECT) == DTBG_CLIPRECT)
			hr = DrawParentBGIfTransparent(dc, iPartId, iStateId, rect, &opts->rcClip);
		else
			hr = DrawParentBGIfTransparent(dc, iPartId, iStateId, rect, nullptr);
	}

	if (!parent || SUCCEEDED(hr))
		hr = ::DrawThemeBackgroundEx(m_theme, dc, iPartId, iStateId, rect, opts);

	return hr;
}

HRESULT CXTheme::DrawBG(HDC dc, int iPartId, int iStateId, LPCRECT rect, LPCRECT clipRect, bool parent /*= true*/) const
{
	HRESULT hr = S_FALSE;
	if (parent)
		hr = DrawParentBGIfTransparent(dc, iPartId, iStateId, rect, clipRect);

	if (!parent || SUCCEEDED(hr))
		hr = ::DrawThemeBackground(m_theme, dc, iPartId, iStateId, rect, clipRect);

	return hr;
}

bool CXTheme::IsBGPartiallyTransparent(int iPartId, int iStateId) const
{
	return IsThemeBackgroundPartiallyTransparent(m_theme, iPartId, iStateId) != FALSE;
}

HRESULT CXTheme::DrawParentBGIfTransparent(HDC dc, int iPartId, int iStateId, LPCRECT rect, LPCRECT clipRect) const
{
	if (IsThemeBackgroundPartiallyTransparent(m_theme, iPartId, iStateId))
	{
		if (clipRect)
		{
			CRect iRect;
			iRect.IntersectRect(rect, clipRect);
			return ::DrawThemeParentBackground(m_hWnd, dc, iRect);
		}
		else
		{
			return ::DrawThemeParentBackground(m_hWnd, dc, rect);
		}
	}

	return S_FALSE;
}

COLORREF CXTheme::GetSysColor(int colorId)
{
	return ::GetThemeSysColor(m_theme, colorId);
}

void CXTheme::OnThemeChanged()
{
	CloseThemeData();
	OpenThemeData(m_hWnd, m_classList);
}

void CXTheme::OpenThemeData(HWND hWnd, LPCWSTR pszClassList)
{
	CloseThemeData();
	m_theme = ::OpenThemeData(hWnd, pszClassList);
	if (m_theme)
	{
		m_hWnd = hWnd;
		m_classList = pszClassList;
	}
	else
	{
		m_hWnd = nullptr;
		m_classList.Empty();
	}
}

void CXTheme::CloseThemeData()
{
	if (m_theme)
		::CloseThemeData(m_theme);
	m_theme = NULL;
}

void CPenMap::Clear()
{
	CSingleLock lock(&m_cs, true);
	m_map.clear();
}

void CPenMap::Erase(COLORREF color, int style /*= PS_SOLID*/, int width /*= 0*/)
{
	CSingleLock lock(&m_cs, true);
	m_map.erase(pen_index(color, style, width));
}

HPEN CPenMap::GetHPEN(COLORREF color, int style /*= PS_SOLID*/, int width /*= 0*/)
{
	return GetPen(color, style, width);
}

CPen& CPenMap::GetPen(COLORREF color, int style /*= PS_SOLID*/, int width /*= 0*/)
{
	CSingleLock lock(&m_cs, true);
	pen_index index(color, style, width);
	pen_map::iterator it = m_map.find(index);
	if (it != m_map.end())
		return *it->second;
	pen_ptr new_pen(new CPen(style, width, color));
	m_map[index] = new_pen;
	return *new_pen;
}

CPen* CPenMap::GetPenP(COLORREF color, int style /*= PS_SOLID*/, int width /*= 0*/)
{
	CPen& pen = GetPen(color, style, width);
	return &pen;
}

bool CPenMap::pen_index::operator<(const pen_index& rhs) const
{
	if (color != rhs.color)
		return color < rhs.color;
	else if (style != rhs.style)
		return style < rhs.style;
	else
		return width < rhs.width;
}

CPenMap::pen_index::pen_index(COLORREF c, int s, int w) : color(c), style(s), width(w)
{
}

void CBrushMap::Clear()
{
	CSingleLock lock(&m_cs, true);
	m_map.clear();
}

void CBrushMap::Erase(COLORREF color)
{
	CSingleLock lock(&m_cs, true);
	m_map.erase(color);
}

HBRUSH CBrushMap::GetHBRUSH(COLORREF color)
{
	return GetBrush(color);
}

CBrush& CBrushMap::GetBrush(COLORREF color)
{
	CSingleLock lock(&m_cs, true);
	brush_map::iterator it = m_map.find(color);
	if (it != m_map.end())
		return *it->second;
	brush_ptr new_brush(new CBrush(color));
	m_map[color] = new_brush;
	return *new_brush;
}

CBrush* CBrushMap::GetBrushP(COLORREF color)
{
	CBrush& brush = GetBrush(color);
	return &brush;
}

CMemDC::CMemDC(CDC* pDC, const CRect* pRect /*= NULL*/) : CMemDC(pDC, true, pRect)
{
}

CMemDC::CMemDC(CDC* pDC, bool fill_bg, const CRect* pRect /*= NULL*/)
{
	ASSERT(pDC != NULL);

	// Some initialization
	m_pDC = pDC;
	m_oldBitmap = NULL;
	m_bMemDC = !pDC->IsPrinting();

	// Get the rectangle to draw
	if (pRect == NULL)
	{
		pDC->GetClipBox(&m_rect);
	}
	else
	{
		m_rect = *pRect;
	}

	if (m_bMemDC)
	{
		// Create a Memory DC
		CreateCompatibleDC(pDC);
		pDC->LPtoDP(&m_rect);

		m_bitmap.CreateCompatibleBitmap(pDC, m_rect.Width(), m_rect.Height());
		m_oldBitmap = SelectObject(&m_bitmap);

		SetMapMode(pDC->GetMapMode());

		SetWindowExt(pDC->GetWindowExt());
		SetViewportExt(pDC->GetViewportExt());

		pDC->DPtoLP(&m_rect);
		SetWindowOrg(m_rect.left, m_rect.top);
	}
	else
	{
		// Make a copy of the relevent parts of the current
		// DC for printing
		m_bPrinting = pDC->m_bPrinting;
		m_hDC = pDC->m_hDC;
		m_hAttribDC = pDC->m_hAttribDC;
	}

	if (!fill_bg)
	{
		BitBlt(m_rect.left, m_rect.top, m_rect.Width(), m_rect.Height(), pDC, m_rect.left, m_rect.top, SRCCOPY);
	}
	else
	{
		// Fill background
		FillSolidRect(m_rect, pDC->GetBkColor());
	}
}

CMemDC::~CMemDC()
{
	if (m_bMemDC)
	{
		// Copy the offscreen bitmap onto the screen.
		m_pDC->BitBlt(m_rect.left, m_rect.top, m_rect.Width(), m_rect.Height(), this, m_rect.left, m_rect.top, SRCCOPY);

		// Swap back the original bitmap.
		SelectObject(m_oldBitmap);
	}
	else
	{
		// All we need to do is replace the DC with an illegal
		// value, this keeps us from accidentally deleting the
		// handles associated with the CDC that was passed to
		// the constructor.
		m_hDC = m_hAttribDC = NULL;
	}
}

void ReorderChildren(HWND parent, bool childrenOfChildren /*= false*/)
{
	struct WndSorter
	{
		CRect rect;
		HWND hWnd;

		WndSorter() : rect(), hWnd(0)
		{
		}
		WndSorter(HWND wnd) : hWnd(wnd)
		{
			::GetWindowRect(wnd, &rect);
		}

		bool operator<(const WndSorter& rhs) const
		{
			// If rectangles vertically intersect, controls are
			// considered as being in one row, therefore we do not
			// apply "top" rule on them in such case.
			if (rect.top > rhs.rect.bottom || rect.bottom < rhs.rect.top)
				return rect.top < rhs.rect.top;

			// If rectangles horizontally intersect, controls are
			// considered as being in one column, therefore we do not
			// apply "left" rule on them in such case.
			if (rect.left > rhs.rect.right || rect.right < rhs.rect.left)
				return rect.left < rhs.rect.left;

			// When intersection rules disallowed to define order,
			// we consider top and left respectively.

			if (rect.top != rhs.rect.top)
				return rect.top < rhs.rect.top;

			if (rect.left != rhs.rect.left)
				return rect.left < rhs.rect.left;

			_ASSERTE(!"Controls on equal position");

			// Something is very wrong...
			// As the last option, we compare IDs of controls
			// to ensure consistency of sorting order.
			return ::GetDlgCtrlID(hWnd) < ::GetDlgCtrlID(rhs.hWnd);
		}
	};

	std::set<WndSorter> wnds;

	for (HWND hwnd = ::GetWindow(parent, GW_CHILD); hwnd; hwnd = ::GetWindow(hwnd, GW_HWNDNEXT))
	{
		wnds.insert(WndSorter(hwnd));

		if (childrenOfChildren && ::GetWindow(hwnd, GW_CHILD))
			ReorderChildren(hwnd, true);
	};

	HWND prev = 0;
	for (const WndSorter& wpt : wnds)
	{
		::SetWindowPos(wpt.hWnd, prev, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		prev = wpt.hWnd;
	}
}

Canvas& Canvas::AddAction(action act)
{
	m_actions.push_back(act);
	return *this;
}
Canvas& Canvas::UsePen(DWORD id)
{
	return AddAction([id](Context& ctx) {
		ctx.pen = ctx.pens[id];
		ctx.brush.reset();
	});
}
Canvas& Canvas::UsePenFixed(DWORD id)
{
	return AddAction([id](Context& ctx) {
		ctx.pen = ctx.pens[id];

		if (ctx.pen)
		{
			ctx.pen.reset(ctx.pen->Clone());
			ctx.pen->SetWidth(ctx.pen->GetWidth() / __min(ctx.yScale, ctx.xScale));
		}

		ctx.brush.reset();
	});
}

Canvas& Canvas::UsePenMinWidth(DWORD id, float min_width)
{
	return AddAction([id, min_width](Context& ctx) {
		ctx.pen = ctx.pens[id];

		float scale = __min(ctx.yScale, ctx.xScale);

		if (ctx.pen && ctx.pen->GetWidth() * scale < min_width)
		{
			ctx.pen.reset(ctx.pen->Clone());
			ctx.pen->SetWidth(min_width / scale);
		}

		ctx.brush.reset();
	});
}

Canvas& Canvas::UseBrush(DWORD id)
{
	return AddAction([id](Context& ctx) {
		ctx.pen.reset();
		ctx.brush = ctx.brushes[id];
	});
}
Canvas& Canvas::UsePenAndBrush(DWORD id_pen, DWORD id_brush)
{
	return AddAction([id_pen, id_brush](Context& ctx) {
		ctx.pen = ctx.pens[id_pen];
		ctx.brush = ctx.brushes[id_brush];
	});
}
Canvas& Canvas::SetBrush(DWORD index, brushP brush)
{
	if (m_brushes.size() <= index)
		m_brushes.resize(index + 1);

	m_brushes[index] = brush;
	return *this;
}
Canvas& Canvas::SetSolidBrush(DWORD index, COLORREF rgb, BYTE alpha /*= 0xFF*/)
{
	return SetSolidBrush(index, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb), alpha);
}

Canvas& Canvas::SetSolidBrush(DWORD index, BYTE R, BYTE G, BYTE B, BYTE A /*= 0xFF*/)
{
	return SetBrush(index, brushP(new Gdiplus::SolidBrush(Gdiplus::Color(A, R, G, B))));
}
Canvas& Canvas::SetPen(DWORD index, float width, COLORREF rgb, BYTE alpha /*= 0xFF*/)
{
	return SetPen(index, width, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb), alpha);
}

Canvas& Canvas::SetPen(DWORD index, penP pen)
{
	if (m_pens.size() <= index)
		m_pens.resize(index + 1);

	m_pens[index] = pen;
	return *this;
}
Canvas& Canvas::SetPen(DWORD index, BYTE R, BYTE G, BYTE B, BYTE A /*= 0xFF*/)
{
	return SetPen(index, penP(new Gdiplus::Pen(Gdiplus::Color(A, R, G, B))));
}
Canvas& Canvas::SetPen(DWORD index, COLORREF rgb, BYTE alpha /*= 0xFF*/)
{
	return SetPen(index, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb), alpha);
}
Canvas& Canvas::SetPen(DWORD index, float width, BYTE R, BYTE G, BYTE B, BYTE A /*= 0xFF*/)
{
	return SetPen(index, penP(new Gdiplus::Pen(Gdiplus::Color(A, R, G, B), width)));
}
Canvas& Canvas::Line(float x1, float y1, float x2, float y2)
{
	return AddAction([=](Context& ctx) {
		auto pen = ctx.pen.get();
		if (pen)
			ctx.gr.DrawLine(pen, x1, y1, x2, y2);
	});
}
Canvas& Canvas::Ellipse(float x1, float y1, float x2, float y2)
{
	return AddAction([=](Context& ctx) {
		auto pen = ctx.pen.get();
		if (pen)
			ctx.gr.DrawEllipse(pen, x1, y1, fabs(x2 - x1), fabs(y2 - y1));

		auto brush = ctx.brush.get();
		if (brush)
			ctx.gr.FillEllipse(brush, x1, y1, fabs(x2 - x1), fabs(y2 - y1));
	});
}
Canvas& Canvas::Arc(float x1, float y1, float x2, float y2, float start, float sweep)
{
	return AddAction([=](Context& ctx) {
		auto pen = ctx.pen.get();
		if (pen)
			ctx.gr.DrawArc(pen, x1, y1, fabs(x2 - x1), fabs(y2 - y1), start, sweep);

		auto brush = ctx.brush.get();
		if (brush)
			ctx.gr.FillPie(brush, x1, y1, fabs(x2 - x1), fabs(y2 - y1), start, sweep);
	});
}
Canvas& Canvas::Rect(float x1, float y1, float x2, float y2)
{
	return AddAction([=](Context& ctx) {
		auto pen = ctx.pen.get();
		if (pen)
			ctx.gr.DrawRectangle(pen, x1, y1, fabs(x2 - x1), fabs(y2 - y1));

		auto brush = ctx.brush.get();
		if (brush)
			ctx.gr.FillRectangle(brush, x1, y1, fabs(x2 - x1), fabs(y2 - y1));
	});
}
void Canvas::Paint(HDC hdc, float x, float y, float cx, float cy, setup_gr sgr /*= nullptr*/)
{
	CSize size((int)cx, (int)cy);
	CPoint pos((int)x, (int)y);
	CRect rect(pos, size);

	CDC* m_dc = CDC::FromHandle(hdc);
	if (m_dc == NULL)
	{
		ASSERT(FALSE);
		return;
	}

	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(m_dc))
	{
		ASSERT(FALSE);
		return;
	}

	COLORREF* pBits;
	HBITMAP hmbpDib = CDrawingManager::CreateBitmap_32(size, (LPVOID*)&pBits);

	if (hmbpDib == NULL || pBits == NULL)
	{
		ASSERT(FALSE);
		return;
	}

	HGDIOBJ pOldBmp = dcMem.SelectObject(hmbpDib);
	ENSURE(pOldBmp != NULL);

	Gdiplus::Graphics gpg(dcMem);
	gpg.ScaleTransform(cx / m_width, cy / m_height, Gdiplus::MatrixOrderAppend);
	gpg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	gpg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
	if (sgr)
		sgr(gpg);
	Paint(gpg, cx / m_width, cy / m_height);
	gpg.Flush();

	// Copy bitmap back to the screen:

	BLENDFUNCTION pixelblend = {AC_SRC_OVER, 0, 255, 1 /*AC_SRC_ALPHA*/};
	m_dc->AlphaBlend(rect.left, rect.top, rect.Width(), rect.Height(), &dcMem, 0, 0, size.cx, size.cy, pixelblend);

	dcMem.SelectObject(pOldBmp);
	DeleteObject(hmbpDib);
}

void Canvas::Paint(HDC hdc, float x, float y, setup_gr sgr /*= nullptr*/)
{
	CSize size((int)m_width, (int)m_height);
	CPoint pos((int)x, (int)y);
	CRect rect(pos, size);

	CDC* m_dc = CDC::FromHandle(hdc);
	if (m_dc == NULL)
	{
		ASSERT(FALSE);
		return;
	}

	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(m_dc))
	{
		ASSERT(FALSE);
		return;
	}

	COLORREF* pBits;
	HBITMAP hmbpDib = CDrawingManager::CreateBitmap_32(size, (LPVOID*)&pBits);

	if (hmbpDib == NULL || pBits == NULL)
	{
		ASSERT(FALSE);
		return;
	}

	HGDIOBJ pOldBmp = dcMem.SelectObject(hmbpDib);
	ENSURE(pOldBmp != NULL);

	Gdiplus::Graphics gpg(dcMem);
	gpg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	gpg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
	if (sgr)
		sgr(gpg);
	Paint(gpg);
	gpg.Flush();

	// Copy bitmap back to the screen:

	BLENDFUNCTION pixelblend = {AC_SRC_OVER, 0, 255, 1 /*AC_SRC_ALPHA*/};
	m_dc->AlphaBlend(rect.left, rect.top, rect.Width(), rect.Height(), &dcMem, 0, 0, size.cx, size.cy, pixelblend);

	dcMem.SelectObject(pOldBmp);
	DeleteObject(hmbpDib);
}

void Canvas::Paint(HDC hdc, const CRect& rect, setup_gr sgr /*= nullptr*/)
{
	Paint(hdc, (float)rect.left, (float)rect.top, (float)rect.Width(), (float)rect.Height(), sgr);
}

void Canvas::Paint(Gdiplus::Graphics& graphics, float x_scale /*= 1.0f*/, float y_scale /*= 1.0f*/)
{
	Context ctx(graphics, m_pens, m_brushes, x_scale, y_scale);

	for (action act : m_actions)
		act(ctx);
}

void ColorSet::DrawVertical(CDC& dc, const CRect& rect) const
{
	_ASSERTE(!colors.empty());

	if (colors.size() == 1)
		dc.FillSolidRect(&rect, Color(0));
	else if (!colors.empty())
		DrawVerticalVS2010Gradient(dc, rect, &colors.front(), (int)colors.size());
}

void ColorSet::DrawVertical(CDC& dc, const CRect& rect, float opacity) const
{
	_ASSERTE(!colors.empty());

	if (opacity == 1.0f)
		return DrawVertical(dc, rect);

	if (colors.size() == 1)
	{
		COLORREF fg_color = Color(0);
		TransformRectColors(dc, rect, [fg_color, opacity](int x, int y, COLORREF& color) {
			color = InterpolateColor(color, fg_color, opacity);
		});
	}
	else if (!colors.empty())
	{
		std::vector<COLORREF> clrArr;
		clrArr.resize((uint)rect.Height());
		InterpolateGradientColors(&clrArr[0], (int)clrArr.size(), &colors[0], (int)colors.size());
		TransformRectColors(dc, rect, [opacity, &clrArr](int x, int y, COLORREF& color) {
			color = InterpolateColor(color, clrArr[(uint)y], opacity);
		});
	}
}

void ColorSet::MixColors(COLORREF overlay, float opacity)
{
	for (auto& p : colors)
		p.second = InterpolateColor(p.second, overlay, opacity);
}

void ColorSet::Desaturate(BYTE percents)
{
	for (auto& p : colors)
		p.second = DesaturateColor(p.second, percents);
}

COLORREF ColorSet::Average() const
{
	DWORD sumR = 0, sumG = 0, sumB = 0;

	for (const auto& p : colors)
	{
		sumR += GetRValue(p.second);
		sumG += GetGValue(p.second);
		sumB += GetBValue(p.second);
	}

	sumR /= (uint)colors.size();
	sumG /= (uint)colors.size();
	sumB /= (uint)colors.size();

	return RGB((BYTE)sumR, (BYTE)sumG, (BYTE)sumB);
}

double ColorSet::Param(int index /*= 0*/) const
{
	return colors[(uint)index].first;
}

COLORREF ColorSet::Color(int index /*= 0*/) const
{
	return colors[(uint)index].second;
}

bool ColorSet::IsSolid() const
{
	return colors.size() == 1;
}

int ColorSet::ColorCount() const
{
	return (int)colors.size();
}

void ColorSet::Init(COLORREF c1, COLORREF c2, COLORREF c3, COLORREF c4)
{
	colors.resize(4);
	colors[0] = std::make_pair(0.0, c1);
	colors[1] = std::make_pair(0.49, c2);
	colors[2] = std::make_pair(0.5, c3);
	colors[3] = std::make_pair(1.0, c4);
}

void ColorSet::Init(COLORREF c1, COLORREF c2, COLORREF c3)
{
	colors.resize(3);
	colors[0] = std::make_pair(0.0, c1);
	colors[1] = std::make_pair(0.5, c2);
	colors[2] = std::make_pair(1.0, c3);
}

void ColorSet::Init(COLORREF c1, COLORREF c2)
{
	colors.resize(2);
	colors[0] = std::make_pair(0.0, c1);
	colors[1] = std::make_pair(1.0, c2);
}

void ColorSet::Init(COLORREF color)
{
	colors.resize(1);
	colors[0] = std::make_pair(0.0, color);
}

void ColorSet::Init(COLORREF* in_colors, double* in_pos, size_t in_count)
{
	_ASSERTE(in_colors != nullptr);
	_ASSERTE(in_pos != nullptr);
	_ASSERTE(in_count != 0);

	if (in_colors == nullptr || in_pos == nullptr || in_count == 0)
		return;

	this->colors.resize(in_count);
	for (size_t i = 0; i < in_count; i++)
		colors[i] = std::make_pair(in_pos[i], in_colors[i]);
}

ColorSet::ColorSet(COLORREF c1, COLORREF c2, COLORREF c3, COLORREF c4)
{
	Init(c1, c2, c3, c4);
}

ColorSet::ColorSet(COLORREF c1, COLORREF c2, COLORREF c3)
{
	Init(c1, c2, c3);
}

ColorSet::ColorSet(COLORREF c1, COLORREF c2)
{
	Init(c1, c2);
}

ColorSet::ColorSet(COLORREF color)
{
	Init(color);
}

ColorSet::ColorSet(const ColorSet& other) : colors(other.colors)
{
}

ColorSet::ColorSet()
{
}

ColorSet::ColorSet(COLORREF* in_colors, double* in_pos, size_t in_count)
{
	Init(in_colors, in_pos, in_count);
}

ColorSet& ColorSet::operator=(const ColorSet& other)
{
	if (&other != this)
		colors = other.colors;
	return *this;
}

bool TransformRectColors(CDC& m_dc, const CRect& rect, std::function<void(int x, int y, COLORREF& color)> transform)
{
	if (afxGlobalData.m_nBitsPerPixel <= 8)
		return false;

	if (rect.Height() <= 0 || rect.Width() <= 0 || !transform)
	{
		return true;
	}

	int cx = rect.Width();
	int cy = rect.Height();

	// copy the DC content into the memory bitmap

	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(&m_dc))
	{
		ASSERT(false);
		return false;
	}

	CBitmap bmpMem;
	if (!bmpMem.CreateCompatibleBitmap(&m_dc, cx, cy))
	{
		ASSERT(false);
		return false;
	}

	CBitmap* pOldBmp = dcMem.SelectObject(&bmpMem);
	ENSURE(pOldBmp != NULL);

	// initialize the 32 bit bitmap

	COLORREF* pBits;
	HBITMAP hmbpDib = CDrawingManager::CreateBitmap_32(CSize(cx, cy), (LPVOID*)&pBits);

	if (hmbpDib == NULL || pBits == NULL)
	{
		ASSERT(false);
		return false;
	}

	// copy bits from the DC to the bitmap

	dcMem.SelectObject(hmbpDib);
	dcMem.BitBlt(0, 0, cx, cy, &m_dc, rect.left, rect.top, SRCCOPY);

	// apply transform to bits of bitmap

	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
		{
			COLORREF c = 0x00ffffff & *pBits;
			c = RGB(GetBValue(c), GetGValue(c), GetRValue(c));
			transform(x, cy - y, c);
			c = RGB(GetBValue(c), GetGValue(c), GetRValue(c));
			*pBits = c | 0xff000000;
			pBits++;
		}
	}

	// copy bits form the bitmap to the DC

	m_dc.BitBlt(rect.left, rect.top, cx, cy, &dcMem, 0, 0, SRCCOPY);

	dcMem.SelectObject(pOldBmp);
	DeleteObject(hmbpDib);

	return true;
}

void InterpolateGradientColors(COLORREF* out_colors, int out_colors_size, const std::pair<double, COLORREF>* gradient,
                               int num_gradient)
{
	int i = 1;

	for (int y = 0; y < out_colors_size; y++)
	{
		double y_pos = (double)y / double(out_colors_size - 1);

		for (; i < num_gradient; i++)
			if (y_pos >= gradient[i - 1].first && y_pos <= gradient[i].first)
				break;

		double c1_pos = gradient[i - 1].first;
		double c2_pos = gradient[i].first;
		double c2_opacity = (y_pos - c1_pos) / (c2_pos - c1_pos);

		out_colors[y] = InterpolateColor(gradient[i - 1].second, gradient[i].second, float(c2_opacity));
	}
}

void InterpolateGradientColorsVS2010(COLORREF* out_colors, int out_colors_size, const COLORREF* colors, int num_colors)
{
	std::vector<std::pair<double, COLORREF>> gradient((uint)num_colors);

	if (num_colors == 4)
	{
		gradient[0] = std::make_pair(0, colors[0]);
		gradient[1] = std::make_pair(0.50, colors[1]);
		gradient[2] = std::make_pair(0.51, colors[2]);
		gradient[3] = std::make_pair(1, colors[3]);
	}
	else
	{
		for (uint i = 0; i < (uint)num_colors; i++)
			gradient[i] = std::make_pair((double)i / (double)(num_colors - 1), colors[i]);
	}

	InterpolateGradientColors(out_colors, out_colors_size, &gradient[0], num_colors);
}

void FrameRectDPI(CDC& dc, const CRect& rect, COLORREF outline, RECT sides /*= { 1, 1, 1, 1 }*/)
{
	int x = VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
	int y = VsUI::DpiHelper::LogicalToDeviceUnitsY(1);

	if (x == 1 && y == 1 && sides.left && sides.top && sides.right && sides.bottom)
	{
		CBrush brush(outline);
		dc.FrameRect(&rect, &brush);
	}
	else
	{
		CBrush brush(outline);

		CRect inner = rect;
		if (sides.left)
			inner.left += x;
		if (sides.top)
			inner.top += y;
		if (sides.right)
			inner.right -= x;
		if (sides.bottom)
			inner.bottom -= y;

		CRgn orgn, irgn;
		orgn.CreateRectRgnIndirect(&rect);
		irgn.CreateRectRgnIndirect(&inner);
		orgn.CombineRgn(&orgn, &irgn, RGN_DIFF);

		dc.FillRgn(&orgn, &brush);
	}
}

bool MakeBitmapTransparent(CBitmap& bitmap, COLORREF clrTransparent /*= CLR_INVALID*/)
{
	if (bitmap.m_hObject == NULL)
	{
		ASSERT(FALSE);
		return NULL;
	}

	BITMAP bmp;
	if (::GetObject(bitmap, sizeof(BITMAP), &bmp) == 0)
	{
		ASSERT(FALSE);
		return NULL;
	}

	int nHeight = bmp.bmHeight;
	LPVOID lpBits = NULL;
	HBITMAP hbmp = CDrawingManager::CreateBitmap_32(CSize(bmp.bmWidth, nHeight), &lpBits);
	nHeight = abs(nHeight);

	if (hbmp != NULL)
	{
		DWORD nSizeImage = DWORD(bmp.bmWidth * nHeight);

		CDC dcSrc;
		dcSrc.CreateCompatibleDC(NULL);
		HBITMAP hbmpSrc = (HBITMAP)dcSrc.SelectObject(bitmap);

		if (hbmpSrc != NULL)
		{
			CDC dcDst;
			dcDst.CreateCompatibleDC(NULL);
			HBITMAP hbmpDst = (HBITMAP)dcDst.SelectObject(hbmp);

			dcDst.BitBlt(0, 0, bmp.bmWidth, nHeight, &dcSrc, 0, 0, SRCCOPY);

			dcDst.SelectObject(hbmpDst);
			dcSrc.SelectObject(hbmpSrc);

			COLORREF* pBits = (COLORREF*)lpBits;

			COLORREF clrTrans =
			    clrTransparent == CLR_INVALID
			        ? *pBits
			        : RGB(GetBValue(clrTransparent), GetGValue(clrTransparent), GetRValue(clrTransparent));

			for (DWORD i = 0; i < nSizeImage; i++)
			{
				if (*pBits != clrTrans)
				{
					*pBits |= 0xFF000000;
				}
				else
				{
					*pBits = (COLORREF)0;
				}

				pBits++;
			}
		}

		bitmap.DeleteObject();
		bitmap.Attach(hbmp);
		return true;
	}

	return false;
}

void ThemeBitmap(CBitmap& bitmap, COLORREF bgClr)
{
	BITMAP bmp;
	if (bitmap.m_hObject && bitmap.GetBitmap(&bmp))
	{
		CImageList il;
		if (il.Create(bmp.bmWidth, bmp.bmHeight, ILC_COLOR32, 0, 0))
		{
			MakeBitmapTransparent(bitmap);
			int img_id = il.Add(&bitmap, RGB(0, 0, 0));

			gImgListMgr->ThemeImageList(il, bgClr);

			VsUI::GdiplusImage img_dst;
			img_dst.Create(bmp.bmWidth, bmp.bmHeight, PixelFormat32bppARGB);
			ATL::CAutoPtr<Gdiplus::Graphics> gr(img_dst.GetGraphics());
			gr->Clear(Gdiplus::Color::Transparent);
			HDC dc = gr->GetHDC();

			il.DrawEx(CDC::FromHandle(dc), img_id, {0, 0}, {bmp.bmWidth, bmp.bmHeight}, CLR_NONE, CLR_NONE,
			          ILD_TRANSPARENT);

			gr->ReleaseHDC(dc);
			dc = nullptr;

			bitmap.DeleteObject();
			bitmap.Attach(img_dst.Detach());
		}
	}
}

void LogicalToDeviceBitmap(CWnd& wnd, CBitmap& bmp, double quality /*= 8.0*/)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(&wnd);

	if (!VsUI::DpiHelper::IsScalingRequired() || !VsUI::DpiHelper::IsScalingSupported())
	{
		return;
	}

	VsUI::GdiplusImage srcImg;
	srcImg.Attach(bmp);

	double dpi_scale_x = VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX();
	double dpi_scale_y = dpi_scale_x; // VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorY();
	double scale_x = quality * dpi_scale_x;
	double scale_y = quality * dpi_scale_y;

	// make a list of allowed colors in quality bitmap
	std::set<DWORD> palette;
	srcImg.ProcessBitmapBits(srcImg, [&](Gdiplus::ARGB* pix) { palette.insert(*pix & 0x00FFFFFF); });

	// OK, let's create a quality bitmap.
	// in general, it is scaled original bitmap
	// and then it is converted to indexed colors bitmap
	// when only colors from original palette are allowed.
	VsUI::GdiplusImage dstImg;
	dstImg.Create((int)(scale_x * (double)srcImg.GetWidth()), (int)(scale_y * (double)srcImg.GetHeight()));
	{
		CAutoPtr<Gdiplus::Graphics> dst_gr(dstImg.GetGraphics());
		dst_gr->ScaleTransform((Gdiplus::REAL)(scale_x), (Gdiplus::REAL)(scale_y), Gdiplus::MatrixOrderAppend);
		dst_gr->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		dst_gr->SetSmoothingMode(Gdiplus::SmoothingModeNone);
		dst_gr->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
		dst_gr->Clear(Gdiplus::Color::Transparent);
		dst_gr->DrawImage(srcImg, 0, 0);
		dst_gr->Flush();
	}

	Gdiplus::Bitmap* pBigBitmap = dstImg;
	Gdiplus::BitmapData lockedBigBitmapData;
	UINT bigBitmapWidth = pBigBitmap->GetWidth();
	UINT bigBitmapHeight = pBigBitmap->GetHeight();

	Gdiplus::Rect rectDstImage(0, 0, (int)bigBitmapWidth, (int)bigBitmapHeight);
	if (pBigBitmap->LockBits(&rectDstImage, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
	                         PixelFormat32bppARGB, &lockedBigBitmapData) == Gdiplus::Ok)
	{
		BYTE* pDstData = reinterpret_cast<BYTE*>(lockedBigBitmapData.Scan0);

		auto RGBDistance = [](COLORREF A, COLORREF B) -> double {
			unsigned int r, g, b;
			double y, u, v;

			b = (uint)abs(GetBValue(A) - GetBValue(B));
			g = (uint)abs(GetGValue(A) - GetGValue(B));
			r = (uint)abs(GetRValue(A) - GetRValue(B));

			y = abs(0.299 * r + 0.587 * g + 0.114 * b);
			u = abs(-0.169 * r - 0.331 * g + 0.500 * b);
			v = abs(0.500 * r - 0.419 * g - 0.081 * b);

			return 48 * y + 7 * u + 6 * v;
		};

		// Finds the best match RGB value in the palette.
		auto findClosestRGB = [&](Gdiplus::ARGB c) -> Gdiplus::ARGB {
			Gdiplus::ARGB minRGB = 0;
			double minDist = DBL_MAX;

			for (auto currRGB : palette)
			{
				if (currRGB == c)
					return currRGB;

				else if (currRGB != minRGB)
				{
					double currDist = RGBDistance(currRGB, c);
					if (currDist < minDist)
					{
						minDist = currDist;
						minRGB = currRGB;
					}
				}
			}

			return minRGB;
		};

		// Process pixels in quality bitmap (the big one)
		for (UINT y = 0; y < bigBitmapHeight; y++, pDstData += lockedBigBitmapData.Stride)
		{
			Gdiplus::ARGB* px = reinterpret_cast<Gdiplus::ARGB*>(pDstData);

			for (UINT x = 0; x < bigBitmapWidth; x++, px++)
			{
				// if alpha is less than 0x60,
				// set pixel to transparent...
				if ((*px & 0xFF000000) < 0x60000000)
					*px = 0;

				// ... else find the closest RGB in
				// palette and make pixel fully opaque
				else
					*px = 0xFF000000 | findClosestRGB(*px & 0x00FFFFFF);
			}
		}

		pBigBitmap->UnlockBits(&lockedBigBitmapData);
	}

	// 	CStringW path;
	// 	static int num = 0;
	// 	path.Format(L"C:\\test\\bmp\\pic%d.png", num++);
	// 	dstImg.Save(path);

	// downscale the big bitmap into required size
	VsUI::GdiplusImage rsltImg;
	rsltImg.Create((int)(dpi_scale_x * (double)srcImg.GetWidth()), (int)(dpi_scale_y * (double)srcImg.GetHeight()));
	{
		CAutoPtr<Gdiplus::Graphics> rslt_gr(rsltImg.GetGraphics());
		rslt_gr->ScaleTransform((Gdiplus::REAL)(dpi_scale_x / scale_x), (Gdiplus::REAL)(dpi_scale_y / scale_y),
		                        Gdiplus::MatrixOrderAppend);
		rslt_gr->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		rslt_gr->SetSmoothingMode(Gdiplus::SmoothingModeNone);
		rslt_gr->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
		rslt_gr->Clear(Gdiplus::Color::Transparent);
		rslt_gr->DrawImage(dstImg, 0, 0);
		rslt_gr->Flush();
	}

	VsUI::GdiplusImage::ProcessBitmapBits(rsltImg, [&](Gdiplus::ARGB* pPixelData) {
		// if alpha is less than 0x80,
		// set pixel to transparent...
		if ((*pPixelData & 0xFF000000) < 0x80000000)
			*pPixelData = Gdiplus::Color::Transparent;

		// else make pixel fully opaque
		else
			*pPixelData |= 0xFF000000;
	});

	bmp.DeleteObject();
	bmp.Attach(rsltImg.Detach());
}

void ThemeUtils::LogicalToDeviceBitmap2(CBitmap& bmp, double quality /*= 3.0*/)
{
	// #DPI_HELPER_UNRESOLVED

	if (!VsUI::DpiHelper::IsScalingRequired() || !VsUI::DpiHelper::IsScalingSupported())
	{
		return;
	}

	VsUI::GdiplusImage srcImg;
	srcImg.Attach(bmp);

	Gdiplus::Bitmap* pSrcBitmap = srcImg;
	Gdiplus::BitmapData pSrcLockedData;
	Gdiplus::Rect srcRect(0, 0, (int)pSrcBitmap->GetWidth(), (int)pSrcBitmap->GetHeight());

	double dpi_scale = VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX();
	double scale = quality * dpi_scale;

	VsUI::GdiplusImage dstImg;
	dstImg.Create((int)round(scale * (double)srcImg.GetWidth()), (int)round(scale * (double)srcImg.GetHeight()));

	Gdiplus::Bitmap* pDstBitmap = dstImg;
	Gdiplus::BitmapData pDstLockedData;
	Gdiplus::Rect dstRect(0, 0, dstImg.GetWidth(), dstImg.GetHeight());

	Gdiplus::Status srcLockStatus =
	    pSrcBitmap->LockBits(&srcRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &pSrcLockedData);
	Gdiplus::Status dstLockStatus =
	    pDstBitmap->LockBits(&dstRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &pDstLockedData);

	if (srcLockStatus == Gdiplus::Ok && dstLockStatus == Gdiplus::Ok)
	{
		struct Utils
		{
			static float YUVDistance(Gdiplus::Color A, Gdiplus::Color B)
			{
				float r, g, b;

				b = (float)abs(A.GetB() - B.GetB());
				g = (float)abs(A.GetG() - B.GetG());
				r = (float)abs(A.GetR() - B.GetR());

				return b * b + g * g + r * r;
			}

			// 			static void fixBitmapData(Gdiplus::BitmapData& data, int height)
			// 			{
			// 				if (data.Stride < 0)
			// 				{
			// 					data.Scan0 = (BYTE*)data.Scan0 + data.Stride * (height - 1);
			// 					data.Stride = -data.Stride;
			// 				}
			// 			}

			static void clampXY(const Gdiplus::BitmapData& data, int& x, int& y)
			{
				if (x < 0)
					x = 0;
				else if ((UINT)x >= data.Width)
					x = int(data.Width - 1);

				if (y < 0)
					y = 0;
				else if ((UINT)y >= data.Height)
					y = int(data.Height - 1);
			}

			static Gdiplus::Color getpixel(const Gdiplus::BitmapData& data, int x, int y)
			{
				clampXY(data, x, y);
				int i = y * data.Stride / 4 + x;
				return *((Gdiplus::Color*)data.Scan0 + i);
			}

			static void putpixel(Gdiplus::Color color, Gdiplus::BitmapData& data, int x, int y)
			{
				clampXY(data, x, y);
				int i = y * data.Stride / 4 + x;
				*((Gdiplus::Color*)data.Scan0 + i) = color;
			};

			static float lerp(float s, float e, float t)
			{
				return s + (e - s) * t;
			}

			static float blerp(float c00, float c10, float c01, float c11, float tx, float ty)
			{
				return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
			}

			static BYTE clamp(float value)
			{
				if (value < 0)
					return 0;

				if (value > 255)
					return 255;

				return (BYTE)value;
			}

			static Gdiplus::Color blerpARGB(Gdiplus::Color c0, Gdiplus::Color c1, float frag)
			{
				return Gdiplus::Color(clamp(lerp(c0.GetA(), c1.GetA(), frag)), clamp(lerp(c0.GetR(), c1.GetR(), frag)),
				                      clamp(lerp(c0.GetG(), c1.GetG(), frag)), clamp(lerp(c0.GetB(), c1.GetB(), frag)));
			}

			static Gdiplus::Color blerpARGB(Gdiplus::Color c00, Gdiplus::Color c10, Gdiplus::Color c01,
			                                Gdiplus::Color c11, float tx, float ty)
			{
				return Gdiplus::Color(clamp(blerp(c00.GetA(), c10.GetA(), c01.GetA(), c11.GetA(), tx, ty)),
				                      clamp(blerp(c00.GetR(), c10.GetR(), c01.GetR(), c11.GetR(), tx, ty)),
				                      clamp(blerp(c00.GetG(), c10.GetG(), c01.GetG(), c11.GetG(), tx, ty)),
				                      clamp(blerp(c00.GetB(), c10.GetB(), c01.GetB(), c11.GetB(), tx, ty)));
			}

			static Gdiplus::Color blerpRGB(Gdiplus::Color c00, Gdiplus::Color c10, Gdiplus::Color c01,
			                               Gdiplus::Color c11, float tx, float ty)
			{
				return Gdiplus::Color(clamp(blerp(c00.GetR(), c10.GetR(), c01.GetR(), c11.GetR(), tx, ty)),
				                      clamp(blerp(c00.GetG(), c10.GetG(), c01.GetG(), c11.GetG(), tx, ty)),
				                      clamp(blerp(c00.GetB(), c10.GetB(), c01.GetB(), c11.GetB(), tx, ty)));
			}
		};

		enum px
		{
			LT,
			RT,
			LB,
			RB
		};

		Gdiplus::Color c[4];

		for (int x = 0, y = 0; y < dstRect.Height; x++)
		{
			if (x > dstRect.Width)
			{
				x = 0;
				y++;
			}

			float gx = (float)x / (float)((dstRect.Width) * (srcRect.Width - 1));
			float gy = (float)y / (float)((dstRect.Height) * (srcRect.Height - 1));

			int gxi = (int)gx;
			int gyi = (int)gy;

			c[LT] = Utils::getpixel(pSrcLockedData, gxi, gyi);
			c[RT] = Utils::getpixel(pSrcLockedData, gxi + 1, gyi);
			c[LB] = Utils::getpixel(pSrcLockedData, gxi, gyi + 1);
			c[RB] = Utils::getpixel(pSrcLockedData, gxi + 1, gyi + 1);

			Gdiplus::Color rslt = Utils::blerpARGB(c[0], c[1], c[2], c[3], gx - (float)gxi, gy - (float)gyi);

			// 			Gdiplus::Color min = rslt;
			// 			float minDst = FLT_MAX;
			// 			for (auto curr : c)
			// 			{
			// 				float dst = Utils::YUVDistance(rslt, curr);
			// 				if (dst < minDst)
			// 				{
			// 					minDst = dst;
			// 					min = curr;
			//
			// 					if (minDst <= FLT_EPSILON)
			// 						break;
			// 				}
			// 			}
			//
			// 			//rslt = Utils::blerpARGB(min, rslt, 0.5f);

			Utils::putpixel(rslt, pDstLockedData, x, y);
		}
	}

	if (srcLockStatus == Gdiplus::Ok)
		pSrcBitmap->UnlockBits(&pSrcLockedData);

	if (dstLockStatus == Gdiplus::Ok)
		pDstBitmap->UnlockBits(&pDstLockedData);

	// downscale the big bitmap into required size
	VsUI::GdiplusImage rsltImg;
	rsltImg.Create((int)(dpi_scale * (double)srcImg.GetWidth()), (int)(dpi_scale * (double)srcImg.GetHeight()));
	{
		CAutoPtr<Gdiplus::Graphics> rslt_gr(rsltImg.GetGraphics());
		rslt_gr->ScaleTransform((Gdiplus::REAL)(dpi_scale / scale), (Gdiplus::REAL)(dpi_scale / scale),
		                        Gdiplus::MatrixOrderAppend);
		rslt_gr->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		rslt_gr->SetSmoothingMode(Gdiplus::SmoothingModeNone);
		rslt_gr->SetInterpolationMode(Gdiplus::InterpolationModeBicubic);
		rslt_gr->Clear(Gdiplus::Color::Transparent);
		rslt_gr->DrawImage(dstImg, 0, 0);
		rslt_gr->Flush();
	}

	VsUI::GdiplusImage::ProcessBitmapBits(rsltImg, [&](Gdiplus::ARGB* pPixelData) {
		// if alpha is less than 0x80,
		// set pixel to transparent...
		if ((*pPixelData & 0xFF000000) < 0x80000000)
			*pPixelData = Gdiplus::Color::Transparent;

		// else make pixel fully opaque
		else
			*pPixelData |= 0xFF000000;
	});

	bmp.DeleteObject();
	bmp.Attach(rsltImg.Detach());
}

COLORREF DesaturateColor(COLORREF rgb, BYTE percents)
{
	UINT h, s, l;
	RGBtoHSL(rgb, h, s, l);
	s -= s * percents / 100;
	return HSLtoRGB(h, s, l);
}

bool GetTomatoBitmap(CBitmap& bitmap, int size, COLORREF bgColor)
{
	if (!gVaInteropService)
		return false;

	DWORD colors[6];
	colors[0] = 0xffdd5b28; // tomato color
	colors[1] = 0x80dd5b28; // semi transparent tomato
	colors[2] = 0xff1e7f1e; // green

	// following are added for debugging purpose only, 
	// it does not make difference to theme 3 more colors
	colors[3] = 0xff000000;
	colors[4] = 0xffffffff;
	colors[5] = 0xff808080;

	if (bgColor != CLR_INVALID)
	{
		ThemeColors(colors, 6, bgColor);
	}

	CStringW tomatoXAML = tomatoXAML_FMT;
	CStringW color, colorKey;
	for (int i = 0; i < 3; i++)
	{
		color.Format(L"#%08X", colors[i]);
		colorKey.Format(L"#COLOR%d", i);
		tomatoXAML.Replace(colorKey, color);
	}

	auto hBitmap = gVaInteropService->XamlToBitmap(tomatoXAML, size, size);
	if (hBitmap)
	{
		bitmap.Attach(hBitmap);
		return true;
	}

	return false;
}

bool ThemeColors(DWORD* argb, int count, COLORREF backColor)
{
	return SUCCEEDED(ThemeDIBits((DWORD)count * 4, (BYTE*)argb, (DWORD)count, 1, VARIANT_TRUE, backColor));
}

HRESULT ThemeDIBits(
    DWORD dwBitmapLength,
    BYTE *pBitmap,
    DWORD dwPixelWidth,
    DWORD dwPixelHeight,
    VARIANT_BOOL fIsTopDownBitmap,
    COLORREF crBackground)
{
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO)
	return E_FAIL;
#else

	if (gShellAttr->IsDevenv14OrHigher())
	{
		CComPtr<IVsImageService2> imageService;

		if (SUCCEEDED(
		        gPkgServiceProvider->QueryService(SID_SVsImageService, IID_IVsImageService2, (void**)&imageService)) &&
		    imageService)
		{
			VARIANT_BOOL themed = VARIANT_FALSE;
			return imageService->ThemeDIBits((int)dwBitmapLength, pBitmap, (int)dwPixelWidth, (int)dwPixelHeight, fIsTopDownBitmap, crBackground, &themed);
		}
	}
	else
	{
		CComPtr<IVsUIShell5> uiShell5;
		if (SUCCEEDED(
		        gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell5, (void**)&uiShell5)) &&
		    uiShell5)
		{
			return uiShell5->ThemeDIBits(dwBitmapLength, pBitmap, dwPixelWidth, dwPixelHeight, fIsTopDownBitmap, crBackground);
		}
	}

	return E_FAIL;
#endif
}

void Rect_AdjustToFit(CRect& rectToMove, const CRect& boundingRect)
{
	// Ensure the width and height of the rectToMove is not larger than boundingRect
	if (rectToMove.Width() > boundingRect.Width())
		rectToMove.right = rectToMove.left + boundingRect.Width();
	if (rectToMove.Height() > boundingRect.Height())
		rectToMove.bottom = rectToMove.top + boundingRect.Height();

	// Move the rectToMove within the boundingRect if it is outside
	if (rectToMove.left < boundingRect.left)
		rectToMove.OffsetRect(boundingRect.left - rectToMove.left, 0);
	else if (rectToMove.right > boundingRect.right)
		rectToMove.OffsetRect(boundingRect.right - rectToMove.right, 0);

	if (rectToMove.top < boundingRect.top)
		rectToMove.OffsetRect(0, boundingRect.top - rectToMove.top);
	else if (rectToMove.bottom > boundingRect.bottom)
		rectToMove.OffsetRect(0, boundingRect.bottom - rectToMove.bottom);
}

NS_THEMEUTILS_END
