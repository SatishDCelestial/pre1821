#include "stdafxed.h"
#include "edcnt.h"
#include "project.h"
#include "log.h"
#include "VaMessages.h"
#include "..\Addin\DSCmds.h"
#include "expansion.h"
#include "resource.h"
#include "bordercwnd.h"
#include "ArgToolTipEx.h"
#include "FontSettings.h"
#include "rbuffer.h"
#include "parsethrd.h"
#include "VATree.h"
#include "timer.h"
#include "wtcsym.h"
#include "VAFileView.h"
#include "VaTimers.h"
#include "VAParse.h"
#include "AutotextManager.h"
#include "FindReferencesResultsFrame.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "PooledThreadBase.h"
#include <memory>
#include "StatusWnd.h"
#include "wt_stdlib.h"
#include "Registry.h"
#include "WindowUtils.h"
#include "Settings.h"
#include "Oleobj.h"
#include "TempSettingOverride.h"
#include "EolTypes.h"
#include "FindReferences.h"
#include "file.h"
#include "UndoContext.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "VaService.h"
#include "DevShellService.h"
#include "Usage.h"
#include "WrapCheck.h"
#include "SyntaxColoring.h"
#include "FileId.h"
#include "TokenW.h"
#include "StringUtils.h"
#include "Guesses.h"
#include "SymbolPositions.h"
#include "DefListFilter.h"
#include "FeatureSupport.h"
#include "VASeException\VASeException.h"
#include "FileFinder.h"
#include "myspell\WTHashList.h"
#include "SubClassWnd.h"
#include "WPF_ViewManager.h"
#include "..\common\AssignOnExit.h"
#include "IdeSettings.h"
#include "VAAutomation.h"
#include "VAClassView.h"
#include "GetFileText.h"
#include "LogElapsedTime.h"
#include "ProjectInfo.h"
#include "VAWorkspaceViews.h"
#include "VABrowseMembers.h"
#include "..\common\TempAssign.h"
#include "includesDb.h"
#include "inheritanceDb.h"
#include "AutotextExpansion.h"
#include <iterator>
#include "..\common\ScopedIncrement.h"
#include "CommentSkipper.h"
#include "VAOpenFile.h"
#include "RegKeys.h"
#include "InsertPathDialog.h"
#include "DllNames.h"

#ifdef RAD_STUDIO
#include "CppBuilder.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

#if _MSC_VER > 1200
using std::ios;
using std::ofstream;
#endif

extern WTString GetLine(LPCSTR p, int i);
extern void InvalidateHintThread();

CRollingBuffer g_rbuffer;

LPCSTR
strrstr(LPCSTR sBeg, LPCSTR sEnd, LPCSTR sFind, BOOL caseMatch = TRUE)
{
	size_t sLen = strlen(sFind);
	BOOL wholeWord = ISCSYM(*sFind);
	if (caseMatch)
	{
		for (sEnd -= sLen; sEnd >= sBeg; sEnd--)
		{
			for (size_t i = 0; sEnd[i] == sFind[i]; i++)
				if (i == (sLen - 1) && (!wholeWord || !ISCSYM(sEnd[-1])))
					return sEnd;
		}
	}
	else
	{
		WTString lstr(sFind);
		lstr.MakeLower();
		sFind = lstr.c_str();
		char uc = sFind[0] & 0x80 ? sFind[0] : (char)toupper(sFind[0]);
		char lc = sFind[0] & 0x80 ? sFind[0] : (char)tolower(sFind[0]);
		//		int len = lstr.GetLength();
		UINT vflag = Psettings->m_bAllowShorthand ? 0x2 : 0u;
		for (sEnd -= sLen; sEnd > sBeg; sEnd--)
		{
			if ((sEnd[0] == uc || sEnd[0] == lc) && (!wholeWord || !ISCSYM(sEnd[-1])))
			{
				int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag = FALSE);
				if (ContainsSubset(sEnd, sFind, vflag))
					return sEnd;
			}
		}
	}

	return NULL;
}

LPCSTR
strstrn(LPCSTR sBeg, LPCSTR sFind, BOOL caseMatch = TRUE, int len = 5000)
{
	size_t sLen = strlen(sFind);
	BOOL wholeWord = ISCSYM(*sFind);
	if (caseMatch)
	{
		for (; *(++sBeg) && len > 0; len--)
		{
			sBeg++;
			for (size_t i = 0; sBeg[i] == sFind[i]; i++)
				if (i == (sLen - 1) && (!wholeWord || !ISCSYM(sBeg[-1])))
					return sBeg;
		}
	}
	else
	{
		WTString lstr(sFind);
		lstr.MakeLower();
		sFind = lstr.c_str();
		UINT vflag = Psettings->m_bAllowShorthand ? 0x2 : 0u;
		for (; *(++sBeg) && len > 0; len--)
		{
			int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag = FALSE);
			if (!ISCSYM(sBeg[-1]) && (sBeg[0] & 0x80 ? sBeg[0] : tolower(sBeg[0])) == sFind[0] &&
			    ContainsSubset(sBeg, sFind, vflag))
				return sBeg;
		}
	}

	return NULL;
}

BOOL EdCnt::CheckForSaveAsOrRecycling()
{
	// check for save as or window recycling?
	DEFTIMERNOTE(SinkMeTimer, NULL);
	Log2("CheckForSaveAsOrRecycling");
	const CStringW fname2(FileName());
	CStringW newname(fname2);
#if defined(RAD_STUDIO)
	if (gRadStudioHost)
	{
		LPCWSTR fname = gRadStudioHost->ExGetActiveViewFilename();
		newname = ::NormalizeFilepath(fname);
	}
#else
	// Get Doc's filename
	if (m_pDoc)                          // make sure we did not do a save as...
		newname = m_pDoc->GetFileName(); // should we do this here?
	else
	{
		const LPCWSTR fname = (LPCWSTR)SendVamMessage(VAM_FILEPATHNAMEW, (WPARAM)m_VSNetDoc, NULL);
		// watch out for potential error codes being returned from SendMessage
		if ((uintptr_t)fname > 0x100)
			newname = ::NormalizeFilepath(fname);
	}
#endif

	if (newname.GetLength() && fname2 != newname)
	{
		// [case: 100684]
		// DTE sometimes reports filename as fully upper-case.
		// Don't switch from our casing to what it reports.
		if (!::StrIsUpperCase(newname) || fname2.CompareNoCase(newname))
		{
			// SaveAs or window recycling changed filename...
#if !defined(RAD_STUDIO) // RadStudio does window recycling without closing backing buffer
			modified = false; // they saved, force us to saved state so we don't try to flush changes
			if (Psettings->m_autoBackup)
			{
				// file closed properly, remove temp file
				::RemoveTempFile(fname2);
			}
#endif

			vLog("  changed (%s) -> (%s)", (LPCTSTR)CString(fname2), (LPCTSTR)CString(newname));
#if defined(RAD_STUDIO)
			if (gVaRadStudioPlugin)
				gVaRadStudioPlugin->UpdateEdCntConnection(mThis, newname);
#if defined(_DEBUG)
			{
				CStringW msg;
				CString__FormatW(msg, L"VARSP CheckForSaveAsOrRecycling changed: HWND(%p) %s\n    (from %s)\n", GetSafeHwnd(), (LPCWSTR)newname, (LPCWSTR)fname2);
				::OutputDebugStringW(msg);
			}
#endif
#else
			MultiParsePtr mp(GetParseDb());
			{
				AutoLockCs l(mDataLock);
				filename = newname;
				mp->SetFilename(newname);
			}
			CheckForDeletedFile(newname);
			if (g_VATabTree && Psettings->m_keepBookmarks)
				g_VATabTree->SetFileBookmarks(FileName(), m_LnAttr.get());
			if (m_lborder)
				m_lborder->DrawBkGnd();
#endif
			return TRUE;
		}
	}

	return FALSE;
}

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
void EdCnt::RsEditorConnected(bool initialConnection)
{
	// RsEditorConnected can be called without change in focus
#if defined(RAD_STUDIO) && defined(_DEBUG)
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP RsEditorConnected: %s\n", (LPCWSTR)FileName());
		::OutputDebugStringW(msg);
	}
#endif

	// some of the logic from Init and OnSetFocus
	DEFTIMERNOTE(RsEditorConnectedTimer, NULL);
	Log2("RsEditorConnected");
	if (initialConnection)
		modified = false;
	const CStringW fname(FileName());
	vLog("  editor connected (%s)", (LPCTSTR)CString(fname));
	CheckForDeletedFile(fname);

	gTypingDevLang = m_ScopeLangType = m_ftype = ::GetFileType(fname, false, true);
	gAutotextMgr->Load(m_ScopeLangType);
	GetBuf(TRUE);

	if (initialConnection)
		CheckForInitialGotoParse(); // MIGHT call QueForReparse

	if (!m_FileIsQuedForFullReparse && !mInitialParseCompleted)
		QueForReparse();

	// once caret movement is working, some of these calls might be needed?
	SetTimer(ID_TIMER_RELOAD_LOCAL_DB, 200, NULL);
	Scope(true); // reget scope even if we are in the same pos
	CurScopeWord();
}

void EdCnt::RsEditorDisconnected()
{
	// RsEditorDisconnected can be called without change in focus
#if defined(RAD_STUDIO) && defined(_DEBUG)
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP RsEditorDisconnected: %s\n", (LPCWSTR)FileName());
		::OutputDebugStringW(msg);
	}
#endif

	KillTimer(ID_TIMER_RELOAD_LOCAL_DB);
	KillTimer(ID_DOC_MODIFIED_TIMER);
	KillTimer(ID_GETBUFFER);
	KillTimer(ID_UNDERLINE_ERRORS);
	KillTimer(ID_TIMER_GETSCOPE);
	KillTimer(ID_CURSCOPEWORD_TIMER);
	KillTimer(ID_SINKME_TIMER);
	KillTimer(WM_SETFOCUS);
	KillTimer(DSM_LISTMEMBERS);
	KillTimer(DSM_VA_LISTMEMBERS);
	KillTimer(HOVER_CLASSVIEW_OUTLINE_TIMER);
	KillTimer(ID_FILE_SAVE);
	KillTimer(ID_TIMER_RTFCOPY);
	KillTimer(ID_IME_TIMER);
	KillTimer(ID_TIMER_CHECKFORSCROLL);
	KillTimer(ID_TIMER_GETHINT);
	KillTimer(IDT_REPARSE_AFTER_PASTE);
	KillTimer(ID_ADDMETHOD_TIMER);
	KillTimer(ID_TIMER_MOUSEMOVE);
	KillTimer(HOVER_CLASSVIEW_TIMER);
	KillTimer(ID_ARGTEMPLATE_DISPLAY_TIMER);
	KillTimer(ID_ARGTEMPLATE_CHECKVS_TIMER);
	KillTimer(ID_ARGTEMPLATE_CHECKVS_TIMER2);
	KillTimer(ID_ARGTEMPLATE_TIMER);
	KillTimer(ID_ARGTEMPLATEERASE_TIMER);
	KillTimer(IDT_PREPROC_KEYUP);
	KillTimer(ID_KEYUP_TIMER);
	KillTimer(ID_TIMER_CheckMinihelp);
	KillTimer(ID_HELP_TIMER);

	// some of the logic from OnKillFocus
	m_FileIsQuedForFullReparse = FALSE;
	m_FileHasTimerForFullReparseASAP = FALSE;
	m_FileHasTimerForFullReparse = false;
	modified_ever = false;
	m_ReparseScreen = FALSE;
}
#endif

void EdCnt::CmHelp()
{
	DEFTIMERNOTE(CmHelpTimer, NULL);
	// setup helpstr for dev stud
	DisplayToolTipArgs(false);
	Help();
}

uint g_lastChar = 0;        // assigned in middle of OnChar
uint g_LastKeyTyped = 0;    // assigned on exit from OnChar
uint g_CurrentKeyTyped = 0; // assigned on entry into OnChar
#include "VACompletionSet.h"

// [case: 141003] return true if fileName has a HLSL or UE shader file extension
bool IsHlslFile(const CStringW& fileName)
{
	WTString ext = GetBaseNameExt(fileName);
	ext.MakeLower();
	if (ext.Compare(L"hlsl") && ext.Compare(L"hlsli") && ext.Compare(L"usf") && ext.Compare(L"ush"))
		return false;
	return true;
}

void EdCnt::OnChar(uint key, uint count, uint flags)
{
	vCatLog("Editor.Events", "VaEventUE OnChar '%c' key=0x%x, flags=0x%x, pos=0x%x", key, key, flags, CurPos());
	DISABLECHECK();
	BOOL AutoMatch = Psettings->IsAutomatchAllowed(false, key);
	g_CurrentKeyTyped = key;
	AssignOnExit<uint> tmpKey(g_LastKeyTyped, key);
	m_preventGetBuf = 0;

	if (!Psettings->m_incrementalSearch && gTypingAllowed)
		OnModified();

	if (!Is_C_CS_File(m_ScopeLangType) && !Is_VB_VBS_File(m_ScopeLangType) && m_ScopeLangType != JS && // case=24159
	    EdPeekMessage(m_hWnd))
	{
		CTer::OnChar(key, count, flags);
		CTer::UpdateWindow();
		return;
	}

	WTString bb(GetBufConst());
	int cpIdx = GetBufIndex(bb, (long)CurPos());
	if (cpIdx > 0)
		g_LastKeyTyped = (uint)bb[cpIdx - 1]; // Set to previous char not just last char typed. case=48863

	if (!ISCSYM((int)key) || !gTypingAllowed)
		InvalidateHintThread(); //  Don't let hintthread display old suggestions in new locations. case=26616
//#ifdef _DEBUG
// If some other window tries to set focus to an editcontrol that is below the current editor, commands will go to one
// win and commands to the other This will catch those instances.
#if !defined(RAD_STUDIO)
	if (gShellAttr->IsDevenv())
	{
		HWND topPane = ::GetParent(::GetParent(::GetParent(::GetParent(GetSafeHwnd()))));
		HWND pEd = ::GetWindow(topPane, GW_HWNDPREV);
		if (pEd && !gShellAttr->IsDevenv10OrHigher())
		{
			bool resetFocus = true;
			if (gShellAttr->IsDevenv8OrHigher())
			{
				// prevent eating of chars in vs2005 if user is using
				// multiple window tab groups
				const WTString wndCls = ::GetWindowClassString(pEd);
				if (wndCls == "EzMdiSlider")
					resetFocus = false;
				// Simple check for XAML files: case ????
				if (wndCls.contains("WindowsForms10.Window"))
					resetFocus = false;
			}
			if (resetFocus)
			{
				VALOGERROR("EDC:OC: ");
				::SetFocus(topPane);
				return; // will eat char, but will not go into wrong editor
			}
		}
	}
#endif

	if (g_inMacro || !gTypingAllowed)
	{
		CTer::OnChar(key, count, flags);
		return;
	}

	static const WTString kAutoMatchKeys("[<\'\"({");
	static const WTString kOtherSpecialKeys(":.>"); // chars that can cause display of listboxes
	if (gShellAttr->IsDevenv() && (kAutoMatchKeys.Find((char)key) != -1 || kOtherSpecialKeys.Find((char)key) != -1))
	{
		if (m_IVsTextView && m_IVsTextView->GetSelectionMode() == SM_BOX)
		{
			CTer::OnChar(key, count, flags);
			return;
		}
	}

	VAProjectAddFileMRU(FileName(), mruFileEdit);
	if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
		VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
	static uint lkey = '\0';
	const BOOL kWasExpUp = g_CompletionSet->IsExpUp(mThis);
	if (g_CompletionSet->ProcessEvent(mThis, (long)key, WM_CHAR))
		return;
	extern EdCnt* g_paintingEdCtrl;
	if (!ISCSYM((int)key))
		g_rbuffer.Add(CurWord());
	if (m_hasSel && Psettings->m_SelectionOnChar && !gExpSession && !Psettings->m_incrementalSearch &&
	    !gShellSvc->HasBlockModeSelection(this))
	{
		if (strchr(Psettings->mSurroundWithKeys, (int)key))
		{
			BOOL instr = m_lastScope.GetLength() && m_lastScope[0] != DB_SEP_CHR; // not in comments or strings

			WTString txt = GetSelString();
			if (txt.length() > 0 && txt != "_" && !(m_ScopeLangType == CS && '(' == key && txt == "(")) // [case: 21179]
			{
				const uint curPos = CurPos();
				long line1 = LineFromChar((long)curPos);
				if (instr)
				{
					SwapAnchor();
					long line2 = LineFromChar((long)CurPos());
					SwapAnchor();
					if (line1 != line2)
					{
						// allow surround if selection is multiline (case: 15111)
						instr = false;
					}
				}
				else if (Is_Tag_Based(m_ScopeLangType))
				{
					if (mTagParserScopeType == "String" || mTagParserScopeType == "Comment" ||
					    mTagParserScopeType == "tag")
					{
						// [case: 74045]
						// not in comments, strings or tags
						instr = true;
					}
				}

				if (!instr)
				{
					switch (key)
					{
					case '\'':
						if (!Is_VB_VBS_File(m_ScopeLangType))
							break; // Allow ' to comment in VB only
					case '/':
						if (EolTypes::eolNone != EolTypes::GetEolType(txt))
							SendMessage(WM_VA_COMMENTLINE);
						else if (!Is_VB_VBS_File(m_ScopeLangType)) //   No block Comments in VB
							SendMessage(WM_VA_COMMENTBLOCK);
						return;
					case '*':
						if (!(Is_C_CS_File(gTypingDevLang)) || GetSelStringW() != L"&") // [case: 788 / case: 24181]
						{
							SendMessage(WM_VA_COMMENTBLOCK);
							return;
						}
						break;
					case '{':
						SendMessage(WM_COMMAND, VAM_ADDBRACE);
						return;
					case '#':
						// [case: 27945] limit auto-surround on # to these languages
						if (Is_C_CS_File(gTypingDevLang) || Is_VB_VBS_File(gTypingDevLang))
						{
							SendMessage(WM_COMMAND, VAM_IFDEFBLOCK);
							return;
						}
						break;
					case '(':
						SendMessage(WM_COMMAND, VAM_PARENBLOCK);
						return;
					}
				}
				else if (instr && key == '/' && txt[0] == '/' && txt[1] == '/')
				{
					// attempting c++ line comment removal?
					// see if selection goes to end of line
					long p1 = GetSelBegPos();
					const long p2 = (long)CurPos();
					p1 = max(p1, p2);
					const long p1Line = LineFromChar(p1);
					const uint eolPos = LinePos(p1Line, 0) + GetLine(p1Line).GetLength();
					if ((uint)p1 == eolPos)
					{
						// if so, allow removal of comment at end of line/file
						SendMessage(WM_VA_COMMENTLINE);
						return;
					}
				}
			}
		}
		else if (Psettings->mEnableSurroundWithSnippetOnChar && !gExpSession)
		{
			bool trySurround = Psettings->mSurroundWithSnippetOnCharIgnoreWhitespace;
			if (!trySurround)
			{
				WTString txt = GetSelString();
				trySurround = -1 != txt.FindOneOf("\r\n\t ");
			}

			if (trySurround)
			{
				// [case: 90458] [case: 90489] [case: 90661] [case: 93760]
				// find snippet with shortcut == key and whose source uses $selected$
				std::list<int> snippetIndexes;
				if (gAutotextMgr->GetSurroundWithSnippetsForSpecialShortcut((char)key, snippetIndexes))
				{
					if (snippetIndexes.size())
					{
						if (InsertSnippetFromList(snippetIndexes))
							return;
					}
				}
			}
		}
	}

	g_paintingEdCtrl = this;
	if (!Psettings->m_incrementalSearch)
		modified = TRUE;
	if (key == VK_ESCAPE || key == VK_BACK)
	{
		if (gShellAttr->IsDevenv())
		{
			SetTimer(ID_GETBUFFER, 500, NULL);
			// UpdateWindow();
			Invalidate(FALSE);

#ifdef RAD_STUDIO
			if(gShellAttr->IsCppBuilder())
				CTer::OnChar(key, count, flags);
#endif
		}
		else
			CTer::OnChar(key, count, flags);

		return;
	}
	if (key == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x1000))
	{
		_ASSERTE(gShellAttr->IsDevenv());
		// handled in onkeydown.
		return;
	}

	int dirty = m_bufState;
	bool hadSel = m_hasSel || (key == VK_RETURN);
	DEFTIMERNOTE(OnCharTimer, itos(key));
	LOG("OnChar");
	m_undoOnBkSpacePos = (uint)-1;
	CrackedWrapperCheck chk(g_pUsage->mCharsTyped, 512);

	if (!m_typing && gShellAttr->IsDevenv10OrHigher())
		GetCharPos(0); // Hack to force updating of GetViewOffset() when zooming views.

	m_typing = gTypingAllowed;
	if ((Psettings->m_incrementalSearch && gShellAttr->IsDevenv()) || !gTypingAllowed)
	{
		CTer::OnChar(key, count, flags);
		return;
	}
	OnModified();

	Log("EDC::OCH:LBT");
	bool doElectricReturn = false;
#ifndef TI_BUILD
	WTString nukedIndentStr;

	if (!GetSymDtypeType() && strchr(":-+=!@#$%^&*(){}[]' \r\n;,.", (int)key)) // added ';' for faster case correct after;
	{
		Scope(TRUE);
		CurScopeWord();
		// fix case when m_autoSuggest is disabled case:20498
		if (!Psettings->m_autoSuggest && Psettings->CaseCorrect &&
		    !g_CompletionSet->IsFileCompletion() // #include "a-a.h" // don't complete case=19564
		    && !GetSymDtypeType() && m_isValidScope && !GetParseDb()->m_isDef &&
		    m_lastScope.c_str()[0] == DB_SEP_CHR // Not within strings
		    // && IsCFile(m_ScopeLangType) // limit to c/c++?
		)
		{
			g_CompletionSet->DoCompletion(mThis, 1, true);
		}
	}
#ifndef RAD_STUDIO
	uint ppos = CurPos(); // before key
#endif

	if ((!ISALPHA((int)key) || !key) && count == 1)
	{
		CurScopeWord();
	}
	// tab & ^Z & ^y should be caught by OnKeyDown or HotKey
	if (key == '\t' || key == 0x19 || key == 0x1a)
	{
		SetBufState(BUF_STATE_WRONG);
		return;
	}

	// save def and type of current sym before char is entered.
	WTString lwdef;
	int lwtype;
	int lwAttr;
	{
		AutoLockCs l(mDataLock);
		lwdef = SymDef;
		lwtype = (int)SymType.MaskedType();
		lwAttr = (int)SymType.Attributes();
	}

	// fix (& bold problem cause CurScopeWord doesn't do symbols
	const WTString pscope = Scope(); // scope before keypress

	// <space> before { to align brace for msdev compatibility

	if (!gShellAttr->IsDevenv() && !HasSelection() && key == ' ' && strchr("{}", CharAt(CurPos())))
	{
		token t = GetSubString(LinePos(), CurPos());
		WTString lstr = t.read(" \t");
		if (!lstr.GetLength())
		{
			// align brace if nothing but space left of caret
			gShellSvc->FormatSelection();
			return;
		}
	}

	BOOL didInsert = FALSE;
	int ln = CurLine();
	// Don't insert key if typing match to automatch char
	bool did_over_type = false;
	g_lastChar = key;
	BOOL postTheirCompletion = FALSE;
	if ((char)key == ')')
	{
		// make sure m_pmparse->m_inParenCount is correct
		Scope();
	}

	MultiParsePtr mp(GetParseDb());
	if ((char)key == ')' && mp->m_inParenCount == 1 /*&& !Psettings->m_macroRecordState*/ &&
	    // [case: 75031]
	    // special-case overwrite of ')' in vs2013+ due to our FixUpFnCall that auto-inserts ()
	    (AutoMatch || (gShellAttr->IsDevenv12OrHigher() && Psettings->IsAutomatchAllowed(true))) &&
	    CharAt(CurPos()) == (char)key && !HasSelection())
	{
		// don't insert last closing paren if there already is one
		SetPos(CurPos() + 1);
		did_over_type = true;
		m_autoMatchChar = 0; // only allow typeover once
	}
	else if (!HasSelection() && m_autoMatchChar == (char)key)
	{
		uint cp = CurPos();
		if (m_autoMatchChar == ':' && CharAt(cp ? cp - 1 : 0) == (char)key)
		{
			// don't do anything, ignore second :
			did_over_type = true;
		}
		else if (CharAt(CurPos()) == (char)key)
		{
			SetPos(CurPos() + 1);
			did_over_type = true;
		}
		else
		{                 // if  {..., select [\r\n\t}]
			              // work in progress
			bb = GetBuf(TRUE); // ensure we are in sync with what they have
			long cp2 = (long)CurPos();
			long i = GetBufIndex(bb, cp2);
			bool closeBrcMatch = false;
			LPCSTR p;
			for (p = bb.c_str(); p[i] && wt_isspace(p[i]); i++)
				;
			if (p[i] == m_autoMatchChar)
			{
				if (m_autoMatchChar == '}')
					closeBrcMatch = true;

				SetSel(cp2, i + 1);
				// Insert(""); // some emulations don't do destructive inserts
			}

			const int cl = CurLine();
			(void)cl;
			CTer::OnChar(key, count, flags);

			if (closeBrcMatch && CS == m_ScopeLangType && gShellAttr->IsDevenv14OrHigher() &&
			    0 == g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "Formatting_TriggerOnBlockCompletion"))
			{
				// Reformat this line in C# on insertion of '}' [case=8971] (updated for vs14)
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.FormatSelection"), 0);
			}
		}

		m_autoMatchChar = 0; // only allow typeover once
	}
	else
	{
#ifndef RAD_STUDIO
		if (key == VK_RETURN && !HasSelection())
		{
			// select all spaces/tabs to the right of the caret
			// don't do this if caret is in first column
			if (ppos != LinePos(LineFromChar((long)ppos)))
			{
				long p1, p2;
				GetSel(p1, p2);
				for (char c = CharAt((uint)p2); c == ' ' || c == '\t'; ++p2)
					c = CharAt(uint(p2 + 1));
				if (p1 != p2)
					SetSelection(p1, p2);
			}
			// select all white space left of caret
			uint p1 = LinePos(), p2 = CurPos();
			if (p1 != p2)
			{
				nukedIndentStr = GetSubString(p1, p2);
				token t = nukedIndentStr;
				if (nukedIndentStr.GetLength() && !t.read(" \t").GetLength()) // only if blank line
					SetSelection((long)p1, (long)p2);
				else
					nukedIndentStr.Empty();
			}
		}
#endif

		// Backspace sometimes gets two undos in OnKeyDown and OnChar if we don't clear
		//		ClearModify();
		if (count)
		{
			SetBufState(BUF_STATE_WRONG);
			const BOOL instr = m_lastScope.GetLength() && m_lastScope[0] != DB_SEP_CHR;
			const BOOL doOurExp =
			    !Psettings->m_bUseDefaultIntellisense && IsCFile(m_ftype) && !Psettings->mSuppressAllListboxes;
			const BOOL CanInsert =
			    !HasVsNetPopup(TRUE) &&
			    (!m_cwd.GetLength() || !strchr("\r\n", m_cwd[0])); // don't call insert after <CR>indent
			// added "key == '.' && m_cwd == ']'" below to fix members box for [0].  -Jer
			const BOOL isXref =
			    key && !instr && strchr(".>", (int)key) && m_cwd.GetLength() && !wt_isdigit(m_cwd[0]) &&
			    (m_cwd[0] == '-' || (key != '>' && ISCSYM(m_cwd[0])) ||
			     (key == '.' /*&& m_cwd != m_cwd.SpanExcluding("()[]")*/) // removed SpanExcluding per email (see
			                                                              // original changelist 10316 and then
			                                                              // changelist 16991 / case 9876)
			    );
			if (CanInsert && count == 1 && doOurExp && isXref)
			{
				Insert(WTString((char)key).c_str()); // don't let them expand keywords
				didInsert = TRUE;
				if (Psettings->m_AutoComplete && pscope != "CommentLine" && pscope != "String" &&
				    pscope != DB_SCOPE_PREPROC)
					SetTimer(DSM_VA_LISTMEMBERS, 200,
					         0); // Don't do a PostMessage after insert, vs2010 takes some time to update the WPFView
			}
			else
			{
				bool blockParamInfo = CanInsert && doOurExp && gShellAttr->IsDevenv() &&
				                      !gShellSvc->HasBlockModeSelection(this) && !instr;
				if (blockParamInfo && '(' == key && gShellAttr->IsDevenv12OrHigher())
				{
					// [case: 75031]
					// we insert '(' to prevent their param info from displaying
					// but that can break vs2013+ paren auto completion
					if (!AutoMatch && Psettings->IsAutomatchAllowed(true))
					{
						// we'll insert both ( and ); and use our param info
						AutoMatch = TRUE;
					}
					else if (g_IdeSettings->GetEditorIntOption("C/C++", "BraceCompletion"))
					{
						// our automatch is disabled, but their's isn't.
						// attempting to block their param info breaks their paren completion.
						blockParamInfo = false;
					}
				}

				if (blockParamInfo && strchr("(,", (int)key))
				{
					// don't let them do arg info
					Insert(WTString((char)key).c_str());
					didInsert = TRUE;
				}
				else if (CanInsert && gShellAttr->IsDevenv() && key == ':' && CurWord() == ":" && IsCFile(m_ftype))
				{
					bool doOverride = false;
					if (Psettings->m_AutoComplete && !Psettings->m_bUseDefaultIntellisense &&
					    !Psettings->mSuppressAllListboxes)
					{
						// [case: 9] support for VA members list for Foo::
						// but don't override global "::"
						WTString cur1(CurWord(-1));
						cur1.Trim();
						if (!cur1.IsEmpty())
							doOverride = true;
					}

					if (doOverride)
					{
						// [case: 9] support for VA members list for Foo::
						Insert(":");
						didInsert = TRUE;
#ifndef AVR_STUDIO
						// case=68537, AVR's FormatSelection behaves much differently than VS's, and leaves caret at
						// beginning or line. Since they do not un-indent tags, the reformat below is not needed in AVR
						// [case: 77157] unnecessary in vs2012+
						if (g_LastKeyTyped == ':' && !gShellAttr->IsDevenv11OrHigher())
							gShellSvc->FormatSelection(); // fix indent if user typed previous :
#endif                                                    // AVR_STUDIO
					}
					else
						CTer::OnChar(key, count, flags);

					if (Psettings->m_AutoComplete && !HasVsNetPopup(FALSE))
					{
						// [case: 42349] use timer rather than PostMessage so that VA can provide
						// list if IDE fails
						if (gShellAttr->IsDevenv16OrHigher() && Psettings->m_bUseDefaultIntellisense)
							SetTimer(DSM_VA_LISTMEMBERS, 400, 0); // [case: 142247]
						else
							SetTimer(DSM_VA_LISTMEMBERS, 200, 0);
					}
				}
				else
				{
					if (gShellAttr->IsDevenv10OrHigher() && isXref)
					{
						::ScopeInfoPtr si = ScopeInfoPtr();
						si->m_xref = isXref; // VS10 will cause a completion before our scope can run.
					}
					if (key == '.' && Psettings->m_fixPtrOp && !hadSel && (Src == m_ftype || Header == m_ftype) &&
					    !instr)
					{
						// VA may change this into a "->"
						if (g_LastKeyTyped != '.')
							postTheirCompletion = TRUE;      // Do DSM_LISTMEMBERS when all is done
						Insert(WTString((char)key).c_str()); // don't let them complete till we change to "->"
					}
					else
					{
						// If Power Tools is installed, just let them handle it, or we will get two parens. case=46640
						int mod = m_modCookie;
						const EdCntPtr curEdCnt = g_currentEdCnt;
						if (key == ';')
							if (Psettings && Psettings->mUnrealEngineCppSupport && gShellAttr &&
							    gShellAttr->IsDevenv14OrHigher())
								if (g_IdeSettings &&
								    g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "AutoFormatOnSemicolon"))
									UeEnableUMacroIndentFixIfRelevant(
									    true); // [case: 109205] [case: 141693] fix formatting on semicolon
						CTer::OnChar(key, count, flags);
						if (mUeRestoreSmartIndent)
							UeDisableUMacroIndentFixIfEnabled();
						if (curEdCnt.get() == this && !g_currentEdCnt)
							return; // [case: 51306]
						if (m_IVsTextView && m_modCookie > mod + 1)
						{
							WTString oldLine = GetLine(CurLine()) + (char)key;
							WTString newLine = IvsGetLineText(CurLine());
							oldLine.Trim();
							newLine.Trim();
							if (newLine != oldLine)
							{
								// case=49002
								if (key == '(')
									m_autoMatchChar = ')';

								if (AutoMatch)
								{
									// dev14 C# does a lot of auto-formatting, so restrict power tools workaround
									if (key != '{' || CS != gTypingDevLang || !gShellAttr->IsDevenv14OrHigher())
										AutoMatch = FALSE;
								}
							}
						}
					}

					if (isXref)
					{
						if (m_ScopeLangType == JS || m_ScopeLangType == VBS)
							PostMessage(DSM_VA_LISTMEMBERS, 0, 0);
						else if (gShellAttr->IsDevenv10OrHigher() && IsCFile(m_ScopeLangType))
						{
							if (Psettings->m_bUseDefaultIntellisense)
							{
								if (gShellAttr->IsDevenv16OrHigher())
									SetTimer(DSM_VA_LISTMEMBERS, 400, 0); // [case: 142247]
								else
									SetTimer(DSM_VA_LISTMEMBERS, 200,
									         0); // Give their intellisense time to display, if not display ours. Using
									             // timer to give them extra time to display.
							}
							else
								SetTimer(DSM_VA_LISTMEMBERS, 10, 0);
						}
					}
				}

				if (gShellAttr->IsDevenv() && key == '(')
					HasVsNetPopup(FALSE); // color arg tooltip
				if (!gShellAttr->IsDevenv() && m_cwd == ":" && key == ':' && Psettings->m_AutoComplete)
					PostMessage(DSM_VA_LISTMEMBERS, 0, 0);
			}
		}
	}
	WTString cwd;
#endif // TI_BUILD

	{
		// mshack to modify insert char into buffer so we don't need to GetBuffer again
		long p1, p2;
		GetSel(p1, p2);
		if (key != VK_ESCAPE && !did_over_type)
		{
			/*for(int i =count; i;i--)*/
			{
				long offset = GetBufIndex(p2 - 1); // get position
				if (Psettings->m_overtype && (long)(offset + count) < (long)bb.GetLength() &&
				    bb[(int)offset] != '\n')
				{
					for (int i = (int)count; i; i--)
						bb.SetAt(offset + i - 2, (TCHAR)key);
					UpdateBuf(bb, false);
				}
				else
				{
					bool doGetBuf = m_hasSel;
					if (!doGetBuf && gShellAttr->IsDevenv12OrHigher() && !AutoMatch)
					{
						static const WTString kVsAutoMatchKeys("[\'\"({");
						if (kVsAutoMatchKeys.Find((char)key) != -1)
						{
							if (Psettings->IsAutomatchAllowed(true))
							{
								// need to sync up with what they auto-matched
								doGetBuf = true;
							}
						}
					}

					if (doGetBuf || ((p2 & 0x7) > 1 && offset > 1 &&
					                 strchr("\r\n", bb[(int)offset - 1])) // char after CR, needs sink
					)
					{
						// sink with them now
						GetBuf(TRUE);
					}
					else
					{
						if (count && !didInsert &&
						    m_bufState != BUF_STATE_CLEAN) // called GetBuf, so char already in buf, happens in C#2005
							BufInsert(offset, WTString((wchar_t)key).c_str()); // [case: 137996] key passed as wchar_t
						if (ln == CurLine())
							m_bufState = dirty; // restore to previous state
					}
				}
				SetTimer(ID_GETBUFFER, 500, NULL);
			}
		}

		if (m_hasSel)
		{
			if (!m_IVsTextView || m_IVsTextView->GetSelectionMode() != SM_BOX)
				SetPos(CurPos(TRUE)); // cancel Selection for the "mYbug"
		}

		if (g_loggingEnabled)
			MyLog("OnKeyDown: '%c', bw=%d, h=%d, Sel=&d", key, Psettings->m_borderWidth,
			      g_FontSettings->GetCharHeight(), hadSel);
		cwd = CurWord();
		if (key == '"')
			Scope(); // reget scope so file expansion below works

		if (key == '<' || key == '"') // update scope for #include's
			Scope();
		if (gShellAttr->IsDevenv())
			UpdateWindow();
		if (key == ';' && g_LastKeyTyped != ';')
		{
			Reparse();
			mp = GetParseDb();
		}
		if (gShellAttr->IsDevenv() && (key == ':' || !strchr(CurWord().c_str(), (char)key)))
		{
			GetBuf(TRUE);
		}
		if (!Psettings->m_AutoComplete || m_lastScope.c_str()[0] != DB_SEP_CHR)
		{
		}
		else if ((key == '.') && m_lastScope == DB_SCOPE_PREPROC)
		{
			// allow expand while typing [#include "foo.] if listbox is already up
		}
		else if (key == '.' || (key == '>' && CurWord() == "->"))
		{
			//			if(!isVSNET) // let them do it.  SHould we post a message in case they don't do it?
			// CmEditExpand(3);
		}
		else if (key == '(' /*|| (key == '<' && m_lastScope != DB_SCOPE_PREPROC)*/)
		{            // display args  for f(...) and t<...>
			Scope(); // refresh tooltip info
			if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
				SetTimer(
				    ID_ARGTEMPLATE_TIMER,
				    (IsCFile(m_ftype) && (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes))
				        ? 200u
				        : 10u,
				    NULL);
		}
		else if (!Psettings->m_AutoComplete || m_lastScope.c_str()[0] != DB_SEP_CHR)
		{
		}
		else if (strchr(":.>", (int)key) && (cwd == "::" || cwd == "->" || cwd == ".") &&
		         (m_ftype == Src || m_ftype == Header))
		{
			// 			if(!gShellAttr->IsDevenv()) // let them do it.  SHould we post a message in case they don't do
			// it? 				g_ListBoxLoader->Populate(this, 3);
		}
		else if (m_lastScope == DB_SCOPE_PREPROC && (key == '<' || key == '"' || key == '/' || key == '\\'))
		{
			CmEditExpand(ET_EXPAND_INCLUDE); // Special flag to only expand includes/imports.
		}
	}

	if (!(gShellAttr->IsDevenv()))
		CTer::UpdateWindow();
