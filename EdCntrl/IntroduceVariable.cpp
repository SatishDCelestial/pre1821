#include "StdAfxEd.h"
#include "IntroduceVariable.h"
#include "EDCNT.H"
#include "InferType.h"
#include "AddClassMemberDlg.h"
#include "VAParse.h"
#include "FreezeDisplay.h"
#include "AutotextManager.h"
#include "CommentSkipper.h"
#include "DevShellService.h"
#include "VAAutomation.h"
#include "VaTimers.h"
#include "RegKeys.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern WTString GetInnerScopeName(WTString fullScope);

int FindClosestPos(int insertPos, const WTString& fileBuf)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	int lastNewLinePos = insertPos + 1;
	for (int i = insertPos + 1; i < fileBuf.GetLength(); i++)
	{
		if (commentSkipper.IsCode(fileBuf[i]))
		{
			if (fileBuf[i] == 13 || fileBuf[i] == 10)
				lastNewLinePos = i;
			if (!IsWSorContinuation(fileBuf[i]) && fileBuf[i] != '/')
			{
				insertPos = lastNewLinePos;
				break;
			}
		}
	}
	return insertPos;
}

int FindNonWhiteSpace(int pos, const WTString& buf)
{
	for (int i = pos; i < buf.GetLength(); i++)
	{
		TCHAR c = buf[i];
		if (!IsWSorContinuation(c))
		{
			pos = i;
			break;
		}
	}
	return pos;
}

int FindNonWhiteSpaceBackward(int pos, const WTString& buf)
{
	for (int i = pos; i >= 0; i--)
	{
		TCHAR c = buf[i];
		if (!IsWSorContinuation(c))
		{
			pos = i;
			break;
		}
	}
	return pos;
}

int FindNonAlphaNumericalCharBackward(int pos, const WTString& buf)
{
	for (int i = pos; i >= 1; i--)
	{
		if (!ISCSYM(buf[i - 1]))
		{
			pos = i;
			break;
		}
	}
	return pos;
}

int FindEqualitySign(int begPos, int endPos, const WTString& fileBuf)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	int parens = 0;
	for (int i = begPos + 1; i < endPos; i++)
	{
		TCHAR c = fileBuf[i];
		if (commentSkipper.IsCode(c))
		{
			// skipping parens
			if (c == '(')
			{
				parens++;
				continue;
			}
			if (c == ')')
				parens--;
			if (parens > 0)
				continue;

			if (c == '=' && i > 0)
			{
				TCHAR prevC = fileBuf[i - 1];
				if (prevC != '=' && i < fileBuf.GetLength() - 1 && fileBuf[i + 1] != '=' && prevC != '!' &&
				    prevC != '<' && prevC != '>')
				{
					begPos = i + 1;
					int afterEquality = FindNonWhiteSpace(begPos, fileBuf);
					if (afterEquality <= endPos)
						begPos = afterEquality;
					break;
				}
			}
		}
	}
	return begPos;
}

long FindConstructorCall(long begPos, long endPos, const WTString& fileBuf)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	for (int i = begPos + 1; i < endPos; i++)
	{
		TCHAR c = fileBuf[i];
		if (commentSkipper.IsCode(c))
		{
			if (ISCSYM(c) || IsWSorContinuation(c))
				continue;
			if (c == '(')
				return i;
			return begPos;
		}
	}

	return begPos;
}

// First find the end of line, and then the end of comment, so multi line comments can be skipped
int FindEOLThenEOC(int insertPos, const WTString& fileBuf)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	int lastNewLinePos = -1;
	for (int i = insertPos; i < fileBuf.GetLength(); i++)
	{
		if (commentSkipper.IsCode(fileBuf[i]))
		{
			if (fileBuf[i] == 13 /*|| fileBuf[i] == 10*/)
			{
				insertPos = i;
				break;
			}
			if (!IsWSorContinuation(fileBuf[i]) && fileBuf[i] != '/')
			{
				if (lastNewLinePos == -1)
				{
					insertPos = i;
					break;
				}
			}
		}
	}
	return insertPos;
}

// bool IsSymbolChar(TCHAR c)
// {
// 	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
// }

void LowerFirstChar(WTString& symbolName)
{
	WTString firstChar = symbolName.Left(1);
	WTString remaining = symbolName.Right(symbolName.GetLength() - 1);
	firstChar.MakeLower();
	symbolName = firstChar + remaining;
}

bool IsVariableDeclaration(const WTString& fileBuf, int& beg, int end)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	int angleBrackets = 0;
	enum
	{
		neutral,
		typeName,
		separator,
	} state = neutral;
	bool newSection = true;
	for (int i = beg; i < end; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			// skipping white space
			if (IsWSorContinuation(c))
			{
				newSection = true;
				continue;
			}

			// skipping angle brackets
			if (c == '<')
			{
				angleBrackets++;
				state = separator;
				newSection = false;
				continue;
			}
			if (c == '>')
			{
				angleBrackets--;
				if (angleBrackets == 0)
				{
					state = typeName;
					newSection = true;
				}
				else
				{
					state = separator;
					newSection = false;
				}

				continue;
			}
			if (angleBrackets > 0)
			{
				newSection = true;
				continue;
			}

			if (c == '&' || c == '*')
			{
				newSection = true;
				continue;
			}

			if (c == '(') // function call
				return false;

			// invalid chars
			if (ISCSYM(c))
			{
				if (newSection && state == typeName)
				{
					beg = i;
					return true;
				}

				newSection = false;
				state = typeName;
				continue;
			}

			if (c == '.' || c == ':')
			{
				newSection = false;
				state = separator;
				continue;
			}

			return false;
		}
	}

	return false;
}

bool IsFunctionPointerDeclaration(WTString fileBuf, int& beg, int end)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	bool parenReached = false;
	for (int i = beg; i < end; i++)
	{
		TCHAR c = fileBuf[i];
		if (parenReached)
		{
			if (c == '*')
				return true;
			if (!IsWSorContinuation(c))
				parenReached = false;
		}
		if (cs.IsCode(c))
		{
			if (c == '(')
				parenReached = true;
		}
	}

	return false;
}

int FindWholeWordInCodeOutsideBrackets(const WTString& str, WTString subStr, int from = 0, int to = INT_MAX)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	int brackets = 0;
	if (to == INT_MAX)
		to = str.GetLength();
	for (int i = from; i < to; i++)
	{
		TCHAR c = str[i];
		if (!cs.IsCode(c))
			continue;

		if (c == '{')
		{
			brackets++;
			continue;
		}

		if (c == '}')
		{
			brackets--;
			continue;
		}

		if (brackets > 0)
			continue;

		if (i > 0 && ISCSYM(str[i - 1]))
			continue;

		for (int j = 0; j < subStr.GetLength(); j++)
		{
			if (str[i + j] != subStr[j])
				goto next_i;
		}

		{
			int subLen = subStr.GetLength();
			if (i < str.GetLength() - subLen && ISCSYM(str[i + subLen]))
				continue;
		}

		return i;
	next_i:;
	}

	return -1;
}

BOOL IsInvalidKeywordInside(const WTString& sel, bool checkReturn)
{
	if (FindWholeWordInCodeOutsideBrackets(sel, "if") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "for") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "while") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "do") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "for each") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "foreach") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "else") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "switch") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "case") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "break") != -1)
		return TRUE;
	if (FindWholeWordInCodeOutsideBrackets(sel, "continue") != -1)
		return TRUE;
	if (checkReturn && FindWholeWordInCodeOutsideBrackets(sel, "return") != -1)
		return TRUE;
	if (checkReturn && FindWholeWordInCodeOutsideBrackets(sel, "co_return") != -1)
		return TRUE;
	if (checkReturn && FindWholeWordInCodeOutsideBrackets(sel, "co_yield") != -1)
		return TRUE;

	return FALSE;
}

extern int FindOutsideFunctionCall(WTString str, TCHAR op, int fileType);

int DeleteCharAndFollowingWhiteSpaces(int pos, const WTString& fileBuf, bool dele)
{
	int length = fileBuf.GetLength();
	int begPos = -1;
	int endPos = -1;
	for (int i = pos + 1; i < length; i++)
	{
		TCHAR c = fileBuf[i];
		if (!IsWSorContinuation(c))
		{
			endPos = i;
			begPos = pos;
			goto del;
		}
		if (c == '\n' || c == '\r')
		{
			endPos = i;
			goto next;
		}
	}

	endPos = length - 1;

next:
	for (int i = pos - 1; i > 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (!IsWSorContinuation(c))
		{
			begPos = i + 1;
			goto del;
		}
	}

	begPos = 0;

del:
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		ed->SetSel((long)begPos, (long)endPos);
		if (dele)
			ed->Insert("");
		return endPos - begPos;
	}

	return 0;
}

// returns the nr of deleted characters
// dele: false leaves the } selected without deleting it for debug purposes
int DeleteLine(int pos, const WTString& fileBuf, bool dele)
{
	int length = fileBuf.GetLength();
	int begPos = -1;
	int endPos = -1;
	for (int i = pos + 1; i < length; i++)
	{
		TCHAR c = fileBuf[i];
		if (i < length - 1 && c == '\r' && fileBuf[i + 1] == '\n')
		{
			endPos = i + 2;
			goto next;
		}

		if (c == '\n' || c == '\r')
		{
			endPos = i + 1;
			goto next;
		}
	}

	endPos = length - 1;

next:
	for (int i = pos - 1; i > 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (i > 1 && c == '\n' && fileBuf[i - 1] == '\r')
		{
			begPos = i + 1;
			goto del;
		}

		if (c == '\n' || c == '\r')
		{
			begPos = i + 1;
			goto del;
		}
	}

	begPos = 0;

del:
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		ed->SetSel((long)begPos, (long)endPos);
		if (dele)
			ed->Insert("");
		return endPos - begPos;
	}

	return 0;
}

bool IsFunction(const WTString& selection, int pos, int fileType)
{
	ASSERT(ISCSYM(selection[pos]));
	enum class eStage
	{
		FNAME,
		WS1,
		TEMPLATE_PARAMETERS,
		WS2
	};
	eStage stage = eStage::FNAME;
	CommentSkipper cs(fileType);
	int sq = 1;
	for (int i = pos; i < selection.GetLength(); i++)
	{
		TCHAR c = selection[i];
		if (cs.IsCode(c))
		{
			switch (stage)
			{
			case eStage::FNAME:
				if (!ISCSYM(c))
				{
					stage = eStage::WS1;
				}
				else
					break;
			case eStage::WS1:
				if (c == '<')
					stage = eStage::TEMPLATE_PARAMETERS;
				else if (c == '(')
					return true;
				else if (!IsWSorContinuation(c))
					return false;
				break;
			case eStage::TEMPLATE_PARAMETERS:
				if (c == '<')
				{
					sq++;
				}
				else if (c == '>')
				{
					sq--;
					if (sq == 0)
					{
						stage = eStage::WS2;
					}
				}
				break;
			case eStage::WS2:
				if (c == '(')
					return true;
				else if (!IsWSorContinuation(c))
					return false;
				break;
			}
		}
	}

	return false;
}

bool DoesIncludeFunction(WTString selection, int fileType)
{
	CommentSkipper cs(fileType);
	bool isAbc = false;
	for (int i = 0; i < selection.GetLength(); i++)
	{
		TCHAR c = selection[i];
		if (cs.IsCode(c))
		{
			if (isAbc == false && ISCSYM(c))
			{
				isAbc = true;

				if (IsFunction(selection, i, fileType))
					return true;
			}
			else if (isAbc && !ISCSYM(c))
			{
				isAbc = false;
			}
		}
	}

	return false;
}

