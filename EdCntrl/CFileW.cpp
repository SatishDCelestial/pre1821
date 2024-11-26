#include "StdAfxEd.h"
#include "CFileW.h"
#include <strsafe.h>
#include "FILE.H"
#include "VAAutomation.h"
#include "..\common\TempAssign.h"
#include "assert_once.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/*
// not implemented - should it throw if Open fails??
CFileW::CFileW(const CStringW& filename, UINT nOpenFlags)
{
    m_hFile = INVALID_HANDLE_VALUE;
    if (!Open(filename, nOpenFlags))
        throw ;
}
*/

BOOL CFileW::Open(const CStringW& fileName, UINT nOpenFlags)
{
	ASSERT_VALID(this);

	ASSERT((nOpenFlags & typeText) == 0); // text mode not supported

	// shouldn't open an already open file (it will leak)
	ASSERT(m_hFile == INVALID_HANDLE_VALUE);

	// CFile objects are always binary and CreateFile does not need flag
	nOpenFlags &= ~(UINT)typeBinary;

	m_bCloseOnDelete = FALSE;

	m_hFile = INVALID_HANDLE_VALUE;
	mFilename.Empty();
	m_strFileName.Empty();

	//	TCHAR szTemp[_MAX_PATH];
	if (SUCCEEDED(StringCchLengthW(fileName, _MAX_PATH, NULL)))
	{
		// 		if( _AfxFullPath2(szTemp, fileName, pException) == FALSE )
		// 			return FALSE;
	}
	else
	{
		_ASSERTE(!"CFileW::Open path is too long");
		return FALSE; // path is too long
	}

	//	m_strFileName = szTemp;
	m_strFileName = fileName;
	ASSERT(shareCompat == 0);

	// map read/write mode
	ASSERT((modeRead | modeWrite | modeReadWrite) == 3);
	DWORD dwAccess = 0;
	switch (nOpenFlags & 3)
	{
	case modeRead:
		dwAccess = GENERIC_READ;
		break;
	case modeWrite:
		dwAccess = GENERIC_WRITE;
		break;
	case modeReadWrite:
		dwAccess = GENERIC_READ | GENERIC_WRITE;
		break;
	default:
		ASSERT(FALSE); // invalid share mode
	}

	// map share mode
	DWORD dwShareMode = 0;
	switch (nOpenFlags & 0x70) // map compatibility mode to exclusive
	{
	default:
		ASSERT(FALSE); // invalid share mode?
	case shareCompat:
	case shareExclusive:
		dwShareMode = 0;
		break;
	case shareDenyWrite:
		dwShareMode = FILE_SHARE_READ;
		break;
	case shareDenyRead:
		dwShareMode = FILE_SHARE_WRITE;
		break;
	case shareDenyNone:
		dwShareMode = FILE_SHARE_WRITE | FILE_SHARE_READ;
		break;
	}

	// Note: typeText and typeBinary are used in derived classes only.

	// map modeNoInherit flag
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = (nOpenFlags & modeNoInherit) == 0;

	// map creation flags
	DWORD dwCreateFlag;
	if (nOpenFlags & modeCreate)
	{
		if (nOpenFlags & modeNoTruncate)
			dwCreateFlag = OPEN_ALWAYS;
		else
			dwCreateFlag = CREATE_ALWAYS;
	}
	else
		dwCreateFlag = OPEN_EXISTING;

	// special system-level access flags

	// Random access and sequential scan should be mutually exclusive
	ASSERT((nOpenFlags & (osRandomAccess | osSequentialScan)) != (osRandomAccess | osSequentialScan));

	DWORD dwFlags = FILE_ATTRIBUTE_NORMAL;
	if (nOpenFlags & osNoBuffer)
		dwFlags |= FILE_FLAG_NO_BUFFERING;
	if (nOpenFlags & osWriteThrough)
		dwFlags |= FILE_FLAG_WRITE_THROUGH;
	if (nOpenFlags & osRandomAccess)
		dwFlags |= FILE_FLAG_RANDOM_ACCESS;
	if (nOpenFlags & osSequentialScan)
		dwFlags |= FILE_FLAG_SEQUENTIAL_SCAN;

	// attempt file creation
	for (int cnt = 0; cnt < 5; ++cnt)
	{
		HANDLE hFile = ::CreateFileW(fileName, dwAccess, dwShareMode, &sa, dwCreateFlag, dwFlags, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			bool retry = true;
			const DWORD errCode = GetLastError();
			CStringW errMsg;
			if (OPEN_EXISTING == dwCreateFlag)
			{
				if (::IsFile(fileName))
					CString__FormatW(errMsg, L"VAX ERROR: CFileW::Open failed to open file (%d)\n\tfilename: %ls\n",
					                 cnt, (LPCWSTR)fileName);
				else
				{
					CString__FormatW(errMsg, L"VAX ERROR: CFileW::Open no file (%d)\n\tfilename: %ls\n", cnt,
					                 (LPCWSTR)fileName);
					if (!cnt)
						cnt = 3;
				}
			}
			else if (OPEN_ALWAYS == dwCreateFlag)
			{
				CString__FormatW(errMsg, L"VAX ERROR: CFileW::Open failed to create/open file (%d)\n\tfilename: %ls\n",
				                 cnt, (LPCWSTR)fileName);
				if (ERROR_PATH_NOT_FOUND == errCode)
					retry = false;
			}
			else // CREATE_ALWAYS
				CString__FormatW(errMsg, L"VAX ERROR: CFileW::Open failed to create file (%d)\n\tfilename: %ls\n", cnt,
				                 (LPCWSTR)fileName);

			::OutputDebugStringW(errMsg);
			if (gTestsActive && gTestLogger)
				gTestLogger->LogStr(WTString(errMsg));

			if (retry)
			{
				// http://stackoverflow.com/questions/964920/is-it-possible-to-reasonably-workaround-an-antivirus-scanning-the-working-directo
				::Sleep(50);
			}
			else
				return FALSE;
		}
		else
		{
			mFilename = fileName;
			m_hFile = hFile;
			m_bCloseOnDelete = TRUE;

			if (cnt)
			{
				::OutputDebugStringW(L"VAX: CFileW::Open success after retry\n");
				if (gTestsActive && gTestLogger)
					gTestLogger->LogStr(WTString("VAX: CFileW::Open success after retry\n"));
			}

			return TRUE;
		}
	}

	return FALSE;
}

