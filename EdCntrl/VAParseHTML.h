

// Parse code to decide which language it is VB or JS
class LangDetector : public VAParse
{
	int lang;

  public:
	LangDetector()
	    : VAParse(Src), lang(0)
	{
	}
	BOOL LooksLikeVB(LPCSTR code, ULONG bufLen)
	{
		DoParse(code, (int)bufLen);
		return lang != JS; // Default to VB if no discerning features.
	}
	virtual BOOL IsDone()
	{
		if (StartsWithNC(CurPos(), "</script"))
			return TRUE;
		return lang ? TRUE : __super::IsDone();
	}
	virtual void IncCP()
	{
		if (!State().m_inComment && strchr(";{}", CurChar()))
			lang = JS; // Must be JS if it contains {} or ;
		if (State().m_inComment == '\'' && strchr("\r\n", CurChar()))
			lang = VB; // Line comment, must be vb line comment?
		_ASSERTE(m_cp < mBufLen);
		__super::IncCP();
	}
};

template <class VP>
class LangWrapperHTML_Script : public LangWrapperJS<VP>
{
  protected:
	int mLang;
	int mDefaultLang;
	WTString mNamespaces;
	BOOL mInScript;
	WTString m_CodeNamespace; // Corresponding C# class scope

  public:
	using BASE = LangWrapperJS<VP>;
	using BASE::_DoParse;
	using BASE::ClearLineState;
	using BASE::CommentType;
	using BASE::CurChar;
	using BASE::CurPos;
	using BASE::DecDeep;
	using BASE::FileType;
	using BASE::GetBufLen;
	using BASE::GetCp;
	using BASE::GetFType;
	using BASE::GetLangType;
	using BASE::IncDeep;
	using BASE::IsDone;
	using BASE::IsXref;
	using BASE::m_cp;
	using BASE::m_mp;
	using BASE::mBufLen;
	using BASE::NextChar;
	using BASE::PrevChar;
	using BASE::State;

	LangWrapperHTML_Script(int fType)
	    : LangWrapperJS<VP>(fType)
	{
	}

	virtual void DoParse()
	{
		if (mInScript)
			ParseScript();        // Cashed pos in script, return to script mode
		GetSysDic(GetLangType()); // Set to current lang
		__super::DoParse();
	}
	int GetScriptLangFromCode(LPCSTR code, ULONG buflen)
	{
		LangDetector langDect;
		BOOL managedCode = (GetFType() == XAML || GetFType() == ASP);
		if (managedCode && code && code[-1] == '%')
			managedCode = FALSE; // <% non-managed code %>
		if (langDect.LooksLikeVB(code, buflen))
			return managedCode ? VB : VBS;
		return managedCode ? CS : JS;
	}

	//////////////////////////////////////////////////////////////////////////
	//  This parses scripts/asp with their native language parser
	void ParseScript(LPCSTR tag = NULL, BOOL scriptTag = FALSE)
	{
		if (tag)
		{
			// Get language from tag if provided
			WTString line = TokenGetField(tag, ">\r\n");
			if (line.FindNoCase("Language=") != -1 || line.FindNoCase("type=") != -1 || line.FindNoCase("VB") != -1)
			{
				if (line.FindNoCase("vbs") != -1)
					mLang = VBS;
				else if (line.FindNoCase("vb") != -1)
					mLang = VB;
				else if (line.FindNoCase("C#") != -1)
					mLang = CS;
				else if (line.FindNoCase("JS") != -1 || line.FindNoCase("Java") != -1 ||
				         line.FindNoCase("ecmascript") != -1)
					mLang = JS;
			}
			else if (mDefaultLang)
				mLang = mDefaultLang;
			else
				mLang = GetScriptLangFromCode(
				    CurPos(), ULONG(GetBufLen() - GetCp())); // analyze code to detect which parser to use
			int nsPos = line.FindNoCase("NameSpace=");
			if (nsPos != -1)
			{
				mNamespaces += TokenGetField(line.c_str() + nsPos + 10, "\"' \t\r\n");
			}
		}
		State().m_defType = TAG;
		IncDeep();
		mInScript = TRUE;
		const BOOL parseToQuote = FALSE;
		if (mLang == VB || mLang == VBS)
			ScriptParserVB tmp(this, parseToQuote, scriptTag);
		else if (mLang == JS)
			ScriptParserJS tmp(this, parseToQuote, scriptTag);
		else if (mLang == PHP)
			ScriptParserPHP tmp(this, parseToQuote, scriptTag);
		else
			ScriptParserCS tmp(this, parseToQuote, scriptTag);

		if (!IsDone())
		{
			mInScript = FALSE;
			GetSysDic(GetLangType()); // reset to HTML
		}
	}

	template <class Base>
	class ScriptParser : public Base
	{
		BOOL m_parseToQuote;
		BOOL mStartedInScriptTag;
		LangWrapperHTML_Script<VP>* m_htmlParser;

	  public:
		using BASE = Base;
		using BASE::_DoParse;
		using BASE::ClearLineState;
		using BASE::CommentType;
		using BASE::CurChar;
		using BASE::CurPos;
		using BASE::m_cp;
		using BASE::mBufLen;
		using BASE::NextChar;
		using BASE::State;

