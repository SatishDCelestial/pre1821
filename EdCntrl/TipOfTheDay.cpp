// TipOfTheDay.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "TipOfTheDay.h"
#include "Registry.h"
#include "WTString.h"
#include "Directories.h"
#include "RegKeys.h"
#include "PROJECT.H"
#include "..\common\ThreadName.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CTipOfTheDay dialog

static BOOL s_ShowAtStart = TRUE;
static BOOL s_ShowAtStart_info = TRUE;
static CTipOfTheDay* sTotd = nullptr;
BOOL CTipOfTheDay::sHasFocus = FALSE;
const bool DisableInfoOfTheDay = true;

static void PreCacheDlgThemeColors()
{
	// pre-cache theme settings on ui thread (queries of uncached values fail on background thread)
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	if (g_IdeSettings)
	{
		g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
		g_IdeSettings->GetNewProjectDlgColor(L"BackgroundLowerRegion", FALSE);
		g_IdeSettings->GetNewProjectDlgColor(L"InputFocusBorder", FALSE);
		g_IdeSettings->GetNewProjectDlgColor(L"CheckBox", FALSE);
		g_IdeSettings->GetNewProjectDlgColor(L"CheckBox", TRUE);
		g_IdeSettings->GetNewProjectDlgColor(L"CheckBoxMouseOver", FALSE);
		g_IdeSettings->GetNewProjectDlgColor(L"CheckBoxMouseOver", TRUE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"Button", FALSE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"Button", TRUE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonBorder", FALSE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOver", FALSE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOver", TRUE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonMouseOverBorder", FALSE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressed", FALSE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressed", TRUE);
		g_IdeSettings->GetThemeColor(ThemeCategory11::TeamExplorer, L"ButtonPressedBorder", FALSE);
	}
}

// http://www.developer.com/net/cplus/article.php/632041/Convert-modal-dialogs-to-modeless.htm
class CTotdDialogThread : public CWinThread
{
	DECLARE_DYNCREATE(CTotdDialogThread)
	CTotdDialogThread()
	{
		::PreCacheDlgThemeColors();
	}

	~CTotdDialogThread() = default;

	virtual BOOL InitInstance()
	{
		DEBUG_THREAD_NAME_IF_NOT("VA: TOTD", g_mainThread);
		// http://stackoverflow.com/questions/3699633/webbrowser-control-mfc-created-in-seperate-thread-working-in-windows-7-and-vis
		::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

		VsUI::CDpiScope dpi; // [case: 142180]
		CTipOfTheDay dlg;
		sActiveDlg = &dlg;
		dlg.DoModal();
		sActiveDlg = nullptr;

		::CoUninitialize();
		return FALSE;
	}

  public:
	static CTipOfTheDay* sActiveDlg;
};
class CIotdDialogThread : public CWinThread
{
	DECLARE_DYNCREATE(CIotdDialogThread)
	CIotdDialogThread()
	{
		::PreCacheDlgThemeColors();
	}

	~CIotdDialogThread() = default;

	virtual BOOL InitInstance()
	{
		DEBUG_THREAD_NAME_IF_NOT("VA: IOTD", g_mainThread);
		// http://stackoverflow.com/questions/3699633/webbrowser-control-mfc-created-in-seperate-thread-working-in-windows-7-and-vis
		::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

		VsUI::CDpiScope dpi; // [case: 142180]
		CInfoOfTheDay dlg;
		sActiveDlg = &dlg;
		dlg.DoModal();
		sActiveDlg = nullptr;

		::CoUninitialize();
		return FALSE;
	}

  public:
	static CInfoOfTheDay* sActiveDlg;
};

IMPLEMENT_DYNCREATE(CTotdDialogThread, CWinThread);
IMPLEMENT_DYNCREATE(CIotdDialogThread, CWinThread);


CTipOfTheDay* CTotdDialogThread::sActiveDlg = nullptr;
CInfoOfTheDay* CIotdDialogThread::sActiveDlg = nullptr;


