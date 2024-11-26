#include "StdAfxEd.h"
#include "ChangeSignature.h"
#include "FreezeDisplay.h"
#include "UndoContext.h"
#include "AutotextManager.h"
#include "TraceWindowFrame.h"
#include "project.h"
#include "WTString.h"
#include "Foo.H"
#include "EdCnt.H"
#include "VARefactor.h"
#include "AddClassMemberDlg.h"
#include "FileTypes.h"
#include "parse.h"
#include "RenameReferencesDlg.h"
#include "FindReferences.h"
#include "file.h"
#include "VACompletionBox.h"
#include "fdictionary.h"
#include "Registry.h"
#include "RegKeys.h"
#include "VAAutomation.h"
#include "FindReferencesThread.h"
#include "WindowUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

static std::vector<WTString> Split(const WTString& txt)
{
	std::vector<WTString> arr;
	token2 t = txt;
	while (t.more())
	{
		arr.push_back(t.read2(";"));
	}
	return arr;
}

BOOL ChangeSignature::CanChange(DType* sym)
{
	if (!sym || sym->IsEmpty())
		return FALSE;

	mSym = std::make_shared<DType>(sym);
	BOOL retval = CanChange();
	if (!retval)
		mSym.reset();
	return retval;
}

BOOL ChangeSignature::CanChange()
{
	if (!mSym || mSym->IsEmpty())
		return FALSE;
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;
	WTString symDef(mSym->Def());
	if (strstrWholeWord(symDef, "operator"))
		return FALSE;
	if (mSym->IsSysLib())
		return FALSE;

	// bug 2197 - allow if the selected text is the current symbol
	// otherwise not available if any text is selected
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	const WTString selString(ed->GetSelString());
	if (!selString.IsEmpty() && selString != StrGetSym(mSym->SymScope()))
		return FALSE;
	if (mSym->MaskedType() != FUNC)
		return FALSE;
	// [case: 60987]
	if (symDef.GetLength() > 1536)
		return FALSE;
	MultiParsePtr mp = ed->GetParseDb();
	if (!mp)
		return FALSE;

	// [case: 11717] don't allow on overloaded methods
	DTypeList lst;
	DTypeList* pList;
	pList = &lst;
	DTypePtr cwDat(mp->GetCwData());
	if (cwDat && *mSym == *cwDat)
		pList = &mp->mCwDataList;

	// populate list if not already cached
	if (pList->empty())
		mp->FindExactList(mSym->SymHash(), mSym->ScopeHash(), *pList, false);

	if (pList->size() && 2 > pList->size())
		return TRUE;

	DTypeList filteredList;
	filteredList.assign(pList->begin(), pList->end());
	filteredList.FilterDupesAndGotoDefs();
	filteredList.FilterEquivalentDefs();

	if (!filteredList.size())
		return FALSE; // whoops!

	if (2 == filteredList.size())
	{
		// [case: 1540] deal with header implementations that are separate from the decl
		DTypeList::iterator it = filteredList.begin();
		const DType& d1 = (*it++);
		const DType& d2 = (*it);

		const BOOL d1Impl = d1.IsImpl();
		const BOOL d2Impl = d2.IsImpl();
		if ((d1Impl || d2Impl) && !(d1Impl && d2Impl)) // XOR
		{
			if (d1.FileId() == d2.FileId())
			{
				const int kMinLinesApart =
				    2; // [case: 24303] change from 10 to 2 due to test in auto_change_signature_0008.h
				const int d1Line = d1.Line();
				const int d2Line = d2.Line();
				if (d1Line > (d2Line + kMinLinesApart) || d2Line > (d1Line + kMinLinesApart))
				{
					// This could potentially be a false positive if:
					// a function is overloaded two times where:
					//    one overload is just a declaration
					//    and the other is both a declaration and definition
					// and the two overloads are more than kMinLinesApart lines apart

					// The result of a false positive here is that we would
					// allow Change Signature on an overloaded function but
					// Chg Sig is not overload aware.
					return TRUE;
				}
			}
			else if (IsCFile(gTypingDevLang))
			{
				// declaration and definition
				return TRUE;
			}
		}

		return FALSE;
	}

	if (2 > filteredList.size())
		return TRUE;

	return FALSE;
}

BOOL ChangeSignature::Change()
{
	TraceScopeExit tse("Change Signature exit");
	UndoContext undoContext("VA Change Signature");

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;

	if (!mSym)
		return FALSE;

	BOOL changeUe4ImplicitMethods = FALSE;
	if (Psettings && Psettings->mUnrealEngineCppSupport)
	{
		bool isImplicitMethod = false;
		WTString symScope = mSym->SymScope();
		if (symScope.EndsWith("_Implementation"))
		{
			symScope = symScope.Left(symScope.GetLength() - 15);
			isImplicitMethod = true;
		}
		else if (symScope.EndsWith("_Validate"))
		{
			symScope = symScope.Left(symScope.GetLength() - 9);
			isImplicitMethod = true;
		}
		MultiParsePtr mp = ed->GetParseDb();
		if (!mp)
			return FALSE;
		if (isImplicitMethod)
		{
			// [case: 141288] Called with an implicit method. Find the non-implicit definition.
			DType* dType = mp->FindExact2(symScope, false);
			if (dType)
				mSym = std::make_shared<DType>(dType);
			changeUe4ImplicitMethods = TRUE;
		}
		else
		{
			// [case: 141288] Check if this method has any implicit methods we also need to update.
			DTypeList dTypes;
			// Even though we don't need the list, we need to use FindExactList because FindExact rejects hits from
			// undeclared method implementations (GOTODEFs) when not searching for them explicity. We want results for
			// GOTODEFs and FUNCs.
			mp->FindExactList(mSym->SymScope() + "_Implementation", dTypes);
			if (!dTypes.size())
				mp->FindExactList(mSym->SymScope() + "_Validate", dTypes);
			changeUe4ImplicitMethods = dTypes.size() ? TRUE : FALSE;
		}
	}

	WTString bcl;
	MultiParsePtr mp = ed->GetParseDb();
	if (mp)
	{
		DTypePtr cwd = mp->GetCwData();
		if (cwd)
		{
			// [case: 115688]
			WTString betterScope = cwd->SymScope();
			betterScope = StrGetSymScope(betterScope);
			bcl = mp->GetBaseClassList(betterScope);

			if (bcl.GetLength() && bcl[0] == '\f')
				bcl = bcl.Mid(1);
			int pos = bcl.find('\f');
			if (pos != -1)
				bcl = bcl.Mid(pos + 1);
			bcl.ReplaceAll("\f", " ");
			bcl = DecodeTemplates(bcl);
			bcl.ReplaceAll(":", " ");
		}
	}

	return RunNewDialog(bcl, changeUe4ImplicitMethods);
}

BOOL ChangeSignature::AppendParameter(WTString paramDecl)
{
	mParamDeclToAppend = paramDecl;
	mDisplayNagBoxes = FALSE;
	const BOOL retval = Change();
	mDisplayNagBoxes = TRUE;
	mParamDeclToAppend.Empty();
	mSym.reset();
	return retval;
}

BOOL ChangeSignature::RunNewDialog(const WTString& bcl, BOOL changeUe4ImplicitMethods /*= FALSE*/)
{
	ChangeSignatureDlg dlg;
	if (!dlg.Init(mSym, bcl, changeUe4ImplicitMethods))
		return FALSE;

	if (!mParamDeclToAppend.IsEmpty())
	{
		if (!dlg.AppendArg(mParamDeclToAppend))
			return FALSE;
	}

	if (Psettings && Psettings->mUnrealEngineCppSupport)
	{
		dlg.SetSymMaskedType(mSym->MaskedType()); // [case: 147774] needed for UE core redirects
		dlg.SetIsUEMarkedType(IsUEMarkedType(mSym->SymScope().c_str())); // [case: 148145]
	}
	dlg.DoModal();
	return TRUE;
}

