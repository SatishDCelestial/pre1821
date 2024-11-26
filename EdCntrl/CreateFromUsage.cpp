#include "StdAfxEd.h"
#include "CreateFromUsage.h"
#include "AutotextManager.h"
#include "VARefactor.h"
#include "EDCNT.H"
#include "PROJECT.H"
#include "FileTypes.h"
#include "Mparse.h"
#include "FOO.H"
#include "TraceWindowFrame.h"
#include "UndoContext.h"
#include "FILE.H"
#include "AddClassMemberDlg.h"
#include "FreezeDisplay.h"
#include "VAParse.h"
#include "GenericMultiPrompt.h"
#include "fdictionary.h"
#include "Expansion.h"
#include "CreateFileDlg.h"
#include "VAAutomation.h"
#include "InferType.h"
#include "FileId.h"
#include "LocalRefactoring.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static bool ValidateData(DType* data);

CreateFromUsage::CreateFromUsage()
    : mParentDataIfAsMember(NULL), mDataForKeywordThis(NULL), mMp(NULL), mCreateType(ctUndetermined),
      mStaticMember(false), mConst(false), mInvokedOnXref(false), mConstKeywordThis(false),
      mMemberInitializationList(false), mMemberVisibility(vPublic), mConstExpr(false), mConstEval(false)
{
}

BOOL CreateFromUsage::CanCreate()
{
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd || !Is_C_CS_File(curEd->m_ftype))
	{
		// all other languages not supported
		// (brace assumptions, snippets not created)
		return FALSE;
	}

	if (curEd->HasSelection())
		return FALSE; // case=31604

	mMp = curEd->GetParseDb();
	if (mMp->m_isDef || mMp->IsSysFile())
		return FALSE;

	WTString symScope(curEd->GetSymScope());
	if (curEd->GetSymDtypeType() != UNDEF)
	{
		// [case: 30654] SymType might be for left substring rather than whole string
		// confirm w/ ScopeInfoPtr
		ScopeInfoPtr si = curEd->ScopeInfoPtr();
		if (si->GetCwData())
			return FALSE;

		// EdCnt::Scope sets SymScope based on m_cwdLeftOfCursor, but
		// if m_cwdLeftOfCursor is only the prefix to cwd, blow it away
		_ASSERTE(curEd->m_cwdLeftOfCursor.GetLength());
		_ASSERTE(curEd->m_cwd.Find(curEd->m_cwdLeftOfCursor) == 0);
		_ASSERTE(curEd->m_cwd != curEd->m_cwdLeftOfCursor);
		symScope.Empty();
	}

	mInvokingScope = curEd->m_lastScope;
	if (-1 == mInvokingScope.Find('-'))
	{
		// only allow within functions
		return FALSE;
	}

	mNewSymbolName = curEd->CurWord();
	mNewSymbolName.Trim();
	if (mNewSymbolName.IsEmpty() || !ISCSYM(mNewSymbolName[0]))
	{
		// see if caret is at start of word (CurWord does not work at start of word)
		mNewSymbolName = curEd->WordRightOfCursor();
		mNewSymbolName.Trim();
		if (mNewSymbolName.IsEmpty())
			return FALSE;
	}

	if (::wt_isdigit(mNewSymbolName[0]))
		return FALSE; // case=31604
	if (!ISCSYM(mNewSymbolName[0]))
		return FALSE; // case=32289

	mBaseScope = mMp->m_baseClass;
	if (mBaseScope.GetLength() && curEd->m_ftype == Src)
	{
		// [case: 20072]
		const WTString baseScopeBcl = mMp->GetBaseClassList(mBaseScope);
		AdjustScopesForNamespaceUsings(mMp.get(), baseScopeBcl, mBaseScope, mInvokingScope);
	}

	mWordAfterNext = curEd->WordRightOfCursorNext();
	mWordAfterNext.TrimRight();
	if (mWordAfterNext.IsEmpty())
	{
		// [case: 50606] account for space between name and ()
		WTString lineText(curEd->GetLine(curEd->CurLine()));
		int pos = lineText.Find(mNewSymbolName);
		if (-1 != pos)
		{
			lineText = lineText.Mid(pos + mNewSymbolName.GetLength());
			lineText.TrimLeft();
			if (!lineText.IsEmpty() && lineText[0] == '(')
				mWordAfterNext = lineText.Left(1);
		}
	}

	BOOL retval = FALSE;

	if (mMp->m_xref && !mMp->m_xrefScope.IsEmpty() && mMp->m_xrefScope != DB_SEP_STR && mMp->m_xrefScope != ":return")
	{
		retval = HaveXrefToCreate(curEd);
	}
	else if (((!mMp->m_xref && mMp->m_xrefScope.IsEmpty()) ||
	          (mMp->m_xref && (mMp->m_xrefScope == DB_SEP_STR || mMp->m_xrefScope == ":return"))) &&
	         symScope.IsEmpty())
	{
		if (mMp->m_xref && (mMp->m_xrefScope == DB_SEP_STR || mMp->m_xrefScope == ":return"))
		{
			// watch out for:
			// BadClass::SomeMethod();
			// SomeMethod comes through as xref with xrefScope(DB_SEP_STR)
			WTString prevWd(curEd->CurWord(-1));
			if (!prevWd.IsEmpty() && !wt_isspace(prevWd[0]))
			{
				if (prevWd != "::")
					return FALSE;

				prevWd = curEd->CurWord(-2);
				if (!prevWd.IsEmpty() && !wt_isspace(prevWd[0]))
					return FALSE;
			}
		}

		retval = HaveNonXrefToCreate(curEd);
	}

	if (!retval)
		return FALSE;

	mFileInvokedFrom = curEd->FileName();
	if (ctMember == mCreateType || ctMethod == mCreateType || ctEnumMember == mCreateType)
	{
		_ASSERTE(mParentDataIfAsMember);
	}
	else
	{
		_ASSERTE(ctUndetermined != mCreateType);
		_ASSERTE(!mParentDataIfAsMember);
		_ASSERTE(ctFunction != mCreateType || CS != gTypingDevLang);
		_ASSERTE(ctGlobal != mCreateType || CS != gTypingDevLang);
	}

	return TRUE;
}

BOOL CreateFromUsage::Create()
{
	TraceScopeExit tse("Create Member From Unknown exit");

	if (!CanCreate())
	{
		Log("CreateFromUsage - invoked, but denied due to state change");
		return FALSE;
	}

	RefineCreationType();
	if (ctClass == mCreateType)
		return CreateClass();

	if (mParentDataIfAsMember)
	{
		_ASSERTE(AsMember());
		mDeclFile = gFileIdManager->GetFileForUser(mParentDataIfAsMember->FileId());
		if (!mDeclFile.GetLength())
		{
			Log("ERR: CreateFromUsage - no decl file");
			return FALSE;
		}

		mSrcFile = ::GetFileByType(mDeclFile, Src);
		if (!mSrcFile.IsEmpty() && mSrcFile == mDeclFile)
			mSrcFile.Empty();
	}
	else
	{
		_ASSERTE(!AsMember());
		mDeclFile = mFileInvokedFrom;
	}

	if (ctMethod == mCreateType || ctMember == mCreateType)
	{
		if (CS == gTypingDevLang && mFileInvokedFrom == mDeclFile)
			mMemberVisibility = vInternal;

		if (!mInvokedOnXref)
			mMemberVisibility = vPrivate;

		if (ctMethod == mCreateType)
		{
			// [case: 31277] check to see if caret is in ctor initialization list
			// make this check only if they actually exec - so that it has no
			// impact during QueryStatus
			// how can we tell if we are in the initialization list, not in the ctor body?
			//			if (xxx)
			//				mCreateType = ctMember;
		}
	}

	WTString newCodeToAdd(GenerateSourceToAdd());
	if (ctMethod == mCreateType && !mSrcFile.IsEmpty() && !mDeclFile.IsEmpty() && IsCFile(gTypingDevLang))
	{
		// only trim implementations that we are going to later Move to Source
		newCodeToAdd.Trim();
	}
	if (newCodeToAdd.IsEmpty())
	{
		Log("ERR: CreateFromUsage - no code generated");
		return FALSE;
	}

	UndoContext undoContext((WTString("Create ") + mNewSymbolName).c_str());
	FreezeDisplay _f;
	if (mDeclFile != mFileInvokedFrom)
		g_currentEdCnt = ::DelayFileOpen(mDeclFile);
	_ASSERTE(g_currentEdCnt);
	if (!g_currentEdCnt)
	{
		Log("ERR: CreateFromUsage - no editor");
		return FALSE;
	}

	BOOL rslt = FALSE;
	const WTString newMethodScope((mParentDataIfAsMember ? mParentDataIfAsMember->SymScope() : "") + DB_SEP_STR +
	                              mNewSymbolName);
	if (ctEnumMember == mCreateType)
		rslt = InsertEnumItem(newMethodScope, newCodeToAdd, _f);
	else if (ctMember == mCreateType || ctMethod == mCreateType)
		rslt = InsertXref(newMethodScope, newCodeToAdd, _f);
	else if (ctFunction == mCreateType || ctGlobal == mCreateType)
		rslt = InsertGlobal(newCodeToAdd, _f);
	else if (ctVariable == mCreateType)
		rslt = InsertLocalVariable(newCodeToAdd, _f);
	else if (ctParameter == mCreateType)
		return InsertParameter(newCodeToAdd);
	else
	{
		_ASSERTE(!"unsupported mCreateType");
		Log("ERR: CreateFromUsage - unsupported");
		return FALSE;
	}

	if (!rslt)
	{
		OnRefactorErrorMsgBox();
		return FALSE;
	}

	if (mSrcFile.IsEmpty())
		return rslt;

	// Move to source file
	// Pass method scope because caret is not on new method
	if (g_currentEdCnt) // declFile
		g_currentEdCnt->GetBuf(TRUE);
	VARefactorCls ref(newMethodScope.c_str());
	const DType dt(g_currentEdCnt->GetSymDtype());
	if (ref.CanMoveImplementationToSrcFile(&dt, "", TRUE))
		rslt = ref.MoveImplementationToSrcFile(&dt, "", TRUE);
	if (!rslt)
		OnRefactorErrorMsgBox();

	return rslt;
}

WTString CreateFromUsage::GenerateSourceToAdd()
{
	if (ctMethod == mCreateType || ctFunction == mCreateType)
	{
		return GenerateSourceToAddForFunction();
	}
	else if (ctEnumMember == mCreateType)
	{
		mSrcFile.Empty();
		// Assumes there is already at least one item
		return WTString(", ") + mNewSymbolName;
	}
	else
	{
		_ASSERTE(ctClass != mCreateType);
		return GenerateSourceToAddForVariable();
	}
}

BOOL CreateFromUsage::ValidateParentData()
{
	if (mParentDataIfAsMember)
	{
		const uint parentType = mParentDataIfAsMember->MaskedType(); // IsType is not strict enough
		if (STRUCT == parentType || CLASS == parentType || NAMESPACE == parentType ||
		    (ctEnumMember == mCreateType && C_ENUM == parentType) || C_INTERFACE == parentType)
		{
			if (::ValidateData(mParentDataIfAsMember))
				return TRUE;
		}
	}

	mParentDataIfAsMember = NULL;
	return FALSE;
}

