#include "StdAfxEd.h"
#include "ConvertBetweenPointerAndInstance.h"
#include "EdCnt.h"
#include "StringUtils.h"
#include "UndoContext.h"
#include "FreezeDisplay.h"
#include "VAParse.h"
#include "TraceWindowFrame.h"
#include "CommentSkipper.h"
#include "DevShellService.h"
#include "VAAutomation.h"
#include "IntroduceVariable.h"
#include "RegKeys.h"
#include "VAWatermarks.h"

const int maxSearchSteps = 4096;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

BEGIN_MESSAGE_MAP(ConvertBetweenPointerAndInstanceDlg, UpdateReferencesDlg)
ON_BN_CLICKED(IDC_CHECK_USE_AUTO_WITH_CONVERT, OnToggleUseAuto)
ON_BN_CLICKED(IDC_RADIO_CONVERT_RAW, OnRawPtr)
ON_BN_CLICKED(IDC_RADIO_CONVERT_UNIQUE_PTR, OnUniquePtr)
ON_BN_CLICKED(IDC_RADIO_CONVERT_SHARED_PTR, OnSharedPtr)
ON_BN_CLICKED(IDC_RADIO_CONVERT_CUSTOM_SMART_PTR, OnCustomPtr)
ON_EN_CHANGE(IDC_EDIT_CUSTOM_TYPE, OnChangeEditCustomType)
END_MESSAGE_MAP()

WTString GetLeftSymName(WTString complexType)
{
	CommentSkipper cs(Src);
	for (int i = 0; i < complexType.GetLength(); i++)
	{
		auto c = complexType[i];
		if (cs.IsCode(c))
		{
			if (ISCSYM(c))
				continue;
			return complexType.Left(i);
		}
	}

	return complexType;
}

CStringW GetRightType(const WTString& buf, const ConversionInfo& Info, long symPos)
{
	if (!Info.CallName.IsEmpty()) // for "auto obj = convertclass58(61);" type of stuff. See VAAutoTest:Convert58
		return Info.CallName.Wide();

	long startPos;
	if (Info.NewPos != -1)
		startPos = Info.NewPos + 3; // the length of "new"
	else
		startPos = symPos;

	WTString typeName;
	CommentSkipper cs(Src);
	bool skipWhitespaces = true;
	int counter = 0;
	for (int i = startPos; i < buf.GetLength(); i++)
	{
		if (counter++ > maxSearchSteps)
			return "";
		auto c = buf[i];
		if (cs.IsCode(c))
		{
			if (skipWhitespaces)
			{
				if (IsWSorContinuation(c))
					continue;
				else
					skipWhitespaces = false;
			}

			if (c == '(') // supporting "new (int)" format: '(' does not necessarily means the end of the type - it can
			              // be the BEGINNING of it
			{
				if (typeName.GetLength())
					break;
				else
					continue;
			}
			if (c == ';' || c == '{' || c == '}' || c == ')')
				break;
			typeName += c;
		}
	}

	typeName.TrimRight();
	return typeName.Wide();
}

WTString GetType_SkipWSs(long& typeFrom, long getPos, long symPos, const WTString& buf, long& typeTo)
{
	bool symName = false;
	typeFrom = getPos + 1;
	CommentSkipper cs2(Src);
	for (long j = getPos + 1; j < symPos - 1; j++)
	{
		auto c2 = buf[(uint)j];
		bool isCode = cs2.IsCode(c2);
		if (isCode && c2 != '/')
		{
			if (ISCSYM(c2))
			{
				if (!symName)
				{
					symName = true;
					typeFrom = j;
				}
				continue;
			}
			if (IsWSorContinuation(c2))
			{
				symName = false;
				continue;
			}
			break;
		}
	}

	typeTo = symPos;
	while (typeTo - 1 >= 0 && buf[uint(typeTo - 1)] != '>' && !ISCSYM(buf[uint(typeTo - 1)]))
		typeTo--;

	return buf.Mid((int)typeFrom, int(typeTo - typeFrom));
}

WTString GetLeftType(long symPos, const WTString& buf, long& typeFrom, long& typeTo)
{
	CommentSkipper cs(Src);
	int counter = 0;
	for (uint i = uint(symPos - 1); (int)i >= 0; i--)
	{
		if (counter++ > maxSearchSteps)
			return "";
		auto c = buf[i];
		if (cs.IsCodeBackward(buf, (int)i))
		{
			// for
			if (c == 'r' && int(i - 3) >= 0 && buf[i - 1] == 'o' && buf[i - 2] == 'f' && !ISCSYM(buf[i - 3]) &&
			    int(i + 1) < buf.GetLength() && !ISCSYM(buf[i + 1]))
			{
				// find '('
				CommentSkipper cs2(Src);
				int counter2 = 0;
				for (uint j = i; j < (uint)buf.GetLength(); j++)
				{
					if (counter2++ > maxSearchSteps)
						return "";
					auto c2 = buf[j];
					if (cs2.IsCode(c2)) // skipping any possible comments between the for and the '('
					{
						if (c2 == '(')
						{
							return GetType_SkipWSs(typeFrom, (int)j, symPos, buf, typeTo);
						}
						if (c2 == '{' || c == '}') // unexpected character after a "for"
						{
							return "";
						}
					}
				}
				break;
			}
			// while
			if (c == 'e' && int(i - 5) >= 0 && buf[i - 1] == 'l' && buf[i - 2] == 'i' && buf[i - 3] == 'h' &&
			    buf[i - 4] == 'w' && !ISCSYM(buf[i - 5]) && int(i + 1) < buf.GetLength() && !ISCSYM(buf[i + 1]))
			{
				return "while";
			}
			// if
			if (c == 'f' && int(i - 2) >= 0 && buf[i - 1] == 'i' && !ISCSYM(buf[i - 2]) &&
			    int(i + 1) < buf.GetLength() && !ISCSYM(buf[i + 1]))
			{
				return "if";
			}
			if (c == ';' || c == '{' || c == '}')
			{
				return GetType_SkipWSs(typeFrom, (int)i, symPos, buf, typeTo);
			}
		}
	}

	return "";
}

WTString GetWordIfPresentBeforeSym(const WTString& buf, long symPos, long& pos)
{
	CommentSkipper cs(Src);
	int counter = 0;
	for (int i = symPos - 1; i >= 0; i--)
	{
		if (counter++ > maxSearchSteps)
		{
			pos = 0;
			return "";
		}
		auto c = buf[i];
		if (cs.IsCodeBackward(buf, i))
		{
			if (IsWSorContinuation(c))
				continue;

			WTString word;
			int counter2 = 0;
			for (int j = i; j >= 0; j--)
			{
				if (counter2++ > 16) // this function is to recognize keywords such as "delete" which are short
				{
					pos = j;
					return word;
				}
				auto c2 = buf[j];
				if (IsWSorContinuation(c2))
				{
					pos = j + 1;
					return word;
				}
				word = c2 + word;
			}
		}
	}

	pos = 0;
	return "";
}

// returns '.' or -> or ''
WTString GetDotOrArrowIfPresentAfterSym(const WTString& buf, long symPos, WTString symName, long& pos)
{
	CommentSkipper cs(Src);
	int counter = 0;
	for (int i = symPos + symName.GetLength(); i < buf.GetLength(); i++)
	{
		if (counter++ > maxSearchSteps)
		{
			pos = 0;
			return "";
		}
		auto c = buf[i];
		if (cs.IsCode(c))
		{
			if (IsWSorContinuation(c))
				continue;
			if (c == '.')
			{
				pos = i;
				return ".";
			}
			if (c == '-' && i + 1 < buf.GetLength() && buf[i + 1] == '>')
			{
				pos = i;
				return "->";
			}
			break;
		}
	}

	pos = 0;
	return "";
}

eEndPosCorrection CorrectEndPosOfDelete(const WTString& buf, long& endPos, bool doNotFindEOL)
{
	enum class eState
	{
		FINDING_SEMICOLON,
		FINDING_NEWLINE,
	};

	CommentSkipper cs(Src);
	eState state = eState::FINDING_SEMICOLON;
	int counter = 0;
	long semicolonPos = 0;
	for (int i = endPos; i < buf.GetLength(); i++)
	{
		if (counter++ > maxSearchSteps)
			return eEndPosCorrection::NONE; // no correction

		auto c = buf[i];
		if (cs.IsCode(c) && c != '/')
		{
			switch (state)
			{
			case eState::FINDING_SEMICOLON:
				if (IsWSorContinuation(c))
					continue;
				if (c == ';')
				{
					semicolonPos = i;
					if (doNotFindEOL)
					{
						endPos = i;
						return eEndPosCorrection::FOUND_SEMICOLON;
					}
					state = eState::FINDING_NEWLINE;
					continue;
				}
				return eEndPosCorrection::NONE; // no correction
			case eState::FINDING_NEWLINE:
				if (c == '\r')
				{
					if (i + 1 < buf.GetLength() && buf[i + 1] == '\n')
					{
						endPos = i + 2;
						return eEndPosCorrection::FOUND_EOL;
					}
					endPos = i + 1;
					return eEndPosCorrection::FOUND_EOL;
				}
				if (c == '\n')
				{
					endPos = i + 1;
					return eEndPosCorrection::FOUND_EOL;
				}
				if (IsWSorContinuation(c))
					continue;
				endPos = i;
				return eEndPosCorrection::FOUND_SEMICOLON;
			}

			return eEndPosCorrection::NONE; // no correction
		}
	}

	return eEndPosCorrection::NONE; // no correction
}