BOOL IntroduceVariable::Introduce()
{
	EdCntPtr ed(g_currentEdCnt);
	// is everything ok?
	if (!ed)
	{
		_ASSERTE(!"CanIntroduceVariable should have caught this");
		return FALSE;
	}
	WTString scope = ed->m_lastScope;
	if (-1 == scope.Find('-'))
	{
		_ASSERTE(!"CanIntroduceVariable should have caught this");
		return FALSE; // only allow within functions
	}

	WTString selString = ed->GetSelString();
	if (!IsSelectionValid(selString, true, ed->m_ftype)) // the error messages / AST logs are inside the method
		return FALSE;

	if (IsInvalidKeywordInside(selString, true))
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: keyword in selection");
		else
			WtMessageBox("Introduce Variable: keyword in selection.", IDS_APPNAME, MB_OK | MB_ICONERROR);

		return FALSE;
	}

	// infer type
	InferType infer;
	CStringW selection = ed->GetSelStringW();
	MultiParsePtr mp = ed->GetParseDb();
	WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
	WTString type = infer.Infer(selection, scope, bcl, mp->FileType(), true);
	type.TrimLeft();

	// try to infer an appropriate default symbol name from selection
	WTString defaultName = GetDefaultName(selection);
	if (defaultName.IsEmpty())
		defaultName = "name";

	// find method DType
	DTypePtr method = GetMethod(mp.get(), GetReducedScope(scope), scope, mp->m_baseClassList, nullptr,
	                            ed->LineFromChar((long)ed->CurPos()));
	if (method == nullptr)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: GetMethod fail");
		else
			WtMessageBox("Introduce Variable failed to parse method.", IDS_APPNAME, MB_OK | MB_ICONERROR);
		return FALSE;
	}

	// find {
	WTString fileBuf;
	int curPos;
	int bracePos = FindOpeningBrace(method.get()->Line(), fileBuf, curPos);
	if (bracePos == -1)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: invalid brace pos");
		_ASSERTE(!"didn't find { on/after method's line: DType::Line()");
		return FALSE;
	}
	if (curPos < bracePos)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: bad brace pos");
		else
			WtMessageBox("Introduce Variable is not supported at this position.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);
		return FALSE;
	}

	// find previous statement
	eCreateBraces braces;
	int statementPos;
	int prevStatementPos; // is there a statement directly before the last statement (not { or ;)
	int closingParenPos;
	int caseBraces;
	int openBracePos;

	// case 113649
	long outsidePos = FindPosOutsideUniformInitializer(
	    fileBuf, bracePos, curPos); // support for Initializer Lists and Uniform Initialization

	int insertPos = FindPreviousStatement(fileBuf, bracePos, outsidePos /*curPos*/, braces, statementPos,
	                                      prevStatementPos, closingParenPos, caseBraces, openBracePos);
	if (braces == CASE_BEFORE_COLON)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: invalid user input (selection is between case and ':')");
		else
			WtMessageBox("Introduce Variable is not supported at this position.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);
		return FALSE;
	}

	// analyze context and modify insertPos if needed
	int insertPos_brace = -1;
	if (!ModifyInsertPosAndBracesIfNeeded(braces, insertPos, statementPos, closingParenPos, curPos, prevStatementPos,
	                                      caseBraces, fileBuf, insertPos_brace))
		return FALSE;

	// introduce inside selection checks
	bool introduceInsideSelection = ShouldIntroduceInsideSelection(ed, insertPos, fileBuf);

	// does the selection contain a function or a template function? if so, do not find additional occurrences - if
	// would break the code (case 102782)
	bool doesIncludeFunction = DoesIncludeFunction(selection, ed->m_ftype);

	int replaceAllPopupResult = 0;
	long p1, p2;
	ed->GetSel2(p1, p2);
	const WTString bb(ed->GetBufConst());
	int selBegin = ed->GetBufIndex(bb, p1);
	int selEnd = ed->GetBufIndex(bb, p2);
	if (selBegin > selEnd)
		std::swap(selBegin, selEnd);
	int nrOfSelectionLines = ed->LineFromChar(selEnd) - ed->LineFromChar(selBegin);

	int searchFrom = selEnd;
	int searchTo;

	if (braces != NOCREATE || caseBraces == 1)
	{
		if (InsideStatementParens)
		{ // complex
			searchTo = FindEndOfStructure(statementPos, fileBuf, braces);
		}
		else
		{                                              // simple
			int eos_guess = GuessEOS(fileBuf, curPos); // supporting guess here as well
			int eos = ::FindInCode(fileBuf, ';', ed->m_ftype, curPos);
			if (eos != -1 && eos < eos_guess)
				searchTo = eos;
			else if (eos_guess != -1)
				searchTo = eos_guess;
			else
				return FALSE;
		}
	}
	else
	{
		searchTo = FindCorrespondingParen2(fileBuf, searchFrom, '{', '}');
	}
	WTString compactExpression;
	using PosAndLen = std::pair<int, int>;
	PosAndLen posAndLen;
	if (!doesIncludeFunction)
	{
		// try to find additional occurrences in the current scope
		compactExpression = RemoveWhiteSpaces(selection); // TODO: remove comments as well? (but not strings)
		posAndLen = PosAndLen(searchFrom, 0);
		int replaceAllCounter = 0;
		do
		{
			posAndLen = ::FindInCode_Skipwhitespaces(fileBuf, compactExpression, posAndLen, searchTo, ed->m_ftype);
			if (posAndLen.first != -1 && posAndLen.first > selEnd)
				replaceAllCounter++;
			posAndLen.first += posAndLen.second; // continue searching from the end of the found expression
		} while (posAndLen.first != -1);

		if (replaceAllCounter > 0)
		{
			replaceAllPopupResult = ed->ShowIntroduceVariablePopupMenuXP(replaceAllCounter + 1);
			if (replaceAllPopupResult == 0)
				return FALSE; // pressing ESC or clicking outside of the menu
		}
	}

	// get name and type from user dialog
	AddClassMemberDlg dlg(AddClassMemberDlg::DlgIntroduceVariable, (type + WTString(" ") + defaultName).c_str(),
	                      "Variable signature:");
	dlg.SelectLastWord();
	if (dlg.DoModal() != IDOK)
		return FALSE;
	WTString userText = dlg.GetUserText();
	userText.ReplaceAll(";", "");
	int userSpacePos = userText.ReverseFind(' ');
	if (userSpacePos == -1)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"IntroduceVariable error: invalid user input");
		else
			WtMessageBox("Introduce Variable failed to parse the signature.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);
		return FALSE;
	}
	WTString name = userText.Right(userText.GetLength() - (userSpacePos + 1));

	fileBuf = ed->GetBuf();

	// extend selection
	ExtendSelectionToIncludeParensWhenAppropriate(bracePos, fileBuf); // case 84951 Remove residual parentheses

	// case 85028
	WTString origName = name;
	if (IsPrecedingCharacterAlphaNumerical(fileBuf))
		name = " " + name;

	// case 87598 remove surrounding parens
	WTString trimmedSelection = selection; // trimmed whitespaces and parens
	trimmedSelection.Trim();
	int trimmedLen = trimmedSelection.GetLength();
	if (AreParensRemovable(trimmedSelection, trimmedLen))
		trimmedSelection = trimmedSelection.Mid(1, trimmedLen - 2);

	BOOL res = FALSE;
	{
		// replace selection
		searchFrom = -1;
		FreezeDisplay _f(TRUE, TRUE);
		_f.ReadOnlyCheck();
		if (introduceInsideSelection)
		{
			WTString codeToAdd = userText + WTString(" = ") + trimmedSelection;
			ed->InsertW(codeToAdd.Wide());
			searchFrom =
			    selBegin + codeToAdd.GetLength(); // new searchFrom after code modification case #1: replacing selection
		}
		else
		{
			ed->InsertW(name.Wide());
		}

		// introduce variable with or without adding a leading {
		const bool openBraceOnNewLine = Psettings->mInsertOpenBraceOnNewLine;
		bool insertOpeningBrace = braces != NOCREATE || caseBraces == 1;
		if (insertOpeningBrace && !openBraceOnNewLine && !caseBraces)
		{
			ed->SetPos((uint)insertPos_brace);
			ed->Insert(" {");
			insertPos += 2;
		}
		ed->SetPos((uint)insertPos);
		WTString codeToAdd =
		    introduceInsideSelection ? "" : userText + WTString(" = ") + trimmedSelection + WTString(";");
		codeToAdd.Trim();
		if (insertOpeningBrace)
		{
			WTString braceString;
			if (openBraceOnNewLine || caseBraces)
			{
				if (::GetFileType(ed->FileName()) == CS)
					braceString = WTString("{\n\t");
				else
					braceString = WTString("{\n");
			}

			codeToAdd = braceString + codeToAdd;
			if (prevStatementPos != -1 && statementPos != -1)
				if (ed->LineFromChar(prevStatementPos) == ed->LineFromChar(statementPos))
					codeToAdd += "\n";
		}
		const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
		codeToAdd = implCodelnBrk + codeToAdd /* + "$end$"*/;
		if (!introduceInsideSelection || (insertOpeningBrace && openBraceOnNewLine))
		{
			int linesToOffset = 0;
			if (insertOpeningBrace && openBraceOnNewLine)
				++linesToOffset;
			if (!introduceInsideSelection)
				++linesToOffset;
			_f.OffsetLine(linesToOffset);
		}

		res = gAutotextMgr->InsertAsTemplate(ed, codeToAdd, FALSE);
		if (res == FALSE)
		{
			if (gTestLogger)
				gTestLogger->LogStrW(
				    L"IntroduceVariable error: cannot modify the file (InsertAsTemplate returned FALSE)");
			else
				WtMessageBox("Introduce Variable: Cannot modify the file.", IDS_APPNAME,
				             MB_OK | MB_ICONERROR);
		}

		// since the code have changed, these needs to be updated
		searchTo =
		    -1; // 3 cases (replaced later) 1. complex {} wrapping, 2. simple {} wrapping and 3. not inserting '}'

		// search the insertPos for the new variable and find ; to get the end pos of the inserted variable
		if (searchFrom == -1)
		{
			fileBuf = ed->GetBuf(TRUE);
			searchFrom = ed->GetBufIndex(fileBuf, ed->LineIndex(
			    ed->LineFromChar(insertPos) +
			    1)); // new searchFrom after code modification case #2: the variable is introduced outside the selection
			if (!CorrectCurPosToSemicolon(fileBuf, searchFrom))
				return FALSE;
		}

		// find EOL and insert } if needed
		if (braces != NOCREATE || caseBraces == 1)
		{
			if (InsideStatementParens)
				res = InsertClosingBracketComplex(ed, braces, insertPos, searchTo,
				                                  false); // new searchTo after code modification case #1
			else
				res = InsertClosingBracketSimple(ed, introduceInsideSelection,
				                                 searchTo); // new searchTo after code modification case #2
		}

		// replacing additional occurrences
		if (replaceAllPopupResult == 2)
		{
			fileBuf = ed->GetBuf(TRUE);
			ASSERT(searchFrom != -1);

			if (searchTo == -1)
				searchTo = FindCorrespondingParen2(fileBuf, searchFrom, '{',
				                                   '}'); // new searchTo after code modification case #3
			posAndLen = PosAndLen(searchFrom, 0);
			std::list<PosAndLen> replacements;

			// build list of replacements
			do
			{
				posAndLen = ::FindInCode_Skipwhitespaces(fileBuf, compactExpression, posAndLen, searchTo, ed->m_ftype);
				if (posAndLen.first != -1)
				{
					selEnd = -1; // since we replace the selection, the end will not be valid anymore and we correct the
					             // search pos anyway
					replacements.insert(replacements.begin(), posAndLen);
					posAndLen.first += name.GetLength(); // continue searching from the end of the found expression
				}
			} while (posAndLen.first != -1);

			// do replacements in reverse order of search results -- results were stored in the order to exec
			// replacement
			for (const auto& cur : replacements)
			{
				// select the occurrence
				auto pos = cur.first;
				auto len = cur.second;
				ed->SetSel((long)pos, (long)pos + len);

				// replace
				ed->InsertW(name.Wide());
			}

			fileBuf = ed->GetBuf(TRUE);
		}

		long lineFrom = ed->LineFromChar(insertPos);
		if (insertOpeningBrace)
		{ // format from insertPos to searchTo
			long lineTo = ed->LineFromChar(searchTo) +
			              (mp->FileType() == CS
			                   ? 1
			                   : 0); // in C#, even the very same code needs one more line to format for the same result
			ed->Reformat(-1, lineFrom, -1, lineTo);
		}
		else
		{ // format from insertPos to selEnd
			long lineTo = lineFrom + nrOfSelectionLines;
			ed->Reformat(-1, lineFrom, -1, lineTo);
		}
	}

	if (!introduceInsideSelection)
	{
		fileBuf = ed->GetBuf(TRUE);
		curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
		WTString wordAhead = ReadWordAhead(fileBuf, curPos);
		if (wordAhead != origName)
		{
			for (int i = curPos + 1; i < fileBuf.GetLength(); i++)
			{
				TCHAR c = fileBuf[i];
				if (c == '\r' || c == '\n')
					break;
				if (!ISCSYM(fileBuf[i - 1]) && ReadWordAhead(fileBuf, i) == origName)
				{
					ed->SetSel(static_cast<long>(i), static_cast<long>(i));
					break;
				}
			}
		}
	}

	ed->KillTimer(ID_TIMER_GETHINT); // [case: 84936]

	return res; // when we do not insert }, we use the result of the last InsertAsTemplate call
}

