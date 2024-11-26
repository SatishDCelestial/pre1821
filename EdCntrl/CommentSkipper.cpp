#include "stdafxed.h"
#include "CommentSkipper.h"
#include "WTString.h"
#include "FileTypes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma warning(disable : 4062)

// param "backwards" doesn't support skipping preprocessor and single line // comments - use IsCodeBackward() for that
bool CommentSkipper::IsCode(TCHAR c, bool backwards /*= false*/)
{
	if (!Is_C_CS_File(FileType))
		return true;

	if (SkipNext != NO)
	{
		switch (SkipNext)
		{
		case YES:
			SkipNext = ONLY_NEWLINE;
			return false;
		case ONLY_NEWLINE:
			SkipNext = NO;
			if (c == '\r' || c == '\n')
				return false;
			break;
		case SKIP_IN_STRING:
			SkipNext = NO;
			return false;
			break;
		}
	}
	if (c == '\\' && IsCFile(FileType))
	{ // there is no line continuity character in C#
		if (State == IN_STRING)
			SkipNext = SKIP_IN_STRING; // [case: 97015]
		else
			SkipNext = YES;
		return false;
	}
	else
	{
	again:
		switch (State)
		{
		case COMMENT_MAY_START:
			if (c == '/')
			{
				if (!backwards)
					State = IN_COMMENT;
			}
			else if (c == '*')
			{
				State = IN_MULTILINE_COMMENT;
			}
			else
			{
				State = IN_CODE;
				goto again;
			}
			break;
		case COMMENT_WILL_END:
		case RAW_WILL_END:
			State = IN_CODE;
		case IN_CODE:
			if (c == '/')
				State = COMMENT_MAY_START;
			else if ((IsCFile(FileType) && c == 'R') || (FileType == CS && c == '@'))
				State = RAW_MAY_START1;
			else if (c == '#')
			{
				if (!backwards)
					State = IN_PREPROC;
			}
			else
			{
				if (SkippingStrings)
				{
					if (c == '\"')
						State = IN_STRING;
					else if (c == '\'')
						State = IN_CHAR;
				}
			}
			break;
		case RAW_MAY_START1:
			if (c == '\"')
			{
				if (IsCFile(FileType))
					State = RAW_MAY_START2;
				else
					State = IN_RAW_STRING;
			}
			else
				State = IN_CODE;
			break;
		case RAW_MAY_START2:
			if (c == '(')
				State = IN_RAW_STRING;
			else
				State = IN_CODE;
			break;
		case IN_RAW_STRING:
			if (IsCFile(FileType))
			{
				if (c == ')')
					State = RAW_MAY_END;
			}
			else
			{
				if (c == '"')
					State = RAW_MAY_END;
			}
			break;
		case RAW_MAY_END:
			if (IsCFile(FileType))
			{
				if (c == '\"')
					State = RAW_WILL_END;
				else
					State = IN_RAW_STRING;
			}
			else
			{
				if (c != '\"')
					State = IN_CODE;
				else
					State = IN_RAW_STRING;
			}
			break;
		case IN_CHAR:
			if (c == '\'')
				State = IN_CODE;
			break;
		case COMMENT_MAY_END:
			if (c == '/')
			{
				State = COMMENT_WILL_END;
				break;
			}
			else
			{
				State = IN_COMMENT;
			}
		case IN_MULTILINE_COMMENT:
			if (c == '*')
			{
				State = COMMENT_MAY_END;
			}
			break;
		case IN_COMMENT:
		case IN_PREPROC:
			if (c == '\r' || c == '\n')
				State = IN_CODE;
			break;
		case IN_STRING:
			if (c == '\"')
				State = IN_CODE;
			break;
		}
	}

	return State == IN_CODE || State == COMMENT_MAY_START || State == RAW_MAY_START1 || State == RAW_MAY_START2;
}

// it already returns false on the first character of a comment
bool CommentSkipper::IsCode2(TCHAR c, TCHAR nc)
{
	bool res = IsCode(c, false);
	if (c == '/')
	{
		if (nc == '/' || nc == '*')
			return false;
	}

	return res;
}

bool CommentSkipper::IsComment(TCHAR c)
{
	bool code = IsCode(c);
	if (code)
		return false;

	return State == IN_COMMENT || State == IN_MULTILINE_COMMENT || State == COMMENT_MAY_START ||
	       State == COMMENT_MAY_END;
}

