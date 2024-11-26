// AutotextManager.cpp : implementation file
//

#include "stdafxed.h"
#include "AutotextManager.h"
#include "token.h"
#if _MSC_VER <= 1200
#include <../src/afximpl.h>
#endif
#include "wtcsym.h"
#include "Foo.h"
#include "Edcnt.h"
#include "rbuffer.h"
#include "../AddIn/DSCmds.h"
#include "../common/GlobalString.h"
#include <rpc.h>
#include "AutoTemplateConverter.h"
#include "VaTree.h"
#include "DefaultAutotext.h"
#include "project.h"
#include "wt_stdlib.h"
#include "RegKeys.h"
#include "EolTypes.h"
#include "file.h"
#include "Directories.h"
#include "expansion.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "Settings.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "WrapCheck.h"
#include "Usage.h"
#include "VACompletionSet.h"
#include "VAAutomation.h"
#include "StringUtils.h"
#include "ProjectInfo.h"
#include "VARefactor.h"
#include "IdeSettings.h"
#include "AutotextExpansion.h"
#include "UnicodeHelper.h"
#include "DllNames.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma comment(lib, "Rpcrt4")

#if _MSC_VER > 1200
using std::ifstream;
using std::ios;
#endif

using OWL::string;
using OWL::TRegexp;

AutotextManager* gAutotextMgr = NULL;
LPCTSTR kAutotextKeyword_Selection = "$selected$";
LPCTSTR kAutotextKeyword_Clipboard = "$clipboard$";
LPCTSTR kAutotextKeyword_End = "$end$";
LPCTSTR kAutotextSearch_IsolatedSelection = "^[ \t]*[$]selected[$][ \t]*$";
LPCTSTR kAutotextSearch_IsolatedSelectionPreEnd = "^[ \t]*[$]end[$][$]selected[$][ \t]*$";
LPCTSTR kAutotextSearch_IsolatedSelectionPostEnd = "^[ \t]*[$]selected[$][$]end[$][ \t]*$";

static CStringW GetSharedTemplateFilename(int type);
static CStringW GetSharedTemplateDir();
static CStringW GetLocalTemplateFilename(int type);
static CStringW GetLocalTemplateDir();
static bool TrimEmptyLine(WTString& text);
static void ProcessFileKeywords(const WTString& originalSource, token& processedSource, CStringW filename);
static void ProcessGuids(const WTString& originalSource, token& processedSource, const GUID* guidOverride = nullptr);
static void ProcessDateAndTime(const WTString& originalSource, token& processedSource,
                               const SYSTEMTIME* timeOverride = nullptr);
static void ProcessEnvironmentVariables(token& processedSource);
static void ProcessScope(const WTString& originalSource, token& processedSource, EdCnt* ed);
static void ProcessProjectSolution(const WTString& originalSource, token& processedSource, EdCnt* ed);
static void StripKeywordLines(token& processedSource, LPCTSTR keywordRegExp);
static void ConvertFormfeeds(token& processedSource);
static void IndentationRule2(token& processedSource, const WTString& currentSelection);
static void IndentationRule3(token& processedSource, WTString& currentSelection);
static void LinebreakRule2(token& processedSource, const WTString& currentSelection, bool didTrim, EdCnt* ed);
static bool ProcessClipboard(token& processedSource, const WTString& currentSelection, int selectionPos, EdCnt* ed);
static void DoSimpleIndentation(token& processedSource, const WTString& indentation);
static void SimplifyLinebreaks(WTString& str);
static WTString SimplifyLinebreaks(LPCTSTR str);
static void ReplaceItemSourceInFile(const WTString& itemHeader, const CStringW& theFile, WTString newSource);
static bool RenameItemInFile(const CStringW& theFile, const WTString& headerOld, const WTString& headerNew);
static bool IsUserVisibleSnippet(int langType, const WTString& title);
static VateLanguage GetVateLang(int type);
static bool NormalSnippetExpansionPerAST();

/////////////////////////////////////////////////////////////////////////////
// AutotextManager

void AutotextManager::CreateAutotextManager()
{
	ASSERT(!gAutotextMgr);
#if !defined(RAD_STUDIO)
	CAutoTemplateConverter cnvt;
	CWaitCursor curs;
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(Src));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(CS));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(JS));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(Java));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(HTML));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(PERL));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(VB));
	cnvt.ConvertIfRequired(::GetLocalTemplateFilename(Plain));
#endif
	gAutotextMgr = new AutotextManager;
}

void AutotextManager::DestroyAutotextManager()
{
	delete gAutotextMgr;
	gAutotextMgr = NULL;
}

AutotextManager::AutotextManager() : mLangType(0)
{
	mLocalTemplateFileTime.dwHighDateTime = mLocalTemplateFileTime.dwLowDateTime = 0xffffffff;
	mSharedTemplateFileTime.dwHighDateTime = mSharedTemplateFileTime.dwLowDateTime = 0xffffffff;
}

AutotextManager::~AutotextManager()
{
	mDynamicSelectionCmdInfo.clear();
	mTemplateCollection.RemoveAll();
}

WTString AutotextManager::GetTitle(int idx, bool skipRequiredDefaultItems) const
{
	if (-1 == idx || idx >= GetCount())
		return WTString();

	WTString title(mTemplateCollection.GetAt(idx).mTitle);

	if (skipRequiredDefaultItems)
	{
		// return empty string if item is a mandatory/default item
		if (!::IsUserVisibleSnippet(mLangType, title))
			title.Empty();
	}

	return title;
}

static CCriticalSection sSnippetLoaderLock;

void AutotextManager::Unload()
{
	AutoLockCs l(sSnippetLoaderLock);
	mLangType = Other;

	mLocalTemplateFileTime.dwHighDateTime = mLocalTemplateFileTime.dwLowDateTime = 0;
	mLocalTemplateFile.Empty();

	mSharedTemplateFileTime.dwHighDateTime = mSharedTemplateFileTime.dwLowDateTime = 0;
	mSharedTemplateFile.Empty();

	// remove all old items
	mDynamicSelectionCmdInfo.clear();
	mTemplateCollection.RemoveAll();
}

void AutotextManager::Load(int type)
{
	AutoLockCs l(sSnippetLoaderLock);

	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed && UC == ed->m_ftype && Src == type)
		{
			// overrride typing lang by ftype for UC since UC gets forced to Src in
			// EdCnt::Scope and EdCnt::OnSetFocus
			type = UC;
		}
	}

	if (Header == type)
		type = Src;

	// check file time and return if unchanged
	FILETIME chk;

	bool localTemplateFileDirty = false;
	bool sharedTemplateFileDirty = false;

	// LOCAL USER SNIPPETS
	const CStringW localTemplateFileToLoad(mLangType == type ? mLocalTemplateFile : ::GetLocalTemplateFilename(type));
	::GetFTime(localTemplateFileToLoad, &chk);
	if (localTemplateFileToLoad != mLocalTemplateFile || chk.dwHighDateTime != mLocalTemplateFileTime.dwHighDateTime ||
	    chk.dwLowDateTime != mLocalTemplateFileTime.dwLowDateTime)
	{
		// save the time
		localTemplateFileDirty = true;
		mLocalTemplateFileTime.dwHighDateTime = chk.dwHighDateTime;
		mLocalTemplateFileTime.dwLowDateTime = chk.dwLowDateTime;
		mLocalTemplateFile = localTemplateFileToLoad;
	}

	// SHARED SNIPPETS
	const CStringW sharedTemplateFileToLoad(mLangType == type ? mSharedTemplateFile
	                                                          : ::GetSharedTemplateFilename(type));
	::GetFTime(sharedTemplateFileToLoad, &chk);
	if (sharedTemplateFileToLoad != mSharedTemplateFile ||
	    chk.dwHighDateTime != mSharedTemplateFileTime.dwHighDateTime ||
	    chk.dwLowDateTime != mSharedTemplateFileTime.dwLowDateTime)
	{
		// save the time
		sharedTemplateFileDirty = true;
		mSharedTemplateFileTime.dwHighDateTime = chk.dwHighDateTime;
		mSharedTemplateFileTime.dwLowDateTime = chk.dwLowDateTime;
		mSharedTemplateFile = sharedTemplateFileToLoad;
	}

	if (!localTemplateFileDirty && !sharedTemplateFileDirty)
		return;

	mLangType = type;

	// remove all old items
	mDynamicSelectionCmdInfo.clear();
	mTemplateCollection.RemoveAll();

	// populate from file
	PopulateSnippetFile(mLocalTemplateFile);
	PopulateSnippetFile(mSharedTemplateFile);
}

bool AutotextManager::IsLoaded() const
{
	if (!mLocalTemplateFile.IsEmpty() || !mSharedTemplateFile.IsEmpty())
		return true;
	return false;
}

#pragma warning(push)
#pragma warning(disable : 4774)
static string _SprintfGuid(const GUID& g, const char* formatSpec)
{
	const int kBufLen = 256;
	ASSERT(strlen(formatSpec) < (kBufLen + 100));
	char buf[kBufLen + 1] = "";

	_snprintf(buf, kBufLen, formatSpec, g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
	          g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
	buf[kBufLen] = '\0';
	return string(buf);
}
#define SprintfGuid(g, formatSpec)                                                                                     \
	(true ? _SprintfGuid(g, formatSpec)                                                                                \
	      : (_snprintf_s(nullptr, 0, 0, formatSpec, g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],     \
	                     g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]),                                  \
	         string()))
#pragma warning(pop)

BOOL AutotextManager::InsertText(EdCntPtr ed, const WTString& inputText, bool isAutotextPoptype) const
{
	BOOL rslt;

	mDeletedEdSelection.Empty();
	if (!ed || inputText.IsEmpty())
	{
		_ASSERTE(ed || inputText.GetLength());
		return FALSE;
	}

	g_rbuffer.Add(inputText);

	if (isAutotextPoptype && IsItemTitle(inputText))
	{
		const int idx = GetItemIndex(inputText);
		if (!(0 <= idx && idx < GetCount()))
			return FALSE;

		// got here through a suggestion - delete the text
		// that caused the suggestion since it won't match
		// the autotext shortcut - don't want to treat it as
		// a selection for autotext processing.
		if (ed->HasSelection())
		{
			mDeletedEdSelection = ed->GetSelString();

			const WTString shortCut = GetShortcut(idx);
			if (!shortCut.IsEmpty() && 0 == shortCut.Find("/*") && 0 == mDeletedEdSelection.Find("/**") &&
			    -1 == mDeletedEdSelection.Find("/**/"))
			{
				const WTString cwdRight = ed->WordRightOfCursor();
				if (!cwdRight.IsEmpty() && '/' == cwdRight[0])
				{
					// [case: 109573]
					// add the trailing '/' to the selection
					// that the snippet overwrites
					long kOldCurPos = (long)ed->CurPos(true);
					long kSelBegPos = ed->GetSelBegPos();
					if (kOldCurPos < kSelBegPos)
					{
						ed->SwapAnchor();
						kOldCurPos = (long)ed->CurPos(true);
						kSelBegPos = ed->GetSelBegPos();
						ed->SwapAnchor(); // restore
					}

					++kOldCurPos;
					ed->SetSel(kSelBegPos, kOldCurPos);
					mDeletedEdSelection = ed->GetSelString();
				}
			}

			rslt = ed->Insert("");
			// need to set focus back into EdCnt else GetBuf fails and then we
			// aren't able to set caret pos properly after insertion.
			ed->vSetFocus();
		}

		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		const WTString kItemSource(it.mSource);

		if (EditUserInputFieldsInEditor(ed, kItemSource))
		{
			CStringW xml;
			IndentingMode mode = IndentingMode::VASnippets;
			if (ConvertVaSnippetToVsSnippetXML(ed, xml, kItemSource.Wide(), mode))
				if (InsertVsSnippetXML(ed, xml, mode))
					return TRUE;
		}

		// can't let IDE do format - $end$ handling is wrong if auto formatted.
		// See block with 'Special logic for ...' comment.
		// Maybe try vsfReplaceTabs here for case=6712
		rslt = DoInsert(idx, ed.get(), kItemSource, noFormat);
	}
	else
	{
		// not autotext or code template
		RealWrapperCheck chk(g_pUsage->mAutotextMgrInsertions, 150);
		rslt = ed->InsertW(inputText.Wide());
	}

	return rslt;
}

BOOL AutotextManager::Insert(EdCntPtr ed, int idx) const
{
	mDeletedEdSelection.Empty();
	if (!ed || 0 > idx || idx >= GetCount())
		return FALSE;

	const TemplateItem& it = mTemplateCollection.GetAt(idx);
	const WTString kItemSource(it.mSource);

	if (EditUserInputFieldsInEditor(ed, kItemSource))
	{
		CStringW xml;
		IndentingMode mode = IndentingMode::VASnippets;
		if (ConvertVaSnippetToVsSnippetXML(ed, xml, kItemSource.Wide(), mode))
			if (InsertVsSnippetXML(ed, xml, mode))
				return TRUE;
	}

	// can't let IDE do format - $end$ handling is wrong if auto formatted.
	// See block with 'Special logic for ...' comment.
	// Maybe try vsfReplaceTabs here for case=6712
	return DoInsert(idx, ed.get(), kItemSource, noFormat);
}

BOOL AutotextManager::InsertAsTemplate(EdCntPtr ed, const WTString& templateText, BOOL reformat /*= FALSE*/,
                                       const WTString& templateTitle /*= NULLSTR*/) const
{
	mDeletedEdSelection.Empty();
	if (!ed || templateText.IsEmpty())
	{
		_ASSERTE(ed && templateText.GetLength());
		return FALSE;
	}

	if (reformat && !Psettings->mRefactorAutoFormat && RefactoringActive::IsActive() && gShellAttr->IsDevenv8OrHigher())
	{
		// no ui option to control formatting added for VS2013 experimentation
		reformat = FALSE;
	}

	WTString tempTxt(templateText);
	::SimplifyLinebreaks(tempTxt);

	if (EditUserInputFieldsInEditor(ed, tempTxt))
	{
		CStringW xml;
		IndentingMode mode = IndentingMode::VASnippets;
		if (ConvertVaSnippetToVsSnippetXML(ed, xml, tempTxt.Wide(), mode))
			if (InsertVsSnippetXML(ed, xml, mode))
				return TRUE;
	}

	return DoInsert(-1, ed.get(), tempTxt, reformat ? vsfAutoFormat : noFormat, templateTitle);
}

