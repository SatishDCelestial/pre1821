/*------------------------------------------------------------------------*/
/*                                                                        */
/*  APPEND3.CPP                                                           */
/*                                                                        */
/*  string& string::append( const char *cp, size_t n );                   */
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



string _FAR & string::append( const char _FAR *cp, size_t orig, size_t n )
    throw( xalloc, string::lengtherror )
{
    if( cp != 0 )
        {
        cow();
        size_t slen = strlen(cp);
        size_t loc = min(orig,slen);
        size_t len = min(slen-loc,n);
        p->splice( p->nchars, 0, cp+loc, len );
        }
    return *this;
}

