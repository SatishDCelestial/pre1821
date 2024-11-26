#pragma once

#include "LocalRefactoring.h"
#include "RenameReferencesDlg.h"
#include "EdCnt_fwd.h"

class VAScopeInfo_MLC;

enum class eConversionType
{
	UNINITIALIZED,
	POINTER_TO_INSTANCE,
	INSTANCE_TO_POINTER
};

enum class eEndPosCorrection
{
	NONE,
	FOUND_SEMICOLON,
	FOUND_EOL
};

struct ConversionInfo
{
	eConversionType Type = eConversionType::UNINITIALIZED;
	long PointerPos = -1;
	long NewPos = -1;
	long AddressPos = -1;
	long EqualitySign = -1;
	int TypeOfCall = UNDEF; // is it a method or a constructor call? neither if it stays UNDEF
	WTString CallName;      // the name of such calls

	bool IsPointer(bool simplify)
	{
		return PointerPos != -1 || NewPos != -1 ||
		       (simplify &&
		        (TypeOfCall == CLASS || TypeOfCall == STRUCT || TypeOfCall == RESWORD || TypeOfCall == TYPE));
	}
	bool Chain = false;
};

CStringW GetRightType(const WTString& buf, const ConversionInfo& Info, long symPos);
WTString GetLeftType(long symPos, const WTString& buf, long& typeFrom, long& typeTo);
WTString GetWordIfPresentBeforeSym(const WTString& buf, long symPos, long& pos);
WTString GetDotOrArrowIfPresentAfterSym(const WTString& buf, long symPos, WTString symName, long& pos);
eEndPosCorrection CorrectEndPosOfDelete(const WTString& buf, long& endPos, bool doNotFindEOL);
WTString GetStarOrAddressIfPresentBeforeSym(const WTString& buf, long symPos, long& pos);
UpdateReferencesDlg::UpdateResult ConvertDefinitionSiteToInstance(long symPos, const WTString& buf,
                                                                  const ConversionInfo& Info);

class ConvertBetweenPointerAndInstance : public LocalRefactoring
{
	enum class eNewProblem
	{
		NO_PROBLEM,
		PLACEMENT_PARAM,
		OPERATOR,
		ARRAY,
	};

	friend class ConvertBetweenPointerAndInstanceDlg;

  public:
	ConvertBetweenPointerAndInstance()
	{
	}
	~ConvertBetweenPointerAndInstance()
	{
	}

	BOOL CanConvert(bool simplify);
	BOOL Convert(DType* sym, VAScopeInfo_MLC& info, WTString& methodScope, CStringW& filePath, bool simplify);

	void UpdateTypeOfMethodCall();
	void SetSimplify(bool val)
	{
		Simplify = val;
	}
	eConversionType FigureOutConversionType(bool tryToSimplify);

  private:
	bool IsThereParenAfterEqual(WTString& name);
	bool IsPointerOrInstanceVariable(const WTString& fileBuf, long pos, bool simplify);
	BOOL DoModal(DTypePtr sym);

	ConversionInfo Info;
	bool Simplify = false;
	eNewProblem IsThereNewProblem(const WTString& buf, long symPos);
};

class ConvertBetweenPointerAndInstanceDlg : public UpdateReferencesDlg
{
	friend class ConvertBetweenPointerAndInstance;

  public:
	ConvertBetweenPointerAndInstanceDlg(uint dlgID);
	~ConvertBetweenPointerAndInstanceDlg();

	BOOL Init(DTypePtr sym, ConversionInfo info);
	bool IsStarted()
	{
		return mStarted;
	}
	void ShouldNotBeEmpty() override
	{
	} // can be empty with Convert refactoring
	void OnChangeEditCustomType();

  private:
	WTString mStatusText;
	bool mStarted = false;
	ConversionInfo mInfo;
	int mNrOfAssignments = 0;
	int mStackAddressReturns = 0;
	WTString mError;
	WTString mEditTypeNameText;
	CEdit* mEditTypeName;
	CtrlBackspaceEdit<CThemedEdit> mEditTypeName_subclassed;
	CButton mCustomSmartPointer;

	bool IsSymOrNumBefore(const WTString& buf, long pos);
	void SearchResultsForIssues(HTREEITEM item = TVI_ROOT);
	void RefreshRadiosViaConvertToPointerType();
	void UpdateCustomEditBox();
	long FindHigherThanStarPrecedenceAfterSym(const WTString& buf, long symPos);
	afx_msg void OnDestroy();

  protected:
	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f) override;

	virtual void UpdateStatus(BOOL done, int fileCount) override;
	void GetStatusMessageViaRadios(WTString& msg);
	virtual BOOL OnInitDialog() override;

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	virtual void OnSearchComplete(int fileCount, bool wasCanceled) override;
	virtual BOOL ValidateInput() override;
	virtual void RegisterReferencesControlMovers() override;
	virtual void RegisterRenameReferencesControlMovers() override;
	virtual bool GetCommentState() override
	{
		return false;
	}

	DECLARE_MESSAGE_MAP()

	void OnToggleUseAuto();
	void OnRawPtr();
	void OnUniquePtr();
	void OnSharedPtr();
	void OnCustomPtr();
	void SetErrorStatus(const WTString& msg);
	bool IsTemplateType(WTString symName);
};
