#ifndef CreateFromUsage_h__
#define CreateFromUsage_h__

#include "WTString.h"
#include "ChangeSignature.h"
#include "Foo.h"
#include "EdCnt_fwd.h"
#include "FileLineMarker.h"
#include "Mparse.h"

class DType;
class MultiParse;
class FreezeDisplay;

class CreateFromUsage
{
  public:
	CreateFromUsage();

	enum CreateType
	{
		ctUndetermined, // only if CanCreate returns FALSE or before it is called
		ctMember,
		ctMethod,
		ctFunction,   // free function
		ctGlobal,     // global var
		ctVariable,   // local var					case=3964
		ctParameter,  // add param to cur function	case=27630
		ctEnumMember, // enum.newVal					case=27629
		ctClass,      // (operator) new expressions	case=27628

		// following are unsupported ideas
		ctConstant // 12 -> constant var			case=28761 (replace with constant)
	};

	BOOL CanCreate();
	CreateType GetCreateType() const
	{
		_ASSERTE(ctUndetermined != mCreateType);
		return mCreateType;
	}
	WTString GetCommandText();
	WTString GetName() const
	{
		return mNewSymbolName;
	}
	BOOL Create();

	static CStringW GetLabelStringWithColon(SymbolVisibility visibility);

  private:
	BOOL HaveNonXrefToCreate(EdCntPtr ed);
	BOOL HaveXrefToCreate(EdCntPtr ed);

	void RefineCreationType();
	void LookupParentDataIfAsMember(WTString symScope);
	BOOL AsMember() const
	{
		return ctMember == mCreateType || ctMethod == mCreateType || ctEnumMember == mCreateType;
	}
	BOOL CreateClass();
	WTString GenerateSourceToAdd();
	WTString GenerateSourceToAddForFunction();
	WTString GenerateSourceToAddForVariable();
	void GetUiText(CreateType ctType, WTString& optionText, WTString& labelText);
	BOOL ValidateParentData();
	WTString BuildDefaultMethodSignature();
	WTString InferTypeInParens(WTString lineText);
	void GetArgs(WTString text, std::vector<WTString>& args);
	WTString GetArgDecl(WTString theName);
	WTString BuildDefaultMemberDeclType();
	WTString InferDeclType(WTString defaultType, WTString lineText);
	BOOL InsertEnumItem(const WTString newScope, WTString codeToAdd, FreezeDisplay& _f);
	BOOL InsertLocalVariable(WTString codeToAdd, FreezeDisplay& _f);
	int GetPosToInsertLocalVariable();
	int GetPosToInsertGlobal();
	BOOL InsertXref(const WTString newScope, WTString codeToAdd, FreezeDisplay& _f);
	BOOL InsertGlobal(WTString codeToAdd, FreezeDisplay& _f);
	BOOL InsertParameter(WTString codeToAdd);

  private:
	CStringW mFileInvokedFrom;
	DType* mParentDataIfAsMember;
	DType* mDataForKeywordThis;
	DType mParentDataBackingStore;
	MultiParsePtr mMp;
	WTString mInvokingScope;
	WTString mBaseScope;
	CreateType mCreateType;
	WTString mWordAfterNext;
	ChangeSignature mChangeSig;
	bool mStaticMember, mConst, mInvokedOnXref, mConstKeywordThis, mMemberInitializationList;
	SymbolVisibility mMemberVisibility;
	
	// [case: 141293] [c++20] support new constexpr keywords consteval and constinit
	bool mConstExpr;
	bool mConstEval;

	CStringW mDeclFile, mSrcFile;
	WTString mNewSymbolName;

  public:
	class FindLnToInsert
	{
	  public:
		FindLnToInsert(SymbolVisibility memberVisibility, ULONG ln, WTString scope, const WTString& fileBuf)
		    : FileBuf(fileBuf)
		{
			MemberVisibility = memberVisibility;
			Ln = ln;
			Scope = scope;
			if (Scope.find(":", 1) != -1)
			{
				ClassName = StrGetSymScope(Scope);
				if (Scope.find(":", 1) != -1)
				{
					ClassName = StrGetSym(ClassName);
				}
			}
			else
			{
				ClassName = Scope;
			}

			if (ClassName.GetLength() && ClassName[0] == ':')
				ClassName = ClassName.Mid(1);
		}

		LineMarkers::Node* GetAppropriateVisibilityNode();
		std::pair<int, bool> GetLocationByVisibility(LineMarkers::Node& node);
		bool IsLabelAppropriate(WTString label);
		WTString GetLastLabel();
		int GetClassRealEndLine(ULONG charPos);

	  private:
		enum class eSymbolType
		{
			NONE,
			CLASS,
			STRUCT
		};
		eSymbolType SymbolType = eSymbolType::NONE;
		LineMarkers::Node* DeepestClass = nullptr;
		SymbolVisibility MemberVisibility;
		ULONG Ln;
		WTString Scope;
		WTString ClassName;
		const WTString& FileBuf;
	};
};

WTString GetDeclTypeFromDef(MultiParse* mp, WTString def, WTString scope, int symType);
WTString GetDeclTypeFromDef(MultiParse* mp, DType* dt, int symType);

#endif // CreateFromUsage_h__
