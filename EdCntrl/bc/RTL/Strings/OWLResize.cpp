/*------------------------------------------------------------------------*/
/*                                                                        */
/*  RESIZE.CPP                                                            */
/*                                                                        */
/*  void string::resize( size_t n );                                      */
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


void string::resize( size_t n )
{
    cow();
    if( n>length() )
        p->splice( length(), 0, 0, n-length() );      // Grew
    else
        p->splice( n, length()-n, 0, 0 );             // Shrunk
}

