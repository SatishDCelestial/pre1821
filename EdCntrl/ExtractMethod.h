#pragma once

#include "WTString.h"
#include "VAParse.h"
#include "FOO.H"

class UndoContext;
class ExtractMethodDlg;

// [case: 109020]
struct FinishMethodExtractorParams
{
	std::shared_ptr<UndoContext> mUndoContext;
	WTString mImplCode;
	WTString mFreeFuncMoveImplScope;
	WTString mBaseScope;
	WTString mMethodName;
	bool mExtractToSrc;
	bool mExtractAsFreeFunction;

	void Load(std::shared_ptr<UndoContext> undoContext, const WTString& implcode, const WTString& freeFuncMoveImplScope,
	          const WTString& baseScope, const WTString& methodName, bool exToSrc, bool exAsFree);

	void Clear();
};

// formerly ExtractMethodCls
class MethodExtractor : public VAParseMPScope
{
  public:
	MethodExtractor(int fType);
	virtual ~MethodExtractor()
	{
	}
	BOOL ExtractMethod(const CStringW& destinationHeaderFile);

	const WTString& GetMethodParameterList()
	{
		return mMethodParameterList;
	}
	const WTString& GetMethodParameterList_Free()
	{
		return mMethodParameterList_Free;
	}
	const WTString& GetMethodInvocation()
	{
		return mMethodInvocation;
	}
	const WTString& GetMethodInvocation_Free()
	{
		return mMethodInvocation_Free;
	}

	WTString BuildImplementation(const WTString& methodParameterList);

  protected:
	static const int kMaxArgs = 20;
	struct MethArg
	{
		WTString arg;
		DType* data;
		INT modified;
		bool member;
	};

	int mArgCount;
	MethArg mMethodArgs[kMaxArgs];
	WTString m_orgScope;            // scope of where extract method was invoked from (corrected for namespace usings)
	WTString m_orgScopeUncorrected; // uncorrected copy of m_orgScope (often identical to m_orgScope)
	WTString m_baseScopeBCL;
	DTypePtr mReturnTypeData;
	WTString m_baseScope;
	WTString mAutotextItemTitle;
	CStringW mTargetFile;

	WTString mMethodName;
	WTString mMethodBody;
	WTString mMethodBody_Free;
	WTString mMethodInvocation;
	WTString mMethodInvocation_Free;
	WTString mMethodParameterList;
	WTString mMethodParameterList_Free;
	WTString mMethodReturnType;
	WTString mMethodReturnType_Free;
	WTString mMethodQualifier;
	BOOL mIsStatic;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	BOOL mIsClassmethod; // [case: 135862]
#endif

	BOOL GatherMethodInfo();

	void TrimTheBody(WTString& methodBody)
	{
		while (methodBody.GetLength() && strchr("\r\n", methodBody[methodBody.GetLength() - 1]))
			methodBody = methodBody.Left(methodBody.GetLength() - 1);
	}

	void PointerRelatedReplaces(WTString& methodBodyToChange, int methodArgsIndex, const WTString& addressOfStr,
	                            const WTString& refStr)
	{
		const WTString argDot(mMethodArgs[methodArgsIndex].arg + ".");
		if (addressOfStr.IsEmpty() || (!refStr.IsEmpty() && methodBodyToChange.Find(argDot.c_str()) != -1))
		{
			// change arg. to arg->
			const WTString argPt(mMethodArgs[methodArgsIndex].arg + "->");
			methodBodyToChange.ReplaceAll(argDot.c_str(), argPt.c_str());
		}
		else
		{
			// prepend * to pointer types (that are modified in the body)
			const WTString derefArg("*" + mMethodArgs[methodArgsIndex].arg);
			methodBodyToChange.ReplaceAll(mMethodArgs[methodArgsIndex].arg.c_str(), derefArg.c_str(), TRUE);
		}
	}

	BOOL ParseMethodBody(WTString scope);
	bool FindOutermostClass(WTString scope, WTString& outermost);
	bool IsMember(DType* data);
	virtual void DoScope();
	virtual BOOL ProcessMacro();
	WTString CreateTempMethodSignature(const WTString& methodParameterList, const WTString& methodRetType,
	                                   bool canBeStatic);
	BOOL MoveCaret(const WTString& jumpName, const WTString& jumpScope, UINT type);
	std::pair<BOOL, BOOL> GetExtractionOptions();
	void GetJumpInfo(WTString& jumpName, WTString& jumpScope, WTString& freeFuncMoveImplScope, UINT& type, const ExtractMethodDlg& methDlg, BOOL canExtractAsFreeFunction);
};

void SubstituteMethodBody(WTString& implCode, WTString methodBody);
WTString GetCommonLeadingWhitespace(const WTString& text);
WTString NormalizeCommonLeadingWhitespace(const WTString& text, const WTString& requiredSpace);
int GetActiveTabSize();
extern FinishMethodExtractorParams sFinishMethodExtractorParams;
extern void CALLBACK FinishMethodExtractor(HWND hWnd = nullptr, UINT ignore1 = 0, UINT idEvent = 0, DWORD ignore2 = 0);
extern WTString GetInnerScopeName(WTString fullScope);
