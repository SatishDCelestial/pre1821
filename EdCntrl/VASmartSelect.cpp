#include "stdafxed.h"
#include "VASmartSelect.h"
#include "EdCnt.h"
#include "VAParse.h"
#include "VAAutomation.h"
#include "ArgToolTipEx.h"
#include "PARSE.H"
#include "FDictionary.h"
#include "InferType.h"

#include "VASmartSelect_Parsing.h"
#include "EdcntWPF.h"
#include "FileLineMarker.h"
#include "LiveOutlineFrame.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "IdeSettings.h"
#include "VAThemeUtils.h"
#include "ColorListControls.h"
#include "DevShellService.h"
#include "RegKeys.h"
#include <functional>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

#pragma warning(disable : 4866)

BEGIN_VA_SMART_SELECT_NS

#ifdef _SSPARSE_DEBUG
const bool IN_DEBUG = true;
#else
const bool IN_DEBUG = false;
#endif

namespace SSParse = VASmartSelect::Parsing;

struct StrConstants
{
	std::wstring inner, scope, trimmed, comments, inner_trim, scope_trim, comments_trim;

	StrConstants()
	{
		inner = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (inner text)").Reset();
		scope = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (complete scope)").Reset();
		trimmed = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (trimmed)").Reset();
		comments = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (with comment)").Reset();
		inner_trim = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (inner text trimmed)").Reset();
		scope_trim = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (complete scope trimmed)").Reset();
		comments_trim = FtmBuilder<WCHAR>().MultiMode().Italic().Renderer(1).Text(L"  (with comment trimmed)").Reset();
	}

	static StrConstants s_inst;
};

StrConstants StrConstants::s_inst;

#define SS_WSTR_INNER StrConstants::s_inst.inner
#define SS_WSTR_SCOPE StrConstants::s_inst.scope
#define SS_WSTR_TRIMMED StrConstants::s_inst.trimmed
#define SS_WSTR_WCOMMENTS StrConstants::s_inst.comments
#define SS_WSTR_INNER_TRIM StrConstants::s_inst.inner_trim
#define SS_WSTR_SCOPE_TRIM StrConstants::s_inst.scope_trim
#define SS_WSTR_WCOMMENTS_TRIM StrConstants::s_inst.comments_trim

struct SSContext;

class DbgTimeMeasure
{
	DWORD start_ticks;
	LPCSTR func_name;

  public:
	explicit DbgTimeMeasure(LPCSTR func) : start_ticks(::GetTickCount()), func_name(func)
	{
	}

	~DbgTimeMeasure()
	{
		_SSDbgTraceA("Function: \"%s\" Ticks: \"%lu\"", func_name, ::GetTickCount() - start_ticks);
	}
};

class TabsExpander
{
	int column = 0;

  public:
	std::wstring exp(const std::wstring& wstr, std::vector<size_t>* outPos = nullptr)
	{
		int tab_size = Psettings->TabSize ? (int)Psettings->TabSize : 4;

		std::wstring out_str;

		if (outPos)
			outPos->resize(wstr.length() + 1);

		for (size_t i = 0; i < wstr.length(); i++)
		{
			wchar_t ch = wstr[i];

			if (outPos)
				outPos->at(i) = out_str.length();

			if (ch == '\r' || ch == '\n')
			{
				column = 0;
				out_str += ch;
			}
			else if (ch == '\t')
			{
				for (int x = 0; x < tab_size; x++)
				{
					out_str += L' ';
					if (++column % tab_size == 0)
						break;
				}
			}
			else
			{
				column++;
				out_str += ch;
			}
		}

		if (outPos)
			outPos->back() = out_str.length();

		return out_str;
	}
};

struct SelRng
{
#ifndef _SSPARSE_DEBUG_POS_PROPS
	int s_pos;
	int e_pos;
#else
	std::wstring dbg_str;

	int dbg_s_pos;
	int dbg_e_pos;

	void get_dbg_text(); // defined under SSContext

	void on_change(int s, int e)
	{
		get_dbg_text();
	}

	void on_set(int s, int e)
	{
	}

	int get_dbg_s_pos() const
	{
		return dbg_s_pos;
	}

	int get_dbg_e_pos() const
	{
		return dbg_e_pos;
	}

	void set_dbg_s_pos(int val)
	{
		if (dbg_s_pos != val)
		{
			dbg_s_pos = val;
			on_change(dbg_s_pos, dbg_e_pos);
		}
		on_set(dbg_s_pos, dbg_e_pos);
	}

	void set_dbg_e_pos(int val)
	{
		if (dbg_e_pos != val)
		{
			dbg_e_pos = val;
			on_change(dbg_s_pos, dbg_e_pos);
		}
		on_set(dbg_s_pos, dbg_e_pos);
	}

	__declspec(property(get = get_dbg_s_pos, put = set_dbg_s_pos)) int s_pos;
	__declspec(property(get = get_dbg_e_pos, put = set_dbg_e_pos)) int e_pos;
#endif

	SSParse::CodeBlock::block_type b_type = SSParse::CodeBlock::b_undefined;
	IF_DBG(int b_index = -1);

	enum type
	{
		tDefault = 0x0000,

		tLines = 0x0001,
		tEmbedded = 0x0002,

		tInner = 0x0004,
		tScope = 0x0008,
		tTrimmed = 0x0010,

		tNotForMenu = 0x0020,
		tWithComments = 0x0040,

		tGroup = 0x0080,
		tBlock = 0x0100, // be added as block
		tWOSemicolon = 0x0200,
	};

	SSParse::SharedWStr name;
	type rng_type;
	int priority = 0;
	int id;

	static int global_id;
	static int next_id()
	{
		// #SmartSelect_SelRngInit
		int next = ++global_id;
		return next;
	}

	explicit SelRng(type t = tDefault) : rng_type(t), id(next_id())
	{
		s_pos = -1;
		e_pos = -1;
	}

	SelRng(int sPos, int ePos, type t = tDefault) : rng_type(t), id(next_id())
	{
		s_pos = sPos;
		e_pos = ePos;
	}

	SelRng(const SelRng& r) : b_type(r.b_type), name(r.name), rng_type(r.rng_type), priority(r.priority), id(r.id)
	{
		s_pos = r.s_pos;
		e_pos = r.e_pos;
		IF_DBG(b_index = r.b_index);
	}

	SelRng& operator=(const SelRng& r)
	{
		if (this != &r)
		{
			s_pos = r.s_pos;
			e_pos = r.e_pos;
			id = r.id;
			name = r.name;
			rng_type = r.rng_type;
			b_type = r.b_type;
			priority = r.priority;
			IF_DBG(b_index = r.b_index);
		}

		return *this;
	}

	SelRng& SetName(LPCWSTR new_name)
	{
		name = new_name;
		return *this;
	}

	SelRng& SetName(const std::wstring& new_name)
	{
		name = new_name;
		return *this;
	}

	SelRng& SetPriority(int val)
	{
		priority = val;
		return *this;
	}

	SelRng& add_type(type t)
	{
		rng_type = (type)(rng_type | t);
		return *this;
	}

	SelRng& remove_type(type t)
	{
		rng_type = (type)(rng_type & ~t);
		return *this;
	}

	bool has_type_bit(type t) const
	{
		return (rng_type & t) == t;
	}

	bool is_block() const
	{
		return (rng_type & tBlock) == tBlock;
	}
	bool is_lines() const
	{
		return (rng_type & tLines) == tLines;
	}
	bool is_embedded() const
	{
		return (rng_type & tEmbedded) == tEmbedded;
	}
	bool is_trimmed() const
	{
		return (rng_type & tTrimmed) == tTrimmed;
	}
	bool is_scope() const
	{
		return (rng_type & tScope) == tScope;
	}
	bool is_inner() const
	{
		return (rng_type & tInner) == tInner;
	}
	bool is_exact() const
	{
		return !is_lines();
	}
	bool is_outer() const
	{
		return !is_scope() && !is_inner();
	}
	bool is_with_comments() const
	{
		return (rng_type & tWithComments) == tWithComments;
	}
	bool is_group() const
	{
		return (rng_type & tGroup) == tGroup;
	}
	bool is_not_for_menu() const
	{
		return (rng_type & tNotForMenu) == tNotForMenu;
	}
	bool is_for_menu() const
	{
		return !is_not_for_menu();
	}

	std::wstring type_str(type mask = (type)0xFFFF) const
	{
		std::wstring out_str;

		auto apply = [&](type t, LPCWSTR pName) {
			if ((mask & t) == t && has_type_bit(t))
			{
				if (!out_str.empty())
					out_str += L'|';
				else
					out_str += pName;
			}
		};

		apply(tLines, L"Lines");
		apply(tEmbedded, L"Embedded");
		apply(tInner, L"Inner");
		apply(tScope, L"Scope");
		apply(tTrimmed, L"Trimmed");
		apply(tNotForMenu, L"NotForMenu");
		apply(tWithComments, L"WithComments");
		apply(tGroup, L"Group");
		apply(tBlock, L"Block");

		return out_str;
	}

	std::wstring line_column_string() const;
	int virtual_start() const;
	int virtual_end() const;

	int type_modifs_count(type mask = (type)0xFFFF) const
	{
		int count = 0;

		auto apply = [&](type t) {
			if ((mask & t) == t && has_type_bit(t))
				count++;
		};

		apply(tLines);
		apply(tEmbedded);
		apply(tInner);
		apply(tScope);
		apply(tTrimmed);
		apply(tNotForMenu);
		apply(tWithComments);
		apply(tGroup);
		apply(tBlock);

		return count;
	}

	bool is_single_line() const;

	bool is_pos_on_se(int pos) const
	{
		return s_pos == pos || e_pos == pos;
	}

	bool is_valid() const
	{
		return s_pos >= 0 && e_pos - s_pos >= -1;
	}

	bool is_empty() const
	{
		return e_pos - s_pos == -1;
	}

	bool is_null() const
	{
		return s_pos == -1 && e_pos == -1;
	}

	static const SelRng& get_superior(const SelRng& a, const SelRng& b)
	{
		if (a.is_block() != b.is_block())
			return a.is_block() ? a : b;

		_ASSERTE(a.priority >= 0 && a.priority < 10);
		_ASSERTE(b.priority >= 0 && b.priority < 10);

		// which is preferred -- it has user defined name

		if (a.priority != b.priority)
			return a.priority > b.priority ? a : b;

		// embedded vs. trimmed conflict
		// embedded > trimmed as there was effort to extend

		if (a.is_embedded() != b.is_embedded())
			return a.is_embedded() ? a : b;

		// if block is for menu, it has bigger priority
		// than block that is not for menu

		if (a.is_for_menu() != b.is_for_menu())
			return a.is_for_menu() ? a : b;

		// non-inner has bigger priority
		// as those are original blocks

		if (a.is_inner() != b.is_inner())
			return a.is_inner() ? b : a;

		// preserve as much as possible

		return a.type_modifs_count() > b.type_modifs_count() ? a : b;
	}

	bool operator<(const SelRng& r) const
	{
		int e_diff = abs(e_pos - r.e_pos);
		int s_diff = abs(s_pos - r.s_pos);

		if (e_diff > s_diff)
			return e_pos > r.e_pos;

		return s_pos < r.s_pos;
	}

	bool operator==(const SelRng& r) const
	{
		return equals(r);
	}

	bool contains_visually(const SelRng& r) const;
	bool contains_visually(int pos) const;

	bool contains(int pos) const
	{
		if (is_empty())
			return pos == s_pos;

		return s_pos <= pos && e_pos >= pos;
	}

	bool contains(const SelRng& r) const
	{
		if (r.is_empty())
			return contains(r.s_pos);

		return s_pos <= r.s_pos && e_pos >= r.e_pos;
	}

	bool intersects(const SelRng& r) const
	{
		if (r.is_empty())
			return contains(r.s_pos);

		return (s_pos <= r.s_pos && e_pos >= r.s_pos) || (s_pos <= r.e_pos && e_pos >= r.e_pos);
	}

	bool equals(const SelRng& r) const
	{
		return s_pos == r.s_pos && e_pos == r.e_pos;
	}

	bool extends_end_trimmed(const SelRng& r) const;

	bool extends(const SelRng& r) const
	{
		return contains_visually(r) && !equals(r);
	}
};

int SelRng::global_id = 0;

struct SSContext : public SSContextBase
{
	typedef std::shared_ptr<SSContext> Ptr;

	BlockBuilder bb;

	std::vector<SelRng> sel_rngs;

	// following are protected by pos_cache_mtx
	std::map<int, int> ternary_map;    // map of ? : pairs in ternary expressions
	std::set<int> global_scope_starts; // set of : which are starts of global scopes ::
	std::set<int> deref_ops;           // set of *^&% which are pointer or dereference operators
	std::set<int> return_type_ops;     // set of -> which are return type operators

	// Prevents us to simply return "bool" without setting the report if
	// function has return type split_report, but tries to return bool,
	// compiler does not allow such return type conversion however,
	// split_report can be then used directly as boolean in conditionals.
	// Also this class tests if specific range (start/end) was already
	// tried to be split by specific function.
	// If it already was, this report gives an information about
	// status whether it was successful or not.
	// Status is empty, if it was not even tried yet.
	// Else status is succeeded or not.
	// In VOID methods, success is reported at first access,
	// so they are considered to always succeed.
	class split_report
	{
		int s_pos;
		int e_pos;
		int status;

		split_report(int spos, int epos) : s_pos(spos), e_pos(epos), status(-1)
		{
		}

	  public:
		static split_report create_key(int spos, int epos)
		{
			return split_report(spos, epos);
		}

		split_report(SSContext& ctx, const std::wstring& name, int spos, int epos, bool success)
		    : s_pos(spos), e_pos(epos), status(success ? 1 : 0)
		{
			ctx.solved_stmts[name].insert(*this);
		}

		bool is_empty() const
		{
			return status == -1;
		}

		bool is_success() const
		{
			_ASSERTE(status != -1);
			return status == 1;
		}

		operator bool() const
		{
			return is_success();
		}

		bool operator<(const split_report& rhs) const
		{
			if (s_pos != rhs.s_pos)
				return s_pos < rhs.s_pos;

			return e_pos < rhs.e_pos;
		}
	};

	struct token_data
	{
		int ang_sum = 0;      // < = +1; > = -1; so: <> = 0; <<> = 1; <>> = -1 and so on.
		bool is_tmpl = false; // whether it is template parameter

		std::wstring code_text;    // collapsed text
		std::vector<int> code_pos; // collapsed text char original positions

		int s_pos = -1; // start buffer pos
		int e_pos = -1; // end buffer pos
	};

	std::map<std::wstring, std::set<split_report>> solved_stmts;

	SelRng sel_start;
	bool update_sel_rngs;

	SSParse::CodeBlockTree tree;
	SSParse::CodeBlock::PtrSet full_stmts;

	double start_zoom = 100;

	long top_line, bottom_line;

	SelRng selection;

	int ctx_start, ctx_end;

	LineMarkersPtr markers;
	LineMarkerPath caretPath;

	struct regexes
	{
		std::wregex line_break = std::wregex(LR"(\r\n|\n|\r)", std::regex::ECMAScript | std::regex::optimize);
		std::wregex long_wspace = std::wregex(LR"(\s+)", std::regex::ECMAScript | std::regex::optimize);
		std::wregex directive_inner_wspace =
		    std::wregex(LR"(^#\s*?(?=[^\s]))", std::regex::ECMAScript | std::regex::optimize);
		std::wregex xml_attributes = std::wregex(L"(\\S+)\\s*=\\s*(([\"'])((?:.(?!\\3\\s+(?:\\S+)=|>))+.)\\3)",
		                                         std::wregex::ECMAScript | std::wregex::optimize);
		std::wregex xml_tag = std::wregex(L"<[^>]+>", std::wregex::ECMAScript | std::wregex::optimize);
	} rgx;

	explicit SSContext(DWORD cmdId) : bb(CurrentBlocks())
	{
		init(cmdId);
	}

	static SSContext::Ptr& Get()
	{
		static SSContext::Ptr sContextPtr;
		return sContextPtr;
	}

	static SSContext::Ptr& Get(bool reinit, DWORD cmdId)
	{

		SSContext::Ptr& ptr = Get();

		if (!ptr && reinit)
			ptr.reset(new SSContext(cmdId));
		else if (reinit)
			ptr->init(cmdId);

		return ptr;
	}

	void temp_cleanup()
	{
		text.clear();
		caretPath.clear();
	}

	std::wstring get_line_column_string(int s, int e)
	{
		std::wostringstream ss;

		ULONG sl, sc, el, ec;

		if (lines->LineAndColumnFromPos((ULONG)s, sl, sc) && lines->LineAndColumnFromPos((ULONG)e, el, ec))
		{
			ss << L"[" << sl << L", " << sc << L"] - [" << el << L", " << ec << L"]";
		}
		else
		{
			ss << L"[Error]";
		}

		return ss.str();
	}

	bool is_cs_or_cpp() const
	{
		return is_cs || is_cpp;
	}

	bool is_extend_shrink_block_cmd() const
	{
		return is_extend_cmd() || is_shrink_cmd() || is_block_cmd();
	}

	bool is_extend_shrink_cmd() const
	{
		return is_extend_cmd() || is_shrink_cmd();
	}

	bool is_block_cmd() const
	{
		return is_extend_block_cmd() || is_shrink_block_cmd();
	}

	bool lines_allowed() const
	{
		return (is_block_cmd() && ActiveSettings().BlockIsAllowed(Settings::Allowed::lines)) ||
		       (is_extend_shrink_cmd() && ActiveSettings().ExtendShrinkIsAllowed(Settings::Allowed::lines));
	}

	bool scope_allowed() const
	{
		return (is_block_cmd() && ActiveSettings().BlockIsAllowed(Settings::Allowed::scope)) ||
		       (is_extend_shrink_cmd() && ActiveSettings().ExtendShrinkIsAllowed(Settings::Allowed::scope));
	}

	bool trimmed_allowed() const
	{
		return (is_block_cmd() && ActiveSettings().BlockIsAllowed(Settings::Allowed::trimmed)) ||
		       (is_extend_shrink_cmd() && ActiveSettings().ExtendShrinkIsAllowed(Settings::Allowed::trimmed));
	}

	bool inner_allowed() const
	{
		return (is_block_cmd() && ActiveSettings().BlockIsAllowed(Settings::Allowed::inner)) ||
		       (is_extend_shrink_cmd() && ActiveSettings().ExtendShrinkIsAllowed(Settings::Allowed::inner));
	}

	bool comments_allowed() const
	{
		return (is_block_cmd() && ActiveSettings().BlockIsAllowed(Settings::Allowed::comments)) ||
		       (is_extend_shrink_cmd() && ActiveSettings().ExtendShrinkIsAllowed(Settings::Allowed::comments));
	}

	bool granular_start() const
	{
		return Psettings->mSmartSelectEnableGranularStart;
	}

	bool word_start() const
	{
		return Psettings->mSmartSelectEnableWordStart;
	}

	bool word_start_split_by_case() const
	{
		return word_start() && Psettings->mSmartSelectSplitWordByCase;
	}

	bool word_start_split_by_underscore() const
	{
		return word_start() && Psettings->mSmartSelectSplitWordByUnderscore;
	}

	bool is_one_line_open_close(SSParse::CodeBlock& block) const
	{
		if (block.start_pos() == block.open_pos && block.end_pos() == block.close_pos)
		{
			return text.line_start(block.open_pos) == text.line_start(block.close_pos);
		}
		return false;
	}

	SSParse::SharedWStr::CharFilter make_char_filter(SSParse::CharState mask = SSParse::chs_no_code,
	                                                 wchar_t default_char = ' ') const
	{
		return [this, mask, default_char](int pos, wchar_t& ch) -> bool {
			auto state = ch_states[pos];

			if (state.HasAnyBitOfMask(mask))
			{
				ch = default_char; // instead of comment/literal/continuation return space
			}

			return true;
		};
	}

	void init(DWORD cmdId)
	{
		command = cmdId;
		init_ctx();
	}

	bool init_ctx(bool force_update = false)
	{
		is_initialized = false;
		is_preparing_tree = false;

		EdCntPtr ed(g_currentEdCnt);
#ifndef RAD_STUDIO
		EdCntWPF* ed_wpf = gShellAttr && gShellAttr->IsDevenv10OrHigher() ? dynamic_cast<EdCntWPF*>(ed.get()) : nullptr;
#endif
		WTString fileText;

		if (ed)
		{
			is_cs = ed->m_ftype == CS;
			is_cpp = IsCFile(ed->m_ftype);
			is_xml = ed->m_ftype == XML || ed->m_ftype == XAML;

			kCurPos = ed->CurPos();
			fileText = ed->GetBuf(TRUE);
			caret_pos = ed->GetBufIndex(fileText, (long)kCurPos);
		}
		else
		{
			_ASSERTE(!"No EdCnt available!");
			return false;
		}

		RemovePadding_kExtraBlankLines(fileText);

		if (caret_pos >= fileText.GetLength())
			caret_pos = fileText.GetLength() - 1;

		text = fileText.Wide();
		uint file_hash = fileText.hash();

		if (!markers || file_hash != buffer_hash)
		{
			buffer_hash = file_hash;
			markers.reset();
			ch_states.clear();
			cb_vec.clear();
			dir_starts_vec.clear();
			lines.reset();
			sel_rngs.clear();
			cs_interp.clear();
			num_literals.clear();

			// defines whether we need to rebuild also context tree
			ctx_start = caret_pos;
			ctx_end = caret_pos;

			LiveOutlineFrame* loFrame = nullptr;

			if (gVaService)
			{
				loFrame = gVaService->GetOutlineFrame();
				if (loFrame)
				{
					LineMarkersPtr olm = loFrame->GetMarkers();
					if (olm && olm->mModCookie == ed->m_modCookie)
						markers.reset(new LineMarkers(*olm));
				}
			}

			auto prepare_markers = [this, &fileText, ed]() {
				if (!markers)
				{
					markers.reset(new LineMarkers());
					MultiParsePtr mparse = ed->GetParseDb();
					if (GetFileOutline(fileText, *markers, mparse, 0, FALSE))
					{
						// [case: 87527] don't save cookie if timed out
						markers->mModCookie = ed->m_modCookie;
					}
				}
			};

			auto prepare_states = [this]() {
				lines.reset(new SSParse::TextLines(text));

				auto on_num_lit = [&](int sp, int ep, char ch) {
					if (sp >= 0 && ep >= sp)
						num_literals.push_back(std::make_tuple(sp, ep, ch));
				};

				SSParse::Parser::MarkCommentsAndContants(text, ch_states, &cs_interp, &ch_state_borders, &cb_vec,
				                                         &dir_starts_vec, on_num_lit, lines.get(), nullptr, !is_cpp);

				// loop makes all # directives ignored by all methods
				// using states to determine the state of char
				if (is_cs || is_cpp)
				{
					for (int dir_pos : dir_starts_vec)
					{
						for (int i = dir_pos; i < (int)text.length(); i++)
						{
							if (text.is_on_line_delimiter(i, true))
								break;

							if (ch_states[i].state == SSParse::chs_none)
								ch_states.Set(i, SSParse::chs_directive);
						}
					}
				}
			};

			prepare_markers();
			prepare_states();
		}

		fileText.Empty();
		caretPath.clear();

		if (markers)
		{
			std::function<void(LineMarkers::Node & node, ULONG pos, LineMarkerPath & path)> createMarkerPath;
			createMarkerPath = [ed, &createMarkerPath](LineMarkers::Node& node, ULONG pos, LineMarkerPath& path) {
				LONG ln, col;
				ed->PosToLC((LONG)pos, ln, col);

				for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
				{
					LineMarkers::Node& ch = node.GetChild(idx);
					FileLineMarker& mkr = *ch;

					if (mkr.mEndLine < (ULONG)ln)
						continue;

					if (mkr.mStartLine > (ULONG)ln)
						return;

					bool push_to_path = false;

					if (mkr.mEndLine > (ULONG)ln && mkr.mStartLine < (ULONG)ln)
						push_to_path = true;
					else if (mkr.mEndLine == (ULONG)ln)
					{
						LONG mkr_ln, mkr_col;
						ed->PosToLC((LONG)mkr.mEndCp, mkr_ln, mkr_col);
						if (col < mkr_col) // Note: NOT <= as end is EXCLUSIVE!!!
							push_to_path = true;
					}
					else if (mkr.mStartLine == (ULONG)ln)
					{
						LONG mkr_ln, mkr_col;
						ed->PosToLC((LONG)mkr.mStartCp, mkr_ln, mkr_col);
						if (col >= mkr_col)
							push_to_path = true;
					}

					if (push_to_path)
					{
						if (mkr.mDisplayFlag == FileOutlineFlags::ff_Comments)
						{
							if (++idx < node.GetChildCount())
							{
								LineMarkers::Node& ch2 = node.GetChild(idx);
								FileLineMarker& mkr2 = *ch2;
								if (mkr2.mDisplayFlag & FileOutlineFlags::ff_PseudoGroups)
								{
									LineMarkers::Node& ch3 = ch2.GetChild(0);
									FileLineMarker& mkr3 = *ch3;

									path.push_back(mkr3);
									path.back().mStartCp = mkr.mStartCp;
									path.back().mStartLine = mkr.mStartLine;
									createMarkerPath(ch3, pos, path);
									return;
								}
								else
								{
									path.push_back(mkr2);
									path.back().mStartCp = mkr.mStartCp;
									path.back().mStartLine = mkr.mStartLine;
									createMarkerPath(ch2, pos, path);
									return;
								}
							}
						}

						path.push_back(mkr);
						createMarkerPath(ch, pos, path);
						return;
					}
				}
			};

			createMarkerPath(markers->Root(), kCurPos, caretPath);
		}

		pos_buffer_to_wide((ULONG)caret_pos, caret_pos);
		get_selection(selection);

		update_sel_rngs = ranges_begin_update(selection);

#ifndef RAD_STUDIO
		ed->GetVisibleLineRange(top_line, bottom_line);

		if (ed_wpf)
			start_zoom = ed_wpf->GetViewZoomFactor();

		if (lines)
			lines->DoLineColumnTest();
#endif

		is_initialized = true;
		return true;
	}

	bool find_extend_for_prepare(SelRng& in_out_rng)
	{
		if (sel_rngs.empty())
			return false;

		auto start = std::find(sel_rngs.cbegin(), sel_rngs.cend(), in_out_rng);

		auto it = start;

		// first, try to find preferred Left To Right extension
		while (it != sel_rngs.cend() && ++it != sel_rngs.cend())
		{
			auto& rng = *it;
			if (rng.contains_visually(sel_start) && rng.extends_end_trimmed(in_out_rng))
			{
				in_out_rng = rng;
				return true;
			}
		}

		// if not found, try to find any extension (old way)

		it = start; // reset iterator to starting position!
		while (it != sel_rngs.cend() && ++it != sel_rngs.cend())
		{
			auto& rng = *it;
			if (rng.contains_visually(sel_start) && rng.extends(in_out_rng))
			{
				in_out_rng = rng;
				return true;
			}
		}

		// else, set last

		if (!sel_rngs.back().equals(in_out_rng))
		{
			in_out_rng = sel_rngs.back();
			return true;
		}

		return false;
	}

	bool find_extend_block(SelRng& in_out_rng)
	{
		if (sel_rngs.empty())
			return false;

		auto it = std::find(sel_rngs.cbegin(), sel_rngs.cend(), in_out_rng);
		while (it != sel_rngs.cend() && ++it != sel_rngs.cend())
		{
			auto& rng = *it;

			if (rng.is_block())
			{
				in_out_rng = rng;
				return true;
			}
		}

		return false;
	}

	bool find_shrink_block(SelRng& in_out_rng)
	{
		if (sel_rngs.empty())
			return false;

		auto it = std::find(sel_rngs.cbegin(), sel_rngs.cend(), in_out_rng);
		while (it != sel_rngs.cend() && it != sel_rngs.cbegin())
		{
			auto& rng = *(--it);

			if (rng.is_block())
			{
				in_out_rng = rng;
				return true;
			}
		}

		if (!in_out_rng.equals(sel_start))
		{
			in_out_rng = sel_start;
			return true;
		}

		return false;
	}

	bool find_extend(SelRng& in_out_rng)
	{
		if (sel_rngs.empty())
			return false;

		auto it = std::find(sel_rngs.cbegin(), sel_rngs.cend(), in_out_rng);
		if (it != sel_rngs.cend() && ++it != sel_rngs.cend())
		{
			in_out_rng = *it;
			return true;
		}

		return false;
	}

	bool find_shrink(SelRng& in_out_rng)
	{
		if (sel_rngs.empty())
			return false;

		auto it = std::find(sel_rngs.cbegin(), sel_rngs.cend(), in_out_rng);
		while (it != sel_rngs.cend() && it != sel_rngs.cbegin())
		{
			in_out_rng = *(--it);
			return true;
		}

		if (!in_out_rng.equals(sel_start))
		{
			in_out_rng = sel_start;
			return true;
		}

		return false;
	}

	// move positions to the closest non-space chars
	// direction of move is from outside to inside
	void trim(int& s_pos, int& e_pos) const
	{
		int tmp = text.find(s_pos, e_pos, [](wchar_t ch) { return !wt_isspace(ch); });

		if (tmp >= 0)
			s_pos = tmp;

		tmp = text.rfind(e_pos, s_pos, [](wchar_t ch) { return !wt_isspace(ch); });

		if (tmp >= 0 && tmp < e_pos)
			e_pos = tmp;
	};

	void trim(SelRng& rng) const
	{
		int s_pos = rng.s_pos;
		int e_pos = rng.e_pos;

		trim(s_pos, e_pos);

		rng.s_pos = s_pos;
		rng.e_pos = e_pos;
	}

	void line_end_or_nospace(int& s_pos, int& e_pos, bool original_if_no_line = true) const
	{
		int old_s_pos = s_pos;
		int old_e_pos = e_pos;

		if (s_pos > 0)
		{
			int line_s_pos = text.line_start(s_pos);
			s_pos = text.rfind(s_pos - 1, line_s_pos, [](wchar_t ch) { return !wt_isspace(ch); });

			if (s_pos == -1)
				s_pos = line_s_pos;
			else if (text.is_valid_pos(s_pos + 1))
				s_pos = original_if_no_line ? old_s_pos : s_pos + 1;
		}

		if (e_pos < (int)(text.length() - 1))
		{
			int line_e_pos = text.line_end(e_pos);
			e_pos = text.find(e_pos + 1, line_e_pos, [](wchar_t ch) { return !wt_isspace(ch); });

			if (e_pos == -1)
				e_pos = line_e_pos;
			else if (text.is_valid_pos(e_pos - 1))
				e_pos = original_if_no_line ? old_e_pos : e_pos - 1;
		}

		if (!ActiveSettings().select_partial_line || text.line_start(old_s_pos) == text.line_start(old_e_pos))
		{
			if ((s_pos == old_s_pos && !text.is_on_line_start(old_s_pos)) ||
			    (e_pos == old_e_pos && !text.is_on_line_start(old_e_pos)))
			{
				s_pos = old_s_pos;
				e_pos = old_e_pos;
			}
		}
	};

	void line_end_or_nospace(SelRng& rng, bool original_if_no_line = true) const
	{
		int s_pos = rng.s_pos;
		int e_pos = rng.e_pos;
		line_end_or_nospace(s_pos, e_pos, original_if_no_line);
		rng.s_pos = s_pos;
		rng.e_pos = e_pos;
	}

	std::wstring rng_get_modifiers_str(const SelRng& rng, bool surround = true)
	{
		std::wstring wstr;

		if (rng.is_inner())
			wstr = L"inner text";
		else if (rng.is_scope())
			wstr = L"complete scope";

		if (rng.is_with_comments())
			wstr += wstr.empty() ? L"with comments" : L" with comments";

		if (rng.is_trimmed())
			wstr += wstr.empty() ? L"trimmed" : L" trimmed";

		if (!wstr.empty() && surround)
		{
			wstr.insert(wstr.begin(), L'(');
			wstr.push_back(L')');
		}

		return wstr;
	}

	// fills selection range from block and portion
	bool rng_from_block(SelRng& rng, const SSParse::CodeBlock& block, Block::Portion portion) const
	{
		int s_pos = -1;
		int e_pos = -1;

		bool bt = (block.is_directive() || block.is_brace_type()) && block.has_open_close();

		switch (portion)
		{
		case Block::Portion::Scope:
			s_pos = bt ? block.open_pos : block.start_pos();
			e_pos = bt ? block.close_pos : block.get_max_end_pos();
			break;

		case Block::Portion::InnerText:
			s_pos = bt ? block.open_pos + 1 : block.start_pos();
			e_pos = bt ? block.close_pos - 1 : block.get_max_end_pos();
			break;

		case Block::Portion::OuterTextNoChild:
			s_pos = block.start_pos();
			e_pos = block.end_pos();
			break;

		case Block::Portion::Prefix:
			s_pos = bt ? block.start_pos() : -1;
			e_pos = bt ? text.rfind_nospace(block.open_pos - 1) : -1;
			break;

		case Block::Portion::Suffix:
			s_pos = bt ? text.find_nospace(block.close_pos + 1) : -1;
			e_pos = bt ? block.end_pos() : -1;
			break;

		case Block::Portion::Group:
			block.get_statement_start_end(nullptr, s_pos, e_pos);
			break;

		default:
			s_pos = block.start_pos();
			e_pos = block.get_max_end_pos();
			break;
		}

		rng.s_pos = s_pos;
		rng.e_pos = e_pos;
		rng.b_type = block.type;
		IF_DBG(rng.b_index = block.index);

		rng_set_type(rng, block, portion);

		return rng.is_valid();
	}

	void rng_apply_mode(SelRng& rng, Block::Mode mode) const
	{
		if (mode == Block::Mode::Normal)
			return;

		if (!rng.is_valid())
			return;

		rng.remove_type(SelRng::tLines);

		if (rng.s_pos >= 0 && rng.e_pos >= 0)
		{
			switch (mode)
			{
			case Block::Mode::Trimmed: {
				int s = rng.s_pos;
				int e = rng.e_pos;
				trim(rng);
				if (s != rng.s_pos || e != rng.e_pos)
					rng.add_type(SelRng::tTrimmed);
				break;
			}
			case Block::Mode::TrimmedWOSemicolon: {
				rng_apply_mode(rng, Block::Mode::Trimmed);
				if (text.safe_at(rng.e_pos) == ';')
				{
					int e = text.rfind_nospace(rng.e_pos - 1, rng.s_pos);
					if (e != -1 && e < rng.e_pos)
					{
						rng.e_pos = e;
						rng.add_type(SelRng::tWOSemicolon);
					}
				}
				break;
			}
			case Block::Mode::LinesAfterTrim: {
				trim(rng);
				int s = rng.s_pos;
				int e = rng.e_pos;
				line_end_or_nospace(rng);
				if (s != rng.s_pos || e != rng.e_pos)
					rng.add_type(SelRng::tLines);
				else
					rng.add_type(SelRng::tEmbedded);
				break;
			}
			case Block::Mode::LinesOrNonSpace: {
				int s = rng.s_pos;
				int e = rng.e_pos;
				line_end_or_nospace(rng);
				if (s != rng.s_pos || e != rng.e_pos)
					rng.add_type(SelRng::tLines);
				else
					rng.add_type(SelRng::tEmbedded);
				break;
			}
			case Block::Mode::TrimAfterLines: {
				int s_pre = rng.s_pos;
				int e_pre = rng.e_pos;

				rng.s_pos = text.line_start(rng.s_pos, !is_cs);
				rng.e_pos = text.line_end(rng.e_pos, !is_cs);

				trim(rng);

				if (rng.s_pos > s_pre || rng.e_pos < e_pre)
					rng.add_type(SelRng::tTrimmed);

				break;
			}
			case Block::Mode::Lines: {
				rng.s_pos = text.line_start(rng.s_pos, !is_cs);
				rng.e_pos = text.line_end(rng.e_pos, !is_cs);
				rng.add_type(SelRng::tLines);
				break;
			}
			default:

				break;
			}
		}
	}

