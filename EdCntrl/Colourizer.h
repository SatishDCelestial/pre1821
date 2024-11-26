#pragma once
#include "SyntaxColoring.h"
#include <memory>

class CColourizedControl
{
  public:
	enum patch
	{
		patch_nothing = 0x0000,
		patch_wndproc = 0x0001,
		patch_getdc = 0x0002,
		patch_releasedc = 0x0004
	};

	CColourizedControl(HWND hwnd) : _hwnd(hwnd), mPaintType(PaintType::None) // None is unspecified
	{
	}

	virtual ~CColourizedControl()
	{
	}

	virtual patch WhatToPatch() const
	{
		return patch_nothing;
	}
	virtual const char* GetClassName() const = 0;

	virtual LRESULT ControlWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC WndProcNext)
	{
		return 0;
	}
	virtual HDC GetDCHook(HWND hwnd, HDC(WINAPI* GetDCHookNext)(HWND hwnd))
	{
		return 0;
	}
	virtual int ReleaseDCHook(HWND hwnd, HDC hdc, int(WINAPI* ReleaseDCHookNext)(HWND hwnd, HDC hdc))
	{
		return 0;
	}

	const HWND _hwnd;
	void SetPaintType(int pt)
	{
		mPaintType = pt;
	}

  protected:
	int mPaintType;
};

typedef std::shared_ptr<CColourizedControl> CColourizedControlPtr;

#if _MSC_VER >= 1900 // vs2015
namespace stdext
{
using _STD size_t;

#ifndef _HASH_SEED
#define _HASH_SEED (size_t)0xdeadbeef
#endif

inline size_t hash_value(const CColourizedControlPtr& _Keyval)
{
	return ((size_t)_Keyval.get() ^ _HASH_SEED);
}
} // namespace stdext
#endif

void RegisterColourizedControlClass(const char* classname, CColourizedControlPtr (*Instance)(HWND hwnd));

CColourizedControlPtr ColourizeControl(CWnd* parent, int id);
CColourizedControlPtr ColourizeControl(CWnd* control);
void ReleaseColourizerHooks();
