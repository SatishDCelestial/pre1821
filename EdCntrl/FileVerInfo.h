#pragma once

#define INVALID_VER_INFO 0xffffffff

class FileVersionInfo
{
	DWORD m_dwFileVersionMS;
	DWORD m_dwFileVersionLS;
	DWORD m_dwProductVersionMS;
	DWORD m_dwProductVersionLS;
	CStringW m_szModuleName;
	CStringW m_szModuleFullPath;
	CStringW m_szModuleComment;
	CStringW m_szModuleDescription;

	void Clear()
	{
		m_dwFileVersionLS = m_dwFileVersionMS = m_dwProductVersionLS = m_dwProductVersionMS = INVALID_VER_INFO;
		m_szModuleName.Empty();
		m_szModuleFullPath.Empty();
		m_szModuleComment.Empty();
		m_szModuleDescription.Empty();
	}
	BOOL Query(HMODULE hMod)
	{
		BOOL retval = FALSE;
		if (!hMod)
			return retval;

		WCHAR strBuf[MAX_PATH];
		DWORD infoSize, tmp;
		GetModuleFileNameW(hMod, strBuf, MAX_PATH);
		m_szModuleFullPath = strBuf;
		infoSize = GetFileVersionInfoSizeW(strBuf, &tmp);
		if (!infoSize)
			return retval;

		LPVOID pBlock = NULL, pVerInfo = NULL;
		pBlock = calloc(infoSize, sizeof(DWORD));
		if (!pBlock)
			return retval;

		if (GetFileVersionInfoW(strBuf, 0, infoSize, pBlock))
		{
			if (VerQueryValueW(pBlock, L"\\", &pVerInfo, (UINT*)&infoSize))
			{
				VS_FIXEDFILEINFO* pVer = (VS_FIXEDFILEINFO*)pVerInfo;
				m_dwProductVersionMS = pVer->dwProductVersionMS;
				m_dwProductVersionLS = pVer->dwProductVersionLS;
				m_dwFileVersionMS = pVer->dwFileVersionMS;
				m_dwFileVersionLS = pVer->dwFileVersionLS;
				retval = TRUE;
			}
			if (VerQueryValue(pBlock, _T("\\VarFileInfo\\Translation"), &pVerInfo, (UINT*)&infoSize) && infoSize >= 4)
			{
				// To get a string value must pass query in the form
				//    "\StringFileInfo\<langID><codepage>\keyname"
				// where <lang-codepage> is the languageID concatenated with the code page, in hex.
				LPCWSTR pVal;
				CStringW query;
				CString__FormatW(query, L"\\StringFileInfo\\%04x%04x\\%s",
				                 LOWORD(*(DWORD*)pVerInfo), // langID
				                 HIWORD(*(DWORD*)pVerInfo), // charset
				                 L"Comments");

				if (VerQueryValueW(pBlock, (LPWSTR)(LPCWSTR)query, (LPVOID*)&pVal, (UINT*)&infoSize))
					m_szModuleComment = pVal;

				CString__FormatW(query, L"\\StringFileInfo\\%04x%04x\\%s",
				                 LOWORD(*(DWORD*)pVerInfo), // langID
				                 HIWORD(*(DWORD*)pVerInfo), // charset
				                 L"FileDescription");

				if (VerQueryValueW(pBlock, (LPWSTR)(LPCWSTR)query, (LPVOID*)&pVal, (UINT*)&infoSize))
					m_szModuleDescription = pVal;
			}
		}
		free(pBlock);
		return retval;
	}