class ArgTracker : public ParseToCls
{
  public:
	ArgTracker(int fType) : ParseToCls(fType), mIsDone(false)
	{
	}

	virtual void IncDeep() override
	{
		if (m_deep == 0 && CurChar() == '(')
			mArgs.push_back(CurPos() + 1);

		__super::IncDeep();
	}

	virtual void DecDeep() override
	{
		__super::DecDeep();

		if (m_deep == 0 && CurChar() == ')')
		{
			mArgs.push_back(CurPos() + 1);
			mIsDone = true;
		}
	}

	virtual void OnChar() override
	{
		if (!InComment() && m_deep == 1 && CurChar() == ',')
			mArgs.push_back(CurPos() + 1);

		__super::OnChar();
	}

	virtual BOOL IsDone() override
	{
		if (mIsDone)
			return true;
		return __super::IsDone();
	}

	uint ArgCount()
	{
		uint count = (uint)mArgs.size();
		if (count > 0)
			return count - 1;
		return 0;
	}

	WTString GetArg(uint i, int* outStartIdx = nullptr, int* outEndIdx = nullptr)
	{
		if (i < ArgCount())
		{
			auto argStart = mArgs[i];
			if (outStartIdx)
				*outStartIdx = ptr_sub__int(argStart, m_buf);
			auto argEnd = mArgs[i + 1] - 1;
			if (outEndIdx)
				*outEndIdx = ptr_sub__int(argEnd, m_buf);
			return WTString(argStart, ptr_sub__int(argEnd, argStart));
		}
		return WTString();
	}

	bool GetRange(LPCSTR* outStart, LPCSTR* outEnd)
	{
		if (!mArgs.size())
			return false;

		*outStart = *(mArgs.begin());
		*outEnd = *(mArgs.rbegin());
		return true;
	}

	WTString GetArg(uint i, uint whitespaceArg)
	{
		WTString rslt = GetArg(i);
		if (i != whitespaceArg && !rslt.IsEmpty() && (int)whitespaceArg >= 0 && whitespaceArg < ArgCount())
		{
			const auto wsArgStart = mArgs[whitespaceArg];
			const auto wsArgEnd = mArgs[whitespaceArg + 1] - 1;
			auto wsTrimStart = wsArgStart;
			auto wsTrimEnd = wsArgEnd;

			while (wsTrimStart < wsTrimEnd)
			{
				if (wt_isspace(*wsTrimStart))
					++wsTrimStart;
				else
					break;
			}

			while (wsTrimStart < wsTrimEnd)
			{
				--wsTrimEnd;
				if (!wt_isspace(*wsTrimEnd))
				{
					++wsTrimEnd;
					break;
				}
			}

			const WTString whitespacePre(wsArgStart, ptr_sub__int(wsTrimStart, wsArgStart));
			const WTString whitespacePost(wsTrimEnd, ptr_sub__int(wsArgEnd, wsTrimEnd));

			rslt.Trim();

			rslt = whitespacePre + rslt + whitespacePost;
		}

		return rslt;
	}

	bool GetArgWhitespace(uint i, WTString* outPre, WTString* outPost)
	{
		if ((int)i < 0 || i >= ArgCount())
			return false;

		const auto wsArgStart = mArgs[i];
		const auto wsArgEnd = mArgs[i + 1] - 1;
		auto wsTrimStart = wsArgStart;
		auto wsTrimEnd = wsArgEnd;

		while (wsTrimStart < wsTrimEnd)
		{
			if (wt_isspace(*wsTrimStart))
				++wsTrimStart;
			else
				break;
		}

		while (wsTrimStart < wsTrimEnd)
		{
			--wsTrimEnd;
			if (!wt_isspace(*wsTrimEnd))
			{
				++wsTrimEnd;
				break;
			}
		}

		if (outPre)
			*outPre = WTString(wsArgStart, ptr_sub__int(wsTrimStart, wsArgStart));
		if (outPost)
			*outPost = WTString(wsTrimEnd, ptr_sub__int(wsArgEnd, wsTrimEnd));
		return true;
	}

  private:
	std::vector<LPCSTR> mArgs;
	bool mIsDone;
};

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(ChangeSignatureDlg, UpdateReferencesDlg)
ON_WM_CTLCOLOR()
ON_BN_CLICKED(IDC_CHK_CORE_REDIRECTS, OnToggleCoreRedirects)
END_MESSAGE_MAP()
#pragma warning(pop)

ChangeSignatureDlg::ChangeSignatureDlg()
    : UpdateReferencesDlg("RenameDlg", IDD_CHANGESIGNATURE, NULL, Psettings->mIncludeProjectNodeInRenameResults, true),
      mSym(nullptr), mDeclFileType(0), mDisableEdit(false), mWarningBrush(RGB(255, 255, 153)) // light yellow
{
	SetHelpTopic("dlgChangeSignature");
	mColourize = true;
}

ChangeSignatureDlg::~ChangeSignatureDlg()
{
	// after modal loop has exited
	if (mRefsForResultsWnd && mRefsForResultsWnd->Count() && gVaService)
		gVaService->DisplayReferences(*mRefsForResultsWnd);
}

BOOL ChangeSignatureDlg::OnInitDialog()
{
	__super::OnInitDialog();

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_CORE_REDIRECTS, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}
	
	AddSzControl(IDC_WARNING, mdResize, mdRepos);
	AddSzControl(IDC_CHK_CORE_REDIRECTS, mdRelative, mdNone);
	GetDlgItem(IDC_CHK_CORE_REDIRECTS)->ShowWindow(Psettings->mUnrealEngineCppSupport && mIsUEMarkedType); // show option only if it is UE solution and marked type
	((CButton*)GetDlgItem(IDC_CHK_CORE_REDIRECTS))->SetCheck(false);
	
	if (mSym->IsConstructor())
	{
		GetDlgItem(IDC_WARNING)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_WARNING)->SetWindowText("Warning: Change Signature does not update calls to constructors");
	}

	mEdit->SetSel(0, 0);

	if (gTestLogger)
	{
		gTestLogger->LogStr(WTString("Change Signature dlg"));
		gTestLogger->LogStr(WTString("\t" + mEditTxt));
	}

	if (mDisableEdit)
	{
		GetDlgItem(IDC_EDIT1)->EnableWindow(FALSE);
		m_tree.SetFocus();
	}

	return FALSE;
}

void ChangeSignatureDlg::UpdateStatus(BOOL done, int /*unused*/)
{
	WTString msg;
	if (!done)
	{
		msg.WTFormat("Searching for references in %s to %s that may need to be updated...",
		             mRefs->GetScopeOfSearchStr().c_str(), mRefs->GetFindSym().c_str());
	}
	else if (mFindRefsThread && mFindRefsThread->IsStopped() && mRefs->Count())
		msg.WTFormat("Search canceled before completion.  U&pdate references to %s at your own risk.",
		             mRefs->GetFindSym().c_str());
	else if (!mBaseClassWarning.IsEmpty())
		msg = mBaseClassWarning;
	else if (mRefs->Count())
		msg.WTFormat("U&pdate signature of %s (in %s) to:", mRefs->GetFindSym().c_str(),
		             mRefs->GetScopeOfSearchStr().c_str());

	if (msg.GetLength())
		::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());

	mStatusText = msg;
}

