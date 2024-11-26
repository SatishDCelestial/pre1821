// Not much in here since the default parser supports C++ and C#
// Might break out c# stuff from the default VAParse to add here?

#include "InferType.h"

template <class VP> class LangWrapperCS : public VP
{
	// #extendsParserStateFlagsEnum
	enum ParserStateFlagsEnumExt
	{
		VPS_FLAG_IN_FROM = 0x200,
		VPS_FLAG_USE_PARENT_SCOPE = 0x400,
		VPS_FLAG_GROUP = 0x800,
		VPS_FLAG_GENERIC_TYPE = 0x1000,
		VPS_FLAG_PATTERN = 0x2000
	};
	WTString mLinqVar;

  public:
	using BASE = VP;
	using BASE::CurPos;
	using BASE::FileType;
	using BASE::GetBufLen;
	using BASE::GetCp;
	using BASE::InLocalScope;
	using BASE::IsXref;
	using BASE::m_deep;
	using BASE::m_mp;
	using BASE::m_parseGlobalsOnly;
	using BASE::m_writeToFile;
	using BASE::MethScope;
	using BASE::NextChar;
	using BASE::Scope;
	using BASE::State;

	LangWrapperCS(int fType) : VP(fType)
	{
		g_doDBCase = TRUE;
		SetDollarValid(false);
	}
	virtual void OnCSym()
	{
		if (State().HasParserStateFlags(VPS_FLAG_PATTERN))
		{
			if (State().m_lastWordPos <= State().m_begLinePos)
			{
				// About to parse the pattern (just a type for now)
				State().m_defType = UNDEF;
				State().m_lastScopePos = CurPos();
			}
		}
		BASE::OnCSym();
		if (StartsWith(CurPos(), "is") && m_mp)
		{
			this->IncCP(); //Skip the 'i'
			this->IncDeep(); //IncDeep skips one character (so the 's')
			State().m_parseState = BASE::VPS_BEGLINE;
			State().AddParserStateFlags(VPS_FLAG_PATTERN);
		}
	}

	virtual void OnChar()
	{
		if (State().HasParserStateFlags(VPS_FLAG_PATTERN) && strchr("&|^=?!);", this->CurChar()))
		{
			// Hack to force OnDef to not take more than the actual variable name
			int savedBufLen = this->mBufLen;
			this->mBufLen = int(CurPos() - (this->GetBuf()) - 1);
			this->OnDef();
			this->mBufLen = savedBufLen;
			State().RemoveParserStateFlags(VPS_FLAG_PATTERN);
			this->DecDeep();
		}
		BASE::OnChar();
	}

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		// Support for: "string".length;
		if (!c && State().m_inComment == '"' && m_mp && NextChar() == '.')
		{
			DType* dt = m_mp->FindExact(":System:String");
			if (dt)
				State().m_lwData = std::make_shared<DType>(dt);
			else
				State().m_lwData.reset();
			State().m_parseState = BASE::VPS_BEGLINE;
		}
		VP::OnComment(c, altOffset);
	}

	virtual void DoScope()
	{
		if (m_deep && State(m_deep - 1).HasParserStateFlags(VPS_FLAG_USE_PARENT_SCOPE) && !IsXref() && m_mp)
		{
			if (m_deep && State(m_deep - 1).m_lwData)
			{
				WTString sym = State(m_deep - 1).m_lwData->SymScope() + DB_SEP_STR + GetCStr(CurPos());
				DType* dt = m_mp->FindExact(sym);
				if (dt)
				{
					State().m_lwData = std::make_shared<DType>(dt);
					return;
				}

				WTString bcl = m_mp->GetBaseClassList(State(m_deep - 1).m_lwData->SymScope(), false, 0, GetLangType());
				sym = GetCStr(CurPos());
				dt = m_mp->FindSym(&sym, NULL, &bcl);
				if (dt)
				{
					State().m_lwData = std::make_shared<DType>(dt);
					return;
				}
				State().m_lwData.reset();
			}
		}
		// support for: [attributecls member=val] class foo{};
		if (!State().m_lwData && m_deep && State(m_deep - 1).m_lastChar == '[')
		{
			WTString sym = GetCStr(CurPos()) + "Attribute";
			WTString scope = Scope();
			State().AddParserStateFlags(VPS_FLAG_USE_PARENT_SCOPE); // Use this attribute as scope for args
			DType* dt = m_mp->FindSym(&sym, &scope, NULL);
			if (dt)
			{
				State().m_lwData = std::make_shared<DType>(dt);
				return;
			}
		}
		VP::DoScope();

		// Linq support to look up iterator of a var from assignment
		if (State().m_lwData && State().m_lwData->MaskedType() == LINQ_VAR)
			State(m_deep).m_lwData = InferTypeFromAutoVar(State(m_deep).m_lwData, m_mp, FileType());
	}

	BOOL ShouldAddDef()
	{
		if (m_writeToFile && (!m_parseGlobalsOnly || !InLocalScope()))
			return TRUE;
		return FALSE;
	}
	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
	{
		if (StartsWith(def, "var"))
			VP::OnAddSymDef(symScope, def, LINQ_VAR, 0); // LINQ_VAR flag for Linq "var"
		else
		{
			if (StartsWith(def, "foreach")) // [case=14446]
				return;                     // this should or could be fixed earlier in the parse.
			VP::OnAddSymDef(symScope, def, type, attrs);
		}
	}
	virtual void ClearLineState(LPCSTR cPos)
	{
		__super::ClearLineState(cPos);
		if (FileType() == CS && !InLocalScope())
		{
			// [case 57884] c# access is default private, however namespaces are always public.
			if (StartsWith(CurPos(), "namespace"))
				State().m_privilegeAttr = 0; // public
			else if (StartsWith(CurPos(), "interface"))
				State().m_privilegeAttr = 0; // [case: 63893] interfaces are always public in C#
			else if (StartsWith(CurPos(), "using"))
				State().m_privilegeAttr = 0; // [case 57884] using statements to have public access
			else if (m_deep && C_ENUM == State(m_deep - 1).m_defType)
				State().m_privilegeAttr = 0; // enum items should be public
			else if (m_deep && C_INTERFACE == State(m_deep - 1).m_defType)
				State().m_privilegeAttr = 0; // [case: 63893] interfaces are always public in C#
			else
				State().m_privilegeAttr = V_PRIVATE;
		}
	}

	virtual BOOL ProcessMacro()
	{
		DEFTIMERNOTE(VAP__ProcessMacro, NULL);
		if (m_deep && State(m_deep - 1).HasParserStateFlags(VPS_FLAG_GENERIC_TYPE) && State(m_deep - 1).m_defType &&
		    State(m_deep - 1).m_lastChar == '<' && ShouldAddDef())
		{
			// Add C# generic types case=22131 case=11633
			// void genericMethod<type1, type2, ...>(){}
			const WTString varName(GetCStr(CurPos()));
			if (varName == "in" || varName == "out")
			{
				// [case: 63897] in/out are modifiers in C# generics
				// http://msdn.microsoft.com/en-us/library/dd469484.aspx
				// http://msdn.microsoft.com/en-us/library/dd469487.aspx
				return FALSE;
			}

			const WTString whereVar(Scope() + DB_SEP_STR + varName);
			if (!m_mp->FindExact(whereVar))
			{
				WTString def = TokenGetField(State().m_lastWordPos, ">,\t\n{/");
				OnAddSymDef(whereVar, def, CLASS, 0);
			}
		}
		if (!InLocalScope(m_deep) && State(m_deep).HasParserStateFlags(VPS_FLAG_GENERIC_TYPE))
		{
			// handle "where" in generic methods/types. case=11633
			//  void genericMethod<type>() where type: new(){}
			if (StartsWith(CurPos(), "where"))
			{
				State().m_parseState = BASE::VPS_ASSIGNMENT;
				State().SetParserStateFlags(BASE::VPSF_CONSTRUCTOR);
			}
		}
		// Linq support
		if (StartsWith(CurPos(), "from") && m_mp)
			State().AddParserStateFlags(VPS_FLAG_IN_FROM);
		if (State().HasParserStateFlags(VPS_FLAG_IN_FROM) || BASE::InParen(m_deep))
		{
			if (StartsWith(CurPos(), "group") && m_mp)
				State().AddParserStateFlags(VPS_FLAG_GROUP);
			else if (StartsWith(CurPos(), "into") && ShouldAddDef())
			{
				WTString group = TokenGetField(CurPos() + 4);
				if (group.GetLength())
				{
					WTString sym = Scope() + DB_SEP_STR + group;
					if (State(m_deep).HasParserStateFlags(VPS_FLAG_GROUP))
						OnAddSymDef(sym, "IGrouping<" + WTString(StrGetSym(mLinqVar)) + ">", VAR, 0);
					else
						OnAddSymDef(sym, "IEnumerable<" + WTString(StrGetSym(mLinqVar)) + ">", VAR, 0);
				}
			}
			else if (StartsWith(CurPos(), "in"))
			{
				// Handle foreach(object o in lst)
				LPCSTR pw = State().m_lastWordPos;
				if (m_mp)
				{
					if (pw != State().m_begLinePos && !StartsWith(State().m_begLinePos, "var"))
					{
						// foreach(string s in lst)
						VP::OnDef();
						// [case: 73981] after 'in' is typed, then like assign not def
						State().m_parseState = BASE::VPS_ASSIGNMENT;
						return FALSE;
					}
					// Get iterator type from list "foreach([var] s in list)"
					const WTString symName(GetCStr(pw));
					mLinqVar = Scope() + DB_SEP_STR + symName;
					if (!m_mp->FindExact(mLinqVar) && ShouldAddDef())
					{
						ReadToCls rtc(CS);
						WTString def("var " + symName + " in ");
						def += rtc.ReadTo(CurPos() + 2, GetBufLen() - (GetCp() + 2), ",)=;\r\n");
						OnAddSymDef(mLinqVar, def, LINQ_VAR, 0);
					}
				}
			}
			else if (BASE::InParen(m_deep) && StartsWith(CurPos(), "out"))
			{
				const BASE::ParserState& prevState = BASE::ConstState(m_deep - 1);

				// [case: 116073] special 'out' handling does not occur for method
				// implementation that accepts the out variables
				if (prevState.m_defType == UNDEF ||
				    // [case: 147955] allow assignment calls like: bool x = foo(..., out int y);
				    (prevState.m_defType == VAR && prevState.m_parseState == VAParseBase::VPS_ASSIGNMENT))
				{
					ReadToCls rtc(CS);
					const WTString stuff = rtc.ReadTo(CurPos() + 3, GetBufLen() - (GetCp() + 3), ",)=;\r\n");
					if (stuff.GetTokCount(' ') == 1)
					{
						// [case: 116073]
						// c#7 out declares local variable in scope m_deep-1
						State().m_parseState = BASE::VPS_ASSIGNMENT;
						State().m_defType = VAR;
						State().m_lastScopePos = CurPos();
					}
					else
					{
						// old use of out, doesn't declare a new variable
					}
				}
			}
		}
		return FALSE;
	}

	virtual void SpecialTemplateHandler(int endOfTemplateArgs)
	{
		// Support for generic types
		if (!InLocalScope())
		{
			if (IS_OBJECT_TYPE(State().m_defType))
			{
				// Generic class/type
				State().AddParserStateFlags(VPS_FLAG_GENERIC_TYPE);
				if (ShouldAddDef())
				{
					// Add CPP template logic ":< >" so members work.
					WTString methscope = MethScope();
					ReadToCls rtc(GetLangType());
					WTString def = rtc.ReadTo(CurPos() + 1, GetBufLen() - (GetCp() + 1), ">");
					EncodeTemplates(def);
					OnAddSymDef(methscope + ":< >", WTString('<') + def + '>', TEMPLATETYPE, V_HIDEFROMUSER);
				}
			}
			else
			{
				// Generic method?
				// Parse forward to see if this is a generic class "class gClass<T>(T) where T:new(){}"
				ReadToCls rtc(GetLangType());
				WTString def = rtc.ReadTo(CurPos() + 1, GetBufLen() - (GetCp() + 1), ">");
				if (rtc.CurPos()[0])
				{
					ReadToCls rtc2(GetLangType());
					WTString def2 = rtc.ReadTo(rtc.CurPos() + 1, rtc.GetBufLen() - (rtc.GetCp() + 1), "{");
					if (def2[0] == '(')
					{
						// generic classes, add types <T1, T2, ...>
						State().AddParserStateFlags(VPS_FLAG_GENERIC_TYPE);
					}
				}
			}
		}
	}
	virtual int GetLangType()
	{
		return CS;
	}
	virtual void ForIf_CreateScope(NewScopeKeyword keyword) override
	{
		if (keyword == NewScopeKeyword::FOR)
		{
			BASE::ForIf_DoCreateScope(); // can be used for "for" and "if", but in C#, we only use it for "for", but not
			                             // for "if"
		}
	}
	virtual BOOL ForIf_CloseScope(NewScopeKeyword keyword) override
	{
		if (keyword == NewScopeKeyword::FOR)
		{
			ASSERT_ONCE(m_deep > 1);
			BOOL retval = m_deep > 1;
			BASE::DecDeep();
			return retval;
		}

		return FALSE;
	}
};
