#include "StdAfxEd.h"
#include "PromoteLambda.h"
#include <tuple>
#include "CommentSkipper.h"
#include "InferType.h"
#include "FILE.H"
#include "UndoContext.h"
#include "ExtractMethodDlg.h"
#include "FreezeDisplay.h"
#include "VARefactor.h"
#include "LocalRefactoring.h"

BOOL PV_InsertAutotextTemplate(const WTString& templateText, BOOL reformat, const WTString& promptTitle = NULLSTR);

LambdaPromoter::LambdaPromoter(int fType, const WTString& symbolName)
    : MethodExtractor(fType)
{
	mMethodName = symbolName;
	mSymbolName = symbolName;

	// Capitalize the first letter of the method name since we're moving from local to global scope
	if (!mMethodName.IsEmpty() && !isupper(mMethodName[0]))
	{
		mMethodName.SetAt(0, (TCHAR)toupper(mMethodName[0]));
	}

	if (mMethodName == symbolName)
	{
		// If the method name is still the same as the symbol name, append "Method" to the method name
		mMethodName += "Method";
	}
}

std::tuple<WTString, WTString, WTString, WTString, long> LambdaPromoter::FindLambdaParts(const WTString& buf, int realPos)
{
	enum class State
	{
		InLambda,
		InArguments,
		InReturnType,
		InBody,
		Done
	};

	int lambdaStart = -1;
	int captureEnd = -1;
	int paramsStart = -1;
	int paramsEnd = -1;
	int returnTypeStart = -1;
	int returnTypeEnd = -1;
	int bodyStart = -1;
	int bodyEnd = -1;
	int braceCount = 0;
	int parenCount = 0;
	CommentSkipper cs(FileType());

	// Search forwards for lambda start
	for (int i = realPos; i < buf.GetLength(); ++i)
	{
		if (!cs.IsCode(buf[i]))
			continue;

		if (buf[i] == '[')
		{
			lambdaStart = i;
			break;
		}
	}

	if (lambdaStart == -1)
		return std::make_tuple(WTString(), WTString(), WTString(), WTString(), 0);

	// Reset CommentSkipper before second loop
	cs.Reset();

	// Search forwards for lambda parts
	State state = State::InLambda;
	for (int i = lambdaStart + 1; i < buf.GetLength() && state != State::Done; ++i)
	{
		if (!cs.IsCode(buf[i]))
			continue;

		switch (state)
		{
		case State::InLambda:
			if (buf[i] == ']')
			{
				captureEnd = i;
				state = State::InArguments;
			}
			break;
		case State::InArguments:
			if (paramsStart == -1 && buf[i] == '(')
			{
				paramsStart = i;
				++parenCount;
			}
			else if (paramsStart != -1)
			{
				if (buf[i] == '(')
				{
					++parenCount;
				}
				else if (buf[i] == ')')
				{
					--parenCount;
					if (parenCount == 0)
					{
						paramsEnd = i;
						state = State::InReturnType;
					}
				}
			}
			else if (buf[i] == '{')
			{
				// Lambda without explicit argument list
				state = State::InBody;
				--i; // Reprocess this character in the InBody state
			}
			break;
		case State::InReturnType:
			if (buf[i] == '-' && i + 1 < buf.GetLength() && buf[i + 1] == '>')
			{
				returnTypeStart = i + 2;
			}
			else if (returnTypeStart != -1 && buf[i] == '{')
			{
				returnTypeEnd = i - 1;
				state = State::InBody;
				--i; // Reprocess this character in the InBody state
			}
			else if (buf[i] == '{')
			{
				// No return type specified
				state = State::InBody;
				--i; // Reprocess this character in the InBody state
			}
			break;
		case State::InBody:
			if (bodyStart == -1 && buf[i] == '{')
			{
				bodyStart = i;
				++braceCount;
			}
			else if (bodyStart != -1)
			{
				if (buf[i] == '{')
				{
					++braceCount;
				}
				else if (buf[i] == '}')
				{
					--braceCount;
					if (braceCount == 0)
					{
						bodyEnd = i;
						state = State::Done;
					}
				}
			}
			break;
		default:
			break;
		}
	}

	WTString capturePart = (lambdaStart != -1 && captureEnd != -1)
	                           ? buf.Mid(lambdaStart + 1, captureEnd - lambdaStart - 1)
	                           : WTString();

	WTString argsPart = (paramsStart != -1 && paramsEnd != -1)
	                        ? buf.Mid(paramsStart + 1, paramsEnd - paramsStart - 1)
	                        : WTString();

	WTString returnTypePart = (returnTypeStart != -1 && returnTypeEnd != -1)
	                              ? buf.Mid(returnTypeStart, returnTypeEnd - returnTypeStart + 1)
	                              : WTString();
	returnTypePart.Trim();

	WTString bodyPart = (bodyStart != -1 && bodyEnd != -1)
	                        ? buf.Mid(bodyStart + 1, bodyEnd - bodyStart - 1)
	                        : WTString();

	return std::make_tuple(capturePart, argsPart, returnTypePart, bodyPart, bodyEnd + 1);
}

