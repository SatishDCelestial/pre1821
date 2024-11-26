#include "stdafxed.h"
#include "resource.h"
#include "VACompletionBox.h"
#include "VACompletionSet.h"
#include "expansion.h"
#include "rbuffer.h"
#include "project.h"
#include "../Addin/DSCmds.h"
#include "VaTimers.h"
#include "VaTree.h"
#include "VARefactor.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "FontSettings.h"
#include "AutotextManager.h"
#include "Edcnt.h"
#include "Registry.h"
#include "WTComBSTR.h"
#include "file.h"
#include "TempSettingOverride.h"
#include "MenuXP/ToolbarXP.h"
#if defined(_DEBUG) && defined(SEAN)
#include "..\common\PerfTimer.h"
#endif // _DEBUG && SEAN
#include "RedirectRegistryToVA.h"
#include "TokenW.h"
#include "Guesses.h"
#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\textmgr2.h"
#include "RegKeys.h"
#include "FeatureSupport.h"
#include "MEFCompletionSet.h"
#include "VACompletionSetEx.h"
#include "VaService.h"
#include "DevShellService.h"
#include "ScreenAttributes.h"
#include "IdeSettings.h"
#include "WindowUtils.h"
#include "StringUtils.h"
#include "vsshell100.h"
#include "VsSnippetManager.h"
#include "LogElapsedTime.h"
#include "MenuXP\Tools.h"
#include "ImageListManager.h"
#include <commctrl.h>
#include "FileVerInfo.h"
#include "DpiCookbook\VsUIDpiHelper.h"

#define USE_IDX_AS_BEST_MATCH
//#define SUPPORT_DEV11_JS

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

extern UINT g_baseExpScope;
static int g_charWidthsBold[256];
static int g_charWidths[256];

int g_ourImgSize = 36;
int g_imgFilter[255];
int g_ExImgCount = 0;
CStringW s_lPath;
VACompletionSet* g_CompletionSet = NULL;

WTString VA_Snippet_Edit_suffix("[ Edit ]");
static int GetMaxListboxStringDisplayLen();

//////////////////////////////////////////////////////////////////////////

void EnableVB9Filtering(BOOL enable)
{
	static BOOL sEnabled = TRUE;
	if (enable != sEnabled)
	{
		sEnabled = enable;
		try
		{
			// Last resort hack to disable filtering in VS9 VB. case=15202
			// In the assembly CVBViewFilterBase::FilterActiveCompletionSet,
			// there is an "if(eax) FilterCompletionSet()".
			// The code below changes the test to "if(0)" to prevent the filter.
			HMODULE hVB = GetModuleHandleA("msvb7.dll");
			FileVersionInfo fvi;
			if (hVB && fvi.QueryFile(hVB))
			{
				long codeOffset = NULL; // Offset of the "if" test
				if (fvi.GetFileVerString() == "9.0.30729.1")
					codeOffset = 0xC2169;
				if (fvi.GetFileVerString() == "9.0.30729.170")
					codeOffset = 0xA6C91;
				if (fvi.GetFileVerString() == "9.0.21022.8")
					codeOffset = 0xBB1EB;
				if (codeOffset)
				{
					long fromAsm = 0x2674c085; // Assembly for if(eax),	"test eax,eax; je ..."
					long toAsm = 0x2674c033;   // Assembly for if(0)	"xor eax,eax; je ..."
					if (enable)
						std::swap(fromAsm, toAsm);

					long* pCode = (long*)((uintptr_t)hVB + codeOffset);
					if (pCode[0] == fromAsm) // Make sure it is "if(eax)"
					{
						DWORD oldProtFlags;
						if (VirtualProtect(pCode, sizeof(long), PAGE_EXECUTE_READWRITE, &oldProtFlags))
						{
							pCode[0] = toAsm; // Change the assembly to if(0)
							VirtualProtect(pCode, sizeof(long), oldProtFlags, &oldProtFlags);
						}
						else
							vLog("ERROR: vb9 filter fail vp");
					}
					else
						vLog("ERROR: vb9 filter fail pc");
				}
				else
					vLog("ERROR: vb9 filter fail ver(%s)", (LPCTSTR)fvi.GetFileVerString());
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("EnableVB9Filtering:");
			ASSERT(FALSE);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

#define ResetPtrs()                                                                                                    \
	p1 = text;                                                                                                         \
	p2 = subText;                                                                                                      \
	/* special treatment for symbols that start with '_' - pretend they don't start with '_' */                        \
	if (p1[0] == '_' && p2[0] != '_' && p1[1] && Psettings->m_bAllowShorthand && (flag & SUBSET || flag & ACRONYM))    \
	{                                                                                                                  \
		p1++;                                                                                                          \
		skippedStartOfText = true;                                                                                     \
	}

int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag)
{
	if (!*subText)
		return CS_StartsWithNC;
//#define UC(c) (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '1') || c == '_')?c:((c >= 'A' || c <= 'Z')) )
#define LC(c) ((flag & MATCHCASE) ? c : c & 0x80 ? c : (char)tolower(c))

	LPCSTR p1, p2;
	bool skippedStartOfText = false;
	ResetPtrs();
	int rank = 0;
	if (TRUE)
	{
		// begins with same case
		BOOL hasUpper = FALSE;
		int i;
		for (i = 0; p2[i] && p1[i] == p2[i]; i++)
			if (p1[i] >= 'A' && p1[i] <= 'Z')
				hasUpper = TRUE;
		if (!p2[i])
		{
			if (skippedStartOfText)
				return CS_ContainsChars; // typing app should prefer append over _append
			if (ISCSYM(p1[i]))
				return hasUpper ? CS_StartsWith : CS_StartsWithNC; // dont always suggest the lower case if typing all
				                                                   // in lowercase #lowercaseMatchOverridden
			return CS_ExactMatch;
		}
		if (!(flag & MATCHCASE))
		{
			// begins with no case
			for (i = 0; p2[i] && LC(p1[i]) == LC(p2[i]); i++)
				;
			if (!p2[i])
			{
				if (skippedStartOfText)
					return CS_ContainsChars; // typing app should prefer append over _append
				if (ISCSYM(p1[i]))
					return CS_StartsWithNC;
				return CS_ExactMatchNC;
			}
		}
	}
	if ((flag & SUBSET || flag & ACRONYM) && Psettings->m_bAllowAcronyms && LC(*p2) == LC(*p1))
	{
		// has acronym?
		p1++;
		p2++;
		bool eow = false;
		bool doCase = !!(flag & MATCHCASE);
		int skippedUppers = 0;
		LPCSTR lMatch = 0;
		//		if(!doCase)
		for (; *p1 && *p2 && !eow; p1++, p2++)
		{
			if (!skippedUppers)
				lMatch = p1;
			char uc = doCase ? *p2 : (*p2 & 0x80 ? *p2 : (char)toupper(*p2));
			if (*p2 == '_')
				eow = TRUE;
			if (doCase && uc == *p2)
				doCase = FALSE;
			for (; !eow && *p1 && (uc != ((p1[-1] == '_') ? (*p1 & 0x80 ? *p1 : toupper(*p1)) : *p1)); p1++)
			{
				if (p1[-1] == '_' || (*p1 >= 'A' && *p1 <= 'Z'))
					skippedUppers++;
				if (!ISCSYM(*p1))
					eow = TRUE;
			}
			if (!*p1 || !*p2)
				break;
			if (!skippedUppers)
				lMatch = p1;
			if (uc != ((p1[-1] == '_') ? (*p1 & 0x80 ? *p1 : toupper(*p1)) : *p1))
				eow = TRUE;
		}
		if (*p2 && lMatch && !skippedUppers)
		{
			for (; *lMatch && *p2 == *lMatch; p2++, lMatch++)
				;
			if (!*p2)
				return CS_ContainsChars; // not end of acronyms
		}
		if (!eow && !*p2 && skippedUppers < 3)
		{
			if (!skippedUppers)
			{
				for (; (*p1 >= '0' && *p1 <= '9') || (*p1 >= 'a' && *p1 <= 'z'); p1++)
					;
				if ((*p1 == '_' && ISCSYM(p1[1])) || (*p1 >= 'A' && *p1 <= 'Z'))
					return CS_ContainsChars; // not end of acronyms
				return CS_AcronymMatch;
			}
			// typing "wes", matches WM_SETFOCUS since WEC are capitals/acronyms
			// don't return here if we skipped more than two CAPS
			// not worthy of a suggestion unless we are looking for a subset
			if ((flag & SUBSET || g_CompletionSet->GetPopType() != ET_SUGGEST) && skippedUppers <= 2)
				return 10 + skippedUppers; // acronym match
		}
		rank = ptr_sub__int(p2, subText);
	}
	if (TRUE)
	{
		ResetPtrs();
		// begins with same case
		int i;
		for (i = 0; p2[i] && p1[i] == p2[i]; i++)
			;
		if (!p2[i])
			return CS_ContainsChars;
		if (!(flag & MATCHCASE))
		{
			// begins with no case
			for (i = 0; p2[i] && LC(p1[i]) == LC(p2[i]); i++)
				;
			if (!p2[i])
				return 12;
		}
	}
	if (TRUE /*(flag&MATCHCASE)*/)
	{
		// begins with no case
		ResetPtrs();
		int i;
		for (i = 0; p2[i] && ((p1[i] & 0x80 || p2[i] & 0x80) ? p1[i] == p2[i] : tolower(p1[i]) == tolower(p2[i])); i++)
			;
		if (!p2[i])
			return 30;
	}

	if (Psettings->m_bAllowShorthand && (flag & SUBSET || flag & ACRONYM) /*&& LC(*p2) == LC(*p1)*/)
	{
		// contains substr
		ResetPtrs();
		int rv = 30;
		for (; ISCSYM(*p1); p1++)
		{
			int i;
			for (i = 0; p2[i] && LC(p1[i]) == LC(p2[i]); i++)
				;
			if (!p2[i])
				return rv;
			if (i > 2)
			{
				// getm, Look for get...m
				p2 = &p2[i];
				p1 += i - 1;
				rv++;
			}
		}
		//		return 0;
	}

	//	if(s_popType == ET_SUGGEST)
	//		return 0; // dont suggest strange guesses

	ResetPtrs();
	if ((flag & SUBSET) && LC(*p2) == LC(*p1))
	{
		// contains chars?
		BOOL eow = FALSE;
		int caphits = 0;
		int capSkips = 0;
		int nSplits = 0;
		for (; *p1 && *p2 && !eow; p1++, p2++)
		{
			char lc = LC(*p2);
			if (LC(*p1) != lc)
			{
				nSplits++;
				for (; !eow && *p1 && LC(*p1) != lc && ISCSYM(*p1); p1++)
				{
					if (*p1 >= 'A' && *p1 <= 'Z')
						capSkips++;
				}

				if (LC(*p1) != lc)
					eow = TRUE;
			}
			if (*p1 >= 'A' && *p1 <= 'Z')
				caphits++;

			// [case: 49770]
			if (!*p1)
				break;
		}
		if (!*p2 && !eow)
			return 30 + nSplits - caphits + (capSkips * 2); // acronym match
	}

	if ((g_currentEdCnt && Is_Tag_Based(g_currentEdCnt->m_ftype)) ||
	    !ISCSYM(*text)) // C# comment tags? <![CDATA[]]> [case=36021]
	{
		if (StrStrI(text, subText))
			return CS_ContainsChars;
	}

	return 0;
}

static int DISPLAYLINES = 10;

//////////////////////////////////////////////////////////////////////////
// VACompletionSet

VACompletionSet::VACompletionSet()
{
	// setup list icons
	RebuildExpansionBox();
	DISPLAYLINES = Psettings->mListBoxHeightInItems;
}

VACompletionSet::~VACompletionSet()
{
	TearDownExpBox();
}

BOOL VACompletionSet::ExpandCurrentSel(char key /*= '\0'*/, BOOL* didVSExpand /*= NULL*/)
{
	mVsCompletionSetFlagsForExpandedItem = 0;
	mDidFileCompletionRepos = FALSE;
	UpdateCurrentPos(m_ed);
	if (didVSExpand)
		*didVSExpand = FALSE;

	const int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
	if (item == -1)
		return FALSE;
	WTString selTxt;
	long img;
	SymbolInfoPtr sinf = GetDisplayText((long)item, selTxt, &img);
	vCatLog("Editor.Events", "VaEventUE OnCommit '%s'", selTxt.c_str());
	if (!sinf)
	{
		_ASSERTE(!"ExpandCurrentSel: sinf == NULL");
		Dismiss();
		return FALSE;
	}

	BOOL bSelected = m_expBox->GetItemState(item, LVIS_SELECTED) != 0;
	if (!bSelected && key)
	{
		if (key != VK_TAB && key != VK_RETURN)
		{
			// Only TAB can expand non-selected items in vsnet [case: 16277]
			return FALSE;
		}
		else if (Psettings->mListboxCompletionRequiresSelection)
		{
			// [case: 140535]
			Dismiss();
			return FALSE;
		}
	}

	const uint sinfType = sinf->m_type;
	const bool isAutoText = sinfType == ET_AUTOTEXT;
	const bool isVsSnippet = sinfType == ET_VS_SNIPPET;
	const int autoTextShortcut = selTxt.Find(AUTOTEXT_SHORTCUT_SEPARATOR);

	m_ed->vSetFocus();

	// see if end of word is a valid sym?
	WTString eword;
	const WTString edBuf(m_ed->GetBuf());
	long cp = m_ed->GetBufIndex(edBuf, (long)m_ed->CurPos(true));
	WTString soFarWTStr = StartsWith();
	BOOL isFileExp = FALSE;
	if (IS_IMG_TYPE(sinfType) && TYPE_TO_IMG_IDX(sinfType)) // Eat "."s to the right of caret.  case 20500
		isFileExp = (TYPE_TO_IMG_IDX(sinfType) >= ICONIDX_FILE_FIRST && TYPE_TO_IMG_IDX(sinfType) <= ICONIDX_FILE_LAST);

	if ('"' == key || '\'' == key)
	{
		// [case: 54790] [case: 52550]
		if (soFarWTStr == "l" || soFarWTStr == "s")
		{
			SelectStartsWith();
			soFarWTStr.MakeUpper();
			gAutotextMgr->InsertAsTemplate(m_ed, soFarWTStr, FALSE);
		}

		if (soFarWTStr == "L" || soFarWTStr == "S")
		{
			Dismiss();
			// allow insert of key
			return FALSE;
		}
	}

	bool checkForColonInsert = false;
	if (!isFileExp && !isAutoText && !isVsSnippet && (Is_Tag_Based(gTypingDevLang) || XAML == m_ed->m_ftype) &&
	    selTxt.GetLength())
	{
		if (-1 != selTxt.Find(':') && -1 == soFarWTStr.Find(':') &&
		    (soFarWTStr.IsEmpty() || -1 != selTxt.FindNoCase(soFarWTStr)))
		{
			// [case: 62750] <asp:dat<ctrl+space> inserts "asp:datagrid" without overwriting "asp:"
			WTString prevWd(m_ed->CurWord(-1));
			if (prevWd == ":" || soFarWTStr.IsEmpty())
			{
				int wc = soFarWTStr.IsEmpty() ? -2 : -3;
				prevWd = m_ed->CurWord(wc);
				if (prevWd == "<" || (prevWd == "\"{" && XAML == m_ed->m_ftype))
				{
					// rewind m_p1
					uint p1, p2;
					m_ed->GetSel((long&)p1, (long&)p2);
					p1 = m_ed->WordPos(BEGWORD, p2);
					for (++wc; wc < 0; wc++)
						p1 = m_ed->WordPos(BEGWORD, p1);

					p1 = (uint)m_ed->GetBufIndex((int)p1);
					SetInitialExtent((long)p1, m_p2);
					soFarWTStr = m_startsWith;
				}
			}
			else if (selTxt[selTxt.GetLength() - 1] == ':' && key != ':')
			{
				// [case: 63269]
				checkForColonInsert = true;
			}
		}
	}

	if (gTypingDevLang == CS && soFarWTStr == selTxt &&
	    (g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "InsertNewlineOnEnterWithWholeWord") ||
	     soFarWTStr == "else")) // Always for else?
	{
		// C#: typing "else<ENTER>" should insert enter case=25252
		Dismiss();
		return FALSE;
	}

	if (autoTextShortcut != -1)
	{
		WTString autoTextShortcutStr;
		autoTextShortcutStr = selTxt.Mid(autoTextShortcut + 1);
		if (m_p2 >= autoTextShortcutStr.GetLength() &&
		    autoTextShortcutStr == edBuf.Mid(m_p2 - autoTextShortcutStr.GetLength(), autoTextShortcutStr.GetLength()))
		{
			const int leftCharPos = m_p2 - autoTextShortcutStr.GetLength() - 1;
			// [case: 35091] only assume shortcut if there is no other text to the left
			if (leftCharPos < 1 || !::wt_isalpha(edBuf[leftCharPos]))
				m_p1 = m_p2 - autoTextShortcutStr.GetLength();
		}

		if (gShellAttr->IsDevenv12OrHigher() && IsCFile(gTypingDevLang) && 0 == soFarWTStr.Find("/*") &&
		    g_IdeSettings->GetEditorBoolOption("C/C++", "BraceCompletion"))
		{
			// [case: 75035] vs does auto /**/ completion
			// increment m_p2 to end of auto completed comment
			const WTString line(edBuf.Mid(m_p2, 2));
			if (!line.IsEmpty())
			{
				if (soFarWTStr == "/*")
				{
					if (line.GetLength() > 1)
					{
						if (line[0] == '*' && line[1] == '/')
							m_p2 += 2;
					}
				}
				else if (soFarWTStr == "/**")
				{
					if (line[0] == '/')
						m_p2++;
				}
			}
		}

		selTxt = selTxt.Mid(0, autoTextShortcut);
	}

	LPCSTR pstr = edBuf.c_str();
	int i = 0;
	for (; ISCSYM(pstr[cp + i]) || (isFileExp && pstr[cp + i] == '.'); i++)
		eword += pstr[cp + i];

	if (isFileExp)
	{
		// [case: 100878]
		// nuke rest of text to right of caret, unless inserting directory
		if (sinfType != IMG_IDX_TO_TYPE(ICONIDX_FILE_FOLDER))
			m_p2 = cp + i;
	}
	else if (sinfType != ET_AUTOTEXT && sinfType != ET_VS_SNIPPET)
	{
		// [case: 90899]
		if (Settings::lbob_alwaysOverwrite == Psettings->mListboxOverwriteBehavior)
			m_p2 = cp + i; // nuke rest of text to right of caret
		else if (Settings::lbob_neverOverwrite == Psettings->mListboxOverwriteBehavior)
			; // no overwrite, always insert
		else  // if (Settings::lbob_default == Psettings->mListboxOverwriteBehavior)
		{
			// default behavior is to overwrite text to right in some cases
			// [case: 90866] [case: 99511]
			// the V_CONSTRUCTOR checks occur because sinfType might be FUNC rather than CLASS due to
			// DType collision between class and ctor
			if (!IsMembersList() && !IS_IMG_TYPE(sinfType) && !IS_OBJECT_TYPE(sinfType) && sinfType != VAR &&
			    !(sinf->mAttrs & V_CONSTRUCTOR) && sinfType != RESWORD && sinfType != DEFINE)
			{
				m_p2 = cp + i; // nuke rest of text to right of caret
			}
			else
			{
				DType* anySym = m_ed->GetParseDb()->FindAnySym(eword);
				uint anySymType = anySym ? anySym->type() : 0u;
				if (!anySym)
				{
					m_p2 = cp + i; // nuke rest of text to right of caret
				}
				else if (anySymType == C_ENUMITEM && (sinfType == C_ENUM || sinfType == CLASS || sinfType == STRUCT ||
				                                      sinfType == NAMESPACE || (sinf->mAttrs & V_CONSTRUCTOR)))
				{
					// assume user is typing accessor in front of enum item
				}
				else if ((anySymType == FUNC || anySymType == VAR || anySymType == CLASS || anySymType == STRUCT ||
				          anySymType == NAMESPACE) &&
				         (sinfType == CLASS || sinfType == STRUCT || sinfType == NAMESPACE || sinfType == VAR ||
				          (sinf->mAttrs & V_CONSTRUCTOR) || -1 != anySym->Scope().Find(sinf->mSymStr)))
				{
					// assume user is typing accessor in front of method or member
				}
				else if (sinfType == RESWORD &&
				         (anySymType == CLASS || anySymType == STRUCT || anySymType == VAR || anySymType == RESWORD))
				{
					// assume user is modifying symbol (const/volatile/typedef/etc)
				}
				else if (sinfType == DEFINE && (anySymType == FUNC || anySymType == VAR) && anySymType != DEFINE)
				{
					// assume user is modifying symbol (CONST/etc)
				}
				else if (!IsMembersList() || !anySym) // [case: 99511] restores old pre-90866 behavior for members lists
				{
					m_p2 = cp + i; // nuke rest of text to right of caret
				}
			}
		}
	}

	if (sinf && IS_IMG_TYPE(sinfType) && !(sinfType & VSNET_TYPE_BIT_FLAG))
	{
		if (TYPE_TO_IMG_IDX(sinfType) == ICONIDX_REFACTOR_INSERT_USING_STATEMENT)
		{
			WTString txt(sinf->mSymStr);
			Dismiss();
			return VARefactorCls::AddUsingStatement(
			    TokenGetField(txt.c_str(), WTString(AUTOTEXT_SHORTCUT_SEPARATOR).c_str()));
		}
		if (TYPE_TO_IMG_IDX(sinfType) == ICONIDX_REFACTOR_RENAME)
		{
			RefactorFlag flag = (sinf->mSymStr == "Rename references with preview...") ? VARef_Rename_References_Preview
			                                                                           : VARef_Rename_References;
			// [case: 95272]
			// Dismiss clears rename suggestion state; save and then restore post-Dismiss
			const ULONG kLastEditPos = m_ed->m_lastEditPos;
			const WTString kLastEditSymScope = m_ed->m_lastEditSymScope;
			Dismiss();
			m_ed->m_lastEditPos = kLastEditPos;
			m_ed->m_lastEditSymScope = kLastEditSymScope;
			Refactor(flag);
			// invalidate rename suggestion state
			m_ed->m_lastEditPos = 0;
			m_ed->m_lastEditSymScope.Empty();
			return TRUE;
		}
	}

	bool allowVsCompletionSetCommit = true;
	if (GetIVsCompletionSet() && IS_VSNET_IMG_TYPE(sinfType) && gTypingDevLang == VB &&
	    gShellAttr->IsDevenv14OrHigher() && IsMembersList() && Psettings->m_bAllowShorthand && sinf &&
	    0 != sinf->mSymStr.FindNoCase(soFarWTStr))
	{
		// [case: 85017]
		// vs2015 vb does not support commit on item filtered from completion set,
		// so va acronyms do not work.
		// Filter: https://msdn.microsoft.com/en-us/library/ee696373.aspx
		// I tried changing DisplayText in
		// WholeTomatoSoftware.VisualAssist.WTCompletion.VaProxyCompletionSet14.OnCommit,
		// but the 'set' didn't take (no error, just not applied).
		// DisplayText:
		// https://msdn.microsoft.com/en-us/library/microsoft.visualstudio.language.intellisense.completion.displaytext.aspx
		//
		// Rather than calling Commit on the vs completionset, VA will insert the text
		// directly in these acronym cases.
		// Don't do it in all cases because it only works for simple text insert --
		// code generation listbox items won't work.
		allowVsCompletionSetCommit = false;
	}

	if (GetIVsCompletionSet() && IS_VSNET_IMG_TYPE(sinfType) && allowVsCompletionSetCommit)
	{
		_ASSERTE(sinf);
		const int iIndex = sinf->m_idx;
		WTComBSTR cmpStr;
		mVsCompletionSetFlagsForExpandedItem = GetIVsCompletionSet()->GetFlags();
		const long kCnt = GetCount();
		if (iIndex < kCnt || IsVACompletionSetEx())
		{
			char ch = key;
			WTComBSTR soFar = (const wchar_t*)soFarWTStr.Wide();
			if (soFarWTStr == "<")
				soFar = L""; // OnCommit needs empty ""; // See also case: 12324
			if (!key || key == VK_TAB)
			{
				ch = '\0';
				bSelected = TRUE;
			}
			if (key == '.' &&
			    !(m_expContainsFlags & SUGGEST_FILE_PATH)) // href="..",   ".." should not cause expansion case=21788
				ch = '\0';                                 // Don't insert '.', so it gets processed in OnChar

#ifdef _DEBUG
			{
				// Sanity check to make sure we are expanding the correct item.
				LPCWSTR wtext = NULL;
				long img2;
				/*HRESULT res =*/GetIVsCompletionSet()->GetDisplayText(iIndex, &wtext, &img2);
				if (!wtext && !sinf->mSymStr.IsEmpty())
				{
					// if this assert fires, put a breakpoint here and repro to
					// catch the completionset before the assert dialog appears.
					_ASSERTE(!"GetIVsCompletionSet()->GetDisplayText fail -- no text -- completionset destroyed by "
					          "this dialog");
					// assert produces a dialog; loss of focus causes Dismiss() to be called; so just return
					return FALSE;
				}
				else if (wtext && sinf->mSymStr.Wide() != wtext)
				{
					// if this assert fires, put a breakpoint here and repro to
					// catch the completionset before the assert dialog appears.
					_ASSERTE(
					    !"GetIVsCompletionSet()->GetDisplayText mbcs fail -- completionset destroyed by this dialog");
					// assert produces a dialog; loss of focus causes Dismiss() to be called; so just return
					return FALSE;
				}
			}
#endif // _DEBUG

			if (Settings::lbob_neverOverwrite == Psettings->mListboxOverwriteBehavior && IsCFile(gTypingDevLang) &&
			    pstr && ::wt_isalpha(pstr[cp]))
			{
				// [case: 110943] Settings::lbob_neverOverwrite doen't work with VS intellisense,
				// so we need to insert the selected text.
				// only 'fixed' for c/c++ because completion in other languages can insert constructs
				// that we do not have access to.  c/c++ only insert the text displayed in the listbox.
				_ASSERTE(!isAutoText);
				_ASSERTE(!checkForColonInsert);
				_ASSERTE(ET_VS_SNIPPET != sinfType);
				_ASSERTE(ET_SUGGEST_BITS != sinfType);
				if (!m_ed->HasSelection())
					SelectStartsWith();
				gAutotextMgr->InsertText(m_ed, sinf->mSymStr, false);
			}
			else if (S_OK != GetIVsCompletionSet()->OnCommit(soFar, (long)iIndex, bSelected, (WCHAR)ch, &cmpStr))
				return FALSE; // they did not expand

			m_ed->OnModified();
			bool doCommitComplete = false;
			bool doListMembers = false;
			CComQIPtr<IVsCompletionSetEx> compSetEx(GetIVsCompletionSet());
			if (checkForColonInsert && cmpStr.Length())
			{
				// [case: 63269] specifically fixes the missing : but doesn't work for missing =
				CStringW vsInsert(cmpStr);
				if (vsInsert[vsInsert.GetLength() - 1] != ':')
				{
					_ASSERTE(mVsCompletionSetFlagsForExpandedItem & CSF_CUSTOMCOMMIT);
					doCommitComplete = true;
					doListMembers = true;
				}
			}

#ifdef Case63270
			if (mVsCompletionSetFlagsForExpandedItem & CSF_CUSTOMCOMMIT && !doCommitComplete)
			{
				if (Is_Tag_Based(m_ed->m_ScopeLangType))
				{
					// this would be a more general solution to the problem, but has side-effects
					// that need to be further investigated/tested.
					// Also, in this case, wouldn't want to set doListMembers.
					// [case: 63269] [case: 63270]
					doCommitComplete = true;
				}
			}
#endif // Case63270

			m_ed->GetBuf(TRUE); // fixes FixUpFnCall in c#
			BOOL possibleXMLTag = (m_p1 > 0) ? (edBuf[(uint)m_p1 - 1] == '<') : FALSE;
			if (soFar.Length() && cmpStr.Length() && StrCmpACW(cmpStr, soFar) == 0 &&
			    !possibleXMLTag) // StrCmpAC for VB. case=24470
			{
				// No completion needed.
				if (m_ed->m_ftype != Other && sinfType != ET_AUTOTEXT &&
				    sinfType != ET_VS_SNIPPET) // do not add parens in .css files Case: 314
					g_ExpHistory->AddExpHistory(
					    selTxt); // Add to history so JS will display as best guess in next suggestion.
				Dismiss();
				if (doCommitComplete && compSetEx)
				{
					// [case: 63269]
					compSetEx->OnCommitComplete();
				}

				if (doListMembers)
				{
					static CString sCmd("Edit.ListMembers");
					PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)sCmd, 0);
				}

				if (selTxt.EndsWith("..."))
					return TRUE; // Don't process key in OnChar() < href="Pick URL ..." case=21541

				if ('\t' == key || '\r' == key)
				{
					// [case: 60744] call FixUpFnCall after dismiss even if nothing actually inserted
					// sort of copied the block below where FixUpFnCall is called
					if (!m_ed->HasSelection() && !isAutoText && !isVsSnippet)
					{
						if (Is_C_CS_File(m_ed->m_ScopeLangType))
						{
							// [case: 56276] watch out for double ()
							if (CS == m_ed->m_ScopeLangType || '(' != key)
								m_ed->FixUpFnCall();
						}
					}

					// don't insert enter or tab since they did commit - the key was used to dismiss like esc
					return TRUE;
				}

				// return false so that completion key gets inserted (just not for enter or tab)
				return FALSE;
			}

			if (didVSExpand)
				*didVSExpand = TRUE;
			if (cmpStr)
				selTxt = CStringW(cmpStr);
			if (cmpStr && didVSExpand)
			{
				// Expand with any char needs to insert char. case=12139
				if (key && !selTxt.EndsWith(WTString(key)))
					*didVSExpand = FALSE; // Let OnChar process key

				if (key == '\r' && m_ed->m_ScopeLangType == VB)
				{
					WTString eolStr = EolTypes::GetEolStr(selTxt);
					if (selTxt.EndsWith(eolStr))
					{
						// Strip off the \r\n, insert the text and let them process [enter] to reformat line. case=27337
						selTxt = selTxt.Mid(0, selTxt.GetLength() - eolStr.GetLength());
						*didVSExpand = FALSE;
					}
				}
			}
			else if (!cmpStr && didVSExpand)
			{
				if (m_ed->m_ScopeLangType == VB)
				{
					// VB overrides [case=36778]
					ulong cp2 = m_ed->CurPos();
					if (cp2 && key == ' ')
					{
						// They did the expansion after ' '
						// [case: 20259] [case: 20261] added vs2010 check for "As"
						if (m_ed->GetSubString(cp2 - 1, cp2) == " " // see if there is a space to the left of caret
						    &&
						    // only on new, override and As ?
						    (sinf->mSymStr == "new" || sinf->mSymStr == "Overrides" ||
						     (sinf->mSymStr == "As" && gShellAttr->IsDevenv10OrHigher())))
						{
							// case=310 no suggestion listbox after new in C# if expand with space is set
							// There is no option to expand with space in VB, so should only be needed in CS?
							m_ed->SetSel(cp2 - 1, cp2); // select the space
							*didVSExpand = FALSE;       // Allow OnChar to list suggestions
						}
					}
					//  Doesn't work in VB
					// 					else if (cp && key == '(')
					// 					{
					// 						// They did the expansion after '('
					// 						if(m_ed->GetSubString(cp-1, cp) == "(")					// see if there is a ( to
					// the left of caret
					// 						{
					// 							// case=21179 no suggestion listbox after ( in C#
					// 							m_ed->SetSel(cp-1, cp); // select the paren
					// 							*didVSExpand = FALSE;	// Allow OnChar to list suggestions
					// 						}
					// 					}
				}
				else if (m_ed->m_ScopeLangType == CS)
				{
					ulong cp2 = m_ed->CurPos();
					if (cp2 && key == ' ')
					{
						// They did the expansion after ' '
						if (m_ed->GetSubString(cp2 - 1, cp2) == " " // see if there is a space to the left of caret
						    && (sinf->mSymStr == "new" || sinf->mSymStr == "override") // only on new and override?
						)
						{
							// case=310 no suggestion listbox after new in C# if expand with space is set
							// There is no option to expand with space in VB, so should only be needed in CS?
							m_ed->SetSel(cp2 - 1, cp2); // select the space
							*didVSExpand = FALSE;       // Allow OnChar to list suggestions
						}
					}
					else if (cp2 && key == '(')
					{
						// They did the expansion after '('
						// see if there is a ( to the left of caret
						if (m_ed->GetSubString(cp2 - 1, cp2) == "(")
						{
							// case=21179 no suggestion listbox after ( in C#
							// select the paren
							m_ed->SetSel(cp2 - 1, cp2);
							if (gShellAttr->IsDevenv12OrHigher() &&
							    (g_IdeSettings->GetEditorIntOption("CSharp", "BraceCompletion") ||
							     Psettings->IsAutomatchAllowed(true)))
							{
								// special-case vs2013+ editor brace completion
								m_ed->Insert("");
							}

							// Allow OnChar to list suggestions
							*didVSExpand = FALSE;
						}
					}
					else if (cp2 && ')' == key && key == m_ed->m_autoMatchChar)
					{
						// [case: 49167] excess parens if ) used while list is up
						if (m_ed->GetSubString(cp2, cp2 + 1) == m_ed->m_autoMatchChar)
						{
							m_ed->SetSel(cp2, cp2 + 1);
							m_ed->Insert("");
							m_ed->m_autoMatchChar = 0;
						}
					}
				}
				else if ((m_ed->m_ScopeLangType == Src && gShellAttr->IsDevenv11OrHigher()) ||
				         gShellAttr->IsDevenv12OrHigher())
				{
					// [case: 66328]
					ulong cp2 = m_ed->CurPos();
					if (cp2 && (key == '(' || key == '['))
					{
						if (Psettings->IsAutomatchAllowed(true))
						{
							// they committed and inserted ( or [
							const WTString ch2 = m_ed->GetSubString(cp2 - 1, cp2);
							if (ch2 == "(" || ch2 == "[") // confirm correct pos
							{
								// delete the char they inserted
								m_ed->SetSel(cp2 - 1, cp2);
								m_ed->Insert("");
								// Allow OnChar to automatch the key
								*didVSExpand = FALSE;
							}
						}
						else if ('(' == key && m_ed->m_ScopeLangType == Src && gShellAttr->IsDevenv14OrHigher())
						{
							// [case: 94912]
							// this block is from EdCnt::FixUpFnCall
							if (Psettings->m_ParamInfo)
							{
								if (!Psettings->mVaAugmentParamInfo || Psettings->m_bUseDefaultIntellisense ||
								    Psettings->mSuppressAllListboxes)
									::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_PARAMINFO, 0); // Display theirs
								else
									m_ed->SetTimer(ID_ARGTEMPLATE_DISPLAY_TIMER, 500, NULL); // Display ours
							}
						}
					}
				}
			}

			if ((m_expContainsFlags & SUGGEST_FILE_PATH) && m_ed->GetSubString((ulong)m_p2, ulong(m_p2 + 1)) == "\"")
			{
				// expanding with a quote to the right of caret. case=20911
				if (selTxt.EndsWith("\""))
					m_p2++; // Over write the auto matched quote
				else
				{
					m_p2++;        // Over write the auto matched quote
					selTxt += '"'; // add a quote to over write it.
				}
			}

			if (!IsVACompletionSetEx() || cmpStr.Length() > 0)
			{
				if ((cmpStr.m_str && (selTxt.Find("...") == -1)) || (iIndex >= kCnt && !selTxt.IsEmpty()))
				{
					if (Is_Tag_Based(m_ed->m_ScopeLangType))
					{
						// Inserting quoted string, check to see if string is already quoted.  case=39884
						BOOL isExpQuoted = strchr("\"'", selTxt[0]) != NULL;
						if (isExpQuoted && m_p1 && edBuf[(uint)m_p1 - 1] == selTxt[0])
							m_p1 -= 1; // already has matching opening quote
						else if (!isExpQuoted && soFarWTStr.GetLength() && strchr("\"'", soFarWTStr[0]))
							m_p1 += 1; // Put it in the quote, don't just replace it [case=40890] [case=41878]

						if (strchr("\"'", edBuf[(uint)m_p2]))
						{
							if (selTxt.GetLength() && edBuf[(uint)m_p2] == selTxt[uint(selTxt.GetLength() - 1)])
							{
								m_p2 += 1; // already has matching closing quote case=39884 case=40890
							}
							else // extended to all strings in xaml/htm/... [case=40890]
							{
								selTxt += edBuf[(uint)m_p2]; // leave caret outside of closing quote. case=40335
								m_p2 += 1;
							}
						}
					}
					if (!m_ed->HasSelection() && sinfType != ET_SUGGEST_BITS)
						SelectStartsWith();
					gAutotextMgr->InsertText(m_ed, WTString(selTxt), sinfType == ET_AUTOTEXT);
					if (ET_VS_SNIPPET == sinfType)
					{
						// [case: 64205] get vs to expand their snippet
						// if this doesn't work, try using Edit.InvokeSnippetFromShortcut
						m_ed->InsertTabstr();
					}
					else if (doCommitComplete && compSetEx)
					{
						// [case: 63269]
						compSetEx->OnCommitComplete();
					}
					else if (possibleXMLTag)
					{
						// Insert closing ?>, -->, ... [case 15614]
						if (didVSExpand && key)
							*didVSExpand = FALSE; // allow OnChar to insert typed char
						WTString closeMatch;
						if (selTxt == "!--")
							closeMatch = "-->";
						if (selTxt == "![CDATA[")
							closeMatch = "]]>";
						if (selTxt == "?")
							closeMatch = "?>";
						if (closeMatch.GetLength())
						{
							uint cp2 = m_ed->CurPos();
							gAutotextMgr->InsertText(m_ed, closeMatch, FALSE);
							m_ed->SetPos(cp2);
						}
					}

					if (doListMembers)
					{
						static CString sCmd("Edit.ListMembers");
						PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)sCmd, 0);
					}
				}
			}
		}
		else
		{
			// case=5007
			vLogUnfiltered("VACS::ECS  idx error %d out of %ld f(%lx)", iIndex, kCnt, mVsCompletionSetFlagsForExpandedItem);
		}
	}
	else
	{
		// startsWith a quote and we are inserting a non-quoted string, leave the quote
		// html: <tag src=" // leave the quote
		if ((soFarWTStr[0] == '\'' || soFarWTStr[0] == '"') && soFarWTStr[0] != selTxt[0])
			SetInitialExtent(m_p1 + 1, m_p2);
		if (selTxt == "Create implementation...")
		{
			Dismiss();
			CStringW fname = m_ed->FileName();
			if (SwapExtension(fname) && fname.GetLength())
			{
				CreateImplementation();
			}
			return TRUE;
		}

		if (gShellAttr->IsDevenv10OrHigher() && !m_ed->HasSelection() && m_startsWith == selTxt && !isAutoText &&
		    !isVsSnippet)
		{
			// vs2010: Prevents exception typing "int<tab>" in c++
			// Already matches, no expansion needed.
			Dismiss();
			if (Is_C_CS_File(m_ed->m_ScopeLangType))
			{
				// [case: 56276] watch out for double ()
				// see also test_completion_list.cs_Listbox0018v14 in vs2015
				if ((CS == m_ed->m_ScopeLangType && !gShellAttr->IsDevenv14OrHigher()) || '(' != key)
					m_ed->FixUpFnCall();
			}

			if ('\t' == key || '\r' == key)
			{
				// [case: 60744] don't insert enter or tab since we did commit - the key was used to dismiss like esc
				return TRUE;
			}

			// return false so that completion key gets inserted (just not for enter or tab)
			return FALSE;
		}

		if (!m_ed->HasSelection())
		{
			if (sinf && soFarWTStr == sinf->mSymStr && !Is_C_CS_File(m_ed->m_ScopeLangType)) // Need FixUpFnCall() below
			{
				// Already matches, no expansion needed.
				// Prevents flicker
				Dismiss();
				if (!key || key == VK_TAB ||
				    (m_ed->m_ScopeLangType != VB && key == VK_RETURN)) // Pass CR on in VB. case=24602
					return TRUE;                                       // Don't insert tab or CR, just dismiss the list.
				return FALSE;                                          // Allow OnChar to insert char
			}
			SelectStartsWith();
		}

		if (sinf && sinfType == CLASS && key == '<' && IsCFile(m_ed->m_ScopeLangType) & selTxt.EndsWith(">"))
		{
			auto templStart = selTxt.Find('<');
			if (templStart > 0) //Don't reduce selTxt to empty string
			{
				selTxt = selTxt.Left(templStart);
			}
		}

		if (sinf && sinfType == IMG_IDX_TO_TYPE(ICONIDX_FILE_FOLDER) && selTxt == sinf->mSymStr)
		{
			// #include directory completion
			ExpandIncludeDirectory(m_ed, selTxt, key);
			if (Is_Tag_Based(m_ed->m_ScopeLangType))
				Dismiss(); // Dismiss VSNET so expanding  "subdir/" will list subdir files in HTML case=21788
			return TRUE;
		}
		else if (sinf && isFileExp)
		{
			// #include file completion
			// [case: 100874]
			gAutotextMgr->InsertText(m_ed, selTxt, isAutoText);
			uint curPos = m_ed->CurPos();
			char curCh = m_ed->CharAt(curPos);
			if ('"' == curCh || '>' == curCh)
			{
				// jump past closing delimiter
				m_ed->SetPos(curPos + 1);
				mDidFileCompletionRepos = TRUE;
			}

			Dismiss();
			return TRUE;
		}
		else
		{
			// Allow XML bits to place caret between ><'s
			// Added ><'s to ensure this isn't a PHP symbol that contains $end$
			if (selTxt.contains(">$end$<"))
				gAutotextMgr->InsertAsTemplate(m_ed, WTString(selTxt), FALSE);
			else if (sinfType == ET_SCOPE_SUGGESTION || sinfType == ET_AUTOTEXT_TYPE_SUGGESTION)
			{
				if (WTString(selTxt).EndsWith(":"))
					gAutotextMgr->InsertAsTemplate(m_ed, WTString(selTxt), TRUE); // Reformat "public:"/"default:"/...
				else
					gAutotextMgr->InsertAsTemplate(m_ed, WTString(selTxt), FALSE);

				if (sinfType == ET_AUTOTEXT_TYPE_SUGGESTION && selTxt.Find(_T("case ")) == 0)
				{
					// [case: 70316] show ScopeSuggestions immediately
					m_ed->KillTimer(ID_TIMER_GETHINT);
					Dismiss();

					// required steps to display suggestions
					m_ed->m_typing = true;
					m_ed->m_isValidScope = FALSE;
					m_ed->Scope();
					ShowGueses(m_ed, nullptr, ET_SUGGEST);

					return TRUE;
				}
			}
			else
			{
				if (isAutoText && GetIVsCompletionSet())
				{
					// [case: 81303]
					// see also comment re: ET_AUTOTEXT in VACompletionSetEx::ExpandCurrentSel
					Dismiss();
				}

				gAutotextMgr->InsertText(m_ed, selTxt, isAutoText);
			}

			if (isVsSnippet)
			{
				// [case: 64205] get vs to expand their snippet
				// if this doesn't work, try using Edit.InvokeSnippetFromShortcut
				m_ed->InsertTabstr();
			}

			// [case: 9827] fix for hints appearing immediately after accepting keyword suggestion
			if (isAutoText || isVsSnippet || ::IsReservedWord(selTxt, gTypingDevLang))
				m_ed->KillTimer(ID_TIMER_GETHINT);
		}
	}
	if ((!key || key == VK_TAB || key == VK_RETURN) && selTxt.Find('(') == -1 &&
	    (!Is_Tag_Based(m_ed->m_ScopeLangType))      // Not in <html tags
	    && (!Is_VB_VBS_File(m_ed->m_ScopeLangType)) // Don't insert "()" in VB/VBS files
	    && (m_ed->m_ScopeLangType != JS)            // Annoying in JS since almost everything is a "function"
	    && !isAutoText && !isVsSnippet)             // [case: 23119] don't call FixUpFnCall for snippets
	{
		m_ed->FixUpFnCall();
	}
