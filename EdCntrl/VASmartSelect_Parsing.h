#ifndef VASmartSelect_Parsing_h__
#define VASmartSelect_Parsing_h__

#pragma once

#include "stdafxed.h"

#include <memory>
#include <stack>
#include <vector>
#include <string>
#include <regex>
#include <functional>
#include <xutility>
#include <set>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <mutex>

#include "FileLineMarker.h"
#include "DevShellAttributes.h"
#include "Settings.h"

#define SS_NODEBUG
#define SS_PARALLEL
#define SS_DOLCTEST 
#define SS_SKIPTEST_146208 // blocks some code while case: 146208 is unresolved #SmartSelect_Case_146208 

#ifdef SS_PARALLEL
#include <ppl.h>
#define SS_FOR_EACH concurrency::parallel_for_each
#define IF_SS_PARALLEL(x) x
#else
#define SS_FOR_EACH std::for_each
#define IF_SS_PARALLEL(x)
#endif

#if defined(_DEBUG) && !defined(SS_NODEBUG)
#define _SSPARSE_DEBUG
#define _SSPARSE_DEBUG_POS_PROPS
#define IF_DBG(x) x
#else
#define IF_DBG(x)
#endif

void _SSDbgTraceW(LPCWSTR lpszFormat, ...);
void _SSDbgTraceA(LPCSTR lpszFormat, ...);

#ifdef _SSPARSE_DEBUG
#define SS_DBG(x) x
#define SS_DBG_TRACE_W _SSDbgTraceW
#define SS_DBG_TRACE_A _SSDbgTraceA
#else
#define SS_DBG(x)
#define SS_DBG_TRACE_W(...)
#define SS_DBG_TRACE_A(...)
#endif

struct ScopeToggle
{
	bool& val;
	ScopeToggle(bool& v) : val(v)
	{
		val ^= true;
	}
	~ScopeToggle()
	{
		val ^= true;
	}
};

struct MatchWrapp
{
	std::wsmatch* m;
	int m_offset;
	mutable std::wstring m_str;

	MatchWrapp() : m(nullptr), m_offset(0)
	{
	}

	int position() const
	{
		return m ? (int)m->position() + m_offset : 0;
	}
	int length() const
	{
		return m ? (int)m->length() : 0;
	}

	std::wstring& str() const
	{
		if (m_str.empty() && m)
			m_str = m->str();
		return m_str;
	}
};

void regex_for_each(const std::wstring& input, const std::wregex& rgx, std::function<bool(const MatchWrapp&)> fnc,
                    std::regex_constants::match_flag_type flags = std::regex_constants::match_default);

void regex_replace_lambda(std::wstring& out, const std::wstring& input, std::wregex& rgx,
                          std::function<const wchar_t*(const std::wsmatch&, bool&)> fnc,
                          std::regex_constants::match_flag_type flags = std::regex_constants::match_default);

namespace VASmartSelect
{
struct SSContext;
struct SSContextBase;
class CInvertExpression;
} // namespace VASmartSelect

#define BEGIN_VA_SMART_SELECT_PARSING_NS                                                                               \
	namespace VASmartSelect                                                                                            \
	{                                                                                                                  \
	namespace Parsing                                                                                                  \
	{
#define END_VA_SMART_SELECT_PARSING_NS                                                                                 \
	}                                                                                                                  \
	}
#define USING_VA_SMART_SELECT_PARSING_NS using namespace VASmartSelect::Parsing;

BEGIN_VA_SMART_SELECT_PARSING_NS

class BraceCounter;

struct IWstrSrc
{
	typedef std::shared_ptr<IWstrSrc> Ptr;

	virtual ~IWstrSrc() = default;

	virtual wchar_t at(int index) const = 0;
	virtual std::wstring substr(int start, int len) const = 0;
	virtual LPCWSTR c_str() const = 0;
	virtual size_t length() const = 0;
	virtual bool empty() const = 0;
};

class STLWstrSrc : public IWstrSrc
{
	std::wstring wstr;

  public:
	STLWstrSrc()
	{
	}
	virtual ~STLWstrSrc() = default;
	STLWstrSrc(const std::wstring& in_str) : wstr(in_str)
	{
	}
	STLWstrSrc(LPCWSTR in_str) : wstr(in_str)
	{
	}

	virtual wchar_t at(int index) const
	{
		return wstr.at((uint)index);
	}

	virtual std::wstring substr(int start, int len) const
	{
		return wstr.substr((uint)start, (uint)len);
	}

	virtual LPCWSTR c_str() const
	{
		return wstr.c_str();
	}
	virtual size_t length() const
	{
		return wstr.length();
	}
	virtual bool empty() const
	{
		return wstr.empty();
	}
};

enum CharState : unsigned char
{
	chs_none = 0, // do not change, is used like: if(!state) ...
	chs_any = 0xFF,

	chs_dq_string = 0x01,    //: " " or L" "
	chs_sq_string = 0x02,    //: ' ' or L' '
	chs_verbatim_str = 0x04, //: @" "
	chs_raw_str = 0x08,      //: R" " or LR" "
	chs_c_comment = 0x10,    //: /* */
	chs_comment = 0x20,      //: //
	chs_directive = 0x40,    //: # directives
	chs_continuation = 0x80, //: line continuation - in C++ backslash followed by line break

	chs_string_mask = chs_dq_string | chs_sq_string | chs_verbatim_str | chs_raw_str,
	chs_comment_mask = chs_comment | chs_c_comment,
	chs_boundary_mask = chs_string_mask | chs_comment_mask | chs_directive,
	chs_no_code = chs_comment_mask | chs_string_mask | chs_continuation
};

template <typename TEnum> class EnumMask
{
  public:
	TEnum state;

	EnumMask(DWORD initial_state = 0) : state((TEnum)initial_state)
	{
	}
	EnumMask(TEnum initial_state) : state(initial_state)
	{
	}

	bool Contains(TEnum flag) const
	{
		return (state & flag) == flag;
	}

	bool Contains(DWORD flag) const
	{
		return Contains((TEnum)flag);
	}

	bool Equals(TEnum flag) const
	{
		return state == flag;
	}

	bool Equals(DWORD flag) const
	{
		return Equals((TEnum)flag);
	}

	bool HasAnyBitOfMask(TEnum flag) const
	{
		return (state & flag) != 0;
	}

	bool HasAnyBitOfMask(DWORD flag) const
	{
		return HasAnyBitOfMask((TEnum)flag);
	}

	EnumMask& Set(TEnum flag)
	{
		state = flag;
		return *this;
	}

	EnumMask& Set(DWORD flag)
	{
		return Set((TEnum)flag);
	}

	operator TEnum() const
	{
		return state;
	}

	EnumMask& operator=(TEnum flag)
	{
		return Set(flag);
	}

	bool operator==(TEnum flag) const
	{
		return Equals(flag);
	}
	bool operator!=(TEnum flag) const
	{
		return !Equals(flag);
	}

	bool operator==(const EnumMask& other) const
	{
		return other.state == state;
	}
	bool operator!=(const EnumMask& other) const
	{
		return other.state != state;
	}

	EnumMask& Add(TEnum flag)
	{
		state = (TEnum)(state | flag);
		return *this;
	}

	EnumMask& Add(DWORD flag)
	{
		return Add((TEnum)flag);
	}

	EnumMask& Remove(TEnum flag)
	{
		state = (TEnum)(state & ~flag);
		return *this;
	}

	EnumMask& Remove(DWORD flag)
	{
		return Remove((TEnum)flag);
	}
};

template <typename TEnum> class EnumMaskList
{
	typedef EnumMask<TEnum> MaskT;
	typedef std::vector<MaskT> VectorT;

	VectorT m_vec;
	std::mutex m_mutex;

  public:
	EnumMaskList()
	{
	}
	EnumMaskList(size_t size) : m_vec(size)
	{
	}
	EnumMaskList(const EnumMaskList& src, int start_index, int end_index)
	    : m_vec(src.m_vec.begin() + start_index, src.m_vec.begin() + end_index)
	{
	}

	void Set(const int nIndex, TEnum val)
	{
		std::lock_guard<std::mutex> _guard(m_mutex);

		if (IsValidIndex(nIndex))
			m_vec[(uint)nIndex].Set(val);
	}

	void Set(const int nIndex, DWORD val)
	{
		Set(nIndex, (TEnum)val);
	}

	const MaskT UnsafeAt(const int nIndex) const
	{
		return m_vec.at((uint)nIndex);
	}

	const MaskT SafeAt(const int nIndex) const
	{
		if (IsValidIndex(nIndex))
			return UnsafeAt(nIndex);
		else
		{
			_ASSERTE("Subscript out of range!");

			// return empty
			return MaskT(0);
		}
	}

	bool IsValidIndex(const int nIndex) const
	{
		return nIndex >= 0 && nIndex < (int)m_vec.size();
	}

	bool Contains(const int nIndex, TEnum flag) const
	{
		if (!IsValidIndex(nIndex))
			return false;

		return UnsafeAt(nIndex).Contains(flag);
	}

	bool Contains(const int nIndex, DWORD flag) const
	{
		return Contains(nIndex, (TEnum)flag);
	}

	bool Equals(const int nIndex, TEnum flag) const
	{
		if (!IsValidIndex(nIndex))
			return false;

		return UnsafeAt(nIndex).Equals(flag);
	}

	bool Equals(const int nIndex, DWORD flag) const
	{
		return Equals(nIndex, (TEnum)flag);
	}

	bool HasAnyBitOfMask(const int nIndex, TEnum flag) const
	{
		if (!IsValidIndex(nIndex))
			return false;

		return UnsafeAt(nIndex).HasAnyBitOfMask(flag);
	}

	bool HasAnyBitOfMask(const int nIndex, DWORD flag) const
	{
		return HasAnyBitOfMask(nIndex, (TEnum)flag);
	}

	/////////////////////////////////
	// Standard methods/operators
	/////////////////////////////////

	void clear()
	{
		m_vec.clear();
	}
	size_t size() const
	{
		return m_vec.size();
	}
	bool empty() const
	{
		return m_vec.empty();
	}
	void resize(size_t new_size)
	{
		m_vec.resize(new_size);
	}
	typename VectorT::iterator begin()
	{
		return m_vec.begin();
	}
	typename VectorT::iterator end()
	{
		return m_vec.end();
	}

	const MaskT at(const int nIndex) const
	{
		return SafeAt(nIndex);
	}
	const MaskT operator[](const int nIndex) const
	{
		return at(nIndex);
	}
};

