#include "stdafxed.h"
#include "resource.h"
#include "Edcnt.h"
#include "VAParse.h"
#include "AutotextManager.h"
#include "ScreenAttributes.h"
#include "expansion.h"
#include "SubClassWnd.h"
#include "AddClassMemberDlg.h"
#include "VaService.h"
#include "MenuXP/MenuXP.h"
#include "varefactor.h"
#include "token.h"
#include "ExtractMethod.h"
#include "wt_stdlib.h"
#include "file.h"
#include "Settings.h"
#include "DBLock.h"
#include "EolTypes.h"
#include "FreezeDisplay.h"
#include "UndoContext.h"
#include "VaMessages.h"
#include "VACompletionBox.h"
#include "RenameReferencesDlg.h"
#include "FontSettings.h"
#include "TraceWindowFrame.h"
#include "FileId.h"
#include "project.h"
#include "CreateFromUsage.h"
#include "RegKeys.h"
#include "Registry.h"
#include "ChangeSignature.h"
#include <memory>
#include "VAAutomation.h"
#include "CodeGraph.h"
#include "ImplementMethods.h"
#include "IdeSettings.h"
#include "KeyBindings.h"
#include "RenameFilesDlg.h"
#include "CreateFileDlg.h"
#include "Project.h"
#include "ProjectInfo.h"
#include "FileLineMarker.h"
#include "IntroduceVariable.h"
#include "VASmartSelect.h"
#include "EdcntWPF.h"
#include "../VaPkg/VaPkgUI/PkgCmdID.h"
#include "CommentSkipper.h"
#include "FindSimilarLocation.h"
#include "GetFileText.h"
#include "includesDb.h"
#include "CreateMissingCases.h"
#include "EncapsulateField.h"
#include "VaTree.h"
#include "LogElapsedTime.h"
#include "../common/TempAssign.h"
#include "ConvertBetweenPointerAndInstance.h"
#include "FileFinder.h"
#include "VaTimers.h"
#include "VAOpenFile.h"
#include "AddForwardDeclaration.h"
#include "Mparse.h"
#include "EnumConverter.h"
#include "SymbolPositions.h"
#include "VAWatermarks.h"
#include "serial_for.h"
#include "PromoteLambda.h"
#pragma warning(push, 3)
#include "miloyip-rapidjson\include\rapidjson\document.h"
#include "MethodsSorting/MethodsSorting.h"
#pragma warning(pop)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

bool IsWhiteSpace(char letter)
{
	switch (letter)
	{
	case ' ':
	case '\r':
	case '\n':
	case '\t':
		return true;

	default:
		return false;
	}
}

bool IsWhiteSpace(wchar_t letter)
{
	switch (letter)
	{
	case L' ':
	case L'\r':
	case L'\n':
	case L'\t':
		return true;

	default:
		return false;
	}
}

static int FindExactList(MultiParsePtr& pmp, const WTString& symscope, DTypeList& lst, bool searchSys = true)
{
	auto SymHash = DType::GetSymHash(symscope);
	auto ScopeHash = DType::GetScopeHash(symscope);
	return pmp->FindExactList(SymHash, ScopeHash, lst, searchSys);
}

void OnRefactorErrorMsgBox(WTString customErrorString /*= ""*/)
{
	static const char skRefactorErrorMessage[] =
	    "Visual Assist failed to modify one or more files.  The refactoring may not have completed successfully.\n"
	    "Inspect your files to see if any changes need to be rolled back.\n\n"
	    "Common causes for this error are read-only files and related source control problems.";

	if (customErrorString.IsEmpty())
		::ErrorBox(skRefactorErrorMessage, MB_OK | MB_ICONWARNING);
	else
		::ErrorBox(customErrorString.c_str(), MB_OK | MB_ICONWARNING);
}

BOOL PV_InsertAutotextTemplate(const WTString& templateText, BOOL reformat, const WTString& promptTitle = NULLSTR)
{
	// [case: 9863] remove unsupported symbol related snippet reserved words in refactoring snippets
	WTString sanitizedText = templateText;
	sanitizedText.ReplaceAll("$MethodName$", "");
	sanitizedText.ReplaceAll("$MethodArgs$", "");
	sanitizedText.ReplaceAll("$ClassName$", "");
	sanitizedText.ReplaceAll("$BaseClassName$", "");
	sanitizedText.ReplaceAll("$NamespaceName$", "");

	BOOL rslt = gAutotextMgr->InsertAsTemplate(g_currentEdCnt, sanitizedText, reformat, promptTitle);
	if (TRUE != rslt)
		OnRefactorErrorMsgBox();
	return rslt;
}

static CStringW DoFileFromScope(WTString symScope, DType* outSym = NULL)
{
	const CStringW activeFile(g_currentEdCnt ? g_currentEdCnt->FileName() : CStringW());
	const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(activeFile));
	MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
	CStringW file;
	if (!projForActiveFile.size())
		return file;

	DTypeList dList;
	if (mp->FindExactList(symScope.c_str(), dList, false))
	{
		for (DTypeList::iterator iter = dList.begin(); iter != dList.end();)
		{
			const DType& dt = *iter;
			if (dt.MaskedType() == GOTODEF && dList.size() > 1)
			{
				// previous version used FindExact2 which filters out GOTODEF dtypes
				dList.erase(iter++);
			}
			else
			{
				const CStringW tmpFile(gFileIdManager->GetFileForUser(dt.FileId()));
				ProjectVec projForFoundFile(GlobalProject->GetProjectForFile(tmpFile));
				if (projForActiveFile == projForFoundFile)
				{
					file = tmpFile;
					if (outSym)
						*outSym = DType(dt);
					break;
				}

				++iter;
			}
		}
	}

	return file;
}

CStringW FileFromScope(WTString symScope, DType* outSym /*= NULL*/)
{
	CStringW file(DoFileFromScope(symScope, outSym));
	if (file.IsEmpty())
	{
		// fall back to old logic
		EdCntPtr ed(g_currentEdCnt);
		MultiParsePtr pmp = ed ? ed->GetParseDb() : MultiParse::Create(gTypingDevLang);
		DType* data = pmp->FindExact2(symScope.c_str());
		if (data)
		{
			file = gFileIdManager->GetFileForUser(data->FileId());
			if (outSym)
			{
				data->LoadStrs();
				*outSym = data;
			}
		}
	}

	return file;
}

CStringW FileFromDef(DType* sym)
{
	WTString symSymScope(sym->SymScope());
	CStringW file(DoFileFromScope(symSymScope));
	if (file.IsEmpty())
	{
		// fall back to old logic
		MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
		DType* dt = mp->FindExact2(symSymScope.c_str());
		if (!dt)
			dt = sym;
		file = gFileIdManager->GetFileForUser(dt->FileId());
	}

#ifdef _DEBUG
	// old code kept for the asserts
	{
		MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
		DType* gData = mp->FindExact2(symSymScope.c_str());
		if (gData)
			sym = gData;

		CStringW oldFile(gFileIdManager->GetFileForUser(sym->FileId()));
		if (!oldFile.GetLength())
		{
			//			DType * oldSym = sym;
			DType* newSym = mp->FindExact2(symSymScope.c_str());
			if (newSym)
			{
				// tell sean if these fire - can't figure out from revision #62 what intent was;
				// encapsulate field on class defined in source file does not hit inside !file.GetLength()
				_ASSERTE(!"need test case for this condition");
			}
		}
	}
#endif // _DEBUG

	return file;
}

void ReparseAndWaitIfNeeded(bool forcedOperation = false)
{
	// [case: 4568]
	const int maxFileSize = forcedOperation ? INT_MAX : Psettings->mReparseIfNeeded;
	if (maxFileSize == 0)
		return;

	EdCntPtr ed(g_currentEdCnt);
	if (ed == nullptr)
		return;

	if (ed->HasInitialParseCompleted() && !ed->m_FileHasTimerForFullReparse && !ed->m_FileIsQuedForFullReparse)
		return;

	if (!CAN_USE_NEW_SCOPE())
	{
		// don't wait in unsupported/unparsed file types
		return;
	}

	LogElapsedTime let("ReparseAndWait", 50);
	if (!ed->m_FileIsQuedForFullReparse)
	{
		int fileSize = ed->GetBuf(TRUE).GetLength();
		if (maxFileSize == -1 || fileSize <= maxFileSize)
			ed->QueForReparse();
		else
			return;
	}

	HWND h = ed->vGetFocus()->GetSafeHwnd();
	DWORD duration = 0;
	// 2 second timeout if maxFileSize <= the default file size.
	const DWORD kMaxDuration =
	    maxFileSize <= REPARSE_IF_NEEDED_DEFAULT_VAL ? (maxFileSize == -1 ? 7000u : 2000u) : 7000u;
	while (ed->m_FileIsQuedForFullReparse && duration < kMaxDuration)
	{
		MSG m;
		PeekMessage(&m, h, WM_TIMER, WM_TIMER, PM_NOYIELD | PM_NOREMOVE);
		PeekMessage(&m, h, WM_VA_SET_DB, WM_VA_SET_DB, PM_NOYIELD | PM_NOREMOVE);
		Sleep(25);
		duration += 25;
	}

	ed->Scope();
}

volatile LONG RefactoringActive::sIsActive = 0;
volatile int RefactoringActive::sCurrentRefactoring = 0;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
bool DoImplementPropertyCppB(DType* sym)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;

	int ln = sym->Line();
	if (ln < 0)
		return false; // no valid line number found, don't continue with implementation

	int lnFirst = ln;
	for (; lnFirst >= 0; lnFirst--)
	{
		WTString lineText = ed->GetLine(lnFirst);
		if (lineText.contains("__property"))
			break;
	}

	if (lnFirst < 0)
		return false; // something is wrong with formatting of the property, don't go to implementation

	uint linePos1 = ed->LinePos(lnFirst);
	WTString bufPos = ed->GetSubString(linePos1, NPOS);
	ReadToCls rtc(Src);
	WTString propLine = rtc.ReadTo(bufPos.c_str(), bufPos.GetLength(), ";}");
	propLine.ReplaceAll(" ", "");
	if (propLine.contains("read=") || propLine.contains("write="))
		return false; // already implemented

	return true; // if we came here everything is OK with formatting and property is not already implemented
}
#endif

class BulkImplementer
{
  public:
	BulkImplementer(DType& dt) : mType(dt), mExecutionError(false)
	{
		mFileInvokedFrom = g_currentEdCnt->FileName();
		mFileIdInvokedFrom = gFileIdManager->GetFileId(mFileInvokedFrom);
		mMp = g_currentEdCnt->GetParseDb();
		BuildListOfMethods(dt);
	}

	BOOL CanCreateImplementations()
	{
		return (mMethodDts.size() != 0);
	}

	BOOL CreateImplementations()
	{
		CWaitCursor cur;
		if (!CanCreateImplementations())
			return FALSE;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		DoubleCheckMethodList();
#endif
		SortMethodList();
		cur.Restore();

		if (!mDlgParams.mNodeItems.size())
		{
			::ErrorBox("No methods to implement were identified.", MB_OK | MB_ICONEXCLAMATION);
			return FALSE;
		}

		{
			mDlgParams.mCaption = "Create Implementations";
			mDlgParams.mDirectionsText = "Select methods to implement:";
			mDlgParams.mHelpTopic = "dlgCreateImplementation";
			VAUpdateWindowTitle(VAWindowType::CreateImplementations, mDlgParams.mCaption);

			GenericTreeDlg dlg(mDlgParams);
			const int dlgRet = (int)dlg.DoModal();
			if (IDOK != dlgRet)
			{
				if (-1 == dlgRet)
					::ErrorBox("The dialog failed to be created.", MB_OK | MB_ICONERROR);
				return FALSE;
			}
		}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		SortMethodList(true);
#endif

		const int cnt = ImplementMethodList();
		if (!cnt && !mExecutionError)
		{
			if (gTestLogger)
				gTestLogger->LogStr("MsgBox: No methods implemented.");
			else
				WtMessageBox("No methods implemented.", IDS_APPNAME, MB_OK);
		}
		else if (cnt)
		{
			CString msg;
			CString__FormatA(msg, "%d methods implemented.", cnt);
			SetStatus(msg);
		}

		if (mExecutionError)
			::OnRefactorErrorMsgBox();

		return !mExecutionError;
	}

  private:
	void BuildListOfMethods(DType& dt)
	{
		GenericTreeNodeItem root;
		DTypeList members;

		WTString dtSymScope(dt.SymScope());
		g_pGlobDic->GetMembersList(NULLSTR, dtSymScope, members);
		if (!members.size() || Src == ::GetFileType(mFileInvokedFrom))
		{
			// if invoked in Src file, check localHcbDb even if stuff found in project.
			// cpp file defined class will have GOTO_DEF dtypes in project db (members.size() != 0)
			mMp->LocalHcbDictionary()->GetMembersList(NULLSTR, dtSymScope, members);
		}

		if (!members.size())
			g_currentEdCnt->GetSysDicEd()->GetMembersList(NULLSTR, dtSymScope, members);

		members.GetStrs();
		for (DTypeList::iterator it = members.begin(); it != members.end(); ++it)
		{
			DType& cur = *it;

			// this check prevents bulk implementation in source file when
			// class is defined in header file
			if (cur.FileId() != mFileIdInvokedFrom)
				continue;

			if (!VARefactorCls::CanCreateImplementation(&cur, mType.SymScope()))
			{
				continue;
			}
			else if (Psettings->mUnrealEngineCppSupport)
			{
				// [case: 116596] check for existing postfixed UFunction implementation
				CStringW parameters;
				GetUFunctionParametersForSym(cur.SymScope().c_str(), &parameters);
				// some UFunctions are implemented entirely by the Unreal Engine precompiler
				const bool isPrecompilerImplemented = parameters.Find(L"blueprintimplementableevent") != -1;

				if (isPrecompilerImplemented)
				{
					continue;
				}
				else
				{
					const bool doCreateImpForUFunction = DoCreateImpForUFunction(parameters);
					const bool doCreateValForUFunction = DoCreateValForUFunction(parameters);

					if (doCreateImpForUFunction || doCreateValForUFunction)
					{
						MultiParsePtr pmp = g_currentEdCnt ? g_currentEdCnt->GetParseDb() : nullptr;
						if (!pmp)
							continue;

						DTypeList lst;
						pmp->FindExactList(cur.SymHash(), cur.ScopeHash(), lst);

						if (doCreateImpForUFunction)
							::FindExactList(pmp, cur.SymScope() + "_Implementation", lst, true);

						if (doCreateValForUFunction)
							::FindExactList(pmp, cur.SymScope() + "_Validate", lst, true);

						lst.GetStrs();
						bool impForUFunctionExists = false;
						bool valForUFunctionExists = false;

						for (auto it2 = lst.begin(); it2 != lst.end(); ++it2)
						{
							DType& dt2 = *it2;

							if (dt2.IsImpl())
							{
								if (AreSymbolDefsEquivalent(cur, dt2, true, true))
								{
									if (doCreateImpForUFunction && dt2.Sym().EndsWith("_Implementation"))
										impForUFunctionExists = true;
									else if (doCreateValForUFunction && dt2.Sym().EndsWith("_Validate"))
										valForUFunctionExists = true;
								}
							}
						}

						if (doCreateImpForUFunction)
						{
							if (impForUFunctionExists)
							{
								if (doCreateValForUFunction)
								{
									if (valForUFunctionExists)
										continue;
								}
								else
								{
									continue;
								}
							}
						}
						else if (doCreateValForUFunction && valForUFunctionExists)
						{
							continue;
						}
					}
				}
			}

			WTString def(cur.Def());
			if (def.contains("GetMessageMap()") || def.contains("GetThisMessageMap()"))
			{
				// [case: 68802]
				continue;
			}

			if (def.Find('\f') != -1)
				def = TokenGetField(def.c_str(), "\f");

			def = ::DecodeTemplates(def); // [case: 55727] [case: 57625]
			bool doAdd = true;

			def.ReplaceAll("BEGIN_INTERFACE", "", TRUE);
			def.ReplaceAll("  ", " ");
			def.TrimLeft();
			const int parenPos = def.ReverseFind(')');
			if (-1 != parenPos)
			{
				int pos = def.Find("=0", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				pos = def.Find("= 0", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				pos = def.Find("PURE", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				def.ReplaceAll("{...}", "");
				def.TrimRight();
			}

			// store cleaned up version back into dtype
			cur.SetDef(def);

			// make sure cur.Sym() does not already exist in mMethodDts
			for (DTypeList::iterator it2 = mMethodDts.begin(); it2 != mMethodDts.end(); ++it2)
			{
				if ((*it2).Sym() == cur.Sym())
				{
					// [case: 54587] allow overloads
					if (::AreSymbolDefsEquivalent(cur, *it2, true))
					{
						if ((*it2).Scope() == cur.Scope())
						{
							// identical sym in same scope, don't display at all
							doAdd = false;
							break;
						}
						else
						{
							// don't break, continue to check for same scope
						}
					}
				}
			}

			if (!doAdd)
				continue;

			GenericTreeNodeItem nodeItem;
			mMethodDts.push_back(cur);
			nodeItem.mNodeText = def;
			nodeItem.mIconIndex = ::GetTypeImgIdx(cur.MaskedType(), cur.Attributes());
			nodeItem.mChecked = true;
			nodeItem.mEnabled = true;
			nodeItem.mData = &(mMethodDts.back());
			root.mChildren.push_back(nodeItem);
		}

		if (root.mChildren.size())
		{
			root.mChecked = true;
			root.mEnabled = true;
			root.mData = &dt;
			root.mNodeText = ::StrGetSym(dtSymScope);
			root.mIconIndex = ::GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
			mDlgParams.mNodeItems.push_back(root);
		}
	}

	void SortMethodList(bool reorderForInlineImplementations = false)
	{
		for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin();
		     it != mDlgParams.mNodeItems.end(); ++it)
		{
			// [case: 58301] sort mChildren by DType lineNumber
			std::sort((*it).mChildren.begin(), (*it).mChildren.end(),
			          [reorderForInlineImplementations](const GenericTreeNodeItem& n1,
			                                            const GenericTreeNodeItem& n2) -> bool {
				          // outer 'for' loop assumes single level of child nodes
				          DType* n1dt = static_cast<DType*>(n1.mData);
				          DType* n2dt = static_cast<DType*>(n2.mData);

				          if (reorderForInlineImplementations)
				          {
					// inline implementations have to be inserted last, in
					// reverse order so that line numbers captured before
					// modification remain correct for insert as the class
					// is modified from bottom to top
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
					          // [case: 140658]
					          const bool n1IsProp =
					              n1dt->MaskedType() == PROPERTY && n1dt->Def().Find("__property") == 0;
					          const bool n2IsProp =
					              n2dt->MaskedType() == PROPERTY && n2dt->Def().Find("__property") == 0;
					          if (n1IsProp && n2IsProp)
						          return n2dt->Line() < n1dt->Line(); // properties are sorted in reverse line order
					          else if (n1IsProp)
						          return false; // move property after any other non-property
					          else if (n2IsProp)
						          return true; // leave property after any other non-property
#endif
				          }

				          // by default, sort top to bottom in line order
				          return n1dt->Line() < n2dt->Line();
			          });
		}
	}

	int ImplementMethodList()
	{
		FreezeDisplay _f;
		int cnt = 0;
		UndoContext undoContext("ImplementMethods");
		for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin();
		     it != mDlgParams.mNodeItems.end(); ++it)
		{
			GenericTreeNodeItem& parentNode = *it;
			DType* parentType = static_cast<DType*>(parentNode.mData);

			for (GenericTreeNodeItem::NodeItems::iterator it2 = (*it).mChildren.begin(); it2 != (*it).mChildren.end();
			     ++it2)
			{
				GenericTreeNodeItem& curNode = *it2;
				if (!(curNode.mEnabled && curNode.mChecked))
					continue;

				DType* curType = static_cast<DType*>(curNode.mData);

				DelayFileOpen(mFileInvokedFrom, curType->Line());
				VARefactorCls r(curType->SymScope().c_str(), curType->Line());

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
				if (curType->type() == PROPERTY && curType->Def().Find("__property") == 0)
				{
					// [case: 140658]
					// bulk implementation of properties need extra help
					const WTString curSymType = ::GetTypeFromDef(curType->Def(), Src);
					r.OverrideCurSymType(curSymType);
				}
#endif

				if (r.CreateImplementation(curType, parentType->SymScope(), FALSE))
				{
					if (g_currentEdCnt)
						g_currentEdCnt->GetBuf(TRUE);
					++cnt;
				}
				else
					mExecutionError = true;
			}
		}

		return cnt;
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	void DoubleCheckMethodList()
	{
		for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin();
		     it != mDlgParams.mNodeItems.end(); ++it)
		{
			auto& rootNodeItem = (*it).mChildren;
			rootNodeItem.erase(std::remove_if(rootNodeItem.begin(), rootNodeItem.end(),
			                                  [](const GenericTreeNodeItem& childNodeItem) -> bool {
				                                  DType* dt = static_cast<DType*>(childNodeItem.mData);
				                                  if (dt->MaskedType() == PROPERTY && dt->Def().Find("__property") == 0)
				                                  {
					                                  return !DoImplementPropertyCppB(
					                                      dt); // if it is __property, check if is already implemented
					                                           // and remove it from list if it is
				                                  }

				                                  return false; // default behavior, do not delete from list
			                                  }),
			                   rootNodeItem.end());
		}

		// now delete all root node items that no longer have any elements in it
		mDlgParams.mNodeItems.erase(
		    std::remove_if(mDlgParams.mNodeItems.begin(), mDlgParams.mNodeItems.end(),
		                   [](const GenericTreeNodeItem& nodeItem) -> bool { return nodeItem.mChildren.size() == 0; }),
		    mDlgParams.mNodeItems.end());
	}
#endif

	DType mType;
	CStringW mFileInvokedFrom;
	UINT mFileIdInvokedFrom;
	MultiParsePtr mMp;
	DTypeList mMethodDts;
	GenericTreeDlgParams mDlgParams;
	bool mExecutionError;
};

class BulkMover
{
  public:
	BulkMover(DType& dt) : mType(dt), mExecutionError(false)
	{
		mFileInvokedFrom = g_currentEdCnt->FileName();
		mFileIdInvokedFrom = gFileIdManager->GetFileId(mFileInvokedFrom);
		mMp = g_currentEdCnt->GetParseDb();
		BuildListOfMethods(dt);
	}

	BOOL CanMoveImplementations()
	{
		return (mMethodDts.size() != 0);
	}

	BOOL MoveImplementations()
	{
		CWaitCursor cur;
		if (!CanMoveImplementations())
			return FALSE;

		SortMethodList();
		cur.Restore();

		if (!mDlgParams.mNodeItems.size())
		{
			::ErrorBox("No methods to move were identified.", MB_OK | MB_ICONEXCLAMATION);
			return FALSE;
		}

		{
			mDlgParams.mCaption = "Move Implementations";
			CStringW targetFilePath = ::GetFileByType(mFileInvokedFrom, Src);
			mDlgParams.mDirectionsText = "Select methods to move to " + WTString(Basename(targetFilePath)) + ":";
			mDlgParams.mHelpTopic = "dlgMoveImplementation";
			VAUpdateWindowTitle(VAWindowType::MoveImplementations, mDlgParams.mCaption);

			GenericTreeDlg dlg(mDlgParams);
			const int dlgRet = (int)dlg.DoModal();
			if (IDOK != dlgRet)
			{
				if (-1 == dlgRet)
					::ErrorBox("The dialog failed to be created.", MB_OK | MB_ICONERROR);
				return FALSE;
			}
		}

		const int cnt = MoveMethodList();
		if (!cnt && !mExecutionError)
		{
			if (gTestLogger)
				gTestLogger->LogStr("MsgBox: No methods moved.");
			else
				WtMessageBox("No methods moved.", IDS_APPNAME, MB_OK);
		}
		else if (cnt)
		{
			CString msg;
			CString__FormatA(msg, "%d methods moved.", cnt);
			SetStatus(msg);
		}

		if (mExecutionError)
			::OnRefactorErrorMsgBox();

		return !mExecutionError;
	}

  private:
	void BuildListOfMethods(DType& dt)
	{
		GenericTreeNodeItem root;
		DTypeList members;

		DTypeList lst;
		WTString dtSymScope(dt.SymScope());
		g_pGlobDic->GetMembersList(NULLSTR, dtSymScope, members);
		if (!members.size() || Src == ::GetFileType(mFileInvokedFrom))
		{
			// if invoked in Src file, check localHcbDb even if stuff found in project.
			// cpp file defined class will have GOTO_DEF dtypes in project db (members.size() != 0)
			mMp->LocalHcbDictionary()->GetMembersList(NULLSTR, dtSymScope, members);
		}

		if (!members.size())
			g_currentEdCnt->GetSysDicEd()->GetMembersList(NULLSTR, dtSymScope, members);

		if (members.size() == 0)
			return;

		std::regex defaultSuffix("[=][ \t]*default", std::regex::ECMAScript | std::regex::optimize);

		for (DTypeList::iterator it = members.begin(); it != members.end(); ++it)
		{
			DType& cur = *it;

			if (cur.FileId() != mFileIdInvokedFrom)
				continue;

			if (!VARefactorCls::CanMoveImplementationToSrcFile(&cur, ""))
				continue;

			WTString def(cur.Def());
			if (def.Find('\f') != -1)
				def = TokenGetField(def.Wide(), L"\f");

			def = ::DecodeTemplates(def); // [case: 55727] [case: 57625]

			def.ReplaceAll("BEGIN_INTERFACE", "", TRUE);
			def.ReplaceAll("  ", " ");
			def.TrimLeft();
			const int parenPos = def.ReverseFind(')');
			if (-1 != parenPos)
			{
				int pos = def.Find("=0", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				pos = def.Find("= 0", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				pos = def.Find("PURE", parenPos);
				if (-1 != pos)
					def = def.Left(pos);
				def.ReplaceAll("{...}", "");
				def.TrimRight();
			}

			// store cleaned up version back into dtype
			cur.SetDef(def);

			GenericTreeNodeItem nodeItem;
			mMethodDts.push_back(cur);
			nodeItem.mNodeText = def;
			nodeItem.mIconIndex = ::GetTypeImgIdx(cur.MaskedType(), cur.Attributes());
			// [case: 141280 don't select "=default" items by default
			nodeItem.mChecked = !std::regex_search(def.c_str(), defaultSuffix);
			nodeItem.mEnabled = true;
			nodeItem.mData = &(mMethodDts.back());
			root.mChildren.push_back(nodeItem);
		}

		if (root.mChildren.size())
		{
			root.mChecked = true;
			root.mEnabled = true;
			root.mData = &dt;
			root.mNodeText = ::StrGetSym(dtSymScope);
			root.mIconIndex = ::GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
			mDlgParams.mNodeItems.push_back(root);
		}
	}

	void SortMethodList()
	{
		for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin();
		     it != mDlgParams.mNodeItems.end(); ++it)
		{
			// [case: 58301] sort mChildren by DType lineNumber
			std::sort((*it).mChildren.begin(), (*it).mChildren.end(),
			          [](const GenericTreeNodeItem& n1, const GenericTreeNodeItem& n2) -> bool {
				          // outer 'for' loop assumes single level of child nodes
				          const DType* n1dt = static_cast<DType*>(n1.mData);
				          const DType* n2dt = static_cast<DType*>(n2.mData);
				          return n1dt->Line() < n2dt->Line();
			          });
		}
	}

	int MoveMethodList()
	{
		FreezeDisplay _f;
		int cnt = 0;
		UndoContext undoContext("MoveImplementions");
		for (auto it = mDlgParams.mNodeItems.begin(); it != mDlgParams.mNodeItems.end(); ++it)
		{
			GenericTreeNodeItem& parentNode = *it;
			//			DType *parentType = static_cast<DType*>(parentNode.mData);

			for (auto it2 = parentNode.mChildren.rbegin(); it2 != parentNode.mChildren.rend(); ++it2)
			{
				GenericTreeNodeItem& curNode = *it2;
				if (!(curNode.mEnabled && curNode.mChecked))
					continue;

				DType* curType = static_cast<DType*>(curNode.mData);

				DelayFileOpen(mFileInvokedFrom, curType->Line(), curType->Sym().c_str());
				VARefactorCls r(curType->SymScope().c_str(), curType->Line());
				WTString empty;
				if (r.MoveImplementationToSrcFile(curType, empty,
				                                  FALSE)) // [case: 137996] The call was added in change 20810 with TRUE
				                                          // but at the wrong argument position
				{
					EdCntPtr curEd(g_currentEdCnt);
					if (curEd)
						curEd->GetBuf(TRUE);
					++cnt;
				}
				else
					mExecutionError = true;
			}
		}

		return cnt;
	}

	DType mType;
	CStringW mFileInvokedFrom;
	UINT mFileIdInvokedFrom;
	MultiParsePtr mMp;
	DTypeList mMethodDts;
	GenericTreeDlgParams mDlgParams;
	bool mExecutionError;
};

VARefactorCls::VARefactorCls(LPCSTR symscope /*= NULL*/, int curLine /*= -1*/)
    : mInfo(g_currentEdCnt ? g_currentEdCnt->m_ftype : 0)
{
	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return;

	curEd->Scope(TRUE);

	WTString tmpBuf = curEd->GetBuf();
	mDecLine = mInfo->GetDefFromLine(tmpBuf, ULONG(curLine == -1 ? curEd->CurLine() : curLine));

	mInfo->ParseEnvArgs();
	mDecLine = mInfo->GetLineStr();
	if (symscope)
	{
		// Use passed symscope instead of symbol under caret
		mInfo->mCurSymScope = symscope;
		mInfo->GeneratePropertyName();
		mMethodScope = symscope;
	}
	else
		mMethodScope = curEd->GetSymScope();
	mMethod = ::StrGetSym(mMethodScope);
	mScope = ::StrGetSymScope(mMethodScope);

	mFilePath = curEd->FileName();
	mSelection = curEd->GetSelString();
}

BOOL VARefactorCls::CanRefactor(RefactorFlag flag, WTString* outMenuText) const
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return false;

