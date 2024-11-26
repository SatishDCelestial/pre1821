#include "stdafxed.h"
#include "EnumConverter.h"
#include "EDCNT.H"
#include "CreateMissingCases.h"
#include "FreezeDisplay.h"
#include "UndoContext.h"
#include "CommentSkipper.h"
#include "InferType.h"
#include "VAParse.h"
#include "VARefactor.h"
#include "VAAutomation.h"
#include "RegKeys.h"
#include "IntroduceVariable.h"
#include "DBQuery.h"
#include "DefListFilter.h"

bool EnumConverter::CanConvert()
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		if (ScopeInfoPtr scopeInfoPtr = ed->ScopeInfoPtr())
		{
			if (!scopeInfoPtr->m_isDef)
				return false;

			if (DTypePtr cwData = scopeInfoPtr->GetCwData())
			{
				uint maskedType = cwData->MaskedType();
				if (maskedType == C_ENUM)
				{
					bool already =
					    strstrWholeWord(cwData->Def(), "class", TRUE) || strstrWholeWord(cwData->Def(), "struct", TRUE);

					// enum and enum class both has the same maskedType C_ENUM - let's see whether the word before the
					// enum name (where the caret is on) is a "class"
					if (scopeInfoPtr->m_LastWord != "class" && scopeInfoPtr->m_LastWord != "struct" &&
					    !already) // we don't offer conversation to enum class on enum class
						return true;
				}
			}
		}
	}

	return false;
}

WTString EnumConverter::GetUnderlyingType(WTString def)
{
	const WTString defaultType = "int";

	// let's see if we can find a ':' before we reach the opening brace '{'
	CommentSkipper cs(Src);
	int counter = 0;
	for (int i = 0; i < def.GetLength(); i++)
	{
		if (counter++ > 4096)
			return defaultType;

		TCHAR c = def[i];
		if (cs.IsCode(c))
		{
			if (c == '{')
				return defaultType;
			if (c == ':')
			{
				cs.Reset();
				counter = 0;
				WTString underlyingType;
				for (int j = i + 1; j < def.GetLength(); j++)
				{
					TCHAR c2 = def[j];
					if (cs.IsCode(c2))
					{
						if (c2 == '{')
						{
							underlyingType.Trim();
							if (underlyingType.Left(5) == "const")
							{
								underlyingType = underlyingType.Mid(5);
								underlyingType.TrimLeft();
							}
							if (underlyingType.Left(8) == "volatile")
							{
								underlyingType = underlyingType.Mid(5);
								underlyingType.TrimLeft();
							}

							if (underlyingType.IsEmpty())
								return defaultType;
							else
								return underlyingType;
						}

						underlyingType += c2;
					}
				}
			}
		}
	}

	return defaultType;
}

