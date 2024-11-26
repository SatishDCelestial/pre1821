#include "stdafxed.h"
#include "UnicodeHelper.h"
#include "Utf32CharInfo.h"
#include "Settings.h"
#include "Log.h"
#include "Library.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define VaLCID ((Psettings && Psettings->m_doLocaleChange) ? LOCALE_SYSTEM_DEFAULT : GetThreadLocale())

static const char8_t Utf8Trans[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

const char8_t* Utf8Read(const char8_t* pz, char32_t& c)
{
	c = *(pz++);
	if (c >= 0xc0)
	{
		c = Utf8Trans[c - 0xc0];
		while ((*pz & 0xc0) == 0x80)
		{
			c = (c << 6) + (0x3f & *(pz++));
		}
		if (c < 0x80 || (c & 0xFFFFF800) == 0xD800 || (c & 0xFFFFFFFE) == 0xFFFE)
		{
			c = 0xFFFD;
		}
	}

	return pz;
}


bool normalizeString(LPCWSTR src, int srcLen, CStringW& dst, UnicodeHelper::TransformMethod form);
bool foldString(LPCWSTR src, int srcLen, CStringW& dst, UnicodeHelper::TransformMethod form,
                BOOL mapDigitsToASCII /*= FALSE*/);

template <bool check_locale = true, bool fromTitleCase = true> struct ToLowerHelper
{
	static bool is_special_locale()
	{
		switch (Utf32GetLocaleHash())
		{
		case 'az':
		case 'tr':
			return true;
		}

		return false;
	}

	static int map_char(LPCWSTR key, int length, CStringW& rslt)
	{
		char32_t ch = length == 2 ? UnicodeHelper::FromSurrogatePair(key[0], key[1]) : key[0];

#pragma warning(push)
#pragma warning(disable : 4127)
		if (!fromTitleCase && Utf32IsTitlecaseLetter(ch))
			return 0;
#pragma warning(pop)

		if (check_locale) // block is ignored by compiler if !check_locale
		{
			switch (Utf32GetLocaleHash())
			{
			case 'az':
			case 'tr':
				if (key[1] == 0x307 && key[0] == 'I' && !Utf32IsCombining(key[2]))
				{
					rslt += 'i';
					return 2; // processed 2 chars!
				}
				break;
			}
		}

		LPCWSTR mapping = Utf32ToLower(ch);
		if (mapping)
		{
			rslt += mapping;
			return length;
		}
		return 0;
	}

	static void map_str(const CStringW& src, CStringW& dst, UnicodeHelper::MappingFlags mapping)
	{
		if (mapping & UnicodeHelper::MF_Accurate)
		{
			CStringW resultStr;
			resultStr.Preallocate(src.GetLength() * 5 / 4);

			LPCWSTR lpStr = src;
			LPCWSTR lpStrEnd = lpStr + src.GetLength();

			for (int codeLen = 1; lpStr < lpStrEnd; lpStr += codeLen)
			{
				codeLen = IS_SURROGATE_PAIR(lpStr[0], lpStr[1]) ? 2 : 1;
				int processed = map_char(lpStr, codeLen, resultStr);
				if (!processed)
					resultStr.Append(lpStr, codeLen);
				else
					codeLen = processed;
			}

			std::swap(resultStr, dst);
		}
		else
		{
			DWORD flags = LCMAP_LOWERCASE;
			if (mapping & UnicodeHelper::MF_WinAPI_Linguistic)
				flags |= LCMAP_LINGUISTIC_CASING;

			UnicodeHelper::MapString(src, dst, flags);
		}
	}
};

template <bool check_locale = true, bool fromTitleCase = true> struct ToUpperTitleHelper
{
	static bool is_special_locale()
	{
		switch (Utf32GetLocaleHash())
		{
		case 'lt':
			return true;
		}

		return false;
	}

	static int map_char(LPCWSTR key, int length, BOOL toTitleCase, CStringW& rslt)
	{
		char32_t ch = length == 2 ? UnicodeHelper::FromSurrogatePair(key[0], key[1]) : key[0];

#pragma warning(push)
#pragma warning(disable : 4127)
		if (!fromTitleCase && Utf32IsTitlecaseLetter(ch))
			return 0;
#pragma warning(pop)

		if (check_locale) // skipped by compiler if check_locale == false
		{
			switch (Utf32GetLocaleHash())
			{
			case 'lt': {
				// special casing for Lithuanian locale with decomposed I/J characters with special accents
				if (key[0] == 0x69 || key[0] == 0x6a || key[0] == 0x12f)
				{
					UnicodeHelper::TextElementIterator it(key);
					if (it.MoveToNextCodePoint())
					{
						bool is_decomposed = false;
						int elm_len = it.CurrentTextElementLength(&is_decomposed);
						if (elm_len > 1 && is_decomposed)
						{
							CStringW elm = it.CurrentTextElement();
							int dot_above = elm.Find(L'\u0307');
							if (dot_above >= 0)
							{
								elm.Remove(L'\u0307');

								switch (elm[0])
								{
								case 0x69:
									if (elm.GetLength() == 1)
										elm = L"\u0049";
									else if (elm.GetLength() == 2)
									{
										switch (elm[1])
										{
										case 0x300:
											elm = L"\u00CC";
											break;
										case 0x301:
											elm = L"\u00CD";
											break;
										case 0x303:
											elm = L"\u0128";
											break;
										}
									}
									break;
								case 0x6A:
									if (elm.GetLength() == 1)
										elm = L"\u004A";
									break;
								case 0x12F:
									if (elm.GetLength() == 1)
										elm = L"\u012E";
									break;
								}

								rslt += elm;
								return elm_len;
							}
						}
					}
				}
				break;
			}
			}
		}

		LPCWSTR mapping = Utf32ToUpperOrTitle(ch, toTitleCase);
		if (mapping)
		{
			rslt += mapping;
			return length;
		}
		return 0;
	}

	static void map_str(const CStringW& src, CStringW& dst, BOOL toTitleCase, UnicodeHelper::MappingFlags mapping)
	{
		if (mapping & UnicodeHelper::MF_Accurate)
		{
			CStringW resultStr;
			resultStr.Preallocate(src.GetLength() * 5 / 4);

			LPCWSTR lpStr = src;
			LPCWSTR lpStrEnd = lpStr + src.GetLength();

			for (int codeLen = 1; lpStr < lpStrEnd; lpStr += codeLen)
			{
				codeLen = IS_SURROGATE_PAIR(lpStr[0], lpStr[1]) ? 2 : 1;

				int processed = map_char(lpStr, codeLen, toTitleCase, resultStr);
				if (processed == 0)
					resultStr.Append(lpStr, codeLen);
				else
					codeLen = processed;
			}

			std::swap(resultStr, dst);
		}
		else
		{
			DWORD flags = LCMAP_UPPERCASE;
			if (mapping & UnicodeHelper::MF_WinAPI_Linguistic)
				flags |= LCMAP_LINGUISTIC_CASING;

			UnicodeHelper::MapString(src, dst, flags);
		}
	}
};

int compareTextElements(LPCWSTR elm0, int elm0len, LPCWSTR elm1, int elm1len, DWORD flags)
{
	if (!(flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)))
	{
		return CompareStringW(VaLCID, flags, elm0, elm0len, elm1, elm1len) - CSTR_EQUAL;
	}
	else
	{
		int len0 = IS_SURROGATE_PAIR(elm0[0], elm0[1]) ? 2 : 1;
		int len1 = IS_SURROGATE_PAIR(elm1[0], elm1[1]) ? 2 : 1;

		CStringW upper0, upper1;
		bool sp_locale = ToUpperTitleHelper<>::is_special_locale();
		if (sp_locale)
		{
			ToUpperTitleHelper<true, true>::map_char(elm0, len0, FALSE, upper0);
			ToUpperTitleHelper<true, true>::map_char(elm1, len1, FALSE, upper1);
		}
		else
		{
			ToUpperTitleHelper<false, true>::map_char(elm0, len0, FALSE, upper0);
			ToUpperTitleHelper<false, true>::map_char(elm1, len1, FALSE, upper1);
		}

		if (len0 != elm0len)
			upper0.Append(elm0 + len0, elm0len - len0);

		if (len1 != elm1len)
			upper1.Append(elm1 + len1, elm1len - len1);

		return CompareStringW(VaLCID, flags, upper0, upper0.GetLength(), upper1, upper1.GetLength()) - CSTR_EQUAL;
	}
}