// returns '' or '&' or '*'
WTString GetStarOrAddressIfPresentBeforeSym(const WTString& buf, long symPos, long& pos)
{
	CommentSkipper cs(Src);
	int counter = 0;
	for (int i = symPos - 1; i >= 0; i--)
	{
		if (counter++ > maxSearchSteps)
		{
			pos = 0;
			return "error"; // empty string would be a valid result
		}
		auto c = buf[i];
		if (cs.IsCodeBackward(buf, i))
		{
			if (IsWSorContinuation(c))
				continue;
			if (c == '&')
			{
				pos = i;
				return "&";
			}
			if (c == '*')
			{
				pos = i;
				return "*";
			}
			pos = i + 1;
			return "";
		}
	}

	pos = 0;
	return "error"; // empty string would be a valid result
}

UpdateReferencesDlg::UpdateResult ConvertDefinitionSiteToInstance(long symPos, const WTString& buf,
                                                                  const ConversionInfo& Info)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return UpdateReferencesDlg::rrError;

	bool noDelete = false;
	// remove address if needed
	if (Info.AddressPos != -1)
	{
		noDelete = true;
		ed->SetSel(Info.AddressPos, Info.AddressPos + 1);
		if (!ed->ReplaceSelW("", noFormat))
			return UpdateReferencesDlg::rrError;
	}

	// add star if needed
	bool doNotPasteParams = false; // to be used to share info between this and the next (see:#cutInitAndPaste) session
	if (Info.AddressPos == -1 && Info.PointerPos != -1 && Info.NewPos == -1 && Info.EqualitySign != -1)
	{
		noDelete = true;
		enum class eState
		{
			SYM,
			WS1,
			EQUAL,
			WS2,
			SYM2
		};
		eState state = eState::SYM;
		CommentSkipper cs(Src);
		int castParens = 0; // counting parens to skip casts
		for (long i = symPos; i < buf.GetLength(); i++)
		{
			auto c = buf[(uint)i];
			if (cs.IsCode(c))
			{
				switch (state)
				{
				case eState::SYM:
					if (ISCSYM(c))
						continue;
					state = eState::WS1;
					// fall-through
				case eState::WS1:
					if (IsWSorContinuation(c))
						continue;
					state = eState::EQUAL;
					// fall-through
				case eState::EQUAL:
					if (c == '=')
					{
						state = eState::WS2;
						continue;
					}
					return UpdateReferencesDlg::rrNoChange; // ERROR
				case eState::WS2:
					if (IsWSorContinuation(c))
						continue;
					state = eState::SYM2;
				case eState::SYM2:
					// case 113194
					if (c == '(')
					{
						castParens++;
						break;
					}
					if (c == ')')
					{
						castParens--;
						break;
					}
					if (castParens > 0)
						break;

					if (!ISCSYM(c) && c != '*')
						return UpdateReferencesDlg::rrNoChange;

					// normally, we dereference variables. but we need to make sure we're not dereferencing nullptr or
					// NULL read the next word
					WTString nextWord;
					int counter2 = 0;
					for (int j = i; j < buf.GetLength(); j++)
					{
						if (counter2++ > maxSearchSteps)
							break;
						TCHAR c2 = buf[(uint)j];
						if (!ISCSYM(c2))
							break;
						nextWord += c2;
					}

					if (nextWord == "nullptr" || nextWord == "NULL" || nextWord == "0")
					{
						// we're reusing code - instead of deleting code here, we let existing code does that so the
						// identification of boundaries done by a tested code and will include future fixes this 2
						// variables are to control the next session (see:#cutInitAndPaste)
						doNotPasteParams = true;
						noDelete = false;

						goto success;
					}

					// put the star
					ed->SetSel(i, i);
					if (!ed->ReplaceSelW("*", noFormat))
						return UpdateReferencesDlg::rrError;

					goto success;
				}
			}
		}

		return UpdateReferencesDlg::rrNoChange; // ERROR
	success:;
	}

	// get the initialization value and put after the instance name in parens #cutInitAndPaste
	// ----------------------------------------------------------------------
	{
		CommentSkipper cs(Src);
		long selectFrom = 0;
		long selectTo = 0;
		int parens = 0;
		long parensBegin = 0;
		long parensEnd = 0;
		//		int star = (Info.PointerPos == -1 ? 0 : 1);
		enum class eBraceInit
		{
			NOT,
			WS_AFTER_EQUAL,
			BRACE_INIT,
		};
		eBraceInit braceInit = eBraceInit::NOT;
		int counter = 0;
		WTString pre;
		for (auto i = symPos; i < buf.GetLength(); i++)
		{
			if (counter++ > maxSearchSteps)
				return UpdateReferencesDlg::rrNoChange; // ERROR. VA's "cannot modify file" would be confusing to use
				                                        // here.
			auto c = buf[(uint)i];
			if (cs.IsCode(c))
			{
				if (braceInit == eBraceInit::WS_AFTER_EQUAL && !IsWSorContinuation(c))
				{
					if (c == '{')
					{
						braceInit = eBraceInit::BRACE_INIT;
						continue;
					}
					else
					{
						braceInit = eBraceInit::NOT;
					}
				}
				if (braceInit == eBraceInit::BRACE_INIT)
				{
					if (c == '}')
					{
						braceInit = eBraceInit::NOT;
						continue;
					}
				}
				if (c == '=' && parensBegin == 0)
				{
					selectFrom = i;
					while (selectFrom >= 0 && IsWSorContinuation(buf[(uint)selectFrom]))
						selectFrom--;
					braceInit = eBraceInit::WS_AFTER_EQUAL;
				}
				if (c == ';')
				{
					selectTo = i;
					break;
				}
				if (c == '{' || c == '}')
				{
					return UpdateReferencesDlg::rrNoChange; // ERROR: unexpected character before ';' when it's not a
					                                        // brace init. VA's "cannot modify file" would be confusing
					                                        // to use here.
				}
				if (c == '(')
				{
					if (pre == "" || pre == "new")
					{
						int corr = IntroduceVariable::FindCorrespondingParen(buf, i);
						if (corr != -1)
						{
							i = corr;
							pre = ")";
							continue;
						}
					}

					if (parens == 0)
						parensBegin = i;
					parens++;
				}
				if (c == ')')
				{
					parens--;
					if (parens == 0)
						parensEnd = i + 1;
				}
				if (ISCSYM(c))
				{
					if (i > 0 && !ISCSYM(buf[uint(i - 1)]))
						pre = "";
					pre += c;
				}
			}
		}

		CStringW constructorParams;
		if (parensBegin && parensEnd)
		{
			ed->SetSel(parensBegin, parensEnd);
			constructorParams = ed->GetSelString().Wide();
			CStringW nullCheck = constructorParams;
			if (nullCheck.GetLength() >= 2)
			{
				if (nullCheck[0] == '(')
					nullCheck = nullCheck.Mid(1);
				if (nullCheck.Right(1) == ')')
					nullCheck = nullCheck.Left(nullCheck.GetLength() - 1);
				nullCheck.Trim();
				if (nullCheck == "nullptr" || nullCheck == "NULL")
				{
					if (!ed->ReplaceSelW("", noFormat))
						return UpdateReferencesDlg::rrError;
				}
			}
		}

		if (!selectFrom || !selectTo || noDelete)
			goto deletePointer; // no default value

		ed->SetSel(selectFrom - 1, selectTo);
		if (!ed->ReplaceSelW("", noFormat))
			return UpdateReferencesDlg::rrError;
		if (constructorParams.GetLength() && constructorParams != "()" && !doNotPasteParams)
			ed->ReplaceSelW(constructorParams, noFormat);
	}

deletePointer:

	// delete pointer and make sure we leave a space
	// ---------------------------------------------
	if (Info.PointerPos != -1)
	{
		ed->SetSel(Info.PointerPos, Info.PointerPos + 1);
		CStringW replace;
		if (Info.PointerPos > 0 && !IsWSorContinuation(buf[uint(Info.PointerPos - 1)]) &&
		    !IsWSorContinuation(buf[uint(Info.PointerPos + 1)]))
			replace = L" ";

		if (!ed->ReplaceSelW(replace, noFormat))
			return UpdateReferencesDlg::rrError;
	}

	// replace if auto with real type
	// ------------------------------
	long typeFrom = 0;
	long typeTo = 0;
	WTString discardedName = GetLeftType(symPos, buf, typeFrom, typeTo);
	if (discardedName == "auto")
	{
		CStringW typeName = GetRightType(buf, Info, symPos); // the real one after "new"
		ed->SetSel(typeFrom, typeTo);
		if (!ed->ReplaceSelW(typeName, noFormat))
			return UpdateReferencesDlg::rrError;
	}

	return UpdateReferencesDlg::rrSuccess;
}

BOOL ConvertBetweenPointerAndInstance::CanConvert(bool simplify)
{
	if (!IsCPPFileAndInsideFunc())
		return FALSE;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	DType dt(ed->GetSymDtype());
	if (dt.IsEmpty())
		return FALSE;

	if (dt.type() != VAR)
		return FALSE;

	WTString fileBuf = ed->GetBuf();
	const long curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
	if (!IsPointerOrInstanceVariable(fileBuf, curPos, simplify))
		return FALSE;

	// [case: 113195]
	const WTString def(dt.Def());
	if (-1 != def.Find("ptr"))
	{
		if (-1 != def.Find("shared_ptr") || -1 != def.Find("unique_ptr") || -1 != def.Find("weak_ptr"))
			return FALSE;
	}

	if (-1 != def.Find("make_"))
	{
		if (-1 != def.Find("make_shared") || -1 != def.Find("make_unique"))
			return FALSE;
	}

	return TRUE;
}