class WStrFilterIterator
{
  public:
	typedef std::function<bool(LPCWSTR)> FilterFnc;

	typedef std::iterator_traits<LPCWSTR>::iterator_category underlying_category;

	typedef std::conditional<std::is_same<underlying_category, std::random_access_iterator_tag>::value,
	                         std::bidirectional_iterator_tag, underlying_category>::type iterator_category;

	typedef std::iterator_traits<LPCWSTR>::value_type value_type;
	typedef std::iterator_traits<LPCWSTR>::reference reference;
	typedef std::iterator_traits<LPCWSTR>::pointer pointer;
	typedef std::iterator_traits<LPCWSTR>::difference_type difference_type;

  private:
	LPCWSTR m_iter;
	LPCWSTR m_end;
	LPCWSTR m_begin;
	FilterFnc m_pred;

  public:
	WStrFilterIterator() : m_iter(), m_end(), m_pred()
	{
	}

	WStrFilterIterator(LPCWSTR begin, LPCWSTR end, FilterFnc pred = FilterFnc())
	    : m_iter(begin), m_end(end), m_begin(begin), m_pred(pred)
	{
		while (m_iter < m_end && !m_pred(m_iter))
		{
			++m_iter;
		}
	}

	WStrFilterIterator(LPCWSTR wstr, int begin, int end, FilterFnc pred = FilterFnc())
	    : m_iter(wstr + begin), m_end(wstr + end), m_begin(wstr + begin), m_pred(pred)
	{
		while (m_iter < m_end && !m_pred(m_iter))
		{
			++m_iter;
		}
	}

	WStrFilterIterator end() const
	{
		return WStrFilterIterator(m_end, m_end, m_pred);
	}

	FilterFnc predicate() const
	{
		return m_pred;
	}
	LPCWSTR base_end() const
	{
		return m_end;
	}
	LPCWSTR base_begin() const
	{
		return m_begin;
	}
	LPCWSTR base() const
	{
		return m_iter;
	}
	unsigned int base_offset() const
	{
		assert(m_iter >= m_begin);
		return uint(m_iter - m_begin);
	}
	reference operator*() const
	{
		return *m_iter;
	}
	pointer operator->() const
	{
		return m_iter;
	}

	WStrFilterIterator& operator++()
	{
		if (m_iter >= m_end)
		{
			m_iter = m_end;
			return *this;
		}

		++m_iter;
		while (m_iter < m_end && !m_pred(m_iter))
		{
			++m_iter;
		}
		return *this;
	}

	WStrFilterIterator operator++(int)
	{
		WStrFilterIterator temp(*this);
		++*this;
		return temp;
	}

	WStrFilterIterator& operator--()
	{
		if (m_iter < m_begin)
		{
			m_iter = m_end;
			return *this;
		}

		--m_iter;
		while (m_iter >= m_begin && !m_pred(m_iter))
		{
			--m_iter;
		}
		return *this;
	}

	WStrFilterIterator operator--(int)
	{
		WStrFilterIterator temp(*this);
		--*this;
		return temp;
	}

	bool operator==(const WStrFilterIterator& f) const
	{
		return m_iter == f.m_iter;
	}
	bool operator!=(const WStrFilterIterator& f) const
	{
		return m_iter != f.m_iter;
	}
	bool operator>=(const WStrFilterIterator& f) const
	{
		return m_iter >= f.m_iter;
	}
	bool operator<=(const WStrFilterIterator& f) const
	{
		return m_iter <= f.m_iter;
	}
	bool operator>(const WStrFilterIterator& f) const
	{
		return m_iter > f.m_iter;
	}
	bool operator<(const WStrFilterIterator& f) const
	{
		return m_iter < f.m_iter;
	}
};

struct SharedWStr
{
	// ([out] buffer position, [in, out] char)
	// returns "true" if char value should be processed,
	// if "false" is returned, char is not counted nor added
	typedef std::function<bool(int, wchar_t&)> CharFilter;

	// used to find char
	// returns 'true' if wanted char equals to passed value
	typedef std::function<bool(wchar_t)> CharPredicate;
	typedef std::function<bool(int, wchar_t)> CharPredicateEx;
	typedef std::function<bool(BraceCounter&)> BraceCounterPredicate;

	// ([out] buffer position, [in, out] char, [in, out] stop)
	// returns "true" if char value should be processed,
	// if "false" is returned, char is not counted nor added
	typedef std::function<bool(int, wchar_t&, bool&)> CharFilterEx;

	struct ScopedFnc
	{
		typedef std::function<void()> Func;
		Func fnc;

		ScopedFnc(Func f) : fnc(f)
		{
		}
		~ScopedFnc()
		{
			if (fnc)
				fnc();
		}
	};

	IWstrSrc::Ptr str_ptr;

	SharedWStr();
	SharedWStr(const std::wstring& in_str);
	SharedWStr(LPCWSTR in_str);
	SharedWStr(IWstrSrc::Ptr sptr);
	SharedWStr(const SharedWStr& other);

	SharedWStr& operator=(const SharedWStr& other);
	SharedWStr& operator=(const std::wstring& other);
	SharedWStr& operator=(LPCWSTR other);

	void assign(const std::wstring& other);
	void assign(LPCWSTR other);

	wchar_t operator[](const size_t index) const;

	void clear()
	{
		str_ptr.reset();
	}

	wchar_t safe_at(const int index, CharFilter filter = nullptr, wchar_t default_char = L'\0') const;
	wchar_t at(const int index, CharFilter filter = nullptr) const;

	std::wstring substr(int start, int len, CharFilter filter = nullptr) const;
	std::wstring read_str(int start, CharFilterEx filter, int step = 1) const;

	LPCWSTR c_str() const;

	size_t length() const;
	bool empty() const;

	static bool is_line_continuation(wchar_t ch, wchar_t ch_next);

	bool is_one_of(int pos, const std::wstring& str, CharFilter filter = nullptr) const;
	bool is_not_of(int pos, const std::wstring& str, CharFilter filter = nullptr) const;

	bool is_space(int pos, CharFilter filter = nullptr) const;
	bool is_EOF(int pos) const;
	bool is_space_or_EOF(int pos, CharFilter filter = nullptr) const;
	int get_line_continuation_length(int pos) const;

	bool is_identifier(int pos, CharFilter filter = nullptr) const;
	bool is_identifier_start(int pos, CharFilter filter = nullptr) const;
	bool is_identifier_boundary(int pos, CharFilter filter = nullptr) const;

	bool is_valid_pos(int pos) const;

	bool line_is_whitespace(int pos, CharFilter filter = nullptr, bool check_backslash = false) const;
	int prev_line_start(int pos, bool check_backslash = false) const;
	int prev_line_end(int pos, bool check_backslash = false) const;
	bool is_on_line_start(int pos, bool check_backslash = false) const;
	bool is_on_line_end(int pos, bool check_backslash = false) const;

	// like 'is_on_line_end' but it returns true also on '\r' in '\r\n'
	bool is_on_line_delimiter(int pos, bool check_backslash = false) const;
	bool is_within_line_continuation(int pos) const;