bool IntroduceVariable::AreParensRemovable(WTString trimmedSelection, int trimmedLen)
{
	if (trimmedLen > 0 && trimmedSelection[0] == '(' && trimmedSelection[trimmedLen - 1] == ')')
	{
		EdCntPtr ed(g_currentEdCnt);
		CommentSkipper cs(ed->m_ftype);
		int parens = 1;
		for (int i = 1; i < trimmedLen - 1; i++)
		{
			if (cs.IsCode(trimmedSelection[i]))
			{
				if (trimmedSelection[i] == '(')
				{
					parens++;
					continue;
				}
				if (trimmedSelection[i] == ')')
				{
					parens--;
					if (parens <= 0)
						return false;
				}
			}
		}

		return true;
	}

	return false;
}

BOOL IntroduceVariable::CanIntroduce(bool select)
{
	if (!IsCPPCSFileAndInsideFunc())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	WTString selection = ed->GetSelString();

	if (selection.GetLength() == 0)
	{ // case 84978 Be able to trigger without a selection
		MultiParsePtr mp = ed->GetParseDb();
		WTString scope = ed->m_lastScope;
		DTypePtr method = GetMethod(mp.get(), GetReducedScope(scope), scope, mp->m_baseClassList, nullptr,
		                            ed->LineFromChar((long)ed->CurPos()));
		if (method == nullptr)
			return FALSE;

		WTString fileBuf;
		int curPos;
		int bracePos = FindOpeningBrace(method.get()->Line(), fileBuf, curPos);
		if (bracePos == -1) // invalid brace pos
			return FALSE;
		if (curPos < (long)bracePos)
			return FALSE;

		// find previous statement
		IntroduceVariable::eCreateBraces braces;
		int statementPos;
		int prevStatementPos; // is there a statement directly before the last statement (not { or ;)
		int closingParenPos;
		int caseBraces;
		int openBracePos;
		int insertPos = FindPreviousStatement(fileBuf, bracePos, curPos, braces, statementPos, prevStatementPos,
		                                      closingParenPos, caseBraces, openBracePos);
		if (braces == CASE_BEFORE_COLON) // invalid user input (selection is between case and ':')
			return FALSE;

		if (braces == FOR_WITHOUT_BRACES || braces == FOREACH_WITHOUT_BRACES || braces == WHILE_WITHOUT_BRACES)
		{
			return FALSE;
		}

		// [case: 147654]

		// Find a ; or { to the left
		// prevStatementPos is seemingly has what we need but it's not what we need. e.g. VAAutoTest:TestCommentBlock1 would fail with it
		CommentSkipper cs(ed->m_ftype);
		int searchFrom = -1;
		int safetyCounter = 0;
		for (int i = curPos; i >= 0; i--)
		{
			if (safetyCounter++ > 1000)
				break;
			if (cs.IsCodeBackward(fileBuf, i))
			{
				if (fileBuf[i] == '{' || fileBuf[i] == ';')
				{
					searchFrom = i;
					break;
				}
			}
		}

		// Is the first non-whitespace character is a non-comment to the right? If yes, don't disable the command,
		// abort this checker.
		safetyCounter = 0;
		for (int i = curPos; i < fileBuf.GetLength() - 1; i++)
		{
			if (!IsWSorContinuation(fileBuf[i]))
			{
				if (fileBuf[i] != '/' || (fileBuf[i + 1] != '/' && fileBuf[i + 1] == '*'))
					goto abortCommentBlockCheck;
				break;
			}
		}

		// Is there any non-whitespace, non-comment character from ; or { ? If none found, disable the command
		if (searchFrom == -1)
			goto abortCommentBlockCheck; // syntax error
		cs.Reset();
		for (int i = searchFrom + 1; i < curPos - 1; i++)
		{
			if (cs.IsCode(fileBuf[i]))
			{
				if (!IsWSorContinuation(fileBuf[i]) &&
				    !(fileBuf[i] == '/' && (fileBuf[i + 1] == '/' || fileBuf[i + 1] == '*')))
				{
					goto abortCommentBlockCheck;
				}
			}
		}

		return FALSE;

	abortCommentBlockCheck: // this logic didn't disable the command, continue with the other checkers

		// analyze context and modify insertPos if needed
		int insertPos_brace = -1;
		if (!ModifyInsertPosAndBracesIfNeeded(braces, insertPos, statementPos, closingParenPos, curPos,
		                                      prevStatementPos, caseBraces, fileBuf, insertPos_brace))
			return FALSE;

		int endPos = curPos;
		int begPos;
		if (InsideStatementParens)
		{
			if (statementPos == -1)
				return FALSE;

			begPos = ::FindInCode(fileBuf, "(", ed->m_ftype, statementPos);
			endPos = FindCorrespondingParen(fileBuf, begPos);
			begPos++;
		}
		else
		{
			if (!MovePosToEndOfStatement(fileBuf, endPos))
				return FALSE;
			// 			begPos = insertPos;
			begPos = FindNonWhiteSpace(insertPos, fileBuf);
			if (endPos - begPos > 6)
			{
				if (fileBuf[begPos] == 'r' && fileBuf[begPos + 1] == 'e' && fileBuf[begPos + 2] == 't' &&
				    fileBuf[begPos + 3] == 'u' && fileBuf[begPos + 4] == 'r' && fileBuf[begPos + 5] == 'n' &&
				    IsWSorContinuation(fileBuf[begPos + 6]))
				{
					begPos += 7;
				}
			}
			if (endPos - begPos > 9)
			{
				if (fileBuf[begPos] == 'c' && fileBuf[begPos + 1] == 'o' && fileBuf[begPos + 2] == '_' &&
				    fileBuf[begPos + 3] == 'r' && fileBuf[begPos + 4] == 'e' && fileBuf[begPos + 5] == 't' &&
				    fileBuf[begPos + 6] == 'u' && fileBuf[begPos + 7] == 'r' && fileBuf[begPos + 8] == 'n' &&
				    IsWSorContinuation(fileBuf[begPos + 9]))
				{
					begPos += 10;
				}
			}
			if (endPos - begPos > 8)
			{
				if (fileBuf[begPos] == 'c' && fileBuf[begPos + 1] == 'o' && fileBuf[begPos + 2] == '_' &&
				    fileBuf[begPos + 3] == 'y' && fileBuf[begPos + 4] == 'i' && fileBuf[begPos + 5] == 'e' &&
				    fileBuf[begPos + 6] == 'l' && fileBuf[begPos + 7] == 'd' && IsWSorContinuation(fileBuf[begPos + 8]))
				{
					begPos += 9;
				}
			}
		}

		WTString cut = fileBuf.Mid(begPos, endPos - begPos);
		if (IsInvalidKeywordInside(cut, false))
			return FALSE;

		cut.TrimLeft();
		cs.Reset();
		if (cut.GetLength() >= 4)
		{
			for (int i = 0; i < cut.GetLength(); i++)
			{
				TCHAR c = cut[i];
				if (cs.IsCode(c) && !IsWSorContinuation(c))
				{
					if (c == 'n' && cut[i + 1] == 'e' && cut[i + 2] == 'w' && IsWSorContinuation(cut[i + 3]))
					{
						goto checkSelectionValidity;
					}
					break;
				}
			}
		}

		if (!InsideStatementParens && IsFunctionPointerDeclaration(fileBuf, begPos, endPos))
		{
			return FALSE;
		}

		if (!InsideStatementParens && IsVariableDeclaration(fileBuf, begPos, endPos))
		{
			long newBegPos = FindEqualitySign(begPos, endPos, fileBuf);
			if (newBegPos != begPos)
			{ // it's a declaration but we can select the part after '='
				begPos = newBegPos;
			}
			else
			{
				newBegPos = FindConstructorCall(begPos, endPos, fileBuf);
				if (newBegPos != begPos)
				{ // it's a declaration but we can select the part inside the constructor call's parens
					begPos = newBegPos + 1;
					endPos = FindCorrespondingParen(fileBuf, newBegPos);
					if (endPos == -1)
						return FALSE;
				}
				else
				{
					return FALSE;
				}
			}
		}
		else
		{
			begPos = FindEqualitySign(begPos, endPos, fileBuf);
		}

	checkSelectionValidity:
		cut = fileBuf.Mid(begPos, endPos - begPos);
		if (!IsSelectionValid(cut, false, ed->m_ftype))
			return FALSE;

		if (select)
			ed->SetSel(static_cast<long>(begPos), static_cast<long>(endPos));

		return !!g_currentEdCnt;
	}
	else
	{
		// case 86774 Support lambdas
		// if there is a ';' outside curly brackets, we abort the operation
		CommentSkipper cs(ed->m_ftype);
		int curly = 0;
		for (int i = 0; i < selection.GetLength(); i++)
		{
			TCHAR c = selection[i];
			if (cs.IsCode(c))
			{
				if (c == '{')
					curly++;
				if (c == '}')
					curly--;
				if (c == ';' && curly == 0)
					return FALSE;
			}
		}

		return g_currentEdCnt && selection.GetLength() != 0;
	}
}