	void rng_set_type(SelRng& rng, const SSParse::CodeBlock& block, Block::Portion portion) const
	{
		rng.rng_type = SelRng::tDefault;

		if (block.is_code_block())
			rng.add_type(SelRng::tBlock);

		if (portion == Block::Portion::InnerText)
			rng.add_type(SelRng::tInner);
		else if (portion == Block::Portion::Scope)
			rng.add_type(SelRng::tScope);
	}

	bool rng_init(SelRng& rng, const std::wstring& name, const SSParse::CodeBlock& block, Block::Portion portion,
	              Block::Mode mode) const
	{
		if (name == L"Numeric Literal" || name == L"String Literal" || name == L"Comment")
		{
			rng.SetPriority(2);
		}

		rng.name = name;
		rng.b_type = block.type;
		IF_DBG(rng.b_index = block.index);
		rng_from_block(rng, block, portion);
		rng_apply_mode(rng, mode);

		return rng.is_valid();
	}

	void set_ternary_pair(int q_mark_pos, int colon_pos)
	{
		if (text.is_valid_pos(q_mark_pos) && text.is_valid_pos(colon_pos))
		{
			_ASSERTE(text.safe_at(q_mark_pos) == '?');
			_ASSERTE(text.safe_at(colon_pos) == ':');

			IF_SS_PARALLEL(std::lock_guard<std::mutex> _lock(pos_cache_mtx));
			ternary_map[q_mark_pos] = colon_pos;
			ternary_map[colon_pos] = q_mark_pos;
		}
		else
		{
			_ASSERTE(!"Invalid char position(s)!");
		}
	}

	int get_other_ternary_operator(int pos)
	{
		if (!text.is_one_of(pos, L"?:"))
		{
			_ASSERTE(!"Not expected char!");
			return -1;
		}

		IF_SS_PARALLEL(std::lock_guard<std::mutex> _lock(pos_cache_mtx));

		auto it = ternary_map.find(pos);
		if (it != ternary_map.end())
			return it->second;

		return -1;
	}

	bool pos_wide_to_RC_LONG(int vs_pos, ULONG& va_pos) const
	{
		EdCntPtr ed(g_currentEdCnt);

		if (!ed)
			return false;

		return lines->VaLineAndColumnFromPos((ULONG)vs_pos, va_pos);
	}

	void apply_selection(int s_pos, int e_pos, Block::Mode mode = Block::Mode::Normal) const
	{
		SelRng rng(s_pos, e_pos);
		rng_apply_mode(rng, mode);
		apply_selection(rng);
	};

	void apply_selection(const SSParse::CodeBlock& block, Block::Portion portion = Block::Portion::OuterText,
	                     Block::Mode mode = Block::Mode::Normal) const
	{
		SelRng rng;
		if (rng_from_block(rng, block, portion))
		{
			rng_apply_mode(rng, mode);
			apply_selection(rng);
		}
	};

	bool ranges_begin_update(const SelRng& sel)
	{
		if (is_extend_shrink_block_cmd())
		{
			if (std::find(sel_rngs.cbegin(), sel_rngs.cend(), sel) == sel_rngs.end())
			{
				sel_start = sel;
				sel_rngs.clear();
				return true;
			}
		}
		else
		{
			sel_rngs.clear();
			sel_start = sel;
		}

		return false;
	}

	bool rng_is_in_current_list(const SelRng& sel) const
	{
		return std::find(sel_rngs.cbegin(), sel_rngs.cend(), sel) != sel_rngs.end();
	}

	bool get_selection(int& s_pos, int& e_pos) const
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			long ls_pos, le_pos;
			ed->GetSel2(ls_pos, le_pos);

			int tmp1, tmp2;
			if (pos_buffer_to_wide((ULONG)ls_pos, tmp1) && pos_buffer_to_wide((ULONG)le_pos, tmp2))
			{
				s_pos = __min(tmp1, tmp2);
				e_pos = __max(tmp1, tmp2) - 1;

				return true;
			}
		}
		return false;
	};

	bool get_selection(SelRng& rng) const
	{
		int s, e;
		if (get_selection(s, e))
		{
			rng.s_pos = s;
			rng.e_pos = e;
			return true;
		}
		return false;
	}

	void show_tooltip(const SelRng& rng) const
	{
		try
		{
			EdCntPtr ed(g_currentEdCnt);

			DWORD peek_ms = Psettings->mSmartSelectPeekDuration;
#ifdef DEBUG
			if (peek_ms == 0)
				peek_ms = 5000;
#endif
			if (is_logging_peek())
				peek_ms = 1000;

			if (ed && peek_ms != 0)
			{
				long topLine, bottomLine;

				ed->GetVisibleLineRange(topLine, bottomLine);

				// don't show tooltip if there is not enough room for it
				if (bottomLine - topLine + 1 < 6)
				{
					show_tooltip(0, 0, 0); // hide
					return;
				}

				CPoint top_pos = ed->GetCharPos(TERRCTOLONG(topLine, 1));

				CRect client;
				ed->vGetClientRect(&client);

				if (client.top != top_pos.y)
				{
					CPoint next_pos = ed->GetCharPos(TERRCTOLONG((topLine + 1), 1));
					long line_height = next_pos.y - top_pos.y;

					if (client.top - top_pos.y > line_height / 3)
						topLine++;
				}

				long s_line = (long)lines->GetLineNumberFromPos((ULONG)rng.s_pos);

				if (s_line < topLine)
				{
					show_tooltip(peek_ms, rng.s_pos, rng.e_pos);
					return;
				}
			}

			show_tooltip(0, 0, 0); // hide
		}
		catch (...)
		{
			_ASSERTE(!"Smart Select peek tootlip failed.");
		}
	}

	void apply_selection(const SelRng& rng) const
	{
		// avoid hiding of tooltip during changes in editor
		ArgToolTipEx::KeepVisible keepTTVisible(ActiveSettings().tool_tip);

		CStringW nameWOMarkup = RemoveFormattedTextMarkup(rng.name.c_str());

		SS_DBG_TRACE_W(L"apply_selection: ID: %d, BID: %d, B: %s, RNG: %s, S: %d, E: %d, VS: %d, VE: %d, N: %s", rng.id,
		               rng.b_index, rng.is_block() ? L"True" : L"False", rng.line_column_string().c_str(), rng.s_pos,
		               rng.e_pos, rng.virtual_start(), rng.virtual_end(), (LPCWSTR)nameWOMarkup);

		if (rng.is_valid())
		{
			EdCntPtr ed(g_currentEdCnt);
			//			EdCntWPF * ed_wpf = gShellAttr && gShellAttr->IsDevenv10OrHigher() ?
			// dynamic_cast<EdCntWPF*>(ed.get()) : nullptr;

			if (!ed)
				return;

			ULONG buff_s_pos, buff_e_pos;
			if (pos_wide_to_RC_LONG(rng.s_pos, buff_s_pos) && pos_wide_to_RC_LONG(rng.e_pos + 1, buff_e_pos))
			{
				ed->SetSel(buff_s_pos, buff_e_pos);
			}

			// [case: 87378]
#ifndef RAD_STUDIO
			show_tooltip(rng);
#endif
			if (is_logging_block())
			{
				CStringW rng_nfo;
				CString__FormatW(rng_nfo, L"\r\nS: %d, E: %d, N: %s", rng.s_pos - ctx_start, rng.e_pos - ctx_start,
				                 (LPCWSTR)nameWOMarkup);
				gTestLogger->LogStrW(rng_nfo);
				gTestLogger->LogStrW(L"\r\n>>>\r\n");
				gTestLogger->LogStrW(ed->GetSelStringW());
				gTestLogger->LogStrW(L"\r\n<<<\r\n");
			}
		}
	};

	void apply_selection(const SSParse::CodeBlock& block, const Block::Context& ctx) const
	{
		apply_selection(block, ctx.portion, ctx.mode);
	};

	bool prepare_tree()
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		struct auto_state
		{
			bool& val;
			explicit auto_state(bool& v) : val(v)
			{
				val = true;
			}

			~auto_state()
			{
				val = false;
			}
		} _state_handler(is_preparing_tree);

		EdCntPtr ed(g_currentEdCnt);

		if (!ed)
			return false;

		//  		if (!tree.IsEmpty() && is_menu_cmd() && prev_caret_pos == caret_pos)
		//  			return true;

		SelRng sel;
		get_selection(sel);

		if (!tree.IsEmpty() && is_extend_shrink_block_cmd() && rng_is_in_current_list(sel))
			return true;

		solved_stmts.clear();
		tree.Clear();
		full_stmts.clear();
		brace_map.clear();
		operator_angs.clear();
		ternary_map.clear();
		global_scope_starts.clear();
		deref_ops.clear();
		return_type_ops.clear();
		ctx_start = caret_pos;
		ctx_end = caret_pos;

		std::wstringstream dbg_wss;
		DWORD dwTicks = ::GetTickCount();

		SSParse::CodeBlock::PtrSet vao_blocks;
		SSParse::CodeBlock::Ptr vao_stmt_block;

		if (is_cpp || is_cs)
		{
			// if not valid context range can not be
			// created, we create a parse context
			int min_start = caret_pos;
			int min_end = caret_pos;
			SSParse::CodeBlock::Ptr vao_min_block;

			int minor_start = caret_pos;
			int minor_end = caret_pos;
			SSParse::CodeBlock::Ptr vao_minor_block;

			auto code_filter = text.make_char_filter(ch_states, SSParse::chs_boundary_mask);

			dwTicks = ::GetTickCount();
			for (auto it = caretPath.rbegin(); it != caretPath.rend(); ++it)
			{
				FileLineMarker& lm = *it;

				// TODO: implement support for pseudo groups?
				if (lm.mDisplayFlag & FileOutlineFlags::ff_PseudoGroups)
					continue;

				// Comments do not contain code
				if (lm.mDisplayFlag & FileOutlineFlags::ff_Comments)
					continue;

				int cp_start = 0, cp_end = 0;

				pos_buffer_to_wide((ULONG)lm.mStartCp, cp_start);
				pos_buffer_to_wide((ULONG)lm.mEndCp, cp_end);

				cp_end -= 1; // in Smart Select end position points to last included char
				fix_outline_block_range(cp_start, cp_end);

#ifdef _DEBUG
// 				CStringW lm_substr = ed->GetSubString(lm.mStartCp, lm.mEndCp);
// 				std::wstring cp_substr = text.substr(cp_start, cp_end - cp_start);
//
// 				_ASSERTE((LPCWSTR)lm_substr == cp_substr);
#endif
				bool is_stmt_block = false;
				bool is_ctx_block = false;
				bool is_min_block = false;
				bool is_minor_block = false;

				if (min_start == min_end || (cp_start > min_start && cp_end < min_end))
				{
					min_start = cp_start;
					min_end = cp_end;
					is_min_block = true;
				}

				if (lm.mDisplayFlag == FileOutlineFlags::ff_None)
				{
					int open_brace = text.find(cp_start, cp_end + 1, L'{', code_filter);
					if (open_brace >= cp_start)
					{
						std::wstring wstr = SSParse::Parser::ReadCode(
						    text, ch_states, cp_start,
						    [open_brace](int pos, std::wstring) { return pos + 1 >= open_brace; }, true, false);

						if (wstr.find(L"namespace ") != std::wstring::npos ||
						    wstr.find(L" namespace ") != std::wstring::npos)
						{
							lm.mDisplayFlag = FileOutlineFlags::ff_Namespaces;
						}
						else
						{
							lm.mDisplayFlag = FileOutlineFlags::ff_TypesAndClasses;
						}
					}
				}

				if (lm.mDisplayFlag == FileOutlineFlags::ff_Preprocessor ||
				    (lm.mDisplayFlag & FileOutlineFlags::ff_Namespaces) ||
				    (lm.mDisplayFlag & FileOutlineFlags::ff_TypesAndClasses) ||
				    (lm.mDisplayFlag & FileOutlineFlags::ff_MethodsAndFunctions))
				{
					if (minor_start == minor_end || (cp_start > minor_start && cp_end < minor_end))
					{
						minor_start = cp_start;
						minor_end = cp_end;
						is_minor_block = true;
					}
				}

				if (ctx_start == ctx_end || (cp_start > ctx_start && cp_end < ctx_end))
				{
					if (lm.mDisplayFlag & FileOutlineFlags::ff_TypesAndClasses)
					{
						is_ctx_block = true;
						is_stmt_block = false;
					}
					else if (lm.mDisplayFlag & FileOutlineFlags::ff_MethodsAndFunctions)
					{
						is_ctx_block = true;
						is_stmt_block = true;
					}
				}

				SSParse::CodeBlock::Ptr vao_b(new SSParse::CodeBlock(text, L'\0', cp_start, cp_end));

				if (is_min_block)
					vao_min_block = vao_b;

				if (is_minor_block)
					vao_minor_block = vao_b;

				if (is_cpp || is_cs)
				{
					SSParse::Parser::ResolveVAOutlineBlock(lm, text, ch_states, *vao_b, is_cs);

					// this must be applied AFTER ResolveVAOutlineBlock as we need
					// block resolved so that its brace state is known!
					if (is_stmt_block && vao_b->is_brace_type() && vao_b->contains(caret_pos, false, false))
					{
						if (!vao_stmt_block || vao_stmt_block->contains(*vao_b))
							vao_stmt_block = vao_b;
					}
				}

				if (is_ctx_block)
				{
					ctx_start = vao_b->start_pos();
					ctx_end = vao_b->end_pos();
				}

				vao_blocks.insert(vao_b);
			}

			if (ctx_start == ctx_end)
			{
				if (minor_start == minor_end)
				{
					minor_start = min_start;
					minor_end = min_end;
					vao_minor_block = vao_min_block;
				}

				if (minor_start < ctx_start && minor_end > ctx_end)
				{
					ctx_start = minor_start;
					ctx_end = minor_end;

					if (!vao_stmt_block && vao_minor_block)
					{
						// fake the open/close for statement generating process
						vao_stmt_block.reset(new SSParse::CodeBlock(
						    SSParse::CodeBlock::b_va_outline, vao_minor_block->src_code,
						    L'{', // behave like in curly brace scope
						    vao_minor_block->start_pos(), vao_minor_block->start_pos(), vao_minor_block->end_pos()));

						ctx_start = vao_stmt_block->start_pos();
						ctx_end = vao_stmt_block->end_pos();
					}
				}
			}

			IF_DBG(dbg_wss << L"caretPath: " << ::GetTickCount() - dwTicks << std::endl);
		}
		else
		{
			dwTicks = ::GetTickCount();
			for (auto it = caretPath.rbegin(); it != caretPath.rend(); ++it)
			{
				FileLineMarker& lm = *it;

				int cp_start = 0, cp_end = 0;

				pos_buffer_to_wide((ULONG)lm.mStartCp, cp_start);
				pos_buffer_to_wide((ULONG)lm.mEndCp, cp_end);
				cp_end -= 1; // in Smart Select end position points to last included char

				if (ctx_start == ctx_end || (cp_start > ctx_start && cp_end < ctx_end))
				{
					ctx_start = cp_start;
					ctx_end = cp_end;
				}

				SSParse::CodeBlock::Ptr vao_b(
				    new SSParse::CodeBlock(SSParse::CodeBlock::b_code_block, text, L'\0', cp_start, cp_end));
				vao_blocks.insert(vao_b);
			}
			IF_DBG(dbg_wss << L"caretPath: " << ::GetTickCount() - dwTicks << std::endl);
		}

		std::vector<SSParse::CodeBlock::Ptr> ctx_blocks;
		auto code_filter = make_code_filter();

		// setup template cache and global scope cache
		// this is necessary due to issues with MPGetScope
		// from parallel threads and this way is fast enough.
		for (int i = ctx_start; i <= ctx_end; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (ch == '<' || ch == '>')
			{
				cache_template_char(i, ch, false, true, true);
			}
			else if (is_cpp && ch == ':' && text.safe_at(i + 1) == ':')
			{
				// global scopes alone "::method()" are allowed only in C++
				// C# uses syntax: "global::method()"

				cache_global_scope_delimiter_start(i);
				i++; // step over :: (avoid double check)
			}
			else if (ch == '*' || ch == '&' || (is_cpp && (ch == '^' || ch == '%')))
			{
				cache_deref_operator(i, code_filter);
			}
			else if (is_cpp && ch == '-' && text.safe_at(i + 1) == '>')
			{
				cache_return_type_operator(i, code_filter);
				i++;
			}
		}

		// resolve comment over context (in case when caret is over context in comment),
		// so then blocks in comments get selected.
		if (caret_pos < ctx_start && ch_states[caret_pos].HasAnyBitOfMask(SSParse::chs_comment_mask))
		{
			for (int i = caret_pos; text.is_valid_pos(i); i--)
			{
				if (ch_states[i].HasAnyBitOfMask(SSParse::chs_comment_mask))
					ctx_start = i;
				else if (!text.is_space(i))
					break;
			}
		}

		int ctx_caret_pos = caret_pos - ctx_start;
		SSParse::SharedWStr ctx_text = text.substr(ctx_start, ctx_end - ctx_start + 1);
		SSParse::EnumMaskList<SSParse::CharState> ctx_states(ch_states, ctx_start, ctx_end + 1);

		auto flood_inserter = [this, &ctx_blocks](int side, SSParse::CodeBlock::Ptr bl) {
			set_matching_brace(ctx_start + bl->open_pos, ctx_start + bl->close_pos);
			ctx_blocks.push_back(bl);
		};

		dwTicks = ::GetTickCount();

		auto ctx_ang_filter = [this](wchar_t ch, int pos) {
			if (ch_states.at(ctx_start + pos).HasAnyBitOfMask(SSParse::chs_comment_mask | SSParse::chs_string_mask))
				return true;

			return is_template(ctx_start + pos, ch);
		};

		SSParse::Parser::BraceBlocksFlood(ctx_text, ctx_states, flood_inserter, ctx_ang_filter, is_cs, ctx_caret_pos,
		                                  true, -1);

		IF_DBG(dbg_wss << L"BraceBlocksFlood: " << ::GetTickCount() - dwTicks << std::endl);

		SSParse::CodeBlock::PtrSet blocks(vao_blocks.begin(), vao_blocks.end());

		auto ang_filter = make_ang_filter(); // now without offset!

		dwTicks = ::GetTickCount();

		SS_FOR_EACH(ctx_blocks.begin(), ctx_blocks.end(), [this, &vao_blocks, ang_filter](SSParse::CodeBlock::Ptr bl) {
			// following converts positions in block
			// from local context positions to global
			// positions within whole text
			bl->reset_src(ctx_start, text);

			if (is_cs || is_cpp)
			{
				auto vao_it =
				    std::find_if(vao_blocks.cbegin(), vao_blocks.cend(), [&bl](const SSParse::CodeBlock::Ptr& p) {
					    return p->open_char == bl->open_char && p->open_pos == bl->open_pos &&
					           p->close_pos == bl->close_pos;
				    });

				if (vao_it != vao_blocks.cend())
				{
					// delete this block in next loop as it already exists
					// in blocks from VA Outline which are preferred.
					bl->make_garbage();
				}
				else
				{
					// find top position of block
					SSParse::Parser::ResolveBlock(text, ch_states, *bl, is_cs, ang_filter);
					SSParse::Parser::ResolveComments(*bl, text, ch_states);
				}
			}
		});

		IF_DBG(dbg_wss << L"ResolveBlock: " << ::GetTickCount() - dwTicks << std::endl);

		if (is_cs || is_cpp)
		{
			std::mutex blocks_mutex;
			auto stmt_inserter = [this, &blocks, &blocks_mutex, &code_filter](SSParse::CodeBlock::Ptr bl) {
				if (is_cpp)
				{
					// include global namespace scope delimiter, but only in C++,
					// where it can be used without "global" keyword as in C#
					int s_pos = bl->start_pos();
					int scope_sep = text.rfind_scope_delimiter_start(s_pos - 1, is_cs, true, code_filter);
					if (scope_sep != -1 && is_global_scope_delimiter_start(scope_sep))
						s_pos = scope_sep;
					if (s_pos < bl->start_pos())
						bl->top_pos = s_pos;
				}

				std::lock_guard<std::mutex> lock(blocks_mutex);

				if (bl->is_full_stmt())
					full_stmts.insert(bl);
				else
					blocks.insert(bl);
			};

			dwTicks = ::GetTickCount();

			if (vao_stmt_block)
			{
				auto brace_founder = [&](int pos) {
					return get_matching_brace(pos, false, code_filter, vao_stmt_block->start_pos(),
					                          vao_stmt_block->end_pos());
				};

				SSParse::Parser::StatementBlocks(text, ch_states, stmt_inserter, brace_founder, *vao_stmt_block,
				                                 caret_pos, is_cs);
			}

			SS_FOR_EACH(ctx_blocks.begin(), ctx_blocks.end(), [&](SSParse::CodeBlock::Ptr block) {
				if (block)
				{
					// insert only those not garbage
					if (!block->is_garbage())
					{
						{
							std::lock_guard<std::mutex> lock(blocks_mutex);
							blocks.insert(block);
						}

						if (block->is_statement_container(ch_states) && block->scope_contains(caret_pos, false))
						{
							auto brace_founder = [&](int pos) {
								return get_matching_brace(pos, false, code_filter, block->start_pos(),
								                          block->end_pos());
							};

							SSParse::Parser::StatementBlocks(text, ch_states, stmt_inserter, brace_founder, *block,
							                                 caret_pos, is_cs);
						}
					}
				}
			});

			IF_DBG(dbg_wss << L"StatementBlocks: " << ::GetTickCount() - dwTicks << std::endl);

			dwTicks = ::GetTickCount();

			SS_FOR_EACH(blocks.cbegin(), blocks.cend(),
			            [this](SSParse::CodeBlock::Ptr block) { block->ensure_name_and_type(ch_states, is_cs); });

			IF_DBG(dbg_wss << L"ensure_name_and_type: " << ::GetTickCount() - dwTicks << std::endl);
		}
		else
		{
			dwTicks = ::GetTickCount();
			for (const auto& block : ctx_blocks)
				if (block && !block->is_garbage())
					blocks.insert(block);
			IF_DBG(dbg_wss << L"blocks.insert: " << ::GetTickCount() - dwTicks << std::endl);
		}

		dwTicks = ::GetTickCount();

		tree.Init(text, ch_states, blocks, is_cs || is_cpp, is_cs);
		IF_DBG(dbg_wss << L"tree.Init: " << ::GetTickCount() - dwTicks << std::endl);

#ifdef _DEBUG
		std::wstring dbg_wstr = dbg_wss.str();

		auto xxx = tree.root->debug_string(ch_states, is_cs);
