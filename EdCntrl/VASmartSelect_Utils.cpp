#include "stdafxed.h"

#include "VASmartSelect_Utils.h"
#include "VASmartSelect_Parsing.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

USING_VA_SMART_SELECT_PARSING_NS;

namespace VASmartSelect
{
namespace Utils
{

class RefWstrSrc : public IWstrSrc
{
	LPCWSTR m_str;
	int m_len;

  public:
	RefWstrSrc(const CStringW& in_str) : m_str(in_str), m_len(in_str.GetLength())
	{
		_ASSERTE(m_str != nullptr);
	}

	RefWstrSrc(LPCWSTR in_str, int length = -1) : m_str(in_str), m_len(length >= 0 ? length : wcslen_i(in_str))
	{
		_ASSERTE(m_str != nullptr);
	}

	virtual wchar_t at(int index) const
	{
		_ASSERTE(index < m_len);
		return m_str[index];
	}

	virtual std::wstring substr(int start, int len) const
	{
		_ASSERTE(start < m_len && start + len <= m_len);
		return std::wstring(m_str + start, (uint)len);
	}

	virtual LPCWSTR c_str() const
	{
		return m_str;
	}
	virtual size_t length() const
	{
		return (size_t)m_len;
	}
	virtual bool empty() const
	{
		return m_len == 0;
	}
};

class RefWTStringSrc : public IWstrSrc
{
	const WTString& str;
	mutable std::wstring wbuff;

  public:
	RefWTStringSrc(const WTString& in_str) : str(in_str)
	{
	}

	virtual wchar_t at(int index) const
	{
		return (wchar_t)str[(uint)index];
	}

	virtual std::wstring substr(int start, int len) const
	{
		if (len < 0)
			len = (int)length() - start;
		std::wstring wstr;
		wstr.resize((uint)len);
		for (int i = 0; i < len; i++)
			wstr[(uint)i] = (wchar_t)str[uint(start + i)];
		return wstr;
	}

	virtual size_t length() const
	{
		return (uint)str.GetLength();
	}
	virtual bool empty() const
	{
		return !!str.IsEmpty();
	}

	virtual LPCWSTR c_str() const
	{
		if (wbuff.empty())
			wbuff = substr(0, (int)length());

		return wbuff.c_str();
	}
};

struct CodeCleanupRange
{
	enum type
	{
		tNone,
		tStrChar,
		tComment,
		tDirective,
		tLineCont
	};

	type t;
	int s_pos;
	int e_pos;

	CodeCleanupRange(type _t) : t(_t), s_pos(-1), e_pos(-1)
	{
	}

	void reset()
	{
		s_pos = -1;
		e_pos = -1;
	}
	void open(int start_pos)
	{
		s_pos = start_pos;
		e_pos = -1;
	}

	void close(int end_pos)
	{
		_ASSERTE(s_pos >= 0);
		e_pos = end_pos;
	}

	bool contains(int pos) const
	{
		return is_closed() && pos >= s_pos && pos <= e_pos;
	}

	bool may_contain(int pos) const
	{
		return (is_closed() && pos >= s_pos && pos <= e_pos) || (is_open() && pos >= s_pos);
	}

	bool is_str_char() const
	{
		return t == tStrChar;
	}
	bool is_comment() const
	{
		return t == tComment;
	}
	bool is_directive() const
	{
		return t == tDirective;
	}
	bool is_line_cont() const
	{
		return t == tLineCont;
	}

