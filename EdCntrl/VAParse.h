#pragma once

#include "WTString.h"
#include "Edcnt.h"
#include "assert_once.h"
#include <set>
#include "FileTypes.h"
#include <memory>
#include <vector>
#include "UnrealPostfixType.h"

class DType;
class MultiParse;

enum class LiteralType
{
	None,
	Unadorned,    // could be ascii or MBCS or Unicode depending upon compiler settings
	MacroT,       // _T(), T()
	Narrow,       // C++11 u8		http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2442.htm
	Wide,         // L, @ or S prefix
	Wide16,       // C++11 u	http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2249.html
	Wide32,       // C++11 U	http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2249.html
	UnadornedRaw, // C++ unadorned raw string literal, ascii
	Lraw,         // C++11 LR (for differentiation from simple Wide with L)
	StdString,    // C++14 std::string: auto foo = "str"s
	StdWString,   // C++14 std::string: auto foo = L"str"s
	Stdu16String, // C++14 std::string: auto foo = u"str"s
	Stdu32String  // C++14 std::string: auto foo = U"str"s
};

enum class NewScopeKeyword
{
	FOR, // for OR for each OR foreach
	IF   // C++ or C++ 17 style if
};

//////////////////////////////////////////////////////////////////////////
// Macros to support language unique versions of each parser class
#define CREATE_MLC_DECL(ParserCls, ParserMlcCls)         \
	class ParserMlcCls                                   \
	{                                                    \
	  public:                                            \
		typedef std::shared_ptr<ParserCls> ParserClsPtr; \
		ParserMlcCls(int fType);                         \
		ParserCls* operator->()                          \
		{                                                \
			return m_parser.get();                       \
		}                                                \
		ParserClsPtr Ptr() const                         \
		{                                                \
			return m_parser;                             \
		}                                                \
		const ParserCls* ConstPtr() const                \
		{                                                \
			return m_parser.get();                       \
		}                                                \
                                                         \
	  protected:                                         \
		ParserClsPtr m_parser;                           \
	};

template <typename ParserMlcCls, typename LangWrappedParserCls>
typename ParserMlcCls::ParserClsPtr CreateLangWrappedParser(int fType)
{
	return std::make_shared<LangWrappedParserCls>(LangWrappedParserCls(fType));
}

#define CREATE_MLC_IMPL(ParserCls, ParserMlcCls)                                                  \
	ParserMlcCls::ParserMlcCls(int fType)                                                         \
	{                                                                                             \
		if (fType == Src || fType == Header)                                                      \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperCPP<ParserCls>>(fType);   \
		else if (Is_VB_VBS_File(fType))                                                           \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperVB<ParserCls>>(fType);    \
		else if (fType == CS)                                                                     \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperCS<ParserCls>>(fType);    \
		else if (fType == UC)                                                                     \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperUC<ParserCls>>(fType);    \
		else if (Is_Tag_Based(fType) || fType == PHP)                                             \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperHTML<ParserCls>>(fType);  \
		else if (fType == RC)                                                                     \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperRC<ParserCls>>(fType);    \
		else if (fType == JS)                                                                     \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperJS<ParserCls>>(fType);    \
		else                                                                                      \
			m_parser = CreateLangWrappedParser<ParserMlcCls, LangWrapperOther<ParserCls>>(fType); \
	}

#define CREATE_MLC(ParserCls, ParserMlcCls)  \
	CREATE_MLC_DECL(ParserCls, ParserMlcCls) \
	CREATE_MLC_IMPL(ParserCls, ParserMlcCls)

//////////////////////////////////////////////////////////////////////////
// Core parser class

#define STATE_COUNT 75
#define WBREAK_COUNT 10
#define STATEMASK 0xffff0000
#define STATE_DEFLINE 0x00010000
#define DEFAULT_START_REPARSE_LINE 300000

class VAParseBase
{
  public:
	enum ParserStateEnum
	{
		VPS_NONE,
		VPS_BEGLINE,
		VPS_ASSIGNMENT
	};
	enum ParserStateFlagsEnum
	{
		VPSF_CONSTRUCTOR = 0x1,
		VPSF_GOTOTAG = 0x2,
		VPSF_UC_STATE = 0x4,
		VPSF_INHERITANCE = 0x8,
		VPSF_SCOPE_TAG = 0x10,
		VPSF_DEC_DEEP_ON_EOS =
		    0x20,                      // must explicitly clear; VAParseBase::ParserState::ClearStatement will not clear it
		VPSF_NAKEDSCOPE_FOR_IF = 0x40, // [case: 97964] [case: 113209] [case: 78814]
		VPSF_KEEPSCOPE_FOR = 0x80,
		VPSF_KEEPSCOPE_IF = 0x100,
		// *WARNING* this enum is extended by language parsers; see:#extendsParserStateFlagsEnum to avoid overlapping
		// values when adding new parser flags
	};

	class ParserState
	{
	  public:
		LPCSTR m_begLinePos;
		LPCSTR m_lastWordPos;
		LPCSTR m_lastScopePos;
		CHAR m_lastChar;
		LPCSTR m_begBlockPos;
		ParserStateEnum m_parseState;
		ULONG m_ParserStateFlags; // ParserStateFlagsEnum bitfield
		ULONG m_StatementBeginLine;
		DTypePtr m_lwData; // DType that can outlive db hash tables
		int m_argCount;
		int m_conditionalBlocCount;
		ULONG m_defType;       // line is defining a VAR/FUNC/CLASS...
		ULONG m_defAttr;       // line is defining a v_POINTER/V_CONSTRUCTOR???
		ULONG m_privilegeAttr; // line is defining a Public/private/protected...