void CFileW::Flush()
{
	try
	{
		__super::Flush();
	}
	catch (CFileException* e)
	{
		CStringW errMsg;
		CString__FormatW(errMsg,
		                 L"VAX ERROR: CFileW::Flush CFileException cause(%d) osError(%ld)\n\tfilename: %ls (%p)\n",
		                 e->m_cause, e->m_lOsError, (LPCWSTR)mFilename, m_hFile);
		::OutputDebugStringW(errMsg);
		ASSERT_ONCE(!"CFileW::Flush CFileException (ASSERT_ONCE)");
		e->Delete();
		if (gTestsActive && gTestLogger)
		{
			static BOOL sHandlingException = false;
			if (!sHandlingException)
			{
				TempTrue t(sHandlingException);
				gTestLogger->LogStr(WTString(errMsg));
			}
		}
		// do not log the exception - this can cause stack overflow (case=5025)
		// VALOGEXCEPTION writes to the error log using WTofstream which is derived from CFileW!
	}
}

void CFileW::WriteUtf8Bom()
{
	const int utf8BomLen = 3;
	const char utf8Bom[utf8BomLen] = {'\xef', '\xbb', '\xbf'};
	Write(utf8Bom, utf8BomLen);
}

void CFileW::Write(const void* lpBuf, UINT nCount)
{
	try
	{
		__super::Write(lpBuf, nCount);
	}
	catch (CFileException* e)
	{
		CStringW errMsg;
		CString__FormatW(errMsg,
		                 L"VAX ERROR: CFileW::Write CFileException cause(%d) osError(%ld)\n\tfilename: %ls (%p)\n",
		                 e->m_cause, e->m_lOsError, (LPCWSTR)mFilename, m_hFile);
		::OutputDebugStringW(errMsg);
		ASSERT_ONCE(!"CFileW::Write CFileException (ASSERT_ONCE)");
		e->Delete();
		if (gTestsActive && gTestLogger)
		{
			static BOOL sHandlingException = false;
			if (!sHandlingException)
			{
				TempTrue t(sHandlingException);
				gTestLogger->LogStr(WTString(errMsg));
			}
		}
		// do not log the exception - this can cause stack overflow (case=5025)
		// VALOGEXCEPTION writes to the error log using WTofstream which is derived from CFileW!
	}
	catch (...)
	{
		CStringW errMsg;
		CString__FormatW(errMsg, L"VAX ERROR: CFileW::Write exception\n\tfilename: %ls\n", (LPCWSTR)mFilename);
		::OutputDebugStringW(errMsg);
		ASSERT_ONCE(!"CFileW::Write exception... (ASSERT_ONCE)");
		if (gTestsActive && gTestLogger)
		{
			static BOOL sHandlingException = false;
			if (!sHandlingException)
			{
				TempTrue t(sHandlingException);
				gTestLogger->LogStr(WTString(errMsg));
			}
		}
		// do not log the exception - this can cause stack overflow (case=5025)
		// VALOGEXCEPTION writes to the error log using WTofstream which is derived from CFileW!
	}
}

void CFileW::Close()
{
	try
	{
		__super::Close();
	}
	catch (CFileException* e)
	{
		CStringW errMsg;
		CString__FormatW(errMsg,
		                 L"VAX ERROR: CFileW::Close CFileException cause(%d) osError(%ld)\n\tfilename: %ls (%p)\n",
		                 e->m_cause, e->m_lOsError, (LPCWSTR)mFilename, m_hFile);
		::OutputDebugStringW(errMsg);
		ASSERT_ONCE(!"CFileW::Close CFileException (ASSERT_ONCE)");
		e->Delete();
		if (gTestsActive && gTestLogger)
		{
			static BOOL sHandlingException = false;
			if (!sHandlingException)
			{
				TempTrue t(sHandlingException);
				gTestLogger->LogStr(WTString(errMsg));
			}
		}
		// do not log the exception - this can cause stack overflow (case=5025)
		// VALOGEXCEPTION writes to the error log using WTofstream which is derived from CFileW!
	}
	catch (...)
	{
		CStringW errMsg;
		CString__FormatW(errMsg, L"VAX ERROR: CFileW::Write exception\n\tfilename: %ls\n", (LPCWSTR)mFilename);
		::OutputDebugStringW(errMsg);
		ASSERT_ONCE(!"CFileW::Close exception... (ASSERT_ONCE)");
		if (gTestsActive && gTestLogger)
		{
			static BOOL sHandlingException = false;
			if (!sHandlingException)
			{
				TempTrue t(sHandlingException);
				gTestLogger->LogStr(WTString(errMsg));
			}
		}
		// do not log the exception - this can cause stack overflow (case=5025)
		// VALOGEXCEPTION writes to the error log using WTofstream which is derived from CFileW!
	}
}
