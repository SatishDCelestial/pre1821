#include "stdafxed.h"
#include "log.h"
#include "RegKeys.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "LogVersionInfo.h"
#include "project.h"
#include "file.h"
#include "Registry.h"
#include "mainThread.h"
#include "StatusWnd.h"
#include "Armadillo\Armadillo.h"
#include "Directories.h"
#include "file.h"
#include "WrapCheck.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "VaService.h"
#include "VAAutomation.h"
#include "EDCNT.H"
#include "DllNames.h"
#include "CatLog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::filebuf;
using std::ios;
using std::ofstream;
#endif

//#define INTENSELOG
DWORD getLogging()
{
#if defined(RAD_STUDIO)
	// #cppBRegistry -- note that this logging reg value read reads from WT key because 
	// the read occurs before plugin init
#endif
	DWORD flag = (DWORD)atoi(GetRegValue(HKEY_CURRENT_USER, ID_RK_WT_KEY, "Logging"));
	// turn off for next launch
	SetRegValue(HKEY_CURRENT_USER, ID_RK_WT_KEY, "Logging", "0");
#ifdef _DEBUGxx
	flag = 1;
#endif
	return flag;
}

DWORD g_loggingEnabled = getLogging();

CFileW g_logFile;
CStringW sErrorFilePath;
CStringW sStartupLogFilePath;
//#if (defined INTENSELOG || defined LOGALL)
// DWORD g_loggingEnabled = 0;
//#else
// DWORD g_loggingEnabled = NULL;
//#endif
DWORD g_tlsLogCountIdx = (DWORD)-1;
CCriticalSection sLogLock;

int ErrorBox(const WTString& msg, unsigned int options /*= MB_OK*/)
{
	return ErrorBox(msg.c_str(), options);
}

int ErrorBox(const char* msg, uint options, HWND hWnd)
{
	Log(msg);
	if (!hWnd)
	{
		hWnd = gMainWnd->GetSafeHwnd();
		if (!hWnd)
		{
			// error before we have a real main wnd
			CWnd* mWnd = AfxGetMainWnd();
			if (mWnd)
				hWnd = mWnd->m_hWnd;
		}
	}

	// [case: 18747] don't display messagebox during LoadLibrary due to managed code exceptions (in IDE)
	if (g_mainThread == GetCurrentThreadId() && gShellAttr)
	{
		if (gTestLogger)
		{
			gTestLogger->LogStr(WTString("ErrorBox :") + msg);
			return IDCANCEL;
		}

		return WtMessageBox(hWnd, msg, IDS_APPNAME, options | MB_ICONEXCLAMATION);
	}
#ifdef _DEBUG
	// no message boxes from main thread
	__debugbreak();
#endif // _DEBUG
	return IDCANCEL;
}

//#if defined(VAX_CODEGRAPH)
#ifdef VAX_CODEGRAPH
#define LOGFILENAME L"Spaghetti.log"
#else
#ifdef _DEBUG
#define LOGFILENAME L"va_dbg.log"
#else
#define LOGFILENAME L"va.log"
#endif
#endif

std::once_flag catlogInitFlag;