	int find_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space, CharFilter filter = nullptr) const;
	int rfind_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space, CharFilter filter = nullptr) const;

	bool is_at_scope_delimiter_start(int pos, bool is_cs, bool allow_preceding_space,
	                                 CharFilter filter = nullptr) const;
	bool is_at_scope_delimiter_end(int pos, bool is_cs, bool allow_preceding_space, CharFilter filter = nullptr) const;
	bool is_at_scope_delimiter(int pos, bool is_cs, bool allow_surrounding_space, CharFilter filter = nullptr) const;

	int find_call_delimiter_start(int pos, bool is_cs, bool allow_preceding_space, CharFilter filter = nullptr) const;
	int rfind_call_delimiter_start(int pos, bool is_cs, bool allow_preceding_space, CharFilter filter = nullptr) const;
	bool is_at_call_delimiter(int pos, bool is_cs, bool allow_surrounding_space, CharFilter filter = nullptr) const;

	int line_start(int pos, bool check_backslash = false) const;
	int line_end(int pos, bool check_backslash = false) const;
	int next_line_start(int pos, bool check_backslash = false) const;

	void clamp_to_range(int& pos, bool is_end = false) const;

	int find_matching_brace(int pos, CharFilter filter = nullptr, int min_pos = -1, int max_pos = -1) const;
	int find_matching_brace_strict(int pos, CharFilter filter = nullptr, int min_pos = -1, int max_pos = -1) const;
	int find_matching_brace_strict_ex(int pos, const std::wstring& stoppers, CharFilter filter = nullptr,
	                                  int min_pos = -1, int max_pos = -1) const;

	// Search stops on mismatch and steps over nested scopes
	// To search backwards, 'start' must be greater than 'end'.
	// You can set BraceCounter to mismatch state to stop iteration.
	int find_in_scope(int start, int end, BraceCounterPredicate fnc, const char* braces = "(){}[]",
	                  CharFilter filter = nullptr) const;

	int rfind(int start, int end, CharPredicate fnc, CharFilter filter = nullptr) const;
	int find(int start, int end, CharPredicate fnc, CharFilter filter = nullptr) const;
	int rfind(int start, CharPredicate fnc, CharFilter filter = nullptr) const;
	int find(int start, CharPredicate fnc, CharFilter filter = nullptr) const;

	int find_str(int start, int end, const std::wstring& wstr, CharFilter filter = nullptr) const;
	int find_str(int start, const std::wstring& wstr, CharFilter filter = nullptr) const;
	int rfind_str(int start, int end, const std::wstring& wstr, CharFilter filter = nullptr) const;
	int rfind_str(int start, const std::wstring& wstr, CharFilter filter = nullptr) const;

	int rfind(int start, int end, wchar_t ch, CharFilter filter = nullptr) const;
	int find(int start, int end, wchar_t ch, CharFilter filter = nullptr) const;
	int rfind(int start, wchar_t ch, CharFilter filter = nullptr) const;
	int find(int start, wchar_t ch, CharFilter filter = nullptr) const;

	int rfind_one_of(int start, int end, const std::wstring& chars, CharFilter filter = nullptr) const;
	int find_one_of(int start, int end, const std::wstring& chars, CharFilter filter = nullptr) const;
	int rfind_one_of(int start, const std::wstring& chars, CharFilter filter = nullptr) const;
	int find_one_of(int start, const std::wstring& chars, CharFilter filter = nullptr) const;

	int rfind_one_not_of(int start, int end, const std::wstring& chars, CharFilter filter = nullptr) const;
	int find_one_not_of(int start, int end, const std::wstring& chars, CharFilter filter = nullptr) const;
	int rfind_one_not_of(int start, const std::wstring& chars, CharFilter filter = nullptr) const;
	int find_one_not_of(int start, const std::wstring& chars, CharFilter filter = nullptr) const;

	int find_nospace(int start, CharFilter filter = nullptr) const;
	int rfind_nospace(int start, CharFilter filter = nullptr) const;
	int find_nospace(int start, int end, CharFilter filter = nullptr) const;
	int rfind_nospace(int start, int end, CharFilter filter = nullptr) const;

	int find_csym(int start, CharFilter filter) const;
	int find_csym(int start, int end, CharFilter filter) const;
	int rfind_csym(int start, int end, CharFilter filter) const;
	int rfind_csym(int start, CharFilter filter) const;

	int find_csym(int start, const std::wstring& stoppers, CharFilter filter) const;
	int find_csym(int start, int end, const std::wstring& stoppers, CharFilter filter) const;
	int rfind_csym(int start, int end, const std::wstring& stoppers, CharFilter filter) const;
	int rfind_csym(int start, const std::wstring& stoppers, CharFilter filter) const;

	int find_not_csym(int start, CharFilter filter) const;
	int find_not_csym(int start, int end, CharFilter filter) const;
	int rfind_not_csym(int start, int end, CharFilter filter) const;
	int rfind_not_csym(int start, CharFilter filter) const;

	int find_virtual_start(int pos, bool return_pos = false) const;
	int find_virtual_end(int pos, bool return_pos = false) const;

	std::wstring substr_se(int s_pos, int e_pos, CharFilter filter = nullptr) const;
	std::wstring line_text(int pos) const;
	std::wstring get_symbol(int pos, bool handle_line_continuation = true, int* out_spos = nullptr,
	                        int* out_epos = nullptr, CharFilter filter = nullptr) const;

	CharFilter make_char_filter(const EnumMaskList<CharState>& states, CharState mask = chs_no_code,
	                            wchar_t default_char = ' ') const
	{
		return [mask, default_char, &states](int pos, wchar_t& ch) -> bool {
			if (states[pos].HasAnyBitOfMask(mask))
				ch = default_char; // instead of comment/literal/continuation return space
			return true;
		};
	}
};

struct ConditionalBlock
{
	struct Directive;
	typedef std::shared_ptr<ConditionalBlock> Ptr;
	typedef std::pair<int, ConditionalBlock*> PathItem;
	typedef std::vector<PathItem> Path;

	enum DirectiveType
	{
		d_none,

		d_if = 0x10,
		d_ifdef = 0x11,
		d_ifndef = 0x12,

		d_else = 0x20,
		d_elif = 0x21,

		d_endif = 0x40
	};

	enum PositionRelation
	{
		pos_invalid = -1,
		pos_is_in = 0,
		pos_is_over = 1,
		pos_is_under = 2,
	};

	static DirectiveType get_directive(int pos, int& start_pos, SharedWStr src);

	struct Directive
	{
		DirectiveType type;
		int top_pos; // position of #
		int kw_pos;  // position of keyword
		int line_end_pos;

		Directive(DirectiveType t, int topPos, int kwPos, int brPos)
		    : type(t), top_pos(topPos), kw_pos(kwPos), line_end_pos(brPos)
		{
		}

		std::wstring get_name()
		{
			switch (type)
			{
			case d_if:
				return L"#if";
			case d_ifdef:
				return L"#ifdef";
			case d_ifndef:
				return L"#ifndef";
			case d_else:
				return L"#else";
			case d_elif:
				return L"#elif";
			case d_endif:
				return L"#endif";
			default:
				break;
			}
			return L"";
		}

		std::vector<ConditionalBlock> sub_blocks;
	};

	std::wstring get_name()
	{
		std::wstring name;
		for (auto& d : dirs)
		{
			if (name.empty())
				name = d.get_name();
			else
				name += L" " + d.get_name();
		}
		return name;
	}

	bool process_directive(DirectiveType type, int top_pos, int kw_pos, int line_end_pos)
	{
		if (is_complete())
			return false;

		ConditionalBlock* parent = find_incomplete_block();

		_ASSERTE(parent != nullptr);
		_ASSERTE(!parent->is_complete());

		if (parent == nullptr || parent->is_complete())
			return false;

		if (type & d_if)
		{
			ConditionalBlock new_cb;
			new_cb.dirs.push_back(Directive(type, top_pos, kw_pos, line_end_pos));
			parent->dirs.back().sub_blocks.push_back(new_cb);
			return true;
		}

		parent->dirs.push_back(Directive(type, top_pos, kw_pos, line_end_pos));
		return true;
	}

	bool is_complete() const
	{
		return !dirs.empty() && dirs.back().type == d_endif;
	}

	ConditionalBlock* find_incomplete_block()
	{
		if (is_complete())
			return nullptr;

		if (dirs.empty())
			return this;

		Directive& last_dir = dirs.back();
		if (!last_dir.sub_blocks.empty())
		{
			ConditionalBlock* cb = last_dir.sub_blocks.back().find_incomplete_block();
			if (cb)
				return cb;
		}

		return this;
	}

	int directive_by_pos(int pos, bool check_headers = true)
	{
		int index = -1;
		for (auto& dir : dirs)
		{
			if (pos >= dir.top_pos)
			{
				if (check_headers && pos <= dir.line_end_pos)
				{
					return dir.type == d_endif ? index : index + 1;
				}

				if (dir.type == d_endif)
					return -1;

				index++;
			}
			else if (index >= 0)
				return index;
		}

		return index;
	}

	PositionRelation get_pos_relation(int pos)
	{
		if (dirs.empty())
		{
			_ASSERTE(!"Invalid ConditionalBlock");
			return pos_invalid;
		}

		if (dirs.front().top_pos > pos)
			return pos_is_over;
		else if (dirs.back().top_pos <= pos)
			return pos_is_under;

		return pos_is_in;
	}

	bool build_path(int pos, Path& path)
	{
		typedef PathItem pair;

		int dir_id = directive_by_pos(pos);
		if (dir_id >= 0)
		{
			path.push_back(pair(dir_id, this));

			for (ConditionalBlock& cb : dirs[(uint)dir_id].sub_blocks)
				if (cb.build_path(pos, path))
					return true;

			return true;
		}

		return false;
	}

	std::vector<Directive> dirs;
};

class TextLine
{
	struct Data
	{
		typedef std::shared_ptr<Data> Ptr;

		ULONG s_pos;    // first non-CRLF char on line
		ULONG e_pos;    // first CRLF char on line
		BYTE delim_len; // CRLF length (0, 1 or 2) Note: 0 is at last line

