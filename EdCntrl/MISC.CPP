#include "stdafxed.h"
#include "resource.h"
#include <atlbase.h>
#include "settings.h"
#include "RegKeys.h"
#include "file.h"
#include "log.h"
#include "Registry.h"
#include "StackSpace.h"
#include "RecursionClass.h"
#include "StringUtils.h"
#include "mainThread.h"
#include "VaTree.h"
#include "WindowUtils.h"
#include "incToken.h"
#include "PROJECT.H"
#include "ShellListener.h"
#include "vsshell110.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "VaTimers.h"
#include "..\common\ScopedIncrement.h"
#include "VAParse.h"
#include "VAAutomation.h"
#include "EdDll.h"
#include <cwctype>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

WTString GetWindowTextString(HWND h)
{
	WTString r;
	if (h)
	{
		char buf[255] = "";
		if (GetWindowText(h, buf, 254))
			r = buf;
	}
	return r;
}

WTString GetWindowClassString(HWND h)
{
	WTString r = "";
	char buf[255];
	if (h)
	{
		if (GetClassName(h, buf, 254))
			r = buf;
	}
	return r;
}

const char* itoa(int i)
{
	// warning static object, should use i to s
	static char buf[52] = "";
	sprintf(buf, "%d", i);
	return buf;
}

// thread safe use instead of atoi
WTString itos(int i)
{
	char buf[52] = "";
	sprintf(buf, "%d", i);
	return WTString(buf);
}

CStringW itosw(int i)
{
	WCHAR buf[52] = L"";
	swprintf_s(buf, L"%d", i);
	return CStringW(buf);
}

WTString uptrtos(uintptr_t i)
{
	char buf[52] = "";
	sprintf(buf, "%zu", i);
	return WTString(buf);
}

// thread safe use instead of atoi
WTString utos(uint i)
{
	char buf[52] = "";
	sprintf(buf, "%u", i);
	return WTString(buf);
}

CStringW utosw(uint i)
{
	WCHAR buf[52] = L"";
	swprintf_s(buf, L"%u", i);
	return CStringW(buf);
}

WTString hextos(int i)
{
	char buf[52] = "";
	sprintf(buf, "%d", i);
	return WTString(buf);
}

UINT atou(const char* str)
{
	UINT i = 0;
	sscanf(str, "%u", &i);
	return i;
}

int atox(const char* hexstr)
{
	int i = 0;
	sscanf(hexstr, "%x", &i);
	return i;
}

int wtox(const wchar_t* hexstr)
{
	int i = 0;
	swscanf(hexstr, L"%x", &i);
	return i;
}

static HWND hFound = NULL;
static BOOL CALLBACK EnumProc(HWND h, LPARAM id)
{
	if (hFound)
		return false; // already found
	hFound = GetDlgItem(h, (int)id);
	if (hFound == NULL)
		EnumChildWindows(h, (WNDENUMPROC)EnumProc, id);
	return true;
}

HWND GetDescendant(HWND h, int id)
{
	hFound = NULL;
	EnumProc(h, (LPARAM)id);
	return hFound;
}

CString GetRegValue(HKEY rootKey, const char* key, const char* name, LPCTSTR defaultValue /*= ""*/)
{
	CString retval(defaultValue);
	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, key, 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD dataSize = 0, dataType = 0;
		// get string length first
		err = RegQueryValueEx(hKey, name, 0, &dataType, NULL, &dataSize);
		if (ERROR_SUCCESS == err && dataSize)
		{
			_ASSERTE(REG_SZ == dataType || REG_EXPAND_SZ == dataType);
			// allocate a buffer
			dataSize++;
			const std::unique_ptr<TCHAR[]> strDataVec(new TCHAR[dataSize + 1]);
			LPTSTR strData = &strDataVec[0];

			// get the value
			err = RegQueryValueEx(hKey, name, 0, &dataType, (LPBYTE)strData, &dataSize);
			if (ERROR_SUCCESS == err)
			{
				if (dataSize > 1 && !strData[0])
				{
					// unhide hidden key
					retval = CString(&strData[1], int(dataSize - 1));
				}
				else
				{
					// added this check to prevent returning empty strings with a
					// length of 1 - just return an empty CString instead
					if (!(dataSize == 1 && !strData[0]))
						retval = CString(strData, (int)dataSize);
				}

				int len = retval.GetLength();
				if (len && !retval[len])
					retval = retval.Left(len - 1);

				if (REG_EXPAND_SZ == dataType)
					retval = WTExpandEnvironmentStrings(CStringW(retval));
			}
		}
		RegCloseKey(hKey);
	}
	return retval;
}

CStringW GetRegValueW(HKEY rootKey, const char* key, const char* name, LPCWSTR defaultValue /*= L""*/)
{
	CStringW retval(defaultValue);
	HKEY hKey;
	LSTATUS err = RegOpenKeyExW(rootKey, CStringW(key), 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD dataSize = 0, dataType = 0;
		// get string length first
		err = RegQueryValueExW(hKey, CStringW(name), 0, &dataType, NULL, &dataSize);
		if (ERROR_SUCCESS == err && dataSize)
		{
			_ASSERTE(REG_SZ == dataType || REG_EXPAND_SZ == dataType);
			// allocate a buffer
			dataSize += sizeof(WCHAR);
			const std::unique_ptr<WCHAR[]> strDataVec(new WCHAR[dataSize + 1]);
			LPWSTR strData = &strDataVec[0];
			strData[(dataSize / sizeof(WCHAR)) - 1] = L'\0';
			strData[dataSize / sizeof(WCHAR)] = L'\0';

			// get the value
			err = RegQueryValueExW(hKey, CStringW(name), 0, &dataType, (LPBYTE)strData, &dataSize);
			if (ERROR_SUCCESS == err)
			{
				if (dataSize > sizeof(WCHAR) && !strData[0])
					retval = CStringW(&strData[1]); // unhide hidden key
				else
				{
					// added this check to prevent returning empty strings with a
					// length of 1 - just return an empty CString instead
					if (!(dataSize == sizeof(WCHAR) && !strData[0]))
						retval = CStringW(strData);
				}

				if (REG_EXPAND_SZ == dataType)
					retval = WTExpandEnvironmentStrings(retval);
			}
		}
		RegCloseKey(hKey);
	}
	return retval;
}

DWORD GetRegDword(HKEY rootKey, const char* key, const char* name, DWORD defaultValue /*= 0*/)
{
	DWORD retval = defaultValue;
	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, key, 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD dataSize = sizeof(DWORD), dataType;
		err = RegQueryValueEx(hKey, name, 0, &dataType, (LPBYTE)&retval, &dataSize);
		if (ERROR_SUCCESS == err)
		{
			_ASSERTE(sizeof(DWORD) == dataSize);
			_ASSERTE(REG_DWORD == dataType);
		}
		else
			retval = defaultValue;
		RegCloseKey(hKey);
	}
	return retval;
}