		ScriptParser(LangWrapperHTML_Script<VP>* htmlParser, BOOL parseToQuote, BOOL isScriptTag)
		    : Base(htmlParser->FileType())
		{
			// Make a copy of this instance
			VP* pThis = this;
			m_htmlParser = htmlParser;
			VP* pThat = htmlParser;
			*pThis = *pThat;
			m_parseToQuote = parseToQuote;
			mStartedInScriptTag = isScriptTag;

			ClearLineState(CurPos()); // Needed?
			_DoParse();

			// Load state from parsed instance
			*pThat = *pThis;
		}

		virtual BOOL IsDone()
		{
			if (m_parseToQuote && CurChar() == '"')
				return TRUE;

			if ((CurChar() == '%' || CurChar() == '?') && NextChar() == '>' &&
			    (CommentType() != '\'' && CommentType() != '"' && // Allow "?> and %>" in strings.
			     CommentType() != '\n'))                          // [case: 67330] Allow "?> and %>" in comments.
			{
				// [case: 66999] don't break on "%>" if we started in a script tag
				if (!mStartedInScriptTag)
				{
					// Break from asp %>
					return TRUE;
				}
			}

			if (CurChar() == '<' && !State().m_inComment)
			{
				// Use StartsWithNC because the closing tag can be in anyCase, independent of language/g_doDBCase
				if (NextChar() == '/' && StartsWithNC(CurPos() + 2, "script"))
				{
					m_cp--;      // so the < get picked up in LangWrapperHTML
					return TRUE; // Break from script </script>
				}
				if (StartsWithNC(CurPos(), "<!--"))
				{
					m_cp += 3; // skip the "<--" in script blocks
					_ASSERTE(m_cp < mBufLen);
				}
			}
			return Base::IsDone();
		}
		virtual int GetLangType()
		{
			return m_htmlParser->GetLangType(); // returns m_htmlParser->mLang; // JS/VB/VBS...
		}
		virtual WTString Scope(ULONG deep)
		{
			// Scripts in html/aspx/... need to go into class scope
			return m_htmlParser->m_CodeNamespace + __super::Scope(deep);
		}
#ifdef _DEBUG
		// for debug breakpoints
		virtual void OnDef()
		{
			__super::OnDef();
		}
		virtual void DoParse()
		{
			__super::DoParse();
		}
#endif // _DEBUG
	};
	typedef ScriptParser<LangWrapperCS<VP>> ScriptParserCS;
	typedef ScriptParser<LangWrapperJS<VP>> ScriptParserJS;
	typedef ScriptParser<LangWrapperVB<VP>> ScriptParserVB;
	typedef ScriptParser<LangWrapperPHP<VP>> ScriptParserPHP;
};

template <class VP>
class LangWrapperHTML : public LangWrapperHTML_Script<VP>
{
  protected:
	BOOL mAddTagDefs;
	BOOL mInTag;
	BOOL mIsMethodsInFile;
	int m_inCSym;
	DTypePtr tagData;
	DTypePtr propData;
	DTypePtr lwData;
	DTypePtr xmlProperty;
	LPCSTR m_TagTitle;
	LPCSTR m_AltTitle;
	LPCSTR m_propPos;
	int m_tagFlags;
	int m_propFlags;

  public:
	using BASE = LangWrapperHTML_Script<VP>;
	using BASE::ClearLineState;
	using BASE::CommentType;
	using BASE::CurChar;
	using BASE::CurPos;
	using BASE::DebugMessage;
	using BASE::DecDeep;
	using BASE::FileType;
	using BASE::GetFType;
	using BASE::IncDeep;
	using BASE::InComment;
	using BASE::IsDone;
	using BASE::IsXref;
	using BASE::m_buf;
	using BASE::m_CodeNamespace;
	using BASE::m_cp;
	using BASE::m_deep;
	using BASE::m_mp;
	using BASE::m_writeToFile;
	using BASE::mBufLen;
	using BASE::mDefaultLang;
	using BASE::mInScript;
	using BASE::mLang;
	using BASE::NextChar;
	using BASE::OnChar;
	using BASE::OnCSym;
	using BASE::OnNextLine;
	using BASE::OnSymbol;
	using BASE::PrevChar;
	using BASE::State;
	using BASE::VPS_NONE;

