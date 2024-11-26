#pragma once

#include "FOO.H"
#include "Mparse.h"

DTypePtr InferTypeFromAutoVar(DTypePtr symDat, MultiParsePtr mp, int devLang, BOOL forBcl = FALSE);

// expects buf to be pointing to either [ or ]
// IsLambda will rewind from buf, but won't go beyond pStartOfBuf
BOOL IsLambda(LPCTSTR buf, LPCTSTR pStartOfBuf);
inline BOOL IsLambda(LPCTSTR buf, const WTString& pStartOfBuf)
{
	return IsLambda(buf, pStartOfBuf.c_str());
}
WTString ResolveLambdaType(MultiParsePtr& mp, WTString expression, const WTString& scope, const WTString& bcl,
                           const WTString& defaultType);

class InferType
{
  public:
	WTString Infer(WTString expression, WTString scope, WTString bcl, int fileType, bool useEditorSelection = false,
	               WTString defaultType = "", DType** dt = nullptr);
	WTString Infer_orig(WTString expression, WTString scope, WTString bcl, int fileType, bool useEditorSelection,
	                    bool resolveLambda, DType** dt);
	WTString Infer(WTString expression, WTString scope, WTString bcl, int fileType, bool useEditorSelection,
	               bool resolveLambda, DType** dt);
	WTString Infer(MultiParsePtr mp, WTString expression, WTString scope, WTString bcl, int fileType,
	               bool resolveLambda = true);
	WTString GetArrayType(WTString type, WTString var, bool& isTemplateType, int fileType);
	WTString NextChainItem(WTString& expression, bool useEditorSelection, int fileType);
	void RemoveParens(WTString& expression);
	WTString GetDefaultType(int devLang = gTypingDevLang);
	WTString GetConstructorCastType(WTString expression, int fileType);
	bool GetTypesAndNamesForStructuredBinding(LPCSTR expression, MultiParsePtr mp, WTString scope,
	                                             std::vector<WTString>& names, std::vector<WTString>& types, char stOpen, char stClose, const WTString& stKeyword);

  private:
	EdCntPtr mEd;
	MultiParsePtr mMp;
	WTString DefaultType;
	bool mSkipConstPrepend = false;
	void GetTemplateParameters(const WTString& typeVar, const WTString& type, std::vector<WTString>& types);
	void GetInferredTypesFromArguments(const WTString& type, std::vector<WTString>& types, MultiParsePtr mp, WTString scope);
	void GetStructMembers(const WTString& type, std::vector<WTString>& types, MultiParsePtr mp, WTString scope);
};
