#ifndef WTSTRINGDEF
#define WTSTRINGDEF

#include "OWLDefs.h"
#include <vector>
#include "log.h"
#include "wtcsym.h"
#include "npos.h"
#include <string>
#include <ranges>
#include <iterator>

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(8) // the default
#endif

namespace OWL
{
class string;
}

struct WtStringData
{
	long nRefs; // reference count
	int nDataLength;
	int nAllocLength;
	// TCHAR data[nAllocLength]

	TCHAR* data()
	{
		return (TCHAR*)(this + 1);
	}
};

#define DB_SEP_CHR ':'
#define DB_SEP_CHR_W L':'
#define DB_SEP_STR2 ":."

class WTString;
extern const WTString DB_SEP_STR;
WTString DBColonToSepStr(LPCSTR p);
int strlen_utf8(LPCSTR buf);
extern const WTString DB_SCOPE_PREPROC;
extern const WTString kUnnamed;
namespace std
{
inline void swap(WTString& lhs, WTString& rhs);
}


class WTString
{
  public:
	using __npos = ::__npos;

	// Constructors
	WTString();
	WTString(nullptr_t)
	    : WTString()
	{
	}
	WTString(const WTString& stringSrc);
	WTString(WTString&& stringSrc)
	    : m_pchData(stringSrc.m_pchData)
	{
		stringSrc.Init();
	}
	WTString(const CStringW& stringSrc);
	WTString(char ch, int nRepeat = 1);
	WTString(wchar_t wch, int nRepeat = 1);
	WTString(LPCSTR lpsz);
	WTString(LPCWSTR lpsz);
	WTString(LPCTSTR lpch, int nLength);
	WTString(LPCTSTR lpch, uint nLength)
	    : WTString(lpch, (int)nLength)
	{
	}
	explicit WTString(const unsigned char* psz);
	WTString(std::string_view sv) : WTString(sv.data(), (uint)sv.size()) {}
	WTString(std::initializer_list<std::string_view> l) {
		Init();
		*this += l;
	}

	// Attributes & Operations
	// as an array of characters
	[[nodiscard]] int GetLength() const;
	[[nodiscard]] uint32_t GetULength() const {return (uint32_t)GetLength();}
	[[nodiscard]] int GetCharLength() const
	{
		return ::strlen_utf8(c_str());
	} // return count of characters rather than bytes
	  ////////////////////////////
	  // owl stuff
	explicit WTString(const OWL::string& str);
	explicit WTString(const std::string& str)
	    : WTString(str.c_str(), (uint)str.size())
	{
	}

	[[nodiscard]] inline WTString substr(int start, int len) const
	{
		return Mid(start, len);
	}
	[[nodiscard]] inline WTString substr(int start) const
	{
		return Mid(start);
	}
	void prepend(LPCTSTR s)
	{
		if(s && s[0])
			insert_new(0, s);
	}
	void prepend(WTString &&s)
	{
		if(s.IsEmpty())
			return;
		s += *this;
		*this = std::move(s);
	}
	void prepend(const WTString &s)
	{
		insert_new(0, s);
	}
	void prepend(std::string_view sv)
	{
		insert_new(0, sv);
	}
	void prepend(char ch)
	{
		insert_new(0, {&ch, (&ch) + 1});
	}
	LPCTSTR append(LPCTSTR s)
	{
		*this += s;
		return c_str();
	}
	LPCTSTR append(TCHAR c)
	{
		*this += c;
		return c_str();
	}
	void surround(char left, char right)
	{
		prepend(left);
		append(right);
	}
	void read_to_delim(VS_STD::istream& ifs, char c, int resizeInc = 64);
	void read_line(VS_STD::istream& ifs, int resizeInc = 64)
	{
		read_to_delim(ifs, '\n', resizeInc);
	}
	bool ReadFile(const CStringW& file, int maxAmt = -1, bool forceUtf8 = false);
	[[nodiscard]] inline int rfind(LPCTSTR s)
	{
		return ReverseFind(s);
	}
	[[nodiscard]] int rfind(LPCTSTR s, int p);
	[[nodiscard]] int find_last_of(LPCTSTR s);
	[[nodiscard]] int GetTokCount(char tok) const;
	void ReplaceAt(int pos, int len, LPCSTR str);
	void ReplaceAt_new(size_t pos, size_t len, std::string_view sv);
	int ReplaceAll(LPCSTR s1, std::string_view s2, BOOL wholeword = FALSE);
	void ReplaceAll(char from, char to);
	int ReplaceAll(const WTString& s1, std::string_view s2, BOOL wholeword = FALSE)
	{
		return ReplaceAll(s1.c_str(), s2, wholeword);
	}
	int Replace(LPCSTR s1, LPCSTR s2)
	{
		return ReplaceAll(s1, std::string_view(s2));
	}
	void ReplaceAll(char(&s1)[2], char(&s2)[2])
	{
		assert(s1[0] && s2[0]);
		ReplaceAll(s1[0], s2[0]);
	}

	int ReplaceAllRE(LPCWSTR pattern, bool iCase, std::function<bool(int, CStringW&)> fmtFnc);
	int ReplaceAllRE(LPCWSTR pattern, bool iCase, const CStringW& replacement);

	[[nodiscard]] inline int length() const
	{
		return GetLength();
	}
	[[nodiscard]] inline bool EndsWith(LPCTSTR str) const
	{
		int slen = strlen_i(str);
		return (length() >= slen && strcmp(&c_str()[length() - slen], str) == 0);
	}
	[[nodiscard]] bool EndsWith(const WTString& str) const
	{
		int slen = str.GetLength();
		return (length() >= slen && strcmp(&c_str()[length() - slen], str.c_str()) == 0);
	}
	[[nodiscard]] bool EndsWithNC(LPCTSTR str) const
	{
		int slen = strlen_i(str);
		return (length() >= slen && _tcsicmp(&c_str()[length() - slen], str) == 0);
	}
	[[nodiscard]] bool EndsWithNC(const WTString& str) const
	{
		int slen = str.GetLength();
		return (length() >= slen && _tcsicmp(&c_str()[length() - slen], str.c_str()) == 0);
	}
	[[nodiscard]] bool begins_with(char ch) const
	{
		return *c_str() == ch;
	}
	[[nodiscard]] bool begins_with(std::string_view sv) const
	{
		// used std terminology as StartsWith does some dictionary lookups
		return !strncmp(c_str(), sv.data(), sv.size());
	}
	[[nodiscard]] bool begins_with2(std::string_view sv, char ch_after_sv) const
	{
		// matches (sv + char_after_sv)
		return (length() > (int)sv.length()) &&
		       (GetAt((int)sv.length()) == ch_after_sv) &&
		       begins_with(sv);
	}
	[[nodiscard]] bool begins_with2(char ch_before_sv, std::string_view sv) const
	{
		// matches (sv + char_after_sv)
		return (length() > (int)sv.length()) &&
		       (GetAt(0) == ch_before_sv) &&
		       !strncmp(c_str() + 1, sv.data(), sv.size());
	}
	[[nodiscard]] bool ends_with(std::string_view sv) const
	{
		return (length() >= (int)sv.size()) && !strncmp(&c_str()[length() - sv.size()], sv.data(), sv.size());
	}
	[[nodiscard]] bool ends_with(char ch) const
	{
		return IsEmpty() ? false : (c_str()[length() - 1] == ch);
	}
	[[nodiscard]] bool is_null()
	{
		return IsEmpty();
	}

