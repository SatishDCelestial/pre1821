#pragma once

#include "CatLog.h"

#ifndef OWL
#include "wt_stdlib.h"
#endif

#define __MyLogUnfiltered(format, ...)																				   \
	if (false)																										   \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__);									   \
	else																											   \
		MyLog(format, __VA_ARGS__)

#define __MyLog(format, ...)                                                                                           \
	if (false)                                                                                                         \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__);                                    \
	else                                                                                                               \
		if (gLoggingCategories.test(0))																			   \
			MyLog(format, __VA_ARGS__)

#define __MyCatLog(cat, format, ...)                                                                                   \
	do                                                                                                                 \
	{                                                                                                                  \
		constexpr int _cat_id = GetIdByString(cat);                                                                    \
		static_assert(_cat_id != 0, "log category was not found: correct the name or add a new category to the log_categories array in CatLog.h"); \
		if (gLoggingCategories.test((size_t)_cat_id))                                                                  \
			__MyLogUnfiltered(cat ": " format, __VA_ARGS__);                                                           \
	} while (false)

#define ERRORSTRING                                                                                                    \
	(((int)(errno ? errno : _doserrno) < _sys_nerr) ? _sys_errlist[errno ? errno : _doserrno]                          \
	                                                : "error idx out of range")
#define CLEARERRNO                                                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		errno = 0;                                                                                                     \
		_doserrno = 0;                                                                                                 \
	} while (false)
#define Log(f)                                                                                                         \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled)                                                                                          \
			__MyLog("Uncategorised: %s", f);                                                                           \
	} while (false)
#define LogUnfiltered(f)                               \
	do                                                 \
	{                                                  \
		if (g_loggingEnabled)                          \
			__MyLogUnfiltered("Uncategorised: %s", f); \
	} while (false)
#define CatLog(cat, f)                                                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled)                                                                                          \
			__MyCatLog(cat, "%s", f);																					   \
	} while (false)
#define Log1(f)                                                                                                        \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x01)                                                                                   \
			__MyLog("Uncategorised: %s", f);                                                                           \
	} while (false)
#define Log2(f)                                                                                                        \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x02)                                                                                   \
			__MyLog("Uncategorised: %s", f);                                                                           \
	} while (false)
#define Log3(f)                                                                                                        \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x04)                                                                                   \
			__MyLog("Uncategorised: %s", f);                                                                           \
	} while (false)
#define vLog(...)                                                                                                      \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled)                                                                                          \
			__MyLog("Uncategorised: " __VA_ARGS__);                                                                    \
	} while (false)
#define vLogUnfiltered(...)\
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled)                                                                                          \
			__MyLogUnfiltered("unfiltered: " __VA_ARGS__);                                                             \
	} while (false)
#define vCatLog(cat, ...)                 \
	do                                    \
	{                                     \
		if (g_loggingEnabled)             \
			__MyCatLog(cat, __VA_ARGS__); \
	} while (false)
#define vLog1(...)                                                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x01)                                                                                   \
			__MyLog("Uncategorised: " __VA_ARGS__);                                                                    \
	} while (false)
#define vLog2(...)                                                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x02)                                                                                   \
			__MyLog("Uncategorised: " __VA_ARGS__);                                                                    \
	} while (false)
#define vLog3(...)                                                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		if (g_loggingEnabled & 0x04)                                                                                   \
			__MyLog("Uncategorised: " __VA_ARGS__);                                                                    \
	} while (false)

// change this if you want complete logging in a release build
// set the log level in log.cpp
//#define LOGALL
#ifdef _DEBUG
#define LOGALL
// by default, if this is defined then log level will be set to 0xffffffff in eddll.cpp and
// we won't check the registry value
#endif

#ifdef LOGALL
#define LOG_CONCAT_IMPL(x, y) x##y
#define LOG_MACRO_CONCAT(x, y) LOG_CONCAT_IMPL(x, y)
#define LOG(f) LogFunc LOG_MACRO_CONCAT(Logfunc, __COUNTER__)(f)
#define LOG1(f) LogFunc LOG_MACRO_CONCAT(Logfunc, __COUNTER__)(f, 0x01)
#define LOG2(f) LogFunc LOG_MACRO_CONCAT(Logfunc, __COUNTER__)(f, 0x02)
#define LOG3(f) LogFunc LOG_MACRO_CONCAT(Logfunc, __COUNTER__)(f, 0x04)
#else
#define LOG(f)                                                                                                         \
	{                                                                                                                  \
	}
#define LOG1(f)                                                                                                        \
	{                                                                                                                  \
	}
#define LOG2(f)                                                                                                        \
	{                                                                                                                  \
	}
#define LOG3(f)                                                                                                        \
	{                                                                                                                  \
	}
#endif

#define LOG_SPACING "   "

extern DWORD g_loggingEnabled;
void MyLog(const char* __fmt, ...);
int ErrorBox(const char*, unsigned int options = MB_OK, HWND hWnd = nullptr);
#ifndef OWL
int ErrorBox(const WTString& msg, unsigned int options = MB_OK);
void InitLogFile(HWND parent = NULL);
void UnInitLogFile();

void VALogError(LPCSTR errMsg, int line, BOOL isError);
#define VALOGERROR(file) VALogError(file, __LINE__, TRUE)
#define VALOGMETHOD(file) VALogError(file, __LINE__, FALSE)
extern void VALogExceptionCallStack();
#define VALOGEXCEPTION(file)                                                                                           \
	{                                                                                                                  \
		VALogError(CString("Exception: ") + CString(file), __LINE__, TRUE);                                            \
		VALogExceptionCallStack();                                                                                     \
	}

void LogAssertFailure(LPCSTR failedCondition, LPCSTR msg, int line);
void LogAssertFailure1(LPCSTR failedCondition, LPCSTR msg, LPCSTR info, int line);

#ifdef _DEBUG
#define LOGMETHOD() LogFunc Logfunc(CString(__FILE__) + itos(__LINE__))
#define SILENT_ASSERT(condition, msg)                                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(condition))                                                                                              \
			LogAssertFailure(#condition, msg, __LINE__);                                                               \
	} while (0)
#define SILENT_ASSERT1(condition, msg, info)                                                                           \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(condition))                                                                                              \
			LogAssertFailure1(#condition, msg, info, __LINE__);                                                        \
	} while (0)
#else
#define LOGMETHOD()
#define SILENT_ASSERT(condition, msg)
#define SILENT_ASSERT1(condition, msg, info)
#endif // _DEBUG

class LogFunc
{
	CString name;
	DWORD tick;
	DWORD m_thrdID;
	DWORD m_logCnt;

  public:
	LogFunc(CString& s, DWORD level = 1)
	{
		if (!g_loggingEnabled & level)
			return;
		name = s;
		Init();
	}
	LogFunc(LPCTSTR s, DWORD level = 1)
	{
		if (!g_loggingEnabled & level)
			return;
		name = s;
		Init();
	}
	void Init();
	~LogFunc();
};
#endif