	bool is_empty() const
	{
		return s_pos < 0 && e_pos < 0;
	}
	bool is_open() const
	{
		return s_pos >= 0 && e_pos < 0;
	}
	bool is_closed() const
	{
		return !is_open();
	}
	bool is_valid() const
	{
		return s_pos >= 0 && e_pos >= s_pos;
	}
};

void GetRanges(SharedWStr& s_wstr, bool is_cpp, bool strings, bool comments, bool directives, bool continuations,
               std::function<void(const CodeCleanupRange&)> func)
{
	// init iterator
	CharStateIter csi = CharStateIter(0, int(s_wstr.length() - 1), !is_cpp, s_wstr);

	auto is_start = [](CharState old_state, CharState new_state, CharState mask) {
		return (new_state & mask) && !(old_state & mask);
	};

	auto is_end = [](CharState old_state, CharState new_state, CharState mask) {
		return (old_state & mask) && !(new_state & mask);
	};

	CodeCleanupRange str_rng(CodeCleanupRange::tStrChar);
	CodeCleanupRange cmt_rng(CodeCleanupRange::tComment);
	CodeCleanupRange lct_rng(CodeCleanupRange::tLineCont);
	CodeCleanupRange dir_rng(CodeCleanupRange::tDirective);

	// setup state change callback for strings and comments
	csi.on_state_change = [&](int pos, CharState old_state, CharState new_state) mutable {
		if (strings)
		{
			if (is_start(old_state, new_state, chs_string_mask))
				str_rng.open(pos);
			else if (is_end(old_state, new_state, chs_string_mask) && str_rng.is_open())
			{
				str_rng.close(pos - 1);
				if (str_rng.is_valid())
					func(str_rng);
				str_rng.reset();
			}
		}

		if (comments)
		{
			if (is_start(old_state, new_state, chs_comment_mask))
				cmt_rng.open(pos);
			else if (is_end(old_state, new_state, chs_comment_mask) && cmt_rng.is_open())
			{
				cmt_rng.close(pos - 1);
				if (cmt_rng.is_valid())
					func(cmt_rng);
				cmt_rng.reset();
			}
		}

		if (directives)
		{
			if (is_start(old_state, new_state, chs_directive))
				dir_rng.open(pos);
			else if (is_end(old_state, new_state, chs_directive) && dir_rng.is_open())
			{
				dir_rng.close(pos - 1);
				if (dir_rng.is_valid())
					func(dir_rng);
				dir_rng.reset();
			}
		}

		if (continuations)
		{
			if (is_start(old_state, new_state, chs_continuation))
				lct_rng.open(pos);
			else if (is_end(old_state, new_state, chs_continuation) && lct_rng.is_open())
			{
				lct_rng.close(pos - 1);
				if (lct_rng.is_valid())
				{
					bool is_sub_range = false;

					if (!is_sub_range && dir_rng.is_open())
						is_sub_range = dir_rng.may_contain(lct_rng.s_pos);

					if (!is_sub_range && str_rng.is_open())
						is_sub_range = str_rng.may_contain(lct_rng.s_pos);

					if (!is_sub_range && cmt_rng.is_open())
						is_sub_range = cmt_rng.may_contain(lct_rng.s_pos);

					if (!is_sub_range)
						func(lct_rng);
				}
				lct_rng.reset();
			}
		}
	};

	// process all chars
	while (csi.next())
		;

	// on end append also open ranges

	if (str_rng.is_open())
		func(str_rng);
	if (cmt_rng.is_open())
		func(cmt_rng);
	if (lct_rng.is_open())
		func(lct_rng);
	if (dir_rng.is_open())
		func(dir_rng);
}

template <typename CHAR_TYPE> class CodeCleanupCls
{
	CHAR_TYPE* buff;
	SharedWStr& s_wstr;
	bool is_cpp;
	int rng_s;
	int rng_e;
	CodeCleanupFnc fnc;

  public:
	CodeCleanupCls(CHAR_TYPE* _buff, SharedWStr& _s_wstr, bool _is_cpp, int rng_start, int rng_end, CodeCleanupFnc _fnc)
	    : buff(_buff), s_wstr(_s_wstr), is_cpp(_is_cpp), rng_s(rng_start), rng_e(rng_end), fnc(_fnc)
	{
	}

	void process_range(const CodeCleanupRange& rng)
	{
		switch (rng.t)
		{
		case CodeCleanupRange::tStrChar:
			return on_str_char(rng);
		case CodeCleanupRange::tComment:
			return on_comment(rng);
		case CodeCleanupRange::tDirective:
			return on_directive(rng);
		case CodeCleanupRange::tLineCont:
			return on_line_cont(rng);
		default:
			_ASSERTE(!"Unknown range type");
			break;
		}
	}

  protected:
	///////////////////////////////////////
	// process COMMENTS

	void on_comment(const CodeCleanupRange& rng)
	{
		// may happen that one of sides is -1 => that is error
		if (rng.is_valid() || rng.is_open())
		{
			bool open_ended = rng.is_open();
			int s_pos = rng.s_pos;
			int e_pos = !open_ended ? rng.e_pos : rng_e;

			if (fnc)
			{
				int s_content = s_pos;
				int e_content = e_pos;

				DWORD scp = ccPartStart | ccPartComment;
				DWORD mcp = ccPartContent | ccPartComment;
				DWORD ecp = (open_ended ? ccPartContent : ccPartEnd) | ccPartComment;

				if (buff[s_pos + 1] == '*')
				{
					s_content += 2;
					e_content -= 2;

					scp |= ccPartMultiLine;
					mcp |= ccPartMultiLine;
					ecp |= ccPartMultiLine;
				}
				else
				{
					s_content += 2;
				}

				for (int p = s_pos; p <= e_pos; p++)
				{
					if (is_cpp && !(scp & ccPartMultiLine) && s_wstr.is_within_line_continuation(p))
					{
						scp |= ccPartLineCont;
						mcp |= ccPartLineCont;
						ecp |= ccPartLineCont;
					}

					if (p < s_content)
						buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)scp, (WCHAR)buff[p]);
					else if (p <= e_content)
						buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)mcp, (WCHAR)buff[p]);
					else
						buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)ecp, (WCHAR)buff[p]);
				}
			}
			else
			{
				for (int p = s_pos; p <= e_pos; p++)
					if (!wt_isspace(buff[p]) && !(is_cpp && s_wstr.is_within_line_continuation(p)))
						buff[p] = ' ';
			}
		}
	}

	///////////////////////////////////////
	// process STRING LITERALS

	void on_str_char(const CodeCleanupRange& rng)
	{
		// may happen that one of sides is -1 => that is error
		if (rng.is_valid() || rng.is_open())
		{
			bool open_ended = rng.is_open();
			int s_pos = rng.s_pos;
			int e_pos = !open_ended ? rng.e_pos : rng_e;

			if (!fnc)
			{
				for (int p = s_pos; p <= e_pos; p++)
					if (!wt_isspace(buff[p]) && !(is_cpp && s_wstr.is_within_line_continuation(p)))
						buff[p] = ' ';
			}
			else
			{
				int s_content = s_pos;
				int e_content = e_pos;

				for (; s_content < e_pos; ++s_content)
					if (buff[s_content] == '"' || buff[s_content] == '\'')
						break;

				const bool is_char = buff[s_content] == '\'';
				const bool is_verb = !is_char && !is_cpp && s_content > 0 && buff[s_content - 1] == '@';
				bool is_raw = !is_char && is_cpp && s_content > 0 && buff[s_content - 1] == 'R';

				s_content++;
				e_content--;

				if (!is_raw)
				{
					DWORD scp =
					    ccPartStart | (is_char ? ccPartChar : ccPartString) | (is_verb ? ccPartMultiLine : ccPartNone);
					DWORD mcp = ccPartContent | (is_char ? ccPartChar : ccPartString) |
					            (is_verb ? ccPartMultiLine : ccPartNone);
					DWORD ecp = (open_ended ? ccPartContent : ccPartEnd) | (is_char ? ccPartChar : ccPartString) |
					            (is_verb ? ccPartMultiLine : ccPartNone);

					for (int p = s_pos; p <= e_pos; p++)
					{
						if (is_cpp && !(scp & ccPartMultiLine) && s_wstr.is_within_line_continuation(p))
						{
							scp |= ccPartLineCont;
							mcp |= ccPartLineCont;
							ecp |= ccPartLineCont;
						}

						if (p < s_content)
							buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)scp, (WCHAR)buff[p]);
						else if (p <= e_content)
							buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)mcp, (WCHAR)buff[p]);
						else
							buff[p] = (CHAR_TYPE)fnc((CodeCleanupPart)ecp, (WCHAR)buff[p]);
					}
				}
				else
				{
					int s_delim = s_content;
					int e_delim = e_content;

					bool is_invalid = false;

					int cnt_len = e_delim - s_delim + 1;
					for (int x = 1; x < cnt_len; x++)
					{
						if (buff[s_delim + x] == '(' && buff[e_delim - x] == ')')
						{
							s_delim += x;
							e_delim -= x;
							break;
						}
						else if (s_delim + x >= e_delim - x)
						{
#ifndef VA_CPPUNIT
							_ASSERTE(!"Invalid RAW string!");
#endif
							is_invalid = true;
							break;
						}
					}

					// NOTE: RAW strings don't support line continuation character!!!

					if (is_invalid)
					{
						for (int p = s_pos; p <= e_pos; p++)
						{
							if (p < s_content)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringStart, (WCHAR)buff[p]);
							else if (p <= e_content || open_ended)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringContent, (WCHAR)buff[p]);
							else
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringEnd, (WCHAR)buff[p]);
						}
					}
					else
					{
						for (int p = s_pos; p <= e_pos; p++)
						{
							if (p < s_content)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringStart, (WCHAR)buff[p]);
							else if (p <= s_delim)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringStartDelim, (WCHAR)buff[p]);
							else if (p < e_delim || open_ended)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringContent, (WCHAR)buff[p]);
							else if (p <= e_content)
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringEndDelim, (WCHAR)buff[p]);
							else
								buff[p] = (CHAR_TYPE)fnc(ccPartRawStringEnd, (WCHAR)buff[p]);
						}
					}
				}
			}
		}
	}

	///////////////////////////////////////
	// process DIRECTIVES

	void on_directive(const CodeCleanupRange& rng)
	{
		if (rng.is_valid())
		{
			int s_pos = rng.s_pos;
			int e_pos = rng.e_pos;

			bool isLineCont = false;

			for (int i = s_pos; i <= e_pos; i++)
			{
				isLineCont = is_cpp && s_wstr.is_within_line_continuation(i);

				if (fnc)
				{
					DWORD part = ccPartDirective;

					if (i == s_pos)
						part |= ccPartStart;
					else if (i == e_pos)
						part |= (ccPartEnd | ccPartContent);
					else
						part |= ccPartContent;

					if (isLineCont)
						part |= ccPartLineCont;

					buff[i] = (CHAR_TYPE)fnc((CodeCleanupPart)part, (WCHAR)buff[i]);
				}
				else if (!isLineCont && !wt_isspace(buff[i]))
					buff[i] = ' ';
			}
		}
	};

	///////////////////////////////////////
	// process LINE CONTINUATIONS in code

	void on_line_cont(const CodeCleanupRange& rng)
	{
		if (rng.is_valid())
		{
			int s_pos = rng.s_pos;
			int e_pos = rng.e_pos;

			for (int i = s_pos; i <= e_pos; i++)
			{
				DWORD part = ccPartLineCont;

				if (i == s_pos)
					part |= ccPartStart;

				if (i == e_pos)
					part |= ccPartEnd;

				buff[i] = (CHAR_TYPE)fnc((CodeCleanupPart)part, (WCHAR)buff[i]);
			}
		}
	}
};

