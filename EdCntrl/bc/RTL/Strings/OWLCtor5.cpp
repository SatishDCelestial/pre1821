/*------------------------------------------------------------------------*/
/*                                                                        */
/*  CTOR5.CPP                                                             */
/*                                                                        */
/*  string::string( const string& s, size_t n );                          */
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


string::string( const string _FAR &s, size_t orig, size_t n )
    throw( xalloc )
{
    if( orig != 0 || n < s.length() )
        {
        size_t loc = min(orig,s.length());
        size_t len = min(s.length()-loc,n);
        p = new TStringRef( s.c_str()+loc, len, 0, 0, 0 );
        }
    else
        {
        p = s.p;
        p->AddReference();
        }
}

