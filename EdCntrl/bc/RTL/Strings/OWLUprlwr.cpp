/*------------------------------------------------------------------------*/
/*                                                                        */
/*  UPRLWR.CPP                                                            */
/*                                                                        */
/*  void string::to_lower();                                              */
/*  void string::to_upper();                                              */
/*                                                                        */
/*  string to_lower( const string& s );                                   */
/*  string to_upper( const string& s );                                   */
/*                                                                        */
/*  void TSubString::to_lower();                                          */
/*  void TSubString::to_upper();                                          */
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
#include <stdlib.h>
#include <ctype.h>

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#undef THIS_FILE
//static char THIS_FILE[] = __FILE__;
//#endif

using namespace OWL;


void string::to_lower()
{
    cow();
    _strlwr_s((char _FAR *)c_str(), length() + 1);		// sean changed from _lstrlwr
}

void string::to_upper()
{
    cow();
    _strupr_s((char _FAR *)c_str(), length() + 1);		// sean changed from _lstrupr
}

string _Cdecl _FARFUNC to_lower( const string _FAR &s ) throw()
{
    string temp = s.copy();
    temp.to_lower();
    return temp;
}

string _Cdecl _FARFUNC to_upper( const string _FAR &s ) throw()
{
    string temp = s.copy();
    temp.to_upper();
    return temp;
}

void TSubString::to_lower() throw()
{
    if( begin != NPOS )         // Ignore null substrings
        {
        s->cow();
        char _FAR *p = s->p->array + begin;
        size_t n = extent;
        while( n-- )
            {
            *p = char(_tolower(*p));	// sean changed from _ltolower
            p++;
            }
        }
}

void TSubString::to_upper() throw()
{
    if( begin != NPOS )         // Ignore null substrings
        {
        s->cow();
        char _FAR *p = s->p->array + begin;
        size_t n = extent;
        while( n-- )
            {
            *p = char(_toupper(*p));	// sean changed from _ltoupper
            p++;
            }
        }
}