		char m_inComment;              // comment or string literal or preproc directive: \n * ' " # -
		char m_inSubComment;           // for comments/strings within preproc statements
		char mStringLiteralMainType;   // R L S @ u U $ C (only R @ $ C really matter) (where C means both $@)
		bool m_dontCopyTypeToLowerState = false;
		WTString mRawStringTerminator; // only valid for mStringLiteralMainType == R

		void ClearStatement(LPCSTR cPos, bool resetAllFlags = false);
		void Init(LPCSTR cPos);

		// ParserStateFlagsEnum wrapper
		void SetParserStateFlags(ULONG flag)
		{
			m_ParserStateFlags = flag;
		}
		ULONG GetParserStateFlags()
		{
			return m_ParserStateFlags;
		}
		ULONG HasParserStateFlags(ULONG flags)
		{
			return (m_ParserStateFlags & flags);
		}
		void AddParserStateFlags(ULONG flag)
		{
			m_ParserStateFlags |= flag;
		}
		void RemoveParserStateFlags(ULONG flag)
		{
			m_ParserStateFlags ^= flag;
		}

		ParserState()
		    : m_lastScopePos(nullptr) // member can be read before Init is called see:#parseStateDeepReadAhead
		{
		}
	};

	virtual ~VAParseBase() = default;
	VAParseBase(const VAParseBase &) = default;
	VAParseBase &operator =(const VAParseBase &) = default;

	enum
	{
		PF_NONE,
		PF_TEMPLATECHECK = 0x1,
		PF_TEMPLATECHECK_BAIL = 0x2,
		PF_CONSTRUCTORSCOPE = 0x4
	};
	ULONG GetParseFlags() const
	{
		return m_parseFlags;
	}

	ULONG Depth() const
	{
		return m_deep;
	}
	ULONG CurLine() const
	{
		return m_curLine;
	}
	int GetCp() const
	{
		return m_cp;
	}
	int GetBufLen() const
	{
		return mBufLen;
	}
	int GetLenOfCp() const
	{
		return GetBufLen() - GetCp();
	}
	LPCSTR CurPos() const
	{
		return &m_buf[m_cp];
	}
	virtual char CurChar() const
	{
		return m_buf[m_cp];
	}
	BOOL InLocalScope() const
	{
		return InLocalScope(m_deep);
	}
	WTString GetTemplateStr();
	WTString GetLineStr()
	{
		return GetLineStr(m_deep);
	}

	virtual void LoadParseState(VAParseBase* cp, bool assignBuf);
	ParserState& State()
	{
		return pState[m_deep];
	}
	const ParserState& ConstState() const
	{
		return pState[m_deep];
	}
	ParserState& State(ulong deep)
	{
		if (deep > STATE_COUNT) [[unlikely]]
		{
			vLog("WARN: PState too deep");
			return pState[STATE_COUNT - 1];
		}
		return pState[deep];
	}
	const ParserState& ConstState(ulong deep) const
	{
		if (deep > STATE_COUNT) [[unlikely]]
		{
			vLog("WARN: PState too deep");
			return pState[STATE_COUNT - 1];
		}
		return pState[deep];
	}
	bool IsStateDepthOk(ulong deep) const
	{
		return deep <= STATE_COUNT;
	}

	LPCSTR GetBuf() const
	{
		return m_buf;
	}
	WTString GetSubStr(LPCSTR sBeg, LPCSTR sEnd, BOOL okToTruncate = TRUE);
	BOOL m_parseGlobalsOnly;
	BOOL mParseLocalTypes; // only matters during m_parseGlobalsOnly
	BOOL m_writeToFile;

	int FileType() const;
	INT GetInMacro() const
	{
		return m_inMacro;
	}
	enum class Directive
	{
		None,
		Include,
		IfElse,
		Define,
		Pragma,
		Error
	};
	Directive GetInDirectiveType() const
	{
		return m_inDirectiveType;
	}

  protected:
	VAParseBase(int fType)
	    : m_fType(fType)
	{
	}
	void Init(LPCSTR buf, int bufLen);
	void Init(const WTString& buf)
	{
		Init(buf.c_str(), buf.GetLength());
	}
	BOOL InParen(ULONG deep)
	{
		return (deep && State(deep - 1).m_lastChar == '(');
	}
	BOOL InTemplateArg(ULONG deep)
	{
		return (deep && State(deep - 1).m_lastChar == '<');
	}
	BOOL InSquareBrace(ULONG deep)
	{
		return (deep && State(deep - 1).m_lastChar == '[');
	}
	BOOL InCurlyBrace(ULONG deep)
	{
		return (deep && State(deep - 1).m_lastChar == '{');
	}
	char NextChar() const
	{
		return m_buf[m_cp + 1];
	}
	char PrevChar() const
	{
		return m_cp ? m_buf[m_cp - 1] : '\0';
	}
	BOOL IsXref() const
	{
		return ConstState().m_lastChar == '.';
	}
	BOOL InLocalScope(ULONG deep) const
	{
		if (!deep)
			return FALSE;

		if (IS_OBJECT_TYPE(ConstState(deep - 1).m_defType))
			return FALSE;

		if (PROPERTY == ConstState(deep - 1).m_defType)
		{
			// [case: 67411]
			return !ParsePropertyAsClassScope();
		}

		return TRUE;
	}
	BOOL InClassScope(ULONG deep) const
	{
		if (!deep)
			return FALSE;

		switch (ConstState(deep - 1).m_defType)
		{
		case CLASS:
		case STRUCT:
		case C_INTERFACE:
			return TRUE;
		case PROPERTY:
			// [case: 67411]
			return ParsePropertyAsClassScope();
		case NAMESPACE:
			// [case: 87471]
			return ParseNamespaceAsClassScope();
		default:
			return FALSE;
		}
	}
	ULONG GetState(ULONG deep) const
	{
		return ConstState(deep).m_parseState;
	}
	BOOL IsDef(ULONG deep) const
	{
		return ConstState(deep).m_defType != UNDEF;
	}
	WTString GetTemplateArgs();
	bool IsAtDigitSeparator();