bool UnicodeHelper::MapString(CStringW& str, DWORD flags)
{
	return MapString(str, str, flags);
}

bool UnicodeHelper::MapString(const CStringW& src, CStringW& dst, DWORD flags)
{
	if (std::addressof(src) == std::addressof(dst))
	{
		CStringW rslt;
		if (MapString(dst, rslt, flags))
		{
			std::swap(rslt, dst);
			return true;
		}
		return false;
	}

	int newLength = ::LCMapStringW(VaLCID, flags, src, src.GetLength(), nullptr, 0);
	if (newLength)
	{
		newLength = ::LCMapStringW(VaLCID, flags, src, src.GetLength(), dst.GetBuffer(newLength), newLength);
		dst.ReleaseBufferSetLength(newLength);
		return true;
	}
	return false;
}

int UnicodeHelper::CompareStrings(const CStringW& strL, const CStringW& strR, DWORD flags /*= 0*/,
                                  BOOL return_as_strcmp /*= TRUE*/)
{
	return CompareStringW(VaLCID, flags, strL, -1, strR, -1) - (return_as_strcmp ? CSTR_EQUAL : 0);
}

int UnicodeHelper::CompareStrings(LPCWSTR str0, int chCount0, LPCWSTR str1, int chCount1, DWORD flags /*= 0*/,
                                  BOOL return_as_strcmp /*= TRUE*/)
{
	return CompareStringW(VaLCID, flags, str0, chCount0, str1, chCount1) - (return_as_strcmp ? CSTR_EQUAL : 0);
}

int UnicodeHelper::GetTextElements(std::vector<std::tuple<int, int, bool>>& vec, LPCWSTR str, int length /*= -1*/)
{
	return GetTextElements(
	    [&](int index, int length, bool decoposed) {
		    vec.emplace_back(std::make_tuple(index, length, decoposed));
		    return true;
	    },
	    str, length);
}
int UnicodeHelper::GetTextElements(std::vector<std::tuple<int, int>>& vec, LPCWSTR str, int length /*= -1*/)
{
	return GetTextElements(
	    [&](int index, int length, bool decomposed) {
		    (void)decomposed;
		    vec.emplace_back(std::make_tuple(index, length));
		    return true;
	    },
	    str, length);
}

