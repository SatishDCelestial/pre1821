/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPRASGN2.CPP                                                          */
/*                                                                        */
/*  TRegexp& TRegexp::operator = ( const TRegexp& r );                    */
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


TRegexp _FAR & TRegexp::operator = ( const TRegexp _FAR & r ) throw( xalloc )
{
    if(this != &r)
        {
//        delete the_pattern;
        copy_pattern( r );
        }
    return *this;
}