RECT GetRegRect(HKEY rootKey, const char* key, const char* name)
{
	RECT rc = {0, 0, 0, 0};
	CString rcStr(GetRegValue(rootKey, key, name));
	if (rcStr.GetLength())
		sscanf(rcStr, "%ld,%ld,%ld,%ld", &rc.top, &rc.left, &rc.bottom, &rc.right);
	return rc;
}

bool GetRegBool(HKEY rootKey, const char* key, const char* name, bool defaultValue /*= false*/)
{
	bool retval = defaultValue;
	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, key, 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD dataSize = sizeof(bool), dataType;
		err = RegQueryValueEx(hKey, name, 0, &dataType, (LPBYTE)&retval, &dataSize);
		if (ERROR_SUCCESS == err)
		{
			_ASSERTE(sizeof(bool) == dataSize);
			_ASSERTE(REG_BINARY == dataType);
		}
		else
			retval = defaultValue;
		RegCloseKey(hKey);
	}
	return retval;
}

byte GetRegByte(HKEY rootKey, const char* key, const char* name, byte defaultValue /*= 0*/)
{
	byte retval = defaultValue;
	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, key, 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD dataSize = sizeof(byte), dataType;
		err = RegQueryValueEx(hKey, name, 0, &dataType, (LPBYTE)&retval, &dataSize);
		if (ERROR_SUCCESS == err)
		{
			_ASSERTE(sizeof(byte) == dataSize);
			_ASSERTE(REG_BINARY == dataType);
		}
		else
			retval = defaultValue;
		RegCloseKey(hKey);
	}
	return retval;
}

LONG DeleteRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name)
{
	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, lpSubKey, 0, KEY_QUERY_VALUE | KEY_WRITE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		err = RegDeleteValue(hKey, name);
		RegCloseKey(hKey);
	}
	return err;
}

LONG ForEachRegSubKeyName(HKEY rootKey, LPCSTR lpSubKey, std::function<bool(LPCSTR)> iter)
{
	_ASSERTE(iter != nullptr);

	if (iter == nullptr)
		return ERROR_BAD_ARGUMENTS;

	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, lpSubKey, 0, KEY_READ, &hKey);
	if (ERROR_SUCCESS == err)
	{
		CHAR SubKeyName[256];
		DWORD cName = 256;

		for (DWORD Index = 0;; Index++)
		{
			cName = 256;
			LSTATUS res = RegEnumKeyEx(hKey, Index, SubKeyName, &cName, NULL, NULL, NULL, NULL);

			if (res != ERROR_SUCCESS)
				break;

			SubKeyName[cName] = '\0';

			if (!iter(SubKeyName))
				break;
		}
		RegCloseKey(hKey);
	}
	return err;
}

LONG ForEachRegValueName(HKEY rootKey, LPCSTR lpSubKey, std::function<bool(LPCSTR)> iter)
{
	_ASSERTE(iter != nullptr);

	if (iter == nullptr)
		return ERROR_BAD_ARGUMENTS;

	HKEY hKey;
	LSTATUS err = RegOpenKeyEx(rootKey, lpSubKey, 0, KEY_READ, &hKey);
	if (ERROR_SUCCESS == err)
	{
		CHAR ValueName[16384];
		DWORD cName = 16384;

		for (DWORD Index = 0;; Index++)
		{
			cName = 16384;
			LSTATUS res = RegEnumValue(hKey, Index, ValueName, &cName, NULL, NULL, NULL, NULL);

			if (res != ERROR_SUCCESS)
				break;

			ValueName[cName] = '\0';

			if (!iter(ValueName))
				break;
		}
		RegCloseKey(hKey);
	}
	return err;
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const RECT& value)
{
	CString rcStr;
	CString__FormatA(rcStr, "%ld,%ld,%ld,%ld", value.top, value.left, value.bottom, value.right);
	return SetRegValue(rootKey, lpSubKey, name, rcStr);
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, DWORD value)
{
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyEx(rootKey, lpSubKey, 0, const_cast<LPSTR>(""), REG_OPTION_NON_VOLATILE,
	                             KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
	{
		err = RegSetValueEx(hKey, name, 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD));
		RegCloseKey(hKey);
	}
	return err;
}

