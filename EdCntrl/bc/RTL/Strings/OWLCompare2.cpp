/*------------------------------------------------------------------------*/
/*                                                                        */
/*  COMPARE2.CPP                                                          */
/*                                                                        */
/*  int string::compare( const string& s, size_t n ) const;               */
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


int string::compare( const string _FAR &s, size_t orig, size_t n ) const throw()
{
    size_t loc = min(orig,s.length());
    size_t len = min(s.length()-loc,n);
    if( get_case_sensitive_flag() )
        return strncmp( c_str(), s.c_str()+loc, len );
    else
        return _tcsnicmp( c_str(), s.c_str()+loc, len );
}