// templateIdx will be -1 for on the fly templates not stored in a .tpl file
BOOL AutotextManager::DoInsert(int templateIdx, EdCnt* ed, const WTString& kItemSource, vsFormatOption reformat,
                               const WTString& title /*= NULLSTR*/) const
{
	_ASSERTE(kItemSource.GetLength());
	_ASSERTE(kItemSource.Find('\r') == -1);

	RealWrapperCheck chk(g_pUsage->mAutotextMgrInsertions, 200);
	WTString processedCode;
	if (-1 == templateIdx)
		processedCode = kItemSource;
	int finalPos = ProcessTemplate(templateIdx, processedCode, ed, title);
	// finalPos is the character offset in the processedCode string where
	// the caret should be left after insertion.
	if (-1 == finalPos)
		return TRUE;

	long kOldCurPos = (long)ed->CurPos(true);
	long kSelBegPos = ed->GetSelBegPos();
	if (kOldCurPos < kSelBegPos)
	{
		ed->SwapAnchor();
		kOldCurPos = (long)ed->CurPos(true);
		kSelBegPos = ed->GetSelBegPos();
		ed->SwapAnchor(); // restore
	}

	// [case: 15617] in html/tag, remove leading single leading '<' if
	// shortcut[0] == '<' when the editor has a shortcut match before insert pos
	if (Is_Tag_Based(mLangType) && -1 != templateIdx &&
	    // HTML: snippets selected after typing just '<', the < needs to be replaced. case=27012
	    //		mDeletedEdSelection.GetLength() &&
	    processedCode.GetLength() && processedCode[0] == '<')
	{
		const WTString shortCut = GetShortcut(templateIdx);
		if (shortCut.GetLength() && shortCut[0] == '<')
		{
			const WTString cwdLeft = ed->WordLeftOfCursor() + mDeletedEdSelection;
			if (shortCut.Find(cwdLeft) == 0 || cwdLeft.Find(shortCut) == 0)
			{
				processedCode = processedCode.Mid(1);
				if (finalPos)
					finalPos--;
			}
		}
	}

	BOOL rslt = ed->InsertW(processedCode.Wide(), true, reformat, false);
	if (FALSE == rslt)
		return rslt;

	const EolTypes::EolType processedEt = EolTypes::GetEolType(processedCode);
	if (vsfAutoFormat == reformat && finalPos < processedCode.GetLength())
	{
		// Special logic for calculating finalPos when code is reformatted
		int ln = ed->CurLine((uint)kOldCurPos);
		for (int n = 0; n < finalPos; n++)
		{
			if ((EolTypes::eolCr == processedEt && processedCode[n] == '\r') || processedCode[n] == '\n')
				ln++;
		}

		int eolPos = finalPos;
		for (; eolPos < processedCode.GetLength(); eolPos++)
		{
			if (processedCode[eolPos] == '\r' || processedCode[eolPos] == '\n')
				break;
		}

		const int kOnePos = ed->GetBufIndex((long)ed->LinePos(ln));
		const int kAnotherPos = ed->GetBufIndex((long)ed->LinePos(ln + 1)) - (eolPos - finalPos) -
		                        (EolTypes::eolCrLf == processedEt || EolTypes::eolNone == processedEt ? 2 : 1);
		const long epos = max(kOnePos, kAnotherPos);
		ed->SetPos((uint)epos);
	}
	else if (kItemSource.Find('\n') != -1 || kItemSource.Find(kAutotextKeyword_End) != -1)
	{
		// reposition caret to $end$ after insert
		if (EolTypes::eolNone != processedEt)
		{
			// calculate new caretPos after format
			const WTString edBuf(ed->GetBuf(TRUE));
			long idx = ed->GetBufIndex(edBuf, kSelBegPos);
			int ln = 0, c = 0;

			// find line and # of chars past first non space char
			int i;
			for (i = 0; i < finalPos && processedCode[i] != '\0'; i++)
			{
				if ((EolTypes::eolCr == processedEt && processedCode[i] == '\r') || processedCode[i] == '\n')
				{
					ln++;
					c = 0;
				}
				if (c || !::wt_isspace(processedCode[i]))
					c++;
			}

			// find same line in new text
			LPCSTR nbuf = edBuf.c_str();
			for (; nbuf[idx] && ln; idx++)
			{
				if ((EolTypes::eolCr == processedEt && nbuf[idx] == '\r') || nbuf[idx] == '\n')
					ln--;
			}
			// skip initial white
			for (i = 0; nbuf[idx] && (nbuf[idx + i] == ' ' || nbuf[idx + i] == '\t'); idx++)
				;
			idx += c;
			ed->SetPos((uint)idx);
		}
		else
		{
			// set curpos to savedPos + $end$ offset
			ed->SetPos(uint(kSelBegPos + finalPos));
		}
	}

	if (processedCode.contains("#include ") || processedCode.contains("#import "))
	{
		if (Psettings->mIncludeDirectiveCompletionLists)
			ed->SetTimer(DSM_VA_LISTMEMBERS, 50, NULL);
	}

	return TRUE;
}

// templateIdx will be -1 for on the fly templates not stored in a .tpl file
int AutotextManager::ProcessTemplate(int itemIdx, WTString& outInsertText, EdCnt* ed,
                                     const WTString& snippetTitle /*= NULLSTR*/) const
{
	const WTString kOriginalSource(-1 != itemIdx ? mTemplateCollection.GetAt(itemIdx).mSource : outInsertText);
	// get selected text
	WTString currentSelection = ed->GetSelString();
	const WTString currentIndentation = ed->GetCurrentIndentation();

	if (currentSelection.GetLength())
	{
		if (-1 != itemIdx)
		{
			// if this is autotext, then the shortcut is selected.
			// do not use the shortcut for kAutotextKeyword_Selection
			if (!currentSelection.Compare(GetShortcut(itemIdx)))
				currentSelection.Empty();
		}
		if (currentSelection.GetLength())
			::SimplifyLinebreaks(currentSelection); // convert to \n only

		// [case: 19247] need to escape user text
		currentSelection = ::EncodeUserText(currentSelection);
	}

	token rawCode = kOriginalSource;
	rawCode = ::HideVaSnippetEscapes(WTString(rawCode.c_str()));
	::ConvertFormfeeds(rawCode);
	::ProcessFileKeywords(kOriginalSource, rawCode, ed->FileName());
	::ProcessProjectSolution(kOriginalSource, rawCode, ed);
	::ProcessGuids(kOriginalSource, rawCode);
	::ProcessDateAndTime(kOriginalSource, rawCode);
	::ProcessEnvironmentVariables(rawCode);
	::ProcessScope(kOriginalSource, rawCode, ed);

	// DUPLICATED CODE #autotextCodeDuplication
	//
	// Linebreak rule 1.
	// trim final line of selection if it is empty
	bool didTrim = ::TrimEmptyLine(currentSelection);

	// if there is no selection,
	//    and the only occurrences of kAutotextKeyword_Selection
	//       are on lines by themselves (ignoring whitespace),
	//    and kAutotextKeyword_End is used in the autotext,
	// then remove the kAutotextSearch_IsolatedSelection lines
	if (currentSelection.IsEmpty() && -1 != kOriginalSource.Find(kAutotextKeyword_End))
		::StripKeywordLines(rawCode, kAutotextSearch_IsolatedSelection);

	// save position of first kAutotextKeyword_Selection
	const int kSelectedPos = rawCode.Str().Find(kAutotextKeyword_Selection);

	// handle clipboard replacements
	if (-1 != kOriginalSource.Find(kAutotextKeyword_Clipboard))
	{
		if (::ProcessClipboard(rawCode, currentSelection, kSelectedPos, ed))
			didTrim = true;
	}

	// Indentation rule 1.
	// If autotext starts with '#' then ignore rule 2 unless it is "#region"
	const int kRegionPos = kOriginalSource.Find("#region");
	const bool indentationRule1 =
	    ('#' == kOriginalSource[0] ||
	     (kOriginalSource.GetLength() > 5 && '$' == kOriginalSource[0] && '#' == kOriginalSource[5])) &&
	    kRegionPos != 0;

	// Indentation rule 2.
	// Indent autotext source based upon leading whitespace in initial
	// multiline selection.
	// But do not apply the leading whitespace to lines that only contain
	// kAutotextKeyword_Selection (ignoring whitespace and ignoring kAutotextKeyword_End).
	// This will only happen if the user made a multiline selection and then
	// invoked autotext via a menu (since by using a shortcut, the selection
	// would have been deleted).
	// This must occur before the selection is processed to prevent multiple
	// indentation passes on the selection.
	const bool indentationRule2 =
	    (-1 != currentSelection.Find('\n') || didTrim) && ('\t' == currentSelection[0] || ' ' == currentSelection[0]);
	if (indentationRule2 && !indentationRule1)
		::IndentationRule2(rawCode, currentSelection);

	// Linebreak rule 2.
	// Process before currentSelection is modified by indentation rule 3.
	::LinebreakRule2(rawCode, currentSelection, didTrim, ed);

	// Indentation rule 3.
	// if the current selection has multiple lines and the
	// autotext source contains kAutotextKeyword_Selection, then
	// prepend the leading whitespace from the autotext line
	// (that has the keyword) to each line of the selection.
	const bool indentationRule3 = (-1 != currentSelection.Find('\n') || didTrim) && -1 != kSelectedPos ||
	                              '\t' == currentSelection[0] || ' ' == currentSelection[0];
	if (indentationRule3)
		::IndentationRule3(rawCode, currentSelection);

	// Indentation handling for non-multiline selections
	if (!currentIndentation.IsEmpty() && !indentationRule1 && !indentationRule2 && !indentationRule3)
	{
		::DoSimpleIndentation(rawCode, currentIndentation);
	}
	//
	//// End duplicated block

	// Restore linebreaks before calculating caret placement since char cnt
	// will be different with \r\n than \n.
	const WTString lnBrk(ed->GetLineBreakString());
	_ASSERTE(strchr(rawCode.c_str(), '\r') == NULL); // SimplifyLinebreaks should have rid us of \r
	if (rawCode.ReplaceAll("\n", "\001"))
	{
		rawCode.ReplaceAll("\r", ""); // get rid of \r\r\n, VC6 will insert two lines
		rawCode.ReplaceAll("\001", lnBrk);
	}

	if (lnBrk != "\n")
		currentSelection.ReplaceAll("\n", lnBrk);

	// handle kAutotextKeyword_Selection replacements
	int selectedNewPos;
	if (-1 != kSelectedPos && currentSelection.IsEmpty())
	{
		// kAutotextKeyword_Selection was specified but there is no selection.
		// Save pos of the first kAutotextKeyword_Selection
		selectedNewPos = rawCode.Str().Find(kAutotextKeyword_Selection);
	}
	else
		selectedNewPos = -1;
	rawCode.ReplaceAll(kAutotextKeyword_Selection, currentSelection, FALSE);

	// handle user specified replacements
	WTString title(snippetTitle);
	if (title.IsEmpty())
		title = GetTitle(itemIdx, false);

	if (MakeUserSubstitutions(rawCode, title, ed) != IDOK)
		return -1;

	// restore escaped '='
	rawCode.ReplaceAll("\006", "=");
	// restore $$ combos without escapes
	rawCode.ReplaceAll("\005", "$");

	const WTString kFinalCode = ::DecodeUserText(WTString(rawCode.c_str()));

	int finalPos;
	// if kOriginalSource has a $end$ get the offset and remove the $end$
	const int keywordEndPos = kFinalCode.Find(kAutotextKeyword_End);
	if (-1 == keywordEndPos)
	{
		outInsertText = kFinalCode;
		if (-1 == selectedNewPos)
		{
			finalPos = kFinalCode.Find(kAutotextKeyword_Clipboard);
			if (-1 == finalPos)
			{
				finalPos = kFinalCode.GetLength();
			}
			else
			{
				outInsertText = kFinalCode;
				outInsertText.ReplaceAll(kAutotextKeyword_Clipboard, "", FALSE);
			}
		}
		else
			finalPos = selectedNewPos;
	}
	else
	{
		finalPos = keywordEndPos;
		outInsertText = kFinalCode.Left(finalPos);
		outInsertText += kFinalCode.Mid(finalPos + 5);
		outInsertText.ReplaceAll(kAutotextKeyword_End, "", FALSE);
	}

	return finalPos;
}

void AutotextManager::ApplyVAFormatting(CStringW& sourceW, CStringW& selectionW, CStringW& clipboardW, EdCnt* ed)
{
	const WTString currentIndentation = ed->GetCurrentIndentation();

	// convert unformatted text to UTF8 before we apply rules on it
	WTString source = sourceW;
	WTString selection = selectionW;
	WTString clipboard = clipboardW;

	if (!selection.IsEmpty())
		::SimplifyLinebreaks(selection); // convert to \n only

	token rawCode = source;
	::ConvertFormfeeds(rawCode);

	// DUPLICATED CODE #autotextCodeDuplication
	//
	// Linebreak rule 1.
	// trim final line of selection if it is empty
	bool didTrim = ::TrimEmptyLine(selection);

	// if there is no selection,
	//    and the only occurrences of kAutotextKeyword_Selection
	//       are on lines by themselves (ignoring whitespace),
	//    and kAutotextKeyword_End is used in the autotext,
	// then remove the kAutotextSearch_IsolatedSelection lines
	if (selection.IsEmpty() && -1 != source.Find(kAutotextKeyword_End))
		::StripKeywordLines(rawCode, kAutotextSearch_IsolatedSelection);

	// save position of first kAutotextKeyword_Selection
	const int kSelectedPos = rawCode.Str().Find(kAutotextKeyword_Selection);

	// handle clipboard replacements
	if (-1 != source.Find(kAutotextKeyword_Clipboard))
	{
		if (::TrimEmptyLine(clipboard))
			didTrim = true;
	}

	// Indentation rule 1.
	// If autotext starts with '#' then ignore rule 2 unless it is "#region"
	const int kRegionPos = source.Find("#region");
	const bool indentationRule1 =
	    ('#' == source[0] || (source.GetLength() > 5 && '$' == source[0] && '#' == source[5])) && kRegionPos != 0;

	// Indentation rule 2.
	// Indent autotext source based upon leading whitespace in initial
	// multiline selection.
	// But do not apply the leading whitespace to lines that only contain
	// kAutotextKeyword_Selection (ignoring whitespace and ignoring kAutotextKeyword_End).
	// This will only happen if the user made a multiline selection and then
	// invoked autotext via a menu (since by using a shortcut, the selection
	// would have been deleted).
	// This must occur before the selection is processed to prevent multiple
	// indentation passes on the selection.
	const bool indentationRule2 =
	    (-1 != selection.Find('\n') || didTrim) && ('\t' == selection[0] || ' ' == selection[0]);
	if (indentationRule2 && !indentationRule1)
		::IndentationRule2(rawCode, selection);

	// Linebreak rule 2.
	// Process before currentSelection is modified by indentation rule 3.
	::LinebreakRule2(rawCode, selection, didTrim, ed);

	// Indentation rule 3.
	// if the current selection has multiple lines and the
	// autotext source contains kAutotextKeyword_Selection, then
	// prepend the leading whitespace from the autotext line
	// (that has the keyword) to each line of the selection.
	const bool indentationRule3 =
	    (-1 != selection.Find('\n') || didTrim) && -1 != kSelectedPos || '\t' == selection[0] || ' ' == selection[0];
	if (indentationRule3)
		::IndentationRule3(rawCode, selection);

	// Indentation handling for non-multiline selections
	if (!currentIndentation.IsEmpty() && !indentationRule1 && !indentationRule2 && !indentationRule3)
	{
		::DoSimpleIndentation(rawCode, currentIndentation);
	}
	//
	//// End duplicated block

	// convert formatted text back to UTF16
	// see:#badStringTruncation bug in LinebreakRule2;
	// rawCode.length is 1 too many, ast tests depend on this bug (but maybe shouldn't, left as exercise for reader)
	// after this call, sourceW.GetLength() will include terminating null
	sourceW = ::MbcsToWide(rawCode.c_str(), rawCode.length());
	// this next line would be the correct behavior, but breaks tests and took all of thanksgiving weekend to identify
	// sourceW    = WTString(rawCode.c_str()).Wide();
	selectionW = selection.Wide();
	clipboardW = clipboard.Wide();
}

static void DoSimpleIndentation(token& processedSource, const WTString& indentation)
{
	TRegexp findRegExp = "[\n]";
	string tmpStr = "\001" + string(indentation.c_str());
	if (processedSource.ReplaceAll(findRegExp, tmpStr))
	{
		findRegExp = "\001";
		tmpStr = "\n";
		processedSource.ReplaceAll(findRegExp, tmpStr);
	}
}

static bool ProcessClipboard(token& processedSource, const WTString& currentSelection, int selectionPos, EdCnt* ed)
{
	bool didTrim = false;
	WTString clipTxt(GetClipboardText(ed->GetSafeHwnd()));
	if (!clipTxt.IsEmpty())
	{
		// Linebreak rule 1.
		if (TrimEmptyLine(clipTxt))
			didTrim = true;
	}

	TRegexp findRegExp = " ";
	string tmpStr;
	if (clipTxt.IsEmpty())
	{
		if ((-1 == selectionPos || (-1 != selectionPos && !currentSelection.IsEmpty())))
		{
			// empty clipboard overrides end
			const int kEndPos = processedSource.Str().Find(kAutotextKeyword_End);
			if (-1 != kEndPos)
			{
				findRegExp = "[$]end[$]";
				processedSource.ReplaceAll(findRegExp, tmpStr);
			}

			// retain kAutotextKeyword_Clipboard until end
		}
		else
		{
			// blow away kAutotextKeyword_Clipboard identifier
			findRegExp = "[$]clipboard[$]";
			processedSource.ReplaceAll(findRegExp, tmpStr);
		}
	}
	else
	{
		findRegExp = "[$]clipboard[$]";
		::SimplifyLinebreaks(clipTxt);
		tmpStr = clipTxt.c_str();
		processedSource.ReplaceAll(findRegExp, tmpStr);
	}

	return didTrim;
}