	try
	{
		WTString invokingScope;
		DTypePtr symBak;
		GetRefactorSym(curEd, symBak, &invokingScope, true);
		DType* sym = symBak.get();

		switch (flag)
		{
		case VARef_FindUsage:
		case VARef_FindUsageInFile:
			return CanFindReferences(sym);
		case VARef_Rename:
			return CanRename(sym);
		case VARef_Rename_References:
			return CanRenameReferences();
		case VARef_ExtractMethod:
			return CanExtractMethod();
		case VARef_CreateMethodImpl:
			return CanCreateImplementation(sym, invokingScope);
		case VARef_CreateMethodDecl:
			return CanCreateDeclaration(sym, invokingScope);
		case VARef_CreateFromUsage:
			return CanCreateMemberFromUsage();
		case VARef_ImplementInterface:
			return CanImplementInterface(outMenuText);
		case VARef_EncapsulateField:
			return CanEncapsulateField(sym);
		case VARef_CreateMethodComment:
			return CanCreateMethodComment();
		case VARef_FindErrorsInFile:
		case VARef_FindErrorsInProject:
			return true;
		case VARef_MoveImplementationToSrcFile:
			return CanMoveImplementationToSrcFile(sym, invokingScope, FALSE, outMenuText);
		case VARef_AddMember:
			return CanAddMember(sym);
		case VARef_AddSimilarMember:
			return CanAddSimilarMember(sym);
		case VARef_ChangeSignature:
			return CanChangeSignature(sym);
		case VARef_ChangeVisibility:
			return CanChangeVisibility(sym);
		case VARef_OverrideMethod:
			return CanOverrideMethod(sym);
		case VARef_AddInclude:
			return CanAddInclude();
		case VARef_ExpandMacro:
			return CanExpandMacro(sym, invokingScope);
		case VARef_SmartSelect:
			return CanSmartSelect();
		case VARef_ModifyExpression:
			return CanSmartSelect(icmdVaCmd_RefactorModifyExpression);
		case VARef_RenameFilesFromRefactorTip:
			return CanRenameFiles(curEd->FileName(), sym, invokingScope, true);
		case VARef_RenameFilesFromMenuCmd:
			return CanRenameFiles(curEd->FileName(), sym, invokingScope, false);
		case VARef_CreateFile:
			return CanCreateFile(curEd);
		case VARef_MoveSelectionToNewFile:
			return CanMoveSelectionToNewFile(curEd);
		case VARef_IntroduceVariable:
			return CanIntroduceVariable(false);
		case VARef_AddRemoveBraces:
			return CanAddRemoveBraces(curEd, outMenuText);
		case VARef_AddBraces:
			return CanAddBraces();
		case VARef_RemoveBraces:
			return CanRemoveBraces();
		case VARef_CreateMissingCases:
			return CanCreateCases();
		case VARef_MoveImplementationToHdrFile:
			return CanMoveImplementationToHdrFile(sym, invokingScope, FALSE, outMenuText);
		case VARef_ConvertBetweenPointerAndInstance:
			return CanConvertInstance(outMenuText);
		case VARef_SimplifyInstance:
			return CanSimplifyInstanceDeclaration();
		case VARef_AddForwardDeclaration:
			return CanAddForwardDeclaration();
		case VARef_ConvertEnum:
			return CanConvertEnum();
		case VARef_MoveClassToNewFile:
			return CanMoveClassToNewFile(curEd, sym, invokingScope);
		case VARef_SortClassMethods:
			return CanSortClassMethods(curEd, sym);
		default:
			return false;
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAR:");
	}

	return false;
}

BOOL VARefactorCls::CanCreateDeclaration(DType* sym, const WTString& invokingScope)
{
	if (!sym || sym->IsEmpty())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	if (ed->m_ftype != Header && ed->m_ftype != Src)
		return FALSE;

	if (DType::IsLocalScope(invokingScope))
		return FALSE;

	if (sym->MaskedType() == FUNC)
	{
		if (sym->IsImpl())
		{
			WTString scope = sym->Scope();
			if (scope.GetLength())
			{
				if (scope == invokingScope)
					return FALSE;
			}

			// check for existing decl
			DTypeList lst;
			MultiParsePtr pmp = ed->GetParseDb();
			pmp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst);
			lst.GetStrs();

			for (auto it = lst.begin(); it != lst.end(); ++it)
			{
				DType& dt = *it;
				if (dt.IsDecl())
				{
					if (AreSymbolDefsEquivalent(*sym, dt, true))
						return FALSE;
				}
			}

			return TRUE;
		}
	}
	return FALSE;
}

BOOL VARefactorCls::CreateDeclaration(DType* sym, const WTString& invokingScope)
{
	TraceScopeExit tse("Create Declaration exit");

	// Create Declaration
	if (!CanCreateDeclaration(sym, invokingScope))
		return FALSE;

	_ASSERTE(g_currentEdCnt);

	UndoContext undoContext("CreateDeclaration");
	CStringW file(::GetFileByType(mFilePath, Header));
	if (mScope.GetLength())
	{
		const CStringW deffile(::FileFromScope(mScope));
		if (deffile.GetLength())
			file = deffile;
	}
	if (!file.GetLength())
		file = ::GetFileByType(mFilePath, Src);

	WTString scopeDeclPosToGoto(mMethodScope);
	FindClassScopePositionViaNamespaceUsings(scopeDeclPosToGoto, file);

	EdCntPtr ptr = nullptr;

	FindSimilarLocation find;
	WTString buf = GetFileText(file);
	int ln = find.WhereToPutDeclaration(g_currentEdCnt->GetBuf(TRUE), g_currentEdCnt->CurPos(), buf);
	if (ln != -1)
		ptr = DelayFileOpen(file, ln);
	if (!ptr && GotoDeclPos(scopeDeclPosToGoto, file))
		ptr = g_currentEdCnt;

	if (ptr)
	{
		const WTString lnBrk(ptr->GetLineBreakString());
		// change foo::bar() to just "foo();\n"
		if (mMethodScope.GetLength() && mMethodScope[0] == DB_SEP_CHR)
			mMethodScope = mMethodScope.Mid(1);

		if (mMethodScope.ReplaceAll(":", "::"))
		{
			WTString scopeToReplace(mMethodScope);
			WTString strippedScope;
			while (!mDecLine.ReplaceAll(scopeToReplace.c_str(), mMethod.c_str()) && -1 != mDecLine.Find("::"))
			{
				// [case: 6593]
				// strip first scope off scopeToReplace
				int pos = scopeToReplace.Find("::");
				if (-1 == pos)
					break;

				// see if removed scope is a namespace
				strippedScope += DB_SEP_STR;
				strippedScope += scopeToReplace.Left(pos);
				strippedScope.ReplaceAll("::", DB_SEP_STR.c_str());
				MultiParsePtr mp = ptr->GetParseDb();
				DType* strippedDat = mp->FindExact(strippedScope.c_str());
				if (!strippedDat || NAMESPACE != strippedDat->type())
					break;

				// remove namespace from scope that will be replaced
				scopeToReplace = scopeToReplace.Mid(pos + 2);
				pos = scopeToReplace.Find("::");
				if (-1 == pos)
					break; // don't repeat
			}
		}

		if (mScope.GetLength())
			mDecLine.prepend("\t");

		// [case: 1738] ctor initialization list
		int pos = mDecLine.Find(')');
		if (-1 != pos)
		{
			pos = mDecLine.Find(":", pos);
			if (-1 != pos && mDecLine[pos + 1] != ':')
			{
				mDecLine = mDecLine.Left(pos);
				mDecLine.TrimRight();
			}
		}

		_ASSERTE(mDecLine.FindOneOf("\r\n") == -1);

		// [case: 81059]
		// find "//" in code and put semicolon ";" before it
		if (Is_C_CS_File(ptr->m_ScopeLangType))
		{
			CommentSkipper cs(ptr->m_ScopeLangType);
			int lastNonWSPos = 0;
			for (int i = 0; i < mDecLine.GetLength() - 1; i++)
			{
				TCHAR c = mDecLine[i];
				TCHAR c2 = mDecLine[i + 1];
				if (cs.IsCode(c))
				{
					if (c == '/' && c2 == '/')
					{
						mDecLine.insert(lastNonWSPos + 1, ";");
						goto semicolon_inserted;
					}
					if (!IsWSorContinuation(c))
						lastNonWSPos = i;
				}
			}
		}
		mDecLine += ";"; // not C or C# type of language OR it is but we didn't find "//" in code
	semicolon_inserted:
		mDecLine += lnBrk;

		if (TERCOL(ptr->CurPos()) > 1)
			mDecLine.prepend(lnBrk.c_str());

		return ::PV_InsertAutotextTemplate(WTString("$end$") + mDecLine, TRUE);
	}
	return FALSE;
}

void VARefactorCls::FindClassScopePositionViaNamespaceUsings(WTString& scopeDeclPosToGoto, CStringW& file)
{
	MultiParsePtr mp = g_currentEdCnt ? g_currentEdCnt->GetParseDb() : nullptr;
	if (::GetFileType(mFilePath) == Src && mp)
	{
		WTString baseScope(mScope);
		WTString baseScopeBcl(mp->GetBaseClassList(baseScope));
		AdjustScopesForNamespaceUsings(mp.get(), baseScopeBcl, baseScope, baseScope);
		if (baseScope != mScope && -1 != baseScope.Find(mScope.c_str()))
		{
			if (-1 != baseScope.Find(::StrGetSymScope(mMethodScope).c_str()))
			{
				// [case: 5277]
				// add namespace to scope that will be used to set insert line
				scopeDeclPosToGoto = baseScope + DB_SEP_STR + ::StrGetSym(mMethodScope);

				if (!baseScope.IsEmpty())
				{
					CStringW deffile(::FileFromScope(mScope));
					if (deffile.IsEmpty())
					{
						// [case: 5315]
						// didn't get a deffile and have defaulted to header file.
						// check to see if we get a deffile with the new scope.
						deffile = ::FileFromScope(baseScope);
						if (deffile.GetLength())
							file = deffile;
					}
				}
			}
		}
	}
}

BOOL VARefactorCls::CanCreateMemberFromUsage()
{
	CreateFromUsage cfu;
	return cfu.CanCreate();
}

BOOL VARefactorCls::CanImplementInterface(WTString* outMenuText)
{
	ImplementMethods im;
	BOOL ok = im.CanImplementMethods();
	if (outMenuText)
		*outMenuText = im.GetCommandText();
	return ok;
}

BOOL VARefactorCls::ImplementInterface()
{
	ImplementMethods im;
	return im.DoImplementMethods();
}

BOOL VARefactorCls::CanCreateMethodComment() const
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	BOOL b = FALSE;
	if (g_currentEdCnt && g_currentEdCnt->GetSymDtypeType() == FUNC)
	{
		const VAScopeInfo* info = mInfo.ConstPtr();
		if (info->InLocalScope())
		{
			// we could be in a ctor init list
			ULONG depth = info->Depth();
			if (depth && info->ConstState(depth - 1).m_lastChar == ':')
				b = TRUE;
		}
		else
		{
			b = TRUE;
		}
	}
	return b;
}

BOOL VARefactorCls::CreateMethodComment()
{
	TraceScopeExit tse("Document Method exit");

	// Create Method Comment
	EdCntPtr ed(g_currentEdCnt);
	if (!CanCreateMethodComment())
		return FALSE;

	_ASSERTE(ed);

	// 	ParseDefStr def(mScope, decLine);
	UndoContext undoContext("CreateMethodComment");

	WTString impText = mInfo->CommentStrFromDef();
	ULONG deep = mInfo->Depth();
	if (deep && mInfo->State(deep).m_parseState == VAParseBase::VPS_ASSIGNMENT &&
	    mInfo->State(deep - 1).m_lastChar == ':')
		deep--; //   Case 1083: dec deep for assignments "Constructor() : var(0){}"
	int p = ptr_sub__int(mInfo->State(deep).m_begLinePos, mInfo->GetBuf());
	ed->SetSel(p, p);
	return PV_InsertAutotextTemplate(impText, FALSE);
}

// static BOOL
// IsDeepInline(const VAScopeInfo * cp)
// {
// 	if (!g_currentEdCnt->m_pmparse->m_isDef &&
// 		!g_currentEdCnt->m_pmparse->m_inClassImplementation &&
// 		g_currentEdCnt->GetSymDtypeType() == FUNC &&
// 		g_currentEdCnt->m_ftype == Header &&
// 		cp->Depth() &&
// 		cp->ConstState().m_defType == FUNC)
// 	{
// 		if (cp->ConstState(cp->Depth()-1).m_defType == CLASS ||
// 			cp->ConstState(cp->Depth()-1).m_defType == STRUCT)
// 			return true;
// 	}
// 	return false;
// }

bool IsFreeFunc(const DType* sym)
{
	if (!sym)
		return false;
	if (sym->type() != FUNC)
		return false;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;

	MultiParsePtr mp(ed->GetParseDb());
	DType* parent = mp->FindExact(sym->Scope().c_str());
	if (!parent)
		return true;
	if (parent->type() == CLASS || parent->type() == STRUCT)
		return false;

	return true;
}

static bool IsExternalClassMethod(const DType* sym)
{
	WTString def(sym->Def());
	WTString s(sym->Sym());

	// ::\s*((operator.*)|SYM)\s*[(]
	std::string rgx = "::\\s*((operator.*)|";
	s.ReplaceAll("+", "[+]"); // [case: 80662]
	s.ReplaceAll("*", "[*]"); // [case: 80663]
	rgx += s.c_str();
	rgx += ")\\s*[(]";

	std::regex r(rgx, std::regex_constants::ECMAScript | std::regex_constants::optimize);
	std::cmatch m;
	if (std::regex_search(def.c_str(), m, r))
	{
		return true;
	}

	return false;
}

// static BOOL
// IsMovableExternalInline(const VAScopeInfo * cp)
// {
// 	if (!g_currentEdCnt->m_pmparse->m_isDef &&
// 		!g_currentEdCnt->m_pmparse->m_inClassImplementation &&
// 		g_currentEdCnt->GetSymDtypeType() == FUNC &&
// 		g_currentEdCnt->m_ftype == Header)
// 	{
// 		if (!(cp->GetParseFlags() & VAParseBase::PF_CONSTRUCTORSCOPE)) // we don't deal with external inline ctors
// 			return true;
// 		ULONG colonPos = cp->ConstState().m_begLinePos - cp->GetBuf() - 1;
// 		if (cp->GetBuf()[colonPos] != ':') // ctor
// 			return !IsDeepInline(cp);
// 	}
// 	return false;
// }

static BOOL IsMovableExternalInline(const DType* sym)
{
	if (g_currentEdCnt && g_currentEdCnt->m_ftype == Header && sym && sym->type() == FUNC && sym->IsImpl() &&
	    IsExternalClassMethod(sym))
	{
		return true;
	}
	return false;
}

// static bool
// IsFreeInline(const VAScopeInfo * cp)
// {
// 	if (g_currentEdCnt->m_pmparse->m_isDef &&
// 		!g_currentEdCnt->m_pmparse->m_inClassImplementation &&
// 		g_currentEdCnt->GetSymDtypeType() == FUNC &&
// 		g_currentEdCnt->m_ftype == Header &&
// 		!cp->Depth() &&
// 		cp->ConstState().m_defType == FUNC &&
// 		cp->CurChar() == '{')
// 	{
// 		return true;
// 	}
// 	return false;
// }

BOOL VARefactorCls::CanMoveImplementationToSrcFile(const DType* sym, const WTString& invokingScope, BOOL force /*= FALSE*/,
                                                   WTString* outMenuText /*= NULL*/)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (ed->m_ftype != Header && ed->m_ftype != Src)
		return FALSE;

	if (force)
		return true;

	if (!sym || sym->IsEmpty())
		return FALSE;

	WTString symName = StrGetSym(sym->SymScope());
	
	if (sym->type() == FUNC)
	{
		if (outMenuText)
			*outMenuText = "&Move Implementation of '" + symName + "' to Source File";

		// 		 if (mInfo.ConstPtr()->CurChar() != '{')
		// 			 return false;
		//
		// 		if (ed->m_pmparse->m_isDef && ed->m_pmparse->m_inClassImplementation)
		// 			return true;
		//
		// 		// check for inline implementation in header
		// 		if (::IsMovableExternalInline(mInfo.ConstPtr()))
		// 			return true;
		//
		// 		// check for inline in class in header that is deep like STDMETHOD(Foo) - bug 1923
		// 		if (::IsDeepInline(mInfo.ConstPtr()))
		// 			return true;
		//
		// 		// [case: 3207] check for free function
		// 		if (::IsFreeInline(mInfo.ConstPtr()))
		// 			return true;

		if (!sym->IsImpl())
			return false;

		if (DType::IsLocalScope(invokingScope))
		{
			// [case: 12800] [case: 58329] invokingScope is a bit hosed for Func in
			// STDMETHOD(Func)();
			// it comes out like :Foo:STDMETHOD-104:
			// hack workaround just for STDMETHOD* macros
			if (-1 == invokingScope.Find("STDMETHOD"))
				return FALSE;
		}

		if (::IsExternalClassMethod(sym))
			return (ed->m_ftype == Header);

		if (::IsFreeFunc(sym))
			return (ed->m_ftype == Header);

		return true;
	}
	else if (sym->type() == CLASS || sym->type() == STRUCT)
	{
		if (outMenuText)
			*outMenuText = "&Move Method Implementations to Source File...";

		DType dt(*sym);
		BulkMover bm(dt);
		return bm.CanMoveImplementations();
	}

	return false;
}

static BOOL UpdateStdMethodImpDecl(EdCnt* ed, VAScopeInfo_MLC& cp)
{
	// [case: 45936]
	// change STDMETHODIMP_(res)Foo(int arg); to STDMETHOD_(res,Foo)(int arg);
	long bp = ptr_sub__int(cp->State().m_begLinePos, cp->GetBuf());
	long selPos1, selPos2;
	// get current position - which is the end of the declaration
	ed->GetSel(selPos1, selPos2);
	_ASSERTE(selPos1 == selPos2);
	// select the entire declaration
	ed->SetSel(bp, selPos1);
	WTString selStr(ed->GetSelString());

	int pos = selStr.find(')');
	if (-1 != pos)
		selStr.SetAt(pos, ',');
	pos = selStr.Find("(", pos);
	if (-1 != pos)
	{
		const WTString begin(selStr.Left(pos));
		const WTString end(selStr.Mid(pos));
		selStr = begin + ")" + end;
	}
	selStr.ReplaceAll("STDMETHODIMP_", "STDMETHOD_");

	// rewrite the declaration
	return ed->InsertW(selStr.Wide(), true, noFormat, false);
}

static BOOL RemoveInlineKeywords(EdCnt* ed, VAScopeInfo_MLC& cp)
{
	// calculate position of start of declaration
	long bp = ptr_sub__int(cp->State().m_begLinePos, cp->GetBuf());
	while (bp && isspace(cp->GetBuf()[(uint)bp - 1]))
		bp--;
	long selPos1, selPos2;
	// get current position - which is the end of the declaration
	ed->GetSel(selPos1, selPos2);
	_ASSERTE(selPos1 == selPos2);
	// select the entire declaration
	ed->SetSel(bp, selPos1);

	OWL::TRegexp exp(" ");
	OWL::string emptyStr;
	// grab the declaration and remove inline keywords
	token selStr(ed->GetSelString());
	LPCTSTR searchExpressions[] = {"__forceinline[ \t\r\n]*",
	                               "__inline[ \t\r\n]*",
	                               "_inline[ \t\r\n]+",
	                               "inline[ \t\r\n]+",
	                               "_AFX_INLINE[ \t\r\n]*",
	                               "AFX_INLINE[ \t\r\n]*",
	                               "_AFX_PUBLIC_INLINE[ \t\r\n]*",
	                               NULL};

	for (int idx = 0; searchExpressions[idx]; ++idx)
	{
		exp = searchExpressions[idx];
		selStr.ReplaceAll(exp, emptyStr);
	}

	// rewrite the declaration
	return ed->Insert(selStr.c_str(), true, noFormat, false);
}

BOOL VARefactorCls::MoveImplementationToHdrFile(const DType* sym, const WTString& invokingScope, BOOL force /*= FALSE*/)
{
	TraceScopeExit tse("Move Implementation to Header exit");
	_ASSERTE(sym || force);

	EdCntPtr ed(g_currentEdCnt);
	int overloads;
	WTString defPath;
	if (!CanMoveImplementationToHdrFile(sym, invokingScope, force, nullptr, &overloads, &defPath))
	{
		vLog("WARN: MoveImplToHdrFile reject 1");
		return FALSE;
	}

	UndoContext undoContext("MoveImplementationToHdrFile");

	int ln = -1;
	CStringW targetFilePath;
	if (defPath.GetLength())
	{ // if there is definition, it will determine the target file both for free functions and methods
		targetFilePath = defPath.Wide();
	}
	else if (!::IsFreeFunc(sym) && mScope.GetLength())
	{ // if there is no definition, use the scope for methods
		const CStringW deffile(::FileFromScope(mScope));
		if (deffile.GetLength())
			targetFilePath = deffile;
	}
	else
	{ // if there is no definition, use the corresponding header for free functions
		targetFilePath = ::GetFileByType(mFilePath, Header);
	}

	int cutSelectionOffset = 0;
	if (targetFilePath.IsEmpty())
	{
		const int fType = ::GetFileType(ed->FileName());
		if (gTestLogger)
		{
			vLog("WARN: MoveImplToHdrFile reject 2");
			gTestLogger->LogStr(
			    WTString("MoveImplementationToHdrFile failed to identify suitable target header file."));
		}
		else
		{
			EdCntPtr ed2(g_currentEdCnt);
			if (ed2 && CanCreateFile(ed2) && Src == fType && !ed2->FileName().CompareNoCase(mFilePath))
			{
				vLog("WARN: MoveImplToHdrFile reject - query CreateFile");
				// [case: 164427] notify user that suitable file was not found and that a new one will be created
				::SetStatus("Failed to identify a target header file for moving the implementation. You can create a new file.");
				
				WTString sourceBufBefore = ed2->GetBuf(TRUE);
				
				if (CreateFile(ed2))
				{
					// [case: 164428] if file is created, pick up the path of that file and continue
					EdCntPtr ed3(g_currentEdCnt);
					if (ed3)
					{
						
						targetFilePath = ed3->FileName();
						ed2->vSetFocus(); // need to set a focus to original Editor for MoveImplementations later
						
						// now calculate offset caused by adding #include into source file
						WTString sourceBufAfter = ed2->GetBuf(TRUE);
						cutSelectionOffset = sourceBufAfter.length() - sourceBufBefore.length();
						if (cutSelectionOffset < 0) // this shouldn't ever happen, but placing a guard regardless
							cutSelectionOffset = 0;
					}
				}

				SetStatus(IDS_READY);
			}
			else
			{
				vLog("WARN: MoveImplToHdrFile reject 2");
				WtMessageBox("Move Implementation to Header File failed to identify a suitable target header file.",
				             IDS_APPNAME, MB_OK | MB_ICONSTOP);
			}
		}
		
		// [case: 164428] if targetFilePath is still empty return FALSE as before, otherwise continue
		if (targetFilePath.IsEmpty())
			return FALSE;
	}

	WTString symScopeToGoto = mMethodScope /*sym->SymScope()*/;
	FindClassScopePositionViaNamespaceUsings(symScopeToGoto, targetFilePath);

	const WTString targetBuf(GetFileText(targetFilePath));
	WTString sourceBuf;
	uint curPos = 0;
	if (overloads == 0)
	{
		curPos = ed->CurPos();
		sourceBuf = ed->GetBuf(TRUE);
	}

	bool isExplicitlyDefaulted = IsExplicitlyDefaulted(sym->Def());
	if (!CutImplementation(true, isExplicitlyDefaulted,
	                       &mInfo->Comment,
	                       cutSelectionOffset)) // with Move Implementation to Header File, everything is an external
	                                            // inline (talking about the bool argument)
		return FALSE;

	if (overloads == 0)
	{
		FindSimilarLocation find;
		ln = find.WhereToPutDeclaration(sourceBuf, curPos, targetBuf);
	}

	EdCntPtr curEd = nullptr;
	if (ln != -1)
		curEd = DelayFileOpen(targetFilePath, ln);

	if (!curEd)
	{
		if (::GotoDeclPos(symScopeToGoto, targetFilePath))
			curEd = g_currentEdCnt;

		if (!curEd)
			return false;
	}

	std::pair<ULONG, ULONG> boundaries;
	long begPos = -1;
	if (overloads > 0)
	{
		WTString fileText(curEd->GetBuf(TRUE));
		MultiParsePtr mp = curEd->GetParseDb();
		LineMarkers markers; // outline data
		GetFileOutline(fileText, markers, mp);
		// 		WTString symScope = sym->SymScope();
		//
		// 		LineMarkerPath pathForUserLine; // path to caret
		// 		markers.CreateMarkerPath((ULONG)curEd->CurLine(), pathForUserLine, false, true);
		// 		if (pathForUserLine.empty()) {
		// 			OnRefactorErrorMsgBox();
		// 			return FALSE;
		// 		}

		WTString name = ::TokenGetField(::StrGetSym(sym->SymScope()), "(");
		name.Trim();
		int curLine = curEd->CurLine();
		if (GetSelBoundaries(markers.Root(), (ULONG)curLine, name, boundaries))
		{
			begPos = (long)boundaries.first;
			curEd->SetSel((int)boundaries.second, (int)boundaries.first);
		}
		else
		{
			curLine--; // workaround: curEd->CurLine() returns different curLine
			// depending on whether the function declaration has EOL or not, so we
			// can't find the line in outline. The file buffer will contain EOL in both cases.
			if (GetSelBoundaries(markers.Root(), (ULONG)curLine, name, boundaries))
			{
				begPos = (long)boundaries.first;
				curEd->SetSel((int)boundaries.second, (int)boundaries.first);
			}
			else
			{
				static const char skMoveErrorMessage[] =
				    "Visual Assist failed to update the header file.  The refactoring did not complete successfully.\n"
				    "Inspect your files to see if changes need to be rolled back.\n";

				OnRefactorErrorMsgBox(skMoveErrorMessage);
				return FALSE;
			}
		}
		// curEd->SetSel(pathForUserLine.back().mEndCp, pathForUserLine.back().mStartCp);

		mInfo->mAlterDef = curEd->GetSelString();
		CommentSkipper cs(curEd->m_ftype);
		for (int i = mInfo->mAlterDef.GetLength() - 1; i >= 0; i--)
		{
			TCHAR c = mInfo->mAlterDef[i];
			if (cs.IsCodeBackward(mInfo->mAlterDef, i))
			{
				if (c == ';')
				{
					mInfo->mAlterDef.ReplaceAt(i, 1, "");
					break;
				}
				if (!IsWSorContinuation(c) && c != '/')
					break;
			}
		}
		mInfo->mAlterDef.Trim();
	}
	else
	{
		mInfo->mAlterDef = "";
	}

	// even if it really is a template, don't special case it unless keeping impl in header
	if (!mInfo->InsertImplementation(true, targetFilePath, targetBuf, true, begPos, UnrealPostfixType::None,
	                                 isExplicitlyDefaulted))
	{
		OnRefactorErrorMsgBox();
		return FALSE;
	}

	return TRUE;
}

BOOL VARefactorCls::MoveImplementationToSrcFile(const DType* sym, const WTString& invokingScope, BOOL force /*= FALSE*/)
{
	TraceScopeExit tse("Move Implementation exit");
	_ASSERTE(sym || force);

	EdCntPtr ed(g_currentEdCnt);
	if (!CanMoveImplementationToSrcFile(sym, invokingScope, force))
	{
		vLog("WARN: MoveImplToSrcFile reject 1");
		return FALSE;
	}

	const BOOL isExternalInline = ::IsMovableExternalInline(sym);
	bool isExternalTemplateInHeader = false;
	const int fType = ::GetFileType(ed->FileName());
	// [case: 30663 / case: 68389]
	// wait until command is actually executed to do this check rather than
	// during every QueryStatus
	CStringW srcFilePath(::GetFileByType(mFilePath, Src));
	if (Header == fType && !isExternalInline)
	{
		if (mInfo->GetTemplateStr().GetLength() > 0)
		{
			if (Psettings->mTemplateMoveToSourceInTwoSteps)
			{
				// [case: 67727]
				// template stays in same file if was inline in class definition
				// but not if external inline (outside of class definition)
				isExternalTemplateInHeader = true;
				srcFilePath = mFilePath;
			}
		}
	}

	if (srcFilePath.IsEmpty())
	{
		if (gTestLogger)
		{
			vLog("WARN: MoveImplToSrcFile reject 2");
			gTestLogger->LogStr(
			    WTString("MoveImplementationToSrcFile failed to identify suitable target source file."));
		}
		else
		{
			EdCntPtr ed2(g_currentEdCnt);
			if (ed2 && CanCreateFile(ed2) && Header == fType && !ed2->FileName().CompareNoCase(mFilePath))
			{
				vLog("WARN: MoveImplToSrcFile reject - query CreateFile");
				// [case: 164427] notify user that suitable file was not found and that a new one will be created
				::SetStatus("Failed to identify a target source file for moving the implementation. You can create a new file.");
				
				if(CreateFile(ed2))
				{
					// [case: 164428] if file is created, pick up the path of that file and continue 
					EdCntPtr ed3(g_currentEdCnt);
					if (ed3)
					{
						srcFilePath = ed3->FileName();
						ed2->vSetFocus(); // need to set a focus to original Editor for MoveImplementations later
					}
				}

				SetStatus(IDS_READY);
			}
			else
			{
				vLog("WARN: MoveImplToSrcFile reject 2");
				WtMessageBox("Move Implementation to Source File failed to identify a suitable target source file.",
				             IDS_APPNAME, MB_OK | MB_ICONSTOP);
			}
		}
		
		// [case: 164428] if srcFilePath is still empty return FALSE as before, otherwise continue
		if (srcFilePath.IsEmpty())
			return FALSE;
	}

	if (Src == fType && ::IsCfile(srcFilePath))
	{
		// [case: 63161] don't allow move to source in C files
		vLog("WARN: MoveImplToSrcFile reject 3");
		return FALSE;
	}

	if (sym && (sym->MaskedType() == CLASS || sym->MaskedType() == STRUCT))
	{
		DType dt(*sym);
		BulkMover im(dt);
		return im.MoveImplementations();
	}

	UndoContext undoContext("MoveImplementationToSrcFile");
	bool isExplicitlyDefaulted = IsExplicitlyDefaulted(sym->Def());
	if (!CutImplementation(isExternalInline, isExplicitlyDefaulted))
		return FALSE;

	// even if it really is a template, don't special case it unless keeping impl in header
	if (!mInfo->CreateImplementation(mMethodScope.c_str(), srcFilePath, isExternalTemplateInHeader, true,
	                                 UnrealPostfixType::None, isExplicitlyDefaulted))
	{
		OnRefactorErrorMsgBox();
		return FALSE;
	}

	return TRUE;
}

