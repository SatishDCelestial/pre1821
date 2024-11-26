/*------------------------------------------------------------------------*/
/*                                                                        */
/*  FIND3.CPP                                                             */
/*                                                                        */
/*  size_t string::find( const TRegexp& r, size_t start ) const;          */
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


size_t string::find( const TRegexp _FAR &r, size_t start ) const throw()
{
    size_t len;
    return r.find( *this, &len, start ); // len not used
}

