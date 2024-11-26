// CreateFileDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "CreateFileDlg.h"
#include "Colourizer.h"
#include "VAAutomation.h"
#include "FILE.H"
#include "WindowUtils.h"
#include "FileTypes.h"
#include "PROJECT.H"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// CreateFileDlg dialog

IMPLEMENT_DYNAMIC(CreateFileDlg, VADialog)

CreateFileDlg::CreateFileDlg(DlgType dlgType, CStringW invokingFilePath, CStringW initialNewfileName,
                             IncludeOption includeOption)
    : CThemedVADlg(CreateFileDlg::IDD, NULL, fdHorz), mDlgType(dlgType)
{
	mInvokingFileName = Basename(invokingFilePath);
	mFileName = initialNewfileName;

	mLocation = Path(invokingFilePath);
	wchar_t tmp[MAX_PATH] = {0};
	wcscpy_s(tmp, mLocation);
	PathAddBackslashW(tmp);
	mLocation = tmp;

	switch (includeOption)
	{
	case IncludeOption_Hide:
		mShowIncludeControls = false;
		mIncludeNewFile = BST_UNCHECKED;
		break;
	case IncludeOption_Yes:
		mShowIncludeControls = true;
		mIncludeNewFile = BST_CHECKED;
		break;
	case IncludeOption_No:
	default:
		mShowIncludeControls = true;
		mIncludeNewFile = BST_UNCHECKED;
		break;
	}

	if (DlgMoveSelectionToNewFile == mDlgType)
		UseEdCaretPos();
}

CreateFileDlg::~CreateFileDlg()
{
}

void CreateFileDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_NAME, mEditName);
	DDX_Control(pDX, IDC_STATIC_LOCATION, mStaticLocation);
	DDX_Check(pDX, IDC_CHECK_INCLUDE, mIncludeNewFile);
}

BEGIN_MESSAGE_MAP(CreateFileDlg, CThemedVADlg)
ON_EN_CHANGE(IDC_EDIT_NAME, OnEnChangeEditName)
ON_BN_CLICKED(IDC_BROWSE_FOR_DIR, OnBnClickedBrowse)
ON_WM_DESTROY()
END_MESSAGE_MAP()

// CreateFileDlg message handlers

void CreateFileDlg::OnEnChangeEditName()
{
	CWnd* pOkButton = GetDlgItem(IDOK);

	if (mEditName.m_hWnd)
		mEditName.GetText(mFileName);

	bool isOk = false;
	bool canInclude = false;
	CString statusMsg;

	if (mFileName.GetLength())
	{
		if (IsValidFileName(mFileName))
		{
			wchar_t tmp[MAX_PATH] = {0};
			const CStringW newFilePath = PathCombineW(tmp, mLocation, mFileName);
			if (IsFile(newFilePath))
				statusMsg = "File with same name already exists at this location.";
			else
				isOk = true;
			canInclude = (GetFileType(mFileName) == Header);
		}
		else
			statusMsg = "Invalid characters in file name.";
	}

	if (!statusMsg.IsEmpty() && gTestLogger)
		gTestLogger->LogStr(WTString(statusMsg));

	GetDlgItem(IDC_STATUS)->SetWindowText(statusMsg);
	GetDlgItem(IDC_CHECK_INCLUDE)->EnableWindow(canInclude);
	if (!canInclude)
		CheckDlgButton(IDC_CHECK_INCLUDE, BST_UNCHECKED);

	pOkButton->EnableWindow(isOk);
}

// [case: 164403]
void CreateFileDlg::OnBnClickedBrowse()
{
	CFolderPickerDialog folderPickerDialog((CString)mLocation, 0, this, 0);

	if (folderPickerDialog.DoModal() == IDOK)
	{
		CString selectedPath = folderPickerDialog.GetPathName();

		mLocation = selectedPath;
		SetDlgItemText(IDC_STATIC_LOCATION, (CString)mLocation);
	}
}

