// XThemeHelper.cpp  Version 1.0
//
// Author:  Hans Dietrich
//          hdietrich@gmail.com
//
// Description:
//     XThemeHelper implements CXThemeHelper, a singleton helper class that
//     wraps the functions of UXTHEME.DLL.
//
// History
//     Version 1.1 - 2008 January 12
//     - Added DrawThemeParentBackground()
//
//     Version 1.0 - 2005 March 22
//     - Initial public release
//
// Public APIs:
//                  NAME                               DESCRIPTION
//     ------------------------------   -------------------------------------------
//     IsAppThemed()                    Reports whether the current application's 
//                                      user interface displays using visual styles.
//     IsThemeActive()                  Tests if a visual style for the current 
//                                      application is active.
//     IsThemeLibAvailable()            Test whether UXTHEME.DLL (and its functions) 
//                                      are accessible.
//     CloseThemeData()                 Closes the theme data handle.
//     DrawThemeBackground()            Draws the background image defined by the 
//                                      visual style for the specified control part.
//     DrawThemeParentBackground()      Draws the part of a parent control that is 
//                                      covered by a partially-transparent or 
//                                      alpha-blended child control.
//     DrawThemeText()                  Draws text using the color and font 
//                                      defined by the visual style.
//     GetThemeBackgroundContentRect()  Retrieves the size of the content area 
//                                      for the background defined by the visual 
//                                      style.
//     OpenThemeData()                  Opens the theme data for a window and 
//                                      its associated class.
//
// License:
//     This software is released into the public domain.  You are free to use
//     it in any way you like, except that you may not sell this source code.
//
//     This software is provided "as is" with no expressed or implied warranty.
//     I accept no liability for any damage or loss of business that this
//     software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafxed.h"
#define XTHEMEHELPER_CPP
#include "XThemeHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE [] = __FILE__;
#endif


///////////////////////////////////////////////////////////////////////////////
// ctor
CXThemeHelper::CXThemeHelper()
{
}

///////////////////////////////////////////////////////////////////////////////
// dtor
CXThemeHelper::~CXThemeHelper()
{
}

///////////////////////////////////////////////////////////////////////////////
// CloseThemeData
BOOL CXThemeHelper::CloseThemeData(HTHEME hTheme)
{
	TRACE(_T("in CXThemeHelper::CloseThemeData\n"));

	BOOL ok = FALSE;

	if (hTheme)
	{
		HRESULT hr = ::CloseThemeData(hTheme);
		if (SUCCEEDED(hr))
			ok = TRUE;
	}

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// DrawThemeBackground
BOOL CXThemeHelper::DrawThemeBackground(HTHEME hTheme,
										HDC hdc,
										int iPartId,
										int iStateId,
										const RECT *pRect,
										const RECT *pClipRect)
{
	//TRACE(_T("in CXThemeHelper::DrawThemeBackground\n"));

	BOOL ok = FALSE;

	if (hTheme)
	{
		HRESULT hr = ::DrawThemeBackground(hTheme, hdc, iPartId, iStateId, pRect,  pClipRect);
		if (SUCCEEDED(hr))
			ok = TRUE;
	}

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// DrawThemeParentBackground
BOOL CXThemeHelper::DrawThemeParentBackground(HWND hWnd,
											  HDC hdc,
											  RECT *pRect)
{
	//TRACE(_T("in CXThemeHelper::DrawThemeParentBackground\n"));

	ASSERT(::IsWindow(hWnd));
	ASSERT(pRect);

	BOOL ok = FALSE;

	if (::IsWindow(hWnd) && pRect)
	{
		HRESULT hr = ::DrawThemeParentBackground(hWnd, hdc, pRect);
		if (SUCCEEDED(hr))
			ok = TRUE;
	}

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// DrawThemeText
BOOL CXThemeHelper::DrawThemeText(HTHEME hTheme,
								  HDC hdc,
								  int iPartId,
								  int iStateId,
								  LPCTSTR lpszText,
								  DWORD dwTextFlags,
								  DWORD dwTextFlags2,
								  const RECT *pRect)
{
	//TRACE(_T("in CXThemeHelper::DrawThemeText\n"));

	BOOL ok = FALSE;

	if (hTheme)
	{
		HRESULT hr = S_OK;

#ifdef _UNICODE

		hr = ::DrawThemeText(hTheme, hdc, iPartId, iStateId,
						lpszText, (int)wcslen(lpszText),
						dwTextFlags, dwTextFlags2, pRect);

#else

		int nLen = MultiByteToWideChar(CP_UTF8, 0, lpszText, strlen_i(lpszText)+1, NULL, 0);
		nLen += 2;
		WCHAR * pszWide = new WCHAR[(DWORD)nLen];
		if (pszWide)
		{
			MultiByteToWideChar(CP_UTF8, 0, lpszText, strlen_i(lpszText)+1, pszWide, nLen);
			hr = ::DrawThemeText(hTheme, hdc, iPartId, iStateId,
							pszWide, wcslen_i(pszWide),
							dwTextFlags, dwTextFlags2, pRect);
			delete [] pszWide;
		}

#endif

		if (SUCCEEDED(hr))
			ok = TRUE;
	}

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// GetThemeBackgroundContentRect
BOOL CXThemeHelper::GetThemeBackgroundContentRect(HTHEME hTheme,
												  HDC hdc,
												  int iPartId,
												  int iStateId,
												  const RECT *pBoundingRect,
												  RECT *pContentRect)
{
	BOOL ok = FALSE;

	if (hTheme)
	{
		HRESULT hr = ::GetThemeBackgroundContentRect(hTheme, hdc, iPartId, 
							iStateId, pBoundingRect, pContentRect);
		if (SUCCEEDED(hr))
			ok = TRUE;
	}

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// IsAppThemed
BOOL CXThemeHelper::IsAppThemed()
{
	BOOL ok = FALSE;

	ok = ::IsAppThemed();

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// IsThemeActive
BOOL CXThemeHelper::IsThemeActive()
{
	BOOL ok = FALSE;

	ok = ::IsThemeActive();

	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// OpenThemeData
HTHEME CXThemeHelper::OpenThemeData(HWND hWnd, LPCTSTR lpszClassList)
{
	TRACE(_T("in CXThemeHelper::OpenThemeData\n"));

	HTHEME hTheme = NULL;

#ifdef _UNICODE

	hTheme = ::OpenThemeData(hWnd, lpszClassList);

#else

	int nLen = MultiByteToWideChar(CP_UTF8, 0, lpszClassList,
		strlen_i(lpszClassList)+1, NULL, 0);
	nLen += 2;
	WCHAR * pszWide = new WCHAR [(DWORD)nLen];
	if (pszWide)
	{
		MultiByteToWideChar(CP_UTF8, 0, lpszClassList,
			strlen_i(lpszClassList)+1, pszWide, nLen);
		hTheme = ::OpenThemeData(hWnd, pszWide);
		delete [] pszWide;
	}

#endif

	TRACE(_T("m_hTheme=%p\n"), hTheme);

	return hTheme;
}