#ifdef case36017
	else if ('(' == key && (Is_VB_VBS_File(m_ed->m_ScopeLangType)) // [case: 36017] insert matching ) in VB/VBS files
	         && !isAutoText && !isVsSnippet                        // [case: 23119] don't call FixUpFnCall for snippets
	         && -1 == selTxt.Find('(') && gShellAttr->IsDevenv10OrHigher())
	{
		m_ed->FixUpFnCall();
	}
#endif // case36017

	if (m_ed->m_ftype != Other && sinfType != ET_AUTOTEXT &&
	    sinfType != ET_VS_SNIPPET) // do not add parens in .css files Case: 314
		g_ExpHistory->AddExpHistory(selTxt);
	Dismiss();
	return TRUE;
}

class CToolbarWithBgColour : public CToolBar
{
	DECLARE_DYNAMIC(CToolbarWithBgColour)

  public:
	CToolbarWithBgColour(COLORREF bgclr);
	virtual ~CToolbarWithBgColour();

  protected:
	const COLORREF bgclr;

	DECLARE_MESSAGE_MAP()
  public:
	virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = TBSTYLE_FLAT,
	                      DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_ALIGN_TOP, CRect rcBorders = CRect(0, 0, 0, 0),
	                      UINT nID = AFX_IDW_TOOLBAR);
	afx_msg void OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult);

  private:
	using CWnd::CreateEx;
};
IMPLEMENT_DYNAMIC(CToolbarWithBgColour, CToolBar)

CToolbarWithBgColour::CToolbarWithBgColour(COLORREF bgclr) : bgclr(bgclr)
{
}
CToolbarWithBgColour::~CToolbarWithBgColour()
{
}
BOOL CToolbarWithBgColour::CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle, DWORD dwStyle, CRect rcBorders, UINT nID)
{
	dwCtrlStyle |= TBSTYLE_TRANSPARENT | TBSTYLE_CUSTOMERASE;
	return CToolBar::CreateEx(pParentWnd, dwCtrlStyle, dwStyle, rcBorders, nID);
}
void CToolbarWithBgColour::OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMCUSTOMDRAW pNMCD = reinterpret_cast<LPNMCUSTOMDRAW>(pNMHDR);
	*pResult = 0;

	CBrush br;
	br.CreateSolidBrush(bgclr);
	CRect rect;
	GetClientRect(rect);
	::FillRect(pNMCD->hdc, rect, (HBRUSH)br.m_hObject);
}
#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CToolbarWithBgColour, CToolBar)
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CToolbarWithBgColour::OnNMCustomdraw)
END_MESSAGE_MAP()
#pragma warning(pop)