// Indentation rule 3.
// if the current selection has multiple lines and the
// autotext source contains kAutotextKeyword_Selection, then
// prepend the leading whitespace from the autotext line
// (that has the keyword) to each line of the selection.
static void IndentationRule3(token& processedSource, WTString& currentSelection)
{
	WTString leadingWhitespace;
	// find ^[ \t]kAutotextKeyword_Selection
	TRegexp srch("^[ \t]+[$]selected[$]");
	size_t pos = 0;
	string foundStr = processedSource.SubStr(srch, &pos);
	if (foundStr.length())
		leadingWhitespace = foundStr.c_str();
	else
	{
		srch = "^[ \t]+[$]end[$][$]selected[$]";
		foundStr = processedSource.SubStr(srch, &pos);
		if (!foundStr.length())
			return;

		leadingWhitespace = foundStr.c_str();
		leadingWhitespace.ReplaceAll(kAutotextKeyword_End, "");
	}

	// extract up to kAutotextKeyword_Selection
	leadingWhitespace = leadingWhitespace.Left(leadingWhitespace.GetLength() - strlen_i(kAutotextKeyword_Selection));
	// add to each ^ in currentSelection
	WTString oldArg(currentSelection);
	currentSelection.Empty();
	const int kLen = oldArg.GetLength();
	for (int idx = 0; idx < kLen; ++idx)
	{
		bool appendLeadingWhitespace = false;
		currentSelection += oldArg[idx];
		if ('\n' == oldArg[idx] && '\r' != oldArg[idx + 1])
			appendLeadingWhitespace = true;
		else if ('\r' == oldArg[idx] && '\n' != oldArg[idx + 1])
			appendLeadingWhitespace = true;

		if (appendLeadingWhitespace)
			currentSelection += leadingWhitespace;
	}
}

// Linebreak rule 2 (when there is a selection).
// If there is any selection at all,
//    and the selection was not trimmed
//    and the selection does not end with a linebreak
//    and the autotext ends with a linebreak
//    and the autotext contains $selected$ on line by itself (ignoring whitespace and ignoring kAutotextKeyword_End)
//    and the next character after the selection is a linebreak (in the editor)
// then omit the autotext terminating linebreak
//
// Linebreak rule 2 (when there is no selection).
// If there is no selection,
//    and the autotext ends with a linebreak
//    and the next character after the cur position is a linebreak (in the editor)
// then omit the autotext terminating linebreak
static void LinebreakRule2(token& processedSource, const WTString& currentSelection, bool didTrim, EdCnt* ed)
{
	bool trimAutotext = false;
	string autotextCode(processedSource.c_str());
	const size_t kCodeLen = autotextCode.length();

	const int kSelLen = currentSelection.GetLength();
	if (kSelLen && !didTrim && '\r' != currentSelection[kSelLen - 1] && '\n' != currentSelection[kSelLen - 1] &&
	    '\n' == autotextCode[kCodeLen - 1])
	{
		TRegexp srchSelected(kAutotextSearch_IsolatedSelection);
		size_t pos = 0;
		string fnd = processedSource.SubStr(srchSelected, &pos);
		if (!fnd.length())
		{
			srchSelected = kAutotextSearch_IsolatedSelectionPreEnd;
			fnd = processedSource.SubStr(srchSelected, &pos);
			if (!fnd.length())
			{
				srchSelected = kAutotextSearch_IsolatedSelectionPostEnd;
				fnd = processedSource.SubStr(srchSelected, &pos);
			}
		}

		if (fnd.length())
		{
			// Get the character immediately after the selection
			int p1 = ed->GetSelBegPos();
			int p2 = (int)ed->CurPos(true);
			if (p1 == p2)
			{
				ed->SwapAnchor();
				p1 = ed->GetSelBegPos();
				p2 = (int)ed->CurPos(true);
				ed->SwapAnchor(); // restore
				_ASSERTE(p1 != p2);
			}
			const char nextChar = ed->CharAt((uint)max(p1, p2));
			if ('\r' == nextChar || '\n' == nextChar)
				trimAutotext = true;
		}
	}
	else if (!kSelLen && kCodeLen && '\n' == autotextCode[kCodeLen - 1])
	{
		// Get the character immediately after the caret
		const uint p1 = ed->CurPos(true);
		const char nextChar = ed->CharAt(p1);
		if ('\r' == nextChar || '\n' == nextChar)
		{
			// this can be responsible for Create Declaration adding text
			// at end of file without inserting a linebreak.  The problem
			// may be due to the padding that VA adds to buf (so that next
			// char is not actually in the editor, just in va buf) or that
			// we should be checking something like p1+lenOfLinebreak
			// instead of p1.
			trimAutotext = true;
		}
	}

	if (trimAutotext)
	{
		// omit the autotext terminating linebreak if the
		// next character is a linebreak
		if (kCodeLen)
		{
			// this does not cause recalculation of length...
			// should be something like autotextCode = autotextCode.Left(kCodeLen - 1);
			// but fixing that breaks ast snippet tests
			// see #badStringTruncation
			autotextCode[kCodeLen - 1] = '\0';
		}
		processedSource = autotextCode;
	}
}

// Indentation rule 2.
// Indent autotext source based upon leading whitespace in initial
// multiline selection.
// But do not apply the leading whitespace to lines that only contain
// kAutotextKeyword_Selection (ignoring whitespace and ignoring kAutotextKeyword_End).
static void IndentationRule2(token& rawCode, const WTString& currentSelection)
{
	TRegexp srch("^[ \t]+");
	size_t pos = 0;
	token theInitialSelection(currentSelection);
	const string leadingWhitespace = theInitialSelection.SubStr(srch, &pos);
	if (leadingWhitespace.length() && 0 == pos)
	{
		WTString oldCode(rawCode.c_str());
		WTString newCode(leadingWhitespace.c_str());
		const int kLen = oldCode.GetLength();
		for (int idx = 0; idx < kLen; ++idx)
		{
			bool appendLeadingWhitespace = false;
			newCode += oldCode[idx];
			if ('\n' == oldCode[idx] && '\r' != oldCode[idx + 1])
				appendLeadingWhitespace = true;
			else if ('\r' == oldCode[idx] && '\n' != oldCode[idx + 1])
				appendLeadingWhitespace = true;

			if (appendLeadingWhitespace)
			{
				// do not append if line is just $selected$ with whitespace... (ignore kAutotextKeyword_End)
				WTString stringToSearch(oldCode.Right(oldCode.GetLength() - idx));
				stringToSearch.ReplaceAll(kAutotextKeyword_End, "");
				token halfCode(stringToSearch);
				TRegexp srchSelected(kAutotextSearch_IsolatedSelection);
				size_t pos2 = 0;
				string fnd = halfCode.SubStr(srchSelected, &pos2);
				if (fnd.length() && (0 == pos2 || 1 == pos2))
					appendLeadingWhitespace = false;
			}

			if (appendLeadingWhitespace && (idx + 1 == kLen))
			{
				// do not append whitespace if at end
				appendLeadingWhitespace = false;
			}

			if (appendLeadingWhitespace)
				newCode += leadingWhitespace.c_str();
		}
		rawCode = newCode.c_str();
	}
}

static void ConvertFormfeeds(token& rawCode)
{
	TRegexp findRegExp = "[\f]";
	string tmpStr = "\n";
	rawCode.ReplaceAll(findRegExp, tmpStr);
}

static void StripKeywordLines(token& processedSource, LPCTSTR keywordRegExp)
{
	TRegexp findRegExp = keywordRegExp;
	size_t foundPos = 0;
	for (;;)
	{
		string foundStr = processedSource.SubStr(findRegExp, &foundPos);
		if (!foundStr.length())
			break;
		// strip out the found line including the linebreak (which is not part of the found string)
		WTString oldCode(processedSource.Str());
		WTString newCode = oldCode.Left(foundPos);
		size_t nextCharPos = foundPos + foundStr.length();
		if (nextCharPos < (size_t)oldCode.length())
		{
			int linebreakCharCnt = 0;
			if ((oldCode[nextCharPos] == '\n' && oldCode[nextCharPos + 1] == '\r') ||
			    (oldCode[nextCharPos] == '\r' && oldCode[nextCharPos + 1] == '\n'))
				linebreakCharCnt = 2;
			else if (oldCode[nextCharPos] == '\n' || oldCode[nextCharPos] == '\r')
				linebreakCharCnt = 1;
			else
			{
				_ASSERTE(!"bad linebreakcount");
			}

			nextCharPos += linebreakCharCnt;
			newCode += oldCode.Right(oldCode.GetLength() - nextCharPos);
		}
		processedSource = newCode.c_str();
	}
}

static void ProcessScope(const WTString& originalSource, token& processedSource, EdCnt* ed)
{
	if (originalSource.Find("$SCOPE_LINE$") != -1)
	{
		TRegexp findRegExp = "[$]SCOPE_LINE[$]";
		ed->GetBuf(TRUE);
		ed->Scope(TRUE);
		ed->Scope(TRUE);
		token ln = ""; // ed->m_pmparse->GetLastRedBcl();
		ln.Strip(TRegexp("^[ 	]+"));
		ln.ReplaceAll(TRegexp("[\t\r\n]+"), string(" "));
		processedSource.ReplaceAll(findRegExp, ln.str);
	}

	EdCntPtr curEd(g_currentEdCnt);
	if (curEd)
	{
		// Expand ScopeInfo args case=2050
		ScopeInfoPtr si = curEd->ScopeInfoPtr();
		processedSource.ReplaceAll("$MethodName$", DecodeTemplates(si->m_MethodName));
		processedSource.ReplaceAll("$MethodArgs$", DecodeTemplates(si->m_MethodArgs));
		processedSource.ReplaceAll("$ClassName$", DecodeTemplates(si->m_ClassName));
		if (si->m_BaseClassName == WILD_CARD)
			processedSource.ReplaceAll("$BaseClassName$", ""); // [case: 39236]
		else
			processedSource.ReplaceAll("$BaseClassName$", DecodeTemplates(si->m_BaseClassName));
		processedSource.ReplaceAll("$NamespaceName$", si->m_NamespaceName);
		WTString src(processedSource.c_str());
	}
}

static void ProcessEnvironmentVariables(token& rawCode)
{
	if (rawCode.Str().FindOneOf("%") != -1)
	{
		if (gTestsActive && !NormalSnippetExpansionPerAST())
		{
			WTString ss(rawCode.c_str());
			ss.ReplaceAll("%username%", "AST_RUNNER");
			ss.ReplaceAll("%USERNAME%", "AST_RUNNER");
			rawCode = ss;
			if (rawCode.Str().FindOneOf("%") == -1)
				return;
		}

		uint len = rawCode.Str().GetLength() * 2 + 1024u;
		char* tmp = new char[len];
		// get size needed
		if (ExpandEnvironmentStrings(rawCode.c_str(), tmp, len) > 0)
			rawCode = tmp;
		delete[] tmp;
	}
}

