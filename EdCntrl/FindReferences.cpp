#include "StdAfxEd.h"
#include "FindReferences.h"
#include "Edcnt.h"
#include "Settings.h"
#include "VaMessages.h"
#include "ParseThrd.h"
#include "mctree/ColumnTreeWnd.h"
#include "Log.h"
#include "..\common\DeletePtr.h"
#include "project.h"
#include "DatabaseDirectoryLock.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "FILE.H"
#include "AutoReferenceHighlighter.h"
#include "SubClassWnd.h"
#include "VAAutomation.h"
#include "WtException.h"
#include "GetFileText.h"
#include "FileTypes.h"
#include "StringUtils.h"
#include "FileId.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

FindReferencesPtr g_References;
DWORD sFindReferencesInstances = 0;

FindReferences::FindReferences(const FindReferences& refs, bool isClone /*= true*/)
    : m_doHighlight(false), flags(refs.flags), mSym(refs.mSym), mSymScope(refs.mSymScope), mOldSym(refs.mOldSym),
      mSymOrigData(refs.mSymOrigData), mSymFileId(0), mSearchScope(refs.mSearchScope), mIsClone(isClone),
      mIsAutoRef(false), mIsRerun(false), mSymCanBeUsedAsAuto(refs.mSymCanBeUsedAsAuto),
      mTypeImageIdx(refs.mTypeImageIdx), mObserver(NULL)
{
	::InterlockedIncrement(&sFindReferencesInstances);
	AutoLockCs l(mVecLock);
	AutoLockCs l2(refs.mVecLock);
	for (References::const_iterator it = refs.refVect.begin(); it != refs.refVect.end(); ++it)
	{
		const FindReference* curRef = *it;
		_ASSERTE(curRef);
		if (curRef->ShouldDisplay())
			refVect.push_back(new FindReference(*curRef));
	}
}

FindReferences::FindReferences(int typeImgIdx, LPCSTR symscope /*= NULL*/, bool isRerun /*=false*/,
                               bool findAutoVars /*= true*/)
    : m_doHighlight(Psettings->mHighlightFindReferencesByDefault),
      flags(FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments), mSymFileId(0),
      mSearchScope(Psettings->mDisplayReferencesFromAllProjects ? searchSolution : searchProject), mIsClone(false),
      mIsAutoRef(false), mIsRerun(isRerun), mSymCanBeUsedAsAuto(TRUE), mObserver(NULL)
{
	::InterlockedIncrement(&sFindReferencesInstances);
	ShouldFindAutoVars(findAutoVars);
	Init(symscope, typeImgIdx);
}

FindReferences::FindReferences()
    : m_doHighlight(true), flags(FREF_Flg_Reference | FREF_Flg_InFileOnly | FREF_FLG_FindAutoVars), mSymFileId(0),
      mSearchScope(searchFile), mIsClone(false), mIsAutoRef(true), mIsRerun(false), mSymCanBeUsedAsAuto(FALSE),
      mObserver(NULL)
{
	::InterlockedIncrement(&sFindReferencesInstances);
	_ASSERTE(::IsAutoReferenceThreadRequired());
}

FindReferences::~FindReferences()
{
	vLog("FindRefs::dtor");
	_ASSERTE(g_References.get() != this);
	StopReferenceHighlights();
	// _ASSERTE(mTmpFileRefs.empty()); // this can happen if find refs is canceled before running to completion
	ClearReferences();
	::InterlockedDecrement(&sFindReferencesInstances);
}

void FindReferences::Init(LPCSTR symscope, int typeImageIdx, bool clear /*= true*/)
{
	mTypeImageIdx = typeImageIdx;
	const WTString symscopestr = StripEncodedTemplates(symscope);
	if (!mIsClone && !IsAutoHighlightRef() && !mIsRerun)
	{
		{
			FindReferencesPtr tmp(g_References);
			if (tmp && tmp->m_doHighlight)
				tmp->StopReferenceHighlights();
		}

		if (g_References.get() != this)
		{
			if (!mThis.expired())
				g_References = mThis.lock();
		}
	}

	if (clear)
		ClearReferences();
	
	mOldSym.Empty();
	mSymScope = symscopestr;
	if (symscope)
		mSym = StrGetSym(symscopestr);

	if (!mIsClone && !IsAutoHighlightRef() && !mIsRerun)
		m_doHighlight = Psettings->mHighlightFindReferencesByDefault;

	RefineSearchSym();
}

