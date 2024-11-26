
enum VBParseState
{
	VBPS_NONE,
	VBPS_DEFINENEXTSTATEMENT = 0x1,
	VBPS_DIMSTATEMENT = 0x2
};

template <class VP> class LangWrapperVB : public VP
{
  public:
	using BASE = VP;
	using BASE::ClearLineState;
	using BASE::CurChar;
	using BASE::CurPos;
	using BASE::DebugMessage;
	using BASE::DecDeep;
	using BASE::FileType;
	using BASE::GetBaseScope;
	using BASE::GetBufLen;
	using BASE::GetCp;
	using BASE::GetFType;
	//	using BASE::GetLineStr;
	using BASE::HandleUsingStatement;
	using BASE::IncCP;
	using BASE::IncDeep;
	using BASE::InLocalScope;
	using BASE::IsDef;
	using BASE::IsDone;
	using BASE::m_buf;
	using BASE::m_cp;
	using BASE::m_deep;
	using BASE::m_inIFDEFComment;
	using BASE::m_inMacro;
	using BASE::m_InSym;
	using BASE::m_mp;
	using BASE::m_parseFlags;
	using BASE::m_ReadAheadInstanceDepth;
	using BASE::m_writeToFile;
	using BASE::MethScope;
	using BASE::NextChar;
	using BASE::OnChar;
	using BASE::OnComment;
	using BASE::OnCSym;
	using BASE::OnDirective;
	using BASE::OnNextLine;
	using BASE::PF_NONE;
	using BASE::PF_TEMPLATECHECK_BAIL;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;
	using BASE::VPS_BEGLINE;
	using BASE::VPS_NONE;

	LangWrapperVB(int fType) : VP(fType)
	{
		g_doDBCase = FALSE;
	}
	~LangWrapperVB()
	{
	}

	virtual void DoParse()
	{
		VP::DoParse();
	}
	virtual int GetLangType()
	{
		return GetFType();
	}
	virtual void _DoParse()
	{
		DEFTIMERNOTE(VAP__DoParseTimer, NULL);
		// main parsing loop
		for (char c; (c = CurChar()) != '\0'; IncCP())
		{
			if (IsDone())
			{
				if (StrNCmp("%>", CurPos(), 2) == 0)
					OnNewLineVB(); // Process current line, fixes: "end sub %>"
				return;
			}
			// VB does not support \escape chars Case:3904
			// 			if(c == '\\' && NextChar())
			// 			{
			// 				m_cp++;
			// 				if(m_buf[m_cp] == '\r' && m_buf[m_cp+1] == '\n')
			// 					m_cp++;
			// 				if(m_buf[m_cp] == '\n' || m_buf[m_cp] == '\r')
			// 					m_curLine++;
			// 				continue;
			// 			}

			if (State().m_inComment)
			{
				OnChar();
				State().m_lastChar = c;
				if (m_buf[m_cp] == '\r' && m_buf[m_cp + 1] == '\n')
					IncCP();
				if (m_buf[m_cp] == '\n' || m_buf[m_cp] == '\r')
				{
					OnNewLineVB();
					OnNextLine();
					c = '\n';
				}
				if (c == State().m_inComment || (State().m_inComment == '#' && c == '\n'))
				{
					if (c == '#')
						continue; // #define with a # in it, continue
					if (c == '*')
					{
						if (NextChar() != '/')
							continue; // not the end
						IncCP();      // eat the /
					}
					if (IsDone())
						return;
					OnComment('\0');
				}
			}
			else
			{
				OnChar();
				switch (c)
				{
				case '\'':
					OnComment('\n');
					continue; // skip inSym = FALSE;  below
				case '"':
					OnComment(c);
					continue; // skip inSym = FALSE;  below
				case '#':
					OnComment(c);
					OnDirective();
					continue; // skip inSym = FALSE;  below
					          // EOL
				case '\r':
					// catch \r's only
					if (NextChar() == '\n')
						IncCP();
					// what about \r\r\n?
				case '\n':
					if (m_cp > 2 && (m_buf[m_cp - 1] == '_' || (m_buf[m_cp - 1] == '\r' && m_buf[m_cp - 2] == '_')))
					{
						// [case: 60988] line continuation
						if (State().m_lastWordPos == &m_buf[m_cp - 1] || State().m_lastWordPos == &m_buf[m_cp - 2])
							State().m_lastWordPos = State().m_lastScopePos;
						OnNextLine();
					}
					else
					{
						OnNewLineVB();
						OnNextLine();
						m_InSym = FALSE;
					}
					continue;
					// WhiteSpace
				case ' ':
				case '\t':
					if (m_InSym)
					{
						// set m_lastChar, so "foo.bar baz", baz does not see last char as '.'
						State().m_lastChar = ' ';
						m_InSym = FALSE;
					}
					continue;
					// Open brackets {([<
				case '<':
					// template?
					{
						// scan ahead to see if <>'s match
						// check FwIS because we don't IncDeep until linebreak - (case=9311)
						//		so checking for template here: while x < y
						//		will be confused by this on the next line: if y > x
						//		since FwIS isn't 'while' inside of IsTemplate when called at CurPos().
						// Using IsTemplateCls to handle property attributes: <Default> ...
						if (FwIS("While") || FwIS("If") || FwIS("Do") || FwIS("For") || FwIS("ElseIf"))
							break;
						LangWrapperVB<IsTemplateCls> it(FileType()); // use correct _DoParse
						if (!it.IsTemplate(CurPos() + 1, GetBufLen() - (GetCp() + 1), m_ReadAheadInstanceDepth))
						{
							if (m_parseFlags & PF_TEMPLATECHECK_BAIL)
								return;
							State().m_parseState = VPS_ASSIGNMENT;
							break;
						}
					}
				case '(':
				case '[':
					//					if(!m_parseFlags)
					//						break;
				case '{': {
					if (!m_inIFDEFComment)
					{
						if (c == '(' && State().m_parseState != VPS_ASSIGNMENT && !IsDef(m_deep) && !InLocalScope() &&
						    m_deep)
						{
							State().m_defType = FUNC;           // constructor
							State().m_defAttr |= V_CONSTRUCTOR; // constructor
							OnDef();
						}
						if (c == '[' && IsDef(m_deep))
						{
							OnDef();
						}
						if (State().m_parseState == VPS_NONE)
						{
							// *foo = 'x'; // set VPS_ASSIGNMENT
							ClearLineState(CurPos());
							State().m_parseState = VPS_ASSIGNMENT;
						}

						if (c == '{')
						{
							// 								if(c == '{' && State().m_parseState != VPS_ASSIGNMENT &&
							// State().m_defType
							// == VAR
							// && InLocalScope(m_deep))
							// 								{
							// 									// something is whacked, macro freaked out scope?
							// 									while(m_deep && InLocalScope(m_deep))
							// 										m_deep--;
							// 								}
						}
						if (c == '{' && m_deep && State(m_deep - 1).m_lastChar == ':')
						{
							// recover scope for constructors "foo(int i): m_bar(i){"
							DecDeep();
						}
						if (c == '{' && IsDef(m_deep))
							OnDef();
					}

					State().m_lastChar = c;
					IncDeep();
					State(m_deep + 1).m_defType = NULL;
					if (State(m_deep - 1).m_defType == CLASS)
						State().m_privilegeAttr = V_PRIVATE;
					m_InSym = FALSE;
					continue;
				}
					// Close brackets })]>
				case '>':
					if (m_deep && State(m_deep - 1).m_lastChar != '<')
					{
						if (State().m_parseState != VPS_ASSIGNMENT)
							State().m_defType = NULL; // "foo*bar > 3" bar is not a pointer to foo
						State().m_parseState = VPS_ASSIGNMENT;
						if (m_cp && m_buf[m_cp - 1] == '-') // pretend a foo->bar is the same as a foo.bar
							c = '.';
						State().m_lastWordPos = CurPos(); // points to beginning of last sym
						break;                            // if( a < b || a > b)
					}
				case ')':
				case ']':
					//					if(!m_parseFlags)
					//						break;
				case '}':
					if (!m_inIFDEFComment || !strchr(")]}>", c))
					{
						CHAR mc = strchr("()[]{}<>", c)[-1];
						if (!m_deep || mc != State(m_deep - 1).m_lastChar)
						{
							DebugMessage("Brace Mismatch");
							// recover from mismatch
							while (m_deep && mc != State(m_deep - 1).m_lastChar)
								DecDeep();
						}

						if (c == '}' && IsDef(m_deep))
							OnDef();
					}

					if (!m_inIFDEFComment)
					{
						if (c == ')' && GetLangType() == VBS && m_deep && !IsDef(m_deep) &&
						    (State(m_deep - 1).m_defType == FUNC || State(m_deep - 1).m_defType == PROPERTY))
						{
							SetDefType(VAR); // VBS: function f(arg) `define arg as a var
							OnDef();
						}

						if (IsDef(m_deep))
						{
							if (c == '>' && !BASE::InParen(m_deep))
								OnDef();
							if (c == ')')
							{
								if (m_deep && InLocalScope(m_deep - 1) && State().m_parseState != VPS_ASSIGNMENT)
								{
									// if(foo & bar)  is not defining bar,
									// but if(foo & bar = 123), bar is defined
								}
								else if (State().GetParserStateFlags())
									OnDef();
							}
						}
					}

					DecDeep();
					if (c == ']' && State().m_begLinePos[0] == '[' /*!State().m_lwData*/)
					{
						// "[c# attribute]" // pretend there is a ; here "foo(){};"
						ClearLineState(CurPos());
						m_InSym = FALSE;
						continue;
					}
					if (c == '}' && !m_inMacro && !m_inIFDEFComment)
						State().m_conditionalBlocCount++;

					if (c == '}') // foo(){...} no semicolon needed
					{
						if (State().m_defType == CLASS && LwIS("extern")) // extern "C" {}
						{
							// We set extern "C"{ to CLASS so InLocalScope is not set and all defs appear in global
							// scope. Now, however, a ; is not expected after the }, so clear and continue.
							ClearLineState(CurPos());
							m_InSym = FALSE;
							continue;
						}

						if (GetLangType() != CS && (State().m_defType == CLASS || State().m_defType == C_ENUM ||
						                            State().m_defType == STRUCT)) // class foo{} bar;
						{
							if (!StartsWith(State().m_begLinePos, "typedef"))
							{
								State().m_parseState = VPS_BEGLINE;
								State().m_lastScopePos = CurPos();
							}
						}
						else if (State().m_defType != VAR) // not int i[]={1, 2}, j;
						{
							// "foo(){}" // pretend there is a ; here "foo(){};"
							ClearLineState(CurPos());
							m_InSym = FALSE;
							continue;
						}
						// hack for methods in file for WTString.h
						if (!m_deep && m_parseFlags == PF_NONE)
						{
							ClearLineState(CurPos());
							m_InSym = FALSE;
							continue;
						}
					}
					// handle typecasting (foo->Bar())->MarMethod();
					//					if(c == ')' && !State().m_lwData && State(m_deep+1).m_lwData)
					//					{
					//						// don't do it for (HWND)::SendMessage()
					//						for(ULONG p = m_cp+1; wt_isspace(m_buf[p]);p++);
					//						if(m_buf[p] != ':')
					//							State().m_lwData = State(m_deep+1).m_lwData; // (foo.bar())
					//					}
					break;

					// If def call OnDef on ";,)="
					// Assignment, could be definition
				case '=':
					if (m_buf[m_cp + 1] == '=')
					{
						// ==, not a def
						if (State().m_parseState != VPS_ASSIGNMENT)
							State().m_defType = NULL; // "foo*bar == 3" bar is not a pointer to foo
						IncCP();                      // eat the other '='
					}
					State().m_parseState = VPS_ASSIGNMENT; // assignment
					State().m_lwData.reset();
					break;
				case ',': {
					if (GetLangType() == VBS && m_deep && !IsDef(m_deep) &&
					    (State(m_deep - 1).m_defType == FUNC || State(m_deep - 1).m_defType == PROPERTY))
					{
						SetDefType(VAR); // VBS: function f(arg, arg2) `define arg as a var
						OnDef();
					}

					if (IsDef(m_deep) && State().GetParserStateFlags())
					{
						const BOOL restoreStateFlagToDim = State().HasParserStateFlags(VBPS_DIMSTATEMENT) != 0;
						OnDef();
						if (restoreStateFlagToDim)
						{
							// OnDef clears stateFlags; need to restore for
							// rest of items that are separated by commas
							State().SetParserStateFlags(VBPS_DIMSTATEMENT);
						}
					}
					State().m_argCount++;

					if (BASE::InParen(m_deep) && !StartsWith(State(m_deep - 1).m_lastWordPos, "for") &&
					    !StartsWith(State(m_deep - 1).m_lastWordPos, "Dim"))
					{
						// foo(int a, int b)
						// pretend there is a ; here
						ClearLineState(CurPos());
						m_InSym = FALSE;
						continue;
					}
					else if (State().m_defType)
						State().m_parseState = VPS_BEGLINE; // int a=1, b

					State().m_lastWordPos = CurPos(); // points to beginning of last sym
					State().m_lwData.reset();
				}
				break;
				case '.':
					if (c == '&' && NextChar() == '&')
						IncCP();
					else
					{
						// typecasting
						if (!State().m_lwData)
							State().m_lwData = State(m_deep + 1).m_lwData;
						break;
					}

					// other symbols, must be an assignment
				case '~':
				case ':':
				case ';':
				case '*':
				case '&':
				case '-':
				case '!':
				case '@':
				case '%':
				case '^':
				case '+':
				case '|':
				case '?':
					if (State().m_parseState != VPS_ASSIGNMENT)
					{
						State().m_defType = NULL;              // "foo*bar + 3" bar is not a pointer to foo
						State().m_parseState = VPS_ASSIGNMENT; // assignment
						State().m_lastScopePos = CurPos();
					}
					State().m_lwData.reset();
					break;
				case '`':
					DebugMessage("Invalid char");
					break;
					// AlpgaNum, _ or maybe ~
					// else ~MyClass()
				default:
					// Gotta be CSym?
#ifdef DEBUG_VAPARSE
					if (!(ISCSYM(c) || c == '~'))
						DebugMessage("NonCSym");
#endif // DEBUG_VAPARSE
					if (!m_InSym)
					{
						// set beginning of line
						if (State().m_parseState == VPS_NONE)
						{
							ClearLineState(CurPos());
							State().m_parseState = VPS_BEGLINE;
							if (m_deep && State(m_deep - 1).m_defType == C_ENUM)
							{
								if (!FwIS("End"))
								{
									State().m_defType = C_ENUMITEM;
									State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
								}
							}
						}
						// set m_lastScopePos
						if (State().m_parseState != VPS_ASSIGNMENT /*&& State().m_defType != FUNC*/)
						{

							if (State().m_lastChar != '.' && State().m_lastChar != '~')
								if (State().m_defType != FUNC)         // int foo() const;
									State().m_lastScopePos = CurPos(); // points to beginning of last sym's scope
									                                   // "foo.bar.baz"// foo, not baz
						}
						if (!m_inIFDEFComment)
						{
							ProcessVBSym();
							OnCSym();
						}
						State().m_lastWordPos = CurPos(); // points to beginning of last sym
						m_InSym = TRUE;
					}
					State().m_lastChar = c;
					continue; // skip inSym = FALSE;  below
				}
				// non whitespace non-alphacsym
				if (State().m_parseState == VPS_NONE)
				{
					ClearLineState(CurPos());
					State().m_parseState = VPS_BEGLINE;
				}
				State().m_lastChar = c;
				m_InSym = FALSE;
			}
		}

		// Cleanup
		if (State().m_inComment)
		{
			OnComment(0);
		}
	}
	void SetDefType(ULONG type)
	{
		// This causes the symbol to be added at end of statement.
		if (!State().m_defType) // Don't over write Class prev type
			State().m_defType = type;
		State().m_parseState = VPS_ASSIGNMENT;
		State().SetParserStateFlags(VBPS_DIMSTATEMENT);
	}

	virtual void DoScope()
	{
		if (CwIS("me"))
		{
			DType* dt = m_mp->FindExact(GetBaseScope());
			if (dt)
				State().m_lwData = std::make_shared<DType>(dt);
			else
				State().m_lwData.reset();
		}
		else if (CwIS("MyBase"))
		{
			WTString sym = GetCStr(CurPos());
			WTString baseClass = GetBaseScope();
			DType* dt = m_mp->FindSym(&sym, NULL, &baseClass);
			if (dt)
				State().m_lwData = std::make_shared<DType>(dt);
			else
				State().m_lwData.reset();
		}
		else
			VP::DoScope();
	}
	WTString myGetLineStr(ULONG deep)
	{
		LPCSTR endl = CurPos();
		while (*endl && *endl != '\r' && *endl != '\n')
			endl++;
		WTString lineStr = BASE::GetSubStr(State(deep).m_begLinePos, endl);
		WTString def = ParseLineClass_SingleLineFormat(lineStr, FALSE, VB);
		def.ReplaceAll("_", "", TRUE);
		return def;
	}

	virtual void OnDef()
	{
		VP::OnDef();
		State().m_ParserStateFlags = VBPS_NONE;
		if (m_mp && (m_writeToFile || m_mp->GetParseType() == ParseType_GotoDefs))
		{
			if (!m_mp->GetParseAll() && m_deep && !IS_OBJECT_TYPE(State(m_deep - 1).m_defType))
				return; // don't add local vars to global dict
			WTString symScope = MethScope();
			if (FwIS("Inherits"))
			{
				// add MyBase
				WTString scope = StrGetSymScope(symScope);
				symScope = scope + DB_SEP_STR + "MyBase";
				if (!m_mp->FindExact2(symScope))
				{
					WTString def = myGetLineStr(m_deep);
					BASE::OnAddSymDef(symScope, def, CLASS, 0); // Add MyBase
					BASE::OnAddSymDef(scope, def, CLASS, 0);    // add Inherits to class definition
				}
			}
			else if (m_writeToFile || m_mp->GetParseAll() || !m_mp->FindExact2(symScope))
			{
				WTString def = myGetLineStr(m_deep);
				BASE::OnAddSymDef(symScope, def, State().m_defType, 0);
			}
		}
	}

	virtual void OnError(LPCSTR errPos)
	{
		// if we start underlining symbol errors in vb then reopen these bugs:
		// 4476, 4477, 4478, 5061

		if (State().m_inComment) // Only underline spelling errors
			VP::OnError(errPos);
	}

	virtual WTString Scope(ULONG deep)
	{
		DEFTIMERNOTE(VAP_ScopeVB, NULL);
#define MAX_SCOPE_LEN 512
		const std::unique_ptr<CHAR[]> bufVec(new CHAR[MAX_SCOPE_LEN + 1]);
		CHAR* buf = &bufVec[0];
		ULONG idx = 0;
		if (!deep)
			return WTString(DB_SEP_CHR);
		for (ULONG i = 0; i < deep && idx < MAX_SCOPE_LEN; i++)
		{

			LPCSTR p1 = State(i).m_lastScopePos;

			if (idx < MAX_SCOPE_LEN && (ISCSYM(*p1) || strchr(":.~{", *p1)))
			{
				buf[idx++] = DB_SEP_CHR;
#define LsIS(s) StartsWith(lwScopePos, s)
				LPCSTR p2;
				for (p2 = p1; idx < MAX_SCOPE_LEN && (*p2 == '.' || ISCSYM(*p2)); p2++)
				{
					if (*p2 == '.')
						buf[idx++] = DB_SEP_CHR;
					else
						buf[idx++] = *p2;
				}
				if (i < m_deep && !IS_OBJECT_TYPE(State(i).m_defType) && InLocalScope(i + 1))
				{
					WTString idStr;
					if (p1 == p2)
						idStr.WTFormat("BRC-%lu", i * 100 + State(i).m_conditionalBlocCount);
					else
						idStr.WTFormat("-%lu", i * 100 + State(i).m_conditionalBlocCount);
					for (LPCSTR p = idStr.c_str(); *p && idx < MAX_SCOPE_LEN; p++)
						buf[idx++] = *p;
				}
			}
		}
		buf[idx++] = '\0';
		WTString scope(buf);
		return scope;
	}

	// g_doDBCase still needs work to ensure it is correct
	// Creating a member StartsWith that does not rely on the flag fixes most of the issues
	BOOL StartsWith(LPCSTR buf, LPCSTR begStr)
	{
		// No Case
		if (!buf)
			return FALSE;
		int i;
		for (i = 0; buf[i] == begStr[i] ||
		            (!(buf[i] & 0x80) && !(begStr[i] & 0x80) && tolower(buf[i]) == tolower(begStr[i])) && begStr[i];
		     i++)
			;
		return (!begStr[i] && !ISCSYM(buf[i]));
	}

	void ProcessVBSym()
	{
		// when parsing VB doDBCase should not normally be set;
		// but if loading a multi-language project it might not be right - ok per case 12815
		// 		ASSERT_ONCE(!g_doDBCase || (GlobalProject && GlobalProject->IsBusy()));
		if (!InLocalScope(m_deep))
		{
			if (CwIS("Enum"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = C_ENUM;
			}
			else if (CwIS("Module"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = MODULE;
			}
			else if (CwIS("Namespace"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = NAMESPACE;
			}
			else if (CwIS("Class"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = CLASS;
			}
			else if (CwIS("Structure"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = STRUCT;
			}
			else if (CwIS("Interface"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = C_INTERFACE;
			}
			else if (CwIS("Sub") || CwIS("Function"))
			{
				State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
				State().m_defType = FUNC;
			}
			else if (CwIS("Imports"))
			{
				OnDirective();
				HandleUsingStatement();
			}
			else if (CwIS("Property"))
			{
				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
				State().m_defType = PROPERTY;
			}
			else if (CwIS("Public"))
			{
				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
				State().m_defType = VAR;
				State().m_privilegeAttr = 0;
			}
			else if (CwIS("Private"))
			{
				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
				State().m_defType = VAR;

				State().m_privilegeAttr = V_PRIVATE;
			}
			else if (CwIS("Protected"))
			{
				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
				if (!State().m_defType)
					State().m_defType = VAR;
				State().m_privilegeAttr = V_PROTECTED;
			}
			else if (CwIS("Let"))
			{
				if (State().m_defType != PROPERTY)
				{
					State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
					State().m_defType = VAR;
				}
			}
			else if (State().HasParserStateFlags(VBPS_DEFINENEXTSTATEMENT))
			{
				State().m_lastScopePos = CurPos();
				OnDef();
			}
		}
		// 		else if(CwIS("Property"))
		// 		{
		// 			if (m_deep && State(m_deep-1).m_defType == PROPERTY)
		// 			{
		// 				// [case: 71057]
		// 				// recover from Property that has no explicit terminator
		// 				DecDeep();
		// 				State().m_StateFlags = VBPS_NONE;
		// 				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
		// 				State().m_defType = PROPERTY;
		// 			}
		// 		}

		if (CwIS("Dim") || CwIS("Declare") || CwIS("Const") || CwIS("ByVal") || CwIS("ByRef"))
		{
			if (!State().m_defType)
			{
				if (CwIS("Const"))
					State().m_defType = DEFINE;
				else
					State().m_defType = VAR;
				State().SetParserStateFlags(VBPS_DIMSTATEMENT);
			}
		}
		else if (CwIS("As"))
		{
			State().m_lastScopePos = State().m_lastWordPos;
			// FUNC and C_ENUM already have OnDef called before the "As" keyword
			if (State().m_defType != FUNC && State().m_defType != C_ENUM)
			{
				if (State().m_defType != PROPERTY)
					State().m_defType = VAR;
				OnDef();
			}
			State().m_parseState = VPS_ASSIGNMENT;
		}
		else if (CwIS("Event"))
		{
			State().m_defType = EVENT;
			State().SetParserStateFlags(VBPS_DEFINENEXTSTATEMENT);
		}
	}
	virtual int GetSuggestMode()
	{
		if (State().m_inComment == '\n')
			return SUGGEST_NOTHING;
		else if (State().m_inComment)
			return __super::GetSuggestMode(); // Don't force SUGGEST_TEXT in strings. Case 20647
		return __super::GetSuggestMode() | SUGGEST_TEXT;
	}

	virtual void GetScopeInfo(ScopeInfo* scopeInfo)
	{
		__super::GetScopeInfo(scopeInfo);
		scopeInfo->m_isDef = State().GetParserStateFlags() != 0; // Fixes IsDef in VB/VBS,  case=21966,  case=31848
	}

	void OnNewLineVB()
	{
		if (IsDef(m_deep) && State().GetParserStateFlags() && !FwIS("End"))
		{
			OnDef();
		}
		if (FwIS("Inherits"))
		{
			OnDef();
			State().m_defType = NULL; // Not a function, do not perform IncDeep below
		}
		// IncDeep keywords
		if (FwIS("If"))
		{
			State().m_lastScopePos = State().m_begLinePos;
			if (LwIS("then") || !CurLineContainsSymVB("then"))
				IncDeep();
		}
		else if (FwIS("Get") || FwIS("Set"))
		{
			// "Set" breaks TestComplete Unit_Library.svb
			// Only process Set and Get in properties
			if (m_deep &&
			    State(m_deep - 1).m_defType == PROPERTY
			    // Case: 17244
			    // Ignore "Set" as an assignment within a property ie: Set i = 0
			    && (!FwIS("Set") || TokenGetField(State().m_begLinePos, "<>{}()'\r\n").Find('=') == -1))
			{
				State().m_lastScopePos = State().m_begLinePos;
				IncDeep();
			}
			else if (!IsDef(m_deep) && FwIS("Set") && GetLangType() == VBS)
				SetDefType(VAR); // VBS: Set foo = 0 ' define foo as a var
		}
		else if (FwIS("SyncLock") || FwIS("SyncLock") || FwIS("Using") || FwIS("For") || FwIS("Try") || FwIS("While") ||
		         FwIS("Do") || FwIS("Select") || FwIS("With"))
		{
			State().m_lastScopePos = State().m_begLinePos;
			IncDeep();
		}
		else if (FwIS("End") || FwIS("Loop") || FwIS("Next") || FwIS("Wend"))
		{
#ifdef _DEBUG
			WTString begLine =
			    m_deep ? BASE::GetSubStr(State(m_deep - 1).m_begLinePos, State().m_begBlockPos) : NULLSTR;
			WTString lastWord = GetCStr(State().m_lastScopePos);
			if (!begLine.contains(lastWord))
			{
				begLine.MakeLower();
				lastWord.MakeLower();
				if (!begLine.contains(lastWord))
				{
					if (!StartsWithNC(lastWord, "Loop") && !StartsWithNC(lastWord, "Next") &&
					    !StartsWithNC(lastWord, "Wend"))
						DebugMessage("Non-matching End tag.");
				}
			}
#endif // _DEBUG
			DecDeep();
			State().m_ParserStateFlags = VBPS_NONE;
		}
		else if (State().m_defType && State().m_defType != VAR && State().m_defType != DEFINE &&
		         State().m_defType != EVENT && State().m_defType != C_ENUMITEM)
		{
			IncDeep();
			return;
		}

		ClearLineState(CurPos());
	}

	BOOL CurLineContainsSymVB(LPCSTR sym)
	{
		WTString ln(State().m_begLinePos, ptr_sub__int(CurPos(), State().m_begLinePos));
		ln.MakeLower();
		return ln.contains(sym);
	}
	virtual WTString GetNodeText(LPCSTR symPos, ULONG* outFlags)
	{
		if (outFlags)
			*outFlags = 0;
		// case 20488: some VBScript items not colored correctly in VA Outline
		// strip "=" or ' comment
		return TokenGetField(symPos, "='\r\n");
	}

	virtual LPCSTR GetParseClassName()
	{
		return "LangWrapperVB";
	}
};