void ProcessDateAndTime(const WTString& kOriginalSource, token& rawCode, const SYSTEMTIME* timeOverride /*= nullptr*/)
{
	typedef AutotextFmt ATF;
	static const LCID LOCALE_ENGLISH_US = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);

	TRegexp findRegExp = " ";
	string txt;
	WTString tmp;
	SYSTEMTIME st = {0};
	const int kBufLen = 64;
	char dateBuf[kBufLen] = "";

	if (AutotextManager::GetDateAndTime(st, timeOverride))
	{
		{
			// only these keywords are good for casing modifiers

			ATF fmt;
			fmt.AddModifiers(ATF::UPPERCASE, ATF::All);

			fmt.SetBaseKeyword("DATE_LOCALE");
			::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &st, NULL, dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("MONTHNAME");
			::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &st, "MMM", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("MONTHLONGNAME");
			::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &st, "MMMM", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("DAYNAME");
			::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &st, "ddd", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("DAYLONGNAME");
			::GetDateFormat(LOCALE_USER_DEFAULT, NULL, &st, "dddd", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("MONTHNAME_EN");
			::GetDateFormat(LOCALE_ENGLISH_US, NULL, &st, "MMM", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("MONTHLONGNAME_EN");
			::GetDateFormat(LOCALE_ENGLISH_US, NULL, &st, "MMMM", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("DAYNAME_EN");
			::GetDateFormat(LOCALE_ENGLISH_US, NULL, &st, "ddd", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);

			fmt.SetBaseKeyword("DAYLONGNAME_EN");
			::GetDateFormat(LOCALE_ENGLISH_US, NULL, &st, "dddd", dateBuf, kBufLen);
			fmt.Replacement = dateBuf;
			fmt.ReplaceAll(rawCode);
		}

		if (kOriginalSource.Find("$DATE$") != -1)
		{
			findRegExp = "[$]DATE[$]";
			tmp.WTFormat("%d/%02d/%02d", st.wYear, st.wMonth, st.wDay);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$YEAR$") != -1)
		{
			findRegExp = "[$]YEAR[$]";
			txt = itoa(st.wYear);
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$YEAR_02$") != -1)
		{
			findRegExp = "[$]YEAR_02[$]";
			tmp = itoa(st.wYear);
			txt = tmp.Right(2).c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$MONTH$") != -1)
		{
			findRegExp = "[$]MONTH[$]";
			txt = itoa(st.wMonth);
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$MONTH_02$") != -1)
		{
			findRegExp = "[$]MONTH_02[$]";
			tmp.WTFormat("%02d", st.wMonth);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$DAY$") != -1)
		{
			findRegExp = "[$]DAY[$]";
			txt = itoa(st.wDay);
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$DAY_02$") != -1)
		{
			findRegExp = "[$]DAY_02[$]";
			tmp.WTFormat("%02d", st.wDay);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$HOUR$") != -1)
		{
			findRegExp = "[$]HOUR[$]";
			txt = itoa(st.wHour);
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$HOUR_02$") != -1)
		{
			findRegExp = "[$]HOUR_02[$]";
			tmp.WTFormat("%02d", st.wHour);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$MINUTE$") != -1)
		{
			findRegExp = "[$]MINUTE[$]";
			tmp.WTFormat("%02d", st.wMinute);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
		if (kOriginalSource.Find("$SECOND$") != -1)
		{
			findRegExp = "[$]SECOND[$]";
			tmp.WTFormat("%02d", st.wSecond);
			txt = tmp.c_str();
			rawCode.ReplaceAll(findRegExp, txt);
		}
	}
}

bool AutotextManager::GetGuid(GUID& outGuid, const GUID* guidOverride /*= nullptr*/)
{
	if (gTestsActive && !NormalSnippetExpansionPerAST())
	{
		// For AST, use constant GUID
		return RPC_S_OK == ::UuidFromStringA((RPC_CSTR) "12345678-ABCD-CDEF-1234-12345678ABCD", &outGuid);
	}

	if (guidOverride != nullptr)
	{
		if (&outGuid != guidOverride)
			outGuid = *guidOverride;

		return true;
	}

	return SUCCEEDED(::CoCreateGuid(&outGuid));
}

bool AutotextManager::GetDateAndTime(SYSTEMTIME& outDT, const SYSTEMTIME* dtOverride /*= nullptr*/)
{
	if (gTestsActive && !NormalSnippetExpansionPerAST())
	{
		// in case of AST session,
		// fill some constant values

		outDT.wYear = 2015;
		outDT.wMonth = 9;
		outDT.wDay = 8;
		outDT.wHour = 7;
		outDT.wMinute = 56;
		outDT.wSecond = 34;
		outDT.wMilliseconds = 123;

		FILETIME ft = {0};
		return SystemTimeToFileTime(&outDT, &ft) == TRUE &&
		       FileTimeToSystemTime(&ft, &outDT) == TRUE; // fills st.wDayOfWeek
	}

	if (dtOverride != nullptr)
	{
		if (&outDT != dtOverride)
			outDT = *dtOverride;
	}
	else
		GetLocalTime(&outDT);

	return true;
}

static void ProcessGuids(const WTString& kOriginalSource, token& rawCode, const GUID* guidOverride /*= nullptr*/)
{
	// GUIDs are lower by default.
	// _UPPER is only modifier supported.
	if (kOriginalSource.Find("$GUID_STRUCT$") != -1 || kOriginalSource.Find("$GUID_STRUCT_UPPER$") != -1 ||
	    kOriginalSource.Find("$GUID_DEFINITION$") != -1 || kOriginalSource.Find("$GUID_DEFINITION_UPPER$") != -1 ||
	    kOriginalSource.Find("$GUID_STRING$") != -1 || kOriginalSource.Find("$GUID_STRING_UPPER$") != -1 ||
	    kOriginalSource.Find("$GUID_SYMBOL$") != -1 || kOriginalSource.Find("$GUID_SYMBOL_UPPER$") != -1)
	{
		GUID g;
		if (AutotextManager::GetGuid(g, guidOverride))
		{
			unsigned char* strUuid = NULL;
			if (RPC_S_OK == UuidToString(&g, &strUuid))
			{
				string txt;
				TRegexp findRegExp = " ";

				txt = (const char*)strUuid;
				findRegExp = "[$]GUID_STRING[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt.to_upper();
				findRegExp = "[$]GUID_STRING_UPPER[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt = SprintfGuid(g, "%08lx_%04x_%04x_%02x%02x_%02x%02x%02x%02x%02x%02x");
				findRegExp = "[$]GUID_SYMBOL[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt.to_upper();
				findRegExp = "[$]GUID_SYMBOL_UPPER[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt = SprintfGuid(
				    g, "0x%08lx, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x");
				findRegExp = "[$]GUID_DEFINITION[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt = SprintfGuid(
				    g, "0x%08lX, 0x%04X, 0x%04X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X");
				findRegExp = "[$]GUID_DEFINITION_UPPER[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt = SprintfGuid(
				    g, "0x%08lx, 0x%04x, 0x%04x, { 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x }");
				findRegExp = "[$]GUID_STRUCT[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				txt = SprintfGuid(
				    g, "0x%08lX, 0x%04X, 0x%04X, { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X }");
				findRegExp = "[$]GUID_STRUCT_UPPER[$]";
				rawCode.ReplaceAll(findRegExp, txt);

				RpcStringFree(&strUuid);
			}
		}
	}
}

struct ATF_FileModifiers
{
	typedef AutotextFmt ATF;

	WTString name, path, ext, folderName;

	void Reset()
	{
		name.Empty();
		path.Empty();
		ext.Empty();
		folderName.Empty();
	}

	static void TrimPathPartsForTesting(WTString& file)
	{
		_ASSERTE(gTestsActive);
		// remove IDE version postfix from file name
		// remove winX from file name
		file.ReplaceAllRE(LR"(win[0-9]+)", true, CStringW(L""));
		// remove store from file name
		file.ReplaceAll("store.sln", ".sln");
		// remove all from file name
		file.ReplaceAll("all.sln", ".sln");
		// - test for replacing by capture group
		file.ReplaceAllRE(LR"(_[0-9]+([.].+))", false, CStringW(L"$1"));

		// - test for replacing by lambda
		file.ReplaceAllRE(L"[.].+", false, [](int pos, CStringW& part) {
			WTString wt_part = part;

			// - test for matching RE
			if (wt_part.MatchRENoCase("^[.]vcx?proj$"))
				part = L".vcproj Test";
			else if (wt_part.MatchRENoCase("^[.]sln$"))
				part = L".sln Test";

			return true;
		});

		if (GlobalProject)
		{
			WTString solu = GlobalProject->SolutionFile();
			// - test for replacing by simple string
			solu.ReplaceAllRE(LR"(\\[^\\]+?\\[^\\]+?[.].+)", false, CStringW(L""));

			// replace part below solution directory by "(path removed)"
			file = "(path removed)" + file.substr(solu.GetLength());
		}
	}

	void AddToATF(ATF& fmt, LPCSTR base_name = "_BASE")
	{
		//////////////////////////////////////////////////////////////////
		// _NAME, _NAME_UPPER, _NAME_LOWER, _NAME_CAMEL, _NAME_PASCAL
		// _BASE, _BASE_UPPER, _BASE_LOWER, _BASE_CAMEL, _BASE_PASCAL
		fmt.AddModifiers(ATF::UPPERCASE, ATF::All, base_name, [this](WTString& file) mutable {
			if (name.IsEmpty())
			{
				name = Basename(file);
				name = name.Left(name.ReverseFind('.'));
			}
			file = name;
		});

		//////////////////////////////////////////////////////////////////
		// _PATH, _PATH_UPPER, _PATH_LOWER, _PATH_CAMEL, _PATH_PASCAL
		fmt.AddModifiers(ATF::UPPERCASE, ATF::All, "_PATH", [this](WTString& file) mutable {
			if (path.IsEmpty())
			{
				const int pos1 = file.ReverseFind('\\');
				const int pos2 = file.ReverseFind('/');
				const int thePos = pos1 == -1 ? pos2 : (pos2 == -1 ? pos1 : min(pos1, pos2));
				path = file.Left(thePos);
			}
			file = path;
		});

		//////////////////////////////////////////////////////////////////
		// _EXT, _EXT_UPPER, _EXT_LOWER, _EXT_CAMEL, _EXT_PASCAL
		fmt.AddModifiers(ATF::UPPERCASE, ATF::All, "_EXT", [this](WTString& file) mutable {
			if (ext.IsEmpty())
			{
				WTString tmp = Basename(file);
				ext = tmp.Mid(tmp.ReverseFind('.') + 1, tmp.GetLength());
			}
			file = ext;
		});

		//////////////////////////////////////////////////////////////////
		// _FOLDER_NAME
		// [case: 164231]
		fmt.AddModifiers(ATF::UPPERCASE, ATF::All, "_FOLDER_NAME", [this](WTString& file) mutable {
			if (folderName.IsEmpty())
			{
				// first find full path of the folder
				int pos1 = file.ReverseFind('\\');
				int pos2 = file.ReverseFind('/');
				int thePos = pos1 == -1 ? pos2 : (pos2 == -1 ? pos1 : min(pos1, pos2));
				folderName = file.Left(thePos);

				// then cut full path to get just folder name
				pos1 = folderName.ReverseFind('\\');
				pos2 = folderName.ReverseFind('/');
				thePos = pos1 == -1 ? pos2 : (pos2 == -1 ? pos1 : max(pos1, pos2));
				thePos += 1;
				folderName = folderName.Right(folderName.length() - thePos);
			}
			file = folderName;
		});
	}
};

static void ProcessFileKeywords(const WTString& kOriginalSource, token& rawCode, CStringW filename)
{
	typedef AutotextFmt ATF;
	ATF fmt;
	fmt.SetBaseKeyword("FILE");
	fmt.Replacement = filename;

	if (gTestsActive && !NormalSnippetExpansionPerAST())
		ATF_FileModifiers::TrimPathPartsForTesting(fmt.Replacement);

	//////////////////////////////////////////////////////////////////
	// _UPPER, _LOWER, _CAMEL, _PASCAL
	fmt.AddModifiers(ATF::UPPERCASE, ATF::All);

	ATF_FileModifiers file_mods;
	file_mods.AddToATF(fmt);

	fmt.ReplaceAll(rawCode);
}

static void ProcessProjectSolution(const WTString& originalSource, token& processedSource, EdCnt* ed)
{
	if (GlobalProject == nullptr)
	{
		_ASSERTE("GlobalProject is not assigned");
		return;
	}

	typedef AutotextFmt ATF;
	ATF fmt;
	fmt.SetBaseKeyword("PROJECT"); // base of keywords

	// this takes text which is passed to each modifier during process
	fmt.GetReplacement = [ed](WTString& file) {
		ProjectVec projs = GlobalProject->GetProjectForFile(ed->FileName());
		if (!projs.empty())
		{
			file = projs.front()->GetProjectFile();

			if (gTestsActive && !NormalSnippetExpansionPerAST())
				ATF_FileModifiers::TrimPathPartsForTesting(file);
		}
	};

	//////////////////////////////////////////////////////////////////
	// _FILE, _FILE_UPPER, _FILE_LOWER, _FILE_CAMEL, _FILE_PASCAL
	fmt.AddModifiers(ATF::UPPERCASE, ATF::All, "_FILE", [](WTString& file) {});

	ATF_FileModifiers file_mods;
	file_mods.AddToATF(fmt, "_NAME");

	fmt.ReplaceAll(processedSource);

	fmt.Replacement.Empty();
	file_mods.Reset();
	fmt.SetBaseKeyword("SOLUTION");
	fmt.GetReplacement = [](WTString& file) {
		CStringW w_solu_f = GlobalProject->SolutionFile();
		file = w_solu_f;

		if (gTestsActive && !NormalSnippetExpansionPerAST())
			ATF_FileModifiers::TrimPathPartsForTesting(file);
	};

	fmt.ReplaceAll(processedSource);
}

WTString AutotextManager::GetShortcut(int idx) const
{
	if (idx < GetCount())
		return mTemplateCollection.GetAt(idx).mShortcut;
	return WTString();
}

WTString AutotextManager::GetTitleOfFirstSourceMatch(const WTString& search) const
{
	const int kCnt = GetCount();
	for (int idx = 0; idx < kCnt; ++idx)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		if (it.mSource[0] == search[0] && it.mSource.Find(search) == 0)
			return it.mTitle;
	}
	return WTString();
}

#define STRCMP_AC(s1, s2) (g_doDBCase ? StrCmp(s1, s2) : StrCmpI(s1, s2)) // Case insensitive shortcuts in VB.
WTString AutotextManager::FindNextShortcutMatch(const WTString& potentialShortcut, int& pos)
{
	if (!Psettings->m_codeTemplateTooltips)
		return WTString();
	WTString potentialShortcutExt = ::GetPotentialShortcutExt(potentialShortcut);

	Load(gTypingDevLang);
	const int kCnt = GetCount();
	_ASSERTE((pos == -1 || pos < kCnt) && potentialShortcut.c_str());
	for (pos++; pos < kCnt; ++pos)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(pos);
		WTString shortCut(it.mShortcut);
		if (!shortCut.IsEmpty() && STRCMP_AC(shortCut.c_str(), potentialShortcut.c_str()) == 0)
		{
			// don't suggest alpha snippets in comments or strings.
			if (!ISALPHA(potentialShortcut[0]) ||
			    (g_currentEdCnt && g_currentEdCnt->m_lastScope.c_str()[0] == DB_SEP_CHR))
				return it.mTitle;
		}
		else if (potentialShortcutExt.GetLength() && STRCMP_AC(shortCut.c_str(), potentialShortcutExt.c_str()) == 0)
			return it.mTitle;
	}
	pos = -1;
	return WTString();
}

int AutotextManager::GetSnippetsForShortcut(const WTString& shortcutToMatch, std::list<int>& matches)
{
	Load(gTypingDevLang);
	const int kCnt = GetCount();
	for (int pos = 0; pos < kCnt; ++pos)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(pos);
		const WTString shortCut(it.mShortcut);
		if (!shortCut.IsEmpty() && STRCMP_AC(shortCut.c_str(), shortcutToMatch.c_str()) == 0)
			matches.push_back(pos);
	}

	return (int)matches.size();
}

int AutotextManager::GetSurroundWithSnippetsForSpecialShortcut(char shortcutKeyToMatch, std::list<int>& matches)
{
	Load(gTypingDevLang);
	const int kCnt = GetCount();
	for (int pos = 0; pos < kCnt; ++pos)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(pos);
		if (it.mSpecialShortcut && it.mSpecialShortcut == shortcutKeyToMatch)
		{
			if (DoesItemUseString(pos, "$selected$"))
				matches.push_back(pos);
		}
	}

	return (int)matches.size();
}

void AutotextManager::AddAutoTextToExpansionData(const WTString& potentialShortcut, ExpansionData* m_lst,
                                                 BOOL addAll /*= FALSE*/)
{
	if (!Psettings->m_codeTemplateTooltips)
		return;

	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed && !Psettings->mAllowSnippetsInUnrealMarkup)
		{
			ScopeInfoPtr si(ed->ScopeInfoPtr());
			if (si->HasUe4SuggestionMode())
			{
				// [case: 111552]
				return;
			}
		}
	}

	Load(gTypingDevLang);
	const WTString potentialShortcutExt = ::GetPotentialShortcutExt(potentialShortcut);
	const int kCnt = GetCount();
	_ASSERTE(potentialShortcut.c_str());
	const BOOL overrideAll = false;
#if 0
	BOOL overrideAll = mLangType == CS && gShellAttr->IsDevenv8OrHigher();
	if (overrideAll && g_currentEdCnt && g_currentEdCnt->ScopeInfoPtr()->HasScopeSuggestions())
		overrideAll = false; // case=32585 don't display all when there are scope suggestions
	if (gShellAttr->IsDevenv() || Psettings->m_UseVASuggestionsInManagedCode)
		overrideAll = false; // [case=42309] Only show Snippets with exact shortcut match
#endif // 0

	const bool inlineSnippetExpansionActive = gExpSession != nullptr;
	const BOOL filterNonCSym = (!potentialShortcut.GetLength() || ISCSYM(potentialShortcut[0]));
	for (int pos = 0; pos < kCnt; ++pos)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(pos);
		const WTString curTitle(it.mTitle);
		if (!::IsUserVisibleSnippet(mLangType, curTitle))
			continue; // [case: 24994] don't display default refactor snippets

		if (inlineSnippetExpansionActive)
		{
			const WTString kItemSource(it.mSource);
			if (-1 != kItemSource.FindOneOf("\r\n"))
			{
				// [case: 88949]
				continue;
			}
		}

		const WTString shortCut(it.mShortcut);
		if (overrideAll)
		{
			// case=32585 don't display all when there are scope suggestions
			if (!shortCut.IsEmpty())
			{
				// Filter non-csym snippets from IDE suggestion lists. case=21971
				if (!filterNonCSym || ISCSYM(shortCut[0]))
					m_lst->AddStringAndSort(curTitle + AUTOTEXT_SHORTCUT_SEPARATOR + shortCut, ET_AUTOTEXT, 0,
					                        WTHashKey(curTitle), 0);
				else if (potentialShortcutExt.GetLength() && shortCut == potentialShortcutExt.c_str())
					m_lst->AddStringAndSort(curTitle + AUTOTEXT_SHORTCUT_SEPARATOR + shortCut, ET_AUTOTEXT, 0,
					                        WTHashKey(curTitle), 0);
			}
		}
		else
		{
			// Only add matching autotext
			if (addAll)
			{
				// Filter non-csym snippets from IDE suggestion lists. case=21971
				if (!filterNonCSym || ISCSYM(curTitle[0]))
					m_lst->AddStringAndSort(curTitle + AUTOTEXT_SHORTCUT_SEPARATOR + shortCut, ET_AUTOTEXT, 0,
					                        WTHashKey(curTitle), 0);
			}
			else if (shortCut.GetLength() && potentialShortcut.GetLength() &&
			             (STRCMP_AC(shortCut.c_str(), potentialShortcut.c_str()) == 0 ||
			              STRCMP_AC(curTitle.c_str(), potentialShortcut.c_str()) == 0) ||
			         (Psettings->mPartialSnippetShortcutMatches && shortCut.GetLength() > 2 &&
			          potentialShortcut.GetLength() > 2 && ::StartsWithNC(shortCut, potentialShortcut, FALSE)))
			{
				// don't suggest alpha snippets in comments or strings.
				if (!ISALPHA(potentialShortcut[0]) ||
				    (g_currentEdCnt && g_currentEdCnt->m_lastScope.c_str()[0] == DB_SEP_CHR))
					m_lst->AddStringAndSort(curTitle + AUTOTEXT_SHORTCUT_SEPARATOR + shortCut, ET_AUTOTEXT, 0,
					                        WTHashKey(curTitle), 0);
			}
			else if (potentialShortcutExt.GetLength() &&
			             (STRCMP_AC(shortCut.c_str(), potentialShortcutExt.c_str()) == 0 ||
			              STRCMP_AC(curTitle.c_str(), potentialShortcutExt.c_str()) == 0) ||
			         (Psettings->mPartialSnippetShortcutMatches && shortCut.GetLength() > 2 &&
			          potentialShortcutExt.GetLength() > 2 && ::StartsWithNC(shortCut, potentialShortcutExt, FALSE)))
			{
				m_lst->AddStringAndSort(curTitle + AUTOTEXT_SHORTCUT_SEPARATOR + shortCut, ET_AUTOTEXT, 0,
				                        WTHashKey(curTitle), 0);
			}
		}
	}
}

bool AutotextManager::IsItemTitle(const WTString& txt) const
{
	const int kCnt = GetCount();
	for (int idx = 0; idx < kCnt; ++idx)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		if (it.mTitle == txt)
			return true;
	}
	return false;
}

// this algorithm is also used by vate2.dll snippet editor
void AutotextManager::EnsureTitleIsUnique(WTString& newTitle)
{
	int baseMatchCount = 0;
	WTString titleToCheck(newTitle);

	while (IsItemTitle(titleToCheck))
		titleToCheck.WTFormat("%s (%d)", newTitle.c_str(), ++baseMatchCount);

	if (baseMatchCount)
		newTitle = titleToCheck;
}