		Data(ULONG s, ULONG e, BYTE d) : s_pos(s), e_pos(e), delim_len(d)
		{
		}
		Data(ULONG s) : s_pos(s), e_pos(s), delim_len(0)
		{
		}

		void set(ULONG s, ULONG e, BYTE d)
		{
			s_pos = s;
			e_pos = e;
			delim_len = d;
		}
	};

	friend class TextLines;

	Data::Ptr m_data;
	ULONG m_line_num;
	SharedWStr m_src;

	TextLine(Data::Ptr d, ULONG num, SharedWStr src) : m_data(d), m_line_num(num), m_src(src)
	{
	}

  public:
	TextLine() : m_line_num(0)
	{
	}

	bool operator<(const TextLine& r) const
	{
		return m_line_num < r.m_line_num;
	}

	ULONG line_number() const
	{
		return m_line_num;
	}
	ULONG start_pos() const
	{
		return m_data->s_pos;
	}
	ULONG end_pos() const
	{
		return m_data->e_pos;
	}

	ULONG delim_length() const
	{
		return m_data->delim_len;
	}
	ULONG length() const
	{
		return m_data->e_pos - m_data->s_pos;
	}

	ULONG total_end_pos() const
	{
		return m_data->e_pos + m_data->delim_len;
	}
	ULONG total_length() const
	{
		return length() + m_data->delim_len;
	}

	LPCWSTR start() const 
	{
		return m_src.c_str() + start_pos();
	}

	LPCWSTR end(bool include_delimiter = false) const
	{
		return m_src.c_str() + (include_delimiter ? end_pos() + m_data->delim_len : end_pos());
	}
};

class TextLines
{
	friend class Parser;
	friend struct VASmartSelect::SSContext;
	friend class VASmartSelect::CInvertExpression;

	typedef TextLine::Data LineData;
	typedef TextLine::Data::Ptr LineDataPtr;
	typedef std::vector<LineDataPtr> LineDataVec;

	SharedWStr src;
	LineDataVec lines;

	mutable bool in_calc_test = false;

	bool is_valid_line_number(ULONG index) const;
	void DoLineColumnTest();

  public:
	void add_line(ULONG s, ULONG e, BYTE dl);

	typedef std::shared_ptr<TextLines> Ptr;

	TextLines(SharedWStr s);

	ULONG LinesCount() const;
	bool GetLine(ULONG line_num, TextLine& line) const;
	bool GetFirst(TextLine& line) const;
	bool GetLast(TextLine& line) const;
	bool GetNext(TextLine& line) const;
	bool GetPrevious(TextLine& line) const;
	bool GetNext(const TextLine& after, TextLine& line) const;
	bool GetPrevious(const TextLine& before, TextLine& line) const;
	bool HaveNext(const TextLine& line) const;
	bool HavePrevious(const TextLine& line) const;

	bool GetLineFromPos(ULONG pos, TextLine& line) const;

	ULONG GetLineNumberFromPos(ULONG pos) const;

	bool PosFromLineAndColumn(ULONG& pos, // [out]	(0 based) position within text
	                          ULONG line, // [in]		(1 based) line number
	                          ULONG col   // [in]		(1 based) column number
	) const;

	bool LineAndColumnFromPos(ULONG pos,   // [in]		(0 based) position within text
	                          ULONG& line, // [out]	(1 based) line number
	                          ULONG& col   // [out]	(1 based) column number
	) const;

	bool VaLineAndColumnFromPos(ULONG pos,    // [in]		(0 based) position within text
	                            ULONG& va_pos // [out]	VA position TERRCTOLONG
	) const;

	void SelectionRanges(ULONG s_pos, ULONG e_pos, std::vector<ULONG>& vec) const;
};

template <typename TVal> class ThreadSafeStack
{
	std::stack<TVal> stack;
	mutable std::mutex mtx;

  public:
	const TVal& top() const
	{
		std::lock_guard<std::mutex> _lock(mtx);
		return stack.top();
	}

	TVal& top()
	{
		std::lock_guard<std::mutex> _lock(mtx);
		return stack.top();
	}

	bool empty() const
	{
		return stack.empty();
	}

	void push(const TVal& val)
	{
		std::lock_guard<std::mutex> _lock(mtx);
		return stack.push(val);
	}

	void pop()
	{
		std::lock_guard<std::mutex> _lock(mtx);
		return stack.pop();
	}
};

struct CodeBlock
{
	typedef std::shared_ptr<CodeBlock> Ptr;

	struct PtrComparer
	{
		bool operator()(const Ptr& ls, const Ptr& rs) const;
	};

	typedef std::set<Ptr, PtrComparer> PtrSet;
	typedef EnumMaskList<CharState> CharStates;

	struct auto_fnc_name
	{
		auto_fnc_name(LPCSTR name)
		{
#ifdef _DEBUG
			CodeBlock::s_fnc_name.push(name);
#endif
		}
		~auto_fnc_name()
		{
#ifdef _DEBUG
			CodeBlock::s_fnc_name.pop();
#endif
		}
	};

	static std::shared_ptr<auto_fnc_name> PushFunctionName(LPCSTR fnc_name)
	{
		return std::shared_ptr<auto_fnc_name>(new auto_fnc_name(fnc_name));
	}

	enum block_type : uint
	{
		b_type_none = 0,

		b_csharp = 0x0100,           // C# specific
		b_parent = 0x0200,           // can be parent
		b_scope = 0x0400 | b_parent, // name [(...)] { ... }

		b_type_mask = 0x00ff,

		b_if_group = 0x1000,
		b_try_group = 0x2000,
		b_ms_try_group = 0x4000,
		b_group_mask = 0x7000,

		b_if = 1 | b_parent | b_if_group, // if (...) ...;
		b_if_scope = b_if | b_scope,      // if (...) { ... }

		b_else_if = 2 | b_parent | b_if_group, // else if (...) ...;
		b_else_if_scope = b_else_if | b_scope, // else if (...) { ... }

		b_else = 3 | b_parent | b_if_group, // else ...;
		b_else_scope = b_else | b_scope,    // else { ... }

		b_for = 4 | b_parent,          // for (...) ...;
		b_for_scope = b_for | b_scope, // for (...) { ... }

		b_for_each = 5 | b_parent,               // C++/CLI for each (...) ...;
		b_for_each_scope = b_for_each | b_scope, // C++/CLI for each (...) { ... }

		b_cs_foreach = b_for_each | b_csharp,        // foreach (...) ...;
		b_cs_foreach_scope = b_cs_foreach | b_scope, // foreach (...) { ... }

		b_while = 6 | b_parent,            // while (...) ...;
		b_while_scope = b_while | b_scope, // while (...) { ... }

		b_switch = 7 | b_scope, // switch {...}
		b_case = 8 | b_parent,  // case value: ... [break;|continue;|return;]

		b_case_scope = b_case | b_scope, // "virtual type" case value: { ... } [break;|continue;|return;]

		b_do = 9 | b_parent,      // do ...;
		b_do_scope = 9 | b_scope, // do (...) { ... }

		b_try = 10 | b_scope | b_try_group,     // try { ... }
		b_catch = 11 | b_scope | b_try_group,   // catch (...) { ... }  OR catch { ... }
		b_finally = 12 | b_scope | b_try_group, // C# or C++/CLI finally { ... }

		b_local_scope = 13 | b_scope, // { ... }

		b_class = 14 | b_scope,     // class ... { ... }
		b_struct = 15 | b_scope,    // struct ... { ... }
		b_namespace = 16 | b_scope, // namespace ... { ... }
		b_union = 17 | b_scope,     // union ... { ... }
		b_enum = 18 | b_scope,      // enum ... { ... }

		b_cpp_lambda = 19 | b_scope,               // [...] (...) ... {...}
		b_cs_lambda = 20 | b_csharp,               // (...) => ...;
		b_cs_lambda_scope = b_cs_lambda | b_scope, // (...) => {...}
		b_cs_delegate = 21 | b_csharp | b_scope,   // delegate (...) {...}

		b_method_decl = 22,                     // ... name(...)...;
		b_method_def = b_method_decl | b_scope, // ... name(...) {...}

		b_method_call = 22, // name(...)

		b_cs_property = 23 | b_scope | b_csharp,     // type name {...}
		b_cs_property_get = 24 | b_scope | b_csharp, // get[{...}|;]
		b_cs_property_set = 25 | b_scope | b_csharp, // set[{...}|;]

		b_break = 26,
		b_return = 27,
		b_continue = 28,
		b_goto = 29,

		b_rounds = 30,  // ( ... )
		b_squares = 31, // [ ... ]
		b_angles = 32,  // < ... >

		b_vc_try = 33 | b_scope | b_ms_try_group,     // MS Specific: __try { ... }
		b_vc_except = 34 | b_scope | b_ms_try_group,  // MS Specific: __except (...) { ... }
		b_vc_finally = 35 | b_scope | b_ms_try_group, // MS Specific: __finally { ... }

		b_vc_asm = 36,                       // _asm ...     or __asm ...
		b_vc_asm_scope = b_vc_asm | b_scope, // _asm { ... } or __asm { ... }

		b_va_group = 37,        // special internal type used to group siblings
		b_code_block = 38,      // for non C style languages
		b_directive = 39,       // preprocessor directives
		b_directive_group = 40, // preprocessor directives group

