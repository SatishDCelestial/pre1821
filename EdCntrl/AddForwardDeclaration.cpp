#include "StdAfxEd.h"
#include "AddForwardDeclaration.h"
#include "EDCNT.H"
#include "VAParse.h"
#include "VAAutomation.h"
#include "UndoContext.h"
#include "FreezeDisplay.h"
#include "PROJECT.H"
#include "CommentSkipper.h"
#include "StringUtils.h"
#include "DefListFilter.h"

BOOL InsertForwardDeclaration(int insertAtLine, CStringW forwardDecl)
{
	_ASSERTE(!forwardDecl.IsEmpty());
	CWaitCursor curs;
	// [case: 70467] fix for #includes dropped into ifdef blocks
	// only refine if actually invoking AddInclude, not during QueryStatus

	const uint curPos = g_currentEdCnt->CurPos();
	(void)curPos;
	const long initialFirstVisLine = g_currentEdCnt->GetFirstVisibleLine();
	ulong insertPos = g_currentEdCnt->LinePos(insertAtLine);
	if (insertPos == -1)
		return false;

	UndoContext undoContext("Add Include");
	std::unique_ptr<TerNoScroll> ns;
	if (gShellAttr->IsMsdev())
		ns = std::make_unique<TerNoScroll>(g_currentEdCnt.get());
	FreezeDisplay _f;

	g_currentEdCnt->SetSel(insertPos, insertPos);
	_f.ReadOnlyCheck();
	if (gTestLogger)
	{
		WTString info;
		info.WTFormat("AddForwardDeclaraion: %s at line %d", (LPCTSTR)CString(forwardDecl), insertAtLine);
		gTestLogger->LogStr(info);
	}
	g_currentEdCnt->InsertW(forwardDecl);
	if (gShellAttr->IsDevenv())
	{
		// [case: 30577] restore first visible line
		ulong topPos = g_currentEdCnt->LinePos(initialFirstVisLine + 1);
		if (-1 != topPos && gShellSvc)
		{
			g_currentEdCnt->SetSel(topPos, topPos);
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

void AmendLocation(const WTString& fileBuf, int& line)
{
	EdCntPtr ed(g_currentEdCnt);

	for (int i = line - 1; i > 0; i--)
	{
		int offset = ed->GetBufIndex(fileBuf, ed->LineIndex(i));
		TCHAR c = fileBuf[offset];
		if (c != 13 && c != 10 && c != 0)
		{
			line = i + 1;
			break;
		}
	}
}

bool IsIncludeGuard(FileLineMarker& marker, LineMarkers::Node& ch)
{
	// special casing
	//
	// #ifndef name
	// #define name
	// ...
	// #endif
	//
	// type of things.
	LineMarkers::Node& subNode = ch;
	if (subNode.GetChildCount()) // category "#defines"
	{
		LineMarkers::Node& subCh = subNode.GetChild(0);
		LineMarkers::Node& subSubNode = subCh;
		if (subSubNode.GetChildCount())
		{
			LineMarkers::Node& subSubCh = subSubNode.GetChild(0);
			FileLineMarker& subSubMarker = *subSubCh;
			if (subSubMarker.mText.Left(7) == "#define")
			{
				CStringW first = marker.mText.Right(marker.mText.GetLength() - 7);
				CStringW second = subSubMarker.mText.Right(subSubMarker.mText.GetLength() - 7);
				if (first == second) // not an include guard
					return true;
			}
		}
	}

	return false;
}

bool IsPreprocInMarkerText(FileLineMarker& marker)
{
	return marker.mText.Left(11) == "#if defined" || marker.mText.Left(13) == "#elif defined" ||
	       marker.mText.Left(6) == "#ifdef" || marker.mText.Left(5) == "#else";
}

// symScope: DType style scope including name. get it by calling DType::SymScope()
// node:	 e.g. outline root node. get it by calling LineMarkers::Root()
ulong GetForwardDeclLine(LineMarkers::Node& node, ULONG displayFlag)
{
	int lastLine = 0;
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		if (IsPreprocInMarkerText(marker))
			continue;
		if (marker.mText.Left(7) == "#ifndef")
			if (!IsIncludeGuard(marker, ch))
				continue;
		if (marker.mDisplayFlag & displayFlag)
		{
			if (marker.mDisplayFlag & FileOutlineFlags::ff_Comments)
			{
				// skipping preproc blocks
				for (size_t subidx = idx + 1; subidx < node.GetChildCount(); ++subidx)
				{
					LineMarkers::Node& ch2 = node.GetChild(subidx);
					FileLineMarker& marker2 = *ch2;
					if (IsPreprocInMarkerText(marker2))
					{
						idx = subidx;
						continue;
					}
				}
				LineMarkers::Node& ch3 = node.GetChild(idx);
				FileLineMarker& marker3 = *ch3;
				return (ulong)marker3.mEndLine;
			}
			else
			{
				lastLine = (int)marker.mGotoLine + 1;
			}
		}
		else
		{
			if (displayFlag &
			    FileOutlineFlags::ff_Comments) // comments are only valid as the first element to avoid adding forward
			                                   // declaration before the descriptive comment for the file
				return 0;

			if (lastLine)
				return (ulong)lastLine;
		}
		if (marker.mDisplayFlag == FileOutlineFlags::ff_FwdDeclPseudoGroup || FileOutlineFlags::ff_IncludePseudoGroup ||
		    marker.mDisplayFlag == FileOutlineFlags::ff_MacrosPseudoGroup)
		{
			ulong symbolCP = GetForwardDeclLine(ch, displayFlag);
			if (symbolCP)
				return symbolCP;
		}
	}

	return (ulong)lastLine;
}

const int FirstBytesToScan = 8192;

bool IsAlreadyForwardDeclared(WTString className)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return true;

	WTString fileBuf = ed->GetBuf();
	int u = fileBuf.GetLength() - 6;
	if (u > FirstBytesToScan)
		u = FirstBytesToScan;

	enum State
	{
		WS,
		CLASSNAME,
		WS2,
	};

	CommentSkipper cs(Src);
	for (int i = 0; i < u; i++)
	{
		if (cs.IsCode(fileBuf[i]))
		{
			if (fileBuf[i] == 'c' && fileBuf[i + 1] == 'l' && fileBuf[i + 2] == 'a' && fileBuf[i + 3] == 's' &&
			    fileBuf[i + 4] == 's' && !ISCSYM(fileBuf[i + 5]))
			{
				State state = WS;
				WTString name;
				CommentSkipper cs2(Src);
				int counter = 0;
				for (int j = i + 5; j < fileBuf.GetLength(); j++)
				{
					if (counter++ > FirstBytesToScan)
						return true;
					TCHAR c2 = fileBuf[j];
					if (cs2.IsCode(c2))
					{
						switch (state)
						{
						case WS:
							if (!IsWSorContinuation(c2))
							{
								if (ISCSYM(c2))
									state = CLASSNAME;
								else
									goto next_i;
							}
							else
								break;
						case CLASSNAME:
							if (!ISCSYM(c2))
							{
								if (c2 == ';')
								{
									if (name == className)
										return true;
									else
										goto next_i;
								}
								if (IsWSorContinuation(c2))
									state = WS2;
								else
									goto next_i;
							}
							else
							{
								name += c2;
								break;
							}
						case WS2:
							if (!IsWSorContinuation(c2))
							{
								if (c2 == ';')
								{
									if (name == className)
										return true;
									else
										goto next_i;
								}
								else
									goto next_i;
							}
							break;
						}
					}
				}
			}
		}
	next_i:;
	}

	return false;
}

BOOL AddForwardDeclaration::CanAdd()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (ed->m_ftype != Header)
	{
		if (ed->m_ScopeLangType != Header)
			return FALSE;
	}

	WTString scope = ed->m_lastScope;
	const bool localScope = (-1 != scope.Find('-'));

	const DType dd(ed->GetSymDtype());
	if (dd.IsEmpty())
		return FALSE;

	const uint symType = dd.type();
	if (symType != CLASS && symType != STRUCT && (symType != TYPE || ed->GetSymDef().Left(7) != "typedef"))
		return FALSE;

	WTString fileBuf = ed->GetBuf();
	const long curPos = ed->GetBufIndex(fileBuf, (long) ed->CurPos());

	if (IsTemplate(fileBuf, curPos))
		return false;

	if (!IsItPointerOrReferenceOrTemplate(fileBuf, curPos) && !IsReturnOfValueOfFunctionDeclaration(fileBuf, curPos))
	{
		if (symType != TYPE || ed->GetSymDef().find('*') == -1)
			return false;
	}

	if (localScope && !IsParamListOfDeclaration(fileBuf, curPos))
		return false;

	std::pair<WTString, int> symNameAndType = GetSymNameAndType(false);
	if (symNameAndType.second == -1 ||
	    symNameAndType.second == TYPE) // symbol name was not found or typedef of typedef isn't allowed
		return false;

	if (IsAlreadyForwardDeclared(symNameAndType.first))
		return FALSE;

	InsertedText = (symNameAndType.second == CLASS ? "class " : "struct ");
	InsertedText += symNameAndType.first;
	InsertedText += ";";

	return TRUE;
}