int AutotextManager::GetItemIndex(const char* title) const
{
	const int kCnt = GetCount();
	for (int idx = 0; idx < kCnt; ++idx)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		if (it.mTitle == title)
			return idx;
	}
	return -1;
}

bool AutotextManager::DoesItemUseString(int idx, LPCTSTR str) const
{
	if (idx < GetCount())
	{
		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		if (-1 != it.mSource.Find(str))
			return true;
	}
	return false;
}

static const WTString kRefactorFilter(">>><<<VA-Refactor-Filter>>><<<");

void AutotextManager::EditRefactorSnippets(int type /*= 0*/)
{
	Edit(type, kRefactorFilter.c_str());
}

bool AutotextManager::IsReservedString(const WTString& match)
{
	if (match.IsEmpty() || match.GetAt(0) != '$' || match.Find('=') != -1)
		return false;

	bool found = false;
	ForEachReservedString([&](const ReservedString* rs, char modif, const WTString& str) {
		if (str == match)
		{
			found = true;
			return false;
		}
		return true;
	});

	return found;
}

void AutotextManager::ExpandReservedString(EdCntPtr ed, WTString& in_out, const GUID* guid /*= nullptr*/,
                                           const SYSTEMTIME* time /*= nullptr*/)
{
	token rawCode = in_out;
	const WTString& originalCode = in_out;

	auto AreEqual = [&]() -> bool {
		if (originalCode.GetLength() != rawCode.length())
			return false;

		return originalCode == rawCode.c_str();
	};

	::ProcessProjectSolution(originalCode, rawCode, ed.get());

	if (AreEqual())
		::ProcessGuids(originalCode, rawCode, guid);
	else
		goto done;

	if (AreEqual())
		::ProcessDateAndTime(originalCode, rawCode, time);
	else
		goto done;

	if (AreEqual())
		::ProcessEnvironmentVariables(rawCode);
	else
		goto done;

	if (AreEqual())
		::ProcessScope(originalCode, rawCode, ed.get());
	else
		goto done;

	if (AreEqual())
		::ProcessFileKeywords(originalCode, rawCode, ed->FileName());
	else
		goto done;

	if (AreEqual())
	{
		gAutotextMgr->ProcessTemplate(-1, in_out, ed.get());
		return;
	}

done:
	in_out = rawCode.c_str();
}

void AutotextManager::ForEachReservedString(std::function<bool(const ReservedString*, char, const WTString&)> func)
{
	for (const ReservedString* rs = gReservedStrings; rs->KeyWord; rs++)
	{
		WTString start_str;
		start_str += "$";
		start_str += rs->KeyWord;
		start_str.ReplaceAll("&", "");

		for (LPCSTR c = rs->Modifiers; c && *c; c++)
		{
			LPCSTR end_str = "$";
			switch (*c)
			{
			case 'D':
				break;
			case 'U':
				switch (rs->ModifiersCase)
				{
				case 'U':
					end_str = "_UPPER$";
					break;
				case 'L':
					end_str = "_upper$";
					break;
				case 'P':
					end_str = "_Upper$";
					break;
				}
				break;
			case 'L':
				switch (rs->ModifiersCase)
				{
				case 'U':
					end_str = "_LOWER$";
					break;
				case 'L':
					end_str = "_lower$";
					break;
				case 'P':
					end_str = "_Lower$";
					break;
				}
				break;
			case 'C':
				switch (rs->ModifiersCase)
				{
				case 'U':
					end_str = "_CAMEL$";
					break;
				case 'L':
					end_str = "_camel$";
					break;
				case 'P':
					end_str = "_Camel$";
					break;
				}
				break;
			case 'P':
				switch (rs->ModifiersCase)
				{
				case 'U':
					end_str = "_PASCAL$";
					break;
				case 'L':
					end_str = "_pascal$";
					break;
				case 'P':
					end_str = "_Pascal$";
					break;
				}
				break;
			}

			WTString full_kw = start_str + end_str;

			if (!func(rs, *c, full_kw))
				return;
		}
	}
}

bool AutotextManager::IsUserInputSnippet(const WTString& input)
{
	CStringW snipp = input.Wide();

	LPCWSTR s_pos = snipp;
	LPCWSTR e_pos = s_pos + snipp.GetLength();

	// #VASnippetsKWRegex
	const auto& rgx = GetKeywordsRegex();
	std::wcmatch m;

	while (std::regex_search(s_pos, e_pos, m, rgx))
	{
		if (m[4].first == m[0].first && m[4].second == m[0].second)
		{
			// [case: 95951] mimic behavior of old engine
			s_pos = m[0].second;
			continue; // skip - this is not a valid keyword
		}

		if (!AutotextManager::IsReservedString(m[0].str().c_str()))
			return true;

		s_pos = m[0].second;
	}

	return false;
}

void AutotextManager::Edit(int type /*= 0*/, LPCSTR snipName /*= NULL*/, const WTString& snipText)
{
	// need to prevent recursive call - due to being triggered from
	// the completionBox during notification handling
	static bool sIsActive = false;
	if (sIsActive || !gAutotextMgr)
		return;
	sIsActive = true;
	try
	{
		gAutotextMgr->LoadVate();
		if (gAutotextMgr->mVateDll.IsLoaded())
		{
			ShowAutotextEditorWFn pfnEditAutotextDlg;
			gAutotextMgr->mVateDll.GetFunction("ShowAutotextEditorW", pfnEditAutotextDlg);
			if (pfnEditAutotextDlg)
			{
				CStringW snipTextW(snipText.Wide());
				const CStringW kAutotextDirW(GetLocalTemplateDir());
				const CStringW kSharedAutotextDirW(GetSharedTemplateDir());
				const CStringW kSettingsRegPath(ID_RK_APP + "\\VATE\\Autotext Dialog");
				AutotextEditorParams params(MainWndH, (LPCWSTR)kAutotextDirW, (LPCWSTR)kSharedAutotextDirW,
				                            (LPCWSTR)kSettingsRegPath, gReservedStrings);
				if (type)
				{
					VateLanguage Lang = ::GetVateLang(type);
					params.mRootSnippetLanguage = Lang;

					if (!gAutotextMgr->GetCount() || Lang == vlEND)
					{
						// default to C++
						type = Src;
						params.mRootSnippetLanguage = vlCPP;
						gAutotextMgr->Load(type);
					}
				}

				if (snipName)
				{
					int id = -1;
					if (kRefactorFilter == snipName)
					{
						params.mSnippetFilter = sfRefactor;
						if (type == CS)
							snipName = "Refactor Create File";
						else
							snipName = "Refactor Create From Usage Class";

						id = gAutotextMgr->GetItemIndex(snipName);
						if (-1 == id)
						{
							snipName = "Refactor Document Method";
							id = gAutotextMgr->GetItemIndex(snipName);
						}
					}

					if (-1 == id)
						id = gAutotextMgr->GetItemIndex(snipName);
					params.mSnippetSelectionIndex = (UINT)id;
				}

				if (!snipTextW.IsEmpty())
					params.mNewSnippetText = snipTextW;

				// duplicate any changes here in VAOpsWin - PropPageSuggestions.cpp
				params.mRestrictToPrimaryLangs = Psettings->mRestrictVaToPrimaryFileTypes;
				params.mColumnIndicatorWidth = Psettings->m_colIndicatorColPos;
				params.mFontSize = Psettings->FontSize; // this defaults to 10 and doesn't appear to get updated
				if (gShellAttr->IsDevenv10OrHigher())
				{
					LOGFONTW envLf;
					ZeroMemory((PVOID)&envLf, sizeof(LOGFONTW));
					g_IdeSettings->GetEditorFont(&envLf);
					if (envLf.lfFaceName[0])
					{
						const CStringW fnt(envLf.lfFaceName);
						if (!fnt.IsEmpty() && fnt.GetLength() < 255)
						{
							::wcscpy(Psettings->m_srcFontName, fnt);
							params.mFontFace = Psettings->m_srcFontName;
						}
					}
				}
				else
				{
					if (g_pUsage && g_pUsage->mFilesOpened)
						params.mFontFace = Psettings->m_srcFontName;
					else
					{
						// [case: 132425]
						// if no editor has been opened, don't change scintilla default font
						params.mFontFace = nullptr;
					}
				}

				{
					VsUI::CDpiScope dpi(true);
					pfnEditAutotextDlg(&params);
				}

				// these are necessary due to weird problem after dlg display
				HWND hFoc = ::GetFocus();
				::SetFocus(MainWndH);
				::SetFocus(hFoc);

				if (gAutotextMgr)
					gAutotextMgr->Load(type);
			}
			else
			{
				const CString errMsg("Missing function export in VA Snippet editor dll");
				WtMessageBox(errMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
			}
		}
		else
		{
			const CString errMsg("Unable to load VA Snippet editor");
			WtMessageBox(errMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("ATM:");
		ASSERT(!"template edit exception");
	}

	sIsActive = false;
}

WTString AutotextManager::GetSource(int idx) const
{
	if (-1 == idx)
		return WTString();

	if (idx < GetCount())
		return mTemplateCollection.GetAt(idx).mSource;
	return WTString();
}

WTString AutotextManager::GetSource(LPCTSTR title)
{
	WTString src;
	const int kCnt = GetCount();
	for (int idx = 0; idx < kCnt; ++idx)
	{
		const TemplateItem& it = mTemplateCollection.GetAt(idx);
		if (it.mTitle == title)
		{
			src = it.mSource;
			break;
		}
	}

	if (!src.IsEmpty() && IsCFile(gTypingDevLang) && WTString("Refactor Extract Method") == title)
	{
		// [case: 87455]
		// extract to source fails badly if $end$ is not in the default position
		int endTagPos = src.Find("$end$$SymbolType$");
		if (-1 == endTagPos)
		{
			src.ReplaceAll("$end$", "");
			src.ReplaceAll("$SymbolType$", "$end$$SymbolType$");
		}
	}
	else if (src.IsEmpty())
	{
		const TemplateItem* item(::GetDefaultAutotextItem(mLangType, title));
		if (item)
		{
			AppendDefaultItem(item);
			src = item->mSource;
		}
	}

	return src;
}

void AutotextManager::AppendDefaultItem(const TemplateItem* item)
{
	_ASSERTE(item);

	// add to current collection
	TemplateItem newItem(item->mTitle, item->mShortcut, item->mSource);
	mTemplateCollection.Add(newItem);

	// create record
	LPCTSTR itemDescription = _T("VA Snippet used for refactoring.\nIf you have modified this item, you may delete it ")
	                          _T("to restore the default upon next use.");
	if (!item->mDescription.IsEmpty())
		itemDescription = item->mDescription.c_str();
	WTString txt;
	txt.WTFormat("\f\nreadme:\n%s\n\f\na:%s:%s:\n%s\f\n", itemDescription, item->mTitle.c_str(),
	             item->mShortcut.c_str(), item->mSource.c_str());

	// append to current tpl file
	try
	{
		//		CFileException e;
		CFileW file;
		if (!file.Open(mLocalTemplateFile, CFile::modeCreate | CFile::modeWrite | CFile::modeNoTruncate /*, &e*/))
		{
			_ASSERTE(!"Save file open error");
			return;
		}

		file.SeekToEnd();
		file.Write(txt.c_str(), txt.GetLength() * sizeof(TCHAR));
		file.Close();
	}
	catch (CFileException* e)
	{
		delete e;
	}
}

void AutotextManager::InstallDefaultItems()
{
	bool createdMgr = false;
	if (!gAutotextMgr)
	{
		CreateAutotextManager();
		createdMgr = true;
	}

	gAutotextMgr->CheckForDefaults(Header);
#if !defined(RAD_STUDIO) && !defined(AVR_STUDIO)
	gAutotextMgr->CheckForDefaults(CS);
	gAutotextMgr->CheckForDefaults(VB);
#endif // !RAD_STUDIO && !AVR_STUDIO

	if (createdMgr)
		DestroyAutotextManager();
}

bool AutotextManager::EditUserInputFieldsInEditor(EdCntPtr ed, const WTString& itemSource)
{
	if (gExpSession)
		return false;

	if (!ed)
		return false;

	if (RefactoringActive::IsActive())
		return false;

	if (!gShellAttr->IsDevenv10OrHigher())
		return false;

	if (Is_C_CS_File(gTypingDevLang) || Is_Tag_Based(gTypingDevLang))
	{
		if (gShellAttr->IsDevenv14())
		{
			if (!gShellAttr->IsDevenv14u3OrHigher() && gShellAttr->IsDevenv14u2OrHigher())
			{
				// [case: 95473]
				// [case: 95894]
				return false;
			}
		}

		// [case: 24605]
		if (Psettings->mEditUserInputFieldsInEditor)
		{
			if (IsUserInputSnippet(itemSource))
				return true;
		}
	}

	return false;
}

void AutotextManager::CheckForDefaults(int langType)
{
	AutoLockCs l(sSnippetLoaderLock);
	Load(langType);

	// rename existing default items that have old name (before loading current defaults)
	{
		bool needReload = false;
		WTString headerOld, headerNew;
		headerOld = "\na:#ifdef (VA X):";
		headerNew = "\na:#ifdef (VA):";
		if (RenameItemInFile(mLocalTemplateFile, headerOld, headerNew))
			needReload = true;

		headerOld = "\na:#region (VA X):";
		headerNew = "\na:#region (VA):";
		if (RenameItemInFile(mLocalTemplateFile, headerOld, headerNew))
			needReload = true;

		if (needReload)
			Load(langType);
	}

	const TemplateItem* defaultItem = NULL;
	for (int idx = 0; (defaultItem = ::GetDefaultAutotextItem(langType, idx)) != NULL; ++idx)
	{
		// the call to GetSource is required - populates default snippets
		const WTString src(GetSource(defaultItem->mTitle));

		if (IsCFile(langType))
		{
			if (defaultItem->mTitle == "Refactor Extract Method")
			{
				// the original C++ default for Extract Method had no $MethodQualifier$
				if (-1 == src.Find("$MethodQualifier$"))
				{
					int posClose = src.Find(')');
					if (-1 != posClose)
					{
						int nextPos = src.Find(")", posClose + 1);
						if (-1 == nextPos)
						{
							WTString updatedSource(src.Left(posClose));
							updatedSource += ") $MethodQualifier$";
							updatedSource += src.Mid(posClose + 1);

							WTString header;
							header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
							ReplaceItemSourceInFile(header, mLocalTemplateFile, updatedSource);
						}
					}
				}
			}
			else if (defaultItem->mTitle == "SuggestionsForType HRESULT")
			{
				// [case: 76984]
				if (src == "S_OK\nS_FALSE\n")
				{
					WTString header;
					header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
					ReplaceItemSourceInFile(header, mLocalTemplateFile, defaultItem->mSource);
				}
			}
		}
		else if (CS == langType)
		{
			if (defaultItem->mTitle == "Refactor Create Implementation")
			{
				// the original C# default for Create Implementation was hardcoded to private
				if (-1 == src.Find("$SymbolPrivileges$"))
				{
					WTString updatedSource(src);
					updatedSource.ReplaceAll("private", "$SymbolPrivileges$", FALSE);

					WTString header;
					header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
					ReplaceItemSourceInFile(header, mLocalTemplateFile, updatedSource);
				}
			}
			else if (defaultItem->mTitle == "Refactor Extract Method")
			{
				// the original C# default for Extract Method was hardcoded to private
				if (-1 == src.Find("$SymbolPrivileges$"))
				{
					WTString updatedSource(src);
					updatedSource.ReplaceAll("private", "$SymbolPrivileges$", FALSE);

					WTString header;
					header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
					ReplaceItemSourceInFile(header, mLocalTemplateFile, updatedSource);
				}
			}
		}
		else if (VB == langType)
		{
			if (defaultItem->mTitle == "Refactor Create Implementation")
			{
				// the original VB default for Create Implementation was hardcoded to Private
				if (-1 == src.Find("$SymbolPrivileges$"))
				{
					WTString updatedSource(src);
					updatedSource.ReplaceAll("Private", "$SymbolPrivileges$", FALSE);

					WTString header;
					header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
					ReplaceItemSourceInFile(header, mLocalTemplateFile, updatedSource);
				}
			}
			else if (defaultItem->mTitle == "Refactor Extract Method")
			{
				// the original VB default for Extract Method was hardcoded to private
				if (-1 == src.Find("$SymbolPrivileges$"))
				{
					WTString updatedSource(src);
					updatedSource.ReplaceAll("Private", "$SymbolPrivileges$", FALSE);

					WTString header;
					header.WTFormat("\na:%s:", defaultItem->mTitle.c_str());
					ReplaceItemSourceInFile(header, mLocalTemplateFile, updatedSource);
				}
			}
		}
	}
}

LPCWSTR
AutotextManager::QueryStatusText(DWORD cmdId, DWORD* statusOut)
{
	if (!mDynamicSelectionCmdInfo.size())
	{
		const int kItems = GetCount();
		int dynamicIdx = 0;
		for (int curItemIdx = 0; curItemIdx < kItems; ++curItemIdx)
		{
			const bool usesSelection = DoesItemUseString(curItemIdx, kAutotextKeyword_Selection);
			if (!usesSelection)
				continue;

			WTString menuItemTxt(GetTitle(curItemIdx, true));
			if (menuItemTxt.IsEmpty())
				continue;

			if (menuItemTxt.Find('&') == -1)
				menuItemTxt = '&' + menuItemTxt;

			mDynamicSelectionCmdInfo[dynamicIdx].mAutotextIdx = curItemIdx;
			mDynamicSelectionCmdInfo[dynamicIdx].mMenutext = menuItemTxt.Wide();
			++dynamicIdx;
		}
	}

	*statusOut = 1;
	return mDynamicSelectionCmdInfo[int(cmdId - icmdVaCmd_DynamicSelectionFirst)].mMenutext;
}

HRESULT
AutotextManager::Exec(EdCntPtr ed, DWORD cmdId)
{
	if (!ed)
		return E_UNEXPECTED;

	const DynamicSelectionCmdInfo& cmdInfo = mDynamicSelectionCmdInfo[int(cmdId - icmdVaCmd_DynamicSelectionFirst)];
	if (cmdInfo.mMenutext.IsEmpty())
		return E_UNEXPECTED;

	Insert(ed, cmdInfo.mAutotextIdx);
	return S_OK;
}

void AutotextManager::LoadVate()
{
	if (mVateDll.IsLoaded())
		return;

#if defined(_WIN64)
	CString vateFilename(IDS_VATE2_DLL);
#else
	const CString vate("vate.dll");
	CString vateFilename(vate);
	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 78966]
		vateFilename = IDS_VATE2_DLL;
	}
#endif

	CString fullVatePath(VaDirs::GetDllDir() + CStringW(vateFilename));
	mVateDll.Load(fullVatePath);
	if (!mVateDll.IsLoaded())
	{
		mVateDll.LoadFromVaDirectory(vateFilename);
#if !defined(_WIN64)
		if (!mVateDll.IsLoaded() && vateFilename != vate)
		{
			// revert to original editor if new one failed to load
			vateFilename = vate;
			mVateDll.LoadFromVaDirectory(vateFilename);
		}
#endif
	}
}

WTString GetPotentialShortcutExt(const WTString& potentialShortcut)
{
	// Get shortcut including preluding non-csyms
	WTString potentialShortcutExt;
	if (g_currentEdCnt /*&& Is_Tag_Based(g_currentEdCnt->m_ScopeLangType)*/)
	{
		WTString buf = g_currentEdCnt->GetBuf();
		int ep = g_currentEdCnt->GetBufIndex(buf, (long)g_currentEdCnt->CurPos());
		if (ep > potentialShortcut.GetLength())
		{
			int bp = ep - potentialShortcut.GetLength();
			while (bp && !wt_isspace(buf[bp - 1]))
				bp--;
			if (bp != (ep - potentialShortcut.GetLength()))
				potentialShortcutExt = g_currentEdCnt->GetSubString((ulong)bp, (ulong)ep);
		}
	}
	return potentialShortcutExt;
}

void AutotextManager::PopulateSnippetFile(CStringW templateFileNmae)
{
	WTString ln;
	ifstream codeifs(templateFileNmae, ios::in | ios::binary);
	while (codeifs)
	{
		ln.read_to_delim(codeifs, '\f');
		int tagLineBreakLen = 0;
		int brkPos = ln.Find(":\n");
		if (brkPos == -1)
		{
			brkPos = ln.Find(":\r\n");
			if (brkPos != -1)
				tagLineBreakLen = 2;
		}
		else
			tagLineBreakLen = 1;

		if (!(ln.GetLength() && brkPos != -1 && brkPos))
			continue;

		// eat whitespace before title tag
		int eatCh = 0;
		for (int i = 0; ln[i]; i++)
		{
			if (::wt_isspace(ln[i]))
				eatCh++;
			else
				break;
		}
		const WTString recordHeader = ln.Mid(eatCh, brkPos - eatCh);
		if (recordHeader == "readme")
			continue;

		// source starts after ":\r\n"
		ln = ln.Mid(brkPos + 1 + tagLineBreakLen);
		if (ln.IsEmpty())
			continue;

		// format is a:title:shortcut:\r\n
		if ('a' != recordHeader[0])
			continue;

		const int titleOffset = recordHeader.Find(':', 2);
		if (-1 == titleOffset)
			continue;

		WTString title;
		if (titleOffset != 2)
		{
			title = recordHeader.Mid(2, titleOffset - 2);
			if (!title.CompareNoCase("untitled"))
				title.Empty(); // [case: 23074] don't display Untitled as a listbox item
		}

		if (title.IsEmpty())
		{
			// no title - use flattened source up to kMaxSourceTitleLen chars
			// this algorithm is also used by vate2.dll snippet editor
			const int kMaxSourceTitleLen = 27;
			token flattenedSrc(ln);
			flattenedSrc.ReplaceAll(TRegexp("[\t\r\n]+"), string(" "));
			flattenedSrc.ReplaceAll(TRegexp("[ ]+"), string("\001"));
			flattenedSrc.ReplaceAll(TRegexp("\001"), string(" "));
			flattenedSrc.ReplaceAll(TRegexp("[$]end[$]"), string(" "));
			title = flattenedSrc.c_str();
			if (title.GetLength() > kMaxSourceTitleLen)
			{
				// [case: 132585]
				CStringW titleW(title.Wide());
				titleW = titleW.Left(kMaxSourceTitleLen - 3) + L"...";
				title = titleW;
			}
		}

		EnsureTitleIsUnique(title);

		const WTString shortCut(recordHeader.Right(recordHeader.GetLength() - titleOffset - 1));
		::SimplifyLinebreaks(ln);
		TemplateItem item(title, shortCut, ln);
		if (!shortCut.IsEmpty())
		{
			const char* kSw = "SurroundWith=";
			if (-1 != shortCut.Find(kSw))
			{
				item.mShortcut.Empty();
				StrVectorA shortcuts;
				WtStrSplitA(shortCut, shortcuts, " ");
				for (auto shc : shortcuts)
				{
					shc.Trim();
					int swkPos = shc.Find(kSw);
					if (-1 != swkPos)
					{
						swkPos += strlen_i(kSw);
						if (shc.GetLength() > swkPos)
							item.mSpecialShortcut = shc[swkPos];
					}
					else if (!shc.IsEmpty())
						item.mShortcut = shc;
				}
			}
		}

		mTemplateCollection.Add(item);
	}
}

void BackupPreUtf8AutotextFile(const CStringW& theFile);

static void ReplaceItemSourceInFile(const WTString& itemHeader, const CStringW& theFile, WTString newSource)
{
	WTString fileSource;
	if (!fileSource.ReadFile(theFile))
		return;

	int pos = fileSource.Find(itemHeader);
	if (pos == -1)
		return;

	pos += itemHeader.GetLength() * sizeof(char);
	pos = fileSource.Find("\n", pos);
	if (pos == -1)
		return;

	// [case: 132425]
	if (!HasUtf8Bom(theFile))
		BackupPreUtf8AutotextFile(theFile);

	RecycleFile(theFile);

	CFileW file;
	if (!file.Open(theFile, CFile::modeCreate | CFile::modeWrite /*, &e*/))
	{
		_ASSERTE(!"Save file open error");
		return;
	}

	// [case: 132425]
	file.WriteUtf8Bom();
	if (fileSource[0] != '\f')
	{
		// write out empty initial snippet for backwards compatibility
		char fileHeader[2] = {'\f', '\n'};
		file.Write(fileHeader, 2);
	}

	// write out from start of file to 'pos' as is (inclusive)
	file.Write(fileSource.c_str(), (pos + 1) * sizeof(char));

	// append newSource
	newSource.ReplaceAll("\n", "\r\n");
	file.Write(newSource.c_str(), newSource.GetLength() * sizeof(char));

	// advance to /f at end of original item source (skip item source)
	pos = fileSource.Find("\f", pos);
	fileSource = fileSource.Mid(pos);

	// write out from end of original item source to EOF
	file.Write(fileSource.c_str(), fileSource.GetLength() * sizeof(char));

	file.Close();
}

bool RenameItemInFile(const CStringW& theFile, const WTString& headerOld, const WTString& headerNew)
{
	WTString fileSource;
	if (!fileSource.ReadFile(theFile))
		return false;

	if (-1 != fileSource.Find(headerNew))
		return false;

	if (!fileSource.ReplaceAll(headerOld, headerNew))
		return false;

	// [case: 132425]
	if (!HasUtf8Bom(theFile))
		BackupPreUtf8AutotextFile(theFile);

	RecycleFile(theFile);

	CFileW file;
	if (!file.Open(theFile, CFile::modeCreate | CFile::modeWrite /*, &e*/))
	{
		_ASSERTE(!"Save file open error");
		return false;
	}

	// [case: 132425]
	file.WriteUtf8Bom();
	if (fileSource[0] != '\f')
	{
		// write out empty initial snippet for backwards compatibility
		char fileHeader[2] = {'\f', '\n'};
		file.Write(fileHeader, 2);
	}

	file.Write(fileSource.c_str(), fileSource.GetLength() * sizeof(char));
	file.Close();
	return true;
}

static CStringW GetLocalTemplateDir()
{
	if (gTestsActive)
	{
		// [case: 47141] load test snippets instead of user snippets while testing
		_ASSERTE(GlobalProject);
		if (gTestSnippetSubDir.GetLength())
		{
			// AST: <SnippetDir:subdir>
			CStringW dir = gTestSnippetSubDir + L"\\";
			if (IsDir(dir))
				return dir;
			_ASSERTE(!"gTestSnippetSubDir");
		}

		CStringW dir(GlobalProject->SolutionFile());
		if (!dir.IsEmpty())
		{
			dir = ::Path(dir);
			if (!dir.IsEmpty())
			{
				dir += L"\\VaSnippets\\";
				if (IsDir(dir))
				{
					if (IsFile(dir + L"cpp.tpl"))
						return dir;
				}
			}
		}

		WtMessageBox(NULL, "Tests are dependent upon VaSnippets sub-dir in root solution dir.", IDS_APPNAME,
		             MB_OK | MB_ICONERROR);
	}

	return VaDirs::GetUserDir() + L"Autotext\\";
}

CStringW GetSharedTemplateDir()
{
	if (gTestsActive)
		return CStringW(); // no shared snippets during AST yet...

	CStringW sharedTemplateDir = GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_SHAREDSNIPPETSDIR);
	if (sharedTemplateDir.GetLength())
	{
		const int dirLen = sharedTemplateDir.GetLength();
		if (sharedTemplateDir[dirLen - 1] != L'\\' && sharedTemplateDir[dirLen - 1] != L'/')
			sharedTemplateDir += L"\\";
	}
	return sharedTemplateDir;
}