std::tuple<long, long> LambdaPromoter::FindSelection(const WTString& buf, int realPos, long bodyEnd)
{
	CommentSkipper cs(FileType());
	WTString currentWord;
	long selectionStart = realPos;
	long selectionEnd = bodyEnd;

	// Search backwards
	for (int i = realPos - 1; i >= 0; --i)
	{
		if (!cs.IsCodeBackward(buf, i))
			continue;

		char c = buf[i];

		if (isalnum(c) || c == '_' || c == ':')
		{
			currentWord = c + currentWord;
		}
		else
		{
			if (currentWord == "auto" || currentWord == "std::function")
			{
				// Found a relevant word, use it as the beginning of the selection
				selectionStart = i + 1;
				break;
			}
			else if (c == '{' || c == ';')
			{
				// Found '{' or ';' before a relevant word, use the original realPos
				break;
			}
			currentWord.Empty();
		}
	}

    // Extend the beginning of the selection
	for (int i = selectionStart - 1; i >= 0; --i)
	{
		char c = buf[(unsigned int)i];
		if (c == '\n')
		{
			// Found a newline, stop here without including it
			selectionStart = i + 1;
			break;
		}
		else if (!isspace(c))
		{
			// Found a non-whitespace character, stop here
			selectionStart = i + 1;
			break;
		}
		// Continue backwards for whitespace
		selectionStart = i;
	}

	// Search forwards
	cs.Reset();
	for (int i = bodyEnd; i < buf.GetLength(); ++i)
	{
		if (!cs.IsCode(buf[i]))
			continue;

		if (!isspace(buf[i]))
		{
			if (buf[i] == ';')
			{
				// Found ';', use the next position as the end of selection
				selectionEnd = i + 1;

                // Check for newline character(s) after ';'
				if (selectionEnd < buf.GetLength())
				{
					if (buf[(unsigned int)selectionEnd] == '\r')
						selectionEnd++;
					if (selectionEnd < buf.GetLength() && buf[(unsigned int)selectionEnd] == '\n')
						selectionEnd++;
				}
			}
			// If it's not ';', we keep the original bodyEnd
			break;
		}
	}

	return std::make_tuple(selectionStart, selectionEnd);
}

