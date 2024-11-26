#include "stdafxed.h"
#include "edcnt.h"
#include "VaMessages.h"
#include "VAParse.h"
#include "VACompletionSet.h"
#include "rbuffer.h"
#include "Settings.h"
#include "ScopeInfo.h"
#include "FileTypes.h"
#include "file.h"
#include "PooledThreadBase.h"
#include "MainThread.h"
#include "Guesses.h"
#include "project.h"
#include "AutotextManager.h"
#include "VASeException\VASeException.h"
#include "FileLineMarker.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// MiniHelp
BOOL MiniHelpInfo::UpdateMiniHelp()
{
	if (HasMiniHelpInfo())
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			ed->UpdateMinihelp((LPCWSTR)m_MiniHelpContext.Wide(), (LPCWSTR)m_MiniHelpDef.Wide());
		}
		return TRUE;
	}
	return FALSE;
}

void MiniHelpInfo::SetMiniHelpInfo(LPCSTR context, LPCSTR def, int type)
{
	m_MiniHelpType = type;
	if (type && m_MiniHelpContext != context)
	{
		m_MiniHelpContext = context;
		m_MiniHelpDef = def;
#define ID_KEYUP_TIMER 131
		EdCntPtr ed(g_currentEdCnt);
		if (type && ed)
			ed->SetTimer(ID_KEYUP_TIMER, 500, NULL);
	}
}

BOOL MiniHelpInfo::GoToDef()
{
	if (HasMiniHelpInfo())
	{
		const CStringW maybeFile(m_MiniHelpDef.Wide());
		if (IsFile(maybeFile))
			DelayFileOpen(maybeFile, 0, NULL, TRUE);
		else
			::GoToDEF(m_MiniHelpContext);
		return TRUE;
	}
	return FALSE;
}

// ScopeInfo
ScopeInfo::ScopeInfo(int fType) : m_ftype(fType)
{
	InitScopeInfo();
}

ScopeInfo::ScopeInfo()
{
	m_ftype = gTypingDevLang;
	InitScopeInfo();
}

void ScopeInfo::InitScopeInfo()
{
	m_isDef = FALSE;
	m_Scope = m_LastWord = m_baseClassList = m_stringText = NULLSTR;
	// Init MParse members
	m_isDef = m_xref = m_inParamList = m_isMethodDefinition = m_inClassImplementation = false;
	m_lastScope = m_baseClass = m_baseClassList = m_firstWord = m_argTemplate = m_argScope = m_xrefScope =
	    m_ScopeSuggestions = "";
	m_firstVisibleLine = 0;
	m_lastErrorPos = 0;
	ClearCwData();

	mFormatAttrs = 0;
	m_suggestionType = 0;
	mScopeSuggestionMode = smNone;
	m_MethodName = m_NamespaceName = m_ClassName = m_MethodArgs = "";
	m_commentSubType = m_commentType = '\0';
	SetMiniHelpInfo(NULL, NULL, 0);
}
BOOL FindListFromTextHTML(class SuggestionClass* suggestionClass, LPCSTR buf, uint offset);
BOOL FindListFromTextJS(SuggestionClass* suggestionClass, LPCSTR buf, uint offset);
static int s_HintThreadCookie = 0;

class SuggestionClass
{
	EdCntPtr m_ed;
	WTString m_cwd;
	WTString m_rbuf;
	WTString m_buf;
	WTString m_LastWord;
	uint m_offset;
	int m_HintThreadCookie;
	int mScopeInfoSuggestionType = 0;
	BOOL m_isSnippet;

  public:
	int m_listAll;
	WTString m_scope, m_bcl;
	BOOL IsBestGuess(WTString& txt)
	{
		// determin ranking;
		int rpos = m_rbuf.ReverseFind((SPACESTR + txt).c_str());
		if (rpos != -1)
		{
			m_rbuf = m_rbuf.Mid(rpos);
			return TRUE;
		}
		return FALSE;
	}