static int VirtSpecPos(WTString decl)
{
	int virtPos;
	auto declPtr = decl.c_str();
	for (virtPos = 0; virtPos < decl.GetLength(); ++virtPos)
	{
		if (wt_isspace(declPtr[virtPos]))
			continue;

		auto word = TokenGetField(declPtr + virtPos);
		if (word == "virtual" || word == "override" || word == "static" || word == "public" || word == "private" ||
		    word == "protected" || word == "__published" || word == "abstract")
		{
			virtPos += word.GetLength();
		}
		else
		{
			break;
		}
	}

	return virtPos;
}

static int PureSpecPos(WTString decl)
{
	int purePos = -1;
	const int parenPos = decl.ReverseFind(')');
	if (-1 != parenPos)
	{
		if (-1 != (purePos = decl.Find("=0", parenPos)) || -1 != (purePos = decl.Find("= 0", parenPos)) ||
		    -1 != (purePos = decl.Find("override", parenPos)) || -1 != (purePos = decl.Find("PURE", parenPos)))
		{
			// grab leading whitespace
			while (purePos > 0 && wt_isspace(decl[purePos - 1]))
				purePos--;
		}
	}
	return purePos;
}

UpdateReferencesDlg::UpdateResult ChangeSignatureDlg::UpdateReference(int refIdx, FreezeDisplay& _f)
{
	auto curRef = mRefs->GetReference((uint)refIdx);

	if (curRef->mData && !curRef->mData->IsMethod())
	{
		// nothing to do here
		return UpdateReferencesDlg::rrSuccess;
	}

	if (!mRefs->GotoReference(refIdx))
		return UpdateReferencesDlg::rrError;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	const WTString sel = ed->GetSelString();
	if (sel != mRefs->GetFindSym())
	{
		if (mRefs->flags & FREF_Flg_UeFindImplicit)
		{
			// [case: 141288] The symbol names will not always match for UE4 implicit methods.
			if (sel != mRefs->GetFindSym() + "_Implementation" && sel != mRefs->GetFindSym() + "_Validate")
				return UpdateReferencesDlg::rrError;
		}
		else
			return UpdateReferencesDlg::rrError;
	}

	_f.ReadOnlyCheck();

	switch (curRef->type)
	{
	case FREF_ScopeReference:
	case FREF_Reference:
		if (curRef->mData && curRef->mData->IsConstructor())
			return UpdateReferencesDlg::rrNoChange;

		// Rename method call
		if (mMap.OldArgs.size() && mMap.NewArgs.size() && mMap.OldArgs[0].Name != mMap.NewArgs[0].Name)
		{
			if (!ed->ReplaceSelW(mMap.NewArgs[0].Name, noFormat))
				return rrError;
			if (gShellAttr->IsMsdev())
				ed->GetBuf(TRUE);
			ed->OnModified(TRUE);
		}

		if (mMap.ArgsReordered)
		{
			auto buf = ed->GetBuf(TRUE);
			auto pos = curRef->GetEdBufCharOffset(ed, sel, buf);
			auto buf2 = buf.c_str() + pos;
			ArgTracker af(mDeclFileType);
			af.ParseTo(buf2, buf.GetLength() - (int)pos, ");");
			LPCSTR start = nullptr, end = nullptr;
			if (!af.GetRange(&start, &end))
				return rrError;

			WTString newArgStr;

			uint processedExistingArgs = 0;
			CStringW whiteSpaces;

			for (uint i = 1; i < mMap.NewArgs.size(); ++i)
			{
				WTString tmpArg;
				bool isInsertedArg = false;

				auto& newArg = mMap.NewArgs[i];
				bool removedDefaultVal = false;
				if (newArg.RefIndex != -1)
					removedDefaultVal = mMap.OldArgs[(uint)newArg.RefIndex].DefaultValue.IsEmpty() == false &&
					                    newArg.DefaultValue.IsEmpty();
				if (newArg.RefIndex == -1 || removedDefaultVal)
				{
					// new arg
					if (newArg.IsOptional())
					{
						auto oldArgStr = af.GetArg(processedExistingArgs, i - 1);
						if (!oldArgStr.IsEmpty())
						{
							// there was already an arg here, so insert default
							tmpArg = newArg.TodoValue;
							isInsertedArg = true;
						}
					}
					else
					{
						auto oldArgStr = af.GetArg(uint(newArg.RefIndex - 1), i - 1);
						WTString trimmedOld = oldArgStr;
						trimmedOld.TrimLeft();
						if (removedDefaultVal && !trimmedOld.IsEmpty())
						{                       // do not overwrite when argument is already in-place
							tmpArg = oldArgStr; // no isInsertedArg = true to avoid inserting space - we keep the
							                    // original formatting
						}
						else
						{
							tmpArg = newArg.TodoValue;
							isInsertedArg = true;
						}
					}
				}
				else
				{
					processedExistingArgs++;

					auto oldArgStr = af.GetArg(uint(newArg.RefIndex - 1), i - 1);
					if (oldArgStr.IsEmpty())
					{
						if (!newArg.IsOptional())
						{
							// arg became non-optional?
							tmpArg = newArg.TodoValue;
							isInsertedArg = true;
						}
					}
					else
					{
						tmpArg = oldArgStr;
						if ((uint)newArg.RefIndex - 1 == af.ArgCount() - 1)
						{ // only store last param's whitespaces to keep the ones before the closing paren
							for (uint j = 1; j < mMap.NewArgs.size(); ++j)
							{
								if ((uint)mMap.NewArgs[j].RefIndex - 1 > af.ArgCount() - 1)
								{ // trim only if at least one new param will be inserted to the right of the current
								  // last param
									tmpArg.TrimRight();
									whiteSpaces = oldArgStr.Right(oldArgStr.GetLength() - tmpArg.GetLength()).Wide();
									break;
								}
							}
						}
					}
				}

				if (!tmpArg.IsEmpty())
				{
					if (!newArgStr.IsEmpty())
					{
						newArgStr += ",";
						if (isInsertedArg)
							newArgStr += " ";
					}
					newArgStr += tmpArg;
				}
			}

			newArgStr += whiteSpaces;
			ed->SetSelection(ptr_sub__int(start, buf.c_str()), ptr_sub__int(end, buf.c_str()) - 1);
			if (!ed->ReplaceSelW(newArgStr.Wide(), noFormat))
				return rrError;
			if (gShellAttr->IsMsdev())
				ed->GetBuf(TRUE);
			ed->OnModified(TRUE);
			return rrSuccess;
		}
		break;

	case FREF_None:
	case FREF_DefinitionAssign:
	case FREF_ReferenceAssign:
	case FREF_Unknown:
	case FREF_Comment:
	case FREF_JsSameName:
	case FREF_ReferenceAutoVar:
	case FREF_IncludeDirective:
	default: {
		// rename only
		if (mMap.OldArgs.size() && mMap.NewArgs.size() && mMap.OldArgs[0].Name != mMap.NewArgs[0].Name)
		{
			if (!ed->ReplaceSelW(mMap.NewArgs[0].Name, noFormat))
				return rrError;
			if (gShellAttr->IsMsdev())
				ed->GetBuf(TRUE);
			ed->OnModified(TRUE);
			return rrSuccess;
		}
	}
	break;

	case FREF_Definition: {
		auto sym = curRef->mData;
		if (!sym)
		{
			// [case: 103538]
			_ASSERTE(!"missing ref data");
			vLog("ERROR: Change Signature empty ref data for FREF_Definition");
			return UpdateReferencesDlg::rrError;
		}

		DTypeList& lst = GetDTypeCache(sym);

		// get best match
		DType* data = nullptr;
		for (auto iter = lst.begin(); iter != lst.end(); ++iter)
		{
			DType* pData = &(*iter);

			if (pData->FileId() != curRef->fileId)
				continue;

			if (pData->Line() < (int)curRef->lineNo)
				continue;

			if (!data || (pData->Line() < data->Line()))
				data = pData;
		}

		if (!data)
		{
			if (sym->IsConstructor())
				return rrNoChange; // [case: 78972] Hack-a-licious.
			return rrError;
		}

		// adjusted declStr for derived/overridden methods
		// keep existing "public virtual" at beginning, and "= 0" at end
		// but update the core signature.
		WTString adjustedDeclStr = mEditTxt;
		if (data->SymScope() != mSym->SymScope())
		{
			// [case: 141288] The symbol names will not always match for UE4 implicit methods.
			if (!(mRefs->flags & FREF_Flg_UeFindImplicit) || data->SymScope() != mSym->SymScope() + "_Implementation" &&
			                                                     data->SymScope() != mSym->SymScope() + "_Validate")
			{
				auto curDef = data->Def();
				curDef.ReplaceAll("{...}", "");
				WTString curVirtSpec;
				int curVirtSpecPos = VirtSpecPos(curDef);
				if (curVirtSpecPos != -1)
					curVirtSpec = curDef.Left(curVirtSpecPos);
				WTString curPureSpec;
				int curPureSpecPos = PureSpecPos(curDef);
				if (curPureSpecPos != -1)
					curPureSpec = curDef.Mid(curPureSpecPos);

				WTString coreDef = adjustedDeclStr;
				int coreVirtSpecPos = VirtSpecPos(coreDef);
				if (coreVirtSpecPos != -1)
					coreDef = coreDef.Mid(coreVirtSpecPos);
				int corePureSpecPos = PureSpecPos(coreDef);
				if (corePureSpecPos != -1)
					coreDef = coreDef.Left(corePureSpecPos);

				adjustedDeclStr = curVirtSpec + coreDef + curPureSpec;
			}
		}

		const bool isUe4Implementation = data->Sym().EndsWith("_Implementation");
		const bool isUe4Validate = data->Sym().EndsWith("_Validate");
		if (isUe4Implementation || isUe4Validate)
		{
			// [case: 141288] Add "_Implementation" or "_Validate" to the method signature.
			const int openParenIdx = adjustedDeclStr.Find("(");
			WTString retValType;
			if (isUe4Implementation)
			{
				adjustedDeclStr.insert(openParenIdx, "_Implementation");
				retValType = "void"; // [case: 163908] _Implementation is always 'void'
			}
			else if (isUe4Validate)
			{
				adjustedDeclStr.insert(openParenIdx, "_Validate");
				retValType = "bool"; // [case: 163908] _Validate is always 'bool'
			}

			// [case: 163908] ensure that _Implementation and _Validate have correct return value type
			if (!retValType.IsEmpty())
			{
				adjustedDeclStr.TrimLeft();
				if (adjustedDeclStr.Find(retValType) != 0)
				{
					int currentRetValLength = adjustedDeclStr.Find(' ');
					if (currentRetValLength != -1)
						adjustedDeclStr.ReplaceAt(0, currentRetValLength, retValType.c_str());
				}
			}
		}

		bool declIsImpl = false;

		FindSymDef_MLC impl(ed->m_ftype); // use of declFileType here is not quite proper, but probably ok.
		int declCount = 0;

		if (data->IsImpl())
		{
			for (auto iter = lst.begin(); iter != lst.end(); ++iter)
			{
				if (!iter->IsImpl())
					declCount++;
			}

			impl->FindSymbolInFile(data->FilePath(), data, TRUE);
			if (impl->mShortLineText.IsEmpty())
				return rrError;
		}

		// [case: 141288] The declCount for a UE4 implicit method may be zero. The declaration is
		// optional.
		if ((declCount || isUe4Implementation || isUe4Validate) && data->IsImpl())
		{
			// Create Implementation
			VAScopeInfo_MLC cp(GetFileType(data->FilePath()));
			WTString declStr = adjustedDeclStr;
			const WTString lnBrk(ed->GetLineBreakString());
			if (mTemplateStr.GetLength())
				declStr = mTemplateStr + lnBrk + declStr;
			cp->ParseDefStr(StrGetSymScope(sym->SymScope()), declStr, declIsImpl);
			WTString methns = impl->Depth() ? impl->Scope(impl->Depth() - 1) : NULLSTR;
			if (methns.GetLength() > 1)
				cp->mCurSymScope.ReplaceAll(methns + DB_SEP_STR, DB_SEP_STR);
			WTString impText = TokenGetField(cp->ImplementationStrFromDef(true, false, false), "{");
			impText.ReplaceAll("\r\n", "\n");
			impText.ReplaceAll("\r", "\n");
			impText.ReplaceAll("\n", lnBrk);
			impText.TrimLeft();
			if (-1 != impText.Find("STDMETHOD"))
			{
				// [case: 12386]
				impText.ReplaceAll("STDMETHOD(", "STDMETHODIMP");
				if (impText.ReplaceAll("STDMETHOD_(", "STDMETHODIMP_("))
				{
					// fix the comma
					const int commaPos = impText.Find(',');
					if (-1 != commaPos)
					{
						int pos = impText.Find("STDMETHODIMP_(");
						if (-1 != pos)
						{
							pos = impText.Find("(", pos + 15);
							if (-1 != pos && commaPos < pos)
								impText.ReplaceAt(commaPos, 1, ")");
						}
					}
				}
			}

			if (mTemplateStr.GetLength() && !StartsWith(impText, "template"))
				impText = mTemplateStr + lnBrk + impText;

			// Update impl
			if (impl->mCtorInitCode.GetLength())
			{
				// constructor initialization
				// CFoo::CFoo() : member(0) {}
				impText.TrimRight();
				impText += WTString(" ") + impl->mCtorInitCode;
			}

			ed->SetSel(impl->mBegLinePos, impl->GetCp() - 1);
			_f.ReadOnlyCheck();

			impText = ApplyArgWhitespace(ed->GetSelString(), impText.c_str());
			if (Psettings->mUnrealEngineCppSupport)
				impText = RemoveUparamFromImp(impText); // [case: 112654]
			if (!ed->ReplaceSel(impText.c_str(), noFormat))
				return rrError;
			if (gShellAttr->IsMsdev())
				ed->GetBuf(TRUE);
			ed->OnModified(TRUE);
		}
		else
		{
			FindSymDef_MLC decl(ed->m_ftype);
			decl->FindSymbolInFile(ed->FileName(), data, FALSE);
			if (decl->mShortLineText.IsEmpty())
				return rrError;

			// rewind over whitespace - leave trailing whitespace already there as is
			const WTString edBuf(ed->GetBuf());
			int endPos = decl->GetCp() - 1;
			while (endPos > decl->mBegLinePos)
			{
				if (wt_isspace(edBuf[endPos - 1]))
					--endPos;
				else
					break;
			}

			ed->SetSel(decl->mBegLinePos, endPos);

			adjustedDeclStr = ApplyArgWhitespace(ed->GetSelString(), adjustedDeclStr.c_str());
			if (!ed->ReplaceSel(adjustedDeclStr.c_str(), noFormat))
				return rrError;
			if (gShellAttr->IsMsdev())
				ed->GetBuf(TRUE);
			ed->OnModified(TRUE);
		}

		if (data->IsImpl())
		{
			FindSymDefs_MLC curDef(mDeclFileType);
			auto def = data->Def();
			def.ReplaceAll("{...}", "");
			def.ReplaceAll("::", "_");
			if (!curDef->FindSymDefs(def))
				return rrError;

			auto localArgNames = Split(curDef->GetSymNameList());
			if (localArgNames.size() != mMap.OldArgs.size())
				return rrError;

			// rename args
			for (uint i = 1; i < mMap.OldArgs.size(); ++i)
			{
				auto& oldArg = mMap.OldArgs[i];
				if (oldArg.RefIndex == -1)
					continue;

				auto oldName = CStringW(localArgNames[i].Wide()); // oldArg.Name;
				auto newName = mMap.NewArgs[(uint)oldArg.RefIndex].Name;

				if (oldName == newName)
					continue;

				WTString scope = impl->Scope();
				// 						if(scope.GetLength() <= 1)
				// 							scope = decl->Scope();

				// if we renamed the method, we need to update the
				// scope string from the orig impl or decl.
				if (mMap.OldArgs[0].Name != mMap.NewArgs[0].Name)
					scope.ReplaceAll(WTString(mMap.OldArgs[0].Name), WTString(mMap.NewArgs[0].Name), TRUE);

				WTString from = scope + DB_SEP_STR + WTString(oldName);
				WTString to = newName;
				ReplaceReferencesInFile(from, to, ed->FileName());

				ed->m_lastEditSymScope = NULLSTR;
				ed->m_lastEditPos = UINT_MAX;
			}
		}
		return rrSuccess;
	}
	break;
	}
	return rrNoChange;
}

