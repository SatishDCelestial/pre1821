#pragma once

#include "FOO.H"

class DType;
class MultiParse;
class WTString;

class LocalRefactoring
{
  public:
	LocalRefactoring()
	{
	}
	~LocalRefactoring()
	{
	}

	static BOOL IsCPPCSFileAndInsideFunc();
	static BOOL IsCPPFileAndInsideFunc();
	static int FindOpeningBrace(int line, WTString& fileBuf, int& curPos);
	static bool IsStatementEndChar(char end)
	{
		return end == ' ' || end == '(' || end == '\t' || end == '\r' || end == '\n' || end == '/';
	}
};

extern WTString GetCleanScope(WTString scope);
extern WTString GetReducedScope(const WTString& scope);
extern DTypePtr GetMethod(MultiParse* mp, WTString name, WTString scope, WTString baseClassList,
                          WTString* methodScope = nullptr, int ln = -1);
extern WTString GetQualification(const WTString& type);
extern bool IsSubStringPlusNonSymChar(const WTString& buf, const WTString& subString, int pos);
extern WTString GetFileNameTruncated(CStringW path);
