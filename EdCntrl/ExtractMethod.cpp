#include "StdAfxEd.h"
#include "ExtractMethod.h"
#include "Foo.h"
#include "ExtractMethodDlg.h"
#include "AutotextManager.h"
#include "file.h"
#include "wt_stdlib.h"
#include "RegKeys.h"
#include "DBLock.h"
#include "StringUtils.h"
#include "EolTypes.h"
#include "DevShellAttributes.h"
#include "FileId.h"
#include "VARefactor.h"
#include "DevShellService.h"
#include "FileTypes.h"
#include "PROJECT.H"
#include "LocalRefactoring.h"
#include "UndoContext.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

BOOL PV_InsertAutotextTemplate(const WTString& templateText, BOOL reformat, const WTString& promptTitle = NULLSTR);

MethodExtractor::MethodExtractor(int fType) : VAParseMPScope(fType), mArgCount(0), mIsStatic(false)
{
	m_firstVisibleLine = 1;
	m_Scoping = TRUE;
	m_parseTo = 0;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	mIsClassmethod = false; // [case: 135862]
#endif
}

WTString GetInnerScopeName(WTString fullScope)
{
	int len = fullScope.GetLength();
	if (len && fullScope[len - 1] == ':')
		fullScope = fullScope.Left(len - 1);

	WTString innerScope;
	int minusPos = fullScope.ReverseFind("-");
	int colonPos = fullScope.ReverseFind(":");
	if (minusPos != -1 && (colonPos == -1 || minusPos > colonPos))
		innerScope = fullScope.Left(minusPos);
	else
		innerScope = fullScope;
	if (colonPos == innerScope.GetLength() - 1)
		innerScope = innerScope.Left(colonPos);

	colonPos = innerScope.ReverseFind(":");
	if (colonPos != -1)
		innerScope = innerScope.Right(innerScope.GetLength() - colonPos - 1);

	return innerScope;
}

extern void InsertNewLinesBeforeAfterIfNeeded(WTString& impText, const WTString& fileBuf);

FinishMethodExtractorParams sFinishMethodExtractorParams;

void CALLBACK FinishMethodExtractor(HWND hWnd, UINT ignore1, UINT idEvent, DWORD ignore2)
{
	if (idEvent)
		KillTimer(hWnd, idEvent);

	RefactoringActive active;
	DB_READ_LOCK;
	RefactoringActive::SetCurrentRefactoring(VARef_ExtractMethod);
	std::shared_ptr<UndoContext> undoContext = sFinishMethodExtractorParams.mUndoContext;
	sFinishMethodExtractorParams.mUndoContext = nullptr;

	BOOL r = PV_InsertAutotextTemplate(sFinishMethodExtractorParams.mImplCode, TRUE);
	if (r && sFinishMethodExtractorParams.mExtractToSrc)
	{
		// Move to source file
		// Pass method scope because caret is not on new method
		if (g_currentEdCnt)
			g_currentEdCnt->GetBuf(TRUE);
		WTString moveImplScope = sFinishMethodExtractorParams.mExtractAsFreeFunction
		                             ? sFinishMethodExtractorParams.mFreeFuncMoveImplScope
		                             : sFinishMethodExtractorParams.mBaseScope;
		VARefactorCls ref((moveImplScope + DB_SEP_CHR + sFinishMethodExtractorParams.mMethodName).c_str());
		_ASSERTE(g_currentEdCnt);
		const DType dt(g_currentEdCnt->GetSymDtype());
		if (ref.CanMoveImplementationToSrcFile(&dt, "", TRUE))
			ref.MoveImplementationToSrcFile(&dt, "", TRUE);
	}

	sFinishMethodExtractorParams.Clear();
	RefactoringActive::SetCurrentRefactoring(0);
}

