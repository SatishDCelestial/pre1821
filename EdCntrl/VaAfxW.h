#pragma once

#include <afxpriv.h>

class CDialogTemplateW : public CDialogTemplate
{
  public:
	CDialogTemplateW() : CDialogTemplate()
	{
	}

	// modified version of CDialogTemplate::GetFont that supports unicode font in non-unicode build
	BOOL SetFont(LPCWSTR lpFaceName, WORD nFontSize);
};

// added unicode font name in non-unicode build
class CFontW : public CFont
{
  public:
	CFontW() : CFont()
	{
	}

	// nPointSize is actually scaled 10x
	BOOL CreatePointFont(int nPointSize, LPCWSTR lpszFaceName, CDC* pDC = NULL);

	// pLogFont->nHeight is interpreted as PointSize * 10
	BOOL CreatePointFontIndirect(const LOGFONTW* lpLogFont, CDC* pDC);

	BOOL CreateFontIndirect(const LOGFONTW* lpLogFont)
	{
		return Attach(::CreateFontIndirectW(lpLogFont));
	}

	int GetLogFont(LOGFONTW* pLogFont)
	{
		ASSERT(m_hObject != NULL);
		return ::GetObjectW(m_hObject, sizeof(LOGFONTW), pLogFont);
	}
};