BOOL AddForwardDeclaration::Add()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	WTString fileText(ed->GetBuf());
	//	long curPos = ed->GetBufIndex(fileText, (long)ed->CurPos());

	MultiParsePtr mp = ed->GetParseDb();
	const uint dtType = ed->GetSymDtypeType();
	WTString symScope = dtType == TYPE ? GetSymNameAndType(true).first : mp->GetCwData()->SymScope();
	if (symScope.GetLength())
	{
		if (symScope[0] == ':')
			symScope = symScope.Mid(1);
		QualifyForwardDeclaration(symScope, symScope.GetLength() - 1);
	}

	InsertedText += ed->GetLineBreakString();

	if (dtType == TYPE)
	{
		InsertedText += ed->GetSymDef();
		InsertedText += ";";
		InsertedText += ed->GetLineBreakString();
	}

	LineMarkers markers; // outline data
	GetFileOutline(fileText, markers, mp);

	int curLine = ed->LineFromChar((long)ed->CurPos());
	int forwDeclLine = (int)GetForwardDeclLine(markers.Root(), FileOutlineFlags::ff_FwdDecl);
	if (forwDeclLine == 0 || forwDeclLine > curLine)
	{
		forwDeclLine = (int)GetForwardDeclLine(markers.Root(), FileOutlineFlags::ff_Includes);
		if (forwDeclLine)
			InsertedText = ed->GetLineBreakString() + InsertedText;
	}
	if (forwDeclLine == 0 || forwDeclLine > curLine)
	{
		forwDeclLine =
		    (int)GetForwardDeclLine(markers.Root(), FileOutlineFlags::ff_Preprocessor | FileOutlineFlags::ff_Macros);
		if (forwDeclLine)
			InsertedText = ed->GetLineBreakString() + InsertedText;
	}
	if (forwDeclLine == 0 || forwDeclLine > curLine)
	{
		forwDeclLine = (int)GetForwardDeclLine(markers.Root(), FileOutlineFlags::ff_Comments);
		if (forwDeclLine)
			InsertedText = ed->GetLineBreakString() + InsertedText;
	}

	AmendLocation(fileText, forwDeclLine);
	if (forwDeclLine == 0 || forwDeclLine > curLine)
		forwDeclLine = 1;

	// 	ulong cp = ed->GetBufIndex(ed->LineIndex(forwDeclLine));
	// 	ed->SetSel(cp, cp);
	// 	ed->Insert(InsertedText, false);

	InsertForwardDeclaration(forwDeclLine, InsertedText.Wide());

	return TRUE;
}

