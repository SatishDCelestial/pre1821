/*------------------------------------------------------------------------*/
/*                                                                        */
/*  FNDLASTN.CPP                                                          */
/*                                                                        */
/*  size_t string::find_last_not_of( const string& s, size_t pos ) const; */
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


size_t string::find_last_not_of( const string _FAR &s, size_t pos )
    const throw()
{
    if( pos == 0 )
        return NPOS;
    if( pos > length() )
        pos = length();
    else
        pos++;
    const char *target = s.c_str();
    const char *text = c_str();
    while( --pos > 0 )
        {
        if( strchr( target, text[pos] ) == 0 )
            return pos;
        }
    return NPOS;
}

