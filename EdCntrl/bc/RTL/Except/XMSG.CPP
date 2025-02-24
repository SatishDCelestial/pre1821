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


_RTLENTRY xmsg::xmsg(const string _FAR & msg)
{
    str = new string(msg);
};

_RTLENTRY xmsg::xmsg(const xmsg _FAR & msg)
{
    str = new string(msg.why());
}

_RTLENTRY xmsg::~xmsg()
{
    delete str;
}

void _RTLENTRY xmsg::raise() throw( xmsg )
{
    throw *this;
}

xmsg _FAR & _RTLENTRY xmsg::operator=(const xmsg _FAR & src)
{
   if(this!=&src)
   {
      delete str;
      str = new string(src.why());
   }
   return(*this);
}