bool AddForwardDeclaration::IsItPointerOrReferenceOrTemplate(const WTString& fileBuf, long curPos)
{
	enum State
	{
		SYM,
		WS,
		SEPARATOR,  // '=' or '('
		ALLOCATION, // "new"
		ISTEMPLATEPARAM,
	};

	State state = SYM;
	CommentSkipper cs(Src);
	int angleBrackets = 0;
	int counter = 0;
	bool sameParam = true;
	for (int i = curPos; i < fileBuf.GetLength() - 2; i++) // -2 to safely check for "new"	in case ALLOCATION
	{
		if (counter++ > FirstBytesToScan)
			return false;
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			switch (state)
			{
			case SYM:
				if (ISCSYM(c) || IsWSorContinuation(c))
					break;
				else
				{
					if (c == '>')
						return true;
					else if (c == '*' || c == '&')
					{
						state = SEPARATOR;
						break;
					}
					else
					{
						state = ISTEMPLATEPARAM;
						break;
					}
				}
			case SEPARATOR:
				if (c == ',' || c == ';' || c == '{' || c == '}') // '{' and '}' is for unexpected end
					return true;                                  // didn't find allocation

				if (c == '<')
					angleBrackets++;
				if (c == '>')
				{
					if (angleBrackets == 0)
						return true; // we're inside a template param list
					angleBrackets--;
				}
				if (angleBrackets)
					break;

				if (sameParam && (c == '=' || c == '('))
				{
					state = ALLOCATION;
					break;
				}
				break;
			case ALLOCATION:
				if (IsWSorContinuation(c))
					break;
				if (c == ',' || c == ';' ||
				    c == '}')    // '}' is for unexpected end of the statement (e.g. non-compiling state)
					return true; // didn't find allocation

				if (c == 'n' && fileBuf[i + 1] == 'e' && fileBuf[i + 2] == 'w') // we run the for until buf len - 2
					return false;
				break;
			case ISTEMPLATEPARAM:
				if (IsWSorContinuation(c))
					break;
				if (c == '>')
					return true;
				if (c == ';' || c == '{' || c == '}') // '{' and '}' is for unexpected end
					return false;

				break;
			default:
				break;
			}
		}
	}

	return false;
}

