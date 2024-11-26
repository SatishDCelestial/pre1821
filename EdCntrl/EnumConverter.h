#pragma once

#include "RenameReferencesDlg.h"

class EnumConverter
{
  public:
	EnumConverter()
	{
	}
	~EnumConverter()
	{
	}

	bool CanConvert();
	WTString GetUnderlyingType(WTString def);
	bool Convert(DType* sym);

	static BOOL DoModal(WTString symScope, const WTString& enumName, const WTString& underlyingType,
	                    const std::set<WTString>& elements);
	static void InsertClass();
};

class EnumConverterDlg : public UpdateReferencesDlg
{
  public:
	EnumConverterDlg();
	~EnumConverterDlg();
	static EnumConverterDlg* Me;

	BOOL Init(WTString symScope, const WTString& enumName, const WTString& underlyingType,
	          const std::set<WTString>& elements);
	bool IsStarted()
	{
		return mStarted;
	}

  protected:
	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f);
	virtual void UpdateStatus(BOOL done, int fileCount);
	virtual BOOL OnUpdateStart();
	virtual void ShouldNotBeEmpty();

	virtual void RegisterReferencesControlMovers() override;
	virtual void RegisterRenameReferencesControlMovers() override;
	virtual BOOL OnInitDialog() override;

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	virtual void OnSearchComplete(int fileCount, bool wasCanceled) override;

	bool mStarted = false;
	WTString EnumName;
	WTString UnderlyingType;
	int DebugCounter = 0;
	bool FirstFile = true;
	uint LastFileID = 0;
};