void FindReferences::RefineSearchSym()
{
	if (mSymScope.IsEmpty())
	{
		vLogUnfiltered("FindReferences:RSS: empty"); // error?
		mSymOrigData = DType();
		return;
	}

	MultiParsePtr pmp = g_currentEdCnt ? g_currentEdCnt->GetParseDb() : MultiParse::Create(gTypingDevLang);
	// When we Find Usage for a class, find all constructors and destructors as well.
	DType* data = pmp->FindExact2(mSymScope);
	if (!data && 0 == mSymScope.Find(":ForwardDeclare:"))
	{
		// [case: 113008]
		// ignore V_HIDEFROMUSER attr for forward declaration.
		// setting data means icon for reference will show as definition;
		// not sure that is really desirable, but keeping the behavior
		// so that tests behave the same.
		// Without this FindExact2 call, icon for "class foo;" would be
		// normal reference, not definition.
		data = pmp->FindExact2(mSymScope, true, 0, false);
	}

	if (!data)
	{
		// [case: 142375]
		data = pmp->FindExact2(mSymScope, false, GOTODEF);
	}

	if (data && data->type() == C_ENUMITEM)
	{
		// [case:78898]
		DType* dat2 = ::GetBestEnumDtype(pmp.get(), data);
		if (dat2 && dat2 != data)
		{
			data = dat2;
			mSymScope = data->SymScope();
			mSym = data->Sym();
		}
	}

	if (data)
	{
		mSymOrigData = *data;
		mSymOrigData.LoadStrs();
		mSymFileId = data->FileId();

		switch (data->type())
		{
		case FUNC:
		case VAR:
		case DEFINE:
		case VaMacroDefArg:
		case VaMacroDefNoArg:
		case GOTODEF:
		case NAMESPACE:
		case TEMPLATE:
		case TAG:
		case LINQ_VAR:
		case Lambda_Type:
			mSymCanBeUsedAsAuto = FALSE;
			break;
		}
	}
	else
	{
		mSymOrigData = DType();
		mSymCanBeUsedAsAuto = TRUE;
	}

	// [change 5419 originally made in vaparse.cpp]
	// Fix unnamed structs for find usages
	// Since they are added twice, we need to find either
	int unnamedPos = mSymScope.Find(kUnnamed);
	if (unnamedPos != -1)
		mSymScope = mSymScope.Mid(0, unnamedPos + 1) + mSym;

	if (gTestsActive && ::IsReservedWord(mSym, gTypingDevLang))
	{
		::OutputDebugString("Find References run on reserved word, quitting current test");
		mSym = "AstErrorFindReferencesRunOnReservedWord";
		mSymScope = ":";
	}

	vCatLog("Parser.FindReferences", "FindReferences:RSS: s(%s) scp(%s)", mSym.c_str(), mSymScope.c_str());
}

