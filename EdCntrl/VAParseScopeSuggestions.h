#pragma once
#include "VACompletionSet.h"
#include "DBQuery.h"
#include "rbuffer.h"
#include "StringUtils.h"
#include "WtException.h"
#include <memory>

#ifdef VA_TEMPLATE_HELP_HACK
// Helps Templates in VAParse??.h files
typedef VAParseMPScope VP;
#endif

// VAParseArgTracker is a simple VAParse wrapper to store scope info
// for VAParseScopeSuggestions.
// Contains duplicate m_MethArgs logic which could be pulled from MPGetScopeCls.
template <class VP> class VAParseArgTracker : public VP
{
  protected:
#define MAX_ARGS 20
	LPCSTR m_MethArgs[MAX_ARGS + 1];
	DTypePtr m_lSymScopeData[STATE_COUNT + 1];

  public:
	using BASE = VP;
	using BASE::CommentType;
	using BASE::CurChar;
	using BASE::CurPos;
	using BASE::Depth;
	using BASE::InComment;
	using BASE::IsXref;
	using BASE::m_cp;
	using BASE::m_deep;
	using BASE::m_inIFDEFComment;
	using BASE::m_inMacro;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;
	using BASE::VPS_NONE;

	VAParseArgTracker(int fType) : VP(fType)
	{
		memset(&m_MethArgs, 0, sizeof(m_MethArgs));
	}
	LPCSTR* GetArgs()
	{
		return m_MethArgs;
	}
	WTString GetArgStr()
	{
		WTString def;
		LPCSTR* args = GetArgs();
		for (int i = 0; args[i]; i++)
		{
			if (i)
				def += ", ";
			def += TokenGetField(args[i], ")");
		}
		return def;
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		// [case: 2050]
		if (CurChar() == '}' && (!m_deep || !BASE::InLocalScope(m_deep - 1)))
			m_MethArgs[0] = NULL;
		__super::DecDeep();
	}
	virtual void OnDef()
	{
		if (!m_inMacro)
		{
			if (BASE::InParen(m_deep) && State(m_deep - 1).m_defType == FUNC && State().m_argCount < MAX_ARGS)
			{
				m_MethArgs[State().m_argCount] = State().m_lastScopePos;
				m_MethArgs[State().m_argCount + 1] = NULL;
			}
			else if (CurChar() == ';' && !BASE::InParen(m_deep) && State(m_deep).m_defType == FUNC)
			{
				// [case: 23124] clear args after func declaration
				m_MethArgs[0] = NULL;
			}
		}
		__super::OnDef();
	}
	virtual int OnSymbol()
	{
		__super::OnSymbol();
		if (State().m_parseState != VPS_ASSIGNMENT || !BASE::IsDef(m_deep))
		{
			m_lSymScopeData[m_deep] = State().m_lwData;
			return 1;
		}
		return 0;
	}
	virtual void OnUndefSymbol()
	{
		__super::OnUndefSymbol();

		// [case: 40179] condition added to prevent bustage of
		// suggestions when typing "string str = "
		if (IsXref())
		{
			// [case: 70991] clear lastSymScopeData for unknown xrefs
			m_lSymScopeData[m_deep].reset();
		}
	}
	DTypePtr GetLastSymScopeData()
	{
		return GetLastSymScopeData(m_deep);
	}
	DTypePtr GetLastSymScopeData(ulong deep)
	{
		if (deep < 0 || deep >= MAX_ARGS)
			return NULL;
		return m_lSymScopeData[deep];
	}
};

