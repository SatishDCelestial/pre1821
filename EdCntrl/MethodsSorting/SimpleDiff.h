#pragma once

#include "WTString.h"
#include "StringUtils.h"
#include "LineReader.h"

template <typename T>
struct Default_Helper
{
	static size_t GetLength(const T& str)
	{
		return (size_t)str.GetLength();
	}

	static T Mid(const T& str, size_t start, size_t length)
	{
		return str.Mid((int)start, (int)length);
	}

	static void Reverse(T& str)
	{
		str.MakeReverse();
	}

	static void Append(T& dst, const T& src, size_t pos)
	{
		dst += src[(int)pos];
	}

	static bool Equals(const T& leftItems, size_t leftIndex, const T& rightItems, size_t rightIndex)
	{
		return leftItems[(int)leftIndex] == rightItems[(int)rightIndex];
	}
};

struct Line_Helper
{
	static size_t GetLength(const std::vector<Line>& lines)
	{
		return lines.size();
	}

	static std::vector<Line> Mid(const std::vector<Line>& lines, size_t start, size_t length)
	{
		return std::vector<Line>(lines.cbegin() + (__int64)start, lines.cbegin() + (__int64)(start + length));
	}

	static void Reverse(std::vector<Line>& lines)
	{
		std::reverse(lines.begin(), lines.end());
	}

	static void Append(std::vector<Line>& dst, const std::vector<Line>& src, size_t pos)
	{
		dst.emplace_back(src[pos]);
	}

	static bool Equals(const std::vector<Line>& leftItems, size_t leftIndex, const std::vector<Line>& rightItems, size_t rightIndex)
	{
		const Line& l = leftItems[leftIndex];
		const Line& r = rightItems[rightIndex];

		if (l.len == r.len)
		{
			if (l.buff.c_str() == r.buff.c_str())
				return l.sp == r.sp;

			return 0 == StrNCmp(l.buff.c_str() + l.sp, r.buff.c_str() + r.sp, r.len);
		}

		return false;
	}
};

template <typename T_Content, typename T_Helper = Default_Helper<T_Content>>
class CSimpleDiff
{
  public:
	class Change
	{
		friend class CSimpleDiff;

		const T_Content* pContent;

		char type;

		size_t start;
		size_t length;

#ifdef _DEBUG
		T_Content dbgContent;
#endif
	  public:
		Change(const T_Content* text, char type)
		    : pContent(text), type(type), start(0), length(0)
		{
		}

		void Add(size_t pos)
		{
			if (IsEmpty())
			{
#ifdef _DEBUG
				T_Helper::Append(dbgContent, *pContent, pos);
#endif
				start = pos;
				length = 1;
				return;
			}

			_ASSERT(start + length == pos);

#ifdef _DEBUG
			T_Helper::Append(dbgContent, *pContent, pos);
#endif

			length++;
		}

		void Clear()
		{
			start = 0;
			length = 0;
#ifdef _DEBUG
			dbgContent = T_Content();
#endif
		}

		bool IsInsert() const
		{
			return type == '+';
		}

		bool IsRemove() const
		{
			return type == '-';
		}

		size_t Start() const
		{
			return start;
		}

		T_Content GetContent() const
		{
			return T_Helper::Mid(*pContent, start, length);
		}

		size_t GetLength() const
		{
			return length;
		}

		bool IsEmpty() const
		{
			return length == 0;
		}
	};

  private:
	void fill_LCS(std::vector<std::vector<size_t>>& mtx, const T_Content& s1, const T_Content& s2, size_t m, size_t n)
	{
		for (size_t i = 0; i < m; ++i)
		{
			for (size_t j = 0; j < n; ++j)
			{
				if (T_Helper::Equals(s2, j, s1, i))
				{
					// common char found, add 1 to highest lcs found so far
					mtx[i + 1][j + 1] = mtx[i][j] + 1;
				}
				else
				{
					// not a match, add highest lcs length found so far
					mtx[i + 1][j + 1] = std::max<size_t>(mtx[i][j + 1], mtx[i + 1][j]);
				}
			}
		}
	}

