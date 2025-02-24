/*------------------------------------------------------------------------*/
/*                                                                        */
/*  STRIP.CPP                                                             */
/*                                                                        */
/*  TSubString string::strip( string::StripType st, char c );             */
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


TSubString string::strip( string::StripType st, char c )
{
    size_t start = 0;                        // index of first character
    size_t end = length()-1;                 // index of last character
    const char _FAR *direct = p->array;

    if( st==Leading || st==Both )
        {
        for( ; start<=end; start++)
            {
            if( direct[start] != c )
                goto nonNull;
            }
        return TSubString( this, NPOS, 0 ); // Null substring
    }

nonNull:

    if(st==Trailing || st==Both)
        {
        for( ; end >= start; end--)
            if( direct[end] != c )
                break;
        }
    return TSubString(this, start, end-start+1);
}