std::tuple<WTString, WTString, bool> LambdaPromoter::ProcessCaptureList(const WTString& capturePart)
{
	WTString capturedParams;
	WTString capturedArguments;
	bool captureAll = false;
	token captureToken(capturePart);
	captureToken.ifs = ","; // Set delimiter to comma

	while (captureToken.more())
	{
		WTString symbolName = captureToken.read();
		symbolName.Trim();
		if (symbolName == "=" || symbolName == "&" || symbolName == "this")
		{
			captureAll = true;
			continue;
		}

		// Deduce type for the captured symbol
			InferType infer;
			EdCntPtr ed(g_currentEdCnt);
			MultiParsePtr mp = ed->GetParseDb();
			WTString scope = ed->m_lastScope;
			WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
		WTString inferredType = infer.Infer(symbolName, scope, bcl, mp->FileType());

		if (!inferredType.IsEmpty() && inferredType != "UnknownType")
		{
			if (!capturedParams.IsEmpty())
				capturedParams += ", ";
			capturedParams += inferredType + " " + symbolName;
		}

		symbolName.TrimLeftChar('&');
		if (!capturedArguments.IsEmpty())
			capturedArguments += ", ";
		capturedArguments += symbolName;
	}

	return std::make_tuple(capturedParams, capturedArguments, captureAll);
}

WTString LambdaPromoter::GetArgs(WTString params)
{
	std::vector<WTString> parameterNames;
	WTString currentParam;
	int parenCount = 0;

	for (int i = params.GetLength() - 1; i >= 0; --i)
	{
		char c = params[i];

		if (c == ')')
		{
			++parenCount;
			currentParam = c + currentParam;
		}
		else if (c == '(')
		{
			--parenCount;
			currentParam = c + currentParam;
		}
		else if (c == ',' && parenCount == 0)
		{
			parameterNames.push_back(ExtractName(currentParam));
			currentParam.Empty();
		}
		else
		{
			currentParam = c + currentParam;
		}
	}

	if (!currentParam.IsEmpty())
	{
		parameterNames.push_back(ExtractName(currentParam));
	}

	// Reverse the order of names since we processed them backwards
	std::reverse(parameterNames.begin(), parameterNames.end());

	// Build the result string
	WTString result;
	for (size_t i = 0; i < parameterNames.size(); ++i)
	{
		if (i > 0)
		{
			result += ", ";
		}
		result += parameterNames[i];
	}

	return result;
}

WTString LambdaPromoter::ExtractName(const WTString& param)
{
	WTString name;
	int parenCount = 0; // supporting C-style function pointers

	for (int i = param.GetLength() - 1; i >= 0; --i)
	{
		char c = param[i];

		if (c == ')')
		{
			++parenCount;
		}
		else if (c == '(')
		{
			--parenCount;
			name.Empty();
		}
		else if (isalnum(c) || c == '_')
		{
			name = c + name; // prepend
		}
		else
		{
			if (parenCount == 0 && name.IsEmpty() == false)
				break;
			if (c == '*') // supporting C-style function pointers
				break;
		}
	}

	return name;
}