	[[nodiscard]] int find_first_of(LPCTSTR s) const
	{
		return FindOneOf(s);
	}
	[[nodiscard]] int find_first_of(const WTString& s) const
	{
		return FindOneOf(s.c_str());
	}
	[[nodiscard]] bool contains(LPCTSTR s) const
	{
		return (Find(s) != NPOS);
	}
	[[nodiscard]] bool contains(const WTString& s) const
	{
		return (Find(s) != NPOS);
	}
	[[nodiscard]] int find_first_of(LPCTSTR s, int p) const
	{
		WTString str = Mid(p);
		int i = str.FindOneOf(s);
		if (i != NPOS)
			i += p;
		return i;
	}

	void insert(int p, LPCTSTR s);
	void insert_new(size_t p, std::string_view sv);
	[[nodiscard]] bool is_my_ptr(const char* p) const { return !IsEmpty() && (cbegin() <= p) && (p <= cend()); }
	void remove(size_t p, size_t l = 1);

	[[nodiscard]] int find(LPCTSTR s, int p) const
	{
		int ret = Find(s, p);

#ifdef _DEBUG
		WTString str = Mid(p); // terribly inefficient
		int i = str.Find(s);
		if (i != NPOS)
			i += p;
		assert(ret == i);
#endif

		return ret;
	}
	[[nodiscard]] int find(LPCSTR s) const
	{
		return Find(s);
	}
	[[nodiscard]] int find(const WTString& s) const
	{
		return Find(s.c_str());
	}
	[[nodiscard]] int find(TCHAR s) const
	{
		return Find(s);
	}
	[[nodiscard]] int find_first_not_of(LPCSTR s, int i = 0) const
	{
		if(i >= GetLength())
			return NPOS;
		if(i < 0)
			i = 0;

		for (; (*this)[i] && strchr(s, (*this)[i]); i++)
			;
		return ((*this)[i] ? i : NPOS);
	}
	void to_lower()
	{
		MakeLower();
	}
	void to_upper()
	{
		MakeUpper();
	}

	void assign(const WTString& s)
	{
		*this = s;
	}
	void assign(WTString&& s)
	{
		*this = std::move(s);
	}
	void assign(const char* s)
	{
		*this = s;
	}
	[[nodiscard]] const char* c_str() const
	{
		return m_pchData;
	}
	[[nodiscard]] char* data()
	{
		assert(GetData()->nRefs == 1);
		return const_cast<char*>(c_str());
	}
	[[nodiscard]] uint hash() const;
	[[nodiscard]] size_t xxhash() const;
	[[nodiscard]] const char* cbegin() const
	{
		return c_str();
	}
	[[nodiscard]] const char* cend() const
	{
		return c_str() + length();
	}

	std::string_view to_string_view() const
	{
		return std::string_view(cbegin(), cend());
	}
	operator std::string_view() const
	{
		return to_string_view();
	}
	explicit operator std::string() const
	{
		return std::string(cbegin(), cend());
	}

	// operator string(){ return string((const char *) *this); }
	////////////////////////////

	[[nodiscard]] bool IsEmpty() const;
	[[nodiscard]] bool IsStaticEmpty() const;
	void Empty();                                     // free up the data
	void Clear()                                      // clears contents, but leaves buffer
	{
		if(!IsEmpty())
			*this = std::string_view();
	}
	[[nodiscard]] TCHAR GetAt(int nIndex) const;      // 0 based
	[[nodiscard]] TCHAR operator[](int nIndex) const; // same as GetAt
#ifdef _WIN64
	[[nodiscard]] TCHAR operator[](uint nIndex) const
	{
		return (*this)[(int)nIndex];
	}
#endif
	[[nodiscard]] TCHAR operator[](size_t nIndex) const
	{
		return (*this)[(int)nIndex];
	}
	//	TCHAR operator[](long nIndex) const; // same as GetAt
	void SetAt(int nIndex, TCHAR ch);
	void SetAt_nocopy(int nIndex, TCHAR ch);	// be sure to call that on a WTString that exists only as a single instance!!
	// 	operator LPCTSTR() const           // as a C string
	// 	{ return m_pchData; }
	[[nodiscard]] CStringW Wide() const;

	template <typename TString>
	[[nodiscard]] inline void GetWide(TString & str) const
	{
		str = ::MbcsToWide(m_pchData, GetLength());
	}

	// overloaded assignment
	WTString& operator=(const WTString& stringSrc);
	WTString& operator=(WTString&& stringSrc)
	{
		std::swap(m_pchData, stringSrc.m_pchData);
		return *this;
	}
	WTString& operator=(TCHAR ch);
#ifdef _UNICODE
	WTString& operator=(char ch);
#endif
	WTString& operator=(const char* lpsz);
// 	const WTString& operator=(const wchar_t* lpsz)
// 	{
// 		return *this = WTString(lpsz);
// 	}
	WTString& operator=(const std::string_view sv); // will reuse its buffer and make a copy
	//	const WTString& operator=(LPCWSTR lpsz);
	WTString& operator=(const unsigned char* psz);

	WTString& operator=(std::initializer_list<std::string_view> l); // will concatenate all string_views; will work even if string_view points to the part of the destination string, but with performance penalty