bool EnumConverter::Convert(DType* sym)
{
	if (sym == nullptr)
		return false;

	UndoContext undoContext("Convert Unscoped Enum to Scoped Enum");

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;
	if (MultiParsePtr mp = ed->GetParseDb())
	{
		if (DTypePtr cwData = mp->GetCwData())
		{
			std::vector<WTString> enumNames;
			GetEnumItemNames(mp, cwData.get(), enumNames);
			if (enumNames.empty())
			{
				EnumConverter::InsertClass();
				if (gTestLogger)
					gTestLogger->LogStrW(L"EnumConverter error: empty enum");
				else
					WtMessageBox("Convert Unscoped Enum to Scoped Enum: no enumerators defined.", IDS_APPNAME,
					             MB_OK | MB_ICONINFORMATION);
				return false;
			}
			std::set<WTString> elements;
			for (uint i = 0; i < enumNames.size(); i++)
				elements.insert(enumNames[i]);

			WTString symScope = sym->SymScope();

			// We're trying to find in the scope of :MyEnum:MyEnumeratorX, so we emulate the same syntax.
			// This way, the depth of the scope will be the same.
			// Also, any code that want this enum item to be real (existing), will be happy.
			// However, when Find References tries to compare if the enum item is the same, we skip the comparison since
			// any of the enum items are valid.
			symScope += ":" + enumNames[0];

			WTString enumName = sym->Sym();
			WTString underlyingType = GetUnderlyingType(sym->Def());

			if (DoModal(symScope, enumName, underlyingType, elements))
			{
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}

BOOL EnumConverter::DoModal(WTString symScope, const WTString& enumName, const WTString& underlyingType,
                            const std::set<WTString>& elements)
{
	EnumConverterDlg dlg;
	if (!dlg.Init(symScope, enumName, underlyingType, elements))
		return FALSE;

	dlg.DoModal();
	dlg.Me = nullptr;
	if (dlg.IsStarted())
	{
		InsertClass();
	}
	return dlg.IsStarted(); // dlg.DoModal() always returns IDCANCEL (2)
}

void EnumConverter::InsertClass()
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		WTString buf = ed->GetBuf(TRUE);
		long curPos = ed->GetBufIndex(buf, (long)ed->CurPos());
		while (curPos > 0 && ISCSYM(buf[size_t(curPos - 1)]))
			curPos--;
		ed->SetSel(curPos, curPos);
		ed->InsertW(L"class ", false);
	}
}

EnumConverterDlg::EnumConverterDlg()
    : UpdateReferencesDlg("EnumConverterDlg", IDD_CONVERTENUM, NULL, Psettings->mIncludeProjectNodeInRenameResults,
                          true)
{
	SetHelpTopic("dlgEnumConverter");
	mColourize = true;
	Me = this;
}

EnumConverterDlg::~EnumConverterDlg()
{
	Me = nullptr;
}

EnumConverterDlg* EnumConverterDlg::Me = nullptr;

BOOL EnumConverterDlg::Init(WTString symScope, const WTString& enumName, const WTString& underlyingType,
                            const std::set<WTString>& elements)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	m_symScope = symScope;
	EnumName = enumName;
	UnderlyingType = underlyingType;
	mRefs->flags |= FREF_Flg_Convert_Enum;
	mRefs->elements = elements;

	return TRUE;
}

WTString SimplifiedInfer(WTString def)
{
	if (def.Left(6) == "static")
	{
		def = def.Mid(6);
		def.TrimLeft();
	}

	for (int i = 0; i < def.length(); i++)
	{
		TCHAR c = def[i];
		if (!ISCSYM(c) && c != ':')
			return def.Left(i);
	}

	return def;
}

class ParamCounter
{
  public:
	ParamCounter(const WTString& buf, ULONG startPos) : Buf(buf), StartPos(startPos)
	{
	}
	virtual ~ParamCounter() = default;

	void Parse();
	bool ShouldConsider(TCHAR c);
	virtual bool IsDone(ULONG pos, TCHAR c) = 0;
	virtual bool OnNextParam(ULONG pos) = 0;

	const WTString& Buf;
	ULONG StartPos = 0;
	int ParenCount = 0;
	int BraceCount = 0;
	int AngleCount = 0;
	int ParamCount = -1;
	int OuterParenCount = 0;
};

void ParamCounter::Parse()
{
	bool FirstParam = true;
	CommentSkipper cs(Src);
	for (ULONG i = StartPos; i < (ULONG)Buf.length(); i++)
	{
		TCHAR c = Buf[(size_t)i];

		if (cs.IsCode(c))
		{
			if (ShouldConsider(c))
			{
				if (IsDone(i, c))
					return;

				if (FirstParam)
				{
					FirstParam = false;
					if (OnNextParam(i))
						return;
					continue;
				}
				if (c == '(')
				{
					ParenCount++;
					continue;
				}
				if (c == ')')
				{
					if (ParenCount > 0)
						ParenCount--;
					continue;
				}
				if (c == '{')
				{
					BraceCount++;
					continue;
				}
				if (c == '}')
				{
					if (BraceCount > 0)
						BraceCount--;
					continue;
				}
				if (c == '<')
				{
					AngleCount++;
					continue;
				}
				if (c == '>' && (int)i - 1 >= 0 && Buf[(size_t)i - 1] != '-')
				{
					if (AngleCount > 0)
						AngleCount--;
					continue;
				}

				if (c == ',' && ParenCount == 0 && BraceCount == 0 && AngleCount == 0)
					if (OnNextParam(i))
						return;
			}
		}
	}
}

