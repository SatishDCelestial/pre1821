#pragma once
#include "VAThemeDraw.h"

// CInsertPathDialog dialog

class CInsertPathDialog : public CThemedVADlg
{
	DECLARE_DYNAMIC(CInsertPathDialog)

  public:
	enum class AutoGetUserPathEnum
	{
		Disabled,
		BeforeIsVisible,
		AfterIsVisible
	};

	enum class PathTypeEnum
	{
		Invalid,
		Absolute,
		RelativeToFile,
		RelativeToProject,
		RelativeToSolution
	};

	CInsertPathDialog(CWnd* pParent = nullptr); // standard constructor
	virtual ~CInsertPathDialog();

	CStringW UserPath;
	CStringW FixedPath;
	PathTypeEnum PathType;
	bool ValidatePath;
	AutoGetUserPathEnum AutoGetUserPath; // must be set before opening the dialog

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum
	{
		IDD = IDD_INSERTPATH
	};
#endif

  protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;

	void SaveSettings();
	void LoadSettings();
	void LogForAST();

	void OnUserPathChanged();
	void OnBnClickedPath();
	void OnBnClickedOk();
	void OnBnClickedAbsRel();
	void OnBnClickedFilePrjSln();
	void OnBnClickedFwdSlash();

	void OnShowWindow(BOOL bShow, UINT nStatus);
	void OnTimer(UINT_PTR nTimerID);

	void UpdateOutput();
	void SetFixedPath();
	void GetUserPath();

	bool GetFileDir(CStringW& dir);
	bool GetProjectDir(CStringW& dir);
	bool GetSolutionDir(CStringW& dir);

	DECLARE_MESSAGE_MAP()

	CThemedEdit mEditPath;
	CThemedEditACID<true> mEditPreview;

	CThemedButton mButtonPath;
	CThemedRadioButton mRadioAbsolute;
	CThemedRadioButton mRadioRelative;
	CThemedRadioButton mRadioCurrentFile;
	CThemedRadioButton mRadioProject;
	CThemedRadioButton mRadioSolution;
	CThemedCheckBox mCheckFwdSlash;
};