BOOL FindReferences::GotoReference(int i, bool goto_first_line, int selLengthOverride /*= -1*/)
{
	CStringW refFile;
	UINT refFileId = 0;
	ULONG refLineNo = 0;
	ULONG refLineIdx = 0;
	ULONG refPos = 0;
	FREF_TYPE refType = FREF_None;
	int bufIndex = 0;
	EdCntPtr lastEdCnt = g_currentEdCnt;
	bool selectSym = true;

	{
		AutoLockCs l(mVecLock);
		FindReference* ref = GetReference((uint)i);
		if (!ref)
			return FALSE;

		if (ref->dirty != FindReference::rsUnmodified)
		{
			selectSym = false;
			if (!Psettings->mAlternateDirtyRefsNavBehavior && ref->dirty == FindReference::rsModified)
			{
				// go to top of file for references that have been modified
				goto_first_line = true;
			}
		}

		refFile = ref->file;
		refFileId = ref->fileId;
		refLineNo = ref->lineNo;
		refLineIdx = ref->lineIdx;
		refType = ref->type;
		refPos = ref->GetPos();
	}

	if (gShellAttr->IsDevenv11OrHigher() && g_mainThread != GetCurrentThreadId())
	{
		// [case: 69164]
		// strange project-dependent hang during find refs navigation with
		// ui deadlocked in cs lang service
		CStringW lastActiveFile;
		if (lastEdCnt)
			lastActiveFile = lastEdCnt->FileName();

		static CStringW sRefFile;
		sRefFile = refFile;
		extern volatile LONG gFileOpenIdx;
		const LONG kPreviousFileOpenIdx = gFileOpenIdx;
		::PostMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_DELAYOPENFILE, (WPARAM)(LPCWSTR)sRefFile,
		              (LPARAM)(goto_first_line ? 1 : refLineNo));

		::Sleep(50);

		for (int idx = 0; idx < 10; ++idx, ::Sleep(100))
		{
			if (gFileOpenIdx != kPreviousFileOpenIdx)
				break;
		}
	}
	else
		::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_DELAYOPENFILE, (WPARAM)(LPCWSTR)refFile,
		              (LPARAM)(goto_first_line ? 1 : refLineNo));

	EdCntPtr startEd = g_currentEdCnt;
	if (!startEd)
		return FALSE;

	if (refFile.CompareNoCase(startEd->FileName()) != 0)
		return FALSE;

	if (goto_first_line || !selectSym)
		return TRUE;

	// [case: 138734] len must be in utf16 elements
	int kSelLength = (refType == FREF_ReferenceAutoVar || refType == FREF_Creation_Auto) ? 0 : (selLengthOverride >= 0 ? selLengthOverride : mSym.Wide().GetLength());

	if (flags & FREF_Flg_Convert_Enum || flags & FREF_Flg_UeFindImplicit)
	{
		WTString buf;

		buf = startEd->GetBuf(FALSE);
		bufIndex = startEd->GetBufIndex(buf, (long)refPos);

		// convert enum find multiple enum items with multiple lengths. let's find the real length of the occurrence we
		// are jumping to
		int securityCounter = 0;
		for (int i2 = bufIndex; i2 < buf.GetLength(); i2++)
		{
			if (securityCounter++ > 128) // max symbol length
				break;
			TCHAR c = buf[i2];
			if (ISCSYM(c))
				continue;
			kSelLength = i2 - bufIndex;
			break;
		}
	}

	if (gShellAttr->IsDevenv())
	{
		startEd->SendMessage(WM_VA_THREAD_SETSELECTION, refPos, LPARAM(refPos + kSelLength));
		WTString sel(startEd->GetSelString());
		if (sel != mSym && sel != "auto" && sel != "var")
		{
			if (!mOldSym.IsEmpty())
			{
				// [case: 73302] after using change signature to do a rename in which not
				// all references were updated, then some references will have the old name.
				// [case: 138734] len must be in utf16 elements
				int selLen = mOldSym.Wide().GetLength();
				startEd->SendMessage(WM_VA_THREAD_SETSELECTION, refPos, LPARAM(refPos + selLen));
				sel = startEd->GetSelString();
				if (sel == mOldSym)
					sel = mSym;
			}

			if (sel != mSym)
			{
				// [case: 71169] #findRefsUnicodeHack
				const WTString lineText(startEd->GetLine((int)refLineNo));
				refLineIdx = (ULONG)::ByteOffsetToUtf16ElementOffset(lineText, (int)refLineIdx);
				refPos = TERRCTOLONG(refLineNo, refLineIdx);
				startEd->SendMessage(WM_VA_THREAD_SETSELECTION, refPos, LPARAM(refPos + kSelLength));
			}
		}
	}
	else
	{
		int startCurPos = 0;
		startEd->SendMessage(WM_VA_THREAD_GETBUFINDEX, (WPARAM)-1, (LPARAM)&startCurPos);

		WTString buf;
		startEd->SendMessage(WM_VA_THREAD_GETBUFFER, (WPARAM)&buf, 0);

		ULONG gotoPos = refPos;

		// See if text matches up with reference
		int fpos;
		startEd->SendMessage(WM_VA_THREAD_GETBUFINDEX, refPos, (LPARAM)&fpos);
		if (!StartsWith(buf.c_str() + fpos, mSym))
		{
			// file has been modified
			CWaitCursor cur;
			FindReferences trefs(mTypeImageIdx, mSymScope.c_str(), true);
			trefs.flags = flags;
			trefs.SearchFile(CStringW(), startEd->FileName());
			int nFromEnd = 0;
			bool didBreak = false;
			for (; (i + nFromEnd) < (int)Count(); nFromEnd++)
			{
				const FindReference* theRef = GetReference(uint(i + nFromEnd));
				_ASSERTE(theRef);
				if (refFileId != theRef->fileId)
				{
					didBreak = true;
					break;
				}
			}

			if (nFromEnd > (int)trefs.Count() && !didBreak)
				nFromEnd = (int)trefs.Count(); // grab the last ref (is from the current file)

			if (nFromEnd > 0 && nFromEnd <= (int)trefs.Count())
			{
				const FindReference* theRef = trefs.GetReference(trefs.Count() - nFromEnd);
				_ASSERTE(theRef);

				{
					AutoLockCs l(mVecLock);
					FindReference* ref = GetReference((size_t)i);
					if (ref && ref->fileId == refFileId && ref->lineNo == refLineNo && ref->GetPos() == refPos)
					{
						// update the reference with the new info
						ref->lineNo = theRef->lineNo;
						ref->lineIdx = theRef->lineIdx;
						ref->pos = theRef->pos;
						gotoPos = theRef->GetPos();
						_ASSERTE(ULONG_MAX != gotoPos);
					}
				}
			}
		}

		// Simulate case=51696
		// Sleep(10000);

		EdCntPtr endEd = g_currentEdCnt;
		int endCurPos = 0;
		if (endEd)
			endEd->SendMessage(WM_VA_THREAD_GETBUFINDEX, (WPARAM)-1, (LPARAM)&endCurPos);

		if (startEd == endEd && startCurPos == endCurPos && ULONG_MAX != gotoPos)
		{
			// cursor hasn't moved since opening file
			endEd->SendMessage(WM_VA_THREAD_SETSELECTION, gotoPos, LPARAM(gotoPos + kSelLength));
		}
		else
		{
			vLog("FindReferences:Goto: cursor moved since file open");
		}

		if (m_doHighlight && lastEdCnt == g_currentEdCnt)
		{
			//			if(lastFirstVisibleLine != lastEdCnt->GetFirstVisibleLine())
			lastEdCnt->Invalidate(true);
		}
	}

