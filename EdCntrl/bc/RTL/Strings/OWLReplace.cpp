/*------------------------------------------------------------------------*/
/*                                                                        */
/*  REPLACE.CPP                                                           */
/*                                                                        */
/*  string& string::replace( size_t pos,                                  */
/*                           size_t n1,                                   */
/*                           const string& s,                             */
/*                           size_t n2 );                                 */
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



string _FAR &string::replace( size_t pos,
                              size_t n1,
                              const string _FAR &s,
                              size_t orig,
                              size_t n2 )
    throw( xalloc, string::outofrange, string::lengtherror )
{
    if( pos > length() )
        throw outofrange();
    cow();
    size_t loc = min(orig,s.length());
    size_t len = min(s.length()-loc,n2);
    p->splice( pos, min(n1,length()-pos), s.c_str()+loc, len );
    return *this;
}

