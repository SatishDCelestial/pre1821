#include "stdafxed.h"
#include "VAWatermarks.h"
#include "WTString.h"


static const CString title_watermark = " -- Visual Assist";
static const CString title_watermark_front = "VA ";

extern bool gTestsActive;

bool VAUpdateWindowTitle(VAWindowType wintype, CString& title, int reason)
{
	if (gTestsActive)
		return false;

	if (wintype == VAWindowType::SortSelectedLines)
	{
		if (StartsWith(title, title_watermark_front, false, true))
			return false;

		title = title_watermark_front + title;
		return true;
	}

	if(EndsWith(title, title_watermark, false, true))
		return false;

	title += title_watermark;
	return true;
}
bool VAUpdateWindowTitle(VAWindowType wintype, WTString& title, int reason)
{
	if (gTestsActive)
		return false;

	if (wintype == VAWindowType::SortSelectedLines)
	{
		if (title.begins_with((const char *)title_watermark_front))
			return false;

		title = title_watermark_front + title;
		return true;
	}

	if (title.EndsWith(title_watermark))
		return false;

	title += title_watermark;
	return true;
}

WTString VAUpdateWindowTitle_c(VAWindowType wintype, WTString title, int reason)
{
	VAUpdateWindowTitle(wintype, title, reason);
	return title;
}

bool VAUpdateWindowTitle(VAWindowType wintype, HWND hwnd, int reason)
{
	if (!hwnd || gTestsActive)
		return false;

	char title[512];
	title[0] = 0;
	if(::GetWindowTextA(hwnd, title, _countof(title)) > 0)
	{
		CString title2 = title;
		if(VAUpdateWindowTitle(wintype, title2, reason))
		{
			::SetWindowTextA(hwnd, title2);
			return true;
		}
	}

	return false;
}

