#include "StdAfxEd.h"
#include "InferType.h"
#include "Mparse.h"
#include "WTString.h"
#include "VAParse.h"
#include "FileTypes.h"
#include "StringUtils.h"
#include "PROJECT.H"
#include "CommentSkipper.h"
#include "IntroduceVariable.h"
#include "VAAutomation.h"
#include "CreateFromUsage.h"
#include "fdictionary.h"
#include "DBQuery.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

static const WTString sGetEnum(":GetEnumerator");

// these are tracking if nested Infer call handled dereferencing
// gmit: remove this when confident enough for Infer to always handle dereferences
static std::mutex threads_where_infer_handled_dereference_mutex;
static std::unordered_set<std::thread::id> threads_where_infer_handled_dereference;

// Given "var v = SomeList()", find definition of SomeList()
class VAParseTypeSymFromDefCls : public VAParseMPScope
{
	WTString mOrgScope;
	WTString mBaseScope;

  public:
	VAParseTypeSymFromDefCls(int ftype)
	    : VAParseMPScope(ftype)
	{
	}

	DTypePtr VAParseTypeSymFromDef(const WTString& orgScope, WTString codeString, MultiParsePtr mp,
	                               bool ignore_local_template_check = true)
	{
		mBaseScope = mOrgScope = orgScope;
		while (mBaseScope.GetLength() > 1)
		{
			DType* cd = mp->FindExact(mBaseScope);
			if (!mBaseScope.GetLength() || (cd && cd->IsType()))
				break;
			mBaseScope.MidInPlace(StrGetSymScope_sv(mBaseScope));
		}
		mIgnoreLocalTemplateCheck = ignore_local_template_check /*true*/; // [case: 78153]
		m_startReparseLine = 0;
		m_parseTo = MAXLONG;
		m_writeToFile = TRUE;
		m_parseGlobalsOnly = TRUE;
		mHighVolumeFindSymFlag = FDF_SlowUsingNamespaceLogic | FDF_NoConcat;
		Parse(codeString, mp);
		return GetCWData(0);
	}

	virtual WTString GetBaseScope()
	{
		return mBaseScope;
	}
	virtual WTString Scope(ULONG deep)
	{
		return mOrgScope;
	}
	virtual void OnCSym()
	{
		VAParseMPReparse::OnCSym();
		if (!m_deep && !wt_isdigit(CurChar()))
			DoScope();
	}
};

enum CastType
{
	castNone,    // no cast
	castFormal,  // const_cast<foo>(bar)
	castInformal // (foo)bar
};

WTString RetouchDefinition(const WTString& origDef, const WTString& symName, CastType& castType)
{
	castType = castNone;
	WTString retval;
	int openParenPos = -1, endCastPos = -1;

	int startPos = origDef.Find('=');
	const bool isAssign = -1 != startPos;
	if (!isAssign)
		startPos = 0;

	int castPos = origDef.Find("_cast", startPos);

	if (isAssign)
	{
		openParenPos = origDef.Find("(", startPos);
		if (-1 == openParenPos && -1 == castPos)
			return retval;

		if (-1 != castPos && (-1 == openParenPos || castPos < openParenPos))
		{
			castPos = origDef.Find("<", castPos);
			if (-1 != castPos)
				endCastPos = origDef.Find(">", castPos);
		}

		if (-1 == endCastPos && -1 != openParenPos && (openParenPos - startPos) < 4)
		{
			const int firstCloseParenPos = origDef.Find(")", openParenPos);
			if (-1 != firstCloseParenPos)
			{
				const int secondOpenParenPos = origDef.Find("(", openParenPos + 1);
				if (-1 == secondOpenParenPos || firstCloseParenPos < secondOpenParenPos)
				{
					// auto foo = (bar*) baz();
					castType = castInformal;
					retval = origDef.Mid(openParenPos + 1, firstCloseParenPos - openParenPos - 1);
					const int ptrPos = origDef.Find("*", openParenPos);
					if (-1 != ptrPos && ptrPos < firstCloseParenPos)
					{
						const int possibleDerefPos = origDef.Find("*");
						if (-1 != possibleDerefPos && possibleDerefPos < ptrPos)
						{
							// auto foo = *(Bar*) Baz();
							retval.ReplaceAll("*", " ");
						}
					}
					retval.TrimRight();
					retval += ' ';
					retval += symName;
					return retval;
				}
			}
		}
	}
	else
	{
		// this is a workaround for something, somewhere in vaparse treating
		// "auto foo(bar)" as a ctor of some sort.  The workaround is to
		// change the def to "auto foo = bar"
		openParenPos = origDef.Find(symName + "(");
		if (-1 != castPos)
		{
			if (-1 == openParenPos || castPos < openParenPos)
			{
				castPos = origDef.Find("<", castPos);
				if (-1 != castPos)
					endCastPos = origDef.Find(">", castPos);
			}
			else if (castPos > openParenPos)
			{
				// auto foo(reinterpret_cast<Bar*>(Baz()));
				int castPosTmp = origDef.Find("<", castPos);
				if (-1 != castPos)
				{
					endCastPos = origDef.Find(">", castPosTmp);
					if (-1 != endCastPos)
					{
						castPos = castPosTmp;
						openParenPos = -1;
					}
				}
			}
		}

		if (-1 == openParenPos && -1 == endCastPos)
			return retval;

		if (-1 == endCastPos)
		{
			WTString newDef = origDef.Left(symName.GetLength() + openParenPos);
			newDef += " = ";

			WTString defPart2 = origDef.Mid(symName.GetLength() + openParenPos + 1);
			defPart2.TrimLeft();
			int pos = defPart2.Find(')');
			if (-1 != pos)
			{
				int secondOpenPos = defPart2.Find('(');
				if (0 == secondOpenPos || (secondOpenPos == 1 && defPart2[0] == '*'))
				{
					pos = defPart2.Find(")", secondOpenPos);
					if (-1 != pos)
					{
						// auto foo((bar*) baz())
						// turn into "bar * foo"
						castType = castInformal;
						newDef = defPart2.Mid(secondOpenPos + 1, pos - 1);
						const int ptrPos = defPart2.Find("*", openParenPos);
						if (-1 != ptrPos && ptrPos < pos)
						{
							const int possibleDerefPos = defPart2.Find("*");
							if (-1 != possibleDerefPos && possibleDerefPos < ptrPos)
							{
								// auto foo(*(Bar*) Bax())
								newDef.ReplaceAll("*", " ");
							}
						}
						newDef.TrimRight();
						newDef += ' ';
						newDef += symName;
						return newDef;
					}
				}

				if (-1 != secondOpenPos && secondOpenPos < pos)
				{
					// auto foo(bar())
					pos = defPart2.Find(")", pos);
					secondOpenPos = -1;
				}

				if (-1 == secondOpenPos && -1 != pos)
				{
					newDef += defPart2.Left(pos);
					newDef += defPart2.Mid(pos + 1);
					return newDef;
				}
			}
		}
	}

	if (-1 != castPos && -1 != endCastPos)
	{
		castType = castFormal;
		retval = origDef.Mid(castPos + 1, endCastPos - castPos - 1);
		const int ptrPos = origDef.Find("*", castPos);
		if (-1 != ptrPos && ptrPos < endCastPos)
		{
			const int possibleDerefPos = origDef.Find("*");
			if (-1 != possibleDerefPos && possibleDerefPos < ptrPos)
			{
				// auto foo = *reinterpret_cast<Bar*>(Baz())
				retval.ReplaceAll("*", " ");
				retval.TrimRight();
			}
		}
		retval += " ";
		retval += symName;
	}

	return retval;
}

WTString GetDefForInference(MultiParsePtr& mp, const WTString& orig, BOOL& isLambda)
{
	isLambda = FALSE;
	WTString decodedDef(DecodeScope(orig));
	const int openBracketPos = decodedDef.Find('[');
	if (-1 == openBracketPos)
		return decodedDef;

	const int closeBracketPos = decodedDef.Find(']', openBracketPos);
	if (-1 != closeBracketPos && closeBracketPos < decodedDef.GetLength())
	{
		if (decodedDef[closeBracketPos + 1] == ')')
		{
			// auto foo(new int[bar]);
			decodedDef.LeftInPlace(closeBracketPos + 2);
			return decodedDef;
		}

		const int openParenPos = decodedDef.Find('(', closeBracketPos);
		if (-1 != openParenPos)
		{
			const int closeParenPos = decodedDef.Find(')', openParenPos);
			if (-1 != closeParenPos)
			{
				int retTypePos = decodedDef.Find("->", closeParenPos);
				if (-1 != retTypePos)
				{
					// auto v1 = [....] (....) -> BOOL
					isLambda = TRUE;
					decodedDef.MidInPlace(retTypePos + 2);
					TokenGetField2InPlace(decodedDef, '\f');
					TokenGetField2InPlace(decodedDef, '{');
					decodedDef.ReplaceAll("*", "");
					decodedDef.ReplaceAll("^", "");
					decodedDef.ReplaceAll("&", "");
					decodedDef.ReplaceAll("%", "");
					decodedDef.Trim();
					return decodedDef;
				}

				int assignPos = decodedDef.Find('=');
				if (-1 != assignPos && assignPos < openBracketPos)
				{
					std::string_view tmp;
					++assignPos;
					if (openBracketPos != assignPos)
					{
						tmp = decodedDef.mid_sv((uint32_t)assignPos, uint32_t(openBracketPos - assignPos));
						if(tmp.find_first_not_of(" \r\n\t") == tmp.npos)
							tmp = {};
					}

					if (tmp.empty())
					{
						// auto v1 = [....] (....)
						isLambda = TRUE;
						// TODO: [case: 86390] need return type deduction
						// ResolveLambdaType(mp); ??
					}
				}
			}
		}
	}

	TokenGetField2InPlace(decodedDef, '[');
	return decodedDef;
}

