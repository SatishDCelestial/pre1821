#pragma once
#include "WTString.h"

struct Line
{
	long sp = 0;
	long len = 0;
	WTString buff;
	WTString GetText() const
	{
		return buff.Mid((size_t)sp, (size_t)len);
	}
};

class LineReader
{
	long startPos;
	long endPos;

  public:
	LineReader(int startPos = 0)
	    : startPos(startPos), endPos(startPos)
	{
	}

	long EndPos() const
	{
		return endPos;
	}

	long StartPos() const
	{
		return startPos;
	}

	long Length() const
	{
		return endPos - startPos;
	}

	template <typename T_CHAR>
	static long CountLines(const T_CHAR* buf, long start = 0, long end = -1)
	{
		long count = 0;

		if (end >= 0)
		{
			const T_CHAR* pEnd = buf + (ULONG)end;

			for (const T_CHAR* p = buf + start; p && p < pEnd && *p; p++)
				if (*p == '\n')
					count++;
		}
		else
		{
			for (const T_CHAR* p = buf + start; p && *p; p++)
				if (*p == '\n')
					count++;
		}

		return count;
	}

	template <typename T_CHAR>
	static long GetLineLength(const T_CHAR* buf, LONG lineStart, bool includeLineBreak = false)
	{
		long len = 0;
		for (const T_CHAR* p = buf + lineStart; p && *p; p++, len++)
		{
			if (p[0] == '\n')
			{
				return includeLineBreak ? len + 1 : len;
			}

			if (p[0] == '\r' && p[1] != '\n')
			{
				return includeLineBreak ? len + 2 : len;
			}
		}
		return len;
	}

	bool ReadLine(const WTString& src, WTString& line)
	{
		long lineLen = GetLineLength(src.c_str(), endPos, true);
		if (lineLen)
		{
			line = src.Mid((size_t)endPos, (size_t)lineLen);
			startPos = endPos;
			endPos += lineLen;
			return true;
		}
		return false;
	}

	bool ReadLine(const WTString& src, Line& line)
	{
		long lineLen = GetLineLength(src.c_str(), endPos, true);
		if (lineLen)
		{
			line.buff = src;
			line.sp = endPos;
			line.len = lineLen;

			startPos = endPos;
			endPos += lineLen;
			return true;
		}
		return false;
	}

	bool ReadLine(const CStringW& src, CStringW& line)
	{
		long lineLen = GetLineLength((LPCWSTR)src, endPos, true);
		if (lineLen)
		{
			line = src.Mid(endPos, lineLen);
			startPos = endPos;
			endPos += lineLen;
			return true;
		}
		return false;
	}

	template <typename TString>
	static void ReadLines(const TString& str, std::vector<TString>& lines)
	{
		TString line;
		LineReader reader;
		while (reader.ReadLine(str, line))
			lines.emplace_back(line);
	}

	static void ReadLines(const WTString& str, std::vector<Line>& lines)
	{
		Line line;
		LineReader reader;
		while (reader.ReadLine(str, line))
		{
			lines.emplace_back(line);
		}
	}
};