BOOL CreateFromUsage::HaveXrefToCreate(EdCntPtr ed)
{
	// ex 1:
	//		Foo::AddThis;
	// ex 2:
	//		Foo ff;
	//		ff.AddThat;
	WTString curWd(ed->CurWord());
	if (curWd == mNewSymbolName)
		curWd = ed->CurWord(-1);

	bool ptrHack = false;
	bool xrefViaVar = false;
	LookupParentDataIfAsMember(mMp->m_xrefScope);

	if (mParentDataIfAsMember && curWd == "->")
	{
		// [case: 31318] watch out for smart pointers
		WTString scp(mParentDataIfAsMember->SymScope());
		if (scp.EndsWith(":->"))
		{
			scp = scp.Left(scp.GetLength() - 3);
			DType* tmp = mMp->FindOpData(curWd, scp, NULL);
			if (tmp && tmp->ScopeHash() == mParentDataIfAsMember->ScopeHash() &&
			    tmp->SymHash() == mParentDataIfAsMember->SymHash() &&
			    tmp->FileId() == mParentDataIfAsMember->FileId() && tmp->Line() == mParentDataIfAsMember->Line() &&
			    tmp->Attributes() == mParentDataIfAsMember->Attributes() &&
			    tmp->DbFlags() == mParentDataIfAsMember->DbFlags() && tmp->type() == mParentDataIfAsMember->type())
			{
				WTString varSymScope(mParentDataIfAsMember->SymScope());
				const WTString varDef(mParentDataIfAsMember->Def());
				if (0 == varDef.Find("pointer "))
				{
					// [case: 64935] std::unique_ptr<foo>::->
					ptrHack = true;
				}
			}

			if (tmp)
			{
				mParentDataIfAsMember = tmp;
				mParentDataIfAsMember->LoadStrs();
			}
		}
	}

	if (mParentDataIfAsMember &&
	    (mParentDataIfAsMember->MaskedType() == VAR || mParentDataIfAsMember->MaskedType() == FUNC))
	{
		xrefViaVar = true;
		WTString varDef(mParentDataIfAsMember->Def());
		if (varDef.Find("const ") != -1 || varDef.Find("constexpr ") != -1 || varDef.Find("_CONSTEXPR17 ") != -1 ||
		    varDef.Find("_CONSTEXPR20 ") != -1 || varDef.Find("_CONSTEXPR20_CONTAINER ") != -1)
			mConst = true;

		WTString varSymScope(mParentDataIfAsMember->SymScope());

		// [case: 52335]
		varDef.ReplaceAll("static", "", TRUE);
		varDef.ReplaceAll("thread_local", "", TRUE);
		varDef.ReplaceAll("tile_static", "", TRUE);
		varDef.ReplaceAll("const", "", TRUE);
		varDef.ReplaceAll("constexpr", "", TRUE);
		varDef.ReplaceAll("consteval", "", TRUE);
		varDef.ReplaceAll("constinit", "", TRUE);
		varDef.ReplaceAll("_CONSTEXPR17", "", TRUE);
		varDef.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
		varDef.ReplaceAll("_CONSTEXPR20", "", TRUE);
		varDef.ReplaceAll("literal", "", TRUE);
		varDef.ReplaceAll("initonly", "", TRUE);
		varDef.ReplaceAll("readonly", "", TRUE);
		varDef.ReplaceAll("mutable", "", TRUE);
		varDef.TrimLeft();

		WTString types;
		try
		{
			types = ::GetTypesFromDef(varSymScope, varDef, (int)mParentDataIfAsMember->MaskedType(), gTypingDevLang);
		}
		catch (const WtException&)
		{
		}
		if (types.GetLength() > 1)
		{
			if (types[types.GetLength() - 1] == '\f')
			{
				types = types.Left(types.GetLength() - 1);
				types.TrimRight();
			}
			LookupParentDataIfAsMember(types);
			if (!mParentDataIfAsMember)
			{
				// cast a wider net
				WTString bcl(mMp->GetBaseClassList(varSymScope));
				if (!bcl.IsEmpty())
				{
					if (ptrHack)
					{
						// [case: 64935] std::unique_ptr<foo>::->
						WTString searchStr(":pointer\f");
						int pos = bcl.Find(searchStr);
						if (-1 != pos)
							bcl = bcl.Mid(pos + searchStr.GetLength());
					}

					if (bcl[0] == '\f')
						bcl = bcl.Mid(1);
					int pos = bcl.Find('\f');
					if (-1 != pos)
						bcl = bcl.Left(pos);

					// [case: 112952]
					pos = varDef.Find("_NODISCARD ");
					if (-1 != pos)
						varDef.ReplaceAll("_NODISCARD ", "");

					pos = varDef.Find("_CONSTEXPR17 ");
					if (-1 != pos)
						varDef.ReplaceAll("_CONSTEXPR17 ", "");
					else
					{
						pos = varDef.Find("_CONSTEXPR20 ");
						if (-1 != pos)
							varDef.ReplaceAll("_CONSTEXPR20 ", "");
						else
						{
							pos = varDef.Find("_CONSTEXPR20_CONTAINER ");
							if (-1 != pos)
								varDef.ReplaceAll("_CONSTEXPR20_CONTAINER ", "");
						}
					}

					pos = varDef.Find(' ');
					if (-1 != pos)
						varDef = varDef.Left(pos);

					// [case: 52335]
					varDef.ReplaceAll("*", "");
					varDef.ReplaceAll("&", "");
					varDef.ReplaceAll("^", "");

					if (bcl.Find(varDef) != -1 || ptrHack)
						LookupParentDataIfAsMember(bcl);
				}
			}
		}
		else
			mParentDataIfAsMember = NULL;
	}
	else if (mParentDataIfAsMember && mParentDataIfAsMember->MaskedType() == C_ENUM)
	{
		mCreateType = ctEnumMember;
		if (ValidateParentData())
		{
			mInvokedOnXref = true;
			return TRUE;
		}
		mCreateType = ctUndetermined;
	}

	if (ValidateParentData())
	{
		if (mWordAfterNext.GetLength() && mWordAfterNext[0] == '(')
			mCreateType = ctMethod;
		else
			mCreateType = ctMember;

		if (curWd == "::" ||                                                  // C++
		    (!xrefViaVar && curWd == "." && mParentDataIfAsMember->IsType())) // C#
		{
			mStaticMember = true;
			mConst = false;
		}

		mInvokedOnXref = true;
		return TRUE;
	}

	return FALSE;
}

BOOL CreateFromUsage::HaveNonXrefToCreate(EdCntPtr ed)
{
	// free function (or object method if call is within member method)
	//	 FooThis();
	//	 ::FooThat();
	if (mWordAfterNext.GetLength())
	{
		if (mWordAfterNext[0] == '.' || mWordAfterNext[0] == ':' || mWordAfterNext[0] == '-')
			return FALSE; // ctClass?
	}

	if (mWordAfterNext.GetLength() && mWordAfterNext[0] == '(')
	{
		mCreateType = ctFunction;
		if (!mBaseScope.IsEmpty() && -1 != mBaseScope.Find(mMp->m_MethodName))
		{
			// [case: 31277] check for cfu in member initialization list
			WTString ctor(mBaseScope + DB_SEP_STR + mMp->m_MethodName);
			if (mInvokingScope.contains(ctor))
			{
				ctor = mMp->m_MethodName;
				const int curLine = ed->CurLine();
				WTString prevLines;
				int curPosInSubstr = 0;
				for (int cnt = 30; cnt >= 0; --cnt)
				{
					if (curLine > cnt)
					{
						WTString lineText(ed->GetLine(curLine - cnt));
						if (!cnt)
						{
							int pos = lineText.Find(mNewSymbolName);
							if (-1 != pos)
								curPosInSubstr = prevLines.GetLength() + pos;
							if (-1 != lineText.Find(';'))
								ctor.Empty(); // short-circuit the rest, it's not the initializer list
						}
						prevLines += lineText;
					}
				}

				if (!ctor.IsEmpty())
				{
					// need a ReverseFindWholeWord...
					int ctorPos = prevLines.ReverseFind(ctor);
					if (-1 != ctorPos && ctorPos && ISCSYM(prevLines[ctorPos - 1]))
					{
						prevLines = prevLines.Left(ctorPos);
						ctorPos = prevLines.ReverseFind(ctor);
					}

					if (-1 != ctorPos && ctorPos >= curPosInSubstr)
					{
						if (-1 != mNewSymbolName.Find(ctor))
						{
							// watch out for m_Foo in class Foo
							prevLines = prevLines.Left(ctorPos);
							ctorPos = prevLines.ReverseFind(ctor);
						}
					}

					if (-1 != ctorPos && ctorPos < curPosInSubstr)
					{
						const int colonPos = prevLines.ReverseFind(':');
						if (-1 != colonPos && colonPos > ctorPos && colonPos < curPosInSubstr)
						{
							const int bracePos = prevLines.ReverseFind('{');
							if (-1 == bracePos || bracePos < ctorPos)
							{
								// in member initialization list
								mCreateType = ctVariable;
								mMemberInitializationList = true;
							}
						}
					}
				}
			}
		}
	}
	else
		mCreateType = ctVariable;

	if (mBaseScope.IsEmpty())
	{
		if (ctVariable == mCreateType)
		{
			// if preceded by ::, change to ctGlobal
			WTString curWd = ed->CurWord();
			if (curWd == mNewSymbolName)
				curWd = ed->CurWord(-1);
			if (curWd == "::")
				mCreateType = ctGlobal;
		}
		else if (ctFunction == mCreateType)
		{
			const int pos = mInvokingScope.Find('-');
			if (-1 != pos)
			{
				const WTString fnScope(mInvokingScope.Left(pos));
				DType* fnData = mMp->FindExact2(fnScope);
				if (fnData && fnData->MaskedType() == FUNC && ::ValidateData(fnData))
				{
					WTString tmp(fnData->Def());
					tmp.TrimRight();
					if (FindWholeWordInCode(tmp, "constexpr", Src, 0) != -1)
						mConstExpr = true;
					if (FindWholeWordInCode(tmp, "consteval", Src, 0) != -1)
						mConstEval = true;
				}
			}
		}
	}
	else
	{
		LookupParentDataIfAsMember(mBaseScope);
		if (ValidateParentData())
		{
			if (ctFunction == mCreateType)
				mCreateType = ctMethod;
			else
				mCreateType = ctMember;

			const int pos = mInvokingScope.Find('-');
			if (-1 != pos)
			{
				const WTString fnScope(mInvokingScope.Left(pos));
				DType* fnData = mMp->FindExact2(fnScope);
				if (fnData && fnData->MaskedType() == FUNC && ::ValidateData(fnData))
				{
					WTString tmp(fnData->Def());
					tmp.TrimRight();
					// if calling scope is static, make it class static
					if (::strstrWholeWord(tmp, "static"))
						mStaticMember = true;
					// if calling scope is const, make it const (if ctMethod)
					if (ctMethod == mCreateType && (tmp.ReverseFind("const") == (tmp.GetLength() - 5) || (tmp.ReverseFind("const{...}") == (tmp.GetLength() - 10))))
						mConstKeywordThis = mConst = true;
					if (ctMethod == mCreateType && (FindWholeWordInCode(tmp, "constexpr", Src, 0) != -1))
						mConstExpr = true;
					if (ctMethod == mCreateType && (FindWholeWordInCode(tmp, "consteval", Src, 0) != -1))
						mConstEval = true;
				}
			}

			mDataForKeywordThis = mParentDataIfAsMember;
			WTString curWd = ed->CurWord();
			if (curWd == mNewSymbolName)
				curWd = ed->CurWord(-1);
			if (curWd == "::")
			{
				mStaticMember = false;
				mConst = false;
				mParentDataIfAsMember = NULL;
				if (ctMethod == mCreateType)
					mCreateType = ctFunction;
				else if (ctMember == mCreateType)
					mCreateType = ctGlobal;
			}
		}
	}

	return TRUE;
}

WTString CreateFromUsage::BuildDefaultMethodSignature()
{
	WTString sig;
	if (UC == g_currentEdCnt->m_ftype) // All methods in UC need "function" regardless of return type
		sig += "function ";

	WTString lineText(g_currentEdCnt->GetLine(g_currentEdCnt->CurLine()));
	WTString tmp(InferDeclType("void", lineText));
	if (UC == g_currentEdCnt->m_ftype && tmp == "void")
		tmp.Empty(); // bug 1395
	sig += tmp;
	sig += " " + mNewSymbolName + "(";
	sig += InferTypeInParens(lineText);
	sig += ")";
	return sig;
}