// fills the vector with characters and surrogates in the string
int UnicodeHelper::GetCodePoints(std::vector<std::tuple<int, int>>& vec, LPCWSTR str, int length /*= -1*/)
{
	return GetCodePoints(
	    [&](int index, int length) {
		    vec.emplace_back(std::make_tuple(index, length));
		    return true;
	    },
	    str, length);
}

int UnicodeHelper::GetTextElements(std::function<bool(int, int, bool)> func, LPCWSTR str, int length /*= -1*/)
{
	int count = 0;
	TextElementIterator it(str, length);
	int textElmLen;
	bool textElmDecomposed;
	while (it.MoveToNextTextElement())
	{
		count++;
		textElmLen = it.CurrentTextElementLength(&textElmDecomposed);
		if (!func(it.CurrentIndex(), textElmLen, textElmDecomposed))
			break;
	}
	return count;
}

int UnicodeHelper::GetCodePoints(std::function<bool(int, int)> func, LPCWSTR str, int length /*= -1*/)
{
	int count = 0;
	TextElementIterator it(str, length);
	while (it.MoveToNextCodePoint())
	{
		count++;
		if (!func(it.CurrentIndex(), it.CurrentCodePointLength()))
			break;
	}
	return count;
}

int UnicodeHelper::EditDistance(const CStringW& str0, const CStringW& str1, BOOL textElements /*= TRUE*/,
                                DWORD flags /*= 0*/)
{
	return EditDistance(str0, str0.GetLength(), str1, str1.GetLength(), textElements, flags);
}

int editDistanceTextElements(LPCWSTR str0, int chCount0, LPCWSTR str1, int chCount1, DWORD flags)
{
	std::vector<std::tuple<int, int>> te0, te1;

	UnicodeHelper::GetTextElements(te0, str0, chCount0);
	UnicodeHelper::GetTextElements(te1, str1, chCount1);

	std::vector<std::vector<int>> matrix(te0.size() + 1, std::vector<int>(te1.size() + 1, 0));
	std::vector<CStringW> normalized0(te0.size());
	std::vector<CStringW> normalized1(te1.size());

	const auto min_int = [](int x, int y, int z) { return __min(x, __min(y, z)); };

	const auto get0 = [&](size_t x) {
		CStringW& wstr = normalized0[x];
		if (wstr.IsEmpty())
			normalizeString(str0 + std::get<0>(te0[x]), std::get<1>(te0[x]), wstr, UnicodeHelper::TFM_NormalFormD);
		return wstr;
	};

	const auto get1 = [&](size_t x) {
		CStringW& wstr = normalized1[x];
		if (wstr.IsEmpty())
			normalizeString(str1 + std::get<0>(te1[x]), std::get<1>(te1[x]), wstr, UnicodeHelper::TFM_NormalFormD);
		return wstr;
	};

	const auto compare = [&](const CStringW& x, const CStringW& y) {
		return compareTextElements(x, x.GetLength(), y, y.GetLength(), flags);
	};

	for (size_t i = 0; i <= te0.size(); i++)
	{
		for (size_t j = 0; j <= te1.size(); j++)
		{
			// if first string is empty, only option is to
			// insert all characters of second string
			if (i == 0)
				matrix[i][j] = (int)j; // Min. operations = j

			// if second string is empty, only option is to
			// remove all characters of second string
			else if (j == 0)
				matrix[i][j] = (int)i; // Min. operations = i

			// if last text elements are same, ignore last text element
			// and recur for remaining string
			else if (!compare(get0(i - 1), get1(j - 1)))
				matrix[i][j] = matrix[i - 1][j - 1];

			// if the last character is different, consider all
			// possibilities and find the minimum
			else
				matrix[i][j] = 1 + min_int(matrix[i][j - 1],      // insert
				                           matrix[i - 1][j],      // remove
				                           matrix[i - 1][j - 1]); // replace
		}
	}

	return matrix[te0.size()][te1.size()];
}

int UnicodeHelper::EditDistance(LPCWSTR str0, int chCount0, LPCWSTR str1, int chCount1, BOOL textElements /*= TRUE*/,
                                DWORD flags /*= 0*/)
{
	if (textElements)
		return editDistanceTextElements(str0, chCount0, str1, chCount1, flags);

	std::vector<std::tuple<int, int>> te0, te1;

	GetCodePoints(te0, str0, chCount0);
	GetCodePoints(te1, str1, chCount1);

	std::vector<std::vector<int>> matrix(te0.size() + 1, std::vector<int>(te1.size() + 1, 0));

	const auto compare = [&](int x, int y) {
		return UnicodeHelper::CompareStrings(str0 + std::get<0>(te0[(uint)x]), std::get<1>(te0[(uint)x]),
		                                     str1 + std::get<0>(te1[(uint)y]), std::get<1>(te1[(uint)y]), flags);
	};

	const auto min_int = [](int x, int y, int z) { return __min(x, __min(y, z)); };

	for (size_t i = 0; i <= te0.size(); i++)
	{
		for (size_t j = 0; j <= te1.size(); j++)
		{
			// if first string is empty, only option is to
			// insert all characters of second string
			if (i == 0)
				matrix[i][j] = (int)j; // Min. operations = j

			// if second string is empty, only option is to
			// remove all characters of second string
			else if (j == 0)
				matrix[i][j] = (int)i; // Min. operations = i

			// if last text elements are same, ignore last text element
			// and recur for remaining string
			else if (!compare((int)i - 1, (int)j - 1))
				matrix[i][j] = matrix[i - 1][j - 1];

			// if the last character is different, consider all
			// possibilities and find the minimum
			else
				matrix[i][j] = 1 + min_int(matrix[i][j - 1],      // insert
				                           matrix[i - 1][j],      // remove
				                           matrix[i - 1][j - 1]); // replace
		}
	}

	return matrix[te0.size()][te1.size()];
}