// #refactor_convert check if definition: IsPointerOrInstanceVariable()
bool ConvertBetweenPointerAndInstance::IsPointerOrInstanceVariable(const WTString& fileBuf, long pos, bool simplify)
{
	// find first non-ws backward if caret is on ';' or ws. The analisys will continue from there
	if (fileBuf[(uint)pos] == ';' || IsWSorContinuation(fileBuf[(uint)pos]))
	{
		int i;
		int counter = 0;
		for (i = pos - 1; i >= 0; i--)
		{
			if (counter++ > maxSearchSteps)
				return false;

			if (!IsWSorContinuation(fileBuf[(uint)i]))
				break;
		}

		pos = i;
	}

	// is there ; on the right?
	enum eRightState
	{
		VARNAME_FIRST,
		VARNAME,
		WS,
		POST_EQUAL,
	};

	eRightState rstate = VARNAME_FIRST;
	int counter = 0;
	CommentSkipper cs(Src);
	long opEqual = 0;
	int braces = 0;
	int parens = 0;
	for (long i = pos; i < fileBuf.GetLength(); i++)
	{
		if (counter++ > maxSearchSteps) // just to avoid unexpected performance issues with an unexpected edge case
			return false;

		TCHAR c = fileBuf[(uint)i];
		if (cs.IsCode(c))
		{
			switch (rstate)
			{
			case VARNAME_FIRST:
				if (ISCSYM(c))
				{
					rstate = VARNAME;
					continue;
				}
				return false;
			case VARNAME:
				if (ISCSYM(c))
					continue;
				rstate = WS;
				// fall-through
			case WS:
				if (IsWSorContinuation(c))
					continue;
				if (c == '=')
				{
					Info.EqualitySign = i;
					opEqual = i;
					rstate = POST_EQUAL;
					continue;
				}
				if (c == ';' || c == '(')
					goto leftCheck;
				return false;
			case POST_EQUAL:
				if (c == '{')
				{
					braces++;
					continue;
				}
				if (c == '}')
				{
					braces--;
					if (braces < 0)
						goto leftCheck;
					continue;
				}
				if (c == '(')
				{
					parens++;
					continue;
				}
				if (c == ')')
				{
					parens--;
					continue;
				}
				if (braces || parens)
					continue;
				if (c == ';')
					goto leftCheck;
				if (c == ',')
					return false;
				if (simplify)
				{
					if (!ISCSYM(c) && !IsWSorContinuation(c) && c != ':')
						return false;
				}
				break;
			default:
				return false;
			}
		}
	}

	return false;

leftCheck:

	// is there a variable on the left? an identifier with skipping possible '*', and between <>s
	enum eLeftState
	{
		SYM,
		WS1,
		STAR,
		WS2,
		ANGLEPARENS,
		WS3_FIRST,
		WS3,
		TYPE,
	};

	eLeftState lstate = SYM;
	counter = 0;
	int angleCount = 0;
	bool pointer = false;
	cs.Reset();
	for (uint i = uint(pos - 1); (int)i >= 0; i--)
	{
		if (counter++ > maxSearchSteps) // just to avoid unexpected performance issues with an unexpected edge case
			return false;

		TCHAR c = fileBuf[i];
		if (cs.IsCodeBackward(fileBuf, (int)i))
		{
			switch (lstate)
			{
			case SYM:
				if (ISCSYM(c))
					continue;
				lstate = WS1;
				// fall-through
			case WS1:
				if (IsWSorContinuation(c))
					continue;
				lstate = STAR;
				// fall-through
			case STAR:
				if (c == '*')
				{
					pointer = true;
					Info.PointerPos = (int)i;
					continue;
				}
				lstate = WS2;
				// fall-through
			case WS2:
				if (IsWSorContinuation(c))
					continue;
				lstate = ANGLEPARENS;
				// fall-through
			case ANGLEPARENS:
				if (c == '>')
				{
					angleCount++;
					continue;
				}
				if (c == '<')
				{
					angleCount--;
					if (angleCount == 0)
						lstate = WS3;
					continue;
				}
				if (angleCount)
					continue;
				lstate = WS3;
				// fall-through
			case WS3:
				if (IsWSorContinuation(c))
					continue;
				lstate = TYPE;
				// fall-through
			case TYPE:
				if (ISCSYM(c))
				{
					if (opEqual && pointer)
					{
						CommentSkipper cs2(Src);
						int counter2 = 0;
						for (long j = opEqual; j < fileBuf.GetLength(); j++)
						{
							if (counter2++ > maxSearchSteps)
								return false;
							auto c2 = fileBuf[(uint)j];
							if (cs2.IsCode(c2))
							{
								if (c2 == ';' || c2 == '}')
									// return false;
									return true;
								// if (c2 == 'n' && j + 3 < fileBuf.GetLength() && fileBuf[j+1] == 'e' && fileBuf[j+2]
								// == 'w' && IsWSorContinuation(fileBuf[j-1]) && IsWSorContinuation(fileBuf[j+3]))
								// return
								// true;
								if (c2 == '&')
								{
									Info.AddressPos = j;
									return true;
								}
							}
						}
						// return false;
						return true;
					}

					// delete is a keyword, not a type
					if (c == 'e' && int(i - 6) >= 0 && fileBuf[i - 1] == 't' && fileBuf[i - 2] == 'e' &&
					    fileBuf[i - 3] == 'l' && fileBuf[i - 4] == 'e' && fileBuf[i - 5] == 'd' &&
					    !ISCSYM(fileBuf[i - 6]))
						return false;

					// new is a keyword, not a type
					if (c == 'w' && int(i - 3) >= 0 && fileBuf[i - 1] == 'e' && fileBuf[i - 2] == 'n' &&
					    !ISCSYM(fileBuf[i - 3]))
						return false;

					// if we find "return" or '=', our found '*' is not a pointer declaration but a multiplication
					// expression case 112991
					if (pointer)
					{
						CommentSkipper cs2(Src);
						int counter2 = 0;
						for (uint j = i; (int)j >= 0; j--)
						{
							if (counter2++ > maxSearchSteps)
								return false;
							auto c2 = fileBuf[j];
							if (cs2.IsCode(c2))
							{
								if (c2 == ';' || c2 == '}' || c2 == '{')
									break;
								if (c2 == '=')
									return false;
								//									int fbLen = fileBuf.GetLength();
								auto isKeyword = [&](const WTString& word) {
									if (int(j + 1) < fileBuf.GetLength())
									{
										if (!IsWSorContinuation(fileBuf[j + 1]))
											return false;
										uint wordLength = (uint)word.GetLength();
										uint start = j - wordLength + 1;
										for (uint h = 0; h < wordLength; h++)
										{
											if (fileBuf[start + h] != word[h])
												return false;
										}
										if (!IsWSorContinuation(fileBuf[j - wordLength]))
											return false;

										return true;
									}

									return false;
								};

								if (isKeyword("return"))
									return false;
								if (isKeyword("co_return"))
									return false;
								if (isKeyword("co_yield"))
									return false;
							}
						}
					}
					return true;
				}
				return false;
			default:
				return false;
			}
		}
	}

	return false;
}

