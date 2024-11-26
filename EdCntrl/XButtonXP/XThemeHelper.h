// XThemeHelper.h  Version 1.0
//
// Author:  Hans Dietrich
//          hdietrich@gmail.com
//
// This software is released into the public domain.  You are free to use
// it in any way you like, except that you may not sell this source code.
//
// This software is provided "as is" with no expressed or implied warranty.
// I accept no liability for any damage or loss of business that this
// software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef  XTHEMEHELPER_H
#define  XTHEMEHELPER_H

///////////////////////////////////////////////////////////////////////////////
// NOTE:  following two files are available in Microsoft Platform SDK.
///////////////////////////////////////////////////////////////////////////////

#include "uxtheme.h"

///////////////////////////////////////////////////////////////////////////////
//
// CXThemeHelper class definition
//
class CXThemeHelper
{
// Construction
public:
	CXThemeHelper();
	virtual ~CXThemeHelper();

// Attributes
public:
	BOOL IsAppThemed();
	BOOL IsThemeActive();

// Operations
public:
	BOOL	CloseThemeData(HTHEME hTheme);
	BOOL	DrawThemeBackground(HTHEME hTheme,
								HDC hdc,
								int iPartId,
								int iStateId,
								const RECT *pRect,
								const RECT *pClipRect);
	BOOL	DrawThemeParentBackground(HWND hWnd,
									  HDC hdc,
									  RECT *pRect);
	BOOL	DrawThemeText(HTHEME hTheme,
						  HDC hdc,
						  int iPartId,
						  int iStateId,
						  LPCTSTR lpszText,
						  DWORD dwTextFlags,
						  DWORD dwTextFlags2,
						  const RECT *pRect);
	BOOL	GetThemeBackgroundContentRect(HTHEME hTheme,
										  HDC hdc,
										  int iPartId,
										  int iStateId,
										  const RECT *pBoundingRect,
										  RECT *pContentRect);
	HTHEME	OpenThemeData(HWND hWnd, LPCTSTR lpszClassList);

// Implementation
private:
};

///////////////////////////////////////////////////////////////////////////////
//
// CXThemeHelper instance
//
#ifndef XTHEMEHELPER_CPP
// include an instance in each file;  the namespace insures uniqueness
namespace { static CXThemeHelper ThemeHelper; }
#endif

#endif // XTHEMEHELPER_H
