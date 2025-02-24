/*------------------------------------------------------------------------*/
/*                                                                        */
/*  REGEXP.H                                                              */
/*                                                                        */
/*------------------------------------------------------------------------*/

/*
 *      C/C++ Run Time Library - Version 7.0
 *
 *      Copyright (c) 1987, 1996 by Borland International
 *      All Rights Reserved.
 *
 */

#ifndef __cplusplus
#error Must use C++ for REGEXP.H
#endif

#ifndef __REGEXP_H
#define __REGEXP_H

#include "../OWLDefs.h"
#if !defined( __EXCEPT_H )
#include "../bc/except.h"
#endif


#if !defined(RC_INVOKED)
/*
#if defined(__STDC__)
#pragma warn -nak
#endif
*/
#endif  /* !RC_INVOKED */


namespace OWL
{

class _EXPCLASS string;

/*------------------------------------------------------------------------*/
/*                                                                        */
/*  class TRegexp                                                         */
/*                                                                        */
/*  Implements regular expression searching                               */
/*                                                                        */
/*------------------------------------------------------------------------*/

class _EXPCLASS TRegexp
{

public:

    enum StatVal
    {
        OK=0,
        ILLEGAL,
	NOMEMORY,
        TOOLONG
    };

    _RTLENTRY TRegexp( const char _FAR *cp );
    _RTLENTRY TRegexp( const TRegexp _FAR &r );
    _RTLENTRY ~TRegexp();

    TRegexp _FAR & _RTLENTRY operator = ( const TRegexp _FAR &r ) throw( xalloc );
    TRegexp _FAR & _RTLENTRY operator = ( const char _FAR *cp ) throw( xalloc );
    size_t _RTLENTRY find( const string _FAR &s,
                           size_t _FAR *len,
                           size_t start = 0 ) const;
    StatVal _RTLENTRY status() const throw();

private:

    void _RTLENTRY copy_pattern( const TRegexp _FAR &r ) throw( xalloc );
    void _RTLENTRY gen_pattern( const char _FAR *cp ) throw( xalloc );

//    unsigned char _FAR *the_pattern;
    unsigned char the_pattern[128];	// per maxpat as set in regexp.cpp
    mutable StatVal stat;
    static const unsigned maxpat;
};

#if defined( BI_OLDNAMES )
#define BI_Regexp TRegexp
#endif


#if !defined(RC_INVOKED)
/*
#if defined(__STDC__)
#pragma warn .nak
#endif
*/
#endif  /* !RC_INVOKED */

} // namespace OWL

#endif /* __REGEXP_H */