// #refactor_convert class entry point: Convert()
BOOL ConvertBetweenPointerAndInstance::Convert(DType* sym, VAScopeInfo_MLC& info, WTString& methodScope,
                                               CStringW& filePath, bool simplify)
{
	UndoContext undoContext("VA Convert Between Pointer and Instance");
	FreezeDisplay f(FALSE);
	TraceScopeExit tse("Convert Between Pointer and Instance exit");

	if (!sym)
		return FALSE;

	if (!CanConvert(simplify))
		return FALSE;

	UpdateTypeOfMethodCall();
	FigureOutConversionType(simplify);

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	{
		WTString tmp(ed->GetSymScope());
		if (tmp.IsEmpty())
			return FALSE;

		tmp = ::StrGetSymScope(tmp);
		MultiParsePtr mp = ed->GetParseDb();
		DType* ptr = mp->FindExact(tmp);
		if (ptr && ::IS_OBJECT_TYPE(ptr->type()))
		{
			// [case: 112990]
			return false;
		}
	}

	WTString buf = ed->GetBuf();
	long curPos = ed->GetBufIndex(buf, (long)ed->CurPos());
	long symPos = curPos;
	while (symPos - 1 >= 0 && ISCSYM(buf[uint(symPos - 1)]))
		symPos--;

	long typeFrom = 0;
	long typeTo = 0;
	CStringW leftTypeName = GetLeftType(symPos, buf, typeFrom, typeTo).Wide();
	if (leftTypeName == L"while")
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"Convert pointer to instance error: are we in a 'while'?");
		else
			WtMessageBox("Conversion is not supported in the condition of a 'while' loop.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);

		return FALSE;
	}
	if (leftTypeName == L"if")
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"Convert pointer to instance error: are we in an 'if'?");
		else
			WtMessageBox("Conversion is not supported in the condition of an 'if' statement.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);

		return FALSE;
	}

	if (leftTypeName == L"auto" &&
	    ((Info.CallName.IsEmpty() && Info.Type == eConversionType::INSTANCE_TO_POINTER) || Info.TypeOfCall == FUNC))
	{
		if (gTestLogger)
			gTestLogger->LogStrW(
			    L"Convert is not supported in this scenario. The type is auto but we don't know if it's a pointer.");
		else
			WtMessageBox("Conversion of 'auto' is not supported in this context.", IDS_APPNAME,
			             MB_OK | MB_ICONERROR);

		return FALSE;
	}

	if (Info.Type == eConversionType::POINTER_TO_INSTANCE && Info.TypeOfCall == FUNC)
	{
		if (gTestLogger)
			gTestLogger->LogStrW(L"Function call warning dialog is skipped in AST");
		else
		{
			if (IDNO ==
			    WtMessageBox("The variable is initialized by a function call.  Depending upon the implementation of "
			                 "the function, conversion may result in problems like dangling pointers.\n\n"
			                 "Do you want to continue?",
			                 IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
			{
				return FALSE;
			}
		}
	}

	if (Info.Type == eConversionType::POINTER_TO_INSTANCE && Info.NewPos != -1)
	{
		ConvertBetweenPointerAndInstance::eNewProblem newProblem = IsThereNewProblem(buf, symPos);
		if (newProblem == eNewProblem::NO_PROBLEM)
		{
			CStringW rightTypeName = GetRightType(buf, Info, symPos);
			if (FindInCode(rightTypeName, "[", Src, 0) != -1)
				newProblem = eNewProblem::ARRAY;
			else if (FindInCode(rightTypeName, "+", Src, 0) != -1)
				newProblem = eNewProblem::OPERATOR;
			else if (FindInCode(rightTypeName, "-", Src, 0) != -1)
				newProblem = eNewProblem::OPERATOR;
		}
		switch (newProblem)
		{
		case ConvertBetweenPointerAndInstance::eNewProblem::NO_PROBLEM:
			break;
		case ConvertBetweenPointerAndInstance::eNewProblem::PLACEMENT_PARAM:
			if (gTestLogger)
				gTestLogger->LogStrW(L"Convert pointer to instance error: new placement param detected");
			else
				WtMessageBox("Conversion of placement new is not supported.", IDS_APPNAME,
				             MB_OK | MB_ICONERROR);

			return FALSE;
			break;
		case ConvertBetweenPointerAndInstance::eNewProblem::OPERATOR:
			if (gTestLogger)
				gTestLogger->LogStrW(L"Convert pointer to instance error: operator detected");
			else
				WtMessageBox("Conversion is not supported in this context due to the use of an operator in the "
				             "variable declaration.",
				             IDS_APPNAME, MB_OK | MB_ICONERROR);

			return FALSE;
			break;
		case ConvertBetweenPointerAndInstance::eNewProblem::ARRAY:
			if (gTestLogger)
				gTestLogger->LogStrW(L"Convert pointer to instance error: array detected");
			else
				WtMessageBox("Conversion of arrays is not supported.", IDS_APPNAME, MB_OK | MB_ICONERROR);

			return FALSE;
			break;
		default:
			break;
		}
	}
	if (Info.Type == eConversionType::POINTER_TO_INSTANCE && Info.NewPos != -1)
	{
		CStringW rightTypeName = GetRightType(buf, Info, symPos);
		if (GetLeftSymName(rightTypeName) != GetLeftSymName(leftTypeName) && leftTypeName != L"auto")
		{
			if (gTestLogger)
				gTestLogger->LogStrW(
				    L"Convert pointer to instance error: different left and right typename. Is it a derived class?");
			else
				WtMessageBox("Conversion is not supported in this context due to the use of multiple types (for "
				             "example, via a cast).",
				             IDS_APPNAME, MB_OK | MB_ICONERROR);

			return FALSE;
		}
	}

	if (Simplify)
	{
		ConvertDefinitionSiteToInstance(symPos, buf, Info);
	}
	else
	{
		if (DoModal(std::make_shared<DType>(sym)) == FALSE)
			return FALSE;
	}

	return TRUE;
}

// #refactor_convert which way? get conversion type
eConversionType ConvertBetweenPointerAndInstance::FigureOutConversionType(bool tryToSimplify)
{
	// call CanConvert() if needed which fills up Info.PointerPos
	if (Info.Type == eConversionType::UNINITIALIZED)
	{
		BOOL c = CanConvert(tryToSimplify);
		ASSERT(c);
		std::ignore = c;

		// figure out the direction of the conversion. For that, we need Info.NewPos besides Info.PointerPos
		EdCntPtr ed(g_currentEdCnt);
		WTString buf = ed->GetBuf();
		long curPos = ed->GetBufIndex(buf, (long)ed->CurPos());
		CommentSkipper cs(Src);
		for (uint i = (uint)curPos; i < (uint)buf.GetLength(); i++)
		{
			auto c2 = buf[i];
			if (cs.IsCode(c2))
			{
				if (c2 == ';')
					break;
				if (c2 == 'n' && int(i + 3) < buf.GetLength() && buf[i + 1] == 'e' && buf[i + 2] == 'w' &&
				    IsWSorContinuation(buf[i + 3]) && IsWSorContinuation(buf[i - 1]))
				{
					Info.NewPos = (int)i;
					break;
				}
			}
		}

		if (tryToSimplify)
		{
			bool pointer1 = Info.IsPointer(false);
			bool pointer2 = Info.IsPointer(true);
			if (!pointer1 && pointer2)
				Info.Type = eConversionType::POINTER_TO_INSTANCE;
			else
				Info.Type = eConversionType::INSTANCE_TO_POINTER;
		}
		else
		{
			if (Info.IsPointer(false))
				Info.Type = eConversionType::POINTER_TO_INSTANCE;
			else
				Info.Type = eConversionType::INSTANCE_TO_POINTER;
		}
	}

	return Info.Type;
}

BOOL ConvertBetweenPointerAndInstance::DoModal(DTypePtr sym)
{
	ConvertBetweenPointerAndInstanceDlg dlg(uint(Info.Type == eConversionType::POINTER_TO_INSTANCE
	                                                 ? IDD_CONVERT_POINTER_TO_INSTANCE
	                                                 : IDD_CONVERT_INSTANCE_TO_POINTER));
	if (!dlg.Init(sym, Info))
		return FALSE;

	dlg.DoModal();

	return dlg.IsStarted(); // dlg.DoModal() always returns IDCANCEL (2)
}

ConvertBetweenPointerAndInstance::eNewProblem ConvertBetweenPointerAndInstance::IsThereNewProblem(const WTString& buf,
                                                                                                  long symPos)
{
	ASSERT(Info.NewPos != -1);

	enum class eState
	{
		WS1,
		OPEN_PAREN,
		WS2,
	};

	eState state = eState::WS1;
	CommentSkipper cs(Src);
	int counter = 0;
	bool notVar = false;
	for (int i = Info.NewPos + 3; i < buf.GetLength(); i++)
	{
		if (counter++ > maxSearchSteps)
			return eNewProblem::NO_PROBLEM;

		auto c = buf[i];
		if (cs.IsCode(c))
		{
			switch (state)
			{
			case eState::WS1:
				if (!IsWSorContinuation(c))
				{
					if (c == '(')
					{
						// open paren
						int corresponding = IntroduceVariable::FindCorrespondingParen(buf, i);
						if (corresponding == -1)
							return eNewProblem::NO_PROBLEM;
						WTString name = buf.Mid(i + 1, corresponding - (i + 1));
						name.Trim();

						EdCntPtr ed(g_currentEdCnt);
						if (!ed)
							return eNewProblem::NO_PROBLEM;

						MultiParsePtr mp = ed->GetParseDb();
						DType* ptr = mp->FindAnySym(name);
						if (ptr)
						{
							uint mt = ptr->MaskedType();
							if (mt != VAR)
								notVar = true;
						}

						i = corresponding;
						state = eState::WS2;
					}
					else
						return eNewProblem::NO_PROBLEM;
				}
				break;
			case eState::WS2:
				if (!IsWSorContinuation(c))
				{
					if (c == '(' || ISCSYM(c))
					{
						if (notVar)
							return eNewProblem::NO_PROBLEM;
						else
							return eNewProblem::PLACEMENT_PARAM;
					}
					else if (c == '+' || c == '-')
					{
						return eNewProblem::OPERATOR;
					}
					else
					{
						return eNewProblem::NO_PROBLEM;
					}
				}
				break;
			default:
				break;
			}
		}
	}

	return eNewProblem::NO_PROBLEM;
}

bool ConvertBetweenPointerAndInstance::IsThereParenAfterEqual(WTString& name)
{
	name = "";
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;

	WTString buf = ed->GetBuf();
	long curPos = ed->GetBufIndex(buf, (long)ed->CurPos());

	CommentSkipper cs(Src);
	bool afterEquality = false;
	bool dotOrArrow = false;
	int angleBrackets = 0; // template support. see VAAutoTest:Convert89
	for (int i = curPos; i < buf.GetLength(); i++)
	{
		auto c = buf[i];
		if (cs.IsCode(c))
		{
			if (c == '=' || (!afterEquality && c == '('))
			{
				afterEquality = true;
				continue;
			}
			if (c == '<')
			{
				angleBrackets++;
				name += c;
				continue;
			}
			if (c == '>')
			{
				angleBrackets--;
				name += c;
				continue;
			}
			if (angleBrackets > 0)
			{
				name += c;
				continue;
			}
			if (c == '(' && afterEquality)
			{
				// is there any dot or arrow after the paren? we need to find the last '(' in a chain call to see if
				// it's a function call or just a variable
				int closeParen = IntroduceVariable::FindCorrespondingParen(buf, i);
				if (closeParen != -1)
				{
					CommentSkipper cs2(Src);
					for (int j = closeParen + 1; j < buf.GetLength(); j++)
					{
						auto c2 = buf[j];
						if (cs2.IsCode(c2))
						{
							if (IsWSorContinuation(c2))
								continue;
							if (c2 == '.' || (c2 == '-' && j + 1 < buf.GetLength() && buf[j + 1] == '>'))
							{
								Info.Chain = true;
								goto next_i;
							}
							break;
						}
					}
				}

				if (dotOrArrow)
				{
					name = ".";
					return true;
				}
				name.Trim();
				while (name.GetLength() && name[0] == ':')
					name = name.Mid(1);
				if (name == "")
					continue;
				else
					return true;
			}
			if (afterEquality)
			{
				if (c == '.' || (c == '-' && i + 1 < buf.GetLength() && buf[i + 1] == '>'))
					dotOrArrow = true;
				else if (!ISCSYM(c) && !IsWSorContinuation(c) && c != '>')
					dotOrArrow = false;

				if (ISCSYM(c) || c == ':')
				{
					name += c;
				}
				else
				{
					if (!IsWSorContinuation(c) && c != ')')
						name = "";
					if (!name.IsEmpty() && name == "new")
						name = "";
				}
			}
			if (c == ';' || c == '{' || c == '}')
				return false;
		}
	next_i:;
	}

	return false;
}

void ConvertBetweenPointerAndInstance::UpdateTypeOfMethodCall()
{
	WTString name;
	if (!IsThereParenAfterEqual(name))
		return;

	if (name == "sizeof")
		return;

	if (name == '.')
	{
		Info.TypeOfCall = FUNC;
		return;
	}

	Info.CallName = name; // we can use the name even if it is not a func

	name = TokenGetField(name, "<"); // cut of the template arguments for FindAnySym

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	int sc = name.ReverseFind("::");
	if (sc != -1)
	{
		sc += 2;
		name = name.Right(name.GetLength() - sc);
	}

	MultiParsePtr mp = ed->GetParseDb();
	DType* ptr = mp->FindAnySym(name);
	if (ptr)
	{
		Info.TypeOfCall = (int)ptr->MaskedType();
	}
}

ConvertBetweenPointerAndInstanceDlg::ConvertBetweenPointerAndInstanceDlg(uint dlgID)
    : UpdateReferencesDlg("ConvertBetweenPointerAndInstanceDlg", dlgID, nullptr,
                          Psettings->mIncludeProjectNodeInRenameResults, true)
{
	mEditTypeName = nullptr;
	SetHelpTopic("dlgConvertInstance");
}

ConvertBetweenPointerAndInstanceDlg::~ConvertBetweenPointerAndInstanceDlg()
{
}

BOOL ConvertBetweenPointerAndInstanceDlg::Init(DTypePtr sym, ConversionInfo info)
{
	if (!sym)
		return FALSE;

	m_symScope = sym->SymScope();
	mInfo = info;

	return TRUE;
}

void ConvertBetweenPointerAndInstanceDlg::OnChangeEditCustomType()
{
	// get the latest text even when thread running
	if (mEditTypeName_subclassed.GetSafeHwnd())
	{
		CStringW txt;
		mEditTypeName_subclassed.GetText(txt);
		mEditTypeNameText = txt;
	}

	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return; // don't refresh while thread is still running

	BOOL ok = ValidateInput();

	CButton* pRenameBtn = (CButton*)GetDlgItem(IDC_RENAME);
	if (pRenameBtn)
		pRenameBtn->EnableWindow(ok);
}

void ConvertBetweenPointerAndInstanceDlg::UpdateStatus(BOOL done, int fileCount)
{
	WTString msg;
	if (!done)
	{
		msg.WTFormat("Searching...");
	}
	else if (mFindRefsThread && mFindRefsThread->IsStopped() && mRefs->Count())
		msg.WTFormat("Search canceled before completion.  U&pdate references to %s at your own risk.",
		             mRefs->GetFindSym().c_str());
	else
	{
		if (mError.IsEmpty())
		{
			const WTString sym(StrGetSym(m_symScope));
			GetStatusMessageViaRadios(msg);
		}
	}

	if (msg.GetLength())
		::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());

	mStatusText = msg;
}

