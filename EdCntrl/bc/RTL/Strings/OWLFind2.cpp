/*------------------------------------------------------------------------*/
/*                                                                        */
/*  FIND2.CPP                                                             */
/*                                                                        */
/*  size_t TRegexp::find( const string& string,                           */
/*                        size_t *len,                                    */
/*                        size_t i ) const;                               */
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

#define __VA_UTF8__
#if defined(__VA_UTF8__)
int GetUtf8SequenceLen(LPCSTR cpt);
#include "../LOG.H"
#endif

using namespace OWL;


typedef unsigned char PatternType;

const char _FAR * matchs( const char _FAR *str,
                          const PatternType _FAR *pat,
                          char _FAR * _FAR *startpat);

size_t TRegexp::find( const string _FAR &string,
                      size_t _FAR *len,
                      size_t i ) const
{
//    PRECONDITION( stat==OK );
    char _FAR * startp;
    const char _FAR * s = string.c_str();
	const char _FAR * endp = ::matchs( s+i, the_pattern, &startp );
    if( endp )
        {
#if defined(__VA_UTF8__)
		size_t len8 = (size_t)GetUtf8SequenceLen(endp);
		if (!len8)
		{
			_ASSERTE(!"TRegexp::find bad utf8 seq len, breaking potential infinite loop");
			vLog("ERROR: TRegexp::find bad utf8 seq len");
			len8 = 1;
		}
		*len = (size_t)((char _FAR *)endp - startp + len8);
#else
		*len = (size_t)((char _FAR *)endp - startp + 1);
#endif
        return (size_t)(startp - (char _FAR *)s);
        }
    else
        {
        *len = 0;
        return (size_t)-1;
        }
}