bool ParamCounter::ShouldConsider(TCHAR c)
{
	if (c == '(')
		OuterParenCount++;

	return OuterParenCount > 0;
}

class WhichParam : public ParamCounter
{
  public:
	WhichParam(const WTString& buf, ULONG startPos) : ParamCounter(buf, startPos)
	{
	}

	bool IsDone(ULONG pos, TCHAR c) override;
	virtual bool OnNextParam(ULONG pos) override;
	int GetParamIndex(ULONG bufIndex);

	ULONG BufIndex = 0;
	int Res = -1;

	int PanicCounter = 0;
};

bool WhichParam::IsDone(ULONG pos, TCHAR c)
{
	if (c == ')')
		OuterParenCount--;

	if (c == ')' && ParenCount == 0)
	{
		if (BufIndex <= pos)
		{
			Res = ParamCount;
		}
		return true;
	}

	if (PanicCounter++ > 4096)
		return true;

	return false;
}

bool WhichParam::OnNextParam(ULONG pos)
{
	ParamCount++;
	if (BufIndex <= pos)
	{
		Res = ParamCount - 1;
		return true;
	}

	return false;
}

int WhichParam::GetParamIndex(ULONG bufIndex)
{
	BufIndex = bufIndex;

	Parse();

	return Res;
}

class ParamType : public ParamCounter
{
  public:
	ParamType(const WTString& buf, ULONG startPos) : ParamCounter(buf, startPos)
	{
	}

	bool IsDone(ULONG pos, TCHAR c) override;
	WTString GetParamType(int paramIndex);

	bool Considering = false;

	WTString Res;
	int ParamIndex = -1;

	virtual bool OnNextParam(ULONG pos) override;
};

bool ParamType::IsDone(ULONG pos, TCHAR c)
{
	if (c == ')')
		OuterParenCount--;

	return OuterParenCount == 0;
}

WTString ParamType::GetParamType(int paramIndex)
{
	ParamIndex = paramIndex;

	Parse();

	return Res;
}

bool ParamType::OnNextParam(ULONG pos)
{
	ParamCount++;

	if (ParamCount == ParamIndex)
	{
		std::pair<WTString, int> res = GetNextWord(Buf, int(pos + 1));
		Res = res.first;
		return true;
	}

	return false;
}

bool IsOverLoaded(WTString sym, uint scopeHash)
{
	std::vector<int> lines;
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return true;

	MultiParsePtr mp = ed->GetParseDb();
	if (mp)
	{
		WTString bcl = mp->GetBaseClassList(mp->m_baseClass);

		DBQuery query(mp);
		query.FindExactList(sym, scopeHash);

		// collecting enum items in enumNames array
		for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
		{
			if (dt->type() != FUNC)
				continue;

			lines.push_back(dt->Line());
		}

		// sorting to be able to remove duplicates
		std::sort(lines.begin(), lines.end(), [](int a, int b) { return a < b; });

		// removing duplicates
		for (uint i = 1; i < lines.size(); i++)
		{
			if (lines[i - 1] == lines[i])
			{
				lines.erase(lines.begin() + (int)i);
				i--;
			}
		}
	}

	return lines.size() > 1;
}

const char ec_operators[] = {'+', '-', '*', '/', '%', '<', '>', '&', '|', '^', '~', '[', ']', 0};

bool IsSimpleType(WTString type)
{
	type = TokenGetField(type, " ");
	return TypeListComparer::IsPrimitiveCType(type);
}