	// string concatenation
	WTString& operator+=(TCHAR ch)
	{
		if (ch)
			ConcatInPlace(1, &ch);
		return *this;
	}
#ifdef _UNICODE
	const WTString& operator+=(char ch);
#endif
	WTString& operator+=(const char* lpsz);
	WTString& operator+=(const wchar_t* lpsz)
	{
		return *this += WTString(lpsz);
	}
	WTString& operator+=(std::string_view sv);

	private:
	void do_il_concat(const std::initializer_list<std::string_view> &l, size_t added_size, bool has_inplace_view);
	public:
	WTString& operator+=(std::initializer_list<std::string_view> l);

	friend WTString operator+(WTString&& str1, std::string_view sv2)
	{
		str1 += sv2;
		return std::move(str1);
	}
	friend WTString operator+(const WTString& str1, std::string_view sv2);
	friend WTString operator+(WTString&& str1, const char* str2)
	{
		str1 += str2;
		return std::move(str1);
	}
	friend WTString operator+(const WTString& str1, const char *str2);
	friend WTString operator+(const char* str1, WTString&& str2);
	friend WTString operator+(const char* str1, const WTString& str2);
	friend WTString operator+(WTString str1, char ch2)
	{
		str1 += ch2;
		return std::move(str1);
	}
	friend WTString operator+(char ch, WTString&& string);
	friend WTString operator+(char ch, const WTString& string);

// #ifdef _UNICODE
// 	friend WTString AFXAPI operator+(const WTString& string, char ch);
// 	friend WTString AFXAPI operator+(WTString&& string, char ch)
// 	{
// 		string += ch;
// 		return std::move(string);
// 	}
// 	friend WTString AFXAPI operator+(char ch, const WTString& string);
// #endif

	// string comparison
	[[nodiscard]] int Compare(LPCTSTR lpsz) const; // straight character
	[[nodiscard]] int Compare(const WTString& lpsz) const
	{
		return Compare(lpsz.c_str());
	}
	[[nodiscard]] int CompareNoCase(LPCTSTR lpsz) const; // ignore case
	[[nodiscard]] int CompareNoCase(const WTString& lpsz) const
	{
		return CompareNoCase(lpsz.c_str());
	}
	[[nodiscard]] int Collate(LPCTSTR lpsz) const; // NLS aware

	// simple sub-string extraction
	[[nodiscard]] WTString Mid(int nFirst, int nCount) const;
	[[nodiscard]] WTString Mid(int nFirst) const;
	[[nodiscard]] WTString Mid(size_t nFirst, size_t nCount) const
	{
		return Mid((int)nFirst, (int)nCount);
	}
	[[nodiscard]] WTString Mid(size_t nFirst) const
	{
		return Mid((int)nFirst);
	}
	void MidInPlace(int nFirst);
	void MidInPlace(int nFirst, int nCount);
	void MidInPlace(std::string_view sv); // cut WTString into sv; sv may or may not be within the WTString

	[[nodiscard]] WTString Left(int nCount) const;
	[[nodiscard]] WTString Left(size_t nCount) const
	{
		return Left((int)nCount);
	}
	void LeftInPlace(int nCount);
	[[nodiscard]] WTString Right(int nCount) const;
	[[nodiscard]] WTString Right(size_t nCount) const
	{
		return Right((int)nCount);
	}

	std::string_view left_sv(uint32_t count) const
	{
		count = std::clamp(count, 0u, GetULength());
		return to_string_view().substr(0, count);
	}
	std::string_view mid_sv(uint32_t offset, uint32_t count = 0x7fffffff) const
	{
		offset = std::clamp(offset, 0u, GetULength());
		count = std::clamp(count, 0u, GetULength() - offset);

		return to_string_view().substr(offset, count);
	}
	std::string_view right_sv(uint32_t count) const
	{
		count = std::clamp(count, 0u, GetULength());
		return to_string_view().substr(GetULength() - count, count);
	}

	[[nodiscard]] WTString SpanIncluding(LPCTSTR lpszCharSet) const;
	[[nodiscard]] WTString SpanExcluding(LPCTSTR lpszCharSet) const;

	// upper/lower/reverse conversion
	void MakeUpper();
	void MakeLower();
	void MakeReverse();

	bool TrimRightChar(char cut);
	// trimming whitespace (either side)
	bool TrimRight();
	bool TrimLeftChar(char cut);
	bool TrimLeft();
	bool Trim()
	{
		return TrimLeft() | TrimRight();
	}
	bool TrimWordLeft(const char* word, bool case_insensitive = false);
	bool TrimWordRight(const char* word, bool case_insensitive = false);

	// searching (return starting index, or -1 if not found)
	// look for a single character match
	[[nodiscard]] int Find(TCHAR ch, int startPos = 0) const; // like "C" strchr
	[[nodiscard]] int ReverseFind(TCHAR ch) const;
	[[nodiscard]] int FindOneOf(LPCTSTR lpszCharSet) const;

	// look for a specific sub-string
	// can start from any pos within string, but returns absolute pos
	[[nodiscard]] int Find(LPCTSTR lpszSub, int startPos = 0) const;
	[[nodiscard]] int Find(const WTString& lpszSub, int startPos = 0) const
	{
		return Find(lpszSub.c_str(), startPos);
	}
	[[nodiscard]] int FindNoCase(LPCTSTR lpszSub, int startPos = 0) const; // like "C" strstr
	[[nodiscard]] int FindNoCase(const WTString& lpszSub, int startPos = 0) const
	{
		return FindNoCase(lpszSub.c_str(), startPos);
	}
	[[nodiscard]] int Find2(const char* substr, char char_after_substr) const;
	[[nodiscard]] int Find2(char char_before_substr, const char* substr) const;

	[[nodiscard]] int FindRE(LPCTSTR pattern, int startPos = 0) const;
	[[nodiscard]] int FindRENoCase(LPCTSTR pattern, int startPos = 0) const;

	[[nodiscard]] bool MatchRE(LPCTSTR pattern) const;
	[[nodiscard]] bool MatchRENoCase(LPCTSTR pattern) const;

	[[nodiscard]] int ReverseFind(LPCTSTR lpszSub) const;
	[[nodiscard]] int ReverseFind(const WTString& lpszSub) const
	{
		return ReverseFind(lpszSub.c_str());
	}
	[[nodiscard]] int ReverseFindNoCase(LPCTSTR lpszsub);