	virtual WTString GetLineStr(ULONG deep, BOOL okToTruncate = TRUE);

	virtual void ClearLineState(LPCSTR cPos)
	{
		State().ClearStatement(cPos);
		State().m_StatementBeginLine = m_curLine;
		if (!InParen(m_deep))
			State().m_argCount = 0;
	}

	virtual void OnNextLine()
	{
		m_curLine++;
	}

	virtual void IncDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		if (m_deep < STATE_COUNT)
			m_deep++;
		else
		{
			vLog("ERROR: Inc too deep");
			OnError(CurPos());
		}
		State().Init(CurPos() + 1);
	}

	virtual void IncCP()
	{
		// Unlike OnChar, this gets called on every char in the buffer
		m_cp++;
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		if (m_deep)
			m_deep--;
		else if (mReportDecDeepErrors)
		{
			// if this is not called, then these unit tests fail: Outline_test::testAsp and
			// VaParser_test::testIsTemplate
			OnError(CurPos());
		}
		else
		{
#if defined(_DEBUG)
			// [case: 12553]
			OnError(CurPos());
#endif // _DEBUG
		}
	}

	virtual BOOL ProcessMacro()
	{
		return FALSE;
	}

	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return FALSE;
	}

	virtual void OnCSym()
	{
	}
	virtual void OnError(LPCSTR errPos)
	{
	}
	virtual void OnDirective()
	{
	}
	virtual void OnChar()
	{
	}
	virtual void DebugMessage(LPCSTR msg);
	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
	{
	}
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseBase";
	}
	virtual BOOL IsTag();
	virtual BOOL ParsePropertyAsClassScope() const
	{
		return FALSE;
	}
	virtual BOOL ParseNamespaceAsClassScope() const
	{
		return FALSE;
	}
	// return true if in comment or string literal or preproc directive
	BOOL InComment()
	{
		return State().m_inComment != NULL;
	}
	char CommentType(ULONG deep)
	{
		return State(deep).m_inSubComment ? State(deep).m_inSubComment : State(deep).m_inComment;
	}
	char CommentType()
	{
		return CommentType(m_deep);
	}

	std::vector<ParserState> pState{size_t(STATE_COUNT + 1)};
	LPCSTR m_buf = nullptr;
	int mBufLen = 0;
	ULONG m_deep = 0;
	ULONG m_curLine = 0;
	int m_cp = 0;
	BOOL m_inIFDEFComment = false; // treat prepoc block as comment, do not dec/inc deep, do not define symbols -- but
	                               // continue to parse to get to end of block and flip state off
	Directive m_inDirectiveType = Directive::None;

	// keeps track of directive nesting so that a nested directive block does not clear an outer
	// 'comment' state stored in m_inIFDEFComment (int value stored in the stack: 0 = normal parse, 1 = 'comment' or
	// ignored)
	std::vector<int> mDirectiveBlockStack;
	INT m_inMacro = 0;
	ULONG m_parseFlags = 0;
	BOOL m_InSym = false;
	ULONG m_ReadAheadInstanceDepth = 0; // used to prevent runaway recursion of classes like IsTemplateCls
	BOOL mReportDecDeepErrors = FALSE;

	// SUGGEST_TEXT, SUGGEST_MEMBERS, ...
	virtual int GetSuggestMode();
	virtual void GetScopeInfo(ScopeInfo* scopeInfo)
	{
	}
	// Similar to m_ftype, but it changes in html files in scripts to JS/VB/CS/...
	virtual int GetLangType()
	{
		return m_fType;
	}
	int GetFType()
	{
		return m_fType;
	}

  private:
	int m_fType = 0;
};

class VAParse : public VAParseBase
{
  public:
	MultiParsePtr m_mp;
	ScopeInfoPtr GetScopeInfoPtr()
	{
		_ASSERTE(m_mp);
		return m_mp;
	}
	WTString Scope()
	{
		return Scope(m_deep);
	}
	virtual WTString Scope(ULONG deep);
	virtual void DoParse();

	VAParse(const VAParse&) = default;
	VAParse& operator=(const VAParse&) = default;

  protected:
	VAParse(int fType);

	BOOL m_Scoping;
	virtual WTString GetBaseScope();
	virtual WTString MethScope();
	void DoParse(const WTString& buf, BOOL parseFlags = PF_TEMPLATECHECK | PF_CONSTRUCTORSCOPE);
	void DoParse(LPCTSTR buf, int bufLen, BOOL parseFlags = PF_TEMPLATECHECK | PF_CONSTRUCTORSCOPE);
	virtual void SpecialTemplateHandler(int endOfTemplateArgs = 0)
	{
	}
	void ParseStructuredBinding();
	virtual void _DoParse();
	virtual void OnForIf_CreateScope(NewScopeKeyword keyword)
	{
		ForIf_CreateScope(keyword);
	}
	virtual BOOL OnForIf_CloseScope(NewScopeKeyword keyword)
	{
		return ForIf_CloseScope(keyword);
	}
	virtual void ForIf_CreateScope(NewScopeKeyword keyword);
	virtual BOOL ForIf_CloseScope(NewScopeKeyword keyword)
	{
		return FALSE;
	}
	virtual void ForIf_DoCreateScope();

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		ParserState& st = State();
		st.m_inComment = c;
		st.m_inSubComment = 0;
		if (!c)
		{
			if ('R' == st.mStringLiteralMainType)
				st.mRawStringTerminator.Empty();
			st.mStringLiteralMainType = '\0';
		}
	}

	virtual void OnDef()
	{
	}
	virtual void DoScope()
	{
	}
	virtual void ExpandMacroCode(const WTString& code)
	{
	}
	virtual LPCSTR GetParseClassName()
	{
		return "VAParse";
	}
	virtual void HandleUsingStatement()
	{
	}
	virtual int OnSymbol()
	{
		return 0;
	}
	virtual void OnUndefSymbol()
	{
	}
	virtual BOOL ShouldForceOnDef()
	{
		return FALSE;
	}
	virtual void OnAttribute()
	{
	}
	bool IsNakedScope(LPCSTR pos);
	int GetLineNumber(uint type, const WTString& def, WTString symName);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Derived classes

