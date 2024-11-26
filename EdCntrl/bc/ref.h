/*------------------------------------------------------------------------*/
/*                                                                        */
/*  REF.H                                                                 */
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
#error Must use C++ for REF.H
#endif

#ifndef __REF_H
#define __REF_H

#include "../OWLDefs.h"

/*
 *
 * Base class for reference counting
 *
 */
/*

#if !defined(RC_INVOKED)

#if defined(__BCOPT__)
#if !defined(_RTL_ALLOW_po) && !defined(__FLAT__)
#pragma option -po-     // disable Object data calling convention
#endif
#endif

#pragma option -Vo-

#if defined(__STDC__)
#pragma warn -nak
#endif

#endif  /* !RC_INVOKED */


namespace OWL
{

/*------------------------------------------------------------------------*/
/*                                                                        */
/*  class TReference                                                      */
/*                                                                        */
/*  Base class for reference counting                                     */
/*                                                                        */
/*------------------------------------------------------------------------*/

class _EXPCLASS TReference
{

public:

    _RTLENTRY TReference(long initRef = 0) : Refs(initRef) { }
    void _RTLENTRY AddReference() { ::InterlockedIncrement(&Refs); }
    long _RTLENTRY References() { return Refs; }
    long _RTLENTRY RemoveReference() { return ::InterlockedDecrement(&Refs); }

private:

    volatile long Refs;    // Number of references to this block

};

#if defined( BI_OLDNAMES )
#define BI_Reference TReference
#endif


#if !defined(RC_INVOKED)

#if defined(__STDC__)
#pragma warn .nak
#endif

#if defined(__BCOPT__)
#if !defined(_RTL_ALLOW_po) && !defined(__FLAT__)
#pragma option -po.     // restore Object data calling convention
#endif
#endif

#pragma option -Vo.

#endif  /* !RC_INVOKED */


} // namespace OWL

#endif  // __REF_H