// #AddRemoveBraces
BOOL IntroduceVariable::AddBraces(bool canAdd)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!IsCPPCSFileAndInsideFunc() && ed->m_lastScope != "String")
		return FALSE;

	if (ed->GetSelString().GetLength() == 0)
	{
		MultiParsePtr mp = ed->GetParseDb();
		WTString scope = ed->m_lastScope;
		WTString name = GetReducedScope(scope);
		if (scope == "String")
		{
			scope = ":" + mp->m_MethodName;
			name = scope;
		}
		DTypePtr method =
		    GetMethod(mp.get(), name, scope, mp->m_baseClassList, nullptr, ed->LineFromChar((long)ed->CurPos()));
		if (method == nullptr)
			return FALSE;

		WTString fileBuf;
		int curPos;
		int bracePos = FindOpeningBrace(method.get()->Line(), fileBuf, curPos);
		if (bracePos == -1)
		{
			if (gTestLogger && !canAdd)
				gTestLogger->LogStrW(L"AddBraces error: invalid brace pos");
			return FALSE;
		}
		if (curPos < (long)bracePos)
			return FALSE;

		// the order of calling of MoveBeforeSemicolonIfNeeded and MoveInsideControlStatementParensIfNeeded was swapped
		// due to case 90586
		MoveBeforeSemicolonIfNeeded(curPos, fileBuf);
		MoveInsideControlStatementParensIfNeeded(curPos, fileBuf);
		MoveAfterOpeningBraceIfNeeded(curPos, fileBuf);

		// find previous statement
		IntroduceVariable::eCreateBraces braces;
		int statementPos;
		int prevStatementPos; // is there a statement directly before the last statement (not { or ;)
		int closingParenPos;
		int caseBraces;
		int openBracePos;
		int insertPos = FindPreviousStatement(fileBuf, bracePos, curPos, braces, statementPos, prevStatementPos,
		                                      closingParenPos, caseBraces, openBracePos);
		if (braces == CASE_BEFORE_COLON)
		{
			if (gTestLogger && !canAdd)
				gTestLogger->LogStrW(L"AddBraces error: invalid user input (selection is between case and ':')");
			return FALSE;
		}

		// case 90461
		if (caseBraces == 1) // case 92040
			return FALSE;

		// analyze context and modify insertPos if needed
		int insertPos_brace = -1;
		if (!ModifyInsertPosAndBracesIfNeeded(braces, insertPos, statementPos, closingParenPos, curPos,
		                                      prevStatementPos, caseBraces, fileBuf, insertPos_brace))
			return FALSE;

		bool insertOpeningBrace = braces != NOCREATE || caseBraces == 1;
		if (canAdd)
			return insertOpeningBrace;
		if (!insertOpeningBrace)
			return FALSE;

		FreezeDisplay _f(TRUE, TRUE);
		_f.ReadOnlyCheck();

		// insert { on same line
		const bool openBraceOnNewLine = Psettings->mInsertOpenBraceOnNewLine;
		if (!openBraceOnNewLine && !caseBraces)
		{
			ed->SetPos((uint)insertPos_brace);
			ed->Insert(" {");
			insertPos += 2;
			ed->SetPos((uint)insertPos);
		}

		// insert { on new line
		bool ip_below_cp = false;
		if (openBraceOnNewLine || caseBraces)
		{
			int line_ip = ed->LineFromChar(insertPos_brace) + 1;
			int line_cp = ed->LineFromChar(curPos);
			if (line_ip > line_cp)
				ip_below_cp = true;

			ed->SetPos((uint)insertPos_brace);
			const WTString lineBreak(ed->GetLineBreakString());
			WTString braceString;
			braceString =
			    WTString(lineBreak + "{" /*+ lineBreak*/); // second lineBreak removal: case 90808 reopen: newline fix

			BOOL res = FALSE;
			res = gAutotextMgr->InsertAsTemplate(ed, braceString, FALSE);
			if (res == FALSE)
			{
				if (gTestLogger && !canAdd)
					gTestLogger->LogStrW(L"AddBraces error: cannot modify the file (InsertAsTemplate returned FALSE)");
				else
					WtMessageBox("Add Braces: Cannot modify the file.", IDS_APPNAME, MB_OK | MB_ICONERROR);
			}
			if (!res)
				return FALSE;
		}

		int linesToOffset = 0;
		if (openBraceOnNewLine && !ip_below_cp)
			++linesToOffset;
		//++linesToOffset;
		_f.OffsetLine(linesToOffset);

		// find EOL and insert } if needed
		int searchTo = -1;
		if (braces != NOCREATE || caseBraces == 1)
		{
			if (InsideStatementParens)
			{
				if (!InsertClosingBracketComplex(ed, braces, insertPos, searchTo,
				                                 true)) // new searchTo after code modification case #1
					return FALSE;
			}
			else
			{
				if (!InsertClosingBracketSimple(ed, false, searchTo)) // new searchTo after code modification case #2
					return FALSE;
			}
		}

		// format the block
		long lineFrom = ed->LineFromChar(insertPos);
		if (openBraceOnNewLine) // case 90808 reopen: newline fix
			lineFrom--;
		if (searchTo != -1)
		{
			long lineTo = ed->LineFromChar(searchTo) +
			              (mp->FileType() == CS
			                   ? 1
			                   : 0); // in C#, even the very same code needs one more line to format for the same result
			ed->Reformat(-1, lineFrom, -1, lineTo);
		}
	}
	else
	{
		return FALSE;
	}

	// case 90808 reopen: fix for curpos
	ed->SetPos(
	    (uint)ed->GetBufIndex((long)ed->CurPos())); // fixes the case when the caret would have been left beyond EOL

	return FALSE;
}

BOOL IntroduceVariable::RemoveBraces(bool canRemove, int methodLine /*= -1*/)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!IsCPPCSFileAndInsideFunc() && ed->m_lastScope != "String")
		return FALSE;

	if (ed->GetSelString().GetLength() == 0)
	{
		MultiParsePtr mp = ed->GetParseDb();
		WTString scope = ed->m_lastScope;
		WTString name = GetReducedScope(scope);
		if (scope == "String")
		{
			scope = ":" + mp->m_MethodName;
			name = scope;
		}
		DTypePtr method =
		    GetMethod(mp.get(), name, scope, mp->m_baseClassList, nullptr, ed->LineFromChar((long)ed->CurPos()));
		if (method == nullptr)
			return FALSE;

		WTString fileBuf;
		int curPos;
		int line = methodLine != -1 ? methodLine : method.get()->Line();
		int bracePos = FindOpeningBrace(line, fileBuf, curPos);
		if (bracePos == -1)
		{
			if (gTestLogger && !canRemove)
				gTestLogger->LogStrW(L"RemoveBraces error: invalid brace pos");
			return FALSE;
		}
		if (curPos < bracePos)
			return FALSE;

		MoveAfterOpeningBraceIfNeeded(curPos, fileBuf);

		int delete1 = FindPreviousBracket(bracePos, curPos, fileBuf);
		if (delete1 != -1)
		{
			int delete2 = FindCorrespondingParen(fileBuf, delete1, '{', '}');
			if (delete2 != -1)
			{
				if (curPos > delete2)
					return FALSE;

				if (IsEmptyBetween(delete1, delete2, fileBuf))
					return FALSE;

				if (IsMultipleStatementBetween(delete1, delete2, fileBuf))
					return FALSE;

				if (IsInsideCLambdaFunction(bracePos, delete1, delete2, fileBuf, mp.get()))
					return FALSE;

				if (canRemove)
					return TRUE;

				fileBuf = ed->GetBuf();
				curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
				int origWhiteSpaces = CountWhiteSpacesBeforeFirstChar(curPos, fileBuf);
				int origCharsToEOL = CountCharsToEOL(curPos, fileBuf);

				DeleteParens(delete1, delete2, fileBuf, mp.get());

				// correcting caret pos after formatting
				fileBuf = ed->GetBuf();
				curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
				int currentWhiteSpaces = CountWhiteSpacesBeforeFirstChar(curPos, fileBuf);
				int caretShift = currentWhiteSpaces - origWhiteSpaces;
				if (caretShift < 0)
					caretShift =
					    -std::min(std::abs(caretShift),
					              origCharsToEOL); // the IDE does not allow the caret to go over EOL, so we correct the
					                               // amount of shift depending on the distance from EOL
				MoveCaretUntilNewLine(curPos, caretShift, fileBuf);

				return TRUE;
			}
		}
	}

	if (!canRemove)
	{
		// This path is triggered by AST AddRemoveBraces57, for example. at CanRemove, we don't have enough information
		// for complex decisions due to VARefactorCls not being available (for parser states)
		if (gTestLogger)
			gTestLogger->LogStrW(L"Remove braces error: other error");
		else
			WtMessageBox("Cannot remove braces", IDS_APPNAME, MB_OK | MB_ICONERROR);
	}

	return FALSE;
}

BOOL IntroduceVariable::IsMultipleStatementBetween(int paren1, int paren2, const WTString& fileBuf)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	int openBraces = 0;
	int openParens = 0;
	bool whiteSpaceAvoider = false;
	BOOL semiColonFound = false; // case 90126
	for (int i = paren1 + 1; i < paren2; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (whiteSpaceAvoider && openBraces == 0 && !IsWSorContinuation(c) && c != '/')
			{
				std::pair<WTString, int> nextWord = GetNextWord(fileBuf, i);
				if (nextWord.first == "else")
					whiteSpaceAvoider = false;
				else
					return TRUE;
			}
			if (c == '{')
			{
				openBraces++;
				whiteSpaceAvoider = true;
			}
			if (c == '}')
				openBraces--;
			if (c == '(')
				openParens++;
			if (c == ')')
				openParens--;
			if (openBraces == 0 && openParens == 0 && c == ';')
			{
				whiteSpaceAvoider = true;
				semiColonFound = true;
			}

			// case 90461
			if (openBraces == 0 && openParens == 0)
			{
				if (i + 4 < fileBuf.GetLength() && c == 'c' && fileBuf[i + 1] == 'a' && fileBuf[i + 2] == 's' &&
				    fileBuf[i + 3] == 'e' && (IsWSorContinuation(fileBuf[i + 4]) || fileBuf[i + 4] == '/'))
					return TRUE;
				if (i + 7 < fileBuf.GetLength() && c == 'd' && fileBuf[i + 1] == 'e' && fileBuf[i + 2] == 'f' &&
				    fileBuf[i + 3] == 'a' && fileBuf[i + 4] == 'u' && fileBuf[i + 5] == 'l' && fileBuf[i + 6] == 't' &&
				    (IsWSorContinuation(fileBuf[i + 7]) || fileBuf[i + 7] == '/'))
					return TRUE;
			}
		}
	}

	if (!semiColonFound)
	{
		// case 90126
		int from = paren1 + 1;
		int to = paren2;
		if (to - from > 0)
		{
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "if", from, to) != -1)
				return FALSE;
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "for", from, to) != -1)
				return FALSE;
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "while", from, to) != -1)
				return FALSE;
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "do", from, to) != -1)
				return FALSE;
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "for each", from, to) != -1)
				return FALSE;
			if (FindWholeWordInCodeOutsideBrackets(fileBuf, "foreach", from, to) != -1)
				return FALSE;
		}

		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL IntroduceVariable::IsOnlyCharacterOnLine(int pos, const WTString& fileBuf)
{
	for (int i = pos + 1; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (c == '\r' || c == '\n')
			goto next;
		if (!IsWSorContinuation(c))
			return FALSE;
	}

next:
	for (int i = pos - 1; i > 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (c == '\r' || c == '\n')
			return TRUE;
		if (!IsWSorContinuation(c))
			return FALSE;
	}

	return TRUE;
}