bool CommentSkipper::IsCodeBackward(const WTString& buf, int i)
{
	if (!Is_C_CS_File(FileType))
		return true;

	TCHAR c = buf[i];

	if (c == '\r' || c == '\n')
	{
		if ((i + 1 < buf.GetLength() && (buf[i + 1] == '\r' || buf[i + 1] == '\n')) || (i > 0 && buf[i - 1] != '\r'))
		{
			// new line
			int comPos = INT_MAX; // comment position
			int prePos = INT_MAX; // preprocessor position
			for (int j = i - 2; j >= 0; j--)
			{
				if (buf[j] == '/' && buf[j + 1] == '/')
				{
					// State = IN_COMMENT;
					comPos = j;
					break;
				}
				if (buf[j] == '#')
				{
					// State = IN_PREPROC;
					prePos = j;
				}
				if (buf[j] == '\r' || buf[j] == '\n')
				{
					if (IsCFile(FileType) && (j >= 2 && (buf[j - 1] == '\\' || buf[j - 2] == '\\')))
						continue; // line continuity character, so not stopping at this line break
					else
						break; // line brake, stopping
				}
			}
			if (prePos < comPos)
			{
				State = IN_PREPROC;
				LastCommentPos = prePos;
			}
			else
			{
				if (comPos != INT_MAX)
				{
					State = IN_COMMENT;
					LastCommentPos = comPos;
				}
			}
		}
	}

	if (LastCommentPos >= i)
	{
		if (LastCommentPos == i && (State == IN_COMMENT || State == IN_PREPROC))
			State = IN_CODE;
	}

	if (SkipNext != NO)
	{
		switch (SkipNext)
		{
		case YES:
			SkipNext = ONLY_NEWLINE;
			return false;
		case ONLY_NEWLINE:
			SkipNext = NO;
			if (c == '\r' || c == '\n')
				return false;
		}
	}
	if (c == '\\')
	{
		SkipNext = YES;
		return false;
	}
	else
	{
	again:
		switch (State)
		{
		case COMMENT_MAY_START:
			if (c == '/')
			{
				State = IN_COMMENT;
			}
			else if (c == '*')
			{
				State = IN_MULTILINE_COMMENT;
			}
			else
			{
				State = IN_CODE;
				goto again;
			}
			break;
		case COMMENT_WILL_END:
			State = IN_CODE;
		case IN_CODE:
			if (c == '/')
				State = COMMENT_MAY_START;
			else if (c == '#')
			{
				// if (!backwards)
				//	State = IN_PREPROC;
			}
			else
			{
				if (SkippingStrings)
				{
					if (c == '\"')
						State = IN_STRING;
					else if (c == '\'')
						State = IN_CHAR;
				}
			}
			break;
		case IN_CHAR:
			if (c == '\'')
				State = IN_CODE;
			break;
		case COMMENT_MAY_END:
			if (c == '/')
			{
				State = COMMENT_WILL_END;
				break;
			}
			else
			{
				State = IN_COMMENT;
			}
		case IN_MULTILINE_COMMENT:
			if (c == '*')
			{
				State = COMMENT_MAY_END;
			}
			break;
		case IN_STRING:
			if (c == '\"')
				State = IN_CODE;
			break;
		}
	}

	return State == IN_CODE || State == COMMENT_MAY_START;
}

bool CommentSkipper::IsCommentBackward(const WTString& buf, int i)
{
	bool code = IsCodeBackward(buf, i);
	if (code)
		return false;

	return State == IN_COMMENT || State == IN_MULTILINE_COMMENT || State == COMMENT_MAY_END ||
	       State == COMMENT_WILL_END;
}

bool CommentSkipper::IsCodeBackward(LPCSTR buf, int len, int i)
{
	if (!Is_C_CS_File(FileType))
		return true;

	TCHAR c = buf[i];

	if (c == '\r' || c == '\n')
	{
		if ((i + 1 < len && (buf[i + 1] == '\r' || buf[i + 1] == '\n')) || (i > 0 && buf[i - 1] != '\r'))
		{
			// new line
			for (int j = i - 2; j >= 0; j--)
			{
				if (buf[j] == '/' && buf[j + 1] == '/')
				{
					State = IN_COMMENT;
					LastCommentPos = j;
					break;
				}
				if (buf[j] == '#')
				{
					State = IN_PREPROC;
					LastCommentPos = j;
					break;
				}
				if (buf[j] == '\r' || buf[j] == '\n')
				{
					if (IsCFile(FileType) && (j >= 2 && buf[j - 1] == '\\' || buf[j - 2] == '\\'))
						continue; // line continuity character, so not stopping at this line break
					else
						break; // line brake, stopping
				}
			}
		}
	}

	if (LastCommentPos >= i)
	{
		if (LastCommentPos == i && (State == IN_COMMENT || State == IN_PREPROC))
			State = IN_CODE;
	}

	if (SkipNext != NO)
	{
		switch (SkipNext)
		{
		case YES:
			SkipNext = ONLY_NEWLINE;
			return false;
		case ONLY_NEWLINE:
			SkipNext = NO;
			if (c == '\r' || c == '\n')
				return false;
		}
	}
	if (c == '\\')
	{
		SkipNext = YES;
		return false;
	}
	else
	{
	again:
		switch (State)
		{
		case COMMENT_MAY_START:
			if (c == '/')
			{
				State = IN_COMMENT;
			}
			else if (c == '*')
			{
				State = IN_MULTILINE_COMMENT;
			}
			else
			{
				State = IN_CODE;
				goto again;
			}
			break;
		case COMMENT_WILL_END:
			State = IN_CODE;
		case IN_CODE:
			if (c == '/')
				State = COMMENT_MAY_START;
			else if (c == '#')
			{
				// if (!backwards)
				//	State = IN_PREPROC;
			}
			else
			{
				if (SkippingStrings)
				{
					if (c == '\"')
						State = IN_STRING;
					else if (c == '\'')
						State = IN_CHAR;
				}
			}
			break;
		case IN_CHAR:
			if (c == '\'')
				State = IN_CODE;
			break;
		case COMMENT_MAY_END:
			if (c == '/')
			{
				State = COMMENT_WILL_END;
				break;
			}
			else
			{
				State = IN_COMMENT;
			}
		case IN_MULTILINE_COMMENT:
			if (c == '*')
			{
				State = COMMENT_MAY_END;
			}
			break;
		case IN_STRING:
			if (c == '\"')
				State = IN_CODE;
			break;
		}
	}

	return State == IN_CODE || State == COMMENT_MAY_START;
}
