#pragma once
#include <functional>

class WTString;

CString GetRegValue(HKEY rootKey, const char* key, const char* name, LPCTSTR defaultValue = "");
CStringW GetRegValueW(HKEY rootKey, const char* key, const char* name, LPCWSTR defaultValue = L"");
DWORD GetRegDword(HKEY rootKey, const char* key, const char* name, DWORD defaultValue = 0);
RECT GetRegRect(HKEY rootKey, const char* key, const char* name);
bool GetRegBool(HKEY rootKey, const char* key, const char* name, bool defaultValue = false);
byte GetRegByte(HKEY rootKey, const char* key, const char* name, byte defaultValue = 0);

LONG DeleteRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name);

LONG ForEachRegSubKeyName(HKEY rootKey, LPCSTR lpSubKey, std::function<bool(LPCSTR)> iter);
LONG ForEachRegValueName(HKEY rootKey, LPCSTR lpSubKey, std::function<bool(LPCSTR)> iter);

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, DWORD value);
LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, LPCSTR value);
LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const WTString& value);
LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const CStringW& value);
LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, LPCSTR value, INT len);
LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const RECT& value);
LONG SetRegValueByte(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, byte value);
LONG SetRegValueBool(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, bool value);