#endif

		return !tree.IsEmpty();
	}

	bool is_cs_cpp_data_type(std::wstring str)
	{
		auto starts_with = [&str](LPCWSTR s, int len, bool match_as_true) -> bool {
			if (len < 0)
				len = wcslen_i(s);

			_ASSERTE(wcslen(s) == (size_t)len);

			if (match_as_true && (size_t)len == str.length() && str == s)
				return true;

			return wcsncmp(s, str.c_str(), (uint)len) == 0 && str.length() >= (size_t)len && !ISCSYM(str[(uint)len]);
		};

		auto find_csym = [&str](int s_pos) -> int {
			for (size_t i = (size_t)s_pos; i < str.length(); i++)
				if (ISCSYM(str[i]))
					return (int)i;

			return -1;
		};

		auto get_rgx = [](LPCWSTR pRgx) -> std::shared_ptr<std::wregex> {
			static std::mutex mtx;
			static std::map<std::wstring, std::shared_ptr<std::wregex>> rgx_map;

			auto str1 = std::wstring(pRgx);

			std::lock_guard<std::mutex> _lock(mtx);

			auto it = rgx_map.find(str1);

			if (it != rgx_map.end())
				return it->second;

			auto r = std::make_shared<std::wregex>(str1, std::wregex::ECMAScript);

			rgx_map[str1] = r;

			return r;
		};

		auto starts_with_rgx = [&str, &get_rgx](LPCWSTR pRgx) -> size_t {
			auto r_ptr = get_rgx(pRgx);
			if (r_ptr)
			{
				const std::wregex& r = *r_ptr;
				std::wsmatch m;

				if (std::regex_search(str, m, r, std::regex_constants::match_continuous) && m.position() == 0)
				{
					return (uint)m.length();
				}
			}

			return 0;
		};

		int csym = find_csym(0);
		if (csym > 0)
			str.erase(0u, (uint)csym);

		if (is_cs)
		{
			if (starts_with_rgx(L"System\\s*[.]\\s*"))
				return true;

			return str == L"var";
		}
		else if (is_cpp)
		{
			if (starts_with(L"unsigned", 8, true))
				return true;

			if (starts_with(L"signed", 6, true))
				return true;

			if (starts_with(L"long", 4, true))
				return true;

			if (starts_with(L"const", 5, false))
				return true;

			if (starts_with(L"static", 6, false))
				return true;

			if (starts_with(L"static", 6, false))
				return true;

			if (starts_with_rgx(L"decltype\\s*[(]"))
				return true;

			return str == L"auto";
		}

		return false;
	}

	bool is_initializer_list(SSParse::CodeBlock::Ptr& ptr)
	{
		using namespace SSParse;

		_ASSERTE(is_cs || is_cpp);

		if (ptr->open_char != '{')
			return false;

		ptr->ensure_name_and_type(ch_states, is_cs);

		if (ptr->has_type_specified() && ptr->has_type_bit(CodeBlock::b_scope) &&
		    !ptr->is_of_type(CodeBlock::b_local_scope))
		{
			return false;
		}

		auto code_filter =
		    make_char_filter(static_cast<SSParse::CharState>(SSParse::chs_no_code | SSParse::chs_directive));

		if (is_cpp)
		{
			int prev_pos = text.rfind_nospace(ptr->open_pos - 1, ptr->start_pos(), code_filter);
			if (prev_pos != -1)
			{
				wchar_t ch = text.safe_at(prev_pos);
				return ch == ',' || ch == '=' || ch == '(';
			}
		}
		else if (is_cs)
		{
			// C# style init list in form:
			// new myClass { member = value, member = value }

			BraceCounter bc("(){}[]<>");
			bc.set_ignore_angle_brackets(false);
			bc.ang_filter = make_ang_filter();
			int eq_count = 0;
			int comma_count = 0;

			int sp = ptr->open_pos + 1;
			int ep = ptr->close_pos - 1;

			for (int i = sp; i <= ep; i++)
			{
				wchar_t ch = text.safe_at(i);

				if (!code_filter(i, ch))
					continue;

				if (ch <= ' ' || ISCSYM(ch) || ch == '"' || ch == '\'')
					continue;

				bc.on_char(ch, i, nullptr);

				if (bc.is_mismatch())
					return false;

				if (!bc.is_inside_braces())
				{
					if (ch == ';') // semicolon can't be in the init list
					{
						return false;
					}

					if (ch == '=')
					{
						int op_sp, op_ep;
						auto op = get_operator(i, &op_sp, &op_ep);
						if (op != nullptr)
						{
							if (op_sp == op_ep && *op == L'=')
								eq_count++;

							i = op_ep;
						}
					}
					else if (ch == ',')
					{
						comma_count++;
					}
				}
			}

			if (bc.is_inside_braces())
				return false;

			return (eq_count > 0) && (eq_count == comma_count + 1);
		}

		return false;
	}

	bool is_arg_list(int start_pos, int end_pos)
	{
		using namespace SSParse;

		SS_DBG(auto str = text.substr_se(start_pos, end_pos));

		if (end_pos < start_pos)
			return false;

		auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

		BraceCounter bcx("(){}[]<>");
		bcx.set_ignore_angle_brackets(false);
		bcx.ang_filter = make_ang_filter();

		wchar_t ch0 = text.safe_at(start_pos, code_filter);
		wchar_t ch1 = text.safe_at(end_pos, code_filter);

		if (bcx.is_match(ch0, ch1))
		{
			start_pos++;
			end_pos--;
		}

		for (int i = start_pos; i <= end_pos; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bcx.on_char(ch, i, nullptr);

			if (bcx.is_mismatch())
				return false;

			if (!bcx.is_inside_braces() && ch == ',')
				return true;
		}

		return bcx.is_inside_braces();
	}

	bool is_bracing_mismatch(int start_pos, int end_pos, LPCSTR braces = "()[]{}<>")
	{
		using namespace SSParse;

		if (end_pos < start_pos)
			return false;

		auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

		BraceCounter bcx(braces);
		if (!strstr(braces, "<>"))
			bcx.set_ignore_angle_brackets(true);
		else
			bcx.ang_filter = make_ang_filter();

		for (int i = start_pos; i <= end_pos; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bcx.on_char(ch, i, nullptr);

			if (bcx.is_mismatch())
				return true;
		}

		return bcx.is_inside_braces();
	}

	bool is_multi_statement_or_mismatch(int start_pos, int end_pos, LPCSTR braces = "()[]{}<>")
	{
		using namespace SSParse;

		auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

		BraceCounter bcx(braces);
		if (!strstr(braces, "<>"))
			bcx.set_ignore_angle_brackets(true);
		else
			bcx.ang_filter = make_ang_filter();

		int semi_colons = 0;

		for (int i = start_pos; i <= end_pos; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bcx.on_char(ch, i, nullptr);

			if (!bcx.is_inside_braces() && ch == ';')
			{
				semi_colons++;
				if (semi_colons > 1)
					return true;
			}

			if (bcx.is_mismatch())
				return true;
		}

		return bcx.is_inside_braces();
	}

	bool is_whole_cpp_lambda(SSParse::CodeBlock::Ptr& ptr, bool repair_scope = false)
	{
		if (!repair_scope)
			return is_whole_cpp_lambda(ptr->start_pos(), ptr->end_pos());

		int op, cp;
		if (is_whole_cpp_lambda(ptr->start_pos(), ptr->end_pos(), &op, &cp))
		{
			int end_pos = ptr->end_pos();

			ptr->open_pos = op;
			ptr->close_pos = cp;
			ptr->open_char = '{';
			ptr->semicolon_pos = -1;

			if (ptr->end_pos() < end_pos)
				ptr->semicolon_pos = end_pos;

			return true;
		}

		return false;
	}

	bool is_whole_cpp_lambda(int start_pos, int end_pos, int* open_pos = nullptr, int* close_pos = nullptr)
	{
		using namespace SSParse;

		auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

		if (end_pos <= start_pos)
			return false;

		int first = text.find_nospace(start_pos, end_pos, code_filter);
		wchar_t first_ch = text.safe_at(first);
		if (first_ch != '[')
			return false;

		int last = get_matching_brace(first, false, code_filter, start_pos, end_pos);
		if (last != -1)
		{
			first = text.find_nospace(last + 1, end_pos, code_filter);
			first_ch = text.safe_at(first);
			if (first_ch == '(' || first_ch == '{')
			{
				last = get_matching_brace(first, false, code_filter, start_pos, end_pos);
				if (last != -1)
				{
					if (first_ch == '{')
					{
						first = text.find_nospace(last + 1, end_pos, code_filter);
						first_ch = text.safe_at(first);
						return !ISCSYM(first_ch);
					}
					else
					{
						first = text.find_one_of(last + 1, end_pos, L"-{", code_filter);
						first_ch = text.safe_at(first);

						if (first_ch == '-')
						{
							if (!is_return_type_operator(first))
								return false;

							first = text.find(first + 1, end_pos, L'{', code_filter);
							first_ch = text.safe_at(first);
						}

						if (first_ch != '{')
							return false;

						last = get_matching_brace(first, false, code_filter, start_pos, end_pos);

						if (last != -1)
						{
							if (open_pos)
								*open_pos = first;

							if (close_pos)
								*close_pos = last;

							return true;
						}
					}
				}
			}
		}

		return false;
	}

	bool is_ternary_expr(int start_pos, int end_pos, int* out_q_mark = nullptr, int* out_colon = nullptr)
	{
		using namespace SSParse;

		auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

		BraceCounter bcx;
		bcx.ang_filter = make_ang_filter();
		int q_mark = -1, colon = -1;

		if (end_pos <= start_pos)
			return false;

		for (int i = start_pos; i <= end_pos; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bcx.on_char(ch, i, nullptr);

			if (!bcx.is_inside_braces())
			{
				if (ch == '=' && q_mark == -1)
				{
					int osp, oep;
					auto op = get_operator(i, &osp, &oep);

					if (op)
					{
						if (Parser::IsAssignmentOperator(op, is_cs))
							return false;

						i = oep;
						continue;
					}
				}

				if (ch == '?' || ch == ':')
				{
					int sp, ep;
					auto op = get_operator(i, &sp, &ep);
					if (op && sp == ep)
					{
						if (ch == '?')
							q_mark = sp;
						else
							colon = sp;
					}
				}
			}

			if (bcx.is_mismatch())
				return false;
		}

		if (!bcx.is_inside_braces() && q_mark != -1 && colon != -1)
		{
			if (out_q_mark)
				*out_q_mark = q_mark;

			if (out_colon)
				*out_colon = colon;

			set_ternary_pair(q_mark, colon);

			return true;
		}

		return false;
	}

	// It is done this way to ensure easy concurrent access to map (no writes allowed)
	// const correctness of the map guarantees no writing to the map
	static bool is_invalid_ternary_expr(SSParse::CodeBlock::Ptr& ptr, const std::map<int, int>& ternmap)
	{
		for (auto p : ternmap)
		{
			int c = ptr->contains(p.first, false, false) ? 1 : 0;
			c += ptr->contains(p.second, false, false) ? 1 : 0;

			if (c == 1)
				return true;
		}

		return false;
	}

	split_report split_cs_cpp_round_scope(SSParse::CodeBlock::Ptr& ptr,
	                                      std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		fix_block_open_close(ptr);

		int report_start = ptr->open_pos;
		if (report_start == -1)
			report_start = ptr->top_pos;

		int report_end = ptr->close_pos;
		if (report_end == -1)
			report_end = ptr->semicolon_pos;

		auto rep = get_split_report(L"parens", report_start, report_end);
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"parens", report_start, report_end, val);
		};

		// if this is already resolved scope, return false
		if ((ptr->type & CodeBlock::b_dont_split_scope) != 0)
			return REPORT(false);

		if (ptr->open_char == '(' && ptr->has_open_close() && ptr->close_pos > ptr->open_pos &&
		    !is_comment_or_string(ptr->open_pos) && ptr->scope_contains(caret_pos, false))
		{
			if (split_cs_cpp_for_stmt(ptr, sub_items))
				return REPORT(true);

			if (split_cs_cpp_ternary_expr(ptr, sub_items))
				return REPORT(true);

			if (split_cs_cpp_comma_delimited_scope(ptr, sub_items))
				return REPORT(true);

			if (split_cs_cpp_single_param_scope(ptr, sub_items))
				return REPORT(true);

			auto code_filter = make_code_filter();

			int sp = text.find_nospace(ptr->open_pos + 1, ptr->end_pos() - 1, code_filter);
			int ep = text.rfind_nospace(ptr->close_pos - 1, ptr->open_pos + 1, code_filter);
			if (sp != -1 && ep != -1 && ep > sp)
			{
				CodeBlock::Ptr tmp(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, L'.', sp, ep));

				split_cs_cpp_stmt(tmp, sub_items);

				split_open_close_prefix(ptr, sub_items);

				tmp->type = (SSParse::CodeBlock::block_type)(ptr->type | SSParse::CodeBlock::b_dont_split_scope);

				return REPORT(true);
			}
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_ternary_expr(SSParse::CodeBlock::Ptr& ptr,
	                                       std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		auto rep = get_split_report(L"ternary", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"ternary", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->open_char == '(' && ptr->open_pos >= 0 && ptr->close_pos >= 0 && ptr->close_pos > ptr->open_pos &&
		    !is_comment_or_string(ptr->open_pos) && ptr->scope_contains(caret_pos, false))
		{
			// if this is already resolved scope, return false
			if (ptr->type & CodeBlock::b_dont_split_scope)
				return REPORT(false);

			auto code_filter = make_code_filter();

			int sp = text.find_nospace(ptr->open_pos + 1, ptr->end_pos() - 1, code_filter);
			int ep = text.rfind_nospace(ptr->close_pos - 1, ptr->open_pos + 1, code_filter);
			if (sp != -1 && ep != -1 && ep > sp)
			{
				CodeBlock::Ptr tmp(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, L'.', sp, ep));

				if (split_cs_cpp_ternary_expr(tmp, sub_items))
				{
					if (granular_start())
						split_open_close_prefix(ptr, sub_items);

					ptr->type = (SSParse::CodeBlock::block_type)(ptr->type | SSParse::CodeBlock::b_dont_split_scope);
					return REPORT(true);
				}

				return REPORT(false);
			}
		}

		int tcq, tcs;
		if (is_ternary_expr(ptr->start_pos(), ptr->end_pos(), &tcq, &tcs))
		{
			std::set<std::tuple<int, int>> splits;
			splits.insert(std::make_tuple(tcq, 1));
			splits.insert(std::make_tuple(tcs, 1));

			std::vector<SSParse::CodeBlock::Ptr> vec;

			split_by_splitters(ptr->start_pos(), ptr->end_pos(), split_mode::delimiter, splits,
			                   [&vec](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
				                   b->type = CodeBlock::b_ternary_part;
				                   vec.push_back(b);
				                   return true;
			                   });

			_ASSERTE(vec.size() == 3);

			if (vec.size() == 3)
			{
				for (auto b : vec)
				{
					if (b->contains(caret_pos, false, true))
					{
						split_cs_cpp_stmt(b, sub_items);
						sub_items(b);
					}
				}

				if (granular_start())
				{
					SSParse::CodeBlock::Ptr right(
					    new CodeBlock(CodeBlock::b_ternary_part, text, L'.', vec[1]->start_pos(), vec[2]->end_pos()));

					if (right->contains(caret_pos, false, true))
						sub_items(right);
				}
			}

			ptr->type = (SSParse::CodeBlock::block_type)(ptr->type | SSParse::CodeBlock::b_dont_split);
			return REPORT(true);
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_type_and_name(SSParse::CodeBlock::Ptr& ptr, bool strict_typing, bool scoped_name,
	                                        std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		auto rep = get_split_report(L"type_and_name", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"type_and_name", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->has_type_bit(CodeBlock::b_dont_split))
			return REPORT(false);

		auto code_filter = make_code_filter();
		BraceCounter bc("{}<>", true);
		bc.ang_filter = make_ang_filter();

		int start_pos = ptr->start_pos();
		int end_pos = ptr->end_pos();
		int attrib_start = -1;

		// allow comma or semicolon at the end of block
		wchar_t end_ch = text.safe_at(end_pos);
		if (end_ch == ',' || end_ch == ';')
		{
			int new_end = text.rfind_nospace(end_pos - 1, code_filter);
			if (new_end != -1)
			{
				end_pos = new_end;
			}
		}

		{ // initial filtering

			// first char from the right must be symbol char
			int right_nospace = text.rfind_nospace(end_pos, code_filter);
			if (!text.is_identifier_boundary(right_nospace))
			{
				return REPORT(false);
			}

			// first char from the left must be valid identifier start or scope delimiter
			int left_nospace = text.find_nospace(start_pos, end_pos, code_filter);

			// but check first for [...] attributes
			for (int x = 0; x < 20 && text.safe_at(left_nospace) == '['; x++)
			{
				if (attrib_start == -1)
					attrib_start = left_nospace;

				int attr_cpos = get_matching_brace(left_nospace, false, code_filter, left_nospace, end_pos);
				if (attr_cpos != -1)
				{
					if (left_nospace <= caret_pos && caret_pos <= attr_cpos)
					{
						CodeBlock::Ptr b(
						    new CodeBlock(CodeBlock::b_undefined, ptr->src_code, L'.', -1, x, attr_cpos, -1));

						if (!split_cs_cpp_comma_delimited_scope(b, sub_items))
							split_cs_cpp_single_param_scope(b, sub_items);
					}
					left_nospace = text.find_nospace(attr_cpos + 1, end_pos, code_filter);
					continue;
				}
				break;
			}

			if (!text.is_identifier_start(left_nospace) &&
			    !text.is_at_scope_delimiter_start(left_nospace, is_cs, false, code_filter))
			{
				return REPORT(false);
			}

			if (left_nospace != -1)
				start_pos = left_nospace;

			if (right_nospace != -1)
				end_pos = right_nospace;

			// don't allow any bracing mismatch
			// usually caused by expressions like "abc >> efg" or "abc > efg"
			//
			// (nested) don't allow expressions, directives, commas, semicolons
			// but allow: typedef struct { ... } name;
			BraceCounter bcx("{}<>");
			bcx.ang_filter = make_ang_filter();

			std::wstring invalid_chars(L"()[]=+-/#|;,");
			for (int i = start_pos; i <= end_pos; i++)
			{
				wchar_t ch = text.at(i);

				if (!code_filter(i, ch))
					continue;

				if (wt_isspace(ch))
					continue;

				bcx.on_char(ch, i, nullptr);

				if (!bcx.is_inside_braces())
				{
					// don't allow expressions, directives, semicolons, commas

					if (invalid_chars.find(ch) != std::wstring::npos)
						return REPORT(false);

					if ((ch == '<' || ch == '>') && !is_template(i, ch))
						return REPORT(false);
				}

				if (bcx.is_mismatch())
					return REPORT(false);
			}

			// this is also mismatch like in "abc << efg" or "abc < efg"
			if (bcx.is_inside_braces())
				return REPORT(false);
		}

		wchar_t ch = '\0';

		// split the rest to type and name
		for (int i = end_pos; i >= start_pos; --i)
		{
			ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bc.on_char(ch, i, nullptr);
			if (!bc.is_inside_braces() && ISCSYM(ch))
			{
				int no_sym = text.rfind_not_csym(i, start_pos, code_filter);
				if (no_sym != -1)
				{
					// single symbol => not a type and name pair
					if (no_sym + 1 == start_pos)
						return REPORT(false);

					int no_space = text.rfind_nospace(no_sym, start_pos, code_filter);

					// for case of invalid range (should not happen)
					if (no_space == start_pos)
						return REPORT(false);

					if (!scoped_name)
					{
						// don't allow name to be x::y nor x.y
						if (text.is_at_scope_delimiter(no_space, is_cs, false, code_filter))
						{
							return REPORT(false);
						}
					}
					else
					{
						// find range of whole delimited name
						// name is allowed to be like: x::y or x.y

						while (text.is_at_scope_delimiter(no_sym, is_cs, true, code_filter))
						{
							int sym_pos = text.rfind_csym(no_sym, start_pos, L"[](){}<>;,*^%&-+/|~", code_filter);
							if (sym_pos == -1 || sym_pos <= start_pos)
								return REPORT(false);

							no_sym = text.rfind_not_csym(sym_pos, start_pos, code_filter);
							if (no_sym == -1 || no_sym + 1 <= start_pos)
								return REPORT(false);
						}

						if (no_sym == -1 || no_sym + 1 == start_pos)
							return REPORT(false);

						if (text.is_at_call_delimiter(no_space, is_cs, false, code_filter))
							return REPORT(false);

						int type_end = text.rfind_nospace(no_sym, start_pos, code_filter);

						// for case of invalid range (should not happen)
						if (type_end == -1 || type_end == start_pos)
							return REPORT(false);
					}

					// [case: 98549] disallow delimiters after type
					if (text.is_at_call_delimiter(no_sym, is_cs, true, code_filter))
						return REPORT(false);

					if (strict_typing)
					{
						// if strict typing is enabled, try to lookup for the type
						// if the left side is not the type, then it is invalid

						if (!is_template(no_space))
						{
							auto op = get_operator(no_space);
							if (op)
							{
								if (wcscmp(op, L"...") != 0)
									return REPORT(false);
							}
							else
							{
								auto sym = get_DType(no_space);
								if (sym && !(sym->IsType() || sym->IsReservedType()))
								{
									bool consider_as_type = false;

									if (sym->MaskedType() == RESWORD)
									{
										auto sym_name = sym->Sym();

										if (is_cs_cpp_data_type((LPCWSTR)sym_name.Wide()))
											consider_as_type = true;
									}

									if (consider_as_type)
										return REPORT(false);
								}
							}
						}
					}

					// identifier block
					CodeBlock::Ptr b(
					    new CodeBlock((CodeBlock::block_type)(CodeBlock::b_identifier | CodeBlock::b_not_block |
					                                          CodeBlock::b_dont_split),
					                  ptr->src_code, L'.', no_sym + 1, end_pos));

					include_dereference(b, false);

					if (b->contains(caret_pos, false, true))
					{
						if (scoped_name)
							split_cs_cpp_stmt(b, sub_items);

						if (granular_start())
						{
							b->add_type_bit(CodeBlock::b_dont_split);
							sub_items(b);
						}

						return REPORT(true);
					}

					// exclude ref/ptr ops from data type range
					int data_type_end = no_space;
					if (no_sym + 1 != b->start_pos())
					{
						int tmpx = text.rfind_nospace(b->start_pos() - 1, start_pos, code_filter);
						if (tmpx != -1)
							data_type_end = tmpx;
					}

					CodeBlock::Ptr b1(
					    new CodeBlock((CodeBlock::block_type)(CodeBlock::b_data_type | CodeBlock::b_not_block),
					                  ptr->src_code, L'.', attrib_start == -1 ? start_pos : attrib_start, no_space));

					if (b1->contains(caret_pos, false, true))
					{
						// split only template args if appropriate
						wchar_t dt_end_ch = text.safe_at(data_type_end);
						if (dt_end_ch == '>' && is_template(data_type_end, dt_end_ch))
						{
							int tpl_start = get_matching_brace(data_type_end, false, code_filter);
							if (tpl_start != -1)
							{
								int old_end = b1->end_pos();

								b1->open_pos = tpl_start;
								b1->close_pos = data_type_end;
								b1->semicolon_pos = old_end;
								b1->open_char = '<';

								if (b1->scope_contains(caret_pos, false))
								{
									if (!split_cs_cpp_comma_delimited_scope(b1, sub_items))
										split_cs_cpp_single_param_scope(b1, sub_items);
								}
							}
						}

						if (granular_start())
						{
							b1->add_type_bit(CodeBlock::b_dont_split);
							sub_items(b1);

							if (attrib_start != -1)
							{
								CodeBlock::Ptr b2(new CodeBlock(
								    (CodeBlock::block_type)(CodeBlock::b_data_type | CodeBlock::b_not_block),
								    ptr->src_code, L'.', start_pos, no_space));

								if (b2->contains(caret_pos, false, true))
								{
									b2->add_type_bit(CodeBlock::b_dont_split);
									sub_items(b2);
								}
							}
						}
					}

					return REPORT(true);
				}
			}
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_single_param_scope(SSParse::CodeBlock::Ptr& ptr,
	                                             std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
		using namespace SSParse;

		auto rep = get_split_report(L"single_param", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"single_param", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->has_type_bit(CodeBlock::b_dont_split_scope))
			return REPORT(false);

		if (!ptr->has_open_close())
			return REPORT(false);

		if (!ptr->scope_contains(caret_pos, false))
			return REPORT(false);

		if (is_comment_or_string(ptr->open_pos))
			return REPORT(false);

		ptr->ensure_name_and_type(ch_states, is_cs);

		if (ptr->is_of_type(SSParse::CodeBlock::b_if) || ptr->is_of_type(SSParse::CodeBlock::b_else) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_else_if) || ptr->is_of_type(SSParse::CodeBlock::b_try) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_catch) || ptr->is_of_type(SSParse::CodeBlock::b_finally) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_vc_try) || ptr->is_of_type(SSParse::CodeBlock::b_vc_except) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_vc_finally) || ptr->is_of_type(SSParse::CodeBlock::b_for) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_switch) || ptr->is_of_type(SSParse::CodeBlock::b_do) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_while) || ptr->is_of_type(SSParse::CodeBlock::b_for_each) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_cs_foreach) || ptr->is_of_type(SSParse::CodeBlock::b_cs_using) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_cs_lock) || ptr->is_of_type(SSParse::CodeBlock::b_cs_unsafe) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_namespace) || ptr->is_of_type(SSParse::CodeBlock::b_class) ||
		    ptr->is_of_type(SSParse::CodeBlock::b_struct) || ptr->is_of_type(SSParse::CodeBlock::b_union))
		{
			return REPORT(false);
		}

		auto code_filter = make_code_filter();
		BraceCounter bc("()[]{}<>");
		bc.ang_filter = make_ang_filter();

		if (ptr->open_char == '{')
		{
			int pre_nospace = text.rfind_nospace(ptr->open_pos - 1);

			if (is_cpp)
			{
				if (pre_nospace == -1 || !text.is_one_of(pre_nospace, L"{(=,"))
					return REPORT(false);
			}
			else if (is_cs)
			{
				if (pre_nospace == -1 || (!text.is_one_of(pre_nospace, L")") && !text.is_identifier(pre_nospace)))
					return REPORT(false);
			}

			int post_nospace = text.find_nospace(ptr->close_pos + 1);
			if (post_nospace == -1 || !text.is_one_of(post_nospace, L"}),;"))
				return REPORT(false);
		}
		else if (ptr->open_char == '(')
		{
			int pre_nospace = text.rfind_nospace(ptr->open_pos - 1);
			if (pre_nospace == -1 || !text.is_identifier(pre_nospace))
			{
				return REPORT(false);
			}

			int post_nospace = text.find_nospace(ptr->close_pos + 1);
			if (post_nospace == -1 ||
			    (!text.is_identifier_start(post_nospace) && (!is_cpp || !is_return_type_operator(post_nospace)) &&
			     !text.is_one_of(post_nospace, L"{:;")))
			{
				return REPORT(false);
			}
		}

		int start_pos = text.find_nospace(ptr->open_pos + 1, ptr->close_pos - 1, code_filter);
		int end_pos = text.rfind_nospace(ptr->close_pos - 1, ptr->open_pos + 1, code_filter);

		if (text.safe_at(end_pos) == L';')
			return REPORT(false);

		if (start_pos == -1 || end_pos == -1 || end_pos <= start_pos)
			return REPORT(false);

		bool in_symbol = false;
		int num_symbols = 0;

		int i = start_pos;
		// try to find default parameter delimiter (first '=' from left)
		for (; i <= end_pos; i++)
		{
			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			bc.on_char(ch, i, nullptr);

			if (!bc.is_inside_braces())
			{
				if (ch == ',' || ch == ';')
					return REPORT(false);

				if (ISCSYM(ch))
					in_symbol = true;

				else if (!text.is_at_scope_delimiter(i, is_cs, true, code_filter))
				{
					if (in_symbol)
					{
						in_symbol = false;
						num_symbols++;
					}

					auto op = get_operator(i);
					if (op)
					{
						if (wcscmp(op, L"*") == 0 || wcscmp(op, L"&") == 0 || (is_cpp && wcscmp(op, L"^") == 0) ||
						    (is_cpp && wcscmp(op, L"%") == 0))
						{
							if (num_symbols == 0)
								return REPORT(false);
							else
								continue;
						}

						if (wcscmp(op, L"=") == 0)
							break;

						else if (wcscmp(op, L"...") == 0)
							break;

						else
							return REPORT(false);
					}
				}
				else
				{
					int tmp = text.find_csym(i, end_pos, code_filter);
					if (tmp != -1)
					{
						i = tmp - 1;
						continue;
					}

					return REPORT(false);
				}
			}
		}

		if (ptr->open_char == '(')
		{
			// if not type and name, return false

			if (num_symbols != 2)
				return REPORT(false);

			int tmp = text.rfind_nospace(i - 1, start_pos, code_filter);

			if (!text.is_identifier(tmp) && text.safe_at(tmp) != '.')
			{
				return REPORT(false);
			}
		}

		if (num_symbols == 0)
			return REPORT(false);

		if (!bc.is_inside_braces())
		{
			CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_parameter, ptr->src_code, L'.', start_pos, end_pos));

			split_cs_cpp_param(b, true, sub_items);
			split_open_close_prefix(b, sub_items);

			return REPORT(true);
		}

		return REPORT(false);
	}

	void split_cs_cpp_param(SSParse::CodeBlock::Ptr& ptr, bool split_default_value,
	                        std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		if (ptr->has_type_bit(CodeBlock::b_dont_split))
			return;

		if (!ptr->is_of_type(CodeBlock::b_parameter))
		{
			_ASSERTE(!"Pass only parameters to this method!");
			return;
		}

		auto rep = get_split_report(L"param", ptr->start_pos(), ptr->end_pos());
		if (rep.is_empty())
			split_report(*this, L"param", ptr->start_pos(), ptr->end_pos(), true);
		else
			return;

		auto code_filter = make_code_filter();
		BraceCounter bc("()[]{}<>");
		bc.ang_filter = make_ang_filter();

		int start_pos = ptr->start_pos();
		int end_pos = ptr->end_pos();

		if (text.safe_at(end_pos) == ',')
			end_pos = text.rfind_nospace(end_pos - 1, code_filter);

		int init_end_pos = end_pos;

		wchar_t ch = L'\0';

		int eq_pos = -1;

		// try to find default parameter delimiter (first '=' from left)
		for (int i = start_pos; i <= end_pos; i++)
		{
			ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			bc.on_char(ch, i, nullptr);

			if (bc.is_stack_empty() && ch == '=')
			{
				eq_pos = i;
				break;
			}
		}

		if (eq_pos != -1)
		{
			// we have a default parameter,
			// so push it as 2 separate blocks Left and Right

			// right
			int bR_start = text.find_nospace(eq_pos + 1, end_pos, code_filter);
			if (bR_start != -1 && bR_start != end_pos)
			{
				CodeBlock::Ptr bR(new CodeBlock((CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block),
				                                ptr->src_code, L'=', bR_start, end_pos));

				if (bR->contains(caret_pos, false, true))
				{
					if (granular_start())
						sub_items(bR);

					if (split_default_value)
					{
						split_cs_cpp_stmt(bR, sub_items);
					}
				}
			}

			// left part defines new end
			end_pos = text.rfind_nospace(eq_pos - 1, start_pos, code_filter);
		}

		if (end_pos != -1 && end_pos > start_pos)
		{
			CodeBlock::Ptr bTypeAndName(new CodeBlock((CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block),
			                                          ptr->src_code, L'=', start_pos, end_pos));

			if (bTypeAndName->contains(caret_pos, false, true))
			{
				bool resolved = false;

				if (split_cs_cpp_type_and_name(bTypeAndName, false, false, sub_items))
				{
					bTypeAndName->add_type_bit(CodeBlock::b_dont_split);
					resolved = true;
				}

				if (granular_start())
				{
					if (end_pos != init_end_pos)
						sub_items(bTypeAndName);
					else if (!resolved)
					{
						// no rule applicable, so split it as statement
						split_cs_cpp_stmt(ptr, sub_items);
					}
				}
			}
		}
	}

	bool get_numeric_literal_at_position(int pos, int& spos, int& epos, int* numeric_sig = nullptr)
	{
		int first = 0;
		int last = (int)num_literals.size() - 1;
		int mid;

		// use binary search, literals are sorted

		while (first <= last)
		{
			mid = (first + last) / 2;

			const auto& tup = num_literals[(uint)mid];

			int nsp = std::get<0>(tup);
			int nep = std::get<1>(tup);

			if (pos >= nsp && pos <= nep)
			{
				spos = nsp;
				epos = nep;

				if (numeric_sig)
				{
					if (std::get<2>(tup) == '.')
						*numeric_sig = 0;
					else
						*numeric_sig = 2;
				}

				return true;
			}
			else if (pos < nsp)
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

	int get_end_of_numeric_literal(int start_pos, SSParse::SharedWStr::CharFilter filter = nullptr)
	{
		using namespace SSParse;

		if (start_pos < 0)
			start_pos = caret_pos;

		int end_pos = -1;

		wchar_t ch_first = text.safe_at(start_pos);
		if (!wt_isdigit(ch_first))
		{
			if (ch_first == '-' || ch_first == '+' || ch_first == '.')
			{
				int x = text.rfind_nospace(start_pos - 1, filter);
				wchar_t xch = text.safe_at(x);
				if (ISCSYM(xch) || xch == ')' || xch == ']')
					return -1;

				x = text.find_nospace(start_pos + 1, filter);
				xch = text.safe_at(x);

				if ((ch_first == '-' || ch_first == '+') && xch == '.')
				{
					x = text.find_nospace(x + 1, filter);
					xch = text.safe_at(x);
				}

				if (!wt_isdigit(xch))
					return -1;
				else
					end_pos = x;
			}
			else
			{
				return -1;
			}
		}

		if (end_pos < start_pos)
			end_pos = start_pos;

		int dummy_sp = start_pos;
		if (get_numeric_literal_at_position(end_pos, dummy_sp, end_pos))
			return end_pos;

		return -1;
	}

	bool get_range(int at_pos, int& s_pos, int& e_pos, std::function<bool(int)> accept_pos = nullptr)
	{
		if (!text.is_valid_pos(at_pos))
			return false;

		if (accept_pos)
		{
			if (!accept_pos(at_pos))
				return false;

			s_pos = at_pos;
			while (s_pos > 1 && accept_pos(s_pos - 1))
				s_pos--;

			e_pos = at_pos;
			while (e_pos + 1 < (int)ch_states.size() && accept_pos(e_pos + 1))
				e_pos++;
		}

		return true;
	}

	bool get_state_range(int at_pos, int& s_pos, int& e_pos, SSParse::CharState mask, bool specific,
	                     std::function<bool(int)> accept_pos = nullptr)
	{
		if (at_pos < 0)
			at_pos = caret_pos;

		std::function<bool(int)> is_good_char;

		if (specific)
		{
			SSParse::CharState state = (SSParse::CharState)(ch_states[at_pos].state & mask);

			if (state == SSParse::chs_none)
				return false;

			is_good_char = [this, state, accept_pos](int pos) -> bool {
				return ch_states[pos].Contains(state) || (accept_pos && accept_pos(pos));
			};
		}
		else
		{
			if (!ch_states[at_pos].HasAnyBitOfMask(mask))
				return false;

			is_good_char = [this, accept_pos, mask](int pos) -> bool {
				return ch_states[pos].HasAnyBitOfMask(mask) || (accept_pos && accept_pos(pos));
			};
		}

		return get_range(at_pos, s_pos, e_pos, is_good_char);
	}

	bool is_operator(int at_pos, std::initializer_list<LPCWSTR> ops)
	{
		auto op = get_operator(at_pos);

		if (op)
		{
			for (auto x : ops)
				if (!wcscmp(op, x))
					return true;
		}

		return false;
	}

	LPCWSTR find_operator(int pos, bool _is_cs, SSParse::SharedWStr::CharFilter filter = nullptr)
	{
		pos = text.find_nospace(pos, filter);

		if (pos >= 0)
			return get_operator(pos);

		return nullptr;
	}

	LPCWSTR rfind_operator(int pos, bool _is_cs, SSParse::SharedWStr::CharFilter filter = nullptr)
	{
		pos = text.rfind_nospace(pos, filter);

		if (pos >= 0)
			return get_operator(pos);

		return nullptr;
	}

	bool is_single_identifier(SSParse::CodeBlock::Ptr& ptr)
	{
		int sp = ptr->start_pos();
		int ep = ptr->end_pos();

		for (int i = sp; i <= ep; i++)
			if (!text.is_identifier(i))
				return false;

		return true;
	}

	bool include_or_exclude_end(SSParse::CodeBlock::Ptr& ptr, bool include, LPCWSTR chs = L";")
	{
		if (word_start() && is_single_identifier(ptr))
			return false;

		using namespace SSParse;
		auto code_filter = make_code_filter();

		int end_pos = ptr->end_pos();

		std::wstring chars(chs);

		if (include)
		{
			if (text.is_one_of(end_pos, chars))
				return true;

			// include semicolon if appropriate
			int sc_pos = text.find_nospace(end_pos + 1, code_filter);
			if (text.is_one_of(sc_pos, chars))
			{
				if (ptr->close_pos == -1)
				{
					ptr->close_pos = end_pos;
					ptr->semicolon_pos = sc_pos;
				}
				else
				{
					ptr->semicolon_pos = sc_pos;
				}

				return true;
			}

			return false;
		}

		// else
		if (!text.is_one_of(end_pos, chars))
			return true;

		// exclude semicolon if appropriate
		int e_pos = text.rfind_nospace(end_pos - 1, code_filter);
		if (ptr->has_open_close())
		{
			if (ptr->close_pos == e_pos)
				ptr->semicolon_pos = -1;
			else
				ptr->semicolon_pos = e_pos;
		}
		else
		{
			ptr->close_pos = e_pos;
			ptr->semicolon_pos = -1;
		}

		return true;
	}

	bool insert_equal_operator_as_splitter(SSParse::CodeBlock::Ptr& ptr)
	{
		using namespace SSParse;
		auto code_filter = make_code_filter();

		int start_pos = ptr->start_pos();
		int end_pos = ptr->end_pos();

		int first_pos = text.find_nospace(start_pos, code_filter);
		wchar_t ch = text.safe_at(first_pos, code_filter);
		if (!wt_isalpha(ch) && ch != ':' && !text.is_at_scope_delimiter_start(first_pos, false, false, code_filter))
			return false;

		if (is_multi_statement_or_mismatch(start_pos, end_pos, "(){}{}"))
			return false;

		int eq = text.find(start_pos, end_pos, '=', code_filter);
		if (eq != -1)
		{
			if (!is_bracing_mismatch(start_pos, eq) && !is_bracing_mismatch(eq, end_pos))
			{
				int sp, ep;
				auto op = get_operator(eq, &sp, &ep);
				if (op != nullptr)
				{
					int op_len = ep - sp + 1;

					if ((op_len == 1 && *op == '=') || // simple assignment '='
					    (op_len == 2 && op[0] != '=')) // '*=', '+=' and o on, but not '==' nor '=>'
					{
						ptr->splitters.insert(std::make_tuple(sp, op_len));
						return true;
					}
				}
			}
		}

		return false;
	}

	enum class split_mode
	{
		// Every splitter is used as delimiter of the block
		// so the result is n+1 count of parts.
		delimiter,

		// Similar as delimiter, except it passes
		// only those blocks which contain caret.
		// Also it creates extra blocks to mimic
		// extending and shrinking in left to right order.
		caret_extend_L2R,

		// Every splitter is used to cut the block into two parts.
		// Each such block is then passed to inserter.
		middle_point,

		mode_mask = 0x00FF, // mask for insertions modes
		no_filter = 0x1000, // no code filtering is done
		no_search = 0x2000, // no searching for no-space is done
	};

	void split_by_splitters(int start_pos, int end_pos, split_mode mode,
	                        const std::set<std::tuple<int, int>>& splitters,
	                        std::function<bool(SSParse::CodeBlock::Ptr, std::tuple<int, int>)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		auto code_filter = ((DWORD)mode & (DWORD)split_mode::no_filter) ? nullptr : make_code_filter();
		bool dont_search = ((DWORD)mode & (DWORD)split_mode::no_search) == (DWORD)split_mode::no_search;

		int L2R_start = -1;
		bool cancel = false;

		mode = (split_mode)((DWORD)mode & (DWORD)split_mode::mode_mask);

		auto insert_L2R = [&](SSParse::CodeBlock::Ptr b, const std::tuple<int, int>& sp) {
			// calculate left to right start position
			if (L2R_start < b->start_pos() && b->virtual_start_pos() <= caret_pos)
			{
				L2R_start = b->start_pos();
			}

			if (b->contains(caret_pos, false, true))
			{
				cancel = !sub_items(b, sp);
			}

			if (L2R_start != -1 && L2R_start <= b->start_pos())
			{
				SSParse::CodeBlock::Ptr leftToRight(
				    new CodeBlock(CodeBlock::b_undefined, text, L'.', L2R_start, b->end_pos()));

				if (leftToRight->contains(caret_pos, false, true))
				{
					cancel = !sub_items(leftToRight, sp);
				}
			}

			SSParse::CodeBlock::Ptr rightToLeft(
			    new CodeBlock(CodeBlock::b_undefined, text, L'.', b->start_pos(), end_pos));

			if (rightToLeft->contains(caret_pos, false, true))
			{
				cancel = !sub_items(rightToLeft, sp);
			}
		};

		if (mode == split_mode::delimiter || mode == split_mode::caret_extend_L2R)
		{
			auto& sset = splitters;
			const std::tuple<int, int>* prev = nullptr;

			for (const std::tuple<int, int>& sp : sset)
			{
				if (cancel)
					break;

				int left_pos = !prev ? start_pos : std::get<0>(*prev) + std::get<1>(*prev);
				int right_pos = std::get<0>(sp) - 1;

				prev = &sp;

				if (!dont_search)
				{
					left_pos = text.find_nospace(left_pos, right_pos, code_filter);
					right_pos = text.rfind_nospace(right_pos, left_pos, code_filter);
				}

				if (left_pos != -1 && right_pos >= left_pos)
				{
					CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, text, L'.', left_pos, right_pos));

					if (mode == split_mode::caret_extend_L2R)
						insert_L2R(b, sp);
					else
						cancel = !sub_items(b, sp);
				}
			}

			if (prev != nullptr && !cancel)
			{
				int left_pos = std::get<0>(*prev) + std::get<1>(*prev);
				int right_pos = end_pos;

				if (!dont_search)
				{
					left_pos = text.find_nospace(left_pos, right_pos, code_filter);
					right_pos = text.rfind_nospace(right_pos, left_pos, code_filter);
				}

				if (left_pos != -1 && right_pos >= left_pos)
				{
					CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, text, L'.', left_pos, right_pos));

					if (mode == split_mode::caret_extend_L2R)
						insert_L2R(b, *prev);
					else
						cancel = !sub_items(b, *prev);
				}
			}
		}
		else if (mode == split_mode::middle_point)
		{
			for (std::tuple<int, int> sp : splitters)
			{
				if (cancel)
					break;

				int start_pre = std::get<0>(sp) - 1;
				int start_post = std::get<0>(sp) + std::get<1>(sp);

				int pre = dont_search ? start_pre : text.rfind_nospace(start_pre, start_pos, code_filter);
				int post = dont_search ? start_post : text.find_nospace(start_post, end_pos, code_filter);

				if (pre != -1 && pre != start_pos)
				{
					CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, text, L'.', start_pos, pre));

					cancel = !sub_items(b, sp);
				}

				if (post != -1 && post != end_pos)
				{
					CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, text, L'.', post, end_pos));

					cancel = !sub_items(b, sp);
				}
			}
		}
	}

	split_report split_open_close_prefix(SSParse::CodeBlock::Ptr& ptr,
	                                     std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
		using namespace SSParse;

		auto rep = get_split_report(L"prefix", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"prefix", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (!ptr->has_open_close() || ptr->open_char_is_one_of("<["))
			return REPORT(false);

		if (!granular_start())
			return REPORT(false);

		int end_pos = text.rfind_nospace(ptr->open_pos - 1);

		if (end_pos > ptr->start_pos())
		{
			CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, text, L'.', ptr->start_pos(), end_pos));

			if (!split_cs_cpp_type_and_name(b, false, false, sub_items))
				split_cs_cpp_stmt(b, sub_items);

			b->add_type_bit(CodeBlock::b_dont_split);
			sub_items(b);

			return REPORT(true);
		}

		return REPORT(false);
	}

	bool is_return_type_operator(int pos)
	{
		if (!is_cpp)
		{
			_ASSERTE(!"Method is designed for C++ only.");
			return false;
		}

		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
		return return_type_ops.find(pos) != return_type_ops.end();
	}

	bool cache_return_type_operator(int pos, SSParse::SharedWStr::CharFilter filter)
	{
		_ASSERTE(is_preparing_tree);

		if (!is_cpp)
		{
			_ASSERTE(!"Method is designed for C++ only.");
			return false;
		}

		auto cache_result = [&](bool rslt) -> bool {
			if (rslt)
			{
				IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
				return_type_ops.insert(pos);
			}

			return rslt;
		};

		wchar_t op_ch = text.safe_at(pos);
		if (op_ch == '-' || op_ch == '>')
		{
			int sp, ep;
			auto op = get_operator(pos, &sp, &ep);
			if (op && wcscmp(op, L"->") == 0)
			{
				// return type operator must be after ')' ...) -> type
				int prev = text.rfind_nospace(sp - 1, filter);
				wchar_t prev_ch = text.safe_at(prev);
				if (prev_ch != ')')
				{
					if (prev_ch == 'r' || prev_ch == 'e' || prev_ch == 't')
					{
						auto sym = text.get_symbol(prev, true);
						if (sym == L"mutable" || sym == L"constexpr" || sym == L"consteval" || sym == L"constinit" || sym == L"noexcept")
							return cache_result(true); // lambda
					}

					return cache_result(false);
				}

				// after the return type operator the type must follow
				int next = text.find_nospace(ep + 1, filter);
				if (text.is_identifier_start(next))
					cache_result(is_type(next));
			}
		}

		return cache_result(false);
	}

	bool is_deref_operator(int pos)
	{
		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
		return deref_ops.find(pos) != deref_ops.end();
	}

	bool cache_deref_operator(int pos, SSParse::SharedWStr::CharFilter filter)
	{
		_ASSERTE(is_preparing_tree);

		auto cache_result = [&](bool rslt) -> bool {
			if (rslt)
			{
				IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
				deref_ops.insert(pos);
			}

			return rslt;
		};

		wchar_t op_ch = text.safe_at(pos);
		if (op_ch == '*' || op_ch == '&' || op_ch == '^' || op_ch == '%')
		{
			int next = text.find_nospace(pos + 1, filter);
			wchar_t next_ch = text.safe_at(next);

			if (next_ch == '=' ||                   // *= or &= or %= or ^=
			    (op_ch == '&' && next_ch == '&') || // &&
			    wt_isdigit(next_ch))                // & 123 * 123
				return cache_result(false);

			int tmp = text.rfind_nospace(pos - 1, filter);
			wchar_t tmp_ch = text.safe_at(tmp);
			if (!ISCSYM(tmp_ch) && tmp_ch != ')' && tmp_ch != ']')
			{
				if (tmp == pos - 1 && tmp_ch == '&' && op_ch == '&')
					return cache_result(false); // &&

				if ((op_ch == '*' || op_ch == '^') && tmp_ch == op_ch) // ** or ^^
					return cache_result(true);

				if (tmp_ch == '>' && is_template(tmp))
					return cache_result(true);

				auto op = get_operator(tmp);
				if (op)
					return cache_result(true);
			}

			if (tmp_ch == 'n' || tmp_ch == 'd')
			{
				int ssym = text.rfind_not_csym(tmp - 1, filter);
				if (ssym != -1)
				{
					auto sym = text.substr_se(ssym + 1, tmp, filter);
					if (sym == L"return" || sym == L"co_return" || sym == L"co_yield")
					{
						return cache_result(true);
					}
				}
			}

			if (ISCSYM(tmp_ch))
			{
				auto sym = get_DType(tmp);
				if (sym && (sym->IsType() || sym->IsReservedType()))
					return cache_result(true);
			}

			tmp = text.find_nospace(pos + 1, filter);
			tmp_ch = text.safe_at(tmp);

			if (tmp_ch == op_ch)
				return cache_result(true);

			if (tmp_ch == '.')
			{
				tmp = text.find_nospace(tmp + 1, filter);
				tmp_ch = text.safe_at(tmp);
			}

			if (wt_isdigit(tmp_ch))
				return cache_result(false);

			if (is_cs)
				return cache_result(false);

			if (ISCSYM(tmp_ch))
			{
				auto sym = get_DType(tmp);
				if (sym)
				{
					if ((op_ch == '*' || op_ch == '^') && sym->IsPointer())
						return cache_result(true);

					if ((op_ch == '&' || op_ch == '%') && sym->IsReservedType())
						return cache_result(true);
				}
			}
			else if (tmp_ch == '(')
			{
				int cpos = get_matching_brace(tmp, false, filter);
				if (cpos != 0 && cpos > tmp)
				{
					EdCntPtr ed(g_currentEdCnt);
					if (ed)
					{
						ULONG offsetToSym;
						if (pos_wide_to_buffer(tmp, offsetToSym))
						{
							WTString srcBuf = ed->GetBuf();
							MultiParsePtr mp = ed->GetParseDb();
							WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
							WTString scope = ::MPGetScope(srcBuf, mp, (int)offsetToSym);
							WTString expr(text.substr_se(tmp + 1, cpos - 1, filter).c_str());

							InferType infer;
							auto wtstr = infer.Infer(expr, scope, bcl, mp->FileType());
							if (wtstr != infer.GetDefaultType())
							{
								if (op_ch == '&' || op_ch == '%')
									return cache_result(true);

								return cache_result(wtstr.find_first_of(_T("*^")) >= 0);
							}
						}
					}
				}
			}
		}

		return cache_result(false);
	}

	void generate_open_close_by_caret(SSParse::CodeBlock::Ptr& ptr)
	{
		if (ptr->has_open_close())
			return;

		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));

		for (auto p : brace_map)
		{
			if (ptr->contains(p.first, false, false) && ptr->contains(p.second, false, false) &&
			    !is_comment_or_string(p.first))
			{
				if (caret_pos >= p.first && caret_pos <= p.second)
				{
					int tmp = ptr->end_pos();

					ptr->open_pos = __min(p.first, p.second);
					ptr->close_pos = __max(p.first, p.second);
					ptr->open_char = text.safe_at(ptr->open_pos);
					ptr->semicolon_pos = -1;

					ptr->add_type_bit(SSParse::CodeBlock::b_not_block);

					if (tmp > ptr->close_pos)
						ptr->semicolon_pos = tmp;

					return;
				}
			}
		}
	}

	bool include_dereference(SSParse::CodeBlock::Ptr& ptr, bool strict_typing, int num_levels = 10)
	{
		auto code_filter = make_code_filter();

		int s_pos = ptr->start_pos();

		if (!text.is_identifier_start(s_pos) && text.safe_at(s_pos) != '(')
		{
			return false;
		}

		for (int q = 0; q < num_levels; q++)
		{
			_ASSERTE(q < 5);

			int left_op = text.rfind_nospace(s_pos - 1, code_filter);

			if (!strict_typing && text.is_one_of(left_op, L"*&^%"))
				s_pos = left_op;
			else if (strict_typing && is_deref_operator(left_op))
				s_pos = left_op;
			else
				break;
		}

		if (s_pos != ptr->start_pos())
		{
			ptr->top_pos = s_pos;
			return true;
		}

		return false;
	}

	split_report split_cs_cpp_comma_delimited_scope(SSParse::CodeBlock::Ptr& ptr,
	                                                std::function<void(SSParse::CodeBlock::Ptr)> sub_items,
	                                                std::function<void(int)> on_comma = nullptr)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		fix_block_open_close(ptr);

		int report_start = ptr->open_pos;
		if (report_start == -1)
			report_start = ptr->top_pos;

		int report_end = ptr->close_pos;
		if (report_end == -1)
			report_end = ptr->semicolon_pos;

		//		IF_DBG(auto str = text.substr_se(report_start, report_end));

		auto rep = get_split_report(L"arg_list", report_start, report_end);
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"arg_list", report_start, report_end, val);
		};

		if (!ptr->has_open_close() || is_comment_or_string(ptr->open_pos))
			return REPORT(false);

		ptr->ensure_name_and_type(ch_states, is_cs);

		if (ptr->is_of_curly_scope_type())
		{
			return REPORT(false);
		}

		if (ptr && ptr->scope_contains(caret_pos, false) &&
		    ((ptr->is_declaration() || ptr->is_definition_header()) ||
		     (ptr->open_char == '(' && ptr->open_pos != -1 && is_arg_list(ptr->open_pos, ptr->close_pos)) ||
		     // [case: 90797] allow also template args to increase granularity
		     (ptr->open_char == '<' && ptr->open_pos != -1 && is_arg_list(ptr->open_pos, ptr->close_pos)) ||
		     (ptr->open_char == '[' && ptr->open_pos != -1 && is_arg_list(ptr->open_pos, ptr->close_pos)) ||
		     is_initializer_list(ptr) || ptr->is_of_type(SSParse::CodeBlock::b_enum)))
		{
			return split_cs_cpp_comma_delimited_stmt(ptr, true, sub_items, on_comma);
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_comma_delimited_stmt(SSParse::CodeBlock::Ptr& ptr, bool prefer_scope,
	                                               std::function<void(SSParse::CodeBlock::Ptr)> sub_items,
	                                               std::function<void(int)> on_comma = nullptr)
	{
		auto rep = get_split_report(L"comma_delim_stmt", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"comma_delim_stmt", ptr->start_pos(), ptr->end_pos(), val);
		};

		enum
		{
			LT_TMP = 0xF3, // temporary replacement for '<'
			GT_TMP = 0xF2  // temporary replacement for '>'
		};

		auto filter = make_code_filter();

		// counts < and >
		// < is for +1
		// > is for -1
		auto count_angs = [](LPCWSTR wstr, int& rslt) {
			for (LPCWSTR x = wstr; x && *x; x++)
			{
				if (*x == '<')
					rslt++;
				else if (*x == '>')
					rslt--;
			}
		};

		bool template_mode = !ptr->is_of_type(SSParse::CodeBlock::b_enum);

		SSParse::BraceCounter bc;
		bc.set_ignore_angle_brackets(!template_mode);
		if (template_mode)
			bc.ang_filter = make_ang_filter();

		int s_pos = ptr->start_pos();
		int e_pos = ptr->end_pos();

		std::vector<int> delims;

		if (prefer_scope && !ptr->has_type_bit(SSParse::CodeBlock::b_dont_split_scope) && ptr->has_open_close())
		{
			s_pos = text.find_nospace(ptr->open_pos + 1, filter);
			e_pos = text.rfind_nospace(ptr->close_pos - 1, filter);
		}
		else if (ptr->semicolon_pos == e_pos)
		{
			e_pos = text.rfind_nospace(e_pos - 1, filter);
		}

		delims.push_back(s_pos - 1);

		for (int i = s_pos; i <= e_pos; i++)
		{
			if (!ch_states[i].HasAnyBitOfMask(SSParse::chs_no_code))
			{
				wchar_t ch = text.at(i);

				if (wt_isspace(ch))
					continue;

				bc.on_char(ch, i, &ch_states);

				if (ch == ',' && bc.is_none_or_closed())
					delims.push_back(i);
			}
		}

		delims.push_back(e_pos + 1);

		if (delims.size() > 2)
		{
			std::vector<token_data> tokens;

			for (size_t i = 1; i < delims.size(); i++)
			{
				token_data td;
				td.s_pos = delims[i - 1] + 1;
				td.e_pos = delims[i] - 1;

				if (!text.is_valid_pos(td.s_pos) || !text.is_valid_pos(td.e_pos))
				{
					_ASSERTE(!"Invalid delimiter");
					continue;
				}

				// read code in collapsed way
				auto code = SSParse::Parser::ReadCodeEx(
				    text, ch_states, td.s_pos, [&td](int pos, std::wstring& wstr) { return pos >= td.e_pos; }, true,
				    true, true, 1, L"({[");

				td.is_tmpl = false;
				td.code_text = code.first.c_str(); // collapsed code
				td.code_pos = code.second;         // corresponding original buffer positions of chars in result

				if (!template_mode)
					tokens.push_back(td);
				else
				{
					// replace known operators by safe variants
					// Note: preserve length so that code_pos is valid
					for (int c = 1; c < (int)td.code_text.length(); c++)
					{
						wchar_t& ch0 = td.code_text[uint(c - 1)];
						wchar_t& ch1 = td.code_text[(uint)c];

						if ((ch0 == '>' && ch1 == '=') || (ch0 == '<' && ch1 == '=') || (ch0 == '-' && ch1 == '>') ||
						    (ch0 == '=' && ch1 == '>') ||
						    (ch0 == '<' &&
						     ch1 == '<') // don't handle ">>" as it could be on end of template: tmpl<tmpl1<bool>>
						)
						{
							if (ch0 == '<')
								ch0 = LT_TMP;
							else if (ch0 == '>')
								ch0 = GT_TMP;

							if (ch1 == '<')
								ch1 = LT_TMP;
							else if (ch1 == '>')
								ch1 = GT_TMP;
						}
					}

					td.ang_sum = 0;

					// count angle braces '<' = +1, '>' = -1
					count_angs(td.code_text.c_str(), td.ang_sum);

					if (tokens.empty() || tokens.back().ang_sum <= 0)
						// count mismatch - this is not a template
						tokens.push_back(td);
					else
					{
						token_data& prev = tokens.back();

						// detected already as template,
						// just add new until token is closed
						if (prev.is_tmpl)
						{
							prev.e_pos = td.e_pos;
							prev.ang_sum += td.ang_sum;
							prev.code_text += L"," + td.code_text;
							prev.code_pos.clear();

							// if this fails, this is probably not a template!!!
							_ASSERTE(prev.ang_sum >= 0);

							continue;
						}

						// first opening <
						int open_pos = (int)prev.code_text.find(L'<');
						if (open_pos >= 0)
						{
							if (is_template(prev.code_pos[(uint)open_pos], L'<'))
							{
								// this is a template parameter start
								prev.e_pos = td.e_pos;
								prev.ang_sum += td.ang_sum;
								prev.code_text += L"," + td.code_text;
								prev.code_pos.clear();
								prev.is_tmpl = true;
								continue;
							}

							// not a type, just expression,
							// add current token to the end
							tokens.push_back(td);
							continue;
						}
						else
						{
							_ASSERTE(!"Something went wrong!");
							tokens.push_back(td);
						}
					}
				}
			}

			if (template_mode)
			{
				for (auto& td : tokens)
				{
					if (on_comma)
					{
						// remove all <...> blocks that contain
						// a parameter list separator. This may happen in
						// cases when in params are < > operators
						// such as: less < more, more > less
						if (td.e_pos != ptr->close_pos && ptr->open_char != '<' // [case: 90797] not for <...> blocks!
						)
							on_comma(td.e_pos);
					}

					// replace back all altered (<= >= ->) operators
					for (wchar_t& ch : td.code_text)
					{
						if (ch == LT_TMP)
							ch = '<';
						else if (ch == GT_TMP)
							ch = '>';
					}
				}
			}

			int start_index = -1;
			int end_pos_trimmed = text.rfind_nospace(ptr->close_pos - 1, ptr->start_pos(), filter);
			std::vector<SSParse::CodeBlock::Ptr> blocks;

			auto inserter = [&](int s_pos, int e_pos) {
				if (s_pos == -1)
					return;

				int scope_sep = text.rfind_scope_delimiter_start(s_pos - 1, is_cs, true, filter);
				if (scope_sep != -1)
					s_pos = scope_sep;

				SSParse::CodeBlock::Ptr b(new SSParse::CodeBlock(SSParse::CodeBlock::b_undefined, ptr->src_code, L';',
				                                                 start_index == -1 ? s_pos : start_index, e_pos));

				include_or_exclude_end(b, true, L",");

				if (b->contains(caret_pos, false, true))
				{
					if (start_index < b->start_pos() && b->start_pos() <= caret_pos)
					{
						start_index = b->start_pos();
					}

					if (!ActiveSettings().block_include_header)
					{
						b->ensure_name_and_type(ch_states, is_cs);
						if (!b->is_parent_type())
							blocks.push_back(b);
					}
					else
					{
						blocks.push_back(b);
					}
				}

				SSParse::CodeBlock::Ptr b1(new SSParse::CodeBlock(SSParse::CodeBlock::b_undefined, ptr->src_code, L';',
				                                                  s_pos, end_pos_trimmed));

				include_or_exclude_end(b1, true, L",");

				if (b1->contains(caret_pos, false, true))
				{
					blocks.push_back(b1);
				}
			};

			// generate selection ranges
			for (size_t i = 0; i < tokens.size(); i++)
			{
				// trim the token range
				int trimmed_e_pos = text.rfind_nospace(tokens[i].e_pos, filter);
				int trimmed_s_pos = text.find_nospace(tokens[i].s_pos, filter);

				if (granular_start())
				{
					// [case: 90797] increased granularity
					inserter(trimmed_s_pos, trimmed_e_pos);
				}

				SSParse::CodeBlock::Ptr block(
				    new SSParse::CodeBlock(SSParse::CodeBlock::b_parameter, text, ',', trimmed_s_pos, trimmed_e_pos));

				include_or_exclude_end(block, true, L",");

				if (block->contains(caret_pos, false, true))
				{
					block->name = tokens[i].code_text;

					// [case: 90797] split parameter into left and right parts
					split_cs_cpp_param(block, true, [&](SSParse::CodeBlock::Ptr b) {
						include_or_exclude_end(b, true, L",");

						sub_items(b);
					});

					block->type = SSParse::CodeBlock::b_parameter;
					sub_items(block);
				}
			}

			if (granular_start())
			{
				for (const auto& bptr : blocks)
					sub_items(bptr);

				if (!blocks.empty())
					split_open_close_prefix(ptr, sub_items);
			}

			ptr->type = (SSParse::CodeBlock::block_type)(ptr->type | SSParse::CodeBlock::b_dont_split_scope);

			return REPORT(!blocks.empty() || !granular_start());
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_native_scoped_stmt(const SSParse::CodeBlock::Ptr& cpRef,
	                                             std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
		using namespace SSParse;

		SSParse::CodeBlock::Ptr ptr(cpRef);

		auto rep = get_split_report(L"scoped_stmt", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"scoped_stmt", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->has_type_bit(CodeBlock::b_dont_split))
			return REPORT(false);

		if (ptr->is_directive() || ptr->is_modifier())
			return REPORT(false);

		auto is_required_type = [&](const SSParse::CodeBlock::Ptr& bPtr) {
			bPtr->ensure_name_and_type(ch_states, is_cs);

			return bPtr->is_of_type(CodeBlock::b_if) || bPtr->is_of_type(CodeBlock::b_else) ||
			       bPtr->is_of_type(CodeBlock::b_else_if) || bPtr->is_of_type(CodeBlock::b_try) ||
			       bPtr->is_of_type(CodeBlock::b_catch) || bPtr->is_of_type(CodeBlock::b_finally) ||
			       bPtr->is_of_type(CodeBlock::b_vc_try) || bPtr->is_of_type(CodeBlock::b_vc_except) ||
			       bPtr->is_of_type(CodeBlock::b_vc_finally) || bPtr->is_of_type(CodeBlock::b_for) ||
			       bPtr->is_of_type(CodeBlock::b_switch) || bPtr->is_of_type(CodeBlock::b_do) ||
			       bPtr->is_of_type(CodeBlock::b_while) || bPtr->is_of_type(CodeBlock::b_for_each) ||
			       bPtr->is_of_type(CodeBlock::b_cs_foreach) || bPtr->is_of_type(CodeBlock::b_cs_using) ||
			       bPtr->is_of_type(CodeBlock::b_cs_lock) || bPtr->is_of_type(CodeBlock::b_cs_unsafe);
		};

		if (!is_required_type(ptr))
			return REPORT(false);

		int iters_count = 0;

		auto code_filter = make_code_filter();

		while (ptr && ++iters_count < 500)
		{
			int h_opos = text.find(ptr->start_pos(), L'(', code_filter);
			if (h_opos != -1)
			{
				int h_cpos = get_matching_brace(h_opos, false, code_filter, ptr->start_pos(), ptr->end_pos());
				if (h_cpos != -1)
				{
					// try to find opening curly brace
					int s_opos = text.find_nospace(h_cpos + 1, ptr->end_pos(), code_filter);
					if (s_opos != -1)
					{
						// not scoped statement
						if (text.safe_at(s_opos) != '{')
						{
							return split_cs_cpp_native_unscoped_stmt(ptr, true, sub_items);
						}

						int s_cpos = get_matching_brace(s_opos, false, code_filter, s_opos, ptr->end_pos());
						if (s_cpos != -1)
						{
							int old_end_pos = ptr->end_pos();

							int semicolon_pos = text.find_nospace(s_cpos + 1, ptr->end_pos(), code_filter);
							if (text.safe_at(semicolon_pos) != ';')
								semicolon_pos = -1;

							bool hdr = caret_pos >= h_cpos && caret_pos <= h_cpos;

							SSParse::CodeBlock::Ptr top(new SSParse::CodeBlock(
							    SSParse::CodeBlock::b_undefined, ptr->src_code, hdr ? L'(' : L'{', ptr->start_pos(),
							    hdr ? h_opos : s_opos, hdr ? h_cpos : s_cpos, semicolon_pos));

							top->ensure_name_and_type(ch_states, is_cs);

							sub_items(top);

							if (semicolon_pos != -1)
								return REPORT(true);

							int new_start_pos = text.find_nospace(top->end_pos() + 1, old_end_pos, code_filter);

							if (new_start_pos == -1)
								return REPORT(true);

							ptr.reset(new SSParse::CodeBlock(SSParse::CodeBlock::b_full_stmt, ptr->src_code, L';',
							                                 new_start_pos, old_end_pos));

							if (!is_required_type(ptr))
							{
								ptr->add_type_bit(CodeBlock::b_full_stmt);
								sub_items(ptr);
								return REPORT(true);
							}
						}
					}
				}
			}
		}

		return REPORT(false);
	}

	split_report split_cs_cpp_native_unscoped_stmt(const SSParse::CodeBlock::Ptr& cpRef, bool split_subparts,
	                                               std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		SSParse::CodeBlock::Ptr ptr(cpRef);

		// [case: 94946]
		// split blocks like: if(b) if(b) if(c) if(d) anything;

		using namespace SSParse;

		auto rep = get_split_report(L"unscoped_stmt", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"unscoped_stmt", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->has_type_bit(CodeBlock::b_dont_split))
			return REPORT(false);

		if (ptr->is_directive() || ptr->is_modifier())
			return REPORT(false);

		auto is_of_required_type = [&](SSParse::CodeBlock::Ptr& bp) {
			bp->ensure_name_and_type(ch_states, is_cs);

			return bp->is_of_type(CodeBlock::b_if) || bp->is_of_type(CodeBlock::b_else) ||
			       bp->is_of_type(CodeBlock::b_else_if) || bp->is_of_type(CodeBlock::b_for) ||
			       bp->is_of_type(CodeBlock::b_do) || bp->is_of_type(CodeBlock::b_while) ||
			       bp->is_of_type(CodeBlock::b_for_each) || bp->is_of_type(CodeBlock::b_cs_foreach) ||
			       bp->is_of_type(CodeBlock::b_cs_using) || bp->is_of_type(CodeBlock::b_cs_lock) ||
			       bp->is_of_type(CodeBlock::b_cs_unsafe);
		};

		bool is_last = false;
		int level;

		for (level = 0; level < 100; level++)
		{
			if (is_of_required_type(ptr))
			{
				fix_block_open_close(ptr);

				if (split_subparts && ptr->has_open_close())
				{
					split_cs_cpp_round_scope(ptr, sub_items);
				}

				auto code_filter = make_code_filter();
				int start_pos = ptr->start_pos();
				int end_pos = ptr->end_pos();

				int o_pos = text.find_one_of(start_pos, end_pos, L"({;", code_filter);
				wchar_t o_char = text.safe_at(o_pos);

				wchar_t valid_o_char = ptr->is_of_type(CodeBlock::b_do) ? L'{' : L'(';

				if (o_char == valid_o_char && !is_last)
				{
					int c_pos = get_matching_brace(o_pos, false, code_filter, start_pos, end_pos);
					if (c_pos == -1)
						return REPORT(false);

					int next_pos = text.find_nospace(c_pos + 1, code_filter);
					wchar_t next_ch = text.safe_at(next_pos);

					if (next_ch == '{')
						return REPORT(false);

					if (next_ch == '}' || next_ch == ';')
						return REPORT(false);

					wchar_t next_open_ch = '.';

					int next_open = text.find_one_of(next_pos, end_pos, L"({;", code_filter);
					int next_close = -1;
					if (next_open != -1)
					{
						next_open_ch = text.safe_at(next_open);

						if (next_open_ch == ';')
						{
							next_open = -1;
							next_open_ch = '.';
						}
						else
						{
							next_close = get_matching_brace(next_open, false, code_filter, next_open, end_pos);
							if (next_close == -1)
							{
								next_open = -1;
								next_open_ch = '.';
							}
						}
					}

					CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, next_open_ch, next_pos,
					                               next_open, next_close, end_pos));

					if (b->contains(caret_pos, false, true))
					{
						if (!is_of_required_type(b))
						{
							if (b->open_pos != -1)
							{
								// This is most probably some method call,
								// or some expression, so make it generic.

								b->open_pos = -1;
								b->close_pos = b->end_pos();
								b->open_char = '.';

								int sc_pos = text.rfind_nospace(b->end_pos(), b->start_pos(), code_filter);
								if (sc_pos != -1 && text.safe_at(sc_pos) == ';')
									b->semicolon_pos = sc_pos;
								else
									b->semicolon_pos = -1;

								b->name.clear();
								b->ensure_name_and_type(ch_states, is_cs);
								b->type = CodeBlock::b_expression;
							}

							// add b_full_stmt so block selection selects this
							b->add_type_bit(CodeBlock::b_full_stmt);

							sub_items(b); // pass AFTER changes!!!

							if (split_subparts)
								split_cs_cpp_stmt(b, sub_items);

							return REPORT(level > 0);
						}

						sub_items(b);
					}

					ptr = b;

					auto id = text.get_symbol(next_pos);
					if (id == L"else")
					{
						// Anything what is after "else" except '{' and ';' is new statement.

						next_pos = text.find_not_csym(next_pos, end_pos, code_filter);
						if (next_pos != -1)
						{
							next_pos = text.find_nospace(next_pos, end_pos, code_filter);
							if (next_pos != -1)
							{
								next_ch = text.safe_at(next_pos);

								if (next_ch != '{' && next_ch != '}' && next_ch != ';')
								{
									CodeBlock::Ptr b1(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, '.',
									                                next_pos, next_open, next_close, end_pos));

									if (b1->contains(caret_pos, false, true))
										sub_items(b1);

									ptr = b1;
								}
							}
						}
					}
				}
			}
			else
			{
				break;
			}
		}

		return REPORT(level > 0);
	}

	split_report get_split_report(const std::wstring& name, int s_pos, int e_pos)
	{
		auto key = split_report::create_key(s_pos, e_pos);

		auto check_set = solved_stmts.find(name);

		if (check_set != solved_stmts.end())
		{
			auto it = check_set->second.find(key);
			if (it != check_set->second.end())
			{
				return *it;
			}
		}

		return key;
	}

	bool is_global_scope_delimiter_start(int pos)
	{
		if (!is_cpp)
		{
			_ASSERTE(!"Method is designed for C++ only.");
			return false;
		}

		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
		return global_scope_starts.find(pos) != global_scope_starts.end();
	}

	bool cache_global_scope_delimiter_start(int pos)
	{
		_ASSERTE(is_preparing_tree);

		auto code_filter = make_code_filter();

		if (!is_cpp)
		{
			_ASSERTE(!"Method is designed for C++ only.");
			return false;
		}

		auto cache_result = [&](bool rslt) -> bool {
			if (rslt)
			{
				IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
				global_scope_starts.insert(pos);
			}

			return rslt;
		};

		if (!text.is_valid_pos(pos))
			return cache_result(false);

		if (is_comment_or_string(pos))
			return cache_result(false);

		if (!text.is_at_scope_delimiter_start(pos, is_cs, false, code_filter))
			return cache_result(false);

		int pre_op = text.rfind_nospace(pos - 1, code_filter);
		if (pre_op != -1)
		{
			wchar_t ch = text.safe_at(pre_op);
			if (!ISCSYM(ch) && ch != '>')
				return cache_result(true);
			else
			{
				if (ch == '>')
				{
					if (!is_template(pre_op, ch))
						return cache_result(true);
				}
				else if (ISCSYM(ch))
				{
					// if there is no space, consider it connected
					// so don't test for type, we will "trust to user"
					if (pre_op + 1 == pos)
						return cache_result(false);

					auto dt = get_DType(pre_op);
					if (!dt || !dt->IsType())
						return cache_result(true);
				}
				else
				{
					return cache_result(true);
				}
			}
		}

		return cache_result(false);
	}

	split_report split_cs_cpp_for_stmt(SSParse::CodeBlock::Ptr& ptr,
	                                   std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		auto rep = get_split_report(L"for", ptr->start_pos(), ptr->end_pos());
		if (!rep.is_empty())
			return rep;

		auto REPORT = [&](bool val) -> split_report {
			return split_report(*this, L"for", ptr->start_pos(), ptr->end_pos(), val);
		};

		if (ptr->has_type_bit(CodeBlock::b_dont_split_scope))
			return REPORT(false);

		ptr->ensure_name_and_type(ch_states, is_cs);

		if (ptr->is_of_type(CodeBlock::b_for) || ptr->is_of_type(CodeBlock::b_for_each) ||
		    ptr->is_of_type(CodeBlock::b_cs_foreach))
		{
			auto code_filter = make_char_filter((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive));

			BraceCounter bcx("(){}[]<>");
			bcx.set_ignore_angle_brackets(false);
			bcx.ang_filter = make_ang_filter();

			std::set<std::tuple<int, int>> splits;
			bool allow_in = ptr->is_of_type(CodeBlock::b_for_each) || ptr->is_of_type(CodeBlock::b_cs_foreach);
			bool allow_colon = is_cpp && !allow_in;
			bool allow_semicolon = !allow_in;
			bool allow_comma = !allow_in;
			int commas = 0;
			int semi_colons = 0;
			int colons = 0;
			bool have_in = false;

			int start_pos = text.find_nospace(ptr->open_pos + 1, ptr->close_pos, code_filter);
			int end_pos = text.rfind_nospace(ptr->close_pos - 1, ptr->open_pos, code_filter);

			if (end_pos <= start_pos)
				return REPORT(false);

			IF_DBG(auto dbg_txt = text.substr_se(start_pos, end_pos));

			for (int i = start_pos; i <= end_pos; i++)
			{
				wchar_t ch = text.at(i);

				if (!code_filter(i, ch))
					continue;

				if (wt_isspace(ch))
					continue;

				bcx.on_char(ch, i, nullptr);

				if (!bcx.is_inside_braces())
				{
					if ((allow_semicolon && ch == ';') || (allow_comma && ch == ','))
					{
						if (ch == ',')
							commas++;
						else
							semi_colons++;

						splits.insert(std::make_tuple(i, 1));
					}
					else if (allow_colon && ch == ':' && !text.is_at_scope_delimiter(i, is_cs, false, code_filter))
					{
						colons++;
						splits.insert(std::make_tuple(i, 1));
					}
					else if (allow_in && ch == 'i')
					{
						int sp, ep;
						auto id = text.get_symbol(i, true, &sp, &ep);
						if (id == L"in")
						{
							have_in = true;
							splits.insert(std::make_tuple(i, ep - sp + 1));
							break;
						}
					}
				}

				if (bcx.is_mismatch())
					return REPORT(false);
			}

			if (!bcx.is_inside_braces() && !splits.empty())
			{
				if ((allow_in && have_in) || (allow_colon && colons == 1))
				{
					_ASSERTE(splits.size() == 1);

					bool is_first = true;

					split_by_splitters(
					    start_pos, end_pos, split_mode::middle_point, splits,
					    [this, sub_items, &is_first](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
						    if (b->contains(caret_pos, false, true))
						    {
							    b->type = CodeBlock::b_undefined;

							    if (is_first)
								    split_cs_cpp_type_and_name(b, false, false, sub_items);
							    else
								    split_cs_cpp_stmt(b, sub_items);

							    sub_items(b);
						    }

						    is_first = false;
						    return true;
					    });
				}
				else if (allow_semicolon && semi_colons == 2)
				{
					if (commas == 0)
					{
						split_by_splitters(start_pos, end_pos, split_mode::caret_extend_L2R, splits,
						                   [this, sub_items](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
							                   if (b->contains(caret_pos, false, true))
							                   {
								                   b->type = CodeBlock::b_undefined;
								                   sub_items(b);
							                   }
							                   return true;
						                   });

						split_by_splitters(start_pos, end_pos, split_mode::delimiter, splits,
						                   [this, sub_items](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
							                   if (b->contains(caret_pos, false, true))
							                   {
								                   b->type = CodeBlock::b_undefined;
								                   split_cs_cpp_stmt(b, sub_items);
							                   }
							                   return true;
						                   });
					}
					else
					{
						std::set<std::tuple<int, int>> semicolons;

						for (auto sp : splits)
							if (text.safe_at(std::get<0>(sp)) == ';')
								semicolons.insert(sp);

						split_by_splitters(start_pos, end_pos, split_mode::caret_extend_L2R, semicolons,
						                   [sub_items](SSParse::CodeBlock::Ptr bptr, std::tuple<int, int> sp) {
							                   bptr->type = CodeBlock::b_undefined;
							                   sub_items(bptr);
							                   return true;
						                   });

						split_by_splitters(
						    start_pos, end_pos, split_mode::delimiter, semicolons,
						    [this, sub_items, &splits](SSParse::CodeBlock::Ptr bptr, std::tuple<int, int> sp) {
							    if (bptr->contains(caret_pos, false, true))
							    {
								    std::set<std::tuple<int, int>> commas_set;

								    for (auto c_sp : splits)
								    {
									    if (bptr->contains(std::get<0>(c_sp), false, false) &&
									        text.safe_at(std::get<0>(c_sp)) == ',')
									    {
										    commas_set.insert(c_sp);
									    }
								    }

								    split_by_splitters(
								        bptr->start_pos(), bptr->end_pos(), split_mode::caret_extend_L2R, commas_set,
								        [sub_items](SSParse::CodeBlock::Ptr b, std::tuple<int, int>) {
									        b->type = CodeBlock::b_undefined;
									        sub_items(b);
									        return true;
								        });

								    split_by_splitters(
								        bptr->start_pos(), bptr->end_pos(), split_mode::delimiter, commas_set,
								        [this, sub_items](SSParse::CodeBlock::Ptr b, std::tuple<int, int>) {
									        if (b->contains(caret_pos, false, true))
									        {
										        b->type = CodeBlock::b_undefined;
										        split_cs_cpp_stmt(b, sub_items);
									        }
									        return true;
								        });
							    }

							    return true;
						    });
					}
				}

				ptr->add_type_bit(CodeBlock::b_dont_split_scope);
				return REPORT(true);
			}
		}

		return REPORT(false);
	}

	void fix_outline_block_range(int start_pos, int& end_pos)
	{
		using namespace SSParse;

		auto code_filter = make_code_filter();

		BraceCounter bcx("(){}");
		bcx.set_ignore_angle_brackets(true);

		int last_valid = -1;

		for (int i = start_pos; i <= end_pos; i++)
		{
			if (!text.is_valid_pos(i))
			{
				if (last_valid != -1)
					end_pos = last_valid;

				return;
			}

			wchar_t ch = text.at(i);

			if (!code_filter(i, ch))
				continue;

			if (wt_isspace(ch))
				continue;

			last_valid = i;

			switch (ch)
			{
			case '(':
			case ')':
			case '[':
			case ']':
			case '{':
			case '}':
			case '<':
			case '>':
				bcx.on_char(ch, i, nullptr);
				break;

			default:
				continue;
			}

			if (bcx.is_mismatch())
			{
				if (last_valid != -1)
					end_pos = last_valid;

				return;
			}
		}
	}

	void fix_block_open_close(SSParse::CodeBlock::Ptr& ptr)
	{
		using namespace SSParse;

		if (ptr->open_pos == -1)
		{
			ptr->ensure_name_and_type(ch_states, is_cs);
			auto code_filter = make_code_filter();

			if (ptr->is_of_type(CodeBlock::b_if) || ptr->is_of_type(CodeBlock::b_else) ||
			    ptr->is_of_type(CodeBlock::b_else_if) || ptr->is_of_type(CodeBlock::b_try) ||
			    ptr->is_of_type(CodeBlock::b_catch) || ptr->is_of_type(CodeBlock::b_finally) ||
			    ptr->is_of_type(CodeBlock::b_vc_try) || ptr->is_of_type(CodeBlock::b_vc_except) ||
			    ptr->is_of_type(CodeBlock::b_vc_finally) || ptr->is_of_type(CodeBlock::b_for) ||
			    ptr->is_of_type(CodeBlock::b_switch) || ptr->is_of_type(CodeBlock::b_do) ||
			    ptr->is_of_type(CodeBlock::b_while) || ptr->is_of_type(CodeBlock::b_for_each) ||
			    ptr->is_of_type(CodeBlock::b_cs_foreach) || ptr->is_of_type(CodeBlock::b_cs_using) ||
			    ptr->is_of_type(CodeBlock::b_cs_lock) || ptr->is_of_type(CodeBlock::b_cs_unsafe))
			{
				wchar_t op_char = ptr->is_of_type(CodeBlock::b_do) ? L'{' : L'(';

				int open_pos = text.find(ptr->start_pos(), ptr->end_pos(), op_char, code_filter);
				if (open_pos != -1)
				{
					int close_pos = get_matching_brace(open_pos, false, code_filter);
					if (close_pos > open_pos)
					{
						if (ptr->close_pos > close_pos)
						{
							int sc_pos = text.rfind_nospace(ptr->close_pos, close_pos, code_filter);
							if (sc_pos != -1 && text.safe_at(sc_pos) == ';')
								ptr->semicolon_pos = sc_pos;
						}

						ptr->open_pos = open_pos;
						ptr->close_pos = close_pos;
						ptr->open_char = op_char;
					}
				}
			}
			else if (text.is_one_of(ptr->start_pos(), L"({[") &&
			         text.find_matching_brace(ptr->start_pos(), code_filter, -1, ptr->end_pos()) == ptr->end_pos())
			{
				ptr->open_pos = ptr->start_pos();
				ptr->close_pos = ptr->end_pos();

				if (ptr->top_pos == ptr->open_pos)
					ptr->top_pos = -1;

				if (ptr->semicolon_pos == ptr->close_pos)
					ptr->semicolon_pos = -1;

				ptr->open_char = text.safe_at(ptr->open_pos);
			}
		}
	}

	void split_cs_cpp_stmt(SSParse::CodeBlock::Ptr& ptr, std::function<void(SSParse::CodeBlock::Ptr)> sub_items)
	{
#ifdef _DEBUG
		auto _auto_fname = SSParse::CodeBlock::PushFunctionName(__FUNCTION__);
#endif

		using namespace SSParse;

		if (ptr->has_type_bit(CodeBlock::b_dont_split))
			return;

		if (ptr->is_directive() || ptr->is_modifier())
			return;

		struct filtered_inserter_factory
		{
			std::function<void(SSParse::CodeBlock::Ptr)> pass_to;

			explicit filtered_inserter_factory(std::function<void(SSParse::CodeBlock::Ptr)> func)
			    : pass_to(std::move(func))
			{
			}

			std::function<void(SSParse::CodeBlock::Ptr)> create(SSParse::CodeBlock::Ptr orig)
			{
				return [this, orig](SSParse::CodeBlock::Ptr b) {
					if (b->has_open_close() && orig->has_open_close())
					{
						if (b->open_pos == b->start_pos() && b->open_pos == orig->open_pos &&
						    b->close_pos == orig->close_pos)
						{
							return;
						}
					}
					else if (orig->start_pos() == b->start_pos() && orig->end_pos() == b->end_pos())
					{
						if (b->has_open_close())
						{
							int osp = orig->start_pos();
							int oep = orig->end_pos();

							orig->top_pos = -1;
							orig->semicolon_pos = -1;
							orig->open_pos = b->open_pos;
							orig->close_pos = b->close_pos;
							orig->open_char = b->open_char;

							if (orig->start_pos() != osp)
								orig->top_pos = osp;

							if (orig->end_pos() != oep)
								orig->semicolon_pos = oep;
						}

						return;
					}

					pass_to(b);
				};
			}
		} inserter_factory(sub_items);

		auto ptr_sub_items = inserter_factory.create(ptr);

		fix_block_open_close(ptr);
		ptr->ensure_name_and_type(ch_states, is_cs);

		if (ptr->is_of_type(CodeBlock::b_if) || ptr->is_of_type(CodeBlock::b_else) ||
		    ptr->is_of_type(CodeBlock::b_else_if) || ptr->is_of_type(CodeBlock::b_try) ||
		    ptr->is_of_type(CodeBlock::b_catch) || ptr->is_of_type(CodeBlock::b_finally) ||
		    ptr->is_of_type(CodeBlock::b_vc_try) || ptr->is_of_type(CodeBlock::b_vc_except) ||
		    ptr->is_of_type(CodeBlock::b_vc_finally) || ptr->is_of_type(CodeBlock::b_for) ||
		    ptr->is_of_type(CodeBlock::b_switch) || ptr->is_of_type(CodeBlock::b_do) ||
		    ptr->is_of_type(CodeBlock::b_while) || ptr->is_of_type(CodeBlock::b_for_each) ||
		    ptr->is_of_type(CodeBlock::b_cs_foreach) || ptr->is_of_type(CodeBlock::b_cs_using) ||
		    ptr->is_of_type(CodeBlock::b_cs_lock) || ptr->is_of_type(CodeBlock::b_cs_unsafe))
		{
			split_cs_cpp_native_unscoped_stmt(ptr, true, ptr_sub_items);
			return;
		}

		auto code_filter = make_code_filter();

		// try first to split as a simple round scope
		if (ptr->has_open_close() && ptr->open_char == '(' && ptr->start_pos() == ptr->open_pos)
		{
			auto prev = text.rfind_nospace(ptr->start_pos(), code_filter);

			if (!text.is_identifier_boundary(prev) && split_cs_cpp_round_scope(ptr, ptr_sub_items))
				return;
		}

		// try as ternary (not only for scopes)
		if (split_cs_cpp_ternary_expr(ptr, ptr_sub_items))
			return;

		// try to split as multi assignment [case: 98589]
		if (!ptr->is_scope() && text.safe_at(ptr->end_pos()) == ';' && is_type(ptr->start_pos()) &&
		    split_cs_cpp_comma_delimited_stmt(ptr, false, sub_items))
			return;

		auto rep = get_split_report(L"stmt", ptr->start_pos(), ptr->end_pos());
		if (rep.is_empty())
			split_report(*this, L"stmt", ptr->start_pos(), ptr->end_pos(), true);
		else
			return;

		std::vector<CodeBlock::Ptr> blocks;

		// Don't split complex statements from outline
		if (ptr->has_type_bit(CodeBlock::b_va_outline))
		{
			int s_pos = ptr->start_pos();
			if (ch_states.at(s_pos).Contains(chs_directive))
				return;
		}

		// helps to determine if ch is one of chars
		auto is_one_of = [](wchar_t ch, const wchar_t* chars) -> bool {
			for (const wchar_t* cp = chars; cp && *cp; cp++)
				if (ch == *cp)
					return true;

			return false;
		};

		// **************************************************

		if (ptr)
		{
			int start_pos = ptr->start_pos();
			int end_pos = ptr->end_pos();

			if (start_pos == -1)
			{
				_ASSERTE(!"Invalid block passed!");
				return; // invalid
			}

			bool dont_split = true;

			// don't split blocks containing directives
			// don't split very simple statements
			for (int x = start_pos; x <= end_pos; x++)
			{
				if (ch_states[x].Contains(SSParse::chs_directive))
				{
					ptr->add_type_bit(CodeBlock::b_dont_split);
					return;
				}

				if (dont_split)
				{
					wchar_t tmp_ch = text.at(x);
					if (tmp_ch > ' ' && !ISCSYM(tmp_ch))
						dont_split = false;
				}
			}

			if (dont_split)
			{
				ptr->add_type_bit(CodeBlock::b_dont_split);
				return;
			}

			// ***********************************************************************
			// Start of process

			wchar_t ch = 0;
			bool watch_sym = true;

			BraceCounter bc;
			bc.set_ignore_angle_brackets(false);
			bc.ang_filter = make_ang_filter();

			auto push = [&](int s_pos, int o_pos, int c_pos, int e_pos, wchar_t o_ch, bool dont_split,
			                bool not_for_queue) {
				if (s_pos == -1)
					return;

				_ASSERTE(o_pos != -1 || get_matching_brace(o_pos, false, code_filter) == c_pos);
				_ASSERTE(o_pos != -1 || !is_bracing_mismatch(o_pos + 1, c_pos - 1));
				_ASSERTE(!is_bracing_mismatch(s_pos, max(c_pos, e_pos)));

				if (is_cpp)
				{
					// include global namespace scope delimiter, but only in C++,
					// where it can be used without "global" keyword as in C#

					int scope_sep = text.rfind_scope_delimiter_start(s_pos - 1, is_cs, true, code_filter);
					if (scope_sep != -1 && is_global_scope_delimiter_start(scope_sep))
						s_pos = scope_sep;
				}

				// include left ++ or -- operator
				int left_op = text.rfind_nospace(s_pos - 1, code_filter);
				if (text.is_one_of(left_op, L"+-"))
				{
					int sp, ep;
					auto op = get_operator(left_op, &sp, &ep);
					if (op && sp + 1 == ep)
					{
						if (wcscmp(op, L"++") == 0 || wcscmp(op, L"--") == 0)
							s_pos = sp;
					}
				}

				// include left ! operator(s) max 20 in row
				left_op = text.rfind_nospace(s_pos - 1, code_filter);
				for (int x = 0; x < 20 && text.safe_at(left_op) == '!'; x++)
				{
					int sp, ep;
					auto op = get_operator(left_op, &sp, &ep);
					if (op != nullptr && sp == ep && *op == '!')
					{
						s_pos = sp;
						left_op = text.rfind_nospace(s_pos - 1, code_filter);
						continue;
					}
					break;
				}

				// include right ++ or -- operator
				int right_op = text.find_nospace(e_pos + 1, code_filter);
				if (text.is_one_of(right_op, L"+-"))
				{
					int sp, ep;
					auto op = get_operator(right_op, &sp, &ep);
					if (op != nullptr && sp + 1 == ep)
					{
						if (wcscmp(op, L"++") == 0 || wcscmp(op, L"--") == 0)
							e_pos = ep;
					}
				}

				// include right ellipses ... (just for sure if type/name split has failed)
				else if (is_cpp && text.safe_at(right_op) == '.')
				{
					int sp, ep;
					auto op = get_operator(right_op, &sp, &ep);
					if (op != nullptr && sp + 2 == ep)
					{
						if (wcscmp(op, L"...") == 0)
							e_pos = ep;
					}
				}

				// include semicolon if appropriate
				int sc_pos = text.find_nospace(e_pos + 1, code_filter);
				if (text.safe_at(sc_pos) == ';')
				{
					if (c_pos == -1)
					{
						c_pos = e_pos;
						e_pos = sc_pos;
					}
					else
					{
						e_pos = sc_pos;
					}
				}

				DWORD b_type = CodeBlock::b_not_block | (dont_split ? CodeBlock::b_dont_split : 0);
				std::wstring name;

				if (o_ch == '0')
				{
					b_type |= CodeBlock::b_numeric_literal;
					name = L"Numeric Literal";
				}
				else if (o_ch == '"')
				{
					b_type |= CodeBlock::b_string_literal;
					name = L"String Literal";
				}
				else if (o_ch == '*')
				{
					b_type |= CodeBlock::b_comment;
					name = L"Comment";
				}
				else if (o_ch == '.')
				{
					b_type |= CodeBlock::b_expression;
				}

				CodeBlock::Ptr b(
				    new CodeBlock((CodeBlock::block_type)b_type, ptr->src_code, o_ch, s_pos, o_pos, c_pos, e_pos));

				if (!name.empty())
					b->name = name;

				// handle max. 10 levels of dereferencing
				include_dereference(b, true, 10);

				if (!ActiveSettings().block_include_header)
				{
					b->ensure_name_and_type(ch_states, is_cs);
					if (!b->is_parent_type())
					{
						if (not_for_queue)
							ptr_sub_items(b);
						else
							blocks.push_back(b);
					}
				}
				else
				{
					if (not_for_queue)
						ptr_sub_items(b);
					else
						blocks.push_back(b);
				}
			};

			// On bracing block, insert whole block,
			// so that we have properly defined
			// right to left blocks.
			bc.on_block = [&](char ch_at, int o_pos, int c_pos) {
				push(o_pos, o_pos, c_pos, -1, (wchar_t)ch_at, true, false);
			};

			// resolve spacial case: "return ..."
			if (ptr->is_of_type(SSParse::CodeBlock::b_return))
			{
				int symLen = text.find_not_csym(start_pos, code_filter) - start_pos;

				push(start_pos, -1, -1, start_pos + symLen - 1, '.', true, false);
				int top = text.find_nospace(start_pos + symLen, code_filter);
				if (top != -1)
				{
					if (granular_start())
					{
						CodeBlock::Ptr b(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, '.', top, end_pos));

						if (b->contains(caret_pos, false, true))
						{
							// Don't push the right side to the resolve queue,
							// we don't want as big part in it.
							// Push it directly as a resolved block;
							ptr_sub_items(b);
						}
					}

					// specify new starting position for the splitting
					start_pos = top;
				}
			}

			{
				CodeBlock::Ptr tmp = ptr;

				if (start_pos != ptr->start_pos())
				{
					tmp.reset(new CodeBlock(CodeBlock::b_undefined, ptr->src_code, '.', start_pos, end_pos));
				}

				auto tmp_pass = inserter_factory.create(tmp);

				if (split_cs_cpp_ternary_expr(tmp, tmp_pass))
				{
					ptr_sub_items(tmp);
					return;
				}

				if (split_cs_cpp_type_and_name(tmp, true, false, tmp_pass))
				{
					ptr_sub_items(tmp);
					return;
				}
			}

			// **********************************************************
			// start of iteration

			std::set<std::tuple<int, int>> eq_splitters;

			for (int i = start_pos; i <= end_pos; i++)
			{
				_ASSERTE(text.is_valid_pos(i));

				ch = text.at(i);

				if (!code_filter(i, ch))
					continue;

				if (wt_isspace(ch))
					continue;

				bc.on_char(ch, i, nullptr);

				if (!bc.is_stack_empty())
				{
					watch_sym = false;

					// we need this very fast,
					// so jump over known blocks
					if (is_one_of(ch, L"([{"))
					{
						int next = get_matching_brace(i, false, code_filter, start_pos, end_pos);
						if (next != -1)
						{
							i = next - 1;
							continue;
						}
					}
				}
				else
				{
					if (is_comment_or_string(i))
					{
						int sp, ep;
						CharState state = (CharState)(ch_states[i] & (chs_string_mask | chs_comment_mask));
						if (state && get_state_range(i, sp, ep, state, true))
						{
							_ASSERTE(text.is_valid_pos(sp) && text.is_valid_pos(ep));

							if (sp != -1 && ep != -1)
							{
								wchar_t ch1 = ch_states[i].HasAnyBitOfMask(chs_string_mask) ? L'"' : L'*';

								// handle C++11 user defined literals
								if (ch1 == '"' && text.is_identifier_start(ep + 1))
								{
									int sym_end;
									if (!text.get_symbol(ep + 1, true, nullptr, &sym_end).empty())
										ep = sym_end;
								}

								push(sp, -1, -1, ep, ch1, true, false);

								i = ep;
							}
						}
						continue;
					}

					if (ch == ',' && ptr->open_char_is_one_of("<({[") && ptr->scope_contains(i, false))
					{
						// STOP, this is parameter list!
						return;
					}

					if (ch == '=')
					{
						// add equal operators for later use

						int op_sp, op_ep;
						auto op = get_operator(i, &op_sp, &op_ep);
						if (op != nullptr)
						{
							int op_l = op_ep - op_sp + 1;
							if (Parser::IsAssignmentOperator(op, is_cs))
								eq_splitters.insert(std::make_tuple(op_sp, op_l));

							watch_sym = true;
							i = op_ep;
							continue;
						}
					}

					if (bc.is_closed())
					{
						if (!watch_sym)
							watch_sym = true;
					}

					if (watch_sym && (wt_isdigit(ch) || ch == '-' || ch == '+' || ch == '.'))
					{
						int end = get_end_of_numeric_literal(i, code_filter);
						if (end != -1)
						{
							push(i, -1, -1, end, '0', true, false);
							i = end;
							continue;
						}
					}

					if (watch_sym && ISCSYM(ch))
					{
						int o_pos = -1;
						int pre_op = -1;
						int post_op = -1;
						bool mismatch = false;

						for (int c = i + 1; c <= end_pos; c++)
						{
							wchar_t ch1 = text.at(c);

							if (!code_filter(c, ch1))
								continue;

							if (is_one_of(ch1, L"{(["))
							{
								o_pos = c;
								break;
							}
							else if (ch1 == '<' && is_template(c, ch1))
							{
								o_pos = c;
								break;
							}
							else if (is_one_of(ch1, L"})]"))
							{
								mismatch = true;
								break;
							}
							else if (ch1 == '>' && is_template(c, ch1))
							{
								mismatch = true;
								break;
							}
							else if (ch1 == ',' && ptr->open_char_is_one_of("<({[") && ptr->scope_contains(c, false))
							{
								// STOP, this is parameter list!
								return;
							}

							if (!ISCSYM(ch1) && !wt_isspace(ch1))
							{
								int op_spos, op_epos;
								auto op = get_operator(c, &op_spos, &op_epos);
								if (op && op_spos == c)
								{
									int op_len = op_epos - op_spos + 1;

									if (op_len == 1 && (*op == '*' || *op == '&' || *op == '^' || *op == '%') &&
									    is_deref_operator(op_spos))
									{
										// ignore deref operators
										continue;
									}
									else if (op_len == 3 && *op == '.')
									{
										// ignore ellipses ...
										continue;
									}
									else if (op_len == 1 && *op == '!')
									{
										// ignore ! operator
										continue;
									}
									else if (is_cpp && op_len == 2 && (op[0] == '-' && op[1] == '>') &&
									         is_return_type_operator(op_spos))
									{
										// ignore return type "...) -> type" like in lambda
										c = op_epos;
										continue;
									}
									else if (op_len == 2 && (wcscmp(op, L"++") == 0 || wcscmp(op, L"--") == 0 ||
									                         wcscmp(op, L"::") == 0))
									{
										// ignore some other operators
										c = op_epos;
										continue;
									}
									else
									{
										// add equal operators for later use
										if (Parser::IsAssignmentOperator(op, is_cs))
											eq_splitters.insert(std::make_tuple(op_spos, op_len));

										pre_op = text.rfind(
										    c - 1, i,
										    [is_one_of](wchar_t ch2) {
											    return ISCSYM(ch2) || is_one_of(ch2, L"(){}[]\"'@;,");
										    },
										    code_filter);

										post_op = text.find(
										    op_epos + 1, end_pos,
										    [is_one_of](wchar_t ch3) {
											    return ISCSYM(ch3) || is_one_of(ch3, L"(){}[]\"'@;,");
										    },
										    code_filter);

										break;
									}
								}
							}
						}

						if (mismatch)
						{
							watch_sym = true;
							continue;
						}

						// push part before operator and continue to next 'i'

						if (pre_op != -1 && post_op != -1)
						{
							push(i, -1, -1, pre_op, '.', true, false);
							i = post_op - 1;
							continue;
						}

						// push next parts

						int e_pos = -1;

						if (o_pos >= start_pos)
							e_pos = get_matching_brace(o_pos, false, code_filter, o_pos, end_pos);
						else
							e_pos = text.find_not_csym(i + 1, end_pos, code_filter) - 1;

						if (e_pos < 0)
							e_pos = end_pos;

						wchar_t o_ch = o_pos >= start_pos ? text.at(o_pos) : L'\0';
						wchar_t e_ch = text.safe_at(e_pos);

						if (o_pos >= start_pos)
						{
							if (o_ch == '{' && is_cpp)
							{
								bool pushed = false;

								// don't push blocks like: "type { lambda body }"
								int round_close_pos = text.rfind(i, start_pos, L')', code_filter);
								if (round_close_pos != -1)
								{
									int pre_i = text.find_nospace(round_close_pos + 1, end_pos, code_filter);
									if (pre_i != -1 && is_return_type_operator(pre_i))
									{
										push(o_pos, o_pos, e_pos, -1, o_ch, true, false);
										pushed = true;
									}
								}

								if (!pushed)
									push(i, o_pos, e_pos, -1, o_ch, true, false);
							}
							else
							{
								push(i, o_pos, e_pos, -1, o_ch, true, false);
							}
						}
						else
						{
							push(i, -1, -1, e_pos, '.', true, false);
						}

						if (e_pos != -1)
							i = e_pos;

						if (o_pos == -1)
							watch_sym = get_operator(e_pos + 1) != nullptr;
						else
							watch_sym = is_one_of(e_ch, L")]}>");
					}
				}
			} // for

			// #SmartSelect_StmtBlocks
			if (!blocks.empty())
			{
				// finally, if all is OK,
				// combine all tokens into meaningful blocks
				// and pass them to inserter

				// call group such as: std::floor(abc)->abc()->efg
				// simply row of mini statements delimited by :: -> or .
				struct group
				{
					int start;
					int end;
					int caret;

					int brace_start;
					int brace_end;
					bool brace_closed;
				};

				std::vector<group> groups;

				group g;
				g.start = -1;
				g.end = -1;
				g.caret = -1;
				g.brace_start = -1;
				g.brace_end = -1;
				g.brace_closed = false;
				group caret_g = g;

				for (size_t i = 0; i < blocks.size(); i++)
				{
					auto& b = blocks[i];

					bool contains_caret = b->contains(caret_pos, false, true);

					if (!contains_caret && i + 1 < blocks.size() && caret_pos > b->end_pos() &&
					    caret_pos < blocks[i + 1]->start_pos())
					{
						contains_caret = true;
					}

					// first ends by closing brace and next starts by opening
					// AND
					// nothing significant (mainly operator) is between those braces
					bool is_brace_row_block =
					    b->ends_by_close() && i + 1 < blocks.size() && blocks[i + 1]->starts_by_open() &&
					    (text.find_nospace(b->end_pos() + 1, code_filter) == blocks[i + 1]->start_pos());

					// if subpart ends by: -> :: or .
					if (is_cs_cpp_partial_stmt(b) || is_brace_row_block)
					{
						if (g.start == -1)
							g.start = (int)i;

						g.end = (int)i;

						if (contains_caret)
						{
							g.caret = (int)i;
						}

						if (!g.brace_closed)
						{
							if (is_brace_row_block)
							{
								if (g.brace_start == -1)
									g.brace_start = (int)i;

								g.brace_end = (int)i;
							}
							else
							{
								if (g.brace_start != -1)
									g.brace_end = (int)i;

								if (g.brace_start != -1 && g.brace_end != -1 && g.caret != -1 &&
								    g.caret >= g.brace_start && g.caret <= g.brace_end)
								{
									g.brace_closed = true;
								}
								else
								{
									g.brace_start = -1;
									g.brace_end = -1;
								}
							}
						}
					}
					else if (g.end != -1 && i - g.end == 1) // or is last in row?
					{
						// first starts by opening brace and previous ends by closing
						// AND
						// nothing significant (mainly operator) is between those braces
						bool is_brace_row_end =
						    b->starts_by_open() && i > 0 && blocks[i - 1]->ends_by_close() &&
						    (text.rfind_nospace(b->start_pos() - 1, code_filter) == blocks[i - 1]->end_pos());

						if (is_brace_row_end)
						{
							g.brace_end = (int)i;

							if (g.brace_start != -1 && g.brace_end != -1 && g.caret != -1 && g.caret >= g.brace_start &&
							    g.caret <= g.brace_end)
							{
								g.brace_closed = true;
							}
						}

						g.end = (int)i;
						if (contains_caret)
							g.caret = (int)i;

						if (g.caret != -1)
							caret_g = g;

						if ((g.brace_start == g.start && g.brace_end == g.end) || (g.brace_start == g.brace_end))
						{
							g.brace_start = -1;
							g.brace_end = -1;
							g.brace_closed = false;
						}

						groups.push_back(g);

						g.start = -1;
						g.end = -1;
						g.caret = -1;
						g.brace_start = -1;
						g.brace_end = -1;
						g.brace_closed = false;
					}
				}

				// helper to build up tokens
				auto insert_blocks = [&](size_t first_idx, size_t last_idx, const std::vector<group>* local_groups) {
					int L2R_start = -1;
					std::set<size_t> used_groups;

					for (size_t i = first_idx; i <= last_idx; i++)
					{
						SSParse::CodeBlock::Ptr b = blocks[i];

						if (local_groups != nullptr)
						{
							// merge all in single group into one big block
							// this block is then selected at once when user
							// is extending from the outside of this group

							// for example in "abc + foo()->efg + 125" there are
							// 4 blocks: "abc", "foo()", "efg" and "125"
							// These form 3 final blocks: "abc", "foo()->efg" and "125".

							// But if caret is inside of "foo()->efg", it is later split as well.
							// See second call to this "insert_blocks" lambda.
							for (size_t gx = 0; gx < local_groups->size(); gx++)
							{
								if (used_groups.find(gx) != used_groups.end())
									continue;

								auto& grp = local_groups->at(gx);

								if ((int)i >= grp.start && (int)i <= grp.end)
								{
									int num_scopes = 0;
									int last_open_pos = -1, last_close_pos = -1;
									wchar_t last_open_ch = '\0';

									for (int gi = grp.start; gi <= grp.end; gi++)
									{
										const auto& gib = blocks[(uint)gi];
										if (gib->has_open_close())
										{
											num_scopes++;
											last_open_pos = gib->open_pos;
											last_close_pos = gib->close_pos;
											last_open_ch = gib->open_char;
										}
									}

									if (num_scopes == 1 && last_open_ch != '\0' && last_open_pos != -1 &&
									    last_close_pos != -1)
									{
										b.reset(new CodeBlock(
										    (CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block), ptr->src_code,
										    last_open_ch, blocks[(uint)grp.start]->start_pos(), last_open_pos,
										    last_close_pos, blocks[(uint)grp.end]->end_pos()));
									}
									else
									{
										b.reset(
										    new CodeBlock((CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block),
										                  ptr->src_code, L'.', blocks[(uint)grp.start]->start_pos(),
										                  blocks[(uint)grp.end]->end_pos()));
									}

									i = (uint)grp.end;
									used_groups.insert(gx);
									break;
								}
							}
						}

						// calculate left to right start position
						if (L2R_start < b->start_pos() && b->virtual_start_pos() <= caret_pos)
						{
							L2R_start = b->start_pos();
						}

						if (b->contains(caret_pos, false, true))
						{
							if (granular_start() || b->has_open_close())
								ptr_sub_items(b);

							// if block is brace block, apply specialized splitters
							if (b->has_open_close())
							{
								auto b_sub_items = inserter_factory.create(b);

								// try handle any round braces: (...)
								if (!split_cs_cpp_round_scope(b, b_sub_items))
								{
									// else try handle rest: <...>, [...], {...}
									if (!split_cs_cpp_comma_delimited_scope(b, b_sub_items))
									{
										if (!split_cs_cpp_single_param_scope(b, b_sub_items))
										{
											split_open_close_prefix(b, b_sub_items);
										}
									}
								}
							}
						}

						// from caret to current -> Left To Right
						// These blocks are selected first in Left to Right order.
						if (granular_start() && L2R_start != -1 && L2R_start <= b->start_pos())
						{
							SSParse::CodeBlock::Ptr leftToRight(
							    new CodeBlock((CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block),
							                  ptr->src_code, L'.', L2R_start, b->end_pos()));

							if (leftToRight->contains(caret_pos, false, true))
							{
								ptr_sub_items(leftToRight);
							}
						}

						if (granular_start())
						{
							// from current to end <- Right To left
							// These blocks are selected when there nothing to extend in Left to Right order
							// and are adding room on the left side of current block which ends on the end
							// of current logical subpart of the statement.
							SSParse::CodeBlock::Ptr rightToLeft(
							    new CodeBlock((CodeBlock::block_type)(ptr->type | CodeBlock::b_not_block),
							                  ptr->src_code, L'.', b->start_pos(), blocks[last_idx]->end_pos()));

							if (rightToLeft->contains(caret_pos, false, true))
							{
								ptr_sub_items(rightToLeft);
							}
						}
					}
				};

				if (granular_start() && caret_g.start != -1 && caret_g.end != -1 && caret_g.caret != -1)
				{
					std::vector<group> br_groups;

					if (caret_g.brace_start != -1 && caret_g.brace_start < caret_g.brace_end &&
					    caret_g.caret >= caret_g.brace_start && caret_g.caret <= caret_g.brace_end)
					{
						g.start = caret_g.brace_start;
						g.end = caret_g.brace_end;
						g.caret = caret_g.caret;
						g.brace_start = -1;
						g.brace_end = -1;
						g.brace_closed = false;

						br_groups.push_back(g);

						insert_blocks((uint)caret_g.brace_start, (uint)caret_g.brace_end, nullptr);
					}

					// only group that contains caret is split into parts
					insert_blocks((uint)caret_g.start, (uint)caret_g.end, &br_groups);
				}

				if (!granular_start() || eq_splitters.empty())
				{
					// all blocks where groups are merged into big blocks,
					insert_blocks(0, blocks.size() - 1, granular_start() ? &groups : nullptr);
				}
				else
				{
					bool is_left_part = true;
					split_by_splitters(start_pos, end_pos, split_mode::delimiter, eq_splitters,
					                   [&](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
						                   // b is a block between some '=' and ends of parent block
						                   // or between two '=' operators - it does not matter

						                   if (b->contains(caret_pos, false, true))
						                   {
							                   bool resolved = false;

							                   auto b_sub_items = inserter_factory.create(b);

							                   if (is_left_part)
								                   resolved = split_cs_cpp_type_and_name(b, false, false, b_sub_items);

							                   if (!resolved)
								                   resolved = split_cs_cpp_ternary_expr(b, b_sub_items);

							                   if (!resolved)
							                   {
								                   // find blocks within high priority operators with '='
								                   // and add subparts only for current part of expression

								                   int min_id = -1, max_id = -1;

								                   for (int q = 0; q < static_cast<int>(blocks.size()); q++)
								                   {
									                   const auto& qb = blocks[(uint)q];
									                   if (b->contains(*qb))
									                   {
										                   if (min_id == -1)
											                   min_id = q;

										                   max_id = q;
									                   }
									                   else if (min_id != -1 && max_id != -1)
									                   {
										                   break;
									                   }
								                   }

								                   // if min_id and max_id equal it is OK,
								                   // Note: insert_blocks also splits parts,
								                   // so this call it very needed!

								                   if (min_id != -1 && max_id != -1)
								                   {
									                   // so we have a range of this sub-block, insert it
									                   insert_blocks((uint)min_id, (uint)max_id,
									                                 granular_start() ? &groups : nullptr);
								                   }
							                   }

							                   b->add_type_bit(CodeBlock::b_dont_split);
							                   ptr_sub_items(b);
						                   }

						                   is_left_part = false;
						                   return true;
					                   });
				}
			}
		}
	}

	// Checks if ptr is subpart like "int abc = args[25]" in "int abc = args[25]->xyz().abc"
	// Simply returns true, if block is followed by "->", "::" or "."
	bool is_cs_cpp_partial_stmt(SSParse::CodeBlock::Ptr& ptr, bool check_start = false)
	{
		auto code_filter = make_code_filter();

		int pos = text.find_call_delimiter_start(ptr->end_pos() + 1, is_cs, true, code_filter);
		if (pos != -1)
		{
			if (is_cpp)
				return !is_global_scope_delimiter_start(pos);

			return true;
		}

		if (check_start)
		{
			pos = text.find_call_delimiter_start(ptr->start_pos(), is_cs, true, code_filter);
			if (pos != -1)
			{
				if (is_cpp)
					return !is_global_scope_delimiter_start(pos);

				return true;
			}

			pos = text.rfind_call_delimiter_start(ptr->start_pos() - 1, is_cs, true, code_filter);
			if (pos != -1)
			{
				if (is_cpp)
					return !is_global_scope_delimiter_start(pos);

				return true;
			}
		}

		return false;
	}

	static bool is_logging_peek()
	{
		return gTestsActive && gTestLogger && gTestLogger->IsSmartSelectLoggingPeek();
	}

	static bool is_logging_block()
	{
		return gTestsActive && gTestLogger && gTestLogger->IsSmartSelectLoggingBlock();
	}

	COLORREF get_peek_color(const CStringW& name) const
	{
		if (is_logging_peek())
		{
			// IDE/theme independent colors
			// other colors are handled by renderers
			// so are indexed so always same in FTM

			if (name == L"Normal BG")
				return RGB(255, 255, 255);
			else if (name == L"Selected BG")
				return RGB(173, 213, 255);
			else if (name == L"Selected Text") // not used
				return RGB(0, 0, 0);
			else if (name == L"Plain Text")
				return RGB(0, 0, 0);
			else if (name == L"Comment")
				return RGB(0, 128, 0);
			else if (name == L"String")
				return RGB(163, 21, 21);
		}
		else
		{
			if (name == L"Normal BG")
				return g_IdeSettings->GetDteEditorColor(L"Plain Text", FALSE);
			else if (name == L"Selected BG")
			{
				return gShellAttr->IsDevenv10OrHigher()
				           ? ThemeUtils::InterpolateColor(g_IdeSettings->GetDteEditorColor(L"Plain Text", FALSE),
				                                          g_IdeSettings->GetDteEditorColor(L"Selected Text", FALSE),
				                                          0.4f)
				           : g_IdeSettings->GetDteEditorColor(L"Selected Text", FALSE);
			}
			else if (name == L"Selected Text")
			{
				if (gShellAttr->IsDevenv10OrHigher())
					return g_IdeSettings->GetDteEditorColor(L"Plain Text", TRUE);
				else
					return g_IdeSettings->GetDteEditorColor(L"Selected Text", TRUE);
			}
			else if (name == L"Comment")
				return g_IdeSettings->GetDteEditorColor(L"Comment", TRUE);
			else if (name == L"String")
				return g_IdeSettings->GetDteEditorColor(L"String", TRUE);
			else if (name == L"Plain Text")
				return g_IdeSettings->GetDteEditorColor(L"Plain Text", TRUE);
		}

		_ASSERTE(!"Unknown color!");
		return 0;
	}

	void get_tooltip_ftm(ArgToolTipEx& tt, int s_pos, int e_pos, FtmBuilder<WCHAR>& lb) const
	{
		EdCntPtr ed(g_currentEdCnt);

		if (!ed)
			return;

		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*ed);

		const bool enhColor = Psettings->m_bEnhColorSourceWindows || gTestsActive;

		const DWORD txt_bg = get_peek_color(L"Normal BG") | 0xFF000000;
		const DWORD sel_bg = get_peek_color(L"Selected BG") | 0xFF000000;
		const DWORD sel_fg = get_peek_color(L"Selected Text") | 0xFF000000;
		const DWORD cmt_fg = get_peek_color(enhColor ? L"Comment" : L"Plain Text") | 0xFF000000;
		const DWORD str_fg = get_peek_color(enhColor ? L"String" : L"Plain Text") | 0xFF000000;

		const bool isActivePeekLogging = is_logging_peek();

		const bool HighlightSyntaxInSelection = (gShellAttr->IsDevenv10OrHigher() && enhColor) || isActivePeekLogging;

		int maxPixW = 0;

		{
			CRect cr;
			ed->vGetClientRect(&cr);

			CPoint pt = ed->GetCharPos(TERRCTOLONG(ed->GetFirstVisibleLine(), 1));
			cr.left = pt.x;

			maxPixW = cr.Width() - VsUI::DpiHelper::LogicalToDeviceUnitsX(tt.Margin().left);
		}

		CDC dc;
		dc.CreateCompatibleDC(nullptr);
		dc.SelectObject(tt.FontOverride());

		auto getPixWidth = [&](const std::wstring& w_text, int index) -> int {
			CSize size;

			WCHAR c[2] = {(index >= 0 && index <= (int)w_text.length()) ? w_text[(uint)index] : L'\0',
			              (index + 1 >= 0 && index + 1 <= (int)w_text.length()) ? w_text[uint(index + 1)] : L'\0'};

			if (!c[0])
				return 0;

			if (wt_isspace(c[0]))
				c[0] = L' ';

			if (!c[1] || wt_isspace(c[1]))
				c[1] = L' ';

			// get width of second char (w/o char spacing)
			::GetTextExtentPoint32W(dc, &c[1], 1, &size);
			long s1 = size.cx;

			// get width of both chars (w/ char spacing)
			::GetTextExtentPoint32W(dc, c, 2, &size);

			return int(size.cx - s1); // width of char + spacing
		};

		auto add_line = [&](SSParse::TextLine& line, bool is_last) {
			TabsExpander tabs;

			lb.Normal().Renderer(1).Value(line.line_number());

			int line_start = (int)line.start_pos();
			int line_end = (int)line.end_pos() - 1;

			// append line delimiter
			if (line.delim_length() > 0)
				line_end += 1;

			// char positions after tabs expansion
			std::vector<size_t> exp_pos;

			// line text after tabs expansion
			std::wstring exp_text = tabs.exp(text.substr_se(line_start, line_end), &exp_pos);

			// selection start after tabs expansion
			int exp_spos = s_pos >= line_start && s_pos <= line_end ? (int)exp_pos[uint(s_pos - line_start)] : -1;

			// selection end after tabs expansion
			//			int exp_epos = e_pos >= line_start && e_pos <= line_end ? (int)exp_pos[uint(e_pos - line_start)]
			//: -1;

			// replace ending \r or \n by a space
			if (!exp_text.empty() && wt_isspace(exp_text.back()))
				exp_text.back() = ' ';

			typedef std::pair<int, int> rng;
			typedef std::pair<rng, char> t_rng;
			std::vector<t_rng> rngs;

			// generate a type string where
			// 'c' = selected comment
			// 'C' = normal comment
			// 's' = selected string
			// 'S' = normal string
			// 'p' = selected text
			// 'P' = plane text
			std::string exp_type;
			for (int i = 0; i < (int)exp_pos.size() - 1; i++)
			{
				int size = int(exp_pos[uint(i + 1)] - exp_pos[uint(i)]);
				int index = line_start + i;
				char t;

				if (ch_states.HasAnyBitOfMask(index, SSParse::chs_comment_mask))
					t = index >= s_pos && index <= e_pos ? 'c' : 'C';
				else if (ch_states.HasAnyBitOfMask(index, SSParse::chs_string_mask))
					t = index >= s_pos && index <= e_pos ? 's' : 'S';
				else
					t = index >= s_pos && index <= e_pos ? 'p' : 'P';

				exp_type.append(std::string((uint)size, t));
			}

			// those strings must be of same length
			_ASSERTE(exp_type.length() == exp_text.length());

			int L = -1, R = -1;
			int linePixWidth = 0;

			// If selection starts somewhere within line,
			// flood from its start to both sides taking char by char
			// to the text until pixel width reaches allowed maximum.
			if (exp_spos > 0 && exp_spos < (int)exp_text.length())
			{
				rngs.emplace_back(rng(exp_spos, exp_spos), exp_type[(uint)exp_spos]);

				linePixWidth = getPixWidth(exp_text, exp_spos);

				L = exp_spos - 1;
				R = exp_spos + 1;

				for (int i = 0; i <= (int)exp_text.length(); i++)
				{
					if (R < (int)exp_text.length())
					{
						linePixWidth += getPixWidth(exp_text, R);

						if (linePixWidth > maxPixW)
							break;

						char ch_t = exp_type[(uint)R];
						if (rngs.back().second == ch_t)
							rngs.back().first.second = R;
						else
							rngs.emplace_back(rng(R, R), ch_t);

						R++;
					}

					if (L >= 0)
					{
						linePixWidth += getPixWidth(exp_text, L);

						if (linePixWidth > maxPixW)
							break;

						char ch_t = exp_type[(uint)L];
						if (rngs.front().second == ch_t)
							rngs.front().first.first = L;
						else
							rngs.insert(rngs.begin(), t_rng(rng(L, L), ch_t));

						L--;
					}

					if (L < 0 && R >= (int)exp_text.length())
						break;
				}
			}
			else
			{
				rngs.emplace_back(rng(0, 0), exp_type[0]);
				linePixWidth = getPixWidth(exp_text, 0);
				L = -1;
				for (R = 1; R < (int)exp_text.length(); R++)
				{
					linePixWidth += getPixWidth(exp_text, R);

					if (linePixWidth > maxPixW)
						break;

					char ch_t = exp_type[(uint)R];
					if (rngs.back().second == ch_t)
						rngs.back().first.second = R;
					else
						rngs.emplace_back(rng(R, R), ch_t);
				}
			}

			if (!rngs.empty())
			{
				int s_rng = rngs.front().first.first;
				int e_rng = rngs.back().first.second;

				std::wstring rng_text = exp_text.substr((uint)s_rng, uint(e_rng - s_rng + 1));
				std::wstring right_margin;

				if (L >= 0 && (int)rng_text.length() > 3)
					rng_text.replace(rng_text.begin(), rng_text.begin() + 3, L"...");

				if (R < (int)exp_text.length())
				{
					if ((int)rng_text.length() > 3)
						rng_text.replace(rng_text.end() - 3, rng_text.end(), L"...");
				}
				else if (!isActivePeekLogging)
				{
					int spaceWidth = getPixWidth(L" ", 0);
					for (int i = 0; i < 0xFFFF && linePixWidth < maxPixW; i++)
					{
						right_margin += L' ';
						linePixWidth += spaceWidth;
					}
				}

				auto rng_substr_se = [&](int s, int e) -> std::wstring {
					if (rng_text.empty())
						return std::wstring();

					s -= s_rng;
					e -= s_rng;

					if (s < 0)
						s = 0;

					if (e >= (int)rng_text.length())
						e = int(rng_text.length() - 1);

					return rng_text.substr((uint)s, uint(e - s + 1));
				};

				for (t_rng& r : rngs)
				{
					std::wstring str = rng_substr_se(r.first.first, r.first.second);

					lb.MultiMode();

					if (!enhColor)
						lb.DontColour();

					if (HighlightSyntaxInSelection)
					{
						if (r.second >= 'a' && r.second <= 'z')
							lb.BG(sel_bg); // lowercase => selection
						else
							lb.BG(txt_bg); // uppercase => no selection

						if (r.second == 'C' || r.second == 'c')
							lb.FG(cmt_fg).TextS(str.c_str());
						else if (r.second == 'S' || r.second == 's')
							lb.FG(str_fg).TextS(str.c_str());
						else if (r.second == 'P' || r.second == 'p')
							lb.TextS(str.c_str()); // colorize code
					}
					else
					{
						if (r.second >= 'a' && r.second <= 'z')
							// lowercase => selection
							lb.BG(sel_bg).FG(sel_fg).TextS(str.c_str());
						else
						{
							lb.BG(txt_bg); // uppercase => no selection

							if (r.second == 'C' || r.second == 'c')
								lb.FG(cmt_fg).TextS(str.c_str());
							else if (r.second == 'S' || r.second == 's')
								lb.FG(str_fg).TextS(str.c_str());
							else if (r.second == 'P' || r.second == 'p')
								lb.TextS(str.c_str()); // colorize code
						}
					}
				}

				if (!right_margin.empty())
					lb.MultiMode().BG(txt_bg).TextS(right_margin.c_str());
			}

			if (!is_last)
				lb.LineEnd();

			lb.Normal();
		};

		SSParse::TextLine curr_line;

		if (lines->GetLineFromPos((ULONG)s_pos, curr_line))
		{
			int num_added = 0;

			if (lines->HavePrevious(curr_line))
			{
				SSParse::TextLine pre_line;
				if (lines->GetLine(curr_line.line_number() - 1, pre_line))
				{
					add_line(pre_line, false);
					num_added++;
				}
			}

			while (num_added < 3)
			{
				add_line(curr_line, !lines->HaveNext(curr_line));
				num_added++;
				if (!lines->GetNext(curr_line))
					break;
			}

			if (lines->HaveNext(curr_line))
			{
				lb.Normal().Renderer(1).TextS(L"...");

				if (HighlightSyntaxInSelection)
					lb.Normal().BG(sel_bg).TextS(L"... ").LineEnd();
				else
					lb.Normal().BG(sel_bg).FG(sel_fg).TextS(L"... ").LineEnd();
			}
		}
	}

	void show_tooltip(DWORD ms_duration, int s_pos, int e_pos) const
	{
		std::shared_ptr<ArgToolTipEx>& tt = ActiveSettings().tool_tip;

		{
			// if passed duration is 0
			// just clear current tt
			if (ms_duration == 0)
			{
				tt.reset();
				return;
			}

			EdCntPtr ed(g_currentEdCnt);
			if (ed)
			{
				bool tt_is_invalid = tt == nullptr || ed.get() != tt->GetEd() || !::IsWindow(tt->GetParentHWND()) ||
				                     !::IsWindow(tt->m_hWnd);

				if (tt_is_invalid)
					tt.reset(new ArgToolTipEx(ed.get()));

				tt->SetMargin(1, 1, 1, 1);

				COLORREF header_bg =
				    g_IdeSettings ? g_IdeSettings->GetDteEditorColor(L"Indicator Margin", FALSE) : UINT_MAX;
				if (header_bg == UINT_MAX && g_IdeSettings && CVS2010Colours::IsExtendedThemeActive())
					header_bg = g_IdeSettings->GetThemeColor(ThemeCategory11::TextEditorTextManagerItems,
					                                         L"Indicator Margin", FALSE);
				if (header_bg == UINT_MAX)
					header_bg = GetSysColor(COLOR_BTNFACE);

				InitTooltip(*tt);

				FtmBuilder<WCHAR> ftm;

				COLORREF border = header_bg;
				COLORREF header_fg = 0;

				if (ThemeUtils::ColorBrightness(header_bg) < 128)
					header_fg = ThemeUtils::BrightenColor(header_bg, 80);
				else
					header_fg = ThemeUtils::DarkenColor(header_bg, 80);

				ftm.MultiMode().Italic().DontColour();

				if (!gTestsActive)
					ftm.FG(header_fg | 0xFF000000);

				ftm.Text(L"Selection start: ").LineEnd();

				get_tooltip_ftm(*tt, s_pos, e_pos, ftm);

				auto calc_top_left = [ed](CPoint& pt) -> bool {
					if (ed == g_currentEdCnt)
					{
						CRect client, wnd;
						ed->GetWindowRect(&wnd);
						ed->vGetClientRect(&client);
						ed->vClientToScreen(&client);

						if (pt.y == client.top && pt.x == wnd.left)
							return false;

						pt.x = wnd.left;
						pt.y = client.top;
					}

					return true;
				};

				CPoint pt;
				calc_top_left(pt);

				if (is_logging_peek())
				{
					gTestLogger->LogStrW(ftm.Str().c_str());
				}

				if (!ftm.Str().empty())
				{
					COLORREF text_fg = g_IdeSettings->GetDteEditorColor(L"Plain Text", TRUE);
					if (text_fg == UINT_MAX)
						text_fg = GetSysColor(COLOR_BTNTEXT);

					tt->OverrideColor(true, text_fg, header_bg, border);

					static CStringW NULLSTR_WIDE(L"");

					tt->SetDuration(ms_duration, 500);
					tt->DisplayWstrDirect(&pt, ftm.Str().c_str(), NULLSTR_WIDE, NULLSTR_WIDE, 1, 1);
					tt->TrackPosition(calc_top_left, 100);
				}
			}
		}
	}

	void InitTooltip(ArgToolTipEx& tt) const
	{
		bool haveMargins = false;
		int glyphMargin = 0;       // breakpoint and other glyphs
		int lineNumbersMargin = 0; // line numbers
		int spacerMargin = 0;      // space between the line numbers and outlining
		int outliningMargin = 0;   // outlining

		EdCntPtr ed(g_currentEdCnt);

		{
			LOGFONTW lf = {0};
			if (!g_IdeSettings || !g_IdeSettings->GetEditorFont(&lf))
			{
				GetLogFont(lf, VsUI::CDpiAwareness::GetDpiForWindow(*ed), VaFontType::TTdisplayFont);
				lf.lfFaceName[0] = 0;
				wcscpy_s(lf.lfFaceName, 32, L"Consolas");
			}
			else
			{
				ScaleSystemLogFont(lf, VsUI::CDpiAwareness::GetDpiForWindow(*ed));
			}

			if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
			{
				EdCntWPF* edWpf = dynamic_cast<EdCntWPF*>(ed.get());
				if (edWpf)
				{
					double zoom = edWpf->GetViewZoomFactor() / 100.0;

					if (zoom != 1.0)
					{
						lf.lfHeight = (long)((double)lf.lfHeight * zoom);
						lf.lfWidth = (long)((double)lf.lfWidth * zoom);
					}

					glyphMargin = edWpf->GetMarginWidth(L"Glyph");
					lineNumbersMargin = edWpf->GetMarginWidth(L"LineNumber");
					spacerMargin = edWpf->GetMarginWidth(L"Spacer");
					outliningMargin = edWpf->GetMarginWidth(L"Outlining");
					haveMargins = true;
				}
			}

			CFontW font;
			font.CreateFontIndirect(&lf);
			if (font.m_hObject)
				tt.OverrideFont(font);
		}

		tt.mSmartSelectMode = true;

		// Colors we use are user defined

		const DWORD txt_bg = g_IdeSettings->GetDteEditorColor(L"Plain Text", FALSE);
		const DWORD num_bg = txt_bg; // g_IdeSettings->GetDteEditorColor(L"Line Number", FALSE);
		DWORD num_fg = g_IdeSettings->GetDteEditorColor(L"Line Number", TRUE);
		if (num_fg == UINT_MAX) // old IDEs use "Line Numbers" name
			num_fg = g_IdeSettings->GetDteEditorColor(L"Line Numbers", TRUE);

		COLORREF marginColor = g_IdeSettings ? g_IdeSettings->GetDteEditorColor(L"Indicator Margin", FALSE) : UINT_MAX;
		if (marginColor == UINT_MAX && g_IdeSettings && CVS2010Colours::IsExtendedThemeActive())
			marginColor =
			    g_IdeSettings->GetThemeColor(ThemeCategory11::TextEditorTextManagerItems, L"Indicator Margin", FALSE);
		if (marginColor == UINT_MAX)
			marginColor = GetSysColor(COLOR_BTNFACE);

		// Calculate line start position and margins (if not known already)

		long line_start = 0;

		if (!haveMargins)
		{
			CRect wnd;
			ed->GetWindowRect(&wnd);

			long first_line = ed->GetFirstVisibleLine();
			CPoint pt1 = ed->GetCharPos(TERRCTOLONG(first_line, 1));
			ed->vClientToScreen(&pt1);
			int columnWidth = 0;

			{
				CDC dc;
				dc.CreateCompatibleDC(nullptr);
				dc.SelectObject(g_FontSettings->m_TxtFont);

				CSize size;
				::GetTextExtentPoint32W(dc, L"01234567890123456789", 20, &size);
				columnWidth = size.cx / 20;
			}

			outliningMargin = (columnWidth * 17) / 10;
			if (outliningMargin > 20)
				outliningMargin = 20;
			lineNumbersMargin = 5 * columnWidth;
			spacerMargin = 2;

			line_start = pt1.x - wnd.left - 1;
			glyphMargin = line_start - outliningMargin - lineNumbersMargin - spacerMargin;

			haveMargins = true;
		}
		else
		{
			line_start = glyphMargin + lineNumbersMargin + spacerMargin + outliningMargin +
			             VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		}

		// Default ToolTip line renderer
		// -------------------------------------
		// It is not usable in text markup,
		// it is just defined or not and handles
		// all those lines which already do not
		// have assigned custom renderer.

		uint edDpi = VsUI::CDpiAwareness::GetDpiForWindow(*ed);
		std::vector<CRect> line_ends; // used to draw remaining portions of lines
		tt.renderers[0] = FormatRenderer([txt_bg, line_ends, edDpi](FormatRenderer::DrawingContext& ctx) mutable {
			FormattedTextLine tl = *ctx.fmt;
			tl._rc.bottom = ctx.dstRect->bottom;

			// When any of lines is rendered, tooltip size may expand.
			// Problem is, that then places without text on preceding
			// lines have default background - is not used txt_bg color.
			//
			// The line_ends is a list of empty rectangles representing
			// ends of previously drawn lines. Each is updated by every
			// line that extends width of tooltip, see: #extend_line_end

			int max_right = max(ctx.dstRect->right, tl._rc.right);
			for (auto& rc : line_ends)
			{
				if (rc.right < max_right)
				{
					rc.right = max_right;
					ctx.dc->FillSolidRect(rc, txt_bg);
					rc.left = rc.right; // #extend_line_end
				}
			}

			// If BG is defined, we want to process
			// this line end if width of ToolTip expands.
			// So we add the end of current line for further
			// processing, see: #extend_line_end
			//
			// Note: if BG is defined, it means, that current
			// line part is a code part. Parts without BG color
			// are only in caption. Line numbers have its own
			// renderer, so they are not handled in this default.

			if (tl._bg_rgba != 0)
			{
				CRect rc_end = tl._rc;
				rc_end.left = rc_end.right;
				line_ends.push_back(rc_end);
			}

			ctx.draw_default_bg(&tl); // draw BG with new rect

			if (ctx.fmt->_bg_rgba == 0)
			{
				// draw CAPTION of the tooltip

				ThemeUtils::AutoSaveRestoreDC _asr(*ctx.dc);
				CFontW new_font;

				if (g_FontSettings)
				{
					LOGFONTW lf = {0};
					HFONT currFont = (HFONT)::GetCurrentObject(*ctx.dc, OBJ_FONT);
					if (currFont)
					{
						::GetObjectW(currFont, sizeof(LOGFONTW), &lf);

						LOGFONTW lfTT = {0};
						GetLogFont(lfTT, edDpi, VaFontType::TTdisplayFont);
						lfTT.lfItalic = lf.lfItalic;
						lfTT.lfWeight = lf.lfWeight;
						lfTT.lfUnderline = lf.lfUnderline;

						new_font.CreateFontIndirect(&lfTT);

						ctx.dc->SelectObject(&new_font);
					}
				}

				ctx.draw_default_fg(&tl);
			}
			else
			{
				if (!tl._txt.IsEmpty() && tl._txt[tl._txt.GetLength() - 1] == '.')
				{
					// Fix for ending "dots" (not only ellipsis, one dot also breaks behavior)
					// When code ends by ellipsis, colorizer fails to highlight syntax,
					// so we have to draw code and remaining "dots" separately.

					// find start of dots string
					int last_dot = tl._txt.GetLength() - 1;
					while (last_dot > 0 && tl._txt[last_dot - 1] == '.')
						last_dot--;

					// split text into code and dots
					CStringW code = tl._txt.Mid(0, last_dot);
					CStringW dots = tl._txt.Mid(last_dot);

					// draw code text
					tl._txt = code;
					ctx.draw_default_fg(&tl);

					// move the destination rectangle to the right of code
					tl._rc.left = ctx.rsltRect->right;

					// draw dots
					tl._txt = dots;
					ctx.draw_default_fg(&tl);
				}
				else
				{
					ctx.draw_default_fg(&tl);
				}
			}
		});

		// margins renderer
		// -------------------------------
		// It is used in text markup as 1

		tt.renderers[1] = FormatRenderer([=, &tt](FormatRenderer::DrawingContext& ctx) mutable {
			CRect rc = ctx.fmt->_rc;

			// Extend rect so that whole line is affected
			// by method FillSolidRect and not only minimal
			// bounds of the text being rendered.

			rc.bottom = ctx.dstRect->bottom;
			rc.right = line_start;

			// Inform rendering engine about the width
			// of our recent drawing.

			ctx.rsltRect->right = rc.right;

			if (haveMargins)
			{
				CRect margRc = rc;
				margRc.right = margRc.left + glyphMargin - VsUI::DpiHelper::LogicalToDeviceUnitsX(tt.Margin().left);
				ctx.dc->FillSolidRect(&rc, marginColor); // indicator margin BG
				rc.left = margRc.right;
			}

			// Fill the line number BG
			ctx.dc->FillSolidRect(&rc, num_bg);

			// move rect to numbers position
			if (haveMargins)
				rc.right = line_start - outliningMargin - spacerMargin - VsUI::DpiHelper::LogicalToDeviceUnitsX(2);

			// Setup DC to use our FG color for Text drawing
			ctx.dc->SetTextColor(num_fg);

			// Turn off the syntax highlighting
			VAColorPaintMessages pw(PaintType::DontColor);

			// Draw line number (or ellipsis ...)
			CStringW numStr = ctx.fmt->_txt;
			::DrawTextW(*ctx.dc, numStr, numStr.GetLength(), &rc, DT_RIGHT);
		});
	}
};