  public:
	virtual BOOL ShouldBail()
	{
		if (m_HintThreadCookie == s_HintThreadCookie)
			return TRUE;
		if (!m_listAll && g_Guesses.GetCount() >= Psettings->m_nDisplayXSuggestions)
			return TRUE;
		return FALSE;
	}
	virtual void OnFound(WTString txt, UINT type)
	{
		if (!(mScopeInfoSuggestionType & SUGGEST_TEXT))
		{
			MultiParsePtr mp(m_ed->GetParseDb());
			DType* data = mp->FindAnySym(txt);
			if (!data)
				return;
			type = data->MaskedType();
		}
		WTString entry = CompletionSetEntry(txt, type);
		if (!g_Guesses.Contains(entry))
		{
			if (type != ET_SUGGEST_BITS && IsBestGuess(txt))
				g_Guesses.AddTopGuess(entry);
			else
				g_Guesses.AddGuess(entry);
		}
	}
	SuggestionClass(EdCntPtr ed)
	{
		// Call only from main thread
		ASSERT_ONCE(g_mainThread == ::GetCurrentThreadId());
		m_ed = ed;
		bool scopeInfoXref;
		{
			ScopeInfoPtr si(ed->ScopeInfoPtr());
			// copy so the values do not change running in thread
			mScopeInfoSuggestionType = si->m_suggestionType;
			scopeInfoXref = si->m_xref;
			m_LastWord = si->m_LastWord;
		}
		m_buf = m_ed->GetBuf();
		m_rbuf = g_rbuffer.GetStr();
		m_offset = (uint)m_ed->GetBufIndex(m_buf, (long)m_ed->CurPos());
		uint bwPos = m_offset;
		for (; bwPos && ISCSYM(m_buf[bwPos - 1]); bwPos--)
			;
		m_cwd = m_buf.Mid((int)bwPos, int(m_offset - bwPos));
		MultiParsePtr mp(m_ed->GetParseDb());
		mp->CwScopeInfo(m_scope, m_bcl, m_ed->m_ScopeLangType);
		m_bcl = "";
		m_HintThreadCookie = s_HintThreadCookie++;
		m_listAll = m_ed->m_typing == false;
		int autotextItemIdx = -1;
		m_isSnippet =
		    (!scopeInfoXref && (Psettings->m_codeTemplateTooltips &&
		                                 gAutotextMgr->FindNextShortcutMatch(m_cwd, autotextItemIdx).GetLength()));
	}
	virtual ~SuggestionClass() = default;

	void DoSuggestions()
	{

		// Thread safe.
		AutoLockCs l(g_Guesses.GetLock());
		g_Guesses.Reset();
		int suggestionType = mScopeInfoSuggestionType;
		if (suggestionType & SUGGEST_SYMBOLS)
		{
			// Suggest Completion
			// Suggestions from .va/*.js files, "reserved words" case:17920
			WTString localVarGuess;
			if (Is_Tag_Based(m_ed->m_ScopeLangType))
			{
				// Suggest from local ang global only
				if (m_cwd.GetLength())
					m_ed->LDictionary()->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
				if (m_cwd.GetLength())
					g_pGlobDic->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
			}
			else if (m_cwd.GetLength())
			{
				// Suggest from misc/*.JS/VB/...
				GetDFileMP(m_ed->m_ScopeLangType)->LDictionary()->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
				// Local
				m_ed->LDictionary()->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
				// Global
				g_pGlobDic->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
				// System
				m_ed->GetSysDicEd()->GetHint(m_cwd, m_scope, m_bcl, localVarGuess);
			}
		}

		if (suggestionType & SUGGEST_TEXT)
		{
			if (Is_Tag_Based(m_ed->m_ScopeLangType))
				FindListFromTextHTML(this, m_buf.c_str(), m_offset);
			else
				FindListFromTextJS(this, m_buf.c_str(), m_offset);
		}

		// Post message to main thread
		if (g_Guesses.GetCount() ||
		    m_isSnippet) // Allow Autotext in HTML/JS/VBS even if there are no guesses case:23009
			m_ed->PostMessage(
			    WM_COMMAND, VAM_SHOWGUESSES,
			    0); // Needs to be PostMessage for Case:5444/3050, or this will be called before the OnChar()
	}
};

class SuggestionThreadClass : public PooledThreadBase
{

  public:
	SuggestionClass m_SuggestBitsClass;
	SuggestionThreadClass(EdCntPtr ed) : PooledThreadBase("SuggestBitsThread"), m_SuggestBitsClass(ed)
	{
		LOG2("HintThread ctor");
		StartThread();
	}

  protected:
	virtual void Run()
	{
		try
		{
			m_SuggestBitsClass.DoSuggestions();
		}
		catch (...)
		{
			VALOGEXCEPTION("SuggestionThreadClass::Run");
		}
	}
};

bool ScopeInfo::HtmlSuggest(EdCntPtr ed)
{
	if (Psettings->m_autoSuggest)
	{
		if ((m_suggestionType & SUGGEST_TEXT_IF_NO_VS_SUGGESTIONS) && !g_CompletionSet->IsExpUp(NULL))
			m_suggestionType |= SUGGEST_TEXT;
		if (m_suggestionType & (SUGGEST_MEMBERS | SUGGEST_FILE_PATH))
		{
			g_CompletionSet->ShowCompletion(ed);
			if (m_suggestionType & SUGGEST_TEXT)
				new SuggestionThreadClass(ed); // Suggest Bits after members/file lists
			return true;
		}
		else if (m_suggestionType && m_suggestionType != SUGGEST_TEXT_IF_NO_VS_SUGGESTIONS)
		{
			new SuggestionThreadClass(ed);
			return true;
		}
	}

	if (!m_xref)
	{
		// Allow non-text snippets in HTML/JS/VBS case:20955
		int autotextItemIdx = -1;
		if (Psettings->m_codeTemplateTooltips &&
		    gAutotextMgr->FindNextShortcutMatch(ed->CurWord(), autotextItemIdx).GetLength())
			g_CompletionSet->ShowGueses(
			    ed, "", ET_SUGGEST); // show autotext immediately, Passing "" will fill with autotext only.
	}

	return false;
}