class VAParseKeywordSupport : public VAParse
{
  protected:
	VAParseKeywordSupport(int fType)
	    : VAParse(fType)
	{
	}

  public:
	VAParseKeywordSupport(const VAParseKeywordSupport &) = default;
	VAParseKeywordSupport& operator=(const VAParseKeywordSupport &) = default;

  protected:
	// Hooks for child classes
	virtual void TestForUnnamedStructs()
	{
	}
	virtual void OnCSym();
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseKeywordSupport";
	}

	void RewindScopePos();
};

// Add unwind support for #if #else directives
class VAParseDirectiveC : public VAParseKeywordSupport
{
  public:
	BOOL GetInDirective() const
	{
		return m_inDirective;
	}

	VAParseDirectiveC(const VAParseDirectiveC &) = default;
	VAParseDirectiveC& operator=(const VAParseDirectiveC&) = default;

  protected:
	VAParseDirectiveC(int fType)
	    : VAParseKeywordSupport(fType), m_inHashIncludeMembers(FALSE), m_savedState(fType)
	{
		m_inDirective = FALSE;
	}

	virtual void OnDirective();
	virtual void OnNextLine();
	virtual ULONG GetDefPrivAttrs(uint deep);

	virtual LPCSTR GetParseClassName()
	{
		return "VAParseDirectiveC";
	}

	BOOL m_inDirective;
	BOOL m_inHashIncludeMembers;
	VAParse m_savedState;

	typedef std::map<CStringW, int> ParsedScopedIncludeCount;
	ParsedScopedIncludeCount mParsedScopedIncludeCount;
};

////////////////////////////////////////////////////////////////////////////
// Adds Macro expansion for C/C++
class VAParseMPMacroC : public VAParseDirectiveC
{
  public:
	VAParseMPMacroC(int fType);
	VAParseMPMacroC(const VAParseMPMacroC &) = default;
	VAParseMPMacroC& operator=(const VAParseMPMacroC &) = default;


	BOOL GetProcessMacrosFlag() const
	{
		return m_processMacros;
	}
	int GetProcessMacroBraceDepth() const
	{
		return mConditionallyProcessedMacroBraceDepth;
	}
	std::vector<WTString> mBufCache;
	int m_parseTo;

  protected:
	virtual BOOL ProcessMacro();
	virtual BOOL ShouldExpandEndPos(int ep);
	virtual void OnDirective();
	virtual void ExpandMacroCode(const WTString& code);
	virtual void DoParse();
	virtual void TestForUnnamedStructs();
	virtual DType* GetMacro(WTString sym);
	BOOL ConditionallyProcessMacro();

	BOOL m_processMacros;
	int mConditionallyProcessedMacroBraceDepth;
};

////////////////////////////////////////////////////////////////////////////
// Cache state so scopeing is fast in large files
class VAParseMPCache : public VAParseMPMacroC
{
  public:
	BOOL m_useCache;
	BOOL m_updateCachePos;
	ULONG m_firstVisibleLine;

	VAParseMPCache(const VAParseMPCache &) = default;
	VAParseMPCache& operator=(const VAParseMPCache &) = default;

  protected:
	VAParseMPCache(int fType)
	    : VAParseMPMacroC(fType)
	{
		m_useCache = TRUE;
		m_updateCachePos = FALSE;
		m_cached = FALSE;
		m_firstVisibleLine = MAXLONG;
	}

	void CachePos();
	void LoadFromCache(int firstLn);
	virtual void OnNextLine();

	BOOL m_cached;
};

////////////////////////////////////////////////////////////////////////////
// Simple Reparse Class
// Add definitions as needed while parsing scope
class VAParseMPReparse : public VAParseMPCache
{
  public:
	ULONG m_startReparseLine;

	VAParseMPReparse(const VAParseMPReparse &) = default;
	VAParseMPReparse& operator=(const VAParseMPReparse &) = default;

  protected:
	VAParseMPReparse(int fType)
	    : VAParseMPCache(fType), m_startReparseLine(DEFAULT_START_REPARSE_LINE), m_stopReparseLine(MAXLONG),
	      m_addedSymbols(0), mIsUsingNamespace(false), mConcatDefsOnAddSym(true)
	{
	}

	void AdjustScopeForNamespaceUsage(WTString& theScope);
	void AddTemplateParams(const WTString& theScope);

	virtual void OnDef();
	virtual void OnAddSym(WTString symScope);
	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs);
	virtual void HandleUsingStatement();
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPReparse";
	}
	virtual void OnChar() override;
	virtual void OnHashtag(const WTString& hashTag, bool isRef);
	ULONG m_stopReparseLine;
	int m_addedSymbols;
	WTString m_enumArg;
	bool m_enumArgValueParsed = false;
	int m_enumArgValue = 0;
	bool mIsUsingNamespace;
	std::set<WTString> mUsingNamespaces;
	bool mConcatDefsOnAddSym;
};

//////////////////////////////////////////////////////////////////////////
// VAParseMPScope looks up scope of symbols and stores in m_lwData when m_scoping is set
class VAParseMPScope : public VAParseMPReparse // VAParseC
{
	WTString m_bufStorage;

