/*------------------------------------------------------------------------*/
/*                                                                        */
/*  COMPARE1.CPP                                                          */
/*                                                                        */
/*  int string::compare( const string& s ) const;                         */
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


int string::compare( const string _FAR &s ) const throw()
{
    if( get_case_sensitive_flag() )
        return strcmp( c_str(), s.c_str() );
    else
        return _tcsicmp( c_str(), s.c_str() );
}