void IntroduceVariable::DeleteParens(int paren1, int paren2, const WTString& fileBuf, MultiParse* mp)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	int removedChars;
	int paren1Line = ed->LineFromChar(paren1);
	int paren2Line = ed->LineFromChar(paren2);

	{
		FreezeDisplay _f(TRUE, TRUE);
		_f.ReadOnlyCheck();
		int linesToOffset = 0;

		// removing {
		if (IsOnlyCharacterOnLine(paren1, fileBuf))
		{
			int caretLine = ed->LineFromChar((long)ed->CurPos());
			removedChars = DeleteLine(paren1, fileBuf, true);
			if (paren1Line < caretLine)
				linesToOffset--;
		}
		else
		{
			removedChars = DeleteCharAndFollowingWhiteSpaces(paren1, fileBuf, true);
		}

		// removing }
		WTString fileBuf2 = ed->GetBuf();
		paren2 -= removedChars;
		if (IsOnlyCharacterOnLine(paren2, fileBuf2))
		{
			DeleteLine(paren2, fileBuf2, true);
		}
		else
		{
			DeleteCharAndFollowingWhiteSpaces(paren2, fileBuf2, true);
		}

		_f.OffsetLine(linesToOffset);

		// format the block
		if (paren2Line > paren1Line)
		{
			int formatFrom = paren1Line - 1;
			int diff = paren2Line - paren1Line;
			int formatTo = formatFrom + diff - 2 + (mp->FileType() == CS ? 1 : 0);
			ed->Reformat(-1, formatFrom, -1, formatTo);
		}
	}

	ed->SetPos(
		(uint)ed->GetBufIndex((long)ed->CurPos())); // fixes the case when the caret would have been left beyond EOL
}

int IntroduceVariable::FindPreviousBracket(int openingBrace, int currentPos, const WTString& fileBuf)
{
	int latestOpeningBrace = -1;
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	for (int i = openingBrace + 1; i < currentPos; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c) && c == '{')
			latestOpeningBrace = i;
	}

	return latestOpeningBrace;
}

void IntroduceVariable::MoveInsideControlStatementParensIfNeeded(int& curPos, const WTString& fileBuf)
{
	int newCurPos;

	// 1. if we're on whitespace, go forward till we find a non-whitespace
	newCurPos = FindNonWhiteSpace(curPos, fileBuf);
	if (IsWSorContinuation(fileBuf[newCurPos]))
		return;

	// 2. if it's a '(' go backward till we find non-whitespace to see if it's an alphanumerical
	if (fileBuf[newCurPos] == '(')
		newCurPos = FindNonWhiteSpaceBackward(newCurPos, fileBuf);

	// if still not over an alphanumerical, abort mission
	if (!ISCSYM(fileBuf[newCurPos]))
		return;

	// 3. if we're on alphabets, go backward on them till we find a whitespace to find the beginning of the word
	newCurPos = FindNonAlphaNumericalCharBackward(newCurPos, fileBuf);

	// 4. if it's a keyword for control statement, go find it's opening '(' and move insertPos inside them and set
	// mInsideStatementParens to true
	std::pair<WTString, int> word = GetNextWord(fileBuf, newCurPos);
	if (word.first == "if" || word.first == "while" || word.first == "for" || word.first == "foreach" ||
	    word.first == "each")
	{
		EdCntPtr ed(g_currentEdCnt);
		CommentSkipper cs(ed->m_ftype);
		for (int i = word.second; i < fileBuf.GetLength(); i++)
		{
			TCHAR c = fileBuf[i];
			if (cs.IsCode(c))
			{
				if (c == '(')
				{
					curPos = i + 1;
					// mInsideStatementParens = true;
					return;
				}
			}
		}
	}
}

void IntroduceVariable::MoveBeforeSemicolonIfNeeded(int& curPos, const WTString& fileBuf)
{
	if (!IsWSorContinuation(fileBuf[curPos]))
		return;

	int semicolonPos;

	// char on the left should be ';'
	for (int i = curPos - 1; i > 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (!IsWSorContinuation(c))
		{
			if (c == ';')
			{
				semicolonPos = i;
				goto right;
			}
			return;
		}
	}
	return;

right:
	// char on the right should be newline
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (c == '\n' || c == '\r')
			goto next;
	}

	return;

next:
	curPos = semicolonPos;
}

void IntroduceVariable::MoveAfterOpeningBraceIfNeeded(int& curPos, const WTString& fileBuf)
{
	int openingBrace = FindOpeningBraceAfterWhiteSpace(curPos, fileBuf);
	if (openingBrace != -1)
		curPos = openingBrace + 1;
}

int IntroduceVariable::FindCorrespondingParen(const WTString& buf, int pos, char opParen /*='('*/,
                                              char clParen /*=')'*/)
{
	_ASSERTE(buf[pos] == opParen);
	if (buf[pos] != opParen)
		return -1;

	int openBraces = 0;
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed ? ed->m_ftype : gTypingDevLang);
	for (int i = pos; i < buf.GetLength(); i++)
	{
		if (commentSkipper.IsCode(buf[i]))
		{
			if (buf[i] == opParen)
				openBraces++;
			if (buf[i] == clParen)
			{
				openBraces--;
				if (openBraces <= 0) // <= means it may work for some not compiling code as well
					return i;
			}
		}
	}

	return -1;
}

int IntroduceVariable::FindCorrespondingParen2(const WTString& buf, int pos, char opParen /*='('*/,
                                               char clParen /*=')'*/)
{
	int openBraces = 1;
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed ? ed->m_ftype : gTypingDevLang);
	for (int i = pos; i < buf.GetLength(); i++)
	{
		if (commentSkipper.IsCode(buf[i]))
		{
			if (buf[i] == opParen)
				openBraces++;
			if (buf[i] == clParen)
			{
				openBraces--;
				if (openBraces <= 0) // <= means it may work for some not compiling code as well
					return i;
			}
		}
	}

	return -1;
}

int IntroduceVariable::FindOpeningBraceAfterWhiteSpace(long curPos, const WTString& fileBuf)
{
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (!IsWSorContinuation(c))
		{
			if (c == '{')
				return i;
			else
				return -1;
		}
	}

	return -1;
}

int IntroduceVariable::FindPreviousStatement(const WTString& buf, int from, int to, eCreateBraces& braces,
                                             int& statementPos, int& prevStatementPos, int& closingParenPos,
                                             int& caseBraces, int& openBracePos)
{
	enum eStatementType
	{
		NOSTATEMENT,
		FOR,
		WHILE,
		IF,
		DO,
		ELSE,
		FOREACH
	};

	eStatementType lastStatementType = NOSTATEMENT;

	braces = NOCREATE;
	statementPos = -1;
	prevStatementPos = -1;
	closingParenPos = -1;
	caseBraces = 0;

	int lastOpenBracePos = from;
	int lastCloseBracePos = -1;
	int lastSemicolonPos = -1;
	int semicolonBeforeFor = -1;
	bool waitingForCaseColon = false;

	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	bool forSkipper = false; // do not consider ; as statement closer inside a for's parens
	int forParens = 0;

	long p1, p2;
	ed->GetSel2(p1, p2);
	int selBegin = ed->GetBufIndex(p1);
	int selEnd = ed->GetBufIndex(p2);
	if (selBegin > selEnd)
		std::swap(selBegin, selEnd);

	for (int i = from; i < to; i++)
	{
		if (!commentSkipper.IsCode(buf[i]))
			continue;

		if (i >= selBegin && i < selEnd)
			continue;

		if (forSkipper)
		{
			if (buf[i] == '(')
				forParens++;
			if (buf[i] == ')')
			{
				forParens--;
				if (forParens == 0)
					forSkipper = false;
			}

			continue;
		}

		// case
		if (i > 0 && !ISCSYM(buf[i - 1]))
		{
			if (i + 4 < to && buf[i] == 'c' && buf[i + 1] == 'a' && buf[i + 2] == 's' && buf[i + 3] == 'e' &&
			    IsStatementEndChar(buf[i + 4]))
			{
				caseBraces = 1;
				waitingForCaseColon = true;
			}
		}
		if (waitingForCaseColon && buf[i] == ':')
		{
			// closingParenPos = i;
			lastSemicolonPos = i;
			waitingForCaseColon = false;
		}

		if (caseBraces > 0 && buf[i] == '{')
			caseBraces++;
		if (caseBraces > 0 && buf[i] == '}')
			caseBraces--;

		if (buf[i] == ';')
		{
			lastSemicolonPos = i;
			continue;
		}
		if (buf[i] == '{')
		{
			lastOpenBracePos = i;
			continue;
		}
		if (buf[i] == '}')
		{
			lastCloseBracePos = i;
			continue;
		}
		if (i > 0 && !ISCSYM(buf[i - 1]))
		{
			if (i + 3 < to && buf[i] == 'f' && buf[i + 1] == 'o' && buf[i + 2] == 'r' && IsStatementEndChar(buf[i + 3]))
			{ // for
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = FOR;
				semicolonBeforeFor = lastSemicolonPos;
				forSkipper = true;
				continue;
			}
			if (i + 2 < to && buf[i] == 'd' && buf[i + 1] == 'o' && IsStatementEndChar(buf[i + 2]))
			{ // do
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = DO;
				continue;
			}
			if (i + 2 < to && buf[i] == 'i' && buf[i + 1] == 'f' && IsStatementEndChar(buf[i + 2]))
			{ // if
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = IF;
				continue;
			}
			if (i + 5 < to && buf[i] == 'w' && buf[i + 1] == 'h' && buf[i + 2] == 'i' && buf[i + 3] == 'l' &&
			    buf[i + 4] == 'e' && IsStatementEndChar(buf[i + 5]))
			{
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = WHILE;
				continue;
			}
			if (i + 3 < to && buf[i] == 'e' && buf[i + 1] == 'l' && buf[i + 2] == 's' && buf[i + 3] == 'e')
			{
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i;
				lastStatementType = ELSE;
				continue;
			}
			if (i + 7 < to && buf[i] == 'f' && buf[i + 1] == 'o' && buf[i + 2] == 'r' && buf[i + 3] == 'e' &&
			    buf[i + 4] == 'a' && buf[i + 5] == 'c' && buf[i + 6] == 'h' && IsStatementEndChar(buf[i + 7]))
			{
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = FOREACH;
				continue;
			}
			if (i + 8 < to && buf[i] == 'f' && buf[i + 1] == 'o' && buf[i + 2] == 'r' && buf[i + 3] == ' ' &&
			    buf[i + 4] == 'e' && buf[i + 5] == 'a' && buf[i + 6] == 'c' && buf[i + 7] == 'h' &&
			    IsStatementEndChar(buf[i + 8]))
			{
				if (statementPos != -1)
					prevStatementPos = statementPos;
				statementPos = i - 1;
				lastStatementType = FOREACH;
				continue;
			}
		}
	}

	openBracePos = lastOpenBracePos;

	if (statementPos != -1 && lastStatementType != ELSE)
	{
		int openParenPos = ::FindInCode(buf, '(', ed->m_ftype, statementPos);
		if (openParenPos != -1)
			closingParenPos = FindCorrespondingParen(buf, openParenPos);
	}

	if (lastStatementType == FOR)
	{
		if ((lastSemicolonPos == -1 || lastSemicolonPos < closingParenPos) && statementPos > lastOpenBracePos)
		{ // for without {
			braces = FOR_WITHOUT_BRACES;
			// lastSemicolonPos = semicolonBeforeFor; // use last semicolon before the for if there is no { after the
			// "for (;;)".
		}
	}

	if (lastStatementType == IF)
	{
		if ((lastSemicolonPos == -1 || statementPos > lastSemicolonPos) && statementPos > lastOpenBracePos)
		{ // if without {
			braces = IF_WITHOUT_BRACES;
		}
	}

	if (lastStatementType == FOREACH)
	{
		if ((lastSemicolonPos == -1 || statementPos > lastSemicolonPos) && statementPos > lastOpenBracePos)
		{ // foreach without {
			braces = FOREACH_WITHOUT_BRACES;
		}
	}

	if (lastStatementType == WHILE)
	{
		if ((lastSemicolonPos == -1 || statementPos > lastSemicolonPos) && statementPos > lastOpenBracePos)
		{ // while without {
			braces = WHILE_WITHOUT_BRACES;
		}
	}

	if (lastStatementType == DO)
	{
		if ((lastSemicolonPos == -1 || statementPos > lastSemicolonPos) && statementPos > lastOpenBracePos)
		{ // do without {
			braces = DO_WITHOUT_BRACES;
		}
	}

	if (lastStatementType == ELSE)
	{
		if ((lastSemicolonPos == -1 || statementPos > lastSemicolonPos) && statementPos > lastOpenBracePos)
		{ // else without {
			braces = ELSE_WITHOUT_BRACES;
		}
	}

	if (caseBraces == 1)
	{
		if (waitingForCaseColon)
			braces = CASE_BEFORE_COLON;
		// 		else
		// 			braces = CASE_WITHOUT_BRACES;
	}

	if (lastSemicolonPos > prevStatementPos || lastOpenBracePos > prevStatementPos)
		prevStatementPos = -1;

	if (lastSemicolonPos != -1 && lastCloseBracePos != -1)
	{
		if (lastSemicolonPos > lastOpenBracePos && lastSemicolonPos > lastCloseBracePos)
			return lastSemicolonPos;
		return lastOpenBracePos > lastCloseBracePos ? lastOpenBracePos : lastCloseBracePos;
	}

	if (lastSemicolonPos != -1)
		return lastSemicolonPos > lastOpenBracePos ? lastSemicolonPos : lastOpenBracePos;

	if (lastCloseBracePos != -1)
		return lastCloseBracePos > lastOpenBracePos ? lastCloseBracePos : lastOpenBracePos;

	return lastOpenBracePos;
}

