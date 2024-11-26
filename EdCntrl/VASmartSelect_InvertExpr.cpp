#include "stdafxed.h"
#include "VASmartSelect.h"
#include "VASmartSelect_Parsing.h"
#include "DevShellService.h"
#include "RegKeys.h"
#include "PROJECT.H"
#include "EDCNT.H"
#include <iosfwd>
#include "VAAutomation.h"
#include "VaService.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "..\common\ScopedIncrement.h"
#include "DllNames.h"

#define ENABLE_LOGGING 0

#if ENABLE_LOGGING
#define INV_EXP_LOG(x)                                                                                                 \
	if (!gTestsActive)                                                                                                 \
	log << x
#else
#define INV_EXP_LOG(x)
#endif

BEGIN_VA_SMART_SELECT_NS
namespace SSParse = VASmartSelect::Parsing;

struct InvertExprSettings
{
	UINT flags = tagSettings::ModExpr_Default;
	bool check_only = false;

	bool is_cs() const
	{
		return !!(flags & tagSettings::ModExpr_IsCSharp);
	}
	bool invert_expression() const
	{
		return !!(flags & tagSettings::ModExpr_Invert);
	}
	bool transform_expression() const
	{
		return !invert_expression();
	}

	bool invert_relational_operators() const
	{
		return !!(flags & tagSettings::ModExpr_ToggleRelOps);
	}
	bool invert_logical_operators() const
	{
		return !!(flags & tagSettings::ModExpr_ToggleLogOps);
	}
	bool remove_redundant_not_operator() const
	{
		return !!(flags & tagSettings::ModExpr_NotOpRemoval);
	}
	bool remove_redundant_parentheses_with_not() const
	{
		return true;
	}
	bool remove_redundant_parentheses_in_fnc_calls() const
	{
		return false;
	}
	bool remove_redundant_parentheses() const
	{
		return !!(flags & tagSettings::ModExpr_ParensRemoval);
	}

	bool is_parens_removal_only() const
	{
		return remove_redundant_parentheses() && transform_expression() && !invert_relational_operators() &&
		       !invert_logical_operators() && !remove_redundant_not_operator();
	}
} inv_exp_settings;

struct IApplyEdit
{
	virtual ~IApplyEdit() = default;
	virtual void apply_edit(int pos, int len, LPCWSTR wstr, bool tail) = 0;
};

struct ExprRng;

int exprRangesCount = 0;
std::vector<std::weak_ptr<ExprRng>> exprRanges;

// #InvertExpression01_rng_info
struct ExprRng : std::enable_shared_from_this<ExprRng>
{
	using SPtr = std::shared_ptr<ExprRng>;

	enum class invertedBy
	{
		None,

		ToggledOps,    // A || B => !A && !B
		NotToggledOps, // used !( ) instead of toggling

		RemovedNot,             // !foo => foo
		RemovedNotWrapped,      // !(expr) => expr
		RemovedNotInner,        // (!foo) => (foo)
		RemovedNotWrappedInner, // (!(a||b)) => (a||b)

		AddedNot,             // foo => !foo
		AddedNotWrapped,      // expr => !(expr)
		AddedNotInner,        // (foo) => (!foo)
		AddedNotWrappedInner, // (a||b) => (!(a||b))
		ReplacedSymbol,       // TRUE -> FALSE
	};

	enum type
	{
		t_empty,
		t_space,
		t_brace,
		t_fnc_brace, // special case for function foo(...)
		t_idx_brace, // special case for indexer foo[...]
		t_tpl_brace, // special case for template foo<...>
		t_symbol,
		t_type,
		t_string,
		t_number,
		t_operator,

		// groups
		t_group,
		t_log_expr_group,
		t_rel_expr_group,
		t_ternary_group,
	};

	class Edit
	{
	  public:
		int id;
		int pos;
		int len;
		std::wstring str;
		bool tail;

		Edit(int _pos, int _len, LPCWSTR _wstr, bool _tail) : pos(_pos), len(_len), str(_wstr), tail(_tail)
		{
			static int _next_id = 0;
			id = _next_id++;
		}
	};

	static bool is_cs()
	{
		return inv_exp_settings.is_cs();
	}

	static LPCWSTR invert_op(LPCWSTR op)
	{
		if (wcscmp(op, L"==") == 0)
			return L"!=";
		if (wcscmp(op, L"!=") == 0)
			return L"==";
		if (wcscmp(op, L"<=") == 0)
			return L">";
		if (wcscmp(op, L">=") == 0)
			return L"<";
		if (wcscmp(op, L"||") == 0)
			return L"&&";
		if (wcscmp(op, L"&&") == 0)
			return L"||";
		if (wcscmp(op, L">") == 0)
			return L"<=";
		if (wcscmp(op, L"<") == 0)
			return L">=";
		return op;
	};

	static bool try_invert_stack(IApplyEdit* invert, std::stack<ExprRng::SPtr>& work_stack, bool invert_ops,
	                             bool remove_not, bool add_not)
	{
		int inverted_count = 0;

		bool success = false;
		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			if (top->children.empty())
				inverted_count += top->try_invert(invert, invert_ops, remove_not, add_not) ? 1 : 0;
			else
			{
				// in relational parent process only relational operators
				bool is_relational = false;
				for (auto& rng : top->children)
				{
					if (rng->is_operator(SSParse::OpType::Relational))
					{
						is_relational = true;
						break;
					}
				}

				success = true;

				// check if either range is nested logical
				// or range is successfully invertible

				std::set<ExprRng::SPtr> nested;
				for (auto& rng : top->children)
				{
					// is nested logical expression?
					if (rng->is_logical_expr())
					{
						nested.insert(rng);
						continue;
					}

					// in relational parent process only relational operators
					if (is_relational && !rng->is_operator(SSParse::OpType::Relational))
						continue;

					// is invertible?
					if (rng->try_invert(nullptr, invert_ops, remove_not, add_not))
						continue;

					success = false;
					break;
				}

				if (!success)
					// not a complex expression, invert without inverting operators
					inverted_count += top->try_invert(invert, false, remove_not, add_not) ? 1 : 0;
				else
				{
					// push nested logical expressions
					for (auto& rng : nested)
						work_stack.push(rng);

					std::vector<ExprRng::SPtr> tmp(top->children);

					for (auto& rng : tmp)
					{
						// skip nested expressions,
						// they are already pushed into stack
						if (nested.find(rng) != nested.end())
							continue;

						// in relational parent process only relational operators
						if (is_relational && !rng->is_operator(SSParse::OpType::Relational))
							continue;

						// invert current expression
						if (rng->try_invert(invert, invert_ops, remove_not, add_not))
						{
							inverted_count++;
							continue;
						}

						// failed!!!
						return false;
					}
				}
			}
		}