// [case: 164428] cutSelectionOffset added for "Move selection to Header file" when move selection
// creates new file; needed in order to recalculate change in the cut locations after insertion of #include
// statement; can be used for other purposes if needed in the future
BOOL VARefactorCls::CutImplementation(const BOOL isExternalInline, const BOOL isExplicitlyDefaulted,
                                      WTString* _comment /*= nullptr*/, int cutSelectionOffset /*= 0*/)
{
	WTString dummy;
	WTString& comment = _comment ? *_comment : dummy;

	// This has the side-effect of caching template<> information.
	mInfo->ImplementationStrFromDef(true, false, false);
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
	{
		OnRefactorErrorMsgBox();
		return FALSE;
	}

	if (isExplicitlyDefaulted && !isExternalInline)
	{
		// [case: 118695] erase "= default" from explicitly defaulted declaration
		try
		{
			LPCSTR buf = mInfo->GetBuf();
			int end = mInfo->GetCp(); // the current position will be at the semicolon
			int start = end;

			do
			{
				// walk backwards to the "="
				if (start > 0)
					--start;
				else
					throw;
			} while (buf[(uint)start] != '=');

			do
			{
				// walk back any further whitespace
				if (start > 0)
					--start;
				else
					throw;
			} while (wt_isspace(buf[(uint)start]));

			start++;

			// remove it
			ed->SetSel(start, end);
			ed->Insert("");
		}
		catch (...)
		{
			OnRefactorErrorMsgBox();
			return FALSE;
		}
	}
	else if (mInfo->CurChar() == '{' || (isExplicitlyDefaulted && mInfo->CurChar() == ';'))
	{
		LPCSTR buf = mInfo->GetBuf();
		int blockStartPos = mInfo->GetCp();
		int kEndPos = 0;
		WTString blockStr;
		int bp = blockStartPos;
		while (bp && isspace(buf[bp - 1]))
			bp--;

		if (isExplicitlyDefaulted)
		{
			// [case: 118695] explicitly defaulted implementation, no body
			kEndPos = blockStartPos + 1; // include the ";"
		}
		else
		{
			ParseToEndBlock eblk(mInfo->FileType(), buf, mInfo->GetBufLen(), blockStartPos);
			kEndPos = eblk.GetEndPos() + 1;

			// get implementation body
			ed->SetSel(bp + cutSelectionOffset, kEndPos + cutSelectionOffset);
			blockStr = ed->GetSelString();

			// Constructor member initializers need to get moved as well
			// Foo::Foo() : mPtr(NULL) {}
			// Pretend they are method qualifiers ala 'const';
			if (mInfo->GetParseFlags() & VAParseBase::PF_CONSTRUCTORSCOPE)
			{
				int colonPos = ptr_sub__int(mInfo->State().m_begLinePos, buf) - 1;
				if (buf[colonPos] == ':')
				{
					while (colonPos && isspace(buf[colonPos - 1]))
						colonPos--;

					// [case: 79737] check for C++11 ctor initializer list
					const int finalStartPos = eblk.GetFinalStartPos();
					if (finalStartPos != blockStartPos)
					{
						bp = blockStartPos = finalStartPos;

						while (bp && isspace(buf[bp - 1]))
							bp--;
						ed->SetSel(bp + cutSelectionOffset, kEndPos + cutSelectionOffset);
						blockStr = ed->GetSelString();
					}

					WTString methodQualifier = WTString(&buf[colonPos], blockStartPos - colonPos);
					ed->SetSel(colonPos + cutSelectionOffset, kEndPos + cutSelectionOffset);
					methodQualifier.Trim();
					mInfo->SetMethodQualifier(methodQualifier);
					bp = colonPos;
				}
			}
		}

		// if is external inline, select the whole thing so it all gets deleted
		if (isExternalInline)
		{
			auto state = mInfo->State();

			// fix for: the whole thing is not selected with constructor initializers. see ASTs MoveImplToHdr06 and
			// MoveImplConstructorCut.
			const WTString& methodQualifier = mInfo->GetMethodQualifier();
			if (methodQualifier.GetLength() && methodQualifier[0] == ':' && mInfo->Depth() > 0)
				state = mInfo->State(mInfo->Depth() - 1);

			bp = ptr_sub__int(state.m_begLinePos, buf);
			CommentSkipper cs(ed->m_ftype);
			int bpBefore = bp;
			while (bp && (cs.IsCommentBackward(buf, bp - 1) || cs.GetState() == CommentSkipper::COMMENT_MAY_START ||
			              isspace(buf[bp - 1])))
				bp--;
			
			if (bp > 0) // [case: 164495] bp will be 0 in scenario when code for cut starts on first line; don't recalculate bp when 0
			{
				int bufLen = mInfo->GetBufLen();
				while (bp < bufLen && buf[bp] != '\r' &&
				       buf[bp] != '\n') // do not select and copy comments when there is code before the comment. see AST
				                        // MoveImplToHdr35
					bp++;
			}

			ed->SetSel(bpBefore + cutSelectionOffset, bp + cutSelectionOffset);
			comment = ed->GetSelString();
			comment.TrimLeft();
			comment.TrimRight();
			if (!comment.IsEmpty())
				comment += "\r\n";
			if (gTestsActive)
			{
				if (comment.Find("VAAutoTest:") != -1)
				{ // we do not copy AST comment blocks
					comment = "";
					bp = bpBefore;
					while (bp && isspace(buf[bp - 1]))
						bp--;
				}
			}
			ed->SetSel(bp + cutSelectionOffset, kEndPos + cutSelectionOffset);
		}

		if (isExplicitlyDefaulted)
		{
			// [case: 118695] erase the explicitly defaulted body
			/*BOOL rslt =*/ed->Insert("");
		}
		else if (blockStr.GetLength())
		{
			blockStr.Trim();
			if (blockStr.GetLength() >= 2) // strip {}
			{
				blockStr = blockStr.Mid(1, blockStr.GetLength() - 2);
				blockStr.TrimRight();
				// trim line chars, not space and tab
				while (blockStr.GetLength() > 1 && strchr("\r\n", blockStr[0]))
					blockStr = blockStr.Mid(1);
			}
			mInfo->SetMethodBody(blockStr);

			BOOL rslt;
			if (isExternalInline)
				rslt = ed->Insert(""); // blow away all of the external inline
			else
			{
				rslt = ed->Insert(""); // erase body

				// find last char before trailing line comment
				int tmpPos = bp;
				long semiPos = tmpPos;
				while (tmpPos > 0 && buf[tmpPos - 1] != '\n' && buf[tmpPos - 1] != '\r') // until start of line
				{
					tmpPos--;
					if (buf[tmpPos] == '/' && buf[tmpPos + 1] == '/')
					{
						while (tmpPos > 0 && isspace(buf[tmpPos - 1]))
							tmpPos--;
						semiPos = tmpPos;
					}
				}
				ed->SetSel(semiPos + cutSelectionOffset, semiPos + cutSelectionOffset);
				rslt = ed->Insert(";"); // convert to declaration
			}
			if (!rslt)
			{
				OnRefactorErrorMsgBox();
				return FALSE;
			}

			if (!isExternalInline)
			{ // if it is external inline, the whole body is already removed. added VAAutoTest:MoveImplSTDMETHODIMP_
				const WTString def(mInfo->CurSymDef());
				if (-1 != def.Find("STDMETHODIMP_"))
				{
					if (!::UpdateStdMethodImpDecl(ed.get(), mInfo))
					{
						OnRefactorErrorMsgBox();
						return FALSE;
					}
				}
			}
		}

		// remove inline keywords - after the body is blown away
		if (mDecLine.contains("__forceinline ") || mDecLine.contains("inline ") || mDecLine.contains("__inline ") ||
		    mDecLine.contains("_inline ") || mDecLine.contains("AFX_INLINE ") || mDecLine.contains("_AFX_INLINE ") ||
		    mDecLine.contains("_AFX_PUBLIC_INLINE "))
		{
			if (!isExternalInline)
			{
				// RemoveInlineKeywords works on the assumption that the
				// implementation and declaration are the same.  Don't call
				// if moving external implementation until it can handle
				// independent declaration and implementation.
				if (!::RemoveInlineKeywords(ed.get(), mInfo))
				{
					OnRefactorErrorMsgBox();
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}

BOOL VARefactorCls::CanCreateImplementation(DType* sym, const WTString& invokingScope, WTString* outMenuText /*= NULL*/)
{
	if (!sym || sym->IsEmpty())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	if (ed->m_ftype != Header && ed->m_ftype != Src)
		return FALSE;

	if (DType::IsLocalScope(invokingScope))
	{
		// [case: 12800] [case: 58329] invokingScope is a bit hosed for Func in
		// STDMETHOD(Func)();
		// it comes out like :Foo:STDMETHOD-104:
		// hack workaround just for STDMETHOD* macros
		if (-1 == invokingScope.Find("STDMETHOD"))
			return FALSE;
	}

	WTString symName = StrGetSym(sym->SymScope());

	if (sym->MaskedType() == FUNC)
	{
		if (outMenuText)
			*outMenuText = "Create &Implementation for '" + symName + "'";

		if (sym->IsDecl())
		{
			// check for existing impl
			DTypeList lst;
			MultiParsePtr pmp = ed->GetParseDb();
			pmp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst);
			lst.GetStrs();

			for (auto it = lst.begin(); it != lst.end(); ++it)
			{
				DType& dt = *it;
				if (dt.IsImpl())
				{
					if (AreSymbolDefsEquivalent(*sym, dt, true))
						return FALSE;
				}
				else if (VAR == dt.MaskedType())
				{
					// [case: 80653]
					return FALSE;
				}
			}

			// [case: 78759]
			const WTString symDef(sym->Def());
			if (-1 != symDef.Find("=default") || -1 != symDef.Find("= default") || -1 != symDef.Find("=delete") ||
			    -1 != symDef.Find("= delete"))
			{
				return FALSE;
			}

			return TRUE;
		}
	}
	else if (sym->MaskedType() == VAR)
	{
		if (outMenuText)
			*outMenuText = "Create &Implementation for '" + symName + "'";

		// check if static
		WTString symDef = sym->Def();
		if (strstrWholeWord(symDef, "static") || strstrWholeWord(symDef, "thread_local"))
		{
			// additional check if already defined or invalid
			if (symDef.Find("=") >= 0 || strstrWholeWord(symDef, "constexpr") || strstrWholeWord(symDef, "consteval") || strstrWholeWord(symDef, "constinit"))
				return FALSE;

			// check for existing impl
			DTypeList lst;
			MultiParsePtr pmp = ed->GetParseDb();
			pmp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst);
			lst.GetStrs();

			for (auto it = lst.begin(); it != lst.end(); ++it)
			{
				DType& dt = *it;
				if (dt.MaskedType() == GOTODEF || dt.MaskedType() == FUNC)
				{
					if (AreSymbolDefsEquivalent(*sym, dt, true))
						return FALSE;
				}
			}

			return TRUE;
		}
	}
	else if (sym->MaskedType() == CLASS || sym->MaskedType() == STRUCT)
	{
		if (outMenuText)
			*outMenuText = "Create Method &Implementations...";

		BulkImplementer im(*sym);
		return im.CanCreateImplementations();
	}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	else if (sym->MaskedType() == PROPERTY)
	{
		if (outMenuText)
			*outMenuText = "Create &Implementation";

		// check if __property
		if (sym->Def().Find("__property") == 0)
			return TRUE;
	}
#endif

	return FALSE;
}

// [case: 111093] return true if an _Implementation postfixed method should be implemented for the UFunction
bool DoCreateImpForUFunction(const CStringW& parameters)
{

	if (strstrWholeWord(parameters.GetString(), L"client"))
		return true;
	else if (strstrWholeWord(parameters.GetString(), L"server"))
		return true;
	else if (strstrWholeWord(parameters.GetString(), L"netmulticast"))
		return true;
	else if (strstrWholeWord(parameters.GetString(), L"blueprintnativeevent"))
		return true;
	else
		return false;
}

// [case: 111093] return true if an _Validate postfixed method should be implemented for the UFunction
bool DoCreateValForUFunction(const CStringW& parameters)
{
	if (strstrWholeWord(parameters.GetString(), L"withvalidation"))
		return true;
	else
		return false;
}

template <class T>
using InclSet = std::unordered_set<T>; // #TODO_FLAT: try with flat_set

template <class T1, class T2>
using InclMap = std::unordered_map<T1, T2>; // #TODO_FLAT: try with flat_map

using KindSymVecMap = InclMap<std::string, std::vector<std::string>>;	// <kind, <symbols>>
using NsKindSymVecMap = InclMap<std::string, KindSymVecMap>;			// <namespace, <kind, <symbols>>>
using FileNsKindSymVecMap = InclMap<std::string, NsKindSymVecMap>;		// <file, <namespace, <kind, <symbols>>>>

// hierarchy: <file, <namespace, <kind, <symbols>>>> 
static FileNsKindSymVecMap includes;
static CCriticalSection includes_cs;

void LoadIncludesFromJSON(FileNsKindSymVecMap& includes_map, const CStringW& json_file, bool force = false)
{
	if (!includes_map.empty() && !force)
		return;

	includes_map.clear();

	namespace rjs = RAPIDJSON_NAMESPACE;
	rjs::Document jsDoc;

	WTString text;
	text.ReadFile(json_file);

	_ASSERT(!text.IsEmpty());
	if (text.IsEmpty())
		return;

	jsDoc.Parse<rjs::ParseFlag::kParseCommentsFlag | rjs::ParseFlag::kParseTrailingCommasFlag>(text.c_str());

	_ASSERT(!jsDoc.HasParseError());
	_ASSERT(jsDoc.IsArray());

	if (!jsDoc.HasParseError() && jsDoc.IsArray())
	{
		InclMap<std::string, std::vector<std::string>> symbols_map;
		for (rjs::Value::ConstValueIterator hdr_it = jsDoc.Begin(); hdr_it != jsDoc.End(); ++hdr_it)
		{
			const rjs::Value& header = *hdr_it;

			_ASSERT(header.HasMember("symbols"));
			_ASSERT(header.HasMember("filename"));
			_ASSERT(header.HasMember("namespace"));

			if (header.HasMember("symbols") && 
				header.HasMember("filename") && 
				header.HasMember("namespace"))
			{
				symbols_map.clear();
				auto file_name = header["filename"].GetString();	// taken here for better debugging
				auto ns_name = header["namespace"].GetString();		// taken here for better debugging

				const rjs::Value& symbols = header["symbols"];
				for (auto sym_it = symbols.MemberBegin(); sym_it != symbols.MemberEnd(); ++sym_it)
				{
					const auto& sym_arr = *sym_it;
					auto name = sym_arr.name.GetString();

					_ASSERT(name && *name);
					_ASSERT(sym_arr.value.IsArray());

					std::vector<std::string> sym_vec;
					if (sym_arr.value.IsArray())
					{
						for (rjs::Value::ConstValueIterator val_it = sym_arr.value.Begin(); val_it != sym_arr.value.End(); ++val_it)
						{
							if (val_it->IsString())
							{
								sym_vec.emplace_back(val_it->GetString());
							}
						}
					}

					//_ASSERT(!sym_vec.empty());

					if (!sym_vec.empty())
					{
						symbols_map[name] = sym_vec;
					}
				}

				_ASSERT(!symbols_map.empty());
				if (!symbols_map.empty())
				{
					auto & file = includes_map[file_name];
					for (auto& rec : symbols_map)
					{
						for (auto& scope : rec.second)
						{
							auto sym = ::StrGetSym(scope.c_str());
							if (sym == scope.c_str())
								file[ns_name][rec.first].emplace_back(scope);
							else
							{
								std::string ns = ns_name;
								ns += DB_SEP_CHR;
								ns += scope.substr(0, sym - scope.c_str() - (size_t)1);
								file[ns][rec.first].emplace_back(sym);
							}
						}
					}
				}
			}
		}
	}
}


bool CanFindHeaderInTable(const WTString& sym_scope)
{
	if (includes.empty())
	{
		CStringW jsonFile;
		jsonFile = VaDirs::GetDllDir() + L"misc\\includes.json";
		__lock(includes_cs);
		LoadIncludesFromJSON(includes, jsonFile);
	}

	_ASSERT(!includes.empty());
	if (includes.empty())
		return false;

	auto pSym = ::StrGetSym(sym_scope.c_str());
	if (pSym == sym_scope.c_str())
		return true;

	WTString ns = sym_scope.Left((int)(pSym - sym_scope.c_str()) - 1);

	if (ns.IsEmpty() ||	ns.Compare(DB_SEP_STR) == 0)
		return false;

	ns.TrimLeftChar(DB_SEP_CHR);

	std::string strns(ns.c_str());
	for (auto& kvp : includes)
		if (kvp.second.contains(strns))
			return true;

	return false;
}

LPCSTR FindHeaderInTable(const WTString& sym_scope)
{
	if (includes.empty())
	{
		CStringW jsonFile;
		jsonFile = VaDirs::GetDllDir() + L"misc\\includes.json";
		__lock(includes_cs);
		LoadIncludesFromJSON(includes, jsonFile);
	}

	_ASSERT(!includes.empty());
	if (includes.empty())
		return nullptr;

	static LPCSTR kind_match[] = {
	    "classes",
	    "types",
	    "typedefs",
	    "objects",
	    "enumerations",
	    "declarations",
	    "macros"
	};

	static LPCSTR kind_substr[] = {
		"classes", "class", 
		"typedefs", "types", "type", 
		"objects", "object", 
		"declarations",
	    "enum",
		"macro", 
		"decl", 
		"func"
	};

	auto get_kind = [](LPCSTR str) {
		int kind_idx = 0;

		for (auto x : kind_match)
		{
			if (::StrCmp(str, x))
				return kind_idx;

			kind_idx++;
		}

		for (auto x : kind_substr)
		{
			if (::StrStr(str, x))
				return kind_idx;

			kind_idx++;
		}

		return kind_idx;
	};

	WTString ns = ::StrGetOuterScope(sym_scope);
	ns.TrimLeftChar(DB_SEP_CHR);

	WTString scope(sym_scope);
	scope.TrimLeftChar(DB_SEP_CHR);
	
	WTString symbol;
	auto sym = ::StrGetSym(scope);
	if (sym != scope.c_str())
		symbol = sym;

	WTString trimmed_scope;
	auto start = ns + DB_SEP_STR;
	if (strncmp(scope.c_str(), start.c_str(),(size_t)start.length()) == 0)
		trimmed_scope = scope.substr(start.length());

	const std::string* best_hdr = nullptr;
	int best_kind = INT_MAX;

	const std::string* best_sym_hdr = nullptr;
	int best_sym_kind = INT_MAX;

	for (const auto& file_nssym : includes)
	{
		for (const auto& ns_syms : file_nssym.second)
		{
			if (ns_syms.first.c_str() != ns)
				continue;

			for (const auto& kvp : ns_syms.second)
			{
				if (kvp.first == "includes")
					continue;

				for (const auto& cls : kvp.second)
				{
					if (scope.Compare(cls.c_str()) == 0 ||
					    trimmed_scope.Compare(cls.c_str()) == 0)
					{
						int kind_idx = get_kind(kvp.first.c_str());

						if (kind_idx < best_kind)
						{
							best_kind = kind_idx;
							best_hdr = &file_nssym.first;
						}

						break;
					}

					if (!best_hdr)
					{
						auto symPtr = ::StrGetSym(cls.c_str());
						if (symbol.Compare(symPtr) == 0)
						{
							int kind_idx = get_kind(kvp.first.c_str());

							if (kind_idx < best_sym_kind)
							{
								best_sym_kind = kind_idx;
								best_sym_hdr = &file_nssym.first;
							}
						}
					}
				}
			}
		}
	}

	if (best_hdr)
		return best_hdr->c_str();
	
	if (best_sym_hdr)
		return best_sym_hdr->c_str();

	return nullptr;
}

bool IsEditorProbablyCppFile(EdCnt* ed)
{
	if (!ed || !(ed->m_ftype == Src || ed->m_ftype == Header))
		return false;

	// compares extensions passed as nullptr terminated array 
	auto cmpExt = [](const CStringW& fileName, LPCWSTR const *exts) {
		int pos = fileName.ReverseFind('.');
		if (pos >= 0)
		{
			for (; exts && *exts; exts++)
				if (_wcsicmp((LPCWSTR)fileName + pos, *exts) == 0)
					return true;
		}
		return false;
	};

	// set of files where we include <cname> form of header by default
	LPCWSTR const cppFiles[] = {
		L".hpp", L".hh", L".tlh", L".hxx", L".hp", 
		L".cpp", L".cc", L".tli", L".cxx", L".cp", 
		L".c++", L".cppm", L".ixx", L".inl",
		nullptr
	};
	
	// the only file that is definitely a C file is ".c"
	// note that UNIX uses (upper case) ".C" for c++
	// we don't respect this as we are in Visual Studio
	// see: https://stackoverflow.com/questions/1545080/c-code-file-extension-what-is-the-difference-between-cc-and-cpp
	LPCWSTR const cFiles[] = { L".c", nullptr};

	CStringW filename = ed->FileName();

	// if the file is certainly a CPP file return early
	if (cmpExt(filename, cppFiles))
		return true;

	// for source file return whether it is not a C file
	if (ed->m_ftype == Src)
		return !cmpExt(filename, cFiles);

	// for any header file check if the file has been included in a C file
	// Note: we only check direct inclusion for better performance... 
	DTypeList incList;
	IncludesDb::GetIncludedBys(filename, DTypeDbScope::dbSolution, incList);
	for (DType& inc : incList)
	{
		uint fid = inc.FileId();
		if (fid)
		{
			filename = gFileIdManager->GetFile(fid);

			// if header is included in C file, it is not C++ file
			// as it has to follow C file rules 
			// (sure #ifdefs could handle this, but let's do it simple)
			if (cmpExt(filename, cFiles))
				return false;
		}
	}

	// header file not directly included in any C file 
	return true;
}

bool IsPreferredStlHeader(const CStringW& filename)
{
	static LPCWSTR stlHeaders[] = {
	    L"vector", L"list", L"algorithm",
	    L"map", L"set", L"queue", L"stack", L"mutex", L"functional",
	    L"tuple", L"array", L"initializer_list", L"iostream", L"iomanip",
	    L"numeric", L"memory", L"string", L"iterator", L"bitset",
	    L"deque", L"limits", L"stdexcept", L"cmath", L"cstddef",
	    L"cstdlib", L"ctime", L"cstring", L"cctype", L"cwctype",
	    L"cwchar", L"clocale", L"climits", L"cfloat", L"new",
	    L"typeinfo", L"exception", L"cassert", L"cstdarg", L"cstdint",
	    L"cinttypes"
	};

	LPCWSTR base = GetBaseName(filename);

	for (LPCWSTR h : stlHeaders)
		if (_wcsicmp(h, base) == 0)
			return true;

	return false;
}

bool FindCppIncludeFile(CStringW& filenameRef, bool isBase /*= false*/)
{
	auto cpp_hdr = isBase ? filenameRef : ::Basename(filenameRef);
	int hdr = cpp_hdr.ReverseFind('.');
	if (hdr >= 0)
	{
		CStringW empty;
		cpp_hdr = L"c" + cpp_hdr.Mid(0, hdr);
		if (::FindFile(cpp_hdr, empty, false, false) >= 0)
		{
			filenameRef = cpp_hdr;
			return true;
		}
	}
	return false;
}

struct FidTree
{
	struct FidNode
	{
		uint fid = 0;
		std::wstring base;
		uint index = UINT_MAX; // lower index = higher priority 

		InclSet<FidNode*> parents;
		InclSet<FidNode*> children;

		bool contains_fid(uint id)
		{
			if (this->fid == id)
				return true;

			for (auto x : children)
				if (x->contains_fid(id))
					return true;

			return false;
		}

		FidNode* top()
		{
			FidNode* min_node = this;

			for (FidNode* node : parents)
			{
				node = node->top();
				if (min_node == this || min_node->index > node->index)
					min_node = node;
			}

			return min_node;
		}

		FidNode(uint _fid, FidNode* _parent)
		    : fid(_fid)
		{
			if (_parent)
				parents.insert(_parent);
		}
	};

	InclMap<uint, std::unique_ptr<FidNode>> fid_map;

	FidNode* find_best(uint fid)
	{
		FidNode* best = nullptr;
		for (const auto & p : fid_map)
			if (p.second->contains_fid(fid))
				if (best == nullptr || p.second->index < best->index)
					best = p.second.get();
		return best;
	}

	FidNode* get_node(uint fid)
	{
		auto it = fid_map.find(fid);

		if (it == fid_map.cend())
			return fid_map.emplace(fid, new FidNode(fid, nullptr)).first->second.get();
		
		return it->second.get();
	}

	FidNode* find(uint fid)
	{
		auto it = fid_map.find(fid);
		if (it != fid_map.cend())
			return it->second.get();
		return nullptr;
	}

	FidNode* insert(FidNode* parent, uint fid_child, bool & inserted)
	{
		auto child = get_node(fid_child);
		inserted = child->parents.insert(parent).second;
		parent->children.insert(child);
		return child;
	}
};

bool FindPreferredInclude(
    CStringW& filenameRef,
    const WTString& symScope,
    MultiParsePtr mp,
    const std::initializer_list<std::wstring>& base_names,
    bool isCpp,
	uint includeDepth /*= 5*/,
    bool samePathIncludesOnly /*= true*/,
    DTypeDbScope dbScope /*= DTypeDbScope::dbSystem*/)
{
	std::vector<std::wstring> bn_vec;
	bn_vec.reserve(base_names.size());

	for (const auto& bn : base_names)
		bn_vec.emplace_back(bn + L".h");

	auto is_less = [](LPCWSTR x, LPCWSTR y) {
		return _wcsicmp(x, y) < 0;
	};

	std::map<LPCWSTR, UINT, decltype(is_less)> bn_map(is_less);
	for (size_t i = 0; i < bn_vec.size(); i++)
		bn_map.emplace(bn_vec[i].c_str(), (UINT)i);

	if (!mp || filenameRef.IsEmpty() || symScope.IsEmpty())
	{
		_ASSERTE(!"Invalid arguments in FindPreferredInclude");
		return false;
	}

	// map of pairs < file, nested header file >
	//std::unordered_map<uint, uint> fidmap;
	FidTree fidTree;
	CCriticalSection critSection;
	InclSet<uint> same_path_files;

	const CStringW base(::Basename(filenameRef));
	CStringW path = filenameRef.Mid(0, filenameRef.GetLength() - base.GetLength());

	if (samePathIncludesOnly)
	{
		FileList files;
		FindFiles(path, L"*.*", files);
		for (auto& fi : files)
		{
			same_path_files.insert(fi.mFileId);
		}
	}

	auto loopFn = [&](uint index) {
		CStringW tmp = filenameRef.Mid(0, filenameRef.GetLength() - base.GetLength()) + bn_vec[index].c_str();
		if (::IsFile(tmp))
		{
			auto set_index = [&](FidTree::FidNode* node) {
				if (node->index != UINT_MAX)
					return;

				auto _fname = gFileIdManager->GetFile(node->fid);
				LPCWSTR _cwstr = _fname;
				LPCWSTR _cbase = ::GetBaseName(_cwstr);
				node->base = _cbase;

				auto it = bn_map.find(_cbase);
				if (it == bn_map.cend())
					node->index = UINT_MAX;
				else
					node->index = it->second;
			};

			// current header from the passed list
			UINT fid = gFileIdManager->GetFileId(tmp);
			FidTree::FidNode* fid_node = nullptr;

			if (fid)
			{
				__lock(critSection);
				fid_node = fidTree.get_node(fid);
				set_index(fid_node);
			}
			else
			{
				return;
			}

			// if nested includes are required, includeDepth is > 0
			if (includeDepth && fid_node)
			{
				std::list<std::tuple<uint, uint>> list; // allows FIFO access
				list.emplace_back(fid, 1);
				uint loops = 0; // prevent from endless loop

				while (!list.empty() && loops++ < 5000)
				{
					uint cur_fid = std::get<0>(list.front());
					uint cur_depth = std::get<1>(list.front());
					list.pop_front();

					// get includes in file
					DTypeList incList;

					IncludesDb::GetIncludes(cur_fid, dbScope, incList);
					for (auto& inc : incList)
					{
						// try to find file ID
						const WTString &curInc(inc.Def()); // FileId() is id of file, not of include

						int idPos = curInc.Find("fileid:");
						if (idPos == -1)
							continue;

						// if file ID exists...
						char* endStr = nullptr;
						const char* startStr = curInc.c_str() + idPos + 7;
						cur_fid = (uint)strtoul(startStr, &endStr, 16); // sscanf is 4x slower
						if (cur_fid && endStr != startStr)
						{
							// in case of only includes from same path are allowed,
							// compare the path and skip adding to list if path differs
							if (samePathIncludesOnly)
							{
								if (!same_path_files.contains(cur_fid))
								{
									continue;
								}
							}

							bool inserted = false;

							{
								__lock(critSection);
								auto node = fidTree.insert(fid_node, cur_fid, inserted);
								set_index(node);
							}

							// only adding unique files
							// cur_fid is nested FID
							// fid is top FID

							if (inserted)
							{
								// add item to list only when we are not too deep
								if (cur_depth < includeDepth)
								{
									list.emplace_back(cur_fid, cur_depth + 1);
								}
							}
						}
					}
				}
			}
		}
	};

#pragma warning(push)
#pragma warning(disable : 4127)
	if (bn_vec.size() > 16 && Psettings->mUsePpl)
		Concurrency::parallel_for((uint)0, (uint)bn_vec.size(), loopFn);
	else
		::serial_for<uint>((uint)0, (uint)bn_vec.size(), loopFn);
#pragma warning(pop)

	// empty map means error
	if (fidTree.fid_map.empty())
		return false;

	// try to find symbol in better file
	bool found = false;
	uint better_fid = 0;
	auto fn = [&](DType* dt, bool checkInvalidSys) {
		// all of this happens in parallel

		if (found || dt->IsVaStdAfx()) // ignore VaStdAfx
			return;

		// no need for lock, we are just reading
		auto it = fidTree.find(dt->FileId());
		if (!found && it)
		{
			// no guessing, only full match
			if (symScope == dt->SymScope())
			{
				__lock(critSection);
				if (!found)
				{
					// we found the symbol in header or its include,
					// stop iteration and assign better_fid to the header
					// which is the source of symbol or includes the source
					auto best_node = fidTree.find_best(dt->FileId());
					if (best_node)
					{
						found = true;
						better_fid = best_node->fid;
					}
				}
			}
		}
	};

	// specialized ForEach with scope
	mp->ForEach(fn, found, dbScope);

	// did we found symbol in the file to be included?
	if (found && better_fid)
	{
		// apply new filename and FID
		filenameRef = gFileIdManager->GetFile(better_fid);

		// [case: 164393]
		if (isCpp)
		{
			FindCppIncludeFile(filenameRef);
		}

		return true;
	}

	return false;
}

BOOL VARefactorCls::CreateImplementation(DType* sym, const WTString& invokingScope, BOOL displayErrorMessage /*= TRUE*/)
{
	TraceScopeExit tse("Create Implementation exit");
	bool doCreateImpForUFunction = false;
	bool doCreateValForUFunction = false;

	if (!CanCreateImplementation(sym, invokingScope))
	{
		return FALSE;
	}
	else if (Psettings->mUnrealEngineCppSupport)
	{
		// [case: 111093] check for existing postfixed UFunction implementation
		CStringW parameters;
		GetUFunctionParametersForSym(sym->SymScope().c_str(), &parameters);
		// some UFunctions are implemented entirely by the Unreal Engine precompiler
		const bool isPrecompilerImplemented = parameters.Find(L"blueprintimplementableevent") != -1;
		doCreateImpForUFunction = DoCreateImpForUFunction(parameters);
		doCreateValForUFunction = DoCreateValForUFunction(parameters);

		if (isPrecompilerImplemented || doCreateImpForUFunction || doCreateValForUFunction)
		{
			MultiParsePtr pmp = g_currentEdCnt->GetParseDb();
			DTypeList lst;
			pmp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst);

			if (doCreateImpForUFunction)
				::FindExactList(pmp, sym->SymScope() + "_Implementation", lst, true);

			if (doCreateValForUFunction)
				::FindExactList(pmp, sym->SymScope() + "_Validate", lst, true);

			lst.GetStrs();

			for (auto it = lst.begin(); it != lst.end(); ++it)
			{
				DType& dt = *it;

				if (dt.IsImpl())
				{
					if (AreSymbolDefsEquivalent(*sym, dt, true, true))
					{
						if (dt.Sym().EndsWith("_Implementation"))
							doCreateImpForUFunction = false;
						else if (dt.Sym().EndsWith("_Validate"))
							doCreateValForUFunction = false;
					}
				}
			}

			if (!doCreateImpForUFunction && !doCreateValForUFunction)
			{
				const WTString alreadyImplementedMsg("The UFunction is already fully implemented.");
				const WTString implementedInBlueprintMsg("This UFunction should be implemented in Blueprint, not C++.");
				const WTString& message = isPrecompilerImplemented ? implementedInBlueprintMsg : alreadyImplementedMsg;

				if (displayErrorMessage)
				{
					if (gTestLogger)
						gTestLogger->LogStr("MsgBox: " + message);
					else
						WtMessageBox(message.c_str(), IDS_APPNAME, MB_OK);
				}

				return FALSE;
			}
		}
	}

	// 	ParseDefStr def(mScope, decLine);
	UndoContext undoContext("CreateImplementation");
	const bool isTemplate = mInfo->GetTemplateStr().GetLength() > 0;
	CStringW srcFilePath(::GetFileByType(mFilePath, Src));
	if (srcFilePath.IsEmpty() && IsCFile(::GetFileType(mFilePath)))
	{
		// [case: 3735] instead of fail, give them something they can cut and paste wherever
		srcFilePath = mFilePath;
	}

	if (sym->MaskedType() == VAR)
	{
		WTString symDef = sym->Def();
		_ASSERTE(strstrWholeWord(symDef, "static") || strstrWholeWord(symDef, "thread_local"));
	}
	else if (sym->MaskedType() == CLASS || sym->MaskedType() == STRUCT)
	{
		BulkImplementer im(*sym);
		return im.CreateImplementations();
	}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	else if (sym->MaskedType() == PROPERTY && sym->Def().Find("__property") == 0)
	{
		// check if already implemented
		if (!DoImplementPropertyCppB(sym))
		{
			const WTString message("The Property is already implemented or the declaration could not be understood.");

			if (displayErrorMessage)
			{
				if (gTestLogger)
					gTestLogger->LogStr("MsgBox: " + message);
				else
					WtMessageBox(message.c_str(), IDS_APPNAME,
					             MB_OK); // property already implemented or line is not formatted properly, inform user
			}

			return FALSE;
		}

		BOOL rsltProp = mInfo->CreateImplementationPropertyCppB(sym, mFilePath);

		if (FALSE == rsltProp && displayErrorMessage)
			OnRefactorErrorMsgBox(); // something is wrong with editing file, raise general refactor MsgBox
		return rsltProp;
	}
#endif

	BOOL rslt = TRUE;

	if (doCreateImpForUFunction || doCreateValForUFunction)
	{
		if (doCreateImpForUFunction)
			rslt = mInfo->CreateImplementation(mMethodScope.c_str(), srcFilePath, isTemplate, true,
			                                   UnrealPostfixType::Implementation, false);

		if (doCreateValForUFunction && rslt == TRUE)
		{
			DelayFileOpen(mFilePath); // reopen the header as some of the code in FindSimilarLocation assumes it
			UnrealPostfixType unrealPostfixType = doCreateImpForUFunction
			                                          ? UnrealPostfixType::ValidateFollowingImplementation
			                                          : UnrealPostfixType::Validate;
			WTString implTemplate =
			    gAutotextMgr->GetSource("Refactor Create Implementation Method Body (Unreal Engine _Validate Method)");
			mInfo->SetMethodBody(implTemplate);
			rslt = mInfo->CreateImplementation(mMethodScope.c_str(), srcFilePath, isTemplate, true, unrealPostfixType,
			                                   false);
		}
	}
	else
	{
		// if this is a template, put impl in file where the template is (so the impl is inline)
		rslt = mInfo->CreateImplementation(mMethodScope.c_str(), isTemplate ? mFilePath : srcFilePath, isTemplate,
		                                   sym->MaskedType() != VAR, UnrealPostfixType::None, false);
	}

	if (FALSE == rslt && displayErrorMessage)
		OnRefactorErrorMsgBox();
	return rslt;
}

BOOL VARefactorCls::CanEncapsulateField(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (sym->Def().Find('[') >= 0) // do not allow encapsulate field on arrays in case of C++ Builder
		return FALSE;
#endif

	// Allow encapsulate on classes defined in src files
	return sym->MaskedType() == VAR && sym->infile() && StrGetSymScope(sym->SymScope()).GetLength() > 1 &&
	       !sym->HasLocalScope();
}

BOOL VARefactorCls::Encapsulate(DType* sym)
{
	TraceScopeExit tse("Encapsulate Field exit");
	if (!CanEncapsulateField(sym))
		return FALSE;

	ULONG depth = mInfo->Depth();
	bool inNS = false;
	for (ULONG i = 0; i < depth; i++)
	{
		auto state = mInfo->State(i);
		ULONG defType = state.m_defType;
		if (defType == NAMESPACE)
			inNS = true;
		else if (defType == CLASS || defType == STRUCT)
			inNS = false;
	}

	if (inNS)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"Encapsulate field error: triggered inside a namespace");
		else
			WtMessageBox("Encapsulate Field cannot be triggered inside a namespace", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);

		return FALSE;
	}

	EncapsulateField ef;
	return ef.Encapsulate(sym, mInfo, mMethodScope, mFilePath);
}

