// AutoUpdater.h: interface for the CAutoUpdater class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_AUTOUPDATER_H__227B2B21_B6AE_4164_B3A5_BFDAAF13D85D__INCLUDED_)
#define AFX_AUTOUPDATER_H__227B2B21_B6AE_4164_B3A5_BFDAAF13D85D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <Wininet.h>

#define LOCATION_UPDATE_FILE_CHECK _T("update.txt")

class CAutoUpdater
{
  public:
	CAutoUpdater();
	virtual ~CAutoUpdater();

	enum ErrorType
	{
		Success,
		InternetConnectFailure,
		InternetSessionFailure,
		ConfigDownloadFailure,
		FileDownloadFailure,
		NoExecutableVersion,
		UpdateNotRequired,
		UpdateNotComplete
	};

	ErrorType CheckForUpdate(LPCTSTR UpdateServerURL);
	HINTERNET GetSession(CStringW& URL);

	bool InternetOkay();
	bool DownloadConfig(HINTERNET hSession, BYTE* pBuf, DWORD bufSize);
	bool DownloadFile(HINTERNET hSession, LPCWSTR localFile);

	CString GetFileVersion(LPCWSTR file);
	int CompareVersions(CString ver1, CString ver2);
	bool IsDigits(CString text);
	CStringW GetExecutable();
	bool Switch(CStringW executable, CStringW update, bool WaitForReboot);

  private:
	HINTERNET hInternet;
};

#endif // !defined(AFX_AUTOUPDATER_H__227B2B21_B6AE_4164_B3A5_BFDAAF13D85D__INCLUDED_)
