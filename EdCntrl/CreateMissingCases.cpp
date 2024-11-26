#include "StdAfxEd.h"
#include "CreateMissingCases.h"
#include "EdCnt.h"
#include "VAAutomation.h"
#include "CommentSkipper.h"
#include <algorithm>
#include "InferType.h"
#include "DBQuery.h"
#include "AutotextManager.h"
#include "FreezeDisplay.h"
#include "StringUtils.h"
#include "RegKeys.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const bool moveCaret = true;
const bool quickCheck = true; // note: if you want to disable this, remove the error messages when this is false

bool IsEnumClass(const WTString& enumDef)
{
	CommentSkipper cs(Src);
	enum class state
	{
		NONE,
		ENUM,
		ENUM_WS,
		ENUM_WS_CLASS,
	};
	state state = state::NONE;
	for (int i = 0; i < enumDef.GetLength() - 5; i++)
	{ // the length of "class" is 5
		TCHAR c = enumDef[i];
		if (cs.IsCode(c))
		{
			if (enumDef[i] == 'e' && enumDef[i + 1] == 'n' && enumDef[i + 2] == 'u' && enumDef[i + 3] == 'm' &&
			    state == state::NONE)
			{
				state = state::ENUM;
				i += 3;
				continue;
			}
			if (enumDef[i] == 'c' && enumDef[i + 1] == 'l' && enumDef[i + 2] == 'a' && enumDef[i + 3] == 's' &&
			    enumDef[i + 4] == 's' && state == state::ENUM_WS)
			{
				state = state::ENUM_WS_CLASS;
				i += 4;
				break;
			}
			if (IsWSorContinuation(c) && state == state::ENUM)
				state = state::ENUM_WS;
			if (!IsWSorContinuation(c) && state < state::ENUM_WS_CLASS)
				state = state::NONE;
		}
	}

	return state == state::ENUM_WS_CLASS;
}

int FindTypeDefEnumName(const WTString& enumDef)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	enum class state
	{
		NONE,
		TYPEDEF,
		TYPEDEF_WS,
		TYPEDEF_WS_ENUM,
	};
	state state = state::NONE;
	for (int i = 0; i < enumDef.GetLength() - 7; i++)
	{ // the length of "typedef" is 7
		TCHAR c = enumDef[i];
		if (cs.IsCode(c))
		{
			if (enumDef[i] == 't' && enumDef[i + 1] == 'y' && enumDef[i + 2] == 'p' && enumDef[i + 3] == 'e' &&
			    enumDef[i + 4] == 'd' && enumDef[i + 5] == 'e' && enumDef[i + 6] == 'f' && state == state::NONE)
			{
				state = state::TYPEDEF;
				i += 6;
				continue;
			}
			if (enumDef[i] == 'e' && enumDef[i + 1] == 'n' && enumDef[i + 2] == 'u' && enumDef[i + 3] == 'm' &&
			    state == state::TYPEDEF_WS)
			{
				state = state::TYPEDEF_WS_ENUM;
				i += 3;
				continue;
				;
			}
			if (IsWSorContinuation(c) && state == state::TYPEDEF)
				state = state::TYPEDEF_WS;
			if (!IsWSorContinuation(c) && state < state::TYPEDEF_WS_ENUM)
				state = state::NONE;
			if (!IsWSorContinuation(c) && state == state::TYPEDEF_WS_ENUM)
				return i;
		}
	}

	return -1;
}