void InitLogFile(HWND parent /*= NULL*/)
{
	if (!g_logFile.m_hFile || (HANDLE)g_logFile.m_hFile == INVALID_HANDLE_VALUE)
	{
		if (!parent)
			parent = MainWndH;

		AutoLockCs l(sLogLock);
		bool logErr = true;

		if (!g_logFile.m_hFile || (HANDLE)g_logFile.m_hFile == INVALID_HANDLE_VALUE)
		{
			CStringW tempDir(GetTempDir());
			if (!tempDir.IsEmpty())
			{
				CStringW fName = tempDir + L"/" LOGFILENAME;
				fName.Replace(L"\\/", L"\\");

				if (IsFile(fName) && IsFileReadOnly(fName))
				{
					_wchmod(fName, _S_IREAD | _S_IWRITE);
					_wremove(fName);
				}

				try
				{
					g_logFile.Open(fName,
					               CFile::modeCreate | CFile::shareDenyWrite | CFile::modeWrite | CFile::modeNoInherit);
				}
				catch (...)
				{
				}
			}
		}

		if (g_logFile.m_hFile && (HANDLE)g_logFile.m_hFile != INVALID_HANDLE_VALUE)
		{
			DWORD pid = GetCurrentProcessId();
			vLog("Logfile created - level %ld(0x%08lx) PID %ld(0x%08lx)", g_loggingEnabled, g_loggingEnabled, pid, pid);
			g_logFile.Flush();
			logErr = false;
		}

		if (logErr)
		{
			ErrorBox("Error creating " IDS_VAX_DLL " log file.", 0, parent);
		}
		else
		{
			std::call_once(catlogInitFlag, InitCategories);
			LogVersionInfo(true);
			VaDirs::LogDirs();

			{
				EdCntPtr ed(g_currentEdCnt);
				if (ed)
					vCatLog("Editor", "CurEd %s", (LPCTSTR)CString(ed->FileName()));
			}

			if (Psettings && GlobalProject)
				GlobalProject->DumpToLog();

			if (gVaInteropService)
				gVaInteropService->SetLoggingEnabled(!!g_loggingEnabled);
		}
	}

	if (g_tlsLogCountIdx == -1)
		g_tlsLogCountIdx = TlsAlloc();
}


void UnInitLogFile()
{
	if (gVaInteropService)
		gVaInteropService->SetLoggingEnabled(false);
	if (g_logFile.m_hFile && (HANDLE)g_logFile.m_hFile != INVALID_HANDLE_VALUE)
	{
		g_logFile.Close();
	}
	if (g_tlsLogCountIdx != -1)
	{
		TlsFree(g_tlsLogCountIdx);
		g_tlsLogCountIdx = (DWORD)-1;
	}
}

static void AppendToFile(const CStringW& fname, const CString& msg)
{
	TRY
	{
		CFileW outFile;
		if (outFile.Open(fname, CFile::shareDenyNone | CFile::modeCreate | CFile::modeWrite | CFile::modeNoTruncate |
		                            CFile::typeBinary | CFile::modeNoInherit))
		{
			outFile.SeekToEnd();
			outFile.Write(msg, (UINT)msg.GetLength());
			outFile.Close();
		}
	}
	CATCH(CFileException, pEx)
	{
		CString exMsg;
		CString__FormatA(exMsg, "VA ERROR exception caught during log: %d\n", __LINE__);
		OutputDebugString(exMsg);
	}
	END_CATCH
}

static void BackupLogFile(const CStringW& filepath, bool forceBackup = false);

// errors.log is stored in db dir
static void LogToErrorFile(const CString& msg, bool doGeneralLog = true)
{
	static bool once = true;
	if (once)
	{
		AutoLockCs l(sLogLock);
		if (once)
		{
			sErrorFilePath = VaDirs::GetDbDir() + L"/errors.log";
			BackupLogFile(sErrorFilePath, true);
			once = false;
		}
	}

	if (g_loggingEnabled && doGeneralLog)
	{
		if (!g_logFile.m_hFile || (HANDLE)g_logFile.m_hFile == INVALID_HANDLE_VALUE)
			InitLogFile();
		Log((const char*)msg);
	}

	if (sErrorFilePath.GetLength())
	{
		static int sCount = 0;
		if (++sCount > 10000)
		{
			// [case: 74931] cap size by simple line count check
			sCount = 1;
			BackupLogFile(sErrorFilePath);
		}
		AppendToFile(sErrorFilePath, msg);
	}

#ifdef _DEBUG
	MessageBeep(0xffffffff);
	SetStatus(msg);
	OutputDebugString("VA ERROR: ");
	OutputDebugString(msg);
	OutputDebugString("\n");
#endif // _DEBUG
#if defined(VA_CPPUNIT)
	std::cout << msg;
#endif
}