BOOL LambdaPromoter::PromoteLambda(const CStringW& destinationHeaderFile)
{
	const WTString& buf = g_currentEdCnt->GetBuf(FALSE);
	uint fakePos = g_currentEdCnt->CurPos();
	int realPos = g_currentEdCnt->GetBufIndex(buf, (long)fakePos);
	if (!UpdateMembers(buf, realPos))
		return FALSE;

	m_orgScope = g_currentEdCnt->m_lastScope;

	auto [mathcingBracePos, callSites] = FindMatchingBrace(buf, SelEnd, mSymbolName);
	if (mathcingBracePos == -1)
	{
		// Failed to find matching brace
		return FALSE;
	}

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
	WTString tmpMethodSignature_free = CreateTempMethodSignature(mMethodParameterList_Free, mMethodReturnType_Free, false);

	// Show the ExtractMethodDlg which is customized for Promote Lambda
	auto [canExtractToSource, canExtractAsFreeFunction] = GetExtractionOptions();
	ExtractMethodDlg methDlg(NULL, mMethodName, tmpMethodSignature, tmpMethodSignature_free,
								canExtractToSource, canExtractAsFreeFunction, "Promote Lambda BETA");
	if (methDlg.DoModal() != IDOK)
		return FALSE;

	std::shared_ptr<UndoContext> undoContext = std::make_shared<UndoContext>("PromoteLambda BETA");

	WTString jumpName, jumpScope, freeFuncMoveImplScope;
	UINT type;

	GetJumpInfo(jumpName, jumpScope, freeFuncMoveImplScope, type, methDlg, canExtractAsFreeFunction);

	// replace the calls
	// iterate on the call sites on the reverse order to avoid changing the positions of the other calls
	auto replaceCalls = [&](const WTString& methodInvocation)
	{
		for (auto it = callSites.rbegin(); it != callSites.rend(); ++it)
		{
			long selBeg = it->startPosition;
			long selEnd = it->afterOpenParen;
			g_currentEdCnt->SetSelection(selBeg, selEnd);
			CStringW callSite = methodInvocation.c_str();
			g_currentEdCnt->ReplaceSelW(callSite, noFormat);
		}
	};

	if (methDlg.m_extractAsFreeFunction)
	{
		// replace the calls with the free function call
		replaceCalls(mMethodInvocation_Free);
	}
	else
	{
		// replace the calls with the method call
		replaceCalls(mMethodInvocation);
	}

	// remove the lambda definition
	CStringW replaceBy;
	g_currentEdCnt->SetSelection(SelStart, SelEnd);
	g_currentEdCnt->ReplaceSelW(replaceBy, noFormat);

	WTString implCode = BuildImplementation(methDlg.m_extractAsFreeFunction ? mMethodParameterList_Free : mMethodParameterList);

	if (MoveCaret(jumpName, jumpScope, type))
	{
		// Setup FinishMethodExtractorParams
		bool extractToSrc = methDlg.IsExtractToSrc();
		bool extractAsFreeFunction = !!methDlg.m_extractAsFreeFunction;

		sFinishMethodExtractorParams.Load(undoContext, implCode, "", m_baseScope, mMethodName, extractToSrc, extractAsFreeFunction);

		// Call FinishMethodExtractor via timer
		if (gShellAttr->IsDevenv15OrHigher())
		{
			// insert the method implementation async
			SetTimer(nullptr, 0, USER_TIMER_MINIMUM, (TIMERPROC)(uintptr_t)&FinishMethodExtractor);
		}
		else
		{
			// insert the method implementation
			FinishMethodExtractor();
		}
	}

	return TRUE;
}