BOOL VARefactorCls::CanExtractMethod() const
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
#ifndef _DEBUG
	if (ed && Is_VB_VBS_File(ed->m_ftype))
		return FALSE; // Case 4089:   Extract Method broken in VB
#endif                // _DEBUG
	if (ed)
	{
		int lang = ed->m_ScopeLangType;
		if (Is_HTML_JS_VBS_File(lang))
			return FALSE;
	}
	return ed && mSelection.GetLength() != 0 /*&& mFileHdr.GetLength()*/;
}

BOOL VARefactorCls::ExtractMethod()
{
	TraceScopeExit tse("Extract Method exit");

	// Create Method From Selection
	if (!CanExtractMethod())
		return FALSE;
	MethodExtractor em(mInfo->FileType());
	const CStringW hdrFilePath(::GetFileByType(mFilePath, Header));
	if (hdrFilePath.GetLength())
		return em.ExtractMethod(hdrFilePath);
	else
		return em.ExtractMethod(mFilePath);
}

BOOL VARefactorCls::PromoteLambda()
{
	TraceScopeExit tse("Promote Lambda exit");
	if (!CanPromoteLambda())
		return FALSE;
	_ASSERTE(g_currentEdCnt);

	// Get the symbol name at the current caret position
	WTString symbolName = StrGetSym(g_currentEdCnt->GetSymScope());

	LambdaPromoter lp(mInfo->FileType(), symbolName);
	const CStringW hdrFilePath(::GetFileByType(mFilePath, Header));
	if (hdrFilePath.GetLength())
		return lp.PromoteLambda(hdrFilePath);
	else
		return lp.PromoteLambda(mFilePath);
}

// #refactor_convert VARefactorCls::Convert()
BOOL VARefactorCls::ConvertInstance(DType* sym)
{
	TraceScopeExit tse("Convert Between Pointer and Instance exit");

	ConvertBetweenPointerAndInstance convert;
	return convert.Convert(sym, mInfo, mMethodScope, mFilePath, false);
}

BOOL VARefactorCls::SimplifyInstanceDeclaration(DType* sym)
{
	TraceScopeExit tse("Convert Between Pointer and Instance exit");

	ConvertBetweenPointerAndInstance convert;
	convert.SetSimplify(true);
	return convert.Convert(sym, mInfo, mMethodScope, mFilePath, true);
}

BOOL VARefactorCls::CanFindReferences(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;
	if (!sym || sym->IsEmpty())
		return FALSE;
	if (strstrWholeWord(sym->Def().c_str(), "operator"))
		return FALSE;
	if (sym->SymScope().GetLength())
		return TRUE;
	return FALSE;
}

BOOL VARefactorCls::CanRename(DType* sym)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	if (strstrWholeWord(sym->Def().c_str(), "operator"))
		return FALSE;
	// bug 2197 - allow if the selected text is the current symbol
	// otherwise not available if any text is selected
	if (ed->GetSelString().GetLength() > 0 && ed->GetSelString() == sym->Sym())
		return true;
	return 0 == ed->GetSelString().GetLength();
}

BOOL VARefactorCls::CanPromoteLambda()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	// Get the current context
	WTString context = ed->m_lastMinihelpDef;

	// Check if the context contains "[<whitespace character>="
	std::regex lambda_pattern(R"(\s*=\s*\[)");
	if (std::regex_search(context.c_str(), lambda_pattern)) // no need for CommentSkipper and complex state machine, the definition field removes comments
	{
		// Now, after we have quickly identified the lambda to make sure the quick menu
		// remains fast, we need to check if the lambda is at a call-site or a definition
		// So we only do additional calculations when needed
		const WTString& buf = ed->GetBuf(FALSE);
		uint fakePos = ed->CurPos();
		int realPos = ed->GetBufIndex(buf, (long)fakePos);
		CommentSkipper cs(Src);

		// Find the end of the word from the caret
		int wordEnd = realPos;
		for (; wordEnd < buf.GetLength(); ++wordEnd)
		{
			if (!cs.IsCode(buf[wordEnd]))
				continue;

			if (!isalnum(buf[wordEnd]) && buf[wordEnd] != '_')
				break;
		}

		// Skip whitespaces and check for opening parenthesis
		for (int i = wordEnd; i < buf.GetLength(); ++i)
		{
			if (!cs.IsCode(buf[i]))
				continue;

			if (!isspace(buf[i]))
			{
				// Check if the first non-whitespace character is '('
				if (buf[i] == '(')
				{
					return FALSE; // We're at a call-site, so don't promote
				}
				break;
			}
		}

		return TRUE; // Lambda definition found and not at a call-site
	}

	return FALSE; // Default case: can't promote
}

BOOL VARefactorCls::CanRenameReferences() const
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	// What is this?
	return ed->m_lastEditSymScope.GetLength() != 0 && ed->m_lastEditPos == (ULONG)ed->GetBegWordIdxPos() &&
	       ed->GetSymDef().GetLength() != 0;
}

BOOL VARefactorCls::CanAddMember(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	WTString symSymScope(sym->SymScope());
	return symSymScope.Find(":ForwardDeclare:") == -1 && symSymScope.Find(":TemplateParameter:") == -1 &&
	       (sym->MaskedType() == CLASS || sym->MaskedType() == STRUCT || sym->MaskedType() == C_INTERFACE);
}

BOOL VARefactorCls::CanAddSimilarMember(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	if (sym->SymScope().contains(EncodeScope("<").c_str()))
		return FALSE; // Add similar member of template instances fails, so disable for now.

	return sym->ScopeHash() &&
	       !sym->HasLocalScope()
	       // 		&& !sym->Value().contains("\f") // enable for overloaded methods -per Jeff
	       && (sym->MaskedType() == VAR || sym->MaskedType() == FUNC);
}

BOOL VARefactorCls::CanChangeSignature(DType* sym)
{
	class ChangeSignature cs;
	return cs.CanChange(sym);
}

BOOL VARefactorCls::CanChangeVisibility(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	return FALSE;
}

BOOL VARefactorCls::CanOverrideMethod(DType* sym)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	return FALSE;
}

WTString GetDeclDef(DType* sym)
{
	EdCntPtr ed(g_currentEdCnt);
	// We might have been passed the DType for the impl, not the declaration
	// def = "int Foo::Fn1() {...}", instead of "int Fn1()"
	if (ed && sym && sym->IsImpl())
	{
		MultiParsePtr mp(ed->GetParseDb());
		sym = mp->FindExact(sym->SymScope().c_str());
	}

	if (sym)
	{
		token2 t = sym->Def();
		while (t.more() > 1)
		{
			WTString def = t.read('\f');
			if (!def.contains("{...}") || t.more() < 2)
				return def;
		}
	}
	return NULLSTR;
}

BOOL VARefactorCls::AddMember(DType* cls, DType* similarMember /*= NULL*/)
{
	TraceScopeExit tse("Add Member exit");

	WTString def = GetDeclDef(similarMember);
	def = DecodeTemplates(def);
	def.ReplaceAll("{...}", "");

	// [case: 142818] adding support for constructors
	def.ReplaceAll("class ", "");
	if (def.find("::") == -1)
		def = TokenGetField(def.c_str(), ":");
	def.TrimRight();
	if (def.find(" ") == -1 && def.GetLength() && def[def.GetLength() - 1] != ')')
		def += "()";

	AddClassMemberDlg dlg(similarMember ? AddClassMemberDlg::DlgAddSimilarMember : AddClassMemberDlg::DlgAddMember,
	                      similarMember ? def.c_str() : NULL);
	if (dlg.DoModal() != IDOK)
		return FALSE;

	_ASSERTE(g_currentEdCnt);
	const WTString lnBrk(g_currentEdCnt->GetLineBreakString());
	WTString tmpBuf = dlg.GetUserText();
	if (Is_VB_VBS_File(g_currentEdCnt->m_ftype))
	{
		if (strstrWholeWord(tmpBuf.c_str(), "Sub"))
		{
			tmpBuf += "$end$" + lnBrk + "End Sub";
		}
	}
	else
	{
		if (tmpBuf.contains("("))
			tmpBuf += lnBrk + "{$end$" + lnBrk + "}";
		else if (!tmpBuf.contains(";"))
			tmpBuf = WTString("$end$") + tmpBuf + ';';
	}
	ParseToCls ptc(g_currentEdCnt->m_ftype);
	ptc.ParseTo(tmpBuf.c_str(), tmpBuf.GetLength(), ";");
	const CStringW file = FileFromDef(cls);
	WTString classScope = cls->SymScope() + ":NewMember";
	if (similarMember)
		classScope = similarMember->SymScope();

	FreezeDisplay _f;
	// if ptc is unable to determine the type assign it manually
	if (!ptc.State().m_defType)
	{
		ptc.State().m_defType = tmpBuf.contains("(") ? FUNC : VAR;
	}
	if (GotoDeclPos(classScope /*+ COLONSTR + ptc.MethScope()*/, file, ptc.State().m_defType))
	{
		BOOL rslt;
		UndoContext undoContext("AddClassMember");
		_f.ReadOnlyCheck();
		if (TERCOL(g_currentEdCnt->CurPos()) > 1)
			rslt = PV_InsertAutotextTemplate(WTString(" ") + tmpBuf, TRUE);
		else
			rslt = PV_InsertAutotextTemplate(tmpBuf + lnBrk, TRUE);
		if (tmpBuf.contains("{") || tmpBuf.contains("End Sub"))
			_f.LeaveCaretHere(); // user will need to modify newly created method body
		return rslt;
	}

	return FALSE;
}

BOOL VARefactorCls::ChangeSignature(DType* sym)
{
	class ChangeSignature cs;
	if (cs.CanChange(sym))
		return cs.Change();
	return FALSE;
}

BOOL VARefactorCls::AddUsingStatement(const WTString& statement)
{
	TraceScopeExit tse("Add Using Statement exit");
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	//	RefactoringActive active;
	long p = (long)ed->CurPos();
	ulong ln = TERROW(p);
	ulong col = TERCOL(p);
	WTString buf = ed->GetBuf();
	int ipos = 0;
	WTString usingstr = (ed->m_ftype == CS) ? "using" : "Imports";
	for (long lp = 0; (lp = buf.Find(usingstr.c_str(), lp)) != -1; lp++)
	{
		if (!lp || buf[(uint)lp - 1] == '\n')
			ipos = lp;
	}
	if (ipos)
		ipos = buf.find("\n", ipos) + 1;
	if (ipos != -1)
	{
		{
			FreezeDisplay _f;
			ed->SetSel(ipos, ipos);
			_f.ReadOnlyCheck();
			ed->InsertW(statement.Wide() + L"\r\n");
			token2 nsTok(statement);
			nsTok.read(" ;");
			WTString ns = DB_SEP_STR + nsTok.read(" ;");
			ns.ReplaceAll(".", ":");

			MultiParsePtr mp(ed->GetParseDb());
			WTString globalNamespaces = mp->GetGlobalNameSpaceString();
			globalNamespaces += ns + "\f";
			mp->SetGlobalNameSpaceString(globalNamespaces);

			ln++;
		}
		ed->SetSel(TERRCTOLONG(ln, col), TERRCTOLONG(ln, col));
		return TRUE;
	}

	return FALSE;
}

void FixupQtInclude(CStringW& filename, uint& SymFileId)
{
	if (filename.GetLength() < 3)
		return;

	CStringW tmp(filename.Left(filename.GetLength() - 2));
	if (!IsFile(tmp))
	{
		const CStringW base(Basename(filename));
		DTypeList incByList;
		IncludesDb::GetIncludedBys(filename, DTypeDbScope::dbSlnAndSys, incByList);
		for (auto& inc : incByList)
		{
			const CStringW incFile(inc.FilePath());
			const CStringW incFileBase(Basename(incFile));
			if (incFileBase == base && incFile.GetLength() > 2)
			{
				tmp = incFile.Left(incFile.GetLength() - 2);
				break;
			}
		}
	}

	if (IsFile(tmp))
	{
		// replace .h version of include with version that has no extension
		FixFileCase(tmp);
		filename = tmp;
		SymFileId = gFileIdManager->GetFileId(filename);
	}
}

BOOL VARefactorCls::GetAddIncludeInfo(int& outAtLine, CStringW& outSymFile, BOOL* sysOverride /*= nullptr*/) const
{
	outAtLine = 0;
	outSymFile.Empty();
	if (sysOverride)
		*sysOverride = FALSE;
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed || !(ed->m_ftype == Src || ed->m_ftype == Header))
		return false;

	if (ed->HasSelection())
		return FALSE;

	const VAScopeInfo* info = mInfo.ConstPtr();
	if (!info)
		return false;

	bool isSysSym = info->CurDataAttr() & V_SYSLIB;

	if (sysOverride)
	{
		if (isSysSym)
			*sysOverride = TRUE;
	}

	const uint curFileId = gFileIdManager->GetFileId(ed->FileName());
	uint SymFileId = info->CurDataFileId();
	WTString infoCurSymScope(info->mCurSymScope);
	WTString infoCurSymName(info->CurSymName());
	if (info->CurDataType() == VAR && SymFileId == curFileId)
	{
		// set baseInfo to the VAR type
		DType* baseInfo = NULL;
		token t;
		try
		{
			t = ::GetTypesFromDef(infoCurSymScope.c_str(), info->CurSymDef().c_str(), info->CurDataType(),
			                      ed->m_ScopeLangType);
		}
		catch (const WtException&)
		{
		}
		WTString sym = t.read("\f");
		MultiParsePtr mp(ed->GetParseDb());
		token bcl = mp->GetBaseClassList(sym);
		if (bcl.length())
		{
			sym = bcl.read("\f");
			baseInfo = mp->FindExact(sym.c_str());
		}

		if (!baseInfo)
			return false;

		SymFileId = baseInfo->FileId();
		infoCurSymScope = baseInfo->SymScope();
		infoCurSymName = ::StrGetSym(infoCurSymScope);
		infoCurSymName = ::DecodeTemplates(infoCurSymName);
		int pos = infoCurSymName.Find('<');
		if (-1 != pos)
			infoCurSymName = infoCurSymName.Left(pos);
		info = NULL;
	}

	if (!SymFileId)
		return false;

	CStringW filename(gFileIdManager->GetFileForUser(SymFileId));
	if (filename.IsEmpty() || Header != ::GetFileType(filename))
		return false;

	CStringW base(::Basename(filename));

    const std::initializer_list<std::wstring> std_hdrs = 
	{ 
		// clang-format off
		L"stdlib", L"stdio",  L"string",   L"conio",  L"math", 
		L"ctype",  L"wctype", L"time",     L"assert", L"locale", 
		L"signal", L"setjmp", L"stdarg",   L"errno",  L"complex", 
		L"fenv",   L"float",  L"inttypes", L"iso646", L"limits", 
		L"setjmp", L"signal", L"stdalign", L"uchar",  L"wchar",  		 
		// clang-format on
	};

	bool isCppSysSym = isSysSym && IsEditorProbablyCppFile(ed.get()); // [case: 164393]
	bool isFileResolved = false;

	if (isCppSysSym)
	{
		LPCWSTR basewstr = base;
		PWSTR hIdx = StrStrIW(basewstr, L".h");
		if (hIdx)
		{
			// try to find out if this is std header
			auto len = (int)(hIdx - basewstr);
			for (const auto& hdr : std_hdrs)
			{
				if (_wcsnicmp(hdr.c_str(), basewstr, (size_t)len) == 0)
				{
					// we know it is a standard header file, 
					// switch the name from "name.h" to "cname" form
					CStringW tmpPath = base;
					if (FindCppIncludeFile(tmpPath, true))
					{
						filename = tmpPath;
						base = ::Basename(filename);
						isFileResolved = true;
					}
					break;
				}
			}
		}
	}

	// [case: 24899] boost hack for _fwd headers
	if (::Path(filename).Find(L"boost") != -1)
	{
		int pos = base.Find(L"_fwd.h");
		if (-1 == pos)
			pos = base.Find(L"fwd.h");
		if (-1 != pos)
		{
			const CStringW shortBase(base.Left(pos));
			if (filename.Find(shortBase) == (filename.GetLength() - base.GetLength() - shortBase.GetLength() - 1))
			{
				int pos2 = filename.Find(base);
				if (-1 != pos2)
				{
					CStringW tmpHeader = filename.Left(pos2 - 1) + L".hpp";
					if (::IsFile(tmpHeader))
					{
						filename = tmpHeader;
						base = ::Basename(filename);
					}
				}
			}
		}
	}

	if (::ShouldIgnoreFile(filename, true))
	{
		bool shouldReturn = true;
		CStringW filenameLower(filename);
		filenameLower.MakeLower();
		if (filenameLower.Find(L"misc\\stdafx.h") != -1)
		{
			// [case: 23430] vector/list/etc are found in our file
			// locate correct def; update filename and SymFileId
			DTypeList refList;
			const uint scopeId = ::WTHashKey(::StrGetSymScope(infoCurSymScope).c_str());
			GetSysDic()->FindExactList(infoCurSymName.c_str(), scopeId, refList);
			for (DTypeList::iterator it = refList.begin(); it != refList.end(); ++it)
			{
				const CStringW curFilename(gFileIdManager->GetFileForUser((*it).FileId()));
				if (!curFilename.IsEmpty() && curFilename != filename && Header == ::GetFileType(curFilename) &&
				    !::ShouldIgnoreFile(curFilename, true))
				{
					shouldReturn = false;
					filename = curFilename;
					base = ::Basename(filename);
					SymFileId = (*it).FileId();
					break;
				}
			}
		}

		if (shouldReturn)
			return false;
	}

	// [case:117655] use correct header for STL types
	// current JSON file contains only STL, but we can extend it
	if (isCppSysSym && !isFileResolved && CanFindHeaderInTable(infoCurSymScope))
	{
		auto hdr = FindHeaderInTable(infoCurSymScope);
		if (hdr && *hdr)
		{
			// reset all including base so it does not match other workarounds
			CStringW filedir = filename.Mid(0, filename.GetLength() - base.GetLength());
			base = ::MbcsToWide(hdr, (int)strlen(hdr));
			filename = filedir + base;
			SymFileId = gFileIdManager->GetFileId(filename);
		}
	}

	if (base == "xstring")
	{
		// [case: 31895] don't use xstring for std::string
		CStringW tmp(filename);
		tmp.Replace(L"xstring", L"string");
		if (::IsFile(tmp))
		{
			filename = tmp;
			base = ::Basename(filename);
			SymFileId = gFileIdManager->GetFileId(filename);
		}
	}
	else if (base == L"crtdbg.h" || ::StartsWith(base, L"corecrt_", FALSE, true))
	{
		// [case: 163871] wrong header for malloc, calloc
		MultiParsePtr mp(ed->GetParseDb()); 
		if (::FindPreferredInclude(filename, infoCurSymScope, mp, std_hdrs, isCppSysSym))	
			SymFileId = gFileIdManager->GetFileId(filename);
	}
	else if (base[0] == 'q' && base.Find(L".h") == base.GetLength() - 2)
	{
		// [case: 80599]
		::FixupQtInclude(filename, SymFileId);
		base = ::Basename(filename);
	}
	else
	{
		CStringW lowerBase(base);
		lowerBase.MakeLower();
		if (-1 != lowerBase.Find(L"gdiplus") && lowerBase != L"gdiplus.h")
		{
			// [case: 78630]
			CStringW tmp(::Path(filename));
			tmp += L"\\Gdiplus.h";
			if (::IsFile(tmp))
			{
				filename = tmp;
				base = ::Basename(filename);
				SymFileId = gFileIdManager->GetFileId(filename);
			}
		}
	}

	const uint curPos = ed->CurPos();
	const int userAtLine = (int)TERROW(curPos);

	if (!::GetAddIncludeLineNumber(curFileId, SymFileId, userAtLine, outAtLine))
		return false;

	outSymFile = filename;

	return true;
}

WCHAR
GetIncludeDirectivePathDelimiter(EdCnt* ed)
{
	if (Psettings->mUnrealEngineCppSupport && GlobalProject->GetContainsUnrealEngineProject())
	{
		// [case: 114560] forward slashes are required when including headers in unreal engine games
		if (gTestLogger)
			gTestLogger->TraceStr(" PathDelim:ue ");
		return L'/';
	}

	// [case: 23341] use same delimiter already in use in the target file
	// get list of includes for the file
	DTypeList incList;
	IncludesDb::GetIncludes(ed->FileName(), DTypeDbScope::dbSolution, incList);
	for (DTypeList::iterator it = incList.begin(); it != incList.end(); ++it)
	{
		int incLine = (*it).Line();
		const WTString lineTxt(ed->GetLine(incLine));
		int pos = lineTxt.Find('/');
		if (-1 != pos && lineTxt.Find("/", pos + 1) != (pos + 1) &&
		    lineTxt.Find("*", pos + 1) != (pos + 1)) // watch out for trailing comments
		{
			if (gTestLogger)
				gTestLogger->TraceStr(" PathDelim:/ ");
			return L'/';
		}

		pos = lineTxt.Find('\\');
		if (-1 != pos)
		{
			if (gTestLogger)
				gTestLogger->TraceStr(" PathDelim:\\ ");
			return L'\\';
		}
	}

#ifdef AVR_STUDIO
	return Psettings->mDefaultAddIncludeDelimiter;
#else
	if (gShellAttr->IsDevenv10OrHigher())
	{
		bool useFwdSlash =
		    g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "UseForwardSlashForIncludeAutoComplete");
		if (gTestLogger)
			gTestLogger->TraceStr(useFwdSlash ? " PathDelim:useFwdSlash " : " PathDelim:!useFwdSlash ");
		return useFwdSlash ? L'/' : L'\\';
	}
	else
	{
		return Psettings->mDefaultAddIncludeDelimiter;
	}
#endif
}

int AdjustForBlankLines(int ln)
{
	_ASSERTE(g_currentEdCnt);
	int lastLn = ln;
	for (; ln; --ln)
	{
		lastLn = ln;

		WTString txt(g_currentEdCnt->GetLine(ln - 1));
		txt.TrimLeft();
		if (!txt.IsEmpty())
			break;
	}

	return lastLn;
}