	// simple formatting
	void FormatV(LPCTSTR lpszFormat, va_list argList);
	void AppendFormatV(LPCTSTR lpszFormat, va_list argList);
	WTString& AFX_CDECL Format(UINT nFormatID, ...);
#ifdef _DEBUG
	WTString& AFX_CDECL __WTFormat(int dummy, LPCTSTR lpszFormat, ...);
#define WTFormat(format, ...) __WTFormat((_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__), 0), format, __VA_ARGS__)
#else
	WTString& AFX_CDECL __WTFormat(LPCTSTR lpszFormat, ...);
#define WTFormat(format, ...) __WTFormat(format, __VA_ARGS__)
#endif
#ifdef _DEBUG
	WTString& AFX_CDECL __WTAppendFormat(int dummy, LPCTSTR lpszFormat, ...);
#define WTAppendFormat(format, ...) __WTAppendFormat((_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__), 0), format, __VA_ARGS__)
#else
	WTString& AFX_CDECL __WTAppendFormat(LPCTSTR lpszFormat, ...);
#define WTAppendFormat(format, ...) __WTAppendFormat(format, __VA_ARGS__)
#endif

#ifndef _MAC
	// formatting for localization (uses FormatMessage API)
	void AFX_CDECL FormatMessage(LPCTSTR lpszFormat, ...);
	void AFX_CDECL FormatMessage(UINT nFormatID, ...);
#endif

	// input and output
#ifdef _DEBUG
	friend CDumpContext& AFXAPI operator<<(CDumpContext& dc, const WTString& string);
#endif
	// friend CArchive& AFXAPI operator<<(CArchive& ar, const WTString& string);
	// friend CArchive& AFXAPI operator>>(CArchive& ar, WTString& string);

	// Windows support
	BOOL LoadString(UINT nID); // load from string resource
	                           // 255 chars max
#ifndef _UNICODE
	// ANSI <-> OEM support (convert string in place)
	void AnsiToOem();
	void OemToAnsi();
#endif

#ifndef _AFX_NO_BSTR_SUPPORT
	// OLE BSTR support (use for OLE automation)
	BSTR AllocSysString() const;
	BSTR SetSysString(BSTR* pbstr) const;
#endif

	// Access to string implementation buffer as "C" character array
	[[nodiscard]] LPTSTR GetBuffer(int nMinBufLength);
	void ReleaseBuffer(int nNewLength = -1);
	void reserve(uint32_t size)
	{
		std::ignore = GetBuffer((int)size);
	}

	// Implementation
  public:
	~WTString();
	[[nodiscard]] int GetAllocLength() const;
	bool AllocBuffer(int nLen, LPCTSTR pStr, int strLen);
	void PreAllocBuffer(int sz)
	{
		if (!AllocBeforeWrite(sz))
			return;
		*this = "";
	}
	void AssignCopy(int nSrcLen, LPCTSTR lpszSrcData);
	void AssignCopy(std::string_view sv)
	{
		AssignCopy((int)sv.size(), sv.data());
	}
	void ConcatInPlace(int nSrcLen, LPCTSTR lpszSrcData);

  protected:
	LPTSTR m_pchData; // pointer to ref counted string data

	// implementation helpers
	[[nodiscard]] WtStringData* GetData() const;
	void Init();
	void AllocCopy(WTString& dest, int nCopyLen, int nCopyIndex, int nExtraLen) const;
	void ConcatCopy(int nSrc1Len, LPCTSTR lpszSrc1Data, int nSrc2Len, LPCTSTR lpszSrc2Data);
	void ConcatCopyUnchecked(int nSrc1Len, LPCTSTR lpszSrc1Data, int nSrc2Len, LPCTSTR lpszSrc2Data);
  public:
	void CopyBeforeWrite();
  protected:
	bool AllocBeforeWrite(int nLen);
// 	void FreeExtra();
	static void PASCAL Release(WtStringData* pData);
	static int PASCAL SafeStrlen(LPCTSTR lpsz); // {return strlen(lpsz);}

friend inline void std::swap(WTString& lhs, WTString& rhs);
};

// Compare helpers
bool operator==(const WTString& s1, const WTString& s2);
bool operator==(const WTString& s1, LPCTSTR s2);
bool operator==(LPCTSTR s1, const WTString& s2);
bool operator==(const WTString& s1, std::string_view s2);
bool operator==(std::string_view s1, const WTString& s2);
bool operator!=(const WTString& s1, const WTString& s2);
bool operator!=(const WTString& s1, LPCTSTR s2);
bool operator!=(LPCTSTR s1, const WTString& s2);
bool operator!=(const WTString& s1, std::string_view s2);
bool operator!=(std::string_view s1, const WTString& s2);
bool AFXAPI operator<(const WTString& s1, const WTString& s2);
bool AFXAPI operator<(const WTString& s1, LPCTSTR s2);
bool AFXAPI operator<(LPCTSTR s1, const WTString& s2);
bool AFXAPI operator>(const WTString& s1, const WTString& s2);
bool AFXAPI operator>(const WTString& s1, LPCTSTR s2);
bool AFXAPI operator>(LPCTSTR s1, const WTString& s2);
bool AFXAPI operator<=(const WTString& s1, const WTString& s2);
bool AFXAPI operator<=(const WTString& s1, LPCTSTR s2);
bool AFXAPI operator<=(LPCTSTR s1, const WTString& s2);
bool AFXAPI operator>=(const WTString& s1, const WTString& s2);
bool AFXAPI operator>=(const WTString& s1, LPCTSTR s2);
bool AFXAPI operator>=(LPCTSTR s1, const WTString& s2);

inline bool operator==(std::string_view sv, const char* s)
{
	assert(s);
	return sv == std::string_view(s);
}
inline bool operator==(const char* s, std::string_view sv)
{
	assert(s);
	return sv == std::string_view(s);
}
inline bool operator!=(std::string_view sv, const char* s)
{
	return !(sv == s);
}
inline bool operator!=(const char* s, std::string_view sv)
{
	return !(s == sv);
}
bool operator==(std::string_view sv, std::initializer_list<std::string_view> svs);
inline bool operator==(std::initializer_list<std::string_view> svs, std::string_view sv)
{
	return sv == svs;
}
inline bool operator!=(std::string_view sv, std::initializer_list<std::string_view> svs)
{
	return !(sv == svs);
}
inline bool operator!=(std::initializer_list<std::string_view> svs, std::string_view sv)
{
	return !(svs == sv);
}

// Globals
// don't use afxEmptyString or AfxGetEmptyString since they reference static data in MFC
//  we have our own static data in wtstring.cpp
#define WTafxEmptyString WTAfxGetEmptyString()
inline const WTString& AFXAPI WTAfxGetEmptyString()
{
	extern LPCTSTR __afxPchNil;
	return *(const WTString*)&__afxPchNil;
}