bool AddForwardDeclaration::IsReturnOfValueOfFunctionDeclaration(const WTString& fileBuf, long curPos)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	enum State
	{
		RETURN,
		FUNCNAME,
	};

	State state = RETURN;

	// step 1: is it a return value of a function?
	CommentSkipper cs(Src);
	int i;
	for (i = curPos; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			switch (state)
			{
			case RETURN:
				if (ISCSYM(c))
					break; // we're still inside the returning type
				if (IsWSorContinuation(c))
					state = FUNCNAME;
				else
					return false; // unaccepted character
					              // no break is intentional
			case FUNCNAME:
				if (IsWSorContinuation(c) || ISCSYM(c) || c == ':') // accepted characters
					break;
				if (c == '(')
					goto IsItDeclaration;
				return false;
			}
		}
	}

IsItDeclaration:
	// step 2: is it a declaration?
	return IsParamListOfDeclaration(fileBuf, i + 1);
}

bool AddForwardDeclaration::IsParamListOfDeclaration(const WTString& fileBuf, long curPos)
{
	enum LambdaState
	{
		NO,
		LAMBDA_OR_DIRECT_INIT,
		SQUARE_BRACKETS,
		ROUND_PARENS,
		CURLY_BRACKETS,
	};

	LambdaState ls = NO;

	int counter = 0;
	CommentSkipper cs(Src);
	int parens = 0;
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		if (counter++ > FirstBytesToScan)
			return false;

		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			switch (ls)
			{
			case NO:
				if (c == ';')
					return true;

				if (c == '{')
					return false;

				if (c == '=')
					ls = LAMBDA_OR_DIRECT_INIT;

				break;
			case LAMBDA_OR_DIRECT_INIT: // case 131036
				if (IsWSorContinuation(c))
					break;
				if (c == '[')
					ls = SQUARE_BRACKETS;
				else if (c == '{')
				{
					ls = CURLY_BRACKETS;
					parens++;
				}
				else
					ls = NO;
				break;
			case SQUARE_BRACKETS:
				if (ISCSYM(c) || IsWSorContinuation(c) || c == ',' || c == '&' || c == '=')
					break;
				if (c == ']')
				{
					ls = ROUND_PARENS;
					parens = 0;
					break;
				}
				ls = NO; // not allowed character in lambda's [] block so not a Capture Clause
				break;
			case ROUND_PARENS:
				if (IsWSorContinuation(c))
					break;

				if (c == '(')
				{
					parens++;
					break;
				}
				if (c == ')')
				{
					parens--;
					if (parens <= 0)
					{
						ls = CURLY_BRACKETS;
						parens = 0;
					}
					break;
				}
			case CURLY_BRACKETS:
				if (IsWSorContinuation(c))
					break;

				if (c == '{')
				{
					parens++;
					break;
				}
				if (c == '}')
				{
					parens--;
					if (parens <= 0)
						ls = NO;
					break;
				}

				break;
			}
		}
	}

	return false;
}