  public:
	void Parse(const WTString& buf, MultiParsePtr mp = nullptr);
	void SetQuitMonitor(volatile const INT* monitorForQuit)
	{
		mMonitorForQuit = monitorForQuit;
	}
	bool GetLocalTemplateCheckFlag() const
	{
		return mIgnoreLocalTemplateCheck;
	}

  protected:
	VAParseMPScope(int fType)
	    : VAParseMPReparse(fType), mMonitorForQuit(NULL), mHighVolumeFindSymFlag(0), mIgnoreLocalTemplateCheck(false)
	{
		// note that m_firstVisibleLine may be irrelevant or wrong if this class
		// instance is not related to g_currentEdCnt
		m_firstVisibleLine = g_currentEdCnt ? g_currentEdCnt->m_firstVisibleLine : 0u;
		m_Scoping = FALSE;
		m_parseTo = 0;
	}

	VAParseMPScope(const INT* monitorForQuit, int fType)
	    : VAParseMPReparse(fType), mMonitorForQuit(monitorForQuit), mHighVolumeFindSymFlag(0),
	      mIgnoreLocalTemplateCheck(false)
	{
		// note that m_firstVisibleLine may be irrelevant or wrong if this class
		// instance is not related to g_currentEdCnt
		m_firstVisibleLine = g_currentEdCnt ? g_currentEdCnt->m_firstVisibleLine : 0u;
		m_Scoping = FALSE;
		m_parseTo = 0;
	}

	public:
	VAParseMPScope(const VAParseMPScope &) = default;
	VAParseMPScope& operator=(const VAParseMPScope &) = default;

	protected:
	void ParseTo(const WTString& buf, int parseTo = 0, MultiParsePtr mp = nullptr);
	void LoadLocals(LPCWSTR file, MultiParsePtr mp);
	void LoadLocalsAndParseFile(LPCWSTR file);
	WTString GetDataKeyStr(ULONG deep);
	DTypePtr GetCWData(ULONG deep);
	WTString GetTemplateFunctionCallTemplateArg(DType* funcData, const WTString& scopeToCheck, const WTString& bcl);

  public:
	static WTString GetTemplateFunctionCallTemplateArg(WTString expr, MultiParsePtr m_mp, int ftype, DType* funcData, const WTString& scopeToCheck, const WTString& bcl);

  protected:
	DTypePtr ResolveAutoVarAtCurPos(DTypePtr pDat, WTString* originalDef = nullptr);

	virtual BOOL IsDone();
	virtual void OnChar();
	virtual void OnCSym();
	virtual void DoScope();
	virtual void DebugMessage(LPCSTR msg);
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPScope";
	}
	virtual void PostAsyncParse()
	{
	}

	volatile const INT* mMonitorForQuit;
	int mHighVolumeFindSymFlag;
	bool mIgnoreLocalTemplateCheck; // [case: 78153]
};

//////////////////////////////////////////////////////////////////////////
// Finds references to a symbol
class FindReferences;
class VAParseMPFindUsage : public VAParseMPScope
{
  public:
	void FindUsage(const CStringW& project, const CStringW& file, MultiParsePtr mp, FindReferences* ref,
	               const WTString& buf);
	void UseQuickScope()
	{
		mFullScope = false;
		mHighVolumeFindSymFlag = FDF_NoConcat;
	}

  protected:
	VAParseMPFindUsage(int fType)
	    : VAParseMPScope(fType), mFullScope(true)
	{
	}

	void SetNextPos(LPCSTR fromPos);
	int AddRef(int flag = 0, LPCSTR overriddenScope = NULL, WTString* originalDef = nullptr);

	virtual void DoScope();
	virtual void OnCSym();
	void FindNextSymPos();
	virtual BOOL IsDone();
	bool StartsWithSymOrEnumerator(LPCSTR buf, const WTString& begStr, BOOL wholeWord = TRUE);
	virtual void OnDirective();
	virtual int OnSymbol();
	virtual void OnError(LPCSTR errPos);
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPFindUsage";
	}
	virtual BOOL ShouldExpandEndPos(int ep);

	bool IsNewBefore();
	bool IsNewAfter();
	bool IsExtern();
	bool IsHeapOrStaticOrParam(WTString* originalDef = nullptr);
	bool IsInitializerListItem();
	bool IsFunctionParam();
	bool IsMethodNameBackwards(int pos);
	bool IsCreateByTemplateFunction(WTString creationTemplateFunctions);
	bool IsCreateByTemplateMember(WTString creationTemplateFunctions);
	bool IsClassAndParens();
	bool IsInsideGlobalOrClassScope();

	FindReferences* m_FindSymRef;
	CStringW mProject;
	CStringW m_fileName;
	LPCSTR m_symPos;
	ULONG m_lastLine;
	BOOL m_foundUsage;
	BOOL mFullScope;
	int mLastOnSymCp;
};

//////////////////////////////////////////////////////////////////////////
// Simple
class DefFromLineParse : public VAParseDirectiveC
{
  public:
	WTString GetDefFromLine(WTString buf, ULONG line);

  protected:
	DefFromLineParse(int fType)
	    : VAParseDirectiveC(fType)
	{
	}

	virtual BOOL IsDone();
	virtual LPCSTR GetParseClassName()
	{
		return "DefFromLineParse";
	}

	WTString m_bufRef;
	ULONG m_parseToLine;
};

class ParseToCls : public VAParse
{
  public:
	ParseToCls(int fType)
	    : VAParse(fType), m_readToPos(0), mAcceptLineContinuation(false), mFoundContinuedLine(false)
	{
	}
	ParseToCls(const ParseToCls &) = default;
	ParseToCls& operator=(const ParseToCls &) = default;

	BOOL ParseTo(LPCSTR buf, int bufLen, LPCSTR readto, int maxLen = 1024, int parseFlags = PF_TEMPLATECHECK);
	void SetReadToPos(int pos)
	{
		m_readToPos = pos;
	}