////////////////////////////
// WTString
__forceinline WtStringData* WTString::GetData() const
{
	ASSERT(m_pchData != NULL);
	return ((WtStringData*)m_pchData) - 1;
}
__forceinline void WTString::Init()
{
	::InterlockedExchangePointer((PVOID*)&m_pchData, WTafxEmptyString.m_pchData);
}
//__forceinline WTString::WTString(const unsigned char* lpsz)
__forceinline WTString::WTString(char const* lpsz) // this is the WTString(LPCSTR lpsz) ctor
{
	Init();
	*this = (LPCSTR)lpsz;
}
__forceinline WTString& WTString::operator=(const unsigned char* lpsz)
{
	*this = (LPCSTR)lpsz;
	return *this;
}
// #ifdef _UNICODE
// __forceinline const WTString& WTString::operator+=(char ch)
// {
// 	*this += (TCHAR)ch;
// 	return *this;
// }
// __forceinline const WTString& WTString::operator=(char ch)
// {
// 	*this = (TCHAR)ch;
// 	return *this;
// }
// __forceinline WTString AFXAPI operator+(const WTString& string, char ch)
// {
// 	return string + (TCHAR)ch;
// }
// __forceinline WTString AFXAPI operator+(char ch, const WTString& string)
// {
// 	return (TCHAR)ch + string;
// }
// #endif

__forceinline int WTString::GetLength() const
{
	return GetData()->nDataLength;
}
__forceinline int WTString::GetAllocLength() const
{
	return GetData()->nAllocLength;
}
__forceinline bool WTString::IsEmpty() const
{
	return GetData()->nDataLength == 0;
}
inline bool WTString::IsStaticEmpty() const
{
	return m_pchData == WTafxEmptyString.m_pchData;
}
__forceinline int PASCAL WTString::SafeStrlen(LPCTSTR lpsz)
{
	return (lpsz == NULL) ? 0 : lstrlen(lpsz);
}

// WTString support (windows specific)
__forceinline int WTString::Compare(LPCTSTR lpsz) const
{
	return _tcscmp(m_pchData, lpsz);
} // MBCS/Unicode aware
__forceinline int WTString::CompareNoCase(LPCTSTR lpsz) const
{
	return _tcsicmp(m_pchData, lpsz);
} // MBCS/Unicode aware
// WTString::Collate is often slower than Compare but is MBSC/Unicode
//  aware as well as locale-sensitive with respect to sort order.
__forceinline int WTString::Collate(LPCTSTR lpsz) const
{
	return _tcscoll(m_pchData, lpsz);
} // locale sensitive

__forceinline TCHAR WTString::GetAt(int nIndex) const
{
#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		ASSERT(nIndex >= 0);

#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		// sean changed this assert from < to <=
		ASSERT(nIndex <= GetData()->nDataLength);

	return m_pchData[nIndex];
}
__forceinline TCHAR WTString::operator[](int nIndex) const
{
	// same as GetAt
#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		ASSERT(nIndex >= 0);

#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		// sean changed this assert from < to <=
		ASSERT(nIndex <= GetData()->nDataLength);

	return m_pchData[nIndex];
}
//__forceinline TCHAR WTString::operator[](long nIndex) const
//{
//	// same as GetAt
// #if defined(VA_CPPUNIT)
//	extern bool gEnableAllAsserts;
//	if (gEnableAllAsserts)
// #endif
//		ASSERT(nIndex >= 0);
//
// #if defined(VA_CPPUNIT)
//	extern bool gEnableAllAsserts;
//	if (gEnableAllAsserts)
// #endif
//		// sean changed this assert from < to <=
//		ASSERT(nIndex <= GetData()->nDataLength);
//
//	return m_pchData[nIndex];
//}

inline bool operator==(const WTString& s1, const WTString& s2)
{
	if(s1.length() != s2.length())
		return false;
	return s1.Compare(s2.c_str()) == 0;
}
inline bool operator==(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) == 0;
}
inline bool operator==(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) == 0;
}
inline bool operator==(const WTString& s1, std::string_view s2)
{
	if (s1.GetULength() != s2.length())
		return false;
	return !strncmp(s1.c_str(), s2.data(), s2.length());
}
inline bool operator==(std::string_view s1, const WTString& s2)
{
	if (s1.length() != s2.GetULength())
		return false;
	return !strncmp(s1.data(), s2.c_str(), s1.length());
}
inline bool operator!=(const WTString& s1, const WTString& s2)
{
	if (s1.length() != s2.length())
		return true;
	return s1.Compare(s2.c_str()) != 0;
}
inline bool operator!=(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) != 0;
}
inline bool operator!=(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) != 0;
}
inline bool operator!=(const WTString& s1, std::string_view s2)
{
	return !(s1 == s2);
}
inline bool operator!=(std::string_view s1, const WTString& s2)
{
	return !(s1 == s2);
}
__forceinline bool AFXAPI operator<(const WTString& s1, const WTString& s2)
{
	return s1.Compare(s2.c_str()) < 0;
}
__forceinline bool AFXAPI operator<(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) < 0;
}
__forceinline bool AFXAPI operator<(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) > 0;
}
__forceinline bool AFXAPI operator>(const WTString& s1, const WTString& s2)
{
	return s1.Compare(s2.c_str()) > 0;
}
__forceinline bool AFXAPI operator>(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) > 0;
}
__forceinline bool AFXAPI operator>(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) < 0;
}
__forceinline bool AFXAPI operator<=(const WTString& s1, const WTString& s2)
{
	return s1.Compare(s2.c_str()) <= 0;
}
__forceinline bool AFXAPI operator<=(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) <= 0;
}
__forceinline bool AFXAPI operator<=(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) >= 0;
}
__forceinline bool AFXAPI operator>=(const WTString& s1, const WTString& s2)
{
	return s1.Compare(s2.c_str()) >= 0;
}
__forceinline bool AFXAPI operator>=(const WTString& s1, LPCTSTR s2)
{
	return s1.Compare(s2) >= 0;
}
__forceinline bool AFXAPI operator>=(LPCTSTR s1, const WTString& s2)
{
	return s2.Compare(s1) <= 0;
}