// it returns "" if the selection is not a single variable/method call or a chain of the mentioned.
// Orange returns Orange, DoSomething() returns DoSomething, GetClass()->Apple returns Apple, while Orange + Apple
// returns an empty string, etc.
WTString IntroduceVariable::GetSingleOrChainSymbolName(WTString selection)
{
	int lastWordPos = 0;

	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	for (int i = 0; i < selection.GetLength(); i++)
	{
		if (commentSkipper.IsCode(selection[i]))
		{
			if (!IsWSorContinuation(selection[i]) && !ISCSYM(selection[i]))
			{
				// "chain" delimiter (. -> ::)
				if ((i + 1 < selection.GetLength() && selection[i] == '-' && selection[i + 1] == '>'))
				{
					i++;
					lastWordPos = i + 1;
					continue;
				}
				if ((i + 1 < selection.GetLength() && selection[i] == ':' && selection[i + 1] == ':'))
				{
					i++;
					lastWordPos = i + 1;
					continue;
				}
				if (selection[i] == '.')
				{
					lastWordPos = i + 1;
					continue;
				}

				// skipping between ()
				if (selection[i] == '(')
				{
					int pos = FindCorrespondingParen(selection, i);
					if (pos != -1)
					{
						i = pos;
						continue;
					}
				}

				// skipping between []
				if (selection[i] == '[')
				{
					int pos = FindCorrespondingParen(selection, i, '[', ']');
					if (pos != -1)
					{
						i = pos;
						continue;
					}
				}

				// skipping between <>
				if (selection[i] == '<')
				{
					int pos = FindCorrespondingParen(selection, i, '<', '>');
					if (pos != -1)
					{
						i = pos;
						continue;
					}
				}

				// something that can't fit in a single symbol/function call or a chain of them
				return ""; // do not offer name
			}
		}
	}

	// get symbol name
	WTString symbolName;
	for (int i = lastWordPos; i < selection.GetLength(); i++)
	{
		if (!IsWSorContinuation(selection[i]))
		{
			for (int j = i; j < selection.GetLength(); j++)
			{
				if (!ISCSYM(selection[j]))
					return symbolName;
				symbolName += selection[j];
			}
			break;
		}
	}

	return symbolName;
}

WTString IntroduceVariable::GetDefaultName(WTString selection)
{
	WTString symbolName = GetSingleOrChainSymbolName(selection);
	if (!symbolName.IsEmpty())
	{
		if (symbolName.GetLength() > 3 &&
		    (symbolName.Left(3) == "Get" || symbolName.Left(3) == "get" || symbolName.Left(3) == "GET"))
			symbolName = symbolName.Right(symbolName.GetLength() - 3);
		else if (symbolName.GetLength() > 2 &&
		         (symbolName.Left(2) == "Is" || symbolName.Left(2) == "is" || symbolName.Left(2) == "IS"))
			symbolName = symbolName.Right(symbolName.GetLength() - 2);
		else if (symbolName.GetLength() >= 2 && symbolName[0] == 'm' && symbolName[1] >= 'A' && symbolName[1] <= 'Z')
			symbolName = symbolName.Right(symbolName.GetLength() - 1);
		else if (symbolName.GetLength() >= 3 && symbolName[0] == 'm' && symbolName[1] == '_' && symbolName[2] >= 'A' &&
		         symbolName[2] <= 'Z')
			symbolName = symbolName.Right(symbolName.GetLength() - 2);
		else if (symbolName[0] >= 'A' && symbolName[0] <= 'Z')
		{
			LowerFirstChar(symbolName);
			return symbolName;
		}
		else
		{
			return "";
		}
		LowerFirstChar(symbolName);
	}

	return symbolName;
}

BOOL IntroduceVariable::InsertClosingBracketSimple(EdCntPtr ed, bool introduceInsideSelection, int& closeBracePos)
{
	WTString fileBuf = ed->GetBuf();

	long curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());

	if (!introduceInsideSelection)
	{
		// 		if (!CorrectCurPosToSemicolon(fileBuf, curPos))
		// 			return FALSE;
	}

	FreezeDisplay _f;
	_f.ReadOnlyCheck();

	const bool openBraceOnNewLine = Psettings->mInsertOpenBraceOnNewLine;
	int eos_guess = GuessEOS(fileBuf, curPos);
	int eos = ::FindInCode(fileBuf, ';', ed->m_ftype, curPos); // end of statement in which we selected the expression
	if (eos != -1 && (eos_guess == -1 || eos_guess > eos))
	{
		eos++; // next char after ;
		int insertClosingBrace = FindEOLThenEOC(eos, fileBuf);
		closeBracePos = insertClosingBrace;
		WTString codeToAdd = "}";
		std::pair<WTString, int> nextWord = GetNextWord(fileBuf, insertClosingBrace);
		if (!openBraceOnNewLine && nextWord.first == "else")
		{
			codeToAdd += " ";
			insertClosingBrace = nextWord.second - 4;
		}
		else
		{
			const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
			codeToAdd = implCodelnBrk + codeToAdd;
		}
		ed->SetPos((uint)insertClosingBrace);

		if (!introduceInsideSelection)
			_f.OffsetLine(1);
		return ed->InsertW(codeToAdd.Wide());
		// return gAutotextMgr->InsertAsTemplate(ed, codeToAdd, FALSE);
	}
	if (eos_guess != -1)
	{                // incomplete code, since a '}' or keyword like if, for, etc. was found earlier than a ;
		eos_guess--; // prev char before '}' or a keyword
		int insertClosingBrace = eos_guess; // FindEOLThenEOC(eos, fileBuf);
		closeBracePos = insertClosingBrace;
		WTString codeToAdd;
		if (gShellAttr->IsDevenv12OrHigher())
			codeToAdd = "}";
		else
			codeToAdd = "}\n";

		std::pair<WTString, int> nextWord = GetNextWord(fileBuf, insertClosingBrace);
		if (!openBraceOnNewLine && nextWord.first == "else")
		{
			codeToAdd += " ";
			insertClosingBrace = nextWord.second - 4;
		}
		else
		{
			const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
			codeToAdd = implCodelnBrk + codeToAdd;
		}
		ed->SetPos((uint)insertClosingBrace);
		if (!introduceInsideSelection)
			_f.OffsetLine(1);
		return ed->InsertW(codeToAdd.Wide());
		// return gAutotextMgr->InsertAsTemplate(ed, codeToAdd, FALSE);
	}

	return FALSE;
}

BOOL IntroduceVariable::InsertClosingBracketComplex(EdCntPtr ed, eCreateBraces braces, long selBegin,
                                                    int& closeBracePos, bool doFormat)
{
	WTString fileBuf = ed->GetBuf();
	long curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());

	// 	if (!CorrectCurPosToSemicolon(fileBuf, curPos))
	// 		return FALSE;

	// TODO: recalc statementPos and closingParenPos and delete from param
	std::pair<WTString, int> wordAndPos = GetNextWord(fileBuf, curPos);
	if (wordAndPos.first == "")
	{
		_ASSERTE(!"unexpected parsing error 3");
		return FALSE;
	}

	if (wordAndPos.first != "for" && wordAndPos.first != "if" && wordAndPos.first != "do" &&
	    wordAndPos.first != "while" && wordAndPos.first != "else")
	{
		_ASSERTE(!"unexpected parsing error 4 (Introduce Variable - no statement structure found)");
		return FALSE;
	}

	int structureEndPos = FindEndOfStructure(wordAndPos.second, fileBuf, braces);
	if (structureEndPos == -1)
		return FALSE;

	FreezeDisplay _f;
	_f.ReadOnlyCheck();

	int insertCloseBrace = structureEndPos + 1;
	WTString codeToAdd = "}";
	std::pair<WTString, int> nextWord = GetNextWord(fileBuf, structureEndPos + 1);
	if (!Psettings->mInsertOpenBraceOnNewLine && nextWord.first == "else")
	{
		codeToAdd += " ";
		insertCloseBrace = nextWord.second - 4;
	}
	else
	{
		const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
		codeToAdd = implCodelnBrk + codeToAdd;
	}
	ed->SetPos((uint)insertCloseBrace);
	_f.OffsetLine(1);
	// BOOL res = gAutotextMgr->InsertAsTemplate(ed, codeToAdd, FALSE); // formatting via TRUE doesn't work for every
	// test case (typing manually works for the same test case)
	BOOL res = ed->InsertW(codeToAdd.Wide());

	// select and do format selection manually via Edit.FormatSelection
	if (res)
	{
		// ed->SetSelection(selBegin, ed->CurPos());
		// ed->SendVamMessage(VAM_EXECUTECOMMAND, (long)_T("Edit.FormatSelection"), 0);
		long line = ed->LineFromChar((long)ed->CurPos());
		if (doFormat)
			ed->Reformat(-1, ed->LineFromChar(selBegin), -1, line);

		closeBracePos = ed->GetBufIndex(ed->LineIndex(line));
	}

	return res;
}