#ifndef TI_BUILD

	if (ISALNUM((int)key))
		m_doFixCase = true;

	switch (key)
	{
	case '>':
		if (Is_Tag_Based(m_ftype))
			GetBuf(TRUE); // They will insert a closing </tag>, need to getbuf to make sure weare in sink.
		break;
	case '[':
		key = ']'; // for automatch below
	case '<':      // #include <>
		if (key == '<')
		{
			if (m_lastScope != DB_SCOPE_PREPROC)
				break;
			key = '>';
		}
	case '\'':
	case '"':
		if ('\'' == key && (Is_Tag_Based(gTypingDevLang) || Is_VB_VBS_File(m_ftype)))
			break; // don't auto match ' in text or VB// case 20648

		if (AutoMatch && !did_over_type && pscope[0] == DB_SEP_CHR)
		{
			const char nextCh = CharAt(CurPos());
			if (wt_isspace(nextCh) || nextCh == ')')
			{
				// see if we are in a string already
				WTString ln2 = GetSubString(LinePos(), CurPos());
				if (key == '>' && !ln2.contains("include") && !ln2.contains("import"))
					break;
				BOOL inComment = FALSE;
				for (int n = 0; n < ln2.GetLength() - 1; n++)
				{
					if (ln2[n] == '"')
						inComment = !inComment;
				}
				if (inComment)
					break;

				// not already in a string
				WTString k = (char)key;
				Insert(k.c_str());
				SetPos(CurPos() - 1);
				m_undoOnBkSpacePos = CurPos();
				m_autoMatchChar = (char)key;
			}
		}
		break;
	case '(':
		if (AutoMatch && !did_over_type)
		{
			bool tryParenAutomatch = pscope[0] == DB_SEP_CHR;
			if (!tryParenAutomatch && kWasExpUp && !g_CompletionSet->IsExpUp(mThis) &&
			    Psettings->IsAutomatchAllowed(true, key))
				tryParenAutomatch = true; // [case: 97044]

			if (tryParenAutomatch)
			{
				char nc = CharAt(CurPos());
				if (wt_isspace(nc) || nc == ')')
				{
					Insert(")");
					m_autoMatchChar = ')';
					SetPos(CurPos() - 1);
					m_typing = true; // Allow ScopeSuggestions after "(" case=8612
					m_undoOnBkSpacePos = CurPos();
				}
			}
		}
		break;
	case ')': //
		break;
	case '{':
		// Changed behavior of Automatch {}, Resharp does this and I liked it much better
		if (AutoMatch && !did_over_type && pscope[0] == DB_SEP_CHR)
		{
			bb = GetBuf(TRUE);
			LPCSTR buf = bb.c_str();
			long cp = GetBufIndex(bb, (int)CurPos());
			while (buf[cp] == ' ' || buf[cp] == '\t')
				cp++;
			if (buf[cp] == '\r' || buf[cp] == '\n') // no more code to right of cursor
			{
				// see if next line is indented
				int ln2 = CurLine();
				token2 lines = GetSubString(LinePos(ln2), LinePos(ln2 + 2));
				WTString curln = lines.read("\r\n");
				WTString nextln = lines.read("\r\n");
				int i;
				for (i = 0; curln[i] && curln[i] == nextln[i]; i++)
					;
				if (!nextln[i] || !wt_isspace(nextln[i]))
				{
					// Next line not indented
					Scope(TRUE);
					std::unique_ptr<UndoContext> undoContext; // don't use undo in vs2003 C# - bug 2299
					if (m_ftype == Src || m_ftype == Header || m_ftype == UC || gShellAttr->IsDevenv8OrHigher())
						undoContext = std::make_unique<UndoContext>("AutoInsertCloseBrace");
					WTString closingBrace("}");
					if (IsCFile(m_ftype))
					{
						if (mp->m_inClassImplementation)
						{
							if (UC == m_ftype && !mp->m_ParentScopeStr.IsEmpty() &&
							    strstrWholeWord(mp->m_ParentScopeStr, "state", FALSE))
								; // no ';' after UC state definition
							else
								closingBrace = "};";
						}
						else
						{
							// is there a better to see if the open brace is for an enum def?
							const WTString curLn(GetLine(CurLine()));
							if (strstrWholeWord(curLn, "enum"))
								closingBrace = "};";
							else
							{
								const WTString prevLn(GetLine(CurLine() - 1));
								if (strstrWholeWord(prevLn, "enum"))
									closingBrace = "};";
							}
						}
					}
					Insert(closingBrace.c_str());
					SetPos(CurPos() - closingBrace.GetLength());
					m_autoMatchChar = '}';
					m_undoOnBkSpacePos = CurPos();

					// On a blank line, do we handle {}'s differently?
					if (Psettings->mBraceAutoMatchStyle == 3 || Psettings->mBraceAutoMatchStyle == 2)
					{
						curln.TrimLeft();
						if (curln[0] == '{')
						{
							// hitting { on blank line
							if (Psettings->mBraceAutoMatchStyle == 3)
								ProcessReturn(); // auto insert {\n\t\n}
							else
								ProcessReturn(false); // auto insert {\n} - old VA style
							m_undoOnBkSpacePos = CurPos();
							m_autoMatchChar = '\n';
						}
					}

					if (CS == m_ScopeLangType && gShellAttr->IsDevenv() &&
					    (gShellAttr->IsDevenv10OrHigher() || !Is_Tag_Based(m_ftype)) && // [case: 69883]
					    g_IdeSettings->GetEditorBoolOption("CSharp-Specific", "Formatting_TriggerOnBlockCompletion"))
					{
						// [case: 55483] more checks due to regression in pre-vs2010 versions
						// with block (or none) tab indent
						if (gShellAttr->IsDevenv10OrHigher() ||
						    2 == g_IdeSettings->GetEditorIntOption("CSharp", "IndentStyle"))
						{
							// Reformat C# on insertion of '}' [case=8971]
							int cl = CurLine();
							// Reformat this line and the two above it.
							Reformat(-1, cl - 3, -1, cl);
						}
					}
				}
			}
		}

	case ';':                          // handled in SaveUndo
	                                   // case '/': //
	                                   // case '*':
		if (Scope() != "CommentBlock") // don't reparse on ; or { in comment
		{
			if (key != ';' || g_LastKeyTyped != ';')
				Reparse();
		}
		// may support this later?
		//		if (Psettings->m_electricSemicolon || Psettings->m_electricBrace)
		//		{
		//			// don't do if in string, comment, preproc
		//			if(!(pscope[0] != ':' || pscope == DB_SCOPE_PREPROC))
		//			{
		//				int line = CurLine();
		//				// get start of line pos
		//				int lineStart = LineIndex(line);
		//				// get end of line pos
		//				int lineEnd = lineStart + LineLength(lineStart);
		//				// if at beg of line, goto first non-whitespace char
		//				TCharRange lineRng(lineStart, (lineEnd - lineStart < 256 ? lineEnd : lineStart + 255));
		//				char buf1[256];
		//				GetTextRange(lineRng, buf1);
		//				if (';' == key && Psettings->m_electricSemicolon) {
		//					// don't do if first line of a for loop
		//					WTString buf2(buf1);
		//					int cpPos = buf2.find_first_not_of(" \t");
		//					if (strncmp("for", &buf1[cpPos], 3))
		//						doElectricReturn = true;
		//				} else if ('{' == key && Psettings->m_electricBrace) {
		//					token tok = buf1;
		//					if (tok.SubStr(TRegexp("[ \t]+[Ss]et")).length() ||
		//						tok.SubStr(TRegexp("[ \t]+[Gg]et")).length() ||
		//						tok.SubStr(TRegexp("[ \t]+[Pp]ut")).length()
		//						)
		//					{
		//						// place closing } on same line - no electric return
		//						if (AutoMatch /*&& !Psettings->m_macroRecordState*/)
		//						{
		//							CmEditUndo();
		//							int cp = CurPos();
		//							Insert("}");
		//							SetPos(cp);
		//						}
		//					} else
		//						doElectricReturn = true;
		//				}
		//			}
		//		}
		break;
	case '.':
		if (!Psettings->m_fixPtrOp || hadSel || (Src != m_ftype && Header != m_ftype))
			break;

		// [case: 141003] [ue4 / hlsl] do not do dot to arrow conversion in HLSL or UE shader files
		if (IsHlslFile(FileName()))
			break;

		// change '.' to '->' where needed
		m_lastScopePos = 0;
		if ('.' == CharAt(CurPos() - 2))
			break; // [case:19149] slow when typing "..................."
		if (strchr(")]", CharAt(CurPos() - 2)))
		{
			// get def of "foo()" so we can later test to see if it is a pointer
			Scope();
			lwtype = lwAttr = 0;
			mp->Tdef(WTString(mp->m_xrefScope), lwdef, FALSE, lwtype, lwAttr);
		}

		// made unconditional due to bug 2131 - scope is on a timer / can't rely on lwdef
		{ // fix for combo case correct/. to -> conversion
			uint cp = CurPos();
			SetPos(cp - 1);
			{
				// give parse thread a shot at completing since CurScopeWord will
				// just set a timer and not return anything if the thread is active
				Sleep(20);
				const WTString prevDef(lwdef);
				vLog("PtrOp: pd(%s)", prevDef.c_str());
				lwdef = CurScopeWord(); // may have been fixed by case correct
				if (lwdef.IsEmpty() && m_cwd == "this" && prevDef == "* HIDETHIS ")
				{
					// [case: 72517]
					// CurScopeWord hoses SymDef for 'this' if current scope
					// is not explicitly scoped (relying on using namespace)
					lwdef = prevDef;
				}
			}

			{
				AutoLockCs l(mDataLock);
				lwtype = (int)SymType.MaskedType();
				lwAttr = (int)SymType.Attributes();
			}
			SetPos(cp);

#ifdef case113970
			if (0 == lwdef.Find("using "))
			{
				// [case: 113970]
				auto dat = mp->GetCwData();
				if (dat)
				{
					auto dat2 = ::TraverseUsing(dat, mp.get());
					if (dat2 && dat2.get() != dat.get())
					{
						lwdef = dat2->Def();
						lwAttr = (int)dat2->Attributes();
						lwtype = (int)dat2->MaskedType();
					}
				}
			}
#endif
		}

		if (!m_txtFile && lwdef.length() && m_lastScope != "String" && m_lastScope != DB_SCOPE_PREPROC &&
		    m_cwd != "...") // case=8300: hack for incorrect scope in catch(...)
		{
			vLog("PtrOp: s1(%s), lwd(%s)", m_lastScope.c_str(), lwdef.c_str());
			bool tryConvert = false;
			bool overrideArrayCheck = false;
			if (lwAttr & V_POINTER)
			{
				vLog("PtrOp: V_POINTER");
				tryConvert = true;
			}
			else if (mp->IsPointer(lwdef))
			{
				vLog("PtrOp: IsPointer");
				tryConvert = true;
			}
			else if (m_cwdLeftOfCursor == "this") // case=21161 - difference in debug vs release due to EdCnt::GetBuf
			{
				vLog("PtrOp: this 1");
				tryConvert = true;
			}
			else if (m_cwd == "this" && m_cwdLeftOfCursor == ".")
			{
				vLog("PtrOp: this 2");
				tryConvert = true;
			}
			else if (Psettings->m_fixSmartPtrOp && mp->GetCwData())
			{
				// [case: 999] optionally do . to -> on smart pointer vars
				const WTString ss(GetSymScope());
				if ((lwtype & VAR) && mp->HasPointerOverride(ss))
					tryConvert = true;
				else if ((lwtype & FUNC) && ss.Find("[]") == (ss.GetLength() - 2) &&
				         mp->HasPointerOverride(ss))
				{
					// also handle case where operator[] is overloaded and returns a type that overloads ->
					overrideArrayCheck = tryConvert = true;
				}
			}

			if (tryConvert)
			{
				// this might already have been done by Expand if case had to be fixed
				const uint pos = CurPos() - 1;
				if (CharAt(pos) == '.')
				{
					const WTString cur1(CurWord(-1));
					tryConvert = overrideArrayCheck || (cur1 != ']');
					if (!tryConvert)
					{
						WTString theDef;
						int pos2 = lwdef.Find('=');
						if (-1 == pos2)
							theDef = lwdef;
						else
							theDef = lwdef.Left(pos2);

						if (theDef.FindOneOf(("*^" + ::EncodeScope("*^")).c_str()) != -1 && theDef.Find('[') != -1)
							tryConvert = true;
						else if (-1 == theDef.Find('[') && -1 != theDef.Find("**"))
							tryConvert = true; // [case: 70518]
					}

					if (tryConvert)
					{
						const WTString cur2(CurWord(-2));
						const WTString cur3(CurWord(-3));

						bool doConvert = false;
						if (cur1 == ")" && cur3 == "*")
							; // don't convert (*t).
						else if ((-1 != cur2.FindOneOf("<>") && cur2 != "->" && cur2 != ">>") ||
						         -1 != CurWord().FindOneOf("<>"))
						{
							// [case: 73142] [case: 80210] [case: 78153]
							// don't convert on foo[1] > .5
							// parser template handling gives incorrect scope for comparison operators?
							// prevent conversion;  does not fix impropery display of members list
						}
						else if (cur1 != ")" && cur2 == "*")
						{
							if (lwdef.Find("operator*") == -1 && lwdef.Find("operator *") == -1)
								doConvert = true; // convert *t. but not if t has an operator* overload
						}
						else if (!cur2.contains("*")) // (this is the original condition)
							doConvert = true;         // convert t. and *(t).

						if (doConvert)
						{
							SetSelection((long)pos, long(pos + 1));
							WrapperCheckDecoy chk2(g_pUsage->mDotToPointerConversions);
							Insert("->");
							m_undoOnBkSpacePos = CurPos();
							SetSelection((long)m_undoOnBkSpacePos, (long)m_undoOnBkSpacePos);
							if (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes)
							{
								if (gShellAttr->IsDevenv10OrHigher())
									postTheirCompletion = TRUE; // Do timer after insert in VS2010
								else
								{
									postTheirCompletion = FALSE;
									::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_LISTMEMBERS, NULL);
								}
							}

							CheckRepeatedDotToArrow(pos);
						}
					}
				}
			}
		}
		break;
	case '}':
		if (Scope()[0] != DB_SEP_CHR || Scope() == DB_SCOPE_PREPROC)
			break;
	case '\r':
	case '\n': {
		// reformat bullets
		if (m_lborder)
			m_lborder->DrawBkGnd();
	}

	break;
	case '/': // fix repaint after typing /* or */
	case '*':
		if (cwd.contains("/*") || cwd.contains("*/"))
			Invalidate(TRUE);
	}
	//	SetModify(FALSE);

#endif // TI_BUILD
	if (ISCSYM((int)key) ||
	    g_CompletionSet->IsExpUp(mThis) // If IsExpUp, allow filtering on any char ie :XML <! [case 15614]
	)
		g_CompletionSet->DoCompletion(mThis, 0, false);
	SetTimer(ID_TIMER_GETHINT, 10, NULL);

	if (key == '(' || key =='{')
	{
		if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
			SetTimer(ID_ARGTEMPLATE_DISPLAY_TIMER,
			         (IsCFile(m_ftype) && (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes))
			             ? 500u
			             : 10u,
			         NULL);
	}
	else if (key == ')' || key == '}')
	{
		DisplayToolTipArgs(false);
		SetTimer(ID_ARGTEMPLATE_TIMER, 500, NULL);
	}
	else if (key == ',')
	{
		Scope(true); // refresh tooltip info
		if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
			SetTimer(ID_ARGTEMPLATE_DISPLAY_TIMER, 50, NULL);
	}

	if (doElectricReturn)
	{
		// TODO: anyway to make this a single undoable action for DEL or BACKSPACE?
		SendMessage(WM_KEYDOWN, VK_RETURN, 0x00000001);
		SendMessage(WM_CHAR, VK_RETURN, 1);
		SendMessage(WM_KEYUP, VK_RETURN, 0xC0000001);
	}
	if (postTheirCompletion && (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes ||
	                            (Psettings->mRestrictVaListboxesToC && !(IsCFile(m_ftype)))))
	{
		postTheirCompletion = FALSE;
		KillTimer(DSM_VA_LISTMEMBERS); // OnTimer::DSM_LISTMEMBERS will reset this if needed
		SetTimer(DSM_LISTMEMBERS, 10, NULL);
	}
	// GetCWHint();
	SetTimer(ID_TIMER_GETHINT, 10, NULL);
	SetTimer(ID_CURSCOPEWORD_TIMER, 500, NULL);
	SetTimer(ID_ADDMETHOD_TIMER, 200, NULL);

	if (g_CompletionSet->m_popType == ET_SUGGEST_BITS)
		g_CompletionSet->Dismiss();
	//	ScrollCaret(false);
}

void EdCnt::CheckRepeatedDotToArrow(uint pos)
{
	if (mLastDotToArrowPos == pos)
	{
		const TCHAR* infoMsg =
		    "Backspace, Delete or Undo will restore the '.' that was originally typed.\r\n\r\n"
		    "Alternatively, you can disable the auto-conversion of '.' to '->' on the Editor page of the "
		    "Visual Assist Options dialog.";
		::OneTimeMessageBox(_T("InfoDotToArrow"), infoMsg);
	}
	mLastDotToArrowPos = pos;
}

Guesses g_Guesses;

static void GetGuesses(MultiParse* mp, WTString scope, WTString bcl, WTString str, LPCSTR buf, BOOL caseMatch = FALSE,
                       LPCSTR endPtr = NULL, BOOL txtFile = FALSE)
{
	LPCSTR p1 = endPtr ? strrstr(buf, endPtr, str.c_str(), caseMatch) : strstrn(buf, str.c_str(), caseMatch, 5000);
	// Get all likely guesses, then limit how many are displayed in VACompletionSet.
	while (p1 /*&& g_Guesses.GetCount() < 10*/)
	{
		// look for case in code above
		int i;
		for (i = 0; p1[i] && ISCSYM(p1[i]); i++)
			;

		if (i > 0)
		{
			WTString sym(p1, i);
			DType* dt = nullptr;
			if ((txtFile && (dt = mp->FindAnySym(sym)) != nullptr) ||
			    (dt = mp->FindSym(&sym, scope.GetLength() ? &scope : NULL, &bcl)) != nullptr)
			{
				if (!g_Guesses.ContainsWholeWord(sym))
				{
					g_Guesses.AddGuess(CompletionSetEntry(sym));
					if (g_Guesses.GetMostLikelyGuess().IsEmpty())
						g_Guesses.SetMostLikely(sym);
				}
			}
		}

		p1 = endPtr ? strrstr(buf, p1, str.c_str(), caseMatch)
		            : strstrn(&p1[1], str.c_str(), caseMatch, 5000 - ptr_sub__int(p1, buf));
	}
}

#define GUESSCOUNT Psettings->m_nDisplayXSuggestions

// Changed HintThread so each call gets it's own thread
// Fixes inconsistency when typing the same thing twice resulted in different suggestions
// Also it seems to have much better response
std::atomic<int> s_HintThreadCookie;

void InvalidateHintThread()
{
	// Don't let hintthread display old suggestions in new locations.
	s_HintThreadCookie += 2;
}

class HintThread : public PooledThreadBase
{
	const int m_cookie; // so we know to abort if another thread is started
	LPCSTR m_ep, m_sp;
	const WTString m_cwd;
	const WTString mEdBufCopy; // so that m_ep and m_sp don't point to released mem if buf is updated
	EdCntPtr m_pEd;

  public:
	HintThread(EdCntPtr ed, const WTString& cwd, long sp_idx, long ep_idx)
	    : PooledThreadBase("HintThread"), m_cookie(++s_HintThreadCookie), m_cwd(cwd), mEdBufCopy(ed->GetBuf()),
	      m_pEd(ed)
	{
		LOG2("HintThread ctor");
		m_ep = mEdBufCopy.c_str() + ep_idx;
		m_sp = mEdBufCopy.c_str() + sp_idx;
		StartThread();
	}

  protected:
#define CheckCookie()                                                                                                  \
	{                                                                                                                  \
		if (m_cookie != s_HintThreadCookie)                                                                            \
			return;                                                                                                    \
	}

	virtual void Run()
	{
		if (gShellIsUnloading)
			return;

#if !defined(SEAN)
		try // make sure this thread never dies
#endif      // !SEAN
		{
			Log1("HintThread2");
			CheckCookie();
			Log1("HintThread1");
			LPCSTR ep = m_ep, sp = m_sp;
			WTString cwd = m_cwd;
			WTString localVarGuess;
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				WTString scope, bcl;
				MultiParsePtr mp = m_pEd->GetParseDb();
				mp->CwScopeInfo(scope, bcl, m_pEd->m_ScopeLangType);
				if (scope == DB_SCOPE_PREPROC)
				{
					if (gShellAttr->IsDevenv()) // m_pEd->CurLine() not thread safe in vc6
					{
						const WTString line =
						    m_pEd->GetSubString(m_pEd->LinePos(), m_pEd->LinePos(m_pEd->CurLine() + 1));
						if (line.contains("include"))
							return;
					}
				}

				CheckCookie();
				g_Guesses.Reset();
				g_Guesses.ClearMostLikely();

				// Get all likely guesses, then limit how many are displayed in VACompletionSet.

				// Search above
				CheckCookie();
				GetGuesses(mp.get(), scope, bcl, cwd, max((ep - 5000), sp), FALSE, ep - 1);

				// Search below
				CheckCookie();
				GetGuesses(mp.get(), scope, bcl, cwd, ep, FALSE);

				// look in rbuffer history
				CheckCookie();
				const WTString rbuf = g_rbuffer.GetStr();
				GetGuesses(mp.get(), scope, bcl, cwd, rbuf.c_str(), FALSE, rbuf.c_str() + rbuf.GetLength());

				// look in exp history
				CheckCookie();
				const WTString p = g_ExpHistory->GetExpHistory(cwd);
				GetGuesses(mp.get(), scope, bcl, cwd, p.c_str(), false);

				// Only start looking for suggestions from db's after N characters and we don't already have a full boat
				const int cwdLen = cwd.GetLength();
				if (cwdLen > 2 // Don't take wild guesses until the user types more than N characters
				    && g_Guesses.GetCount() < GUESSCOUNT // If there are plenty of guesses, don't need to search db
				)
				{
					CheckCookie();
					const int rank = ContainsSubset(g_Guesses.GetMostLikelyGuess().c_str(), cwd.c_str(),
					                                Psettings->m_bAllowShorthand ? 0x2 : 0u);
					CheckCookie();

					// Suggestions from CurrentFile
					if (cwdLen > 3)
					{
						mp->LDictionary()->GetHint(cwd, scope, bcl, localVarGuess);
						CheckCookie();
					}

					// Suggestions from Solution
					if (cwdLen > 3 && g_Guesses.GetCount() < GUESSCOUNT)
					{
						if (!g_pGlobDic->GetHint(cwd, scope, bcl, localVarGuess))
							return;
						CheckCookie();
					}

					// Suggestions from .va files "reserved words" case:17920, case: 60913
					if (cwdLen > 2 && g_Guesses.GetCount() < GUESSCOUNT)
					{
						GetDFileMP(m_pEd->m_ScopeLangType)->LDictionary()->GetHint(cwd, scope, bcl, localVarGuess);
						CheckCookie();
					}

					// Suggestions from SysDic
					if (cwdLen > 3 && g_Guesses.GetCount() < GUESSCOUNT)
					{
						if (!GetSysDic()->GetHint(cwd, scope, bcl, localVarGuess))
							return;
						CheckCookie();
					}

					if (cwdLen > 3 && g_Guesses.GetCount() < GUESSCOUNT && (!rank || rank >= 30))
					{
						WTString cwdlcase = cwd;
						cwdlcase.MakeLower();
						if (cwd != cwdlcase)
						{
							cwd = cwdlcase;
							mp->LDictionary()->GetHint(cwd, scope, bcl, localVarGuess);
							CheckCookie();

							if (g_Guesses.GetCount() < GUESSCOUNT)
							{
								if (!g_pGlobDic->GetHint(cwd, scope, bcl, localVarGuess))
									return;
								CheckCookie();
							}

							if (g_Guesses.GetCount() < GUESSCOUNT)
							{
								if (!GetSysDic()->GetHint(cwd, scope, bcl, localVarGuess))
									return;
							}
						}
					}
				}

				CheckCookie();
				if (g_Guesses.GetCount())
					m_pEd->PostMessage(
					    WM_COMMAND, VAM_SHOWGUESSES,
					    0); // Needs to be PostMessage for Case:5444/3050, or this will be called before the OnChar()
				else if (m_cookie == s_HintThreadCookie)
				{
					Log("HT:R:HI");
				}
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("HT1:");
				if (!Psettings->m_catchAll && !gTestsActive)
				{
					_ASSERTE(!"Fix the bad code that caused this exception in HintThread");
				}
			}
#endif // !SEAN
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("HT2:");
			if (!Psettings->m_catchAll && !gTestsActive)
			{
				_ASSERTE(!"Fix the bad code that caused this exception in HintThread");
			}
		}
#endif // !SEAN
	}
};

void EdCnt::GetCWHint()
{
	if (Psettings->mSuppressAllListboxes)
		return;
	if (Psettings->mRestrictVaListboxesToC && !(IsCFile(m_ftype)))
		return;
	if ((!Psettings->m_autoSuggest && !Psettings->m_codeTemplateTooltips) || g_inMacro || !Psettings->m_enableVA ||
	    !gTypingAllowed || Psettings->m_incrementalSearch)
		return;
	if (!g_CompletionSet->ShouldSuggest())
		return;
	// don't offer suggestion if Resharper dlg is up
	if (HasVsNetPopup(TRUE))
		return;

	WTString cwd = CurWord();
	MultiParsePtr mp(GetParseDb());
	if (!mp->FindAnySym(cwd))
		cwd = WordLeftOfCursor();
	if (!cwd.GetLength())
		return;

	LOG("GetCWHint");
	CurScopeWord();
	mp = GetParseDb();
	if (Psettings->UsingResharperSuggestions(m_ftype, false))
		return;
	bool returnIfNoAutotext = false;
	::ScopeInfoPtr si(ScopeInfoPtr());
	if (Is_Tag_Based(m_ftype) || m_ftype == JS || m_ftype == PHP || m_ftype == VBS)
	{
		if (si->HtmlSuggest(mThis))
			return;
		returnIfNoAutotext = true;
	}
	if (si->HasScopeSuggestions() && !si->m_xref)
	{
		const WTString wlc(WordLeftOfCursor());
		if (!wlc.IsEmpty() && wlc[0] == '#' && g_CompletionSet->IsExpUp(mThis) && GetSymScope() == "String")
		{
			// [case: 86085]
			// hashtag suggestion is active -- don't dismiss listbox
			return;
		}

		if (wlc.find_first_of(",=<>)\"'") == -1) // do not offer immediately after these symbols
		{
			g_Guesses.ClearMostLikely();
			g_Guesses.Reset();
			if (!g_CompletionSet->IsVACompletionSetEx())
				g_CompletionSet->Dismiss();
			g_CompletionSet->ShowGueses(mThis, "", ET_SUGGEST); // show ScopeSuggestions/autotext immediately.
			if (cwd.GetLength() == 1 && ISCSYM(cwd[0]) && g_CompletionSet->IsExpUp(mThis))
				return; // Don't include other suggestions unless the user keeps typing...
		}
	}

	int autotextItemIdx = -1;
	WTString autotextTitle;
	if (Psettings->m_codeTemplateTooltips && !mp->m_xrefScope.GetLength() && !si->m_xref)
		autotextTitle = gAutotextMgr->FindNextShortcutMatch(cwd, autotextItemIdx);
	if (autotextTitle.GetLength() && !g_CompletionSet->IsExpUp(NULL))
		g_CompletionSet->ShowGueses(mThis, "",
		                            ET_SUGGEST); // show autotext immediately, Passing "" wil fill with autotext only.

	if (m_lastScope.c_str()[0] != DB_SEP_CHR && cwd.FindOneOf("/#") != 0)
	{
		if (Is_C_CS_VB_File(m_ftype))
			return;
		returnIfNoAutotext = true;
	}
	else if (Is_Tag_Based(m_ftype) && m_lastScope.Find(":<") == 0)
		return; // <html> Don't suggest here </html>

	if (!returnIfNoAutotext)
	{
		if (si->HasUe4SuggestionMode())
		{
			// [case: 109117]
			returnIfNoAutotext = true;
		}
		else if (!Psettings->m_autoSuggest)
		{
			_ASSERTE(Psettings->m_codeTemplateTooltips);
			returnIfNoAutotext = true;
		}
	}

	const WTString bb(GetBufConst());
	if (m_txtFile && JS != m_ftype)
	{
		cwd.Empty();
		long p = (long)CurPos(TRUE);
		p = GetBufIndex(bb, p);
		LPCSTR pp = &bb.c_str()[p - 1];
		for (; *pp && (ISCSYM(*pp) || strchr("<!-", *pp)) && cwd.GetLength() < 200; pp--)
			cwd.prepend(WTString(*pp).c_str());
	}

	LPCSTR buf = bb.c_str();
	LPCSTR sp = buf;
	LPCSTR ep = &sp[GetBufIndex(bb, (long)CurPos())];
	if (strchr(">\"", *ep) && m_lastScope == DB_SCOPE_PREPROC)
		return; // don't offer suggestions if in #include "foo.h"

	LPCSTR psym;
	for (psym = ep - 1; psym > sp && ISCSYM(*psym); psym--)
		;

	if (returnIfNoAutotext)
		return;

	// search for auto suggest and handle typo alerts
	WTString scope, bcl;
	mp->CwScopeInfo(scope, bcl, m_ScopeLangType);
	WTString recentGuess;

	if ((ep - sp) > 5000)
		sp = ep - 5000;

	if (m_txtFile && ISCSYM(*cwd.c_str()))
	{
		if (!mp->m_xref)
		{
			// look for text match
			const WTString rbuf(g_rbuffer.GetStr());
			LPCSTR p = rbuf.c_str();
			g_Guesses.ClearMostLikely();
			g_Guesses.Reset();

			if (g_Guesses.GetCount() < GUESSCOUNT) // search history
				GetGuesses(mp.get(), scope, bcl, cwd, p, FALSE, p + strlen(p), TRUE);
			if (g_Guesses.GetCount() < GUESSCOUNT) // search back
				GetGuesses(mp.get(), scope, bcl, cwd, max((ep - 5000), sp), FALSE, ep - 1, TRUE);
			if (g_Guesses.GetCount() < GUESSCOUNT) // // search below
				GetGuesses(mp.get(), scope, bcl, cwd, ep, FALSE, NULL, TRUE);

			Log("EDC::CGWH:D1");
			if (g_Guesses.GetCount())
				g_CompletionSet->ShowGueses(mThis, g_Guesses.GetGuesses(), ET_SUGGEST);
			else if (JS != m_ftype && !Is_Tag_Based(m_ftype))
			{
				if (!autotextTitle.GetLength()) // Don't dismiss autotext. case=21067
					g_CompletionSet->Dismiss();
			}
			else if (ET_EXPAND_VSNET != g_CompletionSet->m_popType)
				g_CompletionSet->Dismiss(); // don't close IDE suggestions in JS or HTML
			return;
		}
		LPCSTR bp;
		for (bp = ep; bp > sp && ISCSYM(bp[-1]); bp--)
			;
		for (; bp > sp && strchr("->.", bp[-1]); bp--)
			;
		WTString str(bp, ptr_sub__int(ep, bp));
		//				if(str.FindOneOf("(=") == -1)
		//					return;

		// look for similar pattern...
		extern LPCSTR strrstr(LPCSTR sBeg, LPCSTR sEnd, LPCSTR sFind, BOOL matchCase);
		LPCSTR p1 = strrstr(sp, bp, str.c_str(), FALSE);
		if (!p1)
		{
			p1 = strstrn(ep, str.c_str(), FALSE, 5000);
		}
		if (p1)
		{
			p1 += str.GetLength();
			for (; p1 > sp && ISCSYM(p1[-1]); p1--)
				;
			LPCSTR p2;
			for (p2 = p1; *p2 && strchr(" \t.-", *p2); p2++)
				;
			for (; *p2 && !strchr(" \t\r\n'\".-;=!*^)", *p2); p2++)
			{
				if (*p2 == ',' && mp->m_inParenCount)
				{ // only suggest commas if in parens
					p2++;
					if (*p2 == ' ')
						p2++;
					break;
				}
				if (*p2 == '(')
				{
					p2++;
					if (*p2 == ')')
						p2++;
					break;
				}
			}
			if ((p2 - p1) > 1)
			{
				for (LPCSTR p3 = p1; *p3 && ISCSYM(*p3); p3++)
					;
				WTString wd(p1, ptr_sub__int(p2, p1));
				if (wd != cwd)
					recentGuess = CompletionSetEntry(wd, ET_SUGGEST_BITS);
			}
		}
		Log("EDC::CGWH:D2");
		if (recentGuess.GetLength() && !mp->FindSym(&cwd, scope.GetLength() ? &scope : NULL, &bcl))
			g_CompletionSet->ShowGueses(mThis, recentGuess, ET_SUGGEST_BITS);
		else if (JS != m_ftype && !Is_Tag_Based(m_ftype))
			g_CompletionSet->Dismiss();
		else if (ET_EXPAND_VSNET != g_CompletionSet->m_popType)
			g_CompletionSet->Dismiss(); // don't close IDE suggestions in JS or HTML
		return;
	}
	else if (ISCSYM(*cwd.c_str()))
	{
		Log("EDC::CGWH:D3");
		// Suggestions when isdef caused confusion, so I disabled it to see what would happen...
		// [8031] Turned it back on...
		if (!m_typing /*|| m_pmparse->m_isDef*/)
			return;

		if (g_CompletionSet->m_popType == ET_AUTOTEXT)
		{
			const WTString sugTxt = g_CompletionSet->GetCurrentSelString();
			if (_tcsnicmp(cwd.c_str(), sugTxt.c_str(), (uint)cwd.GetLength()) != 0)
				g_CompletionSet->Dismiss();
		}

		if (scope.contains(DB_SCOPE_PREPROC) && si->m_suggestionType == SUGGEST_NOTHING)
			return; // Respect SUGGEST_NOTHING in #region/pragma/if/define/... lines. case=426

		if (g_CompletionSet->m_popType != ET_EXPAND_MEMBERS && g_CompletionSet->m_popType != ET_EXPAND_VSNET)
		{
			if (SUGGEST_NOTHING == g_CompletionSet->m_popType && mp->m_xref)
			{
				// [case: 57852] members list instead of suggestion when in xref
				if ((IsCFile(m_ScopeLangType) && Psettings->m_bUseDefaultIntellisense) || !IsCFile(m_ScopeLangType))
					SetTimer(DSM_LISTMEMBERS, 10, NULL);
				else
					SetTimer(DSM_VA_LISTMEMBERS, 10, NULL);
				return;
			}

			new HintThread(mThis, cwd, ptr_sub__int(sp, buf), ptr_sub__int(ep, buf));
		}
		return;
	}
	else
	{
		Log("EDC::CGWH:D4");
		if (!Psettings->m_defGuesses || cwd == "else" || cwd == ";" || cwd == "{" || cwd == "}" || cwd == "do" ||
		    cwd[0] == '#' || cwd[0] == '/' || !strchr(" \t\r\n)", *ep)) // don't do it if there is text to the right
		{
			// 					if (isValidSem || (cwd[0] == '#' && s_popType == ET_SUGGEST_AUTOTEXT && _tcsnicmp(cwd,
			// sugTxt, cwd.GetLength())!=0)) 						g_CompletionSet->Dismiss();
			return;
		}
		if (Psettings->m_defGuesses && strchr(" ,=([", *cwd.c_str()))
		{
			EdCntPtr curEd = g_currentEdCnt;
			WTString sugTxt = curEd ? curEd->GetSurroundText() : "";
			if (sugTxt.GetLength() || g_CompletionSet->IsExpUp(NULL))
				g_CompletionSet->ShowGueses(mThis, sugTxt, ET_SUGGEST_BITS);
		}
	}
}

void EdCnt::CmToggleBookmark()
{
	DEFTIMERNOTE(CmToggleBookmarkTimer, NULL);
	int curLn = CurLine(CurPos(IsRSelection() == false));
	bool added = m_LnAttr->Line(curLn)->ToggleBookmark();
	if (g_VATabTree)
		g_VATabTree->ToggleBookmark(FileName(), curLn, added);
	if (m_lborder)
		m_lborder->DrawBkGnd();
}

void EdCnt::CmGotoNextBookmark()
{
	DEFTIMERNOTE(CmGotoNextBookmarkTimer, NULL);
	int line = CurLine();
	if (m_LnAttr->NextBookmarkFromLine(line))
	{
		line = LineIndex(line);
		// Case book mark past eof
		if (line == NPOS)
		{
			line = 0;
			m_LnAttr->NextBookmarkFromLine(line);
			line = LineIndex(line);
		}
		SetSelection(line, line);
		ScrollTo();
		// _hideblock.HorScrollCheck();
	}
}

void EdCnt::CmGotoPrevBookmark()
{
	DEFTIMERNOTE(CmGotoPrevBookmarkTimer, NULL);
	int line = CurLine();
	if (m_LnAttr->PrevBookmarkFromLine(line))
	{
		line = LineIndex(line);
		// Case book mark past eof
		if (line == NPOS)
		{
			line = 0;
			m_LnAttr->PrevBookmarkFromLine(line);
			line = LineIndex(line);
		}

		SetSelection(line, line);
		ScrollTo();
		// _hideblock.HorScrollCheck();
	}
}

void EdCnt::CmClearAllBookmarks()
{
	DEFTIMERNOTE(CmClearAllBookmarksTimer, NULL);
	if (!m_pDoc)
		return;
	// clear our bookmarks
	m_LnAttr->ClearAllBookmarks(this);
	g_VATabTree->RemoveAllBookmarks(FileName());
	long p1, p2;
	{
		TerNoScroll noscroll(this);
		GetSel(p1, p2);
		long line = 0, lstline, col;
		// clear all of theirs
		do
		{
			lstline = line;
			CTer::m_pDoc->SetBookMark(false);
			::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_GOTONEXTBKMK, 0);
			CTer::m_pDoc->CurPos(line, col);
		} while (lstline != line);
		SetSel(p1, p2);
	}
	CWnd::Invalidate(TRUE);
	UpdateWindow();
}

