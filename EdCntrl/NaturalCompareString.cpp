#include "stdafxed.h"

#include "StringUtils.h"
#include <afxstr.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

template <typename m_string_t> class NaturalComparer
{
	template <typename T> struct is_wide
	{
		static const bool value = true;
	};

	template <> struct is_wide<char>
	{
		static const bool value = false;
	};

	typedef typename m_string_t::XCHAR m_char_t;

	enum class CharType
	{
		none,
		whitespace,
		other,
		number,
		letter,
	};

	CharType m_next_cat;
	int m_next_index;
	const m_string_t& m_str;

	m_string_t m_current_text;
	CharType m_current_cat;
	bool m_ignore_spaces;
	bool m_non_alphanum_per_char;

	NaturalComparer(const m_string_t& str, bool ignore_spaces, bool non_alphanum_per_char)
	    : m_next_cat(str.IsEmpty() ? CharType::none : GetCharType(str[0])), m_next_index(0), m_str(str),
	      m_current_cat(CharType::none), m_ignore_spaces(ignore_spaces), m_non_alphanum_per_char(non_alphanum_per_char)
	{
	}

	CharType GetCharType(m_char_t ch)
	{
		if (is_wide<m_char_t>::value)
		{
			if (iswalpha((wchar_t)ch))
				return CharType::letter;
			if (ch >= '0' && ch <= '9')
				return CharType::number;
			if (iswspace((wchar_t)ch))
				return CharType::whitespace;
		}
		else
		{
			if (isalpha((char)ch))
				return CharType::letter;
			if (ch >= '0' && ch <= '9')
				return CharType::number;
			if (isspace((char)ch))
				return CharType::whitespace;
		}

		return CharType::other;
	}

	bool NextToken()
	{
		if (m_next_index >= m_str.GetLength())
		{
			m_current_text = "";
			m_current_cat = CharType::none;
			return false;
		}

		m_current_cat = m_next_cat;
		m_current_text = "";

		while (m_next_index < m_str.GetLength())
		{
			m_char_t ch = m_str[m_next_index];
			CharType ch_cat = GetCharType(ch);

			if (m_ignore_spaces && ch_cat == CharType::whitespace)
			{
				m_next_index++;
				continue;
			}

			if (ch_cat != m_current_cat)
			{
				m_next_cat = ch_cat;
				break;
			}

			m_current_text += ch;
			m_next_index++;

			// if other types per char
			if (m_non_alphanum_per_char && ch_cat == CharType::other)
			{
				if (m_next_index < m_str.GetLength())
					m_next_cat = GetCharType(m_str[m_next_index]);
				else
					m_next_cat = CharType::none;
				break;
			}
		}

		return true;
	}

	const m_string_t& GetText() const
	{
		return m_current_text;
	}

	CharType GetCurrType() const
	{
		return m_current_cat;
	}

	template <typename TVal> static int compare(const TVal& lhs, const TVal& rhs)
	{
		return lhs == rhs ? 0 : (lhs > rhs ? 1 : -1);
	}

	static int compare_str(const m_string_t& lhs, const m_string_t& rhs, bool ignore_case)
	{
		int rslt = ignore_case ? lhs.Compare(rhs) : lhs.CompareNoCase(rhs);
		return rslt == 0 ? 0 : (rslt > 0 ? 1 : -1);
	}

	static int compare_num_str(const m_string_t& aStr, const m_string_t& bStr)
	{
		int aNumDigits = aStr.GetLength();
		int bNumDigits = bStr.GetLength();

		if (aNumDigits == 0 && bNumDigits == 0)
			return 0;

		if (aNumDigits == 0 || bNumDigits == 0)
			return compare<int>(aNumDigits, bNumDigits);

		// find first non-zero digit in aStr
		const m_char_t* aNumStr = aStr;
		while (aNumDigits > 0 && *aNumStr == '0')
		{
			aNumStr++;
			aNumDigits--;
		}

		// find first non-zero digit in bStr
		const m_char_t* bNumStr = bStr;
		while (bNumDigits > 0 && *bNumStr == '0')
		{
			bNumStr++;
			bNumDigits--;
		}

		// if one number has more digits than another, it is bigger
		if (aNumDigits != bNumDigits)
			return compare<int>(aNumDigits, bNumDigits);

		// if lengths are zero, numbers are zero
		if (aNumDigits == 0)
			return 0;

		// if the length of number strings is equal, we can compare as strings
		int rslt = m_string_t::StrTraits::StringCompare(aNumStr, bNumStr);
		return rslt == 0 ? 0 : (rslt > 0 ? 1 : -1);
	}

  public:
	static int Compare(const m_string_t& a, const m_string_t& b, bool ignore_spaces = false,
	                   bool non_alnum_by_char = false)
	{
		try
		{
			if (a.IsEmpty() && b.IsEmpty())
				return 0;

			if (a.IsEmpty() || b.IsEmpty())
				return compare<int>(a.GetLength(), b.GetLength());

			NaturalComparer aCmp(a, ignore_spaces, non_alnum_by_char);
			NaturalComparer bCmp(b, ignore_spaces, non_alnum_by_char);

			int result = 0;

			do
			{
				bool a_have_token = aCmp.NextToken();
				bool b_have_token = bCmp.NextToken();

				if (!a_have_token && !b_have_token)
					return 0;

				result = compare<bool>(a_have_token, b_have_token);
				if (result)
					return result;

				if (aCmp.GetCurrType() != bCmp.GetCurrType())
				{
					result = compare_str(aCmp.GetText(), bCmp.GetText(), false);
					if (result)
						return result;
				}

				switch (aCmp.GetCurrType())
				{
				case CharType::letter: {
					result = compare_str(aCmp.GetText(), bCmp.GetText(), true);
					if (result == 0)
						result = compare_str(aCmp.GetText(), bCmp.GetText(), false);
					break;
				}
				case CharType::number: {
					result = compare_num_str(aCmp.GetText(), bCmp.GetText());
					break;
				}
				default:
					result = compare_str(aCmp.GetText(), bCmp.GetText(), false);
					break;
				}
			} while (result == 0);

			return result;
		}
		catch (...)
		{
			_ASSERTE(!"Exception in NaturalComparer::Compare");
		}

		return compare_str(a, b, false);
	}
};

int NaturalCompare(const CStringA& lhs, const CStringA& rhs, bool ignore_spaces /*= false*/,
                   bool non_alnum_by_char /*= true*/)
{
	return NaturalComparer<CStringA>::Compare(lhs, rhs, ignore_spaces, non_alnum_by_char);
}

int NaturalCompare(const CStringW& lhs, const CStringW& rhs, bool ignore_spaces /*= false*/,
                   bool non_alnum_by_char /*= true*/)
{
	return NaturalComparer<CStringW>::Compare(lhs, rhs, ignore_spaces, non_alnum_by_char);
}
