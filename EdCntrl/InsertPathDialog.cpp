// InsertPathDialog.cpp : implementation file
//

#include "stdafxed.h"
#include "InsertPathDialog.h"
#include "resource.h"
#include "..\VAOpsWin\sentence.h"
#include "VaService.h"
#include "EDCNT.H"
#include "FILE.H"
#include "PROJECT.H"
#include "ProjectInfo.h"
#include "Settings.h"
#include "VAAutomation.h"
#include "VAWatermarks.h"

// CInsertPathDialog dialog

IMPLEMENT_DYNAMIC(CInsertPathDialog, CDialog)

CInsertPathDialog::CInsertPathDialog(CWnd* pParent /*=nullptr*/)
    : CThemedVADlg(IDD_INSERTPATH, pParent, cdxCDynamicWnd::fdHoriz, flSizeIcon | flAntiFlicker),
      PathType(PathTypeEnum::Invalid), ValidatePath(false), AutoGetUserPath(AutoGetUserPathEnum::AfterIsVisible)
{
	SetHelpTopic("dlgInsertPath");
}

CInsertPathDialog::~CInsertPathDialog()
{
}

void CInsertPathDialog::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDITPATH, mEditPath);
	DDX_Control(pDX, IDC_BUTTONPATH, mButtonPath);
	DDX_Control(pDX, IDC_RADIOABSOLUTE, mRadioAbsolute);
	DDX_Control(pDX, IDC_RADIORELATIVE, mRadioRelative);
	DDX_Control(pDX, IDC_RADIOCURRENTFILE, mRadioCurrentFile);
	DDX_Control(pDX, IDC_RADIOPROJECT, mRadioProject);
	DDX_Control(pDX, IDC_RADIOSOLUTION, mRadioSolution);
	DDX_Control(pDX, IDC_EDITPREVIEW, mEditPreview);
	DDX_Control(pDX, IDC_FWDSLASH, mCheckFwdSlash);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CInsertPathDialog, CThemedVADlg)
ON_BN_CLICKED(IDOK, OnBnClickedOk)
ON_EN_CHANGE(IDC_EDITPATH, OnUserPathChanged)
ON_BN_CLICKED(IDC_BUTTONPATH, OnBnClickedPath)
ON_BN_CLICKED(IDC_RADIOABSOLUTE, OnBnClickedAbsRel)
ON_BN_CLICKED(IDC_RADIORELATIVE, OnBnClickedAbsRel)
ON_BN_CLICKED(IDC_RADIOCURRENTFILE, OnBnClickedFilePrjSln)
ON_BN_CLICKED(IDC_RADIOPROJECT, OnBnClickedFilePrjSln)
ON_BN_CLICKED(IDC_RADIOSOLUTION, OnBnClickedFilePrjSln)
ON_BN_CLICKED(IDC_FWDSLASH, OnBnClickedFwdSlash)
ON_WM_SHOWWINDOW()
ON_WM_TIMER()
END_MESSAGE_MAP()
#pragma warning(pop)

void CInsertPathDialog::OnUserPathChanged()
{
	UpdateOutput();
}

void CInsertPathDialog::OnBnClickedPath()
{
	// user clicked on ... button
	GetUserPath();
}

void CInsertPathDialog::OnBnClickedOk()
{
	// user clicked on OK button

	SaveSettings();

	if (gTestsActive)
		LogForAST();

	EndDialog(IDOK);
}

void CInsertPathDialog::OnBnClickedAbsRel()
{
	// Absolute/Relative

	BOOL relative = mRadioRelative.GetCheck() == BST_CHECKED ? TRUE : FALSE;

	CStringW dir;
	mRadioCurrentFile.EnableWindow(relative && GetFileDir(dir));
	mRadioProject.EnableWindow(relative && GetProjectDir(dir));
	mRadioSolution.EnableWindow(relative && GetSolutionDir(dir));

	UpdateOutput();
}

void CInsertPathDialog::OnBnClickedFilePrjSln()
{
	// File/Project/Solution

	UpdateOutput();
}

void CInsertPathDialog::OnBnClickedFwdSlash()
{
	// Forward slash changed

	UpdateOutput();
}

void CInsertPathDialog::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (bShow)
		RestoreWindowPosition("InsertPath");
	else
		StoreWindowPosition("InsertPath");

	if (bShow)
		mEditPath.SetFocus();

	if (bShow && AutoGetUserPath > AutoGetUserPathEnum::Disabled)
	{
		if (AutoGetUserPath == AutoGetUserPathEnum::BeforeIsVisible)
		{
			AutoGetUserPath = AutoGetUserPathEnum::Disabled;
			GetUserPath();
			if (UserPath.IsEmpty())
				UpdateOutput();
		}
		else if (AutoGetUserPath == AutoGetUserPathEnum::AfterIsVisible)
		{
			SetTimer('GUPT', 5, nullptr);
		}
	}
}

void CInsertPathDialog::OnTimer(UINT_PTR nTimerID)
{
	KillTimer('GUPT');
	if (AutoGetUserPath == AutoGetUserPathEnum::AfterIsVisible)
	{
		AutoGetUserPath = AutoGetUserPathEnum::Disabled;
		GetUserPath();
		if (UserPath.IsEmpty())
			UpdateOutput();
	}
}