#ifdef _DEBUG
	{
		EdCntPtr endEd = g_currentEdCnt;
		_ASSERTE(endEd);
		if (endEd)
		{
// #RAD_MissingFeature
#ifndef RAD_STUDIO
			const WTString sel = endEd->GetSelString();
			_ASSERTE("Goto Reference fail" &&
			         (sel == mSym || (flags & FREF_Flg_Convert_Enum) || (flags & FREF_Flg_UeFindImplicit) ||
			          sel == "auto" || sel == "var" || (!mOldSym.IsEmpty() && sel == mOldSym)));
#endif
		}
	}
#endif // _DEBUG

	return TRUE;
}

int FindReferences::SearchFile(const CStringW& project, const CStringW& file, WTString* buf /* = NULL */)
{
	_ASSERTE(!IsAutoHighlightRef());
	if (!mIsClone && !mIsRerun && g_References.get() != this)
	{
		if (!mThis.expired())
			g_References = mThis.lock();
	}
	const EdCntPtr ed(::GetOpenEditWnd(file)); // Doc may no be able to get buffer, must do a save all before command
	WTString txt = buf ? *buf : ::GetFileText(file);
	bool shouldSearchFile =
	    (flags & FREF_Flg_FindErrors) ? true : NULL != ::strstrWholeWord(txt, mSym, ::IsCaseSensitiveFiletype(file));
	if (!shouldSearchFile && ShouldFindAutoVars())
	{
		const int fType = ::GetFileType(file);
		if (Is_C_CS_File(fType))
		{
			// [case: 69271]
			if (CS == fType)
				shouldSearchFile = NULL != ::strstrWholeWord(txt, "var", ::IsCaseSensitiveFiletype(file));
			else
				shouldSearchFile = NULL != ::strstrWholeWord(txt, "auto", ::IsCaseSensitiveFiletype(file));
		}
	}
	const size_t orgcount = Count();
	_ASSERTE(!orgcount); // this should be a clean single-threaded call to SearchFile
	_ASSERTE(mTmpFileRefs.empty());
	if (shouldSearchFile)
	{
		vCatLog("Parser.FindReferences", "FindReferences:File %s", (LPCTSTR)CString(file));
		DatabaseDirectoryLock l2;
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			MultiParsePtr mp = MultiParse::Create();
			// this is fast if already parsed
			mp->FormatFile(file, V_INFILE, ParseType_Locals, false, txt.c_str());

			// this can be slow in large files - due to having to look up
			// classdata for every symbol in the file
			mp->FindAllReferences(project, file, this, &txt, NULL, TRUE);
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("MP:SearchFile");
		}
#endif // !SEAN
	}

	FlushFileRefs(file, NULL);

	if (m_doHighlight && ed == g_currentEdCnt && g_currentEdCnt)
		g_currentEdCnt->Invalidate(TRUE);

	return int(Count() - orgcount);
}

void FindReferences::SearchFile(const CStringW& project, const CStringW& file, volatile const INT* monitorForQuit)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	if (!mIsClone && !IsAutoHighlightRef() && !mIsRerun && g_References.get() != this)
	{
		if (!mThis.expired())
			g_References = mThis.lock();
	}
	const EdCntPtr ed(::GetOpenEditWnd(file)); // Doc may no be able to get buffer, must do a save all before command
	WTString txt(::GetFileText(file));
	LPCSTR pos;
	if (flags & FREF_Flg_Convert_Enum)
	{
		pos = nullptr;
		for (const auto& element : elements)
		{
			pos = ::strstrWholeWord(txt, element, ::IsCaseSensitiveFiletype(file));
			if (pos != nullptr)
				break;
		}
	}
	else
	{
		pos = (flags & FREF_Flg_FindErrors) ? "parseNoMatterWhat"
		                                    : ::strstrWholeWord(txt, mSym, ::IsCaseSensitiveFiletype(file));
		if (pos == nullptr && flags & FREF_Flg_UeFindImplicit)
		{
			// [case: 141287] also search for *_Implementation and *_Validate methods
			pos = ::strstrWholeWord(txt, mSym + "_Implementation", ::IsCaseSensitiveFiletype(file));
			if (pos == nullptr)
				pos = ::strstrWholeWord(txt, mSym + "_Validate", ::IsCaseSensitiveFiletype(file));
		}
	}
	if (pos || (flags & FREF_Flg_FindErrors))
	{
		vLog("FindReferences2:File %s", (LPCTSTR)CString(file));
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			MultiParsePtr mp = MultiParse::Create();
			// this is fast if already parsed
			mp->FormatFile(file, V_INFILE, ParseType_Locals, false, txt.c_str());
			if (monitorForQuit && *monitorForQuit)
				return;

			// this can be slow in large files - due to having to look up
			// classdata for every symbol in the file
			mp->FindAllReferences(project, file, this, &txt, monitorForQuit, !IsAutoHighlightRef());
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("MP:SearchFile");
		}