#ifdef _SSPARSE_DEBUG_POS_PROPS
void SelRng::get_dbg_text()
{
	auto ctx = SSContext::Get();
	if (ctx && ctx->text.str_ptr)
	{
		if (dbg_s_pos >= 0 && dbg_e_pos >= dbg_s_pos && dbg_e_pos < (int)ctx->text.length())
		{
			dbg_str = ctx->text.substr_se(dbg_s_pos, dbg_e_pos);
		}
	}
}
#endif

std::wstring SelRng::line_column_string() const
{
	auto ctx = SSContext::Get();
	if (ctx && ctx->text.str_ptr)
	{
		return ctx->get_line_column_string(s_pos, e_pos);
	}

	return std::wstring();
}

int SelRng::virtual_start() const
{
	auto ctx = SSContext::Get();
	if (ctx && ctx->text.str_ptr)
	{
		return ctx->text.find_virtual_start(s_pos);
	}
	return -1;
}

int SelRng::virtual_end() const
{
	auto ctx = SSContext::Get();
	if (ctx && ctx->text.str_ptr)
	{
		return ctx->text.find_virtual_end(e_pos);
	}
	return -1;
}

bool SelRng::is_single_line() const
{
	auto ctx = SSContext::Get();

	if (ctx && ctx->text.str_ptr)
	{
		return ctx->text.line_start(s_pos) == ctx->text.line_start(e_pos);
	}

	_ASSERTE(!"Text is NOT initialized!");

	return false;
}