static CStringW DoGetTemplateFilename(int type, CStringW baseDir)
{
	if (baseDir.IsEmpty() || !IsDir(baseDir))
		return CStringW();

	switch (type)
	{
	case UC:
		return baseDir + L"uc.tpl";
	case Src:
	case Header:
	case Idl:
	case Tmp:
		return baseDir + L"cpp.tpl";
	case PERL:
		return baseDir + L"perl.tpl";
	case JS:
		return baseDir + L"js.tpl";
	case VB:
		return baseDir + L"vb.tpl";
	case VBS:
		return baseDir + L"vbs.tpl";
	case CS:
		return baseDir + L"cs.tpl";
	case XML:
		return baseDir + L"xml.tpl";
	case XAML:
		return baseDir + L"xaml.tpl";
	case HTML:
	case ASP:
		return baseDir + L"html.tpl";
	case Binary:
	case Image:
		return L"";
	case Plain:
	case Other:
	case RC:
	case SQL:
	default:
		return baseDir + L"txt.tpl";
	}
}

static CStringW GetLocalTemplateFilename(int type)
{
	return DoGetTemplateFilename(type, GetLocalTemplateDir());
}

static CStringW GetSharedTemplateFilename(int type)
{
	return DoGetTemplateFilename(type, GetSharedTemplateDir());
}

const int kMaxParams = 8;

void BuildSubstitutionList(const token& partiallySubstitutedTemplateIn, const WCHAR* const regExp,
                           WideStrVector& substList)
{
	const std::wstring outerTxt((LPCWSTR)WTString(partiallySubstitutedTemplateIn.c_str()).Wide());

	LPCWSTR outerStartPos = outerTxt.c_str();
	LPCWSTR outerEndPos = outerStartPos + outerTxt.length();

	std::wregex outerRegex(regExp, std::wregex::ECMAScript | std::wregex::optimize | std::wregex::icase);
	std::wcmatch outerMatch;

	while (outerStartPos < outerEndPos)
	{
		if (!std::regex_search(outerStartPos, outerEndPos, outerMatch, outerRegex))
			break;
		else
		{
			if (outerMatch.empty())
				break;

			std::wstring found = outerMatch[0].str();
			if (found.empty())
				break;

			outerStartPos = outerMatch[0].second;

			WTString strToAdd(found.c_str());

			if (strToAdd == kAutotextKeyword_Selection || strToAdd == kAutotextKeyword_End ||
			    strToAdd == kAutotextKeyword_Clipboard)
			{
				continue;
			}

			// strip the $s
			strToAdd = strToAdd.Mid(1, strToAdd.GetLength() - 2);

			int pos = -1;
			if (-1 == pos && strToAdd.EndsWith("_Upper"))
				pos = strToAdd.GetLength() - 6; // [case: 2765]
			if (-1 == pos && strToAdd.EndsWith("_Lower"))
				pos = strToAdd.GetLength() - 6; // [case: 2765]
			if (-1 == pos && strToAdd.EndsWith("_Pascal"))
				pos = strToAdd.GetLength() - 7; // [case: 66101]
			if (-1 == pos && strToAdd.EndsWith("_Camel"))
				pos = strToAdd.GetLength() - 6; // [case: 66101]

			CStringW strToAddW;
			if (-1 != pos)
				strToAddW = strToAdd.Left(pos).Wide();
			else
				strToAddW = strToAdd.Wide();

			bool add = true;
			for (const auto& subst : substList)
			{
				if (!subst.Compare(strToAddW))
				{
					add = false;
					break;
				}
			}
			if (!add)
				continue;

			if (kMaxParams == substList.size())
				break;

			substList.push_back(strToAddW);
		}
	}
}

const wchar_t* kUserLabelsWithDefaultRegex = LR"(\$[\w_]+=[\w_ =^*&'"\\:;()#%-/<>!@]+\$)";
const wchar_t* kUserLabelsNoDefaultRegex = LR"(\$[\w_]+\$)";