#endif // !SEAN
	}

	if (m_doHighlight && ed == g_currentEdCnt && g_currentEdCnt && !IsAutoHighlightRef())
		g_currentEdCnt->Invalidate(TRUE);
}

int FindReferences::GetFileLine(int i, CStringW& file, long& pos, WTString& linetext, bool strip_markers)
{
	AutoLockCs l(mVecLock);
	FindReference* ref = GetReference((size_t)i);
	if (ref)
	{
		file = ref->file;
		pos = (long)ref->GetPos();
		if (strip_markers) // by default, strip bold/colouring markers to return clear text as before
		{
			linetext = "";
			for (int i2 = 0; i2 < ref->lnText.GetLength(); i2++)
			{
				if (!strchr(MARKERS, ref->lnText[i2]))
					linetext.append(ref->lnText[i2]);
			}
		}
		else
		{
			linetext = ref->lnText;
		}
		return ref->type;
	}
	return 0;
}

FREF_TYPE
FindReferences::IsReference(const CStringW& file, ULONG line, ULONG xOffset)
{
	AutoLockCs l(mVecLock);
	size_t count = Count();
	for (size_t i = 0; i < count; i++)
	{
		FindReference* ref = GetReference(i);
		_ASSERTE(ref);
		if (line == ref->lineNo && ref->file == file)
			return ref->type;
	}
	return FREF_None;
}

FindReference* FindReferences::GetReference(size_t i)
{
	AutoLockCs l(mVecLock);
	_ASSERTE(!(i & 0x80000000));
	if (i >= refVect.size())
		return NULL;
	return refVect[i];
}

void FindReferences::ClearReferences()
{
	{
		AutoLockCs l(mTmpFileRefsLock);
		for (FileReferences::iterator it = mTmpFileRefs.begin(); it != mTmpFileRefs.end(); ++it)
			for_each(it->second.begin(), it->second.end(), DeletePtr<FindReference>());
		mTmpFileRefs.clear();
	}

	{
		AutoLockCs l(mVecLock);
		for_each(refVect.begin(), refVect.end(), DeletePtr<FindReference>());
		refVect.clear();
	}
}

void FindReferences::RemoveLastUnrelatedComments(const CStringW& fromFile)
{
	if (StopIt)
		return;

	{
		AutoLockCs l(mVecLock);
		size_t count = Count();
		while (count)
		{
			FindReference* ref = GetReference(count - 1);
			_ASSERTE(ref);
			if (ref->type != FREF_Comment || ref->file != fromFile)
				break;
			refVect.pop_back();
			delete ref;
			count = Count();
		}
	}

	if (!IsAutoHighlightRef())
	{
		AutoLockCs l(mTmpFileRefsLock);
		References& fRef = mTmpFileRefs[fromFile];
		for (References::reverse_iterator it = fRef.rbegin(); it != fRef.rend();)
		{
			FindReference* ref = *it;
			_ASSERTE(ref);
			if (ref->type != FREF_Comment)
				break;

			// http://stackoverflow.com/questions/1830158/how-to-call-erase-with-a-reverse-iterator
			fRef.erase((it + 1).base());
			it = fRef.rbegin();
			delete ref;
		}

		if (fRef.empty())
			mTmpFileRefs.erase(fromFile);
	}
}

bool FindReferences::HideReference(int i)
{
	AutoLockCs l(mVecLock);
	FindReference* ref = GetReference((size_t)i);
	if (ref && ref->ShouldDisplay())
	{
		ref->Hide();
		return true;
	}
	return false;
}

void FindReferences::AddHighlightReferenceMarkers()
{
	if (m_doHighlight && g_currentEdCnt)
	{
		const CStringW& inFile = g_currentEdCnt->FileName();
		AutoLockCs l(mVecLock);
		size_t count = Count();
		for (size_t i = 0; i < count; i++)
		{
			FindReference* ref = GetReference(i);
			_ASSERTE(ref);
			if (ref->file == inFile)
			{
				EdCntPtr ed = g_currentEdCnt;
				switch (ref->type)
				{
				case FREF_None:
					break;
				case FREF_DefinitionAssign:
				case FREF_ReferenceAssign:
					if (IsAutoHighlightRef())
						g_ScreenAttrs.QueueDisplayAttribute(ed, mSym, (long)ref->GetPos(), SA_REFERENCE_ASSIGN_AUTO);
					else
						g_ScreenAttrs.AddDisplayAttribute(ed, mSym, (long)ref->GetPos(), SA_REFERENCE_ASSIGN);
					break;
				case FREF_ReferenceAutoVar: {
					const WTString autoVar(CS == ed->m_ftype ? "var" : "auto");
					if (IsAutoHighlightRef())
						g_ScreenAttrs.QueueDisplayAttribute(ed, autoVar, (long)ref->GetPos(), SA_REFERENCE_AUTO);
					else
						g_ScreenAttrs.AddDisplayAttribute(ed, autoVar, (long)ref->GetPos(), SA_REFERENCE);
				}
				break;
				default:
					if (IsAutoHighlightRef())
						g_ScreenAttrs.QueueDisplayAttribute(ed, mSym, (long)ref->GetPos(), SA_REFERENCE_AUTO);
					else
						g_ScreenAttrs.AddDisplayAttribute(ed, mSym, (long)ref->GetPos(), SA_REFERENCE);
					break;
				}
			}
		}
	}
}

