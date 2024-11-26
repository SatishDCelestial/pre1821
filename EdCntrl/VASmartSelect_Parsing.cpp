#include "StdAfxEd.h"
#include "VASmartSelect_Parsing.h"
#include "VAParse.h"
#include <iomanip>
#include "TraceWindowFrame.h"
#include "VAAutomation.h"
#include <mutex>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

using namespace VASmartSelect::Parsing;

void regex_replace_lambda(std::wstring& out, const std::wstring& input, std::wregex& rgx,
                          std::function<const wchar_t*(const std::wsmatch&, bool&)> fnc,
                          std::regex_constants::match_flag_type flags /*= std::regex_constants::match_default*/)
{
	std::wstring in_tmp;
	bool use_tmp = false;

	if (&input == &out)
	{
		in_tmp = input;
		use_tmp = true;
	}

	std::wstring::const_iterator first = use_tmp ? in_tmp.begin() : input.begin();
	std::wstring::const_iterator last = use_tmp ? in_tmp.end() : input.end();

	std::wstring::const_iterator pos = first;

	std::wsmatch m;
	std::wsmatch::difference_type offset = 0;
	bool do_continue = true;
	int count = 0;

	while (std::regex_search(pos, last, m, rgx, flags))
	{
		std::wstring w = m.str();

		if (m.prefix().length())
			out += m.prefix().str();

		out += fnc(m, do_continue);
		count++;

		if (!do_continue)
			break;

		pos = first + (offset + m.position() + m.length());
		offset += m.position() + m.length();
	}

	if (count == 0)
		out.assign(first, last);
	else
		out += m.suffix().str();
}

void regex_for_each(const std::wstring& input, const std::wregex& rgx, std::function<bool(const MatchWrapp&)> fnc,
                    std::regex_constants::match_flag_type flags /*= std::regex_constants::match_default*/)
{
	std::wstring::const_iterator first = input.begin();
	std::wstring::const_iterator last = input.end();
	std::wstring::const_iterator pos = first;

	std::wsmatch m;
	MatchWrapp mWrapp;
	int offset = 0;

	while (std::regex_search(pos, last, m, rgx, flags))
	{
		mWrapp.m = &m;
		mWrapp.m_str.clear();
		mWrapp.m_offset = offset;

		if (!fnc(mWrapp))
			break;

		pos += m.position() + m.length();
		offset += int(m.position() + m.length());
	}
}

int Parser::FindTemplateOpposite(int pos, SharedWStr str, CodeBlock::CharStates& states)
{
	auto code_filter = str.make_char_filter(states);
	wchar_t ch = str.safe_at(pos);
	int s_pos, e_pos;

	if (ch == '>')
	{
		e_pos = pos;
		s_pos = str.find_matching_brace_strict(pos, code_filter);

		if (s_pos < 0)
			return -1;
	}
	else if (ch == '<')
	{
		s_pos = pos;
		e_pos = str.find_matching_brace_strict(pos, code_filter);

		if (e_pos < 0)
			return -1;
	}
	else
	{
		return -1;
	}

	pos = str.rfind_nospace(s_pos - 1, code_filter);
	wchar_t sym_ch = str.safe_at(pos);

	if (!ISCSYM(sym_ch))
		return -1;

	EdCntPtr ed(g_currentEdCnt);
	WTString srcBuf = ed->GetBuf();
	MultiParsePtr mp = ed->GetParseDb();

	pos = ::AdjustPosForMbChars(srcBuf, pos);

	WTString scope;
	DTypePtr pos_res = SymFromPos(srcBuf, mp, pos, scope);

	if (pos_res && (pos_res->IsMethod() || pos_res->IsType()))
		return ch == '<' ? e_pos : s_pos;

	return -1;
}

bool Parser::IsEqualityOperator(LPCWSTR op, bool is_cs)
{
	if (!op)
		return false;

	return !!GetOperatorInfo(op, is_cs, OpType::Equality);
}

bool Parser::IsRelationalOperator(LPCWSTR op, bool is_cs)
{
	if (!op)
		return false;

	return !!GetOperatorInfo(op, is_cs, OpType::Relational);
}

int Parser::IndexOf(wchar_t ch, LPCWSTR str, int count /*= -1*/)
{
	if (count == 0)
		return -1;

	int index = 0;
	while (str)
	{
		if (str[0] == ch)
			return index;

		if (!str[0] || ++index == count)
			break;

		str++;
	}

	return -1;
}

bool Parser::IsComparisonOperator(LPCWSTR op, bool is_cs)
{
	if (!op)
		return false;

	return !!GetOperatorInfo(op, is_cs, OpType::Comparison);
}

bool Parser::IsAssignmentOperator(LPCWSTR op, bool is_cs)
{
	if (!op)
		return false;

	return !!GetOperatorInfo(op, is_cs, OpType::Assignment);
}

int Parser::GetOperatorPrecedence(LPCWSTR op, bool is_cs, OpType typeMask)
{
	if (!op)
		return 0;

	auto nfo = GetOperatorInfo(op, is_cs, typeMask);

	if (nfo)
		return nfo->prec;

	return 0;
}

const OpNfo* Parser::GetOperatorInfo(LPCWSTR op, bool is_cs, OpType typeMask)
{
	if (typeMask != OpType::None)
	{
		for (const auto& nfo : GetOpNfoVector(is_cs))
		{
			if (((DWORD)typeMask & (DWORD)nfo.getType()) == 0)
				continue;

			if (wcscmp(op, nfo.op) == 0)
			{
				return &nfo;
			}
		}
	}
	return nullptr;
}

const std::vector<VASmartSelect::Parsing::OpNfo>& Parser::GetOpNfoVector(bool is_cs)
{
	struct op_info_vec
	{
		std::vector<OpNfo> vec;

		op_info_vec(std::initializer_list<LPCWSTR> strs)
		{
			OpNfo nfo = {0};

			for (LPCWSTR str : strs)
			{
				nfo.prec++; // 1 is highest
				while (str && *str)
				{
					// prefix 'a' to 'k' define type
					if (*str >= 'a' && *str <= 'z')
						str++;

					nfo.len = (byte)wcslen(str);
					nfo.op = str;

					vec.push_back(nfo);

					str += nfo.len + 1;
				}
			}

			std::sort(vec.rbegin(), vec.rend());
		}
	};

	// 	Primary,		// a
	// 	Suffix,			// b
	// 	Prefix,			// c (unary)
	// 	Multiplicative, // d
	// 	Additive,		// e
	// 	Shift,			// f
	// 	Comparison,		// g
	// 	Equality,		// h
	// 	Logical,		// i
	// 	Conditional,	// j
	// 	Assignment		// k
	// 	Splitter		// l

	static const op_info_vec cpp_ops(
	                  {
	                      // sorted by precedence
	                      // lover value == higher precedence

	                      L"a::\0",
	                      L"a.\0a->\0a...\0b++\0b--\0",
	                      L"c++\0c--\0c+\0c-\0c!\0c~\0c*\0c&\0c^\0c%\0",
	                      L"a.*\0a->*\0",
	                      L"d*\0d/\0d%\0",
	                      L"e+\0e-\0",
	                      L"f<<\0f>>\0",
	                      L"g<=>\0",
	                      L"g<\0g<=\0g>\0g>=\0",
	                      L"h==\0h!=\0",
	                      L"i&\0",
	                      L"i^\0",
	                      L"i|\0",
	                      L"j&&\0",
	                      L"j||\0",
	                      L"j?\0j:\0k=\0k+=\0k-=\0k*=\0k/=\0k%=\0k<<=\0k>>=\0k&=\0k^=\0k|=\0",
	                      L"l,\0l;\0",
	                  });

	static const op_info_vec cs_ops(
	                  {
	                      // sorted by precedence
	                      // lover value == higher precedence

	                      L"a::\0a.\0a?.\0a?\0a->\0b++\0b--\0b!\0",
	                      L"c++\0c--\0c+\0c-\0c!\0c~\0c*\0c&\0",
	                      L"a..\0", // range C# 8.0
	                      L"d*\0/d\0%d\0",
	                      L"e+\0e-\0",
	                      L"f<<\0f>>\0",
	                      L"g<\0g<=\0g>\0g>=\0",
	                      L"h==\0h!=\0",
	                      L"i&\0",
	                      L"i^\0",
	                      L"i|\0",
	                      L"j&&\0",
	                      L"j||\0",
	                      L"j??\0",
	                      L"k?\0k:\0",
	                      L"k=\0k+=\0k-=\0k*=\0k/=\0k%=\0k<<=\0k>>=\0k&=\0k^=\0k|=\0k??=\0",
	                      L"l,\0l;\0",
	                  });

	return is_cs ? cs_ops.vec : cpp_ops.vec;
}

void Parser::MarkCommentsAndContants(SharedWStr str, CodeBlock::CharStates& states, bool is_cs)
{
	size_t str_length = str.length();

	if (states.size() < str_length)
		states.resize(str_length);

	CharStateIter csb(0, int(str_length - 1), is_cs, str);

	while (csb.next())
		states.Set(csb.pos, csb.state);

#ifdef _SSPARSE_DEBUG

	std::vector<std::pair<wchar_t, CharState>> dbg_states(str_length);
	for (size_t i = 0; i < str.length(); i++)
		dbg_states[i] = std::pair<wchar_t, CharState>(str[i], states[i].state);

	int y = 0;

#endif
}

void Parser::MarkCommentsAndContants(SharedWStr str, CodeBlock::CharStates& states, std::map<int, int>* cs_int_exprs,
                                     std::vector<int>* state_borders, std::vector<ConditionalBlock::Ptr>* cnd_blocks,
                                     std::vector<int>* directive_starts, std::function<void(int, int, char)> numerics,
                                     TextLines* lines,
                                     const std::function<void(int, wchar_t, CharState)>& on_char_state, bool is_cs)
{
	size_t str_length = str.length();

	if (states.size() < str_length)
		states.resize(str_length + 1);

	CharStateIter csb(0, int(str_length - 1), is_cs, str);

	csb.on_num_literal = numerics;

	if (is_cs && cs_int_exprs)
	{
		auto& exprMap = *cs_int_exprs;
		csb.on_interp = [&](int x, int y, const VASmartSelect::Parsing::CharStateIter* iter) {
			if (y > x)
				exprMap[x] = y;
		};
	}

	if (cnd_blocks)
	{
		csb.on_new_conditional = [cnd_blocks](ConditionalBlock::Ptr cb) { cnd_blocks->push_back(cb); };
	}

	if (directive_starts)
	{
		csb.on_directive = [directive_starts](int pos) { directive_starts->push_back(pos); };
	}

	if (lines)
	{
		csb.on_line_end = [lines](int s_pos, int e_pos, int delim_len) {
			lines->add_line((uint)s_pos, (uint)e_pos, (BYTE)delim_len);
		};
	}

	if (state_borders)
	{
		csb.on_state_change = [state_borders](int pos, CharState old_state, CharState new_state) {
			state_borders->push_back(pos);
		};
	}

	while (csb.next())
	{
		states.Set(csb.pos, csb.state);

		if (on_char_state)
			on_char_state(csb.pos, csb.ch, csb.state);
	}

#ifdef _SSPARSE_DEBUG

	std::vector<std::pair<wchar_t, CharState>> dbg_states(str_length);

	std::wstring code(str_length, 0), strings(str_length, 0), comments(str_length, 0);

	for (size_t i = 0; i < str.length(); i++)
	{
		const auto& st = states[i];
		wchar_t ch = str[i];

		dbg_states[i] = std::pair<wchar_t, CharState>(ch, st.state);
		code[i] = (!wt_isspace(ch) && st.HasAnyBitOfMask(chs_comment_mask | chs_string_mask)) ? ' ' : ch;
		strings[i] = (wt_isspace(ch) || st.HasAnyBitOfMask(chs_string_mask)) ? ch : ' ';
		comments[i] = (wt_isspace(ch) || st.HasAnyBitOfMask(chs_comment_mask)) ? ch : ' ';
	}

	int y = 0;

#endif
}

CodeBlock& Parser::ResolveVAOutlineBlock(FileLineMarker& marker, SharedWStr str, CodeBlock::CharStates& states,
                                         CodeBlock& block, bool is_cs)
{
	block.clamp_to_min_max(0, int(str.length() - 1));

	int s_pos = block.start_pos();
	int e_pos = block.end_pos();

#ifdef _SSPARSE_DEBUG
	std::vector<std::pair<wchar_t, CharState>> dbg_states(e_pos - s_pos + 1);

	for (int i = s_pos; i <= e_pos; i++)
		dbg_states[i - s_pos] = std::pair<wchar_t, CharState>(str[i], states[i].state);

	int y = 0;
#endif

	CharState whiteSpace_mask = chs_comment_mask;
	auto filter = [&states, &whiteSpace_mask](int pos, wchar_t& ch) -> bool {
		if (states[pos].Contains(chs_continuation))
			return false; // ignore line continuations

		if (states[pos].HasAnyBitOfMask(whiteSpace_mask))
			ch = ' '; // instead of comment return space

		return true;
	};

	// Following calls trim out whitespace and comments
	// Comments are considered whitespace due to filter
	s_pos = str.find_nospace(s_pos, filter);
	e_pos = str.rfind_nospace(e_pos, filter);

	// don't support comment blocks
	_ASSERTE(s_pos >= 0 && e_pos >= 0);

	if (s_pos < 0 || e_pos < 0)
		return block;

	// if block is directive,
	// do not resolve open/close.
	if (states[s_pos].Contains(chs_directive))
	{
		block.open_char = '#';
		block.top_pos = s_pos;
		block.open_pos = -1;
		block.close_pos = -1;
		block.semicolon_pos = e_pos;
		block.src_code = str;
		block.name = marker.mText;

		for (int i = e_pos; i >= s_pos; --i)
		{
			if (states[i].Contains(chs_directive))
			{
				block.semicolon_pos = i;
				break;
			}

			if (!wt_isspace(str.safe_at(i, filter)))
			{
				if (gTestsActive && gTestLogger != nullptr)
					gTestLogger->LogStr("Invalid VA Outline directive block detected.");

				_ASSERTE(!"Invalid VA Outline directive block detected.");
			}
		}

		static std::wregex rgn_rgx(LR"(^\s*#\s*(?:pragma)?\s*region)", std::wregex::ECMAScript | std::wregex::optimize);

		std::wcmatch m;
		if (std::regex_search((LPCWSTR)marker.mText, m, rgn_rgx) && m.position() == 0 && m.size() > 0)
		{
			block.open_pos = str.next_line_start(s_pos);
			block.close_pos = str.prev_line_end(e_pos);
			block.type = CodeBlock::b_directive;
		}
		else
		{
			block.type = CodeBlock::b_va_outline;
		}

		// comments must be resolved so that tree
		// can find blocks from their comments
		ResolveComments(block, str, states);
		return block;
	}

	if (!is_cs)
	{
		// If this is modifier block, there is nothing to resolve
		std::wstring code = ReadCode(
		    str, states, s_pos, [e_pos](int pos, std::wstring& wstr) { return pos == e_pos || wstr.length() >= 10; },
		    false);

		if (wcsncmp(code.c_str(), L"public:", 7) == 0 || wcsncmp(code.c_str(), L"private:", 8) == 0 ||
		    wcsncmp(code.c_str(), L"protected:", 10) == 0 || wcsncmp(code.c_str(), L"__published:", 12) == 0)
		{
			block.type = CodeBlock::b_cpp_modifier;
			block.name = code.substr(0, code.find(L':') + 1);

			// comments must be resolved so that tree
			// can find blocks from their comments
			ResolveComments(block, str, states);
			return block;
		}
	}

	block.top_pos = s_pos;
	block.semicolon_pos = e_pos;
	block.src_code = str;
	block.type = CodeBlock::b_va_outline;

	// temporarily ignore also string literals
	whiteSpace_mask = (CharState)(whiteSpace_mask | chs_string_mask);

	int close_pos = str.rfind(
	    e_pos, [](wchar_t ch) { return ch == '}' || ch == ')'; }, filter);
	if (close_pos >= 0)
	{
		int semicolon_pos = str.find(
		    close_pos + 1, e_pos, [](wchar_t ch) { return ch == ';'; }, filter);
		int open_pos = str.find_matching_brace(close_pos, filter, s_pos, e_pos);

		// _ASSERTE(open_pos >= 0);

		if (open_pos >= 0 && open_pos < close_pos)
		{
			block.open_pos = open_pos;
			block.close_pos = close_pos;
			block.semicolon_pos = semicolon_pos;
			block.open_char = str[(uint)open_pos];

			if (block.open_char == '(' && semicolon_pos >= 0)
				block.type = CodeBlock::b_declaration;
			else if (block.open_char == '{')
				block.type = CodeBlock::b_definition;
		}
	}

	// remove ignoring of string literals
	whiteSpace_mask = (CharState)(whiteSpace_mask & ~chs_string_mask);

	// block.name = marker.mText;
	// CodeBlock::ellipsis_scopes_and_literals(block.name);
	CodeBlock::block_type type = block.type;
	block.ensure_name_and_type(states, is_cs);

	if (block.type == CodeBlock::b_undefined)
		block.type = type;

	// comments must be resolved so that tree
	// can find blocks from their comments
	ResolveComments(block, str, states);

	return block;
}

