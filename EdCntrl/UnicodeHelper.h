#pragma once
#include <afxstr.h>
#include <functional>

class UnicodeHelper
{
	// Win32 nor STL provide libraries for detecting Unicode characters above BMP (Basic Multilingual Plane - Plane 00).
	// BPM contains characters up to 0xffff, but there are more planes in Unicode we should handle if we want to say
	// that we support Unicode. IDE is implemented in .NET framework, so they have access to Unicode aware library. This
	// class is small helper to work with Unicode, it helps in all situations that we should face to and if not, any
	// functionality can be added later.

	// detailed information about problem
	// http://archives.miloush.net/michkap/archive/2011/11/21/10239128.html

	// look at following pages for examples of chars over BPM:
	// https://en.wikibooks.org/wiki/Unicode/Character_reference/10000-10FFF
	// https://en.wikibooks.org/wiki/Unicode/Character_reference/20000-20FFF

	// how the .NET solves the problem
	// https://github.com/dotnet/coreclr/blob/master/src/System.Private.CoreLib/shared/System/Globalization/CharUnicodeInfo.cs
	// https://github.com/dotnet/coreclr/blob/master/src/System.Private.CoreLib/shared/System/Globalization/CharUnicodeInfoData.cs

	// to create tables of Unicode characters/ranges used in this class the public UnicodeData.txt file was used:
	// http://www.unicode.org/Public/UNIDATA/UnicodeData.txt

  public:
	enum Planes : char32_t
	{
		Plane00_End = 0x00ffff,   // end of basic multilingual plane
		Plane01_Start = 0x010000, // start of supplementary multilingual plane
		Plane16_End = 0x10ffff    // maximum Unicode code point due to surrogate pair limits
	};

	// https://en.wikipedia.org/wiki/Unicode_equivalence#Normal_forms
	// https://docs.microsoft.com/sk-sk/windows/desktop/Intl/using-unicode-normalization-to-represent-strings
	enum TransformMethod
	{
		TFM_None = 0, // No transform

		TFM_NormalFormC = 0x1,  // Canonical Composition
		TFM_NormalFormD = 0x2,  // Canonical Decomposition
		TFM_NormalFormKC = 0x5, // Compatibility Composition
		TFM_NormalFormKD = 0x6, // Compatibility Decomposition

		TFM_ExpandLigatures = 0x10, // Expansion of ligatures
	};

	enum MappingFlags
	{
		MF_WinAPI = 0x0,            // uses only Win API which has limited support
		MF_WinAPI_Linguistic = 0x1, // applies if MF_Accurate is not present
		MF_Accurate = 0x2,          // uses local Unicode maps (Win32 API has limited support)
	};

	static const char8_t* ReadUtf8(const char8_t* pz, char32_t& c);
	static char32_t FromSurrogatePair(WCHAR hi, WCHAR lo);
	static bool ToSurrogatePair(char32_t ch, WCHAR& hi, WCHAR& lo);
	static bool IsValidCodePoint(char32_t ch)
	{
		return ch <= Plane16_End;
	}
	static bool IsSurrogatePair(WCHAR hi, WCHAR lo);
	static bool IsHighSurrogate(WCHAR ch);
	static bool IsLowSurrogate(WCHAR ch);

	// returns current Unicode char and moves pos to next one
	static char32_t MoveToNextCodePoint(LPCWSTR& pos);
	static char32_t MoveToNextCodePoint(LPCWSTR str, int& pos);

	// returns current Unicode char
	static char32_t ReadCodePoint(LPCWSTR str);

	// returns true if str points to decomposed text element consisting of a base character/surrogate and combining
	// characters/surrogates sequence length is assigned regardless of returned value with following logic:
	//   if text element is combining sequence
	//       length = sum of lengths of base character/surrogate and following combining characters/surrogates
	//   else if text element is surrogate pair
	//       length = 2
	//   else if text element is character in plane 0, but not \0 char
	//       length = 1
	//   else (text element is \0 char or str is NULL)
	//       length = 0
	// Note: If you don't need the sequence length, keep it NULL and you'll get result faster
	static bool IsDecomposedTextElement(LPCWSTR str, int* sequence_wchar_length = nullptr);

