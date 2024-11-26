#pragma once

#include "WTString.h"
#include "FOO.H"
#include <vector>
#include "EdCnt_fwd.h"

class ChangeSignature
{
  public:
	ChangeSignature() : mIsRename(FALSE), mDisplayNagBoxes(TRUE)
	{
	}
	BOOL CanChange(DType* sym);
	BOOL ChangePreviouslyAllowed() const
	{
		return mSym.get() != NULL;
	}

	// prompt user to edit signature
	BOOL Change();

	// just append new parameter to current signature - no prompt
	BOOL AppendParameter(WTString paramDecl);

  private:
	BOOL RunNewDialog(const WTString& bcl, BOOL changeUe4ImplicitMethods = FALSE);
	BOOL CanChange();

  private:
	BOOL mIsRename;
	BOOL mDisplayNagBoxes;
	DTypePtr mSym;
	WTString mNewSignature;
	WTString mParamDeclToAppend;
};

#include "RenameReferencesDlg.h"

class ArgInfo
{
  public:
	ArgInfo() : RefIndex(-1)
	{
	}
	CStringW Type;
	CStringW Name;
	CStringW DefaultValue;
	CStringW TodoValue;
	int RefIndex;

	bool IsOptional() const
	{
		return !DefaultValue.IsEmpty();
	}
};

class ChangeSignatureMap
{
  public:
	ChangeSignatureMap()
	{
		Clear();
	}

	void Clear()
	{
		OldArgs.clear();
		NewArgs.clear();
		ArgsReordered = false;
	}

	std::vector<ArgInfo> OldArgs;
	std::vector<ArgInfo> NewArgs;
	bool ArgsReordered;
};

class ChangeSignatureDlg : public UpdateReferencesDlg
{
  public:
	ChangeSignatureDlg();
	~ChangeSignatureDlg();
	BOOL Init(DTypePtr sym, const WTString& bcl, BOOL mChangeUe4ImplicitMethods = FALSE);
	BOOL AppendArg(WTString mParamDeclToAppend);

  protected:
	DECLARE_MESSAGE_MAP()
	virtual BOOL OnInitDialog() override;

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	virtual void UpdateStatus(BOOL done, int /*unused*/) override;
	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f) override;
	virtual BOOL ValidateInput() override;
	virtual BOOL OnUpdateStart() override;
	virtual void OnUpdateComplete() override;
	virtual void OnSearchComplete(int fileCount, bool wasCanceled) override;
	virtual HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

  private:
	WTString ApplyArgWhitespace(WTString curSig, LPCSTR newSig);
	void SetErrorStatus(LPCSTR msg);
	DTypeList& GetDTypeCache(DTypePtr sym);
	bool GetTodoValue(uint paramIndex, bool lastParam);
	void SearchResultsForIssues(int& deepestBaseClassPos, HTREEITEM item = TVI_ROOT);

	DType* mSym;
	WTString mBCL;
	WTString mOrigSig;
	int mDeclFileType;
	WTString mTemplateStr;
	ChangeSignatureMap mMap;
	std::unique_ptr<FindReferences> mRefsForResultsWnd;
	bool mDisableEdit;
	std::map<WTString, DTypeList> mSymMap;
	EdCntPtr mEdCnt;
	CBrush mWarningBrush;
	WTString mBaseClassWarning;
	WTString mStatusText;
};