void CTipOfTheDay::LaunchTipOfTheDay(BOOL manual /*= FALSE*/)
{
	CString regStr = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "ShowTipOfTheDay");
	s_ShowAtStart = regStr != "No";
	if ((s_ShowAtStart && (regStr == "Yes" || GetCurrDateStr() > regStr)) || manual)
	{
		enum class TotdExec
		{
			execBackgroundThread,
			execModal,
			execModeless
		};

		TotdExec exec = manual ? TotdExec::execModal
		                       : (TotdExec)::GetRegByte(HKEY_CURRENT_USER, ID_RK_APP, "ModalTipOfTheDay",
		                                                (byte)TotdExec::execBackgroundThread);

		switch (exec)
		{
		case TotdExec::execModal:
			if (CTotdDialogThread::sActiveDlg)
			{
				// [case: 100783]
				CTotdDialogThread::sActiveDlg->SetForegroundWindow();
			}
			else
			{
				VsUI::CDpiScope dpi; // [case: 142180]
				CTipOfTheDay tip(gMainWnd);
				tip.DoModal();
			}
			break;
		case TotdExec::execModeless:
			// [case: 2324]
			// optional modeless behavior; however, dialog handling does not work
			// while modeless.  These standard dialog behaviors are not supported:
			// tab, shift+tab, alt+XX, esc, return
			// Requires mouse click to close window (via close button in window or in caption).
			// ESC will close the dialog if an editor is open.
			// Due to inability to call IsDialogMessage.  see also:
			// http://www.codeproject.com/Articles/1097/Tabs-and-Accelerators-in-ATL-Modeless-Dialogs
			sTotd = new CTipOfTheDay(gMainWnd);
			sTotd->DoModeless();
			break;

		case TotdExec::execBackgroundThread:
		default:
			// [case: 2324]
			::AfxBeginThread(RUNTIME_CLASS(CTotdDialogThread));
			break;
		}
	}
}
void CInfoOfTheDay::LaunchInfoOfTheDay()
{
	if (DisableInfoOfTheDay)
		return;

	CString regStr = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "ShowInfoOfTheDay");
	s_ShowAtStart_info = regStr != "No";
	if (s_ShowAtStart_info && (regStr == "Yes" || GetCurrDateStr() > regStr))
		::AfxBeginThread(RUNTIME_CLASS(CIotdDialogThread));
}

BOOL CTipOfTheDay::CloseTipOfTheDay()
{
	if (CIotdDialogThread::sActiveDlg)
		CIotdDialogThread::sActiveDlg->SendMessage(WM_CLOSE);

	if (CTotdDialogThread::sActiveDlg)
	{
		// [case: 100997]
		CTotdDialogThread::sActiveDlg->SendMessage(WM_CLOSE);
		return FALSE;
	}

	if (!sTotd)
		return FALSE;

	sTotd->SendMessage(WM_CLOSE);
	return TRUE;
}

CTipOfTheDay::CTipOfTheDay(CWnd* pParent /*=NULL*/)
    : CThemedHtmlDlg(CTipOfTheDay::IDD, pParent, NULL, IDC_STATIC_HTML), m_tipNo(1), mModal(true), mParent(pParent)
{
	//{{AFX_DATA_INIT(CTipOfTheDay)
	//}}AFX_DATA_INIT
}

void CTipOfTheDay::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CHECK1, GetShowAtStartVar());
	//{{AFX_DATA_MAP(CTipOfTheDay)
	// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CTipOfTheDay, CThemedHtmlDlg)
//{{AFX_MSG_MAP(CTipOfTheDay)
ON_BN_CLICKED(IDC_CHECK1, OnCheckboxTick)
ON_BN_CLICKED(IDC_PREVTIP, &CTipOfTheDay::OnBnClickedPrevtip)
ON_BN_CLICKED(IDC_NEXTTIP, &CTipOfTheDay::OnBnClickedNexttip)
ON_WM_WINDOWPOSCHANGED()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CTipOfTheDay message handlers