CodeBlock& Parser::ResolveBlock(SharedWStr str, CodeBlock::CharStates& states, CodeBlock& block, bool is_cs,
                                std::function<bool(wchar_t, int)> ang_filter)
{
	if (block.open_char == '<')
		return block;

	if (block.top_pos >= 0)
		return block;

	if (block.open_pos < 0 || states[block.open_pos].HasAnyBitOfMask(chs_comment_mask | chs_string_mask))
		return block;

#ifdef _SSPARSE_DEBUG

	std::vector<std::pair<wchar_t, CharState>> dbg_states(str.length());
	for (size_t i = 0; i < str.length(); i++)
		dbg_states[i] = std::pair<wchar_t, CharState>(str[i], states[i].state);

	int y = 0;

#endif

	std::string braces = "()[]{}<>";
	size_t braces_len = braces.length();

	/*static*/ auto trim = [](const std::wstring& s) -> std::wstring {
		std::locale locale;
		auto is_space = [&locale](wchar_t c) { return std::isspace(c, locale); };
		auto wsfront = std::find_if_not(s.begin(), s.end(), is_space);
		return std::wstring(
		    wsfront, std::find_if_not(s.rbegin(), std::wstring::const_reverse_iterator(wsfront), is_space).base());
	};

	auto opening_id = [&braces, braces_len, ang_filter](int pos, wchar_t test_ch) -> int {
		if (pos != -1 && (test_ch == '<' || test_ch == '>'))
			if (!ang_filter(test_ch, pos))
				return -1;

		for (size_t i = 0; i < braces_len; i += 2)
			if (braces[i] == test_ch)
				return (int)i;

		return -1;
	};

	auto closing_id = [&braces, braces_len, ang_filter](int pos, wchar_t test_ch) -> int {
		if (pos != -1 && (test_ch == '<' || test_ch == '>'))
			if (!ang_filter(test_ch, pos))
				return -1;

		for (size_t i = 1; i < braces_len; i += 2)
			if (braces[i] == test_ch)
				return int(i - 1);

		return -1;
	};

	auto is_closing_of = [opening_id, closing_id](wchar_t ch_open, wchar_t ch_close) -> bool {
		int op_id = opening_id(-1, ch_open);
		return op_id >= 0 && op_id == closing_id(-1, ch_close);
	};

	auto is_identifier = [](wchar_t ch) -> bool { return ISCSYM(ch); };

	auto is_identifier_start = [](wchar_t ch, wchar_t ch_prev) -> bool {
		return !ISCSYM(ch_prev) && ISCSYM(ch) && !wt_isdigit(ch);
	};

	auto get_identifier = [str, is_identifier, &states](int pos, int num_words) -> std::wstring {
		std::wstring id_str;

		static std::wstring braces(L"(){}[]<>");

		int len = (int)str.length();

		bool add_space = false;

		while (pos < len)
		{
			if (states[pos].HasAnyBitOfMask(chs_continuation | chs_comment_mask))
			{
				pos++;
				continue;
			}
			else if (states[pos].HasAnyBitOfMask(chs_string_mask))
				break;

			wchar_t ch = str[(uint)pos];

			if (is_identifier(ch))
			{
				if (add_space)
				{
					id_str += L' ';
					add_space = false;
				}

				id_str += ch;
				pos++;
			}
			else if (num_words && wt_isspace(ch))
			{
				if (!id_str.empty() && !add_space)
				{
					add_space = true;

					if (--num_words <= 0)
						break;
				}

				pos++;
			}
			else
				break;
		}

		return id_str;
	};

	auto get_identifier_rev = [str, is_identifier, &states](int pos) -> std::wstring {
		std::wstring id_str;

		while (pos >= 0)
		{
			if (states[pos].HasAnyBitOfMask(chs_continuation | chs_comment_mask))
			{
				pos--;
				continue;
			}
			else if (states[pos].HasAnyBitOfMask(chs_string_mask))
				break;

			wchar_t ch = str[(uint)pos];
			if (is_identifier(ch))
			{
				id_str += ch;
				pos--;
			}
			else
				break;
		}

		return std::wstring(id_str.rbegin(), id_str.rend());
	};

	auto is_modifier_or_case = [str, is_identifier, &states, &trim](int pos) {
		std::wstring id_str;
		bool has_space = false;

		while (pos >= 0)
		{
			if (states[pos].HasAnyBitOfMask(chs_continuation | chs_string_mask | chs_comment_mask))
			{
				pos--;
				continue;
			}

			wchar_t ch = str[(uint)pos];
			if (is_identifier(ch))
			{
				id_str += ch;
				pos--;
			}
			else if (wt_isspace(ch))
			{
				if (!id_str.empty() && ' ' != id_str.back())
				{
					if (!has_space)
					{
						has_space = true;
						id_str += ' ';
					}
					else
					{
						break;
					}
				}
				pos--;
			}
			else
				break;
		}

		if (!id_str.empty())
		{
			std::wstring identifier = trim(std::wstring(id_str.rbegin(), id_str.rend()));

			if (identifier == L"public" || identifier == L"private" || identifier == L"protected" ||
			    identifier == L"__published" || identifier == L"default" || identifier.find(L"case") == 0)
			{
				return true;
			}
		}

		return false;
	};

	auto resolve_cpp_lambda = [str, &states](CodeBlock& bl) mutable -> bool {
		auto filter = [&states](int pos, wchar_t& ch) -> bool {
			if (states[pos].HasAnyBitOfMask(chs_no_code))
				ch = ' '; // instead of comment/literal/continuation return space
			return true;
		};

		int lmb_start = str.find(
		    bl.top_pos, bl.open_pos - 1, [](wchar_t ch) { return ch == '['; }, filter);

		if (lmb_start >= 0)
		{
			auto read_stopper = [&bl](int pos, std::wstring& wstr) {
				return (!wstr.empty() && wstr.back() == '}') || pos >= bl.close_pos;
			};

			std::wstring code = ReadCode(str, states, lmb_start, read_stopper, false // no spaces
			                             ,
			                             false // no literals
			                             ,
			                             true // collapse braces
			);

			if (code.length() >= 4 && code[0] == '[' &&
			    code[1] == ']' && // [] part check
			                      // note: here could be a test for () however it is optional!!!
			    code[code.length() - 1] == '}' && code[code.length() - 2] == '{' // {} part check
			)
			{
				bl.top_pos = lmb_start;
				bl.type = CodeBlock::b_cpp_lambda;
				bl.ensure_name_and_type(states, false);
				// ResolveComments(block, str, states);
				return true;
			}
		}

		return false;
	};

	// method type is meant in a template
	// such as "void()" in "std::function<void()>"
	auto is_template_args = [&states, &block, &str](int pos) {
		auto filter = [&states](int _pos, wchar_t& ch) -> bool {
			if (states[_pos].HasAnyBitOfMask(chs_no_code))
				ch = ' '; // instead of comment/literal/continuation return space
			return true;
		};

		int close_pos = str.find_matching_brace(pos, filter);
		if (close_pos > block.close_pos)
		{
			int no_space = str.rfind_nospace(close_pos - 1, pos, filter);
			return no_space == block.close_pos;
		}

		return false;
	};

	auto block_has_valid_top_pos = [&]() {
		if (block.top_pos == -1)
			return true;

		if (block.top_pos < -1)
			return false;

		if (!states.Equals(block.top_pos, chs_none))
			return false;

		if (str.is_space(block.top_pos))
			return false;

		return true;
	};

	std::stack<wchar_t> stack;
	std::wstring stoppers;

	stoppers.append(L",;{}");

	if (block.open_char == '(')
		stoppers.append(L"()");
	else if (block.open_char == '[')
		stoppers.append(L"=()[]");

	auto filter = str.make_char_filter(states, chs_any);

	wchar_t ch = 0, ch_prev = 0, ch_next = 0;
	int eq_pos = -1;
	bool first_significant = false;

	for (int i = block.open_pos - 1; i >= 0; i--)
	{
		auto chs = states[i];

		if (stack.empty() && chs.HasAnyBitOfMask(chs_string_mask))
		{
			block.top_pos = str.find_nospace(i + 1, block.open_pos - 1, filter);

			_ASSERTE(block_has_valid_top_pos());

			break;
		}
		else if (chs.state == chs_none)
		{
			ch = str.safe_at(i, filter);
			ch_prev = str.safe_at(i - 1, filter);
			ch_next = str.safe_at(i + 1, filter);

			if (!wt_isspace(ch) && !first_significant)
			{
				first_significant = true;

				if (block.open_char == '(' && (ch == '+' || ch == '-' || ch == '/'))
				{
					return block;
				}
			}

			if (stack.empty() && ch == '=' && ch_next != '>')
			{
				eq_pos = i;
			}

			if (stack.empty() &&
			    (stoppers.find(ch) != std::wstring::npos ||
			     (
			         // C# also supports "::" operator (with similar meaning)!
			         // https://msdn.microsoft.com/en-us/library/c3ay4x3d.aspx
			         /*!is_cs && */ ch == ':' && ch_prev != ':' && ch_next != ':' && // don't affect :: operator
			         (is_modifier_or_case(i - 1) || // modifiers like "public:", "private:" or "case ##:"
			          block.open_char == '{'        // initializer in constructor ?
			          )) ||
			     (
			         // check for function type as template argument
			         // such as "void()" in "std::function<void()>"
			         !is_cs && ch == '<' && block.open_char == '(' && is_template_args(i))))
			{
				block.top_pos = str.find_nospace(i + 1, block.open_pos - 1, filter);

				if (block.top_pos >= 0 && ch == ',' && !is_cs && resolve_cpp_lambda(block))
					return block;

				_ASSERTE(block_has_valid_top_pos());

				break;
			}
			else if (closing_id(i, ch) >= 0)
				stack.push(ch);
			else if (opening_id(i, ch) >= 0)
			{
				if (!stack.empty() && is_closing_of(ch, stack.top()))
				{
					stack.pop();
				}
				else
				{
					// This is brace mismatch!!!
					// It usually occurs when we are resolving inner scope,
					// such as {} inside of arguments, or something like that.
					// Mainly c++ tr1 lambda in args or c++ tr11 initializer in args
					block.top_pos = str.find_nospace(i + 1, block.open_pos - 1, filter);

					if (block.top_pos >= 0 && !is_cs && resolve_cpp_lambda(block))
						return block;

					_ASSERTE(block_has_valid_top_pos());

					break; // stop the loop
				}
			}
			else if (stack.empty() && is_identifier_start(ch, ch_prev))
			{
				if (block.top_pos >= 0)
					block.sub_starts.insert(block.top_pos);

				block.top_pos = i;

				std::wstring id_str = get_identifier(i, 1);
				if (id_str == L"else" && block.open_char != '{')
				{
					// This may be an invalid state:
					// "else" must be either "else {...}" or "else ...;"
					// It is valid only in case of "else if".
					// Solution: Crop out "else" keyword

					int n_pos = str.find_nospace(i + 4, block.open_pos - 1, filter);

					if (n_pos != -1)
					{
						id_str = get_identifier(n_pos, 1);
						if (id_str != L"if")
						{
							// if next keyword is not "if",
							// crop else keyword away from block
							block.top_pos = n_pos;
						}
					}

					_ASSERTE(block_has_valid_top_pos());

					break;
				}
				else if (id_str == L"else" || (!is_cs && id_str == L"_asm") || (!is_cs && id_str == L"__asm") ||
				         id_str == L"try" || id_str == L"catch" || (is_cs && id_str == L"finally") ||
				         (!is_cs && id_str == L"__try") || (!is_cs && id_str == L"__except") ||
				         (!is_cs && id_str == L"__finally") || id_str == L"for" || id_str == L"switch" ||
				         id_str == L"do" || id_str == L"while" || (is_cs && id_str == L"foreach") ||
				         (is_cs && id_str == L"using") || (is_cs && id_str == L"lock") ||
				         (is_cs && id_str == L"unsafe") // not sure if this will not cause incomplete methods (but
				                                        // should not due to import from VA Outline)
				)
				{
					_ASSERTE(block_has_valid_top_pos());
					break;
				}
				else if (id_str == L"if")
					stoppers.append(L"()");
			}
		}
	}

	// This is not necessary, but block is easier to understand
	// if its top position is not set, rather than same as open.
	if (block.top_pos == block.open_pos)
		block.top_pos = -1;

	// Try to find the semicolon of the block
	for (int x = block.close_pos + 1; x < (int)str.length(); x++)
	{
		if (states[x].state == chs_none)
		{
			wchar_t xch = str[(uint)x];
			if (!iswspace(xch))
			{
				if (xch == ';')
					block.semicolon_pos = x;
				break;
			}
		}
	}

	if (eq_pos != -1)
	{
		wchar_t next_ch = str.safe_at(eq_pos + 1);
		if (ISCSYM(next_ch) || wt_isspace(next_ch))
			block.splitters.insert(std::make_tuple(eq_pos, 1));
		else if (eq_pos > 0 && str.is_one_of(eq_pos - 1, L"+-*/%&^|"))
			block.splitters.insert(std::make_tuple(eq_pos - 1, 2));
		else
			block.splitters.insert(std::make_tuple(eq_pos, 1));
	}

	return block;
}

void Parser::BraceBlocksFlood(SharedWStr str, CodeBlock::CharStates& states,
                              std::function<void(int, CodeBlock::Ptr)> inserter,
                              std::function<bool(wchar_t, int)> ang_filter, bool is_cs, int pos, bool outer_blocks,
                              int level /*= 1*/, LPCSTR braces /*= "(){}[]<>"*/)
{
#ifdef _DEBUG
	auto _auto_fname = CodeBlock::PushFunctionName(__FUNCTION__);
#endif

	typedef BraceCounter bc_t;

	if (!inserter)
	{
		_ASSERTE(!"Inserter is empty!");
		return;
	}

	int str_length = (int)str.length();

	if (pos < 0 || pos >= str_length)
	{
		return;
	}

#ifdef _SSPARSE_DEBUG
	std::vector<std::pair<wchar_t, CharState>> dbg_states(str_length);
	for (size_t i = 0; i < str.length(); i++)
		dbg_states[i] = std::pair<wchar_t, CharState>(str[i], states[i].state);
#endif

	CharState pos_state = (CharState)(states[pos].state & chs_boundary_mask);

	SS_DBG(std::wstringstream log);

	auto approve_state = [pos_state](CharState chs) {
		if ((chs & pos_state) == pos_state)
			return BraceCounter::StateValid;
		else
			return BraceCounter::StateInvalid_Stop;
	};

	auto approve_state_default = [](CharState chs) {
		if (chs != chs_none)
			return BraceCounter::StateInvalid_Continue;

		return BraceCounter::StateValid;
	};

	BraceCounter L_bc("(){}[]", true);
	BraceCounter R_bc("(){}[]", false);
	L_bc.ang_filter = ang_filter;
	L_bc.set_ignore_angle_brackets(false);
	R_bc.ang_filter = ang_filter;
	R_bc.set_ignore_angle_brackets(false);
	int L = pos, R = pos;

	if (pos_state != chs_none)
	{
		L_bc.ApproveState = approve_state;
		R_bc.ApproveState = approve_state;
	}
	else
	{
		L_bc.ApproveState = approve_state_default;
		R_bc.ApproveState = approve_state_default;
	}

	wchar_t caret_char = str.safe_at(pos);
	if (!ang_filter(caret_char, pos))
		caret_char = ' ';

	switch (caret_char)
	{
	case '(':
	case '{':
	case '[':
	case '<':
		R++;
		break;

	case ')':
	case '}':
	case ']':
	case '>':
		L--;
		break;

	default: // by default, left counter starts left from caret
		if (str.is_one_of(pos - 1, L"({[<"))
			R++; // [case: 98729] move right counter if left from caret is opening brace
		else
			L--;
	}

	if (outer_blocks)
	{
		// don't use IF_SS_PARALLEL on mtxP as it may be then
		// missing for capturing groups in following lambdas.
		std::shared_ptr<std::mutex> mtxP(new std::mutex());

		L_bc.on_block = [&inserter, str, mtxP](char type, int open_pos, int close_pos) {
			IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(*mtxP));
			inserter(-1, CodeBlock::Ptr(new CodeBlock(str, (wchar_t)type, -1, open_pos, close_pos)));
		};

		R_bc.on_block = [&inserter, str, mtxP](char type, int open_pos, int close_pos) {
			IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(*mtxP));
			inserter(1, CodeBlock::Ptr(new CodeBlock(str, (wchar_t)type, -1, open_pos, close_pos)));
		};
	}

	int max_iter_count = str_length * 5;

	for (int lvl = 0; (lvl < level || level < 0) && (L >= 0 || R < str_length); lvl++)
	{
		SS_DBG(log << L"level: " << lvl << std::endl);

		bool L_continue = true, R_continue = true;

		L_bc.reset();
		R_bc.reset();

		auto L_update = [&]() {
			L_continue = bc_t::is_valid_pos(L, str_length) && !L_bc.is_outOfTextRange() && !L_bc.is_mismatch() &&
			             !L_bc.is_wrongCharState();

			if (L_continue)
			{
				L_bc.on_char(str[(uint)L], L, &states);
				L_continue = L_bc.next_valid_pos(L, str_length, &states);
			}
		};

		auto R_update = [&]() {
			R_continue = bc_t::is_valid_pos(R, str_length) && !R_bc.is_outOfTextRange() && !R_bc.is_mismatch() &&
			             !R_bc.is_wrongCharState();

			if (R_continue)
			{
				R_bc.on_char(str[(uint)R], R, &states);
				R_continue = R_bc.next_valid_pos(R, str_length, &states);
			}
		};

		do
		{
			if (0 == --max_iter_count)
			{
				_ASSERTE(!"Too many iterations!");
				return;
			}

#ifdef SS_PARALLEL
			concurrency::parallel_invoke(L_update, R_update);
#else
			L_update();
			R_update();
#endif // SS_PARALLEL

			SS_DBG(log << "L: ");
			SS_DBG(L_bc.debug_print(log));
			SS_DBG(log << std::endl);

			SS_DBG(log << "R: ");
			SS_DBG(R_bc.debug_print(log));
			SS_DBG(log << std::endl);

			SS_DBG(log << std::endl);

			if (L_bc.is_wrongCharState() && R_bc.is_wrongCharState())
			{
				SS_DBG(log << "CROSSED STATE BOUNDARIES" << std::endl << std::endl);

				if (str.is_valid_pos(R) && str.is_valid_pos(L))
				{
					CharState L_state = (CharState)(states[L].state & chs_boundary_mask);
					CharState R_state = (CharState)(states[R].state & chs_boundary_mask);
					if (L_state == R_state && L_state != pos_state)
						pos_state = L_state;
				}

				if (pos_state != chs_none)
				{
					L_bc.ApproveState = approve_state;
					R_bc.ApproveState = approve_state;
				}
				else
				{
					L_bc.ApproveState = approve_state_default;
					R_bc.ApproveState = approve_state_default;
				}

				L_bc.reset();
				R_bc.reset();

				L_continue = R_continue = true;
			}
			else if (L_bc.is_mismatch() && R_bc.is_mismatch())
			{
				SS_DBG(log << "Compare: L: " << L_bc.get_char() << ", R: " << R_bc.get_char() << std::endl);

				if (bc_t::is_open_close(L_bc.get_char(), R_bc.get_char()))
				{
					SS_DBG(log << "SUCCESS" << std::endl << std::endl);

					CodeBlock::Ptr ptr(
					    new CodeBlock(str, (wchar_t)L_bc.get_char(), -1, L_bc.get_pos(), R_bc.get_pos()));

					SS_DBG(Parser::ResolveBlock(str, states, *ptr, is_cs, ang_filter));
					SS_DBG(ptr->debug_print(states, log, is_cs));

					inserter(0, ptr);

					break; // do {} while;
				}

				if (!ang_filter)
				{
					//////////////////////////////////////////////////////////////
					// did we just consume some operator like < > <= >= => -> ?

					if (!L_bc.is_operator_precondition() && !R_bc.is_operator_precondition())
					{
						// no, this is normal mismatch
						SS_DBG(log << "NORMAL MISMATCH" << std::endl << std::endl);

#ifdef _SSPARSE_DEBUG
						goto go_away; // normal mismatch - go away (but allow results)
#else
						return; // mismatch - go away
#endif
					}

					if (L_bc.is_operator_precondition())
					{
						SS_DBG(log << "RESETING LEFT due to < > containing operator precondition" << std::endl
						           << std::endl);

						L_bc.reset();
						L_continue = true;
					}

					if (R_bc.is_operator_precondition())
					{
						SS_DBG(log << "RESETING RIGHT due to < > containing operator precondition" << std::endl
						           << std::endl);

						R_bc.reset();
						R_continue = true;
					}
				}
			}
		} while (L_continue || R_continue);
	}

#ifdef _SSPARSE_DEBUG
go_away:
	std::wstring str_log = log.str();
	int _break_point = 0;
#endif // _SSPARSE_DEBUG
}

void Parser::StatementBlocks(SharedWStr str, CodeBlock::CharStates& states,
                             std::function<void(CodeBlock::Ptr)> inserter, std::function<int(int)> brace_finder,
                             const CodeBlock& block, int caret_pos, bool is_cs)
{
#ifdef _DEBUG
	auto _auto_fname = CodeBlock::PushFunctionName(__FUNCTION__);
#endif

	auto is_identifier = [](wchar_t ch) -> bool { return ISCSYM(ch); };

	auto is_identifier_start = [](wchar_t ch, wchar_t ch_prev) -> bool {
		return !ISCSYM(ch_prev) && ISCSYM(ch) && !wt_isdigit(ch);
	};

	auto get_identifier = [str, is_identifier, is_cs](int& pos, bool allow_space) -> std::wstring {
		std::wstring id_str;

		int len = (int)str.length();
		while (pos < len)
		{
			wchar_t ch = str[(uint)pos];
			if (is_identifier(ch))
				id_str += str[uint(pos++)];
			else if (allow_space && iswspace(ch))
			{
				if (!id_str.empty() && !iswspace(id_str.back()))
					id_str += ' ';
				pos++;
			}
			else if (!is_cs && ch == '\\')
			{
				int lcl = str.get_line_continuation_length(pos);
				if (lcl == 0)
					break;
				pos += lcl;
				continue;
			}
			else
			{
				break;
			}
		}

		return id_str;
	};

	auto move_to_end_of_identifier = [str, is_identifier, is_cs](int& pos, int pos_limit, bool allow_space) {
		while (pos < pos_limit)
		{
			wchar_t ch = str[(uint)pos];

			if (is_identifier(ch))
				pos++;
			else if (allow_space && iswspace(ch))
				pos++;
			else if (!is_cs && ch == '\\')
			{
				int lcl = str.get_line_continuation_length(pos);
				if (lcl == 0)
					break;
				pos += lcl;
				continue;
			}
			else
				break;
		}

		if (pos > pos_limit)
			pos = pos_limit;
		else if (pos < pos_limit)
			pos--;
	};

	auto code_filter = [&](int pos, wchar_t& ch) -> bool {
		auto state = states[pos];

		if (state.HasAnyBitOfMask((CharState)(chs_no_code | chs_directive)))
		{
			if (state.Contains(chs_directive))
			{
				ch = '#';
				return true;
			}
			else if (state.HasAnyBitOfMask(chs_string_mask))
			{
				if (ch == '"' || ch == '\'')
					return true;

				if (state.Contains(chs_verbatim_str))
					ch = '\'';
				else
					ch = '"';

				return true;
			}

			ch = ' '; // instead of comment/literal/continuation return space
		}

		return true;
	};

	BraceCounter bc;
	bc.set_ignore_angle_brackets(false);
	bc.ang_filter = [&](wchar_t ch, int pos) -> bool { return (ch == L'<' || ch == L'>') && brace_finder(pos) != -1; };

	//	bool do_continue = true;
	int start = -1;
	int full_stmt_start = -1;
	int full_stmt_depth = -1;

	std::wstring allowed_opens = L"({[<";

	bc.on_close = [&](char ch, int pos) {
		if (start >= 0)
		{
			wchar_t open_ch = str[(uint)start];
			if (open_ch && allowed_opens.find(open_ch) == std::string::npos)
				open_ch = '\0';

			inserter(CodeBlock::Ptr(new CodeBlock(str, open_ch, start, pos)));
		}

		if (ch == '}' && full_stmt_depth == bc.depth())
		{
			bool reset_full_stmt = true;

			int nospace = str.find_nospace(pos + 1, code_filter);
			if (nospace != -1 && str.is_identifier_start(nospace))
			{
				auto id = str.get_symbol(nospace);

				if (id == L"while")
					reset_full_stmt = false;
			}

			if (reset_full_stmt)
			{
				int virt_s = str.find_virtual_start(full_stmt_start, true);
				if (caret_pos >= virt_s)
				{
					int virt_e = str.find_virtual_end(pos, true);
					if (caret_pos <= virt_e)
					{
						int open_pos = brace_finder(pos);

						auto b = CodeBlock::Ptr(new CodeBlock(CodeBlock::b_full_stmt, str, str.safe_at(open_pos),
						                                      full_stmt_start, open_pos, pos,
						                                      str.safe_at(nospace) == ';' ? nospace : -1));

						b->virtual_start = virt_s;
						b->virtual_end = virt_e;
						b->virtual_resolved = true;

						inserter(b);
					}
				}

				full_stmt_start = -1;
				full_stmt_depth = -1;
			}
		}

		start = -1;
	};

	bc.on_open = [&start](char ch, int pos) { start = -1; };

#ifdef _SSPARSE_DEBUG
	std::wostringstream ss;
#endif

	int start_pos = block.open_pos;
	int end_pos = block.close_pos;

	if (str.is_one_of(start_pos, L"([{"))
	{
		start_pos++;
		if (str.is_one_of(end_pos, L")]}"))
		{
			end_pos--;
		}
	}

	CharStatePosIter<1, 1> csp(states);
	for (int pos = start_pos; pos <= end_pos; pos++)
	{
		if (states[pos] != chs_none)
			continue;

		csp.init(pos);
		wchar_t ch = str[(uint)pos];

		if (wt_isspace(ch))
			continue;

		bc.on_char(ch, pos, &states);

#ifdef _SSPARSE_DEBUG
		bc.debug_print(ss);
		ss << std::endl;
#endif

		if (!bc.is_stack_empty())
			continue;

		wchar_t ch_prev = str.safe_at(csp.prev());
		wchar_t ch_next = str.safe_at(csp.next());

		struct handle_full_stmt
		{
			wchar_t _ch;
			int& fss;
			int& fsd;

			handle_full_stmt(wchar_t c, int& full_start, int& full_depth) : _ch(c), fss(full_start), fsd(full_depth)
			{
			}

			~handle_full_stmt()
			{
				if (fss != -1 && _ch == ';')
				{
					fss = -1;
					fsd = -1;
				}
			}
		} _hfs(ch, full_stmt_start, full_stmt_depth);

		if (start == -1 && is_identifier_start(ch, ch_prev))
		{
			if (ch == 'e' && ch_next == 'l' && bc.is_none_or_closed()) // handle special case for 'else'
			{
				// this is needed if else is alone before anything else then "if"
				// for example "else for(...)" and so on...

				int new_pos = pos;
				std::wstring elseStr = get_identifier(new_pos, true);
				if (wcsncmp(elseStr.c_str(), L"else", 4) == 0 && wcsncmp(elseStr.c_str(), L"else if", 7) != 0 &&
				    (int)str.length() > new_pos && str[(uint)new_pos] != '{')
				{
					inserter(CodeBlock::Ptr(new CodeBlock(CodeBlock::b_else, str, '\0', pos, pos + 3)));
					pos += 3;
					start = -1;
					full_stmt_start = -1;
					full_stmt_depth = -1;
					continue;
				}
			}
			else
			{
				if (full_stmt_start == -1)
				{
					full_stmt_start = pos;
					full_stmt_depth = bc.depth();
				}

				start = pos;
				move_to_end_of_identifier(pos, block.close_pos, false);
				continue;
			}
		}

		if (start >= 0)
		{
			// test for _asm and __asm
			// note: in VC++ _asm and __asm are statement separators
			if (!is_cs && ch == '_' && bc.is_none_or_closed())
			{
				int tmp_pos = pos;
				std::wstring idstr = get_identifier(tmp_pos, false);
				if (idstr == L"_asm" || idstr == L"__asm")
				{
					// find the end of previous statement
					int prev_e_pos = pos - 1;
					while (prev_e_pos > 0 && (states[prev_e_pos].HasAnyBitOfMask(chs_comment_mask) ||
					                          wt_isspace(str.safe_at(prev_e_pos))))
						prev_e_pos--;

					inserter(CodeBlock::Ptr(new CodeBlock(str, '\0', start, prev_e_pos)));

					start = full_stmt_start = pos;
					full_stmt_depth = bc.depth();
					pos = tmp_pos - 1; // for loop re-increments by 1
					continue;
				}
			}

			// standard "...;" or "...," statements
			// note: params as "(..., ..., ...)" are not considered as statements
			else if (ch == ';')
			{
				inserter(CodeBlock::Ptr(new CodeBlock(CodeBlock::b_undefined, str, ';', start, -1, -1, pos)));

				int virt_s = str.find_virtual_start(full_stmt_start, true);
				if (caret_pos >= virt_s)
				{
					int virt_e = str.find_virtual_end(pos, true);
					if (caret_pos <= virt_e)
					{
						auto b = CodeBlock::Ptr(new CodeBlock(str, ';', full_stmt_start, pos));
						b->type = CodeBlock::b_full_stmt;
						b->virtual_start = virt_s;
						b->virtual_end = virt_e;
						b->virtual_resolved = true;
						inserter(b);
					}
				}

				full_stmt_start = -1;
				full_stmt_depth = -1;
				start = -1;
			}

			// these are items separated by comma,
			// mainly within enums and initializers
			// [case: 86905] do not include comma in range
			else if (bc.is_none_or_closed() && ch == ',')
			{
				auto fltr = str.make_char_filter(states);

				bool stop_on_this_comma = true;

				if (bc.m_angs.size())
				{
					for (int ang : bc.m_angs)
					{
						if (str[(uint)ang] == '<')
						{
							int closing = brace_finder(ang);
							if (closing > pos)
							{
								stop_on_this_comma = false;
								break;
							}
						}
					}
				}

				if (stop_on_this_comma)
				{
					int epos = str.rfind_nospace(pos - 1, fltr);

					if (epos >= start)
						inserter(CodeBlock::Ptr(new CodeBlock(str, ',', start, epos)));

					start = -1;
				}
			}

			// such as "public:", "protected:" etc.
			// also handles "case ###:" statements,
			// but ignores "::" operator
			else if (ch == ':')
			{
				if (ch_next == ':') // skip the :: operator
				{
					pos++;
					continue;
				}

				inserter(CodeBlock::Ptr(new CodeBlock(str, ':', start, pos)));
				start = -1;
				full_stmt_start = -1;
				full_stmt_depth = -1;
			}
		}
		else if (full_stmt_start != -1 && ch == ';')
		{
			int virt_s = str.find_virtual_start(full_stmt_start, true);
			if (caret_pos >= virt_s)
			{
				int virt_e = str.find_virtual_end(pos, true);
				if (caret_pos <= virt_e)
				{
					auto b = CodeBlock::Ptr(new CodeBlock(str, ';', full_stmt_start, pos));
					b->type = CodeBlock::b_full_stmt;
					b->virtual_start = virt_s;
					b->virtual_end = virt_e;
					b->virtual_resolved = true;
					inserter(b);
				}
			}

			full_stmt_start = -1;
			full_stmt_depth = -1;
		}
	}

#ifdef _SSPARSE_DEBUG
	std::wstring log = ss.str();
	int _break_point = 0;
#endif
}

