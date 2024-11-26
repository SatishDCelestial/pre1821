/*------------------------------------------------------------------------*/
/*                                                                        */
/*  CTOR8.CPP                                                             */
/*                                                                        */
/*  TStringRef::TStringRef( char c, size_t n );                           */
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



//extern xalloc __xalloc;		// sean removed

TStringRef::TStringRef( char c, size_t n ) : TReference(1), flags(0)
{
    nchars = n;
    capacity = round_capacity( nchars );
    array = (char _FAR *)malloc( capacity + 1 );
    ASSERT(array != 0);
//    if( array == 0 )
//        __xalloc.raise();
    memset( array, c, nchars );
    array[nchars] = '\0';
}