void AddForwardDeclaration::QualifyForwardDeclaration(const WTString& buf, long curPos)
{
	enum State
	{
		SYM,   // abc or WS
		COLON, // : or WS
	};

	State state = SYM;
	bool collect = false;
	CommentSkipper cs(Src);
	WTString q;
	int counter = 0;
	for (int i = curPos; i >= 0; i--)
	{
		if (counter++ > FirstBytesToScan)
			return;

		TCHAR c = buf[i];
		if (cs.IsCodeBackward(buf, i))
		{
			switch (state)
			{
			case SYM:
				if (IsWSorContinuation(c))
					break;
				if (c == ':')
				{
					state = COLON;
					if (collect)
						InsertedText = "namespace " + q + " { " + InsertedText + " }";
					else
						collect = true;
					break;
				}
				else if (!ISCSYM(c))
				{
					if (collect)
						InsertedText = "namespace " + q + " { " + InsertedText + " }";
					return;
				}
				if (collect)
					q = c + q;
				break;
			case COLON:
				if (IsWSorContinuation(c))
					break;
				if (c == ':')
					break;
				if (ISCSYM(c))
				{
					state = SYM;
					q = c;
					break;
				}
				return;
			}
		}
	}

	// [case: 137679] when a symbols only other reference is another forward
	// declaration, do not create a namespace in error
	if (collect && q.Compare("ForwardDeclare") != 0)
		InsertedText = "namespace " + q + " { " + InsertedText + " }";
}

std::pair<WTString, int> AddForwardDeclaration::GetSymNameAndType(bool qualified)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return std::make_pair("", 0);

	const DType dt2(ed->GetSymDtype());
	if (dt2.type() == TYPE)
	{
		WTString symDef = ed->GetSymDef();
		if (symDef.Find("<") != -1 || symDef.Find("\f") != -1)
			return std::make_pair("", -1);
		WTString symName;
		WTString symQualifiedName;
		int endPos;

		for (endPos = symDef.GetLength() - 1; endPos >= 0; endPos--)
		{
			if (!ISCSYM(symDef[endPos]))
				break;
		}

		for (int i = 8; i < endPos; i++)
		{
			TCHAR c = symDef[i];
			if (c == ':')
				symName = "";
			else
				symName += c;
			symQualifiedName += c;
		}

		MultiParsePtr mp = ed->GetParseDb();

		symName = TokenGetField(symName, "*&");
		if (TypeListComparer::IsBasicType(symName))
			return std::make_pair("", -1);

		DType* dt = mp->FindAnySym(symName);
		if (dt)
		{
			if (qualified)
				return std::make_pair(symQualifiedName, (int)dt->type());
			else
				return std::make_pair(symName, (int)dt->type());
		}
		else
		{
			return std::make_pair("", -1);
		}
	}
	else
	{
		return std::make_pair(dt2.Sym(), (int)dt2.type());
	}
}

bool AddForwardDeclaration::IsTemplate(const WTString& fileBuf, long curPos)
{
	CommentSkipper cs(Src);

	int counter = 0;
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		if (counter++ > FirstBytesToScan)
			return false;
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (ISCSYM(c) || IsWSorContinuation(c))
				continue;
			if (c == '<')
				return true;
			return false;
		}
	}

	return false;
}