  protected:
	virtual void OnError(LPCSTR errPos)
	{
		m_hadError = TRUE;
	}
	virtual BOOL IsDone();
	virtual LPCSTR GetParseClassName()
	{
		return "ParseToCls";
	}

	bool AnyBaseStateIsInComment();

	WTString m_readToStr;
	int m_readToPos;
	int m_maxLen;
	BOOL m_hadError;
	bool mAcceptLineContinuation;
	bool mFoundContinuedLine;
};

class ReadToCls : public ParseToCls
{
  public:
	ReadToCls(int fType, bool keepStrings = false)
	    : ParseToCls(fType), mKeepStrings(keepStrings), mKeepBraces(false)
	{
	}
	ReadToCls(const ReadToCls &) = default;
	ReadToCls& operator=(const ReadToCls &) = default;

	WTString ReadTo(LPCSTR buf, int bufLen, LPCSTR readto, int maxLen = 1024);
	WTString ReadTo(const WTString& buf, LPCSTR readto, int maxLen = 1024)
	{
		return ReadTo(buf.c_str(), buf.GetLength(), readto, maxLen);
	}
	void KeepStrings()
	{
		mKeepStrings = true;
	}
	void KeepBraces()
	{
		mKeepBraces = true;
	}

  protected:
	virtual void OnChar();
	virtual void OnComment(char c, int altOffset = UINT_MAX);
	virtual LPCSTR GetParseClassName()
	{
		return "ReadToCls";
	}

	WTString m_argStr;
	CHAR m_lastChar;
	ULONG inblock;
	ULONG inParens;
	bool mKeepStrings;
	bool mKeepBraces;
};

class ArgsSplitter : public ReadToCls
{
  public:
	enum class ArgSplitType
	{
		AngleBrackets,
		Parens
	};

	ArgsSplitter(int langType, ArgSplitType splitType = ArgSplitType::AngleBrackets)
	    : ReadToCls(langType), mSplitType(splitType)
	{
	}

	std::vector<WTString> mArgs;

  protected:
	virtual void IncDeep() override;
	virtual void DecDeep() override;
	virtual void OnChar() override;
	virtual BOOL IsDone() override;

	ArgSplitType mSplitType = ArgSplitType::AngleBrackets;
	LPCSTR mCurArgStartPos = nullptr;
	bool mIsDone = false;
};

class TextStripper : public ParseToCls
{
  public:
	TextStripper(int fType, bool keepStrings = false)
	    : ParseToCls(fType), mKeepStrings(keepStrings)
	{
	}
	WTString StripText(const WTString& buf);

	void KeepStrings()
	{
		mKeepStrings = true;
	}
	void KeepWhitespace()
	{
		mCollapseWhitespace = false;
	}
	void KeepDirectives()
	{
		mKeepDirectives = true;
	}

  protected:
	virtual void OnChar();
	virtual void OnComment(char c, int altOffset = UINT_MAX);
	virtual LPCSTR GetParseClassName()
	{
		return "TextStripper";
	}
	virtual void IncCP();

	WTString mCleanText;
	char mLastChar = '\0';
	bool mKeepStrings = false;
	bool mCollapseWhitespace = true;
	bool mKeepDirectives = false;
};

// Used to determine if < is a template definition or a comparison
class IsTemplateCls : public VAParse
{
  public:
	IsTemplateCls(int fType)
	    : VAParse(fType)
	{
		mReportDecDeepErrors = TRUE;
	}
	BOOL IsTemplate(LPCSTR buf, int bufLen, ULONG previousDepth);

  protected:
	virtual void OnError(LPCSTR errPos);
	virtual BOOL IsDone();
	virtual LPCSTR GetParseClassName()
	{
		return "IsTemplateCls";
	}

	BOOL m_hadError;
};

class ParseToEndBlock : public VAParse
{
	int mOpenPos = -1;
	int mClosePos = -1;
	int mBlockStack = 0;
	int mParenStack = 0;
	bool mIsDone = false;
	bool mHadComma = false;

  public:
	ParseToEndBlock(int fType, LPCSTR buf, int bufLen, int startPos);
	virtual BOOL IsDone();
	void OnEveryChar();

	virtual void IncCP()
	{
		OnEveryChar();
		__super::IncCP();
	}

	int GetEndPos() const
	{
		if (mClosePos != -1)
			return mClosePos;
		return m_cp;
	}

	int GetFinalStartPos() const
	{
		return mOpenPos;
	}
};

class VAScopeInfo : public DefFromLineParse
{
  public:
	void ParseEnvArgs();

	void ParseMethodQualifier(WTString& line);

	const WTString& CurSymName() const
	{
		return mCurSymName;
	}
	const WTString& CurScope() const
	{
		return mCurScope;
	}
	const WTString& CurSymScope() const
	{
		return mCurSymScope;
	}
	const WTString& CurSymType() const
	{
		return mCurSymType;
	}
	void SetCurSymType(const WTString curSymType)
	{
		mCurSymType = curSymType;
	}
	WTString CurSymDef() const
	{
		return mData ? mData->Def() : NULLSTR;
	}
	uint CurDataFileId() const
	{
		return mData ? mData->FileId() : 0;
	}
	int CurDataType() const
	{
		return mData ? (int)mData->MaskedType() : 0;
	}
	int CurDataAttr() const
	{
		return mData ? (int)mData->Attributes() : 0;
	}

	void SetMethodQualifier(const WTString& str)
	{
		mMethodQualifier = str;
	}
	const WTString& GetMethodQualifier()
	{
		return mMethodQualifier;
	}
	void SetMethodBody(const WTString& str)
	{
		mMethodBody = str;
	}