void Parser::ResolveComments(CodeBlock& block, SharedWStr str, CodeBlock::CharStates& states,
                             bool allow_comma /*= false*/)
{
	if (block.comments_resolved)
		return;

	// don't resolve comments of blocks within non-code parts
	if ((block.start_pos() >= 0 && states[block.start_pos()].HasAnyBitOfMask(chs_no_code)) ||
	    (block.end_pos() >= 0 && states[block.end_pos()].HasAnyBitOfMask(chs_no_code)))
	{
		block.comments_resolved = true;
		return;
	}

	auto resolve_prefix = [&]() {
		////////////////////////////////////////////////////////
		// Prefix comment: /*...*/ and //...
		//
		int comment_start = -1, comment_end = -1, stopper = -1;
		for (int i = block.start_pos() - 1; str.is_valid_pos(i); i--)
		{
			if (states[i].HasAnyBitOfMask(chs_comment_mask))
			{
				if (comment_end == -1)
					comment_end = i;

				comment_start = i;
			}
			else
			{
				wchar_t ch = str[(uint)i];

				if (!wt_isspace(ch))
				{
					stopper = i;
					break;
				}
			}
		}

		if (stopper >= 0 && comment_start >= 0 &&

		    // if comment is on same line as stopper,
		    // it means that comment relates to stopper, not to block being resolved
		    str.line_start(stopper, true) == str.line_start(comment_start, true))
		{
			// Now we want to skip all comment related to stopper,
			// to do that, we find closest sibling comment block.

			// Get a type of comment for comment start.
			// Assume, that stopper related comment is of one type.
			// If line comment, only first line will be related to stopper
			DWORD state = DWORD(states[comment_start].state & chs_comment_mask);

			// find first char not of same state as comment_start
			// Note: line-breaks are w/o state if in // inline comments,
			//       so this nicely breaks on linebreak, while C comments
			//       are skipped including all line-breaks
			for (; comment_start <= comment_end; comment_start++)
				if (!states[comment_start].HasAnyBitOfMask(state))
					break;

			// find first char of our block comment
			for (; comment_start <= comment_end; comment_start++)
				if (states[comment_start].HasAnyBitOfMask(chs_comment_mask))
					break;
		}

		// if we have comment...
		if (comment_start >= 0 && comment_end > comment_start)
		{
			block.prefix_comment.reset(new CodeBlock(CodeBlock::b_comment, str, L'*', comment_start, comment_end));
		}
	};

	auto resolve_suffix = [&]() {
		////////////////////////////////////////////////////////
		// Suffix comment: /*...*/ and //...
		//

		// first we need to check, if there is any code on this line
		// which will suggest whether comment on right relates to
		// current block or to its next sibling
		int comment_start = -1, comment_end = -1, stopper = -1;

		stopper = str.find(block.end_pos() + 1, [](wchar_t ch) { return ch == '\r' || ch == '\n' || !wt_isspace(ch); });

		if (stopper >= 0)
		{
			if (!str.is_one_of(stopper, L"\r\n"))
			{
				for (int i = stopper; str.is_valid_pos(i); i++)
				{
					if (states[i].HasAnyBitOfMask(chs_comment_mask))
					{
						if (comment_start == -1)
							comment_start = i;

						comment_end = i;
					}
					else if (!str.is_space(i) || str.is_on_line_end(i, true))
					{
						if (allow_comma && str[(uint)i] == ',')
							continue;

						stopper = i;
						break;
					}
				}

				if (comment_start >= 0 && comment_end > comment_start)
				{
					block.suffix_comment.reset(
					    new CodeBlock(CodeBlock::b_comment, str, L'*', comment_start, comment_end));
				}
			}
		}
	};

#ifdef SS_PARALLEL
	concurrency::parallel_invoke(resolve_prefix, resolve_suffix);
#else
	resolve_prefix();
	resolve_suffix();
#endif // SS_PARALLEL

	block.comments_resolved = true;
}

std::pair<std::wstring, std::vector<int>> Parser::ReadCodeEx(SharedWStr str, const CodeBlock::CharStates& states,
                                                             int start,
                                                             std::function<bool(int pos, std::wstring& wstr)> stopper,
                                                             bool spaces /*= true*/, bool literals /*= true*/,
                                                             bool collapse_braces /*= true*/, int step /*= 1*/,
                                                             LPCWSTR braces /*= L"({[<"*/)
{
	std::wstring br_str = braces ? braces : L"({[<";
	std::wstring wstr;
	std::vector<int> pvec;

	if (!stopper || !step)
		return std::make_pair(wstr, pvec);

	auto code_filter = str.make_char_filter(states);

	for (int i = start; str.is_valid_pos(i); i += step)
	{
		if (!states[i].HasAnyBitOfMask(chs_no_code))
		{
			wchar_t ch = str[(uint)i];
			if (wt_isspace(ch))
			{
				if (spaces && !wstr.empty() && wstr.back() != ' ')
				{
					wstr += ' ';
					pvec.push_back(i);
				}
			}
			else if (collapse_braces && br_str.find(ch) != std::wstring::npos)
			{
				wstr += ch;
				pvec.push_back(i);

				int matching = str.find_matching_brace(i, code_filter);
				if (matching >= 0)
				{
					i = matching;
					wstr += str[(uint)i];
					pvec.push_back(i);
				}
			}
			else
			{
				wstr += ch;
				pvec.push_back(i);
			}
		}
		else if (literals && states[i].HasAnyBitOfMask(chs_string_mask))
		{
			if (collapse_braces)
			{
				if (i > 0 && !states[i - 1].HasAnyBitOfMask(chs_string_mask))
				{
					wstr += L'"';
					pvec.push_back(i);
				}
				else if (i + 1 < (int)states.size() && !states[i + 1].HasAnyBitOfMask(chs_string_mask))
				{
					wstr += L'"';
					pvec.push_back(i);
				}
			}
			else
			{
				wchar_t ch = str[(uint)i];
				if (wt_isspace(ch))
				{
					if (spaces && !wstr.empty() && wstr.back() != ' ')
					{
						wstr += ' ';
						pvec.push_back(i);
					}
				}
				else
				{
					wstr += ch;
					pvec.push_back(i);
				}
			}
		}

		if (stopper(i, wstr))
			break;
	}

	if (step < 0)
	{
		std::reverse(wstr.begin(), wstr.end());
		std::reverse(pvec.begin(), pvec.end());
	}

	return std::make_pair(wstr, pvec);
}

std::wstring Parser::ReadCode(SharedWStr str, const CodeBlock::CharStates& states, int start,
                              std::function<bool(int pos, std::wstring& wstr)> stopper, bool spaces /*= true*/,
                              bool literals /*= true*/, bool collapse_braces /*= true*/, int step /*= 1*/,
                              LPCWSTR braces /*= L"({[<"*/)
{
	auto rslt = ReadCodeEx(str, states, start, stopper, spaces, literals, collapse_braces, step, braces);
	return rslt.first;
}

bool BraceCounter::on_char(wchar_t ch, int pos, const CodeBlock::CharStates* states)
{
	typedef BraceCounter mType;

	m_char = ch;
	m_pos = pos;

	if (states && ApproveState)
	{
		StateStatus ss = ApproveState(states->at(pos));
		if (ss != BraceCounter::StateValid)
		{
			set_state(mType::WrongCharState);
			return false;
		}
	}

	bool ret_val = false;

	if (ch < 0x80)
	{
		if (ch == (m_backward ? '>' : '<'))
		{
			if (!ang_filter || ang_filter(ch, pos))
			{
				m_angs.push_back(pos);
				if (!m_ignore_angs)
				{
					if (on_open)
						on_open((char)ch, pos);

					set_state(mType::Open);
				}
			}
		}
		else if (ch == (m_backward ? '<' : '>'))
		{
			if (!ang_filter || ang_filter(ch, pos))
			{
				// this is closing angle brace,
				// so if m_angs is already empty,
				// we are in mismatch
				bool ang_mismatch = m_angs.empty();

				if (!m_angs.empty())
					m_angs.pop_back();

				if (!m_ignore_angs && m_stack.empty())
				{
					if (ang_mismatch)
					{
						set_state(mType::Mismatch);
						ret_val = true;
					}
					else if (m_angs.empty())
					{
						set_state(mType::Closed);

						if (on_close)
							on_close((char)ch, pos);
					}
				}
			}
		}
		else if (open_index(ch) >= 0)
		{
			set_state(mType::Open);

			if (on_open)
				on_open((char)ch, pos);

			m_stack.push_back(pos_char(pos, (char)ch));
		}
		else if (close_index(ch) >= 0)
		{
			if (!m_stack.empty() && is_match((wchar_t)m_stack.back().ch, ch))
			{
				pos_char top = m_stack.back();
				m_stack.pop_back();

				char open_char = m_backward ? (char)ch : top.ch;
				int open_pos = m_backward ? pos : top.pos;
				int close_pos = m_backward ? top.pos : pos;

				// we are on a closing brace so remove all pending opening angle braces
				// which are within closed range from the stack as those are mismatched
				for (int i = (int)m_angs.size() - 1; i >= 0 && m_angs[(uint)i] > open_pos; i--)
					m_angs.pop_back();

				if (m_stack.empty() && (m_ignore_angs || m_angs.empty()))
					set_state(mType::Closed);

				if (on_close)
					on_close((char)ch, pos);

				if (on_block)
					on_block(open_char, open_pos, close_pos);
			}
			else if (m_stack.empty())
			{
				set_state(mType::Mismatch);
				ret_val = true;
			}
		}
		else if (!m_angs.empty() && reset_angs)
		{
			int num_reset = reset_angs(pos, ch);
			if (num_reset)
			{
				if (num_reset < 0 || (int)m_angs.size() == num_reset)
					m_angs.clear();
				else if ((int)m_angs.size() > num_reset)
					m_angs.resize(m_angs.size() - (uint)num_reset);

				if (!m_ignore_angs && m_stack.empty() && m_angs.empty())
					set_state(BraceCounter::None);
			}
		}
	}
	else if (!m_angs.empty() && reset_angs)
	{
		int num_reset = reset_angs(pos, ch);
		if (num_reset)
		{
			if (num_reset < 0 || (int)m_angs.size() == num_reset)
				m_angs.clear();
			else if ((int)m_angs.size() > num_reset)
				m_angs.resize(m_angs.size() - (uint)num_reset);

			if (!m_ignore_angs && m_stack.empty() && m_angs.empty())
				set_state(BraceCounter::None);
		}
	}

	return ret_val;
}

BraceCounter::BraceCounter(LPCSTR braces /*= "(){}[]"*/, bool reversed /*= false*/)
    : m_braces(braces), m_backward(reversed)
{
	reset_angs = [](int pos, wchar_t ch) {
		if (ch == ';')
			return -1; // reset all angle braces

		return 0;
	};
}

bool BraceCounter::is_match(wchar_t ch_open, wchar_t ch_close)
{
	int open_id = open_index(ch_open);
	return open_id >= 0 && open_id == close_index(ch_close);
}

int BraceCounter::close_index(wchar_t ch)
{
	return brace_index(ch, m_backward ? 0 : 1);
}

int BraceCounter::open_index(wchar_t ch)
{
	return brace_index(ch, m_backward ? 1 : 0);
}

int BraceCounter::brace_index(wchar_t ch, int start)
{
	if (ch < 0x80)
	{
		for (size_t i = (uint)start; i < m_braces.length(); i += 2)
			if (m_braces[i] == ch)
				return int(i - start);
	}
	return -1;
}

bool BraceCounter::is_valid_pos(int pos, int str_len)
{
	return pos >= 0 && pos < str_len;
}

template <typename TOstream> static void debug_print_BraceCounter(BraceCounter& bc, TOstream& ss)
{
	typedef BraceCounter t;

	std::left(ss);

	ss << "state: ";

	ss << std::setw(14);
	if (bc.is_closed())
		ss << "CLOSED";
	else if (bc.is_open())
		ss << "OPEN";
	else if (bc.is_mismatch())
		ss << "MISMATCH";
	else if (bc.is_wrongCharState())
		ss << "WRONG CHARS";
	else if (bc.is_outOfTextRange())
		ss << "OUT OF RANGE";
	else
		ss << "NONE";

	ss << ", ch: ";
	ss << std::setw(2);
	if (bc.get_char() == '\n')
		ss << "\\n";
	else if (bc.get_char() == '\r')
		ss << "\\r";
	else if (bc.get_char() == '\t')
		ss << "\\t";
	else if (bc.get_char() == '\0')
		ss << "\\0";
	else if (bc.get_char() == ' ')
		ss << "\\s";
	else
		ss << (char)bc.get_char();

	ss << ", pos: ";
	std::right(ss);
	ss << std::setw(8);
	ss << bc.get_pos();

	std::left(ss);
	ss << ", stack: ";
	{
		std::ostringstream oss;
		for (auto pch : bc.m_stack)
			oss << pch.ch;
		ss << std::setw(10);
		ss << oss.str().c_str();
	}

	ss << ", <>: ";
	ss << std::setw(3);
	ss << bc.m_angs.size();
}

void ___SSDbgTraceW(LPCWSTR lpszFormat, ...)
{
#ifdef _SSPARSE_DEBUG
	CStringW trace_str;
	va_list argList;
	va_start(argList, lpszFormat);
	trace_str.FormatV(lpszFormat, argList);
	va_end(argList);
	// TraceHelp th((LPCWSTR)trace_str);
	trace_str.Insert(0, L"#SSDBG ");
	::OutputDebugStringW((LPCWSTR)trace_str);
#endif
}

void ___SSDbgTraceA(LPCSTR lpszFormat, ...)
{
#ifdef _SSPARSE_DEBUG
	CStringA trace_str;
	va_list argList;
	va_start(argList, lpszFormat);
	trace_str.FormatV(lpszFormat, argList);
	va_end(argList);
	// TraceHelp th((LPCSTR)trace_str);

	trace_str.Insert(0, "#SSDBG ");
	::OutputDebugStringA((LPCSTR)trace_str);
#endif
}

#define _SSDbgTraceW(wformat, ...)                                                                                     \
	if (false)                                                                                                         \
		_snwprintf_s(nullptr, 0, 0, wformat, __VA_ARGS__);                                                             \
	else                                                                                                               \
		___SSDbgTraceW(format, __VA_ARGS__)

#define _SSDbgTraceA(format, ...)                                                                                      \
	if (false)                                                                                                         \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__);                                                               \
	else                                                                                                               \
		___SSDbgTraceA(format, __VA_ARGS__)

void BraceCounter::debug_print(std::ostream& ss)
{
	debug_print_BraceCounter(*this, ss);
}

void BraceCounter::debug_print(std::wostream& ss)
{
	debug_print_BraceCounter(*this, ss);
}

std::string BraceCounter::debug_str()
{
	std::ostringstream oss;
	debug_print(oss);
	return oss.str();
}

bool BraceCounter::next_valid_pos(int& pos, int str_len, const CodeBlock::CharStates* states)
{
	typedef BraceCounter mType;

	int addon = m_backward ? -1 : 1;

	pos += addon;
	if (states && (int)states->size() >= str_len)
	{
		if (is_valid_pos(pos, str_len))
		{
			CharState chs = states->at(pos);

			if (!ApproveState)
			{
				if (chs != chs_none)
					while (is_valid_pos(pos, str_len) && chs_none != states->at(pos))
						pos += addon;
			}
			else
			{
				StateStatus ss = ApproveState(chs);

				if (ss == BraceCounter::StateValid)
					return true;

				if (ss == BraceCounter::StateInvalid_Stop)
				{
					if (is_mismatch())
						return false;
					else
					{
						set_state(mType::WrongCharState);
						return true;
					}
				}

				if (ss == BraceCounter::StateInvalid_Continue)
				{
					while (is_valid_pos(pos, str_len) && BraceCounter::StateValid != ApproveState(states->at(pos)))
						pos += addon;
				}
			}
		}
	}

	if (!is_valid_pos(pos, str_len))
	{
		set_state(mType::OutOfTextRange);
		return false;
	}

	return true;
}

bool BraceCounter::is_open_close(wchar_t ch_open, wchar_t ch_close)
{
	switch (ch_open)
	{
	case '<':
		return ch_close == '>';
	case '(':
		return ch_close == ')';
	case '[':
		return ch_close == ']';
	case '{':
		return ch_close == '}';
	default:
		return false;
	}
}

void BraceCounter::set_state(State state /*= None*/)
{
	if (m_state != state)
	{
		m_state = state;
		if (on_state)
			on_state(*this);
	}
}

CodeBlockTree::CodeBlockTree()
{
}