bool LambdaPromoter::UpdateMembers(const WTString& buf, int realPos, WTString scope /*= ""*/)
{
	auto [captureClausePart, paramsPart, returnTypePart, bodyPart, bodyEnd] = FindLambdaParts(buf, realPos);

	if (captureClausePart.IsEmpty() && bodyPart.IsEmpty())
	{
		// Failed to find lambda parts
		return false;
	}

	bodyPart.Trim(); // Remove empty lines

	// Find the proper boundaries for the selection that replaces the lambda with the call
	std::tie(SelStart, SelEnd) = FindSelection(buf, realPos, bodyEnd);

	// process lambda parts
	mTargetFile = g_currentEdCnt->FileName();
	mMethodBody = bodyPart;
	mMethodReturnType = returnTypePart;

	if (mMethodReturnType.IsEmpty())
	{
		// If no explicit return type, attempt to deduce it
		mMethodReturnType = DeduceReturnType(bodyPart);
	}

	mMethodReturnType_Free = mMethodReturnType;

	// Process capture list
	auto [captureClauseParams, capturedArguments, captureAll] = ProcessCaptureList(captureClausePart);

	// Insert captured arguments before arguments
	WTString args = GetArgs(paramsPart);
	if (!capturedArguments.IsEmpty())
	{
		if (!args.IsEmpty())
			args = capturedArguments + ", " + args;
		else
			args = capturedArguments;
	}

	// Compile the method parameter list
	mMethodParameterList = captureClauseParams;
	if (!mMethodParameterList.IsEmpty() && !paramsPart.IsEmpty())
		mMethodParameterList += ", ";
	mMethodParameterList += paramsPart;

	mMethodParameterList_Free = captureClauseParams;
	if (!mMethodParameterList_Free.IsEmpty() && !paramsPart.IsEmpty())
		mMethodParameterList_Free += ", ";
	mMethodParameterList_Free += paramsPart;

	// Parse the code to get the variables that are outside the lambda and also add them to the parameter lists
	WTString capturedParameterList;
	WTString capturedParameterList_Free;
	if (captureAll)
	{
		WTString usedScope = scope.IsEmpty() ? g_currentEdCnt->m_lastScope : scope;
		ParseMethodBody(usedScope);
		std::tie(capturedParameterList, capturedParameterList_Free) = ExpandParameterList();

	    // Add capturedParameterList to the beginning of mMethodParameterList
		if (!capturedParameterList.IsEmpty() && !mMethodParameterList.IsEmpty())
			mMethodParameterList = capturedParameterList + ", " + mMethodParameterList;
		else if (!capturedParameterList.IsEmpty())
			mMethodParameterList = capturedParameterList;

		// Add capturedParameterList to the beginning of mMethodParameterList_Free
		if (!capturedParameterList_Free.IsEmpty() && !mMethodParameterList_Free.IsEmpty())
			mMethodParameterList_Free = capturedParameterList_Free + ", " + mMethodParameterList_Free;
		else if (!capturedParameterList_Free.IsEmpty())
			mMethodParameterList_Free = capturedParameterList_Free;
	}

	auto updateMethodInvocation = [&](WTString& methodInvocation, const WTString& captured)
	{
		const WTString implCodelnBrk(EolTypes::GetEolStr(buf));
		if (!captured.IsEmpty() && !captureClauseParams.IsEmpty())
			methodInvocation = captured + ", " + captureClauseParams;
		else
			methodInvocation = captured + captureClauseParams;
		methodInvocation = GetArgs(methodInvocation); // converting parameters to arguments
		if (!methodInvocation.IsEmpty() && !paramsPart.IsEmpty())
			methodInvocation += ", "; // we put parameters before arguments at the call sites to stay compatible with parameter default values

		methodInvocation = mMethodName + "(" + methodInvocation; // leaving the end alone to properly handle default values
	};

	updateMethodInvocation(mMethodInvocation, capturedParameterList);
	updateMethodInvocation(mMethodInvocation_Free, capturedParameterList_Free);

	// build the method implementation
	const bool kIsC = (Src == FileType() || Header == FileType()) && ::IsCfile(g_currentEdCnt->FileName());
	if (kIsC && Src == ::GetFileType(mTargetFile))
		mAutotextItemTitle = "Refactor Create Implementation";
	else
		mAutotextItemTitle = "Refactor Extract Method";

	mMethodBody = bodyPart;

	return true;
}