void ConvertBetweenPointerAndInstanceDlg::GetStatusMessageViaRadios(WTString& msg)
{
	DWORD type = 0;
	if (mInfo.Type == eConversionType::INSTANCE_TO_POINTER)
		type = Psettings->mConvertToPointerType;

	switch (type) // see:#convert_pointer_types
	{
	case 0: {
		if (mRefs->Count())
		{
			const WTString sym(StrGetSym(m_symScope));
			if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE)
				msg.WTFormat("Select references to change or remove.");
			else
				msg.WTFormat(R"(You must add a call to "delete %s" or otherwise manage the lifetime of "%s".)",
				             sym.c_str(), sym.c_str());
		}
		break;
	}
	case 1:
		msg = " ";
		break;
	case 2:
		msg = " ";
		break;
	case 3:
		msg = " ";
		break;
	}
}

BOOL ConvertBetweenPointerAndInstanceDlg::OnInitDialog()
{
	__super::OnInitDialog();

	if (mInfo.Type == eConversionType::INSTANCE_TO_POINTER)
	{
		if (GetDlgItem(IDC_EDIT_CUSTOM_TYPE))
			mEditTypeName_subclassed.SubclassWindow(GetDlgItem(IDC_EDIT_CUSTOM_TYPE)->m_hWnd);
		mEditTypeName = &mEditTypeName_subclassed;
	}

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHECK_USE_AUTO_WITH_CONVERT, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_CONVERT_RAW, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_CONVERT_UNIQUE_PTR, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_CONVERT_SHARED_PTR, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_CONVERT_CUSTOM_SMART_PTR, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedStaticNormal>(IDC_STATIC_CONVERT_POINTER_TYPE, this);
		Theme.AddDlgItemForDefaultTheming(IDC_STATIC_OF_TYPE);

		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	if (mInfo.Type == eConversionType::INSTANCE_TO_POINTER)
	{
		((CButton*)GetDlgItem(IDC_CHECK_USE_AUTO_WITH_CONVERT))->SetCheck(Psettings->mUseAutoWithConvertRefactor);
		if (strlen(Psettings->mConvertCustomPtrName))
			mEditTypeName_subclassed.EnableWindow(!Psettings->mUseAutoWithConvertRefactor);
		UpdateCustomEditBox();
		RefreshRadiosViaConvertToPointerType();
		if (CEdit* edit = (CEdit*)GetDlgItem(IDC_EDIT1))
		{
			WTString makeName = Psettings->mConvertCustomMakeName;
			::SetWindowTextW(GetDlgItem(IDC_EDIT1)->GetSafeHwnd(), makeName.Wide());

			// if (strnlen_s(Psettings->mConvertCustomPtrName, MAX_PATH))
			WTString typeName = Psettings->mConvertCustomPtrName;
			if (mEditTypeName_subclassed.GetSafeHwnd())
				mEditTypeName_subclassed.SetText(typeName.Wide());
		}
		SetWindowText("Convert Instance to Pointer");
	}
	else
	{
		SetWindowText("Convert Pointer to Instance");
	}

	if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE)
	{
		CTreeCtrl* tree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
		if (tree)
			tree->SetFocus();
	}
	else
	{
		switch (Psettings->mConvertToPointerType) // see:#convert_pointer_types
		{
		case 1:
			if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_UNIQUE_PTR))
				button->SetFocus();
			break;
		case 2:
			if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_SHARED_PTR))
				button->SetFocus();
			break;
		case 3:
			if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_CUSTOM_SMART_PTR))
				if (mEdit)
					mEdit->SetFocus();
			break;
		case 0:
		default:
			if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_RAW))
				button->SetFocus();
			break;
		}
	}

	VAUpdateWindowTitle(VAWindowType::ConvertBetweenPointerAndInstance, *this);

	return FALSE;
}

void ConvertBetweenPointerAndInstanceDlg::OnToggleUseAuto()
{
	Psettings->mUseAutoWithConvertRefactor = !Psettings->mUseAutoWithConvertRefactor;
	mEditTypeName_subclassed.EnableWindow(!Psettings->mUseAutoWithConvertRefactor);

	// workaround: without this, the editbox doesn't get enabled until hovered by mouse pointer or focused by shift+tab
	if (!Psettings->mUseAutoWithConvertRefactor)
	{
		mEditTypeName_subclassed.SetFocus();
		(CButton*)GetDlgItem(IDC_CHECK_USE_AUTO_WITH_CONVERT)->SetFocus();
	}
}

void ConvertBetweenPointerAndInstanceDlg::OnRawPtr()
{
	Psettings->mConvertToPointerType = 0; // see:#convert_pointer_types
	UpdateCustomEditBox();
	UpdateStatus(TRUE, -1);
}

void ConvertBetweenPointerAndInstanceDlg::OnUniquePtr()
{
	Psettings->mConvertToPointerType = 1; // see:#convert_pointer_types
	UpdateCustomEditBox();
	UpdateStatus(TRUE, -1);
}

void ConvertBetweenPointerAndInstanceDlg::OnSharedPtr()
{
	Psettings->mConvertToPointerType = 2; // see:#convert_pointer_types
	UpdateCustomEditBox();
	UpdateStatus(TRUE, -1);
}

