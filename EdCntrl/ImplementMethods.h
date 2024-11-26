#pragma once

#include "GenericTreeDlg.h"
#include "FOO.H"
#include "Mparse.h"

class DType;
class FreezeDisplay;

class ImplementMethods
{
  public:
	ImplementMethods(bool displayPrompt = false);

	BOOL CanImplementMethods(bool forceBclUpdate = false);
	BOOL DoImplementMethods();
	WTString GetCommandText();

  private:
	void BuildListOfMethods(DType& mBaseType);
	void CheckForImplementedMethods();
	int ImplementMethodList();
	BOOL ImplementBaseMethod(DType& baseType, DType& curType, FreezeDisplay& _f);
	WTString GenerateSourceToAdd(DType& baseType, DType& curType);
	void SortMethodList();
	void ReviewCheckedItems();

  private:
	WTString mInvokingScope; // the class that will implement the methods
	DTypePtr mThisType;
	DTypeList mImplTypes;
	DTypeList mBaseTypes;
	DTypeList mMethodDts;
	DTypeList mFinalMethods;
	CStringW mFileInvokedFrom;
	MultiParsePtr mMp;
	bool mPreconditionsMet = false;
	bool mDisplayPrompt = false;
	bool mHaveNonInterfacePureMethods = false;
	bool mHaveNonInterfaceVirtualMethods = false;
	bool mExecutionError = false;
	bool mThisIsTemplate = false;
	bool mIsCppWinrt = false;
	GenericTreeDlgParams mDlgParams;
};