static const char* const kErrorCantParse = "Error: Can't parse signature";
static const char* const kErrorMissingDefault = "Error: Missing default value for argument";
static const char* const kErrorMissingName = "Error: Missing argument name";
static const char* const kErrorDuplicateName = "Error: Duplicate argument names";
static const char* const kErrorTooManyChanges = "Error: Too many changes";
static const char* const kErrorCantRenameCtor = "Error: Can't rename constructors";

BOOL ChangeSignatureDlg::ValidateInput()
{
	if (mEditTxt.IsEmpty())
	{
		SetErrorStatus(kErrorCantParse);
		return FALSE;
	}

	if (mEditTxt == mOrigSig)
	{
		UpdateStatus(TRUE, -1);
		return FALSE;
	}

	if (mEditTxt.Find(';') != -1)
	{
		SetErrorStatus(kErrorCantParse);
		return FALSE;
	}

	std::wregex rx1(L"=\\s*,");
	std::wregex rx2(L"=\\s*\\)");
	std::wstring str = (const wchar_t*)mEditTxt.Wide();
	if (std::regex_search(str, rx1) || std::regex_search(str, rx2))
	{
		SetErrorStatus(kErrorMissingDefault);
		return FALSE;
	}

	FindSymDefs_MLC oldDef(mDeclFileType);
	if (!oldDef->FindSymDefs(mOrigSig))
	{
		SetErrorStatus(kErrorCantParse);
		return FALSE;
	}

	FindSymDefs_MLC newDef(mDeclFileType);
	if (!newDef->FindSymDefs(mEditTxt))
	{
		SetErrorStatus(kErrorCantParse);
		return FALSE;
	}

	{
		mMap.Clear();

		// always build map, even if only whitespace change, so that we
		// catch any parse errors
		// 		if (newDef->GetSymTypeList() != oldDef->GetSymTypeList() ||
		// 			newDef->GetSymNameList() != oldDef->GetSymNameList() ||
		// 			newDef->GetSymDefaultList() != oldDef->GetSymDefaultList())
		{
			auto oldNames = Split(oldDef->GetSymNameList());
			auto newNames = Split(newDef->GetSymNameList());
			auto oldTypes = Split(oldDef->GetSymTypeList());
			auto newTypes = Split(newDef->GetSymTypeList());
			auto oldDefaults = Split(oldDef->GetSymDefaultList());
			auto newDefaults = Split(newDef->GetSymDefaultList());

			const size_t nOld = oldTypes.size();
			const size_t nNew = newTypes.size();

			if (nOld != oldNames.size() || nOld != oldDefaults.size() || nNew != newNames.size() ||
			    nNew != newDefaults.size())
			{
				SetErrorStatus(kErrorCantParse);
				return FALSE;
			}

			for (size_t i = 0; i < nOld; ++i)
			{
				mMap.OldArgs.push_back(ArgInfo());
				mMap.OldArgs[i].Name = oldNames[i].Wide();
			}

			for (size_t i = 0; i < nOld; ++i)
			{
				if (i == 0 && mSym->IsConstructor())
				{
					// return type for ctor must be empty
					if (!oldTypes[i].IsEmpty())
					{
						SetErrorStatus(kErrorCantParse);
						return FALSE;
					}
				}
				else if (oldTypes[i].IsEmpty())
				{
					SetErrorStatus(kErrorCantParse);
					return FALSE;
				}
				mMap.OldArgs[i].Type = oldTypes[i].Wide();
			}

			for (size_t i = 0; i < nOld; ++i)
				mMap.OldArgs[i].DefaultValue = oldDefaults[i].Wide();

			for (size_t i = 0; i < nNew; ++i)
			{
				mMap.NewArgs.push_back(ArgInfo());
				mMap.NewArgs[i].Name = newNames[i].Wide();
			}
			for (size_t i = 0; i < nNew; ++i)
			{
				if (i == 0 && mSym->IsConstructor())
				{
					// return type for ctor must be empty
					if (!newTypes[i].IsEmpty())
					{
						SetErrorStatus(kErrorCantParse);
						return FALSE;
					}
				}
				else if (newTypes[i].IsEmpty())
				{
					SetErrorStatus(kErrorCantParse);
					return FALSE;
				}
				mMap.NewArgs[i].Type = newTypes[i].Wide();
			}

			if (mSym->IsConstructor())
			{
				if (mMap.OldArgs[0].Name != mMap.NewArgs[0].Name)
				{
					SetErrorStatus(kErrorCantRenameCtor);
					return FALSE;
				}
			}

			if (mMap.OldArgs.size() == 2 && mMap.OldArgs[1].Type == "void" && mMap.OldArgs[1].Name.IsEmpty())
			{
				mMap.OldArgs.erase(mMap.OldArgs.begin() + 1);
			}

			if (mMap.NewArgs.size() == 2 && mMap.NewArgs[1].Type == "void" && mMap.NewArgs[1].Name.IsEmpty())
			{
				mMap.NewArgs.erase(mMap.NewArgs.begin() + 1);
			}

			bool hasDefaultArg = false;
			for (size_t i = 0; i < mMap.NewArgs.size(); ++i)
			{
				auto defaultArg = newDefaults[i];
				mMap.NewArgs[i].DefaultValue = defaultArg.Wide();
				if (i)
				{
					defaultArg.Trim();
					if (defaultArg.IsEmpty())
					{
						// all args after one with a default value must have a default also.
						if (hasDefaultArg)
						{
							SetErrorStatus(kErrorMissingDefault);
							return FALSE;
						}
					}
					else
					{
						hasDefaultArg = true;
					}
				}
			}

			mMap.OldArgs[0].RefIndex = 0;
			mMap.NewArgs[0].RefIndex = 0;

			uint newArgIdx, oldArgIdx;

			// check arg names
			for (newArgIdx = 1; newArgIdx < mMap.NewArgs.size(); ++newArgIdx)
			{
				// can't have empty arg names
				if (mMap.NewArgs[newArgIdx].Name.IsEmpty())
				{
					SetErrorStatus(kErrorMissingName);
					return FALSE;
				}

				for (auto newArgIdx2 = newArgIdx + 1; newArgIdx2 < mMap.NewArgs.size(); ++newArgIdx2)
				{
					// can't have multiple args with same name.
					if (mMap.NewArgs[newArgIdx].Name == mMap.NewArgs[newArgIdx2].Name)
					{
						SetErrorStatus(kErrorDuplicateName);
						return FALSE;
					}
				}
			}

			// match arg names
			// look for moved params (same name, but possible type change)
			for (oldArgIdx = 1; oldArgIdx < mMap.OldArgs.size(); ++oldArgIdx)
			{
				for (newArgIdx = 1; newArgIdx < mMap.NewArgs.size(); ++newArgIdx)
				{
					auto& oldArg = mMap.OldArgs[oldArgIdx];
					auto& newArg = mMap.NewArgs[newArgIdx];
					if (newArg.Name == oldArg.Name)
					{
						oldArg.RefIndex = (int)newArgIdx;
						newArg.RefIndex = (int)oldArgIdx;
						if (newArgIdx != (int)oldArgIdx)
							mMap.ArgsReordered = true;
						if (oldArg.IsOptional() != newArg.IsOptional())
							mMap.ArgsReordered = true;
					}
				}
			}

			// look for renamed params (same type in same pos)
			for (oldArgIdx = 1; oldArgIdx < mMap.OldArgs.size(); ++oldArgIdx)
			{
				auto& oldArg = mMap.OldArgs[oldArgIdx];
				if (oldArg.RefIndex != -1)
					continue;

				for (newArgIdx = 1; newArgIdx < mMap.NewArgs.size(); ++newArgIdx)
				{
					auto& newArg = mMap.NewArgs[newArgIdx];
					if (newArg.RefIndex != -1)
						continue;

					if (newArg.Type == oldArg.Type)
					{
						oldArg.RefIndex = (int)newArgIdx;
						newArg.RefIndex = (int)oldArgIdx;
						if (oldArg.IsOptional() != newArg.IsOptional())
							mMap.ArgsReordered = true;
						break;
					}
				}
			}

			// special case, single arg always matches
			if (mMap.NewArgs.size() == 2 && mMap.OldArgs.size() == 2 && mMap.NewArgs[1].RefIndex == -1 &&
			    mMap.OldArgs[1].RefIndex == -1)
			{
				mMap.NewArgs[1].RefIndex = 1;
				mMap.OldArgs[1].RefIndex = 1;
			}

			int unresolvedOldArgs = 0;
			int unresolvedNewArgs = 0;

			for (auto iter = mMap.OldArgs.begin(); iter != mMap.OldArgs.end(); ++iter)
			{
				auto& oldArg = *iter;
				if (oldArg.RefIndex == -1)
					unresolvedOldArgs++;
			}

			for (auto iter = mMap.NewArgs.begin(); iter != mMap.NewArgs.end(); ++iter)
			{
				auto& newArg = *iter;
				if (newArg.RefIndex == -1)
					unresolvedNewArgs++;
			}

			if (unresolvedNewArgs && unresolvedOldArgs)
			{
				SetErrorStatus(kErrorTooManyChanges);
				return FALSE;
			}
			if (unresolvedNewArgs || unresolvedOldArgs)
				mMap.ArgsReordered = true;
		}
	}

	UpdateStatus(TRUE, -1);
	return TRUE;
}