	LangWrapperHTML(int fType)
	    : LangWrapperHTML_Script<VP>(fType)
	{
		WTString cls = __super::GetParseClassName();
		mIsMethodsInFile = cls == "MethodsInFile";
		mAddTagDefs = (cls == "VAParseFileOutline" || cls == "MethodsInFile");
		mInScript = FALSE;
		mInTag = FALSE;
		mLang = JS;
		mDefaultLang = 0;
		g_doDBCase = TRUE;
		m_inCSym = NULL;
		tagData = NULL;
		m_TagTitle = m_AltTitle = NULL;
		m_propPos = NULL;
		m_tagFlags = m_propFlags = 0;
	}
	virtual void LoadParseState(VAParseBase* cp, bool assignBuf)
	{
		static LangWrapperHTML s_cachedInstanceHTML(Other);
		__super::LoadParseState(&s_cachedInstanceHTML, assignBuf);
		mLang = s_cachedInstanceHTML.mLang;
		mInScript = s_cachedInstanceHTML.mInScript;
		mAddTagDefs = s_cachedInstanceHTML.mAddTagDefs;
	}
	BOOL StartsWith(LPCSTR buf, LPCSTR begStr)
	{
		return StartsWithNC(buf, begStr);
	}
	virtual int GetSuggestMode()
	{
		const int suggestBitsFlag =
		    (Psettings && Psettings->m_autoSuggest && Psettings->m_defGuesses) ? SUGGEST_TEXT : SUGGEST_NOTHING;
		if (xmlProperty && StartsWith(PropPos(), "Value"))
			return SUGGEST_MEMBERS;
		if (!mInTag && !mInScript)
		{
			if (State().m_lastChar == '>')
				return suggestBitsFlag; // Suggest only just after the >, provieds helps with xml files
			return SUGGEST_NOTHING;
		}
		if (mInTag)
		{
			const int propFlags = m_propFlags;
			if (!propFlags)
			{
				WTString prop = GetCStr(PropPos());
				prop.to_lower();
				if (prop.EndsWith("src") || prop.EndsWith("href") || prop.EndsWith("url") ||
				    prop.EndsWith("source")) // Don't parse "Src/href/Text..."
					return suggestBitsFlag | SUGGEST_FILE_PATH;
				// Unknown tag, only provide text guesses if no intellisense is provided.
				if (suggestBitsFlag)
					return SUGGEST_TEXT_IF_NO_VS_SUGGESTIONS;
				else
					return SUGGEST_NOTHING;
			}
			if (propFlags & PF_SUGGEST_NOTHING)
				return SUGGEST_NOTHING; // do not suggest code
			int suggestMode = propFlags & 0xff;
			if (propFlags & PF_PARSE_AS_CODE)
			{
				int codeMode = suggestMode | __super::GetSuggestMode();
				return codeMode ? codeMode | suggestBitsFlag : 0; // <tag prop="scriptCode">
			}
			return suggestBitsFlag | suggestMode;
		}
		return __super::GetSuggestMode();
	}
	virtual int GetLangType()
	{
		if (mInScript)
			return mLang;
		return FileType();
	}
	WTString GetBCL()
	{
		WTString bcl;
		if ((IsXref() || PrevChar() == '.') && lwData)
			bcl += m_mp->GetBaseClassList(lwData->SymScope(), false, 0, GetLangType());
		else
		{
			if (xmlProperty && StartsWith(PropPos(), "Value"))
				return m_mp->GetBaseClassList(xmlProperty->SymScope(), false, 0, GetLangType());
			if (m_CodeNamespace.GetLength() > 1)
				bcl += m_CodeNamespace + "\f" + m_mp->GetBaseClassList(m_CodeNamespace, false, 0, GetLangType());
			if (propData)
				bcl += m_mp->GetBaseClassList(propData->SymScope(), false, 0, GetLangType());
			if (tagData && tagData != propData)
				bcl += m_mp->GetBaseClassList(tagData->SymScope(), false, 0, GetLangType());
		}
		if (mLang == JS && !IsXref())
		{
			int idx = bcl.Find(WILD_CARD_SCOPE);
			if (idx != -1)
				bcl = bcl.Mid(0, idx); // No wild card guessing w/o an xref; case=24766
		}
		return bcl;
	}
	virtual void DoScope()
	{
		if (InComment() || mInTag)
		{
			WTString sym = GetCStr(CurPos());
			State().m_lwData.reset();
			if (!sym.GetLength())
				return;
			WTString bcl = GetBCL();
			DType* data = m_mp->FindSym(&sym, &m_CodeNamespace, &bcl);
			if (data)
			{
				// Filter out Constructors
				// caused by bcl above containing the class we are looking in
				WTString symscope = data->SymScope();
				WTString parentscope = StrGetSymScope(symscope);
				if (strcmp(StrGetSym(symscope), StrGetSym(parentscope)) == 0)
					data = m_mp->FindExact2(parentscope);
			}
			if (!data && strncmp(State().m_begBlockPos, "<%@", 3) != 0) // Guesses in <%@ is too error prone. case=26668
				data = m_mp->FindAnySym(sym);
			// if(!IsDone()) // Don't call IsDone in comments, case:20935
			if (data)
				lwData = std::make_shared<DType>(data);
			else
				lwData.reset();
			State().m_lwData = lwData;
			if (lwData)
			{
				if (mInTag && (State().m_lastChar == '<' || State().m_lastChar == ':' ||
				               State().m_lastChar == '{')) // <tag or <asp:tag, or binding="{
					tagData = lwData;
				if (m_propFlags & PF_IS_PROPERTY)
					xmlProperty = lwData;
				if (!InComment())
					propData = lwData;
				OnSymbol();
			}
		}
		else if (mInTag || mInScript)
		{
			__super::DoScope();
			// Breaks intellisense on ActiveX objects, needs more thinking...
			// 			if(/*mLang == JS &&*/ State().m_lwData && State().m_lwData->val()&(V_DB_CPP|V_DB_SYS|V_SYSLIB))
			// 				State().m_lwData = NULL;
		}
	}