void EdCnt::OnLButtonDblClk(uint modKeys, CPoint pnt)
{
	DISABLECHECK();
	CTer::OnLButtonDblClk(modKeys, pnt);
}

// static CString IndentString(CString txt, int depth)
// {
// 	for (int i = 0; i < depth; ++i)
// 		txt = "  " + txt;
// 	return txt;
// }

static CStringW IndentStringW(CStringW txt, int depth)
{
	for (int i = 0; i < depth; ++i)
		//		txt = L"\u25B8 " + txt;
		txt = L"> " + txt;
	//	txt = FTM_DIM + txt + FTM_NORMAL;
	return txt;
}

static CStringW FormatFileAndLine(const DType& dt)
{
	CStringW txt;
	const CStringW filePath = dt.FilePath();
	if (!filePath.IsEmpty())
		CString__FormatW(txt, L"%s:%d", (LPCWSTR)::Basename(filePath), dt.Line());
	return txt;
}

WTString GetTrimmedParamListAndAdditionalCount(const WTString& paramList, int fileType)
{
	CommentSkipper cs(fileType);
	int i;
	int parens = 0;
	int angleBrackets = 0;
	int squareBrackets = 0;
	int additionalParams = 0;
	bool trimmed = false;
	for (i = 0; i < paramList.GetLength(); i++)
	{
		TCHAR c = paramList[i];
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
				continue;
			}
			if (c == '<')
			{
				angleBrackets++;
				continue;
			}
			if (c == '<')
			{
				angleBrackets--;
				continue;
			}
			if (c == '[')
			{
				squareBrackets++;
				continue;
			}
			if (c == ']')
			{
				squareBrackets--;
				continue;
			}

			if ((c == ',' && i > 60) || i > 75)
			{
				int length = paramList.GetLength();
				if (i > 75 && i != length - 1)
					trimmed = true;
				for (int j = i + 1; j < length - 2; j++)
				{
					TCHAR c2 = paramList[j];
					if (cs.IsCode(c2))
					{
						if (c2 == ',')
							break;
						if (c2 == '.' && paramList[j + 1] == '.' && paramList[j + 2] == '.')
						{ // ellipsis found
							WTString res = paramList;
							res.ReplaceAll(".", "|");
							return res;
						}
					}
				}

				// the next param is not an ellipsis. count the params
				cs.Reset();
				if (c == ',')
					additionalParams++;
				for (int j = i + 1; j < length - 2; j++)
				{
					TCHAR c2 = paramList[j];
					if (cs.IsCode(c2))
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
						if (c2 == '<')
						{
							angleBrackets++;
							continue;
						}
						if (c2 == '<')
						{
							angleBrackets--;
							continue;
						}
						if (c2 == '[')
						{
							squareBrackets++;
							continue;
						}
						if (c2 == ']')
						{
							squareBrackets--;
							continue;
						}

						if (c2 == ',')
							additionalParams++;
					}
				}
				i++; // we want to include the comma
				break;
			}
		}
	}

	WTString res;
	if (additionalParams)
	{
		WTString ellipsis;
		if (trimmed)
			ellipsis = "|||, ";
		else
			ellipsis = " ";
		res = paramList.Left(i) + ellipsis + FTM_DIM + itos(additionalParams) + " more" + FTM_NORMAL;
	}
	else
	{
		WTString ellipsis;
		if (trimmed)
			ellipsis = "|||";
		else
			ellipsis = "";
		res = paramList.Left(i) + ellipsis;
	}

	return res;
}

// [case: 7148] [case: 111049]
DType* StripScopeFindSym(MultiParse* m_pmparse, bool symTypeIsIncludedMember, WTString& symScopeToSearchFor,
                         const WTString& symToSearchFor, WTString& scopeStrippedForFind)
{
	WTString strippedScope;
	WTString scp(symScopeToSearchFor);
	for (;;)
	{
		// strip first scope off scopeToReplace
		int pos = scp.Find(DB_SEP_STR, 1);
		if (-1 == pos)
			break;

		// see if scope to remove is a namespace
		strippedScope += scp.Left(pos);
		DType* strippedDat = m_pmparse->FindExact(strippedScope);
		if (!strippedDat)
		{
			if (scopeStrippedForFind.IsEmpty())
				break;

			// [case: 93987]
			strippedDat = m_pmparse->FindExact(scopeStrippedForFind + strippedScope);
			if (!strippedDat)
				break;
		}

		if (NAMESPACE != strippedDat->type())
			break;

		// remove outer scope
		scopeStrippedForFind += strippedScope;
		scp = scp.Mid(pos);
		pos = scp.Find(DB_SEP_STR);
		if (-1 == pos || scp == (DB_SEP_STR + symToSearchFor))
		{
			if (symTypeIsIncludedMember)
			{
				// [case: 111049]
				// allow search of just the sym since the header was #included
				// into a namespace, so the actual decl may be at global scope.
				// for example, Gdiplus.
			}
			else
			{
				// prevent search of just the sym
				break;
			}
		}

		// search for truncated scope
		DType* truncDat = m_pmparse->FindSym(&scp, nullptr, nullptr, FDF_NoConcat | FDF_GotoDefIsOk | FDF_GUESS);
		if (truncDat)
		{
			symScopeToSearchFor = scp;
			return truncDat;
		}
	}

	return nullptr;
}

uint EdCnt::AddInheritanceMenuItem(WTString* methodName, WTString searchSymScope, bool overloadResolution,
                                   int gotoRelatedOverloadResolutionMode, DType* ptr, uint cnt,
                                   std::vector<DType>& savedItems, CMenu* menu, int depth, int insertPos /*= -1*/)
{
	CStringW txt;
	CStringW scopeStr;

	if (methodName)
	{
		CStringW className = CleanScopeForDisplay(StrGetSym(StrGetSymScope(searchSymScope))).Wide();
		className = FTM_BOLD + className + FTM_NORMAL;
		if (overloadResolution && gotoRelatedOverloadResolutionMode == Settings::GORM_HIDE)
		{
			txt = className + DB_SEP_STR.Wide() + methodName->Wide();
		}
		else
		{
			if (ptr)
			{
				WTString paramList;
				bool constFunc = false;
				OverloadResolver::GetParamListFromDef(ptr->Def(), *methodName, paramList, constFunc, m_ftype);
				if (Psettings->mGotoRelatedParameterTrimming)
				{
					WTString firstParamAndMore = GetTrimmedParamListAndAdditionalCount(paramList, m_ftype);
					txt = className + DB_SEP_STR.Wide() + methodName->Wide() + L'(' + firstParamAndMore.Wide() + L')';
				}
				else
				{
					txt = className + DB_SEP_STR.Wide() + methodName->Wide() + L'(' + paramList.Wide() + L')';
				}
			}
		}
		scopeStr = CleanScopeForDisplay(StrGetSymScope(StrGetSymScope(searchSymScope))).Wide();
	}
	else
	{
		CStringW symName = CleanScopeForDisplay(StrGetSym(searchSymScope)).Wide();
		symName = FTM_BOLD + symName + FTM_NORMAL;
		txt = symName;
		scopeStr = CleanScopeForDisplay(StrGetSymScope(searchSymScope)).Wide();
	}

	if (scopeStr[0] == DB_SEP_CHR)
		scopeStr = scopeStr.Mid(1);

	if (!scopeStr.IsEmpty())
	{
		scopeStr = FTM_DIM + CStringW(L"(") + scopeStr + CStringW(L")") + FTM_NORMAL;
		txt += L"   " + scopeStr;
	}

	bool hasFileLine = false;

	if (ptr)
	{
		CStringW fileLineStr = FormatFileAndLine(*ptr);
		if (!fileLineStr.IsEmpty())
		{
			hasFileLine = true;

			// work around so file extensions don't expand (foo.h:30 --> foo::h::30)
			fileLineStr.Replace(L".", L"|");
			fileLineStr.Replace(L":", L"$");
			fileLineStr = FTM_DIM + fileLineStr + FTM_NORMAL;
			txt += L"   " + fileLineStr;
		}
	}

	txt.Replace(L"...",
	            L"@"); // work around so ... in ellipses don't expand ("int param1, ..." -> "int param1, ::::::")
	txt.Replace(L"::", CStringW(DB_SEP_CHR));
	txt.Replace(CStringW(DB_SEP_CHR), L".");
	if (IsCFile(m_ftype))
		txt.Replace(L".", L"::");
	// work around so file extensions don't expand (foo.h:30 --> foo::h::30)
	txt.Replace(L"|", L".");
	txt.Replace(L"$", L":");
	txt.Replace(L"@", L"...");

	if (methodName)
	{
		if (ptr)
		{
			++cnt;
			if (hasFileLine)
				txt = ::BuildMenuTextHexAcceleratorW(cnt, txt);
			savedItems.push_back(ptr);
			if (insertPos != -1)
				::InsertMenuW(*menu, (UINT)insertPos, MF_BYPOSITION | MF_STRING | (hasFileLine ? 0u : MF_DISABLED),
				              savedItems.size(), txt);
			else
				::AppendMenuW(*menu, MF_STRING | (hasFileLine ? 0u : MF_DISABLED), savedItems.size(), txt);
		}
	}
	else
	{
		txt = ::IndentStringW(txt, depth);
		++cnt;
		if (hasFileLine)
			txt = ::BuildMenuTextHexAcceleratorW(cnt, txt);
		auto flags = MF_STRING | (hasFileLine ? 0u : MF_DISABLED);
		// 			if (!ptr)
		// 				flags |= MF_DISABLED | MF_GRAYED;
		savedItems.push_back(ptr);
		::AppendMenuW(*menu, flags, savedItems.size(), txt);
	}

	if (ptr)
	{
		auto iImage = GetTypeImgIdx(ptr->MaskedType(), ptr->Attributes());
		auto imgList = gImgListMgr->GetImgList(ImageListManager::bgMenu);
		if (imgList && *imgList)
		{
			CMenuXP::SetMenuItemImage(savedItems.size(), *imgList, iImage);
		}
	}
	return cnt;
}

void EdCnt::BuildInheritanceMenuItems(CMenu* menu, int symType, const WTString& scopeStr,
                                      std::vector<DType>& savedItems, int depth, uint& cnt, WTString* methodName,
                                      bool overloadResolution, Separator* separator /*= nullptr*/)
{
	MultiParsePtr mp(GetParseDb());
	if (!mp)
		return;

	DTypeList relationships;
	InheritanceDb::GetInheritanceRecords(symType, scopeStr, relationships);

	// limit recursion
	if (relationships.size() > 0 && depth >= 20)
	{
		CStringW txt = ::IndentStringW(L"...", depth);
		savedItems.push_back(DType());
		::AppendMenuW(*menu, MF_STRING | MF_DISABLED | MF_GRAYED, savedItems.size(), txt);
		return;
	}

	int gotoRelatedOverloadResolutionMode = Psettings->mGotoRelatedOverloadResolutionMode;
	for (auto rel : relationships)
	{
		auto clsSymScope = rel.Def();
		WTString searchSymScope = clsSymScope;
		if (methodName)
			searchSymScope += DB_SEP_STR + *methodName;

		DTypeList dtypes;
		DTypeList filteredItems;
		mp->FindExactList(searchSymScope, dtypes);
		dtypes.FilterNonActiveSystemDefs();
		dtypes.FilterDupesAndGotoDefs();
		dtypes.FilterEquivalentDefs();
		if (!overloadResolution)
			gotoRelatedOverloadResolutionMode = Settings::GORM_DISABLED;

		switch (gotoRelatedOverloadResolutionMode)
		{
		case Settings::GORM_HIDE:
		case Settings::GORM_USE_SEPARATOR: {
			for (auto dt : dtypes)
				filteredItems.push_back(dt);

			VirtualDefListGotoRelated list;
			list.SetList(&filteredItems);
			OverloadResolver resolver(list);
			resolver.Resolve(OverloadResolver::CALL_SITE);
			if (!list.IsEmpty())
			{ // overload found
				// ptr = &list.list->front();
				if (separator && separator->AddSeparator && separator->Unresolved)
				{
					::InsertMenu(*menu, 0, MF_SEPARATOR, 1000, "");
					separator->AddSeparator = false;
				}
				for (auto ptr2 : filteredItems)
				{
					// cnt = AddInheritanceMenuItem(methodName, searchSymScope, overloadResolution,
					// gotoRelatedOverloadResolutionMode, &ptr, cnt, savedItems, menu, depth, separator->Resolved);
					RecordedItem rec(methodName, searchSymScope, overloadResolution, gotoRelatedOverloadResolutionMode,
					                 ptr2, savedItems, menu, depth);
					separator->RecordedItemsBefore.push_back(rec);
					if (separator)
						separator->Resolved++;
				}
				if (gotoRelatedOverloadResolutionMode == Settings::GORM_USE_SEPARATOR)
				{
					for (auto it : dtypes)
					{
						if (std::find(filteredItems.begin(), filteredItems.end(), it) == filteredItems.end())
						{
							if (separator && separator->AddSeparator && separator->Resolved)
							{
								//::AppendMenu(*menu, MF_SEPARATOR, 1000, "");
								separator->AddSeparator = false;
							}
							// cnt = AddInheritanceMenuItem(methodName, searchSymScope, overloadResolution,
							// gotoRelatedOverloadResolutionMode, &it, cnt, savedItems, menu, depth);
							RecordedItem rec(methodName, searchSymScope, overloadResolution,
							                 gotoRelatedOverloadResolutionMode, it, savedItems, menu, depth);
							separator->RecordedItemsAfter.push_back(rec);
							if (separator)
								separator->Unresolved++;
						}
					}
				}
				break;
			}
			else
			{ // no overload found
				if (gotoRelatedOverloadResolutionMode == Settings::GORM_HIDE)
					goto hide;
			}
		}
		// fall-through
		case Settings::GORM_DISABLED: {
			if (methodName)
			{
				for (auto it : dtypes)
				{
					if (separator && separator->AddSeparator && separator->Resolved)
					{
						::AppendMenu(*menu, MF_SEPARATOR, 1000, "");
						separator->AddSeparator = false;
					}
					cnt = AddInheritanceMenuItem(methodName, searchSymScope, overloadResolution,
					                             gotoRelatedOverloadResolutionMode, &it, cnt, savedItems, menu, depth);
					if (separator)
						separator->Unresolved++;
				}
			}
			else
			{
				// [case: 104133]
				// for a class or type, there should be one entry in the dtype list, but check
				if (dtypes.size() > 0)
					cnt = AddInheritanceMenuItem(methodName, searchSymScope, overloadResolution,
					                             gotoRelatedOverloadResolutionMode, &dtypes.front(), cnt, savedItems,
					                             menu, depth);
			}
			break;
		}
		}
	hide:
		BuildInheritanceMenuItems(menu, symType, clsSymScope, savedItems, depth + 1, cnt, methodName,
		                          overloadResolution, separator);
	}
}

static void FindDeclsAndImpls(WTString symScope, MultiParse* mp, DTypeList& outDecls, DTypeList& outImpls)
{
	DTypeList dtypes;
	mp->FindExactList(symScope, dtypes);

	if (Psettings->mUnrealEngineCppSupport && IsCFile(mp->FileType()))
	{
		// [case: 141285] handle *_Implementation and *_Validate
		const bool isUe4Implmentation = symScope.EndsWith("_Implementation");
		const bool isUe4Validate = symScope.EndsWith("_Validate");

		if (!isUe4Implmentation && !isUe4Validate)
		{
			mp->FindExactList(symScope + "_Implementation", dtypes);
			mp->FindExactList(symScope + "_Validate", dtypes);
		}
		else if (isUe4Implmentation)
		{
			mp->FindExactList(symScope.Left(symScope.GetLength() - 15), dtypes);
			mp->FindExactList(symScope.Left(symScope.GetLength() - 15) + "_Validate", dtypes);
		}
		else if (isUe4Validate)
		{
			mp->FindExactList(symScope.Left(symScope.GetLength() - 9), dtypes);
			mp->FindExactList(symScope.Left(symScope.GetLength() - 9) + "_Implementation", dtypes);
		}
	}

	dtypes.FilterNonActive();

	const size_t kOriginalLen = dtypes.size();
	for (auto& dt : dtypes)
	{
		if (dt.IsDbLocal() && !dt.HasLocalScope())
		{
			// [case: 88096] don't ignore the only def
			if (kOriginalLen > 1)
				continue;
		}

		if (dt.IsImpl())
			outImpls.push_back(dt);
		else if (dt.MaskedType() == GOTODEF)
			outImpls.push_back(dt);
		else
			outDecls.push_back(dt);
	}

	mp->FindExactList(":ForwardDeclare" + symScope, outDecls);
	outDecls.FilterNonActive();

	auto fn = [](const DType& p1, const DType& p2) -> bool {
		if (p1.FileId() == p2.FileId())
			return p1.Line() < p2.Line();
		return Basename(p1.FilePath()).CompareNoCase(Basename(p2.FilePath())) < 0;
	};

	outDecls.FilterDupes();
	outDecls.FilterEquivalentDefsIfNoFilePath(); // remove dupes of syms we can't go to
	outDecls.sort(fn);
	outImpls.FilterDupes();
	outImpls.FilterEquivalentDefsIfNoFilePath(); // remove dupes of syms we can't go to
	outImpls.sort(fn);
}

void ReplaceParamsWithTrimmedList(CStringW def, CStringW sym, CStringW& txt, int fileType)
{
	WTString paramList;
	bool constFunc = false;
	OverloadResolver::GetParamListFromDef(def, sym, paramList, constFunc, fileType);
	int from = txt.Find('(');
	if (from != -1)
	{
		int to = txt.ReverseFind(')');
		if (to != -1)
		{
			WTString trimmedParamListAndMore = GetTrimmedParamListAndAdditionalCount(paramList, fileType);
			trimmedParamListAndMore.ReplaceAll("|", ".");
			txt.Delete(from + 1, (to - from) - 1);
			txt.Insert(from + 1, trimmedParamListAndMore.Wide());
		}
	}
}

uint EdCnt::BuildDefMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu, bool allowNoPath /*= false*/)
{
	const CStringW filePath = dt.FilePath();
	if (filePath.IsEmpty() && !allowNoPath)
	{
		// can't goto
	}
	else
	{
		token2 tk(DecodeTemplates(dt.Def()));
		CStringW def = tk.read2("\f").Wide(); // first def only
		CStringW sym = dt.Sym().Wide();
		CStringW boldSym = FTM_BOLD + sym + FTM_NORMAL;
		ReplaceWholeWord(def, sym, boldSym);

		CStringW txt = def;
		if (Psettings->mGotoRelatedParameterTrimming)
			ReplaceParamsWithTrimmedList(def, sym, txt, m_ftype);

		CStringW dimTxt = FormatFileAndLine(dt);
		if (!dimTxt.IsEmpty())
		{
			dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
			txt += L"   " + dimTxt;
		}

		txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);
		items.push_back(dt);
		::AppendMenuW(*menu, MF_STRING, items.size(), txt);

		auto iImage = GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
		auto safeHandle = gImgListMgr->GetImgList(ImageListManager::bgMenu)->GetSafeHandle();
		if (safeHandle)
			CMenuXP::SetMenuItemImage(items.size(), safeHandle, iImage);
	}

	return cnt;
}

uint EdCnt::BuildHashtagMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu,
                                 bool allowNoPath /*= false*/)
{
	const CStringW filePath = dt.FilePath();
	if (filePath.IsEmpty() && !allowNoPath)
	{
		// can't goto
	}
	else
	{
		CStringW txt = FormatFileAndLine(dt);

		token2 tk(dt.Def());
		CStringW def = tk.read2("\f").Wide(); // first def only
		def.Trim();

		if (!def.IsEmpty())
		{
			CStringW defStr;
			CString__FormatW(defStr, L"   %c%s", FTM_DIM, (LPCWSTR)def);
			if (defStr.GetLength() > 80)
			{
				defStr = defStr.Left(80);
				defStr += L"...";
			}
			txt += defStr;
		}

		txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);
		items.push_back(dt);
		::AppendMenuW(*menu, MF_STRING, items.size(), txt);

		auto iImage = GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
		auto safeHandle = gImgListMgr->GetImgList(ImageListManager::bgMenu)->GetSafeHandle();
		if (safeHandle)
			CMenuXP::SetMenuItemImage(items.size(), safeHandle, iImage);
	}

	return cnt;
}

uint EdCnt::BuildCtorMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu, bool allowNoPath /*= false*/)
{
	const CStringW filePath = dt.FilePath();
	if (filePath.IsEmpty() && !allowNoPath)
	{
		// can't goto
	}
	else
	{
		// replace Foo::Foo() in def with just Foo()
		WTString dtSym(dt.Sym());
		WTString scopedCtor = dtSym + "::" + dtSym;
		auto def = dt.Def();
		def.ReplaceAll(scopedCtor, dtSym);

		token2 tk(DecodeTemplates(def));
		CStringW defW = tk.read2("\f").Wide(); // first def only

		// strip initializer lists
		int closeParen = defW.Find(')');
		if (closeParen >= 0)
		{
			defW = defW.Left(closeParen + 1);
			if (dt.IsImpl())
				defW += "{...}";
		}

		// bold the arguments, not the sym name
		int openParen = defW.Find('(');
		if (openParen >= 0 && openParen < closeParen)
		{
			CStringW defTmp = defW.Left(openParen + 1);
			CStringW args = FTM_BOLD + defW.Mid(openParen + 1, closeParen - openParen - 1) + FTM_NORMAL;
			args.Replace(L",", FTM_NORMAL + CStringW(L",") + FTM_BOLD);
			defTmp += args;
			defTmp += defW.Mid(closeParen);
			defW = defTmp;
		}

		CStringW txt = defW;

		if (Psettings->mGotoRelatedParameterTrimming)
			ReplaceParamsWithTrimmedList(defW, dtSym.Wide(), txt, m_ftype);

		CStringW dimTxt = FormatFileAndLine(dt);
		if (!dimTxt.IsEmpty())
		{
			dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
			txt += L"   " + dimTxt;
		}

		txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);
		items.push_back(dt);
		::AppendMenuW(*menu, MF_STRING, items.size(), txt);

		auto iImage = GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
		auto safeHandle = gImgListMgr->GetImgList(ImageListManager::bgMenu)->GetSafeHandle();
		if (safeHandle)
			CMenuXP::SetMenuItemImage(items.size(), safeHandle, iImage);
	}

	return cnt;
}

void EdCnt::BuildDeclsAndDefsMenuItems(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu)
{
	DTypeList decls;
	DTypeList impls;
	MultiParsePtr mp(GetParseDb());
	FindDeclsAndImpls(dtype->SymScope(), mp.get(), decls, impls);

	auto fileId = gFileIdManager->GetFileId(FileName());
	auto line1 = TERROW(CurPos());
	auto line2 = CurLine() + 1;

	// strip out the DType at the cursor
	auto remDt = [fileId, line1, line2](const DType& x) {
		return x.FileId() == fileId && (x.Line() == (int)line1 || x.Line() == line2);
	};
	decls.erase(std::remove_if(std::begin(decls), std::end(decls), remDt), std::end(decls));
	impls.erase(std::remove_if(std::begin(impls), std::end(impls), remDt), std::end(impls));
	uint declSeparator = (uint)-1;
	uint implSeparator = (uint)-1;

	// resolving overloads
	int gotoRelatedOverloadResolutionMode = Psettings->mGotoRelatedOverloadResolutionMode;
	int noFilePathCounter = 0;
	int filePathCounter = 0;
	if (gotoRelatedOverloadResolutionMode != Settings::GORM_DISABLED && dtype->MaskedType() == FUNC)
	{
		DTypeList unifiedList;
		for (auto dt : impls)
		{
			if (!dt.FilePath().IsEmpty())
			{ // BuildDefMenuItem() will skip items without FilePath. it needs to be excluded from OverLoad resolution
			  // for correct results
				unifiedList.push_back(dt);
				filePathCounter++;
			}
			else
			{
				noFilePathCounter++;
			}
		}
		for (auto dt : decls)
		{
			if (!dt.FilePath().IsEmpty())
			{ // BuildDefMenuItem() will skip items without FilePath. it needs to be excluded from OverLoad resolution
			  // for correct results
				unifiedList.push_back(dt);
				filePathCounter++;
			}
			else
			{
				noFilePathCounter++;
			}
		}

		if (noFilePathCounter && filePathCounter == 0 && (gTypingDevLang == CS || gTypingDevLang == VB))
		{
			DType emptyType;
			items.push_back(emptyType);

			CStringW txt = CStringW("De&finition");
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
			return;
		}

		VirtualDefListGotoRelated list;
		list.SetList(&unifiedList);
		OverloadResolver resolver(list);
		resolver.Resolve(OverloadResolver::CALL_SITE);
		auto removedOverload = [&unifiedList](const DType& x) {
			return std::find(unifiedList.begin(), unifiedList.end(), x) == unifiedList.end();
		};

		if (!unifiedList.empty())
		{
			switch (gotoRelatedOverloadResolutionMode)
			{
			case Settings::GORM_HIDE: {
				decls.erase(std::remove_if(std::begin(decls), std::end(decls), removedOverload), std::end(decls));
				impls.erase(std::remove_if(std::begin(impls), std::end(impls), removedOverload), std::end(impls));
			}
			break;
			case Settings::GORM_USE_SEPARATOR: {
				declSeparator = 0;
				DTypeList matching;
				for (auto i = decls.begin(); i != decls.end();)
				{
					if (!removedOverload(*i))
					{
						matching.push_front(*i);
						i = decls.erase(i);
						declSeparator++;
					}
					else
					{
						i++;
					}
				}
				for (auto dt : matching)
					decls.push_front(dt);

				implSeparator = 0;
				matching.clear();
				for (auto i = impls.begin(); i != impls.end();)
				{
					if (!removedOverload(*i))
					{
						matching.push_front(*i);
						i = impls.erase(i);
						implSeparator++;
					}
					else
					{
						i++;
					}
				}
				for (auto dt : matching)
					impls.push_front(dt);
			}
			break;
			}
		}
	}

	if (impls.empty())
	{
		// 		items.push_back(DType());
		// 		xpmenu.AddMenuItem(items.size(), MF_STRING | MF_GRAYED | MF_DISABLED, "De&finition");
	}
	else if (impls.size() == 1)
	{
		CStringW filePath = impls.front().FilePath();
		if (!filePath.IsEmpty())
		{
			items.push_back(impls.front());

			CStringW txt;
			const bool isUe4Implmentation = impls.front().SymScope().EndsWith("_Implementation");
			const bool isUe4Validate = impls.front().SymScope().EndsWith("_Validate");
			if (Psettings->mUnrealEngineCppSupport && (isUe4Implmentation || isUe4Validate))
			{
				// [case: 141285] display the name of the *_Implementation or *_Validate method
				txt = impls.front().Sym().Wide();
			}
			else
			{
				txt = CStringW("De&finition");
			}

			CStringW dimTxt = FormatFileAndLine(impls.front());
			if (!dimTxt.IsEmpty())
			{
				dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
				txt += L"   " + dimTxt;
			}
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
		}
	}
	else
	{
		CMenu* menu = new CMenu;
		menu->CreateMenu();

		// note: should only be one definition? overloads?
		uint cnt = 0;
		bool addSep = false;
		for (auto dt : impls)
		{
			if (addSep)
			{
				menu->AppendMenu(MF_SEPARATOR);
				addSep = false;
			}
			cnt = BuildDefMenuItem(dt, cnt, items, menu);
			if (cnt == implSeparator)
				addSep = true;
		}

		if (cnt)
			xpmenu.AppendPopup("De&finitions", menu);
		else
			delete menu;
	}

	if (decls.empty())
	{
		// 		items.push_back(DType());
		// 		xpmenu.AddMenuItem(items.size(), MF_STRING | MF_GRAYED | MF_DISABLED, "De&claration");
	}
	else if (decls.size() == 1)
	{
		CStringW filePath = decls.front().FilePath();
		if (!filePath.IsEmpty())
		{
			items.push_back(decls.front());
			CStringW txt = CStringW("De&claration");
			CStringW dimTxt = FormatFileAndLine(decls.front());
			if (!dimTxt.IsEmpty())
			{
				dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
				txt += L"   " + dimTxt;
			}
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
		}
	}
	else
	{
		CMenu* menu = new CMenu;
		menu->CreateMenu();

		uint cnt = 0;
		int addSep = 0;
		for (auto dt : decls)
		{
			if (addSep == 1)
			{
				menu->AppendMenu(MF_SEPARATOR);
				addSep++; // we already added our separator so no need to add it anymore
			}
			cnt = BuildDefMenuItem(dt, cnt, items, menu);
			if (cnt == declSeparator)
				addSep++; // only add one separator - the condition can be met more than once
				          // (TestResolvingOverloadsGotoCS03 would fail without this change)
		}

		if (cnt)
			xpmenu.AppendPopup("De&clarations", menu);
		else
			delete menu;
	}
}

void EdCnt::BuildHashtagUsageAndRefsMenuItems(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu)
{
	DTypeList dtypes;
	{
		TempAssign<int> ta(g_doDBCase, 0);
		MultiParsePtr mp(GetParseDb());
		mp->FindExactList(dtype->SymScope(), dtypes);
	}
	dtypes.FilterNonActiveSystemDefs();

	auto fn = [](const DType& p1, const DType& p2) -> bool {
		if (p1.FileId() == p2.FileId())
			return p1.Line() < p2.Line();
		return Basename(p1.FilePath()).CompareNoCase(Basename(p2.FilePath())) < 0;
	};

	dtypes.FilterDupes();
	dtypes.FilterEquivalentDefsIfNoFilePath(); // remove dupes of syms we can't go to
	dtypes.sort(fn);

	DTypeList usages;
	DTypeList refs;
	for (auto& dt : dtypes)
	{
		if (dt.IsHashtagRef())
			refs.push_back(dt);
		else
			usages.push_back(dt);
	}

	auto fileId = gFileIdManager->GetFileId(FileName());
	auto line1 = TERROW(CurPos());
	auto line2 = CurLine() + 1;

	// strip out the DType at the cursor
	auto remDt = [fileId, line1, line2](const DType& x) {
		return x.FileId() == fileId && (x.Line() == (int)line1 || x.Line() == line2);
	};
	usages.erase(std::remove_if(std::begin(usages), std::end(usages), remDt), std::end(usages));
	refs.erase(std::remove_if(std::begin(refs), std::end(refs), remDt), std::end(refs));

	if (usages.empty())
	{
		// 		items.push_back(DType());
		// 		xpmenu.AddMenuItem(items.size(), MF_STRING | MF_GRAYED | MF_DISABLED, "&References");
	}
	else if (usages.size() == 1)
	{
		CStringW filePath = usages.front().FilePath();
		if (!filePath.IsEmpty())
		{
			items.push_back(usages.front());
			CStringW txt = CStringW("&Reference");
			CStringW dimTxt = FormatFileAndLine(usages.front());
			if (!dimTxt.IsEmpty())
			{
				dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
				txt += L"   " + dimTxt;
			}
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
		}
	}
	else
	{
		CMenu* menu = new CMenu;
		menu->CreateMenu();

		uint cnt = 0;
		for (auto dt : usages)
			cnt = BuildHashtagMenuItem(dt, cnt, items, menu);

		if (cnt)
			xpmenu.AppendPopup("&References", menu);
		else
			delete menu;
	}

	if (refs.empty())
	{
		// 		items.push_back(DType());
		// 		xpmenu.AddMenuItem(items.size(), MF_STRING | MF_GRAYED | MF_DISABLED, "&Cross-references");
	}
	else if (refs.size() == 1)
	{
		CStringW filePath = refs.front().FilePath();
		if (!filePath.IsEmpty())
		{
			items.push_back(refs.front());
			CStringW txt = CStringW("&Cross-reference");
			CStringW dimTxt = FormatFileAndLine(refs.front());
			if (!dimTxt.IsEmpty())
			{
				dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
				txt += L"   " + dimTxt;
			}
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
		}
	}
	else
	{
		CMenu* menu = new CMenu;
		menu->CreateMenu();

		uint cnt = 0;
		for (auto dt : refs)
			cnt = BuildHashtagMenuItem(dt, cnt, items, menu);

		if (cnt)
			xpmenu.AppendPopup("&Cross-references", menu);
		else
			delete menu;
	}
}

void EdCnt::BuildCtorsMenuItem(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu)
{
	if (!dtype->IsType())
		return;

	// strip template specialization
	WTString sym = dtype->Sym();
	int startTemplate = sym.Find(EncodeChar('<'));
	if (startTemplate >= 0)
		sym = sym.Left(startTemplate);

	WTString ctorSymScope = dtype->Scope() + DB_SEP_STR + sym + DB_SEP_STR + sym;
	if (ctorSymScope[0] != DB_SEP_CHR)
		ctorSymScope.prepend(DB_SEP_STR.c_str());

	DTypeList decls;
	DTypeList impls;
	MultiParsePtr mp(GetParseDb());
	FindDeclsAndImpls(ctorSymScope, mp.get(), decls, impls);

	// combine
	for (auto& dt : decls)
		impls.push_back(dt);

	if (impls.empty())
	{
		// 		items.push_back(DType());
		// 		xpmenu.AddMenuItem(items.size(), MF_STRING | MF_GRAYED | MF_DISABLED, "Const&ructor");
	}
	else if (impls.size() == 1)
	{
		CStringW filePath = impls.front().FilePath();
		if (!filePath.IsEmpty())
		{
			items.push_back(impls.front());
			CStringW txt = CStringW("Const&ructor");
			CStringW dimTxt = FormatFileAndLine(impls.front());
			if (!dimTxt.IsEmpty())
			{
				dimTxt = FTM_DIM + dimTxt + FTM_NORMAL;
				txt += L"   " + dimTxt;
			}
			xpmenu.AddMenuItemW(items.size(), MF_STRING, txt);
		}
	}
	else
	{
		DTypeList unifiedList;
		for (auto dt : impls)
		{
			if (!dt.FilePath().IsEmpty()) // BuildCtorMenuItem() will skip items without FilePath. it needs to be
			                              // excluded from OverLoad resolution for correct results
				unifiedList.push_back(dt);
		}

		uint implSeparator = (uint)-1;
		VirtualDefListGotoRelated list;
		list.SetList(&unifiedList);
		OverloadResolver resolver(list);
		resolver.Resolve(OverloadResolver::CONSTRUCTOR);
		auto removedOverload = [&unifiedList](const DType& x) {
			return std::find(unifiedList.begin(), unifiedList.end(), x) == unifiedList.end();
		};
		int gotoRelatedOverloadResolutionMode = Psettings->mGotoRelatedOverloadResolutionMode;
		if (!unifiedList.empty())
		{
			switch (gotoRelatedOverloadResolutionMode)
			{
			case Settings::GORM_HIDE: {
				impls.erase(std::remove_if(std::begin(impls), std::end(impls), removedOverload), std::end(impls));
			}
			break;
			case Settings::GORM_USE_SEPARATOR: {
				implSeparator = 0;
				DTypeList matching;
				for (auto i = impls.begin(); i != impls.end();)
				{
					if (!removedOverload(*i))
					{
						matching.push_front(*i);
						i = impls.erase(i);
						implSeparator++;
					}
					else
					{
						i++;
					}
				}
				for (auto dt : matching)
					impls.push_front(dt);
			}
			break;
			}
		}

		CMenu* menu = new CMenu;
		menu->CreateMenu();

		uint cnt = 0;
		bool addSep = false;
		for (auto dt : impls)
		{
			if (addSep)
			{
				menu->AppendMenu(MF_SEPARATOR);
				addSep = false;
			}
			cnt = BuildCtorMenuItem(dt, cnt, items, menu, true);
			if (cnt == implSeparator)
				addSep = true;
		}

		if (cnt)
			xpmenu.AppendPopup("Const&ructors", menu);
		else
			delete menu;
	}
}

void EdCnt::BuildIncludeMenuItems(CMenu* menu, CStringW file, bool includedBy, std::vector<DType>& savedItems,
                                  int depth, uint& cnt)
{
	DTypeList incList;
	if (includedBy)
	{
		IncludesDb::GetIncludedBys(file, DTypeDbScope::dbSlnAndSys, incList);

		auto fn = [](const DType& p1, const DType& p2) -> bool {
			if (p1.FileId() == p2.FileId())
				return false;
			return Basename(p1.FilePath()).CompareNoCase(Basename(p2.FilePath())) < 0;
		};
		incList.sort(fn);
		auto unq = [](const DType& p1, const DType& p2) -> bool { return (p1.FileId() == p2.FileId()); };
		incList.unique(unq);
	}
	else
	{
		IncludesDb::GetIncludes(file, DTypeDbScope::dbSlnAndSys, incList);

		auto fn = [](DType& p1, DType& p2) -> bool {
			const WTString p1Def(p1.Def());
			CStringW f1 = gFileIdManager->GetFile(p1Def);
			CStringW b1;
			if (f1.IsEmpty() && p1Def.contains("(unresolved)"))
				b1 = p1Def.Wide();
			else
				b1 = Basename(f1);

			const WTString p2Def(p2.Def());
			CStringW f2 = gFileIdManager->GetFile(p2Def);
			CStringW b2;
			if (f2.IsEmpty() && p2Def.contains("(unresolved)"))
				b2 = p2Def.Wide();
			else
				b2 = Basename(f2);

			return b1.CompareNoCase(b2) < 0;
		};
		incList.sort(fn);

		auto unq = [](DType& p1, DType& p2) -> bool { return (p1.Def() == p2.Def()); };
		incList.unique(unq);
	}

	incList.FilterNonActive();

	if (incList.size())
	{
		if (depth >= 5)
		{
			CStringW txt = ::IndentStringW(L"...", depth);
			txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);
			savedItems.push_back(DType());
			::AppendMenuW(*menu, MF_STRING | MF_GRAYED | MF_DISABLED, savedItems.size(), txt);
		}
		else
		{
			for (auto& inc : incList)
			{
				CStringW incFile;
				if (includedBy)
					incFile = inc.FilePath();
				else
				{
					const WTString incDef(inc.Def());
					incFile = gFileIdManager->GetFile(incDef);
					if (incFile.IsEmpty() && incDef.contains("(unresolved)"))
						incFile = incDef.Wide();
				}

				const CStringW basename = ::Basename(incFile);
				const CStringW dirname = ::Path(incFile);
				CStringW txt = basename + L"   " + FTM_DIM + dirname + FTM_NORMAL;
				txt = ::IndentStringW(txt, depth);
				txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);

				MultiParsePtr mp(GetParseDb());
				auto incFileDt = mp->GetFileData(incFile);
				if (incFileDt)
				{
					savedItems.push_back(*incFileDt);
					// 					menu->AppendMenu(MF_STRING, savedItems.size(), txt);
					::AppendMenuW(*menu, MF_STRING, savedItems.size(), txt);
				}
				else
				{
					savedItems.push_back(DType());
					// 					menu->AppendMenu(MF_STRING | MF_GRAYED | MF_DISABLED, savedItems.size(), txt);
					::AppendMenuW(*menu, MF_STRING | MF_GRAYED | MF_DISABLED, savedItems.size(), txt);
				}

				// No recursion for now...
				// BuildIncludeMenuItems(menu, incFile, includedBy, savedItems, depth + 1, cnt);
			}
		}
	}
}

