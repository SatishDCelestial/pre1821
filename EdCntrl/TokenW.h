#pragma once

#include "WtString.h"

class TokenW : public CStringW
{
	std::wstring_view m_ptr;
	CStringW ifs;
	CStringW mCharsSkippedOnRead;

	template <typename CH, typename TRAITS>
	static inline std::basic_string_view<CH> to_string_view(const ATL::CStringT<CH, TRAITS>& str)
	{
		return {str.GetString(), (uint)str.GetLength()};
	}

  public:
	TokenW()
	    : CStringW(), m_ptr(to_string_view(*this)), ifs(DB_SEP_STR.Wide())
	{
	}
	TokenW(LPCWSTR txt)
	    : CStringW(txt), m_ptr(to_string_view(*this)), ifs(DB_SEP_STR.Wide())
	{
	}
	TokenW(LPCWSTR txt, int n)
	    : CStringW(txt, n), m_ptr(to_string_view(*this)), ifs(DB_SEP_STR.Wide())
	{
	}
	TokenW(const CStringW& str)
	    : CStringW(str), m_ptr(to_string_view(*this)), ifs(DB_SEP_STR.Wide())
	{
	}

	int Replace(LPCWSTR s1, LPCWSTR s2)
	{
		int r = CStringW::Replace(s1, s2);
		m_ptr = to_string_view(*this);
		return r;
	}

	CStringW Str()
	{
		ValidatePtr();
		return CStringW(m_ptr.data(), (int)m_ptr.length());
	}

	int more()
	{
		ValidatePtr();
		return (int)m_ptr.size();
	}

	int GetLength()
	{
		ValidatePtr();
		return (int)m_ptr.size();
	}

	void ValidatePtr()
	{
		LPCWSTR p = *this;
		if (m_ptr.data() < p || m_ptr.data() > p + CStringW::GetLength())
			m_ptr = to_string_view(*this);
	}

	CStringW read(LPCWSTR ifsx = NULL)
	{
		ValidatePtr();
		if (ifsx)
			ifs = ifsx;

		// eat beginning spaces
		mCharsSkippedOnRead.Empty();
		for (; !m_ptr.empty() && wcschr(ifs, m_ptr[0]); m_ptr = m_ptr.substr(1))
			mCharsSkippedOnRead += m_ptr[0];

		// find next
		int n = (int)wcscspn(m_ptr.data(), ifs);
		if (n > 0)
		{
			CStringW rstr(m_ptr.data(), n);
			m_ptr = m_ptr.substr((uint32_t)n);
			return rstr;
		}

		return {};
	}
	std::wstring_view read_sv(LPCWSTR ifsx = NULL)
	{
		ValidatePtr();
		if (ifsx)
			ifs = ifsx;

		// eat beginning spaces
		mCharsSkippedOnRead.Empty();
		for (; !m_ptr.empty() && wcschr(ifs, m_ptr[0]); m_ptr = m_ptr.substr(1))
			mCharsSkippedOnRead += m_ptr[0];

		// find next
		size_t n = wcscspn(m_ptr.data(), ifs);
		if (n > 0)
		{
			std::wstring_view ret = m_ptr.substr(0, n);
			m_ptr = m_ptr.substr(n);
			return ret;
		}

		return {};
	}

	CStringW GetCharsSkipped() const
	{
		return mCharsSkippedOnRead;
	}
};

// this is a new class, based on TokenW
// created for FilterableSet::FilterFiles #FilterFiles
template <typename CH>
class TokenT : public std::basic_string<CH>
{
	std::basic_string_view<CH> m_ptr;
	std::basic_string<CH> ifs;
	std::basic_string<CH> mCharsSkippedOnRead;

	// Helper to convert to string view
	template <typename STR>
	static inline std::basic_string_view<CH> to_string_view(const STR& str)
	{
		return {str.data(), str.size()};
	}

  public:
	TokenT()
	    : ifs(1, DB_SEP_CHR) // Initialize ifs with DB_SEP_CHR
	{
		m_ptr = to_string_view(*this);
	}

	TokenT(const CH* txt)
	    : std::basic_string<CH>(txt), ifs(1, DB_SEP_CHR)
	{
		m_ptr = to_string_view(*this);
	}

    TokenT(const std::basic_string<CH>& str, const std::basic_string<CH>& separators)
	    : std::basic_string<CH>(str), ifs(separators)
	{
		m_ptr = to_string_view(*this);
	}


	TokenT(const CH* txt, int n)
	    : std::basic_string<CH>(txt, n), ifs(1, DB_SEP_CHR)
	{
		m_ptr = to_string_view(*this);
	}

	TokenT(const std::basic_string<CH>& str)
	    : std::basic_string<CH>(str), ifs(1, DB_SEP_CHR)
	{
		m_ptr = to_string_view(*this);
	}

	int GetLength()
	{
		ValidatePtr();
		return (int)m_ptr.size();
	}

	void ValidatePtr()
	{
		const CH* p = this->data();
		if (m_ptr.data() < p || m_ptr.data() > p + this->size())
			m_ptr = to_string_view(*this);
	}

	int more()
	{
		ValidatePtr();
		return !m_ptr.empty();
	}

	std::basic_string<CH> read(const CH* ifsx = nullptr)
	{
		ValidatePtr();
		if (ifsx)
			ifs = std::basic_string<CH>(1, *ifsx);

		// Eat leading separators or specified characters
		mCharsSkippedOnRead.clear();
		while (!m_ptr.empty() && m_ptr[0] == ifs[0])
		{
			mCharsSkippedOnRead += m_ptr[0];
			m_ptr.remove_prefix(1);
		}

		if (!m_ptr.empty()) // Check if there's still content after removing prefixes
		{
			size_t n = m_ptr.find(ifs[0]);
			if (n != std::basic_string_view<CH>::npos)
			{
				std::basic_string<CH> rstr(m_ptr.data(), n);
				m_ptr.remove_prefix(n + 1); // +1 to skip the separator and move to the next character
				return rstr;
			}
			else
			{
				// If no separator is found, return the rest of the string
				std::basic_string<CH> rstr(m_ptr.begin(), m_ptr.end());
				m_ptr.remove_prefix(m_ptr.size()); // Move m_ptr to the end
				return rstr;
			}
		}

		return {}; // Return an empty string if nothing left to parse
	}

	std::basic_string<CH> GetCharsSkipped() const
	{
		return mCharsSkippedOnRead;
	}
};