BOOL CTipOfTheDay::OnInitDialog()
{
	CHtmlDialog::OnInitDialog();

	InitializeDayCounter();

	if (!ShowTip())
		PostMessage(WM_COMMAND, IDOK);

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHECK1, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_PREVTIP, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_NEXTTIP, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	// !!!
	// we cant do here any action with Html object ...

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CInfoOfTheDay::OnInitDialog()
{
	BOOL ret = __super::OnInitDialog();

	SetWindowText("Visual Assist Information"); // set in HtmlDialog.cpp from .html file

	GetDlgItem(IDC_PREVTIP)->ShowWindow(SW_HIDE);
	GetDlgItem(IDC_NEXTTIP)->ShowWindow(SW_HIDE);

	auto checkbox = GetDlgItem(IDC_CHECK1);
	if(checkbox)
	{
		CString checkbox_text;
		checkbox->GetWindowText(checkbox_text);
		checkbox_text.Replace("tips", "this information");
		checkbox->SetWindowText(checkbox_text);

		CRect rect;
		checkbox->GetClientRect(rect);
		checkbox->SetWindowPos(nullptr, 0, 0, rect.Width() * 2, rect.Height(), SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	}

	return ret;
}

BOOL& CTipOfTheDay::GetShowAtStartVar() const
{
	return s_ShowAtStart;
}
BOOL& CInfoOfTheDay::GetShowAtStartVar() const
{
	return s_ShowAtStart_info;
}

void CTipOfTheDay::InitializeDayCounter()
{
	m_tipNo = atoi(GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "TipOfTheDayCount"));
	m_tipNo++;
}

CString CTipOfTheDay::GetCurrDateStr()
{
	// current date
	SYSTEMTIME ctm;
	GetLocalTime(&ctm);
	CString curDateStr;
	CString__FormatA(curDateStr, _T("%04d.%02d.%02d"), ctm.wYear, ctm.wMonth, ctm.wDay);
	return curDateStr;
}

void CTipOfTheDay::OnCheckboxTick()
{
	UpdateData(TRUE);
}

bool CTipOfTheDay::ShowTip()
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW searchSpec;

	const CStringW tipDir = VaDirs::GetDllDir() + L"TipOfTheDay/";

	CString__FormatW(searchSpec, L"%stip*.htm", (const wchar_t*)tipDir);
	hFile = FindFirstFileW(searchSpec, &fileData);
	int count = 0;
	CStringW firstFile, htmFile, lastFile;
	if (hFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			count++;
			htmFile = tipDir + fileData.cFileName;
			if (!firstFile.GetLength())
				firstFile = htmFile;
		} while (count < m_tipNo && FindNextFileW(hFile, &fileData));

		if (count < m_tipNo && m_tipNo != 900)
		{
			htmFile = firstFile;
			count = 1;
		}

		FindClose(hFile);
		m_tipNo = count;
		CString asciival;
		CString__FormatA(asciival, "%d", m_tipNo);
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "TipOfTheDayCount", asciival);
		if (htmFile.IsEmpty())
			return false;

		return m_HtmlCtrl.Navigate(CStringW(L"file:///") + htmFile);
	}
	else
		return false;
}

bool CInfoOfTheDay::ShowTip()
{
	const CStringW htmFile = VaDirs::GetDllDir() + L"TipOfTheDay/info000.html";
	return m_HtmlCtrl.Navigate(CStringW(L"file:///") + htmFile);
}

void CTipOfTheDay::OnBnClickedPrevtip()
{
	if (--m_tipNo < 1)
		m_tipNo = 900; // last tip
	ShowTip();
}

void CTipOfTheDay::OnBnClickedNexttip()
{
	m_tipNo++;
	ShowTip();
}

void CTipOfTheDay::PostNcDestroy()
{
	CHtmlDialog::PostNcDestroy();

	if (!mModal)
	{
		sTotd = nullptr;
		delete this;
	}
}

void CTipOfTheDay::OnOK()
{
	sHasFocus = FALSE;
	RefreshRegStr();
	if (mModal)
		__super::OnOK();
	else
		OnCancel();
}

void CTipOfTheDay::OnCancel()
{
	sHasFocus = FALSE;
	RefreshRegStr();
	if (mModal)
		__super::OnCancel();
	else
		DestroyWindow();
}

void StripAmpersand(CWnd* h)
{
	if (!h)
		return;

	CString txt;
	h->GetWindowText(txt);
	if (txt.IsEmpty())
		return;

	txt.Replace("&", "");
	h->SetWindowText(txt);
}

void CTipOfTheDay::DoModeless()
{
	mModal = false;
	Create(CTipOfTheDay::IDD, mParent);

	// remove ampersand since alt key does not work due to IsDialogMessage
	// not being called whole modeless.
	CWnd* tmp;
	tmp = GetDlgItem(IDC_NEXTTIP);
	::StripAmpersand(tmp);
	tmp = GetDlgItem(IDC_PREVTIP);
	::StripAmpersand(tmp);
	tmp = GetDlgItem(IDC_CLOSE);
	::StripAmpersand(tmp);
	tmp = GetDlgItem(IDC_CHECK1);
	::StripAmpersand(tmp);
	tmp = GetDlgItem(IDCANCEL);
	::StripAmpersand(tmp);

	ShowWindow(SW_SHOWNORMAL);
	CenterWindow();
}

void CTipOfTheDay::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	__super::OnWindowPosChanged(lpwndpos);
	if (lpwndpos->flags & SWP_NOACTIVATE)
		sHasFocus = FALSE;
	else
		sHasFocus = TRUE;
}

void CTipOfTheDay::RefreshRegStr()
{
	if (GetShowAtStartVar())
	{
		CString curVal = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, GetRegValueName());
		if (curVal == "No")
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, GetRegValueName(), "Yes");
		else
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, GetRegValueName(), GetCurrDateStr());
	}
	else
	{
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, GetRegValueName(), "No");
	}
}
