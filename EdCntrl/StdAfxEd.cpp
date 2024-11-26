// stdafxed.cpp : source file that includes just the standard includes
//	EdCntrl.pch will be the pre-compiled header
//	stdafxed.obj will contain the pre-compiled type information

#include "stdafxed.h"
#include <atlbase.h>
#if _MSC_VER <= 1200
#include <atlimpl.cpp>
#endif

#if _MSC_VER < 1600 // pre-VS2010
// remove MSVCP60.dll dependency
#include <../../crt/SRC/xlock.cpp> // add .../vc98/crt to your project additional include dirs
#undef _CRTIMP2
#define _CRTIMP2
#include <../../crt/SRC/string.cpp>
#endif