std::optional<token2> handle_std_get(MultiParsePtr mp, const token2& tdef, const WTString& vscope,
                                     int chained_recursion = 0)
{
	// This function will try to detect std::get<> and tranform it into an expression that
	// existing 'auto type deduction' logic will know how to handle.

	// Helper internal c++ constructs (defined in Project.cpp, GetSysDic):
	// template <typename TYPE> TYPE __va_passthrough_type(TYPE t);
	// template <typename TYPE> TYPE __va_passthrough_type2();
	// using __va_void = void;
	//
	// __va_passthrough_type will return the type of given expression
	// __va_passthrough_type2 will return the type given in a template parameter
	// both are used to get types using existing parser routines

	try
	{
		// matches something like: auto autovar = std ::   get<std::optional<abc>>(xy)
		// group1: prefix, "auto autovar =" part
		// group2: "get" or "get_if"
		// group3: get index, such as "3" in get<3>
		// group4: get type, "std::optional<abc>"
		// group5: expression, "xy"

		static const std::regex r_get(
			R"(^([^=:]+[=:]\s*)(?:(?:(?:::\s*)?std\s*::)?\s*(get|get_if)\s*<(?:([0-9]+)|(.*))>\s*\()(.*)\))",
		    std::regex_constants::ECMAScript | std::regex_constants::optimize);
		std::cmatch m_get;
		if (!std::regex_match(tdef.cbegin(), tdef.cend(), m_get, r_get))
			return std::nullopt;
		if (m_get.size() != 6)
			return std::nullopt;

		std::string g_var_declaration = m_get[1];
		std::string g_get = m_get[2];
		std::string g_get_index = m_get[3];
		std::string g_get_type = m_get[4];
		std::string g_expression = m_get[5];
		if (g_get_index.empty() && g_get_type.empty())
			return std::nullopt;
		if (g_var_declaration.find("auto") == g_var_declaration.npos)
			return std::nullopt; // sanity check

		// try to handle chained std::gets
		bool was_chained = false;
		if (g_expression.find("get") != g_expression.npos)
		{
			auto chained_type =
			    handle_std_get(mp, (g_var_declaration + g_expression).c_str(), vscope, chained_recursion + 1);
			if (chained_type)
			{
				g_expression = chained_type->c_str().c_str();
				was_chained = true;
			}
		}

		// get the expression type
		VAParseTypeSymFromDefCls c(mp->FileType());
		/*auto*/ std::string
		    expr_to_get_type /*= g_var_declaration*/; // commented out since a declaration from foreach (with : instead
		                                              // of =) was not recognized, but it seems to work without it
		if (!was_chained)
			expr_to_get_type += " __va_passthrough_type(" + g_expression + ")"; // note: even though getting the type of g_expression would be the same, VAParseTypeSymFromDef is quite fragile and using this wrapper gave me better results
		else
			expr_to_get_type += g_expression; // g_expression will be replaced with __vapassthrough_type2
		DTypePtr tuple_type = c.VAParseTypeSymFromDef(vscope, WTString(expr_to_get_type), mp);
		if (!tuple_type)
			tuple_type = c.VAParseTypeSymFromDef(vscope, WTString(expr_to_get_type), mp); // for some reason, first call often fails and second call succeeds; this seems to be consistent (likely some template instantiation issue/feature)
		if (!tuple_type)
			return std::nullopt;

		tuple_type->LoadStrs(true);
		//WTString def = tuple_type->Def();
		// int i = def.find_last_of(" "); // uncomment to strip variable name, but it doesn't seem to be a problem
		// if (i != -1)
		// def = def.Left(i);

		// adjust handling to supported std classes
		WTString def_decoded = tuple_type->Def();
		DecodeTemplatesInPlace(def_decoded, Src);
		std::function<int()> get_index;
		if (def_decoded.begins_with("std.tuple<") || def_decoded.begins_with("std.pair<") || def_decoded.begins_with("std.variant<"))
		{
			get_index = [&g_get_index] { return std::stoi(g_get_index); };
		}
		else if (def_decoded.begins_with("std.array<"))
		{
			get_index = [] { return 0; };
		}
		else
			return std::nullopt;

		// get final type
		std::optional<WTString> type;
		if (!g_get_index.empty())
		{
			// handle std::get<index>
			std::vector<WTString> templates;

			void SplitTemplateArgs(const WTString& templateFormalDeclarationArguments,
			                       std::vector<WTString>& templateFormalDeclarationArgumentList);
			SplitTemplateArgs(def_decoded, templates);

			if (!templates.empty())
			{
				int index = get_index();
				if (index >= 0 && index < (int)templates.size())
					type = templates[(uint)index];
			}
		}
		else if (!g_get_type.empty())
			type = g_get_type; // handle std::get<type>

		if (!type)
			return std::nullopt;

		// GetTypesFromDef seems to work incorrectly if type is void * and I don't know how to fix it.
		// This is the workaround: try to detect void * and temporarily change the void type to __va_void.

		// std::regex doesn't support look behind, so it is matched first
		static const std::regex r_void_lookbehind(
			R"(^((?:const)?\s*)(?=void)(?:.*))", 
			std::regex_constants::ECMAScript | std::regex_constants::optimize);
		static const std::regex r_void(
			R"(void(?=\s*(?:const\s*)?\*))", 
			std::regex_constants::ECMAScript | std::regex_constants::optimize);
		std::cmatch m_void_lookbehind;
		if (std::regex_match(type->c_str(), m_void_lookbehind, r_void_lookbehind) && (m_void_lookbehind.size() == 2))
		{
			// m_void_lookbehind[1] will contain all chars before void
			assert(m_void_lookbehind[1].length() <= type->length());
			*type = m_void_lookbehind[1] + std::regex_replace(type->c_str() + m_void_lookbehind[1].length(), r_void,
			                                                  "__va_void", std::regex_constants::format_first_only);
		}

		// compose the final type
		bool is_get_if = g_get == "get_if";
		WTString new_def;
		if (chained_recursion == 0)
			new_def += g_var_declaration; // if chained, we just need a type expression
		new_def += {"__va_passthrough_type2<", *type, (is_get_if ? "*" : ""), ">()"};
		c.VAParseTypeSymFromDef(vscope, new_def, mp); // dummy
		return new_def;
	}
	catch (const std::exception& /*e*/)
	{
		return std::nullopt;
	}
}

#pragma warning(disable : 4706)
static std::tuple<bool, bool> trim_const_volatile(WTString& str, bool right = true)
{
	bool const_trimmed = false;
	bool volatile_trimmed = false;

	for (bool retry = true; retry;)
	{
		if (right)
			str.TrimRight();
		else
			str.TrimLeft();
		retry = false;

		if (!const_trimmed && (const_trimmed = right ? str.TrimWordRight("const", false) : str.TrimWordLeft("const", false)))
			retry = true;
		if (!volatile_trimmed && (volatile_trimmed = right ? str.TrimWordRight("volatile", false) : str.TrimWordLeft("volatile", false)))
			retry = true;
	}
	return {const_trimmed, volatile_trimmed};
};
#pragma warning(default : 4706)

