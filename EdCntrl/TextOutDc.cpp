#include "StdAfxEd.h"
#include "TextOutDc.h"
#include "MenuXP\Tools.h"
#include "WTString.h"
#include "StringUtils.h"
#include "VAThemeUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

int TextOutDc::DrawTextA(const CStringA& str, LPRECT lpRect, UINT nFormat)
{
	if (wvWin8 > ::GetWinVersion())
	{
		// pass-through
		return CDC::DrawTextA(str, lpRect, nFormat);
	}

	const CStringW txt(str);
	return ::VaDrawTextW(m_hDC, txt, lpRect, nFormat);
}

int TextOutDc::DrawTextA(const LPCSTR str, int len, LPRECT lpRect, UINT nFormat)
{
	return ::VaDrawTextW(m_hDC, CStringW(str, len), lpRect, nFormat);
}

int TextOutDc::DrawTextW(const CStringW& str, LPRECT lpRect, UINT nFormat)
{
	return ::VaDrawTextW(m_hDC, str, lpRect, nFormat);
}

// [case: 75892]
// CDC::DrawText and CDC::DrawTextEx do something funky on Windows 8.1.
// Our ScriptShape intercept doesn't get hit.
// Workaround by doing some work on behalf of DrawText callers and then
// use TextOut instead.
// Problem also occurs on Windows 8 when connected via RDP.

int VaDrawTextW(HDC dc, const CStringW& str, LPRECT lpRect, UINT nFormat)
{
	return VaDrawTextW(dc, str, lpRect, nFormat, false);
}

int VaDrawTextW(HDC m_hDC, const CStringW& str, LPRECT lpRect, UINT nFormat, bool ignore_OS_version)
{
	if (!ignore_OS_version && wvWin8 > ::GetWinVersion())
	{
		// pass-through
		return ::DrawTextW(m_hDC, str, str.GetLength(), lpRect, nFormat);
	}

	if (PaintType::in_WM_PAINT == PaintType::None)
	{
		return ::DrawTextW(m_hDC, str, str.GetLength(), lpRect, nFormat);
	}

	CStringW txt(str);
	if (nFormat & DT_SINGLELINE)
	{
		txt.Replace(L"\r\n", L" ");
		txt.Replace(L'\n', L' ');
	}

	if (nFormat & DT_CALCRECT)
		return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);
	if (nFormat & DT_PATH_ELLIPSIS) // file paths do not get colored
		return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);
	if (nFormat & DT_PREFIXONLY) // prefixes are still not colored
		return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);
	// DrawText ellipsis operations are not supported in this conversion to TextOut
	if (nFormat & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS))
		return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);

	// just some of the DrawText operations that are not supported in this conversion to TextOut
	_ASSERTE(!(nFormat & (DT_TABSTOP | DT_EXTERNALLEADING | DT_EXPANDTABS | DT_MODIFYSTRING)));

	CRect boundingRect(lpRect);
	CRect calcedRect(boundingRect);
	const int txtHeight = ::DrawTextW(m_hDC, txt, txt.GetLength(), calcedRect, nFormat | DT_CALCRECT);
	if (!txtHeight)
	{
		// error; just pass-through
		return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);
	}

	if (calcedRect.Height() > boundingRect.Height())
		calcedRect.bottom = boundingRect.bottom;

	if (calcedRect.Width() > boundingRect.Width())
		calcedRect.right = boundingRect.right;

	_ASSERTE(!(nFormat & (DT_RIGHT | DT_CENTER))); // not implemented
	_ASSERTE(!(nFormat & DT_BOTTOM));              // not implemented
	int xPos = calcedRect.left;                    // DT_LEFT
	int yPos = calcedRect.top;                     // DT_TOP
	if (nFormat & DT_VCENTER)
	{
		const int newHt = calcedRect.Height();
		const int oldHt = boundingRect.Height();
		if (newHt < oldHt)
		{
			// this fixes text y position in tree items (relative to icon)
			yPos += ((oldHt - newHt) / 2);
		}
	}

	{
		// Align is currently set to default TextOutW settings.
		// More changes would need more testing of all affected parts.
		ThemeUtils::AutoTextAlign ata(m_hDC, TA_NOUPDATECP | TA_LEFT | TA_TOP);

		DWORD eto_flags = 0;

		// This ensures that text is clipped by lpRect
		if ((nFormat & DT_NOCLIP) != DT_NOCLIP)
			eto_flags |= ETO_CLIPPED;

		if ((nFormat & DT_NOPREFIX) == DT_NOPREFIX || txt.Find(L'&') < 0)
		{
			// DT_NOPREFIX turns off &'s prefixes handling
			if (::ExtTextOutW(m_hDC, xPos, yPos, eto_flags, lpRect, txt, (UINT)txt.GetLength(), nullptr))
				return txtHeight;
		}
		else
		{
			// following regex replaces "&" by "" and "&&" by "&".
			static std::wregex rgx(L"&(&?)", std::regex::ECMAScript | std::regex::optimize);
			std::wstring wstr = std::regex_replace((LPCWSTR)txt, rgx, L"$1");

			// draw new string (w/o mnemonic-prefix &'s)
			if (::ExtTextOutW(m_hDC, xPos, yPos, eto_flags, lpRect, wstr.c_str(), (uint)wstr.length(), nullptr))
			{
				// draw only underlines by use of DT_PREFIXONLY
				// but only if DT_HIDEPREFIX bit is not set
				if ((nFormat & DT_HIDEPREFIX) != DT_HIDEPREFIX)
					::DrawTextW(m_hDC, txt, txt.GetLength(), calcedRect, nFormat | DT_PREFIXONLY);

				return txtHeight;
			}
		}
	}

	// something didn't work right; just pass-through
	return ::DrawTextW(m_hDC, txt, txt.GetLength(), lpRect, nFormat);
}