char32_t UnicodeHelper::MapCodePoint(char32_t ch, DWORD flags)
{
	if (!IsValidCodePoint(ch))
		return ch;

	WCHAR buf_in[2] = {0};
	WCHAR buf_out[2] = {0};

	if (ch < Plane01_Start)
		buf_in[0] = (WCHAR)ch;
	else if (!ToSurrogatePair(ch, buf_in[0], buf_in[1]))
		return ch;

	if (::LCMapStringW(VaLCID, flags, buf_in, buf_in[1] ? 2 : 1, buf_out, 2))
	{
		if (buf_out[1])
		{
			_ASSERTE(IsSurrogatePair(buf_out[0], buf_out[1]));

			if (IsSurrogatePair(buf_out[0], buf_out[1]))
				return FromSurrogatePair(buf_out[0], buf_out[1]);

			return ch;
		}

		return buf_out[0];
	}

	return ch;
}

WORD UnicodeHelper::GetCodePointType(char32_t ch, DWORD infoType)
{
	if (!IsValidCodePoint(ch))
		return 0;

	WCHAR buf_in[2] = {0};
	WORD type_out[2] = {0};

	if (ch < Plane01_Start)
		buf_in[0] = (WCHAR)ch;
	else if (!ToSurrogatePair(ch, buf_in[0], buf_in[1]))
		return 0;

	if (::GetStringTypeExW(VaLCID, infoType, buf_in, buf_in[1] ? 2 : 1, type_out))
		return WORD(type_out[0] | type_out[1]);

	return 0;
}

const char8_t* UnicodeHelper::ReadUtf8(const char8_t* pz, char32_t& c)
{
	return Utf8Read(pz, c);
}

char32_t UnicodeHelper::FromSurrogatePair(WCHAR hi, WCHAR lo)
{
	_ASSERTE(IS_SURROGATE_PAIR(hi, lo));
	return (UINT)(((INT)hi - HIGH_SURROGATE_START) * 0x400) + (UINT)((INT)lo - LOW_SURROGATE_START) + Plane01_Start;
}

bool UnicodeHelper::ToSurrogatePair(char32_t ch, WCHAR& hi, WCHAR& lo)
{
	_ASSERTE(ch <= Plane16_End);
	if (ch >= Plane01_Start && ch <= Plane16_End)
	{
		ch -= Plane01_Start;
		hi = static_cast<WCHAR>(((INT)ch / 0x400) + HIGH_SURROGATE_START);
		lo = static_cast<WCHAR>(((INT)ch % 0x400) + LOW_SURROGATE_START);
		return true;
	}
	return false;
}

bool UnicodeHelper::IsSurrogatePair(WCHAR hi, WCHAR lo)
{
	return IS_SURROGATE_PAIR(hi, lo);
}

bool UnicodeHelper::IsHighSurrogate(WCHAR ch)
{
	return IS_HIGH_SURROGATE(ch);
}

bool UnicodeHelper::IsLowSurrogate(WCHAR ch)
{
	return IS_LOW_SURROGATE(ch);
}

char32_t UnicodeHelper::MoveToNextCodePoint(LPCWSTR& pos)
{
	if (!pos)
		return 0;

	if (IsHighSurrogate(pos[0]) && IsLowSurrogate(pos[1]))
	{
		char32_t code = FromSurrogatePair(pos[0], pos[1]);
		pos += 2;
		return code;
	}

	return *pos++;
}

char32_t UnicodeHelper::MoveToNextCodePoint(LPCWSTR str, int& pos)
{
	if (!str)
		return 0;

	if (IsHighSurrogate(str[pos]) && IsLowSurrogate(str[pos + 1]))
	{
		char32_t code = FromSurrogatePair(str[pos], str[pos + 1]);
		pos += 2;
		return code;
	}

	return str[pos++];
}

char32_t UnicodeHelper::ReadCodePoint(LPCWSTR str)
{
	if (!str)
		return 0;

	if (IsHighSurrogate(str[0]) && IsLowSurrogate(str[1]))
		return FromSurrogatePair(str[0], str[1]);

	return *str;
}

int UnicodeHelper::GetTextElementLength(LPCWSTR str, bool* is_decomposed /*= nullptr*/)
{
	int len = 0;

	if (is_decomposed)
		*is_decomposed = IsDecomposedTextElement(str, &len);
	else
		IsDecomposedTextElement(str, &len);

	return len;
}