BOOL MethodExtractor::ExtractMethod(const CStringW& destinationHeaderFile)
{
	std::shared_ptr<UndoContext> undoContext = std::make_shared<UndoContext>("ExtractMethod");
	mMethodName = "MyMethod";
	mTargetFile = g_currentEdCnt->FileName();

	if (GatherMethodInfo())
	{
		if (m_baseScope.GetLength() && ::GetFileType(mTargetFile) == Src)
		{
			// Find header to put it in
			DType data;
			CStringW tmp(::FileFromScope(m_baseScope, &data));
			if (!data.IsEmpty())
				mTargetFile = tmp;

			if (!mTargetFile.GetLength())
			{
				mTargetFile = (!data.IsEmpty() && data.infile()) ? g_currentEdCnt->FileName() : destinationHeaderFile;
			}
		}

		WTString tmpMethodSignature = CreateTempMethodSignature(mMethodParameterList, mMethodReturnType, true);
		WTString tmpMethodSignature_free =
		    CreateTempMethodSignature(mMethodParameterList_Free, mMethodReturnType_Free, false);

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		// [case: 135862]
		WTString scopeMethod = ::TokenGetField(m_orgScope, "-");
		DTypeList dts;
		m_mp->FindExactList(scopeMethod, dts, false);
		for (const auto& dt : dts)
		{
			if (dt.IsCppbClassMethod())
			{
				tmpMethodSignature.prepend("__classmethod ");
				mIsClassmethod = true;
				break;
			}
		}
#endif

		auto [canExtractToSource, canExtractAsFreeFunction] = GetExtractionOptions();
		ExtractMethodDlg methDlg(NULL, mMethodName, tmpMethodSignature, tmpMethodSignature_free, canExtractToSource,
		                         canExtractAsFreeFunction);
		if (methDlg.DoModal() != IDOK)
			return FALSE;

		WTString jumpName, jumpScope, freeFuncMoveImplScope;
		UINT type;

		GetJumpInfo(jumpName, jumpScope, freeFuncMoveImplScope, type, methDlg, canExtractAsFreeFunction);

		// insert the call to the new method
		if (!PV_InsertAutotextTemplate(mMethodInvocation, TRUE))
			return FALSE;

		// insert the implementation
		if (MoveCaret(jumpName, jumpScope, type))
		{
			bool extractToSrc = methDlg.IsExtractToSrc();
			bool extractAsFreeFunction = !!methDlg.m_extractAsFreeFunction;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			if (extractAsFreeFunction) // [case: 135862]
				mIsClassmethod = false;
#endif

			WTString implCode(BuildImplementation(mMethodParameterList));

			// case 87859
			if (!canExtractAsFreeFunction)
			{
				WTString fileText(g_currentEdCnt->GetBuf(TRUE));
				InsertNewLinesBeforeAfterIfNeeded(implCode, fileText);
			}

			RefactoringActive::SetCurrentRefactoring(0);

			sFinishMethodExtractorParams.Load(undoContext, implCode, freeFuncMoveImplScope, m_baseScope, mMethodName,
			                                  extractToSrc, extractAsFreeFunction);
			if (gShellAttr->IsDevenv15OrHigher())
			{
				// [case: 109020]
				vLog("WARN: FinishMethodExtractor async");
				SetTimer(nullptr, 0, USER_TIMER_MINIMUM, (TIMERPROC)(uintptr_t)&FinishMethodExtractor);
			}
			else
				FinishMethodExtractor();

			return TRUE;
		}
	}

	WtMessageBox("Cannot extract method from selected text.", IDS_APPNAME, MB_OK | MB_ICONERROR);
	return FALSE;
}