bool CreateMissingCases::Parse(EdCntPtr ed, std::vector<WTString>& switchLabels, int& endPos,
                               std::vector<WTString>& enumNames, WTString& labelQualification, bool& defLabel,
                               bool check)
{
	MultiParsePtr mp = ed->GetParseDb();
	WTString scope = ed->m_lastScope;
	WTString methodScope;
	DTypePtr method = GetMethod(mp.get(), GetReducedScope(scope), scope, mp->m_baseClassList, &methodScope,
	                            ed->LineFromChar((long)ed->CurPos()));
	if (method == nullptr)
		return true;

	WTString fileBuf;
	int curPos;
	int bracePos = FindOpeningBrace(method.get()->Line(), fileBuf, curPos);
	if (bracePos == -1)
	{
		if (gTestLogger && !check)
			gTestLogger->LogStrW(L"CreateMissingCases error: invalid brace pos");
		return true;
	}
	if (curPos < bracePos)
		return true;

	enum eState
	{
		NO,
		SWITCH,
		PARENS,
		BRACKETS,
	};

	// find trigger point
	CommentSkipper cs(ed->m_ftype);
	eState triggerZone = NO;
	long till = std::min(curPos + 7, fileBuf.GetLength());
	int openParens = 0;
	int openBrackets = 0;
	long switchPos = -1;
	for (int i = bracePos; i < till; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (c == '}' && triggerZone == BRACKETS)
			{
				openBrackets--;
				if (openBrackets == 0)
					triggerZone = NO;
				continue;
			}
			if (c == '{' && triggerZone >= PARENS)
			{
				openBrackets++;
				triggerZone = BRACKETS;
				continue;
			}
			if (c == 's' && fileBuf[i + 1] == 'w' && fileBuf[i + 2] == 'i' && fileBuf[i + 3] == 't' &&
			    fileBuf[i + 4] == 'c' && fileBuf[i + 5] == 'h' && IsStatementEndChar(fileBuf[i + 6]))
			{
				triggerZone = SWITCH;
				switchPos = i;
				i += 5;
				continue;
			}
			if (c == '(' && triggerZone == SWITCH)
			{
				openParens++;
				triggerZone = PARENS;
				continue;
			}
			if (c == ')' && triggerZone == PARENS)
			{
				openParens--;
				continue;
			}
			if (triggerZone == SWITCH && !IsWSorContinuation(c))
			{
				triggerZone = NO;
				continue;
			}
			if (triggerZone == PARENS && openParens == 0 && !IsWSorContinuation(c))
			{
				triggerZone = NO;
				continue;
			}

			if (i == curPos)
			{
				if (triggerZone != NO)
					break;
				else
					return true;
			}
		}
	}

	if (switchPos == -1)
		return true;

	// if we got here it means we've found the trigger zone and the caret is inside it
	if (check && quickCheck)
		return false;

	// read the case labels sans qualification and the part inside the switch's parens
	cs.Reset();
	openParens = 0;
	openBrackets = 0;
	eState state = SWITCH;
	int length = fileBuf.GetLength();
	endPos = -1;
	WTString varName;
	for (int i = switchPos; i < length; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (c == '(')
			{
				openParens++;
				if (state == SWITCH)
					state = PARENS;
				continue;
			}
			if (c == ')')
			{
				openParens--;
				if (openParens <= 0 && state == PARENS)
					state = BRACKETS;
			}
			if (openParens == 1 && state == PARENS)
				varName += c;
			if (c == '{')
				openBrackets++;
			if (c == '}')
			{
				openBrackets--;
				if (openBrackets <= 0 && state == BRACKETS)
				{
					endPos = i;
					break;
				}
			}
			if (i + 4 < length && c == 'c' && fileBuf[i + 1] == 'a' && fileBuf[i + 2] == 's' && fileBuf[i + 3] == 'e' &&
			    IsStatementEndChar(fileBuf[i + 4]))
			{
				CommentSkipper cs2(ed->m_ftype);
				WTString label;
				for (int j = i + 4; j < fileBuf.GetLength(); j++)
				{
					TCHAR c2 = fileBuf[j];
					if (cs2.IsCode(c2))
					{
						if (c2 == ':')
						{
							if (j < fileBuf.GetLength() - 1 && fileBuf[j + 1] == ':')
							{
								j++;
								label.Empty();
								continue;
							}
							break;
						}
						if (c2 == '.')
						{
							label.Empty();
							continue;
						}

						if (ISCSYM(c2))
							label += c2;
					}
				}
				if (!label.IsEmpty())
					switchLabels.push_back(label);
			}
			if (i + 7 < length && c == 'd' && fileBuf[i + 1] == 'e' && fileBuf[i + 2] == 'f' && fileBuf[i + 3] == 'a' &&
			    fileBuf[i + 4] == 'u' && fileBuf[i + 5] == 'l' && fileBuf[i + 6] == 't' &&
			    (fileBuf[i + 7] == ':' || IsStatementEndChar(fileBuf[i + 7])))
			{
				defLabel = true;
				i += 6;
			}
		}
	}

	DType* dtype = nullptr;
	InferType infer;
	WTString bcl = mp->GetBaseClassList(mp->m_baseClass);

	WTString typeName = infer.Infer(varName, scope, bcl, mp->FileType());
	if (typeName.GetLength() && typeName.Right(1) == '&')
	{
		// [case: 109635]
		typeName = typeName.Left(typeName.GetLength() - 1);
		typeName.TrimRight();
	}
	typeName.ReplaceAll("const", "", 1);
	typeName.ReplaceAll("constexpr", "", 1);
	typeName.ReplaceAll("consteval", "", 1);
	typeName.ReplaceAll("constinit", "", 1);
	typeName.ReplaceAll("_CONSTEXPR17", "", 1);
	typeName.ReplaceAll("_CONSTEXPR20_CONTAINER", "", 1);
	typeName.ReplaceAll("_CONSTEXPR20", "", 1);
	typeName.ReplaceAll("volatile", "", 1);
	typeName.TrimLeft();

	labelQualification = GetQualification(typeName);

	typeName.ReplaceAll("::", ":", 0);
	typeName.ReplaceAll(".", ":", 0);

	if (!IsEnum(mp.get(), scope, methodScope, bcl, typeName, &dtype))
	{
		if (gTestLogger)
			gTestLogger->LogStrW("The switch type isn't recognized as an enum");
		else
			WtMessageBox("The switch type isn't recognized as an enum.", IDS_APPNAME,
			             MB_OK | MB_ICONEXCLAMATION);

		return true;
	}

	WTString enumDef = dtype->Def();
	if (mp->FileType() == CS || IsEnumClass(enumDef))
	{
		labelQualification = typeName;
		if (mp->FileType() == CS)
			labelQualification.ReplaceAll(":", ".");
		else
			labelQualification.ReplaceAll(":", "::");
	}

	if (!labelQualification.IsEmpty())
	{
		if (mp->FileType() == CS)
			labelQualification += ".";
		else
			labelQualification += "::";
	}

	MultiParsePtr mpLatest(ed->GetParseDb());
	GetEnumItemNames(mpLatest, dtype, enumNames);

	return false;
}