template <class VP> class VAParseScopeSuggestions : public VAParseArgTracker<VP>
{
	VAParse* m_vp;
	WTString mSuggestions;
	WTString m_startsWith;
	WTString mCachedScope, mCachedBaseScope;
	int m_HistoryRank;
	DWORD mScopeInfoStartTicks;
	bool mKeepLooking;
	ScopeSuggestionMode mStartSuggestMode;

  public:
	using BASE = VAParseArgTracker<VP>;
	using BASE::CommentType;
	using BASE::CurLine;
	using BASE::CurPos;
	using BASE::Depth;
	using BASE::FileType;
	using BASE::GetBaseScope;
	using BASE::GetLangType;
	using BASE::GetLastSymScopeData;
	using BASE::InComment;
	using BASE::m_buf;
	using BASE::m_cp;
	using BASE::m_deep;
	using BASE::m_mp;
	using BASE::mBufLen;
	using BASE::Scope;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;
	using BASE::VPS_NONE;
	using BASE::VPSF_NAKEDSCOPE_FOR_IF;

	VAParseScopeSuggestions(int fType)
	    : VAParseArgTracker<VP>(fType), m_HistoryRank(0), mScopeInfoStartTicks(0), mKeepLooking(true),
	      mStartSuggestMode(smNone)
	{
	}

	virtual void GetScopeInfo(ScopeInfo* scopeInfo)
	{
		mScopeInfoStartTicks = ::GetTickCount();
		VP::GetScopeInfo(scopeInfo);
		if (scopeInfo->mScopeSuggestionMode != smNone)
			scopeInfo->mScopeSuggestionMode = smNone;

		EdCntPtr ed(g_currentEdCnt);
		// [case: 111552] changed condition to determine mode even if !m_typing
		if (ed && Psettings->m_autoSuggest)
		{
			mSuggestions = NULLSTR;
			const bool isPossibleHashtag = (CommentType() == '*' || CommentType() == '\n') && CurPos()[-1] == '#';
			if (Psettings->mScopeSuggestions && (!InComment() || isPossibleHashtag) &&
			    !Psettings->mSuppressAllListboxes && (!Psettings->mRestrictVaListboxesToC || IsCFile(gTypingDevLang)))
			{
				m_startsWith = GetCStr(CurPos());
				DetermineSuggestMode(scopeInfo);
				if (smNone != mStartSuggestMode)
				{
					scopeInfo->mScopeSuggestionMode = mStartSuggestMode;
					mCachedScope.Empty();
					mCachedBaseScope.Empty();

					if (ed->m_typing)
					{
						// Only needed when typing? (condition introduced in change 13603, no case)
						GetSuggestionsInThread();
					}
				}

				const DWORD duration = ::GetTickCount() - mScopeInfoStartTicks;
				if (duration > 200)
				{
					WTString msg;
					msg.WTFormat("TIME: VAParseScopeSuggestions::GetScopeInfo threshold %lu ticks\n", duration);
					Log(msg.c_str());
#if !defined(NDEBUG)
					::OutputDebugString(msg.c_str());
#endif
				}
			}

			scopeInfo->m_ScopeSuggestions = mSuggestions;
		}
	}

  private:
	// this is the scope suggestion thread
	static void SuggestionThreadFunction(LPVOID paramsIn)
	{
		VAParseScopeSuggestions<VP>* _this = (VAParseScopeSuggestions<VP>*)paramsIn;
		_this->GetSuggestions();
	}

	// this runs on the UI thread and waits on the scope suggestion thread
	void GetSuggestionsInThread()
	{
		_ASSERTE(mKeepLooking == true);
		FunctionThread* thrd = new FunctionThread(SuggestionThreadFunction, this, CString("ScopeSuggest"), true, false);
		thrd->AddRef();
		thrd->StartThread();

		int eventCnt = 0;
		CPoint p1;
		::GetCursorPos(&p1);
		HWND h = g_currentEdCnt->GetSafeHwnd();
		while (thrd->Wait(10, false) == WAIT_TIMEOUT)
		{
			if (!mKeepLooking)
			{
				vLog("WARN: GST - waiting for thread exit");
				continue;
			}

			CPoint p2;
			::GetCursorPos(&p2);
#ifdef _DEBUG
			// Allows debugging suggestion issues, without it bailing out
			static BOOL sIgnoreEvents = FALSE;
			if (sIgnoreEvents)
				;
			else
#endif // _DEBUG
			    if (p1 != p2 || EdPeekMessage(h))
				eventCnt++;
			if (eventCnt > 2)
			{
				mKeepLooking = false;
				vLog("WARN: GST event threshold hit - quit");
				// don't break - wait for clean exit due to VParse lifetime
			}
			else if (eventCnt && !thrd->HasStarted())
			{
				mKeepLooking = false;
				vLog("WARN: GST hadEvent, thread not started");
				// don't break - wait for clean exit due to VParse lifetime
			}

			// [case: 61442]
			const DWORD duration = ::GetTickCount() - mScopeInfoStartTicks;
			if (duration > 1000)
			{
#ifdef _DEBUG
				if (sIgnoreEvents)
					continue;
#endif // _DEBUG
				mKeepLooking = false;
				vLog("WARN: GST time threshold hit - quit");
				// don't break - wait for clean exit due to VParse lifetime
			}
		}

		thrd->Release();
	}

	// this happens on the UI thread
	void DetermineSuggestMode(ScopeInfo* scopeInfo)
	{
		if (/*PrevChar() == ' ' &&*/ strchr("=<>", State().m_lastChar))
		{
			mStartSuggestMode = smAssignment;
			if (State().m_lastChar == '=')
				CheckForUe4SuggestMode(scopeInfo, true);
		}
		else if ((CommentType() == '*' || CommentType() == '\n') && CurPos()[-1] == '#')
		{
			// hashtags can't be preceded by chars or ##
			if (m_cp > 1)
			{
				char tmp = CurPos()[-2];
				// #invalidHashtagPrefix   [case: 108807]
				if (!ISCSYM(tmp) && '#' != tmp && '?' != tmp && '"' != tmp && '\\' != tmp)
					mStartSuggestMode = smHashtag;
			}
		}
		else if (StartsWith(State().m_lastWordPos, "new") || StartsWith(State().m_lastWordPos, "gcnew"))
			mStartSuggestMode = smNew;
		else if (BASE::InParen(Depth()))
		{
			mStartSuggestMode = smParam;
			CheckForUe4SuggestMode(scopeInfo, false);
		}
		else if (State().m_parseState == VPS_NONE && Depth() &&
		         ((IsCFile(gTypingDevLang) && (*CurPos() == '~' || *CurPos() == '!')) || ISCSYM(*CurPos())))
			mStartSuggestMode = smScope;
		else if (StartsWith(State().m_lastWordPos, "return") || StartsWith(State().m_lastWordPos, "co_return") ||
		         StartsWith(State().m_lastWordPos, "co_yield"))
			mStartSuggestMode = smReturn;
		else if (gTypingDevLang == CS && StartsWith(State().m_lastWordPos, "as"))
			mStartSuggestMode = smType;
		else if (m_deep && StartsWith(State(Depth() - 1).m_begLinePos, "switch") &&
		         StartsWith(State().m_lastWordPos, "case"))
			mStartSuggestMode = smSwitchCase;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		else if (m_deep && State(Depth() - 1).m_defType == PROPERTY &&
		         StartsWith(State(Depth() - 1).m_begLinePos, "__property"))
		{
			// [case: 133417]
			const char ch = State().m_lastChar;
			if ('{' == ch || ',' == ch || ';' == ch || '}' == ch)
				mStartSuggestMode = smPropertyAttribute;
			else
				mStartSuggestMode = smNone;
		}
#endif
		else if (State().m_parseState == VPS_ASSIGNMENT && Depth())
		{
			// [case: 40467]
			// if / else without braces
			if (State().m_lastChar == ')' && m_deep && StartsWith(State(m_deep - 1).m_lastWordPos, "if") &&
			    State().HasParserStateFlags(VPSF_NAKEDSCOPE_FOR_IF))
			{
				// check lastChar so that suggestion is not made before parens typed
				mStartSuggestMode = smScope;
			}
			else if (StartsWith(State().m_lastWordPos, "else"))
			{
				mStartSuggestMode = smScope;
			}
			else
				mStartSuggestMode = smNone;
		}
		else
			mStartSuggestMode = smNone;
	}

	void CheckForUe4SuggestMode(ScopeInfo* scopeInfo, bool isAssign)
	{
		if (!m_deep)
			return;
		if (!(IsCFile(gTypingDevLang)))
			return;
		if (!Psettings->mUnrealEngineCppSupport)
		{
			// [case: 114477]
			return;
		}

		int symTypeOfAttribute = UNDEF;

		if (State(m_deep - 1).m_begLinePos && State(m_deep - 1).m_begLinePos[0] == 'U' &&
		    (State(m_deep - 1).m_defType == DEFINE || State(m_deep - 1).m_defType == FUNC ||
		     State(m_deep - 1).m_defType == UNDEF))
		{
			symTypeOfAttribute = ::GetTypeOfUnrealAttribute(State(m_deep - 1).m_begLinePos);

			if (UNDEF == symTypeOfAttribute && StartsWith(State(m_deep - 1).m_begLinePos, "UPARAM", TRUE))
			{
				mStartSuggestMode = smUe4attrNoSuggestion;

				if (!isAssign)
				{
					// [case: 109571] [case: 111404]
					EdCntPtr ed(g_currentEdCnt);
					if (!ed)
						return;

					WTString wd(ed->WordLeftOfCursor());
					if (wd == " ")
						wd = ed->CurWord(-1);

					if (!wd.CompareNoCase("meta") || !wd.CompareNoCase("displayname"))
						return;

					mStartSuggestMode = smUe4attrParam;
				}

				return;
			}
		}
		else if (m_deep > 1 && State(m_deep - 2).m_begLinePos && State(m_deep - 2).m_begLinePos[0] == 'U' &&
		         (State(m_deep - 2).m_defType == DEFINE || State(m_deep - 2).m_defType == FUNC ||
		          State(m_deep - 2).m_defType == UNDEF))
		{
			if (StartsWithNC(State(m_deep - 1).m_begLinePos, "meta", true))
			{
				symTypeOfAttribute = ::GetTypeOfUnrealAttribute(State(m_deep - 2).m_begLinePos);

				switch (symTypeOfAttribute)
				{
				case C_INTERFACE:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrInterfaceMeta;
					break;
				case CLASS:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrClassMeta;
					break;
				case STRUCT:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrStructMeta;
					break;
				case FUNC:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrFunctionMeta;
					break;
				case DELEGATE:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrDelegateMeta;
					break;
				case C_ENUM:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrEnumMeta;
					break;
				case PROPERTY:
					mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrPropertyMeta;
					break;
				case UNDEF:
					if (StartsWith(State(m_deep - 2).m_begLinePos, "UPARAM", TRUE))
						mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrParamMeta;
					else
						mStartSuggestMode = smUe4attrNoSuggestion;
					break;
				default:
					_ASSERTE(!"unhandled / invalid symTypeOfAttribute");
					mStartSuggestMode = smUe4attrNoSuggestion;
				}
			}

			return;
		}
		else if (m_deep > 1 && State(m_deep - 1).m_lastWordPos && State(m_deep - 1).m_lastWordPos[0] == 'U' &&
		         (State(m_deep - 1).m_defType == C_ENUMITEM || State(m_deep - 1).m_defType == FUNC))
		{
			if (StartsWithNC(State(m_deep - 1).m_lastWordPos, "UMETA", true))
				mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrMeta;

			return;
		}

		if (UNDEF == symTypeOfAttribute)
			return;

		switch (symTypeOfAttribute)
		{
		case CLASS:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrClass;
			break;
		case STRUCT:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrStruct;
			break;
		case C_INTERFACE:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrInterface;
			break;
		case FUNC:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrFunction;
			break;
		case DELEGATE:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrDelegate;
			break;
		case C_ENUM:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrEnum;
			break;
		case PROPERTY:
			mStartSuggestMode = isAssign ? smUe4attrNoSuggestion : smUe4attrProperty;
			break;
		}

		if (smUe4attrNoSuggestion != mStartSuggestMode)
		{
			EdCntPtr ed(g_currentEdCnt);
			if (!ed)
				return;

			WTString wd(ed->WordLeftOfCursor());
			if (wd == " ")
				wd = ed->CurWord(-1);

			if (!wd.CompareNoCase("meta") || !wd.CompareNoCase("displayname"))
			{
				// these two specifiers are followed by '='
				// don't make further suggestions when space is typed following the word
				mStartSuggestMode = smUe4attrNoSuggestion;
				return;
			}
		}
	}

	// this happens in the suggestion thread
	void GetSuggestions()
	{
		try
		{
			switch (mStartSuggestMode)
			{
			case smAssignment:
				SuggestAssignment();
				break;
			case smNew:
				SuggestNew();
				break;
			case smParam:
				SuggestParam();
				break;
			case smScope:
				SuggestByScope();
				break;
			case smReturn:
				SuggestReturn();
				break;
			case smType: {
				// case=28314 Suggest correct type by default in C# listbox after keyword "as" for assignment
				WTString type = GetAssignType();
				if (type.GetLength() && !StartsWith(type, "void"))
					AddSuggestion(type);
			}
			break;
			case smSwitchCase:
				SuggestSwitchCaseArgument();
				break;
			case smHashtag:
				SuggestHashtags();
				break;
			case smPropertyAttribute:
				SuggestPropertyAttribute();
				break;
			case smUe4attrClass:
				SuggestUe4Attribute("UCLASS", "UC");
				break;
			case smUe4attrStruct:
				SuggestUe4Attribute("USTRUCT", "US");
				break;
			case smUe4attrInterface:
				SuggestUe4Attribute("UINTERFACE", "UI");
				break;
			case smUe4attrFunction:
				SuggestUe4Attribute("UFUNCTION", "UF");
				break;
			case smUe4attrDelegate:
				// UDELEGATE uses specifiers defined in the UF namespace
				SuggestUe4Attribute("UDELEGATE", "UF");
				break;
			case smUe4attrEnum:
				SuggestUe4Attribute("UENUM", "Z0_UE4_UENUM_SPECIFIERS");
				break;
			case smUe4attrProperty:
				SuggestUe4Attribute("UPROPERTY", "UP");
				break;
			case smUe4attrParam:
				SuggestUe4Attribute("UPARAM", "Z0_UE4_UPARAM_SPECIFIERS");
				break;
			case smUe4attrMeta:
				SuggestUe4Attribute("UMETA", "Z0_UE4_UMETA_SPECIFIERS");
				break;
			case smUe4attrClassMeta:
				SuggestUe4Attribute("UCLASS_META", "UM_UC");
				break;
			case smUe4attrStructMeta:
				SuggestUe4Attribute("USTRUCT_META", "UM_US");
				break;
			case smUe4attrInterfaceMeta:
				SuggestUe4Attribute("UINTERFACE_META", "UM_UI");
				break;
			case smUe4attrFunctionMeta:
				SuggestUe4Attribute("UFUNCTION_META", "UM_UF");
				break;
			case smUe4attrDelegateMeta:
				SuggestUe4Attribute("UDELEGATE_META", "UM_UD");
				break;
			case smUe4attrEnumMeta:
				SuggestUe4Attribute("UENUM_META", "UM_UE");
				break;
			case smUe4attrPropertyMeta:
				SuggestUe4Attribute("UPROPERTY_META", "UM_UP");
				break;
			case smUe4attrParamMeta:
				SuggestUe4Attribute("UPARAM_META", "UM_UR");
				break;
			case smUe4attrNoSuggestion:
				// prevent suggestion from appearing after '='
				break;
			default:
				_ASSERTE(!"unhandled / invalid mStartSuggestMode");
			}
		}
		catch (const WtException&)
		{
			const char* logMsg = "TIME: GetScopeInfo threshold timeout\n";
			Log(logMsg);
#if !defined(NDEBUG)
			::OutputDebugString(logMsg);
#endif
		}
	}

	WTString GetCachedScope()
	{
		if (mCachedScope.IsEmpty())
			mCachedScope = Scope();
		return mCachedScope;
	}

	WTString GetCachedBaseScope()
	{
		if (mCachedBaseScope.IsEmpty())
			mCachedBaseScope = GetBaseScope();
		return mCachedBaseScope;
	}

	void SuggestParam()
	{
		// foo( [prompt for argType]
		auto si = BASE::GetScopeInfoPtr();
		if (si->m_argTemplate.GetLength())
		{
			token2 t = si->m_argTemplate;
			// [case: 61442] limit number of iterations
			for (int cnt = 0; cnt < 6 && t.more() > 2; ++cnt)
			{
				WTString tipInfo = t.read('\f');
				if (tipInfo.GetLength())
				{
					ReadToCls_MLC ptc(GetLangType());
					ptc->ReadTo(tipInfo, "(");
					WTString def;
					for (int i = 0; ptc->CurChar() && ptc->GetCp() < ptc->GetBufLen() && i <= si->m_argCount; i++)
						def = ptc->ReadTo(ptc->CurPos() + 1, ptc->GetBufLen() - ptc->GetCp() - 1, ",)");
					if (def.GetLength())
					{
						WTString type = GetAssignType(def.c_str());
						WTString varName = GetCStr(ptc->State().m_lastScopePos);
						SuggestLocalVars(type, varName, NULLSTR);
					}
				}
			}
		}
		else if (m_deep && GetLastSymScopeData(m_deep - 1).get())
		{
			// Typecast?
			// s = ([suggest typecast])
			// UGLY check to qualify that it really is "= ("
			LPCSTR eqPos = State().m_begLinePos - 3;
			if (eqPos >= m_buf && strncmp(eqPos, "= (", 3) == 0)
			{
				WTString type = GetAssignType(GetLastSymScopeData(m_deep - 1)->Def().c_str());
				if (type.GetLength())
					AddSuggestion(type);
			}
		}

		if (m_deep > 1 && State(m_deep - 1).m_lastChar == '(' && State(m_deep - 2).m_lastChar == '(' &&
		    StartsWith(State(m_deep - 2).m_lastWordPos, "__attribute__"))
		{
			// [case: 79523]
			SuggestTypesOf(WTString("__attribute__"));
		}
	}

	void SuggestByScope()
	{
		if (StartsWith(State(Depth() - 1).m_begLinePos, "switch"))
		{
			// switch(){...
			SuggestTypesOf(WTString("switch"));
			return;
		}
		if (IS_OBJECT_TYPE(State(Depth() - 1).m_defType))
		{
			// class/interface/struct {...
			SuggestTypesOf(WTString("class"));
			if (IsCFile(FileType()))
			{
				// Add constructor/~destructor() for C/++ [case=47139]
				WTString className = StrGetSym(GetCachedBaseScope());
				if (className.GetLength())
				{
					AddSuggestion(className);
					AddSuggestion(WTString("~") + className + WTString("()"));
					if (GlobalProject->CppUsesClr())
						AddSuggestion(WTString("!") + className + WTString("()"));
				}
			}
			// Suggest Methods to override
			//			if(IsCFile(gTypingDevLang))
			//				SuggestMethodsToOverride();
		}
		WTString scope = GetCachedScope();
		if (gTypingDevLang == VB)
			scope.MakeLower();
		if (scope.contains(":for-") || scope.contains(":while-") || scope.contains(":switch-") ||
		    scope.contains(":do-"))
		{
			// for/while(){...
			SuggestTypesOf(WTString("loop"));
			return;
		}
		else if (CS == gTypingDevLang && scope.contains(":foreach-"))
		{
			// [case: 105013]
			SuggestTypesOf(WTString("loop"));
			return;
		}
	}
	WTString GetAssignSymScope()
	{
		if (GetLastSymScopeData().get())
			return GetLastSymScopeData()->SymScope();
		return GetCachedScope() + GetCStr(State().m_lastScopePos); // BOOL b =
	}
	WTString GetAssignDef()
	{
		WTString def;
		DTypePtr dt = GetLastSymScopeData();
		if (BASE::IsDef(Depth()) || !dt)
		{
			// BOOL b =
			def = TokenGetField(State().m_begLinePos, "\r\n");
			if (!dt)
			{
				int pos = def.Find('=');
				if (-1 != pos)
				{
					const WTString tmp(def.Left(pos));
					if (-1 != tmp.FindOneOf(".:"))
					{
						// v1.UnknownMember =

						// [case: 70991]
						// if is xref of unknown type (!dt), then don't try suggestion
						// since ours will break ones from VS
						def.Empty();
					}
				}
			}

			if (Is_C_CS_File(gTypingDevLang))
			{
				if (-1 != def.Find('<'))
				{
					// [case: 91733]
					// normalize spacing because we are working here with raw
					// text (TokenGetField) rather than parsed defs
					// (should consider doing this directly in TokenGetField)
					def = ::StripCommentsAndStrings(gTypingDevLang, def, false);
					if (-1 != def.Find('<'))
					{
						::EncodeTemplates(def);
						def = ::DecodeTemplates(def);
					}
				}
			}
		}
		else
		{
			// just "b = "
			def = dt->Def();
		}
		return def;
	}
	void SuggestAssignment()
	{
		// var = [suggest guesses]
		WTString type = GetAssignType();
		WTString symScope = GetAssignSymScope();
#ifdef _DEBUG
		if (symScope.GetLength() == 2 && symScope[0] == ':')
		{
			if (strchr("&|^~!", symScope[1]))
			{
				// consider just returning here?
				// see macro DEFINE_ENUM_FLAG_OPERATORS
				// return;
			}
		}
#endif                                                         // _DEBUG
		SuggestLocalVars(type, StrGetSym(symScope), symScope); // don't suggest this one "foo = [foo]"
	}
	void SuggestNew()
	{
		WTString type;
		// Type *pT = new [prompt Type]
		if (StartsWith(State().m_begLinePos, "return"))
			type = GetReturnType(); // return new [suggest return type];
		else
			type = GetAssignType();

		if (type.IsEmpty() || type == "var") // [case: 41898]
			return;

		if (IsCFile(gTypingDevLang))
		{
			if (type == "auto")
				return;

			// suggest new Foo, not new Foo * or new Foo ^
			if (type[type.GetLength() - 1] == '*')
			{
				type = type.Left(type.GetLength() - 1);
				type.TrimRight();
			}
			else if (type[type.GetLength() - 1] == '^')
			{
				type = type.Left(type.GetLength() - 1);
				type.TrimRight();
			}

			if (type == "void")
				return;
		}

		AddSuggestion(type);
	}
	void SuggestReturn()
	{
		// this doesn't work for functions that return pointers (see case=31065)
		// return [prompt MethodType]
		WTString type = GetReturnType();
		if (type.GetLength())
			SuggestLocalVars(type, NULLSTR, NULLSTR);
		SuggestNewOrNullOf(type);
	}

	int SuggestEnumMemberItems()
	{
		// case=4622 suggest enum items when using an instance of the enum (switch statement, == operator)
		WTString types = GetAssignType();
		if (types.IsEmpty())
			return 0;
		return SuggestEnumMemberItems(types, true);
	}

	int SuggestEnumMemberItems(const WTString& enumType, bool blindTypeGuess)
	{
		CheckStatus();
		int membersAdded = 0;
		const WTString curScope(GetCachedScope());

		WTString searchFor(enumType);
		if (searchFor[0] != DB_SEP_CHR)
			searchFor = DB_SEP_STR + searchFor;
		DType* dt = m_mp->FindExact(searchFor);
		if (dt && dt->type() == C_ENUM)
		{
			WTString searchFor2(dt->Def());
			int pos = searchFor2.Find(_T("typedef enum"));
			if (-1 != pos)
			{
				// [case: 70315]
				searchFor2 = searchFor2.Mid(pos + _tcslen(_T("typedef enum")));
				pos = searchFor2.Find(enumType[0] == DB_SEP_CHR ? enumType.Mid(1) : enumType);
				if (-1 != pos)
					searchFor2 = searchFor2.Left(pos);
				else if (-1 == searchFor2.Find(_T("unnamed_enum_")))
					searchFor2.Empty();

				if (!searchFor2.IsEmpty())
				{
					searchFor2.Trim();
					if (searchFor2[0] != DB_SEP_CHR)
						searchFor2 = DB_SEP_STR + searchFor2;
					dt = m_mp->FindExact(searchFor2);
					if (dt && dt->type() == C_ENUM)
						membersAdded += TypedSearchFor(NULLSTR, searchFor2, curScope, C_ENUMITEM);
				}
			}

			if (!membersAdded)
				membersAdded += TypedSearchFor(NULLSTR, searchFor, curScope, C_ENUMITEM);
		}

		if (!membersAdded)
		{
			token2 types = enumType;
			for (WTString type = types.read(DB_SEP_STR); type.GetLength() > 2; type = types.read(DB_SEP_STR))
			{
				membersAdded += TypedSearchFor(NULLSTR, DB_SEP_STR + type, curScope, C_ENUMITEM);
				if (!blindTypeGuess && curScope.Find(DB_SEP_STR + type) == -1)
					SuggestEnumType(type, false);
			}

			if (!membersAdded)
				membersAdded += TypedSearchFor(enumType, NULLSTR, curScope, C_ENUMITEM);
		}

		return membersAdded;
	}

	int TypedSearchFor(const WTString& searchForScope, const WTString& bcl, const WTString& curScope,
	                   uint searchForType, uint funcReturnType = UINT_MAX, int dbs = MultiParse::DB_ALL)
	{
		CheckStatus();
		int itemsAdded = 0;
		std::shared_ptr<DBQuery> it(new DBQuery(m_mp));
		ThreadedDbIterate(it, searchForScope, bcl, dbs, "TypedSearchFor");
		// [case: 32395] [case: 102926]
		if (it->Count() < 1000 || vaHashtag == searchForType || C_ENUMITEM == searchForType)
		{
			WTString cleanedCurScope;
			WTString lastAdded;
			for (DType* dt = it->GetFirst(); dt && itemsAdded < 1000; dt = it->GetNext())
			{
				if (dt->MaskedType() != searchForType)
					continue;

				if (curScope.Find(dt->SymScope()) != -1)
					continue;

				if (FUNC == searchForType)
				{
					_ASSERTE(-1 != funcReturnType);
					if (dt->MaskedType() == funcReturnType)
					{
						WTString checkThis(searchForScope);
						if (checkThis.IsEmpty() && bcl.GetLength())
							checkThis = bcl.Mid(1);
						if (checkThis.IsEmpty())
							return itemsAdded;

						// look at function return type
						const WTString ftype(GetAssignType(dt->Def().c_str()));
						if (ftype.Find(checkThis) != -1 || checkThis.Find(ftype) != -1)
						{
							const WTString addThis = ::StrGetSym(dt->SymScope());
							if (addThis != lastAdded)
							{
								AddSuggestion(addThis, FALSE, FALSE);
								lastAdded = addThis;
								itemsAdded++;
							}
						}
					}
				}
				else
				{
					_ASSERTE(-1 == funcReturnType);
					WTString suggestThis;
					if (C_ENUMITEM == dt->type())
					{
						if (cleanedCurScope.IsEmpty())
						{
							cleanedCurScope = DB_SEP_STR + ::CleanScopeForDisplay(curScope);
							cleanedCurScope.ReplaceAll('.', DB_SEP_CHR);
						}

						if ((searchForScope.IsEmpty() && -1 == bcl.find(cleanedCurScope)) ||
						    (bcl.IsEmpty() && !searchForScope.IsEmpty()))
						{
							// [case: 65011] qualify the enum items with correct scope
							suggestThis = GetScopedEnumItem(dt);
						}
					}

					if (suggestThis.IsEmpty())
						suggestThis = ::StrGetSym(dt->SymScope());

					if (suggestThis != lastAdded)
					{
						AddSuggestion(suggestThis, FALSE, FALSE);
						lastAdded = suggestThis;
						itemsAdded++;
					}
				}
			}

			CheckStatus();
		}
		return itemsAdded;
	}

	int SuggestFunctionsThatReturnEnums()
	{
		// case=4622 suggest enum items when using an instance of the enum (switch statement, == operator)
		WTString types = GetAssignType();
		if (types.IsEmpty())
			return 0;
		return SuggestFunctionsThatReturnEnums(types, true);
	}

	int SuggestFunctionsThatReturnEnums(const WTString& enumType, bool blindTypeGuess)
	{
		CheckStatus();
		int membersAdded = 0;
		const WTString curScope(GetCachedScope());
		membersAdded += TypedSearchFor(NULLSTR, DB_SEP_STR + ::StrGetSym(enumType), curScope, FUNC, C_ENUM);
		if (!blindTypeGuess && curScope.Find(DB_SEP_STR + ::StrGetSym(enumType)) == -1)
			SuggestEnumType(enumType, false);
		if (!membersAdded)
			membersAdded += TypedSearchFor(enumType, NULLSTR, curScope, FUNC, C_ENUM);
		return membersAdded;
	}

	// this was started by Jerry but he never enabled it.
	// see commented out call in SuggestByScope().
	void SuggestMethodsToOverride()
	{
		// class cfoo: base{ // list virtual/all? base members so user can override.
		WTString cwd = GetCStr(CurPos());
		WTString bcl = m_mp->GetBaseClassList(GetCachedScope(), false, 0, gTypingDevLang) + DB_SEP_STR;
		std::shared_ptr<DBQuery> it(new DBQuery(m_mp));
		ThreadedDbIterate(it, NULLSTR, bcl, MultiParse::DB_ALL, "SuggestMethodsToOverride");
		for (DType* dt = it->GetFirst(); dt; dt = it->GetNext())
		{
			if (StartsWithNC(StrGetSym(dt->SymScope()), cwd, FALSE))
			{
				if (!dt->IsImpl() && dt->type() == FUNC)
				{
					WTString sym = StrGetSym(dt->SymScope());
					WTString def = CleanDefForDisplay(dt->Def(), gTypingDevLang);
					WTString buf(def);
					VAParseArgTracker<ParseToCls> ptc(GetLangType());
					ptc.ParseTo(buf, ")");
					def += WTString("\r\n{\r\n\t");
					if (GetAssignType(def.c_str()) != "void")
						def += "return ";
					def += WTString("__super::$end$") + sym + "(" + ptc.GetArgStr() + ");\r\n}";

					AddSuggestion(def, FALSE, FALSE);
				}
			}
		}
	}

	bool SuggestTypesOf(const WTString& type)
	{
		CheckStatus();
		WTString suggestionsForType = WTString("SuggestionsForType ") + type;
		token2 types = gAutotextMgr->GetSource(suggestionsForType);
		auto ShouldAdd = [&]() { return types.length() > 2; };
		bool added = ShouldAdd();
		while (ShouldAdd())
			AddSuggestion(types.read("\r\n") + AUTOTEXT_SHORTCUT_SEPARATOR + suggestionsForType, FALSE, TRUE,
			              ET_AUTOTEXT_TYPE_SUGGESTION);
		return added;
	}

	void SuggestNewOrNullOf(const WTString& type)
	{
		if (type.IsEmpty())
			return;

		CheckStatus();
		DType* cd = m_mp->FindAnySym(StrGetSym(type));
		if (cd && cd->MaskedType() == C_ENUM)
			return; // Don't offer new or null on enums.  case=31117

		if (type == "var")
			return; // [case: 41898]

		// Suggest "null" or "new type"
		if (gTypingDevLang == CS)
		{
			// Ugly: Using contains for system.bool, or boolean
			if (!type.contains("bool") && !type.contains("Boolean") && !type.contains("int") &&
			    !type.contains("EventHandler")  // They provide this, and they create the new method
			    && GetLastSymScopeData().get()) // Only suggest if we know the type. case=40179
			{
				AddSuggestion("null");
				if (!BASE::InParen(m_deep))
					AddSuggestion(WTString("new ") + type);
			}
		}
		else if (gTypingDevLang == VB)
		{
			// Ugly: Using contains for system.bool, or boolean, type should be lower cased in VB
			if (!type.contains("bool") && !type.contains("int"))
			{
				AddSuggestion("Null");
				if (!BASE::InParen(m_deep))
					AddSuggestion(WTString("New ") + type);
			}
		}
		else if (IsCFile(gTypingDevLang) && IsPointer())
		{
			const bool kIsCFile(::IsCfile(g_currentEdCnt->FileName()));
			WTString suggestType;
			bool addNull = Psettings->mSuggestNullInCppByDefault;
			// [case: 61593]
			// http://msdn.microsoft.com/en-us/library/4ex65770.aspx
			bool addNullPtr = kIsCFile ? false : Psettings->mSuggestNullptr;
			bool addGcNew = false;
			bool addRefNew = false;
			bool addNew = false;

			if (!BASE::InParen(m_deep) && !kIsCFile)
			{
				bool isGcNew = false;
				bool isRefNew = false;
				suggestType = type;
				if (suggestType[suggestType.GetLength() - 1] == '*')
				{
					// suggest new Foo, not new Foo *
					suggestType = suggestType.Left(suggestType.GetLength() - 1);
					suggestType.TrimRight();
				}
				else if (suggestType[suggestType.GetLength() - 1] == '^')
				{
					// suggest gcnew Foo, not new Foo ^
					suggestType = suggestType.Left(suggestType.GetLength() - 1);
					suggestType.TrimRight();
					if (GlobalProject->CppUsesClr())
						isGcNew = true;
					if (GlobalProject->CppUsesWinRT())
						isRefNew = true; // [case: 61592]
					if (!isGcNew && !isRefNew)
					{
						if (gShellAttr->IsDevenv11OrHigher())
							isRefNew = true;
						else
							isGcNew = true;
					}
				}

				if (suggestType != "void" && suggestType != "HWND" && suggestType != "auto" && suggestType != "VOID" &&
				    suggestType != "HANDLE" && suggestType != "HDC")
				{
					if (isGcNew || isRefNew)
					{
						addNull = false;
						addNullPtr = true;
						if (isGcNew)
							addGcNew = true;
						if (isRefNew)
							addRefNew = true;
					}
					else
					{
						addNew = true;
					}
				}
			}

			if (addNullPtr)
				AddSuggestion("nullptr");
			if (addNull)
				AddSuggestion("NULL");
			if (addRefNew)
				AddSuggestion(WTString("ref new ") + suggestType);
			if (addNew)
				AddSuggestion(WTString("new ") + suggestType);
			if (addGcNew)
				AddSuggestion(WTString("gcnew ") + suggestType);
		}
		else if (IsCFile(gTypingDevLang) && -1 != type.FindNoCase("ptr"))
		{
			WTString createCall(type);

			const int spPos = createCall.Find("shared_ptr");
			const int upPos = createCall.Find("unique_ptr");

			int ueSpPos = -1;
			int ueUpPos = -1;
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				// [case: 141820] [case: 141821] add suggestions for ue4 smart pointers
				ueSpPos = createCall.Find("TSharedPtr");
				ueUpPos = createCall.Find("TUniquePtr");
			}

			if (-1 == spPos && -1 == upPos && -1 == ueSpPos && -1 == ueUpPos)
			{
				// [case: 93856]
				// check for: typedef shared_ptr<CFoo> CFooPtr;
				// or		: using CFooPtr = shared_ptr<CFoo>;
				WTString sym(createCall);
				sym.ReplaceAll("::", DB_SEP_STR);
				WTString basescope = GetCachedBaseScope();
				WTString curScope = GetCachedScope();
				DType* cd2 = m_mp->FindSym(&sym, &curScope, &basescope, FDF_NoAddNamespaceGuess | FDF_NoConcat);
				if (!cd2)
				{
					if (sym[0] != DB_SEP_CHR)
						sym = DB_SEP_STR + sym;
					cd2 = m_mp->FindExact2(sym, false);
				}

				if (cd2)
					createCall = ::DecodeTemplates(cd2->Def());

				if (0 == createCall.Find("typedef "))
				{
					createCall = createCall.Mid(8);
					int pos = createCall.ReverseFind('>');
					if (-1 != pos)
						createCall = createCall.Left(pos + 1);
				}
				else if (0 == createCall.Find("using "))
				{
					int pos = createCall.Find('=');
					if (-1 != pos)
						createCall = createCall.Mid(pos);
				}

				createCall.Trim();
			}

			// [case: 92897]
			// this will work with ::std::shared_ptr / std::shared_ptr / ::shared_ptr / shared_ptr (likewise unique_ptr)
			if (::strstrWholeWord(createCall, "shared_ptr"))
			{
				int openPos = createCall.Find('<');
				if (openPos > spPos)
				{
					if (!createCall.ReplaceAll("shared_ptr<", "make_shared<"))
						if (!createCall.ReplaceAll("shared_ptr <", "make_shared <"))
							createCall.Empty();
				}
				else
				{
					// [case: 96425]
					createCall.Empty();
				}
			}
			else if (::strstrWholeWord(createCall, "unique_ptr"))
			{
				int openPos = createCall.Find('<');
				if (openPos > upPos)
				{
					if (!createCall.ReplaceAll("unique_ptr<", "make_unique<"))
						if (!createCall.ReplaceAll("unique_ptr <", "make_unique <"))
							createCall.Empty();
				}
				else
				{
					// [case: 96425]
					createCall.Empty();
				}
			}
			else if (Psettings && Psettings->mUnrealEngineCppSupport && ::strstrWholeWord(createCall, "TSharedPtr"))
			{
				int openPos = createCall.Find('<');
				if (openPos > spPos)
				{
					// [case: 141820] add suggestions for ue4 smart pointers
					if (!createCall.ReplaceAll("TSharedPtr<", "MakeShared<"))
						if (!createCall.ReplaceAll("TSharedPtr <", "MakeShared <"))
							createCall.Empty();
				}
				else
				{
					// [case: 96425]
					createCall.Empty();
				}
			}
			else if (Psettings && Psettings->mUnrealEngineCppSupport && ::strstrWholeWord(createCall, "TUniquePtr"))
			{
				int openPos = createCall.Find('<');
				if (openPos > upPos)
				{
					// [case: 141821] add suggestions for ue4 smart pointers
					if (!createCall.ReplaceAll("TUniquePtr<", "MakeUnique<"))
						if (!createCall.ReplaceAll("TUniquePtr <", "MakeUnique <"))
							createCall.Empty();
				}
				else
				{
					// [case: 96425]
					createCall.Empty();
				}
			}
			else
				createCall.Empty();

			if (createCall.GetLength())
			{
				if (Psettings->mSmartPtrSuggestModes & CSettings::spsm_parens)
					AddSuggestion(createCall + "()");
				if (Psettings->mSmartPtrSuggestModes & CSettings::spsm_openParen)
					AddSuggestion(createCall + "(");
				if (Psettings->mSmartPtrSuggestModes & CSettings::spsm_parensWithEnd)
					AddSuggestion(createCall + "($end$)");
			}
		}
	}

	void SuggestLocalVars(const WTString& type, const WTString varName, const WTString excludeSymScope)
	{
		if (type.IsEmpty())
			return;

		SuggestTypesOf(type); // true/false/... before local vars

		// Find local vars that match the type or name
		WTString types = GetSimilarTypes(type);
		WTString vatype = type;
		vatype.ReplaceAll(".", ":");
		WTString basescope = GetCachedBaseScope();
		DType* cd = m_mp->FindExact(basescope);
		if (cd && cd->type() == NAMESPACE)
			basescope.Empty();
		for (uint i = m_deep; i && State(i - 1).m_defType != NAMESPACE; i--)
			basescope += WTString('\f') + Scope(i);
		std::shared_ptr<DBQuery> it(new DBQuery(m_mp));
		ThreadedDbIterate(it, NULLSTR, basescope, MultiParse::DB_LOCAL | MultiParse::DB_GLOBAL, "SuggestLocalVars");
		if (it->Count() < 1000)
		{
			for (DType* dt = it->GetFirst(); dt; dt = it->GetNext())
			{
				if (dt->type() == VAR)
				{
					WTString sym = StrGetSym(dt->SymScope());
					if (dt->SymScope() == excludeSymScope)
						continue;
					if (dt->HasLocalScope() && dt->FileId() == m_mp->GetFileID() && dt->Line() > (int)CurLine())
						continue; // Filter locals that are defined below current line.
					WTString bcl = m_mp->GetBaseClassListCached(dt->SymScope());
					if (gTypingDevLang == VB)
						bcl.MakeLower();
					if (0 == sym.CompareNoCase(varName))
						AddSuggestion(sym, TRUE);
					else if (bcl.Find(DB_SEP_STR + vatype) != -1)
						AddSuggestion(sym);
					else if (dt->Def().GetLength() > 1)
					{
						// [case: 91733]
						if (-1 != types.FindOneOf("*&^"))
							types.ReplaceAllRE(LR"([ ]*([*&^]+)[ ]*)", false, CStringW(L"$1"));
						WTString assignType = GetAssignType(dt->Def().c_str());
						if (-1 != assignType.FindOneOf("*&^"))
							assignType.ReplaceAllRE(LR"([ ]*([*&^]+)[ ]*)", false, CStringW(L"$1"));
						if (strstrWholeWord(types, assignType, FALSE))
							AddSuggestion(sym);
					}
				}
				else if (dt->type() == FUNC)
				{
					// consider functions that return the type we are looking for?
				}
			}
		}

		SuggestNewOrNullOf(type); // NULL/new type/... after local var matches
		SuggestEnum(type, false);
	}

	void SuggestEnum(const WTString& typeIn, bool constRestricted)
	{
		CheckStatus();
		WTString type(typeIn);
		type.ReplaceAll("::", ".");
		type.ReplaceAll(".", DB_SEP_STR);

		DType* cd = m_mp->FindAnySym(type);
		if (cd && cd->MaskedType() == C_ENUM)
			SuggestEnumType(type, true);

		DType* dt = m_mp->FindExact(DB_SEP_STR + type);
		if (dt && dt->MaskedType() == C_ENUM)
		{
			if (Is_C_CS_File(gTypingDevLang) || VB == gTypingDevLang)
			{
				if (CS == gTypingDevLang || VB == gTypingDevLang || !SuggestEnumMemberItems())
					SuggestEnumMemberItems(type, false);
			}

			if (!constRestricted)
			{
				// don't suggest functions in case statements
				if (!SuggestFunctionsThatReturnEnums())
					SuggestFunctionsThatReturnEnums(type, false);
			}
		}
		else if (cd && cd->MaskedType() == C_ENUM)
		{
			if (Is_C_CS_File(gTypingDevLang) || VB == gTypingDevLang)
				SuggestEnumMemberItems(cd->SymScope(), false);

			if (!constRestricted)
			{
				// don't suggest functions in case statements
				SuggestFunctionsThatReturnEnums(cd->SymScope(), false);
			}
		}
	}

	void SuggestEnumType(const WTString& type, bool doFilter)
	{
		if (type.Find('-') != -1)
			return;

		CheckStatus();
		WTString suggestThis(RemoveCommonScope(type));
		if (suggestThis[0] == DB_SEP_CHR)
			suggestThis = suggestThis.Mid(1);

		suggestThis.ReplaceAll(DB_SEP_STR, IsCFile(gTypingDevLang) ? "::" : ".");
		AddSuggestion(suggestThis, doFilter, doFilter); // case=21588
	}

	void SuggestSwitchCaseArgument()
	{
		// [case: 4622] suggest items for case statements
		const WTString startBlockText(::TokenGetField(State(Depth() - 1).m_begLinePos, "\r\n"));
		if (startBlockText.IsEmpty())
			return;

		WTString tmp(startBlockText);
		int pos = tmp.FindNoCase("switch");
		if (-1 == pos)
			return;

		pos = tmp.Find("(", pos);
		if (pos < 0 || pos > 25)
			return;

		tmp = tmp.Mid(pos + 1);
		tmp = ::TokenGetField(tmp, "\r\n()=-+/");
		if (tmp.IsEmpty())
			return;

		pos = tmp.Find(')');
		if (-1 != pos)
			tmp = tmp.Left(pos);

		// at this point tmp is the switch expression
		// it could be a variable or it could be a func call
		WTString basescope = GetCachedBaseScope();

		// NOTE: FindAnySym can result in bad suggestions for same named local
		// vars in different scopes of the current file
		DTypePtr dtBak;
		DType* dt = m_mp->FindAnySym(tmp);
		if (!dt && tmp.GetLength())
		{
			// [case: 65199] switch (foo.enumVar) or switch (Class::EnumType)
			// need to find DType for enumVar or EnumType
			pos = ptr_sub__int(State(Depth() - 1).m_begLinePos, m_buf);
			int offset = startBlockText.Find(tmp);
			if (-1 != offset)
			{
				pos += (offset + tmp.GetLength() - 1);

				WTString scope;
				WTString theBuf(m_buf, mBufLen);
				dtBak = ::SymFromPos(theBuf, m_mp, pos, scope, false);
				dt = dtBak.get();
			}
		}

		if (dt)
		{
			if (dt->MaskedType() == VAR || dt->MaskedType() == FUNC)
			{
				tmp = GetAssignType(dt->Def().c_str());
				dt = m_mp->FindAnySym(::StrGetSym(tmp));
			}
			else if (dt->MaskedType() != TYPE) // TODO: check typedefs
				dt = NULL;
		}

		if (!dt)
			return;

		// Don't SuggestLocalVars since case statements must be const.
		// Would be good to suggest const vars though...
		if (dt->MaskedType() == C_ENUM)
		{
			SuggestEnum(dt->SymScope(), true);
		}
		else
		{
			// should this be fully qualified or not? affects user defined snippets...
			SuggestTypesOf(::StrGetSym(dt->SymScope()));
		}
	}

	// [case: 109116]
	void SuggestUe4Attribute(const WTString& ueMacroName, const WTString& namespaceName)
	{
		_ASSERTE(!ueMacroName.IsEmpty());
		_ASSERTE(!namespaceName.IsEmpty());

		if (SuggestTypesOf(ueMacroName))
		{
			// user overrode ObjectMacros.h
			// no need to query db
			return;
		}

		WTString basescope = WTString(":") + namespaceName;
		DType* cd = m_mp->FindExact2(basescope, false, NAMESPACE);
		if (!cd)
			return;

		std::shared_ptr<DBQuery> it(new DBQuery(m_mp));
		// check system for global engine source installation
		// check project for solution engine source
		ThreadedDbIterate(it, basescope, NULLSTR, MultiParse::DB_SYSTEM | MultiParse::DB_GLOBAL, "SuggestUe4Attribute");
		if (it->Count() >= 500)
			return;

		for (DType* dt = it->GetFirst(); dt; dt = it->GetNext())
		{
			if (dt->type() != C_ENUMITEM)
			{
				// the U* keywords are defined as enum items
				continue;
			}

			WTString sym = StrGetSym(dt->SymScope());
			AddSuggestion(sym);
		}
	}

	void SuggestPropertyAttribute()
	{
		SuggestTypesOf("__property");
	}

	void CheckStatus()
	{
		if (!mKeepLooking)
			throw WtException("bail out");
	}

	BOOL Filter(const WTString& suggestion)
	{
		// starts with/contains any/acronym?  Useflags?
		if (m_startsWith.GetLength() && ISCSYM(m_startsWith[0]) && !StartsWith(suggestion, m_startsWith, FALSE))
			return TRUE;
		return FALSE;
	}
	void AddSuggestion(const WTString& suggestion, BOOL top = FALSE, BOOL filter = TRUE,
	                   UINT type = ET_SCOPE_SUGGESTION)
	{
		if (suggestion.GetLength() && !(filter && Filter(suggestion)))
		{
			if (top)
			{
				m_HistoryRank = -1; // Disable history below
				mSuggestions = CompletionSetEntry(suggestion, type) + mSuggestions;
			}
			else
			{
				static const WTString AUTOTEXT_SHORTCUT_SEPPERATOR_Str = AUTOTEXT_SHORTCUT_SEPARATOR;
				int h = (m_HistoryRank >= 0)
				            ? g_rbuffer.Rank(
				                  (SPACESTR + TokenGetField(suggestion, AUTOTEXT_SHORTCUT_SEPPERATOR_Str) + SPACESTR)
				                      .c_str())
				            : -1;
				if (h > m_HistoryRank)
				{
					m_HistoryRank = h;
					mSuggestions = CompletionSetEntry(suggestion, type) + mSuggestions;
				}
				else
					mSuggestions += CompletionSetEntry(suggestion, type);
			}
		}
	}
	WTString GetReturnType()
	{
		// return [prompt MethodType]
		for (uint d = Depth(); (int)d >= 0; d--)
		{
			if (State(d).m_defType == FUNC)
			{
				WTString type = GetAssignType(State(d).m_begLinePos);
				return type;
			}
		}
		return NULLSTR;
	}

	WTString GetSimilarTypes(const WTString& type)
	{
		WTString simTypes;
		if (type == "LPCSTR")
			simTypes = "CString WTString LPSTR";
		else if (type == "LPCWSTR")
			simTypes = "CStringW LPWSTR";
		else if (type == "LPPOINT")
			simTypes = "CPoint POINT";
		else if (type == "LPRECT")
			simTypes = "CRect RECT";
		else if (type == "CString" || type == "WTString")
			simTypes = "LPCSTR LPSTR";
		else if (type == "CComBSTR")
			simTypes = "CString WTString LPCSTR LPSTR";
		else
			return type;

		return type + " " + simTypes;
	}

	WTString GetAssignType()
	{
		return GetAssignType(GetAssignDef().c_str());
	}

	WTString GetAssignType(LPCSTR symDef)
	{
		if (gTypingDevLang == VB)
		{
			// GetTypeFromDef doesn't have VB support.
			ReadToCls rtc(GetLangType());

			// need to copy symDef to a local temp.
			// rtc.ReadTo takes a WTString&, so an implicit ctor from LPCSTR
			// happens, copies symDef, and when ReadTo returns, ~WTString frees
			// the copy.  However, rtc.State().m_lastWordPos is a ptr into the
			// buffer that was just deleted.
			WTString tmp(symDef);
			int pos = tmp.Find('\f');
			if (-1 != pos)
			{
				// [case: 63006]
				// multiple defs, just parse first
				tmp = tmp.Left(pos);
			}

			bool breakOnSpace = false;
			pos = tmp.Find("(");
			if (-1 != pos)
			{
				const LPCTSTR asPos = strstrWholeWord(tmp, "as", FALSE);
				if (!asPos)
				{
					const LPCTSTR funcPos = strstrWholeWord(tmp, "function", FALSE);
					if (!funcPos)
						breakOnSpace = true;
				}
			}
			else
			{
				int cnt = tmp.GetTokCount(' ');
				if (1 == cnt)
				{
					// assume Type varname;
					int pos2 = tmp.Find(' ');
					if (-1 != pos2)
					{
						tmp = tmp.Left(pos2);
						if (!IsReservedWord(tmp, VB))
							return tmp;
					}
				}
			}

			// [case: 68240]
			::CleanupTypeString(tmp, VB);

			WTString name;
			if (breakOnSpace)
			{
				// [case: 63006]
				// System.Windows.Forms.DialogResult Show(System.Windows.Forms.IWin32Window owner, System.String text)
				name = rtc.ReadTo(tmp, " \r\n");
			}
			else
			{
				// [case: 37765]
				// Public Overrides Function Equals(ByVal obj As Object) As Boolean
				name = rtc.ReadTo(tmp, "\r\n");
			}

			// using m_lastScopePos makes more sense than m_lastWordPos but
			// doing so breaks TestFuncEnumReturn in form1.vb and
			// SuggestNewWithoutScope02 in test_completion_list_vb.vb
			return TokenGetField(rtc.State().m_lastWordPos);
		}

		WTString def = ::GetTypeFromDef(symDef, gTypingDevLang);
		if (def.IsEmpty())
			return def;

		def.ReplaceAll("extern", "", TRUE);
		def.ReplaceAll("const", "", TRUE);
		def.ReplaceAll("constexpr", "", TRUE);
		def.ReplaceAll("consteval", "", TRUE);
		def.ReplaceAll("constinit", "", TRUE);
		def.ReplaceAll("_CONSTEXPR17", "", TRUE);
		def.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
		def.ReplaceAll("_CONSTEXPR20", "", TRUE);
		def.ReplaceAll("volatile", "", TRUE);
		def.ReplaceAll("readonly", "", TRUE); // [case: 32586] handle readonly like const(?)
		def.ReplaceAll("initonly", "", TRUE); // [case: 65644] handle initonly like readonly
		def.ReplaceAll("literal", "", TRUE);  // [case: 65644] handle literal like const
		def.ReplaceAll("?", "");              // [case: 115359]
		def.TrimLeft();
		// TODO: Encode templates?
		return def;
	}
	BOOL IsPointer()
	{
		DTypePtr lastSymScopeData = GetLastSymScopeData();
		if (lastSymScopeData)
		{
			// pRect =
			if (lastSymScopeData->IsPointer() || m_mp->IsPointer(lastSymScopeData->Def()))
				return TRUE;
		}

		WTString assignDef(GetAssignDef());
		if (!lastSymScopeData)
		{
			// CRect *pRect =
			// dependent upon a previous parse of the assignment line - which
			// will only happen if user paused after typing the declaration
			// (before the suggestion list is built)
			if (m_mp->IsPointer(assignDef))
				return TRUE;
		}

		// return if we were not called on an assignment
		const int pos = assignDef.Find('=');
		if (-1 == pos)
			return FALSE;

		// do a simple 'sloppy' check for pointer - accuracy is not as important
		// here as during a parse because this only affects suggestions

		// CRect *pRect =  (if called before parse)
		assignDef = assignDef.Left(pos);
		if (assignDef.Find('*') != -1 || assignDef.Find('^') != -1)
			return TRUE;

		return FALSE;
	}

	WTString GetScopedEnumItem(DType* dt)
	{
		WTString suggestThis(dt->SymScope());
		suggestThis = RemoveCommonScope(suggestThis);

		if (IsCFile(gTypingDevLang))
		{
			// suggestThis could be:
			//		:class:enumItem
			//		:class:enumType:enumItem
			//		:enumType:enumItem
			//		:enumItem
			//		:Func-0:enumType:enumItem
			const WTString baseScope(dt->Scope());
			if (dt->SymScope().GetTokCount(DB_SEP_CHR) > 1)
			{
				// in C++, replace ":enumType:" with ":" (without affecting :class:enumItem)
				DType* baseType = m_mp->FindExact(baseScope);
				if (baseType)
				{
					if (baseType->type() == C_ENUM)
					{
						const WTString baseTypeDef(baseType->Def());
						// [case: 65646] don't strip scope from strongly typed enum C++11
						if (!strstrWholeWord(baseTypeDef, "class") && !strstrWholeWord(baseTypeDef, "struct"))
							suggestThis.ReplaceAll(DB_SEP_STR + baseType->Sym() + DB_SEP_STR, DB_SEP_STR);

						// [case: 76960]
						int pos = suggestThis.Find('-');
						while (-1 != pos)
						{
							int pos2 = suggestThis.Find(DB_SEP_STR, pos);
							if (-1 == pos2)
								break;

							suggestThis = suggestThis.Mid(pos2);
							pos = suggestThis.Find('-');
						}
					}
				}
				else
					suggestThis.Empty();
			}
		}

		if (!suggestThis.IsEmpty() && suggestThis[0] == DB_SEP_CHR)
			suggestThis = suggestThis.Mid(1);

		if (!suggestThis.IsEmpty())
			suggestThis.ReplaceAll(DB_SEP_STR, IsCFile(gTypingDevLang) ? "::" : ".");

		return suggestThis;
	}

	WTString RemoveCommonScope(const WTString& scopedSym)
	{
		WTString retval;
		const WTString basescope(GetCachedBaseScope());
		// [case: 31477] check using directives
		const WTString scopeList(basescope + "\f" + m_mp->GetGlobalNameSpaceString(false));
		// [case: 31348] strip common scopage
		if (DB_SEP_CHR == scopedSym[0])
			retval = ::GetMinimumRequiredNameFromList(scopedSym, scopeList);
		else
		{
			const WTString tryThis(DB_SEP_STR + scopedSym);
			const WTString tmp(::GetMinimumRequiredNameFromList(tryThis, scopeList));
			if (tmp != tryThis)
				retval = tmp;
			else
				retval = scopedSym;
		}
		return retval;
	}

	struct IteratorThreadParams
	{
		std::shared_ptr<DBQuery> mDbq;
		WTString mScope;
		WTString mBcl;
		int mDbs;

		IteratorThreadParams() : mDbq(NULL), mScope(NULL), mBcl(NULL), mDbq(0)
		{
		}

		IteratorThreadParams(std::shared_ptr<DBQuery>& dbq, LPCTSTR scope, LPCTSTR bcl, int dbs)
		    : mDbq(dbq), mScope(scope), mBcl(bcl), mDbs(dbs)
		{
		}
	};

	// this is the DbQuery / DbIterator thread
	static void DbIteratorThreadFunction(LPVOID paramsIn)
	{
		std::unique_ptr<IteratorThreadParams> params((IteratorThreadParams*)paramsIn);
		params->mDbq->FindAllSymbolsInScopeList(params->mScope.c_str(), params->mBcl.c_str(), params->mDbs);
	}

	// this runs on the scope suggestion thread; it waits on the dbquery thread
	void ThreadedDbIterate(std::shared_ptr<DBQuery>& it, const WTString& scope, const WTString& bcl, int dbs,
	                       LPCSTR thrdName)
	{
		CheckStatus();

		FunctionThread* thrd =
		    new FunctionThread(DbIteratorThreadFunction, new IteratorThreadParams(it, scope.c_str(), bcl.c_str(), dbs),
		                       CString(thrdName), true, false);
		thrd->AddRef();
		thrd->StartThread();

		while (thrd->Wait(25, false) == WAIT_TIMEOUT)
		{
			if (!mKeepLooking)
			{
				it->Stop();
				vLog("WARN: TDI break");
				// don't break - wait for clean exit due to VParse lifetime
			}
		}

		thrd->Release();
		CheckStatus();
	}

	void SuggestHashtags()
	{
		int membersAdded = TypedSearchFor(":VaHashtag", NULLSTR, ":", vaHashtag, UINT_MAX, MultiParse::DB_GLOBAL);
		(void)membersAdded;
	}
};
