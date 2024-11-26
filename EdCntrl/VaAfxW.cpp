#include "stdafxed.h"
#include <..\src\mfc\afximpl.h>
#include "VaAfxW.h"

// copied from MFC dlgtempl.cpp
namespace VaDlgTempl
{
/////////////////////////////////////////////////////////////////////////////
// IsDialogEx

AFX_STATIC inline BOOL IsDialogEx(const DLGTEMPLATE* pTemplate)
{
	return ((DLGTEMPLATEEX*)pTemplate)->signature == 0xFFFF;
}

/////////////////////////////////////////////////////////////////////////////
// HasFont

AFX_STATIC inline BOOL HasFont(const DLGTEMPLATE* pTemplate)
{
	return !!(DS_SETFONT & (IsDialogEx(pTemplate) ? ((DLGTEMPLATEEX*)pTemplate)->style : pTemplate->style));
}

/////////////////////////////////////////////////////////////////////////////
// FontAttrSize

AFX_STATIC inline int FontAttrSize(BOOL bDialogEx)
{
	return (int)sizeof(WORD) * (bDialogEx ? 3 : 1);
}
} // namespace VaDlgTempl

BOOL CDialogTemplateW::SetFont(LPCWSTR lpFaceName, WORD nFontSize)
{
	ASSERT(m_hTemplate != NULL);

	if (m_dwTemplateSize == 0)
		return FALSE;

	DLGTEMPLATE* pTemplate = (DLGTEMPLATE*)GlobalLock(m_hTemplate);

	BOOL bDialogEx = VaDlgTempl::IsDialogEx(pTemplate);
	BOOL bHasFont = VaDlgTempl::HasFont(pTemplate);
	int cbFontAttr = VaDlgTempl::FontAttrSize(bDialogEx);

	if (bDialogEx)
		((DLGTEMPLATEEX*)pTemplate)->style |= DS_SETFONT;
	else
		pTemplate->style |= DS_SETFONT;

	int nFaceNameLen = lstrlenW(lpFaceName);
	if (nFaceNameLen >= LF_FACESIZE)
	{
		// Name too long
		return FALSE;
	}

	int cbNew = int(cbFontAttr + ((nFaceNameLen + 1) * sizeof(WCHAR)));
	BYTE* pbNew = (BYTE*)lpFaceName;
	if (cbNew < cbFontAttr)
	{
		return FALSE;
	}
	BYTE* pb = GetFontSizeField(pTemplate);
	int cbOld = (int)(bHasFont ? cbFontAttr + 2 * (wcslen((WCHAR*)(pb + cbFontAttr)) + 1) : 0);

	BYTE* pOldControls = (BYTE*)(((DWORD_PTR)pb + cbOld + 3) & ~DWORD_PTR(3));
	BYTE* pNewControls = (BYTE*)(((DWORD_PTR)pb + cbNew + 3) & ~DWORD_PTR(3));

	WORD nCtrl = bDialogEx ? (WORD)((DLGTEMPLATEEX*)pTemplate)->cDlgItems : (WORD)pTemplate->cdit;

	if (cbNew != cbOld && nCtrl > 0)
	{
		size_t nBuffLeftSize = (size_t)(m_dwTemplateSize - (pOldControls - (BYTE*)pTemplate));
		if (nBuffLeftSize > m_dwTemplateSize)
		{
			return FALSE;
		}
		Checked::memmove_s(pNewControls, nBuffLeftSize, pOldControls, nBuffLeftSize);
	}

	*(WORD*)pb = nFontSize;
	Checked::memmove_s(pb + cbFontAttr, size_t(cbNew - cbFontAttr), pbNew, size_t(cbNew - cbFontAttr));

	m_dwTemplateSize += ULONG(pNewControls - pOldControls);

	GlobalUnlock(m_hTemplate);
	m_bSystemFont = FALSE;
	return TRUE;
}

BOOL CFontW::CreatePointFont(int nPointSize, LPCWSTR lpszFaceName, CDC* pDC /*= NULL*/)
{
	ASSERT(AfxIsValidString(lpszFaceName));

	LOGFONTW logFont;
	memset(&logFont, 0, sizeof(LOGFONTW));
	logFont.lfCharSet = DEFAULT_CHARSET;
	logFont.lfHeight = nPointSize;
	Checked::wcsncpy_s(logFont.lfFaceName, _countof(logFont.lfFaceName), lpszFaceName, _TRUNCATE);

	return CreatePointFontIndirect(&logFont, pDC);
}

BOOL CFontW::CreatePointFontIndirect(const LOGFONTW* lpLogFont, CDC* pDC)
{
	ASSERT(AfxIsValidAddress(lpLogFont, sizeof(LOGFONTW), FALSE));
	HDC hDC;
	if (pDC != NULL)
	{
		ASSERT_VALID(pDC);
		ASSERT(pDC->m_hAttribDC != NULL);
		hDC = pDC->m_hAttribDC;
	}
	else
		hDC = ::GetDC(NULL);

	// convert nPointSize to logical units based on pDC
	LOGFONTW logFont = *lpLogFont;
	POINT pt;
	// 72 points/inch, 10 decipoints/point
	pt.y = ::MulDiv(::GetDeviceCaps(hDC, LOGPIXELSY), logFont.lfHeight, 720);
	pt.x = 0;
	::DPtoLP(hDC, &pt, 1);
	POINT ptOrg = {0, 0};
	::DPtoLP(hDC, &ptOrg, 1);
	logFont.lfHeight = -abs(pt.y - ptOrg.y);

	if (pDC == NULL)
		ReleaseDC(NULL, hDC);

	return CreateFontIndirect(&logFont);
}