BOOL MethodExtractor::GatherMethodInfo()
{
	mMethodParameterList.Empty();
	mMethodParameterList_Free.Empty();
	mMethodBody_Free.Empty();
	mMethodReturnType_Free.Empty();
	mMethodReturnType = "void";

	mMethodBody = g_currentEdCnt->GetSelString();
	if (!ParseMethodBody(g_currentEdCnt->m_lastScope))
		return FALSE;

	const WTString lnBrk(g_currentEdCnt->GetLineBreakString());
	WTString rAssign, rAssign_free, insertedCallStatementTermination = ";" + lnBrk;
	WTString bodyCommonLeadingWhitespace(::GetCommonLeadingWhitespace(mMethodBody));
	if (bodyCommonLeadingWhitespace.IsEmpty())
		bodyCommonLeadingWhitespace = "\t";
	const WTString strippedBody(::StripCommentsAndStrings(FileType(), mMethodBody));

	if (State().m_parseState != VPS_NONE)
	{
		// Method from expression: "(a*a/2)" -> "Meth(int a){ return (a*a/2);}"
		if (mReturnTypeData && mReturnTypeData->IsType())
		{
			mMethodReturnType = WTString(StrGetSym(mReturnTypeData->SymScope()));
			// don't return a reference to a local var
			// 			if (Src == gTypingDevLang || Header == gTypingDevLang)
			// 			{
			// 				if (!::IsCfile(g_currentEdCnt->FileName()))
			// 					mMethodReturnType += "&";
			// 			}
		}
		else
			mMethodReturnType = mReturnTypeData ? ::GetTypeFromDef(mReturnTypeData->Def(), FileType()) : NULLSTR;
		if (mMethodReturnType.IsEmpty() && mMethodBody.find_first_of("<>=!|") == -1 && mMethodBody.GetLength())
			mMethodReturnType = ::SimpleTypeFromText(FileType(), mMethodBody);
		if (mMethodReturnType.IsEmpty() || mMethodBody.find_first_of("<>=!|") != -1)
			mMethodReturnType = "bool";
		mMethodBody = WTString("return ") + mMethodBody + ";";
		insertedCallStatementTermination.Empty(); // do not append ;
	}
	else if (strstrWholeWord(strippedBody, "return") || strstrWholeWord(strippedBody, "co_return") ||
	         strstrWholeWord(strippedBody, "co_yield"))
	{
		// Selection has a return, so Meth must return same type as current method
		// get return type of current method
		WTString wd(TokenGetField(m_mp->m_lastScope, "-"));
		DType* data = m_mp->FindExact2(wd);
		if (!data)
		{
			// [case: 77282]
			data = m_mp->FindSym(&wd, &m_orgScope, &m_baseScopeBCL);
		}
		if (data)
		{
			WTString def(data->Def());
			const int fFeedPos = def.Find('\f');
			if (fFeedPos != -1)
				def = def.Left(fFeedPos);
			mMethodReturnType = ::GetTypeFromDef(def, FileType());
			if (mMethodReturnType == "void" || mMethodReturnType == "VOID" ||
			    (FileType() == UC && mMethodReturnType == "function"))
			{
				insertedCallStatementTermination = ";" + lnBrk + bodyCommonLeadingWhitespace + "return;";
			}
			else
				rAssign = "return ";
		}
		else
			return FALSE;
	}
	else
	{
		// Look to see how many passed args were modified
		int nChanged = 0, rArg = -1;
		int nChanged_free = 0, rArg_free = -1;
		for (int x = 0; x < mArgCount; x++)
		{
			if (mMethodArgs[x].modified && !(mMethodArgs[x].modified & 0x2))
			{
				if (!mMethodArgs[x].member)
				{
					nChanged++;
					rArg = x;
				}

				nChanged_free++;
				rArg_free = x;
			}
		}
		if (nChanged == 1)
		{
			// Only one arg was modified, return that arg and set it in the code
			mMethodReturnType = ::GetTypeFromDef(mMethodArgs[rArg].data->Def(), FileType());
			rAssign = mMethodArgs[rArg].arg + " = ";
			mMethodBody += bodyCommonLeadingWhitespace + WTString("return ") + mMethodArgs[rArg].arg + ";";
			mMethodArgs[rArg].modified = FALSE; // so we don't add make it a reference "int &i"
		}
		else
		{
			if (nChanged_free == 1)
			{
				// Only one arg was modified, return that arg and set it in the code
				mMethodReturnType_Free = ::GetTypeFromDef(mMethodArgs[rArg_free].data->Def(), FileType());
				rAssign_free = mMethodArgs[rArg_free].arg + " = ";
				mMethodBody_Free = mMethodBody;
				mMethodBody_Free +=
				    bodyCommonLeadingWhitespace + WTString("return ") + mMethodArgs[rArg_free].arg + ";";
				mMethodArgs[rArg_free].modified = FALSE; // so we don't add make it a reference "int &i"
			}
		}
	}

	if (mMethodBody_Free.IsEmpty())
		mMethodBody_Free = mMethodBody;
	if (mMethodReturnType_Free.IsEmpty())
		mMethodReturnType_Free = mMethodReturnType;
	if (rAssign_free.IsEmpty())
		rAssign_free = rAssign;

	const bool kIsC = (Src == FileType() || Header == FileType()) && ::IsCfile(g_currentEdCnt->FileName());
	if (kIsC && Src == ::GetFileType(mTargetFile))
		mAutotextItemTitle = "Refactor Create Implementation";
	else
		mAutotextItemTitle = "Refactor Extract Method";

	// Get arg list
	WTString argsToMethodCall;
	WTString argsToMethodCall_free;
	int nonFreeCounter = 0;
	for (int i = 0; i < mArgCount; nonFreeCounter += !mMethodArgs[i].member, i++)
	{
		const WTString kCurArgVal(mMethodArgs[i].data->Def());
		const WTString commaStr(nonFreeCounter ? ", " : "");
		const WTString commaStr_free(i ? ", " : "");
		const WTString addressOfStr(
		    kIsC && mMethodArgs[i].modified && !mMethodArgs[i].data->IsPointer() && !m_mp->IsPointer(kCurArgVal) ? "&"
		                                                                                                         : "");

		bool clearCsRefStrAfterCall = false;
		WTString refStr;
		if (mMethodArgs[i].modified)
		{
			// TODO: make this whole class lang safe for VB/UC
			switch (FileType())
			{
			case CS:
				refStr = "ref ";
				{
					WTString argIdataDef(mMethodArgs[i].data->Def());
					const WTString argType = ::GetTypeFromDef(argIdataDef, FileType());
					if (!argType.IsEmpty())
					{
						if (gShellAttr->IsDevenv8OrHigher())
						{
							// classes are passed by ref implicitly per:
							// http://msdn2.microsoft.com/en-us/library/8b0bdca4(VS.80).aspx
							// but that does not seem to be the case before .Net 2:
							// http://msdn2.microsoft.com/en-us/library/0f66670z(VS.71).aspx
							DType* data = m_mp->FindAnySym(argType);
							if (data)
							{
								if (data->MaskedType() == CLASS)
									refStr.Empty();
							}
						}

						// check if arg is a foreach iter
						if (argIdataDef.Find(" in ") != -1)
						{
							// case: 6951 - foreach vars can not be passed by ref
							refStr.Empty();
						}
						else if (0 == argType.Find("ref "))
						{
							// [case: 76987] don't add ref if already there to begin with
							clearCsRefStrAfterCall = true;
						}
					}
				}
				break;
			case Src:
			case Header:
				if (kIsC)
				{
					if (!mMethodArgs[i].data->IsPointer() && !m_mp->IsPointer(kCurArgVal))
						refStr = "*";
				}
				else if (mMethodArgs[i].data->Def().Find("^") != -1)
					refStr.Empty(); // [case: 14864] is a managed ref ^
				else
					refStr = "&";
				break;
			case UC: // I think UC passes all args by reference?
			default:
				break;
			}
		}

		// C# ref: http://msdn2.microsoft.com/en-us/library/14akc2c7(VS.71).aspx
		// http://stackoverflow.com/questions/924360/difference-between-c-reference-type-argument-passing-and-cs-ref
		const WTString paramModifier(CS == FileType() ? refStr : addressOfStr);
		bool member = mMethodArgs[i].member;
		if (!member)
			argsToMethodCall += commaStr + paramModifier + mMethodArgs[i].arg;
		argsToMethodCall_free += commaStr_free + paramModifier + mMethodArgs[i].arg;
		if (clearCsRefStrAfterCall)
			refStr.Empty();

		if (!member)
			mMethodParameterList += commaStr;
		mMethodParameterList_Free += commaStr_free;
		if (mMethodArgs[i].modified)
		{
			if (CS == FileType())
			{
				if (!member)
					mMethodParameterList += refStr + ::GetTypeFromDef(kCurArgVal, FileType()) + " ";
			}
			else
			{
				if (!member)
					mMethodParameterList += ::GetTypeFromDef(kCurArgVal, FileType()) + " " + refStr;
				mMethodParameterList_Free += ::GetTypeFromDef(kCurArgVal, FileType()) + " " + refStr;
			}
			if (!member)
				mMethodParameterList = DecodeScope(mMethodParameterList);
			mMethodParameterList_Free = DecodeScope(mMethodParameterList_Free);

			if (kIsC)
			{
				// change method body in C source - no references, etc.
				if (!mMethodArgs[i].member)
					PointerRelatedReplaces(mMethodBody, i, addressOfStr, refStr);
				PointerRelatedReplaces(mMethodBody_Free, i, addressOfStr, refStr);
			}
		}
		else
		{
			if (!member)
				mMethodParameterList += ::GetTypeFromDef(kCurArgVal, FileType()) + " ";
			mMethodParameterList_Free += ::GetTypeFromDef(kCurArgVal, FileType()) + " ";
		}
		if (!member)
			mMethodParameterList += mMethodArgs[i].arg;
		mMethodParameterList_Free += mMethodArgs[i].arg;
	}

	// logic above may add int &&i, if "i" was already a reference.
	mMethodParameterList.ReplaceAll("&&", "&", FALSE);
	mMethodParameterList.ReplaceAll(" & &", " &", FALSE);
	mMethodParameterList.ReplaceAll("& &", " &", FALSE);
	mMethodParameterList_Free.ReplaceAll("&&", "&", FALSE);
	mMethodParameterList_Free.ReplaceAll(" & &", " &", FALSE);
	mMethodParameterList_Free.ReplaceAll("& &", " &", FALSE);

	if (FileType() ==
	    UC /*&& mMethodReturnType == "void"*/) // All methods in UC need "function" regardless of return type
	{
		if (mMethodReturnType == "void")
			mMethodReturnType = "function"; // bug 1395
		else
			mMethodReturnType = "function " + mMethodReturnType; // bug 1395
		mMethodReturnType.ReplaceAll("local", "", TRUE);
	}
	mMethodReturnType.TrimRight();

	// build the call to the new method
	mMethodInvocation = rAssign + mMethodName + "(" + argsToMethodCall + ")" + insertedCallStatementTermination;
	mMethodInvocation_Free =
	    rAssign_free + mMethodName + "(" + argsToMethodCall_free + ")" + insertedCallStatementTermination;

	// trim the body
	TrimTheBody(mMethodBody);
	TrimTheBody(mMethodBody_Free);

	return TRUE;
}

