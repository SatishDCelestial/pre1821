#pragma once

#include <wtypes.h>

class WTString;

// skipping inside (), <>, {}, []
// the goal of the class is to provide a general solution for avoiding false positives
// such as skipping inside <> with Method<>() type of things while not skipping "b && c" in "a < b && c > b", etc.
class ParenSkipper
{
  public:
	ParenSkipper() = delete;
	ParenSkipper(int ftype) : fType(ftype)
	{
	}

	bool IsInside(const WTString& buff, int i);

	int parens = 0;
	int braces = 0;
	int squareBrackets = 0;
	int angleBrackets = 0;
	TCHAR previousChar = 0;

	bool checkInsideParens = true;

	int fType;
};