		b_cs_using = 41 | b_parent,              // using (...) ...;
		b_cs_using_scope = b_cs_using | b_scope, // using (...) { ... }

		b_cs_lock = 42 | b_parent,             // lock (...) ...;
		b_cs_lock_scope = b_cs_lock | b_scope, // lock (...) { ... }

		b_cs_unsafe = 43 | b_parent | b_scope, // unsafe { ... }

		b_cpp_modifier = 44, // public: private: protected:

		b_comment = 45, // comment by language

		b_garbage = 46, // block to be deleted
		b_declaration = 47,
		b_definition = 48,
		b_parameter = 49,
		b_type_and_name = 50,   // block to be split
		b_numeric_literal = 51, // any number
		b_string_literal = 52,  // string literal
		b_data_type = 53,       // data type
		b_identifier = 54,      // identifier
		b_ternary_part = 55,    // part of ternary condition
		b_expression = 56,      // some statement

		b_cs_interp_expr = 57 | b_csharp, // C# interpolated expression {expr.}

		//---------------------------------------------------------------------------------------------

		b_full_stmt = 0x02000000,        // full statement
		b_dont_split = 0x04000000,       // final, don't split
		b_dont_split_scope = 0x08000000, // don't split what is inside of braces
		b_not_block = 0x10000000,        // not to be a part of block selection
		b_va_outline = 0x20000000,       // is a result of ResolveVAOutlineBlock
		b_char_state = 0x40000000,       // if this bit is set, type = CharState mask + b_char_state

		b_ex_attr_mask = 0x7f000000, // extended attributes expect b_undefined

		//----------------------------------------------------------------------------------------------

		b_undefined = 0x80000000
	};

	mutable std::wstring name;
	mutable block_type type;

#ifdef _SSPARSE_DEBUG
	mutable std::wstring dbg_text;
#endif

	wchar_t open_char;
	bool processed = false;

	mutable int virtual_start = -1; // lowest position where block virtually "contains" position
	mutable int virtual_end = -1;   // highest position where block virtually "contains" position
	mutable bool virtual_resolved = false;

#ifndef _SSPARSE_DEBUG_POS_PROPS
	int top_pos;
	int open_pos;
	int close_pos;
	int semicolon_pos; // in some cases may be pos of colon
#else
	int dbg_top_pos;
	int dbg_open_pos;
	int dbg_close_pos;
	int dbg_semicolon_pos;

	int get_dbg_top_pos() const
	{
		return dbg_top_pos;
	}
	int get_dbg_open_pos() const
	{
		return dbg_open_pos;
	}
	int get_dbg_close_pos() const
	{
		return dbg_close_pos;
	}
	int get_dbg_semicolon_pos() const
	{
		return dbg_semicolon_pos;
	}

	void set_dbg_top_pos(int val)
	{
		dbg_top_pos = val;
		dbg_text = get_text();
	}
	void set_dbg_open_pos(int val)
	{
		dbg_open_pos = val;
		dbg_text = get_text();
	}
	void set_dbg_close_pos(int val)
	{
		dbg_close_pos = val;
		dbg_text = get_text();
	}
	void set_dbg_semicolon_pos(int val)
	{
		dbg_semicolon_pos = val;
		dbg_text = get_text();
	}

	__declspec(property(get = get_dbg_top_pos, put = set_dbg_top_pos)) int top_pos;
	__declspec(property(get = get_dbg_open_pos, put = set_dbg_open_pos)) int open_pos;
	__declspec(property(get = get_dbg_close_pos, put = set_dbg_close_pos)) int close_pos;
	__declspec(property(get = get_dbg_semicolon_pos, put = set_dbg_semicolon_pos)) int semicolon_pos;
#endif

	std::set<int> sub_starts;
	std::set<std::tuple<int, int>> splitters; // <position, length>

#ifdef _DEBUG
	const LPCSTR fnc_name;
	static ThreadSafeStack<LPCSTR> s_fnc_name;

	const int index;
	static int s_index;
#endif

	PtrSet children;
	CodeBlock* parent = nullptr;
	CodeBlock::Ptr direct_group;
	CodeBlock::Ptr prefix_comment;
	CodeBlock::Ptr suffix_comment;

	bool comments_resolved = false;

	SharedWStr src_code;

	CodeBlock(SharedWStr src, wchar_t t, int top_pos, int end_pos);
	CodeBlock(SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos);
	CodeBlock(SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos, int semicolon);

	CodeBlock(block_type bt, SharedWStr src, wchar_t t, int top_pos, int end_pos);
	CodeBlock(block_type bt, SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos);
	CodeBlock(block_type bt, SharedWStr src, wchar_t t, int top_pos, int open_pos, int close_pos, int semicolon);

	void on_init(int id) const; // debug purposes

	int start_pos() const;
	int end_pos() const;

	int virtual_start_pos() const;
	int virtual_end_pos() const;

	void reset_src(int offset, SharedWStr src, bool apply_to_children = true)
	{
		src_code = src;

		if (offset)
		{
			if (top_pos >= 0)
				top_pos += offset;

			if (open_pos >= 0)
				open_pos += offset;

			if (close_pos >= 0)
				close_pos += offset;

			if (semicolon_pos >= 0)
				semicolon_pos += offset;

			if (virtual_start >= 0)
				virtual_start += offset;

			if (virtual_end >= 0)
				virtual_end += offset;

			std::vector<int> vec(sub_starts.begin(), sub_starts.end());

			for (int& i : vec)
				i += offset;

			sub_starts.clear();
			sub_starts.insert(vec.begin(), vec.end());

			std::vector<std::tuple<int, int>> tup_vec(splitters.begin(), splitters.end());
			for (std::tuple<int, int>& tup : tup_vec)
				std::get<0>(tup) += offset;

			splitters.clear();
			splitters.insert(tup_vec.begin(), tup_vec.end());
		}

		if (apply_to_children)
		{
			if (prefix_comment)
				prefix_comment->reset_src(offset, src, true);

			if (suffix_comment)
				suffix_comment->reset_src(offset, src, true);

			if (!children.empty())
			{
				iterate_children_mutable([offset, src](int d, CodeBlock& block) -> bool {
					block.reset_src(offset, src, true);
					return true;
				});
			}
		}
	}

	void clamp_to_min_max(int min_pos, int max_pos)
	{
		if (top_pos != -1)
		{
			if (top_pos < min_pos)
				top_pos = min_pos;
			else if (top_pos > max_pos)
				top_pos = max_pos;
		}

		if (open_pos != -1)
		{
			if (open_pos < min_pos)
				open_pos = min_pos;
			else if (open_pos > max_pos)
				open_pos = max_pos;
		}

		if (close_pos != -1)
		{
			if (close_pos < min_pos)
				close_pos = min_pos;
			else if (close_pos > max_pos)
				close_pos = max_pos;
		}

		if (semicolon_pos != -1)
		{
			if (semicolon_pos < min_pos)
				semicolon_pos = min_pos;
			else if (semicolon_pos > max_pos)
				semicolon_pos = max_pos;
		}
	}

	void get_statement_start_end(std::wstring* name, int& s_pos, int& e_pos) const;

	void get_range_with_comments(int& s_pos, int& e_pos, int caret_pos = -1) const;

	int get_max_end_pos() const;

	bool has_type_bit(block_type t) const
	{
		return (type & t) == t;
	}

	bool is_of_type(block_type t) const
	{
		_ASSERTE((t & b_type_mask) != b_type_none);
		return (type & b_type_mask) == (t & b_type_mask);
	}

	bool is_of_curly_scope_type() const
	{
		return is_of_type(b_if) || is_of_type(b_else) || is_of_type(b_else_if) || is_of_type(b_try) ||
		       is_of_type(b_catch) || is_of_type(b_finally) || is_of_type(b_vc_try) || is_of_type(b_vc_except) ||
		       is_of_type(b_vc_finally) || is_of_type(b_for) || is_of_type(b_switch) || is_of_type(b_do) ||
		       is_of_type(b_while) || is_of_type(b_for_each) || is_of_type(b_cs_foreach) || is_of_type(b_cs_using) ||
		       is_of_type(b_cs_lock) || is_of_type(b_cs_unsafe) || is_of_type(b_namespace) || is_of_type(b_class) ||
		       is_of_type(b_struct) || is_of_type(b_union);
	}

	bool has_type_specified() const
	{
		return (type & b_type_mask) != b_type_none;
	}

	void add_type_bit(block_type t)
	{
		type = (block_type)(type | t);
	}

	void remove_type_bit(block_type t)
	{
		type = (block_type)(type & ~t);
	}

	int group_members_count() const;

	bool is_group_type() const;
	bool is_group_start() const;
	bool is_group_end() const;

	bool is_scope() const
	{
		return open_char == '{';
	}
	bool is_round_scope() const
	{
		return open_char == '(';
	}

	bool is_garbage() const
	{
		return is_of_type(b_garbage);
	}
	void make_garbage()
	{
		type = b_garbage;
	}

	bool is_full_stmt() const
	{
		return has_type_bit(b_full_stmt);
	}

	bool is_directive(bool allow_group = true) const
	{
		return is_of_type(b_directive) || (allow_group && is_of_type(b_directive_group));
	}

