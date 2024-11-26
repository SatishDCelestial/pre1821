#pragma once

#include "FileLineMarker.h"
#include "RenameReferencesDlg.h"
#include "EdCnt_fwd.h"

class WTString;
class VAScopeInfo_MLC;
class DType;

class EncapsulateField
{
  public:
	EncapsulateField()
	{
	}
	~EncapsulateField()
	{
	}

	enum class eSymbolType
	{
		NONE,
		CLASS,
		STRUCT
	};

	struct sSetterGetter
	{
		bool Valid = false;

		WTString GetterName;
		int GetterFrom = 0;
		int GetterTo = 0;

		WTString SetterName;
		int SetterFrom = 0;
		int SetterTo = 0;
	};

	BOOL Encapsulate(DType* sym, VAScopeInfo_MLC& info, WTString& methodScope, CStringW& filePath);

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	static WTString ConstructPropertySnippetCppB_Head(sSetterGetter& setterGetter, const WTString& symName,
	                                                  SymbolVisibility symVisibility, bool& outIsBodyCreated);
	static WTString ConstructPropertySnippetCppB_Body(sSetterGetter& setterGetter, const WTString& symName);
	static WTString ExtractLogicalVariableNameCppB(const WTString& originalVariableName);
#endif

  private:
	static BOOL DoModal(DTypePtr sym, sSetterGetter& setterGetter, bool hidden);
	static sSetterGetter GetSetterGetter_C(const WTString& rawSnippet, DType* sym);
	static sSetterGetter GetProperty_CS(const WTString& rawSnippet, DType* sym);
	static sSetterGetter GetSetterGetter(const WTString& rawSnippet, DType* sym);
	void ChangeRawSnippet(const sSetterGetter& sg, DType* sym, int fileType);
	std::pair<bool, long> IsHiddenSection(DType* sym);

	class FindLnToInsert
	{
	  public:
		FindLnToInsert(ULONG line) : Ln(line)
		{
		}
		~FindLnToInsert()
		{
		}

		std::pair<int, bool> GetLnToInsert(LineMarkers::Node& node);
		std::pair<int, bool> GetLnToMove(LineMarkers::Node& node, WTString symName, int& selFrom, int& selTo,
		                                 bool& oneLineSelection, int& multiType);
		std::pair<bool, long> IsHiddenSection(LineMarkers::Node& node, WTString symName);

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		std::pair<int, bool> GetLnToMoveCppB(LineMarkers::Node& node, WTString symName, int& selFrom, int& selTo);
#endif
	  private:
		LineMarkers::Node* GetVisibilityNode(CStringW labelWithColon);

		ULONG Ln;
		WTString Label;
		eSymbolType SymbolType = eSymbolType::NONE;
		LineMarkers::Node* DeepestClass = nullptr;
	};

	WTString RawSnippet; // unexpanded snippet
};

class EncapsulateFieldDlg : public UpdateReferencesDlg
{
  public:
	EncapsulateFieldDlg();
	~EncapsulateFieldDlg();
	BOOL Init(DTypePtr sym, EncapsulateField::sSetterGetter setterGetter, bool hidden);

	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f) override;
	virtual void UpdateStatus(BOOL done, int fileCount) override;
	virtual BOOL OnInitDialog() override;

  private:
	using ReferencesWndBase::OnInitDialog;

  public:
	virtual BOOL OnUpdateStart() override;
	virtual BOOL ValidateInput() override;

	void ResizeEdit1();
	bool IsStarted()
	{
		return mStated;
	}
	WTString GetNewSetterName()
	{
		return mNewSetterName;
	}
	WTString GetNewGetterName()
	{
		return mNewGetterName;
	}
	BOOL ReplaceAroundEqualitySign(const WTString& buf, int assPos, int symPos, const CStringW& replaceWith) const;
	bool IsValidSymbol(const WTString& symbolName);
	void SetErrorStatus(LPCSTR msg);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	void OnChangeEdit();

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	void SetGetterSetterCaptionCppB(const WTString& getterCaption, const WTString& setterCaption);
	void SetGetterSetterTextCppB(bool setGetter = true, bool setSetter = true);
#endif

	DECLARE_MESSAGE_MAP()
	void OnTogglePublicAccessors();
	void OnMoveVarPrivate();
	void OnMoveVarProtected();
	void OnMoveVarNone();
	void OnMoveVarInternal();
	virtual bool GetCommentState() override
	{
		return false;
	}
	virtual void RegisterRenameReferencesControlMovers();
	void ShouldNotBeEmpty() override
	{
	} // can be empty with Encapsulate Field

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	void OnTypePropertyCppB();
	void OnTypeGetSetCppB();
	void OnToggleReadFieldCppB();
	void OnToggleReadVirtualCppB();
	void OnToggleWriteFieldCppB();
	void OnToggleWriteVirtualCppB();
	void OnMovePropPublishedCppB();
	void OnMovePropPublicCppB();
	void OnMovePropPrivateCppB();
	void OnMovePropProtectedCppB();
	void OnMovePropNoneCppB();
	void SetGetterGUIState();
	void SetSetterGUIState();
#endif

  private:
	WTString mEditTxt2;
	CtrlBackspaceEdit<CThemedEdit> mEdit_subclassed2;
	CEdit* mEdit2 = nullptr;

	DType* mSym;
	int mDeclFileType;
	WTString mTemplateStr;
	WTString mStatusText;
	bool mStated = false;
	WTString mNewSetterName;
	WTString mNewGetterName;
	bool mHidden;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	WTString mSymbolName;
	WTString mOriginalSetterName;
	WTString mOriginalGetterName;
#endif
};