void CodeCleanup(CStringW& code, bool is_cpp, CodeCleanupDo ccm /*= ccmAll*/, CodeCleanupFnc fnc /*= nullptr*/)
{
	SharedWStr s_wstr(IWstrSrc::Ptr(new RefWstrSrc(code)));

	LPWSTR buff = code.GetBuffer();
	struct _auto_release_buffer
	{
		CStringW& m_str;
		_auto_release_buffer(CStringW& str) : m_str(str)
		{
		}
		~_auto_release_buffer()
		{
			m_str.ReleaseBuffer();
		}
	} _arb(code);

	CodeCleanupCls<WCHAR> ccc(buff, s_wstr, is_cpp, 0, code.GetLength() - 1, fnc);

	GetRanges(s_wstr, is_cpp, !!(ccm & ccDoStrCharLiteral), !!(ccm & ccDoComment), !!(ccm & ccDoDirective),
	          !!(ccm & ccDoCodeLineCont), [&](const CodeCleanupRange& rng) { ccc.process_range(rng); });
}

void CodeCleanup(WTString& code, bool is_cpp, CodeCleanupDo ccm /*= ccmAll*/, CodeCleanupFnc fnc /*= nullptr*/)
{
	SharedWStr s_wstr(IWstrSrc::Ptr(new RefWTStringSrc(code)));

	LPTSTR buff = code.GetBuffer(code.GetLength());
	struct _auto_release_buffer
	{
		WTString& m_str;
		_auto_release_buffer(WTString& str) : m_str(str)
		{
		}
		~_auto_release_buffer()
		{
			m_str.ReleaseBuffer();
		}
	} _arb(code);

	CodeCleanupCls<TCHAR> ccc(buff, s_wstr, is_cpp, 0, code.GetLength() - 1, fnc);

	GetRanges(s_wstr, is_cpp, !!(ccm & ccDoStrCharLiteral), !!(ccm & ccDoComment), !!(ccm & ccDoDirective),
	          !!(ccm & ccDoCodeLineCont), [&](const CodeCleanupRange& rng) { ccc.process_range(rng); });
}