bool UnicodeHelper::IsDecomposedTextElement(LPCWSTR str, int* sequence_wchar_length /*= nullptr*/)
{
	if (sequence_wchar_length)
		*sequence_wchar_length = 0;

	char32_t current = ReadCodePoint(str);
	if (current)
	{
		if (sequence_wchar_length)
			*sequence_wchar_length = WideCharLength(current);

		char32_t next = ReadCodePoint(str + WideCharLength(current));
		if (next && Utf32IsCombining(next))
		{
			// if current is not a valid base char, return false
			if (!Utf32IsBase(current))
				return false;

			if (sequence_wchar_length)
			{
				*sequence_wchar_length += WideCharLength(next);

				// read all following combining chars while adding length
				next = ReadCodePoint(str + *sequence_wchar_length);
				while (next && Utf32IsCombining(next))
				{
					*sequence_wchar_length += WideCharLength(next);
					next = ReadCodePoint(str + *sequence_wchar_length);
				}
			}

			return true;
		}
	}

	return false;
}

void UnicodeHelper::AppendToString(char32_t ch, CStringW& str)
{
	if (!IsValidCodePoint(ch))
		return;

	if (ch >= Plane01_Start)
	{
		WCHAR lo, hi;
		if (ToSurrogatePair(ch, hi, lo))
		{
			str += hi;
			str += lo;
		}

		return;
	}

	str += (WCHAR)ch;
}

bool UnicodeHelper::IsUpper(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsAscii(ch))
		return (ch >= 'A' && ch <= 'Z');

	if (mapping & MF_Accurate)
		return Utf32IsUppercaseLetter(ch);

	return GetCodePointType(ch, CT_CTYPE1) & C1_UPPER;
}

bool UnicodeHelper::IsLower(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsAscii(ch))
		return (ch >= 'a' && ch <= 'z');

	if (mapping & MF_Accurate)
		return Utf32IsLowercaseLetter(ch);

	return GetCodePointType(ch, CT_CTYPE1) & C1_LOWER;
}

bool UnicodeHelper::IsAlpha(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsAscii(ch))
		return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');

	if (mapping & MF_Accurate)
	{
		return Utf32IsLowercaseLetter(ch) || Utf32IsUppercaseLetter(ch) || Utf32IsTitlecaseLetter(ch) ||
		       Utf32IsModifierLetter(ch) || Utf32IsOtherLetter(ch);
	}

	return GetCodePointType(ch, CT_CTYPE1) & C1_ALPHA;
}

bool UnicodeHelper::IsDigit(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsLatin1(ch))
		return (ch >= '0' && ch <= '9');

	if (mapping & MF_Accurate)
		return Utf32IsDecimalDigitNumber(ch);

	return GetCodePointType(ch, CT_CTYPE1) & C1_DIGIT;
}

bool UnicodeHelper::IsNumber(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsLatin1(ch))
		return (ch >= '0' && ch <= '9');

	if (mapping & MF_Accurate)
	{
		return Utf32IsDecimalDigitNumber(ch) || Utf32IsOtherNumber(ch);
	}

	return (GetCodePointType(ch, CT_CTYPE1) & (C1_DIGIT | C1_XDIGIT)) ||
	       (GetCodePointType(ch, CT_CTYPE2) & (C2_EUROPENUMBER | C2_ARABICNUMBER));
}

bool UnicodeHelper::IsAlNum(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	(void)mapping;
	return IsAlpha(ch) || IsDigit(ch);
}

bool UnicodeHelper::IsWhiteSpace(char32_t ch, MappingFlags mapping /*= MF_Accurate*/)
{
	if (!IsValidCodePoint(ch))
		return false;

	if (IsLatin1(ch))
		return ch == ' ' || (ch >= 0x09 && ch <= 0x0d) || ch == 0xa0 || ch == 0x85;

	if (mapping & MF_Accurate)
	{
		return Utf32IsSpaceSeparator(ch) || Utf32IsLineSeparator(ch) || Utf32IsParagraphSeparator(ch);
	}

	return (GetCodePointType(ch, CT_CTYPE1) & C1_SPACE) || (GetCodePointType(ch, CT_CTYPE2) & C2_WHITESPACE);
}