bool CodeBlockTree::CreatePath(int pos, CodeBlock::PtrSet& path)
{
	bool retval = false;

	try
	{
		std::stack<CodeBlock::Ptr> stack;

		root->resolve_virtual_space();
		stack.push(root);

		while (!stack.empty())
		{
			CodeBlock::Ptr top = stack.top();
			stack.pop();

			if (top->children.size() > 0)
			{
				std::vector<CodeBlock::Ptr> children_copy(top->children.begin(), top->children.end());

				for (CodeBlock::Ptr ptr : children_copy)
				{
					ptr->resolve_virtual_space();
					if (ptr->contains(pos, true, true))
					{
						stack.push(ptr);
						path.insert(ptr);
						retval = true;
					}
				}
			}
		}

		if (!path.empty())
		{
			CodeBlock::Ptr ptr = *path.crbegin();
			CodeBlock::Ptr top, bottom;

			for (auto ch : ptr->children)
			{
				if (ch->end_pos() < pos)
					top = ch;
				else if (!bottom && ch->start_pos() > pos)
				{
					bottom = ch;
					break;
				}
			}

			if (top && bottom && top->get_type_group() == bottom->get_type_group() && !top->is_group_end() &&
			    !bottom->is_group_start())
			{
				std::wstring name;
				int s_pos, e_pos;
				bottom->get_statement_start_end(&name, s_pos, e_pos);
				CodeBlock::Ptr new_cb(new CodeBlock(CodeBlock::b_va_group, src, L'\0', s_pos, e_pos));
				new_cb->name = name;
				path.insert(new_cb);
			}
		}
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in CodeBlockTree::CreatePath");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in CodeBlockTree::CreatePath");
	}

	return retval;
}

// This method groups non-scope statements like "if, for, do, while"
// and makes relations between all children blocks in current scope.
// For example, if one scope has two children: "if(true)" and "y = 50;"
// this method makes "y = 50;" a children of "if(true)", because
// "if(true)" is w/o semicolon or scope considered to be unresolved.
// Once "if(true)" has "y = 50;" as a child, it is resolved, because
// "y = 50;" is a resolved statement. Each parent is considered unresolved
// while it does NOT contain compatible resolver. Resolvers are
// statements with scope, semicolon or parents of such blocks.
// Once resolver is a child of parent, parent is considered as resolved.
// Scope is considered resolved, when it does not contain unresolved parents.
void CodeBlockTree::resolve(const CodeBlock::CharStates& states, bool is_cs)
{
	std::stack<CodeBlock::Ptr> stack;
	stack.push(root);

	const unsigned int MAX_CYCLES = 100000;
	unsigned int num_cycles = 0;

	while (!stack.empty())
	{
		if (++num_cycles > MAX_CYCLES)
		{
			_ASSERTE(!"The number of cycles exceeded the MAX_CYCLES limit!");
			return;
		}

		CodeBlock::Ptr top = stack.top();
		stack.pop();

		////////////////////////////////////////
		// Special handling of SWITCH / CASE
		if (top->is_of_type(CodeBlock::b_switch))
		{
			CodeBlock::Ptr case_block;
			std::vector<CodeBlock::Ptr> to_remove;

			for (CodeBlock::Ptr sptr : top->children)
			{
				CodeBlock* ptr = sptr.get();

				ptr->ensure_name_and_type(states, is_cs);

#ifdef _SSPARSE_DEBUG
				std::wstring wstr = ptr->get_text();
#endif

				if (sptr->is_of_type(CodeBlock::b_case))
				{
					if (case_block)
					{
						for (auto ch : case_block->children)
						{
							ch->ensure_name_and_type(states, is_cs);
							if (ch->is_unresolved_parent() || ch->is_of_type(CodeBlock::b_switch))
							{
								stack.push(case_block);
								break;
							}
						}
					}

					case_block = sptr;
				}
				else if (case_block)
				{
					if (ptr->parent)
					{
						if (ptr->parent != top.get())
							ptr->parent->children.erase(sptr);
						else
							to_remove.push_back(sptr);
					}

					ptr->parent = case_block.get();
					case_block->children.insert(sptr);
				}
			}

			for (CodeBlock::Ptr sptr : to_remove)
				top->children.erase(sptr);

			if (case_block)
			{
				for (auto ch : case_block->children)
				{
					ch->ensure_name_and_type(states, is_cs);
					if (ch->is_scope() ||           // nested scopes
					    ch->is_unresolved_parent()) // alone parents
					{
						stack.push(case_block);
						break;
					}
				}
			}
		}

		////////////////////////////////////////
		// if, else, else if, for and so on....
		else
		{
			std::stack<CodeBlock::Ptr> parent_stack;
			std::vector<CodeBlock::Ptr> to_remove;

			for (CodeBlock::Ptr sptr : top->children)
			{
				CodeBlock* ptr = sptr.get();

				ptr->ensure_name_and_type(states, is_cs);

#ifdef _SSPARSE_DEBUG
				std::wstring wstr = ptr->get_text();
#endif

				CodeBlock* parent = parent_stack.empty() ? nullptr : parent_stack.top().get();

				while (parent && parent->is_resolved_parent(ptr))
				{
					parent_stack.pop();
					parent = parent_stack.empty() ? nullptr : parent_stack.top().get();
				}

				if (parent && parent->is_nonscope_parent_of(*ptr))
				{
					if (ptr->parent)
					{
						if (ptr->parent != top.get())
							ptr->parent->children.erase(sptr);
						else
							to_remove.push_back(sptr);
					}

					ptr->parent = parent;
					parent->children.insert(sptr);
				}

				if (ptr->is_unresolved_parent())
					parent_stack.push(sptr);
				else if (ptr->is_scope())
					stack.push(sptr);
				else
				{
					for (auto ch : ptr->children)
					{
						ch->ensure_name_and_type(states, is_cs);
						if (ch->is_scope() ||           // nested scopes
						    ch->is_unresolved_parent()) // alone parents
						{
							stack.push(sptr);
							break;
						}
					}
				}
			}

			for (CodeBlock::Ptr sptr : to_remove)
				top->children.erase(sptr);
		}
	}
}

bool CodeBlock::operator<(const CodeBlock& r) const
{
	int mp = start_pos();
	int rp = r.start_pos();

	if (mp != rp)
		return mp < rp;

	mp = end_pos();
	rp = r.end_pos();

	if (mp != rp)
		return mp > rp;

	mp = open_pos;
	rp = r.open_pos;

	if (mp != rp)
		return mp < rp;

	mp = close_pos;
	rp = r.close_pos;

	return mp > rp;
}

bool CodeBlock::is_within_scope(const CodeBlock& other) const
{
	if (other.open_pos == -1)
		return false;

	return other.open_pos <= start_pos() && other.close_pos >= end_pos();
}

bool CodeBlock::scope_contains(const CodeBlock& other) const
{
	return other.is_within_scope(*this);
}

bool CodeBlock::contains(const CodeBlock& other) const
{
	return other.is_within(*this);
}

bool CodeBlock::is_nonscope_parent() const
{
	if (is_scope_header())
		return false;

	if (!has_type_bit(b_parent))
		return false;

	if (has_type_bit(b_scope))
		return false;

	return !children.empty();
}

bool CodeBlock::is_nonscope_parent_of(const CodeBlock& other) const
{
	if (other.start_pos() <= start_pos())
		return false;

	if (end_pos() > other.start_pos())
		return false;

	if (is_scope_header())
		return false;

	if ((type & b_parent) == 0)
		return false;

	if (is_of_type(CodeBlock::b_do) && other.is_of_type(CodeBlock::b_while))
		return true;

	return (type & b_scope) != b_scope;
}

bool CodeBlock::is_within(const CodeBlock& other) const
{
	return other.start_pos() <= start_pos() && other.end_pos() >= end_pos();
}

void CodeBlock::on_init(int id) const
{
	// #SmartSelect_CodeBlockInit
	//	_asm nop;
}

CodeBlock::CodeBlock(SharedWStr src, wchar_t t, int t_pos, int o_pos, int c_pos, int sc_pos)
    : type(b_undefined), open_char(t)
#ifdef _DEBUG
      ,
      fnc_name(s_fnc_name.empty() ? nullptr : s_fnc_name.top()), index(++s_index)
#endif
      ,
      src_code(src)
{
	top_pos = t_pos;
	close_pos = c_pos;
	open_pos = o_pos;
	semicolon_pos = sc_pos;
#ifdef _DEBUG
	on_init(index);
#endif
}

CodeBlock::CodeBlock(SharedWStr src, wchar_t t, int top_pos, int end_pos)
    : CodeBlock::CodeBlock(src, t, top_pos, -1, end_pos, -1)
{
}

CodeBlock::CodeBlock(SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos)
    : CodeBlock::CodeBlock(src, t, top_pos, open_pos, close_pos, -1)
{
}

CodeBlock::CodeBlock(CodeBlock::block_type bt, SharedWStr src, wchar_t t, int t_pos, int o_pos, int c_pos, int sc_pos)
    : type(bt), open_char(t)
#ifdef _DEBUG
      ,
      fnc_name(s_fnc_name.empty() ? nullptr : s_fnc_name.top()), index(++s_index)
#endif
      ,
      src_code(src)
{
	top_pos = t_pos;
	close_pos = c_pos;
	open_pos = o_pos;
	semicolon_pos = sc_pos;
#ifdef _DEBUG
	on_init(index);
#endif
}

CodeBlock::CodeBlock(CodeBlock::block_type bt, SharedWStr src, wchar_t t, int top_pos, int end_pos)
    : CodeBlock::CodeBlock(bt, src, t, top_pos, -1, end_pos, -1)
{
}

CodeBlock::CodeBlock(CodeBlock::block_type bt, SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos)
    : CodeBlock::CodeBlock(bt, src, t, top_pos, open_pos, close_pos, -1)
{
}

std::wstring CodeBlock::debug_string(const CodeBlock::CharStates& states, bool is_cs)
{
	std::wostringstream ss;
	debug_print(states, ss, is_cs, 0);
	return ss.str();
}

void CodeBlock::debug_print(const CodeBlock::CharStates& states, std::wostream& out, bool is_cs,
                            int indent /*= 0*/) const
{
	if (is_root())
	{
		for (auto& ch : children)
			ch->debug_print(states, out, is_cs, indent);

		return;
	}

	std::wstring indent_str(indent ? (indent * 2u - 1u) : 0u, L'>');
	if (indent)
		indent_str += ' ';

	if (is_top_level())
		out << LR"(\/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/ \/)" << std::endl;
	else
		out << indent_str << "-----------------------------------" << std::endl;

	if (top_pos == -1)
	{
		_ASSERTE("Unresolved block?");
	}

	ensure_name_and_type(states, is_cs);
	out << indent_str << L"NAME: \"" << name << "\"" << std::endl;
	out << indent_str << L"TYPE: \"" << std::hex << (int)type << std::dec << "\"" << std::endl;
	out << indent_str << L"COMPLETE: \"" << (is_resolved_parent() ? L"true" : L"false") << "\"" << std::endl;
	out << indent_str << L"DEPTH: \"" << depth() << "\"" << std::endl;

	if (get_type_group())
	{
		std::set<CodeBlock> group;
		get_group(group);

		auto m_it = group.find(*this);
		int idx = (int)std::distance(group.begin(), m_it);

		block_type gt = get_type_group();
		if (gt == b_if_group)
			out << indent_str << L"GROUP: IF[" << idx << "/" << group.size() << "]" << std::endl;
		else if (gt == b_try_group)
			out << indent_str << L"GROUP: TRY[" << idx << "/" << group.size() << "]" << std::endl;
		else if (gt == b_ms_try_group)
			out << indent_str << L"GROUP: MS TRY[" << idx << "/" << group.size() << "]" << std::endl;
	}

	if (!is_root())
		out << indent_str << get_text() << std::endl;

	if (!children.empty())
	{
		out << indent_str << "CHILDREN:";
		out << std::endl;
		for (const auto& ptr : children)
			ptr->debug_print(states, out, is_cs, indent + 1);
		out << std::endl;
	}

	if (is_top_level())
		out << LR"(/\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\ /\)" << std::endl << std::endl;
}

bool CodeBlock::try_get_text(std::wstring& str_out) const
{
	if (is_root())
	{
		str_out = src_code.c_str();
		return true;
	}

	int s = start_pos();
	int e = end_pos();

	if (s >= 0 && e >= s && e < (int)src_code.length())
	{
		str_out = src_code.substr_se(s, e);
		return true;
	}

	return false;
}

bool CodeBlock::try_get_header_text(std::wstring& str_out) const
{
	if (open_pos >= 0 && top_pos >= 0 && open_pos > top_pos)
	{
		str_out = src_code.substr(top_pos, open_pos - top_pos);
		return true;
	}

	return false;
}

bool CodeBlock::try_get_scope_text(std::wstring& str_out) const
{
	if (open_pos >= 0 && close_pos >= 0 && close_pos > open_pos)
	{
		str_out = src_code.substr(open_pos, close_pos - open_pos + 1);
		return true;
	}

	return false;
}

void CodeBlock::ensure_dbg_text() const
{
#if defined(_SSPARSE_DEBUG) && !defined(_SSPARSE_DEBUG_POS_PROPS)
	dbg_text = get_text();
	for (auto child : children)
		child->ensure_dbg_text();
#endif
}

void CodeBlock::get_group(std::set<CodeBlock>& items) const
{
	if (parent == nullptr)
		return;

	block_type group = (block_type)(type & b_group_mask);
	if (group)
	{
		items.insert(*this);
		int step = 0;

		auto inserter = [&step, &items, group](const CodeBlock& block) -> bool {
			if (block.get_type_group() != group)
				return false;

			if (block.is_header())
				return false;

			items.insert(block);

			if ((step < 0 && block.is_group_start()) || (step > 0 && block.is_group_end()))
			{
				return false;
			}

			return true;
		};

		if (!is_group_start())
			iterate_siblings(step = -1, inserter);

		if (!is_group_end())
			iterate_siblings(step = 1, inserter);
	}
}

int CodeBlock::group_members_count() const
{
	if (parent == nullptr)
		return 0;

	block_type group = (block_type)(type & b_group_mask);
	if (group)
	{
		int count = 1;

		int step = 0;
		auto inserter = [&step, &count, group](const CodeBlock& block) -> bool {
			if (block.get_type_group() != group)
				return false;

			if (block.is_header())
				return false;

			count++;

			if ((step < 0 && block.is_group_start()) || (step > 0 && block.is_group_end()))
			{
				return false;
			}

			return true;
		};

		if (!is_group_start())
			iterate_siblings(step = -1, inserter);

		if (!is_group_end())
			iterate_siblings(step = 1, inserter);

		return count;
	}

	return 0;
}

CodeBlock::block_type CodeBlock::get_type_group() const
{
	return (block_type)(type & b_group_mask);
}

void CodeBlock::ensure_name_and_type(const CodeBlock::CharStates& states, bool is_cs) const
{
	if (!name.empty())
		return;

	/*static*/ auto trim = [](const std::wstring& s) -> std::wstring {
		std::locale locale;
		auto is_space = [&locale](wchar_t c) { return std::isspace(c, locale); };
		auto wsfront = std::find_if_not(s.begin(), s.end(), is_space);
		return std::wstring(
		    wsfront, std::find_if_not(s.rbegin(), std::wstring::const_reverse_iterator(wsfront), is_space).base());
	};

	/*static*/ auto starts_with = [](const std::wstring& what, const std::wstring& with, bool non_identifier_end) {
		if (what.length() < with.length())
			return false;

		if (wcsncmp(what.c_str(), with.c_str(), with.length()) == 0)
		{
			if (!non_identifier_end)
				return true;

			if (what.length() > with.length())
			{
				wchar_t ch = what[with.length()];
				return !ISCSYM(ch);
			}

			return true;
		}

		return false;
	};

	/*static*/ auto ends_with = [](const std::wstring& what, const std::wstring& with) {
		if (what.length() < with.length())
			return false;

		return std::equal(with.rbegin(), with.rend(), what.rbegin());
	};

	// can't be static due to capture!
	auto get_type = [&](const std::wstring& b_name, bool scope) -> block_type {
		if (!b_name.empty() && b_name.front() == '[')
		{
			static std::wregex lmb_rgx(LR"([[][.]*[\]]\s*(?:[(][.]*[)][^{};]*?)?[{][.]*[}][ \t]*;?)", std::wregex::ECMAScript);

			if (std::regex_match(b_name, lmb_rgx, std::regex_constants::match_continuous))
				return b_cpp_lambda;
		}

		if (starts_with(b_name, L"if", true))
			return scope ? b_if_scope : b_if;
		else if (starts_with(b_name, L"else if", true))
			return scope ? b_else_if_scope : b_else_if;
		else if (starts_with(b_name, L"else", true))
			return scope ? b_else_scope : b_else;
		else if (!is_cs && starts_with(b_name, L"for each", true))
			return scope ? b_for_each_scope : b_for_each;
		else if (is_cs && starts_with(b_name, L"foreach", true))
			return scope ? b_cs_foreach_scope : b_cs_foreach;
		else if (starts_with(b_name, L"for", true))
			return scope ? b_for_scope : b_for;
		else if (starts_with(b_name, L"while", true))
			return scope ? b_while_scope : b_while;
		else if (starts_with(b_name, L"do", true))
			return scope ? b_do_scope : b_do;
		else if (is_cs && starts_with(b_name, L"using", true))
			return scope ? b_cs_using_scope : b_cs_using;
		else if (is_cs && starts_with(b_name, L"lock", true))
			return scope ? b_cs_lock_scope : b_cs_lock;
		else if (!is_cs && (starts_with(b_name, L"_asm", true) || starts_with(b_name, L"__asm", true)))
			return scope ? b_vc_asm_scope : b_vc_asm;
		else if (is_cs && starts_with(b_name, L"unsafe", true))
			return b_cs_unsafe;
		else if (starts_with(b_name, L"switch", true))
			return b_switch;
		else if (starts_with(b_name, L"case", true))
			return b_case;
		else if (starts_with(b_name, L"default", true))
			return b_case;
		else if (starts_with(b_name, L"try", true))
			return b_try;
		else if (starts_with(b_name, L"catch", true))
			return b_catch;
		else if (starts_with(b_name, L"finally", true))
			return b_finally;
		else if (!is_cs && starts_with(b_name, L"__try", true))
			return b_vc_try;
		else if (!is_cs && starts_with(b_name, L"__except", true))
			return b_vc_except;
		else if (!is_cs && starts_with(b_name, L"__finally", true))
			return b_vc_finally;
		else if (starts_with(b_name, L"{", false))
			return b_local_scope;
		else if (starts_with(b_name, L"class", true))
			return b_class;
		else if (starts_with(b_name, L"struct", true))
			return b_struct;
		else if (starts_with(b_name, L"namespace", true))
			return b_namespace;
		else if (!is_cs && starts_with(b_name, L"union", true))
			return b_union;
		else if (starts_with(b_name, L"enum", true))
			return b_enum;
		else if (starts_with(b_name, L"break", true))
			return b_break;
		else if (starts_with(b_name, L"return", true))
			return b_return;
		else if (starts_with(b_name, L"co_return", true))
			return b_return;
		else if (starts_with(b_name, L"co_yield", true))
			return b_return;
		else if (starts_with(b_name, L"continue", true))
			return b_continue;

		// not known currently
		else
			return b_undefined;
	};

	auto is_empty_scope = [this, &trim](int op, int cp) {
		if (cp < op || cp - op <= 1)
			return true;

		auto str = src_code.substr_se(op + 1, cp - 1);
		str = trim(str);

		return str.empty();
	};

	// 	std::wstring txt = get_text();
	// 	if (txt == L"else")
	// 		_asm int 3;

	std::wstring wstr;
	int e_pos = (open_char == '(' || open_char == '{' || open_char == '[' || open_char == '<') ? open_pos : end_pos();

	if (open_char == '\0')
	{
		e_pos++;
		src_code.clamp_to_range(e_pos);
	}

	if (e_pos >= 0 && top_pos >= 0 && e_pos > top_pos)
	{
		bool prev_is_ws = true;
		for (int i = top_pos; i <= e_pos; i++)
		{
			// quick handling for opening and closing
			if (i == open_pos && close_pos > i)
			{
				wstr += src_code[(uint)i];

				if (!is_empty_scope(open_pos, close_pos))
					wstr += L"...";

				wstr += src_code[(uint)close_pos];
				i = close_pos;
				continue;
			}

			if (!states[i].HasAnyBitOfMask(chs_comment_mask))
			{
				wchar_t ch = src_code[(uint)i];
				if (iswspace(ch))
				{
					if (!prev_is_ws)
					{
						wstr += ' ';
						prev_is_ws = true;
					}
				}
				else if (ch == '\\')
				{
					wchar_t ch_next = src_code.safe_at(i + 1);
					if (ch_next == '\n')
					{
						i++;
						continue;
					}
					else if (ch_next == '\r')
					{
						wchar_t ch_next2 = src_code.safe_at(i + 2);
						i += (ch_next2 == '\n') ? 2 : 1;
						continue;
					}
					else
					{
						wstr += ch;
						prev_is_ws = false;
					}
				}
				else
				{
					wstr += ch;
					prev_is_ws = false;
				}
			}
		}
	}

	DWORD ex_attrs = type & b_ex_attr_mask;

	if (wstr.empty())
	{
		if (open_char == '{')
		{
			name = is_empty_scope(open_pos, close_pos) ? L"{}" : L"{...}";
			type = (CodeBlock::block_type)(ex_attrs | CodeBlock::b_local_scope);
		}
		else if (open_char == '(')
		{
			name = is_empty_scope(open_pos, close_pos) ? L"()" : L"(...)";
			type = (CodeBlock::block_type)(ex_attrs | CodeBlock::b_rounds);
		}
		else if (open_char == '[')
		{
			name = is_empty_scope(open_pos, close_pos) ? L"[]" : L"[...]";
			type = (CodeBlock::block_type)(ex_attrs | CodeBlock::b_squares);
		}
		else if (open_char == '<')
		{
			name = is_empty_scope(open_pos, close_pos) ? L"<>" : L"<...>";
			type = (CodeBlock::block_type)(ex_attrs | CodeBlock::b_angles);
		}

		return;
	}

	if (!wstr.empty() && wstr.at(wstr.length() - 1) == ' ')
		wstr.erase(wstr.length() - 1);

	if (!wstr.empty())
	{
		ellipsis_scopes_and_literals(wstr, is_cs);

		// remove preceding [...] attribute boxes, but not lambda caption group
		if (open_char != '[' && !wstr.empty() && wstr[0] == '[')
		{
			for (size_t i = 1; i < wstr.length(); i++)
			{
				wchar_t ch = wstr[i];
				if (ch != '[' && ch != ']' && ch != '.' && !wt_isspace(ch))
				{
					if (ch != '(') // preserve if it is lambda
						wstr.erase(0, i);

					break;
				}
			}
		}
	}

	bool determine_type = (type & b_type_mask) == b_type_none;

	if (open_char == '{')
	{
		type = get_type(wstr, true);
		determine_type = type == b_undefined;

		if (type == b_undefined || is_of_type(b_class) || is_of_type(b_struct) || is_of_type(b_namespace) ||
		    is_of_type(b_enum) || is_of_type(b_union))
		{
			normalize_scope_name(wstr, is_cs);
		}
	}
	else if (open_char == ';')
	{
		if (wstr.length() > 40)
			wstr = wstr.substr(0, 37) + L"...";

		if (semicolon_pos != -1)
		{
			wchar_t colon = src_code.safe_at(semicolon_pos);
			if (colon && !wstr.empty() && wstr.back() != colon)
				wstr += colon;
		}
		else if (!wstr.empty() && wstr.back() != L';' && wstr.back() != L',')
			wstr += L';';
	}
	else if (open_char == ':' && wstr.rfind(L':') == std::wstring::npos)
		wstr += L':';
	else if (wstr.empty())
		wstr = L"...";

	_ASSERTE(!wstr.empty());

	name = wstr;

	if (determine_type)
		type = get_type(wstr, open_char == '{');

	type = (block_type)(type | ex_attrs);
}

