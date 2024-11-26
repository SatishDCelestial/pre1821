/*------------------------------------------------------------------------*/
/*                                                                        */
/*  COPY3.CPP                                                             */
/*                                                                        */
/*  string string::copy() const;                                          */
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


string string::copy() const throw( xalloc )
{
    return string(*this, (char _FAR *)0); /* actual copy. Not ++reference */
}