void MethodExtractor::DoScope()
{
	VAParseMPScope::DoScope();
	if (m_startReparseLine)
		return; // first-pass parsing local vars
	if (/*!State().m_lwData &&*/ !IsXref())
	{
		if (State().m_lwData && State().m_lwData->HasLocalScope())
			return; // local var defined in selection
		WTString cwd = GetCStr(CurPos());
		int i = 0;
		while (i < mArgCount && cwd != mMethodArgs[i].arg)
			i++;
		if (i == mArgCount)
		{
			if (mArgCount == kMaxArgs)
				return;
			DType* data = m_mp->FindSym(&cwd, &m_orgScopeUncorrected, &m_baseScopeBCL);
			bool member = false;
			if (data)
				member = IsMember(data);
			if (data && (data->HasLocalScope() || member))
			{
				// case=21012 param could be "struct FOO f" or "class Foo f"
				if (data->MaskedType() == VAR || data->MaskedType() == STRUCT || data->MaskedType() == CLASS ||
				    data->MaskedType() == LINQ_VAR || // [case: 86109]
				    data->MaskedType() == Lambda_Type)
				{
					mMethodArgs[mArgCount].arg = cwd;
					mMethodArgs[mArgCount].data = data;
					mMethodArgs[mArgCount].modified = 0;
					mMethodArgs[mArgCount].member = member;
					mArgCount++;
				}
			}

			if (data)
				State().m_lwData = std::make_shared<DType>(data);
			else
				State().m_lwData.reset();
		}
		LPCSTR op = CurPos() + cwd.GetLength();
		while (wt_isspace(*op))
			op++;
		WTString opStr;
		for (LPCSTR p = op; strchr("!@#$%^&*-+=<>.", *p); p++)
			opStr += *p;

		// check for []
		bool isArrayAccess = false;
		if (opStr.IsEmpty() && *op == '[')
		{
			const bool kIsCpp = (Src == FileType() || Header == FileType()) && !::IsCfile(g_currentEdCnt->FileName());
			if (kIsCpp && i < mArgCount && !(mMethodArgs[i].data && mMethodArgs[i].data->IsPointer()))
			{
#if PASS_ARRAYS_BY_REFERENCE
				// assume there is a closing bracket, don't check for assignment, etc
				mMethodArgs[i].modified |= 0x2;
#else
				// check for closing bracket, only pass by reference if assignment, etc
				LPCTSTR p = op;
				while (*p && *p != ']')
					++p;
				if (*p++ == ']')
				{
					op = p;
					while (wt_isspace(*op))
						op++;
					for (LPCSTR p2 = op; strchr("!@#$%^&*-+=<>.", *p2); p2++)
						opStr += *p2;

					isArrayAccess = true;
				}
#endif
			}
		}

		if ((0 != strncmp(op, "==", 2)) &&               // i == ...
		    (0 == strncmp(op, "=", 1) ||                 // i = ...
		     0 == strncmp(op, "++", 2) ||                // i++
		     0 == strncmp(op, "--", 2) ||                // i--
		     0 == strncmp(op, "<<", 2) ||                // i << [case: 119662]
		     (strchr("%^&*-+", op[0]) && op[1] == '='))) // i %=, ^=, &=, *=, -=, += ...
		{
			mMethodArgs[i].modified |= 0x1;
			if (isArrayAccess)
				mMethodArgs[i].modified |= 0x2;
		}
		else if (State().m_parseState == VPS_ASSIGNMENT)
		{
			// check for pre-increment/decrement (previous if only works for post-inc/dec)
			LPCSTR op2 = State().m_lastScopePos;
			if (0 == strncmp(op2, "++", 2) || // ++i
			    0 == strncmp(op2, "--", 2))   // --i
			{
				mMethodArgs[i].modified |= 0x1;
				if (isArrayAccess)
					mMethodArgs[i].modified |= 0x2;
			}
		}

		if (opStr == ".") // called member that may or may not change contents, better pass as a reference
			mMethodArgs[i].modified |= 0x2;
	}
	if (m_deep && State().m_lwData && (m_deep == 1 || !mReturnTypeData))
		mReturnTypeData = State().m_lwData;

	if (mReturnTypeData)
		mReturnTypeData->LoadStrs();
}