void CodeBlock::resolve_virtual_space(bool force_update /*= false*/) const
{
	if (virtual_resolved && !force_update)
		return;

	virtual_start = src_code.find_virtual_start(start_pos());
	virtual_end = src_code.find_virtual_end(end_pos());

	virtual_resolved = true;
}

void CodeBlock::normalize_scope_name(std::wstring& wstr, bool is_cs)
{
	// trim all before scope type name

	int next_start = 0;

	if (wstr.empty())
		return;

	for (LPCWSTR cstr = wstr.c_str(); cstr && *cstr; cstr++)
	{
		if (!ISCSYM(*cstr))
		{
			if (wt_isalpha(cstr[1]))
			{
				cstr++;

				if (wcsncmp(cstr, L"namespace", 9) == 0)
					next_start = 9;
				else if (wcsncmp(cstr, L"class", 5) == 0)
					next_start = 5;
				else if (wcsncmp(cstr, L"struct", 6) == 0)
					next_start = 6;
				else if (wcsncmp(cstr, L"enum", 4) == 0)
					next_start = 4;
				else if (!is_cs && wcsncmp(cstr, L"union", 5) == 0)
					next_start = 5;

				if (next_start > 0)
				{
					wstr = cstr;
					break;
				}
			}
			else if (*cstr == ':')
			{
				if (cstr[1] == ':')
					cstr++;
				else
					break;
			}
			else if (*cstr == '(' || *cstr == '[' || *cstr == '{' || *cstr == '=' || *cstr == ',' || *cstr == ';')
			{
				break;
			}
		}
	}

	// trim all after and including ':'
	size_t last_no_whitespace = 0;
	for (size_t i = (uint)next_start; i < wstr.length(); i++)
	{
		wchar_t ch = wstr[i];

		if (ch == ':')
		{
			if (wstr.length() > i + 1)
			{
				if (wstr[i + 1] == L':')
					i++;
				else
				{
					wstr = wstr.substr(0, last_no_whitespace + 1);
					break;
				}
			}
		}

		if (ch != ' ')
			last_no_whitespace = i;
	}
}

std::wstring CodeBlock::get_text() const
{
	std::wstring out;
	try_get_text(out);
	return out;
}

std::wstring CodeBlock::get_header_text() const
{
	std::wstring out;
	try_get_header_text(out);
	return out;
}

std::wstring CodeBlock::get_scope_text() const
{
	std::wstring out;
	try_get_scope_text(out);
	return out;
}

CodeBlock* CodeBlock::prev_sibling()
{
	return sibling_at_offset(-1);
}

const CodeBlock* CodeBlock::prev_sibling() const
{
	return sibling_at_offset(-1);
}

CodeBlock* CodeBlock::next_sibling()
{
	return sibling_at_offset(1);
}

const CodeBlock* CodeBlock::next_sibling() const
{
	return sibling_at_offset(1);
}

CodeBlock* CodeBlock::sibling_at_offset(int offset)
{
	if (parent == nullptr)
		return nullptr;

	auto it = std::find_if(parent->children.begin(), parent->children.end(),
	                       [this](const CodeBlock::Ptr a) -> bool { return a.get() == this; });
	auto it_end = parent->children.end();

	if (it != it_end)
	{
		// in VC++: [ set::begin() - 1 == set::end() ]
		// I have it checked...

		for (; 0 < offset; --offset)
		{
			if (++it == it_end)
				return nullptr;
			else if ((*it)->parent != parent)
				offset++; // one more
		}

		for (; offset < 0; ++offset)
		{
			if (--it == it_end)
				return nullptr;
			else if ((*it)->parent != parent)
				offset--; // one more
		}

		if (it != it_end)
			return it->get();
	}

	return nullptr;
}

const CodeBlock* CodeBlock::sibling_at_offset(int offset) const
{
	if (parent == nullptr)
		return nullptr;

	auto it = std::find_if(parent->children.begin(), parent->children.end(),
	                       [this](const CodeBlock::Ptr a) -> bool { return a.get() == this; });
	auto it_end = parent->children.cend();

	if (it != it_end)
	{
		// in VC++: [ set::begin() - 1 == set::end() ]
		// I have it checked...

		for (; 0 < offset; --offset)
		{
			if (++it == it_end)
				return nullptr;
			else if ((*it)->parent != parent)
				offset++; // one more
		}

		for (; offset < 0; ++offset)
		{
			if (--it == it_end)
				return nullptr;
			else if ((*it)->parent != parent)
				offset--; // one more
		}

		if (it != it_end)
			return it->get();
	}

	return nullptr;
}

void CodeBlock::iterate_children(std::function<bool(const CodeBlock&)> fnc) const
{
	CodeBlock::PtrSet::const_iterator it;
	for (it = children.cbegin(); it != children.cend(); ++it)
		if (*it && (*it)->parent == this && !fnc(*(*it)))
			return;
}

void CodeBlock::iterate_children_all(std::function<bool(int, const CodeBlock&)> fnc) const
{
	typedef std::pair<int, const CodeBlock*> pair;
	std::stack<pair> stack;
	stack.push(pair(0, this));

	while (!stack.empty())
	{
		pair top(stack.top().first, stack.top().second);
		stack.pop();

		const CodeBlock* block = top.second;

		if (block)
		{
			CodeBlock::PtrSet::const_iterator it;
			for (it = block->children.cbegin(); it != block->children.cend(); ++it)
			{
				const CodeBlock::Ptr child = *it;

				if (!child || child->parent != block)
					continue;

				if (!fnc(top.first, *child))
					return;

				if (!child->children.empty())
					stack.push(pair(top.first + 1, child.get()));
			}
		}
	}
}

void CodeBlock::iterate_children_mutable(std::function<bool(int, CodeBlock&)> fnc)
{
	typedef std::pair<int, CodeBlock*> pair;
	std::stack<pair> stack;
	stack.push(pair(0, this));

	while (!stack.empty())
	{
		pair top(stack.top().first, stack.top().second);
		stack.pop();

		CodeBlock* block = top.second;

		if (block)
		{
			CodeBlock::PtrSet::const_iterator it;
			for (it = block->children.begin(); it != block->children.end(); ++it)
			{
				CodeBlock::Ptr child = *it;

				if (!child || child->parent != block)
					continue;

				if (!fnc(top.first, *child))
					return;

				if (!child->children.empty())
					stack.push(pair(top.first + 1, child.get()));
			}
		}
	}
}

void CodeBlock::iterate_siblings(int step, std::function<bool(const CodeBlock&)> fnc, int num_iters /*= -1*/) const
{
	if (parent == nullptr)
		return;

	auto it = std::find_if(parent->children.begin(), parent->children.end(),
	                       [this](const CodeBlock::Ptr a) -> bool { return a.get() == this; });
	auto it_end = parent->children.cend();

	if (it != it_end)
	{
		for (int iters = 0; num_iters < 0 || iters < num_iters; iters++)
		{
			// in VC++: [ set::begin() - 1 == set::end() ]
			// I have it checked...

			int offset = step;

			for (; 0 < offset; --offset)
			{
				if (++it == it_end)
					return;
				else if ((*it)->parent != parent)
					offset++; // one more
			}

			for (; offset < 0; ++offset)
			{
				if (--it == it_end)
					return;
				else if ((*it)->parent != parent)
					offset--; // one more
			}

			if (!fnc(*(*it)))
				return;
		}
	}
}

bool CodeBlock::is_content() const
{
	if (parent == nullptr)
		return false;

	return parent->contains(*this);
}

int CodeBlock::get_max_end_pos() const
{
	// 	if (this->dbg_text.find(L"if (y == 2233)") != std::wstring::npos)
	// 		_asm int 3;

	int e_pos = end_pos();

	for (auto b : children)
	{
		if (b)
		{
			int e = b->get_max_end_pos();
			if (e > e_pos)
				e_pos = e;
		}
	}

	return e_pos;
}

void CodeBlock::get_statement_start_end(std::wstring* name_ptr, int& s_pos, int& e_pos) const
{
	if (get_type_group())
	{
		s_pos = start_pos();
		e_pos = end_pos();

		std::set<CodeBlock> blocks;
		get_group(blocks);
		for (auto& b : blocks)
		{
			if (name_ptr)
			{
				if (!name_ptr->empty())
					*name_ptr += ' ';

				name_ptr->append(b.name);
			}

			int tmp = b.start_pos();
			s_pos = __min(s_pos, tmp);

			tmp = b.get_max_end_pos();
			e_pos = __max(e_pos, tmp);
		}
	}
	else
	{
		s_pos = start_pos();
		e_pos = get_max_end_pos();

		if (name_ptr)
			*name_ptr = this->name;
	}
}

const CodeBlock* CodeBlock::children_back() const
{
	if (!children.empty())
		return children.crbegin()->get();
	return nullptr;
}

CodeBlock* CodeBlock::children_back()
{
	if (!children.empty())
		return children.rbegin()->get();
	return nullptr;
}

bool CodeBlock::is_top_level() const
{
	return parent == nullptr || parent->is_root();
}

int CodeBlock::depth() const
{
	int depth = 0;
	CodeBlock* p = parent;
	while (p && !p->is_root())
	{
		depth++;
		p = p->parent;
	}

	return depth;
}

bool CodeBlock::is_root() const
{
	return start_pos() == INT_MIN && end_pos() == INT_MAX;
}

int CodeBlock::end_pos() const
{
	return max(close_pos, semicolon_pos);
}

int CodeBlock::virtual_end_pos() const
{
	resolve_virtual_space();
	if (virtual_end != -1)
		return virtual_end;
	return end_pos();
}

int CodeBlock::virtual_start_pos() const
{
	resolve_virtual_space();
	if (virtual_start != -1)
		return virtual_start;
	return start_pos();
}

int CodeBlock::start_pos() const
{
	if (top_pos != -1 && open_pos != -1)
		return top_pos < open_pos ? top_pos : open_pos;

	if (top_pos != -1)
		return top_pos;

	if (open_pos != -1)
		return open_pos;

	return close_pos;
}

bool CodeBlock::is_header() const
{
	if (is_content())
		return parent->top_pos == top_pos;

	return false;
}

bool CodeBlock::is_invalid_parent() const
{
	return is_parent_type() && !is_scope() && children.empty();
}

bool CodeBlock::is_parent_type() const
{
	return has_type_bit(CodeBlock::b_parent);
}

bool CodeBlock::is_unresolved_parent(CodeBlock* incoming_child /*= nullptr*/) const
{
	if (!is_parent_type() || is_header())
		return false;

	return !is_resolved_parent(incoming_child);
}

bool CodeBlock::is_resolver() const
{
	if (is_parent_type())
		return is_resolved_parent();

	if (is_content())
		return false;

	int ep = end_pos();

	if (ep >= 0 && (src_code[(uint)ep] == ';' || src_code[(uint)ep] == '}'))
		return true;

	return false;
}

bool CodeBlock::is_resolved_parent(CodeBlock* incoming_child /*= nullptr*/) const
{
	if (!is_parent_type() || is_header())
		return false;

	if (is_of_type(b_do))
	{
		for (auto b : children)
			if (b && b->is_of_type(b_while))
				return true;

		return false;
	}

	if (is_of_type(b_case))
	{
		return incoming_child && incoming_child->is_of_type(CodeBlock::b_case);
	}

	if (is_of_type(b_while) && parent && parent->is_of_type(b_do))
		return true;

	if (is_scope())
		return true;

	bool has_if = false;
	bool has_else = false;
	bool has_resolver = false;

	const CodeBlock* prev_child = nullptr;

	CodeBlock::PtrSet::const_iterator it;
	for (it = children.begin(); it != children.end(); ++it)
	{
		CodeBlock::Ptr b = *it;

		if (!b)
			continue;

		if (b->is_resolver())
			has_resolver = true;

		if (incoming_child && incoming_child->has_type_bit(b_if_group))
		{
			if (b->is_of_type(b_if) && !b->is_header())
				has_if = true;

			if (b->is_of_type(b_else) && !b->is_header())
				has_else = true;
		}

		prev_child = b.get();
	}

	if (has_if)
	{
		if (incoming_child && incoming_child->is_of_type(b_if))
			return true;

		return has_else;
	}

	return has_resolver;
}

bool CodeBlock::is_group_end() const
{
	if (is_of_type(b_else) || is_of_type(b_finally))
		return true;

	const CodeBlock* nxtSbl = next_sibling();

	if (nxtSbl == nullptr)
		return true;

	if (nxtSbl->is_group_start())
		return true;

	if (nxtSbl->get_type_group() != get_type_group())
		return true;

	return false;
}

bool CodeBlock::is_group_start() const
{
	return is_of_type(b_if) || is_of_type(b_try);
}

bool CodeBlock::is_group_type() const
{
	return get_type_group() != 0;
}

bool CodeBlock::has_open_close() const
{
	return open_pos >= 0 && close_pos >= 0;
}

bool CodeBlock::is_scope_header() const
{
	if (parent == nullptr)
		return false;

	if (!parent->is_scope())
		return false;

	return !parent->scope_contains(*this) && parent->top_pos == top_pos;
}

bool CodeBlock::PtrComparer::operator()(const Ptr& ls, const Ptr& rs) const
{
	if (ls && rs)
		return *ls < *rs;
	return ls < rs;
}

CodeBlock::Ptr CodeBlock::find_parent_candidate(const CodeBlock::PtrSet& blocks) const
{
	typedef CodeBlock::PtrSet::const_iterator cIter;

	int m_spos = start_pos();
	int m_epos = end_pos();

	for (const auto& block : blocks)
	{
		CodeBlock* b = block.get();

		if (!b)
			continue;

		if (b->end_pos() < m_spos)
			continue;

		if (b->start_pos() > m_epos)
			break;

		if (b->contains(*this))
			return block;
	}

	return CodeBlock::Ptr();
}

bool CodeBlock::insert_child(CodeBlock::Ptr child)
{
	if (child && contains(*child))
	{
		CodeBlock::Ptr new_parent;
		auto curr_parent = this;

		while (curr_parent)
		{
			new_parent = child->find_parent_candidate(curr_parent->children);

			if (new_parent)
			{
				curr_parent = new_parent.get();
				continue;
			}
			else
			{
				child->parent = curr_parent;
				curr_parent->children.insert(child);
				return true;
			}
		}
	}

	return false;
}

void CodeBlock::ellipsis_scopes_and_literals(std::wstring& wstr, bool is_cs)
{
	for (int x = 0; x < 2; x++)
	{
		BraceCounter bc(x == 0 ? "[]{}()" : "", true);
		bc.set_ignore_angle_brackets(x == 0);

		CodeBlock::CharStates ch_states(wstr.length() + 1);
		Parser::MarkCommentsAndContants(SharedWStr(wstr), ch_states, is_cs);

		bc.on_block = [&bc, &wstr](char ch, int open, int close) {
			if (bc.is_closed() && close - open > 3)
			{
				wstr.replace(wstr.begin() + (open + 1), wstr.begin() + close, L"...");
			}
		};

		for (int i = (int)wstr.length() - 1; i >= 0; i--)
		{
			if (!ch_states[i].HasAnyBitOfMask(chs_string_mask | chs_comment_mask))
			{
				wchar_t ch = wstr[(uint)i];
				bc.on_char(ch, i, nullptr);
			}
		}
	}

	std::vector<std::pair<int, int>> ranges;
	SharedWStr s_wstr(wstr);
	CharStateIter csi = CharStateIter(0, int(wstr.length() - 1), is_cs, s_wstr);

	auto finder = [](wchar_t ch) { return ch == '"' || ch == '\''; };

	csi.on_state_change = [&ranges, finder, s_wstr](int pos, CharState old_state, CharState new_state) mutable {
		if (new_state & chs_string_mask)
			ranges.push_back(std::make_pair(s_wstr.find(pos, finder), -1));
		else if (old_state & chs_string_mask && !ranges.empty() && ranges.back().second == -1)
			ranges.back().second = s_wstr.rfind(pos - 1, finder);
	};

	while (csi.next())
		;

	for (auto it = ranges.crbegin(); it != ranges.crend(); ++it)
	{
		// may happen that one of sides is -1 => it is error
		if (it->first >= 0 && it->second >= 0)
			wstr.replace(wstr.begin() + (it->first + 1), wstr.begin() + it->second, L"...");
	}
}

void CodeBlock::get_range_with_comments(int& s_pos, int& e_pos, int caret_pos /*= -1*/) const
{
	auto get_start_end = [caret_pos](const CodeBlock& block, int& spos, int& epos) {
		if (caret_pos < 0)
		{
			spos = block.prefix_comment ? block.prefix_comment->start_pos() : block.start_pos();
			epos = block.suffix_comment ? block.suffix_comment->end_pos() : block.end_pos();
		}
		else
		{
			spos = block.start_pos();
			if (block.prefix_comment && block.prefix_comment->contains(caret_pos, false, true))
				spos = block.prefix_comment->start_pos();

			epos = block.end_pos();
			if (block.suffix_comment && block.suffix_comment->contains(caret_pos, false, true))
				epos = block.suffix_comment->end_pos();
		}
	};

	get_start_end(*this, s_pos, e_pos);

	std::function<bool(int, const CodeBlock&)> fnc = [&s_pos, &e_pos, get_start_end](int depth,
	                                                                                 const CodeBlock& block) -> bool {
		int s = s_pos, e = e_pos;

		get_start_end(block, s, e);

		if (s < s_pos)
			s_pos = s;

		if (e > e_pos)
			e_pos = e;

		return true;
	};

	iterate_children_all(fnc);
}

#ifdef _DEBUG
ThreadSafeStack<LPCSTR> CodeBlock::s_fnc_name;
int CodeBlock::s_index = 0;
#endif

void SequenceComparer::AddRegex(LPCWSTR pattern, bool icase, bool nosubs /*= true*/, bool collate /*= true*/,
                                bool optimize /*= true*/)
{
	std::wregex::flag_type flags = std::wregex::ECMAScript;

	if (icase)
		flags |= std::wregex::icase;

	if (nosubs)
		flags |= std::wregex::nosubs;

	if (collate)
		flags |= std::wregex::collate;

	if (optimize)
		flags |= std::wregex::optimize;

	std::shared_ptr<std::wregex> rgx(new std::wregex(pattern, flags));

	AddRegex(rgx);
}

void SequenceComparer::AddRegex(std::shared_ptr<std::wregex> rgx)
{
	Add([rgx](context& ctx) -> bool {
		std::wcmatch m;
		int max_pos = ctx.max_pos;

		if (max_pos == -1)
			max_pos = (int)ctx.src.length() - 1;

		LPCWSTR str_begin = ctx.src_begin();
		LPCWSTR str_end = ctx.src_end();

		if (std::regex_search(str_begin, str_end, m, *rgx))
		{
			if (m.position() == 0)
			{
				ctx.pos += (int)m.length();
				ctx.curr_cmp_it++;
				return true;
			}
		}

		return false;
	});
}

void SequenceComparer::Add(std::function<bool(context&)> cmp)
{
	m_cmps.push_back(cmp);
}

void SequenceComparer::AddString(const std::wstring& str_to_match, bool ignore_case /*= false*/)
{
	std::wstring str = str_to_match;
	Add([str, ignore_case](context& ctx) -> bool {
		if ((int)str.length() > ctx.src_length())
			return false;

		int cmp = ignore_case ? _wcsnicmp(str.c_str(), ctx.src_begin(), str.length())
		                      : wcsncmp(str.c_str(), ctx.src_begin(), str.length());

		if (cmp == 0)
		{
			ctx.pos += (int)str.length();
			ctx.curr_cmp_it++;
			return true;
		}

		return false;
	});
}

void SequenceComparer::AddMove(std::function<bool(wchar_t ch)> fnc,
                               const Quantifier& qant /*= Quantifier::ZeroOrMore()*/)
{
	_ASSERTE(fnc ? 1 : 0);

	Quantifier q = qant;
	Add([fnc, q](context& ctx) -> bool {
		int moved = 0;
		for (; ctx.src.is_valid_pos(ctx.pos); ctx.pos++)
		{
			if (!fnc(ctx.src[(uint)ctx.pos]))
				break;

			moved++;

			if (moved < q.GetMin())
				continue;

			if (moved >= q.GetMax())
				break;

			if (q.IsLazy())
			{
				int tmp_pos = ctx.pos;
				auto tmp_it = ctx.curr_cmp_it;

				auto next = tmp_it + 1;
				if (next != ctx.end_cmp_it && *next)
				{
					if (next->operator()(ctx))
						return true;
					else
					{
						ctx.pos = tmp_pos;
						ctx.curr_cmp_it = tmp_it;
					}
				}
			}
		}

		return q.IsInRange(moved);
	});
}