		return !!inverted_count;
	}

	static bool try_invert_complex(IApplyEdit* invert, ExprRng::SPtr complex, bool invert_ops, bool remove_not,
	                               bool add_not)
	{
		if (complex->children.empty())
			return false;

		// get last equal child from the set
		auto x = complex->all_single_children();
		if (!x.empty())
			complex = x.back();

		std::stack<ExprRng::SPtr> all_stack;
		std::stack<ExprRng::SPtr> rel_ops_stack;

		for (auto it = complex->children.rbegin(); it != complex->children.rend(); ++it)
		{
			all_stack.push(*it);

			// if any relational, invert only relational operators
			if ((*it)->is_operator(SSParse::OpType::Relational))
				rel_ops_stack.push(*it);
		}

		// only invert relational operators
		if (!rel_ops_stack.empty())
			return ExprRng::try_invert_stack(invert, rel_ops_stack, true, false, false);

		return ExprRng::try_invert_stack(invert, all_stack, true, true, true);
	}

	bool is_wrapped_in_parens()
	{
		if (prev || next)
			return false;

		if (parent)
		{
			return parent->is_brace(L"(") ||
			       // if we are in group with single child, test parent...
			       (parent->children.size() == 1 && parent->is_wrapped_in_parens());
		}

		// use bottom elements (low level)
		auto bf = bottom_first();
		auto bl = bottom_last();

		return bf && bf->bottom_prev && bf->bottom_prev->text == L"(" && bl && bl->bottom_next &&
		       bl->bottom_next->text == L")";
	};

	bool is_casting() const
	{
		// standard C casting (type)
		if (is_brace(L"(") && children.size() == 1)
			return children[0]->is_type();

		if (!is_cs())
		{
			// c++ functional casting like bool(value)
			if (is_type() && next && next->is_brace(L"("))
				return true;

			// from brace...
			if (is_brace(L"(") && prev && prev->is_type())
				return true;

			// c++ ###_cast<type>(...)
			// also DYNAMIC_DOWNCAST(type)
			// or anything with "cast" or "CAST" followed by <> or ()
			if (is_symbol() && StrStrIW(def.c_str(), L"cast") && next && next->is_brace(L"<("))
				return true;

			// from brace...
			if (is_brace(L"<(") && prev && prev->is_symbol() && StrStrIW(prev->def.c_str(), L"cast"))
				return true;
		}

		return false;
	}

	bool InvertRule_RemoveNot(IApplyEdit* invert, bool only_added, bool equal_childs = true)
	{
		if (inv == invertedBy::AddedNot && !text.empty() && text[0] == '!')
		{
			invert->apply_edit(s_pos, 1, L"", false);
			inv = invertedBy::RemovedNot;
			return true;
		}

		if (inv == invertedBy::AddedNotWrapped && text.length() > 2 && text[0] == '!' && text[1] == '(' &&
		    text[text.length() - 1] == ')')
		{
			invert->apply_edit(e_pos, 1, L"", false);
			invert->apply_edit(s_pos, 2, L"", false);
			inv = invertedBy::RemovedNotWrapped;
			return true;
		}

		if (inv == invertedBy::AddedNotInner && text.length() > 2 && text[1] == '!')
		{
			invert->apply_edit(s_pos + 1, 1, L"", false);
			inv = invertedBy::RemovedNotInner;
			return true;
		}

		if (inv == invertedBy::AddedNotWrappedInner && text.length() > 4 && text[0] == '(' && text[1] == '!' &&
		    text[2] == '(' && text[text.length() - 1] == ')' && text[text.length() - 2] == ')')
		{
			invert->apply_edit(e_pos - 1, 1, L"", false);
			invert->apply_edit(s_pos + 1, 2, L"", false);
			inv = invertedBy::RemovedNotWrappedInner;
			return true;
		}

		if (only_added && equal_childs)
		{
			for (const auto& eq_ch : all_single_children())
				if (eq_ch->InvertRule_RemoveNot(invert, true, false))
					return true;
		}

		if (only_added || is_operator() || is_expression(true))
			return false;

		if (!only_added && equal_childs)
		{
			for (const auto& eq_ch : all_single_children())
				if (eq_ch->InvertRule_RemoveNot(invert, false, false))
					return true;
		}

		// RULE1 - remove ! operator
		if (!children.empty())
		{
			for (auto& ch : children)
			{
				if (ch->is_operator(L"!"))
				{
					if (!invert)
						return true;

					// remove parentheses?
					if (inv_exp_settings.remove_redundant_parentheses_with_not() &&
					    inv_exp_settings.remove_redundant_parentheses() && ch->next && ch->next->is_brace(L"(") &&
					    (is_wrapped_in_parens()            // is wrapped tightly like (!(this)) ?
					     || !ch->next->is_expression(true) // not an expression
					     ))
					{
						// remove parentheses
						invert->apply_edit(ch->next->e_pos, 1, L"", false);
						invert->apply_edit(ch->next->s_pos, 1, L"", false);
						ch->next->t = t_group; // change type, it is no longer brace type

						inv = invertedBy::RemovedNotWrapped;
					}

					// delete NOT operator
					invert->apply_edit(ch->def_pos, 1, L"", false);

					if (inv == invertedBy::None)
						inv = invertedBy::RemovedNot;

					return true;
				}

				if (ch->is_casting())
					continue;

				break;
			}
		}

		return false;
	}

	bool InvertRule_InvertOp(IApplyEdit* invert)
	{
		using namespace SSParse;

		if (is_ternary_group() || is_casting() || is_type())
			return false;

		std::vector<ExprRng::SPtr> rngs;

		if (is_operator())
			rngs.push_back(shared_from_this());

		if (rngs.empty() && is_log_expr_group() && !is_function_call() && !children.empty())
		{
			std::vector<ExprRng::SPtr> invertibles;

			for (auto& rng : children)
			{
				if (!rng->is_function_call() && !rng->is_function_brace() && rng->is_invertible_expression())
				{
					invertibles.push_back(rng);
				}
			}

			if (invertibles.size() == 1)
			{
				auto front = invertibles.front();

				if (front->is_expression(true) && try_invert_complex(invert, front, true, true, true))
					return true;

				if (front->InvertRule_InvertOp(invert))
					return true;
			}
		}

		// RULE2 - invert an operator
		for (auto& ch : rngs.empty() ? children : rngs)
		{
			if (ch->is_operator())
			{
				if (ch->def == invert_op(ch->def.c_str()))
					continue;

				if (is_operator({L"&&", L"||"}) && !inv_exp_settings.invert_logical_operators())
					return false;

				if (!invert)
					return true;

				// this is special case where we wrap into !( ) while we are supposed to toggle operator
				// it is because we would normally toggle, but as it is disabled, we do wrap
				if (ch->is_operator(OpType::Relational) && !inv_exp_settings.invert_relational_operators())
				{
					int prec = get_operator_precedence();

					auto _prev = ch->prev_significant_operator(prec);
					if (_prev)
						_prev = _prev->next;
					else
						_prev = ch->first_sibling();

					auto _next = ch->next_significant_operator(prec);
					if (_next)
						_next = _next->prev;
					else
						_next = ch->last_sibling();

					// if we have something to wrap...
					if (_prev && _next)
					{
						invert->apply_edit(_next->e_pos + 1, 0, L")", true);
						invert->apply_edit(_prev->s_pos, 0, L"!(", false);

						inv = invertedBy::NotToggledOps;

						return true;
					}

					return false;
				}

				// when inverting A || B && C, && has precedence,
				// so we need to parse it as A || (B && C) and the
				// result must be !A && (!B || !C)

				bool wrap = false;

				if (ch->is_operator(L"&&"))
				{
					int prec = get_operator_precedence();

					// only wrap if || is applied before or after

					auto _prev = ch->prev_significant_operator(prec);
					if (_prev)
					{
						wrap = _prev->is_operator(L"||");
						_prev = _prev->next;
					}
					else
					{
						_prev = ch->first_sibling();
					}

					auto _next = ch->next_significant_operator(prec);
					if (_next)
					{
						wrap = wrap || _next->is_operator(L"||");
						_next = _next->prev;
					}
					else
					{
						_next = ch->last_sibling();
					}

					// if we have something to wrap...
					if (_prev && _next && wrap)
					{
						invert->apply_edit(_next->e_pos + 1, 0, L")", true);
						invert->apply_edit(ch->def_pos, (int)ch->def.length(), invert_op(ch->def.c_str()), false);
						invert->apply_edit(_prev->s_pos, 0, L"(", false);

						inv = invertedBy::ToggledOps;

						return true;
					}
				}

				invert->apply_edit(ch->def_pos, (int)ch->def.length(), invert_op(ch->def.c_str()), false);

				inv = invertedBy::ToggledOps;

				return true;
			}
		}

		return false;
	}

	bool InvertRule_AddNot(IApplyEdit* invert)
	{
		// - can't negate operator by NOT: like !&&
		// - can't negate type casting by NOT: like (!bool)
		if (is_operator() || is_casting() || is_type())
			return false;

		// below always succeeds!
		if (!invert)
			return true;

		// try invert last single nested child in braces
		if (!children.empty())
		{
			auto eq = all_single_children(true, false);
			if (!eq.empty() && !eq.back()->should_wrap_inner_text())
				return eq.back()->InvertRule_AddNot(invert);
		}

		if (is_symbol())
		{
			inv = invertedBy::ReplacedSymbol;

			if (def == L"true")
				invert->apply_edit(s_pos, (int)def.length(), L"false", false);
			else if (def == L"TRUE")
				invert->apply_edit(s_pos, (int)def.length(), L"FALSE", false);
			else if (def == L"false")
				invert->apply_edit(s_pos, (int)def.length(), L"true", false);
			else if (def == L"FALSE")
				invert->apply_edit(s_pos, (int)def.length(), L"TRUE", false);
			else
			{
				// insert ! before symbol
				invert->apply_edit(s_pos, 0, L"!", false);
				inv = invertedBy::AddedNot;
			}

			return true;
		}

		if (is_brace(L"("))
		{
			if (is_function_brace())
			{
				// wrap inner text with !(...)

				if (children.empty())
					return false;

				auto srng = children.empty() ? this : children.front().get();
				auto erng = children.empty() ? this : children.back().get();

				if (should_wrap_inner_text())
				{
					// wrap multiple
					invert->apply_edit(erng->e_pos + 1, 0, L")", true); // first, so spos don't need to be recalculated
					invert->apply_edit(srng->s_pos, 0, L"!(", false);
					inv = invertedBy::AddedNotWrappedInner;
				}
				else
				{
					invert->apply_edit(srng->s_pos, 0, L"!", false);
					inv = invertedBy::AddedNotInner;
				}
			}
			else
			{
				// insert ! before (...)
				invert->apply_edit(s_pos, 0, L"!", false);
				inv = invertedBy::AddedNot;
			}

			return true;
		}

		if (!is_expression(true))
		{
			invert->apply_edit(s_pos, 0, L"!", false);
			inv = invertedBy::AddedNot;
			return true;
		}

		// wrap with !(...)
		invert->apply_edit(e_pos + 1, 0, L")", true);
		invert->apply_edit(s_pos, 0, L"!(", false);

		inv = invertedBy::AddedNotWrapped;

		return true;
	}

	// fills the vector with itself and children of range while always
	// parent is followed by all children..
	void get_all_ranges(std::vector<ExprRng::SPtr>& ranges, bool include_self = true)
	{
		std::stack<ExprRng::SPtr> push_stack;
		push_stack.push(shared_from_this());

		// fill the vector
		while (!push_stack.empty())
		{
			auto top = push_stack.top();
			push_stack.pop();

			if (include_self || top.get() != this)
				ranges.push_back(top);

			for (auto& ch : top->children)
				push_stack.push(ch);
		}
	}

	bool remove_redundant_parentheses(IApplyEdit* invert)
	{
		if (!in_expr)
			return false;

		if (!inv_exp_settings.remove_redundant_parentheses_in_fnc_calls() && is_function_call())
			return false;

		std::vector<ExprRng::SPtr> rngs;
		get_all_ranges(rngs);

		std::vector<ExprRng::SPtr> braces;
		for (auto& ch : rngs)
			if (ch->is_brace(L"("))
				braces.push_back(ch);

		std::vector<ExprRng::SPtr> redundant;

		// process from parent to child
		for (const std::shared_ptr<VASmartSelect::ExprRng>& top : braces)
		{
			if (top->is_redundant_brace())
			{
				redundant.push_back(top);
			}
		}

		for (const auto& top : redundant)
		{
			if (top->text.front() == L'(')
			{
				// remove redundant braces
				invert->apply_edit(top->e_pos, 1, L"", false);
				invert->apply_edit(top->s_pos, 1, L"", false);
			}
			else if (top->text.front() == L'!' && top->text.length() > 1 && top->text[1] == L'(')
			{
				// remove redundant closing brace
				invert->apply_edit(top->e_pos, 1, L"", false);

				// remove redundant brace after ! operator
				invert->apply_edit(top->s_pos + 1, 1, L"", false);
			}
		}

		return !redundant.empty();
	}

	bool try_invert(IApplyEdit* invert, bool invert_ops, bool remove_not, bool add_not)
	{
		// 		if (inv != invertedBy::None && !invert_inverted)
		// 			return false;

		// 		if (!inv_exp_settings.invert_relational_operators() &&
		// 			parent && parent->is_rel_expr_group())
		// 			return false;

		// Rules:
		// ---------------------------------
		// 1) try remove ! if possible
		if (remove_not)
		{
			if (!inv_exp_settings.remove_redundant_not_operator())
			{
				// we can always remove ! we have added!
				if (InvertRule_RemoveNot(invert, true))
					return true;
			}
			else if (InvertRule_RemoveNot(invert, false))
				return true;
		}

		// 2) try invert comparison and assignment operators
		if (invert_ops &&
		    (inv_exp_settings.invert_relational_operators() || inv_exp_settings.invert_logical_operators()) &&
		    InvertRule_InvertOp(invert))
			return true;

		// 3) add ! or wrap in !(...)
		if (add_not && InvertRule_AddNot(invert))
			return true;

		return false;
	}

	bool is_empty() const
	{
		return t == t_empty;
	}
	bool is_space() const
	{
		return t == t_space;
	}
	bool is_brace() const
	{
		return t == t_brace || t == t_fnc_brace || t == t_idx_brace || t == t_tpl_brace;
	}
	bool is_symbol() const
	{
		return t == t_symbol || t == t_type;
	}
	bool is_number() const
	{
		return t == t_number;
	}
	bool is_string() const
	{
		return t == t_string;
	}
	bool is_operator() const
	{
		return t == t_operator;
	}
	bool is_group() const
	{
		return t == t_group;
	}
	bool is_ternary_group() const
	{
		return t == t_ternary_group;
	}
	bool is_log_expr_group() const
	{
		return t == t_log_expr_group;
	}
	bool is_rel_expr_group() const
	{
		return t == t_rel_expr_group;
	}
	bool is_expr_group() const
	{
		return is_log_expr_group() || is_log_expr_group();
	}
	bool is_type() const
	{
		return t == ExprRng::t_type;
	}
	bool is_function_brace() const
	{
		return t == t_fnc_brace;
	}
	bool is_indexer_brace() const
	{
		return t == t_idx_brace;
	}
	bool is_template_brace() const
	{
		return t == t_tpl_brace;
	}

	bool is_any_group() const
	{
		return is_group() || is_ternary_group() || is_expr_group();
	}

	bool is_function_call() const
	{
		if (!children.empty())
		{
			switch (children.size())
			{
			case 1:
				return children.front()->s_pos == s_pos && children.front()->e_pos == e_pos &&
				       children.front()->is_function_call();

			case 2:
				return children[0]->is_symbol() && children[1]->is_function_brace();
			}
		}

		return false;
	}

	bool is_ternary_group_or_child() const
	{
		if (is_ternary_group())
			return true;

		auto p = parent;
		while (p)
		{
			if (p->is_ternary_group())
				return true;

			p = p->parent;
		}

		return false;
	}

	bool is_operator(LPCWSTR op) const
	{
		return is_operator() && def_is(op);
	}
	bool is_operator(const std::initializer_list<LPCWSTR>& ops) const
	{
		return is_operator() && def_is(ops);
	}

	bool is_operator(SSParse::OpType typeMask) const
	{
		if (!is_operator())
			return false;

		return !!SSParse::Parser::GetOperatorInfo(def.c_str(), is_cs(), typeMask);
	}

	bool is_child_of(const ExprRng* possible_parent) const
	{
		auto tmp = parent;
		while (tmp)
		{
			if (tmp.get() == possible_parent)
				return true;

			tmp = tmp->parent;
		}
		return false;
	}

	bool is_parent_of(const ExprRng::SPtr& possible_child) const
	{
		return possible_child && possible_child->is_child_of(this);
	}

	// Counts the instances of equal operators with the definition.
	// If count is more than 1, it is considered confusable,
	// because it could be one or another and in such case
	// we must use surroundings to determine the type of current operator.
	bool is_confusable_operator() const
	{
		if (!is_operator())
			return false;

		int count = 0;

		for (auto& nfo : SSParse::Parser::GetOpNfoVector(is_cs()))
			if (0 == wcscmp(nfo.op, def.c_str()) && ++count > 1)
				return true;

		return false;
	}

	bool is_prefix_operator(std::function<ExprRng::SPtr(int)> at_offset) const
	{
		return is_operator(SSParse::OpType::Prefix) && (!prev || prev->is_operator()) && next &&
		       (next->is_prefix_operator(at_offset) || next->is_block_or_block_start(at_offset));
	}

	bool is_suffix_operator(std::function<ExprRng::SPtr(int)> at_offset) const
	{
		return is_operator(SSParse::OpType::Suffix) && prev && prev->is_block_end(at_offset) &&
		       (!next || next->is_operator());
	}

	bool is_symbol(LPCWSTR op) const
	{
		return is_symbol() && def_is(op);
	}
	bool is_symbol(const std::initializer_list<LPCWSTR>& ops) const
	{
		return is_symbol() && def_is(ops);
	}

	bool is_block_end(std::function<ExprRng::SPtr(int)> at_offset // this is necessary when relations are unset
	) const
	{
		// ...) ...] or symbol
		if (is_symbol() || is_number() || is_string() || is_brace(L"([{"))
			return true;

		// suffix ++ --
		if (is_operator(SSParse::OpType::Suffix))
		{
			auto prev2 = at_offset ? at_offset(-1) : this->prev;
			if (prev2 && (prev2->is_symbol() || prev2->is_number() || prev2->is_brace(L"([{")))
				return true;
		}

		return false;
	}

	bool is_block_or_block_end(std::function<ExprRng::SPtr(int)> at_offset // this is necessary when relations are unset
	) const
	{
		if (is_group() && !children.empty())
			return children.back()->is_block_or_block_end(at_offset);

		return is_block_end(at_offset);
	}

	bool is_block_start(std::function<ExprRng::SPtr(int)> at_offset, // this is necessary when relations are unset
	                    bool brace = true, bool symbol = true, bool ops = true) const
	{
		// (... or symbol
		if ((symbol && (is_symbol() || is_number() || is_string())) || (brace && is_brace(L"(")))
			return true;

		// try find the symbol or (...) after the multiple preceding operators to confirm the validity

		// prefix operators
		if (ops && is_operator(SSParse::OpType::Prefix))
		{
			int offset = 1;
			auto next2 = at_offset ? at_offset(offset) : this->next;

			while (next2 && next2->is_operator(SSParse::OpType::Prefix))
				next2 = at_offset ? at_offset(++offset) : next2->next;

			if (next2 &&
			    ((symbol && (next2->is_symbol() || (!is_operator(L"&") && next2->is_number()) || next2->is_string())) ||
			     (brace && next2->is_brace(L"("))))
				return true;
		}

		return false;
	}

	bool is_block_or_block_start(
	    std::function<ExprRng::SPtr(int)> at_offset, // this is necessary when relations are unset
	    bool brace = true, bool symbol = true, bool ops = true) const
	{
		if (is_group() && !children.empty())
			return children.front()->is_block_or_block_start(at_offset, brace, symbol, ops);

		return is_block_start(at_offset, brace, symbol, ops);
	}

	bool is_brace(LPCWSTR br) const
	{
		if (!is_brace() || def.empty())
			return false;

		for (LPCWSTR ch = br; ch && *ch; ch++)
			if (def[0] == *ch)
				return true;

		return false;
	}

	bool empty() const
	{
		return s_pos > e_pos;
	}

	int get_parens_depth() const
	{
		int depth = 0;
		auto par = parent;
		while (par)
		{
			if (par->is_brace())
				depth++;

			par = par->parent;
		}
		return depth;
	}

	bool is_invertible_expression()
	{
		if (children.empty())
			return false;

		for (const std::shared_ptr<ExprRng>& child : children)
		{
			if (child->is_operator() && (child->def != invert_op(child->def.c_str())))
			{
				return true;
			}
		}

		return false;
	}

	enum class expr_type
	{
		any,
		logical,
		relational
	};

	bool is_logical_expr() const
	{
		return is_expression(false, expr_type::logical);
	}

	bool is_relational_expr() const
	{
		return is_expression(false, expr_type::relational);
	}

	std::vector<ExprRng::SPtr> all_single_children(bool braces = true, bool non_braces = true) const
	{
		std::vector<ExprRng::SPtr> result;

		if (is_brace() ? braces : non_braces)
		{
			int nested_s_pos = is_brace() ? s_pos + 1 : s_pos;
			int nested_e_pos = is_brace() ? e_pos - 1 : e_pos;

			if (children.size() == 1)
			{
				ExprRng::SPtr ch = children.front();
				while (ch->s_pos == nested_s_pos && ch->e_pos == nested_e_pos)
				{
					result.push_back(ch);

					if (ch->children.size() == 1)
					{
						ch = ch->children.front();

						if (ch->is_brace() ? braces : non_braces)
						{
							nested_s_pos = ch->is_brace() ? ch->s_pos + 1 : ch->s_pos;
							nested_e_pos = ch->is_brace() ? ch->e_pos - 1 : ch->e_pos;

							continue;
						}
					}

					break;
				}
			}
		}

		return result;
	}

	bool is_expression(bool check_equal_nested = false, expr_type xtype = expr_type::any) const
	{
		using namespace SSParse;

		if (children.empty())
			return false;

		if (is_ternary_group())
			return xtype == expr_type::any;

		if (check_equal_nested)
		{
			for (const auto& eq_ch : all_single_children())
				if (eq_ch->is_expression(false, xtype))
					return true;
		}

		int i = 0;

		auto at_offset = [&](int offset) -> ExprRng::SPtr {
			int pos = i + offset;

			if (pos >= 0 && pos < (int)children.size())
				return children[(uint)pos];

			return nullptr;
		};

		for (i = 0; i < (int)children.size(); i++)
		{
			auto child = children[(uint)i];

			if (is_cs() && child->is_symbol({L"new", L"as", L"is"}))
				return true;

			if (!is_cs() && child->is_symbol({L"new", L"gcnew"}))
				return true;

			if (child->is_operator())
			{
				// skip operators which don't define expression
				if (child->is_operator((OpType)(USHORT(OpType::Primary) | USHORT(OpType::Suffix))))
					continue;

				if (xtype == expr_type::logical)
				{
					if (child->def_is({L"&&", L"||"}))
						return true;
				}
				else if (xtype == expr_type::relational)
				{
					if (child->is_operator(OpType::Relational))
						return true;
				}
				else if (child->is_expression_defining_operator())
				{
					// confusable is for example '+' as it is additive or unary
					// like in example "a + -b", '+' is additive, '-' is unary
					// the '-' is part of block, the '+' is not
					if (!child->is_confusable_operator())
						return true;

					// operator preceded and followed by block is not unary
					if (child->prev && child->prev->is_block_or_block_end(at_offset) && child->next &&
					    child->next->is_block_or_block_start(at_offset))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool should_wrap_inner_text() const
	{
		if (children.empty())
			return false;

		if (is_expression(true))
			return true;

		return false;
	}

	bool contains(int pos) const
	{
		return pos >= s_pos && pos <= e_pos;
	}

	ExprRng* top_parent()
	{
		if (parent)
			return parent->top_parent();

		return this;
	}

	ExprRng* bottom_first()
	{
		if (children.empty())
			return this;

		return children.front()->bottom_first();
	}

	ExprRng* bottom_last()
	{
		if (children.empty())
			return this;

		return children.back()->bottom_last();
	}

  private:
	ExprRng()
	{
		exprRangesCount++;
		static int s_id = 0;
		id = s_id++;
	}

  public:
	~ExprRng()
	{
		exprRangesCount--;
	}
	ExprRng& operator=(const ExprRng &) = default;

	static SPtr create()
	{
		auto shared = std::shared_ptr<ExprRng>(new ExprRng());
		exprRanges.push_back(shared);
		return shared;
	}

	bool def_is(LPCWSTR str) const
	{
		return def == str;
	}

	bool def_is(const std::initializer_list<LPCWSTR>& list) const
	{
		for (const auto& x : list)
			if (def == x)
				return true;

		return false;
	}

	bool is_redundant_brace()
	{
		if (!in_expr)
			return false;

		// if not a brace type or is the top parent,
		// return false
		if (!is_brace(L"(") || // not a brace type
		    !parent)           // is the top parent, don't remove top braces
		{
			return false;
		}

		// quick skip of fnc braces
		if (is_function_brace())
			return false;

		bool invert_ops = true;
		bool invert_log = invert_ops && inv_exp_settings.invert_logical_operators();
		bool invert_cmp = invert_ops && inv_exp_settings.invert_relational_operators();

		// text may already start by ! or have parentheses removed
		// following uses extra parsing of the edited text,
		// so we can see if removing parentheses is safe
		if (!is_brace_match(is_cs()))
			return false;

		bool has_redundant = false;
		if (parent && parent->is_brace(L"(") && parent->children.size() == 1 &&
		    parent->is_brace_match(is_cs(), &has_redundant) && has_redundant)
		{
			// if the brace parent contains only
			// this element and this is a brace type,
			// then this is a redundant parentheses
			return true;
		}

		if (prev && prev->is_prefix_operator(nullptr) && !prev->is_empty())
			return false;

		// casting is not redundant
		if (is_casting())
			return false;

		// function calls and such are not redundant
		if (prev && (prev->is_symbol() || prev->is_brace()))
			return false;

		ExprRng::SPtr op;
		int max_precedence = 0; // lowest precedence in parentheses (smaller value means higher precedence)
		bool invert = false;

		// find the nested expression which will give us information
		// about used operators and thus the precedence of the expression
		// inside of parentheses
		ExprRng* expr = this;
		while (expr && expr->children.size() == 1 && expr->children.front()->is_brace(L"(") &&
		       expr->children.front()->is_brace_match(is_cs()))
		{
			expr = expr->children.front().get();
		}

		if (!expr)
			return false;

		auto op_mask = SSParse::OpType(USHORT(SSParse::OpType::Mask_All) & ~USHORT(SSParse::OpType::Primary) &
		                               ~USHORT(SSParse::OpType::Prefix) & ~USHORT(SSParse::OpType::Suffix));

		for (const auto& ch : expr->children)
		{
			if (ch->is_expression_defining_operator())
			{
				invert = (invert_log && ch->is_operator({L"||", L"&&"})) ||
				         (invert_cmp && SSParse::Parser::IsRelationalOperator(ch->def.c_str(), is_cs())) || invert_ops;

				// get precedence of current operator
				auto str_op = invert ? invert_op(ch->def.c_str()) : ch->def.c_str();
				int prec = SSParse::Parser::GetOperatorPrecedence(str_op, is_cs(), op_mask);

				if (max_precedence < prec)
				{
					max_precedence = prec;
					op = ch;
				}
			}
		}

		if (max_precedence <= 0)
			return true;

		if (text.front() == L'!') // not redundant if wrapping expression
			return false;

		auto prev_sig = prev_significant_operator();
		if (prev_sig)
		{
			// if on left is operator with higher or equal precedence,
			// this parentheses are not redundant, return false
			// #ModifyExprToDo: Use get_operator_precedence?

			invert = (invert_log && prev_sig->is_operator({L"||", L"&&"})) ||
			         (invert_cmp && SSParse::Parser::IsRelationalOperator(prev_sig->def.c_str(), is_cs())) ||
			         invert_ops;

			auto outer_op = invert ? invert_op(prev_sig->def.c_str()) : prev_sig->def.c_str();
			int outer_prec = SSParse::Parser::GetOperatorPrecedence(outer_op, is_cs(), op_mask);

			if (outer_prec <= max_precedence)
				return false;
		}

		auto next_sig = next_significant_operator();
		if (next_sig)
		{
			// if on right is operator with higher or equal precedence,
			// this parentheses are not redundant, return false
			// #ModifyExprToDo: Use get_operator_precedence?

			invert = (invert_log && next_sig->is_operator({L"||", L"&&"})) ||
			         (invert_cmp && SSParse::Parser::IsRelationalOperator(next_sig->def.c_str(), is_cs())) ||
			         invert_ops;

			auto outer_op = invert ? invert_op(next_sig->def.c_str()) : next_sig->def.c_str();
			int outer_prec = SSParse::Parser::GetOperatorPrecedence(outer_op, is_cs(), op_mask);

			if (outer_prec <= max_precedence)
				return false;
		}

		return true;
	}

	int get_operator_precedence(std::function<ExprRng::SPtr(int)> at_offset = nullptr) const
	{
		if (is_operator())
		{
			// filter out confusable (those with meaning by context)
			if (is_confusable_operator())
			{
				if (def_is({L"+", L"-", L"*", L"&", L"%", L"^"}) && !prev || !prev->is_block_end(at_offset))
				{
					return SSParse::Parser::GetOperatorPrecedence(def.c_str(), is_cs(), SSParse::OpType::Prefix);
				}

				// resolve prefix/suffix ++ and --
				if (def_is({L"++", L"--"}))
				{
					if (!prev || !prev->is_block_end(at_offset))
						return SSParse::Parser::GetOperatorPrecedence(def.c_str(), is_cs(), SSParse::OpType::Prefix);

					return SSParse::Parser::GetOperatorPrecedence(def.c_str(), is_cs(), SSParse::OpType::Suffix);
				}

				_ASSERTE(!"Unresolved confusable operator!");
			}

			// resolve the rest
			return SSParse::Parser::GetOperatorPrecedence(def.c_str(), is_cs(), SSParse::OpType::Mask_All);
		}

		return 0; // means no operator
	}

	bool is_expression_defining_operator() const
	{
		if (is_operator())
		{
			if (def_is({L"|", L"??", L"||", L"&&"}) || SSParse::Parser::IsAssignmentOperator(def.c_str(), is_cs()) ||
			    SSParse::Parser::IsRelationalOperator(def.c_str(), is_cs()))
			{
				return true;
			}

			if (def_is({L"+", L"-", L"*", L"&", L"%", L"^"}))
			{
				if (prev && prev->is_block_or_block_end(nullptr) && next && next->is_block_or_block_start(nullptr))
				{
					return true;
				}
			}
		}
		return false;
	}

	ExprRng::SPtr next_significant_operator(int precedence = 0) const
	{
		auto n = next;

		// pass -1 to get precedence of this
		if (n && precedence == -1 && is_operator())
			precedence = get_operator_precedence();

		while (n)
		{
			if (n->is_expression_defining_operator())
			{
				// 				if (precedence > 0)
				// 				{
				// 					return precedence >= n->get_operator_precedence(nullptr) ? n : nullptr;
				// 				}

				return n;
			}

			n = n->next;
		}

		return nullptr;
	}

	ExprRng::SPtr prev_significant_operator(int precedence = 0) const
	{
		auto n = prev;

		// pass -1 to get precedence of this
		if (n && precedence == -1 && is_operator())
			precedence = get_operator_precedence();

		while (n)
		{
			if (n->is_expression_defining_operator())
			{
				// 				if (precedence > 0)
				// 				{
				// 					return precedence >= n->get_operator_precedence(nullptr) ? n : nullptr;
				// 				}

				return n;
			}

			n = n->prev;
		}

		return nullptr;
	}

	ExprRng::SPtr first_sibling()
	{
		if (parent && !parent->children.empty())
			return parent->children.front();

		if (!prev)
			return shared_from_this();

		auto n = prev;
		while (n && n->prev)
		{
			n = n->prev;
		}
		return n;
	}

	ExprRng::SPtr last_sibling()
	{
		if (parent && !parent->children.empty())
			return parent->children.back();

		if (!next)
			return shared_from_this();

		auto n = next;
		while (n && n->next)
		{
			n = n->next;
		}
		return n;
	}

	void save(bool process_children);

	void restore(bool process_children);

	void offset(int offset, bool process_children = true);
	bool apply_edit(const std::shared_ptr<ExprRng::Edit>& edit, bool process_children);
	size_t get_brace_map(bool is_cs, std::map<int, int>& brace_map);
	bool is_brace_match(bool is_cs, bool* has_redundant_braces = nullptr, bool* negated = nullptr);

	void copy_to(ExprRng* rng)
	{
		if (rng)
		{
			rng->text = text;
			rng->t = t;
			rng->s_pos = s_pos;
			rng->e_pos = e_pos;
			rng->def_pos = def_pos;
			rng->def = def;
			rng->id = id;
			rng->invert_ = invert_;
			rng->inv = inv;
			rng->in_expr = in_expr;
			rng->dirty = dirty;
			rng->bottom_prev = bottom_prev;
			rng->bottom_next = bottom_next;
			rng->prev = prev;
			rng->next = next;
			rng->parent = parent;
			rng->children.assign(children.cbegin(), children.cend());
			rng->edits.assign(edits.cbegin(), edits.cend());
		}
	}

	void dispose()
	{
		saved = nullptr;
		parent = nullptr;
		bottom_prev = nullptr;
		bottom_next = nullptr;
		prev = nullptr;
		next = nullptr;
		children.clear();
	}

	std::wstring text;

	type t = type::t_empty;

	int s_pos = -1;
	int e_pos = -1;
	int def_pos = -1;

	std::wstring def;

	std::vector<std::tuple<std::shared_ptr<ExprRng::Edit>, bool>> edits;

	int id = -1;

	bool invert_ = false;
	bool in_expr = true;
	bool dirty = false;
	invertedBy inv = invertedBy::None;

	ExprRng::SPtr bottom_prev; // unaffected by grouping
	ExprRng::SPtr bottom_next; // unaffected by grouping

	ExprRng::SPtr prev;
	ExprRng::SPtr next;

	ExprRng::SPtr parent;

	ExprRng::SPtr saved;

	std::vector<ExprRng::SPtr> children;
};

void ExprRng::save(bool process_children)
{
	saved = ExprRng::create();
	copy_to(saved.get());

	if (process_children)
	{
		for (auto& ch : children)
		{
			_ASSERTE(ch);
			ch->save(true);
		}
	}
}

void ExprRng::restore(bool process_children)
{
	if (saved)
		saved->copy_to(this);

	if (process_children)
	{
		for (auto& ch : children)
		{
			_ASSERTE(ch);
			ch->restore(true);
		}
	}
}

void ExprRng::offset(int offset, bool process_children /* = true*/)
{
	if (process_children)
	{
		for (auto& ch : children)
		{
			_ASSERTE(ch);
			ch->offset(offset, true);
		}
	}

	s_pos += offset;
	e_pos += offset;
	def_pos += offset;
}

bool ExprRng::apply_edit(const std::shared_ptr<ExprRng::Edit>& edit, bool process_children)
{
	bool result = true;

	try
	{
		_ASSERTE(edit->len >= 0);
		_ASSERTE(edit->pos >= 0);
		_ASSERTE(e_pos - s_pos + 1 == (int)text.length());

		if (!in_expr)
		{
			// in non-expression ranges do only offset
			// in general, non-expressions are first and last in row,
			// these are just to tell us what is surrounding the expression

			if (edit->pos < s_pos)
				offset((int)edit->str.length() - edit->len, process_children);

			return false; // always return false in non-expression
		}

		if (edit->pos > e_pos + 1 || (edit->str.empty() && edit->pos > e_pos)) // only insertions at the end make sense
			return false;

		if (edit->pos < s_pos || (edit->pos == s_pos && edit->tail))
		{
			offset((int)edit->str.length() - edit->len, process_children);
			edits.emplace_back(edit, false);
			return false;
		}

		// Invalid erase on end?
		if (edit->len > 0 && edit->pos + edit->len - 1 > e_pos)
		{
			_ASSERTE(!"Invalid erase on end in ExprRng::apply_edit");
			return false;
		}

		if (process_children)
		{
			// apply changes in children
			for (auto it = children.crbegin(); it != children.crend(); it++)
			{
				_ASSERTE(*it);

				if (*it && (*it)->apply_edit(edit, true))
					break;
			}
		}

		bool applied = false;
		int loc_pos = edit->pos - s_pos;

		int dbg_txt_len = (int)text.length();
		auto dbg_txt = text;

		if (edit->len > 0)
		{
			text.erase((uint)loc_pos, (uint)edit->len);
			dirty = true;
			applied = true;
		}

		if (!edit->str.empty())
		{
			text.insert((uint)loc_pos, edit->str);
			dirty = true;
			applied = true;
		}

		int offset = (int)edit->str.length() - edit->len;
		int dbg_offset = (int)text.length() - dbg_txt_len;

		_ASSERTE(offset == dbg_offset);
		std::ignore = dbg_offset;

		if (edit->pos < def_pos)
			def_pos += offset;

		e_pos += offset;

		edits.emplace_back(edit, applied);

		_ASSERTE(e_pos - s_pos + 1 == (int)text.length());
	}
	catch (const std::exception& e)
	{
		(void)e;
		result = false;
		_ASSERTE(!"Exception in ExprRng::apply_edit");
	}
	catch (...)
	{
		result = false;
		_ASSERTE(!"SEH exception in ExprRng::apply_edit");
	}

	return result;
}

size_t ExprRng::get_brace_map(bool is_cs, std::map<int, int>& brace_map)
{
	size_t old_size = brace_map.size();
	SSParse::SharedWStr wstr(text);
	SSParse::CharStateIter csb(0, (int)wstr.length() - 1, is_cs, wstr);
	SSParse::BraceCounter bc;
	bc.on_block = [&](char ch, int opos, int cpos) {
		if (ch == '(')
		{
			brace_map[opos] = cpos;
		}
	};

	bc.set_ignore_angle_brackets(true);

	while (csb.next())
	{
		if (!csb.ch || wt_isspace(csb.ch))
			continue;

		if ((csb.state & (SSParse::chs_comment_mask)) != 0)
			continue;

		if ((csb.state & (SSParse::chs_no_code | SSParse::chs_directive)) != 0)
			continue;

		bc.on_char(csb.ch, csb.pos, nullptr);
	}

	return brace_map.size() - old_size;
}

bool ExprRng::is_brace_match(bool is_cs, bool* has_redundant_braces /*= nullptr*/, bool* negated /*= nullptr*/)
{
	if (has_redundant_braces)
		*has_redundant_braces = false;

	if (negated)
		*negated = false;

	if (!dirty)
	{
		if (!is_brace(L"("))
			return false;

		if (has_redundant_braces)
		{
			*has_redundant_braces = children.size() == 1 && children.front()->is_brace(L"(");
		}

		return true;
	}

	if (text.back() != L')')
		return false;

	// allow once negated by !
	if (text.front() != L'(' && text.length() > 1 && text[1] != L'(')
		return false;

	if (negated)
		*negated = text.front() == L'!';

	// confirm that opening brace is matching closing one
	std::map<int, int> brace_map;
	std::wstring tmp_str(text);
	SSParse::SharedWStr wstr(tmp_str);

	int csb_start = 0;
	int csb_end = (int)wstr.length() - 1;

	// if text starts with !(( or (( or ends with )), we can face to child used to wrap in parentheses,
	// like if "(A) + (B)" which breaks to "(A)" "+" "(B)" get wrapped into "!()",
	// we get "!((A) + (B))" which breaks to "!((A)" "+" "(B))".
	// Fortunately we can skip those starts or ends and count their braces then

	// 	if (parent && parent->inv != invertedBy::None &&
	// 		(
	// 		parent->inv == invertedBy::AddedNotWrapped ||
	// 		parent->inv == invertedBy::AddedNotWrappedInner ||
	// 		parent->inv == invertedBy::RemovedNotWrapped ||
	// 		parent->inv == invertedBy::RemovedNotWrappedInner
	// 		))
	// 	{
	// 		// check the start
	// 		if (tmp_str.compare(0, 3, L"!((") == 0)
	// 			csb_start = 2;
	// 		else if (tmp_str.compare(0, 2, L"((") == 0)
	// 			csb_start = 1;
	//
	// 		// check the end
	// 		if (tmp_str.compare(tmp_str.length() - 2, 2, L"))") == 0)
	// 			csb_end = wstr.length() - 2;
	// 	}

	SSParse::CharStateIter csb(csb_start, csb_end, is_cs, wstr);
	SSParse::BraceCounter bc;
	bc.on_block = [&](char ch, int opos, int cpos) {
		if (ch == '(')
		{
			brace_map[opos] = cpos;
		}
	};

	bc.set_ignore_angle_brackets(true);

	while (csb.next())
	{
		if (!csb.ch || wt_isspace(csb.ch))
			continue;

		if ((csb.state & (SSParse::chs_comment_mask)) != 0)
		{
			// replace unwanted chars so we don't need filter later
			tmp_str.replace((uint)csb.pos, 1, 1, L' ');
			continue;
		}

		if ((csb.state & (SSParse::chs_no_code | SSParse::chs_directive)) != 0)
		{
			// replace unwanted chars so we don't need filter later
			tmp_str.replace((uint)csb.pos, 1, 1, L'@');
			continue;
		}

		// possibly argument list, let it be...
		if (csb.ch == ',' && bc.is_open())
			return false;

		bc.on_char(csb.ch, csb.pos, nullptr);

		if (bc.is_mismatch())
			return false;

		if (csb.pos >= csb_end)
			break;
	}

	// brace counter stayed open!
	if (!bc.is_closed())
		return false;

	{
		// check if start pos is match with end pos

		auto sp = brace_map.find(csb_start);
		if (sp != brace_map.end())
		{
			if (sp->second != csb_end)
				return false;
		}
	}

	if (has_redundant_braces)
	{
		// offset brace map
		std::map<int, int> tmp;
		std::swap(tmp, brace_map);
		for (auto& x : tmp)
			brace_map[x.first + csb_start] = x.second + csb_start;

		if (brace_map.size() > 1 && wstr.length() >= 4) // 4 at least (())
		{
			// no need filter,
			// we removed unwanted chars in previous step

			auto top = brace_map.begin();
			int sp = wstr.find_nospace(top->first + 1);
			int ep = wstr.rfind_nospace(top->second - 1);

			// is nested?
			if (ep > sp && wstr.safe_at(sp) == L'(' && wstr.safe_at(ep) == L')')
			{
				auto found = brace_map.find(sp);
				if (found != brace_map.end() && found->second == ep)
				{
					*has_redundant_braces = true;
				}
			}
		}
	}

	return true;
}

class CInvertExpression : public IApplyEdit, public SSContextBase
{
	enum constants
	{
		status_none = -1,
		status_initialised = -2,
		status_parsed = -3,
		status_prepared = -4
	};

  public:
	int expr_start = -1;
	int expr_end = -1;

	static std::shared_ptr<CInvertExpression>& Get(bool init_if_null)
	{
		static std::shared_ptr<CInvertExpression> s_inv_exp;

		if (!s_inv_exp && init_if_null)
			s_inv_exp = std::make_shared<CInvertExpression>();

		return s_inv_exp;
	}

  private:
	int status = status_none;
	uint status_curpos = UINT_MAX;

	std::vector<ExprRng::SPtr> ranges;

	std::vector<std::shared_ptr<ExprRng::Edit>> edits;
	size_t edits_count = 0; // saved to trim on restore

#if ENABLE_LOGGING
	std::wostringstream log;
#endif

	void on_error(const WTString& msg, int line)
	{
		status = line;

#if ENABLE_LOGGING
		{
			CString cstr;
			CString__FormatA(cstr, " [%d]", line);
			INV_EXP_LOG("error: " << (LPCSTR)(msg.c_str() + cstr) << "\r\n");
		}
#endif

		if (g_loggingEnabled)
			VALogError(("ERROR: [MODIFY EXPR] " + msg).c_str(), line, TRUE);

		if (gTestsActive && gTestLogger && !inv_exp_settings.check_only)
		{
			CString cstr;
			CString__FormatA(cstr, " [%d]", line);
			gTestLogger->LogStr(WTString(msg + cstr));
		}
	}

	void show_log()
	{
#if ENABLE_LOGGING
		if (!gTestsActive)
			WtMessageBox(WTString(log.str().c_str()), IDS_APPNAME, MB_OK);
#endif
	}

	void log_rng(const ExprRng::SPtr& rng, bool log_children, int indent = 0)
	{
#if ENABLE_LOGGING
		if (gTestsActive)
			return;

		std::wstring indent_str((size_t)indent, L' ');

		INV_EXP_LOG(indent_str << "\"" << rng->text << "\"");

		if (log_children)
		{
			for (const auto& ch : rng->children)
				log_rng(ch, true, indent + 1);
		}
#endif
	}

  public:
	CInvertExpression()
	{
		command = icmdVaCmd_RefactorModifyExpression;

		dispose_ranges();

		expr_start = caret_pos;
		expr_end = caret_pos - 1;

		init_braces = true;
	}

	~CInvertExpression()
	{
		dispose_ranges();
	}

	void dispose_ranges()
	{
		for (const auto& x : exprRanges)
		{
			auto spt = x.lock();
			if (spt)
				spt->dispose();
		}

		exprRanges.clear();
		ranges.clear();

		_ASSERTE(exprRangesCount == 0);
	}

	bool sub_init(bool reset, const WTString& fileText)
	{
		if (status == status_initialised && !reset && kCurPos == status_curpos)
			return true;

		status_curpos = kCurPos;

		pos_buffer_to_wide((ULONG)caret_pos, caret_pos);

		int user_caret_pos = caret_pos;
		expr_start = caret_pos;
		expr_end = caret_pos - 1;
		dispose_ranges();

		INV_EXP_LOG("caret start = " << caret_pos << "\r\n");

		status = status_none;

		if (is_cs)
			inv_exp_settings.flags |= tagSettings::ModExpr_IsCSharp;

		std::wstring close_braces = L")]}>";
		std::wstring open_braces = L"({";

		// skip leading spaces and splitters
		while (caret_pos > 0 && (text.is_EOF(caret_pos) || text.is_one_of(caret_pos, L"\r\n;")))
			caret_pos--;

		auto code_filter = make_code_filter();

		// #SmartSelect_CaretPosition
		if (text.safe_at(caret_pos, code_filter) == L',')
			caret_pos++;

		// find previous and next non-space position
		auto code_pred = [](wchar_t ch) { return ch == L'\r' || ch == '\n' || !wt_isspace(ch); };
		int prev_ns = text.rfind(caret_pos, code_pred, code_filter);
		int next_ns = text.find(caret_pos, code_pred, code_filter);

		// if there is no non-space position on line, cancel...
		if (prev_ns != next_ns &&
		    (text.is_space_or_EOF(prev_ns, code_filter) || text.is_one_of(prev_ns, L"{([,;", code_filter)) &&
		    (text.is_space_or_EOF(next_ns, code_filter) || text.is_one_of(next_ns, L"})],;", code_filter)))
		{
			on_error(L"Cannot find expression to be inverted!", __LINE__);
			return false;
		}

		// [case: 147653] don't offer out of expression when caret is in leading white space
		bool in_leading_white_space = text.line_start(user_caret_pos) > prev_ns && user_caret_pos < next_ns;

		// set new position to "caret_pos" so we start parsing there for now
		if (text.is_identifier_boundary(prev_ns, code_filter))
			caret_pos = prev_ns;
		else if (text.is_identifier_boundary(next_ns, code_filter))
			caret_pos = next_ns;

		auto is_return_lch = [](wchar_t lch) { return lch == L'n' || lch == L'd'; };

		auto is_return = [](const std::wstring& sym) {
			// NOTE: when adding new keywords, also modify is_return_lch
			return sym == L"return" || sym == L"co_return" || sym == L"co_yield";
		};

		int sym_epos = -1;
		auto sym = text.get_symbol(caret_pos, true, nullptr, &sym_epos, code_filter);
		if (!sym.empty())
		{
			if (sym == L"if" || sym == L"while" || sym == L"else")
			{
				auto next = text.find(
				    sym_epos + 1, [](wchar_t ch) -> bool { return !ISCSYM(ch) && !wt_isspace(ch); }, code_filter);

				if (next != -1 && text.safe_at(next, code_filter) == L'(')
					caret_pos = next;
				else
				{
					on_error(L"Cannot find expression to be inverted!", __LINE__);
					return false;
				}
			}
			else if (is_return(sym))
			{
				auto next = text.find_nospace(sym_epos + 1, code_filter);
				if (next != -1)
					caret_pos = next;
			}
		}

		auto include_not = [&](int& sp, int ep) -> bool {
			int old_sp = sp;
			// include preceding NOT operators like !!!!(...)
			if (text.safe_at(sp, code_filter) == L'(' && text.safe_at(ep, code_filter) == L')')
			{
				int prev = text.rfind_nospace(sp - 1, code_filter);
				if (text.safe_at(prev, code_filter) == '!')
				{
					sp = prev;
					prev = text.rfind_nospace(prev - 1, code_filter);
					while (text.safe_at(prev, code_filter) == '!')
					{
						sp = prev;
						prev = text.rfind_nospace(prev - 1, code_filter);
					}
				}
			}
			return old_sp != sp;
		};

		// #InvertExpression00_Init

		INV_EXP_LOG("caret in init = " << caret_pos << "\r\n");

		auto on_initialised = [&](int line_number) 
		{ 		
#if ENABLE_LOGGING
			auto dbg_str = text.substr_se(expr_start, expr_end);
			INV_EXP_LOG("rng: " << expr_start << ", " << expr_end << "\r\n");
			INV_EXP_LOG("rng text: " << dbg_str << "\r\n");
#endif // ENABLE_LOGGING

			// [case:147653] don't offer out of expression when caret is on line start
			if (in_leading_white_space && user_caret_pos < expr_start) 
			{
				on_error(L"Caret in leading white space out of any expression!", line_number);
				return false;
			}

			status = status_initialised;
			return true; 
		};

		for (int x = caret_pos; x >= 0; --x)
		{
			wchar_t lch = text.safe_at(x, code_filter);

			if (wt_isspace(lch))
				continue;

			// skip arrow operators -> =>
			if (lch == '>' && text.is_one_of(x - 1, L"-=", code_filter))
			{
				x--;
				continue;
			}

			if (x != caret_pos)
			{
				if (lch == L',' || lch == L';' ||
				    (lch == L'=' && SSParse::Parser::IsAssignmentOperator(get_operator_or_empty(x), is_cs)) ||
				    //(lch == L':' && !ssc->is_operator(x, L"::")) ||
				    //(lch == L'?' && ssc->is_operator(x, L"?")) ||
				    (is_return_lch(lch) && is_return(text.get_symbol(x, true, nullptr, nullptr, code_filter))) ||
				    (open_braces.find(lch) != std::wstring::npos && get_matching_brace(x, false, code_filter) >= 0))
				{
					if (lch == L'{')
					{
						on_error(L"Cannot find expression to be inverted!", __LINE__);
						return false;
					}

					if (is_operator(caret_pos, L"?"))
					{
						expr_start = text.find_nospace(x + 1, code_filter);
						expr_end = text.rfind_nospace(caret_pos - 1, code_filter);

						include_not(expr_start, expr_end);

						// position caret_pos
						if (caret_pos < expr_start)
							caret_pos = expr_start;
						else if (caret_pos > expr_end)
							caret_pos = expr_end;

						return on_initialised(__LINE__);
					}

					int n = (int)text.length();
					for (int y = caret_pos; y < n; ++y)
					{
						wchar_t rch = text.safe_at(y, code_filter);

						if (wt_isspace(rch))
							continue;

						// skip arrow operator
						if (lch == '-' && text.safe_at(y + 1, code_filter) == '>')
						{
							y++;
							continue;
						}

						if (open_braces.find(rch) != std::wstring::npos)
						{
							int found = get_matching_brace(y, false, code_filter);
							if (found >= 0)
							{
								y = found;
								continue;
							}
						}

						if (rch == L';' || rch == L',' ||
						    //(rch == L':' && !ssc->is_operator(y, L"::")) ||
						    //(rch == L'?' && ssc->is_operator(y, L"?")) ||
						    (rch == L'=' && SSParse::Parser::IsAssignmentOperator(get_operator_or_empty(y), is_cs)) ||
						    (close_braces.find(rch) != std::wstring::npos &&
						     get_matching_brace(y, false, code_filter) >= 0))
						{
							if (include_not(x, y))
							{
								expr_start = x;
								expr_end = y;
							}
							else
							{
								expr_start = text.find_nospace(x + 1, code_filter);
								expr_end = text.rfind_nospace(y - 1, code_filter);
							}

							// position caret_pos
							if (caret_pos < expr_start)
								caret_pos = expr_start;
							else if (caret_pos > expr_end)
								caret_pos = expr_end;

							return on_initialised(__LINE__);
						}
					}
				}
			}
			if (expr_start <= expr_end)
				break;

			if (close_braces.find(lch) != std::wstring::npos)
			{
				auto found = get_matching_brace(x, false, code_filter);
				if (found >= 0)
				{
					// user clicked directly on ) or ]
					if (caret_pos == x)
					{
						if (lch == L'}' || (lch == '>' && is_template(x, lch)))
						{
							on_error(L"Cannot find expression to be inverted!", __LINE__);
							return false;
						}

						include_not(x, found);

						expr_start = found;
						expr_end = x;

						return on_initialised(__LINE__);
					}

					x = found;
					continue;
				}
			}
			else if (open_braces.find(lch) != std::wstring::npos)
			{
				if (lch == L'{')
				{
					on_error(L"Cannot find expression to be inverted!", __LINE__);
					return false;
				}

				auto found = get_matching_brace(x, false, code_filter);
				if (found >= 0)
				{
					include_not(x, found);

					expr_start = x;
					expr_end = found;

					return on_initialised(__LINE__);
				}
			}
		}

		on_error(L"Cannot find expression to be inverted!", __LINE__);
		return false;
	}

	bool init_ctx(bool force_update = false)
	{
		is_initialized = false;
		is_preparing_tree = false;

		EdCntPtr ed(g_currentEdCnt);

		if (ed)
		{
			is_cs = ed->m_ftype == CS;
			is_cpp = IsCFile(ed->m_ftype);
			is_xml = ed->m_ftype == XML || ed->m_ftype == XAML;

			kCurPos = ed->CurPos();
			caret_pos = ed->GetBufIndex((int)kCurPos);
		}
		else
		{
			_ASSERTE(!"No EdCnt available!");
			return false;
		}

		BOOL forceBuff = ed->m_bufState == CTer::BUF_STATE_CLEAN ? TRUE : FALSE;
		WTString fileText(ed->GetBuf(forceBuff));
		uint file_hash = fileText.hash();

		RemovePadding_kExtraBlankLines(fileText);

		if (caret_pos >= fileText.GetLength())
			caret_pos = fileText.GetLength() - 1;

		text.assign(fileText.Wide());

		bool reset = force_update || ed_ptr != ed.get() || prev_CurPos != ed->CurPos() || file_hash != buffer_hash;

		if (reset)
		{
			ed_ptr = ed.get();
			buffer_hash = file_hash;
			ch_states.clear();
			cb_vec.clear();
			dir_starts_vec.clear();
			lines.reset();
			cs_interp.clear();
			num_literals.clear();
			brace_map.clear();
			operator_angs.clear();

			lines.reset(new SSParse::TextLines(text));

			auto on_num_lit = [&](int sp, int ep, char ch) {
				if (sp >= 0 && ep >= sp)
					num_literals.emplace_back(sp, ep, ch);
			};

			if (!init_braces)
			{
				SSParse::Parser::MarkCommentsAndContants(text, ch_states, &cs_interp, &ch_state_borders, &cb_vec,
				                                         &dir_starts_vec, on_num_lit, lines.get(), nullptr, is_cs);
			}
			else
			{
				BraceCounter bc;
				bc.on_block = [&](char ch, int opos, int cpos) { brace_map.insert_or_assign(opos, cpos); };

				bc.set_ignore_angle_brackets(true);

				auto on_char_state = [&](int pos, wchar_t ch, CharState chs) {
					if (!ch || wt_isspace(ch))
						return;

					if ((chs & chs_comment_mask) != 0)
						return;

					if ((chs & (chs_no_code | chs_directive)) != 0)
						return;

					if (ch == '<' || ch == '>')
						cache_template_char(pos, ch, false, true, true);

					bc.on_char(ch, pos, nullptr);
				};

				SSParse::Parser::MarkCommentsAndContants(text, ch_states, &cs_interp, &ch_state_borders, &cb_vec,
				                                         &dir_starts_vec, on_num_lit, lines.get(), on_char_state,
				                                         is_cs);
			}

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
		}

		if (!sub_init(reset, fileText))
			return false;

		fileText.Empty();

		if (lines)
			lines->DoLineColumnTest();

		prev_CurPos = kCurPos;
		is_initialized = true;

		return true;
	}

	bool parse()
	{
		INV_EXP_LOG("\r\n");
		INV_EXP_LOG("#### parsing ranges #### \r\n");

		int pre_expr = -1;
		int post_expr = -1;

		auto code_filter = make_code_filter();

		if (expr_start > 0)
			pre_expr = text.rfind_nospace(expr_start - 1, 0, code_filter);

		if (expr_end < (int)text.length() - 1)
			post_expr = text.find_nospace(expr_end + 1, (int)text.length() - 1, code_filter);

		ExprRng::SPtr pre_rng;
		ExprRng::SPtr post_rng;

		auto add_range = [&](ExprRng::SPtr rng) {
			log_rng(rng, false);
			INV_EXP_LOG("\r\n");
			ranges.push_back(rng);
		};

		// #InvertExpression02_Parsing

		INV_EXP_LOG("-- num literals \r\n");

		std::set<std::tuple<int, int>> skip_parts;

		auto skip_part = [&](int pos, int& end_pos) -> bool {
			int sp = 0;
			for (const auto& nl : skip_parts)
			{
				sp = std::get<0>(nl);
				if (sp < pos)
					continue;
				if (sp == pos)
				{
					end_pos = std::get<1>(nl);
					return true;
				}
				break;
			}

			return false;
		};

		// add known literals
		for (auto num : num_literals)
		{
			if ((std::get<0>(num) >= expr_start && std::get<1>(num) <= expr_end) ||
			    (!post_rng && std::get<0>(num) == post_expr) || (!pre_rng && std::get<1>(num) == pre_expr))
			{
				auto rng = ExprRng::create();
				rng->t = ExprRng::t_number;
				rng->s_pos = std::get<0>(num);
				rng->e_pos = std::get<1>(num);

				skip_parts.emplace(rng->s_pos, rng->e_pos);

				rng->def = text.substr_se(rng->s_pos, rng->e_pos);
				rng->text = rng->def;
				rng->def_pos = rng->s_pos;

				if (!post_rng && std::get<0>(num) == post_expr)
				{
					rng->in_expr = false;
					post_rng = rng;
					continue;
				}

				if (!pre_rng && std::get<1>(num) == pre_expr)
				{
					rng->in_expr = false;
					pre_rng = rng;
					continue;
				}

				add_range(rng);
			}
		}

		INV_EXP_LOG("-- string literals \r\n");

		// try find string literal preceding expression
		if (!pre_rng && is_string_literal(pre_expr))
		{
			pre_rng = ExprRng::create();
			pre_rng->t = ExprRng::t_string;
			pre_rng->e_pos = pre_expr;

			int x = pre_expr;
			while (x >= 0 && is_string_literal(x))
			{
				pre_rng->s_pos = x--;
			}

			skip_parts.emplace(pre_rng->s_pos, pre_rng->e_pos);

			pre_rng->def = text.substr_se(pre_rng->s_pos, pre_rng->e_pos);
			pre_rng->text = pre_rng->def;
			pre_rng->def_pos = pre_rng->s_pos;
			pre_rng->in_expr = false;
		}

		// try find string literal succeeding expression
		if (!post_rng && is_string_literal(post_expr))
		{
			post_rng = ExprRng::create();
			post_rng->t = ExprRng::t_string;
			post_rng->s_pos = post_expr;

			int x = post_expr;
			while (is_string_literal(x))
			{
				post_rng->e_pos = x++;
			}

			skip_parts.emplace(post_rng->s_pos, post_rng->e_pos);

			post_rng->def = text.substr_se(post_rng->s_pos, post_rng->e_pos);
			post_rng->text = post_rng->def;
			post_rng->def_pos = post_rng->s_pos;
			post_rng->in_expr = false;
		}

		// add known string literals inside expression
		int str_s = -1;
		for (int i = expr_start; i <= expr_end; i++)
		{
			if (is_string_literal(i))
			{
				if (str_s == -1)
					str_s = i;
			}
			else if (str_s != -1)
			{
				auto rng = ExprRng::create();
				rng->t = ExprRng::t_string;
				rng->s_pos = str_s;
				rng->e_pos = i - 1;

				skip_parts.emplace(rng->s_pos, rng->e_pos);

				rng->def = text.substr_se(rng->s_pos, rng->e_pos);
				rng->text = rng->def;
				rng->def_pos = rng->s_pos;
				add_range(rng);

				str_s = -1;
			}
		}

		if (str_s != -1)
		{
			auto rng = ExprRng::create();
			rng->t = ExprRng::t_string;
			rng->s_pos = str_s;
			rng->e_pos = expr_end;

			skip_parts.emplace(rng->s_pos, rng->e_pos);

			rng->def = text.substr_se(rng->s_pos, rng->e_pos);
			rng->text = rng->def;
			rng->def_pos = rng->s_pos;
			add_range(rng);
		}

		auto resolve_surrounding = [&](int& index, ExprRng::SPtr& rslt) {
			std::wstring brs = L"()[]()<>";
			auto ch = text.safe_at(index);
			size_t idx = brs.find(ch);

			// ignore < > operators for braces
			if ((ch == L'<' || ch == L'>') && !is_template(index, ch, true))
				idx = std::wstring::npos;

			if (idx != std::wstring::npos)
			{
				rslt = ExprRng::create();
				rslt->t = ExprRng::t_brace;
				rslt->s_pos = index;
				rslt->e_pos = index;
				rslt->def = (idx % 2) ? brs[idx - 1] : brs[idx];
				rslt->text = ch;
				rslt->def_pos = rslt->s_pos;
				rslt->in_expr = false;
			}
			else if (ISCSYM(ch))
			{
				rslt = ExprRng::create();
				rslt->t = ExprRng::t_symbol;
				rslt->def = text.get_symbol(index, true, &rslt->s_pos, &rslt->e_pos);
				rslt->text = rslt->def;
				rslt->def_pos = rslt->s_pos;
				rslt->in_expr = false;
			}
			else
			{
				auto rng = ExprRng::create();
				auto op = get_operator(index, &rng->s_pos, &rng->e_pos);
				rng->t = ExprRng::t_operator;
				rng->def = op ? std::wstring(op) : std::wstring(1, ch);
				rng->text = rng->def;
				rng->def_pos = rng->s_pos;
				rng->in_expr = false;
				rslt = rng;
			}
		};

		if (!pre_rng)
			resolve_surrounding(pre_expr, pre_rng);

		if (!post_rng)
			resolve_surrounding(post_expr, post_rng);

		_ASSERTE(pre_rng);
		_ASSERTE(post_rng);

		std::vector<ExprRng::SPtr> blocks;

		auto fix_type = [&](ExprRng::SPtr& rng) {
			if (rng->t == ExprRng::t_symbol && is_type(rng->def_pos))
				rng->t = ExprRng::t_type;

			else if (rng->t == ExprRng::t_brace)
			{
				int x = text.rfind_nospace(rng->s_pos - 1, code_filter);
				if (text.is_identifier(x))
				{
					if (rng->def_is(L"("))
						rng->t = ExprRng::t_fnc_brace;
					else if (rng->def_is(L"["))
						rng->t = ExprRng::t_idx_brace;
					else if (rng->def_is(L"<"))
						rng->t = ExprRng::t_tpl_brace;
				}
			}

			return rng;
		};

		SSParse::BraceCounter bc;
		bc.set_ignore_angle_brackets(false);
		bc.ang_filter = make_ang_filter();
		bc.on_block = [&](char open_ch, int open_pos, int close_pos) {
			ExprRng::SPtr nfo = ExprRng::create();
			nfo->t = ExprRng::t_brace;
			nfo->def = (wchar_t)open_ch;
			nfo->s_pos = open_pos;
			nfo->e_pos = close_pos;
			nfo->text = text.substr_se(open_pos, close_pos);
			blocks.push_back(fix_type(nfo));
		};

		INV_EXP_LOG("-- parsed \r\n");

		int skip_to = -1;
		ExprRng::SPtr oi = ExprRng::create();
		for (int i = expr_start; i <= expr_end; i++)
		{
			wchar_t ch = text.at(i);

			bc.on_char(ch, i, nullptr);

			if (i <= skip_to)
				continue;

			if (skip_part(i, skip_to))
			{
				if (!oi->is_space() && !oi->is_empty() && oi->s_pos >= 0 && oi->e_pos >= 0)
				{
					oi->text = text.substr_se(oi->s_pos, oi->e_pos);
					add_range(fix_type(oi));
				}

				// create new range
				oi = ExprRng::create();
				continue;
			}

			bool is_space = !code_filter(i, ch) || wt_isspace(ch);
			bool is_csym = ISCSYM(ch);

			// start the range
			if ((is_space || is_csym) && oi->s_pos == -1)
			{
				oi->t = is_csym ? ExprRng::t_symbol : ExprRng::t_space;
				oi->s_pos = i;
				oi->e_pos = i;
				oi->def_pos = i;
				oi->def = ch;
				continue;
			}

			if (is_csym)
			{
				if (oi->t != ExprRng::t_symbol && oi->s_pos != -1)
				{
					if (!oi->is_space())
					{
						oi->text = text.substr_se(oi->s_pos, oi->e_pos);
						add_range(fix_type(oi));
					}

					// start new symbol
					oi = ExprRng::create();
					oi->s_pos = i;
					oi->e_pos = i;
					oi->t = ExprRng::t_symbol;
					oi->def_pos = i;
					oi->def = ch;
				}
				else if (oi->t == ExprRng::t_symbol)
				{
					oi->def += ch;
					oi->e_pos = i;
				}

				continue;
			}

			if (is_space)
			{
				if (oi->t == ExprRng::t_symbol && oi->s_pos != -1)
				{
					if (!oi->is_space())
					{
						oi->text = text.substr_se(oi->s_pos, oi->e_pos);
						add_range(fix_type(oi));
					}

					// start new empty
					oi = ExprRng::create();
					oi->s_pos = i;
					oi->e_pos = i;
					oi->t = ExprRng::t_space;
					oi->def_pos = i;
					oi->def = ch;
				}
				else if (oi->t != ExprRng::t_symbol)
				{
					if (oi->t != ExprRng::t_operator)
						oi->def += ch;

					oi->e_pos = i;
				}

				continue;
			}

			// !is_space && !is_csym
			{
				// if symbol, close it
				if (oi->t == ExprRng::t_symbol)
				{
					oi->text = text.substr_se(oi->s_pos, oi->e_pos);
					add_range(fix_type(oi));

					// start new range
					oi = ExprRng::create();
					// don't initialize
				}

				int epos = -1;
				auto op = get_operator(i, nullptr, &epos);
				if (op)
				{
					if (oi->t == ExprRng::t_operator && oi->s_pos != -1)
					{
						oi->text = text.substr_se(oi->s_pos, oi->e_pos);
						add_range(fix_type(oi));

						// start new range
						oi = ExprRng::create();
					}

					if (oi->s_pos == -1)
						oi->s_pos = i;

					oi->def = op;
					oi->def_pos = i;
					oi->t = ExprRng::t_operator;
					oi->e_pos = epos;
					i = epos;
					continue;
				}

				// !operator && !symbol && !space
				// so whatever we are in, close it
				if (oi->s_pos != -1)
				{
					if (!oi->is_space())
					{
						oi->text = text.substr_se(oi->s_pos, oi->e_pos);
						add_range(fix_type(oi));
					}

					// start new range
					oi = ExprRng::create();
					// don't initialize
				}
			}
		}

		if (oi->s_pos != -1 && !oi->is_space())
		{
			oi->text = text.substr_se(oi->s_pos, oi->e_pos);
			add_range(fix_type(oi));
		}

		// *****************************************************************
		// *****************************************************************
		// resolve children

		// #InvertExpression03_Children

		auto rng_compare = [](const ExprRng::SPtr& a, const ExprRng::SPtr& b) {
			if (a->s_pos != b->s_pos)
				return a->s_pos < b->s_pos;

			return a->e_pos > b->e_pos;
		};

		// sort left to right - just for sure
		std::sort(ranges.begin(), ranges.end(), rng_compare);

		auto at_pos = [&](int pos, bool pre_post = false) -> ExprRng::SPtr {
			if (pos <= 0)
				return pre_post ? pre_rng : nullptr;

			if (pos >= (int)ranges.size())
				return pre_post ? post_rng : nullptr;

			return ranges[(uint)pos];
		};

		// resolve literals children
		// we need literals and strings to appear in next_simple and prev_simple,
		// which ignore parentheses, thus these are both handled in separate steps
		for (int i = (int)ranges.size() - 1; i > 0; --i)
		{
			auto& curr = ranges[(uint)i];

			for (int j = i - 1; j >= 0; --j)
			{
				auto& prev = ranges[(uint)j];
				if (prev->e_pos >= curr->e_pos)
				{
					if (!prev->is_number())
						prev->children.insert(prev->children.begin(), curr);

					ranges.erase(ranges.begin() + i);
					break;
				}
			}
		}

		// resolve bottom siblings before adding blocks
		for (size_t x = 0; x < ranges.size(); x++)
		{
			ranges[x]->bottom_prev = at_pos(int(x - 1), true);
			ranges[x]->bottom_next = at_pos(int(x + 1), true);

			_ASSERTE(ranges[x]->bottom_prev);
			_ASSERTE(ranges[x]->bottom_next);
		}

		// add blocks and sort again, so parenting works
		ranges.insert(ranges.end(), blocks.begin(), blocks.end());
		std::sort(ranges.begin(), ranges.end(), rng_compare);

		// resolve blocks' children
		for (int i = (int)ranges.size() - 1; i > 0; --i)
		{
			auto& curr = ranges[(uint)i];

			for (int j = i - 1; j >= 0; --j)
			{
				auto& prev = ranges[(uint)j];
				if (prev->e_pos >= curr->e_pos)
				{
					prev->children.insert(prev->children.begin(), curr);
					ranges.erase(ranges.begin() + i);
					break;
				}
			}
		}

		if (!ranges.empty())
		{
			// add parent group if there is no parent wrapping all

			bool add_group = true;
			for (const auto& r : ranges)
			{
				if (r->s_pos == expr_start && r->e_pos == expr_end)
				{
					add_group = false;
					break;
				}
			}

			if (add_group)
			{
				auto grp = ExprRng::create();
				grp->t = ExprRng::t_group;
				grp->s_pos = expr_start;
				grp->e_pos = expr_end;
				grp->def = text.substr_se(grp->s_pos, grp->e_pos);
				grp->text = grp->def;
				grp->def_pos = grp->s_pos;

				grp->children.swap(ranges);
				add_range(grp);
			}
		}

		if (pre_rng)
			ranges.insert(ranges.begin(), pre_rng);

		if (post_rng)
			ranges.insert(ranges.end(), post_rng);

#if ENABLE_LOGGING
		INV_EXP_LOG("\r\n");
		INV_EXP_LOG("#### relations #### \r\n");

		if (!ranges.empty())
		{
			INV_EXP_LOG("\r\n");
			INV_EXP_LOG("-- children\r\n");
			int index = 0;

			for (const auto& rng : ranges)
			{
				INV_EXP_LOG("ranges[" << index++ << "]\r\n");
				log_rng(rng, true);
				INV_EXP_LOG("\r\n");
			}
		}
#endif // ENABLE_LOGGING

		status = status_parsed;
		return !ranges.empty();
	}

	// applies grouping rules
	bool merge(ExprRng::SPtr& prev,                        // previous
	           ExprRng::SPtr& curr,                        // current
	           ExprRng::SPtr& next,                        // next
	           std::function<ExprRng::SPtr(int)> at_offset // at any offset from current
	)
	{
		// #InvertExpression04_MergeFnc

		if (curr->next && curr->prev)
			return false;

		// symbol|op|symbol, or ::symbol in C++
		// ------------------------------------
		// Examples:
		// myNS::myType
		// myClass.method
		// myPtr->method
		// ::GlobalMethod
		if (curr && next && curr->is_operator({L"::", L".", L"->", L"?.", L".*", L"->*"}) && next->is_symbol() &&
		    ((prev && prev->is_symbol()) || (curr->def_is(L"::") && !is_cs)))
		{
			if (prev && prev->is_symbol())
			{
				curr->next = next;
				curr->prev = prev;

				next->prev = curr;
				prev->next = curr;
			}
			else
			{
				curr->next = next;
				next->prev = curr;
			}

			return true;
		}

		// [brace]|[brace] always should be good to merge if there is nothing between
		// note: angle braces should be only on left side, but there is compiler to care about
		// -----------------------------------------------------------------------------------
		// Examples:
		// (bool)(BOOL)
		// (x)(y, z)
		// [x](y, z)
		// <x>(y, z)
		// (x, y)[z]
		if (curr && next && curr->is_brace() && next->is_brace())
		{
			curr->next = next;
			next->prev = curr;
			return true;
		}

		// [symbol]|[brace]
		// ----------------
		// Examples:
		// foo(...)
		// foo[...]
		// foo<...>
		if (curr && next && curr->is_symbol() && next->is_brace())
		{
			_ASSERTE(next->t != ExprRng::t_brace); // expect defined type like t_fnc_brace

			curr->next = next;
			next->prev = curr;
			return true;
		}

		// C-style casting followed by operator or symbol
		// For special cases also followed by ! ~ ++ -- operators
		// if we have only single non-space which is type, then this is C casting
		// -----------------------------------------------------------------------------------
		// Examples:
		// (bool)!
		// (bool)!!Method
		// (bool)!!(FALSE)
		// (int)whatever
		if (curr && next && curr->is_brace(L"(") && next->is_block_start(at_offset))
		{
			bool ok = true;

			// validate the C-style casting,
			// confirm that the child inside (...) is a type
			if (ok && !curr->children.empty() && !is_type(curr->children[0]->def_pos))
			{
				ok = false;
			}

			if (ok)
			{
				curr->next = next;
				next->prev = curr;
				return true;
			}
		}

		// return operator ->
		// ----------------------------------------
		// Examples:
		// (...) -> bool
		if (prev && curr && next && prev->is_brace(L"(") && curr->is_operator({L"->"}) &&
		    next->is_symbol()) // #ModifyExprToDo : should be type??? (is_type)
		{
			curr->next = next;
			curr->prev = prev;

			next->prev = curr;
			prev->next = curr;

			return true;
		}

		// ternary operators ? :
		// ----------------------------------------
		// Examples:
		// expr ? T : F
		if (prev && curr && next && prev->is_block_end(at_offset) && curr->is_operator({L"?", L":"}) &&
		    next->is_block_start(at_offset))
		{
			curr->next = next;
			curr->prev = prev;

			next->prev = curr;
			prev->next = curr;

			return true;
		}

		// merge multiple prefix operators in row !!!! and ~~~~~
		if (curr && next && curr->is_operator({L"!", L"~", L"++", L"--"}) &&
		    next->is_operator({L"!", L"~", L"++", L"--"}))
		{
			curr->next = next;
			next->prev = curr;

			return true;
		}

		// last try to merge something meaningful
		if (curr && next && curr->is_block_start(at_offset, false) &&
		    (next->is_block_start(at_offset) || next->is_block_end(at_offset)))
		{
			curr->next = next;
			next->prev = curr;
			return true;
		}

		return false;
	}

	void prepare_to_merge_siblings()
	{
		// *****************************************************************
		// *****************************************************************
		// assign next/prev for related ranges

		// #InvertExpression06_Joining

		// assign next/prev for related ranges
		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			auto& rngs = top->children;

			int i = 0;

			auto at_offset = [&](int offset) -> ExprRng::SPtr {
				int pos = i + offset;

				if (pos >= 0 && pos < (int)rngs.size())
					return rngs[(uint)pos];

				return nullptr;
			};

			// assign next/prev in merge method
			for (i = (int)rngs.size() - 1; i >= 0; --i)
			{
				ExprRng::SPtr prev = (i > 0) ? rngs[uint(i - 1)] : nullptr;
				ExprRng::SPtr curr = rngs[(uint)i];
				ExprRng::SPtr next = (i + 1 < (int)rngs.size()) ? rngs[uint(i + 1)] : nullptr;

				if (curr->children.size() > 1)
					work_stack.push(curr);

				while (i >= 0 && merge(prev, curr, next, at_offset))
				{
					i--;
					next = curr;
					curr = prev;
					prev = (i > 0) ? rngs[uint(i - 1)] : nullptr;

					if (curr && !curr->children.empty())
						work_stack.push(curr);
				}
			}
		}
	}

	void merge_siblings()
	{
		// #InvertExpression07_Grouping

		// *****************************************************************
		// *****************************************************************
		// process grouping by next/prev
		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			auto& rngs = top->children;

			// merge ranges
			for (int i = (int)rngs.size() - 1; i >= 0; --i)
			{
				auto curr = rngs[(uint)i];
				int first = i;
				int last = i;
				while (curr->prev)
				{
					--i;
					curr = curr->prev;
					first = i;
					_ASSERTE(curr == rngs[(uint)i]);
				}

				if (first != last)
				{
					auto grp = ExprRng::create();
					grp->t = ExprRng::t_group;
					grp->s_pos = rngs[(uint)first]->s_pos;
					grp->e_pos = rngs[(uint)last]->e_pos;
					grp->def = text.substr_se(grp->s_pos, grp->e_pos);
					grp->text = grp->def;
					grp->def_pos = grp->s_pos;

					for (int ch = last; ch >= first; --ch)
					{
						// make ternary group different, so it is easy to identify
						if (grp->t == ExprRng::t_group && rngs[(uint)ch]->is_operator({L"?", L":"}))
							grp->t = ExprRng::t_ternary_group;

						grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
						rngs.erase(rngs.begin() + ch);
					}

					rngs.insert(rngs.begin() + first, grp);

					// validate prev/next
					auto tmp = rngs[(uint)first];
					tmp->prev = (first > 0) ? rngs[uint(first - 1)] : nullptr;
					tmp->next = (first + 1 < (int)rngs.size()) ? rngs[uint(first + 1)] : nullptr;

					if (tmp->prev)
						tmp->prev->next = tmp;

					if (tmp->next)
						tmp->next->prev = tmp;
				}
			}
		}
	}

	// *****************************************************************
	// *****************************************************************
	// split ternary groups

	void split_ternary(ExprRng::SPtr r)
	{
		auto& rngs = r->children;

		// at least five parts: X ? T : F
		if (rngs.size() <= 5)
			return;

		for (int i = (int)rngs.size() - 1; i > 0; --i)
		{
			// find valid ternary expression
			if (i + 3 < (int)rngs.size() && rngs[(uint)i]->is_operator(L"?") && rngs[uint(i + 2)]->is_operator(L":"))
			{
				int first = i - 1;
				int last = i + 3;

				if (first == 0 && last == (int)rngs.size() - 1)
					break;

				auto grp = ExprRng::create();
				grp->s_pos = rngs[(uint)first]->s_pos;
				grp->e_pos = rngs[(uint)last]->e_pos;
				grp->t = ExprRng::t_ternary_group;
				grp->def = text.substr_se(grp->s_pos, grp->e_pos);
				grp->text = grp->def;
				grp->def_pos = grp->s_pos;

				// add to group, remove from source
				for (int ch = last; ch >= first; --ch)
				{
					grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
					rngs.erase(rngs.begin() + ch);
				}

				rngs.insert(rngs.begin() + first, grp);

				auto tmp = rngs[(uint)first];
				tmp->prev = (first > 0) ? rngs[uint(first - 1)] : nullptr;
				tmp->next = (first + 1 < (int)rngs.size()) ? rngs[uint(first + 1)] : nullptr;

				if (tmp->prev)
					tmp->prev->next = tmp;

				if (tmp->next)
					tmp->next->prev = tmp;

				i = first;
			}
		}
	}

	void separate_conditional_expressions()
	{
		// #InvertExpression08_Ternary

		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		// group nested ternary expressions
		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			for (const auto& rng : top->children)
			{
				if (rng->is_ternary_group())
				{
					split_ternary(rng);
				}

				if (!rng->children.empty())
				{
					work_stack.push(rng);
				}
			}
		}
	}

	void remove_redundant_groups()
	{
		// #InvertExpression08_RemoveGroups

		// *****************************************************************
		// *****************************************************************
		// remove groups with single child
		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			while (top && top->children.size() == 1 && top->s_pos == top->children[0]->s_pos &&
			       top->e_pos == top->children[0]->e_pos)
			{
				auto tmp = top->children[0];
				auto prev = top;

				*top = *tmp;

				if (top->is_any_group())
				{
					if (!prev->is_any_group())
						top->t = prev->t;
					else
						top->t = max(top->t, prev->t);
				}
			}

			if (!top->children.empty())
			{
				for (const auto& ch : top->children)
					work_stack.push(ch);
			}
		}
	}

	void fix_relations()
	{
		// #InvertExpression09_FinalRelations

		// *****************************************************************
		// *****************************************************************
		// create parent/child & next/prev relations

		std::stack<ExprRng::SPtr> work_stack;

		auto make_relations = [&](ExprRng::SPtr curr, int x, ExprRng::SPtr top) {
			curr->parent = top;
			curr->prev = (top && (x > 0)) ? top->children[uint(x - 1)] : nullptr;
			curr->next = (top && (x + 1 < (int)top->children.size())) ? top->children[uint(x + 1)] : nullptr;

			// invert anything down from caret position
			// except expressions in: function calls, macros, ternary expressions
			// not containing caret pos (those are considered constant)

			curr->invert_ = curr->in_expr && !curr->is_ternary_group_or_child() &&
			                ((top && top->invert_) || curr->contains(caret_pos));

			// don't invert operators in nested method calls
			if (curr->invert_ && !curr->contains(caret_pos) && curr->is_brace() && curr->prev &&
			    curr->prev->is_symbol())
			{
				curr->invert_ = false;
			}

			if (!curr->children.empty())
				work_stack.push(curr);
		};

		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
		{
			make_relations(*it, -1, nullptr);
			work_stack.push(*it);
		}

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			// generate prev/next
			for (size_t x = 0; x < top->children.size(); x++)
			{
				make_relations(top->children[x], (int)x, top);
			}
		}
	}

	void group_by_logical_operators()
	{
		// #InvertExpression10_SplitByCondiOp

		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			auto& rngs = top->children;

			int last = -1;

			for (int i = (int)rngs.size() - 1; i >= 0; --i)
			{
				auto curr = rngs[(uint)i];

				if (last == -1)
					last = i;

				if (curr->is_logical_expr())
					work_stack.push(curr);

				if (curr->prev && curr->prev->is_operator({L"||", L"&&"}))
				{
					if (last != i)
					{
						auto grp = ExprRng::create();
						grp->t = ExprRng::t_log_expr_group;
						grp->s_pos = curr->s_pos;
						grp->e_pos = rngs[(uint)last]->e_pos;
						grp->def = text.substr_se(grp->s_pos, grp->e_pos);
						grp->text = grp->def;
						grp->def_pos = grp->s_pos;

						for (int ch = last; ch >= i; --ch)
						{
							grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
							rngs.erase(rngs.begin() + ch);
						}

						_ASSERTE(grp->children.size() != 1);

						rngs.insert(rngs.begin() + i, grp);
					}

					last = -1;
					i--; // skip previous
				}
			}

			if (last > 0) // if 0, we would have 1 child group
			{
				auto grp = ExprRng::create();
				grp->t = ExprRng::t_log_expr_group;
				grp->s_pos = rngs.front()->s_pos;
				grp->e_pos = rngs[(uint)last]->e_pos;
				grp->def = text.substr_se(grp->s_pos, grp->e_pos);
				grp->text = grp->def;
				grp->def_pos = grp->s_pos;

				for (int ch = last; ch >= 0; --ch)
				{
					grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
					rngs.erase(rngs.begin() + ch);
				}

				_ASSERTE(grp->children.size() != 1);

				rngs.insert(rngs.begin(), grp);
			}
		}

		fix_relations();
	}

	void group_by_relational_operators()
	{
		using namespace SSParse;

		// #InvertExpression10_SplitByCondiOp

		std::stack<ExprRng::SPtr> work_stack;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				work_stack.push(*it);

		while (!work_stack.empty())
		{
			auto top = work_stack.top();
			work_stack.pop();

			auto& rngs = top->children;

			int last = -1;

			for (int i = (int)rngs.size() - 1; i >= 0; --i)
			{
				auto curr = rngs[(uint)i];

				if (last == -1)
					last = i;

				if (curr->is_relational_expr())
					work_stack.push(curr);

				if (curr->prev && curr->prev->is_operator(OpType::Relational))
				{
					if (last != i)
					{
						auto grp = ExprRng::create();
						grp->t = ExprRng::t_rel_expr_group;
						;
						grp->s_pos = curr->s_pos;
						grp->e_pos = rngs[(uint)last]->e_pos;
						grp->def = text.substr_se(grp->s_pos, grp->e_pos);
						grp->text = grp->def;
						grp->def_pos = grp->s_pos;

						for (int ch = last; ch >= i; --ch)
						{
							grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
							rngs.erase(rngs.begin() + ch);
						}

						_ASSERTE(grp->children.size() != 1);

						rngs.insert(rngs.begin() + i, grp);
					}

					last = -1;
					i--; // skip previous
				}
			}

			if (last > 0) // if 0, we would have 1 child group
			{
				auto grp = ExprRng::create();
				grp->t = ExprRng::t_rel_expr_group;
				grp->s_pos = rngs.front()->s_pos;
				grp->e_pos = rngs[(uint)last]->e_pos;
				grp->def = text.substr_se(grp->s_pos, grp->e_pos);
				grp->text = grp->def;
				grp->def_pos = grp->s_pos;

				for (int ch = last; ch >= 0; --ch)
				{
					grp->children.insert(grp->children.begin(), rngs[(uint)ch]);
					rngs.erase(rngs.begin() + ch);
				}

				_ASSERTE(grp->children.size() != 1);

				rngs.insert(rngs.begin(), grp);
			}
		}

		fix_relations();
	}

	void apply_edit(int pos, int len, LPCWSTR wstr, bool tail) override
	{
		edits.emplace_back(new ExprRng::Edit(pos, len, wstr, tail));

		for (auto it = ranges.crbegin(); it != ranges.crend(); it++)
		{
			if ((*it)->apply_edit(edits.back(), true))
			{
				edits_count++;
				break;
			}
		}
	};

	void save_state()
	{
		for (const auto& rng : ranges)
			rng->save(true);

		edits_count = edits.size();
	}

	void restore_state()
	{
		for (size_t i = ranges.size() - 1; (int)i >= 0; --i)
		{
			if (ranges[i]->saved)
				ranges[i]->restore(true);
			else
			{
				ranges[i]->dispose();
				ranges.erase(ranges.cbegin() + (int)i);
			}
		}

		if (edits.size() > edits_count)
			edits.resize(edits_count);
	}

	bool generate_edits()
	{
		// #InvertExpression11_Edits

		// *****************************************************************
		// *****************************************************************
		// *****************************************************************
		// generate edits

		if (inv_exp_settings.is_parens_removal_only())
		{
			for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
				if ((*it)->in_expr)
					(*it)->remove_redundant_parentheses(this);

			return true;
		}

		std::stack<ExprRng::SPtr> work_stack;

		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
		{
			if ((*it)->in_expr)
				work_stack.push(*it);
		}

		ExprRng::try_invert_stack(this, work_stack, true, true, true);

		// remove empty ranges (mostly removed ! operators)
		std::vector<ExprRng::SPtr> rngs;
		for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
			if ((*it)->in_expr)
				(*it)->get_all_ranges(rngs);

		bool have_toggled_ops = false;
		bool have_any_inversion = false;

		for (auto& r : rngs)
		{
			if (r->inv == ExprRng::invertedBy::ToggledOps)
				have_toggled_ops = true;

			if (r->inv != ExprRng::invertedBy::None)
				have_any_inversion = true;

			if (r->text.empty())
			{
				if (r->parent)
				{
					auto it = std::find(r->parent->children.begin(), r->parent->children.end(), r);
					if (it != r->parent->children.end())
					{
						r->parent->children.erase(it);

						if (r->prev)
							r->prev->next = r->next;

						if (r->next)
							r->next->prev = r->prev;
					}
				}
			}
		}

		// invert whole range to get "no inversion"
		if (inv_exp_settings.transform_expression() && have_any_inversion)
		{
			if (!have_toggled_ops)
				return false;

			for (auto& rng : ranges)
			{
				if (rng->in_expr)
				{
					// first try remove ADDED !
					if (rng->InvertRule_RemoveNot(this, true))
						break;

					// then try the same in nested children
					bool resolved = false;
					auto tmp = rng;
					while (tmp && tmp->children.size() == 1)
					{
						if (tmp->children.front()->InvertRule_RemoveNot(this, true))
						{
							resolved = true;
							break;
						}

						tmp = tmp->children.front();
					}

					// then try whatever else except inverting operators
					if (!resolved && !rng->try_invert(this, false, true, true))
						return false;

					break;
				}
			}
		}

		// remove redundant parentheses
		if (inv_exp_settings.remove_redundant_parentheses())
		{
			for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
				if ((*it)->in_expr)
					(*it)->remove_redundant_parentheses(this);
		}

		return true;
	}

	bool get_before_and_after(_bstr_t& before, _bstr_t& after)
	{
		before = L"";
		after = L"";

		restore_state();

		for (auto& rng : ranges)
		{
			if (rng->in_expr)
			{
				before = rng->text.c_str();
				break;
			}
		}

		if (generate_edits())
		{
			for (auto& rng : ranges)
			{
				if (rng->in_expr)
				{
					after = rng->text.c_str();
					break;
				}
			}
		}
		else
		{
			if (before.length())
				after = (LPCWSTR)before;
			else
				return false;
		}

		return before.length() && after.length();
	}

	bool reparse()
	{
		expr_start = -1;
		expr_end = -1;
		status = status_none;
		status_curpos = UINT_MAX;

		dispose_ranges();

		edits.clear();
		edits_count = 0;

		return prepare();
	}

	bool prepare()
	{
		if (status != status_initialised || update_needed())
		{
			if (!init_ctx())
				return false;
		}

		if (!parse())
			return false;

		prepare_to_merge_siblings();
		merge_siblings();
		separate_conditional_expressions();
		remove_redundant_groups();
		fix_relations();

		// group_by_relational_operators();
		group_by_logical_operators();

		save_state();

#if ENABLE_LOGGING
		INV_EXP_LOG("\r\n");
		INV_EXP_LOG("#### final tree #### \r\n");

		if (!ranges.empty())
		{
			INV_EXP_LOG("\r\n");
			INV_EXP_LOG("-- children\r\n");
			int index = 0;

			for (const auto& rng : ranges)
			{
				INV_EXP_LOG("ranges[" << index++ << "]\r\n");
				log_rng(rng, true);
				INV_EXP_LOG("\r\n");
			}
		}

		auto log_str = log.str();
		// WtMessageBox(WTString(log_str.c_str()), IDS_APPNAME, MB_OK);
#endif // ENABLE_LOGGING

		status = status_prepared;
		return true;
	}
};

