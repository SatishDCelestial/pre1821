/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPRCALL3.CPP                                                          */
/*                                                                        */
/*  TSubString string::operator()( const TRegexp& r, size_t start );      */
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
#include "regexp.h"

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#undef THIS_FILE
//static char THIS_FILE[] = __FILE__;
//#endif

using namespace OWL;


TSubString string::operator()( const TRegexp _FAR &r, size_t start ) throw()
{
    size_t len;
    size_t begin = find( r, &len, start );
    return TSubString( this, begin, len );
}

