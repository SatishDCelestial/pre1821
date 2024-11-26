/*------------------------------------------------------------------------*/
/*                                                                        */
/*  HASH.CPP                                                              */
/*                                                                        */
/*  unsigned string::hash() const;                                        */
/*  unsigned string::hash_case() const;                                   */
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


union TCharMask
{
    unsigned in[sizeof(unsigned)];
    char ch[sizeof(unsigned)*sizeof(unsigned)];
};

#ifdef __FLAT__
const union TCharMask Mask = { { 0, 0xFF, 0xFFFF, 0xFFFFFF } };
#else
const union TCharMask Mask = { { 0, 0xFF } };
#endif

unsigned string::hash() const
{
    if( get_case_sensitive_flag() )
        return hash_case();
    else
        return ::to_upper(*this).hash_case();
}

unsigned string::hash_case() const
{
    unsigned i, h;
    const unsigned _FAR *c;

    h = (unsigned)length();                 // Mix in the string length.
    i = h/sizeof(*c);       // Could do "<<" here, but less portable.
    c = (const unsigned _FAR *)c_str();

    while( i-- )
        h ^= *c++;        // XOR in the characters.

    // If there are any remaining characters,
    // then XOR in the rest, using a mask:
    if( (i = length()%sizeof(unsigned)) != 0 )
        {
        h ^= *c & Mask.in[i];
        }
    return (unsigned)h;
}
