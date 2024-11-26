#pragma once

#include "LocalRefactoring.h"
#include "EdCnt_fwd.h"
#include "Mparse.h"

class CreateMissingCases : public LocalRefactoring
{
  public:
	CreateMissingCases()
	{
	}
	~CreateMissingCases()
	{
	}

	bool Parse(EdCntPtr ed, std::vector<WTString>& switchLabels, int& endPos, std::vector<WTString>& enumNames,
	           WTString& labelQualification, bool& defLabel, bool check);

	bool IsEnum(MultiParse* mp, WTString scope, WTString methodScope, WTString bcl, WTString typeName,
	            DType** dtype_out = nullptr);
	BOOL CanCreate();
	BOOL Create();
};

extern bool IsEnumClass(const WTString& enumDef);
extern void GetEnumItemNames(MultiParsePtr mp, DType* dtype, std::vector<WTString>& enumNames);
extern void GetEnumItemNames(MultiParsePtr mp, std::vector<WTString>& enumNames, WTString enumSymScope);
