/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPRCALL2.CPP                                                          */
/*                                                                        */
/*  char& TSubString::operator()( size_t pos );                           */
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


char _FAR & TSubString::operator()( size_t pos ) throw( string::outofrange )
{
    assert_element(pos);
    s->cow();
    return s->p->array[begin+pos];
}

