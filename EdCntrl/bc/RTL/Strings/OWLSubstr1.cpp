/*------------------------------------------------------------------------*/
/*                                                                        */
/*  SUBSTR1.CPP                                                           */
/*                                                                        */
/*  string string::substr( size_t pos ) const;                            */
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


string string::substr( size_t pos ) const throw( xalloc, string::outofrange )
{
    assert_index(pos);
    return string( c_str()+pos );
}