void CreateFromUsage::GetArgs(WTString text, std::vector<WTString>& args)
{
	std::vector<WTString> literalParams;

	CodeToken tok(gTypingDevLang, text, true);
	while (tok.more())
	{
		WTString cur(tok.read(",;"));
		cur.Trim();
		if (cur.GetLength())
		{
			const int posOpen = cur.Find('(');
			const int posClose = cur.Find(')');
			if (cur[cur.GetLength() - 1] == ')')
			{
				cur = cur.Left(cur.GetLength() - 1);
				cur.TrimRight();
			}

			if (cur.GetLength())
				literalParams.push_back(cur);

			if (-1 == posOpen && -1 != posClose)
			{
				// ran beyond end of args - just break here.  ex:
				// NewMethod(arg1, arg); "foo";
				break;
			}
		}
	}

	int cnt = 1;
	for (std::vector<WTString>::const_iterator it = literalParams.begin(); it != literalParams.end(); ++it, ++cnt)
	{
		const WTString theName(*it);
		WTString theDecl = GetArgDecl(theName);
		if (theDecl.GetLength() > 6 && theDecl.ReverseFind(" param") == (theDecl.GetLength() - 6))
		{
			WTString tmp;
			tmp.WTFormat("%d", cnt);
			theDecl += tmp;
		}

		args.push_back(theDecl);
	}
}

WTString GetCastType(WTString expression)
{
	if (expression.Find("_cast") != -1)
	{
		// [case: 31478] cast, use it
		WTString searchFor("static_cast<");
		int pos = expression.Find(searchFor);
		if (-1 == pos)
		{
			searchFor = "const_cast<";
			pos = expression.Find(searchFor);
		}
		if (-1 == pos)
		{
			searchFor = "dynamic_cast<";
			pos = expression.Find(searchFor);
		}
		if (-1 == pos)
		{
			searchFor = "reinterpret_cast<";
			pos = expression.Find(searchFor);
		}

		if (-1 != pos)
		{
			WTString tmp(expression.Mid(pos + searchFor.GetLength()));
			pos = tmp.Find('>');
			if (-1 != pos)
			{
				expression = tmp.Left(pos);
				return expression;
			}
		}
	}

	return "";
}

WTString CreateFromUsage::GetArgDecl(WTString theName)
{
	WTString theType = IsCFile(gTypingDevLang) ? "UnknownType" : "Object";
	if (theName.IsEmpty())
		return theType + " " + theName;

	bool isDerefdPtr = false;
	if (theName[0] == '*')
	{
		// [case: 31471] watch out for pointer derefs
		isDerefdPtr = true;
		theName = theName.Mid(1);
		theName.TrimLeft();
	}

	if (theName.Find("this") == 0)
	{
		if (theName == "this")
		{
			// TODO: is baseclass ok to use here? check namespaces
			if (!mDataForKeywordThis && !mBaseScope.IsEmpty())
			{
				// don't use mParentDataIfAsMember (it applies to the xref, not invoking scope)
				// this block is similar to a nested block in HaveNonXrefToCreate
				mDataForKeywordThis = mMp->FindExact2(mBaseScope);
				if (mDataForKeywordThis)
				{
					const int pos = mInvokingScope.Find('-');
					if (-1 != pos)
					{
						const WTString fnScope(mInvokingScope.Left(pos));
						DType* fnData = mMp->FindExact2(fnScope);
						if (fnData && fnData->MaskedType() == FUNC && ::ValidateData(fnData))
						{
							WTString tmp(fnData->Def());
							tmp.TrimRight();
							if (tmp.ReverseFind("const") == (tmp.GetLength() - 5))
								mConstKeywordThis = true;
						}
					}
				}
			}

			if (mDataForKeywordThis)
			{
				WTString tmp(mDataForKeywordThis->SymScope());
				if (tmp.GetLength() && tmp[0] == DB_SEP_CHR)
					tmp = tmp.Mid(1);
				tmp = ::DecodeTemplates(tmp);
				if (tmp.GetLength())
				{
					if (mConstKeywordThis)
						tmp = "const " + tmp;
					if (IsCFile(gTypingDevLang))
						tmp += "*";
					tmp += " param";
					return tmp;
				}
			}
		}
		else if (theName.Find("this.") == 0 || theName.Find("this->") == 0)
		{
			// strip 'this' so that we look up the member theName is pointing to
			theName = theName.Mid(5);
			if (theName[0] == '>')
				theName = theName.Mid(1);
		}
	}

	if (theName.Find("==") != -1)
		return "bool param";

	LPCSTR tmpType = ::SimpleTypeFromText(gTypingDevLang, theName);
	if (tmpType)
		return WTString(tmpType) + " param";

	if (theName[0] == '(')
	{
		if (theName[theName.GetLength() - 1] == ')')
			return "bool param"; // this doesn't happen, name comes in without trailing )

		// [case: 31478] cast, use it
		int pos = theName.Find(')');
		if (-1 != pos)
		{
			theName = theName.Left(pos);
			pos = theName.ReverseFind('*');
			if (isDerefdPtr && -1 != pos)
			{
				// [case: 31471] watch out for pointer derefs - remove pointer from type
				theName.ReplaceAt(pos, 1, " ");
				theName.TrimRight();
				pos = theName.ReverseFind('*');
			}

			// try to watch out for non-cast expressions...
			WTString tmp(theName);
			if (-1 != pos)
			{
				// see if * has arg as in (x*y) rather than as (char *)
				if ((pos + 1) < tmp.GetLength() && tmp[pos + 1] == ' ')
					pos++;
				if ((pos + 1) < tmp.GetLength() && tmp[pos + 1] != ')')
					return "int param";
				tmp.ReplaceAll("*", "", TRUE);
			}

			tmp.ReplaceAll("const", " ", TRUE);
			tmp.ReplaceAll("constexpr", " ", TRUE);
			tmp.ReplaceAll("consteval", " ", TRUE);
			tmp.ReplaceAll("constinit", " ", TRUE);
			tmp.ReplaceAll("_CONSTEXPR17", " ", TRUE);
			tmp.ReplaceAll("_CONSTEXPR20_CONTAINER", " ", TRUE);
			tmp.ReplaceAll("_CONSTEXPR20", " ", TRUE);
			tmp.ReplaceAll("literal", " ", TRUE);
			tmp.ReplaceAll("readonly", " ", TRUE);
			tmp.ReplaceAll("initonly", " ", TRUE);
			tmp.ReplaceAll("( ", ""); // due to check for " " further below
			tmp.Trim();
			if (tmp.FindOneOf("+-/") != -1)
			{
				if (tmp.Find('.') != -1 || tmp.Find('/') != -1)
					return "double param";
				return "int param";
			}
			if (tmp.Find("==") != -1)
				return "bool param";
			if (tmp.FindOneOf(" ") != -1)
				return theType + " param"; // give up, use default

			tmp = theName.Mid(1);
			tmpType = ::SimpleTypeFromText(gTypingDevLang, tmp);
			if (tmpType) // (2) * foo
				return WTString(tmpType) + " param";

			theType = tmp + " param";
			return theType;
		}

		return "bool param";
	}

	WTString castType = GetCastType(theName);
	if (castType.GetLength())
		return castType + " param";

	bool isPointer = false;
	if (theName[0] == '&')
	{
		isPointer = true;
		theName = theName.Mid(1);
	}

	bool isCall = false;
	int pos = theName.Find('(');
	if (-1 != pos && pos)
	{
		theName = theName.Left(pos);
		theName.TrimRight();
		isCall = true;
	}
	if (-1 == pos)
	{
		pos = theName.Find(')');
		if (-1 != pos)
		{
			// [case: 32269]
			// had no open parens, but did have a close;
			// chop and retry
			theName = theName.Left(pos);
			return GetArgDecl(theName);
		}
	}

	pos = theName.Find('[');
	if (-1 != pos && pos)
	{
		// [case: 52460]
		theName = theName.Left(pos);
		theName.TrimRight();
		if (isPointer)
			isPointer = false;
	}

	DTypePtr cdBak;
	DType* cd = mMp->FindSym(&theName, &mInvokingScope, nullptr, FDF_SlowUsingNamespaceLogic);
	if (!cd)
	{
		// [case: 33485] param of foo.bar or foo->bar
		const WTString fileContents(g_currentEdCnt->GetBuf());
		int pos2 = g_currentEdCnt->GetBufIndex(fileContents, (long)g_currentEdCnt->CurPos());
		pos2 = fileContents.Find(theName, pos2);
		if (-1 != pos2)
		{
			WTString outScp;
			cdBak = ::SymFromPos(fileContents, mMp, pos2 + theName.GetLength() - 1, outScp);
			cd = cdBak.get();
			if (cd)
			{
				const char* offset = ::strstrWholeWord(cd->Def(), theName);
				if (!offset)
					theName = cd->Sym();
			}
		}
	}

	if (cd && (cd->MaskedType() == VAR || cd->MaskedType() == FUNC || cd->IsType()))
	{
		const WTString def(cd->Def());
		const char* offset = strstrWholeWord(def.c_str(), theName.c_str());
		if (offset)
		{
			int pos3 = ptr_sub__int(offset, def.c_str());
			if (pos3)
			{
				theType = def.Left(pos3);
				theType = ::DecodeTemplates(theType);
				theType.TrimRight();
				if (!theType.IsEmpty())
				{
					if (cd->MaskedType() == FUNC)
					{
						// [case: 33485] watch out for concat'd defs
						WTString tmpDef(::GetTypeFromDef(def, gTypingDevLang));
						pos3 = tmpDef.Find('\f');
						if (-1 != pos3)
							tmpDef = tmpDef.Left(pos3);
						if (!tmpDef.IsEmpty())
						{
							_ASSERTE(-1 != theType.Find(tmpDef));
							return tmpDef + " param"; // don't pass in fn name as a param name
						}

						return theType + " param"; // don't pass in fn name as a param name
					}

					if (isPointer && !isDerefdPtr)
						theType += "*";

					if (isDerefdPtr)
					{
						// [case: 31471] watch out for pointer derefs - remove pointer from type
						pos3 = theType.ReverseFind('*');
						if (-1 != pos3)
						{
							theType.ReplaceAt(pos3, 1, " ");
							theType.TrimRight();
						}
					}

					// [case: 65497]
					::CleanupTypeString(theType, gTypingDevLang);
					return theType + " " + theName;
				}
			}
		}
	}

	if (!cd)
		cd = mMp->FindAnySym(::StrGetSym(theName)); // [case: 31175] scoped enum items

	if (cd && cd->MaskedType() == C_ENUMITEM)
	{
		// [case: 31175] enum item constants (as opposed to variables of enum item type)
		const WTString def(cd->Def());
		const char* offset = strstrWholeWord(def.c_str(), ::StrGetSym(theName));
		if (offset)
		{
			int pos4 = ptr_sub__int(offset, def.c_str());
			if (pos4)
			{
				theType = def.Left(pos4);
				theType = ::DecodeTemplates(theType);
				pos4 = theType.Find("enum ");
				if (-1 != pos4)
				{
					theType = theType.Mid(pos4 + 4);
					theType.TrimLeft();
				}
				theType.TrimRight();
				if (theType.IsEmpty())
				{
					// [case: 65519]
					return "int param";
				}

				return theType + " param"; // don't use enum item value as param name
			}
		}
	}

	if (isCall)
		return theType + " param"; // don't return fn name as a param name

	if (isPointer)
		theType += "*";

	_ASSERTE(!theType.IsEmpty());
	return theType + " " + theName;
}

WTString CreateFromUsage::BuildDefaultMemberDeclType()
{
	WTString lineText(g_currentEdCnt->GetLine(g_currentEdCnt->CurLine()));
	const WTString defType(IsCFile(gTypingDevLang) ? "UnknownType" : "Object");
	WTString declType(InferDeclType(defType, lineText));

	// [case: 54480]
	if (declType.Find("constexpr ") == 0 && declType.Find('*') == -1)
		declType = declType.Mid(10);
	if (declType.Find("_CONSTEXPR17 ") == 0 && declType.Find('*') == -1)
		declType = declType.Mid(13);
	if (declType.Find("_CONSTEXPR20_CONTAINER ") == 0 && declType.Find('*') == -1)
		declType = declType.Mid(23);
	if (declType.Find("_CONSTEXPR20 ") == 0 && declType.Find('*') == -1)
		declType = declType.Mid(13);
	if (declType.Find("const ") == 0 && declType.Find('*') == -1)
		declType = declType.Mid(6);

	return declType;
}

