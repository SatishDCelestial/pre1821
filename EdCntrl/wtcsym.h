#ifndef wtcsym_h__
#define wtcsym_h__

#include "DevShellAttributes.h"

#pragma warning(push)
#pragma warning(disable : 4127 4310)

#ifdef _DEBUGNOT
#define CSYM_WRAPPER(crtFn)                                                                                            \
	inline int __cdecl wt_##crtFn(int c)                                                                               \
	{                                                                                                                  \
		if (c & 0xffffff00)                                                                                            \
		{                                                                                                              \
			DWORD callerAddr;                                                                                          \
			__asm {							\
			__asm push eax					\
			__asm mov eax, dword ptr[ebp + 4]\
			__asm mov callerAddr, eax		\
			__asm pop eax}           \
			{                                                                                                          \
				WTString msg;                                                                                          \
				msg.WTFormat("PARAM ERROR: " #crtFn " param: %08x  param as char: %c caller: %08x\n", c,               \
				             c & 0x000000ff, callerAddr);                                                              \
				OutputDebugString(msg);                                                                                \
			}                                                                                                          \
			return 0;                                                                                                  \
		}                                                                                                              \
		return crtFn(c);                                                                                               \
	}
#else
#define CSYM_WRAPPER(crtFn)                                                                                            \
	inline int __cdecl xxwt_##crtFn(int c)                                                                             \
	{                                                                                                                  \
		if (c & 0xffffff00)                                                                                            \
			return 0;                                                                                                  \
		return crtFn(c);                                                                                               \
	}
#endif // _DEBUG

CSYM_WRAPPER(isalpha)
CSYM_WRAPPER(isalnum)
CSYM_WRAPPER(isdigit)
CSYM_WRAPPER(isxdigit)
// CSYM_WRAPPER(isspace)
CSYM_WRAPPER(isupper)
CSYM_WRAPPER(islower)
CSYM_WRAPPER(__iscsymf)
CSYM_WRAPPER(__iscsym)
CSYM_WRAPPER(ispunct)
CSYM_WRAPPER(iscntrl)

inline int __cdecl xxwt_isspace(unsigned int c)
{
	if (c & 0xffffff00)
		return 0;

	// 0xbb and 0xb7 characters are used to mark whitespace - they are regarded as whitespace
	// case 661, case 6466
	if ((c == 0xbb) || (c == 0xb7))
	{
		if (gShellAttr && gShellAttr->IsMsdev())
			return true;
	}

	return isspace((int)c);
}

inline int __cdecl xxwt_isspace(int c)
{
	if (c & 0xffffff00)
		return 0;

	if ((c == 0xbb) || (c == 0xb7))
	{
		// 0xbb and 0xb7 characters are used to mark whitespace - they are regarded as whitespace
		// case 661, case 6466
		if (gShellAttr && gShellAttr->IsMsdev())
			return true;
	}

	return isspace(c);
}

inline int __cdecl xxwt_isspace(char c)
{
	if ((c == (char)0xbb) || (c == (char)0xb7))
	{
		// 0xbb and 0xb7 characters are used to mark whitespace - they are regarded as whitespace
		// case 661, case 6466
		if (gShellAttr && gShellAttr->IsMsdev())
			return true;
	}

	if ((unsigned)(c + 1) > 256)
		return false;

	return isspace(c);
}


// #define VISIBLE_WHITE_SPACE_CHAR(c)                                                                                    \
// 	(((c == '\xbb') || (c == '\xb7')) && gShellAttr &&                                                                 \
// 	 gShellAttr->IsMsdev()) // 0xb7(VC6 ViewWhiteSpace) 0xbb? // case 661, case 6466
#define xxIS_SPECIAL_CSYM(c) ((c & '\x80') || ((c) == '@') || (IsDollarValid() && ((c) == '$'))) // allow $'s and UNICODE

#define xxISCSYM(c) (/*!VISIBLE_WHITE_SPACE_CHAR(c) &&*/ (xxIS_SPECIAL_CSYM(c) || xxwt___iscsym(c)))
#define xxISALPHA(c) (/*!VISIBLE_WHITE_SPACE_CHAR(c) &&*/ (xxIS_SPECIAL_CSYM(c) || xxwt___iscsymf(c)))
#define xxISALNUM(c) ISCSYM(c)
#define xxISCPPCSTR(c) ISCSYM(c)




extern bool g_DollarValid;
inline bool IsDollarValid()
{
	return g_DollarValid;
}


// precalculated table of all chars classifications VA needs
class CharsTable
{
  public:
	enum class table_e : uint32_t
	{
		is_csym,
		is_alpha,
		wt_isspace,
		wt_isalpha,
		wt_isupper,
		wt_islower,
		wt_isalnum,
		wt_isdigit,
		wt_isxdigit,
		wt_ispunct,
		wt_iscntrl,
		wt_isspace4,

		__countof
	};


