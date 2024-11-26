#pragma once

#if defined(_DEBUG) && !defined(SEAN)

#include <Windows.h>
#include <sstream>
#include <ostream>
#include <string>
#include <iomanip>

namespace debug
{
template <class CharT, class TraitsT = std::char_traits<CharT>>
class basic_debugbuf : public std::basic_stringbuf<CharT, TraitsT>
{
  public:
	virtual ~basic_debugbuf()
	{
		sync();
	}

  protected:
	int sync() override
	{
		output_debug_string(this->str().c_str());
		this->str(std::basic_string<CharT>()); // Clear the string buffer

		return 0;
	}

	void output_debug_string(const CharT* text)
	{
	}
};

template <> inline void basic_debugbuf<char>::output_debug_string(const char* text)
{
	if (text && *text)
		::OutputDebugStringA(text);
}

template <> inline void basic_debugbuf<wchar_t>::output_debug_string(const wchar_t* text)
{
	if (text && *text)
		::OutputDebugStringW(text);
}

template <class CharT, class TraitsT = std::char_traits<CharT>>
class basic_dostream : public std::basic_ostream<CharT, TraitsT>
{
  public:
	basic_dostream() : std::basic_ostream<CharT, TraitsT>(new basic_debugbuf<CharT, TraitsT>())
	{
	}
	~basic_dostream()
	{
		delete this->rdbuf();
	}
};

std::wstring to_string(const POINT& pt);
std::wstring to_string(const SIZE& size);
std::wstring to_string(const RECT& rc);
std::wstring to_string(const WINDOWPOS& wp);
std::wstring to_string(const LOGFONTW& wp);

using wstream = basic_dostream<wchar_t>;
} // namespace debug

#define VADEBUGPRINTLINE(expr) {VADEBUGPRINT(expr << std::endl)}

#define VADEBUGPRINT(expr)                                                                                             \
	{                                                                                                                  \
		debug::wstream dbgStreamVADEBUGPRINT;                                                                          \
		dbgStreamVADEBUGPRINT << expr;                                                                                 \
	}

#else

#define VADEBUGPRINT(expr)                                                                                             \
	{                                                                                                                  \
	}
#endif