BOOL ChangeSignatureDlg::Init(DTypePtr sym, const WTString& bcl, BOOL mChangeUe4ImplicitMethods /*= FALSE*/)
{
	if (!sym)
		return FALSE;

	mEdCnt = g_currentEdCnt;
	mSym = sym.get();
	mBCL = bcl;

	const CStringW declFilePath = FileFromDef(mSym);
	mDeclFileType = GetFileType(declFilePath);
	FindSymDef_MLC decl(mDeclFileType);
	//	FindSymDef_MLC impl(mDeclFileType); // use of mDeclFileType here is not quite proper, but probably ok.

	decl->FindSymbolInFile(declFilePath, mSym, FALSE);
	decl->mShortLineText.ReplaceAll("{...}", "");
	mTemplateStr = decl->GetTemplateStr();
	if (decl->mShortLineText.IsEmpty())
		return FALSE;
	mEditTxt = mOrigSig = decl->mShortLineText;
	m_symScope = sym->SymScope();

	if (mChangeUe4ImplicitMethods)
		mRefs->flags = mRefs->flags | FREF_Flg_UeFindImplicit;
	else
		mRefs->flags = mRefs->flags & ~FREF_Flg_UeFindImplicit;

	return TRUE;
}

BOOL ChangeSignatureDlg::OnUpdateStart()
{
	int paramCounter = 0;
	int filledParamCount = INT_MAX;
	std::vector<int> valueDialogs;
	for (auto iter = mMap.NewArgs.begin(); iter != mMap.NewArgs.end(); ++iter)
	{
		bool removedDefaultVal = false;
		if (iter->RefIndex != -1)
			removedDefaultVal =
			    mMap.OldArgs[(uint)iter->RefIndex].DefaultValue.IsEmpty() == false && iter->DefaultValue.IsEmpty();
		if (iter->RefIndex == -1 || removedDefaultVal)
		{
			for (size_t i = 0; i < mRefs->Count(); ++i)
			{
				auto ref = mRefs->GetReference(i);
				if (ref && (ref->type == FREF_Reference || ref->type == FREF_ScopeReference) && ref->mData &&
				    !ref->mData->IsConstructor())
				{
					if (removedDefaultVal)
					{
						ArgTracker af(mDeclFileType);
						int pos = ref->lnText.Find(
						    "):"); // warning: I assume that there is a "):" before each function in the find references
						           // list - didn't find position or function name for this yet
						if (pos != -1)
						{
							pos += 2;
							af.ParseTo(ref->lnText.c_str() + pos, ref->lnText.GetLength() - pos, ");");
							int paramCount = (int)af.ArgCount();
							if (paramCount == 1)
							{
								WTString trimmed = af.GetArg(0);
								trimmed.TrimLeft();
								if (trimmed == "")
									paramCount--;
							}
							int paramPosInOldMap = mMap.NewArgs[(uint)paramCounter].RefIndex;
							if (paramCount < paramPosInOldMap)
							{
								valueDialogs.push_back(paramCounter);
								if (paramCounter - 1 < filledParamCount)
									filledParamCount = paramCounter - 1;
								break;
							}
						}
					}
					else
					{
						valueDialogs.push_back(paramCounter);
						if (paramCounter - 1 < filledParamCount)
							filledParamCount = paramCounter - 1;
						break;
					}
				}
			}
		}
		paramCounter++;
	}

	for (uint i = 0; i < valueDialogs.size(); i++)
	{
		if (!GetTodoValue((uint)valueDialogs[i], i == valueDialogs.size() - 1))
			return FALSE;
	}

	mRefsForResultsWnd.reset(new FindReferences(*(mRefs.get()), false));
	if (mMap.NewArgs.size())
	{
		auto newName = mMap.NewArgs[0].Name;
		mRefsForResultsWnd->UpdateSymForRename(newName, StrGetSymScope(mRefs->GetFindScope()) + ':' + WTString(newName));
	}
	RefactoringActive::SetCurrentRefactoring(VARef_ChangeSignature);

	return TRUE;
}