	// uses IsDecomposedTextElement to get current text element (grapheme) length
	static int GetTextElementLength(LPCWSTR str, bool* is_decomposed = nullptr);

	// fills the vector or invokes functor for each text element in the string
	// returns count of processed text elements (graphemes)
	//		functor/tuple attributes are: int startIndex, int length, [bool is_decomposed]
	//		functor returns true to continue iteration, false otherwise
	static int GetTextElements(std::function<bool(int, int, bool)> func, LPCWSTR str, int length = -1);
	static int GetTextElements(std::vector<std::tuple<int, int, bool>>& vec, LPCWSTR str, int length = -1);
	static int GetTextElements(std::vector<std::tuple<int, int>>& vec, LPCWSTR str, int length = -1);

	// fills the vector or invokes functor for each character or surrogate pair in the string
	// returns count of processed characters/surrogates
	//		functor/tuple attributes are: int startIndex, int length
	//		functor returns true to continue iteration, false otherwise
	static int GetCodePoints(std::function<bool(int, int)> func, LPCWSTR str, int length = -1);
	static int GetCodePoints(std::vector<std::tuple<int, int>>& vec, LPCWSTR str, int length = -1);

	// appends the code point into string either as single character or as surrogate pair
	static void AppendToString(char32_t ch, CStringW& str);

	// maps the string using LCMapStringW API (not very useful)
	static bool MapString(CStringW& str, DWORD flags);
	static bool MapString(const CStringW& src, CStringW& dst, DWORD flags);

	// compares strings using CompareStringW, converts returned value
	// returns -1 for CSTR_LESS_THAN, 0 for CSTR_EQUAL, 1 for CSTR_GREATER_THAN
	static int CompareStrings(const CStringW& strL, const CStringW& strR, DWORD flags = 0,
	                          BOOL return_as_strcmp = TRUE);
	static int CompareStrings(LPCWSTR str0, int chCount0, LPCWSTR str1, int chCount1, DWORD flags = 0,
	                          BOOL return_as_strcmp = TRUE);

	// calculates how many edit operations are needed to transform str0 into str1
	// if textElements == true, operations are counted in text elements (graphemes)
	// otherwise operations are counted in characters/surrogates
	static int EditDistance(LPCWSTR str0, int chCount0, LPCWSTR str1, int chCount1, BOOL textElements = TRUE,
	                        DWORD flags = 0);
	static int EditDistance(const CStringW& str0, const CStringW& str1, BOOL textElements = TRUE, DWORD flags = 0);

	static char32_t MapCodePoint(char32_t ch, DWORD flags);    // code point version of LCMapStringW
	static WORD GetCodePointType(char32_t ch, DWORD infoType); // code point version of GetStringTypeExW

	static bool MakeCamelPascal(BOOL camel, const CStringW& src, CStringW& dst, MappingFlags mapping = MF_Accurate);

	static void MakeUpper(const CStringW& src, CStringW& dst, MappingFlags mapping = MF_Accurate);
	static void MakeUpper(CStringW& str, MappingFlags mapping = MF_Accurate)
	{
		MakeUpper(str, str, mapping);
	}
	static void MakeLower(const CStringW& src, CStringW& dst, MappingFlags mapping = MF_Accurate);
	static void MakeLower(CStringW& str, MappingFlags mapping = MF_Accurate)
	{
		MakeLower(str, str, mapping);
	}
	static void MakeCamel(CStringW& str, MappingFlags mapping = MF_Accurate)
	{
		MakeCamelPascal(TRUE, str, str, mapping);
	}
	static void MakeCamel(const CStringW& src, CStringW& dst, MappingFlags mapping = MF_Accurate)
	{
		MakeCamelPascal(TRUE, src, dst, mapping);
	}
	static void MakePascal(CStringW& str, MappingFlags mapping = MF_Accurate)
	{
		MakeCamelPascal(FALSE, str, str, mapping);
	}
	static void MakePascal(const CStringW& src, CStringW& dst, MappingFlags mapping = MF_Accurate)
	{
		MakeCamelPascal(FALSE, src, dst, mapping);
	}

