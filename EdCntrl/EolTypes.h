#pragma once

class WTString;

namespace EolTypes
{
typedef enum EolType
{
	eolCrLf,
	eolCr,
	eolLf,
	eolNone,
	eolMax
} EolType;

EolType GetEolType(const CString& txt);
EolType GetEolType(const WTString& txt);
EolType GetEolType(const CStringW& txt);

LPCSTR GetEolStr(EolType eolType);
inline LPCSTR GetEolStr(const CString& txt)
{
	return GetEolStr(GetEolType(txt));
}
inline LPCSTR GetEolStr(const WTString& txt)
{
	return GetEolStr(GetEolType(txt));
}
LPCWSTR GetEolStrW(EolType eolType);
inline LPCWSTR GetEolStrW(const CStringW& txt)
{
	return GetEolStrW(GetEolType(txt));
}
} // namespace EolTypes
