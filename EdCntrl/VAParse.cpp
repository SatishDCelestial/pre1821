#include "stdafxed.h"
#include "VAParse.h"
#include "FindUsageDlg.h"
#include "macrolist.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "AutotextManager.h"
#define LwIS(s) StartsWith(State().m_lastWordPos, s)
#define FwIS(s) StartsWith(State().m_begLinePos, s)
#define CwIS(s) StartsWith(CurPos(), s)

enum GetNodeTextFlags
{
	GNT_HIDE = 0x01,
	GNT_EXPAND = 0x02
};

#include "VAParseCPP.h"
#include "VAParseCS.h"
#include "VAParseVB.h"
#include "VAParseUC.h"
#include "VAParseJS.h"
#include "VAParsePHP.h"
#include "VAParseHTML.h"
#include "VAParseRC.h"

// TODO: #include "VAParseHTML.h"
#include "PooledThreadBase.h"
#include "StackSpace.h"
#include "wt_stdlib.h"
#include "assert_once.h"
#include "file.h"
#include "Import.h"
#include "Settings.h"
#include "DBLock.h"
#include "EolTypes.h"
#include "WtException.h"
#include "Registry.h"
#include "FileLineMarker.h"
#include "StringUtils.h"
#include "FileOutlineFlags.h"
#include "VAFileView.h"
#include "WrapCheck.h"
#include "FeatureSupport.h"
#include "VASeException\VASeException.h"
#if defined(_DEBUG) && defined(SEAN)
#include "..\common\PerfTimer.h"
#endif // _DEBUG && SEAN
#include "myspell\WTHashList.h"
#include "SyntaxColoring.h"
#if defined(VA_CPPUNIT)
#include "tests\EdTests\ParserTestUtils.h"
#endif
#include "FileFinder.h"
#include "VAHashTable.h"
#include "IdeSettings.h"
#include "AutoReferenceHighlighter.h"
#include "DBQuery.h"
#include "SubClassWnd.h"
#include "DatabaseDirectoryLock.h"
#include "VAAutomation.h"
#include "..\common\ThreadStatic.h"
#include "GetFileText.h"
#include "LogElapsedTime.h"
#include "FindSimilarLocation.h"
#include "ExtractMethod.h"
#include "DTypeDbScope.h"
#include "inheritanceDb.h"
#include "ThreadSafeStr.h"
#include "FileId.h"
#include "CommentSkipper.h"
#include "ParenSkipper.h"
#include "..\common\ScopedIncrement.h"
#include "RegKeys.h"
#include "ParseThrd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

#if defined(xxSEAN) && defined(_DEBUG)
#define case2321
#endif // SEAN && _DEBUG

static CCriticalSection sMacroExpandLock;

WTString GetOperatorChars(LPCSTR p, LPCSTR* outEndPtr = NULL);
static WTString StripExtraWhiteChars(const WTString& str);
bool IsFreeFunc(const DType* sym);
const int MAX_SEARCH_LEN = 4096;

// *** DO NOT MODIFY THIS FUNCTION ***
BOOL CAN_USE_NEW_SCOPE_Decoy_DontReallyUseMe(int ftype /*= NULL*/)
{
	// *** DO NOT MODIFY THIS FUNCTION ***
	if (!ftype && g_currentEdCnt)
		ftype = g_currentEdCnt->m_ftype;
	switch (ftype)
	{
	case Java:
	case Src:
	case Header:
	case Idl:
	case VB:
	case VBS:
	case CS:
	case UC:
		return WrapperCheck::IsOk();

	default:
		return FALSE;
	}
	// *** DO NOT MODIFY THIS FUNCTION ***
}

// *** DO NOT MODIFY THIS FUNCTION ***
BOOL SupportedFeatureDecoy_DontReallyUseMe(int feature, int ftype /*= NULL*/)
{
	// *** DO NOT MODIFY THIS FUNCTION ***
	BOOL retval = FALSE;
	if (!ftype && g_currentEdCnt)
		ftype = g_currentEdCnt->m_ftype;
	switch (feature)
	{
	case Feature_Outline:
		if (Idl == ftype)
		{
			retval = FALSE;
			break;
		}
		// fall through
	case Feature_HCB:
	case Feature_Refactoring:
		retval = Is_C_CS_VB_File(ftype);
		break;
	case Feature_MiniHelp:
		retval = Is_C_CS_VB_File(ftype) || ftype == JS;
		break;
	}

	if (retval && !WrapperCheck::IsOk())
		retval = false;

	return retval;
	// *** DO NOT MODIFY THIS FUNCTION ***
}

// Support for similar c-like languages JS/Java/...
template <class VP>
class LangWrapperOther : public VP
{
  public:
	using BASE = VP;
	using BASE::GetLangType;
	using BASE::State;

	LangWrapperOther(int fType)
	    : VP(fType)
	{
	}

	virtual void OnError(LPCSTR errPos)
	{
		if (State().m_inComment) // Only underline spelling errors
			VP::OnError(errPos);
	}
	virtual void DoScope()
	{
		// Don't show guesses
		VP::DoScope();
		if (GetLangType() != RC && GetLangType() != Idl)
		{
			if (State().m_lwData && !(State().m_lwData->infile()))
				State().m_lwData.reset();
		}
	}
};

#include "VAParseTextOut.h"
#include "VAParseCodeGraph.h"
#include "VAParseCodeGraph.inl"

MultiParsePtr VAParseExtTextOutCls::s_mp;

#if defined _DEBUG
#define DEBUG_VAPARSE
#define PrintDebugMsg(s) Log(s)
#else
// #define PrintDebugMsg(s) AfxOutputDebugString(s)
#define PrintDebugMsg(s) Log(s)
#endif // _DEBUG

// bumped up for case 60987, case 93482
const int kMaxLen = 3071;

// ParseLineClass::SingleLineFormat()
class ParseLineClass : public ReadToCls
{
  public:
	static WTString SingleLineFormat(int fType, const WTString& lineStr, BOOL commentDefaultValues, int maxlen = 512)
	{
		// Strip whitespace, change // comments to /**/ to make it single line safe
		// Optional: comment default values "(int i=1)" -> "(int i/*=1*/)"
		ParseLineClass ptc(fType);
		ptc.commentDefaultValues = commentDefaultValues;

		if (Is_C_CS_File(fType))
		{
			// [case: 71459] maintain last trailing line comment
			WTString ln(lineStr);
			ln.TrimRight();
			ptc.mTrailingLineCommentPos = ln.ReverseFind("//");
			const int lnCnt = ln.GetTokCount('\n');
			if (0 != lnCnt)
			{
				const int pos = ln.ReverseFind('\n');
				if (-1 != pos && pos > ptc.mTrailingLineCommentPos)
				{
					// oops - line break after line comment therefore,
					// mTrailingLineCommentPos is not a valid trailing line comment
					ptc.mTrailingLineCommentPos = UINT_MAX;
				}
			}
		}

		WTString line(ptc.ReadTo(lineStr, ";", maxlen));
		line.Trim();
		return line;
	};

	ParseLineClass(int fType)
	    : ReadToCls(fType), mTrailingLineCommentPos(UINT_MAX)
	{
		mInPureQualifier = mHadOpenParen = false;
		inargComment = 0;
		commentDefaultValues = TRUE;
		mDefValBeingWritten = false;
	}

  protected:
	BOOL commentDefaultValues;
	bool mInPureQualifier;
	bool mHadOpenParen;
	ULONG inargComment;
	int mTrailingLineCommentPos;
	bool mDefValBeingWritten;

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (inargComment && c == '*')
		{
			// prevent double uncomment */*/ from appearing at end of param list.
			// prevent "(int i = 1 /*comment*/)" from becoming "(int i /*= 1 /*comment*/*/)"
			inargComment = 0;
		}

		if (CommentType() == '\n' && !c && (UINT_MAX == mTrailingLineCommentPos || GetCp() < mTrailingLineCommentPos))
		{
			m_argStr += "*/ ";
		}

		if (c == '\n' && GetCp() != mTrailingLineCommentPos)
		{
			m_argStr += "*";
			m_cp++;
		}
		ReadToCls::OnComment(c, altOffset);
	}
	void OnEveryChar()
	{
		char c = CurChar();
		if (!InComment() && '(' == c)
			mHadOpenParen = true;
		if (commentDefaultValues && !InComment())
		{
			if (c == '=' && !inargComment)
			{
				inargComment = m_deep + 1;
				if (m_deep)
				{
					if (!Is_VB_VBS_File(GetLangType()))
					{
						if (Psettings->mIncludeDefaultParameterValues)
							m_argStr += "/*";
						else
							mDefValBeingWritten = true;
					}
				}
				else if (mHadOpenParen) // [case: 22639] operator != () etc
				{
					// [case: 6439] virtual void foo() = 0;
					mInPureQualifier = true;
				}
			}

			if ('{' == c && inargComment == (m_deep + 1) && mInPureQualifier)
				mInPureQualifier = false;

			if ((',' == c || ')' == c) && inargComment == (m_deep + 1))
			{
				inargComment = 0;
				if (!Is_VB_VBS_File(GetLangType()))
				{
					if (Psettings->mIncludeDefaultParameterValues)
						m_argStr += "*/";
					else
						mDefValBeingWritten = false;
				}
			}

			// Since no block comment in VB, we eat the default value
			if (Is_VB_VBS_File(GetLangType()) && inargComment)
				c = ' ';
		}

		if (wt_isspace(c))
			c = ' ';
		if (m_lastChar != ' ' || c != ' ')
		{
			if (!mInPureQualifier && !mDefValBeingWritten)
				m_argStr += c;
		}
		m_lastChar = c;
	}
	virtual void IncCP()
	{
		if (!m_lastChar)
			OnEveryChar();
		ReadToCls::IncCP();
		OnEveryChar();
	}
	virtual void OnChar(){}; // ignore default ReadToCls processing
};
CREATE_MLC(ParseLineClass, ParseLineClass_MLC);

WTString ParseLineClass_SingleLineFormat(const WTString& lineStr, BOOL commentDefaultValues, INT lang,
                                         int maxlen /*= 512*/)
{
	ParseLineClass_MLC vbLineParse(lang);
	return vbLineParse->SingleLineFormat(lang, lineStr, commentDefaultValues, maxlen);
}

// sort of similar to ParseLineClass but does not collapse lines, whitespace.
// does not convert line comments to block comments.
// should return input with default values either commented out or removed.
class DefaultValueHandler : public ReadToCls
{
  public:
	DefaultValueHandler(int fType, BOOL commentDefaultValues)
	    : ReadToCls(fType), mCommentDefaultValues(commentDefaultValues), mInDefaultValue(true), mInArgComment(0),
	      mLastOpenGroupChar('\0')
	{
		if (mCommentDefaultValues && Is_VB_VBS_File(fType))
		{
			// no block comments in vb/vbs so eat default values instead
			mCommentDefaultValues = FALSE;
		}
	}

  protected:
	BOOL mCommentDefaultValues;
	bool mInDefaultValue;
	ULONG mInArgComment;
	char mLastOpenGroupChar;

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (mInArgComment)
		{
			if (c == '*' && mCommentDefaultValues)
			{
				// prevent double uncomment */*/ from appearing at end of param list.
				// prevent "(int i = 1 /*comment*/)" from becoming "(int i /*= 1 /*comment*/*/)"
				mInArgComment = 0;
			}
		}

		ReadToCls::OnComment(c, altOffset);
	}

	void OnEveryChar()
	{
		char c = CurChar();
		if (InComment())
		{
			if (Is_VB_VBS_File(GetLangType()) && ('\r' == c || '\n' == c) && CommentType() == '\'')
				OnComment('\0');
		}
		else
		{
			char nc = NextChar();
			if ('(' == c && !mInArgComment)
				mLastOpenGroupChar = c;
			else if ('<' == c && !mInArgComment)
			{
				if (nc != '=' && m_lastChar != '=')
					mLastOpenGroupChar = c;
			}
			else if (c == '=' && !mInArgComment && m_deep && mLastOpenGroupChar)
			{
				// != == <= >=
				if ('=' != nc && !strchr("=!<>", m_lastChar))
				{
					mInArgComment = m_deep + 1;
					if (mCommentDefaultValues)
						m_argStr += "/*";
				}
			}
			else if (mInArgComment == (m_deep + 1))
			{
				if (',' == c || (')' == c && '(' == mLastOpenGroupChar) ||
				    ('>' == c && '<' == mLastOpenGroupChar && PrevChar() != '='))
				{
					mInArgComment = 0;
					if (',' != c)
						mLastOpenGroupChar = '\0';
					if (mCommentDefaultValues)
						m_argStr += "*/";
				}
			}
		}

		if (mInArgComment && !mCommentDefaultValues)
			return;

		m_argStr += c;
		m_lastChar = c;
	}

	virtual void IncCP()
	{
		if (!m_lastChar)
			OnEveryChar();
		ReadToCls::IncCP();
		OnEveryChar();
	}

	virtual void OnChar()
	{
		// ignore default ReadToCls processing
	}
};

// [case: 2948], [case: 86091]
WTString HandleDefaultValues(int fType, const WTString& lineStr, int maxlen /*= 1024*/, LPCTSTR readToChars /*= ";"*/,
                             BOOL commentOutDefaultValues /*= TRUE*/)
{
	// by default, comments out default values
	DefaultValueHandler ptc(fType, commentOutDefaultValues);
	WTString line(ptc.ReadTo(lineStr, readToChars, maxlen));
	return line;
};

class CloseParenPosCls : public ReadToCls
{
  public:
	CloseParenPosCls(int fType, int openParenPos = -1)
	    : ReadToCls(fType), mOpenParenPos(-1), mCloseParenPos(-1), mParenStack(0)
	{
	}

	int GetOpenParenPos() const
	{
		return mOpenParenPos;
	}
	int GetCloseParenPos() const
	{
		return mCloseParenPos;
	}

  protected:
	int mOpenParenPos;
	int mCloseParenPos;
	int mParenStack;

	BOOL IsDone()
	{
		if (mCloseParenPos != -1)
			return TRUE;

		return __super::IsDone();
	}

	void OnEveryChar()
	{
		if (InComment())
			return;

		const char c = CurChar();
		if ('(' == c)
		{
			if (-1 == mOpenParenPos)
				mOpenParenPos = m_cp;
			if (m_cp >= mOpenParenPos)
				++mParenStack;
		}
		else if (')' == c && mOpenParenPos > -1 && m_cp > mOpenParenPos)
		{
			if (!--mParenStack)
				mCloseParenPos = m_cp;
		}
	}

	virtual void IncCP()
	{
		OnEveryChar();
		__super::IncCP();
	}

	virtual void OnChar(){}; // ignore default ReadToCls processing
};

int GetCloseParenPos(const WTString& buf, int devLang, int openParenPos /*= -1*/)
{
	_ASSERTE(openParenPos == -1 || buf[openParenPos] == '(');
	CloseParenPosCls gpp(devLang, openParenPos);
	gpp.ReadTo(buf, "");
	return gpp.GetCloseParenPos();
}

int GetSafeReadLen(LPCTSTR readPos, LPCSTR maybeMasterBuf, int masterBufLen)
{
	const bool kReadPosIsInBuf = readPos >= maybeMasterBuf && readPos <= maybeMasterBuf + masterBufLen;
	// [case: 61448] workaround for ExpandMacroCode / State().m_begLinePos problem
	// ExpandMacroCode can do strange things to State().m_begLinePos / readPos
	// if readPos doesn't even point into maybeMasterBuf, can't rely on masterBufLen.
	const int readToLen = kReadPosIsInBuf
	                          ? masterBufLen - ptr_sub__int(readPos, maybeMasterBuf)
	                          : strlen_i(readPos); // no alternative but to trust that the string is null-terminated...

	return readToLen;
}

// Used to determine if < is a template definition or a comparison
BOOL IsTemplateCls::IsTemplate(LPCSTR buf, int bufLen, ULONG previousDepth)
{
	DEFTIMERNOTE(VAP_IsTemplate, NULL);
	m_ReadAheadInstanceDepth = previousDepth + 1;
	// [case: 15454] prevent runaway recursion
	if (m_ReadAheadInstanceDepth < 20)
	{
		m_hadError = FALSE;
		DoParse(buf, bufLen, PF_TEMPLATECHECK | PF_CONSTRUCTORSCOPE | PF_TEMPLATECHECK_BAIL);
		return CurChar() == '>' && PrevChar() != '=';
	}
	else
	{
		DebugMessage("WARN: bailing on deep template");
		m_hadError = TRUE;
		return false; // ??
	}
}

void IsTemplateCls::OnError(LPCSTR errPos)
{
	// paren mismatch
	m_hadError = TRUE;
}

BOOL IsTemplateCls::IsDone()
{
	if (m_cp > mBufLen)
		return TRUE;

	if (!InComment() && !m_deep)
	{
		char ch = CurChar();
		if ('>' == ch && PrevChar() != '=')
			return TRUE; // Looks like a template
		if ('{' == ch || ';' == ch)
			return TRUE; // error
	}

	if (m_hadError)
		return TRUE;

	// [case: 71326] increased max len
	if (m_cp > (kMaxLen / 2))
		return TRUE;

	return FALSE;
}

//////////////////////////////////////////////////////////////////////////
// VAParseBase

void VAParseBase::ParserState::Init(LPCSTR cPos)
{
	m_begBlockPos = cPos;
	m_StatementBeginLine = 0;
	m_argCount = 0;
	m_conditionalBlocCount = 0;
	m_privilegeAttr = 0;
	m_inComment = '\0';
	m_inSubComment = '\0';
	mStringLiteralMainType = '\0';
	m_dontCopyTypeToLowerState = false;
	ClearStatement(cPos, true);
}

void VAParseBase::ParserState::ClearStatement(LPCSTR cPos, bool resetAllFlags /*= false*/)
{
	m_begLinePos = m_lastWordPos = m_lastScopePos = cPos;
	m_lastChar = ';';
	m_parseState = VPS_NONE;
	m_defType = UNDEF;
	m_defAttr = 0;
	m_lwData.reset();
	if (resetAllFlags)
		m_ParserStateFlags = 0;
	else
	{
		bool ss = !!HasParserStateFlags(VPSF_DEC_DEEP_ON_EOS);
		m_ParserStateFlags = 0;
		if (ss)
			AddParserStateFlags(VPSF_DEC_DEEP_ON_EOS);
	}
}

// Allow us to save/load the state of the parser for #ifdef's or caching scope pos
void VAParseBase::LoadParseState(VAParseBase* cp, bool assignBuf)
{
	if (assignBuf)
	{
		m_buf = cp->m_buf;
		mBufLen = cp->mBufLen;
	}

	// buffer can be switched, but we need to fix all lpcstr's to point to new buffer
	const auto delta = cp->m_buf - m_buf;
	for (ULONG deep = 0; deep <= cp->m_deep; deep++)
	{
		State(deep) = cp->State(deep);
		if (delta)
		{
			// Test to see if lpcstr is in this buffer if so fix it's offset
			// else it is a pointer to a temporary buffer for macros, leave it pointing there

			ParserState& ps = State(deep);

#if defined(_DEBUG)
			if (ps.m_begLinePos < cp->m_buf || ps.m_begLinePos > &cp->m_buf[cp->mBufLen])
			{
				// This is not a ptr into GetBuf(), so assert that this
				// is a ptr into an item in mBufCache
				const WTString cls = GetParseClassName();
				if (cls == "MPGetScopeCls" || cls == "VAParseMPUnderline")
				{
					VAParseMPCache* _this = (VAParseMPCache*)this;

					bool foundBuf = false;
					for (UINT i = 0; i < _this->mBufCache.size(); ++i)
					{
						LPCSTR bufPtr = _this->mBufCache[i].c_str();
						int bufPtrLen = _this->mBufCache[i].GetLength();

						if (ps.m_begLinePos >= bufPtr && ps.m_begLinePos <= &bufPtr[bufPtrLen])
						{
							foundBuf = true;
							break;
						}
					}

					_ASSERTE(foundBuf);
					if (!foundBuf)
					{
						vLog("ERROR: LoadParseState: %s m_begLinePos bad? not backed by mBufCache", cls.c_str());
					}
				}
				else
				{
					_ASSERTE(!"VAParseBase::LoadParseState: What class is this?");
				}
			}
#endif
			auto FixOffsetofLocalBuffers = [delta, cp](LPCSTR& p) {
				if (p >= cp->m_buf && p <= &cp->m_buf[cp->mBufLen])
				{
					p -= delta;
					return;
				}

				if (p)
				{
					vLog("WARN: LoadParseState: pState pos is not in parse buf");
				}
			};

			FixOffsetofLocalBuffers(ps.m_begLinePos);
			FixOffsetofLocalBuffers(ps.m_lastWordPos);
			FixOffsetofLocalBuffers(ps.m_lastScopePos);
			FixOffsetofLocalBuffers(ps.m_begBlockPos);
		}
	}
	m_cp = cp->m_cp;
	_ASSERTE(m_cp <= mBufLen); // m_cp can be == mBufLen because m_buf is NULL terminated (buflen does not include NULL)
	m_curLine = cp->m_curLine;
	m_inIFDEFComment = cp->m_inIFDEFComment;
	mDirectiveBlockStack = cp->mDirectiveBlockStack;
	m_deep = cp->m_deep;
}

//////////////////////////////////////////////////////////////////////////
// VAParseBase

void VAParseBase::Init(LPCSTR buf, int bufLen)
{
	m_buf = buf;
	if (bufLen < 0)
	{
		_ASSERTE(bufLen >= 0);
		int len;
		for (len = 0; len < 1024 && buf[len]; len++)
			; // len is bad, read to null terminator
		bufLen = len;
	}
	mBufLen = bufLen;
	m_deep = 0;
	m_curLine = 1;
	m_cp = 0;
	m_inMacro = 0;
	m_inIFDEFComment = FALSE;
	m_InSym = FALSE;
	m_inDirectiveType = Directive::None;

	State().Init(CurPos());
}

WTString VAParseBase::GetLineStr(ULONG deep, BOOL okToTruncate /*= TRUE*/)
{
	if (deep && State().m_parseState == VPS_ASSIGNMENT && State(deep - 1).m_lastChar == ':')
		deep--; //   Case 1083: dec deep for assignments "Constructor() : var(0){}"
	LPCSTR p1 = State(deep).m_begLinePos;
	if (!p1)
		return NULLSTR;
	LPCSTR p2 = CurPos();
	WTString lineStr;
	try
	{
		lineStr = GetSubStr(p1, p2, okToTruncate);
	}
	catch (const WtException& e)
	{
		vLog("ERROR: GetLineStr() %s", e.GetDesc().c_str());
		return lineStr;
	}
	if (Is_VB_VBS_File(GetLangType()))
		lineStr = TokenGetField(p1, "\r\n");
	lineStr = ParseLineClass::SingleLineFormat(GetLangType(), lineStr, FALSE, lineStr.GetLength());
	return lineStr;
}

void VAParseBase::DebugMessage(LPCSTR msg)
{
	// Ignore errors in OnPaint and IsTemplate.
}

WTString VAParseBase::GetSubStr(LPCSTR sBeg, LPCSTR sEnd, BOOL okToTruncate /*= TRUE*/)
{
	auto len = sEnd - sBeg;
	if (!len)
		return NULLSTR;

	if (sBeg > sEnd)
	{
		_ASSERTE(!"VAParseBase::GetSubStr reversed args");
		vLog("ERROR: GetSubStr() reversed args");
		return NULLSTR;
	}

	/*  Legend for comments below:
	    p1 == sBeg
	    p2 == sEnd
	    bs == buf start / m_buf
	    be == buf end / m_buf[m_BufLen]  */

	if (sBeg >= m_buf && sEnd <= &m_buf[mBufLen])
	{
		// bs p1 p2 be: ok
		// both positions are inside of our buf
		if (len > mBufLen)
		{
			// only check mBufLen if both beg and end point into the buffer
			_ASSERTE(!"VAParseBase::GetSubStr invalid len");
			if (!okToTruncate)
				throw WtException("GetSubStr > mBufLen");
			vLog("ERROR: GetSubStr() invalid len");
			return NULLSTR;
		}
	}
	else
	{
		// one or both positions are not in our buf
		//
		if (sBeg < m_buf && sEnd > &m_buf[mBufLen])
		{
			// p1 bs be p2: bad
			// starting before our buf and ending beyond the end of it
			vLog("ERROR: GetSubStr() exceeds buf on both ends");
			return NULLSTR;
		}

		if (sBeg < m_buf && sEnd >= m_buf)
		{
			// p1 bs p2 be: bad
			// starting before our buf and ending in our buf
			vLog("ERROR: GetSubStr() bad start?");
			return NULLSTR;
		}

		if (sEnd > &m_buf[mBufLen] && sBeg < &m_buf[mBufLen])
		{
			// bs p1 be p2: bad
			// starting in our buf and ending beyond the end of it
			vLog("ERROR: GetSubStr() sEnd beyond buf");
			return NULLSTR;
		}

		// both positions are not in our buf and both are either before or after it;
		// trust that caller knows what they are doing
		// p1 p2 bs be: ok but might truncate
		// bs be p1 p2: ok but might truncate
		vLog("warn: GetSubStr() called on non-m_buf pointers");

		// but test anyway
		int testLen = 0;
		for (; testLen < len && sBeg[testLen]; testLen++)
			;

		if (testLen < len)
			len = testLen;
	}

	if (len > kMaxLen)
	{
		if (!okToTruncate)
			throw WtException("GetSubStr > kMaxLen");
		vLog("WARN: GetSubStr() truncated from %d chars to %d", (int)len, kMaxLen);
		len = kMaxLen;
	}

	return WTString(sBeg, (int)len);
}

bool VAParseBase::IsAtDigitSeparator()
{
	if (InComment())
		return false;

	if (CurChar() != '\'')
		return false;

	char ch1 = PrevChar();
	if (!::wt_isxdigit(ch1))
		return false;

	char ch2 = NextChar();
	if (!::wt_isxdigit(ch2))
		return false;

	if ('8' != ch1 || m_cp == 1)
		return true;

	if (m_cp < 3)
		return true;

	// watch out for u8 char literal like u8'0' -- is not a digit
	if (m_buf[m_cp - 2] == 'u')
		return false;

	return true;
}

WTString VAParseBase::GetTemplateStr()
{
	for (int deep = (int)m_deep; deep >= 0; deep--)
	{
		if (StartsWith(State((ulong)deep).m_begLinePos, "template"))
		{
			WTString tmplateArg = GetSubStr(State((ulong)deep).m_begLinePos, State((ulong)deep).m_lastScopePos);
			int i = tmplateArg.find_last_of(">");
			if (i != -1)
			{
				return tmplateArg.Mid(0, i + 1);
			}
		}
	}
	return NULLSTR;
}

// this class works for template definition, not instances
class TemplateArgsReader : public ReadToCls
{
  public:
	TemplateArgsReader(int fType)
	    : ReadToCls(fType)
	{
	}

	WTString args;

  protected:
	virtual void OnDef()
	{
		WTString sym = GetCStr(State().m_lastScopePos);
		if (args.GetLength())
			args += WTString(", ");
		args += sym;
	}
};

WTString VAParseBase::GetTemplateArgs()
{
	WTString tmplstr = GetTemplateStr();
	TemplateArgsReader rtc(GetLangType());
	rtc.ReadTo(tmplstr, ";{");
	return WTString("<") + rtc.args + ">";
}

BOOL VAParseBase::IsTag()
{
	return State().m_begLinePos == State().m_lastWordPos || FwIS("case") || LwIS("private") || LwIS("public") ||
	       LwIS("protected") ||
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	       LwIS("__published") ||
#endif
	       LwIS("internal") ||
	       // case=912 Qt Library
	       LwIS("slots") || LwIS("signals") || LwIS("Q_SLOTS") || LwIS("Q_SIGNALS");
}

int VAParseBase::FileType() const
{
	return m_fType;
}

int VAParseBase::GetSuggestMode()
{
	int suggestBitsFlag = (Psettings && Psettings->m_defGuesses) ? SUGGEST_TEXT : 0;
	if (InComment())
	{
		if (State().m_inComment == '#' && m_inDirectiveType == Directive::Include)
		{
			if (CommentType() == '#')
			{
				// check to see if we are in the <>'s
				WTString line = GetSubStr(State().m_begLinePos, CurPos());
				if (line.contains("<") && !line.contains(">"))
					return SUGGEST_FILE_PATH;
			}
			if (CommentType() == '"')
				return SUGGEST_FILE_PATH;
		}
		if ((CommentType() == '"' || CommentType() == '\'') &&
		    PrevChar() == CommentType()) // Don't suggest in the middle of a string. case=20647
			return suggestBitsFlag;
		if (CommentType() == '#' && (m_inDirectiveType == Directive::IfElse || m_inDirectiveType == Directive::Define))
			return SUGGEST_SYMBOLS; // Allow suggestions in #if/define blocks [case=32089]
		return SUGGEST_NOTHING;
	}
	if (IsXref())
		return SUGGEST_MEMBERS;
	return SUGGEST_SYMBOLS | suggestBitsFlag;
}

//////////////////////////////////////////////////////////////////////////
// VAParse
VAParse::VAParse(int fType)
    : VAParseBase(fType)
{
	_ASSERTE(HasSufficientStackSpace());
	m_mp = NULL;
	Init(NULL, 0);
	m_parseFlags = PF_TEMPLATECHECK | PF_CONSTRUCTORSCOPE;
	m_parseGlobalsOnly = FALSE;
	mParseLocalTypes = FALSE;
	m_writeToFile = FALSE;
}

WTString VAParse::Scope(ULONG deep)
{
	_ASSERTE(HasSufficientStackSpace());
	DEFTIMERNOTE(VAP_Scope, NULL);
#define MAX_SCOPE_LEN 512
	const std::unique_ptr<CHAR[]> bufVec(new CHAR[MAX_SCOPE_LEN + 2]);
	CHAR* buf = &bufVec[0];
	ULONG idx = 0;
	if (!deep)
		return DB_SEP_STR;

	for (ULONG i = 0; i < deep && idx < MAX_SCOPE_LEN; i++)
	{
		if (State(i).m_lastChar == '(' && m_deep && InLocalScope(m_deep - 1))
		{
#define LswIS(s) StartsWith(State(i).m_lastWordPos, s)
			// Compatibility with Mparse, "foo(){ for(int i;", scope is just :foo:i
			if (LswIS("while") || (FileType() == CS && LswIS("if")))
				continue;
		}
		if (FileType() == CS && StartsWith(State(i).m_lastScopePos, "is"))
			continue;

		LPCSTR p1 = State(i).m_lastScopePos;
		BOOL externBlock = FALSE;
		if (!p1)
			externBlock = TRUE;
		else if ((*p1 == 'e' && StartsWith(p1, "extern")) || (*p1 == 'n' && StartsWith(p1, "namespace")))
			externBlock = TRUE; // fix for  extern "C"{ and "namespace{...}"
		else if (*p1 == '_' && StartsWith(p1, "__if_exists"))
			externBlock = TRUE;

		if (Psettings->mUnrealScriptSupport)
		{
			if ((*p1 == 'c' && StartsWith(p1, "cpptext")) || (*p1 == 's' && StartsWith(p1, "structcpptext")))
				externBlock = TRUE;
		}

		if (externBlock)
		{
			if (i) // set type to same as parents so we don't think this is a method
				State(i).m_defType = State(i - 1).m_defType;
			continue; // fix for  extern "C"{ and "namespace{...}"
		}

		if (idx < MAX_SCOPE_LEN &&
		    (ISCSYM(*p1) || strchr(":.~{", *p1) || (*p1 == '!' && ISCSYM(*(p1 + 1)) && (!InLocalScope() || IsXref()))))
		{
			buf[idx++] = DB_SEP_CHR;
			LPCSTR lwScopePos = p1;
			ULONG lIdx = idx;
#define LsIS(s) StartsWith(lwScopePos, s)
			LPCSTR p2;
			for (p2 = p1; idx < MAX_SCOPE_LEN &&
			              (ISCSYM(*p2) || strchr(":.~< \t\r\n", *p2) || (*p2 == '!' && ISCSYM(*(p2 + 1))));
			     p2++)
			{
				// Case 1740: "foo :: bar(){" freaked out the scope
				// Added nxtNonWhite just for this case
				// being very careful not to break scope in other instances.
				LPCSTR nxtNonWhite = p2;
				while (*nxtNonWhite && wt_isspace(*nxtNonWhite))
					nxtNonWhite++;
				if (*nxtNonWhite == ':' && nxtNonWhite[1] == ':')
				{
					p2 = nxtNonWhite;
					buf[idx++] = DB_SEP_CHR;
					p2++;
					while (*p2 && wt_isspace(p2[1]))
						p2++; // eat space on other side of "::"
					lwScopePos = p2 + 1;
					if (i && State(i - 1).m_defType != NAMESPACE && IS_OBJECT_TYPE(State(i - 1).m_defType))
						idx = lIdx; // for class foo{ int foo::i; };
				}
				else if (*p2 == '.')
					buf[idx++] = DB_SEP_CHR;
				else if (State(i).m_defType == NAMESPACE && StartsWith(p2, "inline"))
				{
					// [case: 141698]
					p2 += sizeof("inline") - 1;
					while (*p2 != 0 && *(p2 + 1) != 0 && IsWSorContinuation(*p2) && IsWSorContinuation(*(p2 + 1)))
						p2++;
				}
				else if (*p2 == '.' || *p2 == '~' ||
				         (*p2 == '!' && ISCSYM(*(p2 + 1)) && (!InLocalScope() || IsXref())) || ISCSYM(*p2))
					buf[idx++] = *p2;
				else if (*p2 == '<' && !LsIS("template") && !LsIS("generic") && !LsIS("operator") &&
				         GetLangType() != CS)
				{
					IsTemplateCls it(GetLangType());
					LPCSTR chkStart = p2 + 1;
					const auto here = chkStart - GetBuf();
					auto chkLen = here > GetBufLen() ? GetBufLen() - here : 0;
					if (chkLen <= 0 || chkLen > GetBufLen())
						chkLen = GetSafeReadLen(chkStart, GetBuf(), GetBufLen());
					if (chkLen && it.IsTemplate(chkStart, (int)chkLen, m_ReadAheadInstanceDepth))
					{
						// Fixes case:961 where vector<CHAR>::rereference was different than vector::rereference
						// template<class T> class vector<CHAR>{ class reference{...};};
						// Leave <CHAR> in scope ":std:vector<CHAR>" so it doesn't interfere with ":std:vector"
						WTString targ(p2, it.m_cp + 2);
						targ = ParseLineClass::SingleLineFormat(GetLangType(), targ, FALSE);
						EncodeTemplates(targ);
						if ((targ.GetLength() + idx) < MAX_SCOPE_LEN)
						{
							strcpy(&buf[idx], targ.c_str());
							idx += targ.GetLength();
							p2 += it.m_cp + 1; // discard "<type>"
						}
						else
							break;
					}
				}
				else if (wt_isspace(*p2))
				{
					while (wt_isspace(p2[1]))
						p2++;
					if (StartsWith(&p2[1], "if"))
						idx = lIdx; // else if, make just "if"
					else if (StartsWith(&p2[1], "in"))
						break; // foreach(object o in lst)...
					else if (IS_OBJECT_TYPE(State(i).m_defType) || State(i).m_defType == VAR)
						break; // Fix for UC "class foo extends bar;" OK for C++/C#
					else if ((GetLangType() == Src || GetLangType() == Header) && State(i).m_defType)
						break; // Fix for "int foo HC_PROTO((void));"
					continue;
				}
				else
					break;
			}

			if (LsIS("operator"))
			{
				// any changes made here need to be duplicated in VAParseMPReparse::OnDef()
				WTString opstr = GetOperatorChars(p2);
				idx -= 8; // go back to beginning of "operator"
				if ((opstr.GetLength() + idx) < MAX_SCOPE_LEN)
				{
					strcpy(&buf[idx], opstr.c_str());
					idx += opstr.GetLength();
				}
				else
					break;
			}
			else if (LsIS("this"))
			{
				// C# operator "object this[int i]{ get{...} set{...}}
				if (*p2 == '[' && idx < (MAX_SCOPE_LEN - 1))
				{
					buf[idx++] = '[';
					buf[idx++] = ']';
				}
			}
			else if (LsIS("int") && lwScopePos[3] != '.')
			{
				// [case: 114223]
				// hack workaround to prevent bad parse from causing widespread problems
				if (g_loggingEnabled)
				{
					buf[idx++] = '\0';
					WTString scope(buf[0] ? buf : DB_SEP_STR);
					vCatLog("Parser.Scope", "uncaught bad scope: %s", scope.c_str());
				}
				return NULLSTR;
			}
			else if (LsIS("const"))
			{
				buf[idx] = '\0';
				if (nullptr == strstr(buf, "Z0_UE4_UPARAM_SPECIFIERS"))
				{
					// [case: 114223]
					// hack workaround to prevent bad parse from causing widespread problems
					// but allow "const" enum item defined in our unreal Z0_UE4_UPARAM_SPECIFIERS namespace
					if (g_loggingEnabled)
					{
						WTString scope(buf[0] ? buf : DB_SEP_STR);
						vCatLog("Parser.Scope", "uncaught bad scope: %s", scope.c_str());
					}
					return NULLSTR;
				}
			}
			else if (lwScopePos && *lwScopePos == '_' && IS_OBJECT_TYPE(State(i).m_defType) &&
			         (State(i).m_lastWordPos < State(i).m_begLinePos ||
			          State(i).m_lastWordPos > (State(i).m_begLinePos + 200)))
			{
				// likely unnamed type since m_lastWordPos points to different
				// memory than m_begLinePos (which does not point into buffer
				// when an unnamed identifier has been created by the parse).
				if ((lwScopePos[0] == '_') && (LsIS("_CRT_ALIGN") || LsIS("__attribute__") || LsIS("__declspec") || LsIS("_declspec")))
				{
					WTString txt(State(i).m_begLinePos);
					int pos = txt.Find("unnamed_");
					if (-1 != pos && pos < 200)
					{
						txt.MidInPlace(pos);
						if ((lIdx + txt.GetLength()) < MAX_SCOPE_LEN)
						{
							// [case: 73348]
							// overwrite what was added to buf in this pass starting
							// at lIdx + 1 and adjust idx for new len
							buf[lIdx] = '\0';
							strcat(buf, txt.c_str());
							idx = lIdx + txt.GetLength();
							idx = strlen_u(buf);
						}
					}
				}
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

	_ASSERTE(idx <= MAX_SCOPE_LEN); // we have padding so allow idx == MAX_SCOPE_LEN
	buf[idx++] = '\0';
	WTString scope(buf[0] ? buf : DB_SEP_STR);
	// fix for compatibility with MParse
	scope.ReplaceAll(":elseif-", ":if-");
	return scope;
}

void GetCppStringInfo(WTString buf, bool& isStr, bool& isChar, bool& isRaw, int& charsToSkip,
                      WTString& rawStringTerminator, LiteralType& litType)
{
	_ASSERTE(!isStr && !isChar && !isRaw && !charsToSkip && rawStringTerminator.IsEmpty());
	_ASSERTE(litType == LiteralType::None);

	const int quotePos = buf.find_first_of("'\"");
	if (-1 == quotePos)
		return;

	const char ch = buf[0];
	const char nextCh = ch ? buf[1] : '\0';

	const int rPos = buf.Find(buf[quotePos] == '"' ? "R\"" : "R'");
	if (rPos != -1 && rPos < quotePos && rPos < 3)
	{
		if (!rPos || rPos == 1 || (rPos == 2 && ch == 'u' && nextCh == '8'))
		{
			int parenPos = buf.Find('(');
			if (parenPos != -1 && parenPos > quotePos)
			{
				WTString delim(buf.Mid(quotePos + 1, parenPos - quotePos - 1));
				if (-1 != delim.find_first_of("()\\ \t\r\n"))
				{
					// https://en.wikipedia.org/wiki/C%2B%2B11#New_string_literals
					// delim cannot contain spaces, control characters, '(', ')', or the '\' character
					return;
				}

				switch (rPos)
				{
				case 0:
					litType = LiteralType::UnadornedRaw;
					break;
				case 1:
					switch (ch)
					{
					case 'u':
						litType = LiteralType::Wide16;
						break;
					case 'U':
						litType = LiteralType::Wide32;
						break;
					case 'L':
						litType = LiteralType::Lraw;
						break;
					default:
						_ASSERTE(!"unhandled raw string literal format");
						return;
					}
					break;
				case 2:
					if (ch == 'u' && nextCh == '8')
						litType = LiteralType::Narrow;
					else
					{
						_ASSERTE(!"unhandled raw string literal chars");
						return;
					}
					break;
				default:
					_ASSERTE(!"unhandled raw string literal rPos");
					return;
				}

				isRaw = true;
				if (buf[quotePos] == '"')
					isStr = true;
				else
					isChar = true;

				rawStringTerminator = ")" + std::move(delim) + (isStr ? "\"" : "'");
				charsToSkip = quotePos - 1;
				return;
			}
		}
	}

	if ('L' == ch || 'U' == ch)
	{
		if (nextCh == '"')
			isStr = true;
		else if (nextCh == '\'')
			isChar = true;

		litType = 'L' == ch ? LiteralType::Wide : LiteralType::Wide32;
	}
	else if ('u' == ch)
	{
		if (nextCh == '"')
		{
			isStr = true;
			litType = LiteralType::Wide16;
		}
		else if (nextCh == '\'')
		{
			isChar = true;
			litType = LiteralType::Wide16;
		}
		else if (nextCh == '8')
		{
			const char nextNextCh = buf[2];
			if (nextNextCh == '"')
			{
				isStr = true;
				litType = LiteralType::Narrow;
			}
			else if (nextNextCh == '\'')
			{
				isChar = true;
				litType = LiteralType::Narrow;
			}

			if (isStr || isChar)
				charsToSkip = 1;
		}
	}
	else if ('\'' == ch)
	{
		isChar = true;
		litType = LiteralType::Unadorned;
	}
	else if ('"' == ch)
	{
		isStr = true;
		litType = LiteralType::Unadorned;
	}
}

static ThreadStatic<int> sParseToRecurseCnt;

void VAParse::ParseStructuredBinding()
{
	char stOpen = GetLangType() == Src ? '[' : '(';
	char stClose = GetLangType() == Src ? ']' : ')';
	WTString stKeyword = GetLangType() == Src ? "auto" : "var";

	uint offset = 0;
	bool constSB = false;
	bool refSB = false;
	if (StartsWith(CurPos(), "const", true) && !ISCSYM(m_buf[m_cp - 1]))
	{
		constSB = true;
		auto ptr = CurPos() + sizeof("const") - 1;
		CommentSkipper cs(Src);
		cs.NoStringSkip();
		int counter = 0;
		for (; *ptr != 0; ptr++)
		{
			if (counter++ > 128)
				return;
			auto ch = *ptr;
			if (cs.IsCode2(ch, *(ptr + 1)) && !IsWSorContinuation(ch))
			{
				if (StartsWith(ptr, stKeyword, true))
				{
					offset = uint(ptr - CurPos()) + (uint)stKeyword.GetLength();
					break;
				}
				return;
			}
		}
		if (*ptr == 0)
			return;
	}
	else if (StartsWith(CurPos(), stKeyword, true) && !ISCSYM(m_buf[m_cp - 1]))
		offset = (uint)stKeyword.GetLength();

	if (offset && m_cp > 0)
	{
		CommentSkipper cs(Src);
		cs.NoStringSkip();
		int counter = 0;
		for (auto ptr = CurPos() + offset; *ptr != 0; ptr++)
		{
			if (counter++ > 128)
				return;
			const auto ch = *ptr;
			if (cs.IsCode2(ch, *(ptr + 1)))
			{
				if (ch == '&')
				{
					refSB = true;
					continue;
				}
				if (ch == stOpen)
					break;
				if (!IsWSorContinuation(ch) && ch != '&')
					return;
			}
		}

		InferType infer;
		std::vector<WTString> names;
		std::vector<WTString> types;
		WTString methodScope = MethScope();
		if (methodScope == "" && constSB) // MethScope() returns empty string at "for (|const auto["
			methodScope = Scope(m_deep);
		const bool res = infer.GetTypesAndNamesForStructuredBinding(CurPos(), m_mp, methodScope, names, types, stOpen, stClose, stKeyword);

		if (res)
		{
			// final formatting of types
			for (WTString& type : types)
			{
				type.ReplaceAll(".", "::");
				if (constSB && !StartsWith(type, "const", true))
					type = "const " + type;
				if (refSB && type.GetLength() && type[type.GetLength() - 1] != '&')
					type += "&";
			}

			uint typeIndex = 0;
			for (const WTString& name : names)
			{
				WTString scope = methodScope;
				if (scope != ":")
					scope += ":";
				scope += name;
				scope.ReplaceAll(":auto", "");
				const WTString type = types[typeIndex];
				OnAddSymDef(scope, type + " " + name, VAR, 0);
				if (typeIndex + 1 < types.size())
					typeIndex++;
			}
		}
	}
}

void VAParse::_DoParse()
{
	_ASSERTE(HasSufficientStackSpace());
	DEFTIMERNOTE(VAP__DoParseTimer, NULL);
	// main parsing loop
	for (char c; (c = CurChar()) != 0; IncCP())
	{
		if (IsDone())
			return;
		if (c == '\\')
		{
			const char nextCh = NextChar();
			if (('$' == State().mStringLiteralMainType || 'C' == State().mStringLiteralMainType))
			{
				if ('"' == nextCh || '\'' == nextCh)
				{
					const char nextNextCh = m_buf[m_cp + 2];
					// in an @ verbatim string, ignore: \""
					if ('"' != nextNextCh || '$' == State().mStringLiteralMainType)
					{
						const char prevCh = m_buf[m_cp - 1];
						if (('\\' == prevCh && '$' == State().mStringLiteralMainType) ||
						    ('"' == nextCh && 'C' == State().mStringLiteralMainType))
						{
							// [case: 145778] ignore \\ and \ in following scenarios
							// $"{variable}\\"
							// $@"{variable}\"
						}
						else if ('"' == State().m_inComment || '\'' == State().m_inComment ||
						         '"' == State().m_inSubComment || '\'' == State().m_inSubComment)
						{
							// [case: 141701]
							OnChar();
							IncCP();
							OnChar();
							continue;
						}
					}
				}
			}
			// don't process \'s in @"c:\"
			else if (nextCh && '@' != State().mStringLiteralMainType)
			{
				if ('#' == State().m_inComment && '*' == State().m_inSubComment)
				{
					// [case: 60826] don't escape * within a block comment in a macro definition
				}
				// [case: 46838]
				else if ('#' == State().m_inComment || '"' == State().m_inComment || '\'' == State().m_inComment ||
				         '"' == State().m_inSubComment || '\'' == State().m_inSubComment ||
				         '/' == State().m_inComment || '/' == State().m_inSubComment)
				{
					IncCP();
					if (m_buf[m_cp] == '\r' && m_buf[m_cp + 1] == '\n')
						IncCP();
					if (m_buf[m_cp] == '\n' || m_buf[m_cp] == '\r')
						OnNextLine();
					continue;
				}
			}
		}

		if (InComment())
		{
			_ASSERTE(!(State().m_inComment == 'L' || State().m_inComment == '@' || State().m_inComment == 'S' ||
			           State().m_inComment == '$'));
			OnChar();

			if (m_buf[m_cp] == '\r' && m_buf[m_cp + 1] == '\n')
				IncCP();
			if (m_buf[m_cp] == '\n' || m_buf[m_cp] == '\r')
				c = '\n';

			//
			// check for c-style comments within preprocessor statements.  If
			// a comment spans multiple lines, then the EOL's don't terminate the
			// preproc statement.
			//
			if (State().m_inComment == '#')
			{
				if (!State().m_inSubComment)
				{
					if (CurChar() == '/' && NextChar() == '*')
						State().m_inSubComment = '*';
					else if (CurChar() == '/' && NextChar() == '/')
						State().m_inSubComment = '\n';
					else if (CurChar() == '\'' || CurChar() == '"')
						State().m_inSubComment = CurChar();
					if (State().m_inSubComment && CurChar() == '/')
						IncCP(); // Eat second / or *, Fixes /*/ comment /*/
				}
				else
				{
					if (State().m_inSubComment == CurChar())
					{
						if (CurChar() != '*' || NextChar() == '/')
							State().m_inSubComment = 0;
					}
				}
			}
			else if ('$' == State().mStringLiteralMainType || 'C' == State().mStringLiteralMainType)
			{
				_ASSERTE('\0' == State().m_inSubComment);
				if ('{' == c)
				{
					// [case: 96559] [case: 98335]
					if (NextChar() == '{')
						IncCP(); // escaped { -- eat it
					else
					{
						// entering interpolated expression
						IncDeep();
					}
				}
			}
			else if (State().m_inComment == '[')
			{
				// [case: 112204] c++ [[attributes]]
				// treated like block comments but support nesting
				if (c == ']' && NextChar() == ']')
				{
					// end of c++ [[attribute]]
					IncCP(); // eat the ]
					OnChar();
					OnComment('\0');
					DecDeep();

					m_InSym = FALSE;
					State().m_lastChar = c;
					continue;
				}
				else if (c == '[' && NextChar() == '[')
				{
					// start of nested c++ [[attribute([[inner]])]]
					IncDeep();
					OnComment('[', 1);
					IncCP();
					OnChar();

					m_InSym = FALSE;
					State().m_lastChar = c;
					continue;
				}
			}

			if (c == State().m_inComment || (State().m_inComment == '#' && c == '\n'))
			{
				if (State().m_inComment == '#' && c == '\n')
				{
					if (State().m_inSubComment == '*')
					{
						// case 3384
						// EOL within c-style comment doesn't end preproc statement
						OnNextLine();
						continue;
					}
				}

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

				if ('R' == State().mStringLiteralMainType)
				{
					_ASSERTE(c == '"' || c == '\'');
					// check for termination of raw string
					bool terminatedRawString = false;
					const WTString rawStringTerminator(State().mRawStringTerminator);
					if (rawStringTerminator.IsEmpty())
					{
						_ASSERTE(!"in raw string but rawStringTerminator is empty!");
					}
					else if (m_cp > rawStringTerminator.GetLength() + 1)
					{
						const char* p = &m_buf[m_cp - rawStringTerminator.GetLength() + 1];
						if (*p && *p == rawStringTerminator[0])
						{
							if (StartsWith(p, rawStringTerminator, FALSE))
							{
								if ('"' == c && NextChar() == 's')
								{
									// [case: 95006][case: 65734]
									IncCP();
									OnChar();
								}

								OnComment('\0');
								terminatedRawString = true;
							}
						}
					}

					if (!terminatedRawString)
					{
						// wasn't terminated -- literal quote in raw string
						continue;
					}
				}
				else if (('@' == State().mStringLiteralMainType || '$' == State().mStringLiteralMainType ||
				          'C' == State().mStringLiteralMainType) &&
				         (('\'' == c || '"' == c) && NextChar() == c))
				{
					// [case: 72288]
					// support for "" escaping in @ string literals
					IncCP();
				}
				else if (c == '\n' && GetLangType() != CS) // [case: 116079] C# does not do comment line continuation
				{
					if (m_cp > 2 && (m_buf[m_cp - 1] == '\\' || (m_buf[m_cp - 1] == '\r' && m_buf[m_cp - 2] == '\\')))
					{
						// [case:85222] line continuation in single-line comment
						OnComment('\n');
					}
					else
						OnComment('\0');
				}
				else
				{
					if ('"' == c && NextChar() == 's')
					{
						// [case: 95006][case: 65734]
						IncCP();
						OnChar();
					}

					OnComment('\0');
				}
			}

			if (c == '\n')
				OnNextLine();
		}
		else if (m_deep && '}' == c && State(m_deep - 1).m_inComment != NULL &&
		         ('$' == State(m_deep - 1).mStringLiteralMainType || 'C' == State(m_deep - 1).mStringLiteralMainType))
		{
			// [case: 98335]
			// nested interpolated string -- semi-comment state
			OnChar();

			if ('}' == NextChar())
			{
				IncCP(); // escaped } -- eat it
				OnChar();
			}
			else
			{
				// close expression
				DecDeep();
			}

			m_InSym = FALSE;
			State().m_lastChar = c;
			continue;
		}
		else
		{
			// Look for comment/string/directives
			switch (c)
			{
			case '@':
			case 'S':
			case '$': {
				char nextCh = NextChar();
				if (nextCh == '"' || nextCh == '\'')
				{
					// treat as start of string
					OnComment(nextCh, 1);
					IncCP();
					State().mStringLiteralMainType = c;
					continue; // skip inSym = FALSE;  below
				}
				else if ('$' == c && '@' == nextCh)
				{
					nextCh = m_buf[m_cp + 2];
					if (nextCh == '"' || nextCh == '\'')
					{
						// treat as start of string
						OnComment(nextCh, 2);
						IncCP();
						IncCP();
						State().mStringLiteralMainType = 'C';
						continue; // skip inSym = FALSE;  below
					}
				}
			}
			break;
			case '\'': {
				if (IsAtDigitSeparator())
				{
					// [case: 86379]
					// not starting a char literal

					// [case: 143035]
					// handle no space between case and value, e.g. case'1'
					char ch1 = PrevChar();
					if (ch1 == 'e') // do a quick char comparison as first filter
					{
						// now do a more time intensive task to check if it is "case" string
						if (GetCp() > 4)
						{
							LPCSTR rawCaseStr = CurPos() - 4;
							const WTString caseStr(GetCStr(rawCaseStr));
							if (0 == caseStr.Find("case"))
							{
								OnComment(c);
								continue; // skip inSym = FALSE;  below
							}
						}
					}
				}
				else
				{
					OnComment(c);
					continue; // skip inSym = FALSE;  below
				}
			}
			break;
			case '"':
				OnComment(c);
				continue; // skip inSym = FALSE;  below
			case '/':
				if (NextChar() == '*')
					OnComment('*');
				else if (NextChar() == '/')
					OnComment('\n');
				if (InComment())
				{
					IncCP();  // eat NextChar so we don't think /*/ is a /**/
					continue; // skip inSym = FALSE;  below
				}
				else if (GetLangType() == JS)
				{
					// handle JS regular expressions s = /.../; case=21411
					// State().m_lastChar is unpredictable here, go back and see what the last char was.
					//  Not for foo/bar, foo()/bar, or foo[bar]/bar
					int i = m_cp;
					for (; i && m_buf[i - 1] == ' ' || m_buf[i - 1] == '\t'; i--)
						;
					if (!ISCSYM(m_buf[i - 1]) && !strchr("])} ", m_buf[i - 1]))
						OnComment('/');
				}
				break;
			case '#':
				if (InParen(m_deep) && (GetLangType() == JS || GetLangType() == VBS || GetLangType() == PHP))
					continue; // No # processing in JS  str.replace( /(#.*$)/, ''); // Causes closing ')' to be ignored.
				OnComment('#');
				OnDirective();
				continue; // skip inSym = FALSE;  below
			case '[': {
				char nextCh = NextChar();
				if (nextCh == '[' && IsCFile(GetLangType()))
				{
					// [case: 112204]
					// start of c++ [[attribute]]
					// treated like block comments but support nesting
					IncDeep();
					OnComment(nextCh, 1);
					IncCP();
					OnChar();

					m_InSym = FALSE;
					State().m_lastChar = c;
					continue;
				}
			}
			break;
			}

			// [case: 65734] c++ raw and unicode strings
			bool tryCppLiteral = false;
			if ('L' == c)
			{
				const char nextCh = NextChar();
				if (nextCh == '"' || nextCh == '\'' || nextCh == 'R')
					tryCppLiteral = true;
			}
			else if ('u' == c)
			{
				const char nextCh = NextChar();
				if (nextCh == '"' || nextCh == '\'' || nextCh == 'R' || nextCh == '8')
					tryCppLiteral = true;
			}
			else if ('U' == c)
			{
				const char nextCh = NextChar();
				if (nextCh == '"' || nextCh == '\'' || nextCh == 'R')
					tryCppLiteral = true;
			}
			else if ('R' == c)
			{
				const char nextCh = NextChar();
				if (nextCh == '"' || nextCh == '\'')
					tryCppLiteral = true;
			}

			if (tryCppLiteral)
			{
				bool isRaw = false;
				bool isStr = false;
				bool isChar = false;
				int charsToSkip = 0;
				LiteralType litType = LiteralType::None;

				const int lenLeft = mBufLen - m_cp;
				int bufLen = lenLeft > 1024 ? 1024 : lenLeft;
				if (lenLeft < 0)
					bufLen = 5;
				WTString buf(&m_buf[m_cp], bufLen);
				WTString rawStringTerminator;
				::GetCppStringInfo(buf, isStr, isChar, isRaw, charsToSkip, rawStringTerminator, litType);
				if (isStr || isChar)
				{
					if (rawStringTerminator.IsEmpty())
					{
						_ASSERTE(!isRaw);
					}
					else
					{
						_ASSERTE(isRaw);
						State().mRawStringTerminator = rawStringTerminator;
					}
					charsToSkip++;

					State().mStringLiteralMainType = isRaw ? 'R' : c;
					OnComment(isChar ? '\'' : '"', charsToSkip);

					for (int cnt = 0; cnt < charsToSkip; ++cnt)
						IncCP();
					continue;
				}
			}

			// process code
			OnChar();

			switch (c)
			{
				// WhiteSpace
			case '\r':
				// catch \r's only
				if (NextChar() == '\n')
					IncCP();
				// what about \r\r\n?
			case '\n':
				OnNextLine();
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
				if (!(m_parseFlags & PF_TEMPLATECHECK))
					break;
				{
					// scan ahead to see if <>'s match
					IsTemplateCls it(GetLangType());
					const bool doubleLeft = NextChar() == '<';
					LPCSTR chkStart = CurPos() + 1;
					bool didLenOverride = false;
					int chkLen = GetCp() > GetBufLen() ? GetBufLen() - (GetCp() + 1) : 0;
					if (chkLen <= 0 || chkLen > GetBufLen())
					{
						chkLen = GetSafeReadLen(chkStart, GetBuf(), GetBufLen());
						didLenOverride = true;
					}
					const bool isTemplate = chkLen && it.IsTemplate(chkStart, chkLen, m_ReadAheadInstanceDepth);
					if (doubleLeft || !isTemplate)
					{
						if (doubleLeft)
						{
							IncCP();  // eat the other '<'
							OnChar(); // [case: 7383]
						}
						if (m_parseFlags & PF_TEMPLATECHECK_BAIL)
							return;
						State().m_parseState = VPS_ASSIGNMENT;

						if (!doubleLeft)
						{
							if (InLocalScope() && VAR == State().m_defType && JS != GetLangType() && m_deep &&
							    State(m_deep - 1).m_lastChar == '(')
							{
								if (StartsWith(State(m_deep - 1).m_lastScopePos, "if") ||
								    StartsWith(State(m_deep - 1).m_lastScopePos, "else") ||
								    StartsWith(State(m_deep - 1).m_lastScopePos, "while"))
								{
									// [case: 92734]
									State().m_defType = UNDEF;
									State().m_lastScopePos = CurPos();
								}
							}
						}
						break;
					}
					if (m_parseFlags & PF_TEMPLATECHECK_BAIL)
					{
						// Just checking for IsTemplate,
						// Skip past embedded <<<<>>>>'s since we are only scanning forward for templates
						m_cp += it.m_cp + 1;
						if (m_cp > mBufLen)
						{
#ifdef SEAN
							// if this assert fires, the next statement will prevent invalid access, but still
							// need to find out why m_cp read beyond end of mBufLen -- macro where mBufLen was
							// incorrect?
							_ASSERTE(didLenOverride || m_cp == (mBufLen + 1));
#endif
							m_cp = mBufLen;
						}
						break;
					}
					// [case: 114966] provide the cp of the '>' ending the template argument list
					SpecialTemplateHandler(isTemplate ? m_cp + it.m_cp + 1 : 0);
				}
			case '(':
			case '[':
			case '{': {
				if (c == '(' && State().m_parseState != VPS_ASSIGNMENT && !IsDef(m_deep) && !InLocalScope() && m_deep)
				{
					const WTString t = GetCStr(State().m_lastScopePos);
					if (t.GetLength())
					{
						if (t[0] == '~' || (t[0] == '!' && ISCSYM(t[1])) || Scope().contains(t))
						{
							State().m_defType = FUNC;
							State().m_defAttr |= V_CONSTRUCTOR; // constructor
						}
					}
				}

				if (c == '[' && !m_inIFDEFComment)
				{
					if (IsCFile(GetLangType()))
					{
						if (::IsLambda(&m_buf[GetCp()], m_buf))
						{
							// case: 57605
							// ignore contents of capture clause
							State().m_defType = Lambda_Type;
							OnDef();
							State().m_parseState = VPS_ASSIGNMENT;
							State().m_lastWordPos = CurPos();
						}
					}

					if (IsDef(m_deep) && State().m_begLinePos != State().m_lastScopePos &&
					    State().m_defType != Lambda_Type && GetLangType() != CS)
					{
						// added begLine test above to for defs like "typedef [public] basic_string<...> string;"
						// Not in  c#, or "public string[] strs;" will add "string" as type "public"
						// NOTE: this causes "int * foo = new int[2]" to be defined twice.
						OnDef();
					}
				}

				if (State().m_parseState == VPS_NONE && !m_inIFDEFComment)
				{
					// *foo = 'x'; // set VPS_ASSIGNMENT
					ClearLineState(CurPos());
					State().m_parseState = VPS_ASSIGNMENT;
				}

				if (State().m_parseState != VPS_ASSIGNMENT && !InLocalScope(m_deep))
				{
					const uint kDefType = State().m_defType;
					if (c == '(')
					{
						if (kDefType == CLASS || kDefType == STRUCT || kDefType == C_INTERFACE || kDefType == C_ENUM)
						{
							// struct __declspec(...) foo{}; ?
							ParseToCls ptc(GetLangType());
							LPCSTR lw = State().m_lastWordPos;
							const WTString lastWordStr(GetCStr(lw));
							if (lastWordStr[0] == '_' &&
							    (0 == lastWordStr.Find("_CRT_ALIGN") || 0 == lastWordStr.Find("__attribute__") ||
							     0 == lastWordStr.Find("__declspec") || 0 == lastWordStr.Find("_declspec")))
							{
								// [case: 73348]
								// junk in type/class/struct defs that we can ignore
							}
							else
							{
								const auto here = lw - GetBuf();
								auto chkLen = here > GetBufLen() ? GetBufLen() - here : 0;
								if (chkLen <= 0 || chkLen > GetBufLen())
									chkLen = GetSafeReadLen(lw, GetBuf(), GetBufLen());
								if (ptc.ParseTo(lw, (int)chkLen, ";{") && ptc.State().m_lastScopePos == lw)
									State().m_defType = FUNC; // but not "extern class RClass& GetCls();"
							}
						}
						else if (kDefType != EVENT && kDefType != Lambda_Type && kDefType != DELEGATE)
						{
							// simple check to see if we are defining a function or initializing a variable
							// int i(10); or int foo(int i)
							LPCSTR p = CurPos() + 1;
							while (*p && wt_isspace(*p))
								p++;
							if (WTStrchr("'\"&*1234567890", *p))
								State().m_defType = VAR;
							else
								State().m_defType = FUNC;

							if ((UNDEF == kDefType || TYPE == kDefType || FUNC == kDefType) && IsCFile(GetLangType()))
							{
								LPCSTR lw = State().m_lastWordPos;
								if (lw && *lw == 'd')
								{
									if (::StartsWith(lw, "decltype"))
									{
										// [case: 93387]
										// decltype(...) foo;
										State().m_defType = kDefType;
									}
								}
							}
						}
					}
					else if (c == '{' && (!kDefType || kDefType == VAR))
					{
						if (m_parseFlags && m_deep && !LwIS("extern"))
						{
							if (!kDefType || FwIS("property") || !IsCFile(GetFType()))
								State().m_defType = PROPERTY;
							else
								; // [case: 79074]
						}
						else
						{
							// extern "C" {}? treat like a class.
							// Since we aren't processing keywords like "class", assume it is a CLASS
							State().m_defType = CLASS;
						}
					}
				}

				if (c == '(' && !m_inMacro && m_deep > 1 && Lambda_Type == State().m_defType &&
				    VPS_ASSIGNMENT == State().m_parseState && 0 == State(m_deep - 1).m_ParserStateFlags &&
				    !m_inIFDEFComment && UNDEF == State(m_deep - 1).m_defType)
				{
					if (m_deep && State().m_lastWordPos && '[' == State().m_lastWordPos[0] &&
					    State(m_deep - 1).m_lastChar == '(')
					{
						// [case: 57605]
						// hack for chained async tasks defined via lambdas.
						// lambda params for independent lambdas were being put in
						// same scope for all tasks/lambdas.
						// see ast 57605 for sample code.
						State(m_deep - 1).m_conditionalBlocCount++;
					}
				}

				if ((c == '(' || c == '[') && State().m_lastChar == ')' && State().m_parseState != VPS_ASSIGNMENT)
				{
					bool changeDefType = true;
					if (c == '[')
					{
						// [case: 92732] static_cast<LPTSTR>(pData->data())[nLen] = '\0';
						const WTString cw(GetCStr(State().m_lastScopePos));
						if (-1 != cw.FindNoCase("_cast"))
							changeDefType = false;
					}
					else
					{
						// #parseStateDeepReadAhead - unless explicitly init in ctor,
						// m_lastScopePos can have garbage in it
						LPCSTR p = State(m_deep + 1).m_lastScopePos;
						if (p && *p == '"')
						{
							// [case: 141990]
							// don't change lastScopePos if we just popped out of a string literal
							changeDefType = false;
						}
					}

					if (changeDefType)
					{
						// int (foo)(int i = 123); function pointers
						// int (*foo)[128]; array pointers
						State().m_defType = (c == '[' || InLocalScope()) ? VAR : FUNC;
						// #parseStateDeepReadAhead - unless explicitly init in ctor,
						// m_lastScopePos can have garbage in it
						LPCSTR p = State(m_deep + 1).m_lastScopePos;
						if (p)
						{
							while (*p && !ISCSYM(*p) && *p != ')') // eat off * in "int (*foo)(int arg);"
								p++;
							if (StartsWith(p, "constexpr"))
								p += 9;
							if (StartsWith(p, "consteval"))
								p += 9;
							if (StartsWith(p, "constinit"))
								p += 9;
							if (StartsWith(p, "_CONSTEXPR17"))
								p += 12;
							else if (StartsWith(p, "_CONSTEXPR20_CONTAINER"))
								p += 22;
							else if (StartsWith(p, "_CONSTEXPR20"))
								p += 12;
							if (StartsWith(p, "const"))
								p += 5; // [case: 51050] void (* const foo)(int arg);
							while (*p && !ISCSYM(*p) && *p != ')')
								p++;
							State().m_lastScopePos = p;
						}
					}
				}

				if (c == '{' && !m_inIFDEFComment)
				{
					if (m_deep && State(m_deep - 1).m_lastChar == ':')
					{
						// recover scope for constructors:	 "foo(int i): m_bar(i){"
						// but not for c++11 init lists:	 "foo(int i): m_bar{i}{"
						bool doCtorDepthRecovery = true;
						if (m_deep && IsDef(m_deep - 1) && State(m_deep - 1).m_defType == FUNC)
						{
							auto methscope = WTString(Scope(m_deep));
							auto scopeSym = WTString(StrGetSym(methscope));
							auto baseSymOfSymScope = WTString(StrGetSym(StrGetSymScope(methscope)));
							auto pos1 = scopeSym.ReverseFind('-');
							auto pos2 = baseSymOfSymScope.ReverseFind('-');
							if (-1 != pos1 && -1 == pos2)
								scopeSym = scopeSym.Left(pos1);
							if (scopeSym == baseSymOfSymScope)
							{
								ParseToEndBlock eblk(FileType(), m_buf, GetBufLen(), GetCp());
								int sp = eblk.GetFinalStartPos();
								if (sp != (int)GetCp())
								{
									// [case: 79737]
									doCtorDepthRecovery = false;
								}
							}
						}

						if (doCtorDepthRecovery)
							DecDeep();
					}

					if (IsDef(m_deep) && Lambda_Type != State().m_defType &&
					    (VAR != State().m_defType ||
					     !(Is_C_CS_File(GetLangType()))) // didn't want to mess with js unit test failures
					)
					{
						// this is the V_IMPLEMENTATION "{...}" def
						OnDef();
					}
				}

				State().m_lastChar = c;
				IncDeep();
				State(m_deep + 1).m_defType = UNDEF;
				if (m_deep && State(m_deep - 1).m_defType == CLASS)
				{
					// [case: 2876] fix for extern "C" visibility
					// We set extern "C"{ to CLASS so InLocalScope is not set and all defs appear
					// in global scope.  But don't mark as private.
					if (!State(m_deep - 1).m_lwData)
					{
						if (!StartsWith(State(m_deep - 1).m_lastScopePos, "extern"))
							State().m_privilegeAttr = V_PRIVATE;
					}
					else if (State(m_deep - 1).m_lwData->SymScope() != ":extern")
						State().m_privilegeAttr = V_PRIVATE;
				}
				m_InSym = FALSE;
			}
				continue;
				// Close brackets })]>
			case '>':
				if (!m_deep)
					break; // operator>(); Not closing '>', break to prevent DebugMessage below.
				if (m_deep && State(m_deep - 1).m_lastChar != '<')
				{
					if (State().m_parseState != VPS_ASSIGNMENT)
						State().m_defType = UNDEF; // "foo*bar > 3" bar is not a pointer to foo
					State().m_parseState = VPS_ASSIGNMENT;

					if (PrevChar() == '=' && GetLangType() == CS)
					{
						// C# lambda or expression bodied method (class or local [case: 118894])
						if (!InLocalScope(m_deep) || State(m_deep - 1).m_defType == PROPERTY ||
						    (InLocalScope() && FUNC == State().m_defType))
						{
							// class member/method declaration or property getter/setter
							const uint kDefType = State().m_defType;
							if (!kDefType || kDefType == VAR)
							{
								if (m_parseFlags)
								{
									// [case: 116299]
									State().m_defType = PROPERTY;
								}
							}

							if (IsDef(m_deep) && !m_inIFDEFComment)
							{
								// output def before handling expression body
								OnDef();
								// C# lambda expression bodied method, property
								// push depth for expression body after "=>"
								IncDeep();
								// pop depth on ';'
								State().AddParserStateFlags(VPSF_DEC_DEEP_ON_EOS);

								if (PROPERTY == State().m_defType)
								{
									// TODO: consider AddSymDef for implicit variable named value of same type as
									// property
								}
							}
						}
					}

					if (m_cp && m_buf[m_cp - 1] == '-') // pretend a foo->bar is the same as a foo.bar
						c = '.';
					State().m_lastWordPos = CurPos(); // points to beginning of last sym
					break;                            // if( a < b || a > b)
				}
			case ')':
			case ']':
			case '}': {
				CHAR mc = strchr("()[]{}<>", c)[-1];
				if (!m_deep || mc != State(m_deep - 1).m_lastChar)
				{
					if (Is_Tag_Based(GetFType()) && m_deep && State(m_deep - 1).m_begLinePos[0] == '<')
						continue; // <% } %>  // don't go back looking beyond script case=21511
					if (!m_inIFDEFComment)
					{
						DebugMessage("Brace Mismatch");
						// Do not underline Case: 12553
						// 							if(m_deep)
						// 								OnError(State(m_deep).m_begBlockPos-1);
						// 							OnError(CurPos());
						// recover from mismatch
						while (m_deep && mc != State(m_deep - 1).m_lastChar)
							DecDeep();
					}
				}
			}
				if (c == '}' && IsDef(m_deep) && !m_inIFDEFComment)
				{
					if (State().m_defType == VAR && Is_C_CS_File(GetLangType()))
						DebugMessage("';' expected");
					else
						OnDef();
				}

				if (IsDef(m_deep) && !m_inIFDEFComment)
				{
					if (c == '>' && !InParen(m_deep))
						OnDef();
					else if (c == ')')
					{
						// #functionParams
						// function params and method params are output via this block
						if (m_deep && InLocalScope(m_deep - 1) && State().m_parseState == VPS_BEGLINE &&
						    State(m_deep - 1).m_defType == VAR && GetLangType() == CS)
						{
							// [case: 118894]
							// possible param in potentially locally defined function
							// read ahead to confirm is local function definition
							ReadToCls rtc(CS);
							WTString tmp(rtc.ReadTo(CurPos(), GetLenOfCp(), "{;", 50));
							if (tmp.IsEmpty() && rtc.CurChar() == '{')
							{
								// normal bodied local function definition
								OnDef();
								State(m_deep - 1).m_defType = FUNC;
							}
							else if (tmp.GetLength() > 2 && tmp[0] == '=' && tmp[1] == '>')
							{
								// expression bodied local function definition
								OnDef();
								State(m_deep - 1).m_defType = FUNC;
							}
						}
						else if (m_deep && InLocalScope(m_deep - 1) && State().m_parseState != VPS_ASSIGNMENT)
						{
							// if(foo & bar)  is not defining bar,
							// but if(foo & bar = 123), bar is defined
							if (StartsWith(State(m_deep - 1).m_lastWordPos, "catch") ||
							    Lambda_Type == State(m_deep - 1).m_defType)
							{
								// definition of e in "catch(exception *e){}"
								// definition of p1 in "[] (int p1) -> int { return p1; }"  (case=57605)
								OnDef();
							}
						}
						else if (m_deep && State().m_parseState != VPS_ASSIGNMENT &&
						         (FUNC == State(m_deep - 1).m_defType || GOTODEF == State(m_deep - 1).m_defType ||
						          TYPE == State(m_deep - 1).m_defType) &&
						         (StartsWith(State(m_deep - 1).m_lastWordPos, "decltype") ||
						          StartsWith(State(m_deep - 1).m_lastWordPos, "__attribute__")))
						{
							// [case: 20625] [case: 93387]
							// do not OnDef for anything in the decltype expression
							// auto Func(int x, int y) -> decltype(x * y) { }
							// y is not an "x *"
							// - or -
							// typedef decltype(foo(bar)) baz;
						}
						else
						{
							OnDef();
						}
					}
				}

				DecDeep();

				if (c == ')')
				{
					// [case: 113209] [case: 97964] [case: 78814] reopen scope after for's or if's parens with naked
					// scopes e.g. "if (...)"
					if (m_deep && State(m_deep).HasParserStateFlags(VPSF_KEEPSCOPE_FOR | VPSF_KEEPSCOPE_IF))
					{
						if (IsNakedScope(CurPos()))
						{
							OnForIf_CreateScope(State(m_deep).HasParserStateFlags(VPSF_KEEPSCOPE_FOR)
							                        ? NewScopeKeyword::FOR
							                        : NewScopeKeyword::IF);
							State().AddParserStateFlags(
							    VPSF_NAKEDSCOPE_FOR_IF); // we add additional info: we're dealing with a naked scope
						}
					}

					if (m_deep && InLocalScope(m_deep) && VPS_BEGLINE == State().m_parseState)
					{
						if (VAR == State().m_defType)
						{
							// [case: 61448] workaround for ExpandMacroCode / State().m_begLinePos problem
							// ExpandMacroCode can do strange things to State().m_begLinePos
							// if State().m_begLinePos doesn't even point into the buf, can't rely on it; skip.
							const auto len =
							    (State().m_begLinePos >= GetBuf() && State().m_begLinePos <= GetBuf() + GetBufLen() &&
							     CurPos() >= GetBuf() && CurPos() <= GetBuf() + GetBufLen())
							        ? CurPos() - State().m_begLinePos
							        : -1;
							if (len > 0 && len < 1000)
							{
								WTString line(State().m_begLinePos, (int)len);
								const int startPos = line.Find('(');
								if (-1 != startPos)
								{
									bool looksLikeFuncPtr;
									const int starPos = line.Find('*', startPos);
									if (-1 == starPos)
										looksLikeFuncPtr = false;
									else
									{
										const int closePos = line.Find(')', starPos);
										const int nextStartPos = line.Find('(', startPos + 1);
										// looks like: void (*Foo)(arg) ?
										looksLikeFuncPtr = closePos != -1 && nextStartPos != -1 && starPos < closePos &&
										                   closePos < nextStartPos;
									}

									line = line.Mid(startPos + 1);
									line.Trim();
									if (line.IsEmpty())
									{
										// [case: 25957] empty parens: "void Foo()" local declaration of function
										// http://stackoverflow.com/questions/4702604/c-error-c2228-identifier-not-recognized-as-class#4702621
										// http://en.wikipedia.org/wiki/Most_vexing_parse
										State().m_defType = FUNC;
									}
									else if (StartsWith(State().m_begLinePos, "typedef") && looksLikeFuncPtr)
									{
										// typedef of function pointer
										State().m_defType = FUNC;
									}
									else
									{
										// [case: 25957] non-empty parens
										// "Bar Foo(bool, int)" local declaration of function
										// or "Bar Foo(1);" local var
										token2 args(line);
										WTString curArg;
										while (args.more())
										{
											args.read(',', curArg);
											if (curArg.IsEmpty())
												break;

											const int pos = curArg.Find(' ');
											if (-1 != pos)
												curArg = curArg.Left(pos);

											if (::IsReservedWord(curArg, GetLangType()))
											{
												// [case: 59478] watch out for true and false
												// [case: 60805] function declaration or variable declaration?
												if (curArg != "new" && curArg != "gcnew" && curArg != "false" &&
												    -1 == line.Find("ref new") && curArg != "true" &&
												    curArg != "sizeof" && curArg != "this" && curArg != "nullptr")
												{
													// [case: 135859] void (*Foo)(int) is a function pointer variable
													if (!looksLikeFuncPtr)
														State().m_defType = FUNC;
												}
												break;
											}

											if (m_mp)
											{
												DType* dt = m_mp->FindAnySym(curArg);
												if (dt)
												{
													if (::IS_OBJECT_TYPE(dt->type()))
													{
														State().m_defType = FUNC;
														break;
													}
												}
											}

											// arg is unknown, continue
											// could be another local var or could be a local type
										}
									}
								}
							}
						}
					}
				}
				else if (c == ']')
				{
					if (State().m_begLinePos[0] == '[' /*!State().m_lwData*/)
					{
						// cause of case=7606 ?
						// "[c# attribute]" // pretend there is a ; here "foo(){};"
						ULONG defType = State().m_defType;
						if (Lambda_Type != defType)
						{
							if (defType == 0)
								OnAttribute();

							ClearLineState(CurPos());
						}
						m_InSym = FALSE;
						continue;
					}
				}
				else if (c == '}')
				{
					// [case: 97964] [case: 113209] [case: 78814] if we're in naked scope(s), then closing them
					if (m_deep >= 1)
					{
						for (ULONG lastDeep = m_deep, forIfCnt = 0;
						     m_deep >= 1 && State().HasParserStateFlags(VPSF_NAKEDSCOPE_FOR_IF); ++forIfCnt)
						{
							if (State(m_deep - 1).HasParserStateFlags(VPSF_KEEPSCOPE_IF))
							{
								if (OnForIf_CloseScope(NewScopeKeyword::IF))
									State().m_conditionalBlocCount++;
								else
									break;
							}
							else if (State(m_deep - 1).HasParserStateFlags(VPSF_KEEPSCOPE_FOR))
							{
								if (OnForIf_CloseScope(NewScopeKeyword::FOR))
									State().m_conditionalBlocCount++;
								else
									break;
							}
							else
								break; // avoid infinite loop when the flag states are unexpected

							if (lastDeep == m_deep)
							{
								// [case: 141817]
								// there are conditions in which DecDeep doesn't change m_deep, but this loop assumes
								// always occurs
								vLog("ERROR: forif break inf loop (A1)");
								break;
							}

							lastDeep = m_deep;
							if (forIfCnt > STATE_COUNT)
							{
								// [case: 141817]
								// last chance to catch infinite loop
								vLog("ERROR: forif break inf loop (A2)");
								break;
							}
						}
					}

					if (GetLangType() == JS) // Don't add on } in JS or it will be added twice.
						ClearLineState(CurPos());

					if (!m_inMacro && !m_inIFDEFComment)
						State().m_conditionalBlocCount++;

					// foo(){...} no semicolon needed
					if (State().m_defType == CLASS && LwIS("extern")) // extern "C" {}
					{
						// We set extern "C"{ to CLASS so InLocalScope is not set and all defs appear in global scope.
						// Now, however, a ; is not expected after the }, so clear and continue.
						ClearLineState(CurPos());
						m_InSym = FALSE;
						continue;
					}

					if (State().m_defType == C_INTERFACE)
					{
						// [case: 73222]
						// "interface Iface { }" // pretend there is a ; here "interface Iface { };"
						ClearLineState(CurPos());
						m_InSym = FALSE;
						continue;
					}
					else if (GetLangType() != CS && (State().m_defType == CLASS || State().m_defType == C_ENUM ||
					                                 State().m_defType == STRUCT)) // class foo{} bar;
					{
						if (!StartsWith(State().m_begLinePos, "typedef"))
						{
							State().m_parseState = VPS_BEGLINE;
							State().m_lastScopePos = CurPos();
							State().m_defType = VAR; // instance of class or typedef
						}
					}
					else if (State().m_defType != VAR) // not int i[]={1, 2}, j;
					{
						if (m_deep && '(' == State(m_deep - 1).m_lastChar)
						{
							// [case: 79737]
							// foo() : mMem({1}) { }
						}
						else
						{
							// "foo(){}" // pretend there is a ; here "foo(){};"
							ClearLineState(CurPos());
						}
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
				break;

				// If def call OnDef on ";,)="
				// Assignment, could be definition
			case '=':
				if (NextChar() == '=')
				{
					// ==, not a def
					if (State().m_parseState != VPS_ASSIGNMENT)
						State().m_defType = UNDEF; // "foo*bar == 3" bar is not a pointer to foo
					IncCP();                       // eat the other '='
					OnChar();
				}

				State().m_parseState = VPS_ASSIGNMENT; // assignment

				if (State().m_lwData)
				{
					auto savedDt = State().m_lwData;
					State().m_lwData.reset();

					if (IsCFile(GetLangType()) && m_Scoping)
					{
						ParseToCls ptc(GetLangType());
						// prevent stack overflow by using PF_NONE instead of default PF_TEMPLATECHECK
						LPCSTR subBuf = CurPos() + 1;
						const auto subLen = GetBufLen() - (CurPos() - GetBuf()) - 1;
						if (subLen > 0)
						{
							// [case: 141917]
							if (sParseToRecurseCnt() < 6)
							{
								struct TmpRecInc
								{
									TmpRecInc()
									{
										sParseToRecurseCnt()++;
									}
									~TmpRecInc()
									{
										sParseToRecurseCnt()--;
									}
								};

								TmpRecInc ti;
								ptc.ParseTo(subBuf, (int)subLen, "(){};=!-+", 1024, PF_NONE);
								if (ptc.CurChar() == '{')
								{
									// [case: 118748] C99/C++20 designated initializer support
									State().m_lwData = savedDt;
								}
							}
						}
					}
				}
				break;
			case ',': {
				if (m_deep && State(m_deep - 1).m_defType == C_ENUM)
				{
					State().m_defType = C_ENUMITEM; // In case a macro was processed which reset deftype.
					if (State(m_deep - 1).m_defAttr & V_MANAGED)
						State().m_defAttr |= V_MANAGED;
				}
				if (!IsDef(m_deep))                        // int foo[] = {one, two, three};
					State().m_parseState = VPS_ASSIGNMENT; // assignment

				if (IsDef(m_deep) && !State().HasParserStateFlags(VPSF_CONSTRUCTOR))
				{
					if (State().HasParserStateFlags(VPSF_INHERITANCE))
					{
						// [case: 2302] typedef with inheritance
						// typedef struct tagFoo : public bar {} Foo, *LPFoo;
						if (State().m_defType == STRUCT && FwIS("typedef"))
							OnDef();
					}
					else
						OnDef();
				}
				State().m_argCount++;

				if (m_deep && ('<' == State(m_deep - 1).m_lastChar || '(' == State(m_deep - 1).m_lastChar) &&
				    !StartsWith(State(m_deep - 1).m_lastWordPos, "for") &&
				    !StartsWith(State(m_deep - 1).m_lastWordPos, "using")) // case=6757: using statement in C#
				{
					// foo(int a, int b)
					// pretend there is a ; here
					ClearLineState(CurPos());
					m_InSym = FALSE;
					continue;
				}
				else if (State().m_defType == VAR || State().m_defType == C_ENUMITEM)
					State().m_parseState = VPS_BEGLINE; // int a=1, b

				State().m_lastWordPos = CurPos(); // points to beginning of last sym
				State().m_lwData.reset();
				// in "int *j, k" clear v_pointer attr at comma
				if (State().m_defAttr & V_POINTER)
					State().m_defAttr &= ~V_POINTER;
			}
			break;
			case ';':
				// [case: 146077] enum items marked with [[fallthrough]] attribute are coloured as local variables, not enum item
 				if (State().m_lastChar == ']' && m_deep > 1 && StartsWith(State(m_deep - 1).m_lastScopePos, "switch"))
 					continue;
				
				if (InParen(m_deep) && (!StartsWith(State(m_deep - 1).m_lastWordPos, "for")))
				{
					DebugMessage("Semicolon unexpected");
					OnError(State().m_begBlockPos - 1);
				}
				if (IsDef(m_deep))
				{
					if (ISCSYM(State().m_lastScopePos[0]) || State().m_lastScopePos[0] == '~' ||
					    (State().m_lastScopePos[0] == '!' &&
					     ISCSYM(State().m_lastScopePos[1]))) // don't add "class foo{...};
					{
						if (ConstState(m_deep).m_defType == VAR && FwIS("goto"))
							; // [case: 1909] do not do OnDef for "goto Label;"
						else if (ConstState(m_deep).m_defType == FUNC && FwIS("static_assert"))
							; // [case: 97232]
						else
							OnDef();
					}
				}

				if (State().HasParserStateFlags(VPSF_DEC_DEEP_ON_EOS))
				{
					State().RemoveParserStateFlags(VPSF_DEC_DEEP_ON_EOS);
					DecDeep();
				}

				// [case: 97964] [case: 78814] [case: 113209] DecDeep() on ';'
				for (ULONG lastDeep = m_deep, forIfCnt = 0;
				     m_deep > 1 && State(m_deep - 1).HasParserStateFlags(VPSF_KEEPSCOPE_FOR | VPSF_KEEPSCOPE_IF) &&
				     State().HasParserStateFlags(VPSF_NAKEDSCOPE_FOR_IF);
				     ++forIfCnt)
				{
					if (State(m_deep - 1).HasParserStateFlags(VPSF_KEEPSCOPE_FOR))
					{
						if (!OnForIf_CloseScope(NewScopeKeyword::FOR))
							break;
						State().RemoveParserStateFlags(VPSF_KEEPSCOPE_FOR);
						State().m_conditionalBlocCount++;
					}
					else if (State(m_deep - 1).HasParserStateFlags(VPSF_KEEPSCOPE_IF))
					{
						if (!OnForIf_CloseScope(NewScopeKeyword::IF))
							break;
						State().RemoveParserStateFlags(VPSF_KEEPSCOPE_IF);
						State().m_conditionalBlocCount++;
					}
					else
					{
						// [case: 141021]
						vLog("ERROR: forif pop fail");
						break;
					}

					if (lastDeep == m_deep)
					{
						// [case: 141817]
						// there are conditions in which DecDeep doesn't change m_deep, but this loop assumes always
						// occurs
						vLog("ERROR: forif break inf loop (B1)");
						break;
					}

					lastDeep = m_deep;
					if (forIfCnt > STATE_COUNT)
					{
						// [case: 141021]
						// last chance to catch infinite loop
						vLog("ERROR: forif break inf loop (B2)");
						break;
					}
				}

				ClearLineState(CurPos());
				m_InSym = FALSE;
				continue;
			case ':':
				if (NextChar() == ':')
				{
					// ::
					// Reset lwData on (x && ::SendMessage()) so parser DoScope does not look for "x::SendMessage"
					if (State().m_lwData && strchr("&|", State().m_lastChar))
						State().m_lwData.reset();
					if (State().m_parseState == VPS_NONE && State().m_begLinePos != CurPos())
					{
						// [case: 85237] ::GlobalCls foo;
						ClearLineState(CurPos());
						State().m_parseState = VPS_BEGLINE;
					}
					IncCP();
					c = '.'; // Pretend "foo::bar" is "foo.bar"
					break;
				}
				// :
				if (State().m_parseState == VPS_ASSIGNMENT)
					State().m_lwData.reset(); // for "h?h : ::GetFocus()"
				else
				{
					BOOL isConstructor = State().m_defType == FUNC; // constructor
					BOOL isTag = IsTag();
					if (FwIS("generic"))
						continue; // generic <typename Ty> where Ty: CType class GenericFoo{...};
					if (!isConstructor && isTag)
					{ // GotoTag: or public: or case foo:
						// TODO: call OnDef for "GotoTag:"
						if (!FwIS("public") && !FwIS("protected") && !FwIS("private") &&
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
						    !FwIS("__published") &&
#endif
						    !FwIS("internal") && !FwIS("case") && !FwIS("default") &&
						    // case=912 Qt Library
						    !FwIS("signals") && !FwIS("Q_SIGNALS") && !LwIS("slots") && !LwIS("Q_SLOTS"))
						{
							State().m_defType = VAR;
							State().SetParserStateFlags(VPSF_GOTOTAG);
							OnDef();
						}
						else if (m_deep)
						{
							// Include case :/public: tags in scope info
							State(m_deep - 1).SetParserStateFlags(VPSF_SCOPE_TAG);
							// Using parents m_lastWordPos to store tag position, if it causes any problems, we could
							// create another state member.
							State(m_deep - 1).m_lastWordPos = State().m_lastWordPos;
						}
						State().m_parseState = VPS_NONE;
						m_InSym = FALSE;
						continue;
					}
					else
					{
						// "class foo : bar{"?
						if (!InLocalScope(m_deep))
						{
							if (State().m_defType == VAR) // not Foo(int i): base(i){...}
							{
								LPCTSTR p;
								for (p = CurPos(); *p && !strchr(";{", *p); p++)
									;
								if (*p == ';')
								{
									// struct { int var:8;
									State().m_parseState = VPS_ASSIGNMENT; // assignment
									break;
								}
							}
						}
						if (State().m_defType == FUNC)
							State().SetParserStateFlags(VPSF_CONSTRUCTOR);
						else if (State().m_defType == CLASS || State().m_defType == STRUCT ||
						         State().m_defType == C_INTERFACE)
							State().SetParserStateFlags(VPSF_INHERITANCE);

						if (!(IsDef(m_deep) && State().m_defType == STRUCT && FwIS("typedef")))
							State().m_parseState = VPS_ASSIGNMENT; // assignment except for case=2302
						State().m_lwData.reset();

						// set scope for constructors "foo(int i): m_bar(i){"
						// [case: 74977] and for "class foo : bar" defined inside of function
						if ((m_parseFlags & PF_CONSTRUCTORSCOPE) && m_deep < STATE_COUNT &&
						    (!InLocalScope(m_deep) || (State().GetParserStateFlags() & VPSF_INHERITANCE)) &&
						    State().m_defType != C_ENUM)
						{
							State().m_lastChar = ':';
							IncDeep();
							State().m_parseState = VPS_ASSIGNMENT; // assignment
						}
					}
				}
				break;
				// valid chars before assignment
			case '*':
				if (NextChar() == '/')
				{
					// oops we got a */ in what we thought was code
					// Happens with "#define foo /*\r\n*/
					ClearLineState(CurPos());
					IncCP(); // eat the '/'
					continue;
				}

			case '^': // Placed here for Managed C++ "array<Object^> ^myArray = ..., myArray is a pointer into
			          // GlobalHeap
				if (State().m_defType &&
				    State().m_parseState != VPS_ASSIGNMENT) // "class CWnd *wnd;" is not a forward declare.
				{
					if (FwIS("typedef"))
						State().m_defType = TYPE;
					else
						// CLI EVENTS
						if (State().m_defType != EVENT && State().m_defType != DELEGATE)
							State().m_defType = VAR;
				}
				// In "int c = *pInt" c is not a pointer
				if (State().m_parseState != VPS_ASSIGNMENT)
					State().m_defAttr |= V_POINTER;
			case '%': // Placed here for Managed C++ "int %ti = i;" ti is reference to i
			case '&':
				if (State().m_lwData && State().m_lwData->MaskedType() == VAR)
				{
					// "if(foo*bar", bar is not a def, but if(int*bar" is
					State().m_parseState = VPS_ASSIGNMENT;
					break;
				}
				State().m_lwData.reset();
				if (State().m_parseState == VPS_NONE)
				{
					// *foo = 'x'; // set VPS_ASSIGNMENT
					ClearLineState(CurPos());
					State().m_parseState = VPS_ASSIGNMENT;
					break;
				}
			case '.':
				if (c == '&' && NextChar() == '&')
				{
					IncCP();
					OnChar();

					if (State().m_defType == UNDEF && State().m_parseState != VPS_ASSIGNMENT)
					{
						// [case: 81995]
						// limit fix to case where ) is the preceding char
						// before "&&" so that movable references aren't broken
						LPCSTR p1 = State().m_lastWordPos;
						if (p1)
						{
							// ensure CurPos is near State().m_lastWordPos
							if (CurPos() > p1 && CurPos() < (p1 + 1024))
							{
								LPCSTR p2 = strstr(p1, "&&");
								if (p2)
								{
									WTString txt(p1, ptr_sub__int(p2, p1));
									txt.Trim();
									if (txt == "auto")
										; // [case: 96952]
									else if (txt.GetLength() > 1 && txt.ReverseFind(')') == txt.GetLength() - 1)
										State().m_parseState = VPS_ASSIGNMENT;
									else if (m_deep && State(m_deep - 1).m_lastChar == '(' &&
									         (StartsWith(State(m_deep - 1).m_begLinePos, "if") ||
									          StartsWith(State(m_deep - 1).m_begLinePos, "for")))
									{
										// [case: 93231]
										State().m_parseState = VPS_ASSIGNMENT;
									}
								}
							}
						}
					}
				}
				else
				{
					// typecasting
					if (!State().m_lwData || State(m_deep).m_lwData->MaskedType() ==
					                             RESWORD) // Filter reserved words "return (X).m_x", case:16345
					{
						// don't copy the type if an undefined symbol was encountered; otherwise meaningless autocomplete might be invoked
						bool dont_copy_type = !State().m_lwData && IsStateDepthOk(m_deep + 1) && State(m_deep + 1).m_dontCopyTypeToLowerState;

						if(!dont_copy_type)
							State().m_lwData = State(m_deep + 1).m_lwData;
					}
				}
				break;

			case '-':
				if (State().m_parseState != VPS_ASSIGNMENT)
				{
					if (FUNC == State().m_defType && NextChar() == '>')
					{
						// [case: 20625] decltype
						// auto Func(int x) -> decltype(x) { ... }
					}
					else
					{
						State().m_parseState = VPS_ASSIGNMENT; // assignment
						State().m_defType = UNDEF;
					}
				}
				if (NextChar() == '>')
				{
					IncCP();
					OnChar();
					c = '.';
					if (Lambda_Type != State().m_defType)
					{
						// typecasting
						if (!State().m_lwData || State(m_deep).m_lwData->MaskedType() ==
						                             RESWORD) // Filter reserved words "return (X).m_x", case:16345
							State().m_lwData = State(m_deep + 1).m_lwData;
					}
					break;
				}
				// other symbols, must be an assignment
			case '/':
			case '+':
			case '|':
			case '?':
				if (GetLangType() == CS && c == '?')
				{
					// Nullable vars. case 867
					// int? j = null;

					if (NextChar() == '.')
					{
						// [case: 105904]
						// null-conditional operator: foo?.bar()
						IncCP();
						OnChar();
						c = '.'; // Pretend "foo?.bar" is "foo.bar"
						break;
					}
				}
				else if (State().m_parseState != VPS_ASSIGNMENT)
				{
					State().m_defType = UNDEF;             // "foo*bar + 3" bar is not a pointer to foo
					State().m_parseState = VPS_ASSIGNMENT; // assignment
					State().m_lastScopePos = CurPos();
				}
				// State().m_lastWordPos = CurPos(); // points to beginning of last sym
				State().m_lwData.reset();
				break;
			case '`':
				DebugMessage("Invalid char");
				OnError(CurPos());
				break;
				// AlphaNum, _ or maybe ~!
			case '!':
				// [case: 19150] assignment or finalizer
				if ((InLocalScope() && !IsXref()) || wt_ispunct(NextChar()))
				{
					// this block copied from assignment @/+|? above
					if (State().m_parseState != VPS_ASSIGNMENT)
					{
						State().m_defType = UNDEF;             // "foo*bar + 3" bar is not a pointer to foo
						State().m_parseState = VPS_ASSIGNMENT; // assignment
						State().m_lastScopePos = CurPos();
					}
					//					State().m_lastWordPos = CurPos(); // points to beginning of last sym
					State().m_lwData.reset();
					break;
				}
				else
					; // !MyClass() finalizer - fall through to case '~'
					  // fall through
			case '~':
				if (State().m_parseState != VPS_ASSIGNMENT)
					State().m_defAttr |= V_CONSTRUCTOR;

				if (InLocalScope())
					break; // i~=7;
					       // else ~MyClass()
					       // fall through
			case '$':
				if (c == '$' && !IsDollarValid())
					break;
				// fall through
			case '@':
				if (c == '@')
				{
					if (CS == GetLangType() && m_cp && !(ISCSYM(PrevChar())))
					{
						// [case: 140822]
						// this is lenient, it could be more strict and confirm
						// that following symbol is a keyword.
					}
					else
					{
						if (State().m_parseState != VPS_ASSIGNMENT)
						{
							State().m_defType = UNDEF;             // "foo*bar + 3" bar is not a pointer to foo
							State().m_parseState = VPS_ASSIGNMENT; // assignment
							State().m_lastScopePos = CurPos();
						}
						State().m_lwData.reset();
						break;
					}
				}
				// fall through
			default:
				// Gotta be CSym?
#ifdef DEBUG_VAPARSE
				if (!(ISCSYM(c) || c == '~' || c == '!'))
					DebugMessage("NonCSym");
#endif // DEBUG_VAPARSE
				if ((c == 'a' || c == 'c' || c == 'v') && m_mp)
					ParseStructuredBinding();
				if (!m_InSym)
				{
					if (ProcessMacro())
					{
						if (m_InSym)
						{
							// ProcessMacro() may leave m_InSym set, needs to be cleared. case=43586
							m_InSym = FALSE;

							//							if (CurChar() == ')' && VAR == State().m_defType)
							//							{
							//								// [case: 114223]
							//								// VAR is the default type (see makeVar below)
							//								// if we processed macro, and was left inSym and now CurChar is ')' ,
							// then assume FUNC.
							//								// Example: void SendMessage(UINT message, WPARAM wParam = 0,
							// LPARAM lParam = 0) const;
							//								// State().m_defType = FUNC;
							// #ifdef _DEBUG
							//								_asm nop;
							// #endif
							//							}
						}

						continue; // Macro expanded, continue.
					}

					// Macro hack to recover from MACRO()'s with no ;
					// Since we do not expand all macros by default, we leave this in for special macro recovery
					if (State().m_lastChar == ')')
					{
						if (State().m_parseState != VPS_ASSIGNMENT && State().m_defType == FUNC)
						{
							// "int foo() THROW_NONE", THROW_SOMETHING could be a macro we do not expand unless expand
							// all macros is enabled
							if (_tcsnicmp(CurPos(), "const", 5) == 0 ||
							    _tcsnicmp(CurPos(), "constexpr", 9) == 0 // [case: 86383]
							    || _tcsnicmp(CurPos(), "consteval", 9) == 0
								|| _tcsnicmp(CurPos(), "constinit", 9) == 0
							    || _tcsnicmp(CurPos(), "_CONSTEXPR17", 12) == 0 ||
							    _tcsnicmp(CurPos(), "_CONSTEXPR20_CONTAINER", 22) == 0 ||
							    _tcsnicmp(CurPos(), "_CONSTEXPR20", 12) == 0 ||
							    _tcsnicmp(CurPos(), "restrict", 8) == 0 ||
							    _tcsnicmp(CurPos(), "__resumable", 11) == 0 // [case: 79163]
							    || _tcsnicmp(CurPos(), "throw", 5) == 0 ||
							    _tcsnicmp(CurPos(), "noexcept", 8) == 0 // [case: 85266]
							    || _tcsnicmp(CurPos(), "pure", 4) == 0 ||
							    _tcsnicmp(CurPos(), "__attribute__", 13) == 0 // [case: 79523] gcc
							    || _tcsnicmp(CurPos(), "volatile", 8) == 0 ||
							    _tcsnicmp(CurPos(), "override", 8) == 0        // 2005/clr Specifier
							    || _tcsnicmp(CurPos(), "abstract", 8) == 0     // 2005/clr Specifier
							    || _tcsnicmp(CurPos(), "sealed", 6) == 0       // 2005/clr Specifier
							    || _tcsnicmp(CurPos(), "final", 5) == 0        // c++11 Specifier
							    || _tcsncmp(CurPos(), "Q_DECL_FINAL", 12) == 0 // [case: 86039]
							    || _tcsnicmp(CurPos(), "new", 3) == 0          // 2005/clr Specifier
							    || _tcsnicmp(CurPos(), "VARARG_NONE", 11) == 0 // Fixes UC's VARARG_DECL macro
							    || _tcsnicmp(CurPos(), "try", 3) == 0          // void foo() try{} catch{} case=20848
							)
							{
								if (GetLangType() == UC) // not for "var(type) const bar;" in UC
								{
									ClearLineState(State().m_begLinePos);
								}
								else
									State().m_parseState = VPS_ASSIGNMENT; // int foo() throw(CException) {}
							}
							else
							{
								// int Func() MACRO; // if MACRO is all upper and Func is not, assume we are defining
								// Func
								WTString macroName = GetCStr(CurPos());
								WTString macroNameUC = macroName;
								macroNameUC.to_upper();
								WTString funcName = GetCStr(State().m_lastScopePos);
								WTString funcNameUC = funcName;
								funcNameUC.to_upper();
								bool tryAsDef = false;
								bool treatAsMacro = true;
								if (!macroName.IsEmpty())
								{
									if (macroName == macroNameUC && funcName != funcNameUC)
										tryAsDef = true;
									else if (!funcName.IsEmpty() && funcName == funcNameUC)
									{
										if (State().m_lastWordPos > State().m_lastScopePos)
										{
											// lastWordPos and lastScopePos could be pointing into different buffers;
											// if so, GetSubStr will return empty string
											WTString tmp = GetSubStr(State().m_lastScopePos, State().m_lastWordPos);
											tmp.Trim();
											if (!tmp.IsEmpty())
											{
												int pos = tmp.Find("::");
												if (-1 != pos && pos == tmp.GetLength() - 2)
												{
													// [case: 99509]
													// int MYCLASS::MethodA() macro
													// int MYCLASS::MethodB() MACRO
													treatAsMacro = false;
												}
											}
										}
									}
								}

								if (tryAsDef)
								{
									bool assumeIsDef = true;
									if (IsCFile(GetLangType()))
									{
										MultiParsePtr pmp = m_mp ? m_mp : MultiParse::Create(GetLangType());
										const DType* dt = pmp->FindExact2(funcName);
										if (dt && DEFINE == dt->type())
										{
											// [case: 81329]
											// _IRQL_requires_max_ is not a function def:
											// _Must_inspect_result_ _IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS WINAPI
											// Foo()
											assumeIsDef = false;
											// clear defType even though it is correct right now, so that
											// State().m_lastScopePos gets updated
											State().m_defType = UNDEF;
										}
									}

									if (assumeIsDef)
									{
										DebugMessage("Assuming method definition");

										// TODO: consider removing these two lines -
										// they appear to cause duplication of Func in alt+m list.
										OnDef();                               // Add Func()
										State().m_parseState = VPS_ASSIGNMENT; // int Func() MACRO {}
									}
								}
								else if (0 == funcName.Find("STDMETHOD"))
								{
									// [case: 12800] [case: 45936] [case: 58329] [case: 12803]
									State().m_parseState = VPS_ASSIGNMENT; // STDMETHODIMP_(foo, bar)(int param) {}
								}
								else if (treatAsMacro)
								{
									if (funcName != funcNameUC && !macroName.IsEmpty())
									{
										// [case: 59265]
										// int Func() txt; // if txt is a macro and Func is not all upper, assume we are
										// defining Func
										//
										// See if is macro by doing sym search but note that
										// the search will fail if it is a macro and defined in
										// the current file (since the file is being parsed).
										MultiParsePtr pmp = m_mp ? m_mp : MultiParse::Create(GetLangType());
										const DType* dt = pmp->FindExact2(macroName);
										if (dt && DEFINE == dt->type())
										{
											DebugMessage("Assuming method definition 2");
											treatAsMacro = false;
											// skipping OnDef and State().m_parseState = VPS_ASSIGNMENT prevents
											// some oddities that occur with the UPPER macro logic where the
											// first "Assuming method definition" message is
										}
									}

									if (treatAsMacro)
									{
										// Just a MACRO()
										ClearLineState(CurPos());
									}
								}
							}
						}
					}

					// set beginning of line
					if (State().m_parseState == VPS_NONE)
					{
						const bool wasVirt = State().m_defAttr & V_VIRTUAL;
						const auto prevBegLinePos = State().m_begLinePos;
						ClearLineState(CurPos());
						if (wasVirt)
						{
							// [case: 140598]
							// retain V_VIRTUAL attr set during macro processing
							State().m_defAttr |= V_VIRTUAL;
							State().m_begLinePos = prevBegLinePos;
						}
						State().m_parseState = VPS_BEGLINE;
						if (m_deep && State(m_deep - 1).m_defType == C_ENUM)
						{
							State().m_defType = C_ENUMITEM;
							if (State(m_deep - 1).m_defAttr & V_MANAGED)
								State().m_defAttr |= V_MANAGED;
						}
					}

					// set m_lastScopePos #vaparseUpdateLastScopePos
					if (State().m_parseState != VPS_ASSIGNMENT /*&& State().m_defType != FUNC*/)
					{
						if (State().m_lastChar != '.' && State().m_lastChar != '~' && State().m_lastChar != '!')
						{
							if (State().m_defType != FUNC || State().m_lastChar == ',') // int foo() const;
							{
								LPCSTR curPos = CurPos();
								if (StartsWith(curPos, "const") && (!m_deep || State(m_deep - 1).m_defType != C_ENUM))
								{
									// [case: 114223]
									// macro parsing must have left bad state; "const" should never be a
									// lastScopePos (except in unreal engine enum)
									// #ifdef _DEBUG
									//									_asm nop;
									// #endif
								}
								else if (curPos != State().m_lastScopePos)
								{
									bool updateLastScopePos = true;
									if (State().m_defType == VAR || State().m_defType == TYPE)
									{
										const WTString curwd = GetCStr(curPos);
										if (!curwd.IsEmpty() && curwd.GetLength() > 4)
										{
											WTString curwdUC = curwd;
											curwdUC.to_upper();
											const WTString prevscope = GetCStr(State().m_lastScopePos);
											if (!prevscope.IsEmpty())
											{
												WTString prevscopeUC = prevscope;
												prevscopeUC.to_upper();
												if (curwd == curwdUC && prevscope != prevscopeUC)
												{
													MultiParsePtr pmp = m_mp ? m_mp : MultiParse::Create(GetLangType());
													const DType* dt = pmp->FindExact2(curwd, false, DEFINE, false);
													// const DType *dt = GetSysDic()->FindExact(curwd, 0, FALSE, DEFINE,
													// false);
													if (dt && dt->IsVaStdAfx())
													{
														// [case: 141990]
														// Fix outline text for FooBar:
														// FooBar MACRO (int baz);
														updateLastScopePos = false;
													}
												}
											}
										}
									}
									else if (State().m_defType == NAMESPACE &&
									         StartsWith(State().m_lastWordPos, "inline"))
									{
										// [case: 141698]
										updateLastScopePos = false;
									}

									if (updateLastScopePos)
									{
										// points to beginning of last sym's scope "foo.bar.baz"// foo, not baz
										State().m_lastScopePos = curPos;
									}
								}
								// #ifdef _DEBUG
								//								else
								//									_asm nop;
								// #endif
							}
						}

						if (State().m_parseState == VPS_BEGLINE && State().m_begLinePos != CurPos())
						{
							// two symbols in a row without VPS_ASSIGNMENT, must be a def?
							// "int i" or "char *p", however could be "a *b
							if (UNDEF == State().m_defType && State().m_lastChar != '.')
							{
								bool makeVar = true;
								// Added "return" check because State() does not set VPS_ASSIGNMENT
								// when parsing for globals(should affect JS files only).

								// [case: 82014] added visibility keywords because this is too early
								// a time to set defType as it causes references to be classified as defs
								// (IsDef() simply checks that defType != UNDEF)
								if (StartsWith(State().m_lastWordPos, "return") ||
								    StartsWith(State().m_lastWordPos, "co_return") ||
								    StartsWith(State().m_lastWordPos, "co_yield") ||
								    StartsWith(State().m_lastWordPos, "inline") ||
								    StartsWith(State().m_lastWordPos, "public") ||
								    StartsWith(State().m_lastWordPos, "private") ||
								    StartsWith(State().m_lastWordPos, "internal") ||
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
								    StartsWith(State().m_lastWordPos, "__published") ||
#endif
								    StartsWith(State().m_lastWordPos, "protected"))
								{
									makeVar = false;
								}
								else if (m_deep && State(m_deep - 1).m_lastChar == '<')
								{
									// case:2352 and case:17359
									// not vector<const aClass> or dynamic_cast<const aClass>
									if (StartsWith(State().m_begLinePos, "const") &&
									    CurPos() == (State().m_begLinePos + strlen("const ")))
										makeVar = false;
									// [case: 88554]
									else if (StartsWith(State().m_begLinePos, "unsigned") &&
									         CurPos() == (State().m_begLinePos + strlen("unsigned ")))
										makeVar = false;
									else if (StartsWith(State().m_begLinePos, "signed") &&
									         CurPos() == (State().m_begLinePos + strlen("signed ")))
										makeVar = false;
									else if (StartsWith(State().m_begLinePos, "long") &&
									         CurPos() == (State().m_begLinePos + strlen("long ")))
										makeVar = false;
								}

								if (makeVar)
									State().m_defType = VAR;
							}
						}
					}

					if (!m_inIFDEFComment)
						OnCSym();
					State().m_lastWordPos = CurPos(); // points to beginning of last sym
					m_InSym = TRUE;
					State().m_lastChar = CurChar();
				}
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
	if (InComment())
	{
		OnNextLine(); // pretend there's another line (for line comments on last line)
		OnComment(0);
	}
}

// [case: 97964] [case: 113209] [case: 78814]
void VAParse::ForIf_DoCreateScope()
{
	State().m_lastChar = CurChar();

	IncDeep();
	m_InSym = FALSE;

	State().m_parseState = VPS_ASSIGNMENT;
}

// [case: 97964] [case: 113209] [case: 78814]
void VAParse::ForIf_CreateScope(NewScopeKeyword keyword)
{
	State().m_lastScopePos = CurPos();
	State().m_parseState = VPS_ASSIGNMENT; // assignment
	State().m_defType = UNDEF;
}

// [case: 97964] [case: 113209] [case: 78814]
bool VAParse::IsNakedScope(LPCSTR pos)
{
	auto cp = pos - m_buf;
	if (cp > mBufLen)
		return false;
	cp += 1;

	// skip whitespaces to see if the next non-comment character is '{'
	CommentSkipper cs(FileType());
	int counter = 0;
	while (cp < mBufLen - 1)
	{
		if (++counter > 1024)
			return false;

		TCHAR c = m_buf[cp];
		if (cs.IsCode(c))
		{
			if (!IsWSorContinuation(m_buf[cp]) && c != '/')
				break;
		}

		cp++;
	}

	bool bracedScope = m_buf[cp] == '{';
	return !bracedScope;
}

void VAParse::DoParse(const WTString& buf, BOOL parseFlags /*= PF_TEMPLATECHECK|PF_CONSTRUCTORSCOPE*/)
{
	m_parseFlags = (ULONG)parseFlags;
	if (buf.GetLength())
		Init(buf);
	DoParse();
}

void VAParse::DoParse(LPCTSTR buf, int bufLen, BOOL parseFlags /*= PF_TEMPLATECHECK|PF_CONSTRUCTORSCOPE*/)
{
	m_parseFlags = (ULONG)parseFlags;
	Init(buf, bufLen);
	DoParse();
}

void VAParse::DoParse()
{
	_ASSERTE(HasSufficientStackSpace());
#ifndef DEBUG_VAPARSE
	try
	{
#endif // DEBUG_VAPARSE
		_DoParse();
#ifndef DEBUG_VAPARSE
	}
	catch (const UnloadingException&)
	{
		VALOGEXCEPTION("VAP-unloading:");
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("VAP:");
		DebugMessage("Exception caught in _doparse");
	}
#endif // !SEAN
#endif // DEBUG_VAPARSE
}

WTString VAParse::GetBaseScope()
{
	_ASSERTE(HasSufficientStackSpace());
	DEFTIMERNOTE(VAP_GetBaseScope, NULL);
	ULONG i;
	for (i = m_deep; i && InLocalScope(i - 1); i--)
		;

	if (i && Psettings->mUnrealScriptSupport)
	{
		if (State(i - 1).HasParserStateFlags(VPSF_UC_STATE))
			return StrGetSymScope(Scope(i));
	}

	if (InLocalScope(i))
	{
		const WTString scp(Scope(i));
		return StrGetSymScope(scp);
	}

	return Scope(i);
}

WTString VAParse::MethScope()
{
	return Scope(m_deep + 1);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Derived classes

// see also BoldBraceCls::OnDirective for a bastardized version of VAParseDirectiveC::OnDirective
void VAParseDirectiveC::OnDirective()
{
	DEFTIMERNOTE(VAP_OnDirective, NULL);
	WTString directive = TokenGetField(&m_buf[m_cp], "# \t");
	State().m_StatementBeginLine = m_curLine;
	if (strncmp(directive.c_str(), "if", 2) == 0)
	{
		// Look for #if 0
		mDirectiveBlockStack.push_back(0);
		m_inDirectiveType = Directive::IfElse;
		token2 ln = TokenGetField(&m_buf[m_cp], "\r\n");
		ln.read("# \t("); // if/ifdef
		WTString op = ln.read();
		bool ignoreBlock = false;
		if (op == "0")
		{
			// skip "#if 0" block (change 5345)
			ignoreBlock = true;
		}
		else if (directive.GetLength() == 2 && directive == "if")
		{
			if (op == "defined")
			{
				op = ln.read();
				if (::StartsWith(op, "__OBJC__", TRUE))
				{
					// [case: 119590]
					// skip objective-C block
					// #if defined( __OBJC__ )
					ignoreBlock = true;
				}
			}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			else if (op == "!defined" && !InClassScope(m_deep))
			{
				if (Header == GetFType())
				{
					op = ln.read();
					if (op.contains("DELPHIHEADER_NO_IMPLICIT_NAMESPACE_USE") || op.contains("NO_USING_NAMESPACE"))
					{
						// [case: 141967]
						// #if !defined(DELPHIHEADER_NO_IMPLICIT_NAMESPACE_USE) &&
						// !defined(NO_USING_NAMESPACE_DATA_BIND_DBXSCOPE)
						ignoreBlock = true;
					}
				}
			}
#endif
		}
		else if (directive.GetLength() > 4 && directive[2] != 'n' && op == "__OBJC__")
		{
			// [case: 119590]
			// skip objective-C block
			// #ifdef __OBJC__
			ignoreBlock = true;
		}

		if (ignoreBlock)
		{
			m_inIFDEFComment = TRUE;
			mDirectiveBlockStack.back() = 1;
			return;
		}

		// else save state so we can restore for #else
		m_savedState.LoadParseState(this, true);
		m_inDirective = TRUE;
	}
	else if (strncmp(directive.c_str(), "el", 2) == 0)
	{
		m_inDirectiveType = Directive::IfElse;
		if (m_inIFDEFComment)
		{
			if (!mDirectiveBlockStack.empty() && mDirectiveBlockStack.back())
			{
				// [case: 73884] reset after close of "#if 0"
				m_inIFDEFComment = FALSE;
				mDirectiveBlockStack.back() = 0;
				m_InSym = FALSE;
				ClearLineState(CurPos());
			}
		}

		// restore state to what it was before the if
		if (m_inDirective)
		{
			const int cp = m_cp;
			const ULONG ln = m_curLine;
			_ASSERTE(m_buf == m_savedState.GetBuf());
			LoadParseState(&m_savedState, false);
			m_cp = cp;
			_ASSERTE(m_cp < mBufLen);
			m_curLine = ln;
		}

		if (directive.GetLength() == 4 && directive == "elif")
		{
			token2 ln = TokenGetField(&m_buf[m_cp], "\r\n");
			ln.read("# \t("); // else/elif
			WTString op = ln.read();
			if (op == "defined")
			{
				op = ln.read();
				if (::StartsWith(op, "__OBJC__", TRUE))
				{
					// [case: 119590]
					// skip objective-C block
					// #elif defined( __OBJC__ )
					m_inIFDEFComment = TRUE;
					if (mDirectiveBlockStack.size())
						mDirectiveBlockStack.back() = 1;
					return;
				}
			}
		}
	}
	else if (strncmp(directive.c_str(), "end", 3) == 0)
	{
		m_inDirectiveType = Directive::IfElse;
		if (strncmp(directive.c_str(), "endif", 5) == 0)
		{
			const bool curLevelIsComment = !mDirectiveBlockStack.empty() && mDirectiveBlockStack.back() > 0;
			if (!mDirectiveBlockStack.empty())
				mDirectiveBlockStack.pop_back();

			if (m_inIFDEFComment && (curLevelIsComment || mDirectiveBlockStack.empty()))
			{
				// [case: 119590]
				bool anyCommentInStack = false;
				for (auto cur : mDirectiveBlockStack)
				{
					if (cur)
					{
						anyCommentInStack = true;
						break;
					}
				}

				if (!anyCommentInStack)
				{
					// [case: 73884] reset after close of "#if 0"
					m_inIFDEFComment = FALSE;
					m_InSym = FALSE;
					ClearLineState(CurPos());
				}
			}
			_ASSERTE(!m_inIFDEFComment || mDirectiveBlockStack.size());
		}

#ifdef CASE_97015
		// [case: 97015]
		// restore state to what it was before the if
		if (m_inDirective)
		{
			const int cp = m_cp;
			const ULONG ln = m_curLine;
			_ASSERTE(m_buf == m_savedState.GetBuf());
			LoadParseState(&m_savedState, false);
			m_cp = cp;
			_ASSERTE(m_cp < mBufLen);
			m_curLine = ln;
		}
#endif

		m_inDirective = FALSE;
	}
	else if (strncmp(directive.c_str(), "define", 6) == 0)
	{
		ClearLineState(CurPos());
		m_inDirectiveType = Directive::Define;
	}
	else if (strncmp(directive.c_str(), "pragma", 6) == 0)
	{
		ClearLineState(CurPos());
		m_inDirectiveType = Directive::Pragma;
	}
	else if (strncmp(directive.c_str(), "error", 6) == 0)
	{
		ClearLineState(CurPos());
		m_inDirectiveType = Directive::Error;
	}
	else
	{
		if (m_parseGlobalsOnly && m_writeToFile && GetScopeInfoPtr()->IsWriteToDFile())
		{
			if ((strncmp("include", directive.c_str(), 7) == 0 && m_buf[m_cp + 8] != '\0')
#ifdef RECORD_IDL_IMPORTS
			    || (Idl == GetLangType() && StartsWith(directive.c_str(), "import") && m_buf[m_cp + 7] != '\0')
#endif // RECORD_IDL_IMPORTS
			)
			{
				// This block started out as a duplication of MultiParse::AddPPLn() #includeDirectiveHandling
				BOOL isSysinclude = FALSE;
				CStringW file = ::GetIncFileStr(&m_buf[m_cp + 8], isSysinclude);
				if (file.GetLength())
				{
					int ftype = ::GetFileType(file);
					if (Binary != ftype)
					{
						WTString origFile(file);
						WTString includeDirectiveDef;
						file = gFileFinder->ResolveInclude(file, ::Path(m_mp->GetFilename()), !isSysinclude);
						if (file.GetLength())
						{
							file = ::MSPath(file);
							if (!m_mp->IsIncluded(file))
								::ParseGlob((LPVOID)(LPCWSTR)file);

							static const int hashIncludeMembers = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "AllowHashIncludeMembers") != "No";
							if (hashIncludeMembers && m_deep && IS_OBJECT_TYPE(State(m_deep - 1).m_defType) &&
							    Scope().GetLength() > 1)
							{
								// Allow: class cls{ #include "members.h"};	// case=9117
								// members.h will still be parsed at global scope for GOTO
								// But here, we read in the file and parse it as inline macro code for members lists.
								if (!m_inHashIncludeMembers) // prevent recursion
								{
									file.MakeLower();
									int cnt = mParsedScopedIncludeCount[file];
									if (cnt++ < 3)
									{
										mParsedScopedIncludeCount[file] = cnt;
										WTString code = ReadFile(file);
										m_inHashIncludeMembers = TRUE;
										_ASSERTE(InComment() && CommentType() == '#');
										OnComment(NULL); // fix for parsing of first line in included file
										ExpandMacroCode(code);
										OnComment('#');
										m_inHashIncludeMembers = FALSE;
									}
								}
							}

							includeDirectiveDef = gFileIdManager->GetFileIdStr(file);
						}
						else
						{
							CatLog("Parser.FileName", (WTString("#Include not found: ") + origFile + " in " + WTString(m_mp->GetFilename()))
							        .c_str());
							int pos = origFile.FindOneOf("\r\n");
							if (-1 != pos)
							{
								// [case: 116558]
								// include directive that is unterminated, eat line break
								origFile = origFile.Left(pos);
							}
							includeDirectiveDef = origFile + " (unresolved)";
							while ((pos = includeDirectiveDef.FindOneOf("/\\")) != -1)
								includeDirectiveDef = includeDirectiveDef.Mid(pos + 1);
						}

						// [case: 226] add entry even if we can't locate file
						m_mp->DBOut(gFileIdManager->GetIncludeSymStr(m_mp->GetFilename()), includeDirectiveDef, vaInclude,
						            V_IDX_FLAG_INCLUDE | V_HIDEFROMUSER, (int)m_curLine);
						m_mp->DBOut(WTString(gFileIdManager->GetIncludedByStr(file)), includeDirectiveDef, vaIncludeBy,
						            V_HIDEFROMUSER, (int)m_curLine);
					}
				}
			}
			else if (strncmp("using", directive.c_str(), 5) == 0 && m_buf[m_cp + 6] != '\0')
			{
				BOOL isSysinclude = FALSE;
				CStringW file = ::GetIncFileStr(&m_buf[m_cp + 6], isSysinclude);
				const WTString origFile(file);
				if (IsCFile(GetLangType()) && GlobalProject &&
				    (GlobalProject->CppUsesClr() || GlobalProject->CppUsesWinRT()))
				{
					if (file.GetLength())
					{
						file = gFileFinder->ResolveReference(file, ::Path(m_mp->GetFilename()));
						if (file.GetLength())
						{
							file = ::MSPath(file);
							if (Binary == ::GetFileType(file))
							{
								const WTString usingDirectiveDef(gFileIdManager->GetFileIdStr(file));
								m_mp->DBOut("+using", usingDirectiveDef, UNDEF, V_IDX_FLAG_INCLUDE | V_HIDEFROMUSER,
								            (int)m_curLine);
							}
							else
							{
								LogUnfiltered((WTString("WARN: #using skipped: ") + origFile + " in " +
								     WTString(m_mp->GetFilename()))
								        .c_str());
							}
						}
						else
						{
							CatLog("Parser.FileName", (WTString("#using not found: ") + origFile + " in " + WTString(m_mp->GetFilename()))
							        .c_str());
						}
					}
				}
				else
				{
					CatLog("Parser.FileName", (WTString("#using skipped: ") + origFile + " in " + WTString(m_mp->GetFilename())).c_str());
				}
			}
		}

		ClearLineState(CurPos());
		// include or import or #using assembly
		m_inDirectiveType = Directive::Include;
	}
}

void VAParseDirectiveC::OnNextLine()
{
	if (!m_inHashIncludeMembers) // Don't inc curline in multi-line macros: class c{#include members.h};
		__super::OnNextLine();
}

ULONG VAParseDirectiveC::GetDefPrivAttrs(uint deep)
{
	if (m_inHashIncludeMembers)
		return V_INCLUDED_MEMBER | State(deep).m_defAttr | State(deep).m_privilegeAttr;
	return State(deep).m_defAttr | State(deep).m_privilegeAttr;
}
////////////////////////////////////////////////////////////////////////////
// Adds basic keyword support
void VAParseKeywordSupport::OnCSym()
{
	if (InComment())
		return;

	// Process special keywords
	// TODO: could create a hash lookup for speed
	if (Is_VB_VBS_File(GetLangType()))
		return; // Defining a "Sub Delete()" causes rest of scope in file to be hosed

	DEFTIMERNOTE(VAP__OnCSymMac, NULL);
	LPCSTR cw = CurPos();
	switch (cw[0])
	{
	case 'o':
	case 'O':
		if (StartsWith(cw, "override"))
		{
			// [case: 140896]
			// must set override attribute even if only doing globals
			State().m_defAttr |= V_OVERRIDE;
		}
		break;
	}

	if (m_parseGlobalsOnly)
	{
		// [case: 81110] [case: 81111]
		if (State().m_parseState == VPS_ASSIGNMENT || (InLocalScope(m_deep) && !mParseLocalTypes))
			return;
	}

	switch (cw[0])
	{
	case 'a':
	case 'A':
		if (StartsWith(cw, "abstract") && IS_OBJECT_TYPE(State().m_defType))
		{
			LPCSTR cw2 = State().m_lastScopePos;
			if (!cw2 || StartsWith(cw2, "abstract")) // [case: 115473]
				RewindScopePos();                    // m_lastWordPos should be the class name, don't use "abstract"
			State().m_parseState = VPS_ASSIGNMENT;
		}
		break;
	case 'c':
	case 'C':
		if (StartsWith(cw, "class") && !StartsWith(State().m_begLinePos, "var"))
		{
			if (C_ENUM == State().m_defType && StartsWith(State().m_lastWordPos, "enum"))
				State().m_defAttr |=
				    V_MANAGED;                         // [case: 935] C++ managed enum is defined like "public enum class Bar..."
			else if (C_INTERFACE != State().m_defType) // [case: 65644] public interface class foo...
				State().m_defType = CLASS;
		}
		if (StartsWith(cw, "co_return") || StartsWith(cw, "co_yield"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT; // assignment
			State().m_defType = UNDEF;
		}
		break;
	case 'd':
	case 'D':
		if (cw[1] == 'e' || cw[1] == 'E')
		{
			if (StartsWith(cw, "delegate"))
			{
				State().m_defType = DELEGATE;
			}
			else if (StartsWith(cw, "delete"))
			{
				// [case: 63973] operator delete missing from MIF and outline
				if (InLocalScope())
				{
					State().m_lastScopePos = CurPos();
					State().m_parseState = VPS_ASSIGNMENT;
					State().m_defType = UNDEF;
				}
			}
		}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		else if (cw[0] == 'D' && cw[1] == 'Y' && StartsWith(cw, "DYNAMIC"))
		{
			// [case: 140598]
			State().m_defAttr |= V_VIRTUAL;
		}
#endif
		break;
	case 'e':
	case 'E':
		if (StartsWith(cw, "else"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT; // assignment
			State().m_defType = UNDEF;
		}
		else if (StartsWith(cw, "enum"))
		{
			State().m_defType = C_ENUM;
			if (GetLangType() == CS)
				State().m_defAttr |= V_MANAGED;
			TestForUnnamedStructs();
		}
		else if (StartsWith(cw, "event"))
		{
			if (JS != GetLangType())
			{
				if (m_deep && State(m_deep - 1).m_lastChar == '(' && FUNC == State(m_deep - 1).m_defType &&
				    VAR == State().m_defType && cw != State().m_begLinePos)
				{
					// [case: 72323]
					// function parameter whose name is event
				}
				else
				{
					// CLI events
					// http://msdn.microsoft.com/en-us/library/4b612y2s%28v=vs.100%29.aspx
					// in C++: "static event..." or "virtual event..."
					//		is just "event ..." allowed?
					// in C#: events can be static, virtual and all normal access modifiers
					// [case 58290]
					State().m_defType = EVENT;
				}
			}
		}
		else if (StartsWith(cw, "extern"))
			State().m_defAttr |= V_EXTERN;
		break;
	case 'f':
	case 'F':
		if (StartsWithNC(cw, "final") && IS_OBJECT_TYPE(State().m_defType))
		{
			// m_lastWordPos should be the class name, don't use "final"
			RewindScopePos();
		}
		else if (StartsWith(cw, "for") || StartsWith(cw, "foreach"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT; // assignment
			State().m_defType = UNDEF;
			State().AddParserStateFlags(VPSF_KEEPSCOPE_FOR);
		}
		break;
	case 'i':
	case 'I':
		if (cw[1] == 'n' || cw[1] == 'N')
		{
			if (StartsWith(cw, "inline"))
			{
				// [case: 141698] When the parser encounter an "inline namespace", defType is not yet set to be
				// NAMESPACE. For this reason I needed HandleUsingStatement() to be called in both cases
				HandleUsingStatement();
				if (State().m_defType != NAMESPACE) // [case: 141698]
				{
					State().m_defType = UNDEF;
					State().m_parseState = VPS_BEGLINE;
				}
			}
			else if (StartsWith(cw, "interface"))
			{
				State().m_defType = C_INTERFACE;
			}
			else if (StartsWith(cw, "internal"))
			{
				if (StartsWith(State().m_begLinePos, "internal"))
					State().m_privilegeAttr = V_INTERNAL;
				else
					State().m_privilegeAttr |= V_INTERNAL;
			}
		}
		else if (StartsWith(cw, "if"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT; // assignment
			State().m_defType = UNDEF;
			if (IsCFile(GetLangType()))
				State().AddParserStateFlags(VPSF_KEEPSCOPE_IF);
		}
		else if (StartsWith(cw, "import"))
		{
#ifdef RECORD_IDL_IMPORTS
			if (Idl == GetLangType())
				OnDirective();
#endif
		}
		break;
	case 'l':
	case 'L':
		if (CS == GetLangType() && StartsWith(cw, "lock"))
		{
			// [case: 104206]
			State().m_defType = UNDEF;
			State().m_parseState = VPS_ASSIGNMENT;
		}
		break;
	case 'n':
	case 'N':
		if (StartsWith(cw, "namespace"))
		{
			// watch out for "using namespace foo".  Not type NAMESPACE.
			if (!StartsWith(State().m_begLinePos, "using"))
				State().m_defType = NAMESPACE;
		}
		else if (StartsWith(cw, "new"))
		{
			// Case:1770 C# can have "int new Func(){}"
			// [case: 62065] but don't ignore unassigned "new Foo();"
			if (GetLangType() != CS || InLocalScope())
				State().m_parseState = VPS_ASSIGNMENT;
		}
		break;
	case 'o':
	case 'O':
		if (StartsWith(cw, "operator") && State().m_parseState != VPS_ASSIGNMENT)
		{
			State().m_defType = FUNC;
			// OnDef(); // Caused it to be added twice   Case 766:
			State().m_parseState = VPS_ASSIGNMENT; // assignment
		}
		break;
	case 'p':
	case 'P':
		if (cw[1] == 'r' || cw[1] == 'R')
		{
			if (StartsWith(cw, "private"))
			{
				if (StartsWith(State().m_begLinePos, "private"))
					State().m_privilegeAttr = V_PRIVATE;
				else
					State().m_privilegeAttr |= V_PRIVATE;
			}
			else if (StartsWith(cw, "protected"))
			{
				if (StartsWith(State().m_begLinePos, "protected"))
					State().m_privilegeAttr = V_PROTECTED;
				else
					State().m_privilegeAttr |= V_PROTECTED;
			}
		}
		else if (StartsWith(cw, "public"))
		{
			State().m_privilegeAttr = 0;
		}
		break;
	case 'q':
	case 'Q':
		if (StartsWith(cw, "Q_DECL_FINAL") && IS_OBJECT_TYPE(State().m_defType))
		{
			// m_lastWordPos should be the class name, don't use "Q_DECL_FINAL"
			RewindScopePos();
		}
		break;
	case 'r':
	case 'R':
		if (StartsWith(cw, "return"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT; // assignment
			State().m_defType = UNDEF;
		}
		break;
	case 's':
	case 'S':
	case 'u':
	case 'U':
		if (StartsWith(cw, "sealed") && IS_OBJECT_TYPE(State().m_defType))
		{
			LPCSTR cw2 = State().m_lastScopePos;
			if (!cw2 || StartsWith(cw2, "sealed")) // [case: 115473]
				RewindScopePos();                  // m_lastWordPos should be the class name, don't use "sealed"
			State().m_parseState = VPS_ASSIGNMENT;
		}
		else if (StartsWith(cw, "struct") || StartsWith(cw, "union"))
		{
			if (C_ENUM == State().m_defType && StartsWith(State().m_lastWordPos, "enum"))
				State().m_defAttr |=
				    V_MANAGED;                         // [case: 935] C++ managed enum is defined like "public enum struct Bar..."
			else if (C_INTERFACE != State().m_defType) // [case: 65644] public struct class foo...
			{
				State().m_defType = STRUCT;
				TestForUnnamedStructs();
			}
		}
		else if (StartsWith(cw, "using"))
		{
			HandleUsingStatement();
			// "using foo;" is not a definition.
			State().m_defType = UNDEF;
			State().m_parseState = VPS_ASSIGNMENT;
		}
		break;
	case 't':
	case 'T':
		if (StartsWith(cw, "throw"))
			State().m_parseState = VPS_ASSIGNMENT;
		else if (StartsWith(cw, "typedef"))
			State().m_defType = TYPE;
		else if (StartsWith(cw, "typename") 
			&& !StartsWith(State().m_begLinePos, "var") 
			&& !(State().m_defType == FUNC))
			State().m_defType = TEMPLATETYPE;
		break;
	case 'v':
		if (StartsWith(cw, "virtual"))
			State().m_defAttr |= V_VIRTUAL;
		break;
	case 'w':
	case 'W':
		if (StartsWith(cw, "while"))
		{
			State().m_lastScopePos = CurPos();
			State().m_parseState = VPS_ASSIGNMENT;
			State().m_defType = UNDEF;
		}
		break;
	case '_':
		if (cw[1] == '_')
		{
			if (StartsWith(cw, "__interface"))
				State().m_defType = C_INTERFACE;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			else if (StartsWith(cw, "__classmethod"))
			{
				// [case: 135861]
				State().m_defAttr |= V_CPPB_CLASSMETHOD;
			}
			else if (StartsWith(cw, "__closure"))
			{
				// [case: 135859]
				// don't look like we need any attributes for it
			}
			else if (StartsWith(cw, "__property"))
			{
				// [case: 133417]
				State().m_defType = PROPERTY;
			}
			else if (StartsWith(cw, "__published"))
			{
				// [case: 135860]
				if (StartsWith(State().m_begLinePos, "__published"))
					State().m_privilegeAttr = V_PUBLISHED;
				else
					State().m_privilegeAttr |= V_PUBLISHED;
			}
#endif
		}
		break;
	}
}

void VAParseKeywordSupport::RewindScopePos()
{
	// [case: 140771]
	// class foo::bar final/abstract/sealed
	// make sure recorded as :foo:bar rather than :bar
	LPCSTR newPos = State().m_lastWordPos;
	while (newPos >= State().m_begLinePos)
	{
		char ch = *(newPos - 1);
		if (':' == ch || '.' == ch || ISCSYM(ch))
			--newPos;
		else
			break;
	}

	State().m_lastScopePos = newPos;
}

////////////////////////////////////////////////////////////////////////////
// Adds Macro expansion
static ThreadStatic<int> sUnnamedStructRecurseCnt;

VAParseMPMacroC::VAParseMPMacroC(int fType)
    : VAParseDirectiveC(fType)
{
	m_inMacro = 0;
	m_processMacros = FALSE;
	m_parseTo = MAXLONG;
	mConditionallyProcessedMacroBraceDepth = 0;
}

WTString EatBeginningExpression(WTString name)
{
	WTString retval;
	const int origLen = name.length();
	int idx = 0;
	for (int nestingDepth = 0; idx < origLen; ++idx)
	{
		char cur = name[idx];
		if (nestingDepth)
		{
			if (strchr("[{(", cur))
				++nestingDepth;
			else if (strchr("]})", cur))
			{
				if (!--nestingDepth)
				{
					if (strchr(";,", name[++idx]))
						++idx;
					break;
				}
			}

			continue;
		}

		bool doBreak = false;
		if (::wt_isspace(cur))
		{
			doBreak = true;
			for (; idx < origLen; ++idx)
			{
				cur = name[idx];
				if (!::wt_isspace(cur))
					break;
			}
		}

		if (strchr("[{(", cur))
		{
			++nestingDepth;
			continue;
		}

		if (doBreak)
			break;

		if (::wt_ispunct(cur) && cur != '_' && cur != '$')
		{
			++idx;
			break;
		}
	}

	if (idx < origLen)
		retval = name.Mid(idx);
	retval.TrimLeft();
	return retval;
}

void VAParseMPMacroC::TestForUnnamedStructs()
{
	// this gets called for all languages
	if (!(Is_C_CS_File(GetLangType())))
		return;

	if (sUnnamedStructRecurseCnt() > 4)
	{
		vLog("WARN: TestForUnnamedStruct recursion");
		return;
	}

	// see if it is unnamed
	ReadToCls rtc(GetLangType());
	const WTString fragment(GetCStr(CurPos()));

	struct TempInc
	{
		TempInc()
		{
			sUnnamedStructRecurseCnt()++;
		}
		~TempInc()
		{
			sUnnamedStructRecurseCnt()--;
		}
	};

	TempInc ti;
	LPCTSTR readPos = CurPos() + fragment.GetLength();
	const int readLen = ::GetSafeReadLen(readPos, GetBuf(), GetBufLen() - (GetCp() + fragment.GetLength()));
	WTString name = rtc.ReadTo(readPos, readLen, ";{");
	while (!name.IsEmpty())
	{
		if (name[0] == '_' && (0 == name.Find("_CRT_ALIGN") || 0 == name.Find("__attribute__") ||
		                       0 == name.Find("__declspec") || 0 == name.Find("_declspec")))
		{
			name = ::EatBeginningExpression(name);
		}
		else
			break;
	}

	if (name.GetLength())
	{
		// [case: 62046]
		if (name[0] != ':' || C_ENUM != State().m_defType)
			return;

		name.Empty();
	}

	if (!rtc.CurChar()) // if !rtc.CurChar(), then we are in a macro or eof, in either case, don't use unnamed
		return;

	LPCSTR p = State().m_begLinePos;
	LPCSTR ep;
	for (ep = p; *ep && ep < CurPos(); ep++)
		;
	for (; *ep && ISCSYM(*ep); ep++)
		;
	const WTString line(p, ptr_sub__int(ep, p));
	State().m_parseState = VPS_NONE;
	WTString theType;
	switch (State().m_defType)
	{
	case C_ENUM:
		theType = "enum";
		break;
	case STRUCT:
		if (line == "union" || line == "typedef union")
			theType = "union";
		else
			theType = "struct";
		break;
	default:
		// what else can be unnamed?
		theType = "other";
	}

	WTString uname;
	uname.WTFormat(" unnamed_%s_%x_%lu_%lx", theType.c_str(), GetScopeInfoPtr()->GetFileID(), m_curLine,
	               m_deep * 100 + State().m_conditionalBlocCount);
	WTString code(line + uname);
	State().m_begLinePos = code.c_str();
	ExpandMacroCode(code);
}

void VAParseMPMacroC::ExpandMacroCode(const WTString& code)
{
	// Expand Macro
	DEFTIMERNOTE(VAP_ProcessMacro3, NULL);
	BOOL setScopePos = (State().m_lastScopePos == CurPos());

	//	const LPCSTR kPrevLastWordPos = State().m_lastWordPos;
	const LPCSTR oldBuf = m_buf;
	const int oldLen = mBufLen;
	const int oldcp = m_cp;
	const ULONG ln = m_curLine;
	m_cp = 0;

	// [case 59270] keep local copy
	mBufCache.push_back(code);
	m_buf = code.c_str();
	mBufLen = code.GetLength();

	// [case: 61448] this fixes the old asserts, but breaks 2 unit tests...
	// State().m_begLinePos = code;

	if (setScopePos)
		State().m_lastScopePos = CurPos();
	m_inMacro++;
	_DoParse();
	m_inMacro--;
	m_cp = oldcp;
	m_buf = oldBuf;
	mBufLen = oldLen;
	_ASSERTE(m_cp <= mBufLen);
	m_curLine = ln;

	//	if (!setScopePos)
	//	{
	//		if (m_deep == kStartDepth &&
	//			State().m_lastWordPos == State().m_lastScopePos)
	//		{
	//			LPCSTR curPos = CurPos();
	//			if ((State().m_lastWordPos == kPrevLastWordPos && kPrevLastWordPos && ISCSYM(*kPrevLastWordPos)) //
	//[case: 114224] processing resulted in no change
	//				||
	//				(curPos && ISCSYM(*curPos))) // [case: 114223] processing changed both lastWordPos and lastScopePos
	//			{
	//				// set m_lastScopePos after processing macro like:#vaparseUpdateLastScopePos
	//				if (State().m_parseState == VPS_BEGLINE)
	//				{
	//					if (State().m_lastChar != '.' && State().m_lastChar != '~' && State().m_lastChar != '!')
	//					{
	//						if (State().m_defType == FUNC || State().m_lastChar == ',') // _Check_return_ inline int
	// fpclassify(_In_ float _X) throw()
	//						{
	//							// similar to symptoms of case 61448
	//							_asm nop;
	//							// this breaks native unit test macros
	//							// State().m_lastScopePos = curPos;
	//						}
	//						else if (UNDEF == kPrevDefType && State().m_defType == VAR && State().m_lastWordPos !=
	// kPrevLastWordPos)
	//						{
	//							_asm nop;
	//							// this breaks main.cpp_VaStdafxMacroDefineMem
	//							// State().m_lastScopePos = curPos;
	//						}
	//					}
	//				}
	//			}
	//		}
	//	}
}

BOOL VAParseMPMacroC::ConditionallyProcessMacro()
{
	if (!IsCFile(GetFType()))
		return FALSE;

	// the following checks are used to prevent slowdown of GotoDef, MIF and outline parses.
	// the old behavior of ProcessMacro was just "return FALSE"
	if (mConditionallyProcessedMacroBraceDepth)
	{
		// [case: 68625]
		// always check when there are outstanding braces
	}
	else
	{
		// [case: 68625]
		// check parseState && localScope only if there are no outstanding braces.
		// otherwise me might not close a brace that was expanded by macro.
		if (InLocalScope())
			return FALSE;

		if (VPS_NONE != State().m_parseState)
		{
			bool checkForMacro = false;
			if ('_' == CurChar() && '_' == NextChar() && 'a' == m_buf[m_cp + 2])
			{
				const WTString sym(GetCStr(CurPos()));
				if (sym == "__attribute__")
				{
					// [case: 95299]
					// must read to end of __attribute__ via VaMacroDefArg
					checkForMacro = true;
				}
			}

			if (!checkForMacro)
			{
				if (VPS_BEGLINE == State().m_parseState && State().m_defType != UNDEF)
				{
					// [case: 99486]
				}
				else
					return FALSE;
			}
		}
	}

	const WTString sym(GetCStr(CurPos()));
	DType* pMacro = GetMacro(sym);
	if (!pMacro)
		return FALSE;

	if (pMacro->type() != VaMacroDefArg && pMacro->type() != VaMacroDefNoArg)
	{
		_ASSERTE(pMacro->type() == VaMacroDefArg || pMacro->type() == VaMacroDefNoArg);
		return FALSE;
	}

	const WTString macroDef(pMacro->Def());
	if (!pMacro->IsVaStdAfx()) // [case: 69244]
	{
		if (pMacro->IsSystemSymbol())
		{
			if (0 == sym.Find("STDMETHODIMP"))
				; // [case: 71278]
			else
				return FALSE;
		}
		else if (-1 == macroDef.FindOneOf("{}"))
		{
			if (m_deep && State(m_deep - 1).m_defType == C_ENUM)
			{
				// [case: 99945]
				// process macros that create enum values
			}
			else if (strstrWholeWord(macroDef, "interface") || strstrWholeWord(macroDef, "class") ||
			         strstrWholeWord(macroDef, "struct"))
			{
				// [case: 99795]
				// process macros that create interface/class/struct
			}
			else
			{
				return FALSE;
			}
		}
	}

	// [case: 14901] process macros in source files if they have brace in def.

	// This is not a virtual call since VAParseMPMacroC::ConditionallyProcessMacro is
	// not virtual but is called from derived classes ProcessMacro virtual
	// implementations.  VAParseMPMacroC::ConditionallyProcessMacro is used
	// to conditionally run VAParseMPMacroC::ProcessMacro.
	const BOOL retval = VAParseMPMacroC::ProcessMacro();
	if (retval)
	{
		// [case: 68625]
		const int kOpenBraces = macroDef.GetTokCount('{');
		const int kCloseBraces = macroDef.GetTokCount('}');
		mConditionallyProcessedMacroBraceDepth += kOpenBraces - kCloseBraces;
		if (mConditionallyProcessedMacroBraceDepth < 0)
			mConditionallyProcessedMacroBraceDepth = 0;
	}

	return retval;
}

BOOL VAParseMPMacroC::ProcessMacro()
{
	DEFTIMERNOTE(VAP__ProcessMacro, NULL);
	// see if cur sym is a macro if so, expand it
	//	if(!m_processMacros)
	{
		// Handle foreach(object o in lst) // should go in C# class
		if (InParen(m_deep) && StartsWith(CurPos(), "in"))
		{
			LPCSTR pw = State(m_deep - 1).m_lastWordPos;
			if (StartsWith(pw, "each") || StartsWith(pw, "foreach"))
			{
				OnDef();
				State().m_parseState = VPS_ASSIGNMENT;
				return FALSE;
			}
		}
	}

	if (m_inMacro > 10)
	{
		DebugMessage("Macro parsing error");
		return FALSE;
	}
	DEFTIMERNOTE(VAP_ProcessMacro1, NULL);

	WTString sym = GetCStr(CurPos());
	DType* const pMacro = GetMacro(sym);
	if (!pMacro)
		return FALSE;

	pMacro->LoadStrs();
	if ((pMacro->type() == VaMacroDefArg || pMacro->type() == VaMacroDefNoArg) && pMacro->SymMatch("_T"))
	{
		// [case: 64703]
		// only treat this as a macro if followed by an open paren
		if ((m_cp + sym.GetLength()) >= mBufLen)
			return FALSE;

		LPCSTR p = &m_buf[m_cp + sym.GetLength()];
		if (p[0] != '(' && p[1] != '(')
			return FALSE;
	}

// 	WTString pMacroDef(pMacro->Def());
	WTString macTxt = /*pMacroDef*/ pMacro->Def();
	macTxt.Trim();

	int cp = m_cp + sym.GetLength();
	if (cp > mBufLen)
		return FALSE;
	// Have args?  MACRO(a, b, ...)
	while (cp < mBufLen && (m_buf[cp] == ' ' || m_buf[cp] == '\t')) // there may be whitespace before args
		cp++;

	if (cp > mBufLen)
		return FALSE;

	if (State().m_ParserStateFlags == 0 && Psettings->mEnhanceMacroParsing &&
	    ((TYPE == State().m_defType && State().m_parseState == VPS_BEGLINE) ||
	     (UNDEF == State().m_defType && State().m_parseState == VPS_ASSIGNMENT)))
	{
		const WTString firstWord = GetCStr(State().m_begLinePos);
		if (firstWord.GetLength())
		{
			// [case: 108482]
			if (firstWord[0] == 't' && firstWord == "typedef")
			{
				if (!pMacro->IsVaStdAfx())
					return FALSE;
			}
			else if (firstWord[0] == 'u' && firstWord == "using")
			{
				if (!pMacro->IsVaStdAfx())
					return FALSE;
			}
		}
	}

	ULONG lines = 0;
	if (m_buf[cp] == '(')
	{
		for (int i = 0; i <= 20 && m_buf[cp]; i++)
		{
#ifdef DEBUG_VAPARSE
			if (!(m_buf[cp] == '(' || m_buf[cp] == ','))
				DebugMessage("ProcessMacro1");
#endif // DEBUG_VAPARSE
			cp++;
			ReadToCls rtc(GetLangType());
#define MAX_MACROLEN 5000 // long enough?
			WTString arg = rtc.ReadTo(&m_buf[cp], GetBufLen() - cp, ",)", MAX_MACROLEN);
			if (arg.find_first_of("'\"") == -1)
			{
				// [case: 64029]
				arg.TrimRight();

				// this condition is simply an attempt to reduce chance of
				// negative performance impact of the change (not based on
				// anything other than the test cases I used)
				if (i < 5 && m_inMacro < 2)
				{
					if (!arg.IsEmpty())
					{
						const WTString sym2(GetCStr(arg));
						if (!sym2.IsEmpty())
						{
							// [case: 118939] don't expand BOOST_* macros whose first arg is #
							if (i != 0 || m_buf[cp] != '#' || -1 == StartsWith(sym2, "BOOST_", FALSE))
							{
								DType* pMacro2 = GetMacro(sym2);
								if (pMacro2)
								{
									_ASSERTE(pMacro2->type() == VaMacroDefArg || pMacro2->type() == VaMacroDefNoArg);
									MultiParsePtr mp(m_mp);
									mp->SetFilename(m_mp->GetFilename());
									// [case: 99945] arg itself is a macro
									arg = ::VAParseExpandAllMacros(mp, arg);
								}
							}
						}
					}
				}

				WTString rstr;
				rstr.WTFormat("~~~%.2d", i + 1);
				macTxt.ReplaceAll(rstr, arg);

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
				if (sym[0] == '_' && !arg.IsEmpty() && arg[0] == 'd' && arg == "dynamic" && sym == "__declspec")
				{
					// [case: 140598]
					// __declspec is a vastdafx macro, so we don't see it in OnCSym.
					// have to handle dynamic param here in ProcessMacro.
					State().m_defAttr |= V_VIRTUAL;
					if (State().m_parseState == VPS_NONE)
						State().m_begLinePos = &m_buf[m_cp];
				}
#endif
			}

			cp += rtc.GetCp();
			_ASSERTE(cp <= mBufLen);
			lines += rtc.CurLine() - 1;
			if (rtc.CurChar() == ')')
			{
				cp++; // set to the end "MACRO(...)"
				break;
			}
		}
	}

	// Looking For Scope, don't actually expand this one
	if (!ShouldExpandEndPos(cp))
		return FALSE;

	if (State().m_parseState == VPS_BEGLINE && ('=' == m_buf[cp] || ';' == m_buf[cp]) && m_buf[cp + 1] != '=')
	{
		// About to define a symbol, but the symbol is already defined as a macro
		// returning here prevents expansion allowing the symbol to be defined
		/*
		        #define Foo() Bar()
		        int Foo();  // not expanding allows Foo to be both a method and macro
		        // Also fixes
		        #define AfxDiagnosticInit() TRUE
		        BOOL AFXAPI AfxDiagnosticInit(void);// Parser used to expand this to "BOOL AFXAPI TRUE(void);"
		   redefining TRUE
		*/
		//  #define PACKED()\n int foo PACKED; //allow foo to be defined
//		WTString pMacroDef(pMacro->Def());
		BOOL isempty = TokenGetField(/*pMacroDef*/ pMacro->Def_sv().first.data(), " \t").GetLength() == 0;
		if (!isempty && !pMacro->IsVaStdAfx()) // Allow misc/stdafx macros expand __attribute__ and __gc
			return FALSE;
	}
	if (macTxt.contains("~~~")) // some arg did not expand
	{
		DebugMessage("Macro missing arg");
		return FALSE;
	}

	if (macTxt.GetLength())
	{
		_ASSERTE(HasSufficientStackSpace());
#if !defined(SEAN)
		try
		{
#endif // !SEAN
			{
				// Prevent recursion
				AutoLockCs lck(sMacroExpandLock);
				pMacro->FlipExpand();
				ExpandMacroCode(macTxt);
				pMacro->FlipExpand();
			}

			if (State().m_begLinePos == macTxt)
			{
				// [case: 61448] workaround for ExpandMacroCode / State().m_begLinePos problem?
				State().m_begLinePos = CurPos(); // set line pointer to current macro, not expanded text for def
				_ASSERTE(State().m_begLinePos >= GetBuf() && State().m_begLinePos <= (GetBuf() + GetBufLen()));
			}
#if !defined(SEAN)
		}
		catch (...)
		{
			pMacro->DontExpand();
			DebugMessage("ProcessMacro Exception");
			VALOGEXCEPTION("VAP:");
			return FALSE;
		}
#endif // !SEAN
	}
	// else, just eat macro
#ifdef DEBUG_VAPARSE
	if (!m_inMacro)
	{
		ULONG nlines = 0;
		for (int tmp = m_cp; tmp < cp && tmp < mBufLen; tmp++)
		{
			if (m_buf[tmp] == '\n')
				nlines++;
		}

		if (nlines != lines)
			DebugMessage("MacroLineCount mismatch");
	}

	if (InComment())
	{
		ASSERT_ONCE(!"macro left us in a comment"); // Macro should not have left us in a comment
		OnComment(NULL);
	}
#endif // DEBUG_VAPARSE

	OnComment('\0'); // Just to make sure
	if (!m_inMacro && lines)
		m_curLine += lines;
	m_cp = cp - 1; // leave the last ')' or char of macro, m_cp will be incremented after return
	_ASSERTE(m_cp <= mBufLen);

	return TRUE;
}

void VAParseMPMacroC::OnDirective()
{
	const int pos = Idl != GetLangType() || m_buf[m_cp] == '#' ? m_cp + 1 : m_cp;
	_ASSERTE(m_mp);
	m_mp->SetBuf(m_buf, (ULONG)mBufLen, (ULONG)pos, m_curLine);
	m_mp->AddPPLn();
	VAParseDirectiveC::OnDirective();
	m_mp->ClearBuf();
}

void VAParseMPMacroC::DoParse()
{
	_ASSERTE(HasSufficientStackSpace());
	// Set lock for access to m_mp
	DB_READ_LOCK;
	VAParseDirectiveC::DoParse();
}

BOOL VAParseMPMacroC::ShouldExpandEndPos(int ep)
{
	if (m_parseTo && m_parseTo <= (ep - 1))
		return FALSE;
	return TRUE;
}

DType* VAParseMPMacroC::GetMacro(WTString sym)
{
	if (sym.IsEmpty())
		return nullptr;

	switch (sym[0])
	{
	case 'c':
		if (sym == "class")
			return nullptr;
		break;
	case 'n':
		if (sym == "nullptr")
			return nullptr;
		break;
	case 's':
		if (sym == "struct")
			return nullptr;
		break;
	case 't':
		if (sym == "typedef")
			return nullptr;
		break;
	}

	DType* pMacro;
	if (Psettings->mEnhanceMacroParsing && IsCFile(GetFType()))
	{
		// [case: 108472]
		// this call is similar to what would happen if #FunkyMacroParsingRegistryFlags
		// LimitMacroParsing was disabled
		pMacro = m_mp->GetMacro2(sym);
	}
	else
		pMacro = m_mp->GetMacro(sym);

	if (!pMacro || pMacro->IsDontExpand())
		return nullptr;

	return pMacro;
}

////////////////////////////////////////////////////////////////////////////
// Cache state for large files

static CCriticalSection sCacheLock;
VAParseMPMacroC gCachedParseMpInstance(
    Other); // shared cache pos, so scope, underlinescreen and symfrompos can all use it
MultiParsePtr s_lastMp;

void VAParseMPCache::LoadFromCache(int firstLn)
{
	if (!m_useCache)
	{
		_ASSERTE(!m_updateCachePos);
		return;
	}

	AutoLockCs lock(sCacheLock);

	m_cached = FALSE;
	// See if we can use cached pos
	if (s_lastMp == m_mp && gCachedParseMpInstance.CurLine() < (ULONG)firstLn &&
	    (!m_parseTo || m_parseTo > gCachedParseMpInstance.GetCp()) && gCachedParseMpInstance.GetCp() < mBufLen)
	{
		mBufCache = gCachedParseMpInstance.mBufCache;
		LoadParseState(&gCachedParseMpInstance, false);
		m_inDirective = FALSE;
	}
	else
	{
		if (s_lastMp != m_mp && m_updateCachePos)
		{
			s_lastMp = nullptr;
		}

		// should this happen even if !m_updateCachePos?
		_ASSERTE(NULL == gCachedParseMpInstance.m_mp);
		gCachedParseMpInstance.m_mp = nullptr;
	}

	// clear lwData, db could have been flushed and left with invalid pointers
	for (ULONG i = 0; i < m_deep; i++)
		State(i).m_lwData.reset();
}

void VAParseMPCache::CachePos()
{
	if (!m_useCache)
	{
		_ASSERTE(!m_updateCachePos);
		return;
	}

	if (m_updateCachePos && !m_cached && !m_inMacro)
	{
		AutoLockCs lock(sCacheLock);
		gCachedParseMpInstance.mBufCache = mBufCache;
		gCachedParseMpInstance.LoadParseState(this, true);
		s_lastMp = m_mp;
		m_cached = TRUE;
	}
}

void VAParseMPCache::OnNextLine()
{
	if (m_curLine > (m_firstVisibleLine - 20))
	{
		CachePos();
	}
	VAParseMPMacroC::OnNextLine();
}

////////////////////////////////////////////////////////////////////////////
// Simple Reparse Class
// Add definitions as needed while parsing scope
void VAParseMPReparse::HandleUsingStatement()
{
	if (!m_writeToFile)
		return;

	if (m_parseGlobalsOnly && mParseLocalTypes && InLocalScope())
	{
		// [case: 81110] [case: 81111]
		return;
	}

	token2 ln = TokenGetField(&m_buf[m_cp], "\r\n;");
	if (m_mp && gShellAttr->IsDevenv11OrHigher())
		ln = VAParseExpandAllMacros(m_mp, ln); // Expand _STD macro [case=61468]
	ln.read(" \t\r\n");                        // strip "using" or "Imports"
	WTString ns = ln.read("= \t\r\n;");
	if (GetLangType() == CS && ns.GetLength() && ns[0] == '(')
		return; // variable declaration: using (FooType var = new FooType)

	BOOL isNamespace = (ns == "namespace");

	// [case: 141698] exit unless "inline namespace" is present
	if (State().m_defType != NAMESPACE && StartsWith(CurPos(), "inline") && !isNamespace)
		return;

	if (isNamespace)
		ns = ln.read(); // managed C++ "using namespace System::Data;"

	// [case: 141698] generating correct namespace scope
	if (State().m_defType == NAMESPACE)
	{
		ns = TokenGetField(&m_buf[m_cp], "\r\n;");
		bool mergeMode = false;
		WTString merged;
		ns.ReplaceAll("::", ":");
		for (int i = 0; i < ns.GetLength(); i++)
		{
			if (i + 6 < ns.GetLength() && ns[i] == 'i' && ns[i + 1] == 'n' && ns[i + 2] == 'l' && ns[i + 3] == 'i' &&
			    ns[i + 4] == 'n' && ns[i + 5] == 'e' && IsWSorContinuation(ns[i + 6]))
			{
				mergeMode = true;
			}
			if (mergeMode)
				merged += ns[i];
			if (ns[i] == ':')
				mergeMode = false;
		}

		ns = merged;
		isNamespace = TRUE;
		ns.ReplaceAll("inline", "");
		ns.ReplaceAll(" ", "");
		ns.ReplaceAll("\t", "");
		ns.ReplaceAll("\\", "");
		while (ns.GetLength() && ns[ns.GetLength() - 1] == ':')
			ns = ns.Left(ns.GetLength() - 1);

		ns.ReplaceAll(":", ".");
	}

	WTString op = ln.read(" \t\r\n");
	if (op.GetLength() > 1 && op[0] == '=')
	{
		// [case: 95849]
		const WTString tmp(ln.Str());
		ln = op.Mid(1) + tmp;
		op = '=';
	}

	if (op == "=")
	{
		// [case: 87178]
		WTString newScp(Scope());
		const int len = newScp.GetLength();
		if (len && newScp[len - 1] != DB_SEP_CHR && ns.GetLength() && ns[0] != DB_SEP_CHR)
			newScp += DB_SEP_CHR;
		newScp += ns;

		WTString usingDef = ln.read("\r\n;");
		usingDef.Trim();
		EncodeTemplates(usingDef);

		if (GetScopeInfoPtr()->IsWriteToDFile() && GetCStr(State().m_lastScopePos) == "using")
		{
			if (GetCStr(State().m_begLinePos) == "template")
			{
				// [case: 93229]
				// template<typename T> using Foo = std::vector<T>;
				// skip "using" to record sym ":Foo:< >" with def "<typename T>"
				AddTemplateParams(newScp);
			}
		}

		uint attrs = 0;
		uint defType = TYPE;
		if (usingDef.GetLength())
		{
			int pos = usingDef.Find('*');
			if (pos == usingDef.GetLength() - 1)
			{
				// [case: 109767]
				// using FooPtr = Foo*;
				attrs = V_POINTER;
			}

			pos = usingDef.Find('(');
			if (-1 != pos)
			{
				int pos2 = usingDef.Find(')');
				if (-1 != pos2 && pos2 > pos && -1 == usingDef.Find("((") && -1 == usingDef.Find("))"))
				{
					// [case: 109937]
					// using FuncUsing = LRESULT(CALLBACK*)(int);
					defType = FUNC;
				}
			}
		}

		OnAddSymDef(newScp, usingDef, defType, attrs);
	}
	else if (ns.GetLength())
	{
		ns.ReplaceAll("::", ".");
		// Do not do a NetImport here in c++, it could just be #using some_cpp_namespace
		// In c++, the references will handle this
		if (!m_parseGlobalsOnly && !IsCFile(GetLangType()))
		{
			//			NetImport(ns);
		}

		ns.ReplaceAll(".", DB_SEP_STR);
		if (ns[0] != DB_SEP_CHR)
			ns.prepend(DB_SEP_STR.c_str());

		VAParseMPReparse* pRP = (VAParseMPReparse*)this;
		if (!isNamespace && (GetLangType() == Src || GetLangType() == Header))
		{
			if (ns.GetTokCount(DB_SEP_CHR) < 2)
				return; // ignore "using ::size_t;" since globals are already accessible

			if (m_deep)
			{
				// "using baseclass::method" when in a derived class or a nested namespace (case=972)
				const WTString sym(StrGetSym(ns));
				const WTString scp(Scope() + ":" + sym);
				if (scp.GetLength() && sym.GetLength())
				{
					// BaseClassFinder::GetDataForSym will figure it out (case=9436)
					if (ns[0] == DB_SEP_CHR)
						ns = ns.Mid(1);
					// assume is a FUNC for sake of coloring
					pRP->OnAddSymDef(scp, WTString("using ") + CleanScopeForDisplay(ns), FUNC, 0);
				}
				return;
			}

			// using boost::smart_ptr; using std::ifstream; using std::vector;
			WTString usingCls = ns;
			if (StrGetSymScope(ns).GetLength())
			{
				// Found another case where this type of using was messing up scope in other files
				// Taking another approach, I tried reinterpreting "using foo::bar" to "typedef foo::bar bar"
				// Seems to work in most cases?  See uses for "using OWL::string" in EdCntrl
				// We do not want "OWL" in the using string
				WTString cls = StrGetSym(usingCls);
				if (GetLangType() != Src || !m_parseGlobalsOnly) // don't add for Src files in ParseFileForGotoDef
					OnAddSymDef(DB_SEP_STR + cls, WTString("using ") + CleanScopeForDisplay(usingCls), TYPE, 0);
			}
			return;
		}
		else if (m_mp && !GetScopeInfoPtr()->IsSysFile())
		{
			// save using directives for AddSym (but not for sys namespaces)
			if (m_mp->SDictionary())
			{
				DType* decl = m_mp->SDictionary()->Find(ns);
				if (!decl)
				{
					mIsUsingNamespace = true;
					mUsingNamespaces.insert(ns);
				}
			}
		}

		// [case: 141698] insertion of an artificial using-directive when it encounters the inline keyword in a namespace definition
		WTString usingSymScope;
		if (State().m_defType == NAMESPACE && StartsWith(m_buf + m_cp, "inline"))
		{
			int counter = 0;
			for (LPCSTR lp = State().m_lastScopePos; *lp != 0 && *lp != '{' && counter < 128; lp++)
			{
				if (IsWSorContinuation(*lp))
					continue;
				if (*lp == 'i' && *(lp + 1) == 'n' && *(lp + 2) == 'l' && *(lp + 3) && *(lp + 4) &&
				    *(lp + 5) == 'e' && IsWSorContinuation(*(lp + 6)))
					break;

				usingSymScope += *lp;
				counter++;
			}

			usingSymScope.ReplaceAll("::", ":");
			usingSymScope = ":" + usingSymScope;
			int length = usingSymScope.GetLength();
			while (length && usingSymScope[length - 1] == ':')
				usingSymScope = usingSymScope.Left(length - 1);
		}
		else
			usingSymScope = Scope();

		if (usingSymScope == DB_SEP_STR)
		{
			_ASSERTE(m_mp);
			UINT fid = m_mp->GetFileID();
			_ASSERTE(fid);
			WTString fileScopedUsing;
			fileScopedUsing.WTFormat(":wtUsingNamespace_%x", fid);
			// add file level using statements as less general sym for better
			// hash table distribution and easier lookup
			usingSymScope = fileScopedUsing;
		}
		else
			usingSymScope += ":wtUsingNamespace";

		pRP->OnAddSymDef(usingSymScope, ns, RESWORD, V_HIDEFROMUSER | V_IDX_FLAG_USING);
	}
}

void VAParseMPReparse::AddTemplateParams(const WTString& theScope)
{
	_ASSERTE(StartsWith(State().m_begLinePos, "template") || StartsWith(State().m_begLinePos, "generic"));

	LPCSTR p = strchr(State().m_begLinePos, '<');
	if (!p)
		return;

	ReadToCls rtc(GetLangType());
	// [case: 86136]
	const bool kBegLinePosIsInBuf = State().m_begLinePos >= GetBuf() && State().m_begLinePos <= GetBuf() + GetBufLen();
	++p;
	const int readToLen = ::GetSafeReadLen(p, GetBuf(), GetBufLen());
	WTString def =
	    rtc.ReadTo(p, readToLen, ">", kBegLinePosIsInBuf && readToLen > 1024 && readToLen < kMaxLen ? readToLen : 1024);
	EncodeTemplates(def);
	if (rtc.CurChar() != '>')
		return;

	//	def.ReplaceAll("TYPE", "class", TRUE); // breaks "template <typename TYPE> class ..."
	def.ReplaceAll("const", "", TRUE);
	def.ReplaceAll("constexpr", "", TRUE);
	def.ReplaceAll("consteval", "", TRUE);
	def.ReplaceAll("constinit", "", TRUE);
	def.ReplaceAll("_CONSTEXPR17", "", TRUE);
	def.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
	def.ReplaceAll("_CONSTEXPR20", "", TRUE);
	def.TrimLeft();
	if (def.GetLength() > 0)
	{
		// [case: 14457] [case: 12798]
		// boost 1.37 does a forward declare like this
		// namespace boost {
		//    template <class>
		//    class shared_ptr;
		// }
		// don't add template args for forward decls
		if (def != "class" && def != "typename")
		{
			_ASSERTE(GetScopeInfoPtr()->IsWriteToDFile() == m_writeToFile);
			const WTString templScope = theScope + ":< >";
			State(m_deep).m_defAttr |= V_TEMPLATE;
			// [case: 24420] always add during DbOut
			// [case: 24839] except if found and isSysFile (system symbol
			// stdafx.h hacks are dependent upon 'bad' logic)
			DType* cd = m_mp->FindExact2(templScope, true, TEMPLATETYPE, false);
			if ((GetScopeInfoPtr()->IsWriteToDFile() && !GetScopeInfoPtr()->IsSysFile()) || !cd)
				OnAddSymDef(templScope, WTString('<') + def + '>', TEMPLATETYPE, V_HIDEFROMUSER);
		}
	}

	LPCSTR newBegLinePos = rtc.CurPos() + 1;
	// the !kBegLinePosIsInBuf condition is giving up on safety that the assert is for
	_ASSERTE(!kBegLinePosIsInBuf || (newBegLinePos >= GetBuf() && newBegLinePos <= (GetBuf() + GetBufLen())));
	State().m_begLinePos = newBegLinePos;
}

// does not support C++14 single-quote separators (since this is used in comments rather than literals)
static bool IsHexNumber(const WTString& hashTag, int offset)
{
	if (offset >= hashTag.GetLength())
		return false;

	for (int i = offset; i < hashTag.GetLength(); ++i)
	{
		auto ch = hashTag[i];
		if ('-' == ch && Psettings->mHashtagsAllowHypens)
			; // [case: 90329]
		else if (!wt_isdigit(ch) && !wt_isxdigit(ch))
		{
			return false;
		}
	}
	return true;
}

// does not support C++14 single-quote separators (since this is used in comments rather than literals)
static bool IsNumber(const WTString& hashTag)
{
	if (hashTag[0] == '0' && (hashTag[1] == 'x' || hashTag[1] == 'X'))
	{
		// strict hex
		return IsHexNumber(hashTag, 2);
	}
	else if (hashTag[0] == 'x' || hashTag[0] == 'X')
	{
		// strict hex (possible false negatives like "xfade")
		return IsHexNumber(hashTag, 1);
	}
	else if (Psettings->mIgnoreHexAlphaHashtags)
	{
		// case 89172
		// possible false negatives like "deadbeef", "bad"
		return IsHexNumber(hashTag, 0);
	}
	else
	{
		// fuzzy logic
		int numDigits = 0;
		int numHexLetters = 0;

		for (int i = 0; i < hashTag.GetLength(); ++i)
		{
			auto ch = hashTag[i];
			if (wt_isdigit(ch))
				++numDigits;
			else if (wt_isxdigit(ch))
				++numHexLetters;
			else if ('-' == ch && Psettings->mHashtagsAllowHypens && 0 != i)
				; // [case: 90329]
			else
				return false;
		}

		if (numHexLetters && !numDigits)
			return false; // eg, assume "face" is not hex
	}

	return true;
}

bool IsValidHashtag(const WTString& hashTag)
{
	if (hashTag.GetLength() < (int)Psettings->mMinimumHashtagLength)
		return false;

	if (hashTag == "define" || hashTag == "defined" || hashTag == "defines" || hashTag == "elif" || hashTag == "else" ||
	    hashTag == "endif" || hashTag == "endregion" || hashTag == "error" || hashTag == "errors" || hashTag == "if" ||
	    hashTag == "ifdef" || hashTag == "ifdefs" || hashTag == "ifndef" || hashTag == "ifndefs" || hashTag == "ifs" ||
	    hashTag == "import" || hashTag == "imports" || hashTag == "includ" || // "includ'ing"
	    hashTag == "include" || hashTag == "included" || hashTag == "includes" || hashTag == "including" ||
	    hashTag == "line" || hashTag == "pragma" || hashTag == "pragmas" || hashTag == "region" ||
	    hashTag == "regions" || hashTag == "undef" || hashTag == "undefs" || hashTag == "using" ||
	    hashTag == "usings" || hashTag == "warning" || hashTag == "warnings")
	{
		return false;
	}

	if (IsNumber(hashTag))
		return false;

	return true;
}

inline bool IsValidHashtagChar(char ch)
{
	return ISCSYM(ch) || (ch == '-' && Psettings->mHashtagsAllowHypens);
}

void VAParseMPReparse::OnHashtag(const WTString& hashTag, bool isRef)
{
	::ScopeInfoPtr pScopeInfo = GetScopeInfoPtr();
	if (pScopeInfo->IsWriteToDFile())
	{
		const bool kShowLineText = false;
		const bool kShowTrailingText = false;
		const bool kShowCommentText = true;

		WTString def;
		auto endOfTag = CurPos() + hashTag.GetLength();
		if (*endOfTag == ':')
		{
			auto startOfDef = endOfTag + 1;
			int endOfLine = (int)strcspn(startOfDef, "\r\n");
			def = WTString(startOfDef, endOfLine);

			int endOfComment = def.Find("*/");
			if (endOfComment >= 0)
				def = def.Left(endOfComment);

			int startOfNextTag = def.Find('#');
			if (startOfNextTag >= 0)
				def = def.Left(startOfNextTag);
			def.Trim();
		}
		else if (kShowLineText)
		{
			// shows entire line of text, comment + code
			auto startOfLine = CurPos();
			while (startOfLine > m_buf)
			{
				if (startOfLine[-1] == '\n' || startOfLine[-1] == '\r')
					break;
				--startOfLine;
			}

			int endOfLine = (int)strcspn(startOfLine, "\r\n");

			def = WTString(startOfLine, endOfLine);
			def.Trim();
		}
		else if (kShowCommentText)
		{
			// shows entire line of the comment that the tag is in (no-code)
			auto startOfLine = CurPos();
			while (startOfLine > m_buf)
			{
				if (startOfLine[-1] == '\n' || startOfLine[-1] == '\r')
					break;
				--startOfLine;
			}

			int endOfLine = (int)strcspn(startOfLine, "\r\n");
			def = WTString(startOfLine, endOfLine);

			switch (CommentType())
			{
			case '\n': {
				int startOfLineComment = def.Find("//");
				if (startOfLineComment >= 0)
					def = def.Mid(startOfLineComment + 2);
			}
			break;
			case '*': {
				int hashIdx = ptr_sub__int(CurPos(), startOfLine);
				int startOfBlockComment = 0;
				int nextStart = 0;
				while ((nextStart = def.Find("/*", startOfBlockComment)) >= 0 && nextStart < hashIdx)
				{
					startOfBlockComment = nextStart + 2;
				}
				def = def.Mid(startOfBlockComment);

				int endOfBlockComment = def.Find("*/");
				if (endOfBlockComment >= 0)
					def = def.Left(endOfBlockComment);
			}
			break;
			}

			def.Trim();

			/*
			** strip out pseudo-comment ascii art
			*/
			{
				int i = 0;
				for (i = 0; i < def.GetLength(); ++i)
				{
					char ch = def[i];
					if (!(wt_isspace(ch) || ch == '*'))
						break;
				}
				if (i > 0)
					def = def.Mid(i);
			}

			// strip out all hashtags at beginning of text
			while (def[0] == '#')
			{
				int i;
				for (i = 1; i < def.GetLength(); ++i)
				{
					if (!IsValidHashtagChar(def[i]))
						break;
				}

				WTString hashTag2 = def.Left(i);
				if (IsValidHashtag(hashTag2))
				{
					def = def.Mid(i);

					// strip out commas between leading tags
					int n;
					for (n = 0; n < def.GetLength(); ++n)
					{
						char ch = def[n];
						if (!(wt_isspace(ch) || ch == ','))
							break;
					}
					if (n > 0)
						def = def.Mid(n);
				}
				else
					break;
			}

			if (def[0] == ':')
			{
				def = def.Mid(1);
				def.TrimLeft();
			}
		}
		else if (kShowTrailingText)
		{
			// shows text that trails the tag, up until the next tag

			auto startOfDef = endOfTag;
			int endOfLine = (int)strcspn(startOfDef, "\r\n");
			def = WTString(startOfDef, endOfLine);

			int endOfComment = def.Find("*/");
			if (endOfComment >= 0)
				def = def.Left(endOfComment);

			int startOfNextTag = def.Find('#');
			if (startOfNextTag >= 0)
				def = def.Left(startOfNextTag);

			def.Trim();
		}

		uint attrs = V_HIDEFROMUSER;
		if (isRef)
			attrs |= V_REF;
		if (def.IsEmpty())
			def = " "; // make sure there something. Empty def is not distinguishable from not-loaded-yet
		m_mp->DBOut(":VaHashtag:" + hashTag, def, vaHashtag, attrs, (int)m_curLine);
	}
}

void VAParseMPReparse::OnChar()
{
	switch (CommentType())
	{
	case '\n':
	case '*': {
		auto curPos = CurPos();
		if (m_cp > 0 && curPos[0] == '#' && ISCSYM(curPos[1]))
		{
			char prevChar = curPos[-1];

			// #invalidHashtagPrefix  [case: 108807]
			// can't be preceded by a letter, or ## preproc operator, or '?', or '"' or '\\'
			if (ISCSYM(prevChar) || '#' == prevChar || '?' == prevChar || '"' == prevChar || '\\' == prevChar)
				break;

			// can't be preceded '/' except '//'
			if ('/' == prevChar && m_cp > 1 && curPos[-2] != '/')
				break;

			bool isRef = (':' == prevChar);

			auto tmpPos = curPos + 2;
			char ch;
			do
			{
				ch = *tmpPos++;
			} while (IsValidHashtagChar(ch));

			WTString hashTag = WTString(curPos + 1, ptr_sub__int(tmpPos, curPos) - 2);
			if (IsValidHashtag(hashTag))
			{
				OnHashtag("#" + hashTag, isRef);
			}
		}
	}
	break;
	}
	__super::OnChar();
}

void VAParseMPReparse::OnDef()
{
	if (m_inIFDEFComment || Is_VB_VBS_File(GetLangType()))
		return;

	// if reparsing, it looks to see if the current symbols is defined,
	// if not it adds it.
	DEFTIMERNOTE(VAP_OnDef, NULL);
	bool localOutVarDecl = false;
	if (!ShouldForceOnDef())
	{
		// MPScopeCls::OnDef(); // No need..
		if (!(m_curLine >= m_startReparseLine && m_curLine <= m_stopReparseLine))
			return;

		if (State().m_begLinePos == State().m_lastScopePos && !strchr(";{", State().m_lastChar) && GetLangType() != JS)
		{
			if (LwIS("class") || LwIS("struct") || LwIS("union") || LwIS("interface") || LwIS("__interface"))
			{
				//			// unnamed structs, change "struct {...}" to "struct unnamed123 {...}"
				//			WTString lw = GetCStr(State().m_lastWordPos);
				//			ClearStatement(CurPos());
				//			ExpandMacroCode(lw + " unnamed" + itos(m_deep*100 + State().m_conditionalBlocCount));
			}
			else if (State().HasParserStateFlags(VPSF_GOTOTAG))
			{
				if (!GetScopeInfoPtr()->IsWriteToDFile() ||
				    !InLocalScope()) // prevent typing "CWnd:" from adding CWnd as a goto_tag
					return;
			}
			else if (InLocalScope() && State().m_defType != C_ENUMITEM) // allow enum items
			{
				if ('(' == State(m_deep - 1).m_lastChar && VAR == State().m_defType && CS == GetLangType() &&
				    StartsWith(State().m_lastScopePos, "out"))
				{
					// [case: 116073]
					// this occurs on first variable in function call
					localOutVarDecl = true;
				}
				else
				{
					// allow foo(); or foo(){}, but not bar in foo(bar);
					return;
				}
			}
		}

		if (wt_isdigit(State().m_lastScopePos[0]))
			return;

		if (InParen(m_deep) && InLocalScope(m_deep - 1) && Lambda_Type != State(m_deep - 1).m_defType &&
		    !localOutVarDecl)
		{
			LPCSTR pw = State(m_deep - 1).m_lastWordPos;
			if (!StartsWith(pw, "for") && !StartsWith(pw, "if") && !StartsWith(pw, "each") &&
			    !StartsWith(pw, "foreach") && !StartsWith(pw, "while") && !StartsWith(pw, "catch") &&
			    !StartsWith(pw, "using"))
			{
				if (GetLangType() == CS)
				{
					pw = State().m_lastScopePos;
					if (VAR == State().m_defType && StartsWith(pw, "out"))
					{
						// [case: 116073]
						localOutVarDecl = true;
					}
					else if (VAR == State(m_deep - 1).m_defType)
					{
						// [case: 118894]
						// param in local function definition
						// defType of parent scope/State hasn't been set to FUNC yet
					}
					else
						return;
				}
				else
					return;
			}
		}

		if (InParen(m_deep) && CurChar() == ')')
		{
			// need to scan ahead to see if this is a function pointer STDMETHOD(void foo)(int aaa);
			LPCSTR p;
			for (p = CurPos() + 1; *p && wt_isspace(*p); p++)
				;
			if (*p == '(')
				return;
		}
	}

	WTString methscope = MethScope(); // #parseBuildDefScope

	// [case: 116073]
	if (localOutVarDecl)
	{
		_ASSERTE(m_deep);
		if (m_deep)
		{
			// place declaration in parent scope
			const WTString methscopeNew = Scope(m_deep - 1);
			if (!methscopeNew.IsEmpty())
			{
				// and add the symbol name
				methscope = methscopeNew + DB_SEP_CHR + GetCStr(State().m_lastWordPos);
			}
		}
	}

	if (!methscope.GetLength() || (1 == methscope.GetLength() && ':' == methscope[0]) || methscope.contains(":_asm:"))
		return;

	if (methscope[methscope.GetLength() - 1] == EncodeChar('>') &&
	    CurChar() != '{' // allow template definitions, but not declarations
	)
	{
		// "extern template class basic_istream<int>;" // don't add forward def of template instance
		// or MakeTemplate will think it has already been created.  Case 8959: std::string no longer understood in VC6
		if (!StartsWith(State().m_lastScopePos, "operator")) // Case 8959 allow operators: "operator foo<int>();"
			return;
	}

	if (methscope.FindOneOf(". \t\r") != -1)
		return;

	if (StartsWith(State().m_begLinePos, "template") || StartsWith(State().m_begLinePos, "generic"))
	{
		// template <class T> class foo {}
		// add ":foo:< >, <class T>
		AddTemplateParams(methscope);
	}

	bool addToParentScope = false;
	int unnamedPos = -1;
	switch (State().m_defType)
	{
	case C_ENUMITEM:
		if (IsCFile(GetLangType()))
		{
			if (m_deep && State(m_deep - 1).m_defAttr & V_MANAGED)
				; // [case: 935] managed enums are not injected into parent scope
			else
				addToParentScope = true;
		}
		break;
	case C_ENUM:
		unnamedPos = methscope.Find(":unnamed_enum_");
		if (unnamedPos != -1)
			addToParentScope = true;
		break;
	default:
		if (m_deep && STRUCT == State(m_deep - 1).m_defType)
		{
			unnamedPos = methscope.ReverseFind(":unnamed_"); // could be unnamed_struct_ or unnamed_union_
			if (0 == unnamedPos)
				; // don't promote items at global scope
			else if (-1 != unnamedPos)
			{
				// don't add :UntaggedUnionInStruct:unnamed_union_5381071
				// but do add :UntaggedUnionInStruct:unnamed_union_5381071:mFoo
				const int nextColonPos = methscope.Find(":", unnamedPos + 1);
				if (-1 != nextColonPos)
				{
					// [case 4514]
					addToParentScope = true;

					const int langType = GetLangType();
					if (IsCFile(langType))
					{
						ReadToCls rtc(langType);
						const int kMaxRead = 2048;
						WTString tmp(rtc.ReadTo(CurPos(), GetLenOfCp(), "}", kMaxRead));
						if (!tmp.IsEmpty())
						{
							// [case: 91391]
							if (rtc.GetCp() < kMaxRead)
							{
								const int startOffset = rtc.GetCp() + 1;
								const int len = 16;
								const int endPos = GetCp() + startOffset + len;
								if (endPos > 0 && endPos <= m_parseTo && endPos <= GetBufLen())
								{
									tmp = WTString(CurPos() + startOffset, len);
									tmp.TrimLeft();
									if (!tmp.IsEmpty() && ::wt_isalpha(tmp[0]))
									{
										if (tmp.Find("DUMMYSTRUCTNAME") == -1 && tmp.Find("DUMMYUNIONNAME") == -1)
										{
											// [case: 3548] instance of an untagged struct
											// but not for DUMMYSTRUCTNAME* as defined in winnt.h
											addToParentScope = false;
										}
									}
								}
							}
						}
					}
				}
			}
		}
		break;
	}

	if (State().m_defType == FUNC || State().m_defType == GOTODEF)
	{
		// [case: 42266] If scope is template specialization, then add sym
		// to both regular and templated scope.  For example:
		// template<typename T> void Foo::Bar<T>() {}
		// add both of these to db:
		// :Foo<T>:Bar
		// :Foo:Bar
		WTString tmpScope = StrGetSymScope(methscope);
		if (tmpScope.GetLength() && tmpScope[tmpScope.GetLength() - 1] == EncodeChar('>'))
		{
			auto startPos = tmpScope.ReverseFind(EncodeChar('<'));
			if (startPos != -1)
			{
				tmpScope = tmpScope.Left(startPos);
				OnAddSym(tmpScope + DB_SEP_STR + StrGetSym(methscope));
			}
		}
	}

	if (addToParentScope)
	{
		// (Named or unnamed enums) and (unnamed structs and unions) are added
		// to both their parent scope and their unnamed scope so
		// 1) enum {a, b, c} e_var; e_var. can see them
		// 2) enum e_type {a, b, c}; e_type. can see them
		const WTString sym = StrGetSym(methscope);
		WTString parentScope;
		if (-1 == unnamedPos)
		{
			parentScope = StrGetSymScope(StrGetSymScope(methscope));
			OnAddSym(parentScope + DB_SEP_STR + sym);
		}
		else
		{
			while (-1 != unnamedPos)
			{
				parentScope = methscope.Mid(0, unnamedPos);
				OnAddSym(parentScope + DB_SEP_STR + sym);

				// case=4514 fix for members of nested unnamed structs
				unnamedPos = parentScope.ReverseFind(":unnamed_struct");
				int unnamedPos2 = parentScope.ReverseFind(":unnamed_union");
				if (!unnamedPos || -1 == unnamedPos)
					unnamedPos = unnamedPos2;
				if (!unnamedPos2 || -1 == unnamedPos2)
					unnamedPos2 = unnamedPos;

				if (unnamedPos2 > unnamedPos)
					unnamedPos = unnamedPos2;

				if (!unnamedPos)
					break; // don't bubble up at global scope
			}
		}
	}
	else if (m_deep && Psettings->mUnrealScriptSupport && State(m_deep - 1).HasParserStateFlags(VPSF_UC_STATE))
	{
		// states are added to both their parent scope
		// and their named scope so enum e_type {a, b, c}; e_type. can see them
		WTString sym = StrGetSym(methscope);
		WTString pscope = StrGetSymScope(StrGetSymScope(methscope));
		State().m_defAttr |= V_CONSTRUCTOR;
		OnAddSym(pscope + DB_SEP_STR + sym);
	}

	if (IS_OBJECT_TYPE(State().m_defType) && CurChar() == ';' && State().m_defType != TYPE)
	{
		// Look to see if this is a forward declaration...
		token2 ln = TokenGetField(State().m_begLinePos, "{;");
		std::vector<WTString> wds;
		while (ln.more() > 1)
		{
			// consider removing export, extern, declspec(x), import, etc here?
			wds.push_back(ln.read(" \t\r\n"));
		}

		bool isForwardDecl = true;
		if (wds.size() >= 3)
		{
			if (wds[0] != "friend" && wds[0] != "ref" && wds[0] != "value")
			{
				isForwardDecl = false;

				if (3 == wds.size() && wds[0] != "typedef")
				{
					if ((CLASS == State().m_defType && wds[0] != "class" && wds[1] == "class") ||
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
					    (CLASS == State().m_defType && wds[0] == "class" &&
					     (wds[1] == "DELPHICLASS" || wds[1] == "RTL_DELPHIRETURN" ||
					      wds[1] == "_DELPHICLASS_TOBJECT")) ||                                       // [case: 141968]
					    (STRUCT == State().m_defType && wds[0] == "struct" && wds[1] == "PACKAGE") || // [case: 141968]
					    (C_INTERFACE == State().m_defType && (wds[0] == "interface" || wds[0] == "__interface") &&
					     (wds[1] == "DELPHIINTERFACE" || StartsWith(wds[1], "DECLSPEC_UUID"))) || // [case: 141968]
#endif
					    (STRUCT == State().m_defType && wds[0] != "struct" && wds[1] == "struct") ||
					    (C_INTERFACE == State().m_defType && wds[0] != "interface" && wds[1] == "interface"))
					{
						// [case: 67379]
						// assume first word is storage or unexpanded macro
						isForwardDecl = true;
					}
				}
				else if (4 == wds.size() && wds[0] == "extern")
				{
					if (wds[1] == "class" || wds[1] == "struct" || wds[1] == "interface")
					{
						// [case: 55770]
						// extern struct StrName varName;
						isForwardDecl = true;
						State().m_defType = VAR;
					}
				}
			}
		}

		if (isForwardDecl)
		{
			// struct foo;
			// class bar;
			// ref class baz;
			// friend class bah;
			// XXXX class bam;
			// extern struct StrName varName;
			if (m_parseGlobalsOnly && m_writeToFile && Src == GetLangType())
			{
				// [case: 36934] don't add ForwardDeclares as GotoDef entries to
				// retain behavior that was in place before case 36934 was implemented
				return;
			}
			methscope.prepend(DBColonToSepStr(":ForwardDeclare").c_str());
			State().m_defAttr |= V_HIDEFROMUSER;
		}
		else
		{
			// typedef struct foo bar;
			// struct baz instance;
			// class bar instance;
			// class _EXPCLASS foo;
			// Line below breaks forward declares like "class _EXPCLASS string;"
			// State().m_defType = VAR; // "struct foo bar;"
		}
	}
	else if ((State().m_defType == FUNC || State().m_defType == VAR) && StartsWith(State().m_begLinePos, "friend"))
	{
		if (m_parseGlobalsOnly && m_writeToFile && Src == GetLangType())
			return;
		methscope.prepend(DBColonToSepStr(":ForwardDeclare").c_str());
		State().m_defAttr |= V_HIDEFROMUSER;
	}

	if (StartsWith(State().m_lastScopePos, "operator"))
	{
		// nab operator chars
		LPCSTR p = State().m_lastScopePos + 8;
		WTString opStr = GetOperatorChars(p);
		methscope = StrGetSymScope(methscope) + DB_SEP_STR + opStr;
	}

	if (m_deep && State(m_deep - 1).m_lastChar == '<')
	{
		if (m_parseGlobalsOnly && mParseLocalTypes)
		{
			// [case: 81110] [case: 81111]
			return;
		}

		methscope = DBColonToSepStr(":TemplateParameter:") + WTString(StrGetSym(methscope));
		//		type = TEMPLATETYPE; // template arg <class T>
	}

	OnAddSym(methscope);
}

void VAParseMPReparse::OnAddSym(WTString symScope)
{
	DEFTIMERNOTE(VAP_OnAddSym, NULL);
	if (!(m_writeToFile || GetScopeInfoPtr()->GetParseType() == ParseType_GotoDefs ||
	      !m_mp->FindExact2(symScope, mConcatDefsOnAddSym)))
		return;

	DEFTIMERNOTE(VAP_OnDef3, NULL);
	m_addedSymbols++;
	const LPCSTR p1 = State().m_begLinePos;
	auto p1readtoLen = GetBufLen() - (p1 - GetBuf()); // int in x86, int64 in x64
	bool looksLikeExpandedMacro = false;
	if (p1readtoLen > GetBufLen() || p1readtoLen < 0)
	{
		// [case: 61448] workaround for ExpandMacroCode / State().m_begLinePos problem
		// [case: 59270]
		// Some calls to ExpandMacroCode pass in tmp strings that don't point
		// into GetBuf(), so State().m_begLinePos may not actually point into
		// GetBuf().  That breaks the old pointer math used in the following
		// ReadTo() calls which resulted in large, invalid lengths.
		_ASSERTE(!(p1 >= GetBuf() && p1 <= (GetBuf() + GetBufLen())));
		// p1 is a standalone NULL-term string
		p1readtoLen = strlen_i(p1);
		looksLikeExpandedMacro = true;
	}

	if (State().m_defType == FUNC && WTString(StrGetSym(symScope)) == StrGetSym(StrGetSymScope(symScope)))
		State().m_defAttr |= V_CONSTRUCTOR;

	ReadToCls rtc(GetLangType());
	if (Is_C_CS_File(GetLangType()))
		rtc.KeepStrings(); // [case: 1131] does not apply to xml/html
	WTString def;
	if (IS_OBJECT_TYPE(State().m_defType))
	{
		def = rtc.ReadTo(p1, (int)p1readtoLen, ">{};");
		if (looksLikeExpandedMacro && State().m_lastChar == ':' && State().HasParserStateFlags(VPSF_INHERITANCE))
		{
			// #parseStateDeepReadAhead - unless explicitly init in ctor,
			// m_lastScopePos can have garbage in it
			const LPCSTR p3 = State(m_deep + 1).m_lastScopePos;
			if (p3)
			{
				const auto here = p3 - GetBuf();
				auto readtoLen = GetBufLen() - here;
				if (readtoLen <= 0 || readtoLen > GetBufLen())
					readtoLen = GetSafeReadLen(p3, GetBuf(), GetBufLen());
				if (readtoLen)
				{
					const WTString baseClasses = rtc.ReadTo(p3, (int)readtoLen, ">{};");
					if (!baseClasses.IsEmpty() && -1 == def.Find(baseClasses))
					{
						// [case: 99816]
						def += " : " + baseClasses;
					}
				}
			}
		}
	}
	else if (State().HasParserStateFlags(VPSF_GOTOTAG))
	{
		// [case: 1909] fix scope of label def
		// labels are scoped to the function and are unique
		def = ::StrGetSym(symScope);
		int pos = symScope.Find("-");
		if (-1 != pos)
		{
			pos = symScope.Find(":", pos);
			if (-1 != pos)
			{
				symScope = ::StrGetSymScope(symScope);
				symScope = symScope.Left(pos);
				symScope += DB_SEP_STR + def;
			}
		}
	}
	else if (GetLangType() == RC)
		def = TokenGetField(CurPos(), "\r\n");
	else
	{
		if (GetLangType() == JS)
			def = rtc.ReadTo(p1, (int)p1readtoLen, ",)>{};\r\n"); // ';' is optional in JS
		else
		{
#if 1
			const char* readToChars = ",)>{};";
#else
			const char* readToChars;
			if (VAR == State().m_defType && IsCFile(GetLangType()))
			{
				// [case: 141594]
				// attempt to keep brace initialized declaration for display in va nav bar.
				// abandoned attempt due to:
				//	- lambdas
				//	- unions and struct instances declared with struct definition
				//	- GetBaseClassList and possibly other places that need to be
				//		updated since they assume parens not braces (see unit test
				//		that fails with wildcard fallback)
				readToChars = ",)>};";
				rtc.KeepBraces();
			}
			else
				readToChars = ",)>{};";
#endif

			if (State().m_defAttr & V_CONSTRUCTOR)
			{
				// [case: 79737] c++11 ctor init lists
				if (CurChar() == '{')
				{
					LPCTSTR pCur = &m_buf[m_cp];
					auto readToPos = pCur - p1;
					if (readToPos <= 0 || readToPos > GetBufLen())
						readToPos = GetSafeReadLen(p1, GetBuf(), GetBufLen());
					rtc.SetReadToPos((int)readToPos);
					rtc.KeepBraces();
					readToChars = "";
				}
			}

			def = rtc.ReadTo(p1, (int)p1readtoLen, readToChars, kMaxLen);
			if (State().m_defAttr & V_CONSTRUCTOR && !def.IsEmpty())
			{
				_ASSERTE(readToChars[0] || rtc.CurChar() == '{' || rtc.GetCp() >= rtc.GetBufLen() ||
				         rtc.GetCp() == kMaxLen || rtc.GetCp() == (kMaxLen + 1));
				WTString tmp(::ReadToUnpairedColon(def));
				if (!tmp.IsEmpty())
					def = tmp;
			}

			if (rtc.CurChar() == '>')
			{
				// [case: 57625] operator>(const foo); operator >=(bar);
				int pos = def.Find("operator");
				if (-1 != pos)
				{
					pos += 8;
					if (pos >= def.GetLength() - 2)
						def = rtc.ReadTo(p1, (int)p1readtoLen, ",){};");
				}
			}
		}

		if (State().m_argCount)
		{
			bool fixupMultiArgDef = true;
			if (rtc.CurChar() == '{')
			{
				const WTString tmp(GetCStr(p1));
				if (tmp == "class" || tmp == "struct" || tmp == "union")
				{
					// [case: 81026]
					fixupMultiArgDef = false;
				}
			}

			if (fixupMultiArgDef)
			{
				WTString ndef = rtc.GetSubStr(p1, rtc.State().m_lastScopePos) + " " +
				                TokenGetField(State().m_lastScopePos, ",;\r\n");
				def = rtc.ReadTo(ndef, ")>{};"); // Needs ')' so last param doesn't include mismatched parens.
			}
		}
	}

	bool didScopeChompInDef = false;
	if (symScope.GetLength() && def.Find('.') != -1)
	{
		// change def from "int foo.bar()" to just "int bar()" to fix refactoring issues
		WTString dotSym = symScope.Mid(1);
		dotSym.ReplaceAll(":", ".");
		if (def.ReplaceAll(dotSym, StrGetSym(symScope), TRUE))
			didScopeChompInDef = true;
	}

	if (Is_C_CS_File(GetLangType()))
		EncodeTemplates(def);

	def.TrimRight();

	if (m_deep)
	{
		if (State().m_defType == C_ENUMITEM)
		{
			// Modify enum defs to add enum value
			if (State().m_lastWordPos[0] == ',') // in case last enum is followed by a,
				return;

			const WTString enumItemName(StrGetSym(symScope));
			LPCSTR lwp = State(m_deep - 1).m_lastWordPos;
			WTString enumType = GetCStr(lwp);
			if (enumType == "enum")
				enumType += ' ';
			else
			{
				bool hitColon = false;
				// rewind to see if there was a :
				for (int cnt = 0; lwp-- && lwp > m_buf && cnt < 25; ++cnt)
				{
					if (*lwp == ':')
					{
						hitColon = true;
						break;
					}

					if (lwp[0] == 'e' && lwp[1] == 'n' && lwp[2] == 'u' && lwp[3] == 'm')
						break;
				}

				if (hitColon)
				{
					// [case: 75894]
					enumType = GetCStr(State(m_deep - 1).m_lastScopePos);
				}

				if (enumType == "enum")
					enumType += ' ';
				else
					enumType = WTString("enum ") + enumType + ' ';
			}

			if (State().m_argCount == 0)
			{
				m_enumArg.Empty();
				m_enumArgValueParsed = false;
				m_enumArgValue = 0;
			}

			LPCSTR p = State().m_lastScopePos;
			for (int deep = 0; *p && ((!deep && !strchr(",}=;", *p)) || (deep && *p != ';'));)
			{
				char curCh = *p++;
				if (curCh == '/')
				{
					// [case: 28797] don't read into comment
					if (*p == '*' || *p == '/')
						break;
				}
				else if (curCh == '(')
					++deep; // [case: 111627]
				else if (curCh == ')')
					--deep; // [case: 111627]
			}

			if (p && p[0] == '=')
			{
				m_enumArg = TokenGetField(p, ",}");
				m_enumArg.ReplaceAll("\r\n", " ");
				m_enumArg.ReplaceAll("\r", " ");
				m_enumArg.ReplaceAll("\n", " ");
				m_enumArg.ReplaceAll("\t", " ");

				WTString val = m_enumArg.Mid(1);
				val.Trim();

				if (val.MatchRE("0x[A-Fa-f0-9]+"))
				{
					m_enumArgValueParsed = true;
					m_enumArgValue = atox(val.c_str() + 2);
				}
				else if (val.MatchRE("\\d+"))
				{
					m_enumArgValueParsed = true;
					m_enumArgValue = atoi(val.c_str());
				}

				State().m_argCount = 0;
				def = enumType + WTString(StrGetSym(symScope)) + " " + m_enumArg;
			}
			else if (m_enumArgValueParsed)
				def = enumType + enumItemName + WTString(" = ") + itos(State().m_argCount + m_enumArgValue);
			else if (m_enumArg.GetLength())
				def = enumType + enumItemName + " " + m_enumArg + " + " + itos(State().m_argCount);
			else
				def = enumType + enumItemName + WTString(" = ") + itos(State().m_argCount);

			if (enumType == "enum " && (DB_SEP_STR + enumItemName) != symScope)
			{
				WTString tmp(symScope);
				int pos = tmp.Find(":unnamed_enum_");
				if (pos != -1)
				{
					tmp = tmp.Left(pos + 1);
					tmp += enumItemName;
					uint attrs = 0;
					if (State(m_deep - 1).m_defAttr & V_MANAGED)
						attrs = V_MANAGED;
					// add reverse lookup for unnamed enums (add scope as definition)
					OnAddSymDef(":va_unnamed_enum_scope" + tmp, symScope, C_ENUMITEM, attrs | V_HIDEFROMUSER);
				}
			}
		}
		else if (State(m_deep - 1).m_defType == STRUCT)
		{
			const WTString typeDecl = GetCStr(State(m_deep - 1).m_lastWordPos);
			if (typeDecl == "struct" || typeDecl == "union")
			{
				WTString tmp(symScope);
				const int pos = tmp.ReverseFind(":unnamed_" + typeDecl + "_");
				if (pos != -1)
				{
					tmp = tmp.Left(pos + 1);
					tmp += StrGetSym(symScope);
					// add reverse lookup for members of unnamed structs/unions (add unnamed parent scope as definition)
					OnAddSymDef(":va_unnamed_" + typeDecl + "_scope" + tmp, StrGetSymScope(symScope), State().m_defType,
					            V_HIDEFROMUSER);
				}
			}
		}
	}

	if (def.IsEmpty())
		return;

	#ifdef RAD_STUDIO_LANGUAGE
	if (!symScope.IsEmpty() && (def[0] == '_') && StartsWith(def, "__property ", false, true))
	{
		// If a derived class has a property declaration to pull it from the base class (similar to C++'s using within a
		// class), VA parser will use that new declaration which doesn't contain a type, so the property will stop 
		// working. As a workaround, property (re)declaration will be skipped...
		// A better solution would be to find the property in the base class, take its type and append it to this 
		// redefiniton (that wouldn't be skipped this time).
		// struct base {
		// 		with_member *mXX;
		//		void setXX(with_member * value);
		//		__property with_member* XX = {read = mXX, write = setXX};
		// };
		// struct derived : public base {
		//		__published:
		//		__property XX;
		// };

		const char *sym = StrGetSym(symScope);
		if(sym && sym[0] && !strcmp(sym, def.c_str() + 11))
			return;
	}
	#endif

	uint attrs = 0;
	ULONG type = State().m_defType;
	if (m_deep && VAR == type && (CurChar() == ')' || CurChar() == ':' || CurChar() == ',') &&
	    FUNC == State(m_deep - 1).m_defType && !(def[0] & 0x80) && ::tolower(def[0]) == 'c')
	{
		// function parameter
		if (0 == def.Find("const ") || 0 == def.Find("CONST "))
		{
			WTString tmp(def.Mid(6));
			tmp.ReplaceAll("&", " "); // [case: 846]
			tmp.ReplaceAll("*", " ");
			tmp.ReplaceAll("^", " ");
			tmp.ReplaceAll("  ", " ");
			tmp.TrimRight();
			if (-1 == tmp.Find(" "))
				return; // [case: 3027] no param name: void Foo(const Type);
		}
	}
	else if (CurChar() == '{')
	{
		State().m_defAttr |= V_IMPLEMENTATION;
		def += "{...}";
	}
	else if (CurChar() == ';' && State().m_lastWordPos && State().m_lastWordPos[0] == 'd' &&
	         StartsWith(State().m_lastWordPos, "default"))
	{
		// [case: 118695][case: 104564]
		State().m_defAttr |= V_IMPLEMENTATION;
	}

	if (m_deep && StartsWith(State(m_deep - 1).m_begLinePos, "enum"))
	{
		type = C_ENUMITEM;
		if (State(m_deep - 1).m_defAttr & V_MANAGED)
			attrs |= V_MANAGED;
	}

	if (State().m_defType == NAMESPACE)
	{
		if (Is_C_CS_VB_File(GetLangType()))
		{
			const int pos = def.FindOneOf(":.");
			const int implPos = def.Find("...}");
			if ((pos != -1 && pos != implPos) || didScopeChompInDef)
			{
				// [case: 98016]
				const WTString kNamespace("namespace");
				WTString ns = ::StrGetSymScope(symScope);
				while (ns.GetLength())
				{
					WTString cur(::StrGetSym(ns));
					if (cur.IsEmpty())
						cur = kNamespace;
					else
						cur = kNamespace + " " + cur; // [case: 67133]
					OnAddSymDef(ns, cur, NAMESPACE, attrs);
					ns = ::StrGetSymScope(ns);
				}
			}
		}
	}

	if (GetLangType() == CS)
	{
		if (State().m_defType == CLASS || State().m_defType == C_INTERFACE)
		{
			// add base as a member (base is C# keyword ala this)
			WTString types;
			try
			{
				types = GetTypesFromDef(symScope, def, CLASS, GetLangType());
			}
			catch (const WtException&)
			{
			}

			if (types.GetLength())
			{
				if (types[0] == DB_SEP_CHR && types.GetLength() > 2 && types.ReverseFind(DB_SEP_CHR) == 0 &&
				    types.Find('\f') == types.GetLength() - 1)
				{
					types = types.Mid(1, types.GetLength() - 2);
				}

				types += " " + WTString("base");
			}
			else
			{
				// BaseClassFinder::GetBaseClassList has logic that adds implicit base
				// classes for C#; we should consider moving that logic to here?
				// see comment in that file (// HANDLE default types, "class foo" automatically inherits Object")
				types = " ";
			}

			// [case: 81231] these show up in FSIS dlg.  should add | V_HIDEFROMUSER to attrs???
			OnAddSymDef(symScope + ":base", types, VAR, attrs);
		}
		else if (State().m_defType == FUNC)
		{
			// [case: 23734]
			// check for extension method
			if (m_deep && State().m_defType == FUNC && State(m_deep - 1).m_defType == CLASS)
			{
				if (State(m_deep - 1).m_lastWordPos && State(m_deep - 1).m_begLinePos)
				{
					// check class before checking method since static methods are more common than static classes
					const WTString classDec(
					    State(m_deep - 1).m_begLinePos,
					    ptr_sub__int(State(m_deep - 1).m_lastWordPos, State(m_deep - 1).m_begLinePos));
					if (::strstrWholeWord(classDec, "static"))
					{
						if (State().m_lastWordPos && State().m_begLinePos)
						{
							const WTString funcRet(State().m_begLinePos,
							                       ptr_sub__int(State().m_lastWordPos, State().m_begLinePos));
							if (::strstrWholeWord(funcRet, "static"))
							{
								// pull type from first method param (this param in the def)
								const WTString sym = ::StrGetSym(symScope);
								int pos = def.Find(sym);
								if (-1 != pos)
								{
									pos = def.Find("(", pos);
									if (-1 != pos)
									{
										pos = def.Find("this ", pos);
										if (-1 != pos)
										{
											WTString thisType = def.Mid(pos + 5);
											thisType = ::GetCStr(thisType);
											if (!thisType.IsEmpty())
											{
												const WTString newScope = DB_SEP_STR + thisType + DB_SEP_STR + sym;
												OnAddSymDef(newScope, def, type, attrs);
												// find refs/rename/change sig don't work
												// maybe add hidden type with extension flag like:
												// OnAddSymDef(":extension" + newScope, def, type, attrs |
												// V_HIDEFROMUSER); and do look up in find refs??
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// [case: 80009]
	{
		if (State().m_defType == CLASS || State().m_defType == STRUCT || State().m_defType == C_INTERFACE)
		{
			// find just the inheritance part of the def, after the ':'
			// works around GetTypesFromDef() returning types such as AFX_NOVTABLE for "class AFX_NOVTABLE CCmdTarget :
			// public CObject{...}"
			WTString inhDef;
			int inhPos = 0;
			while ((inhPos = def.Find(":", inhPos)) > 0)
			{
				++inhPos;
				if (def[inhPos] != ':')
					inhDef = def.Mid(inhPos);
				else
					++inhPos;
			}

			if (!inhDef.IsEmpty())
			{
				inhDef = StripEncodedTemplates(inhDef);
				WTString types;
				try
				{
					types = GetTypesFromDef(symScope, inhDef, CLASS, GetLangType());
				}
				catch (const WtException&)
				{
				}
				token2 tk(types);
				while (tk.more())
				{
					WTString base = tk.read2("\f");
					if (!base.IsEmpty())
					{
						OnAddSymDef(symScope + DB_SEP_STR + InheritanceDb::kInheritsFromStr + StrGetSym(symScope), base,
						            vaInheritsFrom, attrs | V_HIDEFROMUSER);
					}
				}
			}
		}
	}

	if (IsCFile(GetLangType()) && State().m_defType == FUNC)
	{
		const LPCSTR scopePos = State().m_lastScopePos;
		if (scopePos && *scopePos && strncmp("et_", scopePos + 1, 3) == 0)
		{
			// Added support for __property for managed C (old managed code)
			// Probably a better place to put this, but it works here.

			// http://www.codeproject.com/Articles/1050/Using-properties-in-managed-C
			// http://msdn.microsoft.com/en-us/library/aa712983%28v=VS.71%29.aspx
			// we have to create the property name based on the accessors
			if (strncmp("get_", scopePos, 4) == 0 || strncmp("set_", scopePos, 4) == 0)
			{
				if (def.contains("__property"))
				{
					WTString prop = StrGetSymScope(symScope) + DB_SEP_CHR + GetCStr(scopePos + 4);
					OnAddSymDef(prop, def, PROPERTY, attrs);
				}
			}
		}

		if (State().m_lastChar == '>' && CurChar() == ';')
		{
			const int returnPtrPos = def.Find("->");
			if (-1 != returnPtrPos)
			{
				// [case: 113008]
				// check for deduction guides
				// http://en.cppreference.com/w/cpp/language/class_template_argument_deduction
				// XxPair2(const char*, int) -> XxPair2<std.string, int>;
				auto sym = StrGetSym(symScope);
				int pos1 = def.Find(sym);
				if (-1 != pos1)
				{
					int pos2 = def.Find("(");
					if (-1 != pos2 && pos2 > pos1 && returnPtrPos > pos2)
					{
						// sym appears before first '(' and that '(' appears before "->"
						pos2 = def.Find(sym, returnPtrPos);
						if (-1 != pos2)
						{
							// sym also appears after "->"
							pos1 = def.Find(WTString(EncodeChar('<')), returnPtrPos);
							if (-1 != pos2 && pos2 < pos1)
							{
								// '<' appears after "->" and sym appears between "->" and "<"
								State().m_defType = type = TEMPLATE_DEDUCTION_GUIDE;
								attrs |= V_HIDEFROMUSER;
							}
						}
					}
				}
			}
		}
	}

	if (StartsWith(def, "extern") &&
	    InLocalScope()) //  Case 3752:   variable declared extern in namespace not properly parsed
	{
		if (VAR == type && PrevChar() == ')')
		{
			// [case: 25957] if def is extern and contains parens, then it's FUNC not VAR
			type = State().m_defType = FUNC;
		}
		OnAddSymDef(DB_SEP_STR + StrGetSym(symScope), def, type, attrs);
	}
	else if (InLocalScope() && FUNC == type && PrevChar() == ')' && IsCFile(GetLangType()) &&
	         !StartsWith(State().m_begLinePos, "typedef"))
	{
		// [case: 25957] "void Foo()" local declaration of function needs to
		// be promoted to global scope, but not "typedef void (*Foo)()"
		OnAddSymDef(DB_SEP_STR + StrGetSym(symScope), def, type, attrs);
	}
	else
	{
		// don't use !mUsingNamespaces.empty (KILLS debug build performance during sysdb init)
		if (mIsUsingNamespace)
			AdjustScopeForNamespaceUsage(symScope);
		OnAddSymDef(symScope, def, type, attrs);
	}
}

static bool IsPointerDeclarationIntoArray(WTString def)
{
	const int kArrPos = def.Find('[');
	const int kEqPos = def.Find('=');
	const int kEq2Pos = def.Find("==");
	const int kRefPos = def.Find('&');
	const int kPtrPos = def.Find('*');

	// see [case: 112246] re: this condition
	if (kRefPos && strstrWholeWord(def, "operator"))
	{
		// [case: 2221] [case: 4733] change 8031
		// ref& operator[]();
		return false;
	}

	if (-1 == kArrPos)
	{
		// [case: 112232]
		// def doesn't even have a '[' char
		return false;
	}

	if (kRefPos != -1)
	{
		if (kEqPos != -1 && kEq2Pos == -1) // not ==
		{
			// int & intRef = intArray[11]; // intRef is not a pointer
			if (kRefPos < kEqPos && kEqPos < kArrPos)
				return false;
		}
	}
	else if (kPtrPos == -1 || kPtrPos > kEqPos)
	{
		if (kEqPos != -1 && kEq2Pos == -1) // not ==
		{
			// 	int anInt2 = intArray[1];  // anInt2 is not a pointer
			if (kEqPos < kArrPos)
				return false;
		}
	}

	return true;
}

int VAParse::GetLineNumber(uint type, const WTString& def, WTString symName)
{
	if (type == FUNC || type == GOTODEF || type == C_ENUM || type == NAMESPACE || type == CLASS || type == STRUCT)
	{
		if (!Is_C_CS_File(FileType()))
			return (int)m_curLine;
		if (*(m_buf + m_cp) != '{')
			return (int)m_curLine;
		if ((type == FUNC || type == GOTODEF) && def.find('(') == -1)
			return (int)m_curLine;
		if (m_inMacro) // fixing message maps, e.g. BEGIN_MESSAGE_MAP(AddClassMemberDlg, VADialog) in outlineTest.cpp of
		               // unit tests
			return (int)m_curLine;

		LPCTSTR iterator = m_buf;
		LPCTSTR wordPos = State().m_lastWordPos;
		if (!StartsWith(wordPos, symName))
		{
			wordPos = State().m_lastScopePos;
			if (!StartsWith(wordPos, symName))
			{
				for (LPCTSTR cp = wordPos; cp < m_buf + m_cp; cp++)
				{
					if (*cp == 0)
						break;
					if (StartsWith(cp, symName))
					{
						wordPos = cp;
						goto out;
					}
				}
				return (int)m_curLine;
			out:;
			}
		}

		LPCTSTR endBuf = m_buf + mBufLen;

		// if wordPos points outside of the file buffer it means that the method name is of a result of a macro
		// expansion
		if (wordPos > endBuf || wordPos < m_buf)
		{
			wordPos = State().m_begLinePos;
			if (wordPos > endBuf || wordPos < m_buf)
				return (int)m_curLine;
		}

		int ln = 1;
		LPCTSTR iterator_end = std::min(wordPos, m_buf + mBufLen);
		while (iterator < iterator_end)
		{
			switch (*iterator)
			{
			case 0:
				return ln;
			case '\r':
				if(iterator[1] == '\n')
					iterator++;
			[[fallthrough]];
			case '\n':
				ln++;
				break;
			}

			iterator++;
		}

		return ln;
	}
	else
		return (int)m_curLine;
}

void VAParseMPReparse::OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
{
	bool dont_write = false;
	if (strstr(def.c_str(), "__va_") || symScope.begins_with(":__va_"))
	{
		attrs |= V_LOCAL;
		dont_write = true;
	}

	_ASSERTE((type & TYPEMASK) == type);
	DEFTIMERNOTE(VAP_OnAddSymDef, NULL);
	::ScopeInfoPtr pScopeInfo = GetScopeInfoPtr();
	EdCntPtr curEd = g_currentEdCnt;

	// when we add/rename a variable, update colors
	if (pScopeInfo->GetParseAll() && !pScopeInfo->IsWriteToDFile() && curEd &&
	    curEd->FileName() == pScopeInfo->GetFilename())
		curEd->Invalidate(TRUE);

	if (CurChar() == '[' && IsPointerDeclarationIntoArray(def))
	{
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		// [case: 133417] index properties are not pointers
		if (type != PROPERTY)
#endif
			State().m_defAttr |= V_POINTER;
	}

	attrs |= GetDefPrivAttrs(m_deep); // Public/Protected/private
	if (!dont_write && pScopeInfo->IsWriteToDFile())
	{
		// Write to dfile
		if (InLocalScope() || (GetLangType() == Src && pScopeInfo->GetParseType() != ParseType_GotoDefs))
			attrs |= V_LOCAL;
		attrs |= pScopeInfo->GetParseAll() ? V_INFILE : (pScopeInfo->IsSysFile() ? V_SYSLIB : V_INPROJECT);
		_ASSERTE(strncmp(":ForwardDeclare:", symScope.c_str(), 16) != 0 || (attrs & V_HIDEFROMUSER));

		int ln = GetLineNumber(type, def, StrGetSym(symScope));

		m_mp->DBOut(symScope, def, type, attrs, ln);
	}
	else
	{
		// reparse, just add to dictionary
		m_mp->m_line = (int)m_curLine;
		if (InLocalScope() || (GetLangType() == Src && pScopeInfo->GetParseType() != ParseType_GotoDefs))
			m_mp->add(symScope, def, DLoc, type, attrs | V_INFILE | V_LOCAL, 0);
		else if (pScopeInfo->IsSysFile())
			m_mp->add(symScope, def, DSys, type, attrs | V_SYSLIB, 0);
		else // if(FileType() != Src)
		{
			m_mp->add(symScope, def, DMain, type, attrs | V_INPROJECT | V_INFILE, 0);
			m_mp->add(symScope, def, DLoc, type, attrs | V_INFILE | V_LOCAL, 0);
		}
#ifdef DEBUG_VAPARSE
		DType* data = m_mp->FindExact2(symScope);
		if (!data)
			DebugMessage("OnAddSymDef failed");
#endif // DEBUG_VAPARSE
	}
}

void VAParseMPReparse::AdjustScopeForNamespaceUsage(WTString& theScope)
{
	_ASSERTE(mIsUsingNamespace);
	if (!g_pGlobDic || mUsingNamespaces.empty())
		return;

	if (!InLocalScope() && GetLangType() != Src)
		return;

	if (theScope == ":using")
		return;

	if (theScope.contains(":wtUsingNamespace"))
		return;

	if (!(State().m_defAttr & V_LOCAL || State().m_defAttr & V_IMPLEMENTATION || State().m_defType == GOTODEF))
		return;

	// we have either parsed for locals or for goto defs and there was
	// a using namespace directive.
	// we need to adjust scope to reflect a namespace that might be in use. (case=971)
	// Don't do this for system symbols (shouldn't have using directives anyway)
	for (std::set<WTString>::iterator it = mUsingNamespaces.begin(); it != mUsingNamespaces.end(); ++it)
	{
		const WTString curNamespaceItem(*it);
		if (theScope.Find(curNamespaceItem + ":") != -1)
			continue;

		const WTString possibleNewScope(curNamespaceItem + theScope);
		// assume that if there is a using directive, it is for
		// something in the project globals.  Shouldn't be implementing
		// a system symbol locally in the project and it would be weird
		// (but possible) to declare a namespace and have a using
		// statement for it all within the same source file.
		DType* decl = g_pGlobDic->Find(possibleNewScope, FDF_NoConcat);
		if (decl)
		{
			vCatLog("Parser.VAParse", "scp chg for using: ns(%s) old(%s) new(%s)", curNamespaceItem.c_str(), theScope.c_str(),
			     possibleNewScope.c_str());
			theScope = possibleNewScope;
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// VAParseMPScope looks up CSym in db and stores in m_lwData when m_scoping is set

DType* FindForwardDeclaration(MultiParse* mp, const WTString& scope, const WTString& sym)
{
	WTString possibleScope(scope);
	const int scopeLen = scope.GetLength();
	if (!scopeLen || possibleScope[scopeLen - 1] != DB_SEP_CHR)
		possibleScope += ":";
	WTString possibleForwardDecl;
	static const WTString sepstr = DBColonToSepStr(":ForwardDeclare");
	for (; !possibleScope.IsEmpty();)
	{
		possibleForwardDecl = {sepstr, possibleScope, sym};
		DType* data = mp->FindExact2(possibleForwardDecl, true, 0, false);
		if (data)
			return data;
		int pos = possibleScope.ReverseFind(':');
		if (-1 == pos)
			break;
		possibleScope.LeftInPlace(pos);
		pos = possibleScope.ReverseFind(':');
		if (-1 == pos)
			break;
		possibleScope.LeftInPlace(pos + 1);
	}
	return NULL;
}

WTString VAParseMPScope::GetTemplateFunctionCallTemplateArg(DType* funcData, const WTString& scopeToCheck,
                                                            const WTString& bcl)
{
	_ASSERTE(InLocalScope() || mIgnoreLocalTemplateCheck);
	int ep = GetBufLen() - GetCp();
	_ASSERTE(ep >= 0);
	if (ep > 512)
		ep = 512;
	WTString tmp(GetSubStr(CurPos(), CurPos() + ep));

	return GetTemplateFunctionCallTemplateArg(std::move(tmp), m_mp, GetLangType(), funcData, scopeToCheck, bcl);
}
// static
WTString VAParseMPScope::GetTemplateFunctionCallTemplateArg(WTString expr, MultiParsePtr mp, int ftype, DType* funcData, const WTString& scopeToCheck,
                                                            const WTString& bcl)
{
	_ASSERTE(funcData && funcData->IsTemplate());
	WTString tmp = expr;
	const WTString funcName(funcData->Sym());
	int pos = tmp.Find(';');
	if (-1 != pos)
	{
		// [case: 142299]
		tmp.LeftInPlace(pos);
	}

	pos = tmp.Find(funcName);
	if (-1 == pos)
		return NULLSTR;

	tmp.MidInPlace(pos + funcName.GetLength());
	pos = tmp.Find('(');
	if (-1 == pos)
		return NULLSTR;

	tmp.LeftInPlace(pos);
	tmp.TrimLeft();
	pos = tmp.Find("<");
	if (0 == pos)
	{
		pos = tmp.ReverseFind(">");
		if (-1 != pos)
		{
			// call has explicit <T>
			tmp.LeftInPlace(pos + 1);
			if (!tmp.IsEmpty())
			{
				// looks like a template function call
				// see if we have an ":< >" entry
				const WTString searchForTemplate(funcData->SymScope() + DB_SEP_STR + "< >");
				DType* tmpData = mp->FindExact2(searchForTemplate, false, TEMPLATETYPE, false);
				if (tmpData)
				{
					::EncodeTemplates(tmp);
					return tmp;
				}
			}
		}
	}
	else if (-1 == pos && IsCFile(ftype))
	{
		// [case: 66730]
		// the call does not have explicit <T> so we need to deduce it from the call params
		// so that we can instantiate the template as if the args were explicit like above.
		const WTString searchForTemplate(funcData->SymScope() + DB_SEP_STR + "< >");
		DType* templateTypeData = mp->FindExact2(searchForTemplate, false, TEMPLATETYPE, false);
		if (!templateTypeData)
			return NULLSTR; // no template definition even found, no need to proceed

		if (funcData->Scope_sv().first == ":std")
		{
			const WTString sym(funcData->Sym());
			if ((-1 != sym.Find("begin") || -1 != sym.Find("end")))
			{
				switch (sym[0])
				{
				case 'b':
					if (sym == "begin")
					{
						// don't interfere with InferTypeFromAutoVar handling of begin/end
						return NULLSTR;
					}
					break;
				case 'c':
					if (sym == "cbegin" || sym == "crbegin" || sym == "cend" || sym == "crend")
						return NULLSTR;
					break;
				case 'e':
					if (sym == "end")
						return NULLSTR;
					break;
				case 'r':
					if (sym == "rbegin" || sym == "rend")
						return NULLSTR;
					break;
				}
			}
		}

		const WTString funcRetType(::GetTypesFromDef(funcData->SymScope(), funcData->Def(), FUNC, Src));
		if (funcRetType.IsEmpty())
			return NULLSTR;
		if (funcRetType[0] == DB_SEP_CHR)
		{
			WTString tmp2 = funcRetType.Mid(1);
			::TokenGetField2InPlace(tmp2, '\f'/*, Src*/);
			if (::IsReservedWord(tmp2, Src))
				return NULLSTR;
		}
		else if (::IsReservedWord(funcRetType, Src))
			return NULLSTR;

		std::vector<WTString> templateTypeDataArgs, funcDataArgs, callsiteArgs;

		// get templateType args (from the TEMPLATETYPE dtype)
		{
			WTString def = templateTypeData->Def();
			if (-1 != def.Find('\f'))
				::TokenGetField2InPlace(def, '\f'/*, Src*/);
			if (def.IsEmpty() || def[0] != '<')
				return NULLSTR;

			ArgsSplitter rtc(Src, ArgsSplitter::ArgSplitType::AngleBrackets);
			rtc.ReadTo(def.c_str(), def.GetLength(), ";{}");
			templateTypeDataArgs.swap(rtc.mArgs);

			for (auto& cur : templateTypeDataArgs)
			{
				cur.ReplaceAll("class", "", TRUE);
				cur.ReplaceAll("struct", "", TRUE);
				cur.ReplaceAll("typename", "", TRUE);
				cur.ReplaceAll("const", "", TRUE);
				cur.Trim();
			}

			if (templateTypeDataArgs.empty())
				return NULLSTR;
		}

		// get funcData args (from the FUNC dtype)
		{
			WTString def = funcData->Def();
			if (-1 != def.Find('\f'))
				::TokenGetField2InPlace(def, '\f'/*, Src*/);
			pos = def.Find('(');
			if (-1 == pos)
				return NULLSTR;

			def = def.Mid(pos);
			ArgsSplitter rtc(Src, ArgsSplitter::ArgSplitType::Parens);
			rtc.ReadTo(def.c_str(), def.GetLength(), ";{}");
			funcDataArgs.swap(rtc.mArgs);

			// remove param names
			for (auto& curArg : funcDataArgs)
			{
				token t(curArg);
				t.ReplaceAll("class", "", TRUE);
				t.ReplaceAll("struct", "", TRUE);
				t.ReplaceAll("typename", "", TRUE);
				t.ReplaceAll("const", "", TRUE);
				// eat default value
				t.ReplaceAllRegex(CStringW(LR"(=.*$)"), CStringW(L""));
				// normalize pointers and references
				t.ReplaceAll(" *", "* ");
				t.ReplaceAll(" ^", "^ ");
				t.ReplaceAll(" &&", "&& ");
				t.ReplaceAll(" &", "& ");
				// eat param name
				t.ReplaceAllRegex(CStringW(LR"( [\w_\$]+ *$)"), CStringW(L""));
				t.ReplaceAll("  ", " ");
				curArg = t.Str();
			}

			if (funcDataArgs.empty())
				return NULLSTR;
		}

		// get the actual args from the call site
		{
			ArgsSplitter rtc(Src, ArgsSplitter::ArgSplitType::Parens);
			rtc.ReadTo(expr.c_str(), expr.GetLength(), ";{}");
			callsiteArgs.swap(rtc.mArgs);
		}

		if (callsiteArgs.size() != funcDataArgs.size())
		{
			// we have more args at call site than appear in def of template
			// function. don't proceed due to some sort of mismatch.
			// callsiteArgs is vector dereferenced by index with assumption that
			// index into funcDataArgs is valid in callsiteArgs.
			return NULLSTR;
		}

		std::map<int, WTString> callSiteArgsInTemplateParamPosition;

		uint funcDataArgPos = 0;
		for (auto& curArg : funcDataArgs)
		{
			// iterate over each funcDataArg and see if there is a match in templateTypeDataArgs
			int templateDataArgPos = 0;
			for (const auto& formalArg : templateTypeDataArgs)
			{
				if (::strstrWholeWord(curArg, formalArg))
				{
					callSiteArgsInTemplateParamPosition[templateDataArgPos] = callsiteArgs[funcDataArgPos];
					break;
				}
				if (::strstrWholeWord(formalArg, curArg))
				{
					callSiteArgsInTemplateParamPosition[templateDataArgPos] = callsiteArgs[funcDataArgPos];
					break;
				}

				++templateDataArgPos;
			}

			++funcDataArgPos;
		}

		WTString deducedTemplateArgs("<");
		InferType inferType;

		// build template args by iterating over collected call site params in correct order
		int callSiteArgPos = 0;
		for (auto& curArg : callSiteArgsInTemplateParamPosition)
		{
			if (curArg.first != callSiteArgPos++)
			{
				// [case: 142031]
				// mismatch in template arg count, likely due to template overloads with
				// different number of formal args in templateTypeDataArgs where one overload
				// has a return type and another overload does not
				return NULLSTR;
			}

			if (curArg.second.IsEmpty())
				return NULLSTR;

			WTString curType = inferType.Infer(mp, curArg.second, scopeToCheck, bcl, ftype);
			if (curType.IsEmpty())
				return NULLSTR;

			// pack into tmp = "<Type1[,Type2*]>"
			if (deducedTemplateArgs.GetLength() > 1)
				deducedTemplateArgs += ",";
			deducedTemplateArgs += curType;
		}

		deducedTemplateArgs += ">";
		if (deducedTemplateArgs.GetLength() > 2)
		{
			::EncodeTemplates(deducedTemplateArgs);
			return deducedTemplateArgs;
		}
	}

	return NULLSTR;
}

bool DefHasDuplicatedName(DType* data)
{
	if (!data)
		return false;

	const WTString def(data->Def());
	WTString name(data->Sym());
	name += " " + name;
	int pos = def.Find(name);
	if (-1 == pos)
		return false;

	return true;
}

void VAParseMPScope::DoScope()
{
	_ASSERTE(HasSufficientStackSpace());
	// Look up scope of CWD and store in m_lwData
	DEFTIMERNOTE(VAP_DoScope, NULL);
	WTString sym = GetCStr(CurPos());
	if (!sym.GetLength())
		return;

	LogElapsedTime let("VPMPS::DS", sym, 50);

	BOOL canUnderline = TRUE;
	if (sym == "operator")
	{
		// if sym is operator, get the actual operator chars
		// since scope will be something like :class:==  (not :class:operator)
		sym = GetOperatorChars(CurPos());
		canUnderline = FALSE; // prevent underlining
	}

	DType* data = NULL;
	DTypePtr tempDataPtr;
	WTString bc;
	WTString nonXrefBcl;
	WTString nonXrefScope;
	if (IsXref())
	{
		DTypePtr bcd = State(m_deep).m_lwData;

		if (!bcd && m_deep)
		{
			if (State(m_deep - 1).m_lwData && State(m_deep - 1).m_lastChar == '{' &&
			    (State(m_deep - 1).m_parseState == VPS_ASSIGNMENT ||
			     (VAR == State(m_deep - 1).m_defType && VPS_BEGLINE == State(m_deep - 1).m_parseState)))
			{
				// [case: 118748] C99/C++20 designated initializer support
				bcd = State(m_deep - 1).m_lwData;
			}
			else if (m_deep > 1 && !State(m_deep - 1).m_lwData && State(m_deep - 1).m_lastChar == '{' &&
			         State(m_deep - 2).m_lwData && State(m_deep - 2).m_lastChar == '{' &&
			         (State(m_deep - 2).m_parseState == VPS_ASSIGNMENT ||
			          (VAR == State(m_deep - 2).m_defType && VPS_BEGLINE == State(m_deep - 2).m_parseState)))
			{
				// [case: 141667] C99/C++20 designated initializer support in array
				bcd = State(m_deep - 2).m_lwData;
			}
		}

		// Filter out reserved words as bc for "typedef std::string string;"
		if (bcd && bcd->MaskedType() != RESWORD)
		{
			bcd = ::TraverseUsing(bcd, m_mp.get());
			bc = bcd->SymScope();
		}

		// test immediate inheritance before going to bcl
		data = m_mp->FindExact2(bc + DB_SEP_CHR + sym);
		if (!data)
		{
			WTString bcl;
			if (bc.GetLength())
				bcl = bc + "\f" + m_mp->GetBaseClassList(bc, false, mMonitorForQuit, GetLangType());
			else
				bcl = DB_SEP_CHR;

			bool isTemplateParam = false;
			data = m_mp->FindSym(&sym, NULL, &bcl);
			if (!data)
			{
				data = m_mp->FindSym(&sym, &bc, &bcl, FDF_NoAddNamespaceGuess | FDF_NoConcat); // [case: 146500]
			}
			if (data && data->type() == FUNC && data->IsTemplate() && InLocalScope())
			{
				// template function
				WTString tmp(GetTemplateFunctionCallTemplateArg(data, NULLSTR, bcl));
				if (!tmp.IsEmpty())
				{
					// [case: 30137] change bc to include the actual template parameter
					tmp = data->SymScope() + tmp;
					DType* tmpData = m_mp->FindSym(&tmp, NULL, &bcl);
					if (!tmpData)
					{
						// cause template instantiation via GetBaseClassList call
						const WTString tmp2(m_mp->GetBaseClassList(tmp, false, mMonitorForQuit, GetLangType()));
						if (!tmp2.IsEmpty() && -1 == tmp2.Find(WILD_CARD))
							tmpData = m_mp->FindSym(&tmp, NULL, &bcl);
					}

					if (tmpData && tmpData != data)
						data = tmpData;
				}
			}

			if (!data && bcd && bcd->type() == VAR && IsCFile(FileType()) && !m_Scoping && m_cp > 1 &&
			    !bcd->IsPointer())
			{
				// [case: 81172]
				char prevCh = m_buf[m_cp - 1];
				if ('>' == prevCh)
				{
					prevCh = m_buf[m_cp - 2];
					if ('-' == prevCh)
					{
						WTString opStr("->");
						DType* opData = m_mp->FindOpData(opStr, GetDataKeyStr(m_deep), mMonitorForQuit);
						if (opData)
						{
							bc.Empty();
							bcd = std::make_shared<DType>(opData);
							if (bcd && bcd->MaskedType() != RESWORD)
							{
								bcd = ::TraverseUsing(bcd, m_mp.get());
								bc = bcd->SymScope();
							}

							if (bc.GetLength())
								bcl = bc + "\f" + m_mp->GetBaseClassList(bc, false, mMonitorForQuit, GetLangType());
							else
								bcl = DB_SEP_CHR;

							data = m_mp->FindSym(&sym, NULL, &bcl);
						}
					}
				}
			}

			if (!data)
			{
				isTemplateParam = bcl.contains(DBColonToSepStr(":TemplateParameter"));
				if (!isTemplateParam)
					data = ::FindForwardDeclaration(m_mp.get(), bc, sym);
			}

			canUnderline = data ? TRUE : !bcl.contains(WILD_CARD);
			if (isTemplateParam || bcl.contains(DBColonToSepStr(":ForwardDeclare")))
				canUnderline = FALSE; // template<class T>... T::SomeMethod()

			if (!data)
			{
				DType* macData = m_mp->FindExact2(DB_SEP_STR + sym);
				if (macData && macData->MaskedType() == DEFINE)
					canUnderline = FALSE;
			}

			if (!data && bcd && bcd->IsType() && FileType() == Src)
			{
				// [case: 5315]
				// see if removing an outer scope yields a FindExact hit.
				// ensure there is at least one outer scope (don't pare scope down to sym).
				WTString strippedScope;
				WTString scp(bcd->SymScope());
				do
				{
					// strip first scope off scopeToReplace
					int pos = scp.Find(DB_SEP_STR, 1);
					if (-1 == pos)
						break;

					// see if scope to remove is a namespace
					strippedScope += scp.Left(pos);
					DType* strippedDat = m_mp->FindExact(strippedScope);
					if (!strippedDat || NAMESPACE != strippedDat->type())
						break;

					// remove outer scope
					scp = scp.Mid(pos);
					pos = scp.Find(DB_SEP_STR);
					if (-1 == pos || scp == (DB_SEP_STR + sym))
					{
						// prevent search of just the sym
						break;
					}

					// search for truncated scope
					data = m_mp->FindExact(scp + DB_SEP_CHR + sym);
					if (data)
					{
						canUnderline = FALSE;
						break;
					}
				} while (!data);
			}
		}
	}
	else
	{
		WTString baseClass = GetBaseScope();
		// #define NameSpaceString ((g_devLang == CS)?GetScopeInfoPtr()->m_namespaces:s_namespaces)
		//		if(baseClass.GetLength() > 1 && !NameSpaceString.contains(baseClass + '\f'))
		//			NameSpaceString = baseClass + '\f' + NameSpaceString;

		const char symFirstChar = sym[0];
		if ((symFirstChar == 't' && sym == "this") ||
		    (UC == GetLangType() && Psettings->mUnrealScriptSupport && symFirstChar == 's' && sym == "self"))
		{
			// [case: 64250] UC "self" same as "this"
			data = m_mp->FindExact(baseClass);
			if (!data)
			{
				// [case: 4135]
				// get using directives for current file to see if can identify what "this" is
				const uint fid = m_mp->GetFileID();
				WTString fileScopedUsing;
				fileScopedUsing.WTFormat(":wtUsingNamespace_%x", fid);
				DTypeList dtList;
				m_mp->FindExactList(fileScopedUsing, dtList, false);
				// naive matching -- does not work for nested namespaces and
				// using directives that are not fully scoped.
				//		using namespace Outer;
				//		using namespace Inner; (fails, but "using namespace Outer::Inner" works)
				for (auto& d : dtList)
				{
					if (d.FileId() == fid && d.MaskedType() == RESWORD && d.Attributes() & V_IDX_FLAG_USING)
					{
						const WTString ns(d.Def());
						data = m_mp->FindExact(ns + baseClass);
						if (data)
							break;
					}
				}

				if (!data)
				{
					// naive search failed -- try all out search with namespace guessing and slow namespace logic
					WTString symscope;
					data =
					    m_mp->FindSym(&baseClass, &symscope, nullptr,
					                  FDF_TYPE | FDF_NoAddNamespaceGuess | FDF_NoConcat | FDF_SlowUsingNamespaceLogic);
				}
			}
		}
		else if ((symFirstChar == '_' && sym == "__super") ||
		         (Psettings->mUnrealEngineCppSupport && symFirstChar == 'S' &&
		          sym == "Super") // [case: 86215] Super is a typedef that is autogenerated as part of the header
		                          // generate process, so VA doesn't see it
		)
		{
			token2 tbc = m_mp->GetBaseClassList(baseClass, false, mMonitorForQuit, GetLangType());
			tbc.read('\f');
			WTString super = tbc.read('\f');
			if (super.GetLength())
				data = m_mp->FindExact(super);
		}
		else if (GetLangType() == CS && symFirstChar == 'b' && sym == "base")
		{
			data = m_mp->FindSym(&sym, NULL, &baseClass);
		}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		else if (m_deep && State(m_deep - 1).m_defType == PROPERTY && !sym.IsEmpty() && IsCFile(GetLangType()) &&
		         State(m_deep - 1).m_begLinePos && State(m_deep - 1).m_begLinePos[0] == '_' &&
		         StartsWith(State(m_deep - 1).m_begLinePos, "__property") &&
		         ((symFirstChar == 'd' && sym == "default") ||
		          (symFirstChar == 'i' && (sym == "index" || sym == "implements")) ||
		          (symFirstChar == 'n' && sym == "nodefault") || (symFirstChar == 'r' && sym == "read") ||
		          (symFirstChar == 's' && sym == "stored") || (symFirstChar == 'w' && sym == "write")))
		{
			// [case: 133417]
			// create a reserved word DType so that we don't pollute global
			// namespace with 'read' and 'write' RESWORD entries that would
			// conflict with system functions.  Before we started using DTypePtr
			// in State() this wouldn't have been possible and we would have
			// added read/write to the cpp.va file.  Note: is not VA_DB_BackedByDatafile.
			tempDataPtr =
			    std::make_shared<DType>("", "", (uint)RESWORD, uint(V_HIDEFROMUSER | V_VA_STDAFX), (uint)VA_DB_Cpp);
			State().m_lwData = tempDataPtr;
			data = tempDataPtr.get();
		}
#endif
		else
		{
			WTString scope = Scope();

			if (GetLangType() == UC)
			{
				if (m_deep && State(m_deep - 1).m_lastChar == '<')
				{
					// UC hack for "class <Foo> Foo;, so goto on <Foo> goes to the actual class, not the var Foo
					scope = baseClass;
				}
			}

			WTString bcl = m_mp->GetBaseClassList(baseClass, false, mMonitorForQuit, GetLangType());
			if (scope.length() > 1)
				bcl.prepend(m_mp->GetNameSpaceString(scope));

			if (m_deep && IsCFile(GetLangType()))
			{
				if (C_ENUM == State(m_deep - 1).m_defType)
				{
					// Enums: use parent scope so Find References finds all references
					bool useParentScope = true;
					if (State(m_deep - 1).m_defAttr & V_MANAGED)
					{
						// [case: 935] managed enums are not injected into parent scope
						useParentScope = false;
					}

					if (useParentScope)
					{
						// [case: 77550]
						// try fully scoped first
						DType* dt = m_mp->FindSym(&sym, &scope, &bcl, mHighVolumeFindSymFlag);
						if (dt)
						{
							State().m_lwData = std::make_shared<DType>(dt);
							if (OnSymbol())
							{
								// success using full scope so don't need to strip to parent scope
								useParentScope = false;
							}
							State().m_lwData.reset();
						}
					}

					if (useParentScope)
					{
						// try searching parent scope for enumItem (since not all enumItem uses are fully scoped)
						scope = StrGetSymScope(scope);
						bcl.Empty();
						if (!scope.GetLength())
							scope = DB_SEP_CHR;
					}
				}
				else if (FUNC == State(m_deep - 1).m_defType)
				{
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
					auto dt = State(m_deep - 1).m_lwData;
					if (dt && dt->IsCppbClassMethod())
					{
						// [case: 135861]
						bcl.prepend(":TMetaClass\f");
					}
#endif
				}
			}

			INT nxtC = m_cp + sym.GetLength();
			while (m_buf[nxtC] && wt_isspace(m_buf[nxtC]))
				nxtC++; // eat whitespace
			if (m_buf[nxtC] == '(')
				data =
				    m_mp->FindSym(&sym, &scope, &bcl, mHighVolumeFindSymFlag | FDF_CONSTRUCTOR | FDF_SplitBclForScope);
			else
				data = m_mp->FindSym(&sym, &scope, &bcl, mHighVolumeFindSymFlag | FDF_SplitBclForScope);

			nonXrefScope = scope;
			nonXrefBcl = bcl;

			if (data && C_ENUMITEM == data->type() && IsCFile(GetLangType()))
			{
				// [case: 78898]
				data = ::GetBestEnumDtype(m_mp.get(), data);
			}
			else if (data && (data->MaskedType() == VAR || data->MaskedType() == PROPERTY) && !InComment() &&
			         ::DefHasDuplicatedName(data))
			{
				// [case: 80234]
				// this block is based on the [case: 73412] section in GetCurWordScope
				LPCSTR p = CurPos();
				for (; ISCSYM(*p); p++)
					; // cwd
				for (; *p && wt_isspace(*p); p++)
					; // whitespace
				if (ISCSYM(*p))
				{
					WTString cls = data->Sym();
					_ASSERTE(cls == GetCStr(CurPos()));
					WTString baseScp(GetBaseScope());
					WTString bcl2 = m_mp->GetBaseClassList(baseScp, true, 0, GetLangType());
					WTString colonStr(DB_SEP_STR);
					DType* clsData = m_mp->FindSym(&cls, &colonStr, &bcl2, FDF_TYPE | FDF_NoConcat);
					if (!clsData)
						clsData = m_mp->FindSym(&cls, &baseScp, &bcl2, FDF_TYPE | FDF_NoConcat);
					if (clsData && IS_OBJECT_TYPE(clsData->type()))
						data = clsData;
				}
			}
			else if (data && ((data->MaskedType() == RESWORD && sym.contains("_cast")) ||
			                  (Psettings->mUnrealEngineCppSupport && sym == "Cast")))
			{
				// static_cast/dynamic_cast/const_cast/reinterpret_cast<type>
				WTString cast = TokenGetField(CurPos() + sym.GetLength(), "< \t&*[]>");
				DType* castdata = m_mp->FindSym(&cast, &scope, &bcl);
				if (!castdata && cast.contains("::"))
				{
					// static_cast<NS::type> change type to ":NS:type" // TOPIC_ID=4936
					cast.ReplaceAll("::", DB_SEP_STR);
					cast.prepend(DB_SEP_STR.c_str());
					castdata = m_mp->FindSym(&cast, &scope, &bcl);
				}
				if (castdata)
					data = castdata;
			}

			static const WTString setStr = DB_SEP_STR + "set-";
			if (!data && sym == "value" && scope.contains(setStr))
			{
				WTString prop = TokenGetField(scope, "-");
				data = m_mp->FindExact2(prop);
			}

			if (data && data->MaskedType() != RESWORD)
			{
				// [case: 80352] [case: 85360] [case: 85587]
				data = ::TraverseUsing(data, m_mp.get());
			}

			if (!data)
			{
				static const WTString TemplateParameter = DB_SEP_STR + "TemplateParameter" + DB_SEP_STR;
				data = m_mp->FindExact2(TemplateParameter + sym);
				if (!data)
				{
					data = ::FindForwardDeclaration(m_mp.get(), scope, sym);

					if (!data && Psettings->mScopeFallbackToGotodef)
					{
						// [case: 79066] source file #included in another file
						// [case: 69085] function defined in one source file and
						// used in a different one with no declaration in header
						// file.  Compiles with GCC (due to old C rules?).
						// Symbol will be in project db as a GOTODEF.
						DType* ldt = g_pGlobDic->FindExactObj(sym, 0);
						if (ldt && GOTODEF == ldt->MaskedType())
							data = ldt;
					}

					if (!data)
					{
						if (canUnderline && m_deep > 2 && sym == "value")
							canUnderline = FALSE;
						if (scope.contains("_asm") || bcl.contains(WILD_CARD))
							canUnderline = FALSE;
						// catch value in properties
					}
				}
			}

			if (data && State().m_lastChar == '*' && State().m_begLinePos[0] == '*')
			{
				// operator*() support
				if (InParen(m_deep)) // only if inparen "(*x)." - Case 9155:
				{
					WTString bcl2 = m_mp->GetBaseClassList(data->SymScope(), false, mMonitorForQuit, GetLangType());
					WTString astr('*');
					DType* opData = m_mp->FindSym(&astr, NULL, &bcl2);
					if (opData) // set parent typecast data to operator data, so scope and Find references still works.
						State(m_deep - 1).m_lwData = std::make_shared<DType>(opData);
				}
			}
		}
	}

	if (!data && (Is_Tag_Based(GetFType()) || GetFType() == VBS))
	{
		// Guess at BCL
		if (!IsXref())
			bc = GetBaseScope();
		data = m_mp->GuessAtBaseClass(sym, bc);
	}
	else if (data && data->IsTemplate() && ((InLocalScope() && data->type() == FUNC) || mIgnoreLocalTemplateCheck))
	{
		// template function
		WTString tmp(GetTemplateFunctionCallTemplateArg(data, nonXrefScope, nonXrefBcl));
		if (!tmp.IsEmpty())
		{
			// [case: 85112] [case: 78153]
			// [case: 5690] change bc to include the actual template parameter
			if (IsXref() && PrevChar() == ':')
				tmp = data->SymScope() + tmp;
			else
				tmp = sym + tmp;
			if (!tmp.IsEmpty())
			{
				// #AUTO_INFER 2nd step
				bool is_va_passthrough_type = sym == "__va_passthrough_type";

				DType* tmpData = m_mp->FindSym(&tmp, &nonXrefScope, &nonXrefBcl, mHighVolumeFindSymFlag);
				if (!tmpData)
				{
					// cause template instantiation via GetBaseClassList call
					// [case: 78153]
					const WTString potentialTemplateSym(tmp[0] == DB_SEP_CHR ? tmp : DB_SEP_STR + tmp);
					const WTString potentialBcl(
					    m_mp->GetBaseClassList(potentialTemplateSym, false, mMonitorForQuit, GetLangType()));

					if (!is_va_passthrough_type)
					{
						// do as before
						if (!potentialBcl.IsEmpty())
						{
							if (-1 != potentialBcl.Find(WILD_CARD) || potentialBcl == "\f")
							{
								// [case: 119789] try to determine if this is a template function in a class;
								// if it is, don't set data to nullptr but use already found sym in data
								bool isTemplFuncInClass = false;
								WTString symScope = StrGetSymScope(data->SymScope());
								if (!symScope.IsEmpty() && symScope != ":")
								{
									int tokPos = 0;
									CStringW bcl = nonXrefBcl.Wide();
									WTString tok = bcl.Tokenize(L"\f", tokPos);
									while (!tok.IsEmpty())
									{
										if (tok.EndsWith(symScope))
										{
											isTemplFuncInClass = true;
											break;
										}

										tok = bcl.Tokenize(L"\f", tokPos);
									}
								}

								if (!isTemplFuncInClass) // [case: 119789] if not template function in a class do as before
									data = nullptr;
							}
							else
								tmpData = m_mp->FindSym(&potentialTemplateSym, &nonXrefScope, &potentialBcl, mHighVolumeFindSymFlag);
						}
					}
					else
					{
						// this seems to be working better for va_passthrough_type
						const WTString emptyBcl;
						tmpData = m_mp->FindSym(&potentialTemplateSym, &nonXrefScope, &emptyBcl, mHighVolumeFindSymFlag);
					}
				}

				if (tmpData && tmpData != data)
					data = tmpData;
			}
			else
				data = nullptr;
		}
	}

	if (data)
	{
		if (!State().m_lwData || State().m_lwData.get() != data)
			State().m_lwData = std::make_shared<DType>(data);

		OnSymbol();
		if (m_deep && !State(m_deep - 1).m_lwData && State(m_deep - 1).m_lastChar == '(')
		{
			if (m_deep > 1 && !State(m_deep - 2).m_lwData && State(m_deep - 2).m_lastChar == '(')
			{
				// handle typecast ((CWnd*)p)->
				State(m_deep - 2).m_lwData = State().m_lwData;
				State(m_deep - 2).m_parseState = VPS_ASSIGNMENT;
				State(m_deep - 2).m_defType = UNDEF;
			}
			else if (StartsWith(State(m_deep - 1).m_begLinePos, "auto"))
			{
				// [case: 13607] c++11 auto
				State(m_deep - 1).m_lwData = State().m_lwData;
				State(m_deep - 1).m_parseState = VPS_ASSIGNMENT;
				if (State(m_deep - 1).m_defType != Lambda_Type)
					State(m_deep - 1).m_defType = LINQ_VAR;
			}
		}
	}
	else
	{
		State().m_lwData.reset();
		if(IsStateDepthOk(m_deep + 1))
			State(m_deep + 1).m_dontCopyTypeToLowerState = true;

		if (!InComment() && !m_mp->GetMacro(sym))
		{
			OnUndefSymbol();
			// don't underline [uuid's] or [ C# attributes]
			for (ULONG i = 1; i <= m_deep && canUnderline; i++)
				if (m_deep && State(m_deep - i).m_lastChar == '[' && !InLocalScope(m_deep - i))
					canUnderline = false; // [csharp Attribute]
			if (sym == "f")
				canUnderline = FALSE; // 0.f
			if (canUnderline)
			{
				DebugMessage("SymNotFound");
				// make sure we are not in a macro
				if ((!m_deep || !GetCWData(m_deep - 1) || GetCWData(m_deep - 1)->MaskedType() != DEFINE))
					OnError(CurPos());
			}
		}
	}
}

void VAParseMPScope::OnCSym()
{
	VAParseMPReparse::OnCSym();
	// This call gets the DType for the current symbol if m_Scoping
	// Calls OnError if not found, which underlines if this is the underline class
	DEFTIMERNOTE(VAP_OnCSym1, NULL);

	if (m_startReparseLine && !m_Scoping && (m_curLine + 3) > m_firstVisibleLine)
	{
		if (Is_Tag_Based(GetLangType()) && State().m_begLinePos <= CurPos())
			m_Scoping = TRUE; // In HTML begLine points to "<" in "<tag", not the current csym
		else if (State().m_begLinePos == CurPos())
			m_Scoping = TRUE; // don't start scoping in the middle of a line.
	}
	if (m_Scoping && !wt_isdigit(CurChar()))
		DoScope();
}

void VAParseMPScope::DebugMessage(LPCSTR msg)
{
#ifdef _DEBUG
	if (GetScopeInfoPtr()->GetFilename().GetLength())
	{
		WTString mstr;
		CString fname(GetScopeInfoPtr()->GetFilename());
		mstr.WTFormat("%s(%lu) : VAP: %s %s\r\n", (LPCSTR)fname, m_curLine, msg, WTString(GetParseClassName()).c_str());
		::OutputDebugStringW(mstr.Wide());

		//		if(WTString(msg) == "OnSymbol Exception")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Assuming method definition")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Macro missing arg")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "NonCSym")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Invalid char")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Macro missing arg")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Semicolon unexpected")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "Brace Mismatch")
		//			_asm nop; // Breakpoint
		//		else if(WTString(msg) == "SymNotFound")
		//			_asm nop; // Breakpoint
		//		else
		//			_asm nop; // Breakpoint
	}
#endif // _DEBUG
}

DTypePtr VAParseMPScope::GetCWData(ULONG deep)
{
	if ((int)deep < 0)
		return NULL;
	return State(deep).m_lwData;
}

WTString VAParseMPScope::GetDataKeyStr(ULONG deep)
{
	if ((int)deep < 0 || !State(deep).m_lwData)
		return NULLSTR;
	return State(deep).m_lwData->SymScope();
}

void VAParseMPScope::OnChar()
{
	// adds operator support
	if (m_Scoping && !InComment())
	{
		DTypePtr data = GetCWData(m_deep);
		if (data)
		{
			const int kLangType = GetLangType();
			if (!(Is_C_CS_File(kLangType)))
				return;

			CHAR c = CurChar();
			// Operator support
			LPCSTR op = NULL;
			if (CS == kLangType)
			{
				if (c == '[')
					op = "this[]";
			}
			else if (c == '(' && data->MaskedType() == VAR)
			{
				op = "()";
			}
			else
			{
				// [case: 23811] make this check even if (data->mType&V_POINTER) because
				// data might be a pointer to a type with overridden operators
				if (c == '[')
				{
					if (!(data->IsPointer()))
						op = "[]";
					else if (VAR != data->MaskedType() ||
					         data->Def().Find('[') == -1) // [case: 8395] don't set op for var decl std::string p[2];
						op = "[]";
				}
				else if (c == '-' && CurPos()[1] == '>' && !data->IsPointer())
					op = "->";
			}

			if (op)
			{
				WTString opStr(op);
				DType* opData = m_mp->FindOpData(opStr, GetDataKeyStr(m_deep), mMonitorForQuit);
				if (opData)
					State().m_lwData = std::make_shared<DType>(opData);
			}
		}
	}
	__super::OnChar();
}

BOOL VAParseMPScope::IsDone()
{
	_ASSERTE(m_cp <= mBufLen);
	if (m_inMacro)
		return __super::IsDone(); // Don't bail while in a macro, m_parseTo does not apply
	return m_parseTo <= m_cp;
}

void VAParseMPScope::ParseTo(const WTString& buf, int parseTo /*= 0*/, MultiParsePtr mp /*= nullptr*/)
{
	m_parseTo = parseTo;
	Parse(buf, mp);
}

void VAParseMPScope::Parse(const WTString& buf, MultiParsePtr mp /*= nullptr*/)
{
	DB_READ_LOCK;
	// #if defined(_DEBUG) && defined(SEAN)
	// 	PerfTimer pt("VAParseMPScope::Parse", true);
	// #endif // _DEBUG && SEAN
	m_mp = mp;
	if (GetLangType() == Src || GetLangType() == Header)
		m_processMacros = TRUE;
	m_addedSymbols = 0;
	m_Scoping = FALSE;
	Init(buf);
	LoadFromCache((int)m_startReparseLine); // Pass m_startReparseLine for cache pos, case=24445
	DoParse();
}

void VAParseMPScope::LoadLocalsAndParseFile(LPCWSTR file)
{
	// Loads locals, then parses the file.
	MultiParsePtr mp = MultiParse::Create();
	LoadLocals(file, mp);
	m_mp = mp;
	Init(m_bufStorage);
	if (GetLangType() == Src || GetLangType() == Header)
		m_processMacros = TRUE;
	DoParse();
}

void VAParseMPScope::LoadLocals(LPCWSTR file, MultiParsePtr mp)
{
	m_bufStorage = GetFileText(file);

	{
		DatabaseDirectoryLock lck;
		if (!mp->IsIncluded(file, TRUE))
		{
			// Copied some parts from FileParserWorkItem::DoParseWork		**** this has not been maintained ****
			const int fType = GetFileType(file);
			auto mp2 = MultiParse::Create(fType);
			mp2->SetCacheable(TRUE);
			if (StopIt)
				return;

			const BOOL sysFile = IncludeDirs::IsSystemFile(file);

			// load into project instead of cache if file is in project [case: 2842]
			// load into project if file is new to project (and doesn't already exist in the project) [case: 4374]
			bool parseIntoProject = !sysFile && (g_pGlobDic && (GlobalProject && GlobalProject->IsBusy()));

			if (!parseIntoProject && Psettings->m_FastProjectOpen && GlobalProject)
			{
				// [case: 30226] if we don't parse all project files at load, then can't rely on GetFileData
				if (GlobalProject->Contains(file))
					parseIntoProject = true;
			}

			mp2->RemoveAllDefs(file, DTypeDbScope::dbSolution);

			if (parseIntoProject)
			{
				// add an entry for the file so that VADatabase::DBIdFromParams returns DbTypeId_Solution instead of
				// DbTypeId_ExternalOther
				UINT fileId = gFileIdManager->GetFileId(file);
				std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(fileId, DbTypeId_Solution));
				if (dbw)
					dbw->DBOut(WTString("+projfile"), WTString(file), UNDEF, V_FILENAME | V_HIDEFROMUSER, 0, fileId);
			}

			LPCTSTR fTxt = m_bufStorage.IsEmpty() ? NULL : (LPCTSTR)m_bufStorage.c_str();

			if (fType == Src)
				; // mp->ParseFileForGoToDef(file, !!sysFile, fTxt);
			else
				mp2->FormatFile(file, uint(sysFile ? V_SYSLIB : V_INPROJECT), ParseType_Globals, true,
				                fTxt); // get only global stuff
		}
		mp->FormatFile(file, V_INPROJECT | V_INFILE, ParseType_Locals, true, m_bufStorage.c_str());
	}
	m_mp = mp;
}

class ParseForwardFromExistingParserState : public VAParseMPScope
{
	VAParseMPScope* mStartingParser = nullptr;

  public:
	ParseForwardFromExistingParserState() = delete;

	ParseForwardFromExistingParserState(VAParseMPScope* startFrom)
	    : VAParseMPScope(startFrom->FileType()), mStartingParser(startFrom)
	{
		_ASSERTE(mStartingParser);
		m_mp = mStartingParser->m_mp;
		m_useCache = FALSE;
		m_updateCachePos = FALSE;
		m_writeToFile = FALSE;
		mParseLocalTypes = mStartingParser->mParseLocalTypes;
		m_parseGlobalsOnly = mStartingParser->m_parseGlobalsOnly;
		mBufCache = mStartingParser->mBufCache;
		mConcatDefsOnAddSym = false;
		mIgnoreLocalTemplateCheck = mStartingParser->GetLocalTemplateCheckFlag();
	}

	// this works when startingParser is at auto, and caller is passing
	// pos that is offset of foo relative to start of buffer:
	//		auto foo = bar;
	// in other words, not very general -- might work for other cases, but not attempted/tested
	DTypePtr GetSymAtPos(int pos)
	{
		ParseToSym(pos);
		return GetCWData(m_deep);
	}

  protected:
	void ParseToSym(int pos)
	{
		// modified implementation of VAParseMPScope::ParseTo
		m_parseTo = pos + 1;

		{
			// modified implementation of VAParseMPScope::Parse
			DB_READ_LOCK;
			m_processMacros = mStartingParser->GetProcessMacrosFlag();
			mConditionallyProcessedMacroBraceDepth = mStartingParser->GetProcessMacroBraceDepth();
			m_addedSymbols = 0;
			m_Scoping = TRUE;

			{
				// modified implementation of VAParseBase::Init
				m_InSym = FALSE;
				m_inMacro = mStartingParser->GetInMacro();
				m_inDirectiveType = mStartingParser->GetInDirectiveType();
				m_inDirective = mStartingParser->GetInDirective();
				m_parseFlags = mStartingParser->GetParseFlags();
				LoadParseState(mStartingParser, true);
				m_cp =
				    pos; // assumption about caller behavior, might not translate to uses other than how currently used
			}

			DoParse();
		}
	}
};

DTypePtr VAParseMPScope::ResolveAutoVarAtCurPos(DTypePtr pDat, WTString* originalDef /*= nullptr*/)
{
	// [case: 69239]
	// use data of next sym instead of the reserved word auto/var
	_ASSERTE(pDat && pDat->type() == RESWORD &&
	         ((IsCFile(GetLangType()) && pDat->SymScope() == ":auto") ||
	          (CS == GetLangType() && pDat->SymScope() == ":var")));

	LPCSTR p = CurPos();
	_ASSERTE(p && *p);
	if (!p || !*p)
		return pDat;

	for (; ISCSYM(*p); p++)
		; // cwd
	for (; *p && (wt_isspace(*p) || WTStrchr("*^&", *p)); p++)
		; // whitespace, ptr, ref
	if (!(ISCSYM(*p)))
		return pDat;

	if (IsCFile(GetLangType()))
	{
		// [case: 142756]
		// do inexpensive lambda check before calling SymFromPos, avoiding any parse classes.
		// In following example code, p at this time points to REPORT:
		//		auto REPORT = [&](bool val) -> split_report
		int readLen = ::GetSafeReadLen(p, GetBuf(), GetBufLen());
		if (readLen > 50)
			readLen = 50;
		WTString tmp(p, readLen);
		int pos = tmp.Find('{');
		if (-1 != pos)
		{
			tmp = tmp.Left(pos);
			pos = tmp.Find('=');
			if (-1 != pos)
			{
				pos = tmp.Find('[');
				if (-1 != pos)
				{
					if (::IsLambda(tmp.c_str() + pos, tmp))
						return pDat;
				}
			}
		}
	}

	ParseForwardFromExistingParserState pf(this);
	DTypePtr clsData = pf.GetSymAtPos(ptr_sub__int(p, m_buf));
	if (clsData && (clsData->type() == LINQ_VAR || clsData->type() == VAR))
	{
		DType* pOrigDtypeBeforeModByBclUpdate = m_mp->FindMatch(clsData);
		// clsData is the DType of our generated var declaration.
		// now get the DType of the type the var is inferred to be.
		const WTString bcl = m_mp->GetBaseClassList(clsData->SymScope(), false, mMonitorForQuit, GetLangType());
		if (!pOrigDtypeBeforeModByBclUpdate || pOrigDtypeBeforeModByBclUpdate->IsDontExpand())
		{
			if (originalDef)
			{
				if (pOrigDtypeBeforeModByBclUpdate)
				{
					// get the updated definition where auto has been replaced with
					// the type deduced by GetBaseClassList / InferTypeFromAutoVar
					*originalDef = pOrigDtypeBeforeModByBclUpdate->Def();
				}
				else
					*originalDef = clsData->Def();
			}

			if (!bcl.IsEmpty() && -1 == bcl.Find(WILD_CARD) && -1 == bcl.Find("VAiterator"))
			{
				token2 tbc = bcl;
				const WTString b = tbc.read('\f');
				if (b.GetLength())
				{
					DType* dt = m_mp->FindExact(b);
					if (dt)
					{
						clsData = std::make_shared<DType>(dt);
						return clsData;
					}

					clsData.reset();
				}
			}
		}
	}

	return pDat;
}

//////////////////////////////////////////////////////////////////////////
// Find Usage Class

void VAParseMPFindUsage::FindUsage(const CStringW& project, const CStringW& file, MultiParsePtr mp,
                                   FindReferences* refs, const WTString& buf)
{
	DB_READ_LOCK;
	mProject = project;
	m_fileName = file;
	m_FindSymRef = refs;
	m_lastLine = UINT_MAX;
	m_foundUsage = FALSE;
	mLastOnSymCp = UINT_MAX;
	if (!mp->FindExact2(m_FindSymRef->GetFindScope()))
	{
		// DbSink/RemoveAllDefs may have removed the previous definition, we need to add it again
		mp->add(m_FindSymRef->GetFindScope(), WTString("Renamed"), DLoc, VAR);
	}
	SetNextPos(buf.c_str());

	//	Parse(buf, mp); // don't LoadFromCache
	m_mp = mp;
	m_addedSymbols = 0;
	m_Scoping = FALSE;
	if (m_FindSymRef->flags & FREF_Flg_FindErrors)
		m_Scoping = TRUE;

	if (!m_FindSymRef->IsBclInited())
		m_FindSymRef->InitBcl(mp.get(), GetLangType());

	Init(buf);
	// Set scoping if it is found near the top
	if (m_symPos && &m_buf[m_cp + 1000] > m_symPos)
		m_Scoping = TRUE; // we are close.

	//	LoadFromCache(); -Not
	if (GetLangType() == Src || GetLangType() == Header)
		m_processMacros = TRUE;
	DoParse();
	if (!m_foundUsage)
		m_FindSymRef->RemoveLastUnrelatedComments(file);
}

void VAParseMPFindUsage::OnCSym()
{
	if (CurChar() == '~' || (CurChar() == '!' && ISCSYM(NextChar())))
		IncCP(); // skip ~ in destructor

	VAParseMPScope::OnCSym();
	if (m_FindSymRef->flags & FREF_Flg_FindErrors)
	{
		//		VAParseMPScope::OnCSym();
		return;
	}

	// don't scope until we are close to the sym we are looking for
	DEFTIMERNOTE(VAP_OnCSymFU, NULL);
	if (!m_Scoping)
	{
		if (m_symPos && &m_buf[m_cp + 1000] > m_symPos)
			m_Scoping = TRUE; // we are close.
	}
	else
	{
		if (m_symPos && CurPos() > m_symPos && !m_inMacro)
		{
			FindNextSymPos();
		}
	}
}

BOOL VAParseMPFindUsage::IsDone()
{
	_ASSERTE(m_cp <= mBufLen);
	if (mMonitorForQuit && *mMonitorForQuit)
		return true;

	if (InComment() && CurPos() == m_symPos)
	{
		// [case: 69958] http://msdn.microsoft.com/en-us/library/3sxhs2ty%28v=vs.110%29.aspx
		static const WTString kHash("#");
		static const WTString kPreprocDirectives("#define#error#import#undef#elif#if#include#using#else#ifdef#line#endif#ifndef#pragma#region#");
		const int directivePos = kPreprocDirectives.Find(kHash + m_FindSymRef->GetFindSym() + kHash);

		// State().m_lastScopePos is not valid, get previous word manually
		LPCSTR p = CurPos();
		while (p > m_buf && wt_isspace(p[-1]))
			p--;
		while (p > m_buf && ISCSYM(p[-1]))
			p--;

		LPCSTR prev = nullptr;
		if (-1 != directivePos)
		{
			// FindSym matches a preproc directive -
			// rewind so that prev points to ch before sym so we can
			// check to see if actually used as preproc directive.
			prev = p;
			while (prev > m_buf && wt_isspace(prev[-1]))
				prev--;
		}

		if (-1 == directivePos || (prev && prev-- > m_buf && *prev != '#'))
		{
			if (CommentType() == '#')
			{
				// See if #define sym, or sym is just used in the #define
				if (StartsWith(p, "define"))
					AddRef(FREF_Definition); // #define sym
				else
					AddRef(); // sym is just used in the "#define something sym"
			}
			else if ((m_FindSymRef->flags & FREF_Flg_Reference_Include_Comments))
			{
				bool addCommentRef = true;
				if (m_FindSymRef->ShouldFindAutoVars())
				{
					// [case: 70281] don't show 'auto' and 'var' results in comments
					if (StartsWith(m_symPos, "auto") || StartsWith(m_symPos, "var"))
						addCommentRef = false;
				}

				if (addCommentRef)
					AddRef(FREF_Comment);
			}
		}

		SetNextPos(m_symPos + 1);
	}

	if (m_FindSymRef->flags & FREF_Flg_FindErrors)
		return FALSE;
	return !m_symPos; // parse until no more references in file
}

bool VAParseMPFindUsage::StartsWithSymOrEnumerator(LPCSTR buf, const WTString& begStr, BOOL wholeWord)
{
	if (m_FindSymRef->flags & FREF_Flg_Convert_Enum)
	{
		for (const auto& elem : m_FindSymRef->elements)
		{
			if (StartsWith(buf, elem, wholeWord))
				return true;
		}

		return false;
	}
	else
	{
		return StartsWith(buf, begStr, wholeWord);
	}
}

int VAParseMPFindUsage::AddRef(int flag /*= 0*/, LPCSTR overriddenScope /*= NULL*/, WTString* originalDef /*= nullptr*/)
{
	// Logic copied to VAParseMPUnderline::OnSymbol(), case=23538
	WTString ref;
	// prepare line for findref window (part 1)
	ULONG bl;
	// If the whole file is in one long line, the logic below has some issues.
	// Added a MAX_LINE_LEN to prevent FindreRerences from freezing up the UI thread.
#define MAX_LINE_LEN 1024
	for (bl = (ULONG)m_cp; bl && !strchr("\r\n", m_buf[bl - 1]) && (m_cp - bl) < MAX_LINE_LEN; bl--)
		;
	WTString preln(&m_buf[bl], uint(m_cp - bl));
	WTString sym;
	for (bl = (ULONG)m_cp; m_buf[bl] && ISCSYM(m_buf[bl]) && (bl - m_cp) < MAX_LINE_LEN; bl++)
		sym += m_buf[bl];
	WTString postln;
	for (; m_buf[bl] && !strchr("\r\n", m_buf[bl]) && (bl - m_cp) < MAX_LINE_LEN; bl++)
		postln += m_buf[bl];
	bool modifiedFoundUsage = false;
	// [case: 9860] tag-based parser can't trust IsComment since code can appear
	// in strings (though perhaps it should use FREF_Unknown)
	const bool funkyComments = GetLangType() == JS || Is_Tag_Based(GetLangType());
	if (flag != FREF_Comment || funkyComments)
	{
		if (!m_foundUsage)
		{
			m_foundUsage = TRUE;
			modifiedFoundUsage = true;
		}
	}

	// grab line text
	//	long tp = (long)m_cp; // TODO: isVSNET?TERRCTOLONG(m_curLine, m_cp-p):lp;
	bool checkForFuncDef = false;
	bool checkForMemberInit = false;
	FREF_TYPE type = FREF_Definition;
	LPCSTR p = CurPos();
	if (flag) // parsing error, let user see it as
	{
		type = (FREF_TYPE)flag;
		if ((m_FindSymRef->flags & FREF_Flg_Reference_Include_Comments) && flag == FREF_Comment &&
		    State().m_inComment == '#' && preln.FindNoCase("include") != -1)
		{
			char lastChar = preln[preln.GetLength() - 1];

			if (lastChar == '\"' || lastChar == '/' || lastChar == '\\')
				type = FREF_IncludeDirective; // #include ""
		}
		else if (Psettings->mFindRefsFlagCreation && flag == FREF_ReferenceAutoVar && Is_C_CS_File(GetFType()) &&
		         IsHeapOrStaticOrParam(originalDef))
		{
			type = FREF_Creation_Auto;
		}
	}
	else if (IsDef(m_deep) && StartsWithSymOrEnumerator(State().m_lastScopePos, m_FindSymRef->GetFindSym()) &&
	         (State().m_lastScopePos == State().m_lastWordPos ||
	          m_FindSymRef->GetOrigDtype()->MaskedType() == State().m_defType ||
	          (PROPERTY == m_FindSymRef->GetOrigDtype()->MaskedType() && VAR == State().m_defType))) // [case: 104019]
	{
		type = FREF_Definition;
		while (ISCSYM(p[0]))
			p++;
		while (wt_isspace(p[0]))
			p++;
		if (p[0] == '=' && p[1] != '=')
			type = FREF_DefinitionAssign; // (case:15881) int x = 1;
		else
		{
			if (IS_OBJECT_TYPE(m_FindSymRef->GetOrigDtype()->type()))
			{
				if (p[0] == '.' || (p[0] == ':' && p[1] == ':'))
					type = FREF_ScopeReference;
			}
			checkForMemberInit = true;
		}

		if (FREF_ScopeReference != type && StartsWithSymOrEnumerator(p, m_FindSymRef->GetFindSym()))
		{
			// [case: 80234]
			// PropertyType == PropertyName:
			// MyProp Myprop; // fools the else if into thinking this is def of the second
			type = FREF_Reference;
		}
	}
	else
	{
		checkForFuncDef = true;
		// Logic copied to VAParseMPUnderline::OnSymbol() - keep in sync
		while (ISCSYM(p[0]))
			p++;
		while (wt_isspace(p[0]))
			p++;

		// case 4985
		// if needs to be modified, modify the other case 4985 marked lines as well
		while (p[0] == '[')
		{
			int counter = 0;
			while (p[0] != ']' && p[0] != 0 && p[0] != ';' && p[0] != '{' && p[0] != '}')
			{
				if (counter++ > 128)
					break;
				p++;
			}
			if (p[0] == ']')
				p++;
		}

		LPCSTR p2 = p;
		while (p2[0] == ')')
			p2++;
		while (wt_isspace(p2[0]))
			p2++;
		if ((!InComment() || funkyComments) &&
		    ((p[0] == '=' && p[1] == '=') || (p2[0] == '=' && p2[1] == '='))) // [case: 71921] (*apple)==4
		{
			type = FREF_Reference;
		}
		else if ((!InComment() || funkyComments) &&
		         (p[0] == '=' ||
		          (p[0] != '>' && p[0] != '<' && p[0] != '!' && p[0] != ']' && p[1] == '=') // watch out for arr[n]=3
		          || p2[0] == '=' ||
		          (p2[0] != '>' && p2[0] != '<' && p2[0] != '!' && p2[0] != ']' &&
		           p2[1] == '=') // watch out for arr[n] =3
		          || (p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-') ||
		          (p[0] == '<' && p[1] == '<'))) // [case: 119662]
		{
			type = FREF_ReferenceAssign;
		}
		else if (preln.GetLength() >= 2)
		{
			// check for pre-increment/decrement
			WTString tmp(preln);
			tmp.TrimRight();
			if ((!InComment() || funkyComments) && (-1 != tmp.ReverseFind("++") || -1 != tmp.ReverseFind("--")))
			{
				// [case: 2493] rewind tmp over xrefs
				while (tmp.GetLength())
				{
					int eatChars = 0;
					int len = tmp.GetLength();
					char ch = tmp[len - 1];
					if ('.' == ch)
						eatChars = 1;
					else if (ISCSYM(ch))
						eatChars = 1;
					else if (':' == ch && len > 1 && ':' == tmp[len - 2])
						eatChars = 2;
					else if ('>' == ch && len > 1 && '-' == tmp[len - 2])
						eatChars = 2;
					else
						break;

					tmp = tmp.Left(len - eatChars);
				}

				if (tmp.ReverseFind("++") == tmp.GetLength() - 2 || tmp.ReverseFind("--") == tmp.GetLength() - 2)
					type = FREF_ReferenceAssign;
			}

			if (FREF_ReferenceAssign != type)
			{
				if (CommentType() == '#')
				{
					// [case: 9860]
					type = FREF_Unknown;
					if (preln.Find("#include") != -1)
						type = FREF_IncludeDirective;
					else if (preln.FindNoCase("include") != -1 && preln.GetLength() > 4)
					{
						char lastChar = preln[preln.GetLength() - 1];
						if (lastChar == '<' || lastChar == '/' || lastChar == '\\' || lastChar == '\"')
							type = FREF_IncludeDirective; // #include <> or #include <stringThatMatchesUnknownSym>
					}
					else if (preln.Find("#region") != -1 || preln.Find("#error") != -1 || preln.Find("#pragma") != -1 ||
					         preln.Find("#endregion") != -1)
					{
						// [case: 21070] treat #region Foo as comment
						type = FREF_Comment;
					}

					if ((FREF_IncludeDirective == type || FREF_Comment == type) &&
					    !(m_FindSymRef->flags & FREF_Flg_Reference_Include_Comments))
					{
						if (modifiedFoundUsage)
							m_foundUsage = FALSE;
						return 0;
					}
				}
				else if (InComment() && !funkyComments)
				{
					if (!(m_FindSymRef->flags & FREF_Flg_Reference_Include_Comments))
					{
						if (modifiedFoundUsage)
							m_foundUsage = FALSE;
						return 0;
					}
					type = FREF_Comment;
				}
				else if (m_deep && (0 == strncmp(State(m_deep - 1).m_begLinePos, "STDMETHOD(", 10) ||
				                    0 == strncmp(State(m_deep - 1).m_begLinePos, "STDMETHOD_(", 11)))
				{
					type = FREF_Definition;
				}
				else
				{
					type = FREF_Reference;
					checkForMemberInit = true;

					if (IS_OBJECT_TYPE(m_FindSymRef->GetOrigDtype()->type()))
					{
						while (ISCSYM(p[0]))
							p++;
						while (wt_isspace(p[0]))
							p++;
						if (p[0] == '.' || (p[0] == ':' && p[1] == ':'))
							type = FREF_ScopeReference;
					}
				}
			}
		}
		else if (InComment() && !funkyComments)
		{
			// [case: 9860]
			if (CommentType() == '#')
			{
				// multiline macro definition
				type = FREF_Unknown;
			}
			else
			{
				if (!(m_FindSymRef->flags & FREF_Flg_Reference_Include_Comments))
				{
					if (modifiedFoundUsage)
						m_foundUsage = FALSE;
					return 0;
				}
				type = FREF_Comment;
			}
		}
		else
		{
			type = FREF_Reference;
			checkForMemberInit = true;

			if (IS_OBJECT_TYPE(m_FindSymRef->GetOrigDtype()->type()))
			{
				while (ISCSYM(p[0]))
					p++;
				while (wt_isspace(p[0]))
					p++;
				if (p[0] == '.' || (p[0] == ':' && p[1] == ':'))
					type = FREF_ScopeReference;
			}
		}

		if (FREF_Reference == type && Psettings->mFindRefsFlagCreation && Is_C_CS_File(GetFType()) && m_FindSymRef)
		{
			const uint maskedType = m_FindSymRef->GetOrigDtype()->MaskedType();
			const bool reasonableType =
			    maskedType == CLASS || maskedType == STRUCT || maskedType == TYPE; // reasonable type, e.g. not function
			if (reasonableType)
			{
				if (IsNewBefore() || IsNewAfter() || IsHeapOrStaticOrParam() ||
				    IsCreateByTemplateFunction("make_shared;make_unique") ||
				    IsCreateByTemplateFunction(Psettings->mCreationTemplateFunctions) ||
				    IsCreateByTemplateMember("CreateInstance;CoCreateInstance") || // [case: 105558]
				    IsClassAndParens())
				{
					if (!FwIS("typedef") && !FwIS("using"))
						type = FREF_Creation;
				}
			}
		}
	}

	if (checkForMemberInit && State().m_lwData && State().m_lwData->MaskedType() == VAR &&
	    (p[0] == '(' || (p[0] == ' ' && p[1] == '(')))
	{
		// [case: 4154] fix for coloring of member inits
		if (FREF_Reference == type || FREF_ScopeReference == type)
			type = FREF_ReferenceAssign;
		else if (FREF_Definition == type)
			type = FREF_DefinitionAssign;
		else
			_ASSERTE(!"unexpected fref type in member init check");
	}

	if (checkForFuncDef && (FREF_Reference == type || FREF_ScopeReference == type) &&
	    State().m_parseState != VPS_ASSIGNMENT && // [case: 39943] watch out for: int foo = bar();
	    State().m_lwData && (IsDef(m_deep) || State().m_lwData->IsConstructor()) &&
	    State().m_lwData->MaskedType() == FUNC)
	{
		// [case: 4442] separation of implementation vs declaration in C++ can
		// cause problems when assigning ref type
		if (m_FindSymRef->GetFindScope() == State().m_lwData->SymScope())
		{
			type = FREF_Definition;
		}
		else
		{
			// NOTE: if you modify this block, also update the slightly modified
			// version that is in VAParseMPFindUsage::OnSymbol

			// Look for overridden method
			const WTString bc = GetDataKeyStr(m_deep);
			if (bc.GetLength())
			{
				// Is member of parent class?
				// i.e: Interface declaration
				const int res = m_FindSymRef->BclContains(WTHashKey(StrGetSymScope(bc)));
				if (res && res > -2000)
					type = FREF_Definition;
				else
				{
					// Is member of class derived from this?
					// i.e: implementation of this interface
					const WTString bcl = m_mp->GetBaseClassList(StrGetSymScope(bc), false, 0, GetLangType());
					ScopeHashAry ary(NULL, bcl, NULLSTR);
					const int res2 = ary.Contains(WTHashKey(StrGetSymScope(m_FindSymRef->GetFindScope())));
					if (res2 && res2 > -2000)
						type = FREF_Definition;
					// no else in this version
				}
			}
		}
	}

	if (XML == GetLangType() && FREF_Reference == type)
	{
		// [case: 76890]
		// special-case .config files
		const CStringW ext(::GetBaseNameExt(m_fileName));
		if (!ext.CompareNoCase(L"config"))
			type = FREF_Unknown;
	}

	// prepare line for findref window (part 2)
	if (preln.GetLength() && sym.GetLength() && wt_isalnum(preln[preln.GetLength() - 1]) && wt_isalnum(sym[0]))
		preln += ' ';
	if (sym.GetLength() && postln.GetLength() && wt_isalnum(sym[sym.GetLength() - 1]) && wt_isalnum(postln[0]))
		postln = ' ' + postln;

	preln.ReplaceAll("\t", " ");
	postln.ReplaceAll("\t", " ");
	preln.TrimLeft();

	WTString ln;
	if (Psettings->mLineNumbersInFindRefsResults)
	{
		ULONG i;
		for (i = m_deep; i && InLocalScope(i - 1); i--)
			;
		if (i)
		{
			WTString scope = CleanScopeForDisplay(StrGetSym(Scope(i)));
			ln.WTFormat("%s (%lu):    ", scope.c_str(), m_curLine);
		}
		else
			ln.WTFormat("(%lu):    ", m_curLine);
	}

	ln += (preln + ((type != FREF_ReferenceAssign && type != FREF_DefinitionAssign) ? MARKER_REF : MARKER_ASSIGN) +
	       sym + MARKER_NONE + postln);
	m_lastLine = m_curLine;
	ln.Trim();

	std::string context;

	// Translate m_cp from index to TER_RC
	int bline = m_cp;
	while (bline && !strchr("\r\n", m_buf[bline - 1]))
		bline--;

#ifdef ALLOW_MULTIPLE_COLUMNS
	const char* type_string;
	switch (type)
	{
	case FREF_Definition:
	case FREF_DefinitionAssign:
		type_string = "definition";
		break;
	case FREF_Reference:
	case FREF_ScopeReference:
	case FREF_ReferenceAutoVar:
		type_string = "reference";
		break;
	case FREF_ReferenceAssign:
		type_string = "assignment";
		break;
	case FREF_Unknown:
	case FREF_IncludeDirective:
	default:
		type_string = "?";
		break;
	case FREF_JsSameName:
		type_string = "overridden";
		break;
	};
	ln += format("\t%ld\t%s\t%s %s", m_curLine, type_string, context.c_str(), CleanScopeForDisplay(Scope()).c_str())
	          .c_str();
#endif
	if (m_FindSymRef && m_FindSymRef->flags & FREF_Flg_Convert_Enum)
	{
		// [case: 93116] we will not modify the enumerator definitions, just the uses
		if ((type == FREF_Definition || type == FREF_DefinitionAssign) && State().m_lwData &&
		    State().m_lwData->type() == C_ENUMITEM)
			return 0;
	}

	FindReference* fref = NULL;
	if (gShellAttr->RequiresPositionConversion())
		fref = m_FindSymRef->Add(mProject, m_fileName, m_curLine, ULONG(m_cp - bline + 1), UINT_MAX, type,
		                         ln.c_str()); // devenv
	else
		fref = m_FindSymRef->Add(mProject, m_fileName, m_curLine, ULONG(m_cp - bline + 1), (ULONG)m_cp, type,
		                         ln.c_str()); // msdev

	if (!fref)
		return 0;

	fref->mData = State().m_lwData;
	if (!fref->mData)
	{
		// [case: 103538]
		vLogUnfiltered("WARN: FindRef::AddRef empty data: %x [%s] [%s]", fref->type, WTString(m_fileName).c_str(), ln.c_str());
	}

	if (overriddenScope && !(m_FindSymRef->flags & FREF_Flg_Convert_Enum))
	{
		fref->overridden = TRUE;
		// Not sure how to best display overridden methods?
#ifdef ALLOW_MULTIPLE_COLUMNS
		context = overriddenScope;
#else
		fref->lnText = WTString("[") + WTString(overriddenScope) + WTString("] ") + ln;
#endif
	}

	return 1;
}

int VAParseMPFindUsage::OnSymbol()
{
	int retval = 0;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		// for some reason, this is freed sometimes.
		if (CurChar() == '~' || (CurChar() == '!' && ISCSYM(NextChar())))
			IncCP(); // skip ~ in destructor

		bool startsWithSymbol = CwIS(m_FindSymRef->GetFindSym());
		if (!startsWithSymbol && m_FindSymRef->flags & FREF_Flg_UeFindImplicit)
		{
			// [case: 141287] include hits for *_Implementation and *_Validate methods
			startsWithSymbol =
			    CwIS(m_FindSymRef->GetFindSym() + "_Implementation") || CwIS(m_FindSymRef->GetFindSym() + "_Validate");
		}

		if (!(!m_inMacro && (startsWithSymbol || m_FindSymRef->flags & FREF_Flg_Convert_Enum)))
		{
			if (!m_FindSymRef->ShouldFindAutoVars())
				return 0;

			if (!(CwIS("auto") || CwIS("var")))
				return 0;
		}

		if (!State().m_lwData)
			return 0;

		DTypePtr dType = State().m_lwData;
		dType = TraverseUsing(dType, m_mp.get());

		bool enumWorkaround = false;
		if (C_ENUMITEM == dType->MaskedType() && IsCFile(GetLangType()) && !dType->IsManaged())
		{
			if (m_cp != mLastOnSymCp /*&& !(m_FindSymRef->flags & FREF_Flg_Convert_Enum)*/)
			{
				// [case: 77550]
				// this is first pass with full enumItem scope, not truncated parent scope.
				// don't FindNextSymPos if no ref found so that second pass in
				// parent scope will occur at same (current) position.
				enumWorkaround = true;
			}
		}
		mLastOnSymCp = m_cp;

		const WTString lwDataKey(dType->SymScope());
		if (m_FindSymRef->ShouldFindAutoVars() && dType->type() == RESWORD)
		{
			if ((lwDataKey == ":auto" && m_FindSymRef->GetFindSym() != "auto") ||
			    (lwDataKey == ":var" && m_FindSymRef->GetFindSym() != "var"))
			{
				if (!((IsCFile(GetLangType()) && lwDataKey == ":auto") || (CS == GetLangType() && lwDataKey == ":var")))
					return 0;

				WTString originalDef;
				DTypePtr resolvedDat = ResolveAutoVarAtCurPos(dType, &originalDef);
				if (resolvedDat && resolvedDat != dType && resolvedDat->Sym() == m_FindSymRef->GetFindSym())
				{
					AddRef(FREF_ReferenceAutoVar, nullptr, &originalDef);
					FindNextSymPos();
					return 1;
				}
				else
				{
					_ASSERTE(CwIS("auto") || CwIS("var"));
					return 0;
				}
			}
		}

		static const char templateEncode = EncodeChar('<');
		const WTString kFindScope(m_FindSymRef->GetFindScope());
		if (-1 != lwDataKey.Find(templateEncode) && kFindScope == StripEncodedTemplates(lwDataKey))
			retval = AddRef();
		else if (kFindScope == lwDataKey)
			retval = AddRef();
		else if (m_FindSymRef->flags & FREF_Flg_UeFindImplicit &&
		         (kFindScope + "_Implementation" == lwDataKey || kFindScope + "_Validate" == lwDataKey))
			retval = AddRef(); // [case: 141287] include hits for *_Implementation and *_Validate methods
		else if (dType->IsConstructor() && 0 == StrGetSymScope(lwDataKey).Find(kFindScope))
		{
			// Con/Destructor for class
			// [case: 83847] changed scope compare from contains to begins with
			retval = AddRef();
		}
		else if ((lwDataKey.contains("~") || (lwDataKey.contains("!") && !lwDataKey.contains("!="))) &&
		         0 == StrGetSymScope(lwDataKey).Find(kFindScope))
		{
			// Destructor/finalizer for class
			// [case: 83847] changed scope compare from contains to begins with
			retval = AddRef();
		}
		else
		{
			const int unnamedPos = lwDataKey.Find(kUnnamed);
			if (unnamedPos != -1 &&
			    kFindScope.Mid(0, unnamedPos) == StrGetSymScope(kFindScope) && // I don't understand this condition...
			    lwDataKey.Mid(0, unnamedPos) == StrGetSymScope(kFindScope))
			{
				// members of unnamed structs
				retval = AddRef();
			}
			else if (Is_HTML_JS_VBS_File(GetLangType()) && !Is_Tag_Based(GetLangType()))
			{
				// In JS, overridden members is not really clear,
				// so we just list all members with the same name as possible overrides
				if (!kFindScope.contains("-")                      // Do not list local vars
				    && StrGetSymScope(kFindScope).GetLength() > 1) // Do not list global vars
				{
					WTString bc = GetDataKeyStr(m_deep);
					if (bc.GetLength() && StrGetSymScope(bc).GetLength() > 1 // Do not list globals vars
					    && !bc.contains("-"))                                // Do not list local vars
						retval = AddRef(FREF_JsSameName, CleanScopeForDisplay(bc).c_str());
				}
			}
			else
			{
				// NOTE: if you update this else block, also update the slightly modified
				// version that is in VAParseMPFindUsage::AddRef

				// Look for overridden method
				const WTString bc = GetDataKeyStr(m_deep);
				if (bc.GetLength())
				{
					DType* findSym = m_FindSymRef->GetOrigDtype();
					_ASSERTE(findSym);
					_ASSERTE(!findSym->IsEmpty() ||
					         (RefactoringActive::IsActive() &&
					          (VARef_ChangeSignature == RefactoringActive::GetCurrentRefactoring() ||
					           VARef_Rename_References_Preview == RefactoringActive::GetCurrentRefactoring())));
					// Is member of parent class?
					// i.e: Interface declaration
					int res = m_FindSymRef->BclContains(WTHashKey(StrGetSymScope(bc)));
					if (res && res > -2000)
					{
						// [case: 80234]
						if (findSym->IsEmpty() ||
						    !((dType->type() == VAR || dType->type() == PROPERTY) &&
						      ::DefHasDuplicatedName(dType.get())) ||
						    dType->IsEquivalentType(findSym->type()) || dType->IsOverride() || findSym->IsOverride())
						{
							retval = AddRef(0, CleanScopeForDisplay(bc).c_str());
						}
					}
					else if (!findSym->ScopeHash() && findSym->type() == FUNC)
					{
						// [case: 140823]
						// globals functions don't participate in inheritance
						m_FindSymRef->RemoveLastUnrelatedComments(m_fileName);
					}
					else
					{
						// Is member of class derived from this?
						// i.e: implementation of this interface
						const WTString bcl = m_mp->GetBaseClassList(StrGetSymScope(bc), false, 0, GetLangType());
						ScopeHashAry ary(NULL, bcl, NULLSTR);
						if (ary.Contains(WTHashKey(StrGetSymScope(kFindScope))))
						{
							// [case: 80234]
							if (findSym->IsEmpty() || dType->IsEquivalentType(findSym->type()) || dType->IsOverride() ||
							    findSym->IsOverride())
							{
								retval = AddRef(0, CleanScopeForDisplay(bc).c_str());
							}
						}
						else
						{
							bool doRemove = true;
							if (enumWorkaround)
							{
								if (m_FindSymRef->flags & FREF_Flg_Convert_Enum)
								{
									const WTString sym(dType->Sym());
									auto search = m_FindSymRef->elements.find(sym);
									if (m_FindSymRef->elements.end() == search)
									{
										// [case: 93116]
										enumWorkaround = false;
									}
								}

								if (enumWorkaround)
								{
									WTString s1(StrGetSymScope(kFindScope));
									s1 = StrGetSymScope(s1);
									if (s1.GetLength())
									{
										s1 += DB_SEP_STR + StrGetSym(kFindScope);
										if (ary.Contains(WTHashKey(StrGetSymScope(s1))))
										{
											retval = AddRef();
											doRemove = false;
										}
									}
								}
							}

							if (doRemove)
								m_FindSymRef->RemoveLastUnrelatedComments(m_fileName);
						}
					}
				}
			}
		}

		if (!enumWorkaround || (enumWorkaround && retval))
			FindNextSymPos();
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("VAP:");
		DebugMessage("OnSymbol Exception");
	}
#endif // !SEAN
	return retval;
}

void VAParseMPFindUsage::OnError(LPCSTR errPos)
{
	if (m_FindSymRef->flags & FREF_Flg_FindErrors)
	{
		if (!m_inMacro && ISCSYM(CurChar())) // don't report mismatched braces due to macro problems
			AddRef(FREF_Unknown);
		return;
	}

	if (CwIS(m_FindSymRef->GetFindSym()))
		AddRef(FREF_Unknown);
}

BOOL VAParseMPFindUsage::ShouldExpandEndPos(int ep)
{
	return (m_inMacro || (m_buf + ep) <= m_symPos);
}

bool VAParseMPFindUsage::IsNewBefore()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	CommentSkipper cs(fType);
	for (int i = m_cp - 1; i >= 4; i--)
	{
		TCHAR c = m_buf[i];
		if (cs.IsCodeBackward(m_buf, mBufLen, i))
		{
			if (!ISCSYM(m_buf[i - 4]) && m_buf[i - 3] == 'n' && m_buf[i - 2] == 'e' && m_buf[i - 1] == 'w' &&
			    !ISCSYM(c))
				return true;

			if (!IsWSorContinuation(c) && c != '<' && !ISCSYM(c))
				return false;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsNewAfter()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	CommentSkipper cs(fType);
	int breakCounter = 0;
	for (int i = m_cp; i < mBufLen; i++)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCode(c))
		{
			if (i >= m_cp + 4 && !ISCSYM(m_buf[i - 4]) && m_buf[i - 3] == 'n' && m_buf[i - 2] == 'e' &&
			    m_buf[i - 1] == 'w' && !ISCSYM(c))
			{
				WTString symName;
				if (m_FindSymRef)
					symName = m_FindSymRef->GetFindSym();

				cs.Reset();
				int failureCounter = 0;
				WTString createdSym;
				for (int j = i + 1; j < mBufLen; j++)
				{
					if (++failureCounter >= MAX_SEARCH_LEN)
						return false;
					TCHAR c2 = m_buf[j];
					if (cs.IsCode(c2))
					{
						if (IsWSorContinuation(c2))
							continue;
						if (ISCSYM(c2) || c2 == '<') // the ability to include List<
						{
							createdSym += c2;
							continue;
						}

						createdSym.ReplaceAll("List<", "");
						return symName != createdSym;
					}
				}

				return true;
			}
			if (c == ';' || c == '{' || c == '}' || c == '?' || c == ':')
				return false;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsExtern()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	CommentSkipper cs(fType);
	int breakCounter = 0;

	for (int i = m_cp - 1; i >= 7; i--)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCodeBackward(m_buf, mBufLen, i))
		{
			if (IsWSorContinuation(c))
				continue;
			if (c == 'n' && m_buf[i - 1] == 'r' && m_buf[i - 2] == 'e' && m_buf[i - 3] == 't' && m_buf[i - 4] == 'x' &&
			    m_buf[i - 5] == 'e' && !ISCSYM(m_buf[i - 6]))
			{
				return true;
			}
			return false;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsHeapOrStaticOrParam(WTString* originalDef /*= nullptr*/)
{
	int fType = GetFType();
	if (!IsCFile(fType))
		return false;

	bool functionParam = IsFunctionParam();
	if (IsExtern())
		return false;

	CommentSkipper cs(fType);
	int breakCounter = 0;
	enum
	{
		SYM1,
		WHITESPACE,
		SYM2
	} state = SYM1;
	int sq = 0;
	for (int i = m_cp + 1; i < mBufLen; i++)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCode(c))
		{
			if (c == '<')
			{
				sq++;
				continue;
			}
			if (c == '>')
			{
				sq--;
				if (sq < 0)
					return false; // e.g. we're inside template parameters such as find SEntInFoliage references in the
					              // expression "PodArray<SEntInFoliage> m_arrEntsInFoliage;"
				continue;
			}
			if (sq > 0) // skip the template params in head creations. e.g. SeanTestClsT<int> v1;
				continue;
			if (c == ';' || c == ',' || c == ')')
			{
				if (state != SYM2) // we need sym1<ws>sym2<ws>; insted of just sym1<ws>;
					break;

				if (c == ',' || c == ')')
					return true;

				// recognizing stuff like "using SeanTestCls3 = SeanTestCls;" or "using
				// spaceRenameEmpty::DupeClassNameForRename;"
				int breakCounter2 = 0;
				CommentSkipper cs2(fType);
				for (int j = m_cp - 1; j >= 6; j--)
				{
					if (++breakCounter2 >= MAX_SEARCH_LEN)
						break;
					TCHAR c2 = m_buf[j];
					if (cs2.IsCodeBackward(m_buf, mBufLen, j))
					{
						if (IsWSorContinuation(c2) && m_buf[j - 1] == 'g' && m_buf[j - 2] == 'n' &&
						    m_buf[j - 3] == 'i' && m_buf[j - 4] == 's' && m_buf[j - 5] == 'u' &&
						    IsWSorContinuation(m_buf[j - 6]))
							break;
						if (!IsWSorContinuation(c2) && !ISCSYM(c2) && c2 != ':' && c2 != '=')
							return true;
					}
				}
				break;
			}
			if (state == SYM1 && IsWSorContinuation(c))
				state = WHITESPACE;
			if (state == WHITESPACE && ISCSYM(c))
				state = SYM2;
			if (!IsWSorContinuation(c) && !ISCSYM(c))
			{
				if (!functionParam && (c == '*' || c == '&')) // find the next comma
				{
					int breakCounter2 = 0;
					CommentSkipper cs2(fType);
					int parens = 0;
					for (int j = i + 1; j < mBufLen; j++)
					{
						if (++breakCounter2 >= MAX_SEARCH_LEN)
							break;
						TCHAR c2 = m_buf[j];
						if (cs2.IsCode(c2))
						{
							if (c2 == '(')
							{
								parens++;
								continue;
							}
							if (c2 == ')')
							{
								parens--;
								continue;
							}
							if (parens > 0)
								continue;

							if (c2 == '>') // > after * means we're inside a template. see AST FindReferencesCreation27
								return false;
							if (c2 == ',')
							{
								i = j;
								state = WHITESPACE;
								break; // continue the outer loop
							}
							if (c2 == ';' || c2 == '{' || c2 == '}')
								return false;
						}
					}
					continue;
				}
				break;
			}
		}
	}

	cs.Reset();
	sq = 0;
	int parens = 0;
	bool alphabet = false;
	WTString symName;
	if (m_FindSymRef)
		symName = m_FindSymRef->GetFindSym();
	breakCounter = 0;
	for (int i = m_cp + symName.GetLength(); i < mBufLen - 1; i++)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCode(c))
		{
			if (c == '<')
			{
				sq++;
				continue;
			}
			if (c == '>')
			{
				sq--;
				continue;
			}
			if (sq > 0) // skip the template params in head creations. ClassName obj<1>(1);
				continue;
			if (ISCSYM(c))
				alphabet = true;
			if (c == '(' || c == '{') // '{' is for brace initialization, i.e. TestCreation v1{ 1 };  '(' is for
			                          // TestCreation v2( 1 );
			{
				if (!IsInsideGlobalOrClassScope())
				{
					WTString def;
					if (originalDef)
						def = TokenGetField(*originalDef, "=");
					CommentSkipper cs2(fType);
					for (int j = 0; j < def.GetLength(); j++)
					{
						TCHAR c2 = def[j];
						if (cs2.IsCode(c2))
							if (c2 == '*')
								return false;
					}
					return alphabet;
				}
				else
				{
					parens++;
					continue;
				}
			}
			if (c == ')')
				return false;

			if (c == '*' || c == ';' || c == ':' || c == '.' || c == '{' || c == '}' || c == ',' || c == '&')
				return false;

			TCHAR pc = m_buf[i - 1];
			if (parens == 0 && c == '=' && pc != '!' && pc != '=' && pc != '+' && pc != '-' && pc != '*' && pc != '/' &&
			    pc != '&' && pc != '|' && m_buf[i + 1] != '=')
			{
				WTString symName2;
				if (m_FindSymRef)
					symName2 = m_FindSymRef->GetFindSym();

				CommentSkipper cs2(fType);
				int failureCounter = 0;
				WTString createdSym;
				int paren = 0;
				int sq2 = 0;
				for (int j = i + 1; j < mBufLen; j++)
				{
					if (++failureCounter >= MAX_SEARCH_LEN)
						return false;
					TCHAR c2 = m_buf[j];
					if (cs2.IsCode(c2))
					{
						if (IsWSorContinuation(c2))
							continue;
						if (c2 == '(')
						{
							paren++;
							continue;
						}
						if (c2 == ')')
						{
							paren--;
							continue;
						}
						if (c2 == '<')
						{
							sq2++;
							continue;
						}
						if (c2 == '>' && m_buf[j - 1] != '-' && m_buf[j - 1] != '=')
						{
							sq2--;
							continue;
						}
						if (paren > 0 || sq2 > 0)
							continue;
						if (ISCSYM(c2))
						{
							createdSym += c2;
							continue;
						}
						if (c2 == ':' || c2 == '.' || c2 == '-' || c2 == '>')
							continue;

						if (c2 == '(')
						{
							if (symName2 == createdSym)
								return false;
							else if (!createdSym.IsEmpty())
								return true;
						}
						// 						if (c2 == ';' || c2 == ',')
						// 							return true;
						// 						if (c2 == '{' && !IsInsideGlobalOrClassScope())
						// 							return true;
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsInitializerListItem()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	CommentSkipper cs(fType);
	int breakCounter = 0;
	for (int i = m_cp - 1; i >= 1; i--)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCodeBackward(m_buf, mBufLen, i))
		{
			if (c == ':')
			{
				CommentSkipper cs2(fType);
				breakCounter = 0;
				for (int j = i - 1; j >= 1; j--)
				{
					if (++breakCounter >= MAX_SEARCH_LEN)
						break;
					TCHAR c2 = m_buf[j];
					if (cs2.IsCodeBackward(m_buf, mBufLen, j))
					{
						if (c2 == ')')
							return true;
						if (!IsWSorContinuation(c2))
							return false;
					}
				}
				return false;
			}
			if (c == '{' || c == '}' || c == ';')
				return false;
		}
	}

	return false;
}

const char operator_overloading[] = {'+', '-', '*', '/', '%', '=', '!', '<', '>', '.', '&', '|', '^', '~', '[', ']', 0};

// is method name or operator
bool VAParseMPFindUsage::IsMethodNameBackwards(int pos)
{
	if (ISCSYM(m_buf[pos]))
		return true;

	int breakCounter = 0;
	CommentSkipper cs2(GetFType());
	for (int j = pos; j >= 7; j--)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;

		TCHAR c2 = m_buf[j];
		if (cs2.IsCode(c2))
		{
			for (int k = 0; operator_overloading[k] != 0; k++)
				if (c2 == operator_overloading[k])
					goto continue_j;
			if (IsWSorContinuation(c2))
				continue;
			// is operator keyword?
			if (!ISCSYM(m_buf[j + 1]) && c2 == 'r' && m_buf[j - 1] == 'o' && m_buf[j - 2] == 't' &&
			    m_buf[j - 3] == 'a' && m_buf[j - 4] == 'r' && m_buf[j - 5] == 'e' && m_buf[j - 6] == 'p' &&
			    m_buf[j - 7] == 'o' && IsWSorContinuation(m_buf[j - 8]))
				return true;
			// is delete keyword?
			if (!ISCSYM(m_buf[j + 1]) && c2 == 'e' && m_buf[j - 1] == 't' && m_buf[j - 2] == 'e' &&
			    m_buf[j - 3] == 'l' && m_buf[j - 4] == 'e' && m_buf[j - 5] == 'd' && IsWSorContinuation(m_buf[j - 6]))
				return true;
			// is new keyword?
			if (!ISCSYM(m_buf[j + 1]) && c2 == 'w' && m_buf[j - 1] == 'e' && m_buf[j - 2] == 'n' &&
			    IsWSorContinuation(m_buf[j - 3]))
				return true;
			return false;
		continue_j:;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsFunctionParam()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	CommentSkipper cs(fType);
	int breakCounter = 0;
	enum
	{
		DEFAULT,
		OPENING_PAREN,
		WHITESPACE,
	} state = DEFAULT;
	int parenCount = 0; // see VAAutoTest:FindReferencesCreation19
	for (int i = m_cp - 1; i >= 1; i--)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCodeBackward(m_buf, mBufLen, i))
		{
			if (c == ')')
			{
				parenCount++;
				state = DEFAULT;
				continue;
			}
			if (c == '(' && parenCount > 0)
			{
				parenCount--;
				continue;
			}

			if (parenCount == 0)
			{
				if (c == '(')
				{
					state = OPENING_PAREN;
					continue;
				}
				if (state == OPENING_PAREN)
				{
					if (IsMethodNameBackwards(i))
						return true;
					else if (IsWSorContinuation(c))
						state = WHITESPACE;
					else
						state = DEFAULT;
					continue;
				}
				if (state == WHITESPACE)
				{
					if (IsMethodNameBackwards(i))
						return true;

					if (!IsWSorContinuation(c))
						state = DEFAULT;
					continue;
				}
			}
			if (c == '{' || c == '}' || c == ';')
				return false;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsCreateByTemplateFunction(WTString creationTemplateFunctions)
{
	int fType = GetFType();
	if (!IsCFile(fType))
		return false;

	token2 t(creationTemplateFunctions);
	while (t.more())
	{
		const WTString funcName = t.read(';');
		CommentSkipper cs(fType);
		for (int i = m_cp - 1; i >= 0; i--)
		{
			TCHAR c = m_buf[i];
			if (cs.IsCodeBackward(m_buf, mBufLen, i))
			{
				if (c != '<' && !IsWSorContinuation(c) && !ISCSYM(c))
					break;

				if (i >= m_cp - funcName.GetLength())
					continue;

				int counter = 0;
				for (int j = i; j < i + funcName.GetLength(); j++, counter++)
				{
					if (m_buf[j] != funcName[counter])
						goto nextChar;
				}

				return true;
			}
		nextChar:;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsCreateByTemplateMember(WTString creationTemplateFunctions)
{
	int fType = GetFType();
	if (!IsCFile(fType))
		return false;

	WTString symName = m_FindSymRef->GetFindSym();
	enum class eState
	{
		ANGLE_CLOSE,
		COLON1,
		COLON2,
		COMPARE,
	};
	eState state = eState::ANGLE_CLOSE;

	{
		CommentSkipper cs(fType);
		for (auto i = m_cp + symName.GetLength(); i < mBufLen; i++)
		{
			TCHAR c = m_buf[i];
			if (cs.IsCode(c))
			{
				switch (state)
				{
				case eState::ANGLE_CLOSE:
					if (IsWSorContinuation(c))
						continue;
					if (c == '>')
					{
						state = eState::COLON1;
						continue;
					}
					return false;
				case eState::COLON1:
					if (IsWSorContinuation(c))
						continue;
					if (c == ':')
					{
						state = eState::COLON2;
						continue;
					}
					return false;
				case eState::COLON2:
					if (c == ':')
					{
						state = eState::COMPARE;
						continue;
					}
					return false;
				case eState::COMPARE:
					if (IsWSorContinuation(c))
						continue;
					if (!ISCSYM(c))
						return false;
				}

				token2 t(creationTemplateFunctions);
				while (t.more())
				{
					const WTString funcName = t.read(';');
					int counter = 0;
					for (auto j = i; j < i + funcName.GetLength(); j++, counter++)
					{
						if (m_buf[j] != funcName[counter])
							goto nextFunc;
					}
					return true;
				nextFunc:;
				}

				return false;
			}
		}
	}

	return false;
}

// detect default params and creations at return.
// e.g. void funcDef(int a, ClassName obj = ClassName());
// e.g. return ClassName(1, 2, 3);
bool VAParseMPFindUsage::IsClassAndParens()
{
	int fType = GetFType();
	_ASSERTE(Is_C_CS_File(fType));
	bool initializerListItem = IsInitializerListItem();
	bool functionParam = IsFunctionParam();
	if (IsInsideGlobalOrClassScope() && !initializerListItem && !functionParam)
		return false;

	WTString symName;
	if (m_FindSymRef)
		symName = m_FindSymRef->GetFindSym();

	CommentSkipper cs(fType);
	int breakCounter = 0;
	for (int i = m_cp + symName.GetLength(); i < mBufLen - 1; i++)
	{
		if (++breakCounter >= MAX_SEARCH_LEN)
			break;
		TCHAR c = m_buf[i];
		if (cs.IsCode(c))
		{
			if (c == '(')
			{
				int parens = 1;
				int failCounter = 0;
				for (int j = i + 1; j < (int)mBufLen; j++)
				{
					if (++failCounter >= MAX_SEARCH_LEN)
						return false;
					TCHAR c2 = m_buf[j];
					if (cs.IsCode(c2))
					{
						if (c2 == '(')
						{
							parens++;
							continue;
						}
						if (c2 == ')' && parens > 0)
						{
							parens--;
							continue;
						}
						if (IsWSorContinuation(c2) || parens > 0)
							continue;
						// 						if (initializerListItem && (c2 == '{' || c2 == ','))
						// 							return false;

						return true;
					}
				}
			}
			if (c == '<')
			{
				int squareBrackets = 1;
				int failCounter = 0;
				for (int j = i + 1; j < (int)mBufLen; j++)
				{
					if (++failCounter >= MAX_SEARCH_LEN)
						return false;
					TCHAR c2 = m_buf[j];
					if (cs.IsCode(c2))
					{
						if (c2 == '<')
						{
							squareBrackets++;
							continue;
						}
						if (c2 == '>')
						{
							squareBrackets--;
							if (squareBrackets == 0)
							{
								i = j;
								goto next_i;
							}
							continue;
						}
						if (c2 == ';' || c2 == '{')
							return false;
					}
				}
			}
			if (!IsWSorContinuation(c)) // invalid character between the ClassName and the paren, eg. "ClassName<int>*("
			                            // or "ClassName a(". Latter case is handled by IsHeapOrStatic().
				return false;
		next_i:;
		}
	}

	return false;
}

bool VAParseMPFindUsage::IsInsideGlobalOrClassScope()
{
	if (m_deep == 0)
		return true;

	for (int i = int(m_deep - 1); i >= 0; i--)
	{
		ULONG defType = pState[(uint)i].m_defType;
		if (defType != UNDEF)
			return defType == CLASS || defType == STRUCT || defType == NAMESPACE;
	}

	return false;
	// return defType == CLASS || defType == STRUCT || defType == NAMESPACE/* || defType == UNDEF*/;
}

void VAParseMPFindUsage::OnDirective()
{
	if (m_symPos && &m_buf[m_cp + 1000] > m_symPos)
		m_Scoping = TRUE; // we are close.
	VAParseMPScope::OnDirective();
}

void VAParseMPFindUsage::SetNextPos(LPCSTR fromPos)
{
	LPCSTR p2 = nullptr;
	LPCSTR p1 = nullptr;

	if (m_FindSymRef && m_FindSymRef->flags & FREF_Flg_Convert_Enum)
		p1 = fromPos;
	else if (m_FindSymRef && m_FindSymRef->flags & FREF_Flg_UeFindImplicit)
	{
		// [case: 141287] skip as much as we can without whole word matching to include *_Implementation and *_Validate
		p1 = strstr(fromPos, m_FindSymRef->GetFindSym().c_str());
	}
	else
		p1 = strstrWholeWord(fromPos, m_FindSymRef->GetFindSym());

	if (m_FindSymRef->ShouldFindAutoVars())
	{
		if (IsCFile(FileType()))
			p2 = strstrWholeWord(fromPos, "auto");
		else if (CS == FileType())
			p2 = strstrWholeWord(fromPos, "var");
	}
	m_symPos = p2 && (!p1 || p2 < p1) ? p2 : p1;
}

void VAParseMPFindUsage::FindNextSymPos()
{
	if (m_symPos && CurPos() >= m_symPos && !m_inMacro)
	{
		SetNextPos(CurPos() + 1);
		if (m_symPos && &m_buf[m_cp + 1000] < m_symPos)
			m_Scoping = FALSE; // disable if it is way down there
	}
}

void VAParseMPFindUsage::DoScope()
{
	if (!mFullScope)
	{
		// for auto highlight refs, don't need to go into __super::DoScope()
		if (!(!m_inMacro && CwIS(m_FindSymRef->GetFindSym())))
		{
			if (IsXref())
			{
				return;
			}

			// if not xref we need to run scope so as to populate lwdata when
			// sym IS an xref - fixes problem with false positives on identical
			// method names in unrelated classes (see class
			// DuplicateNamesNotInterfaceImpls in AST\Interfaces.cpp)
		}
	}

	VAParseMPScope::DoScope();
}

CREATE_MLC(VAParseMPFindUsage, VAParseMPFindUsage_MLC);

void VAParseMPFindUsageFunc(const CStringW& project, const CStringW& file, MultiParsePtr mp, FindReferences* ref,
                            const WTString& buf, volatile const INT* monitorForQuit /*= NULL*/,
                            bool fullscope /*= true*/)
{
	VAParseMPFindUsage_MLC fu(GetFileType(file));
	fu->SetQuitMonitor(monitorForQuit);
	if (!fullscope)
		fu->UseQuickScope();
	fu->FindUsage(project, file, mp, ref, buf);
}

//////////////////////////////////////////////////////////////////////////
// ExpandMacro
#define MACROTEXTLEN 50000
class VAExpandMacro : public VAParseMPMacroC
{
	WTString m_typeList;
	BOOL m_expandedMacro;
	BOOL m_needSep;
	BOOL m_ignoreCharAfterMacro;

  public:
	VAExpandMacro(int fType)
	    : VAParseMPMacroC(fType)
	{
	}

	WTString ExpandMacro(MultiParsePtr mp, const WTString& code)
	{
		DB_READ_LOCK;
		m_needSep = FALSE;
		m_mp = mp;
		m_expandedMacro = FALSE;

		m_processMacros = TRUE;
		m_ignoreCharAfterMacro = FALSE;
		Init(code);
		DoParse();
		if (!m_expandedMacro) // If not expanding macro's, return original definition
			return code;
		return m_typeList;
	}

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (c == '#')
		{
			// [case=16605]
			// Expanding "MACRO(arg) xx_#ARG#", "#ARG..."\n should not be stripped.
			return;
		}
		__super::OnComment(c, altOffset);
	}

	virtual void IncCP()
	{
		// This gets called once too many times after expanding a macro Case: 9784
		// so OnEveryChar() turned "std" into to "sstd"
		// I'm not sure of a better way to fix this since ProcessMacro needs to leave the
		// last char so the IncPos() in the main _DoParse for loop  doesn't eat the next char.
		// Note: ParseLineClass.OnEveryChar is not effected since it does not expand macros.
		if (m_ignoreCharAfterMacro)
		{
			m_ignoreCharAfterMacro = FALSE;

			if (' ' == CurChar() && m_typeList.GetLength() && m_typeList[m_typeList.GetLength() - 1] != ' ')
			{
				// [case: 67222]
				// if there was a space in the original after the text we've substituted,
				// then there has to be a space at the end of m_typeList.
				OnEveryChar();
			}
		}
		else
			OnEveryChar();
		VAParseMPMacroC::IncCP();
	}
	void OnEveryChar()
	{
		char c = CurChar();
		m_typeList += c;
	}
	virtual BOOL ProcessMacro()
	{
		m_expandedMacro = TRUE;
		// If macro expands, we need to ignore closing ')'
		//  even if it is a no-op and expands to nothing, ie ASSERT(X);
		m_ignoreCharAfterMacro = __super::ProcessMacro();
		return m_ignoreCharAfterMacro;
	}
};

WTString VAParseExpandMacrosInDef(MultiParsePtr mp, const WTString& code)
{
	_ASSERTE(mp);
	VAExpandMacro exp(mp->FileType());
	return exp.ExpandMacro(mp, code);
}

class VAExpandAllMacros : public VAExpandMacro
{
  public:
	VAExpandAllMacros(int fType)
	    : VAExpandMacro(fType)
	{
	}

  protected:
	virtual DType* GetMacro(WTString sym)
	{
		DType* pMacro = m_mp->GetMacro2(sym);
		if (!pMacro)
			return nullptr;
		if (pMacro->IsDontExpand())
			return nullptr;

		return pMacro;
	}
};

// [case: 100596]
static ThreadStatic<int> sParseExpandAllMacroCnt;

WTString VAParseExpandAllMacros(MultiParsePtr mp, const WTString& code)
{
	_ASSERTE(mp);

	if (sParseExpandAllMacroCnt() > 19)
	{
		vLog("WARN: ExpandAllMacros recursion");
#ifdef VA_CPPUNIT
		OutputDebugString("WARN: VAParseExpandAllMacros runaway recursion?\r\n");
#else
		ASSERT_ONCE(!"VAParseExpandAllMacros runaway recursion");
#endif
		return WTString();
	}

	struct IncExpandMacroCnt
	{
		IncExpandMacroCnt()
		{
			sParseExpandAllMacroCnt()++;
		}
		~IncExpandMacroCnt()
		{
			sParseExpandAllMacroCnt()--;
		}
	};

	IncExpandMacroCnt ti;
	// put parser on heap so that parse state doesn't use stack
	std::unique_ptr<VAExpandAllMacros> pExp = std::make_unique<VAExpandAllMacros>(mp->FileType());
	if (pExp)
		return pExp->ExpandMacro(mp, code);
	return WTString();
}

#ifdef AVR_STUDIO

extern "C"
{
	_declspec(dllexport) LPCWSTR ExpandAllMacrosInString(LPCWSTR code)
	{
		EdCntPtr curEd = g_currentEdCnt;
		MultiParsePtr pmp = curEd ? curEd->GetParseDb() : MultiParse::Create(Src);
		_ASSERTE(pmp);

		WTString wtCode = code;
		VAExpandAllMacros exp(pmp->FileType());
		WTString expCode = exp.ExpandMacro(pmp, wtCode);

		static CStringW wExpCode;
		wExpCode = expCode.Wide();
		return wExpCode;
	}
}

#endif // AVR_STUDIO

ThreadSafeStr s_HighlightData_SymScope;
ThreadSafeStr s_HighlightData_Sym;
ThreadSafeStr s_HighlightWord;

//////////////////////////////////////////////////////////////////////////
// adds attributes to the screen
//////////////////////////////////////////////////////////////////////////

class VAParseMPUnderline : public VAParseMPScope // VAParseC
{
	BOOL mUnderlineErrors;
	BOOL mAutoHighlightRefs;
	WTString mHighlightData_SymScope;
	WTString mHighlightData_Sym;
	WTString mHighlightWord;
	ULONG m_underlineStopLine;
	enum SpellCheckState
	{
		spellActive,
		spellSkipUntilWhitespace,
		spellLooksLikeCode,
		spellLooksLikeTag
	};
	SpellCheckState mSpellCheckState;

  protected:
	VAParseMPUnderline(int fType);

	void ReparseScreen(EdCntPtr ed, const WTString& buf, MultiParsePtr mp, ULONG lineStart, ULONG lineStop,
	                   BOOL underlineErrors);

	virtual void OnError(LPCSTR errPos);
	virtual DType* GetMacro(WTString sym);
	virtual BOOL IsDone();
	virtual void OnChar();
	virtual void OnHashtag(const WTString& hashTag, bool isRef) override;
	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPUnderline";
	}
	virtual void DoScope();
	virtual int OnSymbol();
	virtual void OnCSym();

	void AddDisplayAttribute(LPCSTR pos);
	BOOL ShouldExpandEndPos(int ep);

	virtual void PostAsyncParse()
	{
		_ASSERTE(g_mainThread != ::GetCurrentThreadId());
		RunFromMainThread([this]() { PostParse(); });
	}

	void PostParse()
	{
		_ASSERTE(g_mainThread == ::GetCurrentThreadId());
		if (m_addedSymbols)
			g_ScreenAttrs.Invalidate(SA_UNDERLINE); // reparse added a new symbol, nuke underlining

		if (mUnderlineErrors)
		{
			g_ScreenAttrs.ProcessQueue_Underlines();
			if (!::IsAutoReferenceThreadRequired())
				g_ScreenAttrs.ProcessQueue_AutoReferences();
		}

		if (mUnderlineErrors || ::IsUnderlineThreadRequired())
		{
			// [case: 108516] hashtags processed even when not underlining errors
			g_ScreenAttrs.ProcessQueue_Hashtags();
		}

		if (::IsAutoReferenceThreadRequired())
			new AutoReferenceHighlighter(mEd);
	}

	EdCntPtr mEd;
};

VAParseMPUnderline::VAParseMPUnderline(int fType)
    : VAParseMPScope(fType), mSpellCheckState(spellActive)
{
	mUnderlineErrors = FALSE;
	mAutoHighlightRefs = Psettings->mAutoHighlightRefs && !Psettings->mUseAutoHighlightRefsThread;
	mConcatDefsOnAddSym = false;

	if (mAutoHighlightRefs)
	{
		if (CS == fType)
		{
			// no case / change 14281
			if (g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "HighlightReferences"))
				mAutoHighlightRefs = FALSE;
		}
		else if (IsCFile(fType))
		{
#ifndef AVR_STUDIO
			// [case: 61409]
			if (gShellAttr->IsDevenv11OrHigher() &&
			    !g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "DisableReferenceHighlighting"))
				mAutoHighlightRefs = FALSE;
#endif
		}
		else if (JS == fType)
		{
			// [case: 75130] no way to disable js auto ref highlights in vs2013 preview so defer to them.
			// Check fType, not scopeType or gTypingDevLang since js auto ref highlights do not work
			// in html/asp (in the preview)
			if (gShellAttr->IsDevenv12OrHigher())
				mAutoHighlightRefs = FALSE;
		}
		else if (VB == fType)
		{
			// [case: 61563]
			if (g_IdeSettings->GetEditorBoolOption("Basic-Specific", "EnableHighlightReferences"))
				mAutoHighlightRefs = FALSE;
		}
	}
}

void VAParseMPUnderline::ReparseScreen(EdCntPtr curEd, const WTString& buf, MultiParsePtr mp, ULONG lineStart,
                                       ULONG lineStop, BOOL underlineErrors)
{
	m_firstVisibleLine = lineStart;
	m_underlineStopLine = lineStop;
	mUnderlineErrors = underlineErrors;
	mEd = curEd;
	_ASSERTE(mEd);

	::ScopeInfoPtr si = curEd->ScopeInfoPtr();
	DTypePtr dType = si->GetCwData();
	bool inComment = si->m_commentType != 0;

	auto resetHighlightData = [&]() {
		if (Psettings->mKeepAutoHighlightOnWS == false)
		{
			s_HighlightData_SymScope.Empty();
			s_HighlightData_Sym.Empty();
		}
	};

	if (dType && !inComment)
	{
		if (mAutoHighlightRefs)
		{
			if (ISCSYM(curEd->WordLeftOfCursor()[0]) || ISCSYM(curEd->WordRightOfCursor()[0]))
			{
				dType = TraverseUsing(dType, mp.get());
				WTString ss(dType->SymScope());
				s_HighlightData_SymScope.Set(ss);
				ss = StrGetSym(ss);
				s_HighlightData_Sym.Set(ss);
				s_HighlightWord.Empty();
			}
			else
			{
				// [case: 105134] Add option to make Highlight write and read references to symbol under cursor turn off highlighting with cursor in whitespace
				resetHighlightData();
			}
		}
		else
		{
			s_HighlightWord.Empty();
		}
	}
	else
	{
		// [case: 105134] Add option to make Highlight write and read references to symbol under cursor turn off highlighting with cursor in whitespace
		resetHighlightData();

		// [case: 73003]
		if (Psettings->mSimpleWordMatchHighlights)
		{
			WTString word = curEd->CurWord();
			if (!(ISCSYM(word[0])))
			{
				word = curEd->WordLeftOfCursor();
				if (!(ISCSYM(word[0])))
				{
					word = curEd->WordRightOfCursor();
					if (!(ISCSYM(word[0])))
						word.Empty();
				}
			}

#if 0
            // notepad++ style highlight; alt+k marker seems better though
            if (Psettings->mSimpleWordMatchOnSelectionOnly)
            {
                if (!word.IsEmpty())
                {
                    const WTString sel(curEd->GetSelString());
                    if (sel.IsEmpty() || sel != word)
                        word.Empty();
                }
                
                if (word.IsEmpty())
                {
                    s_HighlightData_SymScope.Empty();
                    s_HighlightData_Sym.Empty();
                    s_HighlightWord.Empty();
                }
            }
#endif

			if (word.GetLength())
			{
				s_HighlightWord.Set(word);
				s_HighlightData_SymScope.Empty();
				s_HighlightData_Sym.Empty();
			}
		}
	}

	mHighlightData_SymScope = s_HighlightData_SymScope.Get();
	mHighlightData_Sym = s_HighlightData_Sym.Get();
	mHighlightWord = s_HighlightWord.Get();

	Parse(buf, mp);
}


void VAParseMPUnderline::AddDisplayAttribute(LPCSTR pos)
{
	if (pos < m_buf || pos > &m_buf[m_cp] || m_inMacro)
		return; // make sure string not from an expanded macro
	ScreenAttributeEnum ul_type = InComment() ? SA_UNDERLINE_SPELLING : SA_UNDERLINE;
	g_ScreenAttrs.QueueDisplayAttribute(mEd, "", ptr_sub__int(pos, m_buf), ul_type);
}

void VAParseMPUnderline::OnError(LPCSTR errPos)
{
	// Parent::OnError(); // No need..
	DEFTIMERNOTE(VAP__OnErrorUL, NULL);
	if (!mUnderlineErrors)
		return;
	if (GetLangType() == RC || GetLangType() == Idl)
		return;
	if (GetLangType() == JS)
	{
		// [case: 24947] allow underline spell check underlining in js line and block comments
		if (CommentType() != '\n' && CommentType() != '*')
			return;
	}
	if (gShellAttr->IsDevenv10OrHigher() && IsCFile(GetLangType()))
	{
		if (!CommentType() || CommentType() == '#')
		{
			if (g_IdeSettings && g_IdeSettings->AreVcSquigglesEnabled())
			{
				// Let VS2010 do the underlining
				return;
			}
		}

		if (Psettings && Psettings->mUnrealEngineCppSupport)
		{
			WTString wd(::GetCStr(errPos));
			if (wd.GetLength() > 5 && wd.Find("_API") == wd.GetLength() - 4 && ::StrIsUpperCase(wd))
			{
				wd = GetCStr(State().m_lastWordPos);
				if (wd == "class")
				{
					// [case: 114516]
					// don't underline any uppercase symbol after "class" that ends in _API
					return;
				}
			}
		}
	}

	if (!m_inMacro && m_curLine >= m_firstVisibleLine)
		AddDisplayAttribute(errPos);
}

DType* VAParseMPUnderline::GetMacro(WTString sym)
{
#ifdef AVR_STUDIO
	if (13 == sym.GetLength() && sym[0] == '_' && sym == "__attribute__")
	{
		// [case: 138801]
		// __attribute__ was added to cpp.va for case 17758 in change 12802.
		// but there are also two __attribute__ macro definitions in the VA StdAFx.h (pre-2005).
		// If the macro def is found during underline parse, then gcc __attribute__ typos
		// are not underlined since the args are treated as macro args (and get eaten).
		// The macro defs were explicitly added to the AVR stdafx.h in change 28773 for case 95299.
		return nullptr;
	}
#endif

	return __super::GetMacro(sym);
}

BOOL VAParseMPUnderline::IsDone()
{
	_ASSERTE(m_cp <= mBufLen);
	return m_curLine > m_underlineStopLine;
}

void VAParseMPUnderline::OnCSym()
{
	if (Psettings->mSimpleWordMatchHighlights)
	{
		if (mUnderlineErrors && mHighlightWord.GetLength() > 0)
		{
			if (CwIS(mHighlightWord))
				g_ScreenAttrs.QueueDisplayAttribute(mEd, mHighlightWord, ptr_sub__int(CurPos(), m_buf),
				                                    SA_REFERENCE_AUTO);
		}
	}
	__super::OnCSym();
}

void VAParseMPUnderline::OnHashtag(const WTString& hashTag, bool isRef)
{
	__super::OnHashtag(hashTag, isRef);

	g_ScreenAttrs.QueueDisplayAttribute(mEd, hashTag, ptr_sub__int(CurPos(), m_buf), SA_HASHTAG);
}

void VAParseMPUnderline::OnChar()
{
	VAParseMPScope::OnChar();

	if (!mUnderlineErrors)
		return;

	if (Psettings->mSimpleWordMatchHighlights)
	{
		if (InComment() && mHighlightWord.GetLength() > 0)
		{
			if (CwIS(mHighlightWord))
				g_ScreenAttrs.QueueDisplayAttribute(mEd, mHighlightWord, ptr_sub__int(CurPos(), m_buf),
				                                    SA_REFERENCE_AUTO);
		}
	}

	if (!Psettings->m_spellFlags)
		return;
	if (State().m_inComment == '#' && CommentType() == '"')
		return; // Don't spell in include paths

	// Spell words in comments and strings
	if ((m_startReparseLine < m_curLine) && InComment() && CommentType() != '#' && CommentType() != '{')
	{
		if (m_startReparseLine > 0 && !m_inMacro)
		{
			const char curCh = CurChar();
			if (spellSkipUntilWhitespace == mSpellCheckState)
			{
				// [case: 41543] switch state to spellActive on whitespace
				if (::wt_isspace(curCh))
					mSpellCheckState = spellActive;
				else
					return;
			}
			else if (spellLooksLikeCode == mSpellCheckState)
			{
				// [case: 40874][case: 29867] switch state to spellActive on linebreak
				// re-evaluate for code on each line
				if ('\r' == curCh || '\n' == curCh)
					mSpellCheckState = spellActive;
				else
					return;
			}
			else if (spellLooksLikeTag == mSpellCheckState)
			{
				if ('>' == curCh || '\r' == curCh || '\n' == curCh)
					mSpellCheckState = spellActive;
				return;
			}

			LPCSTR cpos = CurPos();
			const char prevCh = PrevChar();
			uint maxlen = 0;
			WTString p;

			const bool inContraction = prevCh == '\'' && wt_isalpha(curCh) && m_cp >= 2 && wt_isalpha(m_buf[m_cp - 2]);

			// Call OnCSym() for words in comments, strings and # preprocessor directives
			if (ISCSYM(curCh) && '@' != curCh && !wt_isdigit(curCh) && (!m_cp || !ISCSYM(prevCh)) && !inContraction)
			{
				// filter out urls and mixed case words
				BOOL mixedCase = FALSE;

				// Get only the next 1024 chars instead of the whole buffer...
				for (maxlen = 0; cpos[maxlen] && maxlen < 1024u; maxlen++)
					;
				p = WTString(cpos, maxlen);

				uint i;
				for (i = 0; p[i] && (ISCSYM(p[i]) || p[i] == '\''); i++)
				{
					if (i && (p[i] >= 'A' && p[i] <= 'Z' || wt_isdigit(p[i])))
						mixedCase = TRUE;
				}

				// [case: 760] read to eol to see if this is code.
				for (uint j = i; p[j] && !strchr("\r\n\"'", p[j]); j++)
				{
					if (!j)
						continue;

					// [case: 3689] removed + and added {} for better code checking
					// consider adding '=' to the string?
					if (strchr(";{}", p[j]) || (p[j] == '(' && p[j + 1] == ')') || // ()
					    (p[j] == '=' && p[j + 1] == '=') ||                        // ==
					    (p[j] == '!' && p[j + 1] == '=') ||                        // !=
					    (p[j] == ':' && p[j + 1] == ':')                           // ::
					)
					{
						mSpellCheckState = spellLooksLikeCode;
						return;
					}
					else if (p[j] == '*' && !strchr("\r\n-_�\\/", p[j + 1]))
					{
						// [case: 3689] less aggressive * check
						mSpellCheckState = spellLooksLikeCode;
						return;
					}
				}

				if (p[i] == '.' && ISCSYM(p[i + 1]))             // www.url
					mSpellCheckState = spellSkipUntilWhitespace; // url
				else if (p[i] == '/' && p[i + 1] == '/')
					mSpellCheckState = spellSkipUntilWhitespace; // url
				else if (p[i] == ':' && (p[i + 1] == '/' || p[i + 1] == '\\'))
				{
					// [case:
					// filepath or url: c:\ or http://
					// watch out for "// TODO: mispell"
					mSpellCheckState = spellSkipUntilWhitespace;
				}
				else if (!mixedCase)
				{
					if (mEd && !GetScopeInfoPtr()->IsWriteToDFile())
					{
#if !defined(SEAN)
						try
#endif // !SEAN
						{
							WTString wd(CurPos(), i);
							if (!m_mp->FindAnySym(wd))
							{
								if (!GetCp() || ((m_buf[GetCp() - 1] != '#') &&
								                 (m_buf[GetCp() - 1] != '%'))) // [case: 91351] skip hashtags
									if (!FPSSpell(wd.c_str(), nullptr) &&
									    FPSSpellSurroundingAmpersandWord(CurPos() - GetCp(), GetCp(),
									                                     GetCp() + wd.GetLength(), wd))
										OnError(CurPos());
							}
						}
#if !defined(SEAN)
						catch (...)
						{
							// had an exception spelling "���}" in non-unicode files with Japanese encoding
							// our logic above thinks the string is only 3 bytes long,
							// causing FPSSpell to exception in CString::MakeLower
							//							_asm nop; // Breakpoint
							VALOGEXCEPTION("VAP:");
						}
#endif // !SEAN
					}
				}
			}

			if (spellActive == mSpellCheckState)
			{
				if ('>' == curCh)
				{
					mSpellCheckState = spellLooksLikeCode;
				}
				else if ('<' == curCh)
				{
					// [case: 29867]
					const char nextCh = NextChar();
					if ('%' == nextCh)
					{
						mSpellCheckState = spellLooksLikeCode;
						return;
					}
					else if ('!' == nextCh)
					{
						// [case: 29867]
						// maybe this was for html <!-- ?
						return;
					}
					else if ('!' == prevCh && ('/' == m_buf[m_cp - 2] || '*' == m_buf[m_cp - 2]))
					{
						// [case: 79651] http://www.stack.nl/~dimitri/doxygen/docblocks.html
						// doxygen "//!<"
						// doxygen "/*!<"
						return;
					}
					else if (prevCh == '*')
					{
						// [case: 57415] doxygen **> http://www.stack.nl/~dimitri/doxygen/docblocks.html
						return;
					}
					else if (prevCh == '/' && '/' == m_buf[m_cp - 2])
					{
						// [case: 69662] ///< doxygen endline comment
						return;
					}

					mSpellCheckState = spellLooksLikeTag;

					if (!maxlen)
						for (; cpos[maxlen] && maxlen < 1024; maxlen++)
							;
					if (p.IsEmpty() && maxlen)
						p = WTString(cpos, maxlen);

					// xml doc tags: http://msdn2.microsoft.com/en-us/library/5ast78ax(VS.80).aspx
					// code:
					// <c> <code> <example> <exception> <include> <list> <paramref> <see> <seealso> <typeparamref>
					if (p.Find("<c>") != -1 || p.Find("code>") != -1 || p.Find("example>") != -1 ||
					    p.Find("exception>") != -1 || p.Find("include>") != -1 || p.Find("list>") != -1 ||
					    p.Find("paramref>") != -1 || p.Find("see>") != -1 || p.Find("seealso>") != -1 ||
					    p.Find("typeparamref>") != -1)
					{
						mSpellCheckState = spellLooksLikeCode;
						return;
					}

					// not code:
					// <para> <remarks> <returns> <summary> <value>
					// 				if (p.Find("para>") != -1 ||
					// 					p.Find("summary>") != -1 ||
					// 					p.Find("returns>") != -1 ||
					// 					p.Find("remarks>") != -1 ||
					// 					p.Find("value>") != -1)
					// 				{
					// 					mSpellCheckState = spellLooksLikeTag;
					// 					return;
					// 				}

					// both:
					// <param> <permission> <typeparam>
					if (p.Find("param>") != -1 || p.Find("permission>") != -1 || p.Find("typeparam>") != -1)
					{
						mSpellCheckState = spellLooksLikeCode;
						return;
					}

					return;
				}
				else if ('\\' == curCh || '@' == curCh)
				{
					// [case: 57415] doxygen
					// doxygen tags: http://www.atomineerutils.com/examplesdoxy.php
					//				 http://www.stack.nl/~dimitri/doxygen/docblocks.html
					// code:
					// \fn \var

					// javadoc tags: http://www.stack.nl/~dimitri/doxygen/docblocks.html
					//  same as doxygen, different delimiter
					// code:
					// @fn @var
					if (!maxlen)
						for (; cpos[maxlen] && maxlen < 1024; maxlen++)
							;
					if (p.IsEmpty() && maxlen)
					{
						p = WTString(cpos, maxlen);
						// only search the rest of the line
						int pos = p.find_first_of("\r\n");
						if (-1 != pos)
							p = p.Left(pos);
					}

					if (p.Find("\\fn") != -1 || p.Find("\\var") != -1 || p.Find("@fn") != -1 || p.Find("@var") != -1)
					{
						mSpellCheckState = spellLooksLikeCode;
						return;
					}
				}
			}
		}
	}
	else
		mSpellCheckState = spellActive;
}

void VAParseMPUnderline::DoScope()
{
	_ASSERTE(HasSufficientStackSpace());
	if (::IsUnderlineThreadRequired()) // not clear why scope should be run for underline thread if underlineErrors was
	                                   // false...
		VAParseMPScope::DoScope();
#ifdef _DEBUG
		// Playing with fixing Enhanced Syntax Coloring with screen attrs.
// 	if(!m_inMacro)
// 	{
// 		DType* data = State().m_lwData;
// 		if(data)
// 		{
// 			WTString sym = GetCStr(CurPos());
// 			extern int GetSymType(LPCSTR sym, MultiParse *mp);
// 			int ctype = GetSymType(sym, m_mp);
// 			if(ctype != data->Type && !(data->Type&V_CONSTRUCTOR))
// 			{
// 				// coloring most likely is off, fix with attr
// 				g_ScreenAttrs.AddDisplayAttribute(g_currentEdCnt, m_cp, 0x800000|data->Type);
// 			}
// 		}
// 	}
#endif // _DEBUG
}

int VAParseMPUnderline::OnSymbol()
{
	if (mAutoHighlightRefs && mUnderlineErrors && !m_inMacro)
	{
		// Logic copied from VAParseMPFindUsage::OnSymbol(), case=23538
		BOOL doAdd = FALSE;
		if (CurChar() == '~' || (CurChar() == '!' && ISCSYM(NextChar())))
			IncCP(); // skip ~ in destructor

		if (!(!m_inMacro && CwIS(mHighlightData_Sym)))
		{
#if 0
			if (!(CwIS("auto") || CwIS("var")))
#endif // 0
			return 0;
		}

		if (!State().m_lwData)
			return 0;

		DTypePtr dType = State().m_lwData;
		dType = TraverseUsing(dType, m_mp.get());

#if 0
		if (dType->type() == RESWORD)
		{
			if ((dType->SymScope() == ":auto" && s_HighlightData_Sym != "auto") || 
				(dType->SymScope() == ":var" && s_HighlightData_Sym != "var"))
			{
				if (!((IsCFile(GetLangType()) && dType->SymScope() == ":auto") || 
					(CS == GetLangType() && dType->SymScope() == ":var")))
					return 1;

				DType * resolvedDat = ResolveAutoVarAtCurPos(dType);
				if (resolvedDat && resolvedDat != dType && resolvedDat->Sym() == s_HighlightData_Sym)
				{
					const WTString autoVar(mEd && CS == mEd->m_ftype ? "var" : "auto");
					g_ScreenAttrs.QueueDisplayAttribute(mEd, autoVar, CurPos()-m_buf, SA_REFERENCE_AUTO);
					__super::OnSymbol();
					return 1;
				}
				else
				{
					_ASSERTE(CwIS("auto") || CwIS("var"));
					return 0;
				}
			}
		}
#endif // 0

		const WTString lwDataKey(dType->SymScope());
		static const char templateEncode = EncodeChar('<');
		if (-1 == lwDataKey.Find(templateEncode) && mHighlightData_SymScope == StripEncodedTemplates(lwDataKey))
			doAdd = TRUE;
		else if (mHighlightData_SymScope == lwDataKey)
			doAdd = TRUE;
		else if (State().m_lwData->IsConstructor() && StrGetSymScope(lwDataKey).contains(mHighlightData_SymScope))
		{
			// Con/Destructor for class
			doAdd = TRUE;
		}
		else if ((lwDataKey.contains("~") || (lwDataKey.contains("!") && !lwDataKey.contains("!="))) &&
		         StrGetSymScope(lwDataKey).contains(mHighlightData_SymScope))
		{
			// Destructor/finalizer for class
			doAdd = TRUE;
		}
		else
		{
			const int unnamedPos = lwDataKey.Find(kUnnamed);
			if (unnamedPos != -1 &&
			    mHighlightData_SymScope.Mid(0, unnamedPos) ==
			        StrGetSymScope(mHighlightData_SymScope) && // I don't understand this condition...
			    lwDataKey.Mid(0, unnamedPos) == StrGetSymScope(mHighlightData_SymScope))
			{
				// members of unnamed structs
				doAdd = TRUE;
			}
			else if (dType->MaskedType() == C_ENUMITEM)
			{
				// [case: 77550]
				// check for fully scope find enumItem vs truncated parent scoped enumItem
				WTString s1(StrGetSymScope(mHighlightData_SymScope));
				s1 = StrGetSymScope(s1);
				if (s1.GetLength())
				{
					s1 += DB_SEP_STR + StrGetSym(mHighlightData_SymScope);
					if (s1 == lwDataKey)
						doAdd = TRUE;
				}
			}
		}

		if (doAdd)
		{
			// Logic copied from VAParseMPFindUsage::AddRef(), case=23538
			ScreenAttributeEnum type = SA_REFERENCE_AUTO;
			LPCSTR p = CurPos();
			while (ISCSYM(p[0]))
				p++;
			while (wt_isspace(p[0]))
				p++;

			// case 4985
			// if needs to be modified, modify the other case 4985 marked lines as well
			while (p[0] == '[')
			{
				int counter = 0;
				while (p[0] != ']' && p[0] != 0 && p[0] != ';' && p[0] != '{' && p[0] != '}')
				{
					if (counter++ > 128)
						break;
					p++;
				}
				if (p[0] == ']')
					p++;
			}

			LPCSTR p2 = p;
			while (p2[0] == ')')
				p2++;
			while (wt_isspace(p2[0]))
				p2++;
			if ((p[0] == '=' && p[1] == '=') || (p2[0] == '=' && p2[1] == '=')) // [case: 71921] (*apple)==4
			{
				; // type = SA_REFERENCE_AUTO;
			}
			else if (p[0] == '=' ||
			         (p[0] != '>' && p[0] != '<' && p[0] != '!' && p[0] != ']' && p[1] == '=') // watch out for arr[n]=3
			         || p2[0] == '=' ||
			         (p2[0] != '>' && p2[0] != '<' && p2[0] != '!' && p2[0] != ']' &&
			          p2[1] == '=') // watch out for arr[n] =3
			         || (p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-'))
			{
				type = SA_REFERENCE_ASSIGN_AUTO;
			}
			else
			{
				ULONG bl;
				// If the whole file is in one long line, the logic below has some issues.
				// Added a MAX_LINE_LEN to prevent FindRerences from freezing up the UI thread.
#define MAX_LINE_LEN 1024
				for (bl = (ULONG)m_cp; bl && !strchr("\r\n", m_buf[bl - 1]) && (m_cp - bl) < MAX_LINE_LEN; bl--)
					;
				WTString preln(&m_buf[bl], uint(m_cp - bl));
				if (preln.GetLength() >= 2)
				{
					// check for pre-increment/decrement
					WTString tmp(preln);
					tmp.TrimRight();
					if (-1 != tmp.ReverseFind("++") || -1 != tmp.ReverseFind("--"))
					{
						// [case: 2493] rewind tmp over xrefs
						while (tmp.GetLength())
						{
							int eatChars = 0;
							int len = tmp.GetLength();
							char ch = tmp[len - 1];
							if ('.' == ch)
								eatChars = 1;
							else if (ISCSYM(ch))
								eatChars = 1;
							else if (':' == ch && len > 1 && ':' == tmp[len - 2])
								eatChars = 2;
							else if ('>' == ch && len > 1 && '-' == tmp[len - 2])
								eatChars = 2;
							else
								break;

							tmp = tmp.Left(len - eatChars);
						}

						if (tmp.ReverseFind("++") == tmp.GetLength() - 2 ||
						    tmp.ReverseFind("--") == tmp.GetLength() - 2)
							type = SA_REFERENCE_ASSIGN_AUTO;
					}
				}

				if (SA_REFERENCE_AUTO == type && State().m_lwData->MaskedType() == VAR &&
				    (p[0] == '(' || (p[0] == ' ' && p[1] == '(')))
				{
					// [case: 4154] fix for coloring of member inits
					type = SA_REFERENCE_ASSIGN_AUTO;
				}
			}

			g_ScreenAttrs.QueueDisplayAttribute(mEd, mHighlightData_Sym, ptr_sub__int(CurPos(), m_buf), type);
		}
	}

	return __super::OnSymbol();
}

BOOL VAParseMPUnderline::ShouldExpandEndPos(int ep)
{
	if (mAutoHighlightRefs)
	{
		// Don't expand macros that contain the string we are searching for in the arg list.
		// Same logic as FindReferences. case=23538
		token2 args = GetSubStr(CurPos(), m_buf + ep);
		args.read('('); // Only look at args to macro.
		if (strstrWholeWord(args.Str(), mHighlightData_Sym))
			return FALSE;
	}

	// [case: 23538]
	if (!m_inMacro)
	{
		// seems like this should have been: return __super::ShouldExpandEndPos(ep);
		return TRUE;
	}

	if (Psettings->mEnhanceMacroParsing && IsCFile(GetFType()))
	{
		// [case: 108472] [case: 23538]
		// expand macros to fix auto highlight references
		return __super::ShouldExpandEndPos(ep);
	}

	// Only expand first level macros in underline thread
	return FALSE;
}

class BoldBraceCls : public VAParse
{
	ULONG m_orgDeep;

  public:
	BoldBraceCls(VAParse* vp)
	    : VAParse(vp->FileType())
	{
		ULONG deep = vp->Depth();
		while (deep && !strchr("()[]{}<>", vp->State(deep).m_begBlockPos[-1]))
			deep--;

		EdCntPtr curEd = g_currentEdCnt;
		if (deep && curEd)
		{
			m_orgDeep = deep;
			CHAR c = vp->State(deep).m_begBlockPos[-1];

			// [case: 97964]
			// pop out of naked scopes since they don't have braces to highlight
			while (')' == c && deep > 1 && vp->State(deep).HasParserStateFlags(VPSF_NAKEDSCOPE_FOR_IF))
			{
				--deep;
				c = vp->State(deep).m_begBlockPos[-1];
			}

			if (!strchr("({[<", c))
			{
				if (Psettings->boldBraceMatch)
					g_ScreenAttrs.InvalidateBraces();
				return; // not in parens
			}
			else if (m_inIFDEFComment && c == '{')
			{
				// [case: 73884]
				return;
			}
			long brc1 = ptr_sub__int(vp->State(deep).m_begBlockPos, vp->GetBuf()) - 1;

			LoadParseState(vp, true); // use current state from scope
			DoParse();                // fast scan to closing brace;
			LPCSTR mc = strchr("()[]{}<>", CurPos()[-1]);
			if (mc && m_inIFDEFComment && strchr("{}", CurPos()[-1]))
				mc = nullptr;
			if (mc && c == mc[-1])
			{
				// Bold matching
				if (Psettings->boldBraceMatch)
				{
					g_ScreenAttrs.AddDisplayAttribute(curEd, WTString(c), brc1, SA_BRACEMATCH);
					g_ScreenAttrs.AddDisplayAttribute(curEd, WTString(mc, 1), ptr_sub__int(CurPos(), m_buf) - 1,
					                                  SA_BRACEMATCH);
				}
			}
			else
			{
				// mismatch
				if (Psettings->m_braceMismatches)
				{
					g_ScreenAttrs.AddDisplayAttribute(curEd, WTString(c), brc1, SA_MISBRACEMATCH);
					g_ScreenAttrs.AddDisplayAttribute(curEd, mc ? WTString(mc, 1) : "",
					                                  ptr_sub__int(CurPos(), m_buf) - 1, SA_MISBRACEMATCH);
				}
			}
		}
		else
		{
			// global scope, un-bold last braces
			if (Psettings->boldBraceMatch)
				g_ScreenAttrs.InvalidateBraces();
		}
	}

	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return (m_deep < m_orgDeep);
	}

	// this is a bastardized version of VAParseDirectiveC::OnDirective -
	// didn't want to change the base class of BoldBraceCls
	void OnDirective()
	{
		WTString directive = TokenGetField(&m_buf[m_cp], "# \t");
		if (strncmp(directive.c_str(), "if", 2) == 0)
		{
			// Look for #if 0
			mDirectiveBlockStack.push_back(0);
			token2 ln = TokenGetField(&m_buf[m_cp], "\r\n");
			ln.read("# \t("); // if/ifdef
			WTString op = ln.read();
			bool ignoreBlock = false;
			if (op == "0")
			{
				// skip "#if 0" block (change 5345)
				ignoreBlock = true;
			}
			else if (directive.GetLength() == 2 && directive == "if" && op == "defined")
			{
				op = ln.read();
				if (::StartsWith(op, "__OBJC__", TRUE))
				{
					// [case: 119590]
					// skip objective-C block
					// #if defined( __OBJC__ )
					ignoreBlock = true;
				}
			}
			else if (directive.GetLength() > 4 && directive[2] != 'n' && op == "__OBJC__")
			{
				// [case: 119590]
				// skip objective-C block
				// #ifdef __OBJC__
				ignoreBlock = true;
			}

			if (ignoreBlock)
			{
				m_inIFDEFComment = TRUE;
				mDirectiveBlockStack.back() = 1;
				return;
			}
		}
		else if (strncmp(directive.c_str(), "el", 2) == 0)
		{
			if (m_inIFDEFComment)
			{
				if (!mDirectiveBlockStack.empty() && mDirectiveBlockStack.back())
				{
					// [case: 73884] reset after close of "#if 0"
					m_inIFDEFComment = FALSE;
					mDirectiveBlockStack.back() = 0;
				}
			}
		}
		else if (strncmp(directive.c_str(), "end", 3) == 0)
		{
			if (strncmp(directive.c_str(), "endif", 5) == 0)
			{
				const bool curLevelIsComment = !mDirectiveBlockStack.empty() && mDirectiveBlockStack.back() > 0;
				if (!mDirectiveBlockStack.empty())
					mDirectiveBlockStack.pop_back();

				if (m_inIFDEFComment && (curLevelIsComment || mDirectiveBlockStack.empty()))
				{
					// [case: 119590]
					bool anyCommentInStack = false;
					for (auto cur : mDirectiveBlockStack)
					{
						if (cur)
						{
							anyCommentInStack = true;
							break;
						}
					}

					if (!anyCommentInStack)
					{
						// [case: 73884] reset after close of "#if 0"
						m_inIFDEFComment = FALSE;
					}
				}
				_ASSERTE(!m_inIFDEFComment || mDirectiveBlockStack.size());
			}
		}
	}
};

static bool IsBraceMatchingSupported()
{
	if (!Psettings)
		return false;

	if (!Psettings->m_ActiveSyntaxColoring && !Psettings->mUseMarkerApi)
		return false;

	return Psettings->boldBraceMatch;
}

CREATE_MLC(ReadToCls, ReadToCls_MLC);

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Gets scope and set all MParse members
#include "VAParseScopeSuggestions.h"
class MPGetScopeCls : public VAParseScopeSuggestions<VAParseMPScope>
{
  protected:
	MPGetScopeCls(int fType)
	    : VAParseScopeSuggestions(fType)
	{
	}

  public:
	WTString DoMpGetScope(const WTString bufIn, MultiParsePtr mp, int pos, bool doEverythingAndUpdateEditor = true)
	{
		if (pos >= bufIn.GetLength())
		{
			vLog("ERROR: MpGS bad offset");
			return NULLSTR;
		}

		_ASSERTE(HasSufficientStackSpace());
		LPCTSTR buf = bufIn.c_str();
		DB_READ_LOCK;
		m_updateCachePos = doEverythingAndUpdateEditor ? TRUE : FALSE;

		bool maybeRewoundTooFar = false;
		while (pos && (buf[pos - 1] == '~' || (buf[pos - 1] == '!' && ISCSYM(buf[pos])) || ISCSYM(buf[pos - 1])))
		{
			if ((buf[pos - 1] == '~' || buf[pos - 1] == '!') && ISCSYM(buf[pos]))
				maybeRewoundTooFar = true;
			pos--; // curscope stops before current word since they may be typing.
		}

		if (doEverythingAndUpdateEditor)
		{
			EdCntPtr curEd = g_currentEdCnt;
			const int topLine = curEd ? curEd->GetFirstVisibleLine() : MAXLONG;

			if (!GlobalProject->IsBusy() /*&& GetScopeInfoPtr()->m_showRed*/)
			{
				// Do underlining?
				if (curEd && GetScopeInfoPtr()->GetFileID() && !curEd->m_FileHasTimerForFullReparse)
				{
					m_startReparseLine = ULONG((topLine > 3) ? topLine - 3 : topLine);
				}
			}
		}

		memset(&m_MethArgs, 0, sizeof(m_MethArgs));
		int added = m_addedSymbols;
		ParseTo(bufIn, pos, mp);
		if (maybeRewoundTooFar && InLocalScope() && !IsXref())
			ParseTo(bufIn, ++pos, mp);
		::ScopeInfoPtr pScopeInfo = GetScopeInfoPtr(); // m_mp should be set to mp after ParseTo()
		pScopeInfo->InitScopeInfo();

		if (doEverythingAndUpdateEditor)
		{
			if (m_cp < pos)
			{
				// Had Event, invalidate mp and bail
				pScopeInfo->m_isDef = TRUE; // Prevent bogus expansion or case correct
				pScopeInfo->m_scopeType = DEFINE;
				pScopeInfo->m_lastScope = DB_SEP_STR;
				pScopeInfo->ClearCwData();
				return NULLSTR;
			}

			if (added != m_addedSymbols)
				g_ScreenAttrs.Invalidate(SA_UNDERLINE); // clear underlining for newly defined syms, No deed to repaint

			if (::IsBraceMatchingSupported())
			{
				BoldBraceCls ptc(this);
			}
		}

		//////////////////////////////////////////////////////////////////////
		//	Fill MultiParse members as needed
		pScopeInfo->m_baseClass = GetBaseScope();
		pScopeInfo->m_baseClassList = mp->GetBaseClassList(pScopeInfo->m_baseClass, false, 0, GetLangType());

		// isDef is important to prevent suggestions while typing def
		pScopeInfo->m_isDef = false;
		// Since we moved only scope to beginning of word,
		// if VPS_BEGLINE, probably a def,
		// if State().m_begLinePos != &buf[pos], this is the first word of the line, ok to to case correct
		if (State().m_parseState == VPS_BEGLINE &&
		    (State(m_deep).m_defType == C_ENUMITEM || State().m_begLinePos != &buf[pos]))
		{
			// but, if C_ENUMTYPE, "enum { foo," it is a def, don't case correct here
			pScopeInfo->m_isDef = true;
		}
		else if (State().m_parseState == VPS_NONE && m_deep && State(m_deep - 1).m_defType == C_ENUM)
		{
			// [case: 29938]
			// isdef not properly set for first enum item
			pScopeInfo->m_isDef = true;
		}
		else if (State().m_parseState == VPS_ASSIGNMENT && m_deep && VAR == State().m_defType && CS == GetLangType() &&
		         '(' == State(m_deep - 1).m_lastChar && StartsWith(State().m_lastScopePos, "out"))
		{
			// [case: 116073]
			// first out param inside func call needs special-casing similar to first enum item
			pScopeInfo->m_isDef = true;
		}
		else if (State().m_parseState == VPS_NONE && StartsWith(CurPos(), "operator"))
		{
			// operator char();
			pScopeInfo->m_isDef = TRUE;
		}

		// Fix isDef for special cases
		if (IsXref()                                                                                // const foo::bar, bar cannot be a definition
		    || StartsWith(State().m_begLinePos, "case") || StartsWith(State().m_begLinePos, "goto") // [case: 1909]
		    || StartsWith(State().m_lastWordPos, "static") ||
		    StartsWith(State().m_lastWordPos, "thread_local") // [case: 86387]
		    || StartsWith(State().m_lastWordPos, "extern") || StartsWith(State().m_lastWordPos, "public") ||
		    StartsWith(State().m_lastWordPos, "private") || StartsWith(State().m_lastWordPos, "protected")
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		    || StartsWith(State().m_lastWordPos, "__published")
#endif
		    || StartsWith(State().m_lastWordPos, "internal") || StartsWith(State().m_lastWordPos, "virtual") ||
		    StartsWith(State().m_lastWordPos, "new") || StartsWith(State().m_lastWordPos, "End") // VB "End Function"
		)
			pScopeInfo->m_isDef = FALSE;

		pScopeInfo->m_inParenCount = 0;
		for (ULONG deep = 0; deep <= m_deep; deep++)
			if (InParen(deep))
				pScopeInfo->m_inParenCount++;
		pScopeInfo->m_inClassImplementation =
		    m_deep && (State(m_deep - 1).m_defType == CLASS || State(m_deep - 1).m_defType == STRUCT ||
		               State(m_deep - 1).m_defType == C_INTERFACE);
		if (!pScopeInfo->m_isDef && m_deep && !InComment()) // Don't set isDEf in comments. case=24320
		{
			// Test for constructor/destructor
			if (State().m_parseState == VPS_NONE)
			{
				if (CurChar() == '~' || (CurChar() == '!' && !InLocalScope() && pScopeInfo->m_inClassImplementation) ||
				    (!InLocalScope() && CwIS(GetCStr(State(m_deep - 1).m_lastScopePos))))
				{
					pScopeInfo->m_isDef = true;
				}
			}
			else if (pScopeInfo->m_inClassImplementation && State().m_parseState == VPS_BEGLINE &&
			         (CurChar() == '~' || CurChar() == '!') && StartsWith(State().m_lastWordPos, "virtual"))
				pScopeInfo->m_isDef = true; // case=39777 virtual destructor
		}
		if (!pScopeInfo->m_isDef &&
		    (
		        // TOPIC_ID=4735 "template<typename C", IsDef should be set
		        StartsWith(State().m_lastWordPos, "class") || StartsWith(State().m_lastWordPos, "struct") ||
		        StartsWith(State().m_lastWordPos, "interface") || StartsWith(State().m_lastWordPos, "__interface") ||
		        StartsWith(State().m_lastWordPos, "_asm") || // Case=28803 if(1) _asm nop; // set isdef
		        StartsWith(State().m_lastWordPos, "typename")))
			pScopeInfo->m_isDef = TRUE;
		if (pScopeInfo->m_isDef && StartsWith(State().m_lastWordPos, "typedef"))
			pScopeInfo->m_isDef = FALSE; // TOPIC_ID=5340

		pScopeInfo->m_xref = IsXref();
		if (pScopeInfo->m_xref)
		{
			pScopeInfo->m_xrefScope = GetDataKeyStr(m_deep);
			if (!pScopeInfo->m_xrefScope.GetLength())
			{
				pScopeInfo->m_xrefScope = DB_SEP_STR;

				// [case: 24121] fix for C# named object initializers might be around here (C# does not use '.' in
				// object initialization)
				if (IsCFile(GetLangType()) && m_mp && m_deep && State().m_lastChar == '.')
				{
					if (State(m_deep - 1).m_lwData && State(m_deep - 1).m_lastChar == '{' &&
					    (State(m_deep - 1).m_parseState == VPS_ASSIGNMENT ||
					     (VAR == State(m_deep - 1).m_defType && VPS_BEGLINE == State(m_deep - 1).m_parseState)))
					{
						// [case: 118748] C99/C++20 designated initializer support
						pScopeInfo->m_xrefScope = State(m_deep - 1).m_lwData->SymScope();
					}
					else if (m_deep > 1 && !State(m_deep - 1).m_lwData && State(m_deep - 1).m_lastChar == '{' &&
					         State(m_deep - 2).m_lwData && State(m_deep - 2).m_lastChar == '{' &&
					         (State(m_deep - 2).m_parseState == VPS_ASSIGNMENT ||
					          (VAR == State(m_deep - 2).m_defType && VPS_BEGLINE == State(m_deep - 2).m_parseState)))
					{
						// [case: 141667] C99/C++20 designated initializer support in array
						pScopeInfo->m_xrefScope = State(m_deep - 2).m_lwData->SymScope();
					}
				}
			}
		}
		else
			pScopeInfo->m_xrefScope.Empty();

		// 		pScopeInfo->m_inClass = pScopeInfo->m_baseClass.GetLength()>1;
		pScopeInfo->m_argScope = m_deep ? GetDataKeyStr(m_deep - 1) : NULLSTR;
		pScopeInfo->m_lastScope = m_deep ? (Scope(m_deep) + DB_SEP_CHR) : DB_SEP_CHR;
		pScopeInfo->m_isMethodDefinition = !InLocalScope();
		pScopeInfo->m_line = (int)m_curLine;
		pScopeInfo->m_firstWord = pScopeInfo->m_isDef ? GetCStr(State().m_begLinePos) : NULLSTR;
		pScopeInfo->m_inParamList = InParen(m_deep) && !InLocalScope(m_deep - 1);
		pScopeInfo->m_argCount = State().m_argCount;
		pScopeInfo->m_argParenOffset = ptr_sub__int(State().m_begBlockPos, buf) - 1;
		pScopeInfo->m_argTemplate.Empty();
		//		pScopeInfo->m_type = 0;
		// pScopeInfo->m_lastLn = State().m_begLinePos - buf;

		// m_offset, m_paramInfoType, pScopeInfo->m_curFunctionScope, m_rscope,
		//??? m_pGotobuf, m_lastBrc, m_lastBrcScope?
		// NOT pScopeInfo->m_isSysFile, m_attrtype

		// Parameter Tooltip stuff
		ULONG t = ~0UL;
		if (m_deep)
			t = State(m_deep - 1).m_defType;

		if (doEverythingAndUpdateEditor && InParen(m_deep) 
			|| (InCurlyBrace(m_deep) && (t == VAR ||  t == UNDEF))
#if defined(case2321)
		    || (m_deep && InSquareBrace(m_deep) && InParen(m_deep - 1))
#endif // case2321
		)
			GetParamInfo(!InParen(m_deep));

		pScopeInfo->m_commentType = State().m_inComment;
		pScopeInfo->m_commentSubType = State().m_inSubComment;

		// Make scope same as mparse in strings and comments
		if (InComment())
		{
			pScopeInfo->m_scopeType = COMMENT;
			pScopeInfo->m_lastScope = "String";
			if (CommentType() == '#' && m_inDirectiveType == Directive::Error)
			{
				// #error treat as comment
				// keep COMMENT/String from above
			}
			else if (CommentType() == '#' ||
			         (CommentType() == '"' && State().m_inComment == '#' && Directive::Include == m_inDirectiveType))
			{
				//				pScopeInfo->m_type = DEFINE;
				pScopeInfo->m_scopeType = DEFINE;
				pScopeInfo->m_lastScope = DB_SCOPE_PREPROC;
			}
			else if (CommentType() == '\'' || CommentType() == '"' || CommentType() == '{')
			{
				pScopeInfo->m_scopeType = STRING;
				pScopeInfo->m_lastScope = "String";
			}
		}

		if (!doEverythingAndUpdateEditor)
			return NULLSTR;

		GetCurWordScope(doEverythingAndUpdateEditor);

		{
			// Update ScopeInfo
			EdCntPtr curEd = g_currentEdCnt;
			if ((CommentType() == '\'' || CommentType() == '"' || CommentType() == '{') && !(IsCFile(GetLangType())))
			{
				// Save StringText
				int bp = m_cp, ep = m_cp;
				for (; bp && m_buf[bp - 1] != CommentType(); bp--)
					;
				for (; ep && m_buf[ep] && m_buf[ep] != CommentType(); ep++)
					;
				WTString stringText = WTString(m_buf + bp, ep - bp);

				if (Is_Tag_Based(GetLangType()))
				{
					// Cleanup HTML paths
					if (stringText.Find("~/") == 0)
						stringText = stringText.Mid(2);
					if (stringText[0] == '/')
						stringText = stringText.Mid(1);
					stringText.ReplaceAll("%20", " ");
					stringText.ReplaceAll("&amp;", "&");
					TokenGetField2InPlace(stringText, '?'); // strip off args "urlPath.asp?args"
					TokenGetField2InPlace(stringText, '#'); // strip off anchor "urlPath.asp#anchor"
				}
				pScopeInfo->m_stringText = stringText;

				// Display "HTTP:..." in context/def minihelp
				if (StartsWith(stringText, "http:"))
				{
					pScopeInfo->SetMiniHelpInfo("URL", stringText.c_str(), GetFileTypeByExtension(stringText.Wide()));
				}

				// Display "file.ext" in context/def minihelp
				if (curEd &&
				    ((GetFileTypeByExtension(stringText.Wide()) != Other) || (GetSuggestMode() & SUGGEST_FILE_PATH)))
				{
					// See if this is file
					CStringW fileBase(stringText.Wide());
					CStringW path = Path(curEd->FileName());
					for (;;)
					{
						CStringW file = path + '/' + fileBase;
						if (IsFile(file))
						{
							// [case: 798] see if we have a mixed case version already in the fileId db
							UINT fid = gFileIdManager->GetFileId(file);
							if (fid)
								file = gFileIdManager->GetFile(fid);
							pScopeInfo->SetMiniHelpInfo(WTString(Basename(file)).c_str(),
							                            WTString(MSPath(file)).c_str(), GetFileTypeByExtension(file));
							break;
						}
						CStringW parentPath = Path(path);
						if (path == parentPath)
							break;
						path = parentPath;
					};
				}
			}
			pScopeInfo->m_LastWord = GetCStr(State().m_lastWordPos);
			if (!pScopeInfo->m_LastWord.GetLength() && InParen(m_deep) && m_deep)
				pScopeInfo->m_LastWord = GetCStr(State(m_deep - 1).m_lastWordPos); // Get MethName()

			pScopeInfo->m_suggestionType = GetSuggestMode();
			if (curEd)
				curEd->m_ScopeLangType = GetLangType();
			GetScopeInfo(pScopeInfo.get());
		}
		// Snippet info
		{
			// [case: 2050]
			pScopeInfo->m_ClassName = StrGetSym(pScopeInfo->m_baseClass);
			int innerMostNs = -1;
			int innerMostCls = -1;
			// Walk Scope looking for Method/Class/Namespace
			for (int i = (int)m_deep; i >= 0; i--)
			{
				const ULONG stateType = State((ulong)i).m_defType;
				if (stateType == FUNC)
				{
					if (!pScopeInfo->m_MethodName.GetLength())
					{
						WTString tmp = GetCStr(State((ulong)i).m_lastWordPos);
						if (tmp.GetLength() && pScopeInfo->m_lastScope.Find(":" + tmp) == -1 &&
						    pScopeInfo->m_lastScope.Find('-') != -1)
						{
							// watch out for "void foo() const { }" and "void foo() MACRO {}"
							tmp = pScopeInfo->m_lastScope;
							if (tmp.GetLength())
								tmp = StrGetSym_sv(::TokenGetField2(tmp, '-'));
						}
						pScopeInfo->m_MethodName = tmp;
					}
				}
				else if (stateType == CLASS || stateType == STRUCT || stateType == C_INTERFACE)
				{
					if (-1 == innerMostCls)
						innerMostCls = i;
				}
				else if (stateType == NAMESPACE)
				{
					if (-1 == innerMostNs)
						innerMostNs = i;
				}

				if (i && State(ulong(i - 1)).m_defType == NAMESPACE && !pScopeInfo->m_NamespaceName.GetLength())
				{
					// List full namespace foo.bar.baz/foo::bar::baz case=30005
					pScopeInfo->m_NamespaceName = CleanScopeForDisplay(Scope(ulong(i)));
					if (IsCFile(GetLangType())) // Use ::'s in C++ "foo::bar"
						pScopeInfo->m_NamespaceName.ReplaceAll(".", "::");
				}
			}

			if (!pScopeInfo->m_NamespaceName.GetLength() && pScopeInfo->m_baseClass.GetLength() &&
			    pScopeInfo->m_baseClass != DB_SEP_STR &&
			    pScopeInfo->m_baseClass != (DB_SEP_STR + pScopeInfo->m_ClassName))
			{
				WTString tmp = StrGetSymScope(pScopeInfo->m_baseClass);
				if (tmp.GetLength() && m_mp)
				{
					DType* pDat = m_mp->FindExact2(tmp);
					if (pDat && pDat->MaskedType() == NAMESPACE)
					{
						// List full namespace foo.bar.baz/foo::bar::baz case=30005
						pScopeInfo->m_NamespaceName = CleanScopeForDisplay(tmp);
						if (IsCFile(GetLangType())) // Use ::'s in C++ "foo::bar"
							pScopeInfo->m_NamespaceName.ReplaceAll(".", "::");
					}
				}
			}

			if (!pScopeInfo->m_ClassName.IsEmpty() && !pScopeInfo->m_NamespaceName.IsEmpty())
			{
				// [case: 2050] [case: 30005]
				if (pScopeInfo->m_ClassName == pScopeInfo->m_NamespaceName)
					pScopeInfo->m_ClassName.Empty();
				else if (pScopeInfo->m_ClassName == StrGetSym(pScopeInfo->m_NamespaceName))
				{
					const WTString savClsName(pScopeInfo->m_ClassName);
					pScopeInfo->m_ClassName.Empty();
					if (-1 != innerMostCls && -1 != innerMostNs && innerMostNs + 1 == innerMostCls)
					{
						// [case: 71875]
						const WTString nsTmp(StrGetSymScope(pScopeInfo->m_lastScope));
						if (savClsName == StrGetSym(nsTmp))
							pScopeInfo->m_ClassName = savClsName;
					}
				}
			}

			// Save MethodArgs for current method
			for (int arg = 0; m_MethArgs[arg]; arg++)
			{
				if (arg)
					pScopeInfo->m_MethodArgs += ", ";
				pScopeInfo->m_MethodArgs += GetCStr(m_MethArgs[arg]);
			}
			// Use BCL of current class to get first BaseClassName
			token2 bcl(pScopeInfo->m_baseClassList);
			bcl.read('\f'); // strip this class
			pScopeInfo->m_BaseClassName = StrGetSym(bcl.read('\f'));
		}
		return pScopeInfo->m_lastScope;
	}

	void GetOverloadedMethods(MultiParsePtr mp, DType* method, std::vector<WTString>& methods)
	{
		if (mp)
		{
			std::ostringstream ostr;
			WTString symName = method->Sym();
			std::vector<DType*> v;
			WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
			DBQuery query(mp);
			WTString scope = method->Scope();
			query.FindAllSymbolsInScopeAndFileList(scope.c_str(), bcl.c_str(), method->FileId());

			for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
			{
				if (dt->type() != FUNC)
					continue;

				if (dt->Sym() != symName)
					continue;

				methods.emplace_back(dt->Def());
			}
		}
	}

	void GetParamInfo(bool inCurlyBrace)
	{
		::ScopeInfoPtr pScopeInfo = GetScopeInfoPtr();
		pScopeInfo->m_argTemplate.Empty();
		pScopeInfo->m_argCount = State().m_argCount;
		DTypePtr data = m_deep ? GetCWData(m_deep - 1) : NULL;
#if defined(case2321)
		// in progress fix for case 2321 - putting on hold since it is low priority
		// currently is overzealous and produces paraminfo for
		// arrays as in p[i] or foo(p[i])
		if (!data && m_deep > 1 && InParen(m_deep - 1))
		{
			// might be in a cast - check deeper
			data = GetCWData(m_deep - 2);
			if (data)
			{
				const uint t = data->MaskedType();
				if (t == DEFINE || t == FUNC || t == TYPE)
				{
					pScopeInfo->m_argScope = GetDataKeyStr(m_deep - 2);
					pScopeInfo->m_argParenOffset = ptr_sub__int(State(m_deep - 1).m_begBlockPos, m_buf) - 1;
					pScopeInfo->m_argCount = State(m_deep - 1).m_argCount;
				}
				else
					data.reset();
			}
		}
#endif // case2321
		if (!data)
			return;

		if (data->MaskedType() == DEFINE)
		{
			// #define MYPROC myProc
			// MYPROC( // list args of MyProc
			if (!data->Def().contains("("))
			{
				token2 types = GetTypesFromDef(data.get(), DEFINE, GetLangType());
				while (types.more() > 1)
				{
					WTString sym = StrGetSym(types.read('\f'));
					if (sym.GetLength())
					{
						DType* apidata = m_mp->FindSym(&sym, &pScopeInfo->m_lastScope, &pScopeInfo->m_baseClassList);
						if (apidata)
						{
							const WTString def(apidata->Def());
							if (-1 == def.Find("HIDETHIS"))
								pScopeInfo->m_argTemplate += def + '\f';
						}
					}
				}
			}

			// Do we still need this with code above?
			//			// see if win api
			//			WTString api = data->Key() + (Psettings->m_AsciiW32API ? "A" : "W");
			//			DType *apidata = m_mp->FindExact2(api);
			//			if(apidata)
			//				data = apidata;
		}
		if(inCurlyBrace && data->MaskedType()!=CLASS && data->MaskedType()!=VAR) {
			//Only implicit constructor calls are allowed with uniform initialization
			return;
		}
		if (data->MaskedType() != FUNC && data->MaskedType() != DEFINE)
		{
			// Support for constructors
			// string s(); // look UP bcl for s to get ":string", then look for ":string:string"
			token2 bcl = data->SymScope();
			if (!data->IsType())
				bcl = m_mp->GetBaseClassList(data->SymScope(), false, 0, GetLangType()); // Use bcl to get vars type;
			// else data is already a type ie CString(
			WTString sep = EncodeScope("\f <>()"); // strip off template args, they are encoded
			WTString bClass = bcl.read(sep);
			bClass = DecodeScope(bClass);
			WTString sym = StrGetSym(bClass);
			DType* constData = m_mp->FindSym(&sym, NULL, &bClass, FDF_CONSTRUCTOR);
			if (constData)
				data = std::make_shared<DType>(constData);
		}

#ifdef RAD_STUDIO
		// #RAD_ParamCompletion
		if (gVaRadStudioPlugin && gVaRadStudioPlugin->IsInParamCompletion())
		{
			extern VaRSParamCompetionOverloads sRSParamComplOverloads;
			sRSParamComplOverloads.Clear();

			GetOverloadedMethods(m_mp, data.get(), sRSParamComplOverloads.defs);
			auto langType = GetLangType();
			for (size_t i = 0; i < sRSParamComplOverloads.defs.size(); i++)
			{
				sRSParamComplOverloads.defs[i] =
				    CleanDefForDisplay(sRSParamComplOverloads.defs[i], langType);
			}
		}
#endif
		if (pScopeInfo->m_argTemplate.IsEmpty())
		{
			const WTString def(data->Def());
			if (-1 == def.Find("HIDETHIS"))
				pScopeInfo->m_argTemplate = def;
		}

		if (pScopeInfo->m_argTemplate.GetLength() >
		    5000) // found a symbol defined many times in a project that caused reall slowdowns.
			pScopeInfo->m_argTemplate = pScopeInfo->m_argTemplate.Left(5000);

		pScopeInfo->m_argTemplate = CleanDefForDisplay(pScopeInfo->m_argTemplate, GetLangType());
	}

	virtual void GetCurWordScope(bool updateEd)
	{
		// do EdCnt::CurScopeWord(); since we are already here...
		::ScopeInfoPtr pScopeInfo = GetScopeInfoPtr(); // m_mp should be set to mp after ParseTo()

		DTypePtr data;
		if (!InComment() || (CommentType() == '#' && m_inDirectiveType != Directive::Include))
		{
			// Don't get the scope of symbols in comments
			DoScope();
			data = State().m_lwData;

			// [no case; p4 change 6768]
			// Catch properties or vars used as types
			// there could be a var with the same name as a type messing things up
			// UC: var() Weapon Weapon;
			// C# property string string {...}
			if (data && (data->MaskedType() == VAR || data->MaskedType() == PROPERTY))
			{
				// Make sure not using the var as a type
				LPCSTR p = CurPos();
				for (; ISCSYM(*p); p++)
					; // cwd
				for (; *p && wt_isspace(*p); p++)
					; // whitespace
				const WTString nextSym = GetCStr(p);
				if (ISCSYM(*p) && !m_mp->GetMacro(nextSym))
				{
					// Being used as a type, get the actual class
					WTString cls = GetCStr(CurPos());
					WTString bcl = m_mp->GetBaseClassList(GetBaseScope(), false, 0, GetLangType());
					WTString colonStr(DB_SEP_STR);
					DType* clsData = m_mp->FindSym(&cls, &colonStr, &bcl, FDF_TYPE); // getting rid of Tdef
					if (!clsData && pScopeInfo->m_isMethodDefinition && pScopeInfo->m_inClassImplementation)
					{
						// [case: 73412]
						// try again using scope of the property instead of global scope
						WTString scpStr(data->Scope());
						clsData = m_mp->FindSym(&cls, &scpStr, &bcl, FDF_TYPE | FDF_NoConcat);
					}

					if (clsData)
						data = std::make_shared<DType>(clsData);
				}
			}
			else if (data && data->type() == RESWORD &&
			         ((IsCFile(GetLangType()) && data->SymScope() == ":auto") ||
			          (CS == GetLangType() && data->SymScope() == ":var")))
			{
				data = ResolveAutoVarAtCurPos(data);
			}

			// Set isdef while working in #defines, or typing "#define a<tab> b" will expand "a..."
			// 		really tough typing if expand on any char is checked
			if (CommentType() == '#')
				pScopeInfo->m_isDef = TRUE;
		}
		else if (GetLangType() == JS || Is_Tag_Based(GetLangType()))
		{
			DoScope();
			data = State().m_lwData;
		}
		pScopeInfo->SetCwData(data);

		if (!m_deep)
			pScopeInfo->m_ParentScopeStr.Empty();
		else if (Is_VB_VBS_File(GetLangType()))
			pScopeInfo->m_ParentScopeStr = TokenGetField(State(m_deep - 1).m_begLinePos, "\r\n", VB);
		else
		{
			ReadToCls rtc(GetLangType());
			LPCTSTR readPos = State(m_deep - 1).m_begLinePos;
			const int readLen = ::GetSafeReadLen(readPos, GetBuf(), GetBufLen());
			pScopeInfo->m_ParentScopeStr = rtc.ReadTo(readPos, readLen, "{;}");
		}

		if (updateEd)
		{
#if defined(VA_CPPUNIT)
			g_currentEdCnt_SymDef = data ? data->Def() : NULLSTR;
			g_currentEdCnt_SymScope = data ? data->SymScope() : NULLSTR;
			if (data)
				g_currentEdCnt_SymType = data;
			else
				g_currentEdCnt_SymType = nullptr;
#else

			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
				curEd->UpdateSym(data.get());
#endif // !VA_CPPUNIT
		}
	}

	virtual LPCSTR GetParseClassName()
	{
		return "MPGetScopeCls";
	}
#define MAX_ARGS 20
	LPCSTR m_MethArgs[MAX_ARGS + 1];
	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		// [case: 2050]
		if (CurChar() == '}' && (!m_deep || !InLocalScope(m_deep - 1)))
			m_MethArgs[0] = NULL;
		__super::DecDeep();
	}
	virtual void OnDef()
	{
		if (!m_inMacro)
		{
			if (InParen(m_deep) && State(m_deep - 1).m_defType == FUNC && State().m_argCount < MAX_ARGS)
			{
				m_MethArgs[State().m_argCount] = State().m_lastScopePos;
				m_MethArgs[State().m_argCount + 1] = NULL;
			}
			else if (CurChar() == ';' && !InParen(m_deep) && State(m_deep).m_defType == FUNC)
			{
				// [case: 23124] clear args after func declaration
				m_MethArgs[0] = NULL;
			}
		}
	}
};
// CREATE_MLC(MPGetScopeCls , MPGetScopeCls_MLC);

//////////////////////////////////////////////////////////////////////////
// Methods In File Class

static WTString PV_ReadCSym(LPCSTR p)
{
	for (; *p && *p != '\n' && *p != '\r' && !ISCSYM(*p); p++)
		;
	int i;
	for (i = 0; p[i] && ISCSYM(p[i]); i++)
		;
	return WTString(p, i);
}

class MethodsInFile : public VAParseMPMacroC
{
  protected:
	LineMarkers* mMarkers;
	DWORD mStartTime;
	BOOL mIsDone;
	WTString mLastSym;
	BOOL mResultStatus;
	BOOL mUseReadAhead = FALSE;
	CommentSkipper CommentSkipper{GetFType()};

	enum eUnexpectedMethodState
	{
		UMS_COMBINATION, // whitespaces / symbolname / :: / skipping content inside square brackets
		UMS_FINDINGMATCHINGPAREN,
		UMS_WS3,
	};

	eUnexpectedMethodState UMS = UMS_COMBINATION;
	int Parens = 0;
	int PatternIterator = 0;
	bool PatternAhead = false;
	WTString SymbolName;
	WTString PrevSymbolName;

	bool InFuncScope(int* level = nullptr)
	{
		for (int i = int(m_deep - 1); i >= 0; i--)
		{
			ULONG defType = pState[(uint)i].m_defType;
			if (defType != UNDEF)
			{
				if (level)
					*level = i;
				return defType == FUNC;
			}
		}

		return false;
	}

	// <whitespaces/symbolname/::/skipping content inside square brackets>(<find_mathcing_paren><whitespaces>{
	// do not call when current position is inside a string literal or comment
	bool IsPatternAheadInsideFunc_Cached()
	{
		_ASSERTE(!InComment()); // [case: 118372]
		if (m_cp >= PatternIterator)
		{
			PatternIterator = m_cp;
			for (char c; (c = m_buf[PatternIterator]) != 0 && PatternIterator < mBufLen; PatternIterator++)
			{
				auto FindEndOfTemplateArguments = [&]() {
					if (c != '<')
						return false;

					int exitCounter = 0;
					int parenCounter = 1;
					int origPatternIterator = PatternIterator;
					while (parenCounter > 0 && c != 0 && ++exitCounter < 128)
					{
						PatternIterator++;
						c = m_buf[PatternIterator];
						if (c == '(' || c == ')')
							break;
						else if (c == '<')
							parenCounter++;
						else if (c == '>')
						{
							parenCounter--;
							if (parenCounter == 0)
							{
								return true;
							}
						}
					}

					PatternIterator = origPatternIterator;
					return false;
				};

				if (!CommentSkipper.IsCode(c) || c == '/')
					continue;

				switch (UMS)
				{
				case UMS_COMBINATION:
					if (ISCSYM(c)) // sym
					{
						if (PatternIterator > 0 && !ISCSYM(m_buf[PatternIterator - 1]))
						{
							PrevSymbolName = SymbolName;
							SymbolName = "";
						}

						SymbolName += c;
						continue;
					}
					if (IsWSorContinuation(c)) // ws
						continue;
					if (c == ':' && PatternIterator > 0 && m_buf[PatternIterator - 1] == ':') // ::
						continue;
					if (c == ':' && PatternIterator < mBufLen - 1 && m_buf[PatternIterator + 1] == ':') // ::
						continue;
					if (c == '<') // skipping <>s
					{
						if (FindEndOfTemplateArguments())
							SymbolName = ">";
						break;
					}
					if (c == '(')
					{
						if (SymbolName == "try" || SymbolName == "catch" || SymbolName == "if" ||
						    SymbolName == "while" || SymbolName == "for" || SymbolName == "switch" ||
						    SymbolName == "each" || SymbolName == "" || PrevSymbolName == "")
							continue;
						UMS = UMS_FINDINGMATCHINGPAREN;
						Parens = 1;
						break;
					}
					PatternAhead = false;
					PatternIterator++;
					PrevSymbolName = "";
					SymbolName = "";
					return false;
				case UMS_FINDINGMATCHINGPAREN:
					if (c == '(')
					{
						Parens++;
					}
					else if (c == ')')
					{
						Parens--;
						if (Parens == 0)
							UMS = UMS_WS3;
					}
					break;
				case UMS_WS3:
					if (!IsWSorContinuation(c))
					{
						UMS = UMS_COMBINATION;
						if (c == '{')
						{
							// unexpected method?
							if (InFuncScope())
								PatternAhead = true;
							else
								PatternAhead = false;
							return PatternAhead;
						}
					}
				}
			}
		}

		return PatternAhead;
	}

	// 1 char delay
	virtual void IncCP() override
	{
		// OnEveryChar
		if (mUseReadAhead && !InComment() && // [case: 118372]
		    IsPatternAheadInsideFunc_Cached())
		{
			// unexpected function (case case 4222)
			int level;
			if (InFuncScope(&level))
			{
				if (level >= 0)
				{
					ULONG origDeep = m_deep;
					while ((int)m_deep != level)
					{ // "i" is int so the "for" would exit immediately if m_deep was 0 for some reason
						DecDeep();
						if (origDeep == m_deep)
						{ // DecDeep() can refuse to decrease m_deep, so we need to avoid a possible infinite loop
							m_deep = (ULONG)level;
							break;
						}
					}

					ClearLineState(CurPos());
					PatternAhead = false;
				}
			}
		}

		__super::IncCP();
	}

  public:
	UINT m_lastLine;
	virtual LPCSTR GetParseClassName()
	{
		return "MethodsInFile";
	}

	BOOL GetMethods(const WTString& buf, MultiParsePtr mp, bool useReadAhead, LineMarkers& markers)
	{
		mMarkers = &markers;
		// [case: 4222]
		mUseReadAhead = useReadAhead && mp ? IsCFile(mp->FileType()) : false;
		// mUseReadAhead = false; // [case: 97700]
		m_lastLine = 0xfffffff;
		mLastSym.Empty();
		m_mp = mp;
		PatternIterator = 0;
		CommentSkipper.Reset();
		Init(buf);
		m_parseFlags = PF_TEMPLATECHECK;
		DoParse();
		mMarkers = NULL;
		return mResultStatus;
	}

	virtual void OnNextLine()
	{
#if 0
		__super::OnNextLine();
#else
		if (m_curLine++ % 16)
		{
			// [case: 20327] bailout after 10 seconds
			if ((::GetTickCount() - mStartTime) > (10 * 1024))
			{
#ifdef _DEBUG
				if (::IsDebuggerPresent())
					return;
#endif // _DEBUG
				mIsDone = TRUE;
				// [case: 87527] notify of time out
				mResultStatus = FALSE;
				vLog("Warn: MIF::ONL timeout");
			}
		}
#endif
	}

	virtual BOOL ProcessMacro()
	{
		// [case: 69244]
		return ConditionallyProcessMacro();
	}

	// [case: 808] display regions in the list
	virtual void OnDirective()
	{
		__super::OnDirective();
		if (InLocalScope(m_deep) || !(Psettings->mMethodInFile_ShowRegions || Psettings->mMethodInFile_ShowDefines))
			return;

		if (m_curLine == m_lastLine)
			return; // don't add line twice
		m_lastLine = m_curLine;

		const WTString directive = PV_ReadCSym(&m_buf[m_cp]);

		if (StrCmpAC(directive.c_str(), "region") == 0)
		{
			if (Psettings->mMethodInFile_ShowRegions)
			{
				const WTString defStr = TokenGetField(CurPos(), "\r\n");
				mMarkers->Root().AddChild(FileLineMarker(defStr.Wide(), m_curLine, DEFINE));
			}
		}
		else if (directive == "pragma")
		{
			if (Psettings->mMethodInFile_ShowRegions)
			{
				const WTString defStr = TokenGetField(CurPos(), "\r\n");
				int pos = defStr.Find("pragma");
				if (-1 != pos)
				{
					const WTString pragma = TokenGetField(&defStr.c_str()[pos + 7], "# \t\r\n");
					if (pragma == "region")
						mMarkers->Root().AddChild(FileLineMarker(defStr.Wide(), m_curLine, DEFINE));
					else if (pragma == "mark")
						mMarkers->Root().AddChild(FileLineMarker(defStr.Wide(), m_curLine, DEFINE));
				}
			}
		}
		else if (directive == "define")
		{
			if (Psettings->mMethodInFile_ShowDefines)
			{
				WTString line = TokenGetField(CurPos(), "\r\n");
				int pos = line.Find("define");
				if (-1 != pos)
				{
					line = line.Mid(pos + 6);
					WTString defName = TokenGetField(line, "# \t(\r\n");
					defName.TrimLeft();
					const WTString defStr = "#define " + defName;
					mMarkers->Root().AddChild(FileLineMarker(defStr.Wide(), defName, m_curLine, DEFINE));
				}
			}
		}
	}

	virtual void OnDef()
	{
		if (m_inIFDEFComment)
			return; // [case: 83377]

		bool isMem = false;
		if (IS_OBJECT_TYPE(State().m_defType) || State().m_defType == FUNC ||
		    (State().m_defType == EVENT && Psettings->mMethodInFile_ShowEvents) ||
		    (State().m_defType == PROPERTY && Psettings->mMethodInFile_ShowProperties))
		{
		}
		else if (m_deep && (State().m_defType == VAR || State().m_defType == C_ENUMITEM) &&
		         Psettings->mMethodInFile_ShowMembers && IS_OBJECT_TYPE(State(m_deep - 1).m_defType))
		{
			isMem = true;
		}
		else if (State().m_defAttr & V_EXTERN && State().m_defType == VAR)
		{
			ReadToCls rtc(GetLangType());
			auto readPos = State().m_lastWordPos;
			const int readLen = ::GetSafeReadLen(readPos, GetBuf(), GetBufLen());
			rtc.ReadTo(readPos, readLen, "({;", 100);
			if (rtc.CurChar() == '(')
			{
				// [case: 141990]
				// fix methods in file, this is a FUNC not a VAR
				State().m_defType = FUNC;
			}
			else
				return;
		}
		else
			return;

		if (m_deep && State(m_deep - 1).m_lastChar == '<')
			return; // Don't show <class args> in tree

		if (m_curLine == m_lastLine && !Is_Tag_Based(GetLangType()) && !isMem)
		{
			// for isMem, we ignore the repeat line check:
			// int mFoo, mBar;

			// don't add line twice
			return;
		}

		if (/*State().m_defType == FUNC &&*/ strchr("[,;", CurChar()) && GetLangType() == Src)
		{
			if ('[' == CurChar() && ']' == NextChar())
			{
				// [case: 63973] operator new[], operator delete[] missing from MIF
				LPCTSTR begLinePos = State().m_begLinePos;
				const WTString tmp(TokenGetField(begLinePos, "\r\n"));
				if (-1 == tmp.Find("operator"))
					return;
			}
			else if (!isMem)
				return;
		}

		WTString symscope = MethScope();
		static const WTString kUsing = DB_SEP_STR + "using";
		int usingIndex = symscope.Find(kUsing);
		if (-1 != usingIndex)
		{
			// [case: 142181] check if symscope starts with 'using' and ends with either
			// null terminator (by checking sizes) or DB_SEP_STR string (by Find == 0)
			int afterUsingIndex = usingIndex + kUsing.GetLength();
			if (symscope.GetLength() <= afterUsingIndex ||
			    symscope.substr(afterUsingIndex).Find(DB_SEP_STR) == 0)
			{
				return; // [case: 19151] don't list using statements
			}
		}

		bool objFunc = false;
		if (symscope.contains("-"))
		{
			// don't skip operator- whose scope is ":foo:-"
			if (symscope.GetLength() < 2 ||
			    (symscope[symscope.GetLength() - 1] != '-' && symscope[symscope.GetLength() - 2] != DB_SEP_CHR))
			{
				if (JS == GetLangType() && State().m_defType == FUNC && symscope.GetLength() >= 2)
				{
					// [case: 25370] adding function to object instance
					symscope = CleanScopeForDisplay(symscope);
					objFunc = true;
				}
				else
					return; // Trying to add a local symbol to MIF
			}
		}

		if (symscope.contains("_declspec"))
			return; // since we are not processing macros, we need to catch it here

		const UINT prevLine = m_lastLine;
		m_lastLine = m_curLine;

		WTString scope = StrGetSymScope(symscope);
		const WTString sym = ::StrGetSym(symscope);
		if (isMem && sym == mLastSym)
		{
			// for isMem, we check sym repeats instead of the repeat line check above
			// prevLine+1 is checked due to enum items
			if (prevLine == m_lastLine || (prevLine + 1) == m_lastLine)
				return;
		}
		mLastSym = sym;
		WTString sep;

		if (Is_Tag_Based(GetLangType()) /* && scope.Find(":<") == 0*/)
		{
			mMarkers->Root().AddChild(FileLineMarker(symscope.Wide(), m_curLine, State().m_defType, State().m_defAttr));
			return;
		}
		else if (Is_VB_VBS_File(GetLangType()))
			sep = "\r\n'";
		else if (IS_OBJECT_TYPE(State().m_defType))
			sep = ":{;=";
		else if (Is_Tag_Based(GetLangType()) && scope.Find(":<") == 0)
			sep = "<>";
		else
			sep = "{;";
		if (scope.GetLength())
			scope += '.';
		ReadToCls rtc(GetLangType());
		LPCTSTR readPos = State().m_lastScopePos;
		WTString defStr;
		if (isMem)
		{
			defStr = symscope;
			defStr.ReplaceAll(":", ".");
		}
		else
		{
			const int readLen = ::GetSafeReadLen(readPos, GetBuf(), GetBufLen());
			defStr = rtc.ReadTo(readPos, readLen, sep.c_str());
		}

		if (!strstrWholeWord(defStr, sym))
		{
			if (IS_OBJECT_TYPE(State().m_defType))
			{
				// [case: 64933] don't stop at :
				sep = "{;=";
				const int readLen = ::GetSafeReadLen(readPos, GetBuf(), GetBufLen());
				defStr = rtc.ReadTo(readPos, readLen, sep.c_str());
			}

#ifdef _DEBUG
			if (!(::strstrWholeWord(defStr, sym) || ::strstrWholeWord(defStr, "operator") ||
			      ::strstrWholeWord(sym, "this[]")))
			{
				WTString encDefStr(defStr);
				::EncodeTemplates(encDefStr);
				_ASSERTE((::strstrWholeWord(encDefStr, sym) || ::strstrWholeWord(defStr, "operator") ||
				          (JS == GetLangType() && -1 != defStr.Find("function"))) &&
				         "MethodsInFile: defStr is missing the actual sym name");
			}
#endif // _DEBUG
		}

		WTString methStr = TokenGetField(defStr, "(");
		if (!methStr.contains(".") &&
		    !methStr.contains("::")) // if def contains :: as in "foo::bar", it already contains scope
		{
			if (objFunc)
			{
				// change ": function" to "= function" so that va doesn't change ':' to '.'
				int pos = defStr.Find(":function");
				if (-1 == pos)
					pos = defStr.Find(": function");
				if (-1 != pos)
					defStr.ReplaceAt(pos, 1, "=");
			}
			defStr = scope + defStr;
		}

		defStr.ReplaceAll("abstract", "", true);
		defStr.ReplaceAll("sealed", "", true);
		defStr.ReplaceAll("final", "", true);
		defStr.ReplaceAll("Q_DECL_FINAL", "", true);

		int p = ::GetCloseParenPos(defStr, GetFType());
		if (p > 0)
		{
			if (methStr.contains("operator") && (defStr.contains("operator(") || defStr.contains("operator (")))
			{
				// [case: 61650] better support for overloads
				int p2 = defStr.Find(")", p + 1);
				if (-1 != p2)
					p = p2;
			}

			// [case: 61650] better support for overloads
			int p2 = defStr.Find("const", p);
			if (-1 != p2)
				p = p2 + strlen_i("const");
			defStr = defStr.Mid(0, p + 1);
		}

		if (CurChar() == ';')
			defStr += ';';
		else if (CurChar() == '>' && PrevChar() == '=')
		{
			int p2 = defStr.Find("=>");
			if (-1 != p2)
				defStr = defStr.Left(p2);
		}

		ULONG ln = (ULONG)GetLineNumber(State().m_defType, "(", sym);
		mMarkers->Root().AddChild(FileLineMarker(defStr.Wide(), sym, m_curLine, State().m_defType,
		                                         State().m_defAttr | State().m_privilegeAttr, ln));
	}

	BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return mIsDone;
	}

  protected:
	MethodsInFile(int fType)
	    : VAParseMPMacroC(fType), mStartTime(::GetTickCount()), mIsDone(FALSE), mResultStatus(TRUE)
	{
		mReportDecDeepErrors = TRUE;
	}
};
CREATE_MLC(MethodsInFile, MethodsInFile_MLC);

template <class VP>
class VAParseThread : public VP
{
	ULONG mLastErrorLine;
	int mLastErrorPosIdx;

  public:
	using BASE = VP;
	using BASE::CachePos;
	using BASE::CommentType;
	using BASE::CurChar;
	using BASE::CurLine;
	using BASE::CurPos;
	using BASE::DoScope;
	using BASE::FileType;
	using BASE::GetFType;
	using BASE::GetLangType;
	using BASE::GetParseClassName;
	using BASE::InComment;
	using BASE::m_buf;
	using BASE::m_cp;
	using BASE::m_deep;
	using BASE::m_inDirectiveType;
	using BASE::m_inMacro;
	using BASE::m_mp;
	using BASE::m_startReparseLine;
	using BASE::m_stopReparseLine;
	using BASE::m_updateCachePos;
	using BASE::m_useCache;
	using BASE::mBufLen;
	using BASE::mMonitorForQuit;
	using BASE::ParseTo;
	using BASE::Scope;
	using BASE::State;
	using typename BASE::Directive;

	VAParseThread(int fType)
	    : VP(fType), mQuit(false)
	{
		mLastErrorLine = 0;
		mLastErrorPosIdx = 0;
		m_hadEvent = 0;
#if defined(VA_CPPUNIT)
		m_StopOnEvents = FALSE;
#else
		m_StopOnEvents = TRUE;
#endif // VA_CPPUNIT
		EdCntPtr curEd = g_currentEdCnt;
		m_needParse = (curEd && curEd->m_FileHasTimerForFullReparse);
		m_skipUnderlining = (curEd && curEd->m_skipUnderlining == 1);
	}

	void Abort()
	{
		mForceAbort = mQuit = true;
	}

	virtual BOOL ShouldExpandEndPos(int ep)
	{
		// [case: 108472] Psettings->mEnhanceMacroParsing added for better
		// scoping with macros without having to create "LimitMacro" but
		// still off by default
		if (m_inMacro && (!Psettings->mEnhanceMacroParsing || !(IsCFile(GetFType()))))
		{
			// change 7610 used a different string value "No" than the one introduced
			// in change 13064 "NoDepthLimit".
			// I suspect that "NoDepthLimit" is broken for macros that depend on other
			// macros defined in the same file, in which case "No" is better for macro parsing.

			// [case: 1971] [case: 2913]
			// Improve performance by only expanding the first level of macro's while scoping
			// Unless disabled in the registry
			// This does not affect parse that writes to db -- only affects scoping + underline parses.
			// #FunkyMacroParsingRegistryFlags
			static const BOOL limitMacros = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "LimitMacro") != "No";
			if (limitMacros)
				return FALSE;
		}

		return VP::ShouldExpandEndPos(ep);
	}

	virtual void OnAddSym(WTString symScope)
	{
		// [case: 1971] [case: 2913]
		// Improve performance when scrolling.
		// Check/add symbols only when Edit is waiting for a reparse.
		if (m_needParse)
			VP::OnAddSym(symScope);
	}

	virtual void OnError(LPCSTR errPos)
	{
		if (!m_inMacro && mLastErrorLine == CurLine() && ISALPHA(CurChar()))
			mLastErrorPosIdx = m_cp;

		// [case: 1971] [case: 2913]
		// Don't underline when we are waiting for a full file reparse.
		if (!m_skipUnderlining)
			VP::OnError(errPos);
	}

	virtual void OnUpdateMRU()
	{
#ifdef USE_NEW_MRU_TREE
		int d = m_deep;
		while (d && InLocalScope(d - 1))
			d--;
		if (d)
			AddMRU(GetScopeInfoPtr()->GetFilename(), CStringW(Scope(d)),
			       GetTypeImgIdx(State(d - 1).m_defType, State(d - 1).m_privilegeAttr), CurLine(), TRUE);
		else
			AddMRU(CStringW(), CStringW(), 0, CurLine(), TRUE);
#endif // USE_NEW_MRU_TREE
	}

	virtual void DoParse();

	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		if (mForceAbort)
			return TRUE;

#if !defined(VA_CPPUNIT)
		if (!gTestsActive) // Don't bail in AST
		{
			if (!g_currentEdCnt || g_currentEdCnt->m_FileIsQuedForFullReparse || mQuit)
			{
				// [case: 1971] [case: 2913]
				// Bail when full file reparse is launched
				return TRUE;
			}
		}
#endif // !VA_CPPUNIT

		if (m_hadEvent)
		{
			CachePos(); // So next parse starts where this one left off
			return TRUE;
		}
		return VP::IsDone();
	}

	virtual int OnSymbol()
	{
		int retval = VP::OnSymbol();
		if (!m_inMacro && mLastErrorLine == CurLine())
		{
			if (IsCFile(FileType()))
			{
				// Mark new methods that don't have impl's yet.
				DTypePtr cd = State().m_lwData;
				if (cd && (!m_deep || m_deep && IS_OBJECT_TYPE(State(m_deep - 1).m_defType)) &&
				    cd->MaskedType() == FUNC)
				{
					const WTString cdDef(cd->Def());
					if (!cdDef.contains(")=")            // Cheesy check for pure virtuals "foo()=NULL;"
					    && !cdDef.contains(") =")        // Cheesy check for pure virtuals "foo() = NULL;"
					    && !cdDef.contains(") abstract") // cheesy check for abstract virtuals "foo() abstract"
					    && !cdDef.contains("{...}")      // Check inline methods "foo(){...}" [case=40219]
					)
					{
						DType* impl = g_pGlobDic->FindImplementation(*cd.get());
						if (!impl)
							impl = GetSysDic()->FindImplementation(*cd.get());
						if (!impl)
							mLastErrorPosIdx = m_cp;
					}
				}
			}
		}
		return retval;
	}

	WTString VAParseThread_MPGetScope(std::shared_ptr<VAParseThread<MPGetScopeCls>> _this, WTString buf,
	                                  MultiParsePtr mp, int pos)
	{
		mThis = _this;
		mBufRef = buf;
		m_mp = mp;
		_ASSERTE(!mp->IsWriteToDFile());
		if (pos >= buf.GetLength())
		{
			vLog("ERROR: VPT::MpGS bad offset");
			return NULLSTR;
		}

		// [case: 1971] [case: 2913]
		// m_StopOnEvents no longer used, we may find a case where we want the thread to wait.
		static const BOOL CanScopeInMainThread = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "CanScopeInMainThread") == "Yes";
		EdCntPtr curEd = g_currentEdCnt;
		if (CanScopeInMainThread && curEd && curEd->m_ReparseScreen)
			m_StopOnEvents = FALSE; // Wait for it to finish when typing
		m_updateCachePos = TRUE;
		mLastErrorLine = curEd ? curEd->CurLine() : UINT_MAX;
		mMonitorForQuit = &m_hadEvent;
		WTString lscope = BASE::DoMpGetScope(buf, mp, pos);

		if (m_hadEvent)
			return NULLSTR; // return NULL if we got interrupted, EdCnt will set timer to retry
		if (curEd && mp == curEd->GetParseDb())
			OnUpdateMRU();
		m_mp->m_lastErrorPos = mLastErrorPosIdx;
		return lscope;
	}

	DTypePtr VAParseThread_SymFromPos(std::shared_ptr<VAParseThread<MPGetScopeCls>> _this, WTString buf,
	                                  MultiParsePtr mp, int pos, WTString& scope, bool thorough = true)
	{
		mThis = _this;
		mBufRef = buf;
		m_updateCachePos = FALSE;

		if (g_mainThread != ::GetCurrentThreadId())
		{
			// [case: 142756]
			// don't stop on events (and parse on background thread) unless invoked from ui thread
			m_StopOnEvents = FALSE;
		}

		if (thorough)
		{
			if (pos < buf.GetLength())
			{
				// VaParser_test::testXrefTemplateFunction demonstrates that SymFromPos is reliant on MPGetScope
				// VaParser_test::testCppAuto assert 56 also shows an odd interaction
				// I wasn't able to isolate what specifically happens in DoMpGetScope that fixes SymFromPos
				m_StopOnEvents = FALSE;
				m_useCache = FALSE;
				WTString lscope = BASE::DoMpGetScope(buf, mp, pos, false);
				BASE::GetCurWordScope(false); // normally called in DoMpGetScope, but called here so that it can be done
				                              // without touching active edcnt
				_ASSERTE(!m_updateCachePos);
			}
			else
				vLog("ERROR: VPT::SFP bad offset");
		}

		ParseTo(buf, pos + 1, mp);
		if ((CommentType() == '#' && m_inDirectiveType != Directive::Include) ||
		    (InComment() && Is_Tag_Based(GetLangType())))
		{
			// Call DoScope() manually since we no longer scope in #define lines,
			for (; m_cp && ISCSYM(m_buf[m_cp - 1]); m_cp--)
				; // go back to beginning of sym
			if (!IsReservedWord(GetCStr_sv(CurPos()), GetLangType()))
				DoScope();
		}

		scope = Scope();

		DTypePtr res = BASE::GetCWData(m_deep);
		return res;
	}

	void VAParseThread_ReparseScreen(std::shared_ptr<VAParseThread<VAParseMPUnderline>> _this, EdCntPtr curEd,
	                                 WTString buf, MultiParsePtr mp, ULONG lineStart, ULONG lineStop,
	                                 BOOL underlineErrors, BOOL runAsync)
	{
		if (!curEd)
			return;

		if (curEd->m_FileIsQuedForFullReparse)
		{
			if (::IsAutoReferenceThreadRequired())
				new AutoReferenceHighlighter(curEd);
			return;
		}

		mThis = _this;
		mBufRef = buf;
		m_updateCachePos = TRUE;
		mRunParseAsyncWithoutWait = runAsync;
		if (curEd->m_ReparseScreen)
		{
			// when typing "int " on a new line, don't let it declare a var of the method on the line below
			m_stopReparseLine = (ULONG)curEd->CurLine();
			curEd->m_ReparseScreen = FALSE;
		}
		m_startReparseLine = lineStart;
		VP::ReparseScreen(curEd, buf, mp, lineStart, lineStop, underlineErrors);
		_ASSERTE(curEd == BASE::mEd);

		if (!runAsync)
			BASE::PostParse();
	}

  private:
	static void ParseThreadFunction(LPVOID _thisIn)
	{
		VAParseThread* _this = (VAParseThread*)_thisIn;
		std::shared_ptr<VAParseThread<VP>> r(_this->mThis.lock());
		ScopedIncrement si(&_this->mThreadHasPtr);
		_ASSERTE(HasSufficientStackSpace());
		_this->VAParse::DoParse();

		if (_this->mRunParseAsyncWithoutWait && !_this->mForceAbort)
			_this->PostAsyncParse();
	}

  private:
	std::weak_ptr<VAParseThread<VP>> mThis;
	INT m_hadEvent;
	BOOL m_StopOnEvents;
	BOOL m_needParse;
	BOOL m_skipUnderlining;
	WTString mBufRef;
	long mThreadHasPtr = 0;
	bool mQuit;
	bool mForceAbort = false;
	BOOL mRunParseAsyncWithoutWait = FALSE;
};

template <class VP>
void VAParseThread<VP>::DoParse()
{
	if (mRunParseAsyncWithoutWait)
	{
		auto _this = mThis.lock();
		FunctionThread* thrd = new FunctionThread(
		    ParseThreadFunction, this, CString("ParseThrdFn-Async-" + CString(GetParseClassName())), true, false);
		if (thrd)
		{
			thrd->AddRef();
			if (thrd->StartThread())
			{
				// ensure that threadFunc has grab set up shard_ptr to
				// us -- it will be our sole owner
				while (!mThreadHasPtr && !thrd->IsFinished())
					Sleep(0);
			}

			thrd->Release();
		}

		return;
	}

	if (g_CodeGraphWithOutVA && g_mainThread != ::GetCurrentThreadId())
		m_StopOnEvents = FALSE; // CodeGraph: never bail when running in thread.
	if (m_StopOnEvents)
	{
		// Start thread and watch for input events on this thread
		CPoint p1;
		::GetCursorPos(&p1);
		HWND h = g_currentEdCnt ? g_currentEdCnt->GetSafeHwnd() : NULL;
		// increment our refCnt before thread starts up
		auto _this = mThis.lock();
		FunctionThread* thrd = new FunctionThread(ParseThreadFunction, this,
		                                          CString("ParseThrdFn-" + CString(GetParseClassName())), true, false);
		if (!thrd)
			return;

		// thrd needs to be refcnted (and on heap) since we may decide to
		// leave the thread while it is still running
		thrd->AddRef();
		if (thrd->StartThread())
		{
			const bool doPeek = g_mainThread == ::GetCurrentThreadId();
			while (thrd->Wait(10, false) == WAIT_TIMEOUT)
			{
				CPoint p2;
				::GetCursorPos(&p2);
#ifdef _DEBUG
				// Allows debugging scope issues, without it bailing out.
				static BOOL sIgnoreEvents = FALSE;
				if (sIgnoreEvents)
					;
				else
#endif // _DEBUG
					if (p1 != p2)
						m_hadEvent++;
					else if (doPeek && EdPeekMessage(h))
						m_hadEvent++;

				if (m_hadEvent > 10)
				{
					ASSERT_ONCE("Scope thread not stopping.");
					vLog("WARN: VPT::DP event threshold hit - quit");
					mQuit = true;
					break;
				}
				else if (m_hadEvent && !thrd->HasStarted())
				{
					// [case: 39932] if thread hasn't even started, no need to
					// continue to wait for it to stop
					vLog("WARN: VPT::DP hadEvent, thread not started - break");
					mQuit = true;
					break;
				}
			}

			// wait for running state to ensure that threadFunc had chance to
			// grab shard_ptr to us
			while (!mThreadHasPtr && !thrd->IsFinished())
				Sleep(0);
		}

		thrd->Release();
	}
	else
	{
		// Just Call in main thread
		_ASSERTE(HasSufficientStackSpace());
		VP::DoParse();
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// External access methods
CREATE_MLC(VAParseThread<MPGetScopeCls>, MPGetScopeClsThread_MLC);

WTString MPGetScope(WTString buf, MultiParsePtr mp, int pos, int cacheLine /*= -1*/)
{
	int ftype = mp->FileType();
	if (XML != ftype)
		ftype = GetFileType(mp->GetFilename());
	MPGetScopeClsThread_MLC mps(ftype);
	if (cacheLine > 0)
		mps->m_firstVisibleLine = (ULONG)cacheLine; // Sets which line to cache when VA isn't installed
	return mps->VAParseThread_MPGetScope(mps.Ptr(), buf, mp, pos);
}

/////////////////////////////////////////////////////////////////////
// Simple Get to see if we are in a string or comment
// used for ScreenAttributes
class QuickScopeCls : public VAParseDirectiveC
{
  public:
	QuickScopeCls(int fType)
	    : VAParseDirectiveC(fType)
	{
	}

	WTString GetScope(const WTString& buf, int pos)
	{
		m_parseTo = pos;
		DoParse(buf, PF_TEMPLATECHECK /*|PF_CONSTRUCTORSCOPE*/); // no PF_CONSTRUCTORSCOPE
		if (Is_Tag_Based(FileType()) && (CommentType() == '"' || CommentType() == '\'' || CommentType() == '{'))
			return Scope(); // Enable refactor tip menu in strings.
		if (InComment())
			return WTString("String");
		return Scope();
	}

  protected:
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return m_cp >= m_parseTo || __super::IsDone();
	}
	int m_parseTo;
};

CREATE_MLC(QuickScopeCls, QuickScopeCls_MLC);

WTString QuickScope(const WTString buf, int pos, int lang)
{
	QuickScopeCls_MLC vp(lang);
	return vp->GetScope(buf, pos);
}

class QuickScopeFromLine : public DefFromLineParse
{
  public:
	QuickScopeFromLine(int fType)
	    : DefFromLineParse(fType)
	{
	}
	WTString GetSymFromLine(const WTString& buf, UINT line)
	{
		m_bufRef = buf; // add ref to buf str
		m_parseToLine = line - 1;
		Init(m_bufRef);
		DoParse();
		WTString symscope = Scope();
		WTString sym = GetCStr(State().m_lastScopePos);
		if (State().m_begLinePos != State().m_lastScopePos || sym == StrGetSym(Scope()))
			symscope += WTString(DB_SEP_CHR) + GetCStr(State().m_lastScopePos);
		return symscope;
	}
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		if (m_parseToLine < m_curLine)
		{
			if (CurChar() == '}' || m_curLine > (m_parseToLine + 1))
				return TRUE;
		}
		return __super::IsDone();
	}
};

CREATE_MLC(QuickScopeFromLine, QuickScopeFromLine_MLC);

WTString QuickScopeLine(const WTString buf, ULONG line, int col, int lang)
{
	QuickScopeFromLine_MLC vp(lang);
	return TokenGetField(vp->GetSymFromLine(buf, line), "-");
}

// based on QuickScopeContextLineCls but uses a different base class and
// GetScope is heavily modified for scope tooltips
class QuickScopeContextPosCls : public VAParseDirectiveC
{
	int m_parseToPos;
	ULONG m_parseToLine;
	int m_parseToCol;

	WTString GetScopeLineText(const WTString& ln, int indent)
	{
		if (indent && strchr("}])", ln[0]))
			indent--; // Unindent closing brace
		WTString indentStr;
		for (; indent; indent--)
			indentStr += "   ";
		token t = ln;
		t.ReplaceAll(OWL::TRegexp("[ \t\r\n]+"), OWL::string(" "));
		WTString line = indentStr + t.c_str();
		line = ::StripCommentsAndStrings(GetLangType(), line, false);
		const int kMaxLine = 120;
		if (line.GetLength() > kMaxLine)
		{
			CStringW tmp(line.Wide());
			tmp = tmp.Mid(0, kMaxLine - 3) + L"...";
			line = tmp;
		}

		while (!line.IsEmpty() && strchr("}; \t\r\n", line[line.GetLength() - 1]))
			line = line.Left(line.GetLength() - 1);
		return line;
	}

  public:
	QuickScopeContextPosCls(int fType)
	    : VAParseDirectiveC(fType)
	{
	}

	// Get text of scope
	WTString GetScope(const WTString& buf, int pos)
	{
		m_parseToPos = pos;
		m_parseToLine = NPOS;
		m_parseToCol = NPOS;

		if (m_parseToPos < buf.GetLength())
		{
			const LPCTSTR str(buf.c_str());
			const WTString tmp(TokenGetField(&str[m_parseToPos], " :{\t\r\n"));
			if (tmp == "case" || tmp == "class" || tmp == "default" || tmp == "enum" || tmp == "struct")
			{
				// scope at the 'c' of "case" (and 'd' of "default") is not
				// quite right - push in one letter (something due to tag handling?)
				++m_parseToPos;
			}
		}

		DB_READ_LOCK;
		VAParseDirectiveC::DoParse(buf, PF_TEMPLATECHECK /*|PF_CONSTRUCTORSCOPE*/); // no PF_CONSTRUCTORSCOPE
		if (!Depth())
			return NULLSTR;

		WTString scope;
		int switchCount = 0;
		WTString switchCaseLabel;
		for (ULONG d = 0; d <= Depth(); d++)
		{
			if (d)
				scope += "\r\n";
			if (State(d).m_begLinePos[0] == ';')
				State(d).m_begLinePos = CurPos();

			LPCSTR linepos;
			const uint defType = State(d).m_defType;
			switch (defType)
			{
			case NAMESPACE:
			case CLASS:
			case STRUCT:
			case PROPERTY:
				linepos = State(d).m_begLinePos;
				break;

			default:
				if (InLocalScope(d) || d == Depth())
					linepos = State(d).m_begLinePos;
				else
					linepos = State(d).m_lastScopePos;
			}

			const char* termTok = IS_OBJECT_TYPE(defType) ? "{" : "\r\n";
			if (PROPERTY == defType || FUNC == defType)
				termTok = "{\r\n";
			else if (!defType && d && State(d - 1).m_defType == PROPERTY)
				termTok = "{\r\n"; // property foo { get { return _foo;

			WTString tok(TokenGetField(linepos, termTok));
			tok.TrimRight();
			if (d == Depth())
			{
				WTString lnTmp(tok);
				if (NPOS == lnTmp.find_first_not_of(" \t\r\n};"))
				{
					// no lone trailing braces
					scope.TrimRight();
					return scope;
				}
			}

			WTString line = GetScopeLineText(tok, int(d) + switchCount);
			if (d == Depth())
			{
				WTString cline = TokenGetField(CurPos(), "\r\n");
				cline.TrimLeft();
				if (!line.contains(cline))
				{
					if (line.contains("{"))
					{
						const WTString scLnTxt(GetScopeLineText(cline, int(d) + switchCount + 1));
						line += WTString("\r\n") + scLnTxt;
					}
				}
			}

			if (State(d).GetParserStateFlags() == VPSF_SCOPE_TAG)
			{
				bool addScopeTagLine = false;
				WTString tok2(TokenGetField(State(d).m_lastWordPos, "\r\n"));
				if (tok2 == "public:" || tok2 == "private:" || tok2 == "protected:" ||
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
				    tok2 == "__published:" ||
#endif
				    tok2 == "internal:")
				{
					if (d < Depth())
					{
						if (State(d + 1).m_defType == UNDEF)
						{
							// the next pass through will add the visibility tag
						}
						else
						{
							addScopeTagLine = true;
						}
					}
					else
						addScopeTagLine = true;
				}
				else
				{
					// deal with funky case statement handling
					if (StartsWith(State(d + 1).m_begLinePos, "case") ||
					    StartsWith(State(d + 1).m_begLinePos, "default"))
					{
						// the next pass through will add the case statement,
						// ignore current depth
					}
					else
					{
						LPCSTR curPos = State(d).m_lastWordPos;
						if (StartsWith(curPos, "default"))
						{
							addScopeTagLine = true;
						}
						else
						{
							// in some situations, lastWordPos is the label after 'case' -
							// rewind to include 'case'
							curPos -= 5;
							if (curPos > m_buf && StartsWith(curPos, "case"))
							{
								addScopeTagLine = true;
								tok2 = TokenGetField(curPos, "\r\n");
							}
						}
					}
				}

				if (addScopeTagLine)
				{
					const WTString scLnTxt(WTString("\r\n") + GetScopeLineText(tok2, int(d) + (++switchCount)));
					if (d == Depth() - 1 && -1 != line.Find("switch"))
						switchCaseLabel = scLnTxt;
					line += scLnTxt;
				}
			}

			if (d == Depth() && !switchCaseLabel.IsEmpty())
			{
				if (line.IsEmpty())
				{
					// we were too aggressive in adding the case label in the previous
					// iteration (at Depth() - 1).
					// now we know there's nothing after it, so undo
					int pos2 = scope.ReverseFind(switchCaseLabel);
					if (-1 != pos2)
						scope = scope.Left(pos2);
				}
			}

			scope += line;
		}
		return scope;
	}

  protected:
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return m_cp >= m_parseToPos || (CurLine() > m_parseToLine && (State().m_begLinePos - m_buf) >= m_parseToCol);
	}
	int m_parseTo;
};
CREATE_MLC(QuickScopeContextPosCls, QuickScopeContextPosCls_MLC);

WTString QuickScopeContext(const WTString& buf, int pos, int lang)
{
	QuickScopeContextPosCls_MLC vp(lang);
	return vp->GetScope(buf, pos);
}

class QuickScopeContextLineCls : public VAParse
{
	int m_parseToPos;
	ULONG m_parseToLine;
	int m_parseToCol;

  public:
	QuickScopeContextLineCls(int fType)
	    : VAParse(fType)
	{
	}
	WTString GetScopeLineText(const WTString& ln, int indent)
	{
		if (indent && strchr("}])", ln[0]))
			indent--; // Unindent closing brace
		WTString indentStr;
		for (; indent; indent--)
			indentStr += "   ";
		token t = ln; // GetSubStr(State(d).m_begLinePos, State(d+1).m_begBlockPos-1);
		t.ReplaceAll(OWL::TRegexp("[ \t\r\n]+"), OWL::string(" "));
		WTString line = indentStr + t.c_str();
		const int kMaxLine = 80;
		if (line.GetLength() > kMaxLine)
		{
			CStringW tmp(line.Wide());
			tmp = tmp.Mid(0, kMaxLine - 3) + L"...";
			line = tmp;
		}
		return line;
	}
	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		__super::DecDeep();
		if (FileType() == CS && !InLocalScope())
			ClearLineState(CurPos()); // TODO: looks like a bug in CS where m_begLinePos doesn't get set after a
			                          // property.  See WholeTomato.VAGraphNS.VAReferencesDB.DbDir
	}
	WTString GetScope(const WTString& buf, int pos)
	{
		m_parseToPos = pos;
		return GetScope(buf, NPOS, NPOS);
	}
	WTString GetScope(const WTString& buf, UINT line, int col)
	{
		// Get text of scope
		m_parseToLine = line;
		m_parseToCol = col;
		DoParse(buf, PF_TEMPLATECHECK /*|PF_CONSTRUCTORSCOPE*/); // no PF_CONSTRUCTORSCOPE
		if (!Depth())
			return NULLSTR;

		WTString scope;
		int switchCount = 0;
		UINT ignoreIndent = 0;
		for (ULONG d = 0; d <= Depth(); d++)
		{
			if (d)
				scope += "\r\n";
			if (State(d).m_begLinePos[0] == ';')
				State(d).m_begLinePos = CurPos();
			if (!InLocalScope(d) || State(d).m_defType == FUNC)
			{
				ignoreIndent = d;
				scope.Empty();
			}
			LPCSTR linepos = InLocalScope(d) ? State(d).m_begLinePos : State(d).m_lastScopePos;
			if (d == Depth())
				linepos = State(d).m_begLinePos;
			WTString line2 = GetScopeLineText(TokenGetField(linepos, "\r\n"), int(d + switchCount - ignoreIndent));
			if (d == Depth())
			{
				WTString cline = TokenGetField(CurPos(), "\r\n");
				cline.TrimLeft();
				if (!line2.contains(cline))
					line2 += WTString("\r\n") + GetScopeLineText(cline, int(d + switchCount + 1 - ignoreIndent));
			}
			if (State(d).GetParserStateFlags() == VPSF_SCOPE_TAG)
				line2 += WTString("\r\n") +
				         GetScopeLineText(TokenGetField(State(d).m_lastWordPos, "\r\n"), int(d + (++switchCount)));
			scope += line2;
		}
		return scope;
	}

  protected:
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return m_cp >= m_parseToPos || (CurLine() > m_parseToLine && (State().m_begLinePos - m_buf) >= m_parseToCol);
	}
	int m_parseTo;
};
CREATE_MLC(QuickScopeContextLineCls, QuickScopeContextLineCls_MLC);

WTString QuickScopeContext(const WTString& buf, ULONG line, int col, int lang)
{
	QuickScopeContextLineCls_MLC vp(lang);
	return vp->GetScope(buf, line, col);
}

DTypePtr SymFromPos(WTString buf, MultiParsePtr mp, int pos, WTString& scope, bool thorough /*= true*/)
{
	LogElapsedTime let("SymFromPos: ", scope, 50);
	MPGetScopeClsThread_MLC mps(GetFileType(mp->GetFilename()));
	return mps->VAParseThread_SymFromPos(mps.Ptr(), buf, mp, pos, scope, thorough);
}

DTypePtr PrevDepthSymFromPos(WTString buf, MultiParsePtr mp, int& pos, WTString& scope, bool thorough /*= true*/)
{
	LogElapsedTime let("PrevDepthSymFromPos: ", scope, 50);
	MPGetScopeClsThread_MLC mps(GetFileType(mp->GetFilename()));
	DTypePtr p = mps->VAParseThread_SymFromPos(mps.Ptr(), buf, mp, pos, scope, thorough);

	ULONG depth = mps->Depth();
	if (depth == 0)
		return nullptr;
	auto prevState = mps->State(mps->Depth() - 1);

	pos = ptr_sub__int(prevState.m_begLinePos, mps->GetBuf());
	return prevState.m_lwData;
}

bool IsUnderlineThreadRequired()
{
	if (!gShellAttr)
		return false;

	if (!Psettings->m_ActiveSyntaxColoring && !Psettings->mUseMarkerApi)
		return false;

	if (Psettings->mAutoHighlightRefs && !Psettings->mUseAutoHighlightRefsThread)
		return true;

	if (Psettings->mSimpleWordMatchHighlights)
		return true;

	if (Psettings->m_bSupressUnderlines)
		return false;

	if (Psettings->m_underlineTypos || Psettings->m_spellFlags)
		return true;

	// [case: 108516]
	// hashtag markers require underline thread
	return true;
}

CREATE_MLC(VAParseThread<VAParseMPUnderline>, VAParseMPUnderlineThread_MLC);

std::weak_ptr<VAParseThread<VAParseMPUnderline>> gActiveUnderliner;

void ReparseScreen(EdCntPtr ed, WTString buf, MultiParsePtr mp, ULONG lineStart, ULONG lineStop, BOOL underlineErrors,
                   BOOL runAsync)
{
	_ASSERTE(g_mainThread == ::GetCurrentThreadId());
	if (!ed)
		return;

	// I'm not checking IsUnderlineThreadRequired here because it is not
	// clear to me what ReparseScreen is supposed to do when underlineErrors
	// is false.  It seems like it would be better to put the check here
	// than in VAParseMPUnderline::DoScope.  Note that underlineErrors is
	// true even when the underline options are off - required for auto
	// highlight refs.

	{
		auto prevUnderliner = gActiveUnderliner.lock();
		if (prevUnderliner)
			prevUnderliner->Abort();
	}

	VAParseMPUnderlineThread_MLC mps(mp->FileType());
	auto ptr = mps.Ptr();
	gActiveUnderliner = ptr;
	mps->VAParseThread_ReparseScreen(ptr, ed, buf, mp, lineStart, lineStop, underlineErrors, runAsync);
}

BOOL GetMethodsInFile(WTString buf, MultiParsePtr mp, LineMarkers& markers)
{
	markers.Clear();

	MethodsInFile_MLC mps(mp->FileType());
	return mps->GetMethods(buf, mp, true, markers);
}

//////////////////////////////////////////////////////////////////////////
WTString DefFromLineParse::GetDefFromLine(WTString buf, ULONG line)
{
	m_bufRef = buf; // add ref to buf str
	m_parseToLine = line - 1;
	Init(m_bufRef);
	DoParse();
	WTString lineStr = GetLineStr();
	return lineStr;
}

BOOL DefFromLineParse::IsDone()
{
	_ASSERTE(m_cp <= mBufLen);
	if (m_parseToLine < m_curLine)
	{
		if (Is_VB_VBS_File(GetLangType()) || CurChar() == ';')
			return TRUE;

		if (CurChar() == '{')
		{
			// [case: 131324]
			bool braceInit = false;
			if (m_cp > 0)
			{
				for (int i = m_cp - 1; i > 0; i--)
				{
					TCHAR c = m_buf[i];
					if (IsWSorContinuation(c))
						continue;
					if (c == '{') // eat up all the {s for multi-dimensional array support to arrive at a = or , for
					              // braceinit
						continue;
					if (c == '=' || c == ',')
						braceInit = true;
					break;
				}
			}
			if (!braceInit)
			{
				if (m_deep < 3)
					return TRUE;

				if (State(m_deep - 1).m_lastChar == '(')
				{
					if (State(m_deep - 2).m_defAttr & V_CONSTRUCTOR)
					{
						if (State(m_deep - 2).m_lastChar == ':')
						{
							// [case: 79737]
							// foo() : mMem({1}) { }
							return FALSE;
						}
					}
				}

				return TRUE;
			}
		}
	}

	return FALSE;
}

class VAParseMPGlobals : public VAParseMPScope
{
  public:
	BOOL processLocalMacros;

	void ParseMPGlobals(const WTString& buf, MultiParsePtr mp)
	{
		m_startReparseLine = 0;
		processLocalMacros = FALSE;
		m_parseTo = MAXLONG;
		m_writeToFile = TRUE;
		m_parseGlobalsOnly = TRUE;
		// [case: 81110] [case: 81111]
		mParseLocalTypes = Psettings->mParseLocalTypesForGotoNav && Src == mp->FileType();
		Parse(buf, mp);
	}

	virtual void OnDef()
	{
		if (InLocalScope() && !ShouldForceOnDef())
		{
			// [case: 81110] [case: 81111]
			if (!mParseLocalTypes)
				return;

			if (!((State().m_defType == CLASS && strchr(",;{", CurChar())) ||
			      (State().m_defType == STRUCT && strchr(",;{", CurChar())) ||
			      (State().m_defType == TYPE && strchr(";", CurChar()))))
				return;
		}

		VAParseMPScope::OnDef();
	}

	virtual BOOL ProcessMacro()
	{
		if (processLocalMacros || !InLocalScope())
		{
			ULONG deep = m_deep;
			BOOL r = VAParseMPScope::ProcessMacro();
			if (m_deep != deep)
				processLocalMacros = TRUE;
			return r;
		}
		return FALSE;
	}

	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPGlobals";
	}

  protected:
	VAParseMPGlobals(int fType)
	    : VAParseMPScope(fType)
	{
	}
};

CREATE_MLC(VAParseMPGlobals, VAParseMPGlobals_MLC);

class VAParseMPForGoToDef : public VAParseMPScope
{
  public:
	void ParseMPForGoToDef(const WTString& buf, MultiParsePtr mp)
	{
		m_startReparseLine = 0;
		m_parseTo = MAXLONG;
		m_writeToFile = TRUE;
		m_parseGlobalsOnly = TRUE;
		// [case: 81110] [case: 81111]
		mParseLocalTypes = Psettings->mParseLocalTypesForGotoNav && Src == mp->FileType();
		Parse(buf, mp);
	}

	virtual void OnDef()
	{
		if (InLocalScope())
		{
			// [case: 81110] [case: 81111]
			if (!mParseLocalTypes)
				return;

			if (!((State().m_defType == CLASS && strchr(",;{", CurChar())) ||
			      (State().m_defType == STRUCT && strchr(",;{", CurChar())) ||
			      (State().m_defType == C_INTERFACE && strchr(",;{", CurChar())) ||
			      (State().m_defType == TYPE && ';' == CurChar())))
				return;
		}
#ifdef NO_MEMBERS_OF_LOCAL_CLASSES
		else if (m_deep > 1 && mParseLocalTypes)
		{
			// [case: 81110] [case: 81111]
			if (ConstState(m_deep - 2).m_defType == FUNC)
			{
				// InLocalScope only checks m_deep - 1, so a members of a class in
				// a local function are not InLocalScope.
				return;
			}
		}
#endif

		if ((State().m_defType == FUNC && strchr(";{", CurChar())) ||
		    (State().m_defType == VAR && strchr(",;{", CurChar())) ||
		    (State().m_defType == PROPERTY && strchr(";{", CurChar())) ||
		    (State().m_defType == GOTODEF && strchr(",;{", CurChar())) ||
		    (State().m_defType == CLASS && strchr(",;{", CurChar())) ||
		    (State().m_defType == STRUCT && strchr(",;{", CurChar())) ||
		    (State().m_defType == C_INTERFACE && strchr(",;{", CurChar())) ||
		    (State().m_defType == NAMESPACE && strchr("{", CurChar())) ||
		    (State().m_defType == TYPE && ';' == CurChar()) ||
		    (State().m_defType == C_ENUM && strchr(";{", CurChar())) ||
		    (State().m_defType == C_ENUMITEM && strchr(",}", CurChar())))
		{
			const WTString scopePosStr(GetCStr(State().m_lastScopePos));
			DType* mac = m_mp->GetMacro(scopePosStr);
			if (mac && mac->IsVaStdAfx())
			{
				if (mac->SymMatch("_CRT_ALIGN") || mac->SymMatch("__attribute__") || mac->SymMatch("__declspec") ||
				    mac->SymMatch("_declspec"))
				{
					// [case: 73348]
					mac = nullptr;
				}
			}

			if (!mac) // since we are skipping macros processing, don't add a macro's as methods
			{
				ULONG dtype = State().m_defType;

				switch (dtype)
				{
				case CLASS:
				case STRUCT:
				case C_ENUM:
				case C_ENUMITEM:
				case TYPE:
				case NAMESPACE:
				case C_INTERFACE:
					break;
				case FUNC:
					if (CurChar() != ';')
					{
						// [case: 62542]
						// if CurChar() is ';' then it IS the declaration;
						// if not, then don't confuse the implementation with the declaration
						State().m_defType = GOTODEF;
					}
					break;
				default:
					State().m_defType = GOTODEF; // so we don't confuse these with the declaration
					break;
				}

				VAParseMPScope::OnDef();
				State().m_defType = dtype;
			}
		}
	}

	virtual void OnDirective()
	{
		const int pos = Idl != GetLangType() || m_buf[m_cp] == '#' ? m_cp + 1 : m_cp;
		// skip VAParseMPMacroC::OnDirective (to improve performance of GotoDef parse ?)
		VAParseDirectiveC::OnDirective();
		if (m_inDirectiveType == Directive::Define && m_mp)
		{
			// [case: 100004]
			// record macros so that macros used to create
			// classes/interfaces/structs/enums can be processed.
			// macros processed here are not serialized, but are added to
			// hashtable and are serialized during local parse.
			m_mp->SetBuf(m_buf, (ULONG)mBufLen, (ULONG)pos, m_curLine);
			m_mp->AddPPLnMacro();
			m_mp->ClearBuf();
		}
	}

	virtual BOOL ProcessMacro()
	{
		if (Src != GetFType())
		{
			// case 14901 old behavior works in header files
			return FALSE;
		}

		return ConditionallyProcessMacro();
	}

	virtual LPCSTR GetParseClassName()
	{
		return "VAParseMPForGoToDef";
	}

  protected:
	VAParseMPForGoToDef(int fType)
	    : VAParseMPScope(fType)
	{
	}
};

CREATE_MLC(VAParseMPForGoToDef, VAParseMPForGoToDef_MLC);

void VAParseParseGlobalsToDFile(WTString buf, MultiParsePtr mp, BOOL useCache)
{
	if (mp->GetParseType() == ParseType_GotoDefs)
	{
		VAParseMPForGoToDef_MLC mps(GetFileType(mp->GetFilename()));
		mps->m_useCache = useCache;
		mps->ParseMPForGoToDef(buf, mp);
	}
	else
	{
		VAParseMPGlobals_MLC mps(GetFileType(mp->GetFilename()));
		mps->m_useCache = useCache;
		mps->ParseMPGlobals(buf, mp);
	}
}

CREATE_MLC(VAParseMPScope, VAParseMPScope_MLC);

// List of strings to underline and their screen position
void VAParseParseLocalsToDFile(WTString buf, MultiParsePtr mp, BOOL useCache)
{
	VAParseMPScope_MLC mps(GetFileType(mp->GetFilename()));
	mps->m_parseGlobalsOnly = FALSE;
	mps->m_startReparseLine = 0;
	mps->m_parseTo = MAXLONG;
	mps->m_writeToFile = TRUE;
	mps->m_useCache = mps->m_updateCachePos = useCache;
	mps->Parse(buf, mp);
}

//////////////////////////////////////////////////////////////////////////
class VANextScopeCls : public VAParseDirectiveC
{
	int m_curPos;
	ULONG m_edLn;
	LPCSTR m_lastScopePos;
	BOOL m_findNext;
	ULONG m_cursorDeep;

  public:
	int FindNextScopePos(const WTString buf, int curPos, ULONG startLine, BOOL next)
	{
		DEFTIMERNOTE(VAP_VANextScopeCls, NULL);
		m_edLn = startLine;
		m_curPos = curPos ? curPos - 1 : curPos;
		m_lastScopePos = NULL;

		m_findNext = next;
		m_cursorDeep = 999;

		DoParse(buf, PF_TEMPLATECHECK);
		if (InLocalScope())
		{
			if (next)
				m_lastScopePos = CurPos();
			else
			{
				// [case: 87471]
				if (!(m_lastScopePos && m_lastScopePos > State().m_begBlockPos))
					m_lastScopePos = State().m_begBlockPos;
			}
		}

		if (!m_lastScopePos)
			m_lastScopePos = next ? CurPos() : m_buf;
		const auto tmpCp = m_lastScopePos - m_buf;
		if (tmpCp > mBufLen || tmpCp < 0)
		{
			_ASSERTE(!"bad m_cp in FindNextScopePos");
			m_cp = 0;
		}
		else
			m_cp = (int)tmpCp;

		if (Is_VB_VBS_File(GetLangType()) && m_cp > 2)
			m_cp -= 2; // VB points after current line, get previous line before \r\n

		if (m_buf && *m_buf)
		{
			// goto beginning of line
			while (((LONG)m_cp) > 0 && !strchr("\r\n", m_buf[m_cp - 1]))
				m_cp--;
		}
		else
			m_cp = 0;

		return m_cp;
	}

	virtual void IncDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		const BOOL wasInClassScope = InClassScope(m_deep);
		const BOOL wasInLocalScope = InLocalScope();
		const char curCh = CurChar();
		if (!wasInLocalScope && !m_inMacro && !strchr("<([", curCh))
		{
			if ((!m_findNext && m_curLine < m_edLn) || (m_findNext && m_curLine > m_edLn))
				m_lastScopePos = CurPos();
		}

		VAParseKeywordSupport::IncDeep();

		if (!wasInClassScope && '{' == curCh && InClassScope(m_deep))
		{
			// [case: 87471]
			if ((!m_findNext && m_curLine < m_edLn) || (m_findNext && m_curLine > m_edLn))
				m_lastScopePos = CurPos();
		}
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		VAParseKeywordSupport::DecDeep();

		if (m_curLine == m_edLn && m_deep)
		{
			const char curCh = CurChar();
			if (strchr("}\r\n", curCh))
			{
				// [case: 87471]
				// I'm unable to produce an example that ever enters this condition.
				// Prior to adding the condition, the only time we entered, it produced
				// undesirable behavior (during next scope in deeply nested braces).
				// Can't figure out what it was attempting to do...
				if (!m_findNext || '}' != curCh)
				{
					// [case: 726] ??
					// sitting on } line, find next }
					m_cursorDeep = m_deep - 1;
				}
			}
		}
	}

	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		if (m_cp > m_curPos)
		{
			if (m_findNext)
			{
				// find closing brace or next class/method/namespace
				if (m_cursorDeep == 999)
					m_cursorDeep = InLocalScope() || InClassScope(m_deep) ? m_deep : 0;
				else if (m_lastScopePos)
					return TRUE; // Found next scope
				else if (m_cursorDeep && m_deep < m_cursorDeep)
				{
					if (m_curLine == m_edLn)
						m_cursorDeep = m_deep; // caret is just to the left of the }, find parent }
					else if (Is_VB_VBS_File(GetLangType()) &&
					         (m_curLine - 1) == m_edLn) // DecDeep doesn't get called until after the \r\n
						m_cursorDeep = m_deep;          // caret is just to the left of the "End..."  case:20615
					else
						return TRUE; // Found closing brace
				}
			}
			else
			{
				return TRUE;
			}
		}
		return FALSE;
	}

	virtual BOOL ParseNamespaceAsClassScope() const
	{
		return TRUE;
	}

	virtual void OnForIf_CreateScope(NewScopeKeyword keyword) override
	{
	}
	virtual BOOL OnForIf_CloseScope(NewScopeKeyword keyword) override
	{
		return FALSE;
	}

  protected:
	VANextScopeCls(int fType)
	    : VAParseDirectiveC(fType)
	{
	}
};
CREATE_MLC(VANextScopeCls, VANextScopeCls_MLC);

int FindNextScopePos(int fType, const WTString& buf, int cp, ULONG startLine, BOOL next)
{
	VANextScopeCls_MLC ns(fType);
	_ASSERTE(buf.GetLength() >= cp);
	return ns->FindNextScopePos(buf, cp, startLine, next);
}

//////////////////////////////////////////////////////////////////////////
// Misc
WTString GetCStr(LPCSTR p)
{
	int i;
	for (i = 0; p[i] && (p[i] == '~' || (p[i] == '!' && ISCSYM(p[i + 1])) || ISCPPCSTR(p[i])); i++)
		;
	return WTString(p, i);
}
std::string_view GetCStr_sv(const char *p)
{
	uint32_t i;
	for (i = 0; p[i] && (p[i] == '~' || (p[i] == '!' && ISCSYM(p[i + 1])) || ISCPPCSTR(p[i])); i++)
		;
	return {p, i};
}

WTString GetOperatorChars(LPCSTR p, LPCSTR* outEndPtr /*= NULL*/)
{
	for (; *p && (*p == '~' || (*p == '!' && ISCSYM(*(p + 1))) || ISCSYM(*p)); p++)
		;
	for (; *p && wt_isspace(*p); p++)
		; // whitespace
	int i = 0;
	if (p[i] && p[i] == '(') // handle operator()()
		++i;
	for (; p[i] && p[i] != '(' && !wt_isspace(p[i]); i++)
		;
	if (*p != '(' && p[i] && p[i] != '(')
	{
		// [case: 10386] check for another sym as in "operator unsigned int();"
		int newI = i;
		for (; p[newI] && p[newI] != '(' && wt_isspace(p[newI]); newI++)
			; // whitespace
		const int prevI = newI;
		for (; p[newI] && p[newI] != '(' && !wt_isspace(p[newI]); newI++)
			;
		if (prevI != newI)
			i = newI;
	}
	if (outEndPtr)
		*outEndPtr = p + i;
	if (ISCSYM(*p))
	{
		// Encode to "operator LPCSTR" so we do not add just MyCls::LPCSTR
		WTString op = WTString("operator ") + WTString(p, i);
		op.ReplaceAll("::", ".");
		return EncodeScope(op);
	}
	return WTString(p, i);
}

BOOL ParseToCls::ParseTo(LPCSTR buf, int bufLen, LPCSTR readto, int maxLen /* = 1024 */,
                         int parseFlags /*= PF_TEMPLATECHECK*/)
{
	// parseFlags: no PF_CONSTRUCTORSCOPE by default
	DEFTIMERNOTE(VAP_ParseToCls, NULL);
	_ASSERTE(readto);
	// for things like IsTemplate, same string is passed in frequently; avoid unnecessary heap operations
	if (m_readToStr != readto)
		m_readToStr = readto;
	m_maxLen = maxLen;
	m_hadError = FALSE;
	mFoundContinuedLine = false;
	DoParse(buf, bufLen, parseFlags);
	return m_hadError == FALSE;
}

BOOL ParseToCls::IsDone()
{
	if (m_cp > mBufLen)
		return TRUE;

	if (m_readToPos && m_cp >= m_readToPos)
		return TRUE;

	if (!InComment() && !m_deep)
	{
		bool stop = false;
		const char ch = CurChar();
		switch (m_readToStr.GetLength())
		{
		case 1:
			if (ch == m_readToStr[0])
				stop = true;
			break;
		case 2:
			if (ch == m_readToStr[0] || ch == m_readToStr[1])
				stop = true;
			break;
		case 3:
			if (ch == m_readToStr[0] || ch == m_readToStr[1] || ch == m_readToStr[2])
				stop = true;
			break;
		case 4:
			if (ch == m_readToStr[0] || ch == m_readToStr[1] || ch == m_readToStr[2] || ch == m_readToStr[3])
				stop = true;
			break;
		default:
			if (strchr(m_readToStr.c_str(), ch)) // slower
				stop = true;
			break;
		}

		if (stop)
		{
			if (mAcceptLineContinuation && ('\r' == ch || '\n' == ch))
			{
				if (Is_VB_VBS_File(GetFType()))
				{
					if (PrevChar() == '_')
					{
						mFoundContinuedLine = true;
						if (NextChar() == '\n')
							IncCP();
						if (m_cp < m_maxLen)
							return FALSE;
					}
				}
				else if (Is_C_CS_File(GetFType()))
				{
					if (PrevChar() == '\\')
					{
						mFoundContinuedLine = true;
						if (NextChar() == '\n')
							IncCP();
						if (m_cp < m_maxLen)
							return FALSE;
					}
				}
			}

			return TRUE;
		}
	}

	if (m_cp >= m_maxLen)
	{
		m_hadError = TRUE;
#if !defined(SEAN)
		DebugMessage("ReadTo MaxLen exceeded");
#endif
		return TRUE;
	}
	return FALSE;
}

// consider tracking base comment status in ParserState rather than
// walking all states every time (update in IncDeep/DecDeep where a comment
// ParserState does not cause itself to be considered a base comment, but
// a comment depth could be both a comment and have a base comment state, for
// example a string literal in a nested interpolated string).  Then
// AnyBaseStateIsInComment() would just check that tracking flag in the
// current State().
bool ParseToCls::AnyBaseStateIsInComment()
{
	if (IsCFile(GetLangType()))
		return false;

	// [case: 98335]
	if (!m_deep)
		return false;

	for (int i = int(m_deep - 1); i >= 0; i--)
	{
		if (State((uint)i).m_inComment)
			return true;
	}

	return false;
}

WTString ReadToCls::ReadTo(LPCSTR buf, int bufLen, LPCSTR readto, int maxLen /*= 1024*/)
{
	DEFTIMERNOTE(VAP_ReadTo, NULL);

#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		_ASSERTE(::strlen(buf) >= (size_t)bufLen && bufLen >= 0);

	// pre-alloc space for speed
	m_argStr.PreAllocBuffer(maxLen > 1024 ? 2048 : 1024);
	m_lastChar = '\0';
	inParens = inblock = 0;
	m_maxLen = maxLen;
	mAcceptLineContinuation = Is_VB_VBS_File(GetFType());
	if (!ParseTo(buf, bufLen, readto, maxLen))
	{
#if !defined(SEAN)
		DebugMessage("ParseTo failed.");
#endif
	}

	if (mFoundContinuedLine)
	{
		// [case: 60988]
		// I think this was intended for single statement buf input (not a whole file or class)
		m_argStr.ReplaceAll("\r\n", " ");
		m_argStr.ReplaceAll('\r', ' ');
		m_argStr.ReplaceAll('\n', ' ');
	}

	if (CurChar() == '>' && PrevChar() == '=' && m_readToStr.contains(">"))
	{
		// don't split "=>"
		m_argStr.LeftInPlace(m_argStr.GetLength() - 1);
	}

	return m_argStr;
}

void ReadToCls::OnChar()
{
	bool doCurChar = false;
	char c = CurChar();

	if (InComment())
	{
		if (mKeepStrings)
		{
			const char cmtType = CommentType();
			_ASSERTE(cmtType != 'L' && cmtType != 'S' && cmtType != '@' && cmtType != '$');
			if (cmtType == '\'' || cmtType == '"' || cmtType == '{')
				doCurChar = true;
		}

		if (c == 0)
			doCurChar = false;
	}
	else
	{
		if (c == '{' && !inblock)
		{
			inblock = m_deep;
			if (mKeepBraces)
				doCurChar = true;
		}
		else if (c == '}' && inblock == m_deep - 1)
		{
			inblock = 0;
			if (mKeepBraces)
				doCurChar = true;
		}
		else if (c == '(' && !inblock && !inParens)
		{
			inParens = m_deep;
			doCurChar = true;
		}
		else if (c == ')' && !inblock && inParens == m_deep - 1)
		{
			inParens = 0;
			doCurChar = true;
		}
		else if (!inblock) // since this is called before we process c, we need to discard comment and string chars
		{
			doCurChar = true;
			if (mKeepStrings && '#' == c)
				doCurChar = false;
			else if (!mKeepStrings && ('"' == c || '\'' == c || '#' == c))
				doCurChar = false;
			else if ('/' == c && // test for operator /, etc.
			         (PrevChar() == '/' || NextChar() == '/' || NextChar() == '*'))
			{
				doCurChar = false;
			}
			else if (c == 0)
				doCurChar = false;
		}
	}

	if (doCurChar)
	{
		if (wt_isspace(c))
		{
			if (m_argStr.GetLength() && !wt_isspace(m_lastChar))
				m_argStr += ' ';
		}
		else if (c == '<' && wt_isspace(m_lastChar) && m_argStr.GetLength())
		{
			// change "TFoo <x> f;" into "TFoo<x> f;"
			m_argStr.SetAt(m_argStr.GetLength() - 1, c);
		}
		else if (c == ':' && NextChar() == ':')
		{
			m_argStr += "::"; // we no longer convert any ::'s to .'s
		}
		else if (!m_deep && !InComment() && c == ')')
		{
			// Strip unmatched closing paren's if we are in top scope.
			// With prototype "void (funcPtr)(args);", we call ReadTo("funcPtr)(args);")
			// return "funcPt(args)", not "funcPtr)(args)" // case=3721
		}
		else
			m_argStr.ConcatInPlace(1, &c);

		m_lastChar = c;
	}
}

void ReadToCls::OnComment(char c, int altOffset /*= UINT_MAX*/)
{
	__super::OnComment(c, altOffset);
	if (c && mKeepStrings && InComment())
	{
		const char cmtType = CommentType();
		_ASSERTE(cmtType != 'L' && cmtType != 'S' && cmtType != '@' && cmtType != '$' && cmtType != 'C');
		if (cmtType == '\'' || cmtType == '"' || cmtType == '{')
		{
			if (altOffset != UINT_MAX)
			{
				_ASSERTE((altOffset + m_cp) < mBufLen);
				for (int idx = 0; idx < altOffset; ++idx)
					m_argStr += m_buf[idx + m_cp];
			}

			m_argStr += c;
			m_lastChar = c;
		}
	}
}

void ArgsSplitter::IncDeep()
{
	if (m_deep == 0)
	{
		char ch = CurChar();
		if ((ArgSplitType::AngleBrackets == mSplitType && '<' == ch) ||
		    (ArgSplitType::Parens == mSplitType && '(' == ch))
			mCurArgStartPos = CurPos() + 1;
	}

	__super::IncDeep();
}

void ArgsSplitter::DecDeep()
{
	__super::DecDeep();

	if (m_deep == 0)
	{
		char ch = CurChar();
		if ((ArgSplitType::AngleBrackets == mSplitType && '>' == ch) ||
		    (ArgSplitType::Parens == mSplitType && ')' == ch))
		{
			// terminate current arg
			if (mCurArgStartPos)
			{
				WTString sym(mCurArgStartPos, ptr_sub__int(CurPos(), mCurArgStartPos));
				sym = ::StripCommentsAndStrings(GetLangType(), sym, false);
				sym.Trim();
				if (sym.IsEmpty())
					mIsDone = true;
				else
					mArgs.push_back(sym);
				mCurArgStartPos = nullptr;
			}
			mIsDone = true;
		}
	}
}

void ArgsSplitter::OnChar()
{
	if (!InComment() && m_deep == 1 && CurChar() == ',')
	{
		// terminate current arg
		if (mCurArgStartPos)
		{
			WTString sym(mCurArgStartPos, ptr_sub__int(CurPos(), mCurArgStartPos));
			sym = ::StripCommentsAndStrings(GetLangType(), sym, false);
			sym.Trim();
			if (sym.IsEmpty())
				mIsDone = true;
			else
				mArgs.push_back(sym);
		}

		// start new arg
		mCurArgStartPos = CurPos() + 1;
	}

	__super::OnChar();
}

BOOL ArgsSplitter::IsDone()
{
	if (mIsDone)
		return true;
	return __super::IsDone();
}

WTString TextStripper::StripText(const WTString& buf)
{
	DEFTIMERNOTE(VAP_TextStripper, NULL);
	m_maxLen = buf.GetLength();
	mCleanText.PreAllocBuffer(m_maxLen);
	mLastChar = '\0';
	mAcceptLineContinuation = Is_VB_VBS_File(GetFType());
	ParseTo(buf.c_str(), m_maxLen, "", m_maxLen);

	if (mFoundContinuedLine && mCollapseWhitespace)
	{
		// [case: 60988]
		// I think this was intended for single statement buf input (not a whole file or class)
		mCleanText.ReplaceAll("\r\n", " ");
		mCleanText.ReplaceAll("\r", " ");
		mCleanText.ReplaceAll("\n", " ");
	}

	return mCleanText;
}

void TextStripper::OnChar()
{
	char c = CurChar();

	if (InComment())
	{
		const char cmtType = CommentType();
		switch (cmtType)
		{
		case '\'':
		case '"':
		case '{':
			if (!mKeepStrings)
				return;

			if ('$' == State().mStringLiteralMainType || 'C' == State().mStringLiteralMainType)
			{
				if (('{' != cmtType && '{' == c && '{' == NextChar()) ||
				    ('{' == cmtType && '}' == c && '}' == NextChar()))
				{
					mCleanText += c;
					mLastChar = c;
				}
			}
			break;
		case '#':
			if (!mKeepDirectives)
				return;
			break;
		case '\n':
		case '*':
		case '-':
			// line and block comments are always stripped
			return;
		default:
			_ASSERTE(!"unhandled comment type in TextStripper::OnChar");
			return;
		}
	}
	else if (!mKeepStrings && AnyBaseStateIsInComment())
	{
		// [case: 98335]
		return;
	}
	else
	{
		if (!mKeepDirectives && '#' == c)
			return; // start of directive

		if ('\'' == c && IsAtDigitSeparator())
		{
			// [case: 86379]
			// not starting a char literal
		}
		else if (!mKeepStrings && ('\'' == c || '"' == c))
			return; // start of string
	}

	if (wt_isspace(c))
	{
		if ('\r' == c || '\n' == c)
			; // handled via OnEveryChar
		else if (!mCollapseWhitespace)
			mCleanText += c;
		else if (mCleanText.GetLength() && !wt_isspace(mLastChar))
			mCleanText += ' ';
	}
	else
		mCleanText += c;

	mLastChar = c;
}

void TextStripper::OnComment(char c, int altOffset /*= UINT_MAX*/)
{
	__super::OnComment(c, altOffset);
	if (c && mKeepStrings && InComment())
	{
		const char cmtType = CommentType();
		_ASSERTE(cmtType != 'L' && cmtType != 'S' && cmtType != '@' && cmtType != '$' && cmtType != 'C');
		if (cmtType == '\'' || cmtType == '"' || cmtType == '{')
		{
			if (altOffset != UINT_MAX)
			{
				_ASSERTE((altOffset + m_cp) < mBufLen);
				for (int idx = 0; idx < altOffset; ++idx)
					mCleanText += m_buf[idx + m_cp];
			}

			mCleanText += c;
			mLastChar = c;
		}
	}
}

void TextStripper::IncCP()
{
	bool keepLines = !mCollapseWhitespace;
	if (!keepLines && InComment())
	{
		const char cmtType = CommentType();
		switch (cmtType)
		{
		case '\'':
		case '"':
		case '{':
			if (mKeepStrings)
				keepLines = true;
			break;
		case '#':
			if (mKeepDirectives)
				keepLines = true;
			break;
		case '\n':
		case '*':
		case '-':
			break;
		default:
			_ASSERTE(!"unhandled comment type in TextStripper::IncCP");
		}
	}
	else if (!keepLines && mKeepStrings && AnyBaseStateIsInComment())
	{
		// [case: 98335]
		keepLines = true;
	}

	if (keepLines)
	{
		const char c = m_buf[m_cp];
		if ('\r' == c || '\n' == c)
		{
			mCleanText += c;
			mLastChar = c;
		}
	}

	__super::IncCP();
}

class DeclPosCls : public VAParseMPMacroC
{
  public:
	WTString m_newSymScope;
	WTString m_scope;
	ULONG m_LastDefLine;
	ULONG m_LastDefLineOfType;
	ULONG m_LastDefLineOfSym;
	ULONG m_LastDefBeforeThisSym;
	UINT m_matchType;
	int m_midlinePos;
	BOOL m_inAfxMsg;

	ULONG GetDeclLine(const WTString& buf, const WTString& newSymScope, UINT type, MultiParsePtr mp = nullptr)
	{
		m_inAfxMsg = FALSE;
		m_midlinePos = 0;
		m_matchType = type;
		_ASSERTE(m_matchType == (type & TYPEMASK) || type == BEFORE_SYMDEF_FLAG);
		m_newSymScope = newSymScope;
		m_scope = StrGetSymScope(newSymScope);
		if (!m_scope.GetLength())
			m_scope = DB_SEP_CHR;
		m_LastDefLine = m_LastDefLineOfType = m_LastDefLineOfSym = m_LastDefBeforeThisSym = UINT_MAX;

		m_processMacros = TRUE;
		EdCntPtr curEd = g_currentEdCnt;
		m_mp = mp ? mp : (curEd ? curEd->GetParseDb() : nullptr);
		Init(buf);
		DoParse();

		if (m_midlinePos)
			return 0;
		if (m_LastDefBeforeThisSym != -1 && type == BEFORE_SYMDEF_FLAG)
			return m_LastDefBeforeThisSym;
		if (m_LastDefLineOfSym != -1)
			return m_LastDefLineOfSym;
		if (m_LastDefLineOfType != -1)
			return m_LastDefLineOfType;
		if (m_LastDefLine != -1)
			return m_LastDefLine;
		_ASSERTE(!"DeclPosCls::GetDeclLine");
		return m_curLine;
	}

	virtual void OnDef()
	{
		if (m_deep && State(m_deep - 1).m_lastChar == '<')
			return; // don't land in <> of a template
		if (!InLocalScope() && Scope() == m_scope)
		{
			WTString symscope = MethScope();
			ULONG ln = (CurChar() == '}') ? m_curLine : m_curLine + 1;
			if (symscope == m_newSymScope) // Found exact definition
			{
				m_LastDefLineOfSym = ln;
				// [case: 1156] extract method doesn't place method declaration before caller
				// if caller is first function in file - use line 1 if no directives
				// found before the calling function
				if (-1 == m_LastDefLine)
				{
					// [case: 22670]
					if (!m_LastDefBeforeThisSym || -1 == m_LastDefBeforeThisSym)
						m_LastDefBeforeThisSym = 1;
				}
				else
				{
					if (m_LastDefLine > m_LastDefBeforeThisSym || -1 == m_LastDefBeforeThisSym)
						m_LastDefBeforeThisSym = m_LastDefLine;
				}
			}
			if (!m_inAfxMsg)
			{
				if (State().m_defType == m_matchType && CurChar() != '}')
					m_LastDefLineOfType = ln;
				m_LastDefLine = ln;
			}
			m_midlinePos = 0;
		}
	}

	virtual void OnDirective()
	{
		VAParseMPMacroC::OnDirective();
		if (InLocalScope(m_deep))
			return;

		// [case: 1156] extract method doesn't place method declaration before caller
		// if caller is first function in file - record last directive (like include)
		if (-1 == m_LastDefBeforeThisSym || -1 == m_LastDefLineOfSym || -1 == m_LastDefLine)
		{
			ULONG ln = (CurChar() == '}') ? m_curLine : m_curLine + 1;
			// [case: 22670] don't change m_LastDefLine
			m_LastDefBeforeThisSym = ln;
		}
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		if (m_deep && State(m_deep - 1).m_lastChar == '<')
		{
			// don't land in <> of a template
			VAParseDirectiveC::DecDeep();
			return;
		}

		bool didMidlinePos = false;
		if (m_LastDefLine != -1)
		{
			if (m_LastDefLine > m_curLine)
			{
				m_midlinePos = m_cp;
				didMidlinePos = true;
			}
			else if (m_midlinePos)
				m_midlinePos = 0;
		}

		if (!InComment())
		{
			if (m_LastDefLine == -1 && CurChar() == '}' && Scope(m_deep) == m_scope)
				m_midlinePos = m_cp;

			if (!strchr(")]>", CurChar()) && m_deep && Scope(m_deep - 1) == m_scope)
			{
				// Found implementation
				if (m_LastDefLine != -1 && didMidlinePos && CurChar() == '}' && m_LastDefLine == (m_curLine + 1))
				{
					// clear midlinePos for:
					// void Foo() { }
					m_midlinePos = 0;
				}
				m_LastDefLine = m_curLine + 1;
				WTString symscope = TokenGetField(Scope(), "-");
				if (symscope == m_newSymScope) // Found exact implementation
				{
					m_LastDefLineOfSym = m_curLine + 1;
					if (m_LastDefBeforeThisSym == -1)
						m_LastDefBeforeThisSym = m_LastDefLine;
				}
				if (!m_inAfxMsg && m_deep && State(m_deep - 1).m_defType == m_matchType)
					m_LastDefLineOfType = m_curLine + 1;
			}
		}
		VAParseDirectiveC::DecDeep();
	}

	BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		if (CurChar() == '/')
		{
			// do not add members to vc6 message map code, Case 1300.
			// However, Add Similar Method still can add there.
			if (strncmp(CurPos(), "//{{AFX_", 8) == 0)
				m_inAfxMsg = TRUE;
			if (strncmp(CurPos(), "//}}AFX_", 8) == 0)
				m_inAfxMsg = FALSE;
		}
		return FALSE;
	}

  protected:
	DeclPosCls(int fType)
	    : VAParseMPMacroC(fType)
	{
	}
};
CREATE_MLC(DeclPosCls, DeclPosCls_MLC);

class ImplementationPosCls : public VAParseMPMacroC
{
  private:
	WTString mScopeOfImplScope;
	WTString mPartialScopeOfImplScope; // truncated version of mScopeOfImplScope (only the final scope (without sym))
	WTString mPartialImplScope;        // truncated version of m_implScope (only the final scope + sym)
	ULONG mGuessInsertLine;            // guess based on match of mPartialScopeOfImplScope or mPartialImplScope
	int mScopeMatchLen = 0;
	bool mLockGuess;

  protected:
	int m_matchRank;

  public:
	WTString m_implScope;
	WTString m_insertLineScope; // namespace scope of place we are adding the impl, so we can strip off all the
	                            // un-needed ns::ns::method()
	ULONG m_insertLine;

	ULONG GetImplLine(const WTString& buf, LPCSTR clsScope)
	{
		m_implScope = clsScope;
		mScopeOfImplScope = ::StrGetSymScope(m_implScope);
		mPartialScopeOfImplScope = ::StrGetSym(mScopeOfImplScope);
		if (!mPartialScopeOfImplScope.IsEmpty() && mPartialScopeOfImplScope[0] != DB_SEP_CHR)
		{
			mPartialScopeOfImplScope.prepend(DB_SEP_STR.c_str());
			mPartialImplScope = mPartialScopeOfImplScope + DB_SEP_STR + ::StrGetSym(m_implScope);
		}
		m_matchRank = 0;
		m_insertLine = mGuessInsertLine = UINT_MAX;
		mLockGuess = false;

		m_processMacros = TRUE;
		EdCntPtr curEd = g_currentEdCnt;
		m_mp = curEd ? curEd->GetParseDb() : nullptr;
		Init(buf);
		DoParse();
		if (m_insertLine != UINT_MAX)
			return m_insertLine;
		if (mGuessInsertLine != UINT_MAX)
			return mGuessInsertLine;
		return m_curLine + 1;
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		VAParseDirectiveC::DecDeep();
		if (CurChar() == '}' && (!m_deep || (!InLocalScope(m_deep) && !InClassScope(m_deep))))
		{
			int rank = 0;
			WTString preScope = TokenGetField(Scope(m_deep + 1), "-");
			WTString postScope = TokenGetField(Scope(), "-");

			if (preScope == m_implScope)
			{
				// full match, must be overriding method, put this one after it
				rank = 999;
			}
			else if (postScope == mScopeOfImplScope)
			{
				// scope, must be overriding method, put this one after it
				rank = 998;
			}
			else if (mScopeOfImplScope == StrGetSymScope(preScope))
			{
				// def of another method of same class
				rank = 997;
			}
			else if ((!m_matchRank || (m_matchRank && mScopeMatchLen && mScopeMatchLen < preScope.GetLength())) &&
			         StartsWith(m_implScope, preScope))
			{
				mScopeMatchLen = preScope.GetLength(); // [case: 140771]
				ULONG d = State().m_defType;
				if (d == CLASS || d == STRUCT || d == C_INTERFACE)
				{
					// end-of-class.  put the impl after class decl.
					m_insertLine = m_curLine + 1;
					m_matchRank = 996;
					m_insertLineScope = postScope;
				}
				else
				{
					// empty namespace, put it here if nothing better comes along
					m_insertLine = m_curLine;
					m_matchRank = 996;
					m_insertLineScope = preScope;
				}
			}
			else if (!mPartialImplScope.IsEmpty() && StartsWith(preScope, mPartialImplScope))
			{
				// [case: 20644]
				// overload of method in class
				mGuessInsertLine = m_curLine + 1;
				mLockGuess = true;
			}
			else if (!mLockGuess && !mPartialScopeOfImplScope.IsEmpty() &&
			         StartsWith(preScope, mPartialScopeOfImplScope))
			{
				// [case: 20644]
				// method in class
				mGuessInsertLine = m_curLine + 1;
			}

			if (rank && rank >= m_matchRank)
			{
				m_insertLine = m_curLine + 1;
				m_matchRank = rank;
				m_insertLineScope = postScope;
			}
		}
	}

  protected:
	ImplementationPosCls(int fType)
	    : VAParseMPMacroC(fType)
	{
	}
};
CREATE_MLC(ImplementationPosCls, ImplementationPosCls_MLC);

class TemplateImplementationPos : public ImplementationPosCls
{
  protected:
	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		// skip ImplementationPosCls::DecDeep()
		VAParseDirectiveC::DecDeep();
		if (CurChar() == '}' && (!m_deep || !InLocalScope(m_deep - 1)))
		{
			const WTString preScope(::TokenGetField(Scope(m_deep + 1), "-"));
			const WTString postScope(::TokenGetField(Scope(), "-"));

			if (!m_matchRank && ::StartsWith(m_implScope, preScope))
			{
				m_insertLine = m_curLine + 1;
				m_matchRank = 996;
				m_insertLineScope = preScope;
			}
		}
	}

  protected:
	TemplateImplementationPos(int fType)
	    : ImplementationPosCls(fType)
	{
	}
};
CREATE_MLC(TemplateImplementationPos, TemplateImplementationPos_MLC);

int GetDeclPos(const WTString& scope, const CStringW& filename, UINT type, int& midLinePos,
               MultiParsePtr mp /*= nullptr*/)
{
	_ASSERTE((type & TYPEMASK) == type || type == BEFORE_SYMDEF_FLAG);
	DeclPosCls_MLC cp2(GetFileType(filename));
	WTString buf = GetFileText(filename);
	_ASSERTE(g_currentEdCnt || mp);
	int ln = (int)cp2->GetDeclLine(buf, scope, type, mp);
	midLinePos = cp2->m_midlinePos;
	return ln;
}

BOOL GotoDeclPos(const WTString& scope, const CStringW& filename, UINT type /*= FUNC*/)
{
	_ASSERTE((type & TYPEMASK) == type || type == BEFORE_SYMDEF_FLAG);
	int midLinePos;
	int ln = GetDeclPos(scope, filename, type, midLinePos);
	if (midLinePos) // mid line entry? {int i;}
		return DelayFileOpenPos(filename, (uint)midLinePos).get() != 0;

	return DelayFileOpen(filename, ln).get() != 0;
}

CREATE_MLC_IMPL(VAScopeInfo, VAScopeInfo_MLC);

void GetGeneratedPropName(WTString& generatedPropertyName)
{
	if (generatedPropertyName.GetLength())
	{
		// updated rules for case: 17714
		if (generatedPropertyName.GetLength() > 2 && ::wt_isalpha(generatedPropertyName[0]) &&
		    ::wt_islower(generatedPropertyName[0]) && ::wt_isalpha(generatedPropertyName[1]) &&
		    ::wt_isupper(generatedPropertyName[1]))
		{
			// mIntercap -> strip leading m that is followed by uppercase letter
			// sIntercap (static member)
			// xIntercap -> strip any leading lowercase letter that is immediately followed by upper
			generatedPropertyName = generatedPropertyName.Mid(1);
		}
		else if (generatedPropertyName.GetLength() > 2 && ::wt_isalpha(generatedPropertyName[0]) &&
		         ::wt_islower(generatedPropertyName[0]) && generatedPropertyName[1] == '_' &&
		         ::wt_isalpha(generatedPropertyName[2]))
		{
			// m_pPointer -> strip leading m_x (where x is a single lowercase letter)
			// that is followed by an uppercase letter
			// also s_nInt (static int member)
			if (generatedPropertyName.GetLength() > 3 && ::wt_islower(generatedPropertyName[2]) &&
			    ::wt_isupper(generatedPropertyName[3]))
				generatedPropertyName = generatedPropertyName.Mid(3);
			// m_xxUpper -> strip leading m_xx (where xx is two lowercase letters)
			else if (generatedPropertyName.GetLength() > 4 && ::wt_islower(generatedPropertyName[2]) &&
			         ::wt_islower(generatedPropertyName[3]) && ::wt_isupper(generatedPropertyName[4]))
				generatedPropertyName = generatedPropertyName.Mid(4);
			// m_strUpper -> strip leading m_str
			else if (generatedPropertyName.GetLength() > 5 && generatedPropertyName[2] == 's' &&
			         generatedPropertyName[3] == 't' && generatedPropertyName[4] == 'r' &&
			         ::wt_isupper(generatedPropertyName[5]))
				generatedPropertyName = generatedPropertyName.Mid(5);
			// m_cchUpper -> strip leading m_cch
			else if (generatedPropertyName.GetLength() > 5 && generatedPropertyName[2] == 'c' &&
			         generatedPropertyName[3] == 'c' && generatedPropertyName[4] == 'h' &&
			         ::wt_isupper(generatedPropertyName[5]))
				generatedPropertyName = generatedPropertyName.Mid(5);
			// m_pszUpper -> strip leading m_psz
			else if (generatedPropertyName.GetLength() > 5 && generatedPropertyName[2] == 'p' &&
			         generatedPropertyName[3] == 's' && generatedPropertyName[4] == 'z' &&
			         ::wt_isupper(generatedPropertyName[5]))
				generatedPropertyName = generatedPropertyName.Mid(5);
			// m_lpszUpper -> strip leading m_lpsz
			else if (generatedPropertyName.GetLength() > 6 && generatedPropertyName[2] == 'l' &&
			         generatedPropertyName[3] == 'p' && generatedPropertyName[4] == 's' &&
			         generatedPropertyName[5] == 'z' && ::wt_isupper(generatedPropertyName[6]))
				generatedPropertyName = generatedPropertyName.Mid(6);
			// m_any -> strip leading m_
			else
				generatedPropertyName = generatedPropertyName.Mid(2);
		}

		// _leadingUnderscore -> strip leading _
		while (generatedPropertyName.GetLength() > 1 && generatedPropertyName[0] == '_')
			generatedPropertyName = generatedPropertyName.Mid(1);

		// trailingUnderscore_ -> strip trailing _
		while (generatedPropertyName.GetLength() > 1 &&
		       generatedPropertyName[generatedPropertyName.GetLength() - 1] == '_')
			generatedPropertyName = generatedPropertyName.Left(generatedPropertyName.GetLength() - 1);

		if (!(generatedPropertyName[0] & 0x80))
		{
			CStringW tmp(generatedPropertyName.Wide());
			tmp.SetAt(0, ::towupper((unsigned short)generatedPropertyName[0]));
			generatedPropertyName = tmp;
		}
	}
}

void VAScopeInfo::GeneratePropertyName()
{
	mGeneratedPropertyName = mCurSymName = ::DecodeScope(::StrGetSym(mCurSymScope));
	GetGeneratedPropName(mGeneratedPropertyName);

	if (mGeneratedPropertyName == mCurSymName)
		mGeneratedPropertyName = "$PropertyName$"; // Make it prompt for a name...
}

void VAScopeInfo::ParseEnvArgs()
{
	WTString methTxt;
	WTString line = GetLineStr();
	WTString scope = StrGetSymScope(MethScope());

	EdCntPtr curEd = g_currentEdCnt;
	MultiParsePtr mp = curEd ? curEd->GetParseDb() : nullptr;
	mData = (curEd && curEd->GetSymScope().c_str()) ? mp->FindExact2(curEd->GetSymScope()) : NULL;
	if (mData)
	{
		WTString def = DecodeScope(mData->Def());
		if (!line.GetLength() /* || !def.contains(line)*/) // Use current line, Some template in def get changed around
			line = TokenGetField(def, "\f");               // get first definition from db
		scope = StrGetSymScope(mData->SymScope());
	}
	if (scope.GetLength())
		mScopeStr = WTString(scope) + DB_SEP_CHR;
	// handle preprocessor macros
	line.ReplaceAll("virtual", "", TRUE);
	line.ReplaceAll("PURE", "", TRUE);
	line.TrimLeft();

	if (strncmp(line.c_str(), "STDMETHOD(", 10) == 0)
	{
		// change STDMETHOD(foo)(int arg); to STDMETHODIMP foo(int arg);
		token2 t = line;
		t.read('(');
		WTString meth = t.read("()");
		WTString args = t.read(')');
		line = WTString("STDMETHODIMP ") + meth + args + ")";
		mMethodBody = "return S_OK;";
	}
	else if (strncmp(line.c_str(), "STDMETHOD_(", 11) == 0)
	{
		// [case: 58329] create implementation of STDMETHOD_(res, Foo)
		// change STDMETHOD_(res, Foo)(int arg); to STDMETHODIMP_(res) Foo(int arg);
		token2 t = line;
		t.read('(');
		WTString retType = t.read(',');
		WTString meth = t.read(')');
		meth.TrimLeft();
		if (meth.GetLength() && meth[0] == ',')
			meth = meth.Mid(1);
		meth.Trim();
		WTString args = t.read(')');
		line = WTString("STDMETHODIMP_") + retType + ") " + meth + args + ")";
	}
	else if (strncmp(line.c_str(), "STDMETHODIMP_(", 14) == 0)
	{
		// [case: 45936] move implementation of STDMETHODIMP_(res)
		// change STDMETHODIMP_(res)Foo(int arg) to STDMETHOD_(res,Foo)(int arg);
		int pos = line.find(')');
		if (-1 != pos)
			line.SetAt(pos, ',');
		pos = line.Find("(", pos);
		if (-1 != pos)
		{
			const WTString begin(line.Left(pos));
			const WTString end(line.Mid(pos));
			line = begin + ")" + end;
		}
		line.ReplaceAll("STDMETHODIMP_", "STDMETHOD_");
	}

	line = ParseLineClass::SingleLineFormat(GetLangType(), line, TRUE, line.GetLength()); // comment (arg /* = 3*/);
	if (mScopeStr.GetLength() > 1)
	{
		mScopeStr.ReplaceAll(":", "::");
		if (mScopeStr.GetLength() > 1)
			mScopeStr = mScopeStr.Mid(2);
		// [case: 73856] don't replace member function pointers
		const WTString memberFnPtr(mScopeStr + "*");
		line.ReplaceAll(memberFnPtr, "\1");
		line.ReplaceAll(mScopeStr, "");
		line.ReplaceAll("\1", memberFnPtr);
	}
	line.ReplaceAll("{...}", "");
	if (Is_VB_VBS_File(GetLangType()))
	{
		token2 t = line;
		t.read(')'); // strip off args so "As" in arg list does not get mistaken as a return val
		LPCSTR type = strstrWholeWord(t.Str(), "As");
		if (!type)
			type = strstrWholeWord(line, "As");
		if (type)
			mCurSymType = type + 2;
		mCurSymType.TrimLeft();
	}
	else
	{
		_ASSERTE(State().m_lastScopePos);
		if (State().m_lastScopePos)
		{
			WTString tmplateArg = GetTemplateStr();
			mCurSymType = GetSubStr(State().m_begLinePos, State().m_lastScopePos);

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			if (mCurSymType.IsEmpty())
			{
				if (mData && mData->type() == PROPERTY && State().m_lastScopePos == State().m_begLinePos)
				{
					const WTString dtDef(mData->Def());
					if (dtDef.contains("__property"))
					{
						// read and write in __property declaration breaks State().
						// pull type from the declaration
						mCurSymType = GetTypeFromDef(dtDef, GetLangType());
					}
				}
			}
#endif

			if (tmplateArg.GetLength())
				mCurSymType.ReplaceAll(tmplateArg, "");
			if ((State().m_defAttr & V_POINTER) && !mCurSymType.contains("*") && !mCurSymType.contains("^"))
			{
				mCurSymType += "*";
			}

			if (mCurSymType == "STDMETHOD(")
				mCurSymType = "STDMETHODIMP";
			else if (0 == mCurSymType.Find("STDMETHOD_("))
			{
				// [case: 58329] create implementation of STDMETHOD_(res, Foo)
				mCurSymType = GetSubStr(State().m_begLinePos, State().m_lastScopePos);
				int commaPos = mCurSymType.Find(',');
				if (-1 == mCurSymType.Find("STDMETHOD_(") || -1 == commaPos)
					mCurSymType.Empty();
				else
				{
					mCurSymType.ReplaceAt(commaPos, 1, ")");
					mCurSymType.TrimRight();
					mCurSymType.ReplaceAll("STDMETHOD_(", "STDMETHODIMP_(");
					int pos = line.Find(')');
					if (-1 != pos)
						line = line.Mid(pos);
				}
			}
			else if (mCurSymType.IsEmpty())
			{
				// [case: 45936] move implementation of STDMETHODIMP_(res)
				mCurSymType = GetSubStr(State().m_begLinePos, State().m_lastWordPos);
				if (-1 == mCurSymType.Find("STDMETHOD"))
					mCurSymType.Empty();
				else
				{
					mCurSymType.TrimRight();
					int pos = mCurSymType.Find(')');
					if (-1 != pos)
						mCurSymType = mCurSymType.Left(pos + 1);
					pos = line.Find(')');
					if (-1 != pos)
						line = line.Mid(pos);
				}
			}
		}
	}

	ParseMethodQualifier(line);

	mCurSymType.ReplaceAll("private", "", TRUE);
	mCurSymType.ReplaceAll("public", "", TRUE);
	mCurSymType.ReplaceAll("protected", "", TRUE);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	mCurSymType.ReplaceAll("__published", "", TRUE);
	mCurSymType.ReplaceAll("__classmethod", "", TRUE);
	mCurSymType.ReplaceAll("__declspec(dynamic)", "", TRUE);
	mCurSymType.ReplaceAll("DYNAMIC", "", TRUE);
#endif
	mCurSymType.ReplaceAll("internal", "", TRUE);
	mCurSymType.ReplaceAll("static", "", TRUE);
	mCurSymType.ReplaceAll("thread_local", "", TRUE);
	mCurSymType.ReplaceAll("tile_static", "", TRUE);
	mCurSymType.ReplaceAll("explicit", "", TRUE);
	mCurSymType.ReplaceAll("inline", "", TRUE);
	mCurSymType.ReplaceAll("__forceinline", "", TRUE);
	mCurSymType.ReplaceAll("__inline", "", TRUE);
	mCurSymType.ReplaceAll("_inline", "", TRUE);
	mCurSymType.ReplaceAll("AFX_INLINE", "", TRUE);
	mCurSymType.ReplaceAll("_AFX_INLINE", "", TRUE);
	mCurSymType.ReplaceAll("_AFX_PUBLIC_INLINE", "", TRUE);
	mCurSymType.ReplaceAll("virtual", "", TRUE);
	mCurSymType.ReplaceAll("afx_msg", "", TRUE);
	while (mCurSymType.ReplaceAll("  ", " ", FALSE))
		;
	mCurSymType.Trim();

	mCurScope = CleanScopeForDisplay(curEd->m_lastScope);
	mCurSymScope = curEd->GetSymScope();
	if (!mCurSymScope.GetLength())
		mCurSymScope = MethScope();

	GeneratePropertyName();
}

LPCSTR FindFunctionParamsStart(const WTString& line)
{
	int openAngleBrackets = 0; // Count of unclosed '<' brackets
	CommentSkipper cs(Src);
	for (int i = 0; i < line.GetLength(); ++i)
	{
		TCHAR c = line[i]; // Use TCHAR for character operations
		if (cs.IsComment(c))
			continue;

		if (c == _T('<'))
		{
			++openAngleBrackets;
		}
		else if (c == _T('>'))
		{
			--openAngleBrackets;
		}
		else if (c == _T('(') && openAngleBrackets == 0)
		{
			// Found the '(' only when there are no unclosed '<' brackets
			return line.c_str() + i;
		}
	}

	return nullptr; // If no suitable '(' was found
}

void VAScopeInfo::ParseMethodQualifier(WTString& line)
{
	LPCSTR paramPos = FindFunctionParamsStart(line);
	if (paramPos)
	{
		LPCTSTR startPos = line.c_str();
		ParseToCls ptc(GetLangType());
	again:
		const int len = line.GetLength() - ptr_sub__int(paramPos + 1, startPos);
		ptc.ParseTo(paramPos + 1, len, ")", len);
		const WTString paramStr(paramPos + 1, ptc.GetCp());
		SetParamString(paramStr);
		LPCSTR qualPos = ptc.CurPos();
		if (qualPos[0] == ')')
			qualPos++;
		while (strchr(" \t", qualPos[0]))
			qualPos++; // eat whitespace
		if (qualPos[0] == '(' && strstrWholeWord(line, "operator"))
		{
			// "operator()(int i);" // ignore() and set it to (int i)
			paramPos = qualPos;
			startPos = ptc.GetBuf();
			goto again;
		}
		mMethodQualifier = TokenGetField(qualPos, "{;\r\n");
		mMethodQualifier.TrimLeft();
	}
}

static WTString CleanupParamList(const WTString& params)
{
	WTString retval(params);
	for (;;)
	{
		int commentStart = retval.Find("/*");
		if (commentStart != -1)
		{
			int commentEnd = retval.Find("*/");
			if (commentEnd != -1)
			{
				WTString tmp(retval.Left(commentStart));
				if (commentEnd + 2 < retval.GetLength())
					tmp += retval.Mid(commentEnd + 2);
				retval = tmp;
				continue;
			}
		}
		break;
	}

	retval.ReplaceAll("&", " & ");
	retval.ReplaceAll("*", " * ");
	retval.ReplaceAll("^", " ^ ");
	retval.ReplaceAll("  ", " ");
	retval.ReplaceAll(" ,", ",");
	retval.ReplaceAll(", ", ",");
	retval.Trim();

	return retval;
}

void InsertNewLinesBeforeAfterIfNeeded(WTString& impText, const WTString& fileBuf)
{
	EdCntPtr curEd = g_currentEdCnt;

	// trim empty lines before
	for (int i = 0; i < impText.GetLength(); i++)
	{
		if (impText[i] == '\n')
		{
			impText.ReplaceAt(i, 1, "");
			i--;
		}
		else
		{
			break;
		}
	}

	// trim empty lines after
	for (int i = impText.GetLength() - 1; i >= 1; i--)
	{
		if (impText[i] == '\n' && impText[i - 1] == '\n') // we need to leave 1 newline
		{
			impText.ReplaceAt(i, 1, "");
		}
		else
		{
			break;
		}
	}

	// Get empty lines before the caret
	int emptyLinesBefore = 0;
	long curPos = curEd->GetBufIndex((int)curEd->CurPos());
#ifdef RAD_STUDIO
	if(curPos > fileBuf.GetLength())
		curPos = fileBuf.GetLength(); // just in case
#endif
	for (int i = curPos - 1; i >= 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (c == 13 || c == 10)
		{
			if (fileBuf[i - 1] == 10 || fileBuf[i - 1] == 13)
				i--;
			emptyLinesBefore++;
			continue;
		}
		if (!IsWSorContinuation(c))
			break;
	}

	if (emptyLinesBefore)
		emptyLinesBefore--;

	// Get empty lines after the caret
	int emptyLinesAfter = 0;
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (c == 13 || c == 10)
		{
			if (fileBuf[i + 1] == 10 || fileBuf[i + 1] == 13)
				i++;
			emptyLinesAfter++;
			continue;
		}
		if (!IsWSorContinuation(c))
			break;
	}

	if (emptyLinesAfter)
		emptyLinesAfter--;

	int between = (int)Psettings->mLinesBetweenMethods;
	if (between > 10)
		between = 10; // to avoid that an unintentionally entered way too large number causing problem
	if (between < 0)
		between = 0;

	if (emptyLinesAfter - (between - 1) <= 0)
	{
		WTString newLinesAfter;
		for (int i = 0; i < between - emptyLinesAfter; i++)
			newLinesAfter += EolTypes::GetEolStr(impText);

		impText = impText + newLinesAfter;
	}

	if (emptyLinesBefore - (between - 1) <= 0)
	{
		WTString newLinesBefore;
		for (int i = 0; i < between - emptyLinesBefore; i++)
			newLinesBefore += EolTypes::GetEolStr(impText);

		impText = newLinesBefore + impText;
	}
}

void CollectDefaultValues(const WTString& methodSignature, std::vector<WTString>& defaultValues, int fileType)
{
	int startPoint = FindInCode(methodSignature, '(', fileType);
	ASSERT(startPoint != -1);
	if (startPoint == -1)
		return;

	int parens = 1;
	int angleBrackets = 0;
	bool waitingForNextParam = true;
	bool collecting = false;
	CommentSkipper cs(fileType);
	for (int i = startPoint + 1; i < methodSignature.GetLength(); i++)
	{
		TCHAR c = methodSignature[i];
		if (cs.IsCode(c) || collecting)
		{
			if (c == '(')
			{
				parens++;
				if (!collecting)
					continue;
			}
			if (c == ')')
			{
				parens--;
				if (parens == 0)
				{
					break;
				}
				else
				{
					if (!collecting)
						continue;
				}
			}
			if (c == '<')
			{
				angleBrackets++;
				if (!collecting)
					continue;
			}
			if (c == '>' && angleBrackets)
			{
				angleBrackets--;
				if (!collecting)
					continue;
			}
			if (waitingForNextParam && !IsWSorContinuation(c) && c != '/')
			{
				defaultValues.push_back("");
				waitingForNextParam = false;
			}
			if (parens == 1 && angleBrackets == 0 && c == '=')
			{
				WTString& back = defaultValues.back();
				back += " =";
				collecting = true;
				continue;
			}
			if (parens == 1 && angleBrackets == 0 && c == ',')
			{
				waitingForNextParam = true;
				collecting = false;
				continue;
			}
			if (collecting)
			{
				WTString& back = defaultValues.back();
				back += c;
			}
		}
	}
}

void InjectDefaultValues(WTString& paramString, const std::vector<WTString>& defaultValues, int fileType)
{
	WTString paramStringToClean = paramString;
	paramString = "";
	CommentSkipper cs(fileType);
	for (int i = 0; i < paramStringToClean.GetLength(); i++)
	{
		TCHAR c = paramStringToClean[i];
		if (cs.IsCode(c) && c != '/')
		{
			paramString += c;
		}
	}

	int parens = 1;
	int angleBrackets = 0;
	uint paramIndex = 0;
	cs.Reset();
	for (int i = 0; i < paramString.GetLength(); i++)
	{
		TCHAR c = paramString[i];
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
				if (parens == 0)
					break;
				else
					continue;
			}
			if (c == '<')
			{
				angleBrackets++;
				continue;
			}
			if (c == '>' && angleBrackets)
			{
				angleBrackets--;
				continue;
			}
			if (parens == 1 && angleBrackets == 0 && c == ',')
			{ // inject place (comma)
				if (paramIndex < defaultValues.size())
				{
					const WTString& def = defaultValues[paramIndex];
					if (!def.IsEmpty())
					{ // do we have a def value for this param to inject?
						paramString.insert(i, def.c_str());
						i += def.GetLength();
					}
				}

				paramIndex++; // prepare for the next param
				continue;
			}
		}
	}

	// inject place (end of params)
	if (paramIndex < defaultValues.size())
	{
		const WTString& def = defaultValues[paramIndex];
		if (!def.IsEmpty()) // do we have a def value for this param to inject?
			paramString.insert(paramString.GetLength(), def.c_str());
	}
}

void RemoveNamespaceScopesExposedViaUsings(MultiParse* mp, WTString& scope)
{
	_ASSERTE(mp);
	DTypeList dts;

	// get file global usings
	_ASSERTE(mp->GetFileID());
	WTString fileScopedUsingLookup;
	fileScopedUsingLookup.WTFormat(":wtUsingNamespace_%x", mp->GetFileID());
	mp->FindExactList(fileScopedUsingLookup, dts, false, false);

	// get file local usings
	WTString localUsingLookup("wtUsingNamespace");
	mp->FindExactList(WTHashKey(localUsingLookup), (uint)-1, dts, false, false);

	dts.GetStrs();
	for (auto& dt : dts)
	{
		if (dt.IsHideFromUser() && dt.type() == RESWORD && dt.Attributes() & V_IDX_FLAG_USING)
		{
			WTString ns = dt.Def();
			if (ns.GetLength())
			{
				WTString searchFor = ns + ":";
				if (int at = scope.Find(searchFor); at != -1)
					scope.ReplaceAt(at, ns.GetLength(), "");
			}
		}
	}
}

WTString VAScopeInfo::ExpandMacros(WTString expText, bool addTemplateDeclaration, bool inlineImplementation,
                                   bool replaceNamespaces /*= false*/)
{
	WTString tmpLnBrk;
	const WTString lnBrk(EolTypes::GetEolStr(expText));
	const CStringW lnBrkW(lnBrk.Wide());
	if (expText.contains("$MethodComment$"))
	{
		WTString commentStr(CommentStrFromDef());
		tmpLnBrk = EolTypes::GetEolStr(commentStr);
		if (tmpLnBrk != lnBrk)
			commentStr.ReplaceAll(tmpLnBrk, lnBrk);
		expText.ReplaceAll("$MethodComment$", commentStr);
	}
	expText.ReplaceAll("$CurScope$", mCurScope);

	if (expText.contains("$MethodArg$") || expText.contains("$MethodArgs$") || expText.contains("$MethodArgName$") ||
	    expText.contains("$MethodArgType$"))
	{
		const WTString theParamList(::CleanupParamList(mParamString));
		WTString newExp;
		WideStrVector lines;
		::WtStrSplitW(CStringW(expText.Wide()), lines, lnBrkW);
		for (WideStrVector::const_iterator it = lines.begin(); it != lines.end(); ++it)
		{
			CStringW ln(*it);
			bool multiLineReplace =
			    -1 != ln.Find(L"$MethodArg$") || -1 != ln.Find(L"$MethodArgName$") || -1 != ln.Find(L"$MethodArgType$");
			bool buildMethodArgs = -1 != ln.Find(L"$MethodArgs$");
			if (multiLineReplace || buildMethodArgs)
			{
				WTString methodArgs;
				token2 pList = theParamList;
				while (pList.more() > 2)
				{
					WTString arg = pList.read(",()");
					WTString argName;
					WTString argType;

					arg.TrimLeft();
					if (!arg.IsEmpty())
					{
						int pos = arg.ReverseFind(' ');
						if (pos == -1)
						{
							argType = arg;
						}
						else
						{
							argName = arg.Mid(pos + 1);
							argType = arg.Left(pos);
						}
					}

					WTString newln(ln);

					// [case: 20543] chop array syntax off name (foo[12])
					int pos = argName.Find('[');
					if (pos != -1)
						argName = argName.Mid(0, pos);

					if (multiLineReplace)
					{
						newln.ReplaceAll("$MethodArgName$", argName, TRUE);
						newln.ReplaceAll("$MethodArgType$", argType, TRUE);
						newln.ReplaceAll("$MethodArg$", arg, TRUE);
						newExp += newln;
					}
					if (buildMethodArgs)
					{
						methodArgs += argName;
						if (pList.more() > 2)
							methodArgs += ", ";
					}
				}
				if (buildMethodArgs)
				{
					// [case: 9863] allow symbol related snippet reserved words in refactoring snippets
					if (multiLineReplace)
					{
						newExp.ReplaceAll("$MethodArgs$", methodArgs, TRUE);
					}
					else
					{
						ln.Replace(L"$MethodArgs$", CStringW(methodArgs.Wide()));
						newExp += WTString(ln);
					}
				}
			}
			else
				newExp += WTString(ln);
		}
		expText = newExp;
	}

	WTString privileges, staticStr, constStr, virtualStr, volatileStr;
	WTString inlineImplPreString, inlineImplPostString;
	if (mData)
	{
		// attempt to locate declaration so that we get correct access/privileges (case 1244)
		DType* pData = ::FindDeclaration(*mData);
		_ASSERTE(pData);
		const WTString pDataDef(pData->Def());
		if (strstrWholeWord(pDataDef, "static"))
			staticStr = "static ";
		if (strstrWholeWord(pDataDef, "thread_local"))
			staticStr += "thread_local ";
		if (strstrWholeWord(pDataDef, "virtual"))
			virtualStr = "virtual ";
		if (strstrWholeWord(mMethodQualifier, "const"))
			constStr = "const ";
		else if (strstrWholeWord(mMethodQualifier, "CONST"))
			constStr = "CONST ";
		if (strstrWholeWord(mMethodQualifier, "volatile"))
			volatileStr = "volatile ";

		if (pData->IsProtected())
			privileges += "protected ";
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (pData->IsPublished())
			privileges += "__published ";
#endif
		if (pData->IsInternal())
			privileges += "internal ";
		if (pData->IsPrivate())
			privileges += "private ";

		if (privileges.IsEmpty())
			privileges = "public ";

		// keep method specifiers and qualifiers from the definition (case 10239)
		if (inlineImplementation)
		{
			WTString symDef = mAlterDef.IsEmpty() ? pDataDef : mAlterDef;
			if (mData && ::FindWholeWordInCode(symDef, "inline", GetFType(), 0) == -1 && IsFreeFunc(mData))
				inlineImplPreString = "inline " + inlineImplPreString;
			std::vector<WTString> defaultValues;
			::CollectDefaultValues(symDef, defaultValues, GetFType());
			::InjectDefaultValues(mParamString, defaultValues, GetFType());
			enum class eFindTypeState
			{
				WHITESPACE,
				TEMPLATE_PARAMS,
				SYM,
			};
			eFindTypeState findTypeState = eFindTypeState::WHITESPACE;
			int firstOpenParen = ::FindInCode(symDef, '(', GetFType());
			int angleBracketCounter = 0;
			int typeEndPos = 0;
			if (firstOpenParen != -1)
			{
				typeEndPos = ::FindWholeWordInCode(symDef, "operator", GetFType(), 0);
				if (typeEndPos == -1)
				{
					for (int i = firstOpenParen - 1; i >= 0; i--)
					{
						TCHAR c = symDef[i];
						switch (findTypeState)
						{
						case eFindTypeState::WHITESPACE:
							if (!IsWSorContinuation(c))
							{
								if (c == '>')
								{
									angleBracketCounter = 1;
									findTypeState = eFindTypeState::TEMPLATE_PARAMS;
									break;
								}
								findTypeState = eFindTypeState::SYM;
								ASSERT(ISCSYM(c) ||
								       c == ':'); // unexpected character (note that the for is going backwards)
							}
							break;
						case eFindTypeState::SYM:
							if (c == ':')
							{
								findTypeState = eFindTypeState::WHITESPACE;
								break;
							}
							if (ISCSYM(c))
								break;
							typeEndPos = i;
							goto endPosFound;
						case eFindTypeState::TEMPLATE_PARAMS:
							if (c == '<')
							{
								angleBracketCounter--;
								if (angleBracketCounter == 0)
								{
									findTypeState = eFindTypeState::WHITESPACE;
									break;
								}
							}
							break;
						}
					}
				}
			endPosFound:;
				inlineImplPreString += symDef.Left(typeEndPos);
				inlineImplPreString.TrimRight();
			}

			int lastCloseParen = symDef.ReverseFind(')');
			ASSERT(lastCloseParen != -1); // method declarations should always have a close paren
			if (lastCloseParen != -1)
			{
				if (lastCloseParen + 1 < symDef.GetLength())
				{
					inlineImplPostString = symDef.Mid(lastCloseParen + 1);
				}
				inlineImplPostString.ReplaceAll("{...}", "");
				if (mMethodQualifier.GetLength() && mMethodQualifier[0] == ':')
				{
					if (inlineImplPostString.Find("//") != -1)
						inlineImplPostString += lnBrk;
					inlineImplPostString += mMethodQualifier;
				}
				inlineImplPostString.TrimLeft();
			}
		}
	}

	// remove 2005/clr specifiers in implementations
	mMethodQualifier.ReplaceAll("override", "", TRUE);
	mMethodQualifier.ReplaceAll("OVERRIDE", "", TRUE);
	// [case: 3519] don't touch ctor initialization list (starts with ':')
	if (!mMethodQualifier.IsEmpty() && mMethodQualifier[0] != ':')
	{
		mMethodQualifier.ReplaceAll("new", "", TRUE);
		mMethodQualifier.ReplaceAll("NEW", "", TRUE);
	}
	mMethodQualifier.ReplaceAll("sealed", "", TRUE);
	mMethodQualifier.ReplaceAll("SEALED", "", TRUE);
	mMethodQualifier.ReplaceAll("abstract", "", TRUE);
	mMethodQualifier.ReplaceAll("ABSTRACT", "", TRUE);
	mMethodQualifier.ReplaceAll("final", "", TRUE);
	mMethodQualifier.ReplaceAll("FINAL", "", TRUE);
	mMethodQualifier.ReplaceAll("Q_DECL_FINAL", "", TRUE);

	if (mCurSymName.IsEmpty())
	{
		// [case: 57625]
		// decided not to use this but it might come in handy in some other scenario
		// 		const WTString symName(::StrGetSym(mCurSymScope));
		// 		if (symName.GetLength() && strchr("!%^&*-+<>/=[|(", symName[0]))
		// 			mCurSymName = symName;
	}

	// Since we strip "operator " in the context, we need to add it back here.
	if (mCurSymName.GetLength() && strchr("!%^&*-+<>/=[|(", mCurSymName[0]))
	{
		bool doOp = true;
		if (mCurSymName[0] == '!' && (mCurSymName != "!" && mCurSymName != "!="))
		{
			int pos = mCurSymScope.Find(mCurSymName);
			if (-1 != pos)
			{
				WTString tmp = mCurSymScope.Left(pos);
				if (tmp.Find(mCurSymName.Mid(1)) != -1)
					doOp = false; // finalizer, not operator!
			}
		}

		if (doOp)
		{
			WTString opstr = WTString("operator") + mCurSymName;
			mCurSymScope.ReplaceAll(mCurSymName, opstr);
			mCurSymName = opstr;
		}
	}

	// template hacks
	WTString tmplateArg = GetTemplateStr();
	WTString templateSymTypeHack;
	if (tmplateArg.GetLength())
	{
		// [case: 2948]
		tmplateArg = ::HandleDefaultValues(GetLangType(), tmplateArg, tmplateArg.GetLength() + 1);

		if (!StartsWith(mCurSymType, "template") && addTemplateDeclaration)
		{
			// <HACK>: We should use a %TemplateArgs% in auto text.
			// only add "template<...>\n" if we are not in document method
			templateSymTypeHack = tmplateArg + lnBrk;
		}

		bool symScopeHasLessThan = -1 != mCurSymScope.Find('<');
		if (symScopeHasLessThan)
		{
			// [case: 1539]
			WTString curSymScope(mCurSymScope);
			curSymScope.ReplaceAll("operator<<", "");
			curSymScope.ReplaceAll("operator<=", "");
			curSymScope.ReplaceAll("operator<", "");
			symScopeHasLessThan = -1 != curSymScope.Find('<');
		}

		if (!symScopeHasLessThan)
		{
			// add foo<T>::bar
			// [case: 2993] don't add <T> for template functions
			// 			const WTString base(StrGetSymScope(mCurSymScope));
			// 			DType * baseDat = g_currentEdCnt ? g_currentEdCnt->m_pmparse->FindExact2(base) : NULL;
			// 			if (baseDat)
			{
				// 				int baseType = baseDat->MaskedType();
				// 				if (CLASS == baseType || STRUCT == baseType || TYPE == baseType)
				{
					// tmp is a bit of a hack to check for out-of-class inline implementation of
					// template class method - needs to retain targs; it's not a standalone
					// template function.  Standalone template functions do not get targs.
					const WTString tmp(WTString(EncodeChar('>')) + WTString("::") + mCurSymName);
					if ((m_deep && !StartsWith(State().m_begLinePos, "template")) ||
					    (mData && (-1 != mData->Def().Find(tmp))))
					{
						WTString targs = GetTemplateArgs(); // TODO: parse to get args
						mCurSymScope = StrGetSymScope(mCurSymScope) + targs + ":" + StrGetSym(mCurSymScope);
					}
				}
			}
		}
	}

	expText.ReplaceAll("$SymbolPrivileges$", privileges);
	expText.ReplaceAll("$SymbolStatic$", staticStr);
	// $SymbolConst$ isn't even used in the default autotext items
	expText.ReplaceAll("$SymbolConst$", constStr + volatileStr);
	expText.ReplaceAll("$SymbolVirtual$", virtualStr);
	WTString curSymScope = inlineImplementation ? StrGetSym(mCurSymScope) : mCurSymScope;

	EdCntPtr curEd = g_currentEdCnt;
	MultiParsePtr mp = curEd ? curEd->GetParseDb() : nullptr;

	if (replaceNamespaces)
	{
		// [case: 20637]
		_ASSERTE(RefactoringActive::IsActive());
		if (curEd)
		{
			if (!mp || !mp->GetFileID() || !mp->IsIncluded(curEd->FileName(), TRUE))
			{
				::SynchronousFileParse(curEd);
				auto tmp = curEd->GetParseDb();
				mp = tmp;
			}
		}
		else
			_ASSERTE(!"curEd is needed for synch parse");

		if (mp)
			::RemoveNamespaceScopesExposedViaUsings(mp.get(), curSymScope);
		else
			_ASSERTE(!"curEd parseDb needed for namespace handling");
	}

	WTString dispCurSymScope = CleanScopeForDisplay(curSymScope);
	if (IsCFile(GetLangType()))
		dispCurSymScope.ReplaceAll(".", "::");
	expText.ReplaceAll("$SymbolContext$", dispCurSymScope);

	if (mCurSymType.IsEmpty() && !templateSymTypeHack.IsEmpty())
	{
		// attempt to cleanup ctor and dtor if $SymbolType$ is used but empty
		expText.ReplaceAll("$SymbolType$" + lnBrk, templateSymTypeHack);
		expText.ReplaceAll("$SymbolType$ ", templateSymTypeHack);
	}

	tmpLnBrk = EolTypes::GetEolStr(mCurSymType);
	if (tmpLnBrk != lnBrk)
		mCurSymType.ReplaceAll(tmpLnBrk, lnBrk);
	if (mCurSymType[0] == DB_SEP_CHR && mCurSymType[1] != DB_SEP_CHR && IsCFile(GetLangType()))
	{
		// [case: 45850] fix ::returnType
		mCurSymType.SetAt(0, '.');
		mCurSymType.ReplaceAll(".", "::");
	}
	mCurSymType = templateSymTypeHack + mCurSymType;

	if (curEd && mCurSymType.GetLength())
	{
		//  case 1247: Qualify CurSymType if needed
		if (!mp->FindExact(mCurSymType[0] == DB_SEP_CHR ? mCurSymType : DB_SEP_STR + mCurSymType))
		{
			WTString curSymType = mCurSymType;
			CommentSkipper cs(GetFType());

			// case 26956
			// cut out template part
			WTString templatePart;
			for (int i = 0; i < curSymType.GetLength(); i++)
			{
				TCHAR c = curSymType[i];
				if (cs.IsCode(c))
				{
					if (c == '<')
					{
						while (i > 0 && IsWSorContinuation(curSymType[i - 1]))
							i--;

						templatePart = curSymType.Mid(i);
						curSymType = curSymType.Left(i);
						break;
					}
				}
			}

			// case 1247 (Type Qualifiers support)

			int ignore = 0;
			WTString keywords[] = {"constexpr", "consteval", "constinit", "_CONSTEXPR17", "_CONSTEXPR20_CONTAINER", "_CONSTEXPR20", "const", "volatile"};

			for (const WTString& keyword : keywords)
			{
				if (curSymType.Left(keyword.length()) == keyword)
				{
					ignore = keyword.length();
					break;
				}
			}

			WTString appendBefore;
			if (ignore)
			{
				CommentSkipper cs2(GetFType());
				for (int i = ignore; i < mCurSymType.GetLength(); i++)
				{
					TCHAR c = mCurSymType[i];
					if (cs2.IsCode(c) && c != '/' && !IsWSorContinuation(c))
					{
						appendBefore = curSymType.Left(i);
						curSymType = curSymType.Mid(i);
						break;
					}
				}
			}

			// case 1247 (Pointer, reference and const after the type support)
			WTString appendAfter;
			cs.Reset();
			int cutPos = -1;
			for (int i = 1; i < curSymType.GetLength(); i++)
			{
				TCHAR c = curSymType[i];
				if (cs.IsCode(c) && c != '/')
				{
					if (c == 'c' && i < curSymType.GetLength() - 4 && curSymType[i + 1] == 'o' &&
					    curSymType[i + 2] == 'n' && curSymType[i + 3] == 's' && curSymType[i + 4] == 't')
					{
						cutPos = i;
						while (cutPos > 0 && IsWSorContinuation(curSymType[cutPos - 1]))
							cutPos--;
						break;
					}
					if (c == '&')
					{
						cutPos = i;
						while (cutPos > 0 && IsWSorContinuation(curSymType[cutPos - 1]))
							cutPos--;
						break;
					}
					if (c == '^')
					{
						cutPos = i;
						while (cutPos > 0 && IsWSorContinuation(curSymType[cutPos - 1]))
							cutPos--;
						break;
					}
					if (c == '*' &&
					    (curSymType[i - 1] != '/' || (curSymType[i - 1] == '/' && i > 1 && curSymType[i - 2] == '*')))
					{
						cutPos = i;
						while (cutPos > 0 && IsWSorContinuation(curSymType[cutPos - 1]))
							cutPos--;
						break;
					}
				}
			}
			if (cutPos != -1)
			{
				appendAfter = curSymType.Mid(cutPos);
				curSymType = curSymType.Left(cutPos);
			}

			// Case 1247 (Comment after type)
			// if we still have comments after the stripping or because there was no stripping, remove all comments
			WTString copy;
			cs.Reset();
			for (int i = 0; i < curSymType.GetLength(); i++)
			{
				TCHAR c = curSymType[i];
				if (cs.IsCode(c) && c != '/')
				{
					copy += c;
				}
			}
			if (copy.GetLength())
				curSymType = copy;

			mCurSymType = GetQualifiedSymType(curSymType, expText);
			mCurSymType = appendBefore + mCurSymType; // case 1247 (Type Qualifiers support)
			mCurSymType += appendAfter;               // case 1247 (pointer and reference support)

			// case 26956
			// parse and qualify template part
			int firstCharPos = -1;
			WTString templateTypeName;
			if (!templatePart.IsEmpty())
			{
				cs.Reset();
				for (int i = 0; i < templatePart.GetLength(); i++)
				{
					TCHAR c = templatePart[i];
					if (cs.IsCode(c))
					{
						if (c == ',' || c == '<' || c == '>')
						{
							if (!templateTypeName.IsEmpty() && firstCharPos != -1)
							{
								templateTypeName.TrimRight();
								WTString qualifiedTemplateTypeName = GetQualifiedSymType(templateTypeName, expText);
								templatePart.ReplaceAt(firstCharPos, templateTypeName.GetLength(),
									                    qualifiedTemplateTypeName.c_str());
								i += qualifiedTemplateTypeName.GetLength() - templateTypeName.GetLength();
							}
							firstCharPos = -1;
							templateTypeName = "";
							continue;
						}
						if (firstCharPos == -1)
						{
							if (!IsWSorContinuation(c))
							{
								firstCharPos = i;
								templateTypeName += c;
							}
						}
						else
						{
							if (c == '(')
							{
								firstCharPos = -1;
								templateTypeName = "";
							}
							else
							{
								auto checkKeyword = [&]()
								{
									for (const WTString& keyword : keywords)
									{
										if (templateTypeName == keyword)
										{
											firstCharPos = -1;
											templateTypeName = "";
											return true; // continue outer loop
										}
									}

									return false;
								};

								if (checkKeyword())
									continue;
								if (c != '*' && c != '&' && c != ')') //
									templateTypeName += c;

							}
						}
					}
				}
			}
			mCurSymType += templatePart; // append back the template part with qualified types
		}
	}

	bool IsFreeFunc(DType * sym);

	if (inlineImplementation) // keep method specifiers from the definition (case 10239)
	{
		// if (mData && IsFreeFunc(mData))
		//	inlineImplPreString = "inline " + inlineImplPreString;
		expText.ReplaceAll("$SymbolType$", Comment + inlineImplPreString);
	}
	else
	{
		expText.ReplaceAll("$SymbolType$", mCurSymType);
	}

	expText.ReplaceAll("$ParameterList$", mParamString);
	::SubstituteMethodBody(expText, mMethodBody);

	// fix up "operator std.vector<int>" --> "operator std::vector<int>"
	if (IsCFile(GetLangType()))
		mCurSymName.ReplaceAll(".", "::");

	expText.ReplaceAll("$SymbolName$", mCurSymName);
	expText.ReplaceAll("$GeneratedPropertyName$", mGeneratedPropertyName);

	// [case: 9863] allow symbol related snippet reserved words in refactoring snippets
	if (curEd && expText.contains("$ClassName$") || expText.contains("$NamespaceName$") ||
	    expText.contains("$BaseClassName$"))
	{
		WTString className;
		WTString namespaceName;
		WTString baseClassName;
		WTString symScope;
		if (mCurScope.GetLength() > 2)
		{
			symScope = '.' + mCurScope + mCurSymName;
			if (IsCFile(GetLangType()))
				symScope.ReplaceAll(".", ":");
		}
		else
		{
			DTypePtr dtPtr = mp->GetCwData();
			if (dtPtr)
				symScope = dtPtr->SymScope();
		}
		if (symScope.GetLength() > 2)
		{
			int sepIdx = symScope.ReverseFind(':');
			if (sepIdx != -1)
			{
				WTString scope = symScope.Left(sepIdx);
				sepIdx = scope.ReverseFind(':');
				if (sepIdx != -1)
				{
					DType* dType = mp->FindExact(scope);
					if (dType)
					{
						if (dType->MaskedType() == CLASS || dType->MaskedType() == STRUCT)
							className = scope.Mid(sepIdx + 1);
					}
				}
				sepIdx = 0;
				bool namespaceFound = false;
				do
				{
					namespaceFound = false;
					sepIdx = scope.Find(":", sepIdx + 1);
					WTString reducedScope;
					if (sepIdx == -1)
						reducedScope = scope;
					else
						reducedScope = scope.Left(sepIdx);
					DType* dType = mp->FindExact(reducedScope);
					if (dType && dType->MaskedType() == NAMESPACE)
					{
						namespaceName = reducedScope.Mid(1);
						namespaceFound = true;
					}
				} while (namespaceFound);
				if (IsCFile(GetLangType()))
					namespaceName.ReplaceAll(":", "::");
				else
					namespaceName.ReplaceAll(":", ".");
				MultiParsePtr mp2 = MultiParse::Create(GetLangType());
				token2 baseClasses = mp2->GetBaseClassList(scope, true, nullptr, GetLangType());
				if (baseClasses.Find(WILD_CARD) == -1)
				{
					if (baseClasses.read('\f').GetLength() > 1)
					{
						WTString baseClass = baseClasses.read("\f");
						if (baseClass.GetLength() > 1)
						{
							sepIdx = baseClass.ReverseFind(':');
							if (sepIdx != -1)
								baseClassName = baseClass.Mid(sepIdx + 1);
						}
					}
				}
			}
		}
		expText.ReplaceAll("$ClassName$", className);
		expText.ReplaceAll("$NamespaceName$", namespaceName);
		expText.ReplaceAll("$BaseClassName$", baseClassName);
	}
	if (mGeneratedPropertyName == "$PropertyName$")
		expText.ReplaceAll("$MethodName$", mCurSymName);
	else
		expText.ReplaceAll("$MethodName$", mGeneratedPropertyName);

	// [case: 35316] common convention to make Fields have lower case first letter
	WTString altGeneratedPropertyName(mGeneratedPropertyName);
	if (!altGeneratedPropertyName.IsEmpty() && !(mGeneratedPropertyName[0] & 0x80))
		altGeneratedPropertyName.SetAt(0, (char)::tolower(mGeneratedPropertyName[0]));
	expText.ReplaceAll("$generatedPropertyName$", altGeneratedPropertyName);

	if (inlineImplementation) // keep method qualifiers from the definition (case 10239)
	{
		if (inlineImplPostString.IsEmpty())
			expText.ReplaceAll(" $MethodQualifier$", inlineImplPostString);
		expText.ReplaceAll("$MethodQualifier$", inlineImplPostString);
	}
	else
	{
		if (mMethodQualifier.IsEmpty())
			expText.ReplaceAll(" $MethodQualifier$", mMethodQualifier);
		expText.ReplaceAll("$MethodQualifier$", mMethodQualifier);
	}

	// case 115411: fix for __declspec(selectany) in case of template; if it is templateSymTypeHack and we have
	// __declspec(selectany), put template definition before __declspec(selectany) instead after it
	if (expText.contains("__declspec(selectany)") && !templateSymTypeHack.IsEmpty())
	{
		WTString declSpecSelectanyHack;
		expText.ReplaceAll(templateSymTypeHack, "");
		declSpecSelectanyHack = templateSymTypeHack + "__declspec(selectany)";
		expText.ReplaceAll("__declspec(selectany)", declSpecSelectanyHack);
	}

	return expText;
}

WTString VAScopeInfo::GetQualifiedSymType(WTString curSymType, WTString expText)
{
	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd)
		return NULLSTR;

	// case 1247 (Typedef in class support)
	curSymType.ReplaceAll("::", ":");
	curSymType.ReplaceAll(".", ":");
	WTString appendAfterType;
	if (curSymType.GetLength() && curSymType[0] != ':')
	{
		int pos = curSymType.Find(":");
		if (pos != -1)
		{
			appendAfterType = curSymType.Mid(pos);
			appendAfterType.ReplaceAll(":", "::");
			curSymType = curSymType.Left(pos);
		}
	}

	MultiParsePtr mp(curEd->GetParseDb());
	// case 1247 (Base class support)
	WTString BCL = mp->GetBaseClassList(StrGetSymScope(mCurSymScope));

	// Needs to be qualified, add scope to sym
	DType* data = mp->FindSym(&curSymType, &mCurSymScope, &BCL, NULL);
	if (data)
	{
		const WTString lnBrk(EolTypes::GetEolStr(expText));
		WTString newSymType = CleanScopeForDisplay(data->SymScope());
		WTString tmpLnBrk;
		tmpLnBrk = EolTypes::GetEolStr(newSymType);
		if (tmpLnBrk != lnBrk)
			newSymType.ReplaceAll(tmpLnBrk, lnBrk);

		if (IsCFile(GetLangType()))
			newSymType.ReplaceAll(".", "::");

		newSymType += appendAfterType;
		return newSymType;
	}

	return curSymType;
}

WTString VAScopeInfo::CommentStrFromDef()
{
	WTString implTemplate = gAutotextMgr->GetSource("Refactor Document Method");
	return ExpandMacros(implTemplate, false, false);
}

WTString VAScopeInfo::ImplementationStrFromDef(bool stripCommentsFromTemplate, bool inlineImplementation,
                                               bool explicitlyDefaulted, bool replaceNamespaces /*= false*/)
{
	WTString implTemplate;

	if (explicitlyDefaulted)
		implTemplate = gAutotextMgr->GetSource("Refactor Create Implementation (defaulted)");
	else
		implTemplate = gAutotextMgr->GetSource("Refactor Create Implementation");

	if (stripCommentsFromTemplate)
		implTemplate = StripCommentsAndStrings(GetLangType(), implTemplate);

	WTString impl = ExpandMacros(implTemplate, true, inlineImplementation, replaceNamespaces);
	impl.ReplaceAll("(  )", "()");
	return impl;
}

WTString VAScopeInfo::MemberImplementationStrFromDef(bool forHeaderFile)
{
	WTString implTemplate;
	if (forHeaderFile)
		implTemplate = gAutotextMgr->GetSource("Refactor Create Implementation for Member (header file)");
	else
		implTemplate = gAutotextMgr->GetSource("Refactor Create Implementation for Member");

	const WTString impl = ExpandMacros(implTemplate, true, false);
	return impl;
}

void VAScopeInfo::ParseDefStr(const WTString& scope, const WTString& def, BOOL declIsImpl)
{
	mData = NULL;
	mBufStr = def;
	mBufStr.ReplaceAll("virtual", "", TRUE);
	mBufStr.ReplaceAll("PURE", "", TRUE);
	mBufStr =
	    ParseLineClass::SingleLineFormat(GetLangType(), mBufStr, TRUE, mBufStr.GetLength()); // comment (arg /* = 3*/);

	DoParse(mBufStr);
	LPCSTR paramPos;
	mCurSymName = GetCStr(State().m_lastScopePos);
	if (mCurSymName == "operator")
	{
		// need to parse operator chars, but be careful for
		// operator()().
		mCurSymName += GetOperatorChars(State().m_lastScopePos, &paramPos);
	}
	else
	{
		paramPos = State().m_lastScopePos;
	}

	// find first '('
	paramPos = strchr(paramPos, '(');

	mCurSymType = GetSubStr(mBufStr.c_str(), State().m_lastScopePos);
	if (!declIsImpl)
	{
		// if this is the implementation in the src file with no header decl, remove certain keywords
		mCurSymType.ReplaceAll("private", "", TRUE);
		mCurSymType.ReplaceAll("public", "", TRUE);
		mCurSymType.ReplaceAll("protected", "", TRUE);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		mCurSymType.ReplaceAll("__published", "", TRUE);
		mCurSymType.ReplaceAll("__classmethod", "", TRUE);
		mCurSymType.ReplaceAll("__declspec(dynamic)", "", TRUE);
		mCurSymType.ReplaceAll("DYNAMIC", "", TRUE);
#endif
		mCurSymType.ReplaceAll("internal", "", TRUE);
		mCurSymType.ReplaceAll("static", "", TRUE);
		mCurSymType.ReplaceAll("thread_local", "", TRUE);
		mCurSymType.ReplaceAll("tile_static", "", TRUE);
		mCurSymType.ReplaceAll("explicit", "", TRUE);
		//		CurSymType.ReplaceAll("inline", "", TRUE); // probably should leave this in the implementation?
	}
	mCurSymType.Trim();

	mCurScope = scope;
	mMethodBody.Empty();
	if (paramPos)
	{
		ParseToCls ptc(GetLangType());
		const auto len = GetBufLen() - ptr_sub__int(paramPos + 1, GetBuf());
		ptc.ParseTo(paramPos + 1, len, ")", len);
		SetParamString(WTString(paramPos + 1, ptc.GetCp()));
		LPCSTR qualPos = ptc.CurPos();
		if (qualPos[0] == ')')
			qualPos++;
		mMethodQualifier = TokenGetField(qualPos, "{;\r\n");
		mMethodQualifier.TrimLeft();
	}

	mScopeStr = scope + DB_SEP_STR;
	mCurSymScope = mScopeStr + mCurSymName;
}

BOOL VAScopeInfo::CreateImplementation(LPCSTR methscope, const CStringW& infile, bool isTemplate, bool isMethod,
                                       UnrealPostfixType unrealPostfixType, bool isDefaulted)
{
	int insertLn = -1;
	const WTString buf(GetFileText(infile));

	FindSimilarLocation find;
	insertLn = find.WhereToPutImplementation(buf, infile, unrealPostfixType);
	if (insertLn != -1)
	{
		WTString implNamespace = find.GetImplNamespace();
		for (int i = 0; i < implNamespace.GetLength();)
		{
			if (implNamespace.GetLength() >= 10 &&
			    implNamespace.Mid(i, 10) == WTString("namespace") + FSL_SEPARATOR_STR)
			{
				implNamespace.ReplaceAt(i, 10, "");
				continue;
			}
			else if (implNamespace.GetLength() >= 9)
			{
				if (implNamespace.Mid(i, 9) == "namespace")
				{
					implNamespace.ReplaceAt(i, 9, "");
					continue;
				}
			}
			else
			{
				break;
			}
			i++;
		}

		if (implNamespace.GetLength())
		{
			// strip off scope where we are going to insert it.
			mCurSymScope = mCurSymScope.Mid(implNamespace.GetLength() + 1);
		}
	}
	else
	{
		if (isTemplate)
		{
			TemplateImplementationPos_MLC cp(GetFileType(infile));
			insertLn = (int)cp->GetImplLine(buf, methscope);
		}
		else
		{
			ImplementationPosCls_MLC cp(GetFileType(infile));
			insertLn = (int)cp->GetImplLine(buf, methscope);
			const WTString iscope(cp->m_insertLineScope);
			if (!iscope.IsEmpty() && StartsWith(mCurSymScope, iscope))
			{
				// strip off scope where we are going to insert it.
				mCurSymScope = mCurSymScope.Mid(iscope.GetLength());
			}
		}
	}

	if (DelayFileOpen(infile, insertLn))
		return InsertImplementation(isMethod, infile, buf, false, -1, unrealPostfixType, isDefaulted);

	return FALSE;
}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
BOOL VAScopeInfo::CreateImplementationPropertyCppB(DType* sym, const CStringW& infile)
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		int ln = sym->Line();

		if (ln < 0)
			return FALSE;

		// property can be split in multiple lines, find the first line from which it starts
		int lnFirst = ln;
		for (; lnFirst >= 0; lnFirst--)
		{
			WTString lineText = ed->GetLine(lnFirst);
			if (lineText.contains("__property"))
				break;
		}

		if (lnFirst < 0)
			return FALSE;

		// in case if it is split, find the last line (one that contains ";")
		int lnLast = -1;
		for (int i = 0; i < 10; i++) // search for ";" in next 10 lines
		{
			WTString lineText = ed->GetLine(ln + i);
			if (lineText.contains(";"))
			{
				lnLast = ln + i;
				break;
			}
		}

		if (lnLast < 0)
			return FALSE;

		WTString visStr(::GetVisibilityString(sym->GetVisibility(), gTypingDevLang));
		visStr.append(":");

		uint linePos = ed->LinePos(lnFirst);
		WTString bufPos = ed->GetSubString(linePos, NPOS);
		ReadToCls rtc(Src);
		rtc.KeepBraces();
		WTString fullText = rtc.ReadTo(bufPos.c_str(), bufPos.GetLength(), ";}");

		// get tokenized values for array (if it is an array)
		std::vector<WTString> arrayTokens;
		int startPosition = fullText.find_first_of("[");
		int endPosition = -1;
		int newStartPosition = -1;
		while (startPosition > -1)
		{
			endPosition = fullText.find_first_of("]", startPosition);
			newStartPosition = fullText.find_first_of("[", startPosition + 1);

			if (endPosition > startPosition && (newStartPosition == -1 || newStartPosition > endPosition))
			{
				arrayTokens.push_back(fullText.substr(startPosition + 1, endPosition - startPosition - 1));
				startPosition = newStartPosition;
			}
			else
			{
				startPosition = -1;
			}
		}

		WTString snippetName;
		if (arrayTokens.size())
		{
			// array
			snippetName = "Refactor Create Implementation for Property (array)";
		}
		else
		{
			fullText.ReplaceAll(" ", "");
			if (fullText.contains("={read}"))
			{
				// read only
				snippetName = "Refactor Create Implementation for Property (read-only)";
			}
			else
			{
				// read/write
				snippetName = "Refactor Create Implementation for Property (read/write)";
			}
		}

		if (DelayFileOpen(infile))
		{
			if (!ChangeOriginalLinePropertyCppB(ed, lnFirst, lnLast, snippetName, arrayTokens, visStr))
				return FALSE;

			DelayFileOpen(infile, lnFirst); // position caret to the line of __property

			return TRUE;
		}
	}

	return FALSE;
}

BOOL VAScopeInfo::ChangeOriginalLinePropertyCppB(EdCntPtr& curEd, int lnFirst, int lnLast, const WTString& snippetName,
                                                 const std::vector<WTString>& arrayTokens,
                                                 const WTString& originalVisibilityScope)
{
	// select text that needs to be changed
	int pos1 = (int)curEd->LinePos(lnFirst);
	int pos2 = (int)curEd->LinePos(lnLast + 1);
	curEd->SetSelection(pos1, pos2);

	WTString declarationTemplate;
	declarationTemplate = gAutotextMgr->GetSource(snippetName);

	WTString oldCurSymType = CurSymType();
	mCurSymType.ReplaceAll("__property", "");
	mCurSymType.TrimLeft();

	declarationTemplate = ExpandMacros(declarationTemplate, true, false);

	// appending original scope visibility
	declarationTemplate.append(originalVisibilityScope.c_str());
	declarationTemplate.append("\n");

	ExpandPropertyMacroCppB(declarationTemplate, arrayTokens);

	SetCurSymType(oldCurSymType);

	return curEd->InsertW(declarationTemplate.Wide(), true, vsFormatOption::vsfAutoFormat, false);
}

void VAScopeInfo::ExpandPropertyMacroCppB(WTString& impText, const std::vector<WTString>& arrayTokens)
{
	if (impText.contains("$PropertyArrayDef$") || impText.contains("$PropertyArrayParam$") ||
	    impText.contains("$PropertyArrayIndex$") || impText.contains("$PropertyArrayCheckCond$") ||
	    impText.contains("$PropertyArraySize$") || impText.contains("$PropertyArrayFieldDef$"))
	{
		WTString propertyArrayDef;
		WTString propertyArrayParam;
		WTString propertyArrayIndex;
		WTString propertyArrayCheckCond;
		WTString propertyArraySize;
		WTString propertyArrayFieldDef;

		for (size_t i = 0; i < arrayTokens.size(); i++)
		{
			WTString arrayToken = arrayTokens[i];
			arrayToken.ReplaceAll("  ", " ");
			arrayToken.ReplaceAll("\t", " ");

			int spaceLocation = arrayToken.ReverseFind(" ");

			// get variable name and type
			WTString variableName = spaceLocation > -1 ? arrayToken.Mid(spaceLocation + 1) : arrayToken;
			WTString variableType = spaceLocation > -1 ? arrayToken.Mid(0, spaceLocation) : "";

			// make uppercased first letter
			WTString variableNameUpperCase = variableName;
			WTString t = variableNameUpperCase.Left(1);
			t.MakeUpper();
			variableNameUpperCase.SetAt(0, t[0]);

			if (i > 0)
			{
				propertyArrayParam.append(",");
				propertyArrayCheckCond.append(" && ");
				propertyArraySize.append("\n");
			}

			propertyArrayDef.append("[");
			propertyArrayDef.append(arrayTokens[i].c_str());
			propertyArrayDef.append("]");

			propertyArrayParam.append(arrayTokens[i].c_str());

			propertyArrayIndex.append("[");
			propertyArrayIndex.append(variableName.c_str());
			propertyArrayIndex.append("]");

			propertyArrayCheckCond.append(variableName.c_str());
			propertyArrayCheckCond.append(" < ");
			propertyArrayCheckCond.append(mCurSymName.c_str());
			propertyArrayCheckCond.append(variableNameUpperCase.c_str());
			propertyArrayCheckCond.append("Size");

			propertyArraySize.append("static const ");
			propertyArraySize.append(variableType.c_str());
			propertyArraySize.append(" ");
			propertyArraySize.append(mCurSymName.c_str());
			propertyArraySize.append(variableNameUpperCase.c_str());
			propertyArraySize.append("Size = 2;");

			propertyArrayFieldDef.append("[");
			propertyArrayFieldDef.append(mCurSymName.c_str());
			propertyArrayFieldDef.append(variableNameUpperCase.c_str());
			propertyArrayFieldDef.append("Size]");
		}

		impText.ReplaceAll("$PropertyArrayDef$", propertyArrayDef);
		impText.ReplaceAll("$PropertyArrayParam$", propertyArrayParam);
		impText.ReplaceAll("$PropertyArrayIndex$", propertyArrayIndex);
		impText.ReplaceAll("$PropertyArrayCheckCond$", propertyArrayCheckCond);
		impText.ReplaceAll("$PropertyArraySize$", propertyArraySize);
		impText.ReplaceAll("$PropertyArrayFieldDef$", propertyArrayFieldDef);
	}
}

#endif

// UE4 UPARAM markup should only appear in the definition, not the implementation. [case: 110735]
WTString RemoveUparamFromImp(WTString impText)
{
	int start = 0;

	do
	{
		start = impText.Find("UPARAM", start);

		if (start != -1)
		{
			int parenDeep = 0;
			int end = 0;

			for (int i = start + 6; i < impText.GetLength(); ++i)
			{
				if (impText[i] == '(')
				{
					++parenDeep;
				}
				else if (impText[i] == ')')
				{
					--parenDeep;

					if (parenDeep < 0)
					{
						// Too many close parenthesis found (malformed).
						break;
					}
					else if (parenDeep == 0)
					{
						end = i + 1; // Include the ')'.
						break;
					}
				}
				else if (parenDeep == 0)
				{
					if (!wt_isspace(impText[i]))
					{
						// Found non-whitespace before first open paren (malformed).
						break;
					}
				}
			}

			if (end == 0)
			{
				// Mismatched parenthesis found (malformed) or no parenthesis found at all after the UPARAM, not
				// even the end of the method declaration (very malformed). Don't do anything and prevent the same
				// UPARAM from being found on the next loop.
				start = start + 6;
			}
			else
			{
				// Expand the end to include any extra whitespace.
				while (end < impText.GetLength() + 1 && wt_isspace(impText[end]))
					++end;

				impText.ReplaceAt(start, end - start, "");
			}
		}
	} while (start != -1);

	return impText;
}

// [case: 111093] add postfix for Unreal UFunctions which require them
WTString AddUFunctionPostfix(WTString impText, UnrealPostfixType unrealPostfixType)
{
	if (unrealPostfixType != UnrealPostfixType::None)
	{
		int openParen = impText.Find('(');

		if (openParen != -1)
		{
			switch (unrealPostfixType)
			{
			case UnrealPostfixType::Implementation:
				impText.insert(openParen, "_Implementation");
				break;

			case UnrealPostfixType::Validate:
			case UnrealPostfixType::ValidateFollowingImplementation:
				impText.insert(openParen, "_Validate");
				break;

			default:
				break;
			}
		}
	}

	return impText;
}

// [case: 118695]
bool IsExplicitlyDefaulted(const WTString& def)
{
	// warning, this code assumes a single def not a concatenated series of defs
	_ASSERTE(def.Find('\f') == -1);
	WTString defNoSpace = def;
	defNoSpace.ReplaceAll(" ", "");
	if (defNoSpace.Find("=default") != -1)
		return true;

	return false;
}

BOOL VAScopeInfo::InsertImplementation(bool isMethod, const CStringW& infile, const WTString& buf,
                                       bool inlineImplementation, long begPos, UnrealPostfixType unrealPostfixType,
                                       bool defaultedImplementation)
{
	EdCntPtr curEd = g_currentEdCnt;
	WTString impText;
	WTString oldCurSymType = CurSymType(); // the symtype may need to be temporarily modified

	if (unrealPostfixType == UnrealPostfixType::Validate ||
	    unrealPostfixType == UnrealPostfixType::ValidateFollowingImplementation)
		SetCurSymType("bool");

	if (isMethod)
		impText = ImplementationStrFromDef(false, inlineImplementation, defaultedImplementation, !infile.IsEmpty());
	else
		impText = MemberImplementationStrFromDef(::GetFileType(infile) == Header);

	SetCurSymType(oldCurSymType);

	if (Psettings->mUnrealEngineCppSupport)
		impText = RemoveUparamFromImp(impText);

	if (unrealPostfixType != UnrealPostfixType::None)
		impText = AddUFunctionPostfix(impText, unrealPostfixType);

	InsertNewLinesBeforeAfterIfNeeded(impText, buf);

	if (TERCOL(curEd->CurPos()) > 1)
		impText = EolTypes::GetEolStr(impText) + impText; // eof needs extra lnBrk

	long p1 = (long)curEd->CurPos();

	// [case: 111093] When a _Validate method follow an _Implementation, remember where the _Implementation started so
	// we can select both. This works because the methods are implemented in the same place one after the other. A
	// little hacky.
	static long unrealImpStart = -1;

	if (unrealPostfixType == UnrealPostfixType::Implementation)
		unrealImpStart = p1;
	else if (unrealImpStart != -1 && unrealPostfixType == UnrealPostfixType::ValidateFollowingImplementation)
		p1 = unrealImpStart;
	else
		unrealImpStart = -1;

	if (Psettings->mSelectImplementation /*&& !inlineImplementation*/)
	{
		// Select implementation so user can cut and paste where they want it
		// See case 11206 / case 2093
		// Strip off $end so we can select whole method
		impText.ReplaceAll("$end$", "");
	}

	BOOL r = gAutotextMgr->InsertAsTemplate(curEd, impText,
	                                        !gShellAttr->IsDevenv12OrHigher() || !Psettings->mSelectImplementation);
	long p2 = (long)curEd->CurPos();
	if (gShellAttr->IsDevenv12OrHigher())
	{
		// [case: 76620]
		curEd->SetSelection(p2, p1);

		curEd->GetBuf(TRUE);
		CStringW selstr(curEd->GetSelStringW());
		if (!selstr.IsEmpty() && selstr != CStringW(impText.Wide()))
		{
			WideStrVector lines;
			::WtStrSplitW(selstr, lines, L"\n");
			selstr.Empty();
			bool fixed = false;
			CStringW openBraceLineTxt;
			int openBraceLineNum = 0;
			int braceDepth = 0, lineNum = 0;
			for (CStringW& ln : lines)
			{
				if (!fixed)
				{
					const int openBrcPos = ln.Find(L'{');
					if (-1 != openBrcPos)
					{
						if (!braceDepth++)
						{
							openBraceLineNum = lineNum;
							openBraceLineTxt = ln;
						}
					}

					const int closeBrcPos = ln.Find(L'}');
					if (-1 != closeBrcPos)
					{
						if (!--braceDepth)
						{
							if (openBraceLineNum && openBraceLineNum != lineNum)
							{
								// compare leading whitespace on the two lines
								CStringW openBraceLineLeadingWhitespace, jnk;
								RemoveLeadingWhitespace(openBraceLineTxt, openBraceLineLeadingWhitespace, jnk);

								CStringW closeBraceLineLeadingWhitespace;
								CStringW closeBraceLineRemainingTxt;
								RemoveLeadingWhitespace(ln, closeBraceLineLeadingWhitespace,
								                        closeBraceLineRemainingTxt);

								if (openBraceLineLeadingWhitespace != closeBraceLineLeadingWhitespace)
								{
									// if there's a difference, then
									// make end brace match the open brace
									ln = openBraceLineLeadingWhitespace + closeBraceLineRemainingTxt;
									fixed = true;
								}
								else
									break;
							}
							else
								break;
						}
					}
				}

				selstr += ln;
				++lineNum;
			}

			if (fixed)
			{
				curEd->InsertW(selstr, true, noFormat, false);
				p1 = (int)curEd->CurPos();
			}
		}

		if (!Psettings->mSelectImplementation /*|| inlineImplementation*/)
			curEd->SetSelection(p2, p2);
	}

	if (Psettings->mSelectImplementation /*&& !inlineImplementation*/)
	{
		if (inlineImplementation)
			p2 = curEd->GetBufIndex(curEd->LineIndex(curEd->LineFromChar(
			    p2))); // because of the indentation inside a class when using Move Implementation to Header File
		if (begPos != -1)
			p1 = begPos;
		curEd->SetSelection(p2, p1);
		if (gShellAttr->IsDevenv12OrHigher())
		{
			// curEd->Reformat(-1, curEd->LineFromChar(p1), -1, curEd->LineFromChar(p2)); // this solution had side
			// effects. e.g. put a space before every line with VAAutoTest:MoveImp0001
			curEd->SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.FormatSelection"), 0);
		}
	}

	return r;
}

extern bool TrimEmptyLine(WTString& text);

CREATE_MLC_IMPL(FindSymDefCls, FindSymDef_MLC);
CREATE_MLC_IMPL(FindSymDefsCls, FindSymDefs_MLC);

void FindSymDefCls::FindSymbolInFile(const CStringW& file, const DType* sym, BOOL impl /*= FALSE*/)
{
	mFoundDef = FALSE;
	mSymData = sym;
	mFindImpl = impl;
	if (IsFile(file))
	{
		mFileId = gFileIdManager->GetFileId(file);
		mFileTxt = ::GetFileText(file);
		Init(mFileTxt);
		// process macros
		m_processMacros = TRUE;
		EdCntPtr curEd = g_currentEdCnt;
		m_mp = curEd ? curEd->GetParseDb() : nullptr;
		DoParse();
	}
}

void FindSymDefCls::OnDef()
{
	if (mFoundDef)
		return;

	WTString sym = MethScope();
	WTString symscope(mSymData->SymScope());
	if (symscope != sym && -1 != symscope.Find(sym))
	{
		// [case: 7204]
		// not the same, but a substr match.
		// get using directives for current file and see if concat creates a match.
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			MultiParsePtr mp = ed->GetParseDb();
			_ASSERTE(mp);
			DTypeList dtList;

			WTString fileScopedUsing;
			_ASSERTE(mFileId);
			fileScopedUsing.WTFormat(":wtUsingNamespace_%x", mFileId);
			mp->FindExactList(fileScopedUsing, dtList, false);
			for (auto& d : dtList)
			{
				if (d.FileId() == mFileId && d.MaskedType() == RESWORD && d.Attributes() & V_IDX_FLAG_USING)
				{
					const WTString ns(d.Def());
					if (ns + sym == symscope)
					{
						sym = symscope;
						break;
					}
				}
			}
		}
	}

	if (symscope == sym)
	{
		if (!m_inMacro && (!mFindImpl || CurChar() != ';'))
		{
			if (Is_VB_VBS_File(GetLangType()))
			{
				// VB defines the symbol as found, not on the ',;{' characters.
				ParseToCls ptc(GetLangType());
				ptc.ParseTo(CurPos(), GetLenOfCp(), "\r\n");
				m_cp += ptc.GetCp();
			}
			if (State().HasParserStateFlags(VPSF_CONSTRUCTOR))
				mCtorInitCode = GetSubStr(State(m_deep + 1).m_begLinePos - 1, CurPos());

			mBegLinePos = ptr_sub__int(State().m_begLinePos, m_buf);
			WTString orgLineText = WTString(State().m_begLinePos, m_cp - mBegLinePos);
			mShortLineText = GetLineStr(m_deep, false);
			if ((State().m_defAttr & V_CONSTRUCTOR))
			{
				if (!orgLineText.contains("("))
				{
					// Constructors get OnDef before the ';', return and pick it up on the ;
					return;
				}

#if 0
				// [case: 85806]
				// truncate at ctor colon that starts base/members list
				WTString tmp(::ReadToUnpairedColon(mShortLineText));
				if (!tmp.IsEmpty())
					mShortLineText = tmp;
#endif
			}
			mFoundDef = TRUE;
		}
	}
}

bool FindSymDefsCls::FindSymDefs(const WTString& defIn)
{
	mFoundDefForArg = false;
	mFoundClosingParen = false;
	mArgsFound = 0;
	mEndOfTemplateArgs = 0;

	WTString def = defIn;
	_ASSERTE(!def.IsEmpty());

	const char defFirstChar = def[0];
	if (defFirstChar == 'S')
	{
		if (def.Find("STDMETHOD(") == 0)
		{
			std::string str = def.c_str();
			std::regex rx("STDMETHOD\\(([^\\)]*)\\)");
			str = regex_replace(str, rx, std::string("HRESULT $1"));
			def = WTString(str.c_str());
		}
		else if (def.Find("STDMETHOD_(") == 0)
		{
			std::string str = def.c_str();
			std::regex rx("STDMETHOD_\\(([^,)]*),([^\\)]*)\\)");
			str = regex_replace(str, rx, std::string("$1 $2"));
			def = WTString(str.c_str());
		}
		else if (def.Find("STDMETHODIMP_(") == 0)
		{
			std::string str = def.c_str();
			std::regex rx("STDMETHODIMP_\\(([^\\)]*)\\)");
			str = regex_replace(str, rx, std::string("$1"));
			def = WTString(str.c_str());
		}
	}
	else if (defFirstChar == 'p')
	{
		// Strip C# access modifiers
		if (StartsWith(def, "public"))
		{
			def = def.Mid(6);
			def.TrimLeft();
		}
		if (StartsWith(def, "private"))
		{
			def = def.Mid(7);
			def.TrimLeft();
		}
		if (StartsWith(def, "protected"))
		{
			def = def.Mid(9);
			def.TrimLeft();
		}
	}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	else if (defFirstChar == '_' && StartsWith(def, "__published"))
	{
		def = def.Mid(11);
		def.TrimLeft();
	}
	else if (defFirstChar == '_' && StartsWith(def, "__declspec"))
	{
		std::string str = def.c_str();
		std::regex rx("__declspec\\(([^\\)]*)\\)");
		str = regex_replace(str, rx, std::string(""));
		def = WTString(str.c_str());
		def.TrimLeft();
	}
#endif

	// not else due to "protected internal"
	if (def[0] == 'i' && StartsWith(def, "internal"))
	{
		def = def.Mid(8);
		def.TrimLeft();
	}

	DoParse(def);

	// prepend method and type and qualifiers
	auto sym = GetCStr(State().m_lastScopePos);
	mDefSyms = sym + ";" + mDefSyms;

	WTString tmp = GetSubStr(State().m_begLinePos, State().m_lastScopePos);
	tmp.TrimRight();
	mDefTypes = GetSubStr(State().m_begLinePos, State().m_lastScopePos) + ";" + mDefTypes;

	return !mErrors && mFoundClosingParen && !m_deep;
}

void FindSymDefsCls::OnDef()
{
	if (GetCp() <= mEndOfTemplateArgs)
	{
		// [case: 114966] ignore defs inside of template argument lists
		return;
	}
	else if (mFoundDefForArg)
	{
		OnError(CurPos());
		return;
	}

	auto sym = GetCStr(State().m_lastScopePos);

	WTString type = GetSubStr(State().m_begLinePos, State().m_lastScopePos);
	type.TrimRight();

	auto defaultsStart = State().m_lastScopePos + sym.GetLength();
	auto defaultsEnd = CurPos();
	auto defaults = GetSubStr(defaultsStart, defaultsEnd);
	// [case: 80605]
	defaults.Trim();

	if (defaultsStart && *defaultsStart == '[')
	{
		// [case: 80169]
		if (!defaults.IsEmpty() && defaults[0] == '[' && defaults[defaults.GetLength() - 1] == ']')
		{
			type += defaults;
			defaults.Empty();
		}
		else
			return;
	}

	AddArg(sym, type, defaults);

	mFoundDefForArg = true;
}

void FindSymDefsCls::DecDeep()
{
	if (!InComment() && m_deep == 1 && InParen(m_deep) && CurChar() == ')')
	{
		// Foo() ok -- no args
		if (!mFoundDefForArg)
		{
			auto startPos = State().m_begLinePos;
			if (*startPos == ',')
				++startPos;

			WTString type = GetSubStr(startPos, CurPos());
			type.TrimRight();

			if (mArgsFound)
			{
				AddArg("", type, "");
			}
			else
			{
				// first arg
				if (!type.IsEmpty())
					AddArg("", type, "");
			}
		}
		mFoundDefForArg = false;
	}
	else if (!InComment() && m_deep && InParen(m_deep - 1) && InSquareBrace(m_deep) && CurChar() == ']')
	{
		// [case: 80169]
		mFoundDefForArg = false;
	}

	__super::DecDeep();

	if (m_deep == 0 && CurChar() == ')')
	{
		mFoundClosingParen = true;

		// int Foo() const = 0;
		WTString qualifiers = CurPos() + 1;
		mDefDefaults = qualifiers + ";" + mDefDefaults;
	}
}

void FindSymDefsCls::OnError(LPCSTR errPos)
{
	__super::OnError(errPos);
	mErrors++;
}

void FindSymDefsCls::ClearLineState(LPCSTR cPos)
{
	if (!InComment() && m_deep == 1 && InParen(m_deep) && CurChar() == ',')
	{
		if (!mFoundDefForArg)
		{
			mDefSyms += ";";

			auto startPos = State().m_begLinePos;
			if (cPos != startPos && *startPos == ',')
				++startPos;

			WTString type = GetSubStr(startPos, CurPos());
			type.TrimRight();
			mDefTypes += type + ";";

			mDefDefaults += ";";
			++mArgsFound;
		}
		mFoundDefForArg = false;
	}

	__super::ClearLineState(cPos);
}

void FindSymDefsCls::AddArg(WTString sym, WTString type, WTString defaults)
{
	mDefSyms += sym + ";";
	mDefTypes += type + ";";
	mDefDefaults += defaults + ";";
	++mArgsFound;
}

void FindSymDefsCls::SpecialTemplateHandler(int endOfTemplateArgs)
{
	__super::SpecialTemplateHandler(endOfTemplateArgs);
	mEndOfTemplateArgs = endOfTemplateArgs;
}

WTString CodeToken::read(LPCSTR parseto)
{
	LPCSTR p = mCodeBuf.c_str();
	while (*p && strchr(parseto, *p))
		p++;

	const int readLen = ::GetSafeReadLen(p, mCodeBuf.c_str(), mCodeBuf.GetLength());
	WTString val = ReadTo(p, readLen, parseto);
	mCodeBuf = CurPos();
	return val;
}

int CodeToken::more()
{
	return mCodeBuf.GetLength();
}

//////////////////////////////////////////////////////////////////////////

static WTString PV_TextFromComment(WTString comment)
{
	token t(comment);
	// replace all whitespace with a single space
	t.ReplaceAll(OWL::TRegexp("[ \t\r\n]+"), OWL_SPACESTR);
	t.ReplaceAll("*/ /*", SPACESTR);
	t.ReplaceAll(" ///", SPACESTR);
	t.ReplaceAll(" //", SPACESTR);
	return t.Str();
}

struct NodeStack
{
	LineMarkers::Node* mNode;
	UINT mGrouping;
};

#define GROUP_NONE 0
#define GROUP_INCLUDE 1
#define GROUP_IFELSE 2
#define GROUP_MACROS 3
#define GROUP_TAG 4
#define GROUP_MSGMAP 5
#define GROUP_REGION 6
#define GROUP_METHODS 7
#define GROUP_METHODS_ITEM 8
#define GROUP_USING 9
#define GROUP_FWDDECL 10
#define GROUP_GLOBALS 11
#define GROUP_IMPORTS 12

class VAParseFileOutlineMarkers : public MethodsInFile
{
  private:
	NodeStack pNode[STATE_COUNT];
	int mNodeDeep;

	// comment markers
	LPCSTR mCommentPosStart;
	LPCSTR mCommentPosEnd;
	ULONG mCommentPosLine;
	ULONG mCommentPosLineEnd;

  public:
	int CurNodeDeep() const
	{
		return mNodeDeep;
	}
	LineMarkers::Node* CurFolder() const
	{
		return pNode[mNodeDeep].mNode;
	}
	UINT CurFolderGroup() const
	{
		return pNode[mNodeDeep].mGrouping;
	}
	void SetCurFolder(LineMarkers::Node* n, UINT grouping = 0)
	{
		NodeStack& ns = pNode[mNodeDeep];
		ns.mNode = n;
		ns.mGrouping = grouping;
		SetCurLeaf(NULL, 0);
	}
	LineMarkers::Node* CurLeaf() const
	{
		return pNode[mNodeDeep + 1].mNode;
	}
	void SetCurLeaf(LineMarkers::Node* n, UINT grouping = 0)
	{
		NodeStack& ns = pNode[mNodeDeep + 1];
		ns.mNode = n;
		ns.mGrouping = grouping;
	}

	LineMarkers::Node* CurVisibleFolder()
	{
		for (int curFolderDepth = mNodeDeep; curFolderDepth >= 0; --curFolderDepth)
		{
			LineMarkers::Node* curFolder = pNode[curFolderDepth].mNode;
			if (curFolder)
				return curFolder;
		}
		_ASSERTE(!"VAParseFileOutlineMarkers::CurActiveFolder() -- Shouldn't get here");
		return NULL;
	}

	virtual void DoParse()
	{
		mNodeDeep = 0;
		SetCurFolder(&mMarkers->Root());
		MethodsInFile::DoParse();

		UpdateFolderEnd();

		int endLn = 0;
		int endCp = 0;
		GetEndLine(&endLn, &endCp);
		while (CurNodeDeep())
		{
			UpdateLeafEnd((ULONG)endLn, (ULONG)endCp);
			NodeDecDeep();
		}
		AddComment();
		UpdateLeafEnd((ULONG)endLn, (ULONG)endCp);
	}

	virtual void IncDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		MethodsInFile::IncDeep();
		NodeIncDeep();
	}

	virtual void DecDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		MethodsInFile::DecDeep();
		NodeDecDeep();
	}

	virtual BOOL ProcessMacro()
	{
		// for now, do not expand macros as Methods in File does.
		// without this implementation, started getting asserts in unit
		// tests working on case 69244.  Also items went missing that I
		// wouldn't expect to be missing.
		// fix for [case: 30076] addressed issues independently
		return FALSE;
	}

	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (JS != GetLangType())
		{
			// Mark comments to add later
			// Ignore comments in local scope, macro defs, or mid-statement
			if (!InLocalScope() && !m_inMacro && 0 == State().m_parseState && !mAttributePos)
			{
				if (c == '*' || c == '\n' || c == '-')
				{
					// Check to see if comment is on a line by itself
					LPCSTR p;
					for (p = CurPos(); p > m_buf && (p[-1] == ' ' || p[-1] == '\t'); p--)
					{
					}

					if (p == m_buf || p[-1] == '\r' || p[-1] == '\n')
					{
						// two comment blocks with whitespace in between get two nodes
						if (mCommentPosStart && m_curLine > (mCommentPosLineEnd + 1))
							AddComment();

						if (!mCommentPosStart)
						{
							mCommentPosStart = CurPos();
							mCommentPosLine = m_curLine;
						}
						mCommentPosLineEnd = m_curLine;
					}
				}
				else if (!c && (CommentType() == '*' || CommentType() == '\n' || CommentType() == '-'))
				{
					mCommentPosEnd = CurPos();
					mCommentPosLineEnd = m_curLine;
				}
			}

			if (c == '[' && !m_inMacro && 0 == State().m_parseState && !mAttributePos)
			{
				// [case: 112204]
				// c++ [[attribute]] are considered comments, but we want the outline to treat as code.
				//
				// Can't check InLocalScope() here because attributes are always local since they Dec/Inc Deep
				if (!m_deep || !InLocalScope(m_deep - 1))
				{
					// Check to see if attribute starts a line
					LPCSTR p;
					for (p = CurPos(); p > m_buf && (p[-1] == ' ' || p[-1] == '\t'); p--)
						;

					if (p == m_buf || p[-1] == '\r' || p[-1] == '\n')
					{
						// outline treats c++ [[attributes]] the same as c# [attributes]
						OnAttribute();
						if (mAttributePos)
						{
							// rewind because IncDeep increments pos to second [ of the [[ pair
							--mAttributePos;
						}
					}
				}
			}
		}

		MethodsInFile::OnComment(c, altOffset);
	}

	void UpdateFolderEnd()
	{
		// Get the currently visible folder node in the tree.  CurActiveFolder
		// should never return NULL because the root node should always be
		// present.
		LineMarkers::Node* curFolder = CurVisibleFolder();
		_ASSERTE(curFolder);

		if (curFolder)
		{
			int& curFolderStart = (int&)curFolder->Contents().mStartLine;
			int& curFolderEnd = (int&)curFolder->Contents().mEndLine;
			_ASSERTE(curFolderStart <= curFolderEnd);
			std::ignore = curFolderStart;

			int& curFolderStartCp = (int&)curFolder->Contents().mStartCp;
			int& curFolderEndCp = (int&)curFolder->Contents().mEndCp;
			_ASSERTE(curFolderStartCp <= curFolderEndCp);
			std::ignore = curFolderStartCp;

			if (CurLeaf())
			{
				int curLeafStart = (int)CurLeaf()->Contents().mStartLine;
				int curLeafEnd = (int)CurLeaf()->Contents().mEndLine;
				_ASSERTE(curLeafStart <= curLeafEnd);
				std::ignore = curLeafStart;

				int curLeafStartCp = (int)CurLeaf()->Contents().mStartCp;
				int curLeafEndCp = (int)CurLeaf()->Contents().mEndCp;
				_ASSERTE(curLeafStartCp <= curLeafEndCp);
				std::ignore = curLeafStartCp;

				if (curLeafEnd > curFolderEnd)
					curFolderEnd = curLeafEnd;

				if (curLeafEndCp > curFolderEndCp)
					curFolderEndCp = curLeafEndCp;
			}
			else
			{
			}
		}
	}

	void UpdateLeafEnd(ULONG newEndLine, ULONG newEndCp)
	{
		if (CurLeaf())
		{
			ULONG& curEndLine = CurLeaf()->Contents().mEndLine;
			if (curEndLine < newEndLine)
			{
				// extend existing leaf (common)
				_ASSERTE(newEndLine >= CurLeaf()->Contents().mStartLine);
				curEndLine = newEndLine;
			}
			else if (curEndLine > newEndLine)
			{
				if (!(newEndLine >= CurLeaf()->Contents().mStartLine))
					VALOGERROR("Outline: curEndLine > newEndLine");

				// shrink existing leaf (uncommon)
				if (newEndLine > CurLeaf()->Contents().mStartLine)
					curEndLine = newEndLine;
			}

			ULONG& curEndCp = CurLeaf()->Contents().mEndCp;
			if (curEndCp < newEndCp)
			{
				// extend existing leaf (common)
				curEndCp = newEndCp;
			}
			else if (curEndCp > newEndCp)
			{
				if (!(newEndCp >= CurLeaf()->Contents().mStartCp))
					VALOGERROR("Outline: curEndCp > newEndCp");

				// shrink existing leaf (uncommon)
				if (newEndCp > CurLeaf()->Contents().mStartCp)
					curEndCp = newEndCp;
			}
		}
	}

	void AddComment()
	{
		if (CurFolder() && mCommentPosStart && mCommentPosEnd)
		{
			ULONG startLine = mCommentPosLine;
			ULONG endLine = mCommentPosLineEnd;
			ULONG startCp = ULONG(mCommentPosStart - m_buf);
			const bool commentEndCharIsNull = !*mCommentPosEnd;
			ULONG endCp = ULONG(mCommentPosEnd + (commentEndCharIsNull ? 0 : 1) - m_buf);

			// Make comment readable (one line)
			WTString commentStr =
			    GetSubStr(mCommentPosStart, commentEndCharIsNull ? mCommentPosEnd : mCommentPosEnd + 1);
			mCommentPosStart = NULL;
			mCommentPosEnd = NULL;
			commentStr = PV_TextFromComment(commentStr);

			// grab whitespace leading up to node (same line only)
			startCp = (ULONG)BackUpCpWhitespace((int)startCp);

			UpdateLeafEnd(startLine, startCp);
			SetCurLeaf(
			    &CurFolder()->AddChild(FileLineMarker(commentStr.Wide(), startLine, startCp, endLine, endCp, COMMENT, 0,
			                                          FileOutlineFlags::ff_Comments, DType(), true)));
		}
	}

	void HandleMarkerGrouping(UINT grouping, ULONG nextStartLine, ULONG nextStartCp)
	{
		if (mCommentPosStart)
		{
			nextStartLine = mCommentPosLine;
			nextStartCp = ULONG(mCommentPosStart - m_buf);
		}

		if (mAttributePos)
		{
			nextStartLine = std::min(nextStartLine, mAttributeLine);
			nextStartCp = std::min(nextStartCp, (ULONG)(mAttributePos - m_buf));
		}

		switch (CurFolderGroup())
		{
		case GROUP_NONE:
		case GROUP_IFELSE:
		case GROUP_TAG:
		case GROUP_MSGMAP:
		case GROUP_REGION:
		case GROUP_METHODS_ITEM:
			break;

		case GROUP_METHODS:
			if (grouping != GROUP_METHODS_ITEM)
			{
				UpdateLeafEnd(nextStartLine, nextStartCp);
				NodeDecDeep();
			}
			break;

		case GROUP_INCLUDE:
		case GROUP_MACROS:
		case GROUP_USING:
		case GROUP_FWDDECL:
		case GROUP_GLOBALS:
		case GROUP_IMPORTS:
			if (grouping != CurFolderGroup())
			{
				UpdateLeafEnd(nextStartLine, nextStartCp);
				NodeDecDeep();
			}
			break;

		default:
			_ASSERTE(0);
			break;
		}
	}

	virtual bool AddMarker(const WTString& txt, int dType, uint attrs, UINT grouping, ULONG displayFlag,
	                       ULONG startLine, ULONG startCp, DType* pCD = NULL)
	{
		bool addedMkr = false;
		bool canDrag = true;

#if defined(_DEBUG) || defined(VA_CPPUNIT) // [case: 21251] drag / drop problems need to be addressed before release
		if (!CurFolder() && GetLangType() == JS)
		{
			// In JS files methods can be declared deep in if() blocks, where there is no CurFolder().
			// This adds them to any parent or root node so they appear in the outline.
			LineMarkers::Node* fld = CurVisibleFolder();
			SetCurFolder(fld);
			canDrag = false;
		}
#endif // _DEBUG

		if (CurFolder())
		{
			HandleMarkerGrouping(grouping, startLine, startCp);

			AddComment();

			int endCp = 0;
			int endLine = 0;
			GetEndLine(&endLine, &endCp, false);

			// grab whitespace leading up to node (same line only)
			startCp = (ULONG)BackUpCpWhitespace((int)startCp);

			UpdateLeafEnd(startLine, startCp);

			// check if parent can be dragged
			if (canDrag)
				canDrag = CurFolder()->Contents().mCanDrag;

			_ASSERTE(endLine >= (int)startLine);
			SetCurLeaf(
			    &CurFolder()->AddChild(FileLineMarker(txt.Wide(), startLine, startCp, (ULONG)endLine, (ULONG)endCp,
			                                          (ULONG)dType, attrs, displayFlag, pCD ? *pCD : DType(), canDrag)),
			    grouping);
			addedMkr = true;
		}
		else
		{
			// this only seems to happen for local non-typedef'd struct variables:
			// {
			//     struct foo myFoo;
			//     ...
			// }
			//
			// This is probably more of an issue with the base parser, than one
			// with the live outline parser.
			//
		}

		return addedMkr;
	}

	void GetEndLine(int* outLn, int* outCp, bool consumeTextOnCurLine = true)
	{
		int ln = (int)m_curLine;
		LPCSTR p = CurPos();

		// This caused a problem for VB, so need to see why it's here for C++/C#
		if (!Is_VB_VBS_File(GetLangType()))
		{
			if (*p)
				p++;
		}

		bool keepGoing;
		do
		{
			keepGoing = false;

			if (consumeTextOnCurLine)
			{
				// include rest of this line
				for (; *p && *p != '\n' && *p != '\r'; p++)
				{
				}
			}

			// check for continuation
			if (p[-1] == '\\')
				keepGoing = true;

			for (; wt_isspace(*p); p++)
			{
				if (*p == '\n' || (*p == '\r' && *(p + 1) != '\n'))
					ln++;
			}
			if (*p == '\0')
				++ln;
		} while (keepGoing);

		*outLn = ln;
		*outCp = BackUpCpWhitespace(ptr_sub__int(p, m_buf)); // grab whitespace leading up to node (same line only)
	}

	void DoNodeDecDeep()
	{
		if (mNodeDeep)
		{
			UpdateFolderEnd();
			SetCurLeaf(NULL);
			mNodeDeep--;
		}
	}

	virtual void ClearLineState(LPCSTR cPos)
	{
		// [case: 40680] node-start hack for __declspec.
		WTString lastWord = GetCStr(State().m_lastWordPos);
		if (lastWord.contains("_declspec"))
			mDeclSpecPos = State().m_begLinePos;

		__super::ClearLineState(cPos);

		if (JS == GetLangType())
		{
			if (CurLeaf())
			{
				int endCp = 0;
				int endLine = 0;
				if (CurChar() == '\n')
					GetEndLine(&endLine, &endCp, false);
				else
					GetEndLine(&endLine, &endCp, true);
				UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
				UpdateFolderEnd();
				SetCurLeaf(NULL);
			}
		}
	}

	void NodeDecDeep()
	{
		if (Is_Tag_Based(GetLangType()))
		{
			if (CurChar() == '>' && PrevChar() != '=')
			{
				DoNodeDecDeep();
				if (CurLeaf())
				{
					int endCp = 0;
					int endLine = 0;
					GetEndLine(&endLine, &endCp, false);
					UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
				}
				UpdateFolderEnd();
				SetCurLeaf(NULL);
			}
			else
			{
				DoNodeDecDeep();
				if (CurLeaf())
					UpdateLeafEnd(m_curLine, (ULONG)BackUpCpWhitespace(m_cp));
				UpdateFolderEnd();
				SetCurLeaf(NULL);
			}
			return;
		}

		switch (CurChar())
		{
		case '}': // End of class/func/etc.
			AddComment();

			{
				int cp = BackUpCpWhitespace(m_cp);

				bool keepGoing = true;
				do
				{
					if (CurLeaf())
						UpdateLeafEnd(m_curLine, (ULONG)cp);

					switch (CurFolderGroup())
					{
					case GROUP_TAG:
					case GROUP_USING:
					case GROUP_FWDDECL:
					case GROUP_METHODS:
					case GROUP_INCLUDE: // case 9254 (#includes within class decl)
					case GROUP_MACROS:
						DoNodeDecDeep();
						break;

					default:
						keepGoing = false;
						break;
					}
				} while (keepGoing);
			}

			DoNodeDecDeep();

			if (JS == GetLangType())
			{
				{
					int endCp = 0;
					int endLine = 0;
					GetEndLine(&endLine, &endCp);
					UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
				}
				UpdateFolderEnd();
				SetCurLeaf(NULL);
				break;
			}

			switch (State().m_defType)
			{
			case CLASS:
			case STRUCT: {
				int endCp = 0;
				int endLine = 0;
				GetEndLine(&endLine, &endCp);
				UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
			}

				// put impl of single-line inline classes in node text
				UpdateNodeTextForInlineImpl();

				UpdateFolderEnd();
				SetCurLeaf(NULL);
				break;

			case FUNC:
				// put impl of single-line inline functions in node text
				UpdateNodeTextForInlineImpl();
			default:
				UpdateLeafEnd(m_curLine, (ULONG)m_cp + 1); // include closing '}'
				UpdateFolderEnd();
				break;
			}
			break;

		case ']':
		case ')':
			DoNodeDecDeep();
			break;
		case '>':
			if (PrevChar() != '=')
				DoNodeDecDeep();
			break;

		case ':': // End of public/protected/private block
		case '#': // End of else/elif/endif block
		default:
			if (CurLeaf())
			{
				// comments at the end of a p/p/p block belong to the next
				// p/p/p block label.  Therefore we have to limit any
				// leafs to the beginning of the comment, not m_curLine.
				// Also applies to endif's.

				ULONG startLn = State().m_StatementBeginLine;
				ULONG startCp = ULONG(ConstState().m_begLinePos - m_buf);

				if (CurChar() == '#')
				{
					startLn = m_curLine;
					startCp = (ULONG)m_cp;
				}
				bool truncateCurLeaf = false;
				if (mCommentPosStart)
				{
					if (!CurChar() && State().m_begLinePos)
					{
						if ((State().m_begLinePos[0] == '}' || State().m_begLinePos[0] == ';'))
						{
							if (startLn < mCommentPosLine /*&& startLn != (mCommentPosLine - 1)*/)
							{
								if (startLn != (mCommentPosLine - 1))
								{
									// [case: 81737]
									startCp = (ULONG)(mCommentPosStart - m_buf);
									startLn = mCommentPosLine - 1;

									// rewind startCp one line
									if (startCp && m_buf[startCp - 1] == '\n')
										startCp--;
									if (startCp && m_buf[startCp - 1] == '\r')
										startCp--;
								}
								else
								{
									// [case: 93336]
									startCp = (ULONG)(mCommentPosStart - m_buf);
									startLn = mCommentPosLine;
								}
							}
						}
					}

					startLn = min(startLn, mCommentPosLine);
					startCp = min(startCp, (ULONG)(mCommentPosStart - m_buf));
					truncateCurLeaf = true;
				}

				if (mAttributePos)
				{
					startLn = min(startLn, mAttributeLine);
					startCp = min(startCp, (ULONG)(mAttributePos - m_buf));
					truncateCurLeaf = true;
				}

				// grab whitespace leading up to node (same line only)
				const ULONG oldStartCp = startCp;
				startCp = (ULONG)BackUpCpWhitespace((int)startCp);

				if (truncateCurLeaf)
				{
					// truncate current leaf
					UpdateLeafEnd(startLn, startCp);
				}
				else if (oldStartCp < (ULONG)CurLeaf()->Contents().mEndCp ||
				         Is_VB_VBS_File(GetLangType()) && CurChar() == '#')
				{
					// extend current leaf
					int endCp = 0;
					int endLine = 0;
					GetEndLine(&endLine, &endCp, true);
					UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
				}
				else
				{
					// extend current leaf
					UpdateLeafEnd(startLn, startCp);
				}
			}

			DoNodeDecDeep();
			if (Is_VB_VBS_File(GetLangType()) && CurChar() == '\n')
			{
				// End Something ' Just select this line, GetEndLine will include End ... on the next line
				int endCp = 0;
				int endLine = 0;
				GetEndLine(&endLine, &endCp);
				UpdateLeafEnd((ULONG)endLine, (ULONG)endCp);
			}
			UpdateFolderEnd();
			SetCurLeaf(NULL);
			break;
		}
	}

	void UpdateNodeTextForInlineImpl()
	{
		if (CurLeaf())
		{
			FileLineMarker& mkr = CurLeaf()->Contents();
			if ((int)m_curLine == mkr.mStartLine)
			{
				LPCSTR lastScopePos = State().m_lastScopePos;
				LPCSTR curPos = CurPos();

				WTString nodeText = GetSubStr(lastScopePos, curPos + 1);
				if (mkr.mType == FUNC)
				{
					const int closePos = nodeText.Find(')');
					if (-1 != closePos)
					{
						const int openPos = nodeText.Find('(');
						if (-1 == openPos || closePos < openPos)
						{
							// [case: 12803]
							WTString newTxt(nodeText.Left(closePos));
							newTxt += nodeText.Mid(closePos + 1);
							nodeText = newTxt;
						}
					}
				}

				// replace all whitespace with a single space
				nodeText.Trim();
				nodeText = StripExtraWhiteChars(nodeText);
				mkr.mText = nodeText.Wide();
			}
		}
	}

	void NodeIncDeep()
	{
		if (mNodeDeep < STATE_COUNT)
			mNodeDeep++;

		if (mNodeDeep < STATE_COUNT)
		{
			SetCurLeaf(NULL);
		}
		else
		{
			_ASSERTE(!"NodeIncDeep: node depth exceeded");
		}
	}

	int BackUpCpWhitespace(int startCp)
	{
		int tmp = startCp - 1;
		while (tmp > 0 && wt_isspace(m_buf[tmp]) && m_buf[tmp] != '\n' && m_buf[tmp] != '\r')
			--tmp;
		return tmp + 1;
	}

  protected:
	VAParseFileOutlineMarkers(int fType)
	    : MethodsInFile(fType)
	{
		ZeroMemory(pNode, sizeof pNode);
		mCommentPosStart = mCommentPosEnd = NULL;
		mNodeDeep = 0;
		mCommentPosLineEnd = 0;
		mDeclSpecPos = NULL;
		mAttributePos = NULL;
	}

	LPCSTR mDeclSpecPos;
	LPCSTR mAttributePos;
	ULONG mAttributeLine;
};

static WTString StripExtraWhiteChars(const WTString& str)
{
	WTString rstr = str;
	int i = 0;
	int j = 0;
	int l = rstr.GetLength();
	for (; i < l; i++, j++)
	{
		// Don't strip MBCS, wt_isspace assumes 0xb7/bb are ViewWhiteSpace chars for painting. case=4983
		if (!(str[i] & 0x80) && wt_isspace(str[i]))
		{
			rstr.SetAt(j, ' ');
			while (!(str[i] & 0x80) && wt_isspace(str[i + 1]))
				i++;
		}
		else
			rstr.SetAt(j, str[i]);
	}
	rstr = rstr.Mid(0, j);
	return rstr;
}

class VAParseFileOutline : public VAParseFileOutlineMarkers
{
  public:
	BOOL BuildOutline(const WTString& buf, MultiParsePtr mp, LineMarkers& markers, bool force)
	{
		// reduce parse time by not calling FindExact for every item
		// it would be better to store MethScope() in the marker and then
		// call g_currentEdCnt->m_mp->FindExact(markerScope) as needed rather
		// than assume it is always needed
		mCollectClassData = force || buf.GetLength() < (500 * 1024) ? true : false;
		return GetMethods(buf, mp, true, markers);
	}

	BOOL BuildOutline(const WTString& buf, MultiParsePtr mp, LineMarkers& markers, ULONG maxlines = 0,
	                  BOOL collectDtype = FALSE)
	{
		mCollectClassData = collectDtype;
		mMaxLines = maxlines;
		return GetMethods(buf, mp, true, markers);
	}

  protected:
	ULONG mInMessageMap;
	ULONG mInMessageMapDeep;
	LPCSTR mLastSymPtr;
	ULONG mMethodGroupHash;
	BOOL mCollectClassData;
	ULONG mMaxLines;
	char MacroChar = 0; // when we parse macro, this is how we show what character we are on

	virtual LPCSTR GetParseClassName()
	{
		return "VAParseFileOutline";
	}

	BOOL IsDone()
	{
		if (mMaxLines && m_curLine > mMaxLines)
			return TRUE;

		return __super::IsDone();
	}

	virtual void TestForUnnamedStructs()
	{
	}

	virtual void OnDirective()
	{
		// skip MethodsInFile::OnDirective
		VAParseDirectiveC::OnDirective();
		if (InLocalScope(m_deep))
			return;

		WTString directive = PV_ReadCSym(&m_buf[m_cp]);

		if (StrCmpAC(directive.c_str(), "region") == 0)
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_REGION, FileOutlineFlags::ff_Regions, m_curLine, (ULONG)m_cp);
			NodeIncDeep();
		}
		else if (StrCmpAC(directive.c_str(), "endregion") == 0 ||
		         (Is_VB_VBS_File(GetLangType()) && StrCmpAC(directive.c_str(), "End") == 0)) // VB uses "#End Region"
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			while (CurNodeDeep() && CurFolderGroup() != GROUP_REGION)
				NodeDecDeep();
			AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_Regions, m_curLine, (ULONG)m_cp);
			NodeDecDeep();
		}
		else if (directive == "pragma")
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			int pos = defStr.Find("pragma");
			if (-1 != pos)
			{
				WTString pragma = TokenGetField(&defStr.c_str()[pos + 7], "# \t\r\n");
				if (pragma == "region")
				{
					AddMarker(defStr, DEFINE, 0, GROUP_REGION, FileOutlineFlags::ff_Regions, m_curLine, (ULONG)m_cp);
					NodeIncDeep();
				}
				else if (pragma == "endregion")
				{
					while (CurNodeDeep() && CurFolderGroup() != GROUP_REGION)
						NodeDecDeep();
					AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_Regions, m_curLine, (ULONG)m_cp);
					NodeDecDeep();
				}
				else
				{
					AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_Preprocessor, m_curLine, (ULONG)m_cp);
				}
			}
		}
		else if (directive == "define" || directive == "undef")
		{
			if (CurFolderGroup() != GROUP_MACROS)
			{
				AddMarker(WTString("#defines"), DEFINE, 0, GROUP_MACROS, FileOutlineFlags::ff_MacrosPseudoGroup,
				          m_curLine, (ULONG)m_cp);
				NodeIncDeep();
			}

			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_MACROS, FileOutlineFlags::ff_Macros, m_curLine, (ULONG)m_cp);
		}
		else if (directive == "if" || directive == "ifdef" || directive == "ifndef")
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_IFELSE, FileOutlineFlags::ff_Preprocessor, m_curLine, (ULONG)m_cp);
			NodeIncDeep();
		}
		else if (directive == "else" || directive == "elif")
		{
			while (CurNodeDeep() && CurFolderGroup() != GROUP_IFELSE)
				NodeDecDeep();
			NodeDecDeep();
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_IFELSE, FileOutlineFlags::ff_Preprocessor, m_curLine, (ULONG)m_cp);
			NodeIncDeep();
		}
		else if (directive == "endif")
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			while (CurNodeDeep() && CurFolderGroup() != GROUP_IFELSE)
				NodeDecDeep();
			AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_Preprocessor, m_curLine, (ULONG)m_cp);
			NodeDecDeep();
		}
		else if (directive == "include")
		{
			if (CurFolderGroup() != GROUP_INCLUDE)
			{
				AddMarker(WTString("#includes"), DEFINE, 0, GROUP_INCLUDE, FileOutlineFlags::ff_IncludePseudoGroup,
				          m_curLine, (ULONG)m_cp);
				NodeIncDeep();
			}

			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_INCLUDE, FileOutlineFlags::ff_Includes, m_curLine, (ULONG)m_cp);
		}
		else if (directive == "Imports" || directive == "import")
		{
			if (CurFolderGroup() != GROUP_IMPORTS)
			{
				AddMarker(directive == "Imports" ? WTString("Imports") : WTString("#imports"), DEFINE, 0, GROUP_IMPORTS,
				          FileOutlineFlags::ff_IncludePseudoGroup, m_curLine, (ULONG)m_cp);
				NodeIncDeep();
			}

			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_IMPORTS, FileOutlineFlags::ff_Includes, m_curLine, (ULONG)m_cp);
		}
		else
		{
			//
			// #using, #line, #error, etc...
			//
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_Preprocessor, m_curLine, (ULONG)m_cp);
		}
	}

	virtual char CurChar() const
	{
		return MacroChar ? MacroChar : __super::CurChar();
	}
	virtual void OnCSym()
	{
		if (m_inIFDEFComment)
			return; // [case: 83377]

		// group BEGIN_MESSAGE_MAP entries
		MethodsInFile::OnCSym();

		if (!Is_C_CS_VB_File(FileType()))
			return;

		if (mInMessageMap && strncmp(CurPos(), "END_", 4) == 0)
		{
			WTString sym = TokenGetField(CurPos(), "()[]{}<> \t\r\n");
			if (sym == "END_MESSAGE_MAP" || sym == "END_SINK_MAP" || sym == "END_INTERFACE_MAP" ||
			    sym == "END_INTERFACE_PART" || sym == "END_COM_MAP" || sym == "END_DISPATCH_MAP")
			{
				while (CurNodeDeep() && CurFolderGroup() != GROUP_MSGMAP)
					NodeDecDeep();
				WTString defStr = TokenGetField(CurPos(), "\r\n");
				AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_MessageMaps, m_curLine, (ULONG)m_cp);
				NodeDecDeep();
				mInMessageMap = 0;
			}
		}
		else if (strncmp(CurPos(), "BEGIN_", 6) == 0)
		{
			WTString sym = TokenGetField(CurPos(), "()[]{}<> \t\r\n");
			if (sym == "BEGIN_MESSAGE_MAP" || sym == "BEGIN_SINK_MAP" || sym == "BEGIN_INTERFACE_MAP" ||
			    sym == "BEGIN_INTERFACE_PART" || sym == "BEGIN_COM_MAP" || sym == "BEGIN_DISPATCH_MAP")
			{
				WTString defStr = TokenGetField(CurPos(), "\r\n");
				AddMarker(defStr, DEFINE, 0, GROUP_MSGMAP, FileOutlineFlags::ff_MessageMaps, m_curLine, (ULONG)m_cp);
				NodeIncDeep();
				mInMessageMap = 1;
				mInMessageMapDeep = m_deep;
			}
		}
		else if (mInMessageMap && m_deep == mInMessageMapDeep)
		{
			WTString defStr = TokenGetField(CurPos(), "\r\n");
			AddMarker(defStr, DEFINE, 0, GROUP_NONE, FileOutlineFlags::ff_MessageMaps, m_curLine, (ULONG)m_cp);
		}
		else
		{
			const WTString str = TokenGetField(CurPos());
			if (str == "using" && !InLocalScope())
			{
				if (CurFolderGroup() != GROUP_USING)
				{
					AddMarker(WTString("using"), DEFINE, 0, GROUP_USING, FileOutlineFlags::ff_IncludePseudoGroup,
					          m_curLine, (ULONG)m_cp);
					NodeIncDeep();
				}

				WTString defStr = TokenGetField(CurPos(), "\r\n");
				AddMarker(defStr, DEFINE, 0, GROUP_USING, FileOutlineFlags::ff_Includes, m_curLine, (ULONG)m_cp);
			}
			else
			{
				// [case: 30076] Code generation by macro (preprocessor) can confuse VA outline
				WTString macroName = GetCStr(CurPos());
				WTString macroNameAndParams;
				DType* pMacro = Psettings->mEnhanceVAOutlineMacroSupport ? GetMacro(macroName) : nullptr;

				if (pMacro)
				{
					int endOfMacro_m_cp = m_cp;
					enum SkipMode
					{
						MACRONAME,
						WHITESPACE, // between the name and a potential (
						OPENPAREN,
					};
					SkipMode mode = MACRONAME;
					for (int skip = m_cp; m_buf[skip] != 0; skip++)
					{
						TCHAR c = m_buf[skip];

						switch (mode)
						{
						case MACRONAME:
							if (ISCSYM(c))
								continue;
							mode = WHITESPACE;
						case WHITESPACE:
							if (IsWSorContinuation(c))
								continue;
							mode = OPENPAREN;
						case OPENPAREN:
							if (c == '(')
							{
								int paren = 0;
								for (int j = skip; m_buf[j] != 0; j++)
								{
									TCHAR c2 = m_buf[j];
									if (c2 == '(')
									{
										paren++;
										continue;
									}
									if (c2 == ')')
									{
										paren--;
										if (paren == 0)
										{
											for (int k = m_cp; k < j + 1; k++)
												macroNameAndParams += m_buf[k];

											endOfMacro_m_cp = j + 1;
											goto out;
										}
									}
								}
							}
							goto out;
						}
					}
				out:;

					WTString macTxt =
					    VAParseExpandMacrosInDef(m_mp, macroNameAndParams.IsEmpty() ? macroName : macroNameAndParams);
					macTxt.Trim();

					int keywordPos = ::FindWholeWordInCode(macTxt, "class", Src, 0);
					if (keywordPos == -1)
						keywordPos = ::FindWholeWordInCode(macTxt, "struct", Src, 0);
					if (keywordPos == -1)
						keywordPos = ::FindWholeWordInCode(macTxt, "namespace", Src, 0);

					if (keywordPos != -1 || (m_deep > 0 && (State(m_deep - 1).m_defAttr & V_VA_STDAFX)))
					{
						WTString lastWord;
						TCHAR prevChar = 0;
						int noAction = 0;

						for (int i = 0; i < macTxt.length(); i++)
						{
							MacroChar = macTxt[i];
							if (ISCSYM(MacroChar))
							{
								if (!ISCSYM(prevChar))
									lastWord = "";
								lastWord += MacroChar;
							}

							// based in part on the switch in VAParseKeywordSupport::OnCSym
							switch (MacroChar)
							{
							case 'c':
							case 'C':
								if (StartsWith(macTxt.c_str() + i, "class") && !StartsWith(State().m_begLinePos, "var"))
								{
									if (C_ENUM == State().m_defType && StartsWith(State().m_lastWordPos, "enum"))
										State().m_defAttr |= V_MANAGED; // [case: 935] C++ managed enum is defined like
										                                // "public enum class Bar..."
									else if (C_INTERFACE !=
									         State().m_defType) // [case: 65644] public interface class foo...
									{
										State().m_defType = CLASS;
										State().m_defAttr |= V_VA_STDAFX;
									}
								}
								break;
							case 's':
							case 'S':
								if (StartsWith(macTxt.c_str() + i, "struct"))
								{
									if (C_INTERFACE != State().m_defType) // [case: 65644] public struct class foo...
									{
										State().m_defType = STRUCT;
										State().m_defAttr |= V_VA_STDAFX;
									}
								}
								break;
							case 'n':
							case 'N':
								if (StartsWith(macTxt.c_str() + i, "namespace"))
								{
									// watch out for "using namespace foo".  Not type NAMESPACE.
									if (!StartsWith(State().m_begLinePos, "using"))
									{
										State().m_defType = NAMESPACE;
										State().m_defAttr |= V_VA_STDAFX;
									}
								}
								break;
							case '{':
								AddMarker(lastWord, (int)State().m_defType, 0, 0, 0, m_curLine, (ULONG)m_cp);

								State().m_lastChar = MacroChar;
								IncDeep();
								State(m_deep + 1).m_defType = UNDEF;

								State().m_lastWordPos = CurPos(); // points to beginning of last sym
								State().m_lastChar = CurChar();
								break;
							case '}':
								DecDeep();

								ClearLineState(CurPos());
								m_InSym = FALSE;
								break;
							default:
								noAction++;
							}
							prevChar = MacroChar;
						}

						MacroChar = 0;
						if (noAction < macTxt.length())
							m_cp = endOfMacro_m_cp;
					}
				}

				if (Psettings->mUnrealEngineCppSupport)
				{
					if (!str.IsEmpty() && str[0] == 'U' && !m_inMacro && VPS_BEGLINE == State().m_parseState &&
					    !mAttributePos && !InLocalScope())
					{
						if (UNDEF != ::GetTypeOfUnrealAttribute(str.c_str()))
						{
							// [case: 110511]
							// Check to see if UE attribute starts a line
							LPCSTR p;
							for (p = CurPos(); p > m_buf && (p[-1] == ' ' || p[-1] == '\t'); p--)
								;

							if (p == m_buf || p[-1] == '\r' || p[-1] == '\n')
							{
								// unreal engine attribute macros are treated by outline like c# [attributes]
								OnAttribute();
							}
						}
					}
				}
			}
		}
	}

	virtual WTString GetNodeText(LPCSTR symPos, ULONG* outFlags)
	{
		if (outFlags)
			*outFlags = 0;
		return TokenGetField(symPos, "\r\n");
	}

	virtual void OnDef()
	{
		// Add all global symbols
		if (mLastSymPtr == State().m_begLinePos)
			return;
		if (m_deep && State(m_deep - 1).m_lastChar == '<')
			return; // Don't show <class args> in tree
		if (m_inIFDEFComment)
			return; // [case: 83377]

		UINT defType = State().m_defType;
		_ASSERTE(defType == (State().m_defType & TYPEMASK));
		// In JS, you can define methods in methods making pseudo classes. Add these to the outline...
		BOOL force_JS_Methods = ShouldForceOnDef(); // (defType == FUNC && FileType() == JS);

		if (Is_Tag_Based(GetLangType()))
		{
			ULONG nodeFlags = 0;
			mLastSymPtr = State().m_begLinePos;
			WTString nodeText = GetNodeText(mLastSymPtr, &nodeFlags);

			FileOutlineFlags::DisplayFlag displayFlag = FileOutlineFlags::ff_None;
			if (nodeFlags & GNT_HIDE)
				displayFlag = FileOutlineFlags::ff_Hidden;
			if (nodeFlags & GNT_EXPAND)
				displayFlag = FileOutlineFlags::ff_Expanded;

			// no icon for TAG yet, use RESWORD for now.
			ULONG defType2 = State().m_defType;
			if (defType2 == TAG)
				defType2 = RESWORD;
			AddMarker(nodeText, (int)defType2, State().m_privilegeAttr, GROUP_NONE, displayFlag,
			          State().m_StatementBeginLine, ULONG(ConstState().m_begLinePos - m_buf),
			          mCollectClassData ? m_mp->FindExact(MethScope()) : NULL);
		}
		else if (force_JS_Methods ||
		         (defType == CLASS || defType == STRUCT) // check for structs/classes declared within a function.
		         || (!InLocalScope() && !m_inMacro))
		{
			FileOutlineFlags::DisplayFlag displayFlag = FileOutlineFlags::ff_None;
			if (!m_deep && strchr("[;=", CurChar()))
			{
				switch (defType)
				{
				case CLASS:
				case STRUCT:
				case C_INTERFACE:
					displayFlag = FileOutlineFlags::ff_FwdDecl;
					break;
				case VAR:
					if (StartsWith(State().m_begLinePos, "extern"))
						displayFlag = FileOutlineFlags::ff_FwdDecl;
					else
						displayFlag = FileOutlineFlags::ff_Globals;
					break;
				case FUNC: // strictly these are fwd decls, but don't treat as such
				default:
					break;
				}
			}
			else
			{
				switch (defType)
				{
				case C_ENUMITEM:
				case TEMPLATETYPE:
					return;

				case C_ENUM:
					displayFlag = FileOutlineFlags::ff_Enums;
					break;
				case VAR:
					displayFlag = FileOutlineFlags::ff_MembersAndVariables;
					break;
				case FUNC:
					displayFlag = FileOutlineFlags::ff_MethodsAndFunctions;
					break;
				case TYPE:
					displayFlag = FileOutlineFlags::ff_TypesAndClasses;
					break;
				case NAMESPACE: // [case: 27730] fix for multi-depth namespaces
				case CLASS:
				case STRUCT:
				case C_INTERFACE:
					// skip local-scope fwdDecls
					if (InLocalScope())
					{
						if (CurChar() == ';')
							return;
						if (State(m_deep - 1).m_lastChar == '(' && State(m_deep - 1).m_defType == FUNC)
							return; // [case: 66673]
					}
					break;
				default:
					break;
				}
			}

			mLastSymPtr = State().m_begLinePos;

			ULONG nodeFlags = 0;
			const WTString symScope = MethScope(); // [case: 96191]
			WTString sym = StrGetSym(symScope);
			WTString nodeText = GetNodeText(State().m_lastScopePos, &nodeFlags);
			if (nodeFlags & GNT_HIDE)
				displayFlag = FileOutlineFlags::ff_Hidden;
			if (nodeFlags & GNT_EXPAND)
				displayFlag = FileOutlineFlags::ff_Expanded;

			if (strncmp(sym.c_str(), "END_", 4) == 0)
			{
				// These are handled in OnCSym(), however they also trigger
				// OnDef() for some reason.  Ignore them here to avoid double
				// entries in the tree:
				if (sym == "END_MESSAGE_MAP" || sym == "END_SINK_MAP" || sym == "END_INTERFACE_MAP" ||
				    sym == "END_INTERFACE_PART" || sym == "END_COM_MAP")
				{
					return;
				}
			}

			if (defType == FUNC ||
			    (!Is_VB_VBS_File(GetLangType()) && GetLangType() != JS && !Is_Tag_Based(GetLangType()) &&
			     (defType == CLASS || defType == STRUCT || defType == C_INTERFACE)))
			{
				if (FUNC == defType) // [case: 96191]
					sym = TokenGetField(State().m_lastScopePos, "([{<;");

				// if def line doesn't have all args, then scan to close-paren
				// 				if (nodeText.Find("{") < 0 &&
				// 					nodeText.Find(";") < 0 &&	//
				// 					nodeText.Find(")") < 0)		// for macro fns with no final ';'
				{
					if (Is_VB_VBS_File(GetLangType()))
						nodeText = TokenGetField(State().m_lastScopePos, "\r\n");
					else
					{
						nodeText = TokenGetField(State().m_lastScopePos, "{;");
						if (CLASS == defType)
						{
							// [case: 67035]
							nodeText.ReplaceAll(" sealed", "");
							nodeText.ReplaceAll(" abstract", "");
						}

						int n = nodeText.length();
						if (State().m_lastScopePos[n] == ';')
						{
							nodeText += ';';
							++n;

							// look for '//' comments following node
							LPCSTR tmpStr = State().m_lastScopePos;
							while (strchr(" \t", tmpStr[n]))
								++n;
							if (tmpStr[n] == '/' && tmpStr[n + 1] == '/')
								nodeText = TokenGetField(State().m_lastScopePos, "\r\n");
						}

						const int closePos = nodeText.Find(')');
						if (-1 != closePos)
						{
							const int openPos = nodeText.Find('(');
							if (-1 == openPos || closePos < openPos)
							{
								// [case: 12803]
								WTString newTxt(nodeText.Left(closePos));
								newTxt += nodeText.Mid(closePos + 1);
								nodeText = newTxt;
							}
						}
					}
				}

				// replace all whitespace with a single space
				nodeText.Trim();
				nodeText = StripExtraWhiteChars(nodeText);
			}
			else if (defType == VAR)
			{
				int pos = nodeText.Find(";");
				if (pos > 0)
					nodeText = nodeText.Left(pos + 1);
			}

			// if def line has open-brace but not closed-brace, then strip brace
			int openBracePos = nodeText.Find('{');
			if (openBracePos >= 0 && nodeText.Find('}') < 0)
				nodeText = nodeText.Mid(0, openBracePos);

			UINT grouping = GROUP_NONE;

			if (displayFlag == FileOutlineFlags::ff_FwdDecl)
			{
				if (JS != GetLangType())
				{
					grouping = GROUP_FWDDECL;
					if (CurFolderGroup() != GROUP_FWDDECL)
					{
						if (AddMarker("Forward declarations", (int)State().m_defType, State().m_privilegeAttr,
						              GROUP_FWDDECL, FileOutlineFlags::ff_FwdDeclPseudoGroup,
						              State().m_StatementBeginLine, ULONG(ConstState().m_begLinePos - m_buf)))
							NodeIncDeep();
					}
				}
			}
			else if (displayFlag == FileOutlineFlags::ff_Globals)
			{
				if (JS != GetLangType())
				{
					grouping = GROUP_GLOBALS;
					if (CurFolderGroup() != GROUP_GLOBALS)
					{
						if (AddMarker("File scope variables", (int)State().m_defType, State().m_privilegeAttr,
						              GROUP_GLOBALS, FileOutlineFlags::ff_GlobalsPseudoGroup,
						              State().m_StatementBeginLine, ULONG(ConstState().m_begLinePos - m_buf)))
							NodeIncDeep();
					}
				}
			}
			else if (sym.contains("::"))
			{
				// handle scoped implementations (usually in source file)
				WTString sscope;
				int brkPos = sym.Find("operator");
				if (brkPos != -1)
				{
					// "foo::operator std::string()" should have scope "foo:"
					// StrGetSymScope shouldn't be passed multiple symbols
					sscope = StrGetSymScope(sym.Left(brkPos));
				}
				else
					sscope = StrGetSymScope(sym);

				if (sscope.EndsWith(":"))
					sscope = sscope.Left(sscope.length() - 1);
				sscope.Trim();

				if (sscope.GetLength())
				{
					UINT sscope_hash = WTHashKey(sscope);
					grouping = GROUP_METHODS_ITEM;

					if (CurFolderGroup() == GROUP_METHODS)
					{
						if (sscope_hash != mMethodGroupHash)
						{
							NodeDecDeep();
							mMethodGroupHash = 0;
						}
					}

					if (CurFolderGroup() != GROUP_METHODS)
					{
						sscope += " methods";
						mMethodGroupHash = sscope_hash;
						if (AddMarker(sscope, (int)State().m_defType, State().m_privilegeAttr, GROUP_METHODS,
						              FileOutlineFlags::ff_MethodsPseudoGroup, State().m_StatementBeginLine,
						              ULONG(ConstState().m_begLinePos - m_buf)))
							NodeIncDeep();
					}
				}
			}

			LPCSTR nodeStartPos = ConstState().m_begLinePos;
			ULONG nodeStartLine = ConstState().m_StatementBeginLine;

			if (mAttributePos)
			{
				nodeStartPos = mAttributePos;
				nodeStartLine = mAttributeLine;
			}
			else if (mDeclSpecPos)
			{
				nodeStartPos = mDeclSpecPos;
			}

			DType symToUse;
			if (mCollectClassData)
			{
				int valuesFound = 0;
				DTypeList dList;
				m_mp->FindExactList(MethScope(), dList);
				for (DTypeList::iterator iter = dList.begin(); iter != dList.end(); ++iter)
				{
					const DType& dt = *iter;
					if (dt.MaskedType() == State().m_defType && dt.FileId() == m_mp->GetFileID())
					{
						// find dtype closest to m_curLine
						int ln = GetLineNumber(defType, "", sym);
						if (dt.Line() >= ln)
						{
							if (valuesFound > 0)
							{
								if (dt.Line() < symToUse.Line())
									symToUse = DType(dt);
							}
							else
							{
								symToUse = DType(dt);
							}
							++valuesFound;
						}
					}
				}

#if 0
				DType *oldSym = m_mp->FindExact(MethScope());
				if (oldSym && oldSym->Line() != symToUse.Line())
				{
					int xxx = 0;
				}
#endif
			}

			AddMarker(nodeText, (int)State().m_defType, State().m_privilegeAttr, grouping, displayFlag, nodeStartLine,
			          ULONG(nodeStartPos - m_buf), &symToUse);
			mDeclSpecPos = NULL;
			mAttributePos = NULL;
			mAttributeLine = 0;
		}
	}

	virtual void OnAttribute() override
	{
		if (!mAttributePos)
		{
			mAttributePos = ConstState().m_begLinePos;
			mAttributeLine = ConstState().m_StatementBeginLine;
		}
	}

	virtual BOOL IsTag()
	{
		// Add public:/private:/protected:
		BOOL istag = VAParseFileOutlineMarkers::IsTag();
		if (istag)
		{
			// watch for initialization lists in ctors "Foo() : mFoo(0) {}"
			_ASSERTE((State().m_defType & TYPEMASK) == State().m_defType);
			const bool inCtor = State().m_defType == FUNC;

			if (!inCtor && !m_inMacro && !InLocalScope())
			{
				WTString tag = TokenGetField(State().m_lastWordPos, "\r\n");
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
				if (!tag.IsEmpty() && tag[0] == '_' && tag == "__published:")
				{
					// [case: 135860] display without underscores
					tag = "published:";
				}
#endif
				if (CurFolderGroup() == GROUP_TAG)
				{
					NodeDecDeep();
					AddComment();
				}

				if (AddMarker(tag, RESWORD, 0, GROUP_TAG, FileOutlineFlags::ff_TagsAndLabels, m_curLine,
				              ULONG(ConstState().m_begLinePos - m_buf)))
					NodeIncDeep();
			}
		}
		return istag;
	}

  protected:
	VAParseFileOutline(int fType)
	    : VAParseFileOutlineMarkers(fType)
	{
		mInMessageMap = 0;
		mInMessageMapDeep = 0;
		mLastSymPtr = NULL;
		mMethodGroupHash = 0;
		mCollectClassData = TRUE;
		mMaxLines = 0;
	}
};

CREATE_MLC(VAParseFileOutline, VAParseFileOutline_MLC);

BOOL GetFileOutline(const WTString& fileText, LineMarkers& markers, MultiParsePtr mparse)
{
	VAParseFileOutline_MLC mps(mparse->FileType());
	return mps->BuildOutline(fileText, mparse, markers, false);
}

BOOL GetFileOutline(const WTString& fileText, LineMarkers& markers, MultiParsePtr mparse, ULONG maxLines,
                    BOOL collectDtypes)
{
	VAParseFileOutline_MLC mps(mparse->FileType());
	return mps->BuildOutline(fileText, mparse, markers, maxLines, collectDtypes);
}

void PV_LogNode(FILE* fp, LineMarkers::Node& n, int depth)
{
	FileLineMarker& mkr = n.Contents();
	fprintf(fp, "%u\t%lu\t%lu\t%lu\t%lu\t%.8lx\t%.8lx\t%.8lx\t%s\n", depth, mkr.mStartLine, mkr.mStartCp, mkr.mEndLine,
	        mkr.mEndCp, mkr.mType, mkr.mAttrs, mkr.mDisplayFlag, WTString(mkr.mText).c_str());
	for (uint i = 0; i < n.GetChildCount(); ++i)
	{
		PV_LogNode(fp, n.GetChild(i), depth + 1);
	}
}

BOOL LogFileOutline(const WTString& fileText, MultiParsePtr mparse, const CStringW& logFilePath)
{
	BOOL rslt = TRUE;
	LineMarkers markers;
	VAParseFileOutline_MLC mps(mparse->FileType());
	BOOL res = mps->BuildOutline(fileText, mparse, markers, true);

	FILE* fp = _wfopen(logFilePath, L"wb");
	if (fp)
	{
		PV_LogNode(fp, markers.Root(), 0);
		if (!res)
			fprintf(fp, "Warn: truncated results\n");
		fclose(fp);
	}
	else
	{
		rslt = FALSE;
	}

	return rslt;
}

BOOL LogMethodsInFile(const WTString& fileText, MultiParsePtr mparse, const CStringW& logFilePath)
{
	BOOL rslt = TRUE;
	LineMarkers markers;
	BOOL res = GetMethodsInFile(fileText, mparse, markers);

	FILE* fp = _wfopen(logFilePath, L"wb");
	if (fp)
	{
		PV_LogNode(fp, markers.Root(), 0);
		if (!res)
			fprintf(fp, "Warn: truncated results\n");
		fclose(fp);
	}
	else
	{
		rslt = FALSE;
	}

	return rslt;
}

//////////////////////////////////////////////////////////////////////////
// Used to get the Offset of "Foo:Bar", used for MRU
class FindScopePosCls : public VAParseDirectiveC
{
	WTString m_scope;
	LPCSTR m_begLinePos;

  protected:
	FindScopePosCls(int fType)
	    : VAParseDirectiveC(fType)
	{
	}

  public:
	int FindScopePos(const WTString& buf, LPCSTR scope)
	{
		m_begLinePos = 0;
		m_scope = scope;
		DoParse(buf, PF_TEMPLATECHECK);
		if (m_begLinePos)
			return ptr_sub__int(m_begLinePos, m_buf);
		return -1;
	}
	virtual void IncDeep()
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		VAParseDirectiveC::IncDeep();
		if (!m_begLinePos)
		{
			const WTString scp(Scope());
			if (scp == m_scope)
			{
				m_begLinePos = State(m_deep - 1).m_begLinePos;
			}
		}
	}
	virtual BOOL IsDone()
	{
		_ASSERTE(m_cp <= mBufLen);
		return m_begLinePos || VAParseDirectiveC::IsDone();
	}
};
CREATE_MLC(FindScopePosCls, FindScopePosCls_MLC);

int FindScopePos(const WTString& buf, LPCSTR scope, int lang)
{
	WTString tbuf(buf);
	FindScopePosCls_MLC gsl(lang);
	return gsl->FindScopePos(tbuf, scope);
}

bool ClearAutoHighlights()
{
	bool maybeHadHighlights =
	    Psettings->mAutoHighlightRefs && (!s_HighlightData_SymScope.IsEmpty() || !s_HighlightData_Sym.IsEmpty());
	if (!maybeHadHighlights && Psettings->mUseAutoHighlightRefsThread && gAutoReferenceHighlighter)
		maybeHadHighlights = gAutoReferenceHighlighter->Count() > 0;
	s_HighlightData_SymScope.Empty();
	s_HighlightData_Sym.Empty();
	s_HighlightWord.Empty();
	if (gAutoReferenceHighlighter)
		gAutoReferenceHighlighter->ClearSym();
	g_ScreenAttrs.Invalidate(SA_REFERENCE_ASSIGN_AUTO);
	g_ScreenAttrs.Invalidate(SA_REFERENCE_AUTO);
	return maybeHadHighlights;
}

LiteralType GetLiteralType(int langType, const WTString& txt, char literalDelimiter)
{
	const int kLen = txt.GetLength();
	if (kLen < 2)
		return LiteralType::None;

	if (IsCFile(langType))
	{
		if (txt.Find("T(") != -1)
		{
			if (txt.begins_with2("T(", literalDelimiter) || txt.begins_with2("_T(", literalDelimiter) ||
			    txt.begins_with2("__T(", literalDelimiter))
				return LiteralType::MacroT;

			if (txt.Find("TEXT") != -1)
			{
				if (txt.begins_with2("TEXT(", literalDelimiter) || txt.begins_with2("_TEXT(", literalDelimiter) ||
				    txt.begins_with2("__TEXT(", literalDelimiter))
					return LiteralType::MacroT;
			}
		}

		bool isRaw = false;
		bool isStr = false;
		bool isChar = false;
		int charsToSkip = 0;
		WTString rawStringTerminator;
		LiteralType litType = LiteralType::None;

		::GetCppStringInfo(txt, isStr, isChar, isRaw, charsToSkip, rawStringTerminator, litType);
		if (litType != LiteralType::None)
		{
			if (literalDelimiter == '\'' && isChar)
				return litType;
			if (literalDelimiter == '"' && isStr)
			{
				int pos = txt.ReverseFind('"');
				int spos = txt.ReverseFind("\"s");
				if (-1 != spos && spos == pos)
				{
					switch (litType)
					{
					case LiteralType::Lraw:
					case LiteralType::Wide:
						return LiteralType::StdWString;
					case LiteralType::Wide16:
						return LiteralType::Stdu16String;
					case LiteralType::Wide32:
						return LiteralType::Stdu32String;
					default:
						return LiteralType::StdString;
					}
				}

				return litType;
			}
		}
	}

	if (txt[0] == literalDelimiter)
		return LiteralType::Unadorned;

	if (txt[1] == literalDelimiter && Is_C_CS_VB_File(langType))
	{
		if (txt[0] == 'S' || txt[0] == '@' || txt[0] == '$')
			return LiteralType::Wide;
	}

	return LiteralType::None;
}

LPCSTR
GetLiteralTypename(int langType, const WTString& txt)
{
	char literalDelimiter = '"';
	LiteralType lt = GetLiteralType(langType, txt, literalDelimiter);
	if (LiteralType::None == lt)
	{
		literalDelimiter = '\'';
		lt = GetLiteralType(langType, txt, literalDelimiter);
		if (LiteralType::None == lt)
			return nullptr;
	}

	if (LiteralType::Wide == lt)
	{
		if (literalDelimiter == '\'')
		{
			if (IsCFile(langType))
				return "WCHAR"; // "wchar_t"
			return "Char";
		}

		if (literalDelimiter == '"')
		{
			if (IsCFile(langType))
				return "LPCWSTR";
			return "String";
		}
	}
	else if (LiteralType::Unadorned == lt)
	{
		if (literalDelimiter == '\'')
		{
			if (IsCFile(langType))
				return "char";
			return "Char";
		}

		if (literalDelimiter == '"')
		{
			if (IsCFile(langType))
				return "LPCSTR";
			return "String";
		}
	}
	else if (LiteralType::Lraw == lt)
	{
		if (literalDelimiter == '\'')
			return "wchar_t";
		if (literalDelimiter == '"')
			return "const wchar_t *";
	}
	else if (LiteralType::Wide16 == lt)
	{
		if (literalDelimiter == '\'')
			return "char16_t";
		if (literalDelimiter == '"')
			return "const char16_t *";
	}
	else if (LiteralType::Wide32 == lt)
	{
		if (literalDelimiter == '\'')
			return "char32_t";
		if (literalDelimiter == '"')
			return "const char32_t *";
	}
	else if (LiteralType::Narrow == lt)
	{
		if (literalDelimiter == '\'')
			return "char";
		if (literalDelimiter == '"')
			return "const char *";
	}
	else if (LiteralType::UnadornedRaw == lt)
	{
		if (literalDelimiter == '\'')
			return "char";
		if (literalDelimiter == '"')
			return "const char *";
	}
	else if (LiteralType::MacroT == lt)
	{
		if (literalDelimiter == '\'')
			return "TCHAR";
		if (literalDelimiter == '"')
			return "LPCTSTR";
	}
	else if (LiteralType::StdString == lt)
		return "std::string";
	else if (LiteralType::StdWString == lt)
		return "std::wstring";
	else if (LiteralType::Stdu16String == lt)
		return "std::u16string";
	else if (LiteralType::Stdu32String == lt)
		return "std::u32string";

	return nullptr;
}

LPCSTR
SimpleTypeFromText(int langType, const WTString& txt)
{
	if (txt == "NULL" || txt == "null" || txt == "nullptr")
	{
		if (IsCFile(langType))
			return "void*";
		else
			return "Object"; // ?
	}

	if (txt == "true" || txt == "false")
		return "bool";

	if (txt == "True" || txt == "False")
		return "boolean";

	if (txt == "TRUE" || txt == "FALSE")
		return "BOOL";

	if (IsCFile(langType) && (txt == "S_OK" || txt == "S_FALSE"))
		return "HRESULT";

	LPCSTR tmp = GetLiteralTypename(langType, txt);
	if (tmp)
		return tmp;

	if (txt[0] == '0')
	{
		// not enforcing correct/appropriate values after prefix...
		if (txt[1] == 'x' || txt[1] == 'X')
		{
			if (IsCFile(langType))
				return "DWORD";

			return "int";
		}

		if (IsCFile(langType))
		{
			if (txt[1] == 'b' || txt[1] == 'B')
			{
				// https://en.wikipedia.org/wiki/C%2B%2B14#Binary_literals
				return "DWORD";
			}
		}
	}

	{
		enum class SuffixType
		{
			none,
			nanoseconds,
			microseconds,
			milliseconds,
			seconds,
			minutes,
			hours,
			_unsigned,
			_long,
			_ulong,
			_longlong,
			_ulonglong,
			_float,
			_double
		};
		bool hasNumbers = false;
		bool hasText = false;
		bool hasDec = false;
		bool hasOp = false;
		bool hasDivide = false;
		bool hasOther = false;
		SuffixType hasSuffixType = SuffixType::none;

		bool cwdHasNumbers = false;
		bool cwdHasText = false;
		SuffixType cwdHasSuffixType = SuffixType::none;

		for (int idx = 0; idx < txt.GetLength(); ++idx)
		{
			if (wt_isdigit(txt[idx]))
			{
				if (!cwdHasText)
					cwdHasNumbers = true;
			}
			else if (wt_isalpha(txt[idx]))
			{
				const char ch = txt[idx];
				if (cwdHasText)
					;
				else if (cwdHasNumbers && ch == 'n' && txt[idx + 1] == 's')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::nanoseconds;
					else
						cwdHasText = true;
					++idx;
				}
				else if (cwdHasNumbers && ch == 'u' && txt[idx + 1] == 's')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::microseconds;
					else
						cwdHasText = true;
					++idx;
				}
				else if (cwdHasNumbers && ch == 'm' && txt[idx + 1] == 's')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::milliseconds;
					else
						cwdHasText = true;
					++idx;
				}
				else if (cwdHasNumbers && ch == 's')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::seconds;
					else
						cwdHasText = true;
				}
				else if (cwdHasNumbers && ch == 'h')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::hours;
					else
						cwdHasText = true;
				}
				else if (cwdHasNumbers && ch == 'm' && txt[idx + 1] == 'i' && txt[idx + 2] == 'n')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::minutes;
					else
						cwdHasText = true;
					idx += 2;
				}
				else if (cwdHasNumbers && (ch == 'u' || ch == 'U'))
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::_unsigned;
					else
						cwdHasText = true;
				}
				else if (cwdHasNumbers && (ch == 'l' || ch == 'L'))
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::_long;
					else if (cwdHasSuffixType == SuffixType::_unsigned)
						cwdHasSuffixType = SuffixType::_ulong;
					else if (cwdHasSuffixType == SuffixType::_ulong)
						cwdHasSuffixType = SuffixType::_ulonglong;
					else if (cwdHasSuffixType == SuffixType::_long)
						cwdHasSuffixType = SuffixType::_longlong;
					else
						cwdHasText = true;
				}
				else if ((cwdHasNumbers || hasDec) && ch == 'f')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::_float;
					else
						cwdHasText = true;
				}
				else if ((cwdHasNumbers || hasDec) && ch == 'd')
				{
					if (cwdHasSuffixType == SuffixType::none)
						cwdHasSuffixType = SuffixType::_double;
					else
						cwdHasText = true;
				}
				else
				{
					if (cwdHasSuffixType != SuffixType::none)
						cwdHasSuffixType = SuffixType::none;
					cwdHasText = true;
				}
			}
			else if (txt[idx] == '.')
				hasDec = true;
			else if (strchr("()*+-^%/ \t\r\n", txt[idx]))
			{
				// cwd delimiter
				if (txt[idx] == '/')
					hasDivide = true;
				else if (wt_isspace(txt[idx]))
					;
				else
					hasOp = true;

				if (cwdHasText)
					hasText = true;
				else if (cwdHasNumbers)
					hasNumbers = true;
				if (cwdHasSuffixType != SuffixType::none)
					hasSuffixType = cwdHasSuffixType;

				cwdHasNumbers = cwdHasText = false;
				cwdHasSuffixType = SuffixType::none;
			}
			else if (txt[idx] == '\'')
			{
				// [case: 86379] single-quote digit separators
				if (!cwdHasNumbers)
				{
					hasOther = true;
					break;
				}
			}
			else
			{
				hasOther = true;
				break;
			}
		}

		if (cwdHasText)
			hasText = true;
		else if (cwdHasNumbers)
			hasNumbers = true;
		if (hasSuffixType == SuffixType::none)
			hasSuffixType = cwdHasSuffixType;

		if (hasNumbers && !hasOther)
		{
			if (IsCFile(langType))
			{
				switch (hasSuffixType)
				{
				case SuffixType::nanoseconds:
					return "std::chrono::nanoseconds";
				case SuffixType::microseconds:
					return "std::chrono::microseconds";
				case SuffixType::milliseconds:
					return "std::chrono::milliseconds";
				case SuffixType::seconds:
					return "std::chrono::seconds";
				case SuffixType::minutes:
					return "std::chrono::minutes";
				case SuffixType::hours:
					return "std::chrono::hours";
				case SuffixType::_longlong:
					return "long long";
				case SuffixType::_ulonglong:
					return "unsigned long long";
				default:
					break;
				}

				// fall through to shared types
			}

			switch (hasSuffixType)
			{
			case SuffixType::_long:
				return "long";
			case SuffixType::_ulong:
				if (IsCFile(langType))
					return "unsigned long";
				return "ulong";
			case SuffixType::_unsigned:
				if (IsCFile(langType))
					return "unsigned int";
				return "uint";
			case SuffixType::_float:
				return "float";
			case SuffixType::_double:
				return "double";
			case SuffixType::none:
				if (hasDivide || hasDec)
					return "double";
				if (hasOp || !hasText)
					return "int";
				return nullptr;
			default:
				// something that made sense for C/C++ but that is not the current language
				return "int";
			}
		}
	}

	return nullptr;
}

ParseToEndBlock::ParseToEndBlock(int fType, LPCSTR buf, int bufLen, int startPos)
    : VAParse(fType)
{
	Init(buf, bufLen);
	m_cp = startPos;
	_ASSERTE(m_buf[m_cp] ==
	         '{'); // the class may work without this condition, but it hasn't been tested to guarantee it
	DoParse();
	_ASSERTE(mIsDone || m_cp >= mBufLen);
}

void ParseToEndBlock::OnEveryChar()
{
	if (InComment())
		return;

	const char c = CurChar();
	if (c == '(')
		++mParenStack;
	else if (c == ')')
	{
		if (mParenStack)
			--mParenStack;
	}
	else if (mParenStack)
		;
	else if ('{' == c)
	{
		if (!mBlockStack++)
		{
			mHadComma = false;
			mOpenPos = m_cp;
		}
	}
	else if ('}' == c)
	{
		--mBlockStack;
		if (!mBlockStack)
			mClosePos = m_cp;
		else if (mBlockStack < 0)
			mIsDone = true; // class foo { Method() { } }
	}
	else if (::wt_isspace(c))
		;
	else if (',' == c)
	{
		// ctor : mem1{1}, mem2{2} { }
		if (!mBlockStack)
			mHadComma = true;
	}
	else if (!mBlockStack && !mHadComma)
		mIsDone = true; // any char other than {,} means we passed end of block
}

BOOL ParseToEndBlock::IsDone()
{
	if (mIsDone)
	{
		_ASSERTE(m_deep <= 1);
		return TRUE;
	}

	BOOL ret = __super::IsDone();
	if (ret)
		mIsDone = TRUE;

	return ret;
}

WTString TemplateInstanceArgsReader::GetTemplateInstanceArgs(const WTString& buf, int pos /*= 0*/)
{
	mIsDone = false;
	mTemplateTerminators = -1;
	mArgs.Empty();

	ReadTo(&buf.c_str()[pos], ";{");

	// value of 0 means we read from '<' to a matching '>'
	if (0 == mTemplateTerminators)
	{
		WTString res(mArgs);
		res.ReplaceAll(":", "::");
		// encode/decode makes spacing consistent w/ va rules/style
		EncodeTemplates(res);
		res = DecodeTemplates(res, GetLangType());
		return res;
	}

	return WTString();
}

void TemplateInstanceArgsReader::IncDeep()
{
	if (0 == m_deep && CurChar() == '<')
	{
		mTemplateTerminators = 1;
		mArgs += "<";
	}

	__super::IncDeep();
}

void TemplateInstanceArgsReader::DecDeep()
{
	__super::DecDeep();

	if (0 == m_deep && CurChar() == '>' && PrevChar() != '=')
	{
		--mTemplateTerminators;
		mIsDone = true;
	}
}

void TemplateInstanceArgsReader::OnChar()
{
	if (!InComment())
	{
		if (mTemplateTerminators == 1)
		{
			char curCh = CurChar();
			if (strchr("\r\n\t", curCh))
				curCh = ' ';
			mArgs += curCh;
		}
	}

	__super::OnChar();
}

// skips matching characters inside comments, strings and preprocessor macro definitions (i.e. lines beginning with #)
int FindInCode(const WTString& code, TCHAR ch, int fileType, int pos /*=0*/)
{
	if (-1 == pos)
		return -1;

	CommentSkipper commentSkipper(fileType);
	for (int i = pos; i < code.GetLength(); i++)
	{
		TCHAR currCh = code[i];
		if (commentSkipper.IsCode(currCh) && ch == currCh)
			return i;
	}

	return -1;
}

// skips matching substrings inside comments, strings and preprocessor macro definitions (i.e. lines beginning with #)
int FindInCode(const WTString& code, const WTString& sub, int fileType, int pos)
{
	if (-1 == pos)
		return -1;

	CommentSkipper cs(fileType);
	for (int i = pos; i < code.GetLength() - sub.GetLength(); i++)
	{
		if (cs.IsCode(code[i]))
		{
			for (int j = 0; j < sub.GetLength(); j++)
			{
				if (code[i + j] != sub[j])
					goto next_i;
			}
			return i;
		next_i:;
		}
	}

	return -1;
}

int FindWholeWordInCode(const WTString& code, const WTString& sub, int fileType, int pos)
{
	if (-1 == pos)
		return -1;

	CommentSkipper cs(fileType);
	for (int i = pos; i <= code.GetLength() - sub.GetLength(); i++)
	{
		if (cs.IsCode(code[i]))
		{
			for (int j = 0; j < sub.GetLength(); j++)
			{
				if (code[i + j] != sub[j])
					goto next_i;
			}
			if (i + sub.GetLength() == code.GetLength() || !ISCSYM(code[i + sub.GetLength()]))
				if (i == 0 || !ISCSYM(code[i - 1]))
					return i;
		next_i:;
		}
	}

	return -1;
}

int FindSymInBCL(const WTString& bcl, const WTString& sym)
{
	for (int i = 0; i <= bcl.GetLength() - sym.GetLength(); i++)
	{
		for (int j = 0; j < sym.GetLength(); j++)
		{
			if (bcl[i + j] != sym[j])
				goto next_i;
		}
		if (i + sym.GetLength() == bcl.GetLength() || bcl[i + sym.GetLength()] == '\f')
			if (i == 0 || bcl[i - 1] == '\f')
				return i;
	next_i:;
	}

	return -1;
}

std::pair<int, int> FindInCode_Skipwhitespaces(const WTString& code, const WTString& compactExpression,
                                               std::pair<int, int> posAndLen, int correspondingBrace, int fileType)
{
	int till = correspondingBrace - compactExpression.GetLength();
	CommentSkipper cs(fileType);
	for (int i = posAndLen.first; i < till; i++)
	{
		if (!cs.IsCode(code[i]))
			continue;
		int counter = 0;
		int length = compactExpression.GetLength();
		if (i != 0)
		{
			if (ISCSYM(code[i - 1])) // for whole word search
				goto next_i;
		}
		for (int j = 0; j < length; j++)
		{
			while (j > 0 && i + counter < code.GetLength() && IsWSorContinuation(code[i + counter]))
				counter++;
			if (code[i + counter] != compactExpression[j])
				goto next_i;
			counter++;
		}
		if (i + counter <= correspondingBrace &&
		    (i + counter == code.GetLength() || !ISCSYM(code[i + counter]))) // for whole word search
			return std::pair<int, int>(i, counter);
	next_i:;
	}

	return std::pair<int, int>(-1, 0);
}

void ClearParserGlobals()
{
	// must release DTypePtrs before the DType heap is closed
	for (uint idx = 0; idx < STATE_COUNT; ++idx)
	{
		VAParseBase::ParserState& ps = gCachedParseMpInstance.State(idx);
		ps.m_lwData.reset();
	}

	AutoLockCs lock(sCacheLock);
	s_lastMp = nullptr;
	gCachedParseMpInstance.m_mp = nullptr;
	gActiveUnderliner.reset();
}