	bool is_statement_container(const CodeBlock::CharStates& states) const
	{
		if (open_char_is_one_of("{["))
			return true;

		if (open_char == '(')
		{
			auto filter = src_code.make_char_filter(states);
			return src_code.find(open_pos, close_pos, L';', filter) >= 0;
		}

		return false;
	}

	bool is_modifier() const
	{
		return is_of_type(b_cpp_modifier);
	}

	bool is_top_parent(bool parent_must_have_children) const
	{
		if (is_header())
			return false;

		if (parent_must_have_children)
			return is_scope() || (is_parent_type() && !children.empty());
		else
			return is_scope() || is_parent_type();
	}

	bool starts_by_open() const
	{
		return has_open_close() && start_pos() == open_pos;
	}

	bool ends_by_close() const
	{
		return has_open_close() && end_pos() == close_pos;
	}

	bool is_scoped_case() const
	{
		return is_of_type(b_case) && children.size() == 1 && (*children.cbegin())->is_of_type(b_local_scope);
	}

	bool is_code_block(bool direct = true, bool modif = true, bool decl = true) const
	{
		if (has_type_bit(b_not_block))
			return false;

		return is_top_parent(true) || is_of_type(b_code_block) ||
		       (Psettings->mSmartSelectEnableGranularStart && is_full_stmt()) || (decl && is_of_type(b_declaration)) ||
		       (direct && is_of_type(b_directive)) || (modif && is_of_type(b_cpp_modifier));
	}

	bool open_char_is_one_of(LPCSTR chars) const
	{
		for (LPCSTR p = chars; p && *p; p++)
			if (*p == open_char)
				return true;
		return false;
	}

	bool is_brace_type() const
	{
		return open_char_is_one_of("{[(<");
	}

	bool is_content() const;
	bool is_scope_header() const;
	bool is_header() const;
	bool is_invalid_parent() const;
	bool is_root() const;
	bool is_top_level() const;
	bool is_within(const CodeBlock& other) const;
	bool is_within_scope(const CodeBlock& other) const;

	bool has_open_close() const;

	bool contains(const CodeBlock& other) const;

	bool is_parent_type() const;
	bool is_unresolved_parent(CodeBlock* incoming_child = nullptr) const;
	bool is_resolver() const;
	bool is_resolved_parent(CodeBlock* incoming_child = nullptr) const;

	bool contains(int pos, bool scan_children, bool check_virtual_space) const
	{
		int s_pos = start_pos();
		int e_pos = end_pos();

		if (s_pos == -1 || e_pos == -1)
			return false;

		if (check_virtual_space)
		{
			resolve_virtual_space(false);

			if (virtual_start >= 0 && virtual_start < s_pos)
				s_pos = virtual_start;

			if (virtual_end >= 0 && virtual_end > e_pos)
				e_pos = virtual_end;
		}

		if (pos >= s_pos && pos <= e_pos)
			return true;

		if (scan_children)
		{
			if (prefix_comment && prefix_comment->contains(pos, false, check_virtual_space))
				return true;

			if (suffix_comment && suffix_comment->contains(pos, false, check_virtual_space))
				return true;

			for (const CodeBlock::Ptr& item : children)
				if (item->contains(pos, true, check_virtual_space))
					return true;
		}

		return false;
	}

	bool scope_contains(int pos, bool scan_children) const
	{
		if (open_pos > -1 && pos >= open_pos && pos <= close_pos)
			return true;

		if (scan_children)
		{
			for (const CodeBlock::Ptr& item : children)
			{
				if (item->scope_contains(pos, true))
					return true;
			}
		}

		return false;
	}

	bool is_nonscope_parent_of(const CodeBlock& other) const;
	bool is_nonscope_parent() const;

	bool is_definition() const
	{
		return is_of_type(b_definition);
	}
	bool is_declaration() const
	{
		return is_of_type(b_declaration);
	}
	bool is_definition_header() const
	{
		return parent != nullptr && parent->top_pos == top_pos && parent->is_of_type(b_definition);
	}

	bool scope_contains(const CodeBlock& other) const;
	int depth() const;

	// iterates children not including nested.
	// Lambda: get_next = lambda(const CodeBlock& block)
	void iterate_children(std::function<bool(const CodeBlock&)> fnc) const;

	// iterates children including nested.
	// Lambda: get_next = lambda(int depth, const CodeBlock& block)
	void iterate_children_all(std::function<bool(int, const CodeBlock&)> fnc) const;

	// iterates mutable children including nested.
	// Lambda: get_next = lambda(int depth, const CodeBlock& block)
	// WARNING: changing children hierarchy may and WILL cause issues,
	// DON'T CHANGE Child->Parent relations in this loops!!!
	void iterate_children_mutable(std::function<bool(int, CodeBlock&)> fnc);

	void iterate_siblings(int step, std::function<bool(const CodeBlock&)> fnc, int num_iters = -1) const;

	CodeBlock* sibling_at_offset(int offset);
	const CodeBlock* sibling_at_offset(int offset) const;

	CodeBlock* next_sibling();
	const CodeBlock* next_sibling() const;

	CodeBlock* prev_sibling();
	const CodeBlock* prev_sibling() const;

	const CodeBlock* children_back() const;
	CodeBlock* children_back();

	CodeBlock::Ptr find_parent_candidate(const CodeBlock::PtrSet& blocks) const;
	bool insert_child(CodeBlock::Ptr child);
	void insert_child_direct(CodeBlock::Ptr child);

	block_type get_type_group() const;

	void get_group(std::set<CodeBlock>& items) const;

	bool try_get_text(std::wstring& str_out) const;
	bool try_get_header_text(std::wstring& str_out) const;
	bool try_get_scope_text(std::wstring& str_out) const;

	void ensure_name_and_type(const CodeBlock::CharStates& states, bool is_cs) const;
	void resolve_virtual_space(bool force_update = false) const;

	static void ellipsis_scopes_and_literals(std::wstring& wstr, bool is_cs);
	static void normalize_scope_name(std::wstring& wstr, bool is_cs);

	void ensure_dbg_text() const;

	std::wstring get_text() const;
	std::wstring get_header_text() const;
	std::wstring get_scope_text() const;

	void debug_print(const CodeBlock::CharStates& states, std::wostream& out, bool is_cs, int indent = 0) const;
	std::wstring debug_string(const CodeBlock::CharStates& states, bool is_cs);

	bool operator<(const CodeBlock& r) const;
};

class CodeBlockTree
{
	void resolve(const CodeBlock::CharStates& states, bool is_cs);

	SharedWStr src;

  public:
	CodeBlock::Ptr root;
	CodeBlockTree();

	bool IsEmpty()
	{
		return !root || root->children.empty();
	}
	void Clear()
	{
		root.reset();
		src.clear();
	}

	template <typename TContainer>
	void Init(SharedWStr src_code, const CodeBlock::CharStates& states, TContainer& items, bool do_resolve, bool is_cs)
	{
		src = src_code;
		root.reset(new CodeBlock(src_code, 0, INT_MIN, INT_MAX));

		for (auto& item : items)
			root->insert_child(item);

		if (do_resolve)
			resolve(states, is_cs);
	}

	bool CreatePath(int pos, CodeBlock::PtrSet& path);
};

class BraceCounter
{
  public:
	enum State
	{
		None = 0x00,
		Open = 0x01,
		Closed = 0x02,
		WrongCharState = 0x04,
		Mismatch = 0x08,
		OutOfTextRange = 0x10
	};

	enum StateStatus
	{
		StateValid,

		StateInvalid_Continue,
		StateInvalid_Stop
	};

	struct pos_char
	{
		int pos;
		char ch;
		pos_char(int p, char c) : pos(p), ch(c)
		{
		}
		pos_char() : pos(-1), ch(0)
		{
		}
	};

	bool m_ignore_angs = false;    // if 'true', only braces within 'm_braces' are considered
	std::string m_braces;          // "(){}[]" by default (angles are handled separately)
	std::vector<pos_char> m_stack; // open position + open char
	std::vector<int> m_angs;       // angle braces counting
	bool m_backward = false;       // reverts the meaning of opening and closing chars
	State m_state = None;          // state of counter

	wchar_t m_char = 0; // last char
	int m_pos = -1;     // last position

	std::function<void(char, int)> on_open;       // void( m_backward == false ? open char : close char, pos of char)
	std::function<void(char, int)> on_close;      // void( m_backward == false ? close char : open char, pos of char)
	std::function<void(char, int, int)> on_block; // void(open char, open pos, close pos)
	std::function<void(BraceCounter&)> on_state;  // void(*this)

	// determines whether char passed means, that N previous angle braces should be removed from consideration (negative
	// number removes all) By default, it returns -1 on a semicolon ';' and 0 otherwise
	std::function<int(int, wchar_t)> reset_angs;
	std::function<bool(wchar_t, int)> ang_filter;

	int brace_index(wchar_t ch, int start);
	int open_index(wchar_t ch);
	int close_index(wchar_t ch);
	bool is_match(wchar_t ch_open, wchar_t ch_close);

	void reset()
	{
		m_state = None;
		m_stack.clear();
		m_angs.clear();
		m_char = 0;
		m_pos = -1;
	}