END_VA_SMART_SELECT_NS

using namespace VASmartSelect;

bool run_invert_expr_callback(CInvertExpression* pExpr, UINT flags, BSTR* before, BSTR* after)
{
	inv_exp_settings.flags = flags;
	Psettings->mModifyExprFlags = flags & CSettings::ModExpr_SaveMask;

	if (pExpr && before && after)
	{
		// real query

		if (flags & CSettings::ModExpr_Reparse)
			pExpr->reparse();

		_bstr_t _before, _after;
		if (pExpr->get_before_and_after(_before, _after))
		{
			*before = _before.Detach();
			*after = _after.Detach();
			return true;
		}
	}

	return false;
}

bool run_invert_expr(bool check_only)
{
	auto ed = g_currentEdCnt;
	if (ed && Psettings)
	{
		inv_exp_settings.flags = Psettings->mModifyExprFlags;

		ScopedValue toggle(inv_exp_settings.check_only, check_only, false);

		auto expr = CInvertExpression::Get(true);

		if (check_only)
		{
			return expr && expr->init_ctx();
		}

		if (!expr || !expr->prepare())
			return false;

		// case: 146282 - use IVsTextView to insert and select in editor
		auto vs_insert = [&](int spos, int epos, const CStringW& text, bool select) {
			// insert text directly to buffer without having to set selection

			if (ed && expr && spos >= 0 && epos >= 0)
			{
				CTer* cTer = dynamic_cast<CTer*>(ed.get());

				long sposL, eposL;
				ViewCol sposC, eposC;
				CComPtr<IVsTextLines> textLines;
				if (cTer && cTer->m_IVsTextView && SUCCEEDED(cTer->m_IVsTextView->GetBuffer(&textLines)) &&
				    SUCCEEDED(cTer->m_IVsTextView->GetLineAndColumn(spos, &sposL, &sposC)) &&
				    SUCCEEDED(cTer->m_IVsTextView->GetLineAndColumn(epos, &eposL, &eposC)))
				{
					TextSpan ts = {0};
					if (SUCCEEDED(textLines->ReplaceLines(sposL, sposC, eposL, eposC, text, text.GetLength(), &ts)))
					{
						if (select)
						{
							return SUCCEEDED(cTer->m_IVsTextView->SetSelection(ts.iStartLine, ts.iStartIndex,
							                                                   ts.iEndLine, ts.iEndIndex));
						}

						return true;
					}
				}
			}

			return false;
		};

		auto flags = (INT32)Psettings->mModifyExprFlags;

		if (gTestsActive && (flags & CSettings::ModExpr_NoDlg))
		{
			_bstr_t _before, _after;
			if (expr->get_before_and_after(_before, _after))
			{
				CStringW afterWstr((LPCWSTR)_after);
				if (vs_insert(expr->expr_start, expr->expr_end + 1, afterWstr, true))
				{
					// text has changed, so we are in invalid state already
					CleanModifyExpr();
				}
			}
		}
		else
		{
			if (expr->is_cs)
				flags |= CSettings::ModExpr_IsCSharp; // used by dialog for syntax highlighting

			_variant_t args[4] = {(intptr_t)MainWndH, flags, (intptr_t)expr.get(), (intptr_t)&run_invert_expr_callback};
			_variant_t result;
			if (gVaInteropService->InvokeDotNetMethod(IDS_VAWPFSNIPPETEDITOR_DLLW, L"VaWPFTheming.ModifyExprDlg",
			                                          L"DoModal", args, 4, &result) &&
			    result.vt == VT_BSTR && result.bstrVal && *result.bstrVal)
			{
				CStringW selStr(result.bstrVal);

				// select whole expression in IDE
				if (vs_insert(expr->expr_start, expr->expr_end + 1, selStr, true))
				{
					// text has changed, so we are in invalid state already
					CleanModifyExpr();
				}
			}
		}

		return true;
	}

	return false;
}

bool VASmartSelect::CanModifyExpr()
{
	return run_invert_expr(true);
}

bool VASmartSelect::RunModifyExpr()
{
	return run_invert_expr(false);
}

void VASmartSelect::CleanModifyExpr()
{
	CInvertExpression::Get(false).reset();
}
