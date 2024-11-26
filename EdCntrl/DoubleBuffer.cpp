#include "StdAfxEd.h"
#include "DoubleBuffer.h"
#include "IdeSettings.h"
#include "DevShellAttributes.h"
#include "ColorListControls.h"
#include "WtException.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CBitmap& CDoubleBuffer::GetBitmap(HDC hdc, COLORREF bgColor)
{
	CRect rect;
	if (hwnd)
		::GetWindowRect(hwnd, rect);
	else
		rect.SetRect(0, 0, 1, 1);

	CBitmap previous_bitmap;
	if (bitmap.m_hObject && ((size.cx < rect.Width()) || (size.cy < rect.Height())))
		previous_bitmap.Attach(bitmap.Detach());

	if (!bitmap.m_hObject)
	{
		size.cx = rect.Width() * 5 / 4;
		if (!size.cx)
			size.cx = 1;
		size.cy = rect.Height() * 5 / 4;
		if (!size.cy)
			size.cy = 1;
		HDC origdc = ::GetDC(hwnd); // rather use windowdc; if dc is from createcompatibledc(null), bitmap will be mono
		if (origdc)
		{
			if (bitmap.CreateCompatibleBitmap(CDC::FromHandle(origdc), size.cx, size.cy))
			{
				::ReleaseDC(hwnd, origdc);

				if (previous_bitmap.m_hObject)
				{
					CDC prev;
					if (prev.CreateCompatibleDC(CDC::FromHandle(hdc)))
					{
						prev.SelectObject(&previous_bitmap);
						CBrush brush;
						if (brush.CreateSolidBrush(bgColor))
						{
							CDC curr;
							if (curr.CreateCompatibleDC(CDC::FromHandle(hdc)))
							{
								curr.SelectObject(&bitmap);
								curr.FillRect(CRect(CPoint(0, 0), size),
								              &brush); // better visual appearance while resizing window
								curr.BitBlt(0, 0, rect.Width(), rect.Height(), &prev, 0, 0, SRCCOPY);
							}
						}
					}
				}
			}
			else
				::ReleaseDC(hwnd, origdc);
		}
	}

	return bitmap;
}

void CDoubleBuffer::HandleEraseBkgndForNicerResize(HDC hdc)
{
	// during resize, fill new areas with WINDOW_COLOR
	CRect rect;
	::GetClientRect(hwnd, rect);

	if (last_rect && (rect != *last_rect))
	{
		CRgn rgn;
		CRgn rgn2;
		if (rgn.CreateRectRgnIndirect(rect) && rgn2.CreateRectRgnIndirect(*last_rect))
		{
			if (rgn.CombineRgn(&rgn, &rgn2, RGN_DIFF) != NULLREGION)
			{
				BOOL res;
				CBrush br;
				if (gShellAttr->IsDevenv11OrHigher())
					res = br.CreateSolidBrush(g_IdeSettings->GetEnvironmentColor(L"Window", false));
				else
					res = br.CreateSysColorBrush(COLOR_WINDOW);
				if (res)
					::FillRgn(hdc, rgn, br);
			}
		}
	}
	last_rect = rect;
}

CDoubleBufferedDC::CDoubleBufferedDC(CDoubleBuffer& db, HDC hdc, COLORREF bgColor, CRect* wanted_rect /*= NULL*/)
    : db(db), destdc(hdc), wanted_rect(wanted_rect)
{
	if (!CreateCompatibleDC(CDC::FromHandle(hdc)))
		throw WtException("CDoubleBufferedDC failed to create DC");
	state = SaveDC();
	SelectObject(db.GetBitmap(hdc, bgColor));
}

CDoubleBufferedDC::~CDoubleBufferedDC()
{
	if (!db.GetWnd())
		return;

	if (wanted_rect)
	{
		::BitBlt(destdc, wanted_rect->left, wanted_rect->top, wanted_rect->Width(), wanted_rect->Height(), *this,
		         wanted_rect->left, wanted_rect->top, SRCCOPY);
	}
	else
	{
		CRect rect;
		::GetClientRect(db.GetWnd(), rect);
		if (!rect.Width() || !rect.Height())
			return;
		::BitBlt(destdc, 0, 0, rect.Width(), rect.Height(), *this, 0, 0, SRCCOPY);
	}

	RestoreDC(state);
}