void ConvertBetweenPointerAndInstanceDlg::OnCustomPtr()
{
	Psettings->mConvertToPointerType = 3; // see:#convert_pointer_types
	UpdateCustomEditBox();
	UpdateStatus(TRUE, -1);
}

void ConvertBetweenPointerAndInstanceDlg::SetErrorStatus(const WTString& msg)
{
	// if (mFindRefsThread && mFindRefsThread->IsRunning())
	//	return;

	if (msg == mStatusText)
		return;

	mStatusText = msg;
	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
}

bool ConvertBetweenPointerAndInstanceDlg::IsTemplateType(WTString symName)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return true;

	MultiParsePtr mp = ed->GetParseDb();
	DType* ptr = mp->FindAnySym(symName);
	if (ptr)
		return !!ptr->IsTemplate();

	return true;
}

long ConvertBetweenPointerAndInstanceDlg::FindHigherThanStarPrecedenceAfterSym(const WTString& buf, long symPos)
{
	int counter = 0;
	long symEndPos;
	for (symEndPos = symPos; symEndPos < buf.GetLength(); symEndPos++)
	{
		if (counter++ > MAX_PATH) // we really shouldn't expect longer sym name than a path
			return 0;
		if (!ISCSYM(buf[(uint)symEndPos]))
			break;
	}

	// since marking sym with * in case of ++ or -- after breaks code, we really want to make sure we don't accidentally
	// miss one. so first, we skip comments to support cases such as "symName /*awkwardly located comment*/ ++" so we
	// can use proper parens, i.e.
	// *(symname /*awkwardly located comment*/ ++)
	counter = 0;
	CommentSkipper cs(Src);
	for (long i = symEndPos; i < buf.GetLength(); i++)
	{
		if (counter > maxSearchSteps) // to long comment or too much white spaces
			return 0;

		auto c = buf[(uint)i];
		if (cs.IsCode(c) && c != '/')
		{
			if (IsWSorContinuation(c))
				continue;
			if (c == '+')
			{
				if (i + 1 < buf.GetLength() && buf[uint(i + 1)] == '+') // here we go
					return i;                                           // the pos of "++", where the paren will close
			}
			if (c == '-')
			{
				if (i + 1 < buf.GetLength() && buf[uint(i + 1)] == '-') // here we go
					return i;                                           // the pos of "--", where the paren will close
			}
			if (IsWSorContinuation(c))
				continue;
			return 0;
		}
	}

	return 0;
}

void ConvertBetweenPointerAndInstanceDlg::OnDestroy()
{
	if (mEditTypeName_subclassed.m_hWnd)
		mEditTypeName_subclassed.UnsubclassWindow();

	mEditTypeName = nullptr;

	__super::OnDestroy();
}

BOOL ConvertBetweenPointerAndInstanceDlg::ValidateInput()
{
	if (mError.GetLength())
	{
		if (gTestLogger)
			gTestLogger->LogStrW(mError.Wide());
		SetErrorStatus(mError);
		return FALSE;
	}

	if (mInfo.Type == eConversionType::INSTANCE_TO_POINTER &&
	    Psettings->mConvertToPointerType == 3) // see:#convert_pointer_types
	{
		if (mEditTxt.IsEmpty() && mEditTypeNameText.IsEmpty())
		{
			return FALSE;
		}
		int length = mEditTxt.GetLength();
		for (int i = 0; i < length; i++)
		{
			auto editChar = mEditTxt[i];
			if (!ISCSYM(editChar) && editChar != ':') // allow :: in case user needs to define a namespace
			{
				return FALSE;
			}
		}
		length = mEditTypeNameText.GetLength();
		for (int i = 0; i < length; i++)
		{
			auto editChar = mEditTypeNameText[i];
			if (!ISCSYM(editChar) && editChar != ':') // allow :: in case user needs to define a namespace
			{
				return FALSE;
			}
		}

		strcpy_s(Psettings->mConvertCustomMakeName, MAX_PATH, mEditTxt.c_str());
		strcpy_s(Psettings->mConvertCustomPtrName, MAX_PATH, mEditTypeNameText.c_str());

		if (CButton* b = (CButton*)GetDlgItem(IDC_CHECK_USE_AUTO_WITH_CONVERT))
		{
			if (strlen(Psettings->mConvertCustomPtrName) == 0)
			{
				b->SetCheck(true);
				Psettings->mUseAutoWithConvertRefactor = true;
				b->EnableWindow(false);
			}
			else
			{
				if (!b->IsWindowEnabled())
				{
					b->EnableWindow(true);
					b->SetCheck(false);
					Psettings->mUseAutoWithConvertRefactor = false;
				}
			}
		}
	}
	else
	{
		if (CButton* b = (CButton*)GetDlgItem(IDC_CHECK_USE_AUTO_WITH_CONVERT))
			b->EnableWindow(true);
	}

	return TRUE;
}

void ConvertBetweenPointerAndInstanceDlg::RegisterReferencesControlMovers()
{
	AddSzControl(IDCANCEL, mdRepos, mdRepos);
}

void ConvertBetweenPointerAndInstanceDlg::RegisterRenameReferencesControlMovers()
{
	AddSzControl(IDC_RENAME, mdRepos, mdRepos);
}

void ConvertBetweenPointerAndInstanceDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		try
		{
			WTString logStr;
			logStr.WTFormat("Convert results from(%s) to(%s) auto(%d) fileCnt(%d) refCnt(%zu) canceled(%d)\r\n",
			                m_symScope.c_str(), mEditTxt.c_str(), mAutoUpdate, fileCount, mRefs->Count(), wasCanceled);
			gTestLogger->LogStr(logStr);
			gTestLogger->LogStrW(mRefs->GetSummary(GetFilter()));
		}
		catch (...)
		{
			gTestLogger->LogStr(WTString("Exception logging Convert results."));
		}
	}

	SearchResultsForIssues();

	__super::OnSearchComplete(fileCount, wasCanceled);

	if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE)
	{
		CTreeCtrl* tree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
		if (tree)
			tree->SetFocus();
	}
}

bool ConvertBetweenPointerAndInstanceDlg::IsSymOrNumBefore(const WTString& buf, long pos)
{
	CommentSkipper cs(Src);
	for (int i = pos - 1; i >= 0; i--)
	{
		if (cs.IsCodeBackward(buf, i))
		{
			auto c = buf[i];
			if (IsWSorContinuation(c))
				continue;

			if (ISCSYM(c) || c == '.') // dot needed because 5. is also a valid number
			{
				return true;
			}
			break;
		}
	}

	return false;
}