UpdateReferencesDlg::UpdateResult EnumConverterDlg::UpdateReference(int refIdx, FreezeDisplay& _f)
{
	// our reference to work with
	FindReference* curRef = mRefs->GetReference((size_t)refIdx);

	// error handling
	if (curRef->mData && curRef->mData->IsMethod())
		return rrSuccess; // nothing to do here
	if (!mRefs->GotoReference(refIdx))
		return rrError;
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	if (FirstFile || LastFileID != curRef->fileId)
	{
		FirstFile = false;
		LastFileID = curRef->fileId;
		void ReparseAndWaitIfNeeded(bool forcedOperation);
		ReparseAndWaitIfNeeded(true);
	}

	_f.ReadOnlyCheck();
	if (curRef->type == FREF_Unknown || curRef->type == FREF_Comment ||
	    curRef->type == FREF_IncludeDirective /*|| curRef->type == FREF_Definition*/)
		return rrNoChange;
	if (!IsCFile(ed->m_ftype))
		return rrNoChange;

	WTString buf = ed->GetBuf(TRUE);
	FindReference* ref = mRefs->GetReference((size_t)refIdx);
	int bufIndex = ed->GetBufIndex(buf, (long)ref->GetPos());

	// are we inside a parameter-list of a function
	bool needCast = false;
	MultiParsePtr mp = ed->GetParseDb();
	WTString scope;
	int funcPos = bufIndex;
	DTypePtr prevTypePtr = PrevDepthSymFromPos(buf, mp, funcPos, scope);
	uint prevType = UNDEF;
	if (prevTypePtr)
	{
		prevType = prevTypePtr->MaskedType();
		if (prevType == FUNC)
		{
			CommentSkipper cs(Src);
			for (int i = funcPos; i < bufIndex; i++)
			{
				TCHAR c = buf[i];
				if (cs.IsCode(c))
				{
					if (c == '{')
					{
						prevType = NOTFOUND;
						break;
					}
				}
			}
		}

		if (prevType == FUNC)
		{
			if (IsOverLoaded(prevTypePtr->Sym(), prevTypePtr->ScopeHash()))
				goto overloaded;

			// get the type of the parameter, using the function's definition list
			WhichParam wp(buf, (ULONG)funcPos);
			int paramIndex = wp.GetParamIndex((ULONG)bufIndex);

			if (paramIndex != -1)
			{
				WTString def = prevTypePtr->Def();
				ParamType pt(def, 0);
				WTString type = pt.GetParamType(paramIndex);

				if (/*type != EnumName && */ IsSimpleType(type))
					needCast = true;
			}
		}
	}

overloaded:

	// new enum item detection method
	if (prevType == C_ENUM)
		return rrNoChange;

	// look for troublemaker characters AFTER the enum name - if we found one, we need casting. Typical example is '+'.
	CommentSkipper cs(Src);
	int counter = 0;
	auto CheckIfCastNeededDueToOperator = [&needCast, &buf](int i) {
		for (int j = 0; ec_operators[j] != 0; j++)
		{
			if (buf[i] == ec_operators[j] && i + 1 < buf.GetLength() && buf[i + 1] != '&' && buf[i + 1] != '|' &&
			    buf[i - 1] != '&' &&
			    buf[i - 1] != '|') // the & and | is for the second part of VAAutoTest:ConvertEnum14
			{
				needCast = true;
				return;
			}
		}
	};

	for (int i = bufIndex; i < buf.GetLength() - 1; i++)
	{
		if (counter++ > 4096)
			break;

		TCHAR c = buf[i];
		if (cs.IsCode(c))
		{
			if (c == '/' && (buf[i + 1] == '/' || buf[i + 1] == '*'))
				continue;

			if (ISCSYM(c) || IsWSorContinuation(c))
				continue;

			CheckIfCastNeededDueToOperator(i);

			break;
		}
	}

	if (!needCast)
	{
		// look for troublemaker characters BEFORE the enum name - if we found one, we need casting. Typical example is
		// '+'.
		cs.Reset();
		counter = 0;
		for (int i = bufIndex; i > 1; i--)
		{
			if (counter++ > 4096)
				break;

			TCHAR c = buf[i];
			if (cs.IsCode(c))
			{
				if (ISCSYM(c) || IsWSorContinuation(c) || c == ':' || c == '.' ||
				    (c == '>' &&
				     buf[i - 1] ==
				         '-')) // skipping any qualification we may already have. e.g. class or namespace name, etc
					continue;

				CheckIfCastNeededDueToOperator(i);

				break;
			}
		}
	}

	// find the word before caret pos
	// the enum item can already be qualified by enum name. it's not necessary but it compiles, so we need to avoid
	// double qualification
	cs.Reset();
	counter = 0;
	WTString wordBefore;
	[&] {
		for (int i = bufIndex - 1; i >= 0; i--)
		{
			if (counter++ > 4096)
				break;

			TCHAR c = buf[i];
			if (cs.IsCode(c))
			{
				if (IsWSorContinuation(c) || c == ':')
					continue;

				for (int j = i; j > i - 128 && j > 0; j--)
				{
					TCHAR c2 = buf[j];
					if (!ISCSYM(c2))
						return; // jumping out of the lambda, thus breaking both fors
					wordBefore = c2 + wordBefore;
				}
			}
		}
	}();

	bool constuctorInitialization = false;
	if (!needCast)
	{
		// check if we're assigning it to a variable with a type other than the enum itself - if so, we need casting
		enum class defMode
		{
			none,
			foundDef,
			foundName,
			searchingType,
		};
		defMode dm = defMode::none;
		cs.Reset();
		counter = 0;
		WTString symType;
		WTString objName;
		int objPos = 0;
		for (int i = bufIndex - 1; i > 0; i--)
		{
			if (counter++ > 4096)
				break;

			TCHAR c = buf[i];
			if (cs.IsCode(c))
			{
				if (c == '{' || c == ';' || c == '}')
					break;

				switch (dm)
				{
				case defMode::none:
					if (IsWSorContinuation(c) || ISCSYM(c) || c == ':' || c == '.' || (c == '>' && buf[i - 1] == '-'))
						continue;
					if (c == '=' || c == '(') // assignment or constructor initialization. TODO: needs to distinguish
					                          // between function calls and constructors but for that, we will need a
					                          // consistent result for findsym. try findallsym?
					{
						if (c == '(')
							constuctorInitialization = true;
						dm = defMode::foundDef; // can be a definition, depending on what characters we find before this
						break;
					}
					goto out;
				case defMode::foundDef:
					if (IsWSorContinuation(c))
						continue;
					if (ISCSYM(c))
					{
						dm = defMode::foundName;
						objPos = i;
						objName = c;
						break;
					}
					goto out;
				case defMode::foundName:
					if (ISCSYM(c) || c == ':' || c == '.' || (c == '>' && buf[i - 1] == '-') ||
					    (c == '-' && buf[i + 1] == '>'))
					{
						// objPos = i;
						objName = c + objName;
						continue;
					}
					dm = defMode::searchingType;
					break;
				case defMode::searchingType:
					if (ISCSYM(c))
					{
						symType = c + symType;
						continue;
					}

					if (!symType.IsEmpty())
					{
						if (/*symType != EnumName*/ IsSimpleType(symType))
							needCast = true;
						goto out; // if we find the symbol type, we can avoid hitting the symbol database
					}

					MultiParsePtr mp2 = ed->GetParseDb();
					if (curRef->mData && mp2)
					{
						InferType infer;
						WTString bcl = mp2->GetBaseClassList(mp2->m_baseClass);

						WTString scope2;
						WTString typeName;

						DTypePtr t = SymFromPos(buf, mp2, objPos /*bufIndex*/, scope2);
						if (t && t->MaskedType() == FUNC)
							goto out;

						scope2 = MPGetScope(buf, mp2, bufIndex);

						if (t)
							typeName = SimplifiedInfer(t->Def());

						//							if (objName == "numerical")
						//								int a = 5;

						DTypePtr tTest = PrevDepthSymFromPos(buf, mp2, objPos, scope2);

						typeName = StrGetSym(typeName); // extract enum name if it's qualified
						if (!typeName.IsEmpty() && IsSimpleType(typeName) && typeName != "auto")
							needCast = true;
					}
					goto out;
				}
			}
		}
	}

out:
	CStringW prePrefix; // this may be inserted as a separate position compared to prefix due to namespaces
	CStringW prefix;
	CStringW postfix;
	if (needCast)
	{
		if (constuctorInitialization)
		{
			prePrefix = L"(" + UnderlyingType.Wide() + L")";
		}
		else
		{
			prePrefix = UnderlyingType.Wide() + L"(";
			postfix = L")";
		}
	}
	if (wordBefore != EnumName)
		prefix += EnumName.Wide() + CStringW(L"::");

	// find prePrefix post
	cs.Reset();
	counter = 0;
	int prePrefixPos = 0;
	for (int i = bufIndex; i > 0; i--)
	{
		if (counter++ > 128)
			return rrNoChange;

		TCHAR c = buf[i];
		if (cs.IsCodeBackward(buf, i))
		{
			if (ISCSYM(c) || c == ':')
			{
				prePrefixPos = i;
				continue;
			}

			goto findPostfix;
		}
	}

	return rrNoChange;

findPostfix:
	// find postfix pos
	cs.Reset();
	counter = 0;
	int postfixPos = 0;
	for (int i = bufIndex; i < buf.GetLength(); i++)
	{
		if (counter++ > 128)
			return rrNoChange;

		TCHAR c = buf[i];
		if (cs.IsCode(c))
		{
			if (ISCSYM(c))
				continue;

			postfixPos = i;
			goto insertPostfix;
		}
	}

	return rrNoChange;

insertPostfix:
	// insert postfix
	if (!postfix.IsEmpty())
	{
		ed->SetSel((long)postfixPos, (long)postfixPos);
		ed->InsertW(postfix, false);
	}

	// insert prefix
	ed->SetSel((long)bufIndex, (long)bufIndex);
	ed->InsertW(prefix, false);

	if (!prePrefix.IsEmpty())
	{
		ed->SetSel((long)prePrefixPos, (long)prePrefixPos);
		ed->InsertW(prePrefix, false);
	}

	return rrSuccess;
}