// Note that the eCreateBraces is used to specify the statement type (for/if/etc) so the "_WITHOUT_BRACES" at the end of
// the enum item names means nothing here. Returns -1 when error occurs
int IntroduceVariable::SkipStructureRecursive(const WTString& fileBuf, eCreateBraces braces, int closingParenPos)
{
	// we have 3 possible scenarios
	// 1. {								- just skip to the }
	// 2. statement						- just do a recursive call
	// 3. other: no { neither statement	- just find the ;

	// categorize based on the next non-comment char/word
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	WTString nextWord = "";
	int pos;
	for (pos = closingParenPos + 1; pos < fileBuf.GetLength(); pos++)
	{
		TCHAR charIter = fileBuf[pos];
		if (commentSkipper.IsCode(charIter))
		{
			if (charIter == '\\')
				continue;
			if (!IsWSorContinuation(charIter))
			{
				if (nextWord.GetLength() == 0)
				{
					nextWord += charIter;
					if (nextWord == "{" || !ISCSYM(charIter))
						break;
				}
				else
				{
					if (!ISCSYM(charIter))
						break;
					nextWord += charIter;
				}
			}
		}
	}

	if (nextWord.IsEmpty())
	{
		_ASSERTE(!"unexpected parsing error 1 (Introduce Variable, finding pos for '}')");
		return -1;
	}

	// case 1: { - find }
	if (nextWord == "{")
	{
		int correspondingBracePos = FindCorrespondingParen(fileBuf, pos, '{', '}');
		if (braces != IF_WITHOUT_BRACES)
		{
			return correspondingBracePos;
		}
		else
		{
			// check if we have an else branch
			std::pair<WTString, int> wordAndPos = GetNextWord(fileBuf, correspondingBracePos + 1);
			if (wordAndPos.first == "")
			{
				_ASSERTE(!"unexpected parsing error 2 (Introduce Variable, finding possible else)");
				return -1;
			}

			if (wordAndPos.first == "else")
			{
				return SkipStructureRecursive(fileBuf, ELSE_WITHOUT_BRACES, wordAndPos.second);
			}
			else
			{
				return correspondingBracePos;
			}
		}
	}

	// case 2: statement - recursion
	if (nextWord == "if" || nextWord == "for" || nextWord == "while" || nextWord == "do" || nextWord == "foreach")
	{
		// this supports C++/CLI "for each" as well since we'll find the '(' anyway
		int corresponingParenPos = FindCorrespondingParen(fileBuf, pos);
		if (corresponingParenPos == -1)
		{
			return FALSE;
		}

		return SkipStructureRecursive(fileBuf, IF_WITHOUT_BRACES, corresponingParenPos);
	}

	// case 3: other
	CommentSkipper commentSkipper2(ed->m_ftype);
	for (int i = closingParenPos + 1; i < fileBuf.GetLength(); i++)
	{
		TCHAR charIter = fileBuf[i];
		if (commentSkipper2.IsCode(charIter))
		{
			if (charIter == ';')
			{
				if (braces == IF_WITHOUT_BRACES)
				{
					std::pair<WTString, int> wordAndPos = GetNextWord(fileBuf, i + 1);
					if (wordAndPos.first == "")
					{
						return -1;
					}
					if (wordAndPos.first == "else")
						return SkipStructureRecursive(fileBuf, ELSE_WITHOUT_BRACES, wordAndPos.second);
				}

				return i;
			}
		}
	}

	return -1;
}

// returns the next word (or 1 non-alphabetical char) and its pos (pointing to the first non-alphabetical char after the
// word) return empty string if EOF reached and only whitespace and comment/strings where found.
std::pair<WTString, int> GetNextWord(const WTString& fileBuf, int pos)
{
	WTString nextWord;
	int i = 0;
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		CommentSkipper commentSkipper(ed->m_ftype);
		for (i = pos; i < fileBuf.GetLength(); i++)
		{
			TCHAR charIter = fileBuf[i];
			if (commentSkipper.IsCode(charIter))
			{
				if (charIter == '\\')
					continue;

				bool whiteSpace = IsWSorContinuation(charIter);
				if (nextWord.GetLength() == 0)
				{
					if (!whiteSpace)
						nextWord += charIter;
				}
				else
				{
					if (whiteSpace || !ISCSYM(charIter))
						break;
					nextWord += charIter;
				}
			}
		}
	}

	return std::pair<WTString, int>(nextWord, i);
}

WTString IntroduceVariable::ReadWordAhead(const WTString& fileBuf, int pos)
{
	WTString wordAhead;
	int i;
	for (i = pos; i < fileBuf.GetLength(); i++)
	{
		TCHAR charIter = fileBuf[i];
		if (charIter == '\\')
			continue;

		bool whiteSpace = IsWSorContinuation(charIter);
		if (whiteSpace || !ISCSYM(charIter))
			break;
		wordAhead += charIter;
	}

	return wordAhead;
}

bool IntroduceVariable::MovePosToEndOfStatement(WTString& fileBuf, int& pos)
{
	EdCntPtr ed(g_currentEdCnt);
	pos = ::FindInCode(fileBuf, ";", ed->m_ftype,
	                   pos); // workaround: the caret isn't always at the correct pos after InsertAsTemplate()

	int guessPos = GuessEOS(fileBuf, pos);
	if (pos == -1 && guessPos == -1)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"GetEndOfStatement error: invalid semi-colon pos");
		else
			vLog("ERROR: intro var - invalid semi-colon pos");
		return false;
	}
	if (guessPos < pos && guessPos != -1)
		pos = guessPos;
	return true;
}

BOOL IntroduceVariable::CorrectCurPosToSemicolon(WTString& fileBuf, int& curPos)
{
	if (fileBuf[curPos - 1] != ';')
	{
		if (!MovePosToEndOfStatement(fileBuf, curPos))
			return FALSE;
		curPos++; // next char after ; where the caret should have been after the InsertAsTemplate() call
	}

	return TRUE;
}

BOOL IntroduceVariable::IsSelectionValid(const WTString& sel, bool showError, int fileType)
{
	if (IsOnlyWhitespace(sel, showError, fileType))
		return FALSE;

	if (IsBracketMismatch(sel, showError))
		return FALSE;

	return TRUE;
}

BOOL IntroduceVariable::IsOnlyWhitespace(const WTString& sel, bool showError, int fileType)
{
	CommentSkipper cs(fileType);
	cs.NoStringSkip();
	for (int i = 0; i < sel.GetLength(); i++)
	{
		TCHAR iterChar = sel[i];
		if (cs.IsCode(iterChar) && cs.GetState() != CommentSkipper::COMMENT_MAY_START && !IsWSorContinuation(iterChar))
			return FALSE;
	}

	if (gTestLogger)
		gTestLogger->LogStrW(L"IntroduceVariable error: only comment and whitespace is selected");
	else if (showError)
		WtMessageBox("Introduce Variable: only comment and whitespace is selected.", IDS_APPNAME,
		             MB_OK | MB_ICONERROR);
	return TRUE;
}

BOOL IntroduceVariable::IsBracketMismatch(const WTString& sel, bool showError)
{
	int parens = 0;   // ()
	int braces = 0;   // {}
	int brackets = 0; // []
	EdCntPtr ed(g_currentEdCnt);
	int fileType = Src;
	if (ed)
		fileType = ed->m_ftype;
	CommentSkipper commentSkipper(fileType);
	for (int i = 0; i < sel.GetLength(); i++)
	{
		TCHAR iterChar = sel[i];
		if (commentSkipper.IsCode(iterChar))
		{
			// bracket counting
			if (iterChar == '(')
			{
				parens++;
				continue;
			}
			if (iterChar == '{')
			{
				braces++;
				continue;
			}
			if (iterChar == '[')
			{
				brackets++;
				continue;
			}
			if (iterChar == ')')
			{
				parens--;
				continue;
			}
			if (iterChar == '}')
			{
				braces--;
				continue;
			}
			if (iterChar == ']')
			{
				brackets--;
				continue;
			}

			// , outside of parens
			// 			if (iterChar == ',' && parens == 0 && braces == 0 && brackets == 0) {
			// 				if (gTestLogger)
			// 					gTestLogger->LogStrW(L"IntroduceVariable error: comma outside brackets");
			// 				else if (showError)
			// 					WtMessageBox("Introduce Variable: there is a comma in the selection, outside
			// of brackets.", IDS_APPNAME, MB_OK | MB_ICONERROR); 				return TRUE;
			// 			}
		}
	}

	bool res = parens || braces || brackets;
	if (res)
	{
		if (showError)
		{
			if (gTestLogger)
				gTestLogger->LogStrW(L"IntroduceVariable error: brace mismatch in selection");
			else
				WtMessageBox("Introduce Variable: brace mismatch in selection.", IDS_APPNAME,
				             MB_OK | MB_ICONERROR);
		}
		return res;
	}

	// Check if we have comma inside '<' and '>' operators but outside of template parameters, or in parens, brackets or
	// braces
	if (FindOutsideFunctionCall(sel, ',', fileType) != -1)
	{
		res = TRUE;
		if (showError)
		{
			if (gTestLogger)
				gTestLogger->LogStrW(L"IntroduceVariable error: comma outside template function call chain and outside "
				                     L"of parens, brackets, braces");
			else
				WtMessageBox("Introduce Variable: there is a comma in the selection", IDS_APPNAME,
				             MB_OK | MB_ICONERROR);
		}
	}

	return res;
}

// Guess end of statement for incomplete code (i.e. when the statement closing ';' is missing)
int IntroduceVariable::GuessEOS(const WTString& filebuf, int curPos)
{
	if (-1 == curPos)
		return -1;

	EdCntPtr ed(g_currentEdCnt);
	int eos = ::FindInCode(filebuf, '}', ed->m_ftype, curPos);
	if (eos == -1)
		eos = ::FindInCode(filebuf, "if", ed->m_ftype, curPos);
	if (eos == -1)
		eos = ::FindInCode(filebuf, "for", ed->m_ftype, curPos);
	if (eos == -1)
		eos = ::FindInCode(filebuf, "while", ed->m_ftype, curPos);
	if (eos == -1)
		eos = ::FindInCode(filebuf, "do", ed->m_ftype, curPos);
	if (eos == -1)
		eos = ::FindInCode(filebuf, "foreach", ed->m_ftype, curPos); // C#
	if (eos == -1)
		eos = ::FindInCode(filebuf, "for each", ed->m_ftype, curPos); // C++/CLI

	return eos;
}

WTString IntroduceVariable::RemoveWhiteSpaces(WTString selection)
{
	WTString res;
	for (int i = 0; i < selection.GetLength(); i++)
	{
		TCHAR c = selection[i];
		if (!IsWSorContinuation(c))
			res += c;
	}

	return res;
}

BOOL IntroduceVariable::ModifyInsertPosAndBracesIfNeeded(eCreateBraces& braces, int& insertPos, int statementPos,
                                                         int closingParenPos, long curPos, int prevStatementPos,
                                                         int caseBraces, const WTString& fileBuf, int& insertPos_brace)
{
	if (braces != NOCREATE)
	{
		if (braces == ELSE_WITHOUT_BRACES)
		{
			insertPos = statementPos + 4;
		}
		else
		{
			if (closingParenPos != -1)
			{
				if (closingParenPos < curPos)
				{ // are we outside a "for () |" or "if () |" or "while () |" or "do () |" statement?
					insertPos = closingParenPos + 1;
				}
				else
				{ // are we inside a "for (|)" or "if (|)" or "while (|)" or "do (|)" statement?
					InsideStatementParens = true;
					if (prevStatementPos == -1)
					{
						if (caseBraces != 1)
							braces = NOCREATE;
						// 							else
						// 								insertPos = closingParenPos + 1; // in case of a "case",
						// closingParenPos have the position of ':'
					}
					else
					{
						EdCntPtr ed(g_currentEdCnt);
						CommentSkipper commentSkipper(ed->m_ftype);
						std::pair<WTString, int> prevKeyword = GetNextWord(fileBuf, prevStatementPos);
						if (prevKeyword.first == "")
							return FALSE;
						if (prevKeyword.first == "else")
						{
							insertPos = prevStatementPos + 4;
						}
						else
						{
							int openParenPos = -1;
							for (int i = prevStatementPos; i < fileBuf.GetLength(); i++)
							{
								TCHAR charIter = fileBuf[i];
								if (commentSkipper.IsCode(charIter))
								{
									if (charIter == '(')
									{
										openParenPos = i;
										break;
									}
								}
							}

							if (openParenPos == -1)
							{
								return FALSE;
							}

							insertPos = FindCorrespondingParen(fileBuf, openParenPos) + 1;
							if (insertPos == -1)
							{
								return FALSE;
							}
						}
					}
				}
			}
		}
	}
	insertPos_brace = insertPos;
	// 	if (braces == ELSE_WITHOUT_BRACES)
	// 		insertPos_brace++;
	insertPos = FindClosestPos(insertPos, fileBuf);
	return TRUE;
}