WTString CreateFromUsage::InferDeclType(WTString defaultType, WTString lineText)
{
	int pos = lineText.Find(mNewSymbolName);
	if (-1 == pos)
		return defaultType;

	WTString rightLnText(lineText.Mid(pos + mNewSymbolName.GetLength()));
	lineText = lineText.Left(pos);
	pos = rightLnText.Find('?');
	if (-1 != pos)
	{
		// [case: 57444] ternary conditional?
		int closeParenPos = rightLnText.Find(')');
		if (-1 != closeParenPos && pos > closeParenPos)
			return "bool";
	}

	if (lineText.Find("return ") != -1)
	{
		int pos2 = mInvokingScope.Find('-');
		if (-1 != pos2)
		{
			const WTString fnScope(mInvokingScope.Left(pos2));
			DType* fnData = mMp->FindExact2(fnScope);
			if (fnData && fnData->MaskedType() == FUNC)
			{
				WTString tmp(::GetDeclTypeFromDef(mMp.get(), fnData->Def(), fnScope, FUNC));
				if (!tmp.IsEmpty())
					return tmp;
			}
		}

		return defaultType;
	}

	WTString inferFrom, scopeToCheck(mInvokingScope);
	pos = lineText.ReverseFind('=');
	if (-1 == pos)
	{
		// determine return type from line text - search for var or function call after '='
		pos = rightLnText.Find('=');
		if (-1 == pos || ((ctFunction == mCreateType || ctMethod == mCreateType) &&
		                  ((pos < rightLnText.GetLength() && rightLnText[pos + 1] != '=') ||
		                   (pos > 0 && !strchr("!|^&~", rightLnText[pos - 1])))))
		{
			if (mCreateType != ctVariable && mCreateType != ctMember && mCreateType != ctMethod &&
			    mCreateType != ctFunction)
				return defaultType;

			rightLnText.TrimLeft();
			lineText.TrimRight();
			if (lineText.IsEmpty() && !mMemberInitializationList)
				return defaultType;

			_ASSERTE(!mMemberInitializationList || ctMember == mCreateType);
			if (ctMember == mCreateType && mMemberInitializationList)
			{
				// [case: 31277] cfu in member initialization list
				pos = rightLnText.Find("),");
				if (-1 != pos)
					rightLnText = rightLnText.Left(pos + 1);

				WTString sig(InferTypeInParens(mNewSymbolName + rightLnText));
				if (!sig.IsEmpty())
				{
					pos = sig.Find(',');
					if (-1 == pos)
					{
						pos = sig.ReverseFind(' ');
						if (-1 != pos)
						{
							sig = sig.Left(pos);
							sig.Trim();
							return sig;
						}
					}
				}

				if (lineText.IsEmpty() || !(rightLnText[0] == ')' && lineText[lineText.GetLength() - 1] == '('))
					return defaultType;
			}
			else if (mCreateType == ctVariable || mCreateType == ctMember)
			{
				if (rightLnText[0] != ')')
					return defaultType;

				if (lineText[lineText.GetLength() - 1] != '(')
				{
					if (-1 != lineText.Find('('))
						return "bool"; // [case: 55527] give benefit of the doubt

					return defaultType;
				}
			}
			else if (mCreateType == ctMethod || mCreateType == ctFunction)
			{
				if (rightLnText[0] != '(')
					return defaultType;

				if (lineText[lineText.GetLength() - 1] != '(')
				{
					const int cnt1 = lineText.GetTokCount('(');
					const int cnt2 = lineText.GetTokCount(')');
					if (cnt1 && cnt1 > cnt2)
						return "bool"; // [case: 55527] give benefit of the doubt

					return defaultType;
				}
			}

			// [case: 32085] use signature of called method to determine expected variable type
			// only implemented for simple, single param methods
			inferFrom = scopeToCheck; // called method is at end of scopeToCheck
			pos = inferFrom.ReverseFind('-');
			if (-1 == pos)
				return defaultType;

			inferFrom = inferFrom.Left(pos);
			pos = inferFrom.ReverseFind(':');
			if (-1 != pos)
				inferFrom = inferFrom.Mid(pos + 1);

			DType* cd = mMp->FindSym(&inferFrom, &scopeToCheck, nullptr, FDF_SlowUsingNamespaceLogic);
			if (!cd && inferFrom[0] != DB_SEP_CHR)
			{
				WTString scopedFrom(DB_SEP_STR + inferFrom);
				cd = mMp->FindSym(&scopedFrom, &scopeToCheck, nullptr, FDF_SlowUsingNamespaceLogic);
				if (!cd)
					cd = mMp->FindAnySym(scopedFrom);
			}

			if (cd && FUNC == cd->MaskedType())
			{
				WTString args = g_pGlobDic->GetArgLists(cd->SymScope());
				// check for overloads - just use first
				pos = args.Find(CompletionStr_SEPType);
				if (-1 != pos)
					args = args.Left(pos);
				pos = args.Find(CompletionStr_SEPLine);
				if (-1 != pos)
					args = args.Left(pos);
				pos = args.Find(',');
				if (-1 != pos)
					args = args.Left(pos); // punt on method param cnt mismatches
				if (args.GetLength())
				{
					WTString tmp(::GetTypeFromDef(args, gTypingDevLang));
					pos = tmp.Find('\f');
					if (-1 != pos)
						tmp = tmp.Left(pos);
					if (!tmp.IsEmpty())
						return tmp;
				}
			}
			else if (cd && VAR == cd->MaskedType())
			{
				// [case: 65588] use the var type
				// this could be enhanced by checking to see if there are constructors,
				// multiple args to them, overloads, etc.
				// This initial pass doesn't even look at potential object ctors.
				WTString tmp(::GetTypeFromDef(cd->Def(), gTypingDevLang));
				pos = tmp.Find('\f');
				if (-1 != pos)
					tmp = tmp.Left(pos);
				if (!tmp.IsEmpty())
					return tmp;
			}

			if (lineText[lineText.GetLength() - 1] == '(' || lineText[lineText.GetLength() - 1] == '!')
				return "bool";

			return defaultType;
		}
		else
		{
			rightLnText = rightLnText.Mid(pos + 1);
			InferType inferType;
			return inferType.Infer(rightLnText, scopeToCheck, mBaseScope, mMp->FileType(), false, defaultType);
		}
	}
	else
	{
		// determine return type from line text - search for var before '='
		lineText = lineText.Left(pos);
		lineText.Trim();
		pos = lineText.ReverseFind(' ');
		int pos2 = lineText.ReverseFind('\t');
		if (-1 == pos && -1 == pos2)
			inferFrom = lineText;
		else
		{
			if (pos != -1 && pos2 != -1)
			{
				pos = max(pos, pos2);
				pos2 = -1;
			}

			inferFrom = lineText.Mid(pos2 == -1 ? pos : pos2);
		}

		inferFrom.Trim();
	}

	if (inferFrom.IsEmpty())
		return defaultType;

	if (!ISCSYM(inferFrom[0]))
	{
		// [case: 96668]
		EdCntPtr ed(g_currentEdCnt);
		MultiParsePtr mp = ed ? ed->GetParseDb() : nullptr;
		if (mp)
		{
			WTString scp = mp->m_ParentScopeStr;
			if (StartsWith(scp, "if") || StartsWith(scp, "else if", FALSE))
			{
				if (IsCFile(gTypingDevLang))
					return "bool";
				return "boolean";
			}
		}

		return defaultType;
	}

	DType* cd = mMp->FindSym(&inferFrom, &scopeToCheck, nullptr, FDF_SlowUsingNamespaceLogic);
	if (!cd && inferFrom[0] != DB_SEP_CHR)
	{
		WTString scopedFrom(DB_SEP_STR + inferFrom);
		cd = mMp->FindSym(&scopedFrom, &scopeToCheck, nullptr, FDF_SlowUsingNamespaceLogic);
		if (!cd)
			cd = mMp->FindAnySym(scopedFrom);
	}
	if (cd)
	{
		const uint typ = cd->MaskedType();
		if (VAR == typ || FUNC == typ || C_ENUMITEM == typ || cd->IsType())
		{
			WTString tmp(::GetDeclTypeFromDef(mMp.get(), cd, (int)typ));
			if (!tmp.IsEmpty())
				return tmp;
		}
	}

	return defaultType;
}

WTString CreateFromUsage::GetCommandText()
{
	WTString txt;
	RefineCreationType();
	switch (mCreateType)
	{
	case ctMethod:
		if (!mInvokedOnXref && CS != gTypingDevLang)
			// since user might be allowed to change method to function, don't be specific here
			txt.WTFormat("Crea&te %s", mNewSymbolName.c_str());
		else
			txt.WTFormat("Crea&te method %s", mNewSymbolName.c_str());
		break;
	case ctFunction:
		txt.WTFormat("Crea&te function %s", mNewSymbolName.c_str());
		break;
	case ctGlobal:
	case ctVariable:
		// is it confusing to call it variable even if they can change it to member or parameter?
		txt.WTFormat("Crea&te variable %s", mNewSymbolName.c_str());
		break;
	case ctParameter:
		txt.WTFormat("Add parame&ter %s", mNewSymbolName.c_str());
		break;
	case ctEnumMember:
		txt.WTFormat("Crea&te enum item %s", mNewSymbolName.c_str());
		break;
	case ctClass:
		if (::IsCfile(mFileInvokedFrom))
			txt.WTFormat("Crea&te struct %s", mNewSymbolName.c_str());
		else
			txt.WTFormat("Crea&te class %s", mNewSymbolName.c_str());
		break;
	default:
		_ASSERTE(!"not implemented");
		// fall-through
	case ctMember:
		// since user can change member to variable, don't be specific here
		txt.WTFormat("Crea&te %s", mNewSymbolName.c_str());
		break;
	}
	return txt;
}

