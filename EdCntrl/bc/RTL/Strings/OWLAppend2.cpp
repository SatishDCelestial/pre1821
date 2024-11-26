/*------------------------------------------------------------------------*/
/*                                                                        */
/*  APPEND2.CPP                                                           */
/*                                                                        */
/*  string& string::append( const char *cp );                             */
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


string _FAR & string::append( const char _FAR *cp )
    throw( xalloc, string::lengtherror )
{
    if( cp != 0 )
        {
        cow();
        p->splice( p->nchars, 0, cp, strlen(cp) );
        }
    return *this;
}