template <bool camel, bool check_locale>
bool makeCamelPascal(const CStringW& src, CStringW& dst, UnicodeHelper::MappingFlags mapping /*= MF_Accurate*/)
{
	try
	{
		enum ch_flags : UINT
		{
			ch_none = 0,
			ch_upper = 0x1,
			ch_lower = 0x2,
			ch_num = 0x4,
			ch_alpha = 0x8,
			ch_caseable = 0x10,
		};

		CStringW outStr;
		bool prev_alnum = false;

		UnicodeHelper::TextElementIterator it(src);

#pragma warning(push)
#pragma warning(disable : 4127)
		while (it.MoveToNextTextElement())
		{
			UINT flags = ch_none;

			char32_t uch = it.CurrentCodePoint(); // to check if upper or lower, base char is good

			if (UnicodeHelper::IsNumber(uch, mapping))
				flags |= ch_num;
			else
			{
				if (UnicodeHelper::IsAlpha(uch))
					flags = ch_alpha;

				if (UnicodeHelper::IsUpper(uch, mapping))
					flags |= ch_upper;
				else if (UnicodeHelper::IsLower(uch, mapping))
					flags |= ch_lower;
				else if ((mapping & UnicodeHelper::MF_Accurate))
				{
					if (camel && Utf32ToLower(uch))
						flags = ch_caseable | ch_upper;
					else if (!camel && Utf32ToUpperOrTitle(uch, true))
						flags = ch_caseable | ch_lower;
				}
			}

			//#ifdef _DEBUG
			//			ch_flags _flags = (ch_flags)flags;
			//#endif // _DEBUG

			CStringW uchStr = it.CurrentTextElement(); // now we need full grapheme

			if (flags & (ch_alpha | ch_num | ch_caseable))
			{
				if (!prev_alnum && (flags & (ch_upper | ch_lower)))
				{
					if (camel && (flags & ch_upper))
						ToLowerHelper<check_locale, true>::map_str(uchStr, uchStr, mapping);
					else if (!camel && (flags & ch_lower))
						ToUpperTitleHelper<check_locale, true>::map_str(uchStr, uchStr, TRUE, mapping);
				}

				prev_alnum = true;
			}
			else
			{
				prev_alnum = false;
			}

			outStr += uchStr;
		}
#pragma warning(pop)

		std::swap(outStr, dst);
		return true;
	}
	catch (const std::exception& ex)
	{
		const char* what = ex.what();

		_ASSERTE(!"Unhandled exception in UnicodeHelper::MakeCamelPascal!");
		vLog("UnicodeHelper: Exception caught in MakeCamelPascal: %s", what);
	}
	catch (...)
	{
		_ASSERTE(!"Unhandled exception in UnicodeHelper::MakeCamelPascal!");
		Log("UnicodeHelper: Exception caught in MakeCamelPascal!");
	}
	return false;
}

bool UnicodeHelper::MakeCamelPascal(BOOL camel, const CStringW& src, CStringW& dst,
                                    MappingFlags mapping /*= MF_Accurate*/)
{
	if (camel)
	{
		if (ToLowerHelper<>::is_special_locale())
			return makeCamelPascal<true, true>(src, dst, mapping);
		else
			return makeCamelPascal<true, false>(src, dst, mapping);
	}
	else
	{
		if (ToUpperTitleHelper<>::is_special_locale())
			return makeCamelPascal<false, true>(src, dst, mapping);
		else
			return makeCamelPascal<false, false>(src, dst, mapping);
	}
}

void UnicodeHelper::MakeUpper(const CStringW& src, CStringW& dst, MappingFlags mapping /*= MF_All*/)
{
	if (ToUpperTitleHelper<>::is_special_locale())
		return ToUpperTitleHelper<true, true>::map_str(src, dst, FALSE, mapping);
	else
		return ToUpperTitleHelper<false, true>::map_str(src, dst, FALSE, mapping);
}

void UnicodeHelper::MakeLower(const CStringW& src, CStringW& dst, MappingFlags mapping /*= MF_Accurate*/)
{
	if (ToLowerHelper<>::is_special_locale())
		return ToLowerHelper<true, true>::map_str(src, dst, mapping);
	else
		return ToLowerHelper<false, true>::map_str(src, dst, mapping);
}

bool UnicodeHelper::PrecomposeString(const CStringW& src, CStringW& dst, BOOL compatibility)
{
	return TransformString(src, dst, compatibility ? TFM_NormalFormKC : TFM_NormalFormC);
}

bool UnicodeHelper::PrecomposeString(CStringW& str, BOOL compatibility)
{
	return PrecomposeString(str, str, compatibility);
}

bool UnicodeHelper::DecomposeString(const CStringW& src, CStringW& dst, BOOL compatibility)
{
	return TransformString(src, dst, compatibility ? TFM_NormalFormKD : TFM_NormalFormD);
}

bool UnicodeHelper::DecomposeString(CStringW& str, BOOL compatibility)
{
	return DecomposeString(str, str, compatibility);
}

bool UnicodeHelper::MapDigitsToASCII(const CStringW& src, CStringW& dst)
{
	return FoldString(src, dst, TFM_None, true);
}

bool UnicodeHelper::MapDigitsToASCII(CStringW& str)
{
	return MapDigitsToASCII(str, str);
}

bool UnicodeHelper::ExpandLigatures(CStringW& str)
{
	return ExpandLigatures(str, str);
}

bool UnicodeHelper::ExpandLigatures(const CStringW& src, CStringW& dst)
{
	return FoldString(src, dst, TFM_ExpandLigatures, false);
}

bool UnicodeHelper::FoldString(const CStringW& src, CStringW& dst, TransformMethod form,
                               BOOL mapDigitsToASCII /*= FALSE*/)
{
	if (std::addressof(src) == std::addressof(dst))
	{
		CStringW rslt;
		if (foldString(dst, dst.GetLength(), rslt, form, mapDigitsToASCII))
		{
			std::swap(rslt, dst);
			return true;
		}
		return false;
	}

	return foldString(src, src.GetLength(), dst, form, mapDigitsToASCII);
}

bool UnicodeHelper::TransformString(CStringW& str, TransformMethod form, BOOL mapDigitsToASCII /*= FALSE*/)
{
	return TransformString(str, str, form, mapDigitsToASCII);
}

class NormalizAPI
{
	using fncPtrNormalizeString = int(_stdcall*)(int NormForm, LPCWSTR lpSrcString, int cwSrcLength, LPWSTR lpDstString,
	                                             int cwDstLength);