WTString CreateFromUsage::GenerateSourceToAddForFunction()
{
	WTString newMethodSig(BuildDefaultMethodSignature());
	WTString prefix, suffix;

	_ASSERTE(ctMember != mCreateType);
	if ((CS == gTypingDevLang || VB == gTypingDevLang) && (ctMethod == mCreateType))
	{
		prefix += ::GetVisibilityString(mMemberVisibility, gTypingDevLang) + CString(" ");
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	// [case: 135862]
	if (!mInvokedOnXref)
	{
		WTString scopeMethod = ::TokenGetField(mInvokingScope, "-");
		DTypeList dts;
		mMp->FindExactList(scopeMethod, dts, false);
		for (const auto& dt : dts)
		{
			if (dt.IsCppbClassMethod())
			{
				prefix += "__classmethod ";
				break;
			}
		}
	}
#endif

	if (mStaticMember && ctMethod == mCreateType)
		prefix += "static ";
	if (mConstExpr && (ctMethod == mCreateType || ctFunction == mCreateType))
		prefix += "constexpr ";
	if (mConstEval && (ctMethod == mCreateType || ctFunction == mCreateType))
		prefix += "consteval ";
	if (mConst && ctMethod == mCreateType && !mStaticMember)
		suffix = " const";

	BOOL isNet = FALSE;
	if (mParentDataIfAsMember)
	{
		WTString parentDataIfAsMemberDef(mParentDataIfAsMember->Def());
		isNet = ::strstrWholeWord(parentDataIfAsMemberDef, "ref") != NULL;
		if (!isNet)
			isNet = ::strstrWholeWord(parentDataIfAsMemberDef, "value") != NULL;
	}
	const WTString bodyAutotextItemTitle(::GetBodyAutotextItemTitle(isNet));
	WTString bodyCode = mConstEval || mConstExpr ? "" : gAutotextMgr->GetSource(bodyAutotextItemTitle);

	// [case: 9863] remove unsupported symbol related snippet reserved words in refactoring snippets
	bodyCode.ReplaceAll("$MethodName$", "");
	bodyCode.ReplaceAll("$MethodArgs$", "");
	bodyCode.ReplaceAll("$ClassName$", "");
	bodyCode.ReplaceAll("$BaseClassName$", "");
	bodyCode.ReplaceAll("$NamespaceName$", "");

	WTString labelText, defaultText;
	const int globalInsertPos = GetPosToInsertGlobal();

	// prompt for sig
	if (ctMethod == mCreateType && !mInvokedOnXref && CS != gTypingDevLang)
	{
		// Consider: allow edit of default bodyCode?
		GenericMultiPrompt dlg(VAUpdateWindowTitle_c(VAWindowType::CreateFromUsage, "Create From Usage", 0), "dlgCreateFromUsage");
		WTString optionText;
		GetUiText(ctMethod, optionText, labelText);
		defaultText = prefix + newMethodSig.c_str() + suffix;
		dlg.AddOption(ctMethod, optionText, labelText, defaultText);
		if (-1 != globalInsertPos)
		{
			GetUiText(ctFunction, optionText, labelText);
			defaultText = newMethodSig;
			dlg.AddOption(ctFunction, optionText, labelText, defaultText);
		}
		dlg.SetSelectionLabel("Create:");
		dlg.OverrideDefaultSelection();
		if (dlg.DoModal() != IDOK)
			return NULLSTR;

		mCreateType = static_cast<CreateType>(dlg.GetOptionSelected());
		_ASSERTE(ctMethod == mCreateType || ctFunction == mCreateType);
		if (ctMethod != mCreateType)
		{
			mParentDataIfAsMember = NULL;
			mDeclFile = mFileInvokedFrom;
			mSrcFile.Empty();
		}
		newMethodSig = dlg.GetUserText();
	}
	else
	{
		// Consider: allow edit of default bodyCode?
		if (ctFunction == mCreateType)
		{
			if (globalInsertPos == -1)
			{
				::ErrorBox(
				    "Visual Assist failed to locate a position to insert the declaration.\nNo changes were made.",
				    MB_OK | MB_ICONWARNING);
				return NULLSTR;
			}

			_ASSERTE(CS != gTypingDevLang);
			labelText = "Function declaration:";
			defaultText = prefix + newMethodSig;
			mDeclFile = mFileInvokedFrom;
			mSrcFile.Empty();
		}
		else if (ctMethod == mCreateType)
		{
			const WTString theType(::StrGetSym(mParentDataIfAsMember->SymScope()));
			if (theType.GetLength())
				labelText.WTFormat("Method declaration in %s:", theType.c_str());
			else
				labelText = "Method declaration:";
			defaultText = prefix + newMethodSig.c_str() + suffix;
		}
		else
			_ASSERTE(!"unhandled mCreateType");

		AddClassMemberDlg dlg(AddClassMemberDlg::DlgCreateMethod, defaultText.c_str(), labelText.c_str());
		dlg.OverrideDefaultSelection();
		dlg.UseEdCaretPos();
		if (dlg.DoModal() != IDOK)
			return NULLSTR;

		newMethodSig = dlg.GetUserText();
	}

	newMethodSig.TrimRight();
	if (newMethodSig[newMethodSig.GetLength() - 1] == ';')
	{
		newMethodSig = newMethodSig.Left(newMethodSig.GetLength() - 1);
		newMethodSig.TrimRight();
	}

	WTString tmp;

	// parse out param list
	const int openPos = newMethodSig.Find('(');
	if (-1 == openPos)
		return NULLSTR;
	const int closePos = newMethodSig.Find(")", openPos);
	if (-1 == closePos)
		return NULLSTR;
	tmp = newMethodSig.Mid(openPos + 1, closePos - openPos - 1);
	tmp.Trim();
	const WTString paramList(tmp);

	// parse out qualifiers
	tmp = newMethodSig.Mid(closePos + 1);
	tmp.TrimLeft();
	const WTString qualifiers(tmp);

	// parse out newMethodName
	tmp = newMethodSig.Left(openPos);
	tmp.TrimRight();
	const int namePos = tmp.ReverseFind(mNewSymbolName);
	if (-1 == namePos)
		return NULLSTR;
	mNewSymbolName = tmp.Mid(namePos);
	if (mNewSymbolName.IsEmpty())
		return NULLSTR;

	// parse out return type
	tmp = tmp.Left(namePos);
	tmp.TrimRight();
	const WTString returnType(tmp);

	WTString autotextItemTitle;
	const int fType = ::GetFileType(mFileInvokedFrom);
	const bool kIsC = Src == fType || Header == fType;

	if (kIsC && Src == ::GetFileType(mDeclFile))
		autotextItemTitle = "Refactor Create Implementation";
	else
		autotextItemTitle = "Refactor Extract Method";

	WTString implCode(gAutotextMgr->GetSource(autotextItemTitle));
	implCode.ReplaceAll("$SymbolType$", returnType);
	implCode.ReplaceAll("$SymbolContext$", mNewSymbolName);
	implCode.ReplaceAll("$SymbolName$", mNewSymbolName);
	implCode.ReplaceAll("$ParameterList$", paramList);
	if (qualifiers.IsEmpty())
		implCode.ReplaceAll(" $MethodQualifier$", qualifiers);
	else
		implCode.ReplaceAll("$MethodQualifier$", qualifiers);
	implCode.ReplaceAll("$MethodBody$", bodyCode);
	implCode.ReplaceAll("(  )", "()");
	implCode.ReplaceAll("$SymbolPrivileges$", "");

	// [case: 9863] remove unsupported symbol related snippet reserved words in refactoring snippets
	implCode.ReplaceAll("$MethodName$", "");
	implCode.ReplaceAll("$MethodArgs$", "");
	implCode.ReplaceAll("$ClassName$", "");
	implCode.ReplaceAll("$BaseClassName$", "");
	implCode.ReplaceAll("$NamespaceName$", "");

	return implCode;
}

WTString CreateFromUsage::GenerateSourceToAddForVariable()
{
	WTString theDecl(BuildDefaultMemberDeclType());
	theDecl += " " + mNewSymbolName;
	WTString prefix;

	if ((CS == gTypingDevLang || VB == gTypingDevLang) && (ctMethod == mCreateType || ctMember == mCreateType))
	{
		prefix = ::GetVisibilityString(mMemberVisibility, gTypingDevLang) + CString(" ");
	}

	// prompt for type and initialization if applicable
	GenericMultiPrompt dlg(VAUpdateWindowTitle_c(VAWindowType::CreateFromUsage,"Create From Usage", 1), "dlgCreateFromUsage");

	WTString optionText, labelText, defaultText;
	// if ctVariable, also allow globalVar and parameter
	// if (ctMember && !mInvokedOnXref), also allow globalVar, localVar and parameter
	// if (ctMember && mInvokedOnXref), only allow member
	// if ctGlobal, only allow global
	BOOL allowLocalVar = FALSE;
	BOOL allowGlobalVar = FALSE;
	switch (mCreateType)
	{
	case ctGlobal:
		allowGlobalVar = TRUE;
		break;
	case ctVariable:
		if (-1 != GetPosToInsertLocalVariable())
		{
			GetUiText(ctVariable, optionText, labelText);
			defaultText = theDecl;
			dlg.AddOption(ctVariable, optionText, labelText, defaultText);
		}
		allowGlobalVar = allowLocalVar = TRUE;
		break;
	case ctMember:
		GetUiText(ctMember, optionText, labelText);
		defaultText = prefix;
		if (mStaticMember)
			defaultText += "static ";
		if (mConstEval)
			defaultText += "constexpr ";
		if (mConstEval)
			defaultText += "consteval ";
		defaultText += theDecl.c_str();

		if (-1 != defaultText.Find('&'))
		{
			// [case: 58376] don't make member references by default
			defaultText.Replace(" & ", " ");
			defaultText.Replace("& ", " ");
			defaultText.Replace(" &", " ");
			defaultText.Replace("&", "");
		}

		dlg.AddOption(ctMember, optionText, labelText, defaultText);
		if (!mInvokedOnXref && !mMemberInitializationList)
		{
			GetUiText(ctVariable, optionText, labelText);
			defaultText = theDecl;
			dlg.AddOption(ctVariable, optionText, labelText, defaultText);
			allowGlobalVar = allowLocalVar = TRUE;
		}
		break;
	default:
		_ASSERTE(!"this shouldn't happen - unhandled mCreateType");
	}

	if (allowGlobalVar && CS != gTypingDevLang)
	{
		if (-1 != GetPosToInsertGlobal())
		{
			GetUiText(ctGlobal, optionText, labelText);
			defaultText = theDecl;

			if (-1 != defaultText.Find('&'))
			{
				// [case: 58376] don't make global references by default
				defaultText.Replace(" & ", " ");
				defaultText.Replace("& ", " ");
				defaultText.Replace(" &", " ");
				defaultText.Replace("&", "");
			}

			dlg.AddOption(ctGlobal, optionText, labelText, defaultText);
		}
	}

	if (allowLocalVar)
	{
		DType* sym = g_currentEdCnt->GetParseDb()->FindExact(::TokenGetField(mInvokingScope, "-"));
		if (sym && sym->MaskedType() == FUNC && !sym->IsSystemSymbol())
		{
			if (mChangeSig.CanChange(sym))
			{
				GetUiText(ctParameter, optionText, labelText);
				if (!optionText.IsEmpty())
				{
					defaultText = theDecl;
					dlg.AddOption(ctParameter, optionText, labelText, defaultText);
				}
			}
		}
	}

	if (!dlg.GetOptionCount())
	{
		::ErrorBox("Visual Assist failed to locate a position to insert the declaration.\nNo changes were made.",
		           MB_OK | MB_ICONWARNING);
		return NULLSTR;
	}

	dlg.SetSelectionLabel("Create:");
	dlg.OverrideDefaultSelection();
	if (dlg.DoModal() != IDOK)
		return NULLSTR;

	mCreateType = static_cast<CreateType>(dlg.GetOptionSelected());
	if (ctMember != mCreateType)
		mParentDataIfAsMember = NULL;
	theDecl = dlg.GetUserText();
	theDecl.Trim();
	if (theDecl.IsEmpty())
		return NULLSTR;

	WTString newMethodSig(theDecl);
	// ensure symbol name was not changed
	if (!strstrWholeWord(newMethodSig, mNewSymbolName))
	{
		// we could parse the decl and update mNewSymbolName here
		return NULLSTR;
	}

	mSrcFile.Empty();
	if (ctVariable == mCreateType || ctGlobal == mCreateType)
		mDeclFile = mFileInvokedFrom;

	if ((ctMember == mCreateType || ctGlobal == mCreateType || ctVariable == mCreateType || ctClass == mCreateType ||
	     ctConstant == mCreateType) &&
	    -1 == newMethodSig.Find(';'))
	{
		newMethodSig = WTString("$end$") + newMethodSig + ';';
	}

	return newMethodSig;
}

void CreateFromUsage::GetUiText(CreateType ctType, WTString& optionText, WTString& labelText)
{
	switch (ctType)
	{
	case ctMethod: {
		WTString theType(::StrGetSym(mParentDataIfAsMember->SymScope()));
		theType = ::DecodeTemplates(theType);
		if (theType.GetLength())
			optionText.WTFormat("&Method in %s", theType.c_str());
		else
			optionText = "&Method";
	}
		labelText = "&Declare method:";
		break;
	case ctFunction:
		optionText = "&Function";
		labelText = "&Declare function:";
		break;
	case ctMember: {
		WTString theType(::StrGetSym(mParentDataIfAsMember->SymScope()));
		theType = ::DecodeTemplates(theType);
		if (theType.GetLength())
			optionText.WTFormat("&Member in %s", theType.c_str());
		else
			optionText = "&Member";
	}
		labelText = "&Declare member:";
		break;
	case ctVariable:
		optionText = "&Local variable";
		labelText = "&Declare variable:";
		break;
	case ctGlobal:
		optionText = "&Global variable";
		labelText = "&Declare variable:";
		break;
	case ctParameter: {
		DType* sym = g_currentEdCnt->GetParseDb()->FindExact(::TokenGetField(mInvokingScope, "-"));
		_ASSERTE(sym && sym->MaskedType() == FUNC && !sym->IsSystemSymbol());
		WTString funcName = sym->Sym();
		_ASSERTE(funcName.GetLength());
		optionText.WTFormat("&Parameter to %s", funcName.c_str());
	}
		labelText = "&Declare parameter:";
		break;
	default:
		_ASSERTE(!"unhandled createtype");
		optionText.Empty();
		labelText.Empty();
	}
}

BOOL CreateFromUsage::InsertEnumItem(const WTString newScope, WTString codeToAdd, FreezeDisplay& _f)
{
	if (!::GotoDeclPos(newScope, mDeclFile, C_ENUM))
		return FALSE;

	_f.ReadOnlyCheck();
	EdCntPtr curEd(g_currentEdCnt);
	const WTString buf(curEd->GetBuf(TRUE));
	long curPos = curEd->GetBufIndex(buf, (long)curEd->CurPos());

	// [case: 30846][case:52617]
	// GotoDeclPos leaves us at the beginning of the line that contains the
	// closing }, rather than at enum word.

	// need to distinguish:
	// |enum { A };
	//
	// and
	//
	// enum
	// {
	// A
	// |};

	// find "enum" before next "}"
	int nextBracePos = buf.Find("}", curPos);
	int enumPos = -1;
	for (;;)
	{
		int next = buf.Find("enum", enumPos + 1);
		if (-1 == next)
			break;
		if (next > nextBracePos)
			break;
		enumPos = next;
	}

	if (enumPos >= 0)
		curPos = enumPos;

	const int closeBrcPos = buf.Find(" }", curPos);
	const int closeBrcPos2 = buf.Find("}", curPos);
	int insertPos;
	if (-1 == closeBrcPos)
		insertPos = closeBrcPos2;
	else if (-1 == closeBrcPos2)
		insertPos = closeBrcPos;
	else
		insertPos = min(closeBrcPos, closeBrcPos2);

	if (-1 == insertPos)
		return FALSE;

	const int openBrcPos = buf.Find("{", curPos);
	const int openLine = curEd->LineFromChar(openBrcPos);
	const int closeLine = curEd->LineFromChar(insertPos);

	if (closeLine > (openLine + 1))
	{
		// support for multiline decls
		const WTString prevLineText(curEd->GetLine(closeLine - 1));
		const WTString closeLineText(curEd->GetLine(closeLine));
		WTString startWhitespace;
		int idx;
		for (idx = 0; idx < prevLineText.GetLength(); ++idx)
		{
			if (prevLineText[idx] == ' ' || prevLineText[idx] == '\t')
				startWhitespace += prevLineText[idx];
			else
				break;
		}

		if (idx == prevLineText.GetLength())
			startWhitespace.Empty();

		bool terminationLineIsBare = true;
		for (idx = 0; terminationLineIsBare && idx < closeLineText.GetLength() && closeLineText[idx] != '}'; ++idx)
		{
			if (closeLineText[idx] != ' ' && closeLineText[idx] != '\t')
				terminationLineIsBare = false;
		}

		const bool prevLineHasCommentOrTerminates = prevLineText.FindOneOf("}'/") != -1;
		//		const bool prevLineHasInit = prevLineText.FindOneOf("=") != -1;

		if (terminationLineIsBare && !prevLineHasCommentOrTerminates)
		{
			// prevLineText has no comment or close Brace (just the enum item and maybe value init)
			// start append at end of prev line
			codeToAdd.ReplaceAll(", ", "");
			codeToAdd = "," + curEd->GetLineBreakString() + startWhitespace + codeToAdd;
			insertPos = curEd->GetBufIndex((long)curEd->LinePos(closeLine - 1));
			insertPos += prevLineText.GetLength();
		}
		else if (terminationLineIsBare)
		{
			// change insertPos if nothing but whitespace before close brace
			// just insert a whole new line at start of line that brace is on
			const int insertLine = curEd->LineFromChar(insertPos);
			insertPos = (int)curEd->LinePos(insertLine);
			// enum FOO {\n\n foo = 12 \n};
			codeToAdd = startWhitespace + codeToAdd + curEd->GetLineBreakString();

			// enum FOO {\n foo,\nbar // comment\n};
			// enum FOO {\n foo,\nbar, // comment\n};
			// hmmm - not sure what to do...
			// [case: 31483] we might insert an extra comma if the last enum item
			// has a trailing comma; it won't be caught below since we don't
			// insert before the comment
		}
		else
		{
			// enum FOO {\n\n foo };
			// enum FOO {\n\n foo\n , bar };
		}
	}
	else if (openLine != closeLine)
	{
		// enum FOO {\n foo };
		codeToAdd += curEd->GetLineBreakString();
	}
	else
		; // enum FOO { foo, bar };

	const char ch = curEd->CharAt(uint(insertPos - 1));
	if (ch == ',' && codeToAdd[0] == ',')
	{
		// [case: 31470] watch out for trailing comma on previous last enum item
		// this condition does not work for the "else if (terminationLineIsBare)" block
		codeToAdd = codeToAdd.Mid(1);
	}

	curEd->SetPos((uint)insertPos);
	return gAutotextMgr->InsertAsTemplate(curEd, codeToAdd, TRUE);
}

BOOL CreateFromUsage::InsertLocalVariable(WTString codeToAdd, FreezeDisplay& _f)
{
	int pos = GetPosToInsertLocalVariable();
	if (-1 == pos)
	{
		_ASSERTE(!"this shouldn't happen here - no place to insert local var");
		return FALSE;
	}

	_f.ReadOnlyCheck();
	EdCntPtr curEd(g_currentEdCnt);
	curEd->SetPos((uint)pos);
	const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
	codeToAdd = implCodelnBrk + codeToAdd;
	_f.OffsetLine(1);
	return gAutotextMgr->InsertAsTemplate(curEd, codeToAdd, TRUE);
}

BOOL CreateFromUsage::InsertXref(const WTString newScope, WTString codeToAdd, FreezeDisplay& _f)
{
	const ULONG kPrevLine = TERROW(g_currentEdCnt->CurPos());
	if (::GotoDeclPos(newScope, mDeclFile,
	                  ctMember == mCreateType ? VAR : FUNC)) // this is based on the add member refactoring
	{
		EdCntPtr curEd(g_currentEdCnt);
		const uint kCurPos = curEd->CurPos();
		_f.ReadOnlyCheck();
		ULONG ln = TERROW(kCurPos);

		// case 29902 respect private/public visibility in C++
		int labelLines = 0;
		if (IsCFile(curEd->m_ftype))
		{
			WTString fileText(curEd->GetBuf(TRUE));
			MultiParsePtr mp = curEd->GetParseDb();
			LineMarkers markers; // outline data
			GetFileOutline(fileText, markers, mp);

			FindLnToInsert insert(mMemberVisibility, ln, newScope, fileText);
			std::pair<int, bool> insertLocation = insert.GetLocationByVisibility(markers.Root());
			if (insertLocation.first >= 0)
			{
				curEd->SetPos((uint)curEd->GetBufIndex((long)curEd->LinePos(insertLocation.first)));
				ln = (ULONG)insertLocation.first;
			}
			if (insertLocation.second)
			{
				labelLines = 1;
				const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
				codeToAdd = GetLabelStringWithColon(mMemberVisibility) + implCodelnBrk + codeToAdd;
			}
		}

		if (ctMethod == mCreateType)
		{
			// do this after DelayFileOpen - dependent upon target file EOF
			if (TERCOL(kCurPos) > 1)
			{
				const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
				codeToAdd = implCodelnBrk + codeToAdd; // eof needs extra CRLF
			}
		}

		if (TERCOL(kCurPos) > 1)
			return gAutotextMgr->InsertAsTemplate(curEd, WTString(" ") + codeToAdd, TRUE);
		else
		{
			const WTString lnBrk(curEd->GetLineBreakString());
			codeToAdd += lnBrk;
			if (mDeclFile == mFileInvokedFrom && kPrevLine > ln)
			{
				if (Header != curEd->m_ftype || ctMember == mCreateType) // case 94976
				{
					if (ctMember == mCreateType || ctMethod == mCreateType)
					{
						// [case: 65516]
						int lnBrks = codeToAdd.GetTokCount('\n');
						if (!lnBrks)
							lnBrks = codeToAdd.GetTokCount('\r');
						if (lnBrks)
							_f.OffsetLine(lnBrks);
					}
				}
				else
				{
					_f.OffsetLine(1 + labelLines); // case 94976
				}
			}
			return gAutotextMgr->InsertAsTemplate(curEd, codeToAdd, TRUE);
		}
	}
	return FALSE;
}

BOOL CreateFromUsage::InsertGlobal(WTString codeToAdd, FreezeDisplay& _f)
{
	const long initialFirstVisLine = g_currentEdCnt->GetFirstVisibleLine();
	// global var or free function in current file
	int pos = GetPosToInsertGlobal();
	if (-1 == pos)
	{
		_ASSERTE(!"this shouldn't happen here - no place to insert global var");
		return FALSE;
	}

	_f.ReadOnlyCheck();
	EdCntPtr curEd(g_currentEdCnt);
	curEd->SetPos((uint)pos);
	int lnBrks = 0;

	if (ctFunction == mCreateType)
	{
		// [case: 66262] [case: 30990]
		lnBrks = codeToAdd.GetTokCount('\n');
		if (!lnBrks)
			lnBrks = codeToAdd.GetTokCount('\r');
		if (lnBrks > 0)
			_f.OffsetLine(lnBrks - 1);
	}
	else if (ctGlobal == mCreateType)
	{
		// positioning is funny since it is a global variable and we rely on
		// the IDE to handle formatting.
		const WTString implCodelnBrk(curEd->GetLineBreakString());
		codeToAdd = codeToAdd + implCodelnBrk + implCodelnBrk;
		lnBrks = 1; // it's funny i said
		_f.OffsetLine(lnBrks);
	}
	else
		_ASSERTE(!"unhandled mCreateType in InsertGlobal");

	const BOOL res = gAutotextMgr->InsertAsTemplate(curEd, codeToAdd, TRUE);
	if (gShellAttr->IsDevenv() && ctFunction == mCreateType)
	{
		// [case: 66263] line position in window
		ulong topPos = curEd->LinePos(initialFirstVisLine + lnBrks - 1);
		if (-1 != topPos && gShellSvc)
		{
			curEd->SetSel(topPos, topPos);
			gShellSvc->ScrollLineToTop();
		}
	}

	return res;
}

BOOL CreateFromUsage::InsertParameter(WTString codeToAdd)
{
	_ASSERTE(mChangeSig.ChangePreviouslyAllowed());
	if (mChangeSig.ChangePreviouslyAllowed())
		return mChangeSig.AppendParameter(codeToAdd);
	return FALSE;
}

CStringW CreateFromUsage::GetLabelStringWithColon(SymbolVisibility visibility)
{
	switch (visibility)
	{
	case vPublic:
		return L"public:";
	case vPublished:
		return L"__published:";
	case vProtected:
		return L"protected:";
	case vPrivate:
		return L"private:";
	default:
		return "";
	}
}

// Line to insert the created usage
// return pair 1/2: the line number. < 0 means we don't give a line number - insert where GotoDeclPos() has landed the
// caret (end of class) return pair 2/2: do we want to create mMemberVisibility label in the struct/class where we
// insert the new method or variable?
//                  false means that the visibility is already correct - via existing label or the class' / struct's
//                  default visibility.
std::pair<int, bool> CreateFromUsage::FindLnToInsert::GetLocationByVisibility(LineMarkers::Node& node)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		if (Ln >= marker.mStartLine && Ln <= marker.mEndLine - 1)
		{
			if (marker.mType == CLASS || marker.mType == STRUCT)
			{
				SymbolType = marker.mType == CLASS ? eSymbolType::CLASS : eSymbolType::STRUCT;
				DeepestClass = &ch;

				WTString currentName = marker.mText;
				currentName =
				    TokenGetField(currentName, ":"); // for things like "VAScopeInfo : public DefFromLineParse"
				currentName.TrimRight();

				if (currentName.Wide() == ClassName.Wide())
				{
					// find visibility node
					LineMarkers::Node* visibilityNode = GetAppropriateVisibilityNode();
					if (visibilityNode)
					{
						FileLineMarker& marker2 = **visibilityNode;
						return std::pair<int, bool>((int)marker2.mEndLine, false);
					}

					// create visibility section at end of class/struct
					WTString label = GetLastLabel();
					int realEndLn = GetClassRealEndLine((ULONG)DeepestClass->Contents().mEndCp);
					return std::pair<int, bool>(realEndLn, !IsLabelAppropriate(label)); // jump to the end of the class
				}

				std::pair<int, bool> res = GetLocationByVisibility(ch);
				if (res.first != -2)
					return res;
			}
			else
			{
				std::pair<int, bool> res = GetLocationByVisibility(ch);
				if (res.first != -2)
					return res;
			}
		}
	}

	return std::pair<int, bool>(-2, false);
}