void GetTemplateArgs(WTString sym, std::vector<WTString>& args)
{
	char tstart = EncodeChar('<');
	char tend = EncodeChar('>');
	char comma = EncodeChar(',');
	int istart = 0;

	int depth = 0;
	for (int i = 0; i < sym.length(); ++i)
	{
		if (sym[i] == tstart)
		{
			if (!depth)
				istart = i;
			++depth;
		}
		else if (sym[i] == comma)
		{
			if (depth == 1)
			{
				WTString arg = sym.Mid(istart + 1, i - istart - 1);
				args.push_back(arg);
				istart = i;
			}
		}
		else if (sym[i] == tend)
		{
			if (depth > 0)
			{
				--depth;
				if (!depth)
				{
					WTString arg = sym.Mid(istart + 1, i - istart - 1);
					args.push_back(arg);
					break;
				}
			}
			else
			{
				// bad
			}
		}
	}
}

void EdCnt::GetRelatedTypes(const DType* dtype, DTypeList& dtypes, DType* scopeType)
{
	MultiParsePtr mp(GetParseDb());
	switch (dtype->MaskedType())
	{
	case TYPE:
	case VAR: {
		WTString types = ::GetTypesFromDef(dtype, (int)dtype->type(), m_ScopeLangType);
		token2 tk(types);
		for (WTString type = tk.read2("\f"); !type.IsEmpty(); type = tk.read2("\f"))
		{
			DType* foundVarType = ResolveTypeStr(type, scopeType, mp.get());
			if (foundVarType)
				dtypes.push_back(*foundVarType);
		}
		dtypes.FilterDupesAndGotoDefs();
		dtypes.FilterNonActive();
		break;
	}

	case CLASS:
	case STRUCT:
	case C_INTERFACE: {
		WTString sym = dtype->Sym();
		std::vector<WTString> templateArgs;
		GetTemplateArgs(sym, templateArgs);
		if (!templateArgs.empty())
		{
			for (auto& type : templateArgs)
			{
				type = DecodeTemplates(type);
				if (type.Find('(') >= 0)
				{
					// Function ptr decl in a template arg?
				}
				else
				{
					// reduce to type, removing ptrs and const, but not from within template args

					const char* kPlaceholder = "___oOoOoOoOo___";
					WTString replaceStr;

					int start = type.Find('<');
					int end = type.ReverseFind('>');
					if (start >= 0 && end >= 0 && end > start)
					{
						WTString startStr = type.Left(start);
						WTString endStr = type.Mid(end + 1);
						replaceStr = type.Mid(start, end - start + 1);
						type = startStr + kPlaceholder + endStr;
					}

					type.ReplaceAll("*", "");
					type.ReplaceAll("^", "");
					type.ReplaceAll("const", "", TRUE);
					type.ReplaceAll("constexpr", "", TRUE);
					type.ReplaceAll("consteval", "", TRUE);
					type.ReplaceAll("constinit", "", TRUE);
					type.ReplaceAll("_CONSTEXPR17", "", TRUE);
					type.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
					type.ReplaceAll("_CONSTEXPR20", "", TRUE);
					type.ReplaceAll("class", "", TRUE);
					type.ReplaceAll("struct", "", TRUE);
					type.ReplaceAll("interface", "", TRUE);
					type.ReplaceAll(kPlaceholder, replaceStr);
				}
				type.Trim();
				EncodeTemplates(type);

				DType* foundVarType = ResolveTypeStr(type, scopeType, mp.get());
				if (foundVarType)
					dtypes.push_back(*foundVarType);
			}
		}
	}
	break;

	default:
		break;
	}
}

void EdCnt::DoBuildTypeOfSymMenuItems(DTypeList& dtypes, std::vector<DType>& items, CMenu* typesMenu, int depth,
                                      uint& cnt, DType* scopeType, DType* outVarType)
{
	if (depth > 10)
		return;

	for (const auto& dt : dtypes)
	{
		if (!dt.IsType())
			continue;

		if (outVarType && outVarType->IsEmpty() && dt.IsType() && dt.MaskedType() != TYPE)
			*outVarType = dt;

		items.push_back(dt);
		WTString scopeStr = dt.Scope();
		WTString sym = dt.Sym();
		WTString shortsym = StripEncodedTemplates(sym);
		sym.ReplaceAll(shortsym, WTString(FTM_BOLD) + shortsym + WTString(FTM_NORMAL), TRUE);
		CStringW txt = CleanScopeForDisplay(sym).Wide();

		if (scopeStr[0] == DB_SEP_CHR)
			scopeStr = scopeStr.Mid(1);

		if (!scopeStr.IsEmpty())
		{
			scopeStr = FTM_DIM + ("(" + std::move(scopeStr)) + ")" + FTM_NORMAL;
			txt += L"   " + scopeStr.Wide();
		}

		txt.Replace(L"::", CStringW(DB_SEP_CHR));
		txt.Replace(CStringW(DB_SEP_CHR), L".");
		if (IsCFile(m_ftype))
			txt.Replace(L".", L"::");

		CStringW fileLineStr = FormatFileAndLine(dt);
		if (!fileLineStr.IsEmpty())
		{
			fileLineStr = FTM_DIM + fileLineStr + FTM_NORMAL;
			txt += L"   " + fileLineStr;
			txt = ::BuildMenuTextHexAcceleratorW(++cnt, txt);
		}

		txt = IndentStringW(txt, depth);
		::AppendMenuW(*typesMenu, MF_STRING, items.size(), txt);

		auto iImage = GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
		auto safeHandle = gImgListMgr->GetImgList(ImageListManager::bgMenu)->GetSafeHandle();
		if (safeHandle)
			CMenuXP::SetMenuItemImage(items.size(), safeHandle, iImage);

		if (gTestLogger && gTestLogger->IsMenuLoggingEnabled())
		{
			// log the type
			CStringW logTxt;
			CString__FormatW(logTxt, L"Symbol type info: %s %s %d", (LPCWSTR)DecodeTemplates(dt.Sym()).Wide(),
			                 (LPCWSTR)::Basename(dt.FilePath()), dt.Line());
			gTestLogger->LogStrW(logTxt);
		}

		// only expand top-level typedefs
		if (dt.MaskedType() != TYPE || !depth)
		{
			DTypeList subDtypes;
			GetRelatedTypes(&dt, subDtypes, scopeType);
			DoBuildTypeOfSymMenuItems(subDtypes, items, typesMenu, depth + 1, cnt, scopeType, outVarType);
		}
	}
}

void EdCnt::BuildTypeOfSymMenuItems(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu, DType* outVarType)
{
	DTypeList dtypes;
	GetRelatedTypes(dtype, dtypes, dtype);

	if (outVarType && outVarType->IsEmpty())
	{
		DType* membersOfType = nullptr;
		if (dtype->MaskedType() == TYPE || dtype->MaskedType() == VAR)
		{
			if (dtypes.size())
				membersOfType = &dtypes.front();
		}
		else
		{
			membersOfType = dtype;
		}

		if (membersOfType && membersOfType->IsType() && membersOfType->MaskedType() != TYPE)
			*outVarType = *membersOfType;
	}

	if (dtypes.size() == 0)
	{
	}
	else if (dtype->MaskedType() != TYPE && dtypes.size() == 1 && dtypes.front().MaskedType() != TYPE &&
	         (dtypes.front().Sym().Find(EncodeChar('<')) < 0))
	{
		DType* foundVarType = &dtypes.front();

		if (!foundVarType->FilePath().IsEmpty())
		{
			items.push_back(*foundVarType);
			xpmenu.AddMenuItem(items.size(), MF_STRING, "&Type of Symbol");

			if (gTestLogger && gTestLogger->IsMenuLoggingEnabled())
			{
				// log the type
				CStringW txt;
				CString__FormatW(txt, L"Symbol type info: %s %s %d", (LPCWSTR)foundVarType->Sym().Wide(),
				                 (LPCWSTR)::Basename(foundVarType->FilePath()), foundVarType->Line());
				gTestLogger->LogStrW(txt);
			}
		}
	}
	else
	{
		CMenu* typesMenu = new CMenu();
		typesMenu->CreateMenu();
		uint cnt = 0;
		DoBuildTypeOfSymMenuItems(dtypes, items, typesMenu, 0, cnt, dtype, outVarType);
		if (cnt)
		{
			const char* label = (dtype->MaskedType() == VAR) ? "&Types of Symbol" : "Related &Types";
			xpmenu.AppendPopup(label, typesMenu);
		}
		else
			delete typesMenu;
	}
}

void EdCnt::BuildMembersOfMenuItem(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu,
                                   bool displayName /*= false*/)
{
	if (dtype)
	{
		switch (dtype->MaskedType())
		{
		case CLASS:
		case STRUCT:
		case C_INTERFACE:
		case NAMESPACE:
		case C_ENUM: {
			items.push_back(*dtype);
			size_t idx = items.size();

			WTString txt;
			if (displayName)
			{
				WTString symDisplayName = CleanScopeForDisplay(dtype->Sym());
				if (dtype->MaskedType() == C_ENUM)
				{
					int pos = symDisplayName.Find("unnamed_enum_");
					if (!pos)
						symDisplayName = "unnamed enum";
				}
				txt.WTFormat("Goto &Member of %s...", symDisplayName.c_str());
			}
			else
				txt = "Goto &Member...";
			xpmenu.AddMenuItem(1000u + idx, MF_BYCOMMAND | MF_STRING, txt, 0);
		}
		break;
		}
	}
}

void EdCnt::BuildTitleMenuItem(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu)
{
	// 	items.push_back(DType());
	//
	// 	CString txt = CleanScopeForDisplay(dtype->SymScope());
	// 	if (txt[0] == DB_SEP_CHR)
	// 		txt = txt.Mid(1);
	// 	txt.Replace("::", DB_SEP_STR);
	// 	txt.Replace(DB_SEP_STR, ".");
	// 	if (IsCFile(m_ftype))
	// 		txt.Replace(".", "::");
	//
	// 	xpmenu.AddMenuItem(items.size(), MF_STRING | MF_DISABLED, txt);
}

bool EdCnt::GetSuperGotoSym(DType& outDtype, bool& isCursorInWs)
{
	isCursorInWs = false;

	CurScopeWord();

	bool isPreProcLine = (m_lastScope == DB_SCOPE_PREPROC);

	if (!isPreProcLine && m_lastScope == DB_SEP_STR)
	{
		// [case: 77331]
		// when caret is at column 0, before #include status info is incorrect (different
		// than after the # char) because m_lastScope does not get get to DB_SCOPE_PREPROC until
		// after the # char.  Compensate here.
		const WTString bb(GetBufConst());
		int pos = GetBufIndex(bb, (long)CurPos());
		if (pos > 0 && pos < bb.GetLength() && bb[pos] == '#')
			isPreProcLine = true;
	}

	if (isPreProcLine)
	{
		token2 lineTok = GetSubString(LinePos(), LinePos(CurLine() + 1));
		if (lineTok.contains("include") || lineTok.contains("import"))
		{
			DTypeList incList;
			IncludesDb::GetIncludes(FileName(), DTypeDbScope::dbSolution, incList);
			for (auto& dt : incList)
			{
				int incLine = dt.Line();
				if (incLine == CurLine())
				{
					outDtype = dt;
					return true;
				}
			}
		}
		return false;
	}

	const DType dd(GetSymDtype());
	if (GetSymDef().IsEmpty() || GetSymDef() == " " ||
	    dd.IsIncludedMember()) // So we don't just goto the #include line. case=9117
	{
		WTString curSym(CurWord());
		if (!curSym.IsEmpty() && Psettings->mHashtagsAllowHypens && (GetSymScope() == "String"))
		{
			// [case: 109520]
			WTString altSym(CurWord(curSym == "-" ? -1 : (curSym == "#" ? 1 : 0), true));
			if (altSym[0] == '#')
				curSym = altSym;
		}
		curSym.Trim();

		// are we on a csym?
		for (int n = 0; n < curSym.GetLength(); ++n)
		{
			const bool startOfHashtag = (n == 0 && curSym[n] == '#');
			if (!startOfHashtag && !ISCSYM(curSym[n]))
			{
				if (curSym[n] == '-' && Psettings->mHashtagsAllowHypens)
					continue;

				curSym.Empty();
				break;
			}
		}

		MultiParsePtr mp(GetParseDb());
		if (curSym.IsEmpty())
		{
			// return cur class scope, if possible
			WTString ctx = m_lastScope;
			while (ctx.GetLength())
			{
				auto pDt = mp->FindExact(ctx);
				if (pDt && pDt->IsType() && pDt->MaskedType() != NAMESPACE)
				{
					outDtype = *pDt;
					isCursorInWs = true;
					return true;
				}
				ctx = StrGetSymScope(ctx);
			}
		}
		else
		{
			// support for alt+g on words in comments/strings
			auto symScope = DB_SEP_STR + curSym;
			auto pDt = mp->FindAnySym(symScope);
			if (pDt)
			{
				if (IsCFile(m_ScopeLangType) && dd.IsIncludedMember() && pDt->IsIncludedMember() &&
				    !GetSymDef().IsEmpty())
				{
					// [case: 7148] [case: 111049]
					// support for alt+shift+g from symbol to gotodef that is not
					// fully qualified due to inclusion in another scope
					WTString scopeThatWasStrippedToFindSym;
					MultiParsePtr mp2(GetParseDb());
					WTString sym = pDt->SymScope();
					auto dt = ::StripScopeFindSym(mp2.get(), true, sym, curSym, scopeThatWasStrippedToFindSym);
					if (dt)
					{
						outDtype = *dt;
						return true;
					}
				}

				outDtype = *pDt;
				return true;
			}
		}
	}
	else
	{
		outDtype = *(&dd);
		return true;
	}

	return false;
}

DType* GetEnumItemParent(DType* dtype, MultiParse* mp)
{
	if (!dtype || !mp || dtype->MaskedType() != C_ENUMITEM)
		return nullptr;

	// c++ enum items are stored at two different scopes
	// :enumType:enunItem and :enumItem. see VAParseMPReparse::OnAddSym.
	// we need to resolve them to :enumType:enumItem.
	auto parent = mp->FindExact(dtype->Scope());
	if (!parent || parent->MaskedType() != C_ENUM)
	{
		// check for unnamed enum
		auto unnamedEnumReverseLookup = mp->FindExact(":va_unnamed_enum_scope" + dtype->SymScope());
		if (unnamedEnumReverseLookup)
		{
			auto fullyScopedEnumItem = mp->FindExact(unnamedEnumReverseLookup->Def());
			if (fullyScopedEnumItem)
			{
				parent = mp->FindExact(fullyScopedEnumItem->Scope());
			}
		}
		else
		{
			WTString def = dtype->Def();
			// Def() format is "enum EnumType ENUMITEM = 0"
			// extract "EnumType"
			if (def.Find("enum ") == 0)
			{
				def = def.Mid(5);
				token2 tk = def;
				def = tk.read(' ');
				if (def.GetLength())
				{
					WTString lookup = dtype->Scope() + DB_SEP_STR + def; // +DB_SEP_STR + dtype->Sym();
					auto fullyScopedEnum = mp->FindExact(lookup);
					if (fullyScopedEnum)
						parent = fullyScopedEnum;
				}
			}
		}
	}

	return parent;
}

uint EdCnt::PopulateMenuFromRecordedEntries(Separator& separator, uint cnt, CMenu* menu)
{
	for (auto& item : separator.RecordedItemsBefore)
	{
		cnt = AddInheritanceMenuItem(item.methodName, item.searchSymScope, item.overloadResolution,
		                             item.gotoRelatedOverloadResolutionMode, &item.ptr, cnt, item.savedItems, item.menu,
		                             item.depth);
	}
	if (separator.RecordedItemsAfter.size())
		::AppendMenu(*menu, MF_SEPARATOR, 1000, "");
	for (auto& item : separator.RecordedItemsAfter)
	{
		cnt = AddInheritanceMenuItem(item.methodName, item.searchSymScope, item.overloadResolution,
		                             item.gotoRelatedOverloadResolutionMode, &item.ptr, cnt, item.savedItems, item.menu,
		                             item.depth);
	}
	return cnt;
}

extern CComPtr<IVsThreadedWaitDialog2> GetVsThreadedWaitDialog2();

LRESULT EdCnt::SuperGoToDef(PosLocation posLocation, bool do_action /*= true*/)
{
	DEFTIMERNOTE(GoToDEFTIMERNOTE, NULL);
	Log("SuperGotoDefX");

	WrapperCheckDecoy chk(g_pUsage->mGotos);
	// [case: 104144]
	CPoint pt(GetPoint(posLocation));
	vClientToScreen(&pt);
	CWaitCursor curs;

	MultiParsePtr mp(GetParseDb());
	DType dtypeTmp;
	bool isCursorInWs = false;
	if (!GetSuperGotoSym(dtypeTmp, isCursorInWs))
	{
		if (do_action)
		{
			// [case: 104185]
			static const char kCantSuperGoto[] = "Goto Related is not available because the symbol is not recognized.";

			if (gTestLogger)
			{
				WTString msg;
				msg.WTFormat("MsgBox: %s\r\n", kCantSuperGoto);
				gTestLogger->LogStr(msg);
			}
			else
				WtMessageBox(kCantSuperGoto, IDS_APPNAME, MB_OK | MB_ICONERROR);
		}
		return FALSE;
	}

	// do_action introduced for [case: 89430]
	// should return here if !do_action? -- otherwise caller will be blocked while all
	// this parsing/lookup happens just to tell caller if something can be exec'd...

	auto dtype = &dtypeTmp;
	dtype->LoadStrs();

	// force template instantiation
	mp->GetBaseClassList(dtype->SymScope());

	std::vector<DType> items;
	int result = 0;

	if (g_ParserThread && g_ParserThread->HasBelowNormalJobActiveOrPending())
	{
		::SetStatus("Results may be incomplete; hierarchy database is being updated...");

		class ClearHierarchyStatus : public ParseWorkItem
		{
		  public:
			ClearHierarchyStatus() : ParseWorkItem("ClearHierarchyStatus")
			{
			}
			virtual void DoParseWork()
			{
				::SetStatus(IDS_READY);
			}
		};

		// post at same priority as InheritanceTypeResolver so it gets handled in order
		g_ParserThread->QueueParseWorkItem(new ClearHierarchyStatus(), ParserThread::priBelowNormal);
	}
	else
		::SetStatus(IDS_READY);

	{
		CComPtr<IVsThreadedWaitDialog2> waitDlg;
		waitDlg = ::GetVsThreadedWaitDialog2();
		if (waitDlg)
		{
			// [case: 140490][case: 104144]
			CComVariant jnk;
			waitDlg->StartWaitDialog(CComBSTR(CStringW(IDS_APPNAME)),
			                         CComBSTR(L"Retrieving data for Goto Related menu..."), nullptr, jnk, nullptr, 2,
			                         VARIANT_FALSE, VARIANT_FALSE);
		}

		PopupMenuXP xpmenu;

		switch (dtype->MaskedType())
		{
		case vaInclude: {
			const WTString incDef(dtype->Def());
			const CStringW curFile(gFileIdManager->GetFile(incDef));
			if (!curFile.IsEmpty())
			{
				MultiParsePtr mp2(GetParseDb());
				DTypePtr fileDt = mp2->GetFileData(curFile);
				if (fileDt)
				{
					items.push_back(*fileDt);
					xpmenu.AddMenuItem(items.size(), MF_STRING, "&Open file");
					items.push_back(*fileDt);
					xpmenu.AddMenuItem(items.size(), MF_STRING, "Open &Containing Folder");
				}

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					BuildIncludeMenuItems(menu, curFile, false, items, 0, cnt);
					if (cnt)
						xpmenu.AppendPopup("&Includes", menu);
					else
						delete menu;
				}

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					BuildIncludeMenuItems(menu, curFile, true, items, 0, cnt);
					if (cnt)
						xpmenu.AppendPopup("Included &By", menu);
					else
						delete menu;
				}
			}
		}
		break;

		case VAR: {
			BuildTitleMenuItem(dtype, items, xpmenu);
			BuildDeclsAndDefsMenuItems(dtype, items, xpmenu);

			DType membersOfType;
			BuildTypeOfSymMenuItems(dtype, items, xpmenu, &membersOfType);

			MultiParsePtr mp2(GetParseDb());
			auto scopePtr = mp2->FindExact(dtype->Scope());
			if (scopePtr && scopePtr->IsType() && scopePtr->MaskedType() != NAMESPACE)
			{
				BuildMembersOfMenuItem(&membersOfType, items, xpmenu, true);
				BuildMembersOfMenuItem(scopePtr, items, xpmenu, true);
			}
			else
			{
				BuildMembersOfMenuItem(&membersOfType, items, xpmenu);
			}
		}
		break;

		case CLASS:
		case STRUCT:
		case C_INTERFACE:
		case TYPE:
			if (isCursorInWs)
			{
				BuildMembersOfMenuItem(dtype, items, xpmenu, true);
			}
			else
			{
				BuildTitleMenuItem(dtype, items, xpmenu);
				BuildDeclsAndDefsMenuItems(dtype, items, xpmenu);

				DType membersOfType;
				BuildTypeOfSymMenuItems(dtype, items, xpmenu, &membersOfType);

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					BuildInheritanceMenuItems(menu, vaInheritsFrom, dtype->SymScope(), items, 0, cnt, nullptr, false);
					if (cnt)
						xpmenu.AppendPopup("&Base Classes", menu);
					else
						delete menu;
				}

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					BuildInheritanceMenuItems(menu, vaInheritedBy, dtype->SymScope(), items, 0, cnt, nullptr, false);
					if (cnt)
						xpmenu.AppendPopup("&Derived Classes", menu);
					else
						delete menu;
				}

				BuildCtorsMenuItem(&membersOfType, items, xpmenu);
				BuildMembersOfMenuItem(&membersOfType, items, xpmenu);
			}
			break;

		case FUNC:
		case PROPERTY: {
			BuildTitleMenuItem(dtype, items, xpmenu);
			BuildDeclsAndDefsMenuItems(dtype, items, xpmenu);

			// only offer Base/Derived for class methods, not free functions
			MultiParsePtr mp2(GetParseDb());
			auto scopePtr = mp2->FindExact(dtype->Scope());
			if (scopePtr && scopePtr->IsType() && scopePtr->MaskedType() != NAMESPACE)
			{
				auto methodName = dtype->Sym();

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					Separator separator =
					    Separator(Psettings->mGotoRelatedOverloadResolutionMode == Settings::GORM_USE_SEPARATOR);
					BuildInheritanceMenuItems(menu, vaInheritsFrom, dtype->Scope(), items, 0, cnt, &methodName,
					                          dtype->MaskedType() == FUNC, &separator);
					cnt = PopulateMenuFromRecordedEntries(separator, cnt, menu);
					if (cnt)
					{
						xpmenu.AppendPopup("&Base Symbols", menu);
					}
					else
					{
						if (Psettings->mGotoRelatedOverloadResolutionMode == Settings::GORM_HIDE)
						{
							BuildInheritanceMenuItems(menu, vaInheritsFrom, dtype->Scope(), items, 0, cnt, &methodName,
							                          false);
							cnt = PopulateMenuFromRecordedEntries(separator, cnt, menu);
						}
						if (cnt)
							xpmenu.AppendPopup("&Base Symbols", menu);
						else
							delete menu;
					}
				}

				{
					CMenu* menu = new CMenu;
					menu->CreateMenu();
					uint cnt = 0;
					Separator separator =
					    Separator(Psettings->mGotoRelatedOverloadResolutionMode == Settings::GORM_USE_SEPARATOR);
					BuildInheritanceMenuItems(menu, vaInheritedBy, dtype->Scope(), items, 0, cnt, &methodName,
					                          dtype->MaskedType() == FUNC, &separator);
					cnt = PopulateMenuFromRecordedEntries(separator, cnt, menu);
					if (cnt)
					{
						xpmenu.AppendPopup("&Derived Symbols", menu);
					}
					else
					{
						if (Psettings->mGotoRelatedOverloadResolutionMode == Settings::GORM_HIDE)
						{
							BuildInheritanceMenuItems(menu, vaInheritedBy, dtype->Scope(), items, 0, cnt, &methodName,
							                          false);
							cnt = PopulateMenuFromRecordedEntries(separator, cnt, menu);
						}
						if (cnt)
							xpmenu.AppendPopup("&Derived Symbols", menu);
						else
							delete menu;
					}
				}

				BuildMembersOfMenuItem(scopePtr, items, xpmenu, true);
			}
		}
		break;

		case C_ENUMITEM:
			BuildTitleMenuItem(dtype, items, xpmenu);
			BuildDeclsAndDefsMenuItems(dtype, items, xpmenu);
			{
				auto parent = GetEnumItemParent(dtype, mp.get());
				if (parent)
					BuildMembersOfMenuItem(parent, items, xpmenu, true);
			}
			break;

		case vaHashtag: {
			BuildTitleMenuItem(dtype, items, xpmenu);
			BuildHashtagUsageAndRefsMenuItems(dtype, items, xpmenu);
		}
		break;

		default:
			BuildTitleMenuItem(dtype, items, xpmenu);
			BuildDeclsAndDefsMenuItems(dtype, items, xpmenu);
			BuildMembersOfMenuItem(dtype, items, xpmenu);
			break;
		}

		if (waitDlg)
		{
			BOOL jnk = FALSE;
			waitDlg->EndWaitDialog(&jnk);
		}

		if (xpmenu.ItemCount() > 0)
		{
			if (!do_action)
				return TRUE;

			if (gShellAttr->IsDevenv10OrHigher())
				SetFocusParentFrame(); // case=45591

			curs.Restore();
			PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
			TempTrue tt(m_contextMenuShowing);
			result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
		}
		else
		{
			if (do_action)
			{
				// [case: 104185]
				const char* kCantSuperGoto;

				if (dtype && RESWORD == dtype->MaskedType())
					kCantSuperGoto = "Goto Related is not available for this text.";
				else
					kCantSuperGoto = "Goto Related could not identify any applicable locations or actions.";

				if (gTestLogger)
				{
					WTString msg;
					msg.WTFormat("MsgBox: %s\r\n", kCantSuperGoto);
					gTestLogger->LogStr(msg);
				}
				else
					WtMessageBox(kCantSuperGoto, IDS_APPNAME, MB_OK | MB_ICONERROR);
			}

			return FALSE;
		}
	}

	if (!result)
		return FALSE;

	if (result > (int)items.size())
	{
		result -= 1000;
		if (result > 0 && result <= (int)items.size())
		{
			VABrowseMembersDlg(&items[uint(result - 1)]);
		}
	}
	else
	{
		auto& d = items[uint(result - 1)];
		if (!d.HasData())
		{
			// [case: 104132]
			d.LoadStrs(true);
		}

#ifdef RAD_STUDIO
		// d.HasData seems to be false, but there seems to be enough inside for goto to work
		if (d.HasData() && d.SymScope().GetLength() || vaInclude == dtype->MaskedType()) // [case: 104132]
#else
		if (d.HasData() && (d.SymScope().GetLength() || vaInclude == dtype->MaskedType())) // [case: 104132]
#endif
		{
			if (dtype->MaskedType() == vaInclude && result == 2)
			{
				if (mThis != nullptr)
				{
					CStringW file = GetFileLocationFromInclude(mThis);
					WTString failureMessage("Open File Location file empty, command not executed.");

					if (file.GetLength())
						BrowseToFile(file);
					else if (gTestLogger)
						gTestLogger->LogStr(failureMessage);
					else
						Log(failureMessage.c_str());
				}
			}
			else
				::GotoSymbol(d);
		}
		else
			CmdEditGotoDefinition(); // [case: 100630]
	}
	return FALSE;
}

bool EdCnt::CmdEditGotoDefinition()
{
	if (gShellAttr && gShellAttr->IsCppBuilder())
		return false;

	// [case: 100904]
	if (!gPkgServiceProvider)
	{
		::MessageBeep(0XFFFFFFFF);
		return false;
	}

	IUnknown* tmp = NULL;
	gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&tmp);
	CComQIPtr<IVsUIShell> uiShell(tmp);
	if (uiShell)
	{
		CComPtr<EnvDTE::Commands> pCmds;
		CComPtr<EnvDTE::Command> pCmd;
		gDte->get_Commands(&pCmds);
		if (pCmds)
			pCmds->Item(CComVariant("Edit.GoToDefinition"), 0, &pCmd);
		if (pCmd)
		{
			BSTR guid;
			pCmd->get_Guid(&guid);
			CString guid2 = guid;
			if (guid2.GetLength() > 1 && guid2[0] == '{')
				guid2 = guid2.Mid(1, guid2.GetLength() - 2); // removing { and }
			GUID guid3;
			UuidFromString((unsigned char*)(LPCTSTR)guid2, &guid3);

			long id;
			pCmd->get_ID(&id);
			uiShell->PostExecCommand(&guid3, (uint)id, 0, 0);
			return true;
		}
	}

	return false;
}

// Function to ensure the last two scopes in WTString are duplicates
// Returns true if duplication happens, false otherwise
bool EnsureLastTwoScopesAreDuplicates(WTString& symScopeToSearchFor)
{
	int lastSepPos = -1;
	int secondLastSepPos = -1;

	for (int i = symScopeToSearchFor.GetLength() - 1; i >= 0; i--)
	{
		if (symScopeToSearchFor[i] == DB_SEP_CHR)
		{
			if (lastSepPos == -1)
			{
				lastSepPos = i;
			}
			else
			{
				secondLastSepPos = i;
				break;
			}
		}
	}

	// no scope found
	if (lastSepPos == -1)
	{
		return false;
	}

	if (secondLastSepPos == -1)
	{
		// Only one scope found, duplicate it
		WTString lastScope = symScopeToSearchFor.Mid(lastSepPos + 1);
		if (lastScope.length()) // sanity check
		{
			symScopeToSearchFor += DB_SEP_CHR + lastScope;
			return true;
		}

		return false;
	}

	WTString lastScope = symScopeToSearchFor.Mid(lastSepPos + 1);
	WTString secondLastScope = symScopeToSearchFor.Mid(secondLastSepPos + 1, lastSepPos - secondLastSepPos - 1);

	// If the last two scopes are not the same, duplicate the last scope
	if (lastScope != secondLastScope && lastScope.length())
	{
		symScopeToSearchFor += DB_SEP_CHR + lastScope;
		return true;
	}

	return false;
}

bool EdCnt::IsOpenParenAfterCaretWord()
{
	const WTString& buf = GetBuf();
	long p1, p2;
	GetSel(p1, p2);
	if (p1 == p2)
	{
		p1 = GetBufIndex(buf, p1);

		int index = p1;

		// 1. Iterate until a non-alphabet character is found
		while (index < buf.length() && wt_isalnum(buf[index]))
			index++;

		// 2. Continue iterating until a non-whitespace character is found
		while (index < buf.length() && wt_isspace(buf[index]))
			index++;

		// 3. Check if the non-whitespace character is '('
		if (index < buf.length() && buf[index] == '(')
			return true;
	}

	return false;
}