namespace std
{
inline void swap(WTString& lhs, WTString& rhs)
{
	std::swap(lhs.m_pchData, rhs.m_pchData);
}
template<>
struct std::hash<WTString>
{
	std::size_t operator()(const WTString &s) const noexcept
	{
		return (std::size_t)XXH64(s.c_str(), (uint32_t)s.length(), 0);
	}
};
template <typename CH, typename TRAITS>
struct std::hash<CStringT<CH, TRAITS>>
{
	std::size_t operator()(const CStringT<CH, TRAITS>& s) const noexcept
	{
		return (std::size_t)XXH64(s.GetString(), (uint32_t)(s.GetLength() * sizeof(CH)), 0);
	}
};
}


#pragma warning(disable : 4702)
inline const char* WTStrchr(LPCSTR str, int c)
{
	if (!c)
		return NULL;
	return strchr(str, c);
}
#define strchr(str, c) WTStrchr(str, c)
#pragma warning(default : 4702)

extern const WTString NULLSTR;
extern const WTString COLONSTR;

class token2 : public WTString
{
	LPCSTR m_ptr; // this needs to be validated, can't let users access it
	WTString ifs;

  public:
	token2()
	    : WTString(), m_ptr(WTString::c_str()), ifs(DB_SEP_STR)
	{
	}
	token2(LPCSTR txt)
	    : WTString(txt), m_ptr(WTString::c_str()), ifs(DB_SEP_STR)
	{
	}
	token2(LPCSTR txt, int n)
	    : WTString(txt, n), m_ptr(WTString::c_str()), ifs(DB_SEP_STR)
	{
	}
	token2(const WTString& str)
	    : WTString(str), m_ptr(WTString::c_str()), ifs(DB_SEP_STR)
	{
	}
	token2(WTString&& str)
	    : WTString(std::move(str)), m_ptr(WTString::c_str()), ifs(DB_SEP_STR)
	{
	}

	int ReplaceAll(LPCSTR s1, std::string_view s2, BOOL wholeword = FALSE)
	{
		//		if(m_ptr != (LPCSTR)*this)
		//			assign(m_ptr);
		int r = WTString::ReplaceAll(s1, s2, wholeword);
		m_ptr = WTString::c_str();
		return r;
	}
	WTString Str()
	{
		ValidatePtr();
		return WTString(m_ptr);
	}
	WTString c_str()
	{
		ValidatePtr();
		return WTString(m_ptr);
	}

	int more()
	{
		assert(length() == strlen_i(m_ptr));
		return length();
	}
	int length(bool do_validate = true)
	{
		if(do_validate)
			ValidatePtr();
		return m_ptr ? (WTString::length() - int(m_ptr - WTString::c_str())) : 0;
	}
	int GetLength()
	{
		return length();
	}
	void ValidatePtr()
	{
		LPCSTR p = WTString::c_str();
		if (m_ptr < p || m_ptr > p + WTString::GetLength())
			m_ptr = p;
	}

	void read(LPCSTR ifsx, WTString& ret)
	{
		ret.Clear();
		ValidatePtr();
		if (ifsx)
			ifs = ifsx;
		// eat beginning spaces
		for (; *m_ptr && strchr(ifs.c_str(), *m_ptr); m_ptr++)
			;

		// find next
		size_t n = strcspn(m_ptr, ifs.c_str());
		if (n > 0)
		{
			ret = std::string_view(m_ptr, n);
			m_ptr += n;
		}
	}
	std::string_view read_sv(char ifch)
	{
		assert(ifch);
		ValidatePtr();
		// eat beginning spaces
		for (; ifch == *m_ptr; m_ptr++)
			;

		// find next
		const char* p = strchr(m_ptr, ifch);
		size_t n = p ? size_t(p - m_ptr) : (uint32_t)length(false);
		std::string_view ret;
		if (n > 0)
		{
			ret = std::string_view(m_ptr, n);
			m_ptr += n;
		}
		return ret;
	}
	void read(char ifch, WTString& ret)
	{
		ret = read_sv(ifch);
	}
	WTString read(LPCSTR ifsx = NULL)
	{
		WTString ret;
		read(ifsx, ret);
		return ret;
	}
	WTString read(char ifch)
	{
		WTString ret;
		read(ifch, ret);
		return ret;
	}
	WTString read(const WTString& ifsx)
	{
		return read(ifsx.c_str());
	}
	void read(const WTString& ifsx, WTString &ret)
	{
		read(ifsx.c_str(), ret);
	}

	// doesn't eat delims chars at start
	// doesn't eat multiple delims
	WTString read2(LPCSTR ifsx)
	{
		ValidatePtr();
		int n = (int)strcspn(m_ptr, ifsx);
		WTString rstr(m_ptr, n);
		m_ptr += n;
		if (*m_ptr)
			m_ptr++;
		return rstr;
	}
};

LPCSTR strstrWholeWord(LPCSTR txt, LPCSTR pat, BOOL caseSensitive = TRUE);
inline LPCSTR strstrWholeWord(const WTString& txt, const WTString& pat, BOOL caseSensitive = TRUE)
{
	return strstrWholeWord(txt.c_str(), pat.c_str(), caseSensitive);
}
inline LPCSTR strstrWholeWord(const WTString& txt, LPCSTR pat, BOOL caseSensitive = TRUE)
{
	return strstrWholeWord(txt.c_str(), pat, caseSensitive);
}
inline LPCSTR strstrWholeWord(LPCSTR txt, const WTString& pat, BOOL caseSensitive = TRUE)
{
	return strstrWholeWord(txt, pat.c_str(), caseSensitive);
}
LPCWSTR strstrWholeWord(LPCWSTR txt, LPCWSTR pat, BOOL caseSensitive = TRUE);
WTString TokenGetField(LPCSTR str, LPCSTR sep = " \t\r\n", int devLang = -1);
inline WTString TokenGetField(const WTString& str, LPCSTR sep = " \t\r\n", int devLang = -1)
{
	return TokenGetField(str.c_str(), sep, devLang);
}
inline WTString TokenGetField(const WTString& str, const WTString& sep, int devLang = -1)
{
	return TokenGetField(str.c_str(), sep.c_str(), devLang);
}
inline WTString TokenGetField(LPCSTR str, const WTString& sep, int devLang = -1)
{
	return TokenGetField(str, sep.c_str(), devLang);
}
CStringW TokenGetField(LPCWSTR str, LPCWSTR sep = L" \t\r\n", int devLang = -1);