	using fncPtrIsNormalizedString = BOOL(_stdcall*)(int NormForm, LPCWSTR lpString, int cwLength);

	Library normalizLib;
	int normalizLibStatus = 0;
	fncPtrNormalizeString normStrFnc = nullptr;
	fncPtrIsNormalizedString isNormStrFnc = nullptr;

	NormalizAPI()
	{
	}

	bool is_valid() const
	{
		return normalizLibStatus > 0;
	}

	bool init()
	{
		if (normalizLibStatus)
			return normalizLibStatus > 0;

		if (!normalizLib.IsLoaded() && !normalizLib.Load("normaliz.dll"))
		{
			normalizLibStatus = -1;
			return false;
		}

		normalizLib.GetFunction("IsNormalizedString", isNormStrFnc);
		if (!isNormStrFnc)
		{
			normalizLibStatus = -2;
			return false;
		}

		normalizLib.GetFunction("NormalizeString", normStrFnc);
		if (!normStrFnc)
		{
			normalizLibStatus = -3;
			return false;
		}

		normalizLibStatus = 1;
		return true;
	}

	static const NormalizAPI& get()
	{
		static NormalizAPI normAPI;
		normAPI.init();
		return normAPI;
	}

  public:
	static bool IsValid()
	{
		return get().is_valid();
	}

	static int NormalizeString(int NormForm, LPCWSTR lpSrcString, int cwSrcLength, LPWSTR lpDstString, int cwDstLength)
	{
		auto fnc = get().normStrFnc;
		if (fnc)
			return fnc(NormForm, lpSrcString, cwSrcLength, lpDstString, cwDstLength);

		return 0;
	}

	static BOOL IsNormalizedString(int NormForm, LPCWSTR lpString, int cwLength)
	{
		auto fnc = get().isNormStrFnc;
		if (fnc)
			return fnc(NormForm, lpString, cwLength);
		;

		return FALSE;
	}
};

bool foldString(LPCWSTR src, int srcLen, CStringW& dst, UnicodeHelper::TransformMethod form,
                BOOL mapDigitsToASCII /*= FALSE*/)
{
	DWORD mapping = 0;

	switch (form)
	{
	case UnicodeHelper::TFM_NormalFormC:
		mapping = MAP_PRECOMPOSED;
		break;
	case UnicodeHelper::TFM_NormalFormD:
		mapping = MAP_COMPOSITE;
		break;
	case UnicodeHelper::TFM_NormalFormKC:
		mapping = MAP_PRECOMPOSED | MAP_FOLDCZONE;
		break;
	case UnicodeHelper::TFM_NormalFormKD:
		mapping = MAP_COMPOSITE | MAP_FOLDCZONE;
		break;
	case UnicodeHelper::TFM_ExpandLigatures:
		mapping = MAP_EXPAND_LIGATURES;
		break;
	default:
		break;
	}

	if (mapDigitsToASCII)
		mapping |= MAP_FOLDDIGITS;

	if (mapping)
	{
		int normLen = ::FoldStringW(mapping, src, srcLen, nullptr, 0);
		if (normLen > 0)
		{
			int actualLen = ::FoldStringW(mapping, src, srcLen, dst.GetBuffer(normLen), normLen);

			if (actualLen <= 0 && GetLastError() != ERROR_SUCCESS)
			{
				dst.ReleaseBuffer();
				return false;
			}

			dst.ReleaseBufferSetLength(actualLen);
			return true;
		}
	}

	return false;
}

bool normalizeString(LPCWSTR src, int srcLen, CStringW& dst, UnicodeHelper::TransformMethod form)
{
	if (!NormalizAPI::IsValid())
		return foldString(src, srcLen, dst, form, FALSE);

	if (NormalizAPI::IsNormalizedString(form, src, srcLen))
	{
		if (GetLastError() != ERROR_SUCCESS)
			return false;

		dst.SetString(src, srcLen);
		return true;
	}

	int trials = 10;
	int normLen = NormalizAPI::NormalizeString(form, src, srcLen, nullptr, 0);

	if (GetLastError() != ERROR_SUCCESS)
		return false;

	while (normLen > 0 && --trials > 0)
	{
		int actualLen = NormalizAPI::NormalizeString(form, src, srcLen, dst.GetBuffer(normLen), normLen);

		if (actualLen <= 0 || GetLastError() != ERROR_SUCCESS)
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				dst.ReleaseBuffer();
				normLen = abs(actualLen);
				continue;
			}
			else
			{
				dst.ReleaseBuffer();
				return false;
			}
		}

		dst.ReleaseBufferSetLength(actualLen);
		return true;
	}

	return false;
}

bool UnicodeHelper::TransformString(const CStringW& src, CStringW& dst, TransformMethod form,
                                    BOOL mapDigitsToASCII /*= FALSE*/)
{
	if (!NormalizAPI::IsValid() || mapDigitsToASCII || form == TFM_ExpandLigatures)
		return FoldString(src, dst, form, mapDigitsToASCII);

	if (std::addressof(src) == std::addressof(dst))
	{
		// if src address equals dst and string is already normalized, just return true
		if (NormalizAPI::IsNormalizedString(form, src, src.GetLength()) && GetLastError() == ERROR_SUCCESS)
			return true;

		// otherwise create a buffer for swap
		CStringW rslt;
		if (normalizeString(dst, dst.GetLength(), rslt, form))
		{
			std::swap(rslt, dst);
			return true;
		}
		return false;
	}

	return normalizeString(src, src.GetLength(), dst, form);
}

