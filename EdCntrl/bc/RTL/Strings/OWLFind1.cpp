/*------------------------------------------------------------------------*/
/*                                                                        */
/*  FIND1.CPP                                                             */
/*                                                                        */
/*  TSubString string::substring( const char *cp, size_t start );         */
/*  const TSubString string::substring( const char *cp,                   */
/*                                      size_t start ) const;             */
/*  size_t string::find( const string& s, size_t startindex ) const;      */
/*  size_t string::find_index( const char *pattern,                       */
/*                             size_t starttinex,                         */
/*                             size_t far& patl ) const;                  */
/*  size_t string::find_case_index( const char *pattern,                  */
/*                                  size_t starttinex,                    */
/*                                  size_t far& patl ) const;             */
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



TSubString string::substring( const char _FAR *cp, size_t start ) throw()
{
    size_t patl;
    size_t pos = find_index( cp, start, patl );
    return TSubString( this, pos, pos == NPOS ? 0 : patl );
}

const TSubString string::substring( const char _FAR *cp, size_t start )
    const throw()
{
    size_t patl;
    size_t pos = find_index( cp, min(start,strlen(cp)), patl );
    return TSubString( this, pos, pos == NPOS ? 0 : patl );
}

size_t string::find( const string _FAR &s, size_t startindex ) const throw()
{
    size_t patl;
    return find_index( s.c_str(), startindex, patl ); // Throws away "patl"
}

size_t string::find_index( const char _FAR * pattern,
                           size_t startindex,
                           size_t _FAR & patl ) const
{
    if( get_case_sensitive_flag() )
        return find_case_index( pattern, startindex, patl );
    else
        return ::to_upper(*this).find_case_index(::to_upper(string(pattern)).c_str(), startindex, patl);
}

size_t string::find_case_index( const char _FAR *cp,
                                size_t startindex,
                                size_t _FAR &patl ) const
{

    const long q = 33554393L;
    const long q32 = q<<5;

    size_t testlength = length() - startindex;
    size_t patternlength = patl = strlen(cp);
    if( testlength < patternlength )
        return NPOS;
    if( patternlength == 0 )
        return 0;

    long patternHash = 0;
    long testHash = 0;

    const char _FAR *testP = c_str()+startindex;
    const char _FAR *patP = cp;
    long x = 1;
    size_t i = patternlength-1;

    while( i-- )
        x = (x<<5)%q;

    for( i=0; i<patternlength; i++ )
        {
        patternHash = ( (patternHash<<5) + *patP++  ) % q;
        testHash    = ( (testHash   <<5) + *testP++ ) % q;
        }

    testP = c_str()+startindex;
    const char _FAR *end = testP + testlength - patternlength;

    for (;;)
        {

        if(testHash == patternHash)
            if( !get_paranoid_check_flag() ||
                !strncmp( testP, cp, patternlength) )
              return (size_t)(testP-c_str());

        if( testP >= end )
            break;

        // Advance & calculate the new hash value:
        testHash = ( testHash + q32 - *testP * x                 ) % q;
        testHash = ( (testHash<<5)  + *(patternlength + testP++) ) % q;
        }
    return NPOS;          // Not found.
}