void AddIncludeHeuristics(int& insertAtLine, CStringW headerfile)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	WTString fileText(ed->GetBuf(TRUE));
	MultiParsePtr mparse = ed->GetParseDb();
	const uint kMaxLines = ed->m_ftype == Header ? 0u : insertAtLine + 500u;
	LineMarkers markers;
	GetFileOutline(fileText, markers, mparse, kMaxLines, FALSE);
	CStringW pchFileName = GlobalProject->GetPCHFileName();

	auto includesBlock = markers.FindIncludesBlock((ULONG)insertAtLine);
	if (includesBlock && includesBlock->GetChildCount() > 0)
	{
		std::optional<uint> firstLocalIncludeIdx = markers.AreSystemIncludesFirst(*includesBlock, pchFileName);
		if (firstLocalIncludeIdx) // system includes are first and there are also local includes
		{
			// we work with two blocks of includes: system and local
			// determine the size of the two blocks by iterating through the children and skipping comments
			int systemIncludesCount = 0;
			int localIncludesCount = 0;
			int lastLine = 0;
			for (uint i = 0; i < includesBlock->GetChildCount(); ++i)
			{
				auto& child = includesBlock->GetChild(i);
				if (child.Contents().mType == COMMENT)
					continue;
				CStringW processedText = GetIncludeFromFileNameLower(child.Contents().mText);
				if (i == 0 && (processedText.Left(8) == "stdafx.h" || processedText.Left(5) == "pch.h"))
					continue; // treat it as it was a comment

				// adding to the right block
				if (i < *firstLocalIncludeIdx)
				{
					systemIncludesCount++;
				}
				else
				{
					localIncludesCount++;
				}

				// finding the last non-comment line and storing the line after that
				lastLine = static_cast<int>(child.Contents().mStartLine + 1);
			}

			bool isInProject = GlobalProject->IsFilePathInProject(headerfile);
			bool notByChance = IsSortedOrderLikelyNotPureChance(systemIncludesCount, localIncludesCount);
			std::optional<uint> systemLine = markers.IsSorted(*includesBlock, headerfile, pchFileName, 0, *firstLocalIncludeIdx - 1);
			std::optional<uint> localLine = markers.IsSorted(*includesBlock, headerfile, pchFileName, *firstLocalIncludeIdx, INT_MAX);
			if (isInProject)
			{
				// is sorted?
				if (systemLine.has_value() && localLine.has_value() && notByChance)
				{
					insertAtLine = static_cast<int>(*localLine);
				}
				else
				{
					insertAtLine = lastLine;
				}
				return;
			}
			else // not in project
			{
				// is sorted?
				if (systemLine.has_value() && localLine.has_value() && notByChance)
				{
					insertAtLine = static_cast<int>(*systemLine);
				}
				else
				{
					auto& firstChild = includesBlock->GetChild(*firstLocalIncludeIdx - 1); // indexing to the last system include
					insertAtLine = static_cast<int>(firstChild.Contents().mStartLine + 1); // insert after the last system include
				}
				return;
			}
		}
		else
		{
			int realChildCount = 0;
			for (uint i = 0; i < includesBlock->GetChildCount(); ++i)
			{
				auto& child = includesBlock->GetChild(i);
				if (child.Contents().mType == COMMENT)
					continue;
				CStringW processedText = GetIncludeFromFileNameLower(child.Contents().mText);
				if (i == 0 && (processedText.Left(8) == "stdafx.h" || processedText.Left(5) == "pch.h"))
					continue; // treat it as it was a comment

				realChildCount++;
			}

			if (IsSortedOrderLikelyNotPureChance(realChildCount))
			{
				// check whether the children of the node are sorted
				std::optional<int> line = markers.IsSorted(*includesBlock, headerfile, pchFileName, 0, INT_MAX);
				if (line.has_value())
				{
					insertAtLine = *line;
					return;
				}
			}
		}
	}
}
void RefineAddIncludeLineNumber(int& insertAtLine)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	const int kCurPos = (int)ed->CurPos();
	const ULONG kUserAtLine = TERROW(kCurPos);
	WTString fileText(ed->GetBuf(TRUE));
	MultiParsePtr mparse = ed->GetParseDb();
	const uint kMaxLines = ed->m_ftype == Header ? 0u : insertAtLine + 500u;
	LineMarkers markers;
	GetFileOutline(fileText, markers, mparse, kMaxLines, FALSE);

	int checkAttempts = 0;
	int checkLine = insertAtLine;
	int fallbackLine = 0;

	LineMarkerPath pathForUserLine;
	markers.CreateMarkerPath(kUserAtLine, pathForUserLine);

	for (;;)
	{
		LineMarkerPath path;
		markers.CreateMarkerPath((ULONG)checkLine, path);

		if (path.size() && !kMaxLines)
		{
			// if the first path step starts with a #ifndef or #if !defined
			// and its endLine is greater than the line that the user is on,
			// then remove it - assume it is a header file include guard that
			// encompasses the whole file (aside from a potential comment above
			// it - hence no check of startline).
			FileLineMarker& f = path.front();
			if (FileOutlineFlags::ff_Preprocessor == f.mDisplayFlag && 0 == f.mText.Find(L"#if"))
			{
				// ignore whitespace that follows node
				int newEndLine = AdjustForBlankLines((int)f.mEndLine);
				if (newEndLine > (int)kUserAtLine)
				{
					// I had wanted to check that the node has an immediate child
					// at index 0 with "#define" text, but LineMarkerPath has
					// FileLineMarkers, not Nodes, so children aren't accessible.
					// I think not worth changing for that check.
					f.mDisplayFlag = FileOutlineFlags::ff_None;
				}
			}
		}

		if (!path.size())
			break;

		if (checkLine == insertAtLine)
		{
			// see if initial line number is safe
			FileLineMarker& f = path.back();
			if (FileOutlineFlags::ff_Preprocessor == f.mDisplayFlag && -1 != f.mText.Find(L"pragma"))
				return; // safe

			if (!path.HasBlockDisplayType(FileOutlineFlags::ff_Preprocessor))
				break;

			// the initial line number is within a preproc block, so need to
			// find a place after the block to use instead.
		}

		if (path.HasBlockDisplayType(FileOutlineFlags::ff_Preprocessor) /*||
			!path.IsLineAtBlockStart((ULONG)checkLine)*/)
		{
			if (path.IsLineAtBlockStart((ULONG)checkLine))
			{
				if (!fallbackLine)
				{
					const FileLineMarker& mkr = path[0];
					FileOutlineFlags::DisplayFlag displayFlag = (FileOutlineFlags::DisplayFlag)mkr.mDisplayFlag;
					if (displayFlag == FileOutlineFlags::ff_Preprocessor)
					{
						if (0 == mkr.mText.Find(L"#if") && (int)mkr.mStartLine == checkLine)
						{
							// this is a global line.
							// save in case preprocs are consecutive without interleaved blanks
							if (checkLine < (int)kUserAtLine)
								fallbackLine = checkLine;
						}
					}
				}

				if (pathForUserLine.size() >= path.size())
				{
					// see if we are in same preproc block; if so, then insert is ok
					if (pathForUserLine.HasBlockDisplayType(FileOutlineFlags::ff_Preprocessor))
					{
						bool isOk = true;
						for (size_t idx = 0; idx < path.size() && isOk; ++idx)
						{
							const FileLineMarker& m1 = path[idx];
							const FileLineMarker& m2 = pathForUserLine[idx];
							if (m1 != m2)
								isOk = false;
							else
							{
								DWORD safeFlags =
								    FileOutlineFlags::ff_Includes | FileOutlineFlags::ff_Preprocessor |
								    FileOutlineFlags::ff_Globals | FileOutlineFlags::ff_Macros |
								    FileOutlineFlags::ff_IncludePseudoGroup | FileOutlineFlags::ff_MacrosPseudoGroup |
								    FileOutlineFlags::ff_FwdDeclPseudoGroup | FileOutlineFlags::ff_FwdDecl |
								    FileOutlineFlags::ff_GlobalsPseudoGroup;

								if (!(m1.mDisplayFlag & safeFlags))
									isOk = false;
							}
						}

						if (isOk)
						{
							if (fallbackLine && (fallbackLine + 50) < checkLine)
								checkLine = fallbackLine;
							else
								++checkLine;
							break;
						}
					}
				}
			}

			if (++checkLine > (int)kUserAtLine || ++checkAttempts >= 100)
			{
				if (!fallbackLine || fallbackLine > (int)kUserAtLine)
				{
					// bail out, no modification to original insert line
					return;
				}

				checkLine = fallbackLine;
				break;
			}
		}
		else
			break;
	}

	if (checkLine == insertAtLine || checkLine >= (int)kUserAtLine)
	{
		if (fallbackLine && fallbackLine < (int)kUserAtLine && fallbackLine != insertAtLine)
			checkLine = fallbackLine;
	}

	if (checkLine != insertAtLine && checkLine <= (int)kUserAtLine)
	{
		insertAtLine = checkLine;
		checkLine = AdjustForBlankLines(checkLine);
		if (checkLine != insertAtLine && checkLine <= (int)kUserAtLine)
			insertAtLine = checkLine;
	}

	if (insertAtLine != 1)
		return;

	// [case: 117094] place includes after #ifndef style include guards or initial file comment if no guards are found
	auto CheckForIncludeProtection = [&](LineMarkers::Node& node) {
		if (ed->m_ftype == Header)
		{
			CStringW textNoSpace = node.Contents().mText;
			textNoSpace.Replace(L" ", L"");
			if (StartsWithNC(textNoSpace, L"#ifndef", FALSE) || StartsWithNC(textNoSpace, L"#if!defined", FALSE))
			{
				if (node.GetChildCount())
				{
					if (node.GetChild(0).GetChildCount())
					{
						if (StartsWithNC(node.GetChild(0).GetChild(0).Contents().mText, L"#define", FALSE))
						{
							CStringW defText = node.GetChild(0).GetChild(0).Contents().mText;
							defText = defText.Mid(7);
							defText.TrimLeft();
							if (!defText.IsEmpty() && -1 != textNoSpace.Find(defText))
							{
								checkLine = insertAtLine = (int)node.GetChild(0).Contents().mStartLine + 1;
							}
						}
					}
				}
			}
		}
	};
	if (markers.Root().GetChildCount())
	{
		if (markers.Root().GetChild(0).Contents().mType == DEFINE &&
		    markers.Root().GetChild(0).Contents().mEndLine > (int)kUserAtLine)
		{
			CheckForIncludeProtection(markers.Root().GetChild(0));
		}
		else if (markers.Root().GetChildCount() > 1)
		{
			if (markers.Root().GetChild(0).Contents().mType == COMMENT)
			{
				if (markers.Root().GetChild(1).Contents().mType == DEFINE &&
				    markers.Root().GetChild(1).Contents().mEndLine > (int)kUserAtLine)
				{
					CheckForIncludeProtection(markers.Root().GetChild(1));
				}
				else if (Psettings->mAddIncludeSkipFirstFileLevelComment)
				{
					if (markers.Root().GetChild(0).Contents().mStartLine < static_cast<ULONG>(3))
					{
						int adjustedEndLine = AdjustForBlankLines((int)markers.Root().GetChild(0).Contents().mEndLine);
						if (adjustedEndLine > insertAtLine)
						{
							if (adjustedEndLine < (int)markers.Root().GetChild(1).Contents().mStartLine)
							{
								// file starts with an initial file comment and has no include protection
								// insert the header after the comment
								checkLine = insertAtLine = adjustedEndLine;
							}
						}
					}
				}
			}
		}
	}
}

BOOL DoAddInclude(int insertAtLine, CStringW headerfile, BOOL sysOverride /*= FALSE*/)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;

	_ASSERTE(!headerfile.IsEmpty());
	CWaitCursor curs;
	// [case: 70467] fix for #includes dropped into ifdef blocks
	// only refine if actually invoking AddInclude, not during QueryStatus
	::RefineAddIncludeLineNumber(insertAtLine);

	// [case: 149209] add include should try to guess the best location for the new include
	::AddIncludeHeuristics(insertAtLine, headerfile);

	const uint curPos = ed->CurPos();
	(void)curPos;
	const long initialFirstVisLine = ed->GetFirstVisibleLine();
	ulong insertPos = ed->LinePos(insertAtLine);
	if (insertPos == -1)
		return false;

	UndoContext undoContext("Add Include");
	std::unique_ptr<TerNoScroll> ns;
	if (gShellAttr->IsMsdev())
		ns = std::make_unique<TerNoScroll>(ed.get());
	FreezeDisplay _f;

	ed->SetSel(insertPos, insertPos);
	_f.ReadOnlyCheck();
	const WCHAR pathDelimiter(::GetIncludeDirectivePathDelimiter(ed.get()));
	const CStringW directive(
	    ::BuildIncludeDirective(headerfile, ed->FileName(), pathDelimiter, Psettings->mAddIncludePath, sysOverride));
	if (gTestLogger)
	{
		WTString info;
		info.WTFormat("AddInclude: %s at line %d", (LPCTSTR)CString(directive), insertAtLine);
		gTestLogger->LogStr(info);
	}
	ed->InsertW(directive + CStringW(ed->GetLineBreakString().Wide()));
	if (gShellAttr->IsDevenv())
	{
		// [case: 30577] restore first visible line
		ulong topPos = ed->LinePos(initialFirstVisLine + 1);
		if (-1 != topPos && gShellSvc)
		{
			ed->SetSel(topPos, topPos);
			gShellSvc->ScrollLineToTop();
		}
		_f.OffsetLine(1);
	}
	else
	{
		_ASSERTE(ns.get());
		_f.LeaveCaretHere();
		ns->OffsetLine(1);
	}

	return true;
}

BOOL VARefactorCls::AddInclude()
{
	int insertAtLine;
	CStringW headerfile;
	BOOL sysOverride = FALSE;
	if (!GetAddIncludeInfo(insertAtLine, headerfile, &sysOverride))
		return false;

	return DoAddInclude(insertAtLine, headerfile, sysOverride);
}

CStringW GetFileLocationFromInclude(EdCntPtr ed)
{
	CStringW file;

	if (ed != nullptr)
	{
		token2 lineTok = ed->GetSubString(ed->LinePos(), ed->LinePos(ed->CurLine() + 1));

		if (lineTok.contains("include"))
		{
			BOOL doLocalSearch = lineTok.contains("\"");
			lineTok.read("<\""); // strip off #include "
			file = lineTok.read("<>\"").Wide();

			if (file.GetLength())
			{
				file = gFileFinder->ResolveInclude(file, ::Path(ed->FileName()), doLocalSearch);

				if (file.GetLength())
				{
					UINT fid = gFileIdManager->GetFileId(file);

					if (fid)
						file = gFileIdManager->GetFile(fid);
				}
			}
		}
	}

	return file;
}

BOOL GetIsCursorOnIncludeDirective()
{
	EdCntPtr ed(g_currentEdCnt);

	if (ed != nullptr)
	{
		const bool isCursorBeforePreproc = ed->m_lastScope == DB_SEP_STR && '#' == ed->CharAt(ed->CurPos());
		const bool isCursorInsidePreproc = ed->m_lastScope == DBColonToSepStr(DB_SCOPE_PREPROC.c_str());

		if (isCursorBeforePreproc || isCursorInsidePreproc)
		{
			WTString line = ed->GetLine(ed->CurLine());
			line.TrimLeft();

			int pos = line.find("include");
			if (-1 != pos && pos < 10)
				return TRUE;
		}
	}

	return FALSE;
}

BOOL VARefactorCls::CanGotoInclude()
{
	return GetIsCursorOnIncludeDirective();
}

// [case: 114572] enhance alt+shift+q quick menu when exec'd on #include directive
BOOL VARefactorCls::GotoInclude()
{
	CStringW file;
	EdCntPtr ed(g_currentEdCnt);

	if (ed != nullptr)
	{
		file = GetFileLocationFromInclude(ed);

		if (file.GetLength())
		{
			DelayFileOpen(file, 0, nullptr, TRUE);
			::SetTimer(::GetFocus(), ID_ADDMETHOD_TIMER, 500, nullptr);
		}
	}

	return file.GetLength() ? TRUE : FALSE;
}

BOOL VARefactorCls::CanOpenFileLocation()
{
	return GetIsCursorOnIncludeDirective();
}

// [case: 114572] enhance alt+shift+q quick menu when exec'd on #include directive
BOOL VARefactorCls::OpenFileLocation()
{
	CStringW file;
	EdCntPtr ed(g_currentEdCnt);

	if (ed != nullptr)
	{
		file = GetFileLocationFromInclude(ed);
		WTString failureMessage("Open File Location file empty, command not executed.");

		if (file.GetLength())
			BrowseToFile(file);
		else if (gTestLogger)
			gTestLogger->LogStr(failureMessage);
		else
			Log(failureMessage.c_str());
	}

	return file.GetLength() ? TRUE : FALSE;
}

BOOL VARefactorCls::CanDisplayIncludes()
{
#if defined(RAD_STUDIO)
	return FALSE;
#else
	bool canDisplayIncludes = false;
	EdCntPtr ed(g_currentEdCnt);

	if (ed)
		canDisplayIncludes = !!ed->QueryStatus(icmdVaCmd_DisplayIncludes);

	return canDisplayIncludes;
#endif
}

BOOL VARefactorCls::DisplayIncludes()
{
	BOOL includesDisplayed = FALSE;
	EdCntPtr ed(g_currentEdCnt);

	if (ed)
	{
		includesDisplayed = (BOOL)ed->SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("VAssistX.ListIncludeFiles"), 0);

		if (gTestLogger)
		{
			WTString msg;

			if (includesDisplayed)
				msg.WTFormat("VARefactorCls::DisplayIncludes() includes successfully displayed.");
			else
				msg.WTFormat("ERROR: VARefactorCls::DisplayIncludes() displayed failed.");

			gTestLogger->LogStr(msg);
		}
	}

	return includesDisplayed;
}

BOOL VARefactorCls::CanAddInclude() const
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	CStringW incFile;
	int atLine;
	const BOOL canAddInclude = GetAddIncludeInfo(atLine, incFile);
	return canAddInclude && !incFile.IsEmpty();
}

BOOL VARefactorCls::CanAddForwardDeclaration()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	AddForwardDeclaration afd;
	return afd.CanAdd();
}

BOOL VARefactorCls::AddForwardDecl() const
{
	AddForwardDeclaration afd;

	if (!afd.CanAdd())
		return FALSE;

	return afd.Add();
}

BOOL VARefactorCls::CanExpandMacro(DType* sym, const WTString& invokingScope)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (!g_currentEdCnt || !IsCFile(g_currentEdCnt->m_ftype))
		return FALSE;

	if (!sym || sym->IsEmpty())
		return FALSE;

	if (sym->MaskedType() != DEFINE)
		return FALSE;

	if (invokingScope == ":PP:")
		return FALSE;

	// 	if (!DType::IsLocalScope(invokingScope))
	// 		return FALSE;

	return TRUE;
}

extern BOOL GetCurrentMacro(EdCntPtr ed, DType* sym, WTString& macroCode, long* sPos = nullptr, long* ePos = nullptr);

BOOL VARefactorCls::ExpandMacro(DType* sym, const WTString& invokingScope)
{
	if (!CanExpandMacro(sym, invokingScope))
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);

	WTString collapsed;
	long sp, ep;
	if (GetCurrentMacro(ed, sym, collapsed, &sp, &ep))
	{
		WTString expanded = VAParseExpandAllMacros(ed->GetParseDb(), collapsed);
		if (expanded != collapsed)
		{
			ed->SetSel(sp, ep);
			if (ed->ReplaceSelW(expanded.Wide(), noFormat))
			{
				// 				if (gShellAttr->IsMsdev())
				// 					ed->GetBuf(TRUE);
				// 				ed->OnModified(TRUE);
				return TRUE;
			}
		}
	}

	return FALSE;
}

BOOL VARefactorCls::CanRenameFiles(CStringW filePath, DType* sym, const WTString& invokingScope, bool fromRefactorTip)
{
#if defined(RAD_STUDIO)
	return FALSE;
#else
	if (!gShellAttr->IsDevenv8OrHigher())
		return FALSE;

	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (fromRefactorTip)
	{
		if (DType::IsLocalScope(invokingScope))
			return FALSE;

		if (sym == nullptr)
			return FALSE;

		if (sym && !sym->IsEmpty())
		{
			switch (sym->MaskedType())
			{
			case CLASS:
			case STRUCT:
			case C_INTERFACE:
				break;
			default:
				return FALSE;
			}

			CStringW baseName = Basename(filePath);
			int dotPos = baseName.ReverseFind('.');
			if (dotPos != -1)
				baseName = baseName.Left(dotPos);

			CStringW symName = sym->Sym().Wide();
			if (baseName.CompareNoCase(symName) == 0)
				return FALSE;

			// MFC Check
			// check for class CBlarg in file Blarg.h/.cpp
			if (IsCFile(GetFileType(filePath)))
			{
				CStringW mfcBaseName = L"C" + baseName;
				if (mfcBaseName.CompareNoCase(symName) == 0)
					return FALSE;
			}
		}
		else
		{
			if (ed->GetSelString().GetLength())
				return FALSE;

			WTString cwd = ed->WordRightOfCursor();
			cwd.TrimLeft();
			if (cwd.GetLength())
				return FALSE;
		}
	}

	return TRUE;
#endif
}

BOOL VARefactorCls::RenameFiles(CStringW filePath, DType* sym, const WTString& invokingScope, bool fromRefactorTip,
                                CStringW newName /*=""*/)
{
	if (!fromRefactorTip)
	{
		// if the passed sym would not have been valid from the refactor tip,
		// then ignore the sym and keep going
		if (!CanRenameFiles(filePath, sym, invokingScope, true))
			sym = NULL;
		else if (sym && !sym->IsEmpty())
		{
			// if the new filename already exists, don't seed with the sym
			CStringW newFilePath = RenameFilesDlg::BuildNewFilename(filePath, sym->Sym().Wide());
			if (IsFile(newFilePath))
				sym = NULL;
		}
	}

	if (!CanRenameFiles(filePath, sym, invokingScope, fromRefactorTip))
		return FALSE;

	ProjectVec projectVec = GlobalProject->GetProjectForFile(filePath);
	for (auto prj : projectVec)
	{
		if (prj->IsDeferredProject())
		{
			if (gTestLogger)
			{
				WTString msg;
				msg.WTFormat("ERROR: VARefactorCls::RenameFiles does not work in deferred project");
				gTestLogger->LogStr(msg);
			}

			vLog("ERROR: VARefactorCls::RenameFiles deferred project\n");
			return FALSE;
		}

		if (prj->IsPseudoProject())
			vLog("WARN: VARefactorCls::RenameFiles pseudo project\n");
	}

	RenameFilesDlg ren(filePath, sym);
	if (!newName.IsEmpty())
		ren.SetNewName(newName);
	ren.DoModal();

	return TRUE;
}

BOOL VARefactorCls::CanCreateFile(EdCntPtr ed)
{
#if defined(RAD_STUDIO)
	return FALSE;
#else
	if (!ed)
		return FALSE;

	CStringW filePath = ed->FileName();

	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	// don't offer to create a new file in a system dir
	// if we allow user to change dir, then this can be removed.
	if (IncludeDirs::IsSystemFile(filePath))
		return FALSE;

	return TRUE;
#endif
}

static void GetProjectForFile(CStringW filePath, CComPtr<EnvDTE::Project>& outProject)
{
	if (!gShellAttr->IsDevenv8OrHigher() || !gDte)
	{
		// we don't support adding to project unless vs2005+
		return;
	}

	_ASSERTE(gShellAttr->IsDevenv());
	ProjectVec projectVec = GlobalProject->GetProjectForFile(filePath);
	for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
	{
		ProjectInfoPtr p = *iter;
		CStringW iterFilePath = p->GetProjectFile();
		iterFilePath.MakeLower();
		if (IsDir(iterFilePath))
		{
			// website project, or folder-based
			return;
		}

		if (p->IsDeferredProject())
		{
			vLog("warn: varefactor GetProjectForFile found deferred\n");
			return;
		}

		if (p->IsPseudoProject())
		{
			vLog("warn: varefactor GetProjectForFile found pseudo\n");
			return;
		}
	}

	VsProjectList vsProjects;
	BuildSolutionProjectList(vsProjects);

	for (auto vsPrj = vsProjects.begin(); vsPrj != vsProjects.end(); ++vsPrj)
	{
		CComBSTR projFilePathBstr;
#ifdef AVR_STUDIO
		(*vsPrj)->get_FullName(&projFilePathBstr);
#else
		(*vsPrj)->get_FileName(&projFilePathBstr);
#endif
		CStringW projFilePath(projFilePathBstr);
		if (projFilePath.IsEmpty())
			continue;

		for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
		{
			ProjectInfoPtr p = *iter;
			const CStringW iterFilePath = p->GetProjectFile();
			if (!projFilePath.CompareNoCase(iterFilePath))
			{
				outProject = *vsPrj;
				// rather than return, we could conceivably return a list of vsProjects
				// instead of just returning the first match
				return;
			}
		}
	}
}

static int AddFileToProject(CStringW existingFile, CStringW newFilePath, WTString* outErrorString)
{
	int errorCount = 0;

	CComPtr<EnvDTE::Project> pProject;
	GetProjectForFile(existingFile, pProject);
	if (pProject)
	{
		CComPtr<EnvDTE::ProjectItems> pItems;
		HRESULT res = pProject->get_ProjectItems(&pItems);
		if (pItems)
		{
			if (gTestLogger)
			{
				// when AST is running, just log the addition, don't actually modify the project
				WTString msg;
				msg.WTFormat("AddFileToProject: %s", (LPCTSTR)CString(::Basename(newFilePath)));
				gTestLogger->LogStr(msg);
			}
			else
			{
				CComPtr<EnvDTE::ProjectItem> newItem;
				CComBSTR newFilePathBstr = (const wchar_t*)newFilePath;
				res = pItems->AddFromFile(newFilePathBstr, &newItem);
				if (!newItem)
				{
					vLog("ERROR: AddFileToProject AddFromFile fail %lx %s", res, (LPCTSTR)CString(newFilePath));

					// [case: 78589] poke the project by asking for its name and path
					CComBSTR projFileB;
					pProject->get_Name(&projFileB);
					CStringW projName(projFileB);

					projFileB.Empty();
					pProject->get_FileName(&projFileB);
					CStringW projFile(projFileB);
					vLog("project: %s %s", (LPCTSTR)CString(projName), (LPCTSTR)CString(projFile));

					// now retry the add
					res = pItems->AddFromFile(newFilePathBstr, &newItem);
					if (!newItem)
					{
						// it really is a failure
						++errorCount;
						vLog("ERROR: AddFileToProject AddFromFile retry failed %lx", res);
					}
				}

				if (!errorCount && ::IsFile(newFilePath))
				{
					// [case: 80931] C# .shproj don't send AddFile (.vcxitems works due to vcengine events).
					// AddFile IS triggered for the platform projects based on the shproj
					CComBSTR projFilePathBstr;
#ifdef AVR_STUDIO
					pProject->get_FullName(&projFilePathBstr);
#else
					pProject->get_FileName(&projFilePathBstr);
#endif
					const CStringW projFilePath(projFilePathBstr);
					const CStringW ext(::GetBaseNameExt(projFilePath));
					if (!ext.CompareNoCase(L"shproj"))
					{
						ProjectVec projectVec = GlobalProject->GetProjectForFile(existingFile);
						for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
						{
							ProjectInfoPtr p = *iter;
							if (!p->GetProjectFile().CompareNoCase(projFilePath))
								GlobalProject->AddFile(projFilePath, newFilePath);
						}
					}
					else if (!ext.CompareNoCase(L"csproj") || !ext.CompareNoCase(L"vbproj"))
					{
						if (!::IsProjectCacheable(projFilePath))
						{
							// [case: 98052] #netCoreSupport
							// temporary hack to update OFIS until project event listener works for .net core projects
							GlobalProject->AddFile(projFilePath, newFilePath);
						}
					}
				}
			}
		}
		else
		{
			++errorCount;
			vLog("ERROR: AddFileToProject ProjectItems fail %lx", res);
		}
	}
	else
	{
		// not an error
		vLog("WARN: AddFileToProject failed to identify project for file add %s %s", (LPCTSTR)CString(existingFile),
		     (LPCTSTR)CString(newFilePath));
	}

	if (errorCount)
	{
		if (outErrorString)
			*outErrorString = "Visual Assist failed to modify the Visual Studio project.\n\n"
			                  "Common causes for this error are read-only files and related source control problems.";
	}

	return errorCount;
}

static int CreateFileInProject(CStringW existingFile, CStringW newFilePath, WTString* outErrorString)
{
	int errorCount = 0;

	// Create the new file
	HANDLE h = ::CreateFileW(newFilePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
	                         FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != NULL && h != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(h);

		errorCount += AddFileToProject(existingFile, newFilePath, outErrorString);
		EdCntPtr ed = DelayFileOpen(newFilePath, 1);
	}
	else
	{
		vLog("ERROR: CFIP: CreateFileW fail %s", (LPCTSTR)CString(newFilePath));
		++errorCount;
	}

	return errorCount;
}

void GetMatchingFileName(CStringW existingFilePath, CStringW* outFileName, IncludeOption* outIncludeOption)
{
	int fileType = GetFileType(existingFilePath);

	CStringW fileName = Basename(existingFilePath);
	IncludeOption includeOption = IncludeOption_Hide;

	CStringW otherFilePath = existingFilePath;
	if (SwapExtension(otherFilePath, false))
	{
		// matching file already exists.
		// Default to new file in current directory
		fileName = "";
		includeOption = IncludeOption_No;
	}
	else
	{
		wchar_t tmp[MAX_PATH] = {0};
		wcscpy_s(tmp, fileName);

		switch (fileType)
		{
		case Header:
			::PathRenameExtensionW(tmp, L".cpp");
			includeOption = IncludeOption_No;
			break;
		case Src:
			::PathRenameExtensionW(tmp, L".h");
			includeOption = IncludeOption_No; // should be yes??
			break;
		default:
			tmp[0] = 0; // ""
			includeOption = IncludeOption_No;
			break;
		}
		fileName = tmp;
	}

	if (!IsCFile(fileType))
		includeOption = IncludeOption_Hide;

	if (outFileName)
		*outFileName = fileName;
	if (outIncludeOption)
		*outIncludeOption = includeOption;
}

struct FinishCreateFileParams
{
	RefactorFlag mRefFlag;
	CStringW mFilePath;
	CStringW mSelectedStr;
	int mErrors; // out param

	void Reset(RefactorFlag flg, const CStringW& file, const CStringW& str)
	{
		mRefFlag = flg;
		mFilePath = file;
		mSelectedStr = str;
		mErrors = 0;
	}
};

FinishCreateFileParams sFinishCreateFileParams;

static void CALLBACK FinishCreateFile(HWND hWnd = NULL, UINT ignore1 = 0, UINT_PTR idEvent = 0, DWORD ignore2 = 0)
{
	if (idEvent)
		KillTimer(hWnd, idEvent);

	RefactoringActive active;
	DB_READ_LOCK;
	sFinishCreateFileParams.mErrors = 0;
	RefactoringActive::SetCurrentRefactoring(sFinishCreateFileParams.mRefFlag);
	EdCntPtr ed = DelayFileOpen(sFinishCreateFileParams.mFilePath);
	if (ed)
	{
		WTString implTemplate;
		const int fileType = GetFileType(sFinishCreateFileParams.mFilePath);
		if (fileType == Header)
			implTemplate = gAutotextMgr->GetSource("Refactor Create Header File");
		else if (fileType == Src)
		{
			if (sFinishCreateFileParams.mRefFlag == VARef_MoveClassToNewFile)
				// if user decide to name .h and .cpp file differently then Refactor Create Source File would not work correctly so we need a new template
				implTemplate = gAutotextMgr->GetSource("Refactor Move Class to New File"); 
			else
				implTemplate = gAutotextMgr->GetSource("Refactor Create Source File");
		}
		else
			implTemplate = gAutotextMgr->GetSource("Refactor Create File");

		if (-1 == implTemplate.Find("$body$"))
			implTemplate.append("$body$\n");
		implTemplate.ReplaceAll("$body$", ::EncodeUserText(sFinishCreateFileParams.mSelectedStr).c_str());
		if (!implTemplate.IsEmpty())
		{
			BOOL reformat = FALSE;
			if (sFinishCreateFileParams.mRefFlag == VARef_MoveClassToNewFile)
				reformat = TRUE;	// [case: 31484] we need to reformat file since copied indentation could be a mess
			gAutotextMgr->InsertAsTemplate(ed, implTemplate, reformat);
		}
	}
	else
		++sFinishCreateFileParams.mErrors;

	RefactoringActive::SetCurrentRefactoring(0);

	if (idEvent && sFinishCreateFileParams.mErrors)
		OnRefactorErrorMsgBox();
}