WTString LambdaPromoter::DeduceReturnType(const WTString& bodyPart)
{
	CommentSkipper cs(FileType());
	WTString returnExpression;
	bool inReturnStatement = false;
	int bracketCount = 0;
	int braceCount = 0;
	int parenCount = 0;
	bool inStringLiteral = false;
	char stringDelimiter = 0;

	for (int i = 0; i < bodyPart.GetLength(); ++i)
	{
		if (cs.IsComment(bodyPart[i]))
			continue;

		if (!inReturnStatement)
		{
			// Look for the start of a return statement
			if (bodyPart.Mid(i, 6) == "return" &&
			    (i == 0 || !isalnum(bodyPart[i - 1])) &&                      // Check before "return"
			    (i + 6 >= bodyPart.GetLength() || !isalnum(bodyPart[i + 6]))) // Check after "return"
			{
				inReturnStatement = true;
				i += 5; // Skip "return"
				continue;
			}
		}
		else
		{
			// We're in a return statement, capture the expression
			if (!inStringLiteral)
			{
				if (bodyPart[i] == '[')
					++bracketCount;
				else if (bodyPart[i] == ']')
					--bracketCount;
				else if (bodyPart[i] == '{')
					++braceCount;
				else if (bodyPart[i] == '}')
					--braceCount;
				else if (bodyPart[i] == '(')
					++parenCount;
				else if (bodyPart[i] == ')')
					--parenCount;
				else if (bodyPart[i] == '"' || bodyPart[i] == '\'')
				{
					inStringLiteral = true;
					stringDelimiter = bodyPart[i];
				}
			}
			else if (bodyPart[i] == stringDelimiter && (i == 0 || bodyPart[i - 1] != '\\'))
			{
				inStringLiteral = false;
			}

			if (bodyPart[i] == ';' && !inStringLiteral && bracketCount == 0 && braceCount == 0 && parenCount == 0)
			{
				// End of return statement
				break;
			}
			returnExpression += bodyPart[i];
		}
	}

    // Trim whitespace from the return expression
	returnExpression.Trim();

	// If we found a return expression, infer its type; otherwise, default to "void"
	if (!returnExpression.IsEmpty())
	{
		InferType infer;
		EdCntPtr ed(g_currentEdCnt);
		MultiParsePtr mp = ed->GetParseDb();
		WTString scope = ed->m_lastScope;
		WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
		WTString inferredType = infer.Infer(returnExpression, scope, bcl, Src, false);

		if (inferredType.IsEmpty() || inferredType == "UnknownType")
			return "auto";

		return inferredType;
	}

	return "void";
}

std::pair<WTString, WTString> LambdaPromoter::ExpandParameterList()
{
	WTString newCapturedParams;
	WTString newCapturedParams_Free;
	const bool kIsC = (Src == FileType() || Header == FileType()) && ::IsCfile(g_currentEdCnt->FileName());
	int nonFreeCounter = 0;
	for (int i = 0; i < mArgCount; nonFreeCounter += !mMethodArgs[i].member, i++)
	{
		const WTString kCurArgVal(mMethodArgs[i].data->Def());
		const WTString commaStr(!newCapturedParams.IsEmpty() ? ", " : "");
		const WTString commaStr_free(!newCapturedParams_Free.IsEmpty() ? ", " : "");
		const WTString addressOfStr(kIsC && mMethodArgs[i].modified && !mMethodArgs[i].data->IsPointer() && !m_mp->IsPointer(kCurArgVal) ? "&" : "");
		WTString refStr;
		if (mMethodArgs[i].modified)
		{
			if (kIsC)
			{
				if (!mMethodArgs[i].data->IsPointer() && !m_mp->IsPointer(kCurArgVal))
					refStr = "*";
			}
			else
			{
				refStr = "&";
			}
		}
		const WTString paramModifier(addressOfStr);
		WTString newParam;
		if (mMethodArgs[i].modified)
		{
			newParam = ::GetTypeFromDef(kCurArgVal, FileType()) + " " + refStr + mMethodArgs[i].arg;
			newParam = DecodeScope(newParam);
			if (kIsC)
			{
				// change method body in C source - no references, etc.
				PointerRelatedReplaces(mMethodBody, i, addressOfStr, refStr);
				PointerRelatedReplaces(mMethodBody_Free, i, addressOfStr, refStr);
			}
		}
		else
		{
			newParam = ::GetTypeFromDef(kCurArgVal, FileType()) + " " + mMethodArgs[i].arg;
		}

		// Check if the parameter is already in the list before adding
		WTString newParamName = ExtractName(newParam);
		if (!strstrWholeWord(mMethodParameterList.c_str(), newParamName.c_str(), TRUE))
		{
			bool member = mMethodArgs[i].member;
			if (!member)			
				newCapturedParams += commaStr + newParam;
		}
		if (!strstrWholeWord(mMethodParameterList_Free.c_str(), newParamName.c_str(), TRUE))
		{
			newCapturedParams_Free += commaStr_free + newParam;
		}
	}
	
	// logic above may add int &&i, if "i" was already a reference.
	newCapturedParams.ReplaceAll("&&", "&", FALSE);
	newCapturedParams.ReplaceAll(" & &", " &", FALSE);
	newCapturedParams.ReplaceAll("& &", " &", FALSE);

	newCapturedParams_Free.ReplaceAll("&&", "&", FALSE);
	newCapturedParams_Free.ReplaceAll(" & &", " &", FALSE);
	newCapturedParams_Free.ReplaceAll("& &", " &", FALSE);

	return {newCapturedParams, newCapturedParams_Free};
}