bool SelRng::contains_visually(const SelRng& r) const
{
	if (r.is_empty())
		return contains_visually(r.s_pos);

	if (contains(r))
		return true;

	auto ctx = SSContext::Get();

	if (ctx && ctx->text.str_ptr)
	{
		int vs_pos = r.s_pos >= s_pos ? s_pos : ctx->text.find_virtual_start(s_pos, true);
		int ve_pos = r.e_pos <= e_pos ? e_pos : ctx->text.find_virtual_end(e_pos, true);

		return vs_pos <= r.s_pos && ve_pos >= r.e_pos;
	}

	return false;
}

bool SelRng::contains_visually(int pos) const
{
	if (contains(pos))
		return true;

	auto ctx = SSContext::Get();

	if (ctx && ctx->text.str_ptr)
	{
		if (pos < s_pos)
			return pos >= ctx->text.find_virtual_start(s_pos, true);
		else if (pos > e_pos)
			return pos <= ctx->text.find_virtual_end(e_pos, true);
	}

	return false;
}

bool SelRng::extends_end_trimmed(const SelRng& r) const
{
	auto ctx = SSContext::Get();

	if (ctx && ctx->text.str_ptr)
	{
		int sp = ctx->text.find_nospace(s_pos);
		int rsp = ctx->text.find_nospace(r.s_pos);

		if (sp == rsp)
		{
			int ep = ctx->text.rfind_nospace(e_pos);
			int rep = ctx->text.rfind_nospace(r.e_pos);
			return rep < ep;
		}
	}

	return false;
}

