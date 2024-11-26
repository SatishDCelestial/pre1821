#pragma once

#include <string>
#include "WTString.h"

WTString FormatStr(UINT nID, ...);

std::string __std_string_format(const char* format, ...);
#define std_string_format(format, ...)                                                                                 \
	if (false)                                                                                                         \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__);                                                               \
	else                                                                                                               \
		__std_string_format(format, __VA_ARGS__)

inline std::string operator+(std::string left, const std::string& right)
{
	left.append(right);
	return left;
}

BOOL HasUnformattableMultilineCStyleComment(LPCWSTR srcstring);
WTString StripCommentsAndStrings(int langType, const WTString& str, bool stripStrings = true);
WTString BuildMenuTextHexAccelerator(uint index, CString text);
CStringW BuildMenuTextHexAcceleratorW(uint index, CStringW text, bool escapeAmpersands = true);
WTString GetMinimumRequiredName(const WTString& qualifiedName, const WTString& basescope);
WTString GetMinimumRequiredNameFromList(const WTString& qualifiedName, const WTString& scopeList);
CStringW MbcsToWide(LPCSTR text, int len, int mbcsCodepage = CP_UTF8);
WTString WideToMbcs(LPCWSTR bstrText, int len, int mbcsCodepage = CP_UTF8);
int RemoveLinesThatContainFragment(CStringW& str, CStringW lineFragmentMatch);
BOOL ContainsIW(CStringW str, CStringW substr); // [case: 60959] use this instead of StrStrIW
inline BOOL ContainsIW(CStringW str, LPCWSTR substr)
{
	return ContainsIW(str, CStringW(substr));
}
BOOL StrIsLowerCase(const WTString& str);
BOOL StrIsUpperCase(const WTString& str);
BOOL StrHasUpperCase(const CStringA str);
BOOL StrHasUpperCase(const CStringW str);
inline BOOL StrIsMixedCase(const WTString str)
{
	if (str.IsEmpty())
		return FALSE;
	return !StrIsLowerCase(str) && !StrIsUpperCase(str);
}
WTString EatBeginningExpression(WTString name);
WTString GetNlDelimitedRecord(LPCTSTR pStr);
void RemoveLeadingWhitespace(const CStringW& lineTxt, CStringW& leadingWhitespace, CStringW& remainderTxt);
// encode arg that is to be passed via query string (where arg needs to be encoded in url?param1=arg)
CString EncodeUrlQueryStringArg(const CString& arg);
int NaturalCompare(const CStringA& lhs, const CStringA& rhs, bool ignore_spaces = false, bool non_alnum_by_char = true);
int NaturalCompare(const CStringW& lhs, const CStringW& rhs, bool ignore_spaces = false, bool non_alnum_by_char = true);

int FindNoCase(const CStringW& inStr, const CStringW& findThis);
void ReplaceNoCase(CStringW& ioStr, const CStringW& replaceThis, const CStringW& withThis);
void ReplaceWholeWord(CStringW& ioStr, const CStringW& replaceThis, const CStringW& withThis);

WTString ReadToUnpairedColon(const WTString& str);

// pos is character offset, return byte offset
int AdjustPosForMbChars(const WTString& buf, int pos);
// pos is byte offset, return character offset
int ByteOffsetToCharOffset(const WTString& buf, int pos, bool returnUtf16ElementOffset = false);
// pos is byte offset, return UTF16 element offset
inline int ByteOffsetToUtf16ElementOffset(const WTString& buf, int pos)
{
	return ByteOffsetToCharOffset(buf, pos, true);
}
// utf8 utils
bool CanReadAsUtf8(LPCSTR buf);
int GetUtf8SequenceLen(LPCSTR cpt);
// strlen for utf8 string that returns character count instead of bytes
int strlen_utf8(LPCSTR buf);

inline bool iseol(const char c)
{
	return c == '\n' || c == '\r';
}
inline bool iseol(const wchar_t c)
{
	return c == L'\n' || c == L'\r';
}
inline bool IsWSorContinuation(char end)
{
	return end == ' ' || end == '\t' || end == '\r' || end == '\n' || end == '\\';
}
inline bool IsWSorContinuation(wchar_t end)
{
	return end == L' ' || end == L'\t' || end == L'\r' || end == L'\n' || end == L'\\';
}

bool HasDatePassed(CString date);

bool EndsWith(const CStringW& inStr, const CStringW& findThisEnd);
bool EndsWithNoCase(const CStringW& inStr, const CStringW& findThisEnd);