void SequenceComparer::AddWhiteSpace(const Quantifier& qant /*= Quantifier::ZeroOrMore()*/)
{
	AddMove([](wchar_t ch) { return wt_isspace(ch) ? true : false; }, qant);
}

void SequenceComparer::AddIdentifier(const Quantifier& qant /*= Quantifier::ZeroOrMore()*/)
{
	bool is_start = true;
	AddMove(
	    [is_start](wchar_t ch) mutable {
		    if (is_start)
		    {
			    is_start = false;
			    return ISCSYM(ch) && !wt_isdigit(ch);
		    }

		    return ISCSYM(ch);
	    },
	    qant);
}

void SequenceComparer::AddAnyOf(const std::wstring& str, const Quantifier& qant /*= Quantifier::ZeroOrMore()*/)
{
	std::wstring _str = str;
	AddMove([_str](wchar_t ch) { return _str.find(ch) != std::wstring::npos; }, qant);
}

void SequenceComparer::AddAnyNotOf(const std::wstring& str, const Quantifier& qant /*= Quantifier::ZeroOrMore()*/)
{
	std::wstring _str = str;
	AddMove([_str](wchar_t ch) { return _str.find(ch) == std::wstring::npos; }, qant);
}

void SequenceComparer::AddBraceMatch(char brace /*= '\0'*/, const std::string& braces /*= "(){}[]"*/)
{
	std::string brs = braces;
	Add([brace, brs](context& ctx) -> bool {
		if (brace != '\0' && ctx.pos_char() != (wchar_t)brace)
			return false;

		char open_brace = brace;
		if (open_brace == '\0')
		{
			wchar_t p_ch = ctx.pos_char();

			if (p_ch >= 0x80)
				return false;

			open_brace = (char)p_ch;
		}

		if (open_brace != '{' && open_brace != '(' && open_brace != '[' && open_brace != '<')
			return false;

		if (brs.find(open_brace) == std::string::npos)
			return false;

		BraceCounter bc = BraceCounter(brs.c_str());
		if (brs.find("<>") == std::string::npos)
			bc.set_ignore_angle_brackets(true);

		int match_pos = -1;
		bc.on_close = [&bc, &match_pos, open_brace](char ch, int pos) {
			if (bc.is_open_close((wchar_t)open_brace, (wchar_t)ch))
			{
				match_pos = pos;
			}
		};

		int max_pos = ctx.safe_max_pos();
		for (int i = ctx.pos; i <= max_pos; i++)
		{
			wchar_t ch = ctx.src[(uint)i];
			bc.on_char(ch, i, &ctx.states);

			if (match_pos >= 0)
			{
				ctx.pos = match_pos + 1;
				ctx.curr_cmp_it++;
				return true;
			}
		}

		return false;
	});
}

bool SequenceComparer::IsMatch(context& ctx) const
{
	ctx.end_cmp_it = m_cmps.cend();
	ctx.curr_cmp_it = m_cmps.cbegin();

	while (ctx.curr_cmp_it != ctx.end_cmp_it)
		if (!ctx.curr_cmp_it->operator()(ctx))
			return false;

	return true;
}

LPCWSTR SharedWStr::c_str() const
{
	return str_ptr->c_str();
}

bool SharedWStr::empty() const
{
	return str_ptr->empty();
}

size_t SharedWStr::length() const
{
	return str_ptr->length();
}

int SharedWStr::find(int start, CharPredicate fnc, CharFilter filter) const
{
	return find(start, -1, fnc, filter);
}

int SharedWStr::find(int start, int end, CharPredicate fnc, CharFilter filter) const
{
	if (start >= (int)length())
		return -1;

	clamp_to_range(start);
	clamp_to_range(end, true);

	if (!filter)
	{
		for (int i = start; i <= end; i++)
			if (fnc(str_ptr->at(i)))
				return i;
	}
	else
	{
		for (int i = start; i <= end; i++)
		{
			wchar_t ch = str_ptr->at(i);

			if (!filter(i, ch))
				continue;

			if (fnc(ch))
				return i;
		}
	}

	return -1;
}

int SharedWStr::find_str(int start, int end, const std::wstring& wstr, CharFilter filter /*= nullptr*/) const
{
	//#untested

	if (start >= (int)length())
		return -1;

	clamp_to_range(start);
	clamp_to_range(end, true);

	if (!filter)
	{
		LPCWSTR s_cstr = c_str() + start;
		LPCWSTR e_cstr = c_str() + (end - wstr.length());

		for (LPCWSTR cstr = s_cstr; cstr <= e_cstr; cstr++)
			if (wcsncmp(cstr, wstr.c_str(), wstr.length()) == 0)
				return ptr_sub__int(cstr, c_str());
	}
	else
	{
		std::wstring buff;
		buff.reserve(wstr.length() + 1);

		for (int i = start; i <= end; i++)
		{
			wchar_t ch = str_ptr->at(i);

			if (!filter(i, ch))
				continue;

			buff.push_back(ch);

			if (buff.length() > wstr.length())
			{
				start++;
				buff.erase(0, 1);
			}

			if (buff == wstr)
				return start;
		}
	}

	return -1;
}

int SharedWStr::find_str(int start, const std::wstring& wstr, CharFilter filter /*= nullptr*/) const
{
	return find_str(start, -1, wstr, filter);
}

int SharedWStr::rfind_str(int start, int end, const std::wstring& wstr, CharFilter filter /*= nullptr*/) const
{
	if (start < 0)
		return -1;

	//#untested

	clamp_to_range(start);
	clamp_to_range(end);

	if (!filter)
	{
		LPCWSTR s_cstr = c_str() + (start - wstr.length());
		LPCWSTR e_cstr = c_str() + end;

		for (LPCWSTR cstr = s_cstr; cstr >= e_cstr; cstr--)
			if (wcsncmp(cstr, wstr.c_str(), wstr.length()) == 0)
				return ptr_sub__int(cstr, c_str());
	}
	else
	{
		std::wstring buff;
		buff.reserve(wstr.length() + 1);

		for (int i = start; i >= end; i--)
		{
			wchar_t ch = str_ptr->at(i);

			if (!filter(i, ch))
				continue;

			buff.insert(buff.begin(), ch);

			if (buff.size() > wstr.length())
				buff.pop_back();

			if (buff == wstr)
				return i;
		}
	}

	return -1;
}

int SharedWStr::rfind_str(int start, const std::wstring& wstr, CharFilter filter /*= nullptr*/) const
{
	return rfind_str(start, -1, wstr, filter);
}

int SharedWStr::rfind(int start, int end, wchar_t ch, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, end, [ch](wchar_t _ch) { return ch == _ch; }, filter);
}

int SharedWStr::rfind(int start, wchar_t ch, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, [ch](wchar_t _ch) { return ch == _ch; }, filter);
}
int SharedWStr::find(int start, int end, wchar_t ch, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, end, [ch](wchar_t _ch) { return ch == _ch; }, filter);
}

int SharedWStr::find(int start, wchar_t ch, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, [ch](wchar_t _ch) { return ch == _ch; }, filter);
}

int SharedWStr::rfind_one_of(int start, int end, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, end, [&chars](wchar_t ch) { return chars.find(ch) != std::wstring::npos; }, filter);
}

int SharedWStr::rfind_one_of(int start, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, [&chars](wchar_t ch) { return chars.find(ch) != std::wstring::npos; }, filter);
}
int SharedWStr::find_one_of(int start, int end, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, end, [&chars](wchar_t ch) { return chars.find(ch) != std::wstring::npos; }, filter);
}

int SharedWStr::find_one_of(int start, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, [&chars](wchar_t ch) { return chars.find(ch) != std::wstring::npos; }, filter);
}

int SharedWStr::rfind_one_not_of(int start, int end, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, end, [&chars](wchar_t ch) { return chars.find(ch) == std::wstring::npos; }, filter);
}

int SharedWStr::rfind_one_not_of(int start, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return rfind(
	    start, [&chars](wchar_t ch) { return chars.find(ch) == std::wstring::npos; }, filter);
}
int SharedWStr::find_one_not_of(int start, int end, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, end, [&chars](wchar_t ch) { return chars.find(ch) == std::wstring::npos; }, filter);
}

int SharedWStr::find_one_not_of(int start, const std::wstring& chars, CharFilter filter /*= nullptr*/) const
{
	return find(
	    start, [&chars](wchar_t ch) { return chars.find(ch) == std::wstring::npos; }, filter);
}

int SharedWStr::rfind(int start, CharPredicate fnc, CharFilter filter) const
{
	return rfind(start, -1, fnc, filter);
}

int SharedWStr::rfind(int start, int end, CharPredicate fnc, CharFilter filter) const
{
	if (start < 0)
		return -1;

	clamp_to_range(start);
	clamp_to_range(end);

	if (!filter)
	{
		for (int i = start; i >= end; i--)
			if (fnc(str_ptr->at(i)))
				return i;
	}
	else
	{
		for (int i = start; i >= end; i--)
		{
			wchar_t ch = str_ptr->at(i);

			if (!filter(i, ch))
				continue;

			if (fnc(ch))
				return i;
		}
	}

	return -1;
}

void SharedWStr::clamp_to_range(int& pos, bool is_end /*= false*/) const
{
	if (pos < 0)
		pos = is_end ? int(length() - 1) : 0;
	else if (pos >= (int)length())
		pos = int(length() - 1);
}

bool SharedWStr::is_on_line_end(int pos, bool check_backslash /*= false*/) const
{
	wchar_t ch = safe_at(pos);

	if (ch == '\0' && pos >= 0)
		return true;

	if (ch == '\n')
	{
		if (check_backslash && safe_at(pos - 1) == '\r')
			return safe_at(pos - 2) != '\\';
		else
			return check_backslash ? safe_at(pos - 1) != '\\' : true;
	}

	if (ch == '\r')
	{
		wchar_t ch_next = safe_at(pos + 1);
		if (ch_next != '\n')
			return check_backslash ? safe_at(pos - 1) != '\\' : true;
		else
			return false;
	}

	return false;
}

bool SharedWStr::is_on_line_delimiter(int pos, bool check_backslash /*= false*/) const
{
	wchar_t ch = safe_at(pos);

	if (ch == '\0' && pos >= 0)
		return true;

	if (ch == '\n' || ch == '\r')
	{
		if (!check_backslash)
			return true;

		if (ch == '\r')
			return safe_at(pos - 1) != '\\';

		if (safe_at(pos - 1) == '\r')
			return safe_at(pos - 2) != '\\';
		else
			return safe_at(pos - 1) != '\\';
	}

	return false;
}

bool SharedWStr::is_within_line_continuation(int pos) const
{
	wchar_t ch = safe_at(pos);

	switch (ch)
	{
	case '\0':
		return false;

	case '\\': {
		wchar_t ch_next = safe_at(pos + 1);
		return ch_next == '\r' || ch_next == '\n';
	}
	case '\n': {
		wchar_t ch_prev = safe_at(pos - 1);

		if (ch_prev == '\r')
			return safe_at(pos - 2) == '\\';
		else
			return ch_prev == '\\';
	}

	case '\r':
		return safe_at(pos - 1) == '\\';

	default:
		return false;
	}
}

int SharedWStr::find_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
                                           CharFilter filter /*= nullptr*/) const
{
	if (allow_preceding_space)
		pos = find_nospace(pos, filter);

	// Note: C# also supports "::" operator!
	// https://msdn.microsoft.com/en-us/library/c3ay4x3d.aspx

	wchar_t ch = safe_at(pos);
	wchar_t ch_next = safe_at(pos + 1);

	if (ch == ':' && ch_next == ':')
		return pos;

	// "?." operator (C# 6+)
	if (is_cs && ch == '?')
	{
		if (ch_next == '.')
			return pos;
	}

	if (is_cs && ch == '.' && !wt_isdigit(ch_next))
		return pos;

	return -1;
}

int SharedWStr::rfind_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
                                            CharFilter filter /*= nullptr*/) const
{
	if (allow_preceding_space)
		pos = rfind_nospace(pos, filter);

	wchar_t ch = safe_at(pos);
	wchar_t ch_prev = safe_at(pos - 1);

	// C# also supports "::" operator!
	// https://msdn.microsoft.com/en-us/library/c3ay4x3d.aspx
	if (ch == ':' && ch_prev == ':')
		return pos - 1;

	if (is_cs)
	{
		// "?." operator (C# 6+)
		if (ch == '.' && ch_prev == '?')
			return pos - 1;

		if (ch == '.')
		{
			wchar_t ch_next = safe_at(pos + 1);

			if (!wt_isdigit(ch_next))
				return pos;
		}
	}

	return -1;
}

bool SharedWStr::is_at_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
                                             CharFilter filter /*= nullptr*/) const
{
	return -1 < find_scope_delimiter_start(pos, is_cs, allow_preceding_space, filter);
}

bool SharedWStr::is_at_scope_delimiter_end(int pos, bool is_cs, bool allow_preceding_space,
                                           CharFilter filter /*= nullptr*/) const
{
	return -1 < rfind_scope_delimiter_start(pos, is_cs, allow_preceding_space, filter);
}

bool SharedWStr::is_at_scope_delimiter(int pos, bool is_cs, bool allow_surrounding_space,
                                       CharFilter filter /*= nullptr*/) const
{
	return is_at_scope_delimiter_start(pos, is_cs, allow_surrounding_space, filter) ||
	       is_at_scope_delimiter_end(pos, is_cs, allow_surrounding_space, filter);
}

int SharedWStr::rfind_call_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
                                           CharFilter filter /*= nullptr*/) const
{
	if (allow_preceding_space)
		pos = rfind_nospace(pos, filter);

	wchar_t ch = safe_at(pos);
	wchar_t ch_prev = safe_at(pos - 1);

	// ::
	if (ch == ':')
	{
		if (ch_prev == ':')
			return pos - 1;
	}

	// ->
	else if (ch == '>')
	{
		if (ch_prev == '-')
			return pos - 1;
	}

	// . and ?. but not ...
	else if (ch == '.')
	{
		// ?.
		if (is_cs && ch_prev == '?')
			return pos - 1;

		// ...
		if (!is_cs && ch_prev == '.')
			return -1; // ellipses (...) ?

		wchar_t ch_next = safe_at(pos + 1);

		if (!is_cs && ch_next == '.')
			return -1; // ellipses (...) ?

		// .
		if (!wt_isdigit(ch_next))
			return pos;
	}

	return -1;
}

int SharedWStr::find_call_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
                                          CharFilter filter /*= nullptr*/) const
{
	if (allow_preceding_space)
		pos = find_nospace(pos, filter);

	wchar_t ch = safe_at(pos);
	wchar_t ch_next = safe_at(pos + 1);

	// . but not ...
	if (ch == '.')
	{
		if (!is_cs)
		{
			// ...
			if (ch_next == '.')
				return -1; // ellipses (...) ?

			wchar_t ch_prev = safe_at(pos - 1);

			if (ch_prev == '.')
				return -1; // ellipses (...) ?
		}

		if (!wt_isdigit(ch_next))
			return pos;

		return -1;
	}

	// ::
	else if (ch == ':')
	{
		if (ch_next == ':')
			return pos;
	}

	// ->
	else if (ch == '-')
	{
		if (ch_next == '>')
			return pos;
	}

	// ?.
	else if (is_cs && ch == '?')
	{
		if (ch_next == '.')
			return pos;
	}

	return -1;
}

bool SharedWStr::is_at_call_delimiter(int pos, bool is_cs, bool allow_surrounding_space,
                                      CharFilter filter /*= nullptr*/) const
{
	return -1 < rfind_call_delimiter_start(pos, is_cs, allow_surrounding_space, filter) ||
	       -1 < find_call_delimiter_start(pos, is_cs, allow_surrounding_space, filter);
}

bool SharedWStr::is_on_line_start(int pos, bool check_backslash /*= false*/) const
{
	if (pos == 0)
		return true;

	return is_on_line_end(pos - 1, check_backslash);
}

int SharedWStr::next_line_start(int pos, bool check_backslash /*= false*/) const
{
	int str_len = (int)str_ptr->length();

	pos = line_end(pos, check_backslash);
	for (int i = pos; i < str_len; i++)
		if (is_on_line_start(i, check_backslash))
			return i;

	return str_len - 1;
}

int SharedWStr::line_end(int pos, bool check_backslash /*= false*/) const
{
	int str_len = (int)str_ptr->length();

	while (pos < str_len)
	{
		if (is_on_line_end(pos, check_backslash))
			return pos;

		pos++;
	}

	return str_len - 1;
}

int SharedWStr::line_start(int pos, bool check_backslash /*= false*/) const
{
	while (pos >= 0)
	{
		if (is_on_line_start(pos, check_backslash))
			return pos;

		pos--;
	}

	return 0;
}

int SharedWStr::prev_line_end(int pos, bool check_backslash /*= false*/) const
{
	int ls = line_start(pos, check_backslash);
	if (ls <= 0)
		return 0;
	if (is_on_line_end(ls - 1, check_backslash))
		return ls - 1;
	else if (is_on_line_end(ls - 2, check_backslash))
		return ls - 2;

	_ASSERTE(!"Something is wrong");
	return 0;
}

int SharedWStr::prev_line_start(int pos, bool check_backslash /*= false*/) const
{
	int ls = line_start(pos, check_backslash);
	if (ls > 0)
		return line_start(ls - 1, check_backslash);
	return 0;
}

bool SharedWStr::line_is_whitespace(int pos, CharFilter filter, bool check_backslash /*= false*/) const
{
	int start = line_start(pos, check_backslash);
	int end = line_end(pos, check_backslash);

	for (int i = start; i <= end; i++)
	{
		wchar_t ch = str_ptr->at(i);

		if (filter && !filter(i, ch))
			continue;

		if (ch == '\\' && check_backslash)
		{
			wchar_t ch_next = safe_at(i + 1);
			if (ch_next != '\r' && ch_next != '\n')
				return false;

			continue;
		}

		if (!wt_isspace(ch))
			return false;
	}

	return true;
}

std::wstring SharedWStr::line_text(int pos) const
{
	int start = line_start(pos);
	int end = line_end(pos);

	return substr_se(start, end);
}

std::wstring SharedWStr::get_symbol(int pos, bool handle_line_continuation /*= true*/, int* out_spos /*= nullptr*/,
                                    int* out_epos /*= nullptr*/, CharFilter filter /* = nullptr*/) const
{
	if (!is_identifier(pos, filter))
		return std::wstring();

	int spos, epos;
	int tmp_spos = spos = pos;
	int tmp_epos = epos = pos;

	std::wstring sym(1, at(pos));

	while (is_identifier(tmp_spos - 1, filter) ||
	       (handle_line_continuation && is_within_line_continuation(tmp_spos - 1)))
	{
		tmp_spos--;

		if (is_identifier(tmp_spos, filter))
		{
			spos = tmp_spos;
			sym.insert(sym.begin(), at(tmp_spos));
		}
	}

	while (is_identifier(tmp_epos + 1, filter) ||
	       (handle_line_continuation && is_within_line_continuation(tmp_epos + 1)))
	{
		tmp_epos++;

		if (is_identifier(tmp_epos, filter))
		{
			sym.append(1, at(tmp_epos));
			epos = tmp_epos;
		}
	}

	if (out_spos)
		*out_spos = spos;

	if (out_epos)
		*out_epos = epos;

	return sym;
}

wchar_t SharedWStr::safe_at(const int index, CharFilter filter, wchar_t default_char /*= L'\0'*/) const
{
	if (is_valid_pos(index))
		return at(index, filter);

	return default_char;
}

wchar_t SharedWStr::at(const int index, CharFilter filter) const
{
	if (!filter)
		return str_ptr->at(index);
	else
	{
		wchar_t ch = str_ptr->at(index);
		filter(index, ch);
		return ch;
	}
}

bool SharedWStr::is_valid_pos(int pos) const
{
	return pos >= 0 && pos < (int)str_ptr->length();
}

void SharedWStr::assign(const std::wstring& other)
{
	str_ptr.reset(new STLWstrSrc(other));
}

void SharedWStr::assign(LPCWSTR other)
{
	str_ptr.reset(new STLWstrSrc(other));
}

wchar_t SharedWStr::operator[](const size_t index) const
{
	return at((int)index);
}

SharedWStr& SharedWStr::operator=(LPCWSTR other)
{
	str_ptr.reset(new STLWstrSrc(other));
	return *this;
}

SharedWStr& SharedWStr::operator=(const std::wstring& other)
{
	str_ptr.reset(new STLWstrSrc(other));
	return *this;
}

SharedWStr& SharedWStr::operator=(const SharedWStr& other)
{
	if (this != &other)
		str_ptr = other.str_ptr;

	return *this;
}

SharedWStr::SharedWStr(LPCWSTR in_str) : str_ptr(new STLWstrSrc(in_str))
{
}

SharedWStr::SharedWStr(const std::wstring& in_str) : str_ptr(new STLWstrSrc(in_str))
{
}

SharedWStr::SharedWStr() : str_ptr(new STLWstrSrc())
{
}

SharedWStr::SharedWStr(const SharedWStr& other) : str_ptr(other.str_ptr)
{
}

SharedWStr::SharedWStr(IWstrSrc::Ptr sptr) : str_ptr(sptr)
{
}

bool SharedWStr::is_line_continuation(wchar_t ch, wchar_t ch_next)
{
	if (ch != '\\')
		return false;

	return ch_next == '\r' || ch_next == '\n';
}