BOOL CreateFileDlg::OnInitDialog()
{
	__super::OnInitDialog();

	switch (mDlgType)
	{
	case DlgCreateFile:
		SetHelpTopic(WTString("dlgCreateFile"));
		SetWindowText("Create File");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Create File dlg"));
		break;

	case DlgMoveSelectionToNewFile:
		SetHelpTopic(WTString("dlgMoveSelToNewFile"));
		SetWindowText("Move Selection to New File");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Move Selection to New File dlg"));
		break;

	case DlgCreateStructAndFile:
		SetHelpTopic(WTString("dlgCreateFileForNewClass"));
		SetWindowText("Create File For New Struct");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Create File For New Struct dlg"));
		break;

	case DlgCreateClassAndFile:
		SetHelpTopic(WTString("dlgCreateFileForNewClass"));
		SetWindowText("Create File For New Class");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Create File For New Class dlg"));
		break;

	case DlgMoveClassToNewFile:
	{
		SetHelpTopic(WTString("dlgMoveClassToNewFile"));
		SetWindowText("Move Class to New File");
		if (gTestLogger)
			gTestLogger->LogStr(WTString("Move Class to New File dlg"));

		// [case: 164403]
		CWnd* pBrowseButton = GetDlgItem(IDC_BROWSE_FOR_DIR);
		if (pBrowseButton)
		{
			pBrowseButton->ShowWindow(SW_SHOW);
			CWnd* pEditName = GetDlgItem(IDC_EDIT_NAME);
			if (pEditName)
			{
				RECT rect;
				rect.left = 0;
				rect.top = 0;
				rect.right = 240;
				rect.bottom = 14;
				MapDialogRect(&rect);
				pEditName->SetWindowPos(nullptr, 0, 0, rect.right, rect.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
		break;
	}

	default:
		_ASSERTE(!"unhandled mDlgType");
		break;
	}

	if (!mShowIncludeControls)
		HideIncludeControls();

	if (gTestLogger)
	{
		gTestLogger->LogStr(WTString(L"\tName = " + mFileName));

		auto slnFile = GlobalProject->SolutionFile();
		auto slnDir = Path(slnFile);
		slnDir.MakeLower();

		CStringW tmpLocation = mLocation;
		tmpLocation.MakeLower();

		tmpLocation.Replace(slnDir, L"%SolutionDir%");

		gTestLogger->LogStr(WTString(L"\tLocation = " + tmpLocation));
	}

	mEditName.SetText(mFileName);
	CString s = mLocation.GetBuffer();
	mStaticLocation.SetWindowText(s);

	::mySetProp(mEditName, "__VA_do_not_colour", (HANDLE)1);
	::mySetProp(mStaticLocation, "__VA_do_not_colour", (HANDLE)1);

	AddSzControl(IDC_EDIT_NAME, mdResize, mdNone);
	AddSzControl(IDC_STATIC_LOCATION, mdResize, mdNone);
	AddSzControl(IDC_CHECK_INCLUDE, mdResize, mdNone);
	AddSzControl(IDC_STATUS, mdResize, mdNone);
	AddSzControl(IDOK, mdRepos, mdNone);
	AddSzControl(IDCANCEL, mdRepos, mdNone);
	if (mDlgType == DlgMoveClassToNewFile)
		AddSzControl(IDC_BROWSE_FOR_DIR, mdRepos, mdNone); // [case: 164403]

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATIC);
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATUS);
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATIC_LOCATION);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHECK_INCLUDE, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_BROWSE_FOR_DIR, this); // [case: 164403]

		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	if (DlgMoveSelectionToNewFile == mDlgType)
	{
		// don't store position at exit since the position is overridden
		// to make caret in editor visible - not applicable to other DlgTypes.
		// OK to use size previously stored though, so NoAutoPos called after init.
		NoAutoPos();
	}

	OnEnChangeEditName();

	VAUpdateWindowTitle(VAWindowType::CreateFile, *this);

	CEdit* pEdit = (CEdit*)GetDlgItem(IDC_EDIT_NAME);
	if (pEdit)
	{
		pEdit->SetFocus();
		pEdit->SetSel(0, -1);
		return FALSE;
	}

	return TRUE; // return TRUE unless you set the focus to a control
}

void CreateFileDlg::OnDestroy()
{
	if (mEditName.m_hWnd)
		mEditName.UnsubclassWindow();
	if (mStaticLocation.m_hWnd)
		mStaticLocation.UnsubclassWindow();

	VADialog::OnDestroy();
}

void CreateFileDlg::HideIncludeControls()
{
	GetDlgItem(IDC_CHECK_INCLUDE)->ShowWindow(SW_HIDE);
}