BOOL MethodExtractor::ProcessMacro()
{
	if (!m_startReparseLine)
		return FALSE; // don't process macros when looking for local variables
	return VAParseMPScope::ProcessMacro();
}

BOOL MethodExtractor::ParseMethodBody(WTString scope)
{
	mReturnTypeData.reset();
	// Parse code looking for undefined variables and generate args and return type
	if (!g_currentEdCnt)
		return FALSE;

	DB_READ_LOCK;
	m_mp = g_currentEdCnt->GetParseDb();
	m_orgScopeUncorrected = m_orgScope = scope;
	m_baseScope = m_mp->m_baseClass;
	m_baseScopeBCL = m_mp->GetBaseClassList(m_baseScope);

	if (m_baseScope.GetLength() && ::GetFileType(mTargetFile) == Src)
	{
		// [case: 20072]
		AdjustScopesForNamespaceUsings(m_mp.get(), m_baseScopeBCL, m_baseScope, m_orgScope);
	}

	const int pos = m_orgScope.Find('-');
	if (-1 != pos)
	{
		// [case: 3430] new method should use current static/const
		const WTString fnScope(m_orgScope.Left(pos));
		DType* fnData = m_mp->FindExact2(fnScope);
		if (fnData && fnData->MaskedType() == FUNC)
		{
			WTString tmp(fnData->Def());
			tmp.TrimRight();
			// if calling scope is static, make extract static
			if (::strstrWholeWord(tmp, "static"))
				mIsStatic = true;
			// if calling scope is const, make extract const
			else if (tmp.EndsWith("const{...}") || tmp.EndsWith("const"))
				mMethodQualifier = "const";
		}
	}

	WTString code = m_baseScope.Mid(1) + ":NewMethod" + itos(mMethodBody.GetLength()) + "(){"; // not lang safe
	code.ReplaceAll(":", "::");
	code += mMethodBody;
	m_addedSymbols = 0;
	m_parseTo = code.GetLength();

	// reparse once to define local variables
	Init(code);
	m_Scoping = FALSE;
	m_startReparseLine = 1;
	DoParse();

	// Make sure the selected text all matching braces
	if (m_deep != 1 || InComment())
		return FALSE;

	// now look for undefined variables that need to be passed as args
	Init(code);
	m_Scoping = TRUE;
	m_startReparseLine = 0;
	DoParse();
	return TRUE;
}