	WTString CommentStrFromDef();
	WTString ImplementationStrFromDef(bool stripCommentsFromTemplate, bool inlineImplementation,
	                                  bool explicitlyDefaulted, bool replaceNamespaces = false);
	WTString MemberImplementationStrFromDef(bool forHeaderFile);
	BOOL CreateImplementation(LPCSTR methscope, const CStringW& infile, bool isTemplate, bool isMethod,
	                          UnrealPostfixType unrealPostfixType, bool isDefaulted);
	BOOL InsertImplementation(bool isMethod, const CStringW& infile, const WTString& buf, bool inlineImplementation,
	                          long begPos, UnrealPostfixType unrealPostfixType, bool defaultedImplementation);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	BOOL CreateImplementationPropertyCppB(DType* sym, const CStringW& infile);
	BOOL ChangeOriginalLinePropertyCppB(EdCntPtr& curEd, int lnFirst, int lnLast, const WTString& snippetName,
	                                    const std::vector<WTString>& arrayTokens,
	                                    const WTString& originalVisibilityScope);
	void ExpandPropertyMacroCppB(WTString& impText, const std::vector<WTString>& arrayTokens);
#endif

	// addTemplateDeclaration should always be true except during Document Method
	WTString ExpandMacros(WTString expText, bool addTemplateDeclaration, bool inlineImplementation,
	                      bool replaceNamespaces = false);
	WTString GetQualifiedSymType(WTString curSymType, WTString expText);
	void ParseDefStr(const WTString& scope, const WTString& def, BOOL declIsImpl);

	void GeneratePropertyName();

	WTString mCurSymScope;
	WTString mAlterDef; // alternative definition for Move Implementation to Header/Class
	WTString Comment;

#ifdef VA_CPPUNIT
	void SetScopeInfo(const WTString& curSymName, const WTString& curScope, const WTString& curSymType,
	                  const WTString& methodBody, const WTString& paramString, const WTString& scopeStr, 
	                  const WTString& bufStr, const WTString& generatedPropertyName, const WTString& curSymScope)
	{
		mCurSymName = curSymName;
		mCurScope = curScope;
		mCurSymType = curSymType;
		mMethodBody = methodBody;
		mParamString = paramString;
		mScopeStr = scopeStr;
		mBufStr = bufStr;
		mGeneratedPropertyName = generatedPropertyName;
		mCurSymScope = curSymScope;
	}
#endif

protected:
	VAScopeInfo(int fType)
	    : DefFromLineParse(fType)
	{
#ifdef VA_CPPUNIT
		mData = nullptr;
#endif
	}

  private:
	void SetParamString(const WTString& paramStr)
	{
		mParamString = paramStr;
		mParamString.Trim();
	}
	DType* mData;
	WTString mCurSymName;
	WTString mCurScope;
	WTString mCurSymType;
	WTString mMethodQualifier;
	WTString mMethodBody;
	WTString mParamString;
	WTString mScopeStr;
	WTString mBufStr;
	WTString mGeneratedPropertyName;
};
CREATE_MLC_DECL(VAScopeInfo, VAScopeInfo_MLC);

BOOL CAN_USE_NEW_SCOPE(int ftype = NULL);
BOOL CAN_USE_NEW_SCOPE_Decoy_DontReallyUseMe(int ftype = NULL);

WTString MPGetScope(WTString buf, MultiParsePtr mp, int pos, int cacheLine = -1);
bool IsUnderlineThreadRequired();
void ReparseScreen(EdCntPtr ed, WTString buf, MultiParsePtr mp, ULONG lineStart, ULONG lineStop, BOOL underlineErrors,
                   BOOL runAsync);
void VAParseParseLocalsToDFile(WTString buf, MultiParsePtr mp, BOOL useCache);
int FindNextScopePos(int fType, const WTString& buf, int cp, ULONG startLine, BOOL next);
void VAParseParseGlobalsToDFile(WTString buf, MultiParsePtr mp, BOOL useCache);
WTString GetCStr(LPCSTR p);
inline WTString GetCStr(const WTString& p)
{
	return GetCStr(p.c_str());
}
std::string_view GetCStr_sv(const char* p);
int GetDeclPos(const WTString& scope, const CStringW& filename, UINT type, int& midLinePos, MultiParsePtr mp = nullptr);
BOOL GotoDeclPos(const WTString& scope, const CStringW& filename, UINT type = FUNC);
DTypePtr SymFromPos(WTString buf, MultiParsePtr mp, int pos, WTString& scope, bool thorough = true);
DTypePtr PrevDepthSymFromPos(WTString buf, MultiParsePtr mp, int& pos, WTString& scope, bool thorough = true);
bool ClearAutoHighlights();
LiteralType GetLiteralType(int langType, const WTString& txt, char literalDelimiter);
LPCSTR GetLiteralTypename(int langType, const WTString& txt);
LPCSTR SimpleTypeFromText(int langType, const WTString& txt);
int FindScopePos(const WTString& buf, LPCSTR scope, int lang);
void VAParseMPFindUsageFunc(const CStringW& project, const CStringW& file, MultiParsePtr mp, FindReferences* ref,
                            const WTString& buf, volatile const INT* monitorForQuit = NULL, bool fullscope = true);
WTString QuickScope(const WTString buf, int pos, int lang);
WTString QuickScopeLine(const WTString buf, ULONG line, int col, int lang);
// used by spaghetti: takes line + col
WTString QuickScopeContext(const WTString& buf, ULONG line, int col, int lang);
// used by VA: takes pos and has different base class
WTString QuickScopeContext(const WTString& buf, int pos, int lang);
WTString VAParseExpandAllMacros(MultiParsePtr mp, const WTString& code);
WTString VAParseExpandMacrosInDef(MultiParsePtr mp, const WTString& code);

