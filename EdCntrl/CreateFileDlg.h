#pragma once

#include "VADialog.h"
#include "resource.h"
#include "VAThemeDraw.h"

// CreateFileDlg dialog

enum IncludeOption
{
	IncludeOption_Hide,
	IncludeOption_No,
	IncludeOption_Yes
};

class CreateFileDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(CreateFileDlg)

  public:
	enum DlgType
	{
		DlgCreateFile,
		DlgMoveSelectionToNewFile,
		DlgCreateClassAndFile,
		DlgCreateStructAndFile,
		DlgMoveClassToNewFile
	};
	CreateFileDlg(DlgType dlgType, CStringW invokingFilePath, CStringW initialNewFileName, IncludeOption includeOption);
	virtual ~CreateFileDlg();

	// Dialog Data
	enum
	{
		IDD = IDD_CREATEFILEDLG
	};

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support

	DECLARE_MESSAGE_MAP()
  public:
	afx_msg void OnEnChangeEditName();
	afx_msg void OnBnClickedBrowse();
	virtual BOOL OnInitDialog();
	afx_msg void OnDestroy();

	CStringW GetFileName() const
	{
		return mFileName;
	}
	CStringW GetLocation() const
	{
		return mLocation;
	}
	bool IsAddIncludeChecked() const
	{
		return mIncludeNewFile == BST_CHECKED;
	}

  private:
	void HideIncludeControls();

	const DlgType mDlgType;
	CStringW mInvokingFileName;
	CStringW mFileName;
	CStringW mLocation;
	CThemedEdit mEditName;
	CStatic mStaticLocation;
	bool mShowIncludeControls;
	int mIncludeNewFile;
};