  public:
	FileVersionInfo()
	{
		Clear();
	}
	FileVersionInfo(HMODULE hMod)
	{
		QueryFile(hMod);
	}
	FileVersionInfo(LPCWSTR modName, BOOL loadLib = TRUE)
	{
		QueryFile(modName, loadLib);
	}
	BOOL QueryFile(LPCSTR modName, BOOL loadLib = TRUE)
	{
		return QueryFile(CStringW(modName), loadLib);
	}
	BOOL QueryFile(LPCWSTR modName, BOOL loadLib = TRUE)
	{
		BOOL retval = FALSE;
		BOOL didLoadLib = FALSE;
		Clear();
		if (!modName || modName[0] == L'\0')
			return retval;
		m_szModuleName = modName;
		const int pos = m_szModuleName.ReverseFind(L'\\');
		if (-1 != pos)
			m_szModuleName = m_szModuleName.Mid(pos + 1);
		HMODULE hMod = GetModuleHandleW(modName);
		if (!hMod)
		{
			if (!loadLib)
				return retval;
			hMod = LoadLibraryA(CString(modName));
			if (hMod)
				didLoadLib = TRUE;
			else
				return retval;
		}
		retval = Query(hMod);
		if (didLoadLib)
		{
			FreeLibrary(hMod);
		}
		return retval;
	}
	BOOL QueryFile(HMODULE hMod)
	{
		BOOL retval = FALSE;
		Clear();
		if (!hMod)
			return retval;
		LPWSTR pName = m_szModuleName.GetBuffer((MAX_PATH * 2) + 1);
		GetModuleFileNameW(hMod, pName, MAX_PATH * 2);
		m_szModuleName.ReleaseBuffer();
		return Query(hMod);
	}
	CString GetFileVerString() const
	{
		CString str;
		CString__FormatA(str, "%d.%d.%d.%d", HIWORD(m_dwFileVersionMS), LOWORD(m_dwFileVersionMS),
		                 HIWORD(m_dwFileVersionLS), LOWORD(m_dwFileVersionLS));
		return str;
	}
	CString GetProdVerString() const
	{
		CString str;
		CString__FormatA(str, "%d.%d.%d.%d", HIWORD(m_dwProductVersionMS), LOWORD(m_dwProductVersionMS),
		                 HIWORD(m_dwProductVersionLS), LOWORD(m_dwProductVersionLS));
		return str;
	}
	WORD GetFileVerLSLo() const
	{
		return LOWORD(m_dwFileVersionLS);
	}
	WORD GetFileVerLSHi() const
	{
		return HIWORD(m_dwFileVersionLS);
	}
	WORD GetFileVerMSLo() const
	{
		return LOWORD(m_dwFileVersionMS);
	}
	WORD GetFileVerMSHi() const
	{
		return HIWORD(m_dwFileVersionMS);
	}
	WORD GetProdVerLSLo() const
	{
		return LOWORD(m_dwProductVersionLS);
	}
	WORD GetProdVerLSHi() const
	{
		return HIWORD(m_dwProductVersionLS);
	}
	WORD GetProdVerMSLo() const
	{
		return LOWORD(m_dwProductVersionMS);
	}
	WORD GetProdVerMSHi() const
	{
		return HIWORD(m_dwProductVersionMS);
	}
	DWORD GetFileVerLS() const
	{
		return m_dwFileVersionLS;
	}
	DWORD GetFileVerMS() const
	{
		return m_dwFileVersionMS;
	}
	DWORD GetProdVerMS() const
	{
		return m_dwProductVersionMS;
	}
	DWORD GetProdVerLS() const
	{
		return m_dwProductVersionLS;
	}
	CStringW GetModuleNameW() const
	{
		return m_szModuleName;
	}
	CStringW GetModuleFullPathW() const
	{
		return m_szModuleFullPath;
	}
	CStringW GetModuleDescW() const
	{
		return m_szModuleDescription;
	}
	CStringW GetModuleCommentW() const
	{
		return m_szModuleComment;
	}
	CString GetModuleName() const
	{
		return CString(m_szModuleName);
	}
	CString GetModuleFullPath() const
	{
		return CString(m_szModuleFullPath);
	}
	CString GetModuleDesc() const
	{
		return CString(m_szModuleDescription);
	}
	CString GetModuleComment() const
	{
		return CString(m_szModuleComment);
	}
	BOOL IsValid() const
	{
		return m_dwFileVersionLS != INVALID_VER_INFO && m_dwFileVersionMS != INVALID_VER_INFO &&
		       m_dwProductVersionLS != INVALID_VER_INFO && m_dwProductVersionMS != INVALID_VER_INFO;
	}
};