void ConvertBetweenPointerAndInstanceDlg::SearchResultsForIssues(HTREEITEM item /*= TVI_ROOT*/)
{
	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		uint data = (uint)m_tree.GetItemData(childItem);
		if (!IS_FILE_REF_ITEM(data))
		{
			FindReference* ref = mRefs->GetReference(data);
			if (ref)
			{
				WTString ln = ref->lnText;
				int lnLength = ln.GetLength();
				int startPos = ln.Find("):");
				int symPos = startPos;
				if (symPos != -1) // if we can't find the beginning, just fall back to previous behavior
				{
					symPos += 2; // skipping "):"
					symPos += ref->lineIdx + 3;
				}

				// pointer re-assignment error
				if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE && ref->type == FREF_ReferenceAssign)
				{
					if (symPos != -1) // if we can't find the beginning, just fall back to previous behavior
					{
						for (int i = symPos - 1; i >= 0; i--)
						{
							auto c = ln[i];
							if (c == '*')
								goto notAssignment;
							if (!IsWSorContinuation(c) && c != '\x4')
								break; // the first character we've found backward is not a '*', so go ahead with the
								       // error
						}
					}

					mNrOfAssignments++;
					mError = "";
					mError = GetFileNameTruncated(ref->file);
					mError += "(";
					mError += itoa((int)ref->lineNo);
					mError += "): Error: Cannot convert because of assignment to pointer after its declaration.";
				notAssignment:;
				}

				// dangling pointers error regarding return keyword
				if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE && ref->mData)
				{
					WTString symName = ref->mData->Sym();
					ref->file;
					ref->lineNo;
					int retPos = FindWholeWordInCode(ln, "return", Src, 0);
					size_t retLen = strlen("return");
					if (retPos == -1)
					{
						retPos = FindWholeWordInCode(ln, "co_return", Src, 0);
						retLen = strlen("co_return");
					}
					if (retPos == -1)
					{
						retPos = FindWholeWordInCode(ln, "co_yield", Src, 0);
						retLen = strlen("co_yield");
					}
					if (retPos != -1)
					{
						CommentSkipper cs(Src);
						for (int i = retPos + (int)retLen; i < ln.GetLength(); i++)
						{
							auto c = ln[i];
							if (cs.IsCode(c))
							{
								if (IsWSorContinuation(c) || c == '\x1' || c == '\x3')
									continue;
								if (IsSubStringPlusNonSymChar(ln, symName, i))
								{
									int symNameLen = symName.GetLength();
									bool minusFound = false;
									for (int j = i + symNameLen; j < ln.GetLength(); j++)
									{
										TCHAR c2 = ln[j];
										if (IsWSorContinuation(c2) || c2 == '\x1')
											continue;
										if (c2 == '-')
											minusFound = true;
										break;
									}
									if (!minusFound) // to avoid false positives with cases like "return obj->A;". see
									                 // also VAAutoTest:Convert08 and 09.
									{
										mStackAddressReturns++;
										mError = "";
										mError = GetFileNameTruncated(ref->file);
										mError += "(";
										mError += itoa((int)ref->lineNo);
										mError += "): Error: Converting return of pointer to return of address of "
										          "local instance would result in a dangling pointer.";
									}
								}
								break;
							}
						}
					}
				}

				// removing star from *name++ would change code - conversion is not possible
				// first, the pointer is increased and than the pointer is dereferenced. increasing the pointer part has
				// no equivalent with instance variable.
				if (mInfo.Type == eConversionType::POINTER_TO_INSTANCE && symPos != -1 && ref->mData)
				{
					bool starBefore = false;
					for (int i = symPos - 1; i >= startPos; i--)
					{
						auto c = ln[i];
						if (c == '*')
						{
							starBefore = true;
							break;
						}
						if (!IsWSorContinuation(c) && c != '\x4' && c != '\x3')
							break; // the previous char we've found after skipping whitespaces is not a '*'
					}

					bool plusPlusFound = false;
					bool closingParen = false;
					WTString symName = ref->mData->Sym();
					for (int i = symPos + symName.GetLength(); i < lnLength; i++)
					{
						auto c = ln[i];
						if ((c == '+' && i + 1 < lnLength && ln[i + 1] == '+') ||
						    (c == '-' && i + 1 < lnLength && ln[i + 1] == '-'))
						{
							plusPlusFound = true;
							break;
						}
						if (c == ')')
						{
							closingParen = true;
							continue;
						}
						if (!IsWSorContinuation(c) && c != '\x4' && c != '\x1')
						{
							break; // the next string we've found after skipping symbol name and whitespaces is not a
							       // "++"
						}
					}
					if ((plusPlusFound && !closingParen) || (plusPlusFound && closingParen && !starBefore))
					{
						mError = "";
						mError = GetFileNameTruncated(ref->file);
						mError += "(";
						mError += itoa((int)ref->lineNo);
						mError += "): Error: Cannot convert pointer used in increment / decrement operation";
					}
				}
			}
		}

		if (m_tree.ItemHasChildren(childItem))
			SearchResultsForIssues(childItem);

		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void ConvertBetweenPointerAndInstanceDlg::RefreshRadiosViaConvertToPointerType()
{
	switch (Psettings->mConvertToPointerType) // see:#convert_pointer_types
	{
	case 0:
		if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_RAW))
		{
			button->SetCheck(true);
			// button->SetFocus();
		}
		break;
	case 1:
		if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_UNIQUE_PTR))
		{
			button->SetCheck(true);
			// button->SetFocus();
		}
		break;
	case 2:
		if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_SHARED_PTR))
		{
			button->SetCheck(true);
			// button->SetFocus();
		}
		break;
	case 3:
		if (CButton* button = (CButton*)GetDlgItem(IDC_RADIO_CONVERT_CUSTOM_SMART_PTR))
		{
			button->SetCheck(true);
		}
		break;
	}

	OnChangeEdit();
	UpdateStatus(TRUE, -1);
}

void ConvertBetweenPointerAndInstanceDlg::UpdateCustomEditBox()
{
	if (mEdit && mEditTypeName)
	{
		if (Psettings->mConvertToPointerType == 3) // see:#convert_pointer_types
		{
			mEdit->ShowWindow(TRUE);
			mEditTypeName->ShowWindow(TRUE);
			((CStatic*)GetDlgItem(IDC_STATIC_OF_TYPE))->ShowWindow(TRUE);
			mEdit->SetFocus();
		}
		else
		{
			mEdit->ShowWindow(FALSE);
			mEditTypeName->ShowWindow(FALSE);
			((CStatic*)GetDlgItem(IDC_STATIC_OF_TYPE))->ShowWindow(FALSE);
		}
		ValidateInput(); // make sure we enable/disable "auto" checkbox correctly
	}

	if (mError.IsEmpty())
	{
		WTString msg;
		GetStatusMessageViaRadios(msg);

		if (msg.GetLength())
		{
			::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
			mStatusText = msg;
		}
	}
}