void VACompletionSet::DisplayList(BOOL forceUpdate /*= FALSE*/)
{
	if (gShellSvc && gShellSvc->HasBlockModeSelection(m_ed.get()))
	{
		Dismiss();
		return;
	}

	if (!g_currentEdCnt || !m_ed)
	{
		Dismiss();
		return;
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*m_ed);

	if (!forceUpdate && m_displayUpToDate)
	{
		// [case: 91386] update position if not going to redisplay
		CPoint newCaretPos(m_ed->vGetCaretPos());
		m_ed->vClientToScreen(&newCaretPos);
		m_caretPos = newCaretPos;
		return;
	}

	LogElapsedTime let("VACS::DL", 100);
	UpdateCurrentPos(m_ed); // Ensure it is up to date
	WTString startsWith = StartsWith();
	if (Is_Tag_Based(m_ed->m_ScopeLangType) && (startsWith[0] == '"' || startsWith[0] == '\''))
		startsWith = startsWith.Mid(1); // strip off ", so <tag prop=" does not dismiss box

	WTString startsWithAutoText = startsWith;
	if (Is_Tag_Based(m_ed->m_ScopeLangType) && m_p1 && m_ed->GetBuf()[(uint)m_p1 - 1] == '<')
		startsWithAutoText =
		    WTString("<") + startsWithAutoText; // Include < in html snippets for exact matches, case:20949
	vCatLog("Editor.Events", "VACS::DL  %s", m_startsWith.c_str());
	if (!Is_Tag_Based(m_ed->m_ftype) && JS != m_ed->m_ftype)
	{
		if (wt_isdigit(startsWith[0]) && !IsFileCompletion() &&
		    !IsMembersList()) // [bug 1502, bug 24599] don't suggest anything if typing a number
		{
			Dismiss(); // if list is already up, dismiss case=22140
			return;
		}
	}

	UINT matchFlags = Psettings->m_bAllowAcronyms ? ACRONYM : 0u;

	IsMembersList(FALSE);
	if (m_ed)
	{
		m_isDef = (m_ed->GetParseDb()->m_isDef || !m_ed->m_isValidScope);
		if (CheckIsMembersList(m_ed))
		{
			WTString pwd = m_ed->CurWord(-1);
			if (pwd == ".")
			{
				WTString pwd2 = m_ed->CurWord(-2);
				if (pwd2.GetLength() && wt_isdigit(pwd2[0]))
				{
					IsMembersList(FALSE); // 123.f
					return;
				}
			}
		}
	}

	if (IsFileCompletion())
	{
		m_isDef = false;
		IsMembersList(FALSE);
	}

	if ((GetIVsCompletionSet() || IsMembersList()) && Psettings->m_bAllowShorthand)
		matchFlags |= SUBSET; // do not set SUBSET for suggestions or you will get crazy results

	// Added case preference, but does not limit to case
	BOOL doCase = FALSE;
	if (strncmp(g_Guesses.GetMostLikelyGuess().c_str(), startsWith.c_str(), (uint)startsWith.GetLength()) != 0)
	{
		for (LPCSTR p = startsWith.c_str(); !doCase && p && *p; p++)
			if (wt_isupper(*p))
				doCase = TRUE;
	}

	CPoint newCaretPos(m_ed->vGetCaretPos());
	m_ed->vClientToScreen(&newCaretPos);
	m_caretPos = newCaretPos;

	_ASSERTE(!m_expBox || m_expBox->GetSafeHwnd());
	// 	if(m_expBox && !m_expBox->GetSafeHwnd())
	// 	{
	// 		// Closing VS10 detached windows, destroys the m_expBos, recreate is needed.
	// 		delete m_expBox;
	// 		m_expBox = NULL;
	// 	}

	if (mRebuildExpBox || m_expBoxDPI != VsUI::DpiHelper::GetDeviceDpiX())
	{
		TearDownExpBox();
		mRebuildExpBox = false;
	}

	int previousCnt = 0;
	bool hadScroll = false;
	const BOOL wasVis = m_expBox->GetSafeHwnd() ? m_expBox->IsWindowVisible() : FALSE;
	if (wasVis)
	{
		// [case: 71879] don't hide in pre-2010 ides
		if (gShellAttr->IsDevenv10OrHigher())
			m_expBox->HideTooltip();
		previousCnt = m_expBox->GetItemCount();
		// [case: 93635]
		hadScroll = (m_expBox->GetStyle() & WS_VSCROLL) != 0;
	}

	if (!m_expBox)
	{
		CRect rc(0, 0, 0, 0);

		// we'll have two containers; there are no drawbacks, but it will be possible to put toolbar above listctrl so
		// it won't be partially hidden by shadow
		m_expBoxContainer = new VACompletionBoxOwner(); // [case 142193] handles WM_MEASUREITEM because m_expBox has ID
		                                                // 0 which is ignored in reflection message
		int WS_EX_TOPMOST_FLAG = (gShellAttr->IsDevenv10OrHigher())
		                             ? WS_EX_TOPMOST
		                             : 0; // Needs to be topmost or re-parented in 2010 for detached windows.
		m_expBoxContainer->CreateEx(DWORD(WS_EX_TOOLWINDOW | WS_EX_LEFT | WS_EX_TOPMOST_FLAG), _T("Static"), _T(""),
		                            DWORD(WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_POPUP | WS_DLGFRAME),
		                            rc, AfxGetMainWnd(), 0);
		m_expBoxContainer->SetWindowRgn(::CreateRectRgn(0, 0, 0, 0),
		                                true); // make it invisible since it will break toolbar fades

		m_expBox = new VACompletionBox();
		m_expBox->Create(m_expBoxContainer);

		// [case 142193] move both windows to planned location, so they already operate with correct DPI
		m_expBox->SetWindowPos(nullptr, m_caretPos.x, m_caretPos.y, 0, 0,
		                       SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		m_expBoxContainer->SetWindowPos(nullptr, m_caretPos.x, m_caretPos.y, 0, 0,
		                                SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);

		m_expBoxDPI = VsUI::DpiHelper::GetDeviceDpiX();

		m_tBarContainer = new CWnd();
		uint style = WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_POPUP;
		if (!m_expBox->UseVsTheme())
		{
			if (gShellAttr->IsDevenv11OrHigher())
				style |= WS_BORDER;
			else
				style |= WS_DLGFRAME;
		}
		m_tBarContainer->CreateEx(
		    WS_EX_TOOLWINDOW | WS_EX_LEFT, _T("Static"), _T(""), style,
		    CRect(0, 0, VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(20), VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(20)),
		    AfxGetMainWnd(), 0);

		if (CVS2010Colours::IsVS2010CommandBarColouringActive())
		{
			const COLORREF toolbarbg_cache = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);
			m_tBar = new CToolbarWithBgColour(toolbarbg_cache);
		}
		else
			m_tBar = new CToolBar;

		m_tBar->CreateEx(m_tBarContainer, TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
		                 WS_VISIBLE | CBRS_ALIGN_TOP | WS_BORDER | WS_THICKFRAME | WS_CHILD | WS_CLIPCHILDREN |
		                     WS_CLIPSIBLINGS);
		m_tBar->SetOwner(m_expBox);

		if (!m_expBox->LC2008_DisableAll())
		{
			// WinXP WS_EX_LAYERED fix
			::SetClassLong(m_expBox->m_hWnd, GCL_STYLE,
			               LONG(::GetClassLong(m_expBox->m_hWnd, GCL_STYLE) & ~(CS_OWNDC | CS_CLASSDC | CS_PARENTDC)));
			::SetClassLong(
			    m_tBarContainer->m_hWnd, GCL_STYLE,
			    LONG(::GetClassLong(m_tBarContainer->m_hWnd, GCL_STYLE) & ~(CS_OWNDC | CS_CLASSDC | CS_PARENTDC)));

			if (m_expBox->LC2008_DoSetSaveBits())
			{
				::SetClassLong(m_expBoxContainer->m_hWnd, GCL_STYLE,
				               LONG(::GetClassLong(m_expBoxContainer->m_hWnd, GCL_STYLE) | CS_SAVEBITS));
				::SetClassLong(m_expBox->m_hWnd, GCL_STYLE,
				               LONG(::GetClassLong(m_expBox->m_hWnd, GCL_STYLE) | CS_SAVEBITS));
				::SetClassLong(m_tBarContainer->m_hWnd, GCL_STYLE,
				               LONG(::GetClassLong(m_tBarContainer->m_hWnd, GCL_STYLE) | CS_SAVEBITS));
				::SetClassLong(m_tBar->m_hWnd, GCL_STYLE,
				               LONG(::GetClassLong(m_tBar->m_hWnd, GCL_STYLE) | CS_SAVEBITS));
			}
			if (m_expBox->LC2008_HasFancyBorder())
			{
				::SetWindowPos(m_tBarContainer->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				::SetClassLong(m_tBarContainer->m_hWnd, GCL_STYLE,
				               LONG(::GetClassLong(m_tBarContainer->m_hWnd, GCL_STYLE) | CS_DROPSHADOW));
			}
			if (m_expBox->LC2008_HasCtrlFadeEffect() || m_expBox->LC2008_HasHorizontalResize() ||
			    m_expBox->LC2008_HasVerticalResize())
				m_expBox->AddCompanion(m_tBarContainer->m_hWnd, cdxCDynamicWnd::mdNone);
			// Causes VB to insert "()" when it looses focus in a method
			// 			extern const uint WM_POSTPONED_SET_FOCUS;
			// 			m_expBox->PostMessage(WM_POSTPONED_SET_FOCUS);
		}

		// apply all flags (required by MSDN)
		::SetWindowPos(m_expBoxContainer->m_hWnd, NULL, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_expBox->m_hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_tBarContainer->m_hWnd, NULL, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_tBar->m_hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);

		CDC* dc = m_expBox->GetDC();
		m_expBox->UpdateFonts(VAFTF_All, (uint)VsUI::DpiHelper::GetDeviceDpiX());
		HGDIOBJ oldFont = dc->SelectObject(m_expBox->GetDpiAwareFont(true));
		// Note from case: 54239
		// To calculate each character width, GetCharWidthW is used. MSDN says that
		// this function cannot be used for TrueType fonts (GetCharABCWidths should
		// be used for TT, but not for non TT). I decided not to touch this because
		// I was afraid to break width calculation further...
		GetCharWidthW(dc->GetSafeHdc(), 0, 255, g_charWidthsBold);
		dc->SelectObject(m_expBox->GetDpiAwareFont(false));
		GetCharWidthW(dc->GetSafeHdc(), 0, 255, g_charWidths);
		dc->SelectObject(oldFont);
		m_expBox->ReleaseDC(dc);
	}

	// delete all existing buttons
	int nCount = (int)m_tBar->SendMessage(TB_BUTTONCOUNT, 0, 0);
	while (nCount--)
		VERIFY(m_tBar->SendMessage(TB_DELETEBUTTON, 0, 0));

	SetImageList(GetImageList());

	if (IsFileCompletion())
		::mySetProp(m_expBox->m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	else
		::myRemoveProp(m_expBox->m_hWnd, "__VA_do_not_colour");

	m_expBox->m_compSet = this;
	m_xrefCount = 0;

	WTString iTxt;
	long img = 0;
	CDC* dc = m_expBox->GetDC();
	CSize maxSz(0, VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(16));
	// Look in expansion history to get most likely suggestion (if not invoked via Ctrl+Space)
	WTString histStr;
	int histPos = 100000;
	const bool selectFirstHeadMatch = false;
	// s_popType == ET_EXPAND_VA || // see bugs 433, 2114, 2052
	// s_popType == ET_EXPAND_TEXT;
	// not ET_EXPAND_VSNET - breaks C# IDE g_GuessMostLikely (bug 2052)
	if (startsWith.GetLength() && GetPopType() != ET_SUGGEST && !selectFirstHeadMatch && !IsFileCompletion())
	{
		histStr = g_ExpHistory->GetExpHistory(startsWith);
	}

	int matchItem = 0;
	const int kInitialMatchRank = 999;
	int matchRank = kInitialMatchRank;
	int tbItem = 0;
	WTString bestMatchTXT;
	bool isExactAutotextMatch = false;
	WTString autoTextMatch, vsSnippetMatch;
	if (!IsMembersList() && !IsFileCompletion())
	{
		int autoTextMatchInt = -1;
		if (gVsSnippetMgr)
		{
			vsSnippetMatch = gVsSnippetMgr->FindNextShortcutMatch(startsWith, autoTextMatchInt);
			autoTextMatchInt = -1;
		}

		autoTextMatch = gAutotextMgr->FindNextShortcutMatch(startsWith, autoTextMatchInt);
	}
	m_ed->Scope(); // update scope so we add items below as needed
	if (autoTextMatch.length() || vsSnippetMatch.length())
	{
		if (vsSnippetMatch.length())
			g_Guesses.SetMostLikely(vsSnippetMatch);
		else
			g_Guesses.SetMostLikely(autoTextMatch);

		if (autoTextMatch.length())
			gAutotextMgr->AddAutoTextToExpansionData(startsWith, &m_ExpData);

		if (vsSnippetMatch.length())
			gVsSnippetMgr->AddToExpansionData(startsWith, &m_ExpData);
	}
	BOOL bFocusRectOnly = FALSE;
	WTString potentialShortcutExt = ::GetPotentialShortcutExt(startsWith);

#ifdef USE_IDX_AS_BEST_MATCH
	if (GetIVsCompletionSet() && (JS != m_ed->m_ScopeLangType)) // their guesses are just alphabetical
	{
		WTString cwd = StartsWith();
		if (cwd.GetLength() && !ISCSYM(cwd[0]))
		{
			if (m_ed->m_ftype != XML) // XML: allow filtering on NonCSym's like "<!"
				cwd.Empty();
		}

		bool isOk = false;
		WTComBSTR bstr(cwd.Wide());
		long item = m_IvsBestMatchCached;
		DWORD flags = 0;
		try
		{
			// Note: GetBestMatch fails the second time it is called, so don't empty mostLikely if display is called
			// twice
			isOk = GetIVsCompletionSet()->GetBestMatch(bstr, (int)bstr.Length(), &item, &flags) == S_OK;
		}
		catch (const ATL::CAtlException& e)
		{
			// [case: 72245]
			// vs2012 Update 2 CTP html completion set throws an E_INVALIDARG
			// exception that it didn't use to (in VACompletionSetEx::SetIvsCompletionSet).
			vLogUnfiltered("ERROR VACS::DL  sw(%s) hr(%lx)", cwd.c_str(), e.m_hr);
		}

		if (isOk || item != -1)
		{
			// [case: 16277] the "!bstr.Length()" condition is necessary for C/C++
			// but it breaks C# linq
			if (!(flags & GBM_SELECT) && (!bstr.Length() || !IsCFile(gTypingDevLang)))
			{
				if (m_ed->m_ScopeLangType == VB)
				{
					bFocusRectOnly = bstr.Length() == 0; // Allow selecting of Acronyms in VB. Related to case=15202
					if (!bFocusRectOnly && m_isDef)
						bFocusRectOnly = TRUE; // [case: 62322]
				}
				else if (!isOk && item != -1 && m_IvsBestMatchFlagsCached & GBM_SELECT)
					bFocusRectOnly = FALSE; // [case: 48374]
				else
					bFocusRectOnly = TRUE;
			}

			// Get vsnet's initial suggestion, typing "new " in c# suggest a type variable getting assigned
			LPCWSTR wtext = NULL;
			long img2;
			GetIVsCompletionSet()->GetDisplayText(item, &wtext, &img2);
			if (wtext)
				g_Guesses.SetMostLikely(WTString(wtext));
		}
		else if (Is_Tag_Based(m_ed->m_ScopeLangType) && !cwd.GetLength())
			bFocusRectOnly = TRUE; // GetBestMatch fails in HTML, yet this needs un-selecting. case=22296
	}
	else
		g_Guesses.ClearMostLikely(); // We can get rid of all external logic which sets this.
#endif                               // USE_IDX_AS_BEST_MATCH

	if (!bFocusRectOnly && m_ed && m_ed->m_ScopeLangType == VB && StartsWith() == "_")
	{
		// [case: 53025]
		bFocusRectOnly = TRUE;
	}

	if (!bFocusRectOnly && m_startsWith.IsEmpty())
	{
		MultiParsePtr si = m_ed->GetParseDb();
		if (si->HasScopeSuggestions() &&
		    !si->m_xref) // Fixes problem with members list focus caused by ScopeSuggestions. case=53612
		{
			//  Don't allow complete on any if there is no startsWith  case=31567
			if (IsCFile(gTypingDevLang))
				bFocusRectOnly = TRUE;
			// Allow for c#/VB/?? "...= new " suggestions case=32256
			else if (_stricmp(si->m_LastWord.c_str(), "new") != 0 && _stricmp(si->m_LastWord.c_str(), "as") != 0 &&
			         _stricmp(si->m_LastWord.c_str(), "case") != 0)
				bFocusRectOnly = TRUE;
		}
	}

	WTString edSymScope;
	if (m_ed)
		edSymScope = m_ed->GetSymScope();
	if (m_ed && startsWith.GetLength() && ISCSYM(startsWith[0]) && edSymScope.GetLength() &&
	    edSymScope[0] == DB_SEP_CHR && // ensure valid symbol, not just "String" case=23074
	    !IsFileCompletion() &&
	    !Is_Tag_Based(m_ed->m_ftype) && // Not suggest current sym in JS/HTML files, messes with bits
	    JS != m_ed->m_ftype)
	{
		const bool kIsCfile = IsCFile(gTypingDevLang);

		// [case: 104120]
		DType dt(m_ed->GetSymDtype());
		if (dt.IsDbCpp() && !kIsCfile)
			; // Mixed C++/C#/VB sln share same SYSDB, this filters c++ code in C#/VB
		else if (dt.IsDbNet() && kIsCfile && GlobalProject && !GlobalProject->CppUsesClr() &&
		         !GlobalProject->CppUsesWinRT())
			; // Mixed C++/C#/VB sln share same SYSDB, this filters .NET symbols in pure c/c++ code
		else
		{
			// Add symbol to existing list
			AddString(TRUE, StrGetSym(edSymScope), dt.MaskedType(), dt.Attributes(),
			          WTHashKey(StrGetSym(edSymScope)), WTHashKey(StrGetSymScope(edSymScope)));
		}
	}

	const long count2 = GetCount();
	int totalItems = count2;
	int matched_idx = MAXFILTERCOUNT - 1;
	int maxStrLen = 0;
	for (uint i = 0; i < 255; i++)
		g_imgFilter[i] = g_imgFilter[i] & 0x1;

	// limit to MAXFILTERCOUNT for speed, but more importantly because of fixed array m_xref
	if (count2 < MAXFILTERCOUNT && !::EdPeekMessage(*m_ed))
	{
	again:
		long i;
		for (i = 0; i < count2; i++)
		{
			// #listboxFilter
			SymbolInfoPtr sinf = GetDisplayText(i, iTxt, &img);
			if (!sinf && iTxt.IsEmpty())
			{
				// if this occurs in a debug build, it could be due to focus change
				// causing call to g_CompletionSet->Dismiss() in EdCnt::ClearAllPopups.
				// comment out that Dismiss call while working on listboxes/completions.
				continue;
			}

			int autoTextShortcut = iTxt.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
			// dispText is the text that will be displayed without the autotext insertion text case=31884
			WTString dispText = (autoTextShortcut == -1) ? iTxt : iTxt.Mid(0, autoTextShortcut);
			const uint sinfType = sinf ? sinf->m_type : 0;

			// _ASSERTE(img < 255); // Asserts in XAML files, not sure what this assert is to test -Jer
			if (Is_Tag_Based(m_ed->m_ftype) || JS == m_ed->m_ftype)
			{
				if (sinfType == ET_SUGGEST || sinfType == ET_SUGGEST_BITS)
					if (iTxt == m_startsWith)
						continue;
			}
			if (tbItem < 8 && img < 255 && !(g_imgFilter[img] & 0x2))
				g_imgFilter[img] |= 2;
			if (m_typeMask && !(img < 255 && (g_imgFilter[img] & 0x1)))
				continue;

			if (GetIVsCompletionSet() && count2 > 2000 && !(i % 1024))
			{
				if (::EdPeekMessage(*m_ed))
				{
					// Allow user to keep typing, and we will display/filter when they stop typing.
					matchItem = GetBestMatch();
					vCatLog("Editor.Events", "VaEventLB HadEvent2 matchItem=%d", matchItem);
					if (matchItem == -1)
					{
						m_expBox->ReleaseDC(dc);
						m_expBox->SetTimer(VACompletionBox::Timer_ReDisplay, 100, NULL); // do later
						return;
					}
					break;
				}
			}

			{
				WTString truncatedCompletionSetItemText(iTxt);
				if (!IsFileCompletion() && sinfType != ET_SUGGEST_BITS) // only check for . in list if not a file list
				{
					const int kLastDotPos = truncatedCompletionSetItemText.ReverseFind('.');
					if (-1 != kLastDotPos)
					{
						// case=5493
						// regression from 3620.  C# completes entire declarations
						// and bodies, so can't filter on just kLastDotPos.
						const bool kIsSimpleText = truncatedCompletionSetItemText.FindOneOf(" \t(){};:") == -1;
						if (kIsSimpleText)
						{
							// case=3620
							// if the listbox contains items with dots, only compare
							// against the last substring to mirror the IDE behavior.
							truncatedCompletionSetItemText = truncatedCompletionSetItemText.Mid(kLastDotPos + 1);
						}
					}
				}

				int match = CS_None;

				if (sinfType == ET_SCOPE_SUGGESTION || sinfType == ET_AUTOTEXT_TYPE_SUGGESTION)
				{
					// Since this may be a limited set, only match items with substrings.
					if (truncatedCompletionSetItemText.FindNoCase(startsWith) != -1)
						match = CS_ContainsChars;
				}
				if (sinfType == ET_SUGGEST_BITS /*&& !IsMembersList()*/)
				{
					match = ContainsSubset(truncatedCompletionSetItemText, startsWith, SUBSET);
					if (!match && ISCSYM(startsWith[0]) != ISCSYM(truncatedCompletionSetItemText[0]))
						match = CS_ContainsChars; // StartsWith may contain opening quote, allow all matches
					if (!match)
						continue;
				}
				else if (IS_IMG_TYPE(sinfType) && !(sinfType & VSNET_TYPE_BIT_FLAG) &&
				         (TYPE_TO_IMG_IDX(sinfType) == ICONIDX_REFACTOR_INSERT_USING_STATEMENT ||
				          TYPE_TO_IMG_IDX(sinfType) == ICONIDX_REFACTOR_RENAME))
					match = CS_ContainsChars; // ICONIDX_* icon, force match
				else if (autoTextShortcut != -1 &&
				         ((startsWithAutoText.GetLength() && iTxt.Mid(autoTextShortcut + 1) == startsWithAutoText) ||
				          (potentialShortcutExt.GetLength() && iTxt.Mid(autoTextShortcut + 1) == potentialShortcutExt)))
				{
					// #exactSnippetMatch, either via homogeneous character classes in startsWith or via mixed classes
					// in potentialShortcutExt
					if (matchRank) // take first AutoText
					{
						matchItem = m_xrefCount;
						matchRank = 0;
					}
					match = CS_ExactMatch; // autotext shortcut
					isExactAutotextMatch = true;
				}
				else if (autoTextShortcut != -1 && matchRank >= CS_StartsWith)
				{
					// Filtering snippets
					if (startsWith.GetLength() && iTxt.Left(autoTextShortcut).FindNoCase(startsWith) == 0)
					{
						// special-case autotext because ContainsSubset will not return
						// CS_StartWith if startsWith and the autotext are both lower-case.
						// Don't change ContainsSubset because we don't want to punish people
						// that type in all lower-case (relying on VA case correction).
						const WTString aTxt(iTxt.Left(autoTextShortcut));
						if (aTxt == startsWith)
							match = CS_ExactMatch;
						else if (aTxt.Find(startsWith) == 0)
							match = CS_StartsWith;
						else if (aTxt.CompareNoCase(startsWith) == 0)
							match = CS_ExactMatchNC;
						else
							match = CS_StartsWithNC;
					}
					else if (potentialShortcutExt.GetLength() &&
					         iTxt.contains(WTString(AUTOTEXT_SHORTCUT_SEPARATOR) + potentialShortcutExt))
					{
						// use of 'contains' means this is not an exact match (exact match occurs at:#exactSnippetMatch)
						match = CS_StartsWith;
					}
					else
						match = ContainsSubset(truncatedCompletionSetItemText, startsWith, matchFlags);
					if(!match && Psettings->m_bShrinkMemberListboxes /*|| (match >= CS_ContainsChars && !IsMembersList() && m_popType != ET_EXPAND_VSNET)*/)
						continue;
				}
				else if (sinf && sinfType >= IMG_IDX_TO_TYPE(ICONIDX_FILE_FIRST) &&
				         sinfType <= IMG_IDX_TO_TYPE(ICONIDX_FILE_LAST))
				{
					if (startsWith.IsEmpty() && !matchItem && IsCFile(gTypingDevLang))
					{
						// [case: 41565] during file completion, if there is no startsWith text,
						// make first item visible (similar logic for autotext)
						if (matchRank)
						{
							matchItem = m_xrefCount;
							matchRank = 0;
						}
						match = CS_ExactMatch;
					}
					else
						match = ContainsSubset(truncatedCompletionSetItemText, startsWith, matchFlags);
					if (!match)
						match = ContainsSubset(iTxt, startsWith, matchFlags);
					if(!match && Psettings->m_bShrinkMemberListboxes /*|| (match >= CS_ContainsChars && !IsMembersList() && m_popType != ET_EXPAND_VSNET)*/)
						continue;
				}
				else
				{
					match = ContainsSubset(truncatedCompletionSetItemText, startsWith, matchFlags);
					if (!match && Psettings->m_bShrinkMemberListboxes /*|| (match >= CS_ContainsChars && !IsMembersList() && m_popType != ET_EXPAND_VSNET)*/)
						continue;
				}
				ASSERT_ONCE(match || !Psettings->m_bShrinkMemberListboxes);

				if (iTxt == g_Guesses.GetMostLikelyGuess() /*&& matchRank > CS_ExactMatchNC*/)
				{
					if (match != CS_ExactMatch)
					{
						if (strncmp(startsWith.c_str(), iTxt.c_str(), (uint)startsWith.GetLength()) == 0)
							match = CS_MostLikely; // really likely suggestion
						else if (_tcsnicmp(startsWith.c_str(), iTxt.c_str(), (uint)startsWith.GetLength()) == 0)
							match = CS_ExactMatchNC; // likely suggestion
						else if (match)
							match--; // if we do not get a better suggestion take this one
					}
				}
				if (matchRank > CS_ExactMatchNC)
				{
					if (doCase && strncmp(iTxt.c_str(), startsWith.c_str(), (uint)startsWith.GetLength()) == 0)
					{
						doCase = FALSE;
						if (match > CS_MostLikely) // don't reduce CS_ExactMatch
							match = CS_MostLikely; // Really likely suggestion
					}
					else if (match && histStr.GetLength())
					{
						// Look in history list
						int p = histStr.find(SPACESTR + iTxt.c_str() + SPACESTR);
						if (p != -1 && p < histPos)
						{
							// this trumps previous tests, not sure if it should? -Jer
							histPos = p;
							matchRank = match;
							matchItem = m_xrefCount;
							bestMatchTXT = iTxt;
						}
					}
				}
#ifdef USE_IDX_AS_BEST_MATCH
				if (sinf && !m_isMembersList && match && match <= matchRank && sinf->m_idx < matched_idx &&
				    (!selectFirstHeadMatch || matchRank == kInitialMatchRank))
				{
					matched_idx = sinf->m_idx;
					matchRank = match;
					matchItem = m_xrefCount;
					bestMatchTXT = iTxt;
				}
				else if (sinf && match && match < matchRank &&
				         (!selectFirstHeadMatch || matchRank == kInitialMatchRank))
				{
					matched_idx = sinf->m_idx;
					matchRank = match;
					matchItem = m_xrefCount;
					bestMatchTXT = iTxt;
				}
				else if (sinf && match && match == matchRank &&
				         (!selectFirstHeadMatch || matchRank == kInitialMatchRank) && match == CS_StartsWithNC)
				{
					if (!bestMatchTXT.CompareNoCase(iTxt))
					{
						if (::StartsWith(iTxt, startsWith, FALSE))
						{
							// [case: 93633]
							// if two suggestions are identical except for case, then prefer matching case
							// (even if all lower-case; see:#lowercaseMatchOverridden)
							matched_idx = sinf->m_idx;
							matchRank = match;
							matchItem = m_xrefCount;
							bestMatchTXT = iTxt;
						}
					}
				}
#else
				if (match && match < matchRank && (!selectFirstHeadMatch || matchRank == kInitialMatchRank))
				{
					matchRank = match;
					matchItem = m_xrefCount;
					bestMatchTXT = iTxt;
				}
#endif // USE_IDX_AS_BEST_MATCH
			}

			m_xref[m_xrefCount++] = i;
			const int curItemLen = dispText.GetLength(); // Size to displayed text case=31884
			if (m_xrefCount <= (DISPLAYLINES * 2) || curItemLen > maxStrLen)
			{
				if (curItemLen > maxStrLen)
					maxStrLen = curItemLen;
				int w;
				WTString truncatedStr = TruncateString(dispText); // Size to displayed text case=31884
				if (m_expBox->LC2008_DisableAll() || !m_expBox->LC2008_DoFixAutoFitWidth())
					w = GetStrWidthEx(truncatedStr.c_str(), g_charWidthsBold);
				else
					w = m_expBox->GetStringWidth(truncatedStr.c_str());
				if (w > maxSz.cx)
					maxSz.cx = w;
			}
		}

		if (!m_xrefCount && (m_typeMask || (Psettings->m_bAllowShorthand && !(matchFlags & SUBSET))))
		{
			m_typeMask = 0;
			matchFlags |= SUBSET;
			goto again;
		}

		totalItems = m_xrefCount + (count2 - i);
		if (count2 > MAXFILTERCOUNT)
		{
			totalItems = count2;
			m_xrefCount = 0;
		}
	}
	else
	{
		if (count2 >= MAXFILTERCOUNT)
		{
			vCatLog("Editor.Events", "VaEventLB MAXFILTERCOUNT count=%ld", count2);
		}
		else
			vCatLog("Editor.Events", "VaEventLB HadEvent count=%ld", count2);

		// If this is a really long list, we might do a binary search for beginning string.
		// only check first DISPLAYLINES*3 items in list
		for (long i = 0; i < count2 && i < (DISPLAYLINES * 3); i++)
		{
			GetDisplayText(i, iTxt, &img);
			int w;
			if (m_expBox->LC2008_DisableAll() || !m_expBox->LC2008_DoFixAutoFitWidth())
				w = GetStrWidthEx(iTxt.c_str(), g_charWidthsBold);
			else
				w = m_expBox->GetStringWidth(iTxt.c_str());
			if (w > maxSz.cx)
				maxSz.cx = w;
		}

		matchItem = GetBestMatch();
		if (matchItem == -1)
		{
			// unselect default item to prevent strange insertion on space [case=37766]
			m_expBox->ReleaseDC(dc);
			int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
			m_expBox->SetItemState(item, NULL, LVIS_SELECTED | LVIS_FOCUSED);
			m_expBox->SetTimer(VACompletionBox::Timer_ReDisplay, 100, NULL); // do later
			return;
		}

		// [case: 37498] check items around matchItem for width sizing
		// additionally check 10 items before and after matchItem
		for (long i = matchItem - 10; i > 0 && i < count2 && i < matchItem + 10; i++)
		{
			GetDisplayText(i, iTxt, &img);
			int w;
			if (m_expBox->LC2008_DisableAll() || !m_expBox->LC2008_DoFixAutoFitWidth())
				w = GetStrWidthEx(iTxt.c_str(), g_charWidthsBold);
			else
				w = m_expBox->GetStringWidth(iTxt.c_str());
			if (w > maxSz.cx)
				maxSz.cx = w;
		}
	}

	int tbIconsOffset = g_ExImgCount;
	if (GetPopType() == ET_SUGGEST && !IsFileCompletion())
	{
		TBBUTTON b;
		b.iString = -1;
		b.fsState = TBSTATE_ENABLED;
		// i is > the size of g_imgFilter array here -- bug exists in very first version of file
		//		if(m_typeMask && g_imgFilter[(uint)i]&0x1)
		//			b.fsState |= TBSTATE_PRESSED|TBSTATE_CHECKED;
		b.fsStyle = 0;
		{
			// Add expand icon
			tbItem++;
			b.iBitmap = ICONIDX_EXPANSION + tbIconsOffset;
			b.idCommand = ICONIDX_EXPANSION | VA_TB_CMD_FLG;
			b.dwData = ICONIDX_EXPANSION;
			m_tBar->GetToolBarCtrl().AddButtons(1, &b);
		}
	}
	else
	{
		if (!IsFileCompletion())
		{
			// Add button for m_bListNonInheritedMembersFirst
			tbItem++;
			TBBUTTON b;
			b.iString = -1;
			b.fsState = TBSTATE_ENABLED;
			if (Psettings->m_bListNonInheritedMembersFirst)
				b.fsState |= TBSTATE_PRESSED | TBSTATE_CHECKED;
			b.fsStyle = 0;
			b.iBitmap = ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST + tbIconsOffset;
			b.idCommand = ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST | VA_TB_CMD_FLG;
			b.dwData = ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST;
			m_tBar->GetToolBarCtrl().AddButtons(1, &b);
		}
		for (int i = 0; i < 255; i++)
		{
			if (tbItem < 26 && img < 255 && (g_imgFilter[i] & 0x2))
			{
				TBBUTTON b;
				b.iString = -1;
				b.fsState = TBSTATE_ENABLED;
				if (m_typeMask && g_imgFilter[i] & 0x1)
					b.fsState |= TBSTATE_PRESSED | TBSTATE_CHECKED;
				b.fsStyle = 0;
				tbItem++;
				int cmd = i;
				b.iBitmap = cmd;
				b.idCommand = cmd;
				b.dwData = (DWORD_PTR)cmd;
				m_tBar->GetToolBarCtrl().AddButtons(1, &b);
			}
		}
	}

	// [case: 142247]
	// reduce listbox flicker/paint, but revert to old behavior when
	// backspacing due to white flashing when box grows
	const bool stoppedRedraw = startsWith.GetLength() >= mPreviousStartsWith.GetLength();
	if (stoppedRedraw)
		m_expBox->SetRedraw(FALSE);

	m_expBox->SetItemCount(totalItems);
	m_expBox->ReleaseDC(dc);
	int displayedItems = min(DISPLAYLINES, totalItems);
	if (GetPopType() == ET_SUGGEST)
	{
		// limit the number of suggestions displayed
		displayedItems = min(Psettings->mInitialSuggestionBoxHeightInItems, totalItems);
	}

	const int scrollWidth = (totalItems > displayedItems || (wasVis && hadScroll))
	                            ? VsUI::DpiHelper::GetSystemMetrics(SM_CXVSCROLL) +
	                                  (m_expBox->UseVsTheme() ? 0 : VsUI::DpiHelper::LogicalToDeviceUnitsX(1))
	                            : 0;
	if ((m_pt.y + (m_expBoxContainer->GetItemHeight() * displayedItems + 1) +
	     VsUI::DpiHelper::LogicalToDeviceUnitsY(20)) > g_FontSettings->GetDesktopEdges(m_ed.get()).bottom)
	{
		if (!m_AboveCaret)
			m_pt.y -= (g_FontSettings->GetCharHeight() + VsUI::DpiHelper::LogicalToDeviceUnitsY(5));
		m_AboveCaret = TRUE;
	}

	// add some more padding due to last letter of longest item sometimes being partially cut-off
	maxSz.cx += VsUI::DpiHelper::LogicalToDeviceUnitsY(10);

	int boxwidth = maxSz.cx + VsUI::DpiHelper::LogicalToDeviceUnitsX(3) +
	               VsUI::DpiHelper::GetSystemMetrics(SM_CXSMICON) + scrollWidth + (m_expBox->GetFrameWidth() * 2);
	int boxheight =
	    (m_expBoxContainer->GetItemHeight() * displayedItems + 1) +
	    VsUI::DpiHelper::LogicalToDeviceUnitsY(6); // the 1 and 6 are magical numbers are needed for Vista. case=31629

	if (!m_expBox->LC2008_DisableAll())
	{
		if (!m_expBox->LC2008_HasFancyBorder())
			boxwidth += VsUI::DpiHelper::LogicalToDeviceUnitsX(9); // case: 52092
		if (m_expBox->LC2008_DoRestoreLastWidth() && m_expBox->LC2008_HasHorizontalResize())
		{
			CRedirectRegistryToVA rreg;
			boxwidth = (int)::AfxGetApp()->GetProfileInt("WindowPositions", "AutoComplete_width", boxwidth);
		} /*else if(m_expBox->LC2008_DoFixAutoFitWidth()) {
		    m_expBox->SetColumnWidth(0, LVSCW_AUTOSIZE);
		    CRect rect(0, 0, m_expBox->GetColumnWidth(0) + GetSystemMetrics(SM_CXVSCROLL) + 3, 0);
		    ::AdjustWindowRectEx(rect, ::GetWindowLong(m_expBox->m_hWnd, GWL_STYLE), false,
		    ::GetWindowLong(m_expBox->m_hWnd, GWL_EXSTYLE)); boxwidth = rect.Width();
		    }*/
		if (m_expBox->LC2008_DoRestoreLastHeight() && m_expBox->LC2008_HasVerticalResize())
		{
			CRedirectRegistryToVA rreg;
			boxheight = (int)::AfxGetApp()->GetProfileInt("WindowPositions", "AutoComplete_height", boxheight);
		}
	}

	if (wasVis)
	{
		// [case: 70878] reduce box movement by retaining old width if new width is
		// less than old width (unless new width will result in kMinSavings px savings)
		const int kMinSavings = VsUI::DpiHelper::LogicalToDeviceUnitsX(125);
		CRect origRc;
		m_expBox->GetWindowRect(&origRc);
		const int oldWidth = origRc.Width();
		if (boxwidth < oldWidth && (boxwidth + kMinSavings) >= oldWidth)
		{
			_ASSERTE(oldWidth > boxwidth);
			const int diff = oldWidth - boxwidth;
			maxSz.cx += diff;
			// reduce flicker by retaining old width
			boxwidth = oldWidth;
		}
	}

	CRect rc(m_pt.x, m_pt.y, m_pt.x + boxwidth, m_pt.y + boxheight);
	rc.bottom += gShellAttr->GetCompletionListBottomOffset();
	if (m_AboveCaret)
		rc.SetRect(m_pt.x, m_pt.y - boxheight, m_pt.x + boxwidth, m_pt.y);
	if (!m_typeMask || !m_tBarContainer->IsWindowVisible())
	{
		if (!m_expBox->LC2008_DisableAll() &&
		    (m_expBox->LC2008_HasHorizontalResize() || m_expBox->LC2008_HasVerticalResize()))
			m_expBox->GetSizingRect(uint(m_AboveCaret ? WMSZ_TOP : WMSZ_BOTTOM), rc);

		g_FontSettings->RestrictRectToMonitor(rc, m_ed.get()); // case=5178
		m_expBox->MoveWindow(&rc);

		CRect rtb(0, 0, VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(23 * tbItem),
		          VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(16 + 10));
		rtb.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(3);
		if (m_expBox->UseVsTheme())
			rtb.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(7);
		else if (gShellAttr->IsDevenv11OrHigher())
			rtb.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(5);

		// convert client to full window rect
		CRect tbc_screen;
		CRect tbc_client;
		m_tBarContainer->GetWindowRect(tbc_screen);
		m_tBarContainer->GetClientRect(tbc_client);
		rtb.right += tbc_screen.Width() - tbc_client.Width();

		m_tBar->MoveWindow(rtb);
		if (m_AboveCaret)
			rtb.OffsetRect(rc.left, rc.top - rtb.Height());
		else
			rtb.OffsetRect(rc.left, rc.bottom);
		if (m_expBox->LC2008_HasFancyBorder())
			rtb.OffsetRect(m_expBox->GetFrameWidth(), 0);
		if (m_expBox->UseVsTheme())
			rtb.OffsetRect(0, -VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
		
		//-----------------------------------------------------------------------------------------------
		// WARNING: following code is done for case 164000 and is fixing misalignment of m_tBarContainer
		// with buttons in m_tBar on scale factors bigger than 100%. If there will in the future be any
		// change directly on the m_tBar regarding high DPI support, this calculation should be removed  
		//-----------------------------------------------------------------------------------------------
		CRect rtbBCFix = rtb;
		rtbBCFix.left -= 1; // [case: 164000] for some reason m_tBarContainer is misaligned by one pixel on the left, fix it here
		
		// [case: 164000] calculate offset for misalignment of m_tBarContainer width
		int buttonOffsetCorrectionWidth = 0;
		double factorX = VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX();
		if (factorX < 1.25f)
			buttonOffsetCorrectionWidth = 0;
		else if (factorX < 1.5f)
			buttonOffsetCorrectionWidth = 1;
		else if (factorX < 2.0f)
			buttonOffsetCorrectionWidth = 2;
		else if (factorX < 2.5f)
			buttonOffsetCorrectionWidth = 3;
		else if (factorX < 3.5f)
			buttonOffsetCorrectionWidth = 4;
		else
			buttonOffsetCorrectionWidth = 5;

		rtbBCFix.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(buttonOffsetCorrectionWidth * tbItem);
		
		// [case: 164000] calculate offset for misalignment of m_tBarContainer height
		int buttonOffsetCorrectionHeight = 0;
		double factorY = VsUI::DpiHelper::LogicalToDeviceUnitsScalingFactorX();
		if (factorY < 1.25f)
			buttonOffsetCorrectionHeight = 0;
		else if (factorY < 1.5f)
			buttonOffsetCorrectionHeight = 1;
		else if (factorY < 1.75f)
			buttonOffsetCorrectionHeight = 2;
		else if (factorY < 3.0f)
			buttonOffsetCorrectionHeight = 3;
		else
			buttonOffsetCorrectionHeight = 4;

		rtbBCFix.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(buttonOffsetCorrectionHeight);

		m_tBarContainer->MoveWindow(&rtbBCFix);
		m_expBoxContainer->MoveWindow(&rtb);

		if (m_expBox->LC2008_DisableAll() || !m_expBox->LC2008_DoRestoreLastWidth())
		{
			int colWidth = maxSz.cx + VsUI::DpiHelper::GetSystemMetrics(SM_CXSMICON);
			if (m_expBox->LC2008_DisableAll() || m_expBox->LC2008_HasFancyBorder())
			{
				if (g_FontSettings->GetDpiScaleX() > 1)
					colWidth += VsUI::DpiHelper::LogicalToDeviceUnitsX(3);
				else
					colWidth += VsUI::DpiHelper::LogicalToDeviceUnitsX(4);
			}
			else
				colWidth += VsUI::DpiHelper::LogicalToDeviceUnitsX(8);

			m_expBox->SetColumnWidth(0, colWidth);
		}

		m_tBar->Invalidate(TRUE);
		m_expBox->SetTimer(VACompletionBox::Timer_DismissIfCaretMoved, 600, NULL);
		// Add a 1 second delay before displaying tip to cut down on noise
		// [case: 94723] reduce timer for vs2015 C# async tip
		const uint timerTime = gShellAttr && gShellAttr->IsDevenv14OrHigher() && gTypingDevLang == CS ? 900u : 1000u;
		m_expBox->SetTimer(VACompletionBox::Timer_UpdateTooltip, timerTime, NULL);
	}

	if (m_ed != g_currentEdCnt)
	{
		Dismiss();
		return;
	}
	SymbolInfoPtr sinf = GetDisplayText(matchItem, iTxt, &img);
	if (sinf && displayedItems == 1 && matchRank == CS_ExactMatch)
	{
		if (IsMembersList())
		{
			Dismiss();
			return;
		}
		else
		{
			// make sure not snippet
			if (sinf->m_type != VSNET_RESERVED_WORD_IDX && sinf->m_type != ET_AUTOTEXT && sinf->m_type != ET_VS_SNIPPET)
			{
				if (sinf->m_type != ET_SUGGEST_BITS &&
				    !GetIVsCompletionSet()) // CS/VB list boxes do hide here, fixes prob2 in case:21347
				{
					// during file completion, the dot will break m_startsWith
					if (!(IsFileCompletion() && iTxt != startsWith))
						displayedItems = 0;
				}

				if (RESWORD == sinf->m_type && CS == gTypingDevLang && Psettings->IsAutomatchAllowed() &&
				    startsWith == "new" && sinf->mSymStr == startsWith)
				{
					// [case: 55775] don't leave a lone 'new' being displayed if it's already
					// been typed (see previous change for 55775 in VACompletionSetEx.cpp)
					displayedItems = 0;
				}
			}
		}
	}
	else if (!IsMembersList() && !isExactAutotextMatch && Psettings->mCloseSuggestionListOnExactMatch)
	{
		if (matchRank == CS_ExactMatch ||
		    (iTxt == WTString(startsWith) && (GetPopType() == ET_SUGGEST || GetPopType() == ET_SUGGEST_BITS)))
		{
			Dismiss();
			return;
		}
	}

	// [case:164000] do not check for m_typeMask (commented) since we always want to show Filtering Toolbar
	ShowFilteringToolbar(Psettings->m_bDisplayFilteringToolbar && displayedItems && /* m_typeMask && */
	                     (GetPopType() != ET_SUGGEST) && (GetPopType() != ET_SUGGEST_BITS));

	BOOL unselect = FALSE;
	if (sinf && IS_IMG_TYPE(sinf->m_type) && sinf->m_type == IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_INSERT_USING_STATEMENT))
		unselect = TRUE; // Never select insert using suggestion

	// unselect suggestions if isDef or current symbol is already defined
	if ((m_popType == ET_SUGGEST || m_popType == ET_SUGGEST_BITS) &&
	    startsWith[0] != '~' && // isDef is messed up for constructors, case=47139
	    startsWith[0] != '!' && (m_ed && (m_ed->GetParseDb()->m_isDef || !m_ed->m_isValidScope)))
		unselect = TRUE;
	// 	if (!unselect && gShellAttr->IsDevenv11OrHigher() &&
	// 		m_startsWith.GetLength() == 0 && IsMembersList() && !matchItem)
	// 	{
	// 		// [case: 70957]
	// 		bFocusRectOnly = TRUE;
	// 	}
	if (unselect && JS == m_ed->m_ScopeLangType && !m_isDef)
		unselect = FALSE; // always select in JS except for isDef
	if (unselect && Is_Tag_Based(m_ed->m_ScopeLangType))
		unselect = FALSE; // always select in HTML

	if (TRUE == unselect)
	{
		// 2 == Psettings->mSuggestionSelectionStyle can wreak havoc with the
		// 10.4 listbox changes - reg entry renamed to force reset (case=8468)
		if (2 == Psettings->mSuggestionSelectionStyle)
		{
			// Do FocusRectOnly to denote "complete on any" is not available. case=48521
			unselect = FALSE;
			bFocusRectOnly = TRUE;
		}
		else
		{
			// don't unselect if defining a function and already typed ) (case=1773)
			const WTString prevWord(m_ed->CurWord(-1));
			if (prevWord == " " || prevWord == "\t")
			{
				const WTString prevWord2(m_ed->CurWord(-2));
				const int prevWord2Len = prevWord2.GetLength();
				if (prevWord2Len && prevWord2[prevWord2Len - 1] == ')')
					unselect = FALSE;
			}
		}
	}
	OverrideSelectMode(sinf, unselect, bFocusRectOnly);
	if (unselect /*|| curData */ || (!matchItem && matchRank == kInitialMatchRank))
	{
		// nothing matches
		m_expBox->SetItemState(0, LVIS_FOCUSED | LVIS_SELECTED,
		                       LVIS_SELECTED | LVIS_FOCUSED);          // select first to unselect any previous
		m_expBox->SetItemState(0, NULL, LVIS_SELECTED | LVIS_FOCUSED); // unselect first
		m_expBox->EnsureVisible(0, false); // Nothing selected, make sure first item is visible. case=31848
		vCatLog("Editor.Events", "VaEventLB   Display displayedItems=%d, NoSel '%s'", displayedItems, startsWith.c_str());
	}
	else
	{
		m_expBox->SetItemState(matchItem + DISPLAYLINES - 1, LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		if (bFocusRectOnly)
			m_expBox->SetItemState(matchItem, LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		else
			m_expBox->SetItemState(matchItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		vCatLog("Editor.Events", "VaEventLB   Display displayedItems=%d, '%s' %s SoFar='%s'", displayedItems,
		     sinf ? sinf->mSymStr.c_str() : "", bFocusRectOnly ? "RectOnly" : "Selected", startsWith.c_str());
		if (totalItems != displayedItems)
		{
			// place default suggestion at top of list box
			// [case: 70878] this can cause flicker if listbox was already
			// displayed before current call to DisplayList, so be more selective
			// about the conditions under which it is invoked in vs2012
			if (!previousCnt || previousCnt > totalItems || !wasVis || !gShellAttr->IsDevenv11OrHigher() ||
			    !(BOOL)ListView_IsItemVisible(m_expBox->GetSafeHwnd(), matchItem))
			{
				int lastvisible = min((matchItem + displayedItems), (totalItems - 1));
				m_expBox->EnsureVisible(lastvisible, false);
				if (matchItem != lastvisible)
					m_expBox->EnsureVisible(matchItem, false);
			}
		}
	}

	if (stoppedRedraw)
		m_expBox->SetRedraw(TRUE);

	if (displayedItems)
	{
		if (!m_expBox->IsWindowVisible() && (GetWindowLong(m_expBox->m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
		    m_expBox->LC2008_HasCtrlFadeEffect())
		{
			SetLayeredWindowAttributes_update(m_expBox->m_hWnd, RGB(0, 0, 0), 0, LWA_ALPHA);
			m_expBox->make_visible_after_first_wmpaint = true;
			m_expBox->Invalidate();
		}

		m_expBox->ShowWindow(SW_SHOWNOACTIVATE);
		if (scrollWidth && !(m_expBox->GetStyle() & WS_VSCROLL))
		{
			// [case: 64898] [case: 65177] assign space reserved for scrollbar instead to content
			m_expBox->SetColumnWidth(0, m_expBox->GetColumnWidth(0) + scrollWidth);
		}

		// [case: 111563]
		// check again now that window is visible
		if (!(BOOL)ListView_IsItemVisible(m_expBox->GetSafeHwnd(), matchItem))
			m_expBox->EnsureVisible(matchItem, false);

		if (gShellAttr->IsDevenv10OrHigher())
		{
			if (!wasVis)
			{
				// [case: 70878] flicker while typing if box was already open
				::SetWindowPos(m_expBoxContainer->m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
				::SetWindowPos(m_expBox->m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
				::SetWindowPos(m_expBoxContainer->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
				::SetWindowPos(m_expBox->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
			}

			// Prevent members list from opening over/under param info in 2010 [case=40685]
			CRect rw;
			m_expBox->GetWindowRect(&rw);
			if (m_AboveCaret)
				m_ed->ReserveSpace(m_pt.y - rw.Height(), m_pt.x, rw.Height(), rw.Height());
			else
				m_ed->ReserveSpace(m_pt.y, m_pt.x, rw.Height(), rw.Height());
			mSpaceReserved = true;
		}
	}
	else
	{
		m_expBox->ShowWindow(SW_HIDE);
		ShowFilteringToolbar(false);
	}

	if (!gShellAttr->IsDevenv10OrHigher())
		m_ed->vSetFocus();

	if (m_ed && m_ed->m_ttParamInfo->GetSafeHwnd())
		m_ed->m_ttParamInfo->RedrawWindow(); // to fix any overlap. case=31814

	// Hide Tomato tip [case=36743]
	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->Dismiss();
	m_displayUpToDate = true;
	//  don't display [A...] icon until user mouses over to cut down on noise
	//	if(displayedItems && GetPopType() == ET_SUGGEST && !(Is_Tag_Based(m_ed->m_ftype) || JS == m_ed->m_ftype))
	//		ShowFilteringToolbar();
}

BOOL VACompletionSet::IsExpUp(EdCntPtr ed)
{
	if ((!ed || m_ed == ed) && m_expBox && m_expBox->GetSafeHwnd() && m_expBox->IsWindowVisible())
		return TRUE;
	return FALSE;
}

WTString VACompletionSet::GetCompleteOnCharSet(symbolInfo* sinf, int lang)
{
	static const WTString kDefaultCompleteWithAnyChars("{}[],:;+-*/%&|^!~=<>?@#'\"\\.() ");
	int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
	BOOL bSelected = (item != -1) && m_expBox->GetItemState(item, LVIS_SELECTED) != 0;
	if (!bSelected)
	{
		if (Psettings->mListboxCompletionRequiresSelection)
		{
			// [case: 140535]
			return WTString("");
		}

		// Psettings->m_bCompleteWithTab ignored on FocusRectOnly
		if (m_ed && m_ed->m_ScopeLangType == VB && StartsWith() == "_")
		{
			// [case: 53025] don't complete _ on return
			return WTString("\t");
		}

		// FocusRectOnly, accepted only on <tab>, or <enter> if option is set. case=16277, case=48521, case=71317
		if (Psettings->m_bCompleteWithReturn)
		{
			bool completeWithReturn = true;
			if (sinf && sinf->m_type == ET_VS_SNIPPET)
			{
				if (m_startsWith == "else" || m_startsWith == "do")
				{
					int pos = sinf->mSymStr.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
					if (-1 != pos)
					{
						WTString shortcut(sinf->mSymStr.Mid(pos + 1));
						pos = shortcut.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
						if (-1 != pos)
						{
							shortcut = shortcut.Left(pos);
							if (m_startsWith == shortcut)
							{
								// [case: 73604]
								// don't insert on return for VC++ else and do snippets
								// if user already typed the whole reserved word.
								// Basically override the m_bCompleteWithReturn setting
								// for these 2 default snippets since what has been typed
								// matches VC++ snippets and are also reserved words that
								// are typically followed by linebreaks.
								completeWithReturn = false;
							}
						}
					}
				}
			}

			if (completeWithReturn)
				return WTString("\t\r");
		}

		return WTString("\t");
	}

	if (lang == CS)
	{
		WTString commitOn = g_IdeSettings->GetEditorStringOption("CSharp-Specific", "CompletionCommitCharacters");
		if (commitOn.IsEmpty())
		{
			// vs2015 doesn't offer the CompletionCommitCharacters setting
			// in vs2015, similar to case:58703 in VB, ignore CompleteWithAny to emulate IDE behavior
			bool completeWithAny = gShellAttr->IsDevenv14OrHigher() ? true : Psettings->m_bCompleteWithAny;
			if (completeWithAny && Psettings->m_bCompleteWithAnyVsOverride)
			{
				// [case: 67723] optionally override VS emulation
				completeWithAny = false;
			}

			// case=25335
			if (completeWithAny)
				commitOn += kDefaultCompleteWithAnyChars;
			if (Psettings->m_bCompleteWithReturn)
				commitOn += WTString("\r");
			if (Psettings->m_bCompleteWithTab)
				commitOn += WTString("\t");
			return commitOn;
		}

		// Psettings->m_bCompleteWithAny ignored if there are IDE set completionCommitChars

		if (Psettings->m_bCompleteWithTab)
			commitOn += WTString("\t");
		// Psettings->m_bCompleteWithReturn ignored in C# if there are IDE set completionCommitChars (?)
		if (g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "CompleteOnNewline"))
			commitOn += "\r";
		if (g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "CompleteOnSpace"))
			commitOn += " ";
		return commitOn;
	}

	if (IsCFile(lang) || !GetIVsCompletionSet()) // Use VA expansion rules whenever VS doesn't offer a
	                                             // suggestion(VBS/VB2005/...) case=24501
	{
		WTString commitOn;

#ifdef AVR_STUDIO
		if (Psettings->m_bCompleteWithAny || (Psettings->mMembersCompleteOnAny && IsMembersList() && IsCFile(lang)))
		{
			commitOn = kDefaultCompleteWithAnyChars;
		}
#else
		if (Psettings->mMembersCompleteOnAny && IsMembersList() && IsCFile(lang)) // MembersExpandOnAny for case=9369
		{
			// special handling for xref completion in C/C++ - but not in dev11+
			// (mMembersCompleteOnAny is always false in v11+)
			_ASSERTE(!gShellAttr->IsDevenv11OrHigher());
			commitOn = kDefaultCompleteWithAnyChars;
		}

		if (IsCFile(lang) && gShellAttr->IsDevenv11OrHigher())
		{
			// Psettings->m_bCompleteWithAny ignored if there are IDE set completionCommitChars
			// (similar to C# behavior) (only in v11+)
			commitOn = g_IdeSettings->GetEditorStringOption("C/C++ Specific", "MemberListCommitCharacters");
		}

		if (commitOn.IsEmpty() && Psettings->m_bCompleteWithAny)
		{
			// no VsCompletionSet or non-xref C/C++ list
			if (IsCFile(lang) && gShellAttr->IsDevenv11OrHigher())
			{
				// not sure if this makes sense
				// user deleted commit chars, but has our option enabled (which by default is off)
				commitOn = kDefaultCompleteWithAnyChars;
			}
			else if (!IsMembersList() || !IsCFile(lang))
				commitOn = kDefaultCompleteWithAnyChars;
		}
#endif

		if (IsCFile(lang))
		{
			if (IsMembersList())
			{
				if (m_startsWith.IsEmpty())
				{
					if (GlobalProject->CppUsesClr())
					{
						// Complete on any in members lists (except ~!)
						// ~! do not commit
						commitOn.ReplaceAll("~", "");
						commitOn.ReplaceAll("!", "");
					}
					else
					{
						// Complete on any in members lists (except ~)
						// ~ does not commit
						commitOn.ReplaceAll("~", "");
					}
				}
			}
			else if (IsFileCompletion())
			{
				// [case: 61997] allow > and " to commit #include completion even if !completeWithAny
				commitOn += WTString(">\"");
			}
		}

		if (Psettings->m_bCompleteWithReturn)
			commitOn += WTString("\r");
		if (Psettings->m_bCompleteWithTab)
			commitOn += WTString("\t");

		return commitOn;
	}

	if (Is_Tag_Based(lang))
	{
		// what keys to expand in "<tag..." case=42354, case=25203
		// Psettings->m_bCompleteWithAny not applicable to tag-based languages
		WTString commitOn("= ");
		if (Psettings->m_bCompleteWithReturn)
			commitOn += WTString("\r");
		if (Psettings->m_bCompleteWithTab)
			commitOn += WTString("\t");

		ScopeInfoPtr si = m_ed->ScopeInfoPtr();
		if (si->m_stringText.GetLength() > 0)
		{
			// in quoted arg, only expand on equals, space, enter or tab.
			return commitOn;
		}

		// Outside of ="", also expands on dot and colon for xaml completions.
		commitOn += ":.";
		return commitOn;
	}

	if (lang == JS)
	{
		// Psettings->m_bCompleteWithTab && Psettings->m_bCompleteWithReturn ignored if this IDE js option is set
		if (g_IdeSettings->GetEditorBoolOption("JScript Specific", "JScriptOnlyUseTabEnterForCompletion") ||
		    g_IdeSettings->GetEditorBoolOption("JavaScript Specific", "OnlyUseTaborEnterToCommit"))
			return WTString("\t\r");
	}

	bool completeWithAny = Psettings->m_bCompleteWithAny;
	if (VB == lang)
	{
		// [case: 58703] in VB, we ignore CompleteWithAny to emulate IDE behavior
		completeWithAny = true;

		if (Psettings->m_bCompleteWithAnyVsOverride)
		{
			// [case: 67723] optionally override VS emulation
			completeWithAny = false;
		}
	}

	WTString commitOn;
	if (completeWithAny)
		commitOn += kDefaultCompleteWithAnyChars;
	if (Psettings->m_bCompleteWithReturn)
		commitOn += WTString("\r");
	if (Psettings->m_bCompleteWithTab)
		commitOn += WTString("\t");
	if (VB == lang)
	{
		// Minus the ? for VB snippets?<tab> case=24366
		commitOn.ReplaceAll("?", "");
	}

	return commitOn;
}

BOOL VACompletionSet::ShouldItemCompleteOn(symbolInfo* sinf, long key)
{
	if (key == '\r' && (2 == Psettings->mSuggestionSelectionStyle) && Psettings->m_bCompleteWithReturn)
	{
		// Allow <enter> to expand completion Scope Suggestions. case=48521, case=71317
		return TRUE;
	}
	if (m_ManuallySelected && m_expBox->GetFirstSelectedItemPosition())
	{
		if (Psettings->m_bCompleteWithAny)
		{
			if (!IsMembersList() || !IsCFile(gTypingDevLang))
				return TRUE;
#ifndef AVR_STUDIO
			if (gShellAttr->IsDevenv11OrHigher() && IsCFile(gTypingDevLang))
				return TRUE; // dev11 only uses m_bCompleteWithAny, ignores mMembersCompleteOnAny
#endif
		}

		if (Psettings->mMembersCompleteOnAny && IsMembersList() && IsCFile(gTypingDevLang))
		{
#ifndef AVR_STUDIO
			_ASSERTE(!gShellAttr->IsDevenv11OrHigher());
#endif
			return TRUE;
		}
	}

	if (sinf && key != '\r' && key != '\t')
	{
		// Filter out special cases where "Complete on any" could cause problems.
		if (sinf->m_type == ET_SUGGEST_BITS)
			return FALSE; // Do not "Complete on any"...
		if (sinf->m_type == ET_AUTOTEXT)
		{
			const WTString expTxt =
			    gAutotextMgr->GetSource(TokenGetField(sinf->mSymStr, WTString(AUTOTEXT_SHORTCUT_SEPARATOR)));
			ScopeInfoPtr si = m_ed->ScopeInfoPtr();
			if (expTxt.FindOneOf("{}[],:;+-*/%&|^!~=<>?@'\"\\.() \t\r") != -1 // contains non csym chars
			    || si->GetCwData())                                           // current sym is defined
				return FALSE;
			if (expTxt.ReverseFind("#") > 0) // [case: 62003] allow any char to commit "#define"
				return FALSE;
		}
		if (sinf->IsFileType())
		{
			if (strchr(">\"", key) && IsCFile(m_ed->m_ScopeLangType) /*&& Psettings->m_bCompleteWithAny*/)
				return TRUE; // [case: 61997] // allow > and " to commit #include completion regardless of
				             // m_bCompleteWithAny
			if (!strchr("/\\#", key))
				return FALSE; // independent of completeWithANy for all languages?
			if (strchr("/\\", key) && (sinf->m_type & ~VA_TB_CMD_FLG) == ICONIDX_FILE_FOLDER &&
			    m_expBox->GetFocusedItem() && Psettings->m_bCompleteWithAny)
				return TRUE; // fix for inconsistent completion on \ and / for directories
		}
		if (m_isDef)
			return FALSE;
		if (m_ed && ISCSYM(m_ed->m_cwd[0]) && m_ed->GetSymScope().GetLength())
			return FALSE; // cwd is already valid
		if (IsCFile(m_ed->m_ScopeLangType) && !IsMembersList() && !Psettings->m_bCompleteWithAny)
			return FALSE;
		ScopeInfoPtr si = m_ed->ScopeInfoPtr();
		const bool useIDESettings = GetIVsCompletionSet() || (si->HasScopeSuggestions() && !IsCFile(gTypingDevLang));
		if (!useIDESettings && !Psettings->m_bCompleteWithAny && !IsMembersList())
			return FALSE; // Use VA expansion rules whenever VS doesn't offer a suggestion(VBS/VB2005/...) case=24501
		if (!m_expBox->GetFirstSelectedItemPosition() || !m_expBox->GetFocusedItem())
			return FALSE;
	}
	// See if key should commit item.
	return !key || GetCompleteOnCharSet(sinf, m_ed->m_ScopeLangType).Find((char)key) != -1;
}

BOOL VACompletionSet::ProcessEvent(EdCntPtr ed, long key, LPARAM flag /* = WM_KEYDOWN */, WPARAM wparam, LPARAM lparam)
{
	vCatLog("Editor.Events", "VACS::PE  0x%lx, 0x%zx", key, (uintptr_t)flag);
	if (!m_expBox)
	{
		if (Psettings->mAllowSuggestAfterTab && ed && VK_TAB == key)
		{
			// [case: 140537]
			// VK_RETURN doesn't work well due to issues with uncommitted virtual space
			ed->SetTimer(ID_TIMER_GETHINT, 10, NULL);
		}
		return FALSE;
	}

	BOOL didExp = FALSE;

	if (!ed || ed != m_ed)
	{
		// Just in case, caught an exception where we tried sending an event to a closed edcnt.
		vCatLog("Editor.Events", "VACS::PE ed");
		BOOL retval = FALSE;
		if (!ed && IsExpUp(NULL)) // VC6 press of escape - make sure to return TRUE to prevent closing of output window
			retval = TRUE;

		// [case: 25332] call UpdateCurrentPos after IsExpUp for vc6
		UpdateCurrentPos(ed);
		Dismiss();
		return retval;
	}

	BOOL expIsUp = IsExpUp(ed);
	if ('\r' == key && !expIsUp)
	{
		// [case: 25111]
		SetInitialExtent(-1, -1);
	}
	UpdateCurrentPos(ed);

	// [case 148906]
	// IntelliCode C# suggestions not accepted on Tab Tab with VA suggestions enabled in C#
	if (flag == WM_KEYDOWN && key == VK_TAB && gShellAttr && gShellAttr->IsDevenv17OrHigher() && ed)
	{
		try
		{
			// this is not perfect... we simply accept the IntelliCode only when VA listbox has no focus
			// it is because we don't currently notify IntelliSense about our selection and I have no idea how to do so 
			if (m_expBox && !m_expBox->GetFocusedItem() && m_ed->IsIntelliCodeVisible())
			{
				return FALSE;
			}
		}
		catch (...)
		{
			// pass through in case of exception
		}
	}

	if (flag == WM_CHAR && key == ' ' && (GetKeyState(VK_CONTROL) & 0x1000) && expIsUp)
		return true;

	if (flag == WM_MOUSEWHEEL && expIsUp)
	{
		m_expBox->SendMessage(WM_MOUSEWHEEL, wparam, lparam);
		return true;
	}

	bool movingOnToNextSymol = false;
	if (WM_KEYDOWN == flag && (key == '\r' || key == '\t'))
		movingOnToNextSymol = true;
	else if (WM_CHAR == flag && !ISCSYM(key))
	{
		if (('~' == key || ('!' == key && GlobalProject->CppUsesClr())) && IsMembersList() && IsCFile(gTypingDevLang) &&
		    m_startsWith.IsEmpty())
			movingOnToNextSymol = false; // [case: 56962] typing C++ dtor
		else
			movingOnToNextSymol = true;
	}

	if (movingOnToNextSymol)
	{
		if (!expIsUp && GetIVsCompletionSet())
			Dismiss(); // List may be filtered down to nothing, release their set on non-csyms. case=27778

		// Moving on to next symbol, kill any pending completion timers. case=45586
		m_ed->KillTimer(DSM_VA_LISTMEMBERS);
		m_ed->KillTimer(DSM_LISTMEMBERS);

		if (IsExpUp(m_ed))
		{
			BOOL swallowKey = FALSE;
			SymbolInfoPtr sinf = GetSymbolInfo();
			if (sinf && ShouldItemCompleteOn(sinf, key))
			{
				BOOL didVSExpand;
				if (m_ed->m_ftype != Other && sinf->m_type != ET_AUTOTEXT &&
				    sinf->m_type != ET_VS_SNIPPET) // do not add parens in .css files Case: 314
					g_ExpHistory->AddExpHistory(
					    sinf->mSymStr); // Add to history so JS will display as best guess in next suggestion.
				didExp = ExpandCurrentSel((char)key, &didVSExpand);
				if (!didExp && key == '\r')
					Dismiss(); // Dismiss lb if enter did not cause and expansion. case=55163

				if (didExp)
				{
					m_ed->Scope(); // Ensure scope is up to date
					// if didVSExpand, they should have already expanded key
					if (didVSExpand && ed->m_ScopeLangType == VB && key == '\r' &&
					    (mVsCompletionSetFlagsForExpandedItem & CSF_CUSTOMCOMMIT))
						swallowKey = TRUE; // [case: 28109] VB snippet
					else if (ed->m_ScopeLangType == VB && key == '\r')
						swallowKey = FALSE; // let VS process the enter to reformat line, both ours and their
						                    // completions: case=27736, case=27337
					else if ((didVSExpand || key == '\t' || key == '\r') &&
					         key != '.') // handle .'s in onchar so we they expand members
						swallowKey = TRUE;
					else if (mDidFileCompletionRepos && ('"' == key || '>' == key))
						swallowKey = TRUE; // [case: 100874]
					// else Return false, so OnChar inserts the key. Case 20499

					if (swallowKey && didVSExpand && key == ';')
					{
						bool needFormat = false;
						if (ed->m_ScopeLangType == CS)
						{
							if (g_IdeSettings->GetEditorBoolOption("CSharp-Specific",
							                                       "Formatting_TriggerOnStatementCompletion"))
								needFormat = true;
						}
						else if (ed->m_ScopeLangType == Src)
						{
							if (gShellAttr->IsDevenv12OrHigher() &&
							    g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "AutoFormatOnSemicolon"))
								needFormat = true;
						}

						if (needFormat)
							gShellSvc->FormatSelection(); // [case: 49027]
					}
				}
			}
			else if (!IsFileCompletion() // Don't dismiss or case correct filepaths, case=19564
			         && !(Is_Tag_Based(m_ed->m_ScopeLangType) &&
			              !strchr(" >", key))) // Don't dismiss on </ in html/asp case=24135, case=25428
			{
				if (Psettings->CaseCorrect)
				{
					// Manually fix case if m_bCompleteWithAny expand did not happen above
					if (!m_ed->GetSymScope().GetLength() && m_ed->m_isValidScope)
					{
						// canCorrect fixes case=38549, where c# should not correct case if the key is not in the
						// "complete on" list.
						bool canCorrect = m_ed->m_ScopeLangType != CS ||
						                  GetCompleteOnCharSet(sinf, m_ed->m_ScopeLangType).Find((char)key) != -1;
						if (!canCorrect && !GetIVsCompletionSet())
						{
							// [case: 48865] we can do repair case for our own comp set.
							// 'complete on' is irrelevant for correct case when using our set.
							canCorrect = true;
						}

						if (m_startsWith.GetLength() > 0 && ISCSYM(m_startsWith[0]))
						{
							if (canCorrect)
								DoCompletion(m_ed, 1, true); // Correct case
							else
								FixCase(); // 'manual' case correction from their comp set
						}
					}
				}
				Dismiss();
			}
			else if (Is_Tag_Based(m_ed->m_ScopeLangType))
			{
				// Non-csym key pressed and we did not expand.
				// Dismiss list if these keys are pressed and not in quoted assignment <img src="...">
				ScopeInfoPtr si = m_ed->ScopeInfoPtr();
				if (si->m_stringText.GetLength() == 0 && strchr("=:.", key))
				{
					// [case: 56660] don't dismiss on : as in <asp:
					if (m_startsWith != "asp" || ':' != key)
					{
						// "<img src= " and "x:" needs to dismiss. [case=41607] [case=42354]
						Dismiss();
					}
				}
			}
			return swallowKey;
		}
		else if (WM_CHAR == flag)
		{
			Dismiss();
		}
	}

	// Left for reference
	// 	if(ed->m_ftype == XML )
	// 	{
	// 		BOOL possibleXMLTag = (m_p1>0)?(ed->GetBuf()[m_p1-1] == '<'):FALSE;
	// 		if(possibleXMLTag)
	//
	// 		{
	// 			// Processing of XML < tags
	// 			if(flag == WM_CHAR && strchr("!-[?", key))
	// 				return FALSE; // just filter the list in XML <!--
	// 			int item = (int)m_expBox->GetFocusedItem()-1;
	// 			BOOL bSelected = ( item>= 0 ) && m_expBox->GetItemState(item, LVIS_SELECTED) != 0;
	// 			if(flag == WM_KEYDOWN && ((key == VK_TAB && Psettings->m_bCompleteWithTab)|| (key == VK_RETURN &&
	// Psettings->m_bCompleteWithReturn)) && IsExpUp(ed))
	// 			{
	// 				if(key == VK_RETURN)
	// 				{
	// 					if(!bSelected)
	// 						return FALSE;
	// 				}
	// 				return ExpandCurrentSel(); // insert tab/cr if not expanded
	// 			}
	// 			if(flag == WM_CHAR)
	// 			{
	// 				symbolInfo* sinf = GetSymbolInfo();
	// 				if(key == ' ' && bSelected && sinf && IS_VSNET_IMG_TYPE(sinf->m_type))
	// 				{
	// 					// XML: <?[space] should expand "?>"
	// 					ExpandCurrentSel();
	// 					return FALSE; // Allow Onchar to insert the space
	// 				}
	// 			}
	// 		}
	// 	}
	// 	if(flag == WM_KEYDOWN && ((key == VK_TAB && Psettings->m_bCompleteWithTab)|| (key == VK_RETURN &&
	// Psettings->m_bCompleteWithReturn)) && IsExpUp(ed))
	// 	{
	// 		vLog("VACS::PE  exp");
	// 		ed->GetBuf(TRUE); // make sure it is up to date
	// 		WTString startsWith = ed->WordLeftOfCursor();
	//
	// 		if(!gShellAttr->IsDevenv())
	// 		{
	// 			// Case 605: Ctrl+Enter causes beep, ignore it.
	// 			extern DWORD g_IgnoreBeepsTimer;
	// 			g_IgnoreBeepsTimer = GetTickCount() + 1000;
	// 		}
	// 		vLog("VACS::PE  exp2");
	// 		if(!ExpandCurrentSel()) // Returns False if nothing is selected, passing tab to editor
	// 		{
	// 			// Dismiss, so snippet can fire. case=21347
	// 			Dismiss();
	// 			return FALSE;
	// 		}
	// 		return TRUE;
	// 	}

	expIsUp = IsExpUp(ed);
	if (expIsUp)
	{
		if (key == VK_ESCAPE)
		{
			Dismiss();
			return true;
		}

		if (flag == WM_KEYDOWN && (key == VK_DOWN || key == VK_UP))
		{
			POSITION item = m_expBox->GetFocusedItem();
			const int fitem = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
			const BOOL bSelected = (fitem != -1) && m_expBox->GetItemState(fitem, LVIS_SELECTED) != 0;
			m_ManuallySelected = true; // [case: 50538] [case: 61951]

			if (!item)
			{
				int itemToSel = m_expBox->GetTopIndex();
				if (itemToSel < 0)
					itemToSel = 0;
				m_expBox->EnsureVisible(itemToSel, false);
				m_expBox->SetItemState(itemToSel, LVIS_FOCUSED | LVIS_SELECTED, LVIS_SELECTED | LVIS_FOCUSED);
				m_expBox->SetTimer(VACompletionBox::Timer_UpdateTooltip, 200, NULL); // Update tooltip
				if (GetPopType() != ET_SUGGEST)
					return TRUE;
			}
			else if (!bSelected)
			{
				// [case: 90124]
				// select the focused item
				m_expBox->SetItemState(fitem, LVIS_FOCUSED | LVIS_SELECTED, LVIS_SELECTED | LVIS_FOCUSED);
				m_expBox->SetTimer(VACompletionBox::Timer_UpdateTooltip, 200, NULL); // Update tooltip
				if (GetPopType() != ET_SUGGEST)
				{
					// [case: 93417] don't dismiss since we just changed from focused to selected
					if (Psettings->mResizeSuggestionListOnUpDown)
						DisplayFullSizeList();
				}
				return TRUE;
			}
			else
				m_expBox->SendMessage(WM_KEYDOWN, (WPARAM)key, 1);

			if (GetPopType() == ET_SUGGEST)
			{
				if (Psettings->mResizeSuggestionListOnUpDown)
					DisplayFullSizeList();

				if (Psettings->mDismissSuggestionListOnUpDown && item == m_expBox->GetFocusedItem())
				{
					// same place, pass off to editor
					Dismiss();
					return false;
				}
			}
			return true;
		}

		if (flag == WM_KEYDOWN && (key == VK_NEXT || key == VK_PRIOR || key == VK_HOME || key == VK_END))
		{
			if (GetPopType() == ET_SUGGEST || GetPopType() == ET_SUGGEST_BITS)
			{
				// change 8626, 4669 -- no cases associated with the changes
				Dismiss();
				return FALSE;
			}

			if (VK_HOME == key)
			{
				// [case: 70491]
				// dismiss if already at top
				int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
				if (!item)
				{
					Dismiss();
					return FALSE;
				}
			}
			else if (VK_END == key)
			{
				// [case: 70491]
				// dismiss if already at bottom
				int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
				if (item == m_expBox->GetItemCount() - 1)
				{
					Dismiss();
					return FALSE;
				}
			}

			m_expBox->SendMessage(WM_KEYDOWN, (WPARAM)key, 1);
			return true;
		}
	}

	// Left for reference
	// 	if (WM_CHAR == flag && IsExpUp(ed) && '.' == key && IsFileCompletion())
	// 	{
	// 		// don't dismiss on '.' during #include completion
	// 		ed->Insert(WTString((char)key));
	// 		// redisplay for multi-dot names [case: 520]
	// 		DoCompletion(ed, ET_EXPAND_TEXT, false);
	// 		return true;
	// 	}
	// 	if(Is_Tag_Based(ed->m_ScopeLangType))
	// 	{
	// 		if(flag == WM_CHAR && strchr("!-[?", key))
	// 			return FALSE; // just filter the list in XML <!--
	// 	}
	// 	if(flag == WM_CHAR && strchr("?!@%^&*()-+={}[]|;:<>,./\\ ", key) && // Added ? for VB case=22138
	// 		(!m_ed->m_txtFile || Is_Tag_Based(m_ed->m_ftype) || JS == m_ed->m_ftype))
	// 	{
	// 		// completeWithAny handling
	// 		if(IsExpUp(ed))
	// 		{
	// 			symbolInfo* sinf = GetSymbolInfo();
	// 			if(Is_Tag_Based(m_ed->m_ScopeLangType) && key == ':')
	// 			{
	// 				// Do not expand typing "<asp:"
	// 				return FALSE;
	// 			}
	// 			ed->CurScopeWord(); // make sure symbol not valid before replacing.
	// 			if (Is_Tag_Based(m_ed->m_ftype) || JS == m_ed->m_ftype)
	// 				m_isDef = (m_ed->m_pmparse->m_isDef || !m_ed->m_isValidScope);
	// 			if(m_isDef && strchr(":<*&~\\ ", key))
	// 				m_isDef = FALSE; // "int f*" cannot be defining "f"
	//
	// 			BOOL shouldExpand = FALSE;
	//
	// 			if (strchr("\\/", key) && IsFileCompletion())
	// 			{
	// 				if (Psettings->m_bCompleteWithAny)
	// 					shouldExpand = TRUE;	// directory completion in #include list
	// 				else
	// 				{
	// 					shouldExpand = FALSE;	// case=8955
	// 					if(Is_Tag_Based(m_ed->m_ftype))
	// 					{
	// 						// Dismiss VSNet expansion so we will list files in subdir. case=21788
	// 						Dismiss();
	// 						return FALSE;
	// 					}
	// 				}
	// 			}
	// #ifdef USE_CanItemExpandOnAny
	// 			else if(ShouldItemExpandOn(sinf, key))
	// 				shouldExpand = TRUE;
	// #else
	// 			// left for comparison
	// 			else if(sinf && IS_VSNET_IMG_TYPE(sinf->m_type))
	// 				shouldExpand = TRUE;	// Let VS decide weather it should expand, case=20499
	// 			else if(!shouldExpand && m_isDef)
	// 				shouldExpand = FALSE;
	// 			else if(Psettings->m_bCompleteWithAny || m_isMembersList)
	// 			{
	// 				// sanity checks
	// 				if(ISCSYM(ed->m_cwd[0]) && ed->SymScope.GetLength())
	// 					shouldExpand = FALSE; // cwd is already valid
	// 				else if(!ISCSYM(ed->m_cwd[0]) && !strchr("-.", ed->m_cwd[0]))
	// 					shouldExpand = FALSE; // Happens with include bits checked
	// 				else if(sinf && sinf->m_type == ET_SUGGEST_BITS)
	// 					shouldExpand = FALSE; // don't CompleteWithAny on guesses
	// 				else
	// 					shouldExpand = TRUE;
	// 			}
	// #endif // USE_CanItemExpandOnAny
	//
	// 			if (shouldExpand)
	// 			{
	// 				BOOL specialCase = FALSE;
	// #ifndef USE_CanItemExpandOnAny
	// 				if(sinf && (sinf->m_type == ET_SUGGEST_BITS))
	// 					specialCase = true;
	// #endif // USE_CanItemExpandOnAny
	// 				if(sinf && (sinf->m_type == ET_AUTOTEXT)
	// 					&& ( sinf->mSymStr.Find((char)key) != -1 // so "if(" doesn't select template
	// 						|| !ISCSYM(sinf->mSymStr[0])))	 // so "/// " doesnt select ///////////... template
	// 					specialCase = true; //
	// 				if(!specialCase)
	// 				{
	// 					if(key == '.')
	// 						ExpandCurrentSel((char)key); // Don't insert '.', and return false so OnChar() handles it
	// 					else
	// 					{
	// 						BOOL didVSExpand;
	// 						didExp = ExpandCurrentSel((char)key, &didVSExpand);
	// 						// if didVSExpand, they should have already expanded key
	// 						if(didExp && !didVSExpand)
	// 						{
	// 							// Return false, so OnChar inserts the key. Case 20499
	// 							Dismiss();
	// 							m_ed->Scope(); // Ensure scope is up to date
	// 							return FALSE;  // Allow OnChar to insert key that caused key
	// 						}
	// 					}
	// 				}
	// 			}
	//
	//
	// 			if (!IsFileCompletion()) // Don't dismiss or case correct filepaths, case=19564
	// 			{
	// 				if(!didExp && !m_ed->SymScope.GetLength() && m_ed->m_isValidScope)
	// 				{
	// 					// Manually fix case if m_bCompleteWithAny expand did not happen above
	// 					if(m_startsWith.GetLength()>0 && ISCSYM(m_startsWith[0]) && Psettings->CaseCorrect)
	// 					{
	// 						DoCompletion(m_ed, 1, true);
	// 						if(!ISCSYM(key))
	// 							Dismiss(); // [case: 16028] case correct listbox should not linger
	// 					}
	// 				}
	// 				Dismiss();
	// 			}
	// 		}
	//
	// 		if(m_ed->m_ftype != Other && sinf->m_type != ET_AUTOTEXT) // do not add parens in .css files Case: 314
	// 			g_ExpHistory->AddExpHistory(m_ed->m_cwd); // Add to history so JS will display as best guess in next
	// suggestion. 		if(!ISCSYM(key) && !IsFileCompletion()) 			Dismiss(); 		m_ed->Scope(); // Ensure scope is
	// up to date
	// 	}
	// Commented out the IsExpUp below to fix a bug were typing:
	// "foo.getz<BACKSPACE>" exp disappears on z, and would not return on backspace
	//  I do not remember the reason the test was there, or if this will have problems.
	if (flag == WM_KEYUP /*&& IsExpUp(ed)*/)
	{
		if (VK_LEFT == key || VK_RIGHT == key)
		{
			// [case: 41209] dismiss on left/right
			Dismiss();
		}
		else if ((VK_HOME == key || VK_END == key || VK_UP == key || VK_DOWN == key) &&
		         (GetKeyState(VK_SHIFT) & 0x1000))
		{
			// [case: 41209] dismiss on shift extend
			Dismiss();
		}
		else if (key == VK_BACK)
		{
			const CPoint prevPos(m_caretPos);
			m_caretPos = m_ed->vGetCaretPos();
			m_ed->vClientToScreen(&m_caretPos);

			if (m_p2 < m_p1)
				Dismiss(); // backspace before completing word...
			else if (GetKeyState(VK_CONTROL) & 0x1000)
			{
				// ctrl+backspace closes listbox
				Dismiss();
			}
			else
			{
				if (IsFileCompletion() && IsCFile(gTypingDevLang))
				{
					// [case: 41564] stay open on backspace when doing file completion
					DoCompletion(m_ed, ET_EXPAND_INCLUDE, false);
				}
				else if (m_startsWith == ".")
				{
					Dismiss(); // Dismiss after backspace over '.'. case=24421
				}
				else if (m_startsWith.GetLength() > 0 && !::wt_isspace(m_startsWith[0]))
				{
					if (!expIsUp || !m_expBox->IsWindowVisible())
					{
						// [case: 81646]
						Dismiss();
					}
					DoCompletion(m_ed, 0, false);
				}
				else if (ET_EXPAND_VSNET == GetPopType() && (Is_Tag_Based(m_ed->m_ftype) || JS == m_ed->m_ftype))
				{
					static CPoint sPrevRestorePos;
					// [case: 15622] don't close IDE suggestions in JS and HTML/XML per default IDE behavior
					if (sPrevRestorePos != prevPos)
					{
						sPrevRestorePos = m_caretPos;
						DoCompletion(m_ed, 0, false); // restore whole completion set
					}
					else
						Dismiss();
				}
				else if (IsMembersList())         // Don't close when typing "foo.a<BACKSPACE>"
					DoCompletion(m_ed, 0, false); // update display full list
				else
					Dismiss();
			}
		}
	}
	else if (Psettings->mAllowSuggestAfterTab && WM_KEYDOWN == flag && VK_TAB == key && !didExp && !expIsUp)
	{
		// [case: 140537]
		// VK_RETURN doesn't work well due to issues with uncommitted virtual space
		ed->SetTimer(ID_TIMER_GETHINT, 10, NULL);
	}

	return didExp;
}

void VACompletionSet::FilterListType(LONG type)
{
	static LONG lType = 0;
	vCatLog("Editor.Events", "VaEventLB   FilterListType type=0x%lx", type);
	_ASSERTE(!Psettings->mSuppressAllListboxes);
	_ASSERTE(!Psettings->mRestrictVaListboxesToC || IsCFile(gTypingDevLang));
	m_displayUpToDate = FALSE;
	if (type & VA_TB_CMD_FLG)
	{
		type &= ~VA_TB_CMD_FLG;
		if (type == ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST)
		{
			Psettings->m_bListNonInheritedMembersFirst ^= 1;
			if (Psettings->m_bListNonInheritedMembersFirst && g_baseExpScope)
				m_ExpData.SortByScopeID(g_baseExpScope);
			m_caretPos.SetPoint(0, 0); // reset flag so it updates
			DisplayList();
			return;
		}
		if (type == ICONIDX_EXPANSION)
		{
			Dismiss();
			if (Psettings->m_bUseDefaultIntellisense) // [case=37498]
			{
				if (gShellAttr && gShellAttr->IsDevenv11OrHigher())
				{
					// [case: 8976]
					// CompleteWord will in many cases complete a word and not display a listbox.
					// ListMembers will display a listbox in many cases, but in others no listbox at all (but no
					// completion either).
					static CString sCmd("Edit.ListMembers");
					PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)sCmd, 0);
				}
				else
					SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.CompleteWord"), 0);
			}
			else
				m_ed->PostMessage(WM_COMMAND, DSM_LISTMEMBERS, 0);
			return;
		}
		if (type == ICONIDX_MODFILE)
		{

			WTString sel = GetCurrentSelString();
			int autoTextShortcut = sel.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
			if (autoTextShortcut != -1)
			{
				if (sel.contains("SuggestionsForType "))
					sel = sel.Mid(autoTextShortcut + 1);
				else
					sel = sel.Mid(0, autoTextShortcut);
			}

			gAutotextMgr->Edit(m_ed->m_ScopeLangType, sel.c_str()); // Edit scopeType in <script> blocks
			return;
		}
		return;
	}

	if (type >= 0 && type < 255)
	{
		int nval = g_imgFilter[type] ^= 1;
		if ((GetKeyState(VK_SHIFT) & 0x1000))
		{
			if (g_imgFilter[lType])
			{
				LONG p1 = min(lType, type);
				LONG p2 = max(lType, type);
				for (LONG i = p1; i <= p2; i++)
					g_imgFilter[i] = 1;
				nval = 1;
			}
		}
		else if (!(GetKeyState(VK_CONTROL) & 0x1000))
			ZeroMemory(g_imgFilter, sizeof(g_imgFilter));
		g_imgFilter[type] = nval;

		m_typeMask = 1;
		DisplayList();
	}
	else
	{
		type = 0;
		m_typeMask = 0;
		ZeroMemory(g_imgFilter, sizeof(g_imgFilter));
		DisplayList();
	}
	lType = type;
	if (IsExpUp(m_ed) && (GetPopType() != ET_SUGGEST))
	{
		if (Psettings->m_bDisplayFilteringToolbar)
			ShowFilteringToolbar();
		m_expBox->SetTimer(VACompletionBox::Timer_UpdateTooltip, 50, NULL);
	}
	return;
}

//////////////////////////////////////////////////////////////////////////
// Data related members

long VACompletionSet::GetCount()
{
	count = m_ExpData.GetCount();
	if (m_pIVsCompletionSet && !count) // not needed with VACompletionSetEx
		count = m_pIVsCompletionSet->GetCount();
	return count;
}

SymbolInfoPtr VACompletionSet::GetDisplayText(long iIndex, WTString& text, long* piGlyph)
{
	HWND dpiWnd = m_ed ? m_ed->m_hWnd : MainWndH;
	auto dpi = VsUI::DpiHelper::SetDefaultForWindow(dpiWnd, false);

	// [case=52599] 2010 c++, their list is dynamically loaded, update our list to match theirs before Display.
	if (m_hVsCompletionSetImgList)
	{
		if (gShellAttr->IsDevenv10OrHigher() &&
		    g_ExImgCount != CImageList::FromHandle((HIMAGELIST)m_hVsCompletionSetImgList)->GetImageCount())
		{
			if (UpdateImageList((HIMAGELIST)m_hVsCompletionSetImgList))
				SetImageList(mCombinedImgList);
		}
	}
	else if (g_ExImgCount != 0 || m_hCurrentImgListInUse != GetDefaultImageList())
		SetImageList(GetDefaultImageList());

	*piGlyph = 0;
	text.Empty();

	if (iIndex < 0)
		return 0;
	if (iIndex < m_xrefCount)
	{
		_ASSERTE(iIndex >= 0);
		_ASSERTE(iIndex < MAXFILTERCOUNT);
		iIndex = m_xref[iIndex];
	}

	if (iIndex >= m_ExpData.GetCount())
	{
		if (m_pIVsCompletionSet) // not needed with VACompletionSetEx
		{
			LPCWSTR wtext = NULL;
			long img;
			m_pIVsCompletionSet->GetDisplayText(iIndex, &wtext, &img);
			if (wtext)
				text = CStringW(wtext);
			*piGlyph = img;

			_ASSERTE(GetExpContainsFlags(VSNET_TYPE_BIT_FLAG));
			// Return temporary sinfo so we can expand. case=21039
			m_TmpVSNetSymInfo.m_idx = iIndex;
			m_TmpVSNetSymInfo.m_type = uint(VSNET_TYPE_BIT_FLAG | IMG_IDX_TO_TYPE(img)); // VSNET_IMAGE_TYPE case=25424
			m_TmpVSNetSymInfo.mSymStr = wtext;
			return &m_TmpVSNetSymInfo;
		}
	}

	symbolInfo* sinf = m_ExpData.FindItemByIdx(iIndex);
	if (sinf)
	{
		text = sinf->mSymStr;
		*piGlyph = GetTypeImgIdx(sinf->m_type, sinf->mAttrs, true);
		return sinf;
	}

	return nullptr;
}

HANDLE VACompletionSet::GetDefaultImageList()
{
	HWND dpiWnd = m_ed ? m_ed->m_hWnd : MainWndH;
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(dpiWnd);
	auto dpi = dpiScope->GetHelper()->GetDeviceDpiX();

	if (mVaDefaultImgList && mVaDefaultImgListDPI == dpi)
		return mVaDefaultImgList;

	if (gImgListMgr)
	{
		HINSTANCE hMod = GetModuleHandleW(GetMainDllName()); // Is this necessary???
		if (hMod)
		{
			HINSTANCE oldMod = AfxGetResourceHandle(); // Is this necessary???
			AfxSetResourceHandle(hMod);

			auto imgList = gImgListMgr->GetImgList(ImageListManager::bgList, (uint)dpi);
			if (imgList)
			{
				if (mVaDefaultImgList)
				{
					mVaDefaultImgList.DeleteImageList();
					mVaDefaultImgListDPI = 0;
				}

				mVaDefaultImgList.Create(imgList);

				if (mVaDefaultImgList)
					mVaDefaultImgListDPI = dpi;
			}

			AfxSetResourceHandle(oldMod);
		}
	}

	return mVaDefaultImgList;
}

void VACompletionSet::SetImageList(HANDLE hImages)
{
	CImageList* iLst = CImageList::FromHandle((HIMAGELIST)hImages);
	if (iLst && m_expBox->GetSafeHwnd())
		m_expBox->SendMessage(LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)iLst->m_hImageList);
	if (iLst && m_tBar->GetSafeHwnd())
		m_tBar->GetToolBarCtrl().SetImageList(iLst);

	HANDLE prevImgListInUse = m_hCurrentImgListInUse;
	m_hCurrentImgListInUse = hImages;

	if (m_hCurrentImgListInUse == GetDefaultImageList())
		g_ExImgCount = 0;
	else if (m_hCurrentImgListInUse == mCombinedImgList)
		g_ExImgCount = mVsListImgCount;
	else if (m_hCurrentImgListInUse == prevImgListInUse)
	{
		LogUnfiltered("ERROR: VACS::SIL reusing img list but source changed?");
		// leave g_ExImgCount unchanged
	}
	else
	{
		LogUnfiltered("ERROR: VACS::SIL unknown img list");
		g_ExImgCount = 0;
		_ASSERTE(!"VACompletionSet::SetImageList unknown image list -- not able to set g_ExImgCount");
	}
}

void VACompletionSet::Dismiss()
{
	SetPopType(NULL);
	m_IvsBestMatchCached = -1;
	m_IvsBestMatchFlagsCached = 0;
	m_displayUpToDate = FALSE;
	m_ManuallySelected = false;
	ClearContents();
	m_expContainsFlags = 0;
	if (m_expBox->GetSafeHwnd())
		m_expBox->KillTimer(VACompletionBox::Timer_ReDisplay);
	if (IsExpUp(NULL))
	{
		vCatLog("Editor.Events", "VaEventLB   Dismiss");
		CatLog("Editor.Events", "VACS::Dismiss");
		if (m_expBox)
		{
			// [case: 54998] local shared_ptr copy guarantees lifetime for duration of scope
			EdCntPtr thisEd = m_ed;
			if (mSpaceReserved && thisEd)
			{
				thisEd->ReserveSpace(0, 0, 0, 0);
				// [case: 54998] it would make more logical sense to clear m_ed,
				// but can't since many calls to Dismiss() use m_ed afterwards
				mSpaceReserved = FALSE;
			}
			m_expBox->ShowWindow(SW_HIDE);
			ShowFilteringToolbar(false);
			if (thisEd && IsWindow(thisEd->GetSafeHwnd()))
				thisEd->UpdateWindow();
			if (thisEd && thisEd->m_hasRenameSug)
			{
				// [case: 95272]
				// clear m_hasRenameSug state on dismiss since it blocks reparse
				thisEd->m_hasRenameSug = false;
				thisEd->m_lastEditPos = 0;
				thisEd->m_lastEditSymScope.Empty();
			}
		}
	}

	if (m_expBox->GetSafeHwnd() && gShellIsUnloading)
	{
		// [winqual] crash in box fader thread at exit after completionSet has been dismissed.
		// see if explicit close during dismiss has any effect.
		m_expBox->CloseWindow();
	}
}

void VACompletionSet::ClearContents()
{
	if (m_pIVsCompletionSet) // not needed with VACompletionSetEx
	{
		m_pIVsCompletionSet->Dismiss();
		m_pIVsCompletionSet->Release();
		m_pIVsCompletionSet = NULL;
	}

	m_ExpData.Clear();

	mSetFileInfo.clear();
	SetInitialExtent(-1, -1);
}

WTString VACompletionSet::GetDescriptionText(long iIndex, bool& shouldColorText)
{
	WTString desc = GetDescriptionTextOrg(iIndex, shouldColorText);

	SymbolInfoPtr sinf = GetSymbolInfo();
	WTString acceptWith;
	if (m_expBox->GetItemState(iIndex, LVIS_FOCUSED) == 0)
	{
		acceptWith = "[Down arrow to select]";
	}
	else if (ShouldItemCompleteOn(sinf, '\0'))
	{
		// Is "expand on any" implied?, this is kinda wordy
		// acceptWith = "Accept with: Any character not valid in symbol.";
	}
	else
	{
		const BOOL selected = m_expBox->GetItemState(iIndex, LVIS_SELECTED) != 0;
		const BOOL kTab = ShouldItemCompleteOn(sinf, '\t');
		const BOOL kEnter = selected && ShouldItemCompleteOn(sinf, '\r'); // Enter only works if selected. case=55163
		if (kTab && kEnter)
			acceptWith = "Accept with: <TAB> or <ENTER>";
		else if (kTab)
			acceptWith = "Accept with: <TAB>";
		else if (kEnter)
			acceptWith = "Accept with: <ENTER>";
	}

	if (sinf && sinf->m_type != ET_AUTOTEXT && sinf->m_type != ET_VS_SNIPPET) // case=31564
	{
		if (sinf->mSymStr.GetLength() > GetMaxListboxStringDisplayLen() && !desc.contains(sinf->mSymStr)) // case=24802
		{
			if (desc.GetLength())
				desc += "\r\n";
			desc += sinf->mSymStr;
		}
	}

	if (desc.GetLength() && acceptWith.GetLength())
		return desc + "\r\n" + acceptWith;
	return desc + acceptWith;
}
WTString VACompletionSet::GetDescriptionTextOrg(long iIndex, bool& shouldColorText)
{
	WTString sym;
	shouldColorText = true;
	long img;
	if (iIndex < 0)
		return sym;
	SymbolInfoPtr sinf = GetDisplayText(iIndex, sym, &img);

	if (sinf)
	{
		switch (sinf->m_type)
		{
		case ET_AUTOTEXT:
			shouldColorText = false;
			return WTString("VA Snippet ") + VA_Snippet_Edit_suffix;
		case ET_VS_SNIPPET:
			shouldColorText = false;
			{
				sym = TokenGetField(sym, WTString(AUTOTEXT_SHORTCUT_SEPARATOR));
				int pos = sinf->mSymStr.ReverseFind(AUTOTEXT_SHORTCUT_SEPARATOR);
				if (-1 != pos)
				{
					// append snippet description
					sym += "\r\n" + sinf->mSymStr.Mid(pos + 1);
				}
			}
			return sym;
		case ET_AUTOTEXT_TYPE_SUGGESTION:
			shouldColorText = false;
			return WTString("Defined smart suggestion. ") + VA_Snippet_Edit_suffix;
		case ET_SCOPE_SUGGESTION:
			shouldColorText = false;
			return WTString("Smart suggestion.");
		case ET_SUGGEST_BITS:
			shouldColorText = false;
			return WTString("Suggestion from surrounding code.");
		}

		iIndex = sinf->m_idx;
	}
	else if (iIndex >= 0 && iIndex < m_xrefCount && iIndex < MAXFILTERCOUNT)
	{
		// case 5493: tooltips were wrong after fixing sort in listbox
		iIndex = m_xref[iIndex];
	}

	if (m_pIVsCompletionSet) // not needed with VACompletionSetEx
	{
		WTComBSTR bstr;
		m_pIVsCompletionSet->GetDescriptionText(iIndex, &bstr);
		if (bstr.Length())
			return WTString(bstr);
	}
	else if (sinf && sinf->m_type == IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME) && !(sinf->m_type & VSNET_TYPE_BIT_FLAG))
	{
		shouldColorText = false;
		WTString msg = WTString("Rename all references of ") + StrGetSym(g_currentEdCnt->m_lastEditSymScope) + " to " +
		               g_currentEdCnt->CurWord();
		return msg;
	}
	else if (sinf && sinf->IsFileType())
	{
		FileInfoIter it = mSetFileInfo.find(sinf->m_hash);
		if (it != mSetFileInfo.end())
		{
			shouldColorText = false;
			return WTString(it->second.mFullpath);
		}
	}

	WTString wsym = sym;
	WTString scope, bcl;
	MultiParsePtr mp(m_ed->GetParseDb());
	mp->CwScopeInfo(scope, bcl, m_ed->m_ScopeLangType);
	DType* data = mp->FindSym(&wsym, &scope, &bcl);
	if (data && !data->IsHideFromUser())
	{
		token t = data->Def();
		WTString def = t.read("\f");
		WTString cmnt = GetCommentForSym(data->SymScope());
		if (cmnt.GetLength())
			cmnt = WTString("\n") + cmnt;
		cmnt = DecodeScope(def) + WTString(cmnt);
#if defined(_DEBUG) && defined(SEAN)
		const WTString location(GetSymbolLocation(*data));
		if (!location.IsEmpty())
			cmnt += "\nFile: " + location;
#endif                   // _DEBUG && SEAN
		if (cmnt == " ") // reserved words have just a " " as a description
			return WTString();
		return cmnt;
	}

	return WTString();
}

BOOL VACompletionSet::ShouldSuggest()
{
	return (TRUE || GetPopType() == ET_SUGGEST || !IsExpUp(NULL));
}

void VACompletionSet::ShowCompletion(EdCntPtr ed)
{
	ScopeInfoPtr si = ed->ScopeInfoPtr();
	if (HasVsNetPopup(TRUE))
		return;

	if (gShellAttr->IsDevenv11OrHigher())
	{
		if (gTypingDevLang == JS && ed && XAML != ed->m_ftype)
		{
#ifdef SUPPORT_DEV11_JS
			// [case: 61584] no xref listboxes in js dev11+
			if (si->m_xref)
				return;
#else
			return; // [case 61584] [case: 66410]
#endif
		}
	}

	LogElapsedTime let("VACS::SC", 100);
	m_ed = ed;
	if (si->m_suggestionType & SUGGEST_MEMBERS && !(m_expContainsFlags & SUGGEST_MEMBERS))
	{
		m_expContainsFlags |= SUGGEST_MEMBERS;
		SetPopType(ET_EXPAND_MEMBERS);
		// 	if (m_ed->m_pmparse->m_xref && bcl.GetLength())
		{
			WTString sym, scope, bcl;
			MultiParsePtr mp(ed->GetParseDb());
			mp->CwScopeInfo(scope, bcl, ed->m_ScopeLangType);
			m_ed->GetSysDicEd()->GetMembersList(scope, bcl, &m_ExpData);
			g_pGlobDic->GetMembersList(scope, bcl, &m_ExpData);
			// Include misc/*.js members in list case=21915
			GetDFileMP(m_ed->m_ScopeLangType)->LDictionary()->GetMembersList(sym, scope, bcl, &m_ExpData, FALSE);
		}
	}

	if ((si->m_suggestionType & SUGGEST_FILE_PATH) && !(m_expContainsFlags & SUGGEST_FILE_PATH))
	{
		if (!Psettings->mIncludeDirectiveCompletionLists)
		{
			if (IsExpUp(nullptr))
				Dismiss();
			return;
		}

		UpdateCurrentPos(ed);
		PopulateWithFileList(false, ed, m_p2 - 1);
	}

	DisplayList();
}

void VACompletionSet::ShowGueses(EdCntPtr ed, LPCSTR items, INT flag)
{
	vCatLog("Editor.Events", "VACS::SG f(%x) i(%s)", flag, items ? items : "");
	LogElapsedTime let("VACS::SG", 100);
	if (HasVsNetPopup(TRUE))
		return;

	if (!g_currentEdCnt)
	{
		Dismiss();
		return;
	}

	if (gShellAttr->IsDevenv11OrHigher())
	{
#ifndef SUPPORT_DEV11_JS
		if (gTypingDevLang == JS)
			return; // [case 61584] Dev11:JavaScript uses new internal type JavascriptCompletionSet, incompatible with
			        // VA.
#endif
	}

	if (!IsExpUp(ed) && GetCount() && !IsVACompletionSetEx())
		Dismiss(); // Clear old suggestions, case=31567

	UpdateCurrentPos(ed);
	if (!ed || ed->HasSelection())
		return; // [case: 55943] no guessing if there's a selection

	if (IsFileCompletion() && !Is_Tag_Based(ed->m_ftype))
		return;

	if (flag == ICONIDX_REFACTOR_INSERT_USING_STATEMENT)
		flag = ET_SUGGEST_BITS;

	WTString startsWith = StartsWith();
	if (GetPopType() != ET_EXPAND_VSNET)
	{
		SetPopType(ET_SUGGEST);
		SetImageList(GetDefaultImageList());
	}
	else
		SetImageList(mCombinedImgList.GetSafeHandle());

	token2 t = items; // FixMBUmlauts(items);

	if (ed)
	{
		MultiParsePtr mp(ed->GetParseDb());
		m_isDef = (mp->m_isDef || !ed->m_isValidScope);
		if (CheckIsMembersList(ed))
		{
			WTString pwd = ed->CurWord(-1);
			if (pwd == ".")
			{
				WTString pwd2 = ed->CurWord(-2);
				if (pwd2.GetLength() && wt_isdigit(pwd2[0]))
				{
					IsMembersList(FALSE); // 123.f
					return;
				}
			}
		}
	}

	if (g_currentEdCnt && flag != ET_SUGGEST_BITS)
	{
		if (!IsMembersList())
		{
			if (gVsSnippetMgr)
				gVsSnippetMgr->AddToExpansionData(m_startsWith, &m_ExpData);
			gAutotextMgr->AddAutoTextToExpansionData(startsWith, &m_ExpData);
		}
	}

	if (m_isMembersList && g_currentEdCnt && Is_HTML_JS_VBS_File(g_currentEdCnt->m_ScopeLangType))
		PopulateAutoCompleteData(ET_SUGGEST, false); // Allow us to fill in missing guessed members

	if (!m_isMembersList)
		m_ExpData.AddScopeSuggestions(m_ed); // if any

	// take the top N items before sorting since they are in order of likeliness
	WTString s;
	for (DWORD n = 0; t.more(); n++)
	{
		token2 ln = t.read(CompletionStr_SEPLine);
		ln.read(CompletionStr_SEPType, s);
		uint type = (uint)atoi(ln.read().c_str());
		uint attrs = 0;
		if (UINT_MAX != type && (type & ET_MASK) != ET_MASK_RESULT && (type & VA_TB_CMD_FLG) != VA_TB_CMD_FLG &&
		    !IS_IMG_TYPE(type))
		{
			_ASSERTE((type & TYPEMASK) == type);
		}
		const BOOL codeTemplate = (type == ET_AUTOTEXT || type == ET_VS_SNIPPET);
		if (type != ET_AUTOTEXT && type != ET_VS_SNIPPET && (!type || codeTemplate))
		{
			// that convoluted condition simply means type == 0 ?
			_ASSERTE(!type);

			WTString scope, bcl;
			MultiParsePtr mp(ed->GetParseDb());
			mp->CwScopeInfo(scope, bcl, ed->m_ScopeLangType);

			DType* data = mp->FindSym(&s, scope.GetLength() ? &scope : NULL, &bcl);
			if (data && data->Def().contains("#define"))
			{
				type = DEFINE;
				attrs = 0;
			}
			else
			{
				type = data ? data->MaskedType() : 0;
				attrs = data ? data->Attributes() : 0;
			}
		}
		if (type == ET_SUGGEST_BITS && ISCSYM(s[0]) != ISCSYM(m_startsWith[0]))
		{
			// If we offer suggest bits at the end of a symbol,
			// accepting the suggestion should append the suggestion and not replace it
			if (g_currentEdCnt && Is_Tag_Based(g_currentEdCnt->m_ScopeLangType))
				SetInitialExtent(m_p2, m_p2); // Reset startswith to remove what we have typed so far
			else
				s = m_startsWith + s;
		}
		if (!type || IsMembersList())
		{
			type = ET_SUGGEST_BITS; // Guessing members of foo.members mark with ET_SUGGEST_BITS to get "?" icon
			attrs = 0;
		}
		if (s.GetLength())
		{
			if (flag == ET_SUGGEST)
				AddString(TRUE, s, type, attrs, WTHashKey(s), 1);
			else
				AddString(FALSE, s, type, attrs, WTHashKey(s), 1);
		}
	}

	if (Psettings->m_bListNonInheritedMembersFirst && g_baseExpScope)
		m_ExpData.SortByScopeID(g_baseExpScope);

	if (IsExpUp(NULL))
		DisplayList();
	else
		DoCompletion(ed, -1, false);
}

void VACompletionSet::DoCompletion(EdCntPtr ed, int popType, bool fixCase)
{
	try
	{
		vCatLog("Editor.Events", "VACS::DC %x %d", popType, fixCase);
		LogElapsedTime let("VACS::DC", 200);
		if (!ed || (!ed->GetSysDicEd()->m_loaded))
			return; // DB is still loading

		ScopeInfoPtr si = ed->ScopeInfoPtr();
		if (!Psettings->mIncludeDirectiveCompletionLists &&
		    (popType == ET_EXPAND_INCLUDE &&
		     (popType == ET_EXPAND_MEMBERS && si->m_suggestionType & SUGGEST_FILE_PATH)))
		{
			// [case: 105000]
			Dismiss();
			return;
		}

		UpdateCurrentPos(ed);
		if (m_popType == ET_EXPAND_VSNET && popType && IsExpUp(ed))
			return;

		m_ed = ed;
		if (::ShouldSuppressVaListbox(m_ed))
		{
			// dismiss before building a potentially large comp set in PopulateAutoCompleteData
			Dismiss();
			return;
		}

		if (popType > 0)
		{
			Dismiss();
			UpdateCurrentPos(ed);
			PopulateAutoCompleteData(popType, fixCase);
		}

		if (!GetCount())
		{
			Dismiss(); // Clear m_popType set in PopulateAutoCompleteData above. Case 8689:
			return;
		}

		CalculatePlacement();
		if (fixCase)
		{
			int itemIdx = -1;
			const int itemCnt = GetCount();
			if (1 == itemCnt)
				itemIdx = 0;
			else if (itemCnt)
				itemIdx = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;

			if (0 <= itemIdx)
			{
				symbolInfo* sinf = m_ExpData.FindItemByIdx(itemIdx);
				if (sinf)
				{
					// SetSel is causing the listbox to be cleared if suggestions
					// are disabled; save string before calling SetSel
					const WTString symStr(sinf->mSymStr);
					if (!symStr.CompareNoCase(m_startsWith) && symStr != m_startsWith)
					{
						bool doRepair = true;
						if (::StrIsLowerCase(symStr))
						{
							if (::StrIsMixedCase(m_startsWith) || ::StrIsUpperCase(m_startsWith))
							{
								// [case: 723] [case: 61416] [case: 62362]
								if (Psettings->mOverrideRepairCase)
								{
									vLog("WARN: skipped questionable repair %s -> %s", m_startsWith.c_str(),
									     symStr.c_str());
									doRepair = false;
								}
								else
								{
									vLog("WARN: questionable repair %s -> %s", m_startsWith.c_str(), symStr.c_str());
								}
							}
						}

						if (doRepair)
						{
							SelectStartsWith();
							// [case: 29869] InsertText causes p1 and p2 to change in msdev
							const long p1 = m_p1;
							const long p2 = m_p2;
							gAutotextMgr->InsertText(m_ed, symStr, false);
							// if suggestions are disabled, we don't see the selection go away - force it (case=5906):
							m_ed->SetSel(min(p1, p2) + symStr.GetLength(), min(p1, p2) + symStr.GetLength());
							Dismiss();
						}
					}
				}
			}

			CatLog("Editor.Events", "VACS::DQD4");
			// TODO: should this be here?
			Dismiss();
			return;
		}

		if (GetPopType() != ET_EXPAND_VSNET)
			SetImageList(GetDefaultImageList());
		else
			SetImageList(mCombinedImgList.GetSafeHandle());

		if (popType > 0)
		{
			m_typeMask = 0;
			ZeroMemory(g_imgFilter, sizeof(g_imgFilter));
		}
		DisplayList();

		if (m_expBox && m_expBox->GetItemCount() == 1 && (popType == ET_EXPAND_VSNET) && !m_ed->GetSymScope().GetLength())
		{
			// make sure text matches before expanding, so typing asdjk<ctrl+space> doesnt expand to some strange symbol
			// that happens to contain these characters
			long item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
			WTString selTxt;
			long img;
			GetDisplayText((long)item, selTxt, &img);
			if (_tcsnicmp(selTxt.c_str(), m_startsWith.c_str(), (uint)m_startsWith.GetLength()) == 0)
				ExpandCurrentSel();
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VACS:");
	}
}

void VACompletionSet::DisplayVSNetMembers(EdCntPtr ed, LPVOID data, LONG ucs_flags, UserExpandCommandState st,
                                          BOOL& expandDataRetval)
{
	vCatLog("Editor.Events", "VACS::DVSNM  f(%lx)", ucs_flags);
	LogElapsedTime let("VACS::DVNM", 100);
	_ASSERTE(!Psettings->mSuppressAllListboxes);
	_ASSERTE(!Psettings->mRestrictVaListboxesToC || IsCFile(gTypingDevLang));
	m_displayUpToDate = FALSE;
	if (ed)
		m_ed = ed;

	HWND dpiWnd = m_ed ? m_ed->m_hWnd : MainWndH;
	auto dpi = VsUI::DpiHelper::SetDefaultForWindow(dpiWnd);

	if (Defaults_to_Net_Symbols(gTypingDevLang))
	{
		// [case 39051]	Display VA suggestions in managed code in 200X (like VA in 2010)
		IVsCompletionSet* pCompSet = (IVsCompletionSet*)data;
		expandDataRetval = g_CompletionSetEx->SetIvsCompletionSet(pCompSet);
		return;
	}
	if (!data)
		return; // Happens in VB
	if (IsFileCompletion() && (IsExpUp(ed) || IsCFile(gTypingDevLang)) && gVaInteropService &&
	    gShellAttr->IsDevenv10OrHigher())
	{
		// Conflicting vs2010 file completions, dismiss theirs if ours is up.
		// Need to dismiss their completionsets if there completionset or it will break further completions. case=41609
		IVsCompletionSet* pCompSet = (IVsCompletionSet*)data;
		if (pCompSet)
			pCompSet->Dismiss();
		// This was needed for B2, but I don't think this is needed anymore.
		// C++ and ASP doesn't have a completion session so no one gets the events.
		gVaInteropService->DismissCompletionSession();
		return;
	}

	if (IsCFile(gTypingDevLang) && gShellAttr->IsDevenv11OrHigher())
	{
		// [case 58895]	In Dev11, display all completion/members lists using ex logic.
		IVsCompletionSet* pCompSet = (IVsCompletionSet*)data;
		if (pCompSet)
		{
			// Ensure IsMembersList is up to date
			//			DWORD flags = pCompSet->GetFlags();
			CheckIsMembersList(g_currentEdCnt);

			if (IsMembersList() && Psettings->m_bUseDefaultIntellisense)
			{
				g_CompletionSetEx->SetIvsCompletionSet(pCompSet);
				// [case: 61591] fix tooltips for C++ completionset items
				expandDataRetval = FALSE;
			}
			else if (uecsNone != st) // pressed ctrl+space or ctrl+j, use CompletionSetEx
			{
				g_CompletionSetEx->SetIvsCompletionSet(pCompSet);
				// [case: 61591] fix tooltips for C++ completionset items
				expandDataRetval = FALSE;
			}
			else if (GetPopType() != ET_EXPAND_VSNET)
			{
				// Dev11's suggestions are terrible (at this point).
				// Ignore theirs and just do VA's suggestions
				// [case: 81646] don't dismiss if already in ET_EXPAND_VSNET
				pCompSet->Dismiss();
			}
			return;
		}
	}

	if (JS != m_ed->m_ftype &&
	    !Is_Tag_Based(
	        m_ed->m_ftype)) // In HTML/JS, our intellisense usually comes up before theirs, don't nuke our data.
		ClearContents();

	m_expContainsFlags |= VSNET_TYPE_BIT_FLAG;

	if (ed)
		ed->OnTimer(ID_TIMER_CHECKFORSCROLL);

	g_baseExpScope = 0;
	ed->GetBuf(TRUE); // make sure it is up to date

	GetBaseExpScope();

	IVsCompletionSet* pCompSet = (IVsCompletionSet*)data;
	pCompSet->AddRef();
	if (ed->m_ScopeLangType == VB)
	{
		CComQIPtr<IVsCompletionSetEx> pCompSetEx = pCompSet;
		if (pCompSetEx)
		{
			EnableVB9Filtering(FALSE);          // Disable vb filtering. case=15202
			pCompSetEx->IncreaseFilterLevel(1); // Set "All" in VB Common/All tabs. case=4558
		}
	}

	m_pIVsCompletionSet = pCompSet;
	SetPopType(ET_EXPAND_VSNET); // .NET flag
	IsMembersList(FALSE);
	if (m_ed)
	{
		MultiParsePtr mp(m_ed->GetParseDb());
		m_isDef = (mp->m_isDef || !m_ed->m_isValidScope);
		CheckIsMembersList(g_currentEdCnt);
	}

	const int kMaxCountForSort = 3000;
	int count2 = pCompSet->GetCount();
	if (count2 > kMaxCountForSort)
	{
		vLog("  large CompletionSet %d", count2);
	}

	ScopeInfoPtr si = m_ed->ScopeInfoPtr();
	if (si->HasScopeSuggestions() && !IsMembersList())
	{
		m_ExpData.AddScopeSuggestions(m_ed);
		count2 = 0; // Display our suggestions only?
	}
	HANDLE imgLst = NULL;
	try
	{
		pCompSet->GetImageList(&imgLst);
	}
	catch (...)
	{
		// this can happen in vs2010 during file completion when IsFileCompletion is false.
		// happens when we get a completionStatus update from the IDE.
		Dismiss();
		return;
	}

	if (count2 < MAXFILTERCOUNT)
	{
		// #if defined(_DEBUG) && defined(SEAN)
		// 		PerfTimer pt("VACompletionSet::DisplayVSNetMembers", true);
		// #endif // _DEBUG && SEAN
		WTString sym;
		for (int i = 0; i < count2; i++)
		{
			LPCWSTR text = NULL;
			long img;
			pCompSet->GetDisplayText(i, &text, &img);
			if (!text)
				continue;

			sym = text;
			uint type = uint(IMG_IDX_TO_TYPE(img) | VSNET_TYPE_BIT_FLAG);
			symbolInfo* sinf = NULL;
			if (g_baseExpScope &&
			    (Psettings->m_bBoldNonInheritedMembers || Psettings->m_bListNonInheritedMembersFirst) &&
			    count2 < kMaxCountForSort)
			{
				DType* d1 = g_pGlobDic->FindExact(sym, g_baseExpScope);
				if (!d1)
					d1 = GetSysDic()->FindExact(sym, g_baseExpScope);
				if (d1)
					sinf = m_ExpData.AddStringAndSort(sym, type, 0, WTHashKey(sym), g_baseExpScope);
			}

			if (!sinf)
			{
				if (count2 > kMaxCountForSort)
				{
					// assume VS is giving us a sorted set
					sinf = m_ExpData.AddStringNoSort(sym, type, 0, WTHashKey(sym), 1, false);
				}
				else
				{
					sinf = m_ExpData.AddStringAndSort(sym, type, 0, WTHashKey(sym), 1, false);
				}
			}

			if (sinf)
				sinf->m_idx = i;
		}

		if (!IsMembersList() && !IsFileCompletion() && !Is_Tag_Based(gTypingDevLang) /*&& ed->m_ftype == CS*/)
		{
			if (gVsSnippetMgr)
				gVsSnippetMgr->AddToExpansionData(WTString(), &m_ExpData, TRUE);
			gAutotextMgr->AddAutoTextToExpansionData(WTString(), &m_ExpData, TRUE);
		}

		if (Psettings->m_bListNonInheritedMembersFirst && g_baseExpScope)
			m_ExpData.SortByScopeID(g_baseExpScope);
	}

	CPoint pt = m_ed->vGetCaretPos();
	ed->vClientToScreen(&pt);

	// VS2010B2  VS is dynamically adding images to their list.
	// So, we need to reget their image list each time;
	if (m_hVsCompletionSetImgList != imgLst || gShellAttr->IsDevenv10OrHigher())
	{
		// Create a copy of their image list with our images appended to the end
		m_hVsCompletionSetImgList = imgLst;
		UpdateImageList((HIMAGELIST)imgLst);
	}

	SetImageList(mCombinedImgList);
#ifndef USE_IDX_AS_BEST_MATCH
	if (m_pIVsCompletionSet) // not needed with VACompletionSetEx
	{
		WTString cwd = ed->CurWord();
		if (cwd.GetLength() && !ISCSYM(cwd[0]))
			cwd.Empty();
		try
		{
			WTComBSTR bstr(cwd.Wide());
			long item = 0;
			DWORD flags = 0;
			m_pIVsCompletionSet->GetBestMatch(bstr, bstr.Length(), &item, &flags);
		}
		catch (const ATL::CAtlException& e)
		{
			// [case: 72245]
			// vs2012 Update 2 CTP html completion set throws an E_INVALIDARG
			// exception that it didn't use to (in VACompletionSetEx::SetIvsCompletionSet).
			vLog("ERROR VACS::DVsnM  sw(%s) hr(%x)", cwd, e.m_hr);
		}
		// Get vsnet's initial suggestion, typing "new " in c# suggest a type variable getting assigned
		LPCWSTR wtext = NULL;
		long img;
		m_pIVsCompletionSet->GetDisplayText(item, &wtext, &img);
		if (wtext)
			g_Guesses.SetMostLikely(WTString(wtext));
	}
#endif
	try
	{
		// We need to cache the GetBestMatch because it often fails the second time we call it.
		CStringW bstr(m_startsWith.Wide());
		long bm_item = 0;
		DWORD flags = 0;
		if (m_pIVsCompletionSet->GetBestMatch(bstr, bstr.GetLength(), &bm_item, &flags) == S_OK)
		{
			m_IvsBestMatchCached = bm_item;
			m_IvsBestMatchFlagsCached = flags;
		}
	}
	catch (const ATL::CAtlException& e)
	{
		// [case: 72245]
		// vs2012 Update 2 CTP html completion set throws an E_INVALIDARG
		// exception that it didn't use to (in VACompletionSetEx::SetIvsCompletionSet).
		vLogUnfiltered("ERROR VACS::DVsnM  sw(%s) hr(%lx)", m_startsWith.c_str(), e.m_hr);
	}

	// Get p1/p2 from them
	long line, c1, c2;
	if (S_OK == m_pIVsCompletionSet->GetInitialExtent(&line, &c1, &c2))
	{
		long p1 = m_ed->GetBufIndex(TERRCTOLONG((line + 1), (c1 + 1)));
		long p2 = m_ed->GetBufIndex(TERRCTOLONG((line + 1), (c2 + 1)));
		SetInitialExtent(p1, p2);
	}
	CalculatePlacement();
	m_typeMask = 0;
	ZeroMemory(g_imgFilter, sizeof(g_imgFilter));
	DisplayList();

	if ((ucs_flags & UCS_COMPLETEWORD) && m_expBox->GetItemCount() == 1 && (GetPopType() == ET_EXPAND_VSNET))
	{
		// this is the flag in .css files, that causes VA to insert when hitting a ':'
		// Case 314:
		if (ucs_flags != 2 || ed->m_ftype != Other)
			ExpandCurrentSel();
	}
}

BOOL VACompletionSet::ExpandInclude(EdCntPtr ed)
{
	try
	{
		UpdateCurrentPos(ed);
		mSetFileInfo.clear();
		SetPopType(ET_EXPAND_TEXT);
		const WTString edBuf = ed->GetBuf();
		const long cp = (long)ed->CurPos(TRUE);
		const long edPos = ed->GetBufIndex(edBuf, cp) - 1;

		// expand include files?
		if (edPos > 0 && edBuf[(int)edPos] == '"' && !::wt_isspace(edBuf[(int)edPos - 1]))
			return FALSE; // #include "file.h  // don't display if typing quote here

		WTString ln;
		for (int i = edPos - 1; i >= 0 && !strchr("\r\n", edBuf[i]); i--)
		{
			if (!::wt_isspace(edBuf[i]))
				ln = edBuf[i] + ln;
		}

		BOOL didFileExpansion = FALSE;
		const bool import = (_tcsnicmp(ln.c_str(), "#import", 7) == 0);
		if (import || _tcsnicmp(ln.c_str(), "#include", 8) == 0)
			didFileExpansion = PopulateWithFileList(import, ed, edPos);

		return didFileExpansion;
	}
	catch (...)
	{
		VALOGEXCEPTION("EXP:");
		ASSERT(FALSE);
	}
	return FALSE;
}

BOOL VACompletionSet::FillFromText(EdCntPtr ed)
{
	const WTString buf = ed->GetBuf();
	WTString lbuf = buf;
	long p1 = ed->GetBufIndex(buf, (long)ed->CurPos() - 1);
	BOOL ignoreCase = TRUE;

	WTString cwd;
	for (long i = p1; i > 0; i--)
	{
		char c = buf.c_str()[i];
		if (wt_isupper(c))
			ignoreCase = FALSE;
		if (ISCSYM(c))
			cwd = c + cwd;
		else
			break;
	}
	if (ignoreCase)
		lbuf.MakeLower();
	WTString str;
	if (cwd.GetLength())
		str = cwd;
	else
	{
		for (; p1 > 0; p1--)
		{
			char c = buf.c_str()[p1];
			// if(ISCSYM(c) || strchr("(\"'=./-?#", c)) // broke if(e.
			if (ISCSYM(c) || strchr("\"'=./-?#", c))
				str = WTString(c) + str;
			else
				break;
		}
	}
	if (str.GetLength() == 1 && !ISCSYM(str[0]))
		return FALSE;
	if (ignoreCase)
		str.MakeLower();
	if (!str.GetLength())
		return FALSE;
	LPCSTR px = lbuf.c_str();
	LPCSTR p = lbuf.c_str();
	while (p)
	{
		if (m_ExpData.GetCount() > 300)
			break;
		p = strstr(p, str.c_str());
		if (p)
		{
			p += str.GetLength();
			string tstr;
			LPCSTR pp = &(buf.c_str()[p - px]); // find equiv in mixed case string
			if (*p && p[-1] != '=')
				while (*pp && (ISCSYM(pp[-1]) /*|| strchr(":./-?#", pp[0])*/))
					pp--;
			char c = pp[-1];
			if (c == '"' || c == '\'')
			{
				tstr += *pp;
				pp++;
				for (char lc = '\0'; *pp && *pp != c; pp++)
				{
					tstr += *pp;
					if (*pp == '\n' || *pp == '\t')
					{
						tstr = "";
						break;
					}
					if (*pp == '\\')
					{
						p++;
						tstr += *pp;
					}
					lc = *pp;
				}
			}
			else
			{
				for (int n = 0; n < 60 && *pp && !strchr(" \t\r\n();\"'>", *pp) &&
				                isascii(*pp) /*(ISCSYM(*pp) || strchr(":./\\-?#", *pp))*/;
				     pp++, n++)
					tstr += *pp;
			}
			if (_tcsnicmp(cwd.c_str(), tstr.c_str(), (uint)cwd.GetLength()) == 0)
			{
				token t = tstr;
				string begStr = t.read(" (){}[]-<>&^%$#@!~\t,:./\\-?#=\"'<>").c_str();
				// 				int type = VAR;
				// 				if(*pp == '(')
				// 					type = FUNC;
				// 				if(pp[0] == '.' || (pp[0] == '-' && pp[1] == '>'))
				// 					type = CLASS;
				if ((p - px) < p1 || (p - px) > (p1 + cwd.GetLength() + 1))
				{ // not word under caret
					if (cwd != begStr.c_str() && begStr.length())
						AddString(TRUE, begStr.c_str(), ET_SUGGEST, 0, WTHashKey(begStr.c_str()), 1);
					if (cwd != tstr.c_str() && tstr.length())
						AddString(TRUE, tstr.c_str(), ET_SUGGEST, 0, WTHashKey(tstr.c_str()), 1);
				}
			}
		}
	}
	return TRUE;
}

WTString VACompletionSet::GetCurrentSelString()
{
	WTString selTxt;
	if (m_expBox)
	{
		int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
		if (item != -1)
		{
			long img;
			SymbolInfoPtr sinf = GetDisplayText((long)item, selTxt, &img);
			(void)sinf;
		}
	}
	return selTxt;
}

void VACompletionSet::ExpandIncludeDirectory(EdCntPtr ed, const WTString& selTxtIn, char key)
{
	WTString selTxt(selTxtIn);
	bool addDelimiter = true;
	if (Psettings->m_bCompleteWithAny)
	{
		if ('/' == key || '\\' == key)
			addDelimiter = false;
	}

	if (addDelimiter)
	{
		int lineNum = ed->CurLine();
		// get cur line text look for either '/' or '\\' and use found; default to '/'
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			WTString line;
			WTString delimiter;

			switch (attempt)
			{
			case 0:
				// use lineNum as is
				break;
			case 1:
				// try next line
				++lineNum;
				break;
			case 2:
				--lineNum; // restore original lineNum
				if (lineNum > 1)
					--lineNum; // try previous line
				else
					lineNum += 2; // try next, next
				break;
			default:
				_ASSERTE(!"unhandled attempt");
				break;
			}

			if (delimiter.IsEmpty())
			{
				line = ed->GetLine(lineNum);
				if (line.Find("include") != -1 && line.Find('/') != -1)
					delimiter = "/";
				else if (line.Find("include") != -1 && line.Find('\\') != -1)
					delimiter = "\\";
				else if (2 == attempt)
					delimiter = "/"; // default if nothing found
			}

			if (!delimiter.IsEmpty())
			{
				uint curPos = m_ed->CurPos();
				char curCh = m_ed->CharAt(curPos);
				if ('\\' != curCh && '/' != curCh)
				{
					// don't add delimiter if one was already present "typingDirHere\foo.h"
					selTxt += delimiter;
				}
				break;
			}
		}
	}

	gAutotextMgr->InsertText(ed, selTxt, false);

	uint curPos = m_ed->CurPos();
	char curCh = m_ed->CharAt(curPos);
	if ('"' == curCh || '>' == curCh || ::wt_isspace(curCh))
	{
		// since this was a directory, redisplay box for headers at the new location
		DoCompletion(ed, ET_EXPAND_TEXT, false);
	}
	else
	{
		Dismiss();
		if ('\\' == curCh || '/' == curCh)
		{
			// if delimiter is present, move caret beyond it
			m_ed->SetPos(curPos + 1);
		}
	}
}

//#define TRACE_INCLUDE_LISTBOX

bool VACompletionSet::PopulateWithFileList(bool isImport, EdCntPtr ed, int edPos)
{
	if (!Psettings->mIncludeDirectiveCompletionLists)
		return FALSE;

	if (m_expContainsFlags & SUGGEST_FILE_PATH)
		return FALSE;

	CStringW filePath, fileStr;
	const WTString edBuf(ed->GetBuf());
	const int langType = gTypingDevLang;
	int i;
	for (i = edPos; i > 0 && !strchr("\"<\\/\r\n", edBuf[i]); i--)
		fileStr = edBuf[i] + fileStr; // <pathStr/fileStr
	for (; i > 0 && !strchr("\"<", edBuf[i]); i--)
		filePath = edBuf[i] + filePath;
	if (!(i > 0 && strchr("\"<", edBuf[i])))
		return false;
	if (filePath.GetLength() && Is_Tag_Based(langType))
	{
		// VS net does not support "dir/file".
		// Dismiss theirs so we reset p1 and p2 to only include the base name. case=21788
		Dismiss(); // Clear their selection.
	}
	m_expContainsFlags |= SUGGEST_FILE_PATH;
	SetPopType(ET_EXPAND_INCLUDE);
	const int kSelectionDotPos = fileStr.ReverseFind(L'.');
#if defined(TRACE_INCLUDE_LISTBOX)
	vLog1(("VA #include file list: s(%s) p(%s) %s", m_startsWith, CString(filePath), CString(fileStr)));
#endif

	// build dir list
	bool addCurFilePath = true;
	IncludeDirs incDirs;
	CStringW incPath;
	if (Is_Tag_Based(langType))
	{
		ScopeInfoPtr si = ed->ScopeInfoPtr();
		if (!strchr(" \r\n>", si->m_stringText[0]))
			filePath = Path(CStringW(si->m_stringText.Wide())) + L'/';
		else
		{
			filePath = L'/';
			fileStr.Empty();
		}
		incPath = Path(ed->FileName());
	}
	else if (filePath.GetLength() && (filePath[0] == L'\\' || filePath[0] == L'/') || Is_Tag_Based(langType))
	{
		// absolute or UNC - don't use standard IncDirs or addlIncDirs
		const CStringW curFilepath(Path(ed->FileName()));
		if (curFilepath.GetLength() > 2 && curFilepath[1] == L':' && -1 == filePath.Find(L"\\\\") &&
		    -1 == filePath.Find(L"//"))
		{
			incPath = curFilepath.Left(2) + filePath + L";";
			filePath.Empty();
		}
		else
		{
			// no help - don't do networked (UNC) file search
		}
	}
	else
	{
		incPath = CStringW(GlobalProject->GetProjAdditionalDirs()) + incDirs.getAdditionalIncludes();
		if (isImport)
		{
			incPath += incDirs.getImportDirs();
			if (edBuf[i] == '<')
				addCurFilePath = false;
		}
		else
		{
			// [case: 41471] sys includes should be used for both quoted and angle-bracket
			// includes but to reduce noise in quoted includes (per vs2010 default behavior),
			// only add sys dirs once user has typed '\\' or '/'
			if (!(edBuf[i] == '"' && -1 == filePath.FindOneOf(L"/\\")))
			{
				const CStringW sysInc = incDirs.getSysIncludes();
				incPath = sysInc + incPath;
			}
		}

		if (addCurFilePath)
		{
			const CStringW curFilepath(Path(ed->FileName()) + L';');
			if (!::ContainsIW(incPath, curFilepath))
				incPath += curFilepath;
		}
	}

	// iterate over dirs
	const int kBufLen = 513;
	const std::unique_ptr<WCHAR[]> bufVec(new WCHAR[kBufLen + 1]);
	HWND hEd = ed->GetSafeHwnd();
	TokenW t(incPath);
	while (t.more())
	{
		WIN32_FIND_DATAW fileData;
		CStringW fDir = t.read(L";");
		WCHAR* full = &bufVec[0];
		full[0] = 0;
		if (::EdPeekMessage(hEd))
		{
			vCatLog("Editor.Events", "VaEventLB HadEvent3 0x%d", count);
#if defined(TRACE_INCLUDE_LISTBOX)
			vLog("return PeekMessage");
#endif
			return FALSE;
		}
		if (!(fDir.GetLength() && ::_wfullpath(full, fDir + L"/" + filePath, kBufLen - 1) &&
		      ::IsDir(CStringW(full) + L'.')))
		{
#if defined(TRACE_INCLUDE_LISTBOX)
			if (fDir.GetLength())
				vLog1(("  skipping: %s\n", CString(fDir)));
#endif
			continue;
		}

		fDir = full;
		s_lPath = fDir;
		const CStringW fPathBase = ::MSPath(fDir) + fileStr;
		const CStringW searchString = fPathBase + L'*';
#if defined(TRACE_INCLUDE_LISTBOX)
		vLog2(("  fDir=%s  fPath=%s\n", CString(fDir), CString(searchString)));
#endif
		HANDLE hFile = ::FindFirstFileW(searchString, &fileData);
		if (hFile == INVALID_HANDLE_VALUE)
		{
#if defined(TRACE_INCLUDE_LISTBOX)
			vLog("  FindFirstFile invalid, continue\n");
#endif
			continue;
		}

		do
		{
			if (::EdPeekMessage(hEd))
			{
				vCatLog("Editor.Events", "VaEventLB HadEvent4 0x%d", count);
				::FindClose(hFile);
#if defined(TRACE_INCLUDE_LISTBOX)
				vLog("return PeekMessage");
#endif
				return FALSE;
			}

			CStringW filenameW(fileData.cFileName);
			if (filenameW[0] != L'.')
			{
				// If filename is reported as all uppercase, then double-check casing.
				int i2;
				for (i2 = 0; i2 < filenameW.GetLength() && (filenameW[i2] == '.' || ::wt_isupper(filenameW[i2])); i2++)
					;
				if (i2 == filenameW.GetLength())
				{
					// Case-correct file path
					CStringW tmp = fPathBase + filenameW;
					WCHAR caseCorrectPath[_MAX_PATH] = L"";
					if (GetLongPathNameW(tmp, caseCorrectPath, _MAX_PATH))
					{
						WCHAR fullPath[_MAX_PATH] = L"";
						WCHAR* fileOrDirName = NULL;
						GetFullPathNameW(caseCorrectPath, _MAX_PATH, fullPath, &fileOrDirName);
						if (fileOrDirName)
						{
							filenameW = fileOrDirName;
						}
					}
				}
			}
			WTString fname = filenameW;

			int fileImgType = 0;
			bool doAdd = false;
			if (fileData.dwFileAttributes & CFile::directory)
			{
				if (filenameW[0] != L'.')
				{
					doAdd = true;
					fileImgType = IMG_IDX_TO_TYPE(ICONIDX_FILE_FOLDER);
				}
			}
			else
			{
				const int ftype = ::GetFileType(::MSPath(fDir + filenameW));

				if (Is_Tag_Based(langType))
				{
					doAdd = TRUE;
				}
				else if (isImport && ftype == Binary)
				{
					doAdd = TRUE;
				}
				else if (ftype == Header)
				{
					if (Psettings->mFilterGeneratedSourceFiles)
					{
						if (filenameW.Find(L".generated.") == -1)
						{
							// #filterGeneratedSourceLiterals
							// [case: 115342] do not display *.generated.h files in #include completion list
							doAdd = TRUE;
						}
					}
					else
					{
						doAdd = TRUE;
					}
				}
				else if (ftype == Idl)
				{
					doAdd = TRUE;
				}

				if (doAdd)
					fileImgType = ::GetFileImgIdx(filenameW);
			}

			if (doAdd)
			{
				const UINT hsh = ::WTHashKeyW(filenameW);
				if (kSelectionDotPos != -1)
				{
					// if they typed "bar." don't display "bar.h" just "h"
					// if they typed "solution.he" don't display "solution.headers" just "headers"
					// prevents duplication of "bar." and "solution." when selected from list [case: 520]
					fname = fname.Mid(kSelectionDotPos + 1);
					if (fname.IsEmpty())
						continue;
				}

				if (-1 != fname.Find('?'))
				{
					// unicode -> mbcs fail
					continue;
				}

				if (AddString(TRUE, fname, (UINT)IMG_IDX_TO_TYPE(fileImgType), V_FILENAME, hsh, 1))
				{
					const CStringW curItemPath = ::MSPath(fDir + fileData.cFileName);
					mSetFileInfo[hsh] = FileInfoItem(fname, curItemPath, fileImgType);
#if defined(TRACE_INCLUDE_LISTBOX)
					vLog3(("   + %s  %s %x\n", fname, CString(curItemPath), hsh));
#endif
				}
				else
				{
#if defined(TRACE_INCLUDE_LISTBOX)
					vLog3(("  - Failed to add %s %s %x\n", CString(fDir), fname, hsh));
#endif
				}
			}
			else
			{
#if defined(TRACE_INCLUDE_LISTBOX)
				vLog1(("   - %s %s\n", CString(fDir), fname));
#endif
			}
		} while (::FindNextFileW(hFile, &fileData));

		::FindClose(hFile);
	}

#if defined(TRACE_INCLUDE_LISTBOX)
	vLog("#include return");
#endif
	return true; // or else we will do a text expansion.
}

void VACompletionSet::PopulateAutoCompleteData(int popType, bool fixCase)
{
	SetPopType(popType);

	std::ignore = m_ed->GetBuf(true);// ensure fresh buffer is got
	m_ed->Scope(TRUE);
	MultiParsePtr mp(m_ed->GetParseDb());
	if (fixCase && mp->m_isDef)
		return;

	LogElapsedTime let("VACS::PACD", 100);
	WTString sym = StartsWith(), scope, bcl;
	mp->CwScopeInfo(scope, bcl, m_ed->m_ScopeLangType);
	vCatLog("Editor.Events", "VACS::PACD pt(0x%x) fc(%d) sym(%s) scp(%s) xref(%d)\n", popType, fixCase, sym.c_str(), scope.c_str(),
	     mp->m_xref);
	if (sym.GetLength() && wt_isdigit(sym[0]))
		return;
	if (!m_ed->m_isValidScope)
	{
		if (fixCase || popType != ET_EXPAND_COMLETE_WORD)
			return;
		if (scope != DB_SEP_CHR)
			return;
		// allow ctrl+space in global scope even if ed scope is not valid
	}

	m_scope = scope;
	m_bcl = bcl;

	{
		token t = bcl;
		WTString bc = t.read(CompletionStr_SEPLineStr "\f");
		g_baseExpScope = WTHashKey(bc);
	}

	if (scope.GetLength() && (scope == ":PP::" || scope[0] != ':'))
	{
		if (fixCase)
			return;
		if (!ExpandInclude(m_ed))
		{
			if (popType == ET_EXPAND_INCLUDE)
				return; // Don't expand text when typing on a # preproc line
			ScopeInfoPtr si = m_ed->ScopeInfoPtr();
			if (!Psettings->mIncludeDirectiveCompletionLists && si->m_suggestionType & SUGGEST_FILE_PATH)
				return;

			// don't get text unless user hits ctrl+space
			FillFromText(m_ed);
		}
	}
	else
	{
		if (mp->m_xref && bcl.GetLength())
		{
			sym.Empty();
			m_ed->GetSysDicEd()->GetMembersList(scope, bcl, &m_ExpData);
			g_pGlobDic->GetMembersList(scope, bcl, &m_ExpData);
		}
		else
		{
			if (!sym.IsEmpty() ||
			    ((popType == ET_EXPAND_MEMBERS || popType == ET_EXPAND_COMLETE_WORD) && !bcl.contains(WILD_CARD)))
			{
				if (mp->m_xref && !bcl.GetLength() && m_scope == DB_SEP_STR)
				{
					// [case: 71949] differentiate between valid "::typing" and
					// invalid "unknown::typing", "unknown->typing" and "unknown().typing"
					const WTString prevWd(m_ed->CurWord(-1));
					const WTString prevWd2(m_ed->CurWord(-2));
					if (!(prevWd == "::" && prevWd2.GetLength() && !ISALPHA(prevWd2[0])))
						return;
				}
			}

			if (sym.IsEmpty())
			{
				if (!bcl.contains(WILD_CARD) && !scope.IsEmpty() && !bcl.IsEmpty() &&
				    (popType == ET_EXPAND_MEMBERS         // ctrl+space when source of content is default intellisense
				     || popType == ET_EXPAND_COMLETE_WORD // ctrl+space when source of content is va
				     ))
				{
					// [case: 113392]
					// empty sym, nothing typed
					m_ed->GetSysDicEd()->GetMembersList(scope, bcl, &m_ExpData);
					g_pGlobDic->GetMembersList(scope, bcl, &m_ExpData);
				}
			}
			else
			{
				EdCntPtr ed(g_currentEdCnt);
				MultiParsePtr mp2 = ed ? ed->GetParseDb() : nullptr;
				if (mp2 && !mp2->m_xref && !fixCase)
				{
					if (gVsSnippetMgr)
						gVsSnippetMgr->AddToExpansionData(WTString(), &m_ExpData, TRUE);
					gAutotextMgr->AddAutoTextToExpansionData(WTString(), &m_ExpData, TRUE);
				}

				if (!bcl.contains(WILD_CARD))
				{
					m_ed->GetSysDicEd()->GetCompletionList(sym, scope, bcl, &m_ExpData, fixCase);
					g_pGlobDic->GetCompletionList(sym, scope, bcl, &m_ExpData, fixCase);
				}

				if (!m_ExpData.GetCount() && !fixCase)
				{
					bcl = NULLSTR;
					m_ed->GetSysDicEd()->GetCompletionList(sym, scope, bcl, &m_ExpData, fixCase);
					g_pGlobDic->GetCompletionList(sym, scope, bcl, &m_ExpData, fixCase);
				}
			}
		}

		m_ed->LDictionary()->GetMembersList(sym, scope, bcl, &m_ExpData, fixCase);
		GetDFileMP(m_ed->m_ScopeLangType)->LDictionary()->GetMembersList(sym, scope, bcl, &m_ExpData, fixCase);
	}

	if (Psettings->m_bListNonInheritedMembersFirst && g_baseExpScope)
		m_ExpData.SortByScopeID(g_baseExpScope);
}

void VACompletionSet::CalculatePlacement()
{
	if (!m_ed)
	{
		_ASSERTE(!"m_ed is NULL!");
		return;
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*m_ed);

	m_ed->GetFirstVisibleLine();
	m_ed->GetBuf(TRUE); // make sure it is up to date
	UpdateCurrentPos(m_ed);
	int xOffset = GetStrWidthEx(m_startsWith.c_str(), g_vaCharWidth);
	if (IsFileCompletion())
	{
		if (m_ed->HasSelection())
		{
			// special dot handling for #include file completions since dot
			// is not a members list in this context (case=8845)
			const WTString pwd = m_ed->CurWord(-1);
			if (pwd == ".")
			{
				m_startsWith.Empty();
				xOffset = GetStrWidthEx(m_startsWith.c_str(), g_vaCharWidth);
			}
		}
	}
	else if ((JS == m_ed->m_ftype || Is_Tag_Based(m_ed->m_ftype)) && !m_startsWith.IsEmpty())
	{
		const char lastCh = m_startsWith[m_startsWith.GetLength() - 1];
		if (' ' == lastCh || '<' == lastCh || '\t' == lastCh)
		{
			// fix placement of listbox when typing in asp/html tags (after typing
			// a space causes the IDE to display a listbox) -
			// without breaking completion insertion if nothing has been typed
			// - don't m_startsWith.Empty()
			xOffset = GetStrWidthEx("", g_vaCharWidth);
		}
	}

	m_pt = m_ed->vGetCaretPos();
	m_ed->m_vpos = m_pt.y; // prevent Check for scroll from dismissing list
	m_pt.x -= (xOffset + VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16));
	if (gShellAttr->IsDevenv10OrHigher())
		m_pt.x = m_ed->GetCharPos(m_p1).x - VsUI::DpiHelper::LogicalToDeviceUnitsX(28);
	m_ed->vClientToScreen(&m_pt);

	m_AboveCaret = (m_ed->m_ttParamInfo && ::IsWindowVisible(m_ed->m_ttParamInfo->m_hWnd)) || HasVsNetPopup(FALSE);
	m_pt.y += m_AboveCaret ? -VsUI::DpiHelper::LogicalToDeviceUnitsX(5) : g_FontSettings->GetCharHeight();
}

SymbolInfoPtr VACompletionSet::GetSymbolInfo()
{
	WTString selTxt;
	long img = 0;
	int item = (int)(intptr_t)m_expBox->GetFocusedItem();
	if (!item)
		return nullptr;
	return GetDisplayText((long)item - 1, selTxt, &img);
}

WTString CompletionSetEntry(LPCSTR sym, UINT type /*= -1*/)
{
	WTString entry;
	if (sym && *sym) // [case: 12061] if sym is empty, type gets displayed as a string
	{
		if (strchr(sym, CompletionStr_SEPLine))
		{
			// [case: 12061] sym was the result of several calls to CompletionSetEntry
			// don't pass the return value of CompletionSetEntry back into CompletionSetEntry
			_ASSERTE(!"CompletionSetEntry called with entry instead of symbol");
			entry = sym;
		}
		else if (type == -1)
			entry.WTFormat("%s%c", sym, CompletionStr_SEPLine);
		else
		{
			_ASSERTE((type & ET_MASK) == ET_MASK_RESULT || (type & TYPEMASK) == type ||
			         (type & VA_TB_CMD_FLG) == VA_TB_CMD_FLG || IS_IMG_TYPE(type));
			entry.WTFormat("%s%c%d%c", sym, CompletionStr_SEPType, type, CompletionStr_SEPLine);
		}
	}
	return entry;
}

void VACompletionSet::ShowFilteringToolbar(bool show, bool allow_fadein)
{
	if (show)
	{
		if (allow_fadein && !m_expBox->LC2008_DisableAll() && m_expBox->LC2008_HasToolbarFadeInEffect())
		{
			if (!m_tBarContainer->IsWindowVisible())
				m_expBox->FadeInCompanion(m_tBarContainer->m_hWnd);
		}

		m_tBarContainer->ShowWindow(SW_SHOWNOACTIVATE);
		m_expBoxContainer->ShowWindow(SW_SHOWNOACTIVATE);
	}
	else
	{
		m_tBarContainer->ShowWindow(SW_HIDE);
		m_expBoxContainer->ShowWindow(SW_HIDE);
	}
}

WTString VACompletionSet::StartsWith()
{
	if (!IsVACompletionSetEx()) // TODO: Causes strange assert
	{
		_ASSERTE(m_p2 == m_ed->GetBufIndex((long)m_ed->CurPos()));
	}
	return m_startsWith;
}

void VACompletionSet::SelectStartsWith()
{
	// m_p2 may include text to the right when completion is called from middle of sym.
	// 	_ASSERTE(m_p2 == m_ed->GetBufIndex(m_ed->CurPos()));
	m_ed->SetSel(m_p1, m_p2);
}

void VACompletionSet::SetInitialExtent(long p1, long p2)
{
	WTString startsWith = m_ed->GetSubString((uint)p1, (uint)p2);
	// [case: 25111] if buf is off, substring will be incorrect - try to catch whitespace problems
	startsWith.Trim();
	if (m_startsWith != startsWith)
	{
		vCatLog("Editor.Events", "VaEventLB sie p1(%08lx) p2(%08lx) '%s' '%s'", p1, p2, m_startsWith.c_str(), startsWith.c_str());
		mPreviousStartsWith = m_startsWith;
		m_startsWith = startsWith;
		m_displayUpToDate = FALSE;
	}
	m_p1 = p1;
	m_p2 = p2;
	if (!IsExpUp(m_ed))
		mPreviousStartsWith.Empty();
}

void VACompletionSet::UpdateCurrentPos(EdCntPtr ed)
{
	// Initialize m_ed;
	if (ed != m_ed)
		Dismiss();

	m_ed = ed;
	if (!ed)
		return; // Happens in VC6 on escape: SubClassWnd.cpp calls ProcessEvent(NULL, VK_ESCAPE)

	// Ensure m_p1/m_p2 are up to date
	long p1 = m_p1;
	long p2 = m_ed->GetBufIndex((long)m_ed->CurPos());
	if (p1 == -1)
	{
		// only calculate m_p1 if not already initialized
		vCatLog("Editor.Events", "VaEventLB ucp p2(%08lx)", p2);
		p1 = p2;
		WTString cwd = m_ed->WordLeftOfCursor();
		if (ISCSYM(cwd[0]) || (!(m_expContainsFlags & SUGGEST_FILE_PATH) &&
		                       (cwd[0] == '#' || cwd[0] == '/'))) // allow /// or #in only if not include path
		{
			if (p2 >= cwd.GetLength())
				p1 = p2 - cwd.GetLength();
		}

		if (IsCFile(gTypingDevLang))
		{
			if (p1 > 0)
			{
				const char prevCh = m_ed->GetBuf()[(uint)p1 - 1];
				if (prevCh == '!' || prevCh == '~')
				{
					// Include ~ in startswith for destructor and finalizer snippet/suggestion. case=47139
					--p1;
					MultiParsePtr mp(m_ed->GetParseDb());
					if (!mp->m_inClassImplementation)
					{
						// [case: 48557]
						// [case: 64089] fix for suggestions in: var1 = ~var2;
						const WTString prevWd(m_ed->CurWord(-2));
						if (prevWd != "::" && prevWd != "." && prevWd != "->")
							++p1;
					}
				}
			}
		}
	}
	SetInitialExtent(p1, p2);
}

void VACompletionSet::SetPopType(INT popType)
{
	if (popType && !m_popType)
	{
		UpdateCurrentPos(m_ed);
		CalculatePlacement();
	}
	if (!popType || GetPopType() != ET_EXPAND_VSNET) // don't overwrite
	{
		m_popType = popType;
		SetInitialExtent(-1, -1); // reset to update m_p2's val
	}

	if (popType)
	{
		if (!GetExpContainsFlags(VSNET_TYPE_BIT_FLAG))
			SetImageList(GetDefaultImageList());
		else
			SetImageList(mCombinedImgList.GetSafeHandle());

		UpdateCurrentPos(m_ed);
	}
}

bool VACompletionSet::IsFileCompletion() const
{
	ScopeInfoPtr si = m_ed ? m_ed->ScopeInfoPtr() : nullptr;
	return (mSetFileInfo.size() || (si && (si->m_suggestionType & SUGGEST_FILE_PATH)));
}

int VACompletionSet::GetBestMatch()
{
	int matchItem = -1;
	int autotextItemIdx = -1;
	WTString snippet;
	if (gVsSnippetMgr)
		snippet = gVsSnippetMgr->FindNextShortcutMatch(m_startsWith, autotextItemIdx);
	if (-1 == autotextItemIdx)
		snippet = gAutotextMgr->FindNextShortcutMatch(m_startsWith, autotextItemIdx);
	if (!IsMembersList() && snippet.GetLength())
		matchItem = m_ExpData.FindIdxFromSym(snippet.c_str());
	else if (GetIVsCompletionSet())
	{
		try
		{
			CStringW bstr(m_startsWith.Wide());
			long item = m_IvsBestMatchCached;
			DWORD flags = 0;
			if (GetIVsCompletionSet()->GetBestMatch(bstr, bstr.GetLength(), &item, &flags) == S_OK || item != -1)
			{
				LPCWSTR wtext = NULL;
				long img;
				GetIVsCompletionSet()->GetDisplayText(item, &wtext, &img);
				if (GetCount() >= MAXFILTERCOUNT)
					matchItem = item; // Large list of only their items, findidx fails. case=25424
				else
					matchItem = m_ExpData.FindIdxFromSym(WTString(wtext).c_str());
			}
			else // Second call to GetBestMatch sometimes fails, use value from first call. case=28032
				matchItem = m_ExpData.FindIdxFromSym(g_Guesses.GetMostLikelyGuess().c_str());
		}
		catch (const ATL::CAtlException& e)
		{
			// [case: 72245]
			// vs2012 Update 2 CTP html completion set throws an E_INVALIDARG
			// exception that it didn't use to (in VACompletionSetEx::SetIvsCompletionSet).
			vLogUnfiltered("ERROR VACS::GBM sw(%s) hr(%lx)", m_startsWith.c_str(), e.m_hr);
			matchItem = m_ExpData.FindIdxFromSym(g_Guesses.GetMostLikelyGuess().c_str());
		}
	}
	else if (!m_startsWith.IsEmpty())
	{
		const WTString startsWith = m_startsWith;
		const long count2 = GetCount();
		if (count2 >= MAXFILTERCOUNT)
		{
			// [case: 8976]
			// ctrl+space produces large list but filtering doesn't work in it.
			// find something to select
			UINT matchFlags = Psettings->m_bAllowAcronyms ? ACRONYM : 0u;
			if ((GetIVsCompletionSet() || IsMembersList()) && Psettings->m_bAllowShorthand)
				matchFlags |= SUBSET; // do not set SUBSET for suggestions or you will get crazy results

			WTString iTxt, dispText;
			long img = 0;
			for (long idx = 0; idx < count2; ++idx)
			{
				// simple version of #listboxFilter for count > MAXFILTERCOUNT
				// returns first hit, not best hit
				SymbolInfoPtr sinf = GetDisplayText(idx, iTxt, &img);
				(void)sinf;
				int autoTextShortcut = iTxt.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
				// dispText is the text that will be displayed without the autotext insertion text case=31884
				dispText = (autoTextShortcut == -1) ? iTxt : iTxt.Mid(0, autoTextShortcut);
				if (::ContainsSubset(dispText, startsWith, matchFlags))
					return idx;
			}
		}
	}
	return matchItem;
}

void VACompletionSet::DisplayFullSizeList()
{
	if (!m_ed)
	{
		_ASSERTE(!"m_ed is NULL!");
		return;
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*m_ed);

	// Resize from smallerlist to max size.
	// Logic pulled from DisplayList()
	CRect rci, rw;
	m_expBox->GetItemRect(0, rci, LVIR_BOUNDS);
	int displayedItems = min(DISPLAYLINES, m_expBox->GetItemCount());
	int boxheight =
	    (rci.Height() * displayedItems + VsUI::DpiHelper::LogicalToDeviceUnitsY(1)) +
	    VsUI::DpiHelper::LogicalToDeviceUnitsY(6); // the 1 and 6 are magical numbers are needed for Vista. case=31629

	m_expBox->GetWindowRect(&rw);
	if (boxheight > (rw.Height() + rci.Height()))
	{
		const CRect initialWndRc(rw);
		const bool hadVscroll = (m_expBox->GetStyle() & WS_VSCROLL) != 0;
		if (m_AboveCaret)
			rw.top = rw.bottom - boxheight;
		else
			rw.bottom = rw.top + boxheight;
		CRect adjustedRc(rw);
		g_FontSettings->RestrictRectToMonitor(adjustedRc, m_ed.get());
		if (adjustedRc != rw)
		{
			// case=31812 new size takes us off screen.  readjust.
			if (m_AboveCaret)
				rw.top = adjustedRc.top + (rci.Height() / 2);
			else
				rw.bottom = adjustedRc.bottom - (rci.Height() / 2);
		}

		if (::abs(initialWndRc.Height() - rw.Height()) <= rci.Height())
			return; // fix flicker due to repeated attempts to resize

		if (!m_expBox->LC2008_DisableAll() &&
		    (m_expBox->LC2008_HasHorizontalResize() || m_expBox->LC2008_HasVerticalResize()))
			m_expBox->GetSizingRect(UINT(m_AboveCaret ? WMSZ_TOP : WMSZ_BOTTOM), rw);
		m_expBox->MoveWindow(rw);

		const bool kHasVscroll = !!(m_expBox->GetStyle() & WS_VSCROLL);
		if (hadVscroll && !kHasVscroll)
		{
			// if the vertical scrollbar was present and now no longer is,
			// then increase the width of the items in the box
			m_expBox->SetColumnWidth(0, m_expBox->GetColumnWidth(0) + GetSystemMetrics(SM_CXVSCROLL));
		}

		// [case: 67637] winxp needs these ensureVisible calls even if no vScroll
		const bool kNeedEnsureVisible = kHasVscroll || ::GetWinVersion() < wvVista;
		if (kNeedEnsureVisible)
		{
			// Scroll top and/or bottom into view
			m_expBox->EnsureVisible(0, false);
			m_expBox->EnsureVisible(m_expBox->GetItemCount() - 1, false);
			// Scroll current item into view
			POSITION item = m_expBox->GetFocusedItem();
			if (item == 0)
				m_expBox->EnsureVisible(0, false); // Nothing selected yet, go to first item. case=31848
			else
				m_expBox->EnsureVisible((int)(intptr_t)item - 1, false);
		}
	}
}

bool VACompletionSet::AddString(BOOL sort, WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
                                bool needDecode /*= true*/)
{
	symbolInfo* retInfo;
	const int count2 = m_ExpData.GetCount();
	if (sort)
		retInfo = m_ExpData.AddStringAndSort(str, type, attrs, symID, scopeHash, needDecode);
	else
		retInfo = m_ExpData.AddStringNoSort(str, type, attrs, symID, scopeHash, needDecode);
	if (m_ExpData.GetCount() != count2)
		m_displayUpToDate = FALSE; // Only update display if it added it to the list.
	return retInfo != nullptr;
}

uint VACompletionSet::GetMemberScopeID(LPCTSTR sym)
{
	uint scopeID = 1;
	const int kMaxCountForSort = 3000;
	if (g_baseExpScope && (Psettings->m_bBoldNonInheritedMembers || Psettings->m_bListNonInheritedMembersFirst) &&
	    count < kMaxCountForSort)
	{
		WTString sym2(sym);
		DType* d1 = g_pGlobDic->FindExact(sym2, g_baseExpScope);
		if (!d1)
			d1 = GetSysDic()->FindExact(sym2, g_baseExpScope);
		if (d1)
			scopeID = g_baseExpScope;
	}
	return scopeID;
}

void VACompletionSet::GetBaseExpScope()
{
	g_baseExpScope = NULL;
	if (Psettings->m_bListNonInheritedMembersFirst || Psettings->m_bBoldNonInheritedMembers)
	{
		m_ed->Scope(TRUE);
		WTString scope, bcl;
		m_ed->GetParseDb()->CwScopeInfo(scope, bcl, m_ed->m_ScopeLangType);
		m_scope = scope;
		m_bcl = bcl;
		{
			token t = bcl;
			WTString bc = t.read(CompletionStr_SEPLineStr "\f");
			g_baseExpScope = WTHashKey(bc);
		}
	}
}

WTString VACompletionSet::ToString(bool selectionState /*= FALSE*/)
{
	WTString contents = "Expansion List Contents:\r\n";
	if (selectionState)
	{
		// Get info on selected item
		contents = "Expansion List Selection State:\r\n";
		int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
		if (item != -1)
		{
			// Item text.
			long img;
			WTString selTxt;
			SymbolInfoPtr sinf = GetDisplayText((long)item, selTxt, &img);
			if (sinf)
				contents += WTString("DisplayText/InsertionText") + selTxt + "/" + sinf->mSymStr + "\r\n";

			// is it selected or just focused?
			bool bSelected = m_expBox->GetItemState(item, LVIS_SELECTED) != 0;
			if (bSelected)
				contents += bSelected ? "Selected:" : "Focuse rect only:";

			// Tooltip for the item.
			bool shoulcolor = 0;
			contents += "TipText:\r\n";

			// in dev11 50417, mIVsCompletionSet->GetDescriptionText() causes this exception:
			//    First-chance exception at 0x74b6b9bc in devenv.exe: 0x8001010D: An outgoing call
			//    cannot be made since the application is dispatching an input-synchronous call.
			// Happens during ast VAAutoTest:Listbox0042c and VAAutoTest:Listbox0043c in
			// test_completion_list.cpp
			if (!GetIVsCompletionSet() || !gShellAttr->IsDevenv11OrHigher())
				contents += GetDescriptionText(item, shoulcolor);
			else
				shoulcolor = true;
			if (shoulcolor)
				contents += "\r\n(colored)";
		}
		else
			contents += "No Item Selected";
	}
	else
	{
		for (int iIndex = 0; iIndex < m_xrefCount; iIndex++)
		{
			symbolInfo* inf = g_CompletionSet->GetExpansionData()->FindItemByIdx(m_xref[iIndex]);
			if (inf)
				contents += inf->mSymStr + "\r\n";
		}
	}
	return contents;
}

BOOL VACompletionSet::HasSelection()
{
	if (IsExpUp(m_ed))
	{
		int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
		return (item != -1) ? TRUE : FALSE;
	}
	return FALSE;
}

void VACompletionSet::TearDownExpBox()
{
	if (m_tBarContainer)
		m_tBarContainer->DestroyWindow();
	delete m_tBarContainer;
	delete m_tBar;
	m_tBarContainer = NULL;
	m_tBar = NULL;

	if (m_expBoxContainer)
		m_expBoxContainer->DestroyWindow();
	delete m_expBoxContainer;
	delete m_expBox;
	m_expBoxContainer = NULL;
	m_expBox = NULL;
	m_expBoxDPI = 0;
}

void VACompletionSet::Reposition()
{
	if (!m_ed->GetSafeHwnd() || !IsExpUp(NULL) || m_ed != g_currentEdCnt || !m_ed->IsWindowVisible())
		return;

	const bool oldCaretPos(m_AboveCaret);

	// extract of CalculatePlacement();
	m_AboveCaret = (m_ed->m_ttParamInfo && ::IsWindowVisible(m_ed->m_ttParamInfo->m_hWnd)) || HasVsNetPopup(FALSE);
	if (m_AboveCaret == oldCaretPos)
		return;

	m_pt.y -= oldCaretPos ? -5 : g_FontSettings->GetCharHeight();
	m_pt.y += m_AboveCaret ? -5 : g_FontSettings->GetCharHeight();

	// extract of DisplayList();
	CRect rc;
	m_expBox->GetWindowRect(&rc);
	if (m_AboveCaret)
		rc.SetRect(m_pt.x, m_pt.y - rc.Height(), m_pt.x + rc.Width(), m_pt.y);
	else
		rc.SetRect(m_pt.x, m_pt.y + rc.Height(), m_pt.x + rc.Width(), m_pt.y);
	m_expBox->MoveWindow(&rc);

	if (!m_expBox->LC2008_DisableAll() &&
	    (m_expBox->LC2008_HasHorizontalResize() || m_expBox->LC2008_HasVerticalResize()))
		m_expBox->GetSizingRect(UINT(m_AboveCaret ? WMSZ_TOP : WMSZ_BOTTOM), rc);

	g_FontSettings->RestrictRectToMonitor(rc, m_ed.get()); // case=5178
	m_expBox->MoveWindow(&rc);

	CRect rtbOrig;
	m_tBarContainer->GetWindowRect(&rtbOrig);
	CRect rtb(0, 0, rtbOrig.Width(), rtbOrig.Height());

	m_tBar->MoveWindow(rtb);
	if (m_AboveCaret)
		rtb.OffsetRect(rc.left, rc.top - rtb.Height());
	else
		rtb.OffsetRect(rc.left, rc.bottom);
	if (m_expBox->UseVsTheme())
		rtb.OffsetRect(0, -1);
	m_tBarContainer->MoveWindow(&rtb);
	m_expBoxContainer->MoveWindow(&rtb);

	m_tBar->Invalidate(TRUE);
	m_expBox->SetTimer(VACompletionBox::Timer_DismissIfCaretMoved, 600, NULL);
	// Add a 1 second delay before displaying tip to cut down on noise
	// [case: 94723] reduce timer for vs2015 C# async tip
	const uint timerTime = gShellAttr && gShellAttr->IsDevenv14OrHigher() && gTypingDevLang == CS ? 900u : 1000u;
	m_expBox->SetTimer(VACompletionBox::Timer_UpdateTooltip, timerTime, NULL);

	ShowFilteringToolbar(Psettings->m_bDisplayFilteringToolbar && m_typeMask && (GetPopType() != ET_SUGGEST) &&
	                     (GetPopType() != ET_SUGGEST_BITS));

	if (!m_expBox->IsWindowVisible() && (GetWindowLong(m_expBox->m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
	    m_expBox->LC2008_HasCtrlFadeEffect())
	{
		SetLayeredWindowAttributes_update(m_expBox->m_hWnd, RGB(0, 0, 0), 0, LWA_ALPHA);
		m_expBox->make_visible_after_first_wmpaint = true;
		m_expBox->Invalidate();
	}

	if (gShellAttr->IsDevenv10OrHigher())
	{
		::SetWindowPos(m_expBoxContainer->m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_expBox->m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_expBoxContainer->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		::SetWindowPos(m_expBox->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
		               SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);
		// Prevent members list from opening over/under param info in 2010 [case=40685]
		CRect rw;
		m_expBox->GetWindowRect(&rw);
		if (m_AboveCaret)
			m_ed->ReserveSpace(m_pt.y - rw.Height(), m_pt.x, rw.Height(), rw.Height());
		else
			m_ed->ReserveSpace(m_pt.y, m_pt.x, rw.Height(), rw.Height());
		mSpaceReserved = TRUE;
	}

	// [case: 111562]
	const int item = (int)(intptr_t)m_expBox->GetFocusedItem() - 1;
	if (item != -1)
	{
		if (!(BOOL)ListView_IsItemVisible(m_expBox->GetSafeHwnd(), item))
			m_expBox->EnsureVisible(item, false);
	}

	if (m_ed && m_ed->m_ttParamInfo->GetSafeHwnd())
		m_ed->m_ttParamInfo->RedrawWindow(); // to fix any overlap. case=31814

	// Hide Tomato tip [case=36743]
	if (g_ScreenAttrs.m_VATomatoTip)
		g_ScreenAttrs.m_VATomatoTip->Dismiss();
	m_displayUpToDate = TRUE;
}

void VACompletionSet::RebuildExpansionBox()
{
	// setup list icons
	if (mVaDefaultImgList)
		mVaDefaultImgList.DeleteImageList();
	mRebuildExpBox = true;
	mVaDefaultImgListDPI = 0;
}

bool VACompletionSet::UpdateImageList(HIMAGELIST copyThis)
{
	const int originalImgCount = ImageList_GetImageCount(copyThis);

	if ((HIMAGELIST)mCombinedImgList && 
		copyThis && 
		(HANDLE)copyThis == m_hVsCompletionSetImgList &&
		mVsListImgCount &&
	    originalImgCount == mVsListImgCount && 
		m_hSourceOfCombinedImgList == copyThis &&
	    mCombinedImgListDPI == VsUI::DpiHelper::GetDeviceDpiX() && 
		g_IdeSettings &&
	    IsEqualGUID(mCombinedImgListTheme, g_IdeSettings->GetThemeID()))
	{
		// [case: 94682]
		// prevent unnecessary repetitive work
		// gImgListMgr->ThemeImageList is expensive in VS2015
		return true;
	}

	// Create a copy of their image list with our images appended to the end
	CImageList copiedImgLst;
	if (!copiedImgLst.Create(CImageList::FromHandle(copyThis)))
	{
		LogUnfiltered("ERROR: VACS::UIL failed to copy");
		return false;
	}

	CImageList alphaImgLst;
	CImageList* newImgLst = &copiedImgLst;

#if 0
	// Code to dump out the image list - 
	// Use //ProductSource/Tools/ImageListConverter/ImageListConverter.sln 
	// to turn into a 32bit bitmap with an alpha channel
	static bool once = true;
	if (once && originalImgCount)
	{
		once = false;
		CFile ild(TEXT("/imgListDump.il"), CFile::modeCreate | CFile::modeWrite);
		CArchive ar(&ild, CArchive::store);
		copiedImgLst.Write(&ar);
	}
#endif
	const COLORREF bgClr = gImgListMgr->GetBackgroundColor(ImageListManager::bgList);

	if (gShellAttr->IsDevenv11OrHigher() && originalImgCount)
	{
		UINT sourceImgListBitDepth = ILC_COLOR;
		IMAGEINFO ii;
		if (copiedImgLst.GetImageInfo(0, &ii))
		{
			CBitmap cbm;
			if (cbm.Attach(ii.hbmImage))
			{
				BITMAP bm;
				if (cbm.GetBitmap(&bm))
				{
					if (bm.bmBitsPixel == 32)
						sourceImgListBitDepth = ILC_COLOR32;
				}
				else
					LogUnfiltered("ERROR: VACS::UIL GetBitmap failed");
			}
			else
				LogUnfiltered("ERROR: VACS::UIL Attach failed");
		}
		else
			LogUnfiltered("ERROR: VACS::UIL GetImageInfo failed");

		if (sourceImgListBitDepth != ILC_COLOR32)
		{
			// copy from copiedImgLst into new list that supports alpha channel
			if (alphaImgLst.Create(16, 16, ILC_COLOR32, 0, 0))
			{
				HDC hDc = ::GetWindowDC(nullptr);
				if (hDc)
				{
					CDC* pDc = CDC::FromHandle(hDc);
					if (pDc)
					{
						CBitmap bmpGeneratedFromCopiedImgList;
						// this call works even though the imgLst is not an alpha list
						if (::ExtractImagesFromAlphaList(copiedImgLst, originalImgCount, pDc, bgClr,
						                                 bmpGeneratedFromCopiedImgList))
						{
							// set the alpha channel
							::MakeBitmapTransparent(bmpGeneratedFromCopiedImgList, bgClr);
							alphaImgLst.Add(&bmpGeneratedFromCopiedImgList, CLR_NONE);
							newImgLst = &alphaImgLst;
						}
						else
							LogUnfiltered("ERROR: VACS::UIL failed to extract");
					}
					else
						LogUnfiltered("ERROR: VACS::UIL DC 1 failed");
				}
				else
					LogUnfiltered("ERROR: VACS::UIL DC 2 failed");
			}
			else
				LogUnfiltered("ERROR: VACS::UIL failed to create");
		}
	}

    // Append our images
	CBitmap *vsIcons, *vaIcons;
	COLORREF vsTrans, vaTrans;
	gImgListMgr->GetRawBitmaps(&vsIcons, &vaIcons, vsTrans, vaTrans);

#ifdef _WIN64
	// case: 146151 we are expecting imagelist scaled according to DPI
	gImgListMgr->ThemeImageList(*newImgLst, bgClr);
	auto imgsScaled = gImgListMgr->ScaleImageList(*newImgLst, bgClr);
	_ASSERTE(!imgsScaled); // expecting 0 as image list should be scaled already
	std::ignore = imgsScaled;

// 	CImageList rawBmps;
// 	rawBmps.Create(16, 16, ILC_MASK | ILC_COLOR32, 0, 1);
// 	rawBmps.SetBkColor(CLR_NONE);
// 
// 	rawBmps.Add(vaIcons, vaTrans);
// 	rawBmps.Add(vsIcons, vsTrans);
// 
// 	gImgListMgr->ThemeImageList(rawBmps, bgClr);
// 	gImgListMgr->ScaleImageListEx(rawBmps, *newImgLst, bgClr);

	ImageListManager::MonikerImageFunc add_func = [newImgLst](const ImageMoniker& mon, CBitmap& bmp, bool isIconIdx, int index) {
		newImgLst->Add(&bmp, CLR_NONE);
	};

	ImageListManager::ForEachMonikerImage(add_func, bgClr, true, true);

	//gImgListMgr->FillWithMonikerImages(*newImgLst, bgClr, false, true, true);

	//rawBmps.DeleteImageList();
#else
	newImgLst->Add(vaIcons, vaTrans);
	newImgLst->Add(vsIcons, vsTrans);

	gImgListMgr->ThemeImageList(*newImgLst, bgClr);
	gImgListMgr->ScaleImageList(*newImgLst, bgClr);
#endif

	if (mCombinedImgList)
		mCombinedImgList.DeleteImageList();
	mCombinedImgList.Attach(newImgLst->Detach());
	mCombinedImgListDPI = VsUI::DpiHelper::GetDeviceDpiX();
	mCombinedImgListTheme = g_IdeSettings ? g_IdeSettings->GetThemeID() : GUID_NULL;
	m_hSourceOfCombinedImgList = copyThis;
	mVsListImgCount = originalImgCount;

#ifdef _DEBUG
	int cx, cy;
	_ASSERTE(ImageList_GetIconSize(mCombinedImgList.m_hImageList, &cx, &cy));
	_ASSERTE(VsUI::DpiHelper::LogicalToDeviceUnitsX(16) == cx && VsUI::DpiHelper::LogicalToDeviceUnitsY(16) == cy);
#endif

	return true;
}

bool VACompletionSet::CheckIsMembersList(EdCntPtr ed)
{
	if (!ed)
		return false;

	bool isMembersList;
	const WTString cwd = ed->CurWord();
	if (cwd == "." || cwd == "->" || cwd == "::" || cwd == "?.")
	{
		isMembersList = true;
	}
	else
	{
		isMembersList = false;
		const WTString pwd = ed->CurWord(-1);
		if (pwd == "." || pwd == "->" || pwd == "::" || pwd == "?.")
			isMembersList = true;
	}

	IsMembersList(isMembersList);
	return isMembersList;
}

int GetMaxListboxStringDisplayLen()
{
	// Prevent really wide suggestion boxes, truncate with "...", full text will display in tip. case=23830
	if (gTypingDevLang == XAML)
		return 60; // case=40411

	if (Is_HTML_JS_VBS_File(gTypingDevLang))
		return 40; // case=23830

	// case=40411
	return 70;
}

WTString TruncateString(const WTString& str)
{
	if (str.GetLength() < 40)
		return str;

	if (str.GetLength() > GetMaxListboxStringDisplayLen())
	{
		CStringW tmp(str.Wide());
		tmp = tmp.Left(GetMaxListboxStringDisplayLen() - 3) + L"...";
		return tmp;
	}

	return str;
}

bool ShouldSuppressVaListbox(EdCntPtr ed)
{
	if (!ed || !ed->IsInitialized())
		return true;

	if (!Psettings || !Psettings->m_enableVA || Psettings->mSuppressAllListboxes)
		return true;

	int langType = ed->m_ftype;
	if (Other == langType)
		return true;

	if (Psettings->mRestrictVaListboxesToC && !(IsCFile(langType)))
		return true;

	ScopeInfoPtr si = ed->ScopeInfoPtr();
	if (!Psettings->mIncludeDirectiveCompletionLists && si->m_suggestionType & SUGGEST_FILE_PATH)
		return true;

	if (gShellAttr->IsDevenv11OrHigher())
	{
#ifdef SUPPORT_DEV11_JS
		if (JS == ed->m_ScopeLangType)
		{
			// [case: 61584] no xref listboxes in js dev11+
			extern uint g_CurrentKeyTyped;
			if (g_CurrentKeyTyped == '.')
				return true;

			if (si->m_xref)
				return true;

			const WTString wd(ed->CurWord());
			if (wd == ".")
				return true;
		}
#else
		if (gTypingDevLang == JS && XAML != ed->m_ftype)
		{
			// [case 61584] [case: 66410]
			return true;
		}
#endif
	}

	if (VB == ed->m_ScopeLangType && gShellAttr->IsDevenv10OrHigher())
	{
		WTString prevWd(ed->CurWord(-2));
		if (prevWd == " ")
			prevWd = ed->CurWord(-1);
		if (0 == prevWd.CompareNoCase("in"))
		{
			// [case: 70663]
			// stay out of the way of the "in" clause in "for" and "for each"
			// intellisense due to failure to format and insert "Next".
			// was unable to make the format work in vs2010+
			return true;
		}
	}

	if (ed->m_IVsTextView && ed->m_IVsTextView->GetSelectionMode() == SM_BOX)
	{
		// Don't offer completion during box edits
		return true;
	}

	return false;
}