void GetEnumItemNames(MultiParsePtr mp, std::vector<WTString>& enumNames, WTString enumSymScope)
{
	if (!mp)
		return;

	WTString bcl = mp->GetBaseClassList(mp->m_baseClass);

	DBQuery query(mp);

	std::vector<std::pair<int, WTString>> enumNamePairs;

	query.FindAllSymbolsInScopeList(enumSymScope.c_str(), bcl.c_str());

	// collecting enum items in enumNames array
	for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
	{
		if (dt->type() != C_ENUMITEM)
			continue;

		WTString itemSymScope = dt->SymScope();
		WTString itemParentScope;
		WTString itemName;
		for (int j = itemSymScope.GetLength() - 1; j >= 0; j--)
		{
			if (itemSymScope[j] == ':')
			{
				itemParentScope = itemSymScope.Left(j);
				itemName = itemSymScope.Right(itemSymScope.GetLength() - j - 1);
				break;
			}
		}

		if (!itemName.IsEmpty() && !itemParentScope.IsEmpty() &&
		    /*dt->Attributes() == V_INPROJECT &&*/ enumSymScope == itemParentScope)
		{ // with dt->Attributes() == V_INPROJECT cannot create missing cases in the switch in NaturalComparer.Compare
		  // in VA source
			int dbOffset = (int)dt->GetDbOffset();
			enumNamePairs.push_back(std::pair<int, WTString>(dbOffset, itemName));
			// WTString debugStr;
			// debugStr.Format("%s Db: %d\n", dt->Def().c_str(), dbOffset);
			// OutputDebugString(debugStr);
		}
	}

	// sorting enum labels by name
	std::sort(enumNamePairs.begin(), enumNamePairs.end(), [](std::pair<int, WTString> a, std::pair<int, WTString> b) {
		if (a.second == b.second)
			return a.first < b.first;
		else
			return a.second < b.second;
	});

	// removing duplicates - if an enum is in multiple files, it can create duplicates
	for (size_t i = 1; i < enumNamePairs.size(); i++)
	{
		if (enumNamePairs[i - 1].second == enumNamePairs[i].second)
		{
			enumNamePairs.erase(enumNamePairs.begin() + (int)i);
			i--;
		}
	}

	// sorting enum labels by DbOffset
	std::sort(enumNamePairs.begin(), enumNamePairs.end(),
	          [](std::pair<int, WTString> a, std::pair<int, WTString> b) { return a.first < b.first; });

	for (uint i = 0; i < enumNamePairs.size(); i++)
		enumNames.push_back(enumNamePairs[i].second);
}

void GetEnumItemNames(MultiParsePtr mp, DType* dtype, std::vector<WTString>& enumNames)
{
	WTString enumSymScope;
	WTString def = dtype->Def();
	int unnamedPos = FindTypeDefEnumName(def);
	if (unnamedPos == -1)
		enumSymScope = dtype->SymScope();
	else
		enumSymScope = ":" + def.Mid(unnamedPos);

	GetEnumItemNames(mp, enumNames, enumSymScope);
}

