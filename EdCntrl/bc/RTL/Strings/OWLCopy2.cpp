/*------------------------------------------------------------------------*/
/*                                                                        */
/*  COPY2.CPP                                                             */
/*                                                                        */
/*  size_t string::copy( char *cb, size_t n, size_t pos );                */
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


size_t string::copy( char _FAR *cb, size_t n, size_t pos )
    throw( string::outofrange )
{
    if( n > length()-pos )
        n = length()-pos;
    memcpy( cb, c_str()+pos, n );
    return n;
}