bool CInsertPathDialog::GetFileDir(CStringW& dir)
{
	EdCntPtr ed(g_currentEdCnt);
	Project* gp = GlobalProject;

	dir.Empty();

	if (ed == nullptr || gp == nullptr)
		return false;

	dir = ::Path(ed->FileName());
	return ::IsDir(dir);
}

bool CInsertPathDialog::GetProjectDir(CStringW& dir)
{
	EdCntPtr ed(g_currentEdCnt);
	Project* gp = GlobalProject;

	dir.Empty();

	if (ed == nullptr || gp == nullptr)
		return false;

	ProjectVec projs = gp->GetProjectForFile(ed->FileName());
	if (!projs.empty())
	{
		dir = ::Path(projs.front()->GetProjectFile());
		return ::IsDir(dir);
	}

	return false;
}

bool CInsertPathDialog::GetSolutionDir(CStringW& dir)
{
	EdCntPtr ed(g_currentEdCnt);
	Project* gp = GlobalProject;

	dir.Empty();

	if (ed == nullptr || gp == nullptr)
		return false;

	ProjectVec projs = gp->GetProjectForFile(ed->FileName());
	if (!projs.empty())
	{
		dir = ::Path(gp->SolutionFile());
		return ::IsDir(dir);
	}

	return false;
}

void CInsertPathDialog::SetFixedPath()
{
	FixedPath = UserPath;

	if (mCheckFwdSlash.GetCheck() == BST_CHECKED)
		FixedPath.Replace(L'\\', L'/');
	else
		FixedPath.Replace(L'/', L'\\');

	// trim absolute paths during AST
	// don't use IsPathAbsolute intentionally
	if (gTestsActive && PathType == PathTypeEnum::Absolute)
	{
		FixedPath = TrimForAst(FixedPath);
	}
}

void CInsertPathDialog::UpdateOutput()
{
	// Note: Expect invalid input!

	auto update_preview = [&](LPCWSTR error = nullptr) {
		bool valid = error == nullptr;
		auto ok = GetDlgItem(IDOK);

		if (ok && valid != !!ok->IsWindowEnabled())
			ok->EnableWindow(valid);

		if (valid)
		{
			SetFixedPath();
			mEditPreview.RemoveTextOverrideColor(ThemeDraw::EditIDE11Draw::ColorIndex::Disabled);
			mEditPreview.SetText(FixedPath);
		}
		else
		{
			mEditPreview.SetTextOverrideColor(ThemeDraw::EditIDE11Draw::ColorIndex::Disabled, RGB(255, 0, 0));
			mEditPreview.SetText(CStringW(error));
		}
	};

	CStringW userPath;
	mEditPath.GetText(userPath);
	if (userPath.IsEmpty())
	{
		PathType = PathTypeEnum::Invalid;
		UserPath.Empty();
		if (AutoGetUserPath == AutoGetUserPathEnum::Disabled)
			update_preview(L"");
		else
			update_preview();

		return;
	}

	if (ValidatePath && !IsValidPath(userPath))
	{
		PathType = PathTypeEnum::Invalid;
		UserPath.Empty();
		update_preview(L"ERROR: Path contains invalid characters.");
		return;
	}

	if (mRadioAbsolute.GetCheck() == BST_CHECKED)
	{
		PathType = PathTypeEnum::Absolute;
		UserPath = userPath;
		update_preview();
	}
	else
	{
		auto dirRel = PathTypeEnum::Invalid;
		CStringW dir;

		if (mRadioCurrentFile.IsWindowEnabled() && mRadioCurrentFile.GetCheck() == BST_CHECKED)
		{
			if (GetFileDir(dir))
				dirRel = PathTypeEnum::RelativeToFile;
		}
		else if (mRadioProject.IsWindowEnabled() && mRadioProject.GetCheck() == BST_CHECKED)
		{
			if (GetProjectDir(dir))
				dirRel = PathTypeEnum::RelativeToProject;
		}
		else if (mRadioSolution.IsWindowEnabled() && mRadioSolution.GetCheck() == BST_CHECKED)
		{
			if (GetSolutionDir(dir))
				dirRel = PathTypeEnum::RelativeToSolution;
		}

		if (dir.IsEmpty() || dirRel == PathTypeEnum::Invalid)
		{
			PathType = PathTypeEnum::Absolute;
			UserPath = userPath;
			update_preview();
		}
		else
		{
			PathType = PathTypeEnum::Invalid;
			UserPath.Empty();

			if (::BuildRelativePathEx(UserPath, userPath, dir))
			{
				PathType = dirRel;
				update_preview();
			}
			else
			{
				CStringW errMsg = L"ERROR: Current ";
				switch (dirRel)
				{
				case PathTypeEnum::RelativeToFile:
					errMsg += L"file";
					break;
				case PathTypeEnum::RelativeToProject:
					errMsg += L"project";
					break;
				case PathTypeEnum::RelativeToSolution:
					errMsg += L"solution";
					break;
				default:
					break;
				}
				errMsg += L" and selected file must share a drive letter.";
				update_preview(errMsg);
			}
		}
	}
}

