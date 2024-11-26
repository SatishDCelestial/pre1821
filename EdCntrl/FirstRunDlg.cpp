#include "stdafxed.h"
#include "FirstRunDlg.h"
#include "PROJECT.H"
#include "Settings.h"
#include "IdeSettings.h"
#include "VaOptions.h"
#include "KeyBindings.h"
#include "RegKeys.h"
#include "DpiCookbook\VsUIDpiAwareness.h"
#include "DevShellService.h"

#if !defined(RAD_STUDIO)

IMPLEMENT_DYNAMIC(FirstRunDlg, VADialog)

FirstRunDlg::FirstRunDlg(CWnd* pParent /*= nullptr*/)
    : CThemedVADlg(IDD, pParent, fdNone, flNoSavePos)
{
}

FirstRunDlg::~FirstRunDlg()
{
}

bool FirstRunDlg::LaunchFirstRunDlg()
{	
	if (Psettings && Psettings->mFirstRunDialogStatus == 0)
	{
		if (!Psettings->IsFirstRunAfterInstall())
		{
			// it is not a first run, set status to 1 to avoid future expensive call to
			// IsFirstRunAfterInstall and return here without showing the dialog
			Psettings->mFirstRunDialogStatus = 1;
			Psettings->SaveRegFirstRunDialogStatus();
			return false;
		}
		
		VsUI::CDpiScope dpi;
		
		// show dialog
		FirstRunDlg fr(gMainWnd);
		fr.DoModal();
		return true;
	}
	else
	{
		// dlg was not shown
		return false;
	}
}

BOOL FirstRunDlg::OnInitDialog()
{
	__super::OnInitDialog();

	mBtnPartial.ModifyStyle(0, BS_ICON);
	HICON hIconPartial = (HICON)LoadImage(
	    AfxGetApp()->m_hInstance,
	    MAKEINTRESOURCE(IDI_FIRST_RUN_LESS),
	    IMAGE_ICON,
	    256, 223,
	    LR_LOADTRANSPARENT);
	mBtnPartial.SetIcon(hIconPartial);

	mBtnFull.ModifyStyle(0, BS_ICON);
	HICON hIconFull = (HICON)LoadImage(
	    AfxGetApp()->m_hInstance,
	    MAKEINTRESOURCE(IDI_FIRST_RUN_MORE),
	    IMAGE_ICON,
	    256, 223,
	    LR_LOADTRANSPARENT);
	mBtnFull.SetIcon(hIconFull);

	mBtnPartial.SetCheck(BST_UNCHECKED);
	mBtnFull.SetCheck(BST_CHECKED);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedStatic>(IDC_STATIC_FIRST_RUN_PARTIAL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedStatic>(IDC_STATIC_FIRST_RUN_FULL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedGroup>(IDC_GROUPBOX_FIRST_RUN, this);

		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	return TRUE;
}

void FirstRunDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BUTTON_FIRST_RUN_PARTIAL, mBtnPartial);
	DDX_Control(pDX, IDC_BUTTON_FIRST_RUN_FULL, mBtnFull);
	DDX_Control(pDX, IDC_BUTTON_FIRST_RUN_DETAILS, mBtnDetails);
	DDX_Control(pDX, IDC_BUTTON_FIRST_RUN_NEXT, mBtnNext);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(FirstRunDlg, CThemedVADlg)
ON_BN_CLICKED(IDC_BUTTON_FIRST_RUN_PARTIAL, OnBnClickedPartial)
ON_BN_CLICKED(IDC_BUTTON_FIRST_RUN_FULL, OnBnClickedFull)
ON_BN_CLICKED(IDC_BUTTON_FIRST_RUN_DETAILS, OnBnClickedDetails)
ON_BN_CLICKED(IDC_BUTTON_FIRST_RUN_NEXT, OnBnClickedNext)
END_MESSAGE_MAP()
#pragma warning(pop)

void FirstRunDlg::OnBnClickedPartial()
{
	mBtnPartial.SetCheck(BST_CHECKED);
	mBtnFull.SetCheck(BST_UNCHECKED);
}

void FirstRunDlg::OnBnClickedFull()
{
	mBtnPartial.SetCheck(BST_UNCHECKED);
	mBtnFull.SetCheck(BST_CHECKED);
}

void FirstRunDlg::OnBnClickedDetails()
{
	// todo: need a correct link which will be opened for details
	::ShellExecute(NULL, _T("open"), "https://www.wholetomato.com/features",
	               NULL, NULL, SW_SHOW);
}

void FirstRunDlg::OnBnClickedNext()
{
	if (mBtnFull.GetCheck())
	{
		// apply full settings
		
		if (!Psettings || !gShellAttr || !g_IdeSettings)
			return;

		// apply all coloring
		Psettings->m_bEnhColorListboxes = true;
		Psettings->m_bEnhColorObjectBrowser = true;
		Psettings->m_bEnhColorTooltips = true;
		Psettings->m_bEnhColorViews = true;
		Psettings->m_bEnhColorFindResults = true;
		Psettings->m_bEnhColorWizardBar = true;
		Psettings->m_bLocalSymbolsInBold = true;

		// highlight current line - thin frame
		if (gShellAttr->IsDevenv10OrHigher())
		{
			Psettings->mMarkCurrentLine = true;
			Psettings->mCurrentLineBorderStyle = 1;
			Psettings->mCurrentLineVisualStyle = 0x2100;

			// disable "highlight current line" in VS
			if (g_IdeSettings->GetEditorBoolOption("General", "HighlightCurrentLine"))
				g_IdeSettings->SetEditorOption("General", "HighlightCurrentLine", "FALSE");
		}

		// show quick info tooltips for more symbols
		// both sub options enabled
		if (gShellAttr->IsDevenv8OrHigher())
		{
			// parent option affects two settings
			Psettings->mQuickInfoInVs8 = true;
			Psettings->m_mouseOvers = true;
			
			// sub options
			Psettings->m_bGiveCommentsPrecedence = true;
			Psettings->mScopeTooltips = true;
		}

		// enable ctrl+left click
		// todo: this one is overlapping with VS? what to do about it?
		Psettings->mMouseClickCmds |= 0x00000001;

		// enable alt + left click
		Psettings->mMouseClickCmds |= 0x00000020;

		// mouse execute VA context menu on middle click
		Psettings->mMouseClickCmds |= 0x00000300;

		// source links enabled - file and url
		// this can't be set here since it is in managed part and does not exist at this point
		// it will use mFirstRunDialogStatus there to determine if it need to be set initially

		// set status of first run to 2 (full settings)
		// force it to be written in registry (needed for source links settings)
		Psettings->mFirstRunDialogStatus = 2;
		Psettings->SaveRegFirstRunDialogStatus();

		// settings are changed outside the option dialog so we need to update them
		::VaOptionsUpdated();
	}
	else
	{
		// apply partial settings
		
		// set status of first run to 1 (partial settings)
		Psettings->mFirstRunDialogStatus = 1;
	}

	OnOK();

	// open recommended key binding dialog
	CheckForKeyBindingUpdate(true);
}

void FirstRunDlg::OnCancel()
{
	// if user click on close button or press ESC, prevent closing
	// the dialog and inform user to choose desired option 
	CString errMsg("Please confirm desired VA integration option by clicking on the 'Next >' button.");
	WtMessageBox(this->m_hWnd, errMsg, IDS_APPNAME, MB_OK | MB_ICONINFORMATION);
}

#endif