// parses a string into two lists of symbol type and name
class FindSymDefsCls : public VAParseDirectiveC
{
  public:
	bool FindSymDefs(const WTString& def);

	// semicolon delimited lists
	const WTString& GetSymTypeList() const
	{
		return mDefTypes;
	}
	const WTString& GetSymNameList() const
	{
		return mDefSyms;
	}
	const WTString& GetSymDefaultList() const
	{
		return mDefDefaults;
	}

  protected:
	FindSymDefsCls(int fType)
	    : VAParseDirectiveC(fType), mFoundClosingParen(false), mErrors(0)
	{
	}
	virtual void OnDef();
	virtual void DecDeep() override;
	virtual void OnError(LPCSTR errPos) override;
	virtual void ClearLineState(LPCSTR cPos) override;
	void AddArg(WTString sym, WTString type, WTString defaults);
	virtual void SpecialTemplateHandler(int endOfTemplateArgs = 0) override;

  private:
	WTString mDefSyms;
	WTString mDefTypes;
	WTString mDefDefaults;
	bool mFoundClosingParen;
	int mErrors;
	bool mFoundDefForArg;
	int mArgsFound;
	int mEndOfTemplateArgs; // the cp of the '>' ending the most recent template argument list
};
CREATE_MLC_DECL(FindSymDefsCls, FindSymDefs_MLC);

class FindSymDefCls : public VAParseMPMacroC
{
  public:
	void FindSymbolInFile(const CStringW& file, const DType* sym, BOOL impl = FALSE);
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return mFoundDef;
	}
	WTString mShortLineText;
	int mBegLinePos;
	WTString mCtorInitCode;

  protected:
	FindSymDefCls(int fType)
	    : VAParseMPMacroC(fType), mFileId(0)
	{
	}
	virtual void OnDef();

  private:
	BOOL mFoundDef;
	WTString mFileTxt;
	const DType* mSymData;
	BOOL mFindImpl;
	UINT mFileId;
	WTString GetDefFromLine(LPCSTR buf, ULONG line);
};
CREATE_MLC_DECL(FindSymDefCls, FindSymDef_MLC);

// Like token class, but uses VAParse for code
class CodeToken : public ReadToCls
{
  public:
	CodeToken(int fType, LPCSTR code, bool wantStrings = false)
	    : ReadToCls(fType, wantStrings), mCodeBuf(code)
	{
	}

	CodeToken(int fType, const WTString& code, bool wantStrings = false)
	    : ReadToCls(fType, wantStrings), mCodeBuf(code)
	{
	}

	WTString read(LPCSTR parseto);
	int more();

  private:
	WTString mCodeBuf;
};

class TemplateInstanceArgsReader : public ReadToCls
{
  public:
	TemplateInstanceArgsReader()
	    : ReadToCls(Header)
	{
	}

	WTString GetTemplateInstanceArgs(const WTString& buf, int pos = 0);

  protected:
	virtual LPCSTR GetParseClassName()
	{
		return "TemplateInstanceArgsReader";
	}
	virtual void IncDeep() override;
	virtual void DecDeep() override;
	virtual void OnChar() override;
	virtual BOOL IsDone() override
	{
		if (mIsDone)
			return true;
		return __super::IsDone();
	}

	bool mIsDone = false;
	WTString mArgs;
	int mTemplateTerminators = -1;
};

// flag to find declare pos before symbol
#define BEFORE_SYMDEF_FLAG ((UINT)-1)

BOOL GetFileOutline(const WTString& fileText, LineMarkers& markers, MultiParsePtr mparse);
BOOL GetFileOutline(const WTString& fileText, LineMarkers& markers, MultiParsePtr mparse, ULONG maxLines,
                    BOOL collectDtypes);
BOOL LogFileOutline(const WTString& fileText, MultiParsePtr mparse, const CStringW& logFilePath);

BOOL GetMethodsInFile(WTString buf, MultiParsePtr mp, LineMarkers& markers);
BOOL LogMethodsInFile(const WTString& fileText, MultiParsePtr mparse, const CStringW& logFilePath);

WTString ParseLineClass_SingleLineFormat(const WTString& lineStr, BOOL commentDefaultValues, INT lang,
                                         int maxlen = 512);

// by default, comments out default values
// can remove default values "(int i=1)" -> "(int i)"
// can comment out default values "(int i=1)" -> "(int i/*=1*/)"
WTString HandleDefaultValues(int fType, const WTString& lineStr, int maxlen = 1024, LPCTSTR readToChars = ";",
                             BOOL commentOutDefaultValues = TRUE);
void CollectDefaultValues(const WTString& methodSignature, std::vector<WTString>& defaultValues, int fileType);
void InjectDefaultValues(WTString& paramString, const std::vector<WTString>& defaultValues, int fileType);

int FindInCode(const WTString& code, TCHAR ch, int fileType, int pos = 0);
int FindInCode(const WTString& code, const WTString& sub, int fileType, int pos);
int FindWholeWordInCode(const WTString& code, const WTString& sub, int fileType, int pos);
int FindSymInBCL(const WTString& bcl, const WTString& sym);
std::pair<int, int> FindInCode_Skipwhitespaces(const WTString& code, const WTString& compactExpression,
                                               std::pair<int, int> posAndLen, int correspondingBrace, int fileType);

int GetCloseParenPos(const WTString& buf, int devLang, int openParenPos = -1);
bool IsValidHashtag(const WTString& hashTag);

WTString RemoveUparamFromImp(WTString impText);
WTString AddUFunctionPostfix(WTString impText, UnrealPostfixType unrealPostfixType);

bool IsExplicitlyDefaulted(const WTString& def);

#ifdef VA_TEMPLATE_HELP_HACK
// Helps Templates in VAParse??.h files
typedef VAParseMPScope VP;
#endif