void EnumConverterDlg::UpdateStatus(BOOL done, int fileCount)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	WTString msg;
	if (!done)
	{
		WTString enumName = StrGetSymScope(mRefs->GetFindScope()); // we strip the enum item to get the enum name
		enumName = StrGetSym(enumName); // we only keep the enum name in case we're inside namespace(s)
		if (enumName.GetLength() && enumName[0] == ':')
			enumName = enumName.Mid(1); // removing the leading ':' character for cosmetic reasons
		msg.WTFormat("Searching for references in %s to enumerators of %s that may need to be updated...",
		             mRefs->GetScopeOfSearchStr().c_str(), enumName.c_str());
	}
	else if (mFindRefsThread && mFindRefsThread->IsStopped() && mRefs->Count())
		msg.WTFormat("Search canceled before completion.  U&pdate references to %s at your own risk.",
		             mRefs->GetFindSym().c_str());
	else
		msg.WTFormat("Select references to update.");

	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
}

BOOL EnumConverterDlg::OnUpdateStart()
{
	mStarted = true;

	return TRUE;
}

void EnumConverterDlg::ShouldNotBeEmpty()
{
}

void EnumConverterDlg::RegisterReferencesControlMovers()
{
	AddSzControl(IDCANCEL, mdRepos, mdRepos);
}