void ChangeSignatureDlg::OnUpdateComplete()
{
	// [case: 147774] handle unreal engine core redirects
	if (Psettings->mUnrealEngineCppSupport && mCreateCoreRedirects && mIsUEMarkedType)
	{
		if (mMap.NewArgs.size()) // need this to find the new name; if doesn't exist, ignore
		{
			try
			{
				auto newName = mMap.NewArgs[0].Name;
				WriteCodeRedirectsForUE(mRefs->GetFindSym().Wide(), newName);
			}
			catch (...)
			{
				// this should not happen but if it is, we will just not write core redirect
				vLog("ERROR: exception caught while writing core redirects during change signature\n");
			}
		}
	}
	
	RefactoringActive::SetCurrentRefactoring(0);
}

void ChangeSignatureDlg::SetErrorStatus(LPCSTR msg)
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	const WTString txt(msg);
	if (txt == mStatusText)
		return;

	mStatusText = txt;
	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), txt.Wide());
}

BOOL ChangeSignatureDlg::AppendArg(WTString newParam)
{
	auto oldSig = mOrigSig;
	WTString newSig;

	const int openParenPos = oldSig.Find("(");
	if (-1 == openParenPos)
		return FALSE;

	const int closeParenPos = oldSig.Find(")", openParenPos);
	if (-1 == closeParenPos)
		return FALSE;

	int spaceCnt = 0;
	newSig = oldSig.Left(closeParenPos);

	// deal with current params
	if (closeParenPos != (openParenPos + 1))
	{
		WTString betweenParens(oldSig.Mid(openParenPos + 1, closeParenPos - openParenPos - 1));

		// count trailing spaces after current last param
		while (betweenParens.GetLength() && betweenParens[betweenParens.GetLength() - 1] == ' ')
		{
			++spaceCnt;
			betweenParens = betweenParens.Left(betweenParens.GetLength() - 1);
			if (newSig[newSig.GetLength() - 1] == ' ')
				newSig = newSig.Left(newSig.GetLength() - 1);
		}

		betweenParens.TrimLeft();
		if (!betweenParens.IsEmpty())
		{
			if (betweenParens == "void")
			{
				// replace void in foo(void)
				newSig = newSig.Left(newSig.GetLength() - 4);
			}
			else
			{
				// already have some params, need comma
				newSig += ", ";
			}
		}
	}

	newSig += newParam;

	// move current trailing spaces to after the new param
	for (int idx = 0; idx < spaceCnt; ++idx)
		newSig += " ";

	newSig += oldSig.Mid(closeParenPos);

	mEditTxt = newSig;
	mDisableEdit = true;
	return TRUE;
}