bool CreateMissingCases::IsEnum(MultiParse* mp, WTString scope, WTString methodScope, WTString bcl, WTString typeName,
                                DType** dtype_out /*= nullptr*/)
{
	extern WTString GetInnerScopeName(WTString fullScope);
	WTString innerScopeName = GetInnerScopeName(typeName);
	DType* dtype = mp->FindSym(&innerScopeName, &typeName, &bcl);
	if (!dtype || dtype->type() != C_ENUM)
	{
		if (typeName.GetLength() && typeName[0] != ':')
		{
			WTString typeBCL;
			WTString typeName2 = ':' + typeName;
			WTString typeScope = StrGetSymScope(typeName);
			if (typeScope != "" && typeScope != ":")
				typeBCL = mp->GetBaseClassList(typeScope);
			dtype = mp->FindSym(&innerScopeName, &typeName2, &typeBCL);
		}
		if (!dtype || dtype->type() != C_ENUM)
		{
			WTString typeName2;
			if (methodScope != ":")
				typeName2 = methodScope + ":" + typeName;
			else
				typeName2 = ":" + typeName;
			dtype = mp->FindSym(&innerScopeName, &typeName2, &bcl);
			if (!dtype || dtype->type() != C_ENUM)
				dtype = mp->FindSym(&innerScopeName, &scope, &bcl);
		}
	}

	// look the type up to get a DType and see if it's a C_ENUM
	if (!dtype || dtype->type() != C_ENUM)
		return false;

	if (dtype_out)
		*dtype_out = dtype;

	return true;
}

BOOL CreateMissingCases::CanCreate()
{
	if (!IsCPPCSFileAndInsideFunc())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (ed && ed->GetSelString().GetLength() == 0)
	{
		// find the method's opening '{'
		std::vector<WTString> switchLabels;
		int endPos;
		std::vector<WTString> enumNames;
		WTString labelQualification;
		bool defLabel = false;
		if (Parse(ed, switchLabels, endPos, enumNames, labelQualification, defLabel, true))
			return FALSE;

		if (quickCheck)
		{
			return TRUE;
		}
		else
		{
			if (endPos != -1)
			{
				for (uint i = 0; i < enumNames.size(); i++)
				{
					WTString name = enumNames[i];
					std::vector<WTString>::iterator it = std::find(switchLabels.begin(), switchLabels.end(), name);
					if (it == switchLabels.end())
						return TRUE;
				}
			}
		}
	}

	return FALSE;
}

// #CreateMissingCases
BOOL CreateMissingCases::Create()
{
	if (!IsCPPCSFileAndInsideFunc())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	BOOL res = false;
	if (ed && ed->GetSelString().GetLength() == 0)
	{
		// find the method's opening '{'
		std::vector<WTString> switchLabels;
		int endPos;
		std::vector<WTString> enumNames;
		WTString labelQualification;
		bool defLabel = false;
		if (Parse(ed, switchLabels, endPos, enumNames, labelQualification, defLabel, false))
			return FALSE;

		const WTString lineBreak(ed->GetLineBreakString());
		int newItems = 0;
		int insertPos = -1;
		if (endPos != -1)
		{
			FreezeDisplay freeze(TRUE, TRUE);

			insertPos = endPos;
			WTString fileBuf = ed->GetBuf();
			for (int i = endPos - 1; i >= 0; i--)
			{
				if (!IsWSorContinuation(fileBuf[i]))
					break;
				insertPos = i;
			}

			WTString newCode = lineBreak;
			for (uint i = 0; i < enumNames.size(); i++)
			{
				WTString name = enumNames[i];
				std::vector<WTString>::iterator it = std::find(switchLabels.begin(), switchLabels.end(), name);
				if (it == switchLabels.end())
				{
					newCode += WTString("case ") + labelQualification + name + WTString(":") + lineBreak;
					newCode += WTString("break;") + lineBreak;
					newItems++;
				}
			}

			if (newItems > 30)
			{
				WTString msg;
				msg.WTFormat("Visual Assist identified %d missing switch cases.\r\n\r\nDo you want to add them?",
				             newItems);
				if (WtMessageBox(msg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION) == IDNO)
					return FALSE;
			}

			if (!newItems && (defLabel || !Psettings->mAddMissingDefaultSwitchCase))
			{
				if (gTestLogger)
					gTestLogger->LogStrW(L"Create Missing Cases: no missing switch cases found");
				else
					WtMessageBox("Visual Assist did not identify any missing switch cases.", IDS_APPNAME,
					             MB_OK | MB_ICONEXCLAMATION);
				return FALSE;
			}

			if (!defLabel && Psettings->mAddMissingDefaultSwitchCase)
				newCode += WTString("default:") + lineBreak + WTString("break;") + lineBreak;

			ed->SetPos((uint)insertPos);
			res = gAutotextMgr->InsertAsTemplate(ed, newCode, TRUE);
		}

		if (moveCaret && insertPos != -1 && res)
		{
			CommentSkipper cs(ed->m_ftype);
			WTString fileBuf = ed->GetBuf();
			for (int i = insertPos; i < fileBuf.GetLength(); i++)
			{
				TCHAR c = fileBuf[i];
				if (cs.IsCode(c))
				{
					insertPos = i;
					if (!IsWSorContinuation(c))
						break;
				}
			}
			ed->SetPos((uint)insertPos);
		}
	}

	return res;
}
