/*------------------------------------------------------------------------*/
/*                                                                        */
/*  FNDFRST.CPP                                                           */
/*                                                                        */
/*  size_t string::find_first_of( const string& s, size_t pos ) const;    */
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


size_t string::find_first_of( const string _FAR &s, size_t pos ) const throw()
{
    if( !valid_element(pos) )
        return NPOS;
    char _FAR * f = strpbrk( p->array+pos, s.c_str() );
    return f ? (size_t)(f-p->array) : NPOS;
}