UnicodeHelper::TextElementIterator::TextElementIterator(LPCWSTR wstr, int length /*= -1*/)
    : _lpStr(wstr), _length(length)
{
}

// resets the iterator and sets new string to iterate
void UnicodeHelper::TextElementIterator::Reset(LPCWSTR wstr, int length /*= -1*/)
{
	_lpStr = wstr;
	_length = length;
	Reset();
}

// resets the iterator, so you must call one of MoveNext to bring it to the start
void UnicodeHelper::TextElementIterator::Reset()
{
	_index = -1;
	_current = 0;
	_elm_len = -1;
	_elm_decomposed = false;
}

// returns current Utf32 code point
char32_t UnicodeHelper::TextElementIterator::CurrentCodePoint() const
{
	return _current;
}

char32_t UnicodeHelper::TextElementIterator::CurrentTextElementPrecomposed() const
{
	if (CurrentTextElementLength() > CurrentCodePointLength())
	{
		CStringW uchStr = CurrentTextElement();
		UnicodeHelper::PrecomposeString(uchStr, FALSE);
		if (uchStr.GetLength() == 1 ||
		    (uchStr.GetLength() == 2 && UnicodeHelper::IsSurrogatePair(uchStr[0], uchStr[1])))
		{
			return UnicodeHelper::ReadCodePoint(uchStr);
		}
	}
	return CurrentCodePoint();
}

// returns next Utf32 character as a wide string
CStringW UnicodeHelper::TextElementIterator::CurrentCodePointText() const
{
	if (_index < 0)
		return CStringW();

	return CStringW(CurrentPos(), CurrentCodePointLength());
}

// returns next Utf32 character
char32_t UnicodeHelper::TextElementIterator::NextCodePoint() const
{
	return UnicodeHelper::ReadCodePoint(NextPos());
}

// returns the start of iterated string
LPCWSTR UnicodeHelper::TextElementIterator::StartPos() const
{
	return _lpStr;
}

// returns the Utf32 index
int UnicodeHelper::TextElementIterator::CurrentIndex() const
{
	return _index;
}

// returns next Utf32 position on the iterated string
LPCWSTR UnicodeHelper::TextElementIterator::NextPos() const
{
	// if not initialized, return what will be beginning
	if (_index < 0)
		return _lpStr;

	return _lpStr + _index + CurrentCodePointLength();
}

// returns current position within iterated string
LPCWSTR UnicodeHelper::TextElementIterator::CurrentPos() const
{
	if (_index < 0)
		return nullptr;

	return _lpStr + _index;
}

// returns current grapheme (text displayed as a single character)
CStringW UnicodeHelper::TextElementIterator::CurrentTextElement(bool* is_decomposed /*= nullptr*/) const
{
	// invalid state?
	if (_index < 0)
	{
		if (is_decomposed)
			*is_decomposed = false;

		return CStringW();
	}

	return CStringW(CurrentPos(), CurrentTextElementLength(is_decomposed));
}

// returns the length of the current grapheme (text displayed as a single character)
int UnicodeHelper::TextElementIterator::CurrentTextElementLength(bool* is_decomposed /*= nullptr*/) const
{
	// invalid state?
	if (_index < 0 || !_current)
	{
		if (is_decomposed)
			*is_decomposed = false;

		return 0;
	}

	// use cached value, if assigned
	if (_elm_len >= 0)
	{
		if (is_decomposed)
			*is_decomposed = _elm_decomposed;

		return _elm_len;
	}

	// little bit of necessary casting to have the value cached and also have the method const
	*((int*)&_elm_len) = UnicodeHelper::GetTextElementLength(CurrentPos(), (bool*)&_elm_decomposed);

	if (is_decomposed)
		*is_decomposed = _elm_decomposed;

	return _elm_len;
}

// returns count of Utf16 chars defining current Utf32 code point
int UnicodeHelper::TextElementIterator::CurrentCodePointLength() const
{
	return (_index < 0) ? 0 : (_current ? UnicodeHelper::WideCharLength(_current) : 0);
}

// moves the index to the next grapheme (text displayed as a single character)
bool UnicodeHelper::TextElementIterator::MoveToNextTextElement()
{
	if (_length >= 0 && (_index >= _length || _index + CurrentTextElementLength() > _length))
		return false;

	_index = (_index < 0) ? 0 : _index + CurrentTextElementLength();
	_elm_len = -1; // AFTER!!! CurrentTextElementLength call so we can use it if already counted
	_elm_decomposed = false;
	_current = UnicodeHelper::ReadCodePoint(_lpStr + _index);
	return _current != 0;
}

// moves the index to the next utf32 code point
bool UnicodeHelper::TextElementIterator::MoveToNextCodePoint()
{
	if (_length >= 0 && (_index >= _length || _index + CurrentCodePointLength() > _length))
		return false;

	_elm_len = -1;
	_elm_decomposed = false;
	_index = (_index < 0) ? 0 : _index + CurrentCodePointLength();
	_current = UnicodeHelper::ReadCodePoint(_lpStr + _index);
	return _current != 0;
}