void ScopeInfo::ClearCwData()
{
	m_CWData.reset();
	mCwDataList.clear();
}

void ScopeInfo::GetGotoMarkers(LineMarkers& markers) const
{
	markers.Clear();
	LineMarkersPtr gotoMarkers(mGotoMarkers);
	if (!gotoMarkers)
		return;

	// assumes list, not tree
	for (uint i = 0; i < gotoMarkers->Root().GetChildCount(); ++i)
	{
		FileLineMarker& mkr = *(gotoMarkers->Root().GetChild(i));
		markers.Root().AddChild(mkr);
	}
}

BOOL FindListFromTextHTML(SuggestionClass* suggestionClass, LPCSTR buf, uint offset)
{
	// in <tag, search for prop="... for suggestions
	// use preceding [prop="..."] as search string
	LPCSTR pBuf = buf;
	uint p = offset;
	const LPCSTR curPos = buf + p;
	// Get prelude text
	for (; p && ISCSYM(pBuf[p - 1]); p--)
		;
	uint cwdLen = offset - p;
	WTString cwd(&pBuf[p], cwdLen);
	if (pBuf[p - 1] == '>')
	{
		for (; p && pBuf[p - 1] != '<'; p--)
			;
	}
	else if (pBuf[p - 1] == '<')
	{
		// for(;p && pBuf[p-1] != '<'; p--);
		p--; // include the <
	}
	else
	{
		// Read to beginning of quote
		for (; p && !strchr("\"'<", pBuf[p - 1]); p--)
			if (strchr("\r\n", pBuf[p - 1]))
				return FALSE;

		p--; // eat the quote
		const uint quotePos = p;
		const bool hadQuote = pBuf[p] == '"' || pBuf[p] == '\'';
		if (hadQuote && !strchr("\r\n'\"", *curPos))
			return FALSE; // [case: 55708] don't suggest inside of a string before the end

		//		for(;p && strchr(" ,(", pBuf[p-1]); p--); // in JS, [foo( 'suggest] eat the [( ']

		// read prop=
		if (hadQuote)
		{
			// [case: 41251] skip . \\ / : in prop value
			for (; p && strchr(".=\\/:", pBuf[p - 1]) || ISCSYM(pBuf[p - 1]); p--)
				;
		}
		else
		{
			for (; p && pBuf[p - 1] == '.' || pBuf[p - 1] == '=' || ISCSYM(pBuf[p - 1]); p--)
				;
		}

		if (hadQuote && p > 2 && pBuf[p - 2] == '=' && pBuf[p - 1] == '"' && quotePos != p && '"' == *(curPos - 1))
			return FALSE; // [case: 24178] don't suggest after the final value quote (="bah")
	}

	const LPCSTR orgPos = pBuf + p;
	const WTString pattern(pBuf + p, offset - p);
	if (pattern == "\"") // Could also check scope to see if we are in the ""'s
		return FALSE;    // don't recommend outside of ""
	BOOL anyFound = FALSE;
	LPCSTR suggestBitsPos = NULL;
	for (; pBuf && *pBuf && !suggestionClass->ShouldBail() && pattern.GetLength(); pBuf++)
	{
		pBuf = StrStrI(pBuf, pattern.c_str());
		if (!pBuf)
			break;
		if (orgPos != pBuf)
		{
			LPCSTR suggestPos = pBuf + pattern.GetLength() - cwdLen;
			if (pBuf < orgPos || !suggestBitsPos)
				suggestBitsPos = suggestPos;
			anyFound = TRUE;
			WTString txt = GetCStr(suggestPos);
#pragma warning(push)
#pragma warning(disable : 4127)
			if (0 && suggestBitsPos != buf && pBuf[0] == '<' && txt == cwd && txt.GetLength())
			{
				txt = TokenGetField(suggestPos, ">");
				if (txt.GetLength() && txt[txt.GetLength() - 1] == '/')
					txt += WTString(">");
				else
					txt += WTString(">$end$</") + TokenGetField(pBuf + 1, " \t\r\n>") + ">";
				suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
			else if (strchr("\"'", suggestPos[0]))
			{
				WTString quote(suggestPos[0]);
				txt = quote + TokenGetField(suggestPos, quote) + quote;
				suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
			else if (strchr("\"'", suggestPos[-1]))
			{
				WTString quote(suggestPos[-1]);
				txt = TokenGetField(suggestPos, quote);
				if (buf[offset] != quote[0])
					txt += quote;
				suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
			else if (suggestPos != buf && suggestPos[-1] == '>' && !strchr("<\r\n", suggestPos[0]))
			{
				txt = TokenGetField(suggestPos, "<\r\n");
				txt.TrimRight();
				if (txt.GetLength() && txt != cwd)
					suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
			else if (txt != cwd)
				suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
#pragma warning(pop)
		}
	}
	return anyFound;
}

BOOL FindListFromTextJS(SuggestionClass* suggestionClass, LPCSTR buf, uint offset)
{
	// Look for "foo.[CSym]"
	// use preceding [prop="..."] as search string
	LPCSTR pBuf = buf;
	uint p = offset;
	//	LPCSTR curPos = buf + p;
	// Get prelude text
	for (; p && ISCSYM(pBuf[p - 1]); p--)
		;
	uint cwdLen = offset - p;
	WTString cwd(&pBuf[p], cwdLen);
	if (p && !strchr(" \t.[(", pBuf[p - 1]))
		return FALSE; // not foo.ba, foo(ba, or foo[ba
	p--;
	if (p && strchr("])", pBuf[p - 1]))
	{
		// foo[x].bar or foo(x).bar
		// Read to beginning of quote
		for (; p && !strchr("[(", pBuf[p - 1]); p--)
			if (strchr("\r\n", pBuf[p - 1]))
				return FALSE;
	}
	if (p && strchr("!=", pBuf[p - 1]))
	{
		// foo = bar, foo != bar, foo == bar
		// Read [space][!=]
		for (; p && strchr("!=", pBuf[p - 1]); p--)
			if (strchr("\r\n", pBuf[p - 1]))
				return FALSE;
		// read [space]
		for (; p && pBuf[p - 1] == ' '; p--)
			;
	}
	if (pBuf[p] == '.')
		suggestionClass->m_listAll = TRUE;
	for (; p && ISCSYM(pBuf[p - 1]); p--)
		;
	LPCSTR orgPos = pBuf + p;
	WTString pattern(pBuf + p, offset - p);
	if (StartsWith(pattern, "if") || pattern == " ")
		return TRUE;

	BOOL anyFound = FALSE;
	LPCSTR suggestBitsPos = NULL;
	for (; pBuf && *pBuf && !suggestionClass->ShouldBail() && pattern.GetLength(); pBuf++)
	{
		pBuf = StrStrI(pBuf, pattern.c_str());
		if (!pBuf)
			break;
		if ((orgPos != pBuf))
		{
			if (pBuf != buf && ISCSYM(pattern[0]) && ISCSYM(pBuf[-1]))
				continue;

			// Simple check to prevent offering bit from comments
			BOOL isComment = FALSE;
			for (LPCSTR blPos = pBuf; !isComment && blPos > buf && !strchr("\r\n", *blPos); blPos--)
			{
				if (blPos[0] == '/' && blPos[1] == '/')
					isComment = TRUE;
			}
			if (isComment)
				continue;
			LPCSTR suggestPos = pBuf + pattern.GetLength() - cwdLen;

			anyFound = TRUE;
			if (pBuf < orgPos || !suggestBitsPos)
				suggestBitsPos = suggestPos;
			if (strchr("\"'", suggestPos[0]))
			{
				WTString quote(suggestBitsPos[0]);
				WTString txt = quote + TokenGetField(suggestBitsPos, quote) + quote;
				if (!txt.contains("\r") && !txt.contains("\n"))
					suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
			else if (ISCSYM(pBuf[pattern.GetLength()]))
			{
				WTString txt = GetCStr(suggestPos);
				suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
			}
		}
	}
	if (suggestBitsPos && !g_Guesses.GetCount())
	{
		WTString txt;
		if (suggestBitsPos[cwdLen] == '(')
		{
			uint i = cwdLen;
			txt += suggestBitsPos[i++];
			if (suggestBitsPos[i] == ' ')
				txt += suggestBitsPos[i++];
			if (suggestBitsPos[i] == ')')
				txt += suggestBitsPos[i++];
			if (suggestBitsPos[i] == ';' && strchr("\r\n", buf[offset]))
				txt += suggestBitsPos[i++];
		}
		else
		{
			if (strchr("\r\n", suggestBitsPos[0]))
				txt = TokenGetField(suggestBitsPos, "\r\n"); // Do not suggest anything from the next line. case 20898
			txt = TokenGetField(txt, "\r\n\"()[]{};,");      // read to any of these
		}
		txt.Trim();
		if (txt.GetLength() && txt != cwd)
			suggestionClass->OnFound(txt, ET_SUGGEST_BITS);
	}
	return anyFound;
}