int VaDrawSingleLineTextWithEndEllipsisW(HDC dc, const CStringW& sLabel, LPRECT lpRect, UINT nFormat)
{
	INT num_chars_fit;         // how many chars from string fits into range
	CArray<INT> width_at_char; // string widths at each char
	CSize txt_size;            // unused text size (if we would need height, this could be used)
	CRect rcLabel(lpRect);
	int result = 0;

	// size must equal to chars in text buffer
	width_at_char.SetSize(sLabel.GetLength());

	// pass default if we have empty label/array
	if (width_at_char.GetCount() == 0)
		return DrawTextW(dc, sLabel, sLabel.GetLength(), rcLabel, nFormat);

	// forcing some flags
	nFormat |= DT_SINGLELINE;
	nFormat &= ~DT_END_ELLIPSIS;

	// measure text and fill arrays with partial widths
	if (GetTextExtentExPointW(dc, sLabel, sLabel.GetLength(), rcLabel.Width(), &num_chars_fit, width_at_char.GetData(),
	                          &txt_size))
	{
		// if string fits range, draw it directly... it's OK
		if (num_chars_fit == sLabel.GetLength())
			result = VaDrawTextW(dc, sLabel, rcLabel, nFormat);
		else
		{
			// what we are going to do is:
			// - find the best position to place ellipsis
			// - reduce output range to calculated width
			// - draw original text into new range
			// - draw ellipsis behind new range

			SIZE ellipsis_size;
			if (ThemeUtils::GetTextSizeW(dc, L"...", 3, &ellipsis_size))
			{
				INT max_width = rcLabel.Width();
				LONG max_right = rcLabel.right;

				if (max_width > ellipsis_size.cx + width_at_char[0])
				{
					for (INT i = num_chars_fit - 1; i >= 0; i--)
					{
						// find the best position to place ellipsis
						if (ellipsis_size.cx + width_at_char[i] <= max_width)
						{
							// reduce output range width to calculated one
							rcLabel.right = rcLabel.left + width_at_char[i];

							if (rcLabel.right > max_right)
								rcLabel.right = max_right;

							// draw original text, so highlighting is preserved!!!
							result = VaDrawTextW(dc, sLabel, rcLabel, nFormat);

							if (rcLabel.right != max_right)
							{
								// move rcLabel to behind of itself and set its width to ellipsis width
								rcLabel.left = rcLabel.right;
								rcLabel.right = rcLabel.left + ellipsis_size.cx;

								if (rcLabel.right > max_right)
									rcLabel.right = max_right;

								// draw custom ellipsis
								VaDrawTextW(dc, _T("..."), rcLabel, nFormat);
							}

							break;
						}
					}
				}

				// if dest. rect is too small, show first char and clip
				if (!result)
				{
					// reduce output range width to calculated one
					rcLabel.right = rcLabel.left + width_at_char[0];

					if (rcLabel.right > max_right)
						rcLabel.right = max_right;

					// draw original text, so highlighting is preserved!!!
					result = VaDrawTextW(dc, sLabel, rcLabel, nFormat);

					if (rcLabel.right != max_right)
					{
						// move rcLabel to behind of itself and set its width to ellipsis width
						rcLabel.left = rcLabel.right;
						rcLabel.right = rcLabel.left + ellipsis_size.cx;

						if (rcLabel.right > max_right)
							rcLabel.right = max_right;

						// draw custom ellipsis
						VaDrawTextW(dc, _T("..."), rcLabel, nFormat);
					}
				}
			}
		}
	}

	// this should never happen, but for sure...
	if (!result)
		result = VaDrawTextW(dc, sLabel, rcLabel, nFormat);

	return result;
}