	void OnCloseTag()
	{
		State().m_defType = TAG;

		if (GetTagOpenPos(m_deep)[1] == '/')
		{
			if (mAddTagDefs)
			{
				g_doDBCase = FALSE;
				OnDef();
			}

			// dec to correct tag
			WTString tag = GetCStr(GetTagOpenPos(m_deep) + 2); // "</tag>
			ULONG matchDeep = m_deep;
			for (; matchDeep && !StartsWith(GetTagOpenPos(matchDeep - 1) + 1, tag.c_str()); matchDeep--)
				;
			if (matchDeep)
			{
				while (m_deep && !StartsWith(GetTagOpenPos(m_deep - 1) + 1, tag.c_str()))
				{
					// slight of hand, backup m_cp to previous start of tag
					int tmpCp = m_cp;
					m_cp = ptr_sub__int(GetTagOpenPos(m_deep), m_buf);
					_ASSERTE(m_cp < mBufLen);
					DecDeep(); // Recover from missing </close> tag
					m_cp = tmpCp;
				}
				DecDeep();
			}
		}
		else
		{
			if ((m_tagFlags & TF_NON_EMBEADABLE) && m_deep)
			{
				LPCSTR p1 = GetTagOpenPos(m_deep - 1);
				LPCSTR tagPos = TagPos();
				WTString str2 = TokenGetField(tagPos, "/ \t>");
				if (StartsWith(p1 + 1, str2.c_str()))
				{
					// Close prev <LI> before opening this one

					// slight of hand, pop state and temporarily backup m_cp
					// to previous start of tag
					State(m_deep - 1) = State();
					int tmpCp = m_cp;
					m_cp = ptr_sub__int(State().m_begLinePos, m_buf);
					_ASSERTE(m_cp < mBufLen);
					DecDeep();
					m_cp = tmpCp;
				}
			}

			if (mAddTagDefs)
			{
				g_doDBCase = FALSE;
				OnDef();
			}

			if (m_tagFlags & TF_IS_SCRIPT)
			{
				if (m_cp < mBufLen)
					m_cp++; // eat "<"
				BASE::ParseScript(GetTagOpenPos(m_deep) + 1, TRUE);
				if (InComment() && !IsDone())
					OnComment(NULL); // clear comment state at end of script, leave if we are still in script
			}
			else if (!m_cp || m_buf[m_cp - 1] != '/')
			{
				if (ISCSYM(GetTagOpenPos(m_deep)[1]))
				{
					if (!(m_tagFlags & TF_NO_CLOSING_TAG))
						IncDeep();
				}
			}
		}
		if (!mInTag || InComment())
			DebugMessage("OnCloseTag when not in tag.");
		mInTag = FALSE;
		// Clear propFlags and propPos Case:20710
		m_propFlags = NULL;
		m_tagFlags = NULL;
		m_propPos = CurPos();
	}
	virtual void OnDef()
	{
		if (!(m_tagFlags & TF_NO_ONDEF))
			__super::OnDef();
		m_tagFlags |= TF_NO_ONDEF; // Prevent second OnDEf for same tag
	}
	void OnOpenTag()
	{
		ClearLineState(CurPos());
		State().m_lastScopePos = CurPos() /*+1*/;
		State().m_lastWordPos = CurPos() + 1;
		State().m_begLinePos = CurPos();
		m_TagTitle = m_AltTitle = NULL;
		if (mInTag || InComment())
			DebugMessage("OnOpenTag within tag.");
		mInTag = TRUE;
		State().m_parseState = VPS_NONE;
		if (strncmp(CurPos(), "<%--", 4) == 0)
			ParseComment("--%>");
		else if (strncmp(CurPos(), "<!--", 4) == 0)
		{
			WTString tok = TokenGetField(CurPos() + 4, " \t\r\n");
			if (StrCmpI(tok.c_str(), "#include") !=
			    0) // Parse #include same as any other tag, display in outline and allow goto on included file
				ParseComment("-->");
		}
		else if (strncmp(CurPos(), "<?xml", 5) == 0)
			;
		else if (NextChar() == '?' || (NextChar() == '%' && (m_cp + 2) < mBufLen && m_buf[m_cp + 2] != '@')) // not <%@
		{
			// Parse <% asp code %>
			if (NextChar() == '?')
				mLang = PHP;
			OnDef();
			m_cp += 2; // eat "<%"
			if (CurChar() == '#' && m_cp < mBufLen)
			{
				if (m_cp < mBufLen)
					m_cp++; // eat # in "<%# aspx code %>"
			}
			ulong orgDeep = m_deep;
			BASE::ParseScript(CurPos());
			if (!mInScript && m_cp < mBufLen)
			{
				if (m_cp < mBufLen)
					m_cp++;              // eat "%>"
				while (m_deep > orgDeep) // recover from any <% { %> case=21511
					DecDeep();
				if (m_deep != orgDeep)
					DebugMessage("Brace Mismatch");
			}
			if (!IsDone())
				OnCloseTag();
		}
	}
	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (!c && InComment())
		{
			State().m_lastWordPos = CurPos();
			m_propFlags = NULL; // reset after <... ID=".." [don't use ID flags here]>
			m_propPos = NULL;   // reset after <... ID=".." [don't use ID flags here]>
		}
		if (mInTag)
			__super::OnComment(c, altOffset);
	}
	virtual void IncCP()
	{
		if (m_cp >= mBufLen)
			return;
		__super::IncCP();
		// Look for tag properties <tag property=xxx>
		// I don't think we need to use iscsym since all properties should be in english ascii?
		char c = CurChar();
		BOOL isCSym = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '-');
		if (isCSym && !m_inCSym)
		{
			if (mInTag && !InComment() && !mInScript)
			{
				if (State().m_lastChar == '<')
					OnTag();
				else if (State().m_lastChar != '/')
					OnProperty();
			}
			OnCSym();
			if (m_mp && InComment() && (m_propFlags & PF_PARSE_AS_CODE))
				DoScope();
		}
		m_inCSym = isCSym;
	}

	virtual void _DoParse()
	{
		DEFTIMERNOTE(VAP__DoParseTimer, NULL);
		// main parsing loop
		if (m_mp && !m_CodeNamespace.GetLength())
			m_CodeNamespace =
			    DB_SEP_STR +
			    EncodeScope(WTString(GetBaseNameNoExt(m_mp->GetFilename()))); // Use filename as scope of tagsId's
		m_inCSym = FALSE;
		for (char c; (c = CurChar()) != '\0'; IncCP())
		{
			OnChar();
			if (IsDone())
				break;

			if (State().m_inComment)
			{
				if (c == '\r' && NextChar() == '\n')
				{
					if (m_cp < mBufLen)
						m_cp++;
				}

				if (CurChar() == '\n' || CurChar() == '\r')
				{
					if (InComment())
					{
						// CR in string OK in strings
						if (StrCSpn(CurPos(), "<>") == '<')
						{
							DebugMessage("Unmatched quote.");
							// Recover from missmatch
							OnComment('\0');
							mInTag = FALSE;
						}
					}
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
							break; // not the end
						IncCP();   // eat the /
					}
					OnComment('\0');
				}

				if (IsDone())
					break; // Prevents IncCP() in loop from going past where it should. (Happens after <script>)
			}
			else
			{
				switch (c)
				{
				case '\'':
					if (mInTag) // Ignore quotes in the body.
						OnComment('\'');
					break;
				case '"':
					if (mInTag) // Ignore quotes in the body.
						OnComment(c);
					break;
				case '\r':
					// catch \r's only
					if (NextChar() == '\n')
					{
						if (m_cp < mBufLen)
							m_cp++;
					}
					// what about \r\r\n?
				case '\n':
					OnNextLine();
					break;
				case ' ':
				case '\t':
					if (mInTag)
						m_propFlags = NULL; // reset after <... ID=".." [don't use ID flags here]>
					break;
				case '<':
					// Look for <tag>...</tag>
					if (NextChar() != ' ') // < followed by a space is just text in an HTML file.
					{
						if (mInTag && !InComment())
							DebugMessage("Unexpected: '<' within tag.");
						OnOpenTag();
					}
					break;
				case '>':
					// Look for <tag>...</tag>
					if (mInTag) // HTML document can contain  > in the text, don't call OnCloseTag().
						OnCloseTag();
					break;
				}

				if (IsDone())
					return; // prevent "State().m_lastChar = c" below
				State().m_lastChar = c;
			}
		}

		if (!CurChar() && (InComment() || mInTag))
			DebugMessage("InComment() || mInTag at EOF."); // EOF, should not have these set.

		if (mInTag && InComment())
		{
			// Parse prop="code" as Code?
			if (m_propFlags & PF_PARSE_AS_CODE) // Prop="provide intellisense here"
			{
				LPCSTR commentPos = strchr(PropPos(), '"');
				if (commentPos)
				{
					m_cp = ptr_sub__int(commentPos, m_buf) + 1;
					_ASSERTE(m_cp < mBufLen);
					OnComment('\0');
					mInScript = TRUE;
					_DoParse();
				}
			}
		}
	}
	void ParseComment(LPCSTR parseToStr)
	{
		m_tagFlags |= TF_NO_ONDEF; // Do not show in outline
		m_inCSym = FALSE;
		size_t parseToStrLen = strlen(parseToStr);
		OnComment('-');
		const bool checkIsDone = !strcmp(parseToStr, "-->");
		for (char c; (c = CurChar()) != '\0'; IncCP())
		{
			if (checkIsDone && IsDone())
			{
				// [case: 74045]
				// we never know that we are in a comment if we always read to the end of --> comments
				break;
			}

			if (strncmp(CurPos(), parseToStr, parseToStrLen) == 0)
			{
				for (size_t i = 0; i < parseToStrLen - 1; i++)
					IncCP(); // eat -- in -->
				OnComment('\0');
				ASSERT_ONCE(mInTag); // Should be set to one in OnOpenTag()
				OnCloseTag();
				return;
			}
			switch (c)
			{
			case '\r':
				// catch \r's only
				if (NextChar() == '\n')
				{
					if (m_cp < mBufLen)
						m_cp++;
				}
				// what about \r\r\n?
			case '\n':
				OnNextLine();
				break;
			}
		}
	}
	void OnTag()
	{
		if (State().m_lastChar != '/' && State().m_lastWordPos[0] != '!')
			State().m_lastWordPos = CurPos();
		m_tagFlags = GetTagFlags();
	}
	void OnProperty()
	{
		m_propPos = CurPos();
		m_propFlags = GetPropFlags();
		int propFlags = m_propFlags;

		if (propFlags & PF_LANGUAGE)
		{
			WTString prop = GetCStr(CurPos());
			WTString val = TokenGetField(CurPos() + prop.GetLength() + 1, "\"' \t>");
			if (val.FindNoCase("vbs") != -1)
				mLang = VBS;
			else if (val.FindNoCase("vb") != -1)
				mLang = VB;
			else if (val.FindNoCase("C#") != -1)
				mLang = CS;
			else if (val.FindNoCase("JS") != -1 || val.FindNoCase("Java") != -1)
				mLang = JS;
			if (!(m_tagFlags & TF_IS_SCRIPT))
				mDefaultLang = mLang;
		}
		if (propFlags & PF_IS_INHERITS)
		{
			WTString prop = TokenGetField(CurPos(), "=");
			WTString ns = TokenGetField(CurPos() + prop.GetLength() + 1, "\"' \t>");
			ns.ReplaceAll(".", DB_SEP_STR);
			m_CodeNamespace = DB_SEP_STR + ns;
		}
		if (mInTag && m_writeToFile)
		{

			if (propFlags & PF_IS_TAG_NAME)
			{
				WTString tag = TokenGetField(GetTagOpenPos(m_deep), "< \t\r\n");
				WTString prop = GetCStr(CurPos());
				WTString val = TokenGetField(CurPos() + prop.GetLength() + 1, "\"' \t>");
				BASE::OnAddSymDef(m_CodeNamespace + DB_SEP_STR + val, tag + " " + prop + "=" + val, VAR,
				                  0); // Add tag to baseclass as VAR case=22921.
			}
		}
		if (propFlags & PF_IS_TAG_NAME)
		{
			if (m_deep)
				State(m_deep - 1).m_defType = C_ENUM; // Back to C_ENUM, so comments get added see case=22921.
			if (!m_TagTitle)
				m_TagTitle = CurPos();
		}
		if (!m_AltTitle && (propFlags & PF_IS_TAG_DESCRIPTION))
			m_AltTitle = CurPos();
	}

	virtual WTString GetBaseScope()
	{
		if (m_CodeNamespace.GetLength())
			return m_CodeNamespace;
		return __super::GetBaseScope();
	}
	virtual void OnError(LPCSTR errPos)
	{
#ifdef _DEBUG
		// underline all errors in debug builds
#else
		// in release builds, only underline spelling errors
		if (State().m_inComment)
#endif // _DEBUG
		{
			bool doError = true;
			if (StartsWith(State().m_lastScopePos, "href="))
				doError = false;

			if (doError)
				__super::OnError(errPos);
		}
	}
	virtual WTString MethScope()
	{
		int flags = GetTagFlags();
		if (flags & TF_HIDE_FROM_OUTLINE)
			return "";
		WTString tag = GetTagName(m_deep);
		WTString spacestr;
		if (mIsMethodsInFile)
			for (ULONG i = m_deep; i; i--)
				spacestr += "  ";
		if (mAddTagDefs)
			return EncodeScope(spacestr + tag) + GetTagDescription(m_deep) + GetTextAfterTag(m_deep);
		return tag;
	}

	LPCSTR GetTagOpenPos(ULONG deep)
	{
		// returns starting position of the "<tag"
		return State(deep).m_begLinePos;
	}
	LPCSTR TagPos()
	{
		return GetTagOpenPos(m_deep) + 1;
	}
	LPCSTR PropPos()
	{
		if (!m_propPos)
			return CurPos();
		return m_propPos;
	}

	WTString GetTextAfterTag(ULONG deep)
	{
		// returns <>short text<>
		// actually returns "// short text", so it will color as a comment
		LPCSTR tag = GetTagOpenPos(deep);
		if (tag)
		{
			LPCSTR endTag = strchr(tag, '>');
			if (endTag)
			{
				endTag++;
				LPCSTR nextTag = strchr(endTag, '<');
				if (nextTag && (nextTag - endTag) > 1)
				{
					WTString text = WTString(endTag, std::min(50, ptr_sub__int(nextTag, endTag)));
					text.Trim();
					if (text.GetLength())
						return WTString(" // ") + text;
				}
			}
		}
		return NULLSTR;
	}
	WTString GetTagDescription(ULONG deep)
	{
		// returns short description Outline and MethodsInFile
		// returns description (tag "description") from <tag name="description" src="path", ....>
		WTString description;
		if (m_TagTitle)
		{
			LPCSTR p = strchr(m_TagTitle, '=');
			if (p)
				description = WTString(" \"") + TokenGetField(p + 1, "\"'") + WTString('"');
		}
		if (m_AltTitle)
		{
			LPCSTR p = strchr(m_AltTitle, '=');
			if (p)
				description += WTString(" ") + GetCStr(m_AltTitle) + WTString("=\"") +
				               ::Basename(TokenGetField(p + 1, "\"'")) + WTString('"');
		}
		return description;
	}
	WTString GetTagName(ULONG deep)
	{
		WTString tag = TokenGetField(GetTagOpenPos(deep), "< \t/\r\n>"); // <%\nvar = 3;%> dont include "var"
		if (tag == "%@")
			tag += SPACESTR + TokenGetField(GetTagOpenPos(deep) + 4, "< \t/>"); // Include <%@ directive/command
		return tag;
	}

	virtual WTString GetNodeText(LPCSTR symPos, ULONG* outFlags)
	{
		if (outFlags)
		{
			ULONG flags = (ULONG)GetTagFlags();
			if (flags & TF_HIDE_FROM_OUTLINE)
				*outFlags |= GNT_HIDE;
			if (flags & TF_EXPAND_IN_OUTLINE)
				*outFlags |= GNT_EXPAND;
		}

		// Text to be displayed in outline and methods-in-file
		WTString tag = GetTagName(m_deep);
		if (mAddTagDefs)
			return tag + GetTagDescription(m_deep) + GetTextAfterTag(m_deep);
		return tag;
	}

	virtual WTString Scope(ULONG deep)
	{
		if (mInScript)
			return m_CodeNamespace + __super::Scope(deep);
		DEFTIMERNOTE(VAP_ScopeVB, NULL);
		WTString scope;
		for (ULONG i = 0; i < deep; i++)
			scope += DB_SEP_STR + GetTagName(i);
		if (mInTag)
			scope += DB_SEP_STR + GetTagName(deep);
		return scope;
	}
	virtual void GetScopeInfo(ScopeInfo* scopeInfo)
	{
		EdCntPtr curEd = g_currentEdCnt;
		::ScopeInfoPtr si = BASE::GetScopeInfoPtr();
		si->m_baseClassList = GetBCL();

		if (CommentType() == '\'' || CommentType() == '"')
		{
			scopeInfo->m_isDef = FALSE;
			scopeInfo->m_Scope = Scope(m_deep);
			scopeInfo->m_baseClassList = m_CodeNamespace + "\f";

			si->m_baseClass = m_CodeNamespace;
			si->m_xref = TRUE;
			si->m_lastScope = DB_SEP_STR;
			if (curEd)
			{
				curEd->mTagParserScopeType = "String";
				curEd->m_lastScope = DB_SEP_STR;
			}
		}
		else if (curEd)
		{
			if (InComment())
				curEd->mTagParserScopeType = "Comment";
			else if (mInTag)
				curEd->mTagParserScopeType = "tag";
			else
				curEd->mTagParserScopeType.Empty();
		}

		if (GetFType() == XML)
		{
			// Don't display system symbols in XML docs.
			if (si->IsCwSystemSymbol())
			{
				if (curEd)
					curEd->UpdateSym(nullptr);
				si->ClearCwData();
			}
		}

		if (mInScript && curEd)
		{
			if (m_cp > 1 && m_buf[m_cp - 2] == '<' && m_buf[m_cp - 1] == '%')
				curEd->m_ScopeLangType =
				    FileType(); // "<%" when caret just inside %, return filetype for snippets. case=25203
		}
	}

	virtual LPCSTR GetParseClassName()
	{
		return "LangWrapperHTML";
	}

	//////////////////////////////////////////////////////////////////////////
	// Define behavior for <Tags props="">
	// TODO: read these tags and flags from .../misc files html/aspx/xml/...
	enum
	{
		// Tag Flags
		TF_NO_CLOSING_TAG = 0x1,    // Tags that requires no matching </endTag>
		TF_HIDE_FROM_OUTLINE = 0x2, // Tags that should not appear in outline/MethodsInFIle
		TF_IS_SCRIPT = 0x4,         // <script> tags
		TF_EXPAND_IN_OUTLINE = 0x8,
		TF_NON_EMBEADABLE = 0x10, // <P>, <LI>, close previous before opening
		TF_NO_ONDEF = 0x20        // OnDef called in OnOpenTag()
	};
	enum
	{
		// Prop Flags
		PF_SUGGEST_NOTHING = 0x100, // No completion in text fields
		// PF_SUGGEST_FILE_PATH = 0x200,		// Displays FileCompletion
		// PF_SUGGEST_BITS = 0x400,			// Do bits even if bits is turned off
		PF_IS_TAG_NAME = 0x800,         // Default name of tag and calls OnDef for GoTo
		PF_IS_TAG_DESCRIPTION = 0x1000, // Alternate name to show in outline/MethodsInFIle
		PF_PARSE_AS_CODE = 0x2000,      // Do provide intellisense in prop="code"
		PF_LANGUAGE = 0x4000,           // Sets default lang for scripts
		PF_IS_INHERITS = 0x8000,        // Sets the baseclass for ASP/XAML classes
		PF_IS_PROPERTY = 0x10000        // Sets the baseclass for ASP/XAML property/value pair
	};
	int GetTagFlags()
	{
#ifdef _DEBUG
		static FILE*fp = fopen("c:/tmp/tags.txt", "wb");
		if (fp)
		{
			WTString cstr = TokenGetField(TagPos(), "/ \t>");
			//			if(!ISCSYM(cstr[0]))
			//			{
			//				int i = 123;
			//			}
			fprintf(fp, "%s\r\n", cstr.c_str());
		}
#endif // _DEBUG

		LPCSTR tagPtr = TagPos();
		if (tagPtr[0] == '/')
			return TF_HIDE_FROM_OUTLINE;

		WTString tagStr = TokenGetField(tagPtr, "/ \t>");
#define TAG_IS(tag) (0 == tagStr.CompareNoCase(tag))

		if (FileType() == XML)
		{
			if (m_deep == 0)
				return TF_EXPAND_IN_OUTLINE;
		}
		else
		{
			if (TAG_IS("GRID") || TAG_IS("BODY") || TAG_IS("TABLE") || TAG_IS("DIV") || TAG_IS("HTML"))
				return TF_EXPAND_IN_OUTLINE;
			if (TAG_IS("BR"))
				return TF_HIDE_FROM_OUTLINE | TF_NO_CLOSING_TAG;
			if (TAG_IS("P") || TAG_IS("LI") || TAG_IS("OPTION"))
				return TF_NON_EMBEADABLE;
			if (TAG_IS("IMG") || TAG_IS("INPUT") || TAG_IS("PARAM") || TAG_IS("META") /*|| TAG_IS("A")*/ ||
			    TAG_IS("LINK") || TAG_IS("%@"))
				return TF_NO_CLOSING_TAG;
			if (TAG_IS("script"))
				return TF_IS_SCRIPT | TF_EXPAND_IN_OUTLINE;
		}

		return 0;
	}
	int GetPropFlags()
	{
		const int suggestBitsFlag =
		    (Psettings && Psettings->m_autoSuggest && Psettings->m_defGuesses) ? SUGGEST_TEXT : SUGGEST_NOTHING;

#define PROP_IS(cwd) (StartsWith(prop, cwd))

		LPCSTR prop = PropPos();
		LPCSTR commentPos = strchr(PropPos(), '=');
		if (commentPos && strncmp(commentPos, "=\"{", 3) == 0) // xaml binding
		{
			int flags = PF_PARSE_AS_CODE | suggestBitsFlag;

			if (PROP_IS("x:Name") || PROP_IS("x:Key"))
			{
				// [case: 57813]
				flags |= PF_IS_TAG_NAME;
			}
			else if (PROP_IS("Command"))
			{
				// [case: 61814]
				flags |= PF_IS_TAG_DESCRIPTION;
			}

			return flags;
		}
		if (!ISCSYM(*prop))
			return 0;

		if (PROP_IS("src") || PROP_IS("href") || PROP_IS("source") || PROP_IS("file")) // Don't parse "Src/href/Text..."
			return 0 | SUGGEST_FILE_PATH | PF_IS_TAG_DESCRIPTION | suggestBitsFlag;

		if (PROP_IS("ImageURL") || PROP_IS("NavigateURL")) // ASPX
			return 0 | SUGGEST_FILE_PATH | PF_IS_TAG_DESCRIPTION | suggestBitsFlag;

		if (PROP_IS("Text") || PROP_IS("Content"))
			return PF_SUGGEST_NOTHING | PF_IS_TAG_DESCRIPTION;
		if (PROP_IS("runat") || PROP_IS("type") || PROP_IS("content"))
			return PF_SUGGEST_NOTHING;

		if (PROP_IS("Command"))
			return PF_IS_TAG_DESCRIPTION | suggestBitsFlag; // [case: 61814]

		if (PROP_IS("Key"))
		{
			if (StartsWith(GetTagOpenPos(m_deep), "<KeyBinding"))
				return 0; // [case: 61814] use Command for KeyBinding, not Key
			else
				return PF_IS_TAG_NAME | suggestBitsFlag; // Suggest other names/ID's in the file?
		}

		if (PROP_IS("ID") || PROP_IS("Name"))
			return PF_IS_TAG_NAME | suggestBitsFlag; // Suggest other names/ID's in the file?

		if (PROP_IS("Title") || PROP_IS("Summary") || PROP_IS("Description"))
			return PF_IS_TAG_DESCRIPTION;

		if (PROP_IS("width") || PROP_IS("height") || PROP_IS("style"))
			return suggestBitsFlag; // Guess styles from surrounding code

		if (PROP_IS("Inherits") || PROP_IS("x:Class") ||
		    (PROP_IS("Class") && StartsWith(GetTagOpenPos(m_deep), "<Window")))
			return PF_IS_INHERITS | PF_PARSE_AS_CODE;
		if (PROP_IS("Class"))
			return PF_IS_TAG_DESCRIPTION | suggestBitsFlag;

		if (PROP_IS("Language"))
			return PF_LANGUAGE | PF_IS_TAG_DESCRIPTION;

		if (PROP_IS("ClassName")) // .ascs <%@ Control Language="VB" ClassName="ForumUser" Strict="false" %>
			return PF_IS_INHERITS | PF_PARSE_AS_CODE | PF_IS_TAG_NAME;
		if (PROP_IS("TagName") ||
		    PROP_IS(
		        "TagPrefix")) // <%@ Register TagPrefix="Club" TagName="Locationpicker" Src="Locations_picker.ascx" %>
			return PF_PARSE_AS_CODE | PF_IS_TAG_NAME;

		// Xaml
		if (PROP_IS("Property"))
			return PF_IS_TAG_DESCRIPTION | suggestBitsFlag | PF_PARSE_AS_CODE | PF_IS_PROPERTY | SUGGEST_MEMBERS;
		if (PROP_IS("Value"))
			return PF_IS_TAG_DESCRIPTION;
		if (PROP_IS("TargetName"))
			return SUGGEST_MEMBERS;
		// ASP
		if (PROP_IS("DataSourceID") || PROP_IS("TargetControlID") || PROP_IS("ControlID") || PROP_IS("PopupButtonID") ||
		    PROP_IS("ContentPlaceHolderID") || PROP_IS("AssociatedControlID") || PROP_IS("AssociatedUpdatePanelID"))
			return SUGGEST_MEMBERS;
		if (PROP_IS("Namespace") || PROP_IS("Import"))
			return PF_IS_TAG_DESCRIPTION;
		if (PROP_IS("SkinID"))
		{
			// Only in skin file
			if (m_writeToFile && m_mp && StrStrIW(m_mp->GetFilename(), L".skin"))
				return PF_IS_TAG_NAME | suggestBitsFlag;
		}
		// Should we assume the prop contains code?
		return 0;
	}
	//////////////////////////////////////////////////////////////////////////
};