	std::function<StateStatus(CharState)> ApproveState; // allowed states
	BraceCounter(LPCSTR braces = "(){}[]", bool reversed = false);

	static bool is_valid_pos(int pos, int str_len);
	static bool is_open_close(wchar_t ch_open, wchar_t ch_close);

	static void get_blocks(std::function<bool(wchar_t& ch, int& pos)> get_char,
	                       std::function<void(char open_ch, int open_pos, int close_pos)> on_block,
	                       LPCSTR braces = "(){}[]", bool reversed = false)
	{
		if (!get_char || !on_block || !braces)
			return;

		BraceCounter bc(braces, reversed);
		bc.set_ignore_angle_brackets(!strstr(braces, "<>"));
		bc.on_block = on_block;

		wchar_t ch = 0;
		int pos = 0;

		while (get_char(ch, pos))
			bc.on_char(ch, pos, nullptr);
	}

	State get_state()
	{
		return m_state;
	}
	void set_state(State state = None);
	void set_ignore_angle_brackets(bool val = true)
	{
		m_ignore_angs = val;
	}

	wchar_t get_char()
	{
		return m_char;
	}
	int get_pos()
	{
		return m_pos;
	}

	bool on_char(wchar_t ch, int pos, const CodeBlock::CharStates* states);
	bool next_valid_pos(int& pos, int str_len, const CodeBlock::CharStates* states);

	bool is_operator_precondition()
	{
		return m_stack.empty() && m_backward ? (m_char == '<') : (m_char == '>');
	}

	bool is_mismatch()
	{
		return m_state == Mismatch;
	}
	bool is_closed()
	{
		return m_state == Closed;
	}
	bool is_none_or_closed()
	{
		return m_state == None || m_state == Closed;
	}
	bool is_open()
	{
		return m_state == Open;
	}
	bool is_wrongCharState()
	{
		return m_state == WrongCharState;
	}
	bool is_outOfTextRange()
	{
		return m_state == OutOfTextRange;
	}
	bool is_stack_empty()
	{
		return m_stack.empty();
	}
	bool is_angs_empty()
	{
		return m_angs.empty();
	}
	bool is_inside_braces()
	{
		return !is_stack_empty() || !is_angs_empty();
	}

	int stack_size() const
	{
		return (int)m_stack.size();
	}
	int angs_size() const
	{
		return (int)m_angs.size();
	}

	int depth() const
	{
		return stack_size() + angs_size();
	}

	void debug_print(std::ostream& ss);
	void debug_print(std::wostream& ss);

	std::string debug_str();
};

class CharStateIter
{
	static const char* cpp_str_start(wchar_t ch0, wchar_t ch1, wchar_t ch2, wchar_t ch3);
	;
	static const char* cs_str_start(wchar_t ch0, wchar_t ch1, wchar_t ch2);

	enum class reset
	{
		do_not,
		to_none,
		to_neutral
	};
	enum num_lit
	{
		NUM_NONE = 0,

		NUM_TYPE_HEX = 'x',
		NUM_TYPE_BIN = 'b',
		NUM_TYPE_NUM = '.', // float, int, pp-number

		NUM_TYPE_MASK = 0xFF,

		NUM_SUFFIX = 0x100,
		NUM_SKIP_NEXT = 0x200
	};

	std::wstring raw_str_id;
	std::wstring raw_str_end;

	int m_sPos, m_ePos;
	int m_skip = 0;
	bool m_escape = false;
	bool m_interp_str = false;                   // true if inside of C# interpolated string like $"{val}"
	std::stack<int> m_interp_br;                 // how deep we are in { } braces
	std::shared_ptr<CharStateIter> m_interp_csi; // iterator for code in C# interpolated string
	std::stack<CharState> m_stack;

	bool m_is_cs;
	reset m_reset_state = reset::do_not;
	bool m_white_line = true;
	num_lit m_num_literal = NUM_NONE;
	int m_num_literal_start = -1;

	void set_state(CharState s);
	void set_state_in_neutral_zone(CharState s);

	bool handle_num_literal();
	void start_num_literal(num_lit type);
	void end_num_literal();
	bool at_digit_separator();

	bool is_x_char(wchar_t ch, bool separator_boundary = false);
	bool is_b_char(wchar_t ch, bool separator_boundary = false);
	bool is_num_suffix_char(wchar_t ch);
	bool is_num_char(wchar_t ch, bool separator_boundary = false);

  public:
	typedef std::function<wchar_t(int)> func;
	typedef std::function<void(std::shared_ptr<ConditionalBlock>)> cb_func;
	typedef std::function<void(int s_pos)> directive_func;
	typedef std::function<void(int s_pos, int e_pos, int delim_len)> line_func;
	typedef std::function<void(int pos, CharState old_state, CharState new_state)> state_func;
	typedef std::function<void(int s_pos, int e_pos, CharStateIter* iter)> interp_func;
	typedef std::function<void(int s_pos, int e_pos, char num_lit)> num_literal_func;

	std::shared_ptr<ConditionalBlock> conditional;

	wchar_t ch_prev2 = L'\0';
	wchar_t ch_prev = L'\0';
	wchar_t ch = L'\0';
	wchar_t ch_next = L'\0';
	wchar_t ch_next2 = L'\0';
	wchar_t ch_next3 = L'\0';
	int pos = -1;
	CharState state = chs_none;

	int line_s_pos = -1;

	SharedWStr src;

	cb_func on_new_conditional;
	state_func on_state_change;
	directive_func on_directive;
	line_func on_line_end;

	// opening (arg2 = -1) and closing of the expression in interpolated string (first level)
	// in call of this you can assign same lambda for handling of nested expressions
	interp_func on_interp;

	// opening (arg2 = -1) and closing of the numeric literal
	// arg3 = 'x' for HEX, arg3 = 'b' for BIN, arg3 = '.' for other numerics (pp-numbers)
	num_literal_func on_num_literal;

	CharStateIter(int s_pos, int e_pos, bool is_cs, SharedWStr code)
	    : m_sPos(s_pos), m_ePos(e_pos), m_is_cs(is_cs), src(code)
	{
	}

	virtual ~CharStateIter()
	{
	}

	const CharStateIter* most_nested() const;

	bool in_neutral_zone() const;
	bool has_state(CharState s) const;
	bool has_one_of(CharState s, CharState s1) const;
	bool has_one_of(CharState s, CharState s1, CharState s2) const;
	bool in_num_literal() const
	{
		_ASSERTE(!!(m_num_literal & NUM_TYPE_MASK) == m_num_literal_start >= 0);
		return m_num_literal_start >= 0;
	}
	bool next();
};

enum class OpType : USHORT
{
	None = 0,

	//  For more info see Parser::GetOpNfoVector

	//	Name = value,			// op[-1]
	Primary = 0x0001,        // a
	Suffix = 0x0002,         // b
	Prefix = 0x0004,         // c (unary)
	Multiplicative = 0x0008, // d
	Additive = 0x0010,       // e
	Shift = 0x0020,          // f
	Comparison = 0x0040,     // g
	Equality = 0x0080,       // h
	Logical = 0x0100,        // i
	Conditional = 0x0200,    // j
	Assignment = 0x0400,     // k
	Splitter = 0x0800,       // l

	Relational = Equality | Comparison,

	Mask_All = 0x0FFF,

	Mask_All_SS = Mask_All & ~Splitter
};

struct OpNfo
{
	LPCWSTR op = nullptr; // op[-1] defines OpType as 'a' to 'k', see OpType and Parser::GetOpNfoVector
	BYTE len = 0;         // length
	BYTE prec = 0;        // precedence

	OpType getType() const
	{
		// this is valid, see Parser::GetOpNfoVector
		if (op && op[-1])
			return (OpType)(DWORD(1) << DWORD(op[-1] - 'a'));

		return OpType::None;
	}

	bool operator<(const OpNfo& other) const
	{
		if (len == other.len)
		{
			int cmp = wcscmp(op, other.op);

			if (!cmp)
				return prec > other.prec;

			return cmp < 0;
		}

		return len < other.len;
	}
};

class Parser
{
  public:
	static int IndexOf(wchar_t ch, LPCWSTR str, int count = -1);

	static int FindTemplateOpposite(int pos, SharedWStr str, CodeBlock::CharStates& states);
	static bool IsComparisonOperator(LPCWSTR op, bool is_cs);
	static bool IsAssignmentOperator(LPCWSTR op, bool is_cs);
	static bool IsEqualityOperator(LPCWSTR op, bool is_cs);
	static bool IsRelationalOperator(LPCWSTR op, bool is_cs);
	static int GetOperatorPrecedence(LPCWSTR op, bool is_cs, OpType typeMask);
	static const OpNfo* GetOperatorInfo(LPCWSTR op, bool is_cs, OpType typeMask);

	static const std::vector<OpNfo>& GetOpNfoVector(bool is_cs);
	static void MarkCommentsAndContants(SharedWStr str, CodeBlock::CharStates& states, bool is_cs);

	static void MarkCommentsAndContants(SharedWStr str, CodeBlock::CharStates& states, std::map<int, int>* cs_int_exprs,
	                                    std::vector<int>* state_borders, std::vector<ConditionalBlock::Ptr>* cnd_blocks,
	                                    std::vector<int>* directive_starts,
	                                    std::function<void(int, int, char)> numerics, TextLines* lines,
	                                    const std::function<void(int, wchar_t, CharState)>& on_char_state, bool is_cs);