// startup.log is stored in users local data dir
static void LogToStartupFile(const CString& msgPassed)
{
	try
	{
		CString msg(msgPassed);
		static bool once = true;
		if (once && VaDirs::GetUserLocalDir().GetLength())
		{
			AutoLockCs l(sLogLock);
			if (once)
			{
#if defined(VAX_CODEGRAPH)
				sStartupLogFilePath = VaDirs::GetUserLocalDir() + L"SpaghettiStartup.log";
#else
				sStartupLogFilePath = VaDirs::GetUserLocalDir() + L"Startup.log";
#endif
				BackupLogFile(sStartupLogFilePath);
				once = false;
			}
		}

		if (g_loggingEnabled)
		{
			if (!g_logFile.m_hFile || (HANDLE)g_logFile.m_hFile == INVALID_HANDLE_VALUE)
				InitLogFile();
			Log((const char*)msg);
		}

		if (sStartupLogFilePath.GetLength())
		{
			static int sCount = 0;
			if (++sCount > 10000)
			{
				// [case: 74931] cap size by simple line count check
				sCount = 1;
				BackupLogFile(sStartupLogFilePath);
			}
			AppendToFile(sStartupLogFilePath, msgPassed);
		}
	}
	catch (...)
	{
	}
}

void VALogError(LPCSTR errMsg, int line, BOOL isError)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	CString msg;
	CString__FormatA(msg, "%s:%d %d/%d/%d %02d:%02d:%02d 0x%lx\r\n", errMsg, line, st.wMonth, st.wDay, st.wYear,
	                 st.wHour, st.wMinute, st.wSecond, GetCurrentThreadId());
	LogToStartupFile(msg);
	if (isError)
		LogToErrorFile(msg, false);
}

void LogAssertFailure(LPCSTR failedCondition, LPCSTR msg, int line)
{
	const DWORD err = ::GetLastError();
	CString str;
	CString__FormatA(str, "Assert (%s) failed (%s:%d) (lastErr %ld 0x%lx)\r\n", failedCondition, msg, line, err, err);
	LogToErrorFile(str);
}

void LogAssertFailure1(LPCSTR failedCondition, LPCSTR msg, LPCSTR info, int line)
{
	const DWORD err = ::GetLastError();
	CString str;
	CString__FormatA(str, "Assert (%s) failed (%s:%d) (%s) (lastErr %ld 0x%lx)\r\n", failedCondition, msg, line, info,
	                 err, err);
	LogToErrorFile(str);
}