bool CreateFromUsage::FindLnToInsert::IsLabelAppropriate(WTString label)
{
	switch (MemberVisibility)
	{
	case vPublic:
		if (label == "public:" || (SymbolType == eSymbolType::STRUCT && label == ""))
			return true;
		break;
	case vPublished:
		if (label == "__published:")
			return true;
		break;
	case vProtected:
		if (label == "protected:")
			return true;
		break;
	case vInternal:
		break;
	case vProtectedInternal:
		break;
	case vPrivate:
		if (label == "private:" || (SymbolType == eSymbolType::CLASS && label == ""))
			return true;
		break;
	default:
		break;
	}

	return false;
}

WTString CreateFromUsage::FindLnToInsert::GetLastLabel()
{
	CStringW lastLabel;
	for (size_t i = 0; i < DeepestClass->GetChildCount(); i++)
	{
		LineMarkers::Node& ch = DeepestClass->GetChild(i);
		FileLineMarker& marker = *ch;
		if (marker.mText == L"public:" || marker.mText == L"private:" || marker.mText == L"protected:" ||
		    marker.mText == L"__published:")
			lastLabel = marker.mText;
	}

	return lastLabel;
}

int CreateFromUsage::FindLnToInsert::GetClassRealEndLine(ULONG charPos)
{
	for (int i = int(charPos) - 1; i >= 0; i--)
	{
		if (FileBuf[i] == '}')
		{
			EdCntPtr curEd(g_currentEdCnt);
			return curEd->LineFromChar(i);
		}
	}

	return -1;
}

