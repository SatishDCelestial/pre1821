#include "StdAfxEd.h"
#include "EolTypes.h"
#include "WTString.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace EolTypes;

LPCTSTR
EolTypes::GetEolStr(EolType eolType)
{
	const LPCTSTR kEolStrs[eolMax] = {
	    "\r\n", // eolCrLf
	    "\r",   // eolCr
	    "\n",   // eolLf
	    "\r\n"  // eolNone
	};

	_ASSERTE(eolType < eolMax);
	return kEolStrs[eolType];
}

LPCWSTR
EolTypes::GetEolStrW(EolType eolType)
{
	const LPCWSTR kEolStrsW[eolMax] = {
	    L"\r\n", // eolCrLf
	    L"\r",   // eolCr
	    L"\n",   // eolLf
	    L"\r\n"  // eolNone
	};

	_ASSERTE(eolType < eolMax);
	return kEolStrsW[eolType];
}

// NOTE: Keep implementation in sync with EolTypes::GetEolType(const CString & txt)
EolType EolTypes::GetEolType(const CStringW& txt)
{
	// MIRROR modifications in EolTypes::GetEolType(const CString & txt)
	int kLen = txt.GetLength();
	if (!kLen)
		return eolNone;

	int pos = txt.FindOneOf(L"\r\n");
	if (-1 == pos)
		return eolNone;

	const WCHAR first = txt[pos];
	if (pos + 1 < kLen)
	{
		const WCHAR sec = txt[pos + 1];
		if (sec != first && (sec == L'\r' || sec == L'\n'))
		{
			_ASSERTE(first == '\r' && sec == '\n');
			return eolCrLf;
		}
	}

	switch (first)
	{
	case L'\r':
		return eolCr;
	case L'\n':
		return eolLf;
	default:
		return eolNone;
	}
}

// NOTE: Keep implementation in sync with EolTypes::GetEolType(const CStringW & txt)
// Duplicated due to performance of ASCII -> UNICODE conversion in large files
EolType EolTypes::GetEolType(const CString& txt)
{
	// MIRROR modifications in EolTypes::GetEolType(const CStringW & txt)
	int kLen = txt.GetLength();
	if (!kLen)
		return eolNone;

	int pos = txt.FindOneOf("\r\n");
	if (-1 == pos)
		return eolNone;

	const char first = txt[pos];
	if (pos + 1 < kLen)
	{
		const char sec = txt[pos + 1];
		if (sec != first && (sec == '\r' || sec == '\n'))
		{
			_ASSERTE(first == '\r' && sec == '\n');
			return eolCrLf;
		}
	}

	switch (first)
	{
	case '\r':
		return eolCr;
	case '\n':
		return eolLf;
	default:
		return eolNone;
	}
}

// NOTE: Keep implementation in sync with EolTypes::GetEolType(const CStringW & txt)
// Duplicated due to performance of ASCII -> UNICODE conversion in large files
EolType EolTypes::GetEolType(const WTString& txt)
{
	// MIRROR modifications in EolTypes::GetEolType(const CStringW & txt)
	int kLen = txt.GetLength();
	if (!kLen)
		return eolNone;

	int pos = txt.FindOneOf("\r\n");
	if (-1 == pos)
		return eolNone;

	const char first = txt[pos];
	if (pos + 1 < kLen)
	{
		const char sec = txt[pos + 1];
		if (sec != first && (sec == '\r' || sec == '\n'))
		{
			_ASSERTE(first == '\r' && sec == '\n');
			return eolCrLf;
		}
	}

	switch (first)
	{
	case '\r':
		return eolCr;
	case '\n':
		return eolLf;
	default:
		return eolNone;
	}
}