int AutotextManager::MakeUserSubstitutions(token& partiallySubstitutedTemplate, const WTString& templateTitle,
                                           EdCnt* parentWnd) const
{
	if (partiallySubstitutedTemplate.Str().Find("$") == -1 || !gAutotextMgr)
		return IDOK;

	uint idx;
	// first build list of user keywords of the form $foo=bar$
	WideStrVector userLabelsWithDefaults;
	// regexp for def value will allow file paths, single-quotes, double-quotes, spaces, etc but NOT $
	// don't add $ because it will break lines like this: $foo=bar$ $baz$
	BuildSubstitutionList(partiallySubstitutedTemplate, kUserLabelsWithDefaultRegex, userLabelsWithDefaults);

	// copy from userLabelsWithDefaults to userLabels removing the actual defaults
	WideStrVector userLabels;
	for (idx = 0; idx < userLabelsWithDefaults.size(); ++idx)
	{
		_ASSERTE(idx < kMaxParams);
		WTString newLabel(userLabelsWithDefaults[idx]);
		const int eqPos = newLabel.Find('=');
		_ASSERTE(eqPos != -1);
		newLabel = newLabel.Left(eqPos);

		// confirm not already in the list - don't allow multiple defaults for the same item
		for (uint idx2 = 0; idx2 < userLabels.size(); ++idx2)
		{
			const WTString curLabel(userLabels[idx2]);
			if (curLabel == newLabel)
			{
				WTString msg;
				msg.WTFormat("The '%s' VA Snippet source contains multiple initializations for '%s'.\n"
				             "An item may be initialized only once.\n"
				             "Please correct the VA Snippet source.\n",
				             templateTitle.c_str(), newLabel.c_str());
				if (gTestLogger)
				{
					WTString msg2;
					msg2.WTFormat("MsgBox: %s\r\n", msg.c_str());
					gTestLogger->LogStr(msg2);
				}
				WtMessageBox(parentWnd->GetSafeHwnd(), msg, IDS_APPNAME, MB_ICONERROR | MB_OK);
				return IDCANCEL;
			}
		}

		userLabels.push_back(newLabel.Wide());
		if (kMaxParams == userLabels.size())
			break;
	}

	// build list of user keywords of the form $foo$
	BuildSubstitutionList(partiallySubstitutedTemplate, kUserLabelsNoDefaultRegex, userLabels);
	// restore escaped '='
	partiallySubstitutedTemplate.ReplaceAll("\006", "=");

	if (!userLabels.size())
		return IDOK;

	int retval = IDOK;
	CStringW pStW(::DecodeUserText(WTString(partiallySubstitutedTemplate.c_str())).Wide());
	pStW.Replace(L'\005', L'$');

	CStringW title(templateTitle.GetLength() ? templateTitle.Wide() : CStringW(L"Make VA Snippet Substitutions"));
	if (0 == title.Find(L"Refactor "))
		title = title.Mid(9);

	AutotextDlgParams params;
	params.hWndParent = gShellAttr->IsDevenv10OrHigher() ? gMainWnd->m_hWnd : parentWnd->m_hWnd;
	params.sExpandedCode = pStW;
	params.sTitleOfAutotext = title;
	params.mCurrentSnippetLanguage = ::GetVateLang(gTypingDevLang);
	const CStringW kSettingsRegPath(ID_RK_APP + "\\VATE\\Autotext Dialog");
	params.mSettingsRegPath = kSettingsRegPath;
	params.mReservedStrings = gReservedStrings;
	const WCHAR** userLabelInitHelp[kMaxParams] = {&params.sArgLabel0, &params.sArgLabel1, &params.sArgLabel2,
	                                               &params.sArgLabel3, &params.sArgLabel4, &params.sArgLabel5,
	                                               &params.sArgLabel6, &params.sArgLabel7};

	GlobalStringW* userValuesInitHelp[kMaxParams] = {&params.sArgValue0, &params.sArgValue1, &params.sArgValue2,
	                                                 &params.sArgValue3, &params.sArgValue4, &params.sArgValue5,
	                                                 &params.sArgValue6, &params.sArgValue7};

	// set labels
	WideStrVector userLabelsW;
	for (idx = 0; idx < userLabels.size(); ++idx)
	{
		_ASSERTE(idx < kMaxParams);
		userLabelsW.push_back(userLabels[idx]);
		*userLabelInitHelp[idx] = userLabelsW[idx];
	}

	// set default values
	for (idx = 0; idx < userLabelsWithDefaults.size(); ++idx)
	{
		_ASSERTE(idx < kMaxParams);
		CStringW tmp(userLabelsWithDefaults[idx]);
		const int eqPos = tmp.Find(L'=');
		_ASSERTE(eqPos != -1);
		tmp = tmp.Mid(eqPos + 1);
		tmp.Replace(L"$$", L"$");
		*userValuesInitHelp[idx] = tmp;
	}

	// get clipboard history
	const int kMaxClipboardItems = 8;
	// ensure strings that the paramStruct points to will be valid when
	// the dialog is run by using CStrings out here.
	CStringW pasteItems[kMaxClipboardItems];
	const WCHAR** pasteHistoryInitHelp[kMaxClipboardItems] = {
	    &params.sClipboard0, &params.sClipboard1, &params.sClipboard2, &params.sClipboard3,
	    &params.sClipboard4, &params.sClipboard5, &params.sClipboard6, &params.sClipboard7};
	for (idx = 0; idx < kMaxClipboardItems; ++idx)
	{
		pasteItems[idx] = g_VATabTree->GetClipboardItem((int)idx);
		// don't pass in multiline clip items since the prompt doesn't allow them
		int pos = pasteItems[idx].FindOneOf(L"\r\n");
		if (-1 != pos)
			pasteItems[idx] = pasteItems[idx].Left(pos);
		*pasteHistoryInitHelp[idx] = pasteItems[idx];
	}

	// fire up VATE dialog
	try
	{
		gAutotextMgr->LoadVate();
		if (gAutotextMgr->mVateDll.IsLoaded())
		{
			ShowAutotextDlgWFn pfnShowAutotextDlg;
			gAutotextMgr->mVateDll.GetFunction("ShowAutotextDlgW", pfnShowAutotextDlg);
			if (pfnShowAutotextDlg)
			{
				{
					VsUI::CDpiScope dpi(true);
					retval = (int)pfnShowAutotextDlg(&params);
				}

				if (!gShellAttr->IsDevenv10OrHigher())
				{
					// these are necessary due to weird problem after dlg display
					::SetFocus(::GetParent(::GetParent(parentWnd->m_hWnd)));
				}
				parentWnd->vSetFocus();
			}
			else
			{
				CString errMsg("Missing function export in VA Snippet editor dll");
				WtMessageBox(parentWnd->GetSafeHwnd(), errMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
			}
		}
		else
		{
			CString errMsg("Unable to load VA Snippet prompt");
			WtMessageBox(parentWnd->GetSafeHwnd(), errMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("ATM:");
		ASSERT(!"user template substitution exception");
	}

	if (IDOK != retval)
		return retval;

	// now make the actual substitutions
	const struct SubstitutionPair
	{
		const WCHAR* mLabel;
		const GlobalStringW* mSubstitute;
	} subPairs[kMaxParams] = {// retain order given in userLabelsWithDefaults and userLabels
	                          {params.sArgLabel0, &params.sArgValue0}, {params.sArgLabel1, &params.sArgValue1},
	                          {params.sArgLabel2, &params.sArgValue2}, {params.sArgLabel3, &params.sArgValue3},
	                          {params.sArgLabel4, &params.sArgValue4}, {params.sArgLabel5, &params.sArgValue5},
	                          {params.sArgLabel6, &params.sArgValue6}, {params.sArgLabel7, &params.sArgValue7}};

	for (idx = 0; idx < kMaxParams; idx++)
	{
		_ASSERTE(subPairs[idx].mSubstitute);

		if (!subPairs[idx].mLabel || !subPairs[idx].mSubstitute)
			continue;

		const CStringW userDefinedSubValue =
		    subPairs[idx].mSubstitute->length() ? subPairs[idx].mSubstitute->c_str() : L"";
		const CStringW origBaseStrToSearchFor(subPairs[idx].mLabel);

		for (int suffixCnt = 0; suffixCnt < 5; ++suffixCnt)
		{
			CStringW baseStrToSearchFor;
			CStringW subW(userDefinedSubValue);

			switch (suffixCnt)
			{
			case 0:
				baseStrToSearchFor = origBaseStrToSearchFor;
				break;
			case 1:
				baseStrToSearchFor = origBaseStrToSearchFor + L"_Lower";
				if (!subW.IsEmpty())
					UnicodeHelper::MakeLower(subW);
				break;
			case 2:
				baseStrToSearchFor = origBaseStrToSearchFor + L"_Upper";
				if (!subW.IsEmpty())
					UnicodeHelper::MakeUpper(subW);
				break;
			case 3:
				baseStrToSearchFor = origBaseStrToSearchFor + L"_Pascal";
				if (!subW.IsEmpty())
					UnicodeHelper::MakePascal(subW);
				break;
			case 4:
				baseStrToSearchFor = origBaseStrToSearchFor + L"_Camel";
				if (!subW.IsEmpty())
					UnicodeHelper::MakeCamel(subW);
				break;
			default:
				break;
			}

			CStringW label(L"$" + baseStrToSearchFor + L"$");

			// ReplaceAll goes into a infinite loop if the new text
			// contains the oldText.
			if (-1 == subW.Find(label))
			{
				// here we replace: $foo$ with the user prompted value
				CStringW reLabel(L"\\$" + baseStrToSearchFor + L"\\$");
				partiallySubstitutedTemplate.ReplaceAllRegex(reLabel, subW);
			}

			// the first items in the params result were ones with default values
			// and match the current item without the default. For example, if
			// userLabelsWithDefaults is size 2 and userLabels is size 3, then:
			// idx = 0: $foo$ and $foo=bar$		- both get same user substitution
			// idx = 1: $baz$ and $baz=bah$		- both get same user substitution
			// idx = 2: $foobar$ (no default provided match)
			if (idx < userLabelsWithDefaults.size())
			{
				baseStrToSearchFor = userLabelsWithDefaults[idx];
				_ASSERTE(baseStrToSearchFor.GetLength());

				// ReplaceAll goes into a infinite loop if the new text
				// contains the oldText.
				label = L"$" + baseStrToSearchFor + L"$";
				if (-1 == subW.Find(label))
				{
					// here we replace: $foo=bar$ with the user prompted value

					// handle reg exp special chars in default value
					baseStrToSearchFor.Replace(L"\\", L"\\\\");
					baseStrToSearchFor.Replace(L"*", L"[*]");
					baseStrToSearchFor.Replace(L"+", L"[+]");
					baseStrToSearchFor.Replace(L".", L"[.]");
					baseStrToSearchFor.Replace(L"$", L"[$]");
					baseStrToSearchFor.Replace(L"^", L"[^]");
					baseStrToSearchFor.Replace(L"(", L"[(]");
					baseStrToSearchFor.Replace(L")", L"[)]");

					const CStringW reLabel(L"\\$" + baseStrToSearchFor + L"\\$");
					partiallySubstitutedTemplate.ReplaceAllRegex(reLabel, subW, false, true, false);
				}
			}
		}
	}

	return retval;
}

static bool TrimEmptyLine(WTString& text)
{
	bool didTrim = false;
	const int kTxtLen = text.GetLength();
	if (kTxtLen > 1)
	{
		const char lastChar = text[kTxtLen - 1];
		const char prevChar = text[kTxtLen - 2];
		if ((lastChar == '\r' && prevChar == '\n') || (lastChar == '\n' && prevChar == '\r'))
		{
			text = text.Left(text.GetLength() - 2);
			didTrim = true;
		}
		else if (lastChar == '\n' || lastChar == '\r')
		{
			text = text.Left(text.GetLength() - 1);
			didTrim = true;
		}
	}
	return didTrim;
}

static void SimplifyLinebreaks(WTString& str)
{
	// handling \r\n and \r in separate steps means that a string with
	// mixed line-endings will be better handled when converted to \n only
	str.ReplaceAll("\r\n", "\n");
	str.ReplaceAll("\r", "\n");
}

// static WTString
// SimplifyLinebreaks(LPCTSTR str)
//{
//	WTString newstr(str);
//	SimplifyLinebreaks(newstr);
//	return newstr;
//}

// hide $$ escapes
WTString HideVaSnippetEscapes(const WTString& str)
{
	if (str.Find("$$") == -1)
	{
		if (str.Find('=') == -1 || str.Find('$') == -1)
			return str;
	}

	WTString escaped;
	int in$ = 0;
	char prevCh = '\0';
	for (int i = 0; i < str.GetLength(); ++i)
	{
		char curCh = str[i];
		if (in$ && ('\n' == curCh || '\r' == curCh))
			in$ = 0; // reset on line break

		if ('=' == curCh && !in$)
			curCh = '\006';
		else if ('$' != curCh)
			;
		else if (in$ && prevCh != '=')
		{
			_ASSERTE(curCh == '$');
			--in$;
		}
		else
		{
			_ASSERTE(curCh == '$');
			// check next char
			if (str[i + 1] == '$')
			{
				curCh = '\005';
				++i;
			}
			else
				++in$;
		}

		escaped += curCh;
		prevCh = curCh;
	}

	return escaped;
}

bool IsUserVisibleSnippet(int langType, const WTString& title)
{
	if (title.contains("SuggestionsForType "))
		return false;

	return !IsDefaultAutotextItemTitle(langType, title);
}

WTString EncodeUserText(WTString txt)
{
	txt.ReplaceAll("%", "@@VAX_Escape1");
	txt.ReplaceAll("$", "@@VAX_Escape2");
	return txt;
}

WTString DecodeUserText(WTString txt)
{
	txt.ReplaceAll("@@VAX_Escape1", "%");
	txt.ReplaceAll("@@VAX_Escape2", "$");
	return txt;
}

WTString GetBodyAutotextItemTitle(BOOL isNetSym, bool inheritsFromUeClass)
{
	const WTString kDefaultAutotextItemTitle("Refactor Create From Usage Method Body");
	WTString autotextItemTitle(kDefaultAutotextItemTitle);
	if (IsCFile(gTypingDevLang))
	{
		const CStringW kActiveFile(g_currentEdCnt ? g_currentEdCnt->FileName() : CStringW(L""));
		if (isNetSym)
		{
			bool isWinrtProj = false;
			const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(kActiveFile));
			for (ProjectVec::const_iterator it1 = projForActiveFile.begin(); it1 != projForActiveFile.end(); ++it1)
			{
				_ASSERTE(*it1);
				if ((*it1)->CppUsesWinRT())
				{
					isWinrtProj = true;
					break;
				}
			}

			if (isWinrtProj)
				autotextItemTitle += " (Platform)";
			else
				autotextItemTitle += " (Managed)";
		}
		else if (IsCfile(kActiveFile))
		{
			autotextItemTitle += " (C)";
		}
		else if (inheritsFromUeClass)
		{
			// [case: 116702] [ue4] call "Super::MethodName" when implementing virtual methods from engine base classes
			autotextItemTitle += " (Unreal Engine Virtual Method)";
		}
		// 		else
		// 		{
		// 			autotextItemTitle += " (C++)";
		// 		}
	}

	const WTString bodyCode(gAutotextMgr->GetSource(autotextItemTitle));
	if (bodyCode.IsEmpty() && autotextItemTitle != kDefaultAutotextItemTitle)
		autotextItemTitle = kDefaultAutotextItemTitle;
	return autotextItemTitle;
}

WTString GetClassAutotextItemTitle()
{
	const WTString kDefaultAutotextItemTitle("Refactor Create From Usage Class");
	WTString autotextItemTitle(kDefaultAutotextItemTitle);
	if (IsCFile(gTypingDevLang))
	{
		const CStringW kActiveFile(g_currentEdCnt ? g_currentEdCnt->FileName() : CStringW(L""));
		if (IsCfile(kActiveFile))
		{
			autotextItemTitle += " (C)";
		}
		else
		{
			bool isWinrtProj = false;
			bool isManagedProj = false;
			const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(kActiveFile));
			for (ProjectVec::const_iterator it1 = projForActiveFile.begin(); it1 != projForActiveFile.end(); ++it1)
			{
				_ASSERTE(*it1);
				if ((*it1)->CppUsesWinRT())
				{
					isWinrtProj = true;
					break;
				}

				if ((*it1)->CppUsesClr())
				{
					isManagedProj = true;
					break;
				}
			}

			if (isWinrtProj)
				autotextItemTitle += " (Platform)";
			else if (isManagedProj)
				autotextItemTitle += " (Managed)";
		}
	}

	const WTString bodyCode(gAutotextMgr->GetSource(autotextItemTitle));
	if (bodyCode.IsEmpty() && autotextItemTitle != kDefaultAutotextItemTitle)
		autotextItemTitle = kDefaultAutotextItemTitle;
	return autotextItemTitle;
}

VateLanguage GetVateLang(int type)
{
	VateLanguage Lang = vlEND;
	if (IsCFile(type))
		Lang = vlCPP;
	else if (type == CS)
		Lang = vlCS;
	else if (type == UC)
		Lang = vlUC;
	else if (type == HTML || type == ASP)
		Lang = vlHTML;
	else if (type == Java)
		Lang = vlJAVA;
	else if (type == JS)
		Lang = vlJS;
	else if (type == PERL)
		Lang = vlPERL;
	else if (type == Plain || type == Other)
		Lang = vlTEXT;
	else if (type == VB)
		Lang = vlVB;
	else if (type == VBS)
		Lang = vlVBSCRIPT;
	else if (type == XML)
		Lang = vlXML;
	else if (type == XAML)
		Lang = vlXAML;
	return Lang;
}

bool NormalSnippetExpansionPerAST()
{
	return gTestLogger != nullptr && gTestLogger->IsNormalSnippetExpansionEnabled();
}

void AutotextFmt::ReplaceAll(WTString& buffer) const
{
	if (!SearchRegex.IsEmpty())
	{
		if (MatchSolverW)
		{
			CStringW wrgx = SearchRegex.Wide();
			buffer.ReplaceAllRE(wrgx, false, [this](int pos, CStringW& match_result) {
				LPCWSTR solverRslt = MatchSolverW(match_result);
				if (solverRslt)
					match_result = solverRslt;
				return solverRslt != nullptr;
			});
		}
		else
		{
			buffer.ReplaceAllRE(SearchRegex.Wide(), false, [this](int pos, CStringW& match_result) {
				if (MatchSolver)
				{
					WTString tmp(match_result);
					WTString solverRslt = MatchSolver(tmp.c_str());
					if (!solverRslt.IsEmpty())
						match_result = solverRslt.Wide();
					return solverRslt != nullptr;
				}

				const Modifier& mod = GetModifier(WTString(match_result).c_str());

				if (mod)
				{
					WTString inputStr = GetModifierInputString();
					mod(inputStr);
					match_result = inputStr.Wide();
				}

				return true;
			});
		}
	}
}

void AutotextFmt::ReplaceAllWide(CStringW& buffer) const
{
	WTString wstr = buffer;
	ReplaceAll(wstr);
	buffer = wstr.Wide();
}

bool AutotextFmt::IsWithinCSTR(LPCSTR buffer, bool use_stl /*= true*/) const
{
	if (!SearchRegex.IsEmpty())
	{
		try
		{
			if (!use_stl)
			{
				TRegexp rgx = SearchRegex.c_str();
				size_t len = 0;
				return (int)rgx.find(buffer, &len) >= 0;
			}
			else
			{
				std::regex rgx(SearchRegex.c_str());
				return regex_search(buffer, rgx);
			}
		}
		catch (const std::exception& e)
		{
			//			LPCSTR what = e.what();
			(void)e;
			_ASSERTE(!"Exception in AutotextFormatter::IsWithinCSTR");
		}
		catch (...)
		{
			_ASSERTE(!"Exception in AutotextFormatter::IsWithinCSTR");
		}
	}

	return false;
}

bool AutotextFmt::IsWithinWide(const CStringW& buffer) const
{
	if (!SearchRegex.IsEmpty())
	{
		try
		{
			CStringW wstrRgx = SearchRegex.Wide();
			std::wregex rgx((LPCWSTR)wstrRgx);
			return regex_search((LPCWSTR)buffer, rgx);
		}
		catch (const std::exception& e)
		{
			//			LPCSTR what = e.what();
			(void)e;
			_ASSERTE(!"Exception in AutotextFormatter::IsWithinCSTR");
		}
		catch (...)
		{
			_ASSERTE(!"Exception in AutotextFormatter::IsWithinCSTR");
		}
	}

	return false;
}

bool AutotextFmt::IsWithin(const token& buffer) const
{
	return IsWithinCSTR(buffer.c_str(), false);
}

bool AutotextFmt::IsWithin(const WTString& buffer) const
{
	return IsWithinCSTR(buffer.c_str(), true);
}

void AutotextFmt::ReplaceAll(token& buffer) const
{
	if (!SearchRegex.IsEmpty())
	{
		TRegexp rgx = SearchRegex.c_str();
		buffer.ReplaceAll(rgx, (void*)this, [](void* user_data, const string& match, string& result) {
			const AutotextFmt* fmt = (const AutotextFmt*)user_data;

			if (fmt->MatchSolver)
			{
				LPCSTR solverRslt = fmt->MatchSolver(match.c_str());
				result = solverRslt ? solverRslt : match;
				return solverRslt != nullptr;
			}

			const Modifier& mod = fmt->GetModifier(match.c_str());

			if (mod)
			{
				WTString replacement = fmt->GetModifierInputString();
				mod(replacement);
				result = replacement.c_str();
			}
			else
			{
				result = match;
			}

			return true;
		});
	}
}

const AutotextFmt::Modifier& AutotextFmt::GetModifier(LPCSTR match) const
{
	try
	{
		for (auto m : Modifiers)
			if (m && !m->Rgx.IsEmpty() && std::regex_match(match, std::regex(m->Rgx.c_str())))
				return *m;
	}
	catch (const std::exception& e)
	{
		//		LPCSTR what = e.what();
		(void)e;
		_ASSERTE(!"Exception in AutotextFormatter::GetModifier");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in AutotextFormatter::GetModifier");
	}

	return DefaultModifier;
}

const WTString& AutotextFmt::GetModifierInputString() const
{
	if (Replacement.IsEmpty() && GetReplacement)
		GetReplacement(Replacement);
	return Replacement;
}

void AutotextFmt::AddModifiersByKeyword(LPCSTR casing_str, LPCSTR name /*= NULL*/,
                                        Modifier::FmtFnc fnc /*= Modifier::FmtFnc()*/)
{
	_ASSERTE(!Keyword.IsEmpty());

	Casing kwCasing = DetectCasing(Keyword.c_str());

	_ASSERTE(kwCasing > Default);

	WTString lowerName(name ? name : "");
	lowerName.MakeLower();
	WTString nameCased = ApplyCasing(kwCasing, std::move(lowerName));
	AddModifiers(kwCasing, ParseCasingStr(casing_str), nameCased.c_str(), fnc);
}

void AutotextFmt::AddModifiersByKeyword(Casing casing_flags /*= All*/, LPCSTR name /*= NULL*/,
                                        Modifier::FmtFnc fnc /*= Modifier::FmtFnc()*/)
{
	_ASSERTE(!Keyword.IsEmpty());

	Casing kwCasing = DetectCasing(Keyword.c_str());

	_ASSERTE(kwCasing > Default);

	WTString lowerName(name ? name : "");
	lowerName.MakeLower();
	WTString nameCased = ApplyCasing(kwCasing, std::move(lowerName));
	AddModifiers(kwCasing, casing_flags, nameCased.c_str(), fnc);
}
void AutotextFmt::AddModifiers(CHAR modifier_casing_char, LPCSTR casing_str, LPCSTR name /*= NULL*/,
                               Modifier::FmtFnc fnc /*= Modifier::FmtFnc()*/)
{
	AddModifiers(ParseCasingChar(modifier_casing_char), ParseCasingStr(casing_str), name, fnc);
}

void AutotextFmt::AddModifiers(Casing modifier_casing, DWORD casing_flags, LPCSTR name /*= NULL*/,
                               Modifier::FmtFnc fnc /*= Modifier::FmtFnc()*/)
{
	AddModifiers(modifier_casing, (Casing)casing_flags, name, fnc);
}

void AutotextFmt::AddModifiers(Casing modifier_casing, Casing casing_flags, LPCSTR name /*= NULL*/,
                               Modifier::FmtFnc fnc /*= Modifier::FmtFnc()*/)
{
	CStringW base = name ? name : "";

	if ((casing_flags & Default) == Default)
		AddModifier(base, [fnc](WTString& file) {
			if (fnc)
				fnc(file);
		});

	if ((casing_flags & UPPERCASE) == UPPERCASE)
		AddModifier(base + ApplyCasing(modifier_casing, CStringW(L"_upper")), [fnc](WTString& file) {
			if (fnc)
				fnc(file);
			file = MakeUpperWT(std::move(file));
		});

	if ((casing_flags & lowercase) == lowercase)
		AddModifier(base + ApplyCasing(modifier_casing, CStringW(L"_lower")), [fnc](WTString& file) {
			if (fnc)
				fnc(file);
			file = MakeLowerWT(std::move(file));
		});

	if ((casing_flags & camelCase) == camelCase)
		AddModifier(base + ApplyCasing(modifier_casing, CStringW(L"_camel")), [fnc](WTString& file) {
			if (fnc)
				fnc(file);
			file = MakeCamelWT(std::move(file));
		});

	if ((casing_flags & PascalCase) == PascalCase)
		AddModifier(base + ApplyCasing(modifier_casing, CStringW(L"_pascal")), [fnc](WTString& file) {
			if (fnc)
				fnc(file);
			file = MakePascalWT(std::move(file));
		});
}

void AutotextFmt::AddModifier(const WTString& modif, Modifier::FmtFnc fnc)
{
	Modifiers.push_back(Modifier::Ptr(new Modifier(Keyword, modif, fnc)));
}

void AutotextFmt::AddModifierRgx(LPCSTR rgx, Modifier::FmtFnc fnc)
{
	Modifiers.push_back(Modifier::Ptr(new Modifier(rgx, fnc)));
}

void AutotextFmt::SetBaseKeyword(LPCSTR keyword, bool apply_casings_to_modifiers /*= true*/)
{
	Keyword = keyword;

	// this should use "\\w" instead of "a-zA-Z0-9" but "\\w" is not compatible with TRegExp.
	// "a-zA-Z0-9" should be fine since all of our keywords/modifiers are ascii-based.
	SearchRegex = (WTString("[$]") + keyword) + "[a-zA-Z0-9_]*[$]";

	for (auto& mod : Modifiers)
		if (mod && mod->IsKeyword())
			mod->SetKeyword(Keyword, apply_casings_to_modifiers);
}

AutotextFmt::~AutotextFmt()
{
}

AutotextFmt::AutotextFmt()
{
}

WTString AutotextFmt::ApplyCasing(Casing c, WTString&& str)
{

	switch (c)
	{
	case UPPERCASE:
		return MakeUpperWT(std::move(str));
		break;
	case lowercase:
		return MakeLowerWT(std::move(str));
		break;
	case camelCase:
		return MakeCamelWT(std::move(str));
	case PascalCase:
		return MakePascalWT(std::move(str));
	default:
		return str;
	}
}

CStringW AutotextFmt::ApplyCasing(Casing c, CStringW&& str)
{
	switch (c)
	{
	case UPPERCASE:
		return MakeUpper(std::move(str));
	case lowercase:
		return MakeLower(std::move(str));
	case camelCase:
		return MakeCamel(std::move(str));
	case PascalCase:
		return MakePascal(std::move(str));
	default:
		return str;
	}
}

AutotextFmt::Casing AutotextFmt::DetectCasing(LPCSTR text)
{
	//	DWORD mask = None;
	CHAR ch_prev = 0;

	int num_upper_on_start = 0;
	int num_lower_on_start = 0;
	int num_upper = 0;
	int num_lower = 0;
	int num_alpha = 0;

	for (LPCSTR c = text; c && *c; c++)
	{
		CHAR ch = *c;

		if (isalnum(ch))
		{
			bool is_upper = isupper(ch) != 0;

			if (isalpha(ch))
			{
				if (!isalnum(ch_prev))
				{
					if (is_upper)
						num_upper_on_start++;
					else
						num_lower_on_start++;
				}

				if (is_upper)
					num_upper++;
				else
					num_lower++;

				num_alpha++;
			}
		}

		ch_prev = ch;
	}

	if (num_alpha == 0)
		return None;
	else if (num_lower == num_alpha)
		return lowercase;
	else if (num_upper == num_alpha)
		return UPPERCASE;
	else if (num_upper_on_start > num_lower_on_start)
		return PascalCase;
	else
		return camelCase;
}

AutotextFmt::Casing AutotextFmt::ParseCasingStr(LPCSTR casingStr /*= "DULCP"*/)
{
	DWORD mask = None;

	for (LPCSTR c = casingStr; c && *c; c++)
		mask |= ParseCasingChar(*c);

	return (Casing)mask;
}

AutotextFmt::Casing AutotextFmt::ParseCasingChar(CHAR ch)
{
	if (ch == 'D' || ch == 'd')
		return Default;
	else if (ch == 'U' || ch == 'u')
		return UPPERCASE;
	else if (ch == 'L' || ch == 'l')
		return lowercase;
	else if (ch == 'C' || ch == 'c')
		return camelCase;
	else if (ch == 'P' || ch == 'p')
		return PascalCase;
	else
		return None;
}

CStringW AutotextFmt::MakeUpper(CStringW&& str)
{
	UnicodeHelper::MakeUpper(str);
	return str;
}

CStringW AutotextFmt::MakeLower(CStringW&& str)
{
	UnicodeHelper::MakeLower(str);
	return str;
}

WTString AutotextFmt::MakeUpperWT(WTString&& str)
{
	str = MakeUpper(str.Wide());
	return str;
}

WTString AutotextFmt::MakeLowerWT(WTString&& str)
{
	str = MakeLower(str.Wide());
	return str;
}

CStringW AutotextFmt::MakePascal(CStringW&& str)
{
	UnicodeHelper::MakePascal(str);
	return str;
}

CStringW AutotextFmt::MakeCamel(CStringW&& str)
{
	UnicodeHelper::MakeCamel(str);
	return str;
}

WTString AutotextFmt::MakeCamelWT(WTString&& str)
{
	str = MakeCamel(str.Wide());
	return str;
}

WTString AutotextFmt::MakePascalWT(WTString&& str)
{
	str = MakePascal(str.Wide());
	return str;
}

ATextFmtPtr AutotextFmt::FromKeyword(LPCSTR keyWord, LPCSTR replacement /*= nullptr*/)
{
	ATextFmtPtr fmt(new AutotextFmt());
	fmt->SetBaseKeyword(keyWord);
	if (replacement)
		fmt->Replacement = replacement;
	return fmt;
}

ATextFmtPtr AutotextFmt::FromKeywordAndCasings(LPCSTR keyWord, LPCSTR replacement /*= nullptr*/,
                                               LPCSTR modifiers /*= "DULCP"*/)
{
	ATextFmtPtr fmt(new AutotextFmt());
	fmt->SetBaseKeyword(keyWord);
	if (replacement)
		fmt->Replacement = replacement;
	fmt->AddModifiersByKeyword(modifiers);
	return fmt;
}

ATextFmtPtr AutotextFmt::FromKeywordAndCasingsWithSolver(LPCSTR keyWord, MatchFnc solver,
                                                         LPCSTR modifiers /*= "DULCP"*/)
{
	ATextFmtPtr fmt(new AutotextFmt());
	fmt->SetBaseKeyword(keyWord);
	fmt->AddModifiersByKeyword(modifiers);
	fmt->MatchSolver = solver;
	return fmt;
}

ATextFmtPtr AutotextFmt::FromKeywordWithSolver(LPCSTR keyWord, MatchFnc solver)
{
	ATextFmtPtr fmt(new AutotextFmt());
	fmt->SetBaseKeyword(keyWord);
	fmt->MatchSolver = solver;
	return fmt;
}

void AutotextFmt::Modifier::SetKeyword(const WTString& newKW, bool inherit_casing /*= true*/)
{
	IsKeywordType = true;

	if (newKW == Keyword && Rgx.c_str())
		return;

	Keyword = newKW;

	if (Keyword.IsEmpty())
	{
		// this should use "\\w" instead of "a-zA-Z0-9" but "\\w" is not compatible with TRegExp.
		// "a-zA-Z0-9" should be fine since all of our keywords/modifiers are ascii-based.
		Rgx = ("[$][a-zA-Z0-9]+" + Name) + "[$]";
	}
	else
	{
		AutotextFmt::Casing casing = AutotextFmt::DetectCasing(Keyword.c_str());

		if (casing > Default)
		{
			Name.MakeLower();
			Name = AutotextFmt::ApplyCasing(casing, std::move(Name));
		}

		Rgx = ("[$]" + Keyword + Name) + "[$]";
	}
}

void AutotextFmt::Modifier::SetRegex(LPCSTR newRegex)
{
	IsKeywordType = false;
	Rgx = newRegex ? newRegex : "";
}

bool AutotextFmt::Modifier::IsKeyword()
{
	return IsKeywordType;
}

void AutotextFmt::Modifier::operator()(WTString& str) const
{
	if (Fnc)
		Fnc(str);
}

AutotextFmt::Modifier::operator bool() const
{
	return Fnc ? true : false;
}

AutotextFmt::Modifier::Modifier(const WTString& kw, const WTString& name, FmtFnc fnc, bool name_casing_by_kw /*= true*/)
    : Name(name), Fnc(fnc)
{
	SetKeyword(kw, name_casing_by_kw);
}

AutotextFmt::Modifier::Modifier(const WTString& rgx, FmtFnc fnc) : IsKeywordType(false), Fnc(fnc)
{
	Rgx = rgx;
}

AutotextFmt::Modifier::Modifier()
{
}