bool IntroduceVariable::ShouldIntroduceInsideSelection(EdCntPtr ed, int insertPos, const WTString& fileBuf)
{
	bool codeBefore = false;
	bool codeAfter = false;
	long p1, p2;
	ed->GetSel2(p1, p2);
	const WTString bb(ed->GetBufConst());
	int selBegin = ed->GetBufIndex(bb, p1);
	int selEnd = ed->GetBufIndex(bb, p2);
	if (selBegin > selEnd)
		std::swap(selBegin, selEnd);
	CommentSkipper commentSkipperBefore(ed->m_ftype);
	for (int i = insertPos; i < selBegin; i++)
	{
		TCHAR c = fileBuf[i];
		if (commentSkipperBefore.IsCode(c) && commentSkipperBefore.GetState() != CommentSkipper::COMMENT_MAY_START &&
		    !IsWSorContinuation(c))
		{
			codeBefore = true;
			break;
		}
	}
	if (!codeBefore)
	{
		CommentSkipper commentSkipperAfter(ed->m_ftype);
		for (int i = selEnd; i < fileBuf.GetLength(); i++)
		{
			TCHAR c = fileBuf[i];
			if (c == ';' || c == '}')
				break;
			if (commentSkipperBefore.IsCode(c) && commentSkipperAfter.GetState() != CommentSkipper::COMMENT_MAY_START &&
			    !IsWSorContinuation(c))
			{
				codeAfter = true;
				break;
			}
		}
	}

	return codeBefore == false && codeAfter == false;
}

int IntroduceVariable::FindEndOfStructure(int pos, WTString& fileBuf, eCreateBraces braces)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper commentSkipper(ed->m_ftype);
	int openBracePos = -1;
	for (int i = pos; i < fileBuf.GetLength(); i++)
	{
		TCHAR charIter = fileBuf[i];
		if (commentSkipper.IsCode(charIter))
		{
			if (charIter == '(')
			{
				openBracePos = i;
				break;
			}
		}
	}

	if (openBracePos == -1)
	{
		_ASSERTE(!"unexpected parsing error 5 (Introduce Variable - while finding '(' after the statement)");
		return -1;
	}

	int closingParenPos = FindCorrespondingParen(fileBuf, openBracePos);
	if (closingParenPos == -1)
	{
		_ASSERTE(!"unexpected parsing error 6 (Introduce Variable - while finding corresponding paren)");
		return -1;
	}

	int structureEndPos = SkipStructureRecursive(fileBuf, braces, closingParenPos);
	return structureEndPos;
}

const char operators[] = {'+', '-', '*', '/', '%', '=', '!', '<', '>', '.', '&', '|', '^', '~', 0};

void IntroduceVariable::ExtendSelectionToIncludeParensWhenAppropriate(int searchFrom, const WTString& fileBuf)
{
	// acquire SELECTION positions
	EdCntPtr ed(g_currentEdCnt);
	long p1, p2;
	ed->GetSel2(p1, p2);
	const WTString bb(ed->GetBufConst());
	int selBegin = ed->GetBufIndex(bb, p1);
	int selEnd = ed->GetBufIndex(bb, p2);
	if (selBegin > selEnd)
		std::swap(selBegin, selEnd);

	// is there a paren on the LEFT side?
	int parenBeforePos;
	for (parenBeforePos = selBegin - 1; parenBeforePos > searchFrom; parenBeforePos--)
	{ // check backward
		TCHAR c = fileBuf[parenBeforePos];
		if (c == '(')
			goto isParenOnTheRight;
		if (!IsWSorContinuation(c))
			break;
	}

	return; // no '(' was found

isParenOnTheRight:

	// is there a paren on the RIGHT side?
	int parenAfterPos;
	for (parenAfterPos = selEnd;
	     parenAfterPos < fileBuf.GetLength() && fileBuf[parenAfterPos] != ';' && fileBuf[parenAfterPos] != '}';
	     parenAfterPos++)
	{ // check forward
		TCHAR c = fileBuf[parenAfterPos];
		if (c == ')')
			goto isOperatorBefore;
		if (!IsWSorContinuation(c))
			break;
	}

	return; // no ')' was found

isOperatorBefore:

	// is there an operator BEFORE the selection?
	for (int i = parenBeforePos - 1; i > searchFrom; i--)
	{ // check backward
		TCHAR c = fileBuf[i];
		if (c == '/' && i > 0 && fileBuf[i - 1] == '*') // comment end, do not extend selection
			goto isOperatorAfter;                       // no operator found, try the other side
		for (int j = 0; operators[j] != 0; j++)
		{
			if (operators[j] == c)
				goto checkWordBeforeSelection;
		}
		if (!IsWSorContinuation(c))
			goto isOperatorAfter; // no operator found, try the other side
	}

	// cycle ended: no operator found, try the other side

isOperatorAfter:

	// is there an operator AFTER the selection?
	{
		CommentSkipper cs(ed->m_ftype);
		for (int i = parenAfterPos + 1; i < fileBuf.GetLength() && fileBuf[i] != ';' && fileBuf[i] != '}'; i++)
		{ // check forward
			TCHAR c = fileBuf[i];
			if (cs.IsCode(c))
			{
				if (c == '/' && i < fileBuf.GetLength() && fileBuf[i + 1] == '*') // multi-line comment
					continue;
				if (c == '/' && i < fileBuf.GetLength() && fileBuf[i + 1] == '/') // single-line comment
					continue;
				for (int j = 0; operators[j] != 0; j++)
				{
					if (operators[j] == c)
						goto checkWordBeforeSelection;
				}
				if (!IsWSorContinuation(c)) // code must be an operator or a whitespace
					return;
			}
		}
	}

	return; // no operator found

checkWordBeforeSelection:

	WTString wordBeforeSelection;

	for (int i = parenBeforePos - 1; i > 0 && fileBuf[i] != ';' && fileBuf[i] != '{'; i--)
	{
		TCHAR c = fileBuf[i];
		if (IsWSorContinuation(c))
			continue;
		if (ISCSYM(c))
		{
			wordBeforeSelection = c + wordBeforeSelection;
			continue;
		}

		break;
	}

	if (wordBeforeSelection != "" && wordBeforeSelection != "return" && wordBeforeSelection != "co_return" &&
	    wordBeforeSelection != "co_yield")
		return;

	// EXTEND the selection
	ed->SetSel(static_cast<long>(parenBeforePos), static_cast<long>(parenAfterPos + 1));
}

bool IntroduceVariable::IsPrecedingCharacterAlphaNumerical(const WTString& fileBuf)
{
	// acquire SELECTION positions
	EdCntPtr ed(g_currentEdCnt);
	long p1, p2;
	ed->GetSel2(p1, p2);
	const WTString bb(ed->GetBufConst());
	int selBegin = ed->GetBufIndex(bb, p1);
	int selEnd = ed->GetBufIndex(bb, p2);
	if (selBegin > selEnd)
		std::swap(selBegin, selEnd);

	// check character
	return selBegin > 0 && ISCSYM(fileBuf[selBegin - 1]);
}

bool IntroduceVariable::IsEmptyBetween(int delete1, int delete2, const WTString& fileBuf)
{
	for (int i = delete1 + 1; i < delete2; i++)
	{
		if (!IsWSorContinuation(fileBuf[i]))
			return false;
	}

	return true;
}

int IntroduceVariable::CountWhiteSpacesBeforeFirstChar(long curPos, const WTString& fileBuf)
{
	// find EOL before curPos
	int pos = curPos;
	for (int i = curPos - 1; i > 0; i--)
	{
		if (fileBuf[i] == '\r' || fileBuf[i] == '\n')
			break;
		pos = i;
	}

	for (int i = pos; i < fileBuf.GetLength(); i++)
	{
		TCHAR c = fileBuf[i];
		if (!IsWSorContinuation(c) || c == '\r' || c == '\n')
			return i - pos;
	}

	return fileBuf.GetLength() - pos;
}

int IntroduceVariable::CountCharsToEOL(long curPos, WTString fileBuf)
{
	int count = 0;
	for (int i = curPos; i < fileBuf.GetLength(); i++)
	{
		if (fileBuf[i] == '\r' || fileBuf[i] == '\n')
			break;
		count++;
	}

	return count;
}

void IntroduceVariable::MoveCaretUntilNewLine(long curPos, int shift, WTString fileBuf)
{
	if (shift == 0)
		return;

	EdCntPtr ed(g_currentEdCnt);

	int newPos = curPos;
	if (shift > 0)
	{
		for (int i = curPos; i <= curPos + shift; i++)
		{
			newPos = i;
			if (fileBuf[i] == '\r' || fileBuf[i] == '\n')
				break;
		}
	}
	else
	{
		for (int i = curPos - 1; i >= curPos + shift; i--)
		{
			if (fileBuf[i] == '\r' || fileBuf[i] == '\n')
				break;
			newPos = i;
		}
	}
	ed->SetPos((uint)newPos);
}

bool IntroduceVariable::IsInsideCLambdaFunction(int mainFuncOpenBrace, int delete1, int delete2,
                                                const WTString& fileBuf, MultiParse* mp)
{
	int fileType = mp->FileType();
	if (!IsCFile(fileType))
		return false;

	CommentSkipper cs(fileType);
	enum class state
	{
		NONE,
		PAREN_FINDING,
	};

	// find closest lambda function
	state state = state::NONE;
	int lastLambdaPos = -1;
	for (int i = mainFuncOpenBrace; i < delete1; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (c == ']')
			{
				state = state::PAREN_FINDING;
				continue;
			}
			if (state == state::PAREN_FINDING && !IsWSorContinuation(c))
			{
				if (c == '(')
					lastLambdaPos = i;
				state = state::NONE;
			}
		}
	}

	if (lastLambdaPos == -1)
		return false;

	int corrPar = FindCorrespondingParen(fileBuf, lastLambdaPos);
	if (corrPar == -1)
		return false;

	// skip whitespaces and comments to find opening brace
	cs.Reset();
	for (int i = corrPar + 1; i < delete1; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			if (c == ';')
				return false;
			if (c == '{')
				return false;
		}
	}

	return true;
}

long IntroduceVariable::FindPosOutsideUniformInitializer(const WTString& fileBuf, int bracePos, long curPos)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed || ed->m_ftype == CS)
		return curPos;

	// find what's before the previous '{'. If more '{', keep going. If a '=', CSYM or a ']', we're inside an
	// initializer list / uniform initialization
	bool beforeOpen = false;
	CommentSkipper cs(Src);
	for (int i = curPos; i > bracePos; i--)
	{
		if (cs.IsCodeBackward(fileBuf, i))
		{
			TCHAR c = fileBuf[i];
			if (c == '{')
			{
				beforeOpen = true;
				continue;
			}
			if (beforeOpen)
			{
				if (IsWSorContinuation(c))
					continue;

				if (c == '{')
					continue;

				beforeOpen = false;
				if (c == '=' || ISCSYM(c) || c == ']') // initializer list / uniform initialization
					return i;
			}
			if (c == ';')
				return curPos;
		}
	}

	return curPos;
}
