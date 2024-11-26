#pragma once

// When in scope, MFC functions such as CWinApp::WriteProfileInt will be stored in correct VA's registry path

class CRedirectRegistryToVA
{
  public:
	CRedirectRegistryToVA() : va_dialogs_reg_key(GetVaRegKeyName())
	{
		oldregkey = AfxGetApp()->m_pszRegistryKey;
		AfxGetApp()->m_pszRegistryKey = va_dialogs_reg_key;
	}

	~CRedirectRegistryToVA()
	{
		AfxGetApp()->m_pszRegistryKey = oldregkey;
	}

  protected:
	static CString GetVaRegKeyName()
	{
		extern CString ID_RK_APP;
		const CString name(ID_RK_APP); // ID_RK_APP starts with "Software\\" so need to remove it
		const CString tmp("Software\\");
		const int pos = name.Find(tmp);
		if (-1 != pos)
			return name.Mid(tmp.GetLength());
		return name;
	}

	CString va_dialogs_reg_key;
	const char* oldregkey;
};