#pragma region VA_Outline

// *****************************************************
//	VA Outline
// *****************************************************
void AddOutline(SSContext::Ptr ssc, bool add_root)
{
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
		return;

	std::vector<std::pair<int, SelRng>> rng_vec;
	int indent = 0;

	for (auto it = ssc->caretPath.rbegin(); it != ssc->caretPath.rend(); ++it)
	{
		FileLineMarker& lm = *it;
		int cp_start = ed->GetBufIndex((long)lm.mStartCp);
		int cp_end = ed->GetBufIndex((long)lm.mEndCp) - 1; // in Smart Select end position points to last included char

		SSParse::SharedWStr text_src((LPCWSTR)lm.mText);
		std::wostringstream new_name;
		SSParse::BraceCounter bc;

		bool add_chars = true;
		int last_open = -1;

		bc.on_state = [text_src, &add_chars, &new_name, &last_open](SSParse::BraceCounter& bcRef) {
			if (bcRef.is_open())
			{
				last_open = bcRef.m_pos;
				new_name << bcRef.m_char;
				add_chars = false;
			}
			else if (bcRef.is_closed())
			{
				bool ellipsis = true;

				// make from: fnc(very very very very large text inside)
				//      this: fnc(very very very very ...)
				if (last_open >= 0 && last_open < bcRef.m_pos)
				{
					int ss = last_open + 1;
					int len = bcRef.m_pos - ss;
					if (len <= 20)
						ellipsis = false;
					else
						len = 20;
					new_name << text_src.substr(ss, len);
					last_open = -1;
				}

				if (ellipsis)
					new_name << L"...";

				add_chars = true; // causes to add also closing brace
			}
		};

		for (int i = 0; i < (int)text_src.length(); i++)
		{
			wchar_t ch = text_src[(uint)i];
			bc.on_char(ch, i, nullptr);

			if (add_chars)
				new_name << ch;
		}

		ssc->pos_buffer_to_wide((ULONG)lm.mStartCp, cp_start);
		ssc->pos_buffer_to_wide((ULONG)lm.mEndCp, cp_end);
		cp_end -= 1; // Smart Select specific

		SelRng rng(cp_start, cp_end);
		rng_vec.push_back(std::make_pair(indent, rng.SetName(new_name.str().c_str())));

		if (ActiveSettings().MenuIsAllowed(Settings::Allowed::trimmed))
		{
			new_name << SS_WSTR_TRIMMED;
			ssc->rng_apply_mode(rng, Block::Mode::Trimmed);
			rng_vec.push_back(std::make_pair(indent, rng.SetName(new_name.str().c_str())));
		}

		indent++;
	}

	if (add_root)
		ssc->bb.AddRoot(L"&Outline");

	for (auto& rng : rng_vec)
	{
		SelRng r = rng.second;
		std::wstring name = std::wstring(2u * rng.first, L' ') + r.name.c_str();
		ssc->bb.AddItem(name.c_str()).SetFnc([ssc, r](const Block::Context& ctx) {
			ssc->apply_selection(r);
			return true;
		});
	}
}
#pragma endregion

#pragma region Lines
// *****************************************************
//	Line(s)
//	Current line
//	Current line trimmed
//	Adjacent lines
// *****************************************************
void AddLines(SSContext::Ptr ssc, std::function<void(SelRng& rng)> inserter = nullptr)
{
	bool single_line = ssc->text.line_start(ssc->selection.s_pos) == ssc->text.line_start(ssc->selection.e_pos);

	std::vector<SelRng> rngs;

	SelRng rng;

	if (ActiveSettings().MenuIsAllowed(Settings::Allowed::lines))
	{
		rng.s_pos = ssc->selection.s_pos;
		rng.e_pos = ssc->selection.e_pos;
		rng.name = single_line ? L"Current line" : L"Current lines";
		ssc->rng_apply_mode(rng, Block::Mode::Lines);
		if (!rng.is_empty())
		{
			if (inserter)
				inserter(rng);
			else
				rngs.push_back(rng);
		}
	}

	if (ActiveSettings().MenuIsAllowed(Settings::Allowed::trimmed))
	{
		rng.s_pos = ssc->selection.s_pos;
		rng.e_pos = ssc->selection.e_pos;
		rng.name = single_line ? L"Current line trimmed" : L"Current lines trimmed";
		ssc->rng_apply_mode(rng, Block::Mode::TrimAfterLines);
		if (!rng.is_empty())
		{
			if (inserter)
				inserter(rng);
			else
				rngs.push_back(rng);
		}
	}

	int s_pos = ssc->text.line_start(ssc->caret_pos);
	int e_pos = ssc->text.line_end(ssc->caret_pos);

	bool ac_ws = ssc->text.line_is_whitespace(s_pos);

	do
	{
		int tmp = ssc->text.prev_line_start(s_pos);

		if (tmp < s_pos && ac_ws == ssc->text.line_is_whitespace(tmp))
			s_pos = tmp;
		else
			break;
	} while (s_pos > 0);

	do
	{
		int tmp = ssc->text.next_line_start(e_pos);

		if (tmp > e_pos && ac_ws == ssc->text.line_is_whitespace(tmp))
			e_pos = ssc->text.line_end(tmp);
		else
			break;
	} while (ssc->text.is_valid_pos(e_pos));

	if (s_pos < e_pos && ssc->text.is_valid_pos(s_pos) && ssc->text.is_valid_pos(e_pos) &&
	    ssc->text.line_start(s_pos) != ssc->text.line_start(e_pos))
	{
		rng.s_pos = s_pos;
		rng.e_pos = e_pos;
		rng.name = L"Adjacent lines";
		if (inserter)
			inserter(rng);
		else
			rngs.push_back(rng);
	}

	if (!rngs.empty())
	{
		ssc->bb.AddRoot(L"&Line(s)");

		for (SelRng& r : rngs)
		{
			s_pos = r.s_pos;
			e_pos = r.e_pos;
			ssc->bb.AddItem(r.name.c_str()).SetFnc([ssc, s_pos, e_pos](const Block::Context& ctx) {
				ssc->apply_selection(s_pos, e_pos);
				return true;
			});
		}
	}
}

#pragma endregion

#pragma region Directives if... endif

// *****************************************************
//	#if...#endif
// *****************************************************

void AddDirectives(SSContext::Ptr ssc,
                   std::function<void(SelRng& outer, SelRng& inner, SelRng* group, int level)> inserter = nullptr)
{
	auto trim = [](std::wstring& s) {
		auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return wt_isspace(c); });
		s.assign(wsfront, std::find_if_not(s.rbegin(), std::wstring::reverse_iterator(wsfront), [](int c) {
			                  return wt_isspace(c);
		                  }).base());
	};

	SSParse::ConditionalBlock::Path path;
	for (const auto& cb : ssc->cb_vec)
	{
		if (cb->build_path(ssc->caret_pos, path) && !path.empty())
		{
			bool root_added = false;

			CStringW indent;
			int level = 0;
			for (auto lm = path.rbegin(); lm != path.rend(); ++lm)
			{
				SSParse::ConditionalBlock::PathItem item = *lm;

				if (item.second && item.first >= 0)
				{
					auto& dir = item.second->dirs[(uint)item.first];
					if ((int)item.second->dirs.size() > item.first + 1)
					{
						std::wstring name = ssc->text.substr(dir.top_pos, dir.line_end_pos - dir.top_pos + 1);
						trim(name);
						name = std::regex_replace(name, ssc->rgx.directive_inner_wspace, std::wstring(L"#"));
						name = std::regex_replace(name, ssc->rgx.long_wspace, std::wstring(L" "));

						if (name.empty())
							continue;
						else if (name.length() > 30)
							name = name.substr(0, 27) + L"...";

						if (!inserter && !root_added)
						{
							ssc->bb.AddRoot(L"#&if...#endif");
							root_added = true;
						}

						std::shared_ptr<SelRng> outer, inner, group;

						// next sibling directive on same level
						auto& dir_next = item.second->dirs[uint(item.first + 1)];

						// start and end positions of "inner" part of block
						int s_pos = ssc->text.next_line_start(dir.top_pos);
						int e_pos = ssc->text.prev_line_end(dir_next.top_pos);

						// type of range depends on whether caret is within header
						SelRng::type rng_type = dir.top_pos >= ssc->caret_pos && dir.line_end_pos <= ssc->caret_pos
						                            ? SelRng::tBlock
						                            : SelRng::tDefault;

						if (inserter)
							inner.reset(new SelRng(SelRng(s_pos, e_pos, rng_type).SetName(name).SetPriority(1)));
						else
						{
							if (ActiveSettings().MenuIsAllowed(Settings::Allowed::inner))
							{
								ssc->bb.AddItem(((LPCWSTR)indent + name + SS_WSTR_INNER).c_str())
								    .SetFnc([s_pos, e_pos, ssc](const Block::Context& ctx) {
									    ssc->apply_selection(s_pos, e_pos, Block::Mode::Normal);
									    return true;
								    });
							}
						}

						// start and end positions of "outer" part of block
						s_pos = ssc->text.line_start(dir.top_pos);

						// if #if* + #endif only, include all text
						if ((dir.type & SSParse::ConditionalBlock::d_if) &&
						    dir_next.type == SSParse::ConditionalBlock::d_endif)
						{
							e_pos = dir_next.line_end_pos;
						}

						if (inserter)
							outer.reset(new SelRng(SelRng(s_pos, e_pos, rng_type).SetName(name).SetPriority(1)));
						else
						{
							ssc->bb.AddItem(indent + name.c_str())
							    .SetFnc([s_pos, e_pos, ssc](const Block::Context& ctx) {
								    ssc->apply_selection(s_pos, e_pos, Block::Mode::Normal);
								    return true;
							    });
						}

						// following adds whole group #if...#endif
						if (item.second->dirs.size() > 2)
						{
							auto& first = item.second->dirs.front();
							auto& last = item.second->dirs.back();
							s_pos = ssc->text.line_start(first.top_pos);
							e_pos = ssc->text.line_end(last.top_pos);
							name = item.second->get_name();

							if (inserter)
								group.reset(new SelRng(SelRng(s_pos, e_pos, rng_type).SetName(name).SetPriority(1)));
							else
							{
								ssc->bb.AddItem(indent + name.c_str())
								    .SetFnc([s_pos, e_pos, ssc](const Block::Context& ctx) {
									    ssc->apply_selection(s_pos, e_pos, Block::Mode::Normal);
									    return true;
								    });
							}
						}

						if (outer && inner && inserter)
							inserter(*outer, *inner, group.get(), level);
					}
				}

				indent += "  ";
				level++;
			}

			break; // for (auto cb : ssc->cb_vec)
		}
	}
}
#pragma endregion

#pragma region Comments

// *****************************************************
//	/* comment */
//	// comment
//	Comment block
// *****************************************************

void AddCComments(SSContext::Ptr ssc, std::function<void(SelRng& rng)> inserter = nullptr)
{
	/////////////////////////////////
	//	/* comment */
	//	// comment

	int top_comment = -1;             // start position of the comment
	int bottom_comment = -1;          // end position of the comment
	int top_stopper = -1;             // position of the first non-comment and non-space char on top
	int top_comment_line_start = -1;  // start of line where top_comment is within its range
	bool single_line_comment = false; // true if comment starts on the same line as caret

	SSParse::CharState specific_state =
	    (SSParse::CharState)(ssc->ch_states[ssc->caret_pos].state & SSParse::chs_comment_mask);
	int start_pos = -1;

	if (specific_state != SSParse::chs_none)
		start_pos = ssc->caret_pos;

	if (start_pos == -1 && ssc->text.is_space_or_EOF(ssc->caret_pos))
	{
		int line_end = ssc->text.line_end(ssc->caret_pos);
		if (line_end != -1)
		{
			int no_space = ssc->text.rfind_nospace(line_end);
			if (no_space != -1 && no_space <= ssc->caret_pos && line_end >= ssc->caret_pos)
			{
				if (ssc->ch_states[no_space].HasAnyBitOfMask(SSParse::chs_comment_mask))
				{
					start_pos = no_space;
					specific_state = (SSParse::CharState)(ssc->ch_states[start_pos].state & SSParse::chs_comment_mask);
				}
			}
		}

		if (start_pos == -1)
		{
			int next = ssc->text.find_nospace(ssc->caret_pos);
			if (next != -1 && ssc->ch_states[next].HasAnyBitOfMask(SSParse::chs_comment_mask))
			{
				start_pos = next;
				specific_state = (SSParse::CharState)(ssc->ch_states[next].state & SSParse::chs_comment_mask);
			}
		}
	}

	if (start_pos != -1)
	{
		top_comment = -1;
		bottom_comment = -1;

		for (int i = start_pos; ssc->text.is_valid_pos(i); i--)
		{
			if (ssc->ch_states[i].Contains(specific_state))
				top_comment = i;
			else if (!ssc->text.is_space(i))
			{
				top_stopper = i;
				break;
			}
		}

		// if stopper is on same line as end of comment,
		// it means, that caret is positioned within
		// single line comment and thus bottom stopper
		// should end on same line as start_stopper
		single_line_comment = ssc->text.line_start(start_pos) == ssc->text.line_start(top_stopper);

		for (int i = start_pos; ssc->text.is_valid_pos(i); i++)
		{
			if (ssc->ch_states[i].Contains(specific_state))
				bottom_comment = i;
			else if (single_line_comment && ssc->text.is_on_line_end(i, true))
				break;
			else if (!ssc->text.is_space(i))
				break;
		}

		top_comment_line_start = ssc->text.line_start(top_comment);

		// if comment is built of multiple lines of "//" comments,
		// remove first line if it corresponds to other
		// element and then find the start of comment...
		if (specific_state != SSParse::chs_c_comment && top_comment_line_start == ssc->text.line_start(top_stopper) &&
		    top_comment_line_start != ssc->text.line_start(bottom_comment))
		{
			top_comment = ssc->text.next_line_start(top_comment, true);

			for (; top_comment < bottom_comment; top_comment++)
				if (ssc->ch_states[top_comment].Contains(specific_state))
					break;
		}

		if (top_comment >= 0 && bottom_comment > top_comment)
		{
			SelRng rng;

			rng.name = specific_state == SSParse::chs_c_comment ? L"/* co&mment */" : L"// co&mment";

			rng.b_type = SSParse::CodeBlock::b_comment;
			rng.s_pos = top_comment;
			rng.e_pos = bottom_comment;

			if (inserter)
				inserter(rng);
			else
			{
				ssc->bb.AddRoot(rng.name.c_str()).SetFnc([rng, ssc](const Block::Context& ctx) {
					ssc->apply_selection(rng);
					return true;
				});
			}
		}
	}

	/////////////////////////////////
	// Comment block

	if (start_pos == -1)
		start_pos = ssc->caret_pos;

	int top_comment_1 = -1;
	int bottom_comment_1 = -1;
	top_stopper = -1;
	top_comment_line_start = -1;
	single_line_comment = false;

	for (int i = start_pos; ssc->text.is_valid_pos(i); i--)
	{
		if (ssc->ch_states[i].HasAnyBitOfMask(SSParse::chs_comment_mask))
			top_comment_1 = i;
		else if (!ssc->text.is_space(i))
		{
			top_stopper = i;
			break;
		}
	}

	// if stopper is on same line as caret which is in
	// comment, it means, that caret is positioned
	// within single line comment and thus bottom stopper
	// should end on same line as start_stopper
	single_line_comment = ssc->text.line_start(start_pos) == ssc->text.line_start(top_stopper);

	for (int i = start_pos; ssc->text.is_valid_pos(i); i++)
	{
		if (ssc->ch_states[i].HasAnyBitOfMask(SSParse::chs_comment_mask))
			bottom_comment_1 = i;
		else if (single_line_comment && ssc->text.is_on_line_end(i, true))
			break;
		else if (!ssc->text.is_space(i))
			break;
	}

	top_comment_line_start = ssc->text.line_start(top_comment_1);

	// if comment is built of multiple lines of "//" comments,
	// remove first line as it corresponds to other
	// element and then find the start of comment...
	if (ssc->ch_states[top_comment_1].Contains(SSParse::chs_comment) &&
	    top_comment_line_start ==
	        ssc->text.line_start(
	            top_stopper) // comparison is true if comment top position IS on same line as its stopper
	    && top_comment_line_start !=
	           ssc->text.line_start(
	               bottom_comment_1)) // comparison is true if comment top position IS NOT on same line as its bottom
	{
		top_comment_1 = ssc->text.next_line_start(top_comment_1, true);

		for (; top_comment_1 < bottom_comment_1; top_comment_1++)
			if (ssc->ch_states[top_comment_1].HasAnyBitOfMask(SSParse::chs_comment_mask))
				break;
	}

	if ((top_comment_1 != top_comment || bottom_comment_1 != bottom_comment) && top_comment_1 >= 0 &&
	    bottom_comment_1 >= 0)
	{
		SelRng rng;

		rng.name = L"Co&mment block";
		rng.b_type = SSParse::CodeBlock::b_comment;
		rng.s_pos = top_comment_1;
		rng.e_pos = bottom_comment_1;

		if (inserter)
			inserter(rng);
		else
		{
			ssc->bb.AddRoot(rng.name.c_str()).SetFnc([rng, ssc](const Block::Context& ctx) {
				ssc->apply_selection(rng);
				return true;
			});
		}
	}
}
#pragma endregion