int SharedWStr::get_line_continuation_length(int pos) const
{
	int str_len = (int)length();
	if (pos >= 0 && pos < str_len)
	{
		wchar_t ch = str_ptr->at(pos);
		if (ch != '\\')
			return 0;

		if (pos + 1 < str_len)
		{
			ch = str_ptr->at(pos + 1);
			if (ch == '\n')
				return 1;

			if (ch == '\r')
			{
				if (pos + 2 < str_len)
				{
					ch = str_ptr->at(pos + 2);
					if (ch == '\n')
						return 2;
				}
				return 1;
			}
		}
	}

	return 0;
}

std::wstring SharedWStr::substr(int start, int len, CharFilter filter) const
{
	if (len == 0)
		return L"";

	if (start + len > (int)length())
		len = (int)length() - start;

	if (!filter)
	{
		return str_ptr->substr(start, len);
	}

	std::wostringstream wss;

	int end = start + len;

	if (end > (int)length())
		end = (int)length();

	for (int i = start; i < end; i++)
	{
		wchar_t ch = str_ptr->at(i);
		if (filter(i, ch))
			wss << ch;
		else if (end < (int)length())
			end++;
	}

	return wss.str();
}

int SharedWStr::find_virtual_start(int pos, bool return_pos /*= false*/) const
{
	int stopper = rfind(pos - 1, [](wchar_t ch) { return ch == '\r' || ch == '\n' || !wt_isspace(ch); });

	//		following code would do what is needed,
	//		but as invalid index is -1, if we add
	//		a +1, result is correct 0, so it is not
	//		necessary as working code has same result
	//
	// 		if (stopper == -1)
	// 		{
	// 			if (pos > 0)
	// 				return 0;
	//
	// 			return -1;
	// 		}

	if (return_pos)
		return stopper + 1;
	else if (stopper + 1 < pos)
		return stopper + 1;

	return -1;
}

int SharedWStr::find_virtual_end(int pos, bool return_pos /*= false*/) const
{
	int stopper = find(pos + 1, [](wchar_t ch) { return !wt_isspace(ch); });

	// if stopper is EOF, return length
	if (stopper == -1)
		return (int)length();

	// we have found next element, which is a start
	// of any non-white text, so what we want
	// to do now, is to find its virtual start
	// and our virtual end is right before the
	// first char of it

	int svs = find_virtual_start(stopper, true);

	if (return_pos)
		return svs - 1;
	else if (svs - 1 > pos)
		return svs - 1;

	return -1;
}

std::wstring SharedWStr::substr_se(int s_pos, int e_pos, CharFilter filter) const
{
	if (e_pos < s_pos && e_pos + 1 == s_pos) // empty Smart Select range
		return L"";

	_ASSERTE(e_pos >= s_pos);

	return substr(s_pos, e_pos - s_pos + 1, filter);
}

std::wstring SharedWStr::read_str(int start, CharFilterEx filter, int step /*= 1*/) const
{
	bool stop = false;
	std::vector<wchar_t> wss;

	if (step > 0)
	{
		int str_len = (int)length();
		for (int i = start; !stop && i < str_len; i += step)
		{
			wchar_t ch = str_ptr->at(i);
			if (filter(i, ch, stop))
				wss.push_back(ch);
		}

		return std::wstring(&wss.front(), wss.size());
	}
	else if (step < 0)
	{
		for (int i = start; !stop && i >= 0; i += step)
		{
			wchar_t ch = str_ptr->at(i);
			if (filter(i, ch, stop))
				wss.push_back(ch);
		}

		std::reverse(wss.begin(), wss.end());
		return std::wstring(&wss.front(), wss.size());
	}

	return L"";
}

bool SharedWStr::is_space(int pos, CharFilter filter /*= nullptr*/) const
{
	wchar_t ch = safe_at(pos, filter);
	return ch && wt_isspace(ch);
}

bool SharedWStr::is_EOF(int pos) const
{
	return (int)length() == pos;
}

bool SharedWStr::is_space_or_EOF(int pos, CharFilter filter /*= nullptr*/) const
{
	return is_EOF(pos) || is_space(pos, filter);
}

bool SharedWStr::is_identifier(int pos, CharFilter filter /*= nullptr*/) const
{
	return ISCSYM(safe_at(pos, filter));
}

bool SharedWStr::is_identifier_start(int pos, CharFilter filter /*= nullptr*/) const
{
	wchar_t ch = safe_at(pos, filter);
	return ISCSYM(ch) && !wt_isdigit(ch);
}

bool SharedWStr::is_identifier_boundary(int pos, CharFilter filter /*= nullptr*/) const
{
	wchar_t ch = safe_at(pos, filter);

	if (ISCSYM(ch))
	{
		wchar_t ch_prev = safe_at(pos - 1, filter);

		if (!ISCSYM(ch_prev))
			return !wt_isdigit(ch);

		wchar_t ch_next = safe_at(pos + 1, filter);

		if (!ISCSYM(ch_next))
			return true;
	}

	return false;
}

int SharedWStr::find_nospace(int start, CharFilter filter) const
{
	return find(
	    start, [](wchar_t ch) { return !wt_isspace(ch); }, filter);
}

int SharedWStr::find_nospace(int start, int end, CharFilter filter) const
{
	return find(
	    start, end, [](wchar_t ch) { return !wt_isspace(ch); }, filter);
}

int SharedWStr::rfind_nospace(int start, int end, CharFilter filter) const
{
	return rfind(
	    start, end, [](wchar_t ch) { return !wt_isspace(ch); }, filter);
}

int SharedWStr::rfind_nospace(int start, CharFilter filter) const
{
	return rfind(
	    start, [](wchar_t ch) { return !wt_isspace(ch); }, filter);
}

int SharedWStr::find_csym(int start, CharFilter filter) const
{
	return find(
	    start, [](wchar_t ch) { return ISCSYM(ch); }, filter);
}

int SharedWStr::find_csym(int start, int end, CharFilter filter) const
{
	return find(
	    start, end, [](wchar_t ch) { return ISCSYM(ch); }, filter);
}

int SharedWStr::rfind_csym(int start, int end, CharFilter filter) const
{
	return rfind(
	    start, end, [](wchar_t ch) { return ISCSYM(ch); }, filter);
}

int SharedWStr::rfind_csym(int start, CharFilter filter) const
{
	return rfind(
	    start, [](wchar_t ch) { return ISCSYM(ch); }, filter);
}

int SharedWStr::find_csym(int start, int end, const std::wstring& stoppers, CharFilter filter) const
{
	bool canceled = false;

	int rslt = find(
	    start, end,
	    [&](wchar_t ch) {
		    if (stoppers.find(ch) != std::wstring::npos)
		    {
			    canceled = true;
			    return true;
		    }

		    return ISCSYM(ch);
	    },
	    filter);

	return canceled ? -1 : rslt;
}

int SharedWStr::find_csym(int start, const std::wstring& stoppers, CharFilter filter) const
{
	bool canceled = false;

	int rslt = find(
	    start,
	    [&](wchar_t ch) {
		    if (stoppers.find(ch) != std::wstring::npos)
		    {
			    canceled = true;
			    return true;
		    }
		    return ISCSYM(ch);
	    },
	    filter);

	return canceled ? -1 : rslt;
}

int SharedWStr::rfind_csym(int start, int end, const std::wstring& stoppers, CharFilter filter) const
{
	bool canceled = false;

	int rslt = rfind(
	    start, end,
	    [&](wchar_t ch) {
		    if (stoppers.find(ch) != std::wstring::npos)
		    {
			    canceled = true;
			    return true;
		    }
		    return ISCSYM(ch);
	    },
	    filter);

	return canceled ? -1 : rslt;
}

int SharedWStr::rfind_csym(int start, const std::wstring& stoppers, CharFilter filter) const
{
	bool canceled = false;

	int rslt = rfind(
	    start,
	    [&](wchar_t ch) {
		    if (stoppers.find(ch) != std::wstring::npos)
		    {
			    canceled = true;
			    return true;
		    }
		    return ISCSYM(ch);
	    },
	    filter);

	return canceled ? -1 : rslt;
}

int SharedWStr::find_not_csym(int start, CharFilter filter) const
{
	return find(
	    start, [](wchar_t ch) { return !ISCSYM(ch); }, filter);
}

int SharedWStr::find_not_csym(int start, int end, CharFilter filter) const
{
	return find(
	    start, end, [](wchar_t ch) { return !ISCSYM(ch); }, filter);
}

int SharedWStr::rfind_not_csym(int start, int end, CharFilter filter) const
{
	return rfind(
	    start, end, [](wchar_t ch) { return !ISCSYM(ch); }, filter);
}

int SharedWStr::rfind_not_csym(int start, CharFilter filter) const
{
	return rfind(
	    start, [](wchar_t ch) { return !ISCSYM(ch); }, filter);
}

bool SharedWStr::is_one_of(int pos, const std::wstring& str, CharFilter filter /*= nullptr*/) const
{
	wchar_t ch = safe_at(pos, filter);
	return ch && str.find(ch) != std::wstring::npos;
}

bool SharedWStr::is_not_of(int pos, const std::wstring& str, CharFilter filter /*= nullptr*/) const
{
	return !is_one_of(pos, str, filter);
}

int SharedWStr::find_matching_brace(int pos, CharFilter filter, int min_pos /*= -1*/, int max_pos /*= -1*/) const
{
	if (is_one_of(pos, L"()[]{}<>"))
	{
		LPCSTR brs = "";
		bool reversed = true;
		bool ignore_angles = true;

		switch (safe_at(pos, filter))
		{
		case '<':
			reversed = false;
			[[fallthrough]];
		case '>':
			ignore_angles = false;
			break;
		case '(':
			reversed = false;
			[[fallthrough]];
		case ')':
			brs = "()";
			break;
		case '{':
			reversed = false;
			[[fallthrough]];
		case '}':
			brs = "{}";
			break;
		case '[':
			reversed = false;
			[[fallthrough]];
		case ']':
			brs = "[]";
			break;
		}

		BraceCounter bc(brs, reversed);
		bc.set_ignore_angle_brackets(ignore_angles);

		if (min_pos < 0)
			clamp_to_range(min_pos, false);

		if (max_pos < 0)
			clamp_to_range(max_pos, true);

		int step = reversed ? -1 : 1;

#ifdef _SSPARSE_DEBUG
		std::ostringstream ss_dbg;
		std::wostringstream ss_dbg_str;
#endif

		for (int i = pos; is_valid_pos(i) && i >= min_pos && i <= max_pos; i += step)
		{
			wchar_t ch = at(i, filter);

			if (!ch || wt_isspace(ch))
				continue;

			SS_DBG(ss_dbg_str << ch);

			bc.on_char(ch, i, nullptr);

			SS_DBG(bc.debug_print(ss_dbg));
			SS_DBG(ss_dbg << std::endl);

			if (bc.is_closed())
				return bc.m_pos;
		}

#ifdef _SSPARSE_DEBUG
		auto dbg_str = ss_dbg.str();
		auto dbg_str_str = ss_dbg_str.str();
		std::reverse(dbg_str_str.begin(), dbg_str_str.end());
		int y = 0;
#endif
	}

	return -1;
}

int SharedWStr::find_matching_brace_strict(int pos, CharFilter filter /*= nullptr*/, int min_pos /*= -1*/,
                                           int max_pos /*= -1*/) const
{
	return find_matching_brace_strict_ex(pos, L"", filter, min_pos, max_pos);
}

int VASmartSelect::Parsing::SharedWStr::find_matching_brace_strict_ex(int pos, const std::wstring& stoppers,
                                                                      CharFilter filter /*= nullptr*/,
                                                                      int min_pos /*= -1*/, int max_pos /*= -1*/) const
{
	if (is_one_of(pos, L"()[]{}<>"))
	{
		bool reversed = true;
		bool ignore_angles = true;

		switch (safe_at(pos, filter))
		{
		case '<':
			reversed = false;
			// fall through
		case '>':
			ignore_angles = false;
			break;
		case '(':
		case '{':
		case '[':
			reversed = false;
			break;
		}

		BraceCounter bc("(){}[]", reversed);
		bc.set_ignore_angle_brackets(ignore_angles);

		if (min_pos < 0)
			clamp_to_range(min_pos, false);

		if (max_pos < 0)
			clamp_to_range(max_pos, true);

		int step = reversed ? -1 : 1;

#ifdef _SSPARSE_DEBUG
		std::ostringstream ss_dbg;
		std::wostringstream ss_dbg_str;
#endif

		for (int i = pos; is_valid_pos(i) && i >= min_pos && i <= max_pos; i += step)
		{
			wchar_t ch = at(i, filter);

			if (!ch || wt_isspace(ch))
				continue;

			SS_DBG(ss_dbg_str << ch);

			bc.on_char(ch, i, nullptr);

			SS_DBG(bc.debug_print(ss_dbg));
			SS_DBG(ss_dbg << std::endl);

			if (bc.is_mismatch())
				return -2;

			if (stoppers.find(ch) != std::wstring::npos)
				return -3;

			if (bc.is_closed())
				return bc.m_pos;
		}

#ifdef _SSPARSE_DEBUG
		auto dbg_str = ss_dbg.str();
		auto dbg_str_str = ss_dbg_str.str();
		std::reverse(dbg_str_str.begin(), dbg_str_str.end());
		int y = 0;
#endif
	}

	return -1;
}

int SharedWStr::find_in_scope(int start, int end, BraceCounterPredicate fnc, const char* braces /*= "(){}[]"*/,
                              CharFilter filter /*= nullptr*/) const
{
	clamp_to_range(start);
	clamp_to_range(end, true);

	bool backward = start > end;

	BraceCounter bc(braces, backward);
	bc.set_ignore_angle_brackets(strstr(braces, "<>") == nullptr);

	int step = backward ? -1 : 1;
	int i = start;

	while (backward ? i >= end : i <= end)
	{
		wchar_t ch = str_ptr->at(i);

		if (!filter || filter(i, ch))
		{
			bc.on_char(ch, i, nullptr);

			if (!fnc || fnc(bc))
			{
				// [case: 94946] don't return 'i',
				// predicate can modify 'm_pos' of 'bc',
				// so we need to return bc's position;
				return bc.get_pos();
			}

			if (bc.is_mismatch())
				break;
		}

		i += step;
	}

	return -1;
}

const CharStateIter* CharStateIter::most_nested() const
{
	const CharStateIter* iter = this;

	while (iter->m_interp_csi)
	{
		auto tmp = iter->m_interp_csi.get();

		if (tmp->pos >= 0) // don't include uninitialized
			iter = tmp;
		else
			break;
	}

	return iter;
}

bool CharStateIter::in_neutral_zone() const
{
	return state == chs_none || state == chs_directive;
}

bool CharStateIter::has_state(CharState s) const
{
	return (state & s) == s;
}

bool CharStateIter::has_one_of(CharState s, CharState s1) const
{
	return has_state(s) || has_state(s1);
}

bool CharStateIter::has_one_of(CharState s, CharState s1, CharState s2) const
{
	return has_state(s) || has_state(s1) || has_state(s2);
}

void CharStateIter::set_state(CharState s)
{
	if (state != s && on_state_change)
		on_state_change(pos, state, s);

	state = s;
}

void CharStateIter::set_state_in_neutral_zone(CharState s)
{
	if ((state & chs_directive) == chs_directive)
		set_state((CharState)(state | s));
	else
		set_state(s);
}

bool CharStateIter::next()
{
	if (pos == -1)
	{
		pos = m_sPos;
		line_s_pos = src.line_start(m_sPos);
		ch_prev2 = src.safe_at(pos - 2);
		ch_prev = src.safe_at(pos - 1);
		ch = src.safe_at(pos);
		ch_next = ch ? src.safe_at(pos + 1) : L'\0';
		ch_next2 = ch_next ? src.safe_at(pos + 2) : L'\0';
		ch_next3 = ch_next2 ? src.safe_at(pos + 3) : L'\0';
	}
	else if (pos > m_ePos)
	{
		return false;
	}
	else
	{
		pos++;
		ch_prev2 = ch_prev;
		ch_prev = ch;
		ch = ch_next;
		ch_next = ch_next2;
		ch_next2 = ch_next3;
		ch_next3 = ch_next2 ? src.safe_at(pos + 3) : L'\0';
	}

	// Line start/end handling
	// s_pos is on pos==0 or right after delimiter
	// e_pos is on pos==Length-1 or right before delimiter
	if (line_s_pos <= pos && (pos == m_ePos || ch == '\r' || ch == '\n'))
	{
		int delim_len = 0;

		if (ch == '\r' && ch_next == '\n')
			delim_len = 2;
		else if (ch == '\r' || ch == '\n')
			delim_len = 1;

		if (on_line_end)
		{
			if (pos == m_ePos && !delim_len)
				on_line_end(line_s_pos, pos + 1, 0); // ZERO length delimiter!
			else
				on_line_end(line_s_pos, pos, delim_len);
		}

		line_s_pos = pos + delim_len;
	}

	if (!m_stack.empty())
	{
		set_state(m_stack.top());
		m_stack.pop();
	}

	if (m_skip > 0)
	{
		m_skip--;
		return true;
	}

	if (m_reset_state != reset::do_not)
	{
		if (m_reset_state == reset::to_none)
			set_state(chs_none);
		else if (m_reset_state == reset::to_neutral)
			set_state((CharState)(state & chs_directive));

		m_reset_state = reset::do_not;
	}

	struct auto_white_line
	{
		CharStateIter* csi;
		auto_white_line(CharStateIter* csi_in) : csi(csi_in)
		{
		}
		~auto_white_line()
		{
			if (!wt_isspace(csi->ch))
			{
				// do not consider backslash at line end as a line breaker
				if (csi->ch == '\\' && (csi->ch_next == '\r' || csi->ch_next == '\n'))
					return;

				csi->m_white_line = false;
			}
		}
	} a_w_l(this);

	{
		// ************************************************
		// THIS MUST BE HANDLED VERY FIRST
		// ************************************************
		// Line continuation character has highest priority
		// RAW strings and multi-line comments don't
		// support line continuation char
		// ************************************************
		if (!m_is_cs && !has_one_of(chs_raw_str, chs_c_comment) && ch == '\\' && (ch_next == '\r' || ch_next == '\n'))
		{
			// we are on line continuation character,
			// so just step over it.

			m_stack.push(state);

			set_state((CharState)(state | chs_continuation));
			m_stack.push(state);
			m_skip = 1;

			if (ch_next == '\r' && ch_next2 == '\n')
			{
				m_stack.push(state);
				m_skip = 2;
			}

			return true;
		}

		if (handle_num_literal())
			return true;

		if (has_one_of(chs_sq_string, chs_dq_string) &&
		    (m_escape || (ch == '\\' && (!m_is_cs || (ch_next != '\r' && ch_next != '\n')))))
		{
			m_escape ^= true; // toggle
			return true;
		}

		if (m_interp_str)
		{
			if (m_interp_csi)
			{
				auto inxt = m_interp_csi->next();
				_ASSERTE(inxt);
				_ASSERTE(m_interp_csi->pos == pos);

				if (inxt && m_interp_csi->pos == pos && !m_interp_csi->in_neutral_zone())
				{
					return true;
				}
			}

			if (ch == '{')
			{
				if (ch_next == '{') // or is this {{ escape?
					m_skip = 1;     // then skip escape
				else
				{
					m_interp_br.push(pos);
					if (m_interp_br.size() == 1)
					{
						m_interp_csi = std::make_shared<CharStateIter>(pos + 1, m_ePos, m_is_cs, src);
						m_interp_csi->on_interp = on_interp;

						if (on_interp)
							on_interp(pos, -1, this);
					}
				}

				return true;
			}
			else if (ch == '}')
			{
				if (!m_interp_br.empty())
				{
					int open_pos = m_interp_br.top();
					m_interp_br.pop();

					if (on_interp)
						on_interp(open_pos, pos, this);

					if (m_interp_br.empty())
						m_interp_csi = nullptr;

					return true;
				}
			}
		}

		if (!m_is_cs && cpp_str_start(ch, ch_next, ch_next2, ch_next3) && in_neutral_zone())
		{
			const char* str_start = cpp_str_start(ch, ch_next, ch_next2, ch_next3);
			if (str_start)
			{
				int ss_len = strlen_i(str_start);

				if (strstr(str_start, "R"))
				{
					set_state_in_neutral_zone(chs_raw_str);
					m_skip = ss_len - 1;
				}
				else
				{
					wchar_t ss_ch = ss_len == 1 ? ch_next : (ss_len == 2 ? ch_next2 : ch_next3);
					set_state_in_neutral_zone(ss_ch == '"' ? chs_dq_string : chs_sq_string);
					m_skip = ss_len;
				}
			}

			return true;
		}
		else if (has_state(chs_raw_str))
		{
			if (ch == '"' && raw_str_id.empty())
				raw_str_id = L")"; // start recording starting sequence (exchange '"' and ')')
			else
			{
				_ASSERTE(!raw_str_id.empty());

				if (raw_str_id.back() != '"')
					raw_str_id += ch == '(' ? L'"' : ch; // drop recording of starting sequence (exchange '"' and '(')
				else if (ch == ')')
					raw_str_end = L")"; // start recording ending sequence
				else if (!raw_str_end.empty())
				{
					raw_str_end += ch;

					if (ch == '"')
					{
						if (raw_str_end == raw_str_id) // are start/end sequences equal?
						{
							raw_str_id.clear();                // SUCCESS - clear starting sequence
							m_reset_state = reset::to_neutral; // from the next char, reset state
						}

						raw_str_end.clear(); // drop recording of ending sequence ( mismatch: '"' )
					}
					else if (raw_str_end.length() >= raw_str_id.length())
					{
						raw_str_end.clear(); // drop recording of ending sequence
					}
				}
			}

			return true;
		}
		else if (ch == '#' && state == chs_none && m_white_line)
		{
			if (on_directive)
				on_directive(pos);

			set_state(chs_directive);

			int start_pos = -1;
			ConditionalBlock::DirectiveType dir = ConditionalBlock::get_directive(pos + 1, start_pos, src);

			if (dir != ConditionalBlock::d_none)
			{
				int line_end_pos = src.line_end(start_pos, true);

				if (!conditional || !conditional->process_directive(dir, pos, start_pos, line_end_pos))
				{
					conditional.reset(new ConditionalBlock());
					conditional->dirs.push_back(ConditionalBlock::Directive(dir, pos, start_pos, line_end_pos));

					if (on_new_conditional)
						on_new_conditional(conditional);
				}
			}
		}
		else if ((m_is_cs && cs_str_start(ch, ch_next, ch_next2)) || ch == '"' || ch == '\'')
		{
			if (in_neutral_zone())
			{
				auto cs_str_s = m_is_cs ? cs_str_start(ch, ch_next, ch_next2) : nullptr;
				if (cs_str_s)
				{
					bool isDQ = ch == '"' || (ch == '$' && ch_next == '"');
					set_state_in_neutral_zone(isDQ ? chs_dq_string : chs_verbatim_str);
					m_skip = strlen_i(cs_str_s) - 1;
					m_interp_str = ch == '$';
					m_interp_csi = nullptr;
					if (!m_interp_br.empty())
						m_interp_br = std::stack<int>();
				}
				else
				{
					set_state_in_neutral_zone(ch == '"' ? chs_dq_string : chs_sq_string);
				}
				return true;
			}
			else if (ch == '"')
			{
				if (has_state(chs_dq_string) && !m_escape)
				{
					m_reset_state = reset::to_neutral;
					m_interp_str = false;
					m_interp_csi = nullptr;
					if (!m_interp_br.empty())
						m_interp_br = std::stack<int>();
					return true;
				}
				else if (has_state(chs_verbatim_str))
				{
					if (ch_next == '"')
						m_skip = 1;
					else
						m_reset_state = reset::to_neutral;

					m_interp_csi = nullptr;
					m_interp_str = false;
					if (!m_interp_br.empty())
						m_interp_br = std::stack<int>();
					return true;
				}
			}
			else if (ch == '\'')
			{
				if (has_state(chs_sq_string) && !m_escape)
				{
					m_reset_state = reset::to_neutral;
					return true;
				}
			}
		}
		else if (ch == '/' && ch_next == '/')
		{
			if (in_neutral_zone())
			{
				set_state_in_neutral_zone(chs_comment);
				return true;
			}
		}
		else if (ch == '/' && ch_next == '*')
		{
			if (in_neutral_zone())
			{
				set_state_in_neutral_zone(chs_c_comment);
				return true;
			}
		}
		else if (ch == '/' && ch_prev == '*')
		{
			if (has_state(chs_c_comment))
			{
				m_reset_state = reset::to_neutral;
				return true;
			}
		}
		else if (ch == '\r' || ch == '\n')
		{
			m_white_line = true;
			end_num_literal();

			// in case of directive, always reset in case of free CR or LF
			if (has_state(chs_directive) || !has_one_of(chs_c_comment, chs_verbatim_str))
			{
				set_state(chs_none);
				return true;
			}
		}
	}

	return true;
}