LRESULT
EdCnt::GoToDef(PosLocation menuPos, WTString customScope /*= ""*/)
{
	static SymbolPosList sGotoList;
	static WTString sLastSymScope;

	DEFTIMERNOTE(GoToDEFTIMERNOTE, NULL);
	Log("GotoDefX");

	WrapperCheckDecoy chk(g_pUsage->mGotos);
	CurScopeWord();

	{
		// Allow ScopeInfoPtr to override GotoDef
		::ScopeInfoPtr si = ScopeInfoPtr();
		if (si->GoToDef())
		{
			sLastSymScope.Empty();
			return TRUE;
		}
	}

	const CStringW fname(FileName());
	// Goto #include <file>
	bool isPreProcLine = m_lastScope == DB_SCOPE_PREPROC;
	if (!isPreProcLine && m_lastScope == DB_SEP_STR)
	{
		// [case: 77331]
		// when caret is at column 0, before #include status info is incorrect (different
		// than after the # char) because m_lastScope does not get get to DB_SCOPE_PREPROC until
		// after the # char.  Compensate here.
		const WTString bb(GetBufConst());
		int pos = GetBufIndex(bb, (long)CurPos());
		if (pos > 0 && pos < bb.GetLength() && bb[pos] == '#')
			isPreProcLine = true;
	}
	if (isPreProcLine)
	{
		token2 lineTok = GetSubString(LinePos(), LinePos(CurLine() + 1));
		if (lineTok.contains("include") || lineTok.contains("import"))
		{
			sLastSymScope.Empty();
			BOOL doLocalSearch = lineTok.contains("\"");
			lineTok.read("<\""); // strip off #include "
			CStringW file = lineTok.read("<>\"").Wide();
			if (file.GetLength())
			{
				file = gFileFinder->ResolveInclude(file, ::Path(fname), doLocalSearch);
				if (file.GetLength())
				{
					// [case: 798] see if we have a mixed case version already in the fileId db
					UINT fid = gFileIdManager->GetFileId(file);
					if (fid)
						file = gFileIdManager->GetFile(fid);
					DelayFileOpen(file, 0, NULL, TRUE);
					return TRUE;
				}
			}
			return FALSE;
		}
	}

	//	BOOL isMeth = (GetSymDtypeType() == FUNC);
	FileDic* sysDic = /*(m_ftype==Java)?g_pJavaDic:*/ GetSysDic();
	if (g_VATabTree) // so user can go back
		g_VATabTree->AddMethod(Scope().c_str(), fname, CurLine() - 1);
	if (GetSymDef().GetLength() && (IsFile(GetSymDef().Wide()) || gFileIdManager->GetFile(GetSymDef()).GetLength()))
	{
		// when would SymDef be a file?
		CStringW theFile(GetSymDef().Wide());
		if (!IsFile(theFile))
			theFile = gFileIdManager->GetFile(GetSymDef());
		DelayFileOpen(theFile, 0, NULL, TRUE);
		::SetTimer(::GetFocus(), ID_ADDMETHOD_TIMER, 500, NULL);
		sLastSymScope.Empty();
		return TRUE;
	}

	Log("GotoDef1");
	int cp = (int)CurPos();
	bool curPosIsInPreviousList = false;
	const WTString ss(GetSymScope());
	if (TERCOL(cp) > 1)
		sGotoList.clear();
	else
	{
		if (sGotoList.Contains(gFileIdManager->GetFileId(fname), (int)TERROW(cp)))
			curPosIsInPreviousList = true;

		if (ss.IsEmpty() && GetSymDef().IsEmpty())
		{
			// if cp is not in sGotoList, then clear list
			if (!curPosIsInPreviousList)
				sGotoList.clear();
		}
	}

	// gotodef from main menu, goto if only one choice, or else pop up menu
	Log("GotoDef4");

	bool bGotoAny = false;
	bool retryAsGotoAnyIfNoHits = false;
	const bool isComment = (ss == "String");
	WTString symScope = customScope.IsEmpty() ? ss : customScope;
	if (curPosIsInPreviousList && symScope.GetLength() > 1 && ('c' == symScope[1] || 'C' == symScope[1]) &&
	    !symScope.CompareNoCase(":const"))
	{
		WTString nextWd(CurWord());
		nextWd.Trim();
		if (nextWd.IsEmpty())
		{
			nextWd = CurWord(1);
			nextWd.Trim();
			if (nextWd.IsEmpty() || nextWd == "{")
			{
				// [case: 100615]
				symScope = sLastSymScope;
			}
		}
	}

	sLastSymScope.Empty();

	const DType dd(GetSymDtype());
	if (GetSymDef().IsEmpty() || dd.IsIncludedMember() // So we don't just goto the #include line. case=9117
	    /*&& symScope == "String"*/) // Goto symbols in unknown scope should behave like goto in comments. case=31460
	{
		// case:17089
		// support for alt+g on words in comments/strings and unidentified syms
		WTString curWord(CurWord(0, isComment));
		if (!curWord.IsEmpty() && Psettings->mHashtagsAllowHypens && isComment)
		{
			// [case: 109520]
			WTString altSym(CurWord(curWord == "-" ? -1 : (curWord == "#" ? 1 : 0), true));
			if (altSym[0] == '#')
				curWord = altSym;
		}

		curWord.Trim();
		// [case: 94296] allow caret at start of sym
		if (curWord.IsEmpty())
			curWord = WordRightOfCursor();
		else if (curWord[curWord.GetLength() - 1] != '.' && curWord[curWord.GetLength() - 1] != ':' &&
		         !(ISCSYM(curWord[curWord.GetLength() - 1])))
		{
			WTString tmp(WordRightOfCursor());
			if (ISCSYM(tmp[tmp.GetLength() - 1]))
				curWord = tmp;
		}

		curWord.Trim();
		if (!curWord.IsEmpty())
		{
			std::list<WTString> wds;
			for (int idx = -1;;)
			{
				const WTString prevWd2(CurWord(idx--));
				const WTString prevWd1(CurWord(idx--));
				if ((prevWd2 == "." || prevWd2 == "::") && prevWd1.GetLength() && wt_isalpha(prevWd1[0]))
				{
					wds.push_front(prevWd1);
					retryAsGotoAnyIfNoHits = true;
				}
				else
					break;
			}

			if (retryAsGotoAnyIfNoHits)
			{
				// [case: 87245]
				symScope.Empty();
				for (auto wd : wds)
					symScope += DB_SEP_STR + wd;

				symScope += DB_SEP_STR + curWord;
			}
			else
			{
				// FindDefList will find all instances, regardless of scope in comments/strings. case=17089
				symScope = DB_SEP_STR + curWord;
				bGotoAny = true;
			}
		}
	}

#ifdef RAD_STUDIO
	// #RAD_GoToDeclFromDefScope
	if (gCurrExecCmd == icmdVaCmd_GotoDeclOrImpl && !dd.IsDecl())
	{
		extern WTString GetReducedScope(const WTString& scope);
		extern WTString GetInnerScopeName(WTString fullScope);

		MultiParsePtr mp = GetParseDb();
		WTString scope = Scope();
		WTString cutScope = Scope();
		WTString methodName = GetInnerScopeName(cutScope);
		WTString BCL = mp->m_baseClassList;
		DType* sym = mp->FindSym(&methodName, &scope, &BCL);
		while (sym && !sym->IsMethod())
		{ // cutting in-method stuff like "for", "if", etc. from the end of the scope
			WTString newScope = GetReducedScope(cutScope);
			if (newScope == cutScope)
				break;
			cutScope = newScope;
			methodName = GetInnerScopeName(cutScope);
			sym = mp->FindSym(&methodName, &scope, &BCL);
		}

		if (sym && sym->IsMethod())
		{
			if (!sym->IsDecl())
				sym = FindDeclaration(*sym);

			if (sym)
			{
				DelayFileOpen(sym->FilePath(), sym->Line(), sym->Sym(), TRUE, sym->type() == FUNC ? TRUE : FALSE);
				return TRUE;
			}
		}
	}
#endif

	if (gTestsActive)
	{
		// handle odd startup state that occurs on colin's win 8 system.
		// first test tries to exec alt+g but something is off sometimes (bad buffer,
		// incorrect file, bad wnd, ...)
		if (isComment && symScope == ":>")
		{
			if (gTestLogger)
			{
				WTString msg2;
				msg2.WTFormat("Error: Goto attempt on symbol '>' in file %s \r\n",
				              (LPCTSTR)CString(Basename(FileName())));
				gTestLogger->LogStr(msg2);
			}
			return FALSE;
		}
	}

	const WTString symToSearchFor(::StrGetSym(symScope));
	TempAssign<int> ta(g_doDBCase, 0, symToSearchFor[0] == '#', -1);
	bool constructor = false;
	for (;;)
	{
		WTString savedStrippedScope;
		WTString symScopeToSearchFor(symScope);
		if (symScopeToSearchFor.GetLength())
			sGotoList.clear();
		// uncomment the curPosIsInPreviousList check to allow alt+g to work multiple times
		// back and forth; not enabling now though; problem if you want to alt+g on
		// a method return type after arriving at the method declaration
		while (symScopeToSearchFor.GetLength() /*&& !curPosIsInPreviousList*/)
		{
			bool isUe4Implmentation = false;
			bool isUe4Validate = false;
			auto AddAdditionalUe4Hits = [&](auto& dict) {
				// [case: 86215] [case: 104685] handle *_Implementation and *_Validate
				if (!isUe4Implmentation && !isUe4Validate)
				{
					dict->FindDefList(symScopeToSearchFor + "_Implementation", !bGotoAny, sGotoList);
					dict->FindDefList(symScopeToSearchFor + "_Validate", !bGotoAny, sGotoList);
				}
				else if (isUe4Implmentation)
					dict->FindDefList(symScopeToSearchFor.Left(ss.GetLength() - 15), !bGotoAny, sGotoList);
				else if (isUe4Validate)
					dict->FindDefList(symScopeToSearchFor.Left(ss.GetLength() - 9), !bGotoAny, sGotoList);
			};

			if (Psettings->mUnrealEngineCppSupport && IsCFile(m_ftype))
			{
				isUe4Implmentation = symScopeToSearchFor.EndsWith("_Implementation");
				isUe4Validate = symScopeToSearchFor.EndsWith("_Validate");
			}

			sysDic->FindDefList(symScopeToSearchFor, !bGotoAny, sGotoList);
			if (Psettings->mUnrealEngineCppSupport && IsCFile(m_ftype))
				AddAdditionalUe4Hits(sysDic); // [case: 141286] handle *_Implementation and *_Validate
			const size_t kSysCnt = sGotoList.size();
			g_pGlobDic->FindDefList(symScopeToSearchFor, !bGotoAny, sGotoList);

			// [case: 149212] navigate to constructor from an explicit constructor call using goto
			if (sGotoList.size() && (*sGotoList.begin()).mType == CLASS) // we might be dealing with a constructor
			{
				if (IsOpenParenAfterCaretWord())
				{
					if (EnsureLastTwoScopesAreDuplicates(symScopeToSearchFor))
					{
						sGotoList.clear();
						g_pGlobDic->FindDefList(symScopeToSearchFor, true, sGotoList);
						if (!sGotoList.empty())
							constructor = true;
					}
				}
			}

			if (Psettings->mUnrealEngineCppSupport && IsCFile(m_ftype))
				AddAdditionalUe4Hits(g_pGlobDic); // [case: 141286] handle *_Implementation and *_Validate

			bool checkLocal = bGotoAny || sGotoList.empty();
			if (!checkLocal && !kSysCnt && Src == m_ftype)
			{
				// [case: 36625]
				// this fixes the first example in case 36625 but is a bit pointless
				// since it doesn't address the problem the user actually reported (the
				// second example).
				// However, it does fix the problem of alt+g not listing a GOTODEF from
				// the active file if there is no solution (debugging a branch without
				// a solution, alt+g on method call opened header ignoring current file)
				checkLocal = true;
			}

			MultiParsePtr mp(GetParseDb());
			if (checkLocal && mp && mp->LDictionary())
				mp->LDictionary()->FindDefList(symScopeToSearchFor, !bGotoAny, sGotoList);

			if (!bGotoAny && !GetSymDef().IsEmpty() && Settings::gib_none != Psettings->mGotoInterfaceBehavior)
			{
				// [case: 36934] alt+g interface support
				// [case: 73415] alt+g support for interface defined properties
				const uint defType = sGotoList.size() ? (*sGotoList.begin()).mType : FUNC;
				if (FUNC == defType || GOTODEF == defType || PROPERTY == defType)
				{
					WTString def(GetSymDef());
					int pos = def.ReverseFind(')');
					// added GOTODEF for source file defined properties
					if (-1 != pos || (PROPERTY == defType || GOTODEF == defType))
					{
						bool lookForInterfaceMethodImplementers = false;
						if (-1 != pos)
						{
							if (IsCFile(m_ScopeLangType))
							{
								if (-1 != def.Find("=0", pos) || -1 != def.Find("= 0", pos) ||
								    -1 != def.Find("PURE", pos))
								{
									// it's an interface (or close enough), search for implementations
									lookForInterfaceMethodImplementers = true;
								}
							}
							else if (CS == m_ScopeLangType)
							{
								const int keyPos = def.Find("abstract");
								if (-1 != keyPos && keyPos < pos)
								{
									// it's an interface (or close enough), search for implementations
									lookForInterfaceMethodImplementers = true;
								}
							}
						}

						if (!lookForInterfaceMethodImplementers)
						{
							// search for interface in base scope of sym
							WTString scp = ::StrGetSymScope(symScopeToSearchFor);
							if (!scp.IsEmpty())
							{
								// This xref check should be based on the type of xref so that
								// :: and . are handled differently than -> in C++.
								// For C#, no difference in handling.
								// Unless we know the xref is on a local var?
								if ((mp->m_xref || -1 != symScopeToSearchFor.Find(mp->m_baseClass) ||
								     Settings::gib_originalInterfaceCheck == Psettings->mGotoInterfaceBehavior) &&
								    Settings::gib_forceExpansive != Psettings->mGotoInterfaceBehavior)
								{
									// old way
									// preferred when sym is overridden in current class
									// or when cur sym is an xref
									_ASSERTE(Psettings->mGotoInterfaceBehavior <=
									         Settings::gib_conditionalBclExpansive);
									DType* dat = mp->FindExact2(scp);
									if (dat)
									{
										if (dat->MaskedType() == C_INTERFACE)
											lookForInterfaceMethodImplementers = true;
										else if (dat->MaskedType() == CLASS && -1 != dat->Def().Find("abstract"))
											lookForInterfaceMethodImplementers = true;
										else if (dat->MaskedType() == STRUCT && -1 != dat->Def().Find("abstract"))
											lookForInterfaceMethodImplementers = true;
									}
								}
								else
								{
									// more expansive way
									// preferred when sym is not overridden in current class
									// [case: 74344] iterate over bcl
									_ASSERTE(Psettings->mGotoInterfaceBehavior >= Settings::gib_conditionalBcl);
									const WTString scpFormfeed(scp + "\f");
									WTString bcl(mp->GetBaseClassList(scp));
									if (-1 == bcl.Find(scpFormfeed))
										bcl = scpFormfeed + bcl;
									token t(bcl);
									bcl.Empty();
									// first we iterate over bcl to see if anything in bcl is interface-like
									while (t.more())
									{
										const WTString bc = t.read("\f");
										if (bc.IsEmpty() || bc == DB_SEP_STR)
											continue;

										if (bc == ":interface")
										{
											lookForInterfaceMethodImplementers = true;
											continue;
										}

										DType* dat = mp->FindExact2(bc);
										if (!dat)
											continue;

										if (dat->MaskedType() == C_INTERFACE)
											lookForInterfaceMethodImplementers = true;
										else if (dat->MaskedType() == CLASS && -1 != dat->Def().Find("abstract"))
											lookForInterfaceMethodImplementers = true;
										else if (dat->MaskedType() == STRUCT && -1 != dat->Def().Find("abstract"))
											lookForInterfaceMethodImplementers = true;

										if (Settings::gib_conditionalBclExpansive <= Psettings->mGotoInterfaceBehavior)
										{
											// Causes override hidden impls to be listed in alt+g.
											// save scope to search for if we come across an interface.
											// can't search now because we might not hit an interface until
											// the end of the list, which means items earlier in the list would
											// have been missed if the search was in this loop.
											bcl += bc + DB_SEP_STR + symToSearchFor + "\f";
										}
										else if (lookForInterfaceMethodImplementers)
											break;
									}

									if (lookForInterfaceMethodImplementers &&
									    Settings::gib_conditionalBclExpansive <= Psettings->mGotoInterfaceBehavior)
									{
										// bcl search found an interface so now search all base
										// classes for implementations.
										t = bcl;
										while (t.more())
										{
											const WTString symScp = t.read("\f");
											if (symScp.IsEmpty())
												continue;

											g_pGlobDic->FindDefList(symScp, true, sGotoList, false, true);
										}
									}
								}
							}
						}

						if (lookForInterfaceMethodImplementers)
						{
							// CONSIDER: search sysDic if curFile is sys file? (but not for C#)
							g_pGlobDic->FindDefList(symScopeToSearchFor, false, sGotoList, true);
						}
					}
				}
			}

			if (!bGotoAny && !GetSymDef().IsEmpty() && 1 == sGotoList.size() && IsCFile(m_ScopeLangType))
			{
				// [case: 7148] [case: 111049]
				// support for alt+g from fully qualified symbol to gotodef that is not
				// fully qualified due to using statements
				MultiParsePtr mp2(GetParseDb());
				const DType dd2(GetSymDtype());
				auto dt = ::StripScopeFindSym(mp2.get(), !!dd2.IsIncludedMember(), symScopeToSearchFor,
				                              symToSearchFor, savedStrippedScope);
				if (dt)
					continue;
			}

			break;
		}

		if (retryAsGotoAnyIfNoHits && sGotoList.empty())
		{
			retryAsGotoAnyIfNoHits = false;
			bGotoAny = true;
			symScope = DB_SEP_STR + symToSearchFor;
		}
		else
			break;
	}

	sLastSymScope = symScope;
	// from this point forward, sGotoList is const

	if (sGotoList.empty())
	{
		if (symScope.GetLength() && ::GoToDEF(symScope)) // LocalDef
			return TRUE;
		Log("GotoDef7");
		if (!isComment)
		{
			if (m_ftype == CS || Is_VB_VBS_File(m_ftype))
			{
				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.GoToDefinition"), 0);
				return TRUE;
			}
			else if (m_ftype == XAML || m_ftype == ASP)
			{
				// Launch Object browser on system symbols in asp/xaml files. case:20970
				// [case: 96541] display menu so that user can cancel
				WTString symDisplay(CleanScopeForDisplay(symScope));
				CStringW displayText(L"Search object browser for " + symDisplay.Wide());
				WTString browseCmd = WTString("View.ObjectBrowserSearch ") + symDisplay;

				if (gShellAttr->IsDevenv10OrHigher())
					SetFocusParentFrame(); // case=45591
				CPoint pt(GetPoint(menuPos));
				vClientToScreen(&pt);

				PopupMenuXP xpmenu;
				CStringW txt = ::BuildMenuTextHexAcceleratorW(1, displayText);
				int locDim = txt.Find(L"    ");
				if (locDim != -1)
					txt = FTM_DIM + txt.Left(locDim) + FTM_NORMAL + txt.Right(txt.GetLength() - locDim);

				CStringW boldSymbol = FTM_BOLD + symDisplay.Wide() + FTM_NORMAL;
				ReplaceWholeWord(txt, symDisplay.Wide(), boldSymbol);
				xpmenu.AddMenuItemW(1, MF_BYPOSITION, txt);

				PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
				int result;
				{
					TempTrue tt(m_contextMenuShowing);
					result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
					if (!result)
						return FALSE;
				}

				SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM)browseCmd.c_str(), 0);
				return TRUE;
			}
			else if (IsCFile(m_ftype))
			{
				if (!g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "DisableIntelliSense"))
					if (CmdEditGotoDefinition())
						return TRUE;
			}
			else
			{
				if (CmdEditGotoDefinition())
					return TRUE;
			}
		}
		return FALSE;
	}

	char c = CharAt(CurPos());
	int ln = -1;
	if (c == '{')
		ln = CurLine();

	int pos = GetBufIndex((long)CurPos());
	DefListFilter filt(fname, (int)TERROW(cp), CurLine() + 1, pos, ln);
	const SymbolPosList& posList = filt.Filter(sGotoList, constructor);

	if (1 == posList.size())
	{
		// only one choice, go there
		const CStringW gfile = (*posList.begin()).mFilename;
		if (IsFile(gfile))
		{
			BOOL func = GetSymDtypeType() == FUNC;
			DelayFileOpen(gfile, (*posList.begin()).mLineNumber, symToSearchFor, TRUE, func);
			VAProjectAddFileMRU((*posList.begin()).mFilename, mruFileOpen);
			::SetTimer(::GetFocus(), ID_ADDMETHOD_TIMER, 500, NULL);
			return TRUE;
		}
	}
	else if (posList.size() > 1)
	{
		if (gShellAttr->IsDevenv10OrHigher())
			SetFocusParentFrame(); // case=45591
		CPoint pt(GetPoint(menuPos));
		vClientToScreen(&pt);

		PopupMenuXP xpmenu;
		SymbolPosList::const_iterator it;
		CStringW txt;
		uint idx = 1;
		uint separatorCount = 0;
		for (it = posList.begin(); it != posList.end(); ++it)
		{
			txt = ::BuildMenuTextHexAcceleratorW(idx - separatorCount, (*it).mDisplayText);
			if (txt.GetLength() >= 3 && txt.Left(3) == L"---")
			{
				xpmenu.AddSeparator(idx++);
				separatorCount++;
				continue;
			}

			int locDim = txt.Find(L"    ");
			if (locDim != -1)
			{
				txt = FTM_DIM + txt.Left(locDim) + FTM_NORMAL + txt.Right(txt.GetLength() - locDim);
			}

			CStringW filenameTxt;
			int colonPos = txt.Find(L':');
			if (-1 != colonPos)
			{
				// [case: 85913]
				filenameTxt = txt.Left(colonPos);
				txt = txt.Mid(colonPos);
			}

			WTString searchSymTextToReplace(symToSearchFor);
			CStringW boldSymbol = FTM_BOLD + searchSymTextToReplace.Wide() + FTM_NORMAL;
			if (-1 == txt.Find(searchSymTextToReplace.Wide()))
			{
				// don't let encoded templates break search for sym to bold
				WTString tmp = DecodeTemplates(searchSymTextToReplace);
				if (tmp != searchSymTextToReplace)
				{
					if (-1 == txt.Find(tmp.Wide()))
					{
						int pos2 = tmp.Find('<');
						if (-1 != pos2)
						{
							searchSymTextToReplace = tmp.Left(pos2);
							boldSymbol = FTM_BOLD + searchSymTextToReplace.Wide() + FTM_NORMAL;
						}
					}
					else
					{
						searchSymTextToReplace = tmp;
						boldSymbol = FTM_BOLD + searchSymTextToReplace.Wide() + FTM_NORMAL;
					}
				}
			}
			ReplaceWholeWord(txt, searchSymTextToReplace.Wide(), boldSymbol);

			WTString stubImpl = _T("{...}");
			CStringW boldStubImpl = FTM_BOLD + stubImpl.Wide() + FTM_NORMAL;
			ReplaceWholeWord(txt, _T("{...}"), boldStubImpl);

			txt = filenameTxt + txt;
			xpmenu.AddMenuItemW(idx++, MF_BYPOSITION, txt);
		}

		PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
		int result;
		{
			TempTrue tt(m_contextMenuShowing);
			result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
			if (!result)
				return FALSE;
		}

		idx = 1;
		for (it = posList.begin(); it != posList.end(); ++it)
		{
			if (idx++ != (uint)result)
				continue;

			if (IsFile((*it).mFilename))
			{
				BOOL func = GetSymDtypeType() == FUNC;
				::DelayFileOpen((*it).mFilename, (*it).mLineNumber, symToSearchFor, TRUE, func);
				VAProjectAddFileMRU((*it).mFilename, mruFileOpen);
				::SetTimer(::GetFocus(), ID_ADDMETHOD_TIMER, 500, NULL);
				return TRUE;
			}
			break;
		}
	}

	// if posList is empty, we most likely filtered out the current position
	return false;
}

void EdCnt::CmEditPasteMenu()
{
	// display copy buffers in popupmenu
	DEFTIMERNOTE(CmEditPasteMenuTimer, NULL);
	if (!g_VATabTree)
		return;

	if (gShellAttr->IsDevenv10OrHigher())
		SetFocusParentFrame(); // case=45591
	CPoint pt = vGetCaretPos();
	pt.y += g_FontSettings->GetCharHeight();
	PopupMenuXP xpmenu;
	std::vector<HTREEITEM> items;

	// walk the paste history list (which is stored in an invisible tree)
	// this is mostly a duplication of BorderCWnd::PopulatePasteMenu
	HTREEITEM mitem = g_VATabTree->GetChildItem(TVI_ROOT);
	if (mitem)
		mitem = g_VATabTree->GetNextItem(mitem, TVGN_CHILD);
	while (mitem)
	{
		const CStringW title = g_VATabTree->GetItemTextW(mitem);
		if (VAT_PASTE == title)
		{
			CStringW menuTxt;
			HTREEITEM item = g_VATabTree->GetChildItem(mitem);
			uint idx = 1;
			while (item && idx <= Psettings->m_clipboardCnt)
			{
				menuTxt = ::BuildMenuTextHexAcceleratorW(idx, g_VATabTree->GetItemTextW(item), false);
				xpmenu.AddMenuItemW(idx++, MF_BYPOSITION, menuTxt);
				items.push_back(item);
				item = g_VATabTree->GetNextItem(item, TVGN_NEXT);
			}
			break;
		}

		mitem = g_VATabTree->GetNextItem(mitem, TVGN_NEXT);
	}

	if (!items.size())
		return;

	// select second item by default (since ctrl+v would be the first item)
	PostMessage(WM_KEYDOWN, VK_DOWN, 1);
	PostMessage(WM_KEYDOWN, VK_DOWN, 1);
	vClientToScreen(&pt);
	int result;
	{
		TempTrue tt(m_contextMenuShowing);
		result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
	}

	if (result)
	{
		if (--result < (int)items.size())
		{
			mitem = items[(uint)result];
			g_VATabTree->OpenItemFile(mitem, this);
		}
	}
}

bool IncrementAndCheckIfMenubarBreak(int& items, int items_per_column)
{
	return !((items++ % items_per_column) || (items <= 1));
}

