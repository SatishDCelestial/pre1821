#pragma once

#ifndef _DEBUG
//#define LICENSE_TRACING
#endif // !_DEBUG

#if defined(LICENSE_TRACING)
#define LicenseTraceBox(parent, msg) MessageBox(parent, msg, "License Debug", MB_OK)
#define LicenseTraceBox1(parent, fmt, arg)                                                                             \
	{                                                                                                                  \
		CString msg;                                                                                                   \
		CString__FormatA(msg, fmt, arg);                                                                               \
		MessageBox(parent, msg, "License Debug", MB_OK);                                                               \
	}
#else
#define LicenseTraceBox(parent, msg)
#define LicenseTraceBox1(parent, msg, arg)
#endif // LICENSE_TRACING