void FindReferences::RemoveHighlightReferenceMarkers()
{
	_ASSERTE(Psettings->mUseMarkerApi);
	if (IsAutoHighlightRef())
	{
		g_ScreenAttrs.Invalidate(SA_REFERENCE_AUTO);
		g_ScreenAttrs.Invalidate(SA_REFERENCE_ASSIGN_AUTO);
	}
	else
	{
		g_ScreenAttrs.Invalidate(SA_REFERENCE);
		g_ScreenAttrs.Invalidate(SA_REFERENCE_ASSIGN);
	}
}

void FindReferences::StopReferenceHighlights()
{
	if (!m_doHighlight)
		return;

	m_doHighlight = FALSE;
	if (gShellAttr && Psettings)
	{
		if (Psettings->mUseMarkerApi)
			RemoveHighlightReferenceMarkers();
		else
			::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
}

void FindReferences::Redraw()
{
	if (Psettings->mUseMarkerApi)
	{
		RemoveHighlightReferenceMarkers();
		AddHighlightReferenceMarkers();
	}
}

void FindReferences::InitBcl(MultiParse* mp, int langType)
{
	if (mSymScope.Find(":ForwardDeclare:") != -1)
		return; // [case: 18883]

	_ASSERTE(mp);
	AutoLockCs l(mBclLock);
	const WTString bcl = mp->GetBaseClassList(::StrGetSymScope(mSymScope), false, 0, langType);
	if (bcl.Find(":ForwardDeclare:") == -1 && bcl != "\f:wILDCard\f\f")
		m_BCL_ary.Init(NULLSTR, bcl, NULLSTR);
}

FindReference* FindReferences::Add(const CStringW& project, const CStringW& file, ULONG lineNo, ULONG lineIdx,
                                   ULONG pos, FREF_TYPE type, LPCSTR lineText)
{
	FindReference* ref = new FindReference(project, file, lineNo, lineIdx, pos, type, lineText);

#if defined(RAD_STUDIO)
	ref->RAD_pass = RAD_pass;
#endif

	if (mObserver)
	{
		_ASSERTE(IsAutoHighlightRef());
		AutoLockCs l(mVecLock);
		refVect.push_back(ref);
		const size_t cnt = refVect.size() - 1;
		// do this so that the auto highlighter can cancel the find if
		// there are too many hits
		mObserver->OnReference((int)cnt);
	}
	else
	{
		_ASSERTE(!IsAutoHighlightRef());
		AutoLockCs l(mTmpFileRefsLock);
		References& fRef = mTmpFileRefs[file];
		if (fRef.size())
		{
			FindReference* prevRef = fRef.back();
			if (prevRef && prevRef->IsEquivalent(*ref))
			{
				// [case: 71372] xaml parser adds duplicate refs for some items
				delete ref;
				return nullptr;
			}
		}
		fRef.push_back(ref);
	}

	return ref;
}

struct UpdateObserverParams
{
	IFindReferencesThreadObserver* mObserver;
	CStringW mFile;
	int mPrevCount;
	int mFound;

	UpdateObserverParams(IFindReferencesThreadObserver* obs, const CStringW& file, int prev, int found)
	    : mObserver(obs), mFile(file), mPrevCount(prev), mFound(found)
	{
	}
};

void WINAPI UpdateObserver(UpdateObserverParams* params)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	params->mObserver->OnBeforeFlushReferences((uint32_t)params->mFound);
	params->mObserver->OnFoundInFile(params->mFile, params->mPrevCount);
	for (int i = 0; i < params->mFound; i++)
		params->mObserver->OnReference(params->mPrevCount + i);
	params->mObserver->OnAfterFlushReferences();
}

