#pragma once
#include "../log.h"

// masks used to determine what logging is enabled
// example: to enable logging of the eddll.dll with ODS set Logging = 0x000000005
#define LOG_EDCNTRL 0x00000001       // eddll.dll
#define LOG_ADDIN 0x00000002         // addin dll - no noisy msg loops (OnWndMsg)
#define LOG_ODS 0x00000004           // output to ODS
#define LOG_ADDIN_MSGLOOP 0x00000008 // addin dll - noisy msg loops only
//#define				0x00000010
//#define				0x00000020
//#define				0x00000040
//#define				0x00000080

/*
 * To use Log you must enclose args in a double set of ()s to fool the
 *   preprocessor into thinking we only have a single arg for the macro.
 * e.g.  Log(("hwnd %08x", hwnd));
 * It will automatically add a newline.
 * The macro allows us to enable logging in release versions without
 *   incurring the cost of all the additional function calls
 *   when logging is disabled.
 * If even this has a performance penalty, then we can enable the
 *   following build-conditional macro definitions.
 */

//#if defined(_DEBUG)
#ifndef Log
#define Log(s)                                                                                                         \
	{                                                                                                                  \
	}
#endif
//#define Log(s) { if (g_loggingEnabled) theRealLogger s ; }
/*
#else	// Release
#define Log(s) {}
#endif
*/