std::tuple<long, std::vector<LambdaCallSite>> LambdaPromoter::FindMatchingBrace(const WTString& buf, int position, const WTString& lambdaName)
{
	CommentSkipper cs(FileType());
	int braceCount = 1;
	int parenCount = 0;
	int bracketCount = 0;
	long afterOpenParen = 0;
	std::vector<LambdaCallSite> callSites;

	enum class CallState
	{
		NotInCall,
		InLambdaName,
		InWhitespace,
		Found
	};

	CallState callState = CallState::NotInCall;
	int callStartPos = -1;
	int nameMatchPos = 0;

	for (int i = position; i < buf.GetLength(); ++i)
	{
		if (!cs.IsCode(buf[i]))
			continue;

		// Detect lambda call
		switch (callState)
		{
		case CallState::NotInCall:
			if (buf[i] == lambdaName[0])
			{
				callState = CallState::InLambdaName;
				callStartPos = i;
				nameMatchPos = 1;
			}
			break;
		case CallState::InLambdaName:
			if (nameMatchPos < lambdaName.GetLength() && buf[i] == lambdaName[nameMatchPos])
			{
				++nameMatchPos;
				if (nameMatchPos == lambdaName.GetLength())
				{
					callState = CallState::InWhitespace;
				}
			}
			else
			{
				callState = CallState::NotInCall;
			}
			break;
		case CallState::InWhitespace:
			if (buf[i] == '(')
			{
				callState = CallState::Found;
				afterOpenParen = i + 1;
			}
			else if (!isspace(buf[i]))
			{
				callState = CallState::NotInCall;
			}
			break;
		case CallState::Found:
		{
			long callEndPos = FindEndOfCallSite(buf, i);
			if (callEndPos != -1)
			{
				callSites.push_back({callStartPos, callEndPos + 1, afterOpenParen});
				i = callEndPos; // Skip to the end of the call site
			}
			callState = CallState::NotInCall;
		}
		break;
		}

		// Brace counting logic
		switch (buf[i])
		{
		case '{':
			++braceCount;
			break;
		case '}':
			--braceCount;
			if (braceCount == 0 && parenCount == 0 && bracketCount == 0)
				return {i, callSites};
			break;
		case '(':
			++parenCount;
			break;
		case ')':
			--parenCount;
			break;
		case '[':
			++bracketCount;
			break;
		case ']':
			--bracketCount;
			break;
		}
	}

	// If we reach this point, no matching brace was found
	return {-1, callSites};
}

long LambdaPromoter::FindEndOfCallSite(const WTString& buf, int startPos)
{
	CommentSkipper cs(FileType());
	int parenCount = 1;
	int braceCount = 0;
	int bracketCount = 0;

	for (int i = startPos; i < buf.GetLength(); ++i)
	{
		if (!cs.IsCode(buf[i]))
			continue;

		switch (buf[i])
		{
			case '(':
				++parenCount;
				break;
			case ')':
				--parenCount;
				if (parenCount == 0 && braceCount == 0 && bracketCount == 0)
					return i;
				break;
			case '{':
				++braceCount;
				break;
			case '}':
				--braceCount;
				break;
			case '[':
				++bracketCount;
				break;
			case ']':
				--bracketCount;
				break;
		}
	}

	// If we reach this point, no matching closing parenthesis was found
	return -1;
}
