#include "StdAfxEd.h"
#include "DebugStream.h"

#if defined(_DEBUG) && !defined(SEAN)

std::wstring debug::to_string(const POINT& pt)
{
	std::wstringstream ss;
	ss << "POINT { X: " << pt.x << ", Y: " << pt.y << " }";
	return ss.str();
}

std::wstring debug::to_string(const WINDOWPOS& wp)
{
#ifndef __SWP_APPEND_STRING
#define __SWP_APPEND_STRING(x) \
	if ((wp.flags & x) == x)   \
	{                          \
		if (!str.empty())      \
			str += L" | ";     \
                               \
		str += L#x;            \
	}

	std::wstring str;

	__SWP_APPEND_STRING(SWP_NOSIZE);
	__SWP_APPEND_STRING(SWP_NOMOVE);
	__SWP_APPEND_STRING(SWP_NOZORDER);
	__SWP_APPEND_STRING(SWP_NOREDRAW);
	__SWP_APPEND_STRING(SWP_NOACTIVATE);
	__SWP_APPEND_STRING(SWP_FRAMECHANGED);
	__SWP_APPEND_STRING(SWP_SHOWWINDOW);
	__SWP_APPEND_STRING(SWP_HIDEWINDOW);
	__SWP_APPEND_STRING(SWP_NOCOPYBITS);
	__SWP_APPEND_STRING(SWP_NOOWNERZORDER);
	__SWP_APPEND_STRING(SWP_NOSENDCHANGING);
#undef __SWP_APPEND_STRING
#endif

	std::wstringstream ss;
	ss << L"WINDOWPOS { X: " << wp.x << L", Y: " << wp.y << L", CX: " << wp.cx << L", CY: " << wp.cy << L", FLAGS: "
	   << str << L" }";
	return ss.str();
}

std::wstring debug::to_string(const RECT& rc)
{
	std::wstringstream ss;
	ss << L"RECT { L: " << rc.left << L", T: " << rc.top << L", R: " << rc.right << L", B: " << rc.bottom << L" { W: "
	   << rc.right - rc.left << L", H: " << rc.bottom - rc.top << L" } }";
	return ss.str();
}

std::wstring debug::to_string(const SIZE& size)
{
	std::wstringstream ss;
	ss << L"SIZE { CX: " << size.cx << L", CY: " << size.cy << L" }";
	return ss.str();
}

std::wstring debug::to_string(const LOGFONTW& lf)
{
	std::wstringstream ss;
	ss << L"FONT { name: " << lf.lfFaceName << L", H: " << lf.lfHeight << L", W: " << lf.lfWidth << L", WG: "
	   << lf.lfWeight << L" }";
	return ss.str();
}

#endif