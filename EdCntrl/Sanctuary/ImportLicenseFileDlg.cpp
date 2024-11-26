#include "StdAfxEd.h"

#if defined(SANCTUARY)

#include "ImportLicenseFileDlg.h"
#include <ShlObj.h>
#include "WTString.h"
#include "ISanctuaryClient.h"
#include "IVaAuxiliaryDll.h"
#include "DevShellService.h"
#include "FILE.H"
#include "RegKeys.h"
#include "LogVersionInfo.h"
#include "RegisterSanctuary.h"
#include "Addin\Register.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CImportLicenseFileDlg::CImportLicenseFileDlg(CWnd* parent) : CDialog(IDD_IMPORT_LICENSE_FILE, parent)
{
	//{{AFX_DATA_INIT(CImportLicenseFileDlg)
	//}}AFX_DATA_INIT
}

CImportLicenseFileDlg::~CImportLicenseFileDlg()
{
}

void CImportLicenseFileDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CImportLicenseFileDlg)
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CImportLicenseFileDlg, CDialog)
//{{AFX_MSG_MAP(CImportLicenseFileDlg)
ON_WM_HELPINFO()
ON_WM_SYSCOMMAND()
ON_BN_CLICKED(IDC_BUTTON_BROWSE, OnBrowse)
ON_EN_CHANGE(IDC_EDIT_FILE, OnInputChanged)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CImportLicenseFileDlg message handlers

BOOL CImportLicenseFileDlg::OnInitDialog()
{
	const BOOL retval = CDialog::OnInitDialog();

	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);

	CWnd* tmpWnd = GetDlgItem(IDOK);
	tmpWnd->EnableWindow(FALSE);
	mEdit_subclassed.SubclassDlgItem(IDC_EDIT_FILE, this);
	return retval;
}

void CImportLicenseFileDlg::OnSysCommand(UINT msg, LPARAM lParam)
{
	if (msg == SC_CONTEXTHELP)
		LaunchHelp();
	else
		__super::OnSysCommand(msg, lParam);
}

BOOL CImportLicenseFileDlg::OnHelpInfo(HELPINFO* info)
{
	LaunchHelp();
	return TRUE;
}

void CImportLicenseFileDlg::LaunchHelp()
{
	// removed EXSTYLE WS_EX_CONTEXTHELP from dlg template so that we wouldn't need to write documentation for this url
	// ::ShellExecuteW(nullptr, L"open", L"http://www.wholetomato.com/support/tooltip.asp?option=importLicense",
	// nullptr, nullptr, SW_SHOW);
}