// #refactor_convert converting: UpdateReference()
UpdateReferencesDlg::UpdateResult ConvertBetweenPointerAndInstanceDlg::UpdateReference(int refIdx, FreezeDisplay& _f)
{
	FindReference* curRef = mRefs->GetReference((uint)refIdx);

	if (curRef->mData && curRef->mData->IsMethod())
		return rrSuccess; // nothing to do here

	if (!mRefs->GotoReference(refIdx))
		return rrError;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	const WTString symName = ed->GetSelString();
	if (symName != mRefs->GetFindSym())
		return rrError;

	_f.ReadOnlyCheck();

	if (curRef->type == FREF_Unknown || curRef->type == FREF_Comment ||
	    curRef->type == FREF_IncludeDirective /*|| curRef->type == FREF_Definition*/)
		return rrNoChange;

	FREF_TYPE refType = curRef->type;
	// 	if (curRef->lineNo == (ULONG)mSym->Line()) // it shouldn't happen: sometimes VA thinks a definition is a
	// reference (see VAAutoTest:EncField0013) 		refType = FREF_Definition;

	WTString buf = ed->GetBuf(TRUE);
	long symPos = (long)curRef->GetEdBufCharOffset(ed, symName, buf);

	switch (mInfo.Type)
	{
	case eConversionType::INSTANCE_TO_POINTER: {
		if (refType == FREF_Definition || refType == FREF_DefinitionAssign) // '' to '*' (for left side of '=')
		{
			// First, harvest the TYPE. Everything before the symbol name until a ';', '{' or '}' is considered part of
			// the symbol name.
			long typeFrom = 0;
			long typeTo = 0;
			WTString leftType = GetLeftType(symPos, buf, typeFrom, typeTo);
			if (leftType == "")
				return rrNoChange;
			WTString rightType = GetRightType(buf, mInfo, symPos);
			bool rightTypeSpecified = false;
			if (leftType == rightType || leftType == "auto")
				rightTypeSpecified = true;

			// Secondly, harvest the VALUES and detect the area to delete
			enum eHarvestValues
			{
				SEEKING,
				OP_EQUAL,
				PARENS,
				FINISHING,
			};
			eHarvestValues harvesting = SEEKING;

			CommentSkipper cs(Src);
			WTString values;
			long valueSelFrom = 0;
			long valueSelTo = 0;
			int counter = 0;
			bool fullCopy = false; // VAAutoTest:Convert69
			bool findingStar = false;
			long removeStar = 0;
			int parens = 0;
			for (long i = symPos; i < buf.GetLength(); i++)
			{
				if (counter++ > maxSearchSteps)
					return rrNoChange;
				auto c = buf[(uint)i];
				if (cs.IsCode(c))
				{
					if (c == '=' && harvesting == SEEKING)
					{
						harvesting = OP_EQUAL;
						findingStar = true;
						valueSelFrom = i;
						while (valueSelFrom > 0 && IsWSorContinuation(buf[uint(valueSelFrom - 1)]))
							valueSelFrom--;
						continue;
					}
					if (findingStar && c == '*')
					{
						removeStar = i;
						findingStar = false;
					}
					if (!IsWSorContinuation(c))
						findingStar = false;
					if ((rightTypeSpecified || harvesting != OP_EQUAL) && c == '(' &&
					    !mInfo.Chain) // in case of a chain, '(' doesn't mean the end of harvesting. '(' means the end
					                  // because it usually means the end of a type, i.e. with "= int(5);"
					{
						harvesting = PARENS;
						parens++;
						if (parens == 1)
						{
							values = "";
							if (valueSelFrom == 0)
								valueSelFrom = i;
							continue;
						}
					}
					if (c == '{' || c == '}')
						return rrNoChange; // ERROR
					if (harvesting == OP_EQUAL && c == ';')
					{
						valueSelTo = i;
						break;
					}
					if (harvesting == PARENS && c == ')')
					{
						parens--;
						if (parens <= 0)
						{
							harvesting = FINISHING;
							continue;
						}
					}
					if (harvesting == FINISHING && c == ';')
					{
						valueSelTo = i;
						break;
					}
					if (harvesting == SEEKING && c == ';')
						break;
				}
				if (harvesting == FINISHING && !IsWSorContinuation(c))
					fullCopy = true;
				if (harvesting == OP_EQUAL || harvesting == PARENS)
					values += c;
			}

			// is it a template pointer?
			bool customPointerIsTemplate =
			    Psettings->mConvertToPointerType == 3 && IsTemplateType(Psettings->mConvertCustomPtrName);

			// deleting / changing the part after '='
			if (removeStar)
			{
				ed->SetSel(removeStar, removeStar + 1);
				if (!ed->ReplaceSelW("", noFormat))
					return rrError;
			}
			else
			{
				if (valueSelFrom && valueSelTo)
				{
					if (fullCopy)
					{
						values = "";
						for (int i = valueSelFrom + 1; i < valueSelTo; i++)
							values += buf[(uint)i];
					}

					ed->SetSel(valueSelFrom, valueSelTo);
					if (!ed->ReplaceSelW("", noFormat))
						return rrError;
				}
			}

			values.Trim();
			if (values.GetLength() && values[0] == '=')
			{
				values = values.Right(values.GetLength() - 1);
				values.Trim();
			}

			// add pointer after TYPE or '>', whichever is closer to the sym
			cs.Reset();
			counter = 0;
			CStringW pointerChar;
			if (Psettings->mConvertToPointerType == 0)
			{
				pointerChar = L"*";
			}
			else
			{
				if (!Psettings->mUseAutoWithConvertRefactor &&
				    (customPointerIsTemplate || Psettings->mConvertToPointerType < 3))
					pointerChar = L">";
			}
			int pcl = pointerChar.GetLength();
			if (pcl)
			{
				for (long i = symPos - 1; i >= 0; i--)
				{
					if (counter++ > maxSearchSteps)
						return rrNoChange;
					auto c = buf[(uint)i];
					if (cs.IsCodeBackward(buf, i))
					{
						if (c == '>' || ISCSYM(c))
						{
							ed->SetSel(i + 1, i + 1);
							if (!ed->ReplaceSelW(pointerChar, noFormat))
								return rrError;
							break;
						}

						if (!IsWSorContinuation(c))
							return rrNoChange;
					}
				}
			}

			// Finally, append the below: = "new TYPE(VALUES)" or smart pointer's make
			if (!removeStar)
			{
				long pos1 = symPos + symName.GetLength() + pcl;
				ed->SetSel(pos1, pos1);
				if (leftType == "auto" && !mInfo.CallName.IsEmpty())
				{
					leftType = mInfo.CallName;
				}
				WTString paste;
				WTString makeName = Psettings->mConvertCustomMakeName;
				DWORD ptrType = Psettings->mConvertToPointerType;
				if (ptrType == 3 && makeName.IsEmpty())
					ptrType = 0; // if there is no makeName, fall-back to construction via "new" keyword
				switch (ptrType) // see:#convert_pointer_types
				{
				case 0:
					paste = " = new " + leftType + "(" + values + ")";
					break;
				case 1:
					paste = " = std::make_unique<" + leftType + ">(" + values +
					        ")"; // todo: check whether std is needed (is using present?)
					break;
				case 2:
					paste = " = std::make_shared<" + leftType + ">(" + values +
					        ")"; // todo: check whether std is needed (is using present?)
					break;
				case 3: {
					if (customPointerIsTemplate)
						paste = " = " + makeName + "<" + leftType + ">(" + values + ")";
					else
						paste = " = " + makeName + "(" + values + ")";
					break;
				}
				}
				if (!ed->ReplaceSelW(paste.Wide(), noFormat))
					return rrError;
				if (Psettings->mUseAutoWithConvertRefactor && typeFrom && typeTo)
				{
					ed->SetSel(typeFrom, typeTo);
					if (!ed->ReplaceSelW(L"auto", noFormat))
						return rrError;
				}
				if (Psettings->mConvertToPointerType > 0 && !Psettings->mUseAutoWithConvertRefactor)
				{
					switch (Psettings->mConvertToPointerType)
					{
					case 1:
						ed->SetSel(typeFrom, typeFrom);
						if (!ed->ReplaceSelW(L"std::unique_ptr<", noFormat))
							return rrError;
						break;
					case 2:
						ed->SetSel(typeFrom, typeFrom);
						if (!ed->ReplaceSelW(L"std::shared_ptr<", noFormat))
							return rrError;
						break;
					case 3: {
						WTString str;
						if (customPointerIsTemplate)
						{
							ed->SetSel(typeFrom, typeFrom);
							str.WTFormat("%s<", Psettings->mConvertCustomPtrName);
						}
						else
						{
							ed->SetSel(typeFrom, typeTo);
							str.WTFormat("%s", Psettings->mConvertCustomPtrName);
						}

						if (!ed->ReplaceSelW(str.Wide(), noFormat))
							return rrError;
						break;
					}
					}
				}
			}

			return rrSuccess;
		}
		else // member: . to ->    reference: '' to '*'  OR  '&' to ''
		{
			long replacePos;
			if (GetDotOrArrowIfPresentAfterSym(buf, symPos, symName, replacePos) == ".")
			{
				ed->SetSel(replacePos, replacePos + 1);
				if (!ed->ReplaceSelW(L"->", noFormat))
					return rrError;
				return rrSuccess;
			}
			WTString charBeforeSym = GetStarOrAddressIfPresentBeforeSym(buf, symPos, replacePos);
			if (charBeforeSym == "" || charBeforeSym == "*") // '*' was added to address case 113015
			{
				if (GetDotOrArrowIfPresentAfterSym(buf, symPos, symName, replacePos) == "->")
				{
					ed->SetSel(replacePos, replacePos);
					if (!ed->ReplaceSelW(L")", noFormat))
						return rrError;
					ed->SetSel(symPos, symPos); // we don't use replacePos here. see VAAutoTest:Convert30 why
					if (!ed->ReplaceSelW(L"(*", noFormat))
						return rrError;
				}
				else
				{
					// detect ++ and -- as per case 113764
					CStringW pointerChar;
					CStringW afterChar;
					long closeParenPos = FindHigherThanStarPrecedenceAfterSym(buf, symPos);
					if (closeParenPos)
					{
						pointerChar = L"(*";
						afterChar = L")";
						ed->SetSel(closeParenPos, closeParenPos);
						if (!ed->ReplaceSelW(afterChar, noFormat))
							return rrError;
					}
					else
					{
						pointerChar = L"*";
					}

					ed->SetSel(symPos, symPos); // we don't use replacePos here. see VAAutoTest:Convert30 why
					if (!ed->ReplaceSelW(pointerChar, noFormat))
						return rrError;
				}

				return rrSuccess;
			}
			if (charBeforeSym == "&")
			{
				if (!IsSymOrNumBefore(buf, replacePos) && replacePos > 0 && buf[uint(replacePos - 1)] != '&')
				{
					ed->SetSel(replacePos, replacePos + 1);
					if (!ed->ReplaceSelW(L"", noFormat))
						return rrError;
					return rrSuccess;
				}
				else
				{
					ed->SetSel(symPos, symPos);
					if (!ed->ReplaceSelW(L"*", noFormat))
						return rrError;
					return rrSuccess;
				}
			}

			return rrNoChange;
		}
		break;
	}
	case eConversionType::POINTER_TO_INSTANCE: {
		if (refType == FREF_Definition || refType == FREF_DefinitionAssign) // '*' to ''
		{
			return ConvertDefinitionSiteToInstance(symPos, buf, mInfo);
		}
		else // member: -> to .    reference: '*' to ''  OR  '' to '&'
		{
			long replacePos;
			if (GetDotOrArrowIfPresentAfterSym(buf, symPos, symName, replacePos) == "->")
			{
				ed->SetSel(replacePos, replacePos + 2);
				if (!ed->ReplaceSelW(L".", noFormat))
					return rrError;
				return rrSuccess;
			}
			if (GetWordIfPresentBeforeSym(buf, symPos, replacePos) == "delete")
			{
				// find if we have ')' before delete to handle cases like "if (obj) delete obj;", see
				// VAAutoTest:Convert111
				int counter2 = 0;
				bool doNotFindEOL = false;
				bool forceComment = false;
				for (uint j = uint(replacePos - 1); (int)j >= 0; j--)
				{
					if (counter2++ > maxSearchSteps)
						break;

					TCHAR c = buf[j];
					if (c == ')' || (IsWSorContinuation(c) && j >= 5 && buf[j - 1] == 'e' && buf[j - 2] == 's' &&
					                 buf[j - 3] == 'l' && buf[j - 4] == 'e' && IsWSorContinuation(buf[j - 5])))
					{
						doNotFindEOL = true;
						forceComment = true;
						break;
					}

					if (!IsWSorContinuation(c))
						break;
				}

				long endPos = symPos + symName.GetLength();
				eEndPosCorrection correctionType = CorrectEndPosOfDelete(buf, endPos, doNotFindEOL);
				if (forceComment && correctionType == eEndPosCorrection::FOUND_SEMICOLON)
					correctionType = eEndPosCorrection::NONE;  // forcing to fall-back to commenting out
				if (correctionType != eEndPosCorrection::NONE) // delete statement / whole line depending whether we
				                                               // have one or more statements on the line
				{
					if (correctionType == eEndPosCorrection::FOUND_EOL) // if we found EOL, we also delete leading space
					{
						while (replacePos - 1 >= 0 &&
						       (buf[uint(replacePos - 1)] == ' ' || buf[uint(replacePos - 1)] == '\t'))
							replacePos--;
					}

					ed->SetSel(replacePos, endPos);
					if (!ed->ReplaceSelW(L"", noFormat))
						return rrError;
				}
				else // we didn't find ';' or we're inside a single statement if/else branch - commenting out "delete
				     // <objname>" part
				{
					ed->SetSel(endPos, endPos);
					if (!ed->ReplaceSelW(L"*/", noFormat))
						return rrError;

					ed->SetSel(replacePos, replacePos);
					if (!ed->ReplaceSelW(L"/*", noFormat))
						return rrError;
				}

				return rrSuccess;
			}
			if (GetStarOrAddressIfPresentBeforeSym(buf, symPos, replacePos) == "*")
			{
				ed->SetSel(replacePos, replacePos + 1);
				if (!ed->ReplaceSelW(L"", noFormat))
					return rrError;
				return rrSuccess;
			}
			if (GetStarOrAddressIfPresentBeforeSym(buf, symPos, replacePos) == "")
			{
				ed->SetSel(symPos, symPos); // we don't use replacePos. see VAAutoTest:Convert29 why
				if (!ed->ReplaceSelW(L"&", noFormat))
					return rrError;
				return rrSuccess;
			}
			return rrNoChange;
		}
		break;
	}
	default: {
		break;
	}
	}

	return rrNoChange;
}