DTypeList& ChangeSignatureDlg::GetDTypeCache(DTypePtr sym)
{
	auto symScope = sym->SymScope();
	auto iter = mSymMap.find(symScope);
	if (iter == mSymMap.end())
	{
		DTypeList lst;
		EdCntPtr ed(g_currentEdCnt ? g_currentEdCnt : mEdCnt);
		if (ed)
		{
			MultiParsePtr mp = ed->GetParseDb();
			if (mp)
				mp->FindExactList(sym->SymHash(), sym->ScopeHash(), lst, false);
		}
		// do not call lst.FilterDupesAndGotoDefs() -- see some change prior to change 33890
		mSymMap[symScope] = lst;
	}
	return mSymMap[symScope];
}

// use the arg whitespace from curSig (existing decl/impl before chg sig)
// to reformat newSig.
WTString ChangeSignatureDlg::ApplyArgWhitespace(WTString curSig, LPCSTR newSig)
{
	WTString adjustedDeclStr = newSig;

	ArgTracker atOld(mDeclFileType);
	if (!atOld.ParseTo(curSig.c_str(), curSig.GetLength(), ");{", curSig.GetLength()))
		return adjustedDeclStr;

	ArgTracker atNew(mDeclFileType);
	if (!atNew.ParseTo(newSig, adjustedDeclStr.GetLength(), ");{", adjustedDeclStr.GetLength()))
		return adjustedDeclStr;

	if (atOld.ArgCount() != mMap.OldArgs.size() - 1)
		return adjustedDeclStr;
	if (atNew.ArgCount() != mMap.NewArgs.size() - 1)
		return adjustedDeclStr;

	bool preserveNewline = atOld.ArgCount() > 1 ? true : false;
	for (uint i = 1; i < atOld.ArgCount(); i++)
	{
		WTString pre;
		atOld.GetArgWhitespace(i, &pre, nullptr);
		if (pre.Find("\n") == -1)
			preserveNewline = false;
	}

	int insertOffset = 0;
	bool modifyNext = false;
	bool modifyNow = false;
	for (uint i = 1; i < mMap.NewArgs.size(); ++i)
	{
		auto& newArg = mMap.NewArgs[i];

		const bool isFirstArg = i == 1;
		const bool wasFirstArg = newArg.RefIndex == 1;
		const bool isLastArg = i == mMap.NewArgs.size() - 1;
		const bool wasLastArg = newArg.RefIndex == (int)(mMap.OldArgs.size() - 1);
		const bool isBeforeOldArgs = mMap.OldArgs.size() > 1 && (int)i < mMap.OldArgs[1].RefIndex;

		if (modifyNext)
		{
			modifyNow = true;
			modifyNext = false;
		}

		WTString wsPre, wsPost;
		if (newArg.RefIndex == -1)
		{
			wsPre = i > 1 ? " " : "";
			wsPost = "";

			if (isFirstArg)
			{
				atOld.GetArgWhitespace(0, &wsPre, nullptr);
			}
			if (isBeforeOldArgs)
			{
				if (preserveNewline)
					modifyNext = true;
			}
			else
			{
				if (preserveNewline)
					atOld.GetArgWhitespace(atOld.ArgCount() - 1, &wsPre, &wsPost);
			}
			if (isLastArg)
			{
				if (preserveNewline)
					atOld.GetArgWhitespace(atOld.ArgCount() - 1, &wsPre, &wsPost);
				else
					atOld.GetArgWhitespace(atOld.ArgCount() - 1, nullptr, &wsPost);
			}
		}
		else
		{
			atOld.GetArgWhitespace(uint(newArg.RefIndex - 1), &wsPre, &wsPost);
			if (wasFirstArg && !isFirstArg)
				wsPre = " ";
			else if (isFirstArg && !wasFirstArg)
				atOld.GetArgWhitespace(0, &wsPre, nullptr);

			if (wasLastArg && !isLastArg)
				wsPost = "";
			else if (isLastArg && !wasLastArg)
				atOld.GetArgWhitespace(atOld.ArgCount() - 1, nullptr, &wsPost);
		}
		if (modifyNow)
		{
			atOld.GetArgWhitespace(atOld.ArgCount() - 1, &wsPre, nullptr);

			modifyNow = false;
		}

		{
			int argStart = 0, argEnd = 0;
			auto newArg2 = atNew.GetArg(i - 1, &argStart, &argEnd);
			newArg2.Trim();
			newArg2 = wsPre + newArg2 + wsPost;

			if (argStart && argEnd)
			{
				adjustedDeclStr.ReplaceAt(insertOffset + argStart, argEnd - argStart, newArg2.c_str());
				insertOffset += newArg2.GetLength() - (argEnd - argStart);
			}
		}
	}

	WTString curSigTrimmed = curSig;
	curSigTrimmed.TrimRight();
	WTString curSigEndWs = curSig.Mid(curSigTrimmed.GetLength());

	adjustedDeclStr.TrimRight();
	adjustedDeclStr += curSigEndWs;

	return adjustedDeclStr;
}

class GetArgValueDlg : public CThemedVADlg
{
  public:
	GetArgValueDlg(CStringW fn, ArgInfo& argInfo, bool more)
	    : CThemedVADlg(IDD_CHANGESIGNATURE_ARG, NULL, fdAll, flDefault), mArg(argInfo), mMore(more), mFn(fn)
	{
	}

