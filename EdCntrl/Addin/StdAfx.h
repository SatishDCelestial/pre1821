// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__62F53220_142B_11D1_9291_9DE84EB1A651__INCLUDED_)
#define AFX_STDAFX_H__62F53220_142B_11D1_9291_9DE84EB1A651__INCLUDED_

#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers

#ifndef WINVER
#if _MSC_VER <= 1200
#define WINVER 0x0400
#else
#ifdef _WIN64
#define WINVER 0x0a00
#else
#define WINVER _WIN32_WINNT_VISTA
#endif
#endif
#endif

#if _MSC_VER > 1200
#define INCL_WINSOCK_API_PROTOTYPES 0
#endif

#pragma warning(disable : 4711)

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#include "../StdAfxEd.h"

#pragma warning(push, 2)
#include <afxwin.h> // MFC core and standard components
#include <afxdisp.h>
#include <afxdlgs.h>
#include <atlbase.h>
#include <afxcmn.h>
#include <atlcom.h>
#pragma warning(pop)

#endif // !defined(AFX_STDAFX_H__62F53220_142B_11D1_9291_9DE84EB1A651__INCLUDED)