LineMarkers::Node* CreateFromUsage::FindLnToInsert::GetAppropriateVisibilityNode()
{
	CStringW labelWithColon = GetLabelStringWithColon(MemberVisibility);

	// workaround for ASTs ImplementNetInterfaceInCpp and ImplementNetInterfaceInCpp2
	LineMarkers::Node* node = DeepestClass;
	FileLineMarker& marker = **DeepestClass;
	if (!marker.mClassData.HasData() && DeepestClass->GetChildCount() > 0)
	{
		LineMarkers::Node& ch = DeepestClass->GetChild(0);
		node = &ch;
	}

	for (size_t j = 0; j < node->GetChildCount(); j++)
	{
		LineMarkers::Node& ch = node->GetChild(j);
		FileLineMarker& curMarker = *ch;
		if (curMarker.mText == labelWithColon)
			return &ch;
	}

	return nullptr;
}

int CreateFromUsage::GetPosToInsertLocalVariable()
{
	const UINT curFileId = gFileIdManager->GetFileId(mFileInvokedFrom);
	UINT altFileId = 0;
	if (::GetFileType(mFileInvokedFrom) == Header)
	{
		CStringW altFile(::GetFileByType(mFileInvokedFrom, Src));
		if (altFile.GetLength() && altFile != mFileInvokedFrom)
			altFileId = gFileIdManager->GetFileId(altFile);
	}

	const DType* sym = nullptr;
	EdCntPtr ed(g_currentEdCnt);
	const WTString buf(ed->GetBuf());
	const int kCurPos = ed->GetBufIndex(buf, (long)ed->CurPos());
	DTypeList defList;
	MultiParsePtr mp = ed->GetParseDb();
	mp->FindExactList(::TokenGetField(mInvokingScope, "-"), defList, false);
	for (const DType& cur : defList)
	{
		if (cur.FileId() == curFileId || cur.FileId() == altFileId)
		{
			sym = &cur;
			break;
		}
	}

	if (!sym || !sym->Line())
		return -1;

	WTString scope = ed->m_lastScope;
	long line = ed->LineFromChar((long)ed->CurPos());
	DTypePtr method = GetMethod(mp.get(), GetReducedScope(scope), scope, mp->m_baseClassList, nullptr, line);
	if (method) // fall-back to the original method
	{
		WTString fileBuf;
		int curPos = ed->GetBufIndex(buf, (long)ed->CurPos());
		const int openBrcPos = LocalRefactoring::FindOpeningBrace(method.get()->Line(), fileBuf, curPos);
		if (openBrcPos != -1)
			return openBrcPos + 1;
	}

	// fall-back to the old (=original) method of finding the '{' on the off chance the new method (above) fails for
	// some reason
	FindSymDef_MLC impl(gTypingDevLang);
	impl->FindSymbolInFile(mFileInvokedFrom, sym, TRUE);
	if (impl->mShortLineText.GetLength() && impl->IsDone() && impl->mBegLinePos < kCurPos)
	{
		// from current pos, find open brace
		int brcPos = buf.Find("{", impl->mBegLinePos);
		if (-1 != brcPos && brcPos < kCurPos)
			return brcPos + 1;
	}
	return -1;
}

int CreateFromUsage::GetPosToInsertGlobal()
{
	int insertPos = -1;
	EdCntPtr ed(g_currentEdCnt);
	const WTString curBuf(ed->GetBuf());
	int fromPos = ed->GetBufIndex(curBuf, (long)ed->CurPos());
	while (-1 == insertPos)
	{
		int spos = ::FindNextScopePos(gTypingDevLang, curBuf, fromPos, (ULONG)ed->CurLine(), FALSE);
		long line = ed->LineFromChar(spos);
		if (line < 1)
		{
			// top of file
			insertPos = 0;
			break;
		}

		// look for an empty line
		while (--line)
		{
			WTString lnTxt(ed->GetLine(line));
			lnTxt.TrimLeft();
			if (lnTxt.IsEmpty())
				break;
		}

		if (!line)
		{
			// top of file
			insertPos = 0;
			break;
		}

		// make sure we aren't going to insert into non-global scope (or string or comment)
		fromPos = ed->GetBufIndex(curBuf, ed->LineIndex(line));
		const WTString scp(::MPGetScope(ed->GetBuf(), mMp, fromPos));
		if (scp == DB_SEP_STR || scp == "::")
			insertPos = fromPos;
	}

	return insertPos;
}

WTString CreateFromUsage::InferTypeInParens(WTString lineText)
{
	WTString sig;
	int pos = lineText.Find(mNewSymbolName);
	if (-1 == pos)
		return sig;

	lineText = lineText.Mid(pos + mNewSymbolName.GetLength());
	// check again for Foo * f = new Foo
	pos = lineText.Find(mNewSymbolName);
	if (-1 != pos)
		lineText = lineText.Mid(pos + mNewSymbolName.GetLength());

	pos = lineText.Find('(');
	if (-1 == pos)
		return sig;

	int tmp = lineText.Find(')');
	if (-1 == tmp || tmp < pos)
		return sig;

	lineText = lineText.Mid(pos + 1);
	pos = lineText.Find(';');
	if (-1 != pos)
	{
		// [case: 68205]
		lineText = lineText.Left(pos);
		lineText.TrimRight();
	}
	if (lineText[lineText.GetLength() - 1] == ';')
	{
		lineText = lineText.Left(lineText.GetLength() - 1);
		lineText.TrimRight();
	}

	if (lineText[lineText.GetLength() - 1] == ')')
	{
		lineText = lineText.Left(lineText.GetLength() - 1);
		lineText.TrimRight();
	}

	// [case: 32269] watch out for: int x = (foo() * 2);
	if (lineText[0] != ')')
	{
		std::vector<WTString> args;
		GetArgs(lineText, args);

		bool addComma = false;
		for (std::vector<WTString>::const_iterator it = args.begin(); it != args.end(); ++it)
		{
			if (addComma)
				sig += ", ";
			addComma = true;

			// [case: 32269] more defense against linetext that goes beyond call
			WTString curItem(*it);
			int pos2 = curItem.Find(')');
			if (-1 != pos2)
			{
				curItem = curItem.Left(pos2);
				sig += curItem;
				break;
			}
			sig += *it;
		}
	}

	return sig;
}

void CreateFromUsage::LookupParentDataIfAsMember(WTString symScope)
{
	if (!mParentDataBackingStore.IsEmpty())
		mParentDataBackingStore = DType();
	CStringW fileW(::FileFromScope(symScope, &mParentDataBackingStore));
	if (mParentDataBackingStore.IsEmpty())
		mParentDataIfAsMember = nullptr;
	else
		mParentDataIfAsMember = &mParentDataBackingStore;
}

void CreateFromUsage::RefineCreationType()
{
	if (ctClass == mCreateType || ctUndetermined == mCreateType || ctConstant == mCreateType ||
	    ctEnumMember == mCreateType || ctParameter == mCreateType)
		return;

	WTString lineText(g_currentEdCnt->GetLine(g_currentEdCnt->CurLine()));
	lineText.ReplaceAll("const", "", TRUE);
	lineText.ReplaceAll("constexpr", "", TRUE);
	lineText.ReplaceAll("consteval", "", TRUE);
	lineText.ReplaceAll("constinit", "", TRUE);
	lineText.ReplaceAll("_CONSTEXPR17", "", TRUE);
	lineText.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
	lineText.ReplaceAll("_CONSTEXPR20", "", TRUE);
	lineText.ReplaceAll("static", "", TRUE);
	lineText.ReplaceAll("thread_local", "", TRUE);
	lineText.TrimLeft();
	const WTString defType("xxxVAxxxVAzzz");
	WTString declType(InferDeclType(defType, lineText));
	declType.ReplaceAll("const", "", TRUE);
	declType.ReplaceAll("constexpr", "", TRUE);
	declType.ReplaceAll("consteval", "", TRUE);
	declType.ReplaceAll("constinit", "", TRUE);
	declType.ReplaceAll("_CONSTEXPR17", "", TRUE);
	declType.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE);
	declType.ReplaceAll("_CONSTEXPR20", "", TRUE);
	declType.ReplaceAll("*", "", FALSE);
	declType.ReplaceAll("&", "", FALSE);
	declType.Trim();

	if (declType != defType && declType != mNewSymbolName)
	{
		// [case: 84639]
		if (-1 == lineText.Find("new") && -1 == lineText.FindNoCase("alloc"))
			return;
	}

	if (-1 != lineText.Find("new " + mNewSymbolName))
	{
		mCreateType = ctClass;
	}
	else if (-1 != lineText.FindNoCase("alloc"))
	{
		// malloc / _malloca / _aligned_malloc / _malloc_dbg
		// alloca / _alloca
		// calloc / _calloc_dbg
		// realloc / _realloc_dbg
		// HeapAlloc
		if (-1 != lineText.Find("malloc") || -1 != lineText.Find("alloca") || -1 != lineText.Find("calloc") ||
		    -1 != lineText.Find("realloc") || -1 != lineText.Find("HeapAlloc"))
		{
			mCreateType = ctClass;
		}
	}
	else if (-1 != lineText.Find("new"))
	{
		// new (std::nothrow) Foo;
		// new (placement) Foo;
		int newPos = lineText.Find("new");
		if (-1 != newPos)
		{
			int openParen = lineText.Find("(", newPos);
			if (-1 != openParen)
			{
				int closeParen = lineText.Find(")", openParen);
				if (-1 != closeParen)
				{
					int symPos = lineText.Find(mNewSymbolName, closeParen);
					if (-1 != symPos)
					{
						mCreateType = ctClass;
					}
				}
			}
		}
	}
	else if (0 == lineText.Find(mNewSymbolName) && 0 != lineText.Find(mNewSymbolName + "(") &&
	         0 != lineText.Find(mNewSymbolName + " ("))
	{
		bool hadSpace = false;
		TCHAR ch;
		for (int x = 0; (ch = lineText[x]) != _T('\0'); ++x)
		{
			if (ch == _T('*'))
			{
				// assume type:
				// Foo* v;
				hadSpace = true;
				break;
			}

			if (ch == _T(';'))
				break;

			if (ch == _T(' ') || ch == _T('\t'))
				hadSpace = true;
			else if (ch == _T('(') && hadSpace)
				break; // Foo ff(true);
			else if (!ISCSYM(ch))
				return;
		}

		if (hadSpace)
			mCreateType = ctClass;
	}
}

