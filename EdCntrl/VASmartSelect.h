#ifndef VASmartSelect_h__
#define VASmartSelect_h__

#pragma once

#include <string>
#include <functional>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <memory>
#include "VASmartSelect_Parsing.h"

#define BEGIN_VA_SMART_SELECT_NS                                                                                       \
	namespace VASmartSelect                                                                                            \
	{
#define END_VA_SMART_SELECT_NS }
#define USING_VA_SMART_SELECT_NS using namespace VASmartSelect;

class ArgToolTipEx;
class EdCnt;

BEGIN_VA_SMART_SELECT_NS

class Block;

extern const bool IN_DEBUG;

enum class SelectionMode
{
	LeftToRight,       // caret ends on right/bottom side
	RightToLeft,       // caret ends on left/top side
	PreferRightToLeft, // if both side changed, used is RightToLeft
	PreferLeftToRight, // if both side changed, used is LeftToRight
	Automatic          // by most significant change
};

class Settings
{
  public:
	enum class Allowed
	{
		inner = 0x01,
		scope = 0x02,
		comments = 0x04,

		trimmed = 0x10,
		lines = 0x20,

		all = 0xFF
	};

	int preview_latency = 50;
	bool preview_zoom_to_fit = true;
	bool preview_scroll_to_top = true;

	int menu_fade_duration = 200;
	BYTE menu_active_min_alpha = 64;
	BYTE menu_active_max_alpha = 240;
	BYTE menu_inactive_min_alpha = 64;
	BYTE menu_inactive_max_alpha = 190;

	SelectionMode selection_mode = SelectionMode::PreferLeftToRight;

	bool block_allow_modifier = true;
	bool block_allow_directive = true;
	bool block_allow_declaration = true;
	bool block_include_header = true;

	Allowed allow_in_menu = Allowed::all;
	Allowed allow_in_extend_shrink = (Allowed)((DWORD)Allowed::all & ~(DWORD)Allowed::trimmed);
	Allowed allow_in_block = (Allowed)((DWORD)Allowed::all & ~(DWORD)Allowed::trimmed);

	bool select_partial_line = true;
	std::shared_ptr<ArgToolTipEx> tool_tip;

	bool MenuIsAllowed(Allowed what) const
	{
		return ((DWORD)allow_in_menu & (DWORD)what) == (DWORD)what;
	}

	bool ExtendShrinkIsAllowed(Allowed what) const
	{
		return ((DWORD)allow_in_extend_shrink & (DWORD)what) == (DWORD)what;
	}

	bool BlockIsAllowed(Allowed what) const
	{
		return ((DWORD)allow_in_block & (DWORD)what) == (DWORD)what;
	}

	CPoint menu_pos = CPoint(INT_MAX, INT_MAX);

	void InvalidateMenuPos()
	{
		menu_pos.SetPoint(INT_MAX, INT_MAX);
	}
	bool MenuPosIsValid() const
	{
		return menu_pos.x != INT_MAX && menu_pos.y != INT_MAX;
	}
	CPoint& MenuPos()
	{
		return menu_pos;
	}
	const CPoint& MenuPos() const
	{
		return menu_pos;
	}
	bool GetMenuPosIfValid(CPoint& pt) const
	{
		if (MenuPosIsValid())
		{
			pt = menu_pos;
			return true;
		}
		return false;
	}

  private:
	void OnEdCntDestroy(const EdCnt* ed);
	friend class EdCnt;
};

size_t GenerateBlocks(DWORD cmdId = 0);
std::vector<Block>& CurrentBlocks();
LPCWSTR GetCommandName(DWORD cmdId);
BOOL IsCommandReady(DWORD cmdId);
Settings& ActiveSettings();
void HideToolTip();

bool CanModifyExpr();
bool RunModifyExpr();
void CleanModifyExpr();

struct AutoCleanup
{
	std::vector<std::function<void()>> actions;
	~AutoCleanup();
};

class Block
{
  public:
	enum class Portion
	{
		OuterText,        // whole block (default)
		Scope,            // inner text including braces
		Prefix,           // text between start and opening brace
		Suffix,           // text between closing brace and end
		InnerText,        // text between braces
		OuterTextNoChild, // whole block w/o children
		Group             // all including related siblings
	};

	enum class Mode
	{
		Normal,
		Trimmed,
		TrimmedWOSemicolon,
		Lines,
		LinesOrNonSpace,
		LinesAfterTrim, // trim, then select lines
		TrimAfterLines  // find lines, then trim
	};

	struct Context
	{
		Portion portion = Portion::OuterText;
		Mode mode = Mode::Normal;
	};

	typedef std::function<bool(const Context&)> ApplyFnc;

  private:
	std::wstring m_name;

	Context m_context;
	ApplyFnc m_apply;
	ApplyFnc m_isAvailable;

  public:
	std::vector<Block> Blocks;

	Block(const wchar_t* name, ApplyFnc apply = ApplyFnc())
	{
		SetName(name).SetFnc(apply);
	}

	Block& Set(const wchar_t* name, ApplyFnc apply = ApplyFnc())
	{
		return SetName(name).SetFnc(apply);
	}

	Block& SetName(const wchar_t* name = L"")
	{
		m_name.assign(name ? name : L"");
		return *this;
	}

	Block& SetFnc(ApplyFnc apply = ApplyFnc())
	{
		m_apply = apply;
		return *this;
	}

	Block& SetIsAvailableFnc(ApplyFnc isAvailable = ApplyFnc())
	{
		m_isAvailable = isAvailable;
		return *this;
	}

	Block& SetPortion(Portion portion)
	{
		m_context.portion = portion;
		return *this;
	}

	Block& SetMode(Mode mode)
	{
		m_context.mode = mode;
		return *this;
	}

	const std::wstring& GetName()
	{
		return m_name;
	}

	Block& AddItem(LPCWSTR name)
	{
		Blocks.push_back(Block(name));
		return Blocks.back();
	}

	const Context& Ctx() const
	{
		return m_context;
	}

	bool Apply();
	bool IsAvailable();

	static Block* FindByName(LPCWSTR name);
};

class BlockBuilder
{
	std::vector<Block>& m_blocks;

  public:
	BlockBuilder(std::vector<Block>& blocks) : m_blocks(blocks)
	{
	}

	Block& AddItem(size_t lvl, LPCWSTR name, bool fix_ampersands)
	{
		if (lvl == 0)
		{
			CStringW wstr = name;
			wstr.Replace(L"\t", L" ");
			wstr.Replace(L"\r\n", L" ");
			wstr.Replace(L"\r", L" ");
			wstr.Replace(L"\n", L" ");

			if (!fix_ampersands)
				m_blocks.push_back(Block((LPCWSTR)wstr));
			else
			{
				wstr.Replace(L"&", L"&&");
				m_blocks.push_back(Block((LPCWSTR)wstr));
			}

			return m_blocks.back();
		}
		else
		{
			Block& b = m_blocks.back();

			for (size_t i = 1; i < lvl; i++)
				b = b.Blocks.back();

			CStringW wstr = name;
			wstr.Replace(L"\t", L" ");
			wstr.Replace(L"\r\n", L" ");
			wstr.Replace(L"\r", L" ");
			wstr.Replace(L"\n", L" ");

			if (!fix_ampersands)
				b.Blocks.push_back(Block((LPCWSTR)wstr));
			else
			{
				wstr.Replace(L"&", L"&&");
				b.Blocks.push_back(Block((LPCWSTR)wstr));
			}

			return b.Blocks.back();
		}
	}

	Block& AddRootSeparator()
	{
		return AddItem(0, L"-S-", false);
	}
	Block& AddSeparator()
	{
		return AddItem(1, L"-S-", false);
	}
	Block& AddSubSeparator()
	{
		return AddItem(2, L"-S-", false);
	}
	Block& AddSubSubSeparator()
	{
		return AddItem(3, L"-S-", false);
	}

	Block& AddRoot(LPCWSTR name, bool fix_ampersands = false)
	{
		return AddItem(0, name, fix_ampersands);
	}
	Block& AddItem(LPCWSTR name, bool fix_ampersands = true)
	{
		return AddItem(1, name, fix_ampersands);
	}
	Block& AddSubItem(LPCWSTR name, bool fix_ampersands = true)
	{
		return AddItem(2, name, fix_ampersands);
	}
	Block& AddSubSubItem(LPCWSTR name, bool fix_ampersands = true)
	{
		return AddItem(3, name, fix_ampersands);
	}
};

using namespace Parsing;

struct SSContextBase
{
	std::mutex pos_cache_mtx;     // mutex used for all following maps and sets
	std::map<int, int> brace_map; // map of pairs of braces () {} [] <>
	std::set<int> operator_angs;  // set of < > which are confirmed to not be templates

	TextLines::Ptr lines;
	SharedWStr text;
	EnumMaskList<CharState> ch_states;
	std::vector<int> ch_state_borders;
	std::vector<ConditionalBlock::Ptr> cb_vec;
	std::vector<int> dir_starts_vec;
	std::vector<std::tuple<int, int, char>> num_literals;
	std::map<int, int> cs_interp;

	bool is_cs = false;
	bool is_cpp = false;
	bool is_xml = false;
	bool is_initialized = false;
	bool is_preparing_tree = false; // for SS, but required for debug purposes

	bool init_braces = false;
	DWORD command = 0;

	uint kCurPos = 0;
	int caret_pos = 0;
	uint prev_CurPos = 0;
	uint buffer_hash = 0;
	PVOID ed_ptr = nullptr; // just for checking!

	bool update_needed();

	bool pos_buffer_to_wide(ULONG va_pos, int& vs_pos) const;
	bool pos_wide_to_buffer(int vs_pos, ULONG& va_pos);
	bool is_comment_or_string(int pos);
	// only to prepare context!
	bool cache_template_char(int pos, wchar_t ch, bool OK_if_match_found, bool use_IsTemplateCls,
	                         bool va_type_detection);
	DTypePtr get_DType(int pos);
	SharedWStr::CharFilter make_code_filter() const;
	std::function<bool(wchar_t, int)> make_ang_filter(bool type_detection = false);
	LPCWSTR get_operator_or_empty(int at_pos, int* spos = nullptr, int* epos = nullptr);
	LPCWSTR get_operator(int at_pos, int* spos = nullptr, int* epos = nullptr, OpNfo* info = nullptr);
	int get_matching_brace(int pos, bool only_exisitng, SharedWStr::CharFilter filter = nullptr, int min_pos = -1,
	                       int max_pos = -1);
	void set_matching_brace(int open_pos, int close_pos);
	bool is_operator(int at_pos, LPCWSTR op);
	bool is_template(int pos, wchar_t ch = '\0', bool type_detection = false);
	bool is_string_literal(int pos) const;
	bool is_type(int pos);

	bool is_extend_cmd() const;
	bool is_shrink_cmd() const;
	bool is_extend_block_cmd() const;
	bool is_shrink_block_cmd() const;
	bool is_modify_expr_cmd() const;

	const OpNfo* get_operator_info(int at_pos, int* spos = nullptr, int* epos = nullptr);
};

END_VA_SMART_SELECT_NS

void CleanupSmartSelectStatics();

#endif // VASmartSelect_h__