#pragma region Matching braces

// *****************************************************
//	Matching (...)
//	Matching [...]
//	Matching {...}
//	Matching <...>
// *****************************************************

bool GetMatchingBraces(SSContext::Ptr ssc, SelRng& rng, int caret_pos = -1)
{
	if (caret_pos == -1)
		caret_pos = ssc->caret_pos;

	if (ssc->text.is_one_of(caret_pos, L"()[]{}<>"))
	{
		LPCSTR brs = "";
		bool reversed = true;
		bool ignore_angles = true;

		switch (ssc->text.safe_at(caret_pos))
		{
		case '<':
			reversed = false;
			// fall through
		case '>':
			ignore_angles = false;
			break;
		case '(':
			reversed = false;
			// fall through
		case ')':
			brs = "()";
			break;
		case '{':
			reversed = false;
			// fall through
		case '}':
			brs = "{}";
			break;
		case '[':
			reversed = false;
			// fall through
		case ']':
			brs = "[]";
			break;
		}

		SSParse::BraceCounter bc = SSParse::BraceCounter(brs, reversed);
		bc.set_ignore_angle_brackets(ignore_angles);

		SSParse::CharState caret_state =
		    (SSParse::CharState)(ssc->ch_states[caret_pos].state & SSParse::chs_boundary_mask);

		int step = reversed ? -1 : 1;
		for (int i = caret_pos; ssc->text.is_valid_pos(i); i += step)
		{
			if (caret_state && !ssc->ch_states[i].HasAnyBitOfMask(caret_state))
				break;
			else if (!caret_state && ssc->ch_states[i].HasAnyBitOfMask(SSParse::chs_boundary_mask))
				continue;

			wchar_t ch = ssc->text[(uint)i];

			if (wt_isspace(ch))
				continue;

			bc.on_char(ch, i, nullptr);

			if (bc.is_closed())
			{
				rng.s_pos = __min(caret_pos, bc.m_pos);
				rng.e_pos = __max(caret_pos, bc.m_pos);
				return true;
			}
		}
	}

	return false;
}
#pragma endregion

#pragma region String literals

bool GetRange(SSContext::Ptr ssc, int& s_pos, int& e_pos, std::function<bool(int)> accept_pos = nullptr)
{
	if (!ssc->sel_start.is_valid())
		return false;

	SelRng rng = ssc->sel_start;
	if (rng.is_empty())
		rng.e_pos++;

	if (!ssc->text.is_valid_pos(rng.s_pos) || !ssc->text.is_valid_pos(rng.e_pos))
		return false;

	if (accept_pos)
	{
		for (int i = rng.s_pos + 1; i <= rng.e_pos; i++)
			if (!accept_pos(i))
				return false;

		s_pos = rng.s_pos;
		while (s_pos > 1 && accept_pos(s_pos - 1))
			s_pos--;

		e_pos = rng.e_pos;
		while (e_pos + 1 < (int)ssc->ch_states.size() && accept_pos(e_pos + 1))
			e_pos++;
	}

	return true;
}

// *****************************************************
//	String literals: "...", R"...", L"...", LR"..."
// *****************************************************

bool GetStateRange(SSContext::Ptr ssc, int& s_pos, int& e_pos, SSParse::CharState mask, bool specific,
                   std::function<bool(int)> accept_pos = nullptr)
{
	std::function<bool(int)> is_good_char;

	if (specific)
	{
		SSParse::CharState state = (SSParse::CharState)(ssc->ch_states[ssc->caret_pos].state & mask);

		if (state == SSParse::chs_none)
			return false;

		is_good_char = [&ssc, state, accept_pos](int pos) -> bool {
			return ssc->ch_states[pos].Contains(state) || (accept_pos && accept_pos(pos));
		};
	}
	else
	{
		if (!ssc->ch_states[ssc->caret_pos].HasAnyBitOfMask(mask))
			return false;

		is_good_char = [&ssc, accept_pos, mask](int pos) -> bool {
			return ssc->ch_states[pos].HasAnyBitOfMask(mask) || (accept_pos && accept_pos(pos));
		};
	}

	return GetRange(ssc, s_pos, e_pos, is_good_char);
}

bool GetStateRange(SSContext::Ptr ssc, int& s_pos, int& e_pos, SSParse::CharState mask, bool specific, bool allow_space)
{
	std::function<bool(int)> accept_pos;
	if (allow_space)
	{
		accept_pos = [ssc](int i) { return ssc->text.is_space(i); };
	}
	return GetStateRange(ssc, s_pos, e_pos, mask, specific, accept_pos);
}

bool GetXMLTagAttributes(SSContext::Ptr ssc, std::function<void(int s_pos, int e_pos)> inserter)
{
	if (!inserter || ssc->text.empty())
		return false;

	if (ssc->ctx_start < 0 || ssc->ctx_end <= ssc->ctx_start)
		return false;

	bool retval = false;

	LPCWSTR zero = ssc->text.c_str();

	LPCWSTR str_begin = zero + ssc->ctx_start;
	LPCWSTR str_end = zero + ssc->ctx_end + 1;

	LPCWSTR sel_begin = zero + ssc->sel_start.s_pos;
	LPCWSTR sel_end = zero + ssc->sel_start.e_pos + 1;

	LPCWSTR str_pos = str_begin;
	std::wcmatch str_m;

	while (std::regex_search(str_pos, str_end, str_m, ssc->rgx.xml_tag))
	{
		LPCWSTR tag_begin = str_m[0].first;
		LPCWSTR tag_end = str_m[0].second;

		if (tag_begin >= sel_end)
			break;

		if (tag_begin <= sel_begin && tag_end >= sel_end)
		{
			LPCWSTR tag_pos = tag_begin;
			std::wcmatch tag_m;

			while (std::regex_search(tag_pos, tag_end, tag_m, ssc->rgx.xml_attributes))
			{
				inserter(ptr_sub__int(tag_m[0].first, zero),
				         ptr_sub__int(tag_m[0].second, zero) - 1); // whole attribute
				inserter(ptr_sub__int(tag_m[1].first, zero), ptr_sub__int(tag_m[1].second, zero) - 1); // name part
				inserter(ptr_sub__int(tag_m[2].first, zero),
				         ptr_sub__int(tag_m[2].second, zero) - 1); // value part w/ quotes

				if (tag_m[3].length() > 0) // group #3 is quote
					inserter(ptr_sub__int(tag_m[4].first, zero),
					         ptr_sub__int(tag_m[4].second, zero) - 1); // value part w/o quotes

				retval = true;
				tag_pos = tag_m[0].second;
			}
		}

		str_pos = tag_end;
	}

	return retval;
}

bool GetStringLiteral(SSContext::Ptr ssc, int& s_pos, int& e_pos)
{
	if (GetStateRange(ssc, s_pos, e_pos, SSParse::chs_string_mask, true, false))
	{
		if (ssc->text.is_identifier_start(e_pos + 1))
		{
			// C++11 user defined literals
			int id_end =
			    ssc->text.find(
			        e_pos + 1, [](wchar_t ch) { return !ISCSYM(ch); }, ssc->text.make_char_filter(ssc->ch_states)) -
			    1;

			if (id_end > e_pos)
				e_pos = id_end;
		}

		return true;
	}

	return false;
}

bool GetWord(SSContext::Ptr ssc, int& s_pos, int& e_pos)
{
	if (!ssc->sel_start.is_valid())
		return false;

	SelRng rng = ssc->sel_start;
	if (rng.is_empty())
		rng.e_pos++;

	if (ssc->text.is_identifier(rng.s_pos))
	{
		for (int i = rng.s_pos + 1; i <= rng.e_pos; i++)
			if (!ssc->text.is_identifier(i))
				return false;

		s_pos = rng.s_pos;
		while (ssc->text.is_identifier(s_pos - 1) || (ssc->is_cpp && ssc->text.is_within_line_continuation(s_pos - 1)))
			s_pos--;

		e_pos = rng.e_pos;
		while (ssc->text.is_identifier(e_pos + 1) || (ssc->is_cpp && ssc->text.is_within_line_continuation(e_pos + 1)))
			e_pos++;

		return true;
	}

	return false;
}

bool GetOperator(SSContext::Ptr ssc, int& s_pos, int& e_pos, int at_pos = -1)
{
	return nullptr != ssc->get_operator(at_pos, &s_pos, &e_pos);
}

bool GetWhiteSpace(SSContext::Ptr ssc, int& s_pos, int& e_pos)
{
	if (!ssc->sel_start.is_valid())
		return false;

	SelRng rng = ssc->sel_start;
	if (rng.is_empty())
		rng.e_pos++;

	std::wstring spaces = L" \t";
	if (ssc->text.is_one_of(rng.s_pos, spaces))
	{
		for (int i = rng.s_pos + 1; i <= rng.e_pos; i++)
			if (!ssc->text.is_one_of(i, spaces))
				return false;

		s_pos = rng.s_pos;
		while (ssc->text.is_one_of(s_pos - 1, spaces))
			s_pos--;

		e_pos = rng.e_pos;
		while (ssc->text.is_one_of(e_pos + 1, spaces))
			e_pos++;

		return true;
	}

	return false;
}

void AddStringLiterals(SSContext::Ptr ssc)
{
	int s_pos, e_pos;
	if (GetStringLiteral(ssc, s_pos, e_pos))
	{
		SSParse::CharState state =
		    (SSParse::CharState)(ssc->ch_states[ssc->caret_pos].state & SSParse::chs_string_mask);
		SSParse::CodeBlock::block_type btype =
		    (SSParse::CodeBlock::block_type)((uint)state | SSParse::CodeBlock::b_string_literal);
		SSParse::CodeBlock::Ptr block(new SSParse::CodeBlock(btype, ssc->text, '\"', s_pos, e_pos));

		std::wstring name = block->get_text();
		name = std::regex_replace(name, ssc->rgx.line_break, L" ");

		if (name.length() > 30)
		{
			name = name.substr(0, 26);
			name += L"...\"";
		}

		ssc->bb.AddRoot(name.c_str()).SetFnc([block, ssc](const Block::Context& ctx) {
			ssc->apply_selection(*block, ctx);
			return true;
		});
	}
}
#pragma endregion

#pragma region Context

struct CommandsAndContext
{
	Settings& actSett;

	bool tree_valid;
	bool lines, scopes, trimmed, inner, comments;

	SSContext::Ptr ssc;
	SSParse::CodeBlock::PtrSet path;

	SelRng sel;
	std::set<SelRng> tmp_sel_rngs;

	explicit CommandsAndContext(SSContext::Ptr ctx) : actSett(ActiveSettings()), ssc(std::move(ctx))
	{
		// DbgTimeMeasure _dbg_time(__FUNCTION__);

		tree_valid = ssc->prepare_tree();

		lines = ssc->lines_allowed();
		scopes = ssc->scope_allowed();
		trimmed = ssc->trimmed_allowed();
		inner = ssc->inner_allowed();
		comments = ssc->comments_allowed();
		sel = ssc->selection;

		if (tree_valid && ssc->update_sel_rngs)
		{
			ssc->tree.root->iterate_children_mutable([this](int depth, SSParse::CodeBlock& block) -> bool {
				SSParse::Parser::ResolveComments(block, ssc->text, ssc->ch_states);
				return true;
			});

			if (!ssc->is_cs_or_cpp())
				ssc->tree.CreatePath(ssc->caret_pos, path);
			else
			{
				ssc->tree.CreatePath(ssc->caret_pos, path);
				ssc->solved_stmts.clear();

				SSParse::CodeBlock::Ptr primary_block;
				SSParse::CodeBlock::Ptr secondary_block;
				SSParse::CodeBlock::Ptr scope_filter;

				for (auto it = path.rbegin(); it != path.rend(); ++it)
				{
					auto b = *it;
					if (b->is_scope() && b->has_open_close() &&
					    !ssc->ch_states[b->open_pos].HasAnyBitOfMask(SSParse::chs_comment_mask |
					                                                 SSParse::chs_string_mask) &&
					    b->scope_contains(ssc->caret_pos, false))
					{
						scope_filter = b;
						break;
					}
				}

				bool primary_scope_only = false;

				for (SSParse::CodeBlock::Ptr bptr : ssc->full_stmts)
				{
					if (bptr->contains(ssc->caret_pos, false, true))
					{
						if (!primary_block || !primary_block->contains(*bptr))
						{
							bptr->ensure_name_and_type(ssc->ch_states, ssc->is_cs);
							ssc->fix_block_open_close(bptr);

							if (bptr->open_pos != -1 && bptr->open_char == '(' &&
							    bptr->scope_contains(ssc->caret_pos, false))
							{
								if (!primary_block || bptr->scope_contains(*primary_block))
									primary_block = bptr;
							}
							else if (bptr->open_pos == -1)
							{
								if (!primary_block || bptr->contains(*primary_block))
									primary_block = bptr;
							}

							ssc->split_cs_cpp_native_unscoped_stmt(bptr, false, [&](SSParse::CodeBlock::Ptr b) {
								if (b->contains(ssc->caret_pos, false, true))
								{
									if (b->open_pos != -1 && b->open_char == '(' &&
									    b->scope_contains(ssc->caret_pos, false))
									{
										if (!secondary_block || b->scope_contains(*secondary_block))
											secondary_block = b;
									}
									else if (b->open_pos == -1)
									{
										if (!secondary_block || b->contains(*secondary_block))
											secondary_block = b;
									}
								}
							});
						}
					}
				}

				// if we don't have a filter block, try to find best fit which is the largest
				// scope that can be found within other blocks;
				{
					SSParse::CodeBlock::Ptr tmp(new SSParse::CodeBlock(SSParse::CodeBlock::b_undefined, ssc->text, '*',
					                                                   ssc->caret_pos, ssc->caret_pos));

					auto ch_filter = ssc->make_char_filter(); // not code filter! scope within directive is OK

					// find the largest non-curly scope that wraps the caret
					for (auto it = path.rbegin(); it != path.rend(); ++it)
					{
						auto bl = *it;

						if (bl->open_char_is_one_of("([<") && bl->scope_contains(*tmp))
						{
							// we don't want one within string nor comment
							if (ssc->text.safe_at(bl->open_pos, ch_filter) == '(')
								tmp = bl;
						}
					}

					if (tmp->open_pos != -1)
					{
						if (!primary_block || tmp->scope_contains(*primary_block))
						{
							primary_block = tmp;
							primary_scope_only = true;
						}
					}
				}

				std::vector<SSParse::CodeBlock::Ptr> tmp_vec(path.begin(), path.end());
				ssc->solved_stmts.clear(); // again needs to be cleared as we need new split

				std::vector<SSParse::CodeBlock::Ptr> blocks;

				auto insert = [this, scope_filter, &blocks](SSParse::CodeBlock::Ptr b) {
					// whole lambda must be handled before filtering
					// as this process makes CODE BLOCK from simple block.
					if (ssc->is_whole_cpp_lambda(b, true))
					{
						b->ensure_name_and_type(ssc->ch_states, ssc->is_cs);
						b->type = SSParse::CodeBlock::b_cpp_lambda;
					}

					if (scope_filter && !b->is_code_block() && !scope_filter->scope_contains(*b))
					{
						return;
					}

					SSParse::Parser::ResolveComments(*b, ssc->text, ssc->ch_states);
					if (b->contains(ssc->caret_pos, false, true))
					{
						if (b->is_code_block())
							blocks.push_back(b);
						else
							path.insert(b);
					}
				};

				// remove unwanted blocks
				SS_FOR_EACH(
				    tmp_vec.begin(), tmp_vec.end(),
				    [this, primary_block, secondary_block, primary_scope_only](SSParse::CodeBlock::Ptr b) {
					    ssc->fix_block_open_close(b);

					    if (b->type & SSParse::CodeBlock::b_type_mask)
					    {
						    return;
					    }

					    auto make_garbage = [&b](bool is_garbage) {
						    if (is_garbage)
						    {
							    b->type = SSParse::CodeBlock::b_garbage;
							    return true;
						    }

						    return false;
					    };

					    if (primary_block && primary_block != b)
					    {
						    if (primary_scope_only)
						    {
							    if (make_garbage(primary_block->scope_contains(*b)))
								    return;
						    }
						    else
						    {
							    if (make_garbage(primary_block->contains(*b)))
								    return;
						    }
					    }

					    if (make_garbage(secondary_block && primary_block != b && secondary_block->contains(*b)))
						    return;

					    if (make_garbage(ssc->is_bracing_mismatch(b->start_pos(), b->end_pos())))
						    return;

					    if (make_garbage(ssc->is_cs_cpp_partial_stmt(b, true)))
						    return;

					    // don't include invalid angle brace blocks
					    if (make_garbage(b->open_char == '<' &&
					                     !ssc->ch_states[b->open_pos].HasAnyBitOfMask(SSParse::chs_comment_mask |
					                                                                  SSParse::chs_string_mask) &&
					                     !ssc->is_template(b->open_pos, '<')))
					    {
						    return;
					    }
				    });

				for (const auto& b : ssc->full_stmts)
				{
					tmp_vec.insert(tmp_vec.begin(), b);
				}

				path.clear();
				for (auto ptr : tmp_vec)
				{
					if (ptr->type == SSParse::CodeBlock::b_garbage)
					{
						continue;
					}

					// MUST be inserted directly, because inserter does
					// NOT pass blocks which contain caret only within children!
					if (ptr->is_code_block())
						blocks.push_back(ptr);
					else
						path.insert(ptr);

					if (ptr->contains(ssc->caret_pos, false, true))
					{
						ssc->split_cs_cpp_stmt(ptr, insert);
					}
				}

				ssc->solved_stmts.clear();

				for (const auto& p : ssc->cs_interp)
				{
					if (ssc->caret_pos >= p.first && ssc->caret_pos <= p.second)
					{
						// insert the {expr.} as a separate block
						insert(std::make_shared<SSParse::CodeBlock>(SSParse::CodeBlock::b_cs_interp_expr, ssc->text,
						                                            (wchar_t)'{', // makes it a block!
						                                            -1, p.first, p.second));

						if (ssc->granular_start())
						{
							int s_pos = p.first + 1;
							int e_pos = p.second - 1;

							// use RAII to change and revert local states
							struct local_states
							{
								int _sp, _ep;
								SSParse::EnumMask<SSParse::CharState> _state;
								SSContext* _ssc;

								local_states(SSContext* ssc, int sp, int ep) : _sp(sp), _ep(ep), _ssc(ssc)
								{
									_state = _ssc->ch_states[_sp];

									// we need to change char states temporarily for this part of text
									SSParse::CharStateIter csi(_sp, _ep, _ssc->is_cs, _ssc->text);
									while (csi.next() && csi.pos <= _ep)
									{
										_ASSERTE(_ssc->ch_states[csi.pos] == _state);
										_ssc->ch_states.Set(csi.pos, csi.state);
									}
								}

								~local_states()
								{
									for (int i = _sp; i <= _ep; i++)
										_ssc->ch_states.Set(i, _state);
								}
							} _local_states(ssc.get(), s_pos, e_pos);

							// find the start and end of block
							auto code_filter = ssc->make_code_filter();
							auto b_spos = ssc->text.find_nospace(s_pos, code_filter);
							auto b_epos = ssc->text.rfind_nospace(e_pos, code_filter);

							auto schar = ssc->text.safe_at(b_spos);
							if (schar != '(')
								schar = '$';

							// create block to be split
							auto b = std::make_shared<SSParse::CodeBlock>(SSParse::CodeBlock::b_expression, ssc->text,
							                                              schar, -1, b_spos, b_epos);

							// split the block and insert sub-blocks
							ssc->split_cs_cpp_stmt(b, insert);
						}
					}
				}

				for (const auto& b : blocks)
				{
					path.erase(b);
					path.insert(b);
				}
			}

			ssc->solved_stmts.clear();

			// Filter out all blocks intersecting with resolved ternary operators.
			// Of course all except those coming from there split_cs_cpp_ternary_expr
			SS_FOR_EACH(path.begin(), path.end(), [this](SSParse::CodeBlock::Ptr b) {
				bool is_garbage = false;

				// no lock needed, we are only reading the map!
				if (b->type != SSParse::CodeBlock::b_ternary_part &&
				    SSContext::is_invalid_ternary_expr(b, ssc->ternary_map))
				{
					b->type = SSParse::CodeBlock::b_garbage;
					is_garbage = true;
				}

				if (!is_garbage)
				{
					ssc->include_or_exclude_end(b, true, L";");
					b->ensure_name_and_type(ssc->ch_states, ssc->is_cs);
					SSParse::Parser::ResolveComments(*b, b->src_code, ssc->ch_states);
				}
			});

			std::vector<SSParse::CodeBlock::Ptr> tmp_vec1(path.begin(), path.end());
			path.clear();

			for (const auto& b : tmp_vec1)
			{
				if (b->type != SSParse::CodeBlock::b_garbage)
				{
					path.insert(b);
				}
			}
		}
	}

	static void insert_superior_rng(const SelRng& rng, std::set<SelRng>& rng_set, const wchar_t* set_name = nullptr)
	{
		if (!rng.is_valid())
		{
			_ASSERTE(!"Invalid range passed!");
		}
		else if (rng.is_empty())
		{
#ifdef _SSPARSE_DEBUG
			_ASSERTE(!"Empty range passed!");
#endif
		}
		else
		{
			if (set_name)
			{
				SS_DBG_TRACE_W(L"Inserting '%s': ID: %d, BID: %d, B: %s, RNG: %s, S: %d, E: %d, VS: %d, VE: %d, N: %s",
				               set_name, rng.id, rng.b_index, rng.is_block() ? L"True" : L"False",
				               rng.line_column_string().c_str(), rng.s_pos, rng.e_pos, rng.virtual_start(),
				               rng.virtual_end(), rng.name.c_str());
			}

			auto found = rng_set.find(rng);

			if (found != rng_set.end())
			{
				SelRng tmp = *found;
				rng_set.erase(found);
				rng_set.insert(SelRng::get_superior(rng, tmp));
			}
			else
			{
				rng_set.insert(rng);
			}
		}
	}

	void insert_tmp_rng(const SelRng& rng)
	{
		insert_superior_rng(rng, tmp_sel_rngs, L"tmp_sel_rngs");
	}