BOOL RefactorDoCreateFile(CreateFileDlg::DlgType dlgType, RefactorFlag refFlag, EdCntPtr ed, bool useSelection,
                          CStringW optionalFileName /*= CStringW()*/, WTString optionalContents /*= WTString()*/,
                          MoveClassRefactorHelper moveClassRefactorHelper /*= MoveClassRefactorHelper()*/)
{
	// we need this check on multiple places later so define it here to improve readability
	const bool isMoveClassToHeaderAST = (gTestLogger && dlgType == CreateFileDlg::DlgType::DlgMoveClassToNewFile);
	
	CStringW existingFilePath = ed->FileName();

	ProjectVec projectVec = GlobalProject->GetProjectForFile(existingFilePath);
	for (auto prj : projectVec)
	{
		if (prj->IsDeferredProject())
		{
			vLog("ERROR: RefactorDoCreateFile deferred project\n");
			return FALSE;
		}

		if (prj->IsPseudoProject())
			vLog("warn: RefactorDoCreateFile pseudo project\n");
	}

	CStringW fileName(optionalFileName);
	IncludeOption includeOption = IncludeOption_Hide;
	if (fileName.IsEmpty())
		GetMatchingFileName(existingFilePath, &fileName, &includeOption);

	if (dlgType == CreateFileDlg::DlgType::DlgMoveClassToNewFile)
	{
		// when moving to header file, add option to include file (default checked), for source hide the option
		if (moveClassRefactorHelper.IsSourceFile)
			includeOption = IncludeOption_Hide;
		else
			includeOption = IncludeOption_Yes;
	}
	
	CreateFileDlg dlg(dlgType, existingFilePath, fileName, includeOption);
	if (dlg.DoModal() != IDOK)
		return false;

	CStringW directoryPath = dlg.GetLocation();
	fileName = dlg.GetFileName();
	const bool doAddInclude = dlg.IsAddIncludeChecked() || CreateFileDlg::DlgCreateStructAndFile == dlgType ||
	                          (CreateFileDlg::DlgCreateClassAndFile == dlgType && IsCFile(gTypingDevLang));

	wchar_t newFilePath[MAX_PATH] = {0};
	PathCombineW(newFilePath, directoryPath, fileName);

	RefactoringActive::SetCurrentRefactoring(refFlag);

	int errorCount = 0;
	WTString errorString;
	if (!isMoveClassToHeaderAST)
	{
		errorCount += CreateFileInProject(existingFilePath, newFilePath, &errorString);
		if (errorCount)
			vLog("ERROR: RDCF: CreateFileInProject fail %s %s %s", (LPCTSTR)CString(existingFilePath),
			     (LPCTSTR)CString(newFilePath), errorString.c_str());
	}

	CStringW selStr(optionalContents.Wide());
	if (useSelection || doAddInclude || dlgType == CreateFileDlg::DlgType::DlgMoveClassToNewFile)
	{
		ed = DelayFileOpen(existingFilePath);
		if (ed)
		{
			if (useSelection)
			{
				_ASSERTE(optionalContents.IsEmpty());
				_ASSERTE(dlgType != CreateFileDlg::DlgCreateClassAndFile &&
				         dlgType != CreateFileDlg::DlgCreateStructAndFile);
				selStr = ed->GetSelStringW();
				// Note: deletes text before new file has been attached to.
				ed->Insert("");
			}

			// [case: 31484] move class to new file
			if (dlgType == CreateFileDlg::DlgType::DlgMoveClassToNewFile)
			{
				if (moveClassRefactorHelper.IsSourceFile)	// handle code that have to be moved into source file
				{
					// first copy all marked code parts located in namespaces
					if (!moveClassRefactorHelper.MoveLocationListSrcNamespc.empty())
					{
						for (const auto& [start, end] : moveClassRefactorHelper.MoveLocationListSrcNamespc)
						{
							ed->SetSelection(start, end);
							selStr += ed->GetSelStringW();
						}

						selStr.Trim();

						for (const auto& namespc : moveClassRefactorHelper.NamespaceList | std::views::reverse)
						{
							selStr = L"namespace " + namespc + L"\n{\n" + selStr;
							selStr += L"\n}";
						}

						selStr += L"\n\n";
					}

					// second copy all code that is in format namespaceX::nemaspaceY::class
					if (moveClassRefactorHelper.MoveLocationListSrc.size() > 0)
					{
						for (const auto& [start, end] : moveClassRefactorHelper.MoveLocationListSrc)
						{
							ed->SetSelection(start, end);
							selStr += ed->GetSelStringW();
						}
					}

					// [case: 164403]
					CStringW includePath = BuildRelativePath(moveClassRefactorHelper.HeaderPathName, directoryPath);
					if (!includePath.IsEmpty())
					{
						CStringW separator = L"\\";
						if (includePath.Find(L'/') != -1)
							separator = L"/";

						includePath += separator;
					}
					selStr.Trim();
					selStr = L"#include \"" + includePath + moveClassRefactorHelper.HeaderFileName + L"\"\n\n" + selStr;

					// now delete all marked code in reverse order by; combine locations from both scenarios
					std::vector<std::pair<long, long>> moveLocationListForDeleteSrc = moveClassRefactorHelper.MoveLocationListSrc;
					moveLocationListForDeleteSrc.insert(moveLocationListForDeleteSrc.end(), moveClassRefactorHelper.MoveLocationListSrcNamespc.begin(), moveClassRefactorHelper.MoveLocationListSrcNamespc.end());
					std::sort(moveLocationListForDeleteSrc.begin(), moveLocationListForDeleteSrc.end());

					if (isMoveClassToHeaderAST)
					{
						// AST test scenario
						WTString msg = "\n\nSource file content:\n\n";
						msg += selStr;
						msg += "\n\n";
						gTestLogger->LogStr(msg);
					}
					else
					{
						for (const auto& [start, end] : moveLocationListForDeleteSrc | std::views::reverse)
						{
							ed->SetSelection(start, end);
							ed->Insert("");
						}
					}
				}
				else	// handle code that have to be moved to header file
				{
					// copy marked class body
					ed->SetSelection(moveClassRefactorHelper.MoveLocationListHdrClass.first, moveClassRefactorHelper.MoveLocationListHdrClass.second);
					CStringW selectionClass = ed->GetSelStringW();
					selStr += selectionClass;

					// handle scenario when class is defined under preprocessor directive(s)
					for (const auto& preproc : moveClassRefactorHelper.PreprocessorList | std::views::reverse)
					{
						selStr = preproc + L"\n" + selStr;
						selStr += L"#endif\n";
					}
					
					// copy all marked class methods
					for (const auto& [start, end] : moveClassRefactorHelper.MoveLocationListHdrMethods)
					{
						ed->SetSelection(start, end);
						CStringW selection = ed->GetSelStringW();
						
						// handle "inline" if needed
						for (int idx = 0; idx < selection.GetLength(); idx++)
						{
							if (!IsWhiteSpace(selection[idx]))
							{
								// check if "inline" does not exist at location of first non white space char and add it if not
								if (selection.Find(L"inline", idx) != idx)
									selection.Insert(idx, L"inline ");

								break;
							}
						}
						
						selStr += selection;
					}

					selStr.Trim();

					for (const auto& namespc : moveClassRefactorHelper.NamespaceList | std::views::reverse)
					{
						selStr = L"namespace " + namespc + L"\n{\n" + selStr;
						selStr += L"\n}";
					}

					// now delete all marked code in reverse order by; combine locations from class body and class methods
					std::vector<std::pair<long, long>> moveLocationListForDeleteHdr = moveClassRefactorHelper.MoveLocationListHdrMethods;
					moveLocationListForDeleteHdr.emplace_back(moveClassRefactorHelper.MoveLocationListHdrClass);
					std::sort(moveLocationListForDeleteHdr.begin(), moveLocationListForDeleteHdr.end());

					if (isMoveClassToHeaderAST)
					{
						// AST test scenario
						WTString msg = "\n\nHeader file content:\n\n";
						msg += selStr;
						msg += "\n\n";
						gTestLogger->LogStr(msg);
					}
					else
					{
						for (const auto& [start, end] : moveLocationListForDeleteHdr | std::views::reverse)
						{
							ed->SetSelection(start, end);
							ed->Insert("");
						}
					}
					
					// store file name so it can be offered later for source file creation
					moveClassRefactorHelper.HeaderFileName = fileName;

					// store path so it can be used to build relative path for include directive [case: 164403]
					moveClassRefactorHelper.HeaderPathName = directoryPath;
				}
			}

			if (doAddInclude && !isMoveClassToHeaderAST)
			{
				const uint curFileId = gFileIdManager->GetFileId(ed->FileName());
				const uint newFileId = gFileIdManager->GetFileId(newFilePath);
				const int curPos = (int)g_currentEdCnt->CurPos();
				const int userAtLine = (int)TERROW(curPos);
				int insertAtLine = 0;

				if (::GetAddIncludeLineNumber(curFileId, newFileId, userAtLine, insertAtLine))
				{
					if (!DoAddInclude(insertAtLine, newFilePath))
					{
						vLog("ERROR: RDCF: DoAddInclude fail %d %s", insertAtLine, (LPCTSTR)CString(newFilePath));
						++errorCount;
					}
				}
				else
				{
					vLog("ERROR: RDCF: GetAddIncludeLineNumber fail %d %d %d %d", curFileId, newFileId, userAtLine,
					     insertAtLine);
					++errorCount;
				}
			}
		}
		else
		{
			vLog("ERROR: RDCF: DelayFileOpen fail %s", (LPCTSTR)CString(newFilePath));
			++errorCount;
		}
	}

	RefactoringActive::SetCurrentRefactoring(0);

	if (!isMoveClassToHeaderAST)
	{
		// populate new file with contents
		sFinishCreateFileParams.Reset(refFlag, newFilePath, selStr);
		ed = DelayFileOpen(newFilePath);
		if (ed)
		{
			FinishCreateFile();
			if (sFinishCreateFileParams.mErrors)
				vLog("ERROR: RDCF: FinishCreateFile fail %s", (LPCTSTR)CString(newFilePath));
			errorCount += sFinishCreateFileParams.mErrors;
		}
		else
		{
			vLog("WARN: RDCF: FinishCreateFile timer");

			// [case: 71208] asp/html/etc problems with DelayFileOpen
			SetTimer(NULL, 0, 250, (TIMERPROC)&FinishCreateFile);
		}
	}

	if (errorCount)
		OnRefactorErrorMsgBox(errorString);

	return (errorCount == 0);
}

BOOL VARefactorCls::CreateFile(EdCntPtr ed)
{
	TraceScopeExit tse("Create File exit");
	if (!CanCreateFile(ed))
		return FALSE;

	return ::RefactorDoCreateFile(CreateFileDlg::DlgCreateFile, VARef_CreateFile, ed, false);
}

BOOL VARefactorCls::CanMoveSelectionToNewFile(EdCntPtr ed)
{
#if defined(RAD_STUDIO)
	return FALSE;
#else
	if (!ed)
		return FALSE;

	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (ed->GetSelStringW().Find(L"\n") == -1)
		return FALSE;

	CStringW filePath = ed->FileName();
	if (IncludeDirs::IsSystemFile(filePath))
		return FALSE;

	return TRUE;
#endif
}

BOOL VARefactorCls::MoveSelectionToNewFile(EdCntPtr ed)
{
	TraceScopeExit tse("Move Selection To New File exit");
	if (!CanMoveSelectionToNewFile(ed))
		return FALSE;

	return ::RefactorDoCreateFile(CreateFileDlg::DlgMoveSelectionToNewFile, VARef_MoveSelectionToNewFile, ed, true);
}

// #MoveClassToNewFile
// [case: 31484] move class to new file
BOOL VARefactorCls::CanMoveClassToNewFile(EdCntPtr ed, const DType* sym, const WTString& invokingScope)
{
#if defined(RAD_STUDIO)
	return FALSE;
#else
	if (!ed || !sym)
		return FALSE;

	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	if (sym->type() != CLASS && sym->type() != STRUCT)
		return FALSE;

	if (ed->m_ftype != Header && ed->m_ftype != Src)
		return FALSE;

	WTString fileBuf = ed->GetBuf();
	int wordStartPos = ed->GetBegWordIdxPos();
	WTString className = WTString(StrGetSym(sym->SymScope()));
	
	// [case: 164401] more robust check for class declaration based on parser state,
	//                so a macro between class name and class keyword doesn't break the check
	ScopeInfoPtr scopeInfo = ed->ScopeInfoPtr();
	if (!scopeInfo)
		return FALSE;

	if (scopeInfo->m_firstWord != "class" && scopeInfo->m_firstWord != "struct")
		return FALSE;

	// now check if it is forward declaration
	for (int inx = wordStartPos + className.length(); inx < fileBuf.length(); inx++)
	{
		char currentChar = fileBuf[inx];
		if (!IsWhiteSpace(currentChar))
		{
			if (currentChar == ';')
				return FALSE;
			
			break;
		}
	}
	
	return TRUE;
#endif
}

// [case: 31484] move class to new file
BOOL VARefactorCls::MoveClassToNewFile(EdCntPtr ed, const DType* sym, const WTString& invokingScope)
{
	TraceScopeExit tse("Move Class To New File exit");
	_ASSERTE(sym);
	
	if (!CanMoveClassToNewFile(ed, sym, invokingScope))
		return FALSE;

	MoveClassRefactorHelper moveClassRefactorHelper;

	// get class name and class scope (if class is in namespace)
	WTString classScope = StrGetSymScope(sym->SymScope());
	WTString className = WTString(StrGetSym(sym->SymScope()));
	if (className.IsEmpty())
		return FALSE;

	WTString fullNamespaceName;

	// if it is in namespace, get those namespaces
	int pos = 0;
	CStringW delimitedNamespace = classScope.Wide();
	CStringW tok = delimitedNamespace.Tokenize(L":", pos);
	while (!tok.IsEmpty())
	{
		fullNamespaceName += tok + L"::";
		moveClassRefactorHelper.NamespaceList.emplace_back(tok);
		tok = delimitedNamespace.Tokenize(L":", pos);
	}

	// use outline to parse class and it's related elements in the location of the class definition
	MultiParsePtr mparse = ed->GetParseDb();
	WTString sourceBuffer(ed->GetBuf(TRUE));
	LineMarkers sourceOutline;
	GetFileOutline(sourceBuffer, sourceOutline, mparse);

	LineMarkers::Node* pSourceOutlineNode = &sourceOutline.Root();
	
	for (const CStringW& namespc : moveClassRefactorHelper.NamespaceList)
	{
		// handle situation when class is in namespace(s)
		for (size_t idx = 0; idx < pSourceOutlineNode->GetChildCount(); idx++)
		{
			auto* pElem = &pSourceOutlineNode->GetChild(idx);
			if (pElem->Contents().mType == NAMESPACE)
			{
				if (pElem->Contents().mText == namespc)
				{
					pSourceOutlineNode = pElem;
					break;
				}
			}
			else
			{
				continue;
			}
		}
	}

	for (size_t idx = 0; idx < pSourceOutlineNode->GetChildCount(); idx++)
	{
		auto* pElem = &pSourceOutlineNode->GetChild(idx);

		// start of lambda function which will do recursive search under nested preprocessor directives
		std::function<void(TreeT<FileLineMarker>::Node*, std::vector<CStringW>)> SearchConditionalInclusions;
		SearchConditionalInclusions = [&](TreeT<FileLineMarker>::Node* pCurrentNode, std::vector<CStringW> tempPreprocList)
		{	
			if (pCurrentNode->Contents().mText.Find(L"#if") == 0 ||
			    pCurrentNode->Contents().mText.Find(L"#ifdef") == 0 ||
			    pCurrentNode->Contents().mText.Find(L"#ifndef") == 0 ||
			    pCurrentNode->Contents().mText.Find(L"#elif") == 0 ||
			    pCurrentNode->Contents().mText.Find(L"#else") == 0)
			{
				tempPreprocList.emplace_back(pCurrentNode->Contents().mText);

				// this is preprocessor conditional inclusion so find it's childes and go one level deeper
				for (size_t i = 0; i < pCurrentNode->GetChildCount(); i++)
				{
					auto* pSubElem = &pCurrentNode->GetChild(i);
					SearchConditionalInclusions(pSubElem, tempPreprocList);
				}
			}
			else
			{
				// this node is not preprocessor conditional inclusion, check if it is related to class
				if (pCurrentNode->Contents().mText == className ||
				    pCurrentNode->Contents().mText.Find((className + " :").Wide()) == 0)
				{
					// this is class declaration, get lines for move and store index of them, store also info about preprocessor conditional inclusion
					if (moveClassRefactorHelper.FoundResult == MoveClassRefactorHelper::ClassFindingStatus::Found)
					{
						// we already found one class body so mark this as duplicate since moving multiple definitions could create a mess (current limitation)
						moveClassRefactorHelper.FoundResult = MoveClassRefactorHelper::ClassFindingStatus::Duplicate;
						return;
					}

					moveClassRefactorHelper.MoveLocationListHdrClass = std::make_pair(pCurrentNode->Contents().mStartCp, pCurrentNode->Contents().mEndCp);
					moveClassRefactorHelper.FoundResult = MoveClassRefactorHelper::ClassFindingStatus::Found;
					moveClassRefactorHelper.PreprocessorList = tempPreprocList;
				}
			}	
		};
		// end of lambda function

		SearchConditionalInclusions(pElem, std::vector<CStringW>());

		// find method implementations but do not look into preprocessor conditional inclusion since that could potentially
		// create a total mess (this is current limit of move class functionality)
		if (pElem->Contents().mText == className + " methods")
		{
			// this is class method definition, go through each one and store their lines index
			for (size_t idx_sub = 0; idx_sub < pElem->GetChildCount(); idx_sub++)
			{
				auto* pSubElem = &pElem->GetChild(idx_sub);
				moveClassRefactorHelper.MoveLocationListHdrMethods.emplace_back(pSubElem->Contents().mStartCp, pSubElem->Contents().mEndCp);
			}
		}
	}

	if (moveClassRefactorHelper.FoundResult != MoveClassRefactorHelper::ClassFindingStatus::Found)
	{
		CString message;	
		switch (moveClassRefactorHelper.FoundResult)
		{
		case MoveClassRefactorHelper::ClassFindingStatus::NotFound:
			// unknown error (for some reason, VA failed to locate class body); hopefully this should not happen
			// TODO: come up with a smarter message to the users?
			message = "Move Class to new file failed. No changes will be done in code.";
			break;

		case MoveClassRefactorHelper::ClassFindingStatus::Duplicate:
			// current limitation
			message = "Multiple definitions of same class found under different preprocessor conditional inclusions. Class will not be moved due to current limitation of this functionality.";
			break;

		case MoveClassRefactorHelper::ClassFindingStatus::Found:
			break;
		}
		
		WtMessageBox(message, IDS_APPNAME, MB_OK | MB_ICONINFORMATION);

		if (gTestLogger)
		{
			// AST test scenario
			WTString logMsg(message);
			gTestLogger->LogStr(logMsg);
		}

		return FALSE;
	}

	// check if class body is under #elif or #else; if yes, do not perform move (current limitation)
	for (const auto& preproc : moveClassRefactorHelper.PreprocessorList | std::views::reverse)
	{
		if (preproc.Find(L"#elif") == 0 ||
		    preproc.Find(L"#else") == 0)
		{
			// TODO: come up with a smarter message to the users?
			CString message = "Class body found under else preprocessor conditional inclusion. Due to current limitation of this functionality, class will not be moved.";
			WtMessageBox(message, IDS_APPNAME, MB_OK | MB_ICONINFORMATION);

			if (gTestLogger)
			{
				// AST test scenario
				WTString logMsg(message);
				gTestLogger->LogStr(logMsg);
			}

			return FALSE;
		}
	}

	// first create header file with class declaration
	CStringW classFileName(className.Wide() + L".h");
	BOOL hdrFileSuccess = ::RefactorDoCreateFile(CreateFileDlg::DlgMoveClassToNewFile, VARef_MoveClassToNewFile, ed, false, classFileName, WTString(), moveClassRefactorHelper);
	if (!hdrFileSuccess)
		return FALSE;	// if class was not successfully moved to new header file then there is no point to continue

	EdCntPtr oppEd = ed;
	if (ed->m_ftype == Header)
	{
		// if class declaration is in header file, check opposite file for eventual definitions
		ed->OpenOppositeFile();

		oppEd = g_currentEdCnt;
		if (oppEd != ed)
		{
			// we are sure that opposite file is opened at this point, let's search if it contains something related to class
			MultiParsePtr mparseOpp = oppEd->GetParseDb();
			WTString sourceBufferOpp(oppEd->GetBuf(TRUE));
			LineMarkers sourceOutlineOpp;
			GetFileOutline(sourceBufferOpp, sourceOutlineOpp, mparseOpp);

			LineMarkers::Node* pSourceOutlineNodeOpp = &sourceOutlineOpp.Root();
			LineMarkers::Node* pSourceOutlineNodeOppNamespc = pSourceOutlineNodeOpp;

			// if the class is in namespace(s) we have two possible scenarios (need to cover both, can be mixed):
			// first, class member implementations are also in namespace (less common)
			// second, class member implementation is in form namespaceX::nemaspaceY::class

			// first case, implementations in namespace
			if (moveClassRefactorHelper.NamespaceList.size() > 0)
			{
				for (const CStringW& namespc : moveClassRefactorHelper.NamespaceList)
				{
					// handle situation when class implementations are in namespace(s)
					for (size_t idx = 0; idx < pSourceOutlineNodeOppNamespc->GetChildCount(); idx++)
					{
						auto* pElem = &pSourceOutlineNodeOppNamespc->GetChild(idx);
						if (pElem->Contents().mType == NAMESPACE)
						{
							if (pElem->Contents().mText == namespc)
							{
								pSourceOutlineNodeOppNamespc = pElem;
								break;
							}
						}
						else
						{
							continue;
						}
					}
				}

				for (size_t idx = 0; idx < pSourceOutlineNodeOppNamespc->GetChildCount(); idx++)
				{
					auto* pElem = &pSourceOutlineNodeOppNamespc->GetChild(idx);

					if (pElem->Contents().mText == className + " methods")
					{
						// this is class method definition, go through each one and store their lines index
						for (size_t idx_sub = 0; idx_sub < pElem->GetChildCount(); idx_sub++)
						{
							auto* pElemSub = &pElem->GetChild(idx_sub);
							moveClassRefactorHelper.MoveLocationListSrcNamespc.emplace_back(pElemSub->Contents().mStartCp, pElemSub->Contents().mEndCp);
						}
					}
				}
			}

			// second case, implementation in form namespaceX::nemaspaceY::class::method or just class::method
			for (size_t idx = 0; idx < pSourceOutlineNodeOpp->GetChildCount(); idx++)
			{
				auto* pElem = &pSourceOutlineNodeOpp->GetChild(idx);
				WTString elementText = pElem->Contents().mText;
				
				if (pElem->Contents().mText == fullNamespaceName + className + " methods")
				{
					// this is class method definition, go through each one and store their lines index
					for (size_t idx_sub = 0; idx_sub < pElem->GetChildCount(); idx_sub++)
					{
						auto* pElemSub = &pElem->GetChild(idx_sub);
						moveClassRefactorHelper.MoveLocationListSrc.emplace_back(pElemSub->Contents().mStartCp, pElemSub->Contents().mEndCp);
					}
				}
			}
		}
	}

	// now create source file with method definitions (if found in opposite file)
	BOOL srcFileSuccess = TRUE;
	if (!moveClassRefactorHelper.MoveLocationListSrc.empty() || !moveClassRefactorHelper.MoveLocationListSrcNamespc.empty())
	{
		if (IDYES ==
		    WtMessageBox(
		        "Move Class to New File identified class member implementations suitable for move to a new source file.\r\n\r\n"
		        "Would you like to move them?",
		        IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
		{
			WTString sourceFileName = moveClassRefactorHelper.HeaderFileName;
			if (sourceFileName.EndsWith(".h"))
				sourceFileName = sourceFileName.substr(0, sourceFileName.length() - 2);

			classFileName = sourceFileName.Wide() + L".cpp";
			moveClassRefactorHelper.IsSourceFile = true;
			srcFileSuccess = ::RefactorDoCreateFile(CreateFileDlg::DlgMoveClassToNewFile, VARef_MoveClassToNewFile, oppEd, false, classFileName, WTString(), moveClassRefactorHelper);
		}
	}

	return srcFileSuccess;
}

BOOL VARefactorCls::CanSortClassMethods(EdCntPtr ed, const DType* sym)
{
	return MethodsSorting::CanSortMethods(ed, sym) ? TRUE : FALSE;
}

BOOL VARefactorCls::SortClassMethods(EdCntPtr ed, const DType* sym)
{
	return MethodsSorting::SortMethods(ed, sym) ? TRUE : FALSE;
}

BOOL VARefactorCls::CanIntroduceVariable(bool select) const
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	IntroduceVariable iv(ed->m_ftype);
	return iv.CanIntroduce(select);
}

BOOL VARefactorCls::IntroduceVar()
{
	TraceScopeExit tse("Introduce Variable exit");

	if (!CanIntroduceVariable(true))
		return FALSE;
	UndoContext undoContext("IntroduceVariable");
	IntroduceVariable introduce(g_currentEdCnt->m_ftype);
	return introduce.Introduce();
}

// #AddRemoveBraces
BOOL VARefactorCls::CanAddRemoveBraces(EdCntPtr ed, WTString* outMenuText)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	WTString fileBuf = ed->GetBuf();
	long curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
	if (IntroduceVariable::FindOpeningBraceAfterWhiteSpace(curPos, fileBuf) != -1)
	{
		BOOL canRemove = CanRemoveBraces();
		if (canRemove)
		{
			if (outMenuText)
				*outMenuText = "Remo&ve Braces";
			return TRUE;
		}
		else
		{
			BOOL canAdd = CanAddBraces();
			if (canAdd)
			{
				if (outMenuText)
					*outMenuText = "Add Brace&s";
				return TRUE;
			}
		}
	}
	else
	{
		BOOL canAdd = CanAddBraces();
		if (canAdd)
		{
			if (outMenuText)
				*outMenuText = "Add Brace&s";
			return TRUE;
		}
		else
		{
			BOOL canRemove = CanRemoveBraces();
			if (canRemove)
			{
				if (outMenuText)
					*outMenuText = "Remo&ve Braces";
				return TRUE;
			}
		}
	}

	if (outMenuText)
		*outMenuText = "Add/Remove Brace&s";
	return FALSE;
}

BOOL VARefactorCls::CanAddBraces()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	IntroduceVariable iv(ed->m_ftype);
	return iv.AddBraces(true);
}

BOOL VARefactorCls::CanRemoveBraces()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	IntroduceVariable iv(ed->m_ftype);
	return iv.RemoveBraces(true);
}

BOOL VARefactorCls::AddRemoveBraces(EdCntPtr ed)
{
	TraceScopeExit tse("AddRemoveBraces exit");

	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	WTString fileBuf = ed->GetBuf();
	long curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
	if (IntroduceVariable::FindOpeningBraceAfterWhiteSpace(curPos, fileBuf) != -1)
	{
		BOOL remove = CanRemoveBraces();
		if (remove)
		{
			UndoContext undoContext("Remove braces");
			IntroduceVariable iv(ed->m_ftype);
			return iv.RemoveBraces(false);
		}
		else
		{
			return AddBraces();
		}
	}
	else
	{
		BOOL add = CanAddBraces();
		if (add)
		{
			UndoContext undoContext("Add braces");
			IntroduceVariable iv(ed->m_ftype);
			return iv.AddBraces(false);
		}
		else
		{
			return RemoveBraces();
		}
	}

	return FALSE;
}

BOOL VARefactorCls::AddBraces()
{
	BOOL add = CanAddBraces();
	if (add)
	{
		UndoContext undoContext("Add braces");
		IntroduceVariable iv(g_currentEdCnt->m_ftype);
		return iv.AddBraces(false);
	}

	return FALSE;
}

BOOL VARefactorCls::RemoveBraces()
{
	ULONG depth = mInfo->Depth();
	if (depth > 0)
	{
		auto state = mInfo->State(depth - 1);
		ULONG defType = state.m_defType;
		if (defType == CLASS || defType == STRUCT)
		{
			if (gTestLogger)
				gTestLogger->LogStrW(L"Remove braces error: cannot remove the braces of a class or struct");
			else
				WtMessageBox("Cannot remove the braces of a class or struct", IDS_APPNAME,
				             MB_OK | MB_ICONERROR);

			return FALSE;
		}
	}

	int methodLine = -1;
	for (ULONG i = 0; i < depth; i++)
	{
		auto state = mInfo->State(i);
		ULONG defType = state.m_defType;
		if (defType == Lambda_Type || defType == FUNC)
			methodLine = (int)state.m_StatementBeginLine;
	}

	BOOL remove = CanRemoveBraces();
	if (remove)
	{
		UndoContext undoContext("Remove braces");

		IntroduceVariable iv(g_currentEdCnt->m_ftype);
		return iv.RemoveBraces(false, methodLine);
	}

	return FALSE;
}

// #CreateMissingCases
BOOL VARefactorCls::CanCreateCases()
{
	CreateMissingCases cmc;
	return cmc.CanCreate();
}

BOOL VARefactorCls::CreateCases()
{
	BOOL create = CanCreateCases();
	if (create)
	{
		UndoContext undoContext("Create Missing Switch Cases");
		CreateMissingCases cmc;
		return cmc.Create();
	}

	return FALSE;
}

// [case: 117140] add "Insert VA Snippet..." to Shift+Alt+Q menu
void VARefactorCls::InsertVASnippet()
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
		ed->CodeTemplateMenu();
}

void VARefactorCls::OverrideCurSymType(const WTString& symType)
{
	mInfo->SetCurSymType(symType);
}