	void backtrack_LCS(const std::vector<std::vector<size_t>>& mtx, const T_Content& src, size_t m, size_t n)
	{
		size_t i = m, j = n;

		while (i >= 1 && j >= 1)
		{
			if (mtx[i][j] == mtx[i][j - 1])
			{
				--j;
			}
			else if (mtx[i][j] == mtx[i - 1][j])
			{
				--i;
			}
			else
			{
				--i;
				--j;
				T_Helper::Append(m_lcs, src, i);
			}
		}

		T_Helper::Reverse(m_lcs);
	}

	void init_LCS()
	{
		size_t m = T_Helper::GetLength(*m_source);
		size_t n = T_Helper::GetLength(*m_target);

		// matrix with highest common sequence lengths
		std::vector<std::vector<size_t>> mtx(m + 1, std::vector<size_t>(n + 1, 0));

		// fill the matrix with highest common sequence lengths
		fill_LCS(mtx, *m_source, *m_target, m, n);

		// backtrack the longest common sequence
		backtrack_LCS(mtx, *m_source, m, n);
	}

	void fill_changes(std::vector<Change>& changes, const T_Content& str, char type)
	{
		size_t m = T_Helper::GetLength(str);
		size_t n = T_Helper::GetLength(m_lcs);

		if (m < n)
		{
			return;
		}

		size_t i = 0, j = 0;
		Change change(&str, type);

		while (i < m && j < n)
		{
			if (T_Helper::Equals(str, i, m_lcs, j))
			{
				if (!change.IsEmpty())
				{
					changes.emplace_back(change);
					change.Clear();
				}
				i++;
				j++;
			}
			else
			{
				change.Add(i);
				i++;
			}
		}

		while (i < m)
		{
			change.Add(i);
			i++;
		}

		if (!change.IsEmpty())
		{
			changes.emplace_back(change);
			change.Clear();
		}
	}

	T_Content m_source_copy;
	T_Content m_target_copy;

  public:
	const T_Content* m_source; // input 1 - from string
	const T_Content* m_target; // input 2 - to string

	T_Content m_lcs; // common sequence

	std::vector<Change> m_removes; // changes to be done to transform m_source to m_target
	std::vector<Change> m_inserts; // changes to be done to transform m_source to m_target

	// Calculates Levenshtein distance from the Longest Common Subsequence(LCS)
	// Levenshtein distance = len(str1) + len(str2) - 2 * LCS
	// This represents the minimum number of insertions, deletions, or substitutions needed
	// to transform one string into the other.
	size_t EditDistance() const
	{
		size_t srcLen = T_Helper::GetLength(*m_source);
		size_t tgtLen = T_Helper::GetLength(*m_target);
		size_t lcsLen = T_Helper::GetLength(m_lcs);

		return srcLen + tgtLen - 2 * lcsLen;
	}

	CSimpleDiff(CSimpleDiff&&) = delete;
	CSimpleDiff& operator=(CSimpleDiff&&) = delete;

	CSimpleDiff(const T_Content& source, const T_Content& target, bool make_local_copy = true)
	{
		try
		{
			if (!make_local_copy)
			{
				m_source = &source;
				m_target = &target;
			}
			else
			{
				m_source_copy = source;
				m_target_copy = target;
				m_source = &m_source_copy;
				m_target = &m_target_copy;
			}

			init_LCS();

			fill_changes(m_removes, *m_source, '-');
			fill_changes(m_inserts, *m_target, '+');

			std::reverse(m_removes.begin(), m_removes.end());
		}
		catch (...)
		{
			_ASSERT(!"Exception thrown in CSimpleDiff's ctor!");
		}
	}

	void Apply(std::function<void(const Change&)> func)
	{
		try
		{
			for (const auto& change : m_removes)
			{
				func(change);
			}

			for (const auto& change : m_inserts)
			{
				func(change);
			}
		}
		catch (...)
		{
			_ASSERT(!"Exception thrown in CSimpleDiff::Apply!");
		}
	}
};

using CDiff = CSimpleDiff<CStringW>;
using CDiffWT = CSimpleDiff<WTString>;
using CLineDiffWT = CSimpleDiff<std::vector<Line>, Line_Helper>;