void CInsertPathDialog::GetUserPath()
{
	CStringW initDirWStr;
	static bool isFirstTime = true;

	if (isFirstTime || gTestsActive)
	{
		isFirstTime = false;
		GetFileDir(initDirWStr);
	}

	CStringW path = VaGetOpenFileName(m_hWnd, L"Insert Path", initDirWStr.IsEmpty() ? nullptr : (LPCWSTR)initDirWStr,
	                                  L"All files (*.*)\0*.*\0\0");

	if (!path.IsEmpty())
		this->mEditPath.SetText(path);
}

BOOL CInsertPathDialog::OnInitDialog()
{
	CThemedVADlg::OnInitDialog();

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATICPATHLABEL);
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATICPREVIEWLABEL);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	LoadSettings();
	OnBnClickedAbsRel();

	AddSzControl(IDC_EDITPATH, mdResize, mdNone);
	AddSzControl(IDC_BUTTONPATH, mdRepos, mdNone);
	AddSzControl(IDC_EDITPREVIEW, mdResize, mdNone);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);

	VAUpdateWindowTitle(VAWindowType::InsertPath, *this);

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void CInsertPathDialog::SaveSettings()
{
	if (Psettings == nullptr)
		return;

	BOOL relative = mRadioRelative.GetCheck() == BST_CHECKED ? TRUE : FALSE;
	if (!relative)
		Psettings->mInsertPathMode = CSettings::IPM_Absolute;
	else
	{
		if (mRadioCurrentFile.IsWindowEnabled() && mRadioCurrentFile.GetCheck() == BST_CHECKED)
			Psettings->mInsertPathMode = CSettings::IPM_RelativeToFile;
		else if (mRadioProject.IsWindowEnabled() && mRadioProject.GetCheck() == BST_CHECKED)
			Psettings->mInsertPathMode = CSettings::IPM_RelativeToProject;
		else if (mRadioSolution.IsWindowEnabled() && mRadioSolution.GetCheck() == BST_CHECKED)
			Psettings->mInsertPathMode = CSettings::IPM_RelativeToSolution;
	}

	Psettings->mInsertPathFwdSlash = mCheckFwdSlash.GetCheck() == BST_CHECKED;
}

void CInsertPathDialog::LoadSettings()
{
	if (Psettings == nullptr)
		return;

	if (Psettings->mInsertPathMode == CSettings::IPM_Absolute)
	{
		mRadioAbsolute.SetCheck(BST_CHECKED);
		mRadioCurrentFile.SetCheck(BST_CHECKED);
	}
	else
	{
		mRadioRelative.SetCheck(BST_CHECKED);
		switch (Psettings->mInsertPathMode)
		{
		case CSettings::IPM_RelativeToFile:
			mRadioCurrentFile.SetCheck(BST_CHECKED);
			break;
		case CSettings::IPM_RelativeToProject:
			mRadioProject.SetCheck(BST_CHECKED);
			break;
		case CSettings::IPM_RelativeToSolution:
			mRadioSolution.SetCheck(BST_CHECKED);
			break;
		}
	}

	mCheckFwdSlash.SetCheck(Psettings->mInsertPathFwdSlash ? BST_CHECKED : BST_UNCHECKED);

	mEditPath.SetText(L"");
	mEditPath.SetSel(0, -1);
}

void CInsertPathDialog::LogForAST()
{
	if (gTestsActive && gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		CStringW logStr("Insert Path:");

		BOOL relative = mRadioRelative.GetCheck() == BST_CHECKED ? TRUE : FALSE;
		if (!relative)
			logStr += L"\r\nMode: Absolute";
		else
		{
			if (mRadioCurrentFile.IsWindowEnabled() && mRadioCurrentFile.GetCheck() == BST_CHECKED)
				logStr += L"\r\nMode: Relative to file";
			else if (mRadioProject.IsWindowEnabled() && mRadioProject.GetCheck() == BST_CHECKED)
				logStr += L"\r\nMode: Relative to project";
			else if (mRadioSolution.IsWindowEnabled() && mRadioSolution.GetCheck() == BST_CHECKED)
				logStr += L"\r\nMode: Relative to solution";
		}

		if (mCheckFwdSlash.GetCheck() == BST_CHECKED)
			logStr += L"\r\nForward slash: True";
		else
			logStr += L"\r\nForward slash: False";

		CStringW userPath;
		if (mEditPath.GetText(userPath))
		{
			if (IsPathAbsolute(userPath))
				userPath = TrimForAst(userPath);

			logStr += L"\r\nInput: " + userPath;
		}

		CStringW preview;
		if (mEditPreview.GetText(preview))
		{
			if (IsPathAbsolute(preview))
				preview = TrimForAst(preview);

			logStr += L"\r\nPreview: " + preview;
		}

		logStr += L"\r\nOutput: " + FixedPath;

		gTestLogger->LogStrW(logStr);
	}
}
