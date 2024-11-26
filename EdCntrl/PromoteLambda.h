#pragma once

#include "ExtractMethod.h"

struct LambdaCallSite
{
	long startPosition;
	long endPosition;
	long afterOpenParen;
};

class LambdaPromoter : public MethodExtractor
{
  public:
	LambdaPromoter(int fType, const WTString& symbolName);
	std::tuple<WTString, WTString, WTString, WTString, long> FindLambdaParts(const WTString& buf, int realPos);
	virtual ~LambdaPromoter()
	{
	}

	std::tuple<long, long> FindSelection(const WTString& buf, int realPos, long bodyEnd);
	std::tuple<WTString, WTString, bool> ProcessCaptureList(const WTString& capturePart);
	WTString GetArgs(WTString params);
	WTString ExtractName(const WTString& param);
	BOOL PromoteLambda(const CStringW& destinationHeaderFile);

	bool UpdateMembers(const WTString& buf, int realPos, WTString scope = "");

	WTString DeduceReturnType(const WTString& bodyPart);

	std::pair<WTString, WTString> ExpandParameterList();
	std::tuple<long, std::vector<LambdaCallSite>> FindMatchingBrace(const WTString& buf, int position, const WTString& lambdaName);
	long FindEndOfCallSite(const WTString& buf, int startPos);

	WTString mSymbolName;
	long SelStart;
	long SelEnd;
};