WTString GetCommonLeadingWhitespace(const WTString& text)
{
	WTString commonLeadingSpace, curLineLeadingSpace, curLine;
	token2 lines(text);
	while (lines.more())
	{
		curLine = lines.read("\r\n");
		int len = curLine.GetLength();
		if (0 == len)
			continue;

		curLineLeadingSpace.Empty();
		for (int idx = 0; idx < len; ++idx)
		{
			char ch = curLine[idx];
			if (strchr(" \t", ch))
				curLineLeadingSpace += ch;
			else
				break;
		}

		if (commonLeadingSpace == curLineLeadingSpace)
			continue;

		if (commonLeadingSpace.GetLength() < curLineLeadingSpace.GetLength())
		{
			if (commonLeadingSpace.IsEmpty())
				commonLeadingSpace = curLineLeadingSpace;
			//			else if (0 != curLineLeadingSpace.Find(commonLeadingSpace))
			//				_asm nop; // cur line has different whitespace than commonLeadingSpace, ignore?

			continue;
		}

		if (commonLeadingSpace.GetLength() > curLineLeadingSpace.GetLength())
		{
			if (curLineLeadingSpace.IsEmpty())
			{
				commonLeadingSpace = curLineLeadingSpace;
				break;
			}
			//			else if (0 != commonLeadingSpace.Find(curLineLeadingSpace))
			//				_asm nop; // cur line has different whitespace than commonLeadingSpace, ignore?

			commonLeadingSpace = curLineLeadingSpace;
			continue;
		}

		_ASSERTE(commonLeadingSpace.GetLength() == curLineLeadingSpace.GetLength());
		// same length, different contents -- spaces vs tabs?
		// ignored -- user needs to handle mixed spaces/tabs
	}

	return commonLeadingSpace;
}

int GetActiveTabSize()
{
	int tabsize = 0;
	if (g_IdeSettings)
	{
		if (IsCFile(gTypingDevLang))
		{
#ifdef AVR_STUDIO
			tabsize = g_IdeSettings->GetEditorIntOption("GCC", "TabSize");
#else
			tabsize = g_IdeSettings->GetEditorIntOption("C/C++", "TabSize");
#endif
		}
		else if (CS == gTypingDevLang)
			tabsize = g_IdeSettings->GetEditorIntOption("CSharp", "TabSize");
	}

	if (!tabsize && Psettings)
		tabsize = (int)Psettings->TabSize;

	if (!tabsize)
		tabsize = 4;

	return tabsize;
}

WTString NormalizeCommonLeadingWhitespace(const WTString& text, const WTString& requiredSpaceIn)
{
	_ASSERTE(-1 == text.Find('\r'));
	const WTString commonLeadingWhitespace(GetCommonLeadingWhitespace(text));
	WTString requiredSpace(requiredSpaceIn);
	if (!requiredSpace.IsEmpty() && commonLeadingWhitespace.GetLength() > 1)
	{
		// attempt to convert tabs in default snippets to spaces
		if (commonLeadingWhitespace[0] == ' ' && requiredSpace[0] == '\t')
		{
			const int tabsize = GetActiveTabSize();
			const WTString spaces(' ', tabsize);
			requiredSpace.ReplaceAll("\t", spaces);
		}
	}

	WTString curLine, newText;
	token2 lines(text);
	bool completedFirstPass = false;
	while (lines.more())
	{
		if (completedFirstPass)
			newText += "\n";
		else
			completedFirstPass = true;

		curLine = lines.read2("\n");
		int pos = curLine.find_first_not_of("\n");
		if (-1 == pos)
		{
			// empty line
			newText += curLine;
			continue;
		}

		newText += requiredSpace;

		// remove commonLeadingWhitespace from curLine
		pos = curLine.Find(commonLeadingWhitespace);
		if (0 == pos)
			curLine = curLine.Mid(commonLeadingWhitespace.GetLength());
		newText += curLine;
	}

	return newText;
}

void SubstituteMethodBody(WTString& implCode, WTString methodBody)
{
	// OutputDebugString(WTString("implcode:\r\n") + implCode + "\r\n");
	// OutputDebugString(WTString("methodBody:\r\n") + methodBody + "\r\n");
	const LPCTSTR kMethodBodyTag = _T("$MethodBody$");
	int tagPos = implCode.Find(kMethodBodyTag);
	if (-1 == tagPos)
		return;

	// this function assumes only one occurrence of kMethodBodyTag
	_ASSERTE(-1 == implCode.Find(kMethodBodyTag, tagPos + 1));
	const WTString implCodelnBrk(EolTypes::GetEolStr(implCode));
	const WTString bodyLnBrk(EolTypes::GetEolStr(methodBody));
	if (bodyLnBrk != "\n")
		methodBody.ReplaceAll(bodyLnBrk, "\n");

	int tagLineStartPos = tagPos;
	for (; tagLineStartPos; --tagLineStartPos)
	{
		if (strchr("\r\n", implCode[tagLineStartPos]))
			break;
	}

	if (tagLineStartPos != tagPos)
		++tagLineStartPos;

	WTString preTagChars = implCode.Mid(tagLineStartPos, tagPos - tagLineStartPos);
	const int endTagWasPresentInPreBodyTagChars = preTagChars.ReplaceAll("$end$", "");
	if (endTagWasPresentInPreBodyTagChars)
	{
		int posOfEndTag = implCode.Find("$end$", tagLineStartPos);
		_ASSERTE(posOfEndTag != -1);
		if (-1 != posOfEndTag && posOfEndTag < tagPos)
		{
			implCode.ReplaceAt(posOfEndTag, 5, "");
			tagPos -= 5;
		}
	}

	WTString requiredSpacing;
	for (int idx = tagPos - 1; idx >= 0; --idx)
	{
		char ch = implCode[idx];
		if (strchr(" \t", ch))
		{
			// move space before $MethodBody$ from implCode to requiredSpacing
			requiredSpacing += ch;
			implCode.ReplaceAt(idx, 1, "");
		}
		else
			break;
	}

	WTString encodedBody(EncodeUserText(methodBody));
	encodedBody = NormalizeCommonLeadingWhitespace(encodedBody, requiredSpacing);
	if (endTagWasPresentInPreBodyTagChars)
		encodedBody.prepend("$end$");

	if (implCodelnBrk != "\n")
		encodedBody.ReplaceAll("\n", implCodelnBrk);

	implCode.ReplaceAll(kMethodBodyTag, encodedBody);
	//	OutputDebugString(WTString("gen'd:\r\n") + implCode + "\r\n");
}

