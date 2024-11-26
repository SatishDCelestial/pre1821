#include "stdafxed.h"
#include "resource.h"
#include "AboutDlg.h"
#include "Directories.h"
#include "LogVersionInfo.h"
#include "FILE.H"
#include "WindowUtils.h"
#include "incToken.h"
#include "PROJECT.H"
#include "Registry.h"
#include "RegKeys.h"
#include "BuildInfo.h"
#include "DpiCookbook\VsUIDpiAwareness.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static void RunAboutDlg(AboutDlg* pDlg)
{
	__try
	{
		pDlg->DoModal();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// [case: 111137]
		vLog("ERROR: caught AboutDlg SEH exception");
	}
}

void DisplayAboutDlg()
{
	VsUI::CDpiScope dpi; // [case: 142180]
	AboutDlg about(IDD_ABOUT, gMainWnd);
	RunAboutDlg(&about);
}

AboutDlg::AboutDlg(int IDD, CWnd* pParent /*=NULL*/) : VaHtmlDlg((UINT)IDD, pParent, NULL, IDC_STATIC_HTML)
{
	//{{AFX_DATA_INIT(AboutDlg)
	//}}AFX_DATA_INIT
}

BEGIN_MESSAGE_MAP(AboutDlg, VaHtmlDlg)
//{{AFX_MSG_MAP(AboutDlg)
ON_COMMAND(ID_EDIT_COPY, OnCopyInfo)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// AboutDlg message handlers

BOOL AboutDlg::PrepareContentFile()
{
	LogVersionInfo getInfo;
	mContentFile = ::GetTempFile(L"AboutVA.htm");

	// read template
	CStringW aboutPath(VaDirs::GetDllDir());
	aboutPath += L"about/about2.html";

	CStringW aboutFileContents(::ReadFile(aboutPath).Wide());
	if (aboutFileContents.IsEmpty())
	{
		_ASSERTE(!"failed to load about dlg template");
		return FALSE;
	}

	// make substitutions: $InstallDir$ $AboutInfo$ $SystemInfo$
	CStringW dllDir = VaDirs::GetDllDir();
	aboutFileContents.Replace(L"$InstallDir$", dllDir);

	dllDir.Replace(L':', L'|');
	CStringW tipDir = dllDir + L"TipOfTheDay";
	aboutFileContents.Replace(L"$TipDir$", tipDir);

	CStringW aboutInfo;
	aboutInfo += "Visual Assist build ";
	aboutInfo += tokenizeMacro(VA_VER_SIMPLIFIED_YEAR);
	aboutInfo += ".";
	aboutInfo += tokenizeMacro(VA_VER_SIMPLIFIED_RELEASE);
	aboutInfo += "<br>\n";
	aboutInfo += "Copyright ©1997-2023 Whole Tomato Software, LLC<br>\n";
	aboutInfo += "All rights reserved.<br>\n";
	aboutFileContents.Replace(L"$AboutInfo$", aboutInfo);

	CStringW sysInfo(::GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, "AboutInfo", L""));
	mSysInfo = sysInfo;
	sysInfo.Replace(L"\n", L"<br>\n");
	aboutFileContents.Replace(L"$SystemInfo$", sysInfo);

	CFileW aboutFile;
	if (!aboutFile.Open(mContentFile, CFile::modeCreate | CFile::modeWrite))
	{
		_ASSERTE(!"Save file open error");
		return FALSE;
	}

	aboutFile.Write(aboutFileContents, aboutFileContents.GetLength() * sizeof(WCHAR));
	aboutFile.Close();
	return TRUE;
}

void AboutDlg::OnCopyInfo()
{
	CStringW clipTxt(mSysInfo);
	clipTxt.Replace(L"\n", L"\r\n");
	::SaveToClipboard(m_hWnd, clipTxt);
}