	constexpr CharsTable()
	{
		for(int c = 0; c <= 256; c++)
		{
			bool is_special_csym = (c & '\x80') || ((c) == '@') || (IsDollarValid() && ((c) == '$'));
			set_flag(table_e::is_csym, c, is_special_csym || xxwt___iscsym(c));
			set_flag(table_e::is_alpha, c, is_special_csym || xxwt___iscsymf(c));
			set_flag(table_e::wt_isspace, c, !!xxwt_isspace(c));
			set_flag(table_e::wt_isalpha, c, !!xxwt_isalpha(c));
			set_flag(table_e::wt_isupper, c, !!xxwt_isupper(c));
			set_flag(table_e::wt_islower, c, !!xxwt_islower(c));
			set_flag(table_e::wt_isalnum, c, !!xxwt_isalnum(c));
			set_flag(table_e::wt_isdigit, c, !!xxwt_isdigit(c));
			set_flag(table_e::wt_isxdigit, c, !!xxwt_isxdigit(c));
			set_flag(table_e::wt_ispunct, c, !!xxwt_ispunct(c));
			set_flag(table_e::wt_iscntrl, c, !!xxwt_iscntrl(c));
			set_flag(table_e::wt_isspace4, c, c && strchr(" \t\r\n", c));
		}
		initialized = true;

		#ifdef VA_CPPUNIT
		test_tables();
		#endif
	}

	constexpr bool get_flag(table_e t, int _i) const
	{
		assert(initialized);
		uint32_t i = std::min<uint32_t>((uint32_t)_i, 256u);
		return tables[std::to_underlying(t)].test(i);
	}

	constexpr void set_flag(table_e t, int i, bool value, bool force = false)
	{
		assert((i >= 0) && (i <= 256));
		if(!force && !value)
			return;

		tables[std::to_underlying(t)].set((uint32_t)i, value);
	}


private:
#ifdef VA_CPPUNIT
	constexpr void test_tables();
#endif

	std::array<std::bitset<257>, std::to_underlying(table_e::__countof)> tables;
	bool initialized = false;
};
extern CharsTable chars_table;

inline void SetDollarValid(bool valid = true)
{
	g_DollarValid = valid;
	chars_table.set_flag(CharsTable::table_e::is_csym, '$', valid, true);
	chars_table.set_flag(CharsTable::table_e::is_alpha, '$', valid, true);
}


inline bool ISCSYM(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::is_csym, c);
	//	assert(ret == !!xxISCSYM(c));
	return ret;
}
inline bool ISALPHA(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::is_alpha, c);
//	assert(ret == !!xxISALPHA(c));
	return ret;
}
inline bool ISALNUM(int c)
{
	return ISCSYM(c);
}
inline bool ISCPPCSTR(int c)
{
	return ISCSYM(c);
}

inline bool wt_isspace(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isspace, c);
// 	assert(ret == !!xxwt_isspace(c));
	return ret;
}
inline bool wt_isspace4(int c)
{
	// only matches " \t\r\n", not '\f'
	return chars_table.get_flag(CharsTable::table_e::wt_isspace4, c);
}
inline bool wt_isalpha(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isalpha, c);
//	assert(ret == !!xxwt_isalpha(c));
	return ret;
}
inline bool wt_isalnum(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isalnum, c);
//	assert(ret == !!xxwt_isalnum(c));
	return ret;
}
inline bool wt_isdigit(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isdigit, c);
//	assert(ret == !!xxwt_isdigit(c));
	return ret;
}
inline bool wt_isxdigit(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isxdigit, c);
//	assert(ret == !!xxwt_isxdigit(c));
	return ret;
}
inline bool wt_isupper(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_isupper, c);
//	assert(ret == !!xxwt_isupper(c));
	return ret;
}
inline bool wt_islower(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_islower, c);
//	assert(ret == !!xxwt_islower(c));
	return ret;
}
inline bool wt_ispunct(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_ispunct, c);
//	assert(ret == !!xxwt_ispunct(c));
	return ret;
}
inline bool wt_iscntrl(int c)
{
	bool ret = chars_table.get_flag(CharsTable::table_e::wt_iscntrl, c);
//	assert(ret == !!xxwt_iscntrl(c));
	return ret;
}

#ifdef VA_CPPUNIT
#pragma warning(push)
#pragma warning(disable : 4365)
constexpr void CharsTable::test_tables()
{
	auto test = [](auto c) {
		assert(ISCSYM(c) == !!xxISCSYM(c));
		assert(ISALPHA(c) == !!xxISALPHA(c));
		assert(ISALNUM(c) == !!xxISALNUM(c));
		assert(ISCPPCSTR(c) == !!xxISCPPCSTR(c));
		assert(wt_isspace(c) == !!xxwt_isspace(c));
		assert(wt_isalpha(c) == !!xxwt_isalpha(c));
		assert(wt_isalnum(c) == !!xxwt_isalnum(c));
		assert(wt_isdigit(c) == !!xxwt_isdigit(c));
		assert(wt_isxdigit(c) == !!xxwt_isxdigit(c));
		assert(wt_isupper(c) == !!xxwt_isupper(c));
		assert(wt_islower(c) == !!xxwt_islower(c));
		assert(wt_ispunct(c) == !!xxwt_ispunct(c));
		assert(wt_iscntrl(c) == !!xxwt_iscntrl(c));
	};
	for(int c = 0; c <= 257; ++c)
	{
		test(c);
		test((char)c);
		test((unsigned int)c);
	}
}
#pragma warning(pop)
#endif



#pragma warning(pop)

#endif // wtcsym_h__
