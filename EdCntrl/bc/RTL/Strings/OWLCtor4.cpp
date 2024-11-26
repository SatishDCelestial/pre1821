/*------------------------------------------------------------------------*/
/*                                                                        */
/*  CTOR4.CPP                                                             */
/*                                                                        */
/*  string::string( const char *pstr, size_t n );                         */
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


string::string( const char _FAR *pstr, size_t orig, size_t n )
    throw( xalloc, string::lengtherror )
{
    size_t slen = strlen( pstr );
    size_t loc = min(orig,slen);
    size_t len = min(slen-loc,n);
    p = new TStringRef( pstr+loc, len, 0, 0, 0 );
}