LONG SetRegValueByte(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, byte value)
{
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyEx(rootKey, lpSubKey, 0, const_cast<LPSTR>(""), REG_OPTION_NON_VOLATILE,
	                             KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
	{
		err = RegSetValueEx(hKey, name, 0, REG_BINARY, (LPBYTE)&value, sizeof(byte));
		RegCloseKey(hKey);
	}
	return err;
}

LONG SetRegValueBool(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, bool value)
{
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyEx(rootKey, lpSubKey, 0, const_cast<LPSTR>(""), REG_OPTION_NON_VOLATILE,
	                             KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
	{
		err = RegSetValueEx(hKey, name, 0, REG_BINARY, (LPBYTE)&value, sizeof(bool));
		RegCloseKey(hKey);
	}
	return err;
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, LPCSTR value)
{
	DWORD dataSize = strlen_u(value) + 1;
	HKEY hKey;
	int idx;
	if (name && strcmp(ID_RK_USERKEY, name) == 0)
	{
		// hide string value
		const std::unique_ptr<char[]> bufVec(new char[1096]);
		char* buf = &bufVec[0];
		buf[0] = '\0';
		strcpy(&buf[1], value);
		return (LONG)SetRegValue(rootKey, lpSubKey, name, buf, int(dataSize + 1));
	}

	LSTATUS err = RegCreateKeyEx(rootKey, lpSubKey, 0, const_cast<LPSTR>(""), REG_OPTION_NON_VOLATILE,
	                             KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
		err = RegSetValueEx(hKey, name, 0, REG_SZ, (LPBYTE)value, dataSize);
	RegCloseKey(hKey);
	return err;
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const CStringW& value)
{
	DWORD dataSize = (value.GetLength() + 1) * sizeof(WCHAR);
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyExW(rootKey, CStringW(lpSubKey), 0, const_cast<LPWSTR>(L""), REG_OPTION_NON_VOLATILE,
	                              KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
		err = RegSetValueExW(hKey, CStringW(name), 0, REG_SZ, (LPBYTE)(LPCWSTR)value, dataSize);
	RegCloseKey(hKey);
	return err;
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, LPCSTR value, INT len)
{
	int dataSize = len + 1;
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyEx(rootKey, lpSubKey, 0, const_cast<LPSTR>(""), REG_OPTION_NON_VOLATILE,
	                             KEY_QUERY_VALUE | KEY_WRITE, NULL, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
		err = RegSetValueEx(hKey, name, 0, REG_SZ, (LPBYTE)value, (DWORD)dataSize);
	RegCloseKey(hKey);
	return err;
}

LONG SetRegValue(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const WTString& value)
{
	return SetRegValue(rootKey, lpSubKey, name, value.c_str());
}

DWORD g_mainThread = GetCurrentThreadId();
long g_vapm_count = 0;
static CCriticalSection g_vapmCritSect;
static DWORD g_lmsg = NULL;

void VAProcessMessages(UINT msg_start /*= 0*/, UINT msg_end /*= WM_USER-1*/)
{
	if (g_mainThread != GetCurrentThreadId())
		return;

	// fix the msdev hang at shutdown when opened via automation bug.
	// we eat a WM_USER OLE msg and that causes the hang.
	_ASSERTE(msg_end < WM_USER);

	ScopedIncrementAtomic si(&gProcessingMessagesOnUiThread);
	AutoLockCs l(g_vapmCritSect);
	ScopedIncrement si1(&g_vapm_count);
	_ASSERTE(g_vapm_count == 1);
	MSG msg;
	// added max so we bail if the message isn't processed.
	// we will spin on wm_paint if m_dopaint is 0.  Cause the message never gets handled.
	for (int max = 50; max && PeekMessage(&msg, NULL, msg_start, msg_end, PM_REMOVE); max--)
	{
		// vLog(("VAProcessMessage 0x%x", msg.message));
		g_lmsg = msg.message;
		// check to make sure we don't close the window...
		switch (msg.message)
		{
			/* found a case where reparse was waiting for a thread in vaprocessmessage
			 * and we processed a keydown which called reparse, so we did a reparse w/in
			 * another reparse.
			 * This is an attempt to limit recursion, yet prevent lockup.
			 */
		case WM_PAINT:
			//		case WM_MOUSEMOVE:
			//		case WM_NCMOUSEMOVE:
		case WM_NCPAINT:
			//			PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);
			//			if(msg.message != WM_CLOSE && msg.message != WM_LBUTTONUP){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			//				}
			break;
		case WM_COMMAND:
		case WM_KEYDOWN:
#ifdef jer
			// oops we lost an event, not sure if we should repost?
			extern volatile bool StopIt;
			if (!StopIt)
			{
				ASSERT(FALSE);
				MessageBeep(0xffffffff);
			}
#endif
			break;
		}
	}
}

BOOL HasUnformattableMultilineCStyleComment(LPCWSTR srcstring)
{
	if (!srcstring || !*srcstring)
		return FALSE;
	LPCWSTR pComment = wcsstr(srcstring, L"/*");
	if (!pComment)
		return FALSE;

	pComment += 2;
	bool inComment = true;
	bool hadLinebreak = false;
	const size_t srcLen = wcslen(pComment);
	for (size_t idx = 0; idx < srcLen; idx++)
	{
		WCHAR curCh = pComment[idx];
		if (curCh == L'/' && pComment[idx + 1] == L'*')
		{
			inComment = true;
			curCh = pComment[++idx]; // extra inc
		}
		else if (inComment && curCh == L'*' && pComment[idx + 1] == L'/')
		{
			inComment = false;
			idx++; // extra inc
		}

		if (inComment)
		{
			// [case: 43351] only return TRUE if there is leading whitespace
			// immediately after a linebreak
			if (curCh == L'\r' || curCh == L'\n')
			{
				hadLinebreak = true;
			}
			else if (hadLinebreak)
			{
				// check here for whitespace
				if (curCh == L' ' || curCh == L'\t')
					return TRUE;

				hadLinebreak = false;
			}
		}
	}
	return FALSE;
}

int RecursionClass::m_count = 0;

CStringW MbcsToWide(LPCSTR text, int len, int mbcsCodepage /*= CP_UTF8*/)
{
	CStringW wstr;
	PWCHAR p = wstr.GetBufferSetLength(len);
	if (p)
	{
		len = MultiByteToWideChar((UINT)mbcsCodepage, 0, text, len, p, len);
		wstr.ReleaseBuffer(len);
	}
	return wstr;
}

WTString WideToMbcs(LPCWSTR bstrText, int len, int mbcsCodepage /*= CP_UTF8*/)
{
	if (len)
	{
		// lossy conversion if chars in text aren't able to be mapped to mbcsCodepage
		int blen = WideCharToMultiByte((UINT)mbcsCodepage, 0, bstrText, len, NULL, 0, NULL, NULL);
		char* cbuf = new char[(uint)blen + 2];
		if (cbuf)
		{
			WideCharToMultiByte((UINT)mbcsCodepage, 0, bstrText, len, cbuf, blen, NULL, NULL);
			cbuf[blen] = '\0';
			WTString rstr(cbuf);
			delete[] cbuf;
			return rstr;
		}
	}

	return NULLSTR;
}

#ifdef _WIN64
namespace
{
using GetCurrentThreadStackLimits_t = VOID(WINAPI*)(PULONG_PTR LowLimit, PULONG_PTR HighLimit);
std::once_flag vaGetCurrentThreadStackLimits_flag;
GetCurrentThreadStackLimits_t vaGetCurrentThreadStackLimits;
} // namespace
#endif
bool HasSufficientStackSpace()
{
#ifdef _WIN64
	if (!gVaDllApp)
		return true; // global vars not initialized yet
	std::call_once(vaGetCurrentThreadStackLimits_flag, []() {
		vaGetCurrentThreadStackLimits = (GetCurrentThreadStackLimits_t)(uintptr_t)GetProcAddress(
		    GetModuleHandleA("kernel32.dll"), "GetCurrentThreadStackLimits");
	});
	if (!vaGetCurrentThreadStackLimits)
		return true;

	volatile char i = 0;
	i = i + 1;

	ULONG_PTR pPtr = (ULONG_PTR)&i;
	ULONG_PTR low = 0, high = 0;
	vaGetCurrentThreadStackLimits(&low, &high);

	bool inside = std::clamp(pPtr, low, high) == pPtr;
	assert(inside); // gmit: something's wrong with this routine - check!
	if (!inside)
	{
		vLog("InsufficientStackSpace - incorrect assumption");
		return false;
	}
	else if ((pPtr - low) < 175000ull)
	{
		vLog("InsufficientStackSpace");
		return false;
	}
	else
		return true;
#else
	PBYTE pPtr;
	_asm mov pPtr, esp; // Get stack pointer

	MEMORY_BASIC_INFORMATION mbi;
	VERIFY(VirtualQuery(pPtr, &mbi, sizeof(mbi)));
	const size_t availableStack = size_t((PBYTE)mbi.BaseAddress - (PBYTE)mbi.AllocationBase);
	_ASSERTE(mbi.State == MEM_COMMIT);
	// mbi.RegionSize is size of committed (used) stack space
	// default stack size should be at least 256KB (usually 1MB but is 256KB
	// on Win server 2003 vpc image for orcas beta 1)
	_ASSERTE((availableStack + mbi.RegionSize) >= 0x40000);
	if (availableStack < 175000L) // return false if less than 175K (build 1845 wer event id -1785954160)
	{
		vLog("InsufficientStackSpace");
		return false;
	}
	else if (mbi.BaseAddress < mbi.AllocationBase)
	{
		// incorrect assumption about stack
		_ASSERTE(mbi.BaseAddress > mbi.AllocationBase);
		vLog("InsufficientStackSpace - incorrect assumption");
		return false;
	}
	return true;
#endif
}

WTString FormatStr(UINT nID, ...)
{
	CString frmt;
	WTString str;
	frmt.LoadString(nID);
	va_list args;
	va_start(args, nID);
	str.FormatV(frmt, args);
	va_end(args);
	return str;
}

std::string __std_string_format(const char* fmt, ...)
{
	const std::unique_ptr<char[]> tempStore(new char[4096]);
	char* temp = &tempStore[0];
	*temp = 0;
	va_list args;
	va_start(args, fmt);
	::vsprintf(temp, fmt, args);
	va_end(args);
	return std::string(temp);
}

WTString StripCommentsAndStrings(int langType, const WTString& src, bool stripStrings /*= true*/)
{
	if (src.Find("/*") == -1 && src.Find("//") == -1 &&
	    (!stripStrings || (src.Find('\"') == -1 && src.Find('\'') == -1)))
		return src;

	TextStripper ct(langType, !stripStrings);
	ct.KeepWhitespace();
	return ct.StripText(src);
}

CStringW GetClipboardText(HWND hWnd)
{
	CStringW clipTxt;
	UINT cbfmts[] = {CF_UNICODETEXT, CF_TEXT};
	HANDLE hglb = NULL;

	const int fmt = GetPriorityClipboardFormat(cbfmts, sizeof(cbfmts));
	if (fmt < 1)
	{
		vLog("ERROR: GetPriorityClipboardFormat requested format not available");
		if (gTestLogger)
			gTestLogger->LogStr(L"GetPriorityClipboardFormat requested format not available");
		return clipTxt;
	}

	if (!OpenClipboard(hWnd))
	{
		vLog("ERROR: OpenClipboard failed");
		if (gTestLogger)
			gTestLogger->LogStr(L"OpenClipboard failed");
		return clipTxt;
	}

	switch (fmt)
	{
	case CF_UNICODETEXT:
		hglb = ::GetClipboardData(CF_UNICODETEXT);
		if (hglb)
			clipTxt = (LPCWSTR)::GlobalLock(hglb);
		break;
	case CF_TEXT:
		hglb = ::GetClipboardData(CF_TEXT);
		if (hglb)
			clipTxt = (LPCSTR)::GlobalLock(hglb);
		break;
	default:
		vLog("ERROR: requested clipboard format not available (2)");
		if (gTestLogger)
			gTestLogger->LogStr(L"requested clipboard format not available (2)");
		break;
	}

	if (hglb)
		::GlobalUnlock(hglb);
	::CloseClipboard();
	return clipTxt;
}

WTString GetClipboardRtf(HWND hWnd)
{
	WTString clipTxt;
	static const CLIPFORMAT cfRTF = (CLIPFORMAT)::RegisterClipboardFormatA(CF_RTF);
	HANDLE hglb = NULL;

	if (!OpenClipboard(hWnd))
		return clipTxt;

	hglb = ::GetClipboardData(cfRTF);
	if (hglb)
		clipTxt = (LPCSTR)::GlobalLock(hglb);

	if (hglb)
		::GlobalUnlock(hglb);
	::CloseClipboard();
	return clipTxt;
}

void SaveToClipboard(HWND hWndOwner, const CStringW& txt)
{
	if (OpenClipboard(hWndOwner))
	{
		// add to clipboard and then call paste
		HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (txt.GetLength() + 1) * sizeof(WCHAR));
		LPVOID pTxt = GlobalLock(hData);
		memcpy(pTxt, (LPCWSTR)txt, (txt.GetLength() + 1) * sizeof(WCHAR));

		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hData);
		CloseClipboard();
		// The Clipboard now owns the allocated memory and will delete this
		//  data object when new data is put on the Clipboard
		GlobalUnlock(hData);
	}

	if (g_VATabTree && Psettings->m_clipboardCnt && txt.GetLength())
		g_VATabTree->AddCopy(txt);
}

void GetWindowTextW(HWND h, CStringW& text)
{
	_ASSERTE(h);
	int nLen = GetWindowTextLengthW(h);
	GetWindowTextW(h, text.GetBufferSetLength(nLen), nLen + 1);
	text.ReleaseBuffer();
}

#include "utils_goran.h"

UINT_PTR
RegisterTimer(LPCSTR timer_name)
{
	_ASSERTE(timer_name != nullptr && *timer_name != 0);

	try
	{
		// since RegisterTimer is mainly used to initialize const/static
		// variables, I needed to GUARANTEE that "timers_cs" is
		// initialized BEFORE CSingleLock will be applied.
		// As global static variables initialization order is
		// unpredictable, I had to move those static variables into
		// body of method to be initialized when method is called.
		// RegisterTimer is used to initialize globals, therefore the
		// local statics have threadsafe init.

		static CCriticalSection timers_cs;
		using timers_t = std::map<std::string, UINT_PTR>;
		static timers_t timers;
		static UINT_PTR timer_id = IDT_GENERATOR_MIN;

		__lock(timers_cs);

		timers_t::const_iterator it = timers.find(timer_name);
		if (it != timers.end())
			return it->second;

		UINT_PTR m_val = timer_id++;

		_ASSERTE(m_val >= IDT_GENERATOR_MIN && m_val <= IDT_GENERATOR_MAX);

		timers[timer_name] = m_val;

		return m_val;
	}
	catch (const std::exception& e)
	{
		//		LPCSTR what = e.what();
		(void)e;
		_ASSERTE(!"Exception in ::RegisterTimer");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in ::RegisterTimer");
	}

	return IDT_GENERATOR_MIN;
}

// gmit: Window Properties emulation - begin

using std::string;
using stdext::hash_map;

static CSpinCriticalSection props_cs;
static hash_map<HWND, hash_map<string, HANDLE>*> props; // <hwnd, <prop, data>>
static HHOOK props_hook; // hook that monitors window destruction to cleanup our props table

static LRESULT CALLBACK CallWndProc(int code, WPARAM wparam, LPARAM lparam)
{
	const CWPSTRUCT* cwp = (const CWPSTRUCT*)lparam;
	if (cwp && (cwp->message == WM_NCDESTROY))
	{
#ifdef NDEBUG
		// wer event id -1590108055 crash during shutdown with only 1 thread active
		// if gMainWnd == NULL, then skip cleanup
		if (gMainWnd)
#endif // NDEBUG
		{
			__lock(props_cs);
			hash_map<HWND, hash_map<string, HANDLE>*>::const_iterator it = props.find(cwp->hwnd);
			if (it != props.end())
			{
				_ASSERTE(it->second);
				if (it->second)
				{
					hash_map<string, HANDLE> props_copy = *(it->second);
					for (hash_map<string, HANDLE>::const_iterator it2 = props_copy.begin(); it2 != props_copy.end();
					     ++it2)
					{
						// kill all props and do all cleanup necessary
						auto s = it2->first;
						// [case: 84223] do not automatically remove __VA_patched_wndproc.
						// it will be removed manually in a separate WNDPROC hook either before or after
						// this hook also in response to WM_NCDESTROY.
						if (s != "__VA_patched_wndproc")
							myRemoveProp(cwp->hwnd, s.c_str());
					}
				}
			}
		}
	}

	if (props_hook)
		return ::CallNextHookEx(props_hook, code, wparam, lparam);

	return 0;
}

BOOL mySetProp(HWND hwnd, const char* prop, HANDLE data)
{
	_ASSERTE(HIWORD(prop) && "atom props not supported");
	if (!hwnd || !prop)
		return false;

	__lock(props_cs);

	if (!props_hook)
		props_hook = ::SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, NULL, ::GetCurrentThreadId());

	hash_map<string, HANDLE>*& window_props = props[hwnd];
	if (!window_props)
		window_props = new hash_map<string, HANDLE>;
	(*window_props)[prop] = data;

	return true;
}

HANDLE myGetProp(HWND hwnd, const char* prop)
{
	if (!hwnd || !prop)
		return NULL;

	__lock(props_cs);

	hash_map<HWND, hash_map<string, HANDLE>*>::const_iterator it = props.find(hwnd);
	if (it == props.end())
		return NULL;
	_ASSERTE(it->second);
	hash_map<string, HANDLE>::const_iterator it2 = it->second->find(prop);
	if (it2 == it->second->end())
		return NULL;

	return it2->second;
}

#pragma warning(disable : 4503) // template symbol name truncation
HANDLE myRemoveProp(HWND hwnd, const char* prop)
{
	if (!hwnd || !prop)
		return NULL;

	__lock(props_cs);

	hash_map<HWND, hash_map<string, HANDLE>*>::iterator it = props.find(hwnd);
	if (it == props.end())
		return NULL;

	_ASSERTE(it->second);
	if (!it->second)
		return NULL;

	hash_map<string, HANDLE>::iterator it2 = it->second->find(prop);
	if (it2 == it->second->end())
		return NULL;

	HANDLE ret = it2->second;
	it->second->erase(it2);
	if (!it->second->size())
	{
		auto itsec = it->second;
		props.erase(it);
		delete itsec;
		if (!props.size())
		{
			HHOOK tmp = props_hook;
			if (tmp)
			{
				props_hook = NULL;
				::UnhookWindowsHookEx(tmp);
			}
		}
	}
	return ret;
}
// gmit: Window Properties emulation - end

WTString BuildMenuTextHexAccelerator(uint index, CString text)
{
	_ASSERTE(index);
	_ASSERTE(!text.IsEmpty());
	WTString menuText;
	if (index > 0 && index < 16)
		menuText.WTFormat("%s\t&%x", (const char*)text, index);
	else if (wt_isalnum(text[0]))
		menuText = '&' + text;
	else
		return WTString(text);

	return menuText;
}

CStringW BuildMenuTextHexAcceleratorW(uint index, CStringW text, bool escapeAmpersands /*= true*/)
{
	_ASSERTE(index);
	_ASSERTE(!text.IsEmpty());

	if (escapeAmpersands)
	{
		// escape existing &'s
		text.Replace(L"&", L"&&");
	}

	CStringW menuText;
	if (index > 0 && index < 16)
		CString__FormatW(menuText, L"%s\t&%x", (const wchar_t*)text, index);
	else if (wt_isalnum(text[0]))
		menuText = L'&' + text;
	else
		return text;

	return menuText;
}

WTString GetMinimumRequiredName(const WTString& qualifiedName, const WTString& basescope)
{
	if (qualifiedName.IsEmpty() || basescope.IsEmpty())
		return qualifiedName;
	if (qualifiedName[0] != DB_SEP_CHR)
		return qualifiedName;
	if (basescope[0] != DB_SEP_CHR)
		return qualifiedName;

	WTString resultName(qualifiedName);

	for (int pos1 = 1, pos2 = 1; pos1 < qualifiedName.GetLength() && pos2 < qualifiedName.GetLength(); pos1++, pos2++)
	{
		resultName = qualifiedName.Mid(pos1 - 1);
		pos1 = qualifiedName.Find(DB_SEP_STR, pos1);
		pos2 = basescope.Find(DB_SEP_STR, pos2);
		if (-1 != pos1 && -1 != pos2)
		{
			if (pos1 != pos2)
				break;

			const WTString tmp1(qualifiedName.Left(pos1));
			const WTString tmp2(basescope.Left(pos2));
			if (tmp1 != tmp2)
				break;

			resultName.Empty();
		}
		else if (-1 == pos1)
		{
			break;
		}
		else // if (-1 == pos2)
		{
			_ASSERTE(-1 == pos2);
			const WTString tmp1(qualifiedName.Left(pos1));
			if (tmp1 == basescope)
				resultName = qualifiedName.Mid(pos1);
			break;
		}
	}

	return resultName;
}

WTString GetMinimumRequiredNameFromList(const WTString& qualifiedName, const WTString& scopeList)
{
	if (qualifiedName.IsEmpty() || scopeList.IsEmpty())
		return qualifiedName;
	if (qualifiedName[0] != DB_SEP_CHR)
		return qualifiedName;
	if (scopeList[0] != DB_SEP_CHR && scopeList[0] != '\f')
		return qualifiedName;

	WTString res(qualifiedName), tmp;
	// iterate over list - return shortest result
	for (token2 scopeListTok(scopeList); scopeListTok.more();)
	{
		WTString curItem(scopeListTok.read('\f'));
		if (curItem.IsEmpty() || curItem == DB_SEP_STR)
			continue;

		tmp = GetMinimumRequiredName(qualifiedName, curItem);
		if (tmp.GetLength() < res.GetLength())
			res = tmp;
	}

	return res;
}

int RemoveLinesThatContainFragment(CStringW& str, CStringW lineFragmentMatch)
{
	_ASSERTE(lineFragmentMatch.GetLength());
	int replacements = 0;
	for (;;)
	{
		int p1 = str.Find(lineFragmentMatch);
		if (-1 == p1)
			break;

		++replacements;

		int p2 = str.Find(L'\r', p1);
		if (-1 == p2)
			p2 = str.Find(L'\n', p1);
		if (-1 == p2)
		{
			str = str.Left(p1);
			break;
		}

		while (++p2 < str.GetLength())
		{
			const WCHAR ch = str.GetAt(p2);
			if (ch != L'\r' && ch != L'\n')
				break;
		}

		CStringW tmp(str.Left(p1));
		tmp += str.Mid(p2);
		str = tmp;
	}
	return replacements;
}

BOOL ContainsIW(CStringW str, CStringW substr)
{
	str.MakeLower();
	substr.MakeLower();
	if (-1 == str.Find(substr))
		return FALSE;
	return TRUE;
}

BOOL StrIsLowerCase(const WTString& str)
{
	if (str.IsEmpty())
		return FALSE;

	for (int i = 0; str[i]; i++)
	{
		if (str[i] & 0x80)
		{
			// utf8 multi-byte char: assume upper when checking to see if all lower
			return FALSE;
		}
		if (wt_isupper(str[i]))
			return FALSE;
	}

	return TRUE;
}

BOOL StrIsUpperCase(const WTString& str)
{
	if (str.IsEmpty())
		return FALSE;

	for (int i = 0; str[i]; i++)
	{
		if (str[i] & 0x80)
		{
			// utf8 multi-byte char: assume lower when checking to see if all upper
			return FALSE;
		}
		if (wt_islower(str[i]))
			return FALSE;
	}

	return TRUE;
}

BOOL StrHasUpperCase(const CStringA str)
{
	if (str.IsEmpty())
		return FALSE;

	for (int i = 0; str[i]; ++i)
	{
		if (str[i] & 0x80)
		{
			// utf8 multi-byte char: assume upper when checking to see if has any upper
			return FALSE;
		}
		if (wt_isupper(str[i]))
			return TRUE;
	}

	return FALSE;
}

BOOL StrHasUpperCase(const CStringW str)
{
	if (str.IsEmpty())
		return FALSE;

	for (int i = 0; str[i]; ++i)
	{
		if (wt_isupper(str[i]))
			return TRUE;
	}

	return FALSE;
}

int GetUtf8SequenceLen(LPCSTR cpt)
{
	if (!cpt)
		return 0;

	if ((*cpt & 0xF8) == 0xF0)
	{
		// start of 4-byte sequence
		if (((*(cpt + 1) & 0xC0) == 0x80) && ((*(cpt + 2) & 0xC0) == 0x80) && ((*(cpt + 3) & 0xC0) == 0x80))
			return 4;
	}
	else if ((*cpt & 0xF0) == 0xE0)
	{
		// start of 3-byte sequence
		if (((*(cpt + 1) & 0xC0) == 0x80) && ((*(cpt + 2) & 0xC0) == 0x80))
			return 3;
	}
	else if ((*cpt & 0xE0) == 0xC0)
	{
		// start of 2-byte sequence
		if ((*(cpt + 1) & 0xC0) == 0x80)
			return 2;
	}
	else if ((*cpt & 0x80) == 0x00)
	{
		// high bit not set, supported ascii character
		return 1;
	}

	return 0;
}

bool CanReadAsUtf8(LPCSTR buf)
{
	if (!buf)
		return false;

	for (int idx = 0; buf[idx]; ++idx)
	{
		if ((buf[idx] & 0x80))
		{
			if (!buf[idx + 1])
			{
				// when not encoded as utf8 "identit�" is not valid utf8
				return false;
			}

			int seqLen = GetUtf8SequenceLen(&buf[idx]);
			if (0 == seqLen)
			{
				// invalid utf8 sequence
				return false;
			}
			else if (seqLen > 1)
			{
				idx += seqLen - 1;
			}
			else
			{
				return false;
			}
		}
	}

	return true;
}

int strlen_utf8(LPCSTR buf)
{
	if (!buf)
		return 0;

	int len = 0;
	for (int idx = 0; buf[idx]; ++len, ++idx)
	{
		if ((buf[idx] & 0x80) && buf[idx + 1])
		{
			int seqLen = GetUtf8SequenceLen(&buf[idx]);
			if (0 == seqLen)
			{
				// invalid utf8 sequence
				_ASSERTE(!"bad utf8 sequence");
				return (int)_mbslen((unsigned char const*)buf);
			}
			else if (seqLen > 1)
			{
				idx += seqLen - 1;
			}
			else
			{
				_ASSERTE(!"bad utf8 sequence");
				// bad because 0x80 shouldn't be set for seqLen of 1
				return (int)_mbslen((unsigned char const*)buf);
			}
		}
	}

	return len;
}

int AdjustPosForMbChars(const WTString& buf, int pos)
{
	// [case: 61871] fix offset because VS uses utf16 but VA uses utf8
	LPCSTR pBuf = buf.c_str();
	int posToReturn = pos;
	int dbcsCorrection = 0;
	for (int idx = 0; idx < pos && pBuf[idx]; ++idx)
	{
		if ((pBuf[idx] & 0x80) && pBuf[idx + 1])
		{
			int seqLen = GetUtf8SequenceLen(&pBuf[idx]);
			if (0 == seqLen)
			{
				// invalid utf8 sequence
				_ASSERTE(!"bad utf8 sequence");
				break;
			}
			else if (seqLen > 1)
			{
				const int extraBytes = seqLen - 1;
				dbcsCorrection += extraBytes;
				idx += extraBytes;
				pos += extraBytes;
			}
			else
			{
				_ASSERTE(!"bad utf8 sequence");
				// bad because 0x80 shouldn't be set for seqLen of 1
				++pos;
			}
		}
	}

	return posToReturn + dbcsCorrection;
}

int ByteOffsetToCharOffset(const WTString& buf, int byteOffset, bool returnUtf16ElementOffset /*= false*/)
{
	// [case: 63166] change from byte offset to character offset
	_ASSERTE(byteOffset < buf.GetLength());
	LPCSTR pBuf = buf.c_str();
	int posToReturn = byteOffset;
	int dbcsCorrection = 0;
	for (int idx = 0; idx < byteOffset && pBuf[idx]; ++idx)
	{
		if ((pBuf[idx] & 0x80) && pBuf[idx + 1])
		{
			int seqLen = GetUtf8SequenceLen(&pBuf[idx]);
			if (0 == seqLen)
			{
				// invalid utf8 sequence
				_ASSERTE(!"bad utf8 sequence");
				break;
			}
			else if (seqLen > 1)
			{
				if (++idx >= byteOffset)
				{
					// don't increment dbcsCorrection if we aren't going to read the following byte
					break;
				}

				if (returnUtf16ElementOffset && 4 == seqLen)
				{
					// [case: 138734]
					// surrogate pair in utf16.
					// increase char count that we return, to return utf16
					// elements rather than chars.
					--dbcsCorrection;
				}

				dbcsCorrection += seqLen - 1;
				idx += seqLen - 2;
			}
			else
			{
				_ASSERTE(!"bad utf8 sequence");
			}
		}
	}

	posToReturn -= dbcsCorrection;
	if (posToReturn >= 0)
		return posToReturn;

	_ASSERTE(!"bad calc in ByteOffsetToCharOffset");
	return byteOffset;
}

BOOL VsScrollbarTheme(HWND hWnd, BOOL enable /*= TRUE*/)
{
	if (!gShellAttr)
	{
		_ASSERTE(!"VsScrollbarTheme called before gShellAttr created");
		return FALSE;
	}

	if (!gShellAttr->IsDevenv11OrHigher())
		return FALSE;

	static CStringW sPropName;
	if (sPropName.IsEmpty())
	{
		CComVariant val;
		CComPtr<IVsShell> vsh(::GetVsShell());
		if (vsh && S_OK == vsh->GetProperty(VSSPROPID_NativeScrollbarThemeModePropName, &val))
			sPropName = val.bstrVal;
	}

	if (sPropName.IsEmpty())
	{
		_ASSERTE(!"failed to theme scrollbar in vs2012+");
		return FALSE;
	}

	if (enable)
		SetPropW(hWnd, sPropName, (HANDLE)NSTM_All);
	else
		SetPropW(hWnd, sPropName, (HANDLE)NSTM_None);

	return TRUE;
}

int FindNoCase(const CStringW& inStr, const CStringW& findThis)
{
	CStringW inLower = inStr;
	inLower.MakeLower();

	CStringW findThisLower = findThis;
	findThisLower.MakeLower();

	return inLower.Find(findThisLower);
}

void ReplaceNoCase(CStringW& ioStr, const CStringW& replaceThis, const CStringW& withThis)
{
	if (replaceThis == L"")
		return;

	CStringW ioStrLower(ioStr);
	ioStrLower.MakeLower();

	CStringW replaceThisLower(replaceThis);
	replaceThisLower.MakeLower();

	const int oldLen = replaceThis.GetLength();
	const int newLen = withThis.GetLength();

	int readPos = 0;
	int lastReadPos = 0;
	int writePos = 0;
	while ((readPos = ioStrLower.Find(replaceThisLower, readPos)) != -1)
	{
		writePos += (readPos - lastReadPos);
		ioStr.Delete(writePos, oldLen);
		ioStr.Insert(writePos, withThis);
		writePos += newLen;

		readPos += oldLen;
		lastReadPos = readPos;
	}
}

void ReplaceWholeWord(CStringW& ioStr, const CStringW& replaceThis, const CStringW& withThis)
{
	int pos = 0;
	while ((pos = ioStr.Find(replaceThis, pos)) != -1)
	{
		if (pos == 0 || !ISCSYM(ioStr[pos - 1]) && !ISCSYM(ioStr[pos + replaceThis.GetLength()]))
		{
			ioStr.Delete(pos, replaceThis.GetLength());
			ioStr.Insert(pos, withThis);
			pos += withThis.GetLength();
		}
		else
			++pos;
	}
}

// [case: 75172]
// mem alloc failure when record is copied since record is not null terminated
// (the entire data set is null terminated; the records are \n terminated).
WTString GetNlDelimitedRecord(LPCTSTR pStr)
{
	if (!pStr || !*pStr)
		return WTString();

	LPCTSTR pNext = ::_tcschr(pStr, _T('\n'));
	if (!pNext)
	{
		// final record is not delimited; read to end of str
		size_t len = ::_tcslen(pStr);
		pNext = pStr + (len * sizeof(TCHAR));
	}

	const WTString curItem(pStr, ptr_sub__int(pNext, pStr));
	return curItem;
}

int OneTimeMessageBox(LPCTSTR regItemName, LPCTSTR text, UINT msgBoxType /*= MB_OK | MB_ICONINFORMATION*/,
                      HWND parent /*= NULL*/)
{
	int res = -1;

	const CString keyName(ID_RK_APP + _T("\\Messages"));
	bool shouldDisplay = GetRegBool(HKEY_CURRENT_USER, keyName, regItemName, true);
	if (shouldDisplay)
	{
		if (parent)
			res = WtMessageBox(parent, text, IDS_APPNAME, msgBoxType);
		else
			res = WtMessageBox(text, IDS_APPNAME, msgBoxType);
		SetRegValueBool(HKEY_CURRENT_USER, keyName, regItemName, false);
	}

	return res;
}

void DisableOneTimeMessageBox(LPCTSTR regItemName)
{
	const CString keyName(ID_RK_APP + _T("\\Messages"));
	SetRegValueBool(HKEY_CURRENT_USER, keyName, regItemName, false);
}

void RemoveLeadingWhitespace(const CStringW& lineTxt, CStringW& leadingWhitespace, CStringW& remainderTxt)
{
	const int kLen = lineTxt.GetLength();
	for (int idx = 0; idx < kLen; ++idx)
	{
		WCHAR ch = lineTxt[idx];
		if (remainderTxt.IsEmpty() && (L' ' == ch || L'\t' == ch))
		{
			leadingWhitespace += ch;
		}
		else
		{
			remainderTxt = lineTxt.Mid(idx);
			break;
		}
	}
}

CString EncodeUrlQueryStringArg(const CString& arg)
{
	// https://en.wikipedia.org/wiki/Query_string
	CString retval, tmp;
	const int kLen = arg.GetLength();
	for (int idx = 0; idx < kLen; ++idx)
	{
		TCHAR ch = arg[idx];
		if ((ch >= _T('0') && ch <= _T('9')) || (ch >= _T('a') && ch <= _T('z')) || (ch >= _T('A') && ch <= _T('Z')) ||
		    ch == _T('.') || ch == _T('-') || ch == _T('~') || ch == _T('_'))
		{
			retval += ch;
		}
		else if (ch == _T(' '))
		{
			retval += _T('+');
		}
		else
		{
			CString__FormatA(tmp, _T("%%%02X"), ch);
			retval += tmp;
		}
	}

	return retval;
}

WTString ReadToUnpairedColon(const WTString& str)
{
	WTString tmp;
	int cnt = str.GetTokCount(':');
	if (!cnt)
		return tmp;

	int pos = -1, startPos = 0;
	// walk thru str and find first unpaired colon
	for (;;)
	{
		int p1 = str.Find(":", startPos);
		if (-1 == p1)
			return tmp;

		if (p1 == str.GetLength() - 1)
		{
			pos = p1;
			break;
		}

		if (str.GetAt(p1 + 1) != ':')
		{
			pos = p1;
			break;
		}

		startPos = p1 + 2;
	}

	if (-1 != pos)
	{
		tmp = str.Left(pos);
		tmp.TrimRight();
	}

	return tmp;
}

LPCTSTR
GetDefaultVaWndCls(LPCTSTR clsNameIn /*= nullptr*/)
{
	LPCSTR clsName = clsNameIn && *clsNameIn ? clsNameIn : _T("VaWndCls");
	WNDCLASS wndcls;
	HINSTANCE hInst = AfxGetInstanceHandle();
	if (!(::GetClassInfo(hInst, clsName, &wndcls)))
	{
		ZeroMemory(&wndcls, sizeof(wndcls));
		wndcls.lpfnWndProc = ::DefWindowProc;
		wndcls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		wndcls.hInstance = hInst;
		wndcls.lpszClassName = clsName;
		wndcls.hbrBackground = (HBRUSH)::GetStockObject(NULL_BRUSH);
		AfxRegisterClass(&wndcls);
	}

	return clsName;
}

UINT_PTR CDimmer::timer_id()
{
	static const UINT_PTR id = (uint)::RegisterTimer("DIMMER");
	return id;
}

void CDimmer::StartDimmer(HWND wnd, BYTE min_alpha /*= 70*/, BYTE max_alpha /*= 255*/, DWORD timer_interval /*= 20*/,
                          DWORD dim_steps /*= 10*/, Func fnc /*= nullptr*/)
{
	m_wnd = wnd;
	m_min_alpha = min_alpha;
	m_max_alpha = max_alpha;
	m_step = 0xFF;
	m_fnc = fnc;

	if (dim_steps > 0)
	{
		m_step = (BYTE)((m_max_alpha - m_min_alpha) / dim_steps);
		if (m_step == 0)
			m_step = 1;
	}

	if (m_fnc == nullptr)
	{
		m_fnc = []() -> bool {
			bool ctrl_down = 0 != (::GetKeyState(VK_CONTROL) & 0x8000);
			bool alt_down = 0 != (::GetKeyState(VK_MENU) & 0x8000);
			bool shift_down = 0 != (::GetKeyState(VK_SHIFT) & 0x8000);

			return ctrl_down && !alt_down && !shift_down;
		};
	}

	LONG ex_style = ::GetWindowLong(m_wnd, GWL_EXSTYLE);
	if ((ex_style & WS_EX_LAYERED) != WS_EX_LAYERED)
	{
		m_wasnt_layered = true;
		::SetWindowLong(m_wnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
	}

	::SetLayeredWindowAttributes(m_wnd, 0, m_max_alpha, LWA_ALPHA);

	::SetTimer(m_wnd, timer_id(), timer_interval, nullptr);
}

void CDimmer::StopDimmer(bool remove_layered_flag_if_wasnt_layered /* = false*/)
{
	if (m_wnd)
	{
		if (::IsWindow(m_wnd))
		{
			::KillTimer(m_wnd, timer_id());

			if (m_wasnt_layered && remove_layered_flag_if_wasnt_layered)
				::SetWindowLong(m_wnd, GWL_EXSTYLE, ::GetWindowLong(m_wnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
		}

		m_wnd = nullptr;
		m_min_alpha = 0;
		m_max_alpha = 0;
		m_step = 0;
	}
}

void CDimmer::CallOnTimer(UINT_PTR nTimer)
{
	if (m_wnd == nullptr)
		return;

	if (timer_id() != nTimer)
		return;

	if (!m_fnc)
		return;

	if ((::GetWindowLong(m_wnd, GWL_EXSTYLE) & WS_EX_LAYERED) == WS_EX_LAYERED)
	{
		COLORREF clrKey;
		BYTE alpha;
		DWORD flags;

		if (::GetLayeredWindowAttributes(m_wnd, &clrKey, &alpha, &flags))
		{
			BYTE dst_alpha = m_fnc() ? m_min_alpha : m_max_alpha;

			if ((flags & LWA_ALPHA) == LWA_ALPHA && alpha != dst_alpha)
			{
				if (alpha < dst_alpha)
					alpha = (m_step > 0 && (dst_alpha - alpha) > m_step) ? BYTE(alpha + m_step) : dst_alpha;
				else if (alpha > dst_alpha)
					alpha = (m_step > 0 && (alpha - dst_alpha) > m_step) ? BYTE(alpha - m_step) : dst_alpha;

				SetLayeredWindowAttributes(m_wnd, clrKey, alpha, flags);

				::RedrawWindow(m_wnd, NULL, NULL, RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
			}
		}
	}
}

bool HasDatePassed(CString expDate)
{
	SYSTEMTIME ctm;
	GetLocalTime(&ctm);
	CString curDateStr;
	CString__FormatA(curDateStr, _T("%04d.%02d.%02d"), ctm.wYear, ctm.wMonth, ctm.wDay);
	if (expDate.GetLength() == curDateStr.GetLength())
		return curDateStr.Compare(expDate) > 0;

	return true;
}

bool EndsWith(const CStringW& inStr, const CStringW& findThisEnd)
{
	return (inStr.GetLength() >= findThisEnd.GetLength() &&
	        wcscmp(&inStr.GetString()[inStr.GetLength() - findThisEnd.GetLength()], findThisEnd.GetString()) == 0);
}

bool EndsWithNoCase(const CStringW& inStr, const CStringW& findThisEnd)
{
	return (inStr.GetLength() >= findThisEnd.GetLength() &&
	        _wcsicmp(&inStr.GetString()[inStr.GetLength() - findThisEnd.GetLength()], findThisEnd.GetString()) == 0);
}

void utils3264_ASSERT_LOG(bool is_OK, const char* file_name, int line)
{
	if (!is_OK)
	{
		assert(is_OK);

		if (g_loggingEnabled || gTestsActive)
		{
			// get the base name
			// 			for (const char* x = file_name; x && *x; x++)
			// 				if (*x == '/' || *x == '\\')
			// 					file_name = x + 1;

			// log to file
			if (g_loggingEnabled)
			{
				CString msg;
				msg.Format("ERROR 3264 in source file %s", file_name);
				VALogError(msg, line, TRUE);
			}

			// let current AST test fail
			if (gTestsActive && gTestLogger)
			{
				CString msg;
				msg.Format("ERROR 3264 in source file %s, line %d", file_name, line);
				gTestLogger->LogStr((LPCSTR)msg);
			}
		}
	}
}