void MyLog(const char* __fmt, ...)
{
	static constexpr size_t buffer_extra_size = 256u;
	char msg[3000 + buffer_extra_size]; // reduce likelihood of chkstk being called; note 512 buffer further down too
	size_t msgLen = 0;

	try
	{
		va_list args;
		va_start(args, __fmt);
		vsnprintf_s(msg, sizeof(msg) - buffer_extra_size,  _TRUNCATE, __fmt, args);
		va_end(args);
	}
	catch (...)
	{
#ifdef _DEBUG
		static int once = TRUE;
		if (once)
			WtMessageBox("Exception caught in MyLog", "VA DbgMsg", MB_OK);
		once = FALSE;
#endif
		strcpy(msg, "ERROR: Exception caught in MyLog\n");
	}

	msgLen = strlen(msg);
	// add newline only if no newline already and msg doesn't begin with a space or period
	if (msg[0] != ' ' && msg[0] != '.' && msg[msgLen - 1] != '\n')
	{
		strcat(msg, "\n");
		msgLen++;
	}

	if (g_logFile.m_hFile && (HANDLE)g_logFile.m_hFile != INVALID_HANDLE_VALUE)
	{
		static DWORD ltm = GetTickCount();
		DWORD tm = GetTickCount();
		char tbuf[512];
		if (tm - ltm)
		{
			constexpr int catlog_id = GetIdByString("LogSystem.TimeTracking");
			if (gLoggingCategories.test(catlog_id))
			{
				sprintf(tbuf, "LogSystem.TimeTracking (%ld t)\n", tm - ltm);
				strcat(msg, tbuf);
				msgLen += strlen(tbuf);
			}
		}
		else if (ltm > tm)
		{
			// unlikely to ever happen (but could).
			// just wanted to reference SECURITY_STRING_NAME value in actual logging code
			if (gArmadilloSecurityStringValue)
			{
				sprintf(tbuf, " (%ld %ld t)\n", tm, ltm);
				strcat(msg, tbuf);
				msgLen += strlen(tbuf);
			}
		}

//  		if (strstr(msg, "c:\\users\\accord\\appdata\\local\\microsoft\\visualstudio\\17.0_0e198e90exp\\extensions\\whole tomato software\\visual assist\\10.9.2112.0\\VaNetObjMD64.dll") != nullptr)
//  			int a = 5;

		g_logFile.Write(msg, (uint)msgLen);
		ltm = tm;
#ifdef INTENSELOG
		g_logFile.Flush();
#endif
	}
	if (strchr(msg, '%') == NULL) // AfxOutputDebugString(" '%' ") will throw an exception trying to expand %'
	{
#ifdef _DEBUG
		AfxOutputDebugString("%s", msg);
#else
		AfxOutputDebugString(msg);
#endif
	}
}

DWORD LSetup(DWORD level)
{
	DWORD retval;
	if (level)
	{
		retval = g_loggingEnabled;
		g_loggingEnabled = level;
		if (!retval)
			InitLogFile();
	}
	else
	{
		retval = g_loggingEnabled;
		g_loggingEnabled = 0;
		UnInitLogFile();
	}
	return retval;
}

void BackupLogFile(const CStringW& filepath, bool forceBackup /*= false*/)
{
	const CStringW filepathBak = Path(filepath) + L"\\" + GetBaseNameNoExt(filepath) + L".bak";

	if (IsFile(filepathBak) && IsFile(filepath) && (GetFSize(filepath) > 500000 || forceBackup))
		DeleteFileW(filepathBak);

	if (IsFile(filepath))
		MoveFileW(filepath, filepathBak);
}

void LogFunc::Init()
{
	if (g_loggingEnabled < 2)
		return;
	if (g_tlsLogCountIdx != -1)
	{
		m_logCnt = (DWORD)(uintptr_t)TlsGetValue(g_tlsLogCountIdx) + 1;
		TlsSetValue(g_tlsLogCountIdx, (LPVOID)(uintptr_t)m_logCnt);
	}
	else
		m_logCnt = 0;
	tick = GetTickCount();
	m_thrdID = GetCurrentThreadId();
	constexpr int catlog_id = GetIdByString("LogSystem.LogFunc");
	if (gLoggingCategories.test(catlog_id))
		for (DWORD i = m_logCnt; i; i--)
			MyLog(LOG_SPACING);
	vCatLog("LogSystem.LogFunc", "Start %s 0x%08lx\n", (LPCTSTR)name, m_thrdID);
}

LogFunc::~LogFunc()
{
	if (g_loggingEnabled < 2)
		return;
	constexpr int catlog_id = GetIdByString("LogSystem.LogFunc");
	if (m_logCnt > 0 && m_logCnt < 10)
		if (gLoggingCategories.test(catlog_id))
			for (DWORD i = m_logCnt; i; i--)
				Log(LOG_SPACING);
	if (g_tlsLogCountIdx != -1)
	{
		m_logCnt--;
		TlsSetValue(g_tlsLogCountIdx, (LPVOID)(uintptr_t)m_logCnt);
	}
	vCatLog("LogSystem.LogFunc", "End %s 0x%08lx %ld\n", (LPCTSTR)name, m_thrdID, GetTickCount() - tick);
}
