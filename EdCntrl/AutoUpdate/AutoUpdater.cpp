// AutoUpdater.cpp: implementation of the CAutoUpdater class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafxed.h"
#include "AutoUpdater.h"
#include "../DevShellService.h"
#include "../FILE.H"

#pragma comment(lib, "wininet")

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#define TRANSFER_SIZE 4096

CAutoUpdater::CAutoUpdater()
{
	// Initialize WinInet
	hInternet = InternetOpen("AutoUpdateAgent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
}

CAutoUpdater::~CAutoUpdater()
{
	if (hInternet)
	{
		InternetCloseHandle(hInternet);
	}
}

// Check if an update is required
//
CAutoUpdater::ErrorType CAutoUpdater::CheckForUpdate(LPCTSTR UpdateServerURL)
{
	if (!InternetOkay())
	{
		return InternetConnectFailure;
	}

	bool bTransferSuccess = false;

	// First we must check the remote configuration file to see if an update is necessary
	CStringW URL{UpdateServerURL + CString(LOCATION_UPDATE_FILE_CHECK)};
	HINTERNET hSession = GetSession(URL);
	if (!hSession)
	{
		return InternetSessionFailure;
	}

	std::vector<BYTE> bufVec(TRANSFER_SIZE);
	BYTE* pBuf = &bufVec[0];
	bTransferSuccess = DownloadConfig(hSession, pBuf, TRANSFER_SIZE);
	InternetCloseHandle(hSession);
	if (!bTransferSuccess)
	{
		return ConfigDownloadFailure;
	}

	// Get the version number of our executable and compare to see if an update is needed
	CStringW executable = GetExecutable();
	CString fileVersion = GetFileVersion(executable);
	if (fileVersion.IsEmpty())
	{
		return NoExecutableVersion;
	}

	CString updateVersion = (char*)pBuf;
	if (CompareVersions(updateVersion, fileVersion) != 1)
	{
		return UpdateNotRequired;
	}

	// At this stage an update is required
	CStringW exeName = executable.Mid(1 + executable.ReverseFind(L'\\'));
	CStringW directory(::GetTempDir());

	// Download the updated file
	URL = CStringW(UpdateServerURL) + exeName;
	hSession = GetSession(URL);
	if (!hSession)
	{
		return InternetSessionFailure;
	}

	CString msg;
	CString__FormatA(msg, _T("An update of %s is now available. Proceed with the update?"), (LPCTSTR)CString(exeName));
	if (IDNO == WtMessageBox(GetActiveWindow(), msg, _T("Update is available"), MB_YESNO | MB_ICONQUESTION))
	{
		return UpdateNotComplete;
	}

	// Proceed with the update
	CStringW updateFileLocation = directory + exeName;
	bTransferSuccess = DownloadFile(hSession, updateFileLocation);
	InternetCloseHandle(hSession);
	if (!bTransferSuccess)
	{
		return FileDownloadFailure;
	}

	if (!Switch(executable, updateFileLocation, false))
	{
		return UpdateNotComplete;
	}

	return Success;
}

// Ensure the internet is ok to use
//
bool CAutoUpdater::InternetOkay()
{
	if (hInternet == NULL)
	{
		return false;
	}

	// Important step - ensure we have an internet connection. We don't want to force a dial-up.
	DWORD dwType;
	if (!InternetGetConnectedState(&dwType, 0))
	{
		return false;
	}

	return true;
}

// Get a session pointer to the remote file
//
HINTERNET CAutoUpdater::GetSession(CStringW& URL)
{
	// Canonicalization of the URL converts unsafe characters into escape character equivalents
	WCHAR canonicalURL[1024];
	DWORD nSize = 1024;
	InternetCanonicalizeUrlW(URL, canonicalURL, &nSize, ICU_BROWSER_MODE);

	DWORD options =
	    INTERNET_FLAG_NEED_FILE | INTERNET_FLAG_HYPERLINK | INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_RELOAD;
	HINTERNET hSession = InternetOpenUrlW(hInternet, canonicalURL, NULL, NULL, options, 0);
	URL = canonicalURL;

	return hSession;
}

// Download a file into a memory buffer
//
bool CAutoUpdater::DownloadConfig(HINTERNET hSession, BYTE* pBuf, DWORD bufSize)
{
	DWORD dwReadSizeOut;
	InternetReadFile(hSession, pBuf, bufSize, &dwReadSizeOut);
	if (dwReadSizeOut <= 0)
	{
		return false;
	}

	return true;
}

// Download a file to a specified location
//
bool CAutoUpdater::DownloadFile(HINTERNET hSession, LPCWSTR localFile)
{
	HANDLE hFile;
	std::vector<BYTE> bufVec(TRANSFER_SIZE);
	BYTE* pBuf = &bufVec[0];
	DWORD dwReadSizeOut, dwTotalReadSize = 0;

	hFile = CreateFileW(localFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
	                    FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	for (;;)
	{
		DWORD dwWriteSize, dwNumWritten;
		BOOL bRead = InternetReadFile(hSession, pBuf, TRANSFER_SIZE, &dwReadSizeOut);
		dwWriteSize = dwReadSizeOut;

		if (bRead && dwReadSizeOut > 0)
		{
			dwTotalReadSize += dwReadSizeOut;
			WriteFile(hFile, pBuf, dwWriteSize, &dwNumWritten, NULL);
			// File write error
			if (dwWriteSize != dwNumWritten)
			{
				CloseHandle(hFile);
				return false;
			}
		}
		else
		{
			if (!bRead)
			{
				// Error
				CloseHandle(hFile);
				return false;
			}
			break;
		}
	}

	CloseHandle(hFile);
	return true;
}

// Get the version of a file
//
CString CAutoUpdater::GetFileVersion(LPCWSTR file)
{
	CString version;
	VS_FIXEDFILEINFO* pVerInfo = NULL;
	DWORD dwTemp, dwSize, dwHandle = 0;
	BYTE* pData = NULL;
	UINT uLen;

	try
	{
		dwSize = GetFileVersionInfoSizeW((LPWSTR)file, &dwTemp);
		if (dwSize == 0)
			throw 1;

		pData = new BYTE[dwSize];
		if (pData == NULL)
			throw 1;

		if (!GetFileVersionInfoW((LPWSTR)file, dwHandle, dwSize, pData))
			throw 1;

		if (!VerQueryValue(pData, _T("\\"), (void**)&pVerInfo, &uLen))
			throw 1;

		DWORD verMS = pVerInfo->dwFileVersionMS;
		DWORD verLS = pVerInfo->dwFileVersionLS;

		int ver[4];
		ver[0] = HIWORD(verMS);
		ver[1] = LOWORD(verMS);
		ver[2] = HIWORD(verLS);
		ver[3] = LOWORD(verLS);

		// Are lo-words used?
		if (ver[2] != 0 || ver[3] != 0)
		{
			CString__FormatA(version, _T("%d.%d.%d.%d"), ver[0], ver[1], ver[2], ver[3]);
		}
		else if (ver[0] != 0 || ver[1] != 0)
		{
			CString__FormatA(version, _T("%d.%d"), ver[0], ver[1]);
		}

		delete[] pData;
		return version;
	}
	catch (...)
	{
		return _T("");
	}
}

// Compare two versions
//
int CAutoUpdater::CompareVersions(CString ver1, CString ver2)
{
	int wVer1[4], wVer2[4];
	int i;
	TCHAR* pVer1 = ver1.GetBuffer(256);
	TCHAR* pVer2 = ver2.GetBuffer(256);

	for (i = 0; i < 4; i++)
	{
		wVer1[i] = 0;
		wVer2[i] = 0;
	}

	// Get version 1 to DWORDs
	TCHAR* pToken = strtok(pVer1, _T("."));
	if (pToken == NULL)
	{
		return -21;
	}

	i = 3;
	while (pToken != NULL)
	{
		if (i < 0 || !IsDigits(pToken))
		{
			return -21; // Error in structure, too many parameters
		}
		wVer1[i] = atoi(pToken);
		pToken = strtok(NULL, _T("."));
		i--;
	}
	ver1.ReleaseBuffer();

	// Get version 2 to DWORDs
	pToken = strtok(pVer2, _T("."));
	if (pToken == NULL)
	{
		return -22;
	}

	i = 3;
	while (pToken != NULL)
	{
		if (i < 0 || !IsDigits(pToken))
		{
			return -22; // Error in structure, too many parameters
		}
		wVer2[i] = atoi(pToken);
		pToken = strtok(NULL, _T("."));
		i--;
	}
	ver2.ReleaseBuffer();

	// Compare the versions
	for (i = 3; i >= 0; i--)
	{
		if (wVer1[i] > wVer2[i])
		{
			return 1; // ver1 > ver 2
		}
		else if (wVer1[i] < wVer2[i])
		{
			return -1;
		}
	}

	return 0; // ver 1 == ver 2
}

// Ensure a string contains only digit characters
//
bool CAutoUpdater::IsDigits(CString text)
{
	for (int i = 0; i < text.GetLength(); i++)
	{
		TCHAR c = text.GetAt(i);
		if (c >= _T('0') && c <= _T('9'))
		{
		}
		else
		{
			return false;
		}
	}

	return true;
}

CStringW CAutoUpdater::GetExecutable()
{
	HMODULE hModule = ::GetModuleHandleW(NULL);
	ASSERT(hModule != 0);

	WCHAR path[MAX_PATH];
	VERIFY(::GetModuleFileNameW(hModule, path, MAX_PATH));
	return path;
}

bool CAutoUpdater::Switch(CStringW executable, CStringW update, bool WaitForReboot)
{
	int type = (WaitForReboot) ? MOVEFILE_DELAY_UNTIL_REBOOT : MOVEFILE_COPY_ALLOWED;

	const WCHAR* backup = L"OldExecutable.bak";
	CStringW directory = executable.Left(executable.ReverseFind(L'\\'));
	CStringW backupFile = directory + L'\\' + CStringW(backup);

	DeleteFileW(backupFile);
	if (!MoveFileExW(executable, backupFile, (DWORD)type))
	{
		return false;
	}

	const bool bMoveOK = (MoveFileExW(update, executable, (DWORD)type) == TRUE);
	return bMoveOK;
}