void FindReferences::FlushFileRefs(const CStringW& file, IFindReferencesThreadObserver* observer)
{
	_ASSERTE(!IsAutoHighlightRef());
	int found, prevCount;

	{
		References fRef;
		{
			AutoLockCs l1(mTmpFileRefsLock);
			auto it = mTmpFileRefs.find(file);
			if(it != mTmpFileRefs.end())
			{
				fRef = std::move(it->second);
				mTmpFileRefs.erase(it);
			}
		}

		found = (int)fRef.size();
		if (found)
		{
			AutoLockCs l2(mVecLock);
			prevCount = (int)Count();
			refVect.append_range(fRef);
		}
		else
			prevCount = 0;

		if (!found || !observer)
			return;
	}

// 	UpdateObserverParams params(observer, file, prevCount, found);
// 	// Run from UI thread to reduce slowdown from multiple cross thread SendMessages
// 	::RunFromMainThread<void, UpdateObserverParams*>(UpdateObserver, &params);

#ifndef VA_CPPUNIT
	extern std::atomic_uint64_t rfmt_flush;
	++rfmt_flush;
#endif
	RunFromMainThread2([=] {
		UpdateObserverParams params(observer, file, prevCount, found);
		UpdateObserver(&params);
	});
}

CStringW FindReferences::GetSummary(DWORD filter /*= FREF_None*/) const
{
	CStringW sum;
	CString__FormatW(sum, L"Find References Results: %s %s 0x%x\r\n", (LPCWSTR)mSym.Wide(), (LPCWSTR)mSymScope.Wide(),
	                 flags);

	AutoLockCs l(mVecLock);
	CStringW curItemStr;
	for (References::const_iterator it = refVect.begin(); it != refVect.end(); ++it)
	{
		const FindReference* cur = *it;
		if (filter != FREF_None && (cur->type == FREF_Comment || cur->type == FREF_IncludeDirective) &&
		    !(filter & (1 << FREF_Comment))) // logic is from #RenameDlgFilter
			continue;
		CStringW tmp = cur->lnText.Wide();

		CStringW lnText;
#if defined(VA_CPPUNIT)
		lnText = tmp;
#else
		// remove line number from lnText
		const int openParenPos = tmp.Find(L'(');
		const int closeParenPos = tmp.Find(L")", openParenPos);
		lnText = tmp.Left(openParenPos + 1);
		lnText += "line # removed";
		lnText += tmp.Mid(closeParenPos);
#endif

		lnText.Replace(CStringW(MARKER_NONE), L"<MarkerNone>");
		lnText.Replace(CStringW(MARKER_BOLD), L"<MarkerBold>");
		lnText.Replace(CStringW(MARKER_REF), L"<MarkerRef>");
		lnText.Replace(CStringW(MARKER_ASSIGN), L"<MarkerAssign>");
		lnText.Replace(CStringW(MARKER_RECT), L"<MarkerRect>");
		lnText.Replace(CStringW(MARKER_INVERT), L"<MarkerInvert>");
		lnText.Replace(CStringW(MARKER_DIM), L"<MarkerDim>");
		CString__FormatW(curItemStr, L"  %s  %d  %d  %s\r\n", (LPCWSTR)::Basename(cur->file), cur->type,
		                 cur->overridden, (LPCWSTR)lnText);
		sum += curItemStr;
	}

	return sum;
}

void FindReferences::DocumentModified(const WCHAR* filenameIn, int startLineNo, int startLineIdx, int oldEndLineNo,
                                      int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo)
{
	// opening the file for the first time.
	if (editNo == 0)
		return;

	// convert from 0-based to 1-based
	++startLineNo;
	++startLineIdx;
	++oldEndLineNo;
	++oldEndLineIdx;
	++newEndLineNo;
	++newEndLineIdx;

	const int linesAdded = newEndLineNo - oldEndLineNo; // might be negative if lines deleted

	static const WCHAR* sLastFilename = NULL;
	static UINT sFileId = 0;
	if (sLastFilename != filenameIn) // raw pointer compare
	{
		sLastFilename = filenameIn;
		sFileId = gFileIdManager->GetFileId(filenameIn);
	}

	bool foundMatch = false;
	AutoLockCs l(mVecLock);
	for (References::iterator it = refVect.begin(); it != refVect.end(); ++it)
	{
		FindReference* cur = *it;
		if (cur->fileId == sFileId)
		{
			foundMatch = true;

			if ((int)cur->lineNo < startLineNo)
			{
				// edit is after span
			}
			else if ((int)cur->lineNo > oldEndLineNo)
			{
				// edit is fully before span
				cur->lineNo += linesAdded;
				_ASSERTE((int)cur->lineNo >= 0);
			}
			else
			{
				// LINE TEXT HAS BEEN MODIFIED OR DELETED
				if ((int)cur->lineNo == startLineNo && (int)cur->lineIdx + mSym.GetLength() <= startLineIdx)
				{
					// edit starts after span, but on same line
				}
				else if ((int)cur->lineNo == oldEndLineNo && (int)cur->lineIdx >= oldEndLineIdx)
				{
					// edit ends before span, but on same line
					cur->lineNo += linesAdded;
					cur->lineIdx += (newEndLineIdx - oldEndLineIdx);
				}
				else
				{
					// SPAN TEXT HAS BEEN MODIFIED OR DELETED
					if (RefactoringActive::IsActive() &&
					    VARef_ChangeSignature == RefactoringActive::GetCurrentRefactoring())
						cur->dirty = FindReference::rsRefactored;
					else
						cur->dirty = FindReference::rsModified;
				}
			}
		}
		else
		{
			// if we've found a match already, then we're done, since
			// all refs for a particular file are grouped together.
			if (foundMatch)
				break;
		}
	}
}

