#include "stdafxed.h"
#include "ParenSkipper.h"
#include "wtcsym.h"
#include "StringUtils.h"
#include "CommentSkipper.h"

bool ParenSkipper::IsInside(const WTString& buff, int i)
{
	TCHAR c = buff[i];

	if (c == '<')
	{
		CommentSkipper cs(fType);
		int angle2 = 1;
		for (int j = i + 1; j < buff.GetLength(); j++)
		{ // finding matching '>'
			TCHAR c2 = buff[j];
			if (cs.IsCode(c2))
			{
				if (c2 == '<')
				{
					angle2++;
					continue;
				}
				if (c2 == '>')
				{
					angle2--;
					if (angle2 == 0)
					{
						for (int k = j + 1; k < buff.GetLength(); k++)
						{
							TCHAR c3 = buff[k];
							if (c3 == '(')
							{ // we found a template function call / template object declaration: '(' after '>', so
							  // increasing the angle brackets count is valid
								angleBrackets++;
								break;
							}
							if (!IsWSorContinuation(c3) && !ISCSYM(c3))
								break;
						}
					}
				}
				if (c2 == ';' || c2 == '{' || c2 == '}') // checking invalid characters in supposed template argument to
				                                         // speed up the process by aborting it
					break;
			}
		}
	}
	else if (c == '>' && previousChar != '-')
	{ // skip ->
		angleBrackets--;
	}
	else if (c == '(')
	{
		parens++;
	}
	else if (c == ')')
	{
		parens--;
	}
	else if (c == '[')
	{
		squareBrackets++;
	}
	else if (c == ']')
	{
		squareBrackets--;
	}
	else if (c == '{')
	{
		braces++;
	}
	else if (c == '}')
	{
		braces--;
	}

	previousChar = c;

	return angleBrackets > 0 || (checkInsideParens && parens > 0) || squareBrackets > 0 || braces > 0;
}