	static void ResolveComments(CodeBlock& block, SharedWStr str, CodeBlock::CharStates& states,
	                            bool allow_comma = false);
	static CodeBlock& ResolveBlock(SharedWStr str, CodeBlock::CharStates& states, CodeBlock& block, bool is_cs,
	                               std::function<bool(wchar_t, int)> ang_filter);
	static CodeBlock& ResolveVAOutlineBlock(FileLineMarker& marker, SharedWStr str, CodeBlock::CharStates& states,
	                                        CodeBlock& block, bool is_cs);

	static void StatementBlocks(SharedWStr str, CodeBlock::CharStates& states,
	                            std::function<void(CodeBlock::Ptr)> inserter, std::function<int(int)> brace_finder,
	                            const CodeBlock& block, int caret_pos, bool is_cs);

	static void BraceBlocksFlood(SharedWStr str, CodeBlock::CharStates& states,
	                             std::function<void(int, CodeBlock::Ptr)>
	                                 inserter, // void(-1 == left outer; 0 == inner; +1 == right outer, block)
	                             std::function<bool(wchar_t, int)> ang_filter, bool is_cs, int pos, bool outer_blocks,
	                             int level = 1, LPCSTR braces = "(){}[]<>");

	static std::wstring ReadCode(SharedWStr str, const CodeBlock::CharStates& states, int start,
	                             std::function<bool(int pos, std::wstring& wstr)> stopper, bool spaces = true,
	                             bool literals = true, bool collapse_braces = true, int step = 1,
	                             LPCWSTR braces = L"({[<");

	static std::pair<std::wstring, std::vector<int>> ReadCodeEx(
	    SharedWStr str, const CodeBlock::CharStates& states, int start,
	    std::function<bool(int pos, std::wstring& wstr)> stopper, bool spaces = true, bool literals = true,
	    bool collapse_braces = true, int step = 1, LPCWSTR braces = L"({[<");
};

template <int prev_size = 1, int next_size = 1, CharState step_over = chs_continuation, CharState filter = chs_no_code>
class CharStatePosIter
{
	static const int arr_size = prev_size + next_size + 1;
	static const int current = prev_size;

	CodeBlock::CharStates& m_states;
	int m_positions[arr_size];

	int& position(int idx)
	{
		_ASSERTE(idx >= 0 && idx < arr_size);
		return m_positions[idx];
	}

	int position(int idx) const
	{
		_ASSERTE(idx >= 0 && idx < arr_size);
		return m_positions[idx];
	}

	EnumMask<CharState> state(int idx) const
	{
		_ASSERTE(idx >= 0 && idx < (int)m_states.size());
		return m_states[idx];
	}

	int find(int start)
	{
		if (start >= 0 && start < (int)m_states.size())
		{
			for (int i = start; i < (int)m_states.size(); i++)
				if (!state(i).HasAnyBitOfMask(step_over))
					return (int)i;
		}

		return -1;
	}

	int rfind(int start)
	{
		if (start >= 0 && start < (int)m_states.size())
		{
			for (int i = start; i >= 0; --i)
				if (!state(i).HasAnyBitOfMask(step_over))
					return (int)i;
		}

		return -1;
	}

  public:
	CharStatePosIter(CodeBlock::CharStates& states) : m_states(states)
	{
		for (int i = 0; i < arr_size; i++)
			position(i) = -1;
	}

	void init(int pos)
	{
		if (curr() != -1)
		{
			if (curr() == pos)
				return;

			for (int i = 1; i <= next_size; i++)
			{
				if (pos == next(i))
				{
					move_to_next(i);
					return;
				}
			}

			for (int i = 1; i <= prev_size; i++)
			{
				if (pos == prev(i))
				{
					move_to_prev(i);
					return;
				}
			}
		}

		position(current) = find(pos);
#pragma warning(push)
#pragma warning(disable : 4127)
		if (arr_size > 1)
		{
			if (prev_size > 0)
				for (int i = 1; i <= prev_size; i++)
					position(current - i) = rfind(position(current - i + 1) - 1);

			if (next_size > 0)
				for (int i = 1; i <= next_size; i++)
					position(current + i) = find(position(current + i - 1) + 1);
		}
#pragma warning(pop)
	}

	int at_offset(int offset = 0) const
	{
		int index = current + offset;

		if (index >= 0 && index < arr_size)
			return position(index);

		return -1;
	}

	bool is_valid_state(int offset = 0) const
	{
		int pos = at_offset(offset);
		if (pos >= 0 && pos < (int)m_states.size())
			return !state(pos).HasAnyBitOfMask(filter);
		return false;
	}

	int curr() const
	{
		return position(current);
	}
	int next(int offset = 1) const
	{
		return at_offset(offset);
	}
	int prev(int offset = 1) const
	{
		return at_offset(-offset);
	}

	bool move_to_next(int count = 1)
	{
		for (int c = 0; c < count; c++)
		{
			for (int i = 1; i < arr_size; i++)
				position(i - 1) = position(i);
			position(arr_size - 1) = find(position(arr_size - 1) + 1);
		}

		return curr() >= 0;
	}

	bool move_to_prev(int count = 1)
	{
		for (int c = 0; c < count; c++)
		{
			for (int i = 1; i < arr_size; i++)
				position(i) = position(i - 1);
			position(0) = rfind(position(0) - 1);
		}

		return curr() >= 0;
	}
};

class SequenceComparer
{
  public:
	struct context;
	typedef std::function<bool(context&)> func;

	struct context
	{
		std::vector<func>::const_iterator curr_cmp_it;
		std::vector<func>::const_iterator end_cmp_it;

		SharedWStr src;
		int pos = -1;
		int max_pos = -1;
		CodeBlock::CharStates& states;

		wchar_t pos_char() const
		{
			return src.safe_at(pos);
		}

		int safe_max_pos() const
		{
			return max_pos >= 0 ? max_pos : (int)src.length() - 1;
		}

		bool is_valid_range() const
		{
			return pos >= 0 && pos <= safe_max_pos();
		}

		int src_length() const
		{
			return max_pos >= 0 ? max_pos + 1 - pos : (int)src.length() - pos;
		}

		LPCWSTR src_begin() const
		{
			return src.c_str() + pos;
		}

		LPCWSTR src_end() const
		{
			return max_pos >= 0 ? src.c_str() + max_pos + 1 : src.c_str() + src.length();
		}

		context(SharedWStr s, int s_pos, int e_pos, CodeBlock::CharStates& ch_states)
		    : src(s), pos(s_pos), max_pos(e_pos), states(ch_states)
		{
		}
	};

	class Quantifier
	{
		int m_min;
		int m_max;
		bool m_lazy;

	  public:
		Quantifier() : m_min(-1), m_max(-1), m_lazy(false)
		{
		}
		Quantifier(int min_count, int max_count = -1) : m_min(min_count), m_max(max_count), m_lazy(false)
		{
		}

		int GetMin() const
		{
			return m_min;
		}
		int GetMax() const
		{
			return m_max;
		}
		bool IsLazy() const
		{
			return m_lazy;
		}

		bool IsInRange(int quantity) const
		{
			if (quantity < m_min && m_min >= 0)
				return false;
			if (quantity > m_max && m_max >= 0)
				return false;
			return true;
		}

		Quantifier& SetLazy(bool lazy = true)
		{
			m_lazy = lazy;
			return *this;
		}
		Quantifier& SetMin(int min_count)
		{
			m_min = min_count;
			return *this;
		}
		Quantifier& SetMax(int max_count)
		{
			m_max = max_count;
			return *this;
		}

		static Quantifier ZeroOrOne()
		{
			return Quantifier(0, 1);
		}
		static Quantifier OneOrMore()
		{
			return Quantifier(1);
		}
		static Quantifier ZeroOrMore()
		{
			return Quantifier(0);
		}
	};

  protected:
	std::vector<func> m_cmps;

  public:
	SequenceComparer()
	{
	}
	~SequenceComparer()
	{
	}

	void Add(std::function<bool(context&)> cmp);
	void AddMove(std::function<bool(wchar_t ch)> fnc, const Quantifier& qant = Quantifier::ZeroOrMore());

	void AddWhiteSpace(const Quantifier& qant = Quantifier::ZeroOrMore());
	void AddIdentifier(const Quantifier& qant = Quantifier::ZeroOrMore());

	void AddAnyOf(const std::wstring& str, const Quantifier& qant = Quantifier::ZeroOrMore());
	void AddAnyNotOf(const std::wstring& str, const Quantifier& qant = Quantifier::ZeroOrMore());

	void AddRegex(std::shared_ptr<std::wregex> rgx);
	void AddRegex(LPCWSTR pattern, bool icase, bool nosubs = true, bool collate = true, bool optimize = true);
	void AddString(const std::wstring& str_to_match, bool ignore_case = false);
	void AddBraceMatch(char brace = '\0',
	                   const std::string& braces = "(){}[]"); // if brace == '\0', brace on current pos is considered

	bool IsMatch(context& ctx) const;
};

END_VA_SMART_SELECT_PARSING_NS

#endif // VASmartSelect_Parsing_h__