DTypePtr InferTypeFromAutoVar(DTypePtr symDat, MultiParsePtr mp, int devLang, BOOL forBcl /*= FALSE*/)
{
	assert(mp);
	assert(symDat);
	if (!symDat)
		return {};

	if (symDat->type() != LINQ_VAR)
	{
		_ASSERTE(!"InferTypeFromAutoVar called on DType that is not LINQ_VAR");
		return symDat;
	}

	if (symDat->IsDontExpand())
	{
		// already processed
		return symDat;
	}

	BOOL isLambda = FALSE;
	const WTString symDatDef(symDat->Def());
	const WTString origDef(GetDefForInference(mp, symDatDef, isLambda));
	if (isLambda && !forBcl)
	{
		// [case: 57605]
		// only proceed with lambdas when used for BCL lookup
		return symDat;
	}

	DType* pSymDatInHashTable = mp->FindMatch(symDat);

	// prevent repeated parsing
	symDat->DontExpand();
	if (pSymDatInHashTable)
		pSymDatInHashTable->DontExpand();

	WTString op;
	if (!isLambda && -1 != symDatDef.Find(_T('[')) && -1 == origDef.Find(_T('[')))
	{
		// [case: 71133]
		op = _T("[]");
	}

	// [case: 85849]
	const WTString combinedSymScope(CombineOverlappedScope(mp->m_argScope, symDat->SymScope()));
	const WTString symDatSymScope(combinedSymScope.IsEmpty() ? symDat->SymScope() : combinedSymScope);
	const WTString symDatSym = StrGetSym(symDatSymScope);
	const WTString vscope = StrGetSymScope(symDatSymScope);
	CastType cast = castNone;
	WTString retouchedDef(RetouchDefinition(origDef, symDatSym, cast));
	token2 tdef;
	if (retouchedDef.IsEmpty())
		tdef = origDef;
	else
		tdef = retouchedDef;

	bool isPointer = false;
	bool isHatPointer = false;
	bool extractTypeFromIterator = false;
	bool rangeBasedFor = false;
	bool va_ptr_substitution = false; // pointers are converted to __va_ptr wrapper

	/*static*/ auto VAParseTypeSymFromDef_with_retry = [](VAParseTypeSymFromDefCls& c, const WTString& vscope, const WTString& expr1, MultiParsePtr mp) {
		DTypePtr type = c.VAParseTypeSymFromDef(vscope, expr1, mp);
		// a single retry sometimes/often helps to resolve type; it's unknown why
		if (!type)
			type = c.VAParseTypeSymFromDef(vscope, expr1, mp);
		return type;
	};

	if (IsCFile(devLang))
	{
		if (auto new_tdef = handle_std_get(mp, tdef, vscope); new_tdef)
			retouchedDef = tdef = *new_tdef;

		try
		{
			// tdef: "auto cptrcvptr2cd = *cptrcvptrc"
			auto ii = tdef.find('=');
			// if (tdef.begins_with("auto cptrcvptr2c"))
			// __debugbreak();

			if (ii != NPOS)
			{
				auto expr = tdef.substr(ii + 1);
				expr.Trim();
				if (!expr.IsEmpty())
				{
					VAParseTypeSymFromDefCls c(mp->FileType());

					// In the original code, infer logic is processed in both parser and InferType::Infer, starting from
					// InferTypeFromAutoVar where there is even more processing.
					// Wrapping expression into __va_passthrough_type will bring code execution to VAParseMPScope::DoScope
					// which will call GetTemplateFunctionCallTemplateArg that calls InferType::Infer for the each
					// parameter (we have one). Resolved type will be returned from __va_passthrough_type and DoScope will
					// continue with it. Otherwise, InferType::Infer wouldn't be called always.

					// #AUTO_INFER 1st step
					WTString expr1;
					if (expr.begins_with("__va_passthrough_type"))
						expr1 = expr;
					else
						expr1 = "__va_passthrough_type(" + expr + ")";
					DTypePtr type = VAParseTypeSymFromDef_with_retry(c, vscope, expr1, mp);

					if (type)
					{
						auto def = type->Def();

						if (def.begins_with("UnknownType") || def.begins_with("auto"))
						{
							// retry with the original expression, maybe we'll be more lucky
							DTypePtr type2 = VAParseTypeSymFromDef_with_retry(c, vscope, expr, mp);
							if (type2)
								def = type2->Def();

							/* expr = expr.Mid(1);
							expr1 = "__va_passthrough_type(" + expr + ")";
							DTypePtr type3 = VAParseTypeSymFromDef_with_retry(vscope, expr1, mp);
							if (type3)
							    def = type3->Def();

							type2 = VAParseTypeSymFromDef_with_retry(vscope, expr, mp);
							if (type2)
							    def = type2->Def();*/
						}

						def = DecodeTemplates(def, Src);
						auto i = def.Find("__va_passthrough_type");
						if (i != NPOS)
						{
							def = def.Left(i);
							def.Trim();
							trim_const_volatile(def);
							bool is_pointer = def.EndsWith("*");

							do
							{
								if (is_pointer)
								{
									// The code below works with only one pointer level and I don't know how to fix it.
									// As a workaround, I temporarily convert all pointers to __va_ptr wrappers and
									// restore them back to pointers later

									// First I get all pointers, along with const and volatile modifiers
									std::list<std::tuple<bool, bool>> ptrs;
									while (def.EndsWith("*"))
									{
										def = def.Left(def.length() - 1);
										def.TrimRight();
										ptrs.emplace_front(trim_const_volatile(def));
									}

									if (ptrs.empty())
										break;

									auto [bc, bv] = trim_const_volatile(def, false); // trim const from the beginning
									std::get<0>(ptrs.front()) |= bc;
									std::get<1>(ptrs.front()) |= bv;

									// Convert to wrappers
									for (auto [_const, _volatile] : ptrs)
									{
										def = "__va_ptr<" + def + ", ";
										if (_const && _volatile)
											def += "__va_ptr_const_volatile";
										else if (_const)
											def += "__va_ptr_const";
										else if (_volatile)
											def += "__va_ptr_volatile";
										else
											def += "__va_ptr_none";
										def += ">";
									}
									def = "__va_passthrough_type2<__va_ptr_begin<" + def + ", __va_ptr_end>>()";
									isPointer = true;
									va_ptr_substitution = true;
								}
								else
								{
									// deference operators handled at Infer
									bool dereferenced = false;
									{
										std::lock_guard l{threads_where_infer_handled_dereference_mutex};
										auto it = threads_where_infer_handled_dereference.find(std::this_thread::get_id());
										dereferenced = it != threads_where_infer_handled_dereference.end();
										if (dereferenced)
											threads_where_infer_handled_dereference.erase(it);
									}
									if (!dereferenced)
										break;

									def = "__va_passthrough_type2<" + def + ">()";
									isPointer = false;
								}

								// get and cleanup initializer expression
								def = tdef.Left(ii + 1) + def;
								while ((ii > 0) && isspace(tdef[--ii]))
								{
								}
								while ((ii > 0) && ISCSYM(tdef[ii - 1]))
									--ii;

								retouchedDef = tdef = def;
							} while (false);
						}
					}
				}
			}
		}
		catch (const std::exception&)
		{
			return {};
		}
	}

	VAParseTypeSymFromDefCls c(mp->FileType());
	DTypePtr cdDef = VAParseTypeSymFromDef_with_retry(c, vscope, tdef, mp);
	if (cdDef)
		cdDef->LoadStrs();
#ifdef _DEBUG
	auto cdDef_Def = cdDef ? cdDef->Def() : "NULL";
#endif
	if (cdDef && Is_C_CS_File(devLang) && -1 != tdef.Find(" in "))
	{
		// [case: 24363]
		extractTypeFromIterator = true;
	}

	if (IsCFile(devLang))
	{
		if (cdDef && cdDef->Scope() == ":std")
		{
			WTString sym(cdDef->Sym());
			if ((-1 != sym.Find("begin") || -1 != sym.Find("end")))
			{
				// [case: 70481]
				// begin cbegin rbegin crbegin end cend rend crend
				if (sym == "begin" || sym == "cbegin" || sym == "rbegin" || sym == "crbegin" || sym == "end" ||
				    sym == "cend" || sym == "rend" || sym == "crend")
				{
					// change cdDef to the type passed into sym
					int pos = tdef.Find(sym);
					if (-1 != pos)
					{
						sym = tdef.Mid(pos + sym.GetLength() + 1);
						sym.TrimLeft();
						if (sym.GetLength())
						{
							cdDef = c.VAParseTypeSymFromDef(vscope, sym, mp);
							extractTypeFromIterator = true;
							isPointer = true;
						}
					}
				}
			}
		}

		if (!cdDef)
		{
			WTString tmp(::ReadToUnpairedColon(tdef));
			if (!tmp.IsEmpty())
			{
				// [case: 69242] range-based for loop
				tmp = tdef.Mid(tmp.GetLength() + 1);
				if (tmp.GetLength())
				{
					if (tmp[0] == ':')
						tmp = tmp.Mid(1);
					tmp.TrimLeft();
					cdDef = c.VAParseTypeSymFromDef(vscope, tmp, mp);
					if (cdDef)
					{
						rangeBasedFor = extractTypeFromIterator = true;
						if (tmp[0] == '*')
						{
							// [case: 97149]
							op = "*";
						}
					}
				}
			}
		}
	}

	if (castFormal != cast)
	{
		if (op.IsEmpty())
		{
			const WTString theDef(retouchedDef.IsEmpty() ? origDef : retouchedDef);
			const int startPos = theDef.Find('=');
			if (-1 != startPos)
			{
				const int secondEqualPos = theDef.Find("=", startPos + 1);
				WTString rightLnText(theDef);
				rightLnText = rightLnText.Mid(startPos + 1);
				rightLnText.Trim();

				if (cdDef)
				{
					if (IsCFile(devLang))
					{
						const int ptrPos = theDef.Find("*", startPos);
						if (-1 != ptrPos && ptrPos < startPos + 3)
						{
							if (-1 == secondEqualPos || secondEqualPos != (startPos + 1))
							{
								int openParenPos = theDef.Find("(", startPos);
								if (-1 == openParenPos || ptrPos < openParenPos)
								{
									// [case: 72600] auto v1 = *iter;
									op = "*";
								}
							}
						}
					}

					if (-1 == secondEqualPos || secondEqualPos != (startPos + 1))
					{
						if (cdDef->SymScope().GetTokCount(DB_SEP_CHR) == 1)
						{
							WTString tmp(rightLnText);
							WTString cdDefSym(cdDef->Sym());
							if (tmp == cdDefSym && cdDef->type() != VAR) // [case: 89121]
							{
								// [case: 88017]
								if (!IS_OBJECT_TYPE(cdDef->type()) || cdDef->SymMatch("nullptr"))
								{
									// [case: 86108]
									// given:
									//		auto foo = SOME_MACRO_OR_RESERVED_WORD;
									// don't generate:
									//		SOME_MACRO foo = SOME_MACRO_OR_RESERVED_WORD;
									// but allow:
									//		auto bar = SomeClass;
									cdDef = nullptr;
								}
							}
						}
					}
				}

				if (!cdDef && !isLambda && !rightLnText.IsEmpty())
				{
					// [case: 86108]
					WTString inferFrom;

					// assert(devLang == gTypingDevLang);
					LiteralType strType = ::GetLiteralType(devLang, rightLnText, '"');
					if (strType == LiteralType::None)
						strType = ::GetLiteralType(devLang, rightLnText, '\'');

					if (strType != LiteralType::None)
						inferFrom = rightLnText; // [case: 48375] don't pass strings to TokenGetField
					else
						inferFrom = ::TokenGetField(
						    rightLnText,
						    "; ()+=*\\<>-:"); // [case: 31924] don't stop at '.' before the simple type check

					LPCSTR typeStr = ::SimpleTypeFromText(devLang, inferFrom);
					if (typeStr)
					{
						WTString simpleSymScope(typeStr);
						if (simpleSymScope[0] != DB_SEP_CHR)
							simpleSymScope = DB_SEP_STR + simpleSymScope;
						simpleSymScope.ReplaceAll("::", DB_SEP_STR);
						cdDef = std::make_shared<DType>(simpleSymScope, NULLSTR, (uint)RESWORD, 0u, 0u);
					}
				}
			}
		}
	}

	if (!cdDef)
	{
		if (mp->m_argScope.IsEmpty())
		{
			// [case: 85849]
			// retry when m_argScope is set
			symDat->FlipExpand();
			if (pSymDatInHashTable)
				pSymDatInHashTable->FlipExpand();
			return symDat;
		}

		return symDat;
	}

	if (isLambda)
	{
		// [case: 57605] do not modify DType of auto vars that are lambdas
		_ASSERTE(forBcl);
		return cdDef;
	}

	if (!op.IsEmpty())
	{
		// [case: 71133]
		DType* opData = mp->FindOpData(op, cdDef->SymScope(), nullptr);
		if (opData)
			cdDef = std::make_shared<DType>(opData);
	}

	bool changedPtrAttrDueToImplicitDef = false;
	if (!retouchedDef.IsEmpty() && castNone != cast)
	{
		DType* pCdDefInHashTable = mp->FindMatch(cdDef);
		cdDef->SetDef(retouchedDef);
		if (pCdDefInHashTable)
			pCdDefInHashTable->SetDef(retouchedDef);
		if (!cdDef->IsPointer())
		{
			if ((-1 != retouchedDef.FindOneOf("*^")))
			{
				cdDef->SetPointer();
				if (pCdDefInHashTable)
					pCdDefInHashTable->SetPointer();
			}
			else if (mp->IsPointer(retouchedDef))
			{
				changedPtrAttrDueToImplicitDef = true;
				cdDef->SetPointer();
				if (pCdDefInHashTable)
					pCdDefInHashTable->SetPointer();
			}
		}
	}

	bool deepModify = false;
	bool isDeref = false;
	bool isPtrToPtr = false;
	WTString symscope;
	const int cdType = (int)cdDef->MaskedType();

	// change def of symDat in memory to definition of assignment
	if (FUNC == cdType || LINQ_VAR == cdType || VAR == cdType || PROPERTY == cdType)
	{
		// [case: 52398] show Func return type instead of Func itself
		symscope = ::GetTypesFromDef(cdDef.get(), LINQ_VAR == cdType ? FUNC : cdType, devLang);
		int pos = symscope.Find('\f');
		if (-1 != pos)
		{
			int pos2 = symscope.Find(":alias");
			if (-1 != pos2 && pos2 < pos)
			{
				symscope = symscope.Mid(pos + 1);
				pos = symscope.Find('\f');
				if (-1 == pos)
					pos = symscope.GetLength();
			}
			symscope = symscope.Left(pos);
		}

		// [case: 86189] qualify types with full scope
		QualifyTypeWithFullScope(symscope, cdDef.get(), mp.get(), vscope, NULLSTR);

		if ((FUNC == cdType || VAR == cdType || (LINQ_VAR == cdType && extractTypeFromIterator)) &&
		    !symscope.IsEmpty() && (extractTypeFromIterator || -1 != symDatSymScope.Find(":foreach")))
		{
			// [case: 76995] support for 'for each (var x in y)'
			WTString tmp(::CleanScopeForDisplay(symDatSymScope));
			// v1 and v2 have same scope (once cleaned), but v2 should not look for enumerator (it is the collection):
			//	for each (auto v1 in y)
			//		if (true)
			//			auto v2 = y;
			if (extractTypeFromIterator || (-1 != origDef.Find(" in ") && (-1 != tmp.Find(".foreach." + symDatSym) ||
			                                                               -1 != tmp.Find(".foreach.if." + symDatSym))))
			{
				DTypePtr tmpCdDef(cdDef);
				tmp = symscope + sGetEnum;
				// [case: 24363][case: 80380] instantiate templates by getting bcl
				WTString bcl(mp->GetBaseClassList(symscope));
				DType* iteratorDtype = nullptr;
				DType* enumerableDtype = mp->FindExact2(tmp, false);
				if (!enumerableDtype && !bcl.IsEmpty() &&
				    (Defaults_to_Net_Symbols(devLang) || GlobalProject->CppUsesClr() || GlobalProject->CppUsesWinRT()))
				{
					// [case: 962][case: 76995] addl support for 'for each (var x in y)'
					enumerableDtype = mp->FindSym(&sGetEnum, &symscope, &bcl, FDF_NoConcat);
					if (enumerableDtype)
					{
						enumerableDtype->LoadStrs();
						symscope = enumerableDtype->SymScope();
					}
				}

				if (!enumerableDtype && IsCFile(devLang))
				{
					// [case: 69242] [case: 24363]
					// see if we can find a begin method
					const WTString kBegin(":begin");
					tmp = symscope + kBegin;
					enumerableDtype = mp->FindExact2(tmp, false);
					if (!enumerableDtype)
					{
						// [case: 86248]
						// see if we can directly find an iterator type
						// rather than indirectly via begin/end.
						// Note the funky substitution made at goto:#parseBadTemplateTypenameSubst
						// can wreak havoc.  Addressed std containers via installed stdafx.h
						// rather than changing SubstituteTemplateInstanceDefText at this time.
						const WTString kIterator(":iterator");
						tmp = symscope + kIterator;
						iteratorDtype = mp->FindExact2(tmp, false);
					}

					if (!iteratorDtype && !enumerableDtype && !bcl.IsEmpty())
					{
						// [case: 80380]
						enumerableDtype = mp->FindSym(&kBegin, &symscope, &bcl, FDF_NoConcat);
						if (enumerableDtype)
						{
							// [case: 86143]
							const WTString enumSymScope(enumerableDtype->SymScope());
							if (enumSymScope == ":begin" || enumSymScope == ":std:begin" ||
							    enumSymScope == ":Platform:begin")
								enumerableDtype = nullptr;
							else
							{
								enumerableDtype->LoadStrs();
								symscope = enumerableDtype->Scope();
							}
						}
					}

					if (enumerableDtype)
					{
						// found a begin method; get the return type of the method
						tmp = ::GetTypesFromDef(enumerableDtype, FUNC, devLang);
						if (tmp.IsEmpty())
							enumerableDtype = nullptr;
						else
						{
							// tmp == "iterator..."
							int pos2 = tmp.Find('\f');
							if (-1 != pos2)
								tmp = tmp.Left(pos2);

							// tmp = "std::vector<Foo>" + ":iterator"
							tmp = symscope + tmp;
							enumerableDtype = mp->FindExact2(tmp, false);
							if (enumerableDtype)
							{
								// found the iterator; deref if appropriate
								if (extractTypeFromIterator)
									op = "*";
								if (op == "*")
								{
									// [case: 71133]
									DType* opData = mp->FindOpData(op, enumerableDtype->SymScope(), nullptr);
									if (opData)
										tmpCdDef = std::make_shared<DType>(opData);
								}
							}
						}
					}
					else if (iteratorDtype)
						enumerableDtype = iteratorDtype;
				}

				if (enumerableDtype)
				{
					tmp = ::DecodeScope(enumerableDtype->Def());
					int pos3 = tmp.Find('\f');
					if (-1 != pos3)
						tmp = tmp.Left(pos3);

					const int openTemplatePos = tmp.Find('<');
					const int openParenPos = openTemplatePos == -1 ? -1 : tmp.Find('(');
					if (-1 != openTemplatePos && (-1 == openParenPos || openTemplatePos < openParenPos) &&
					    enumerableDtype != iteratorDtype)
					{
						tmp = tmp.Mid(openTemplatePos + 1);
						if (!tmp.IsEmpty())
						{
							int tmpPos2 = tmp.Find('>');
							if (-1 != tmpPos2)
							{
								ReadToCls rtc(devLang);
								tmp = rtc.ReadTo(tmp, ",>;{\f");
								if (!tmp.IsEmpty())
								{
									symscope = tmp;
									retouchedDef = tmp + " " + symDatSym;

									DTypePtr newDef = std::make_shared<DType>(tmpCdDef.get());
									if (newDef)
									{
										newDef->SetDef(retouchedDef);
										cdDef = newDef;
									}
								}
							}
						}
					}
					else if (Defaults_to_Net_Symbols(devLang) || GlobalProject->CppUsesClr() ||
					         GlobalProject->CppUsesWinRT())
					{
						// find type returned by GetEnumerator
						bool lastResort = true;
						tmp = ::GetTypesFromDef(enumerableDtype, FUNC, devLang);
						if (!tmp.IsEmpty())
						{
							// find dtype for tmp (it's an IEnumerator)
							int pos4 = tmp.Find('\f');
							if (-1 != pos4)
								tmp = tmp.Left(pos4);
							DType* saveEnumDtype = enumerableDtype;
							const WTString enumScope(enumerableDtype->Scope());
							enumerableDtype = mp->FindSym(&tmp, &enumScope, &bcl, FDF_NoConcat);
							if (enumerableDtype)
							{
								// lookup IEnumerator.Current
								const WTString savTmp(tmp);
								tmp = enumerableDtype->SymScope() + ":Current";
								enumerableDtype = mp->FindExact2(tmp, false);
								if (enumerableDtype)
								{
									// found it, now what does type it return
									tmp = ::GetTypesFromDef(enumerableDtype, FUNC, devLang);
									pos4 = tmp.Find('\f');
									if (-1 != pos4)
										tmp = tmp.Left(pos4);
									if (!tmp.IsEmpty())
									{
										enumerableDtype = mp->FindSym(&tmp, &enumScope, &bcl, FDF_NoConcat);
										if (enumerableDtype)
										{
											// good, fully scoped (otherwise tmp might not be fully scoped)
											tmp = enumerableDtype->SymScope();
											pos4 = tmp.Find('\f');
											if (-1 != pos4)
												tmp = tmp.Left(pos4);
										}

										symscope = tmp;
										retouchedDef = symscope + " " + symDatSym;

										DTypePtr newDef = std::make_shared<DType>(tmpCdDef.get());
										if (newDef)
										{
											newDef->SetDef(retouchedDef);
											cdDef = newDef;
										}

										lastResort = false;
									}
								}
								else if (iteratorDtype)
								{
									symscope = savTmp;
									enumerableDtype = iteratorDtype;
								}
							}
							else
								enumerableDtype = saveEnumDtype;
						}

						if (lastResort && -1 != symscope.Find(sGetEnum))
						{
							// [case: 962][case: 76995] default to Object for type
							symscope = "Object";
							retouchedDef = symscope + " " + symDatSym;

							DTypePtr newDef = std::make_shared<DType>(tmpCdDef.get());
							if (newDef)
							{
								newDef->SetDef(retouchedDef);
								cdDef = newDef;
							}
						}
					}
				}
			}
		}

		if (!symscope.IsEmpty() && symscope[0] == ':')
			symscope = symscope.Mid(1);
		if (symscope == "alias")
			symscope.Empty();

		if ((FUNC == cdType || VAR == cdType) && !symscope.IsEmpty() && IsCFile(devLang))
		{
			if (FUNC == cdType)
			{
				if (symscope == "std:iterator" || symscope == "std:const_iterator" ||
				    symscope == "std:reverse_iterator" || symscope == "std:const_reverse_iterator")
				{
					// [case: 86248]
					WTString scp(cdDef->Scope());
					if (!scp.IsEmpty())
						symscope = "iterator";
				}

				if (symscope == "iterator" || symscope == "const_iterator" || symscope == "reverse_iterator" ||
				    symscope == "const_reverse_iterator")
				{
					// [case: 69089]
					// hack for c++ iterator support.
					// this should have a more generalized fix.
					WTString scp(cdDef->Scope());
					if (!scp.IsEmpty())
					{
						if (scp[0] == DB_SEP_CHR)
							scp = scp.Mid(1);
						symscope = scp + DB_SEP_STR + symscope;
					}
				}
			}

			// remove symscope from def
			WTString def(cdDef->Def());
			int pos5 = def.Find(symscope);
			if (-1 != pos5)
			{
				def = def.Mid(pos5 + symscope.GetLength());

				// truncate at cdDef->SymScope
				const WTString cdDefSym(StrGetSym(cdDef->SymScope()));
				if (cdDefSym.GetLength())
				{
					pos5 = def.Find(cdDefSym);
					if (-1 != pos5)
					{
						// see if * or ^ are left
						bool ptrIsPresent = false;
						def = def.Left(pos5);
						if (-1 != def.Find('*'))
							ptrIsPresent = isPointer = true;
						else if (-1 != def.Find('^'))
							ptrIsPresent = isHatPointer = true;

						if (isPointer || isHatPointer)
						{
							if (ptrIsPresent && !rangeBasedFor)
							{
								// check for array of pointers
								def = cdDef->Def();
								pos5 = def.Find(symscope);
								def = def.Mid(pos5 + symscope.GetLength());
								pos5 = def.Find(cdDefSym);
								def = def.Mid(pos5 + cdDefSym.GetLength());
								if (!def.IsEmpty() && def[0] == '[')
									isPtrToPtr = true;
							}

							// check for deref
							def = symDatDef;
							pos5 = def.Find(symDatSym);
							if (-1 != pos5)
							{
								def = def.Mid(pos5 + symDatSym.GetLength());
								pos5 = def.Find(cdDefSym);
								if (-1 != pos5)
								{
									def = def.Left(pos5);
									def.TrimRight();
									if (def.GetLength() && def[def.GetLength() - 1] == '*')
									{
										isPointer = isHatPointer = false;
										isDeref = true;
									}
									else if (def.GetLength() && def[def.GetLength() - 1] == '&')
										isPtrToPtr = true;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (CLASS == cdType || STRUCT == cdType || C_INTERFACE == cdType || C_ENUM == cdType ||
	         cdDef->IsReservedType())
	{
		if (IsCFile(devLang))
		{
			if (strstrWholeWord(symDatDef, "gcnew"))
				isHatPointer = true;
			else if (-1 != symDatDef.Find("ref new "))
				isHatPointer = true;
			else if (strstrWholeWord(symDatDef, "new"))
				isPointer = true;
		}
	}
	else if (C_ENUMITEM == cdType)
	{
		// not looked at
	}

	bool isRef = false;
	bool isRefRef = false;
	if (!symscope.IsEmpty())
	{
		deepModify = true;
		if (IsCFile(devLang) && (LINQ_VAR == cdType || VAR == cdType || FUNC == cdType))
		{
			if (cdDef->IsPointer() && !changedPtrAttrDueToImplicitDef && !isDeref && !isHatPointer && !isPointer)
			{
				const WTString cdDefDef(DecodeTemplates(cdDef->Def()));
				if (-1 != cdDefDef.Find('^'))
					isHatPointer = true;
				else
				{
					isPointer = true;

					// fold_braces_s is used to find pointer in type, but not in types used as template type arguments
					if (/*extractTypeFromIterator &&*/ std::string::npos == fold_braces_s(cdDefDef).find('*'))
					{
						// [case: 69242]
						// CString arr[6] is recorded as V_POINTER, but an element in the array is not ptr
						// CString *arr[6] is recorded as V_POINTER, and each element in the array is a ptr
						isPointer = false;
						// TODO: gmit: maybe clear pointer flag from cdDef?
					}
				}
			}

			if (-1 != origDef.Find('&'))
			{
				int pos2 = origDef.Find(symDatSym);
				if (-1 != pos2)
				{
					int pos1 = origDef.Find("auto");
					if (-1 != pos1)
					{
						pos1 += 4;
						if (pos1 < pos2)
						{
							WTString tmp(origDef.Mid(pos1, pos2 - pos1));
							pos1 = tmp.Find('&');
							if (-1 != pos1)
							{
								if (tmp[pos1 + 1] == '&')
								{
									// [case: 96952]
									isRefRef = true;
								}
								else
									isRef = true;
							}
						}
					}
				}
			}
		}
	}

	if (symscope.IsEmpty())
	{
		if (-1 != symDatDef.Find(symDatSym + "(") && retouchedDef.IsEmpty())
			return symDat;

		symscope = cdDef->SymScope().Mid(1);
		if (-1 != symscope.Find('-'))
		{
			// try again later
			symDat->FlipExpand();
			if (pSymDatInHashTable)
				pSymDatInHashTable->FlipExpand();
			return symDat;
		}
	}
	//	else if (-1 != symscope.Find('-'))
	//		_asm nop;

	const uint newType = (cdDef->type() == LINQ_VAR) ? LINQ_VAR : VAR;
	uint newAttrs = V_DONT_EXPAND;
	if (symDat->infile())
		newAttrs |= (V_INFILE | V_LOCAL); // Preserve bold (if any). case=24523

	WTString connector(" ");
	if (isHatPointer)
	{
		newAttrs |= V_POINTER;
		int pos = symscope.ReverseFind('^');
		if (!rangeBasedFor || -1 == pos || pos != (symscope.GetLength() - 1))
			connector += "^";
		if (isPtrToPtr)
			connector += "^";
	}
	else if (isPointer)
	{
		newAttrs |= V_POINTER;
		int pos = symscope.ReverseFind('*');
		if ((!rangeBasedFor || -1 == pos || pos != (symscope.GetLength() - 1)) && !va_ptr_substitution)
			connector += "*";
		if (isPtrToPtr)
			connector += "*";
	}
	else
	{
		_ASSERTE(!isPtrToPtr);
	}

	if (isRef || isRefRef)
	{
		if (isRef)
			connector += "& ";
		else
			connector += "&& ";
		connector.TrimLeft();
	}

	symDat->setType(newType, newAttrs, 0);
	if (pSymDatInHashTable)
		pSymDatInHashTable->setType(newType, newAttrs, 0);

	if (IsCFile(devLang))
	{
		symscope.ReplaceAll(DB_SEP_STR, "::");
		symscope.ReplaceAll(".", "::");
	}
	else
		symscope.ReplaceAll(":", ".");
	WTString newDef(symscope + connector + symDatSym);
	WTString origSymDef(symDatDef);
	if (newDef != origSymDef && !isLambda)
	{
		origSymDef = ReadToUnpairedColon(origSymDef);
		if (origSymDef.IsEmpty())
			origSymDef = symDatDef;

		// [case: 85908] improve definition:
		// given "auto v1 = Func()", change va newDef
		// from "int v1" to "int v1 = Func()"
		// questionable results for ast due to non-member begin() overloads:
		// VAAutoTest:NonMemberBeginAuto02
		// VAAutoTest:NonMemberBeginAuto04
		// VAAutoTest:NonMemberBeginAuto06
		LPCSTR pAuto = nullptr;
		if (IsCFile(devLang))
			pAuto = strstrWholeWord(origSymDef, "auto");
		else if (CS == devLang)
			pAuto = strstrWholeWord(origSymDef, "var");
		if (pAuto)
		{
			LPCSTR pSym = strstrWholeWord(origSymDef, symDatSym);
			if (pSym && pSym > pAuto && (pSym - pAuto) < 16)
			{
				WTString tmpDef;
				if (pAuto == origSymDef.c_str())
				{
					// [case: 86383] fix for:
					// auto constexpr foo = 1;
					const int autoLen = 5;
					if ((pSym - pAuto - autoLen) > 0)
					{
						tmpDef = origSymDef.Mid(autoLen, ptr_sub__int(pSym, pAuto) - autoLen);
						if (tmpDef.GetLength() > 4)
						{
							if (isRef)
							{
								auto tmpDef2 = tmpDef;
								tmpDef2.TrimRight();
								if (tmpDef2.ends_with("&"))
								{
									// trying to fix 'const &auto&' issue; reference will be pasted at the end (connector) so remove it from the prefix(tmpDef)
									tmpDef2.TrimRightChar('&');
									tmpDef = tmpDef2;
								}
							}
							if (isPointer && va_ptr_substitution)
							{
								// this code will remove pointers and modifiers from the original declaration since they will be reconstructed from pointer substitution
								auto tmpDef2 = tmpDef;
								tmpDef2.TrimRight();
								if (tmpDef2.ends_with("*"))
								{
									tmpDef2.TrimRightChar('*');
									tmpDef = tmpDef2;
								}
								trim_const_volatile(tmpDef, false); // remove last modifier from the left side
								                                    // TODO: modifiers such as const auto & should be ORed with modifiers from expression as constness can be increased in declaration
							}

							if (!tmpDef.ends_with(" "))
								tmpDef += " ";
						}
						else
							tmpDef.Empty();
					}
				}
				else
				{
					// TODO note: duplicated code as above. merge at some point!!
					tmpDef = origSymDef.Left(ptr_sub__int(pAuto, origSymDef.c_str()));

					if (isPointer && va_ptr_substitution)
					{
						auto tmpDef2 = tmpDef;
						tmpDef2.TrimRight();
						if (tmpDef2.ends_with("*"))
						{
							tmpDef2.TrimRightChar('*');
							tmpDef = tmpDef2;
						}
						trim_const_volatile(tmpDef, false); // remove last modifier from the left side
						                                    // TODO: modifiers such as const auto & should be ORed with modifiers from expression
					}

					if (!tmpDef.IsEmpty() && !isspace(tmpDef.Right(1)[0]))
						tmpDef += " ";
				}

				tmpDef += newDef;
				tmpDef += origSymDef.Mid(ptr_sub__int(pSym, origSymDef.c_str()) + symDatSym.GetLength());
				newDef = tmpDef;
			}
		}
	}

	// 	if (deepModify)	// append (original Def)?
	// 		newDef += "  ( " + symDat->Def() + " )";

	// if pointers have been converted into __va_ptr, now recreate them
	if (newDef.find("__va_ptr_begin") != NPOS)
	{
		static const std::string __va_ptr_begin__lt{"__va_ptr_begin<"};
		static const std::string __va_ptr_end__gt{"__va_ptr_end>"};

		auto newDef2 = DecodeTemplates(newDef, Src);
		const auto start = newDef2.find(__va_ptr_begin__lt.c_str());
		const auto end = newDef2.find(__va_ptr_end__gt.c_str(), start + (int)__va_ptr_begin__lt.size());
		assert((start != NPOS) && (end != NPOS));
		if ((start != NPOS) && (end != NPOS))
		{
			auto ptr_wrapper = newDef2.substr(start, end - start + (int)__va_ptr_end__gt.size()); // remove __va_ptr_end>
			std::vector<WTString> templates;

			void SplitTemplateArgs(const WTString& templateFormalDeclarationArguments,
			                       std::vector<WTString>& templateFormalDeclarationArgumentList);
			SplitTemplateArgs(ptr_wrapper, templates);

			assert(templates.size() == 2);
			assert(templates[0].begins_with("__va_ptr<"));
			assert(!templates[1].Compare("__va_ptr_end"));
			if (templates.size() == 2 && templates[0].begins_with("__va_ptr<") && !templates[1].Compare("__va_ptr_end"))
			{
				WTString newDef3 = "";
				WTString curptr = std::move(templates[0]);
				WTString modifier;
				while (true)
				{
					templates.clear();
					SplitTemplateArgs(curptr, templates);
					assert(templates.size() == 2);
					if (templates.size() != 2)
					{
						newDef3.Empty(); // shouldn't happen
						modifier.Empty();
						break;
					}

					newDef3.TrimLeft(); // remove space inserted with previous cv qualifier
					newDef3.prepend(" *");
					if (!templates[1].Compare("__va_ptr_none"))
					{
					}
					else if (!templates[1].Compare("__va_ptr_const"))
						modifier = "const";
					else if (!templates[1].Compare("__va_ptr_volatile"))
						modifier = "volatile";
					else if (!templates[1].Compare("__va_ptr_const_volatile"))
						modifier = "const volatile";
					else
					{
						assert(!"Unexpected __va_ptr template!");
						newDef3.Empty();
						modifier.Empty();
						break;
					}

					curptr = std::move(templates[0]);
					if (curptr.begins_with("__va_ptr<"))
					{
						modifier.prepend(" ");
						newDef3.prepend(modifier);
						modifier.Empty();
						continue;
					}
					else
					{
						newDef3.prepend(curptr);
						break;
					}
				}

				if (!newDef3.IsEmpty())
				{
					// reconstruct declaration text
					auto left = newDef.Left(start);                         // before "__va_ptr", likely empty
					auto right = newDef.Mid(end + __va_ptr_end__gt.size()); // typically "* var_name = init"
					if (newDef3.ends_with("*"))
					{
						right.TrimLeft();
						right.TrimLeftChar('*'); // remove * as we have reconstructed them fully
					}
					newDef = left + newDef3 + right;
					if (!modifier.IsEmpty())
					{
						// last const/volatile modifiers are put on the left side of type
						if (!newDef.IsEmpty() && !modifier.ends_with(" ") && !newDef.begins_with(" "))
							modifier += " ";
						newDef.prepend(modifier);
					}

					EncodeTemplates(newDef);
				}
			}
		}
	}
	newDef.Replace("__va_void", "void");
	symDat->SetDef(newDef);
	if (pSymDatInHashTable)
		pSymDatInHashTable->SetDef(newDef);

	return symDat;
}

BOOL IsLambda(LPCTSTR buf, LPCTSTR pStartOfBuf)
{
	LPCTSTR p = buf;
	if (*p == ']')
	{
		while (p > pStartOfBuf && p[-1] != '[')
			p--;

		if (p > pStartOfBuf)
			p--;
	}

	if (*p-- != '[')
		return FALSE;

	if (p > pStartOfBuf && wt_isspace(*p))
	{
		while (p > pStartOfBuf && wt_isspace(p[-1]))
			p--;

		if (p > pStartOfBuf)
			p--;
	}

	if (*p == ',')
		return true;

	if (*p == '(')
		return true;

	if (*p == '=')
		return true;

	return false;
}

void ConvertVAScopingToLanguageScoping(WTString& str, int fileType)
{
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == ':' && (i == str.GetLength() - 1 || str[i + 1] != ':') && (i == 0 || str[i - 1] != ':'))
		{
			if (IsCFile(fileType))
			{
				if (i == str.GetLength() - 1 || str[i + 1] != ':')
				{
					str.ReplaceAt(i, 1, "::");
					i++;
				}
			}
			else
			{
				str.ReplaceAt(i, 1, ".");
			}
		}
	}
}

// convert to array type (e.g. "int *" to "int" for C arryas, "std::vector<int, Allocator<int> >" to "int" for
// templates, etc.)
WTString InferType::GetArrayType(WTString type, WTString var, bool& isTemplateType, int fileType)
{
	isTemplateType = false;
	int templPos = ::FindInCode(type, EncodeChar('<'), fileType);
	if (templPos == -1)
		templPos = ::FindInCode(type, '<', fileType);
	if (templPos != -1)
	{ // template type (e.g. std::vector<int>)
		CommentSkipper cs(fileType);
		WTString templType;
		int openAngleBrackets = 1;
		for (int i = templPos + 1; i < type.GetLength(); i++)
		{
			TCHAR c = type[i];
			if (cs.IsCode(c))
			{
				if (c == EncodeChar('<') || c == '<')
					openAngleBrackets++;
				else if (c == EncodeChar('>') || c == '>')
				{
					openAngleBrackets--;
					if (openAngleBrackets <= 0)
						break;
				}
				else if (openAngleBrackets <= 1 && c == ',')
					break;
				templType += c;
			}
		}
		templType.Trim();
		isTemplateType = true;

		while (templType[templType.GetLength() - 1] == '*' || templType[templType.GetLength() - 1] == EncodeChar('*'))
			templType = templType.Left(templType.GetLength() - 1); // removing '*' from the end
		while (templType[templType.GetLength() - 1] == '&' || templType[templType.GetLength() - 1] == EncodeChar('&'))
			templType = templType.Left(templType.GetLength() - 1); // removing '&' from the end
		while (templType[templType.GetLength() - 1] == '^' || templType[templType.GetLength() - 1] == EncodeChar('^'))
			templType = templType.Left(templType.GetLength() - 1); // removing '^' from the end

		templType.TrimRight();
		if (templType[templType.GetLength() - 1] == EncodeChar(' '))
			templType = templType.Left(templType.GetLength() - 1); // removing encoded space from the end

		return templType;
	}

	// for complex types return empty string (auto / var will be used) - '*' and '(' means we have something complex.
	// arrays of references are illegal but better to be safe than sorry (future-proof?)
	if ((type.Find('*') != -1 || type.Find('&') != -1) && type.Find('(') != -1)
		return "";

	// C#: remove as many []s as the variable contains (see test cases from VAAutoTest:IntroduceVariable04 to
	// VAAutoTest:IntroduceVariable08)
	int count = 0;
	for (int i = 0; i < var.GetLength(); i++)
		if (var[i] == '[')
			count++;

	// remove the star
	for (int i = 0; i < type.GetLength(); i++)
	{
		TCHAR c = type[i];
		if (c == '*' || c == EncodeChar('*'))
		{ // C++
			type.ReplaceAt(i, 1, "");
			break; // we only remove one '*'
		}
		if (c == ',')
		{ // C# (remove ',' from "[,]")
			type.ReplaceAt(i, 1, "");
			i--;
		}
		if (c == '[')
		{ // C#
			if (count > 0)
			{
				type.ReplaceAt(i, 1, "");
				i--;
			}
		}
		if (c == ']')
		{ // C#
			if (count > 0)
			{
				type.ReplaceAt(i, 1, "");
				i--;
				if (count-- == 0)
					break;
			}
		}
	}

	type.Trim();
	return type;
}

WTString GetCStyleCastType(WTString expression, int fileType)
{
	bool onlyWhiteSpace = false;
	CommentSkipper cs(fileType);
	int i;
	int start;
	if (expression.GetLength() > 1 && expression[1] == '(')
		start = 2;
	else
		start = 1;
	for (i = start; i < expression.GetLength(); i++)
	{
		TCHAR c = expression[i];
		if (cs.IsCode(c) && cs.GetState() != CommentSkipper::COMMENT_MAY_START)
		{
			if (c == ')') // done
				break;
			if (c == '*')
			{
				onlyWhiteSpace = true;
				continue;
			}

			if (onlyWhiteSpace)
			{
				if (!IsWSorContinuation(c)) // invalid char
					return "";
			}
			else
			{
				if (!IsWSorContinuation(c) && !ISCSYM(c) && c != '*') // invalid char
					return "";
			}
		}
	}

	WTString typeName = expression.Mid(start, i - start);
	typeName.Trim();
	return typeName;
}

extern int FindNonWhiteSpace(int pos, const WTString& fileBuf);

// find '<' or '>' inside a string but outside a method/function call. examples:
// '<' isn't found:
//		pointer->method<false>()				<- '<' is part of the method call
//		functioncall(apple < orange)			<- '<' is inside of a method call
// '<' can be found:
//		call1<true>(ptr) < call1<false>(ptr)	<- '<' can be found outside of a method call
//
// param op is specialized to '<' and '>' because that's what I need and don't want to hit the symbol database.
extern int FindOutsideFunctionCall(WTString str, TCHAR op, int fileType)
{
	ASSERT(op == '<' || op == '>' || op == ',');
	bool inFunction = false;

	int parens = 0;   // ()
	int braces = 0;   // {}
	int brackets = 0; // []

	CommentSkipper commentSkipper(fileType);
	for (int i = 0; i < str.GetLength(); i++)
	{
		TCHAR c = str[i];
		if (commentSkipper.IsCode(c))
		{
			// bracket counting
			if (c == '(')
				parens++;
			if (c == '{')
				braces++;
			if (c == '[')
				brackets++;
			if (c == ')')
				parens--;
			if (c == '}')
				braces--;
			if (c == ']')
				brackets--;

			if (!IsWSorContinuation(c))
			{
				if (ISCSYM(c))
				{
					inFunction = true;
				}
				else
				{
					if (inFunction)
					{
						// "chain" delimiter (. -> ::)
						if ((i + 1 < str.GetLength() && c == '-' && str[i + 1] == '>'))
						{
							i++;
							continue;
						}
						if ((i + 1 < str.GetLength() && c == ':' && str[i + 1] == ':'))
						{
							i++;
							continue;
						}
						if (c == '.')
						{
							continue;
						}

						// skipping between ()
						if (c == '(')
						{
							int pos = IntroduceVariable::FindCorrespondingParen(str, i);
							if (pos != -1)
							{
								i = pos;
								continue;
							}
						}

						// skipping between []
						if (c == '[')
						{
							int pos = IntroduceVariable::FindCorrespondingParen(str, i, '[', ']');
							if (pos != -1)
							{
								i = pos;
								continue;
							}
						}

						// skipping between <>
						if (c == '<')
						{
							int res = -1;
							if (op == '<')
								res = i;
							int pos = IntroduceVariable::FindCorrespondingParen(str, i, '<', '>');
							if (pos != -1)
							{
								i = pos;
								if (op == '>')
									res = i;
								int nonWhiteSpace = FindNonWhiteSpace(pos + 1, str);
								if (nonWhiteSpace >= str.GetLength() || str[nonWhiteSpace] == '(')
									continue; // (optionally chained) "alphanumeric<...>(" was found
								else
									return res; // no '(' was found after a (optionally chained) "alphanumeric<...>"
									            // pattern
							}
						}

						// something that can't fit in a single symbol/function call or a chain of them
						inFunction = false;
					}
					if (c == op)
					{
						if (op == ',' && (parens > 0 || brackets > 0 || braces > 0))
							continue;
						return i;
					}
				}
			}
		}
	}

	return -1;
}

extern WTString GetCastType(WTString expression);

DTypePtr GetDtypeForPos(EdCntPtr ed, ULONG pos)
{
	if (ed)
	{
		const WTString buf(ed->GetBuf());
		WTString scp;
		return SymFromPos(buf, ed->GetParseDb(), (int)pos, scp);
	}

	return std::make_shared<DType>();
}

WTString InferType::Infer(WTString expression, WTString scope, WTString bcl, int fileType,
                          bool useEditorSelection /*= false*/, WTString defaultType /*= ""*/, DType** dt /*= nullptr*/)
{
	mEd = g_currentEdCnt;
	if (!mEd)
	{
		_ASSERTE(!"Infer used without active editor");
		return GetDefaultType();
	}

	DefaultType = defaultType;
	mSkipConstPrepend = false;
	mMp = mEd->GetParseDb();
	return Infer(std::move(expression), std::move(scope), std::move(bcl), fileType, useEditorSelection, false, dt);
}

WTString InferType::Infer(MultiParsePtr mp, WTString expression, WTString scope, WTString bcl, int fileType,
                          bool resolveLambda /*= true*/)
{
	mMp = mp;
	mSkipConstPrepend = true;
	return Infer(std::move(expression), std::move(scope), std::move(bcl), fileType, false, resolveLambda, nullptr);
}

WTString ResolveLambdaType(MultiParsePtr& mp, WTString expression, const WTString& scope, const WTString& bcl,
                           const WTString& defaultType)
{
	// return something like "std::function<void(Param1Type, Param2Type)>"
	// built from parsing the args at posOpenParen in the expression
	int posCloseBracket = expression.Find(']');
	if (-1 == posCloseBracket)
		return defaultType;

	// normalize the expression
	expression.ReplaceAll("\r", " ");
	expression.ReplaceAll("\n", " ");
	expression.ReplaceAll("\t", " ");
	expression.ReplaceAll(" ;", ";");
	expression.ReplaceAll("( ", "(");
	expression.ReplaceAll(" (", "(");
	expression.ReplaceAll(") ", ")");
	expression.ReplaceAll(" )", ")");
	while (-1 != expression.Find("  "))
		expression.ReplaceAll("  ", " ");

	// recalc pos now that expression has changed
	posCloseBracket = expression.Find(']');
	int posOpenParen = expression.Find("(", posCloseBracket);
	if (-1 == posOpenParen)
		return defaultType;

	const int posCloseParen = expression.Find(")", posOpenParen);
	if (-1 == posCloseParen)
		return defaultType;

	WTString returnType;
	WTString args(expression.Mid(posOpenParen, (posCloseParen + 1) - posOpenParen));
	_ASSERTE(0 == args.Find('(') && (args.GetLength() - 1) == args.Find(')'));

	const int posOpenBrace = expression.Find("{", posCloseParen);
	if (-1 != posOpenBrace)
	{
		WTString expressionToInferFrom;
		WTString gap(expression.Mid(posCloseParen, posOpenBrace - posCloseParen));
		bool checkExprForReturn = true;
		const int posExtRet = gap.Find("->");
		if (-1 != posExtRet)
		{
			gap = gap.Mid(posExtRet + 2);
			if (!strstrWholeWord(gap, "auto"))
			{
				// read trailing return type, but if is auto, then fall through to "return" logic
				gap.Trim();

				int decltypePos = gap.Find("decltype");
				if (-1 != decltypePos)
				{
					int decltypeOpenParenPos = gap.Find("(", decltypePos);
					if (-1 != decltypeOpenParenPos)
					{
						int decltypeCloseParenPos = gap.ReverseFind(")");
						if (-1 != decltypeCloseParenPos)
							expressionToInferFrom =
							    gap.Mid(decltypeOpenParenPos + 1, decltypeCloseParenPos - (decltypeOpenParenPos + 1));
					}
				}
				else
					expressionToInferFrom = gap;

				if (!expressionToInferFrom.IsEmpty())
					checkExprForReturn = false;
			}
		}

		if (checkExprForReturn)
		{
			const int posRet = expression.Find("return");
			if (-1 != posRet)
			{
				if (-1 == expression.Find("return;"))
				{
					const int returnSemiPos = expression.Find(";", posRet + 6);
					if (-1 != returnSemiPos)
						expressionToInferFrom = expression.Mid(posRet + 6, returnSemiPos - (posRet + 6));
				}
			}
		}

		expressionToInferFrom.Trim();
		if (!expressionToInferFrom.IsEmpty())
		{
			InferType inf;
			// pass in false for resolveLambda to prevent recursion
			returnType = inf.Infer(mp, expressionToInferFrom, scope, bcl, Src, false);
			if (returnType == "UnknownType" || returnType == "auto")
			{
				if (-1 == expressionToInferFrom.FindOneOf("',.:; \t"))
				{
					// try a find on expressionToInferFrom
					DType* dt = GetDFileMP(Src)->LDictionary()->FindExact(expressionToInferFrom, 0);
					if (dt && dt->IsReservedType())
					{
						returnType = expressionToInferFrom;
					}
					else
					{
						dt = mp->FindSym(&expressionToInferFrom, &scope, &bcl,
						                 FDF_TYPE | FDF_NoConcat | FDF_NoAddNamespaceGuess | FDF_SplitBclForScope);
						if (dt)
							returnType = dt->Sym();
					}
				}
			}
		}
	}

	if (returnType.IsEmpty())
		returnType = "void";

	WTString lambdaType = "std::function<" + returnType + args + ">";
	return lambdaType;
}

// #AUTO_INFER 3rd step
WTString InferType::Infer(WTString expression, WTString scope, WTString bcl, int fileType, bool useEditorSelection,
                          bool resolveLambda, DType** dt)
{
	// before doing original infer, first:

	// remove excessive braces
	expression.Trim();
	while (expression.begins_with("("))
	{
		auto closing = FindMatchingBrace<'(', ')'>(expression.cbegin(), expression.cend());
		if (closing != expression.cend() && closing == std::prev(expression.cend()))
		{
			expression = expression.substr(1, expression.length() - 2);
			expression.Trim();
		}
		else
			break;
	}

	// handle dereference operators one by one
	if (expression.begins_with("*"))
	{
		expression.insert(1, "__va_passthrough_type(");
		expression.append(')');

		// we are going to handle dereference here; don't handle it elsewhere
		std::lock_guard l{threads_where_infer_handled_dereference_mutex};
		threads_where_infer_handled_dereference.insert(std::this_thread::get_id());
	}

	return Infer_orig(std::move(expression), std::move(scope), std::move(bcl), fileType, useEditorSelection, resolveLambda, dt);
}
WTString InferType::Infer_orig(WTString expression, WTString scope, WTString bcl, int fileType, bool useEditorSelection,
                               bool resolveLambda, DType** dt)
{
	_ASSERTE(mMp);
	_ASSERTE(!useEditorSelection || mEd);
	bool dereference = false;
	bool arrOutside = false;

	WTString localScope = scope;
	WTString inferFrom, inferFromBcl;
	expression.Trim();
	WTString orig_expression = expression;
	if (expression.IsEmpty())
		return GetDefaultType(fileType);

	// Lambda
	if (expression.GetLength() && expression[0] == '[')
	{
		const WTString defType(GetDefaultType(fileType));
		if (resolveLambda && IsCFile(fileType))
			return ::ResolveLambdaType(mMp, expression, scope, bcl, defType);
		return defType;
	}

	// C style CAST, e.g. (int)4.5f
	WTString castName;
	if (expression[0] == '(')
	{
		castName = GetCStyleCastType(expression, fileType);
		if (castName.GetLength())
			return castName;
	}

	// C++ style CAST, e.g. dynamic_cast<cSuperClass>(basePointer)
	castName = GetCastType(expression);
	if (castName.GetLength())
		return castName;

	// Constructor style "CAST", e.g. unsigned int(4.5f)
	castName = GetConstructorCastType(expression, fileType);
	if (castName.GetLength())
		return castName;

	// case 84953 improve type inference with parens
	// trimming '('s and comments
	// this should result in the type of Val instead of auto: (cClass::Val + cClass::Val)
	// this should result in int: /*comment*/0
	CommentSkipper cs(fileType);
	int trimPos;
	for (trimPos = 0; trimPos < expression.GetLength(); trimPos++)
	{
		TCHAR c = expression[trimPos];
		if ((cs.IsCode(c) && cs.GetState() != CommentSkipper::COMMENT_MAY_START) ||
		    cs.GetState() == CommentSkipper::IN_STRING || cs.GetState() == CommentSkipper::IN_CHAR)
		{
			if (c != '(' && !IsWSorContinuation(c))
				break;
		}
	}
	if (trimPos)
		expression = expression.Right(expression.GetLength() - trimPos);

	//	assert(fileType == gTypingDevLang);
	LiteralType strType = ::GetLiteralType(fileType, expression, '"');
	if (strType == LiteralType::None)
		strType = ::GetLiteralType(fileType, expression, '\'');

	if (strType != LiteralType::None)
	{
		char lastCh = expression[expression.GetLength() - 1];
		if ('"' != lastCh && '\'' != lastCh)
			mSkipConstPrepend = true;
		inferFrom = expression;
	}
	else
	{
		int questionMarkPos = ::FindInCode(expression, '?', fileType);
		if (questionMarkPos != -1)
			expression = expression.Right(expression.GetLength() - questionMarkPos - 1);

		if (::FindInCode(expression, '!', fileType) != -1)
			return "bool";

		if (FindOutsideFunctionCall(expression, '<', fileType) != -1)
			return "bool";

		if (FindOutsideFunctionCall(expression, '>', fileType) != -1)
			return "bool";

		if (expression.find("==") != -1 || expression.find("&&") != -1 || expression.find("||") != -1)
			return "bool";

		inferFrom = ::TokenGetField(expression,
		                            "; ()+=*\\<>-:"); // [case: 31924] don't stop at '.' before the simple type check
		int part = expression.Find(inferFrom);
		if (part != -1)
		{
			WTString before = expression.Left(part);
			if (before.Find('*') != -1)
				dereference = true;
		}
		if (expression.GetLength() && expression.Right(1) == ']')
			arrOutside = true;
	}

	LPCSTR t = ::SimpleTypeFromText(fileType, inferFrom);
	if (t)
	{
		WTString res = t;
		if (!mSkipConstPrepend && expression.GetLength() == inferFrom.GetLength() && res.Left(3) != "LPC" &&
		    res.Left(6) != "const ")
			res = "const " + res;
		return res; // "newVar = true;"
	}

	inferFrom = NextChainItem(expression, useEditorSelection, fileType);

	bool preciseMode = true;
	while (expression.GetLength() >= 2 && ((expression[0] == '.' || (expression[0] == '-' && expression[1] == '>') ||
	                                        (expression[0] == ':' && expression[1] == ':')) ||
	                                       expression[0] == '<'))
	{
		bool directType = false;
		if (expression[0] == ':' && expression[1])
			directType = true;
		if (inferFrom.GetLength() >= 1 && inferFrom[0] == '<' && inferFrom[1])
		{
			directType = true;
			inferFrom = inferFrom.Mid(1);
			TokenGetField2InPlace(inferFrom, '>');
		}

		expression = expression.Mid((expression[0] == '.') ? 1 : 2);
		WTString next(::TokenGetField(expression, "; ()+=*\\<>.-:"));

		if (directType)
		{
			scope = ":" + inferFrom; // TODO: try to find in namespaces specified by using namespace keywords as well?
			                         // Probably inferring from "inferFrom" needs to do this.
			inferFrom = next;
		}
		else if (preciseMode)
		{
			// get the name of an array (e.g. "a" from a[5]) so we can use type inference on it
			bool arr = false;
			WTString originalVarName = inferFrom;
			if (inferFrom.Find('[') != -1)
			{
				::TokenGetField2InPlace(inferFrom, '[');
				arr = true;
			}

			// infer type
			DType* cd = mMp->FindSym(&inferFrom, &scope, inferFromBcl.IsEmpty() ? &bcl : &inferFromBcl,
			                         FDF_NoConcat | FDF_SlowUsingNamespaceLogic);

			inferFrom.Empty();
			inferFromBcl.Empty();
			if (cd)
			{
				WTString tmp(::GetTypesFromDef(cd, (int)cd->MaskedType(), fileType));
				// convert array type to normal type
				bool tt;
				if (arr)
					tmp = GetArrayType(tmp, originalVarName, tt, fileType);

				int pos = tmp.Find('\f');
				if (-1 != pos)
					tmp = tmp.Left(pos);
				if (tmp.GetLength())
				{
					if (arr && tmp[0] != ':')
						scope = WTString(":") + tmp;
					else
						scope = tmp;

					inferFrom = next;
					DType* rdt = ::ResolveTypeStr(tmp, cd, mMp.get());
					if (rdt)
					{
						// [case: 90750]
						inferFromBcl = mMp->GetBaseClassList(rdt->SymScope());
					}
					else
						inferFromBcl = mMp->GetBaseClassList(scope);
				}
				else
				{
					preciseMode = false;
				}
			}
			else
			{
				preciseMode = false;
			}
			// break;
		}

		if (!preciseMode)
			inferFrom += ":" + next;
		WTString res = NextChainItem(expression, useEditorSelection, fileType);
		if (res.GetLength() > 1 && res[0] == '<')
		{
			return res.Mid(1);
		}
	}

	if (inferFrom.IsEmpty())
		return GetDefaultType();

	if (inferFrom.GetLength() >= 1 && inferFrom[0] == '<' && inferFrom[1])
	{
		inferFrom = inferFrom.Mid(1);
		TokenGetField2InPlace(inferFrom, '>');
		return inferFrom;
	}

	DType* cd = mMp->FindSym(&inferFrom, &scope, &inferFromBcl, FDF_NoConcat | FDF_SlowUsingNamespaceLogic);
	bool arr = false;
	WTString originalVarName = inferFrom;
	if ((!cd && inferFrom[0] != DB_SEP_CHR) || arrOutside)
	{
		// convert array sym to normal sym (e.g. a[5] to a)
		if (inferFrom.Find('[') != -1)
		{
			::TokenGetField2InPlace(inferFrom, '[');
			arr = true;
		}

		// [] is already cut by TokenGetField earlier, e.g. (*param)[0]
		if (arrOutside)
			arr = true;

		// get the type of the symbol
		WTString scopedFrom(DB_SEP_STR + inferFrom);
		cd = mMp->FindSym(&scopedFrom, &scope, &inferFromBcl, FDF_NoConcat | FDF_SlowUsingNamespaceLogic);
		if (!cd)
			cd = mMp->FindAnySym(scopedFrom);
	}

	if (cd)
	{
		const uint typ = cd->MaskedType();

		// handle function calls; logic copied from VAParse/DoScope
		if ((typ == FUNC) && cd->IsTemplate())
		{
			auto orig_expression2 = orig_expression;
			if (dereference)
			{
				// strips dereference operator from expression string
				// gmit todo: allow this only for the first char?!
				int i = orig_expression2.find_first_not_of("*");
				if (i != NPOS)
				{
					assert(i == 1); // remove asserts later; only for development to follow what's going on
					orig_expression2 = orig_expression2.Mid(i);
				}
				else
					orig_expression2.Empty();
			}

			WTString template_args = VAParseMPScope::GetTemplateFunctionCallTemplateArg(std::move(orig_expression2), mMp, fileType, cd, scope, bcl);
			if (!template_args.IsEmpty())
			{
				auto new_symbol = originalVarName + template_args;
				DType* new_symbol_dt = mMp->FindSym(&new_symbol, &scope, &bcl, FDF_SlowUsingNamespaceLogic | FDF_NoConcat);
				if (!new_symbol_dt)
				{
					// cause template instantiation via GetBaseClassList call
					mMp->GetBaseClassList(new_symbol, false, nullptr, fileType);
					new_symbol_dt = mMp->FindSym(&new_symbol, &scope, &bcl, FDF_SlowUsingNamespaceLogic | FDF_NoConcat);
				}

				if (new_symbol_dt && new_symbol_dt != cd)
					cd = new_symbol_dt;
			}
		}

		if (VAR == typ || FUNC == typ || C_ENUMITEM == typ || cd->IsType())
		{
			WTString tmp(GetDeclTypeFromDef(mMp.get(), cd, (int)typ));

			// tmp is unencoded
			const auto apply_operator = [&mp = mMp, &tmp, &cd, &scope, typ](const WTString& operatorName) {
				const WTString operatorScope = ":" + EncodeScope(tmp);
				WTString bcl2 = mp->GetBaseClassList(operatorScope);
				DType* operatorType =
				    mp->FindSym(&operatorName, &operatorScope, &bcl2, FDF_NoConcat | FDF_SlowUsingNamespaceLogic);
				if (operatorType)
				{
					cd = operatorType;
					tmp = GetDeclTypeFromDef(mp.get(), cd, (int)typ);
					if (!tmp.IsEmpty())
						scope = operatorScope; // [case: 85941]
					return true;
				}
				return false;
			};

			if (dereference)
			{
				if (tmp.ends_with("*"))
				{
					// just strip pointer and remove excessive const/volatile
					tmp = tmp.Left(tmp.GetLength() - 1); // removing '*' from the end
					while (true)
						if (auto [c, v] = trim_const_volatile(tmp); !c && !v)
							break;

					if (!tmp.ends_with("*") && !tmp.ends_with("&"))
					{
						while (true)
							if (auto [c, v] = trim_const_volatile(tmp, false); !c && !v)
								break;
					}
				}
				else
					apply_operator("*"); // operator *
			}
			if (arr)
			{ // convert to array type
				if (tmp.GetLength() && tmp.Right(1) == "&")
					tmp = tmp.Left(tmp.GetLength() - 1); // removing '&' from the end
				if (tmp.GetLength() > 5 && tmp.Left(5) == "const")
				{
					tmp = tmp.Right(tmp.GetLength() - 5); // removing "const" from the end
					tmp.TrimLeft();
				}
				bool tt;
				tmp = GetArrayType(tmp, originalVarName, tt, fileType);
				if (!tt)
					apply_operator("[]");
			}
			if (tmp.GetLength() && tmp.Find(':') == -1)
			{ // already qualified
				// tmp = ":" + tmp;
				QualifyTypeWithFullScope(tmp, cd, mMp.get(), localScope, scope);
				if (tmp.GetLength() && tmp[0] == ':')
					tmp = tmp.Right(tmp.GetLength() - 1);
			}
			if (!tmp.IsEmpty())
			{
				tmp = DecodeTemplates(tmp);
				ConvertVAScopingToLanguageScoping(tmp, fileType);
				if (dt)
					*dt = cd;
				return tmp;
			}
		}
		else if (cd->MaskedType() == PROPERTY)
		{
			WTString res = cd->Def();
			res.ReplaceAll("public", "", TRUE);
			res.ReplaceAll("private", "", TRUE);
			res.ReplaceAll("protected", "", TRUE);
			res.ReplaceAll("__published", "", TRUE);
			res.ReplaceAll("internal", "", TRUE);
			res.ReplaceAll("static", "", TRUE);
			res.TrimLeft();
			res = TokenGetField(res);
			return res;
		}
	}

	return GetDefaultType();
}

void InferType::RemoveParens(WTString& expression)
{
	if (expression.GetLength() && expression[0] == '(')
	{
		const int pos = IntroduceVariable::FindCorrespondingParen(expression, 0);
		if (pos != -1 && pos + 1 < expression.GetLength())
			expression = expression.Mid(pos + 1);
	}
}

long FindEndOfChain(const WTString& buf, long s, long e, int fileType)
{
	if (s == 0)
		return s;

	int parens = 0;
	int sq = 0;

	CommentSkipper cs(fileType);
	bool alphaNumStart = true;
	long res = s;
	for (int i = s; i < e; i++)
	{
		const TCHAR c = buf[i];
		if (cs.IsCode(c))
		{
			if (c == '(')
			{
				parens++;
				continue;
			}
			if (c == ')')
			{
				parens--;
				continue;
			}
			if (parens == 0)
			{
				if (c == '<')
				{
					sq++;
					continue;
				}
				if (c == '>' && buf[i - 1] != '-')
				{
					sq--;
					continue;
				}
			}
			if (parens || sq)
				continue;
			if (c == '.' || c == '-' || (c == '>' && buf[i - 1] == '-'))
			{
				alphaNumStart = true;
				continue;
			}
			if (ISCSYM(c))
			{
				if (alphaNumStart)
				{
					alphaNumStart = false;
					res = i;
				}
				continue;
			}
			if (!IsWSorContinuation(c))
				break;
		}
	}

	return res;
}

WTString InferType::NextChainItem(WTString& expression, bool useEditorSelection, int fileType)
{
	WTString inferFrom = ::TokenGetField(expression, " ;()+=*\\>.-:&|,"); // [case: 31924] repeat, but include '.'
	expression = expression.Mid(inferFrom.GetLength());
	if (expression.GetLength() && expression[0] == '>')
		expression = expression.Mid(1);
	int pos = inferFrom.Find('<');

	if (pos != -1) // template arguments
	{
		if (useEditorSelection)
		{
			if (mEd)
			{
				WTString methodName = inferFrom.Left(pos);
				long s, e;
				mEd->GetSel2(s, e);
				WTString buf = mEd->GetBuf();
				long pos1 = mEd->GetBufIndex(buf, s);
				long pos2 = mEd->GetBufIndex(buf, e);
				if (pos1 > pos2)
					std::swap(pos1, pos2);
				pos1 = FindEndOfChain(buf, pos1, pos2, fileType);
				DTypePtr dt = GetDtypeForPos(mEd, (ULONG)pos1);
				if (dt)
				{
					WTString funcRetType(GetDeclTypeFromDef(mMp.get(), dt.get(), (int)dt->MaskedType()));
					return "<" + funcRetType;
				}
			}
			else
				_ASSERTE(!"useEditorSelection was true but there is no editor");
		}

		inferFrom = inferFrom.Mid(pos);
		pos = ::FindInCode(inferFrom, ',',
		                   fileType); // multiple template arguments - use the first one for type inference
		if (pos != -1)
			inferFrom = inferFrom.Left(pos);

		WTString inferFrom2;
		// read the remaining part of the (first) template argument: symbol chars and whitespaces until ',' or '>'
		for (int i = 0; i < expression.GetLength(); i++)
		{
			const TCHAR c = expression[i];

			// don't stop on ptrs/refs in template args
			if (ISCSYM(c) || IsWSorContinuation(c) || (c == '*') || (c == '&'))
			{
				if (c == '>' || c == ',')
					break;
				inferFrom2 += c;
				continue;
			}
			break;
		}
		expression = expression.Mid(inferFrom2.GetLength());
		inferFrom += inferFrom2;
	}

	inferFrom.Trim();

	// supporting method call chains such as GetSomeClass()->GetSomeThing() or GetSomeClass("fdjk")->GetSomeThing(),
	// etc.
	RemoveParens(expression);

	return inferFrom;
}

WTString InferType::GetDefaultType(int devLang /*= gTypingDevLang*/)
{
	if (DefaultType.GetLength()) // keep original value for Create from usage for now
		return DefaultType;

	if (devLang == CS)
	{ // C#
		return L"var";
	}
	else
	{ // C++
		if (gShellAttr->IsDevenv10OrHigher() || gTestsActive)
			return L"auto";
		else
			return L"UnknownType";
	}
}

WTString InferType::GetConstructorCastType(WTString expression, int fileType)
{
	bool addStar = false;
	if (expression.GetLength() >= 4 && expression.Left(3) == "new" &&
	    (IsWSorContinuation(expression[3]) || expression[3] == '/'))
	{
		// return ""; // "new cClass()" or  "new/*comment*/cClass()" is NOT a constructor even if we don't find a class
		// with that name - to avoid breaking create class from usage
		if (fileType != CS)
			addStar = true;
		expression = expression.Right(expression.GetLength() - 3); // cutting "new" from the beginning of the string
	}

	// accepting only whitespaces+symbol characters until a '(' to call it a constructor style cast.
	// examples: unsigned int(5.6f)						unsigned int
	//           unsigned/*awkward comment*/int(6.2f)	unsigned int
	//           cExampleClass(4, 2, 1)					cExampleClass*
	CommentSkipper cs(fileType);
	int i;
	for (i = 0; i < expression.GetLength(); i++)
	{
		const TCHAR c = expression[i];
		if (cs.IsCode(c))
		{
			if (c == '(') // it is a constructor
				goto return_typename;

			if (!ISCSYM(c) && !IsWSorContinuation(c))
				return ""; // no luck
		}
	}

	return ""; // we reached the end of the string without a '('

return_typename:

	WTString typeName = expression.Mid(0, i);
	typeName.Trim();
	const DType* type = mMp->FindAnySym(typeName);
	if (type)
	{
		if (type->MaskedType() == CLASS || type->MaskedType() == STRUCT || type->Attributes() & V_CONSTRUCTOR)
		{
			if (addStar)
				typeName += "*";
		}
		else
		{
			return ""; // methods, function and macros should not be treated as constructor style casts
		}
	}

	return typeName;
}

bool InferType::GetTypesAndNamesForStructuredBinding(LPCSTR expression, MultiParsePtr mp, WTString scope,
                                                     std::vector<WTString>& names, std::vector<WTString>& types, char stOpen, char stClose, const WTString& stKeyword)
{
	scope.ReplaceAll(":" + stKeyword, "");

	enum class eState
	{
		AUTO,
		VARIABLES,
		TYPE,
	};

	int count = 0; // fail safety
	WTString typeVar;
	WTString name;
	eState state = eState::AUTO;
	int roundBrackets = 0;
	bool templParamsProvided = false;
	bool colonSeparator = false;
	bool directType = false; // C#
	const auto parseExpression = [&](LPCSTR expr) {
		CommentSkipper cs(Src);
		cs.NoStringSkip();
		for (auto ptr = expr; *ptr != 0; ptr++)
		{
			const auto ch = DecodeChar(*ptr);
			if (!cs.IsCode2(ch, *(ptr + 1)))
				continue;
			if (ch == ';' || ch == '{' || ch == '}')
				break;

			if (count++ > 1024)
				return false;

			if ((ch == ',' || ch == stClose) && state == eState::VARIABLES)
			{
				if (!name.IsEmpty())
					names.push_back(name);
				name = "";
				continue;
			}

			switch (state)
			{
			case eState::AUTO:
				if (ch == stOpen)
				{
					state = eState::VARIABLES;
					continue;
				}
				break;
			case eState::VARIABLES:
				if (ch == '=')
					state = eState::TYPE;
				else if (ch == ':' && *(ptr + 1) != ':')
				{
					state = eState::TYPE;
					colonSeparator = true;
				}
				else if (!IsWSorContinuation(ch))
					name += ch;
				break;
			case eState::TYPE:
				if (IsWSorContinuation(ch) && typeVar == "new") // C#
				{
					typeVar = "";
					directType = true;
				}
				if (!IsWSorContinuation(ch) || ch == ':')
					typeVar += ch;
				if (ch == '<' && roundBrackets == 0)
					templParamsProvided = true;
				if (ch == '(')
					roundBrackets++;
				else if (ch == ')')
				{
					roundBrackets--;
					if (roundBrackets == 0)
						return true;
				}
				break;
			}
		}

		return true;
	};

	if (!parseExpression(expression))
		return false;

	if (!typeVar.IsEmpty())
	{
		if (directType) // C#
		{
			TokenGetField2InPlace(typeVar, '(');
			GetStructMembers(typeVar, types, mp, ""); // struct members
			return !!types.size();
		}

		InferType infer;
		WTString bcl = mp ? mp->GetBaseClassList(mp->m_baseClass) : "";
		if (scope != ":")
			scope += ":";

		WTString typeVarWord = TokenGetField(typeVar, "(<)");

		WTString type;
		WTString def;
		WTString symScope;
		if (typeVarWord == "std::pair" || typeVarWord == "std::tuple" || typeVarWord == "pair" ||
		    typeVarWord == "tuple" || typeVarWord == "std::make_pair" || typeVarWord == "std::make_tuple" ||
		    typeVarWord == "make_pair" || typeVarWord == "make_tuple" ||
		    (typeVar.GetLength() && typeVar[0] == '(' && mp->FileType() == CS)) // C#
		{
			type = typeVar;
			if (!templParamsProvided)
			{
				// Inferring template types from parameters
				GetInferredTypesFromArguments(type, types, mp, scope);
				return !!types.size();
			}
		}
		else
		{
			// Inferring from type
			if (typeVarWord.Find("::") != -1 || typeVarWord.Find("->") != -1)
			{
				type = infer.Infer(mp, typeVarWord, scope, bcl, mp->FileType(), false);
			}
			else
			{
				const DType* cd = mp->FindSym(&typeVarWord, &scope, &bcl, FDF_NoConcat | FDF_SlowUsingNamespaceLogic);
				if (cd)
				{
					def = cd->Def();
					if (StartsWith(def, "class"))
					{
						def = def.Mid(sizeof("class") - 1);
						def.TrimLeft();
						TokenGetField2InPlace(typeVar, '(');
					}
					if (StartsWith(def, "struct"))
					{
						def = def.Mid(sizeof("struct") - 1);
						def.TrimLeft();
						TokenGetField2InPlace(typeVar, '(');
					}
					type = ::TokenGetField(def, " \t\r\n{");
					symScope = cd->Scope();

					if (mp->FileType() == CS) // C#
					{
						GetStructMembers(type, types, mp, symScope); // struct members
						return !!types.size();
					}
					parseExpression(def.c_str());
				}
			}
		}
		if (templParamsProvided || def.find(stOpen) != -1 || typeVar.Find('(') != -1 || colonSeparator)
			GetTemplateParameters(typeVar, type, types); // array or template parameters or function or "for" keyword's argument support
		else
			GetStructMembers(type, types, mp, symScope); // struct members
		return !!types.size();
	}

	return false;
}

void InferType::GetTemplateParameters(const WTString& typeVar, const WTString& type, std::vector<WTString>& types)
{
	if (!type.IsEmpty())
	{
		enum eState
		{
			FINDING,
			PARAMETERS,
		};

		eState state = FINDING;
		WTString param;
		int angleBracketCount = 0;
		for (int i = 0; i < type.GetLength(); i++)
		{
			const TCHAR c = DecodeChar(type[i]);
			switch (state)
			{
			case FINDING:
				if (c == '<')
				{
					state = PARAMETERS;
					angleBracketCount = 1;
				}
				break;
			case PARAMETERS:
				if (c == '<')
					angleBracketCount++;
				if (c == '>')
					angleBracketCount--;
				if ((angleBracketCount == 1 && c == ',') || (angleBracketCount == 0 && c == '>'))
				{
					if (!param.IsEmpty())
					{
						types.push_back(param);
						if (c == '>')
							return;
						param = "";
					}
				}
				else if (!IsWSorContinuation(c))
					param += c;
				break;
			}
		}

		types.push_back(type);
	}
}

void InferType::GetInferredTypesFromArguments(const WTString& type, std::vector<WTString>& types, MultiParsePtr mp, WTString scope)
{
	if (!type.IsEmpty())
	{
		enum eState
		{
			FINDING,
			PARAMETERS,
		};

		eState state = FINDING;
		WTString param;
		int parenCounter = 0;

		for (int i = 0; i < type.GetLength(); i++)
		{
			const TCHAR c = DecodeChar(type[i]);
			switch (state)
			{
			case FINDING:
				if (c == '(')
				{
					state = PARAMETERS;
					parenCounter = 1;
				}
				break;
			case PARAMETERS:
				if (c == '(')
					parenCounter++;
				else if (c == ')')
					parenCounter--;
				if ((c == ',' && parenCounter == 1) || (c == ')' && parenCounter == 0))
				{
					if (!param.IsEmpty())
					{
						InferType infer;
						// param = ::TokenGetField(param, "(<");
						WTString bcl = mp ? mp->GetBaseClassList(mp->m_baseClass) : "";
						WTString inferredType = infer.Infer(mp, param, scope, bcl, mp->FileType(), false);

						types.push_back(inferredType);
						if (c == ')')
							return;
						param = "";
					}
				}
				else
					param += c;
				break;
			}
		}

		types.push_back(type);
	}
}

void InferType::GetStructMembers(const WTString& type, std::vector<WTString>& types, MultiParsePtr mp,
                                 WTString scope)
{
	WTString bcl = mp ? mp->GetBaseClassList(mp->m_baseClass) : "";
	const DType* cd = mp->FindSym(&type, &scope, &bcl, FDF_NoConcat | FDF_SlowUsingNamespaceLogic);

	WTString symScope = cd ? cd->Scope() : "";
	if (symScope == "")
		symScope = ":";
	symScope += type + ":";

	DBQuery query(mp);

	std::vector<std::tuple<int, WTString, WTString>> variables;

	query.FindAllSymbolsInScopeList(symScope.c_str(), bcl.c_str());

	for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
	{
		if (dt->type() != VAR &&
		    dt->type() != PROPERTY) // C#
			continue;

		WTString itemSymScope = dt->SymScope();
		WTString itemParentScope;
		WTString itemName;
		WTString inferredType;
		for (int j = itemSymScope.GetLength() - 1; j >= 0; j--)
		{
			if (itemSymScope[j] == ':')
			{
				itemParentScope = itemSymScope.Left(j);
				itemName = itemSymScope.Right(itemSymScope.GetLength() - j - 1);
				InferType infer;
				inferredType = infer.Infer(mp, itemName, itemParentScope, bcl, Src, false);
				break;
			}
		}

		if (!inferredType.IsEmpty() && !itemParentScope.IsEmpty())
		{
			int dbOffset = int(dt->GetDbOffset());
			variables.emplace_back(dbOffset, inferredType, itemName);
		}
	}

	// sort "variables" array by itemName
	std::sort(variables.begin(), variables.end(), [](const std::tuple<int, WTString, WTString>& a, const std::tuple<int, WTString, WTString>& b) {
		return std::get<2>(a) < std::get<2>(b);
	});

	// removing duplicates
	for (size_t i = 1; i < variables.size(); i++)
	{
		if (std::get<2>(variables[i - 1]) == std::get<2>(variables[i]))
		{
			variables.erase(variables.begin() + (int)i);
			i--;
		}
	}

	// sort variables array by dbOffset so we get the same order as we have in the file
	std::sort(variables.begin(), variables.end(), [](const std::tuple<int, WTString, WTString>& a, const std::tuple<int, WTString, WTString>& b) {
		return std::get<0>(a) < std::get<0>(b);
	});

	for (auto& variable : variables)
	{
		types.push_back(std::get<1>(variable));
	}
}