void EnumConverterDlg::RegisterRenameReferencesControlMovers()
{
	AddSzControl(IDC_RENAME, mdRepos, mdRepos);
}

BOOL EnumConverterDlg::OnInitDialog()
{
	__super::OnInitDialog();

	CTreeCtrl* tree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	if (tree)
		tree->SetFocus();

	return FALSE;
}

UINT_PTR FinishCloseDlg = 0;

static void CALLBACK CloseDlg(HWND hWnd = NULL, UINT ignore1 = 0, UINT_PTR idEvent = 0, DWORD ignore2 = 0)
{
	if (idEvent)
		KillTimer(hWnd, idEvent);

	if (EnumConverterDlg::Me)
		EnumConverterDlg::Me->EndDialog(0);
}

void EnumConverterDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
	__super::OnSearchComplete(fileCount, wasCanceled);

	if (fileCount == 0)
	{
		EnumConverter::InsertClass();
		if (gTestLogger)
			gTestLogger->LogStrW(L"EnumConverter error: no references");
		else
			WtMessageBox("Convert Unscoped Enum to Scoped Enum: no enumerator usages found.", IDS_APPNAME,
			             MB_OK | MB_ICONINFORMATION);
		FinishCloseDlg = ::SetTimer(nullptr, 0, 250u, (TIMERPROC)&CloseDlg);
	}
}
