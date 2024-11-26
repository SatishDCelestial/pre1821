
enum
{
	VPSF_IS_JS_MEMBER_FLAG = 0x200,
	VPSF_IS_JS_PARAMETER_FLAG = 0x400,
	VPSF_IS_JS_ASSIGN_GLOBAL = 0x800
}; // #extendsParserStateFlagsEnum
#define V_PREFERREDDEFINITION_INVERSE V_PREFERREDDEFINITION
template <class VP> class LangWrapperJS : public LangWrapperCS<VP>
{
	ULONG m_edLine;
	BOOL m_inFindRef;
	BOOL m_inOutline;

  public:
	using BASE = VP;
	using BASE::CurChar;
	using BASE::CurPos;
	using BASE::GetParseClassName;
	using BASE::InComment;
	using BASE::InLocalScope;
	using BASE::IsDef;
	using BASE::IsDone;
	using BASE::IsXref;
	using BASE::m_curLine;
	using BASE::m_deep;
	using BASE::m_InSym;
	using BASE::m_mp;
	using BASE::m_parseGlobalsOnly;
	using BASE::NextChar;
	using BASE::OnDef;
	using BASE::OnSymbol;
	using BASE::PrevChar;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;

	LangWrapperJS(int fType) : LangWrapperCS<VP>(fType)
	{
		g_doDBCase = TRUE;
		m_edLine = TERROW(g_currentEdCnt ? g_currentEdCnt->m_LastPos2 : 0);
		m_inFindRef = strcmp(GetParseClassName(), "VAParseMPFindUsage") != -1;
		m_inOutline = strcmp(GetParseClassName(), "VAParseFileOutline") != -1;
		SetDollarValid(true); // function Sys$UI$Silverlight$_Button$_mouseDown(){}
	}
	virtual int GetLangType()
	{
		return JS;
	}

	virtual void OnError(LPCSTR errPos)
	{
		if (State().m_inComment || m_inFindRef) // Only underline spelling errors
			VP::OnError(errPos);
	}
	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
	{
		if (symScope.IsEmpty())
			return;

		if (::wt_isdigit(symScope[1]))
		{
			// something screwy in html parser - uses depth as scope for
			// anonymous js functions, html meta tags <meta name="robots"...
			return;
		}

		// Give all definitions preferred attr, except global assignments
		State().m_defAttr ^= V_PREFERREDDEFINITION_INVERSE;
		if (StartsWith(def, "reservedword"))
		{
			// def as a space will hide minihelp info
			__super::OnAddSymDef(symScope, SPACESTR, RESWORD, 0);
			return;
		}
		if (WTString("function") == StrGetSym(symScope))
			return;
		if (type != FUNC && StrStrI(def.c_str(), "function"))
		{
			type = FUNC;
			attrs = 0;
		}
		if (State().HasParserStateFlags(VPSF_IS_JS_ASSIGN_GLOBAL))
		{
			// Assigning "foo=3;", add foo to global scope
			// Adding in this scope breaks find references.
			WTString globStr = DB_SEP_STR + StrGetSym(symScope);
			__super::OnAddSymDef(globStr, def, type, attrs);
		}
		else
			__super::OnAddSymDef(symScope, def, type, attrs);
	}
	virtual void OnNextLine()
	{
		__super::OnNextLine();
		// In JS, ';' is optional, below is the logic from _DoParse: ';':
		if (GetLangType() == JS) // Not in HTML
		{
			if (State().m_lastChar == '}')
				BASE::ClearLineState(CurPos()); // JS: no ';' expected after "var v = {}"
			else if (!strchr("=+-*&^%$#@!<>:\\", State().m_lastChar))
			{
				// Pretend there is an optional ';' at end of line.
				if (IsDef(m_deep))
					if (ISCSYM(State().m_lastScopePos[0]) ||
					    State().m_lastScopePos[0] == '~') // don't add "class foo{...};
						OnDef();
				if (State().m_defType != FUNC && !IS_OBJECT_TYPE(State().m_defType))
					BASE::ClearLineState(CurPos());
			}
		}
		m_InSym = FALSE;
	}
	virtual WTString Scope(ULONG deep)
	{
		if (!deep)
			return DB_SEP_STR;
		WTString scope; //  =__super::Scope(deep);
		for (ULONG i = 0; i < deep; i++)
		{
			LPCSTR scopePos = State(i).m_lastScopePos;
			if (ISCSYM(*scopePos))
			{
			again:
				if (m_parseGlobalsOnly && !IsDef(i))
					continue; // strip off all if/else before
				WTString sym = GetCStr(scopePos);
				if (sym != "prototype" && sym != "this")
					scope += DB_SEP_STR + sym;
				if (sym == "this")
				{
					// reset to first function?
					ULONG f = 1;
					for (; f < i && State(f).m_defType != FUNC; f++)
						;
					if (f < i)
						scope = Scope(f);
					WTString baseScope = TokenGetField(scope, ":-");
					if (baseScope.GetLength())
						scope = DB_SEP_STR + baseScope;
				}
				if (scopePos[sym.GetLength()] == '.')
				{
					// foo.bar.baz = 123; // change symscope to just foo.bar.baz
					if (scopePos == State(i).m_lastScopePos && sym != "this")
						scope = DB_SEP_STR + sym;
					scopePos += (sym.GetLength() + 1);
					goto again;
				}
				else if (i < m_deep && !m_parseGlobalsOnly &&
				         !IS_OBJECT_TYPE(State(i).m_defType)) // only local if/else/while/... blocks get -n scope tags
				{
					scope += '-';
					scope += itos(int(i * 100 + State(i).m_conditionalBlocCount));
				}
			}
		}
		return scope.GetLength() ? scope : DB_SEP_STR;
	}
	void SetDefType(ULONG type)
	{
		// This causes the symbol to be added at end of statement.
		if (!State().m_defType) // Don't over write Class prev type
			State().m_defType = type;
		State().m_parseState = VPS_ASSIGNMENT;
	}
	virtual void DoScope()
	{
		// Override stock Scope to work better in script files
		if (m_mp)
		{
			WTString sym = GetCStr(CurPos());

			WTString scope = Scope(m_deep);
			if (StartsWith(CurPos(), "this"))
			{
				WTString bscope = TokenGetField(scope, "-");
				DType* dt = m_mp->FindExact(bscope);
				if (dt)
					State().m_lwData = std::make_shared<DType>(dt);
				else
					State().m_lwData.reset();
			}
			else if (IsXref())
			{
				WTString bc;
				if (State().m_lwData)
					bc = State().m_lwData->SymScope();
				else
					bc = DB_SEP_STR + GetCStr(State().m_lastWordPos);
				WTString bcl = bc + '\f' + m_mp->GetBaseClassList(bc, false, 0, GetLangType());
				DType* dt = m_mp->FindSym(&sym, NULL, &bcl);
				if (!dt && (m_curLine < m_edLine || m_curLine > (m_edLine + 3)))
				{
					// Guess at BCL
					DType* data = m_mp->GuessAtBaseClass(sym, bc);
					(void)data;
				}

				if (!dt) // Take a stab in scope and NAMESPACE
					dt = m_mp->FindSym(&sym, &scope, NULL);

				if (dt)
					State().m_lwData = std::make_shared<DType>(dt);
				else
					State().m_lwData.reset();
			}
			else
			{
				DType* dt = m_mp->FindSym(&sym, &scope, NULL);
				if (dt)
					State().m_lwData = std::make_shared<DType>(dt);
				else
					State().m_lwData.reset();
			}

			if (IsDone() && sym.GetLength() && !State().m_lwData)
			{
				DType* t = m_mp->FindAnySym(sym);
				if (t)
					State().m_lwData = std::make_shared<DType>(t);
			}
			if (State().m_lwData)
				OnSymbol();
			else
				OnError(CurPos());
		}
	}
	virtual int GetSuggestMode()
	{
		int suggestBitsFlag = (Psettings && Psettings->m_defGuesses) ? SUGGEST_TEXT : 0;
		if (InComment())
		{
			// needs better guessing to be less annoying
			// 			if((CommentType() == '"' || CommentType() == '\'') && CommentType() == PrevChar())
			// 				return SUGGEST_TEXT;
			return SUGGEST_NOTHING;
		}
		if (IsXref())
			return SUGGEST_MEMBERS | suggestBitsFlag; // guess from surrounding text
		return SUGGEST_SYMBOLS | suggestBitsFlag;     // guess surrounding text
	}

	virtual void OnChar()
	{
		if (!InComment())
		{
			// Handle function_prototype: = function(){}
			if (CurChar() == ':' && NextChar() != ':' && !InComment())
			{
				if (State().m_parseState == VPS_ASSIGNMENT && StartsWith(State().m_begLinePos, "var") &&
				    VAR == State().m_defType)
					State().m_lwData.reset(); // var foo ? 1 : 2;"
				else
				{
					State().m_lastScopePos = State().m_lastWordPos;
					SetDefType(VAR); // extend({ extMeth: function(){}});
					if (InLocalScope(m_deep) /* && m_mp && m_mp->IsWriteToDFile()*/)
						State().SetParserStateFlags(VPSF_IS_JS_MEMBER_FLAG);
				}
			}
			if (CurChar() == '(' && !IsDef(m_deep))
				State().m_parseState = VPS_ASSIGNMENT; // don't let "alert(i);" define "alert"
			// Handle foo=3; // Define foo
			if (!State().m_inComment && CurChar() == '=' && NextChar() != '=' && !strchr("<>!=*=-&", PrevChar()) &&
			    State().m_lastChar != ']' // Don't add "array[1] = 0;"
			)
			{
				SetDefType(VAR);
				if (State().m_begLinePos == State().m_lastWordPos)
				{
					if (m_inOutline && m_mp && InLocalScope())
					{
						// See if it really is a global assignment
						WTString scope = Scope(m_deep);
						WTString sym = GetCStr(State().m_lastWordPos);
						DType* var = m_mp->FindSym(&sym, &scope, NULL);
						if (var && var->IsPreferredDef() /*StrGetSymScope(var->SymScope()).length() > 0*/)
							return; // not really a global
					}
					State().SetParserStateFlags(VPSF_IS_JS_ASSIGN_GLOBAL);
					// Mark assignments as non-preferred so goto can filter all assignments.
					// This actually sets preferred but it will be inverted when it gets added.
					State().m_defAttr |= V_PREFERREDDEFINITION_INVERSE;
				}
			}
		}

		// Cannnot do here, breaks _DoParse in HTML
		// 		// Strip out <!-- --!> comments
		// 		if((CurChar() == '<' && strncmp(CurPos(), "<!--", 4) == 0)
		// 			|| (CurChar() == '-' && strncmp(CurPos(), "--!>", 4) == 0))
		// 		{
		// 			IncCP();
		// 			IncCP();
		// 			IncCP();
		// 			IncCP();
		// 		}
		// 		else
		VP::OnChar();
	}
	virtual void OnCSym()
	{
		if (!InComment())
		{
			if (State().m_parseState != VPS_ASSIGNMENT)
				if (State().m_lastChar != '.')
					State().m_lastScopePos = CurPos();
			if (StartsWith(CurPos(), "this"))
			{
				// SetDefType(VAR); // this.Foo(0); is calling not defining, '=' will define
				if (State().m_parseState != VPS_ASSIGNMENT) // foo = this.bar; // Not defining a member
					State().SetParserStateFlags(VPSF_IS_JS_MEMBER_FLAG);
			}
			if (StartsWith(CurPos(), "dojo.declare"))
			{
				// This is a hack to help the dojo toolkit.  case=21251
				LPCSTR quotePos = strchr(CurPos(), '"');
				if (quotePos)
				{
					State().SetParserStateFlags(VPSF_IS_JS_MEMBER_FLAG);
					State().m_lastScopePos = quotePos + 1;
					SetDefType(FUNC);
				}
			}
			if (StartsWith(CurPos(), "function"))
			{
				State().SetParserStateFlags(VPSF_IS_JS_MEMBER_FLAG);
				State().m_defType = FUNC;
			}
			if (StartsWith(CurPos(), "extends"))
			{
				// [case: 31611] class Foo extends Bar (js 2)
				// copied this out of VAParserUC.h
				// still need to do support for class extends Bar (class extension)
				State().m_lastScopePos = State().m_lastWordPos;
				State().m_parseState = VPS_ASSIGNMENT; // ignore rest of line
			}
			if (m_deep && BASE::InParen(m_deep) && State(m_deep - 1).m_defType == FUNC)
			{
				State().SetParserStateFlags(VPSF_IS_JS_PARAMETER_FLAG);
				SetDefType(VAR); // function foo(arg1, arg2); // Define args
				State().m_lastScopePos = CurPos();
			}

			if (/*m_writeToFile && */ m_deep && BASE::InParen(m_deep))
			{
				if (State(m_deep - 1).m_defType == FUNC)
					SetDefType(VAR);
			}
		}
		if (!InComment() || m_inFindRef)
			__super::OnCSym();
	}
	virtual BOOL ShouldForceOnDef()
	{
		if (State().HasParserStateFlags(VPSF_IS_JS_MEMBER_FLAG | VPSF_IS_JS_ASSIGN_GLOBAL))
			return TRUE;
		if (State().HasParserStateFlags(VPSF_IS_JS_PARAMETER_FLAG) && m_mp && m_mp->GetParseAll() &&
		    m_mp->IsWriteToDFile())
			return TRUE;
		return FALSE;
	}
};
