#pragma once

#include <wtypes.h>

class WTString;

// skipping comments, strings and defines. supports multi-line defines and multi-line // style comments via backslash.
// general, state-based working without using a buffer.
// the design assumes you start inside code.
class CommentSkipper
{
  public:
	CommentSkipper() = delete;
	CommentSkipper(int ftype) : FileType(ftype)
	{
		Reset();
	}

	enum eState
	{
		IN_CODE,
		COMMENT_MAY_START,
		COMMENT_MAY_END,
		COMMENT_WILL_END,
		DEFINE_MAY_START,
		IN_COMMENT,
		IN_MULTILINE_COMMENT,
		SKIP_EOL,
		IN_PREPROC,
		IN_STRING,
		IN_CHAR,
		RAW_MAY_START1,
		RAW_MAY_START2,
		IN_RAW_STRING,
		RAW_MAY_END,
		RAW_WILL_END
	};

  private:
	eState State;

	enum eSkip
	{
		NO,
		ONLY_NEWLINE,
		YES,
		SKIP_IN_STRING,
	};

	eSkip SkipNext;
	bool SkippingStrings;
	int LastCommentPos; // last comment or macro pos
	int FileType;

  public:
	void Reset()
	{
		State = IN_CODE;
		SkipNext = NO;
		SkippingStrings = true;
		LastCommentPos = INT_MAX;
	}

	eState GetState() const
	{
		return State;
	}
	bool IsCode(TCHAR c, bool backwards = false);
	bool IsCode2(TCHAR c, TCHAR nc);
	bool IsComment(TCHAR c);
	void NoStringSkip()
	{
		SkippingStrings = false;
	}
	bool IsCodeBackward(const WTString& buf, int i);
	bool IsCodeBackward(LPCSTR buf, int len, int i);
	bool IsCommentBackward(const WTString& buf, int i);
};