WTString MethodExtractor::BuildImplementation(const WTString& methodParameterList)
{
	_ASSERTE(mAutotextItemTitle.GetLength());
	WTString implCode(gAutotextMgr->GetSource(mAutotextItemTitle));
	const WTString implCodelnBrk(EolTypes::GetEolStr(implCode));
	WTString methodType(mMethodReturnType);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (mIsClassmethod) // [case: 135862]
		methodType = "__classmethod " + methodType;
#endif
	if (mIsStatic)
		methodType = "static " + methodType;

	implCode.ReplaceAll("$SymbolPrivileges$", ::GetDefaultVisibilityForSnippetSubst(GetLangType()));
	implCode.ReplaceAll("$SymbolType$", methodType);
	implCode.ReplaceAll("$SymbolContext$", mMethodName);
	implCode.ReplaceAll("$SymbolName$", mMethodName);
	implCode.ReplaceAll("$ParameterList$", methodParameterList);
	if (mMethodQualifier.IsEmpty())
		implCode.ReplaceAll(" $MethodQualifier$", mMethodQualifier);
	else
		implCode.ReplaceAll("$MethodQualifier$", mMethodQualifier);
	::SubstituteMethodBody(implCode, mMethodBody);
	implCode.ReplaceAll("(  )", "()");

	if (TERCOL(g_currentEdCnt->CurPos()) > 1)
		implCode = implCodelnBrk + implCode; // eof needs extra CRLF

	return implCode;
}

bool MethodExtractor::IsMember(DType* data)
{
	WTString reducedScope = GetReducedScope(data->SymScope());
	if (reducedScope.GetLength() == 0)
		return false;

	reducedScope = GetCleanScope(reducedScope);
	_ASSERTE(m_mp);
	DType* obj = m_mp->FindExact(reducedScope);
	if (obj && (obj->MaskedType() == CLASS || obj->MaskedType() == STRUCT))
		return true;

	return false;
}

// finds the outermost class or function. if finds a function, returns false.
bool MethodExtractor::FindOutermostClass(WTString scope, WTString& outermost)
{
	_ASSERTE(m_mp);
	if (!m_mp)
		return false; // shouldn't happen

	WTString res;
	bool ret = true;

	scope = GetCleanScope(scope);

	// iterating through the scope: classes, structs and namespaces
	for (int i = scope.ReverseFind(":"); i >= 0; i = scope.ReverseFind(":"))
	{
		DType* data = m_mp->FindExact(scope);
		if (data)
		{ // cannot find DType for function_name:class_name style scopes
			// store if class
			if (data->MaskedType() == CLASS || data->MaskedType() == STRUCT)
				res = scope;
			if (data->IsMethod())
			{
				res = scope;
				ret = false;
			}
		}

		// cut the last item
		scope = scope.Left(i);
	}

	_ASSERTE(res.GetLength());
	outermost = res;
	return ret;
}

BOOL MethodExtractor::MoveCaret(const WTString& jumpName, const WTString& jumpScope, UINT type)
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed && !jumpName.IsEmpty())
	{
		const uint kCurPos = ed->CurPos();
		const ULONG kUserAtLine = TERROW(kCurPos);
		WTString fileText(ed->GetBuf(TRUE));
		MultiParsePtr mparse = ed->GetParseDb();
		LineMarkers markers; // outline data
		GetFileOutline(fileText, markers, mparse);

		LineMarkerPath pathForUserLine; // path to caret
		markers.CreateMarkerPath(kUserAtLine, pathForUserLine, false, true);
		for (size_t i = 0; i < pathForUserLine.size(); i++)
		{
			if (pathForUserLine[i].mText.Find(jumpName.Wide()) != -1)
			{
				return !!DelayFileOpen(mTargetFile, (int)pathForUserLine[i].mStartLine).get();
			}
		}
	}

	return ::GotoDeclPos(jumpScope, mTargetFile, type);
}