int FindWhileSkippingBraces(const CStringW& str, int pos, bool forward, LPCSTR to_find_one_of, bool& is_mismatch)
{
	CStringW list(to_find_one_of);

	BraceCounter bc("()[]{}<>", !forward);
	bc.set_ignore_angle_brackets(false);

	int end_pos = forward ? str.GetLength() : -1;
	int step = forward ? 1 : -1;

	is_mismatch = false;

	for (int i = pos; i != end_pos; i += step)
	{
		wchar_t ch = str[i];

		auto prev_state = bc.get_state();

		bc.on_char(ch, i, nullptr);

		if (bc.is_mismatch())
		{
			if (list.Find(ch) != -1)
			{
				is_mismatch = true;
				return i;
			}

			bc.set_state(prev_state);
		}
		else if (bc.is_stack_empty() && list.Find(ch) != -1)
			return i;
	}

	return -1;
}

bool ReadSymbol(const WTString& code, long pos, bool handle_line_continuation, WTString& out_sym, long& spos,
                long& epos)
{
	SharedWStr str(IWstrSrc::Ptr(new RefWTStringSrc(code)));

	if (!str.is_identifier(pos))
		return false;

	CString sym(code[(uint)pos]);

	long tmp_spos = spos = pos;
	long tmp_epos = epos = pos;

	while (str.is_identifier(tmp_spos - 1) ||
	       (handle_line_continuation && str.is_within_line_continuation(tmp_spos - 1)))
	{
		tmp_spos--;

		if (str.is_identifier(tmp_spos))
		{
			spos = tmp_spos;
			sym.Insert(0, code[(uint)tmp_spos]);
		}
	}

	while (str.is_identifier(tmp_epos + 1) ||
	       (handle_line_continuation && str.is_within_line_continuation(tmp_epos + 1)))
	{
		tmp_epos++;

		if (str.is_identifier(tmp_epos))
		{
			sym.AppendChar(code[(uint)tmp_epos]);
			epos = tmp_epos;
		}
	}

	epos++; // convert SS style to normal end
	out_sym = (LPCTSTR)sym;
	return true;
}

