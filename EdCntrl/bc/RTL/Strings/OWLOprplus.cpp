/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPRPLUS.CPP                                                           */
/*                                                                        */
/*  string operator + ( const char *cp, const string& s );                */
/*                                                                        */
/*------------------------------------------------------------------------*/

/*
 *      C/C++ Run Time Library - Version 8.0
 *
 *      Copyright (c) 1992, 1997 by Borland International
 *      All Rights Reserved.
 *
 */
/* $Revision:   8.2  $        */

#include "stdafx.h"
#include <string.h>
#include "cstring.h"

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#undef THIS_FILE
//static char THIS_FILE[] = __FILE__;
//#endif


using namespace OWL;


string _Cdecl _FARFUNC operator + ( const char _FAR *cp,
                                    const string _FAR &s )
    throw( xalloc, string::lengtherror )
{
    return string(cp) + s.c_str();
}