void FindReferences::DocumentClosed(const WCHAR* filename)
{
	UINT fileId = gFileIdManager->GetFileId(filename);
	AutoLockCs l(mVecLock);
	size_t count = Count();
	for (size_t i = 0; i < count; i++)
	{
		FindReference* ref = GetReference(i);
		_ASSERTE(ref);
		if (ref->fileId == fileId)
			ref->RevertChanges();
	}
}

void FindReferences::DocumentSaved(const WCHAR* filename)
{
	UINT fileId = gFileIdManager->GetFileId(filename);
	AutoLockCs l(mVecLock);
	size_t count = Count();
	for (size_t i = 0; i < count; i++)
	{
		FindReference* ref = GetReference(i);
		_ASSERTE(ref);
		if (ref->fileId == fileId)
			ref->CommitChanges();
	}
}

void FindReferences::UpdateSymForRename(const WTString& sym, const WTString& scope)
{
	if (sym == mSym)
		mOldSym.Empty();
	else
		mOldSym = mSym;

	mSym = sym;
	mSymScope = scope;
	RefineSearchSym();
}

WTString FindReferences::GetScopeOfSearchStr() const
{
	switch (mSearchScope)
	{
	case FindReferences::searchFile:
		return WTString("file");
	case FindReferences::searchFilePair:
		return WTString("corresponding files");
	case FindReferences::searchProject:
		return WTString("project");
	case FindReferences::searchSolution:
		return WTString(gShellAttr->IsMsdev() ? "workspace" : "solution");
	case FindReferences::searchSharedProjects:
		return WTString(gShellAttr->IsMsdev() ? "workspace" : "shared projects");
	default:
		_ASSERTE(!"unhandled search scope");
		return WTString("solution");
	}
}

FindReference::FindReference(const CStringW& project, const CStringW& file, ULONG lineNo, ULONG lineIdx, ULONG pos,
                             FREF_TYPE type, LPCSTR lnText)
    : mShouldDisplay(true), mInitialPos(TERRCTOLONG(lineNo, lineIdx)), mProject(project), file(file), lineNo(lineNo),
      lineIdx(lineIdx), pos(pos), type(type), lnText(lnText), overridden(FALSE), dirty(rsUnmodified)
{
	fileId = gFileIdManager->GetFileId(file);
}

ULONG
FindReference::GetPos() const
{
	return (pos == -1) ? TERRCTOLONG(lineNo, lineIdx) : pos;
}

ULONG
FindReference::GetEdBufCharOffset(EdCntPtr ed, const WTString& symName, const WTString& bufIn /*= NULLSTR*/) const
{
	_ASSERTE(ed && !symName.IsEmpty());
	WTString buf(bufIn);
	if (buf.IsEmpty())
		buf = ed->GetBuf(TRUE);

	long symPos = ed->GetBufIndex((long)GetPos());
	WTString theStrAtSymPos;
	if (symPos < buf.GetLength())
		theStrAtSymPos = ::GetCStr(&buf.c_str()[symPos]);
	if (theStrAtSymPos != symName)
	{
		// #findRefsUnicodeHack
		const WTString lineText(ed->GetLine((int)lineNo));
		// [case: 138734
		// this call doesn't seem right, but I can't trigger the condition at the
		// start of the block to test the call,  Seems that this findRefsUnicodeHack
		// is no longer necessary since switching to utf8 internally
		long refLineIdx = ::ByteOffsetToCharOffset(lineText, (int)lineIdx);
		long refPos = TERRCTOLONG(lineNo, refLineIdx);
		symPos = ed->GetBufIndex(refPos);
		if (symPos < buf.GetLength())
			theStrAtSymPos = ::GetCStr(&buf.c_str()[symPos]);
	}

	_ASSERTE(theStrAtSymPos == symName);
	return (ULONG)symPos;
}

void FindReference::RevertChanges()
{
	lineNo = TERROW(mInitialPos);
	lineIdx = TERCOL(mInitialPos);
}

void FindReference::CommitChanges()
{
	mInitialPos = TERRCTOLONG(lineNo, lineIdx);
}

bool FindReference::IsEquivalent(const FindReference& rhs)
{
	if (rhs.fileId != fileId || rhs.lineIdx != lineIdx || rhs.pos != pos || rhs.lineNo != lineNo || rhs.type != type ||
	    rhs.lnText != lnText)
		return false;

	return true;
}