	// Helper method to generate ranges
	void get_ranges(SSParse::CodeBlock::Ptr ptr, std::function<void(SelRng&)> rng_proc, bool direct = false)
	{
		if (ptr->processed)
			return;

		ptr->processed = true;

		std::set<SelRng> rngs;

		using Portion = Block::Portion;
		using Mode = Block::Mode;

		auto add_in_modes = [&](SelRng r, std::initializer_list<Mode> modes) {
			for (auto m : modes)
			{
				r.id = SelRng::next_id();
				ssc->rng_apply_mode(r, m);
				if (direct)
					rng_proc(r);
				else
					insert_superior_rng(r, rngs);
			}
		};

		auto add_ptr_rng_ex = [&rngs, this, direct, rng_proc](SSParse::CodeBlock::Ptr bPtr, Portion p, Mode m,
		                                                      bool block) {
			SelRng r;
			if (ssc->rng_init(r, bPtr->name, *bPtr, p, m))
			{
				if (!block)
					r.remove_type(SelRng::tBlock);

				if (!r.is_empty())
				{
					if (direct)
						rng_proc(r);
					else
						insert_superior_rng(r, rngs);
				}
			}
		};

		auto add_ptr_rng = [&add_ptr_rng_ex](SSParse::CodeBlock::Ptr bPtr, Portion p, Mode m) {
			add_ptr_rng_ex(bPtr, p, m, true);
		};

		auto add_ptr_rng_not_block = [&add_ptr_rng_ex](SSParse::CodeBlock::Ptr bPtr, Portion p, Mode m) {
			add_ptr_rng_ex(bPtr, p, m, false);
		};

		auto add_with_comments = [add_in_modes, this](SSParse::CodeBlock::Ptr ptr1) {
			int s_vr, e_vr;

			SelRng rng;
			ssc->rng_from_block(rng, *ptr1, Portion::OuterText);

			ptr1->get_range_with_comments(s_vr, e_vr, ssc->caret_pos);
			if (s_vr < rng.s_pos || e_vr > rng.e_pos)
			{
				rng.s_pos = s_vr;
				rng.e_pos = e_vr;
				rng.name = ptr1->name;
				rng.b_type = ptr1->type;
				IF_DBG(rng.b_index = ptr1->index);
				rng.rng_type = SelRng::tWithComments;
				rng.add_type(SelRng::tNotForMenu);
				add_in_modes(rng, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
			}

			ptr1->get_range_with_comments(s_vr, e_vr);
			if (s_vr < rng.s_pos || e_vr > rng.e_pos)
			{
				rng.s_pos = s_vr;
				rng.e_pos = e_vr;
				rng.name = ptr1->name;
				rng.b_type = ptr1->type;
				IF_DBG(rng.b_index = ptr1->index);
				rng.rng_type = SelRng::tWithComments;
				add_in_modes(rng, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
			}
		};

		// [case: 86695] scoped case
		// following block of code resets ptr and changes it into scoped case block
		if (ptr->is_scoped_case() || (ptr->parent && ptr->parent->is_scoped_case()))
		{
			auto parent = ptr->is_scoped_case() ? ptr.get() : ptr->parent;
			auto child = ptr->is_scoped_case() ? ptr->children.begin()->get() : ptr.get();

			parent->processed = true;
			child->processed = true;

			auto old_ptr = ptr;
			ptr.reset(new SSParse::CodeBlock(SSParse::CodeBlock::b_case_scope, ptr->src_code, '{', parent->start_pos(),
			                                 child->start_pos(), child->end_pos()));

			ptr->name = parent->name + L" " + child->name;

			if (parent->comments_resolved)
				ptr->prefix_comment = parent->prefix_comment;
			if (child->comments_resolved)
				ptr->suffix_comment = child->suffix_comment;
			ptr->comments_resolved = true;
		}

		// special handling for directives
		if (ptr->is_of_type(SSParse::CodeBlock::b_directive))
		{
			add_ptr_rng(ptr, Block::Portion::OuterText, Block::Mode::Trimmed);
			add_ptr_rng(ptr, Block::Portion::OuterText, Block::Mode::LinesAfterTrim);

			if (inner)
			{
				SelRng r;
				ssc->rng_init(r, ptr->name, *ptr, Block::Portion::Scope, Block::Mode::Normal);
				r.remove_type(SelRng::tScope);
				r.add_type(SelRng::tInner);
				add_in_modes(r, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
			}

			if (comments)
				add_with_comments(ptr);

			if (ptr->direct_group)
			{
				SelRng r;
				r.s_pos = ptr->direct_group->start_pos();
				r.e_pos = ptr->direct_group->end_pos();
				r.name = ptr->direct_group->name;
				r.b_type = ptr->direct_group->type;
				IF_DBG(r.b_index = ptr->direct_group->index);
				r.rng_type = SelRng::tBlock;
				add_in_modes(r, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});

				if (comments)
				{
					// add "with comments" version
					int s_vr, e_vr;
					ptr->direct_group->get_range_with_comments(s_vr, e_vr);
					if (s_vr < r.s_pos || e_vr > r.e_pos)
					{
						r.s_pos = s_vr;
						r.e_pos = e_vr;
						r.name = ptr->direct_group->name;
						r.b_type = ptr->direct_group->type;
						IF_DBG(r.b_index = ptr->direct_group->index);
						r.add_type(SelRng::tWithComments);
						r.remove_type(SelRng::tBlock);
						add_in_modes(r, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
					}
				}
			}
		}
		else
		{
			bool avoid_this = false;

			// avoid parent type blocks w/o children if headers are not allowed
			if (!actSett.block_include_header && ptr->is_invalid_parent())
				avoid_this = true;

			if (!avoid_this)
			{
				add_ptr_rng(ptr, Block::Portion::OuterText, Block::Mode::Trimmed);
				add_ptr_rng(ptr, Block::Portion::OuterText, Block::Mode::LinesAfterTrim);

				if (comments)
					add_with_comments(ptr);

				// If scope (or brace) type is being added, we add also:
				// - "inner" - stands for text between braces
				// - "scope" - stands for text including braces
				if (ssc->is_cs_or_cpp())
				{
					// allow to select header of non-scope parent block
					if (actSett.block_include_header && ptr->is_parent_type() && !ptr->is_scope())
					{
						if (ptr->has_open_close())
						{
							add_ptr_rng_not_block(ptr, Block::Portion::Prefix, Block::Mode::Trimmed);
							add_ptr_rng_not_block(ptr, Block::Portion::Prefix, Block::Mode::LinesAfterTrim);
						}

						add_ptr_rng_not_block(ptr, Block::Portion::OuterTextNoChild, Block::Mode::Trimmed);
						add_ptr_rng_not_block(ptr, Block::Portion::OuterTextNoChild, Block::Mode::LinesAfterTrim);
					}

					// 					add_ptr_rng_not_block(ptr, Block::Portion::OuterTextNoChild,
					// Block::Mode::TrimmedWOSemicolon);

					if (ptr->is_scope() || (ptr->is_brace_type() && ptr->scope_contains(ssc->caret_pos, false)))
					{
						if (actSett.block_include_header || !ssc->is_block_cmd() || !ptr->is_nonscope_parent())
						{
							if (inner)
							{
								add_ptr_rng_not_block(ptr, Block::Portion::InnerText, Block::Mode::Trimmed);
								add_ptr_rng_not_block(ptr, Block::Portion::InnerText, Block::Mode::LinesAfterTrim);
							}

							if (scopes)
							{
								add_ptr_rng_not_block(ptr, Block::Portion::Scope, Block::Mode::Trimmed);
								add_ptr_rng_not_block(ptr, Block::Portion::Scope, Block::Mode::LinesAfterTrim);
							}

							// allow to select also the header of scope block
							if (actSett.block_include_header && ptr->is_scope() && !ptr->children.empty())
							{
								SSParse::CodeBlock::Ptr hdr;
								for (auto ch : ptr->children)
								{
									if (ch && ch->is_header() && ch->open_char == '(')
									{
										ch->ensure_name_and_type(ssc->ch_states, ssc->is_cs);

										add_ptr_rng_not_block(ch, Block::Portion::Prefix, Block::Mode::Trimmed);
										add_ptr_rng_not_block(ch, Block::Portion::Prefix, Block::Mode::LinesAfterTrim);

										add_ptr_rng_not_block(ch, Block::Portion::OuterText, Block::Mode::Trimmed);
										add_ptr_rng_not_block(ch, Block::Portion::OuterText,
										                      Block::Mode::LinesAfterTrim);

										// 									ssc->rng_init(rng, ch->name, *ch,
										// Block::Portion::OuterText, Block::Mode::Normal);
										// rngs.insert(rng);

										if (inner)
										{
											add_ptr_rng_not_block(ch, Block::Portion::InnerText, Block::Mode::Trimmed);
											add_ptr_rng_not_block(ch, Block::Portion::InnerText,
											                      Block::Mode::LinesAfterTrim);
										}

										if (scopes)
										{
											add_ptr_rng_not_block(ch, Block::Portion::Scope, Block::Mode::Trimmed);
											add_ptr_rng_not_block(ch, Block::Portion::Scope,
											                      Block::Mode::LinesAfterTrim);
										}

										break;
									}
								}
							}
						}
					}
				}

				// Special case for "else if", which gives user an option
				// to select both, "else if" together or only "if" alone.
				if (ptr->is_of_type(SSParse::CodeBlock::b_else_if) && !ptr->sub_starts.empty())
				{
					std::wstring new_name = ptr->name.substr(5);
					SelRng rng;
					ssc->rng_init(rng, new_name, *ptr, Block::Portion::OuterText, Block::Mode::Trimmed);

					LPCWSTR str_begin = ssc->text.c_str();
					SSParse::WStrFilterIterator code(
					    str_begin, rng.s_pos, rng.e_pos + 1, [str_begin, this](LPCWSTR pos) -> bool {
						    size_t index = size_t(pos - str_begin);

						    if (index >= ssc->ch_states.size())
							    return false;

						    return !ssc->ch_states[(int)index].HasAnyBitOfMask(
						        SSParse::chs_comment_mask | SSParse::chs_string_mask | SSParse::chs_continuation);
					    });

					// find "if" in "end if"
					auto s_pos = std::find(code, code.end(), 'i');
					if (s_pos != code.end())
					{
						rng.s_pos = ptr_sub__int(s_pos.base(), str_begin);
						add_in_modes(rng, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
					}
				}

				// Following inserts whole group statements
				// such as "if/else if/else" or "try/catch/finally".
				if (ptr->group_members_count() > 1)
				{
					int e_pos, s_pos;
					std::wstring name;

					ptr->get_statement_start_end(&name, s_pos, e_pos);

					SelRng r;

					r.s_pos = s_pos;
					r.e_pos = e_pos;
					r.name = name;
					r.b_type = ptr->type;
					IF_DBG(r.b_index = ptr->index);
					r.rng_type = SelRng::tBlock;
					add_in_modes(r, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});

					if (comments)
					{
						// Add "with comments" equivalent of whole group
						SSParse::CodeBlock group(ssc->text, L'\0', s_pos, e_pos);
						SSParse::Parser::ResolveComments(group, ssc->text, ssc->ch_states);
						group.get_range_with_comments(s_pos, e_pos);
						if (s_pos < group.start_pos() || e_pos > group.end_pos())
						{
							r.s_pos = s_pos;
							r.e_pos = e_pos;
							r.name = name;
							r.b_type = ptr->type;
							IF_DBG(r.b_index = ptr->index);
							r.rng_type = SelRng::tWithComments;
							add_in_modes(r, {Block::Mode::Trimmed, Block::Mode::LinesAfterTrim});
						}
					}
				}
			}
		}

		// pass all ranges to inserter
		for (auto r = rngs.rbegin(); r != rngs.rend(); ++r)
		{
			SelRng rng = *r;
			if (rng.is_valid())
				rng_proc(rng);
		}
	}

	void prepare_directives()
	{
		if (ssc->is_extend_shrink_block_cmd())
		{
			//////////////////////////////////////
			// Generate directives - #if...#endif
			auto dir_inserter = [this](SelRng& outer_rng, SelRng& inner_rng, SelRng* group, int lvl) {
				SSParse::CodeBlock::Ptr ptr(new SSParse::CodeBlock(SSParse::CodeBlock::b_directive, ssc->text, L'#',
				                                                   outer_rng.s_pos, inner_rng.s_pos, inner_rng.e_pos,
				                                                   outer_rng.e_pos));
				ptr->name = outer_rng.name.c_str();

				SSParse::Parser::ResolveComments(*ptr, ssc->text, ssc->ch_states);

				if (group)
				{
					SSParse::CodeBlock::Ptr group_ptr(new SSParse::CodeBlock(
					    SSParse::CodeBlock::b_directive_group, ssc->text, L'#', group->s_pos, group->e_pos));
					group_ptr->name = group->name.c_str();
					ptr->direct_group = group_ptr;
					SSParse::Parser::ResolveComments(*group_ptr, ssc->text, ssc->ch_states);
				}

				path.erase(ptr);
				path.insert(ptr);
			};

			// #if...#endif
			AddDirectives(ssc, dir_inserter);
		}
	}

	void add_cmd_extend_shrink()
	{
		///////////////////////////////
		// Init Extend/Shrink command
		if (!ssc->sel_rngs.empty())
		{
			SelRng rng = sel;

			if (ssc->is_extend_cmd())
			{
				if (ssc->find_extend(rng))
				{
					SSContext::Ptr local_ssc = this->ssc;
					LPCWSTR cmd_name = GetCommandName(icmdVaCmd_SmartSelectExtend);
					ssc->bb.AddRoot(cmd_name).SetFnc([local_ssc, rng](const Block::Context& ctx) {
						local_ssc->apply_selection(rng);
						return true;
					});
				}
			}

			rng = sel;
			if (ssc->is_shrink_cmd())
			{
				if (ssc->find_shrink(rng))
				{
					SSContext::Ptr local_ssc = this->ssc;
					LPCWSTR cmd_name = GetCommandName(icmdVaCmd_SmartSelectShrink);
					ssc->bb.AddRoot(cmd_name).SetFnc([local_ssc, rng](const Block::Context& ctx) {
						local_ssc->apply_selection(rng);
						return true;
					});
				}
			}
		}
	}

	void add_cmd_extend_shrink_block()
	{
		///////////////////////////////
		// Init Block command
		if (ssc->is_block_cmd() && !ssc->sel_rngs.empty())
		{
			SelRng rng = sel;
			if (ssc->find_extend_block(rng))
			{
				SSContext::Ptr local_ssc = this->ssc;
				LPCWSTR cmd_name = GetCommandName(icmdVaCmd_SmartSelectExtendBlock);
				ssc->bb.AddRoot(cmd_name).SetFnc([rng, local_ssc](const Block::Context& ctx) {
					local_ssc->apply_selection(rng);
					return true;
				});
			}
		}

		if (ssc->is_block_cmd() && !ssc->sel_rngs.empty())
		{
			SelRng rng = sel;
			if (ssc->find_shrink_block(rng))
			{
				SSContext::Ptr local_ssc = this->ssc;
				LPCWSTR cmd_name = GetCommandName(icmdVaCmd_SmartSelectShrinkBlock);
				ssc->bb.AddRoot(cmd_name).SetFnc([rng, local_ssc](const Block::Context& ctx) {
					local_ssc->apply_selection(rng);
					return true;
				});
			}
		}
	}

	void prepare_cs_cpp_comments()
	{
		// Add comments
		AddCComments(ssc, [this](SelRng& rng) {
			if (!gTestsActive)
			{
				SS_DBG(rng.SetName(L"Comments"));
			}

			if (ssc->caret_pos != rng.s_pos && ssc->caret_pos != rng.e_pos)
				ssc->rng_apply_mode(rng, VASmartSelect::Block::Mode::LinesOrNonSpace);

			insert_tmp_rng(rng);
		});
	}

	void prepare_cs_cpp_literals()
	{
		// string literals
		int s_pos, e_pos;
		if (GetStringLiteral(ssc, s_pos, e_pos))
			insert_tmp_rng(SelRng(s_pos, e_pos).SetName(L"String Literal").SetPriority(1));
	}

	void prepare_extend_shrink()
	{
		/////////////////////////
		// Adds Extend / Shrink

		if (ssc->update_sel_rngs)
		{
			SS_DBG_TRACE_W(L"Caret: %d", ssc->caret_pos);

			SelRng srng = ssc->sel_start;
			SS_DBG_TRACE_W(L"Starting point '%s': ID: %d, BID: %d, B: %s, RNG: %s, S: %d, E: %d, VS: %d, VE: %d, N: %s",
			               L"sel_start", srng.id, srng.b_index, srng.is_block() ? L"True" : L"False",
			               srng.line_column_string().c_str(), srng.s_pos, srng.e_pos, srng.virtual_start(),
			               srng.virtual_end(), srng.name.c_str());

			// C/C#/C++ specific parts
			if (ssc->is_cs_or_cpp())
			{
				prepare_cs_cpp_literals();
				prepare_cs_cpp_comments();
			}

			// not good, includes also ends of scopes when they are adjacent to lines!
			// AddLines(ssc, [this](SelRng & rng){ insert_tmp_rng(rng); });

			// XML Tag Attributes
			if (ssc->is_xml)
			{
				auto inserter = [this](int s_pos, int e_pos) { insert_tmp_rng(SelRng(s_pos, e_pos)); };

				GetXMLTagAttributes(ssc, inserter);
			}

			// whole file
			insert_tmp_rng(
			    SelRng(0, int(ssc->text.length() - 1), SelRng::tBlock).SetName(L"Whole File").SetPriority(1));

			if (ssc->word_start())
			{
				int ws = -1, we = -1, numeric_sig = -1;

				if (ssc->get_numeric_literal_at_position(ssc->caret_pos, ws, we, &numeric_sig))
				{
					insert_tmp_rng(SelRng(ws, we).SetName(L"Number").SetPriority(1));

					if (ssc->granular_start() && numeric_sig > 0 && ssc->caret_pos >= ws + numeric_sig)
						insert_tmp_rng(
						    SelRng(ws + numeric_sig, we).SetName(L"Significant Part Of Number").SetPriority(1));
				}

				else if (GetWord(ssc, ws, we) && we >= ws)
				{
					// 				if (GetOperator(ssc, ws, we) && we >= ws)
					// 					insert_tmp_rng(SelRng(ws, we).SetName(L"Operator"));

					bool split_by_case = ssc->word_start_split_by_case();
					bool split_by_underscore = ssc->word_start_split_by_underscore();

					insert_tmp_rng(SelRng(ws, we).SetName(L"Current Word").SetPriority(1));

					if (split_by_case || split_by_underscore)
					{
						std::set<std::tuple<int, int>> splitters;
						std::set<int> existing_hits;

						wchar_t prev_alpha = '\0';
						int underscores_start = -1;
						for (int i = ws; i <= we; i++)
						{
							wchar_t ch = ssc->text.safe_at(i);
							if (ISCSYM(ch))
							{
								if (split_by_underscore)
								{
									if (ch == '_')
									{
										if (underscores_start == -1)
										{
											underscores_start = i;
											existing_hits.insert(i);
										}
									}
									else if (underscores_start != -1)
									{
										// if underscores are from start, push them as separate token
										if (underscores_start == ws)
										{
											splitters.insert(std::make_tuple(i, 0));
											existing_hits.insert(i);
										}
										else
										{
											splitters.insert(std::make_tuple(underscores_start, i - underscores_start));
											existing_hits.insert(i);
										}

										underscores_start = -1;
									}
								}

								if (split_by_case && wt_isalpha(ch))
								{
									if (prev_alpha != '\0')
									{
										if (wt_islower(prev_alpha) && wt_isupper(ch) &&
										    existing_hits.find(i) == existing_hits.end())
										{
											splitters.insert(std::make_tuple(i, 0));
											existing_hits.insert(i);
										}
										else if (i > 0 && wt_isupper(prev_alpha) && wt_islower(ch) &&
										         existing_hits.find(i - 1) == existing_hits.end())
										{
											splitters.insert(std::make_tuple(i - 1, 0));
											existing_hits.insert(i - 1);
										}
									}

									prev_alpha = ch;
								}
							}
						}

						// if starts by underscores, push it as extra token
						if (underscores_start != -1)
							splitters.insert(std::make_tuple(underscores_start, 0));

						if (!splitters.empty())
						{
							DWORD mode = (DWORD)SSContext::split_mode::caret_extend_L2R |
							             (DWORD)SSContext::split_mode::no_filter |
							             (DWORD)SSContext::split_mode::no_search;

							ssc->split_by_splitters(
							    ws, we, (SSContext::split_mode)mode, splitters,
							    [&](SSParse::CodeBlock::Ptr b, std::tuple<int, int> sp) {
								    int bsp = b->start_pos();
								    int bep = b->end_pos();

								    if (bsp <= bep && (bsp != ws || bep != we))
								    {
									    insert_tmp_rng(SelRng(bsp, bep).SetName(L"Current Word Part").SetPriority(1));
								    }

								    return true;
							    });
						}
					}
				}
			}

			// generate extend/shrink vector
			if (!tmp_sel_rngs.empty())
			{
				ssc->sel_rngs.assign(tmp_sel_rngs.begin(), tmp_sel_rngs.end());

				if (tmp_sel_rngs.find(sel) == tmp_sel_rngs.end())
					ssc->sel_rngs.push_back(sel);

				std::reverse(ssc->sel_rngs.begin(), ssc->sel_rngs.end());

				// filter to keep only available ranges,
				// so that shrink works properly
				std::vector<SelRng> filtered;
				filtered.push_back(sel);

				SelRng rng = sel;
				SS_DBG_TRACE_W(L"### FINAL LIST BEGIN ###");
				while (ssc->find_extend_for_prepare(rng))
				{
					filtered.push_back(rng);
					SS_DBG_TRACE_W(
					    L"Inserting '%s': ID: %d, BID: %d, B: %s, RNG: %s, S: %d, E: %d, VS: %d, VE: %d, N: %s",
					    L"sel_rngs", rng.id, rng.b_index, rng.is_block() ? L"True" : L"False",
					    rng.line_column_string().c_str(), rng.s_pos, rng.e_pos, rng.virtual_start(), rng.virtual_end(),
					    rng.name.c_str());
				}
				SS_DBG_TRACE_W(L"### FINAL LIST END ###");

				ssc->sel_rngs.swap(filtered);
			}
		}
	}

	void process_context()
	{
		if (!path.empty() && ssc->update_sel_rngs)
		{
			///////////////////////////////////
			// Adds Context and Context Lines

			for (auto elm = path.rbegin(); elm != path.rend(); ++elm)
				(*elm)->processed = false;

			for (auto elm = path.rbegin(); elm != path.rend(); ++elm)
			{
				SSParse::CodeBlock::Ptr ptr = *elm;
				std::vector<SelRng> rngs;

				get_ranges(ptr, [&](SelRng& rng) { rngs.push_back(rng); });

				if (ssc->update_sel_rngs)
				{
					std::set<int> not_wanted;
					for (SelRng r : rngs)
					{
						if (not_wanted.find(r.id) != not_wanted.end())
							continue;

						if (gTestsActive)
						{
							// make name understandable for AST purposes
							auto modifs = ssc->rng_get_modifiers_str(r, true);
							if (!modifs.empty())
								r.name = r.name.c_str() + (L"  " + modifs);
						}

						// #SmartSelect_SimpleStatementWholeLine

						if (r.is_lines() || r.is_embedded())
							insert_tmp_rng(r);

						// not as aggressive selections
						// single line containing caret select exactly
						else if (r.is_single_line() && r.contains(ssc->caret_pos))
						{
							insert_tmp_rng(r);
							not_wanted.insert(r.id + 1); // skip following Lines range
						}

						// if caret is exactly on start/end of range, select exactly
						// Note: not in case if caret is on start of line
						else if (ssc->sel_start.is_empty() && r.is_pos_on_se(ssc->sel_start.s_pos) &&
						         !ssc->text.is_on_line_start(ssc->sel_start.s_pos))
						{
							insert_tmp_rng(r);
							not_wanted.insert(r.id + 1); // skip following Lines range
						}
					}
				}
			}
		}
	}

	void generate()
	{
		// generating of #if ... #endif ranges
		// those are pushed to temporary set
		// see: insert_tmp_rng

		prepare_directives();

		// fill ranges for Extend/Shrink commands (or menu)
		// ranges are pushed to temporary set which also
		// works as filter for duplicates see: insert_tmp_rng

		process_context();

		// generate all necessary ranges for commands
		// Extend/Shrink and ExtendBlock/ShrinkBlock

		prepare_extend_shrink();

		// init commands
		// Extend/Shrink and ExtendBlock/ShrinkBlock

		add_cmd_extend_shrink_block();
		add_cmd_extend_shrink();
	}
};

// *****************************************************
//	Context
//	Context Lines
// *****************************************************

void AddCommandsAndContext(SSContext::Ptr ssc)
{
	CommandsAndContext cac(ssc);
	cac.generate();
}
#pragma endregion

size_t GenerateBlocks(DWORD cmdId /*= 0*/)
{
	CurrentBlocks().clear();
	IF_DBG(SSParse::CodeBlock::s_index = 0);
	SelRng::global_id = 0;

	std::wstringstream dbg_wss;
	DWORD dwTicks = ::GetTickCount();

	SSContext::Ptr ssc(SSContext::Get(true, cmdId));
	IF_DBG(dbg_wss << L"SSContext: " << ::GetTickCount() - dwTicks << std::endl);

	if (!ssc->is_initialized)
	{
		_ASSERTE(ssc->is_initialized);
		return 0;
	}

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
	{
		_ASSERTE(ed.get() != nullptr);
		return 0;
	}

	dwTicks = ::GetTickCount();
	AddCommandsAndContext(ssc);
	IF_DBG(dbg_wss << L"Code Context: " << ::GetTickCount() - dwTicks << std::endl);

	return CurrentBlocks().size();
}

std::vector<Block>& CurrentBlocks()
{
	static std::vector<Block> _blocks;
	return _blocks;
}

bool Block::Apply()
{
	if (m_apply)
		return m_apply(m_context);

	return false;
}

bool Block::IsAvailable()
{
	if (m_isAvailable)
		return m_isAvailable(m_context);

	return true;
}

Block* Block::FindByName(LPCWSTR name)
{
	if (name == nullptr)
		return nullptr;

	std::vector<Block>& blocks(CurrentBlocks());

	for (Block& b : blocks)
		if (b.GetName() == name)
			return &b;

	return nullptr;
}

AutoCleanup::~AutoCleanup()
{
	for (auto& act : actions)
	{
		try
		{
			act();
		}
		catch (...)
		{
		}
	}

	VASmartSelect::CurrentBlocks().clear();
	SSContext::Ptr ptr = SSContext::Get();
	if (ptr)
		ptr->temp_cleanup();
}

LPCWSTR GetCommandName(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_SmartSelectExtend:
		return L"CMD_Extend";
	case icmdVaCmd_SmartSelectShrink:
		return L"CMD_Shrink";
	case icmdVaCmd_SmartSelectExtendBlock:
		return L"CMD_Extend_Block";
	case icmdVaCmd_SmartSelectShrinkBlock:
		return L"CMD_Shrink_Block";
	case icmdVaCmd_RefactorModifyExpression:
		return L"CMD_Invert_Expression";
	}
	return nullptr;
}

BOOL IsCommandReady(DWORD cmdId)
{
	LPCWSTR cmdName = VASmartSelect::GetCommandName(cmdId);
	return nullptr != VASmartSelect::Block::FindByName(cmdName);
}

Settings& ActiveSettings()
{
	static Settings _s;
	return _s;
}

void Settings::OnEdCntDestroy(const EdCnt* ed)
{
	try
	{
		if (tool_tip && tool_tip->GetEd() == ed)
			tool_tip.reset();
	}
	catch (...)
	{
		_ASSERTE(FALSE);
	}
}

void HideToolTip()
{
	try
	{
		auto tool_tip(ActiveSettings().tool_tip);

		if (tool_tip && tool_tip->GetSafeHwnd() && tool_tip->IsWindowVisible())
		{
			tool_tip->ShowWindow(SW_HIDE);
		}
	}
	catch (...)
	{
		_ASSERTE(FALSE);
	}
}

bool VASmartSelect::SSContextBase::is_modify_expr_cmd() const
{
	return command == icmdVaCmd_RefactorModifyExpression;
}

bool VASmartSelect::SSContextBase::is_shrink_block_cmd() const
{
	return command == icmdVaCmd_SmartSelectShrinkBlock;
}

bool VASmartSelect::SSContextBase::is_extend_block_cmd() const
{
	return command == icmdVaCmd_SmartSelectExtendBlock;
}

bool VASmartSelect::SSContextBase::is_shrink_cmd() const
{
	return command == icmdVaCmd_SmartSelectShrink;
}

bool VASmartSelect::SSContextBase::is_extend_cmd() const
{
	return command == icmdVaCmd_SmartSelectExtend;
}

const VASmartSelect::SSParse::OpNfo* VASmartSelect::SSContextBase::get_operator_info(int at_pos,
                                                                                     int* spos /*= nullptr*/,
                                                                                     int* epos /*= nullptr*/)
{
	using namespace SSParse;

	if (at_pos < 0)
		at_pos = caret_pos;

	auto code_filter = make_code_filter();
	wchar_t ch = text.safe_at(at_pos);

	if (!ch || !code_filter(at_pos, ch) || ISCSYM(ch) || wt_isspace(ch))
		return nullptr;

	std::wstring code(5, L' ');

	for (int i = at_pos - 2, j = 0; i <= at_pos + 2; i++, j++)
	{
		wchar_t at_ch = text.safe_at(i);

		if (at_ch && code_filter(i, at_ch))
			code[(uint)j] = at_ch;
	}

	LPCWSTR pCode = code.c_str();

	for (const auto& nfo : SSParse::Parser::GetOpNfoVector(is_cs))
	{
		if (nfo.getType() == Parsing::OpType::Splitter && !is_modify_expr_cmd())
			continue;

		int op_offset = 3 - nfo.len;
		for (int i = 0; i < nfo.len; i++)
		{
			LPCWSTR code2 = pCode + op_offset + i;

			if (wcsncmp(code2, nfo.op, nfo.len) == 0)
			{
				int curr_pos = (at_pos - 2) + op_offset + i;

				if (nfo.len == 1)
				{
					// if next char after '.' is digit, it is digit separator, not operator
					if (*nfo.op == L'.')
					{
						wchar_t ch_next = code2[1];
						if (wt_isdigit(ch_next))
							return nullptr;
					}

					// check if we are not on a template
					else if (*nfo.op == L'<' || *nfo.op == '>')
					{
						if (is_template(curr_pos, *nfo.op))
							return nullptr;
					}
				}

				// check if we are not on a template at ">>"
				else if (nfo.len == 2 && nfo.op[0] == '>' && nfo.op[1] == '>')
				{
					if (is_template(curr_pos, '>'))
						return nullptr;
				}

				if (spos)
					*spos = curr_pos;

				if (epos)
					*epos = curr_pos + nfo.len - 1;

				return &nfo;
			}
		}
	}

	return nullptr;
}

bool VASmartSelect::SSContextBase::pos_wide_to_buffer(int vs_pos, ULONG& va_pos)
{
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
		return false;

	ULONG line, col;
	if (lines->LineAndColumnFromPos((ULONG)vs_pos, line, col))
	{
		va_pos = (ULONG)ed->GetBufIndex(TERRCTOLONG((long)line, (long)col));
		return true;
	}

	return false;
}

DTypePtr VASmartSelect::SSContextBase::get_DType(int pos)
{
	if (g_mainThread != GetCurrentThreadId())
	{
		_ASSERTE(!"Call this method only from main thread!");
		return nullptr;
	}

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		int macro_end = -1;
		auto code_filter = make_code_filter();

		if (is_cs || is_cpp)
		{
			wchar_t ch = text.safe_at(pos);
			if (ch == '<' || ch == '>')
			{
				// move to the start of identifier like 'd' in "abc::def<...>"
				int test_pos = ch == '<' ? pos : get_matching_brace(pos, false, code_filter);
				if (test_pos >= 0)
				{
					// next from '<' must be either symbol "typename" or scope delimiter "::abc"
					// simply there must be a symbol like "int", "bool" or some macro...
					int next_pos = text.find_nospace(test_pos + 1, code_filter);
					if (next_pos != -1)
					{
						if (!text.is_identifier_start(next_pos))
						{
							if (is_cs)
								return nullptr;

							// Only in C++ can be symbol preceded by "::",
							//
							// In C# it must be used as "global::" or with NS alias like:
							// using colAlias = System.Collections;
							// ...
							// colAlias::Hashtable test = new colAlias::Hashtable();
							// ...
							// https://msdn.microsoft.com/en-us/library/c3ay4x3d.aspx

							if (!text.is_at_scope_delimiter_start(next_pos, false, false, code_filter))
								return nullptr;
						}
					}

					test_pos = text.rfind_nospace(test_pos - 1, code_filter);
					wchar_t test_ch = text.safe_at(test_pos);
					if (ISCSYM(test_ch))
					{
						test_pos = text.rfind_not_csym(test_pos, code_filter) + 1;
						if (test_pos >= 0 && text.is_identifier_start(test_pos))
							pos = test_pos;
						else
							return nullptr;
					}
					else if (test_ch == ')') // some macro defining type?
					{
						macro_end = test_pos;
						int match_br = get_matching_brace(test_pos, false, code_filter);
						if (match_br != -1)
						{
							test_pos = text.rfind_nospace(match_br - 1, code_filter);
							if (test_pos != -1)
							{
								test_ch = text.safe_at(test_pos);
								if (ISCSYM(test_ch))
								{
									test_pos = text.rfind_not_csym(test_pos, code_filter) + 1;
									if (test_pos >= 0 && text.is_identifier_start(test_pos))
										pos = test_pos;
									else
										return nullptr;
								}
							}
						}
					}
				}
			}
		}

		int sym_s_pos, sym_e_pos;
		auto sym = text.get_symbol(pos, true, &sym_s_pos, &sym_e_pos);
		if (!sym.empty())
		{
			WTString wtSym(sym.c_str());
			MultiParsePtr mp = ed->GetParseDb();
			DType* dtPtr = mp->FindAnySym(wtSym);
			if (dtPtr && dtPtr->MaskedType() != DEFINE)
				return std::make_shared<DType>(dtPtr);
		}

		ULONG offsetToSym;
		if (pos_wide_to_buffer(pos, offsetToSym))
		{
			MultiParsePtr mp = ed->GetParseDb();
			WTString srcBuf = ed->GetBuf();
			WTString scope;
			DTypePtr dt = SymFromPos(srcBuf, mp, (int)offsetToSym, scope);

			if (dt && dt->MaskedType() == DEFINE)
			{
				WTString fullMacro;

				if (macro_end != -1)
				{
					auto ch_filter = [this](int ch_pos, wchar_t& ch) -> bool {
						auto state = ch_states[ch_pos];

						if (state.Contains(SSParse::chs_continuation))
							ch = '\1';
						else if (state.HasAnyBitOfMask(SSParse::chs_comment_mask))
							ch = ' ';
						else if (wt_isspace(ch))
							ch = ' ';

						return true;
					};

					fullMacro = text.substr_se(pos, macro_end, ch_filter).c_str();
					fullMacro.ReplaceAll("\1", "");
					fullMacro.ReplaceAllRE(L"\\s+", false, CStringW(L" "));
				}
				else
					fullMacro = dt->Sym();

				if (!fullMacro.IsEmpty())
				{
					WTString macroExpanded = VAParseExpandAllMacros(mp, fullMacro);

					macroExpanded.ReplaceAllRE(L"\\s+", false, CStringW(L" "));
					macroExpanded.ReplaceAll("::", DB_SEP_STR);
					macroExpanded.ReplaceAll(".", DB_SEP_STR);

					if (!scope.EndsWith(DB_SEP_STR))
						scope.append(DB_SEP_CHR);

					macroExpanded.insert(0, scope.c_str());

					// try to find DType for expansion

					EncodeTemplates(macroExpanded);

					auto tmp = mp->FindExact(macroExpanded);

					if (tmp)
						return std::make_shared<DType>(tmp);
				}
			}

			return dt;
		}
	}

	return nullptr;
}

bool VASmartSelect::SSContextBase::is_type(int pos)
{
	if (!is_cs && !is_cpp)
		return false;

	if (text.is_identifier(pos))
	{
		// this is only quick test,
		// if it fails, get_DType is used

		int sp, ep;
		auto sym = text.get_symbol(pos, true, &sp, &ep);
		if (!sym.empty())
		{
			if (is_cpp &&
			    (sym == L"const" || sym == L"auto" || sym == L"decltype" || sym == L"unsigned" || sym == L"signed"))
			{
				return true;
			}

			if (is_cs && (sym == L"var" || sym == L"string" || sym == L"decimal" || sym == L"System"))
			{
				return true;
			}

			if (sym == L"int" || sym == L"long" || sym == L"bool" || sym == L"void" || sym == L"double" ||
			    sym == L"float")
			{
				return true;
			}
		}
	}

	DTypePtr dtp = get_DType(pos);
	if (dtp)
		return dtp->IsType() || dtp->IsReservedType();

	return false;
}

bool VASmartSelect::SSContextBase::cache_template_char(int pos, wchar_t ch, bool OK_if_match_found,
                                                       bool use_IsTemplateCls, bool va_type_detection)
{
	_ASSERTE(is_preparing_tree || (is_modify_expr_cmd() && !is_initialized));

	if (!text.is_valid_pos(pos))
		return false;

	if (is_comment_or_string(pos))
		return false;

	// try to find in already checked

	if (ch == '<' || ch == '>')
	{
		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
		auto it = operator_angs.find(pos);
		if (it != operator_angs.end())
			return false;

		auto it1 = brace_map.find(pos);
		if (it1 != brace_map.end())
			return true;
	}

	auto cache_result = [&](bool rslt, int match) -> bool {
		_ASSERTE(!rslt || match >= 0);

		if (!rslt)
		{
			IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
			operator_angs.insert(pos);
		}
		else if (match != -1)
		{
			set_matching_brace(pos, match);
		}

		return rslt;
	};

	// first - quick operator detection

	if (ch == '>')
	{
		wchar_t ch_next = text.safe_at(pos + 1);
		if (ch_next == '=') // >=
			return cache_result(false, -1);

		wchar_t ch_prev = text.safe_at(pos - 1);
		if (ch_prev == '-' || ch_prev == '=') // -> and =>
			return cache_result(false, -1);
	}
	else if (ch == '<')
	{
		wchar_t ch_next = text.safe_at(pos + 1);
		if (ch_next == '<' || ch_next == '=')
			return cache_result(false, -1); // << and <= and <<=

		wchar_t ch_prev = text.safe_at(pos - 1);
		if (ch_prev == '<') // <<= and <<
			return cache_result(false, -1);
	}
	else
	{
		return false;
	}

	// next quick test if these are matching braces

	int match = text.find_matching_brace_strict_ex(pos, L";{}", make_code_filter());

	if (match < 0)
		return cache_result(false, -1);

	if (OK_if_match_found)
		return true;

	// Next use existing template detection from VA

	// Note: IsTemplateCls returns false positives
	// in cases like: test(less < more, more > less);
	// That is why this method accepts only negative result.
	// See AST: VAAutoTest:SmartSelect_ExtendShrink_19

	if (use_IsTemplateCls)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed != nullptr)
		{
			ULONG ep;
			if (pos_wide_to_buffer(ch == '<' ? pos : match, ep))
			{
				WTString buff = ed->GetBuf();

				IsTemplateCls itc(ed->m_ftype);

				if (FALSE == itc.IsTemplate(buff.c_str() + ep + 1, buff.GetLength() - (int)ep - 1, 0))
					return cache_result(false, -1);

				// in C# are not allowed edge cases like in C++
				if (is_cs)
					return cache_result(true, match);
			}
		}
	}

	if (is_cpp)
	{
		int left_nospace = text.rfind_nospace((ch == '<' ? pos : match) - 1, make_code_filter());
		if (left_nospace != -1)
		{
			auto left_sym = text.get_symbol(left_nospace, true);
			if (!left_sym.empty())
			{
				if (left_sym == L"template")
					return cache_result(true, match);

				// 				if (left_sym == L"pair")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"vector")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"set")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"map")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"list")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"tuple")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"shared_ptr")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"unique_ptr")
				// 					return cache_result(true, match);
				//
				// 				if (left_sym == L"auto_ptr")
				// 					return cache_result(true, match);

				if (left_sym == L"static_cast")
					return cache_result(true, match);

				if (left_sym == L"const_cast")
					return cache_result(true, match);

				if (left_sym == L"dynamic_cast")
					return cache_result(true, match);

				if (left_sym == L"reinterpret_cast")
					return cache_result(true, match);

				if (left_sym == L"safe_cast")
					return cache_result(true, match);
			}
		}
	}

	// else try symbol detection
	if (va_type_detection)
	{
		// note: get_DType handles also < and >

		auto dt = get_DType(pos);
		if (dt && (dt->IsType() || dt->IsMethod()))
			return cache_result(true, match);

		if (dt)
		{
			WTString sym = dt->Sym();
			if (sym.length() > 1 && sym[0] == DB_SEP_CHR)
				sym = sym.substr(1);

			if (sym.Compare(_T("template")) == 0)
				return cache_result(true, match);

			if (sym.Compare(_T("static_cast")) == 0)
				return cache_result(true, match);

			if (sym.Compare(_T("const_cast")) == 0)
				return cache_result(true, match);

			if (sym.Compare(_T("dynamic_cast")) == 0)
				return cache_result(true, match);

			if (sym.Compare(_T("reinterpret_cast")) == 0)
				return cache_result(true, match);

			if (sym.Compare(_T("safe_cast")) == 0)
				return cache_result(true, match);

			return cache_result(false, -1);
		}
	}

	return true;
}

bool VASmartSelect::SSContextBase::is_comment_or_string(int pos)
{
	if (!text.is_valid_pos(pos))
		return false;

	return ch_states[pos].HasAnyBitOfMask(SSParse::chs_comment_mask | SSParse::chs_string_mask);
}

bool VASmartSelect::SSContextBase::is_template(int pos, wchar_t ch /*= '\0'*/, bool type_detection /*= false*/)
{
	if (!text.is_valid_pos(pos))
		return false;

	if (ch == '\0')
		ch = text.at(pos);

	if (ch != '<' && ch != '>')
		return false;

	if (is_comment_or_string(pos))
		return false;

	IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));

	auto it = operator_angs.find(pos);
	if (it != operator_angs.end())
		return false;

	auto it1 = brace_map.find(pos);
	if (it1 != brace_map.end())
		return true;

	if (type_detection && g_mainThread != GetCurrentThreadId())
		return cache_template_char(pos, ch, false, true, true);

	return false;
}

bool VASmartSelect::SSContextBase::is_operator(int at_pos, LPCWSTR op)
{
	if (op)
	{
		auto op_at_pos = get_operator(at_pos);
		return op && op_at_pos && !wcscmp(op, op_at_pos);
	}

	return false;
}

void VASmartSelect::SSContextBase::set_matching_brace(int open_pos, int close_pos)
{
	if (text.is_valid_pos(open_pos) && text.is_valid_pos(close_pos))
	{
		_ASSERTE(text.is_one_of(open_pos, L"([{<)]}>"));
		_ASSERTE(text.is_one_of(close_pos, L")]}>([{<"));

		IF_SS_PARALLEL(std::lock_guard<std::mutex> _lock(pos_cache_mtx));
		brace_map[open_pos] = close_pos;
		brace_map[close_pos] = open_pos;
	}
	else
	{
		_ASSERTE(!"Invalid brace position(s)!");
	}
}

int VASmartSelect::SSContextBase::get_matching_brace(int pos, bool only_exisitng,
                                                     SharedWStr::CharFilter filter /*= nullptr*/, int min_pos /*= -1*/,
                                                     int max_pos /*= -1*/)
{
	{
		IF_SS_PARALLEL(std::lock_guard<std::mutex> lock(pos_cache_mtx));
		auto it = brace_map.find(pos);
		if (it != brace_map.end())
		{
			int found = it->second;

			wchar_t ch = text.at(found, filter);

			if (!ch)
				return -1;

			if (min_pos != -1 && found < min_pos)
				return -1;

			if (max_pos != -1 && found > max_pos)
				return -1;

			return found;
		}

		auto it_ang = operator_angs.find(pos);
		if (it_ang != operator_angs.end())
			return -1;
	}

	if (!only_exisitng)
	{
		// ignore -> => >= <= >>= <<= << operators checking
		int tmp = text.safe_at(pos);
		if (tmp == '>')
		{
			wchar_t ch_next = text.safe_at(pos + 1);
			if (ch_next == '=') // >=
				return -1;

			wchar_t ch_prev = text.safe_at(pos - 1);
			if (ch_prev == '-' || ch_prev == '=') // -> and =>
				return -1;
		}
		else if (tmp == '<')
		{
			wchar_t ch_next = text.safe_at(pos + 1);
			if (ch_next == '<' || ch_next == '=')
				return -1; // << and <= and <<=

			wchar_t ch_prev = text.safe_at(pos - 1);
			if (ch_prev == '<') // <<= and <<
				return -1;
		}

		int op_pos = text.find_matching_brace(pos, filter, min_pos, max_pos);
		if (op_pos != -1)
		{
			int ch = text.safe_at(op_pos);
			if (ch == '<' || ch == '>')
			{
				if (!is_template(ch == '<' ? op_pos : pos, '<'))
					return -1;
			}
			else
			{
				// has lock!
				set_matching_brace(pos, op_pos);
			}
			return op_pos;
		}
	}

	return -1;
}

LPCWSTR VASmartSelect::SSContextBase::get_operator(int at_pos, int* spos /*= nullptr*/, int* epos /*= nullptr*/,
                                                   OpNfo* info /*= nullptr*/)
{
	if (info)
		*info = SSParse::OpNfo();

	auto nfo = get_operator_info(at_pos, spos, epos);
	if (nfo)
	{
		if (info)
			*info = *nfo;

		return nfo->op;
	}
	return nullptr;
}

LPCWSTR VASmartSelect::SSContextBase::get_operator_or_empty(int at_pos, int* spos /*= nullptr*/,
                                                            int* epos /*= nullptr*/)
{
	auto op = get_operator(at_pos);
	return op ? op : L"";
}

bool VASmartSelect::SSContextBase::is_string_literal(int pos) const
{
	if (pos >= 0 && pos < (int)ch_states.size())
		return ch_states[pos].HasAnyBitOfMask(SSParse::CharState::chs_string_mask);
	return false;
}

std::function<bool(wchar_t, int)> VASmartSelect::SSContextBase::make_ang_filter(bool type_detection /*= false*/)
{
	return [this, type_detection](wchar_t ch, int pos) { return is_template(pos, ch, type_detection); };
}

VASmartSelect::Parsing::SharedWStr::CharFilter VASmartSelect::SSContextBase::make_code_filter() const
{
	return [this](int pos, wchar_t& ch) -> bool {
		auto state = ch_states[pos];

		if (state.HasAnyBitOfMask((SSParse::CharState)(SSParse::chs_no_code | SSParse::chs_directive)))
		{
			if (state.Contains(SSParse::chs_directive))
			{
				ch = '#';
				return true;
			}
			else if (state.HasAnyBitOfMask(SSParse::chs_string_mask))
			{
				if (ch == '"' || ch == '\'')
					return true;

				if (state.Contains(SSParse::chs_verbatim_str))
					ch = '\'';
				else
					ch = '"';

				return true;
			}

			ch = ' '; // instead of comment/literal/continuation return space
		}

		return true;
	};
}

bool VASmartSelect::SSContextBase::pos_buffer_to_wide(ULONG va_pos, int& vs_pos) const
{
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
		return false;

	long line, col;
	ed->PosToLC((LONG)va_pos, line, col);

	ULONG pos;
	if (lines->PosFromLineAndColumn(pos, (ULONG)line, (ULONG)col))
	{
		vs_pos = (int)pos;
		return true;
	}

	return false;
}

bool SSContextBase::update_needed()
{
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
		return false;

	if (ed_ptr != ed.get() || prev_CurPos != ed->CurPos())
		return true;

	BOOL forceBuff = ed->m_bufState == CTer::BUF_STATE_CLEAN ? TRUE : FALSE;
	WTString fileText(ed->GetBuf(forceBuff));
	return buffer_hash != fileText.hash();
}

END_VA_SMART_SELECT_NS

void CleanupSmartSelectStatics()
{
	bool ss_active =
	    gCurrExecCmd &&
	    (gCurrExecCmd == icmdVaCmd_RefactorModifyExpression || gCurrExecCmd == icmdVaCmd_SmartSelectShrinkBlock ||
	     gCurrExecCmd == icmdVaCmd_SmartSelectExtendBlock || gCurrExecCmd == icmdVaCmd_SmartSelectShrink ||
	     gCurrExecCmd == icmdVaCmd_SmartSelectExtend);

	if (!ss_active)
	{
		VASmartSelect::CurrentBlocks().clear();
		VASmartSelect::SSContext::Get().reset();
		VASmartSelect::CleanModifyExpr();
	}
}
