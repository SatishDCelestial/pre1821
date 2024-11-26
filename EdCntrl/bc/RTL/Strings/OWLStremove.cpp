/*------------------------------------------------------------------------*/
/*                                                                        */
/*  REMOVE.CPP                                                            */
/*                                                                        */
/*  string& string::remove( size_t pos, size_t n );                       */
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


string _FAR &string::remove( size_t pos, size_t n )
    throw( xalloc, string::outofrange )
{
    if( pos > length() )
        throw outofrange();
    cow();
    p->splice( pos, min(n,length()-pos), 0, 0 );
    return *this;
}

