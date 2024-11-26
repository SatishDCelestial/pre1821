
int GetSafeReadLen(LPCTSTR readPos, LPCSTR maybeMasterBuf, int masterBufLen);

// Not much in here since the default parser supports C++ and C#
// Might break out c# stuff from the default VAParse to add here?
template <class VP> class LangWrapperUC : public VP
{
	WTString m_classname;

  public:
	using BASE = VP;
	using BASE::CurPos;
	using BASE::GetBaseScope;
	using BASE::GetBuf;
	using BASE::GetBufLen;
	using BASE::GetLangType;
	using BASE::IncDeep;
	using BASE::IsXref;
	using BASE::m_deep;
	using BASE::m_mp;
	using BASE::m_Scoping;
	using BASE::m_writeToFile;
	using BASE::Scope;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;
	using BASE::VPSF_UC_STATE;

	LangWrapperUC(int fType) : VP(fType)
	{
		g_doDBCase = FALSE;
		SetDollarValid(false);
	}

	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
	{
		DEFTIMER(UCOnAddSymDef);
		static const WTString classStr = EncodeScope("class<");
		if (strstr(def.c_str(), classStr.c_str()))
		{
			WTString ndef(def);
			ndef.ReplaceAll(EncodeScope("<"), " ");
			ndef.ReplaceAll(EncodeScope(">"), " ");
			VP::OnAddSymDef(symScope, ndef, type, attrs);
		}
		else
			VP::OnAddSymDef(symScope, def, type, attrs);
	}

	virtual void OnDef()
	{
		DEFTIMER(UCOnDef);
		LPCSTR sym = State().m_lastScopePos;
		if (!StartsWith(sym, "cpptext") && !StartsWith(sym, "structcpptext"))
			VP::OnDef();
	}

	virtual void OnCSym()
	{
		DEFTIMER(UCOnCSym);
		ASSERT_ONCE(!g_doDBCase);

		if (!State().m_inComment)
		{
			LPCSTR cw = CurPos();
			if (cw != State().m_lastWordPos && StartsWith(State().m_lastWordPos, "state"))
			{
				// http://udn.epicgames.com/Three/UnrealScriptStates.html
				State().m_defType = CLASS; // Get/Set properties
				OnDef();
				State().m_parseState = VPS_ASSIGNMENT; // ignore rest of line
				State().SetParserStateFlags(VPSF_UC_STATE);
			}
			else if (!m_deep && cw != State().m_lastWordPos && StartsWith(State().m_lastWordPos, "class"))
			{
				WTString classScope = DB_SEP_STR + GetCStr(cw);
				if (m_mp)
				{
					if (m_writeToFile || m_mp->GetParseType() == ParseType_GotoDefs || !m_mp->FindExact2(classScope))
					{
						ReadToCls rtc(GetLangType());
						const int readLen = ::GetSafeReadLen(State().m_lastWordPos, GetBuf(), GetBufLen());
						WTString line = rtc.ReadTo(State().m_lastWordPos, readLen, ";");
						OnAddSymDef(classScope, line.c_str(), CLASS, 0);

						WTString superDef(line);
						superDef.ReplaceAll(StrGetSym(classScope), "super");
						VP::OnAddSymDef(classScope + ":super", superDef, CLASS, 0);
					}
				}
				IncDeep();
				State().m_parseState = VPS_ASSIGNMENT; // ignore rest of line
				/*WTString scope =*/Scope();
			}
			else if (m_deep && StartsWith(cw, "Begin:"))
				State(m_deep - 1).m_defType = FUNC; // nolonger in state method definitions
			else if (StartsWith(cw, "event"))
				State().m_defType = EVENT; // Get/Set properties
			else if (StartsWith(cw, "extends"))
			{
				State().m_lastScopePos = State().m_lastWordPos;
				State().m_parseState = VPS_ASSIGNMENT; // ignore rest of line
			}
			else if (StartsWith(cw, "ignores"))
				State().m_parseState = VPS_ASSIGNMENT; // ignore rest of line
			else if (StartsWith(cw, "cpptext") || StartsWith(cw, "structcpptext"))
				// 				ExpandMacroCode("extern \"C\" ");
				State().m_defType = CLASS; // Get/Set properties
			else if (StartsWith(cw, "state"))
				State().m_defType = CLASS; // Get/Set properties
			else if (StartsWith(cw, "var"))
				State().m_defType = VAR; // Get/Set properties
			else
				VP::OnCSym();
		}
	}

	virtual void DoScope()
	{
		DEFTIMER(UCDoScope);
		if (m_mp && m_Scoping)
		{
			LPCSTR cw = CurPos();
			if (_tcsnicmp(cw, "class'", 6) == 0)
			{
				WTString baseClass = GetBaseScope();
				WTString cls = TokenGetField(cw + 6, "'");
				DType* dt = m_mp->FindSym(&cls, &baseClass, &baseClass);
				if (dt)
					State().m_lwData = std::make_shared<DType>(dt);
				else
					State().m_lwData.reset();
				return;
			}
			else if (StartsWith(cw, "Default") || StartsWith(cw, "Static"))
			{
				WTString baseClass = GetBaseScope();
				if (!IsXref())
				{
					// Default.[list members of this->]
					DType* dt = m_mp->FindExact(baseClass);
					if (dt)
						State().m_lwData = std::make_shared<DType>(dt); // just return "this" for now
					else
						State().m_lwData.reset();
				}
				else
				{
					// class'foo'.Default.[list members of foo]
					if (State().m_lwData)
						return; // return same class 'foo'
					token2 ln = TokenGetField(State().m_lastWordPos, ".");
					if (ln.contains("'"))
					{
						WTString cls = ln.read('\'');
						if (ln.more())
							cls = ln.read('\'');
						if (cls.GetLength())
						{
							DType* dt = m_mp->FindSym(&cls, &baseClass, &baseClass);
							if (dt)
								State().m_lwData = std::make_shared<DType>(dt);
						}
					}
					// else foo.Default.[leave m_lwData as foo, so we list members of foo]
				}
				return;
			}
			else if (StartsWith(cw, "Super"))
			{
				WTString baseClass = GetBaseScope();
				DType* dt = m_mp->FindExact(baseClass + ":super");
				if (dt)
					State().m_lwData = std::make_shared<DType>(dt);
				else
					State().m_lwData.reset();
				return;
			}
		}
		VP::DoScope();
	}

	virtual void ClearLineState(LPCSTR cPos)
	{
		VAParse::ClearLineState(cPos);
		State().m_privilegeAttr = 0;
	}

	virtual void OnError(LPCSTR errPos)
	{
		if (State().m_inComment) // Only underline spelling errors
		{
			DEFTIMER(UCOnError);
			VP::OnError(errPos);
		}
	}
};