void CImportLicenseFileDlg::OnOK()
{
	_ASSERTE(gAuxDll);
	mLicenseFile.Empty();
	mEdit_subclassed.GetText(mLicenseFile);
	if (mLicenseFile.IsEmpty())
	{
		::WtMessageBox(m_hWnd, "No filepath was entered.", IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	if (!::IsFile(mLicenseFile))
	{
		::WtMessageBox(m_hWnd, "The filepath entered can not be found.", IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	CWaitCursor curs;
	LogVersionInfo::ForceNextUpdate();

	LPCSTR errorInfo = nullptr;
	auto ptr = gAuxDll->GetSanctuaryClient();
	_ASSERTE(ptr);
	bool res = ptr->ImportLicenseFile(mLicenseFile, &errorInfo);
	if (!res)
	{
		WTString msg("Import of the license file failed (error IL-4).\n\nThe error reported is:\n");
		if (errorInfo)
			msg += errorInfo;
		else
			msg += "(No error message)";
		msg += "\n\nIf the error recurs, send a screenshot of this message and your license file "
		       "to licenses@wholetomato.com.";

		::WtMessageBox(m_hWnd, msg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	// successful import just means the file is a valid slip file (not corrupt).
	// now need to reload and checkStatus to see if we can activate

	ptr->reload();
	const CString errorMsg1(ptr->getLastErrorMsg());
	res = ptr->checkStatus();
	const CString errorMsg2(ptr->getLastErrorMsg());
	curs.Restore();

	if (!res)
	{
		// this state can occur for following reasons (not exhaustive):
		// - no internet access (case 140892)
		// - authenticated proxy (not supported by sanctuary) (case 140892)
		// - the file is good but max registrations are used up (for example, ast test nodes)
		// - concurrent license maxed out
		// - bad static IP address (not sure if it was in file or somewhere else)

		// once case 140892 is addressed, we can have separate error messages for network issue vs registration issue

		CString msgMsg;

		if (!errorMsg1.IsEmpty() || !errorMsg2.IsEmpty())
		{
			msgMsg = "An error occurred during import of the license file. The error text is:\n";
			if (!errorMsg1.IsEmpty())
			{
				msgMsg += errorMsg1;
				if (!errorMsg2.IsEmpty())
					msgMsg += "\n";
			}

			if (!errorMsg2.IsEmpty())
			{
				msgMsg += errorMsg2;

				if (-1 != errorMsg2.Find("Maximum number of users already reached"))
				{
					msgMsg += "\n\nRetry after another user has released a seat of the concurrent license by exiting "
					          "Visual Studio.";

					::WtMessageBox(m_hWnd, msgMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
					return;
				}
			}

			msgMsg += "\n\nIf you are able to address the issue, do so, then retry import. "
			          "Otherwise, send \"error code IL-5\", this message, and your license file "
			          "to licenses@wholetomato.com.";
		}
		else
			msgMsg = "An unexpected error occurred during import of the license file. "
			         "Confirm that your PC is connected to the internet and retry. "
			         "If the error recurs, send \"error code IL-5\" and your license file "
			         "to licenses@wholetomato.com.";

		::WtMessageBox(m_hWnd, msgMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	// license was accepted, but now check that it is valid for this build
	if (!ptr->IsLicenseValidForThisBuild(false))
	{
		CString msg("Import of the license file was successful, however the license does not qualify to run this build "
		            "(error IL-6). ");
		if (ptr->IsRenewableLicense())
			msg += "You must renew software maintenance ";
		else
			msg += "You must purchase a new license ";

		msg += "to run this build, or revert to a previous build. Previous builds can be downloaded from "
		       "http://www.wholetomato.com/support/history.asp.";
		::WtMessageBox(m_hWnd, msg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}

	// success, valid license good for this build
	::ReportRegistrationSuccess(m_hWnd);

	__super::OnOK();
}

void CImportLicenseFileDlg::OnInputChanged()
{
	CStringW tmpTxt;
	mEdit_subclassed.GetText(tmpTxt);

	CWnd* tmpWnd = GetDlgItem(IDOK);
	tmpWnd->EnableWindow(!tmpTxt.IsEmpty());
}

STDAPI SHGetKnownFolderPath(__in REFKNOWNFOLDERID rfid, __in DWORD /* KNOWN_FOLDER_FLAG */ dwFlags,
                            __in_opt HANDLE hToken,
                            __deref_out PWSTR* ppszPath); // free *ppszPath with CoTaskMemFree

#ifndef __IFileDialog_INTERFACE_DEFINED__

typedef struct _COMDLG_FILTERSPEC
{
	LPCWSTR pszName;
	LPCWSTR pszSpec;
} COMDLG_FILTERSPEC;

typedef DWORD FILEOPENDIALOGOPTIONS;
typedef DWORD FDAP;

MIDL_INTERFACE("42f85136-db7e-439c-85f1-e4075d135fc8")
IFileDialog : public IModalWindow
{
  public:
	virtual HRESULT STDMETHODCALLTYPE SetFileTypes(
	    /* [in] */ UINT cFileTypes,
	    /* [size_is][in] */ __RPC__in_ecount_full(cFileTypes) const COMDLG_FILTERSPEC* rgFilterSpec) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileTypeIndex(
	    /* [in] */ UINT iFileType) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFileTypeIndex(
	    /* [out] */ __RPC__out UINT * piFileType) = 0;
	virtual HRESULT STDMETHODCALLTYPE Advise(
	    /* [in] */ __RPC__in_opt IFileDialogEvents * pfde,
	    /* [out] */ __RPC__out DWORD * pdwCookie) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unadvise(
	    /* [in] */ DWORD dwCookie) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetOptions(
	    /* [in] */ FILEOPENDIALOGOPTIONS fos) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOptions(
	    /* [out] */ __RPC__out FILEOPENDIALOGOPTIONS * pfos) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultFolder(
	    /* [in] */ __RPC__in_opt IShellItem * psi) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFolder(
	    /* [in] */ __RPC__in_opt IShellItem * psi) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFolder(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentSelection(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileName(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszName) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFileName(
	    /* [string][out] */ __RPC__deref_out_opt_string LPWSTR * pszName) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetTitle(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszTitle) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetOkButtonLabel(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszText) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileNameLabel(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszLabel) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetResult(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE AddPlace(
	    /* [in] */ __RPC__in_opt IShellItem * psi,
	    /* [in] */ FDAP fdap) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultExtension(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszDefaultExtension) = 0;
	virtual HRESULT STDMETHODCALLTYPE Close(
	    /* [in] */ HRESULT hr) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetClientGuid(
	    /* [in] */ __RPC__in REFGUID guid) = 0;
	virtual HRESULT STDMETHODCALLTYPE ClearClientData(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFilter(
	    /* [in] */ __RPC__in_opt IShellItemFilter * pFilter) = 0;
};

#endif

#ifndef __IFileOpenDialog_INTERFACE_DEFINED__

MIDL_INTERFACE("d57c7288-d4ad-4768-be02-9d969532d960")
IFileOpenDialog : public IFileDialog
{
  public:
	virtual HRESULT STDMETHODCALLTYPE GetResults(
	    /* [out] */ __RPC__deref_out_opt IShellItemArray * *ppenum) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSelectedItems(
	    /* [out] */ __RPC__deref_out_opt IShellItemArray * *ppsai) = 0;
};

#endif

void CImportLicenseFileDlg::OnBrowse()
{
	WTString dir;
	CFileDialog dlg(TRUE);

	dlg.m_ofn.lpstrTitle = "Select license file";
	dlg.m_ofn.lpstrFilter = "License files (*.slip; reg*.txt)\0"
	                        "*.slip;reg*.txt\0"
	                        "Text files (*.txt)\0"
	                        "*.txt\0"
	                        "All files (*.*)\0"
	                        "*.*\0\0";
	dlg.m_ofn.Flags |= OFN_FILEMUSTEXIST | OFN_DONTADDTORECENT;

	PWSTR downloadsPath = nullptr;
	::SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloadsPath);
	if (downloadsPath)
	{
		dir = downloadsPath;
		::CoTaskMemFree(downloadsPath);
		downloadsPath = nullptr;
	}

	if (dir.IsEmpty())
	{
		WCHAR path[MAX_PATH + 1] = L"";
		SHGetSpecialFolderPathW(GetSafeHwnd(), path, CSIDL_DESKTOP, FALSE);
		dir = path;
	}

	dlg.m_ofn.lpstrInitialDir = dir.c_str();

	// use IFileOpenDialog to get unicode path even though we are using ascii MFC
	CComPtr<IFileOpenDialog> pfdlg = dlg.GetIFileOpenDialog();
	if (pfdlg)
	{
		CComPtr<IShellItem> si;
		SHCreateItemFromParsingName(dir.Wide(), nullptr, IID_IShellItem, (void**)&si);
		if (si)
			pfdlg->SetFolder(si);
	}

	if (dlg.DoModal() == IDOK)
	{
		CStringW filename(dlg.GetPathName());
		if (pfdlg)
		{
			CComPtr<IShellItem> si;
			pfdlg->GetResult(&si);
			if (si)
			{
				PWSTR f = nullptr;
				si->GetDisplayName(SIGDN_FILESYSPATH, &f);
				if (f)
				{
					filename = f;
					::CoTaskMemFree(f);
				}
			}
		}

		mEdit_subclassed.SetText(filename);

		CWnd* tmp = GetDlgItem(IDC_EDIT_FILE);
		tmp->SetFocus();
	}
}

#endif
