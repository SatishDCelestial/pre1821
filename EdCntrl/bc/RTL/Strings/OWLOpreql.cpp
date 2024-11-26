/*------------------------------------------------------------------------*/
/*                                                                        */
/*  OPREQL.CPP                                                            */
/*                                                                        */
/*  int TSubString::operator == ( const char *cp );                       */
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


int TSubString::operator == ( const char _FAR *cp ) const throw()
{
    if( cp == 0 )
        cp = "";

    if( is_null())
        return 0;

    if( string::get_case_sensitive_flag() )
        return !strncmp( s->p->array + begin, cp, extent );
    else
        return !_tcsnicmp( s->p->array + begin, cp, extent );
}

