/*
 *      C/C++ Run Time Library - Version 8.0
 *
 *      Copyright (c) 1993, 1997 by Borland International
 *      All Rights Reserved.
 *
 */
/* $Revision:   8.2  $        */

#include "stdafx.h"
#include "except.h"
#include "cstring.h"


//#ifdef _DEBUG
//#define new DEBUG_NEW
//#undef THIS_FILE
//static char THIS_FILE[] = __FILE__;
//#endif

using namespace OWL;

_RTLENTRY xalloc::xalloc( const string _FAR & msg, size_t size )
                 : xmsg(msg), siz(size)
{
}

void _RTLENTRY xalloc::raise() throw( xalloc )
{
    throw *this;
}