bool VARefactorCls::GetSelBoundaries(LineMarkers::Node& node, ULONG ln, const WTString& name,
                                     std::pair<ULONG, ULONG>& boundaries)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		if ((int)ln >= marker.mStartLine && (int)ln <= marker.mEndLine)
		{
			if (marker.mType == FUNC)
			{
				WTString curName = ::TokenGetField(WTString(marker.mText).c_str(), "(");
				curName.ReplaceAll("operator", "", TRUE);
				curName.Trim();
				if (curName == name)
				{
					boundaries.first = (ULONG)marker.mStartCp;
					boundaries.second = (ULONG)marker.mEndCp;
					return true;
				}
			}
		}
		// if (marker.mType == CLASS || marker.mType == STRUCT || marker.mType == RESWORD || marker.mType == NAMESPACE
		// || marker.mType == DEFINE) {
		if (GetSelBoundaries(ch, ln, name, boundaries))
			return true;
		//}
	}

	return false;
}

BOOL VARefactorCls::CanMoveImplementationToHdrFile(const DType* sym, const WTString& invokingScope, BOOL force /*= FALSE*/,
                                                   WTString* outMenuText /*= nullptr*/,
                                                   int* nrOfOverloads /*= nullptr*/, WTString* defPath /*= nullptr*/)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (ed->m_ftype != Header && ed->m_ftype != Src)
		return FALSE;

	if (force)
		return true;

	if (!sym || sym->IsEmpty())
		return FALSE;

	if (sym->type() != FUNC)
		return FALSE;

	if (sym->SymScope().IsEmpty())
		return FALSE;

	WTString symName = StrGetSym(sym->SymScope());
	
	bool freeFunc = ::IsFreeFunc(sym);
	if (outMenuText)
	{
		if (freeFunc)
			*outMenuText = "&Move Implementation of '" + symName + "' to Header File";
		else
			*outMenuText = "&Move Implementation of '" + symName + "' to Class Declaration";
	}

	if (sym->IsImpl())
	{
		WTString scope = sym->Scope();
		if (scope.GetLength())
		{
			if (scope == invokingScope)
				return FALSE;
		}
		if (freeFunc && ed->m_ftype == Header)
			return FALSE;
	}
	else
	{
		return false;
	}

	if (DType::IsLocalScope(invokingScope))
	{
		// [case: 12800] [case: 58329] invokingScope is a bit hosed for Func in
		// STDMETHOD(Func)();
		// it comes out like :Foo:STDMETHOD-104:
		// hack workaround just for STDMETHOD* macros
		if (-1 == invokingScope.Find("STDMETHOD"))
			return FALSE;
	}

	if (!::IsExternalClassMethod(sym) && !freeFunc)
		return FALSE;

	// how many existing declarations are?
	DTypeList lst;
	MultiParsePtr pmp = ed->GetParseDb();
	pmp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst);
	lst.GetStrs();
	DTypeList types;

	// collecting symbol declarations
	for (auto it = lst.begin(); it != lst.end(); ++it)
	{
		DType& dt = *it;
		if ((!freeFunc && !IsExternalClassMethod(&dt)) || (freeFunc && ::GetFileType(dt.FilePath()) == Header))
		{
			types.push_back(&dt);
			if (defPath)
				*defPath = dt.FilePath();
		}
	}

	types.FilterDupesAndGotoDefs();
	types.FilterEquivalentDefs();

	// 	// sorting symbol declarations by DbOffset
	// 	std::sort(types.begin(), types.end(), [](DType* a, DType* b)
	// 	{
	// 		return a->GetDbOffset() < b->GetDbOffset();
	// 	}
	// 	);
	//
	// 	// removing duplicates
	// 	for (uint i = 1; i < types.size(); i++) {
	// 		DType* t1 = types[i - 1];
	// 		DType* t2 = types[i];
	// 		if (t1->FileId() == t2->FileId() && t1->Line() == t2->Line()) {
	// 			types.erase(types.begin() + i);
	// 			i--;
	// 		}
	// 	}

	if (nrOfOverloads)
		*nrOfOverloads = (int)types.size();
	if (types.size() > 1) // if more than 1, we need support for overloads, we don't have it yet.
		return FALSE;

	// disabling the command on complex signatures - more than 1 () pair
	if (types.size() == 1)
	{
		const WTString str = types.front().Def();
		CommentSkipper cs(ed->m_ftype);
		int parens = 0;
		int counter = 0;
		for (int i = 0; i < str.GetLength(); i++)
		{
			TCHAR c = str[i];
			if (cs.IsCode(c))
			{
				if (c == '(')
					parens++;
				if (c == ')')
				{
					parens--;
					if (parens == 0)
						counter++;
				}
			}
		}
		if (counter > 1)
			return FALSE;
	}

	return TRUE;
}

bool GetAddIncludeLineNumber(const uint curFileId, const uint SymFileId, const int userAtLine, int& outInsertAtLine,
                             int searchForDuplicates)
{
	// searchForDuplicates will look if include file is already included within files included from the curFileId. It
	// doesn't go into the depth. Good values would be: 0 to disable, 1 to look only within the first file (typically
	// stdafx) or std::numeric_limits<int>::max() for all

	outInsertAtLine = 0;

	if (SymFileId == curFileId)
		return false;

	// get list of includes for cur file id
	DTypeList incList;
	IncludesDb::GetIncludes(curFileId, DTypeDbScope::dbSolution, incList);
	incList.sort([](const DType& a, const DType& b) { return a.Line() < b.Line(); });

	std::list<int> incDirectiveLineNumbers;
	incDirectiveLineNumbers.push_back(0);

	// make sure id is not already included; figure out insert line
	for (DTypeList::iterator it = incList.begin(); it != incList.end(); ++it)
	{
		UINT fId = 0;
		CStringW curInc((*it).Def().Wide());
		// pull id out of curInc to compare to outSymFileId
		int pos = curInc.Find(L"fileid:");
		if (pos != -1)
		{
			_ASSERTE(!pos);
			curInc = curInc.Mid(pos + 7);
			::swscanf(curInc, L"%x", &fId);

			if (fId == SymFileId)
				return false;

			if (searchForDuplicates-- > 0)
			{
				DTypeList incList2;
				IncludesDb::GetIncludes(fId, DTypeDbScope::dbSolution, incList2);
				for (auto& inc2 : incList2)
				{
					UINT fId2 = 0;
					CStringW curInc2(inc2.Def().Wide());
					int pos2 = curInc2.Find(L"fileid:");
					if (pos2 == -1)
						continue;
					_ASSERTE(!pos2);
					curInc2 = curInc2.Mid(pos2 + 7);
					::swscanf(curInc2, L"%x", &fId2);

					if (fId2 == SymFileId)
						return false;
				}
			}
		}
#ifdef RAD_STUDIO
		else if (EndsWith(curInc, " (unresolved)"))
		{
			// for unsaved files, symbol with filename will end with "(unresolved)"
			const CStringW filename2(gFileIdManager->GetFile(SymFileId));

			CStringW curInc2 = curInc.Left(curInc.GetLength() - 13);
			if (!StartsWith(curInc2, L"\\"))
				curInc2.Insert(0, L'\\');

			if(EndsWith(filename2, curInc2))
				return false;
		}
#endif

		int curIncLine = (*it).Line();
		if (curIncLine < userAtLine)
		{
			if (fId)
				curInc = gFileIdManager->GetFile(fId);

			curInc.MakeLower();
			if (curInc.Find(L".generated.h") != -1)
			{
				// [case: 111334]
				if (incDirectiveLineNumbers.size() == 1)
				{
					// use the previous line if this is the first include
					incDirectiveLineNumbers.push_back(curIncLine - 1);
				}
				else
					; // don't use this line

				continue;
			}

			incDirectiveLineNumbers.push_back(curIncLine);
		}
	}

	// prefer insert at top of file (at end of grouped includes) rather than
	// after a stray #include in the middle of a file
	for (std::list<int>::iterator it = incDirectiveLineNumbers.begin(); it != incDirectiveLineNumbers.end(); ++it)
	{
		int curIncLine = *it;
		if (outInsertAtLine && curIncLine > (outInsertAtLine + 20))
			break;
		outInsertAtLine = curIncLine;
	}

	outInsertAtLine++;

	const CStringW filename(gFileIdManager->GetFile(curFileId));
	const int fType = GetFileType(filename);
	if (Header != fType && Src != fType)
	{
		_ASSERTE(!"don't run add include in non-c files");
		return false;
	}

	EdCntPtr ed(g_currentEdCnt);
	if (Header == fType && ed)
	{
		// [case: 29480] skip pragmas at top of file
		WTString lnTxt = ed->GetLine(outInsertAtLine);
		while (lnTxt.Find("pragma ") != -1)
			lnTxt = ed->GetLine(++outInsertAtLine);

		// [case: 29480] specifically look for "pragma once"
		int pragPos = ed->GetBuf().Find("#pragma once");
		if (-1 != pragPos)
		{
			int ln = ed->LineFromChar(pragPos);
			if (ln > 0 && outInsertAtLine <= ln)
				outInsertAtLine = ln + 1;
		}
	}

	return true;
}

// #refactor_convert VARefactorCls::CanConvert()
BOOL VARefactorCls::CanConvertInstance(WTString* outMenuText) const
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	ConvertBetweenPointerAndInstance convert;
	BOOL can = convert.CanConvert(false);
	if (can && outMenuText)
	{
		convert.UpdateTypeOfMethodCall(); // this can be removed if performance is a problem but it means less accurate
		                                  // result which means that the context menu can say different direction
		                                  // compared to what happens when executing the command
		eConversionType figuredOutConversionType = convert.FigureOutConversionType(false);
		*outMenuText =
		    (figuredOutConversionType == eConversionType::POINTER_TO_INSTANCE ? "C&onvert Pointer to Instance..."
		                                                                      : "C&onvert Instance to Pointer...");
	}

	return can;
}

BOOL VARefactorCls::CanSimplifyInstanceDeclaration() const
{
	EdCntPtr ed(g_currentEdCnt);
	if (!GlobalProject || GlobalProject->IsBusy() || !ed)
		return FALSE;

	ConvertBetweenPointerAndInstance convert;
	BOOL can = convert.CanConvert(true);
	if (can)
	{
		convert.UpdateTypeOfMethodCall();
		eConversionType figuredOutConversionType = convert.FigureOutConversionType(true);
		return figuredOutConversionType == eConversionType::POINTER_TO_INSTANCE;
	}

	return false;
}

BOOL VARefactorCls::CanConvertEnum()
{
	EnumConverter ce;
	return ce.CanConvert();
}

typedef std::function<void(WTString, int, BOOL, int)> AddItemFnc;
typedef std::function<void(CStringW, int, BOOL, int)> AddItemWFnc;

void FillRefactorMenu(bool isCtxMnu, AddItemFnc AddItem, AddItemWFnc AddItemW, int& count, BOOL& doSep,
                      VARefactorCls& rfctr, DType* sym, WTString invokingScope, EdCntPtr curEd, CreateFromUsage& cfu,
                      ImplementMethods& im, ChangeSignature& cs)
{
	// Most common first.
	CStringW incFile;
	WTString binding;
	WTString cmdText;
	int atLine;

	WTString symName = (sym != nullptr && !sym->IsEmpty()) ? StrGetSym(sym->SymScope()) : "";

	{
		cmdText = "&Move Implementation of '" + symName + "' to Source File";
		BOOL canMoveImpl = rfctr.CanMoveImplementationToSrcFile(sym, invokingScope, FALSE, &cmdText);
		AddItem(cmdText, VARef_MoveImplementationToSrcFile, canMoveImpl, ICONIDX_NONE);
	}

	AddItem("&Create Declaration for '" + symName + "'", VARef_CreateMethodDecl, rfctr.CanCreateDeclaration(sym, invokingScope),
	        ICONIDX_NONE);

	{
		cmdText = "Move Implementation of '" + symName + "' to &Header File";
		BOOL canMoveImpl = rfctr.CanMoveImplementationToHdrFile(sym, invokingScope, FALSE, &cmdText);
		AddItem(cmdText, VARef_MoveImplementationToHdrFile, canMoveImpl, ICONIDX_NONE);
	}

	cmdText = "Create &Implementation for '" + symName + "'";
	BOOL canCreateImpl = rfctr.CanCreateImplementation(sym, invokingScope, &cmdText);
	AddItem(cmdText, VARef_CreateMethodImpl, canCreateImpl, ICONIDX_NONE);

	cmdText = "Re&name '" + symName + "'...";
	binding = GetBindingTip("VAssistX.RefactorRename", "Alt+Shift+R", FALSE);
	if (!binding.IsEmpty() && !gTestsActive)
		cmdText += "\t" + binding;
	AddItem(cmdText, VARef_Rename, rfctr.CanRename(sym), ICONIDX_REFACTOR_RENAME);

	cmdText = "&Promote Lambda BETA";
	AddItem(cmdText, VARef_PromoteLambda, rfctr.CanPromoteLambda(), ICONIDX_NONE);

	if (curEd)
		AddItem("Rename &File to Match Type...", VARef_RenameFilesFromRefactorTip,
		        rfctr.CanRenameFiles(curEd->FileName(), sym, invokingScope, true), ICONIDX_NONE);

	const BOOL canAddInclude = rfctr.GetAddIncludeInfo(atLine, incFile);
	if (canAddInclude)
	{
		CStringW cmdTextW = "Add incl&ude ";
		cmdTextW += ::Basename(incFile);
		AddItemW(cmdTextW, VARef_AddInclude, canAddInclude, ICONIDX_NONE);
	}
	const BOOL canAddForwardDeclaration = rfctr.CanAddForwardDeclaration();
	if (canAddForwardDeclaration)
	{
		CStringW cmdTextW = "Add For&ward Declaration";
		AddItemW(cmdTextW, VARef_AddForwardDeclaration, canAddForwardDeclaration, ICONIDX_NONE);
	}
	AddItem("Encapsulate &Field", VARef_EncapsulateField, rfctr.CanEncapsulateField(sym),
	        ICONIDX_REFACTOR_ENCAPSULATE_FIELD);
	BOOL canConvert = rfctr.CanConvertInstance(&cmdText);
	if (canConvert)
		AddItem(cmdText, VARef_ConvertBetweenPointerAndInstance, 1, ICONIDX_NONE);

	BOOL canSimplify = rfctr.CanSimplifyInstanceDeclaration();
	if (canSimplify)
		AddItem("Sim&plify Instance Definition", VARef_SimplifyInstance, 1, ICONIDX_NONE);

	BOOL canConvertEnum = rfctr.CanConvertEnum();
	if (canConvertEnum)
		AddItem("&Convert Unscoped Enum to Scoped Enum", VARef_ConvertEnum, TRUE, ICONIDX_NONE);

	if (sym && !sym->IsSysLib())
	{
		if (VARefactorCls::CanAddMember(sym))
			AddItem("&Add Member to '" + symName + "'...", VARef_AddMember, TRUE, ICONIDX_NONE);

		if (VARefactorCls::CanAddSimilarMember(sym))
		{
			WTString baseClass = StrGetSym(StrGetSymScope(sym->SymScope()));
			if (baseClass.GetLength())
			{
				AddItem(WTString("Add &Similar Member to '") + baseClass + "'...", VARef_AddMember, TRUE, ICONIDX_NONE);
			}
		}
	}

	if (cfu.CanCreate())
	{
		cmdText = cfu.GetCommandText();
		_ASSERTE(!cmdText.IsEmpty());
		binding = GetBindingTip("VAssistX.RefactorCreateFromUsage", "Alt+Shift+C", FALSE);
		if (!binding.IsEmpty() && !gTestsActive)
			cmdText += "\t" + binding;
		AddItem(cmdText, VARef_CreateFromUsage, 1, ICONIDX_NONE);
	}

	if (im.CanImplementMethods())
	{
		cmdText = im.GetCommandText();
		_ASSERTE(!cmdText.IsEmpty());
		AddItem(cmdText, VARef_ImplementInterface, 1, ICONIDX_NONE);
	}

	AddItem("Sort Methods &By Header File (beta)", VARef_SortClassMethods, rfctr.CanSortClassMethods(curEd, sym), ICONIDX_NONE);

	if (sym && !sym->IsSysLib())
	{
		if (cs.CanChange(sym))
			AddItem("Change Si&gnature of '" + symName + "()'...", VARef_ChangeSignature, TRUE, ICONIDX_REFACTOR_CHANGE_SIGNATURE);
	}

	AddItem("&Document Method", VARef_CreateMethodComment, rfctr.CanCreateMethodComment(), ICONIDX_NONE);
	AddItem("&Extract Method...", VARef_ExtractMethod, rfctr.CanExtractMethod(), ICONIDX_REFACTOR_EXTRACT_METHOD);
	AddItem("&Override Base Method", VARef_OverrideMethod, FALSE /*baseClass.GetLength()*/, ICONIDX_NONE);

#if !defined(RAD_STUDIO)
	if (curEd)
	{
		AddItem("Mo&ve Selection to New File...", VARef_MoveSelectionToNewFile, rfctr.CanMoveSelectionToNewFile(curEd),
		        ICONIDX_NONE);
		AddItem("Modify E&xpression...", VARef_ModifyExpression, CanSmartSelect(icmdVaCmd_RefactorModifyExpression),
		        ICONIDX_NONE);
		AddItem("Move &Class to New File...", VARef_MoveClassToNewFile, rfctr.CanMoveClassToNewFile(curEd, sym, invokingScope),
		        ICONIDX_NONE);
	}
#endif

#if !defined(RAD_STUDIO)
	const BOOL canFindRefs = rfctr.CanFindReferences(sym);
	if (!canFindRefs && !count && isCtxMnu)
	{
		AddItem("Crea&te File...", VARef_CreateFile, true, ICONIDX_NONE);
		AddItem("Rename &Files...", VARef_RenameFilesFromMenuCmd, true, ICONIDX_NONE);
	}
#endif

	cmdText = "Introduce Varia&ble...";
	BOOL canIntroduceVariable = rfctr.CanIntroduceVariable(false);
	AddItem(cmdText, VARef_IntroduceVariable, canIntroduceVariable, ICONIDX_NONE);

	if (curEd)
	{
		cmdText = "Add Brace&s";
		BOOL canAddBraces = rfctr.CanAddRemoveBraces(curEd, &cmdText);
		AddItem(cmdText, VARef_AddRemoveBraces, canAddBraces, ICONIDX_NONE);
	}

	AddItem("Add &Missing Case Statements", VARef_CreateMissingCases, rfctr.CanCreateCases(), ICONIDX_NONE);

#ifdef _DEBUG
	if (!gTestsActive)
	{
		AddItem("DEBUG: Expand Macro", VARef_ExpandMacro, rfctr.CanExpandMacro(sym, invokingScope), ICONIDX_NONE);
	}
#endif

	doSep = TRUE;
#if !defined(RAD_STUDIO)
	// the following needs a toolwindow
	cmdText = "Find &References";
	binding = GetBindingTip("VAssistX.FindReferences", "Alt+Shift+F", FALSE);
	if (!binding.IsEmpty() && !gTestsActive)
		cmdText += "\t" + binding;
	AddItem(cmdText, VARef_FindUsage, canFindRefs, ICONIDX_REFERENCE_FIND_REF);

	if (canFindRefs)
	{
		// [case: 52561]
		AddItem("Find References in Fi&le", VARef_FindUsageInFile, canFindRefs, ICONIDX_REFERENCE_FIND_REF);
	}
#endif

#define WM_VA_TRY_CATCH (VARef_Count + 10)
#ifdef _DEBUG_HOLDING_OFF
	if (curEd && curEd->GetSelString().GetLength() > 0)
	{
		doSep = TRUE;
		AddItem("// Comment selection", WM_VA_COMMENTLINE, TRUE, ICONIDX_NONE);
		AddItem("/* Comment selection */", WM_VA_COMMENTBLOCK, TRUE, ICONIDX_NONE);
		AddItem("#ifdef selection", WM_VA_IFDEF, TRUE, ICONIDX_NONE);
		AddItem("try{ selection }", WM_VA_TRY_CATCH, TRUE, ICONIDX_NONE);
	}
#endif // _DEBUG

	if (!count && !isCtxMnu)
	{
		// if (gTestsActive)
		//{
		//	if (gTestLogger && gTestLogger->IsMenuLoggingEnabled())
		//		gTestLogger->LogStr(WTString("MenuItem: Refactoring not available on symbol"));
		//	return;
		//}
		// xpmenu.AddMenuItem(100, MF_BYPOSITION|MF_GRAYED, "Refactoring not available on symbol");

		if (GetIsCursorOnIncludeDirective())
		{
			// [case: 114572] enhance alt+shift+q quick menu when exec'd on #include directive
			AddItem("&Goto", VARef_GotoInclude, true, ICONIDX_NONE);
			AddItem("&Open Containing Folder", VARef_OpenFileLocation, true, ICONIDX_NONE);

#if !defined(RAD_STUDIO)
			doSep = TRUE;

			if (rfctr.CanDisplayIncludes())
				AddItem("&List Include Files", VARef_DisplayIncludes, true, ICONIDX_NONE);

			AddItem("Crea&te File...", VARef_CreateFile, true, ICONIDX_NONE);
#endif
		}
#if !defined(RAD_STUDIO)
		else
		{
			// [case: 117140] add "Insert VA Snippet..." to Shift+Alt+Q menu
			AddItem("&Insert VA Snippet...", VARef_InsertVASnippet, true, ICONIDX_NONE);
			doSep = TRUE;
			AddItem("Crea&te File...", VARef_CreateFile, true, ICONIDX_NONE);
			AddItem("Rename &Files...", VARef_RenameFilesFromMenuCmd, true, ICONIDX_NONE);
		}
#endif
	}

	if (gShellAttr->IsDevenv14OrHigher())
	{
		doSep = true;
		AddItem("Share code with...", VARef_ShareWith, true, ICONIDX_NONE);
	}
}

UINT_PTR ConvertEnumTimer = 0;

static void CALLBACK FinishConvertEnum(HWND hWnd = NULL, UINT ignore1 = 0, UINT_PTR idEvent = 0, DWORD ignore2 = 0)
{
	if (idEvent)
		KillTimer(hWnd, idEvent);

	ConvertEnumTimer = 0;

	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return;

	DTypePtr symBak;
	WTString invokingScope;
	GetRefactorSym(curEd, symBak, &invokingScope, false);
	DType* sym = symBak.get();

	EnumConverter ce;
	ce.Convert(sym);
}

void Refactor(UINT flag, LPCSTR orgSymscope /*= NULL*/, LPCSTR newName /*= NULL*/, LPPOINT ppt /*= NULL*/)
{
	vCatLog("Editor.Events", "VaEventRE Refactor  0x%x", flag);
	if (!GlobalProject || GlobalProject->IsBusy())
		return;

	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return;

	RefactoringActive active;
	DB_READ_LOCK;

	VARefactorCls rfctr;

	WTString invokingScope;
	DTypePtr symBak;
	GetRefactorSym(curEd, symBak, &invokingScope,
	               false); // no force reparse here. DB_READ_LOCK is set before calling GetRefactorSym -- if
	                       // GetRefactorSym causes a parse to occur in ReparseAndWaitIfNeeded, then the parsethread
	                       // will be blocked by the read lock on the UI thread
	DType* sym = symBak.get();

	CreateFromUsage cfu;
	ImplementMethods im;
	ChangeSignature cs;

	if (!flag || orgSymscope)
	{
		CPoint pt;
		if (ppt)
		{
			pt = *ppt;
		}
		else
		{
			GetCursorPos(&pt);
			_ASSERTE(g_FontSettings->GetCharHeight() && "$$MSD5");
			pt.y = (((pt.y - 10) / g_FontSettings->GetCharHeight()) + 1) * g_FontSettings->GetCharHeight();
		}

		if (sym && sym->type() == RESWORD)
		{
			WTString symToRight = WTString(StrGetSym(curEd->WordRightOfCursor()));
			symToRight.Trim();
			if (symToRight.IsEmpty())
			{
				WTString curwd = WTString(StrGetSym(curEd->CurWord()));
				curwd.Trim();
				if (curwd.IsEmpty())
				{
					// [case: 90088]
					sym = nullptr;
				}
			}
		}

		PopupMenuXP xpmenu;

		int count = 0;
		BOOL doSep = FALSE;

		auto AddItem = [&](WTString s, int cmd, BOOL ok, int icon) {
			const BOOL tmpOk = ok;
			if (!orgSymscope || tmpOk)
			{
				if (count++ && doSep)
					xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
				doSep = FALSE;
				xpmenu.AddMenuItemW((uint)cmd, MF_BYPOSITION | (tmpOk ? 0u : MF_GRAYED), (LPCWSTR)s.Wide(), (UINT)icon);
			}
		};

		auto AddItemW = [&](CStringW s, int cmd, BOOL ok, int icon) {
			const BOOL tmpOk = ok;
			if (!orgSymscope || tmpOk)
			{
				if (count++ && doSep)
					xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
				doSep = FALSE;
				xpmenu.AddMenuItemW((uint)cmd, MF_BYPOSITION | (tmpOk ? 0u : MF_GRAYED), s, (UINT)icon);
			}
		};

		FillRefactorMenu(false, AddItem, AddItemW, count, doSep, rfctr, sym, invokingScope, curEd, cfu, im, cs);

		curEd->PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list
		flag = (UINT)xpmenu.TrackPopupMenuXP(curEd.get(), pt.x, pt.y + 0);
	}

	curEd = g_currentEdCnt;
	if (!flag || !curEd)
		return;

	auto UeStripImplicitFromSymScope = [](const WTString& symScope) -> WTString {
		// [case: 141287] strip off *_Implementation or *_Validate to get the true method name
		if (symScope.EndsWith("_Implementation"))
			return symScope.Left(symScope.GetLength() - 15);
		else if (symScope.EndsWith("_Validate"))
			return symScope.Left(symScope.GetLength() - 9);
		else
			return symScope;
	};

	auto UeFindImplicitDef = [](const WTString& symScope, auto* dict) -> bool {
		// [case: 141287] return true if implicit definitions for a given symScope exist
		SymbolPosList symPosList;
		// symScope should be stripped of *_Implementation of *_Validate even if executed on an implicit method
		dict->FindDefList(symScope + "_Implementation", true, symPosList);
		if (!symPosList.size())
			dict->FindDefList(symScope + "_Validate", true, symPosList);
		return symPosList.size() > 0 ? true : false;
	};

	if (flag == VARef_FindUsage)
	{
#if !defined(RAD_STUDIO)
		MultiParsePtr mp(curEd->GetParseDb());
		sym = TraverseUsing(sym, mp.get());
		if (gVaService && sym)
		{
			auto symScope = sym->SymScope();
			// promote search for ctors to search for class
			if (sym->IsConstructor())
				symScope = ::StrGetSymScope(symScope);
			int findRefFlags = FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars;
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				symScope = UeStripImplicitFromSymScope(symScope);
				if (UeFindImplicitDef(symScope, GetSysDic()) || UeFindImplicitDef(symScope, g_pGlobDic))
					findRefFlags |= FREF_Flg_UeFindImplicit; // [case: 141287] also find references to implicit methods
			}
			gVaService->FindReferences(
			    findRefFlags, GetTypeImgIdx(curEd->GetSymDtypeType(), curEd->GetSymDtypeAttrs()), symScope);
		}
#endif
	}
	else if (flag == VARef_FindUsageInFile)
	{
		MultiParsePtr mp(curEd->GetParseDb());
		sym = TraverseUsing(sym, mp.get());
		if (gVaService && sym)
		{
			auto symScope = sym->SymScope();
			// promote search for ctors to search for class
			if (sym->IsConstructor())
				symScope = ::StrGetSymScope(symScope);
			int findRefFlags =
			    FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars | FREF_Flg_InFileOnly;
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				symScope = UeStripImplicitFromSymScope(symScope);
				if (UeFindImplicitDef(symScope, GetSysDic()) || UeFindImplicitDef(symScope, g_pGlobDic))
					findRefFlags |= FREF_Flg_UeFindImplicit; // [case: 141287] also find references to implicit methods
			}
			gVaService->FindReferences(
			    findRefFlags, GetTypeImgIdx(curEd->GetSymDtypeType(), curEd->GetSymDtypeAttrs()), symScope);
		}
	}
	else if (flag == VARef_Rename_References || flag == VARef_Rename_References_Preview)
	{
		if (curEd->m_lastEditSymScope.GetLength() && curEd->m_lastEditPos == (ULONG)curEd->GetBegWordIdxPos())
		{
			WTString newname = curEd->CurWord(); // curEd->WordRightOfCursor();
			WTString oldname = curEd->m_lastEditSymScope;
			BOOL ueRenameImplicit = FALSE;
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				newname = UeStripImplicitFromSymScope(newname);
				oldname = UeStripImplicitFromSymScope(oldname);
				if (UeFindImplicitDef(oldname, GetSysDic()) || UeFindImplicitDef(oldname, g_pGlobDic))
					ueRenameImplicit = TRUE; // [case: 141287] also rename references to implicit methods
			}
			NestedTrace nt("Rename References", true);
			// Search for all defs with dialog
			RenameReferencesDlg ren(oldname, newname.c_str(), flag == VARef_Rename_References, ueRenameImplicit);
			ren.DoModal();
		}
	}
	else if (flag == VARef_Rename)
	{
		// [case: 83847]
		MultiParsePtr mp(curEd->GetParseDb());
		sym = TraverseUsing(sym, mp.get());
		if (sym)
		{
			NestedTrace nt("Rename", true);

			auto symScope = sym->SymScope();
			BOOL ueRenameImplicit = FALSE;
			// promote search for ctors to search for class
			if (sym->IsConstructor())
				symScope = ::StrGetSymScope(symScope);
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				symScope = UeStripImplicitFromSymScope(symScope);
				if (UeFindImplicitDef(symScope, GetSysDic()) || UeFindImplicitDef(symScope, g_pGlobDic))
					ueRenameImplicit = TRUE; // [case: 141287] also rename references to implicit methods
			}

			RenameReferencesDlg ren(symScope, nullptr, FALSE, ueRenameImplicit);
			if (Psettings && Psettings->mUnrealEngineCppSupport)
			{
				ren.SetSymMaskedType(sym->MaskedType()); // [case: 147774] needed for UE core redirects
				ren.SetIsUEMarkedType(IsUEMarkedType(sym->SymScope().c_str())); // [case: 148145]
			}
			ren.DoModal();
		}
	}
	else if (flag == WM_VA_TRY_CATCH)
	{
		int id = gAutotextMgr->GetItemIndex("try { ... } catch {}");
		if (id > -1) // ?
		{
			if (!gAutotextMgr->Insert(curEd, id))
				OnRefactorErrorMsgBox();
		}
	}
	else if (flag == VARef_ExtractMethod) // Create Method From Selection
		rfctr.ExtractMethod();
	else if (flag == VARef_CreateMethodImpl) // Create Implementation
		rfctr.CreateImplementation(sym, invokingScope);
	else if (flag == VARef_MoveImplementationToSrcFile) // Move Implementation
		rfctr.MoveImplementationToSrcFile(sym, invokingScope);
	else if (flag == VARef_CreateMethodDecl)
		rfctr.CreateDeclaration(sym, invokingScope);
	else if (flag == VARef_CreateFromUsage)
		cfu.Create();
	else if (flag == VARef_ImplementInterface)
		im.DoImplementMethods();
	else if (flag == VARef_CreateMethodComment)
		rfctr.CreateMethodComment();
	else if (flag == VARef_EncapsulateField)
		rfctr.Encapsulate(sym);
	else if (flag == VARef_ChangeSignature)
	{
		if (!cs.ChangePreviouslyAllowed())
			cs.CanChange(sym);
		cs.Change();
	}
	else if (flag == VARef_AddMember || flag == VARef_AddSimilarMember)
	{
		if (sym && sym->ScopeHash() && (sym->MaskedType() == VAR || sym->MaskedType() == FUNC))
			VARefactorCls::AddMember(sym, sym);
		else
			VARefactorCls::AddMember(sym);
	}
	else if (flag == VARef_AddInclude)
		rfctr.AddInclude();
	else if (flag == VARef_AddForwardDeclaration)
		rfctr.AddForwardDecl();
	else if (flag == VARef_ExpandMacro)
		rfctr.ExpandMacro(sym, invokingScope);
	else if (flag == VARef_SmartSelect)
		SmartSelect();
	else if (flag == VARef_ModifyExpression)
		SmartSelect(icmdVaCmd_RefactorModifyExpression);
	else if (flag == VARef_RenameFilesFromRefactorTip)
		rfctr.RenameFiles(curEd->FileName(), sym, invokingScope, true);
	else if (flag == VARef_RenameFilesFromMenuCmd)
		rfctr.RenameFiles(curEd->FileName(), sym, invokingScope, false);
	else if (flag == VARef_RenameFilesFromRenameRefs)
		rfctr.RenameFiles(curEd->FileName(), sym, invokingScope, false, newName);
	else if (flag == VARef_CreateFile)
		rfctr.CreateFile(curEd);
	else if (flag == VARef_MoveSelectionToNewFile)
		rfctr.MoveSelectionToNewFile(curEd);
	else if (flag == VARef_FindErrorsInFile)
	{
		if (gVaService)
			gVaService->FindReferences(FREF_Flg_FindErrors | FREF_Flg_InFileOnly, ICONIDX_SUGGESTION, NULLSTR);
	}
	else if (flag == VARef_FindErrorsInProject)
	{
		if (gVaService)
			gVaService->FindReferences(FREF_Flg_FindErrors, ICONIDX_SUGGESTION, NULLSTR);
	}
	else if (flag == VARef_IntroduceVariable)
	{
		rfctr.IntroduceVar();
	}
	else if (flag == VARef_AddRemoveBraces)
	{
		rfctr.AddRemoveBraces(curEd);
	}
	else if (flag == VARef_AddBraces)
	{
		rfctr.AddBraces();
	}
	else if (flag == VARef_RemoveBraces)
	{
		rfctr.RemoveBraces();
	}
	else if (flag == VARef_CreateMissingCases)
	{
		rfctr.CreateCases();
	}
	else if (flag == VARef_MoveImplementationToHdrFile) // Move Implementation
		rfctr.MoveImplementationToHdrFile(sym, invokingScope);
	else if (flag == VARef_ConvertBetweenPointerAndInstance)
		rfctr.ConvertInstance(sym);
	else if (flag == VARef_SimplifyInstance)
		rfctr.SimplifyInstanceDeclaration(sym);
	else if (flag == VARef_ConvertEnum)
	{
		if (ConvertEnumTimer)
			KillTimer(nullptr, ConvertEnumTimer);
		ConvertEnumTimer = SetTimer(nullptr, 0, 750, (TIMERPROC)&FinishConvertEnum);
	}
	else if (flag == VARef_GotoInclude)
		rfctr.GotoInclude();
	else if (flag == VARef_OpenFileLocation)
		rfctr.OpenFileLocation();
	else if (flag == VARef_DisplayIncludes)
		rfctr.DisplayIncludes();
	else if (flag == VARef_InsertVASnippet)
		rfctr.InsertVASnippet();
	else if (flag == VARef_MoveClassToNewFile)
		rfctr.MoveClassToNewFile(curEd, sym, invokingScope);
	else if (flag == VARef_SortClassMethods)
		rfctr.SortClassMethods(curEd, sym);
	else if (flag == VARef_PromoteLambda)
		rfctr.PromoteLambda();
	else if (flag == VARef_ShareWith)
	{
		if (gShellAttr->IsDevenv14OrHigher())
		{
			bool ShareWith();
			ShareWith();
		}
	}
	else if (flag > WM_APP)
	{
		curEd->SendMessage(WM_COMMAND, flag);
	}
	else
	{
		WtMessageBox("Not yet implemented.", IDS_APPNAME, MB_OK);
	}

	curEd = g_currentEdCnt;
	if (curEd && curEd->m_FileHasTimerForFullReparse)
	{
		// Reparse ASAP, fixes AST create_from_usage reparse delay in vs2010, OnKillFocus sets asap in vs200x
		curEd->OnModified(TRUE);
	}
}