  protected:
	virtual BOOL OnInitDialog() override
	{
		__super::OnInitDialog();

		AddSzControl(IDC_EDIT1, mdResize, mdRepos);
		AddSzControl(IDC_EDIT1_LABEL, mdNone, mdRepos);
		AddSzControl(IDOK, mdRepos, mdNone);
		AddSzControl(IDCANCEL, mdRepos, mdNone);
		AddSzControl(IDC_EDIT2_LABEL, mdResize, mdResize);
		SetHelpTopic(WTString("dlgChangeSignatureParameterUpdate"));

		if (GetDlgItem(IDC_EDIT1))
		{
			// [case: 9194] do not use DDX_Control due to ColourizeControl.
			// Subclass with colourizer before SHAutoComplete (CtrlBackspaceEdit).
			mEdit_subclassed.SubclassWindow(GetDlgItem(IDC_EDIT1)->m_hWnd);
		}

		if (CVS2010Colours::IsExtendedThemeActive())
		{
			Theme.AddDlgItemForDefaultTheming(IDC_EDIT1_LABEL);
			Theme.AddDlgItemForDefaultTheming(IDC_EDIT2_LABEL);
			Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
			Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
			ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
		}

		WTString argName = mArg.Name;
		argName.Trim();
		mFn.Trim();
		WTString msg = WTString("Set value of parameter '") + argName + "' to be used for update of calls to '" +
		               WTString(mFn) + "'";
		::SetWindowTextW(GetDlgItem(IDC_EDIT2_LABEL)->GetSafeHwnd(), msg.Wide());

		GetDlgItem(IDOK)->SetWindowTextA(mMore ? "Continue" : "OK");

		auto name = mArg.Type + CStringW(" ") + mArg.Name;
		::SetWindowTextW(GetDlgItem(IDC_EDIT1_LABEL)->GetSafeHwnd(), name);

		WTString argStr = mArg.TodoValue;
		if (mEdit_subclassed.GetSafeHwnd())
		{
			mEdit_subclassed.SetText(argStr.Wide());
			mEdit_subclassed.SetSel(0, -1);
			mEdit_subclassed.SetFocus();
		}
		return FALSE;
	}

	virtual void OnOK() override
	{
		CStringW tmp;

		if (mEdit_subclassed.GetSafeHwnd())
			mEdit_subclassed.GetText(tmp);

		mArg.TodoValue = tmp;
		__super::OnOK();
	}

	afx_msg void OnDestroy()
	{
		if (mEdit_subclassed.m_hWnd)
			mEdit_subclassed.UnsubclassWindow();

		__super::OnDestroy();
	}

	DECLARE_MESSAGE_MAP()

	ArgInfo& mArg;
	bool mMore;
	CStringW mFn;
	CtrlBackspaceEdit<CThemedEdit> mEdit_subclassed;
};

BEGIN_MESSAGE_MAP(GetArgValueDlg, CThemedVADlg)
//{{AFX_MSG_MAP(GetArgValueDlg)
ON_WM_DESTROY()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void SetTodoValueForType(ArgInfo& argInfo)
{
	CStringW argType(argInfo.Type);
	argType.Replace(L"constexpr ", L"");
	argType.Replace(L"_CONSTEXPR17 ", L"");
	argType.Replace(L"_CONSTEXPR20_CONTAINER", L"");
	argType.Replace(L"_CONSTEXPR20 ", L"");
	argType.Replace(L"const ", L"");
	argType.Replace(L"&", L"");
	argType.Trim();

	if (argType == L"int")
		argInfo.TodoValue = L"0";
	else if (argType == L"bool")
		argInfo.TodoValue = L"false";
	else if (argType == L"BOOL")
		argInfo.TodoValue = L"FALSE";
	else if (argType == L"HANDLE")
		argInfo.TodoValue = L"INVALID_HANDLE_VALUE";
	else if (argType == L"HRESULT")
		argInfo.TodoValue = L"S_OK";
	else if (-1 != argType.Find(L"*") || argType == L"HWND")
		argInfo.TodoValue = L"NULL";
	else if (-1 != argType.Find(L"string") || -1 != argType.Find(L"String"))
		argInfo.TodoValue = L"\"\"";
	else
		argInfo.TodoValue = L"TODO";
}

bool ChangeSignatureDlg::GetTodoValue(uint paramIndex, bool lastParam)
{
	auto& argInfo = mMap.NewArgs[paramIndex];
	bool removedDefaultVal = false;
	if (argInfo.RefIndex != -1)
		removedDefaultVal =
		    mMap.OldArgs[(uint)argInfo.RefIndex].DefaultValue.IsEmpty() == false && argInfo.DefaultValue.IsEmpty();
	if (argInfo.RefIndex == -1 || removedDefaultVal)
	{
#if 1
		if (removedDefaultVal)
		{
			argInfo.TodoValue = mMap.OldArgs[(uint)argInfo.RefIndex].DefaultValue;
			if (argInfo.TodoValue[0] == CStringW("="))
				argInfo.TodoValue.Delete(0, 1);
			argInfo.TodoValue.TrimLeft();
		}
		else
		{
			// force user to enter something or cause compilation error
			argInfo.TodoValue = "TODO";
		}
#else
		::SetTodoValueForType(argInfo);
#endif

		// If the arg has a default value, don't prompt user for value
		if (argInfo.DefaultValue.IsEmpty())
		{
			GetArgValueDlg dlg(mMap.NewArgs[0].Name, argInfo, !lastParam);
			auto ret = dlg.DoModal();
			if (ret != IDOK)
				return false;
		}
		else
		{
			auto eqPos = argInfo.DefaultValue.Find('=');
			if (eqPos != -1)
			{
				auto todoVal = argInfo.DefaultValue.Mid(eqPos + 1);
				todoVal.TrimLeft();
				todoVal.TrimRight();
				argInfo.TodoValue = todoVal;
			}
		}
	}

	return true;
}

void ChangeSignatureDlg::SearchResultsForIssues(int& deepestBaseClassPos, HTREEITEM item /*= TVI_ROOT*/)
{
	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		int data = (int)m_tree.GetItemData(childItem);
		if (!IS_FILE_REF_ITEM(data))
		{
			FindReference* ref = mRefs->GetReference((uint)data);
			if (ref && ref->type == FREF_Definition)
			{
				DType* data2 = ref->mData.get();
				if (data2)
				{
					WTString scope = data2->Scope();
					WTString innerScopeName = StrGetSym(scope); // class name
					int baseClassPos = FindWholeWordInCode(mBCL, innerScopeName, Src, 0);
					if (baseClassPos > deepestBaseClassPos)
					{
						deepestBaseClassPos = baseClassPos;
						mBaseClassWarning = "Warning: run Change Signature in base class \"";
						mBaseClassWarning += innerScopeName;
						mBaseClassWarning += "\" so all references in its derived classes can be updated";
						if (gTestLogger)
							gTestLogger->LogStrW(L"Warning: run Change Signature from base class");
					}
				}
			}
		}

		if (m_tree.ItemHasChildren(childItem))
			SearchResultsForIssues(deepestBaseClassPos, childItem);

		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

HBRUSH
ChangeSignatureDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	auto result = __super::OnCtlColor(pDC, pWnd, nCtlColor);

	if (pWnd->GetDlgCtrlID() == IDC_WARNING)
	{
		pDC->SetBkMode(TRANSPARENT);
		result = mWarningBrush;
	}

	return result;
}

void ChangeSignatureDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		try
		{
			WTString logStr;
			logStr.WTFormat(
			    "Change Signature results symScope(%s) sig(%s) auto(%d) fileCnt(%d) refCnt(%zu) canceled(%d)\r\n",
			    m_symScope.c_str(), mEditTxt.c_str(), mAutoUpdate, fileCount, mRefs->Count(), wasCanceled);
			gTestLogger->LogStr(logStr);
			gTestLogger->LogStrW(mRefs->GetSummary());
		}
		catch (...)
		{
			gTestLogger->LogStr(WTString("Exception logging Change Signature results."));
		}
	}

	if (!mBCL.IsEmpty())
	{
		int deepestBaseClassPos = -1;
		SearchResultsForIssues(deepestBaseClassPos);
	}

	__super::OnSearchComplete(fileCount, wasCanceled);
}