std::string_view TokenGetField2(std::string_view sv, const char* sep = " \t\r\n");
std::string_view TokenGetField2(const char *s, const char* sep = " \t\r\n");
void TokenGetField2InPlace(WTString& s, const char* sep = " \t\r\n");
template <size_t N>
inline std::string_view TokenGetField2(std::string_view sv, std::array<char, N> sep)
{
	// note: _strchr will get inlined and fully optimized to a simple expression in release build
	static const auto _strchr = []<size_t N>(std::array<char, N> sep, char ch) {
		bool ret = false;
		for (size_t i = 0; i < N; ++i)
			ret |= ch == sep[i];
		return ret;
	};

	std::string_view::iterator it = sv.begin();
	for (; it != sv.end() && _strchr(sep, *it); ++it)
	{
	}
	std::string_view::iterator it2 = it;
	for (; it2 != sv.end() && !_strchr(sep, *it2); ++it2)
	{
	}

	return {it, it2};
}
template <size_t N>
inline std::string_view TokenGetField2(const char *s, std::array<char, N> sep)
{
	static const auto _strchr = []<size_t N>(std::array<char, N> sep, char ch) {
		bool ret = false;
		for (size_t i = 0; i < N; ++i)
			ret |= ch == sep[i];
		return ret;
	};

	for (; *s && _strchr(sep, *s); ++s)
	{
	}
	const char *s2 = s;
	for (; *s2 && !_strchr(sep, *s2); ++s2)
	{
	}

	return {s, s2};
}
template <size_t N>
inline void TokenGetField2InPlace(WTString& s, std::array<char, N> sep)
{
	s.MidInPlace(TokenGetField2(s.to_string_view(), sep));
}
std::string_view TokenGetField2(std::string_view sv, char sep);
std::string_view TokenGetField2(const char *s, char sep);
void TokenGetField2InPlace(WTString& s, char sep);




// Language Case sensitivity support
// VB and UC are case insensitive
// We store all symbols in their correct case, but when we do a query, we look using no case
// UC is complicated because in the .uc file, is can access c/c++ symbols in any case,
#define HASHCASEMASK 0x00ffffff
#define HashEqualAC(h1, h2) (g_doDBCase ? (h1 == h2) : ((h1 & HASHCASEMASK) == (h2 & HASHCASEMASK)))
// use this version in loops - capture g_doDBCase to a local const named doDBCase above the loop
#define HashEqualAC_local(h1, h2) (doDBCase ? (h1 == h2) : ((h1 & HASHCASEMASK) == (h2 & HASHCASEMASK)))

#define UseHashEqualAC_fast             uint _hashcasemask_fast = g_doDBCase ? 0xffffffffu : HASHCASEMASK
#define UseHashEqualAC_local_fast       uint _hashcasemask_local_fast = doDBCase ? 0xffffffffu : HASHCASEMASK
#define HashEqualAC_fast(h1, h2)		(((h1) & _hashcasemask_fast) == ((h2) & _hashcasemask_fast))
#define HashEqualAC_local_fast(h1, h2)	(((h1) & _hashcasemask_local_fast) == ((h2) & _hashcasemask_local_fast))


#define StrCmpAC(s1, s2) (g_doDBCase ? _tcscmp(s1, s2) : _tcsicmp(s1, s2))
#define StrCmpACW(s1, s2) (g_doDBCase ? wcscmp(s1, s2) : _wcsicmp(s1, s2))
// use this version in loops - capture g_doDBCase to a local const named doDBCase above the loop
#define StrCmpAC_local(s1, s2) (doDBCase ? _tcscmp(s1, s2) : _tcsicmp(s1, s2))
inline bool StrEqualAC_sv(std::string_view s1, std::string_view s2, int doDBCase)
{
	// warning: the return is negative of StrCmpAC* functions, true for equal
	if(s1.length() != s2.length())
		return false;
	return !(doDBCase ? strncmp : _strnicmp)(s1.data(), s2.data(), s1.length());
}
#define StrEqualAC_local_sv(s1, s2) StrEqualAC_sv((s1), (s2), doDBCase)

extern int g_doDBCase;
BOOL StartsWith(LPCWSTR buf, LPCWSTR begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase); // AutoCase using g_doDBCase flag
BOOL StartsWith(LPCSTR buf, LPCSTR begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase);   // AutoCase using g_doDBCase flag
BOOL StartsWith(std::string_view buf, std::string_view begStr, BOOL wholeWord = true, bool case_sensitive = !!g_doDBCase);
inline BOOL StartsWith(const CStringW& buf, const CStringW& begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase)
{
	return StartsWith((LPCWSTR)buf, (LPCWSTR)begStr, wholeWord, caseSensitive);
} // AutoCase using g_doDBCase flag
inline BOOL StartsWith(const CStringW& buf, LPCWSTR begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase)
{
	return StartsWith((LPCWSTR)buf, begStr, wholeWord, caseSensitive);
} // AutoCase using g_doDBCase flag
inline BOOL StartsWith(const WTString& buf, LPCSTR begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase)
{
	return StartsWith(buf.c_str(), begStr, wholeWord, caseSensitive);
} // AutoCase using g_doDBCase flag
inline BOOL StartsWith(const WTString& buf, const WTString& begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase)
{
	return StartsWith(buf.c_str(), begStr.c_str(), wholeWord, caseSensitive);
} // AutoCase using g_doDBCase flag
inline BOOL StartsWith(LPCSTR buf, const WTString& begStr, BOOL wholeWord = TRUE, bool caseSensitive = !!g_doDBCase)
{
	return StartsWith(buf, begStr.c_str(), wholeWord, caseSensitive);
} // AutoCase using g_doDBCase flag
extern BOOL StartsWithNC(LPCSTR buf, LPCSTR begStr, BOOL wholeWord = TRUE); // Compare NoCase
inline BOOL StartsWithNC(const WTString& buf, const WTString& begStr, BOOL wholeWord = TRUE)
{
	return StartsWithNC(buf.c_str(), begStr.c_str(), wholeWord);
}
inline BOOL StartsWithNC(const WTString& buf, LPCSTR begStr, BOOL wholeWord = TRUE)
{
	return StartsWithNC(buf.c_str(), begStr, wholeWord);
}
inline BOOL StartsWithNC(LPCSTR buf, const WTString& begStr, BOOL wholeWord = TRUE)
{
	return StartsWithNC(buf, begStr.c_str(), wholeWord);
}
extern BOOL StartsWithNC(LPCWSTR buf, LPCWSTR begStr, BOOL wholeWord = TRUE); // Compare NoCase
extern BOOL EndsWith(LPCSTR buf, LPCSTR endStr, BOOL wholeWord = TRUE, bool case_sensitive = !!g_doDBCase);

