#include "stdafxed.h"
#include "resource.h"
#include "TrialElqDlg.h"
#include "DpiCookbook\VsUIDpiAwareness.h"
#include "Directories.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static INT_PTR RunTrialElqDlg(TrialElqDlg* pDlg)
{
	INT_PTR ret = IDCANCEL;
	
	__try
	{
		ret = pDlg->DoModal();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		vLog("ERROR: caught TrialElqDlg SEH exception");
	}

	return ret;
}

bool DisplayTrialElqDlg(CWnd* pParent)
{
	VsUI::CDpiScope dpi;
	TrialElqDlg elq(pParent);
	RunTrialElqDlg(&elq);
	return elq.IsTrialActivated();
}

bool TrialElqDlg::ShowElq()
{
	CStringW elqPath(VaDirs::GetDllDir());
	elqPath += L"ELQ\\ta.html";
	// next line is a hack to allow calling JavaScript form WinForms;
	// instead of calling html file directly from C: we call it from 127.0.0.1/C$/...
	elqPath.Replace(':', '$');
	elqPath = L"//127.0.0.1/" + elqPath;
	
	bool isHtmlFound = ::PathFileExistsW(elqPath); // first check if html file exists on expected location
	if (isHtmlFound)
	{
		// if html file exists there, try to navigate to it and show it with browser component; even if file is
		// there physically, IE still might not have permission to read it so we need this second check
		isHtmlFound = m_HtmlCtrl.Navigate(CStringW(L"file:") + elqPath);
	}
	
	if(!isHtmlFound)
	{
		// [case:164017] for some reason Eloqua HTML files were not found; continue with
		// a workaround to allow user to activate the product regardless; no user data
		// will be sent but at least users will not be stuck on non existing Eloqua dialog
		// Note: this is kind of less evil scenario hack; ideally we should find out why those
		// HTML and JS files can't be reached from the disk (currently not reproducible except
		// by deleting them manually by intention)
		mTrialActivated = true;	// no user data sent, but we don't want this dialog to be shown again
		this->EndDialog(IDABORT);
	}

	return isHtmlFound;
}

bool TrialElqDlg::IsTrialActivated()
{
	return mTrialActivated;
}

TrialElqDlg::TrialElqDlg(CWnd* pParent /*=NULL*/) : CHtmlDialog(IDD, pParent, NULL, IDC_STATIC_HTML)
{
	//{{AFX_DATA_INIT(TrialElqDlg)
	//}}AFX_DATA_INIT
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(TrialElqDlg, CHtmlDialog)
//{{AFX_MSG_MAP(TrialElqDlg)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// TrialElqDlg message handlers

BOOL TrialElqDlg::OnInitDialog()
{
	CHtmlDialog::OnInitDialog();
	return ShowElq();
}

void TrialElqDlg::_onHtmlCmd(UINT cmd, LPCTSTR params)
{
	if (cmd == 2000)
	{
		// http://
		// handle links via shellExec so that default browser is used instead of IE
		if (params && *params)
		{
			CString url(params);
			::ShellExecute(nullptr, _T("open"), url, nullptr, nullptr, SW_SHOW);
		}
	}
	else if (cmd == 2001)
	{
		// file://
		// handle links via shellExec so that default browser is used instead of IE
		if (params && *params)
		{
			CStringW f(params);
			f.Replace(L"%20", L" ");
			::ShellExecuteW(nullptr, L"open", f, nullptr, nullptr, SW_SHOW);
		}
	}
	else if (cmd == 3000)
	{
		// user has entered data, closing window now is start of trial
		mTrialActivated = true;
	}
	else if (cmd == 3001)
	{
		// Close button pressed, close the dialog
		EndDialog(0);
	}
	else
		CHtmlDialog::_onHtmlCmd(cmd, params);
}