void CreateImplementation()
{
	WTString invokingScope;
	DTypePtr symBak;
	GetRefactorSym(g_currentEdCnt, symBak, &invokingScope, false);

	VARefactorCls rfctr;
	rfctr.CreateImplementation(symBak.get(), invokingScope);
}

BOOL DisplayDisabledRefactorCommand(RefactorFlag flag)
{
	if (!g_currentEdCnt)
		return FALSE;

	try
	{
		switch (flag)
		{
		case VARef_CreateMethodImpl:
		case VARef_CreateMethodDecl:
		case VARef_AddInclude:
		case VARef_MoveImplementationToSrcFile:
		case VARef_MoveImplementationToHdrFile:
		case VARef_ConvertBetweenPointerAndInstance:
		case VARef_SimplifyInstance:
		case VARef_AddForwardDeclaration:
			if (!(IsCFile(g_currentEdCnt->m_ftype)))
			{
				// [case: 47138] commands not applicable in the language so
				// don't even display them (even though they are disabled)
				return FALSE;
			}
		default:
			break;
		}
	}
	catch (...)
	{
	}

	return TRUE;
}

BOOL CanRefactor(RefactorFlag flag, WTString* outMenuText)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return false;

	DTypePtr symBak;
	DType* sym = NULL;

	try
	{
		WTString invokingScope;
		GetRefactorSym(curEd, symBak, &invokingScope, true);
		sym = symBak.get();

		// handle static queries separately to prevent unnecessary VARefactorCls instantiation
		switch (flag)
		{
		case VARef_EncapsulateField:
			return VARefactorCls::CanEncapsulateField(sym);
		case VARef_FindUsage:
		case VARef_FindUsageInFile:
			return VARefactorCls::CanFindReferences(sym);
		case VARef_Rename:
			return VARefactorCls::CanRename(sym);
		case VARef_PromoteLambda:
			return VARefactorCls::CanPromoteLambda();
		case VARef_AddMember:
			return VARefactorCls::CanAddMember(sym);
		case VARef_AddSimilarMember:
			return VARefactorCls::CanAddSimilarMember(sym);
		case VARef_ChangeSignature:
			return VARefactorCls::CanChangeSignature(sym);
		case VARef_ChangeVisibility:
			return VARefactorCls::CanChangeVisibility(sym);
		case VARef_OverrideMethod:
			return VARefactorCls::CanOverrideMethod(sym);
		case VARef_CreateFromUsage:
			return VARefactorCls::CanCreateMemberFromUsage();
		case VARef_ImplementInterface:
			return VARefactorCls::CanImplementInterface(outMenuText);
		case VARef_CreateMethodDecl:
			return VARefactorCls::CanCreateDeclaration(sym, invokingScope);
		case VARef_CreateMethodImpl:
			return VARefactorCls::CanCreateImplementation(sym, invokingScope, outMenuText);
		case VARef_ExpandMacro:
			return VARefactorCls::CanExpandMacro(sym, invokingScope);
		case VARef_SmartSelect:
			return CanSmartSelect();
		case VARef_ModifyExpression:
			return CanSmartSelect(icmdVaCmd_RefactorModifyExpression);
		case VARef_RenameFilesFromRefactorTip:
			return VARefactorCls::CanRenameFiles(curEd->FileName(), sym, invokingScope, true);
		case VARef_RenameFilesFromMenuCmd:
			return VARefactorCls::CanRenameFiles(curEd->FileName(), sym, invokingScope, false);
		case VARef_CreateFile:
			return VARefactorCls::CanCreateFile(curEd);
		case VARef_MoveSelectionToNewFile:
			return VARefactorCls::CanMoveSelectionToNewFile(curEd);
		case VARef_AddRemoveBraces:
			return VARefactorCls::CanAddRemoveBraces(curEd, outMenuText);
		case VARef_AddBraces:
			return VARefactorCls::CanAddBraces();
		case VARef_RemoveBraces:
			return VARefactorCls::CanRemoveBraces();
		case VARef_CreateMissingCases:
			return VARefactorCls::CanCreateCases();
		case VARef_GotoInclude:
			return VARefactorCls::CanGotoInclude();
		case VARef_OpenFileLocation:
			return VARefactorCls::CanOpenFileLocation();
		case VARef_DisplayIncludes:
			return VARefactorCls::CanDisplayIncludes();
		case VARef_AddForwardDeclaration:
			return VARefactorCls::CanAddForwardDeclaration();
		case VARef_ConvertEnum:
			return VARefactorCls::CanConvertEnum();
		case VARef_MoveClassToNewFile:
			return VARefactorCls::CanMoveClassToNewFile(curEd, sym, invokingScope);
		case VARef_SortClassMethods:
			return VARefactorCls::CanSortClassMethods(curEd, sym);
		default: {
			const VARefactorCls ref;
			return ref.CanRefactor(flag, outMenuText);
		}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAR:");
		try
		{
			if (curEd)
			{
				MultiParsePtr mp = curEd->GetParseDb();
				if (mp && mp->GetCwData().get() == sym)
					mp->ClearCwData();
			}
		}
		catch (...)
		{
		}
	}

	return false;
}

void FillVAContextMenuWithRefactoring(PopupMenuLmb& menu, size_t& find_refs_id, DTypePtr& refSym, bool isCtxMenu)
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return;

	EdCntPtr curEd(g_currentEdCnt);
	if (!curEd)
		return;

	ReparseAndWaitIfNeeded();

	RefactoringActive active;
	DB_READ_LOCK;

	VARefactorCls rfctr;

	WTString invokingScope;
	GetRefactorSym(curEd, refSym, &invokingScope,
	               false); // no force reparse here. DB_READ_LOCK is set before calling GetRefactorSym -- if
	                       // GetRefactorSym causes a parse to occur in ReparseAndWaitIfNeeded, then the parsethread
	                       // will be blocked by the read lock on the UI thread
	DType* sym = refSym.get();

	CreateFromUsage cfu;
	ImplementMethods im;
	ChangeSignature cs;

	if (sym && sym->type() == RESWORD)
	{
		WTString symToRight = WTString(StrGetSym(curEd->WordRightOfCursor()));
		symToRight.Trim();
		if (symToRight.IsEmpty())
		{
			WTString curwd = WTString(StrGetSym(curEd->CurWord()));
			curwd.Trim();
			if (curwd.IsEmpty())
			{
				// [case: 90088]
				sym = nullptr;
			}
		}
	}

	int count = 0;
	BOOL doSep = FALSE;

	auto AddItem = [&](WTString s, int cmd, BOOL ok, int icon) {
		const BOOL tmpOk = ok;
		if (tmpOk)
		{
			if (count++ && doSep)
				menu.AddSeparator();
			doSep = FALSE;

			menu.Items.push_back(MenuItemLmb(
			    s, [cmd] { Refactor((RefactorFlag)cmd); }, nullptr));
			menu.Items.back().SetIcon((UINT)icon);

			if (cmd == VARef_FindUsage)
				find_refs_id = menu.Items.size();
		}
	};

	auto AddItemW = [&](CStringW s, int cmd, BOOL ok, int icon) {
		const BOOL tmpOk = ok;
		if (tmpOk)
		{
			if (count++ && doSep)
				menu.AddSeparator();
			doSep = FALSE;
			menu.Items.push_back(MenuItemLmb(
			    s, [cmd] { Refactor((RefactorFlag)cmd); }, nullptr));
			menu.Items.back().SetIcon((UINT)icon);

			if (cmd == VARef_FindUsage)
				find_refs_id = menu.Items.size();
		}
	};

	FillRefactorMenu(isCtxMenu, AddItem, AddItemW, count, doSep, rfctr, sym, invokingScope, curEd, cfu, im, cs);
}

/*
void AddVATabTreeItems(PopupMenuLmb &mnu, EdCntPtr &cnt)
{
    _ASSERTE(!"unicode changes not tested");
    if (g_VATabTree)
    {
        HTREEITEM mitem = g_VATabTree->GetChildItem(TVI_ROOT);
        if (mitem)
            mitem = g_VATabTree->GetNextItem(mitem, TVGN_CHILD);
        for (; mitem; mitem = g_VATabTree->GetNextItem(mitem, TVGN_NEXT))
        {
            CStringW title = g_VATabTree->GetItemTextW(mitem);
            if (title != VAT_METHOD && title != VAT_FILE)
                continue;

            mnu.Items.push_back(MenuItemLmb(title, true));

            CStringW menuTxt;
            HTREEITEM item = g_VATabTree->GetChildItem(mitem);
            int itemCnt = 0;
            while (item)
            {
                menuTxt = ::BuildMenuTextHexAcceleratorW(++itemCnt, g_VATabTree->GetItemTextW(item));
                mnu.Items.back().Items.push_back(MenuItemLmb(menuTxt, [item, cnt]
                {
                    g_VATabTree->OpenItemFile(item, cnt.get());
                }, nullptr));

                item = g_VATabTree->GetNextItem(item, TVGN_NEXT);
            }
        }
    }
}
*/

void ShowVAContextMenu(EdCntPtr cnt, CPoint point, bool mouse)
{
	PopupMenuLmb mnu;
	const BOOL edHasSelection = cnt->HasSelection();

	if (cnt->CanSuperGoto())
	{
		mnu.Items.push_back(MenuItemLmb(
		                        L"G&oto Related...", [cnt] { cnt->Exec(icmdVaCmd_SuperGoto); }, nullptr)
		                        .SetKeyBindingForCommand("VAssistX.GotoRelated")
		                        .SetIcon(ICONIDX_REFERENCE_GOTO_DEF));
	}

	// Goto Recent Method / Open Recent File
	// AddVATabTreeItems(mnu, cnt);

	auto insertSnippetCmd = [cnt] { cnt->CodeTemplateMenu(); };

#if !defined(RAD_STUDIO) // CppBuilder has their own snippets
	if (edHasSelection)
	{
		mnu.Items.push_back(MenuItemLmb(L"Surround &With VA Snippet...", insertSnippetCmd, nullptr)
		                        .SetKeyBindingForCommand("VAssistX.VaSnippetInsert")
		                        .SetIcon(ICONIDX_VS11_SNIPPET));
	}
	else
	{
		WTString cwd = cnt->CurWord();
		cwd.Trim();
		WTString rtWd = cnt->WordRightOfCursor();
		rtWd.Trim();
		if (cwd.IsEmpty() || rtWd.IsEmpty())
			mnu.Items.push_back(MenuItemLmb(L"&Insert VA Snippet...", insertSnippetCmd, nullptr)
			                        .SetKeyBindingForCommand("VAssistX.VaSnippetInsert")
			                        .SetIcon(ICONIDX_VS11_SNIPPET));
	}
#endif

	mnu.Items.push_back(MenuItemLmb());

	size_t find_refs_index = 0;
	DTypePtr mSym;
	FillVAContextMenuWithRefactoring(mnu, find_refs_index, mSym, true);

	if (CanSmartSelect())
	{
		int pos = 0;

		if (!mnu.Items.empty())
		{
			if (find_refs_index > 0)
			{
				pos = (int)find_refs_index - 1;
				if (pos > 0 && (mnu.Items[(uint)pos - 1].Flags & MF_SEPARATOR))
					pos--;
			}
			else
			{
				pos = (int)mnu.Items.size();
			}

			mnu.Items.insert(mnu.Items.begin() + pos++, MenuItemLmb());
		}

		mnu.Items.insert(mnu.Items.begin() + pos++,
		                 MenuItemLmb(
		                     L"&Extend Selection", [] { SmartSelect(icmdVaCmd_SmartSelectExtend); }, nullptr)
		                     .SetKeyBindingForCommand("VAssistX.SmartSelectExtend"));

		if (edHasSelection)
			mnu.Items.insert(mnu.Items.begin() + pos++,
			                 MenuItemLmb(
			                     L"&Shrink Selection", [] { SmartSelect(icmdVaCmd_SmartSelectShrink); }, nullptr)
			                     .SetKeyBindingForCommand("VAssistX.SmartSelectShrink"));

		mnu.Items.insert(mnu.Items.begin() + pos++,
		                 MenuItemLmb(
		                     L"E&xtend Block Selection", [] { SmartSelect(icmdVaCmd_SmartSelectExtendBlock); }, nullptr)
		                     .SetKeyBindingForCommand("VAssistX.SmartSelectExtendBlock"));

		if (edHasSelection)
			mnu.Items.insert(
			    mnu.Items.begin() + pos++,
			    MenuItemLmb(
			        L"S&hrink Block Selection", [] { SmartSelect(icmdVaCmd_SmartSelectShrinkBlock); }, nullptr)
			        .SetKeyBindingForCommand("VAssistX.SmartSelectShrinkBlock"));
	}

#ifdef DEBUG
	// mnu.HasUniqueAccessKeys(); // has own asserts
#endif

#if defined(RAD_STUDIO) && defined(_DEBUG)
	// Add these to context menu in CppBuilder till it gets a menu/toolbar
	mnu.Items.push_back(MenuItemLmb());
	mnu.Items.push_back(MenuItemLmb(
	    L"Find Symbol...", [] { gVaService->Exec(IVaService::ct_global, icmdVaCmd_FindSymbolDlg); }, nullptr));
	mnu.Items.push_back(MenuItemLmb(
	    L"Open File in Sol&ution...",
	    [] { gVaService->Exec(IVaService::ct_global, icmdVaCmd_OpenFileInWorkspaceDlg); }, nullptr));
#endif
//	mnu.Items.push_back(MenuItemLmb());
// 	mnu.Items.push_back(MenuItemLmb(
// 	    L"Share code with...", [] { gVaService->Exec(IVaService::ct_editor, icmdVaCmd_ShareWith); }, nullptr));
//	mnu.NormalizeSeparators();

	if (!mnu.Items.empty())
	{
		// force all subsequent commands to open menu on same point
		auto force_pt = PopupMenuXP::PushRequiredPoint(point);

		if (gShellAttr->IsDevenv10OrHigher())
			cnt->SetFocusParentFrame(); // case=45591

		if (!mouse)
			cnt->PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list

		TempTrue tt(cnt->m_contextMenuShowing);
		mnu.Show(cnt.get(), point.x, point.y);
	}

	if (mSym && gTestsActive && gTestLogger && gTestLogger->IsMenuLoggingEnabled())
		gTestLogger->LogStr(mSym->SymScope());
}

#if defined(RAD_STUDIO) && defined(_DEBUG) 
// context menu short circuit for testing before there is a general facility
// for adding commands to RadStudio IDE
void ShowVAContextMenu()
{
	EdCntPtr cnt(g_currentEdCnt);
	if (!cnt)
		return;

	bool mouse = false;
	CPoint point(cnt->vGetCaretPos());
	point.y += 20;
	cnt->vClientToScreen(&point);

	PopupMenuLmb mnu;
	const BOOL edHasSelection = cnt->HasSelection();

	mnu.Items.push_back(MenuItemLmb(
	                    L"&Goto...", [cnt] { cnt->Exec(icmdVaCmd_GotoImplementation); }, nullptr));
	mnu.Items.push_back(MenuItemLmb(
	                    L"Goto &Member...", [cnt] { cnt->Exec(icmdVaCmd_GotoMember); }, nullptr));

	if (cnt->CanSuperGoto())
	{
		mnu.Items.push_back(MenuItemLmb(
		                        L"G&oto Related...", [cnt] { cnt->Exec(icmdVaCmd_SuperGoto); }, nullptr)
		                        .SetKeyBindingForCommand("VAssistX.GotoRelated")
		                        .SetIcon(ICONIDX_REFERENCE_GOTO_DEF));
	}

	mnu.Items.push_back(MenuItemLmb());

	size_t find_refs_index = 0;
	DTypePtr mSym;
	FillVAContextMenuWithRefactoring(mnu, find_refs_index, mSym, true);

	if (CanSmartSelect())
	{
		int pos = 0;

		if (!mnu.Items.empty())
		{
			if (find_refs_index > 0)
			{
				pos = (int)find_refs_index - 1;
				if (pos > 0 && (mnu.Items[(uint)pos - 1].Flags & MF_SEPARATOR))
					pos--;
			}
			else
			{
				pos = (int)mnu.Items.size();
			}

			mnu.Items.insert(mnu.Items.begin() + pos++, MenuItemLmb());
		}

		mnu.Items.insert(mnu.Items.begin() + pos++,
		                 MenuItemLmb(
		                     L"&Extend Selection", [] { SmartSelect(icmdVaCmd_SmartSelectExtend); }, nullptr)
		                     .SetKeyBindingForCommand("VAssistX.SmartSelectExtend"));

		if (edHasSelection)
			mnu.Items.insert(mnu.Items.begin() + pos++,
			                 MenuItemLmb(
			                     L"&Shrink Selection", [] { SmartSelect(icmdVaCmd_SmartSelectShrink); }, nullptr)
			                     .SetKeyBindingForCommand("VAssistX.SmartSelectShrink"));

		mnu.Items.insert(mnu.Items.begin() + pos++,
		                 MenuItemLmb(
		                     L"E&xtend Block Selection", [] { SmartSelect(icmdVaCmd_SmartSelectExtendBlock); }, nullptr)
		                     .SetKeyBindingForCommand("VAssistX.SmartSelectExtendBlock"));

		if (edHasSelection)
			mnu.Items.insert(
			    mnu.Items.begin() + pos++,
			    MenuItemLmb(
			        L"S&hrink Block Selection", [] { SmartSelect(icmdVaCmd_SmartSelectShrinkBlock); }, nullptr)
			        .SetKeyBindingForCommand("VAssistX.SmartSelectShrinkBlock"));
	}

#if defined(RAD_STUDIO)
	// Add these to context menu in CppBuilder till it gets a menu/toolbar
	mnu.Items.push_back(MenuItemLmb());
	mnu.Items.push_back(MenuItemLmb(
	    L"Find Symbol...", [] { gVaService->Exec(IVaService::ct_global, icmdVaCmd_FindSymbolDlg); }, nullptr));
	mnu.Items.push_back(MenuItemLmb(
	    L"Open File in Sol&ution...",
	    [] { gVaService->Exec(IVaService::ct_global, icmdVaCmd_OpenFileInWorkspaceDlg); }, nullptr));
#endif
	mnu.NormalizeSeparators();

	if (!mnu.Items.empty())
	{
		// force all subsequent commands to open menu on same point
		auto force_pt = PopupMenuXP::PushRequiredPoint(point);

		if (gShellAttr->IsDevenv10OrHigher())
			cnt->SetFocusParentFrame(); // case=45591

		if (!mouse)
			cnt->PostMessage(WM_KEYDOWN, VK_DOWN, 1); // select first item in list

		TempTrue tt(cnt->m_contextMenuShowing);
		mnu.Show(cnt.get(), point.x, point.y);
	}

	if (mSym && gTestsActive && gTestLogger && gTestLogger->IsMenuLoggingEnabled())
		gTestLogger->LogStr(mSym->SymScope());
}
#endif

bool GetRefactorSym(EdCntPtr& curEd, DTypePtr& outType, WTString* outInvokingScope, bool reparseIfNeeded)
{
	if (!curEd)
		return false;

	if (reparseIfNeeded)
		ReparseAndWaitIfNeeded();

	MultiParsePtr mp(curEd->GetParseDb());
	DTypePtr sym = mp->GetCwData();
	if (!sym)
	{
		const WTString cwd = curEd->CurWord();
		if (ISCSYM(*cwd.c_str()))
		{
			curEd->Scope();
			mp = curEd->GetParseDb();
			sym = mp->GetCwData();
		}

		if (!sym)
			return false;
	}

	const WTString sel(curEd->GetSelString());
	if (!sel.IsEmpty())
	{
		const WTString symName(sym->Sym());
		if (!symName.IsEmpty() && StrCmpAC(sel.c_str(), symName.c_str()))
		{
			// [case: 111589] reject selections spanning more than the symbol whose DType it locates
			return false;
		}
	}

	WTString invokingScope = StrGetSymScope(curEd->m_lastScope);
	DTypePtr symToUse;

	if (DType::IsLocalScope(invokingScope))
	{
		// this isn't a type declaration, so don't worry about Decl vs Impl
		symToUse = sym;
	}
	else
	{
		// Make sure we have the appropriate DType for the Decl or Impl
		int valuesFound = 0;
		int missOnType = 0;
		int missOnLineNum = 0;
		DTypeList dList, fileIdMiss;
		if (mp->FindExactList(sym->SymScope().c_str(), dList))
		{
			const UINT kCurEdFileId = mp->GetFileID();
			for (DTypeList::iterator iter = dList.begin(); iter != dList.end(); ++iter)
			{
				const DType& dt = *iter;
				if (dt.MaskedType() == sym->MaskedType())
				{
					if (dt.FileId() == kCurEdFileId)
					{
						// find dtype closest to m_curLine
						if (dt.Line() >= curEd->CurLine())
						{
							if (valuesFound > 0)
							{
								if (dt.Line() < symToUse->Line())
									symToUse = std::make_shared<DType>(dt);
							}
							else
							{
								symToUse = std::make_shared<DType>(dt);
							}
							++valuesFound;
						}
						else
							++missOnLineNum;
					}
					else
						fileIdMiss.push_back(dt);
				}
				else
					++missOnType;
			}

			if (!valuesFound && fileIdMiss.size())
			{
				const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(kCurEdFileId));
				if (projForActiveFile.size())
				{
					ProjectVec projForSym(GlobalProject->GetProjectForFile(sym->FileId()));
					if (projForSym == projForActiveFile)
					{
						// default sym is in same project as kCurEdFileId, ok
					}
					else
					{
						// else check items in fileIdMissList for match on project
						for (DTypeList::iterator iter = fileIdMiss.begin(); iter != fileIdMiss.end(); ++iter)
						{
							const DType& dt = *iter;
							projForSym = GlobalProject->GetProjectForFile(dt.FileId());
							if (projForSym == projForActiveFile)
							{
								// found better match than default of sym
								symToUse = std::make_shared<DType>(dt);
								++valuesFound;
							}
						}
					}
				}
			}
		}

		if (!valuesFound)
			symToUse = sym;

		//#ifdef _DEBUG
		//		if (sym && sym->Line() != symToUse->Line())
		//			_asm nop;
		//#endif
	}

	symToUse->LoadStrs();
	outType = symToUse;

	if (outInvokingScope)
		*outInvokingScope = invokingScope;
	return true;
}

BOOL CanSmartSelect(DWORD cmdId /*= 0*/)
{
	try
	{
		EdCntPtr ed(g_currentEdCnt);

		if (!ed)
		{
			_ASSERTE(!"CanSmartSelect failed");
			return FALSE;
		}

		const int ftype = ed->m_ftype;

		if (cmdId == icmdVaCmd_RefactorModifyExpression)
		{
			return Is_C_CS_File(ftype) && gShellAttr && gShellAttr->IsDevenv10OrHigher() &&
			       VASmartSelect::CanModifyExpr();
		}

		if (gShellSvc && gShellSvc->HasBlockModeSelection(ed.get()))
		{
			long ssel, esel;
			ed->GetSel2(ssel, esel);

			if (ed->LineFromChar(ssel) != ed->LineFromChar(esel))
				return FALSE;
		}

		if (gShellAttr && gShellAttr->IsMsdev())
			return FALSE;

		if (Is_C_CS_File(ftype) || XAML == ftype || XML == ftype)
			return TRUE;

		return FALSE;
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in ::CanSmartSelect");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in ::CanSmartSelect");
	}

	return FALSE;
}

BOOL SmartSelect(DWORD cmdId /*= 0*/)
{
	LogElapsedTime let("SmartSelect", 50);
	try
	{
		if (cmdId == icmdVaCmd_RefactorModifyExpression)
		{
			return VASmartSelect::RunModifyExpr() ? TRUE : FALSE;
		}

		if (VASmartSelect::GenerateBlocks(cmdId) == 0)
		{
			if (VASmartSelect::IN_DEBUG)
			{
				_ASSERTE(!"Smart Select failed to generate blocks!");
			}

			return FALSE;
		}

		_ASSERTE(!VASmartSelect::CurrentBlocks().empty());

		if (!VASmartSelect::IsCommandReady(cmdId))
		{
			if (VASmartSelect::IN_DEBUG)
			{
				_ASSERTE(!"Smart Select command is not ready!");
			}

			return FALSE;
		}

		if (!VASmartSelect::CurrentBlocks().empty())
		{
			if (cmdId == icmdVaCmd_SmartSelectExtend || cmdId == icmdVaCmd_SmartSelectShrink ||
			    cmdId == icmdVaCmd_SmartSelectExtendBlock || cmdId == icmdVaCmd_SmartSelectShrinkBlock)
			{
				LPCWSTR cmd_name = VASmartSelect::GetCommandName(cmdId);
				VASmartSelect::Block* block = VASmartSelect::Block::FindByName(cmd_name);
				if (block)
				{
					block->Apply();
					return TRUE;
				}
				return FALSE;
			}
		}

		return TRUE;
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in ::SmartSelect");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in ::SmartSelect");
	}

	return FALSE;
}

void CleanupRefactorGlogals()
{
	includes.clear();
}

CStringW MoveClassRefactorHelper::HeaderFileName;
CStringW MoveClassRefactorHelper::HeaderPathName;