struct stateIteratorImpl : public CharStateIter
{
	stateIteratorImpl(int s_pos, int e_pos, bool is_cpp, SharedWStr code) : CharStateIter(s_pos, e_pos, !is_cpp, code)
	{
	}
};

StateIterator::StateIterator(int s_pos, int e_pos, bool is_cpp, CStringW& code, bool copy_code /*= false*/)
{
	SharedWStr sc;

	if (copy_code)
		sc = SharedWStr(code);
	else
		sc = SharedWStr(IWstrSrc::Ptr(new RefWstrSrc(code)));

	it.reset(new stateIteratorImpl(s_pos, e_pos, is_cpp, sc), [=](stateIteratorImpl* obj) { delete obj; });
}

StateIterator::StateIterator(bool is_cpp, CStringW& code, bool copy_code /*= false*/)
    : StateIterator(0, code.GetLength() - 1, is_cpp, code, copy_code)
{
}

StateIterator::StateIterator(int s_pos, bool is_cpp, CStringW& code, bool copy_code /*= false*/)
    : StateIterator(s_pos, code.GetLength() - 1, is_cpp, code, copy_code)
{
}

StateIterator::StateIterator(int s_pos, int e_pos, bool is_cpp, WTString& code, bool copy_code /*= false*/)
{
	SharedWStr sc;

	if (copy_code)
		sc = SharedWStr(code.Wide());
	else
		sc = SharedWStr(IWstrSrc::Ptr(new RefWTStringSrc(code)));

	it.reset(new stateIteratorImpl(s_pos, e_pos, is_cpp, sc), [=](stateIteratorImpl* obj) { delete obj; });
}

StateIterator::StateIterator(int s_pos, bool is_cpp, WTString& code, bool copy_code /*= false*/)
    : StateIterator(s_pos, code.GetLength() - 1, is_cpp, code, copy_code)
{
}

StateIterator::StateIterator(bool is_cpp, WTString& code, bool copy_code /*= false*/)
    : StateIterator(0, code.GetLength() - 1, is_cpp, code, copy_code)
{
}

StateIterator::~StateIterator()
{
}

bool StateIterator::move_next()
{
	return it->next();
}

bool StateIterator::move_to(int index)
{
	_ASSERTE(index >= it->pos);

	while (it->pos < index && it->next())
		;
	return it->pos == index;
}

int StateIterator::pos() const
{
	return it->pos;
}

wchar_t StateIterator::get_char(int offset /*= 0*/) const
{
	switch (offset)
	{
	case 0:
		return it->ch;
	case 1:
		return it->ch_next;
	case -1:
		return it->ch_prev;
	case 2:
		return it->ch_next2;
	case -2:
		return it->ch_prev2;
	case 3:
		return it->ch_next3;
	default:
		return it->src.safe_at(it->pos + offset);
	}
}

bool StateIterator::is_state(state s) const
{
	return ((STATE_TYPE)it->state & (STATE_TYPE)s) == (STATE_TYPE)s;
}

bool StateIterator::is_any_of_mask(STATE_TYPE mask) const
{
	return ((STATE_TYPE)it->state & (STATE_TYPE)mask) != 0;
}

StateIterator::state StateIterator::get_state() const
{
	return (state)it->state;
}

} // namespace Utils
} // namespace VASmartSelect