void EdCnt::CodeTemplateMenu()
{
	DEFTIMERNOTE(CodeTemplateMenuTimer, NULL);

	// display code template options in popupmenu
	if (gShellAttr->IsDevenv10OrHigher())
		SetFocusParentFrame(); // case=45591
	CPoint pt = vGetCaretPos();
	pt.y += g_FontSettings->GetCharHeight() * 2;
	PopupMenuXP xpmenu;

	const CRect desktoprect = g_FontSettings->GetDesktopEdges(this);
	const int itemsPerCol = desktoprect.Height() / max(CMenuXP::GetItemHeight(m_hWnd), 10) -
	                        1; // gmit: 10 is used to prevent crash if GetItemHeight returns zero for whatever reason
	                           //	const int itemsPerCol = 35;

	gAutotextMgr->Load(m_ScopeLangType);
	const BOOL kHasSelection = HasSelection();

	// this is mostly a duplication of BorderCWnd::PopulateAutotextMenu
	int atUsingSelectionCnt, atWithShortcutCnt, atMoreCnt, atClipboardCnt;
	atUsingSelectionCnt = atWithShortcutCnt = atMoreCnt = atClipboardCnt = 0;

	CMenu* atUsingSelectionMenu = new CMenu;
	CMenu* atWithShortcutsMenu = new CMenu;
	CMenu* atUsingClipboardMenu = new CMenu;
	CMenu* atMoreMenu = new CMenu;

	atUsingSelectionMenu->CreateMenu();
	atWithShortcutsMenu->CreateMenu();
	atMoreMenu->CreateMenu();
	atUsingClipboardMenu->CreateMenu();

	// if editor has no selection:
	// A list of entries with titles and no shortcuts and no selection
	// Autotext using $selected$
	// Autotext with shortcuts
	// Edit Autotext..
	//
	// if editor has selection:
	// A list of titles of entries that use $selected$
	// More Autotext
	// Edit Autotext...

	UINT_PTR curId = 1;
	const int kItems = gAutotextMgr->GetCount();
	for (int mainItemsAdded = 0, curItemIdx = 0; curItemIdx < kItems; ++curItemIdx, ++curId)
	{
		const bool hasShortcut = gAutotextMgr->HasShortcut(curItemIdx);
		const bool usesSelection = gAutotextMgr->DoesItemUseString(curItemIdx, kAutotextKeyword_Selection);
		const bool usesClipboard = gAutotextMgr->DoesItemUseString(curItemIdx, kAutotextKeyword_Clipboard);

		WTString menuItemTxt(gAutotextMgr->GetTitle(curItemIdx, true));
		if (menuItemTxt.IsEmpty())
			continue;
		if (menuItemTxt.Find('&') == -1)
		{
			const int itemLen = menuItemTxt.GetLength();
			for (int idx = 0; idx < itemLen; ++idx)
			{
				if (::wt_isalnum(menuItemTxt[idx]))
				{
					menuItemTxt = menuItemTxt.Left(idx) + '&' + menuItemTxt.Mid(idx);
					break;
				}
			}
		}
		if (hasShortcut)
			menuItemTxt += "\t" + gAutotextMgr->GetShortcut(curItemIdx);

		if (kHasSelection)
		{
			if (usesSelection)
			{
				const uint style = IncrementAndCheckIfMenubarBreak(mainItemsAdded, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				xpmenu.AddMenuItemW(curId, MF_STRING | style, menuItemTxt.Wide());
			}
			else
			{
				const uint style = IncrementAndCheckIfMenubarBreak(atMoreCnt, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				::AppendMenuW(atMoreMenu->m_hMenu, MF_STRING | style, curId, menuItemTxt.Wide());
			}
		}
		else
		{
			if (usesSelection)
			{
				const uint style = IncrementAndCheckIfMenubarBreak(atUsingSelectionCnt, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				::AppendMenuW(atUsingSelectionMenu->m_hMenu, MF_STRING | style, curId, menuItemTxt.Wide());
			}

			if (usesClipboard)
			{
				const uint style = IncrementAndCheckIfMenubarBreak(atClipboardCnt, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				::AppendMenuW(atUsingClipboardMenu->m_hMenu, MF_STRING | style, curId, menuItemTxt.Wide());
			}

			if (hasShortcut)
			{
				const uint style = IncrementAndCheckIfMenubarBreak(atWithShortcutCnt, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				::AppendMenuW(atWithShortcutsMenu->m_hMenu, MF_STRING | style, curId, menuItemTxt.Wide());
			}

			if (!hasShortcut && !usesSelection && !usesClipboard)
			{
				const uint style = IncrementAndCheckIfMenubarBreak(mainItemsAdded, itemsPerCol) ? MF_MENUBARBREAK : 0u;
				xpmenu.AddMenuItemW(curId, MF_STRING | style, menuItemTxt.Wide());
			}
		}
	}

	xpmenu.AddSeparator(curId++);

	if (atMoreCnt)
	{
		_ASSERTE(atMoreMenu->GetMenuItemCount());
		xpmenu.AppendPopup("&More VA Snippets", atMoreMenu);
	}
	else
	{
		_ASSERTE(!atMoreMenu->GetMenuItemCount());
		delete atMoreMenu;
		atMoreMenu = NULL;
	}

	if (atClipboardCnt)
	{
		_ASSERTE(atUsingClipboardMenu->GetMenuItemCount());
		CString txt("V&A Snippets with ");
		txt += kAutotextKeyword_Clipboard;
		xpmenu.AppendPopup(txt, atUsingClipboardMenu);
	}
	else
	{
		_ASSERTE(!atUsingClipboardMenu->GetMenuItemCount());
		delete atUsingClipboardMenu;
		atUsingClipboardMenu = NULL;
	}

	if (atUsingSelectionCnt)
	{
		_ASSERTE(atUsingSelectionMenu->GetMenuItemCount());
		CString txt("V&A Snippets with ");
		txt += kAutotextKeyword_Selection;
		xpmenu.AppendPopup(txt, atUsingSelectionMenu);
	}
	else
	{
		_ASSERTE(!atUsingSelectionMenu->GetMenuItemCount());
		delete atUsingSelectionMenu;
		atUsingSelectionMenu = NULL;
	}

	if (atWithShortcutCnt)
	{
		_ASSERTE(atWithShortcutsMenu->GetMenuItemCount());
		xpmenu.AppendPopup("V&A Snippets with shortcuts", atWithShortcutsMenu);
	}
	else
	{
		_ASSERTE(!atWithShortcutsMenu->GetMenuItemCount());
		delete atWithShortcutsMenu;
		atWithShortcutsMenu = NULL;
	}

#define MENUTEXT_EDIT_AUTOTEXT "&Edit VA Snippets..."
#define MENUTEXT_NEW_AUTOTEXT "&Create VA Snippet from selection..."
	xpmenu.AddMenuItem(curId++, MF_STRING | (kHasSelection ? 0u : MF_GRAYED), MENUTEXT_NEW_AUTOTEXT);
	xpmenu.AddMenuItem(curId++, MF_STRING, MENUTEXT_EDIT_AUTOTEXT);

	vClientToScreen(&pt);
	PostMessage(WM_KEYDOWN, VK_DOWN, 1);
	int result;
	{
		TempTrue tt(m_contextMenuShowing);
		result = xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
		if (result < 1)
			return;
	}

	--curId;

	if (result < int(curId - 2))  // (curId - 2) is be the separator
		gAutotextMgr->Insert(mThis, result - 1);
	else if ((int)curId == result)  // watch out for pos of separator
		gAutotextMgr->Edit(m_ScopeLangType);
	else if (int(curId - 1) == result)  // watch out for pos of separator
	{
		const WTString sel = GetSelString();
		gAutotextMgr->Edit(m_ScopeLangType, NULL, sel);
	}
}

CPoint EdCnt::GetPoint(PosLocation posLocation) const
{
	CPoint pt;
	if (posBestGuess == posLocation)
	{
		// prefer caret unless it is not visible (scrolled out of view)
		{
			posLocation = posAtCaret;
			// ensure caret is actually visible
			pt = vGetCaretPos();
			vClientToScreen(&pt);
			CWnd* tmp = CWnd::WindowFromPoint(pt);
			if (!tmp || tmp->GetSafeHwnd() != m_hWnd)
			{
				// in vs2010, m_hWnd is not visible
				if (!(tmp && gShellAttr->IsDevenv10OrHigher() && tmp->GetSafeHwnd() == MainWndH))
				{
					posLocation = posAtCursor;

					if (tmp && gShellAttr->IsDevenv10OrHigher() && tmp->GetSafeHwnd() != MainWndH)
					{
						const CWnd* p = this;
						for (int idx = 0; idx < 5 && p; ++idx)
							p = p->GetParent();

						if (tmp == p)
						{
							CWnd* top = GetTopLevelParent();
							if (top == p || (gShellAttr->IsDevenv10() && top->GetSafeHwnd() == MainWndH) ||
							    (gShellAttr->IsDevenv11OrHigher() && top->GetSafeHwnd() == MainWndH &&
							     Is_Tag_Based(m_ftype)))
							{
								// [case: 98462]
								// [case: 93164]
								// editor moved out of IDE frame and has its own frame
								// caret is ok
								posLocation = posAtCaret;
							}
						}
					}
				}
			}
		}
	}

	if (posAtCaret == posLocation)
	{
		pt = vGetCaretPos();
		pt.y += 20;
	}
	else
	{
		GetCursorPos(&pt);
		vScreenToClient(&pt);
	}
	return pt;
}

// Extract any Unreal Engine markup before a given line. [case: 109255]
CStringW ExtractUnrealMarkup(const CStringW& fileName, int symbolLine)
{
	// Markup only appears in C++ headers.
	if (!IsCFile(GetFileType(fileName)))
	{
		return CStringW();
	}

	// Determine the beginning and ending index of relevant markup.
	CStringW checkedFileName =
	    TokenGetField(fileName, L"\f"); // Found a case where there were two files separated by "\f".
	CStringW fileText(GetFileTextW(checkedFileName));
	EolTypes::EolType eolType = EolTypes::GetEolType(fileText);
	CStringW eolString(EolTypes::GetEolStrW(eolType)); // The full end of line sequence.
	WCHAR eolChar;                                     // The last character of the end of line sequence.

	switch (eolType)
	{
	case EolTypes::eolCrLf:
		eolChar = L'\n';
		break;

	case EolTypes::eolCr:
		eolChar = L'\r';
		break;

	case EolTypes::eolLf:
		eolChar = L'\n';
		break;

	default:
		eolChar = L'\0';
		break;
	}

	int currentLine = 1;
	int markupBegin = 0;
	int markupEnd = 0;

	for (int i = 0; i < fileText.GetLength() && currentLine < symbolLine; ++i)
	{
		// Track the current line.
		if (fileText[i] == eolChar)
		{
			++currentLine;
		}

		// Skip whitespace.
		if (iswspace(fileText[i]))
		{
			continue;
		}

		// Skip line comments.
		bool isLineComment = fileText.GetLength() > i + 1 && fileText[i] == '/' && fileText[i + 1] == '/';

		if (isLineComment)
		{
			do
			{
				++i;

				// Track the current line.
				if (fileText[i] == eolChar)
				{
					++currentLine;
				}

			} while (i < fileText.GetLength() && (fileText[i] != eolChar));

			continue;
		}

		// Skip block comments.
		bool isBlockComment = fileText.GetLength() > i + 1 && fileText[i] == '/' && fileText[i + 1] == '*';

		if (isBlockComment)
		{
			bool isBlockCommentEnd = false;

			do
			{
				++i;

				// Track the current line.
				if (fileText[i] == eolChar)
				{
					++currentLine;
				}

				isBlockCommentEnd = fileText.GetLength() > i + 1 && fileText[i] == '*' && fileText[i + 1] == '/';
			} while (i < fileText.GetLength() && !isBlockCommentEnd);

			if (isBlockCommentEnd)
			{
				++i; // Step over "*" in "*/".
			}

			continue;
		}

		// Track markup.
		bool isMarkup = ::GetTypeOfUnrealAttribute(fileText.GetBuffer() + i) != UNDEF;

		if (isMarkup)
		{
			markupBegin = i;
			markupEnd = 0;

			// Find the end of the markup, which may span multiple lines.
			int parenDeep = 0;
			bool isMarkupEnd = false;

			do
			{
				++i;

				// Track the current line.
				if (fileText[i] == eolChar)
				{
					++currentLine;
				}
				// Track parentheses.
				else if (fileText[i] == L'(')
				{
					++parenDeep;
				}
				else if (fileText[i] == L')')
				{
					if (--parenDeep == 0)
					{
						markupEnd = i + 1; // Include the ")".
						isMarkupEnd = true;
					}
				}
			} while (i < fileText.GetLength() && !isMarkupEnd);

			// Ignore badly formatted markup.
			if (markupEnd == 0)
			{
				markupBegin = 0;
			}

			continue;
		}

		// Skip "junk" lines.
		do
		{
			++i;

			// Track the current line.
			if (fileText[i] == eolChar)
			{
				++currentLine;
			}
			// The "symbolLine" is the last line of a symbol's declaration, not the first. Because of that we have to
			// be clever about detecting if a new symbol has been declared between the last tracked markup and
			// "symbolLine". This is to prevent a previous symbol's markup from "falling-through" and appearing
			// incorrectly for the symbol defined below it. This is done in practice by assuming any symbol definition
			// ends in either a "}" or a ";", forgetting tracked markup when either those characters are seen outside
			// of a comment or metadata.
			else if (fileText[i] == '}' || fileText[i] == ';')
			{
				markupBegin = 0;
				markupEnd = 0;
			}

		} while (i < fileText.GetLength() && fileText[i] != eolChar);
	}

	// Return the formatted markup substring.
	CStringW formattedMarkup;

	if (markupEnd > 0)
	{
		// Back "markupBegin" up to the beginning of the line to include whitespace.
		while (markupBegin > 0 && fileText[markupBegin - 1] != eolChar)
		{
			--markupBegin;
		}

		CStringW markup = eolString + fileText.Mid(markupBegin, markupEnd - markupBegin);

		// Determine the least amount of whitespace before a line.
		int tabSize = g_IdeSettings->GetEditorIntOption("C/C++", "TabSize");
		int leastWhitespace = INT_MAX;
		int whitespace = 0;

		for (int i = 0; i < markup.GetLength(); ++i)
		{
			// Do not count empty lines.
			if (markup[i] == eolChar)
			{
				whitespace = 0;
			}
			else if (!iswspace(markup[i]))
			{
				if (whitespace < leastWhitespace)
				{
					leastWhitespace = whitespace;
				}

				// Skip to the end of the line.
				do
				{
					++i;
				} while (i < markup.GetLength() && markup[i] != eolChar);
			}
			else if (markup[i] == L'\t')
			{
				whitespace += tabSize;
			}
			else
			{
				++whitespace;
			}
		}

		// Construct "formattedMarkup" one line at a time.
		CStringW tabString(L"\t");
		CStringW tabEquivString(L' ', tabSize);
		int tokenStart = 0;
		int tokenEnd = 0;
		CStringW token;

		do
		{
			// Get the next token.
			do
			{
				++tokenEnd;
			} while (tokenEnd < markup.GetLength() && markup[tokenEnd] != eolChar);

			token = markup.Mid(tokenStart, tokenEnd + 1 - tokenStart);
			tokenStart = tokenEnd + 1;

			if (token.GetLength())
			{
				// Truncate empty lines.
				bool isEmpyLine = CStringW(token).Trim().GetLength() == 0;

				if (isEmpyLine)
				{
					formattedMarkup += eolString;
				}
				// Format and append lines.
				else
				{
					token.Replace(tabString, tabEquivString);
					token.Delete(0, leastWhitespace);
					formattedMarkup += token;
				}
			}
		} while (token.GetLength());
	}

	return formattedMarkup;
}

CStringW ExtractLastCommentsBeforeLine(const CStringW& inFile, int line, int maxCommentLines,
                                       const bool acceptEndlineComment)
{
	CStringW file = TokenGetField(inFile, L"\f"); // found a case where there were two files separated by \f
	CStringW ftxt(GetFileTextW(file));

	LPCWSTR p = ftxt;
	const long plen = ftxt.GetLength();
	int cmtPos = 0, cmtPosEnd = 0;
	bool emptyLine = true;
	int linePos = 0, clipPos = 0;
	int inParen = 0;
	BOOL hadText = FALSE;
	const EolTypes::EolType et = EolTypes::GetEolType(CStringW(ftxt, plen > 512 ? 512 : plen));
	const WCHAR lnBrkChar = et == EolTypes::eolCr ? L'\r' : L'\n';
	int fType = GetFileType(inFile);
	BOOL isHTML = Is_Tag_Based(fType);
	BOOL isVB = Is_VB_VBS_File(fType);

	for (long i = 0; i < plen && line >= 1; i++)
	{
		if (isHTML && line == 1)
		{
			// HTML: Get "<tag ....>"
			CStringW indentStr;
			CStringW tagText;
			// Get initial indenting to strip
			for (int j = i; p[j] && wcschr(L" \t", p[j]); j++)
				indentStr += p[j];
			// get next 6 indented lines
			for (int lcount = 0; lcount < 6; lcount++)
			{
				// Strip initial indentation
				if (wcsncmp(p + i, indentStr, (uint)indentStr.GetLength()) == 0)
				{
					if (lcount && !iswspace(p[i + indentStr.GetLength()]))
						break; // Indent ended, do not include this line.
					i += indentStr.GetLength();
					// add additional indentation
					for (int max = 1000; --max > 0 && p[i] && !wcschr(L"\r\n", p[i]); i++)
						tagText += p[i];
					// add line text
					for (int max = 1000; --max > 0 && p[i] && wcschr(L"\r\n", p[i]); i++)
						tagText += p[i];
				}
			}
			return tagText;
		}
		// Gets comments around comments, unlike msdev
		//		if(emptyLine && p[i] == '/' && (p[i+1] == '/'||p[i+1] == '*'))
		//			cmtPos = i+2;
		if (line < 5 && !clipPos)
			clipPos = linePos;
		// Added support for VB style comments. case 20486
		BOOL lineComment = isVB ? (p[i] == L'\'') : (p[i] == L'/' && p[i + 1] == L'/');
		// only get comment on line above or end of line, like msdev
		if (!inParen && lineComment)
		{
			const int endlineCommentLine = (hadText && !emptyLine) ? line : 0;
			// [case: 60776]

			if (!cmtPos || emptyLine || hadText)
			{
				if (!endlineCommentLine || acceptEndlineComment)
					cmtPos = i;
			}

			for (; i < plen && p[i] != lnBrkChar; i++)
				;
			line--;

			if (!endlineCommentLine || acceptEndlineComment)
				cmtPosEnd = i;
			emptyLine = FALSE;
			hadText = FALSE;
			if (endlineCommentLine && endlineCommentLine != 1 && acceptEndlineComment)
			{
				// don't use endline comments from previous lines.
				// the only endline comment that is valid is the one on the line
				// that was passed in (1 at the end of the loop).
				cmtPos = cmtPosEnd = 0;
			}
			continue;
		}
		else if (!inParen && (emptyLine || line == 1) && p[i] == L'/' && p[i + 1] == L'*')
		{
			cmtPos = i;

			// [case: 66294] support for multiple block comments on a single line /* one */ /* two */ /* three */
			while (i < plen && p[i] == L'/' && p[i + 1] == L'*')
			{
				i += 2;
				for (; i < plen && !(p[i] == L'*' && p[i + 1] == L'/'); i++)
				{
					if (p[i] == lnBrkChar)
						line--;
				}

				i += 2; // skip over comment termination
				cmtPosEnd = i;

				for (; i < plen && iswspace(p[i]); i++)
				{
					if (p[i] == lnBrkChar)
						line--;
				}
			}

			emptyLine = FALSE;
		}

		if (p[i] == L'(')
			inParen = 1;
		else if (p[i] == L')')
			inParen = 0;
		else if (p[i] == lnBrkChar)
		{
			linePos = i + 1;
			emptyLine = true;
			line--;
		}
		else if (line > 1 && emptyLine && i < plen && (L';' == p[i] || L'#' == p[i]))
		{
			emptyLine = false;
			cmtPosEnd = cmtPos = 0;
		}
		else if (i < plen && (p[i] == L'{' || p[i] == L'}') && line > 1)
		{
			emptyLine = false;
			cmtPosEnd = cmtPos = 0;
		}

		if (!iswspace(p[i]))
			hadText = TRUE;
	}

	CStringW theComments;
	if (cmtPos || cmtPosEnd)
	{
		int commentLines = 0;
		theComments = L"\n";
		TokenW t = ftxt.Mid(cmtPos, cmtPosEnd - cmtPos);
		while (t.more() > 2)
		{
			if (commentLines > maxCommentLines)
			{
				theComments += L"...";
				break;
			}
			CStringW wd = t.read(L" \t\r\n") + L' ';
			if (wcschr(t.GetCharsSkipped(), L'\n'))
			{
				theComments += L'\n';
				commentLines++;
			}
			if (wd.GetLength() > 100)
				wd = wd.Mid(0, 97) + L"...";
			theComments += wd;
		}
	}
	else if (clipPos)
	{
		//		theComments = ftxt.Mid(clipPos, i-clipPos);
	}

	// eat some of the common single line garbage comments
	RemoveLinesThatContainFragment(theComments, L"//{{AFX");
	RemoveLinesThatContainFragment(theComments, L"//}}AFX");
	if (theComments.GetLength() == 1 && (theComments == L"\n" || theComments == L"\r"))
		theComments.Empty();
	else if (theComments.GetLength() == 2 && theComments == L"\r\n")
		theComments.Empty();

	return theComments;
}

// Gets the comment before a method...
CStringW GetCommentForSym(LPCSTR sym, bool includeFileString /*= false*/, int maxCommentLines /*= 6*/,
                          int vsAlreadyHasComment /*= 0*/, bool includeBaseMethod /*= false*/)
{
	WrapperCheckDecoy chk(g_pUsage->mJunk);
#if !defined(SEAN)
	// wrap mparse db's in case they are get nuked while running this (shouldn't happen anymore)
	try
#endif // !SEAN
	{
		// first, build list of locations to search for comments
		SymbolPosList posList;
		{
			token t = sym;
			t.ReplaceAll(TRegexp("<.*>"), string(""));
			EdCntPtr ed = g_currentEdCnt;
			if (ed)
				ed->LDictionary()->FindDefList(t.Str(), posList);
			GetDFileMP(gTypingDevLang)->LDictionary()->FindDefList(t.Str(), posList);
			GetSysDic()->FindDefList(t.Str(), posList);
			g_pGlobDic->FindDefList(t.Str(), posList);

			// [case: 164431] in Unreal Engine, definition of the function can have
			// _Implementation or _Validate in the name; that confuses VA since it doesn't
			// consider declaration and implementation to be the same function; strip
			// sym of those to search also comments of correct declaration
			if (Psettings->mUnrealEngineCppSupport)
			{
				bool isUESpecialFunction = true;
				WTString sym_stripped = sym;
				
				if (sym_stripped.contains("_Implementation"))
					sym_stripped.Replace("_Implementation", "");
				else if (sym_stripped.contains("_Validate"))
					sym_stripped.Replace("_Validate", "");
				else
					isUESpecialFunction = false;
				
				if (isUESpecialFunction)
				{
					token t2 = sym_stripped;
					t2.ReplaceAll(TRegexp("<.*>"), string(""));
					EdCntPtr ed2 = g_currentEdCnt;
					if (ed2)
						ed2->LDictionary()->FindDefList(t2.Str(), posList);
					GetDFileMP(gTypingDevLang)->LDictionary()->FindDefList(t2.Str(), posList);
					GetSysDic()->FindDefList(t2.Str(), posList);
					g_pGlobDic->FindDefList(t2.Str(), posList);
				}
			}

			posList.Sort();
		}

		// [case: 76734] clear system includes that aren't applicable to current solution settings
		if (gShellAttr->IsDevenv10OrHigher() && IsCFile(gTypingDevLang))
		{
			DefListFilter filt;
			posList = filt.FilterPlatformIncludes(posList);
		}

		// second, narrow down results and identify up to 2 locations to look for comments
		CStringW cmnt, srcComment;
		CStringW file, hdrFile;
		int line = 0, hdrLn = 0;
		{
			UINT previousFileId = 0;
			int previousLine = 0;
			int repeatCount = 0;
			SymbolPosList::const_iterator it;
			for (it = posList.begin(); it != posList.end(); ++it)
			{
				const CStringW tfile((*it).mFilename);
				if (!tfile.GetLength())
					continue;

				// [case: 53880] don't display comments from .rc files
				if (GetFileType(tfile) == RC)
					continue;

				if (previousFileId == (*it).mFileId && previousLine == (*it).mLineNumber)
				{
					if (++repeatCount > 10)
					{
						// [case: 67497]
						return CStringW();
					}

					// [case: 61994]
					continue;
				}

				repeatCount = 0;
				previousFileId = (*it).mFileId;
				previousLine = (*it).mLineNumber;

				if (Psettings->m_mouseOvers && Psettings->m_bGiveCommentsPrecedence && !file.GetLength() && GetFileType(tfile) != Header)
				{
					file = tfile; // search source first
					line = (*it).mLineNumber;
				}

				if (!hdrFile.GetLength() && (GetFileType(tfile) == Header || GetFileType(tfile) == JS))
				{
					hdrFile = tfile;
					hdrLn = (*it).mLineNumber;
				}
				else if (it != posList.begin() && GetFileType(tfile) == Header && file.IsEmpty())
				{
					// [case: 61994] inlines
					file = tfile; // treat later header as inline (typically higher line number in same file as hdrFile)
					line = (*it).mLineNumber;
				}
			}

			// final housekeeping
			if (!file.GetLength())
			{
				file = hdrFile;
				line = hdrLn;
				hdrFile.Empty();
			}
		}

		// third, handle dev11 comment duplication
		if (vsAlreadyHasComment && gShellAttr->IsDevenv11OrHigher() && IsCFile(gTypingDevLang))
		{
			EdCntPtr ed(g_currentEdCnt);
			const bool kInSrcFile = ed && file.CompareNoCase(ed->FileName()) == 0;
			// [case: 73217]
			// only add declaration comment
			if (file.GetLength() && hdrFile.GetLength())
			{
				// we found separate declaration and definition
				if (file == hdrFile)
				{
					if (hdrLn <= line)
						file.Empty();
					//					else
					//					{
					//						// no test case for this condition
					//						_asm nop;
					//					}
				}
				else if (kInSrcFile) // [case: 76734]
					file.Empty();
				else if (Psettings->m_mouseOvers && Psettings->m_bGiveCommentsPrecedence)
					hdrFile.Empty();
				else
				{
					// no test case for this condition
					return CStringW();
				}
			}
			else
			{
				// assume they already have it.
				// we believe they have something per vsAlreadyHasComment,
				// and we only see one location/position.
				// In dev11, they will use declaration comment if there is
				// no definition.
				// If there is a definition, they will only look at it (even
				// if it has no comment and even if there is a declaration
				// with a comment).
				return CStringW();
			}
		}

		// fourth, read identified locations and look for comments
		for (;;)
		{
			if (file.GetLength())
			{
				cmnt = ExtractLastCommentsBeforeLine(file, line, maxCommentLines, false);
				const CStringW endlineComment(ExtractLastCommentsBeforeLine(file, line, maxCommentLines, true));
				if (endlineComment.GetLength())
				{
					if (cmnt.GetLength() && cmnt != endlineComment)
						cmnt += endlineComment;
					else
						cmnt = endlineComment;
				}
			}

			if (hdrFile.GetLength())
			{
				srcComment = cmnt;
				cmnt.Empty();
				file = hdrFile;
				line = hdrLn;
				hdrFile.Empty();
			}
			else
				break;
		}

		// merge found comments
		if (srcComment.GetLength())
		{
			if (cmnt.GetLength() && !::AreSimilar(cmnt, srcComment))
				cmnt += srcComment;
			else
				cmnt = srcComment;
		}

		DType rootBaseDefinition, parentDefinition;
		if (includeBaseMethod)
		{
			DTypeList allRel;
			DTypeList relToSearch;
			InheritanceDb::GetInheritanceRecords(vaInheritsFrom, StrGetSymScope(sym), relToSearch);
			auto searchDepth = 0; // how deeply into the base class hierarchy we have searched
			constexpr auto maxSearchDepth =
			    15; // the maximum depth into the base class hierarchy to search [case: 141330]
			constexpr auto maxRelToSearch =
			    100; // the maximum number of potential base classes to collect [case: 141330]
			do
			{
				// build a list of base classes to search
				allRel.insert(allRel.cend(), relToSearch.begin(), relToSearch.end());
				DTypeList newRelToSearch;
				for (DType& rel : relToSearch)
				{
					DTypeList buff;
					InheritanceDb::GetInheritanceRecords(vaInheritsFrom, rel.Def(), buff);
					for (DType& dtype : allRel)
					{
						DTypeList::const_iterator it = std::find(buff.cbegin(), buff.cend(), dtype);
						if (it != buff.cend())
							buff.erase(it); // remove any base classes we have already seen [case: 141330]
					}
					newRelToSearch.insert(newRelToSearch.cend(), buff.begin(), buff.end());
				}
				relToSearch = std::move(newRelToSearch);
			} while (!relToSearch.empty() && allRel.size() < maxRelToSearch && ++searchDepth <= maxSearchDepth);
			allRel.FilterNonActiveSystemDefs();
			allRel.FilterDupesAndGotoDefs();
			allRel.FilterEquivalentDefs();
			// reverse the list so that base classes deepest in the inheritance hierarchy appear first
			allRel.reverse();
			for (DType& rel : allRel)
			{
				// search base classes for virtual methods with the same name as our method symbol
				DTypeList dtypes;
				GetDFileMP(gTypingDevLang)->FindExactList(rel.Def() + DB_SEP_STR + StrGetSym(sym), dtypes);
				dtypes.FilterNonActiveSystemDefs();
				dtypes.FilterDupesAndGotoDefs();
				dtypes.FilterEquivalentDefs();
				for (DType& dtype : dtypes)
				{
					if (dtype.IsVirtualOrExtern())
					{
						if (rootBaseDefinition.IsEmpty())
						{
							// [case: 117042] locate the original definition of the virtual method in a base class
							// first item should be the top of the hierarchy
							rootBaseDefinition = dtype;
						}
						else
						{
							// def is virtual (might also have override).
							// grab each subsequent dtype (after root), last one wins
							parentDefinition = dtype;
						}
					}
					else if (dtype.IsOverride())
					{
						// def is override but does not have virtual keyword.
						// grab each override dtype, last one wins
						parentDefinition = dtype;
					}
					else
					{
						_ASSERTE(!dtype.DefStartsWith("virtual"));
					}
				}
			}
		}

		// Append any Unreal Engine markup so it appears after comments.
		cmnt += ExtractUnrealMarkup(file, line);

		auto AddAdditionalComment = [maxCommentLines](const WTString& addlSourceScope, CStringW& cmnt) {
			const CStringW baseCmnt = GetCommentForSym(addlSourceScope, false, maxCommentLines, 0);
			if (baseCmnt.IsEmpty())
				return;

			if (!cmnt.IsEmpty())
				cmnt += '\n';

			// display source of this comment
			WTString prefix = CleanScopeForDisplay(addlSourceScope);
			if (IsCFile(gTypingDevLang))
				prefix.Replace(".", "::");
			cmnt += L"\n" + prefix.Wide();
			cmnt += baseCmnt;
		};

		// [case: 117042] show comments from base classes in tooltip
		if (!parentDefinition.IsEmpty())
			AddAdditionalComment(parentDefinition.SymScope(), cmnt);

		if (!rootBaseDefinition.IsEmpty())
			AddAdditionalComment(rootBaseDefinition.SymScope(), cmnt);

		CStringW retStr(cmnt);
		if (includeFileString && file.GetLength())
		{
			CStringW tmp;
			CString__FormatW(tmp, L"\nFile: %ls", (LPCWSTR)file);
			retStr += tmp;
		}
		return retStr;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("EDC2:");
		_ASSERTE(!"GetCommentForSym");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in GetCommentForSym");
		}
		return CStringW();
	}
#endif // !SEAN
}

CStringW GetCommentFromPos(int pos, int vsAlreadyHasComment, int* commentFlags, EdCntPtr ed = {})
{
#ifndef RAD_STUDIO
	_ASSERTE(gShellAttr->IsDevenv10OrHigher());
#endif
	*commentFlags = 0;

	if (!ed)
		ed = g_currentEdCnt;
	
	if (ed && Psettings->m_AutoComments && GlobalProject && !GlobalProject->IsBusy())
	{
		const WTString buf(ed->GetBuf());
		const int adjustedPos = AdjustPosForMbChars(buf, pos); // [case: 61871]
		if (adjustedPos >= buf.GetLength())
		{
			vLog("ERROR: GetCommentFromPos bad offset");
			return CStringW();
		}

		WTString pscope;
		MultiParsePtr mp(ed->GetParseDb());
		DTypePtr data = SymFromPos(buf, mp, adjustedPos, pscope);
		if (data)
		{
			const WTString symScope = data->SymScope();
			const bool includeBaseMethod = Psettings->m_mouseOvers && Psettings->m_bGiveCommentsPrecedence && data->MaskedType() & FUNC;
			const CStringW cmnt = ::GetCommentForSym(symScope, false, 20, vsAlreadyHasComment, includeBaseMethod);
			if (cmnt.IsEmpty())
				return cmnt;

			// [case: 66239]
			BOOL isManagedSym = data->IsDbNet();
			if (!isManagedSym)
			{
				CStringW symFile(gFileIdManager->GetFile(data->FileId()));
				const int symFileType = GetFileTypeByExtension(symFile);
				if (IsCFile(symFileType) && ed == g_currentEdCnt)
				{
					CStringW activeFile = ed->FileName();
					const int activeFileType = GetFileTypeByExtension(activeFile);
					if (IsCFile(activeFileType))
					{
						RWLockReader lck;
						const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
						for (Project::ProjectMap::const_iterator projIt = projMap.begin(); projIt != projMap.end();
						     ++projIt)
						{
							ProjectInfoPtr projInf = (*projIt).second;
							if (!projInf)
								continue;

							if (!symFile.IsEmpty() && projInf->ContainsFile(symFile))
							{
								if (projInf->CppUsesClr())
								{
									isManagedSym = true;
									break;
								}

								symFile.Empty();
							}

							if (!activeFile.IsEmpty() && projInf->ContainsFile(activeFile))
							{
								if (projInf->CppUsesClr())
								{
									isManagedSym = true;
									break;
								}

								activeFile.Empty();
							}

							if (symFile.IsEmpty() && activeFile.IsEmpty())
								break;
						}
					}
					else
						isManagedSym = true; // assume managed if active file not c/c++
				}
				else
					isManagedSym = true; // assume managed if sym not from c/c++ file
			}

			if (isManagedSym)
				*commentFlags = 1;

			return cmnt;
		}
	}
	return CStringW();
}

WTString EdCnt::GetCommentForSym(const WTString& sym, int maxCommentLines)
{
	return ::GetCommentForSym(sym, false, maxCommentLines);
}

// strip category and meta parameters from Unreal Engine markup
CStringW StripCategoryAndMetaParameters(CStringW parameters)
{
	parameters.MakeLower();
	int prevComma = 0;
	int parenDeep = 0;

	for (int i = 0; i < parameters.GetLength(); ++i)
	{
		// isolate each comma separated parameter, while ignoring commas that appear inside parenthesis
		const bool endOfString = i + 1 == parameters.GetLength();

		if ((parameters[i] == L',' && parenDeep == 0) || endOfString)
		{
			CStringW parameter;

			if (endOfString)
				parameter = parameters.Mid(prevComma,
				                           i - prevComma + 1); // include the last char as it is not a comma this time
			else
				parameter = parameters.Mid(prevComma, i - prevComma);

			parameter.TrimLeft();

			if (StartsWithNC(parameter, L"category") || StartsWithNC(parameter, L"meta"))
			{
				// strip the category / meta parameters
				if (endOfString && prevComma != 0)
					parameters.Delete(prevComma - 1,
					                  i - prevComma +
					                      2); // delete the previous comma, if there is one, as no more params follow
				else
					parameters.Delete(prevComma, i - prevComma + 1);

				i = prevComma;
			}
			else
			{
				prevComma = i + 1;
			}
		}
		else if (parameters[i] == L'(')
		{
			++parenDeep;
		}
		else if (parameters[i] == L')')
		{
			--parenDeep;
		}
	}

	return parameters.Trim();
}

// isolate categories from Unreal Engine markup
CStringW IsolateCategories(CStringW parameters)
{
	parameters.MakeLower();
	int prevComma = 0;
	int parenDeep = 0;

	for (int i = 0; i < parameters.GetLength(); ++i)
	{
		// isolate each comma separated parameter, while ignoring commas that appear inside parenthesis
		const bool endOfString = i + 1 == parameters.GetLength();

		if ((parameters[i] == L',' && parenDeep == 0) || endOfString)
		{
			CStringW parameter;

			if (endOfString)
				parameter = parameters.Mid(prevComma,
				                           i - prevComma + 1); // include the last char as it is not a comma this time
			else
				parameter = parameters.Mid(prevComma, i - prevComma);

			parameter.TrimLeft();

			if (StartsWithNC(parameter, L"category"))
			{
				int start = parameter.Find(L'"') + 1;

				if (start != -1)
				{
					int end = parameter.Find(L'"', start);

					if (end != -1)
					{
						// isolate the categories from between quotation marks
						return parameter.Mid(start, end - start).Trim();
					}
				}

				start = parameter.Find(L'=') + 1;

				if (start != -1)
				{
					// isolate the categories without quotation marks
					return parameter.Mid(start, parameter.GetLength() - start).Trim();
				}
			}
			else
			{
				prevComma = i + 1;
			}
		}
		else if (parameters[i] == L'(')
		{
			++parenDeep;
		}
		else if (parameters[i] == L')')
		{
			--parenDeep;
		}
	}

	return L"";
}

// isolate meta parameters from Unreal Engine markup
CStringW IsolateMetaParameters(CStringW parameters)
{
	parameters.MakeLower();
	int prevComma = 0;
	int parenDeep = 0;

	for (int i = 0; i < parameters.GetLength(); ++i)
	{
		// isolate each comma separated parameter, while ignoring commas that appear inside parenthesis
		const bool endOfString = i + 1 == parameters.GetLength();

		if ((parameters[i] == L',' && parenDeep == 0) || endOfString)
		{
			CStringW parameter;

			if (endOfString)
				parameter = parameters.Mid(prevComma,
				                           i - prevComma + 1); // include the last char as it is not a comma this time
			else
				parameter = parameters.Mid(prevComma, i - prevComma);

			parameter.TrimLeft();

			if (StartsWithNC(parameter, L"meta"))
			{
				int start = parameter.Find(L'(') + 1;

				if (start != -1)
				{
					int end = parameter.Find(L')', start);

					if (end != -1)
					{
						// isolate the meta parameters
						return parameter.Mid(start, end - start).Trim();
					}
				}
			}
			else
			{
				prevComma = i + 1;
			}
		}
		else if (parameters[i] == L'(')
		{
			++parenDeep;
		}
		else if (parameters[i] == L')')
		{
			--parenDeep;
		}
	}

	return L"";
}

// [case: 111093] modified form of GetCommentForSym
// [case: 116650] strips category and meta information from outParams
void GetUFunctionParametersForSym(LPCSTR sym, CStringW* outParams /*= nullptr*/, CStringW* outCategories /*= nullptr*/,
                                  CStringW* outMetaParams /*= nullptr*/)
{
	if (!outParams && !outCategories && !outMetaParams)
		return;

	// first, build list of locations to search for comments
	SymbolPosList posList;
	{
		token t = sym;
		t.ReplaceAll(TRegexp("<.*>"), string(""));
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
			ed->LDictionary()->FindDefList(t.Str(), posList);
		GetDFileMP(gTypingDevLang)->LDictionary()->FindDefList(t.Str(), posList);
		GetSysDic()->FindDefList(t.Str(), posList);
		g_pGlobDic->FindDefList(t.Str(), posList);
		posList.Sort();
	}

	// [case: 76734] clear system includes that aren't applicable to current solution settings
	if (gShellAttr->IsDevenv10OrHigher() && IsCFile(gTypingDevLang))
	{
		DefListFilter filt;
		posList = filt.FilterPlatformIncludes(posList);
	}

	// second, narrow down results and identify up to 2 locations to look for comments
	CStringW srcComment;
	CStringW file, hdrFile;
	int line = 0, hdrLn = 0;
	{
		UINT previousFileId = 0;
		int previousLine = 0;
		int repeatCount = 0;
		SymbolPosList::const_iterator it;
		for (it = posList.begin(); it != posList.end(); ++it)
		{
			const CStringW tfile((*it).mFilename);
			if (!tfile.GetLength())
				continue;

			// [case: 53880] don't display comments from .rc files
			if (GetFileType(tfile) == RC)
				continue;

			if (previousFileId == (*it).mFileId && previousLine == (*it).mLineNumber)
			{
				if (++repeatCount > 10)
				{
					// [case: 67497]
					if (outParams)
						outParams->Empty();

					if (outCategories)
						outCategories->Empty();

					if (outMetaParams)
						outMetaParams->Empty();

					return;
				}

				// [case: 61994]
				continue;
			}

			repeatCount = 0;
			previousFileId = (*it).mFileId;
			previousLine = (*it).mLineNumber;

			if (!file.GetLength() && GetFileType(tfile) != Header)
			{
				file = tfile; // search source first
				line = (*it).mLineNumber;
			}

			if (!hdrFile.GetLength() && GetFileType(tfile) == Header)
			{
				hdrFile = tfile;
				hdrLn = (*it).mLineNumber;
			}
			else if (it != posList.begin() && GetFileType(tfile) == Header && file.IsEmpty())
			{
				// [case: 61994] inlines
				file = tfile; // treat later header as inline (typically higher line number in same file as hdrFile)
				line = (*it).mLineNumber;
			}
		}

		// final housekeeping
		if (!file.GetLength())
		{
			file = hdrFile;
			line = hdrLn;
			hdrFile.Empty();
		}
	}

	// the UFunction parameters are retrieved by isolating the portion within the parens of UFUNCTION(...) markup
	CStringW parameters = ExtractUnrealMarkup(file, line);
	int openParen = parameters.Find('(');

	if (openParen != -1 && parameters.GetLength() - openParen - 2 > 0)
		parameters = parameters.Mid(openParen + 1, parameters.GetLength() - openParen - 2);

	if (outParams)
		*outParams = StripCategoryAndMetaParameters(parameters);

	if (outCategories)
		*outCategories = IsolateCategories(parameters);

	if (outMetaParams)
		*outMetaParams = IsolateMetaParameters(parameters);
}

CStringW GetDefFromPos(int pos, EdCntPtr ed = {})
{
	if (!ed)
		ed = g_currentEdCnt;
	
	// Called from MEF side for QuickInfo tip text. [case=42348]
	if (ed && Psettings && gTypingDevLang != VB && GlobalProject && !GlobalProject->IsBusy())
	{
		WTString tipText;
		LPPOINT pPt = nullptr;

#ifndef RAD_STUDIO
		// Don't add comments to text, 2010 will add them later.
		TempSettingOverride<bool> _t(&Psettings->m_AutoComments, FALSE, true);
#else
		// in RAD studio we don't use mouse position directly
		CPoint pt;
		LONG line, column;
		ed->PosToLC(pos, line, column);
		pt.x = line;
		pt.y = column;
		pPt = &pt;
#endif

		if (ed->GetTypeInfo(pPt, tipText, nullptr))
		{
			// [case: 141726]
			const CStringW tipTextW(DecodeTemplates(tipText).Wide());
			return tipTextW;
		}
	}
	return CStringW();
}

CStringW GetExtraDefInfoFromPos(int pos, EdCntPtr ed = {})
{
#ifndef RAD_STUDIO
	_ASSERTE(gShellAttr->IsDevenv10OrHigher());
#endif

	if (!ed)
		ed = g_currentEdCnt;

	// [case: 72729] vs2010+ implementation of case=8771
	// Called from MEF side to augment QuickInfo tip text
	if (ed && Psettings && GlobalProject && !GlobalProject->IsBusy())
	{
		const WTString buf(ed->GetBuf());
		const int adjustedPos = AdjustPosForMbChars(buf, pos); // [case: 61871]

		WTString pscope;
		MultiParsePtr mp(ed->GetParseDb());
		DTypePtr data = SymFromPos(buf, mp, adjustedPos, pscope);
		CStringW ret(ed->GetExtraDefInfo(data).Wide());
		if (ret.IsEmpty() && gShellAttr)
		{
			if ((gShellAttr->IsDevenv14OrHigher() && CS == gTypingDevLang) ||
			    (gShellAttr->IsDevenv16OrHigher() && Src == gTypingDevLang))
			{
				// [case: 90119] append va scope tooltip to C# vs2015 tooltip
				// [case: 135843] append va scope tooltip to C++ vs2019p2 tooltip
				ret = GetDefFromPos(pos);
				if (0 != ret.Find(L"// Context:\r\n"))
					ret.Empty(); // wasn't a scope tooltip, so clear it
			}
		}
		return ret;
	}
	return CStringW();
}

bool EdCnt::HasBookmarks()
{
	int line = CurLine();
	if (m_LnAttr->NextBookmarkFromLine(line))
	{
		return TRUE;
	}
	return FALSE;
}

// [case: 148145]
bool IsUEMarkedType(LPCSTR sym)
{	
	// first, build list of locations to search for markup
	SymbolPosList posList;
	{
		token t = sym;
		t.ReplaceAll(TRegexp("<.*>"), string(""));
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
			ed->LDictionary()->FindDefList(t.Str(), posList);
		GetDFileMP(gTypingDevLang)->LDictionary()->FindDefList(t.Str(), posList);
		GetSysDic()->FindDefList(t.Str(), posList);
		g_pGlobDic->FindDefList(t.Str(), posList);
		posList.Sort();
	}

	// clear system includes that aren't applicable to current solution settings
	if (gShellAttr->IsDevenv10OrHigher() && IsCFile(gTypingDevLang))
	{
		DefListFilter filt;
		posList = filt.FilterPlatformIncludes(posList);
	}

	// second, narrow down results and identify up to 2 locations to look for comments
	CStringW srcComment;
	CStringW file, hdrFile;
	int line = 0, hdrLn = 0;
	{
		UINT previousFileId = 0;
		int previousLine = 0;
		int repeatCount = 0;
		SymbolPosList::const_iterator it;
		for (it = posList.begin(); it != posList.end(); ++it)
		{
			const CStringW tfile((*it).mFilename);
			if (!tfile.GetLength())
				continue;

			// don't display comments from .rc files
			if (GetFileType(tfile) == RC)
				continue;

			if (previousFileId == (*it).mFileId && previousLine == (*it).mLineNumber)
			{
				if (++repeatCount > 10)
				{
					// limit search repeats
					return false;
				}

				continue;
			}

			repeatCount = 0;
			previousFileId = (*it).mFileId;
			previousLine = (*it).mLineNumber;

			if (!file.GetLength() && GetFileType(tfile) != Header)
			{
				file = tfile; // search source first
				line = (*it).mLineNumber;
			}

			if (!hdrFile.GetLength() && GetFileType(tfile) == Header)
			{
				hdrFile = tfile;
				hdrLn = (*it).mLineNumber;
			}
			else if (it != posList.begin() && GetFileType(tfile) == Header && file.IsEmpty())
			{
				file = tfile; // treat later header as inline (typically higher line number in same file as hdrFile)
				line = (*it).mLineNumber;
			}
		}
	}

	// first check header file if it exists
	if (hdrFile.GetLength())
	{
		CStringW hdrParameters = ExtractUnrealMarkup(hdrFile, hdrLn);
		if (hdrParameters.Find(L"UCLASS") != -1 || hdrParameters.Find(L"USTRUCT") != -1 ||
		    hdrParameters.Find(L"UFUNCTION") != -1 || hdrParameters.Find(L"UENUM") != -1)
		{
			// found markup in .h file
			return true;
		}
	}
	
	// markup not found in header file, check source file
	if (file.GetLength())
	{
		CStringW parameters = ExtractUnrealMarkup(file, line);
		if (parameters.Find(L"UCLASS") != -1 || parameters.Find(L"USTRUCT") != -1 ||
		    parameters.Find(L"UFUNCTION") != -1 || parameters.Find(L"UENUM") != -1)
		{
			// found markup in .cpp file
			return true;
		}
	}

	// type is not marked or not detected as marked
	return false;
}

// #if (defined _DEBUG && !defined SEAN)
// #define AUTO_INSERT_SEMICOLON
// #endif // _DEBUG

int EdCnt::FixUpFnCall()
{
	if (!Psettings->m_enableVA)
		return 0;
	DEFTIMERNOTE(FixUpFnCallTimer, NULL);
	INT ndefs = 0;
	BOOL hasArgs = FALSE;
	BOOL hasParens = TRUE;
	WTString tmpWd(CurWord());
	if (tmpWd != WordLeftOfCursor() || (tmpWd.GetLength() > 10 && tmpWd.Find("_cast") == (tmpWd.GetLength() - 5)))
	{
		return 0; // make sure not in the middle of word
	}

	// [case: 7802] don't add parens when taking address of function
	tmpWd = CurWord(-1);
	if (tmpWd == "&")
	{
		// &foo
		return 0;
	}
	else
	{
		for (int idx = -2; idx > -10 && tmpWd == "::"; --idx)
		{
			tmpWd = CurWord(idx);
			if (tmpWd.IsEmpty())
				break;

			if (tmpWd == "&")
			{
				// &::foo
				return 0;
			}

			if (!ISALPHA(tmpWd[0]))
				break;

			tmpWd = CurWord(--idx);
			if (tmpWd == "&")
			{
				// &foo::bar
				return 0;
			}
		}
	}

	const WTString curLn(GetLine(CurLine()));
	if (-1 != curLn.Find("using "))
	{
		// [case: 69880]
		return 0;
	}

	m_lastScopePos = 0;
	WTString s = CurScopeWord();

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	MultiParsePtr mp(GetParseDb());
	if (mp && !mp->m_ParentScopeStr.IsEmpty() && mp->m_ParentScopeStr[0] == '_')
	{
		if (StartsWith(mp->m_ParentScopeStr, "__property"))
		{
			// [case: 133417]
			// methods are referenced, not called, in __property definitions
			return 0;
		}
	}
#endif

	{
		// See if all def's have empty args
		token t = s;
		while (!hasArgs && t.more() > 1)
		{
			WTString def = t.read("\f");
			int p1 = def.Find('(');
			int p2 = def.Find(')');
			if (p1 == -1 || p2 == -1)
			{
				hasParens = FALSE;
				continue;
			}
			if (p1 < p2)
			{
				WTString args = def.Mid(p1 + 1, p2 - p1 - 1);
				args.Trim();
				if (args.GetLength() && args != "void" && args != "VOID")
					hasArgs = TRUE;
			}
		}
	}

	{
		// see if all def's have parens, if not, grab the last one?
		token t = s;
		while (t.more())
		{
			WTString def = t.read("\f");
			if (def.GetLength())
			{
				ndefs++;
				s = def;
				if (def.Find('(') == -1)
					break;
			}
		}
	}

	// TODO: Nuke this or parse argument template correctly
	int p1 = s.find_first_of("{;=(");
	int p2 = s.find_first_of(");{");
	const WTString kDefaultVoidArgs(Psettings->mFunctionCallParens);
	WTString args;

	if (p1 != NPOS && s[p1] == '(')
	{
		if (p2 == NPOS || s[p2] != ')' || s.find("(", p2) != NPOS)
		{
			if (p2 != NPOS && s[p2] == ')')
			{
				// we're here because s.find("(", p2) != NPOS
				p1 = s.find("(", p2);
				if (s.find(")", p1) == p1 + 1 && s.find("(", p1 + 1) == NPOS)
					args = kDefaultVoidArgs; // there were 2 defs, but both without args - inline declaration
				else
					args = "("; // no clean string
			}
			else
				args = "("; // no clean string to prompt...
		}
		else
		{
			args = s.Mid(p1, (p2 - p1) + 1);
			if (args.FindNoCase("void") != -1)
			{
				// strip out void and see if args should just be kDefaultVoidArgs
				token def = LPCTSTR(args.c_str());
				string spacer("\f");
				def.ReplaceAll(TRegexp("void"), spacer);
				def.ReplaceAll(TRegexp("VOID"), spacer);
				def.ReplaceAll(TRegexp("[ ]+"), spacer);
				def.ReplaceAll(TRegexp("[\f]+"), OWL_SPACESTR);
				if (!strcmp(def.c_str(), "( )") || !strcmp(def.c_str(), "()"))
					args = kDefaultVoidArgs;
			}
			else if (args == "( )") // code wizard generated code
				args = kDefaultVoidArgs;
		}
	}

	// expand unicode #define'd API's
	const DType dt(GetSymDtype());
	if ((dt.type() == FUNC || GetSymDef().contains("API")) && args.length())
	{
		uint curPos = CurPos();
		const char prevChar = curPos ? CharAt(curPos - 1) : '\0';
		char curChar = CharAt(curPos);
		if (curChar == '(' || (curChar == ' ' && CharAt(curPos + 1) == '('))
		{
			// don't insert anything if '(' is already out there
			if (curChar == ' ')
				++curPos;
			int moveTo = 1;
			if (args == "()" || args == kDefaultVoidArgs)
			{
				if (CharAt(curPos + 1) == ')')
					moveTo = 2;
				else if (CharAt(curPos + (args.GetLength() - 1)) == ')')
					moveTo = args.GetLength();
			}

			if (curChar == ' ' && ';' == CharAt(curPos + moveTo))
				++moveTo;

			SetPos(curPos + moveTo);
		}
		else if (prevChar != ')')
		{
			// insert args as built above
			//  check to see if we should leave space between fn and parens
			WTString gap;
			DWORD cnt = Psettings->m_fnParenGap;
			while (cnt--)
				gap += " ";

			if (!Psettings->IsAutomatchAllowed(true))
			{
				// insert nothing like msdev   -Jer
			}
			else if (dt.type() == FUNC && dt.IsConstructor())
				; // [case: 29030] don't do auto-parens for ctors
			else if (hasParens && !hasArgs)
			{
				args = kDefaultVoidArgs;
#ifdef AUTO_INSERT_SEMICOLON
				MultiParsePtr mp(GetParseDb());
				if (!mp->m_inParenCount)
					args += ";";
#endif // AUTO_INSERT_SEMICOLON
				if (gap.GetLength())
					args.prepend(gap.c_str());
				InsertW(args.Wide());
			}
			else if (!Is_VB_VBS_File(m_ftype))
			{
				WTString ldef = GetSymDef();
				ldef.MakeLower();
				// delegate hack, delegates should be types in mparse parser -Jer
				if (!ldef.contains("delegate"))
				{
#ifdef AUTO_INSERT_SEMICOLON
					const WTString bb(GetBufConst());
					long idx = GetBufIndex(bb, CurPos());
					if (strchr("\r\n", bb[idx]))
						args = gap + kDefaultVoidArgs + ";";
					else
#endif // AUTO_INSERT_SEMICOLON
						args = gap + kDefaultVoidArgs;
					InsertW(args.Wide());
					if (Psettings->IsAutomatchAllowed(true) && Scope().c_str()[0] == DB_SEP_CHR)
						m_autoMatchChar = ')';

					if (args.contains(";"))
						SetPos(CurPos() - 2);
					else
						SetPos(CurPos() - 1);
					m_undoOnBkSpacePos = CurPos();
				}
				m_typing = gTypingAllowed; // Allow ScopeSuggestions after "(" case=8612
				Scope();                   // refresh tooltip info
				if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
				{
					if (!Psettings->mVaAugmentParamInfo || Psettings->m_bUseDefaultIntellisense ||
					    Psettings->mSuppressAllListboxes || (Psettings->mRestrictVaListboxesToC && !(IsCFile(m_ftype))))
						::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_PARAMINFO, 0); // Display theirs
					else
						SetTimer(ID_ARGTEMPLATE_DISPLAY_TIMER, 500, NULL); // Display ours
				}
			}
		}
	}

	SetStatusInfo(); // updates context and def
	SetTimer(ID_ARGTEMPLATE_TIMER, 200, NULL);
	return 1;
}

void EdCnt::InsertTabstr()
{
	DEFTIMERNOTE(InsertTabstrTimer, NULL);
	if (HasVsNetPopup(TRUE))
		return;
	int pos = GetBufIndex((long)CurPos());
	if (gShellAttr->IsDevenv())
	{
		SendVamMessage(VAM_INDENT, NULL, NULL);
	}
	else if (m_pDoc)
	{
		m_pDoc->Indent();
		BufInsert(pos, TabStr.c_str());
	}
	else
		Insert(TabStr.c_str());
	OnModified();
}

void EdCnt::OpenOppositeFile()
{
	DEFTIMERNOTE(OpenOppositeFileTimer, NULL);
	CStringW fname(FileName());
	FileList possibleMatches;

	WrapperCheckDecoy chk(g_pUsage->mGotos);
	::GetBestFileMatch(fname, possibleMatches);
	if (0 == possibleMatches.size())
	{
		return;
	}
	else if (1 == possibleMatches.size())
	{
		::DelayFileOpen(possibleMatches.begin()->mFilename);
		return;
	}

	CPoint pt(GetPoint(posBestGuess));
	vClientToScreen(&pt);

	PopupMenuXP xpmenu;
	FileList::const_iterator it;
	uint idx = 1;
	for (it = possibleMatches.begin(); it != possibleMatches.end(); ++it)
	{
		CStringW txt = (*it).mFilename;
		CStringW filePath = ::Path(txt);
		CStringW fileName = ::Basename(txt);
		txt = FTM_DIM + filePath + CStringW("\\") + FTM_BOLD + fileName + FTM_NORMAL;

		txt = ::BuildMenuTextHexAcceleratorW(idx, txt);
		xpmenu.AddMenuItemW(idx++, MF_BYPOSITION, txt);
	}

	PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
	uint result;
	{
		TempTrue tt(m_contextMenuShowing);
		result = (uint)xpmenu.TrackPopupMenuXP(this, pt.x, pt.y);
		if (!result)
			return;
	}

	idx = 1;
	for (it = possibleMatches.begin(); it != possibleMatches.end(); ++it)
	{
		if (idx++ != result)
			continue;

		const CStringW filepath(gFileIdManager->GetFileForUser((*it).mFileId));
		if (filepath.GetLength())
		{
			DelayFileOpen(filepath);
			VAProjectAddFileMRU(filepath, mruFileOpen);
		}

		break;
	}
}

extern void RTFCopy(EdCnt* ed, WTString* txtToFormat = NULL);
void EdCnt::OnRTFCopy(LPARAM wholeFile)
{
	DISABLECHECK();
	long p1, p2;
	CWaitCursor cur;
	CheckForSaveAsOrRecycling();
	GetSel(p1, p2);
	if (p1 == p2 || wholeFile)
	{
		SetSelection(0, NPOS);
		WTString txt = m_pDoc->GetText();
		SetSelection(p1, p2);
		RTFCopy(this, &txt);
	}
	else
	{
		SendMessage(WM_COMMAND, ID_EDIT_COPY);
		RTFCopy(this);
	}
}

void EdCnt::DisplayScope()
{
	if (!GlobalProject || GlobalProject->IsBusy())
	{
		// prevent pause on UI thread during db init
		bool displayScope = true;
		if (!g_pCSDic && !g_pMFCDic)
			displayScope = false;
		else if (g_pMFCDic && !g_pMFCDic->m_loaded && IsCFile(gTypingDevLang))
			displayScope = false;
		else if (g_pCSDic && !g_pCSDic->m_loaded && Defaults_to_Net_Symbols(gTypingDevLang))
			displayScope = false;

		if (!displayScope)
		{
			// [case: 61441] this call causes minihelp to reparent after alt+o during db init
			if (m_hParentMDIWnd)
			{
				const CStringW fname(FileName());
				UpdateMinihelp((LPCWSTR)Basename(fname), (LPCWSTR)fname);
			}
			return;
		}
	}

	WTString scp;
	WTString scope = Scope();
	MultiParsePtr mp = GetParseDb();
	if (mp->IsStopped())
	{
		return;
	}
	if (scope.contains(":PP:"))
	{
		ClearMinihelp();
		return;
	}
	if (scope.GetLength() && scope[0] != DB_SEP_CHR)
		scope = ""; // m_pmparse->m_rscope;	// in comment or string, get prev scope

	scope = DecodeScope(scope);
	if (scope.GetLength() > 1)
	{
		if (scope[0] == DB_SEP_CHR)
		{
			if (scope.GetLength() > 4 && scope[1] == 'B' && scope[2] == 'R' && scope[3] == 'C' && scope[4] == '-')
			{
				// removing the initial : breaks the :BRC- match below
				scope = "{" + scope.Mid(3);
			}
			else
				scp = scope.Mid(1).c_str(); // strip first DB_SEP_CHR
		}
		else
			scp = scope.c_str();
	}
	int l = scp.GetLength();
	if (l && scp[l - 1] == DB_SEP_CHR) // remove last DB_SEP_CHR
		scp = scp.Mid(0, l - 1);
	// TODO: turn single ':' into '::'
	WTString cwd = CurWord();
	if (!m_hasSel && ISCSYM(cwd.c_str()[0]))
	{
		ClearMinihelp();
		return;
	}

	if (scp.GetLength() && scope.GetLength())
	{
		///////////////////
		// display line containing open brace
		scp.ReplaceAll(":BRC-", ":{-", false);
		scp = CleanScopeForDisplay(scp);
		m_lastMinihelpDef = CleanDefForDisplay(mp->m_ParentScopeStr, m_ScopeLangType);
		UpdateMinihelp((LPCWSTR)scp.Wide(), (LPCWSTR)m_lastMinihelpDef.Wide());
	}
	else
	{
		const CStringW fname(FileName());
		UpdateMinihelp((LPCWSTR)Basename(fname), (LPCWSTR)fname);
	}
}

static LPCSTR g_mouseBufPtr = NULL;

bool EdCnt::GetTypeInfo(LPPOINT pPoint, WTString& typeInfo, CPoint* _displayPt)
{
	LogElapsedTime let("Ed:GTI", 50);

	CPoint dummy;
	CPoint& displayPt = _displayPt ? *_displayPt : dummy;

	DISABLECHECKCMD(false);
	CAN_USE_NEW_SCOPE_Decoy_DontReallyUseMe(); // decoy call - prevents linker from removing decoy body
	const WTString curBuf(GetBuf());
	MultiParsePtr mp(GetParseDb());
	if (mp->GetFilename().IsEmpty() || GlobalProject->IsBusy() ||
	    (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()) || !curBuf.GetLength())
		return false; // Don't run until the all files are parsed.

#ifndef RAD_STUDIO
    // Get tooltip text of symbol under pointer or symbol at caret
	// mouseovers can be disabled - but still allow debug evals of defines
	if (!pPoint && !Psettings->m_mouseOvers)
		return false;
	//		VirtSpaceFix vs(this);
	// save cur pos
	long p1, p2;
	GetSel(p1, p2);
	// move cursor under pointer and get def
	CPoint pt;
	if (!pPoint)
	{
		GetCursorPos(&pt);
		vScreenToClient(&pt);
		if (pt.x <= (int)Psettings->m_borderWidth)
			return false;
		// pt.x += Psettings->m_charWidth;
	}
	else
	{
		pt = *pPoint;
		// ScrollCaret(false);
		// pt =  GetCaretPos();
		// pt.x += LMARGIN;
		// pt.y += Psettings->FontSize;
	}
	CRect rc;
	vGetClientRect(&rc);
	if (!rc.PtInRect(pt))
		return false;
	displayPt = pt;
	/////////////////
	CPoint ptx = pt;
	long p3 = CharFromPos(&ptx); // sets ptchar to actual char pos
	static WTString sMouseBuf;
	sMouseBuf = curBuf;
	p3 = GetBufIndex(sMouseBuf, p3);
	const LPCSTR buf = sMouseBuf.c_str();
	LPCSTR lintPtr = p3 ? &buf[p3 - 1] : &buf[p3];

	g_mouseBufPtr = lintPtr;
	if (!g_mouseBufPtr)
	{
		// Word Wrapping may be on, lets go back to the start
		g_mouseBufPtr = buf;
	}
	if (g_mouseBufPtr && lintPtr != g_mouseBufPtr)
	{
		p3 = ptr_sub__int(g_mouseBufPtr, buf) + 1;
		g_mouseBufPtr = nullptr;
	}
	else if (pt.x > (displayPt.x + (long)(Psettings->FontSize * 2)) ||
	         pt.x < (long)(displayPt.x - (Psettings->FontSize * 2)))
		return false; // cursor before beginning, or past end of line

	if (!CAN_USE_NEW_SCOPE())
		return false;
#else
	if (!pPoint)
		return false;

	static WTString sMouseBuf;
	sMouseBuf = curBuf;

	// in RAD Studio pPoint in Line and Column!
	long p3 = GetBufIndex(sMouseBuf, TERRCTOLONG(pPoint->x, pPoint->y));
	const LPCSTR buf = sMouseBuf.c_str();
	LPCSTR lintPtr = p3 ? &buf[p3 - 1] : &buf[p3];
#endif

	WTString pscope;
	DTypePtr data;
	bool isAtBrace = false;
	bool isResWord = false;
	if (lintPtr[0] == '}' || lintPtr[1] == '}')
	{
		// [case: 72732]
		isAtBrace = true;
	}
	else
	{
		data = SymFromPos(curBuf, mp, p3, pscope, false);
		if (data && RESWORD == data->type())
		{
			// [case: 72732]
			isResWord = true;
		}
		else if (CS == gTypingDevLang)
		{
			// If C# doesn't know, don't guess, guessing also interferes with "FeatureBuilder" case=46640
			return false;
		}
	}

	if (!data || isResWord)
	{
		if (Psettings && Psettings->m_mouseOvers && Psettings->mScopeTooltips && p3 && (!ISCSYM(curBuf[(uint)p3]) || isResWord))
		{
			if (isAtBrace)
			{
				// want scope at "}" rather than ";" when hovering over "};" at end of class
				if (p3 > 1 && lintPtr[0] == '}' && lintPtr[1] == ';' &&
				    (::wt_isspace(lintPtr[-1]) || ';' == lintPtr[-1]))
					--p3;
			}

			// Display scope at cursor if hovering over '}' or various keywords
			long pp = p3;
			if (pp && wt_isspace(curBuf[(uint)pp]))
				pp--; // hovering to right of char, back up to char
			displayPt = GetCharPos(pp);
			if (pp > 1 && !isAtBrace && curBuf[(uint)pp] == ';' && curBuf[uint(pp - 1)] == '}')
			{
				// want scope at "}" rather than ";" when hovering over "};" at end of class
				--pp;
			}

#ifndef RAD_STUDIO
			bool doScopeTooltip = strchr("}", curBuf[(uint)pp]) &&
			                      abs(pt.x - displayPt.x) < (g_FontSettings->GetCharWidth() *
			                                                 2); // make sure not hovering in far off whitespace.
			if (!doScopeTooltip && isResWord)
			{
				_ASSERTE(data);
				const WTString s(data->Sym());
				if (s == "class" || s == "struct" || s == "namespace" || s == "enum" || s == "if" || s == "else" ||
				    s == "while" || s == "for" || s == "return" || s == "break" || s == "continue" || s == "switch" ||
				    s == "case" || s == "default" || s == "throw" || s == "try" || s == "catch" || s == "goto" ||
				    s == "yield" || s == "co_yield" || s == "co_return")
					doScopeTooltip = true;
			}

			// [case: 72732]
			if (doScopeTooltip)
#endif
			{
				WTString scopeContext = QuickScopeContext(curBuf, pp, gTypingDevLang);
				if (scopeContext == "{" || scopeContext == "}" || scopeContext == "};")
					scopeContext.Empty();
				if (scopeContext.GetLength())
				{
					vClientToScreen(&displayPt);
					displayPt.y += g_FontSettings->GetCharHeight();
					// "// Context:" is checked in
					// WholeTomatoSoftware.VisualAssist.VaQuickInfoSource.AugmentQuickInfoSession
					typeInfo = WTString("// Context:\r\n") + scopeContext;
					static WTString sTestLoggerCachedStr;
					if (gTestLogger)
					{
						if (sTestLoggerCachedStr != typeInfo)
						{
							// vs2015 15.3 C# causes multiple calls for same info making expected
							// results unpredictable, so only log when the text changes.
							sTestLoggerCachedStr = typeInfo;
							gTestLogger->LogStr("Scope tooltip text:\r\n" + typeInfo + "\r\n");
						}
					}
					else if (!sTestLoggerCachedStr.IsEmpty())
						sTestLoggerCachedStr.Empty();
					return true;
				}
			}
		}

		return false;
	}

	if (Is_Tag_Based(m_ftype) || m_ScopeLangType == JS || m_ScopeLangType == PHP)
	{
		if (data->IsSystemSymbol() || !data->inproject())
			return false; // Only display tips for items in the project
	}

	data->LoadStrs();
	const WTString symScope = data->SymScope();
	const uint sType = data->MaskedType();
	const uint sAttr = data->Attributes();
	const WTString dataDef(data->Def());
	token t = CleanDefForDisplay(dataDef, m_ScopeLangType).c_str(); // (cw != "}")?GetSymDef():m_pmparse->m_lastRedBcl;
	if (DEFINE == sType)
	{
		const WTString apiDef(GetDefsFromMacro(data->SymScope(), dataDef));
		if (apiDef.GetLength())
			t = apiDef;
	}

	{
		// [case: 8771] include def if var is of type typedef or macro
		WTString addlTipTxt(GetExtraDefInfo(data));
		if (!addlTipTxt.IsEmpty())
		{
			WTString oldTip(t.c_str());
			t = oldTip + "\f" + addlTipTxt;
		}
	}

	WTString cmnt;
	if (Psettings->m_AutoComments)
	{
		cmnt = GetCommentForSym(symScope, 20);
		if (cmnt.GetLength() && pPoint)
			cmnt.prepend("\r\n");
	}

	if (1)
	{
		/* allow tooltips in debug mode*/
		char txt[255];
		::GetWindowText(MainWndH, txt, 255);
		// Changed from [break] to fix non english ide's Case 454: multiple tooltips during debug
		// if we see "] - [", we assume the debugger is running [vc6]
		if (strstr(txt, "] - ["))
		{
			if (!strstr(txt, "[run] -"))          // allow tooltips if [run], english only
				if (!t.Str().contains("#define")) // allow #defines to show when debugging
					return false;
		}
	}

	if (t.Str().GetLength() > 2 && pscope[0] == DB_SEP_CHR) // skip comments and strings
	{
		// if debugging don't do auto type info for anything
		//  except defines - if it's a define, then the mouseover
		//  is not auto type info, it's a debug evaluation
		if (t.Str().Find("HIDETHIS") != -1)
			return false;
		else if (symScope.find(":VAunderline") == 0)
			return false;

		// try to add class
		WTString defStr(t.Str());
		defStr.ReplaceAll("::", DB_SEP_STR); // [case: 863]
		if ((VAR == sType || FUNC == sType) && !(sAttr & V_LOCAL) && defStr.Find(symScope.Mid(1)) == -1)
		{
			// skip initial :
			const WTString tmp = symScope.Mid(1);
			// find second :
			const int chPos = tmp.Find(DB_SEP_CHR);
			// find def substr that matches scope2 after the :
			const int defPos = defStr.Find(symScope.Mid(chPos + 2));
			if (defPos != -1 && chPos != -1)
			{
				token tsym = symScope.Mid(1);
				tsym.ReplaceAll(":", "::");
				t.ReplaceAll(StrGetSym(symScope), tsym.c_str(), TRUE);
			}
		}

		// the tooltip won't display if we don't have focus
		// display tooltip
		::BeautifyDefs(t);
		vClientToScreen(&displayPt);
		// Always display below and to the left of mouse or caret.
		if (!gShellAttr->IsDevenv10OrHigher() || !pPoint) // Hack for [case=42350]
		{
			displayPt.y += g_FontSettings->GetCharHeight();
			displayPt.x -= g_FontSettings->GetCharHeight() * 2;
		}
		bool show = true;

#ifndef RAD_STUDIO
		if (!pPoint)
		{
			GetCursorPos(&pt);
			CRect rc2;
			vGetClientRect(&rc2);
			vClientToScreen(&rc2);
			if (!rc2.PtInRect(pt))
				show = false;
		}
#endif

		if (show)
		{
			WTString tip = t.read("\f");
			while (t.more() > 2)
			{
				tip += WTString(" \n \n") + t.read();
			}

			if (cmnt.GetLength())
				tip += WTString(" \n \n") + cmnt;

			typeInfo = tip;
			return true;
		}
	}
	return false;
}

WTString EdCnt::GetExtraDefInfo(DTypePtr data)
{
	WTString addlTipTxt;
	if (!data)
		return addlTipTxt;

	if (!(IsCFile(gTypingDevLang)))
		return addlTipTxt;

	if (LINQ_VAR != data->type() && VAR != data->type())
		return addlTipTxt;

	// [case: 8771] include def if var is of type typedef or macro
	WTString baseType = ::GetTypesFromDef(data.get(), (int)data->type(), gTypingDevLang);
	if (baseType.IsEmpty())
		return addlTipTxt;

	// eat \f terminator
	baseType = baseType.Left(baseType.GetLength() - 1);
	if (!baseType.IsEmpty() && baseType.Find('\f') == -1)
	{
		MultiParsePtr mp(GetParseDb());
		DType* baseData = mp->FindExact(baseType);
		if (!baseData)
		{
			// check in the scope of the variable if not found globally
			const WTString symScope(data->SymScope());
			WTString scp(::StrGetSymScope(symScope));
			baseData = mp->FindSym(&baseType, &scp, NULL);
		}

		if (baseData && !baseData->IsSystemSymbol())
		{
			const uint bdType = baseData->MaskedType();
			if (TYPE == bdType || DEFINE == bdType || FUNC == bdType)
				addlTipTxt = ::CleanDefForDisplay(baseData->Def(), gTypingDevLang);
		}
	}

	return addlTipTxt;
}

bool EdCnt::DisplayTypeInfo(LPPOINT pPoint /* = NULL */)
{
	LogElapsedTime let("Ed:DTI", 50);

	DISABLECHECKCMD(false);
	CAN_USE_NEW_SCOPE_Decoy_DontReallyUseMe(); // decoy call - prevents linker from removing decoy body
	if (GetParseDb()->GetFilename().IsEmpty() || GlobalProject->IsBusy() ||
	    (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()) || IsBufEmpty())
		return false; // Don't run until the all files are parsed.
	CPoint dispPt;
	WTString tipText;
	if (GetTypeInfo(pPoint, tipText, &dispPt))
	{
		if (m_ttTypeInfo)
		{
			ArgToolTip* tmp = m_ttTypeInfo;
			m_ttTypeInfo = NULL;
			delete tmp;
		}
		m_ttTypeInfo = new ArgToolTip(this);
		m_ttTypeInfo->Display(&dispPt, tipText.c_str(), NULLSTR.c_str(), NULLSTR.c_str(), 1, 1, false);
		return true;
	}
	return false;
}

void EdCnt::DisplayClassInfo(LPPOINT pPoint /* = NULL */)
{
	MultiParsePtr mp(GetParseDb());
	if (mp->GetFilename().IsEmpty() || GlobalProject->IsBusy() ||
	    (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()) || IsBufEmpty())
		return; // Don't run until the all files are parsed.

	LogElapsedTime let("Ed:DCI", 50);

	// display tooltip of symbol under pointer or symbol at caret
#pragma warning(push)
#pragma warning(disable : 4127)
	if (TRUE || pPoint || !HasSelection() && !m_ReparseScreen)
	{
		// save cur pos
		long p1, p2;
		GetSel(p1, p2);
		WTString lscope = m_lastScope;
		// move cursor under pointer and get def
		CPoint pt, ptchar;
		if (!pPoint)
		{
			GetCursorPos(&pt);
			vScreenToClient(&pt);
			if (pt.x <= (int)Psettings->m_borderWidth)
				return;
		}
		else
		{
			pt = *pPoint;
		}

		ptchar = pt;
		long p3 = CharFromPos(&ptchar); // sets ptchar to actual char pos
		CPoint ptx = pt;
		p3 = CharFromPos(&ptx); // sets ptchar to actual char pos
		static WTString sMouseBuf;
		sMouseBuf = GetBufConst();
		LPCSTR buf = sMouseBuf.c_str();
		g_mouseBufPtr = &buf[GetBufIndex(sMouseBuf, p3)];
		if (!g_mouseBufPtr)
		{
			// Word Wrapping may be on, lets go back to the start
			g_mouseBufPtr = buf;
		}
		if (g_mouseBufPtr)
		{
			p3 = ptr_sub__int(g_mouseBufPtr, buf) + 1;
			g_mouseBufPtr = nullptr;
		}

		if (Psettings->mUpdateHcbOnHover && CAN_USE_NEW_SCOPE())
		{
			const WTString curBuf(GetBuf());
			WTString scope;
			DTypePtr data = ::SymFromPos(curBuf, mp, GetBufIndex(curBuf, p3), scope, false);
			if (data && data->MaskedType() != RESWORD)
			{
				ClassViewSym = data->SymScope();
				QueueHcbRefresh();
			}
		}
	}
#pragma warning(pop)
}

#include "VARefactor.h"

extern ArgToolTip* g_pLastToolTip;
void EdCnt::ClearAllPopups(bool noMouse /*= true*/)
{
	BOOL doUpdate = FALSE;
	if (noMouse)
	{
		//		g_ScreenAttrs.m_ticPoint.flag = 0;
		if (g_ScreenAttrs.m_VATomatoTip)
			g_ScreenAttrs.m_VATomatoTip->Dismiss();
		// don't do these if called in response to mouseMove
		g_CompletionSet->Dismiss();
		if (g_pLastToolTip) // catch stray tooltip
			g_pLastToolTip->ShowWindow(SW_HIDE);
		if (m_ttParamInfo->GetSafeHwnd())
			m_ttParamInfo->ShowWindow(SW_HIDE);
	}

	if (m_ttTypeInfo)
	{
		ArgToolTip* tmp = m_ttTypeInfo;
		m_ttTypeInfo = NULL;
		delete tmp;
		doUpdate = TRUE;
	}
	if (doUpdate)
		UpdateWindow(); // so that scrolling doesn't scroll junk
}

LRESULT EdCnt::FlashMargin(WPARAM color, LPARAM duration)
{
	if (m_lborder)
		m_lborder->Flash((COLORREF)color, (DWORD)duration);
	return TRUE;
}

void EdCnt::Invalidate(BOOL bErase /* = TRUE */)
{
#if !defined(RAD_STUDIO)
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		return;

	// override windows invalidate to reduce flicker
	// only invalidate one pixel, so onpaint gets called.
	CRect r(5, 5, 1, 1);
	if (bErase)
	{
		// invalidate all but left border
		vGetClientRect(&r);
		r.left += Psettings->m_borderWidth;
	}
	InvalidateRect(&r, FALSE);
#endif
}

BOOL ignoreExpand = FALSE;

BOOL EdCnt::VAProcessKey(uint key, uint repeatCount, uint flags)
{
	if (g_inMacro || !Psettings->m_enableVA)
		return FALSE;
	DEFTIMERNOTE(OnKeyDownTimer, itos(key));
	m_ignoreScroll = false; // repaint onscroll
	if (key != VK_TAB && key != VK_RETURN)
		m_typing = false;
	if (g_CompletionSet->ProcessEvent(mThis, (int)key))
		return TRUE;
	switch (key)
	{
	case DSM_TYPEINFO:
		// VS.NET quick info via shortcut <ctrl>+K,<ctrl>+I
		if (IsFeatureSupported(Feature_HoveringTips, m_ScopeLangType))
		{
			if (gShellAttr->IsDevenv10OrHigher())
				return FALSE; // [case=42350] don't override theirs
			Scope();          // refresh tooltip info
			CPoint pt = vGetCaretPos();
			if (gShellAttr->IsDevenv10OrHigher())
				pt.y += g_FontSettings->GetCharHeight(); // Hack for [case=42350]
			return DisplayTypeInfo(&pt);
		}
		return FALSE;
	case WM_VA_CONTEXTMENU:
		GoToDef(posAtCaret);
		return TRUE;
	case ID_EDIT_COPY:
		CmEditCopy();
		break;
	case ID_EDIT_CUT:
		CmEditCopy();
	case ID_EDIT_PASTE:
	case ID_EDIT_UNDO:
	case ID_EDIT_REDO:
		g_CompletionSet->Dismiss();
		VAProjectAddFileMRU(FileName(), mruFileEdit);
		if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
			VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
		m_doFixCase = false;
		SetBufState(BUF_STATE_WRONG);
		if (!gShellAttr->IsDevenv()) // we get notification from the shell in VS
			OnModified();
		SetTimer(ID_GETBUFFER, 100, NULL);
		if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
			SetTimer(ID_ARGTEMPLATE_TIMER, 200, NULL);
		m_autoMatchChar = '\0';
		break;
	case VK_ESCAPE:
		ignoreExpand = TRUE;
		ClearAllPopups(true);
		DisplayToolTipArgs(false);
		g_CompletionSet->Dismiss();
		SetStatus(IDS_READY);
		{
			FindReferencesPtr globalRefs(g_References);
			if (globalRefs && globalRefs->m_doHighlight)
				globalRefs->StopReferenceHighlights();
		}
		::ClearAutoHighlights();
		break;
	case DSM_DELWORDLEFT:
	case DSM_DELWORDRIGHT:
		GetBuf(TRUE);
		g_CompletionSet->Dismiss();
		break;
	case VK_UP:
	case VK_DOWN:
		g_CompletionSet->Dismiss();
		m_autoMatchChar = '\0';
		break;
	case VK_RIGHT:
	case VK_LEFT:
		g_CompletionSet->Dismiss();
		if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
			SetTimer(ID_ARGTEMPLATE_TIMER, 50, NULL);
		m_autoMatchChar = '\0';
		break;
	case DSM_LISTMEMBERS:
		if (gShellAttr->IsDevenv() && (Is_VB_VBS_File(m_ftype) || m_ftype == CS))
			return FALSE;
		CmEditExpand(ET_EXPAND_MEMBERS); // expand no tab insert -Jer
		return TRUE;
	case DSM_AUTOCOMPLETE:
		if (gShellAttr->IsDevenv() && (Is_VB_VBS_File(m_ftype) || m_ftype == CS || m_ftype == JS))
		{
			if (m_lastScope.length() && m_lastScope[0] != DB_SEP_CHR)
			{
				// expanding text w/in comment or string
				CmEditExpand(ET_EXPAND_COMLETE_WORD); // don't insert tab
				return TRUE;
			}
			return FALSE;
		}
		CmEditExpand(ET_EXPAND_COMLETE_WORD); // expand no tab insert -Jer
		return TRUE;
	case DSM_PARAMINFO:
		if (!IsFeatureSupported(Feature_ParamTips, m_ftype))
			return FALSE;

		if (Psettings->m_bUseDefaultIntellisense)
		{
			if (Psettings->m_ParamInfo || (m_ttParamInfo->GetSafeHwnd() && m_ttParamInfo->IsWindowVisible()))
				SetTimer(ID_ARGTEMPLATE_DISPLAY_TIMER, 500, NULL); // in case theirs doesn't show up...
			return FALSE;
		}
		Scope(true);
		DisplayToolTipArgs(true);
		return TRUE;
	case VK_TAB:
		VAProjectAddFileMRU(FileName(), mruFileEdit);
		if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
			VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
		if (!Psettings->m_incrementalSearch)
			modified = TRUE;
		if (!Psettings->m_bCompleteWithTab && g_CompletionSet->IsExpUp(NULL))
			return FALSE;
		if (HasSelection())
			return FALSE;
		if (gShellAttr->IsDevenv())
		{
			if (m_ftype != Src && m_ftype != Header)
				return FALSE;
		}

		if (HasVsNetPopup(TRUE))
			return FALSE; // let them have
		if (!Psettings->m_bCompleteWithTab && g_CompletionSet->IsExpUp(NULL))
		{
			g_CompletionSet->Dismiss();
			return FALSE;
		}
		if (!IsCFile(m_ftype))
			return FALSE; // let them have it
		if (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes ||
		    (Psettings->mRestrictVaListboxesToC && !(IsCFile(gTypingDevLang))))
		{
			return FALSE; // let them have it
		}
		else
		{
			if (g_CompletionSet->ProcessEvent(mThis, VK_TAB))
				return TRUE;

			SetTimer(ID_GETBUFFER, 20, NULL);
		}
		return FALSE;
	case VAK_INVALIDATE_BUF:
		OnModified();
	case VK_DELETE:
		VAProjectAddFileMRU(FileName(), mruFileEdit);
		if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
			VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
	case VK_BACK:
		SetBufState(BUF_STATE_WRONG);
		UpdateWindow();
		if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 0)
			SetTimer(ID_ARGTEMPLATE_TIMER, 100, NULL);
		SetTimer(ID_GETBUFFER, 200, NULL);
		if (m_undoOnBkSpacePos != CurPos())
		{
			m_typing = true;
			return FALSE;
		}
		break;
	case VK_SPACE:
		if ((GetKeyState(VK_CONTROL) & 0x1000) && (GetKeyState(VK_SHIFT) & 0x1000))
		{
			DisplayToolTipArgs(true);
			return TRUE;
		}
		else if ((GetKeyState(VK_CONTROL) & 0x1000) && !(GetKeyState(VK_SHIFT) & 0x1000))
		{
			CmEditExpand(ET_EXPAND_COMLETE_WORD); // Ctrl+space
			return TRUE;
		}
		break;
	case ID_FILE_SAVE:
		// file is about to save, issue timer so we make sure file is saved;
		SetTimer(ID_FILE_SAVE, 500, NULL);
		return FALSE;
	case VK_RETURN:
		VAProjectAddFileMRU(FileName(), mruFileEdit);
		if (m_lastScope.GetLength() && m_lastScope[0] == DB_SEP_CHR)
			VAProjectAddScopeMRU(m_lastScope, mruMethodEdit);
		if (!Psettings->m_incrementalSearch)
			modified = TRUE;
		OnModified();
		extern BOOL HasVsNetPopup(BOOL);
		if (HasVsNetPopup(TRUE))
		{
			return FALSE;
		}

		if (m_ttTypeInfo)
		{
			ArgToolTip* tmp = m_ttTypeInfo;
			m_ttTypeInfo = NULL;
			delete tmp;
		}
		SetTimer(ID_GETBUFFER, 500, NULL);
		WTString cwd = CurWord();

		if (ProcessReturn())
			return TRUE;
		long offset = GetBufIndex((long)CurPos());
		if (offset)
			BufInsert(offset, GetLineBreakString() +
			                      "                                                                             ");

		SetTimer(ID_TIMER_CHECKFORSCROLL, 10, NULL);
		return FALSE;
	}

	LOG("OnKeyDown");
	int line;
	ulong p1, p2;

	//	int shift = GetKeyState(VK_SHIFT) & 0x1000;
	// insert "_" on VK_SHIFT for m_
	if (key == VK_SHIFT && Psettings->m_auto_m_ && CurWord() == "m" && !(GetKeyState(VK_MENU) & 0x1000) &&
	    !(GetKeyState(VK_CONTROL) & 0x1000) && !gShellSvc->HasBlockModeSelection(this))
	{
		Insert("_");
		ulong cp = (ulong)CurPos();
		SetSel(cp - 1, cp);
	}
	if (m_ttTypeInfo)
	{
		ArgToolTip* tmp = m_ttTypeInfo;
		m_ttTypeInfo = NULL;
		delete tmp;
	}
	if (m_tootipDef)
	{
		switch (key)
		{
		case VK_TAB:
		case VK_ESCAPE:
			DisplayToolTipArgs(false);
			break;
		case VK_NEXT:
			if (!(GetKeyState(VK_CONTROL) & 0x1000))
			{
				DisplayToolTipArgs(false);
				break;
			}
			if (m_ttParamInfo)
				m_ttParamInfo->OnMouseActivate(NULL, 0, WM_LBUTTONDOWN);
			return TRUE;
		case VK_DOWN:
			if (m_ttParamInfo && m_ttParamInfo->m_totalDefs > 1 &&
			    m_ttParamInfo->m_totalDefs != m_ttParamInfo->m_currentDef)
			{
				m_tootipDef++;
				DisplayToolTipArgs(true);
				return TRUE;
			}
			DisplayToolTipArgs(false);
			break;
		case VK_PRIOR:
			if (!(GetKeyState(VK_CONTROL) & 0x1000))
			{
				DisplayToolTipArgs(false);
				break;
			}
			if (m_ttParamInfo)
				m_ttParamInfo->OnMouseActivate(NULL, 0, WM_RBUTTONDOWN);
			return TRUE;
		case VK_UP:
			if (m_ttParamInfo && m_ttParamInfo->m_currentDef > 1)
			{
				if (m_tootipDef > 1)
					m_tootipDef--;
				DisplayToolTipArgs(true);
				return TRUE;
			}
			DisplayToolTipArgs(false);
			break;
		case VK_RETURN:
			m_typing = true;
			DisplayToolTipArgs(false);
		default:
			if (strchr("()<,", (char)key)) // only display show tt-args on these keys
				SetTimer(ID_ARGTEMPLATE_TIMER, 100, NULL);
			break;
		}
	}

	if (Psettings->m_incrementalSearch && gShellAttr->IsDevenv())
		Psettings->m_incrementalSearch = FALSE;
	if (Psettings->m_incrementalSearch && !gShellAttr->IsDevenv())
	{
		if (key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT || key == VK_NEXT || key == VK_PRIOR)
		{
			// stop IncSearch
			::SetForegroundWindow(::GetDesktopWindow());
			Sleep(25);
			SetForegroundWindow();
			Psettings->m_incrementalSearch = false;
		}
		else
			return TRUE;
	}
	if (key == VK_RETURN)
	{
		// see note in OnKeyUp
		if (gShellAttr->IsDevenv())
			KillTimer(ID_GETBUFFER); // or it will eat smart indent
	}

	if ((key == VK_UP) || (key == VK_DOWN))
	{ // arrow
		// catch text that needs parsing
		if (m_ReparseScreen)
			Reparse();
		if (g_lastChar == VK_RETURN)
		{
			// last key was enter, remove auto-indent
			// select all white space left of caret
			uint aip1 = LinePos();
			uint aip2 = aip1;
			if (aip1 != aip2)
			{
				token t = GetSubString(aip1, aip2);
				if (!t.read(" \t").GetLength())
				{ // only if blank line
					SetSelection((long)aip1, (long)aip2);
					Insert("");
				}
			}
			g_lastChar = 0;
		}
		if (!(gShellAttr->IsDevenv() && key == VK_UP && CurLine() == 1))
		{
			return FALSE;
		}
		if (m_lborder)
			m_lborder->DrawBkGnd(); // update border while scrolling up/down
		return FALSE;
	}
	else if ((key == VK_PRIOR) || (key == VK_NEXT) || (key == VK_LEFT) || (key == VK_RIGHT))
	{
		// cancel AutoMatch typeover
		m_autoMatchChar = '\0';
		// Prevent beep at beg or end of file or line
		line = CurLine();
		GetSel(p1, p2);
		switch (key)
		{
		case VK_PRIOR:
			if (line == 0)
				return TRUE;
			break;
		case VK_NEXT:
			break;
		case VK_LEFT:
		case VK_RIGHT:
			if (VK_LEFT == key && p1 == 0 && p2 == 0)
				return TRUE;
			return FALSE;
		}
	}
	if ((key == VK_DELETE) || (key == VK_BACK))
	{
		// not sure why this is here,  reassigning backspace to cnt-h returns here
		//		if (GetKeyState(VK_CONTROL)& 0x1000) {
		//			// TODO: should handle in the accelerator spy because
		//			//  shift+Back can be remapped (by default it's the
		//			//  same as Back) and Del can be remapped to something else.
		//			// If we get into here with VK_CONTROL it's because
		//			//  the keycombo is undefined - just return
		//			return;
		//		}
		GetSel(p1, p2);
		if (key == VK_BACK && Psettings->NoBackspaceAtBOL)
		{
			line = LineIndex();
			if (p1 == p2 && (long)p2 == line)
				return TRUE;
		}
		if (m_undoOnBkSpacePos == CurPos())
		{
			m_undoOnBkSpacePos = NPOS;

			if (gShellAttr->IsDevenv())
			{
				const WTString bb(GetBuf(TRUE)); // ensure we are in sink with what they have
				long cp = (long)CurPos();
				long i = GetBufIndex(bb, cp);
				if (!m_autoMatchChar && CurWord() == "->")
				{
					SetSel(cp - 2, cp);
					Insert("."); // some emulations don't do destructive inserts
					g_CompletionSet->Dismiss();
					if (Psettings->m_bUseDefaultIntellisense || Psettings->mSuppressAllListboxes ||
					    (Psettings->mRestrictVaListboxesToC && !(IsCFile(gTypingDevLang))))
						SetTimer(DSM_LISTMEMBERS, 10, NULL);
					else
						SetTimer(DSM_VA_LISTMEMBERS, 10, NULL);
				}
				else if (m_autoMatchChar == '\n')
				{
					LPCSTR p = bb.c_str();
					long p1_2, p2_2;
					for (p1_2 = i; p1_2 && wt_isspace(p[p1_2 - 1]); p1_2--)
						;
					for (p2_2 = i; p[p2_2] && wt_isspace(p[p2_2]); p2_2++)
						;
					SetSel(p1_2, p2_2);
					Insert(""); // some emulations don't do destructive inserts
				}
				else
				{
					LPCSTR p;
					for (p = bb.c_str(); p[i] && wt_isspace(p[i]); i++)
						;
					if (p[i] == m_autoMatchChar)
					{
						SetSel(cp, i + 1);
						Insert(""); // some emulations don't do destructive inserts
					}
				}
			}
			if (HasSelection())
				SetPos(CurPos(true));
			m_autoMatchChar = '\0';
			return TRUE;
		}
		// catch delete for undo and redo
		WTString cw = CurWord(); // used to smooth out uncommenting

		//		if(p1 == p2){ // select prev char and mark for delete
		//			if(key == VK_DELETE) { // added reverse selection for scrolling
		//				if(CurLine() == (m_nlines-1)){ // update nchars if last line so we can tell if we are deleteing
		// at eof 					nchars =  m_nlines?LineIndex(m_nlines-1):0;
		//				}
		//				if(p1>= nchars)
		//					return;	// delete at eof
		//				SetSelection(p1 + 1, p1, true);
		//				if (CharAt(p1) == '\r')	// need to select \r\n as a pair
		//					SetSelection(p1 + 2, p1, true);
		//			} else if(p1 && CharAt(p1-1) == '\n')
		//				SetSelection(p1 - 2, p1, true);
		//			else if (p1) // VK_BACK
		//				SetSelection(p1, p1 -1, true);
		//			else	// VK_BACK hit at beginning of file
		//				return TRUE;
		//		}
		int blockMode = 0; // TerDropInfo(m_hWnd, -1, -1);
		if (p1 == p2)
			SetSelection((long)p1, (long)p1); // linebreak selection whacks line joins (Del at EOL or BACK at BOL)
		if (p1 != p2)
		{
		} //			TerSpecialInsert(m_hWnd, (unsigned char*)"", FALSE);
		else
		{
			if (flags != 0xdead)
				CTer::OnKeyDown(key, repeatCount, flags);
		}
		if (p1 != p2)
		{
			if (blockMode)
				SetSel((int)p1, (int)CurPos(TRUE)); // set selection for saveundo
			else
				SetSel((int)p1, (int)p1);
		}
		// delete and backspace don't call OnChar
		if (blockMode)
			SetPos(CurPos(TRUE)); // reset saveundo selection
			                      // scroll cursor into view case of the hide on setsel above
	}
	else if (key == VK_RETURN)
	{
		// set mod mark for current line if hit return
		//  in the middle of the line
#ifndef TI_BUILD
		uint cp = CurPos();
		long cl = LineFromChar(-1);
		if (cp != LinePos(cl))
		{
			int modLnStartPos = LineIndex();
			if (cp < (uint)modLnStartPos)
				m_LnAttr->Line(cl)->SetModify();
		}
#endif                // TI_BUILD
		return FALSE; // let OnChar take it
	}
	else if (key == VK_TAB)
	{
		GetSel(p1, p2);
		if (p1 == p2)
		{
			if (LineIndex() == (int)p1) // if tab is hit in column 0, just pass on
				InsertTabstr();
			else if (Psettings->m_tabInvokesIntellisense)
				CmEditExpand(ET_EXPAND_TAB);
			else
				InsertTabstr();
		}
		return TRUE;
	}
	else if (VAK_INVALIDATE_BUF == key)
	{
		if (m_undoOnBkSpacePos == CurPos())
		{
			m_undoOnBkSpacePos = NPOS;
			m_autoMatchChar = '\0';
		}
		return FALSE;
	}
	else
	{
		return FALSE;
	}

	// SendMessage(EM_EMPTYUNDOBUFFER); // An undo is slipping threw to RichEdit
	return TRUE;
}

long EdCnt::BufInsert(long pos, LPCSTR str)
{
	// Update closing bold brace position
	g_ScreenAttrs.InsertText((ULONG)CurLine(), (ULONG)pos, str);
	OnModified();
	return CTer::BufInsert(pos, str);
}

long EdCnt::BufInsertW(long pos, const CStringW& str)
{
	const WTString ascStr(str);
	// OnModified() should be called if this (BufInsertW) is
	// reimplemented without calling BufInsert
	return BufInsert(pos, ascStr.c_str());
}

WTString EdCnt::GetLine(int line)
{
	// don't call ::GetLine since it does not return the entire
	// line (including leading whitespace)
	const WTString bb(GetBufConst());
	int i = GetBufIndex(bb, (long)LinePos(line));
	LPCTSTR p = bb.c_str();
	p = &p[i];
	for (i = 0; p[i] && !strchr("\r\n", p[i]); i++)
		;
	return WTString(p, i);
}

bool EdCnt::SpellCheckWord(CPoint point, bool interactive)
{
	const WTString scope = Scope();
	// [case: 53080] can't tell when the parser might have underlined something.
	// use screenAttrs to check for underline regardless of scope
	const bool kHackFor53080 = !(Is_C_CS_VB_File(m_ftype)) && !gShellAttr->IsDevenv10OrHigher();
	// Only offer spelling suggestions in comments or strings
	if (scope.c_str()[0] != DB_SEP_CHR || kHackFor53080)
	{
		AttrClassPtr attr = g_ScreenAttrs.AttrFromPoint(mThis, g_CursorPos.x, g_CursorPos.y, NULL);
		g_StrFromCursorPosUnderlined = (attr && attr->mFlag == SA_UNDERLINE_SPELLING);
		WTString cwd =
		    interactive && !kHackFor53080 ? CurWord() : ((g_StrFromCursorPosUnderlined && attr) ? attr->mSym : NULLSTR);
		if (g_StrFromCursorPosUnderlined || (cwd.GetLength() && cwd[0] != '#')) // [case: 91351] skip hashtag
		{
			if (interactive)
				SetStatus(IDS_READY);
			if (!g_StrFromCursorPos.IsEmpty())
				cwd = g_StrFromCursorPos;
			DisplaySpellSuggestions(point, cwd);
			return true;
		}

		if (interactive)
			SetStatus("No spelling suggestions available.");
	}
	else if (interactive)
		SetStatus("Spell check is only available in comments and strings.");

	return false;
}

void EdCnt::DisplaySpellSuggestions(CPoint point, const WTString& wordToReplace)
{
	if (wordToReplace.IsEmpty())
		return;
	if (wordToReplace[0] == '#')
		return; // [case: 91351] skip hashtag

	CStringList lst;
	FPSSpell(wordToReplace.c_str(), &lst);
	PopupMenuXP xpmenu;

	const uint maxItems = 10;
	uint itemPos = 0;
	while (itemPos < (uint)lst.GetCount() && itemPos < maxItems)
	{
		CString wd = lst.GetAt(lst.FindIndex((int)itemPos));
		WTString wd2 = ::BuildMenuTextHexAccelerator(++itemPos, wd);
		xpmenu.AddMenuItem(itemPos, MF_BYPOSITION, wd2);
	}

	xpmenu.AddSeparator(++itemPos);
	xpmenu.AddMenuItem(++itemPos, MF_BYPOSITION, "&Add word");
	xpmenu.AddMenuItem(++itemPos, MF_BYPOSITION, "&Ignore all");

	vClientToScreen(&point);
	// select first item by default
	PostMessage(WM_KEYDOWN, VK_DOWN, 1);
	int result;
	{
		TempTrue tt(m_contextMenuShowing);
		result = xpmenu.TrackPopupMenuXP(this, point.x, point.y);
		if (result < 1)
			return;
	}

	if (result < int(itemPos - 2))
	{
		token t = wordToReplace;
		WTString wd = (const char*)lst.GetAt(lst.FindIndex(result - 1));
		uint pos = CurPos();

		const WTString curBuf(GetBuf());
		WTString left_word = WordLeftOfCursor();
		uint left;
		if (!left_word.length() || !isspace(left_word[left_word.length() - 1]))
			left = WordPos(curBuf, BEGWORD, pos);
		else
			left = pos; // don't go to the previous word if space detected
		WTString right_word = WordRightOfCursor();
		uint right;
		if (!right_word.length() || !isspace(right_word[0]))
			right = WordPos(curBuf, ENDWORD, pos);
		else
			right = pos; // don't go to the next word if space detected

		SetSelection((long)left, (long)right);
		InsertW(wd.Wide());
	}
	else if (itemPos == (uint)result) // watch out for pos of separator
	{
		// ignore word
		FPSAddWord(wordToReplace.c_str(), TRUE);
		// [case: 15004] clear screen attributes, invalidate and reparse
		OnModified(TRUE);
	}
	else if ((itemPos - 1) == (uint)result) // watch out for pos of separator
	{
		// add word
		FPSAddWord(wordToReplace.c_str(), FALSE);
		// [case: 15004] clear screen attributes, invalidate and reparse
		OnModified(TRUE);
	}
}

void EdCnt::SetFocusParentFrame()
{
	if (gShellAttr->IsDevenv10OrHigher())
	{
		vSetFocus();
		return;
	}

	// sometimes setFocus doesn't work in vc6 - need to set focus to the MDI window frame
	HWND parent = ::GetParent(m_hWnd);
	if (parent)
	{
		HWND oneMoreParent = ::GetParent(parent);
		if (oneMoreParent)
			::SetFocus(oneMoreParent);
		else
			::SetFocus(parent);
	}
	else
		vSetFocus();
}

WTString EdCnt::GetDefsFromMacro(const WTString& sScope, const WTString& sDef)
{
	MultiParsePtr mp(GetParseDb());
	return ::GetDefsFromMacro(sScope, sDef, m_ScopeLangType, mp.get());
}

void EdCnt::PositionDialogAtCaretWord(VADialog* dlg)
{
	// [case: 29904]
	CPoint pt = vGetCaretPos();
	// [case: 68570] text selection horribly screws up the x position
	if (gShellAttr->IsDevenv() && !HasSelection())
	{
		// this bit of logic was to place the dialog at the start of
		// the word rather than right at the caret -
		// doesn't work in vc6 though (case=30939)
		const uint pos = WordPos(BEGWORD, CurPos());
		pt.x = GetCharPos((long)pos).x;
	}
	pt.y += 20;
	vClientToScreen(&pt);
	dlg->SetWindowPos(NULL, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOZORDER);

	CRect rc;
	dlg->GetWindowRect(&rc);
	if (g_FontSettings->RestrictRectToMonitor(rc, this))
	{
		// there was a problem.  before accepting what it did, let's try above the caret.
		CRect rectForAboveCaret;
		dlg->GetWindowRect(&rectForAboveCaret);
		rectForAboveCaret.MoveToY(pt.y - 25 - rectForAboveCaret.Height());
		if (g_FontSettings->RestrictRectToMonitor(rectForAboveCaret, this))
			dlg->MoveWindow(&rc); // no go above the caret - just use the rc it first returned
		else
			dlg->MoveWindow(&rectForAboveCaret);
	}
}

CPoint EdCnt::GetCharPosThreadSafe(uint pos)
{
	CPoint pt;
	SendMessage(WM_VA_THREAD_GETCHARPOS, (WPARAM)pos, (LPARAM)&pt);
	return pt;
}

void EdCnt::SaveBackup()
{
	if (!Psettings->m_autoBackup)
		return;

	const CStringW fname(FileName());
	// save unsaved file
	// get real text from editor w/o padding
	WTString buf;
	if (gVaShellService || (gShellAttr && gShellAttr->IsCppBuilder()))
	{
		_ASSERTE(g_mainThread == GetCurrentThreadId());

		// [case: 58071] unicode backup
		const CStringW wideBuf(::PkgGetFileTextW(fname));
		buf = wideBuf;
		const CStringW bakName(::GetTempFile(fname, TRUE));
		CFileW bak;
		if (bak.Open(bakName, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
		{
			bak.Write(wideBuf, wideBuf.GetLength() * sizeof(WCHAR));
			bak.Close();
		}
	}
	else
	{
		// gShellSvc->GetText Gets the text of the active document, not the passed hwnd's text. case=49620
		DWORD bufLen = 0;
		LPCTSTR tbuf = gShellSvc->GetText(m_hWnd, &bufLen);
		buf.AssignCopy((int)bufLen, tbuf);
	}

	// wt mbcs backup
	WTofstream ofs(::GetTempFile(fname), ios::out | ios::binary);
	ofs << buf.c_str();
}

bool EdCnt::CanSuperGoto()
{
	return !!QueryStatus(icmdVaCmd_SuperGoto) && !!SuperGoToDef(posAtCursor, false);
}

DWORD
EdCnt::QueryStatus(DWORD cmdId)
{
	// -1 = supported, disabled, invisible
	// 0 = supported but disabled
	// 1 = supported and enabled
	// high-bit set for latch
	DWORD enableCmd = 0;

	switch (cmdId)
	{
	case icmdVaCmd_ListMethods:
		enableCmd = !Psettings->m_noMiniHelp && IsFeatureSupported(Feature_MiniHelp, m_ftype);
		break;
	case icmdVaCmd_OpenCorrespondingFile:
		// [case: 101997]
		enableCmd = 1;
		break;
	case icmdVaCmd_SpellCheckWord:
		enableCmd = ::GetFocus() == m_hWnd;
		if (!enableCmd && gShellAttr->IsDevenv10OrHigher() && g_currentEdCnt)
			enableCmd = 1;
		break;
	case icmdVaCmd_ToggleLineComment:
		enableCmd = 1;
		break;
	case icmdVaCmd_ToggleBlockComment:
	case icmdVaCmd_LineComment:
	case icmdVaCmd_BlockComment:
	case icmdVaCmd_LineUncomment:
	case icmdVaCmd_BlockUncomment:
		enableCmd = !GetSelString().IsEmpty();
		break;
	case icmdVaCmd_SnippetCreateFromSel:
		enableCmd = !GetSelString().IsEmpty() && gAutotextMgr != NULL;
		break;
	case icmdVaCmd_SpellCheck:
		if (m_ftype != Binary && m_ftype != Image && m_ftype != Other)
			enableCmd = 1;
		break;
	case icmdVaCmd_ShareWith:
	case icmdVaCmd_ShareWith2:
		if (!gShellAttr->IsDevenv14OrHigher())
			break;
		[[fallthrough]];
	case icmdVaCmd_InsertCodeTemplate:
	case icmdVaCmd_ContextMenuOld:
	case icmdVaCmd_SurroundWithBraces:
	case icmdVaCmd_SurroundWithParens:
	case icmdVaCmd_SurroundWithPreprocDirective:
	case icmdVaCmd_SortLines:
	case icmdVaCmd_Paste:
	case icmdVaCmd_CommentOrUncomment:
	case icmdVaCmd_ResetEditorZoom:
	case icmdVaCmd_OpenContextMenu:
		if (m_ftype != Binary && m_ftype != Image /*&& m_ftype != Other*/)
			enableCmd = 1;
		break;
	// commands that have been enabled in the past by
	// default without regard to context
	case icmdVaCmd_FindPreviousByContext:
	case icmdVaCmd_FindNextByContext:
	case icmdVaCmd_GotoImplementation:
	case icmdVaCmd_SuperGoto:
	case icmdVaCmd_GotoMember:
	case icmdVaCmd_Reparse:
	case icmdVaCmd_ScopePrevious:
	case icmdVaCmd_ScopeNext:
#ifdef RAD_STUDIO
	case icmdVaCmd_GotoDeclOrImpl:
#endif
		if (m_ftype != Binary && m_ftype != Image && m_ftype != Other)
			enableCmd = 1;
		break;
	case icmdVaCmd_SmartSelectExtend:
	case icmdVaCmd_SmartSelectShrink:
	case icmdVaCmd_SmartSelectExtendBlock:
	case icmdVaCmd_SmartSelectShrinkBlock:
		if (CanSmartSelect())
			enableCmd = 1;
		break;
	case icmdVaCmd_DisplayIncludes:

		if (CVAClassView::IsFileWithIncludes(m_ftype))
		{
			if (g_ParserThread && GlobalProject && !GlobalProject->IsBusy())
				enableCmd = 1;
		}
		break;
	case icmdVaCmd_InsertPath: {
#if !defined(RAD_STUDIO)
		if (gShellAttr && !gShellAttr->IsMsdev())
		{
			auto ed = g_currentEdCnt;
			if (ed && ed.get() == this && HasFocus())
				enableCmd = 1;
		}
#endif
		break;
	}
	default:
#ifdef _DEBUG
		CString msg;
		CString__FormatA(msg, "EdCnt::QueryStatus unhandled va cmdid: 0x%lx\n", cmdId);
		::OutputDebugString(msg);
#endif // _DEBUG
		_ASSERTE(!"EdCnt::QueryStatus unhandled va cmdid");
	}

	return enableCmd;
}

HRESULT
EdCnt::Exec(DWORD cmdId)
{
	vLog("Ed::Exec %lx", cmdId);
	UINT dsmMsg = 0;
	UINT vaSendMsg = 0;
	WPARAM vaCmdWparam = 0;
	ScopedIncrement si(&gExecActive);
	ScopedValue sv(gCurrExecCmd, cmdId, (DWORD)0);

	switch (cmdId)
	{
	case icmdVaCmd_SnippetCreateFromSel: {
		const WTString sel = GetSelString();
		gAutotextMgr->Edit(m_ScopeLangType, NULL, sel);
		return S_OK;
	}
	break;
	case icmdVaCmd_SpellCheckWord:
		if (g_currentEdCnt && this == g_currentEdCnt.get() && GetSafeHwnd() && IsWindowVisible())
		{
			CPoint pos(vGetCaretPos());
			pos.y += 15;
			SpellCheckWord(pos, true);
		}
		return S_OK;
		break;
	case icmdVaCmd_OpenCorrespondingFile:
		vaCmdWparam = WM_VA_OPENOPPOSITEFILE;
		break;
	case icmdVaCmd_SpellCheck:
		vaSendMsg = WM_VA_SPELLDOC;
		break;
	case icmdVaCmd_InsertCodeTemplate:
		vaCmdWparam = WM_VA_CODETEMPLATEMENU;
		break;
	case icmdVaCmd_ContextMenuOld:
		vaCmdWparam = WM_VA_CONTEXTMENU;
		break;
	case icmdVaCmd_SurroundWithBraces:
		vaCmdWparam = VAM_ADDBRACE;
		break;
	case icmdVaCmd_SurroundWithParens:
		vaCmdWparam = VAM_PARENBLOCK;
		break;
	case icmdVaCmd_SurroundWithPreprocDirective:
		vaCmdWparam = VAM_IFDEFBLOCK;
		break;
	case icmdVaCmd_SortLines:
		vaCmdWparam = VAM_SORTSELECTION;
		break;
	case icmdVaCmd_Paste:
		vaCmdWparam = WM_VA_PASTE;
		break;
	case icmdVaCmd_ShareWith:
	case icmdVaCmd_ShareWith2:
		if (gShellAttr->IsDevenv14OrHigher())
		{
			bool ShareWith();
			ShareWith();
		}
		break;
	case icmdVaCmd_CommentOrUncomment:
		vaSendMsg = WM_VA_SMARTCOMMENT;
		break;
	case icmdVaCmd_FindPreviousByContext:
		vaSendMsg = DSM_FINDCURRENTWORDBWD;
		break;
	case icmdVaCmd_FindNextByContext:
		vaSendMsg = DSM_FINDCURRENTWORDFWD;
		break;
	case icmdVaCmd_GotoImplementation:
#ifdef RAD_STUDIO
	case icmdVaCmd_GotoDeclOrImpl:
#endif
		NavAdd(FileName(), CurPos());
		if (!GoToDef(posBestGuess))
		{
			// workaround for bug that I couldn't track down.
			// if alt+g doesn't do anything, focus gets placed into
			// the first button (New Project) in the Standard toolbar
			// if alt is released before g.
			// can't check for aggregate focus since it changes after this point.
			if (g_currentEdCnt.get() == this)
				DelayFileOpen(FileName());
		}
		return S_OK;
	case icmdVaCmd_SuperGoto: {
		NavAdd(FileName(), CurPos());
		SuperGoToDef(posBestGuess);
		return S_OK;
	}
	case icmdVaCmd_GotoMember: {
		DType dtype;
		bool isCursorInWs;
		if (this->GetSuperGotoSym(dtype, isCursorInWs))
		{
			std::vector<DType> dtypes; // unused
			PopupMenuXP xpmenu;        // unused
			DType resolvedType;        // resolve typedefs and vars to real type
			BuildTypeOfSymMenuItems(&dtype, dtypes, xpmenu, &resolvedType);
			if (resolvedType.IsType())
			{
				VABrowseMembersDlg(&resolvedType);
			}
			else
			{
				MultiParsePtr mp(GetParseDb());
				DType* scopePtr = nullptr;
				switch (dtype.MaskedType())
				{
				case C_ENUMITEM:
					scopePtr = GetEnumItemParent(&dtype, mp.get());
					break;

				case TYPE:
				case VAR:
					// couldn't resolve type above, or was resolved to built-in type
					break;

				case FUNC:
				case PROPERTY:
				default:
					scopePtr = mp->FindExact(dtype.Scope());
					break;
				}

				if (scopePtr && scopePtr->IsType() && scopePtr->MaskedType() != NAMESPACE)
					VABrowseMembersDlg(scopePtr);
			}
		}
	}
	break;
	case icmdVaCmd_Reparse:
		vaSendMsg = WM_VA_REPARSEFILE;
		break;
	case icmdVaCmd_ScopePrevious:
		vaSendMsg = WM_VA_SCOPEPREVIOUS;
		break;
	case icmdVaCmd_ScopeNext:
		vaSendMsg = WM_VA_SCOPENEXT;
		break;
	case icmdVaCmd_ListMethods:
		vaCmdWparam = WM_VA_DEFINITIONLIST;
		break;
	case icmdVaCmd_ToggleLineComment:
		vaSendMsg = WM_VA_COMMENTLINE;
		break;
	case icmdVaCmd_ToggleBlockComment:
		vaSendMsg = WM_VA_COMMENTBLOCK;
		break;
	case icmdVaCmd_LineComment:
		vaCmdWparam = VAM_COMMENTBLOCK2;
		break;
	case icmdVaCmd_LineUncomment:
		vaCmdWparam = VAM_UNCOMMENTBLOCK2;
		break;
	case icmdVaCmd_BlockComment:
		vaCmdWparam = VAM_COMMENTBLOCK;
		break;
	case icmdVaCmd_BlockUncomment:
		vaCmdWparam = VAM_UNCOMMENTBLOCK;
		break;
	case icmdVaCmd_ResetEditorZoom:
		if (gShellAttr->IsDevenv10OrHigher())
		{
			ResetZoomFactor();
			return S_OK;
		}
		_ASSERTE(!"why did icmdVaCmd_ResetEditorZoom get called in vs < 10?");
		break;
	case icmdVaCmd_DisplayIncludes: {
		const BOOL vaviewIsVisible = g_CVAClassView ? g_CVAClassView->IsWindowVisible() : false;
		if (!vaviewIsVisible)
			VAWorkspaceViews::GotoHcb();
		if (g_CVAClassView && g_CVAClassView->m_lock)
			g_CVAClassView->OnToggleSymbolLock();
		::QueueHcbIncludeRefresh(true, true);
		if (g_CVAClassView)
			g_CVAClassView->FocusHcb();
	}
		return S_OK;
	case icmdVaCmd_SmartSelectExtend:
	case icmdVaCmd_SmartSelectShrink:
	case icmdVaCmd_SmartSelectExtendBlock:
	case icmdVaCmd_SmartSelectShrinkBlock:
		if (CanSmartSelect(cmdId))
			SmartSelect(cmdId);
		return S_OK;
	case icmdVaCmd_OpenContextMenu: {
		// CMenuXPAltRepeatFilter alt_filter((GetKeyState(VK_MENU) & 0x1000) != 0, false);
		CPoint pt = vGetCaretPos();
		CRect rect;
		vGetClientRect(&rect);
		if (rect.PtInRect(pt))
		{
			vClientToScreen(&pt);
			pt.y += g_FontSettings->GetCharHeight();
			;
		}
		else
			GetCursorPos(&pt);
		ShowVAContextMenu(mThis, pt, false);
		return S_OK;
	}
	case icmdVaCmd_InsertPath: {
		auto ed = g_currentEdCnt;
		if (ed && ed.get() == this && HasFocus())
		{
			CInsertPathDialog dlg;
			if (IDOK == dlg.DoModal())
				InsertW(dlg.FixedPath);
		}
		return S_OK;
	}
	default:
#ifdef _DEBUG
		CString msg;
		CString__FormatA(msg, "EdCnt::Exec unhandled va cmdid: 0x%lx\n", cmdId);
		::OutputDebugString(msg);
#endif // _DEBUG
		_ASSERTE(!"EdCnt::Exec unhandled va cmdid");
		return E_UNEXPECTED;
	}

	if (vaCmdWparam)
		SendMessage(WM_COMMAND, vaCmdWparam, 0);
	else if (vaSendMsg)
		SendMessage(vaSendMsg, 0, 0);
	else if (dsmMsg)
		PostMessage(dsmMsg, 0, 0);

	return S_OK;
}

struct MarkerAlphaLess
{
	bool operator()(const FileLineMarker& f1, const FileLineMarker& f2) const
	{
		auto t1(f1.mText);
		auto t2(f2.mText);
		int res = t1.CompareNoCase(t2);
		if (!res)
		{
			// [case: 87549]
			// if same text, then sort by line
			return f1.mStartLine < f2.mStartLine;
		}
		return res < 0;
	}
};

struct MarkerLineLess
{
	bool operator()(const FileLineMarker& f1, const FileLineMarker& f2) const
	{
		return f1.mStartLine < f2.mStartLine;
	}
};

static bool SortByReverseLength(const CStringW& lhs, const CStringW& rhs)
{
	return lhs.GetLength() > rhs.GetLength();
}

void CleanupMarkerText(EdCntPtr ed, LineMarkers* markers)
{
	if (!ed)
		return;

	std::list<CStringW> namespaceList;
	CStringW itemTxt;
	for (size_t i = 0; i < markers->Root().GetChildCount(); ++i)
	{
		FileLineMarker& mkr = *(markers->Root().GetChild(i));
		itemTxt = mkr.mText;
		itemTxt.Replace(L"\t", L" ");
		itemTxt.Replace(L":", L".");
		const int restoreEllipsis = itemTxt.Replace(L"...", L"___;;;___");
		itemTxt.Replace(L"..", L".");
		if (restoreEllipsis)
			itemTxt.Replace(L"___;;;___", L"...");
		if (itemTxt[0] == L'.')
			itemTxt = itemTxt.Mid(1);

		int pos;
		while ((pos = itemTxt.Find(L"  ")) != -1)
			itemTxt.Replace(L"  ", L" ");
		itemTxt.TrimRight();

		if (!itemTxt.IsEmpty())
		{
			if (!Psettings->mParamsInMethodsInFileList)
			{
				// [case: 3487]
				const int parenPos = itemTxt.Find(L'(');
				if (-1 != parenPos)
				{
					itemTxt = itemTxt.Left(parenPos);
					if (JS == ed->m_ScopeLangType)
					{
						// [case: 25370]
						int pos2 = itemTxt.Find(L":function");
						if (-1 == pos2)
							pos2 = itemTxt.Find(L": function");
						if (-1 == pos2)
							pos2 = itemTxt.Find(L"=function");
						if (-1 == pos2)
							pos2 = itemTxt.Find(L"= function");
						if (-1 != pos2)
							itemTxt = itemTxt.Left(pos2);
					}
				}
			}

			itemTxt = ::DecodeScope(itemTxt);

			if (Src == gTypingDevLang)
			{
				const int restoreEllipsis2 = itemTxt.Replace(L"...", L"___;;;___");
				itemTxt.Replace(L".", L"::");
				if (restoreEllipsis2)
					itemTxt.Replace(L"___;;;___", L"...");
			}

			if (itemTxt[itemTxt.GetLength() - 1] == L';')
			{
				// [case: 64197]
				itemTxt = itemTxt.Left(itemTxt.GetLength() - 1);
			}

			if (!Psettings->mMethodInFile_ShowScope)
			{
				// [case: 70850] remove scope leaving only sym
				for (;;)
				{
					const int delimPos = itemTxt.Find(Src == gTypingDevLang ? L"::" : L".");
					if (-1 == delimPos)
						break;

					const int parenPos = itemTxt.Find(L'(');
					if (-1 != parenPos && parenPos < delimPos)
						break;

					const int templatePos = itemTxt.Find(L'<');
					if (-1 != templatePos && templatePos < delimPos)
						break;

					itemTxt = itemTxt.Mid(delimPos + Src == gTypingDevLang ? 2 : 1);
				}
			}
		}

		mkr.mText = itemTxt;
		if (mkr.mType == NAMESPACE)
		{
			if (!itemTxt.IsEmpty())
				namespaceList.push_back(itemTxt);
		}
	}

	if (Psettings->mMethodInFile_ShowScope && Psettings->mMethodsInFileNameFilter)
	{
		// [case: 1232] reduce noise by removing namespaces from non-namespace entries
		if (namespaceList.begin() != namespaceList.end())
		{
			namespaceList.sort(SortByReverseLength);

			const CStringW delim(IsCFile(ed->m_ftype) ? L"::" : L".");
			for (std::list<CStringW>::iterator it = namespaceList.begin(); it != namespaceList.end(); ++it)
			{
				const CStringW theNamespace(*it + delim);
				for (size_t i = 0; i < markers->Root().GetChildCount(); ++i)
				{
					FileLineMarker& mkr = *(markers->Root().GetChild(i));
					if (NAMESPACE == mkr.mType || PREPROCSTRING == mkr.mType || DEFINE == mkr.mType)
						continue;

					if (!StartsWith(mkr.mText, theNamespace, FALSE))
						continue;

					mkr.mText = mkr.mText.Mid(theNamespace.GetLength());
				}
			}
		}
	}
}

LineMarkersPtr EdCnt::GetMethodsInFile()
{
	// LineMarkers are built according to settings that can change.
	// Compare current settings to those in effect when cache was built.
	const int mifSettingsCookie =
	    (Psettings->m_sortDefPickList ? (1 << 0) : 0) | (Psettings->mParamsInMethodsInFileList ? (1 << 1) : 0) |
	    (Psettings->mMethodInFile_ShowDefines ? (1 << 2) : 0) | (Psettings->mMethodInFile_ShowEvents ? (1 << 3) : 0) |
	    (Psettings->mMethodInFile_ShowMembers ? (1 << 4) : 0) |
	    (Psettings->mMethodInFile_ShowProperties ? (1 << 5) : 0) |
	    (Psettings->mMethodInFile_ShowRegions ? (1 << 6) : 0) | (Psettings->mMethodInFile_ShowScope ? (1 << 7) : 0) |
	    (Psettings->mMethodsInFileNameFilter ? (1 << 8) : 0) | (m_ScopeLangType << 16);

	if (!mMifMarkers || m_modCookie != mMifMarkersCookie || mifSettingsCookie != mMifSettingsCookie ||
	    m_bufState != BUF_STATE_CLEAN)
	{
		mMifMarkersCookie = m_modCookie;
		mMifSettingsCookie = mifSettingsCookie;
		MultiParsePtr mp(GetParseDb());
		LineMarkersPtr mrkrs = std::make_shared<LineMarkers>();
		if (CAN_USE_NEW_SCOPE())
		{
			WTString buf = GetBuf(TRUE);
			if (!::GetMethodsInFile(buf, mp, *mrkrs))
			{
				// [case: 87527] timed out, invalidate cache for retry next time
				mMifMarkersCookie = 0;
			}
		}
		else
			mp->GetGotoMarkers(*mrkrs);

		::CleanupMarkerText(mThis, mrkrs.get());

		if (Psettings->m_sortDefPickList && !(Is_Tag_Based(m_ScopeLangType)))
			mrkrs->Root().Sort<MarkerAlphaLess>();
		else
			mrkrs->Root().Sort<MarkerLineLess>();

		mMifMarkers = mrkrs;
	}

	return mMifMarkers;
}

bool EdCnt::InsertSnippetFromList(const std::list<int>& snippetIndexes)
{
	// [case: 90458]

	enum
	{
		PRESERVE_ACCELERATORS = 1
	};

	if (snippetIndexes.size() == 1)
	{
		gAutotextMgr->Insert(mThis, *snippetIndexes.begin());
		return true;
	}
	else if (snippetIndexes.size() > 1)
	{
		// display menu with multiple items

		if (PopupMenuXP::IsMenuActive())
			PopupMenuXP::CancelActiveMenu();

		PopupMenuLmb mnu;

		// returns index of accelerator like "&a"
		// but jumps over escaped &'s in format "&&"
		// if 'last' is true, returns last as DrawText considers,
		// for example in "&A&B&C&D&E" is returned position of "&E"
		auto find_accelerator = [](const CStringW& str, int start, bool last) -> int {
			int acc = -1;
			int str_len = str.GetLength();
			for (int i = start; i < str_len; i++)
			{
				if (str[i] == '&' && (i + 1 < str_len))
				{
					if (str[i + 1] != '&')
						acc = i;

					if (!last)
						return acc;

					i++; // jump over next char
				}
			}

			return acc;
		};

		// returns 'true' if for corresponding number exists char in group [0-9A-Z],
		// in such case also fills passed ch by found value
		auto get_num_char = [](UINT num, WCHAR& ch) -> bool {
			if (num <= 9)
			{
				ch = WCHAR('0' + num);
				return true;
			}

			WCHAR tmp_ch = WCHAR('A' + (num - 10));
			if (tmp_ch >= 'A' && tmp_ch <= 'Z')
			{
				ch = tmp_ch;
				return true;
			}

			return false;
		};

		std::set<WCHAR> user_accelerators;

		if constexpr (PRESERVE_ACCELERATORS)
		{
			// populate user_accelerators
			for (int curItemIdx : snippetIndexes)
			{
				CStringW menuItemTxt(gAutotextMgr->GetTitle(curItemIdx, true).Wide());
				int acc_idx = find_accelerator(menuItemTxt, 0, true);
				if (acc_idx >= 0 && acc_idx + 1 < menuItemTxt.GetLength())
				{
					WCHAR ch = (WCHAR)::towupper(menuItemTxt[acc_idx + 1]);
					user_accelerators.insert(ch);
				}
			}
		}

		// populate popup menu
		uint acc_index = 0;
		for (int curItemIdx : snippetIndexes)
		{
			CStringW menuItemTxt(gAutotextMgr->GetTitle(curItemIdx, true).Wide());

			if (menuItemTxt.IsEmpty())
				continue;

			if constexpr (!PRESERVE_ACCELERATORS)
			{
				// remove user defined accelerators from name
				int acc_idx = find_accelerator(menuItemTxt, 0, false);
				while (acc_idx >= 0)
				{
					menuItemTxt.Delete(acc_idx, 1);
					acc_idx = find_accelerator(menuItemTxt, acc_idx + 1, false);
				}
			}

			if (!PRESERVE_ACCELERATORS || -1 == find_accelerator(menuItemTxt, 0, true))
			{
				// append new accelerator in group [1-9A-Z]

				WCHAR ch;
				while (get_num_char(++acc_index, ch))
				{
					// don't reuse accelerators already used by user
					if (PRESERVE_ACCELERATORS && user_accelerators.find(ch) != user_accelerators.end())
						continue;

					menuItemTxt.Append(L"\t&");
					menuItemTxt.AppendChar(ch);

					break;
				}
			}

			// command to execute when menu item is selected
			auto cmd = [this, curItemIdx] { gAutotextMgr->Insert(mThis, curItemIdx); };

			// add new item to list
			mnu.Items.push_back(MenuItemLmb(menuItemTxt, true, cmd, nullptr));
		}

		PopupMenuLmb::Events evs;

		// We can't rely on [1-9A-Z] chars, because on Unicode keyboards,
		// keys may represent language specific Unicode chars.
		// Following setting (MapVirtualKeysToInvariantChars) ensures that
		// even when user has Unicode keyboard and presses key 3,
		// menu will get char '3' and not some Unicode char used by
		// specific keyboard layout for active OS language.
		mnu.MapVirtualKeysToInvariantChars = true;

		// show the popup
		CPoint pt = vGetCaretPos();
		pt.y += g_FontSettings->GetCharHeight();
		vClientToScreen(&pt);
		TempTrue tt(m_contextMenuShowing);
		CMenuXPEffects efx(100, 255, 200, false, false); // allow fading
		PostMessage(WM_KEYDOWN, VK_DOWN, 1);
		bool res = mnu.Show(this, pt.x, pt.y, &evs);
		return res;
	}

	return false;
}

void EdCnt::ClearData()
{
	MultiParsePtr pmparse = MultiParse::Create(Other);
	pmparse->SetCacheable(TRUE);

	{
		AutoLockCs l(mDataLock);
		filename = "unsupported.file";
		pmparse->SetFilename(filename);
		SymScope.Empty();
		SymDef.Empty();
		SymType = DType{};
		m_lastScope.Empty();
		mTagParserScopeType.Empty();
		m_lastEditSymScope.Empty();
		if (mMifMarkers)
			mMifMarkers->Clear();
	}

	ClearBuf();
	m_ScopeLangType = Other;
	m_ftype = Other;
	SetNewMparse(pmparse, FALSE);
	mEolTypeChecked = false;
	mMifMarkersCookie = 0;
	mMifSettingsCookie = 0;
	m_modCookie = 0;
	mInitialParseCompleted = true;
	m_preventGetBuf = 0;
	m_txtFile = TRUE;
	modified = false;
	modified_ever = false;
}

void EdCnt::UpdateSym(DType* dt)
{
	WTString s;
	WTString d;

	if (dt)
	{
		s = dt->SymScope();
		d = dt->Def();
	}

	AutoLockCs l(mDataLock);
	if (dt)
	{
		SymScope = s;
		SymDef = d;
		SymType = *dt;
	}
	else
	{
		SymScope.Empty();
		SymDef.Empty();
		SymType.copyType(nullptr);
	}
}

bool ShareWith()
{
	EdCntPtr ed(g_currentEdCnt);

	if (!ed)
	{
		_ASSERTE(!"No EdCnt available!");
		return false;
	}

	WTString ext;
	if (ed->m_ftype == CS)
		ext = "cs";
	else if (IsCFile(ed->m_ftype))
		ext = "cpp";
	else if (ed->m_ftype == XML)
		ext = "xml";
	else if (ed->m_ftype == XAML)
		ext = "xaml";
	else
		ext = "txt";

	bool whole_file = false;
	WTString contents = ed->GetSelString();
	BOOL forceBuff = ed->m_bufState == CTer::BUF_STATE_CLEAN ? TRUE : FALSE;
	WTString full_contents = ed->GetBuf(forceBuff);
	if (contents.IsEmpty())
		whole_file = true;
	if (whole_file && full_contents.IsEmpty())
		return false;

	auto filename = ed->FileName();

// 	extern WTString RTFCopy2(EdCnt * ed, WTString * txtToFormat);
	int tab_size = g_IdeSettings ? g_IdeSettings->GetEditorIntOption("C/C++", "TabSize") : 0;
	if((tab_size < 0) || (tab_size > 16))
		tab_size = 4;
	_variant_t args[] = {(intptr_t)MainWndH, filename.GetString(), ext.c_str(), contents.c_str(), whole_file ? 1 : 0, tab_size, /*RTFCopy2(&*ed, &contents).c_str()*/ full_contents.c_str()};
	_variant_t result;
	if (gVaInteropService->InvokeDotNetMethod(IDS_VADEBUGGERTOOLS_DLLW, L"VaOther.ShareWithDlg",
		                                        L"DoModal", args, _countof(args), &result) &&
		result.vt == VT_BSTR && result.bstrVal && *result.bstrVal)
		return true;
	return false;
}