std::pair<BOOL, BOOL> MethodExtractor::GetExtractionOptions()
{
	BOOL canExtractAsFreeFunction =
	    FALSE; // don't need free function checkbox when extracting in non-member functions
	if (!m_baseScope.IsEmpty() && (g_currentEdCnt->m_ftype == Src || g_currentEdCnt->m_ftype == Header))
	{
		DType* data = m_mp->FindExact(m_baseScope);
		if (data)
			canExtractAsFreeFunction = data->MaskedType() == CLASS || data->MaskedType() == STRUCT;
	}

	BOOL canExtractToSource =
	    (g_currentEdCnt->m_ftype == Src ||
	     (g_currentEdCnt->m_ftype == Header && !GetFileByType(g_currentEdCnt->FileName(), Src).IsEmpty()));
	if (canExtractToSource && Src == g_currentEdCnt->m_ftype && ::GetFileByType(mTargetFile, Src))
	{
		// [case: 63161] don't offer extract to source in .c files
		if (::IsCfile(mTargetFile))
			canExtractToSource = FALSE;
		// [case: 87859] don't offer extract to source on free functions (global or in namespace)
		if (!canExtractAsFreeFunction)
			canExtractToSource = FALSE;
	}

	return std::make_pair(canExtractToSource, canExtractAsFreeFunction);
}

void MethodExtractor::GetJumpInfo(WTString& jumpName, WTString& jumpScope, WTString& freeFuncMoveImplScope, UINT& type, const ExtractMethodDlg& methDlg, BOOL canExtractAsFreeFunction)
{
	jumpScope = ::TokenGetField(m_orgScope, "-");
	if (methDlg.m_extractAsFreeFunction)
	{
		WTString cutScope = m_orgScope;
		WTString methodName = GetInnerScopeName(cutScope);
		DType* sym = m_mp->FindSym(&methodName, &m_baseScope, &m_baseScopeBCL);
		bool treatAsFreeFunc = sym == nullptr; // [case: 140886] extract in lambda where calling scope is unknown?
		if (sym && sym->Scope().IsEmpty())
		{
			// [case: 140886]
			// methodName is a global function.
			// do not make member of class since user wants free function per methDlg.m_extractAsFreeFunction
			treatAsFreeFunc = true;
		}

		while (sym && !sym->IsMethod())
		{ // cutting in-method stuff like "for", "if", etc. from the end of the scope
			WTString newScope = GetReducedScope(cutScope);
			if (newScope == cutScope)
				break;
			cutScope = newScope;
			methodName = GetInnerScopeName(cutScope);
			sym = m_mp->FindSym(&methodName, &m_baseScope, &m_baseScopeBCL);
		}

		if (treatAsFreeFunc || (sym && sym->IsMethod()))
		{ // must be func or method
			VARefactorCls ref((m_baseScope + DB_SEP_CHR + mMethodName).c_str());
			WTString outermostClassOrFunc;
			bool isOutermostClass = FindOutermostClass(m_baseScope, outermostClassOrFunc);
			if (treatAsFreeFunc || !isOutermostClass ||
			    (sym && ref.CanMoveImplementationToSrcFile(sym, m_baseScope, FALSE)))
			{ // inside a class definition
				if (isOutermostClass)
					freeFuncMoveImplScope = GetReducedScope(
					    outermostClassOrFunc); // remove the last scope item (the class), so we get either an empty
					                           // string or the namespace(s) for qualification
				jumpScope = outermostClassOrFunc;
				jumpName = GetInnerScopeName(jumpScope);
			}
			else
			{
				jumpName = methodName;
			}
			mMethodParameterList = mMethodParameterList_Free;
			mMethodInvocation = mMethodInvocation_Free;
			mMethodReturnType = mMethodReturnType_Free;
			mMethodBody = mMethodBody_Free;
			mIsStatic = false;
		}

		mTargetFile = g_currentEdCnt->FileName();
	}
	else
	{
		// case 87859
		if (!canExtractAsFreeFunction)
			jumpName = GetInnerScopeName(jumpScope);
	}

	mMethodName = methDlg.m_newName;
	mMethodInvocation.ReplaceAll(methDlg.m_orgName, mMethodName, TRUE);

	type =
	    ::StrGetSymScope(::TokenGetField(m_orgScope, "-")).GetLength() || methDlg.m_extractAsFreeFunction
	        ? FUNC
	        : BEFORE_SYMDEF_FLAG;
}


WTString MethodExtractor::CreateTempMethodSignature(const WTString& methodParameterList, const WTString& methodRetType,
                                                    bool canBeStatic)
{
	WTString tmpMethodSignature(methodRetType + " " + mMethodName + "(" + methodParameterList + ")");
	if (mIsStatic && canBeStatic)
		tmpMethodSignature = "static " + tmpMethodSignature;
	if (mMethodQualifier.GetLength())
		tmpMethodSignature += " " + mMethodQualifier;

	return tmpMethodSignature;
}

void FinishMethodExtractorParams::Load(std::shared_ptr<UndoContext> undoContext, const WTString& implcode, const WTString& freeFuncMoveImplScope, const WTString& baseScope, const WTString& methodName, bool exToSrc, bool exAsFree)
{
	mUndoContext = undoContext;
	mImplCode = implcode;
	mFreeFuncMoveImplScope = freeFuncMoveImplScope;
	mBaseScope = baseScope;
	mMethodName = methodName;
	mExtractToSrc = exToSrc;
	mExtractAsFreeFunction = exAsFree;
}

void FinishMethodExtractorParams::Clear()
{
	mUndoContext = nullptr;
	mImplCode.Empty();
	mFreeFuncMoveImplScope.Empty();
	mBaseScope.Empty();
	mMethodName.Empty();
}
