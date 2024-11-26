#include "stdafxed.h"
#include "resource.h"
#include "VaHtmlDlg.h"
#include "FILE.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

VaHtmlDlg::VaHtmlDlg(UINT dlgResId, CWnd* pParent, UINT nID_HTML, UINT n_ID_static)
    : CHtmlDialog(dlgResId, pParent, NULL, IDC_STATIC_HTML)
{
	//{{AFX_DATA_INIT(VaHtmlDlg)
	//}}AFX_DATA_INIT
}

VaHtmlDlg::~VaHtmlDlg()
{
	if (mContentFile.IsEmpty() || !::IsFile(mContentFile))
		return;

	::DeleteFileW(mContentFile);
}

void VaHtmlDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VaHtmlDlg)
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VaHtmlDlg, CHtmlDialog)
//{{AFX_MSG_MAP(VaHtmlDlg)
ON_WM_SHOWWINDOW()
ON_WM_ACTIVATE()
ON_WM_TIMER()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// VaHtmlDlg message handlers

BOOL VaHtmlDlg::OnInitDialog()
{
	CHtmlDialog::OnInitDialog();

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	{
		CWaitCursor wc;
		if (!PrepareContentFile() || !::IsFile(mContentFile))
			PostMessage(WM_COMMAND, IDOK);
	}

	m_HtmlCtrl.Navigate(CStringW(L"file:///") + mContentFile);

	// !!!
	// we cant do here any action with Html object ...

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VaHtmlDlg::_onHtmlCmd(UINT cmd, LPCTSTR params)
{
	if (cmd == 2000)
	{
		// http://
		// handle links via shellExec so that default browser is used instead of IE
		if (params && *params)
		{
			CString url("http://");
			url += params;
			::ShellExecute(NULL, _T("open"), url, NULL, NULL, SW_SHOW);
		}
	}
	else if (cmd == 2001)
	{
		// file:///
		// handle links via shellExec so that default browser is used instead of IE
		if (params && *params)
		{
			CStringW f(params);
			f.Replace(L"%20", L" ");
			::ShellExecuteW(NULL, L"open", f, NULL, NULL, SW_SHOW);
		}
	}
	else
		CHtmlDialog::_onHtmlCmd(cmd, params);
}

void VaHtmlDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CHtmlDialog::OnShowWindow(bShow, nStatus);
	if (bShow)
	{
		SetTimer(2000, 100, nullptr);
		SetTimer(2001, 2000, nullptr);
	}
}

void VaHtmlDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (2000 == nIDEvent || 2001 == nIDEvent)
	{
		KillTimer(nIDEvent);

		// force redraw of buttons since they are erased when the page loads
		CWnd* tmp;
		tmp = GetDlgItem(ID_EDIT_COPY);
		if (tmp)
			tmp->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
		tmp = GetDlgItem(IDOK);
		if (tmp)
			tmp->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	}
	else
		CHtmlDialog::OnTimer(nIDEvent);
}

void VaHtmlDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CHtmlDialog::OnActivate(nState, pWndOther, bMinimized);
	if (WA_INACTIVE != nState)
		SetTimer(2000, 100, nullptr);
}
