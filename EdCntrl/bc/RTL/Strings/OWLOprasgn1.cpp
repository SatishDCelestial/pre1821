/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPRASGN1.CPP                                                          */
/*                                                                        */
/*  TSubString& TSubString::operator = ( const string& str );             */
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



TSubString _FAR & TSubString::operator = ( const string _FAR &str ) throw()
{
    if( !is_null() )
        {
        s->cow();
        s->p->splice(begin, min(extent,str.length()),
                     str.c_str(), str.length());
        }
    return *this;
}