bool CharStateIter::handle_num_literal()
{
	if (state != chs_none)
	{
		end_num_literal();
		return false;
	}

	if (m_num_literal & NUM_SKIP_NEXT)
	{
		m_num_literal = (num_lit)(m_num_literal & ~NUM_SKIP_NEXT);
		return true;
	}

	// suffix in user-defined literal like: 90.0_deg (since C++11)
	if (!m_is_cs && ch == '_' && !(m_num_literal & NUM_SUFFIX) && in_num_literal())
	{
		m_num_literal = (num_lit)(m_num_literal | NUM_SUFFIX);
		return true;
	}

	if (at_digit_separator())
		return true;

	switch (m_num_literal & NUM_TYPE_MASK)
	{
	/////////////////////////////////
	// hexadecimal literals
	case NUM_TYPE_HEX:
		if (!is_x_char(ch))
		{
			end_num_literal();
			return false;
		}
		return true;

	/////////////////////////////////
	// binary literals
	case NUM_TYPE_BIN:
		if (!is_b_char(ch))
		{
			end_num_literal();
			return false;
		}
		return true;

	/////////////////////////////////
	// pp-number and any numbers
	case NUM_TYPE_NUM: {
		// 	The preprocessing number preprocessing token is defined very broadly.
		//
		// 	Effectively it matches any sequence of characters that begins with a digit
		// 	or a decimal point followed by any number of digits, non-digits (e.g.letters),
		// 	and e+ and e- .
		//
		// 	So, all of the following are valid preprocessing number preprocessing tokens :
		//
		// 	1.0e-10
		// 	.78
		// 	42
		// 	1e-X
		// 	1helloworld

		if (!is_num_char(ch))
		{
			end_num_literal();
			return false;
		}
		return true;
	}

	//////////////////////////////////////////
	// When undefined, we are checking,
	// whether we are at one currently.
	default: {
		if (state != chs_none)
		{
			end_num_literal();
			return false;
		}

		// skip identifiers
		if (ISCSYM(ch_prev))
		{
			end_num_literal();
			return false;
		}

		// we need to check for one of 0x 0X 0b 0B
		if (ch == '0')
		{
			// hexadecimal literals (also c++17 floats)
			if ((ch_next == 'x' || ch_next == 'X') && is_x_char(ch_next2))
			{
				start_num_literal((num_lit)(NUM_TYPE_HEX | NUM_SKIP_NEXT));
				return true;
			}

			// binary literals
			if ((ch_next == 'b' || ch_next == 'B') && is_b_char(ch_next2))
			{
				start_num_literal((num_lit)(NUM_TYPE_BIN | NUM_SKIP_NEXT));
				return true;
			}
		}

		// pp-number handler
		if (wt_isdigit(ch) || (ch == '.' && wt_isdigit(ch_next)))
		{
			start_num_literal(NUM_TYPE_NUM);
			return true;
		}
	}
	}

	end_num_literal();
	return false;
}

void CharStateIter::start_num_literal(num_lit type)
{
	end_num_literal();

	m_num_literal = type;
	m_num_literal_start = pos;

	if (on_num_literal)
		on_num_literal(pos, -1, (char)(type & NUM_TYPE_MASK));
}

void CharStateIter::end_num_literal()
{
	if (in_num_literal())
	{
		if (on_num_literal)
			on_num_literal(m_num_literal_start, pos - 1, (char)(m_num_literal & NUM_TYPE_MASK));

		m_num_literal = NUM_NONE;
		m_num_literal_start = -1;
	}
}

bool CharStateIter::at_digit_separator()
{
	if (state != chs_none)
		return false;

	if (!in_num_literal())
		return false;

	if (m_is_cs ? ch != '_' : ch != '\'')
		return false;

	if ((m_num_literal & NUM_TYPE_MASK) == NUM_TYPE_HEX) // hex literal
		return is_x_char(ch_prev, true) && is_x_char(ch_next, true);

	if ((m_num_literal & NUM_TYPE_MASK) == NUM_TYPE_BIN) // bin literal
		return is_b_char(ch_prev, true) && is_b_char(ch_next, true);

	if ((m_num_literal & NUM_TYPE_MASK) == NUM_TYPE_NUM) // pp-number
		return is_num_char(ch_prev, true) && is_num_char(ch_next, true);

	return false;
}

bool CharStateIter::is_num_char(wchar_t _ch, bool separator_boundary /*= false*/)
{
	if (m_is_cs && in_num_literal() && _ch == '_' && !(m_num_literal & NUM_SUFFIX))
		return true;

	if (!separator_boundary)
	{
		if (in_num_literal() && ISCSYM(_ch))
			return true;

		if (!(m_num_literal & NUM_SUFFIX) &&
		    (_ch == '.' || _ch == '-' || _ch == '+' || _ch == '$' || _ch == 'e' || _ch == 'E'))
			return true;

		if (is_num_suffix_char(_ch))
		{
			m_num_literal = (num_lit)(m_num_literal | NUM_SUFFIX);
			return true;
		}
	}

	return !!ISCSYM(_ch);
}

bool CharStateIter::is_num_suffix_char(wchar_t _ch)
{
	if (!in_num_literal())
		return false;

	if (!m_is_cs && (m_num_literal & NUM_SUFFIX))
		return !!ISCSYM(_ch);

	return _ch == 'l' || _ch == 'L' ||              // long
	       _ch == 'u' || _ch == 'U' ||              // unsigned
	       _ch == 'f' || _ch == 'F' ||              // float
	       (m_is_cs && (_ch == 'm' || _ch == 'M')); // C# dynamic
}

bool CharStateIter::is_b_char(wchar_t _ch, bool separator_boundary /*= false*/)
{
	if (m_is_cs && in_num_literal() && _ch == '_' &&
	    !(m_num_literal & NUM_SUFFIX)) // C# separator can be long ____ any count
		return true;

	if (!separator_boundary)
	{
		if (in_num_literal() && (_ch == '0' || _ch == '1'))
			return true;

		if (is_num_suffix_char(_ch))
		{
			m_num_literal = (num_lit)(m_num_literal | NUM_SUFFIX);
			return true;
		}
	}

	return _ch == '0' || _ch == '1';
}

bool CharStateIter::is_x_char(wchar_t _ch, bool separator_boundary /*= false*/)
{
	if (m_is_cs && in_num_literal() && _ch == '_' &&
	    !(m_num_literal & NUM_SUFFIX)) // C# separator can be long ____ any count
		return true;

	if (!separator_boundary)
	{
		if (in_num_literal() && wt_isxdigit(_ch))
			return true;

		// C++17 hex float like: 0x1ffp10, 0xa.bp10l
		// see: http://en.cppreference.com/w/cpp/language/floating_literal
		if (!m_is_cs && !(m_num_literal & NUM_SUFFIX) &&
		    (_ch == '.' || _ch == 'p' || _ch == 'P' || _ch == '+' || _ch == '-'))
			return true;

		if (is_num_suffix_char(_ch))
		{
			m_num_literal = (num_lit)(m_num_literal | NUM_SUFFIX);
			return true;
		}
	}

	return !!wt_isxdigit(_ch);
}

const char* CharStateIter::cs_str_start(wchar_t ch0, wchar_t ch1, wchar_t ch2)
{
	if (ch0 == '$' && ch1 == '"')
		return R"($")";

	if (ch0 == '@' && ch1 == '"')
		return R"(@")";

	if (ch0 == '$' && ch1 == '@' && ch2 == '"')
		return R"($@")";

	return nullptr;
}

const char* CharStateIter::cpp_str_start(wchar_t ch0, wchar_t ch1, wchar_t ch2, wchar_t ch3)
{
	if (ch0 == 'L' && (ch1 == '\"' || ch1 == '\''))
		return "L";

	///////////////////////////////////////////
	// raw literals were introduced in VS2013
	//
	else if (ch0 == 'R' && ch1 == '\"')
		return "R";
	else if (ch0 == 'L')
	{
		if (ch1 == '\"' || ch1 == '\'')
			return "L";
		else if (ch1 == 'R' && ch2 == '\"')
			return "LR";
	}

	/////////////////////////////////////////////
	// unicode literals were introduced in VS2015
	//
	else if (ch0 == 'U')
	{
		if (ch1 == '\"' || ch1 == '\'')
			return "U";
		else if (ch1 == 'R' && ch2 == '\"')
			return "UR";
	}
	else if (ch0 == 'u')
	{
		if (ch1 == '\"' || ch1 == '\'')
			return "u";
		else if (ch1 == 'R' && ch2 == '\"')
			return "uR";
		else if (ch1 == '8')
		{
			if (ch2 == '\"' || ch2 == '\'')
				return "u8";
			else if (ch2 == 'R' && ch3 == '\"')
				return "u8R";
		}
	}

	return nullptr;
}

ConditionalBlock::DirectiveType ConditionalBlock::get_directive(int pos, int& start_pos, SharedWStr src)
{
	start_pos = -1;

	wchar_t ch;
	while ('\0' != (ch = src.safe_at(pos)))
	{
		// Between # and directive name MUST NOT be a backslash,
		// so it is safe to avoid line continuation character handling.

		if (ch == '\n' || ch == '\r')
			break;

		if (!wt_isspace(ch))
		{
			if (ch == 'i' || ch == 'e')
			{
				wchar_t ch_next = src.safe_at(pos + 1);
				if (ch == 'i' && ch_next == 'f')
				{
					start_pos = pos;
					wchar_t ch_next2 = src.safe_at(pos + 2);
					if (ch_next2 == 'd')
						return d_ifdef;
					else if (ch_next2 == 'n')
						return d_ifndef;
					else
						return d_if;
				}
				else if (ch == 'e')
				{
					if (ch_next == 'l')
					{
						start_pos = pos;
						wchar_t ch_next2 = src.safe_at(pos + 2);
						if (ch_next2 == 's')
							return d_else;
						else
							return d_elif;
					}
					else if (ch_next == 'n')
					{
						start_pos = pos;
						return d_endif;
					}
				}
			}

			break;
		}

		pos++;
	}

	return d_none;
}

bool TextLines::PosFromLineAndColumn(ULONG& pos, /* [out] (0 based) position within text */
                                     ULONG line, /* [in] (1 based) line index */
                                     ULONG col /* [in] (1 based) char index within line */) const
{
	// in IDE line starts on pos[0] or on '\n'

	_ASSERTE(line);
	_ASSERTE(col);

	TextLine tline;
	if (GetLine(line, tline))
	{
		pos = tline.start_pos();

		ULONG curr_col = 0;

		for (LPCWSTR ptr = tline.start(); ptr; ptr++, pos++)
		{
			if (++curr_col == col)
				break;

			if (!ptr[0]) // EOF
				break;

			if (IS_SURROGATE_PAIR(ptr[0], ptr[1]))
			{
				ptr++;
				pos++;
			}
		}
	}
	else
	{
		_ASSERTE("Line/Column out of real range!");
		pos = (uint)src.length();
	}

#ifndef SS_SKIPTEST_146208
#ifndef _SSPARSE_DEBUG
	if (gTestsActive)
#endif
	{
		ULONG va_dbg_pos = (ULONG)g_currentEdCnt->GetBufIndex(TERRCTOLONG((long)line, (long)col));

		_ASSERTE(pos == va_dbg_pos);

		if (gTestsActive && gTestLogger)
		{
			static bool had_pos_err = false;
			if (!had_pos_err && pos != va_dbg_pos)
			{
				gTestLogger->LogStr(WTString("!!! pos calculation error !!!"));
				had_pos_err = true;
			}
		}

		LONG va_dbg_l, va_dbg_c;
		g_currentEdCnt->PosToLC((long)pos, va_dbg_l, va_dbg_c);

		if (!in_calc_test)
		{
			ScopeToggle toggle(in_calc_test);
#ifdef _DEBUG
			ULONG dbg_l, dbg_c;
			_ASSERTE(LineAndColumnFromPos(pos, dbg_l, dbg_c));
			_ASSERTE(dbg_l == line);
			_ASSERTE(dbg_c == col);
#endif
		}
	}
#endif // SS_SKIPTEST_146208

	return true;
}

bool TextLines::LineAndColumnFromPos(ULONG pos,           /* [in] (0 based) position within text */
                                     ULONG& line,         /* [out] (1 based) line index */
                                     ULONG& line_ch_index /* [out] (1 based) char index within line */
) const
{
	TextLine text_line;
	if (GetLineFromPos(pos, text_line))
	{
		line_ch_index = 0;
		line = text_line.line_number();

		LPCWSTR ptr = text_line.start();
		LPCWSTR posPtr = ptr + (pos - text_line.start_pos());

		_ASSERTE(posPtr <= text_line.end(true));

		for (; ptr && ptr <= posPtr; ptr++)
		{
			line_ch_index++;

			if (!ptr[0]) // EOF
				break;

			if (IS_SURROGATE_PAIR(ptr[0], ptr[1]))
			{
				ptr++;
			}
		}

#ifndef SS_SKIPTEST_146208
#ifndef _SSPARSE_DEBUG
		if (gTestsActive)
#endif
		{
			LONG va_dbg_l, va_dbg_c;
			g_currentEdCnt->PosToLC((long)pos, va_dbg_l, va_dbg_c);

			_ASSERTE(va_dbg_l == (LONG)line);
			_ASSERTE(va_dbg_c == (LONG)line_ch_index);

			if (gTestsActive && gTestLogger)
			{
				static bool had_line_err = false;
				if (!had_line_err && va_dbg_l != (LONG)line)
				{
					gTestLogger->LogStr(WTString("!!! line calculation error !!!"));
					had_line_err = true;
				}

				static bool had_column_err = false;
				if (!had_column_err && va_dbg_c != (LONG)line_ch_index)
				{
					gTestLogger->LogStr(WTString("!!! column calculation error !!!"));
					had_column_err = true;
				}
			}

			//			ULONG va_dbg_pos =
			//				g_currentEdCnt->GetBufIndex(TERRCTOLONG(line, line_ch_index));

			if (!in_calc_test)
			{
				ScopeToggle toggle(in_calc_test);

#ifdef _DEBUG
				ULONG dbg_pos;
				_ASSERTE(PosFromLineAndColumn(dbg_pos, line, line_ch_index));
				_ASSERTE(dbg_pos == pos);
#endif
			}
		}
#endif // SS_SKIPTEST_146208

		return true;
	}

	return false;
}

bool TextLines::VaLineAndColumnFromPos(ULONG pos, ULONG& va_pos) const
{
	ULONG line, col;
	if (LineAndColumnFromPos(pos, line, col))
	{
		va_pos = TERRCTOLONG(line, col);
		return true;
	}
	return false;
}

ULONG TextLines::GetLineNumberFromPos(ULONG pos) const
{
	TextLine line;
	if (GetLineFromPos(pos, line))
		return line.line_number();
	return 0;
}

bool TextLines::GetLineFromPos(ULONG pos, TextLine& line) const
{
	if (pos > src.length() || lines.empty())
		return false;

	if (pos == 0)
		return GetFirst(line);
	else if (pos == src.length())
		return GetLast(line);

	auto line_contains_pos = [](size_t _pos, LineDataPtr& ptr) {
		size_t s_pos = ptr->s_pos;
		size_t e_pos = (size_t)ptr->e_pos + ptr->delim_len;

		return _pos >= s_pos && _pos < e_pos;
	};

	size_t first = 0;
	size_t last = lines.size() - 1;
	size_t mid;
	LineDataPtr ptr;

	// we use binary search to find line index
	// as line data vector is already sorted

	while (first <= last)
	{
		mid = (first + last) / 2;
		ptr = lines[mid];

		if (line_contains_pos(pos, ptr))
		{
			line.m_data = ptr;
			line.m_line_num = uint(mid + 1);
			line.m_src = src;

			return true;
		}
		else if (pos < ptr->s_pos)
		{
			last = mid - 1;
		}
		else
		{
			first = mid + 1;
		}
	}

	return false;
}

bool TextLines::GetNext(TextLine& line) const
{
	return GetLine(line.m_line_num + 1, line);
}

bool TextLines::GetPrevious(TextLine& line) const
{
	return GetLine(line.m_line_num - 1, line);
}

bool TextLines::HaveNext(const TextLine& line) const
{
	return is_valid_line_number(line.line_number() + 1);
}

bool TextLines::HavePrevious(const TextLine& line) const
{
	return is_valid_line_number(line.line_number() - 1);
}

bool TextLines::GetPrevious(const TextLine& before, TextLine& line) const
{
	return GetLine(before.m_line_num - 1, line);
}

bool TextLines::GetNext(const TextLine& after, TextLine& line) const
{
	return GetLine(after.m_line_num + 1, line);
}

bool TextLines::GetLast(TextLine& line) const
{
	return GetLine(LinesCount(), line);
}

bool TextLines::GetFirst(TextLine& line) const
{
	return GetLine(1, line);
}

bool TextLines::GetLine(ULONG line_num, TextLine& line) const
{
	if (is_valid_line_number(line_num))
	{
		line.m_data = lines[line_num - 1];
		line.m_src = src;
		line.m_line_num = line_num;
		return true;
	}
	return false;
}

ULONG TextLines::LinesCount() const
{
	return (ULONG)lines.size();
}

TextLines::TextLines(SharedWStr s) : src(s)
{
	lines.push_back(LineDataPtr(new LineData(0)));
}

bool TextLines::is_valid_line_number(ULONG index) const
{
	return index > 0 && index <= lines.size();
}

void TextLines::add_line(ULONG s, ULONG e, BYTE dl)
{
	lines.back()->set(s, e, dl);

	if (dl > 0)
	{
		lines.push_back(LineDataPtr(new LineData(e + dl)));
	}
}

void TextLines::SelectionRanges(ULONG s_pos, ULONG e_pos, std::vector<ULONG>& vec) const
{
	TextLine line;
	ULONG s, e;
	if (GetLineFromPos(s_pos, line))
	{
		do
		{
			s = line.start_pos();

			if (s >= e_pos)
				break;

			e = line.end_pos();

			if (line.delim_length())
				e++;

			if (s < s_pos)
				s = s_pos;

			if (e > e_pos)
				e = e_pos;

			vec.push_back(s);
			vec.push_back(e);

		} while (GetNext(line));
	}
}

void TextLines::DoLineColumnTest()
{
#ifndef SS_DOLCTEST
	if (!gTestsActive)
		return;
#endif

#ifndef SS_SKIPTEST_146208
#ifndef _SSPARSE_DEBUG
	if (gTestsActive)
#endif
	{
		EdCntPtr ed(g_currentEdCnt);

		if (!ed)
			return;

		static EdCnt* prev_ed = nullptr;
		static int prev_cookie = 0;
		static uint prev_hash = 0;

		if (prev_ed != ed.get() || prev_cookie != ed->m_modCookie)
		{
			prev_ed = ed.get();
			prev_cookie = ed->m_modCookie;

			WTString buff = ed->GetBufConst();
			uint buff_hash = buff.hash();

			if (prev_hash != buff_hash)
			{
				prev_hash = buff_hash;

				ULONG line, column;
				for (ULONG i = 0; i <= (ULONG)buff.length(); i++)
					LineAndColumnFromPos(i, line, column);
			}
		}
	}
#endif // SS_SKIPTEST_146208
}
