#pragma once

#include "utils_goran.h"

class CDoubleBuffer
{
  public:
	CDoubleBuffer(const HWND& hwnd) : hwnd(hwnd)
	{ // beware: don't give temporary object!!
	}
	virtual ~CDoubleBuffer()
	{
	}

	CBitmap& GetBitmap(HDC hdc, COLORREF bgColor);

	HWND GetWnd() const
	{
		return hwnd;
	}

	void HandleEraseBkgndForNicerResize(HDC hdc);

  protected:
	const HWND& hwnd;
	CBitmap bitmap;
	CSize size;
	std::optional<CRect> last_rect; // used in HandleEraseBkgndForNicerResize
};

class CDoubleBufferedDC : public CDC
{
  public:
	CDoubleBufferedDC(CDoubleBuffer& db, HDC hdc, COLORREF bgColor, CRect* wanted_rect = NULL);
	virtual ~CDoubleBufferedDC();

  protected:
	CDoubleBuffer& db;
	HDC destdc;
	int state;
	CRect* wanted_rect;
};
