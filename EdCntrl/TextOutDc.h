#pragma once

class TextOutDc : public CDC
{
  public:
	TextOutDc() : CDC()
	{
	}

	virtual int DrawTextA(LPCSTR str, int len, LPRECT lpRect, UINT nFormat);
	int DrawTextA(const CStringA& str, LPRECT lpRect, UINT nFormat);
	int DrawTextW(const CStringW& str, LPRECT lpRect, UINT nFormat);
};

int VaDrawTextW(HDC dc, const CStringW& str, LPRECT lpRect, UINT nFormat, bool ignore_OS_version);
int VaDrawTextW(HDC dc, const CStringW& str, LPRECT lpRect, UINT nFormat);

// [case: 78263]
// This case relates to [case: 75892], but includes FSIS dialog.
// FSIS dialog needs extra handling for ellipsis.
// This function removes DT_END_ELLIPSIS from nFormat and provides
// custom ellipsis handling for text that does not fit into lpRect.
// That results into correct syntax highlighting of trimmed symbols.
int VaDrawSingleLineTextWithEndEllipsisW(HDC dc, const CStringW& sLabel, LPRECT lpRect, UINT nFormat);