typedef std::vector<CStringW> WideStrVector;
void WtStrSplitW(const CStringW& str, WideStrVector& arr, LPCWSTR delimiter);

typedef std::vector<WTString> StrVectorA;
void WtStrSplitA(const WTString& str, StrVectorA& arr, LPCSTR delimiter);

template<typename CH>
std::vector<std::basic_string<CH>> WtStrSplit(std::basic_string_view<CH> str, std::basic_string_view<CH> delimiter)
{
// this is an implementation with ranges, but I don't know how to make it work :D
//	std::vector<std::basic_string<CH>> ret = str | std::ranges::lazy_split_view(delimiter) | std::ranges::to<std::vector<std::basic_string<CH>>>();

	std::vector<std::basic_string<CH>> ret2;
	std::basic_string<CH> s2;
	for (auto s : std::ranges::views::split(str, delimiter))
	{
		s2.clear();
		for (CH c : s)
			s2.push_back(c);
		ret2.emplace_back(std::move(s2));
	}
//	assert(ret == ret2);

	return ret2;
}

// Performs a case-insensitive compare of two strings, ignoring all whitespace
bool AreSimilar(const WTString& str1, const WTString& str2);

struct StrMatchOptions
{
	// pat is a string that is searched for in str.
	// pat strings may start with '-' to exclude matches.
	// normalStrs is for string sets that don't start with DB_SEP_CHR
	CStringW mMatchPatternW;
	WTString mMatchPatternA;
	bool mCaseSensitive;
	bool mIsExclusivePattern;
	bool mMatchStartsWith;
	bool mMatchEndsWith;
	bool mEatInitialCharIfDbSep; // check for initial char of DB_SEP_CHR in string set and ignore if present

	StrMatchOptions(const CStringW& pattern, bool normalStrs = true, bool dbDelim = false,
	                bool supportsCaseSensitivityOption = true)
	{
		Update(pattern, normalStrs, dbDelim, supportsCaseSensitivityOption);
	}

	StrMatchOptions(const WTString& pattern, bool normalStrs = true, bool dbDelim = false,
	                bool supportsCaseSensitivityOption = true)
	{
		Update(CStringW(pattern.Wide()), normalStrs, dbDelim, supportsCaseSensitivityOption);
	}

	void Update(const CStringW& pattern, bool normalStrs = true, bool dbDelim = false,
	            bool supportsCaseSensitivityOption = true);

	static CStringW TweakFilter(const CStringW& filter, bool forFileSearch = false, bool dbDelim = false);
	static CStringW TweakFilter(const WTString& filter, bool dbDelim = false)
	{
		return TweakFilter(CStringW(filter.Wide()), false, dbDelim);
	}
};
template<typename CH>
struct StrMatchOptionsT
{
	// pat is a string that is searched for in str.
	// pat strings may start with '-' to exclude matches.
	// normalStrs is for string sets that don't start with DB_SEP_CHR
	std::basic_string<CH> mMatchPattern;
	std::basic_string<CH> mMatchPattern_uppercase;
	bool mCaseSensitive;
	bool mIsExclusivePattern;
	bool mMatchStartsWith;
	bool mMatchEndsWith;
	bool mEatInitialCharIfDbSep; // check for initial char of DB_SEP_CHR in string set and ignore if present

	std::optional<uint32_t> mFuzzy = {};
	bool mFuzzyLite = true;
	bool mEnableFuzzyMultiThreading = true;

	StrMatchOptionsT(std::basic_string_view<CH> pattern, bool normalStrs = true, bool dbDelim = false,
	                bool supportsCaseSensitivityOption = true);

	void Update(std::basic_string_view<CH> pattern, bool normalStrs = true, bool dbDelim = false,
	            bool supportsCaseSensitivityOption = true);

	uint32_t GetFuzzy() const;
	bool GetFuzzyLite() const;

	static std::basic_string<CH> TweakFilter2(std::basic_string<CH> filter, bool forFileSearch, bool dbDelim);
};
// temporary strings reused to avoid allocations
template<typename CH>
struct StrMatchTempStorageT
{
	std::basic_string<CH> matchPattern; // matchBothSlashAndBackslash
	std::basic_string<CH> s2; // fuzzy/fuzzylite
	std::basic_string<CH> pat; // fuzzy
};

// str is a string to be searched.
// StrMatchRanked supports a single search pattern (set via StrMatchOptions).
// returns 0 if no match;  higher return values are stronger matches
// TODO: remove StrMatchRankedA and StrMatchRankedW; use only StrMatchRankedT
int StrMatchRankedA(const LPCSTR str, const StrMatchOptions& opt);
int StrMatchRankedW(const LPCWSTR str, const StrMatchOptions& opt, bool matchBothSlashAndBackslash);
template<typename CH>
int8_t StrMatchRankedT(std::basic_string_view<CH> str, const StrMatchOptionsT<CH>& opt, StrMatchTempStorageT<CH> &temp_storage, bool matchBothSlashAndBackslash = false);

// pat can be multiple strings separated by spaces (str must match all strings in pat) for StrMultiMatchRanked.
// a sub-pattern is interpreted case-sensitive if it contains any upper-case letters, otherwise is case-insensitive
// multiple search patterns can separated by commas which act as a logical OR.
// "A B, C" == (A && B) || C
// not recommended for large search sets
// returns > 0 for matches
bool StrIsMultiMatchPattern(LPCSTR pat);
bool StrIsMultiMatchPattern(const CStringW& pat);
template<typename CH>
inline bool StrIsMultiMatchPattern(std::basic_string_view<CH> pat)
{
	static const std::basic_string<CH> chars = {CH{' '}, CH{','}};
	return pat.find_first_of(chars) != pat.npos;
}
int StrMultiMatchRanked(LPCSTR str, LPCSTR pat);
int StrMultiMatchRanked(const CStringW& str, const CStringW& pat, bool matchBothSlashAndBackslash);

extern int g_vaCharWidth[256];
int GetStrWidth(LPCSTR str);                       // Gets display width of string in edit control with non-bold fonts
int GetStrWidthEx(LPCSTR str, int (&widths)[256]); // Pads with with 10 pixels.

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(0) // disable during debug builds
#endif

#endif
