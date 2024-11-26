#pragma once

#include "LocalRefactoring.h"

class AddForwardDeclaration : public LocalRefactoring
{
  public:
	AddForwardDeclaration()
	{
	}
	~AddForwardDeclaration()
	{
	}

	BOOL CanAdd();
	BOOL Add();

	WTString InsertedText;

  private:
	bool IsItPointerOrReferenceOrTemplate(const WTString& fileBuf, long curPos);
	bool IsReturnOfValueOfFunctionDeclaration(const WTString& fileBuf, long curPos);
	bool IsParamListOfDeclaration(const WTString& fileBuf, long curPos);
	void QualifyForwardDeclaration(const WTString& buf, long curPos);
	std::pair<WTString, int> GetSymNameAndType(bool qualified);
	bool IsTemplate(const WTString& fileBuf, long curPos);
};