	static bool IsLatin1(char32_t ch)
	{
		return ch <= 0xff;
	}
	static bool IsAscii(char32_t ch)
	{
		return ch <= 0x7f;
	}
	static bool IsInPlane0(char32_t ch)
	{
		return ch < Plane01_Start;
	}
	static bool IsSupplementary(char32_t ch)
	{
		return ch >= Plane01_Start;
	}
	static int WideCharLength(char32_t ch)
	{
		return ch < Plane01_Start ? 1 : 2;
	}

	static bool IsUpper(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsLower(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsAlpha(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsDigit(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsNumber(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsAlNum(char32_t ch, MappingFlags mapping = MF_Accurate);
	static bool IsWhiteSpace(char32_t ch, MappingFlags mapping = MF_Accurate);

	// Normalizes string into specified form.
	// This method calls FoldString method if Normaliz.dll is missing,
	// or in cases like digits mapping and ligatures expansion.
	static bool TransformString(CStringW& str, TransformMethod form, BOOL mapDigitsToASCII = FALSE);
	static bool TransformString(const CStringW& src, CStringW& dst, TransformMethod form,
	                            BOOL mapDigitsToASCII = FALSE);

	// Deprecated method for string transformations for pre-Vista OSes where NormalizeString is undefined.
	// Our TransformString method also calls this method in cases like digits mapping or ligatures expansion.
	static bool FoldString(const CStringW& src, CStringW& dst, TransformMethod form, BOOL mapDigitsToASCII = FALSE);

	// Combines composite string into precomposed form
	static bool PrecomposeString(CStringW& str, BOOL compatibility);
	static bool PrecomposeString(const CStringW& src, CStringW& dst, BOOL compatibility);

	// Decomposes precomposed string into composite form
	static bool DecomposeString(CStringW& str, BOOL compatibility);
	static bool DecomposeString(const CStringW& src, CStringW& dst, BOOL compatibility);

	// Maps all Unicode digits to range in ASCII 0 to 9
	// Note: This is data loss transformation and there is no way to get back the original string!
	static bool MapDigitsToASCII(CStringW& str);
	static bool MapDigitsToASCII(const CStringW& src, CStringW& dst);

	// Expands Unicode ligatures like fi or ae into separate chars
	// Note: This is data loss transformation and there is no way to get back the original string!
	static bool ExpandLigatures(CStringW& str);
	static bool ExpandLigatures(const CStringW& src, CStringW& dst);

	class TextElementIterator
	{
		LPCWSTR _lpStr = nullptr;
		int _index = -1;
		int _length = -1;
		char32_t _current = 0;
		int _elm_len = -1;
		bool _elm_decomposed = false;

	  public:
		TextElementIterator(LPCWSTR wstr, int length = -1);

		// resets the iterator and sets new string to iterate
		// you must call one of MoveToNext* to bring it to the start
		void Reset(LPCWSTR wstr, int length = -1);

		// resets the iterator, so you must call one of MoveToNext* to bring it to the start
		void Reset();

		// returns current Utf32 code point
		char32_t CurrentCodePoint() const;

		// Tries to return the current text element in precomposed form.
		// If the normalization fails, returned is current Utf32 code point.
		char32_t CurrentTextElementPrecomposed() const;

		// returns current Utf32 character as a wide string
		CStringW CurrentCodePointText() const;

		// returns next Utf32 character
		// if in uninitialized state (not called any of MoveToNext*), returns the first code point
		char32_t NextCodePoint() const;

		// returns the start of iterated string
		LPCWSTR StartPos() const;

		// returns the current index counted in WCHARS (Utf16)
		// this is same as CurrentPos but in integer form
		// CurrentPos = StartPos + CurrentIndex
		int CurrentIndex() const;

		// returns next position on the iterated string
		LPCWSTR NextPos() const;

		// returns current position within iterated string
		// CurrentPos = StartPos + CurrentIndex
		LPCWSTR CurrentPos() const;

		// returns current grapheme (text displayed as a single character)
		CStringW CurrentTextElement(bool* is_decomposed = nullptr) const;

		// returns the length of the current grapheme (text displayed as a single character)
		int CurrentTextElementLength(bool* is_decomposed = nullptr) const;

		// returns count of Utf16 chars defining current Utf32 code point
		int CurrentCodePointLength() const;

		// moves the index to the next grapheme (text displayed as a single character)
		bool MoveToNextTextElement();

		// moves the index to the next utf32 code point
		bool MoveToNextCodePoint();
	};
};