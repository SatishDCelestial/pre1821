/*  except.h

    Definitions for exception handling

*/

/*
 *      C/C++ Run Time Library - Version 7.0
 *
 *      Copyright (c) 1992, 1996 by Borland International
 *      All Rights Reserved.
 *
 */

#ifndef __cplusplus
#error Must use C++ for except.h
#endif

#ifndef __EXCEPT_H
#define __EXCEPT_H

#include "../OWLDefs.h"

#if !defined(RC_INVOKED)
/*
#pragma pack(push, 1)

#if defined(__BCOPT__)
#if !defined(_RTL_ALLOW_po) && !defined(__FLAT__)
#pragma option -po-     // disable Object data calling convention
#endif
#endif

#if !defined(__TINY__)
#pragma option -RT
#endif

#pragma option -Vo-     // set standard C++ options

#if defined(__STDC__)
#pragma warn -nak
#endif
*/
#endif  /* !RC_INVOKED */

namespace OWL
{

typedef void (_RTLENTRY *terminate_function)();
typedef void (_RTLENTRY *unexpected_function)();

terminate_function  _RTLENTRY set_terminate(terminate_function);
unexpected_function _RTLENTRY set_unexpected(unexpected_function);

void  _RTLENTRY terminate();
void  _RTLENTRY unexpected();

extern  char _FAR * _RTLENTRY __ThrowFileName();
extern  unsigned    _RTLENTRY __ThrowLineNumber();
extern  char _FAR * _RTLENTRY __ThrowExceptionName();

#define  __throwFileName      __ThrowFileName()
#define  __throwLineNumber    __ThrowLineNumber()
#define  __throwExceptionName __ThrowExceptionName()

class _EXPCLASS string;

class _EXPCLASS xmsg
{
public:
    _RTLENTRY xmsg(const string _FAR &msg);
    _RTLENTRY xmsg(const xmsg _FAR &msg);
    _RTLENTRY ~xmsg();

    const string _FAR & _RTLENTRY why() const;
    void                _RTLENTRY raise() throw(xmsg);
    xmsg&               _RTLENTRY operator=(const xmsg _FAR &src);

private:
    string _FAR *str;
};

inline const string _FAR & _RTLENTRY xmsg::why() const
{
    return *str;
};

class _EXPCLASS xalloc : public xmsg
{
public:
    _RTLENTRY xalloc(const string _FAR &msg, size_t size);

    size_t _RTLENTRY requested() const;
    void   _RTLENTRY raise() throw(xalloc);

private:
    size_t siz;
};


inline size_t _RTLENTRY xalloc::requested() const
{
    return siz;
}


#if !defined(RC_INVOKED)
/*
#if defined(__STDC__)
#pragma warn .nak
#endif

#pragma option -Vo.     // restore user C++ options

#if !defined(__TINY__)
#pragma option -RT.
#endif

#if defined(__BCOPT__)
#if !defined(_RTL_ALLOW_po) && !defined(__FLAT__)
#pragma option -po.     // restore Object data calling convention
#endif
#endif

// restore default packing 
#pragma pack(pop)
*/
#endif  /* !RC_INVOKED */

} // namespace OWL

#endif  // __EXCEPT_H
