#pragma once

#ifndef VASmartSelect_Utils_h__
#define VASmartSelect_Utils_h__

#include <afxstr.h>
#include "WTString.h"

namespace VASmartSelect
{
namespace Utils
{

template <typename T> T BitOR(T arg)
{
	return arg;
}

template <typename T, typename... Args> T BitOR(T first, Args... args)
{
	return (T)((DWORD)first | (DWORD)BitOR(args...));
}

enum CodeCleanupPart : unsigned int
{
	ccPartNone = 0x0000,

	// sub-parts
	ccPartStart = 0x0001,
	ccPartContent = 0x0002,
	ccPartEnd = 0x0004,

	// types
	ccPartComment = 0x0008,
	ccPartChar = 0x0010,
	ccPartString = 0x0020,
	ccPartDirective = 0x0040,
	ccPartLineCont = 0x0080, // line continuation - also used as modifier

	// used for: Multiline Comment, RAW String, Verbatim String
	ccPartMultiLine = 0x1000,

	// useful masks
	ccPartStartEnd = ccPartStart | ccPartEnd,
	ccPartLiteral = ccPartString | ccPartChar,

	// normal comment: //
	ccPartCommentStart = ccPartStart | ccPartComment,     // on //
	ccPartCommentContent = ccPartContent | ccPartComment, // after //

	// multiline comment: /* */
	ccPartMLCommentStart = ccPartCommentStart | ccPartMultiLine,      // on /*
	ccPartMLCommentContent = ccPartCommentContent | ccPartMultiLine,  // between /* and */
	ccPartMLCommentEnd = ccPartEnd | ccPartComment | ccPartMultiLine, // on */

	// char literal: ' '
	ccPartCharStart = ccPartStart | ccPartChar,     // on starting '
	ccPartCharContent = ccPartContent | ccPartChar, // between ' and '
	ccPartCharEnd = ccPartEnd | ccPartChar,         // on ending '

	// string literal: " " or L" " and so on...
	ccPartStringStart = ccPartStart | ccPartString,     // on starting ", L", u8" and so on
	ccPartStringContent = ccPartContent | ccPartString, // between " and "
	ccPartStringEnd = ccPartEnd | ccPartString,         // on ending "

	// C# verbatim string: @" "
	ccPartVerbStringStart = ccPartStringStart | ccPartMultiLine,     // on @"
	ccPartVerbStringContent = ccPartStringContent | ccPartMultiLine, // between @" and "
	ccPartVerbStringEnd = ccPartStringEnd | ccPartMultiLine,         // on ending "

	// C++ RAW string: R" ", LR" ", u8R" " and so on...
	ccPartRawStringStart = ccPartVerbStringStart,                     // on starting R", LR", u8R" and so on
	ccPartRawStringStartDelim = ccPartRawStringStart | ccPartContent, // on starting delimiter ended by (, such as ABC(
	ccPartRawStringContent = ccPartVerbStringContent,                 // between delimiters
	ccPartRawStringEnd = ccPartVerbStringEnd,                         // on ending delimiter started by ), such as )ABC
	ccPartRawStringEndDelim = ccPartRawStringEnd | ccPartContent,     // on ending "

	// # preprocessor directives
	ccPartDirectiveStart = ccPartDirective | ccPartStart,     // at '#' of the directive
	ccPartDirectiveContent = ccPartDirective | ccPartContent, // after '#' of the directive
	ccPartDirectiveEnd = ccPartDirectiveContent | ccPartEnd,  // last char of the directive (also content)
};

// controls code cleanup process and return replacement for each char
// if passed NULL, each non-space char is replaced by space character
typedef WCHAR (*CodeCleanupFnc)(CodeCleanupPart part, WCHAR ch);

enum CodeCleanupDo : unsigned char
{
	ccDoNone = 0x00,

	ccDoComment = 0x01,        // single and multi line comments
	ccDoStrCharLiteral = 0x02, // string and char literals
	ccDoDirective = 0x04,      // # directives
	ccDoCodeLineCont = 0x08,   // line continuation in code