BOOL CreateFromUsage::CreateClass()
{
	TraceScopeExit tse("Create Class exit");
	const CStringW newFile(mNewSymbolName.Wide() + (gTypingDevLang == CS ? L".cs" : L".h"));
	const WTString lineText(g_currentEdCnt->GetLine(g_currentEdCnt->CurLine()));
	const WTString defArgs(InferTypeInParens(lineText));
	WTString classDec;

	// create class members if there were parameters in constructor
	WTString tmp(gAutotextMgr->GetSource(::GetClassAutotextItemTitle()));
	tmp.ReplaceAll("$ParameterList$", defArgs);

	WTString symNamePrefix, symNameSuffix;
	if (!defArgs.IsEmpty())
	{
		// find prefix or suffix used for "$MemberName$"
		const int pos = tmp.Find("$MemberName$");
		if (-1 != pos)
		{
			// read from last $ to get suffix
			int i = pos + 12;
			TCHAR ch;
			while ((ch = tmp[i++]) != _T('\0') && ISCSYM(ch))
				symNameSuffix += ch;

			// rewind from first $ to get prefix
			i = pos - 1;
			while (i >= 0 && ((ch = tmp[i--]) != _T('\0')) && ISCSYM(ch))
				;
			if (i >= 0)
				i += 2;
			if (i >= 0 && i < pos)
				symNamePrefix = tmp.Mid(i, pos - i);
		}
	}

	StrVectorA classDecLines;
	StrVectorA argPairs;
	::WtStrSplitA(tmp, classDecLines, "\n");
	::WtStrSplitA(defArgs, argPairs, ",");

	// iterate over each line in the refactor snippet
	for (WTString curLine : classDecLines)
	{
		if (-1 == curLine.Find("$InitializeMember$") && -1 == curLine.Find("$MemberInitializationList$") &&
		    -1 == curLine.Find("$MemberType$") && -1 == curLine.Find("$MemberName$"))
		{
			if (defArgs.IsEmpty())
			{
				// remove $colon$ if no members
				curLine.Replace(" $colon$", "");
				curLine.Replace("$colon$", "");
			}
			else
				curLine.Replace("$colon$", ":");

			classDec += curLine;
		}
		else if (defArgs.IsEmpty())
		{
			// remove lines (by skipping them) that deal with init list and members when
			// no params were present in ctor
		}
		else
		{
			bool didInsertColon = false;
			const size_t pairs = argPairs.size();
			// iterate over each argument passed to ctor
			for (size_t i = 0; i < pairs;)
			{
				WTString newLine(curLine);
				WTString curPair(argPairs[i++]);
				WTString curMemberSymType, curMemberSymName, curArgSymName;
				if (curPair[curPair.GetLength() - 1] == ',')
					curPair = curPair.Left(curPair.GetLength() - 1);
				int pos = curPair.ReverseFind(' ');
				if (-1 != pos)
				{
					curMemberSymType = curPair.Left(pos);
					curMemberSymType.TrimLeft();
					curMemberSymType.TrimRight();

					curMemberSymName = curPair.Mid(pos);
					curMemberSymName.TrimLeft();
					curMemberSymName.TrimRight();
					curArgSymName = curMemberSymName;
					if (!curMemberSymName.IsEmpty() && Settings::cmn_noChange != Psettings->mClassMemberNamingBehavior)
					{
						// make first letter upper if prefix is empty or if prefix ends with alpha
						if (Settings::cmn_alwaysUpper == Psettings->mClassMemberNamingBehavior ||
						    Settings::cmn_alwaysLower == Psettings->mClassMemberNamingBehavior ||
						    (Settings::cmn_prefixDependent == Psettings->mClassMemberNamingBehavior &&
						     symNamePrefix.IsEmpty()) ||
						    (!symNamePrefix.IsEmpty() &&
						     (Settings::cmn_prefixDependent == Psettings->mClassMemberNamingBehavior ||
						      Settings::cmn_onlyIfPrefixEndAlpha == Psettings->mClassMemberNamingBehavior) &&
						     wt_isalpha(symNamePrefix[symNamePrefix.GetLength() - 1])))
						{
							WTString t = curMemberSymName.Left(1);
							if (Settings::cmn_alwaysLower == Psettings->mClassMemberNamingBehavior)
								t.MakeLower();
							else
								t.MakeUpper();
							curMemberSymName.SetAt(0, t[0]);
						}
					}
				}

				if (-1 != newLine.Find("$InitializeMember$"))
				{
					// intermediate substitution of $InitializeMember$
					WTString curArgInit;
					curArgInit.WTFormat("%s$MemberName$%s = %s", symNamePrefix.c_str(), symNameSuffix.c_str(),
					                    curArgSymName.c_str());
					newLine.Replace("$InitializeMember$", curArgInit.c_str());
				}
				else if (-1 != newLine.Find("$MemberInitializationList$"))
				{
					// intermediate substitution of $MemberInitializationList$
					WTString curArgInit;
					curArgInit.WTFormat("%s$MemberName$%s(%s)%s", symNamePrefix.c_str(), symNameSuffix.c_str(),
					                    curArgSymName.c_str(),
					                    // on all passes except last, append comma
					                    (i >= pairs ? "" : ","));
					newLine.Replace("$MemberInitializationList$", curArgInit.c_str());
				}

				newLine.Replace("$MemberType$", curMemberSymType.c_str());
				newLine.Replace("$MemberName$", curMemberSymName.c_str());
				if (didInsertColon)
				{
					newLine.Replace("$colon$ ", "");
					newLine.Replace("$colon$", "");
				}
				else
				{
					newLine.Replace("$colon$", ":");
					didInsertColon = true;
				}
				classDec += newLine;
			}
		}
	}

	classDec.ReplaceAll("$ClassName$", ::EncodeUserText(mNewSymbolName));

	// [case: 9863] remove unsupported symbol related snippet reserved words in refactoring snippets
	classDec.ReplaceAll("$MethodName$", "");
	classDec.ReplaceAll("$MethodArgs$", "");
	classDec.ReplaceAll("$BaseClassName$", "");

	WTString namespaceName("MyNamespace");
	if (mBaseScope.GetLength())
	{
		DType* data = mMp->FindExact(mBaseScope);
		if (data && data->IsType() && data->type() != NAMESPACE)
			namespaceName = ::StrGetSymScope(mBaseScope);
		else
			namespaceName = mBaseScope;

		if (namespaceName[0] == DB_SEP_CHR)
			namespaceName = namespaceName.Mid(1);

		// [case: 80179]
		int fType = ::GetFileType(mFileInvokedFrom);
		if (IsCFile(fType))
			namespaceName.ReplaceAll(DB_SEP_STR, "::");
		else
			namespaceName.ReplaceAll(DB_SEP_STR, ".");
	}
	classDec.ReplaceAll("$NamespaceName$", namespaceName);

	const BOOL res = ::RefactorDoCreateFile(::IsCfile(mFileInvokedFrom) ? CreateFileDlg::DlgCreateStructAndFile
	                                                                    : CreateFileDlg::DlgCreateClassAndFile,
	                                        VARef_CreateFromUsage, g_currentEdCnt, false, newFile, classDec);

	if (gTestLogger)
	{
		WTString msg;
		msg.WTFormat("CFU Class: %s %d", (LPCTSTR)CString(newFile), res);
		gTestLogger->LogStr(msg);
		gTestLogger->LogStr(classDec);
	}

	return res;
}

WTString GetDeclTypeFromDef(MultiParse* mp, WTString def, WTString scope, int symType)
{
	WTString tmp;
	if (VAR == symType || FUNC == symType)
	{
		tmp = DecodeTemplates(def);
		tmp = GetTypeFromDef(tmp, gTypingDevLang);

		int pos = tmp.ReverseFind('*');
		if (-1 != pos)
		{
			pos = tmp.Find("const", pos);
			if (-1 != pos)
			{
				// chop off const suffix:
				// const double * const -->> const double *
				tmp = tmp.Left(pos);
				tmp.TrimRight();
			}
		}

		if (tmp.GetTokCount(' '))
		{
			// [case: 86415]
			StrVectorA wds;
			// this would better to break on * and keep parens together (as in "_declspec( foo )" )
			WtStrSplitA(tmp, wds, " ");
			int otherCnt = 0;
			uint lastType = UNDEF;

			for (WTString wd : wds)
			{
				if (wd.IsEmpty())
					continue;

				DType* t = mp->FindSym(&wd, &scope, nullptr,
				                       FDF_NoConcat | FDF_GotoDefIsOk | FDF_GUESS | FDF_NoAddNamespaceGuess);
				if (t)
					lastType = t->MaskedType();
				else
					lastType = UNDEF;

				switch (lastType)
				{
				case DEFINE:
				case RESWORD:
					break;
				default:
					++otherCnt;
				}
			}

			if (lastType == DEFINE && otherCnt)
			{
				// if last sym is MACRO and at least one other sym is not RESWORD or MACRO,
				// then eat last MACRO (assume is call decorator).
				// change "size_type _CLR_OR_THIS_CALL" to simply "size_type"
				const int pos2 = tmp.ReverseFind(wds[wds.size() - 1]);
				if (-1 != pos2)
				{
					tmp = tmp.Left(pos2);
					tmp.Trim();
				}
			}
		}
	}
	else
	{
		try
		{
			tmp = GetTypesFromDef(scope, def, symType, gTypingDevLang);
		}
		catch (const WtException&)
		{
		}
		int pos = tmp.Find('\f');
		if (-1 != pos)
			tmp = tmp.Left(pos);
		if (tmp.GetLength() && tmp[0] == DB_SEP_CHR)
			tmp = tmp.Mid(1);

		// CleanupTypeString calls DecodeScope, but we need DecodeTemplates
		// If CleanupTypeString gets changed, then this call could go away
		tmp = DecodeTemplates(tmp);
		CleanupTypeString(tmp, gTypingDevLang);
	}

	return tmp;
}

WTString GetDeclTypeFromDef(MultiParse* mp, DType* dt, int symType)
{
	dt->LoadStrs();
	return GetDeclTypeFromDef(mp, dt->Def(), dt->SymScope(), symType);
}

bool ValidateData(DType* data)
{
	if (data && data->SymScope().Find(":ForwardDeclare:") == -1 && data->SymScope().Find(":TemplateParameter:") == -1)
	{
		const CStringW deffile = gFileIdManager->GetFileForUser(data->FileId());
		if (deffile.GetLength())
		{
			const int fType = ::GetFileType(deffile);
			if (Is_C_CS_File(fType))
				return TRUE;
		}
	}

	return FALSE;
}