	ccDoAll = 0x0F
};

//********************************************************************************
// CodeCleanup:
// Allows you to remove all unnecessary parts of code that may confuse parser.
// The result consists of output taken from passed CodeCleanupFnc
// If fnc is nullptr, function alternates almost all chars in range by space ' '
// In such case, only line continuations and white-spaces are preserved
//********************************************************************************
// code		- defines the in/out string to be processed
// is_cpp	- true for C++, false for C# (other languages are not supported)
// comments	- whether handle comments
// fnc		- function to determine what to insert to result instead of current char
//			  If passed NULL, each non-space char is replaced by space character
//********************************************************************************
void CodeCleanup(CStringW& code, bool is_cpp, CodeCleanupDo ccm = ccDoAll, CodeCleanupFnc fnc = nullptr);
void CodeCleanup(WTString& code, bool is_cpp, CodeCleanupDo ccm = ccDoAll, CodeCleanupFnc fnc = nullptr);

int FindWhileSkippingBraces(const CStringW& str, int pos, bool forward, LPCSTR to_find_one_of, bool& is_mismatch);

bool ReadSymbol(const WTString& code, long pos, bool handle_line_continuation, WTString& out_sym, long& spos,
                long& epos);

class StateIterator
{
	typedef unsigned char STATE_TYPE;
	friend struct stateIteratorImpl;
	std::shared_ptr<stateIteratorImpl> it;

  public:
	// Note: must be same as VASmartSelect::Parsing::CharState
	enum class state : STATE_TYPE
	{
		none = 0, // do not change, is used like: if(!state) ...

		dq_string = 0x01,    //: " " or L" "
		sq_string = 0x02,    //: ' ' or L' '
		verbatim_str = 0x04, //: @" "
		raw_str = 0x08,      //: R" " or LR" "
		c_comment = 0x10,    //: /* */
		comment = 0x20,      //: //
		directive = 0x40,    //: # directives
		continuation = 0x80, //: line continuation - in C++ backslash followed by line break

		string_mask = dq_string | sq_string | verbatim_str | raw_str,
		comment_mask = comment | c_comment,
	};

	StateIterator(int s_pos, int e_pos, bool is_cpp, CStringW& code, bool copy_code = false);
	StateIterator(int s_pos, bool is_cpp, CStringW& code, bool copy_code = false);
	StateIterator(bool is_cpp, CStringW& code, bool copy_code = false);

	StateIterator(int s_pos, int e_pos, bool is_cpp, WTString& code, bool copy_code = false);
	StateIterator(int s_pos, bool is_cpp, WTString& code, bool copy_code = false);
	StateIterator(bool is_cpp, WTString& code, bool copy_code = false);

	virtual ~StateIterator();

	state no_code_mask = BitOR(state::string_mask, state::comment_mask, state::continuation);

	bool move_next();
	bool move_to(int index);

	int pos() const;
	wchar_t get_char(int offset = 0) const;

	bool is_state(state s) const;
	bool is_any_of_mask(STATE_TYPE mask) const;
	state get_state() const;

	bool is_none() const
	{
		return get_state() == state::none;
	}
	bool is_dq_string() const
	{
		return is_state(state::dq_string);
	} //: " " or L" "
	bool is_sq_string() const
	{
		return is_state(state::sq_string);
	} //: ' ' or L' '
	bool is_verbatim_str() const
	{
		return is_state(state::verbatim_str);
	} //: @" "
	bool is_raw_str() const
	{
		return is_state(state::raw_str);
	} //: R" ", LR" ", UR" ", uR" ", u8R" "
	bool is_c_comment() const
	{
		return is_state(state::c_comment);
	} //: /* */
	bool is_cpp_comment() const
	{
		return is_state(state::comment);
	} //: //
	bool is_directive() const
	{
		return is_state(state::directive);
	} //: # directives
	bool is_continuation() const
	{
		return is_state(state::continuation);
	} //: line continuation - in C++ backslash followed by line break

	bool is_any_of_mask(state mask) const
	{
		return is_any_of_mask((STATE_TYPE)mask);
	}

	bool is_in_string() const
	{
		return is_any_of_mask(state::string_mask);
	}
	bool is_in_comment() const
	{
		return is_any_of_mask(state::comment_mask);
	}
	bool is_in_code() const
	{
		return !is_any_of_mask(no_code_mask);
	}
};

} // namespace Utils
} // namespace VASmartSelect

#endif // VASmartSelect_Utils_h__
